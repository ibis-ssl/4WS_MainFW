/* 旧HWの抵抗ラダーに対応する5方向SW判定。未取得・異常はNONEと区別する。 */
#ifndef USER_SWITCH_H
#define USER_SWITCH_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { USER_SWITCH_INVALID, USER_SWITCH_NONE, USER_SWITCH_CENTER,
    USER_SWITCH_BACK, USER_SWITCH_RIGHT, USER_SWITCH_FORWARD, USER_SWITCH_LEFT } user_switch_t;
user_switch_t user_switch_decode(uint16_t raw, bool valid);
user_switch_t user_switch_read(void);
#endif
