# 4WS_MainFW 作業案内

## 対象と参照先

- 対象はSTM32G474の4輪ステアリング機体のMain基板。着手前に `git status --short --branch`、[概要](doc/overview.md)、関連する現行仕様と[実装・検証計画](doc/implementation_plan.md)を確認する。既存の作業差分を保持する。
- 文書の配置と正本は[文書案内](doc/README.md)を参照する。共通契約は親リポジトリが固定した `specs/`、AtlasのCAN ID・DLC・配置・周期は[接続仕様](doc/design/atlas_can.md)と[ID一覧](doc/design/atlas_can_ids.csv)を参照する。
- ステア基板側のFW・PCツールや旧Orionは該当機能の参照資料として使う。Mainに適用する条件は現行コード、回路、確定要求で照合する。ピン名、他基板の既定値、過去の測定値から仕様を確定しない。

## 実装と生成コード

- ユーザーへの応答・プロジェクト文書・新規ユーザーコードのコメントは日本語とし、ソース・文書・スクリプトはUTF-8で保存する。新しいモジュールの冒頭には役割と前提を短く記す。コードの逐語説明は省く。
- ユーザーコードは `Application/` に配置し、ルート `CMakeLists.txt` に登録する。CubeMX生成ファイルでは `USER CODE BEGIN`／`USER CODE END` を優先し、生成コメントを維持する。変更時は `4WS_MainFW.ioc`、生成初期値、再生成後の差分を照合する。 `cmake/stm32cubemx/CMakeLists.txt` はCubeMX管理。
- 通常制御のTIM6入口は500 Hz。IRQとメインループの責務、HALコールバックとDMA・NVICの所有を[コード階層](doc/architecture.md)と[基礎ドライバ](doc/drivers.md)で確認する。CAN・UARTなどのメインループ専用APIや同期待ちを制御IRQへ持ち込まない。
- Flash配置・リンカ・成果物名・書き込み経路の変更では、[Flash関連の変更確認](doc/development.md#flash関連の変更確認)を同時に行う。

## 検証と記録

- [開発手順](doc/development.md)の検証表に従い、対象構成をビルドする。CMake・ツールチェーン変更はDebug／Releaseとクリーンな構成も確認する。CAN形式、ドライバ、モード・停止処理の変更は該当するホスト検証も行う。
- 実機の書き込み・駆動は対象基板と条件が明確な場合に行う。書き込みでは対象ST-Link、構成・ELF、リセット後の状態を確認する。駆動試験では指令送信元を一つにし、[結合手順](doc/atlas_bringup.md#実機結合の手順)で停止まで確認する。SWDでCPUを止める読取りは駆動・周期計測の終了後に行う。
- 変更に応じてREADMEと現行文書を更新し、コマンドの引数・パスを実装と照合する。実装済み・設定済み・ホスト確認済み・実機確認済みを区別する。結果と未確認範囲は[検証記録](doc/records/README.md)へ残し、現行の課題を実装・検証計画へ反映する。

## コミット

- 変更を目的ごとの単位にまとめ、対応する検証を終えた区切りでコミットする。ステージ済みの差分を確認し、既存の別作業の差分を混ぜない。
