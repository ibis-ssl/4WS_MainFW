# CAN2に固定した電源基板の実機確認

2026-11-21。Main `c72f0fb` のRelease（ELF SHA256 `a61766c67d77ddff1f4152034e36fa0900ed3697697839a9fbbfccfef0fced93`）をST-Link SN `002D00373033510635393935` で書き込み、照合・リセットした。CAN1はCOM12、CAN2はCOM46、UART4はCOM167（2 Mbps・8N1）。

- 新FWの起動以降、電源リセット `0x001`・充電停止 `0x110`・PowerOn／Off `0x010` はCAN2で採取し、CAN1では0件。両バスの車輪電流指令 `0x200` は採取分すべて0 A。
- 電源基板は `0x244` で `0x18` を返した。UARTも実測電圧・電流・温度を表示し、`En=1 Out=1 Bst=0 Kick=0 Chg=0` を確認した。初回はPowerOnを18件、再リセットでは234件採取した。再リセット時のPowerOn間隔は中央値20.0 ms、最大21.35 ms。最初のPowerOnからPowerOffまで4.69秒で、給電中の `0x244=0x18` は771件。
- 再リセットではCAN2のステアID 1から起動確認の応答がなく、Mainは `BT=FAIL F=1`、`PW=OFF` に移行した。電源基板の `0x244` は `0` に戻った。USB-CANの独立照会でもID 1は無応答、ID 0は基板IDとアプリCRCを返した。定常給電の確認はID 1との通信復旧後に行う。
- Debug／Releaseのビルドと `python ./Script/test_drivers.py` が成功。電源指令のCAN2限定、電源通知の受理、ステアID 1更新と同じバスでのPowerOn継続をホスト試験で確認。

生ログは `build/hardware/power-can2-20261121-194130/`、`power-can2-20261121-194300/`、`steer-fw-live-20261121-194338/`。[Orionとの比較](../../design/orion_power_can.md)
