#include "macrdpx/macrdpx.h"

#include <math.h>
#include <string.h>

/* Little-endian field access, independent of host order and alignment. */

static void put_u16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v) {
    for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put_u64(uint8_t *p, uint64_t v) {
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

static void put_f32(uint8_t *p, float f) {
    uint32_t v;
    memcpy(&v, &f, sizeof v);
    put_u32(p, v);
}

static uint16_t get_u16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static uint32_t get_u32(const uint8_t *p) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)p[i] << (8 * i);
    return v;
}

static uint64_t get_u64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static float get_f32(const uint8_t *p) {
    uint32_t v = get_u32(p);
    float f;
    memcpy(&f, &v, sizeof f);
    return f;
}

/* Coordinates and deltas beyond this are nonsense from any real device. */
#define MRX_MAX_ABS_COORD 1.0e6f

static int coord_ok(float v) { return isfinite(v) && fabsf(v) <= MRX_MAX_ABS_COORD; }

/* Payload sizes of version 1. Later versions may append fields. */
enum {
    HELLO_BYTES = 8,
    KEYBOARD_INFO_BYTES = 4,
    KEY_BYTES = 12,
    MODIFIERS_BYTES = 8,
    POINTER_BYTES = 24,
    SCROLL_BYTES = 20,
    GESTURE_BYTES = 20,
    STREAM_CONFIG_BYTES = 8,
    /* With the appended audio_compressed; the shorter one is still read. */
    STREAM_CONFIG_V2_BYTES = 12,
    STREAM_STATUS_BYTES = 16,
    INPUT_SOURCE_BYTES = 2
};

static mrx_status validate(const mrx_message *m) {
    switch (m->type) {
    case MRX_MSG_HELLO:
        if (m->u.hello.role != MRX_ROLE_CLIENT && m->u.hello.role != MRX_ROLE_SERVER) return MRX_ERROR_MALFORMED;
        if (m->u.hello.name_length && !m->u.hello.name) return MRX_ERROR_INVALID_ARGUMENT;
        return MRX_OK;
    case MRX_MSG_KEYBOARD_INFO:
    case MRX_MSG_MODIFIERS:
    case MRX_MSG_STREAM_CONFIG:
    case MRX_MSG_STREAM_STATUS:
        return MRX_OK;
    case MRX_MSG_KEY:
        if (m->u.key.keycode > MRX_MAX_KEYCODE) return MRX_ERROR_MALFORMED;
        if (m->u.key.action > MRX_KEY_UP) return MRX_ERROR_MALFORMED;
        return MRX_OK;
    case MRX_MSG_POINTER:
        if (m->u.pointer.action > MRX_POINTER_UP) return MRX_ERROR_MALFORMED;
        if (m->u.pointer.button > MRX_MAX_BUTTON) return MRX_ERROR_MALFORMED;
        if (!coord_ok(m->u.pointer.x) || !coord_ok(m->u.pointer.y)) return MRX_ERROR_MALFORMED;
        return MRX_OK;
    case MRX_MSG_SCROLL:
        if (m->u.scroll.unit > MRX_SCROLL_LINE) return MRX_ERROR_MALFORMED;
        if (!coord_ok(m->u.scroll.delta_x) || !coord_ok(m->u.scroll.delta_y)) return MRX_ERROR_MALFORMED;
        return MRX_OK;
    case MRX_MSG_GESTURE:
        if (m->u.gesture.kind > MRX_GESTURE_MAX) return MRX_ERROR_MALFORMED;
        if (!isfinite(m->u.gesture.value) || !isfinite(m->u.gesture.velocity)) return MRX_ERROR_MALFORMED;
        return MRX_OK;
    case MRX_MSG_INPUT_SOURCE:
        if (m->u.input_source.id_length && !m->u.input_source.id) return MRX_ERROR_INVALID_ARGUMENT;
        return MRX_OK;
    default:
        return MRX_ERROR_UNKNOWN_TYPE;
    }
}

static size_t payload_size(const mrx_message *m) {
    switch (m->type) {
    case MRX_MSG_HELLO: return HELLO_BYTES + m->u.hello.name_length;
    case MRX_MSG_KEYBOARD_INFO: return KEYBOARD_INFO_BYTES;
    case MRX_MSG_KEY: return KEY_BYTES;
    case MRX_MSG_MODIFIERS: return MODIFIERS_BYTES;
    case MRX_MSG_POINTER: return POINTER_BYTES;
    case MRX_MSG_SCROLL: return SCROLL_BYTES;
    case MRX_MSG_GESTURE: return GESTURE_BYTES;
    case MRX_MSG_STREAM_CONFIG: return STREAM_CONFIG_V2_BYTES;
    case MRX_MSG_STREAM_STATUS: return STREAM_STATUS_BYTES;
    case MRX_MSG_INPUT_SOURCE: return INPUT_SOURCE_BYTES + m->u.input_source.id_length;
    default: return 0;
    }
}

mrx_status mrx_encode(const mrx_message *m, uint8_t *buf, size_t capacity, size_t *written) {
    if (!m || !written || (!buf && capacity)) return MRX_ERROR_INVALID_ARGUMENT;
    *written = 0;
    mrx_status st = validate(m);
    if (st == MRX_ERROR_MALFORMED) return MRX_ERROR_INVALID_ARGUMENT;
    if (st != MRX_OK) return st;

    size_t plen = payload_size(m);
    size_t total = MRX_HEADER_BYTES + plen;
    if (capacity < total) {
        *written = total;
        return MRX_ERROR_BUFFER_TOO_SMALL;
    }

    put_u16(buf, m->type);
    put_u16(buf + 2, 0);
    put_u32(buf + 4, (uint32_t)plen);
    uint8_t *p = buf + MRX_HEADER_BYTES;
    memset(p, 0, plen);

    switch (m->type) {
    case MRX_MSG_HELLO:
        put_u16(p, m->u.hello.version);
        p[2] = m->u.hello.role;
        p[3] = m->u.hello.name_length;
        put_u32(p + 4, m->u.hello.caps);
        if (m->u.hello.name_length) memcpy(p + 8, m->u.hello.name, m->u.hello.name_length);
        break;
    case MRX_MSG_KEYBOARD_INFO:
        put_u32(p, m->u.keyboard_info.keyboard_type);
        break;
    case MRX_MSG_KEY:
        put_u16(p, m->u.key.keycode);
        p[2] = m->u.key.action;
        p[3] = m->u.key.flags & MRX_KEY_FLAG_REPEAT;
        put_u64(p + 4, m->u.key.modifier_flags & MRX_FLAG_MASK);
        break;
    case MRX_MSG_MODIFIERS:
        put_u64(p, m->u.modifiers.modifier_flags & MRX_FLAG_MASK);
        break;
    case MRX_MSG_POINTER:
        p[0] = m->u.pointer.action;
        p[1] = m->u.pointer.button;
        put_u16(p + 2, m->u.pointer.click_count);
        put_u32(p + 4, m->u.pointer.buttons_down);
        put_f32(p + 8, m->u.pointer.x);
        put_f32(p + 12, m->u.pointer.y);
        put_u64(p + 16, m->u.pointer.modifier_flags & MRX_FLAG_MASK);
        break;
    case MRX_MSG_SCROLL:
        put_f32(p, m->u.scroll.delta_x);
        put_f32(p + 4, m->u.scroll.delta_y);
        p[8] = m->u.scroll.unit;
        p[9] = m->u.scroll.phase;
        p[10] = m->u.scroll.momentum_phase;
        p[11] = m->u.scroll.flags & MRX_SCROLL_FLAG_INVERTED_FROM_DEVICE;
        put_u64(p + 12, m->u.scroll.modifier_flags & MRX_FLAG_MASK);
        break;
    case MRX_MSG_GESTURE:
        p[0] = m->u.gesture.kind;
        p[1] = m->u.gesture.phase;
        p[2] = 0;
        p[3] = 0;
        put_f32(p + 4, m->u.gesture.value);
        put_f32(p + 8, m->u.gesture.velocity);
        put_u64(p + 12, m->u.gesture.modifier_flags & MRX_FLAG_MASK);
        break;
    case MRX_MSG_STREAM_CONFIG:
        put_u16(p, m->u.stream_config.max_fps);
        p[2] = 0;
        p[3] = 0;
        put_u32(p + 4, m->u.stream_config.bitrate_kbps);
        p[8] = m->u.stream_config.audio_compressed ? 1 : 0;
        p[9] = 0;
        p[10] = 0;
        p[11] = 0;
        break;
    case MRX_MSG_STREAM_STATUS:
        put_u16(p, m->u.stream_status.max_fps);
        put_u16(p + 2, m->u.stream_status.scale_percent);
        put_u32(p + 4, m->u.stream_status.bitrate_kbps);
        put_u32(p + 8, m->u.stream_status.width);
        put_u32(p + 12, m->u.stream_status.height);
        break;
    case MRX_MSG_INPUT_SOURCE:
        p[0] = m->u.input_source.ascii_capable ? 1 : 0;
        p[1] = m->u.input_source.id_length;
        if (m->u.input_source.id_length) memcpy(p + 2, m->u.input_source.id, m->u.input_source.id_length);
        break;
    default:
        return MRX_ERROR_UNKNOWN_TYPE;
    }
    *written = total;
    return MRX_OK;
}

static size_t min_payload(mrx_message_type type) {
    /* A sender from before audio_compressed writes 8 bytes. */
    if (type == MRX_MSG_STREAM_CONFIG) return STREAM_CONFIG_BYTES;
    mrx_message probe;
    memset(&probe, 0, sizeof probe);
    probe.type = type;
    return payload_size(&probe);
}

mrx_status mrx_decode(const uint8_t *buf, size_t length, mrx_message *m, size_t *consumed) {
    if (!m || !consumed || (!buf && length)) return MRX_ERROR_INVALID_ARGUMENT;
    *consumed = 0;
    if (length < MRX_HEADER_BYTES) return MRX_NEED_MORE;

    mrx_message_type type = get_u16(buf);
    uint32_t plen = get_u32(buf + 4);
    if (plen > MRX_MAX_PAYLOAD_BYTES) return MRX_ERROR_TOO_LARGE;
    if (length - MRX_HEADER_BYTES < plen) return MRX_NEED_MORE;

    const uint8_t *p = buf + MRX_HEADER_BYTES;
    size_t total = MRX_HEADER_BYTES + plen;
    memset(m, 0, sizeof *m);
    m->type = type;

    switch (type) {
    case MRX_MSG_HELLO:
    case MRX_MSG_KEYBOARD_INFO:
    case MRX_MSG_KEY:
    case MRX_MSG_MODIFIERS:
    case MRX_MSG_POINTER:
    case MRX_MSG_SCROLL:
    case MRX_MSG_GESTURE:
    case MRX_MSG_STREAM_CONFIG:
    case MRX_MSG_STREAM_STATUS:
    case MRX_MSG_INPUT_SOURCE:
        if (plen < min_payload(type)) return MRX_ERROR_MALFORMED;
        break;
    default:
        *consumed = total;
        return MRX_ERROR_UNKNOWN_TYPE;
    }

    switch (type) {
    case MRX_MSG_HELLO:
        m->u.hello.version = get_u16(p);
        m->u.hello.role = p[2];
        m->u.hello.name_length = p[3];
        m->u.hello.caps = get_u32(p + 4);
        if ((size_t)HELLO_BYTES + m->u.hello.name_length > plen) return MRX_ERROR_MALFORMED;
        m->u.hello.name = m->u.hello.name_length ? (const char *)(p + HELLO_BYTES) : NULL;
        break;
    case MRX_MSG_KEYBOARD_INFO:
        m->u.keyboard_info.keyboard_type = get_u32(p);
        break;
    case MRX_MSG_KEY:
        m->u.key.keycode = get_u16(p);
        m->u.key.action = p[2];
        m->u.key.flags = p[3] & MRX_KEY_FLAG_REPEAT;
        m->u.key.modifier_flags = get_u64(p + 4) & MRX_FLAG_MASK;
        break;
    case MRX_MSG_MODIFIERS:
        m->u.modifiers.modifier_flags = get_u64(p) & MRX_FLAG_MASK;
        break;
    case MRX_MSG_POINTER:
        m->u.pointer.action = p[0];
        m->u.pointer.button = p[1];
        m->u.pointer.click_count = get_u16(p + 2);
        m->u.pointer.buttons_down = get_u32(p + 4);
        m->u.pointer.x = get_f32(p + 8);
        m->u.pointer.y = get_f32(p + 12);
        m->u.pointer.modifier_flags = get_u64(p + 16) & MRX_FLAG_MASK;
        break;
    case MRX_MSG_SCROLL:
        m->u.scroll.delta_x = get_f32(p);
        m->u.scroll.delta_y = get_f32(p + 4);
        m->u.scroll.unit = p[8];
        m->u.scroll.phase = p[9];
        m->u.scroll.momentum_phase = p[10];
        m->u.scroll.flags = p[11] & MRX_SCROLL_FLAG_INVERTED_FROM_DEVICE;
        m->u.scroll.modifier_flags = get_u64(p + 12) & MRX_FLAG_MASK;
        break;
    case MRX_MSG_GESTURE:
        m->u.gesture.kind = p[0];
        m->u.gesture.phase = p[1];
        m->u.gesture.value = get_f32(p + 4);
        m->u.gesture.velocity = get_f32(p + 8);
        m->u.gesture.modifier_flags = get_u64(p + 12) & MRX_FLAG_MASK;
        if (m->u.gesture.kind > MRX_GESTURE_MAX || !isfinite(m->u.gesture.value) ||
            !isfinite(m->u.gesture.velocity)) {
            return MRX_ERROR_MALFORMED;
        }
        break;
    case MRX_MSG_STREAM_CONFIG:
        m->u.stream_config.max_fps = get_u16(p);
        m->u.stream_config.bitrate_kbps = get_u32(p + 4);
        m->u.stream_config.audio_compressed = plen >= STREAM_CONFIG_V2_BYTES ? (uint8_t)(p[8] & 1) : 0;
        break;
    case MRX_MSG_STREAM_STATUS:
        m->u.stream_status.max_fps = get_u16(p);
        m->u.stream_status.scale_percent = get_u16(p + 2);
        m->u.stream_status.bitrate_kbps = get_u32(p + 4);
        m->u.stream_status.width = get_u32(p + 8);
        m->u.stream_status.height = get_u32(p + 12);
        break;
    case MRX_MSG_INPUT_SOURCE:
        m->u.input_source.ascii_capable = p[0] ? 1 : 0;
        m->u.input_source.id_length = p[1];
        if ((size_t)INPUT_SOURCE_BYTES + m->u.input_source.id_length > plen) return MRX_ERROR_MALFORMED;
        m->u.input_source.id = m->u.input_source.id_length ? (const char *)(p + INPUT_SOURCE_BYTES) : NULL;
        break;
    default:
        break;
    }

    if (validate(m) != MRX_OK) return MRX_ERROR_MALFORMED;
    *consumed = total;
    return MRX_OK;
}

mrx_caps mrx_negotiate(mrx_caps client, mrx_caps server) {
    mrx_caps both = client & server;
    /* Keys without the pointer on the same channel could overtake a click. */
    if ((both & MRX_CAP_MAC_KEYS) && !(both & MRX_CAP_POINTER)) both &= ~(mrx_caps)MRX_CAP_MAC_KEYS;
    return both;
}

const char *mrx_status_name(mrx_status status) {
    switch (status) {
    case MRX_OK: return "ok";
    case MRX_NEED_MORE: return "need-more";
    case MRX_ERROR_INVALID_ARGUMENT: return "invalid-argument";
    case MRX_ERROR_BUFFER_TOO_SMALL: return "buffer-too-small";
    case MRX_ERROR_MALFORMED: return "malformed";
    case MRX_ERROR_TOO_LARGE: return "too-large";
    case MRX_ERROR_UNKNOWN_TYPE: return "unknown-type";
    default: return "unknown-status";
    }
}

const char *mrx_message_type_name(mrx_message_type type) {
    switch (type) {
    case MRX_MSG_HELLO: return "hello";
    case MRX_MSG_KEYBOARD_INFO: return "keyboard-info";
    case MRX_MSG_KEY: return "key";
    case MRX_MSG_MODIFIERS: return "modifiers";
    case MRX_MSG_POINTER: return "pointer";
    case MRX_MSG_SCROLL: return "scroll";
    case MRX_MSG_GESTURE: return "gesture";
    case MRX_MSG_STREAM_CONFIG: return "stream-config";
    case MRX_MSG_STREAM_STATUS: return "stream-status";
    case MRX_MSG_INPUT_SOURCE: return "input-source";
    default: return "unknown";
    }
}
