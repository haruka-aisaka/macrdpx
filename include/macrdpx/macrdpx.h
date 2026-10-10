/*
 * macrdpx - Mac RDP eXtensions.
 *
 * A small protocol carried on one RDP static virtual channel ("MACRDPX")
 * between a Mac-aware RDP client and a Mac RDP server. When both ends join
 * the channel and exchange HELLO, they may replace parts of standard RDP that
 * lose information between two Macs - keyboard scan codes, line-based wheel
 * events, the fixed stream rate - with native equivalents. A client or server
 * that does not know the channel never joins it, and plain RDP is unchanged.
 *
 * This header is the whole public API: value types, the wire codec and the
 * key tables both ends share. Pure C99, no allocation, no dependencies.
 * See docs/PROTOCOL.md for the wire format.
 */
#ifndef MACRDPX_H
#define MACRDPX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Static virtual channel names are at most 7 characters. */
#define MRX_CHANNEL_NAME "MACRDPX"
#define MRX_PROTOCOL_VERSION 1u

#define MRX_HEADER_BYTES 8u
#define MRX_MAX_PAYLOAD_BYTES (64u * 1024u)
#define MRX_MAX_NAME_BYTES 255u

typedef int mrx_status;
enum {
    MRX_OK = 0,
    /* More bytes are needed before a whole message can be read. */
    MRX_NEED_MORE = 1,
    MRX_ERROR_INVALID_ARGUMENT = -1,
    MRX_ERROR_BUFFER_TOO_SMALL = -2,
    MRX_ERROR_MALFORMED = -3,
    MRX_ERROR_TOO_LARGE = -4,
    /* A well-formed message of a type this build does not know. Skip it. */
    MRX_ERROR_UNKNOWN_TYPE = -5
};

typedef uint16_t mrx_message_type;
enum {
    MRX_MSG_HELLO = 0x0001,
    MRX_MSG_KEYBOARD_INFO = 0x0010,
    MRX_MSG_KEY = 0x0011,
    MRX_MSG_MODIFIERS = 0x0012,
    MRX_MSG_POINTER = 0x0020,
    MRX_MSG_SCROLL = 0x0021,
    MRX_MSG_GESTURE = 0x0022,
    MRX_MSG_STREAM_CONFIG = 0x0030,
    MRX_MSG_STREAM_STATUS = 0x0031,
    MRX_MSG_INPUT_SOURCE = 0x0040
};

/* Capabilities each side announces in HELLO. A feature is in use only when
 * both ends announce it (mrx_negotiate). */
typedef uint32_t mrx_caps;
enum {
    /* Keys travel as macOS virtual key codes with full modifier flags (KEY,
     * MODIFIERS, KEYBOARD_INFO) instead of RDP scan codes. */
    MRX_CAP_MAC_KEYS = 1u << 0,
    /* Pointer moves and buttons travel on the channel (POINTER), carrying the
     * modifier flags and the client's own click count. Required with
     * MAC_KEYS so a modifier and a click can never be reordered. */
    MRX_CAP_POINTER = 1u << 1,
    /* Pixel-precise scrolling with phases and momentum (SCROLL). */
    MRX_CAP_PRECISE_SCROLL = 1u << 2,
    /* The client may set the frame rate and bit rate (STREAM_CONFIG); the
     * server reports what it applied (STREAM_STATUS). */
    MRX_CAP_STREAM_CONFIG = 1u << 3,
    /* The server reports its current keyboard input source (INPUT_SOURCE). */
    MRX_CAP_INPUT_SOURCE = 1u << 4,
    /* The client draws 32-bit colour pointers with alpha correctly, so the
     * server may send the real Mac cursor shapes. */
    MRX_CAP_CURSOR_ALPHA = 1u << 5,
    /* Trackpad gestures beyond scrolling travel as themselves (GESTURE):
     * pinch, rotate, smart zoom and the Dock's swipes. */
    MRX_CAP_GESTURES = 1u << 6
};

typedef uint8_t mrx_role;
enum {
    MRX_ROLE_CLIENT = 1,
    MRX_ROLE_SERVER = 2
};

/* Modifier flags use the CGEventFlags / NSEvent.ModifierFlags layout,
 * including the device-dependent left/right bits in the low 16 bits. */
#define MRX_FLAG_CAPS_LOCK 0x00010000ull
#define MRX_FLAG_SHIFT 0x00020000ull
#define MRX_FLAG_CONTROL 0x00040000ull
#define MRX_FLAG_OPTION 0x00080000ull
#define MRX_FLAG_COMMAND 0x00100000ull
#define MRX_FLAG_NUMERIC_PAD 0x00200000ull
#define MRX_FLAG_HELP 0x00400000ull
#define MRX_FLAG_FUNCTION 0x00800000ull
#define MRX_FLAG_DEVICE_LEFT_CONTROL 0x00000001ull
#define MRX_FLAG_DEVICE_LEFT_SHIFT 0x00000002ull
#define MRX_FLAG_DEVICE_RIGHT_SHIFT 0x00000004ull
#define MRX_FLAG_DEVICE_LEFT_COMMAND 0x00000008ull
#define MRX_FLAG_DEVICE_RIGHT_COMMAND 0x00000010ull
#define MRX_FLAG_DEVICE_LEFT_OPTION 0x00000020ull
#define MRX_FLAG_DEVICE_RIGHT_OPTION 0x00000040ull
#define MRX_FLAG_DEVICE_RIGHT_CONTROL 0x00002000ull
/* Every bit a sender may set; others are cleared by the codec. */
#define MRX_FLAG_MASK 0x00FF207Full

/* macOS keyboard types (CGEventSourceKeyboardType / LMGetKbdType). */
#define MRX_KEYBOARD_TYPE_UNKNOWN 0u
#define MRX_KEYBOARD_TYPE_ANSI 40u
#define MRX_KEYBOARD_TYPE_ISO 41u
#define MRX_KEYBOARD_TYPE_JIS 42u

#define MRX_KEYCODE_NONE 0xFFFFu
#define MRX_MAX_KEYCODE 127u

typedef struct mrx_hello {
    uint16_t version;
    mrx_role role;
    mrx_caps caps;
    /* Product and OS, e.g. "mRemote 1.0; macOS 26.1". Borrowed: on decode it
     * points into the input buffer. Not NUL-terminated. */
    const char *name;
    uint8_t name_length;
} mrx_hello;

typedef struct mrx_keyboard_info {
    uint32_t keyboard_type;
} mrx_keyboard_info;

typedef uint8_t mrx_key_action;
enum {
    MRX_KEY_DOWN = 0,
    MRX_KEY_UP = 1
};

enum {
    /* An auto-repeated down sent by the client's own repeat. */
    MRX_KEY_FLAG_REPEAT = 1u << 0
};

/* One key, by position: a kVK_* code (HIToolbox/Events.h) 0...127. For the
 * modifier keys (and Caps Lock, Fn) the server posts a flags-changed event.
 * modifier_flags is the full state AFTER this event. */
typedef struct mrx_key {
    uint16_t keycode;
    mrx_key_action action;
    uint8_t flags;
    uint64_t modifier_flags;
} mrx_key;

/* The client's whole modifier state, sent when it (re)gains focus. The
 * server releases every key it still holds and adopts these modifiers. */
typedef struct mrx_modifiers {
    uint64_t modifier_flags;
} mrx_modifiers;

typedef uint8_t mrx_pointer_action;
enum {
    MRX_POINTER_MOVE = 0,
    MRX_POINTER_DOWN = 1,
    MRX_POINTER_UP = 2
};

typedef uint8_t mrx_pointer_button;
enum {
    MRX_BUTTON_LEFT = 0,
    MRX_BUTTON_RIGHT = 1,
    MRX_BUTTON_MIDDLE = 2
    /* 3...31: other buttons, numbered as NSEvent.buttonNumber. */
};
#define MRX_MAX_BUTTON 31u

/* x/y are remote desktop pixels (the RDP coordinate space), fractional
 * allowed. click_count follows NSEvent.clickCount (1 single, 2 double, ...;
 * 0 for moves). A move while buttons are held is a drag; buttons_down is the
 * bit set (1 << button) of buttons held AFTER the event. */
typedef struct mrx_pointer {
    mrx_pointer_action action;
    mrx_pointer_button button;
    uint16_t click_count;
    uint32_t buttons_down;
    float x;
    float y;
    uint64_t modifier_flags;
} mrx_pointer;

typedef uint8_t mrx_scroll_unit;
enum {
    MRX_SCROLL_PIXEL = 0,
    MRX_SCROLL_LINE = 1
};

/* NSEvent.Phase bits (= CGScrollPhase). */
enum {
    MRX_PHASE_NONE = 0,
    MRX_PHASE_BEGAN = 1u << 0,
    MRX_PHASE_STATIONARY = 1u << 1,
    MRX_PHASE_CHANGED = 1u << 2,
    MRX_PHASE_ENDED = 1u << 3,
    MRX_PHASE_CANCELLED = 1u << 4,
    MRX_PHASE_MAY_BEGIN = 1u << 5
};

enum {
    /* The deltas are already "natural" (as NSEvent.isDirectionInvertedFromDevice). */
    MRX_SCROLL_FLAG_INVERTED_FROM_DEVICE = 1u << 0
};

/* One scroll event as NSEvent reports it: delta_y > 0 moves content down
 * (scrolls toward the top), as scrollingDeltaY. phase / momentum_phase are
 * NSEvent.Phase bits; a momentum event has phase NONE. */
typedef struct mrx_scroll {
    float delta_x;
    float delta_y;
    mrx_scroll_unit unit;
    uint8_t phase;
    uint8_t momentum_phase;
    uint8_t flags;
    uint64_t modifier_flags;
} mrx_scroll;

typedef uint8_t mrx_gesture_kind;
enum {
    /* value: magnification since the previous event (0.1 = 10 % larger),
     * as NSEvent.magnification. */
    MRX_GESTURE_MAGNIFY = 0,
    /* value: degrees since the previous event, counter-clockwise positive,
     * as NSEvent.rotation. */
    MRX_GESTURE_ROTATE = 1,
    /* Two-finger double tap. No phase, no value. */
    MRX_GESTURE_SMART_ZOOM = 2,
    /* The Dock's swipes as the fingers move, the screen following them:
     * value is the progress so far in screen widths (heights), positive
     * as the fingers move left (up), so +1 is the next Space to the right
     * (Mission Control); velocity, on the ending event, in the same units per
     * second, decides whether a short swipe still completes. */
    MRX_GESTURE_DOCK_HORIZONTAL = 3,
    MRX_GESTURE_DOCK_VERTICAL = 4
};
#define MRX_GESTURE_MAX MRX_GESTURE_DOCK_VERTICAL

/* One trackpad gesture event. phase uses the NSEvent.Phase bits (BEGAN,
 * CHANGED, ENDED, CANCELLED); a gesture is BEGAN, CHANGED*, then ENDED or
 * CANCELLED. The server posts it at the pointer. */
typedef struct mrx_gesture {
    mrx_gesture_kind kind;
    uint8_t phase;
    float value;
    float velocity;
    uint64_t modifier_flags;
} mrx_gesture;

/* Client wishes for the video stream. Zero leaves the server's default. */
typedef struct mrx_stream_config {
    uint16_t max_fps;
    uint32_t bitrate_kbps;
} mrx_stream_config;

/* What the server applied, sent after STREAM_CONFIG and whenever the stream
 * changes (resize). scale_percent is 100 for a normal display, 200 for HiDPI. */
typedef struct mrx_stream_status {
    uint16_t max_fps;
    uint16_t scale_percent;
    uint32_t bitrate_kbps;
    uint32_t width;
    uint32_t height;
} mrx_stream_status;

/* The server's selected keyboard input source. ascii_capable is 0 while a
 * Japanese/Chinese/Korean input mode is on. id is the TIS source id. */
typedef struct mrx_input_source {
    uint8_t ascii_capable;
    const char *id;
    uint8_t id_length;
} mrx_input_source;

typedef struct mrx_message {
    mrx_message_type type;
    union {
        mrx_hello hello;
        mrx_keyboard_info keyboard_info;
        mrx_key key;
        mrx_modifiers modifiers;
        mrx_pointer pointer;
        mrx_scroll scroll;
        mrx_gesture gesture;
        mrx_stream_config stream_config;
        mrx_stream_status stream_status;
        mrx_input_source input_source;
    } u;
} mrx_message;

/* Writes one message (header + payload) into buf. On MRX_OK *written is its
 * size; on MRX_ERROR_BUFFER_TOO_SMALL *written is the size needed. 64 bytes
 * hold every fixed-size message; HELLO and INPUT_SOURCE need up to 320. */
mrx_status mrx_encode(const mrx_message *message, uint8_t *buf, size_t capacity, size_t *written);

/* Reads the first message in buf. *consumed is the bytes it occupied, also
 * for MRX_ERROR_UNKNOWN_TYPE (skip and continue) - but not for MALFORMED or
 * TOO_LARGE, after which the stream cannot be trusted. Payloads longer than
 * this version knows are accepted and the extra bytes ignored, so fields can
 * be appended in later versions. Strings borrow from buf. */
mrx_status mrx_decode(const uint8_t *buf, size_t length, mrx_message *message, size_t *consumed);

/* The features both ends may use. */
mrx_caps mrx_negotiate(mrx_caps client, mrx_caps server);

const char *mrx_status_name(mrx_status status);
const char *mrx_message_type_name(mrx_message_type type);

/* ---- Keys ---------------------------------------------------------------- */

/* The kVK code for a USB HID keyboard usage (page 7), as UIKit reports in
 * UIKey.keyCode, or MRX_KEYCODE_NONE. */
uint16_t mrx_keycode_for_hid_usage(uint16_t usage);

/* The inverse, or 0 for keys without a HID usage (Fn). */
uint16_t mrx_hid_usage_for_keycode(uint16_t keycode);

/* For modifier keys (and Caps Lock, Fn): the device-independent flag and the
 * left/right device flag the key sets while held. Returns 0 for other keys. */
uint64_t mrx_modifier_flags_for_keycode(uint16_t keycode);

/* Keys whose events are flags-changed rather than key down/up. */
int mrx_keycode_is_modifier(uint16_t keycode);

/* Tracks the modifier keys held, for senders whose platform does not report
 * full flags (UIKit). Caps Lock toggles on each down. */
typedef struct mrx_modifier_tracker {
    uint32_t held; /* bit per modifier slot */
    uint8_t caps_lock;
} mrx_modifier_tracker;

void mrx_modifier_tracker_reset(mrx_modifier_tracker *tracker);
/* Records a key transition; returns the flags after it. */
uint64_t mrx_modifier_tracker_update(mrx_modifier_tracker *tracker, uint16_t keycode, mrx_key_action action);
uint64_t mrx_modifier_tracker_flags(const mrx_modifier_tracker *tracker);

#ifdef __cplusplus
}
#endif

#endif
