# USBストレージ・FAT系 / exFAT

2026-10-28。USB Mass Storageの読み書きと、FAT12・FAT16・FAT32・exFATを追加しました。
USBキーボード・マウスと既存のSD/FAT32実装、以前の未コミット変更は保持しています。
Pi 4実機の検証はまだ行えていません。DWC2ではEKITOSカーネル全体、xHCIでは同じCドライバー/FSコードとQEMUのPCI/USBモデルを検証しています。

## 利用方法

```sh
make TARGET=hardware -j4
make drivers
```

`image/rpi4/hardware/kernel8.img` をBOOTへ配置し、EKITOSが `/` としてマウントするFAT32上の
`/lib/modules/` へ `build/drivers/{HID,PI4USB,STORAGE}.EKM` を配置します。
通常は `build/rootfs/` の内容を配置してください。シェル・ELFコマンドと全モジュールを含みます。
**現在のドライバーABIは5です。カーネルと全モジュールを一緒に更新してください。**
BOOT+DATA構成では `/` がDATAの場合があります。既存のSD用FAT32の選択順序は変更していません。

起動時に `STORAGE.EKM` を読み込みます。ファイルが使えなければ同梱した `.EKM` をロードします。
接続した対応ストレージは自動検出・自動マウントされます。一般的なSDありの構成では `/media/usb0`、`/media/usb1`…です。
末端のマウントポイントはVFS内の仮想ディレクトリで、`ls /media` に表示されます。親の `/media` はシステムのディレクトリ構成で作成します。

```text
disks
usb
ls /media/usb0
cat /media/usb0/HELLO.TXT
write /media/usb0/test.txt "hello USB"
append /media/usb0/test.txt " + append"
mkdir /media/usb0/work
mv /media/usb0/test.txt /media/usb0/work/saved.txt
truncate /media/usb0/work/saved.txt 5
umount /media/usb0
mount 1 /media/usb0
```

`disks` はブロックデバイスID、容量（ブロック数）、セクターサイズ、書込み保護、offline、マウント先を表示します。
`mount DISK PATH` の番号は `disks` のIDです。FSはFAT12/16/32/exFATを自動判定します。
自動マウントは最初の対応ボリュームを選びます。直接ボリューム、MBRのプライマリ、GPTに対応し、複数パーティションをすべて同時に公開する機能はありません。
手動で `umount` した媒体は、その接続のまま自動再マウントしません。

ファイルを閉じて `umount /media/usb0` が成功してから取り外す運用を想定しています。
開いているファイルがあればアンマウントと `rmmod usb-storage` はEBUSYです。
`umount` 後にドライバーを解除・再ロードすることもできます。
FatFs側のrenameは既存の宛先を上書きせずEEXISTとします。ディレクトリを子孫へ移動して循環を作る操作は拒否します。

## 対応範囲

| 項目 | 状態 |
| --- | --- |
| USB Mass Storage BOT (subclass 06 / protocol 50), SCSI direct-access disk | 実装・QEMUで確認 |
| Bulk IN/OUT（QEMU DWC2 / VL805向けxHCI） | 実装・両バックエンドの実USBモデルで確認 |
| INQUIRY、TEST UNIT READY、REQUEST SENSE、READ CAPACITY(10/16) | 実装。Capacity16はmockでも境界確認 |
| READ/WRITE(10/16)、SYNCHRONIZE CACHE、MODE SENSE | 実装。Read16/Write16はmockで64-bit LBA確認 |
| 複数LUN、512/1024/2048/4096-byte logical block | 実装。512/4096はQEMUでも確認 |
| FAT12/16/32、exFATの読書き・UTF-8長い名前・ディレクトリ | 実装・hostおよびEKITOS USBで確認 |
| MBR / GPT、exFAT連続・FATチェーン、4 GiB超のoffset | 実装。sparseな4 GiB超ファイルをhostとEL0で確認 |
| 書込み保護、媒体交換・取り外し、開いたFDの寿命 | 実装・QEMU/失敗注入で確認 |
| USB 2 Full/High・USB 3 SuperSpeed | 実装。SSの範囲は [対応記録](usb-superspeed.md) |
| Pi 4実機のVL805、実USB-SATA/NVMeブリッジの互換性 | 未検証 |
| SuperSpeedPlus（10/20 Gbit/s）、UAS、CBI/UFI、ATAPI/CD-ROM、暗号化/TexFAT | 未対応 |

固定poolはUSB最大12デバイス、MSC最大8 interface context / 8 LUN object、block登録最大8（SDを含む）、
VFS同時マウント最大4（rootを含む）です。GET MAX LUNは0〜15を扱い、pool上限を超えるLUNは公開しません。
SuperSpeed（5 Gbit/s）のBOT/SCSIストレージにも対応しました。USB 2へ落とさずに動作を検証しています。UASは別のドライバーが必要です。
1つのUSBメモリが動いたことから、あらゆるUSBストレージの実機互換性を保証するものではありません。

## 構造と寿命

- `drivers/usb/msc.c`: BOTの31-byte CBW / data / 13-byte CSW、tag/residue検査、Reset Recovery、SCSI senseとLUN。
- `drivers/modules/storage.c`: C製ロードモジュール `STORAGE.EKM`。PCで開発・クロスビルドし、`insmod` でロード可能。
- `drivers/usb/{core,dwc2,xhci}.c`: bulk転送、endpoint halt解除、USB transactionのgate。
- `drivers/block/block.c`: removable/offline/RO、世代番号、registryと参照数。
- `kernel/storage/storage.c`: USB gateを離れた場所でFSの自動マウント・offlineアンマウントを処理。
- `fs/fat/`, `third_party/fatfs/`: FatFs R0.16を使うVFS、disk I/O、LFN workspace adapter。

VFS I/OはUSB gateを取得してBOTのCBW〜CSW全体を直列化します。
USB class callbackは同じgate内で実行し、そこでVFSを呼びません。これによりUSBとVFSの待ち合わせ循環を避けます。
切断・media-change senseを受けた旧block objectはofflineにし、古いVFS handleはENODEVを返します。
参照が残る間はmoduleとdevice objectを保持し、再接続した別媒体へ旧FDを付け替えません。
FDを閉じると旧マウントを回収し、次の接続で枠を再利用します。

成功したFS変更はFatFsのclose/syncとSCSI SYNCHRONIZE CACHEを通します。書込みエラー後はそのFSへの追加変更を止めます。
FAT/exFATはジャーナルを持たず、書込み途中の抜去・電源断の原子性や修復機能は提供していません。
RTCがまだないためタイムスタンプは固定の2025-01-01です。exFATのaccess timeも有効な日付として保存し、別実装のチェックを通しています。

## 検証

```sh
make test
make test-storage-qemu
make test-storage-xhci
make test-superspeed-qemu
make test-exfat-large
make test-modules-qemu
make test-usb-qemu
make test-qemu
```

`QEMU=/absolute/path/to/qemu-system-aarch64` を指定できます。試験用イメージは `build/tests` 内だけに作成します。
物理USBやユーザーのSDイメージは使用しません。`test-exfat-large` の8 GiBイメージはsparseで、4 GiBのデータを生成しません。

FAT12/16/32はmtoolsで独立にフォーマットします。exFATはhost fixtureのFatFs formatterを利用でき、
独立検証では `relan/exfat-utils` 1.4.0のformatter/checkerも使用します（カーネルにはリンクしません）。
今回取得・検証したarchiveは以下です。

- URL: <https://github.com/relan/exfat/releases/download/v1.4.0/exfat-utils-1.4.0.tar.gz>
- SHA-256: `241575fa93104406a47e79e53e4d907bae69886f11621f70a45276c62b75bf69`

再現する場合は `build/deps` 内へ展開し、その `exfat-utils-1.4.0` ディレクトリで `./configure` と `make -j4` を実行します。
ビルド済みなら試験が自動で利用します。`test-exfat-large` はこの独立formatterを必要とします。

USB 2段階の結果は [USBストレージ検証記録](usb-storage-validation.txt)、ABI 4/SSの結果は [追加記録](usb-superspeed-validation.txt) を参照してください。
FatFsの出典・ライセンスとローカル変更は [vendored source記録](../third_party/fatfs/README.md) にまとめています。

一次資料: [USB-IF BOT仕様](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)、
[Microsoft exFAT仕様](https://learn.microsoft.com/en-us/windows/win32/fileio/exfat-specification)、
[FatFs公式更新履歴](https://elm-chan.org/fsw/ff/updates.html)。
