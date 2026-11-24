# トラブルシューティング

## ビルド

### CMake がコンパイラーを検出できない

- `VCTargetsPath` が残っていないか確認してください（`scripts\build-msvc-xp.bat` は自動解除します）。
- 失敗時は `build-win-xp\CMakeFiles\CMakeConfigureLog.yaml` に実際の MSBuild エラーが残ります。
- CMake 4.x と VS2017 15.9 の組み合わせで失敗する場合は、CMake 3.31.x を試してください。
- WSL/UNC パス上では失敗します。`C:\...` にコピーしてビルドしてください。

### `v141_xp` が見つからない

VS2017 の「C++ Windows XP Support for VS 2017 (v141) tools [Deprecated]」を
インストールしてください。

## 実行

### 起動時に SmartScreen の警告が出る

署名なしのためです。「詳細情報」→「実行」で起動できます。

### 日本語が文字化けする（CLI）

Windows のコンソールでは UTF-16 で出力します。リダイレクトした場合は UTF-8 です。
`chcp 65001` にするか、UTF-8 対応のエディターで開いてください。

## 音声

### 復調に失敗する

1. 録音レベルを確認します（CLIP していないか、小さすぎないか）。
2. `normalize` で正規化コピーを作り、それを復号します。
3. `--baud 1200` / `--baud 2400` を明示します。
4. ステレオ WAV の場合は `--channel 0` / `--channel 1` を試します。

```bat
msxtool normalize REC.wav -o REC.normalized.wav
msxtool wav2cas REC.normalized.wav -o REC.cas --baud 1200
```

### 実機で `CLOAD` が失敗する

- 送信音量を下げます（例: `--volume 60`、GUI ならテープ設定）。
- PC の音量ミキサーで波形が歪んでいないか確認します。
- 再生完了は受信成功ではありません。CF-2000 側で `LIST` / `RUN` を確認してください。

### 録音が無音になる

「テープ > 入力レベル監視」でメーターが動くか確認します。
Windows の録音ミキサーで入力デバイスとレベルを確認してください。

## ファイル

### 出力が上書きされない

仕様です。`--force` を付けるか、別名を指定してください。

### ファイル名でエラーになる

MSX のファイル名は 6 バイト以内です。切り詰めは行いません。
