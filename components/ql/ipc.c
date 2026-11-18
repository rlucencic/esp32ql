/*
 * The IPC (an 8049: keyboard, sound, serial input) at the protocol level,
 * as the 68008 sees it through the ZX8302's link (from the JS ROM's own
 * routines, $2F6E-$2FAC):
 *
 *   A bit to the IPC: a write to $18003, $0C for a 0, $0E for a 1, then
 *   wait while $18020 bit 6 is set (we take bits at once: never set).
 *   A bit from the IPC: write $0E, wait as above, read $18020 bit 7.
 *   Commands are a nibble, most significant bit first; parameters follow
 *   (their lengths are the IPC's to know: table below), then the reply,
 *   read bit by bit as long as the 68008 asks. Other values written to
 *   $18003 (the ROM's $01 at reset) start the link over.
 *
 * Commands (QDOS's use in the JS ROM):
 *   1  status: 8 bits; bit 0 keys to read (or one held), bit 1 sound,
 *      bit 4/5 serial 1/2 input waiting
 *   8  keyboard: a nibble (bits 0-2 how many keys follow, bit 3 a key is
 *      still held: QDOS repeats it), then per key a nibble of modifiers
 *      (bit 0 ALT, 1 CTRL, 2 SHIFT) and a byte, its code (ql.h's QL_KEY_*)
 *   9  keyrow: a nibble (the row as KEYROW numbers it), 8 bits back
 *   2, 3  open SER1, SER2 for receiving; 4, 5 close them
 *   6, 7  serial 1, 2 input: a count (bits 5-0), then the bytes (ser.c)
 *   10 sound (64 bits of parameters: eight bytes, beep.c), 11 sound off
 */
#include <string.h>
#include "ql_internal.h"

#define FIFO            8               /* keys waiting to be read */
#define MAX_REPLY       200             /* bits: RDS's 8 + 23 x 8 the longest */
#define TYPE_FRAMES     3               /* a typed key every 3 frames */

static enum { LINK_COMMAND, LINK_PARAMS, LINK_REPLY } s_state;
static uint8_t s_bits, s_command;
static uint8_t s_params[8];
static int s_param_bits, s_params_needed;
static uint8_t s_reply[MAX_REPLY];
static int s_reply_len, s_reply_pos;
static uint8_t s_out;                   /* the bit $18020 bit 7 shows */

/* The keyboard */
static uint8_t s_matrix[8];             /* by code row: bit n of row r is code r * 8 + n */
static struct { uint8_t mods, code; } s_fifo[FIFO];
static int s_fifo_n;
static int s_held;                      /* keys (not modifiers) down */

/* Key events from other tasks: code | $80 for down */
static uint8_t s_events[64];
static volatile uint32_t s_ev_head, s_ev_tail;

/* Typed text */
static char s_type[256];
static volatile int s_type_pos, s_type_len;
static int s_type_wait;

/* The parameter bits each command takes */
static int params_for(uint8_t command)
{
    switch (command) {
    case 9:  return 4;                  /* keyrow: the row */
    case 10: return 64;                 /* sound */
    case 13: return 4;                  /* baud rate */
    case 15: return 8;                  /* test: echoed */
    default: return 0;
    }
}

static void reply_bits(uint32_t value, int bits)
{
    for (int i = bits - 1; i >= 0 && s_reply_len < MAX_REPLY; i--) {
        s_reply[s_reply_len++] = (value >> i) & 1;
    }
}

static uint8_t modifiers(void)
{
    uint8_t row = s_matrix[0];
    return ((row >> QL_KEY_ALT) & 1) | (((row >> QL_KEY_CTRL) & 1) << 1) | (((row >> QL_KEY_SHIFT) & 1) << 2);
}

bool ql_debug_keys;                     /* log the keys QDOS gets, and those lost */

static void push_key(uint8_t mods, uint8_t code)
{
    if (s_fifo_n < FIFO) {
        s_fifo[s_fifo_n].mods = mods;
        s_fifo[s_fifo_n].code = code;
        s_fifo_n++;
    } else if (ql_debug_keys) {
        QL_LOGI("IPC: key %d lost (%d waiting)", code, s_fifo_n);
    }
}

static void execute(void)
{
    s_reply_len = s_reply_pos = 0;
    switch (s_command) {
    case 1: {                           /* status */
        uint8_t status = (s_fifo_n > 0 || s_held > 0 ? 0x01 : 0) | (ql_beep_on() ? 0x02 : 0) |
                         (ql_ser_waiting(0) ? 0x10 : 0) | (ql_ser_waiting(1) ? 0x20 : 0);
        reply_bits(status, 8);
        break;
    }
    case 2: case 3:                     /* open SER1, SER2 for receiving */
        ql_ser_open(s_command - 2, true);
        break;
    case 4: case 5:                     /* close it */
        ql_ser_open(s_command - 4, false);
        break;
    case 6: case 7: {                   /* RDS1, RDS2: how many (no framing errors), the bytes */
        uint8_t buf[32];
        int n = ql_ser_receive(s_command - 6, buf);
        reply_bits(n, 8);
        for (int i = 0; i < n; i++) {
            reply_bits(buf[i], 8);
        }
        break;
    }
    case 8: {                           /* keyboard */
        int n = s_fifo_n > 7 ? 7 : s_fifo_n;
        reply_bits(n | (s_held > 0 ? 8 : 0), 4);
        for (int i = 0; i < n; i++) {
            reply_bits(s_fifo[i].mods, 4);
            reply_bits(s_fifo[i].code, 8);
            if (ql_debug_keys) {
                uint8_t m = s_fifo[i].mods;
                QL_LOGI("IPC: key %d%s%s%s to QDOS%s", s_fifo[i].code, m & 4 ? " SHIFT" : "", m & 2 ? " CTRL" : "",
                        m & 1 ? " ALT" : "", s_held > 0 && i == n - 1 ? " (a key held)" : "");
            }
        }
        memmove(s_fifo, s_fifo + n, (s_fifo_n - n) * sizeof(s_fifo[0]));
        s_fifo_n -= n;
        break;
    }
    case 9: {                           /* keyrow */
        int row = s_params[0] >> 4;
        reply_bits(s_matrix[(7 - row) & 7], 8);
        break;
    }
    case 10:                            /* sound */
        ql_beep_start(s_params);
        break;
    case 11:
        ql_beep_stop();
        break;
    case 15:                            /* test: the byte back */
        reply_bits(s_params[0], 8);
        break;
    default:
        break;
    }
    s_state = s_reply_len ? LINK_REPLY : LINK_COMMAND;
    s_bits = 0;
}

void ql_ipc_reset(void)
{
    s_state = LINK_COMMAND;
    s_bits = s_command = 0;
    s_reply_len = s_reply_pos = 0;
    s_out = 0;
    s_fifo_n = 0;
    ql_beep_reset();
}

/* A write to $18003 */
void ql_ipc_write(uint8_t v)
{
    if ((v & 0x0C) != 0x0C) {
        s_state = LINK_COMMAND;
        s_bits = 0;
        return;
    }
    int bit = (v >> 1) & 1;
    if (s_state == LINK_REPLY) {
        if (bit) {                      /* the 68008 asks for the next bit */
            s_out = s_reply[s_reply_pos++];
            if (s_reply_pos >= s_reply_len) {
                s_state = LINK_COMMAND;
                s_bits = 0;
            }
            return;
        }
        s_state = LINK_COMMAND;         /* a 0 can only start a new command */
        s_bits = 0;
    }
    if (s_state == LINK_COMMAND) {
        s_command = (s_command << 1) | bit;
        if (++s_bits == 4) {
            s_command &= 0x0F;
            s_params_needed = params_for(s_command);
            s_param_bits = 0;
            memset(s_params, 0, sizeof(s_params));
            s_bits = 0;
            if (s_params_needed) {
                s_state = LINK_PARAMS;
            } else {
                execute();
            }
        }
        return;
    }
    /* parameters, most significant bit first */
    if (bit) {
        s_params[s_param_bits >> 3] |= 0x80 >> (s_param_bits & 7);
    }
    if (++s_param_bits == s_params_needed) {
        execute();
    }
}

/* $18020 bit 7 */
uint8_t ql_ipc_bit(void)
{
    return s_out;
}

/* ---- Keys from other tasks ---- */

void ql_key(int key, bool down)
{
    if (key < 0 || key >= QL_KEYS) {
        return;
    }
    uint32_t head = s_ev_head;
    if (head - s_ev_tail < sizeof(s_events)) {
        s_events[head % sizeof(s_events)] = key | (down ? 0x80 : 0);
        s_ev_head = head + 1;
    }
}

/* Characters typed: the key that makes each, and whether with SHIFT (the
 * JS ROM's tables)
 */
static const char plain_chars[QL_KEYS] = {
    0, 0, 0, 'x', 'v', '/', 'n', ',', '8', '2', '6', 'q', 'e', '0', 't', 'u',
    '9', 'w', 'i', '\t', 'r', '-', 'y', 'o', 'l', '3', 'h', '1', 'a', 'p', 'd', 'j',
    '[', 0, 'k', 's', 'f', '=', 'g', ';', ']', 'z', '.', 'c', 'b', '`', 'm', '\'',
    '\n', 0, 0, 0x1B, 0, '\\', ' ', 0, 0, 0, '5', 0, 0, 0, '4', '7',
};
static const char shifted_chars[QL_KEYS] = {
    0, 0, 0, 'X', 'V', '?', 'N', '<', '*', '@', '^', 'Q', 'E', ')', 'T', 'U',
    '(', 'W', 'I', 0, 'R', '_', 'Y', 'O', 'L', '#', 'H', '!', 'A', 'P', 'D', 'J',
    '{', 0, 'K', 'S', 'F', '+', 'G', ':', '}', 'Z', '>', 'C', 'B', '~', 'M', '"',
    0, 0, 0, 0, 0, '|', 0, 0, 0, 0, '%', 0, 0, 0, '$', '&',
};

bool ql_type(const char *text)
{
    if (s_type_pos < s_type_len) {
        return false;                   /* still typing the last one */
    }
    size_t n = strlen(text);
    if (n >= sizeof(s_type)) {
        n = sizeof(s_type) - 1;
    }
    s_type_len = 0;
    memcpy(s_type, text, n);
    s_type_pos = 0;
    s_type_len = n;
    return true;
}

/* Once a frame, in the emulation task */
void ql_ipc_frame(void)
{
    while (s_ev_tail != s_ev_head) {
        uint8_t e = s_events[s_ev_tail % sizeof(s_events)];
        s_ev_tail++;
        int key = e & 0x3F, row = key >> 3, bit = 1 << (key & 7);
        bool down = e & 0x80, was = s_matrix[row] & bit;
        if (down == (was != 0)) {
            continue;
        }
        s_matrix[row] = down ? s_matrix[row] | bit : s_matrix[row] & ~bit;
        if (key <= QL_KEY_ALT) {
            continue;                   /* modifiers go with the keys */
        }
        s_held += down ? 1 : -1;
        if (down) {
            push_key(modifiers(), key);
        }
    }
    if (s_type_pos < s_type_len && --s_type_wait <= 0 && s_fifo_n < FIFO) {
        char c = s_type[s_type_pos++];
        for (int k = 0; k < QL_KEYS; k++) {
            if (plain_chars[k] == c) {
                push_key(0, k);
                break;
            }
            if (shifted_chars[k] == c) {
                push_key(4, k);
                break;
            }
        }
        s_type_wait = TYPE_FRAMES;
    }
}
