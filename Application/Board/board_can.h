/* Classic CANの転送を担当する。公開APIはメイン処理から使用し、機器IDの意味は上位が扱う。 */
#ifndef BOARD_CAN_H
#define BOARD_CAN_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { BOARD_CAN_1, BOARD_CAN_2, BOARD_CAN_COUNT } board_can_bus_t;
typedef struct {
    uint16_t id;
    uint8_t length;
    uint8_t data[8];
    uint32_t received_ms;
} board_can_frame_t;
typedef struct {
    uint32_t tx_submitted, tx_buffered, tx_dropped, tx_errors;
    uint32_t rx_received, rx_dropped, rx_invalid, rx_errors, rx_lost;
    uint32_t bus_off_events;
} board_can_stats_t;
/* 生成初期化後に一度呼ぶ。両バス開始成功時だけtrue。失敗時は開始済みバスも停止する。 */
bool board_can_init(void);
/* 8 byteをコピーして受理する。trueは送信完了・相手機器の応答を意味しない。 */
bool board_can_send(board_can_bus_t bus, uint16_t id, const uint8_t data[8]);
bool board_can_receive(board_can_bus_t bus, board_can_frame_t *frame);
bool board_can_get_stats(board_can_bus_t bus, board_can_stats_t *stats);
/* 一時的なHAL送信失敗後もFIFOを再処理する。bus-off解除は自動実施しない。 */
void board_can_process(void);
#endif
