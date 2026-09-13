/* 機体制御IRQの入口。演算と出力は未実装で、現在は取得済みSWを周期ごとに取り込む。 */
#ifndef APP_CONTROL_H
#define APP_CONTROL_H
#include "Device/user_switch.h"
/* ユーザー指定の500 Hz。旧世代のレジスター近似ではなく正確な2 msを設定する。 */
#define APP_CONTROL_FREQUENCY_HZ 500U
void app_control_step(void);
user_switch_t app_control_user_switch(void);
#endif
