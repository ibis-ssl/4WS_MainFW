# 基礎ドライバの実装仕様

## 今回の確定事項と状態

2026-09-13のユーザー指定に基づき、Classic CAN転送、旧電源基板通信、UART4デバッグ、Hz指定ブザー、旧HWと同じADCユーザーSWを実装した。モーター・ドリブラー・キッカーの出力API、ESC PWM、ボタンからのアクチュエーター操作は未実装。基板への書き込みと実機確認は行っていない。

`main()`のUSER CODEから`app_init()`、メインループから`app_process()`を呼ぶ。起動時はUART4受信、ADC取得、両CANの受信を開始し、ブザーはCCR=0の停止状態にする。最後にTIM6の500 Hz制御IRQを開始する。UART状態表示は起動後100 msから常時10 Hzで行う。電源ON/OFF・保護値・リセットなどのCAN指令は自動送信しない。初期化失敗は`Error_Handler()`へ伝える。

## 機体制御タイマー（2026-09-14）

`Board/board_control_timer`がTIM6を所有し、登録された`App/app_control_step()`をIRQ内から直接呼ぶ。指定周期は`APP_CONTROL_FREQUENCY_HZ=500`。現在の170 MHzではPSC=169で1 MHzカウンター、ARR=1999で正確な2 msとなる。旧世代のPSC=170・ARR=2000という近似値は転用しない。実時間の精度はHSIの精度に依存する。

TIM6_DAC IRQの優先度は2。CAN・ADC・UARTより先に実行し、mainの処理やログ整形に依存させない。DACは使用せず、この共有IRQはTIM6専用とする。制御演算・出力はまだ未実装で、現在の入口はSW取込みのみ。

通常ドライバのクリティカルセクションはBASEPRIで優先度5以下の緊急度のIRQだけをマスクし、制御IRQを止めない。制御IRQはmain専用APIに割り込んで同じデータを操作してはならない。ADC読取りは二重バッファ公開によって制御IRQからも利用できる。

DWT CYCCNTを有効にし、制御コールバックの最大実行サイクル、TIM6カウンターによる最大入口遅延us、実行回数、周期超過回数を記録する。計測値取得は制御IRQを止めない項目別スナップショット。周期超過または次の更新発生を検出した場合は計数し、溜まった更新をクリアして連続再実行を避ける。入口遅延は1周期内の値であり、長い全IRQ禁止による全欠落周期数を表すものではない。実機での最悪実行時間・ジッターは未測定で、演算を追加する際は2 ms以内に収まることを確認する。

## CAN転送

`Application/Board/board_can.c/.h`が両バスを担当する。

| 項目 | 実装 |
|---|---|
| モード | Classic CAN、Normal、自動再送有効、BRSなし |
| nominal設定 | 170 MHz / (10 × (1 + 14 + 2)) = 1 Mbit/s、SJW=1、サンプル点約88.2% |
| 送信 | 標準ID 0～0x7FF、8 byte固定、データをコピーして受理 |
| 受信 | 標準ID、Classic、data frame、DLC 0～8を検証。データ・長さ・受信msを公開 |
| フィルター | 各バス標準マスク1個で全標準IDをFIFO0へ。拡張IDとremote frameはglobal filterで拒否 |
| TX待ちFIFO | 各バス20フレーム。FIFO順を保持し、満杯時は新規要求をfalseで拒否して破棄数を加算 |
| RX待ちFIFO | 各バス20フレーム。満杯時は新規受信を破棄して記録 |
| 処理上限 | RXは1回3フレームまで。IRQ後に残った分はメイン側からも回収 |
| 失敗 | HAL送信登録失敗時は先頭を保持して次の処理で再試行。bus-off状態変化は計数し、自動復帰は行わない |

`board_can_send()`のtrueはドライバへの受理であり、バス上の送信完了・ACK・相手機器の処理完了ではない。ACKが得られない場合はハードウェア再送と有限FIFOにより、後続要求が拒否され得る。`board_can_process()`はメイン処理で継続して呼ぶ。公開APIはメイン処理専用で、IRQと共有するFIFO・統計の更新は短いクリティカルセクションで保護する。

使用中のHALではDLC定数は0～15の符号値であり、8 byteは`FDCAN_DLC_BYTES_8=8`。受信ヘッダーの値をさらに16 bit右シフトしない。

## 電源基板

`Protocol/power_packet`がバイト列、`Device/power_board`が両バスへの送信と受信状態を担当する。

| API／受信 | 内容 |
|---|---|
| `power_board_set_on(bool)` | 0x010、byte0=0、byte1=ON/OFF、残り0 |
| `power_board_set_param(param, value)` | 0x010、byte0=パラメーター、byte1～4=32 bit float little-endian、残り0 |
| `power_board_request_reset()` | 0x001、先頭0x5A・0xA5、残り0 |
| 0x215／0x216受信 | 電池電圧／キャパシター電圧、先頭float |
| 0x224受信 | byte0=FET温度、byte1/2=コイル温度 |

パラメーターは旧ID 1～5（最低電圧、最高電圧、最大電流、FET温度上限、コイル温度上限）。NaN・無限大・負値は送信しない。機器の許容上限や複数パラメーター間の整合はこのAPIで確定せず、上位が運用値を決める。

旧仕様どおり両バスへ送る。戻り値のbit0がCAN1、bit1がCAN2の受理を表し、3で両方受理、1/2で片側だけ受理、0で未受理。片側成功時の再送判断は呼出し側が行う。

受信長は旧仕様の8 byteに限定し、電圧の非有限値・負値は更新しない。バス別に受信済みフラグと各項目の受信時刻を保持する。受信済みフラグは通信の現在の健全性を保証しない。タイムアウト・低電圧保護の機体側判断は未実装。

共通エラー0x000/001の発生元分類、電流IDの電源基板への対応付け、アクチュエーター指令0x100～104／0x110／0x300～304／0x310は実装対象に含めない。現在のAppは電源テレメトリー以外のCAN受信を消費して破棄する。将来追加する機器はAppの受信振分けへ登録する。

## UART4デバッグ

`Board/board_debug_uart`を使用する。UART4はPC10 TX／PC11 RX、2,000,000 baud・8N1・flow controlなし・FIFO無効。LPUART1はデバッグに使用しない。

- TXはDMA normal、最大2000 byte。呼出しデータを内部バッファへコピーし、UART送信完了まで保持する。busy・長さ超過・無効引数はfalseで返し、部分送信しない。
- `p()`は容量制限付き整形。整形用とDMA送信用の領域を分け、送信中のデータを上書きしない。IRQからの呼出しは整形せずfalseで返す。浮動小数点printfの有効化は行っていない。
- RXは1 byte割り込み。256 byteの待ちリングへ格納し、満杯時は新規文字を破棄して計数する。エラーのある文字は上位に渡さない。
- UARTエラー後はメイン処理で受信を再設定する。TX DMAエラーでは停止完了後にバッファを再利用する。
- 公開APIはメイン処理専用。IRQから整形・送信APIを呼ばない。

起動後、入力操作なしでmainから100 msごとにADC有効性・生値・SW判定値・ADCエラー数・両CANの受信数／RX破棄数／bus-off状態変化数と制御タイマーの計測値を出力する。SW判定値は0=INVALID、1=NONE、2=中央、3=後、4=右、5=前、6=左。UART入力では増速しない。旧ゲイン操作やアクチュエーター操作コマンドは未実装。

整形・DMA送信開始はthread modeのmainで行うため、制御IRQが割り込める。元の100 ms位相を維持し、mainの遅延分を連続送信しない。送信中・異常時の回はスキップし、次の100 msで再開する。通常時は10 Hzだが、main停止やUART障害時にも10 Hz送信完了を保証する方式ではない。

## ブザー

`Board/board_buzzer`はTIM1 CH1・PA8を使用し、1～20000 Hzの整数指定を受け付ける。APB2の分周設定からタイマー入力クロックを求め、`f = timer_clock / ((PSC+1) × (ARR+1))`で計算する。整数レジスターへの丸め誤差があり、`board_buzzer_set_hz()`の出力引数で計算上のmHzを確認できる。無効指定はfalseで返す。

170 MHz時の2000 HzはPSC=1・ARR=42499・CCR=21250で正確に表現できる。dutyは約50%。HSI自体の周波数誤差は補正していないため、計算上の精度と実端子での測定値は区別する。

`Device/buzzer`は`buzzer_start(hz, duration_ms)`、`buzzer_stop()`、`buzzer_process()`を提供する。duration=0は連続鳴動、それ以外は経過msで停止する。新しい開始要求で前の鳴動を置き換える。待機は行わず、時間終了の精度はメイン処理の呼出し間隔に依存する。停止はPWMをCCR=0にし、起動時も音を鳴らさない。起動音列・異常通知パターンは今回未追加。

## ADCユーザーSW

ユーザー指定により旧HWと同じ抵抗ラダー・方向対応として実装した。ADC1 IN3・PA2のU_BTNだけをregular変換へ登録し、PHOTO_0/1、BATT_Vの取得は未実装とする。

- 12 bit、PCLK/4、サンプル640.5 cycles、4倍oversampling・2 bit右シフト、連続変換。
- 起動時に単端校正を実行し、32サンプルの循環DMAを開始する。16サンプルごとの半完了／完了通知で、その半領域の最新値を公開する。
- 未取得、ADCエラー、最終公開から100 ms超の更新停止はINVALID。エラー時は有効性を失い、今回の実装ではリセットまで復旧しない。
- `user_switch_decode()`は生値からの純粋な判定、`user_switch_read()`は実ADCの有効性を含めた判定。

| 生値 | 判定 |
|---|---|
| 0～100 | 中央 |
| 101～500 | 後 |
| 501～2000 | 右 |
| 2001～3000 | 前 |
| 3001～3900 | 左 |
| 3901～4095 | 押下なし |

旧判定と同様、デバウンス・ヒステリシスは含めない。入力未取得を0として押下扱いしない。DIPの意味付けとSW_90／SW_2の操作仕様は未確定のため、既存の生GPIO取得APIのみとする。

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

HALモックはIRQの実時間競合や電気的な通信を再現しない。実際のUART受信負荷、CAN bus-offやACK、PWM端子波形、SW押下値は未検証。ビルドと試験の結果は[開発手順](development.md)に記録する。
