/* 電源ノード100のOFWブートを駆動する。受信した停止状態を確認してから更新へ移行する。 */
#include "App/app_power_update.h"
#include "App/app_board_reset.h"
#include "App/app_motor_debug.h"
#include "App/app_power_features.h"
#include "App/app_steer_update.h"
#include "Device/power_board.h"
#include "Device/user_switch.h"
#include <string.h>

enum { NODE = 100, CONTROL_ID = 0x610, REPLY_ID = 0x6b4, VERSION_ID = 0x611,
       VERSION_REPLY = 0x6c4, DATA_ID = 0x480, APP_BASE = 0x08004000, APP_SIZE = 0x1b800,
       HELLO = 1, ERASE = 2, SET_CRC = 3, BLOCK_START = 4, BLOCK_END = 5, FINALIZE = 6, REBOOT = 7 };
typedef enum { WAIT_OFF, IDENTIFY, ENTER, WAIT_BOOT, BEGIN_FLASH, IMAGE_CRC, OPEN_BLOCK, SEND_DATA,
               CLOSE_BLOCK, CHECK_OFFSET, FINAL, RESTART, CHECK_APP } step_t;
static power_update_status_t status;
static uint8_t block[POWER_UPDATE_BLOCK_SIZE];
static uint32_t last_host, sent_ms, rolling_crc, block_crc, version_build, version_crc;
static uint8_t session, token, retries, block_retries, frame_index, safe_off_samples;
static bool awaiting, boot_entered, version_seen, verified_version, application_alive;

static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint32_t crc32c(uint32_t crc, const uint8_t *data, uint32_t size)
{
    while (size--) {
        crc ^= *data++;
        for (unsigned int i = 0; i < 8U; ++i) { crc = (crc >> 1) ^ ((0U - (crc & 1U)) & 0x82f63b78U); }
    }
    return crc;
}

static uint32_t block_size(void)
{
    uint32_t size = status.size - status.written;
    return size < POWER_UPDATE_BLOCK_SIZE ? size : POWER_UPDATE_BLOCK_SIZE;
}

static void advance(step_t step)
{
    status.step = (uint8_t)step;
    awaiting = false;
    retries = 0;
}

static void fail(uint8_t error)
{
    status.error = error;
    status.state = POWER_UP_FAILED;
    awaiting = false;
    board_can_cancel_immediate_id(POWER_BOARD_CAN_BUS, CONTROL_ID);
}

bool app_power_update_locked(void)
{
    return boot_entered || status.state == POWER_UP_STAGING || status.state == POWER_UP_RUNNING;
}

bool app_power_update_quiet(void)
{
    return boot_entered && ((status.state == POWER_UP_RUNNING && status.step >= WAIT_BOOT) ||
                            (status.state == POWER_UP_STAGING && status.written != 0U));
}

power_update_status_t app_power_update_status(void) { return status; }

bool app_power_version_request(void)
{
    if (status.state == POWER_UP_RUNNING || status.state == POWER_UP_STAGING) { return false; }
    version_seen = false;
    const uint8_t data[8] = {NODE};
    return board_can_send_immediate(POWER_BOARD_CAN_BUS, VERSION_ID, data, 8);
}

bool app_power_version_read(uint32_t *build, uint32_t *crc)
{
    *build = version_build; *crc = version_crc;
    return version_seen;
}

bool app_power_update_begin(uint32_t size, uint32_t crc, uint32_t build, uint32_t now)
{
    if (size < 0x408U || size > APP_SIZE || build == 0U || app_steer_update_locked() ||
        app_board_reset_status() == BOARD_RESET_PENDING || !app_power_features_bench_idle() ||
        !app_motor_debug_idle() || status.state == POWER_UP_STAGING || status.state == POWER_UP_RUNNING) { return false; }
    status = (power_update_status_t){.state = POWER_UP_STAGING, .size = size, .crc = crc, .build = build};
    last_host = now;
    rolling_crc = UINT32_MAX;
    session++;
    return true;
}

bool app_power_update_stage(uint32_t offset, const uint8_t *data, uint32_t size, uint32_t now)
{
    if (status.state != POWER_UP_STAGING || size == 0U || data == NULL || offset < status.written) { return false; }
    offset -= status.written;
    if (offset > status.staged || size > block_size() - offset) { return false; }
    if (offset < status.staged) { return size <= status.staged - offset && memcmp(block + offset, data, size) == 0; }
    memcpy(block + offset, data, size);
    status.staged += size;
    last_host = now;
    return true;
}

bool app_power_update_commit(uint32_t now)
{
    if (status.state != POWER_UP_STAGING || status.staged != block_size()) { return false; }
    if (status.written == POWER_UPDATE_BLOCK_SIZE &&
        (get32(block + (0x400U - POWER_UPDATE_BLOCK_SIZE)) != 0x52565746U ||
         get32(block + (0x404U - POWER_UPDATE_BLOCK_SIZE)) != status.build)) {
        fail(2);
        return false;
    }
    block_crc = ~crc32c(UINT32_MAX, block, status.staged);
    block_retries = 0;
    last_host = now;
    status.state = POWER_UP_RUNNING;
    token = (uint8_t)(session + status.written / POWER_UPDATE_BLOCK_SIZE);
    if (status.written != 0U) { advance(OPEN_BLOCK); return true; }
    uint32_t sp = get32(block), pc = get32(block + 4);
    if (sp <= 0x20000000U || sp > 0x20008000U || (sp & 7U) || !(pc & 1U) ||
        (pc & ~1U) < APP_BASE || (pc & ~1U) >= APP_BASE + status.size) {
        fail(2); return false;
    }
    safe_off_samples = 0;
    /* 復旧時もOFFフレームでブート側の状態応答を取り直す。 */
    advance(WAIT_OFF);
    return true;
}

bool app_power_update_abort(void)
{
    if (status.state != POWER_UP_STAGING) { return false; }
    status.state = boot_entered ? POWER_UP_FAILED : POWER_UP_IDLE;
    return true;
}

static uint8_t opcode(step_t step)
{
    static const uint8_t commands[] = {
        [WAIT_BOOT] = HELLO, [BEGIN_FLASH] = ERASE, [IMAGE_CRC] = SET_CRC,
        [OPEN_BLOCK] = BLOCK_START, [CLOSE_BLOCK] = BLOCK_END, [CHECK_OFFSET] = HELLO,
        [FINAL] = FINALIZE, [RESTART] = REBOOT,
    };
    return commands[step];
}

static void retry_block(void)
{
    if (++block_retries >= 4U) { fail(4); }
    else { advance(OPEN_BLOCK); }
}

static void commit_block(uint32_t received)
{
    if (received != status.written + status.staged) { fail(4); return; }
    rolling_crc = crc32c(rolling_crc, block, status.staged);
    status.written = received;
    status.staged = 0;
    last_host = sent_ms;
    if (received < status.size) { status.state = POWER_UP_STAGING; return; }
    if (~rolling_crc != status.crc) { fail(2); return; }
    advance(FINAL);
}

bool app_power_update_accept(board_can_bus_t bus, const board_can_frame_t *frame)
{
    if (bus != POWER_BOARD_CAN_BUS) { return false; }
    step_t step = (step_t)status.step;
    if (status.state == POWER_UP_RUNNING && step == CHECK_APP && frame->id == 0x215U) {
        power_packet_value_t battery;
        application_alive |= power_packet_decode(frame->id, frame->data, frame->length, &battery);
    }
    if (frame->length != 8U) { return false; }
    if (status.state == POWER_UP_RUNNING && step == WAIT_OFF && frame->id == 0x244U) {
        if ((frame->data[0] & 0x1fU) != 0U) { safe_off_samples = 0; }
        else if (safe_off_samples < 2U) { safe_off_samples++; }
    }
    if (frame->id == VERSION_REPLY) {
        version_build = get32(frame->data); version_crc = get32(frame->data + 4); version_seen = true;
        if (status.state == POWER_UP_RUNNING && awaiting) {
            if (step == IDENTIFY) { advance(ENTER); }
            else if (step == CHECK_APP && version_build == status.build && version_crc == status.crc) {
                verified_version = true;
            }
        }
        return true;
    }
    if (frame->id != REPLY_ID) { return false; }
    if (status.state != POWER_UP_RUNNING || !awaiting || frame->data[2] != NODE || frame->data[3] != token ||
        frame->data[0] != (uint8_t)(opcode(step) | 0x80U)) { return true; }
    uint32_t value = get32(frame->data + 4);
    if (frame->data[1] != 0U) {
        if (step == CLOSE_BLOCK && (frame->data[1] == 3U || frame->data[1] == 4U)) { retry_block(); }
        else { fail((uint8_t)(16U + frame->data[1])); }
        return true;
    }
    switch (step) {
    case WAIT_BOOT: advance(BEGIN_FLASH); break;
    case BEGIN_FLASH: advance(IMAGE_CRC); break;
    case IMAGE_CRC: advance(OPEN_BLOCK); break;
    case OPEN_BLOCK: frame_index = 0; advance(SEND_DATA); break;
    case CLOSE_BLOCK: commit_block(value); break;
    case CHECK_OFFSET:
        if (value == status.written) { retry_block(); }
        else { commit_block(value); }
        break;
    case FINAL: advance(RESTART); break;
    case RESTART:
        advance(CHECK_APP); sent_ms = frame->received_ms;
        verified_version = application_alive = false;
        break;
    default: break;
    }
    return true;
}

static bool send_step(step_t step)
{
    if (step == IDENTIFY || step == CHECK_APP) {
        const uint8_t data[8] = {NODE};
        return board_can_send_immediate(POWER_BOARD_CAN_BUS, VERSION_ID, data, 8);
    }
    if (step == ENTER) {
        const uint8_t data[8] = {'O','F','W','U','P',NODE,0,session};
        return board_can_send_immediate(POWER_BOARD_CAN_BUS, 0x600U, data, 8);
    }
    uint32_t argument = step == BEGIN_FLASH ? status.size : step == IMAGE_CRC ? status.crc :
                        step == OPEN_BLOCK ? status.written : step == CLOSE_BLOCK ? block_crc : 0U;
    uint8_t data[8] = {opcode(step), NODE, token, 0, (uint8_t)argument, (uint8_t)(argument >> 8),
                       (uint8_t)(argument >> 16), (uint8_t)(argument >> 24)};
    return board_can_send_immediate(POWER_BOARD_CAN_BUS, CONTROL_ID, data, 8);
}

void app_power_update_process(uint32_t now)
{
    if (status.state == POWER_UP_STAGING && now - last_host >= 10000U) { fail(1); }
    if (status.state != POWER_UP_RUNNING) { return; }
    step_t step = (step_t)status.step;
    if (step == WAIT_OFF) {
        power_board_status_t power;
        (void)power_board_get_status(POWER_BOARD_CAN_BUS, &power);
        if (now - last_host >= 2000U) { fail(3); }
        else if (power.flags_valid && safe_off_samples >= 2U &&
                 now - power.flags_ms < 200U && !(power.flags & 0x1fU) && now - last_host >= 40U) {
            advance(IDENTIFY);
        }
        return;
    }
    if (step == CHECK_APP && verified_version && application_alive) {
        boot_entered = false;
        status.state = POWER_UP_DONE;
        power_board_reconfigure();
        return;
    }
    if (step == SEND_DATA) {
        if (now - sent_ms < 1U) { return; }
        if (now - sent_ms >= 1000U) { fail(1); return; }
        uint8_t data[8] = {token, 255, 255, 255, 255, 255, 255, 255};
        uint32_t pos = (uint32_t)frame_index * 7U;
        uint32_t remaining = status.staged - pos;
        memcpy(data + 1, block + pos, remaining < 7U ? remaining : 7U);
        if (board_can_send_immediate(POWER_BOARD_CAN_BUS, (uint16_t)(DATA_ID + frame_index), data, 8)) {
            sent_ms = now;
            frame_index++;
            if (remaining <= 7U) { advance(CLOSE_BLOCK); }
        }
        return;
    }
    if (step == CLOSE_BLOCK && retries == 0U && now - sent_ms < 2U) { return; }
    if ((step == WAIT_BOOT || step == CHECK_APP) && retries == 0U && now - sent_ms < 300U) { return; }
    if (!awaiting && now - sent_ms >= 15000U && step != IDENTIFY) { fail(1); return; }
    if (awaiting) {
        uint32_t timeout = step == BEGIN_FLASH ? 10000U : step == FINAL ? 5000U : 400U;
        if (now - sent_ms < timeout) { return; }
        if (step == CLOSE_BLOCK) { advance(CHECK_OFFSET); return; }
        if (step == RESTART) {
            advance(CHECK_APP); sent_ms = now;
            verified_version = application_alive = false;
            return;
        }
        if (retries >= (step == CHECK_APP ? 20U : 5U) || step == BEGIN_FLASH || step == FINAL) { fail(1); return; }
        awaiting = false;
    }
    if (step == IDENTIFY && now - last_host >= 5000U) { fail(1); return; }
    if (send_step(step)) {
        sent_ms = now;
        if (step == ENTER) { boot_entered = true; advance(WAIT_BOOT); return; }
        awaiting = true;
        retries++;
    }
}
