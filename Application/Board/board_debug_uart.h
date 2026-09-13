/* UART4のデバッグ送受信。初期化・処理・公開APIはメイン処理専用。IRQから呼ばない。 */
#ifndef BOARD_DEBUG_UART_H
#define BOARD_DEBUG_UART_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef struct { uint32_t tx_completed, tx_rejected, tx_errors, rx_dropped, rx_errors; } board_debug_uart_stats_t;
bool board_debug_uart_init(void);
void board_debug_uart_process(void);
bool board_debug_uart_ready(void);
/* 最大2000 byteを内部へコピー。busy・過大・無効引数ではfalse、部分送信しない。 */
bool board_debug_uart_write(const void *data, size_t length);
/* 容量超過は切り詰めて送信せずfalseを返す。浮動小数点の表示対応はビルド設定に従う。 */
bool p(const char *format, ...);
bool board_debug_uart_read(uint8_t *byte);
void board_debug_uart_get_stats(board_debug_uart_stats_t *stats);
#endif
