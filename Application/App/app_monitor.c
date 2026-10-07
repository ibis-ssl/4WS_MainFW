/* 機器取得・表示はmainで行い、500 Hz制御IRQへ待機を持ち込まない。 */
#include "App/app_monitor.h"
#include "Board/board_time.h"
#include "Board/board_user_adc.h"
#include "Board/board_control_timer.h"
#include "Device/imu.h"
#include "Device/oled.h"
#include "Device/orion_can.h"
#include <stdio.h>
volatile app_monitor_status_t app_monitor_status = {.magic = 0x34575331U};
static uint32_t report_ms, retry_ms;
void app_monitor_init(void)
{
    imu_init();
    (void)oled_init();
    report_ms = retry_ms = board_millis();
}
void app_monitor_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || frame == NULL) { return; }
    app_monitor_status.last_id[bus] = frame->id;
    (void)orion_can_accept(bus, frame);
}
void app_monitor_process(void)
{
    imu_process();
    oled_process();
    uint32_t now = board_millis();
    oled_status_t display;
    oled_get_status(&display);
    if (!display.ready && (uint32_t)(now - retry_ms) >= 1000U) {
        retry_ms = now; (void)oled_init(); oled_get_status(&display);
    }
    if ((uint32_t)(now - report_ms) < 100U) { return; }
    report_ms = now;
    imu_status_t imu;
    (void)imu_get_status(&imu);
    /* 奇数は更新中、偶数は完成。SWD読取りでは停止または前後のsequence一致を確認する。 */
    app_monitor_status.sequence++;
    app_monitor_status.uptime_ms = now;
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_stats_t can;
        (void)board_can_get_stats((board_can_bus_t)bus, &can);
        app_monitor_status.can_rx[bus] = can.rx_received;
        app_monitor_status.can_drop[bus] = can.rx_dropped;
        app_monitor_status.can_lost[bus] = can.rx_lost;
        app_monitor_status.can_busoff[bus] = can.bus_off_events;
        app_monitor_status.orion_rx[bus] = orion_can_received((board_can_bus_t)bus);
    }
    app_monitor_status.imu_who = imu.who_am_i; app_monitor_status.imu_ready = imu.ready;
    app_monitor_status.imu_valid = imu.valid; app_monitor_status.imu_samples = imu.samples;
    app_monitor_status.imu_errors = imu.errors; app_monitor_status.temperature_raw = imu.raw_temperature;
    for (unsigned int i = 0; i < 3U; ++i) {
        app_monitor_status.accel_raw[i] = imu.raw_accel[i]; app_monitor_status.gyro_raw[i] = imu.raw_gyro[i];
    }
    app_monitor_status.oled_address = display.address; app_monitor_status.oled_ready = display.ready;
    app_monitor_status.oled_frames = display.frames; app_monitor_status.oled_errors = display.errors;
    board_user_adc_sample_t sw = {0};
    app_monitor_status.sw_valid = board_user_adc_read(&sw); app_monitor_status.sw_raw = sw.raw;
    board_control_timer_stats_t timer;
    board_control_timer_get_stats(&timer);
    app_monitor_status.control_calls = timer.calls; app_monitor_status.control_overruns = timer.overruns;
    app_monitor_status.control_max_cycles = timer.max_execution_cycles;
    app_monitor_status.sequence++;
    if (!oled_begin()) { return; }
    char line[32];
    (void)oled_text(0, 0, "4WS RX DIAGNOSTICS");
    for (unsigned int bus = 0; bus < 2U; ++bus) {
        (void)snprintf(line, sizeof(line), "CAN%u RX %lu", bus + 1U, (unsigned long)app_monitor_status.can_rx[bus]);
        (void)oled_text((uint8_t)(bus + 1U), 0, line);
    }
    (void)snprintf(line, sizeof(line), "IMU %s WHO %02X", imu.valid ? "OK" : "WAIT", imu.who_am_i);
    (void)oled_text(3, 0, line);
    (void)snprintf(line, sizeof(line), "AZ %d GZ %d", imu.raw_accel[2], imu.raw_gyro[2]);
    (void)oled_text(4, 0, line);
    (void)snprintf(line, sizeof(line), "TEMP %ld MC", (long)(imu.temperature_c * 1000.0f));
    (void)oled_text(5, 0, line);
    (void)snprintf(line, sizeof(line), "ORION %lu/%lu", (unsigned long)app_monitor_status.orion_rx[0], (unsigned long)app_monitor_status.orion_rx[1]);
    (void)oled_text(6, 0, line);
    (void)snprintf(line, sizeof(line), "SW %u RAW %u", (unsigned)app_monitor_status.sw_valid, sw.raw);
    (void)oled_text(7, 0, line);
    (void)oled_commit();
}
