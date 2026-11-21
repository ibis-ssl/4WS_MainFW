# 文書案内

[プロジェクト概要](overview.md)でMainFWの役割と実装範囲を確認し、目的に合う文書を参照する。

| 確認したい内容 | 参照先 |
|---|---|
| 開発環境、ビルド、書き込み、CubeMX再生成 | [開発手順](development.md)、[スクリプト一覧](../Script/README.md) |
| モーターデバッグの操作、CAN配線、実機結合 | [Atlas／C610結合開発](atlas_bringup.md) |
| ステア温度の読取りとCM4相当のステアFW更新 | [温度とステアFW更新](steer_fw_update.md) |
| デバッグUARTのOrion互換項目と4WSで変更する表示 | [Orion出力比較](design/debug_uart_orion.md) |
| 電源基板のPowerOn継続送信とOrionとの対応 | [Orionの電源CAN](design/orion_power_can.md) |
| 電源監視、キッカー／ドリブラーの試験、電源FW更新 | [電源基板の操作](power_bringup.md) |
| ピン・回路、既存ドライバ、診断 | [ハードウェア](hardware.md)、[基礎ドライバ](drivers.md)、[CAN・IMU・OLED](peripherals.md)、[コード階層](architecture.md) |
| 共通契約とMainFWの責務 | [管理境界](specs_migration.md)、[共通仕様](../specs/README.md) |
| AtlasのCAN ID・DLC・配置・周期 | [接続仕様](design/atlas_can.md)、[ID一覧](design/atlas_can_ids.csv) |
| 次に確認する作業 | [実装・検証計画](implementation_plan.md) |
| 実施済みの試験と当時の条件 | [検証記録](records/README.md) |

## 更新先

- 現行仕様は該当文書と実装を一緒に更新する。手順では条件の定義元を参照し、コマンド例と規定値を区別する。
- `design/` には採用した仕様、比較、設計案を置く。採用状態は本文で確認する。旧世代の[互換調査](driver_compatibility.md)は現行実装を確認するための参考資料。
- 未解決作業は実装・検証計画に反映する。履歴の数値や判断は試験当時のまま保持し、次の測定は新しい記録として追加する。
- 文書変更時はローカルリンクと見出し、コマンドの引数、成果物やログの保存先を照合し、`git diff --check` で差分を確認する。
