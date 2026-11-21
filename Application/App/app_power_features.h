#ifndef APP_POWER_FEATURES_H
#define APP_POWER_FEATURES_H
#include "Board/board_can.h"

void app_power_features_start(uint32_t now);
bool app_power_features_accept(board_can_bus_t bus, const board_can_frame_t *frame);
void app_power_features_process(uint32_t now, bool update_locked);
void app_power_features_publish_fault(void);
bool app_power_features_bench_idle(void);
bool app_power_features_mouse(uint32_t now, int16_t *dx, int16_t *dy, uint16_t *quality);
bool app_power_features_dribbler(uint32_t now, float *rps);
#endif
