/* 小分けの同期転送でmainの占有を抑える。制御IRQは割り込み可能なまま維持する。 */
#include "Board/board_oled_i2c.h"
#include "i2c.h"
#include <string.h>
bool board_oled_i2c_init(void)
{
    if (__get_IPSR() != 0U || HAL_RCC_GetPCLK1Freq() != 170000000U) { return false; }
    /* 弱い内部プルアップで疎通確認するため、SCLを約20 kHzまで下げる。
     * 170 MHz / 16を基準にHigh/Low各256カウント。立上り時間は実測未確認。
     * 通常運用のI2C仕様適合は外部抵抗を追加して別途確認する。 */
    __HAL_I2C_DISABLE(&hi2c1);
    hi2c1.Init.Timing = 0xF0F1FFFFU;
    if (HAL_I2C_Init(&hi2c1) != HAL_OK ||
        HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK ||
        HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0U) != HAL_OK) { return false; }
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_7;
    pins.Mode = GPIO_MODE_AF_OD;
    pins.Pull = GPIO_PULLUP;
    pins.Speed = GPIO_SPEED_FREQ_LOW;
    pins.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &pins);
    pins.Pin = GPIO_PIN_15;
    HAL_GPIO_Init(GPIOA, &pins);
    return true;
}
bool board_oled_i2c_probe(uint8_t address)
{
    return __get_IPSR() == 0U && (address == 0x3CU || address == 0x3DU) &&
        HAL_I2C_IsDeviceReady(&hi2c1, (uint16_t)(address << 1U), 1U, 3U) == HAL_OK;
}
bool board_oled_i2c_write(uint8_t address, bool data, const uint8_t *bytes, size_t length)
{
    if (__get_IPSR() != 0U || bytes == NULL || length == 0U || length > 32U ||
        (address != 0x3CU && address != 0x3DU)) { return false; }
    uint8_t packet[33];
    packet[0] = data ? 0x40U : 0x00U;
    memcpy(packet + 1, bytes, length);
    return HAL_I2C_Master_Transmit(&hi2c1, (uint16_t)(address << 1U), packet,
        (uint16_t)(length + 1U), 25U) == HAL_OK;
}
