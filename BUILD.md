# P3 Linux のビルド方法

## 1. ホストの準備（Ubuntu 24.04 で確認）

```
sudo apt-get install build-essential git wget curl cpio rsync bc file unzip \
  libncurses-dev python3 xorriso isolinux syslinux syslinux-common mtools \
  dosfstools squashfs-tools gettext autopoint gawk flex bison lzip \
  qemu-system-x86 qemu-user imagemagick
```

- `qemu-user`（qemu-i386）はフォントキャッシュの生成に使います（無くてもビルドは通ります）
- `gettext` `autopoint` `gawk` `flex` `bison` はホストのものを使います（下記 3. 参照）

## 2. ビルド

```
git clone <このリポジトリ>
cd Linuxultrasmall
./scripts/build.sh            # 数時間かかります（4 コアで約 2〜3 時間）
```

`build.sh` が行うこと：

1. Buildroot 2026.08（コミット `d5180309b1b6`）を GitLab から取得
2. `patches/buildroot/*.patch` を適用
3. `configs/p3linux_defconfig` を設定
4. カーネル（Linux 6.12.111）を GitLab の linux-stable ミラーから取得
5. ソースを取得（`scripts/fetch-sources.py`、`scripts/ubuntu-subst.py`。下記 3.）
6. `make`。成果物は `output/images/P3-Linux.iso` と `output/images/P3-Linux-hdd.img`

環境変数：`P3_DL_DIR`（ダウンロードキャッシュ）、`JOBS`（並列数）、`P3_UBUNTU_SOURCES=0`（Ubuntu ソース代替を使わない）

## 3. 重要：ソースの取得方法について

開発環境のネットワーク制限で kernel.org・gnu.org・x.org・sourceforge 等に接続できなかったため、
**多くのパッケージを Ubuntu アーカイブにある同じソフトウェアのソースで代用しています。**

- `scripts/ubuntu-sources.lock`：代用したパッケージ・Ubuntu 上のファイル・SHA256・バージョンの固定リスト
- `scripts/ubuntu-subst.py`：Ubuntu のファイルを SHA256 で確認してから Buildroot が期待するファイル名に詰め直し、
  Buildroot の `.mk` のバージョンと `.hash` を書き換えます。Buildroot 自身のハッシュ確認とパッチ適用はそのまま行われます
- 一部はバージョンが異なります（例：X.Org サーバ 21.1.24 → 21.1.22、busybox 1.38.0 → 1.37.0）。
  一覧はビルド後の `output/ubuntu-subst-report.txt`
- 当てられなかった Buildroot のパッチは削除し、レポートに記録しています
- Ubuntu で一部ファイルが除去されたソース（dfsg）には補修処理（`FIXUPS`）を入れています。
  例：MuPDF の標準フォントは upstream のタグから SHA256 固定で取得
- 古いバージョンのソースを GCC 15 でビルドするため、C は `-std=gnu17` などで警告扱いにしています（`external.mk`）

**通常のネットワーク環境では** `P3_UBUNTU_SOURCES=0 ./scripts/build.sh` で、
Buildroot 本来の配布元からのソースでビルドできます（この方法は未検証です）。

## 4. テスト

```
./scripts/test-qemu.sh 256 iso        # 自動テスト（起動〜GUI〜アプリ〜再起動・終了）
./scripts/test-qemu.sh 64 iso         # 64MB
P3_APPEND=nomodeset ./scripts/test-qemu.sh 64 iso   # VESA ドライバ経路
QEMU_NIC=rtl8139 QEMU_EXTRA="-audiodev none,id=s -device ES1370,audiodev=s" \
  EXPECT_MODULES=8139too,snd_ens1370 EXPECT_SOUND=1 ./scripts/test-qemu.sh 64 iso
./scripts/check-image.sh              # ISO 構造・32bit・SSE2 命令の静的チェック
./scripts/run-qemu.sh 256             # 手動で起動
```

## 5. ディレクトリ構成

| パス | 内容 |
|---|---|
| `configs/p3linux_defconfig` | Buildroot 設定 |
| `board/p3linux/` | カーネル設定断片、起動スクリプト、rootfs オーバーレイ、ISO/HDD イメージ作成 |
| `package/` | 独自パッケージ（デスクトップアプリ、ファームウェア、フォント、ゲーム等） |
| `patches/buildroot/` | Buildroot へのパッチ |
| `external.mk` | ビルドフラグの調整 |
| `scripts/` | ビルド・ソース取得・テストのスクリプト |
| `release/` | ISO（分割） |
| `test-results/` | テスト結果とスクリーンショット |
