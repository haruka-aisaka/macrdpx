# macrdpx

**Mac RDP eXtensions** - a small protocol, carried on one RDP static virtual
channel, that lets a Mac RDP client and a Mac RDP server exchange what standard
RDP loses between two Macs:

- keys as macOS virtual key codes with the exact modifier flags (⌘ stays ⌘,
  Fn, 英数/かな, left/right modifiers), in order with the pointer;
- pixel-precise trackpad scrolling with gesture and momentum phases;
- frame-rate and bit-rate requests from the client (for slow links), and the
  server's applied stream status;
- the server's current input source.

Everything else - graphics, audio, clipboard, display resizing - stays plain
RDP, so either end keeps working with ordinary RDP software.

Used by [macos-rdp-server](https://github.com/mikuta0407/macos-rdp-server)
and mRemote.

## Contents

| Path | |
|---|---|
| `include/macrdpx/macrdpx.h` | The whole API: message types, codec, key tables |
| `src/codec.c` | Wire encoding/decoding, negotiation |
| `src/keys.c` | HID usage ↔ kVK table, modifier flags, modifier tracker |
| `docs/PROTOCOL.md` | Wire format |
| `tests/` | Host tests |

Pure C99, no allocation, no dependencies; builds for macOS, iOS and Mac
Catalyst alike.

## Build and test

```sh
cmake -S . -B build -DCMAKE_C_FLAGS="-fsanitize=address,undefined"
cmake --build build
ctest --test-dir build --output-on-failure
```

As a dependency, either `add_subdirectory(macrdpx)` and link `macrdpx`, or
compile `src/*.c` with `include/` on the include path.

## License

MIT
