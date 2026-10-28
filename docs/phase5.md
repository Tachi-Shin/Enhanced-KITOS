# Enhanced KITOS Phase 5 — USB

2026-10-27。ユーザーの指示によりPhase 4を保留し、USB対応を先行します。
最初の対象はユーザーが選択したキーボード・マウスです。既存のPhase 0〜3の変更を保持しています。

## 現在の到達点

| 項目 | 状態 |
| --- | --- |
| USBホスト/クラス分離、USB 2/3標準列挙 | 実装済み |
| SuperSpeed 5 Gbit/s、BOS/companion/USB 2混在 | 実装。同じCコードを実USB 3のQEMUモデルで検証。実機未検証 |
| QEMU raspi4bのDWC2、制御転送・interrupt IN | 実装済み、実際のQEMU USBデバイスで確認 |
| Full-speedハブ、ネストしたハブ、接続・切断・枠再利用 | 実装済み、QMPで抜差しを確認 |
| Boot HIDキーボード・マウス | 実装済み、シェル入力とマウスイベントを確認 |
| EL0からの入力イベント取得 | 実装済み、Newlibを使う実ELFで確認 |
| High-speed root直結キーボード、Full-speed root直結マウス | QEMUで確認 |
| Low-speed実デバイス | host側speed設定はあるが、今回のQEMU機種では未検証 |
| Pi 4向けPCIe/VL805 xHCI | 実装済み。PCIeはmock、同じxHCIコードはQEMU PCIモデルで確認。Pi 4実機は未検証 |
| Cドライバーファイル `.EKM` / SDK | 実装済み。HID・PCIe/xHCIを独立ビルド、ロード/解除/再ロードを確認 |
| USB Mass Storage BOT/SCSI、FAT12/16/32・exFAT | 実装。範囲と検証は [USBストレージ](usb-storage.md) |
| 任意のReport HID、通信、音声、カメラ等 | 未実装。全USBデバイス対応の完了ではない |

Pi 4のUSB-AはPCIe接続のVL805が担当します。QEMUの`raspi4b`にはPCIe Root Portがなく、
DWC2のUSB 2.0ホストが提供されます。この二つを同一のハードウェアとして扱いません。
hardware targetの`platform_usb_host()`はNULLを返し、代わりに起動タスクが `/lib/modules/PI4USB.EKM` をロードします。
このモジュールがBCM2711 PCIeを設定し、VL805 firmware handoff後にxHCIを登録します。QEMU用DWC2のレジスタは使いません。
配置とモジュールABIは [C driver SDK](../sdk/README.md)、実装・実機確認手順は [PCIe/xHCI](pcie-xhci.md) を参照してください。

## 構造と寿命

| 場所 | 責務 |
| --- | --- |
| `include/drivers/usb/usb.h` | ホストops、デバイス/インターフェース/endpoint、クラス登録契約 |
| `drivers/usb/core.c` | アドレス割当て、controlの各stage、設定選択、ハブ探索、切断と再利用 |
| `drivers/usb/descriptor.c` | 長さ・interface/endpoint数・番号重複等の境界検査 |
| `drivers/usb/dwc2.c` | QEMU DWC2のroot port、単一channel DMA、有限timeout/NAK |
| `arch/arm64/kernel/dma.c` | DMA専用bufferのcache clean/invalidateとbarrier |
| `drivers/usb/hid.c` | Boot protocol選択、interrupt IN、接続解除時の押下解除 |
| `drivers/usb/hid_report.c` | ハードウェア非依存のBoot report decode（US文字変換はkeymap.c） |
| `drivers/usb/input.c` | キー/相対ポインターevent queueと文字queue、wire形式への変換 |
| `include/uapi/input.h`, `usr/runtime/kitos.h` | EL0入力ABIとNewlib側の入口 |
| `tests/usb_test.c`, `tools/usb_qemu_test.py` | ASan/UBSan host試験、QMPによるUSB入力・抜差し試験 |

USB workerは保護されたカーネルタスクです。ホスト転送とクラスcallbackを一つのgateで直列化し、
IRQから待機・転送を行いません。USBホストのロードが成功したときにworkerを作り、解除中は休止します。
classのbind/poll/disconnect callbackだけが`usb_control`/`usb_interrupt_in`を呼ぶ契約です。
USB入力queueはIRQマスク下で操作し、コンソールやsyscallからの取得と競合しないようにしています。

DWC2はカーネル所有の4 KiB専用DMA bufferと一つのchannelを使います。
bufferはカーネルBSSの予約領域に含まれ、他のデータとcache lineを共有しません。
DMA前後でcacheを保守し、QEMUのGPU bus aliasへ物理アドレスを変換します。
NAKは未入力として扱い、data toggleは成功したpacketだけ更新します。
timeout後はchannelを停止させ、停止を確認できない場合は以後の転送を止め、同じbufferの再利用を防ぎます。
この場合の自動コントローラ再初期化は未実装です。

オブジェクトは固定poolで、最大12デバイス、各4インターフェース・4 endpoint、設定descriptorは最大1024 bytes、
ハブは16 port・深さ4までです。最初のconfigurationのalternate setting 0を利用します。
不正/上限超過のdescriptorは拒否します。未対応interfaceは`driver=unbound`として見える状態で保持します。
ハブの子の列挙失敗は同じ接続のまま無限再試行せず、再接続時に再試行します。
切断は子孫を先に解除し、押下中のキー/ボタンを解放してからデバイス枠を再利用します。

## キーボード・マウスとEL0入力

対象はHID subclass 1のBoot keyboard (protocol 1)とBoot mouse (protocol 2)です。
SET_PROTOCOLでBoot形式を選び、キーボードのmodifier/6-key、mouseのボタンと相対移動を読みます。
重複キー・同じ押下状態は二重に通知せず、ErrorRollOver等は以前の押下状態を破壊しません。
文字変換はUS配列、Shift/Ctrl/Caps Lockに対応します。Caps Lockはソフトウェア状態で、LED出力は未実装です。
JIS配列・IME・自動key repeat・任意のHID Report Descriptor/NKROは未実装です。

入力eventは64件、文字queueは128 bytesです。満杯時は新しい入力を捨て、欠落数を`input`で表示します。
文字queueは既存のUART入力が空の場合に利用され、シェルとユーザーの標準入力へ接続されます。
キー/マウスevent queueは文字queueとは独立し、`input`診断とEL0の全readerで共有します。
readerごとの配信や権限管理、POSIXの`/dev/input`は未導入です。

syscall 18 `KITOS_SYS_INPUT_READ(buffer, capacity)`は最大16件を非待機で読み、件数を返します。
空なら0、容量超過はEINVAL、不正bufferはEFAULTです。容量0はbufferを参照しません。
buffer全体を書込み可能か検証してからeventを取り出すため、不正bufferでqueueを消費しません。
48-byte固定幅の`kitos_input_event`に時刻、device、type、usage/button、modifier、press/release、
x/y/wheel、flagsを格納し、内部構造体やポインタは公開しません。

```c
struct kitos_input_event events[KITOS_INPUT_BATCH_MAX];
int count = kitos_input_read(events, KITOS_INPUT_BATCH_MAX);
/* count > 0: eventsを処理。0: 入力なし。-1: errnoを参照。 */
```

## 検証方法

```sh
make test-usb
make test-usb-qemu
make test
make test-qemu
make TARGET=hardware -j4
```

USB専用QEMU試験は、生成した一時SDイメージと仮想ハブ/keyboard/mouse/tabletを使います。
QMPはstdio、UARTはFIFOを利用し、実機USBのpassthroughや既存SDイメージは使用しません。
QMPで入力を与えて、USB packet→HID decode→シェルコマンド/EL0 ELFまで通過したことを検査します。
serial transcriptは`build/tests/usb-qemu-transcript.txt`です。
DWC2/HIDの検証結果は [Phase 5検証記録](phase5-validation.txt)、追加したPCIe/xHCI・モジュールの結果は [追加検証記録](pcie-xhci-validation.txt) を参照してください。

## 次の実装段階

1. Pi 4実機でPCIe/VL805 xHCIとUSB-Aの直接接続、ハブ、抜差し、再ロードを検証する。
   実機のUSB-Aを利用するために必要です。QEMU DWC2の成功で実機対応済みとはしません。
2. xHCI経由の実機keyboard/mouse・hub・USB 2/3混在、切断中転送・STALL回復の検証。
3. HID Report Descriptor parserとReport ID/NKRO/ゲームパッド/タブレット、JIS配列・LED出力。
4. 実USBストレージの互換性拡大、UAS/SuperSpeedPlus、CDC ACM/ECM等の通信クラス。
5. Isochronous転送と帯域調停を必要とするAudio/UVC等、追加クラスと機器固有ドライバー。

「あらゆるUSBデバイス」には共通ホスト/バスだけでなく、各クラスのプロトコル、独自仕様の機器向け
ドライバーも必要です。今回はその最初のHID経路を実装し、残る対応を上記の段階に分けています。

## 参照した一次資料

- [USB-IF HID 1.11](https://www.usb.org/sites/default/files/documents/hid1_11.pdf): Boot keyboard/mouse、class requests。
- [QEMU Raspberry Pi仕様](https://github.com/qemu/qemu/blob/v10.2.2/docs/system/arm/raspi.rst): USB hostと未実装PCIe。
- [QEMU 10.2.2 DWC2 model](https://github.com/qemu/qemu/blob/v10.2.2/hw/usb/hcd-dwc2.c): DMA、channel状態、resetとport。
- [QEMU 10.2.2 USB hub](https://github.com/qemu/qemu/blob/v10.2.2/hw/usb/dev-hub.c): 仮想hubの機能と要求。
- [Raspberry Pi USB構成の説明](https://github.com/raspberrypi/documentation/issues/1838): Pi 4のPCIe/VL805。

QEMUの実行環境と対応するソース`/Users/shingo/PD3/qemu-10.2.2`も読み、registerと仮想機器の動作を照合しました。

USB 3の対応範囲とABI 4更新は [SuperSpeed](usb-superspeed.md) を参照してください。

UART・GPIO・SD・IRQ・タイマーを含む全ハードウェアバックエンドの `.EKM` 化、DWC2モジュール、EL0シェルとELFコマンドは [システム構成](unix-system.md) を参照してください。
