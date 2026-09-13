/* TIM1 CH1専用ブザーPWM。公開APIはメイン処理専用、生成初期化後に使用する。 */
#ifndef BOARD_BUZZER_H
#define BOARD_BUZZER_H
#include <stdbool.h>
#include <stdint.h>
bool board_buzzer_init(void);
/* 1～20000 Hz、約50% duty。成功時だけactual_millihzへ量子化後の周波数を返す。NULL可。 */
bool board_buzzer_set_hz(uint32_t hz, uint32_t *actual_millihz);
void board_buzzer_stop(void);
#endif
