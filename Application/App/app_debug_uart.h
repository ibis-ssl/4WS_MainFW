#ifndef APP_DEBUG_UART_H
#define APP_DEBUG_UART_H
#include <stdint.h>

/* メイン処理でのみ呼ぶ。UART入力は診断ページの切替に限定する。 */
void app_debug_uart_init(uint32_t now);
void app_debug_uart_process(uint32_t now);
#endif
