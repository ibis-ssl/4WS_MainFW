# プロジェクト概要

## 用途と責務

RoboCup SSL用ロボットの4輪ステアリング機体に搭載するメイン制御ボードです。2026-09-13に提示された要求として、次を定義します。

- ochin-CM4v2にマウントされたCM4とSPIで通信する。
- 機体内部の高速なフィードバック制御を担う。制御対象、CM4やアクチュエーター側基板との制御分担は今後決定する。周期はユーザー指定の500 Hzとする。
- 通常動作とボタン操作による出力デバッグなど、単純なモード切替を行う。

| モード | 要求される動作 | 未確定事項 |
|---|---|---|
| 通常モード | CM4からのコマンドを受け付けて動作する | [共通の設計案・未決事項](../specs/proposals/control_modes.md#実装前に確定する事項)を参照 |
| モーターデバッグ | SW7のコード3で、ボタンからAtlasの初期化・角度指令・C610速度指令を操作する | [操作と実機確認](atlas_bringup.md#モーターデバッグの操作) |

モーターデバッグへの出入りと停止処理を実装済みです。通常動作のCM4通信と機体制御は未実装です。基礎ドライバと[CAN・IMU・OLED](peripherals.md)の取得・診断機能の実機検証は[検証記録](records/README.md)にまとめています。

## 通信仕様と実装状況

AtlasのMain―周辺基板間の具体的なID・payload・周期・予約は[CAN詳細案](design/atlas_can.md)と[ID一覧CSV](design/atlas_can_ids.csv)で管理します。Mainのモーターデバッグは[Classic 2軸16 bit集約](design/atlas_can_classic16.md)を使います。設定したバス・車輪割当と実機確認項目は[結合開発](atlas_bringup.md)を参照してください。

| 対象 | 参照する契約・設計 | 本FWの状態 |
|---|---|---|
| CM4からの指令 | [共通コマンド](../specs/common/control_packet.md)、[4WSのモード・SPI設計案](../specs/proposals/control_modes.md) | 未実装。モードの採用設計と実際の受信・制御対応を区別する |
| MainからCM4への帰還 | [共通128バイトfeedback](../specs/common/feedback_packet.md) | 採用設計あり。各項目の取得・格納、CRC生成、SPI送信は未実装 |
| SPIの物理条件・転送手順 | [SPI設計案](../specs/proposals/control_modes.md#4ws-mainへのspi転送仮仕様) | 未実装。生成設定のSPI2はslave、CM4_CSはGPIO出力のため、設計案との対応確認が必要 |
| CAN機器通信 | [Orion CAN契約](../specs/common/orion_can.md)・[Atlas CAN](design/atlas_can.md) | Classic CAN転送、Atlas/C610のデバッグ制御、CAN2電源API、Orion受信解析を実装。[バス割当](atlas_bringup.md#バスと車輪)の実配線・車輪位置は結合時に照合 |

共通feedbackの採用設計は、転送や制御の実装完了を示しません。現在のUART4ログ・SWD診断RAMは本repoの内部診断で、CM4向けfeedbackではありません。Orionの停止条件・UART・更新方式を4WSの確定仕様へ転用しません。文書の所有範囲は[管理境界](specs_migration.md)を参照してください。

## 旧世代ボードの参照先

旧世代のプロジェクトは`C:\Users\hiroyuki\STM32CubeIDE\workspace_1.17.0\G474_Orion_main`です。

| 参照ファイル | 参照する内容 |
|---|---|
| `README.md` | 通常動作、モーター・ドリブラー・キッカーなどのテストモードと操作説明 |
| `doc/overview.md`、`doc/board_spec.md` | 旧世代の構成、通信、周辺HWの説明 |
| `doc/hw_control.md` | 旧世代の速度制御の構成 |
| `Core/Src/ai_comm.c`、`Core/Inc/robot_packet.h` | 上位通信の処理とパケット定義 |
| `Core/Src/state_func.c`、`Core/Src/robot_control.c` | モードごとの処理と制御の構成 |

旧世代ではCM4との通信にUSART2を使用しています。4WS向けの指令・帰還の採用設計と未確定のSPI転送手順は[通信仕様と実装状況](#通信仕様と実装状況)を参照してください。2026-09-13の追加要求により、CAN、デバッグUART、ブザーPWM、ユーザーSWなど同等化できる部分は旧世代と同等仕様にする方針です。[ドライバ互換調査](driver_compatibility.md)は旧ソースの調査履歴として保持します。4WSの操作モード、CAN機器割当、制御方式、ブートローダー構成は別途定義します。共通契約はspecsを参照し、移植時は該当ソースと照合します。

## 対象

| 項目 | 現在の設定 |
|---|---|
| CubeMXプロジェクト | `4WS_MainFW.ioc` |
| CMakeターゲット／成果物名 | `4WS_MainFW` |
| MCU | STM32G474RET6、LQFP64 |
| CPU | Arm Cortex-M4F |
| システムクロック | 170 MHz |
| FW基盤 | STM32Cube HAL、RTOSなし、C11 |
| Flash配置 | `0x08000000`から512 KiB |
| RAM配置 | `0x20000000`から128 KiB |

現在は単一アプリケーション構成です。独立したブートローダー、設定保存領域、アプリケーションオフセットは設けていません。

## ディレクトリ構成

| パス | 内容 |
|---|---|
| `Core/Inc`、`Core/Src` | CubeMX生成のアプリケーション、初期化、割り込みコード |
| `Application/` | ユーザーコード。Board / Device / Protocol / Appに基礎ドライバを実装、Controlは今後追加 |
| `Drivers/` | CMSISとSTM32G4 HALドライバー |
| `cmake/stm32cubemx/` | CubeMXが管理するソース一覧とビルド設定 |
| `cmake/` | Arm GCC／STArmClang用ツールチェーン定義 |
| `Script/` | ビルド、書き込み用PowerShellスクリプト |
| `doc/` | プロジェクト文書 |
| `.vscode/` | VS Codeのタスク、デバッグ、補完設定 |
| `build/<Configuration>/` | CMakeが生成するビルド成果物 |

ルートの`CMakeLists.txt`はユーザーが保守する設定です。`cmake/stm32cubemx/CMakeLists.txt`はCubeMXが生成する設定として扱います。

## 現在の起動処理

`main()`はHALとクロックを初期化し、GPIO、ADC1、FDCAN1/2、I2C1、LPUART1、UART4、SPI1/2、TIM1/2/3、CORDICの初期化関数を呼び出します。その後、USER CODEから`app_init()`を呼び、`while (1)`で`app_process()`を繰り返します。

AppはUART4デバッグ、ADCユーザーSW、両Classic CAN、IMU取得の状態管理、OLED検出を開始し、制御IRQ開始後に起動ブザーを2 kHz・100 ms鳴らします。メイン処理で起動時の周辺基板リセット、CAN受信の振り分け、モーターデバッグ、IMU取得、OLED表示、UART入力・状態表示、ブザー時間管理を行います。通常制御用のTIM6 IRQ入口は500 Hz、UART状態表示はmainのp()で10 Hzです。[起動時の周辺基板リセット](atlas_bringup.md#起動時の周辺基板リセット) · [基礎ドライバ](drivers.md)

`Application/Board/board_gpio.c`には、名前付きデジタル入力の生レベル取得とLED端子のレベル設定を実装しています。LEDはHigh点灯の指定に従い、Appの起動シーケンスから使用します。[コード階層](architecture.md)に各層の責務、依存方向、旧世代の実装から整理する方針を定義しています。

## 今後の実装と確認

機器間の制御分担、通信条件、停止・復帰の定義は[specsの未決事項](../specs/proposals/control_modes.md#実装前に確定する事項)で管理します。MainFW側の作業と確認条件は[実装・検証計画](implementation_plan.md)を参照してください。
