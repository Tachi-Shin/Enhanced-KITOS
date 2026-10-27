# Enhanced KITOS Phase 3

2026-10-27。ELF64静的実行ファイル、独立プロセス、Newlibを接続する。

## 実装する範囲

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

## 実装順序

1. ELF検証とload、初期stack、hostの不正ELF試験。
2. プロセス寿命とFD/heap、syscallを接続。
3. NewlibとユーザーELFの再現可能ビルド、モニタrun/wait。
4. 専用SDイメージのELF起動・並列実行・fault/kill・FD/VM回収と既存回帰。
5. 両ターゲットの警告なしビルド、仕様・制約・結果を記録。

物理ロード位置、TTBR1/TTBR0の方針、既存FAT32形式を維持する。
汎用ELF検証・プロセス・syscallと、AArch64 entry/FP状態およびplatformを分離する。
