# Pi 4 PCIe/VL805 xHCI とCモジュール

2026-10-27。既存のPhase 0〜3/DWC2/HID実装を引き継ぎ、Pi 4 USB-A用のホストとCモジュール基盤を追加しました。
**Pi 4実機での動作は未検証です。** クロスビルド、BCM2711レジスタmock、QEMUの実際のPCI/xHCI/USBモデルを区別して検証しています。
全USBデバイス対応の完了ではありません。

## 実装

| ファイル | 内容 |
| --- | --- |
| `drivers/pci/bcm2711.c` | PCIe RC reset/link、config、BAR probe、outbound/inbound window、VL805 firmware handoff |
| `drivers/usb/xhci.c` | xHCI capability/legacy handoff、context、command/event/transfer ring、USB 2/3 root port、control、interrupt IN |
| `drivers/modules/pi4usb.c` | Pi 4でPCIe/xHCIを起動し、USB coreへホストを登録するモジュール |
| `drivers/modules/hid.c`, `hello.c` | HIDクラスモジュールと最小Cサンプル |
| `include/ekitos/driver.h`, `error.h` | バージョン付きC API、manifest、モジュール用エラー番号 |
| `kernel/module/image.c`, `loader.c` | ELF境界検査、PIC再配置、ABI検査、ロード/解除と所有資源管理 |
| `sdk/driver.mk`, `sdk/README.md` | PCでのCクロスビルド、nativeビルド、配置・ロード方法 |
| `tests/pcie_test.c`, `module_test.c` | レジスタmock、異常値・ELF変異・割当失敗試験 |
| `tests/xhci_bridge.c`, `tools/xhci_qtest.py` | 同じxHCIコードをPCで実行し、QEMUのMMIO/DMA/USBへ接続する試験adapter |
| `tools/module_qemu_test.py` | EKITOS内で実AArch64モジュールコードを実行する試験 |

BCM2711 PCIe RCは物理 `0xFD500000`、VL805 BAR0のCPU窓は `0x600000000` を使用します。
PCI側BARを `0xF8000000` に設定し、CPU側の64 MiB outbound windowへ接続します。
カーネルがMMIOとしてマップするBAR部分は先頭64 KiBで、より大きなBARは拒否します。
DMAはPCI bus 0 → CPU physical 0の1 GiB inbound windowで、SDKのDMA割当ても物理1 GiB未満に制限します。
PCIe/デバイス割込み・MSI/MSI-Xは無効化し、USB workerからポーリングします。

VL805 (vendor 1106 / device 3483) を確認し、PERST解除後にmailbox tag `0x30058` へBDF `0x100000` を通知します。
これはfirmwareの起動依頼で、EEPROMへの書込みは行いません。
mailbox共有bufferは4 KiB専用領域で、実行時のcache保守と直列化を追加しました。
mailboxがtimeoutした場合は遅延したfirmware書込み先を再利用せず、そのカーネル起動中の再要求を拒否します。

xHCIは32/64-byte contextを扱い、DCBAA、scratchpad（最大32）、command/event ring、device/endpoint ringを動的に所有します。
各ringは4 KiBで、producer/consumer cycle、Link TRB、ERDPを更新します。TRBのcycleはpayload公開後に渡し、control TD全体が完成してからSetupを公開します。
Enable Slot → Address Device → EP0のMaxPacket評価 → Configure Endpointの順に設定し、USB coreの論理アドレスとhardware Slot IDを分離します。
USB 2ハブのroute string、TT slot/port/think timeの設定も含みます。

制御転送はSetup/Data/Statusを一つの要求として扱い、short packetはData stageの残量で実長を計算します。
STALLではReset EndpointとSet TR Dequeue Pointerを発行し、後続の転送へ進めます。
interrupt INのNAKは未完了の要求として保持し、各pollで停止・再投入しません。
取り外し時はDisable Slot完了後にcontext/ring/bufferを解放します。
command/controlのtimeout時はcontrollerをhaltし、停止が確認できない間はDMAとモジュールを保持します。
controller自動再起動は未実装で、解除・再ロードまたは再起動で復旧します。

## Pi 4実機での確認

[SDKの配置手順](../sdk/README.md) に従い、`kernel8.img` と `/lib/modules/HID.EKM`, `/lib/modules/PI4USB.EKM` を配置します。
USB操作に先立ちPL011 UARTシェルを接続します。起動ログの確認点は次の通りです。

```text
[module] loaded boot-hid
[driver] VL805 PCIe link ready ...
[driver] xHCI started ...
[usb] host=VL805/xHCI
[module] loaded pi4-usb
```

1. USB-AへBoot HIDキーボードとマウスを直接接続し、`lsmod`, `usb` で登録・列挙を確認する。
2. キーボードで `echo usb-ok` を実行し、`input` でキーとマウスの移動・ボタンを確認する。
3. USB 2ハブ、USB 3コネクターのUSB 2経路、Low-speed機器を確認する。
4. 押下中の抜差し、ハブ配下の切断・再接続、UARTからのHID/ホスト解除・再ロードを確認する。
5. `mem`, `memtest`, `selftest` とUARTログを保存し、異常時はPCIe link、mailbox、xHCI start、device enumerationのどこで停止したかを確認する。

これらは実機でこれから行う手順です。QEMU raspi4bはBCM2711 PCIe/VL805をモデル化していません。
そのためnative xHCI試験はQEMU `virt` + `qemu-xhci` を使用します。EKITOSカーネルをvirtへ移植した試験ではありません。
この試験だけでPi 4の電源・配線・firmware・実ハブの互換性までは保証できません。

## 対応範囲

USB 2 Low/Full/High-speedのcontrolとinterrupt IN、および既存のBoot HID keyboard/mouseが今回の対象です。
QEMUではHigh-speed keyboard、Full-speed mouse/外部hub、route、STALL復帰を確認しています。
Low-speed実機、High-speed hubの実TT動作、VL805固有の挙動は未検証です。
USB-AのUSB 3コネクターでもUSB 2デバイスはUSB 2経路を利用しますが、SuperSpeed（5 Gbit/s）の列挙も後続の [USB 3実装](usb-superspeed.md) で追加しました。
BulkとMass Storageは後続の [USBストレージ実装](usb-storage.md) で追加しました。
Isochronous、network/audio/video、任意のHID Report Descriptor/Report ID/NKRO/JIS/IMEは今後の作業です。
ロード可能モジュールはEL1の信頼するコードで、Linux `.ko` バイナリーのロードやEKITOS内Cコンパイラーは含みません。

## 参照した一次資料

- [Intel xHCI specification](https://www.intel.com/content/dam/www/public/us/en/documents/technical-specifications/extensible-host-controler-interface-usb-xhci.pdf): register/context/TRBの形式。
- [Raspberry Pi Linux PCIe controller](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/pci/controller/pcie-brcmstb.c): BCM2711のregister定義とwindow/reset。
- [BCM2711 device tree](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/arch/arm/boot/dts/broadcom/bcm2711.dtsi): PCIe物理アドレスとoutbound range。
- [Raspberry Pi firmware reset driver](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/drivers/reset/reset-raspberrypi.c): VL805起動通知とBDF。
- [Circle PCIe implementation](https://github.com/rsta2/circle/blob/master/lib/bcmpciehostbridge.cpp): bare-metalでのregister順序の照合。
- [QEMU xHCI model](https://gitlab.com/qemu-project/qemu/-/blob/v10.2.2/hw/usb/hcd-xhci.c): 実際の転送/完了モデルを利用した検証。
