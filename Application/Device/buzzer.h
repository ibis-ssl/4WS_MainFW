/* メイン処理で更新する時間指定ブザー。待機せず、最新の要求で前の鳴動を置き換える。 */
#ifndef BUZZER_H
#define BUZZER_H
#include <stdbool.h>
#include <stdint.h>
/* duration_ms=0は連続鳴動。Hzの範囲はBoardの仕様に従う。 */
bool buzzer_start(uint32_t hz, uint32_t duration_ms);
void buzzer_stop(void);
void buzzer_process(void);
#endif
