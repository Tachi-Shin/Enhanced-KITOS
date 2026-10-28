# IPC・シェルのパイプ・ダブルバッファ

2026-10-28。GUI起動前の基盤として実装。カーネル、全12個のABI 6 `.EKM`、`/bin`・`/sbin` のELFを一緒に更新する。

## シェル

```text
echo hello | cat
ls / | cat | cat
export | cat
sh -c 'echo hello | cat'
```

最大8段を同時に起動し、標準出力を次の標準入力へ接続する。不要なFDを親・子で閉じ、全段の終了を待つ。`$?` は最後の段の終了状態。引用符やバックスラッシュで囲んだ `|` は文字として扱い、空の段や未対応演算子は起動前に拒否する。組込みコマンドは子シェルで実行するため、`cd /tmp | cat` は親のcwdを変えない。`cat` は引数なし、または `-` で標準入力を読む。通常の `cat FILE` も維持する。

FDの明示的マッピング付きspawnを使用する。fork/execve、`>` 等のリダイレクト、`&&`・`||`、job control、一般のsignal配信は今回の対象外。

## CからのIPC

公開宣言は `usr/runtime/kitos.h` と `include/uapi/ipc.h`。PC上で既存のクロスビルドとNewlibを使い、静的AArch64 ELFにする。

| API | 契約 |
| --- | --- |
| `pipe` / `kitos_pipe` | 4096 byteのstream。4096 byte以下の各書込みはatomic。空・満杯では待機し、NONBLOCKならEAGAIN。最後のwriter close後は残りを読んでEOF。reader消失後はEPIPE。SIGPIPEは配信しない |
| `kitos_channel` | 双方向message。各方向4通、1通最大4096 byte。小さい受信bufferはEMSGSIZE、messageは残る。0 byte送信は何もしない |
| `kitos_listen/connect/accept` | 名前からchannelへ接続。backlog最大8。listen前のconnectはENOENT、満杯時は待機またはEAGAIN。listenerのcloseで未acceptの接続を切断 |
| `kitos_event_create` | 64-bit counter。8 byte readで全数取得、semaphore指定時は1ずつ取得。writeで加算。最大値はUINT64_MAX−1。空や加算overflow時は待機またはEAGAIN |
| `kitos_shm_open/map/unmap/unlink` | 匿名または名前付き、1 byte〜64 MiB。初期値0。read-only FDからRW mapはEACCES。mappingはFD close後も有効。名前は最後のFD・mapping参照消失時にも消える。unlinkは名前のみ外す |
| `dup` / `dup2` / `fcntl` | IPC端点・VFS file descriptionを共有。file offsetとNONBLOCKは共有、CLOEXECはFDごと。F_GETFL/F_SETFL、F_GETFD/F_SETFDの限定実装 |
| `kitos_spawn_with_fds` | 親のstdioと明示したFDのみ継承。CLOEXECのstdioは省略。明示的マッピングはdupとしてCLOEXECを解除。source=−1は閉じたFD。従来の `kitos_spawn` はconsole初期化を維持 |
| `kitos_poll` | 複数FDのIN/OUT/ERR/HUP/NVAL。timeout=−1は無期限、0は即時。負のFDは無視。不正FDはNVAL |

オブジェクトはgeneration付きcapabilityで管理し、古いFD内部handleの再利用を拒否する。待機はepochの確認とスケジューラーのBLOCKED状態を使い、通知取りこぼしを防ぐ。待機中のkill、正常終了、例外終了でFDとmappingを回収する。共有ページの所有権はIPC objectに置き、VM側のborrowed PTEは二重解放しない。

現在の上限は、内部サービスを含め通常タスク32、プロセスFD16、IPC object全体32、mappingはプロセスごと16。名前は1〜63 byteの単一root名前空間。名前付きサービスの認証・複数ユーザーの権限分離、POSIX socketやSystem V IPCとの互換性は含まない。これはGUI用に実装したIPC一式であり、Linuxの全API実装を意味しない。

## 描画

VIDEO.EKMはfirmwareに2画面分のvirtual heightを要求し、実際の寸法・pitch・割当サイズを確認する。back bufferへ描画し、完成後にvirtual offsetを設定して読み戻す。端末もこの方式を使い、flip後に新しいbackへ表示内容を複製して差分描画を続ける。`display` の `double-buffer=1` が使用可能を示す。

```c
struct kitos_terminal_info info;
kitos_tty_info(&info);
int fd = kitos_shm_open(NULL, (size_t)info.width * info.height * 4,
                       KITOS_IPC_CREATE);
uint32_t *pixels = kitos_shm_map(fd, 0);
/* 実際のプログラムでは各戻り値を検査する。 */
kitos_display_acquire();
/* pixelsへXRGB8888を描画する。little endianのbyte順はB,G,R,X。 */
kitos_display_present(fd, info.width, info.height, info.width * 4);
kitos_display_release();
kitos_shm_unmap(pixels);
close(fd);
```

acquireは1プロセス専有。present中はproducerがstaging frameを書き換えない契約とする。workerが共有メモリーからGPUのbackへ全画面をコピー・色変換し、その後flipする。EL0にGPU物理メモリーは公開しない。presentが戻った後はstagingを再利用できる。UART出力は継続する。release、kill、例外終了で文字端末へ戻る。GUI中のHDMI端末出力は再生しない。

virtual offset非対応や確認失敗時はdouble-buffer capabilityを外し、文字端末を維持する。acquire/presentはENOTSUP。framebuffer mappingは64 MiBまでなので、2画面分が収まらないモードでは文字端末のみ。VSync同期は未実装・未確認であり、フレーム境界での完全なtear-free表示やフレームレート固定は保証しない。QEMUはEDID・timingを提供しないためrefreshはunknown。実機でのfirmware/HDMI検証は別途必要。

virtual geometry/offsetは[Raspberry Pi firmware property](https://github.com/raspberrypi/firmware/wiki/Mailbox-property-interface)に従う。ACKだけで要求値が反映されたと判断せず、readbackを検査する。

## ビルド・試験・配置

```sh
make TARGET=qemu -j4
make test
make test-ipc-qemu QEMU=/path/to/qemu-system-aarch64
make test-display-qemu QEMU=/path/to/qemu-system-aarch64
make TARGET=hardware -j4
make update-system-image
```

`ipc-probe` は実際のEL0子プロセスを使って、backpressure、複数writerのatomic書込み、EOF/EPIPE、dup/poll、message、named接続、event、共有メモリー、read-only fault、待機中kill、file offset、回収を試験する。`display-probe` は赤表示→青をstagingだけに描画→presentで青表示を行う。試験ツールはQMPの実画面ピクセルを検査する。

`tools/boot_snapshot_test.py` は試験用イメージの入口のみを変更し、DTBをNOLOAD BSS内へ置いて、BSS初期化前のsnapshot・16 MiB制限下のVM/OOMを検査する。通常のカーネルイメージは変更しない。通常のDTB fixtureのinitrd/reserveは8/9 MiBへ移し、拡大したカーネルとの重複を避けた。

`make update-system-image` は既存image全体をbackupしてからプログラムとmoduleを更新する。以前と同じく、実機ではkernelだけでなく、EKITOSの `/` にするFAT32上の `/bin`、`/sbin`、`/lib/modules` を更新する。既存設定・ユーザーファイルは削除しない。
