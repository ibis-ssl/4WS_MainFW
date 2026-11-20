# Atlas／C610 結合開発

MainFWで2系統のCANを使う開発の入口。ステア基板・C610のPC単体検証と、MainFWでの実装・結合確認を区別する。

## 現在地

| 項目 | PCをMainの代役にした確認 | Main側 |
|---|---|---|
| ステア2基板 | 基板IDの識別、初期化・操舵指令・SPI角帰還 | Mainに角度指令・校正・帰還の処理を実装。実機結合は未検証 |
| C610×4 | 2バスの帰還・グループ指令、PC速度PI・0 A停止 | Mainに速度PIと0 A停止を実装。搭載状態での各輪の追従は未検証 |
| 周期と遅延 | PC＋USB-CANで帰還がC610各約1 kHz、ステア約480 Hzを計測 | 角度500 Hz・基板状態10 Hz・C610指令100 Hzを目標に実装。Main上での送信間隔、受信欠落は未測定 |

直近のPC駆動記録ではg0/g2/g3の回転を確認し、g1（基板0 M1、C610 ID 2）は電流帰還が変化してもほぼ0 rpsだった。2026-11-20にユーザーから動作確認としてOKとの報告があった。Main結合でも各輪の追従を確認する。

ステア側の参照版は `4WS_G474_Controller` の `1100950374bee9db758e3361872e91e9140504e7`。同じ作業環境のチェックアウト名は `G474_SWCLDvier`。[ステアFW仕様](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/fw_spec.md) · [PC速度試験](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/records/2026-11/c610_gui_speed_20261119.md) · [PC通信計測](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/records/2026-11/c610_can_timing_20261120.md)

## バスと車輪

| Mainの物理バス | ステア基板ID | 軸 | C610 ID | 電源基板 |
|---|---:|---|---|---|
| CAN1（FDCAN1） | 0 | M0=g0、M1=g1 | 1、2 | 接続あり |
| CAN2（FDCAN2） | 1 | M0=g2、M1=g3 | 3、4 | 接続なし |

Classic CAN、標準11 bit、各1 Mbps。Mainの物理バスと電源基板の割当は2026-11-20のユーザー指定。物理車輪位置と機体に対する正方向は組立状態で照合する。通常CANのIDはステアFWのFlashに設定した基板IDで決まる。[Atlas CANのIDと周期](design/atlas_can.md) · [Classicのペイロード](design/atlas_can_classic16.md)

## モーターデバッグの操作

SW7のコードを **3** に設定する。旧Orionのモーター試験の番号を採用し、現行基板のDIP_0～3を押下Lowのビット0～3として読む。コード3のOLED生レベル表示は `DIP 0123 0011`。モードに入ったら全ボタンを30 ms以上離す。起動時の選択はg0。OLED先頭行に対象輪と状態を表示する。

| 操作 | 動作 |
|---|---|
| SW9（SW_1）を押して離す | `g0 → g1 → g2 → g3 → ALL → g0` と切り替える |
| 5方向ボタンの中央を押し続ける | 選択輪の操舵初期化。校正完了、30秒経過、ボタン解放のいずれかで終了する |
| 左／右を押し続ける | 初期化済みの選択輪へ中立基準で−15°／＋15°を指令する。0.5 A・Duty 20%が上限 |
| 上／下を押し続ける | 選択輪のC610をそれぞれ＋5／−5ローターrpsへランプさせる。上限0.5 A。4輪走行には全輪の操舵初期化が必要 |

走行操作の符号はC610のローター回転方向を示す。単輪試験で実車輪の向きを照合する。5方向ボタンは抵抗ラダーのため、操作時は1方向ずつ押す。ステアFWの保存電流上限や可動域が指令を許容しない場合は、ステアFWが指令を受理しない。

ボタン解放、モード離脱、帰還途絶、指令投入失敗時は保留中の周期指令を取り消し、両バスへ0 Aとcoastを送る。動作再開にはボタン解放と新たな押下が必要。共通エラー `0x000`、AtlasのFAULT、Mainのbus-offはリセットまで出力禁止とする。CAN上での停止指令の到達と実際の停止時間は結合時に確認する。

## 実装条件と実機確認

- Atlas/C610のIDはバスとDLCで振り分け、Orionの重複IDを復号しない。CAN1の電源電圧は4/8バイトを受理する。
- 角度指令はメインループから2 ms間隔を目標に送り、C610速度PIは10 ms間隔を目標に実行する。C610は3サンプル以上、最終受信から50 ms未満、始動時に40 rpm以下を必要とする。Atlasの高速帰還は150 ms未満、校正通知は2.5秒未満を必要とする。C610のPI・フィードフォワードとランプはPC版の既定値を使用する。

### 実機結合の手順

1. [書き込み手順](development.md#st-link書き込み)でMainの構成、ELF、対象ST-Linkを照合する。車輪を浮かせ、CAN1／CAN2とステア基板ID・C610 ID・電源基板の接続先を確認する。PCが指令を出す試験とMainが指令を出す試験は切り替えて行う。
2. まず駆動せずに両バスを採取し、ステアの角度・校正通知、対象C610の帰還、CAN1の電源電圧を確認する。OLEDのSW7コード表示を確認し、UARTの受信数・破棄数・bus-offの基準値を記録する。
3. 単輪を選択し、中央で初期化して左右の角度指令、上下の速度指令を確認する。対象輪と正負方向、解放時のcoast／0 Aと実際の停止を記録する。全輪の単輪確認を終えてから四輪操作を確認する。
4. 電源基板・OLED・UARTを含む構成で、モード離脱、帰還途絶、共通エラー時の停止を試験条件に応じて確認する。CANログでは両バスの停止指令、未使用スロットの0 A、帰還の最長間隔とMainの送信間隔を照合する。
5. 試験終了時は全ボタンを解放し、モーターデバッグを解除して停止を確認する。SWDによる診断RAM読取りは駆動・周期計測の終了後に行う。FW、基板、試験条件、ログ、結果は[検証記録](records/README.md)に残す。

## 再現用ツール

- ステア個体やC610の切り分けにはステアリポジトリ側の既存GUIを使用する。Mainによる出力とPC代役による出力は切り替えて使う。両基板・USB-CANの識別、操舵、C610速度調整と停止手順は[ステア側のGUI・CLI手順](https://github.com/ibis-ssl/4WS_G474_Controller/blob/1100950374bee9db758e3361872e91e9140504e7/doc/guides/debug_gui_cli.md)を参照。
- Mainの結合時は別のUSB-CANを受信監視に使い、CANRushで両バスを同じCSVに採る。既にCANRush Serverへ接続したバスを取得する例。出力先はMainFWのルートからの相対パス。

```powershell
New-Item -ItemType Directory -Force ./build/hardware | Out-Null
canrush --server 127.0.0.1:49000 capture --all-buses --duration 12s --output build/hardware/atlas.csv
python ./Script/analyze_atlas_can.py build/hardware/atlas.csv --output build/hardware/atlas-report.json
python ./Script/estimate_atlas_can.py --c610-command-hz 100
```

帯域試算の100 HzはMainのモーターデバッグでのC610指令周期。出力は `classic16_pair` の行を参照し、電源なしの `bus A` をCAN2、電源ありの `bus B+power` をCAN1のモデルとして読む。将来の500 Hz指令と比較する場合は `--c610-command-hz 500` を指定する。

`analyze_atlas_can.py`はオフラインでCANRushのフレームCSVを解析する。バス／ID／DLCごとの件数、平均Hz、P99・最大間隔、50 ms以上の回数、主要帰還の最終値をJSONに出す。`board_buses`はDLC 8の`0x230/0x232`から推定した候補で、Flash IDの読戻しではない。該当IDの複数バス出現、`shared_board_bus`、`bus_matches`、`missing_periodic_ids`を照合する。監視時間が短いと低速通知を取り逃す。C610の実電流はraw値。間隔はPCのCSV時刻（通常ミリ秒単位）で、CAN線上のタイミングやフレーム欠落の保証には使えない。
