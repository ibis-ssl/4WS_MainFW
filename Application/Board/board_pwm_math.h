/* 16 bitタイマーのHz指定をレジスター値へ変換する。実クロックの精度は呼出し側に依存する。 */
#ifndef BOARD_PWM_MATH_H
#define BOARD_PWM_MATH_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { uint16_t prescaler, period, pulse; uint32_t actual_millihz; } board_pwm_plan_t;
/* ブザー用の対応範囲は1～20000 Hz。無効指定ではplanを変更しない。 */
bool board_pwm_calculate(uint32_t clock_hz, uint32_t hz, board_pwm_plan_t *plan);
#endif
