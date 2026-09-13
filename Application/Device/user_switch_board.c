/* ADC取得の有効性と旧SW判定を接続する。IRQではなくメイン処理から利用する。 */
#include "Device/user_switch.h"
#include "Board/board_user_adc.h"
user_switch_t user_switch_read(void)
{
    board_user_adc_sample_t sample;
    if (!board_user_adc_read(&sample)) { return USER_SWITCH_INVALID; }
    return user_switch_decode(sample.raw, true);
}
