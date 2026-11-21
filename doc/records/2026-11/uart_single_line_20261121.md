# UART4の1行表示確認

2026-11-21。`c2b7b53` のMain ReleaseをST-Link SN `002D00373033510635393935` で書き込み、照合・リセットした。ELF SHA256は `e05bfd85933fd23397e96f2acda57e4eae3a6b0fb55bf4c17d8f52ac773719fd`。

`COM167`（2 Mbps・8N1）で起動から178行を採取した。全11ページで、選択後の周期表示はページごとに1行で行幅が一定だった。最長は `MOTION` の193 byte（CRLF込み）。電源フレームは未受信表示、C610の指令電流は全輪0 mA、終了時は `SYSTEM` の `BT=RDY F=0`。周期PowerOnは今回の実装範囲外で、給電・駆動試験は行っていない。

Debug／Releaseのビルドと `python ./Script/test_drivers.py` が成功。ホスト試験では電源電流の未受信・負値・期限切れで後続ラベル位置が一致することも確認した。実機ログは `build/hardware/uart-single-line-20261121-185548/` の `flash.txt`、`uart.jsonl`、`summary.json`。PowerOnの継続条件は[Orion電源CANの調査](../../design/orion_power_can.md)を参照。
