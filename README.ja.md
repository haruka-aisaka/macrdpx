# macrdpx

[English](README.md) | 日本語

**Mac RDP eXtensions** は、RDP の static virtual channel `MACRDPX` 1 本の上で
動く小さなプロトコルです。Mac 同士を標準の RDP でつなぐと失われてしまう情報を、
Mac（または iPad）の RDP クライアントと Mac の RDP サーバーの間でやりとりします。
それ以外はすべて通常の RDP のままなので、どちらの側も一般的な RDP ソフトと
そのまま接続できます。

このリポジトリには、プロトコルの仕様と、それを実装した C ライブラリが入っています。

## なぜ必要か

RDP の入力モデルは Windows を前提にしています。Mac から Mac へ RDP でつなぐと、
次のようなものが途中で失われます。

- ⌘、Fn、左右の修飾キー、英数/かなが、PC のスキャンコードに近似されて届く
- トラックパッドのスクロールが粗いホイールの刻みになり、ジェスチャや慣性の
  フェーズがなくなる
- ピンチ、回転、スマートズーム、Dock のスワイプ（操作スペースの切り替え、
  Mission Control）が存在しない
- クライアントから「回線が遅いのでフレームレートを下げてほしい」と伝えられない

macrdpx は、これらを Mac 本来のイベントとして専用のチャネルで送ります。
画面、音声、クリップボード、画面サイズの変更は標準の RDP のままです。

## 機能

機能はそれぞれ capability のビットになっていて、両側が宣言したものだけが
使われます。

| Capability | 送るもの |
|---|---|
| `MAC_KEYS` | macOS の仮想キーコード（kVK）と正確な修飾フラグ、クライアントのキーボード種別（ANSI / ISO / JIS） |
| `POINTER` | クリック回数つきのポインタ移動とボタン。キー入力と順序が保たれる |
| `PRECISE_SCROLL` | `NSEvent` のジェスチャ／慣性フェーズつきのピクセル単位スクロール |
| `GESTURES` | ピンチ、回転、スマートズーム、指に追従する Dock のスワイプ |
| `STREAM_CONFIG` | クライアントがフレームレートとビットレートを要求し、サーバーが適用した値を返す |
| `INPUT_SOURCE` | サーバーで選ばれている入力ソースを通知する |
| `CURSOR_ALPHA` | クライアントが 32bpp のアルファつきカーソルを描けるので、実際のカーソル形状を送れる |

## 互換性

- **標準の RDP と**: `MACRDPX` を知らないクライアントやサーバーはチャネルを
  開かないので、通常の RDP だけが見えます。mstsc、FreeRDP、Windows App などには
  影響しません。
- **入力が二重にならない**: ネゴシエーションで有効になった機能の入力は、
  サーバーの `HELLO` を受け取ったあと、チャネルだけで送られます。それまでは
  クライアントは通常の RDP 入力を送ります。
- **バージョン間**: 受信側は知らない種類のメッセージを読み飛ばし、payload の
  後ろに追加されたフィールドを無視します。今後のバージョンでは、フィールドは
  後ろに追加するだけです。

## 実装

- [macos-rdp-server](https://github.com/mikuta0407/macos-rdp-server):
  macOS 用の RDP サーバー（サーバー側）
- mRemote: iPadOS / iOS / macOS（Mac Catalyst）用の RDP クライアント
  （クライアント側）。未公開です。

## 仕様

ワイヤフォーマット、ネゴシエーション、全メッセージは
[`docs/PROTOCOL.md`](docs/PROTOCOL.md)（英語）で定義しています。正式な仕様は
この文書で、ライブラリはそれに従います。

## ライブラリ

依存ライブラリなし、メモリ確保なしの純粋な C99 です。macOS、iOS、Mac Catalyst で
同じようにビルドでき、C99 コンパイラがあればそれ以外の環境でも使えます。
API はすべて [`include/macrdpx/macrdpx.h`](include/macrdpx/macrdpx.h) にあります。

| API | |
|---|---|
| `mrx_encode` / `mrx_decode` | メッセージ 1 つとバイト列の相互変換。デコードした文字列は入力バッファを指す |
| `mrx_negotiate` | 両側が使える機能を求める |
| `mrx_keycode_for_hid_usage` / `mrx_hid_usage_for_keycode` | HID usage ↔ kVK |
| `mrx_modifier_flags_for_keycode`、`mrx_keycode_is_modifier` | 修飾キーとそのフラグ |
| `mrx_modifier_tracker_*` | 修飾フラグを全部は教えてくれないプラットフォーム（UIKit）のために、押されている修飾キーからフラグを組み立てる |

### 使用例

```c
#include <macrdpx/macrdpx.h>
#include <string.h>

/* クライアント: 使いたい機能を宣言する */
const char *name = "MyClient 1.0; macOS 26.1";
mrx_message hello = { .type = MRX_MSG_HELLO };
hello.u.hello.version = MRX_PROTOCOL_VERSION;
hello.u.hello.role = MRX_ROLE_CLIENT;
hello.u.hello.caps = MRX_CAP_MAC_KEYS | MRX_CAP_POINTER | MRX_CAP_PRECISE_SCROLL;
hello.u.hello.name = name;
hello.u.hello.name_length = (uint8_t)strlen(name);

uint8_t buf[MRX_HEADER_BYTES + 512];
size_t written = 0;
if (mrx_encode(&hello, buf, sizeof buf, &written) == MRX_OK) {
    /* buf[0..written) を MACRDPX チャネルに書く */
}

/* 受信側: 1 回の読み取りに複数のメッセージが入っていることがある */
size_t offset = 0;
while (offset < length) {
    mrx_message m;
    size_t consumed = 0;
    mrx_status s = mrx_decode(data + offset, length - offset, &m, &consumed);
    if (s == MRX_NEED_MORE) break;                 /* 続きのバイトを待つ */
    if (s == MRX_ERROR_UNKNOWN_TYPE) { offset += consumed; continue; }
    if (s != MRX_OK) { /* 壊れたストリーム: チャネルの使用をやめる */ break; }
    /* m を処理する */
    offset += consumed;
}
```

### ビルドとテスト

```sh
cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build
ctest --test-dir build --output-on-failure
```

### 自分のプロジェクトで使う

次のどちらかで組み込めます。

- CMake: `add_subdirectory(macrdpx)` して `macrdpx` ターゲットをリンクします。
  テストは macrdpx 自体がトップレベルのときだけビルドされます。
- それ以外: `include/` をインクルードパスに入れて、`src/codec.c` と
  `src/keys.c` をコンパイルします。

macos-rdp-server と mRemote は、どちらも git submodule として使っています。

## ライセンス

[MIT](LICENSE)
