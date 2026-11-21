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

SW7のコードを **3** に設定する。旧Orionのモーター試験の番号を採用し、現行基板のDIP_0～3を押下Lowのビット0～3として読む。コード3のOLED生レベル表示は `DIP 0123 0011`。モードに入ったら全ボタンを30 ms以上離す。OLED先頭行にモーターの状態を表示する。

| 操作 | 動作 |
|---|---|
| 5方向ボタンの中央を一度押す | 全4軸の操舵初期校正を開始する。校正完了、30秒経過、他方向の押下またはモード離脱で終了する |
| 左／右を押し続ける | 全4軸の目標角度を−90°/秒／＋90°/秒で変更し、軸ごとの許容端で止める。各軸1.5 A・Duty 20%が上限 |
| 上／下を押し続ける | C610全4輪をそれぞれ＋5／−5ローターrpsへランプさせる。上限は各輪0.5 A。開始には全輪の操舵初期校正とC610帰還が必要 |

走行操作の符号はC610のローター回転方向を示す。走行中はOLEDに `MOTOR ALL DRIVE` を表示する。5方向ボタンは抵抗ラダーのため、操作時は1方向ずつ押す。ステアFWの保存電流上限や可動域が指令を許容しない場合は、ステアFWが指令を受理しない。

左右の解放で目標値の更新を止め、操舵はcoastにする。次の操作では保存した目標値から更新を再開する。再校正後の目標値には、ステアFWから読み出したAB実角を使う。上下の解放、モード離脱、帰還途絶、指令投入失敗時は保留中の周期指令を取り消し、両バスへ0 Aとcoastを送る。共通エラー `0x000`、AtlasのFAULT、Mainのbus-offはリセットまで出力禁止とする。

## 起動時の周辺基板リセット

Mainの電源投入・リセット後、CAN1／CAN2のC610グループへ0 A、ステアへcoastを送る。電源基板の接続先CAN1へ、旧Orionの `resetPowerBoard()` と同じ `0x001`、DLC 8、`5A A5 00 00 00 00 00 00` を送る。電源基板の再起動完了は応答で照合できないため、ここでは指令の送信投入までを確認する。

電源リセット投入から300 ms後、各CANバスで保守offset 0/16を照会し、基板IDを照合して該当ステア基板へ既存の保守RPC RESET（opcode 31）を1回送る。各基板の応答を受けてから300 ms待ち、再起動後にFault値が0であることを読む。処理中は駆動を許可せず、確認失敗または起動から5秒で未完了なら駆動禁止を維持する。ステアの再起動後は操舵の初期校正が必要。

C610には公開CAN仕様で再起動指令が定義されていない。Mainは電流指令 `0x200` で0 Aを送る。 [RoboMaster C610公式ユーザーガイド](https://rm-static.djicdn.com/tem/17348/RoboMaster%20C610%20Brushless%20DC%20Motor%20Speed%20Controller%20User%20Guide.pdf)

## 実装条件と実機確認

- Atlas/C610のIDはバスとDLCで振り分け、Orionの重複IDを復号しない。CAN1の電源電圧は4/8バイトを受理する。
- 角度指令はメインループから2 ms間隔を目標に送り、C610速度PIは10 ms間隔を目標に実行する。C610は3サンプル以上、最終受信から50 ms未満、始動時に40 rpm以下を必要とする。Atlasの高速帰還は150 ms未満、校正通知は2.5秒未満を必要とする。C610のPI・フィードフォワードとランプはPC版の既定値を使用する。
- 各軸の許容範囲はステアFWのCAN保守RPC AXIS 7/8で得た両機械端から各30°内側。保守offset 0/16のいずれかへ読取りを試みる。両基板から範囲を取得できるまで角度操作を待つ。CAN保守RPCは範囲とAB実角の読取りに使用する。

### 実機結合の手順

1. [書き込み手順](development.md#st-link書き込み)でMainの構成、ELF、対象ST-Linkを照合する。車輪を浮かせ、CAN1／CAN2とステア基板ID・C610 ID・電源基板の接続先を確認する。PCが指令を出す試験とMainが指令を出す試験は切り替えて行う。
2. まず駆動せずに両バスを採取し、ステアの角度・校正通知、対象C610の帰還、CAN1の電源電圧を確認する。OLEDのSW7コード表示を確認し、UARTの受信数・破棄数・bus-offの基準値を記録する。
3. 中央で全軸を初期校正し、左右の角度指令、変化速度、各軸の許容端、解放時のcoastを確認する。次に上下またはRTTの全4輪速度試験で、車輪の正負方向、0 A指令と実際の停止を記録する。
4. 電源基板・OLED・UARTを含む構成で、モード離脱、帰還途絶、共通エラー時の停止を試験条件に応じて確認する。CANログでは両バスの停止指令、未使用スロットの0 A、帰還の最長間隔とMainの送信間隔を照合する。
5. 試験終了時は全ボタンを解放し、モーターデバッグを解除して停止を確認する。SWDによる診断RAM読取りは駆動・周期計測の終了後に行う。FW、基板、試験条件、ログ、結果は[検証記録](records/README.md)に残す。

## 再現用ツール

ステア温度の診断と、Mainを経由したステアFW更新の手順は[温度とステアFW更新](steer_fw_update.md)を参照する。

### MainのRTT

```powershell
openocd -c 'set MAIN_STLINK_SERIAL STLINK_SERIAL' -f ./Script/openocd_main_rtt.cfg
```

`STLINK_SERIAL` を対象の番号へ置き換える。OpenOCDはCPUを止めずにRTTチャネル0を `127.0.0.1:19030` へ公開する。TCPで改行付きの `status` を送ると、RPMと指令電流、および `cal`（操舵校正）、`esc`（C610帰還）、`range`（操舵範囲）の4輪ビットマスクが返る。各マスクはbit 0～3がg0～g3に対応する。`boot` は周辺基板リセットの状態で、0が処理中、1が確認完了、2が失敗。

以前のFWで転送後にOpenOCDがステアBIN内の `SEGGER RTT` を拾う場合は、搭載中のMainと同じELFの `main_rtt` を `arm-none-eabi-nm -S` で調べる。表示された16進アドレスに `0x` を付け、OpenOCDの `-f` の前に `-c 'set MAIN_RTT_ADDRESS 0x2001a098'` の形式で渡す。例のアドレスは搭載FWによって異なる。

全4輪を浮かせ、他の駆動指令元を停止し、モーターデバッグに入ってボタンを離した状態で試験する。RTTの `init` は全軸の校正要求を開始する。校正中はホストが `keep` を送り、500 ms途絶するとMainが停止する。校正時間の上限は30秒。

速度試験は `cal=F esc=F` と4輪の停止を確認して `drive+` または `drive-` を1回送る。駆動は最大2.2秒、目標は±300ローターrpm、指令上限は各輪0.5 A。`stop`、ボタン押下、モード離脱、帰還断でも停止する。

RTTと両CANを同時記録する例。Pythonはステア側の `python-can` とCANRushアダプターを利用できる環境を使う。

```powershell
& ../G474_SWCLDvier/.venv/Scripts/python.exe ./Script/debug_motor_rtt.py sequence `
  --can1 COM12 --can2 COM46 --steer-repo ../G474_SWCLDvier
```

`sequence` は `init`、`drive+`、`drive-` の順に実行し、完了条件に達しない段階で停止する。段階を個別に実行する場合は、`sequence` を対象のコマンドへ置き換える。ログは既定で `build/hardware/motor-rtt-YYYYMMDD-HHMMSS/` へ保存する。CANフレーム、ID 0の保守CANによる軸読取り、RTT送受信、結果の要約を収録する。保守offsetの既定は16で、0の場合は `--id0-offset 0` を指定する。

### USB-CANと帯域試算

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
