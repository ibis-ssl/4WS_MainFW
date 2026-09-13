/*
 * 基板の名前付きデジタル入力とLED端子を電気レベルで操作する。
 * MX_GPIO_Init()完了後に使用する。押下・点灯の有効極性は扱わない。
 */
#ifndef BOARD_GPIO_H
#define BOARD_GPIO_H

#include <stdbool.h>

typedef enum {
    BOARD_GPIO_INPUT_DIP_0,
    BOARD_GPIO_INPUT_DIP_1,
    BOARD_GPIO_INPUT_DIP_2,
    BOARD_GPIO_INPUT_DIP_3,
    BOARD_GPIO_INPUT_SW_90,
    BOARD_GPIO_INPUT_SW_2,
    BOARD_GPIO_INPUT_IMU_FSYNC,
    BOARD_GPIO_INPUT_COUNT
} board_gpio_input_t;

typedef enum {
    BOARD_GPIO_LED_0,
    BOARD_GPIO_LED_1,
    BOARD_GPIO_LED_2,
    BOARD_GPIO_LED_3,
    BOARD_GPIO_LED_R,
    BOARD_GPIO_LED_G,
    BOARD_GPIO_LED_B,
    BOARD_GPIO_LED_COUNT
} board_gpio_led_t;

/*
 * highにはHighならtrue、Lowならfalseを返す。デバウンスは行わない。
 * 不正な入力IDまたはNULLではfalseを返し、格納先を変更しない。
 * 成功時はtrueを返す。入力レベルの取得成功は回路の正常性を保証しない。
 */
bool board_gpio_read_input(board_gpio_input_t input, bool *high);

/*
 * high=trueでHigh、falseでLowを出力する。点灯・消灯の意味は持たない。
 * 不正なLED IDではfalseを返し、GPIOを操作しない。
 * trueは書込みを実行したことを表し、実際の端子電圧や点灯は未検証。
 * 同じLED端子の操作は呼出し側で一つの実行コンテキストに集約する。
 */
bool board_gpio_write_led_level(board_gpio_led_t led, bool high);

#endif
