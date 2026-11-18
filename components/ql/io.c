/*
 * On-board I/O at $18000-$1BFFF: a first version to see what the ROM does
 * (QL_IO logs every access on the host). The ZX8302, the IPC link and the
 * Microdrives follow from the documentation.
 */
#include <string.h>
#include <time.h>
#include "ql_internal.h"

bool ql_debug_io;

/* Interrupt status ($18021): gap, interface, transmit, frame, external */
#define INT_FRAME       0x08

static uint8_t s_int_status, s_int_mask;

void ql_io_reset(void)
{
    s_int_status = 0;
    s_int_mask = 0;
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
    case 0x18021:
        v = s_int_status;
        break;
    default:
        break;
    }
    if (ql_debug_io) {
        QL_LOGI("frame %lu line %d: read  $%05lx -> %02x (PC $%05lx)", (unsigned long)ql.frames, ql.line,
                (unsigned long)a, v, (unsigned long)0);
    }
    return v;
}

void ql_io_write8(uint32_t a, uint8_t v)
{
    if (ql_debug_io) {
        QL_LOGI("frame %lu line %d: write $%05lx <- %02x", (unsigned long)ql.frames, ql.line, (unsigned long)a, v);
    }
    switch (a) {
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
    s_int_status |= INT_FRAME;
    ql_update_irq();
}

void ql_io_line(void)
{
    ql_sound_line();
}

int ql_io_irq_level(void)
{
    return s_int_status ? 2 : 0;
}

int ql_io_int_ack(int level)
{
    (void)level;
    return 0xFFFFFFFF;  /* M68K_INT_ACK_AUTOVECTOR */
}

/* ---- Not there yet ---- */

void ql_key(int key, bool down)
{
    (void)key;
    (void)down;
}

bool ql_type(const char *text)
{
    (void)text;
    return true;
}

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
