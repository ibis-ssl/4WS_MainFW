/* SH1106 128x64、列オフセット2。main専用で、送信中の画面を書き換えない。 */
#ifndef OLED_H
#define OLED_H
#include <stdbool.h>
#include <stdint.h>
typedef struct { bool ready, busy; uint8_t address; uint32_t frames, errors; } oled_status_t;
bool oled_init(void);
bool oled_begin(void);
bool oled_text(uint8_t row, uint8_t column, const char *text);
bool oled_commit(void);
void oled_process(void);
void oled_get_status(oled_status_t *out);
#endif
