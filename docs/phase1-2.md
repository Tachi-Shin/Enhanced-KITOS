# Enhanced KITOS Phase 1 / Phase 2

この文書と`phase1-2-validation.txt`はPhase 1・2完了時点の記録です。現在はPhase 3のELF/Newlib・プロセス管理・FP/SIMD保存も実装されており、現状は [Phase 3](phase3.md) と [引継ぎ記録](recovery-2026-10-27.md) を参照してください。

2026-10-27。ユーザー確認済みの「TTBR1の上位共通カーネル＋TTBR0のタスク固有ユーザー空間」を実装。
物理ロード位置は従来の`0x80000`を維持します。標準Cライブラリは**Newlib**を採用する方針です。
本段階はメモリ保護とEL0/syscall基盤までで、ELF64ロード・一般のプロセス管理・NewlibのリンクはPhase 3です。
Phase 0の設計記録は [phase0.md](phase0.md)、今回の実行結果は [phase1-2-validation.txt](phase1-2-validation.txt)。

## 構造と移植境界

| 場所 | 責務 |
| --- | --- |
| `kernel/mm/page.c` | 物理ページの予約・確保・解放・統計。CPUのページテーブル形式を知らない |
| `kernel/mm/boot_memory.c` | 境界検証付きDTB v17/ATAG予約領域reader |
| `kernel/mm/memory.c` | platformが供給したRAM/予約領域とarchの初期化を接続 |
| `kernel/mm/usercopy.c` | ユーザー範囲と全ページの権限を検証し、カーネルの物理写像を介してコピー |
| `arch/arm64/kernel/paging.c` | AArch64 descriptor、TTBR、TLB、cache maintenance、ページテーブル寿命 |
| `arch/arm64/boot/start.S`, `linker/kernel.ld` | 低位起動、VMA/LMA分離、MMU移行、上位PC/SP/VBAR |
| `arch/arm64/kernel/exception.S/.c` | 全整数レジスタの退避・復帰、EL0開始、SVC、ユーザー障害とEL1障害の分離 |
| `kernel/syscall/syscall.c`, `include/uapi/` | CPU固有frameに依存しないsyscall処理と公開ABI |
| `kernel/sched/task.c` | カーネル/ユーザータスク、空間切替、停止・終了・資源回収 |
| `platform/rpi4/common/memory.c` | ARM用RAMのmailbox照会、Pi/QEMU起動情報、使用するMMIOページ |
| `arch/arm64/include/arch/` | Makefileが選択するmemory/user/IRQ/context/timer/exception/I/O/CPU契約 |
| `tests/memory_regression.c`, `tests/user/probe.S` | EL1診断と、libc不要の組込みEL0試験コード |
| `tools/boot_fixture.py`, `tools/qemu_test.py` | DTB・低メモリfixtureと全体回帰 |

共通カーネルと汎用UART/SDHCIのincludeから、ARM専用IRQ/context/timer/MMIOヘッダへの直接参照を除去しました。
`-Iarch/$(ARCH)/include`で実装を選び、ホスト試験は`tests/support/arch/irq.h`で割込み状態を模擬します。
ARMレジスタ形式、EL0復帰方法、TLB無効化はarch側、BCM2711のアドレスとfirmwareはplatform/driver側にあります。

Pi 5では新しいplatform/driver選択、RAM/起動情報アダプタ、MMIO一覧が必要です。
x64/UEFIではarch実装、UEFI memory map・ExitBootServicesを扱うboot/platform、割込み/clock/devicesが必要です。
本段階でこれらの移植先を実装済みとはしていません。現在の単一RAM bank入力や4 GiBまでのallocator metadataも、
その段階で複数bank/大容量向けに拡張する必要があります。

## 起動とアドレス空間

```text
firmware / QEMU: kernel8.img -> physical 0x00080000
  low boot trampoline (MMU off, D-cache off, IRQ masked)
  EL2 -> EL1h when needed; secondary cores remain parked
  initialize temporary L0/L1 tables, MAIR and TCR
  TTBR0: temporary identity map; TTBR1: KERNEL_BASE + PA
  enable MMU; branch to high PC; install high SP and VBAR
  clear BSS; boot-only mailbox, UART, RAM/reservation discovery
  build final TTBR1 permissions and an empty TTBR0
  replace roots, invalidate TLB, enable I/D caches and SCTLR.WXN
  remove kernel stack guard pages; start IRQs and scheduler
```

`KERNEL_BASE = 0xffff000000000000`。通常カーネルのVMAはこの値＋物理アドレス、
LMAは物理アドレスです。ELF entryは`0x80000`、通常textのVMAとLMAは別の値です。
`objcopy`はLMAから従来どおり小さいraw imageを作ります。

ブート中だけ広い1 GiB block写像を使い、通常運用は4 KiBの4段ページテーブルに切り替えます。
恒等写像を外す時点でPC・SP・VBARはすべて上位アドレスです。
一時的な低位例外ベクタも用意し、移行途中の予期しない例外は停止させます。
MMUまたはD-cacheが有効な状態での直接handoffとEL3起動は未対応で、低位停止ループへ入ります。

| 領域 | 最終権限/属性 |
| --- | --- |
| カーネルtext | EL1 RX、EL0アクセス不可、UXN |
| rodata / 組込みEL0 fixture原本 | EL1 RO、PXN/UXN |
| data/BSS、通常RAM、kernel/user stackの物理alias | EL1 RW、PXN/UXN |
| UART/GPIO/mailbox/SDHCI/eMMC2/GICの必要ページ | Device-nGnRnE、EL1 RW、PXN/UXN |
| ブート・通常8タスク・idleのkernel stack直下 | guard page、未マップ |
| 低位カーネル恒等alias / page zero | 未マップ |
| ユーザーtext | EL0 RX、EL1実行禁止(PXN)、書込み禁止 |
| ユーザーdata/stack | EL0 RW、PXN/UXN |

通常RAMはNormal WBWA、inner-shareable。TTBR0のuser leafはnon-globalです。
ユーザーのRWX要求は拒否します。ユーザー実行ページにもカーネルのRW/NX物理aliasがあり、
ELFなどのロード時にカーネルが内容を用意できます。カーネルtextの書込み可能aliasは残しません。
ユーザーコード公開時はD-cacheをPoUへcleanしてI-cacheをinvalidateします。
mailboxはcache有効化前の起動時だけ使い、メッセージには物理アドレスを渡します。

## 物理ページと予約領域

- `page_init`は確認済みRAM bankと予約範囲を受け取る起動専用API。4 KiB未満の端や部分予約ページも除外。
- 利用可能bitmapと確保中bitmapを分け、カーネル/firmwareの予約ページを`page_free`で解放できないようにしています。
- `page_alloc`は物理アドレス、枯渇時は0。`page_free`は未確保、非整列、範囲外、二重解放を拒否。
- ユーザー用ページとページテーブルは公開前にzero-fill。生の`page_alloc`は内容を保証しません。
- カーネル全体、boot stack、spin/handoff領域、DTB自身、memreserve、固定`/reserved-memory/reg`、initrdを予約。
- 固定firmware予約領域は最終RAM direct mapからも除外。`no-map`の有無によらず保守的に扱います。
- DTBの動的CMA要求（`size`のみ）は固定のfirmware所有領域ではないため、CMAを作成しません。
  非空`ranges`を持つ予約領域のアドレス変換は未対応として起動を拒否します。
- QEMUの`-dtb`なし起動はATAGを使うため、その経路でもinitrd予約を読み取ります。
- RAM容量はfirmware mailboxのARM memory応答から取得。推測・破壊的probeはしません。
  QEMU `raspi4b -m 2g`では低位ARM bankの960 MiBを管理します。高位bankやVideoCore RAMは利用しません。

現在はsingle core、IRQマスクによる直列化です。bitmapの容量は4 GiB分、入力は単一bankです。
汎用heap/slab、連続大ブロックallocator、huge pages、swapは未導入です。

## VM APIと寿命

`struct vm_space`は所有する変換rootを持ち、コピーや共有はしません。
`vm_space_create`、`vm_map_new`、`vm_query`、`vm_unmap`、`vm_space_destroy`を公開します。
タスク作成成功時に`task_create_user`が所有権を受け取り、呼出し側のrootを0にします。
失敗時の所有権は呼出し側に残ります。

`vm_map_new`は初期化済みの新規ページだけをマップし、重複、非整列、page zero、範囲外、RWXを拒否します。
途中でページテーブルの確保に失敗すると、作成済みの空tableとdata pageを巻き戻します。
`vm_unmap`はPTEを無効化してTLBを消してから物理ページを解放し、空tableも回収します。
`vm_space_destroy`はその空間が非activeであることが契約です。

schedulerはIRQマスク下でTTBR0を切り替えてからkernel contextを切り替えます。
EL1タスク/idleはempty TTBR0です。ASIDは0で、切替ごとに全EL1 TLBを無効化します。
終了したタスクの空間は別のkernel stackへ移ってから回収します。終了時のstatus/ESR/FAR/PCは
タスク枠の再利用まで保持します。静的kernel stack自体は解放せず、枠再利用時にcontextを再初期化します。

ユーザーメモリ操作は空間ごとに1実行thread、変更はkernel callerが直列化する契約です。
`vm_user_check`は全ページの権限と加算overflowを検査し、`copy_from_user/copy_to_user`は検証後に
カーネルalias経由でコピーします。範囲の後半が不正でも前半を書き換えません。
user VAをEL1から直接dereferenceしないため、user pointerの不備がkernel faultになりません。

COWではpage reference countと共有所有権、Demand PagingではVMA/欠落ページの解決、
SMPではlock・ASID管理・TLB shootdownが必要です。これらを実装したように見せるstubは置いていません。

## EL0、例外、syscall

例外frameは288 bytes。`x0..x30`, `SP_EL0`, `ELR_EL1`, `SPSR_EL1`, ESR, FARを保持します。
IRQ、SVC、ユーザー障害はいずれも専用kernel stackへ入ります。EL0開始時はx0の指定引数以外の
整数レジスタをzeroにし、EL0t/IRQ有効で`eret`します。復帰レジスタの設定中はIRQをマスクします。

SVC中は全user状態が保存済みなのでIRQを許可し、preemption/sleepが可能です。
タイマーIRQでは従来どおりGIC EOI後に切り替えます。
EL0のdata/instruction abortや特権命令は当該タスクだけを終了し、モニタは継続します。
EL1自身の同期例外、未対応vector/SErrorは診断して停止します。
FP/SIMD状態の保存は未実装なのでFP/SIMDを無効にしており、EL0での使用は終了対象です。
EL0からのDAIF、counter/timer、cache maintenanceも直接許可しません。

AArch64 ABIは`svc #0`、x8=番号、x0..x5=引数、x0=結果です。

| 番号/定数 | 引数と結果 |
| --- | --- |
| 0 `KITOS_SYS_EXIT` | x0=32-bit終了status、復帰しない |
| 1 `KITOS_SYS_WRITE` | x0=1/2(console)、x1=user buffer、x2=byte数。最大4096、結果は転送数 |
| 2 `KITOS_SYS_YIELD` | CPUを譲り0を返す |
| 3 `KITOS_SYS_SLEEP_MS` | x0=uint32ミリ秒、0はyield、結果0 |
| 4 `KITOS_SYS_GETPID` | 現在のtask IDを返す |
| 5 `KITOS_SYS_CLOCK_MS` | x0=書込み可能なuint64出力先、単調時刻msを書き、結果0 |

公開エラーは`include/uapi/error.h`で定義し、**Newlibのgeneric errno**に合わせています。
例えばENOSYSは88です。カーネル内部の`kernel/error.h`（ENOSYS=38等）とは別契約で、
境界で変換します。未知番号・`svc #0`以外は公開ENOSYS、無効ポインタはEFAULTです。

Newlibの実装/ビルドや独自libcの作成は今回行っていません。
Phase 3ではNewlib側のwrapperが負のsyscall結果を`errno`と`-1`へ変換し、
`_write/_read/_open/_close/_lseek/_fstat/_isatty/_sbrk/_exit/_getpid`等を接続します。
FD表、heap/VMA、ELF segmentの権限とzero-fill、argv/envp/初期stack、終了待ちの寿命、
Newlib reentrancy、FP/SIMDコンテキストの扱いもPhase 3で整備します。
現行のVFSやfreestanding kernel用の小さなmemory/string helperはユーザーlibcとは別です。

## 検証

- `make TARGET=qemu -j4` / `make TARGET=hardware -j4`: warningsをerrorにしたビルド。
- `make test`: 既存Block/FAT32/VFS/Timer/SD/SDHCIにpage allocatorとDTB/ATAG境界試験を追加。
- `memtest`: CPUのAT/PARによる権限検査、VM独立性、zero-fill、境界/RO/RWX、copyの全体検証、解放。
- `usertest`: EL0 syscall、全非結果GPRとSPの保持、sleep、独立TTBR0、yieldしない2タスクのpreemption、
  stop/resume/kill、kernel/text/NX/guard/NULL/MMIO/恒等alias/特権命令の障害隔離、資源回収。
- `oomtest`: 利用可能なfree pageが4096以下のfixtureでだけ実行。途中table割当ての各段階で
  ENOMEMを起こし、失敗後のfree数と再試行を検証。通常RAM量では拒否します。
- `tools/qemu_test.py`: SD専用イメージで旧機能を回帰し、上記試験を反復。
  ATAG、DTB固定予約、16 MiB制限、SD媒体なし、SD書込み後再起動＋外部FAT検査を含みます。
- 既存の実機用`image/arm64/bcm2711-rpi-4-b.dtb`を使ったQEMU起動とmemtest/usertestも確認。

実機用は**ビルド確認のみ**です。実機のMMU/cache/mailbox/UART/eMMC2動作をQEMUの結果で保証しません。
実機に移す際は最新imageで起動し、`memtest`, `usertest`, `selftest`と専用媒体でのVFS回帰を行ってください。

## 参照仕様

- [Arm: Memory management](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/LearnTheArchitecture-MemoryManagement-101811_0100_00_en.pdf)
- [Arm: AArch64 Exception Model](https://documentation-service.arm.com/static/67ac57fb091bfc3e0a9479cc)
- [Devicetree: Flattened format](https://devicetree-specification.readthedocs.io/en/stable/flattened-format.html)
- [Devicetree: Device nodes / reserved memory](https://devicetree-specification.readthedocs.io/en/stable/devicenodes.html)
- [Raspberry Pi firmware property mailbox](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)
- [QEMU 10.2 property implementation](https://github.com/qemu/qemu/blob/v10.2.0/hw/misc/bcm2835_property.c)
- [Newlib OS interface / syscalls](https://sourceware.org/newlib/libc.html#Syscalls)
- [Newlib generic errno definitions](https://github.com/mirror/newlib-cygwin/blob/master/newlib/libc/include/sys/errno.h)
