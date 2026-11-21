# UART4固定幅と電源基板テレメトリーの確認

2026-11-21。MainのRelease (`cf3b33d`) をST-Link SN `002D00373033510635393935` で書き込み、照合とリセットを実施した。ELF SHA256は `8f51bcf6ee8c787c4478c80d52b172f77d1f81ccd018845f9b3865df3a8a592a`。ST-Link VCP `COM167`、2,000,000 baud・8N1でUART4を受信。

- 起動バナーから全11ページの計419行を採取した。最長はANSI制御文字・CRLF込みで156 byte。モーター・操舵は2軸ごと、システムはCANバスと制御の各行に分割した。電源ページでは未受信、表示中の正負の小数、桁数の変化、期限切れを照合し、後続ラベルの列位置が一定だった。
- CAN両系統でステアとC610の周期帰還を取得した。CAN1には電源リセット `0x001` が送出された。電源の周期通知 `0x215/216/224/234/244` は両バスで観測されず、CAN1の電源ノード100へのバージョン照会 `0x611` も3回とも `0x6C4` が返らなかった。実電源基板の稼働状態と搭載FWは未確認。
- USB-CAN（COM12）でCAN1へ測定通知を模擬し、24.4 V・−3.3 A・状態 `1D`、9.9 V・100.0 V・+25.0 A・温度5/99/103℃・状態 `2` をUARTで確認した。通知停止後は各欄が `stale` に変わった。共通Faultはホスト試験でCAN1・ノード100のみを識別した。電源ON/OFF・充電・キックの指令は送信していない。
- Debug／Releaseのビルドと `python ./Script/test_drivers.py` は成功。最終UARTページは `SYSTEM`、`BT=RDY F=0`。採取中のTIM6オーバーラン、両CANの受信破棄、bus-offは0だった。

生ログは `build/hardware/uart200-20261121-183800/` の `uart.jsonl`、`COM12.jsonl`、`COM46.jsonl`、`summary.json`、`power-paced.jsonl` と、`build/hardware/uart200-power-version-20261121.json`。ページ表と受信形式は[基礎ドライバ](../../drivers.md#電源基板)を参照。
