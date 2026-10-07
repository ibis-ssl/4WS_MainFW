/* SWDからも読める診断値。mainで100 msごとに更新し、制御の入力には使用しない。 */
#ifndef APP_MONITOR_H
#define APP_MONITOR_H
#include "Board/board_can.h"
typedef struct {
    uint32_t magic, sequence, uptime_ms, can_rx[2], can_drop[2], can_lost[2], can_busoff[2];
    uint32_t orion_rx[2], last_id[2];
    uint32_t imu_who, imu_ready, imu_valid, imu_samples, imu_errors;
    int32_t accel_raw[3], gyro_raw[3], temperature_raw;
    uint32_t oled_address, oled_ready, oled_frames, oled_errors;
    uint32_t sw_valid, sw_raw, control_calls, control_overruns, control_max_cycles;
} app_monitor_status_t;
extern volatile app_monitor_status_t app_monitor_status;
void app_monitor_init(void);
void app_monitor_accept(board_can_bus_t bus, const board_can_frame_t *frame);
void app_monitor_process(void);
#endif
