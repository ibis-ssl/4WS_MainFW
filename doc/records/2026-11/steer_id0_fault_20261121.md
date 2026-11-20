# ID 0ステア基板のFault調査

2026-11-21。C610 ID 3の電源配線をつないだとの報告後、MainのRTTとステア基板のCAN保守読取りで状態を確認した。Mainは `e17e01b` のRelease。今回、駆動・リセット・設定書込みは実施していない。

| 観測対象 | 結果 |
|---|---|
| Main RTT | `mode=1 state=7 cal=0 esc=F range=0`、速度目標0 rpm、指令電流全輪0 mA |
| C610帰還 | CAN1（COM12）でID 1/2、CAN2（COM46）でID 3/4。0x203は2,676件受信 |
| ステア基板ID 0（CAN1、保守offset 16） | `error=4`、`fault_axis=1`（M1／g1）、基板状態FAULT、両軸Duty 0%。基板計測電圧23.060 V |
| ステア基板ID 1（CAN2、保守offset 0） | `error=0`、`fault_axis=255`、両軸Duty 0%。基板計測電圧22.984 V |

ID 0側の `error=4` はステアFWの `DUAL_ERR_HOME`。CAN初期校正の失敗を示す。端点間が600°未満、探索距離上限（区間1080°／累積2700°）、探索時間超過などが同じコードになる。Fault処理で探索状態を初期化するため、停止後の保守CAN読取りではどの条件が成立したかは判別できない。観測中の新規 `0x000` も0件で、読めたのはラッチ済みの値。

次回はID 0 M1の探索段階・ABカウント・Dutyを動作中に記録し、最初のFault通知と照合する。機械端手前で止まる場合は摩擦・干渉とAB換算を確認する。既存の600°判定や電流・Duty上限を維持する。

元データは `build/hardware/steer-fault-20261121-can.json`、`steer-fault-20261121-rtt.txt`。OpenOCDで接続したST-LinkはMain側。ステアFWの判定条件は参照チェックアウト `G474_SWCLDvier` の `1100950` による。
