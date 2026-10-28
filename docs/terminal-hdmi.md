# 履歴・Tab補完・HDMI端末

2026-10-28。通常の `/bin/sh` に履歴と補完を追加し、`VIDEO.EKM` でUARTと同じ出力をHDMI framebufferへ描画します。文字セルは指定どおり **6×8ピクセル**、ASCII 32〜126です。QEMUの実画面取得と画素照合を実施しています。**Pi 4のHDMI実機試験はまだ行っていません。**

## 操作

| キー | 動作 |
| --- | --- |
| ↑ / ↓、Ctrl-P / Ctrl-N | 履歴を前後へ移動。↓で入力途中の行に戻る |
| Tab | コマンド名・パスの補完。複数一致は共通部分を補完し候補を表示 |
| ← / →、Ctrl-B / Ctrl-F | カーソル移動 |
| Home / End、Ctrl-A / Ctrl-E | 行頭・行末へ移動 |
| Backspace / Delete | 前・後の文字を削除 |
| Ctrl-U / Ctrl-K / Ctrl-W | カーソル前・後・直前の単語を削除 |
| Ctrl-C / Ctrl-D | 行を取り消す / 空行ならシェルを終了 |

USB Bootキーボードも矢印・Home/End・DeleteをANSIキー列として入力します。配列は従来どおりUS配列です。文字挿入と長い行の横スクロールに対応します。

```text
history
history -c
export HISTFILE=/root/.sh_history
export HISTFILE=''
display
```

履歴は最大64件、連続した同じ行は1件です。既定は `$HOME/.sh_history`（HOME未設定時は `/root/.sh_history`）へコマンド確定時に保存します。履歴ファイルを閉じてからコマンドを実行します。読み取り専用媒体などで保存できなければメモリー内の履歴を使います。`HISTFILE=''` で永続保存を無効化できます。

TabはPATH内のコマンド、シェル組み込み、相対・絶対パス、ディレクトリ末尾の `/`、空白を含むファイル名に対応します。`cd` の引数はディレクトリだけを候補にします。隠しファイルは `.` から入力した場合に表示します。候補表示は最大64件、入力行は最大1023 byteです。`$VAR` を含む語の補完や入力中のghost textは実装していません。

`/bin/sh --version` が `history=64 Tab=command,path,candidates` を表示することを確認できます。`ec`＋Tabで `echo `、`e`＋Tabで `echo`・`env`・`exit`・`export` の候補一覧が出ます。確定前の候補は実行しません。

カーネルの更新だけではシェルの機能は更新されません。実機のルートFAT32上の `/bin/sh` を `build/usr/bin/sh` に更新してください。シェル単体の配置用ファイルは `build/deploy/ekitos-tab-shell-update.zip` の `bin/sh` です。

シェルの幅は `COLUMNS`（既定80）とHDMIの列数の小さい方を使います。カーソル位置はUTF-8を壊さず移動しますが、HDMIの非ASCII文字は代替表示です。UARTの文字列はUTF-8のまま出力します。回復用のカーネルシェルは最小機能のままで、通常の履歴・補完は `/bin/sh` の機能です。

## 表示モード

起動時にファームウェアからdisplay IDとEDIDを読み、有効なEDIDがある最初のHDMI出力を選びます。HDMI1だけに接続した場合も探索します。EDIDは最大4 block、ヘッダー・チェックサム・境界を検査します。

EDIDの推奨Detailed Timingを優先し、対応するCTAのnative modeや他のDetailed Timingを候補にします。解像度、ピクセルクロック、同期極性と前後のporchをファームウェアへ設定し、設定後のtimingを読み戻します。周波数は読み戻したclockとtotal値から算出するため、59.94 Hz等も整数60 Hzに丸めて報告しません。ファームウェアの最大pixel clockを超えるモード、インターレース、double-clock、4096×2160を超える画面は選びません。実際に返された寸法・pitch・depth・pixel orderで描画します。

HDMIのCTAビデオモードはRGB limited、DVIやPCのカスタムタイミングはRGB fullの規則で設定します。EDIDの色深度情報を取得し、`display` の `advertised-bpc` へ表示します。現在の出力対象はRGB/SDRの8-bit/channelで、framebufferは16/24/32-bit RGB/BGRを扱います。32-bppのframebufferは8-bit RGBと未使用8-bitであり、HDR・10/12-bit deep colorやYUV出力の実装ではありません。

EDIDやtiming APIが使えない場合はファームウェアの現在の画面サイズを引き継ぎます。取得できない周波数とlink情報は `unknown` と表示します。QEMU 10.2.2のraspi4bはEDIDとdisplay timing APIを実装していないため、このフォールバック経路を使います。

```text
display: 640x480 32-bpp RGB pitch=2560 terminal=106x60 font=6x8 ASCII
display: refresh=unknown (firmware did not expose timing)
display: id=2 EDID=0 advertised-bpc=0 link=unknown range=unknown virtual=1 dropped=0
```

モード選択は**起動時**です。起動後のHDMI差し替え・自動再設定、複数画面への同時出力、VSync同期は未実装です。double bufferingはABI 6で追加し、virtual offsetのreadbackで確認します。リフレッシュレートはHDMI信号のtimingであり、静止した端末画面を毎フレーム描き直す機構は必要としません。

## ビルドと起動

```sh
make TARGET=hardware -j4
make drivers-native
make run-video QEMU=/path/to/qemu-system-aarch64
# LinuxのGUI環境では QEMU_DISPLAY=gtk または sdl を指定
```

Pi 4には `image/rpi4/hardware/kernel8.img` と `build/rootfs/` の内容を従来のBOOT・ルート配置規則で配置します。**ABI 6のカーネルと全12モジュールを一緒に更新**してください。通常のQEMU起動で10個がロードされ、`VIDEO.EKM` も `/lib/modules/` を優先し、欠損時は同梱版を使います。

Pi 4はファームウェアのdisplay/mailbox機能を使います。HDMIを接続した状態で起動し、既存のbare-metal向けconfig.txtへ次の基本設定を適用してください。固定の `hdmi_group` / `hdmi_mode` / `hdmi_timings` は通常不要です。Linuxの `vc4-kms-v3d` overlayを読み込む構成はこのドライバーの対象外です。

```ini
arm_64bit=1
kernel=kernel8.img
enable_uart=1
disable_overscan=1
framebuffer_depth=32
framebuffer_ignore_alpha=1
```

`make run` は従来どおりUART用のheadless起動です。`make run-video` はQEMUのウィンドウとUSBキーボード・マウスを有効にします。既存のSDイメージは自動的に更新しません。古い `/bin/sh` を検出した場合は起動準備時に不一致を表示します。次の明示的な更新ではイメージ全体を `.before-update-*.bak` へバックアップし、管理対象の実行ファイル・モジュールだけを更新します。`/etc`・履歴・ユーザーファイル・独自コマンドは保持します。

```sh
make update-system-image SD_IMAGE=build/tests/system.img
```

2026-10-28の実機でTabが使えなかった件では、`system.img` 内の `/bin/sh` が594712 byteの旧版で、履歴・Tabのコードを含んでいませんでした。既存イメージを更新し、そのコピーを再配置せずに起動する試験を追加しました。

## 構成と検証

- `usr/runtime/lineedit.c`、`usr/programs/sh_support.c`: 入力編集・履歴・候補探索。
- `drivers/modules/video.c`、`drivers/video/`: ファームウェア通信・EDID・6×8 font・端末描画。実体は独立したC製ELFモジュール。
- `kernel/console/`: UART出力と64 KiBの描画キュー。保護されたワーカーが描画し、scroll中も割込みを有効に保ちます。診断ジョブもこのワーカーで実行します。キュー満杯時は古い出力を捨てて端末をリセットし、欠落byte数を報告します。
- `arch/arm64/kernel/paging.c`: GPU専用RAMをNormal Non-cacheable・EL1 RW/NXでマップ。通常RAMのcacheable aliasや範囲外のアドレスは拒否します。
- `kitos_tty_info()`: EL0へ画面・文字セル・周波数情報を取得するsyscall。`/sbin/display` は独立したELFです。

端末は16/256/true colorのSGR、基本CSIカーソル移動・消去、CR/LF・タブ・スクロール・下線カーソルを処理します。プロンプトに付けるOSC 133 shell integration markerはHDMIには表示しません。

```sh
make test-terminal        # ASan/UBSan: 編集・EDID・firmware応答・画素・変異入力
make test-terminal-qemu QEMU=/path/to/qemu-system-aarch64
```

QEMUの `screendump` から取得した画面は `build/tests/terminal-screen.png`、UART記録は `build/tests/terminal-qemu-transcript.txt` です。6×8 glyphの48画素をRGB値まで照合します。詳細は [検証記録](terminal-hdmi-validation.txt) を参照してください。

更新イメージの検証は `python3 tools/terminal_qemu_test.py --image build/tests/system.img --qemu /path/to/qemu-system-aarch64 --kernel build/rpi4/qemu/kernel8.img` で実行できます。元イメージのコピーを使用し、シェルを再配置して不一致を隠さずに確認します。Tab候補を表示したままの画面は `build/tests/tab-candidates-screen.png` です。

プロトコルの根拠は [公式mailbox仕様](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)、[公式Linux firmware API](https://github.com/raspberrypi/linux/blob/rpi-6.6.y/include/soc/bcm2835/raspberrypi-firmware.h)、[公式firmware KMSのtiming形式](https://github.com/raspberrypi/linux/blob/rpi-6.1.y/drivers/gpu/drm/vc4/vc4_firmware_kms.c)、[公式config.txt説明](https://www.raspberrypi.com/documentation/computers/legacy_config_txt.html) です。数値形式を参照し、fontと描画・EDID処理は本リポジトリ内で実装しています。

ダブルバッファ・GUI描画用API・検証手順は [IPC・ダブルバッファ](ipc-display.md) を参照してください。
