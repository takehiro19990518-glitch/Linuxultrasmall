# P3 Linux

Intel Pentium III 世代（1999〜2003年頃）のPCで動く、32bit の GUI 付き Linux ディストリビューションです。
Buildroot をベースに、カーネルからデスクトップ・アプリまで一式をソースからビルドしています。

> **検証範囲：QEMU（CPU モデル pentium3）で確認済み。Pentium III 実機では未確認です。**

## 必要なハードウェア

| 項目 | 要件 |
|---|---|
| CPU | Intel Pentium III（i686 + MMX + SSE）。SSE2 は不要 |
| メモリ | 最低 64MB（デスクトップと主なアプリ）。AbiWord / Gnumeric・一部のゲーム・複数アプリ同時使用は 128MB 以上推奨 |
| 起動 | BIOS から CD-ROM（ISO）、または IDE HDD（HDD イメージ） |
| 画面 | VGA 以上。Matrox / ATI Rage128 / S3 Savage / 3dfx / NeoMagic / Silicon Motion / Cirrus は専用ドライバ、それ以外（nVidia・SiS・Trident・VIA・Intel 810 等）は VESA で表示 |
| 入力 | PS/2 キーボード・マウス、USB キーボード・マウス |

## 入っているもの

- **デスクトップ**：X.Org + Fluxbox、Start メニュー（カテゴリ別）、タスクバー、時計、システム情報
- **インターネット**：Web ブラウザ（Dillo、Links）、IRC（irssi）、FTP（lftp）、SSH クライアント、wget / curl
- **オフィス**：ワープロ **AbiWord**（Word .doc/.docx・RTF・OpenDocument .odt の編集）、表計算 **Gnumeric**（Excel .xls/.xlsx・.ods・CSV の編集、グラフ）※どちらも 128MB 以上。
  64MB でも使える軽量版：表計算（sc-im）、テキストエディタ、Word/Excel/PowerPoint 文書の閲覧（antiword / catdoc）、PDF ビューア（MuPDF）、電卓
- **マルチメディア**：画像ビューア（feh）、音楽再生（mpg123：MP3、sox：WAV/OGG/FLAC）、音量調整（alsamixer）
- **ゲーム**：LBreakout2、LTris、XBill、XJump、チェス（GNU Chess）、Doom（Chocolate Doom + Freedoom）
- **ツール**：ファイルマネージャ、端末、Midnight Commander、vim、nano、htop、ncdu、7-Zip、unzip、parted、CD 書き込み（cdrkit）
- **日本語**：VL ゴシック フォント（表示のみ。日本語入力は未対応）
- **ドライバ**：サウンド（PCI / ISA / USB、Sound Blaster・ES1371・AC'97 等）、有線 LAN（3Com・Intel・Realtek・NE2000 等）、無線 LAN（Atheros・Ralink・Realtek 等、ファームウェア同梱）、PCMCIA/CardBus、USB、SCSI、プリンタポート、ACPI/APM。機器が見つかったときだけ自動で読み込みます

## 使い方

1. `P3-Linux.iso` を CD-R に焼く（または USB メモリに書き込む）。
2. PC の BIOS で CD-ROM から起動する。
3. 起動メニューで Enter（または 5 秒待つ）→ デスクトップが表示されます。

起動メニュー（`boot:` プロンプトで入力）

| 名前 | 内容 |
|---|---|
| `live` | 通常起動（既定） |
| `vga` | KMS を使わない（古いグラフィックカードで画面が乱れる場合） |
| `depth8` | 256 色で起動 |
| `text` | GUI なし（コンソールのみ） |

追加の起動オプション：`p3.res=800x600`（解像度）、`p3.xdriver=vesa`（X ドライバ指定）、`p3.depth=16`、`p3.zram=0|1`、`p3.isasound=0`

- root のパスワード：`p3linux`
- 無線 LAN：Start → Settings → Wi-Fi
- HDD へのインストール：Start → Install to Hard Disk（**未テスト**）

## ISO の入手

GitHub の 100MB 制限のため、`release/` に分割して置いています。結合してから使ってください。

```
# Linux / macOS
cat release/P3-Linux.iso.part-* > P3-Linux.iso
sha256sum -c release/P3-Linux.iso.sha256

# Windows（コマンドプロンプト）
copy /b release\P3-Linux.iso.part-aa + release\P3-Linux.iso.part-ab + release\P3-Linux.iso.part-ac P3-Linux.iso
```

## ドキュメント

- [BUILD.md](BUILD.md)：ビルド方法（再現手順）と、ソースの取得方法についての注意
- [TEST.md](TEST.md)：QEMU でのテスト結果
- [LICENSES/](LICENSES/)：含まれるソフトウェアのライセンス一覧

## 既知の制限

- Pentium III 実機では未確認
- HDD イメージからの起動・HDD インストーラは未テスト
- 日本語入力は未対応（表示のみ）
- Web ブラウザは軽量なもののみで、JavaScript を多用する最近のサイトは表示が崩れます
- AbiWord / Gnumeric は 128MB 以上が必要（64MB の PC ではファイルを開くと軽量ビューアが使われます）。印刷・スペルチェックは未対応
- OpenGL（3D）は未搭載（GTK3 は OpenGL なしで構成）
- SVG 表示には C 版の旧 librsvg 2.40 / libcroco を使用（Rust 版は SSE2 必須のため）。保守終了版で既知の脆弱性があるため、信頼できない SVG ファイルは開かないでください
- 一部のグラフィックカード（nVidia・SiS・Trident・VIA・Intel 810）はアクセラレーションなしの VESA 表示
- 一部の無線 LAN（Intel IPW2100/2200、ZyDAS、Atmel）はファームウェアが同梱できず、そのままでは使えません
