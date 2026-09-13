/* ホスト検証用の最小HAL。実機レジスターやタイミングの再現は目的としない。 */
#ifndef TEST_HAL_H
#define TEST_HAL_H
#include <stdint.h>
#include <stddef.h>
typedef enum { HAL_OK, HAL_ERROR, HAL_BUSY } HAL_StatusTypeDef;
typedef int IRQn_Type;
enum { FDCAN1_IT0_IRQn, FDCAN2_IT0_IRQn, DMA1_Channel1_IRQn, DMA1_Channel2_IRQn, ADC1_2_IRQn, UART4_IRQn, TIM6_DAC_IRQn };
extern uint32_t mock_primask, mock_tick;
extern uint32_t mock_basepri, mock_ipsr;
#define __NVIC_PRIO_BITS 4U
static inline uint32_t __get_BASEPRI(void) { return mock_basepri; }
static inline void __set_BASEPRI(uint32_t value) { mock_basepri = value; }
static inline void __set_BASEPRI_MAX(uint32_t value) { if (!mock_basepri || value < mock_basepri) { mock_basepri = value; } }
static inline uint32_t __get_IPSR(void) { return mock_ipsr; }
static inline void __DSB(void) { }
static inline void __ISB(void) { }
static inline uint32_t __get_PRIMASK(void) { return mock_primask; }
static inline void __disable_irq(void) { mock_primask = 1; }
static inline void __set_PRIMASK(uint32_t value) { mock_primask = value; }
extern void (*mock_dmb_hook)(void);
static inline void __DMB(void) { if (mock_dmb_hook) { void (*hook)(void)=mock_dmb_hook; mock_dmb_hook=NULL; hook(); } }
uint32_t HAL_GetTick(void);
void HAL_NVIC_SetPriority(IRQn_Type irq, uint32_t a, uint32_t b);
void HAL_NVIC_EnableIRQ(IRQn_Type irq);
void HAL_NVIC_DisableIRQ(IRQn_Type irq);
void HAL_NVIC_ClearPendingIRQ(IRQn_Type irq);
#define __HAL_RCC_DMA1_CLK_ENABLE() ((void)0)
#define __HAL_RCC_DMAMUX1_CLK_ENABLE() ((void)0)
typedef struct { unsigned int index; } FDCAN_HandleTypeDef;
extern FDCAN_HandleTypeDef hfdcan1, hfdcan2;
typedef struct { uint32_t IdType, FilterIndex, FilterType, FilterConfig, FilterID1, FilterID2; } FDCAN_FilterTypeDef;
typedef struct { uint32_t Identifier, IdType, TxFrameType, DataLength, ErrorStateIndicator,
    BitRateSwitch, FDFormat, TxEventFifoControl, MessageMarker; } FDCAN_TxHeaderTypeDef;
typedef struct { uint32_t Identifier, IdType, RxFrameType, DataLength, FDFormat; } FDCAN_RxHeaderTypeDef;
#define FDCAN_STANDARD_ID 0U
#define FDCAN_DATA_FRAME 0U
#define FDCAN_CLASSIC_CAN 0U
#define FDCAN_DLC_BYTES_8 8U
#define FDCAN_ESI_ACTIVE 0U
#define FDCAN_BRS_OFF 0U
#define FDCAN_NO_TX_EVENTS 0U
#define FDCAN_FILTER_MASK 1U
#define FDCAN_FILTER_TO_RXFIFO0 1U
#define FDCAN_REJECT 2U
#define FDCAN_REJECT_REMOTE 1U
#define FDCAN_IT_GROUP_RX_FIFO0 1U
#define FDCAN_IT_GROUP_TX_FIFO_ERROR 2U
#define FDCAN_IT_GROUP_BIT_LINE_ERROR 4U
#define FDCAN_INTERRUPT_LINE0 0U
#define FDCAN_IT_RX_FIFO0_NEW_MESSAGE 1U
#define FDCAN_IT_RX_FIFO0_MESSAGE_LOST 2U
#define FDCAN_IT_TX_FIFO_EMPTY 4U
#define FDCAN_IT_BUS_OFF 8U
#define FDCAN_RX_FIFO0 0U
HAL_StatusTypeDef HAL_FDCAN_ConfigFilter(FDCAN_HandleTypeDef *, FDCAN_FilterTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ConfigGlobalFilter(FDCAN_HandleTypeDef *, uint32_t, uint32_t, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_ConfigInterruptLines(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_Start(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_Stop(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_ActivateNotification(FDCAN_HandleTypeDef *, uint32_t, uint32_t);
uint32_t HAL_FDCAN_GetTxFifoFreeLevel(FDCAN_HandleTypeDef *);
HAL_StatusTypeDef HAL_FDCAN_AddMessageToTxFifoQ(FDCAN_HandleTypeDef *, FDCAN_TxHeaderTypeDef *, uint8_t *);
uint32_t HAL_FDCAN_GetRxFifoFillLevel(FDCAN_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_FDCAN_GetRxMessage(FDCAN_HandleTypeDef *, uint32_t, FDCAN_RxHeaderTypeDef *, uint8_t *);
void HAL_FDCAN_IRQHandler(FDCAN_HandleTypeDef *);
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *, uint32_t);
void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef *);
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *, uint32_t);
typedef struct { uint32_t Request, Direction, PeriphInc, MemInc, PeriphDataAlignment,
    MemDataAlignment, Mode, Priority; } DMA_InitTypeDef;
typedef struct { void *Instance; DMA_InitTypeDef Init; void *Parent; } DMA_HandleTypeDef;
#define DMA1_Channel1 ((void *)1)
#define DMA1_Channel2 ((void *)2)
#define DMA_REQUEST_UART4_TX 30U
#define DMA_REQUEST_ADC1 5U
#define DMA_MEMORY_TO_PERIPH 1U
#define DMA_PERIPH_TO_MEMORY 0U
#define DMA_PINC_DISABLE 0U
#define DMA_MINC_ENABLE 1U
#define DMA_PDATAALIGN_BYTE 0U
#define DMA_MDATAALIGN_BYTE 0U
#define DMA_PDATAALIGN_HALFWORD 1U
#define DMA_MDATAALIGN_HALFWORD 1U
#define DMA_NORMAL 0U
#define DMA_CIRCULAR 1U
#define DMA_PRIORITY_LOW 0U
#define __HAL_LINKDMA(handle, field, dma) do { (handle)->field = &(dma); (dma).Parent = (handle); } while (0)
HAL_StatusTypeDef HAL_DMA_Init(DMA_HandleTypeDef *);
void HAL_DMA_IRQHandler(DMA_HandleTypeDef *);
typedef struct { DMA_HandleTypeDef *hdmatx; uint32_t ErrorCode; } UART_HandleTypeDef;
extern UART_HandleTypeDef huart4;
#define HAL_UART_ERROR_NONE 0U
#define HAL_UART_ERROR_DMA 16U
#define UART_CLEAR_OREF 1U
#define UART_CLEAR_NEF 2U
#define UART_CLEAR_FEF 4U
#define UART_CLEAR_PEF 8U
#define __HAL_UART_CLEAR_FLAG(handle, flags) ((void)(handle), (void)(flags))
HAL_StatusTypeDef HAL_UART_Receive_IT(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_Transmit_DMA(UART_HandleTypeDef *, uint8_t *, uint16_t);
HAL_StatusTypeDef HAL_UART_AbortTransmit(UART_HandleTypeDef *);
HAL_StatusTypeDef HAL_UART_AbortReceive(UART_HandleTypeDef *);
void HAL_UART_IRQHandler(UART_HandleTypeDef *);
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *);
void HAL_UART_ErrorCallback(UART_HandleTypeDef *);
typedef struct { DMA_HandleTypeDef *DMA_Handle; } ADC_HandleTypeDef;
extern ADC_HandleTypeDef hadc1;
#define ADC_SINGLE_ENDED 0U
HAL_StatusTypeDef HAL_ADCEx_Calibration_Start(ADC_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_ADC_Start_DMA(ADC_HandleTypeDef *, uint32_t *, uint32_t);
void HAL_ADC_IRQHandler(ADC_HandleTypeDef *);
void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef *);
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *);
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *);
typedef struct { uint32_t PSC, ARR, CCR1, CNT, EGR, SR, DIER; } TIM_TypeDef;
typedef struct { uint32_t Prescaler, CounterMode, Period, AutoReloadPreload; } TIM_Base_InitTypeDef;
typedef struct { TIM_TypeDef *Instance; TIM_Base_InitTypeDef Init; } TIM_HandleTypeDef;
extern TIM_HandleTypeDef htim1;
extern TIM_TypeDef mock_tim6;
#define TIM6 (&mock_tim6)
#define __HAL_RCC_TIM6_CLK_ENABLE() ((void)0)
#define TIM_COUNTERMODE_UP 0U
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0U
#define RESET 0U
#define TIM_IT_UPDATE 1U
typedef struct { uint32_t APB2CLKDivider, APB1CLKDivider; } RCC_ClkInitTypeDef;
typedef struct { uint32_t DEMCR; } mock_core_debug_t;
typedef struct { uint32_t CTRL, CYCCNT; } mock_dwt_t;
extern mock_core_debug_t mock_core_debug;
extern mock_dwt_t mock_dwt;
#define CoreDebug (&mock_core_debug)
#define DWT (&mock_dwt)
#define CoreDebug_DEMCR_TRCENA_Msk 1U
#define DWT_CTRL_CYCCNTENA_Msk 1U
#define RCC_HCLK_DIV1 1U
#define TIM_CHANNEL_1 0U
#define TIM_EGR_UG 1U
#define TIM_FLAG_UPDATE 1U
#define __HAL_TIM_SET_COMPARE(h, ch, value) ((h)->Instance->CCR1 = (value))
#define __HAL_TIM_SET_PRESCALER(h, value) ((h)->Instance->PSC = (value))
#define __HAL_TIM_SET_AUTORELOAD(h, value) ((h)->Instance->ARR = (value))
#define __HAL_TIM_SET_COUNTER(h, value) ((h)->Instance->CNT = (value))
#define __HAL_TIM_CLEAR_FLAG(h, value) ((h)->Instance->SR &= ~(value))
#define __HAL_TIM_GET_FLAG(h, value) ((h)->Instance->SR & (value))
#define __HAL_TIM_GET_IT_SOURCE(h, value) ((h)->Instance->DIER & (value))
#define __HAL_TIM_GET_COUNTER(h) ((h)->Instance->CNT)
HAL_StatusTypeDef HAL_TIM_Base_Init(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_Base_Start_IT(TIM_HandleTypeDef *);
HAL_StatusTypeDef HAL_TIM_PWM_Start(TIM_HandleTypeDef *, uint32_t);
HAL_StatusTypeDef HAL_TIM_PWM_Stop(TIM_HandleTypeDef *, uint32_t);
void HAL_RCC_GetClockConfig(RCC_ClkInitTypeDef *, uint32_t *);
uint32_t HAL_RCC_GetPCLK2Freq(void);
uint32_t HAL_RCC_GetPCLK1Freq(void);
uint32_t HAL_RCC_GetHCLKFreq(void);
#endif
