/* 旧HWの抵抗ラダーに対応する5方向SW判定。未取得・異常はNONEと区別する。 */
#ifndef USER_SWITCH_H
#define USER_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
/* 検出したボタンのビットを立てる。無効なADC入力は全押下と区別できる値で返す。 */
typedef enum {
    USER_SWITCH_NONE = 0,
    USER_SWITCH_CENTER = 1U << 0,
    USER_SWITCH_BACK = 1U << 1,
    USER_SWITCH_RIGHT = 1U << 2,
    USER_SWITCH_FORWARD = 1U << 3,
    USER_SWITCH_LEFT = 1U << 4,
    USER_SWITCH_INVALID = 0xFF
} user_switch_t;
user_switch_t user_switch_decode(uint16_t raw, bool valid);
user_switch_t user_switch_read(void);
#endif
