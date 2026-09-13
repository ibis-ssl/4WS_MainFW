/*
 * TIM6とDWTサイクルカウンターをユーザー管理し、機体制御を高優先度IRQで起動する。
 * コールバック内で待機、printf、HALの通信操作を行わない。
 * 周期超過は記録し、溜まった更新を連続実行してIRQを占有することは避ける。
 */
#include "Board/board_control_timer.h"
#include "stm32g4xx_hal.h"
#include <stddef.h>
static TIM_HandleTypeDef control_timer;
static void (*control_callback)(void);
static volatile board_control_timer_stats_t stats;
static uint32_t period_cycles;

bool board_control_timer_start(uint32_t hz, void (*callback)(void))
{
    if (control_callback != NULL || callback == NULL || hz == 0U || hz > 1000000U ||
        1000000U % hz != 0U || 1000000U / hz > 65536U) { return false; }
    RCC_ClkInitTypeDef clocks;
    uint32_t latency;
    HAL_RCC_GetClockConfig(&clocks, &latency);
    uint32_t clock = HAL_RCC_GetPCLK1Freq();
    if (clocks.APB1CLKDivider != RCC_HCLK_DIV1) { clock *= 2U; }
    if (clock < 1000000U || clock % 1000000U != 0U || clock / 1000000U > 65536U) { return false; }
    __HAL_RCC_TIM6_CLK_ENABLE();
    control_timer.Instance = TIM6;
    control_timer.Init.Prescaler = clock / 1000000U - 1U;
    control_timer.Init.CounterMode = TIM_COUNTERMODE_UP;
    control_timer.Init.Period = 1000000U / hz - 1U;
    control_timer.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    if (HAL_TIM_Base_Init(&control_timer) != HAL_OK) { return false; }
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    period_cycles = HAL_RCC_GetHCLKFreq() / hz;
    control_callback = callback;
    __HAL_TIM_SET_COUNTER(&control_timer, 0);
    __HAL_TIM_CLEAR_FLAG(&control_timer, TIM_FLAG_UPDATE);
    HAL_NVIC_SetPriority(TIM6_DAC_IRQn, BOARD_CONTROL_IRQ_PRIORITY, 0);
    HAL_NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
    if (HAL_TIM_Base_Start_IT(&control_timer) != HAL_OK) { control_callback = NULL; return false; }
    HAL_NVIC_EnableIRQ(TIM6_DAC_IRQn);
    return true;
}
void TIM6_DAC_IRQHandler(void)
{
    if ((__HAL_TIM_GET_FLAG(&control_timer, TIM_FLAG_UPDATE) == RESET) ||
        (__HAL_TIM_GET_IT_SOURCE(&control_timer, TIM_IT_UPDATE) == RESET)) { return; }
    __HAL_TIM_CLEAR_FLAG(&control_timer, TIM_FLAG_UPDATE);
    uint32_t delay = __HAL_TIM_GET_COUNTER(&control_timer);
    uint32_t start = DWT->CYCCNT;
    if (control_callback != NULL) { control_callback(); }
    uint32_t elapsed = DWT->CYCCNT - start;
    stats.calls++;
    if (elapsed > stats.max_execution_cycles) { stats.max_execution_cycles = elapsed; }
    if (delay > stats.max_entry_delay_us) { stats.max_entry_delay_us = delay; }
    if (elapsed >= period_cycles || __HAL_TIM_GET_FLAG(&control_timer, TIM_FLAG_UPDATE) != RESET) {
        stats.overruns++;
        __HAL_TIM_CLEAR_FLAG(&control_timer, TIM_FLAG_UPDATE);
    }
}
void board_control_timer_get_stats(board_control_timer_stats_t *out)
{
    if (out == NULL) { return; }
    out->calls = stats.calls;
    out->overruns = stats.overruns;
    out->max_execution_cycles = stats.max_execution_cycles;
    out->max_entry_delay_us = stats.max_entry_delay_us;
}
