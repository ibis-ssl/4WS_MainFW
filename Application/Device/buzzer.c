/* HAL_Delayを使わず鳴動時間を管理する。終了精度はメイン処理の呼出し間隔に依存する。 */
#include "Device/buzzer.h"
#include "Board/board_buzzer.h"
#include "Board/board_time.h"
#include <stddef.h>
static uint32_t start_ms, duration;
static bool active;
bool buzzer_start(uint32_t hz, uint32_t duration_ms)
{
    if (!board_buzzer_set_hz(hz, NULL)) { return false; }
    start_ms = board_millis();
    duration = duration_ms;
    active = true;
    return true;
}
void buzzer_stop(void) { board_buzzer_stop(); active = false; }
void buzzer_process(void)
{
    if (active && duration != 0U && (uint32_t)(board_millis() - start_ms) >= duration) { buzzer_stop(); }
}
