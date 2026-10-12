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

## ハードウェア対応テスト（ドライバ自動読み込み）

QEMU で当時の機器を模擬し、udev によるドライバ自動読み込み・音の再生・LAN・X の起動を確認。

| 構成 | LAN | サウンド | 画面 | 結果 |
|---|---|---|---|---|
| A: 64MB | RTL8139 (8139too) | Ensoniq ES1370 (snd_ens1370) | std VGA (KMS) | 23/23 PASS、再生OK |
| B: 128MB | AMD PCnet (pcnet32) | Intel AC'97 (snd_intel8x0) | ATI Rage128 Pro | 23/24。r128 は QEMU のモニタ検出非対応で失敗 → 自動フォールバックで X 起動 |
| C: 128MB | NE2000 PCI (ne2k_pci) | Sound Blaster 16 ISA (snd_sb16、0x220 自動プローブ) | Cirrus (KMS) | 23/23 PASS、再生OK |
| D: 256MB | Intel 82559 (e100) | — | std VGA | 20/20 PASS（USB キーボード・マウス接続） |

### X の起動方式（実機向けの対策）
- グラフィックカードの PCI ベンダーから X ドライバを選び、失敗したら次へ自動で切り替える：
  KMS(modesetting) → カード専用ドライバ(r128/mach64/mga/savage/tdfx/neomagic/siliconmotion/cirrus) → 自動検出 → vesa → fbdev
- 標準起動ではカーネルの VESA フレームバッファを使わない（X の vesa ドライバがフレームバッファ存在時に動作を拒否するため）
- モニタの EDID が取れない場合は当時の CRT に安全な同期範囲（31.5–57kHz / 50–75Hz）を使用

### 静的チェック（scripts/check-image.sh）
- 全 ELF（カーネルモジュール 378 個を含む）が 32bit i386
- Pentium III で実行されるコードに SSE2 以降の命令なし（例外: libjpeg-turbo は実行時 CPU 判定、libgcc の _Float16 補助関数は未使用）
