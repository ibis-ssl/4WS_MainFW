/* HALの時刻依存をBoardへ集約する。SysTickの1 ms更新を前提とする。 */
#include "Board/board_time.h"
#include "stm32g4xx_hal.h"
uint32_t board_millis(void) { return HAL_GetTick(); }
