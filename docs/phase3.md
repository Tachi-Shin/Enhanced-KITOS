# Enhanced KITOS Phase 3

2026-10-27。ELF64静的実行ファイル、独立プロセス、Newlibを接続する。
以下の主要経路は既に実装され、作業再開時の全再コンパイル・ホスト試験・QEMU回帰に成功しました。
Git状態と実行結果の詳細は [引継ぎ記録](recovery-2026-10-27.md) を参照してください。
その後の仕上げでは、境界条件の不具合修正、Newlib本体の新しい作業ディレクトリでの再ビルドとQEMU回帰まで完了しました。
最終結果は [Phase 3検証記録](phase3-validation.txt) を参照してください。

## 実装済みの範囲

- ELF64 little-endian ET_EXEC/AArch64をVFSから読む。header/program header、範囲・重複・整列・entry・権限を検証。
  PT_LOADを個別ページに展開し、BSSをzero-fill。失敗は作成途中の空間ごと破棄する。
- 専用ユーザースタックにargc/argv/envp/auxvを作る。ELFとheap、stackの配置を分離する。
- spawn/waitで親子・zombie・終了statusを管理。終了・fault・killでFDとVMを回収し、親終了時は子を孤児として回収する。
  fork/COW、dynamic linker、TLS/thread、signalは後続の対象。
- プロセスごとのFD表、consoleとVFSのread/write/open/close/seek/stat/isatty/unlink、brk、spawn/wait。
  kernel構造体をuser ABIへ直接公開せず、固定幅のwire構造体を使う。
- Newlib 4.6.0.20260123を専用prefixにビルドし、crt0とsyscall adapterで接続。
  printf/malloc/stdio/file/errno/終了処理を実際のELFで検証する。
- AArch64 FP/SIMDとFPCR/FPSRを例外frameで保存・復元し、通常のNewlibとユーザーコードを実行可能にする。

## 当初の実装順序

1. ELF検証とload、初期stack、hostの不正ELF試験。
2. プロセス寿命とFD/heap、syscallを接続。
3. NewlibとユーザーELFの再現可能ビルド、シェルrun/wait。
4. 専用SDイメージのELF起動・並列実行・fault/kill・FD/VM回収と既存回帰。
5. 両ターゲットの警告なしビルド、仕様・制約・結果を記録。

物理ロード位置、TTBR1/TTBR0の方針、既存FAT32形式を維持する。
汎用ELF検証・プロセス・syscallと、AArch64 entry/FP状態およびplatformを分離する。

## 現在の実装と契約

| 場所 | 実装 |
| --- | --- |
| `kernel/process/elf.c`, `elf_load.c` | ELF検証、PT_LOADのページ展開・zero-fill・最終権限設定 |
| `kernel/process/process.c` | argv/envp/auxv、spawn/wait、親子・終了status・孤児・FD回収 |
| `kernel/process/fd.c`, `syscall.c` | FD、VFS入出力、stat wire構造体、brk、ユーザー引数検査 |
| `kernel/sched/task.c` | プロセスとタスクの結合、VM回収、resource pin中のkill延期 |
| `arch/arm64/kernel/exception.S` | q0〜q31・FPCR/FPSRを含む816-byte frameの保存・復元、EL0開始時の初期化 |
| `usr/runtime/` | crt0、Newlib syscall/reentrant hook、ユーザーリンカスクリプト |
| `usr/programs/probe.c`, `fp.S` | libc・ファイル・親子関係・終了処理・FP/SIMDの試験ELF |
| `usr/programs/process_limits.c` | タスク/FD枯渇・再利用、引数境界、入力待ちkill、heap縮小/OOM後再試行 |
| `tools/process_test.py` | 不正ELF、低メモリload失敗、反復実行・並列FP・ページ回収のQEMU検査 |

ELFはlittle-endian AArch64 ET_EXEC、program header最大16個、ロードページ総量16 MiBまでです。
イメージは`0x400000`以上`0x20000000`未満で、ページを共有するsegment、RWX、実行可能stack、
PT_DYNAMIC/PT_INTERP/PT_TLSを拒否します。ユーザースタックは64 KiB、heap上限は16 MiBです。
argvは最大16個、envpは最大16個、文字列は合計4096 bytesです。

通常タスクは最大8枠で、init・シェル・reaper等も含みます。プロセスごとのFDは最大16個ですが、
VFSのfile poolもシステム全体で16個です。stdin/stdout/stderrはconsoleへ新規接続し、親のFDを継承しません。
タスク枠の不足は`EAGAIN`、プロセスのFD枯渇は`EMFILE`、システム全体のfile pool枯渇は`ENFILE`です。
プロセスごとのcwdを持ち、相対パスはcwd基準です。spawnで継承し、子のchdirは親に影響しません。
`wait`は通常終了を`(status & 255) << 8`、killを9、ユーザーfaultを11で返します。
一般のsignal配信はなく、kill syscallは直下の子に対するsignal 0と9だけです。

既存の0〜5番syscallに加え、6〜17番でread/open/close/lseek/fstat/stat/isatty/brk/spawn/waitpid/unlink/killを提供します。
公開定義は`include/uapi/`、NewlibのC構造体・open flagsからの変換は`usr/runtime/newlib.c`にあります。
ユーザー空間はプロセスごとに独立し、各プロセス内は単一threadです。

## 残る制約

- `_fork`, `_execve`, `_link`, `_gettimeofday`, `_times`は`ENOSYS`を返します。
  動的リンク、TLS/thread、一般のsignalは未実装です。cwd・明示的FD継承・dup/pipeは後続実装で追加しました（[IPC](ipc-display.md)）。
- VFSは同じmountにopen fileがある間のnamespace変更を拒否します。POSIXのopen-unlink寿命は未実装です。
  unlinkは`EBUSY`を返し、ファイルをcloseしてから再試行できます。無期限の内部再試行は行いません。
- 旧shell/BASIC/editor等は`usr/legacy/`に保存され、現行EL0への移植は未着手です。
- Newlibの試験は付属probeで確認した範囲です。実機でのMMU/cache・デバイス・FP/SIMD動作試験は未実施です。

## 引継ぎ時の再検証

2026-10-27の再開時に、両カーネル・ユーザーELFの全再コンパイル、`make -B test`、`make test-qemu`が成功しました。
ホストのELF検証・load試験は21347 checks、QEMUでは不正ELF拒否、BSS/heap、stdio/errno、
spawn/envp/wait/kill/fault/orphan/FD回収、atexit、並列FP/SIMD、load時OOMのrollbackを確認しています。
今回のNewlibライブラリ本体は既存の`build/newlib/`を使用し、ユーザーruntimeとprobeを再コンパイルしました。

## Phase 3の仕上げ

- openしたファイルをunlinkするとresource pinを保持したまま永久再試行する問題を修正しました。
- タスク枠不足で`-1`を返してNewlibが`EPERM`と解釈する問題を、`EAGAIN`へ修正しました。
- VFSのfile pool枯渇をディスク容量不足の`ENOSPC`から`ENFILE`へ分離しました。
- `sbrk(-current_break)`がbrkの照会を縮小成功と解釈する問題を修正しました。
- reaperによるzombie公開・孤児回収をIRQマスク下の一つの更新にし、wait/spawnによる枠再利用との競合を防ぎました。
- 公開errno定義を揃え、Newlibとの値の一致をビルド時に検査します。
- ユーザーELFのC/assembly両方にヘッダー依存関係を生成し、libc/libm更新時も再リンクします。
- QEMU試験は最初の起動が失敗した場合もserial transcriptを保存します。

追加のlimits試験では、タスク枠の枯渇・二度の再利用、EMFILE/ENFILE、16 argv・16 envp・auxv、
文字列合計4096 bytesの成功と超過拒否、不正vector、入力待ち中のkill、heap縮小・zero-fill・OOM後の再試行を検査します。
通常・予約DTB・低メモリDTBの各起動で成功し、終了後の空きページ数も開始時と一致しました。
この試験は多数のELFをロードするため、専用の有限timeoutを45秒としています。

Newlib 4.6.0.20260123を`build/phase3/newlib-build/`でソースからビルドし、`build/phase3/newlib/`へインストールしました。
このライブラリをリンクしたユーザーELFで`make test-qemu`が成功しています。既定の`build/newlib/`も保持しています。
静的ELF・単一threadのプロセス・限定syscall・Newlib・FP/SIMDという本フェーズの対象範囲を検証しました。

UNIX形式のルート、EL0シェル、追加の19〜26番syscallとELFコマンドは [システム構成](unix-system.md) を参照してください。
