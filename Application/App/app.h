/* 基礎ドライバの起動と処理を統括する。機体制御・アクチュエーター出力は未実装。 */
#ifndef APP_H
#define APP_H
#include <stdbool.h>
bool app_init(void);
void app_process(void);
#endif
