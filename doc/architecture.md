# コード階層と実装方針

## 方針

旧世代`G474_Orion_main`と同程度の規模を想定し、C11・RTOSなしの現行構成を維持する。ユーザーコードは`Application/`へ配置し、役割ごとにディレクトリを分ける。各モジュールの`.c`と`.h`は同じディレクトリに置き、公開ヘッダーは`Board/board_gpio.h`のように階層を含めてincludeする。

汎用OS層、機種切替用の仮想ドライバ、モジュールごとのライブラリ化は現時点では設けない。STM32G474専用の直接呼出しと固定長バッファを基本とし、高速制御経路で動的メモリ確保を行わない。

## 配置と責務

`Application/`は、この機体向けに実装・管理するコード全体を表す。CubeMX生成の`Core/`、ST提供の`Drivers/`と管理範囲を分ける。配下の`App/`は、その中でも起動・モード・周期処理を統括する層を表す。

| 配置 | 責務 | 今後置くモジュールの例 |
|---|---|---|
| `Core/` | CubeMX生成の初期化、割り込み入口、起動処理 | `main.c`、`gpio.c`、`spi.c` |
| `Drivers/` | ST提供のHALとCMSIS | ユーザードライバは追加しない |
| `Application/Board/` | ピン・HALハンドルの対応、GPIO/ADC/PWM、SPI/CAN/UART転送、HALコールバックの振分け | `board_gpio`、`board_adc`、`board_pwm`、`board_spi`、`board_can` |
| `Application/Device/` | 接続機器の操作、センサーレジスター、換算・校正、ボタン判定やLED有効極性 | `imu`、`user_input`、`indicator`、`motor_driver` |
| `Application/Protocol/` | CM4・サブ基板通信のフレーム形式、検証、エンコード・デコード | `cm4_packet`、`motor_packet` |
| `Application/Control/` | 4輪操舵の運動学、フィードバック制御、推定・フィルター | `steering_control`、`drive_control`、`odometry` |
| `Application/App/` | 起動、周期処理、モード遷移、指令の選択、通信断・異常時の判断、出力の統括 | `app`、`app_mode`、`app_control`、`app_comm` |

現在はBoardのGPIO・CAN・UART4・ユーザーADC・ブザー、DeviceのユーザーSW・ブザー・電源、Protocolの電源形式、Appの起動・処理を実装している。詳細は[基礎ドライバ](drivers.md)を参照する。Controlと他の機器モジュールは必要になった時点で作成する。空の初期化関数や成功を返すだけのドライバは用意しない。

## 依存方向

- `App`は`Control`、`Device`、`Protocol`、`Board`の必要なAPIを呼ぶ。CM4の受信処理など、対応する`Device`が不要なら直接`Board`の転送APIを使う。
- `Device`は`Board`と必要な`Protocol`に依存する。機器内の校正は担当するが、ロボット全体の姿勢推定・制御は`Control`へ置く。
- `Control`と`Protocol`はHAL・GPIO・アプリの状態に依存しない。数値・構造体・バイト列を引数で受け渡す。
- `Board`だけがユーザーコードから生成ヘッダーやHAL/LLを直接使用する。下位層から`App`や`Control`を直接呼ばない。
- `Core/Src/main.c`は生成初期化と、将来追加する`app_init()`／`app_process()`の呼出しに留める。追加箇所はUSER CODEブロックを使う。

全モジュール共通の`management.h`は設けない。型は所有するモジュールのヘッダーに定義し、全体状態を下位ドライバへ渡さない。共通処理は複数箇所で必要になった段階で小さな専用モジュールへ切り出す。

## 実行とデータ所有

モード判断と最終出力の許可は`App`が一元管理する。通常モードとデバッグモードは指令の生成元を切り替え、両方が独立してアクチュエーターへ書き込む構成にしない。切替時の具体的な停止・継続条件は要求確定後に実装する。

高速制御周期と低速のボタン・表示・ログ処理を分ける。具体的な周期、タイマー、制御を割り込み内で実行するかどうかは、必要な帯域と実行時間を確認して決定する。旧世代の500 Hzを既定値として転用しない。

割り込み側は転送完了、受信データ格納、時刻やイベントの記録を基本とする。HALコールバックは周辺機能ごとに`Board`の一箇所が所有し、同じコールバックを各ドライバへ重複定義しない。上位への通知はポーリング可能な状態・キューを基本とする。

割り込みとメイン処理で共有するデータは所有者と更新タイミングを明記する。`volatile`だけで複数フィールドの整合性を保証せず、短いクリティカルセクションやバッファ切替などを選ぶ。DMAでは送信完了までバッファを保持し、固定長キューには満杯時の扱いと検出手段を設ける。

周期処理で`HAL_Delay()`や無期限待ちを使わない。起動・単発診断で待機が必要なAPIには有限のタイムアウトを設ける。下位層は失敗や未完了を返し、モード変更・再起動などの判断は`App`が行う。

## 旧世代からの整理

| 旧世代の実装 | 今回の配置方針 |
|---|---|
| `main.c`の初期化、HALコールバック、周期処理、モード操作 | 生成入口は`Core`、転送・割り込み管理は`Board`、全体進行は`App` |
| `management.h`のHAL include、全体状態、通信・制御定数 | HAL依存は`Board`、型・定数は各担当モジュール |
| `actuator.c`のCAN指令とブザーPWM操作 | 機器操作は`Device`、パケットは`Protocol`、PWM操作は`Board` |
| `can_ibis.c`のFIFO、パケット解析、通常・停止時の出力判断 | 転送・FIFOは`Board`、形式は`Protocol`、出力判断は`App` |
| `icm20602_spi.c`のSPI操作、レジスター操作、角度積分 | 転送は`Board`、機器操作は`Device`、姿勢推定は`Control` |
| `ai_comm.c`、`robot_packet.h` | 通信形式は`Protocol`、通信状態と指令反映は`App` |
| `control_*.c`、`odom.c`、`state_func.c` | 計算は`Control`、モードに応じた実行選択は`App` |

旧世代と同等化できるCAN、デバッグUART、ブザー、ユーザーSWは同等仕様を採用する方針とする。[ドライバ互換調査](driver_compatibility.md)に実装可能範囲を定義する。旧CANパケットやボタン判定値は再現可能だが、新機体へのID割当・回路対応は確認する。IMU型番、CM4のSPI転送仕様、4WSの制御式は別途定義する。同等の機能を維持しつつ、容量制限、エラー処理、DMAバッファ所有など旧実装で不十分な部分は補う。

## 最初のドライバ実装

`board_gpio`は生成された`main.h`のピン定義を参照し、次を提供する。

- DIP_0～3、SW_90、SW_2、IMU_FSYNCの生のHigh/Low取得。
- LED_0～3、LED_R/G/Bの端子へのHigh/Low書込み。
- 不正なID・NULLの検出。エラー時はGPIO操作や読取り結果の書込みを行わない。

`MX_GPIO_Init()`完了後に使用する。戻り値はAPI処理の成否で、端子の電気的な正常性は示さない。点灯・押下の極性、デバウンス、DIP値からモードへの変換は含まない。`U_BTN`はアナログ入力なので本APIの対象外。CM4_CS、SPI_IMU_CS、PWM、無名ピンも対象外。

APIはビルド対象へ登録するが、起動処理からは呼び出さない。基板の動作確認は未実施であり、この追加により出力試験が自動実行されることはない。

CAN、UART4、ブザー、U_BTNの基礎ドライバは起動処理へ接続済み。GPIO API自体は起動処理から操作しない。ADC1の取得対象はU_BTNだけに変更した。SPI2スレーブに対してCM4_CSが出力である点は未解決で、CM4通信は未実装。

## ビルドと生成コード

ソース一覧とincludeパスはルート`CMakeLists.txt`へ明示登録し、自動探索は使用しない。登録対象は基礎ドライバ各ソースとincludeルート`Application/`。CubeMX管理の`cmake/stm32cubemx/CMakeLists.txt`は編集しない。

ADC/FDCANの初期化定数と`.ioc`を同期して更新した。DMA・NVIC・IRQ入口はBoardが実行時に設定し、CubeMX側には重複登録しない。所有資源と再生成への影響は[基礎ドライバ](drivers.md)に記録する。ユーザーコードは`Application/`に残り、mainとの接続はUSER CODEで保持する。
