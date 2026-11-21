# CAN1のPowerOn周期と昇圧・キッカー停止の確認

2026-11-21。`c9757c7` のMain ReleaseをST-Link SN `002D00373033510635393935` で書き込み、照合・リセットした。ELF SHA256は `ee52d5443e032ed2965f8fd05517539356f3cb93dee9e4fbcfb972d679cf3427`。USB-CANのCOM12（CAN1）・COM46（CAN2）とST-Link VCPのCOM167を使用。モーター操作は行っていない。

- CAN1で電源リセット `0x001`、充電停止 `0x110:0100c80000000000`、PowerOn `0x010:0001000000000000` を順に記録。充電停止から最初のPowerOnまでは20.46 ms。PowerOnは234件、採取時刻間隔の中央値20.005 ms、最大21.333 ms。CAN2への電源指令は0件。観測した `0x110` は充電停止だけで、両CANの `0x200` はすべて0 Aだった。
- 初回はCAN2のステアID 1から保守ID照会の応答がなく、約5秒で `BT=FAIL F=1 PW=OFF` に切り替わった。CAN1でその後のPowerOffを採取した。独立照会でもID 1は応答せず、ID 0はUIDと71,440 byte、CRC32 `92406F64` を返した。再リセットでもID 1の起動確認が失敗し、MainはPowerOffを維持。CAN2ではC610 ID 3／4の帰還は継続した。
- 両試行で実電源基板由来の周期フレーム `0x215/216/224/234/244` は観測されなかった。USB-CANからノード100へ送ったFW読取り照会 `0x611` は、PowerOff中とPowerOn継続中のどちらも応答 `0x6C4` が観測されなかった。実電源基板の動作、出力ピン、キャパシターの実測電圧は未確認。
- CAN1へ測定通知の試験フレームを送り、`KICKER` で24.4 V、キャパシター0.0 V、−2.5 A、31／32／33℃、状態フラグ全0と期限切れを確認した。IMUは `ODOM` でWHO_AM_I `0x12` と有効な3軸データを確認。ステアID 0／C610のCAN帰還と電源測定通知の模擬試験を区別している。
- Debug／Releaseのビルドと `python ./Script/test_drivers.py` が成功。充電停止→PowerOnの順序、20 ms周期、CAN待ち時の再試行、昇圧関連フラグのFault保持、未送信PowerOnの選択的な取り消し、ステア更新中の継続給電をホストで検証した。

生ログは `build/hardware/power-keepalive-20261121-190334/`、`power-keepalive-20261121-190640/`、`steer-fw-live-20261121-190455/`。給電と停止の仕様は[Orion電源CAN](../../design/orion_power_can.md#4wsの給電と停止条件)を参照。
