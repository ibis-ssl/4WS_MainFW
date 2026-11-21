/*
 * 起動時に受信とSW取得を開始し、UART4で読取り専用の状態表示を提供する。
 * 起動時の周辺基板リセットとモーターデバッグは、それぞれ専用モジュールで管理する。
 */
#include "App/app.h"
#include "App/app_debug_uart.h"
#include "Board/board_can.h"
#include "Board/board_debug_uart.h"
#include "Board/board_debug_rtt.h"
#include "Board/board_user_adc.h"
#include "Board/board_buzzer.h"
#include "Device/power_board.h"
#include "Device/buzzer.h"
#include "App/app_control.h"
#include "Board/board_control_timer.h"
#include "Board/board_time.h"
#include "App/app_monitor.h"
#include "App/app_motor_debug.h"
#include "App/app_board_reset.h"
#include "App/app_host_rtt.h"
#include "App/app_steer_update.h"
#include "Board/board_gpio.h"

static uint32_t led_sequence_ms;
static unsigned int led_sequence_index;
static void set_led_sequence(board_gpio_led_t led, bool on)
{
    bool active_high = (APP_LED_SEQUENCE_ACTIVE_HIGH_MASK & (1U << (unsigned int)led)) != 0U;
    (void)board_gpio_write_led_level(led, on == active_high);
}
static void led_sequence_begin(void)
{
    for (unsigned int i = 0; i < BOARD_GPIO_LED_COUNT; ++i) { set_led_sequence((board_gpio_led_t)i, false); }
    led_sequence_index = 0;
    led_sequence_ms = board_millis();
    set_led_sequence(BOARD_GPIO_LED_0, true);
}
static void led_sequence_process(void)
{
    /* mainで時刻を判定し、センサー取得と制御IRQを止めずに1個ずつ切り替える。
     * mainが遅れた場合も次のLEDを200 ms確保し、連続切替で見えなくしない。 */
    uint32_t now = board_millis();
    if ((uint32_t)(now - led_sequence_ms) < APP_LED_SEQUENCE_DURATION_MS) { return; }
    set_led_sequence((board_gpio_led_t)led_sequence_index, false);
    led_sequence_index = (led_sequence_index + 1U) % BOARD_GPIO_LED_COUNT;
    led_sequence_ms = now;
    set_led_sequence((board_gpio_led_t)led_sequence_index, true);
}
bool app_init(void)
{
    board_debug_rtt_init();
    if (!board_debug_uart_init() || !board_user_adc_init() || !board_can_init() || !board_buzzer_init()) {
        return false;
    }
    uint32_t now = board_millis();
    app_debug_uart_init(now);
    app_board_reset_start(now);
    power_board_supply_start(now);
    led_sequence_begin();
    app_monitor_init();
    if (!board_control_timer_start(APP_CONTROL_FREQUENCY_HZ, app_control_step)) { return false; }
    /* 起動通知だけを鳴らし、mainの時間管理で100 ms後にLowへ戻す。 */
    return buzzer_start(2000U, 100U);
}
void app_process(void)
{
    led_sequence_process();
    board_debug_uart_process();
    board_can_process();
    buzzer_process();
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_frame_t frame;
        for (unsigned int n = 0; n < 20U && board_can_receive((board_can_bus_t)bus, &frame); ++n) {
            bool ready = app_board_reset_status() == BOARD_RESET_READY;
            bool motor_frame = app_steer_update_accept((board_can_bus_t)bus, &frame) ||
                app_motor_debug_accept_temperature((board_can_bus_t)bus, &frame) ||
                (ready && app_motor_debug_accept((board_can_bus_t)bus, &frame));
            if (!ready) { app_board_reset_accept((board_can_bus_t)bus, &frame); }
            /* 電源の共通エラーは停止経路と電源診断の両方に渡す。 */
            (void)power_board_accept((board_can_bus_t)bus, &frame);
            app_monitor_accept((board_can_bus_t)bus, &frame, ready && !motor_frame);
        }
    }
    app_board_reset_process(board_millis());
    if (power_board_supply_fault()) { app_motor_debug_latch_fault(); }
    app_host_rtt_process();
    app_motor_debug_process();
    uint32_t now = board_millis();
    /* ステア更新中も制御出力は停止し、Flash書込み用の給電は維持する。 */
    power_board_supply_process(now, app_board_reset_power_allowed(now) &&
                                   app_motor_debug_status() != MOTOR_DEBUG_FAULT);
    app_steer_update_process(board_millis());
    /* 更新応答の回収を優先し、同期I2C転送をFlash書込み中に挟まない。 */
    if (app_steer_update_status().state == STEER_UPDATE_RUNNING) { return; }
    app_monitor_process();
    app_debug_uart_process(board_millis());
}
