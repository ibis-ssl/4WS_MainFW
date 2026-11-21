/* 電源の警告・復帰と、モードスイッチで選んだ単発ベンチ操作を管理する。 */
#include "App/app_power_features.h"
#include "App/app_board_reset.h"
#include "App/app_motor_debug.h"
#include "Device/buzzer.h"
#include "Device/power_board.h"
#include "Device/user_switch.h"
#include "Protocol/orion_telemetry.h"
#include <string.h>

enum { DRIBBLER_MODE = 4, KICKER_AUTO = 5, KICKER_MANUAL = 6, DEBOUNCE_MS = 30,
       TELEMETRY_MS = 200, HOLD_RESET_MS = 500, ARM_LIMIT_MS = 15000, KICK_INTERVAL_MS = 500 };
static uint32_t key_at, mode_at, armed_at, drive_at, last_kick, warn_at;
static uint8_t mode, notified;
static user_switch_t key;
static bool neutral, press_used, armed, dribbler_active, dribbler_zero_sent, reset_sent;
static struct { bool seen; uint32_t at; int16_t dx, dy; uint16_t quality; } mouse;
static struct { bool seen; uint32_t at; float rps; } dribbler;
static struct { bool detected; uint32_t at; } ball;

void app_power_features_start(uint32_t now)
{
    mode = UINT8_MAX;
    key = USER_SWITCH_INVALID;
    key_at = mode_at = now;
    last_kick = now - KICK_INTERVAL_MS;
    warn_at = now;
    neutral = press_used = armed = dribbler_active = dribbler_zero_sent = reset_sent = false;
    notified = 0;
}

bool app_power_features_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    uint16_t id = frame->id;
    if (!((bus == POWER_BOARD_CAN_BUS && id == 0x241U) ||
          (bus == BOARD_CAN_1 && id == 0x204U) || id == 0x240U)) { return false; }
    orion_value_t value;
    if (!orion_telemetry_decode(id, frame->data, frame->length, &value)) { return true; }
    if (id == 0x241U) {
        mouse.seen = true; mouse.at = frame->received_ms;
        mouse.dx = (int16_t)value.integer[0]; mouse.dy = (int16_t)value.integer[1]; mouse.quality = (uint16_t)value.integer[2];
    } else if (id == 0x204U) {
        dribbler.seen = true; dribbler.at = frame->received_ms; dribbler.rps = value.value[0];
    } else {
        ball.detected = value.integer[0] == 1;
        ball.at = frame->received_ms;
    }
    return true;
}

bool app_power_features_mouse(uint32_t now, int16_t *dx, int16_t *dy, uint16_t *quality)
{
    *dx = mouse.dx; *dy = mouse.dy; *quality = mouse.quality;
    return mouse.seen && now - mouse.at < TELEMETRY_MS;
}

bool app_power_features_dribbler(uint32_t now, float *rps)
{
    *rps = dribbler.rps;
    return dribbler.seen && now - dribbler.at < TELEMETRY_MS;
}

bool app_power_features_bench_idle(void) { return !armed && !dribbler_active; }

static bool send_dribbler(float duty)
{
    uint32_t bits;
    memcpy(&bits, &duty, sizeof(bits));
    const uint8_t data[8] = {(uint8_t)bits, (uint8_t)(bits >> 8), (uint8_t)(bits >> 16), (uint8_t)(bits >> 24)};
    return board_can_send_immediate(BOARD_CAN_1, 0x104U, data, 8);
}

void app_power_features_publish_fault(void)
{
    if (!power_board_supply_fault()) { return; }
    const uint8_t error[8] = {0};
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        if (!(notified & (1U << i)) && board_can_send_immediate((board_can_bus_t)i, 0x000U, error, 8)) {
            notified |= (uint8_t)(1U << i);
        }
    }
}

static void warnings(uint32_t now)
{
    power_board_status_t s;
    (void)power_board_get_status(POWER_BOARD_CAN_BUS, &s);
    bool hot = s.temperature_valid && now - s.temperature_ms < TELEMETRY_MS &&
               (s.fet_temp > 70U || s.coil_temp[0] > 70U || s.coil_temp[1] > 70U);
    bool low = power_board_battery_fresh(now) && s.battery_voltage < 22.5f;
    if ((hot || low) && now - warn_at >= 2000U) {
        warn_at = now;
        (void)buzzer_start(hot ? 2200U : 1400U, 80U);
    }
}

void app_power_features_process(uint32_t now, bool update_locked)
{
    uint8_t selected = user_mode_code();
    user_switch_t pressed = user_switch_read();
    bool mode_changed = selected != mode;
    if (key != pressed) { key = pressed; key_at = now; press_used = false; reset_sent = false; }
    if (mode_changed) { mode = selected; mode_at = now; neutral = false; }
    bool fault = app_motor_debug_status() == MOTOR_DEBUG_FAULT || power_board_supply_fault();
    if (!update_locked) { warnings(now); }
    if (!update_locked && fault && key == USER_SWITCH_CENTER && now - key_at >= HOLD_RESET_MS && !reset_sent) {
        reset_sent = power_board_request_reset() != 0U;
    }
    bool interlock = update_locked || fault || app_board_reset_status() != BOARD_RESET_READY ||
                     !power_board_battery_fresh(now) || !power_board_supply_on() || pressed == USER_SWITCH_INVALID;
    bool kicker_mode = selected == KICKER_AUTO || selected == KICKER_MANUAL;
    power_board_status_t power;
    (void)power_board_get_status(POWER_BOARD_CAN_BUS, &power);
    bool kicker_ready = power.capacitor_valid && now - power.capacitor_ms < TELEMETRY_MS &&
                        power.capacitor_voltage <= 110.0f && power.flags_valid &&
                        now - power.flags_ms < TELEMETRY_MS && (power.flags & 0x18U) == 0x18U;
    if (interlock || mode_changed || !kicker_mode || !kicker_ready || (armed && now - armed_at >= ARM_LIMIT_MS)) {
        if (armed) { neutral = false; }
        armed = false;
        power_board_test_stop();
    }
    float rps;
    if (dribbler.seen && !dribbler_zero_sent) { dribbler_zero_sent = send_dribbler(0.0f); }
    bool drive = !interlock && dribbler_zero_sent && selected == DRIBBLER_MODE && neutral && pressed == USER_SWITCH_CENTER &&
                 app_power_features_dribbler(now, &rps);
    if (!drive && dribbler_active) {
        if (pressed == USER_SWITCH_CENTER) { neutral = false; }
        board_can_cancel_immediate_id(BOARD_CAN_1, 0x104U);
        if (send_dribbler(0.0f)) { dribbler_active = false; }
    }
    if (interlock || now - mode_at < DEBOUNCE_MS || now - key_at < DEBOUNCE_MS) {
        if (interlock) { neutral = false; }
        return;
    }
    if (pressed == USER_SWITCH_NONE) { neutral = true; return; }
    if (!neutral) { return; }
    bool ball_present = ball.detected && now - ball.at < TELEMETRY_MS;
    if (drive && (!dribbler_active || now - drive_at >= 20U)) {
        if (send_dribbler(ball_present ? 0.5f : 0.2f)) { dribbler_active = true; drive_at = now; }
        else { neutral = false; }
    }
    if (!kicker_mode || !kicker_ready || press_used) { return; }
    press_used = true;
    if (pressed == USER_SWITCH_FORWARD && !armed) {
        armed = power_board_test_charge();
        armed_at = now;
    } else if (armed && (selected == KICKER_MANUAL || ball_present) &&
               (pressed == USER_SWITCH_CENTER || pressed == USER_SWITCH_BACK) && now - last_kick >= KICK_INTERVAL_MS) {
        if (power_board_test_kick(pressed == USER_SWITCH_BACK, now)) { last_kick = now; }
    }
}
