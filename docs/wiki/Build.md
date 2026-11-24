# ビルド

## Windows XP 対応（MSVC v141_xp）

仕様の基準です。`WINVER` / `_WIN32_WINNT = 0x0501`、PE 5.01、静的 CRT。

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

- `VCTargetsPath` が残っていると v141 を見失うため、スクリプトが自動で解除します。
- WSL/UNC パス（`\\wsl$\...`）では MSBuild が失敗することがあります。`C:\...` に置いてください。
- CMake は新しい SDK を表示しても、`v141_xp` は実際には VS2017 世代の SDK を使います。
  必要なら `set MSX_WINDOWS_SDK=10.0.10240.0` で明示できます。
- `MSB8051`（XP ツールセット非推奨）警告は想定どおりです。

VS2019/2022 に v141_xp を追加している場合:

```bat
set MSX_VS_GENERATOR=Visual Studio 17 2022
scripts\build-msvc-xp.bat
```

## Linux / macOS（CLI とコア）

```sh
cmake -S . -B build -DMSX_BUILD_TESTS=ON -DMSX_BUILD_TOOLS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
./build/tools/msxtool/msxtool selftest
```

## MinGW クロスコンパイル（確認用）

```sh
cmake -S . -B build-win -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw32.cmake \
      -DMSX_BUILD_WIN32=ON -DMSX_BUILD_TOOLS=ON -DMSX_BUILD_TESTS=OFF
cmake --build build-win -j
```

`msxtool.exe` と `msxstudio.exe` は静的リンクされ、追加 DLL は不要です。

## 確認スクリプト

| スクリプト | 内容 |
| --- | --- |
| `scripts\check-vs2017.bat` | VS2017 と v141_xp の有無 |
| `scripts\check-xp-binary.bat` | PE サブシステムと Vista 以降のインポート検査 |
| `scripts\smoke-test.bat` | selftest、往復変換、正規化、再トークン化の一致 |
| `scripts\dump-cf2000-rom.bat` | 実機 ROM のダンプ（[ROM ダンプ](ROM-dump)） |

## CI

GitHub Actions で次を実行しています。

- Linux: ビルド、`ctest`、`selftest`、往復変換
- Windows: MSVC（Win32）でビルド、`ctest`、`selftest`、往復変換、ZIP 作成
- Windows XP: v141_xp ツールセットを導入してビルドし、インポート検査と
  smoke test を実行（タグ時と手動実行時のみ）。タグ `v*` では 2 つの ZIP が
  リリースに自動添付されます
