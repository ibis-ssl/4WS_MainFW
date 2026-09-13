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

`.vscode/tasks.json`には`Build: Debug`、`Build: Release`、`Build: Rebuild Debug`、`Flash: Build and Flash Debug`、`Flash: Dry Run Debug`、`Flash: List ST-LINK Probes`があります。タスクは`Script/`のPowerShellスクリプトを呼び出すため、コマンドラインと同じ手順になります。

デバッグにはVS CodeのCortex-Debug拡張を使用します。`STM32G474: Build & Debug (ST-LINK)`はDebugビルド後に起動し、`STM32G474: Attach (ST-LINK)`は実行中のターゲットへ接続します。どちらもST-Linkと`build/Debug/4WS_MainFW.elf`を使用します。`.vscode/settings.json`の`STM32VSCodeExtension.cubeCLT.path`は、実際にインストールしたSTM32CubeCLTのルートへ変更してください。

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
