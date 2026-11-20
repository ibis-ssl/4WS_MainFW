# 全軸操舵とMain RTT・USB-CAN確認

2026-11-21の記録。現行の[操作と実機結合手順](../../atlas_bringup.md)と[実装・検証計画](../../implementation_plan.md)を参照する。

- Debug／Releaseのクリーンビルドと `python ./Script/test_drivers.py` が成功。中央ボタン解放後の全4軸校正、左右の90°/秒の目標変化と軸ごとのクランプ、保守offset 0/16、C610帰還不足時のRTT駆動拒否、2.2秒での出力終了をホストで確認した。
- リリースELFをMainのSTLINK-V3MINIE（SN `002D00373033510635393935`、SWD 1 MHz）から書き込み、照合とリセットが成功した。Release ELFのSHA256は `126E0DB4EA2F5E4C401CC3871014FB23C870C37C3210C3AC444821A70D06BB10`。全4輪を浮かせて固定し、他の指令送信元を停止した条件。
- MainのRTTチャネル0から `mode=1 state=2 cal=1 esc=B range=0` を受信した。校正済みはg0のみ。C610帰還はID 1・2・4。RTTの `drive+` は `REJECT drive`、`stop` は `OK stop`。指令電流の状態は全輪0 mAだった。
- 同時にUSB-CANで約2秒観測し、CAN1（COM12）で `0x201` を2,055件、`0x202` を2,044件、CAN2（COM46）で `0x204` を2,051件受信した。CAN2の `0x203` は0件。両バスで `0x200` の走行電流指令は観測されなかった。
- 元記録は `build/hardware/main-rtt-20261121-flash.txt`、`main-rtt-20261121-status.txt`、`main-rtt-20261121-run.json`。全軸校正完了、軸別範囲の実機取得、C610/M2006の正負方向・速度追従・停止時間は未確認。
