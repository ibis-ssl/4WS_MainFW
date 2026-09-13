/* PSC+1とARR+1を含めて周期を丸め、使用可能な範囲で大きいARRを選んで分解能を確保する。 */
#include "Board/board_pwm_math.h"
#include <stddef.h>
bool board_pwm_calculate(uint32_t clock_hz, uint32_t hz, board_pwm_plan_t *plan)
{
    if (plan == NULL || clock_hz == 0U || hz == 0U || hz > 20000U) { return false; }
    uint64_t span = (uint64_t)hz * 65536U;
    uint64_t divider = ((uint64_t)clock_hz + span - 1U) / span;
    if (divider == 0U || divider > 65536U) { return false; }
    uint64_t base = divider * hz;
    uint64_t ticks = ((uint64_t)clock_hz + base / 2U) / base;
    if (ticks < 2U || ticks > 65536U) { return false; }
    plan->prescaler = (uint16_t)(divider - 1U);
    plan->period = (uint16_t)(ticks - 1U);
    plan->pulse = (uint16_t)(ticks / 2U);
    uint64_t divisor = divider * ticks;
    plan->actual_millihz = (uint32_t)(((uint64_t)clock_hz * 1000U + divisor / 2U) / divisor);
    return true;
}
