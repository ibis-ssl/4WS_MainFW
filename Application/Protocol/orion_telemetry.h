/* 旧Orion受信形式。IDの機体割当や角度の符号変更はここでは行わない。 */
#ifndef ORION_TELEMETRY_H
#define ORION_TELEMETRY_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { ORION_ERROR, ORION_ENCODER, ORION_VOLTAGE, ORION_MOTOR_TEMP,
    ORION_POWER_TEMP, ORION_CURRENT, ORION_BALL, ORION_MOUSE, ORION_PARAMETER } orion_kind_t;
typedef struct {
    orion_kind_t kind;
    uint8_t index;
    float value[2];
    int32_t integer[3];
} orion_value_t;
bool orion_telemetry_decode(uint16_t id, const uint8_t *data, uint8_t length, orion_value_t *out);
#endif
