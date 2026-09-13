/* 旧電源基板のCAN形式。電源管理のみを対象とし、キッカー指令は含めない。 */
#ifndef POWER_PACKET_H
#define POWER_PACKET_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { POWER_PARAM_MIN_VOLTAGE = 1, POWER_PARAM_MAX_VOLTAGE,
    POWER_PARAM_MAX_CURRENT, POWER_PARAM_MAX_FET_TEMP, POWER_PARAM_MAX_COIL_TEMP } power_param_t;
typedef enum { POWER_RX_BATTERY, POWER_RX_CAPACITOR, POWER_RX_TEMPERATURE } power_rx_kind_t;
typedef struct { power_rx_kind_t kind; float voltage; uint8_t fet_temp, coil_temp[2]; } power_packet_value_t;
bool power_packet_on(bool on, uint8_t data[8]);
bool power_packet_param(power_param_t param, float value, uint8_t data[8]);
bool power_packet_reset(uint8_t data[8]);
/* 対象は0x215/216/224、旧フレーム長8 byteのみ。無関係・異常値はfalse。 */
bool power_packet_decode(uint16_t id, const uint8_t *data, uint8_t length, power_packet_value_t *value);
#endif
