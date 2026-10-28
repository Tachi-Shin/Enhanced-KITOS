# Pi 4のUART入力が反応しない場合

2026-10-28。実機ログではPL011出力・SDマウント・VIDEO.EKM・VL805の起動後に回復用シェルの `root@ekitos:/#` へ到達しています。これは入力待ちです。USBキーボード未接続で発生したUARTの入力停止と、別のUSBデバイスの `enumeration=-95` は区別して扱います。HDMI EDIDの警告だけでUART入力が止まったとは判定できません。

## 今回の変更

UARTの受信ソフトウェアキューが空の場合、IRQを一時的に禁止して同じドライバーでハードウェアFIFOを読みます。IRQ側と同時に同じbyteを読むことはなく、ドライバーの上限付き処理を使います。通常のIRQ受信を維持しつつ、受信割り込みが届かない場合やFIFOしきい値に達していない場合も入力できます。PL011とmini UARTの両方が対象です。ABI 5の構造体レイアウトは変更していません。

起動ログに次が追加されます。

```text
[console] PL011 RX irq=153; FIFO polling fallback enabled
```

`/bin/sh` の起動失敗時は、ファイル欠損・不正なAArch64 ELF・未マウント・メモリー不足・タスク枠不足の理由とエラー番号を表示します。

```text
[init] /bin/sh: file not found (error -2)
[init] Install /bin and /sbin on the mounted root FAT32 volume (DATA if partition 2 is FAT32).
[shell] recovery mode; /bin/sh unavailable
```

`recovery mode` は通常のEL0シェルが起動できなかったことを示します。UARTが回復しても通常シェルのファイル配置が別途必要な場合があります。カーネルだけをBOOTへ更新しても、`/bin/sh` は生成・配置されません。回復用シェルには `uart`、`display`、`ls /bin`、`run /bin/sh` が使えます。

## Macのscreenで確認

現在のscreenを `Ctrl-A` → `\` で終了し、終了確認に答えてから開き直します。`Ctrl-A` → `D` は接続を残すため、別セッションから同じポートを開かないでください。

```sh
stty -f /dev/cu.usbserial-BG03182R 115200 cs8 -parenb -cstopb -crtscts -ixon -ixoff clocal
screen -fn /dev/cu.usbserial-BG03182R 115200,cs8,-ixon,-ixoff
```

これらはMac側の設定です。ポート名が変わった場合は実際の名前に読み替えます。実機側は115200 baud・8N1、RTS/CTSを使用しません。

- USB-UARTのTX → GPIO15（物理10番、PiのRX）
- USB-UARTのRX → GPIO14（物理8番、PiのTX）
- GND共通、信号電圧3.3 V

GPIO14側の送信ログが読めても、GPIO15側の配線・PC側送信が成立している証拠にはなりません。`usb-camera.html` はHDMIキャプチャ映像を表示するビューアで、キー入力のUART送信機能はありません。

入力が戻ったら `echo uart-ok`、`uart`、`ls /bin` を実行します。`uart` の `rx=` は受信byte数です。IRQが届かずFIFO補助経路だけで動いている場合は、`irq=` が増えなくても `rx=` が増えます。更新後も1文字もechoされない場合は、配線・PCのフロー制御を含め実機のRX経路の確認が残ります。

## 配置

```sh
make TARGET=hardware -j4
```

`image/rpi4/hardware/kernel8.img` をBOOTの `kernel8.img` へコピーし、`build/rootfs/bin`・`sbin`・`lib` を **EKITOSが `/` としてマウントするFAT32** へディレクトリを結合してコピーしてください。第2パーティションがFAT32の場合はDATA側を優先する仕様です。既存のconfig.txtやユーザーファイルを消す必要はありません。通常シェルなら `[shell] /bin/sh ready` が表示されます。

更新一式は `build/deploy/ekitos-recovery-cwd-fix.zip` にまとめています。UART修正と下記の回復用シェルの修正を含みます。`boot/kernel8.img` と `rootfs/` の用途を同梱READMEで分けています。

## 回復用シェルで `cd ..` の表示が `/bin/..` になる問題

実機で `/bin` が空の状態では、通常のEL0シェルではなくカーネルの回復用シェルが動いていました。従来のQEMU試験は通常シェルのcdを確認していましたが、回復用シェルのcdを確認していませんでした。

回復用シェルはVFSで移動先を検証した後、入力を連結した `/bin/..` をそのままcwdに保存していました。通常シェルと共通の `vfs_resolve_directory()` を使うよう修正し、`.`・`..`・重複スラッシュを解決したパスだけを保存します。移動失敗時はcwdを維持します。`missing/..` や `file/..` を単純な文字列操作で消して成功扱いにすることもありません。

```text
root@ekitos:/# cd bin
root@ekitos:/bin# cd ..
root@ekitos:/# pwd
/
root@ekitos:/#
```

```sh
make test-recovery-qemu QEMU=/path/to/qemu-system-aarch64
```

この試験はFAT32に通常シェルを配置しない状態を再現します。BOOTにだけELFファイルを置き、DATAをルートに選ぶ2パーティション構成も検査します。DATA側へELFファイルを配置した後は通常の `/bin/sh` が起動し、両シェルで同じcdの結果になり、先に作成したデータも保持されることを確認します。詳細は [検証記録](recovery-cwd-validation.txt) を参照してください。

## 検証

```sh
make test-uart
make test-uart-qemu QEMU=/path/to/qemu-system-aarch64
```

ホスト試験はASan/UBSan付きでFIFOだけでの受信、IRQとの混在、重複抑止、受信エラー後の復帰を確認します。

QEMU試験は **hardware版のカーネルと実際のUART.EKM（PL011）・GIC.EKM** を使用し、受信割り込み有効時・PL011 IMSCを0へ変更した時・再有効化した時の入力を検査します。IMSCが0でも1文字のechoとコマンド実行が成功し、IRQ数を増やさず受信byte数が増えることを確認します。

raspi4bはPCIeを持たないため、この試験専用の一時kernel fixtureでPI4USB.EKMの同梱リソースだけをHELLO.EKMに置き換えます。実機配布用kernel8.imgは置き換えません。SDの実機用eMMC2はこの試験ではマウントせず、回復用シェルで確認します。

`build/tests/uart-fix-host.txt`、`uart-fix-pl011-qemu.txt`、`uart-pl011-qemu-transcript.txt` に結果があります。通常シェル・不正ELF/欠損時の診断・HDMI端末は `uart-fix-unix-qemu.txt` と `uart-fix-terminal-qemu.txt` で確認します。**実機での入力停止の再現・修正確認はまだできていません。**
