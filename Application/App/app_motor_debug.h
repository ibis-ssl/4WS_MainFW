#ifndef APP_MOTOR_DEBUG_H
#define APP_MOTOR_DEBUG_H

#include "Board/board_can.h"

typedef enum {
    MOTOR_DEBUG_OFF, MOTOR_DEBUG_WAIT, MOTOR_DEBUG_READY, MOTOR_DEBUG_STEER,
    MOTOR_DEBUG_DRIVE, MOTOR_DEBUG_INIT, MOTOR_DEBUG_STOP, MOTOR_DEBUG_FAULT
} motor_debug_state_t;

/* MainのCAN受信振分けと周期送信から呼ぶ。 */
bool app_motor_debug_accept(board_can_bus_t bus, const board_can_frame_t *frame);
bool app_motor_debug_accept_temperature(board_can_bus_t bus, const board_can_frame_t *frame);
typedef struct {
    int16_t decic[4];
    uint8_t seen_mask, valid_mask;
} steer_temperature_t;
steer_temperature_t app_motor_debug_temperature(uint32_t now);
void app_motor_debug_rtt_command(const char *command);
bool app_motor_debug_idle(void);
void app_motor_debug_latch_fault(void);
void app_motor_debug_process(void);
motor_debug_state_t app_motor_debug_status(void);

/* UART診断用の最新値。受信済み、鮮度、軸状態で数値の有効性を判定する。 */
typedef struct {
    uint32_t drive_age_ms, steer_age_ms, cal_age_ms;
    int16_t drive_rpm, drive_raw_current, drive_command_ma;
    int16_t steer_angle_mrad, steer_target_mrad;
    uint16_t steer_current_ma;
    uint8_t steer_state;
    bool drive_fresh, steer_fresh, cal_fresh, steer_initialized, steer_target_valid;
} motor_debug_axis_status_t;
typedef struct {
    motor_debug_axis_status_t axes[4];
    motor_debug_state_t state;
    int16_t target_rpm;
    bool mode_active;
} motor_debug_status_t;
motor_debug_status_t app_motor_debug_snapshot(uint32_t now);

#endif
