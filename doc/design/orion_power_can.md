# Orionの電源基板とのCAN通信

参照したFWは `G474_Orion_main` の `500855e` と `F303_boost` の `8950ebc`。OrionはCAN1とCAN2へ同じ電源指令を送り、4WSの電源基板はCAN1に接続する。[CAN接続](../atlas_bringup.md#バスと車輪)

## PowerOnの継続送信

- Orionの `actuatorPower_ONOFF()` は標準ID `0x010`、DLC 8。PowerOnは `00 01 00 00 00 00 00 00`、PowerOffは全byteが0。
- 通常運転ではTIM7割り込みのログ時刻判定で送信し、`MAIN_MODE_ERROR` 中はPowerOff、それ以外はPowerOnを送る。一時的な走行停止と電源断は別の扱い。通常ページは8割り込みごとでおよそ16 ms、TUIは50割り込みごとでおよそ100 ms。旧FWではUARTのページ設定によって送信間隔も変わる。
- FW更新ゲートウェイが有効な間はTIM7のこの処理より手前でreturnするため、通常のPowerOn送信が止まる。
- `F303_boost` はPowerOn受信で `sw_enable_cnt=200` に戻す。メインループで1ずつ減らし、0になると電源出力・昇圧PWM・キックPWMを停止する。通常ループは72 MHz、TIM1分周73、CNTを1000減らす構成から計算上およそ1 ms。200カウントは約200 msの目安で、処理遅延と搭載FWによる差は実機で確認する。明示的なPowerOffでも停止処理に入る。

## 指令とテレメトリー

| CAN ID | 方向 | Orion／電源FWでの用途 | 4WS Mainの実装状況 |
|---|---|---|---|
| `0x001` | Main→電源 | `5A A5` でリセット要求。参照電源FWでは異常時に再起動し、個別ACKは送らない | 起動時にCAN1へ送る |
| `0x010` | Main→電源 | PowerOn／Off、および保護値設定。設定はbyte 0がID、byte 1～4がfloat。Orionの起動時設定は最低15 V、最高35 V、最大50 A、FET・コイルの上限各90℃ | 送信APIあり。周期送信・起動時の閾値書込みは未実装 |
| `0x110` | Main→電源 | 昇圧目標、充電許可、ストレート／チップ選択、キック要求 | 未実装 |
| `0x215` / `0x216` / `0x234` | 電源→Main | バッテリー電圧／キャパシター電圧／バッテリー電流。参照電源FWはDLC 4でfloatを送る | CAN1でDLC 4・8を受理しUARTへ表示 |
| `0x224` / `0x244` | 電源→Main | 温度3値／状態フラグ。`0x244` の許可状態 `En` と出力ピン `Out` は別のビット | CAN1で受信しUARTへ表示 |
| `0x000` | 電源→Main | ノード100のFaultコード・付随値 | CAN1で電源Faultを識別し、最新値をUARTへ表示。共通Faultの停止処理にも渡す |
| `0x611` → `0x6C4` | 照会→応答 | ノード100のFWバージョン照会と応答 | USB-CANから読取り照会を実施済み。実電源基板の応答は未確認 |

参照電源FWは起動時の接続・自己確認を終えてから周期通知を開始する。`canDataSender()` はメインループ6回を1巡として温度、バッテリー電圧、キャパシター電圧、電流、状態を順に送る。周期通知がない場合はPowerOnの有無に加え、給電・CAN配線・自己確認の状態も切り分ける。自己確認が実機でどこまで進んだかは未確認。[前回のCAN採取](../records/2026-11/uart_fixed_width_power_20261121.md)

4WSでの継続送信はCAN1専用とし、UARTの表示周期から独立させる。正常運転時の周期はOrionの通常表示に近い20 msを候補とする。起動中・共通Fault・FW更新中に給電を許可する条件を確定してから有効にする。ステア更新中の給電を維持すべきかは、電源出力がステアとC610のどこに接続されるかで決まる。Orionの保護値は4WSの採用値として扱わない。

ソースの照合箇所はOrionの `Core/Src/main.c` の起動処理と `HAL_TIM_PeriodElapsedCallback()`、`Core/Src/actuator.c` の電源指令、電源FWの `Core/Src/main.c` のCAN受信・`connectionTest()`・メインループ、`Core/Src/can.c` の送信処理。バイト配置の共通仕様は[Orion CAN](../../specs/common/orion_can.md)を参照。
