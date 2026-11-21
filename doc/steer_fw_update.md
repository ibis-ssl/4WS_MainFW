# ステア温度とステアFW更新

## 接続と責務

運用時の相手はCM4。PCデバッグ時はMainのST-Link RTTを使用する。`App/app_host_rtt` がデバッグコマンドを解釈し、更新要求は型付きの `App/app_steer_update` APIへ渡す。将来のCM4 SPI受信側は同じAPIの開始、データ投入、確定、状態取得を呼ぶ。AtlasのSPI転送形式は今後の仕様確定に従う。

Mainは対象のFlash基板IDを保守CANで照合し、1回に1基板だけを更新する。ID 0はCAN1、ID 1はCAN2。保守offset 0/16を照会してから、ステア側V3ブートの `0x600/0x601/0x602 + offset` を使用する。Orionのゲートウェイを参考に、上位通信とCAN更新を分離し、停止・応答・書込み結果をMainで管理する。 [Atlas CAN接続仕様](design/atlas_can.md)

## ステア温度

CAN1の `0x290` とCAN2の `0x292` はDLC 4で、M0／M1の温度を符号付きlittle-endian int16、0.1℃単位で格納する。MainはバスとDLCを照合し、2.5秒以上経過した値と `0x8000` を無効扱いにする。RTTの `temp` はg0〜g3の受信マスク、温度有効マスク、生値を返す。PCツールは無効値をJSONの `null` へ変換する。

## PCからの操作

Mainの[RTTサーバー](atlas_bringup.md#mainのrtt)を起動し、プロジェクトルートで実行する。

```powershell
python ./Script/main_debug_rtt.py temperature
python ./Script/main_debug_rtt.py --log ./build/hardware/steer-update-id0.jsonl steer-update --board 0 ../G474_SWCLDvier/build/Release/G474_SWCLDvier.bin
```

対象の電源・CAN配線・ビルド構成を確認し、ボタンを離して、他のCAN更新ツールを終了する。入力はV4用のアプリBIN。PCとMainの両方がベクター、識別子、サイズを検査し、Mainは最大96 KiBの全データをRAMに揃えてCRC32を照合してからCAN転送を始める。このバッファを含むReleaseの静的RAM使用量は約113 KiB。

更新中はボタン・RTT駆動を抑止し、両CANへ0 Aとcoastを送る。Mainは256バイト単位の書込み後に確定長を確認し、最後に全イメージの長さとCRC、再起動後の基板IDとCRCを照合する。更新対象はアプリとそのメタデータ。ブート本体と保存設定の書き換えはV3ブートの対象外。開始からデータ受信中は中止可能で、CANでの更新開始後はPC接続が切れてもMainが完了または失敗まで処理する。

ステアブートのCAN送信は自動再送しない。ブロックの開始、データ送信、確定で応答が欠落した場合、Mainは再送間隔を35〜63 msの範囲で変え、同じ要求を初回込みで最大24回送る。アプリ領域の消去要求に応答がない場合は更新を停止する。

CAN更新が始まった後に失敗した場合は駆動禁止を維持する。同じMainの起動中なら、照合済みの同じ対象へ再度イメージを送れる。Mainも再起動した場合は、ステア基板の現状態を確認し、既存のステア側CAN更新手順で復旧する。更新成功後は再校正を行ってから操舵する。
