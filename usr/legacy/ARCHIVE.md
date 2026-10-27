# 旧KITOSの参照用ソース

`bin/`のBASIC・エディタ・GPIOアプリ・demo_a〜d、`lib/`のアプリ専用ランタイム、
`shell/`、`include/usr/`、旧リンカスクリプトとREADMEを変更前のまま保存しています。
旧アプリの機能を恒久削除するものではありません。現行Makefileはこのディレクトリを探索・リンクしません。
旧リンカスクリプトは記録であり、現在のビルドパス・スタック方式に対応する互換層ではありません。

除外理由: EL1固定リンク・アプリ関数ごとの固定スタック・専用BSS初期化への依存を、
スケジューラとカーネルの公開APIから取り除くためです。
代替: kernel/monitor、tests/kernel_regression.c、tests/fs_test.c、tools/qemu_test.py。
アプリを再導入する場合はPhase 2以降のEL0/ELF方式へ移植してください。
