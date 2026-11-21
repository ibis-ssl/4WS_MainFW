# 基礎ドライバの実装仕様

## 今回の確定事項と状態

2026-09-13のユーザー指定に基づき、Classic CAN転送、旧電源基板通信、UART4デバッグ、Hz指定ブザー、旧HWと同じADCユーザーSWを実装した。2026-10-08にはOrion形式のCAN受信解析、ICM-20602取得、SH1106表示を追加した。Atlas/C610のボタン操作は[モーターデバッグ](atlas_bringup.md#モーターデバッグの操作)を参照する。ドリブラー・キッカーとESC PWMの出力は未実装。

`main()`のUSER CODEから`app_init()`、メインループから`app_process()`を呼ぶ。起動時はUART4受信、ADC取得、両CANの受信を開始し、ブザーはCCR=0の停止状態にする。IMU取得の状態初期化・OLED検出後、TIM6の500 Hz制御IRQを開始する。UART状態表示は100 ms間隔。電源リセットは[起動時の周辺基板リセット](atlas_bringup.md#起動時の周辺基板リセット)で送信し、電源ON/OFFと保護値は明示要求時に送る。基礎ドライバの初期化失敗は`Error_Handler()`へ伝え、IMU・OLEDの非接続や失敗はmainで再試行する。

## 動作中のLEDシーケンス（2026-10-10）

Appが`board_gpio_write_led_level()`を使用し、LED_0→LED_1→LED_2→LED_3→LED_R→LED_G→LED_Bを1個ずつ各200 ms点灯する。初めに全消灯してLED_0を点灯し、青の後はLED_0へ戻して動作中も繰り返す。切替時は前のLEDを消灯してから次を点灯する。`app_init()`で開始し、`app_process()`で経過時刻を確認して切り替える。HAL_DelayやIRQ内の操作は行わない。mainの遅延時は切替を追い掛けず、次のLEDの表示時間を確保する。

`App/app.h`の`APP_LED_SEQUENCE_ACTIVE_HIGH_MASK`はbit 0～6が0/1/2/3/R/G/Bに対応し、1でHigh点灯、0でLow点灯。ユーザー指定により0x7F（全High点灯）とした。表示時間は`APP_LED_SEQUENCE_DURATION_MS`で変更できる。GPIOのピン設定・`.ioc`・生成コードは変更していない。

## 機体制御タイマー（2026-09-14）

`Board/board_control_timer`がTIM6を所有し、登録された`App/app_control_step()`をIRQ内から直接呼ぶ。指定周期は`APP_CONTROL_FREQUENCY_HZ=500`。現在の170 MHzではPSC=169で1 MHzカウンター、ARR=1999で正確な2 msとなる。旧世代のPSC=170・ARR=2000という近似値は転用しない。実時間の精度はHSIの精度に依存する。

TIM6_DAC IRQの優先度は2。CAN・ADC・UARTより先に実行し、mainの処理やログ整形に依存させない。DACは使用せず、この共有IRQはTIM6専用とする。TIM6の制御入口ではSWを取り込む。モーターデバッグの速度PIとCAN送信はmainで実行する。

通常ドライバのクリティカルセクションはBASEPRIで優先度5以下の緊急度のIRQだけをマスクし、制御IRQを止めない。制御IRQはmain専用APIに割り込んで同じデータを操作してはならない。ADC読取りは二重バッファ公開によって制御IRQからも利用できる。

DWT CYCCNTを有効にし、制御コールバックの最大実行サイクル、TIM6カウンターによる最大入口遅延us、実行回数、周期超過回数を記録する。計測値取得は制御IRQを止めない項目別スナップショット。周期超過または次の更新発生を検出した場合は計数し、溜まった更新をクリアして連続再実行を避ける。入口遅延は1周期内の値であり、長い全IRQ禁止による全欠落周期数を表すものではない。実機での最悪実行時間・ジッターは未測定で、演算を追加する際は2 ms以内に収まることを確認する。

## CAN転送

`Application/Board/board_can.c/.h`が両バスを担当する。

通信契約と接続は[Orion CAN](../specs/common/orion_can.md)と[Atlas/C610のバス・車輪割当](atlas_bringup.md#バスと車輪)を参照する。

| 項目 | 実装 |
|---|---|
| モード | Classic CAN、Normal、自動再送有効、BRSなし |
| nominal設定 | 170 MHz / (10 × (1 + 14 + 2)) = 1 Mbit/s、SJW=1、サンプル点約88.2% |
| 送信 | 標準ID 0～0x7FF。通常のFIFO APIは8 byte固定、即時APIは0～8 byte |
| 受信 | 標準ID、Classic、data frame、DLC 0～8を検証。データ・長さ・受信msを公開 |
| フィルター | 各バス標準マスク1個で全標準IDをFIFO0へ。拡張IDとremote frameはglobal filterで拒否 |
| TX待ちFIFO | 各バス20フレーム。FIFO順を保持し、満杯時は新規要求をfalseで拒否して破棄数を加算 |
| RX待ちFIFO | 各バス20フレーム。満杯時は新規受信を破棄して記録 |
| 処理上限 | RXは1回3フレームまで。IRQ後に残った分はメイン側からも回収 |
| 失敗 | HAL送信登録失敗時は先頭を保持して次の処理で再試行。bus-off状態変化は計数し、自動復帰は行わない |

`board_can_send()`のtrueはドライバへの受理であり、バス上の送信完了・ACK・相手機器の処理完了ではない。ACKが得られない場合はハードウェア再送と有限FIFOにより、後続要求が拒否され得る。`board_can_process()`はメイン処理で継続して呼ぶ。公開APIはメイン処理専用で、IRQと共有するFIFO・統計の更新は短いクリティカルセクションで保護する。

モーターデバッグは`board_can_send_immediate()`でハードウェアの空き枠へ直接投入する。満杯・HAL送信失敗時はfalseを返し、古い周期指令をソフトウェアに保留しない。停止時は`board_can_cancel_immediate()`でハードウェアの未送信周期指令を取り消す。既に送信が始まったフレームは相手に届く場合がある。

使用中のHALではDLC定数は0～15の符号値であり、8 byteは`FDCAN_DLC_BYTES_8=8`。受信ヘッダーの値をさらに16 bit右シフトしない。

## 電源基板

`Protocol/power_packet`がバイト列、`Device/power_board`がCAN1への送信と受信状態を担当する。

バイト配置・パラメーターIDの正本は[電源指令](../specs/common/orion_can.md#電源指令)、受信する値の配置は[テレメトリー](../specs/common/orion_can.md#テレメトリー)を参照。ここではAPIの対応範囲と受理条件を記載する。

| API／処理 | 対応範囲 |
|---|---|
| `power_board_set_on(bool)` | 電源ON/OFF要求 |
| `power_board_set_param(param, value)` | `power_param_t`で定義する保護パラメーターの設定要求 |
| `power_board_request_reset()` | 電源リセット要求 |
| `power_board_accept()` | 電圧、電流、温度、状態フラグ、最新Faultを受信して保持 |

NaN・無限大・負値と、`power_param_t`の範囲外を送信しない。機器の許容上限や複数パラメーター間の整合はこのAPIで確定せず、上位が運用値を決める。

指令はCAN1へ送る。戻り値は1がドライバでの受理、0が未受理を示す。リセットはハードウェアの送信枠へ直接投入する。

CAN1の受信形式は次のとおり。電源基板FW `F303_boost` の `8950ebc` にある `Core/Src/can.c` の通知と `Core/Src/main.c` の `power_state_flags()` を照合した。

| CAN ID | DLC | 内容 |
|---|---|---|
| `0x215` / `0x216` | 4 / 8 | バッテリー／キャパシター電圧（V、先頭float） |
| `0x234` | 4 / 8 | バッテリー電流（A、先頭float、符号付き） |
| `0x224` | 8 | FET／コイル1／コイル2温度（℃、先頭3 byte） |
| `0x244` | 8 | byte 0のbit 0から順に昇圧PWM、キックPWM、充電許可、電源出力許可、出力ピン |
| `0x000` | 8 | 先頭uint16が100のフレームを電源Faultとして記録。続くuint16がコード、末尾floatが付随値 |

非有限値と負の電圧は状態を更新しない。周期通知は項目ごとに受信時刻を保持し、UARTは2.5秒で期限切れと表示する。最新FaultはMain再起動まで記録する。機体側の低電圧判定は未実装。

共通エラー `0x000` は従来どおりモーターデバッグの停止処理に渡す。電源のON/OFF・保護値設定・リセットは既存の送信APIで扱う。起動時はリセット要求を送信し、電源出力を維持するON周期送信やキッカーの充電・発射指令は今回の診断の対象外。電源リセット要求への個別ACKは電源FWにない。[CAN受信](peripherals.md#can受信)

## UART4デバッグ

Orionの出力項目と4WSで利用できるデータの対応は[デバッグUART出力のOrion比較](design/debug_uart_orion.md)を参照。

`Board/board_debug_uart`を使用する。UART4はPC10 TX／PC11 RX、2,000,000 baud・8N1・flow controlなし・FIFO無効。LPUART1はデバッグに使用しない。

- TXはDMA normal、最大2000 byte。呼出しデータを内部バッファへコピーし、UART送信完了まで保持する。busy・長さ超過・無効引数はfalseで返し、部分送信しない。
- `p()`は容量制限付き整形。整形用とDMA送信用の領域を分け、送信中のデータを上書きしない。IRQからの呼出しは整形せずfalseで返す。浮動小数点printfの有効化は行っていない。
- RXは1 byte割り込み。256 byteの待ちリングへ格納し、満杯時は新規文字を破棄して計数する。エラーのある文字は上位に渡さない。
- UARTエラー後はメイン処理で受信を再設定する。TX DMAエラーでは停止完了後にバッファを再利用する。
- 公開APIはメイン処理専用。IRQから整形・送信APIを呼ばない。

`App/app_debug_uart` は起動時にビルド日時、CAN開始、IMU／OLEDの状態、ページ一覧を出力する。既定ページは `SYSTEM`。ST-Linkの仮想COMポートを2,000,000 baud・8N1、フロー制御なしに設定すると表示を取得できる。先頭行の `VB[V]` はCAN1電源基板のバッテリー電圧、`BT` は起動確認状態（`WAIT`／`RDY`／`FAIL`）、`F` はモーターデバッグのFault有無。全行をANSI制御文字とCRLF込みで200 byte以下に収め、数値は空白で列幅を揃える。

| キー | ページ | 表示 |
|---|---|---|
| `1` | `MOTOR` | `TG` は目標rpm。C610 ID 1～4の `R` は実ローターrpm、`Ir` は帰還電流raw値、`C[mA]` は指令、`A[ms]` は帰還経過時間 |
| `3` | `KICKER` | CAN1電源基板。`VC` はキャパシター電圧、`IB` はバッテリー電流、`FET/L1/L2` は温度。`PWR` 行に `Fl` と状態ビット `Bst/Kick/Chg/En/Out`、最新Faultコード `Err` と付随値 `Val` を表示 |
| `5` | `ODOM` | ICM-20602の識別・取得状態、3軸加速度 `Acc` と角速度 `Gyr` の生値、温度 `H`（0.1℃） |
| `6` | `MOTION` | `STEER` 行の `T/P` は目標／実角度（mrad）。`I` は電流絶対値（mA）、`C` は校正済みフラグ、`S` は軸状態、`H` は温度（0.1℃）、`A` は帰還経過時間（ms） |
| `9` | `SYSTEM` | ADC・ボタン・DIP、CANバス別の `RX/RD/Lost/BO`（受信・受信破棄・FIFO喪失・bus-off）、`TX/TE/TQ/TD`（送信投入・送信エラー・キュー退避・送信破棄）、TIM6制御とOLEDの診断 |
| `9` の次 | `UART RAW` | デバッグUART4の送受信統計とCM4 SPIの実装状況 |
| `0`、`2`、`4`、`7`、`8` | `CMD`、`DRIBBLER`、`MOUSE`、`VEL`、`LATENCY` | Orionのページ順を保持し、データ取得や機体制御の未実装項目を `n/a` と表示 |

数字 `0`～`9` は直接選択、CRまたはLFで次へ、CRLFはまとめて1ページ進む。Backspaceで前へ、DEL（0x7F）で `CMD`、`?` で一覧を再表示する。UART入力はページ選択と一覧表示に使う。`--` は未受信、`stale` は古い電源値、`n/a` は無効・期限切れの軸値または未対応項目、`range` は表示幅外を表す。ボタン欄 `K` は16進表記で `0` が非押下、`FF` が無効。

整形・DMA送信開始はthread modeのmainで行うため、制御IRQが割り込める。元の100 ms位相を維持し、mainの遅延分を連続送信しない。送信中・異常時の回はスキップし、次の100 msで再開する。ステアFWのCAN更新処理中は周期出力を休止する。

## ブザー

`Board/board_buzzer`はTIM1 CH1・PA8を使用し、1～20000 Hzの整数指定を受け付ける。APB2の分周設定からタイマー入力クロックを求め、`f = timer_clock / ((PSC+1) × (ARR+1))`で計算する。整数レジスターへの丸め誤差があり、`board_buzzer_set_hz()`の出力引数で計算上のmHzを確認できる。無効指定はfalseで返す。

170 MHz時の2000 HzはPSC=1・ARR=42499・CCR=21250で正確に表現できる。dutyは約50%。HSI自体の周波数誤差は補正していないため、計算上の精度と実端子での測定値は区別する。

`Device/buzzer`は`buzzer_start(hz, duration_ms)`、`buzzer_stop()`、`buzzer_process()`を提供する。duration=0は連続鳴動、それ以外は経過msで停止する。新しい開始要求で前の鳴動を置き換える。待機は行わず、時間終了の精度はメイン処理の呼出し間隔に依存する。停止はPWMを動かしたままCCR=0と更新イベントでLow出力にする。Board初期化時にPA8をAF6・内部プルダウンへ設定し、PWM開始前や一時停止中も端子が浮くことを避ける。内部プルダウンは生成GPIO_NOPULLからの実行時変更で、`.ioc`と生成コードは変更していない。

2026-10-09のユーザー指定で、Appは周辺機器と制御IRQの開始後に`buzzer_start(2000U, 100U)`を呼び、起動通知を1回鳴らす。mainが100 ms経過を確認して停止するため、鳴動終了にはmainの呼出し遅れが加わる。異常通知パターンや音列は未追加。

## ADCユーザーSW

ユーザー指定により旧HWと同じ抵抗ラダー・方向対応として実装した。ADC1 IN3・PA2のU_BTNだけをregular変換へ登録し、PHOTO_0/1、BATT_Vの取得は未実装とする。

- 12 bit、PCLK/4、サンプル640.5 cycles、4倍oversampling・2 bit右シフト、連続変換。
- 起動時に単端校正を実行し、32サンプルの循環DMAを開始する。16サンプルごとの半完了／完了通知で、その半領域の最新値を公開する。
- 未取得、ADCエラー、最終公開から100 ms超の更新停止はINVALID。エラー時は有効性を失い、今回の実装ではリセットまで復旧しない。
- `user_switch_decode()`は生値からの純粋な判定、`user_switch_read()`は実ADCの有効性を含めた判定。

| 生値 | 判定 | ビット | マスク |
|---|---|---:|---|
| 0～100 | 中央 | 0 | `0x01` |
| 101～500 | 下 | 1 | `0x02` |
| 501～2000 | 右 | 2 | `0x04` |
| 2001～3000 | 上 | 3 | `0x08` |
| 3001～3900 | 左 | 4 | `0x10` |
| 3901～4095 | 押下なし | — | `0x00` |

ADCの方向デコーダーは旧判定と同様、デバウンス・ヒステリシスを含めない。Appがデバッグ操作の安定時間を判定する。DIPとSW_1の用途は[モーターデバッグ](atlas_bringup.md#モーターデバッグの操作)を参照。

## CubeMXとユーザーコードの管理境界

`Core/Src/fdcan.c`のClassic化・タイミング・標準フィルター数、`Core/Src/adc.c`のIN3・連続変換・oversampling・サンプル時間は、対応する`4WS_MainFW.ioc`にも反映した。生成コードの初期化定数は直接更新しており、アプリ呼出しは`main.c`のUSER CODE内に置いた。ルートCMakeで全ユーザーソースを明示登録し、CubeMX管理CMakeは変更していない。

TIM6、DMAの割当・ハンドル連携・NVIC・IRQ入口・HALコールバックは`Application/Board`の実行時初期化が所有する。`.ioc`にはTIM6・DMAチャネル／追加NVICの有効化を登録していないため、`.ioc`だけでは起動後の状態を表さない。TIM6追加に伴う生成コード・`.ioc`の変更はなく、CubeMX再生成後もBoardで設定する。ADCの連続DMA要求は`.ioc`のADC設定に含まれるが、実際のDMA転送開始は`board_user_adc_init()`が行う。

| 資源 | 所有モジュール | NVIC優先度 |
|---|---|---|
| TIM6_DAC | board_control_timer | 2 |
| FDCAN1 IT0 | board_can | 5 |
| FDCAN2 IT0 | board_can | 6 |
| DMA1 Channel1 / ADC1_2 | board_user_adc | 8 |
| UART4 | board_debug_uart | 9 |
| DMA1 Channel2（UART4 TX） | board_debug_uart | 10 |

同じDMA・IRQをCubeMXで追加設定すると二重初期化・IRQシンボル重複になるため、その場合はBoard側から所有権を移す変更も必要。現在の`.ioc`で再生成すれば、ユーザー所有モジュールとUSER CODEの呼出しを維持する構成。CubeMX本体での再生成は今回未実行であり、再生成後はADC/FDCAN定数とIRQ重複を確認する。

## 検証

ホスト検証は実際のドライバソースをHALモックとリンクして実行する。

```powershell
python ./Script/test_drivers.py
# ホスト用コンパイラーを明示する場合
python ./Script/test_drivers.py --cc "C:/Program Files/LLVM/bin/clang.exe"
```

CANの2バス独立性・送信順序・20フレーム満杯・HAL失敗後再試行・受信形式検証、UARTのDMAバッファ寿命・容量制限・RX満杯・エラー復帰、ADCの未取得／更新停止／異常、SWの全境界、全1～20000 Hzの周波数計算、タイマーAPB分周、時刻周回時のブザー停止、電源バイト列と片側送信受理を確認する。生成コードとのHAL API整合はSTM32向けDebug／Releaseビルドで確認する。

HALモックはIRQの実時間競合や電気的な通信を再現しない。実際のUART受信負荷、CAN bus-offやACK、PWM端子波形、SW押下値は未検証。ビルドと試験の結果は[検証記録](records/README.md)に記録する。
