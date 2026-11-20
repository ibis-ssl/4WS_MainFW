# RTT校正によるID 0 M1のFault再現

2026-11-21。全4輪を浮かせた固定状態で、MainのRTTから `init` を1回実行した。USB-CANはCAN1がCOM12、CAN2がCOM46。CAN1の保守RPCでID 0の探索段階、ABカウント、指令Duty、累積移動量を採取した。

| PCでの観測時刻 | 結果 |
|---:|---|
| 0.452秒 | Mainが両バスへ全軸校正要求 `0x350/0x352 03 03` を送信。RTTで `OK init` |
| 0.714秒～1.987秒 | ID 0 M1は第1、第2探索へ進み、指令Dutyは＋20%から−20%に反転。ABは−3～−7 counts、累積移動量は最後に読めた時点で89 counts |
| 2.056秒 | CAN1に `0x000`。送信元g1、コード4。開始から約1.60秒 |
| Fault直後 | Mainは両バスでcoastと0 Aを各3回送信。RTTはFAULT。走行試験は開始されず、非ゼロのC610指令も0件 |

同じID 0のM0はABが−35,199から+314,933 countsまで動いた。M1は正負のDuty指令に対しAB変位がほぼ無く、両端の探索幅不足と整合する。Fault処理後は最終端点と区間の終了理由が消去されるため、40°以下の端点判定と600°未満の受入判定のどちらが発火したかは直接読めなかった。今回のDutyはFWの指令値で、軸の実変位とモーター端子出力は未測定。M1が物理的に動いていない場合と、ABに動きが反映されない場合の切り分けを残す。

Debug／ReleaseビルドとHALモック試験を実施。モックはRTTから全軸に校正を送ること、`keep` による継続、500 ms途絶時の両バス停止を確認した。Mainに書き込んだRelease ELFのSHA256は `88BC768283052D024FE8005587D612B04EB41B8539FDD5DA5EA8876F7FD3F8F1`。SWD書き込み・照合・リセットに成功した。

元ログは `build/hardware/motor-rtt-20261121-live/` の `main-rtt.jsonl`、`CAN1.csv`、`CAN2.csv`、`CAN1-axis.jsonl`、`summary.json`。時刻はUSB-CAN・RTTを読んだPCでの観測値。
