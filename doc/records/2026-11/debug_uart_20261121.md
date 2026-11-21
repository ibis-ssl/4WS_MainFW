# Orion形式のUART4診断ページ実機確認

2026-11-21。MainのST-LINK/V3MINIE（SN `002D00373033510635393935`、Main UID `0045004D 4B305018 20383859`）、UART4のVCP `COM167`、2,000,000 baud・8N1。対象FWは `948e105` のRelease。ELF SHA256は `eab26a1acab891780bc81b6e7e22e808a3c1e448b9595584fb566a0fa11c7d0d`。

- 電源投入後、`./Script/flash.ps1 -Configuration Release -Serial 002D00373033510635393935` が書込み、照合、リセットを完了した。ビルド日時を含む起動バナー、ANSI付き一覧、既定の `SYSTEM` を受信した。
- `0`～`9`、CRLF、Backspace、DEL、`?` によるページ切替・一覧再表示を確認した。CRLFで1ページ進み、`q/a/w/s/t/g` 入力時は同じページの表示が継続した。実測した23行の平均間隔は100 ms、観測中のTIM6オーバーランとUARTエラーは0だった。
- `MOTOR` はC610 ID 1～4の帰還と全軸0 mAの指令を表示した。`ODOM` はICM-20602のWHO_AM_I `0x12`、加速度・角速度3軸、26.5℃を表示した。`MOTION` は4軸とも未校正の状態を表示し、操舵角・温度の無効値を `n/a` とした。操舵校正、モーター駆動、CM4の実SPI接続は今回の試験の対象外。
- CAN1の3秒間の監視で電源の `0x215 / 0x216 / 0x224` が観測されなかった。COM12から測定通知の試験フレームを各1回送信し、`KICKER` で24.4 V、0.0 V、31／32／33℃を確認した。2.5秒後に `stale` へ変わった。実電源基板の測定値は未確認。
- 試験値を消去するMainリセットの1回目は `Boot=FAILED` を表示し、制御を抑止した。失敗時のCANログは未採取。再リセット時は両ステアからリセット応答とFault値0が返り、`Batt=-- Boot=READY SYSTEM` を確認した。再リセット時の両CANの `0x200` は採取分すべて0 Aだった。

Debug／Releaseのクリーンビルドと `python ./Script/test_drivers.py` が成功。元ログは `build/hardware/debug-uart-20261121-171711/` の `flash.txt`、`uart.jsonl`、`summary.json`、`power-can-uart.jsonl`、`final-uart.jsonl`、`final-retry-uart.jsonl` と `final-retry-COM12.jsonl`／`final-retry-COM46.jsonl`。端末の設定とページ表は[UART4デバッグ](../../drivers.md#uart4デバッグ)を参照。
