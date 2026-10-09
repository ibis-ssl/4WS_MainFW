/* 実ソースを接続し、識別失敗・転送失敗・時刻周回・SH1106のページ指定を検証する。 */
#include "stm32g4xx_hal.h"
#include "Device/imu.h"
#include "Device/oled.h"
#include "Device/orion_can.h"
#include "Board/board_imu_spi.h"
#include "Board/board_oled_i2c.h"
#include "Board/board_gpio.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
SPI_HandleTypeDef hspi1;
I2C_HandleTypeDef hi2c1;
static uint8_t registers[128], display_address, selected_page, selected_column;
static uint8_t display_ram[8][132];
static bool spi_fail, i2c_fail, corrupt_config, cs_high = true;
static unsigned int spi_writes, i2c_calls;
/* App周期試験ではLEDの実端子を再現せず、出力呼出しだけを受理する。 */
bool board_gpio_write_led_level(board_gpio_led_t led, bool high)
{ (void)high; assert((unsigned int)led < BOARD_GPIO_LED_COUNT); return true; }
/* App試験では入力を開放Highとして扱い、実端子の押下は実機確認の対象とする。 */
bool board_gpio_read_input(board_gpio_input_t input, bool *high)
{ assert((unsigned int)input < BOARD_GPIO_INPUT_COUNT && high != NULL); *high = true; return true; }
HAL_StatusTypeDef HAL_I2C_Init(I2C_HandleTypeDef *h)
{ assert(h == &hi2c1 && h->Init.Timing == 0xF0F1FFFFU); return HAL_OK; }
HAL_StatusTypeDef HAL_I2CEx_ConfigAnalogFilter(I2C_HandleTypeDef *h, uint32_t value)
{ assert(h == &hi2c1 && value == I2C_ANALOGFILTER_ENABLE); return HAL_OK; }
HAL_StatusTypeDef HAL_I2CEx_ConfigDigitalFilter(I2C_HandleTypeDef *h, uint32_t value)
{ assert(h == &hi2c1 && value == 0U); return HAL_OK; }
void HAL_GPIO_Init(void *port, GPIO_InitTypeDef *pins)
{
    if (port == GPIOA && pins->Pin == GPIO_PIN_8) {
        assert(pins->Pull == GPIO_PULLDOWN && pins->Mode == GPIO_MODE_AF_PP && pins->Alternate == GPIO_AF6_TIM1);
        return;
    }
    assert(((port == GPIOB && pins->Pin == GPIO_PIN_7) || (port == GPIOA && pins->Pin == GPIO_PIN_15)) && pins->Pull == GPIO_PULLUP &&
    pins->Mode == GPIO_MODE_AF_OD && pins->Alternate == GPIO_AF4_I2C1); }
HAL_StatusTypeDef HAL_SPI_Init(SPI_HandleTypeDef *h)
{ assert(h == &hspi1 && h->Init.BaudRatePrescaler == 32U); return HAL_OK; }
void HAL_GPIO_WritePin(void *port, uint16_t pin, uint32_t value)
{ assert(port == (void *)1 && pin == 16U); cs_high = value != 0; }
HAL_StatusTypeDef HAL_SPI_TransmitReceive(SPI_HandleTypeDef *h, uint8_t *tx, uint8_t *rx, uint16_t n, uint32_t timeout)
{
    assert(h == &hspi1 && !cs_high && timeout == 2U && n <= 15U);
    if (spi_fail) { return HAL_ERROR; }
    memset(rx, 0, n);
    uint8_t reg = tx[0] & 0x7FU;
    if (tx[0] & 0x80U) {
        memcpy(rx + 1, registers + reg, n - 1U);
        if (reg == 0x3AU) { registers[reg] = 0; }
    } else {
        assert(n == 2U); spi_writes++;
        if (reg == 0x6BU && tx[1] == 0x80U) {
            uint8_t who = registers[0x75]; memset(registers, 0, sizeof(registers)); registers[0x75] = who;
        } else { registers[reg] = corrupt_config && reg == 0x1BU ? 0 : tx[1]; }
    }
    return HAL_OK;
}
HAL_StatusTypeDef HAL_I2C_IsDeviceReady(I2C_HandleTypeDef *h, uint16_t addr, uint32_t tries, uint32_t timeout)
{
    assert(h == &hi2c1 && tries == 1U && timeout == 3U);
    return display_address && addr == (uint16_t)(display_address << 1U) ? HAL_OK : HAL_ERROR;
}
HAL_StatusTypeDef HAL_I2C_Master_Transmit(I2C_HandleTypeDef *h, uint16_t addr, uint8_t *p, uint16_t n, uint32_t timeout)
{
    assert(h == &hi2c1 && addr == (uint16_t)(display_address << 1U) && n <= 33U && timeout == 25U);
    i2c_calls++;
    if (i2c_fail) { return HAL_ERROR; }
    if (p[0] == 0x40U) {
        assert(selected_page < 8U && selected_column + n - 1U <= 132U);
        memcpy(display_ram[selected_page] + selected_column, p + 1, n - 1U); selected_column += (uint8_t)(n - 1U);
    } else {
        assert(p[0] == 0);
        if (n == 4U && p[1] >= 0xB0U && p[1] <= 0xB7U) {
            selected_page = p[1] - 0xB0U; assert(p[2] == 2U && p[3] == 0x10U); selected_column = 2U;
        }
    }
    return HAL_OK;
}
static void put_float(uint8_t *p, float f)
{
    uint32_t bits; memcpy(&bits, &f, 4);
    for (unsigned int i=0; i<4U; ++i) { p[i] = (uint8_t)(bits >> (8U*i)); }
}
static void test_orion(void)
{
    uint8_t data[8] = {0}; orion_value_t value;
    const uint16_t ids[] = {0,1,0x200,0x204,0x210,0x216,0x220,0x223,0x224,0x230,0x234,0x240,0x241,0x500,0x503};
    for (unsigned int i=0; i<sizeof(ids)/sizeof(ids[0]); ++i) {
        assert(orion_telemetry_decode(ids[i], data, 8, &value));
        assert(!orion_telemetry_decode(ids[i], data, 7, &value));
    }
    assert(!orion_telemetry_decode(0x205, data, 8, &value));
    put_float(data, -2.5f); put_float(data+4, 1.25f);
    assert(orion_telemetry_decode(0x200, data, 8, &value));
    assert(value.value[0] == -2.5f && value.value[1] == 1.25f);
    assert(!orion_telemetry_decode(0x210, data, 8, &value));
    put_float(data, NAN); assert(!orion_telemetry_decode(0x200, data, 8, &value));
    put_float(data, INFINITY); assert(!orion_telemetry_decode(0x230, data, 8, &value));
    uint8_t mouse[8] = {0,0x80,0xFF,0x7F,0x34,0x12,0,0};
    assert(orion_telemetry_decode(0x241, mouse, 8, &value));
    assert(value.integer[0] == -32768 && value.integer[1] == 32767 && value.integer[2] == 0x1234);
    board_can_frame_t frame = {.id=0x200,.length=8,.received_ms=UINT32_MAX-50U};
    put_float(frame.data, 2.0f); put_float(frame.data+4, -1.0f);
    assert(orion_can_accept(BOARD_CAN_1, &frame));
    orion_can_sample_t sample;
    assert(orion_can_read(BOARD_CAN_1,0x200,49,100,&sample) && sample.received && sample.count == 1);
    assert(!orion_can_read(BOARD_CAN_1,0x200,50,100,&sample) && sample.received);
    assert(!orion_can_read(BOARD_CAN_2,0x200,49,100,&sample) && !sample.received);
    frame.length=4; assert(!orion_can_accept(BOARD_CAN_1,&frame));
}
static void test_imu(void)
{
    imu_status_t s;
    mock_tick = UINT32_MAX-50U; imu_init(); imu_process();
    assert(spi_writes == 0); imu_get_status(&s); assert(!s.ready && s.errors == 1);
    registers[0x75] = 0x12;
    mock_tick += 1000U; imu_process(); assert(cs_high);
    mock_tick += 100U; imu_process();
    assert(registers[0x19] == 1 && registers[0x1B] == 0x10);
    mock_tick += 100U; registers[0x3A] = 1;
    registers[0x3B] = 0x40; registers[0x3D] = 0x80; registers[0x43] = 0x40;
    imu_process(); assert(imu_get_status(&s) && s.ready && s.samples == 1);
    assert(fabsf(s.accel_mps2[0]-9.80665f)<0.00001f && s.raw_accel[1] == -32768);
    assert(s.gyro_dps[0] == 500.0f && s.temperature_c == 25.0f);
    mock_tick += 2U; imu_process(); imu_get_status(&s); assert(s.samples == 1);
    mock_tick += 101U; assert(!imu_get_status(&s));
    spi_fail = true; imu_process(); imu_get_status(&s); assert(!s.ready && !s.valid && cs_high);
    spi_fail = false;
    mock_tick += 1000U; imu_process();
    corrupt_config = true; mock_tick += 100U; imu_process();
    imu_get_status(&s); assert(!s.ready && !s.valid);
    corrupt_config = false;
    mock_tick += 1000U; imu_process(); mock_tick += 100U; imu_process();
    mock_tick += 100U; registers[0x3A]=1; imu_process(); assert(imu_get_status(&s));
    mock_tick += 1001U; imu_process(); imu_get_status(&s); assert(!s.ready && !s.valid);
    uint8_t byte; mock_ipsr=1; assert(!board_imu_spi_read(0x75,&byte,1)); mock_ipsr=0;
}
static void test_oled(void)
{
    assert(!oled_init()); display_address=0x3D; assert(oled_init());
    assert(oled_begin()); assert(oled_text(0,0,"4WS")); assert(oled_text(7,20,"XYZ"));
    assert(!oled_text(8,0,"X")); assert(oled_commit()); assert(!oled_begin());
    unsigned int before = i2c_calls;
    for (unsigned int i=0; i<137U; ++i) { unsigned int n=i2c_calls; oled_process(); assert(i2c_calls == n+1U); }
    oled_status_t s; oled_get_status(&s); assert(s.frames==1 && !s.busy && s.address==0x3D && i2c_calls==before+137U);
    assert(display_ram[0][2] != 0 && display_ram[0][0]==0 && display_ram[7][131]==0);
    assert(oled_begin() && oled_commit()); i2c_fail=true; oled_process(); oled_get_status(&s);
    assert(!s.ready && !s.busy && s.errors==2); i2c_fail=false;
    display_address=0x3C; assert(oled_init());
    mock_ipsr=1; uint8_t byte=0; assert(!board_oled_i2c_write(0x3C,false,&byte,1)); mock_ipsr=0;
}
void test_peripherals(void)
{
    test_orion(); test_imu(); test_oled();
    puts("PASS: Orion telemetry, ICM-20602, SH1106 failure/boundary tests");
}
