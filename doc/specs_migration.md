# 共通仕様との管理境界

2026-10-20に、`specs`のコミット`42e4dcf9035f932648c8d05b7ee211650c081017`に合わせて整理した。管理方針の正本は[specsの更新ルール](../specs/README.md#文書の参照方向と更新ルール)を参照する。

## specsで管理する内容

- Orion・Atlasのシステム構成とCM4–Main間の通信方式：[システム構成](../specs/common/system_overview.md)。
- 共通コマンドのレイアウト、符号化、単位、フィールドの意味：[制御パケット](../specs/common/control_packet.md)。4WSへの拡張とSPI転送単位は[未実装の設計案](../specs/proposals/control_modes.md)を参照する。
- MainからCM4へ返す共通128バイトの配置・CRC・単位：[フィードバックパケット](../specs/common/feedback_packet.md)。4WSでの採用設計とSPI転送の未実装範囲は[制御・通信の設計案](../specs/proposals/control_modes.md)を参照する。
- OrionのMainとCANノード間の通信契約：[Orion CAN](../specs/common/orion_can.md)。4WSのノード・車輪・操舵軸への割当は本FWで未確定。
- Orionの外部連携、制御・保護動作、更新契約：[対外仕様の入口](../specs/README.md#orionの対外仕様)。

現行の通信契約は`common/`、未実装案は[proposals/](../specs/proposals/README.md)で管理する。[Atlasの記載範囲](../specs/common/system_overview.md#atlasの記載範囲)は本FWの実装への適合を保証しない。共通feedbackを含む通信契約への適合確認は今後の実装時に行い、Orionの停止条件・ブートローダーを本FWの実装済み仕様として扱わない。

## 本リポジトリで管理する内容

| 内容 | 文書 |
|---|---|
| 実装済み・未実装の範囲、起動処理 | [概要](overview.md) |
| Board / Device / Protocol / Control / Appの責務と依存、IRQ・DMAの所有 | [コード階層](architecture.md)、[基礎ドライバ](drivers.md) |
| IO割当、周辺回路、生成設定と実行時設定 | [ハードウェア](hardware.md) |
| Orion CAN契約に対する実装の対応範囲・差異、電源API、デコーダーの受理条件、4WSの未確定事項 | [基礎ドライバ](drivers.md)、[CAN・IMU・OLED](peripherals.md) |
| IMU/OLEDのドライバ、FW内部診断と同repoの読取りツール | [CAN・IMU・OLED](peripherals.md) |
| ビルド・書込み・CubeMX再生成、日付付き検証記録、未確認範囲 | [開発手順](development.md) |
| 旧ソースの調査根拠 | [ドライバ互換調査（履歴）](driver_compatibility.md) |

## 実装時に残る確認事項

- CM4 SPIは未実装。指令は共通コマンド、帰還は共通128バイトfeedbackを採用する設計案を参照する。SPIインスタンス・配線・CS・速度・転送手順・更新周期・タイムアウト、各項目の取得元と適合確認は今後の課題。現行のCM4_CSはGPIO出力であり、STM32 slaveという要求との対応を確認する。
- 制御入口はTIM6の500 Hzで実装済み。4WSの制御演算、出力、モード遷移、通信断時の停止・復帰は未実装。
- 4WSのCANノード・車輪・操舵軸の割当は未確定。現在の解析はバス別・ID別に値を保持し、旧機体の角度符号反転を行わない。
- 電源・Orion解析は8 byte限定。実機で受信した0x215/216のDLC=4は状態へ反映されず、0x244も解析対象外。相手側の送信定義を確認して対応範囲を決める。
- CAN2の高負荷時RX破棄は未解決。測定条件と結果は[検証記録](development.md)を参照する。

外部仕様を追加する場合はspecsへ対象機種と採用状況を明記し、本repoには対応する実装状況・制約・検証結果を残す。

## 文書更新の扱い

共通契約の配置・単位・ID定義はspecsを参照し、本repoではAPI・対応範囲・差異を説明する。specsに詳細が未定義の項目は、現在のソースで確認できる実装として記録し、機器全体の確定仕様にしない。日付付きの実機検証・旧ソース調査は履歴として保持する。
