/* 電源パケットの整数・floatを明示したlittle-endianで変換し、旧形式を維持する。 */
#include "Protocol/power_packet.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
               "32-bit IEEE float required");
static void put_float(uint8_t *data, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    for (unsigned int i = 0; i < 4; ++i) { data[i] = (uint8_t)(bits >> (8U * i)); }
}
static float get_float(const uint8_t *data)
{
    uint32_t bits = (uint32_t)data[0] | ((uint32_t)data[1] << 8U) |
        ((uint32_t)data[2] << 16U) | ((uint32_t)data[3] << 24U);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
bool power_packet_on(bool on, uint8_t data[8])
{
    if (data == NULL) { return false; }
    memset(data, 0, 8); data[1] = on ? 1 : 0;
    return true;
}
bool power_packet_param(power_param_t param, float value, uint8_t data[8])
{
    if (data == NULL || param < POWER_PARAM_MIN_VOLTAGE || param > POWER_PARAM_MAX_COIL_TEMP ||
        !isfinite(value) || value < 0.0f) { return false; }
    memset(data, 0, 8); data[0] = (uint8_t)param; put_float(&data[1], value);
    return true;
}
bool power_packet_reset(uint8_t data[8])
{
    if (data == NULL) { return false; }
    memset(data, 0, 8); data[0] = 0x5A; data[1] = 0xA5;
    return true;
}
void power_packet_stop_charge(uint8_t data[8])
{
    /* Orionの0x110 standby。F303側では昇圧目標とPWMも解除する。 */
    power_packet_kicker(1U, 0U, data);
}
void power_packet_set_charge_voltage(float volts, uint8_t data[8])
{
    memset(data, 0, 8);
    put_float(data + 4, volts);
}
void power_packet_kicker(uint8_t command, uint8_t value, uint8_t data[8])
{
    memset(data, 0, 8);
    data[0] = command;
    data[1] = value;
    data[2] = 200U;
}
bool power_packet_decode(uint16_t id, const uint8_t *data, uint8_t length, power_packet_value_t *value)
{
    if (data == NULL || value == NULL) { return false; }
    power_packet_value_t result = {0};
    if (id == 0x215U || id == 0x216U || id == 0x234U) {
        if (length != 4U && length != 8U) { return false; }
        result.kind = id == 0x215U ? POWER_RX_BATTERY : id == 0x216U ? POWER_RX_CAPACITOR : POWER_RX_CURRENT;
        result.value = get_float(data);
        if (!isfinite(result.value) || (result.kind != POWER_RX_CURRENT && result.value < 0.0f)) { return false; }
    } else if (id == 0x224U) {
        if (length != 8U) { return false; }
        result.kind = POWER_RX_TEMPERATURE;
        result.fet_temp = data[0]; result.coil_temp[0] = data[1]; result.coil_temp[1] = data[2];
    } else if (id == 0x244U && length == 8U) {
        result.kind = POWER_RX_FLAGS;
        result.flags = data[0];
    } else if (id == 0x000U && length == 8U && data[0] == 100U && data[1] == 0U) {
        result.kind = POWER_RX_FAULT;
        result.fault_code = (uint16_t)(data[2] | ((uint16_t)data[3] << 8U));
        result.value = get_float(data + 4);
        if (!isfinite(result.value)) { return false; }
    } else { return false; }
    *value = result;
    return true;
}
