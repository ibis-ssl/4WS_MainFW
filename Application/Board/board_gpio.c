/*
 * CubeMXのピン定義を基板I/Oの識別子へ対応付ける。
 * 有効極性が未確定のため生の電気レベルだけを扱い、初期化はCubeMXに任せる。
 * CS、PWM、用途未確定の無名ピンはこのAPIから操作しない。
 */
#include "Board/board_gpio.h"

#include <stddef.h>

#include "main.h"

typedef struct {
    GPIO_TypeDef *port;
    uint16_t pin;
} board_gpio_pin_t;

static const board_gpio_pin_t input_pins[BOARD_GPIO_INPUT_COUNT] = {
    [BOARD_GPIO_INPUT_DIP_0] = {DIP_0_GPIO_Port, DIP_0_Pin},
    [BOARD_GPIO_INPUT_DIP_1] = {DIP_1_GPIO_Port, DIP_1_Pin},
    [BOARD_GPIO_INPUT_DIP_2] = {DIP_2_GPIO_Port, DIP_2_Pin},
    [BOARD_GPIO_INPUT_DIP_3] = {DIP_3_GPIO_Port, DIP_3_Pin},
    [BOARD_GPIO_INPUT_SW_90] = {SW_90_GPIO_Port, SW_90_Pin},
    [BOARD_GPIO_INPUT_SW_2] = {SW_2_GPIO_Port, SW_2_Pin},
    [BOARD_GPIO_INPUT_IMU_FSYNC] = {IMU_FSYNC_GPIO_Port, IMU_FSYNC_Pin},
};

static const board_gpio_pin_t led_pins[BOARD_GPIO_LED_COUNT] = {
    [BOARD_GPIO_LED_0] = {LED_0_GPIO_Port, LED_0_Pin},
    [BOARD_GPIO_LED_1] = {LED_1_GPIO_Port, LED_1_Pin},
    [BOARD_GPIO_LED_2] = {LED_2_GPIO_Port, LED_2_Pin},
    [BOARD_GPIO_LED_3] = {LED_3_GPIO_Port, LED_3_Pin},
    [BOARD_GPIO_LED_R] = {LED_R_GPIO_Port, LED_R_Pin},
    [BOARD_GPIO_LED_G] = {LED_G_GPIO_Port, LED_G_Pin},
    [BOARD_GPIO_LED_B] = {LED_B_GPIO_Port, LED_B_Pin},
};

bool board_gpio_read_input(board_gpio_input_t input, bool *high)
{
    if ((unsigned int)input >= BOARD_GPIO_INPUT_COUNT || high == NULL) {
        return false;
    }

    const board_gpio_pin_t *pin = &input_pins[input];
    *high = HAL_GPIO_ReadPin(pin->port, pin->pin) == GPIO_PIN_SET;
    return true;
}

bool board_gpio_write_led_level(board_gpio_led_t led, bool high)
{
    if ((unsigned int)led >= BOARD_GPIO_LED_COUNT) {
        return false;
    }

    const board_gpio_pin_t *pin = &led_pins[led];
    HAL_GPIO_WritePin(pin->port, pin->pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
    return true;
}
