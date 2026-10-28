# IPC・ダブルバッファ実装契約

2026-10-28。GUIを起動する前段として、以下を実装・検証する。

- IPCのFDはgeneration付きオブジェクト参照。パイプ、双方向packet channel、名前付きlisten/connect/accept、event counter/semaphore、共有メモリー。固定poolの上限・EAGAIN・EOF/EPIPE・close・異常終了・参照回収を定義する。
- pipeの4096 byte以下の書込みはatomic。channelは最大4096 byteのmessage境界を保持。小さすぎる受信bufferはmessageを消費しない。
- dup、descriptor flags、明示的FDマッピング付きspawn、pollのtimeout/複数FD待機。待機中にresource pinを解除し、kill可能とする。epoch照合とtaskのevent waitでlost wakeupを防ぐ。
- 共有メモリーは64 MiBまで。名前またはspawnのFDマッピングで共有し、read-only参照を尊重。通常heapと別のVAへmapし、borrowed PTEを用いてVMとobjectの二重解放を防ぐ。close後もmapは有効、unmap/終了時にmap参照を回収する。
- 旧spawnは互換性のため従来のconsole FD初期化を維持。spawn_exは親のstdioと明示したFDだけを継承する。共有file descriptionはVFS retainを追加する。
- shのquote-awareなpipeline分割、全stage同時spawn、不要FDのclose、全child wait、最終stageの終了status。catはstdinを受ける。
- VIDEO.EKMのfirmware virtual heightを2画面分とし、backへ描画後にvirtual offsetを切替・readback確認。対応しないfirmwareはtextのみ維持し、double-buffer capabilityを偽らない。VSYNC同期は未実装で、unverifiedとして報告する。
- GUI用のEL0 APIはexclusive acquire/present/release。入力は今回の対象外。shared memory上のXRGB8888 staging frameをworkerがGPU back bufferへ転送し、完成後にflip。UARTは利用可能なまま。owner終了時はtextへ復帰。
- driver ABIを更新し全.EKMを再生成。system.imgもバックアップして更新し、古いELFが残る配布不整合を防ぐ。

これはGUIに必要なIPC一式の契約であり、LinuxのすべてのIPC APIとの互換性を意味しない。実装が完了していない項目を検証済みとは記載しない。
