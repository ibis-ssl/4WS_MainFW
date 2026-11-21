/* 4WSで定めた電源バスに旧形式の電源管理指令を送る。 */
#include "Device/power_board.h"
#include <stddef.h>
static power_board_status_t statuses[BOARD_CAN_COUNT];
enum { SUPPLY_PERIOD_MS = 20, POWER_SWITCH_ID = 0x010, HIGH_VOLTAGE_ID = 0x110, HIGH_VOLTAGE_FLAGS = 0x07 };
static enum { SUPPLY_OFF, SUPPLY_ARMING, SUPPLY_ON } supply;
static uint32_t requested_ms;
static bool immediate_off, high_voltage_fault;
static uint8_t send_power(uint16_t id, const uint8_t data[8])
{
    return board_can_send(POWER_BOARD_CAN_BUS, id, data) ? 1U : 0U;
}
uint8_t power_board_set_on(bool on)
{
    uint8_t data[8]; power_packet_on(on, data); return send_power(0x010, data);
}
uint8_t power_board_set_param(power_param_t param, float value)
{
    uint8_t data[8];
    if (!power_packet_param(param, value, data)) { return 0; }
    return send_power(0x010, data);
}
uint8_t power_board_request_reset(void)
{
    uint8_t data[8];
    power_packet_reset(data);
    return board_can_send_immediate(POWER_BOARD_CAN_BUS, 0x001U, data, 8) ? 1U : 0U;
}

void power_board_supply_start(uint32_t now)
{
    supply = SUPPLY_OFF;
    requested_ms = now;
    immediate_off = true;
    high_voltage_fault = false;
}

void power_board_supply_process(uint32_t now, bool allowed)
{
    bool on = allowed && !power_board_supply_fault();
    uint8_t data[8];
    if (!on && supply != SUPPLY_OFF) {
        supply = SUPPLY_OFF;
        immediate_off = true;
    }
    if (on && supply == SUPPLY_OFF) {
        /* 充電停止は電源FWの給電タイマーも解除する。PowerOnより前に送る。 */
        power_packet_stop_charge(data);
        if (board_can_send_immediate(POWER_BOARD_CAN_BUS, HIGH_VOLTAGE_ID, data, 8)) {
            supply = SUPPLY_ARMING;
            requested_ms = now;
            immediate_off = false;
        }
        return;
    }
    if (!immediate_off && now - requested_ms < SUPPLY_PERIOD_MS) { return; }
    /* ACK待ちで滞った前回分を残さず、最新の許可状態だけを直送する。 */
    board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID);
    power_packet_on(on, data);
    if (!board_can_send_immediate(POWER_BOARD_CAN_BUS, POWER_SWITCH_ID, data, 8)) { return; }
    if (on && supply == SUPPLY_ARMING) {
        supply = SUPPLY_ON;
    }
    requested_ms = now;
    immediate_off = false;
}

const char *power_board_supply_status(void)
{
    if (power_board_supply_fault()) { return "FLT"; }
    static const char *const labels[] = {"OFF", "ARM", "ON"};
    return labels[supply];
}
bool power_board_supply_fault(void)
{
    return statuses[POWER_BOARD_CAN_BUS].fault_seen || high_voltage_fault;
}
bool power_board_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (bus != POWER_BOARD_CAN_BUS || frame == NULL) { return false; }
    power_packet_value_t value;
    if (!power_packet_decode(frame->id, frame->data, frame->length, &value)) { return false; }
    power_board_status_t *s = &statuses[bus];
    switch (value.kind) {
    case POWER_RX_BATTERY:
        s->battery_voltage = value.value; s->battery_ms = frame->received_ms; s->battery_valid = true; break;
    case POWER_RX_CAPACITOR:
        s->capacitor_voltage = value.value; s->capacitor_ms = frame->received_ms; s->capacitor_valid = true; break;
    case POWER_RX_TEMPERATURE:
        s->fet_temp = value.fet_temp; s->coil_temp[0] = value.coil_temp[0]; s->coil_temp[1] = value.coil_temp[1];
        s->temperature_ms = frame->received_ms; s->temperature_valid = true; break;
    case POWER_RX_CURRENT:
        s->battery_current = value.value; s->current_ms = frame->received_ms; s->current_valid = true; break;
    case POWER_RX_FLAGS:
        s->flags = value.flags; s->flags_ms = frame->received_ms; s->flags_valid = true;
        if (supply == SUPPLY_ON && (value.flags & HIGH_VOLTAGE_FLAGS) != 0U) { high_voltage_fault = true; }
        break;
    case POWER_RX_FAULT:
        s->fault_code = value.fault_code; s->fault_value = value.value; s->fault_seen = true; break;
    }
    return true;
}
bool power_board_get_status(board_can_bus_t bus, power_board_status_t *status)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || status == NULL) { return false; }
    *status = statuses[bus]; return true;
}
