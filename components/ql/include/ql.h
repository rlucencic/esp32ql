#pragma once

/*
 * Sinclair QL: a 68008 at 7.5 MHz, 128 KB RAM (up to 640 KB here), 48 KB
 * ROM, the ZX8301 (display, DRAM), the ZX8302 (clock, interrupts,
 * Microdrives, serial out, the link to the IPC), the IPC (an 8049:
 * keyboard, sound, serial in) and two Microdrives. Platform independent: the
 * board's main.c and the host build (host/) drive it.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define QL_ROM_SIZE             (48 * 1024)     /* $00000-$0BFFF */
#define QL_RAM_BASE             0x20000
#define QL_RAM_128K             (128 * 1024)
#define QL_RAM_640K             (640 * 1024)    /* 512 KB expansion from $40000 */

/* The picture: always 512 x 256 (mode 8's pixels twice as wide) */
#define QL_SCREEN_WIDTH         512
#define QL_SCREEN_HEIGHT        256
#define QL_SCREEN_HZ            50

/* Copies of the CPU core's opcode tables (handler pointers and cycles) */
#define QL_OPCODE_TABLES_SIZE   0x50000

/* Sound: 16-bit stereo */
#define QL_AUDIO_HZ             31250

#define QL_MDV_DRIVES           2
#define QL_FLP_DRIVES           2
#define QL_FLP_IMAGE_SIZE       (80 * 2 * 9 * 512)  /* a 720 KB disk */
#define QL_TRUMP_ROM_SIZE       (32 * 1024)

typedef struct {
    const uint8_t *rom;         /* QL_ROM_SIZE bytes */
    uint8_t *ram;               /* ram_size bytes, from $20000 */
    uint32_t ram_size;          /* QL_RAM_128K or QL_RAM_640K */
    /* QL_OPCODE_TABLES_SIZE bytes for copies of the CPU core's tables in
     * faster memory (NULL: the built-in ones)
     */
    void *opcode_tables;
    /* The Miracle Trump Card's ROM (QL_TRUMP_ROM_SIZE bytes): its floppy
     * disk interface and Toolkit 2; NULL: no disk interface
     */
    const uint8_t *trump_rom;
} ql_config_t;

bool ql_init(const ql_config_t *config);
void ql_reset(void);
void ql_run_frame(void);
bool ql_halted(void);

/* ---- Display ---- */
uint32_t ql_screen_signature(void);
void ql_render_line_rgb565(int y, uint16_t *dst);

/* ---- Keyboard: the QL's keys by matrix position (row * 8 + column) ----
 *
 * The codes the IPC reports (the ROM's key translation table: JS $B458).
 * KEYROW(r) reads row 7 - r. SHIFT, CTRL and ALT are reported as modifiers.
 */
enum {
    QL_KEY_SHIFT, QL_KEY_CTRL, QL_KEY_ALT, QL_KEY_X, QL_KEY_V, QL_KEY_SLASH, QL_KEY_N, QL_KEY_COMMA,
    QL_KEY_8, QL_KEY_2, QL_KEY_6, QL_KEY_Q, QL_KEY_E, QL_KEY_0, QL_KEY_T, QL_KEY_U,
    QL_KEY_9, QL_KEY_W, QL_KEY_I, QL_KEY_TAB, QL_KEY_R, QL_KEY_MINUS, QL_KEY_Y, QL_KEY_O,
    QL_KEY_L, QL_KEY_3, QL_KEY_H, QL_KEY_1, QL_KEY_A, QL_KEY_P, QL_KEY_D, QL_KEY_J,
    QL_KEY_LBRACKET, QL_KEY_CAPSLOCK, QL_KEY_K, QL_KEY_S, QL_KEY_F, QL_KEY_EQUALS, QL_KEY_G, QL_KEY_SEMICOLON,
    QL_KEY_RBRACKET, QL_KEY_Z, QL_KEY_PERIOD, QL_KEY_C, QL_KEY_B, QL_KEY_POUND, QL_KEY_M, QL_KEY_QUOTE,
    QL_KEY_ENTER, QL_KEY_LEFT, QL_KEY_UP, QL_KEY_ESC, QL_KEY_RIGHT, QL_KEY_BACKSLASH, QL_KEY_SPACE, QL_KEY_DOWN,
    QL_KEY_F4, QL_KEY_F1, QL_KEY_5, QL_KEY_F2, QL_KEY_F3, QL_KEY_F5, QL_KEY_4, QL_KEY_7,
    QL_KEYS
};

/* A key down or up (from any task) */
void ql_key(int key, bool down);
/* Type text (\n is ENTER) a key at a time; false while still typing */
bool ql_type(const char *text);

/* ---- Sound ---- */
void ql_audio_enable(bool on);
int ql_audio_render(int16_t *dst, int frames);
int ql_audio_pending(void);

/* ---- Microdrives: cartridge images (drive 0 is MDV1_) ---- */
bool ql_mdv_insert(int drive, const char *path);
void ql_mdv_eject(int drive);
const char *ql_mdv_path(int drive);
bool ql_mdv_write_protected(int drive);
bool ql_mdv_create(const char *path);       /* a blank, unformatted cartridge */
bool ql_mdv_dirty(void);                    /* written since the last flush */
void ql_mdv_flush(void);                    /* written cartridges to their files */

/* ---- Floppy disks on the Trump Card: raw sector images (drive 0 is FLP1_) ----
 *
 * 720 KB (80 tracks, 2 sides, 9 sectors of 512 bytes, side 0 then 1 for each
 * track), also 360 KB (40 tracks) and 800 KB (10 sectors)
 */
bool ql_flp_insert(int drive, const char *path);
void ql_flp_eject(int drive);
const char *ql_flp_path(int drive);
bool ql_flp_write_protected(int drive);
bool ql_flp_create(const char *path);       /* a blank 720 KB disk, for FORMAT */
bool ql_flp_dirty(void);
void ql_flp_flush(void);

/* ---- The clock: the host's time plus an offset (seconds) that setting
 * it on the QL (SDATE, ADATE) changes; the board keeps it across restarts
 */
int32_t ql_rtc_offset(void);
void ql_rtc_set_offset(int32_t seconds);

/* ---- Status ---- */
typedef struct {
    uint32_t frames;
    uint32_t pc;
    uint16_t sr;
    uint8_t mode;               /* the ZX8301's MODE register */
    uint8_t mdv_motor;          /* the drive turning (1, 2), 0 none */
    uint32_t mdv_reads[QL_MDV_DRIVES], mdv_writes[QL_MDV_DRIVES];
    uint8_t flp_light;          /* the floppy drive busy (1, 2), 0 none */
    uint8_t flp_track[QL_FLP_DRIVES];
    uint32_t flp_reads[QL_FLP_DRIVES], flp_writes[QL_FLP_DRIVES];  /* sectors, tracks formatted */
} ql_status_t;

void ql_get_status(ql_status_t *status);
