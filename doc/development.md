# 開発手順

プロジェクトルートをカレントディレクトリにしたWindows PowerShellで実行します。ソース、文書、スクリプトはUTF-8で保存します。

## 着手時の確認

```powershell
git status --short --branch
git submodule status --recursive
```

変更対象と既存差分を確認し、[実装・検証計画](implementation_plan.md)と関係する仕様を参照します。実機試験の前に、構成・対象基板・確認内容・ログの保存先を決めます。ツールの入口と引数は[スクリプト一覧](../Script/README.md)にまとめています。

## 共通仕様リポジトリ

複数レイヤー・リポジトリにまたがる仕様は、`specs/`のGit submodule（`https://github.com/ibis-ssl/specs.git`）で参照します。新規取得時は次のコマンドを使用します。

```powershell
git clone --recurse-submodules https://github.com/ibis-ssl/4WS_MainFW.git
```

既存の作業コピーや親リポジトリの更新後は、プロジェクトルートで次を実行し、親リポジトリが記録したコミットを取得します。

```powershell
git submodule update --init --recursive
```

参照する仕様の版を変更する場合は、`specs/`内で対象コミットをcheckoutし、親リポジトリで`git add specs`して参照コミットの変更をコミットします。共通仕様の変更は仕様リポジトリ側で管理し、FW固有の実装状況・検証結果は本リポジトリの`doc/`へ記録します。submoduleの追加自体はFWの実装済み・実機検証済み範囲を変更しません。

仕様の更新後は[管理境界](specs_migration.md)の照合コミット・参照リンクと、[通信仕様と実装状況](overview.md#通信仕様と実装状況)を確認します。Orion CANの契約はspecs、本FWの対応範囲・DLC差異は`doc/`に記載します。未実装案は`specs/proposals/`として区別し、仕様更新だけでFWが適合済みとは扱いません。

specs内を編集するときは作業ブランチを作成します。機器間の契約・未決事項はspecsで更新し、FW側は参照と対応状況を更新します。

```powershell
# 作業ごとにブランチ名を決める
git -C specs switch -c docs/4ws-spec-boundaries
```

specsの未コミット変更は親repoの参照コミットだけでは復元できません。共有するときはspecs側の変更をコミットし、そのコミットを親repoで記録します。設計案や契約へのリンクはsubmodule内への相対パスとし、mainの最新版へのリンクで参照版を迂回しません。

## Atlas／C610の結合確認

ステア基板とC610のPC検証結果、Mainでの操作、実機試験の順序とCANRushのCSV採取は[結合開発](atlas_bringup.md)を参照してください。

## 必要なツール

- CMake 3.22以降とNinja
- Arm GNU Toolchain（`arm-none-eabi-gcc`、`arm-none-eabi-objcopy`など）
- ホスト検証にはPython 3とホスト用Clang。CANのオフライン解析にもPython 3を使う
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

DebugとReleaseのどちらにもモーターデバッグが含まれます。無駆動確認ではSW7をモーターデバッグ以外のコードに合わせ、駆動指令が出ない条件で試験します。CMakeキャッシュの影響を確認する場合は、別の空のビルドディレクトリを指定して構成します。

成果物は次の場所へ生成されます。

| 成果物 | 用途 |
|---|---|
| `build/<Configuration>/4WS_MainFW.elf` | デバッグ情報を含む実行イメージ |
| `build/<Configuration>/4WS_MainFW.bin` | バイナリー書き込み用イメージ |
| `build/<Configuration>/4WS_MainFW.hex` | Intel HEX書き込み用イメージ |
| `build/<Configuration>/4WS_MainFW.map` | セクション配置とサイズの確認 |
| `build/<Configuration>/compile_commands.json` | エディターや静的解析用コンパイル情報 |

## ST-Link書き込み

対象のMain基板とST-Linkを照合します。書き込みとリセットは、モーターデバッグを解除し、意図しない駆動が発生しない条件で行います。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/flash.ps1 -List
```

Release成果物のパスと、書き込み・照合・リセットのコマンドを事前に表示する例です。`STLINK_SERIAL` を列挙結果のMain側の値に置き換えます。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File ./Script/flash.ps1 `
  -Configuration Release `
  -Serial STLINK_SERIAL `
  -DryRun
```

構成・プローブ・ELFを確認したら、`-DryRun` を外して実行します。

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

書き込み後は照合結果とリセット後の起動を確認し、構成・シリアル・書き込んだELFを記録します。成果物を特定する場合は、書き込みに使ったELFのハッシュを保存します。

```powershell
Get-FileHash ./build/Release/4WS_MainFW.elf -Algorithm SHA256
```

### Flash関連の変更確認

| 変更箇所 | 合わせて確認する箇所 |
|---|---|
| Flash/RAM配置、ベクター、起動コード | [リンカースクリプト](../STM32G474xx_FLASH.ld)、[起動コード](../startup_stm32g474xx.s)、ELF／mapのロード範囲とリセット後の起動 |
| 成果物の名前・出力先・構成、ツールチェーン | [CMake](../CMakeLists.txt)、[プリセット](../CMakePresets.json)、[build.ps1](../Script/build.ps1)、[flash.ps1](../Script/flash.ps1)、VS Codeのタスク・デバッグ構成 |
| ST-Link接続・書き込み引数・照合 | [flash.ps1](../Script/flash.ps1)と[build_and_flash.ps1](../Script/build_and_flash.ps1)の引数と`-DryRun`出力。実機確認では対象と照合・リセット結果 |
| 診断RAMの型、シンボル、サイズ | [診断RAM読取り](../Script/read_monitor.py)、[OLED計測読取り](../Script/read_oled_timing.py)で、書き込み済みELFとの組合せ |

ステア基板のブートローダー、設定保持更新、CAN書き込みはステア側の手順を参照します。Mainの現行経路は上記のST-Linkによる単一ELF書き込みです。

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

| 変更範囲 | 確認する内容 |
|---|---|
| 文書のみ | ローカルリンク・見出し・コマンド引数、`git diff --check` |
| FW・ドライバ・通信形式 | 対象構成のビルド、該当するHALモック。実機依存の結果は検証記録へ残す |
| CMake・ツールチェーン・生成設定 | Debug／Releaseのクリーンビルド、必要に応じてキャッシュのない構成、生成差分とELF／map |
| 実機のCAN・モーター動作 | [結合手順](atlas_bringup.md#実機結合の手順)に沿い、無駆動観測・各輪の操舵・四輪走行・停止を段階的に確認 |

基礎ドライバのホスト検証は、Python 3とホスト用Clangを用いて次を実行します。Arm用GCCはこのホスト検証には使用しません。

```powershell
python ./Script/test_drivers.py
# コンパイラーの場所を指定する場合
python ./Script/test_drivers.py --cc "C:/Program Files/LLVM/bin/clang.exe"
```

実ドライバをHALモックに接続したテストです。対象と限界は[基礎ドライバ](drivers.md)を参照してください。CubeMX再生成時は、同文書のDMA・NVIC・IRQ所有表に従ってBoardとの二重設定を避け、ADC/FDCANの生成初期値との整合も確認します。

ビルド成功はコンパイル、リンク、bin／hex生成までを確認するものです。FDCAN、UART、SPI、I2C、ADC、PWMの電気的動作や接続機器との通信は保証しません。実機確認を行った場合は、基板、接続条件、FW成果物、確認内容を記録してください。

## 検証記録

[検証記録の一覧](records/README.md)を参照してください。
