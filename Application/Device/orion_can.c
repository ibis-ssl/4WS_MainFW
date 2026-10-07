/* 既知IDだけ固定長で保持し、旧機体のアクチュエーター指令は送信しない。 */
#include "Device/orion_can.h"
#include <stddef.h>
static const uint16_t ids[] = {0x000,0x001,0x200,0x201,0x202,0x203,0x204,
    0x210,0x211,0x212,0x213,0x214,0x215,0x216,0x220,0x221,0x222,0x223,
    0x224,0x230,0x231,0x232,0x233,0x234,0x240,0x241,0x500,0x501,0x502,0x503};
static orion_can_sample_t samples[BOARD_CAN_COUNT][sizeof(ids)/sizeof(ids[0])];
static uint32_t received[BOARD_CAN_COUNT];
static int slot(uint16_t id)
{
    for (unsigned int i = 0; i < sizeof(ids)/sizeof(ids[0]); ++i) { if (ids[i] == id) { return (int)i; } }
    return -1;
}
bool orion_can_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if ((unsigned int)bus >= BOARD_CAN_COUNT || frame == NULL) { return false; }
    int index = slot(frame->id);
    orion_value_t value;
    if (index < 0 || !orion_telemetry_decode(frame->id, frame->data, frame->length, &value)) { return false; }
    orion_can_sample_t *s = &samples[bus][index];
    s->value = value; s->received_ms = frame->received_ms; s->received = true; s->count++;
    received[bus]++; return true;
}
bool orion_can_read(board_can_bus_t bus, uint16_t id, uint32_t now, uint32_t max_age_ms, orion_can_sample_t *out)
{
    int index = slot(id);
    if ((unsigned int)bus >= BOARD_CAN_COUNT || index < 0 || out == NULL) { return false; }
    *out = samples[bus][index];
    out->fresh = out->received && (uint32_t)(now - out->received_ms) <= max_age_ms;
    return out->fresh;
}
uint32_t orion_can_received(board_can_bus_t bus)
{
    return (unsigned int)bus < BOARD_CAN_COUNT ? received[bus] : 0U;
}
