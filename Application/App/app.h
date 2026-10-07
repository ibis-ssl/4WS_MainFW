/* 基礎ドライバの起動と処理を統括する。機体制御・アクチュエーター出力は未実装。 */
#ifndef APP_H
#define APP_H
#include <stdbool.h>
/* ユーザー指定の全High点灯。bit 0～6はLED_0/1/2/3/R/G/Bに対応する。
 * 点灯時間は各200 msで、最後は全消灯する。 */
#define APP_STARTUP_LED_ACTIVE_HIGH_MASK 0x7FU
#define APP_STARTUP_LED_DURATION_MS 200U
bool app_init(void);
void app_process(void);
#endif
