/*
 * On-board I/O at $18000-$1BFFF: the ZX8302 and the ZX8301's MODE register
 * (docs/ql-hardware.md 3). QL_IO logs every access on the host.
 *
 *   $18000-3 read   the clock: seconds since 1961, most significant first
 *   $18000 write    the clock to 0; $18001 write: a 0 in bit 4, 3, 2, 1
 *                   adds 1 to byte 0, 1, 2, 3 (as $18000-3 read)
 *   $18002 write    transmit control: baud, SER1/SER2/Microdrive/network
 *   $18003 write    the IPC link (ipc.c)
 *   $18020 read     bit 7 the IPC's bit, 6 link busy (never), 3 gap, 2 a
 *                   Microdrive byte to read, 1 transmit buffer full
 *   $18020 write    the Microdrives' control (mdv.c)
 *   $18021 read     interrupts pending: gap, interface, transmit, frame,
 *                   external (bits 0-4)
 *   $18021 write    bits 0-4 clear those, bits 5-7 enable the gap,
 *                   interface and transmit interrupts (frame and external
 *                   always interrupt); enabling the gap's while a gap
 *                   passes raises it (docs 3.2: JS hangs without it)
 *   $18022/3 read   the Microdrive's bytes; $18022 write: a byte to send
 *   $18063 write    the ZX8301's MODE (video.c)
 */
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include "m68k.h"
#include "ql_internal.h"

bool ql_debug_io;

#define INT_GAP         0x01
#define INT_TRANSMIT    0x04
#define INT_FRAME       0x08
#define MASK_GAP        0x20
#define RTC_1961        283996800u      /* seconds from 1961 to 1970 */

static uint8_t s_int_status, s_int_mask;
static uint8_t s_tmode;                 /* $18002 */
static bool s_gap;                      /* a gap under the head at the last look */
static int32_t s_rtc_offset;            /* seconds the QL's clock is off the host's */

void ql_io_reset(void)
{
    s_int_status = 0;
    s_int_mask = 0;
    s_tmode = 0;
    s_gap = true;
    ql_ipc_reset();
    ql_mdv_reset();
    ql_ser_reset();
}

#ifndef ESP_PLATFORM
/* The host runs far faster than real time: its clock counts the emulated
 * time from the moment it started (QL_TIME: a given start, seconds since
 * 1970), so DATE-timed programs (benchmarks) measure the QL, and runs repeat
 */
static uint32_t host_start;

static uint32_t now_seconds(void)
{
    if (host_start == 0) {
        const char *t = getenv("QL_TIME");
        host_start = t ? (uint32_t)strtoul(t, NULL, 10) : (uint32_t)time(NULL);
    }
    return host_start + (uint32_t)(ql_clock() / QL_CPU_HZ);
}
#else
static uint32_t now_seconds(void)
{
    return (uint32_t)time(NULL);
}
#endif

static uint32_t rtc(void)
{
    return now_seconds() + RTC_1961 + s_rtc_offset;
}

int32_t ql_rtc_offset(void)
{
    return s_rtc_offset;
}

void ql_rtc_set_offset(int32_t seconds)
{
    s_rtc_offset = seconds;
}

/* A gap passing with its interrupt enabled */
static void gap_check(uint64_t clock, bool mask_written)
{
    bool gap = ql_mdv_gap(clock);
    if (gap && (s_int_mask & MASK_GAP) && (mask_written || !s_gap)) {
        s_int_status |= INT_GAP;
        ql_update_irq();
    }
    s_gap = gap;
}

/* $18021's top bits as Minerva's inc/pc describes them (JS ignores them):
 * bit 7 the transmit baud clock, up and down once a bit (Minerva counts 12
 * of its rises before it turns from a serial port back to the
 * Microdrives), bit 6 1 unless a Microdrive turns, bit 5 the lowest bit of
 * a 65536 Hz count
 */
static uint8_t status_top(uint64_t clock)
{
    static const uint32_t baud[8] = { 19200, 9600, 4800, 2400, 1200, 600, 300, 75 };
    uint8_t v = 0;
    if ((clock * baud[s_tmode & 7] * 2 / QL_CPU_HZ) & 1) {
        v |= 0x80;
    }
    if (!ql_mdv_motor()) {
        v |= 0x40;
    }
    if ((clock * 65536 / QL_CPU_HZ) & 1) {
        v |= 0x20;
    }
    return v;
}

uint8_t ql_io_read8(uint32_t a)
{
    uint8_t v = 0;
    switch (a) {
    case 0x18000: case 0x18001: case 0x18002: case 0x18003:
        v = rtc() >> (8 * (3 - (a & 3)));
        break;
    case 0x18020: {
        uint64_t clock = ql_clock();
        v = (ql_ipc_bit() << 7) | ql_mdv_status(clock) | ql_ser_handshake();
        if ((s_tmode & 0x10) == 0 && ql_ser_busy(clock)) {
            v |= 0x02;                          /* TXFL: a serial byte going out */
        }
        break;
    }
    case 0x18021:
        v = s_int_status | status_top(ql_clock());
        break;
    case 0x18022: case 0x18023:
        v = ql_mdv_read(ql_clock());
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
    case 0x18000:
        s_rtc_offset -= (int32_t)rtc();
        break;
    case 0x18001:
        for (int bit = 4; bit >= 1; bit--) {
            if (!(v & (1u << bit))) {
                s_rtc_offset += 1 << (8 * (bit - 1));
            }
        }
        break;
    case 0x18002:
        s_tmode = v;
        break;
    case 0x18003:
        ql_ipc_write(v);
        break;
    case 0x18020:
        ql_mdv_control(v, ql_clock());
        break;
    case 0x18021:
        s_int_status &= ~(v & 0x1F);
        s_int_mask = v & 0xE0;
        gap_check(ql_clock(), true);
        ql_update_irq();
        break;
    case 0x18022:
        if ((s_tmode & 0x18) == 0x10) {
            ql_mdv_write(v, ql_clock());        /* microdrive mode */
        } else if ((s_tmode & 0x10) == 0) {
            ql_ser_send(s_tmode, v, ql_clock());    /* SER1, SER2 */
        }                                       /* (the network: not here) */
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
    uint64_t clock = ql_clock();
    gap_check(clock, false);
    if (ql_ser_line(clock)) {
        s_int_status |= INT_TRANSMIT;           /* a serial byte is out: the next */
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
