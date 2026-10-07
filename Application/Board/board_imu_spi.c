/* ICM-20602の上限10 MHzを超えないよう、170 MHz入力を32分周する。 */
#include "Board/board_imu_spi.h"
#include "spi.h"
#include "main.h"
#include <string.h>
bool board_imu_spi_init(void)
{
    HAL_GPIO_WritePin(SPI_IMU_CS_GPIO_Port, SPI_IMU_CS_Pin, GPIO_PIN_SET);
    hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_32;
    return HAL_SPI_Init(&hspi1) == HAL_OK;
}
static bool transfer(uint8_t *tx, uint8_t *rx, uint16_t length)
{
    if (__get_IPSR() != 0U) { return false; }
    HAL_GPIO_WritePin(SPI_IMU_CS_GPIO_Port, SPI_IMU_CS_Pin, GPIO_PIN_RESET);
    bool ok = HAL_SPI_TransmitReceive(&hspi1, tx, rx, length, 2U) == HAL_OK;
    HAL_GPIO_WritePin(SPI_IMU_CS_GPIO_Port, SPI_IMU_CS_Pin, GPIO_PIN_SET);
    return ok;
}
bool board_imu_spi_read(uint8_t reg, uint8_t *data, size_t length)
{
    if (data == NULL || length == 0U || length > 14U) { return false; }
    uint8_t tx[15] = {0}, rx[15];
    tx[0] = reg | 0x80U;
    if (!transfer(tx, rx, (uint16_t)(length + 1U))) { return false; }
    memcpy(data, rx + 1, length);
    return true;
}
bool board_imu_spi_write(uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {reg & 0x7FU, value}, rx[2];
    return transfer(tx, rx, 2U);
}
