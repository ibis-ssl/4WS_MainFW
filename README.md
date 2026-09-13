# 4WS_MainFW

STM32G474RET6を使用するRoboCup SSL用4輪ステアリング機体のメイン制御ボード向けファームウェアです。STM32CubeMXが生成した初期化コードとCMakeプロジェクトを土台にしています。

ochin-CM4v2にマウントされたCM4とSPIで通信し、機体内部の高速なフィードバック制御と単純なモード切替を担います。通常モードではCM4からのコマンドに従って動作し、出力デバッグモードではボタン操作でアクチュエーターを動かす方針です。これらは要求仕様であり、制御周期や通信形式、具体的な操作・出力条件は未確定です。

現時点では各周辺機能を初期化した後、空のメインループへ入ります。ADC取得、FDCAN通信、SPI通信、PWM出力などのアプリケーション処理は未実装です。

ユーザーコードは`Application/`に配置します。最初の基板GPIO APIとして入力の生レベル取得とLED端子のレベル設定を実装しました。起動処理には未接続で、基板の実機動作は未検証です。今後のドライバと制御コードの配置は[コード階層](doc/architecture.md)に従います。

CAN、デバッグUART、ブザー、ユーザーSWなどは旧世代と同等にできる部分を同等仕様にする方針です。[ドライバ互換調査](doc/driver_compatibility.md)に、旧ソースだけで実装できる範囲、現行設定との差分、新基板で確認する項目を整理しています。

## クイックスタート

Windows PowerShellでプロジェクトルートから実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Debug
```

成果物は`build/Debug/`に生成されます。Releaseビルド、クリーンビルド、書き込み方法は[開発手順](doc/development.md)を参照してください。

## 文書

| 文書 | 内容 |
|---|---|
| [概要](doc/overview.md) | ボードの役割、動作モード、旧世代の参照先、現在の実装状況 |
| [コード階層](doc/architecture.md) | 責務・依存方向、旧世代からの整理、ドライバ実装方針 |
| [ドライバ互換調査](doc/driver_compatibility.md) | CAN・UART・ブザー・SWなどの旧仕様と実装可能範囲 |
| [ハードウェア](doc/hardware.md) | 周辺HWの定義とMCU、クロック、ピン、周辺機能の現行設定 |
| [開発手順](doc/development.md) | ビルド、成果物、書き込み、CubeMX再生成 |

周辺機能の設定値は生成コードと`4WS_MainFW.ioc`から読み取った現状です。回路との整合、通信相手との仕様、実機動作は別途確認が必要です。
