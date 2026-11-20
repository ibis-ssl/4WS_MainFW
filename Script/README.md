# 開発スクリプト

コマンド例はプロジェクトルートのWindows PowerShellで実行する。

| ファイル | 用途と主な引数 |
|---|---|
| `build.ps1` | `-Configuration Debug`／`Release`。全成果物の再ビルドは `-Rebuild` |
| `flash.ps1` | 既存ELFの書き込み。 `-List` はプローブ列挙、`-ConnectOnly` は接続確認、`-DryRun` は実行コマンド表示 |
| `build_and_flash.ps1` | ビルド後に書き込み。 `-DryRun` でもビルドを実行する |
| `test_drivers.py` | HALモックを使ったホスト検証。ホストCコンパイラーを指定する場合は `--cc` |
| `analyze_atlas_can.py` | 保存済みCANRush CSVを解析。引数はCSVのパスと、任意の `--output` |
| `estimate_atlas_can.py` | CAN帯域の試算。Mainのデバッグ周期での試算には `--c610-command-hz 100` |
| `read_monitor.py` | `app_monitor_status` のSWD読取り。 `--serial` 必須、`--elf` で実機に書いたELFを指定 |
| `read_oled_timing.py` | OLED計測RAMのSWD読取り。 `--serial` 必須、`--elf` と `--output` を指定可能 |
| `common.ps1` | PowerShellスクリプト共通のツール探索・実行処理 |

書き込みと再生成は[開発手順](../doc/development.md)、CAN採取と読取りの使い分けは[結合手順](../doc/atlas_bringup.md)を参照する。SWD読取りスクリプトはCPUを一時停止し、終了時に再開を試みる。

実機へ書き込んだELFがReleaseの場合の、駆動・周期計測終了後の診断例。

```powershell
python ./Script/read_monitor.py --serial STLINK_SERIAL --elf build/Release/4WS_MainFW.elf --output build/hardware/monitor.json
```

`STLINK_SERIAL` はMainに接続したST-Linkのシリアル番号に置き換える。構成名が同じでも、再ビルドで上書きされたELFを使うとシンボル位置が一致しない場合がある。書き込んだELFを使用する。
