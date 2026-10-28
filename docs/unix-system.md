# モジュール式ドライバーとUNIX形式のシステム構成

2026-10-28。既存のハードウェアドライバーを `.EKM` へ分離し、FAT32上のUNIX形式のディレクトリ、EL0の `/bin/sh`、`/bin`・`/sbin` のELFコマンドを追加しました。

## ビルドと配置

```sh
make TARGET=hardware -j4
```

カーネル、全ドライバー、Newlibのユーザー実行ファイル、配置用の `build/rootfs/` を生成します。
個別には `make drivers`、`make users`、`make rootfs TARGET=hardware` を使えます。
`make drivers-native` は、このPC用にも全12モジュールを共有ライブラリーとしてビルドします。
EL1命令を使うタイマー・GPIO等の実行試験はEKITOS/QEMU内で行い、PCユーザー空間での直接呼出しは対象にしません。

1. `image/rpi4/hardware/kernel8.img` をPiのファームウェアが読むBOOTパーティションへ配置します。
2. `build/rootfs/` の内容を、EKITOSが `/` として使うFAT32ボリュームへ配置します。既存のファイルや設定を削除する必要はありません。
3. ABI 6のカーネルと `.EKM` を一緒に更新します。ABI 1〜5のモジュールは拒否します。

BOOT+DATA構成では、第2パーティションがFAT32ならDATAを `/` に選ぶ従来の規則を維持しています。
システム側の `/boot/kernel8.img` は配置用のコピーです。Piのファームウェアが読むBOOTパーティションと、VFSの `/boot` を自動的に同一視する機能はありません。
`build/rootfs/etc/` に既存ファイルがある場合、ビルドはその設定を上書きしません。
実機の起動と全デバイスの動作は、この環境では未検証です。

## ハードウェアドライバー

| ファイル | 内容 | 起動方法 |
| --- | --- | --- |
| `TIMER.EKM` | ARM汎用タイマーの割り込み設定 | 同梱イメージからロード |
| `GIC.EKM` | GICv2割り込みコントローラー | 同梱イメージからロード |
| `GPIO.EKM` | BCM2711 GPIO・端子機能・pull設定 | 同梱イメージからロード |
| `MAILBOX.EKM` | ファームウェアproperty通信・クロック・VL805 reset通知 | 同梱イメージからロード |
| `VIDEO.EKM` | EDID・ファームウェアframebuffer・6×8 ASCII端末 | `/lib/modules/`優先、同梱版にフォールバック |
| `UART.EKM` | PL011 / mini UART | 同梱イメージからロード |
| `SD.EKM` | SDカード・SDHCI・eMMC2の32-bit I/O | 同梱イメージからロード |
| `DWC2.EKM` | QEMU raspi4bのUSBホスト | QEMUでロード |
| `PI4USB.EKM` | BCM2711 PCIe・VL805 xHCI | Pi 4向けでロード |
| `HID.EKM` | Boot HIDキーボード・マウス | ロード |
| `STORAGE.EKM` | USBストレージBOT/SCSI | ロード |
| `HELLO.EKM` | Cドライバーのサンプル | 手動ロード |

ドライバー本体をカーネルへ静的リンクしません。最初の6個の `.EKM` ファイルを読み取り専用の起動リソースとしてカーネルイメージへ同梱し、ページ管理・MMUの初期化後に、通常と同じELF検査・再配置・W^X設定を経てロードします。
これにより、SDドライバーをロードするためにSDドライバーが必要になる循環を解消しています。
初期RAM情報はファームウェアのDTBまたはQEMUのATAGから読み、ドライバーに依存せずローダーを起動します。

後続のUSBモジュールは `/lib/modules/` を優先します。移行用に旧 `/DRIVERS/` も読めます。
ファイルが使えなければ、同梱した同じ形式の `.EKM` をロードします。HID・DWC2の組み込みCドライバーへ切り替える処理は廃止しました。
起動に必要なモジュールの更新時は `make` で同梱リソースも更新し、カーネルイメージを再配置してください。

```text
lsmod
insmod /lib/modules/HELLO.EKM
rmmod hello
```

`lsmod` にはQEMU起動時に10個のモジュールと `ABI=6` が表示されます。最大32モジュールです。
動作中のタイマー・GIC・GPIO・ファームウェア・UARTは中核サービスとして保持し、`rmmod` はEBUSYを返します。
SDなどのブロックドライバーも、マウントやファイルから参照されている間は解除できません。
USBクラス・ホストと追加ドライバーは、資源を停止・解放してから解除できます。

SDKには幅別MMIO、時刻、DMA、所有者付きIRQ登録・解除、UART受信キュー、GPIO、ファームウェア通信と中核サービス公開を追加しました。
IRQは所有者・callback領域・番号・priorityを検査し、解除時に割り込みを無効化してcallbackを外します。
ユーザープログラムの障害隔離とは異なり、`.EKM` は信頼するEL1コードです。
CPUの起動命令・カウンター読み出し、MMU、メモリー管理、スケジューラー、VFS、ファイルシステム、デバイス登録・入力キュー等のOS共通処理はカーネルに残ります。
これは既存ハードウェアのドライバー分離であり、新しいUSBクラスや未実装のデバイスを追加したという意味ではありません。

## ルートディレクトリ

```text
/
├── bin/             sh、一般コマンドのELF
├── sbin/            管理・診断コマンドのELF
├── etc/             profile、hostname、os-release、motd
├── lib/modules/     .EKM
├── boot/            カーネルの配置用コピー
├── dev/             デバイス名前空間用の予約ディレクトリ
├── proc/            プロセス情報用の予約ディレクトリ
├── tmp/
├── var/log/
├── var/tmp/
├── home/
├── root/
├── mnt/
├── media/
└── usr/{bin,sbin,lib,share}/
```

USBストレージの自動マウント先は `/media/usb0`、`/media/usb1` 等です。従来の `/usb0` 等から変更しました。`disks` で確認できます。

ファイルシステム自体はFAT32のままです。大文字小文字を区別するUNIXファイルシステムへの変換、UID/GID・実行permission・symlink・hardlinkの実装は含みません。
`/dev` と `/proc` は予約ディレクトリで、devfs/procfsの仮想ファイルはまだありません。`/tmp` はRAMディスクではなくFAT32上に保存されます。
起動時に不足した基本ディレクトリと既定設定を作成しますが、既存ファイルは上書きしません。読み取り専用媒体や既存の名前との衝突はログへ出します。

## シェルとELFコマンド

通常の対話環境は独立したEL0プロセス `/bin/sh` です。

```text
root@ekitos:/# ls
root@ekitos:/# cd /tmp
root@ekitos:/tmp# echo "hello world"
hello world
root@ekitos:/tmp# /bin/ls -l /lib/modules
```

プロンプトはユーザー名・ホスト名を緑、作業ディレクトリを青で表示します。`ls` のディレクトリも青です。
`export NO_COLOR=1` または `TERM=dumb` で色を無効化できます。
単引用符・二重引用符・エスケープ、`$NAME`・`$?`、`PATH`探索、環境変数、作業ディレクトリを実装しています。
`/etc/profile` からは `export` 行を読みます。子プロセスは親の作業ディレクトリを継承し、子の `chdir` は親を変更しません。

`/bin` の一般コマンド:

```text
ls cat echo pwd touch mkdir rm rmdir mv cp write append truncate
help env uname clear sleep true false
```

`/sbin` の管理・診断コマンド:

```text
display time ps procs mem uart usb input disks lsmod insmod rmmod mount umount
selftest memtest usertest oomtest gpio stop resume kill tsleep
```

43コマンドを、それぞれ通常のAArch64 ELF64 ET_EXECファイルとして配置します。実装は `usr/programs/commands.c` を共有し、各ファイルを独立したプロセスとして起動します。
診断用 `/sbin/unix-probe` と `/bin/sh` を含めて45個のELFです。拡張子 `.elf` は不要です。
通常シェルに `echo` 等の隠れた組み込み版はなく、ファイルを移動・削除するとPATHから実行できなくなります。
管理コマンドもEL0の実行ファイルで、型付きの管理syscallを通じてカーネルへ要求します。
`ls -l` の所有者・mode表示は、この単一ユーザーFATシステム向けの表示であり、アクセス権検査の実装ではありません。

`history` は履歴の管理、`cd`、`export`、`unset`、`exit` はシェル自身の状態を変更するため組み込みです。既存の `run`・`spawn`・`wait` も、シェルが子プロセスを管理する組み込みとして維持しました。
`sleep SECONDS` は自分を待機させるUNIX形式です。従来の別タスクを待機させる操作は `tsleep ID MILLISECONDS` です。
`cat` はファイルのバイト列を出力し、改行がないファイルに改行を追加しません。
`cp` はmount番号とinode番号で同じファイルへのコピーを拒否し、別マウントで同じinode番号のファイルを混同しません。

パイプは最大8段に対応します。fork/execve・リダイレクト・job control・glob展開・一般のシェルスクリプトは未実装です。
`sh -c 'COMMAND ARGS'` はコマンドとパイプを実行できます。未対応の演算子は実行前に拒否します。
全プロセスは現在の単一ユーザーシステム上で動くため、管理syscallのUID/capabilityによる権限制御はまだありません。
`/bin/sh` が存在しない場合や媒体がない場合は、カーネルの回復用シェルが起動し、明示的に `recovery mode` と表示します。

## 検証

```sh
make test
make test-unix-qemu
make test-qemu
make test-modules-qemu
make test-usb-qemu
make test-storage-qemu
make test-superspeed-qemu
```

`QEMU=/absolute/path/to/qemu-system-aarch64` を指定できます。
QEMUでは、カーネルのsymbol表から物理ドライバー本体が分離されていること、実際の `.EKM` ロード、中核モジュールの解除拒否、IRQの所有者・番号・重複・解除を検査します。
通常シェルでのELF実行、PATHと実行ファイル移動、cwd/環境変数、色の有無、syscallの不正ポインター、子のcwd分離、回収と欠損時の回復シェルも検査します。
USB 2 HID・ストレージとSuperSpeedは、従来の実デバイスモデルの試験を継続します。
Pi 4実機のGIC/UART/SD/PCIe/VL805の組み合わせは未検証です。検証記録は [unix-system-validation.txt](unix-system-validation.txt) を参照してください。

履歴・補完・HDMIとABI 5の追加については [端末・HDMI](terminal-hdmi.md) と [今回の検証記録](terminal-hdmi-validation.txt) を参照してください。上記のunix-system-validation.txtはABI 4時点の記録です。

ABI 6・FD継承/dup/pipe・IPC・描画APIの追加と現行の制限は [IPC・ダブルバッファ](ipc-display.md) を参照してください。
