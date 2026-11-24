# MSX CF-2000 Studio

National **CF-2000**（日本語 MSX1 / MSX BASIC 1.0 / 16 KB RAM）向けの、
BASIC 編集・トークン化・カセット（FSK）変復調ツールです。

- バージョン: 0.1.0（プレビュー）
- 対応: Windows 32 bit（XP SP3 は v141_xp ビルド）
- リポジトリ: <https://github.com/Reimu0929/MX-Studio>
- ライセンス: MIT

## はじめる

1. [インストール](Install) — ZIP を展開するだけ
2. [GUI の使い方](GUI) または [CLI リファレンス](CLI)
3. うまくいかないときは [トラブルシューティング](Troubleshooting)

## そのほか

- [ビルド](Build) — Windows XP 対応ビルド、Linux、MinGW
- [実機 ROM のダンプ](ROM-dump) — 半田を剥がさずカセット経由で吸い出す
- [制限と未検証項目](Limitations)

## 状態

- 自動試験: **821 checks / 0 failures**
- Windows: MSVC でビルドし、`selftest` と往復変換を CI で確認
- **XP SP3 実機・物理テープ・openMSX は未検証**

## 基本の流れ

```
BASIC ソース (.bas)
   │  F5 検証 / トークン化
   ├─ CAS (.cas) ── CAS -> WAV ──> PC の音声出力 -> CF-2000 の CLOAD
   └─ WAV (.wav) ── CAS -> WAV ──> PC の音声出力 -> CF-2000 の CLOAD

CF-2000 の CSAVE
   │  カセット出力 -> PC の録音入力
   └─ WAV 録音 -> 正規化コピー -> CAS / BASIC へ復号
```
