# Enhanced KITOS — Phase 1 / Phase 2

Raspberry Pi 4B / AArch64 向けのカーネルです。Phase 0では既存KITOSの起動・割込み・プリエンプティブタスク・UART・SD/FAT32・GPIOを維持し、プラットフォーム、デバイス、時刻、スケジューラ、VFSの責務を分離しました。

Phase 1・2では、物理ページ管理、上位仮想アドレスの共通カーネル、独立したEL0空間、メモリ保護、例外隔離、syscallを追加しました。標準CライブラリはNewlibを採用し、ELF64ローダとの接続はPhase 3で行います。

現在の設計、主要API、検証結果、制約は [docs/phase1-2.md](docs/phase1-2.md)、Phase 0の記録は [docs/phase0.md](docs/phase0.md) にまとめています。旧KITOSのアプリと説明書は [usr/legacy](usr/legacy) に保存しており、現行カーネルにはリンクしません。

## ビルド

必要なツールはGNU Makeと`aarch64-elf-gcc`/Binutilsです。

```sh
make PLATFORM=rpi4 TARGET=qemu -j4
make PLATFORM=rpi4 TARGET=hardware -j4
```

既定ターゲットは`hardware`です。`CROSS_COMPILE=aarch64-none-elf-`などでツールチェーンを変更できます。
出力は`build/rpi4/<target>/kernel8.elf`と`image/rpi4/<target>/kernel8.img`です。両ターゲットの成果物は混ざりません。
`make TARGET=qemu clean`はそのターゲットのビルドだけを消します。

## テストとQEMU

ホスト試験にはCコンパイラ、Python 3、mtools (`mformat`, `mcopy`)が必要です。
QEMU実行には`raspi4b`対応の`qemu-system-aarch64`が必要です。

```sh
make test                  # block, timer, SD/SDHCI, FAT32/VFS, pages, DTB/ATAG
make test-qemu             # MMU/EL0/障害隔離/OOMと、SD→再起動→外部FAT検査
make run                   # 専用テストイメージで対話実行
```

試験イメージは`build/tests/`配下に生成します。既存の`image/arm64/sd.img`は使用しません。
`test-qemu`は専用の一時イメージを作り、ログを`build/tests/qemu-transcript.txt`へ保存します。
`fsck.fat`またはmacOSの`fsck_msdos`があれば、試験終了後に読み取り専用の外部整合性検査も実行します。

QEMUのmini UARTには`-serial null -serial stdio`を使用します。DTBファイルの手動取得はこの試験では不要です。
別のイメージを対話実行する場合は`make run SD_IMAGE=/absolute/path/to/copy.img`と指定できます。

## カーネルモニタ

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

`cd`, `pwd`, `touch`, `stop`, `resume`, `sleep ID MS`, `kill`も使用できます。
空白を含む名前・本文は引用符で囲みます。モニタはEL1の診断用で、ユーザーシェルやsyscall APIではありません。
`selftest`は専用タスクで切替・sleep・停止/再開・kill・タイマー解除・タスク枠再利用を検査します。ファイルには書き込みません。
`memtest`はページ管理・権限・ユーザーメモリコピーを、`usertest`はEL0のsyscall・独立空間・権限違反・資源回収を検査します。
`mem`は物理ページ統計を表示します。`oomtest`は自動試験用の16 MiB制限DTBで使う診断です。
ストレージが使えなくてもモニタは起動します。

## 実機

ファームウェアから`kernel8.img`を0x80000へロードする方式を維持しています。
MMUとD-cacheが無効なfirmware handoffを前提とし、起動コード内で上位仮想アドレスへ移行します。
実機用成果物をBOOTへコピーし、既存のPi 4ファームウェア構成を使用してください。
`arm_64bit=1`, `kernel=kernel8.img`, `enable_uart=1`が基本です。
PL011をGPIO14/15へ使用するため、Bluetoothとの競合を避ける構成（例:`dtoverlay=disable-bt`）を使用してください。
UARTは115200 baud / 8N1。カーネルはmailboxでUARTクロック48 MHzを設定し、eMMC2クロックを取得します。

FAT32は直接ボリュームまたはMBRのプライマリパーティションを読みます。
既存のBOOT+DATA構成に合わせ、**第2パーティションがFAT32なら優先**し、そうでなければ最初のFAT32パーティションを選びます。
固定LBAの再コンパイルは不要です。別配置は`fat32_mount_options`による明示LBA指定を利用できます。

この環境で実機の動作試験は行っていません。実機向けビルド成功と、QEMU上の機能確認は別の結果です。
現在はsingle core、4 KiBページ、firmwareが返す低位ARM RAM bankを使用します。FP/SIMD、ASID最適化、COW、Demand Pagingは未実装です。
ELF64ロード・一般のプロセス管理・NewlibのリンクはPhase 3の対象です。UEFI x64/Pi 5の実装はまだありません。
