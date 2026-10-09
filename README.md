# 4WS_MainFW

STM32G474RET6を使用するRoboCup SSL用4輪ステアリング機体のメイン制御ボード向けファームウェアです。STM32CubeMXが生成した初期化コードとCMakeプロジェクトを土台にしています。

ochin-CM4v2にマウントされたCM4とSPIで通信し、機体内部の高速なフィードバック制御と単純なモード切替を担います。通常モードではCM4からのコマンドに従って動作し、出力デバッグモードではボタン操作でアクチュエーターを動かす方針です。通信形式や具体的な操作・出力条件は未確定です。

Classic CAN転送と旧電源基板通信、Orion形式のCANテレメトリー受信、ICM-20602の加速度・角速度・温度取得、SH1106（128x64）の診断表示、UART4デバッグ、Hz指定ブザー、旧HW互換のADCユーザーSWを実装しています。機体制御、CM4 SPI通信、アクチュエーター出力系は未実装です。

CANはVIO修正後の2026-10-09に両バスの実機受信とOrion形式の解析を確認しました。CAN2では受信キュー満杯による破棄が継続しており、負荷時の取りこぼし対策が残っています。OLEDはSDA=PB7・SCL=PA15（J18-3）で、内部プルアップのまま50→100→200→400 kHzへ段階的に上げ、400 kHz設定で60秒間の画面転送エラー0・約10回/秒の更新を確認しました。現在は400 kHz設定です。実際のSCL周波数・立上り時間の波形確認は未実施です。IMUは実機取得を確認済みです。

400 kHz設定・Debug FWの実測では、LCDの全画面描画は平均約0.31 ms、I2C同期送信の合計は約31.04 ms、描画開始から全画面転送完了まで約37.60 msです。計測条件と再読取りは[OLED計測](doc/peripherals.md#oled描画転送時間の計測)を参照してください。

起動時から動作中までLED_0→1→2→3→赤→緑→青の順で1個ずつ各200 ms点灯し、青の次はLED_0へ戻して繰り返します。ユーザー指定によりすべてHighで点灯します。mainで切り替えるため、センサー取得と制御IRQを待機で止めません。

追加SW（PC13～15）は内部プルアップを設定済みです。OLEDの下3行にDIP_0～3、SW_0～2、5方向ボタン（LTRBC）を1/0表示します。1は開放/非押下、0は閉成/押下、ADC無効時は`?`です。詳細は[スイッチ仕様](doc/hardware.md#ディスプレイのスイッチ表示)を参照してください。

機体制御の入口はTIM6割り込みから500 Hz（2 ms）で直接実行します。UART4の状態表示はmainの`p()`で整形し、入力操作なしで常時10 Hz出力します。制御演算・アクチュエーター出力は今後この制御入口へ追加します。

ユーザーコードは`Application/`に配置します。実装仕様とAPIは[基礎ドライバ](doc/drivers.md)、追加機器は[CAN・IMU・OLED](doc/peripherals.md)、配置方針は[コード階層](doc/architecture.md)を参照してください。電源指令は自動送信せず、起動ブザーを2 kHzで100 ms鳴らした後、出力をLowへ戻します。実機検証の結果と未確認範囲は[開発手順](doc/development.md)に記録します。

BATT_Vは10 kΩ/1 kΩ分圧（電池電圧はADC端子電圧の11倍）、CM4とのSPIではSTM32側をslaveとする仕様です。BATT_V取得とCM4通信処理は未実装です。

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
| [CAN・IMU・OLED](doc/peripherals.md) | Orion受信形式、ICM-20602取得、SH1106診断表示、SWD読取り |
| [ハードウェア](doc/hardware.md) | 周辺HWの定義、SW・DIPの回路図対応とIO設定、MCU・クロック・周辺機能の現行設定 |
| [開発手順](doc/development.md) | ビルド、成果物、書き込み、CubeMX再生成 |

周辺機能の設定値は生成コードと`4WS_MainFW.ioc`から読み取った現状です。回路との整合、通信相手との仕様、実機動作は別途確認が必要です。

VS Codeのコードブラウズは、ワークスペースでC/C++ IntelliSenseを有効にし、Debugの`compile_commands.json`とArm GCCを使用する設定です。最初に`Build: Debug`を実行してください。設定・定義ジャンプ・補完の確認方法は[VS Codeの手順](doc/development.md#vs-code)を参照してください。

C/C++解析はMicrosoft C/C++へ統一し、このワークスペースではSTM32Cube clangdとSTのIntelliSense自動設定を停止しています。その他の拡張の有効状態とデバッグ拡張の無効設定は[拡張機能の確認](doc/vscode.md)に記録しています。
