#include "macrdpx/macrdpx.h"

/* USB HID keyboard usage (page 7) for each kVK_* code, by physical position;
 * zero where the key has no usage. kVK values are HIToolbox/Events.h. */
static const uint16_t kUsageForKeycode[MRX_MAX_KEYCODE + 1] = {
    /* Letters. */
    [0] = 0x04, [11] = 0x05, [8] = 0x06, [2] = 0x07, [14] = 0x08, [3] = 0x09, [5] = 0x0A,
    [4] = 0x0B, [34] = 0x0C, [38] = 0x0D, [40] = 0x0E, [37] = 0x0F, [46] = 0x10, [45] = 0x11,
    [31] = 0x12, [35] = 0x13, [12] = 0x14, [15] = 0x15, [1] = 0x16, [17] = 0x17, [32] = 0x18,
    [9] = 0x19, [13] = 0x1A, [7] = 0x1B, [16] = 0x1C, [6] = 0x1D,
    /* Digits 1-9, 0. */
    [18] = 0x1E, [19] = 0x1F, [20] = 0x20, [21] = 0x21, [23] = 0x22, [22] = 0x23, [26] = 0x24,
    [28] = 0x25, [25] = 0x26, [29] = 0x27,
    /* Return, Escape, Delete (backspace), Tab, Space. */
    [36] = 0x28, [53] = 0x29, [51] = 0x2A, [48] = 0x2B, [49] = 0x2C,
    /* - = [ ] \ ; ' ` , . / */
    [27] = 0x2D, [24] = 0x2E, [33] = 0x2F, [30] = 0x30, [42] = 0x31, [41] = 0x33, [39] = 0x34,
    [50] = 0x35, [43] = 0x36, [47] = 0x37, [44] = 0x38,
    /* ISO section key. */
    [10] = 0x64,
    /* Caps Lock. */
    [57] = 0x39,
    /* F1-F12. */
    [122] = 0x3A, [120] = 0x3B, [99] = 0x3C, [118] = 0x3D, [96] = 0x3E, [97] = 0x3F,
    [98] = 0x40, [100] = 0x41, [101] = 0x42, [109] = 0x43, [103] = 0x44, [111] = 0x45,
    /* F13-F20. */
    [105] = 0x68, [107] = 0x69, [113] = 0x6A, [106] = 0x6B, [64] = 0x6C, [79] = 0x6D,
    [80] = 0x6E, [90] = 0x6F,
    /* Help/Insert, Home, Page Up, Forward Delete, End, Page Down, PC menu key. */
    [114] = 0x49, [115] = 0x4A, [116] = 0x4B, [117] = 0x4C, [119] = 0x4D, [121] = 0x4E,
    [110] = 0x65,
    /* Arrows: right, left, down, up. */
    [124] = 0x4F, [123] = 0x50, [125] = 0x51, [126] = 0x52,
    /* Keypad: Clear, / * - + Enter, 1-9, 0, ., =. */
    [71] = 0x53, [75] = 0x54, [67] = 0x55, [78] = 0x56, [69] = 0x57, [76] = 0x58,
    [83] = 0x59, [84] = 0x5A, [85] = 0x5B, [86] = 0x5C, [87] = 0x5D, [88] = 0x5E,
    [89] = 0x5F, [91] = 0x60, [92] = 0x61, [82] = 0x62, [65] = 0x63, [81] = 0x67,
    /* JIS: Ro (Intl1), Yen (Intl3), keypad comma, Kana (LANG1), Eisu (LANG2). */
    [94] = 0x87, [93] = 0x89, [95] = 0x85, [104] = 0x90, [102] = 0x91,
    /* Volume up/down, mute. */
    [72] = 0x80, [73] = 0x81, [74] = 0x7F,
    /* Modifiers: left Control, Shift, Option, Command; right ones. */
    [59] = 0xE0, [56] = 0xE1, [58] = 0xE2, [55] = 0xE3,
    [62] = 0xE4, [60] = 0xE5, [61] = 0xE6, [54] = 0xE7,
};

uint16_t mrx_hid_usage_for_keycode(uint16_t keycode) {
    return keycode <= MRX_MAX_KEYCODE ? kUsageForKeycode[keycode] : 0;
}

uint16_t mrx_keycode_for_hid_usage(uint16_t usage) {
    if (usage == 0) return MRX_KEYCODE_NONE;
    /* Some usages have two Mac keys; prefer the main block's. */
    if (usage == 0x32) return 42; /* Non-US # is the backslash position on ISO. */
    for (uint16_t kc = 0; kc <= MRX_MAX_KEYCODE; kc++)
        if (kUsageForKeycode[kc] == usage) return kc;
    return MRX_KEYCODE_NONE;
}

typedef struct {
    uint16_t keycode;
    uint64_t flags;
} modifier_slot;

/* Slot order is the tracker's bit order. */
static const modifier_slot kModifierSlots[] = {
    {59, MRX_FLAG_CONTROL | MRX_FLAG_DEVICE_LEFT_CONTROL},
    {56, MRX_FLAG_SHIFT | MRX_FLAG_DEVICE_LEFT_SHIFT},
    {58, MRX_FLAG_OPTION | MRX_FLAG_DEVICE_LEFT_OPTION},
    {55, MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_LEFT_COMMAND},
    {62, MRX_FLAG_CONTROL | MRX_FLAG_DEVICE_RIGHT_CONTROL},
    {60, MRX_FLAG_SHIFT | MRX_FLAG_DEVICE_RIGHT_SHIFT},
    {61, MRX_FLAG_OPTION | MRX_FLAG_DEVICE_RIGHT_OPTION},
    {54, MRX_FLAG_COMMAND | MRX_FLAG_DEVICE_RIGHT_COMMAND},
    {63, MRX_FLAG_FUNCTION},
};
#define MODIFIER_SLOT_COUNT (sizeof kModifierSlots / sizeof kModifierSlots[0])
#define KEYCODE_CAPS_LOCK 57

static int modifier_slot_for(uint16_t keycode) {
    for (size_t i = 0; i < MODIFIER_SLOT_COUNT; i++)
        if (kModifierSlots[i].keycode == keycode) return (int)i;
    return -1;
}

uint64_t mrx_modifier_flags_for_keycode(uint16_t keycode) {
    if (keycode == KEYCODE_CAPS_LOCK) return MRX_FLAG_CAPS_LOCK;
    int slot = modifier_slot_for(keycode);
    return slot < 0 ? 0 : kModifierSlots[slot].flags;
}

int mrx_keycode_is_modifier(uint16_t keycode) { return mrx_modifier_flags_for_keycode(keycode) != 0; }

void mrx_modifier_tracker_reset(mrx_modifier_tracker *tracker) {
    if (!tracker) return;
    tracker->held = 0;
    tracker->caps_lock = 0;
}

uint64_t mrx_modifier_tracker_flags(const mrx_modifier_tracker *tracker) {
    if (!tracker) return 0;
    uint64_t flags = tracker->caps_lock ? MRX_FLAG_CAPS_LOCK : 0;
    for (size_t i = 0; i < MODIFIER_SLOT_COUNT; i++)
        if (tracker->held & (1u << i)) flags |= kModifierSlots[i].flags;
    return flags;
}

uint64_t mrx_modifier_tracker_update(mrx_modifier_tracker *tracker, uint16_t keycode, mrx_key_action action) {
    if (!tracker) return 0;
    if (keycode == KEYCODE_CAPS_LOCK) {
        if (action == MRX_KEY_DOWN) tracker->caps_lock = !tracker->caps_lock;
    } else {
        int slot = modifier_slot_for(keycode);
        if (slot >= 0) {
            if (action == MRX_KEY_DOWN)
                tracker->held |= 1u << slot;
            else
                tracker->held &= ~(1u << slot);
        }
    }
    return mrx_modifier_tracker_flags(tracker);
}
