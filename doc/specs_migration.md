# 共通仕様との管理境界

2026-11-17に、specsとMain/ステアFWの記述範囲を整理した。以前の2026-10-20の整理から、AtlasのCAN接続詳細をMainFW所有へ変更した。
判断基準は[specsの管理方針](../specs/README.md#新しい項目を置く際の判断基準)に従う。

## specsで管理する内容

- AI・CM4・Main・周辺基板の役割と制御分担：[システム構成](../specs/common/system_overview.md)。
- 上位指令の意味・単位・モード、CM4との転送：[制御パケット](../specs/common/control_packet.md)と[4WSのモード・SPI設計](../specs/proposals/control_modes.md)。
- Main→CM4→PCの帰還値の意味・配置：[共通feedback](../specs/common/feedback_packet.md)。
- システムの停止責務・校正情報の提供・電源互換：[Atlasの責務・互換設計](../specs/proposals/atlas_can.md)。
- 既存Orion/電源の互換性の根拠：[Orion CAN](../specs/common/orion_can.md)と[MCU更新](../specs/common/firmware_update.md)。既存定義は変更しない。

上位・システム契約とCAN接続方法を区別する。複数基板に影響するという理由だけで、CAN詳細をspecsへ複製しない。
例えば「Mainが校正情報を利用できる」はspecs、「約1秒ごとのCAN転送」はMainFW、保存方法はステアFWで管理する。

## MainFWで管理する内容

| 内容 | 正本/参照先 |
|---|---|
| Atlas CAN ID・予約・基板/軸/バス割当 | [Atlas CAN詳細案](design/atlas_can.md)、[ID一覧CSV](design/atlas_can_ids.csv) |
| Main―周辺基板のDLC・byte配置・opcode・CRC・分割・周期・帯域・校正転送 | 同上。まだID最終決定前・未実装 |
| Mainの受理条件・Orion解析との対応差 | [CAN受信](peripherals.md#can受信)、[基礎ドライバ](drivers.md) |
| ピン・HAL・DMA・IRQ・バッファ・モジュール・ビルド | [ハードウェア](hardware.md)、[コード階層](architecture.md)、[開発](development.md) |
| 検証記録・旧実装調査 | [開発](development.md)、[互換調査](driver_compatibility.md) |

ステアFWも同じCAN詳細を参照し、別のID表を手動で保守しない。共通の角度意味とCAN上の単位が異なる場合は、変換責務を接続仕様へ明記する。
MainのCAN接続仕様がspecsにある上位・停止・互換性要件を変更する場合は、specs側も同時に更新する。

## 現在の実装差

- CM4 SPIと通常モードの機体制御は未実装。Mainの通常制御入口は500 Hz。基板上のモーターデバッグは[本repoで実装・管理](atlas_bringup.md)。
- Orion解析は8 byte限定、NaN/Inf拒否。実機で観測した0x215/216のDLC4や0x244は状態へ反映されない。ステア新形式は未実装。
- CAN2高負荷時のRX破棄は未解決。詳細な検証状況は各実装文書で管理する。
- Orionの停止条件・UART・ブートローダーをAtlasへ適合済みとして転用しない。

## 参照方向

```text
Main/ステアFW → specsの上位・責務・安全・互換性
ステアFW → MainFWのCAN接続仕様
specsの概要 → MainFWのCAN詳細へのリンク
各FW → 自身の内部実装・操作・検証
```

規範を項目ごとに一つにし、具体的な値や表を複製しない。未実装案を現行実装と区別し、ID最終決定後に双方の実装と仕様を揃えて結合確認する。
