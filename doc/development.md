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

CubeMXが管理するファイルを直接変更する場合は、再生成で消える箇所かどうかを確認します。アプリケーションコードは可能な限りUSER CODEブロックまたは追加ファイルへ配置し、追加ファイルはルート`CMakeLists.txt`から登録します。

## 検証範囲

ビルド成功はコンパイル、リンク、bin／hex生成までを確認するものです。FDCAN、UART、SPI、I2C、ADC、PWMの電気的動作や接続機器との通信は保証しません。実機確認を行った場合は、基板、接続条件、FW成果物、確認内容を記録してください。

## 検証記録

2026-09-13にWindows PowerShell 5.1で次を確認しました。

- Debugビルド成功: Flash 24,868 byte、RAM 2,744 byte
- Releaseクリーン再ビルド成功: Flash 14,180 byte、RAM 2,744 byte
- `build_and_flash.ps1`のRelease／`-DryRun`／`-Serial`実行成功
- `flash.ps1`の`-List`、`-ConnectOnly`、`-NoReset`を`-DryRun`で確認

この確認ではST-Linkへの接続、実機書き込み、周辺機能の動作試験は行っていません。現在は`.gitignore`を用意し、ローカルGitリポジトリを初期化済みです。ブランチは`main`、リモート`origin`は`https://github.com/ibis-ssl/4WS_MainFW.git`に設定しています。
