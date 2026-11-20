#ifndef APP_STEER_UPDATE_H
#define APP_STEER_UPDATE_H

#include "Board/board_can.h"

#define STEER_UPDATE_CHUNK_SIZE 256U

typedef enum {
    STEER_UPDATE_IDLE, STEER_UPDATE_STAGING, STEER_UPDATE_RUNNING, STEER_UPDATE_DONE, STEER_UPDATE_FAILED
} steer_update_state_t;

typedef struct {
    steer_update_state_t state;
    uint32_t size, staged, written, old_crc, image_crc;
    uint8_t board, step, error;
} steer_update_status_t;

/* CM4等の転送側はこのAPIへ要求を渡す。CANやRTTのバイト列を渡す必要はない。 */
bool app_steer_update_begin(uint8_t board, uint32_t size, uint32_t crc, uint32_t now);
bool app_steer_update_stage(uint32_t offset, const uint8_t *data, uint32_t length, uint32_t now);
bool app_steer_update_commit(uint32_t now);
bool app_steer_update_abort(void);
bool app_steer_update_locked(void);
steer_update_status_t app_steer_update_status(void);
bool app_steer_update_accept(board_can_bus_t bus, const board_can_frame_t *frame);
void app_steer_update_process(uint32_t now);

#endif
