/*
 * On-board I/O at $18000-$1BFFF: the ZX8302 (the clock, interrupts, the
 * Microdrives' control, the link to the IPC) and the ZX8301's MODE
 * register. QL_IO logs every access on the host.
 *
 * The Microdrives so far are empty drives: the select shift register
 * ($18020 writes: bit 1 the clock, bit 0 the select bit; QDOS clocks a 1
 * then n-1 zeros for drive n, eight zeros to stop them all) and a gap
 * interrupt every GAP_LINES while one turns. No data ever comes ($18020
 * bit 2, "a byte to read", stays clear), so QDOS's header reads time out
 * and it reports the cartridge not found, as a QL with an empty drive does.
 */
#include <string.h>
#include <time.h>
#include "m68k.h"
#include "ql_internal.h"

bool ql_debug_io;

/* Interrupt status ($18021): gap, interface, transmit, frame, external.
 * A write clears the bits set in 0-4 and sets the masks: bits 5-7 enable
 * the gap, interface and transmit interrupts (the frame and external ones
 * always interrupt).
 */
#define INT_GAP         0x01
#define INT_FRAME       0x08
#define GAP_LINES       470             /* ~30 ms: a sector's gaps on a turning tape */

static uint8_t s_int_status, s_int_mask;
static uint8_t s_mdv_control;           /* the last write to $18020 */
static uint8_t s_mdv_select;            /* the select shift register: bit n drive n + 1 */
static int s_gap_lines;

void ql_io_reset(void)
{
    s_int_status = 0;
    s_int_mask = 0;
    s_mdv_control = 0;
    s_mdv_select = 0;
    s_gap_lines = 0;
    ql_ipc_reset();
}

/* $18020 written: the Microdrives' control */
static void mdv_control(uint8_t v)
{
    /* the select bit shifts in as the clock falls */
    if ((s_mdv_control & 0x02) && !(v & 0x02)) {
        s_mdv_select = (s_mdv_select << 1) | (v & 1);
    }
    s_mdv_control = v;
}

uint8_t ql_io_read8(uint32_t a)
{
    uint8_t v = 0;
    switch (a) {
    case 0x18000: case 0x18001: case 0x18002: case 0x18003: {
        /* The clock: seconds since 1961, big-endian */
        uint32_t t = (uint32_t)time(NULL) + 283996800u;
        v = t >> (8 * (3 - (a & 3)));
        break;
    }
    case 0x18020:
        /* bit 7 the IPC's reply bit; bit 6 (link busy) never set */
        v = ql_ipc_bit() << 7;
        break;
    case 0x18021:
        v = s_int_status;
        break;
    default:
        break;
    }
    if (ql_debug_io) {
        QL_LOGI("frame %lu line %d: read  $%05lx -> %02x (PC $%05lx)", (unsigned long)ql.frames, ql.line,
                (unsigned long)a, v, (unsigned long)(m68k_get_reg(NULL, M68K_REG_PPC) & QL_ADDR_MASK));
    }
    return v;
}

void ql_io_write8(uint32_t a, uint8_t v)
{
    if (ql_debug_io) {
        QL_LOGI("frame %lu line %d: write $%05lx <- %02x (PC $%05lx)", (unsigned long)ql.frames, ql.line,
                (unsigned long)a, v, (unsigned long)(m68k_get_reg(NULL, M68K_REG_PPC) & QL_ADDR_MASK));
    }
    switch (a) {
    case 0x18003:
        ql_ipc_write(v);
        break;
    case 0x18020:
        mdv_control(v);
        break;
    case 0x18021:
        s_int_status &= ~(v & 0x1F);
        s_int_mask = v & 0xE0;
        ql_update_irq();
        break;
    case 0x18063:
        ql_video_write_mode(v);
        break;
    default:
        break;
    }
}

void ql_io_frame(void)
{
    ql_ipc_frame();
    s_int_status |= INT_FRAME;
    ql_update_irq();
}

void ql_io_line(void)
{
    ql_sound_line();
    if (s_mdv_select && ++s_gap_lines >= GAP_LINES) {
        s_gap_lines = 0;
        s_int_status |= INT_GAP;
        ql_update_irq();
    }
}

int ql_io_irq_level(void)
{
    uint8_t enabled = (s_int_mask >> 5) | 0x18;     /* frame and external always */
    return (s_int_status & enabled) ? 2 : 0;
}

int ql_io_int_ack(int level)
{
    (void)level;
    return 0xFFFFFFFF;  /* M68K_INT_ACK_AUTOVECTOR */
}

/* ---- Not there yet ---- */

bool ql_mdv_insert(int drive, const char *path)
{
    (void)drive;
    (void)path;
    return false;
}

void ql_mdv_eject(int drive)
{
    (void)drive;
}

const char *ql_mdv_path(int drive)
{
    (void)drive;
    return NULL;
}

bool ql_mdv_write_protected(int drive)
{
    (void)drive;
    return true;
}

bool ql_mdv_create(const char *path)
{
    (void)path;
    return false;
}

bool ql_mdv_dirty(void)
{
    return false;
}

void ql_mdv_flush(void)
{
}
