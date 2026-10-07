/* IEEE754 little-endianを明示的に復元し、非有限値と不正な長さを拒否する。 */
#include "Protocol/orion_telemetry.h"
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <string.h>
_Static_assert(sizeof(float) == 4 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128 && FLT_RADIX == 2,
    "32-bit IEEE float required");
static float get_float(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8U) | ((uint32_t)p[2] << 16U) | ((uint32_t)p[3] << 24U);
    float f; memcpy(&f, &v, 4); return f;
}
static int32_t signed_le(const uint8_t *p)
{
    uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8U);
    return v <= 32767U ? (int32_t)v : (int32_t)v - 65536;
}
bool orion_telemetry_decode(uint16_t id, const uint8_t *data, uint8_t length, orion_value_t *out)
{
    if (data == NULL || out == NULL || length != 8U) { return false; }
    orion_value_t v = {0};
    if (id <= 0x001U) {
        v.kind = ORION_ERROR; v.index = (uint8_t)id;
        v.integer[0] = data[0] | ((uint32_t)data[1] << 8U);
        v.integer[1] = data[2] | ((uint32_t)data[3] << 8U);
        v.value[0] = get_float(data + 4);
    } else if (id >= 0x200U && id <= 0x204U) {
        v.kind = ORION_ENCODER; v.index = (uint8_t)(id - 0x200U);
        v.value[0] = get_float(data); v.value[1] = get_float(data + 4);
    } else if (id >= 0x210U && id <= 0x216U) {
        v.kind = ORION_VOLTAGE; v.index = (uint8_t)(id - 0x210U);
        v.value[0] = get_float(data);
        if (v.value[0] < 0.0f) { return false; }
    } else if (id >= 0x220U && id <= 0x223U) {
        v.kind = ORION_MOTOR_TEMP; v.index = (uint8_t)(id - 0x220U);
        v.value[0] = get_float(data); v.value[1] = get_float(data + 4);
    } else if (id == 0x224U) {
        v.kind = ORION_POWER_TEMP;
        for (unsigned int i = 0; i < 3U; ++i) { v.integer[i] = data[i]; }
    } else if (id >= 0x230U && id <= 0x234U) {
        v.kind = ORION_CURRENT; v.index = (uint8_t)(id - 0x230U); v.value[0] = get_float(data);
    } else if (id == 0x240U) {
        v.kind = ORION_BALL; v.integer[0] = data[0]; v.integer[1] = data[1];
    } else if (id == 0x241U) {
        v.kind = ORION_MOUSE; v.integer[0] = signed_le(data); v.integer[1] = signed_le(data + 2);
        v.integer[2] = data[4] | ((uint32_t)data[5] << 8U);
    } else if (id >= 0x500U && id <= 0x503U) {
        v.kind = ORION_PARAMETER; v.index = (uint8_t)(id - 0x500U); v.value[0] = get_float(data);
    } else { return false; }
    if (!isfinite(v.value[0]) || !isfinite(v.value[1])) { return false; }
    *out = v; return true;
}
