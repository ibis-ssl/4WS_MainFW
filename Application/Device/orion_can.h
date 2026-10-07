/* バス・IDごとに最新値と時刻を保持する。APIはmain専用で、未受信と古い値を区別する。 */
#ifndef ORION_CAN_H
#define ORION_CAN_H
#include "Board/board_can.h"
#include "Protocol/orion_telemetry.h"
typedef struct {
    bool received, fresh;
    uint32_t received_ms, count;
    orion_value_t value;
} orion_can_sample_t;
bool orion_can_accept(board_can_bus_t bus, const board_can_frame_t *frame);
bool orion_can_read(board_can_bus_t bus, uint16_t id, uint32_t now, uint32_t max_age_ms, orion_can_sample_t *out);
uint32_t orion_can_received(board_can_bus_t bus);
#endif
