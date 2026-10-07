/* センサー座標系の未校正値を公開する。機体座標への変換・姿勢推定は含めない。 */
#ifndef IMU_H
#define IMU_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool ready, valid;
    uint8_t who_am_i;
    uint32_t samples, errors, sampled_ms;
    int16_t raw_accel[3], raw_gyro[3], raw_temperature;
    float accel_mps2[3], gyro_dps[3], temperature_c;
} imu_status_t;
void imu_init(void);
void imu_process(void);
bool imu_get_status(imu_status_t *out);
#endif
