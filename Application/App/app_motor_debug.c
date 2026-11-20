#include "App/app_motor_debug.h"
#include "Board/board_gpio.h"
#include "Board/board_time.h"
#include "Device/user_switch.h"
#include "Protocol/atlas_motor_packet.h"
#include <math.h>

enum {
    WHEEL_COUNT = 4, ALL_WHEELS = WHEEL_COUNT, DEBUG_MODE_CODE = 3,
    DEBOUNCE_MS = 30, ATLAS_PERIOD_MS = 2, STATE_PERIOD_MS = 100, DRIVE_PERIOD_MS = 10,
    ATLAS_TIMEOUT_MS = 150, CALIBRATION_TIMEOUT_MS = 2500, DRIVE_TIMEOUT_MS = 50,
    LOOP_TIMEOUT_MS = 100, INITIALIZE_TIMEOUT_MS = 30000,
    TARGET_MRAD = 262, TARGET_ROTOR_RPM = 300, STOPPED_RPM = 40, MAX_ROTOR_RPM = 18000
};

typedef struct {
    c610_feedback_t feedback;
    uint32_t received_ms;
    uint8_t samples;
    float filtered_rpm;
    float integral;
} wheel_t;

typedef struct {
    atlas_axis_feedback_t axes[2];
    atlas_axis_calibration_t calibration[2];
    uint32_t feedback_ms, calibration_ms;
    bool seen, calibrated_message;
} steering_t;

static wheel_t wheels[WHEEL_COUNT];
static steering_t steering[BOARD_CAN_COUNT];
static unsigned int selected;
static motor_debug_state_t motion;
static user_switch_t raw_key, running_key;
static bool raw_mode, raw_select, mode_active, neutral_seen;
static bool fault, fault_handled, interrupted;
static uint8_t init_waiting, init_observed;
static uint8_t stop_repeats[BOARD_CAN_COUNT];
static uint32_t mode_at, input_at, motion_at, last_process, last_state, last_angle, last_drive, last_stop;
static float ramped_rpm;

/* CAN1は基板0とC610 ID 1/2、CAN2は基板1とC610 ID 3/4。 */
static unsigned int selected_mask(void)
{
    return selected == ALL_WHEELS ? 0xFU : 1U << selected;
}

static unsigned int board_mask(unsigned int bus)
{
    return 3U << (2U * bus);
}

static bool fresh(uint32_t now, uint32_t at, uint32_t timeout)
{
    return now - at < timeout;
}

bool app_motor_debug_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (frame->id == 0x000U && frame->length == 8U) {
        /* 共通エラーは両バスの出力を止める。既存の電源・ステア契約に従い反射送信しない。 */
        fault = true;
        return true;
    }
    const unsigned int first = 2U * bus;
    const uint32_t at = frame->received_ms;
    steering_t *board = &steering[bus];
    if (frame->id == 0x230U + first) {
        if (frame->length != 8U) { return true; }
        if (board->seen && !fresh(at, board->feedback_ms, ATLAS_TIMEOUT_MS)) {
            board->calibrated_message = false;
            if ((selected_mask() & board_mask(bus)) != 0U) { interrupted = true; }
        }
        atlas_decode_feedback(frame->data, board->axes);
        board->feedback_ms = at;
        board->seen = true;
        if (motion == MOTOR_DEBUG_INIT) {
            for (unsigned int axis = 0; axis < 2U; ++axis) {
                if (board->axes[axis].angle_mrad == INT16_MIN) {
                    init_observed |= (uint8_t)(1U << (first + axis));
                }
            }
        }
        return true;
    }
    if (frame->id == 0x510U + first) {
        if (frame->length != 8U || !atlas_decode_calibration(frame->data, board->calibration)) { return true; }
        board->calibration_ms = at;
        board->calibrated_message = true;
        for (unsigned int axis = 0; axis < 2U; ++axis) {
            const atlas_axis_calibration_t *cal = &board->calibration[axis];
            uint8_t bit = (uint8_t)(1U << (first + axis));
            if (cal->state == 6U) { fault = true; }
            if (motion != MOTOR_DEBUG_INIT || (init_waiting & bit) == 0U) { continue; }
            if (cal->state == 2U) { init_observed |= bit; }
            if ((init_observed & bit) != 0U && cal->initialized && cal->state == 3U) {
                init_waiting &= (uint8_t)~bit;
                last_state = at - STATE_PERIOD_MS;
            }
        }
        return true;
    }
    if (frame->id == 0x290U + first) { return true; }
    if (frame->id >= 0x201U && frame->id <= 0x204U) {
        const unsigned int index = frame->id - 0x201U;
        c610_feedback_t feedback;
        if (index / 2U != bus || frame->length != 8U || !c610_decode_feedback(frame->data, &feedback)) {
            return true;
        }
        wheel_t *wheel = &wheels[index];
        bool continuous = wheel->samples != 0U && fresh(at, wheel->received_ms, DRIVE_TIMEOUT_MS);
        if (motion == MOTOR_DEBUG_DRIVE && (selected_mask() & (1U << index)) != 0U &&
            (!continuous || feedback.rpm > MAX_ROTOR_RPM || feedback.rpm < -MAX_ROTOR_RPM)) {
            interrupted = true;
        }
        wheel->feedback = feedback;
        wheel->received_ms = at;
        if (!continuous) {
            wheel->samples = 1U;
            wheel->filtered_rpm = feedback.rpm;
        } else {
            if (wheel->samples < 3U) { wheel->samples++; }
            wheel->filtered_rpm += 0.5f * (feedback.rpm - wheel->filtered_rpm);
        }
        return true;
    }
    /* 反対側用のAtlas IDもOrionのfloatとして解釈しない。 */
    return frame->id == 0x230U || frame->id == 0x232U ||
           frame->id == 0x510U || frame->id == 0x512U ||
           frame->id == 0x290U || frame->id == 0x292U || frame->id == 0x200U;
}

static bool motor_mode_selected(void)
{
    unsigned int code = 0;
    for (unsigned int bit = 0; bit < 4U; ++bit) {
        bool high;
        if (!board_gpio_read_input((board_gpio_input_t)(BOARD_GPIO_INPUT_DIP_0 + bit), &high)) { return false; }
        if (!high) { code |= 1U << bit; }
    }
    return code == DEBUG_MODE_CODE;
}

static uint16_t state_id(unsigned int bus)
{
    return (uint16_t)(0x350U + 2U * bus);
}

static bool any_stop_pending(void)
{
    return stop_repeats[BOARD_CAN_1] || stop_repeats[BOARD_CAN_2];
}

static void begin_stop(uint32_t now)
{
    motion = MOTOR_DEBUG_OFF;
    neutral_seen = false;
    last_stop = now - DRIVE_PERIOD_MS;
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_cancel_immediate((board_can_bus_t)bus);
        stop_repeats[bus] = 3U;
    }
}

static void send_stops(uint32_t now)
{
    if (!any_stop_pending() || fresh(now, last_stop, DRIVE_PERIOD_MS)) { return; }
    last_stop = now;
    const uint8_t zeros[8] = {0};
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        if (stop_repeats[bus] == 0U) { continue; }
        bool drive = board_can_send_immediate((board_can_bus_t)bus, 0x200U, zeros, 8);
        bool steer = board_can_send_immediate((board_can_bus_t)bus, state_id(bus), zeros, 2);
        if (drive && steer) { stop_repeats[bus]--; }
    }
}

static bool motion_ready(motor_debug_state_t action, uint32_t now, bool starting)
{
    unsigned int mask = selected_mask();
    for (unsigned int i = 0; i < WHEEL_COUNT; ++i) {
        if ((mask & (1U << i)) == 0U) { continue; }
        const steering_t *board = &steering[i / 2U];
        const atlas_axis_calibration_t *cal = &board->calibration[i % 2U];
        if (!board->seen || !fresh(now, board->feedback_ms, ATLAS_TIMEOUT_MS) ||
            !board->calibrated_message || !fresh(now, board->calibration_ms, CALIBRATION_TIMEOUT_MS)) {
            return false;
        }
        if ((action != MOTOR_DEBUG_INIT || starting) && cal->state == 2U) { return false; }
        if ((action == MOTOR_DEBUG_STEER || (action == MOTOR_DEBUG_DRIVE && selected == ALL_WHEELS)) &&
            (!cal->initialized || board->axes[i % 2U].angle_mrad == INT16_MIN)) { return false; }
        if (action == MOTOR_DEBUG_DRIVE) {
            const wheel_t *wheel = &wheels[i];
            int rpm = wheel->feedback.rpm;
            if (wheel->samples < 3U || !fresh(now, wheel->received_ms, DRIVE_TIMEOUT_MS) ||
                rpm > MAX_ROTOR_RPM || rpm < -MAX_ROTOR_RPM ||
                (starting && (rpm > STOPPED_RPM || rpm < -STOPPED_RPM ||
                              fabsf(wheel->filtered_rpm) > STOPPED_RPM))) { return false; }
        }
    }
    return true;
}

static void begin_motion(motor_debug_state_t action, user_switch_t key, uint32_t now)
{
    motion = action;
    running_key = key;
    motion_at = now;
    last_state = now - STATE_PERIOD_MS;
    last_angle = now - ATLAS_PERIOD_MS;
    last_drive = now;
    interrupted = false;
    init_waiting = (uint8_t)selected_mask();
    init_observed = 0;
    ramped_rpm = 0.0f;
    for (unsigned int i = 0; i < WHEEL_COUNT; ++i) {
        wheels[i].integral = 0.0f;
        if (!steering[i / 2U].calibration[i % 2U].initialized) {
            init_observed |= (uint8_t)(1U << i);
        }
    }
}

static float clamp(float value, float limit)
{
    return fmaxf(-limit, fminf(limit, value));
}

static int16_t drive_current(wheel_t *wheel, float dt)
{
    /* PCと同じ回転数PI・フィードフォワード。積分が飽和を深めるときだけ更新を止める。 */
    float error = ramped_rpm - wheel->filtered_rpm;
    float candidate = clamp(wheel->integral + 0.002f * error * dt, 0.25f);
    float ff = copysignf(0.13f + 0.0000273f * fabsf(ramped_rpm), ramped_rpm);
    float unsaturated = ff + 0.00105f * error + candidate;
    float output = clamp(unsaturated, 0.5f);
    if (output == unsaturated || output * error <= 0.0f) { wheel->integral = candidate; }
    return (int16_t)lroundf(output * 1000.0f);
}

static bool send_drive(uint32_t now)
{
    const float dt = (float)(now - last_drive) * 0.001f;
    last_drive = now;
    const float target = running_key == USER_SWITCH_FORWARD ? TARGET_ROTOR_RPM : -TARGET_ROTOR_RPM;
    ramped_rpm += clamp(target - ramped_rpm, 300.0f * dt);
    unsigned int mask = selected_mask();
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        if ((mask & board_mask(bus)) == 0U) { continue; }
        uint8_t data[8] = {0};
        for (unsigned int i = 2U * bus; i < 2U * bus + 2U; ++i) {
            if ((mask & (1U << i)) != 0U) { c610_encode_current(data, i, drive_current(&wheels[i], dt)); }
        }
        if (!board_can_send_immediate((board_can_bus_t)bus, 0x200U, data, 8)) { return false; }
    }
    return true;
}

static bool send_steering(uint32_t now)
{
    bool control_due = !fresh(now, last_state, STATE_PERIOD_MS);
    bool angle_due = motion == MOTOR_DEBUG_STEER && !fresh(now, last_angle, ATLAS_PERIOD_MS);
    unsigned int mask = selected_mask();
    if (control_due) { last_state = now; }
    if (angle_due) { last_angle = now; }
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        if ((mask & board_mask(bus)) == 0U) { continue; }
        unsigned int first = 2U * bus;
        if (control_due) {
            uint8_t data[8] = {0};
            unsigned int active = motion == MOTOR_DEBUG_INIT ? init_waiting : mask;
            for (unsigned int axis = 0; axis < 2U; ++axis) {
                if ((active & (1U << (first + axis))) != 0U) {
                    data[axis] = motion == MOTOR_DEBUG_INIT ? ATLAS_INITIALIZE : ATLAS_ANGLE;
                }
            }
            if (!board_can_send_immediate((board_can_bus_t)bus, state_id(bus), data, 2)) { return false; }
        }
        if (angle_due) {
            uint8_t data[8] = {0};
            int16_t target = running_key == USER_SWITCH_LEFT ? -TARGET_MRAD : TARGET_MRAD;
            for (unsigned int axis = 0; axis < 2U; ++axis) {
                if ((mask & (1U << (first + axis))) != 0U) { atlas_encode_angle(data, axis, target); }
            }
            if (!board_can_send_immediate((board_can_bus_t)bus, (uint16_t)(0x120U + first), data, 8)) {
                return false;
            }
        }
    }
    return true;
}

void app_motor_debug_process(void)
{
    uint32_t now = board_millis();
    bool expired = !fresh(now, last_process, LOOP_TIMEOUT_MS);
    last_process = now;
    bool mode = motor_mode_selected();
    if (mode != raw_mode) { raw_mode = mode; mode_at = now; }
    if (!mode && mode_active) { mode_active = false; begin_stop(now); }
    if (mode && !mode_active && !fresh(now, mode_at, DEBOUNCE_MS)) {
        mode_active = true;
        begin_stop(now);
    }

    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_stats_t stats;
        if (board_can_get_stats((board_can_bus_t)bus, &stats) && stats.bus_off_events != 0U) { fault = true; }
    }
    if (fault && !fault_handled) { fault_handled = true; begin_stop(now); }

    bool select_high = true;
    bool select_valid = board_gpio_read_input(BOARD_GPIO_INPUT_SW_1, &select_high);
    bool select = !select_high;
    user_switch_t key = select_valid ? user_switch_read() : USER_SWITCH_INVALID;
    if (key != raw_key || select != raw_select) { raw_key = key; raw_select = select; input_at = now; }

    if (motion != MOTOR_DEBUG_OFF) {
        bool init_finished = motion == MOTOR_DEBUG_INIT &&
            (init_waiting == 0U || !fresh(now, motion_at, INITIALIZE_TIMEOUT_MS));
        if (expired || interrupted || select || key != running_key || init_finished ||
            !motion_ready(motion, now, false)) { begin_stop(now); }
    }
    send_stops(now);
    if (!mode_active || fault || any_stop_pending()) { return; }

    if (motion == MOTOR_DEBUG_OFF && !fresh(now, input_at, DEBOUNCE_MS)) {
        if (!select && key == USER_SWITCH_NONE) {
            neutral_seen = true;
        } else if (select && key == USER_SWITCH_NONE && neutral_seen) {
            selected = (selected + 1U) % (ALL_WHEELS + 1U);
            neutral_seen = false;
        } else if (!select && neutral_seen && key != USER_SWITCH_NONE) {
            motor_debug_state_t action = MOTOR_DEBUG_INIT;
            if (key == USER_SWITCH_LEFT || key == USER_SWITCH_RIGHT) { action = MOTOR_DEBUG_STEER; }
            if (key == USER_SWITCH_FORWARD || key == USER_SWITCH_BACK) { action = MOTOR_DEBUG_DRIVE; }
            neutral_seen = false;
            if (key != USER_SWITCH_INVALID && motion_ready(action, now, true)) { begin_motion(action, key, now); }
        }
    }
    bool sent = true;
    if (motion == MOTOR_DEBUG_DRIVE && !fresh(now, last_drive, DRIVE_PERIOD_MS)) {
        sent = send_drive(now);
    } else if (motion == MOTOR_DEBUG_STEER || motion == MOTOR_DEBUG_INIT) {
        sent = send_steering(now);
    }
    if (!sent) { begin_stop(now); send_stops(now); }
}

motor_debug_status_t app_motor_debug_status(void)
{
    motor_debug_state_t state = motion;
    if (fault) { state = MOTOR_DEBUG_FAULT; }
    else if (any_stop_pending()) { state = MOTOR_DEBUG_STOP; }
    else if (mode_active && state == MOTOR_DEBUG_OFF) {
        state = neutral_seen ? MOTOR_DEBUG_READY : MOTOR_DEBUG_WAIT;
    }
    return (motor_debug_status_t){state, selected};
}
