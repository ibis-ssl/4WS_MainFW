/*
 * 旧Classic CANの2バス・8 byte送信と20フレーム待ちFIFOを提供する。
 * 転送とIRQの排他をこの層が所有し、受信は有限FIFOでメイン処理へ渡す。
 * NVICと割り込み入口はユーザー管理。CubeMX側で重複生成しないこと。
 */
#include "Board/board_can.h"
#include "Board/board_critical.h"
#include "fdcan.h"
#include <stddef.h>
#include <string.h>

#define CAN_QUEUE_SIZE 21U
typedef struct {
    board_can_frame_t tx[CAN_QUEUE_SIZE], rx[CAN_QUEUE_SIZE];
    uint8_t tx_head, tx_tail, rx_head, rx_tail;
    board_can_stats_t stats;
    bool started;
} can_state_t;
static can_state_t states[BOARD_CAN_COUNT];
static FDCAN_HandleTypeDef *const handles[] = {&hfdcan1, &hfdcan2};
static const IRQn_Type irqs[] = {FDCAN1_IT0_IRQn, FDCAN2_IT0_IRQn};
static uint8_t next(uint8_t index) { return (uint8_t)((index + 1U) % CAN_QUEUE_SIZE); }
static int bus_index(FDCAN_HandleTypeDef *handle)
{
    for (int i = 0; i < BOARD_CAN_COUNT; ++i) { if (handles[i] == handle) { return i; } }
    return -1;
}
/* 呼出し側がIRQとの排他を取る。ハードウェアFIFOの空き分だけ処理する。 */
static void drain(unsigned int bus)
{
    can_state_t *s = &states[bus];
    FDCAN_TxHeaderTypeDef header = {
        .IdType = FDCAN_STANDARD_ID, .TxFrameType = FDCAN_DATA_FRAME,
        .DataLength = FDCAN_DLC_BYTES_8, .ErrorStateIndicator = FDCAN_ESI_ACTIVE,
        .BitRateSwitch = FDCAN_BRS_OFF, .FDFormat = FDCAN_CLASSIC_CAN,
        .TxEventFifoControl = FDCAN_NO_TX_EVENTS, .MessageMarker = 0,
    };
    while (s->tx_tail != s->tx_head && HAL_FDCAN_GetTxFifoFreeLevel(handles[bus]) > 0U) {
        board_can_frame_t *frame = &s->tx[s->tx_tail];
        header.Identifier = frame->id;
        if (HAL_FDCAN_AddMessageToTxFifoQ(handles[bus], &header, frame->data) != HAL_OK) {
            s->stats.tx_errors++;
            break;
        }
        s->stats.tx_submitted++;
        s->tx_tail = next(s->tx_tail);
    }
}
bool board_can_init(void)
{
    if (states[0].started || states[1].started) { return false; }
    memset(states, 0, sizeof(states));
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        FDCAN_FilterTypeDef filter = {
            .IdType = FDCAN_STANDARD_ID, .FilterIndex = 0,
            .FilterType = FDCAN_FILTER_MASK, .FilterConfig = FDCAN_FILTER_TO_RXFIFO0,
            .FilterID1 = 0, .FilterID2 = 0,
        };
        if (HAL_FDCAN_ConfigFilter(handles[i], &filter) != HAL_OK ||
            HAL_FDCAN_ConfigGlobalFilter(handles[i], FDCAN_REJECT, FDCAN_REJECT,
                                        FDCAN_REJECT_REMOTE, FDCAN_REJECT_REMOTE) != HAL_OK ||
            HAL_FDCAN_ConfigInterruptLines(handles[i], FDCAN_IT_GROUP_RX_FIFO0 |
                FDCAN_IT_GROUP_TX_FIFO_ERROR | FDCAN_IT_GROUP_BIT_LINE_ERROR,
                FDCAN_INTERRUPT_LINE0) != HAL_OK ||
            HAL_FDCAN_Start(handles[i]) != HAL_OK) { goto fail; }
        states[i].started = true;
        if (HAL_FDCAN_ActivateNotification(handles[i], FDCAN_IT_RX_FIFO0_NEW_MESSAGE |
                FDCAN_IT_RX_FIFO0_MESSAGE_LOST | FDCAN_IT_TX_FIFO_EMPTY | FDCAN_IT_BUS_OFF, 0) != HAL_OK) {
            goto fail;
        }
    }
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        HAL_NVIC_SetPriority(irqs[i], 5U + i, 0);
        HAL_NVIC_ClearPendingIRQ(irqs[i]);
        HAL_NVIC_EnableIRQ(irqs[i]);
    }
    return true;
fail:
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        HAL_NVIC_DisableIRQ(irqs[i]);
        if (states[i].started) { (void)HAL_FDCAN_Stop(handles[i]); }
        states[i].started = false;
    }
    return false;
}
bool board_can_send(board_can_bus_t bus, uint16_t id, const uint8_t data[8])
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || id > 0x7FFU || data == NULL) { return false; }
    uint32_t lock = board_critical_enter();
    can_state_t *s = &states[bus];
    if (!s->started) { board_critical_exit(lock); return false; }
    drain(bus);
    uint8_t head = next(s->tx_head);
    if (head == s->tx_tail) {
        s->stats.tx_dropped++;
        board_critical_exit(lock);
        return false;
    }
    s->tx[s->tx_head].id = id;
    memcpy(s->tx[s->tx_head].data, data, 8);
    bool queued = s->tx_head != s->tx_tail || HAL_FDCAN_GetTxFifoFreeLevel(handles[bus]) == 0U;
    s->tx_head = head;
    if (queued) { s->stats.tx_buffered++; }
    drain(bus);
    board_critical_exit(lock);
    return true;
}
bool board_can_receive(board_can_bus_t bus, board_can_frame_t *frame)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || frame == NULL) { return false; }
    uint32_t lock = board_critical_enter();
    can_state_t *s = &states[bus];
    bool available = s->rx_tail != s->rx_head;
    if (available) { *frame = s->rx[s->rx_tail]; s->rx_tail = next(s->rx_tail); }
    board_critical_exit(lock);
    return available;
}
bool board_can_get_stats(board_can_bus_t bus, board_can_stats_t *stats)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || stats == NULL) { return false; }
    uint32_t lock = board_critical_enter();
    *stats = states[bus].stats;
    board_critical_exit(lock);
    return true;
}
void board_can_process(void)
{
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        uint32_t lock = board_critical_enter();
        if (states[i].started) {
            drain(i);
            /* IRQ処理上限後の残りも回収し、新たな受信イベントに依存させない。 */
            HAL_FDCAN_RxFifo0Callback(handles[i], 0);
        }
        board_critical_exit(lock);
    }
}
void HAL_FDCAN_RxFifo0Callback(FDCAN_HandleTypeDef *handle, uint32_t flags)
{
    int bus = bus_index(handle);
    if (bus < 0) { return; }
    can_state_t *s = &states[bus];
    if (flags & FDCAN_IT_RX_FIFO0_MESSAGE_LOST) { s->stats.rx_lost++; }
    /* 継続トラフィックでIRQがメイン処理を占有しないよう1回の処理数を制限する。 */
    for (unsigned int n = 0; n < 3U && HAL_FDCAN_GetRxFifoFillLevel(handle, FDCAN_RX_FIFO0) > 0U; ++n) {
        FDCAN_RxHeaderTypeDef header;
        uint8_t raw[64];
        if (HAL_FDCAN_GetRxMessage(handle, FDCAN_RX_FIFO0, &header, raw) != HAL_OK) {
            s->stats.rx_errors++; break;
        }
        if (header.IdType != FDCAN_STANDARD_ID || header.RxFrameType != FDCAN_DATA_FRAME ||
            header.FDFormat != FDCAN_CLASSIC_CAN || header.Identifier > 0x7FFU ||
            header.DataLength > FDCAN_DLC_BYTES_8) { s->stats.rx_invalid++; continue; }
        uint8_t head = next(s->rx_head);
        if (head == s->rx_tail) { s->stats.rx_dropped++; continue; }
        board_can_frame_t *frame = &s->rx[s->rx_head];
        frame->id = (uint16_t)header.Identifier;
        frame->length = (uint8_t)header.DataLength;
        memset(frame->data, 0, sizeof(frame->data));
        memcpy(frame->data, raw, frame->length);
        frame->received_ms = HAL_GetTick();
        __DMB();
        s->rx_head = head;
        s->stats.rx_received++;
    }
}
void HAL_FDCAN_TxFifoEmptyCallback(FDCAN_HandleTypeDef *handle)
{
    int bus = bus_index(handle);
    if (bus >= 0) {
        uint32_t lock = board_critical_enter();
        drain((unsigned int)bus);
        board_critical_exit(lock);
    }
}
void HAL_FDCAN_ErrorStatusCallback(FDCAN_HandleTypeDef *handle, uint32_t flags)
{
    int bus = bus_index(handle);
    if (bus >= 0 && (flags & FDCAN_IT_BUS_OFF)) { states[bus].stats.bus_off_events++; }
}
void FDCAN1_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan1); }
void FDCAN2_IT0_IRQHandler(void) { HAL_FDCAN_IRQHandler(&hfdcan2); }
