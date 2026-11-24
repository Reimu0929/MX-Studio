# MSX CF-2000 Studio

National **CF-2000**（日本語 MSX1 / MSX BASIC 1.0 / 16 KB RAM）向けの、
BASIC 編集・トークン化・カセット（FSK）変復調・WAV 録音を行う開発環境です。

A Windows toolchain for the National CF-2000 (Japanese MSX1): BASIC editor,
MSX tokenizer, 1200/2400 baud FSK modem, and cassette WAV recording — with no
runtime dependencies and no installation.

[![CI](https://github.com/Reimu0929/MX-Studio/actions/workflows/ci.yml/badge.svg)](https://github.com/Reimu0929/MX-Studio/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

- 版: **0.1.0**（プレビュー）
- 対応: Windows 32 bit（XP SP3 は v141_xp ビルド）。Linux では CLI とコアのみビルド可。
- 状態: コア・自動試験・MSVC ビルドは確認済み。**XP 実機・物理テープ・openMSX は未試験。**
- ドキュメント: [Wiki](https://github.com/Reimu0929/MX-Studio/wiki)

## ダウンロード

[Releases](https://github.com/Reimu0929/MX-Studio/releases) から最新の ZIP を取得し、
好きなフォルダーに展開してください。**インストール不要**です。

| ファイル | 内容 |
| --- | --- |
| `MX-Studio-<版>-win32.zip` | `msxstudio.exe`（GUI）、`msxtool.exe`（CLI）、サンプル、ドキュメント。CI でビルドと動作確認済み（Windows 10/11） |
| `MX-Studio-<版>-win32-xp.zip` | MSVC **v141_xp** でビルドした XP 対応版（PE 5.01・静的 CRT）。CI で自動ビルド |

DLL の追加インストールは不要です（静的リンク）。
設定は `HKCU\Software\MSXCF2000Studio` に保存され、アンインストールはフォルダー削除だけです。

XP 版はタグ `v*` を push すると GitHub Actions が v141_xp ツールセットを導入して
自動ビルドし、リリースに添付します。通常の push では CI は動きません（開発中の
ビルドはローカルで行います）。手元で作る手順は
[ビルド](#windowsxp-対応msvc-v141_xp) を参照してください。

## できること

| 機能 | 状態 |
| --- | --- |
| UTF-8 BASIC ソースの字句解析・構文色分け・補完・静的診断 | 実装 |
| MSX BASIC トークン化（行リンク、0x0E 行参照、BCD 数値、REM/DATA/文字列） | 実装 |
| トークン列から BASIC テキストへの逆変換（LIST 相当） | 実装 |
| 日本語 MSX 文字コード（半角カナ・ひらがな・グラフィック文字）の相互変換 | 実装 |
| CF-2000 プロファイル（開始 0x8000、`FRE(0)`=12431） | 実装 |
| CSAVE 形式（D3）、BSAVE（D0）、SAVE/A（EA）の CAS 生成・解析 | 実装 |
| 1200 / 2400 baud FSK 変調（48 kHz / 16 bit / モノラル WAV、デジタル音量 1～100%） | 実装 |
| 48 kHz WAV の復調（リーダー検出、速度推定、フレーム再同期、極性反転・DC オフセット・雑音・速度揺れ） | 実装 |
| WAV のピーク正規化（復号用コピー。原音は無加工で保持） | 実装 |
| Win32 ネイティブ UI（メニュー＋ツールバー、RichEdit、レベルメーター、録音・送信・復号） | 実装 |
| 実機 ROM のダンプ補助（`scripts/dump-cf2000-rom.bat`） | 実装 |
| openMSX 0.12.0 連携（行ブレークポイント・変数表示） | **調査完了・未実装**（[docs/openmsx.md](docs/openmsx.md)） |
| 実機・XP SP3・物理テープでの受入試験 | **未実施** |

## すぐ使う

### GUI（`msxstudio.exe`）

1. `.bas` を開く（サンプルは `examples\HELLO.bas`）
2. `F5` 検証 → 行・桁付きの診断と空き容量
3. `F6` 送信（テープ設定 → PC の音声出力から FSK を再生）
4. `F7` 録音（48 kHz / 16 bit / モノラル WAV）
5. `F8` 復号（WAV → CAS / BASIC）
6. `Ctrl+Space` 補完、`Ctrl+Z` Undo、`Ctrl+S` 保存

テープ設定では次を指定できます。

- **送信音量**: 自動（100% = 約 -7 dBFS）または手動 1～100%
- **録音後に復号用の正規化コピーを作る**: 原音 WAV は無加工のまま、
  `<名前>.normalized.wav` を別ファイルで生成（目標 -6 dBFS、最大 +12 dB）

送信の再生完了は**実機の受信成功を意味しません**。CF-2000 側で `CLOAD` → `LIST` / `RUN`
を確認してください。

### CLI（`msxtool.exe`）

```bat
rem BASIC -> CAS（CSAVE D3 形式）
msxtool bas2cas HELLO.bas -o HELLO.cas --name HELLO

rem BASIC -> 送信用 WAV（2400 baud、音量 70%）
msxtool bas2wav HELLO.bas -o HELLO.wav --name HELLO --baud 2400 --volume 70

rem CAS -> WAV（音量 50% = 約 -13 dBFS）
msxtool cas2wav HELLO.cas -o HELLO.wav --baud 2400 --volume 50

rem WAV の正規化コピー（原音は変更しない）
msxtool normalize REC.wav -o REC.normalized.wav --target -6 --max-gain 12

rem WAV -> CAS（速度自動判定。--baud 1200/2400 で手動指定）
msxtool wav2cas REC.wav -o REC.cas

rem WAV -> BASIC テキスト
msxtool wav2bas REC.wav -o REC.bas --baud auto

rem 一覧 / BASIC 取出し / バイナリ取出し
msxtool casinfo HELLO.cas
msxtool cas2bas HELLO.cas -o HELLO.bas
msxtool cas2bin GAME.cas -o GAME.bin

rem 検証 / ディスク用トークン化形式
msxtool validate HELLO.bas
msxtool tokenize HELLO.bas -o HELLO.bin
msxtool detokenize HELLO.bin -o HELLO.bas --lf
```

- ファイル名は MSX の 6 バイト制限です。超過はエラーで、**勝手に切り詰めません**。
- 既存の出力ファイルは `--force` を付けない限り上書きしません。
- WAV は 48,000 Hz / 16 bit PCM / モノラル（外部入力はステレオ可、`--channel`）。
- `--volume` はデジタル音量 1～100%（100% = 約 -7 dBFS）。既定は 100% です。
- `wav2cas` / `wav2bas` は 1200 / 2400 baud を自動判定します。
- Windows のコンソールでは日本語を UTF-16 で出力します（リダイレクト時は UTF-8）。

## 実機 ROM のダンプ

CF-2000 の ROM は基板直付けですが、MSX BASIC の `BSAVE` は ROM 空間も読めるため、
半田を剥がさずカセット経由で吸い出せます。

```basic
BSAVE"ROM0",&H0000,&H2000
BSAVE"ROM1",&H2000,&H2000
BSAVE"ROM2",&H4000,&H2000
BSAVE"ROM3",&H6000,&H2000
```

各ブロックを WAV に録音したら:

```bat
scripts\dump-cf2000-rom.bat ROM0.wav ROM1.wav ROM2.wav ROM3.wav
```

正規化コピー → `wav2cas` → `cas2bin` → 連結まで自動で行い、`cf2000.rom`（32,768 バイト）と
SHA1 を出力します。原音 WAV は変更しません。詳細は
[Wiki: 実機 ROM のダンプ](https://github.com/Reimu0929/MX-Studio/wiki/ROM-dump) を参照してください。

> ROM は同梱していません。自分が所有する実機から私的利用の範囲でダンプするのは問題ありませんが、
> **公開・配布はできません**。openMSX で使う場合も各自で用意してください。

## ビルド

### Windows（XP 対応・MSVC v141_xp）

仕様の基準は MSVC **v141_xp**（`WINVER` / `_WIN32_WINNT = 0x0501`、PE 5.01、静的 CRT）です。
2026-11-24 に **VS2017 15.9.37902.15 + CMake 4.5.1 + v141_xp** でビルドに成功しています。

必要なもの:

- Visual Studio 2017 の **VC++ 2017 v141 ツールセット (x86/x64)**
- **C++ Windows XP Support for VS 2017 (v141) tools [Deprecated]**
- Windows 7.1A SDK（XP サポートコンポーネントに含まれる）
- CMake 3.16 以上

```bat
scripts\check-vs2017.bat
scripts\build-msvc-xp.bat
scripts\check-xp-binary.bat
scripts\smoke-test.bat
```

`VCTargetsPath` が環境変数に残っていると v141 を見失うことがあるため、ビルドスクリプトが
自動で解除します。WSL/UNC パス（`\\wsl$\...`）上では MSBuild が失敗することがあるので、
ローカルの `C:\...` に置いてビルドしてください。

VS2019/2022 に v141_xp を追加している場合は、ジェネレーターを指定できます。

```bat
set MSX_VS_GENERATOR=Visual Studio 17 2022
scripts\build-msvc-xp.bat
```

### Linux / macOS（CLI とコア）

```sh
cmake -S . -B build -DMSX_BUILD_TESTS=ON -DMSX_BUILD_TOOLS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tools/msxtool/msxtool selftest
```

現在の自動試験: **821 checks / 0 failures**（26 グループ）。
FSK の往復は自作波形と自作復調によるもので、規格適合の証明ではありません。

### MinGW クロスコンパイル（確認用）

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake \
      -DMSX_BUILD_WIN32=ON -DMSX_BUILD_TOOLS=ON -DMSX_BUILD_TESTS=OFF
cmake --build build-win -j
```

## 構成

```
core/                 OS 非依存の C++14 コア（文字コード・トークン化・CAS・FSK・WAV）
tools/msxtool/        コマンドライン
app/win32/            Win32 GUI
tests/                自動試験（外部依存なし）
scripts/              ビルド・検証・ROM ダンプ
installer/            Inno Setup 用スクリプト
docs/                 仕様・解析・形式・検証の記録
examples/             サンプル BASIC
```

## 既知の制限（0.1.0）

- XP SP3・実機・物理テープ・openMSX での受入試験は未実施。
- openMSX 連携（行停止、変数表示）は未実装。調査結果は `docs/openmsx.md`。
- 音声入力の機器選択と録音ミキサー起動は UI にあるが、実機ドライバーでの確認は未実施。
- アプリの復号はステレオ WAV の第 1 チャンネルのみ。CLI は `--channel` に対応。
- 漢字は CF-2000 の文字コードにないため、コンパイル時にエラーとして報告（`?` 置換なし）。
- 全角英数記号は半角へ正規化（例: `！` → `!`）。
- 送信中の進捗は再生位置のみ。実機の受信状態は表示しない。
- インストーラーはスクリプトのみ（Inno Setup でのビルドは未実施）。ZIP 展開で使えます。

## 開発

- 試験: `tests/tests.cpp`（外部フレームワークなし）
- 仕様: [`docs/MSX-CF-2000-technical-spec.md`](docs/MSX-CF-2000-technical-spec.md)
- 解析: [`docs/analysis.md`](docs/analysis.md)
- 形式: [`docs/formats.md`](docs/formats.md)
- 検証: [`docs/verification.md`](docs/verification.md)
- openMSX 調査: [`docs/openmsx.md`](docs/openmsx.md)

## ライセンス

MIT License（[LICENSE](LICENSE)）。同梱・参照している第三者ソフトウェアについては
[THIRD_PARTY_NOTICES.txt](THIRD_PARTY_NOTICES.txt) を参照してください。
openMSX 本体および MSX の ROM は同梱していません。
