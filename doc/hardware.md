# 周辺HWとハードウェア設定

この文書はユーザーが定義した周辺HWと、`4WS_MainFW.ioc`およびCubeMX生成コードの現行設定を区別して記録します。基板回路や接続機器との整合、信号の有効レベル、実機での動作は未確認です。

## 周辺HWの定義

2026-09-13時点のユーザー指定事項です。

| 対象 | 定義 | 未確定事項 |
|---|---|---|
| 機体 | RoboCup SSL用の4輪ステアリング機体 | 駆動・操舵用アクチュエーター、ドライバー、フィードバックセンサーの構成 |
| メイン制御ボード | 機体内の高速フィードバック制御と単純なモード切替を担当 | 制御分担、制御周期 |
| 上位側HW | ochin-CM4v2にマウントされたCM4 | 接続コネクター、信号配線 |
| CM4との通信 | SPIを使用し、通常モードのコマンドをCM4から受け付ける | SPIインスタンス、master/slave、CS、速度、通信形式 |
| 操作入力 | ボタン操作によるアクチュエーター出力デバッグを行う | ボタンの回路・ピン対応、判定条件、モード選択方法 |

以降のピン・周辺機能表は設定済みの内容であり、接続HWの型番や用途の確定を意味しません。IMU、モータードライバー、その他のセンサーやサブ基板の詳細は追加定義が必要です。

## ドライバ実装状況

`Application/Board/board_gpio.c/.h`にDIP_0～3、SW_90、SW_2、IMU_FSYNCの生レベル取得と、LED_0～3・LED_R/G/Bの端子レベル設定を実装しています。生成された`main.h`のピン定義を利用し、入力はHigh/Low、出力はHigh/Low指定として扱います。有効極性や用途は確定していません。初期化後に呼び出すAPIで、現時点では起動処理から呼び出していません。

ADC・PWM・SPI・CANの運用ドライバは未実装です。基板の実機動作確認は未実施です。階層と今後の配置は[コード階層](architecture.md)を参照してください。

## MCUとクロック

STM32G474RET6（LQFP64）を使用します。16 MHz HSIをPLLへ入力し、`M=4`、`N=85`、`R=2`でSYSCLKを170 MHzに設定しています。AHB、APB1、APB2はいずれも分周なしです。電源スケールはBoost、Flash latencyは4です。

リンカースクリプトはFlashを`0x08000000`から512 KiB、RAMを`0x20000000`から128 KiBとして使用します。アプリケーションもFlash先頭に配置されます。

## ピン割当

| ピン | ラベル／機能 | 現在のモード |
|---|---|---|
| PA0 | PHOTO_0 / ADC1_IN1 | アナログ |
| PA1 | PHOTO_1 / ADC1_IN2 | アナログ |
| PA2 | U_BTN / ADC1_IN3 | アナログ |
| PA3 | BATT_V / ADC1_IN4 | アナログ |
| PA4 | SPI_IMU_CS | GPIO出力、初期Low |
| PA5 / PA6 / PA7 | SPI1 SCK / MISO / MOSI | SPI1マスター |
| PA8 | BUZZER | TIM1_CH1 PWM |
| PA9 / PA10 | LED_3 / LED_2 | GPIO出力、初期Low |
| PA11 / PA12 | FDCAN1 RX / TX | FDCAN1 |
| PA13 / PA14 | SWDIO / SWCLK | Serial Wireデバッグ |
| PB0 / PB1 / PB2 / PB10 | DIP_0 / DIP_1 / DIP_2 / DIP_3 | GPIO入力、内部pullなし |
| PB3 | ESC_PWM | TIM2_CH2 PWM |
| PB5 | INTERRUPTER_OUT | TIM3_CH2 PWM |
| PB6 / PB12 | FDCAN2 TX / RX | FDCAN2 |
| PB7 / PB8 | I2C1 SDA / SCL | I2C1、open-drain |
| PB11 | LED_1 | GPIO出力、初期Low |
| PB13 / PB14 / PB15 | SPI2 SCK / MISO / MOSI | SPI2スレーブ |
| PC0 / PC1 | LPUART1 RX / TX | LPUART1 |
| PC4 | ラベルなし | GPIO出力、初期Low |
| PC5 | IMU_FSYNC | GPIO入力、内部pullなし |
| PC6 | CM4_CS | GPIO出力、初期Low |
| PC7 / PC8 / PC9 | LED_G / LED_R / LED_B | GPIO出力、初期Low |
| PC10 / PC11 | UART4 TX / RX | UART4 |
| PC12 | LED_0 | GPIO出力、初期Low |
| PC13 | SW_90 | GPIO入力、内部pullなし |
| PC14 | ラベルなし | GPIO入力、内部pullなし |
| PC15 | SW_2 | GPIO入力、内部pullなし |
| PF0 / PF1 | HSE OSC_IN / OSC_OUT | 外部発振子用に割当。ただし現在のシステムクロック源はHSI |

## 周辺機能

| 機能 | 現在の設定 |
|---|---|
| ADC1 | 12 bit、同期クロックPCLK/4、ソフトウェアトリガー、単発、DMAなし |
| FDCAN1 | Normal、FD+BRS、自動再送、nominal 1 Mbit/s設定 |
| FDCAN2 | Normal、FD without BRS、自動再送、nominal 1 Mbit/s設定 |
| I2C1 | 7 bit address、Fast mode指定、timing `0x40621236` |
| LPUART1 | 2,000,000 baud、8-N-1、flow controlなし |
| UART4 | 2,000,000 baud、8-N-1、flow controlなし |
| SPI1 | マスター、full duplex、8 bit、Mode 0、MSB first、software NSS、10.625 Mbit/s計算値 |
| SPI2 | スレーブ、full duplex、8 bit、Mode 0、MSB first、software NSS |
| TIM1 CH1 | PWM、prescaler 16、period 65535、pulse 0 |
| TIM2 CH2 | PWM、prescaler 170、period 1000、pulse 0 |
| TIM3 CH2 | PWM、prescaler 0、period 65535、pulse 0 |
| CORDIC | 有効 |

タイマーのprescalerとperiodはレジスター設定値です。PWMは初期化されますが、`HAL_TIM_PWM_Start()`は呼ばれていません。

## 要確認事項

### ADC

PA0～PA3はADC1のアナログピンとして構成されていますが、regular conversionに登録されているのはADC1_IN1（PA0、PHOTO_0）だけです。スキャンは無効で変換数は1です。PHOTO_1、U_BTN、BATT_Vも取得する場合は、チャネル切替またはスキャン変換を実装する必要があります。

### FDCAN

両FDCANのnominal設定は、170 MHz、prescaler 17、time segment 1/2が4/5で1 Mbit/sになります。FDCAN1のdata phaseはprescaler 1、segment 1/2が1/1で、設定値からは約56.7 Mbit/sになります。トランシーバー、配線、相手機器が対応するかを確認し、必要なdata bitrateへ設定してください。

標準／拡張フィルター数はいずれも0で、開始処理と受信通知も未実装です。FDCAN2はFD without BRSですが、実際に使用するフレーム形式は通信仕様と合わせて確認が必要です。

### SPIとCS

CM4との通信方式はSPIと定義されていますが、使用するSPIインスタンスと信号配線は未確定です。SPI1はsoftware NSSで、SPI_IMU_CSはGPIO出力です。SPI2もsoftware NSSのスレーブですが、CM4_CSはSTM32側のGPIO出力になっています。CM4とのmaster/slave関係、CM4_CSの実際の役割、CSの駆動側と極性を回路図で確認してください。今回の周辺HW定義では、この設定は変更していません。

SPI_IMU_CSとCM4_CSは起動時Lowです。接続機器のCSがactive-lowの場合、初期化中から選択状態になるため初期レベルの見直しが必要です。
