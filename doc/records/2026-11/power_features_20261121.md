# 電源機能・操作モード・OFW更新のホスト確認

2026-11-21。Main `f5a7f18`。Debug／Releaseを `./Script/build.ps1 -Configuration Debug -Rebuild` と `./Script/build.ps1 -Configuration Release -Rebuild` でクリーンビルドした。ReleaseのELF SHA256は `c1b473c10f01f5ad077444dad037eea28a0d7efbd99bb5daca20533663d734d2`。

- `python ./Script/test_drivers.py` の全9項目が成功。電源保護値5項目の起動時送信、CAN2マウスの6 byte受信と期限切れ、手動キッカーの100 V指令／50%の単発指令、モード離脱時の充電停止をHALモックで確認した。ドリブラーはCAN1帰還ありで動作し、電源通知の途絶で停止した。電源Faultは両CANへの共通通知、PowerOff、中央長押しでのリセット要求を確認した。
- 電源FWはRTTのバージョン照会、出力状態が0に揃うまで待機、2ブロックの送信、ブロック確定ACK欠落後の位置照会、再起動後の通知とビルドID・CRC32C照合を模擬ノードで検証した。PC側のCRC32Cは `123456789` の既知値と、ローカル `F303_boost_app.bin` のメタデータCRC `FD351EE0` に一致した。
- Main／電源基板への書込み、実機の昇圧・発射、ドリブラー出力、電源FW更新は今回未実施。4WSのPB3 `ESC_PWM` に接続する機器の制御条件は未確定で、ドリブラー試験はOrion互換CAN1機器を対象とする。

[モード割当と電源FWコマンド](../../power_bringup.md)
