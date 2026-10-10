#include "macrdpx/macrdpx.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                            \
        }                                                                          \
    } while (0)

static void roundtrip(const mrx_message *in, mrx_message *out, uint8_t *buf, size_t cap) {
    size_t written = 0, consumed = 0;
    CHECK(mrx_encode(in, buf, cap, &written) == MRX_OK);
    CHECK(mrx_decode(buf, written, out, &consumed) == MRX_OK);
    CHECK(consumed == written);
    CHECK(out->type == in->type);
}

static void test_hello(void) {
    uint8_t buf[320];
    mrx_message in = {.type = MRX_MSG_HELLO}, out;
    in.u.hello.version = MRX_PROTOCOL_VERSION;
    in.u.hello.role = MRX_ROLE_CLIENT;
    in.u.hello.caps = MRX_CAP_MAC_KEYS | MRX_CAP_POINTER;
    in.u.hello.name = "mRemote; macOS 26.1";
    in.u.hello.name_length = (uint8_t)strlen(in.u.hello.name);
    roundtrip(&in, &out, buf, sizeof buf);
    CHECK(out.u.hello.version == 1);
    CHECK(out.u.hello.role == MRX_ROLE_CLIENT);
    CHECK(out.u.hello.caps == in.u.hello.caps);
    CHECK(out.u.hello.name_length == in.u.hello.name_length);
    CHECK(memcmp(out.u.hello.name, in.u.hello.name, in.u.hello.name_length) == 0);

    /* Bad role is refused on both sides. */
    in.u.hello.role = 7;
    size_t written;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_ERROR_INVALID_ARGUMENT);
}

static void test_key_and_flags(void) {
    uint8_t buf[64];
    mrx_message in = {.type = MRX_MSG_KEY}, out;
    in.u.key.keycode = 13; /* W */
    in.u.key.action = MRX_KEY_DOWN;
    in.u.key.flags = MRX_KEY_FLAG_REPEAT | 0x80;
    in.u.key.modifier_flags = MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_LEFT_COMMAND | (1ull << 40);
    roundtrip(&in, &out, buf, sizeof buf);
    CHECK(out.u.key.keycode == 13);
    CHECK(out.u.key.flags == MRX_KEY_FLAG_REPEAT);
    CHECK(out.u.key.modifier_flags == (MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_LEFT_COMMAND));

    size_t written;
    in.u.key.keycode = 128;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_ERROR_INVALID_ARGUMENT);
}

static void test_pointer_and_scroll(void) {
    uint8_t buf[64];
    mrx_message in = {.type = MRX_MSG_POINTER}, out;
    in.u.pointer.action = MRX_POINTER_DOWN;
    in.u.pointer.button = MRX_BUTTON_RIGHT;
    in.u.pointer.click_count = 2;
    in.u.pointer.buttons_down = 1u << MRX_BUTTON_RIGHT;
    in.u.pointer.x = 1234.5f;
    in.u.pointer.y = 99.25f;
    in.u.pointer.modifier_flags = MRX_FLAG_CONTROL;
    roundtrip(&in, &out, buf, sizeof buf);
    CHECK(out.u.pointer.button == MRX_BUTTON_RIGHT);
    CHECK(out.u.pointer.click_count == 2);
    CHECK(out.u.pointer.x == 1234.5f && out.u.pointer.y == 99.25f);
    CHECK(out.u.pointer.buttons_down == 2);

    size_t written;
    in.u.pointer.x = NAN;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_ERROR_INVALID_ARGUMENT);

    mrx_message s = {.type = MRX_MSG_SCROLL};
    s.u.scroll.delta_x = -0.5f;
    s.u.scroll.delta_y = 12.75f;
    s.u.scroll.unit = MRX_SCROLL_PIXEL;
    s.u.scroll.phase = MRX_PHASE_NONE;
    s.u.scroll.momentum_phase = MRX_PHASE_CHANGED;
    s.u.scroll.flags = MRX_SCROLL_FLAG_INVERTED_FROM_DEVICE;
    roundtrip(&s, &out, buf, sizeof buf);
    CHECK(out.u.scroll.delta_x == -0.5f && out.u.scroll.delta_y == 12.75f);
    CHECK(out.u.scroll.momentum_phase == MRX_PHASE_CHANGED);
    CHECK(out.u.scroll.flags == MRX_SCROLL_FLAG_INVERTED_FROM_DEVICE);
}

static void test_stream_and_input_source(void) {
    uint8_t buf[320];
    mrx_message in = {.type = MRX_MSG_STREAM_STATUS}, out;
    in.u.stream_status.max_fps = 60;
    in.u.stream_status.scale_percent = 200;
    in.u.stream_status.bitrate_kbps = 20000;
    in.u.stream_status.width = 3024;
    in.u.stream_status.height = 1964;
    roundtrip(&in, &out, buf, sizeof buf);
    CHECK(out.u.stream_status.scale_percent == 200 && out.u.stream_status.height == 1964);

    mrx_message c = {.type = MRX_MSG_STREAM_CONFIG};
    c.u.stream_config.max_fps = 30;
    c.u.stream_config.bitrate_kbps = 1500;
    roundtrip(&c, &out, buf, sizeof buf);
    CHECK(out.u.stream_config.max_fps == 30 && out.u.stream_config.bitrate_kbps == 1500);

    mrx_message i = {.type = MRX_MSG_INPUT_SOURCE};
    i.u.input_source.ascii_capable = 0;
    i.u.input_source.id = "com.apple.inputmethod.Kotoeri.RomajiTyping.Japanese";
    i.u.input_source.id_length = (uint8_t)strlen(i.u.input_source.id);
    roundtrip(&i, &out, buf, sizeof buf);
    CHECK(out.u.input_source.id_length == i.u.input_source.id_length);
    CHECK(memcmp(out.u.input_source.id, i.u.input_source.id, i.u.input_source.id_length) == 0);
}

static void test_stream_framing(void) {
    uint8_t buf[256];
    size_t used = 0, n;
    mrx_message a = {.type = MRX_MSG_MODIFIERS}, b = {.type = MRX_MSG_KEYBOARD_INFO}, out;
    a.u.modifiers.modifier_flags = MRX_FLAG_SHIFT;
    b.u.keyboard_info.keyboard_type = MRX_KEYBOARD_TYPE_JIS;
    CHECK(mrx_encode(&a, buf, sizeof buf, &n) == MRX_OK);
    used += n;
    /* An unknown type in between, with a payload. */
    uint8_t unknown[] = {0x34, 0x12, 0, 0, 3, 0, 0, 0, 'x', 'y', 'z'};
    memcpy(buf + used, unknown, sizeof unknown);
    used += sizeof unknown;
    CHECK(mrx_encode(&b, buf + used, sizeof buf - used, &n) == MRX_OK);
    used += n;

    size_t off = 0, c;
    CHECK(mrx_decode(buf, used, &out, &c) == MRX_OK && out.type == MRX_MSG_MODIFIERS);
    off += c;
    CHECK(mrx_decode(buf + off, used - off, &out, &c) == MRX_ERROR_UNKNOWN_TYPE && c == sizeof unknown);
    off += c;
    CHECK(mrx_decode(buf + off, used - off, &out, &c) == MRX_OK && out.u.keyboard_info.keyboard_type == 42);
    off += c;
    CHECK(off == used);

    /* Truncation asks for more; every prefix of a message does. */
    for (size_t len = 0; len < n; len++)
        CHECK(mrx_decode(buf + used - n, len, &out, &c) == MRX_NEED_MORE);

    /* Oversized length is refused before waiting for it. */
    uint8_t huge[] = {0x11, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0x00};
    CHECK(mrx_decode(huge, sizeof huge, &out, &c) == MRX_ERROR_TOO_LARGE);

    /* Short payload for a known type is malformed. */
    uint8_t shortkey[] = {0x11, 0, 0, 0, 2, 0, 0, 0, 13, 0};
    CHECK(mrx_decode(shortkey, sizeof shortkey, &out, &c) == MRX_ERROR_MALFORMED);

    /* Longer payloads (future fields) are accepted. */
    uint8_t longmods[MRX_HEADER_BYTES + 12] = {0x12, 0, 0, 0, 12, 0, 0, 0, 0, 0, 2, 0};
    CHECK(mrx_decode(longmods, sizeof longmods, &out, &c) == MRX_OK && c == sizeof longmods);
    CHECK(out.u.modifiers.modifier_flags == MRX_FLAG_SHIFT);

    /* A HELLO whose name runs past its payload is malformed. */
    uint8_t badhello[] = {1, 0, 0, 0, 8, 0, 0, 0, 1, 0, 1, 5, 0, 0, 0, 0};
    CHECK(mrx_decode(badhello, sizeof badhello, &out, &c) == MRX_ERROR_MALFORMED);

    size_t need = 0;
    CHECK(mrx_encode(&a, buf, 4, &need) == MRX_ERROR_BUFFER_TOO_SMALL && need == MRX_HEADER_BYTES + 8);
}

static void test_negotiate(void) {
    CHECK(mrx_negotiate(MRX_CAP_MAC_KEYS | MRX_CAP_POINTER | MRX_CAP_PRECISE_SCROLL,
                        MRX_CAP_MAC_KEYS | MRX_CAP_POINTER) == (MRX_CAP_MAC_KEYS | MRX_CAP_POINTER));
    CHECK(mrx_negotiate(MRX_CAP_MAC_KEYS, MRX_CAP_MAC_KEYS | MRX_CAP_POINTER) == 0);
}

static void test_keys(void) {
    CHECK(mrx_keycode_for_hid_usage(0x04) == 0);    /* A */
    CHECK(mrx_keycode_for_hid_usage(0x1A) == 13);   /* W */
    CHECK(mrx_keycode_for_hid_usage(0x90) == 104);  /* Kana */
    CHECK(mrx_keycode_for_hid_usage(0x91) == 102);  /* Eisu */
    CHECK(mrx_keycode_for_hid_usage(0x89) == 93);   /* Yen */
    CHECK(mrx_keycode_for_hid_usage(0x8B) == 102);  /* 無変換 -> Eisu */
    CHECK(mrx_keycode_for_hid_usage(0x8A) == 104);  /* 変換 -> Kana */
    CHECK(mrx_keycode_for_hid_usage(0xE3) == 55);   /* left Command */
    CHECK(mrx_keycode_for_hid_usage(0xE7) == 54);   /* right Command */
    CHECK(mrx_keycode_for_hid_usage(0x00) == MRX_KEYCODE_NONE);
    CHECK(mrx_keycode_for_hid_usage(0xFF) == MRX_KEYCODE_NONE);
    for (uint16_t kc = 0; kc <= MRX_MAX_KEYCODE; kc++) {
        uint16_t u = mrx_hid_usage_for_keycode(kc);
        if (u) CHECK(mrx_keycode_for_hid_usage(u) == kc);
    }
    CHECK(mrx_hid_usage_for_keycode(63) == 0);     /* Fn */
    CHECK(mrx_keycode_is_modifier(63));
    CHECK(mrx_keycode_is_modifier(57));
    CHECK(!mrx_keycode_is_modifier(0));

    mrx_modifier_tracker t;
    mrx_modifier_tracker_reset(&t);
    CHECK(mrx_modifier_tracker_update(&t, 55, MRX_KEY_DOWN) == (MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_LEFT_COMMAND));
    CHECK(mrx_modifier_tracker_update(&t, 54, MRX_KEY_DOWN) ==
          (MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_LEFT_COMMAND | MRX_FLAG_DEVICE_RIGHT_COMMAND));
    CHECK(mrx_modifier_tracker_update(&t, 55, MRX_KEY_UP) == (MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_RIGHT_COMMAND));
    CHECK(mrx_modifier_tracker_update(&t, 54, MRX_KEY_UP) == 0);
    CHECK(mrx_modifier_tracker_update(&t, 57, MRX_KEY_DOWN) == MRX_FLAG_CAPS_LOCK);
    CHECK(mrx_modifier_tracker_update(&t, 57, MRX_KEY_UP) == MRX_FLAG_CAPS_LOCK);
    CHECK(mrx_modifier_tracker_update(&t, 57, MRX_KEY_DOWN) == 0);
    CHECK(mrx_modifier_tracker_update(&t, 0, MRX_KEY_DOWN) == 0);
}

static void test_gesture(void) {
    uint8_t buf[64];
    mrx_message in = {.type = MRX_MSG_GESTURE}, out;
    in.u.gesture.kind = MRX_GESTURE_DOCK_VERTICAL;
    in.u.gesture.phase = MRX_PHASE_ENDED;
    in.u.gesture.value = 0.75f;
    in.u.gesture.velocity = -2.5f;
    in.u.gesture.modifier_flags = MRX_FLAG_OPTION | (1ull << 40);
    roundtrip(&in, &out, buf, sizeof buf);
    CHECK(out.u.gesture.kind == MRX_GESTURE_DOCK_VERTICAL);
    CHECK(out.u.gesture.phase == MRX_PHASE_ENDED);
    CHECK(out.u.gesture.value == 0.75f && out.u.gesture.velocity == -2.5f);
    CHECK(out.u.gesture.modifier_flags == MRX_FLAG_OPTION);

    size_t written, consumed;
    in.u.gesture.kind = MRX_GESTURE_MAX + 1;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_ERROR_INVALID_ARGUMENT);
    in.u.gesture.kind = MRX_GESTURE_MAGNIFY;
    in.u.gesture.value = NAN;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_ERROR_INVALID_ARGUMENT);

    /* An unknown kind from a later version is malformed, not misread. */
    in.u.gesture.value = 0.1f;
    CHECK(mrx_encode(&in, buf, sizeof buf, &written) == MRX_OK);
    buf[MRX_HEADER_BYTES] = 200;
    CHECK(mrx_decode(buf, written, &out, &consumed) == MRX_ERROR_MALFORMED);
    CHECK(strcmp(mrx_message_type_name(MRX_MSG_GESTURE), "gesture") == 0);
}

int main(void) {
    test_hello();
    test_key_and_flags();
    test_pointer_and_scroll();
    test_gesture();
    test_stream_and_input_source();
    test_stream_framing();
    test_negotiate();
    test_keys();
    if (failures) {
        fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    puts("macrdpx: all tests passed");
    return 0;
}
