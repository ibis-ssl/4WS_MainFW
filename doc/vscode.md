# VS Code拡張機能の確認

2026-10-10に、インストール済み48拡張、ユーザー設定、4WS_MainFWの設定、拡張マニフェスト、VS Codeの有効/無効状態と実行プロセスを確認した。インストール・拡張自体の有効状態と、解析機能の実行状態を区別する。他プロジェクトの設定は変更していない。

## C/C++解析

Microsoft C/C++とSTM32Cube clangdは拡張自体が有効。ワークスペースの`stm32cube-ide-clangd.enable=false`により、このプロジェクトのclangd解析を停止した。`C_Cpp.intelliSenseEngine=default`でC/C++解析を有効にし、実行プロセスはcpptools・Tag Parser・IntelliSenseで、clangdプロセスは見つからなかった。拡張機能一覧で2つが有効に見えることと、2つの解析サーバーが実行中であることは異なる。

`ms-vscode.cpptools-extension-pack`は拡張のまとめ、`ms-vscode.cpp-devtools`は既存のC/C++・CMake機能を使う開発支援、`cpptools-themes`と`better-cpp-syntax`は配色・構文強調。これらを独立したC/C++解析サーバーとは扱わない。プロセス一覧の複数のcpptools-srvはすべて同じMicrosoft C/C++拡張に由来する。

## 設定の確認結果

| 対象 | 確認結果・対応 |
| --- | --- |
| Microsoft C/C++ | 全体では解析無効だった。プロジェクトで有効にし、DebugのコンパイルDB・Arm GCC・Cortex-M4/FPUを使用 |
| STM32Cube clangd | 拡張自体は有効、プロジェクトの解析機能は無効。全体の競合検知設定はfalseで、他プロジェクトの設定は維持 |
| STM32Cube CMake | 自動IntelliSense設定が既定で有効だった。プロジェクトで無効にして直接参照するコンパイルDB設定の上書きを防止 |
| Microsoft CMake Tools | プリセット使用、configureOnOpen=false。CubeCLT 1.21のCMakeとGNU/NinjaのPATHを明示し、全体のST拡張同梱ツールPATHを上書き |
| C/C++フォーマット | CファイルのフォーマッターをMicrosoft C/C++へ固定。STM32Cube clangd側のフォーマットは既定でも無効。保存時整形の既存設定を維持 |
| ESP-IDF | 拡張は有効。CMakeLists.txtにより起動対象になるが、STM32のcompilerPathをArm GCCへ固定。確認時にESP系clangdプロセスは見つからず、このプロジェクトの解析設定に採用していない |
| Cortex-Debug | 全体で無効。既存launch.jsonはcortex-debug指定なので、このままでは当該デバッグ構成を起動できない。有効化するときは拡張機能画面から「有効にする（ワークスペース）」を選ぶ |
| MCU Debugの4拡張 | Debug Tracker / Memory View / Peripheral Viewer / RTOS Viewsは全体で無効。今回変更せず |
| Arm Assembly | 全体で無効。Cの定義ジャンプとは別の機能で、今回変更せず |
| STM32のその他の拡張 | インストール・全体有効。ビルド解析、プロジェクト管理、デバッグサーバー、レジスター、RTOSなど。個別の実機デバッグ動作は未検証 |
| Python/Pylance/Debugpy/Python Environments | 全体有効。別言語の機能で、今回設定変更せず。Python補完・デバッグのUI動作は未検証 |
| PowerShell、Git、Remote SSH、LaTeX、表示・計測補助、Codex | 下記一覧で有効状態を照合。C/C++解析設定としては使用せず、個別機能の動作試験は未実施 |

VS Code用と同じCubeCLTのCMake/GNU/Ninjaで、新規の`build/vscode-check/Debug`・`Release`を構成からビルドし、両方成功した。ソース・生成コード・.ioc・FWは変更せず、実機書込みも行っていない。

通常の使い方と定義ジャンプ・補完の確認は[開発手順](development.md#vs-code)を参照。F12や補完のUI操作、各拡張の全機能の実行確認は未実施。監査のJSONは`build/vscode-check/extension-audit.json`に保存した。

## インストール済み拡張の有効状態

「有効」は全体の無効リストに含まれないことを示す。ワークスペースで解析を止めたSTM32Cube clangdも拡張自体は有効として表示する。各拡張の全機能が実行中という意味ではない。

| 拡張ID | バージョン | 全体の有効状態 |
| --- | --- | --- |
| `alexnesnes.teleplot` | 1.1.4 | 有効 |
| `dan-c-underwood.arm` | 1.7.4 | 無効 |
| `eclipse-cdt.memory-inspector` | 1.3.0 | 有効 |
| `eclipse-cdt.serial-monitor` | 2.0.0 | 有効 |
| `espressif.esp-idf-extension` | 2.3.0 | 有効 |
| `george-alisson.html-preview-vscode` | 0.2.5 | 有効 |
| `james-yu.latex-workshop` | 10.19.0 | 有効 |
| `jcdj666.uv-run` | 0.1.0 | 有効 |
| `jeff-hykin.better-cpp-syntax` | 1.27.1 | 有効 |
| `marus25.cortex-debug` | 1.12.1 | 無効 |
| `mcu-debug.debug-tracker-vscode` | 0.0.15 | 無効 |
| `mcu-debug.memory-view` | 0.0.29 | 無効 |
| `mcu-debug.peripheral-viewer` | 1.6.1 | 無効 |
| `mcu-debug.rtos-views` | 0.0.16 | 無効 |
| `mhutchie.git-graph` | 1.30.0 | 有効 |
| `ms-ceintl.vscode-language-pack-ja` | 1.131.2026090407 | 有効 |
| `ms-python.debugpy` | 2026.6.0 | 有効 |
| `ms-python.python` | 2026.8.0 | 有効 |
| `ms-python.vscode-pylance` | 2026.4.1 | 有効 |
| `ms-python.vscode-python-envs` | 1.38.0 | 有効 |
| `ms-vscode-remote.remote-ssh` | 0.128.0 | 有効 |
| `ms-vscode-remote.remote-ssh-edit` | 0.87.0 | 有効 |
| `ms-vscode.cmake-tools` | 1.24.42 | 有効 |
| `ms-vscode.cpp-devtools` | 0.6.18 | 有効 |
| `ms-vscode.cpptools` | 1.34.4 | 有効 |
| `ms-vscode.cpptools-extension-pack` | 1.5.1 | 有効 |
| `ms-vscode.cpptools-themes` | 2.0.0 | 有効 |
| `ms-vscode.hexeditor` | 1.11.1 | 有効 |
| `ms-vscode.powershell` | 2025.4.0 | 有効 |
| `ms-vscode.remote-explorer` | 0.5.0 | 有効 |
| `openai.chatgpt` | 26.1007.21434 | 有効 |
| `openai.codex-audio` | 26.1007.21434 | 有効 |
| `stmicroelectronics.stm32-vscode-extension` | 3.11.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-build-analyzer` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-build-cmake` | 1.47.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-bundles-manager` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-clangd` | 1.1.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-core` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-debug-core` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-debug-generic-gdbserver` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-debug-jlink-gdbserver` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-debug-stlink-gdbserver` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-project-manager` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-registers` | 1.5.0 | 有効 |
| `stmicroelectronics.stm32cube-ide-rtos` | 1.5.0 | 有効 |
| `trond-snekvik.gnu-mapfiles` | 1.1.0 | 有効 |
| `twxs.cmake` | 0.0.17 | 有効 |
| `zixuanwang.linkerscript` | 1.0.4 | 有効 |
