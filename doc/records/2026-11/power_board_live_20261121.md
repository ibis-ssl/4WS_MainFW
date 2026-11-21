# 電源基板の給電状態とCANの現状確認

2026-11-21。ST-Link SN `003300343033510735393935`、電源基板VCP `COM15`（2 Mbps・8N1）、Main VCP `COM167`、CAN1 `COM12`、CAN2 `COM46`。UART／CANは受信のみ、SWDはHot PlugでRAM・レジスターを読み取った。リセット・書込み・給電指令の追加送信は行っていない。

- 電源基板はSTM32F303系、ターゲット電圧3.27～3.28 V、VTOR `0x08004000`。ビルドID `6B0171D2`、更新メタデータのアプリ長65,244 byte・CRC32C `FD351EE0` が `F303_boost` の `Debug/` にある成果物と一致した。対応ELFの型・アドレスでRAMを復号した。UART出力と `uwTick` の進行を確認。
- Mainは `BT=RDY F=0`。CAN1の7秒間の採取でPowerOn `0x010:0001000000000000` が350件、間隔の中央値20.012 ms。CAN1／CAN2でそれぞれステアとC610の帰還があった。電源基板の周期通知 `0x215/216/224/234/244` は観測されなかった。
- 電源基板は `can_rx_cnt=0`、`sw_enable_cnt=0`、`power_enabled=0`、`stat.error=0`。`POWER_SW_EN`（PB2）はODRとIDRの両方がLow。充電許可は0、昇圧とキッカーのPWM CCRは0。内部の昇圧目標は150 Vのままで、UARTのキャパシター測定値は約16～18 V。別に測定したバッテリーは約21.7～21.9 V。
- bxCANの `ESR=0x00800033` はLEC=3（ACKエラー）、TEC=128、REC=0、Error Passive。電源基板は送信時にACKを得られず、MainからのPowerOnも受信カウンターに反映されていない。両者のCAN経路で通信が成立していない状態まで特定した。配線・トランシーバー・波形の原因箇所は未確認。電源基板のBTRは `0x00780001`（設定上1 Mbps、サンプル点約55.6%）。Mainは1 Mbps、約88.2%。この設定差と今回の症状の因果関係は未確認。

ログは `build/hardware/power-live-20261121-192927/` の `COM15.txt`、`COM12.jsonl`、`swd-can.txt`、`swd-state.txt`、`swd-recheck.txt` と、`power-live-20261121-193035/` の再採取分。
