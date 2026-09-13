/*
 * TIM6 IRQからのみ実行する制御入口。mainの負荷によらず入力を取り込む。
 * UART整形・待機・CAN HAL操作は行わない。将来の演算と出力調停はここへ接続する。
 */
#include "App/app_control.h"
static volatile user_switch_t sampled_switch = USER_SWITCH_INVALID;
void app_control_step(void)
{
    sampled_switch = user_switch_read();
}
user_switch_t app_control_user_switch(void) { return sampled_switch; }
