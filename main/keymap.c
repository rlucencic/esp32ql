/*
 * The board's keyboards (Bluetooth, USB, the Tab5 Keyboard: HID usages) to
 * the QL's keys, by position: letters, digits and punctuation where a PC
 * keyboard has them (the key left of 1, ` ~, is the QL's £ key), Shift,
 * Ctrl and Alt as the QL's, F1-F5, the cursor keys, Esc, Tab, Caps Lock,
 * Enter, Space. Backspace is CTRL + left (delete the character to the
 * left), Delete CTRL + right (the one under the cursor), as on the QL.
 */
#include <string.h>
#include "ql.h"
#include "keymap.h"

#define NONE    0xFF

typedef struct {
    uint8_t key, with;                  /* with: NONE or a modifier held with it */
} keymap_t;

#define K(k)        { (k), NONE }
#define CTRL(k)     { (k), QL_KEY_CTRL }

static const keymap_t usage_map[0x66] = {
    [0x04] = K(QL_KEY_A), [0x05] = K(QL_KEY_B), [0x06] = K(QL_KEY_C), [0x07] = K(QL_KEY_D),
    [0x08] = K(QL_KEY_E), [0x09] = K(QL_KEY_F), [0x0A] = K(QL_KEY_G), [0x0B] = K(QL_KEY_H),
    [0x0C] = K(QL_KEY_I), [0x0D] = K(QL_KEY_J), [0x0E] = K(QL_KEY_K), [0x0F] = K(QL_KEY_L),
    [0x10] = K(QL_KEY_M), [0x11] = K(QL_KEY_N), [0x12] = K(QL_KEY_O), [0x13] = K(QL_KEY_P),
    [0x14] = K(QL_KEY_Q), [0x15] = K(QL_KEY_R), [0x16] = K(QL_KEY_S), [0x17] = K(QL_KEY_T),
    [0x18] = K(QL_KEY_U), [0x19] = K(QL_KEY_V), [0x1A] = K(QL_KEY_W), [0x1B] = K(QL_KEY_X),
    [0x1C] = K(QL_KEY_Y), [0x1D] = K(QL_KEY_Z),
    [0x1E] = K(QL_KEY_1), [0x1F] = K(QL_KEY_2), [0x20] = K(QL_KEY_3), [0x21] = K(QL_KEY_4),
    [0x22] = K(QL_KEY_5), [0x23] = K(QL_KEY_6), [0x24] = K(QL_KEY_7), [0x25] = K(QL_KEY_8),
    [0x26] = K(QL_KEY_9), [0x27] = K(QL_KEY_0),
    [0x28] = K(QL_KEY_ENTER),
    [0x29] = K(QL_KEY_ESC),
    [0x2A] = CTRL(QL_KEY_LEFT),         /* Backspace */
    [0x2B] = K(QL_KEY_TAB),
    [0x2C] = K(QL_KEY_SPACE),
    [0x2D] = K(QL_KEY_MINUS),
    [0x2E] = K(QL_KEY_EQUALS),
    [0x2F] = K(QL_KEY_LBRACKET),
    [0x30] = K(QL_KEY_RBRACKET),
    [0x31] = K(QL_KEY_BACKSLASH),
    [0x32] = K(QL_KEY_BACKSLASH),       /* non-US # */
    [0x33] = K(QL_KEY_SEMICOLON),
    [0x34] = K(QL_KEY_QUOTE),
    [0x35] = K(QL_KEY_POUND),           /* ` ~ */
    [0x36] = K(QL_KEY_COMMA),
    [0x37] = K(QL_KEY_PERIOD),
    [0x38] = K(QL_KEY_SLASH),
    [0x39] = K(QL_KEY_CAPSLOCK),
    [0x3A] = K(QL_KEY_F1), [0x3B] = K(QL_KEY_F2), [0x3C] = K(QL_KEY_F3), [0x3D] = K(QL_KEY_F4),
    [0x3E] = K(QL_KEY_F5),
    [0x4C] = CTRL(QL_KEY_RIGHT),        /* Delete */
    [0x4F] = K(QL_KEY_RIGHT), [0x50] = K(QL_KEY_LEFT), [0x51] = K(QL_KEY_DOWN), [0x52] = K(QL_KEY_UP),
    [0x58] = K(QL_KEY_ENTER),           /* keypad Enter */
    [0x64] = K(QL_KEY_BACKSLASH),       /* non-US \ (ISO key left of Z) */
};

static uint8_t s_count[QL_KEYS];        /* presses holding each QL key */
static keymap_t s_held[256];            /* per HID usage: what it pressed */

static void key_ref(uint8_t key, bool down)
{
    if (key == NONE) {
        return;
    }
    bool was = s_count[key] > 0;
    s_count[key] = down ? s_count[key] + 1 : s_count[key] ? s_count[key] - 1 : 0;
    bool now = s_count[key] > 0;
    if (was != now) {
        ql_key(key, now);
    }
}

void keymap_hid_key(uint8_t usage, bool pressed)
{
    if (usage >= 0xE0 && usage <= 0xE7) {
        /* LCtrl LShift LAlt LGUI RCtrl RShift RAlt RGUI */
        static const uint8_t keys[8] = {
            QL_KEY_CTRL, QL_KEY_SHIFT, QL_KEY_ALT, NONE, QL_KEY_CTRL, QL_KEY_SHIFT, QL_KEY_ALT, NONE,
        };
        key_ref(keys[usage - 0xE0], pressed);
        return;
    }
    if (pressed) {
        if (usage >= sizeof(usage_map) / sizeof(usage_map[0])) {
            return;
        }
        keymap_t m = usage_map[usage];
        if (m.key == 0 && m.with == 0) {
            return;                     /* not a QL key */
        }
        s_held[usage] = m;
        key_ref(m.with, true);          /* the modifier first: the IPC reports it with the key */
        key_ref(m.key, true);
    } else {
        keymap_t m = s_held[usage];
        if (m.key == 0 && m.with == 0) {
            return;
        }
        key_ref(m.key, false);
        key_ref(m.with, false);
        memset(&s_held[usage], 0, sizeof(s_held[usage]));
    }
}
