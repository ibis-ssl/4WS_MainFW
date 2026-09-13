/* TIM6専用の周期IRQ。コールバックは優先度2で直接実行し、mainへ実行を委譲しない。 */
#ifndef BOARD_CONTROL_TIMER_H
#define BOARD_CONTROL_TIMER_H
#include <stdbool.h>
#include <stdint.h>
#define BOARD_CONTROL_IRQ_PRIORITY 2U
typedef struct {
    uint32_t calls;
    uint32_t overruns;
    uint32_t max_execution_cycles;
    uint32_t max_entry_delay_us;
} board_control_timer_stats_t;
/* 全ドライバ初期化後に一度呼ぶ。1 MHzタイマーで正確に分割できる周期だけを受理する。 */
bool board_control_timer_start(uint32_t frequency_hz, void (*callback)(void));
/* 計測値は項目ごとのスナップショット。取得中も制御IRQを止めない。 */
void board_control_timer_get_stats(board_control_timer_stats_t *stats);
#endif
