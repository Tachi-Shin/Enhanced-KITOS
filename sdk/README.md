# EKITOS C driver SDK v6

PC上でCドライバーを開発し、AArch64用の `.EKM` ファイルをEKITOSへロードする仕組みです。
同じソースをPCの共有ライブラリーにもビルドして、MMIO/DMAを差し替えた試験に利用できます。
今回はEKITOS内のCコンパイラーは含みません。Linuxの `.ko` やmacOSのドライバーとはバイナリー互換ではありません。

## ビルドと配置

リポジトリのルートで実行します。

```sh
make TARGET=hardware -j4
make drivers
make drivers-native
```

| 出力 | 用途 |
| --- | --- |
| `image/rpi4/hardware/kernel8.img` | Pi 4のBOOTへ配置するカーネル |
| `build/drivers/{TIMER,GIC,GPIO,MAILBOX,UART,SD}.EKM` | タイマー・IRQ・GPIO・ファームウェア・UART・SDの起動用モジュール |
| `build/drivers/VIDEO.EKM` | HDMI framebuffer・EDID・6×8 ASCII端末 |
| `build/drivers/DWC2.EKM` | QEMU USBホスト |
| `build/drivers/PI4USB.EKM` | BCM2711 PCIe + VL805 xHCIホスト |
| `build/drivers/HID.EKM` | Boot HIDキーボード・マウスのクラスドライバー |
| `build/drivers/STORAGE.EKM` | USB Mass Storage BOT/SCSIのブロックドライバー |
| `build/drivers/HELLO.EKM` | ロード・解除のCサンプル |
| `build/drivers/native/*.so` | PCのテスト用ライブラリー。macOSでは拡張子.soのMach-O dylib |

**EKITOSが `/` としてマウントするFAT32ボリューム**の `/lib/modules/` に `.EKM` を配置します。`make` で `build/rootfs/` に全ファイルをまとめられます。
BOOT+DATA構成で第2パーティションがFAT32の場合、`/` はDATAです。カーネルを置くBOOTと同じとは限りません。
起動時に `/lib/modules/HID.EKM` と `/lib/modules/STORAGE.EKM`、実機では続いて `/lib/modules/PI4USB.EKM` を自動ロードします。
ファイルが使えなければ同梱の `.EKM` をロードします。基幹の6モジュールも同じELFローダーで起動リソースからロードし、QEMU raspi4bのUSBホストも `DWC2.EKM` です。
既存ハードウェアの全モジュール、起動依存関係、配置は [システム構成](../docs/unix-system.md) を参照してください。

シェルからも操作できます。

```text
lsmod
usb
input
insmod /lib/modules/HELLO.EKM
rmmod hello
rmmod boot-hid
insmod /lib/modules/HID.EKM
rmmod pi4-usb
insmod /lib/modules/PI4USB.EKM
```

`rmmod`にはファイル名ではなくmanifestの名前を渡します。USB workerがgateを使用中なら `-16` (EBUSY) を返すので再試行します。
USBホストの解除は配下のデバイスも解除します。操作にはUARTを使ってください。
HID解除時には押下中のキーを解放し、再ロード時は既存の未バインドinterfaceに再接続します。

## 最小のCドライバー

`drivers/modules/hello.c` が動作確認済みのサンプルです。基本形は次の通りです。

```c
#include <ekitos/driver.h>
static const struct ek_driver_api *api;
static int start(const struct ek_driver_api *provided)
{
    if (!provided || provided->abi != EK_DRIVER_ABI ||
        provided->size < sizeof(*provided)) return -EK_EINVAL;
    api = provided;
    api->log("my driver started", 0, 0);
    return 0;
}
static int stop(void) { return 0; }
EK_DRIVER_EXPORT const struct ek_driver_manifest ek_driver_manifest = {
    EK_DRIVER_MAGIC, EK_DRIVER_ABI, sizeof(struct ek_driver_manifest),
    "my-driver", start, stop
};
```

新しいソースを指定して独立したドライバーファイルを生成します。カーネルへの再リンクは不要です。

```sh
make -f sdk/driver.mk DRIVER=MYDRV SOURCES=/absolute/path/to/my_driver.c
make -f sdk/driver.mk native DRIVER=MYDRV SOURCES=/absolute/path/to/my_driver.c
```

外部プロジェクトから呼ぶ場合は `SDK_ROOT` にこのリポジトリの絶対パス、`SOURCES` にソースの絶対パスを指定します。
`BUILD_DIR`, `CROSS_COMPILE`, `NATIVE_CC` を変更できます。空白を含まないビルドパスを使用してください。
Newlibやホストのlibcをターゲットモジュールへリンクする必要はありません。
利用可能なfreestanding helperは `lib/memcpy.c`, `memset.c`, `memcmp.c`, `string.c` です。
浮動小数点/SIMD、TLS、外部共有ライブラリー、コンストラクターは使いません。

## APIと寿命

`include/ekitos/driver.h` が入口です。`abi` と `size` を検査してから表の関数を利用します。
ABI v5の構造体（USB host/device/interface、input eventを含む）を変更するときは、ABIの更新とモジュールの再ビルドが必要です。
関数は成功時0、失敗時は `include/ekitos/error.h` の負の値を返します。転送長は `done` で返します。
このエラー番号はEL0/Newlibの `errno` と別です。

- 時刻・sleep・ログはカーネルのサービスを使います。通常のCソースにアーキテクチャ依存命令は不要です。
- `dma_alloc(owner, &physical)` はゼロ化した連続4 KiBページを返し、`dma_free(owner, pointer)` で所有者が解放します。各モジュール256ページまで、ABI v5では物理1 GiB未満に限定します。bus addressへの変換はホストドライバーが行います。
- `dma_sync(pointer, size, EK_DMA_TO_DEVICE)` で書込みを公開し、`EK_DMA_FROM_DEVICE` でデバイスの結果を読みます。DMA用ページを他用途と共有せず、デバイスが所有中のデータを書き換えません。
- `read32`/`write32` はプラットフォームがマップした物理MMIOのみ利用できます。Pi 4のPCIe RCとVL805 BARを含みます。
- USBホストとクラスは `owner` と共に登録し、解除時に全callbackが停止したことを確認します。クラスのbind/poll/disconnectはUSB gate内で呼ばれ、転送関数もそこで使用します。
- `fini` は転送・callbackを停止してDMAを解放してから0を返します。停止できなければ非ゼロを返し、コードと所有メモリーを保持します。失敗したinitがDMAを残した場合もpinし、実行中のDMA先を解放しません。

これは信頼するEL1カーネルモジュール用のABIです。ユーザープロセスのような障害隔離や署名検証はありません。
APIにはUSB bulk/clear-halt/transaction gateと、所有者付きblock登録・解除があります。
blockを参照するマウント/ファイルがある間は解除を拒否し、offline objectもそのまま保持します。
`irq_register(owner, id, handler, context, priority)` / `irq_unregister` で所有者付きIRQを登録・解除できます。各モジュール8個までです。タイマーやUART等の使用済みIRQの横取りと、他モジュールの解除を拒否します。
`device_publish` でGPIO/UART/firmware/IRQ controller/timer/videoのバックエンドを公開できます。稼働中の中核サービスを公開したモジュールは解除を拒否します。全USBクラスの実装を提供するAPIではありません。
将来のEKITOS内コンパイラーは、このCヘッダーとELF出力契約を利用する形で追加できます。

## ELF契約と検証

`.EKM` はAArch64 little-endian ELF64 ET_DYN/PICです。`e_entry` は実行関数ではなく64-byteのmanifestを指します。
ロード前にELFの範囲・セグメント重複・W^X・ABI・entry関数の実行権限を検査し、
`R_AARCH64_RELATIVE` のみをモジュール専用領域へ再配置します。任意のカーネルsymbolを名前解決しません。
最大32モジュール、ファイル512 KiB、各イメージ1 MiBです。RX/R/RWを分離し、解放時はページ表と物理ページも回収します。

```sh
make test-drivers          # PCIeレジスタmock、ELF境界/変異、失敗注入 (ASan/UBSan)
make test-modules-qemu     # EKITOSで実際のAArch64コードをロード・解除
make test-xhci-qemu        # PC用にビルドした同じxHCI Cコード + QEMU virtのPCI USBモデル
make test-usb-qemu         # EKITOS/DWC2 + HID.EKM + コンソール/EL0入力
make test-storage-qemu     # STORAGE.EKM + FAT系/exFATの読書きと抜差し
```

`QEMU=/absolute/path/to/qemu-system-aarch64` で実行ファイルを指定できます。
`test-xhci-qemu` はqtest stdioを備えたQEMUを使用します。TCGを動かし、QMPはFIFOを利用します。
実機のPCIeレジスタはこのエミュレーターに存在しないため、BCM2711の初期化は別のmock試験です。

ABI 1/2/3/4/5の旧ファイルはABI 6カーネルから拒否されます。カーネル更新時は全 `.EKM` を再ビルド・再配置してください。
USBストレージの操作と制限は [対応記録](../docs/usb-storage.md) を参照してください。

ABI 3で追加したUSB_SUPER、16-bit EP0、SS endpoint companionを維持し、ABI 4で中核デバイスサービス・幅別MMIO・IRQ・UART/GPIO/firmwareのAPIを追加しています。
標準列挙とUSB 3の範囲は [SuperSpeed実装](../docs/usb-superspeed.md) を参照してください。

ABI 5は `firmware_request(tags, bytes)` と `framebuffer_map(owner, physical, bytes)` を末尾へ追加しています。
前者は16-byte整列DMAメッセージのタグ部分だけを渡すAPIです。入力タグは4-byte整列、12-byteヘッダー＋4-byte単位の値領域を連結し、最大4084 byte、終端タグとメッセージヘッダーは含めません。重複・長さ・要求フラグを検査し、MAILBOX.EKMが直列化・cache maintenanceします。未知のタグはタグ応答bitで判定します。タイムアウトしたDMAバッファは再利用しません。
後者はfirmwareが報告するVC RAM内のframebufferに限って最大64 MiBをNormal Non-cacheable・EL1 RW/NXでマップします。ARM RAMの通常マッピングとのalias、範囲外、他の所有者や2回目の要求は拒否します。初期化失敗時のマッピングは回収し、公開中のVIDEOサービスは再起動まで保持します。
`EK_DEVICE_VIDEO` の `write/poll` はコンソールワーカーだけから呼ばれ、`info` は短いスナップショット取得です。描画・scroll中も割込みを有効に保ちます。詳細は [端末・HDMI](../docs/terminal-hdmi.md) を参照してください。

ABI 6は `ek_video_ops` に `frame` と `text_mode` を追加します。コンソールworker専用で、frameは境界検証済みSHM readerからXRGB8888を受け、GPU back bufferへコピーしてflipします。`info.flags` の DOUBLE_BUFFER はreadback確認できた場合のみ有効です。[IPC・描画契約](../docs/ipc-display.md) を参照してください。
