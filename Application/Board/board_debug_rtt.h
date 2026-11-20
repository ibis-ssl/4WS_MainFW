#ifndef BOARD_DEBUG_RTT_H
#define BOARD_DEBUG_RTT_H

#include <stdbool.h>
#include <stdint.h>

/* メインループ専用。デバッガ未接続時も待たず、空きが足りなければ送信行を破棄する。 */
void board_debug_rtt_init(void);
bool board_debug_rtt_read(uint8_t *value);
void board_debug_rtt_write(const char *line);

#endif
