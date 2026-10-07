/* SPI1とIMU CSを専有する。有限待ちの転送はmainからのみ呼ぶ。 */
#ifndef BOARD_IMU_SPI_H
#define BOARD_IMU_SPI_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
bool board_imu_spi_init(void);
bool board_imu_spi_read(uint8_t reg, uint8_t *data, size_t length);
bool board_imu_spi_write(uint8_t reg, uint8_t value);
#endif
