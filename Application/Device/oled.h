/* SH1106 128x64、列オフセット2。main専用で、送信中の画面を書き換えない。 */
#ifndef OLED_H
#define OLED_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool ready, busy; uint8_t address; uint32_t frames, errors; } oled_status_t;
/* 起動後の成功64画面を固定保存し、SWD読取りによる停止が計測結果へ混入するのを避ける。
 * すべて経過サイクル数で、割込みを含む。processはHAL送信待機も含む。 */
#define OLED_TIMING_SAMPLE_COUNT 64U
typedef struct {
    uint32_t render_cycles, transfer_cycles, process_cycles, frame_cycles;
    uint32_t transactions, max_transfer_cycles;
} oled_timing_sample_t;
extern volatile oled_timing_sample_t oled_timing_samples[OLED_TIMING_SAMPLE_COUNT];
extern volatile uint32_t oled_timing_count;
bool oled_init(void);
bool oled_begin(void);
bool oled_text(uint8_t row, uint8_t column, const char *text);
bool oled_commit(void);
void oled_process(void);
void oled_get_status(oled_status_t *out);
#endif
