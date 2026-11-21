/* 旧電源基板の管理API。メイン処理専用、電源出力・保護値を自動設定しない。 */
#ifndef POWER_BOARD_H
#define POWER_BOARD_H
#include "Protocol/power_packet.h"
#include "Board/board_can.h"
typedef struct {
    bool battery_valid, capacitor_valid, temperature_valid, current_valid, flags_valid, fault_seen;
    float battery_voltage, capacitor_voltage, battery_current, fault_value;
    uint16_t fault_code;
    uint8_t fet_temp, coil_temp[2], flags;
    uint32_t battery_ms, capacitor_ms, temperature_ms, current_ms, flags_ms;
} power_board_status_t;
/* 戻り値bit0はCAN1での送信受理。ACK・機器動作完了は別に確認する。 */
uint8_t power_board_set_on(bool on);
uint8_t power_board_set_param(power_param_t param, float value);
uint8_t power_board_request_reset(void);
/* CAN1の受信だけを状態に反映する。validは受信済みを表す。 */
bool power_board_accept(board_can_bus_t bus, const board_can_frame_t *frame);
bool power_board_get_status(board_can_bus_t bus, power_board_status_t *status);
#endif
