# 4WS_MainFW

STM32G474RET6を使用するRoboCup SSL用4輪ステアリング機体のメイン制御ボード向けファームウェアです。STM32CubeMXが生成した初期化コードとCMakeプロジェクトを土台にしています。

ochin-CM4v2にマウントされたCM4とSPIで通信し、機体内部の高速なフィードバック制御と単純なモード切替を担います。通常モードではCM4からのコマンドに従って動作し、出力デバッグモードではボタン操作でアクチュエーターを動かす方針です。これらは要求仕様であり、制御周期や通信形式、具体的な操作・出力条件は未確定です。

現時点では各周辺機能を初期化した後、空のメインループへ入ります。ADC取得、FDCAN通信、SPI通信、PWM出力などのアプリケーション処理は未実装です。

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
| [ハードウェア](doc/hardware.md) | 周辺HWの定義とMCU、クロック、ピン、周辺機能の現行設定 |
| [開発手順](doc/development.md) | ビルド、成果物、書き込み、CubeMX再生成 |

周辺機能の設定値は生成コードと`4WS_MainFW.ioc`から読み取った現状です。回路との整合、通信相手との仕様、実機動作は別途確認が必要です。
