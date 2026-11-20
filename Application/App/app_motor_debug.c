#include "App/app_motor_debug.h"
#include "App/app_board_reset.h"
#include "Board/board_gpio.h"
#include "Board/board_debug_rtt.h"
#include "Board/board_time.h"
#include "Device/user_switch.h"
#include "Protocol/atlas_motor_packet.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

enum {
    WHEEL_COUNT = 4, ALL_WHEEL_MASK = (1U << WHEEL_COUNT) - 1U, DEBUG_MODE_CODE = 3,
    DEBOUNCE_MS = 30, ATLAS_PERIOD_MS = 2, STATE_PERIOD_MS = 100, DRIVE_PERIOD_MS = 10,
    ATLAS_TIMEOUT_MS = 150, CALIBRATION_TIMEOUT_MS = 2500, DRIVE_TIMEOUT_MS = 50,
    LOOP_TIMEOUT_MS = 100, INITIALIZE_TIMEOUT_MS = 30000,
    RANGE_READ_MS = 50, RANGE_FIELD_COUNT = 3, RANGE_READ_COUNT = 2 * RANGE_FIELD_COUNT,
    RTT_DRIVE_MS = 2200, RTT_INIT_KEEPALIVE_MS = 500,
    TARGET_ROTOR_RPM = 300, STOPPED_RPM = 40, MAX_ROTOR_RPM = 18000
};

typedef struct {
    c610_feedback_t feedback;
    uint32_t received_ms;
    uint8_t samples;
    float filtered_rpm;
    float integral;
    int16_t command_ma;
} wheel_t;

typedef struct {
    int16_t lower_mrad, upper_mrad;
    float target_mrad;
} steer_range_t;

typedef struct {
    atlas_axis_feedback_t axes[2];
    atlas_axis_calibration_t calibration[2];
    steer_range_t range[2];
    int32_t axis_read[RANGE_FIELD_COUNT];
    uint32_t feedback_ms, calibration_ms;
    uint32_t range_request_ms;
    uint8_t range_step, range_sequence, rpc_offset;
    bool seen, calibrated_message, range_pending;
} steering_t;

static wheel_t wheels[WHEEL_COUNT];
static steering_t steering[BOARD_CAN_COUNT];
static motor_debug_state_t motion;
static user_switch_t raw_key, running_key;
static bool raw_mode, mode_active, neutral_seen;
static bool fault, fault_handled, interrupted, rtt_motion;
static uint8_t init_waiting, init_observed;
static uint8_t stop_repeats[BOARD_CAN_COUNT];
static uint32_t mode_at, input_at, motion_at, last_process, last_state, last_angle, last_drive, last_stop;
static uint32_t rtt_keep_at;
static float ramped_rpm;

static bool fresh(uint32_t now, uint32_t at, uint32_t timeout)
{
    return now - at < timeout;
}

static void invalidate_range(steering_t *board)
{
    board->range_step = 0;
    board->range_pending = false;
}

static float limit_target(float target, const steer_range_t *range)
{
    return fmaxf(range->lower_mrad, fminf(range->upper_mrad, target));
}

static void accept_range(steering_t *board, const board_can_frame_t *frame)
{
    if (!board->range_pending || frame->id != 0x5D1U + board->rpc_offset ||
        frame->length != 8U || !fresh(frame->received_ms, board->range_request_ms, RANGE_READ_MS)) { return; }
    unsigned int axis = board->range_step / RANGE_FIELD_COUNT;
    unsigned int field = board->range_step % RANGE_FIELD_COUNT;
    if (!atlas_decode_axis_read(frame->data, (uint8_t)axis, board->range_sequence, &board->axis_read[field])) {
        return;
    }
    board->range_pending = false;
    if (field == RANGE_FIELD_COUNT - 1U) {
        const float mrad_per_mdeg = 0.01745329252f;
        /* 両端から30度内側。RPCのmdeg切捨てと指令の整数mrad化を内向きに丸める。 */
        int32_t lower = (int32_t)ceilf(((float)board->axis_read[0] + 30001.0f) * mrad_per_mdeg);
        int32_t upper = (int32_t)floorf(((float)board->axis_read[1] - 30001.0f) * mrad_per_mdeg);
        if (lower < -INT16_MAX || upper > INT16_MAX || lower > upper) { invalidate_range(board); return; }
        steer_range_t *range = &board->range[axis];
        range->lower_mrad = (int16_t)lower;
        range->upper_mrad = (int16_t)upper;
        range->target_mrad = limit_target((float)board->axis_read[2] * mrad_per_mdeg, range);
    }
    board->range_step++;
}

static bool steering_fresh(const steering_t *board, uint32_t now)
{
    return board->seen && fresh(now, board->feedback_ms, ATLAS_TIMEOUT_MS) &&
        board->calibrated_message && fresh(now, board->calibration_ms, CALIBRATION_TIMEOUT_MS);
}

static void read_ranges(uint32_t now)
{
    static const uint8_t fields[] = {7, 8, 3};
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        steering_t *board = &steering[bus];
        if (!steering_fresh(board, now) || !board->calibration[0].initialized ||
            !board->calibration[1].initialized) { invalidate_range(board); continue; }
        if (board->range_step == RANGE_READ_COUNT) { continue; }
        if (board->range_pending && fresh(now, board->range_request_ms, RANGE_READ_MS)) { continue; }
        if (board->range_pending) {
            /* 保守offsetは基板IDと独立しており、既存FWの0/16を非同期に照会する。 */
            board->rpc_offset ^= 16U;
            board->range_pending = false;
        }
        uint8_t data[8];
        atlas_encode_axis_read(data, board->range_step / RANGE_FIELD_COUNT,
                              ++board->range_sequence, fields[board->range_step % RANGE_FIELD_COUNT]);
        if (board_can_send_immediate((board_can_bus_t)bus, (uint16_t)(0x5D0U + board->rpc_offset), data, 8)) {
            board->range_request_ms = now;
            board->range_pending = true;
        }
    }
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
    if (frame->id == 0x5D1U || frame->id == 0x5E1U) {
        accept_range(board, frame);
        return true;
    }
    if (frame->id == 0x230U + first) {
        if (frame->length != 8U) { return true; }
        if (board->seen && !fresh(at, board->feedback_ms, ATLAS_TIMEOUT_MS)) {
            board->calibrated_message = false;
            invalidate_range(board);
            interrupted = true;
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
            if (!cal->initialized || cal->state == 2U) { invalidate_range(board); }
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
        if (motion == MOTOR_DEBUG_DRIVE &&
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
    rtt_motion = false;
    for (unsigned int i = 0; i < WHEEL_COUNT; ++i) { wheels[i].command_ma = 0; }
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
    for (unsigned int i = 0; i < WHEEL_COUNT; ++i) {
        const steering_t *board = &steering[i / 2U];
        const atlas_axis_calibration_t *cal = &board->calibration[i % 2U];
        if (!steering_fresh(board, now)) { return false; }
        if ((action != MOTOR_DEBUG_INIT || starting) && cal->state == 2U) { return false; }
        if ((action == MOTOR_DEBUG_STEER || action == MOTOR_DEBUG_DRIVE) &&
            (!cal->initialized || board->axes[i % 2U].angle_mrad == INT16_MIN)) { return false; }
        if (action == MOTOR_DEBUG_STEER && board->range_step != RANGE_READ_COUNT) { return false; }
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
    last_angle = now;
    last_drive = now;
    interrupted = false;
    init_waiting = ALL_WHEEL_MASK;
    init_observed = 0;
    ramped_rpm = 0.0f;
    if (action == MOTOR_DEBUG_INIT) {
        for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) { invalidate_range(&steering[bus]); }
    }
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
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        uint8_t data[8] = {0};
        for (unsigned int i = 2U * bus; i < 2U * bus + 2U; ++i) {
            wheels[i].command_ma = drive_current(&wheels[i], dt);
            c610_encode_current(data, i, wheels[i].command_ma);
        }
        if (!board_can_send_immediate((board_can_bus_t)bus, 0x200U, data, 8)) { return false; }
    }
    return true;
}

static bool send_steering(uint32_t now)
{
    bool control_due = !fresh(now, last_state, STATE_PERIOD_MS);
    bool angle_due = motion == MOTOR_DEBUG_STEER && !fresh(now, last_angle, ATLAS_PERIOD_MS);
    /* 90度/秒を実経過msで積分し、軸別の許容範囲で目標を止める。 */
    float delta_mrad = (running_key == USER_SWITCH_LEFT ? -1.570796327f : 1.570796327f) *
        (float)(now - last_angle);
    if (control_due) { last_state = now; }
    if (angle_due) { last_angle = now; }
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        unsigned int first = 2U * bus;
        if (control_due) {
            uint8_t data[8] = {0};
            unsigned int active = motion == MOTOR_DEBUG_INIT ? init_waiting : ALL_WHEEL_MASK;
            for (unsigned int axis = 0; axis < 2U; ++axis) {
                if ((active & (1U << (first + axis))) != 0U) {
                    data[axis] = motion == MOTOR_DEBUG_INIT ? ATLAS_INITIALIZE : ATLAS_ANGLE;
                }
            }
            if (!board_can_send_immediate((board_can_bus_t)bus, state_id(bus), data, 2)) { return false; }
        }
        if (angle_due) {
            uint8_t data[8] = {0};
            for (unsigned int axis = 0; axis < 2U; ++axis) {
                steer_range_t *range = &steering[bus].range[axis];
                range->target_mrad = limit_target(range->target_mrad + delta_mrad, range);
                atlas_encode_angle(data, axis, (int16_t)lroundf(range->target_mrad));
            }
            if (!board_can_send_immediate((board_can_bus_t)bus, (uint16_t)(0x120U + first), data, 8)) {
                return false;
            }
        }
    }
    return true;
}

static void rtt_status(uint32_t now)
{
    unsigned int cal = 0, esc = 0, ranges = 0;
    for (unsigned int i = 0; i < WHEEL_COUNT; ++i) {
        const steering_t *board = &steering[i / 2U];
        const wheel_t *wheel = &wheels[i];
        if (steering_fresh(board, now) && board->calibration[i % 2U].initialized &&
            board->axes[i % 2U].angle_mrad != INT16_MIN) { cal |= 1U << i; }
        if (wheel->samples >= 3U && fresh(now, wheel->received_ms, DRIVE_TIMEOUT_MS)) { esc |= 1U << i; }
        if (board->range_step == RANGE_READ_COUNT) { ranges |= 1U << i; }
    }
    char line[192];
    int length = snprintf(line, sizeof(line),
        "MAIN ms=%lu mode=%u state=%u cal=%X esc=%X range=%X target=%d rpm=%d,%d,%d,%d ma=%d,%d,%d,%d boot=%u\n",
        (unsigned long)now, (unsigned int)mode_active, (unsigned int)app_motor_debug_status(), cal, esc, ranges,
        motion == MOTOR_DEBUG_DRIVE ? (int)lroundf(ramped_rpm) : 0,
        wheels[0].feedback.rpm, wheels[1].feedback.rpm, wheels[2].feedback.rpm, wheels[3].feedback.rpm,
        wheels[0].command_ma, wheels[1].command_ma, wheels[2].command_ma, wheels[3].command_ma,
        (unsigned int)app_board_reset_status());
    if (length > 0 && (size_t)length < sizeof(line)) { board_debug_rtt_write(line); }
}

static void rtt_process(uint32_t now, user_switch_t key)
{
    static char command[12];
    static size_t length;
    static bool discard;
    uint8_t byte;
    for (unsigned int n = 0; n < 32U && board_debug_rtt_read(&byte); ++n) {
        if (byte == '\r') { continue; }
        if (byte != '\n') {
            if (length == sizeof(command) - 1U || byte == 0U) { discard = true; }
            if (!discard) { command[length++] = (char)byte; }
            continue;
        }
        command[length] = '\0';
        length = 0;
        if (discard) { discard = false; continue; }
        if (strcmp(command, "status") == 0) { rtt_status(now); continue; }
        if (strcmp(command, "keep") == 0) {
            if (rtt_motion && motion == MOTOR_DEBUG_INIT) { rtt_keep_at = now; }
            continue;
        }
        if (strcmp(command, "stop") == 0) {
            if (motion != MOTOR_DEBUG_OFF) { begin_stop(now); }
            board_debug_rtt_write("OK stop\n");
            continue;
        }
        bool initialize = strcmp(command, "init") == 0;
        bool positive = strcmp(command, "drive+") == 0;
        if (!initialize && !positive && strcmp(command, "drive-") != 0) { continue; }
        motor_debug_state_t action = initialize ? MOTOR_DEBUG_INIT : MOTOR_DEBUG_DRIVE;
        if (key != USER_SWITCH_NONE || !neutral_seen || !mode_active || fault || any_stop_pending() ||
            motion != MOTOR_DEBUG_OFF || !motion_ready(action, now, true)) {
            board_debug_rtt_write(initialize ? "REJECT init\n" : "REJECT drive\n");
            continue;
        }
        user_switch_t command_key = initialize ? USER_SWITCH_CENTER :
            (positive ? USER_SWITCH_FORWARD : USER_SWITCH_BACK);
        begin_motion(action, command_key, now);
        rtt_motion = true;
        rtt_keep_at = now;
        neutral_seen = false;
        board_debug_rtt_write(initialize ? "OK init\n" : "OK drive\n");
    }
}

static bool motion_released(user_switch_t key, uint32_t now)
{
    if (rtt_motion) {
        /* 通信が途絶した校正を継続しない。速度試験は最長2.2秒の単発。 */
        bool timed_out = motion == MOTOR_DEBUG_INIT ?
            !fresh(now, rtt_keep_at, RTT_INIT_KEEPALIVE_MS) :
            !fresh(now, motion_at, RTT_DRIVE_MS);
        return key != USER_SWITCH_NONE || timed_out;
    }
    return key != running_key && (motion != MOTOR_DEBUG_INIT || key != USER_SWITCH_NONE);
}

void app_motor_debug_process(void)
{
    uint32_t now = board_millis();
    if (app_board_reset_status() != BOARD_RESET_READY) {
        rtt_process(now, USER_SWITCH_INVALID);
        return;
    }
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

    user_switch_t key = user_switch_read();
    if (key != raw_key) { raw_key = key; input_at = now; }

    if (motion != MOTOR_DEBUG_OFF) {
        bool init_finished = motion == MOTOR_DEBUG_INIT &&
            (init_waiting == 0U || !fresh(now, motion_at, INITIALIZE_TIMEOUT_MS));
        if (expired || interrupted || motion_released(key, now) || init_finished ||
            !motion_ready(motion, now, false)) { begin_stop(now); }
    }
    send_stops(now);
    rtt_process(now, key);
    if (!mode_active || fault || any_stop_pending()) { return; }

    if (motion == MOTOR_DEBUG_OFF) { read_ranges(now); }
    if (motion == MOTOR_DEBUG_OFF && !fresh(now, input_at, DEBOUNCE_MS)) {
        if (key == USER_SWITCH_NONE) {
            neutral_seen = true;
        } else if (neutral_seen) {
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

motor_debug_state_t app_motor_debug_status(void)
{
    if (app_board_reset_status() == BOARD_RESET_FAILED) { return MOTOR_DEBUG_FAULT; }
    if (app_board_reset_status() == BOARD_RESET_PENDING) { return MOTOR_DEBUG_WAIT; }
    motor_debug_state_t state = motion;
    if (fault) { state = MOTOR_DEBUG_FAULT; }
    else if (any_stop_pending()) { state = MOTOR_DEBUG_STOP; }
    else if (mode_active && state == MOTOR_DEBUG_OFF) {
        state = neutral_seen ? MOTOR_DEBUG_READY : MOTOR_DEBUG_WAIT;
    }
    return state;
}
