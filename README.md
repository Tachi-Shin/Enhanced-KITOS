# Enhanced KITOS — モジュール式ドライバー / UNIX形式のシェル

Raspberry Pi 4B / AArch64 向けのカーネルです。Phase 0では既存KITOSの起動・割込み・プリエンプティブタスク・UART・SD/FAT32・GPIOを維持し、プラットフォーム、デバイス、時刻、スケジューラ、VFSの責務を分離しました。

Phase 1・2では、物理ページ管理、上位仮想アドレスの共通カーネル、独立したEL0空間、メモリ保護、例外隔離、syscallを追加しました。Phase 3の静的ELF64ロード、spawn/wait、プロセス別FD・heap、Newlib接続、FP/SIMD状態保存も実装されています。

各段階の設計は [Phase 0](docs/phase0.md)、[Phase 1・2](docs/phase1-2.md)、[Phase 3](docs/phase3.md) にまとめています。作業再開時のGit状態、実装済み・部分実装・未着手の整理と再検証結果は [引継ぎ記録](docs/recovery-2026-10-27.md) を参照してください。旧KITOSのアプリと説明書は [usr/legacy](usr/legacy) に保存しており、現行カーネルにはリンクしません。
Phase 3の不具合修正・境界試験・Newlib再ビルドの結果は [Phase 3検証記録](docs/phase3-validation.txt) に記録しています。

Phase 4を保留し、Phase 5のUSB対応を先行しています。USB列挙・ハブ・Boot HIDキーボード/マウス・EL0入力イベント、USB Mass StorageとFAT12/16/32・exFATの読書きに加え、Pi 4向けPCIe/VL805 xHCI・SuperSpeed（5 Gbit/s）とCドライバーファイルのロード機構を実装しています。
**Pi 4実機でのUSB-A動作は未検証です。** 同じxHCI CコードをQEMUのPCIモデルで検証しています。全USBデバイスへの対応は今後クラスごとに広げます。対応範囲と残作業は [Phase 5](docs/phase5.md)、ストレージの配置・操作は [USBストレージ](docs/usb-storage.md)、USB 3の範囲は [SuperSpeed](docs/usb-superspeed.md) を参照してください。

現在はUART・GPIO・SD・GIC・タイマー等を含む既存ハードウェアドライバーを `.EKM` に分離し、FAT32上のUNIX形式ディレクトリと、EL0の `/bin/sh`・`/bin`・`/sbin` のELFコマンドを利用します。詳細と配置方法は [システム構成](docs/unix-system.md) を参照してください。

履歴（↑/↓・永続保存）、Tabでのコマンド/パス補完と候補表示、6×8 ASCIIのHDMI端末を追加しました。UARTと同時出力し、起動時にEDIDの推奨解像度・周波数とRGB範囲を設定します。**Pi 4実機HDMIは未検証**です。使い方と対応範囲は [端末・HDMI](docs/terminal-hdmi.md) を参照してください。

実機UARTで入力できない場合に備え、受信FIFOのポーリング補助経路と `/bin/sh` の起動失敗理由の表示を追加しました。screenの設定・配線・BOOT/DATAへの配置は [UART入力の確認手順](docs/uart-input.md) を参照してください。

## ビルド

必要なツールはGNU Make、Python 3と`aarch64-elf-gcc`/Binutilsです。ユーザーELFにはNewlibを使用します。

```sh
make PLATFORM=rpi4 TARGET=qemu -j4
make PLATFORM=rpi4 TARGET=hardware -j4
make drivers              # 全12モジュール（通常のmakeでも生成）
make update-system-image  # system.imgをバックアップして実行ファイルを更新
```

既定ターゲットは`hardware`です。`CROSS_COMPILE=aarch64-none-elf-`などでツールチェーンを変更できます。
出力は`build/rpi4/<target>/kernel8.elf`、`image/rpi4/<target>/kernel8.img`、配置用の`build/rootfs/`です。カーネルのターゲット別成果物は分離し、rootfsの`/boot/kernel8.img`は最後に選んだターゲットのコピーになります。
`make TARGET=qemu clean`はそのターゲットのビルドだけを消します。

`make users`で`build/usr/bin/sh`、43個の一般・管理コマンド、診断用ELFと`build/usr/probe.elf`を生成します。通常の`make`でも実行し、UNIX形式の`build/rootfs/`を構成します。
初回は`curl`でNewlib 4.6.0.20260123を取得し、固定SHA-256を確認して`build/newlib/`へビルド・インストールします。既存のホスト環境へはインストールしません。
`make newlib`でライブラリ本体を再ビルドできます。別の作業ディレクトリとインストール先を使う場合は、`NEWLIB_BUILD_DIR`と`NEWLIB_PREFIX`に絶対パスを指定します。

## テストとQEMU

ホスト試験にはCコンパイラ、Python 3、mtools (`mformat`, `mcopy`)が必要です。
QEMU実行には`raspi4b`対応の`qemu-system-aarch64`が必要です。

```sh
make test-terminal-qemu    # 履歴・Tab・USB矢印・実際のフレームバッファ画素
make test                  # block, timer, SD/SDHCI, FAT32/VFS, pages, DTB/ATAG, ELF
make test-unix-qemu        # モジュール式起動・EL0シェル・UNIXルート・ELFコマンド
make test-qemu             # 上記のQEMU回帰、ELF/Newlib/process/FP、SD永続化
make test-usb              # USBディスクリプタとBoot HIDのhost試験
make test-usb-qemu         # 仮想USB機器の列挙・入力・EL0・ハブ・抜差し
make test-drivers          # PCIe mock・ドライバーELF境界試験
make test-modules-qemu     # Cドライバーのロード・解除・HID再ロード
make test-xhci-qemu        # 同じxHCI CコードをQEMUのPCIモデルで検証
make test-storage-qemu     # USBストレージ、FAT12/16/32/exFAT、抜差し
make test-storage-xhci     # xHCI + BOT/SCSI + exFATの同じCコード
make test-superspeed-qemu  # 実際のUSB 3接続・混在・抜差し・エラー復帰
make run                   # 専用テストイメージで対話実行
```

試験イメージは`build/tests/`配下に生成します。既存の`image/arm64/sd.img`は使用しません。
`test-qemu`は専用の一時イメージを作り、ログを`build/tests/qemu-transcript.txt`へ保存します。
ユーザーELFのビルドと試験媒体への配置も自動で行います。`make run` は新規の `build/tests/system.img` にシェル・コマンド・モジュールを配置します。既存イメージへの自動コピーは行いません。
QEMU試験には、プロセス・FD・argv/envpの上限、open中のunlink、入力待ち中のkill、heap縮小・OOM後の再試行も含みます。
`fsck.fat`またはmacOSの`fsck_msdos`があれば、試験終了後に読み取り専用の外部整合性検査も実行します。

QEMUのmini UARTには`-serial null -serial stdio`を使用します。DTBファイルの手動取得はこの試験では不要です。
別のイメージを対話実行する場合は`make run SD_IMAGE=/absolute/path/to/copy.img`と指定できます。

## シェル

```text
help
ps
selftest
mem
memtest
usertest
time
uart
ls /
cat /HELLO.TXT
mkdir /work
write "/work/Long Name.txt" "hello KITOS"
append "/work/Long Name.txt" " + VFS"
truncate "/work/Long Name.txt" 5
mv "/work/Long Name.txt" /saved.txt
rm /saved.txt
rmdir /work
gpio 17 out
gpio 17 1
gpio 17 in
gpio 17 up
```

`cd`, `pwd`, `touch`, `stop`, `resume`, `sleep SECONDS`, `tsleep ID MS`, `kill`も使用できます。
空白を含む名前・本文は引用符で囲みます。通常シェルはEL0の `/bin/sh` で、一般・管理コマンドは `/bin`・`/sbin` のELFです。
`selftest`は保護されたコンソールワーカーで切替・sleep・停止/再開・kill・タイマー解除・タスク枠再利用を検査します。ファイルには書き込みません。
`memtest`はページ管理・権限・ユーザーメモリコピーを、`usertest`はEL0のsyscall・独立空間・権限違反・資源回収を検査します。
`mem`は物理ページ統計を表示します。`oomtest`は自動試験用の16 MiB制限DTBで使う診断です。
媒体に配置したELFは`run /PROBE.ELF hello`で実行・終了待ちできます。
`spawn PATH [ARGS...]`は待たずに起動し、`wait PID`で終了待ち、`procs`でプロセス状態を表示します。
試験プログラムは`/PROBE.ELF`という配置名で自身の子プロセスを起動します。
ストレージや`/bin/sh`が使えない場合は回復用シェルが起動します。
`usb`はUSBトポロジとクラスドライバー、`input`は保留中のキー・マウスイベントと欠落数を表示します。
BootキーボードはUS配列の文字入力としてシェルおよびユーザーの標準入力に接続します。
EL0では`kitos_input_read()`でマウス等の入力イベントを非待機で読めます。
`disks`で媒体とマウント先、`umount /media/usb0`で安全な取り外し、`mount 1 /media/usb0`で手動マウントできます。
`lsmod`, `insmod /lib/modules/HELLO.EKM`, `rmmod hello`でCドライバーファイルを管理できます。
PCでのC開発・ビルド・配置方法は [C driver SDK](sdk/README.md) を参照してください。

## 実機

ファームウェアから`kernel8.img`を0x80000へロードする方式を維持しています。
MMUとD-cacheが無効なfirmware handoffを前提とし、起動コード内で上位仮想アドレスへ移行します。
実機用成果物をBOOTへコピーし、既存のPi 4ファームウェア構成を使用してください。
`arm_64bit=1`, `kernel=kernel8.img`, `enable_uart=1`が基本です。
PL011をGPIO14/15へ使用するため、Bluetoothとの競合を避ける構成（例:`dtoverlay=disable-bt`）を使用してください。
UARTは115200 baud / 8N1。カーネルはmailboxでUARTクロック48 MHzを設定し、eMMC2クロックを取得します。

FAT32は直接ボリュームまたはMBRのプライマリパーティションを読みます。
既存のBOOT+DATA構成に合わせ、**第2パーティションがFAT32なら優先**し、そうでなければ最初のFAT32パーティションを選びます。
このマウント対象のFAT32に`build/rootfs/`の内容を配置します。ドライバーは`/lib/modules/`、シェルとコマンドは`/bin`・`/sbin`です。ABI 6のカーネルと全ドライバーを一緒に更新してください。媒体がなくても、同梱した `.EKM` でUART・タイマー等を起動できます。
固定LBAの再コンパイルは不要です。別配置は`fat32_mount_options`による明示LBA指定を利用できます。

この環境で実機の動作試験は行っていません。実機向けビルド成功と、QEMU上の機能確認は別の結果です。
現在はsingle core、4 KiBページ、firmwareのDTB/ATAGで記述された低位ARM RAM bankを使用します。ASID最適化、COW、Demand Pagingは未実装です。
ユーザー空間は静的AArch64 ET_EXECと限定的なsyscallを対象とし、fork/execve、動的リンク、thread/TLS、一般のsignal配信は未実装です。
VFSは固定poolとopen中のnamespace変更制限を持ちます。旧アプリのEL0移植、UEFI x64/Pi 5の実装もまだありません。

IPC（pipe/channel・名前付き接続・event・共有メモリー）、シェルの `|`、HDMIダブルバッファとEL0のacquire/present/releaseを追加しました。使用例・上限・配置・検証は [IPCとダブルバッファ](docs/ipc-display.md) を参照してください。
