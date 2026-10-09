/*
 * APB2の実設定からTIM1クロックを求め、Hz指定をPWMへ反映する。
 * 停止時はPWMをCCR=0で維持して端子をLowにする。ESC等のタイマーは操作しない。
 */
#include "Board/board_buzzer.h"
#include "Board/board_pwm_math.h"
#include "tim.h"
#include "main.h"
#include <stddef.h>
static bool initialized;
static uint32_t timer_clock(void)
{
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t clock = HAL_RCC_GetPCLK2Freq();
    return clocks.APB2CLKDivider == RCC_HCLK_DIV1 ? clock : clock * 2U;
}
bool board_buzzer_init(void)
{
    if (initialized) { return false; }
    /* PWMの開始前や一時停止中も端子が浮かないよう、内部プルダウンを適用する。
     * 鳴動終了時はPWMを動かしたままCCR=0にして、Lowを能動的に出力する。 */
    GPIO_InitTypeDef pin = {0};
    pin.Pin = BUZZER_Pin;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_PULLDOWN;
    pin.Speed = GPIO_SPEED_FREQ_LOW;
    pin.Alternate = GPIO_AF6_TIM1;
    HAL_GPIO_Init(BUZZER_GPIO_Port, &pin);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    initialized = HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) == HAL_OK;
    return initialized;
}
bool board_buzzer_set_hz(uint32_t hz, uint32_t *actual_millihz)
{
    board_pwm_plan_t plan;
    if (!initialized || !board_pwm_calculate(timer_clock(), hz, &plan)) { return false; }
    if (HAL_TIM_PWM_Stop(&htim1, TIM_CHANNEL_1) != HAL_OK) { return false; }
    __HAL_TIM_SET_PRESCALER(&htim1, plan.prescaler);
    __HAL_TIM_SET_AUTORELOAD(&htim1, plan.period);
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, plan.pulse);
    __HAL_TIM_SET_COUNTER(&htim1, 0);
    /* PSCの反映を次の自然更新まで遅らせず、指定周期で再開する。 */
    htim1.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
    if (HAL_TIM_PWM_Start(&htim1, TIM_CHANNEL_1) != HAL_OK) {
        __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
        return false;
    }
    if (actual_millihz != NULL) { *actual_millihz = plan.actual_millihz; }
    return true;
}
void board_buzzer_stop(void)
{
    if (!initialized) { return; }
    __HAL_TIM_SET_COMPARE(&htim1, TIM_CHANNEL_1, 0);
    htim1.Instance->EGR = TIM_EGR_UG;
    __HAL_TIM_CLEAR_FLAG(&htim1, TIM_FLAG_UPDATE);
}
