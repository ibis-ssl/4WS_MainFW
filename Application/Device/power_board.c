/* 4WSで定めた電源バスに旧形式の電源管理指令を送る。 */
#include "Device/power_board.h"
#include <stddef.h>
static power_board_status_t statuses[BOARD_CAN_COUNT];
enum { SUPPLY_PERIOD_MS = 20, POWER_SWITCH_ID = 0x010, HIGH_VOLTAGE_ID = 0x110, HIGH_VOLTAGE_FLAGS = 0x07 };
static enum { SUPPLY_OFF, SUPPLY_ARMING, SUPPLY_ON } supply;
static uint32_t requested_ms, kick_at;
static uint8_t configured;
static bool immediate_off, high_voltage_fault, test_charging, kick_pending;

static bool send_kicker(const uint8_t data[8])
{
    return board_can_send_immediate(POWER_BOARD_CAN_BUS, HIGH_VOLTAGE_ID, data, 8);
}
static uint8_t send_power(uint16_t id, const uint8_t data[8])
{
    return board_can_send(POWER_BOARD_CAN_BUS, id, data) ? 1U : 0U;
}
uint8_t power_board_set_on(bool on)
{
    uint8_t data[8]; power_packet_on(on, data); return send_power(0x010, data);
}
uint8_t power_board_set_param(power_param_t param, float value)
{
    uint8_t data[8];
    if (!power_packet_param(param, value, data)) { return 0; }
    return send_power(0x010, data);
}
uint8_t power_board_request_reset(void)
{
    uint8_t data[8];
    power_packet_reset(data);
    if (!board_can_send_immediate(POWER_BOARD_CAN_BUS, 0x001U, data, 8)) { return 0U; }
    power_board_reconfigure();
    return 1U;
}

void power_board_reconfigure(void) { configured = 0; }

void power_board_supply_start(uint32_t now)
{
    supply = SUPPLY_OFF;
    requested_ms = now;
    immediate_off = true;
    high_voltage_fault = false;
    test_charging = kick_pending = false;
    configured = 0;
}

bool power_board_configure_step(void)
{
    /* Orionの初期値。電源FW固有の固定保護は上書きしない。 */
    static const float defaults[] = {15.0f, 35.0f, 50.0f, 90.0f, 90.0f};
    if (configured == sizeof(defaults) / sizeof(defaults[0])) { return true; }
    uint8_t data[8];
    if (power_packet_param((power_param_t)(POWER_PARAM_MIN_VOLTAGE + configured), defaults[configured], data) &&
        board_can_send_immediate(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID, data, 8)) { configured++; }
    return configured == sizeof(defaults) / sizeof(defaults[0]);
}

bool power_board_supply_on(void) { return supply == SUPPLY_ON && !power_board_supply_fault(); }

bool power_board_battery_fresh(uint32_t now)
{
    const power_board_status_t *s = &statuses[POWER_BOARD_CAN_BUS];
    return s->battery_valid && now - s->battery_ms < 200U;
}

bool power_board_test_charge(void)
{
    if (!power_board_supply_on()) { return false; }
    uint8_t data[8];
    power_packet_set_charge_voltage(100.0f, data);
    if (!send_kicker(data)) { return false; }
    test_charging = true;
    power_packet_kicker(1U, 1U, data);
    if (!send_kicker(data)) { power_board_test_stop(); return false; }
    return true;
}

bool power_board_test_kick(bool chip, uint32_t now)
{
    const power_board_status_t *s = &statuses[POWER_BOARD_CAN_BUS];
    if (!power_board_supply_on() || !test_charging || !s->capacitor_valid || now - s->capacitor_ms >= 200U ||
        s->capacitor_voltage < 90.0f || s->capacitor_voltage > 110.0f) { return false; }
    uint8_t data[8];
    power_packet_kicker(2U, chip ? 1U : 0U, data);
    if (!send_kicker(data)) { return false; }
    power_packet_kicker(3U, 127U, data);
    if (!send_kicker(data)) { power_board_test_stop(); return false; }
    kick_at = now;
    kick_pending = true;
    return true;
}

void power_board_test_stop(void)
{
    if (!test_charging) { return; }
    board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, HIGH_VOLTAGE_ID);
    board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID);
    test_charging = kick_pending = false;
    supply = SUPPLY_OFF;
    immediate_off = true;
}

void power_board_supply_process(uint32_t now, bool allowed)
{
    /* 電源FWの100周期パルスと状態通知の遅れを見込み、次の操作より前に許容を閉じる。 */
    if (kick_pending && now - kick_at >= 250U) {
        board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, HIGH_VOLTAGE_ID);
        kick_pending = false;
    }
    bool on = allowed && !power_board_supply_fault();
    uint8_t data[8];
    if (!on && supply != SUPPLY_OFF) {
        power_board_test_stop();
        supply = SUPPLY_OFF;
        immediate_off = true;
    }
    if (on && supply == SUPPLY_OFF) {
        /* 充電停止は電源FWの給電タイマーも解除する。PowerOnより前に送る。 */
        power_packet_stop_charge(data);
        if (send_kicker(data)) {
            supply = SUPPLY_ARMING;
            requested_ms = now;
            immediate_off = false;
        }
        return;
    }
    if (!immediate_off && now - requested_ms < SUPPLY_PERIOD_MS) { return; }
    /* ACK待ちで滞った前回分を残さず、最新の許可状態だけを直送する。 */
    board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID);
    power_packet_on(on, data);
    if (!board_can_send_immediate(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID, data, 8)) { return; }
    if (on && supply == SUPPLY_ARMING) {
        supply = SUPPLY_ON;
    }
    requested_ms = now;
    immediate_off = false;
}

const char *power_board_supply_status(void)
{
    if (power_board_supply_fault()) { return "FLT"; }
    static const char *const labels[] = {"OFF", "ARM", "ON"};
    return labels[supply];
}
bool power_board_supply_fault(void)
{
    return statuses[POWER_BOARD_CAN_BUS].fault_seen || high_voltage_fault;
}
bool power_board_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (bus != POWER_BOARD_CAN_BUS || frame == NULL) { return false; }
    power_packet_value_t value;
    if (!power_packet_decode(frame->id, frame->data, frame->length, &value)) { return false; }
    power_board_status_t *s = &statuses[bus];
    switch (value.kind) {
    case POWER_RX_BATTERY:
        s->battery_voltage = value.value; s->battery_ms = frame->received_ms; s->battery_valid = true; break;
    case POWER_RX_CAPACITOR:
        s->capacitor_voltage = value.value; s->capacitor_ms = frame->received_ms; s->capacitor_valid = true; break;
    case POWER_RX_TEMPERATURE:
        s->fet_temp = value.fet_temp; s->coil_temp[0] = value.coil_temp[0]; s->coil_temp[1] = value.coil_temp[1];
        s->temperature_ms = frame->received_ms; s->temperature_valid = true; break;
    case POWER_RX_CURRENT:
        s->battery_current = value.value; s->current_ms = frame->received_ms; s->current_valid = true; break;
    case POWER_RX_FLAGS:
        s->flags = value.flags; s->flags_ms = frame->received_ms; s->flags_valid = true;
        uint8_t permitted = test_charging ? (uint8_t)(kick_pending ? HIGH_VOLTAGE_FLAGS : 0x05U) : 0U;
        if (supply == SUPPLY_ON && (value.flags & HIGH_VOLTAGE_FLAGS & ~permitted) != 0U) { high_voltage_fault = true; }
        break;
    case POWER_RX_FAULT:
        s->fault_code = value.fault_code; s->fault_value = value.value; s->fault_seen = true; break;
    }
    return true;
}
bool power_board_get_status(board_can_bus_t bus, power_board_status_t *status)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || status == NULL) { return false; }
    *status = statuses[bus]; return true;
}
