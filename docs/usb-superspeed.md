# USB 3.0 SuperSpeed（5 Gbit/s）

2026-10-28。Pi 4の青いUSB-A端子で使うSuperSpeedの経路を実装しました。
**Pi 4実機は未検証です。** 同じC実装をQEMUのUSB 3/xHCIモデルで動かし、USB 2への切替をせずにストレージの列挙・exFAT読書き・切断復帰を検証しています。

## 更新方法

```sh
make TARGET=hardware -j4
make drivers
```

`image/rpi4/hardware/kernel8.img` と、マウント対象FAT32の `/lib/modules/` に置く
`HID.EKM`, `PI4USB.EKM`, `STORAGE.EKM` を一緒に更新してください。
**現在のドライバーABIは5です。ABI 1/2/3/4のファイルはロードを拒否します。**
SS companion/512-byte EP0に続き、全ハードウェアドライバーのモジュール化でAPIを拡張しました。
シェル・コマンドを含む [システム構成](unix-system.md) の `build/rootfs/` を配置してください。

起動後は `lsmod` で `ABI=5`、`usb` で `speed=3(SuperSpeed)` を確認します。
ストレージは `disks`, `ls /media/usb0`, `cat`, `write`, `umount` など、USB 2と同じ操作で利用します。

## 実装した内容

| 項目 | 内容 |
| --- | --- |
| xHCI Supported Protocol / PSI | USB 2/3の論理port対応、Protocol Slot Type、標準/非標準Speed IDを解釈 |
| USB 3 root port | Hot Reset、Inactive/Compliance/CASからのWarm Reset、LPM timeout無効化 |
| EP0 / device / BOS | SSのbMaxPacketSize0=9を512 bytesへ変換。BOSとSS capabilityを検査 |
| SS endpoint companion | 必須の直後配置、長さ、MaxBurst、属性、BytesPerIntervalと範囲を検査 |
| endpoint context | SS BulkのMPS=1024、最大16 packets/burst、interruptのinterval/ESIT payloadを設定 |
| SS hub標準要求 | descriptor 0x2A、SET_HUB_DEPTH、下流port power、U1/U2 timeout、SS port change/reset |
| BOT/SCSIストレージ | 1024-byte endpointでFAT12/16/32・exFATを利用する共通経路 |
| 切断中I/O | root切断時はENODEVを返し、DMAをDisable Slotまで保持。他portのHIDを継続 |
| pipe recovery | Halted endpointはReset Endpoint、Stopped/IdleはDrop+Addでsequence/toggleをリセット |

USB 2とUSB 3で同じ列挙コード `drivers/usb/enumerate.c` を利用します。
ネイティブ試験用adapterもこのコードを使うため、試験専用の別のUSB 3列挙処理は持ちません。
`drivers/usb/xhci_protocol.c` はport番号やSlot Typeを固定値とせず、capabilityから取得します。

U1/U2の省電力モードは有効化せず、U0で転送します。USB 3のsequence番号はxHCIが管理します。
エラー回復時に既にIdle/Stoppedのendpointも再設定し、デバイスのCLEAR_FEATUREとhost側sequenceを揃えます。

切断を検出して途中の要求から戻る場合も、公開済みTDのDMA領域をすぐには再利用しません。
USB coreのdetachがDisable Slotの完了を確認してから解放します。
切断と無関係のcontroller timeoutでは、従来どおりhalt確認を優先し、停止できなければDMAとmoduleを保持します。

## 検証方法と限界

```sh
make test-superspeed
make test-superspeed-qemu
make test-storage-xhci
make test-storage-qemu
make test-usb-qemu
make test-modules-qemu
make test-qemu
```

`QEMU=/absolute/path/to/qemu-system-aarch64` を指定できます。
`test-superspeed-qemu` は既定のUSB 3 portを有効にしたQEMU `qemu-xhci` と `usb-storage` を使います。
portのSpeed ID=4、EP0=512、Bulk MPS=1024、MaxBurst=15（16 packets）を実際のcontroller contextでも検査します。
USB 2キーボード・マウスとの混在、読書き、ROでの再起動、3回の抜差し、CSW異常からの復帰、
ファイル読込み途中の物理切断とENODEV、他portでのキーボード入力継続を確認します。

- Pi 4実機のPHY、配線、電源、VL805固有挙動、実機の転送速度は未検証です。5 Gbit/sは対応するリンク方式で、実測スループットではありません。
- QEMUの `raspi4b` はPCIe/xHCIを持たないため、EKITOS全体の試験はDWC2/USB 2、SS試験はPCでビルドした同じCソース＋QEMU `virt` のPCIモデルです。
- QEMU標準hubはUSB 2です。SS hubの標準要求・速度判定はmockで確認していますが、実USB 3 hubの動作は未検証です。
- SuperSpeedPlus（10/20 Gbit/sや複数lane）、UAS/streams、isochronous、USB省電力は未対応です。
- 対応クラスは引き続きBoot HIDとBOT/SCSIストレージです。カメラ・音声・LAN等には別のクラスドライバーが必要です。

配置・FS操作は [USBストレージ](usb-storage.md)、C開発は [SDK](../sdk/README.md)、結果は [SS検証記録](usb-superspeed-validation.txt) を参照してください。

## 一次資料

- [Intel xHCI仕様](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf): Supported Protocol/PSI、slot/endpoint context、port reset、pipeのsequence reset。
- [Microsoft SS endpoint companion定義](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/usbspec/ns-usbspec-_usb_superspeed_endpoint_companion_descriptor): descriptorの形式。
- [Linux USB hub protocol definitions](https://github.com/torvalds/linux/blob/master/include/uapi/linux/usb/ch11.h): USB 3 hubの要求とport status/change定数。
- [QEMU USB descriptor実装](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/usb/desc.c): 実USB 3モデルによるBOS/companion応答。
