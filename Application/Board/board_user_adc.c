/*
 * ADC1 IN3の連続変換をDMA1 Channel1で取得する。旧SWと同じ12 bit・4回平均。
 * 半バッファ完了ごとの最新値を公開し、未取得を中央押下の0と混同しない。
 * ADC1とDMA1 Channel1は本モジュール専用。PHOTO/BATTの取得は含まない。
 */
#include "Board/board_user_adc.h"
#include "Board/board_critical.h"
#include "adc.h"
#include <stddef.h>
static DMA_HandleTypeDef adc_dma;
static volatile uint16_t samples[32];
static board_user_adc_sample_t latest;
static bool initialized;
static volatile bool valid;
static volatile uint32_t errors;
bool board_user_adc_init(void)
{
    if (initialized) { return false; }
    valid = false;
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    adc_dma.Instance = DMA1_Channel1;
    adc_dma.Init.Request = DMA_REQUEST_ADC1;
    adc_dma.Init.Direction = DMA_PERIPH_TO_MEMORY;
    adc_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    adc_dma.Init.MemInc = DMA_MINC_ENABLE;
    adc_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    adc_dma.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    adc_dma.Init.Mode = DMA_CIRCULAR;
    adc_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&adc_dma) != HAL_OK) { return false; }
    __HAL_LINKDMA(&hadc1, DMA_Handle, adc_dma);
    if (HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED) != HAL_OK ||
        HAL_ADC_Start_DMA(&hadc1, (uint32_t *)(uintptr_t)samples, 32) != HAL_OK) { return false; }
    initialized = true;
    HAL_NVIC_SetPriority(DMA1_Channel1_IRQn, 8, 0);
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 8, 0);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_ClearPendingIRQ(ADC1_2_IRQn);
    HAL_NVIC_EnableIRQ(DMA1_Channel1_IRQn);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
    return true;
}
bool board_user_adc_read(board_user_adc_sample_t *sample)
{
    if (sample == NULL) { return false; }
    uint32_t lock = board_critical_enter();
    bool ready = valid && (uint32_t)(HAL_GetTick() - latest.sampled_ms) <= 100U;
    if (ready) { *sample = latest; }
    board_critical_exit(lock);
    return ready;
}
uint32_t board_user_adc_error_count(void) { return errors; }
static void publish(unsigned int index)
{
    if (errors != 0U) { return; }
    latest.raw = samples[index];
    latest.sampled_ms = HAL_GetTick();
    valid = true;
}
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *handle) { if (handle == &hadc1) { publish(15); } }
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *handle) { if (handle == &hadc1) { publish(31); } }
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *handle)
{
    if (handle == &hadc1) { valid = false; errors++; }
}
void DMA1_Channel1_IRQHandler(void) { HAL_DMA_IRQHandler(&adc_dma); }
void ADC1_2_IRQHandler(void) { HAL_ADC_IRQHandler(&hadc1); }
