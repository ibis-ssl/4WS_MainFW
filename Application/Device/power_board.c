/* 旧仕様どおり両CANバスへ電源管理指令を送り、部分的な送信受理も呼出し側へ返す。 */
#include "Device/power_board.h"
#include <stddef.h>
static power_board_status_t statuses[BOARD_CAN_COUNT];
static uint8_t send_both(uint16_t id, const uint8_t data[8])
{
    uint8_t mask = 0;
    if (board_can_send(BOARD_CAN_1, id, data)) { mask |= 1U; }
    if (board_can_send(BOARD_CAN_2, id, data)) { mask |= 2U; }
    return mask;
}
uint8_t power_board_set_on(bool on)
{
    uint8_t data[8]; power_packet_on(on, data); return send_both(0x010, data);
}
uint8_t power_board_set_param(power_param_t param, float value)
{
    uint8_t data[8];
    if (!power_packet_param(param, value, data)) { return 0; }
    return send_both(0x010, data);
}
uint8_t power_board_request_reset(void)
{
    uint8_t data[8]; power_packet_reset(data); return send_both(0x001, data);
}
bool power_board_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || frame == NULL) { return false; }
    power_packet_value_t value;
    if (!power_packet_decode(frame->id, frame->data, frame->length, &value)) { return false; }
    power_board_status_t *s = &statuses[bus];
    switch (value.kind) {
    case POWER_RX_BATTERY:
        s->battery_voltage = value.voltage; s->battery_ms = frame->received_ms; s->battery_valid = true; break;
    case POWER_RX_CAPACITOR:
        s->capacitor_voltage = value.voltage; s->capacitor_ms = frame->received_ms; s->capacitor_valid = true; break;
    case POWER_RX_TEMPERATURE:
        s->fet_temp = value.fet_temp; s->coil_temp[0] = value.coil_temp[0]; s->coil_temp[1] = value.coil_temp[1];
        s->temperature_ms = frame->received_ms; s->temperature_valid = true; break;
    }
    return true;
}
bool power_board_get_status(board_can_bus_t bus, power_board_status_t *status)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || status == NULL) { return false; }
    *status = statuses[bus]; return true;
}
