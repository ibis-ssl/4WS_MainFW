/*
 * 起動時に受信とSW取得を開始し、UART4で読取り専用の状態表示を提供する。
 * 電源APIは明示要求用とし、自動送信やボタンからの出力操作は行わない。
 */
#include "App/app.h"
#include "Board/board_can.h"
#include "Board/board_debug_uart.h"
#include "Board/board_user_adc.h"
#include "Board/board_buzzer.h"
#include "Device/user_switch.h"
#include "Device/power_board.h"
#include "Device/buzzer.h"
#include "App/app_control.h"
#include "Board/board_control_timer.h"
#include "Board/board_time.h"
#include "App/app_monitor.h"
#include "Board/board_gpio.h"

static uint32_t report_ms;
static uint32_t startup_led_ms;
static unsigned int startup_led_index;
static void set_startup_led(board_gpio_led_t led, bool on)
{
    bool active_high = (APP_STARTUP_LED_ACTIVE_HIGH_MASK & (1U << (unsigned int)led)) != 0U;
    (void)board_gpio_write_led_level(led, on == active_high);
}
static void startup_led_begin(void)
{
    for (unsigned int i = 0; i < BOARD_GPIO_LED_COUNT; ++i) { set_startup_led((board_gpio_led_t)i, false); }
    startup_led_index = 0;
    startup_led_ms = board_millis();
    set_startup_led(BOARD_GPIO_LED_0, true);
}
static void startup_led_process(void)
{
    /* mainで時刻を判定し、センサー取得と制御IRQを止めずに1個ずつ切り替える。
     * mainが遅れた場合も次のLEDを200 ms確保し、連続切替で見えなくしない。 */
    uint32_t now = board_millis();
    if (startup_led_index >= BOARD_GPIO_LED_COUNT ||
        (uint32_t)(now - startup_led_ms) < APP_STARTUP_LED_DURATION_MS) { return; }
    set_startup_led((board_gpio_led_t)startup_led_index, false);
    startup_led_index++;
    startup_led_ms = now;
    if (startup_led_index < BOARD_GPIO_LED_COUNT) { set_startup_led((board_gpio_led_t)startup_led_index, true); }
}
bool app_init(void)
{
    if (!board_debug_uart_init() || !board_user_adc_init() || !board_can_init() || !board_buzzer_init()) {
        return false;
    }
    report_ms = board_millis();
    startup_led_begin();
    app_monitor_init();
    return board_control_timer_start(APP_CONTROL_FREQUENCY_HZ, app_control_step);
}
void app_process(void)
{
    startup_led_process();
    board_debug_uart_process();
    board_can_process();
    buzzer_process();
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_frame_t frame;
        for (unsigned int n = 0; n < 20U && board_can_receive((board_can_bus_t)bus, &frame); ++n) {
            (void)power_board_accept((board_can_bus_t)bus, &frame);
            app_monitor_accept((board_can_bus_t)bus, &frame);
        }
    }
    uint8_t byte;
    for (unsigned int n = 0; n < 32U && board_debug_uart_read(&byte); ++n) {
        /* 入力で定期出力を増速しない。操作コマンドは今後Appで定義する。 */
        (void)byte;
    }
    app_monitor_process();
    uint32_t now = board_millis();
    uint32_t elapsed = now - report_ms;
    if (elapsed >= 100U) {
        /* 元の100 ms位相を維持し、遅延分をまとめて連続出力しない。 */
        report_ms += (elapsed / 100U) * 100U;
        if (!board_debug_uart_ready()) { return; }
        board_user_adc_sample_t sample = {0};
        bool valid = board_user_adc_read(&sample);
        board_can_stats_t can1, can2;
        board_can_get_stats(BOARD_CAN_1, &can1);
        board_can_get_stats(BOARD_CAN_2, &can2);
        board_control_timer_stats_t control;
        board_control_timer_get_stats(&control);
        /* 浮動小数点printfに依存せず、取得有効性とドライバ状態を確認できる。 */
        (void)p("SW valid=%u raw=%u key=%u ADC errors=%lu; CAN rx=%lu/%lu drop=%lu/%lu busoff=%lu/%lu; CTRL calls=%lu overrun=%lu max_cycles=%lu delay_us=%lu; IMU who=%02lX valid=%lu samples=%lu err=%lu az=%ld gz=%ld; OLED addr=%02lX frames=%lu err=%lu\r\n",
            (unsigned)valid, (unsigned)sample.raw, (unsigned)user_switch_decode(sample.raw, valid),
            (unsigned long)board_user_adc_error_count(),
            (unsigned long)can1.rx_received, (unsigned long)can2.rx_received,
            (unsigned long)can1.rx_dropped, (unsigned long)can2.rx_dropped,
            (unsigned long)can1.bus_off_events, (unsigned long)can2.bus_off_events,
            (unsigned long)control.calls, (unsigned long)control.overruns,
            (unsigned long)control.max_execution_cycles, (unsigned long)control.max_entry_delay_us,
            (unsigned long)app_monitor_status.imu_who, (unsigned long)app_monitor_status.imu_valid,
            (unsigned long)app_monitor_status.imu_samples, (unsigned long)app_monitor_status.imu_errors,
            (long)app_monitor_status.accel_raw[2], (long)app_monitor_status.gyro_raw[2],
            (unsigned long)app_monitor_status.oled_address, (unsigned long)app_monitor_status.oled_frames,
            (unsigned long)app_monitor_status.oled_errors);
    }
}
