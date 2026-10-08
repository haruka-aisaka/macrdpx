# macrdpx wire protocol, version 1

macrdpx runs on one RDP **static virtual channel** named `MACRDPX`
(`CHANNEL_OPTION_INITIALIZED`, no compression requirement). A client that wants
the extensions lists the channel in its Client Network Data; a server that
supports them opens it. Either side that does not know the channel simply never
sees it, so standard RDP is unaffected.

## Framing

Every message is a header followed by a payload. All integers are
little-endian; `f32` is an IEEE-754 single, also little-endian.

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | type |
| 2 | u16 | reserved, 0 |
| 4 | u32 | payload length (at most 65536) |
| 8 | ... | payload |

One channel write may carry several messages back to back. A receiver:

- skips a message of unknown type by its length;
- accepts a payload longer than this document describes and ignores the extra
  bytes - later versions only ever **append** fields;
- treats a payload shorter than described, an out-of-range value, or a length
  above 65536 as a broken stream and stops using the channel.

## Negotiation

1. When the channel connects, the client sends `HELLO` (role client) with the
   features it wants.
2. The server answers `HELLO` (role server) with the features it supports.
3. Both use `mrx_negotiate(client, server)`: the intersection, except that
   `MAC_KEYS` is dropped unless `POINTER` is also common.

Until the server's `HELLO` arrives the client keeps sending ordinary RDP input.
After it, input covered by a negotiated feature goes **only** on the channel,
so the server never sees a key twice.

## Capabilities

| Bit | Name | Meaning |
|---|---|---|
| 0 | `MAC_KEYS` | Keys as macOS virtual key codes with full modifier flags. |
| 1 | `POINTER` | Pointer motion and buttons on the channel (ordered with keys). |
| 2 | `PRECISE_SCROLL` | Pixel scrolling with gesture and momentum phases. |
| 3 | `STREAM_CONFIG` | Client sets frame rate / bit rate; server reports status. |
| 4 | `INPUT_SOURCE` | Server reports its selected keyboard input source. |
| 5 | `CURSOR_ALPHA` | Client draws 32-bpp alpha colour pointers; real cursor shapes may be sent. |

## Messages

Modifier flags (`mods`, u64) use the `CGEventFlags` /
`NSEvent.ModifierFlags` bit layout, device-dependent left/right bits included;
bits outside `0x00FF207F` are cleared.

### 0x0001 HELLO (both directions)

| Off | Size | Field |
|---|---|---|
| 0 | u16 | protocol version (1) |
| 2 | u8 | role: 1 client, 2 server |
| 3 | u8 | name length *n* |
| 4 | u32 | capabilities |
| 8 | *n* | name, UTF-8, e.g. `mRemote 1.0; macOS 26.1` |

### 0x0010 KEYBOARD_INFO (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | u32 | macOS keyboard type: 40 ANSI, 41 ISO, 42 JIS, 0 unknown |

The server posts key events as this keyboard type, so the server's own input
source maps the keys as the client's physical keyboard is labelled.

### 0x0011 KEY (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | u16 | kVK key code, 0...127 |
| 2 | u8 | 0 down, 1 up |
| 3 | u8 | flags: bit 0 auto-repeat |
| 4 | u64 | mods, the state after this event |

Modifier keys (Control, Shift, Option, Command, left and right; Fn = 63;
Caps Lock = 57) become flags-changed events. Keys are positional: the
character typed is decided by the server's input source and keyboard type.

### 0x0012 MODIFIERS (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | u64 | mods |

Sent when the client window gains focus. The server releases any key it still
holds and adopts these modifiers and the Caps Lock state.

### 0x0020 POINTER (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | u8 | 0 move, 1 down, 2 up |
| 1 | u8 | button: 0 left, 1 right, 2 middle, 3...31 others |
| 2 | u16 | click count (`NSEvent.clickCount`; 0 for moves) |
| 4 | u32 | buttons held after the event, bit per button |
| 8 | f32 | x, remote desktop pixels |
| 12 | f32 | y |
| 16 | u64 | mods |

A move with buttons held is posted as a drag of the lowest held button.

### 0x0021 SCROLL (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | f32 | delta x |
| 4 | f32 | delta y (positive moves content down, as `scrollingDeltaY`) |
| 8 | u8 | unit: 0 pixels (precise), 1 lines |
| 9 | u8 | phase (`NSEvent.Phase` bits) |
| 10 | u8 | momentum phase (`NSEvent.Phase` bits) |
| 11 | u8 | flags: bit 0 direction inverted from device ("natural") |
| 12 | u64 | mods |

Phases follow AppKit: a gesture is `MAY_BEGIN`? → `BEGAN` → `CHANGED`* →
`ENDED`/`CANCELLED`, then optionally momentum events with phase 0 and momentum
`BEGAN` → `CHANGED`* → `ENDED`. Deltas are in points of the server's screen.

### 0x0030 STREAM_CONFIG (client to server)

| Off | Size | Field |
|---|---|---|
| 0 | u16 | max frames per second, 0 = server default |
| 2 | u16 | reserved |
| 4 | u32 | target bit rate in kbit/s, 0 = server default |

May be sent at any time (for example when the client's network changes).
The server clamps to what it supports.

### 0x0031 STREAM_STATUS (server to client)

| Off | Size | Field |
|---|---|---|
| 0 | u16 | max frames per second in effect |
| 2 | u16 | scale percent (100 normal, 200 HiDPI) |
| 4 | u32 | bit rate in kbit/s |
| 8 | u32 | desktop width, pixels |
| 12 | u32 | desktop height, pixels |

### 0x0040 INPUT_SOURCE (server to client)

| Off | Size | Field |
|---|---|---|
| 0 | u8 | 1 if the source is ASCII-capable |
| 1 | u8 | id length *n* |
| 2 | *n* | TIS input source id, UTF-8 |
