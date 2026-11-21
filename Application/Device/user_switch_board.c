/* ADC取得の有効性と旧SW判定を接続する。IRQではなくメイン処理から利用する。 */
#include "Device/user_switch.h"
#include "Board/board_gpio.h"
#include "Board/board_user_adc.h"
user_switch_t user_switch_read(void)
{
    board_user_adc_sample_t sample;
    if (!board_user_adc_read(&sample)) { return USER_SWITCH_INVALID; }
    return user_switch_decode(sample.raw, true);
}
uint8_t user_mode_code(void)
{
    uint8_t code = 0;
    for (unsigned int bit = 0; bit < 4U; ++bit) {
        bool high;
        if (!board_gpio_read_input((board_gpio_input_t)(BOARD_GPIO_INPUT_DIP_0 + bit), &high)) { return UINT8_MAX; }
        if (!high) { code |= (uint8_t)(1U << bit); }
    }
    return code;
}
