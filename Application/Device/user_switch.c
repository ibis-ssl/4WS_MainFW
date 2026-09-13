/* 旧世代と同じADC境界値を適用する。デバウンスや長押し・出力操作はここに含めない。 */
#include "Device/user_switch.h"
user_switch_t user_switch_decode(uint16_t raw, bool valid)
{
    if (!valid || raw > 4095U) { return USER_SWITCH_INVALID; }
    if (raw <= 100U) { return USER_SWITCH_CENTER; }
    if (raw <= 500U) { return USER_SWITCH_BACK; }
    if (raw <= 2000U) { return USER_SWITCH_RIGHT; }
    if (raw <= 3000U) { return USER_SWITCH_FORWARD; }
    if (raw <= 3900U) { return USER_SWITCH_LEFT; }
    return USER_SWITCH_NONE;
}
