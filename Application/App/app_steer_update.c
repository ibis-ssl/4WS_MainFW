/* ステアV3ブートの停止待ち更新。全イメージの検証後にだけ対象へ書込みを開始する。 */
#include "App/app_steer_update.h"
#include "App/app_board_reset.h"
#include "App/app_motor_debug.h"
#include "Protocol/atlas_motor_packet.h"
#include <string.h>

enum {
    CAPACITY = 0x18000, APP_ADDRESS = 0x08004000, TAG_OFFSET = 0x200,
    HARDWARE = 0x34444344, FORMAT = 3, DATA_FRAMES = 43,
    U_ENTER = 1, U_INFO = 2, U_SIZE = 3, U_CRC = 4, U_HARDWARE = 5, U_TARGET = 6,
    U_BEGIN = 0x10, U_BLOCK = 0x11, U_WRITE = 0x12, U_FINISH = 0x13, U_REBOOT = 0x14, U_DATA = 0x20,
    RPC_BOARD = 11, BOARD_ID = 20, RPC_TAG = 0xF4,
    FAIL_TIMEOUT = 1, FAIL_TARGET, FAIL_VERIFY, FAIL_IMAGE,
    STAGING_TIMEOUT = 5000, BOOT_WAIT = 300, REQUEST_TIMEOUT = 250, MAX_ATTEMPTS = 4
};

typedef enum {
    STOP_OUTPUTS, FIND_APP, OLD_CRC, ENTER_BOOT, HARDWARE_CHECK, TARGET_CHECK, CAPACITY_CHECK,
    BEGIN_FLASH, OPEN_BLOCK, SEND_DATA, COMMIT_BLOCK, FINISH_FLASH, VERIFY_SIZE, VERIFY_CRC,
    REBOOT_APP, FIND_NEW_APP, VERIFY_NEW_APP
} update_step_t;

static uint8_t image[CAPACITY];
static steer_update_status_t state;
static uint32_t stage_crc, block_crc, touched_at, request_at;
static uint8_t offset, data_index, attempts, sequence, output_stopped;
static bool awaiting, target_changed;

static uint32_t read_le(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc32(uint32_t crc, const uint8_t *p, uint32_t length)
{
    while (length--) {
        crc ^= *p++;
        for (unsigned int i = 0; i < 8U; ++i) {
            crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0xEDB88320U);
        }
    }
    return crc;
}

static void fail(uint8_t reason)
{
    state.error = reason;
    state.state = STEER_UPDATE_FAILED;
    awaiting = false;
    if (target_changed) { board_can_cancel_immediate((board_can_bus_t)state.board); }
}

static void advance(update_step_t step)
{
    state.step = (uint8_t)step;
    awaiting = false;
    attempts = 0;
}

bool app_steer_update_locked(void)
{
    return state.state == STEER_UPDATE_STAGING || state.state == STEER_UPDATE_RUNNING ||
        (state.state == STEER_UPDATE_FAILED && target_changed);
}

steer_update_status_t app_steer_update_status(void) { return state; }

bool app_steer_update_begin(uint8_t board, uint32_t size, uint32_t crc, uint32_t now)
{
    if (board >= BOARD_CAN_COUNT || size < TAG_OFFSET + 8U || size > CAPACITY ||
        state.state == STEER_UPDATE_STAGING || state.state == STEER_UPDATE_RUNNING ||
        app_board_reset_status() == BOARD_RESET_PENDING || !app_motor_debug_idle() ||
        (target_changed && state.board != board)) { return false; }
    state = (steer_update_status_t){.state = STEER_UPDATE_STAGING, .board = board, .size = size, .image_crc = crc};
    stage_crc = UINT32_MAX;
    touched_at = now;
    return true;
}

bool app_steer_update_stage(uint32_t at, const uint8_t *data, uint32_t length, uint32_t now)
{
    if (state.state != STEER_UPDATE_STAGING || data == NULL || at > state.staged ||
        length == 0U || length > STEER_UPDATE_CHUNK_SIZE || length > state.size - at) { return false; }
    if (at < state.staged) {
        return length <= state.staged - at && memcmp(image + at, data, length) == 0;
    }
    memcpy(image + at, data, length);
    stage_crc = crc32(stage_crc, data, length);
    state.staged += length;
    touched_at = now;
    return true;
}

bool app_steer_update_commit(uint32_t now)
{
    if (state.state != STEER_UPDATE_STAGING || state.staged != state.size) { return false; }
    uint32_t sp = read_le(image), vector = read_le(image + 4);
    if (~stage_crc != state.image_crc || sp <= 0x20000000U || sp > 0x20020000U ||
        (sp & 7U) != 0U || (vector & 1U) == 0U || (vector & ~1U) < APP_ADDRESS ||
        (vector & ~1U) >= APP_ADDRESS + state.size ||
        read_le(image + TAG_OFFSET) != HARDWARE || read_le(image + TAG_OFFSET + 4) != FORMAT) {
        fail(FAIL_IMAGE);
        return false;
    }
    uint32_t padded = (state.size + STEER_UPDATE_CHUNK_SIZE - 1U) & ~(STEER_UPDATE_CHUNK_SIZE - 1U);
    memset(image + state.size, 0xFF, padded - state.size);
    state.state = STEER_UPDATE_RUNNING;
    output_stopped = 0;
    touched_at = now;
    if (!target_changed) { offset = state.board == BOARD_CAN_1 ? 16U : 0U; }
    advance(STOP_OUTPUTS);
    return true;
}

bool app_steer_update_abort(void)
{
    /* CAN起動後はホスト切断に依存せず、Main内で確定か失敗まで進める。 */
    if (state.state != STEER_UPDATE_STAGING) { return false; }
    state.state = target_changed ? STEER_UPDATE_FAILED : STEER_UPDATE_IDLE;
    return true;
}

static uint8_t opcode(update_step_t step)
{
    static const uint8_t commands[] = {
        [OLD_CRC] = U_CRC, [ENTER_BOOT] = U_ENTER, [HARDWARE_CHECK] = U_HARDWARE,
        [TARGET_CHECK] = U_TARGET, [CAPACITY_CHECK] = U_INFO, [BEGIN_FLASH] = U_BEGIN,
        [OPEN_BLOCK] = U_BLOCK, [SEND_DATA] = U_DATA, [COMMIT_BLOCK] = U_WRITE,
        [FINISH_FLASH] = U_FINISH, [VERIFY_SIZE] = U_SIZE, [VERIFY_CRC] = U_CRC,
        [REBOOT_APP] = U_REBOOT, [VERIFY_NEW_APP] = U_CRC,
    };
    return commands[step];
}

static void accept_update_reply(update_step_t step, uint32_t value)
{
    switch (step) {
    case OLD_CRC: state.old_crc = value; advance(ENTER_BOOT); break;
    case ENTER_BOOT: advance(HARDWARE_CHECK); break;
    case HARDWARE_CHECK:
        if (value != HARDWARE) { fail(FAIL_TARGET); break; }
        advance(TARGET_CHECK);
        break;
    case TARGET_CHECK: advance(CAPACITY_CHECK); break;
    case CAPACITY_CHECK:
        if (value != CAPACITY) { fail(FAIL_TARGET); break; }
        advance(BEGIN_FLASH);
        break;
    case BEGIN_FLASH: advance(OPEN_BLOCK); break;
    case OPEN_BLOCK:
        block_crc = ~crc32(UINT32_MAX, image + state.written, STEER_UPDATE_CHUNK_SIZE);
        data_index = 0;
        advance(SEND_DATA);
        break;
    case SEND_DATA:
        data_index++;
        advance(data_index == DATA_FRAMES ? COMMIT_BLOCK : SEND_DATA);
        break;
    case COMMIT_BLOCK: {
        uint32_t expected = state.written + STEER_UPDATE_CHUNK_SIZE;
        if (expected > state.size) { expected = state.size; }
        if (value != expected) { fail(FAIL_VERIFY); break; }
        state.written = value;
        advance(value == state.size ? FINISH_FLASH : OPEN_BLOCK);
        break;
    }
    case FINISH_FLASH: advance(VERIFY_SIZE); break;
    case VERIFY_SIZE:
        if (value != state.size) { fail(FAIL_VERIFY); break; }
        advance(VERIFY_CRC);
        break;
    case VERIFY_CRC:
        if (value != state.image_crc) { fail(FAIL_VERIFY); break; }
        advance(REBOOT_APP);
        break;
    case REBOOT_APP: advance(FIND_NEW_APP); break;
    case VERIFY_NEW_APP:
        if (value != state.image_crc) { fail(FAIL_VERIFY); break; }
        target_changed = false;
        state.state = STEER_UPDATE_DONE;
        awaiting = false;
        break;
    default: break;
    }
}

bool app_steer_update_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (state.state != STEER_UPDATE_RUNNING || bus != (board_can_bus_t)state.board ||
        frame->length != 8U) { return false; }
    update_step_t step = (update_step_t)state.step;
    uint32_t value;
    if (step == FIND_APP || step == FIND_NEW_APP) {
        if (frame->id != 0x5D1U + offset) { return false; }
        if (awaiting && atlas_decode_rpc(frame->data, RPC_BOARD, RPC_TAG, sequence, &value)) {
            if (value != state.board) { fail(FAIL_TARGET); }
            else { advance(step == FIND_APP ? OLD_CRC : VERIFY_NEW_APP); }
        }
        return true;
    }
    if (frame->id != 0x602U + offset) { return false; }
    if (awaiting && frame->data[0] == opcode(step) &&
        (step != SEND_DATA || (frame->data[2] == data_index && frame->data[3] == 0U))) {
        if (frame->data[1] != 0U) { fail((uint8_t)(16U + frame->data[1])); }
        else { accept_update_reply(step, read_le(frame->data + 4)); }
    }
    return true;
}

static bool send_stop_frames(void)
{
    const uint8_t zeros[8] = {0};
    for (unsigned int bus = 0; bus < BOARD_CAN_COUNT; ++bus) {
        uint8_t zero = (uint8_t)(1U << (bus * 2U)), coast = (uint8_t)(zero << 1);
        if (!(output_stopped & zero) && board_can_send_immediate((board_can_bus_t)bus, 0x200, zeros, 8)) {
            output_stopped |= zero;
        }
        if (!(output_stopped & coast) && board_can_send_immediate((board_can_bus_t)bus, (uint16_t)(0x350U + 2U * bus), zeros, 2)) {
            output_stopped |= coast;
        }
    }
    return output_stopped == 0x0FU;
}

static bool send_update_request(update_step_t step)
{
    uint8_t data[8] = {0};
    board_can_bus_t bus = (board_can_bus_t)state.board;
    if (step == FIND_APP || step == FIND_NEW_APP) {
        atlas_encode_rpc(data, RPC_BOARD, RPC_TAG, ++sequence, BOARD_ID);
        return board_can_send_immediate(bus, (uint16_t)(0x5D0U + offset), data, 8);
    }
    if (step == SEND_DATA) {
        memset(data + 2, 0xFF, 6);
        data[0] = data_index;
        uint32_t at = (uint32_t)data_index * 6U;
        uint32_t count = STEER_UPDATE_CHUNK_SIZE - at;
        if (count > 6U) { count = 6U; }
        memcpy(data + 2, image + state.written + at, count);
        return board_can_send_immediate(bus, (uint16_t)(0x601U + offset), data, 8);
    }
    const uint32_t argument = step == TARGET_CHECK ? FORMAT : step == BEGIN_FLASH ? state.size :
        (step == OPEN_BLOCK || step == COMMIT_BLOCK) ? state.written : 0U;
    const uint32_t value = step == TARGET_CHECK ? HARDWARE : step == BEGIN_FLASH ? state.image_crc :
        step == COMMIT_BLOCK ? block_crc : 0U;
    data[0] = opcode(step);
    for (unsigned int i = 0; i < 3U; ++i) { data[1U + i] = (uint8_t)(argument >> (8U * i)); }
    for (unsigned int i = 0; i < 4U; ++i) { data[4U + i] = (uint8_t)(value >> (8U * i)); }
    return board_can_send_immediate(bus, (uint16_t)(0x600U + offset), data, 8);
}

void app_steer_update_process(uint32_t now)
{
    if (state.state == STEER_UPDATE_STAGING && now - touched_at >= STAGING_TIMEOUT) {
        fail(FAIL_TIMEOUT);
    }
    if (state.state != STEER_UPDATE_RUNNING) { return; }
    update_step_t step = (update_step_t)state.step;
    if (step == STOP_OUTPUTS) {
        if (now - touched_at >= 1000U) { fail(FAIL_TIMEOUT); }
        else if (send_stop_frames()) { advance(target_changed ? ENTER_BOOT : FIND_APP); }
        return;
    }
    if (step == FIND_NEW_APP && attempts == 0U && now - request_at < BOOT_WAIT) { return; }
    if (awaiting) {
        uint32_t timeout = step == BEGIN_FLASH ? 3000U :
            (step == ENTER_BOOT || step == FINISH_FLASH) ? 800U : REQUEST_TIMEOUT;
        if (now - request_at < timeout) { return; }
        if (step == REBOOT_APP) { advance(FIND_NEW_APP); return; }
        if (attempts >= MAX_ATTEMPTS || step == BEGIN_FLASH) { fail(FAIL_TIMEOUT); return; }
        if (step == FIND_APP) { offset ^= 16U; }
        awaiting = false;
    }
    if (now - touched_at >= 15000U) { fail(FAIL_TIMEOUT); return; }
    if (send_update_request(step)) {
        if (step == ENTER_BOOT) { target_changed = true; }
        request_at = touched_at = now;
        awaiting = true;
        attempts++;
    }
}
