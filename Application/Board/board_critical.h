/* IRQとメイン処理の短い共有データ更新で、呼出し前の割り込み状態を保持する。 */
#ifndef BOARD_CRITICAL_H
#define BOARD_CRITICAL_H
#include "stm32g4xx_hal.h"
static inline uint32_t board_critical_enter(void)
{
    uint32_t state = __get_PRIMASK();
    __disable_irq();
    __DMB();
    return state;
}
static inline void board_critical_exit(uint32_t state)
{
    __DMB();
    __set_PRIMASK(state);
}
#endif
