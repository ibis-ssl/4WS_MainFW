# 電源基板の給電状態とCANの現状確認

2026-11-21。ST-Link SN `003300343033510735393935`、電源基板VCP `COM15`（2 Mbps・8N1）、Main VCP `COM167`、CAN1 `COM12`、CAN2 `COM46`。UART／CANは受信のみ、SWDはHot PlugでRAM・レジスターを読み取った。リセット・書込み・給電指令の追加送信は行っていない。

- 電源基板はSTM32F303系、ターゲット電圧3.27～3.28 V、VTOR `0x08004000`。ビルドID `6B0171D2`、更新メタデータのアプリ長65,244 byte・CRC32C `FD351EE0` が `F303_boost` の `Debug/` にある成果物と一致した。対応ELFの型・アドレスでRAMを復号した。UART出力と `uwTick` の進行を確認。
- Mainは `BT=RDY F=0`。CAN1の7秒間の採取でPowerOn `0x010:0001000000000000` が350件、間隔の中央値20.012 ms。CAN1／CAN2でそれぞれステアとC610の帰還があった。電源基板の周期通知 `0x215/216/224/234/244` は観測されなかった。
- 電源基板は `can_rx_cnt=0`、`sw_enable_cnt=0`、`power_enabled=0`、`stat.error=0`。`POWER_SW_EN`（PB2）はODRとIDRの両方がLow。充電許可は0、昇圧とキッカーのPWM CCRは0。内部の昇圧目標は150 Vのままで、UARTのキャパシター測定値は約16～18 V。別に測定したバッテリーは約21.7～21.9 V。
- bxCANの `ESR=0x00800033` はLEC=3（ACKエラー）、TEC=128、REC=0、Error Passive。電源基板は送信時にACKを得られず、MainからのPowerOnも受信カウンターに反映されていない。両者のCAN経路で通信が成立していない状態まで特定した。配線・トランシーバー・波形の原因箇所は未確認。電源基板のBTRは `0x00780001`（設定上1 Mbps、サンプル点約55.6%）。Mainは1 Mbps、約88.2%。この設定差と今回の症状の因果関係は未確認。

ログは `build/hardware/power-live-20261121-192927/` の `COM15.txt`、`COM12.jsonl`、`swd-can.txt`、`swd-state.txt`、`swd-recheck.txt` と、`power-live-20261121-193035/` の再採取分。

## ケーブル交換後の再確認

同日19:36頃。CAN1（COM12）でMainのPowerOnを7秒間に349件、中央値20.016 msで観測した。CAN2（COM46）では電源の `0x215/216/224/234/244` を各1,150〜1,151件観測した。`0x244` は全件0で、給電許可と出力はともに0。電源の報告値はバッテリー21.67～21.93 V、キャパシター14.72～15.29 V、バッテリー電流−0.23～−0.16 A、FET／コイル温度33／32／30℃。両バスで対応するステアとC610の帰還も確認した。

FW読取り照会 `0x611` を両バスに各3回送った。CAN1は応答0回、CAN2は `0x6C4` で3回応答し、すべてビルドID `6B0171D2` とCRC32C `FD351EE0` が電源基板のSWD読取りと一致した。電源基板はCAN2で通信しており、PowerOnの送信先はCAN1。配線上の分岐点やコネクターの取り違え箇所は未特定。

電源のSWD Hot Plugで3回確認し、bxCANの `ESR=0`、TEC／RECとも0、`sw_enable_cnt=0`、`power_enabled=0`、PB2出力Low、昇圧／キッカーPWM 0を記録した。給電指令、リセット、FW書込みは追加していない。ログは `build/hardware/power-live-20261121-193610/` の `COM12.jsonl`、`COM46.jsonl`、`COM15.txt`、`COM167.txt`、`version-COM12.json`、`version-COM46.json`。

その後のSWD再接続ではデバイス識別エラーと `DEV_USB_COMM_ERR` が出た。同時期のCAN2再照会は同じビルドIDとCRCを返し、周期通知も継続していた。追加ログは同フォルダーの `swd-state.txt`、`swd-retry.txt`、`version-final-COM46.json`。
