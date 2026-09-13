/* U_BTN専用ADC取得。生成初期化後に一度開始し、取得済みと生値を区別する。 */
#ifndef BOARD_USER_ADC_H
#define BOARD_USER_ADC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint16_t raw; uint32_t sampled_ms; } board_user_adc_sample_t;
bool board_user_adc_init(void);
/* 未取得・ADCエラー・100 ms超の更新停止ではfalse。出力先は変更しない。 */
bool board_user_adc_read(board_user_adc_sample_t *sample);
uint32_t board_user_adc_error_count(void);
#endif
