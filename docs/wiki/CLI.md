# CLI リファレンス

`msxtool.exe` は GUI と同じコアを使うコマンドラインです。

## コマンド

| コマンド | 説明 |
| --- | --- |
| `bas2cas <in.bas> -o <out.cas>` | BASIC を CSAVE（D3）形式の CAS に変換 |
| `bas2wav <in.bas> -o <out.wav>` | BASIC を送信用 WAV に変換 |
| `cas2wav <in.cas> -o <out.wav>` | CAS を WAV に変換 |
| `wav2cas <in.wav> -o <out.cas>` | WAV を CAS に復調 |
| `cas2bas <in.cas> -o <out.bas>` | CAS から BASIC テキストを取り出す |
| `wav2bas <in.wav> -o <out.bas>` | WAV から直接 BASIC を取り出す |
| `cas2bin <in.cas> -o <out.bin>` | CAS からバイナリ（BSAVE D0）を取り出す |
| `normalize <in.wav> -o <out.wav>` | ピーク正規化したコピーを作る |
| `casinfo <in.cas>` | ブロックとファイルの一覧 |
| `validate <in.bas>` | 構文・容量の検証 |
| `tokenize <in.bas> -o <out.bin>` | トークン列に変換 |
| `detokenize <in.bin> -o <out.bas>` | トークン列を BASIC テキストに戻す |
| `selftest` | 内蔵の自己試験 |
| `version` | バージョン表示 |

## 主なオプション

| オプション | 対象 | 説明 |
| --- | --- | --- |
| `--name NAME` | `bas2cas` / `bas2wav` | MSX ファイル名（6 バイト以内） |
| `--baud 1200\|2400` | `bas2wav` / `cas2wav` | 変調速度 |
| `--baud auto\|1200\|2400` | `wav2cas` / `wav2bas` | 復調速度（既定 auto） |
| `--volume 1-100` | `bas2wav` / `cas2wav` | デジタル音量（100% = 約 -7 dBFS） |
| `--channel 0\|1` | `wav2cas` / `wav2bas` | ステレオ WAV のチャンネル |
| `--index N` | `cas2bas` / `cas2bin` | 複数ファイル時の選択 |
| `--target -6` | `normalize` | 目標ピーク（dBFS） |
| `--max-gain 12` | `normalize` | 最大ゲイン（dB） |
| `--force` | 出力全般 | 既存ファイルの上書きを許可 |
| `--tape` | `tokenize` | テープ用の形式で出力 |

## 例

```bat
rem BASIC -> CAS -> WAV -> CAS -> BASIC の往復
msxtool bas2cas HELLO.bas -o HELLO.cas --name HELLO
msxtool cas2wav HELLO.cas -o HELLO.wav --baud 2400 --volume 70
msxtool wav2cas HELLO.wav -o HELLO2.cas
msxtool cas2bas HELLO2.cas -o HELLO2.bas

rem 録音の復号（正規化コピー経由）
msxtool normalize REC.wav -o REC.normalized.wav --target -6 --max-gain 12
msxtool wav2cas REC.normalized.wav -o REC.cas
```

## 注意

- ファイル名は MSX の 6 バイト制限です。超過はエラーで、**切り詰めません**。
- 既存の出力は `--force` なしでは上書きしません。
- `normalize` は無音 WAV をエラーにし、出力ファイルを残しません。
- Windows のコンソールでは日本語を UTF-16 で出力します（リダイレクト時は UTF-8）。
