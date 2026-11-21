#ifndef APP_POWER_UPDATE_H
#define APP_POWER_UPDATE_H
#include "Board/board_can.h"

#define POWER_UPDATE_BLOCK_SIZE 896U
typedef enum { POWER_UP_IDLE, POWER_UP_STAGING, POWER_UP_RUNNING, POWER_UP_DONE, POWER_UP_FAILED } power_update_state_t;
typedef struct {
    power_update_state_t state;
    uint32_t size, written, staged, crc, build;
    uint8_t step, error;
} power_update_status_t;

bool app_power_update_begin(uint32_t size, uint32_t crc, uint32_t build, uint32_t now);
bool app_power_update_stage(uint32_t offset, const uint8_t *data, uint32_t size, uint32_t now);
bool app_power_update_commit(uint32_t now);
bool app_power_update_abort(void);
bool app_power_update_locked(void);
bool app_power_update_quiet(void);
bool app_power_update_accept(board_can_bus_t bus, const board_can_frame_t *frame);
void app_power_update_process(uint32_t now);
bool app_power_version_request(void);
bool app_power_version_read(uint32_t *build, uint32_t *crc);
power_update_status_t app_power_update_status(void);
#endif
