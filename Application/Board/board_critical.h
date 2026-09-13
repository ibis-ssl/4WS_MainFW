/* 通信・ADCの共有更新だけを保護し、優先度2の制御IRQは停止しない。 */
#ifndef BOARD_CRITICAL_H
#define BOARD_CRITICAL_H
#include "stm32g4xx_hal.h"
static inline uint32_t board_critical_enter(void)
{
    uint32_t state = __get_BASEPRI();
    __set_BASEPRI_MAX(5U << (8U - __NVIC_PRIO_BITS));
    __DSB();
    __ISB();
    __DMB();
    return state;
}
static inline void board_critical_exit(uint32_t state)
{
    __DMB();
    __set_BASEPRI(state);
}
#endif
