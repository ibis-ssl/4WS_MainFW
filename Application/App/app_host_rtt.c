/* RTTのデバッグ用テキストを、モーター診断とCM4相当の更新APIに振り分ける。 */
#include "App/app_host_rtt.h"
#include "App/app_motor_debug.h"
#include "App/app_steer_update.h"
#include "Board/board_debug_rtt.h"
#include "Board/board_time.h"
#include <stdio.h>
#include <string.h>

static int hex_digit(char ch)
{
    if (ch >= '0' && ch <= '9') { return ch - '0'; }
    if (ch >= 'a' && ch <= 'f') { return ch - 'a' + 10; }
    if (ch >= 'A' && ch <= 'F') { return ch - 'A' + 10; }
    return -1;
}

static bool read_number(const char **cursor, uint32_t base, uint32_t *value, char end)
{
    const char *p = *cursor;
    uint32_t result = 0;
    int digit;
    while ((digit = hex_digit(*p)) >= 0 && (uint32_t)digit < base) {
        if (result > (UINT32_MAX - (uint32_t)digit) / base) { return false; }
        result = result * base + (uint32_t)digit;
        p++;
    }
    if (p == *cursor || *p != end) { return false; }
    *cursor = end == '\0' ? p : p + 1;
    *value = result;
    return true;
}

static bool stage_line(const char *line, uint32_t now)
{
    uint32_t at;
    if (!read_number(&line, 10, &at, ' ')) { return false; }
    size_t length = strlen(line);
    if (length == 0U || length > 2U * STEER_UPDATE_CHUNK_SIZE || length % 2U != 0U) { return false; }
    uint8_t data[STEER_UPDATE_CHUNK_SIZE];
    for (size_t i = 0; i < length / 2U; ++i) {
        int high = hex_digit(line[2U * i]), low = hex_digit(line[2U * i + 1U]);
        if (high < 0 || low < 0) { return false; }
        data[i] = (uint8_t)(high * 16 + low);
    }
    return app_steer_update_stage(at, data, (uint32_t)(length / 2U), now);
}

static void update_status(void)
{
    steer_update_status_t s = app_steer_update_status();
    char line[145];
    (void)snprintf(line, sizeof(line),
        "FW state=%u board=%u step=%u error=%u staged=%lu written=%lu size=%lu old=%08lX crc=%08lX\n",
        (unsigned int)s.state, (unsigned int)s.board, (unsigned int)s.step, (unsigned int)s.error,
        (unsigned long)s.staged, (unsigned long)s.written, (unsigned long)s.size,
        (unsigned long)s.old_crc, (unsigned long)s.image_crc);
    board_debug_rtt_write(line);
}

static void dispatch(const char *line)
{
    if (strcmp(line, "temp") == 0) {
        steer_temperature_t value = app_motor_debug_temperature(board_millis());
        char text[100];
        (void)snprintf(text, sizeof(text), "TEMP seen=%X valid=%X decic=%d,%d,%d,%d\n",
                       (unsigned int)value.seen_mask, (unsigned int)value.valid_mask,
                       value.decic[0], value.decic[1], value.decic[2], value.decic[3]);
        board_debug_rtt_write(text);
        return;
    }
    if (strncmp(line, "fw_", 3) != 0) { app_motor_debug_rtt_command(line); return; }
    if (strcmp(line, "fw_status") == 0) { update_status(); return; }
    uint32_t now = board_millis();
    bool ok = false;
    if (strncmp(line, "fw_begin ", 9) == 0) {
        const char *cursor = line + 9;
        uint32_t board, size, crc;
        ok = read_number(&cursor, 10, &board, ' ') && board < BOARD_CAN_COUNT &&
            read_number(&cursor, 10, &size, ' ') && read_number(&cursor, 16, &crc, '\0') &&
            app_steer_update_begin((uint8_t)board, size, crc, now);
    } else if (strncmp(line, "fw_data ", 8) == 0) {
        ok = stage_line(line + 8, now);
    } else if (strcmp(line, "fw_commit") == 0) {
        ok = app_steer_update_commit(now);
    } else if (strcmp(line, "fw_abort") == 0) {
        ok = app_steer_update_abort();
    }
    board_debug_rtt_write(ok ? "OK fw\n" : "REJECT fw\n");
}

void app_host_rtt_process(void)
{
    static char line[2U * STEER_UPDATE_CHUNK_SIZE + 24U];
    static size_t length;
    static bool discard;
    uint8_t byte;
    for (unsigned int n = 0; n < 128U && board_debug_rtt_read(&byte); ++n) {
        if (byte == '\r') { continue; }
        if (byte == '\n') {
            if (discard) { board_debug_rtt_write("REJECT line\n"); }
            else { line[length] = '\0'; dispatch(line); }
            length = 0;
            discard = false;
        } else {
            if (length >= sizeof(line) - 1U || byte < 32U || byte > 126U) { discard = true; }
            if (!discard) { line[length++] = (char)byte; }
        }
    }
}
