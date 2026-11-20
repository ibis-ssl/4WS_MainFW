# Atlas／C610 結合開発

MainFWで2系統のCANを使う開発の入口。ステア基板・C610のPC単体検証と、MainFWでの実装・結合確認を区別する。

## 現在地

| 項目 | 確認済み | Main側で残る作業 |
|---|---|---|
| ステア2基板 | PC代役で基板IDの識別、単一バスおよび別バス接続、初期化・操舵指令・SPI角帰還 | Atlasの受信・指令、全体停止、電源基板との混在 |
| C610×4 | 2バスで帰還とグループ指令を確認。PC速度PI・0 A停止を確認 | C610復号・送信、Mainでの速度制御、搭載状態での4輪追従 |
| 周期と遅延 | PC＋USB-CANの測定では帰還がC610各約1 kHz、ステア約480 Hz。0 A送信はGUI経由の試行で67.4 Hz、直接使用で99.9 Hz | TIM6の周期、受信FIFO／ソフトキューの破棄、データ鮮度、送信間隔をMain上で測る |

直近のPC駆動記録ではg0/g2/g3の回転を確認し、g1（基板0 M1、C610 ID 2）は電流帰還が変化してもほぼ0 rpsだった。2026-11-20にユーザーから動作確認としてOKとの報告があった。個々の車輪の追従やMainとの結合は今後の確認項目とする。PCの50 ms帰還停止や速度PIパラメーターはデバッグツールでの実装値。Mainの停止条件・制御周期の決定にはMain上の計測を使う。

ステア側の参照版は `4WS_G474_Controller` の `1100950374bee9db758e3361872e91e9140504e7`。同じ作業環境のチェックアウト名は `G474_SWCLDvier`。[ステアFW仕様](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/fw_spec.md) · [PC速度試験](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/records/2026-11/c610_gui_speed_20261119.md) · [PC通信計測](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/records/2026-11/c610_can_timing_20261120.md)

## バスと車輪

| 論理バス | ステア基板ID | 軸 | C610 ID | 直近のPC試験 |
|---|---:|---|---|---|
| 基板0側 | 0 | M0=g0、M1=g1 | 1、2 | COM12・保守offset 16 |
| 基板1側 | 1 | M0=g2、M1=g3 | 3、4 | COM46・保守offset 0 |

Classic CAN、標準11 bit、各1 Mbps。物理車輪位置、正方向、MainのFDCAN1/2への割当、電源基板を載せる側は組立状態で照合する。COM番号・保守offsetからMainのFDCAN番号を推定しない。通常CANのIDはFlashの基板IDによって決まり、保守offsetは別管理。電源基板の既存Orion契約を維持する。[Atlas CANのIDと周期](design/atlas_can.md) · [Classicのペイロード](design/atlas_can_classic16.md)

## 開発順序と確認項目

1. **受信とバス識別**。現行のOrion互換パーサーはDLC 8を前提に、`0x200～0x204` をfloat速度・角度、`0x230～0x234` をfloat電流として解釈する。Atlas/C610プロファイルではバスと標準ID、DLC（同じDLCの重複を含む）で専用デコーダーへ先に振り分け、Orion互換経路へ重複投入しない。電源側のDLC 4電圧値も既存受信の課題として扱う。
2. **帰還と鮮度の確認**。Atlasの角度・電流は500 Hz、校正・実状態は約1 Hz、C610のローターrpmは既定1 kHz。内部AB制御に使う値と、SPIに基づく実操舵角を区別する。最終受信時刻と有効性を保持し、TIM6 500 Hzから整合した最新値を参照できるようにする。
3. **出力と停止**。明示許可前の出力状態と、同一バス内のC610電流指令`0x200`の未使用スロット0 Aを先に確認する。ステアは初期化から基板制御と最新の出力指令へ進む。共通`0x000`の送信元別の解釈、全体停止、2バス間の伝搬と再通知抑止を結合確認する。ステア受信側のcoast条件は出力指令100 ms／基板制御500 ms。
4. **負荷試験**。受信キュー破棄・FIFO喪失、C610の帰還最長間隔、送信周期、TIM6の実行時間／オーバーランを取得する。OLED・UART・電源通信を含めた条件で比較する。現行Mainは1バスのソフトRXキューが20フレームで、旧測定でCAN2の破棄が継続した。同じバイナリのUART4カウンターとCANキャプチャを比較し、SWDでCPUを止める読取りは周期の評価に使わない。[Mainの既存診断](peripherals.md)

## 再現用ツール

- ステア個体やC610の切り分けにはステアリポジトリ側の既存GUIを使用する。Mainによる出力とPC代役による出力は切り替えて使う。両基板・USB-CANの識別、操舵、C610速度調整と停止手順は[ステア側のGUI・CLI手順](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/guides/debug_gui_cli.md)を参照。
- Mainの結合時は別のUSB-CANを受信監視に使い、CANRushで両バスを同じCSVに採る。既にCANRush Serverへ接続したバスを取得する例。出力先はMainFWのルートからの相対パス。

```powershell
canrush --server 127.0.0.1:49000 capture --all-buses --duration 12s --output build/hardware/atlas.csv
python ./Script/analyze_atlas_can.py build/hardware/atlas.csv --output build/hardware/atlas-report.json
python ./Script/estimate_atlas_can.py --c610-command-hz 500
```

`analyze_atlas_can.py`はオフラインでCANRushのフレームCSVを解析する。バス／ID／DLCごとの件数、平均Hz、P99・最大間隔、50 ms以上の回数、主要帰還の最終値をJSONに出す。`board_buses`はDLC 8の`0x230/0x232`から推定した候補で、Flash IDの読戻しではない。該当IDの複数バス出現、`shared_board_bus`、`bus_matches`、`missing_periodic_ids`を照合する。監視時間が短いと低速通知を取り逃す。C610の実電流はraw値。間隔はPCのCSV時刻（通常ミリ秒単位）で、CAN線上のタイミングやフレーム欠落の保証には使えない。
