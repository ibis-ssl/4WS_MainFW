# 4WS_MainFW

STM32G474RET6を使用するRoboCup SSL用4輪ステアリング機体のメイン制御ボード向けファームウェアです。STM32CubeMXが生成した初期化コードとCMakeプロジェクトを土台にしています。

ochin-CM4v2にマウントされたCM4とSPIで通信し、機体内部の高速なフィードバック制御と単純なモード切替を担います。通常モードではCM4からのコマンドに従って動作し、出力デバッグモードではボタン操作でアクチュエーターを動かす方針です。指令は共通コマンド、帰還は共通128バイトfeedbackを採用する[設計案](specs/proposals/control_modes.md)があります。SPIの転送手順と具体的な操作・出力条件は未確定で、[通信仕様と実装状況](doc/overview.md#通信仕様と実装状況)に整理しています。

Classic CAN転送と旧電源基板通信、Orion形式のCANテレメトリー受信、ICM-20602の加速度・角速度・温度取得、SH1106（128x64）の診断表示、UART4デバッグ、Hz指定ブザー、旧HW互換のADCユーザーSWを実装しています。モーターデバッグモードではMain上のボタンからAtlas操舵とC610の速度制御を行えます。通常モードの機体制御とCM4 SPI通信は未実装です。

モードスイッチSW7のコードを **3** に設定し、ボタンを一度すべて離します。中央を一度押すと全4軸の初期校正を開始します。左右を押している間は、各軸の許容範囲内で目標角度を毎秒90°ずつ変えます。上／下ではC610全4輪を正／負方向へ回します。CAN1にステア基板ID 0、C610 ID 1・2、電源基板を接続し、CAN2にステア基板ID 1、C610 ID 3・4を接続します。[操作と確認手順](doc/atlas_bringup.md#モーターデバッグの操作)

CANはVIO修正後の2026-10-09に両バスの実機受信とOrion形式の解析を確認しました。CAN2では受信キュー満杯による破棄が継続しており、負荷時の取りこぼし対策が残っています。OLEDはSDA=PB7・SCL=PA15（J18-3）で、内部プルアップのまま50→100→200→400 kHzへ段階的に上げ、400 kHz設定で60秒間の画面転送エラー0・約10回/秒の更新を確認しました。現在は400 kHz設定です。実際のSCL周波数・立上り時間の波形確認は未実施です。IMUは実機取得を確認済みです。

400 kHz設定・Debug FWの実測では、LCDの全画面描画は平均約0.31 ms、I2C同期送信の合計は約31.04 ms、描画開始から全画面転送完了まで約37.60 msです。計測条件と再読取りは[OLED計測](doc/peripherals.md#oled描画転送時間の計測)を参照してください。

起動時から動作中までLED_0→1→2→3→赤→緑→青の順で1個ずつ各200 ms点灯し、青の次はLED_0へ戻して繰り返します。ユーザー指定によりすべてHighで点灯します。mainで切り替えるため、センサー取得と制御IRQを待機で止めません。

追加SW（PC13～15）は内部プルアップを設定済みです。5方向ボタンのビット割当はbit 0から中央・下・右・上・左です。OLEDの下3行はDIP_0～3、SW_0～2、5方向ボタン（CDRUL）を1/0表示します。1は開放/非押下、0は閉成/押下、ADC無効時は`?`です。[スイッチ仕様](doc/hardware.md#ディスプレイのスイッチ表示)

通常モードの機体制御用入口はTIM6割り込みから500 Hz（2 ms）で呼びます。モーターデバッグの周期処理とUART4の10 Hz状態表示はメインループで実行します。

ユーザーコードは`Application/`に配置します。実装仕様とAPIは[基礎ドライバ](doc/drivers.md)、追加機器は[CAN・IMU・OLED](doc/peripherals.md)、配置方針は[コード階層](doc/architecture.md)を参照してください。Main起動時は電源基板とステア2基板へ再起動を要求し、C610へ0 Aを送ります。起動ブザーは2 kHzで100 ms鳴らします。[起動時の周辺基板リセット](doc/atlas_bringup.md#起動時の周辺基板リセット) · [検証記録](doc/records/README.md)

BATT_Vは10 kΩ/1 kΩ分圧（電池電圧はADC端子電圧の11倍）です。BATT_V取得とCM4通信処理は未実装で、機器間の通信条件と未決事項は[specsの設計案](specs/proposals/control_modes.md)で管理します。

CAN、デバッグUART、ブザー、ユーザーSWなどは旧世代と同等にできる部分を同等仕様にする方針です。[ドライバ互換調査](doc/driver_compatibility.md)に、旧ソースだけで実装できる範囲、現行設定との差分、新基板で確認する項目を整理しています。

## クイックスタート

Windows PowerShellでプロジェクトルートから実行します。

```powershell
git submodule update --init --recursive
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Debug
```

成果物は`build/Debug/`に生成されます。Releaseビルド、クリーンビルド、書き込み方法は[開発手順](doc/development.md)を参照してください。

## 文書

ステア温度の読取りとステアFW更新はPCからMainのRTTで操作できます。更新のデータ・状態管理は将来のCM4接続からも呼び出せるAPIに分離しています。 [温度とステアFW更新](doc/steer_fw_update.md)

[文書案内](doc/README.md)から現行仕様と作業手順を参照できます。現在の未確認事項は[実装・検証計画](doc/implementation_plan.md)にまとめています。

複数レイヤー・リポジトリにまたがる通信契約は、submoduleの[共通仕様](specs/README.md)を参照します。未実装の4WS通信・制御案は[設計案](specs/proposals/README.md)、このFWの対応範囲・差異・内部実装・検証結果は`doc/`で管理します。[管理境界](doc/specs_migration.md)と[仕様の取得・更新手順](doc/development.md#共通仕様リポジトリ)を参照してください。

上位・安全・互換性の契約はspecsを参照します。AtlasのCAN ID・payload・周期・バス割当は[本repoのCAN詳細案](doc/design/atlas_can.md)と[ID一覧](doc/design/atlas_can_ids.csv)を正本とし、ステアFW・specsから参照します。ステア基板とPC側で2基板・C610混在を確認し、Main側にはモーターデバッグを実装しました。Mainとの実機結合は未検証です。[Atlas／C610結合開発](doc/atlas_bringup.md)を参照してください。

| 文書 | 内容 |
|---|---|
| [Atlas／C610結合開発](doc/atlas_bringup.md) | 2バスの割当、ステア側の確認結果、Main実装の着手順、CANキャプチャ診断 |
| [Atlas CAN帯域・FD比較](doc/design/atlas_can_bandwidth.md) | 2バス構成の負荷と計算条件。FD案は非採用の比較履歴 |
| [状態・基板制御・校正通知案](doc/design/atlas_steering_control.md) | coast停止、通信復帰、HW故障ラッチ、SPI操舵角、要求状態と1 Hz通知の実装済み形式 |
| [Classic 2軸16 bit集約](doc/design/atlas_can_classic16.md) | 目標＋電流/Duty上限を2軸8 byteへ同送。Mainのデバッグでは角度指令を使用 |
| [共通仕様との管理境界](doc/specs_migration.md) | specsとFW側の管理範囲、共通コマンドと未実装範囲 |
| [概要](doc/overview.md) | ボードの役割、動作モード、旧世代の参照先、現在の実装状況 |
| [コード階層](doc/architecture.md) | 責務・依存方向、旧世代からの整理、ドライバ実装方針 |
| [ドライバ互換調査（履歴）](doc/driver_compatibility.md) | 2026-09-13時点の旧ソース調査。現在の仕様は基礎ドライバ・CAN・IMU・OLEDを参照 |
| [基礎ドライバ](doc/drivers.md) | 現在の実装仕様、API、DMA・IRQの管理、ソフト検証、[デバッグUARTのOrion比較](doc/design/debug_uart_orion.md) |
| [CAN・IMU・OLED](doc/peripherals.md) | Orion受信形式、ICM-20602取得、SH1106診断表示、SWD読取り |
| [ハードウェア](doc/hardware.md) | 周辺HWの定義、SW・DIPの回路図対応とIO設定、MCU・クロック・周辺機能の現行設定 |
| [開発手順](doc/development.md) | ビルド、成果物、書き込み、CubeMX再生成 |
| [VS Codeの確認記録](doc/vscode.md) | コード解析設定、拡張機能の監査結果と未確認範囲 |

周辺機能の設定値は生成コードと`4WS_MainFW.ioc`から読み取った現状です。回路との整合、通信相手との仕様、実機動作は別途確認が必要です。

VS Codeのコードブラウズは、ワークスペースでC/C++ IntelliSenseを有効にし、Debugの`compile_commands.json`とArm GCCを使用する設定です。最初に`Build: Debug`を実行してください。設定・定義ジャンプ・補完の確認方法は[VS Codeの手順](doc/development.md#vs-code)を参照してください。

C/C++解析はMicrosoft C/C++へ統一し、このワークスペースではSTM32Cube clangdとSTのIntelliSense自動設定を停止しています。その他の拡張の有効状態とデバッグ拡張の無効設定は[拡張機能の確認](doc/vscode.md)に記録しています。
