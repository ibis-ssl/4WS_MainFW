# CAN・IMU・OLEDの取得と診断

2026-10-08のユーザー指定により、CANにはOrion_CM4相当のダミー、IMUには旧Orionと同じ機器、I2CにはSH1106の128x64 OLEDが接続されているものとして実装した。旧参照ソースは`C:/Users/hiroyuki/STM32CubeIDE/workspace_1.17.0/G474_Orion_main/Core/Src/can_ibis.c`と`icm20602_spi.c`。実装と実機検証結果を区別し、結果は[開発手順](development.md)へ記録する。

## CAN受信

`Protocol/orion_telemetry`がHALに依存せず復号し、`Device/orion_can`がバス別・ID別の最新値、受信時刻、受信回数を保持する。`app_process()`が電源受信処理と並行して渡す。既知IDは8 byte限定、floatはIEEE754 little-endian、NaN/Infは拒否。未知IDも転送層では受信するが、この解析層には格納しない。

| ID | 格納する旧形式の値 |
|---|---|
| 0x000–001 | エラーID・情報（各uint16）、値（float） |
| 0x200–204 | 回転数rps・エンコーダー角度rad（float各1） |
| 0x210–216 | 電圧V（float、負値を拒否） |
| 0x220–223 | モーター・ドライバー温度（float各1） |
| 0x224 | FET・コイル2個の温度（各uint8） |
| 0x230–234 | 電流（float） |
| 0x240 | ボール検出2個（各uint8） |
| 0x241 | マウスX/Y（int16）、品質（uint16） |
| 0x500–503 | モーターパラメーター応答（float） |

旧コードの機体補正であるエンコーダー角度の符号反転は行わず、パケットの値を維持する。4WSの輪・基板へのID割当は別途確定する。`orion_can_read(bus, id, now, max_age_ms, &sample)`は最新値が指定年齢以内ならtrue。falseの場合も、有効な引数なら`received`で未受信と古い値を区別できる。受信状態はmain専用で、制御IRQへ直接渡していない。タイムアウト時の機体停止判断は今後実装する。アクチュエーター指令は自動送信しない。

## ICM-20602

`Board/board_imu_spi`がSPI1（PA5/6/7）とPA4の`SPI_IMU_CS`を使用する。旧OrionのSPI1 Mode 0・CS Low選択を採用し、WHO_AM_Iの0x12を確認してから設定する。旧コードのコメントにある0x68は期待値として使用しない。[TDKのデータシート](https://invensense.tdk.com/wp-content/uploads/2020/11/DS-000176-ICM-20602-v1.1.pdf)を参照。

- `imu_init()`は状態を初期化し、`imu_process()`がmainで識別、リセット、100 ms待機、設定と読戻し、100 ms待機、取得を進める。
- 加速度±2 g、角速度±1000 dps。DLPF設定1、SMPLRT_DIV=1でセンサーの指定出力周期は500 Hz。mainでは2 ms以上の間隔でdata-readyを確認し、0x3Bから14 byteを一括取得する。
- 加速度はm/s²、角速度はdeg/s、温度は`raw / 326.8 + 25` °C。センサー座標系の値で、軸の機体対応、校正、フィルター、姿勢積分は含めない。
- 最終取得から100 ms超は`imu_get_status()`のvalidをfalseにする。識別・転送・設定読戻し失敗は無効化し、1秒後に再試行。data-readyが1秒超停止した場合も再初期化する。
- SPI転送は2 msの有限タイムアウト、main専用。main遅延時は最新値を取得し、FIFOによる全500サンプルの保存は行わない。実際の取得頻度はOLED・CAN負荷に依存する。
- 現行FSYNCは入力として維持し、旧ソースのGPIO出力操作は移植しない。IMUのINT端子やFSYNCをIRQ入力として使用しない。

`.ioc`と生成SPI1初期値の16分周（10.625 MHz）は変更せず、Boardが動作開始時に32分周（5.3125 MHz）へ再設定する。ICM-20602のSPI上限10 MHzを超えないための実行時設定である。生成コードは変更していない。CubeMX再生成後も`Application/`、ルートCMakeの登録、USER CODE内のApp呼出しを保持すれば適用される。

## SH1106表示

`Board/board_oled_i2c`がI2C1（PB7 SDA / PA15 SCL）を使用。SCLはユーザー提示の回路図・基板のJ18-3に合わせてPA15へ修正した。7 bitアドレス0x3C、0x3Dの順でACKを確認し、応答したアドレスにSH1106初期化を行う。ACKだけではコントローラー型番は識別できないため、SH1106であることはユーザー指定を前提とする。[SH1106データシート](https://www.displayfuture.com/Display/datasheet/controller/SH1106.pdf)に従い、ページ位置指定・内部DC-DC設定を使用する。

- 128x64、8ページ、列オフセット2、5x7英大文字・数字・記号による8行×21文字の診断表示。列オフセットと向きはモジュール依存で、実表示で確認する。
- `oled_begin()`で画面を消去、`oled_text(row, column, text)`で描画、`oled_commit()`で送信開始。送信中は次のbeginを拒否し、バッファを固定する。
- `oled_process()`は1回につきページ位置指定または8 byteのデータ転送を1回だけ行う。1画面は位置指定8回＋データ128回＋表示ON1回。転送の間にIMU取得とCAN回収を挟む。
- 最初の画面送信完了まで表示OFF。2026-10-08の追加指定により、外部プルアップ未実装の基板を内部プルアップで疎通確認する。BoardがPB7/PA15をAF4・open-drain・内部プルアップへ設定し、I2C1のTIMINGRを0xF0F1FFFF（PCLK1=170 MHz専用、約20 kHz）へ変更する。転送タイムアウト25 ms、ACK確認3 ms。IRQからの転送を拒否する。
- 失敗時はready/busyを解除してエラー計数、Appが1秒ごとに再検出する。非接続でもCAN・制御入口・UARTの起動を継続する。

Appは約100 msごとに画面生成を試みる。送信中は更新を省略するため、実際の更新頻度はI2C速度とmain負荷に依存する。表示はCAN受信数、IMU識別と有効性、加速度Z・角速度Zの生値、温度m°C、Orion解析受理数、SW生値。UART4にもIMU/OLED状態を追加した。

内部プルアップは疎通確認用で、波形・立上り時間・通常運用の仕様適合は未検証。外部抵抗を追加した後に通信速度を見直す。`.ioc`・生成コードのGPIO_NOPULLとTIMINGR=0x40621236は維持しており、`board_oled_i2c_init()`が実行時に上書きする。再生成時はBoard側の設定も確認する。

SCLのPA15への変更は`.ioc`のピン一覧・I2C1_SCL割当と、生成`Core/Src/i2c.c`のGPIOAクロック・初期化・解除へ同期して反映した。CubeMX本体による再生成は未実施。再生成後はPB7/PA15のAF4設定とBoardの内部プルアップ設定を照合する。

CANについては、ユーザー確認でトランシーバーのVIO誤配線により現状使用不可。受信処理は実装済みだが、VIO修正後にダミー受信を実機検証する。

## SWDからの診断読取り

`App/app_monitor`の`app_monitor_status`を約100 msごとに更新する。magic=0x34575331、sequenceは更新中が奇数・完成が偶数。CAN受信・破棄・FIFO喪失・bus-off、IMU生値、OLEDフレーム数、SW、制御タイマーの計数を収録する。last_idは受信の都度更新するため、診断読取りは短時間CPUを停止して行う。

書き込んだELFと同じファイルを指定する。例のシリアル番号は実際のST-Linkへ置き換える。

```powershell
python ./Script/read_monitor.py --serial 002D00373033510635393935 --elf build/Debug/4WS_MainFW.elf --output build/hardware/monitor.json
```

PythonはArm nmでシンボル位置を求め、CubeProgrammerのHOTPLUG・1 MHz SWD接続でCPU停止、RAM保存、再開を行う。JSONと同名の`.bin`を保存する。`--nm`、`--programmer`でツール位置を指定可能。診断構造体の版・サイズとsequenceを確認するが、FW全体の一致を保証する検証ではない。CPU停止は周期に影響するので、ジッターや周期精度の測定には使用しない。USB通信が切れた場合はCPU再開も保証できない。
