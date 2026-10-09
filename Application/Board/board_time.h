/* システムの単調増加ms時刻。約49.7日で周回するため差分はuint32_tで計算する。 */
#ifndef BOARD_TIME_H
#define BOARD_TIME_H
#include <stdint.h>
uint32_t board_millis(void);
/* 制御タイマー起動後のDWTサイクル数。HCLKごとに増加し、32 bitの周回を差分で扱う。 */
uint32_t board_cycles(void);
#endif
