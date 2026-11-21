#ifndef APP_BOARD_RESET_H
#define APP_BOARD_RESET_H

#include "Board/board_can.h"

typedef enum { BOARD_RESET_PENDING, BOARD_RESET_READY, BOARD_RESET_FAILED } app_board_reset_status_t;

void app_board_reset_start(uint32_t now);
void app_board_reset_process(uint32_t now);
void app_board_reset_accept(board_can_bus_t bus, const board_can_frame_t *frame);
app_board_reset_status_t app_board_reset_status(void);
/* 両バスの停止投入と電源リセット後の待機を終えたか。 */
bool app_board_reset_power_allowed(uint32_t now);

#endif
