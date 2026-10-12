# P3 Linux テスト結果

自動テスト: `./scripts/test-qemu.sh <RAM_MB> [iso|hdd]`（`P3_APPEND=nomodeset` で VESA ドライバ経路）

QEMU 設定: `qemu-system-i386 -cpu pentium3 -vga std`、IDE HDD + IDE CD-ROM、e1000、KVM なし（TCG エミュレーション）

| 構成 | 結果 | 備考 |
|---|---|---|
| 64MB / ISO / KMS (bochs-drm) 1回目 | 18/18 PASS | 起動〜デスクトップ 27秒、ストレス後 available 15MB、zram 使用 2MB、OOM 0 |
| 64MB / ISO / KMS 2回目 | 18/18 PASS | 同上 |
| 64MB / ISO / KMS 3回目 | 18/18 PASS | 同上 |
| 64MB / nomodeset (VESA, 16bit) | 18/18 PASS | 800x600、ストレス後 available 21MB、OOM 0 |
| 256MB / ISO / KMS | 18/18 PASS | 1024x768、ストレス後 available 202MB |

テスト項目: SYSLINUX による BIOS 起動、カーネル起動、ルートFSマウント、init、X.Org、デスクトップ、
キーボード（Ctrl+Alt+T で端末起動・文字入力）、マウス、端末、ファイルマネージャ、エディタ、
ストレス（9ウィンドウ同時表示）、メモリ/スワップ量、OOM（メモリ不足による強制終了）の有無、
ネットワーク（DHCP + ping）、再起動、シャットダウン。

## 低メモリ対策（64MB 対応）
- カーネル縮小: 常駐サイズ 22.8MB → 14.2MB（使えるメモリ 42MB → 50MB）
- RAM 100MB 未満で zram（LZO 圧縮スワップ、RAM の半分）を自動有効化
- フォントキャッシュをビルド時に生成（起動ごとのフォント走査を省く）
- 画面モード上限: 通常 1024x768、RAM 100MB 未満は 800x600（起動オプション `p3.res=` で変更可）

## 検証範囲
- **QEMU で確認済み。Pentium III 実機では未確認。**
- QEMU の `pentium3` CPU モデルは命令セットのみ再現し、速度は再現しません（ホスト上で高速に動作）。
  実機（450MHz〜1.4GHz）ではデスクトップ表示までの時間はより長くなる見込みです。
