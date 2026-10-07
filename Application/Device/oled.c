/* ページの列位置指定と8 byte転送を別のmain呼出しに分け、IMU取得を挟む。 */
#include "Device/oled.h"
#include "Board/board_oled_i2c.h"
#include <stddef.h>
#include <string.h>
static uint8_t pixels[1024];
static oled_status_t status;
static bool editing;
static uint8_t page, part;
#define OLED_CHUNK_SIZE 8U
/* 診断用の5x7字形。英大文字・数字と最小限の記号だけを表示する。 */
static const uint8_t digits[10][5] = {
 {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},
 {0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},{0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E}};
static const uint8_t letters[26][5] = {
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
 {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},
 {0x7F,8,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},
 {0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,9,9,9,6},
 {0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
 {1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
 {0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}};
static void glyph(char ch, uint8_t *out)
{
    memset(out, 0, 6);
    if (ch >= '0' && ch <= '9') { memcpy(out, digits[ch - '0'], 5); }
    else if (ch >= 'A' && ch <= 'Z') { memcpy(out, letters[ch - 'A'], 5); }
    else if (ch == '-') { out[1]=out[2]=out[3]=8; }
    else if (ch == '.') { out[2]=0x60; }
    else if (ch == ':') { out[2]=0x36; }
    else if (ch == '/') { out[0]=0x40; out[1]=0x20; out[2]=0x10; out[3]=8; out[4]=4; }
    else if (ch != ' ') { out[0]=2; out[1]=1; out[2]=0x51; out[3]=9; out[4]=6; }
}
bool oled_init(void)
{
    status.ready = false; status.busy = false; editing = false;
    status.address = 0;
    if (!board_oled_i2c_init()) { status.errors++; return false; }
    for (uint8_t addr = 0x3C; addr <= 0x3D; ++addr) {
        if (board_oled_i2c_probe(addr)) { status.address = addr; break; }
    }
    if (!status.address) { status.errors++; return false; }
    const uint8_t commands[] = {0xAE,0xD5,0x80,0xA8,0x3F,0xD3,0x00,0x40,
        0xAD,0x8B,0xA1,0xC8,0xDA,0x12,0x81,0x80,0xD9,0x22,0xDB,0x35,0xA4,0xA6};
    if (!board_oled_i2c_write(status.address, false, commands, sizeof(commands))) { status.errors++; return false; }
    /* 最初の全画面転送完了まで表示をOFFにし、電源投入時のRAM内容を表示しない。 */
    status.ready = true; return true;
}
bool oled_begin(void)
{
    if (!status.ready || status.busy || editing) { return false; }
    memset(pixels, 0, sizeof(pixels)); editing = true; return true;
}
bool oled_text(uint8_t row, uint8_t column, const char *text)
{
    if (!editing || row >= 8U || column >= 21U || text == NULL) { return false; }
    while (*text && column < 21U) { glyph(*text++, pixels + row * 128U + column++ * 6U); }
    return true;
}
bool oled_commit(void)
{
    if (!editing) { return false; }
    editing = false; status.busy = true; page = 0; part = 0; return true;
}
void oled_process(void)
{
    if (!status.ready || !status.busy) { return; }
    bool ok;
    if (page == 8U) {
        const uint8_t on = 0xAF;
        ok = board_oled_i2c_write(status.address, false, &on, 1);
        if (ok) { status.busy = false; status.frames++; }
    } else if (part == 0U) {
        const uint8_t commands[] = {(uint8_t)(0xB0U + page),0x02,0x10};
        ok = board_oled_i2c_write(status.address, false, commands, sizeof(commands));
        if (ok) { part = 1; }
    } else {
        ok = board_oled_i2c_write(status.address, true,
            pixels + page * 128U + (part - 1U) * OLED_CHUNK_SIZE, OLED_CHUNK_SIZE);
        if (ok && ++part == 128U / OLED_CHUNK_SIZE + 1U) { part = 0; page++; }
    }
    if (!ok) { status.errors++; status.ready = false; status.busy = false; }
}
void oled_get_status(oled_status_t *out) { if (out != NULL) { *out = status; } }
