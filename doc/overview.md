# プロジェクト概要

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
| `Drivers/` | CMSISとSTM32G4 HALドライバー |
| `cmake/stm32cubemx/` | CubeMXが管理するソース一覧とビルド設定 |
| `cmake/` | Arm GCC／STArmClang用ツールチェーン定義 |
| `Script/` | ビルド、書き込み用PowerShellスクリプト |
| `doc/` | プロジェクト文書 |
| `.vscode/` | VS Codeのタスク、デバッグ、補完設定 |
| `build/<Configuration>/` | CMakeが生成するビルド成果物 |

ルートの`CMakeLists.txt`はユーザーが保守する設定です。`cmake/stm32cubemx/CMakeLists.txt`はCubeMXが生成する設定として扱います。

## 現在の起動処理

`main()`はHALとクロックを初期化し、GPIO、ADC1、FDCAN1/2、I2C1、LPUART1、UART4、SPI1/2、TIM1/2/3、CORDICの初期化関数を呼び出します。その後の`while (1)`には処理がありません。

周辺機能は設定レジスターの初期化までです。変換、送受信、PWM開始、FDCANフィルター／通知など、動作開始に必要なアプリケーション処理は実装されていません。

## 実装時に決める事項

- メイン制御の周期、状態遷移、異常時の出力
- FDCANのID、データ形式、送信周期、タイムアウト
- ADC4入力の取得方法、換算式、しきい値
- SPI1のIMU仕様とSPI2の接続相手、CSの極性と担当
- PWMの周波数、デューティ範囲、起動時の安全状態
- LED、DIPスイッチ、ボタンの論理と用途

これらはピン名から用途を推測せず、回路図と接続機器の仕様を確認して決定します。
