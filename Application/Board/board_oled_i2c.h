/* I2C1のSH1106転送。7 bitアドレスを受け取り、main専用とする。 */
#ifndef BOARD_OLED_I2C_H
#define BOARD_OLED_I2C_H
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
/* 外部プルアップ未実装の基板向け疎通確認。内部プルアップと400 kHz設定を適用する。 */
bool board_oled_i2c_init(void);
bool board_oled_i2c_probe(uint8_t address);
bool board_oled_i2c_write(uint8_t address, bool data, const uint8_t *bytes, size_t length);
/* 直前のHAL同期送信の経過サイクル数。割込みと送信完了待機を含む。main専用。 */
uint32_t board_oled_i2c_last_transfer_cycles(void);
#endif
