# 周辺HWとハードウェア設定

この文書はユーザーが定義した周辺HW、回路図上の接続、`4WS_MainFW.ioc`およびCubeMX生成コードの現行設定を区別して記録します。回路図で確認した接続と極性は各節に記載し、実機検証の結果は個別に記録します。

## 周辺HWの定義

2026-09-13時点のユーザー指定事項です。

| 対象 | 定義 | 未確定事項 |
|---|---|---|
| 機体 | RoboCup SSL用の4輪ステアリング機体 | 駆動・操舵用アクチュエーター、ドライバー、フィードバックセンサーの構成 |
| メイン制御ボード | 機体内の高速フィードバック制御と単純なモード切替を担当。TIM6で500 Hz | 制御演算・制御分担 |
| 上位側HW | ochin-CM4v2にマウントされたCM4 | 接続コネクター、信号配線 |
| CM4との通信 | SPIを使用し、STM32側がslave、CM4側がmaster（2026-10-09ユーザー指定） | SPIインスタンス、CS、速度、通信形式 |
| 操作入力 | ボタン操作によるアクチュエーター出力デバッグを行う。U_BTNは旧HW同等。DIPと追加SWの配線は下記の対応表を参照 | DIPと追加SWによるモード選択方法・操作仕様 |

以降のピン・周辺機能表は設定済みの内容であり、接続HWの型番や用途の確定を意味しません。IMU、モータードライバー、その他のセンサーやサブ基板の詳細は追加定義が必要です。

## ドライバ実装状況

`Application/Board/board_gpio.c/.h`にDIP_0～3、SW_90、SW_2、IMU_FSYNCの生レベル取得と、LED_0～3・LED_R/G/Bの端子レベル設定を実装しています。生成された`main.h`のピン定義を利用し、入力はHigh/Low、出力はHigh/Low指定として扱います。LED APIはAppの起動表示から使用し、ユーザー指定により全LEDをHigh点灯として扱います。SWとDIPの回路上の極性は下記に記録しますが、APIは押下や選択値への変換を行いません。IMU_FSYNCの有効極性と各入力の用途は未確定です。

Classic CAN、旧電源基板、UART4デバッグ、ブザー、U_BTN取得・判定を実装済みです。2026-10-08の接続情報に基づき、Orion CAN受信解析、SPI1のICM-20602取得、I2C1のSH1106表示を追加しました。CM4 SPIとアクチュエーター出力系は未実装です。[基礎ドライバ](drivers.md)と[CAN・IMU・OLED](peripherals.md)に実装仕様、[開発手順](development.md)に検証結果を記載しています。

CAN、デバッグUART、ブザー、ユーザーSWの調査時点の比較は[ドライバ互換調査](driver_compatibility.md)を参照してください。以降の表は生成設定とBoardの実行時設定を記録します。CANのClassic化とADCのU_BTN取得は`.ioc`・生成コードへ反映済みです。DMA・NVICはBoardが実行時に設定します。

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
| PB7 / PA15 | I2C1 SDA / SCL | I2C1、AF4・open-drain。PA15 SCLはLCD用J18-3（ユーザー提示の回路図・基板対応） |
| PB11 | LED_1 | GPIO出力、初期Low |
| PB13 / PB14 / PB15 | SPI2 SCK / MISO / MOSI | SPI2スレーブ |
| PC0 / PC1 | LPUART1 RX / TX | LPUART1 |
| PC4 | ラベルなし | GPIO出力、初期Low |
| PC5 | IMU_FSYNC | GPIO入力、内部pullなし |
| PC6 | CM4_CS | GPIO出力、初期Low |
| PC7 / PC8 / PC9 | LED_G / LED_R / LED_B | GPIO出力、初期Low |
| PC10 / PC11 | UART4 TX / RX | UART4 |
| PC12 | LED_0 | GPIO出力、初期Low |
| PC13 | SW_90（回路のSW_0） | GPIO入力、内部pull-up |
| PC14 | SW_1 | GPIO入力、内部pull-up |
| PC15 | SW_2 | GPIO入力、内部pull-up |
| PF0 / PF1 | HSE OSC_IN / OSC_OUT | 外部発振子用に割当。ただし現在のシステムクロック源はHSI |

## スイッチの対応とIO設定

2026-10-09に、`C:\Users\hiroyuki\Documents\KiCAD_Projects\mother_4steer_v3\mother_4steer_v3.kicad_sch`の確認結果を現行FWと照合しました。以下のピン番号はU2（STM32G474RET6、LQFP64）のパッケージピン番号です。回路図上の接続を示すもので、実機での押下・接点動作の確認結果ではありません。

| 部品 | 回路図上の入力 | MCUポート | ピン番号 | FWラベル | 現在のIO設定 |
|---|---|---|---|---|---|
| SW1 | L_BTN（左） | PA2 | 14 | U_BTN | ADC1_IN3、アナログ、内部pullなし |
| SW2 | T_BTN（上／前） | PA2 | 14 | U_BTN | 同上、抵抗ラダー経由 |
| SW3 | R_BTN（右） | PA2 | 14 | U_BTN | 同上、抵抗ラダー経由 |
| SW4 | B_BTN（下／後） | PA2 | 14 | U_BTN | 同上、抵抗ラダー経由 |
| SW5 | C_BTN（中央） | PA2 | 14 | U_BTN | 同上、抵抗ラダー経由 |
| SW6 | nRST | NRST／PG10 | 7 | GPIOラベルなし | リセット端子として使用、GPIO入力APIの対象外 |
| SW8 | SW_0 | PC13 | 2 | SW_90 | GPIO入力、内部pull-up |
| SW9 | SW_1 | PC14 | 3 | SW_1 | GPIO入力、内部pull-up |
| SW10 | SW_2 | PC15 | 4 | SW_2 | GPIO入力、内部pull-up |
| SW7端子1 | DIP_0（bit 0） | PB0 | 24 | DIP_0 | GPIO入力、内部pullなし |
| SW7端子2 | DIP_1（bit 1） | PB1 | 25 | DIP_1 | GPIO入力、内部pullなし |
| SW7端子4 | DIP_2（bit 2） | PB2 | 26 | DIP_2 | GPIO入力、内部pullなし |
| SW7端子8 | DIP_3（bit 3） | PB10 | 30 | DIP_3 | GPIO入力、内部pullなし |

### U_BTNとリセット

SW1～SW5は独立したGPIO入力ではなく、抵抗ラダーを介してPA2のADC入力1本に接続されています。R12の1 kΩで3.3 Vへプルアップし、押下位置ごとの電圧を読み取ります。ADC取得と方向判定は実装済みで、判定しきい値と制約は[基礎ドライバ](drivers.md#adcユーザーsw)を参照してください。複数ボタン同時押下の識別は定義していません。

SW6は押下でNRSTをGNDへ接続します。回路図上はR16の1 kΩで3.3 Vへプルアップし、C12の100 nFをGNDへ接続しています。通常のボタン入力としては取得しません。

### 追加SW

SW8～SW10は押下で各入力をGNDへ接続するため、回路上は押下時Lowです。外付けプルアップがないため、2026-10-10に`.ioc`と`MX_GPIO_Init()`をGPIO_PULLUPへ変更しました。PC13～15を内部プルアップ付き入力として設定し、IMU_FSYNCの内部pullなし設定は維持します。

PC13は回路図の`SW_0`に対してFWでは`SW_90`という名称を維持しています。PC14は`main.h`にSW_1の名前付き定義を追加しました。PC13/14/15はそれぞれ`BOARD_GPIO_INPUT_SW_90`、`BOARD_GPIO_INPUT_SW_1`、`BOARD_GPIO_INPUT_SW_2`で生レベルを取得します。操作の意味付けとデバウンスは未実装です。

### DIP入力

SW7はコード式ロータリースイッチです。共通端子CはGND、端子1／2／4／8にはそれぞれR21／R22／R23／R24の10 kΩで3.3 Vへの外付けプルアップがあります。このため、現行の内部pullなし設定でも回路上は接点開放時High、閉成時Lowです。

`BOARD_GPIO_INPUT_DIP_0`～`BOARD_GPIO_INPUT_DIP_3`で生レベルを取得できます。接点閉成を1とする値へ変換する場合は各ビットのHigh/Lowを反転する必要があります。ロータリーの表示位置とコードの対応、選択値の組み立て、モードへの割当、接点切替時の安定判定は未実装・未検証です。

## ディスプレイのスイッチ表示

診断画面の下3行に次を表示します。GPIOはHigh=1・Low=0で、追加SWは非押下1・押下0、DIPは接点開放1・閉成0です。U_BTNはADC判定から同じ0押下・1非押下表記を作ります。

| 行表示 | 左からの順序 | 例 |
|---|---|---|
| `DIP 0123` | DIP_0/1/2/3（PB0/1/2/10） | `0111` |
| `SW  012` | SW8/9/10（PC13/14/15） | `111` |
| `BTN LTRBC` | SW1/2/3/4/5（左・上/前・右・下/後・中央） | `11111` |

U_BTNの複数押下は識別せず、ADC判定した方向だけ0にします。ADC未取得・無効時は`?????`。NRSTのSW6はリセット入力なので通常の状態表示対象外です。約100 msごとに画面生成を試みますが、実表示更新は低速OLED転送の完了を待つため通信負荷に依存します。

## 周辺機能の設定

| 機能 | 現在の設定 |
|---|---|
| ADC1 | IN3 U_BTN、12 bit、PCLK/4、連続、640.5 cycles、4倍平均、DMA1 Channel1循環 |
| FDCAN1 | Normal、Classic、自動再送、1 Mbit/s |
| FDCAN2 | Normal、Classic、自動再送、1 Mbit/s |
| I2C1 | 7 bit address。生成初期値はFast mode指定・timing `0x40621236`。OLED疎通確認時にBoardが内部プルアップ・timing `0xF0F1FFFF`（約20 kHz）へ変更 |
| LPUART1 | 2,000,000 baud、8-N-1、flow controlなし |
| UART4 | デバッグ、2,000,000 baud、8-N-1、TX DMA1 Channel2、RX 1 byte割り込み |
| SPI1 | マスター、full duplex、8 bit、Mode 0、MSB first、software NSS。生成初期値10.625 Mbit/s、IMU開始時にBoardが5.3125 Mbit/sへ設定 |
| SPI2 | スレーブ、full duplex、8 bit、Mode 0、MSB first、software NSS |
| TIM1 CH1 | PWM、prescaler 16、period 65535、pulse 0 |
| TIM2 CH2 | PWM、prescaler 170、period 1000、pulse 0 |
| TIM3 CH2 | PWM、prescaler 0、period 65535、pulse 0 |
| TIM6 | Boardの実行時設定。PSC=169、ARR=1999、500 Hz、IRQ優先度2 |
| CORDIC | 有効 |

タイマー欄は生成初期値です。TIM1 CH1は起動時にpulse=0でPWMを開始し、Hz指定時にPSC/ARR/CCRを計算して更新します。TIM2/3のPWMは開始しません。

## 要確認事項

### ADC

PA0～PA3はアナログ設定ですが、regular conversionはADC1_IN3（PA2、U_BTN）だけに変更しました。スキャンは無効で変換数は1です。PHOTO_0/1とBATT_Vを追加取得する際は、ADC1を所有するboard_user_adcとの統合が必要です。

2026-10-09のユーザー指定でBATT_Vの分圧は10 kΩ/1 kΩ。電池側10 kΩ・GND側1 kΩとして、電池電圧はADC端子電圧の11倍となる。12 bitでは`Vbat = raw / 4095 × Vref × 11`。Vrefの測定・校正、抵抗誤差、フィルターと保護しきい値は未確定で、BATT_Vの取得・換算処理はまだ実装していない。

### FDCAN

2026-10-08のVIO誤配線はユーザーが修正し、2026-10-09に両CANの実受信を確認した。CAN2ではソフトウェア受信キュー満杯による破棄が継続している。測定条件・受信ID・未確認範囲は[開発手順](development.md)を参照。

両FDCANはClassic CANで、170 MHz、prescaler 10、time segment 1/2が14/2、SJW=1で1 Mbit/sになります。data phase設定はClassicでは使用しません。トランシーバー、終端、配線、HSI起点の通信精度は実機で未確認です。

標準フィルターは各1個、拡張フィルターは0。Boardが標準ID全受入れと拡張・remote拒否、開始・通知を設定します。旧電源基板通信とOrion形式テレメトリーの受信解析を実装し、アクチュエーター出力系は未実装です。

### SPIとCS

CM4との通信ではSTM32側がslave、CM4側がmasterとユーザー指定されました。使用するSPIインスタンスと信号配線は未確定です。SPI1はIMU用のmasterで、SPI2は現状software NSSのslave設定です。CM4_CSはSTM32側のGPIO出力になっているため、その実際の役割、CSの駆動側と極性、フレーム境界の扱いは通信実装時に確認します。今回SPIの生成設定と`.ioc`は変更していません。

SPI_IMU_CSとCM4_CSの生成初期レベルはLowです。IMUはBoard開始時にCSをHighへ戻し、SPI転送時だけLowにします。CM4_CSの役割と初期レベルはCM4通信仕様の確定時に確認します。
