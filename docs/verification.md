# 検証記録（2026-11-24 / 版 0.1.0）

この版は **基本実装のプレビュー** です。仕様 §7 の受入試験 ID に対して、
「自動試験で確認したこと」と「実機・XP で未確認のこと」を分けて記録します。

## 1. 実行環境

| 項目 | 内容 |
| --- | --- |
| コア試験 | Linux, GCC 15.2.0, C++14, CMake 4.2.3 |
| 結果 | 827 checks / 0 failures（`ctest` 1/1） |
| コマンド | `cmake --build build && ctest --test-dir build --output-on-failure` |
| CLI | `msxtool selftest` が 1200/2400 の生成・復調・バイト一致を確認 |
| Win32 | **MSVC v141_xp ビルド成功**（VS2017 15.9.37902.15, CMake 4.5.1, 2026-11-24）。PE subsystem 5.1、UCRT/VCランタイムなし。XP 実機起動は未試験。MinGW 32bit クロスビルドも可 |
| 実機 | CF-2000・XP SP3・録音機は未接続（録音機は故障中） |

## 2. 受入試験との対応

| ID | 仕様の合格条件 | この版の状態 | 自動試験で確認した範囲 |
| --- | --- | --- | --- |
| XP-01 | クリーン XP SP3 にオフライン導入、通常ユーザー起動、更新・削除後もユーザーデータ保持 | **未試験**（ビルドは成功） | v141_xp ビルドと PE 5.1 / インポート検査は確認。XP への導入・起動は未実施 |
| AU-01 | 48 kHz/16 bit/モノラル WAV、30 分連続録音でバッファ欠落 0、形式・サンプル数一致 | **未試験** | WAV ヘッダ・ストリーム書き込み・サイズ上限のコードを実装。実機録音は未実施 |
| AU-02 | 無音/適正/クリップのメーター一致、切断・容量不足・取消・復号エラーを正常完了にしない | **未試験** | 復号の異常系（短い data、奇数チャンク、非 PCM、非 48 kHz）を自動試験。WinMM の実挙動は未試験 |
| TP-01 | 1200 疎通後、2400 で双方向 30 回連続成功、日本語・機種依存文字・容量限界 | **未試験** | 自作波形の 1200/2400 往復、極性反転・DC・雑音・速度 1.05 倍・振幅 0.25 倍を自動試験。実機 CLOAD は未実施 |
| TP-02 | 標準 3 形式の実機書出し・読込み、WAV 復号、CAS 化、再生成、未知信号・断線の失敗提示 | **未試験** | D3/D0/EA の CAS 生成・解析、BASIC/ASCII/バイナリの往復、8 バイト境界、短い data の警告を自動試験。実機は未実施 |
| TP-03 | 物理レコーダーで 2400 書込み・巻戻し・比較・CF-2000 ロード | **機材待ち** | 該当なし |
| ED-01 | XP 日本語 IME、補完、色分け、Undo、リボン、半角カナ・GRAPH の実機往復、位置付きエラー | **未試験** | 文字表 256 バイトの往復、全角→半角、GRAPH 文字、補完候補、字句解析、エラー位置を自動試験。IME/XP 表示は未試験 |
| DB-01 | XP の CF-2000 構成で行停止・ステップ・変数取得・エラー・切断 | **未実装** | openMSX 連携は P3。この版の対象外 |

## 3. 自動試験の内容

`tests/tests.cpp`:

1. `charset_roundtrip` — 全 256 バイト、濁点合成、GRAPH、変換不能文字
2. `bcd_vectors` / `bcd_roundtrip_many` — 単精度・倍精度 BCD の既知バイトと再変換
3. `tokenizer_basic_bytes` — `10 PRINT "HELLO"` の完全なバイト列
4. `tokenizer_numbers` — 0～9、10～255、256～32767、`&H`/`&O`/`&B`、既定倍精度、`!`
5. `tokenizer_comments_data_else` — `REM`、`'`、`DATA`、`ELSE` の `3A A1`
6. `tokenizer_line_refs` — `GOTO` の `0x0E`、行リンク検証
7. `tokenizer_diagnostics` — 未定義行、重複、範囲外、変換不能、行番号なし
8. `detokenize_roundtrip` — 逆変換 → 再トークン化でバイト一致（日本語含む）
9. `cas_roundtrip` — D3 の生成・8 バイト境界・再解析・ファイル名
10. `cas_ascii_and_binary` — EA の 256 バイトブロック、D0 のアドレス検証
11. `fsk_roundtrip_basic` — 1200/2400 の WAV 生成と復調、16 バイトヘッダ + 本体一致
12. `fsk_impairments` — 極性反転、DC +3000、振幅 0.25、雑音
13. `fsk_speed_change` — 1.05 倍に時間伸長した WAV の復調
14. `wav_validation` — 奇数長チャンクのスキップ、44.1 kHz 拒否
15. `pipeline_with_japanese` — BASIC → CAS → WAV → 復号 → CAS → テキスト
16. `completion_basics` — 行番号・キーワード補完、引数ヒント
17. `lexer_basics` — コメント・小数の字句
18. `token_table_lookup` — 2 バイトトークンと単バイトトークン
19. `fsk_stereo_and_manual` — ステレオのチャンネル選択、先頭切断 + 手動 1200 baud
20. `cas_multiple_and_disk_format` — 複数ファイルの CAS、ディスク形式、ASCII 3 ブロック
21. `detokenize_line_address_reference` — `0x0D`（RUN 後の行アドレス）の表示
22. `completion_inside_literal` — 文字列内では補完しない
23. 上記に含む BCD 0 の往復、ヴの濁点合成、8 bit WAV 拒否、EXTENSIBLE PCM
24. `tokenizer_word_operators` — AND/OR/XOR/EQV/IMP/MOD/NOT のトークン化
25. `fsk_dropout_is_not_clean` — データ途中の欠落を `clean=false` として拒否
26. `wav_normalize` — ピーク正規化（+12 dB 上限、-6 dBFS 目標、無音拒否、出力WAVの妥当性）
27. `completion_context` — 行頭は命令、式は関数、GOTO 後は行番号、THEN 後は両方

## 4. Win32 ビルド確認

### 4.1 MSVC v141_xp（2026-11-24 成功）

- 環境: Windows, Visual Studio 2017 Community 15.9.37902.15, CMake 4.5.1,
  v141_xp, v141 14.16.27023, 静的 CRT（`/MT`）。
- ビルド元: ローカルの `C:\...`（WSL/UNC パスでは MSBuild が失敗）。
- 生成物:
  - `build-win-xp\app\win32\Release\msxstudio.exe`
  - `build-win-xp\tools\msxtool\Release\msxtool.exe`
- `msxstudio.exe`:
  - PE subsystem **5.1**
  - 依存: COMCTL32, WINMM, GDI32, USER32, SHELL32, COMDLG32, IMM32, ADVAPI32,
    KERNEL32 のみ
  - UCRT (`ucrtbase.dll`)、VC ランタイム (`vcruntime140` / `msvcp140`)、
    `api-ms-win-*` のインポートなし
  - Vista 以降の既知 API（`CompareStringEx`, `InitializeSRWLock`,
    `CreateEventEx`, `GetTickCount64` など）のインポートなし
- `msxtool.exe`: PE subsystem **5.1**、依存は KERNEL32 のみ。
- `riched20.dll` は実行時に動的ロード（インポート一覧には出ない）。
- **XP SP3 実機での起動・動作は未試験**（XP-01）。インポート検査は
  `scripts\check-xp-binary.bat` で再実行できる。

### 4.2 ビルド警告

- `MSB8051`: v141_xp の非推奨警告。想定どおり。
- 成功ログで出た `C4819`（ソース文字コード）、`C4996`（`_wfopen`/`wcsncpy`）、
  `C4551`（未使用関数の `(void)`）、`C4456`/`C4458`（名前の隠蔽）は修正済み。
  再ビルドで消えることを確認する。
- CLI の日本語出力は、Windows では UTF-8 → UTF-16 の `WriteConsoleW` に変換する
  （リダイレクト時は UTF-8 のまま）。CP932 コンソールでの文字化けを解消。
- 2026-11-24: GitHub Actions（`windows-2022`）に v141_xp を導入してビルドする
  ジョブを追加。PE 5.01、XP 非対応インポートなしを確認（所要 4 分 37 秒）。
  タグ `v*` で `-win32.zip` と `-win32-xp.zip` をリリースへ自動添付する。
- `MSB8051` 以外の警告が残る場合はビルドログを記録する。

### 4.3 MinGW クロスビルド

- `i686-w64-mingw32-g++`（GCC 13, win32 threads）でも `msxstudio.exe` をビルド。
- これは構文・リソース・リンクの補助確認であり、正式配布ビルドではない。

## 5. 未確認・リスク

| リスク | 影響 | 次に必要なこと |
| --- | --- | --- |
| CF-2000 ROM のトークン受入 | 生成 CAS が CLOAD できない可能性 | TP-01/02。ROM の CSAVE サンプルと行リンク・数値表現を比較 |
| 行リンクの基準アドレス | 0x8000 は 16 KB RAM と FRE(0)=12431 から推定 | 実機で CSAVE したバイト列と比較（ROM は再計算するため実行には影響しにくい） |
| 文字表 | MSX1 と MSX2+ のひらがな差異、CF-2000 のフォント | ED-01 で画面と往復を確認 |
| 実機音声 | AGC・周波数特性・レベル・速度むら | AU-01/02、TP-01/02。基準録音を 48 kHz で保存 |
| XP GUI | RichEdit 2.0、IME、色分け、メニュー操作 | XP SP3 実機で ED-01 |
| インストーラー | Inno 5.6.1 の日本語メッセージ、通常ユーザー導入 | XP で XP-01 |
| openMSX | 0.12.0 の pipe 制御と CF-2000 XML | P3 で DB-01 |

「構造を認識」と「照合済み」は別状態です。現在の復号は構造の認識までで、
既知の期待値との照合（`CLOAD?` 相当）は実装していません。
