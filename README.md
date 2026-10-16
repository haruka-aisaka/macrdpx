# macrdpx

English | [日本語](README.ja.md)

**Mac RDP eXtensions**: a small protocol carried on one RDP static virtual
channel, `MACRDPX`. It lets a Mac (or iPad) RDP client and a Mac RDP server
exchange what standard RDP loses between two Macs. Everything else stays plain
RDP, so both ends keep working with ordinary RDP software.

This repository holds the protocol specification and a C library that
implements it.

## Why

RDP's input model was designed for Windows. When a Mac connects to a Mac over
it, a lot gets lost on the way:

- ⌘, Fn, left and right modifiers, and 英数/かな arrive only as approximate
  PC scan codes;
- trackpad scrolling becomes coarse wheel steps, with no gesture or momentum
  phases;
- pinch, rotate, smart zoom and the Dock's swipes (Spaces, Mission Control)
  don't exist;
- the client can't say "my network is slow, lower the frame rate".

macrdpx carries these as Mac-native events on their own channel. Graphics,
audio, clipboard and display resizing stay standard RDP.

## Features

Each feature is a capability bit that both ends must announce. Only the
features they share are used.

| Capability | What it carries |
|---|---|
| `MAC_KEYS` | Keys as macOS virtual key codes (kVK) with exact modifier flags, plus the client's keyboard type (ANSI / ISO / JIS) |
| `POINTER` | Pointer moves and buttons with click counts, ordered with the keys |
| `PRECISE_SCROLL` | Pixel scrolling with `NSEvent` gesture and momentum phases |
| `GESTURES` | Pinch, rotate, smart zoom, and Dock swipes that follow the fingers |
| `STREAM_CONFIG` | Client asks for a frame rate and bit rate; server reports what it applied |
| `INPUT_SOURCE` | Server reports its selected keyboard input source |
| `CURSOR_ALPHA` | Client can draw 32-bpp alpha cursors, so real cursor shapes may be sent |

## Compatibility

- **With standard RDP.** A client or server that doesn't know `MACRDPX` never
  opens the channel and sees ordinary RDP. mstsc, FreeRDP, Windows App and
  others are unaffected.
- **No double input.** Input for a negotiated feature goes only on the
  channel, and only after the server's `HELLO`. Before that, the client sends
  ordinary RDP input.
- **Between versions.** Receivers skip message types they don't know and
  ignore fields appended to a payload. Later versions only append.

## Implementations

- [macos-rdp-server](https://github.com/mikuta0407/macos-rdp-server): an RDP
  server for macOS (server side).
- mRemote: an RDP client for iPadOS, iOS and macOS (Mac Catalyst), client
  side. Not public yet.

## Specification

[`docs/PROTOCOL.md`](docs/PROTOCOL.md) defines the wire format, the
negotiation and every message. It is the normative document; the library
follows it.

## The library

The library is pure C99 with no dependencies and no allocation. It builds the
same for macOS, iOS and Mac Catalyst, and anywhere else with a C99 compiler.
The whole API is in [`include/macrdpx/macrdpx.h`](include/macrdpx/macrdpx.h).

| API | |
|---|---|
| `mrx_encode` / `mrx_decode` | One message to and from bytes; decoded strings borrow from the input buffer |
| `mrx_negotiate` | The features both ends may use |
| `mrx_keycode_for_hid_usage` / `mrx_hid_usage_for_keycode` | HID usage ↔ kVK |
| `mrx_modifier_flags_for_keycode`, `mrx_keycode_is_modifier` | Modifier keys and their flags |
| `mrx_modifier_tracker_*` | Builds full modifier flags for senders whose platform doesn't report them (UIKit) |

### Example

```c
#include <macrdpx/macrdpx.h>
#include <string.h>

/* Client: announce the features it wants. */
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
    /* write buf[0..written) to the MACRDPX channel */
}

/* Receiver: one channel read may hold several messages. */
size_t offset = 0;
while (offset < length) {
    mrx_message m;
    size_t consumed = 0;
    mrx_status s = mrx_decode(data + offset, length - offset, &m, &consumed);
    if (s == MRX_NEED_MORE) break;                 /* wait for more bytes */
    if (s == MRX_ERROR_UNKNOWN_TYPE) { offset += consumed; continue; }
    if (s != MRX_OK) { /* broken stream: stop using the channel */ break; }
    /* handle m */
    offset += consumed;
}
```

### Build and test

```sh
cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build
ctest --test-dir build --output-on-failure
```

### Using it in your project

Use either of these:

- CMake: `add_subdirectory(macrdpx)` and link the `macrdpx` target. The tests
  are built only when macrdpx is the top-level project.
- Anything else: compile `src/codec.c` and `src/keys.c` with `include/` on the
  include path.

Both macos-rdp-server and mRemote use it as a git submodule.

## License

[MIT](LICENSE)
