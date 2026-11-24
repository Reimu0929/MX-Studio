# openMSX 0.12.0 調査（P3: デバッガー連携）

対象: National CF-2000（MSX1 / MSX BASIC 1.0 / RAM 16 KB）
基準バックエンド: openMSX **0.12.0**（2015-09-16）Windows **x86**

この文書は、openMSX をこのアプリから使うために実際のソース・配布バイナリ・
マシン定義を調査した結果です。実機 ROM と openMSX を使った動作確認はまだ行って
いません（未検証と明記した箇所があります）。

## 結論

1. **openMSX 0.12.0 x86 は XP 世代のバイナリ**（PE 5.1、XP にある DLL のみ、
   SDL は静的リンク、コンソールアプリ）。XP SP3 で動く見込み。実機未検証。
2. `share/machines/National_CF-2000.xml` が同梱されている。ROM は 32 KB 1 本で、
   ファイル名 `cf-2000_basic-bios1.rom`、SHA1 `302afb5d8be26c758309ca3df611ae69cced2821`。
3. 外部制御は `-control pipe:<name>`。**アプリが名前付きパイプのサーバ**になり、
   openMSX がクライアントとして接続する。openMSX の**応答とイベントは stdout** に出る
   （パイプには戻らない）。ここが最大の落とし穴。
4. デバッグは `debug` サブコマンド（`set_bp` / `set_watchpoint` / `break` / `cont` / `step`）。
   **CURLIN（0xF41C）への write watchpoint** で、ROM のアドレスに依存せずに
   行ブレーク・行ステップを実現できる。
5. 仕様にある `H.NEWS`（0xFF3E）は **MSX2 の BASIC トラップ**で、MSX1 の CF-2000 では
   使えない。行フックは CURLIN 方式に置き換える。

## 1. 配布物と動作環境

GitHub Releases（`RELEASE_0_12_0`）の Windows x86 バイナリ:

- `openmsx-0.12.0-windows-vc-x86-bin.zip`（6,739,493 バイト）
- `openmsx.exe` は **PE subsystem 5.1 / Windows CUI**
- 依存 DLL: `WSOCK32` `WINMM` `Secur32` `OPENGL32` `DSOUND` `KERNEL32`
  `USER32` `GDI32` `ADVAPI32` `WS2_32` のみ。UCRT / VC ランタイム / SDL.dll なし
- `share/systemroms/` に C-BIOS 一式が同梱（ROM なしでも C-BIOS MSX1 で起動可能）

このため、アプリからは `CreateProcess` で起動し、stdout をパイプで受け取れる。
XP 実機での起動は未検証（XP-01 の受入項目に含める）。

## 2. マシン定義（CF-2000）

`share/machines/National_CF-2000.xml` の要点:

```xml
<PPI id="ppi"> ... <keyboard_type>jp_ansi</keyboard_type> ... </PPI>
<VDP id="VDP"><version>TMS99X8A</version><io base="0x98" num="2"/></VDP>
<PSG id="PSG"> ... </PSG>
<PrinterPort id="Printer Port"><io base="0x90" num="2"/></PrinterPort>
<primary slot="0">
  <RAM id="Main RAM"><mem base="0xC000" size="0x4000"/></RAM>
  <ROM id="MSX BIOS with BASIC ROM">
    <rom>
      <filename>cf-2000_basic-bios1.rom</filename>
      <sha1>302afb5d8be26c758309ca3df611ae69cced2821</sha1>
    </rom>
    <mem base="0x0000" size="0x8000"/>
  </ROM>
</primary>
<primary external="true" slot="1"/>
<primary external="true" slot="2"/>
<primary slot="3"/>
```

- ROM は **0x0000–0x7FFF の 32 KB 1 本**（BIOS + BASIC）
- RAM は **0xC000–0xFFFF の 16 KB**（`Main RAM`）
- スロット 1/2 は外部、スロット 3 は空

ROM の置き場所（0.12.0）:

- `<openMSX を展開したフォルダ>\share\systemroms\cf-2000_basic-bios1.rom`
- または `My Documents\openMSX\share\systemroms\`（Windows のユーザー側 share）

`cf2000.rom` をダンプしたら、このファイル名でコピーする。SHA1 が一致しない場合は
マシン定義の `<sha1>` を自分のダンプの値に書き換えた XML を使う（配布はしない）。

## 3. 制御プロトコル

### 3.1 接続の形

```
[MSX CF-2000 Studio]                         [openmsx.exe]
  CreateNamedPipe(\\.\pipe\<name>)  <----read----  -control pipe:<name>
  CreatePipe(stdout)                <----XML-----   stdout（reply/update/log）
  WriteFile(<command>...)           ---write---->   コマンド受信
```

- `-control pipe:<name>` の `<name>` は自由。アプリが**先に**名前付きパイプを作る。
- openMSX は `CreateFileA("\\\\.\\pipe\\<name>", GENERIC_READ, ...)` で接続し、
  パイプから XML コマンドを読む。
- **openMSX の応答はパイプではなく `std::cout`**（`PipeConnection::output`）。
  アプリは stdout を別パイプにリダイレクトして読む。
- openMSX はコンソールアプリなので `STARTF_USESTDHANDLES` で stdout を渡せる。

起動例:

```bat
openmsx.exe -machine National_CF-2000 -control pipe:msxstudio-1234
```

`-control` を付けると画面は `renderer none` で始まる。ウィンドウを出す場合は接続後に
`<command>set renderer SDL</command>` を送る。

### 3.2 XML

openMSX の出力は `<openmsx-output>` で始まり、次の要素が流れる。

```xml
<log level="info">...</log>
<reply result="ok">...</reply>
<reply result="nok">invalid command name "biep"</reply>
<update type="status" machine="machine1" name="cpu">suspended</update>
<update type="status" machine="machine1" name="cpu">running</update>
```

アプリからは次の形で送る（先頭の `<openmsx-control>` は任意。パーサーは
`<command>` を直接探す）。

```xml
<openmsx-control>
<command>openmsx_update enable status</command>
<command>debug cont</command>
```

`reply` は送った順に返る。`nok` は失敗。タイムアウトはアプリ側で持つ。

### 3.3 状態イベント

`openmsx_update enable status` で、デバッグ停止・再開が分かる。

- `name="cpu"` の `suspended` / `running`
- `name="paused"` の `true` / `false`（ポーズ設定）

これで「停止中」「実行中」「切断」を UI に反映できる。

## 4. デバッグ API（0.12.0 で確認した範囲）

`debug` のサブコマンド:

| サブコマンド | 用途 |
| --- | --- |
| `debug break` / `cont` / `step` | 停止 / 再開 / 1 命令 |
| `debug breaked` | 停止中か |
| `debug read <name> <addr>` | 1 バイト読み |
| `debug write <name> <addr> <val>` | 1 バイト書き |
| `debug read_block <name> <addr> <n>` | ブロック読み（Tcl バイナリ） |
| `debug write_block <name> <addr> <data>` | ブロック書き（Tcl バイナリ） |
| `debug set_bp <addr> [<cond>] [<cmd>]` | 実行アドレスのブレーク |
| `debug set_watchpoint <type> <addr> [<cond>] [<cmd>]` | メモリ/I/O の読み書き監視 |
| `debug list_bp` / `remove_bp` | ブレーク一覧 / 削除 |
| `debug list_watchpoints` / `remove_watchpoint` | 監視一覧 / 削除 |
| `debug disasm [<addr>]` | 逆アセンブル |
| `debug list` | デバッグ可能デバイス一覧 |
| `debug probe ...` | プローブ（CPU 以外の値） |

watchpoint の `type` は `read_io` / `write_io` / `read_mem` / `write_mem`。

重要な仕様（ソースで確認）:

- **write の watchpoint は書き込み後**に評価される（`writeMemSlow` のコメントと実装）。
  **read は読み込み前**。
- 条件は Tcl の式として評価され、真ならコマンド（Tcl スクリプト）を実行する。
- 条件・コマンドの中では `$wp_last_address` と `$wp_last_value` が使える。

例:

```tcl
# 0x8000 に 0 が書かれたら止める
debug set_watchpoint write_mem 0x8000 {[debug read "Main RAM" 0x8000] == 0} {debug break}

# C レジスタが 0x2F のときだけ 0xF37D で止める
debug set_bp 0xF37D {[reg C] == 0x2F}
```

ゼロページ変数の読み書きには `debug read "Main RAM" 0xF41C` のように
デバイス名を指定する（CF-2000 の RAM デバイス名は `Main RAM`）。

## 5. BASIC の行デバッグ（ROM 非依存の方式）

### 5.1 使えるワークエリア

MSX の標準ワークエリア（MSX2 Technical Handbook / MSX Wiki で確認）:

| アドレス | 名前 | 長さ | 内容 |
| --- | --- | --- | --- |
| 0xF41C | CURLIN | 2 | 実行中の BASIC 行番号（ダイレクトモードは 0xFFFF） |
| 0xF676 | TXTTAB | 2 | BASIC プログラムの開始アドレス |
| 0xF6BE | OLDLIN | 2 | 最後に実行した行番号 |
| 0xF6C0 | OLDTXT | 2 | 次に実行するテキストアドレス |
| 0xF6C2 | VARTAB | 2 | 単純変数の開始 |
| 0xF6C4 | ARYTAB | 2 | 配列変数の開始 |
| 0xF6C6 | STREND | 2 | 変数領域の終端 |
| 0xF7C4 | TRCFLG | 1 | TRON のトレースフラグ |

### 5.2 行の到達検出

BASIC は行を実行するたびに CURLIN を書き換える。そこで **CURLIN の書き込みを
watchpoint で監視**する。ROM のアドレスもシンボルも不要。

```tcl
set ::msx_lines {100 200}
set ::msx_step 0
debug set_watchpoint write_mem 0xF41D true {
    set line [expr {[debug read "Main RAM" 0xF41C] + 256 * [debug read "Main RAM" 0xF41D]}]
    if {$::msx_step} {
        set ::msx_step 0
        debug break
    } elseif {[lsearch -exact $::msx_lines $line] >= 0} {
        debug break
    }
}
```

- 0xF41D は CURLIN の上位バイト。Z80 の 16 bit ストアは下位→上位の順なので、
  上位バイトの書き込み後は CURLIN が確定している。
- 条件を `true` にして、ブレークするかどうかを **openMSX 内の Tcl** で判定する。
  こうするとブレークしない行では制御パイプの往復が発生しない。
- `::$msx_lines` を更新すれば、行ブレークポイントの設定・解除ができる。
- 行ステップは `::$msx_step` を 1 にして `debug cont`。次の行到達で止まる。
- 同一行への `GOTO` も CURLIN の書き込みとして観測できる（行到達として扱える）。

停止後、アプリは CURLIN を読んで現在行を表示し、必要なら
`debug read` で変数領域を読む。

### 5.3 プログラムの投入

2 通りを用意する。

1. **RAM に直接書く（推奨・高速）**
   - トークン化済みイメージを `TXTTAB`（0x8000）へ `debug write_block`
   - `VARTAB = プログラム終端 + 2`、`ARYTAB = VARTAB`、`STREND = VARTAB` を書く
   - `type "RUN&#13;"` で実行（`&#13;` は CR。キーマップで Enter に割り当て済み）
   - CLOAD の待ち時間がない。要実機 ROM での検証。
2. **CAS を再生する（互換経路）**
   - `cassetteplayer insert <file.cas>` → `cassetteplayer play`
   - `type "CLOAD&#13;"` → 完了後に `RUN`
   - 実機と同じ経路。ただし遅い。

`type` は 0.12.0 にあり、`&` などの XML エンティティと `&#13;` に対応している。

### 5.4 変数の表示

- 単純変数: `VARTAB` から `ARYTAB` まで。各変数は 2 バイトの名前
  （最後の文字 + 0x80、型サフィックスを含む）と値。
- 配列: `ARYTAB` から `STREND` まで。次元情報の解析が必要。
- 読み出しは `debug read_block` の結果を Tcl 側で `binary scan ... H*` して
  16 進文字列に変換すると、XML にバイナリを混ぜずに済む。

```tcl
binary scan [debug read_block "Main RAM" 0xF6C2 4] H* hex
```

### 5.5 できないこと / 注意

- 文単位のステップは、行内の文境界を ROM から特定する必要がある。今回は行単位のみ。
- `H.NEWS`（0xFF3E）は MSX2 の BASIC トラップ。MSX1 の CF-2000 では呼ばれない。
- CURLIN はダイレクトモードで 0xFFFF になる。`RUN` 前後の遷移を無視する。
- 割り込み処理中は CURLIN が一時的に変わる可能性がある。実 ROM で確認する。
- 1 行ごとに Tcl が走るため、`debug cont` を多用する方式よりは速いが、
  ブレークしない行でも数回の `debug read` が発生する。

## 6. 実装案（P3）

```
app/win32/openmsx.h/.cpp
  OpenMsxProcess    : 起動・終了・stdout パイプ・名前付きパイプ
  OpenMsxProtocol   : <command>/<reply>/<update>/<log> の解析
  OpenMsxDebugger   : 行ブレーク、行ステップ、変数、停止状態
```

- 起動時にパイプを作り、`-control pipe:<name> -machine National_CF-2000` で起動。
- stdout 読み取りスレッドで XML を解析し、UI スレッドへ PostMessage。
- コマンドは ID 付きで送り、`<reply>` の順序で対応付ける（タイムアウト付き）。
- `openmsx_update enable status` を有効化し、`suspended` / `running` を UI に反映。
- ROM が見つからない場合は openMSX の `<log>` を検出して「ROM 未設定」と表示。
- C-BIOS モード（`C-BIOS_MSX1`）を開発用のフォールバックにする。
  CF-2000 固有の挙動（文字コード、BASIC のワークエリア）は ROM が必要。

## 7. 検証手順（実機 ROM 入手後）

1. `cf2000.rom` の SHA1 が `302afb5d8be26c758309ca3df611ae69cced2821` と一致するか確認。
2. `share/systemroms/cf-2000_basic-bios1.rom` に配置。
3. `openmsx.exe -machine National_CF-2000 -control pipe:test` で起動。
4. `<command>debug read "Main RAM" 0xF41C</command>` が返ることを確認。
5. 小さな BASIC プログラムを投入し、CURLIN の watchpoint で行停止するか確認。
6. 行ステップ、同一行への GOTO、RUN 直後の 0xFFFF を確認。
7. 変数領域（VARTAB–ARYTAB）の読み出しと表示を確認。

C-BIOS MSX1 では 3–6 の通信・停止の土台だけを先に確認できる（ワークエリアの
値は CF-2000 と異なる可能性がある）。

## 8. 参考

- openMSX 0.12.0 `doc/manual/openmsx-control.html`（制御プロトコル）
- `src/events/CliConnection.cc`（Windows の pipe 実装、stdout 出力）
- `src/CommandLineParser.cc`（`-control pipe:<name>`）
- `src/debugger/Debugger.cc`（`debug` サブコマンド）
- `src/cpu/BreakPointBase.cc`（条件は Tcl 式、コマンドは Tcl スクリプト）
- `src/cpu/MSXCPUInterface.cc`（write watch は書き込み後、read は読み込み前）
- `share/machines/National_CF-2000.xml`（ROM 名・SHA1・RAM）
- MSX2 Technical Handbook Appendix 4 / MSX Wiki: CURLIN, VARTAB, ARYTAB, STREND
