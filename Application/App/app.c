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

static bool report_pending;
bool app_init(void)
{
    if (!board_debug_uart_init() || !board_user_adc_init() || !board_can_init() || !board_buzzer_init()) {
        return false;
    }
    return board_debug_uart_printf("4WS drivers ready; '?' = status\r\n");
}
void app_process(void)
{
    board_debug_uart_process();
    board_can_process();
    buzzer_process();
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        board_can_frame_t frame;
        for (unsigned int n = 0; n < 20U && board_can_receive((board_can_bus_t)bus, &frame); ++n) {
            (void)power_board_accept((board_can_bus_t)bus, &frame);
        }
    }
    uint8_t byte;
    for (unsigned int n = 0; n < 32U && board_debug_uart_read(&byte); ++n) {
        if (byte == '?' || byte == '\r' || byte == '\n') { report_pending = true; }
    }
    if (report_pending && board_debug_uart_ready()) {
        board_user_adc_sample_t sample = {0};
        bool valid = board_user_adc_read(&sample);
        board_can_stats_t can1, can2;
        board_can_get_stats(BOARD_CAN_1, &can1);
        board_can_get_stats(BOARD_CAN_2, &can2);
        /* 浮動小数点printfに依存せず、取得有効性とドライバ状態を確認できる。 */
        if (board_debug_uart_printf("SW valid=%u raw=%u key=%u ADC errors=%lu; CAN rx=%lu/%lu drop=%lu/%lu busoff=%lu/%lu\r\n",
            (unsigned)valid, (unsigned)sample.raw, (unsigned)user_switch_decode(sample.raw, valid),
            (unsigned long)board_user_adc_error_count(),
            (unsigned long)can1.rx_received, (unsigned long)can2.rx_received,
            (unsigned long)can1.rx_dropped, (unsigned long)can2.rx_dropped,
            (unsigned long)can1.bus_off_events, (unsigned long)can2.bus_off_events)) { report_pending = false; }
    }
}
