#ifndef APP_MOTOR_DEBUG_H
#define APP_MOTOR_DEBUG_H

#include "Board/board_can.h"

typedef enum {
    MOTOR_DEBUG_OFF, MOTOR_DEBUG_WAIT, MOTOR_DEBUG_READY, MOTOR_DEBUG_STEER,
    MOTOR_DEBUG_DRIVE, MOTOR_DEBUG_INIT, MOTOR_DEBUG_STOP, MOTOR_DEBUG_FAULT
} motor_debug_state_t;

typedef struct {
    motor_debug_state_t state;
    unsigned int wheel;
} motor_debug_status_t;

/* MainのCAN受信振分けと周期送信から呼ぶ。 */
bool app_motor_debug_accept(board_can_bus_t bus, const board_can_frame_t *frame);
void app_motor_debug_process(void);
motor_debug_status_t app_motor_debug_status(void);

#endif
