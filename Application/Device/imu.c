/* ICM-20602を識別してから設定する。待機は時刻で管理し、再試行は1秒間隔。 */
#include "Device/imu.h"
#include "Board/board_imu_spi.h"
#include "Board/board_time.h"
#include <stddef.h>
#include <string.h>
static imu_status_t status;
static uint32_t phase_ms, poll_ms;
static unsigned int phase;
static int16_t signed_be(const uint8_t *p)
{
    uint16_t v = (uint16_t)((uint16_t)p[0] << 8U) | p[1];
    return (int16_t)(v <= 32767U ? (int32_t)v : (int32_t)v - 65536);
}
void imu_init(void)
{
    memset(&status, 0, sizeof(status));
    phase = 0; phase_ms = board_millis() - 1000U;
}
static void fail(uint32_t now)
{
    status.errors++; status.ready = false; status.valid = false;
    phase = 0; phase_ms = now;
}
void imu_process(void)
{
    uint32_t now = board_millis();
    if (phase == 0U) {
        if ((uint32_t)(now - phase_ms) < 1000U) { return; }
        if (!board_imu_spi_init() || !board_imu_spi_read(0x75, &status.who_am_i, 1U) ||
            status.who_am_i != 0x12U || !board_imu_spi_write(0x6B, 0x80)) { fail(now); return; }
        phase = 1; phase_ms = now; return;
    }
    if (phase == 1U) {
        if ((uint32_t)(now - phase_ms) < 100U) { return; }
        /* PLLクロック、全軸有効、500 Hz、旧Orionと同じ±2 g・±1000 dps。 */
        const uint8_t settings[][2] = {{0x6B,0x01},{0x6C,0x00},{0x70,0x40},
            {0x19,0x01},{0x1A,0x01},{0x1B,0x10},{0x1C,0x00},{0x1D,0x01},{0x38,0x01}};
        for (unsigned int i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
            if (!board_imu_spi_write(settings[i][0], settings[i][1])) { fail(now); return; }
        }
        /* 書込みACKを持たないSPIなので、設定読戻しも確認する。 */
        for (unsigned int i = 0; i < sizeof(settings)/sizeof(settings[0]); ++i) {
            uint8_t value;
            if (!board_imu_spi_read(settings[i][0], &value, 1U) || value != settings[i][1]) { fail(now); return; }
        }
        phase = 2; phase_ms = now; return;
    }
    if (phase == 2U) {
        if ((uint32_t)(now - phase_ms) < 100U) { return; }
        status.ready = true; phase = 3; poll_ms = now - 2U;
    }
    if ((uint32_t)(now - poll_ms) < 2U) { return; }
    poll_ms = now;
    uint8_t ready, raw[14];
    if (!board_imu_spi_read(0x3A, &ready, 1U)) { fail(now); return; }
    if (!(ready & 1U)) {
        if ((uint32_t)(now - (status.valid ? status.sampled_ms : phase_ms)) > 1000U) { fail(now); }
        return;
    }
    if (!board_imu_spi_read(0x3B, raw, sizeof(raw))) { fail(now); return; }
    for (unsigned int i = 0; i < 3U; ++i) {
        status.raw_accel[i] = signed_be(raw + 2U*i);
        status.raw_gyro[i] = signed_be(raw + 8U + 2U*i);
        status.accel_mps2[i] = status.raw_accel[i] * (9.80665f / 16384.0f);
        status.gyro_dps[i] = status.raw_gyro[i] * (1000.0f / 32768.0f);
    }
    status.raw_temperature = signed_be(raw + 6);
    status.temperature_c = status.raw_temperature / 326.8f + 25.0f;
    status.sampled_ms = now; status.samples++; status.valid = true;
}
bool imu_get_status(imu_status_t *out)
{
    if (out == NULL) { return false; }
    *out = status;
    out->valid = status.valid && (uint32_t)(board_millis() - status.sampled_ms) <= 100U;
    return out->valid;
}
