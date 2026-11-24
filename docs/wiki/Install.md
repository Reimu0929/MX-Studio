# インストール

## 必要なもの

- Windows 32 bit（Windows XP SP3 ～ 11）
- 音声の入出力（送信・録音に使用）
- CF-2000 実機（任意）

**インストール作業はありません。** VC++ ランタイムや .NET も不要です（静的リンク）。

## 手順

1. [Releases](https://github.com/Reimu0929/MX-Studio/releases) から
   `MX-Studio-<版>-win32.zip` をダウンロードします。
2. 好きなフォルダー（例: `C:\MSXStudio`）へ展開します。
3. `msxstudio.exe` を実行します。

```
MX-Studio/
  msxstudio.exe         GUI
  msxtool.exe           CLI
  examples/HELLO.bas    サンプル
  docs/                 仕様・解析・形式・検証
  scripts/              検証・ROM ダンプ
  LICENSE
  THIRD_PARTY_NOTICES.txt
```

## Windows XP で使う場合

配布 ZIP は CI の MSVC ビルドです。XP 対応（PE 5.01 / 静的 CRT / v141_xp）の
バイナリは、VS2017 + v141_xp を用意して [ビルド](Build) の手順で作成してください。

## 設定の保存場所

設定はレジストリの `HKCU\Software\MSXCF2000Studio` に保存されます。

- ウィンドウ位置、最後に開いたファイル
- テープ名、ボーレート、送信音量、自動正規化の有無
- 入力デバイス

## アンインストール

フォルダーを削除するだけです。設定も消す場合は次を実行してください。

```bat
reg delete "HKCU\Software\MSXCF2000Studio" /f
```

## SmartScreen の警告

署名のない実行ファイルのため、初回起動時に「Windows によって PC が保護されました」
と表示されることがあります。「詳細情報」→「実行」で起動できます。
