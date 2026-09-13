# 4WS_MainFW

STM32G474RET6を使用するRoboCup SSL用4輪ステアリング機体のメイン制御ボード向けファームウェアです。STM32CubeMXが生成した初期化コードとCMakeプロジェクトを土台にしています。

ochin-CM4v2にマウントされたCM4とSPIで通信し、機体内部の高速なフィードバック制御と単純なモード切替を担います。通常モードではCM4からのコマンドに従って動作し、出力デバッグモードではボタン操作でアクチュエーターを動かす方針です。通信形式や具体的な操作・出力条件は未確定です。

Classic CAN転送と旧電源基板通信、UART4デバッグ、Hz指定ブザー、旧HW互換のADCユーザーSWを実装しています。起動後は受信・SW取得・デバッグ処理を実行します。機体制御、CM4 SPI通信、アクチュエーター出力系は未実装です。

機体制御の入口はTIM6割り込みから500 Hz（2 ms）で直接実行します。UART4の状態表示はmainの`p()`で整形し、入力操作なしで常時10 Hz出力します。制御演算・アクチュエーター出力は今後この制御入口へ追加します。

ユーザーコードは`Application/`に配置します。実装仕様とAPIは[基礎ドライバ](doc/drivers.md)、配置方針は[コード階層](doc/architecture.md)を参照してください。電源指令は自動送信せず、ブザーは停止状態で起動します。基板の実機動作は未検証です。

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
| [基礎ドライバ](doc/drivers.md) | 現在の実装仕様、API、DMA・IRQの管理、ソフト検証 |
| [ハードウェア](doc/hardware.md) | 周辺HWの定義とMCU、クロック、ピン、周辺機能の現行設定 |
| [開発手順](doc/development.md) | ビルド、成果物、書き込み、CubeMX再生成 |

周辺機能の設定値は生成コードと`4WS_MainFW.ioc`から読み取った現状です。回路との整合、通信相手との仕様、実機動作は別途確認が必要です。
