/* Mainの起動時だけ、電源と2枚のステア基板を再起動し、走行出力を0 Aにする。 */
#include "App/app_board_reset.h"
#include "Device/power_board.h"
#include "Protocol/atlas_motor_packet.h"

enum {
    ATLAS_BOARD = 11, ATLAS_RESET = 31, BOARD_ID_FIELD = 20, BOARD_FAULT_FIELD = 1,
    RPC_TAG = 0xF6, POWER_SETTLE_MS = 300, REBOOT_SETTLE_MS = 300,
    RPC_TIMEOUT_MS = 120, STARTUP_TIMEOUT_MS = 5000
};

typedef enum {
    FIND_BOARD, WAIT_ID, SEND_RESET, WAIT_RESET_ACK, WAIT_REBOOT, CHECK_BOARD, WAIT_HEALTH, BOARD_DONE, BOARD_ERROR
} reset_phase_t;

typedef struct {
    reset_phase_t phase;
    uint32_t request_at;
    uint8_t offset, sequence;
    bool zero_submitted, coast_submitted;
} board_reset_t;

static board_reset_t boards[BOARD_CAN_COUNT];
static app_board_reset_status_t status;
static uint32_t started_at, power_at;
static bool power_submitted;

void app_board_reset_start(uint32_t now)
{
    started_at = now;
    power_submitted = false;
    status = BOARD_RESET_PENDING;
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        /* 既知の配線を先に試し、応答しなければ別の保守offsetも照会する。 */
        boards[i] = (board_reset_t){.offset = i == BOARD_CAN_1 ? 16U : 0U};
    }
}

app_board_reset_status_t app_board_reset_status(void)
{
    return status;
}

bool app_board_reset_power_allowed(uint32_t now)
{
    return status != BOARD_RESET_FAILED && power_submitted && now - power_at >= POWER_SETTLE_MS &&
           boards[BOARD_CAN_1].phase != BOARD_ERROR && boards[BOARD_CAN_2].phase != BOARD_ERROR &&
           boards[BOARD_CAN_1].zero_submitted && boards[BOARD_CAN_1].coast_submitted &&
           boards[BOARD_CAN_2].zero_submitted && boards[BOARD_CAN_2].coast_submitted;
}

static void fail(void)
{
    status = BOARD_RESET_FAILED;
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        board_can_cancel_immediate((board_can_bus_t)i);
    }
}

static void fail_board(board_can_bus_t bus)
{
    boards[bus].phase = BOARD_ERROR;
    board_can_cancel_immediate(bus);
}

static void send_request(board_can_bus_t bus, uint32_t now, uint8_t opcode, uint8_t field, reset_phase_t reply_phase)
{
    board_reset_t *board = &boards[bus];
    uint8_t data[8];
    atlas_encode_rpc(data, opcode, RPC_TAG, board->sequence + 1U, field);
    if (board_can_send_immediate(bus, (uint16_t)(0x5D0U + board->offset), data, 8)) {
        board->sequence++;
        board->request_at = now;
        board->phase = reply_phase;
    }
}

void app_board_reset_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (status != BOARD_RESET_PENDING) { return; }
    board_reset_t *board = &boards[bus];
    if (board->phase == BOARD_DONE && frame->id == 0U && frame->length == 8U) {
        fail_board(bus);
        return;
    }
    if (frame->id != 0x5D1U + board->offset || frame->length != 8U ||
        frame->received_ms - board->request_at >= RPC_TIMEOUT_MS) { return; }
    uint32_t value;
    switch (board->phase) {
    case WAIT_ID:
        if (atlas_decode_rpc(frame->data, ATLAS_BOARD, RPC_TAG, board->sequence, &value) &&
            value == (unsigned int)bus) {
            board->phase = SEND_RESET;
        }
        break;
    case WAIT_RESET_ACK:
        if (atlas_decode_rpc(frame->data, ATLAS_RESET, RPC_TAG, board->sequence, &value) && value == 0U) {
            board->request_at = frame->received_ms;
            board->phase = WAIT_REBOOT;
        }
        break;
    case WAIT_HEALTH:
        if (atlas_decode_rpc(frame->data, ATLAS_BOARD, RPC_TAG, board->sequence, &value)) {
            if (value == 0U) { board->phase = BOARD_DONE; }
            else { fail_board(bus); }
        }
        break;
    default:
        break;
    }
}

static void reset_steering(board_can_bus_t bus, uint32_t now)
{
    board_reset_t *board = &boards[bus];
    uint32_t elapsed = now - board->request_at;
    switch (board->phase) {
    case FIND_BOARD:
        send_request(bus, now, ATLAS_BOARD, BOARD_ID_FIELD, WAIT_ID);
        break;
    case WAIT_ID:
        if (elapsed >= RPC_TIMEOUT_MS) { board->offset ^= 16U; board->phase = FIND_BOARD; }
        break;
    case SEND_RESET:
        send_request(bus, now, ATLAS_RESET, 0, WAIT_RESET_ACK);
        break;
    case WAIT_RESET_ACK:
        /* 実行済みのMCUへ再送しない。ACKがない場合は出力禁止を維持する。 */
        if (elapsed >= RPC_TIMEOUT_MS) { fail_board(bus); }
        break;
    case WAIT_REBOOT:
        if (elapsed >= REBOOT_SETTLE_MS) { board->phase = CHECK_BOARD; }
        break;
    case CHECK_BOARD:
        send_request(bus, now, ATLAS_BOARD, BOARD_FAULT_FIELD, WAIT_HEALTH);
        break;
    case WAIT_HEALTH:
        if (elapsed >= RPC_TIMEOUT_MS) { board->phase = CHECK_BOARD; }
        break;
    case BOARD_DONE:
    case BOARD_ERROR:
        break;
    }
}

void app_board_reset_process(uint32_t now)
{
    if (status != BOARD_RESET_PENDING) { return; }
    if (now - started_at >= STARTUP_TIMEOUT_MS) { fail(); return; }

    const uint8_t zero[8] = {0};
    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        board_reset_t *board = &boards[i];
        if (!board->zero_submitted) {
            board->zero_submitted = board_can_send_immediate((board_can_bus_t)i, 0x200U, zero, 8);
        }
        if (!board->coast_submitted) {
            board->coast_submitted = board_can_send_immediate((board_can_bus_t)i, (uint16_t)(0x350U + 2U * i), zero, 2);
        }
    }
    if (!power_submitted && boards[BOARD_CAN_1].zero_submitted && boards[BOARD_CAN_1].coast_submitted) {
        power_submitted = power_board_request_reset() != 0U;
        power_at = now;
    }
    if (!power_submitted || now - power_at < POWER_SETTLE_MS) { return; }

    for (unsigned int i = 0; i < BOARD_CAN_COUNT; ++i) {
        if (boards[i].zero_submitted && boards[i].coast_submitted) { reset_steering((board_can_bus_t)i, now); }
    }
    if (boards[BOARD_CAN_1].phase >= BOARD_DONE && boards[BOARD_CAN_2].phase >= BOARD_DONE) {
        status = boards[BOARD_CAN_1].phase == BOARD_DONE && boards[BOARD_CAN_2].phase == BOARD_DONE ?
            BOARD_RESET_READY : BOARD_RESET_FAILED;
    }
}
