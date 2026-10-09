# 開発手順

プロジェクトルートをカレントディレクトリにしたWindows PowerShellで実行します。ソース、文書、スクリプトはUTF-8で保存します。

## 必要なツール

- CMake 3.22以降とNinja
- Arm GNU Toolchain（`arm-none-eabi-gcc`、`arm-none-eabi-objcopy`など）
- 書き込み時はSTM32CubeProgrammer CLIとST-Link
- CubeMX設定を変更する場合はSTM32CubeMX

`Script/build.ps1`は、個別指定した実行ファイル、`-CubeCLTPath`で指定したSTM32CubeCLT、PATH、標準的な場所にある最新のSTM32CubeCLT、の順にツールを検索します。自動検出できない場合は`-CMakePath`、`-NinjaPath`、`-ToolchainBinPath`または`-CubeCLTPath`で指定します。

## ビルド

```powershell
# Debug
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Debug

# Release
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Release

# クリーン再ビルド
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Debug -Rebuild
```

ツールの場所を明示する例です。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 `
  -Configuration Release `
  -CubeCLTPath "C:\ST\STM32CubeCLT_1.21.0"
```

`-Configuration`は`Debug`または`Release`で、既定は`Debug`です。Debugは`-O0 -g3`、Releaseは`-Os -g0`を使用します。`-Rebuild`はCMake configureの後、`--clean-first`で既存成果物を消してからビルドします。

成果物は次の場所へ生成されます。

| 成果物 | 用途 |
|---|---|
| `build/<Configuration>/4WS_MainFW.elf` | デバッグ情報を含む実行イメージ |
| `build/<Configuration>/4WS_MainFW.bin` | バイナリー書き込み用イメージ |
| `build/<Configuration>/4WS_MainFW.hex` | Intel HEX書き込み用イメージ |
| `build/<Configuration>/4WS_MainFW.map` | セクション配置とサイズの確認 |
| `build/<Configuration>/compile_commands.json` | エディターや静的解析用コンパイル情報 |

## ST-Link書き込み

接続中のST-Linkを確認します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/flash.ps1 -List
```

Release成果物を書き込み、照合してリセットする例です。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/flash.ps1 `
  -Configuration Release `
  -Serial STLINK_SERIAL
```

ビルドしてから書き込む場合は次を使用します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build_and_flash.ps1 `
  -Configuration Release `
  -Serial STLINK_SERIAL
```

STM32CubeProgrammer CLIまたはSTM32CubeCLTの場所を自動検出できない場合は`-ProgrammerPath`または`-CubeCLTPath`で指定します。接続だけを確認する場合は`-ConnectOnly`、実行コマンドを表示して書き込まない場合は`-DryRun`、書き込み後のリセットを省く場合は`-NoReset`を指定します。既定では書き込み後に照合し、`-NoVerify`で照合を省略できます。`build_and_flash.ps1 -DryRun`はビルドを実行した後、書き込みコマンドだけを表示します。

このプロジェクトは単一アプリケーションをFlash先頭`0x08000000`へ配置します。`flash.ps1`は`4WS_MainFW.elf`を書き込み対象とします。実機書き込みは接続先と構成を確認してから実行してください。

## VS Code

コードブラウズはMicrosoft C/C++拡張（`ms-vscode.cpptools`）を使用する。このワークスペースでは`C_Cpp.intelliSenseEngine=default`にし、STM32Cube clangdを`stm32cube-ide-clangd.enable=false`にする。ユーザー全体のIntelliSense無効設定は変更せず、プロジェクト内で上書きする。

`.vscode/settings.json`の`C_Cpp.default.compileCommands`は`build/Debug/compile_commands.json`を参照する。ソースごとのインクルード・マクロ・Cortex-M4/FPUオプションは実際のビルド情報から取得する。先に`Build: Debug`タスク、または次を実行してDBを生成・更新する。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/build.ps1 -Configuration Debug
```

DBに直接載らないヘッダー向けにArm GCCのcompilerPath、`windows-gcc-arm`、gnu11、DEBUG/USE_HAL_DRIVER/STM32G474xxと明示インクルードを設定する。標準ヘッダーのパスはArm GCCへ問い合わせる。タグ索引はApplication/Core/Driversを対象とし、TestsのHALモックを全体索引へ混ぜない。Tests用のホストコンパイルDBは未設定。

VS Codeでこのフォルダーを開き、F12で定義、Shift+F12で参照、Ctrl+Spaceで補完を使用する。設定変更後に解析が更新されない場合はコマンドパレットの`Developer: Reload Window`を実行する。`C/C++: Log Diagnostics`でArm GCC、windows-gcc-arm、Debugのcompile_commandsとSTM32G474xxを確認できる。必要なら`C/C++: Reset IntelliSense Database`で索引を再作成する。

別PCやCubeCLT更新時は、`STM32VSCodeExtension.cubeCLT.path`、`C_Cpp.default.compilerPath`、`cmake.cmakePath`、`cmake.environment`のPATHを実際のインストール先へ変更してDebugをビルドする。CMake providerは使用せずDBへ直接接続するので、CMake ToolsでReleaseを選んでもコード解析はDebugのまま。[C/C++設定の公式説明](https://code.visualstudio.com/docs/cpp/customize-cpp-settings)を参照。


`flash.ps1`と`build_and_flash.ps1`は`-Frequency`でSWD速度をkHz指定できます。既定は実機確認した1000 kHz、引数範囲は100～24000で、プローブが対応する速度へ調整される場合があります。

`.vscode/tasks.json`には`Build: Debug`、`Build: Release`、`Build: Rebuild Debug`、`Flash: Build and Flash Debug`、`Flash: Dry Run Debug`、`Flash: List ST-LINK Probes`があります。タスクは`Script/`のPowerShellスクリプトを呼び出すため、コマンドラインと同じ手順になります。

拡張の有効状態と競合の確認は[拡張機能の確認](vscode.md)を参照してください。STM32Cube CMakeのIntelliSense自動設定は停止し、CMake ToolsのCMakeとPATHをCubeCLT 1.21へ揃えました。

デバッグにはVS CodeのCortex-Debug拡張を使用します。2026-10-10の確認時はCortex-Debugが全体で無効のため、次の構成を使うにはこのワークスペースで有効化する必要があります。`STM32G474: Build & Debug (ST-LINK)`はDebugビルド後に起動し、`STM32G474: Attach (ST-LINK)`は実行中のターゲットへ接続します。どちらもST-Linkと`build/Debug/4WS_MainFW.elf`を使用します。`.vscode/settings.json`の`STM32VSCodeExtension.cubeCLT.path`は、実際にインストールしたSTM32CubeCLTのルートへ変更してください。

## CubeMXで再生成する場合

1. `4WS_MainFW.ioc`をSTM32CubeMXで開きます。
2. Toolchain/IDEがCMakeであることを確認してコードを生成します。
3. `USER CODE BEGIN`／`USER CODE END`内のユーザーコードが保持されたことを確認します。
4. ルート`CMakeLists.txt`のbin／hex生成設定と、`Script/`、`doc/`、`.vscode/`が保持されたことを確認します。
5. DebugとReleaseを`-Rebuild`付きでビルドします。
6. Git管理下では`git diff`を確認し、意図しない生成差分や設定変更がないか確認します。

CubeMXが管理するファイルを直接変更する場合は、再生成で消える箇所かどうかを確認します。ユーザーコードは[コード階層](architecture.md)に従って`Application/`へ配置し、起動処理などとの接続にはUSER CODEブロックを使用します。追加ファイルはルート`CMakeLists.txt`から登録します。再生成後は`Application/`とそのビルド登録、および生成ピン定義との整合も確認します。

## 検証範囲

基礎ドライバのホスト検証は、Python 3とホスト用Clangを用いて次を実行します。Arm用GCCはこのホスト検証には使用しません。

```powershell
python ./Script/test_drivers.py
# コンパイラーの場所を指定する場合
python ./Script/test_drivers.py --cc "C:/Program Files/LLVM/bin/clang.exe"
```

実ドライバをHALモックに接続したテストです。対象と限界は[基礎ドライバ](drivers.md)を参照してください。CubeMX再生成時は、同文書のDMA・NVIC・IRQ所有表に従ってBoardとの二重設定を避け、ADC/FDCANの生成初期値との整合も確認します。

ビルド成功はコンパイル、リンク、bin／hex生成までを確認するものです。FDCAN、UART、SPI、I2C、ADC、PWMの電気的動作や接続機器との通信は保証しません。実機確認を行った場合は、基板、接続条件、FW成果物、確認内容を記録してください。

## 検証記録

### VS Codeコードブラウズの修正（2026-10-10）

- ユーザー設定でC_Cpp.intelliSenseEngine=disabled、既存のSTM32Cube clangdログで言語サーバー起動失敗を確認した。ワークスペースでC/C++解析を有効化し、STM32Cube clangdを無効化した。全体のユーザー設定と他プロジェクトは変更していない。
- 実ビルドのDebugコンパイルDB、Arm GCC 14.3.1、Cortex-M4/ハードFPU、gnu11とSTM32G474xxを設定。ヘッダー向けのフォールバックと、実機コードに限定したタグ索引も設定した。
- Debugビルド成功。設定JSONと全インクルードパス、GCC実行・標準ヘッダー検索、コンパイルDBの全ソース存在を照合した。開いている4WS_MainFWでboard_oled_i2c.cを開いた後、cpptools本体・Tag Parser・IntelliSense解析プロセスの起動を確認した。F12・補完のUI操作そのものは未検証。
- ファームウェア・CMake・生成コード・`.ioc`は変更せず、実機書込みは行っていない。


### LCD描画・通信時間の実測（2026-10-10）

- 対象はSTM32G474RET6、HCLK=170 MHz（実機のSystemCoreClockを読取り）、I2C1 TIMINGR=0x60400D28、PB7/PA15内部プルアップ、SH1106 0x3C、128x64・1,024 byte全画面・137トランザクション。Debug FW（-O0）、通常の500 Hz制御・IMU取得・CAN受信・200 ms LED循環・100 ms診断画面更新を継続した。
- 描画をbegin→commit、HAL送信をHAL_I2C_Master_Transmitの呼出し前後、OLED処理を各oled_processの実行区間、全画面転送をcommit→完了でDWT計測。最初の成功64画面を保存してから読み取った。別起動でも64画面を測定した。

| 区間 | 1回目平均 | 2回目平均 | 2回目最小～最大 |
| --- | --- | --- | --- |
| 描画（画面消去・文字列整形・入力取得含む） | 0.305 ms | 0.305 ms | 0.278～0.340 ms |
| HAL I2C同期送信137回の合計 | 31.034 ms | 31.041 ms | 30.964～31.107 ms |
| oled_process実行時間の合計（HAL送信含む） | 31.350 ms | 31.358 ms | 31.286～31.431 ms |
| commitから全画面転送完了まで | 37.273 ms | 37.293 ms | 35.942～37.621 ms |
| 描画開始から全画面転送完了まで | 37.578 ms | 37.599 ms | 36.229～37.946 ms |
| 描画とOLED処理の合計からHAL送信を除いた差分 | 0.620 ms | 0.623 ms | 0.574～0.683 ms |

- 2回目は描画とOLED処理の合計が平均31.664 ms（10 Hz更新で1秒あたり約317 ms）、転送間の別処理・計測記帳等の間隔が平均5.935 ms。単一HAL送信の最大は0.260 msだった。各区間は割込みを含む経過時間であり、厳密なCPU専有時間ではない。同期HALの待機が処理時間の大半を占める。
- 2回目の計測と同時期のUARTで約10秒間に100画面完成、OLED/IMUエラー0、制御周期超過0、CAN1/2の受信継続とRX破棄・bus-off 0を確認した。記録は`build/hardware/oled-timing-debug-1.json`と`oled-timing-debug-2.json`。各ファイルに全64画面の生サイクル値と集計を保存。
- Debug／Releaseビルドと`python ./Script/test_drivers.py`成功。ST-Link SN `002D00373033510635393935`、SWD 1 MHzで計測対応Debug FWを書込み・照合・リセット済み。実機へはDebugを残した。生成コードと`.ioc`は変更していない。
- 純粋なSCL波形の時間、LCD内部走査・見た目の反映時間、Releaseの実機時間は未測定。計測処理のオーバーヘッドを完全には除去していない。100 msの画面生成待ち時間は表の区間外。

計測対応FWの書込み・リセット後、64画面が保存される約7秒以上待って次を実行する。再計測するときは先にMCUをリセットする。

```powershell
python ./Script/read_oled_timing.py --serial 002D00373033510635393935 --output build/hardware/oled-timing.json
```


### 動作中LEDの繰り返し点灯（2026-10-10）

- 起動時からLED_0→LED_1→LED_2→LED_3→赤→緑→青を1個ずつ各200 ms点灯し、青の次はLED_0へ戻して繰り返す。全High点灯の指定を維持。mainで時刻を判定し、遅延時は連続切替を追い掛けず次のLEDの点灯時間を確保する。
- Appのみ変更し、GPIO・生成コード・`.ioc`は変更していない。READMEと基礎ドライバ文書も更新した。
- Debugビルドと`python ./Script/test_drivers.py`が成功。ST-Link SN `002D00373033510635393935`、SWD 1 MHzでDebug FWを書込み・照合・リセット成功。
- CubeProgrammerの逐次読取りは約1.4秒/回で200 msの切替を捉えられなかった。ST-Link GDB serverでCPU実行を再開し、約4秒間GPIO出力を連続読取りして21状態の変化を確認。0/1/2/3/R/G/Bの順序、青→0の繰り返し、各観測で1個のみHighを確認した。別ポートの逐次読取りが切替をまたぐ場合は再読取りとの不一致を除外した。
- 観測は`build/hardware/led-repeat-fast.json`。非同期読取りなので厳密な200 msの波形測定ではない。点灯の目視と長時間の動作確認は未実施。
- デバッガー切断後もUART4で動作継続を確認。約10秒間でOLED完成画面が101増え、OLED/IMUエラー0、制御周期超過0、CAN1受信の継続を確認した。デバッガー接続時のCPU停止による制御入口遅延は計数に残っている。


### OLED I2C速度の段階試験（2026-10-10）

- 対象はSTM32G474RET6、ST-Link SN `002D00373033510635393935`、SWD 1 MHz。OLEDはPB7 SDA / PA15 SCL、内部プルアップ、0x3C。Debug FWを各段階で書込み・照合・リセットし、UART4（COM167、2,000,000 baud）で計数を読み取った。
- PCLK1=170 MHz、アナログフィルター有効、DNF=0。CubeMX 6.15の`I2cTimingTraitement`で立上り100 ns・立下り10 nsを仮定してタイミング値を算出した。内部プルアップの実測波形に基づく値ではない。

| 設定速度 | TIMINGR | 起動後の有効診断区間 | 完成画面の増分 | 更新回数/秒 | OLEDエラー | IMUエラー | 制御周期超過 |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 50 kHz | 0x6080DFFF | 14.602秒 | 49 | 3.36 | 0 | 0 | 0 |
| 100 kHz | 0x30E0A7F6 | 14.600秒 | 73 | 5.00 | 0 | 0 | 0 |
| 200 kHz | 0x20B0709C | 14.700秒 | 147 | 10.00 | 0 | 0 | 0 |
| 400 kHz | 0x60400D28 | 59.700秒 | 597 | 10.00 | 0 | 0 | 0 |

- 各段階の記録時間は15/15/15/60秒。表はIMU取得が有効になってからの最初と最後の診断値の差分で、制御呼出し数÷500から時間を算出した。400 kHz設定のIMU取得は約490回/秒、制御最大実行時間は455 cycles。全段階でCAN1受信が継続し、RX破棄・bus-offは0。今回CAN2受信は0なので負荷時の両バス動作は未確認。
- 実機I2C1 TIMINGR=0x60400D28をHOTPLUG読取りで確認した。現在の設定として400 kHzを採用し、`.ioc`・生成`Core/Src/i2c.c`・Boardの値を同期した。GPIO_NOPULLから内部プルアップへの変更は従来どおりBoardの実行時設定。CubeMX本体の再生成は未実施で、再生成後は設定を照合する。
- `python ./Script/test_drivers.py`とDebug／Releaseビルドが成功。最終Debug FWを再書込み・照合・リセットし、さらに10秒間で100画面の完成、OLED/IMUエラー0、制御周期超過0を確認した（`build/hardware/i2c-400-final-uart.txt`）。
- 記録は`build/hardware/i2c-<速度>-uart.txt`、`i2c-<速度>-result.json`。全画面の転送完了計数を確認したが、実表示の目視、スイッチ操作に対する応答時間、SCL実周波数・立上り/立下り時間、長時間・温度変動の動作は未確認。通信成功を波形の規格適合とは扱わない。


### スイッチ内部プルアップと1/0表示（2026-10-10）

- `hardware.md`の回路仕様に従い、外付けプルアップのないSW8～10（PC13/14/15）に内部プルアップを設定。外付け10 kΩのあるDIP、抵抗ラダーのU_BTN、NRST、IMU_FSYNCは設定を変更しない。
- `.ioc`のGPIO_PuPdをGPIO_PULLUPへ変更し、生成`gpio.c`のSW群とIMU_FSYNCを分離。PC14をSW_1として`.ioc`・生成`main.h`・Board入力IDへ追加。設定と生成コードを照合した。CubeMX本体による再生成は未実施で、再生成後は同じピン割当とpull設定を確認する。
- OLED下3行をDIP 0123、SW 012、BTN LTRBCの1/0表示へ変更。GPIO生レベルはHigh=1/Low=0。ADC方向判定は押下0・非押下1、無効時は?。同時押下、デバウンス、モード割当は追加していない。NRSTは表示対象外。
- Debug／Releaseビルド、既存HALモック試験成功。Appの入力モックは開放Highとして扱うため、実接点の押下・変更を検証するものではない。Debug Flash 61,740 byte、Release Flash 35,788 byte、RAMは両構成12,784 byte。
- ST-Link SN `002D00373033510635393935`、SWD 1 MHzでDebug FWを書込み・照合・リセット成功。GPIOC PUPDR=0x54000000でPC13～15がpull-up、GPIO入力で各SW=Highを確認。
- 実機の画面バッファを取得・字形を復号し、`DIP 0123 0111`、`SW  012  111`、`BTN LTRBC 11111`を確認。`build/hardware/switch-display.bin`、`switch-display.txt`に保存。`switch-monitor.json`でOLEDアドレス0x3C、完成39フレーム・エラー0、IMUエラー0、制御周期超過0を確認。
- スイッチの実押下・ロータリー全位置・画面の目視は未確認。SWD診断はCPU停止を伴うため、診断中のCAN受信喪失・破棄は通常運転の評価と区別する。

### 起動ブザーと追加仕様（2026-10-09）

- ユーザー指定でBATT_Vは10 kΩ/1 kΩ分圧の11倍換算、CM4とのSPIではSTM32側slave・CM4側masterと記録。BATT_V取得、基準電圧校正、CM4通信処理は未実装。SPI生成設定は変更していない。
- 周辺機器と500 Hz制御IRQの開始後に2 kHz・100 msの起動音を追加。待機はせずmainの`buzzer_process()`で停止する。停止はPWM有効・CCR=0でLow出力。PA8の内部プルダウンをBoardが適用し、PWMの一時停止中も浮きを抑える。生成コード・`.ioc`は変更していない。
- Debug／Releaseビルドと既存HALモック試験成功。App試験に、起動時の2 kHz設定、99 msまではCCR非ゼロ、100 ms以降はCCR=0の確認を追加した。
- Debug Flash 61,204 byte、Release Flash 35,500 byte、RAMは両構成12,784 byte。同じST-Link SN `002D00373033510635393935`・SWD 1 MHzでDebug FWを書込み・照合・リセット成功。
- 実機ではTIM1 PSC=1・ARR=42499（170 MHz設定で計算上2 kHz）、停止後CCR1=0、CC1E=1、CEN=1を確認。PA8のプルダウン設定とGPIO入力Lowも確認した。
- リセット後のSWDポーリングでは初回取得が約1397 msまで遅れ、100 msの鳴動中は捕捉できなかった。`build/hardware/startup-buzzer.json`に停止状態を保存。音の聴取、実PWM周波数、厳密な鳴動時間、電源投入直後からの波形は未測定。100 ms停止にはmainの呼出し遅れが加わる。

### CAN VIO修正後の実機受信確認（2026-10-09）

- ユーザーがCANトランシーバーのVIOを修正。接続中のOrion相当ダミーからの受信を、現在のDebug FWで確認した。今回はFWの変更・再書込み・アクチュエーター指令送信は行っていない。
- 対象: STLINK-V3MINIE SN `002D00373033510635393935`、SWD 1 MHz、MCU ID 0x469、電圧3.27 V。参照ELF `build/Debug/4WS_MainFW.elf`。BIN SHA256 `ECA253C45EB7DAC03077C2A3CD5DF9B0E62348AFDD54AC2845199131009775A0`、ソースHEAD `e132551`。
- 最初の診断値（uptime 70,163 ms）でCAN1受信142,836・解析142,836、CAN2受信296,034・解析252,388を確認。両バスのbus-offは0。CAN2のソフトウェアRX破棄45,780が既に発生していた。
- CPUを停止しないCOM167・2 Mbps UART観測を約10秒行い、102行を保存。CAN1受信414,182→435,274（+21,092）、CAN2受信854,964→898,406（+43,442）。CAN1破棄0、CAN2破棄132,951→139,727（+6,776）。両bus-off・制御周期超過は0のまま。制御呼出し+5,049を2 ms換算すると約10.10秒、受理速度は概算CAN1約2,089、CAN2約4,302フレーム/秒。CAN2破棄は概算約671フレーム/秒で、SWD停止だけによるものではない。
- CAN1の解析済みIDは0x202/203、0x212/213、0x222/223、0x232/233、0x502/503。CAN2は0x200/201/204、0x210/211/214、0x220/221/224、0x230/231/240、0x500/501を確認。例: CAN1の0x202は0 rps・3.96847 rad、0x212は23.6092 V、0x222は18/35。CAN2の0x200は0 rps・2.38653 rad、0x210は23.7067 V、0x224は23/24/24。これらは受信・復号値であり、単位・校正精度を実測保証したものではない。
- ドライバ統計のRAM取得では両バスrx_invalid=0・rx_errors=0・bus-off=0を確認。ハードウェアFIFO喪失は各1で、途中のSWD診断でCPU停止を伴っているため通常運転の結果とは区別する。受信キューの満杯はソフトウェアの取りこぼしであり、20フレームの容量とmain回収・OLED同期転送などの負荷対策が残る。原因を単一箇所に確定してはいない。
- 受信キューの生フレームでは0x215/0x216のDLC=4を確認した。現在の電源・Orion解析は8 byte限定なので、この電圧フレームはCAN転送層で受信しても機器状態へ反映されない。0x244（DLC=8）も受信したが、現在の解析対象外。CAN2の受信数と解析受理数の差を通信不良だけとみなさず、これらの形式差も区別する。4 byte電源パケット対応は今回実施していない。
- 保存先: `build/hardware/can-vio-fixed-1.json`、`can-vio-uart-10s.txt`、`can-vio-uart-summary.json`、`can-vio-states.bin`、`can-vio-samples.bin`、`can-vio-decoded.json`。SWD取得後はCPUを再開した。CAN送信・通信断復帰・bus-off復帰・波形/終端/通信精度は未検証。文書のみ更新したためビルドは実行していない。

### LCD I2C SCLをPA15へ修正（2026-10-08）

- ユーザー提示の回路図・基板対応に従い、LCD用J18-3のSCLをPB8からPA15へ修正。`.ioc`のピン一覧とI2C1_SCL割当、`Core/Src/i2c.c`のGPIOAクロック有効化・AF4初期化・解除を同期した。SDAはPB7を維持。Boardの内部プルアップもPB7/PA15へ変更。
- `.ioc`と生成コードのピン割当は一致。生成部分の直接変更なのでCubeMX再生成時にPA15 SCL・PB7 SDAが出力されることを確認する。CubeMX本体の再生成は未実施。内部プルアップと約20 kHzは引き続きBoardで実行時に適用する。
- Debug／Releaseビルド、既存HALモック試験成功。Debug Flash 59,900 byte、Release Flash 34,908 byte、RAMは両構成12,784 byte。
- ST-Link SN `002D00373033510635393935`、SWD 1 MHzでDebug FWを書込み・照合・リセット成功。GPIOA AFRHのPA15=AF4、PA15の内部プルアップ、GPIOB PB7=AF4・内部プルアップを実機レジスターで確認。PB8はI2Cから解放した。
- `build/hardware/oled-pa15-1.json`で起動後5,822 msにOLEDアドレス0x3C、ready=1、完成フレーム9、エラー0を確認。IMU valid=1・エラー0、制御周期超過0。ACK・転送完了を確認したもので、画面の見え方とI2C波形・立上り時間は未検証。
- `build/hardware/oled-pa15-2.json`でも起動後70,725 msに完成フレーム115・エラー0を確認。IMU取得16,409回・エラー0、制御周期超過0で、表示転送中も動作が継続した。
- 過去のPB8でのLow/BUSY観測は、誤ったSCL割当での結果であり、J18側のSCL状態を示さない。PA15へ修正した構成ではOLEDの疎通が成立した。

### 起動LEDシーケンス（2026-10-08）

- ユーザー指定でLED_0/1/2/3/R/G/BはすべてHigh点灯。Appに1個ずつ各200 ms、0→1→2→3→R→G→B→全消灯の起動表示を追加した。mainの経過時刻判定で進め、HAL_Delayは使用しない。
- Debug／Releaseビルドと既存HALモック試験成功。LEDの試験側はGPIO APIの呼出しを受理する代替で、点灯の電気的検証は含まない。
- Debug: Flash 59,844 byte、RAM 12,784 byte。Release: Flash 34,864 byte、RAM 12,784 byte。
- ST-Link SN `002D00373033510635393935`、SWD 1 MHzでDebug FWを書込み・照合・リセット成功。
- CPUを停止せずGPIOA/B/CのODRをSWDで取得し、起動後約85/247/413/660/854/1032/1210 msで0/1/2/3/R/G/BのHigh出力を観測、約1453 msで全LED出力Lowを確認。観測時刻には読取り間隔と通信遅延を含む。`build/hardware/led-sequence.json`に保存。
- 実GPIOの出力順を確認したもので、LEDの光学的な点灯・明るさ・厳密な200 ms波形は別途確認する。生成コード・`.ioc`は変更していない。

### 内部プルアップでのOLED疎通確認（2026-10-08）

- ユーザーからCANトランシーバーのVIO誤配線により現状使用不可と確認。CANダミー受信の実機検証はVIO修正後とする。
- 外部I2Cプルアップ未実装との指定に基づき、BoardでPB7/PB8の内部プルアップと約20 kHzの低速設定を適用。生成コード・`.ioc`の初期値は変更せず、実行時設定の差分を[CAN・IMU・OLED](peripherals.md)へ記載。
- OLEDの画面データを8 byte単位へ変更し、初期コマンドを含む低速転送に合わせてタイムアウトを25 msへ変更。HALモック試験成功、Debug／Releaseビルド成功。Debug Flash 59,468 byte、Release Flash 34,632 byte、RAMは両構成12,776 byte。
- 同じST-Link・対象MCUにDebug FWを書込み・照合・リセット成功。GPIOB PUPDR=0x00014100（PB7/PB8ともpull-up）、I2C1 TIMINGR=0xF0F1FFFFを実機読取りで確認。
- `build/hardware/oled-pullup-1.json`、`oled-pullup-2.json`に診断値を保存。uptime 6,404→45,204 msでIMU取得数3,042→22,146、IMUエラー0、制御周期超過0。OLEDは0x3C/3D未検出、完成フレーム0、初期化失敗7→46。内部プルアップだけでは今回の疎通は成立しなかった。
- 切り分けのためSWDでCPU停止、I2C1のPEを解除し、PB7/PB8を一時的に入力へ変更。内部プルアップを保持した状態でGPIOB IDR=0x000014FF、PB7 SDAはHigh、PB8 SCLはLowを確認。MODERとI2C1 CR1を元に戻してCPUを再開した。SCL側の負荷・プルダウン・配線の原因は未確定。OLED表示、I2C波形と立上り時間は未確認。

### Orion CAN受信・ICM-20602・SH1106（2026-10-08）

- Debug／Releaseの`build.ps1 -Configuration <構成> -Rebuild`成功。新規`build/peripherals-check/Debug`、`build/peripherals-check/Release`でも構成からビルド・bin/hex生成まで確認。
- Debug: Flash 59,264 byte、RAM 12,776 byte。Release: Flash 34,500 byte、RAM 12,776 byte。
- `python ./Script/test_drivers.py`成功。旧試験と制御IRQ／ログ周期試験に加え、Orion形式の長さ・NaN/Inf・符号・バス分離・受信鮮度・ms周回、IMUの識別・設定読戻し失敗・再試行・換算・取得停止、OLEDの0x3C/3D検出・ページと列位置・描画範囲・転送中のバッファ保護・転送失敗をHALモックで確認。
- 対象: STLINK-V3MINIE、SN `002D00373033510635393935`、FW V3J17M11、MCU ID 0x469、STM32G47x/G48x/G414、Flash 512 KB、電圧3.26～3.27 V。ユーザー接続情報はOrion相当CANダミー、旧Orionと同じIMU、SH1106 128x64 OLED。
- 最初のFlash消去が失敗し、ST-Link USBエラーが発生。USB再接続後、SWD 1 MHzで`build/Debug/4WS_MainFW.elf`の書込み・照合・リセット成功。現在の書込みスクリプトのSWD既定値を1000 kHzにした。
- `read_monitor.py`でCPUを短時間停止して診断RAMを取得し、その都度再開。`build/hardware/monitor-1.json`、`monitor-2.json`へ保存。uptime 4,104→43,404 ms、IMU WHO_AM_I=0x12、valid=1、取得数1,923→21,332、エラー0。加速度の各軸、角速度の各軸、温度31.06→31.45 °Cを取得した。静止校正・軸向き・精度は未検証。
- 制御IRQ実行数2,049→21,700、周期超過0、最大463 cycles。SW取得有効、生値4095→4089。SWD停止が時間へ影響するため、これを500 Hzの実時間精度・最悪ジッター測定とは扱わない。ボタン押下は未検証。
- ST-Link VCPのCOM167を2,000,000 baudで読取り、UARTの継続出力を確認。約2秒で21行を取得し、`build/hardware/uart.txt`へ保存。制御実行数119,299→120,249、IMU取得数117,632→118,570で各エラー0。これは連続動作確認であり、厳密な周期測定ではない。
- 両CAN受信数0、RX破棄0、FIFO喪失0、bus-off計数0。FDCANは開始状態を確認したが、ダミーからの実データ受信は未確認。
- OLEDは0x3C/3Dとも検出できず、完成フレーム0。I2C1 ISR=0x00008001（BUSY）、HAL ErrorCode=0x20（TIMEOUT）、PB7/PB8生入力ともLowを確認。実表示は未確認、電源・配線・プルアップの確認が必要。
- アクチュエーター出力、電源操作、ブザー鳴動は実施していない。生成コード・`.ioc`は変更せず、SPI1の32分周設定はBoardが実行時に適用する。CubeMX本体の再生成は未実施。

### 500 Hz制御IRQ・10 Hzデバッグ出力（2026-09-14）

- Debug／Releaseの`build.ps1 -Configuration <構成> -Rebuild`が成功。新規の`build/control-loop-check/Debug`、`build/control-loop-check/Release`でも構成・ビルド成功。
- Debug: Flash 49,632 byte、RAM 9,344 byte。Release: Flash 28,456 byte、RAM 9,344 byte。
- `python ./Script/test_drivers.py`成功。既存ドライバ試験に加え、TIM6のPSC=169・ARR=1999、IRQ優先度、周期超過計数、BASEPRIの入れ子復元を確認。
- mainを停止した状態での制御入口実行、1秒で制御500回・ログ10回、IRQからの`p()`拒否、UART busy／main遅延時の追い掛け出力抑止、ms時刻周回をHALモックで確認。
- ADC公開途中の制御割り込みを模擬し、完成済みバッファだけを読むことを確認。
- 生成コードと`.ioc`は変更せず、TIM6・DWT・NVICはBoardが実行時に設定。実機の500 Hz波形・最悪実行時間・ジッターは未測定、書き込み未実施。

### CAN・UART4・ブザー・ADCユーザーSW（2026-09-13）

- Debug／Releaseとも`build.ps1`の`-Rebuild`付きでクリーンビルド成功。
- 新規の`build/drivers-check/Debug`と`build/drivers-check/Release`でCMake構成からビルド、bin／hex生成まで成功。
- Debug: Flash 48,080 byte、RAM 9,232 byte。Release: Flash 27,532 byte、RAM 9,224 byte。
- `python ./Script/test_drivers.py`がClang 17.0.6で成功。CANの満杯・順序・2バス・受信長、UARTのバッファ保持・容量・エラー復帰、ADC有効性、SW境界、全1～20000 Hz、時刻周回、電源形式・片側受理を検証。
- `.ioc`とADC/FDCAN生成初期値、UART4 baudrate、追加IRQ入口6個の単一定義をソース照合で確認。
- CubeMX本体による再生成、ST-Link接続・書き込み、実機の波形・通信・ボタン操作は未実施。

### ユーザーコード配置名の見直し（2026-09-13）

`Application/`への配置変更後、`build.ps1 -Configuration Debug -Rebuild`と`build.ps1 -Configuration Release -Rebuild`が成功しました。新規の`build/application-check/Debug`と`build/application-check/Release`でも構成からビルド、bin／hex生成まで確認しました。ソースの処理内容と生成コード・`.ioc`は変更していません。実機検証は未実施です。

### コード階層・GPIO APIの追加（2026-09-13）

- `build.ps1 -Configuration Debug -Rebuild`と`build.ps1 -Configuration Release -Rebuild`が成功。
- 新規の`build/architecture-check/Debug`と`build/architecture-check/Release`でもCMake構成からビルド、bin／hex生成まで成功。
- `board_gpio.c`のコンパイルを両構成で確認。未使用関数はリンク時に除去されるため、使用量はDebugがFlash 24,868 byte、ReleaseがFlash 14,180 byte、RAMは両構成2,744 byteのまま。
- 新規構成時には、検証コマンドで指定した`CMAKE_SIZE`がプロジェクトで未使用とのCMake警告あり。ビルドは成功。
- GPIOの有効極性、実端子の入力・出力、LED点灯は未検証。基板への書き込み・接続・動作試験は未実施。
- CubeMX生成コードと`.ioc`は変更していない。

### 初期プロジェクトの確認

2026-09-13にWindows PowerShell 5.1で次を確認しました。

- Debugビルド成功: Flash 24,868 byte、RAM 2,744 byte
- Releaseクリーン再ビルド成功: Flash 14,180 byte、RAM 2,744 byte
- `build_and_flash.ps1`のRelease／`-DryRun`／`-Serial`実行成功
- `flash.ps1`の`-List`、`-ConnectOnly`、`-NoReset`を`-DryRun`で確認

この確認ではST-Linkへの接続、実機書き込み、周辺機能の動作試験は行っていません。現在は`.gitignore`を用意し、ローカルGitリポジトリを初期化済みです。ブランチは`main`、リモート`origin`は`https://github.com/ibis-ssl/4WS_MainFW.git`に設定しています。
