/*
 * UART4のTX DMAバッファを送信完了まで保持し、RX文字を有限リングへ格納する。
 * DMA1 Channel2とUART4 IRQはこのモジュールが所有する。
 * 受信エラー後の再設定はメイン処理で行い、コマンドの意味はAppへ委ねる。
 */
#include "Board/board_debug_uart.h"
#include "Board/board_critical.h"
#include "usart.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define UART_TX_CAPACITY 2000U
#define UART_RX_SIZE 257U
static DMA_HandleTypeDef tx_dma;
static uint8_t tx_buffer[UART_TX_CAPACITY];
static char format_buffer[UART_TX_CAPACITY + 1U];
static uint8_t rx_buffer[UART_RX_SIZE], rx_byte;
static uint16_t rx_head, rx_tail;
static volatile bool tx_busy, rx_restart, tx_failed;
static bool initialized;
static board_debug_uart_stats_t stats;

bool board_debug_uart_init(void)
{
    if (initialized) { return false; }
    __HAL_RCC_DMAMUX1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    tx_dma.Instance = DMA1_Channel2;
    tx_dma.Init.Request = DMA_REQUEST_UART4_TX;
    tx_dma.Init.Direction = DMA_MEMORY_TO_PERIPH;
    tx_dma.Init.PeriphInc = DMA_PINC_DISABLE;
    tx_dma.Init.MemInc = DMA_MINC_ENABLE;
    tx_dma.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    tx_dma.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    tx_dma.Init.Mode = DMA_NORMAL;
    tx_dma.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&tx_dma) != HAL_OK) { return false; }
    __HAL_LINKDMA(&huart4, hdmatx, tx_dma);
    if (HAL_UART_Receive_IT(&huart4, &rx_byte, 1) != HAL_OK) { return false; }
    HAL_NVIC_SetPriority(DMA1_Channel2_IRQn, 10, 0);
    HAL_NVIC_SetPriority(UART4_IRQn, 9, 0);
    HAL_NVIC_ClearPendingIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_ClearPendingIRQ(UART4_IRQn);
    initialized = true;
    HAL_NVIC_EnableIRQ(DMA1_Channel2_IRQn);
    HAL_NVIC_EnableIRQ(UART4_IRQn);
    return true;
}
bool board_debug_uart_ready(void) { return initialized && !tx_busy && !tx_failed; }
bool board_debug_uart_write(const void *data, size_t length)
{
    if (!board_debug_uart_ready() || data == NULL || length == 0U || length > UART_TX_CAPACITY) {
        stats.tx_rejected++; return false;
    }
    memcpy(tx_buffer, data, length);
    tx_busy = true;
    if (HAL_UART_Transmit_DMA(&huart4, tx_buffer, (uint16_t)length) != HAL_OK) {
        tx_busy = false;
        uint32_t lock = board_critical_enter();
        stats.tx_errors++;
        board_critical_exit(lock);
        return false;
    }
    return true;
}
bool p(const char *format, ...)
{
    /* 制御IRQから誤って呼んでも重い整形処理へ入らない。 */
    if (__get_IPSR() != 0U) { return false; }
    if (format == NULL || !board_debug_uart_ready()) { stats.tx_rejected++; return false; }
    va_list args;
    va_start(args, format);
    int length = vsnprintf(format_buffer, sizeof(format_buffer), format, args);
    va_end(args);
    if (length <= 0 || (size_t)length > UART_TX_CAPACITY) { stats.tx_rejected++; return false; }
    return board_debug_uart_write(format_buffer, (size_t)length);
}
bool board_debug_uart_read(uint8_t *byte)
{
    if (byte == NULL) { return false; }
    uint32_t lock = board_critical_enter();
    bool available = rx_head != rx_tail;
    if (available) {
        *byte = rx_buffer[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1U) % UART_RX_SIZE);
    }
    board_critical_exit(lock);
    return available;
}
void board_debug_uart_get_stats(board_debug_uart_stats_t *out)
{
    if (out == NULL) { return; }
    uint32_t lock = board_critical_enter();
    *out = stats;
    board_critical_exit(lock);
}
void board_debug_uart_process(void)
{
    if (!initialized) { return; }
    if (tx_failed) {
        /* DMA停止を確認するまで内部送信領域を再利用しない。 */
        if (HAL_UART_AbortTransmit(&huart4) == HAL_OK) { tx_failed = false; tx_busy = false; }
    }
    if (rx_restart) {
        uint32_t lock = board_critical_enter();
        if (HAL_UART_AbortReceive(&huart4) == HAL_OK) {
            __HAL_UART_CLEAR_FLAG(&huart4, UART_CLEAR_OREF | UART_CLEAR_NEF | UART_CLEAR_FEF | UART_CLEAR_PEF);
            rx_restart = HAL_UART_Receive_IT(&huart4, &rx_byte, 1) != HAL_OK;
        }
        board_critical_exit(lock);
    }
}
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle != &huart4) { return; }
    /* エラー付き受信文字は操作入力として上位へ渡さない。 */
    if (handle->ErrorCode != HAL_UART_ERROR_NONE) {
        /* 再受信APIはErrorCodeを消すため、HAL IRQのエラー処理が終わるまで呼ばない。 */
        rx_restart = true;
        return;
    }
    uint16_t head = (uint16_t)((rx_head + 1U) % UART_RX_SIZE);
    if (head == rx_tail) { stats.rx_dropped++; }
    else { rx_buffer[rx_head] = rx_byte; __DMB(); rx_head = head; }
    if (HAL_UART_Receive_IT(handle, &rx_byte, 1) != HAL_OK) { rx_restart = true; }
}
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *handle)
{
    if (handle == &huart4) { stats.tx_completed++; tx_busy = false; }
}
void HAL_UART_ErrorCallback(UART_HandleTypeDef *handle)
{
    if (handle != &huart4) { return; }
    if (handle->ErrorCode & HAL_UART_ERROR_DMA) { stats.tx_errors++; tx_failed = true; }
    else { stats.rx_errors++; }
    rx_restart = true;
}
void DMA1_Channel2_IRQHandler(void) { HAL_DMA_IRQHandler(&tx_dma); }
void UART4_IRQHandler(void) { HAL_UART_IRQHandler(&huart4); }
