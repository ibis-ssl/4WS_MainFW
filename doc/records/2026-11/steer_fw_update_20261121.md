# ステア温度とMain経由FW更新

2026-11-21。PCがCM4相当の要求をMainのST-Link RTTへ送った。MainはReleaseをST-Link SN `002D00373033510635393935` で書き込み、照合とリセットに成功。最終ELF SHA256は `761E66E0D9D6904469705A04FE5B30F52ECB3284ECD9FA8FEBB03CBE01C4ED51`。

- Debug／Releaseのクリーンビルドと `python ./Script/test_drivers.py` が成功。温度の符号、無効値、0℃、鮮度、バス分離と、RTTのデータ受信、CRC不一致拒否、CAN更新の分割、確定、再起動確認を検証した。
- MainをHALモック、ステアを参照チェックアウト `1100950` の `Boot/update_protocol.c`、対象FlashをRAMで模擬した組合せでも、更新、書込み内容の一致、起動可能判定を確認した。
- 実機MainではUSB-CANからCAN1に `0x290`、CAN2に `0x292` の試験フレームを各1回送った。PCのRTT診断は順に25.3℃、−1.2℃、`null`、0.0℃を返し、2.5秒経過後は4軸とも `null` になった。両ステア基板の実温度フレームは観測されなかった。
- ステアV4のReleaseアプリ48,172バイト、CRC32 `63ECDA06` をRTTからMainのRAMへ転送した。ID 0／1それぞれでMainが対象バスへ保守offset 0/16のID照会を送り、応答なしで停止した。書込み長は両方とも0バイト。採取したCANログにENTERやFlash消去指令は無かった。C610の4台分の帰還は両バスで継続した。
- 実基板のアプリ書込み・再起動、CRCと保存設定の照合、CM4のSPI経路は未確認。[実装・検証計画](../../implementation_plan.md)

元データは `build/hardware/` の `atlas-fw-final-evidence-20261121.json`、`steer-update-id0-20261121.jsonl`、`steer-update-id1-20261121.jsonl`、`steer-update-can-20261121.json`。実行手順は[温度とステアFW更新](../../steer_fw_update.md)を参照する。

## 失敗箇所の再調査

同日、両バスを12秒間受信し、COM12ではC610のID 1・2、COM46ではID 3・4を各約1.2万件確認した。ステアの角度・校正・温度通知はどちらのバスでも0件だった。

Mainを経由せずUSB-CANからも、保守offset 0／16の両方でID・Faultおよび更新情報・イメージ長・CRC・ハードウェアの読取りを行った。全照会が無応答。MainのRTTは `boot=2`、温度の受信マスク0を示した。前回の更新ログは、ステアのID照会段階のタイムアウトで、両軸基板とも書込み長0バイトだった。今回の採取では、更新開始、リセット、書込みは実行していない。

この構成では更新プロトコルに入る前のステアCAN応答が得られていない。給電・物理経路・ステアFWの稼働状態のいずれかを確定できる計測は未実施。元データは `build/hardware/` の `steer-fw-diagnosis-20261121.json`、`steer-fw-diagnosis-passive-20261121.json`、`steer-fw-diagnosis-rtt-20261121.json`。

## ST-LinkでのID 1実機照合

接続先のステア基板はRAMの設定でID 1、保守offset 0、有効軸マスク3、commissioned 1。ST-Linkで3.27 V、VTOR `0x08004000`、HAL tickの増加を確認した。V4形式3のブートとアプリの識別子を読み取れた。

アプリ領域から読み取った69,800バイトのCRC32は `68302DCC`。参照プロジェクトの `build/Debug/G474_SWCLDvier.bin` とバイト単位で一致した。この構成は `DUAL_NO_MOTOR_TEST=ON`。CAN1の `CCCR=0x1060` はMON=1で、`dual_can_active=0`。一方で受信カウンターは250 msで0x5E2ADから0x5E4A4へ増加した。ID 1はCAN監視専用FWのため通常のステア帰還・保守応答・更新開始要求の処理が無効。ID 0の搭載イメージは今回の接続からは未確認。

CPU停止・リセット・書込みは未実施。元データは `build/hardware/` の `steer-stlink-status-20261121.txt`、`steer-stlink-can-live-20261121.txt`、`steer-stlink-identity-20261121.txt` とFlash読み取り結果。

## ステアFW差し替え後のRTT実機試験

ステアFW差し替え後、ST-LinkをMainに接続して再試験した。CAN1／COM12はID 0、保守offset 16、CAN2／COM46はID 1、保守offset 0。開始時は両基板とも保守CANでIDとUIDを読み取れた。元のアプリは両基板とも通常動作用のDebug 71,440バイト、CRC32 `92406F64`。試験時はモーターの駆動指令を送らず、C610の周期帰還がある状態で実施した。

- ID 0では、MainのRTTから試験用Release 48,172バイトを転送し、CAN書込み、ステア再起動後のCRC32 `63ECDA06`、保存設定の一致を確認した。
- ID 1では、同じイメージを7,424バイト確定した後、オフセット `0x1D00` のブロック開始要求をCAN2で4回観測したが、対応する応答が届かなかった。Mainは段階8、エラー1で停止した。応答が欠落した物理要因は未特定。ステアブートのCAN送信は自動再送が無効のため、Mainのブロック再送間隔を変更し、応答欠落の許容回数を増やした。
- 再接続時、OpenOCDがステアBIN内のRTT署名をMainの制御ブロックとして検出した。搭載Mainの制御ブロックアドレスを指定して接続を確認した。修正後のMainでは制御ブロックを更新バッファより前へ配置した。

初回のRTTと両CANのログは `build/hardware/steer-fw-live-20261121-021155/`、RTT再接続のログは `steer-fw-live-20261121-021338/openocd.txt` と `steer-fw-live-20261121-145735/` を参照。

### 復旧と修正版での再試験

Mainの再起動後はID 1のアプリが起動せず、ブートローダーは宛先指定のENTERを待っていた。CAN2のUSB-CANからENTERを送り、試験前のイメージで復旧した。再起動後、ID 1のUID、71,440バイト、CRC32 `92406F64` と保存設定を読み戻した。

再送とRTTを修正したMainのReleaseをST-Linkで書き込み、照合とリセットを実施した。ELF SHA256は `FE6DF8EAF1C9FD598127CE7020A9DB12DF978ECDF0462D446038994A7856EA05`。そのMainにPCからRTTで試験前と同じイメージを渡し、両基板を順に更新した。

| 更新先 | RTT転送から完了まで | 更新前 → 再起動後のCRC32 | 再起動後の設定 |
|---|---:|---|---|
| ID 0／CAN1 | 18.10秒 | `63ECDA06` → `92406F64` | 試験前と一致 |
| ID 1／CAN2 | 41.09秒 | `92406F64` → `92406F64` | 試験前と一致 |

両基板とも71,440バイトの書込み完了と、再起動後のアプリ応答を確認した。照合した設定項目は基板ID、UID、両軸の有効状態、commissioned、保存電流上限と極性。両CANの採取ログで再送と再起動応答を確認した。CAN2の記録は一部のフレームが未収録のため、全ブロックの完了判定はMainの更新ステータスと、再起動後の長さ・CRCの独立照会を用いた。

更新後はMainを再起動せずにOpenOCDを立ち上げ直し、アドレス指定なしで `0x200000A0` のRTTへ再接続できた。Mainは `boot=1`、C610帰還 `esc=F`、指令電流 `0,0,0,0`、ステア校正 `cal=0`。温度は4軸分のフレームを受信したが、有効値は0軸だった。対象ステアFWは温度入力に `NAN` を設定している。CM4の実SPI経路、実温度値、校正や駆動は今回の再試験の対象外。

Debug／Releaseのクリーンビルドと `python ./Script/test_drivers.py` が成功。ホスト試験はブロック開始応答を5回落としても更新が完了する条件を追加した。元データは `build/hardware/` の `recover-steer-id1-20261121.log`、`steer-fw-live-20261121-150231/`、`flash-main-steer-retry-20261121.log`、`steer-fw-live-20261121-150237/`、`steer-fw-live-20261121-150346/`。
