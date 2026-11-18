/*
 * The 68008's memory: 20 address lines (1 MB, wrapping above it), an 8-bit
 * data bus.
 *
 *   $00000-$0BFFF  system ROM (48 KB)
 *   $0C000-$0FFFF  ROM cartridge port (empty here)
 *   $10000-$17FFF  the Trump Card's ROM (32 KB)
 *   $18000-$1BFFF  on-board I/O: ZX8302, ZX8301's MODE register (io.c)
 *   $1C000-$1C003  the Trump Card's WD1772 (flp.c)
 *   $1E000         its drive latch (write only)
 *   $20000-$3FFFF  RAM (128 KB; the screen at $20000, the second at $28000)
 *   $40000-$BFFFF  expansion RAM (640 KB machines)
 *   $C0000-$C7FFF  the Trump Card's ROM again, after a read of $0C000-$0FFFF
 *                  and until one of $10000-$17FFF (as MAME's trumpcard.cpp
 *                  has it): QDOS looks for expansion ROMs at $C0000, the
 *                  card's code then runs at $10000
 *
 * Everything else reads as nothing and ignores writes. Memory is kept in
 * the 68008's byte order.
 *
 * Time: Musashi counts the 68000's clocks, a bus cycle of 4 for each word;
 * the 68008 moves a word as two byte cycles, so a word access costs 4
 * clocks more and a long one 8 (taken from Musashi's remaining cycles: the
 * instruction lasts longer).
 *
 * The ZX8301's contention (docs/ql-hardware.md 2.5): it owns the on-board
 * RAM's bus ($20000-$3FFFF, all of it, not only the screen) and reads the
 * screen in chunks of 12 CPU clocks, 40 a line; in a chunk it reads in, the
 * CPU gets one byte access, in a free one three. So a byte access to that
 * RAM in a busy chunk costs 12 clocks instead of 4 (RAM-bound code gets 56
 * accesses a picture line of 120: Nasta's figure). Busy: 32 chunks of a
 * picture line, 8 of the others (the refresh: MiSTer's and sQLux's model,
 * timed on a QL; Nasta has 32 there too). Where the CPU is in the line:
 * the instruction's start plus the waits already charged in it (Musashi
 * charges an instruction's clocks after it). ROM, the I/O and expansion
 * RAM run at full speed.
 */
#include <string.h>
#include "m68k.h"
#include "ql_internal.h"

extern int m68ki_remaining_cycles;
extern int m68ki_initial_cycles;

#define BYTE_CYCLE      4
#define WAIT(n)         (m68ki_remaining_cycles -= (n))

#define TRUMP_ROM       0x10000
#define TRUMP_MIRROR    0xC0000

/* The Trump Card's ROM at $C0000 or not: while it is, its pages at $10000
 * take the slow path, where a read turns the mirror off
 */
static void trump_map(bool rom_en)
{
    ql.trump_rom_en = rom_en;
    for (uint32_t p = 0; p < QL_TRUMP_ROM_SIZE >> QL_PAGE_SHIFT; p++) {
        ql.rmap[(TRUMP_ROM >> QL_PAGE_SHIFT) + p] = rom_en ? NULL : ql.trump_rom - TRUMP_ROM;
        ql.rmap[(TRUMP_MIRROR >> QL_PAGE_SHIFT) + p] = rom_en ? ql.trump_rom - TRUMP_MIRROR : NULL;
    }
}

static uint8_t slow_read8(uint32_t a)
{
    a &= QL_ADDR_MASK;
    if (a >= 0x18000 && a < 0x1C000) {
        return ql_io_read8(a);
    }
    if (ql.trump_rom) {
        if (a >= 0x1C000 && a < 0x1C004) {
            return ql_flp_read8(a);
        }
        if (a >= 0x0C000 && a < TRUMP_ROM) {
            if (!ql.trump_rom_en) {
                trump_map(true);
            }
        } else if (a >= TRUMP_ROM && a < TRUMP_ROM + QL_TRUMP_ROM_SIZE) {
            trump_map(false);
            return ql.trump_rom[a - TRUMP_ROM];
        }
    }
    return 0;
}

static void slow_write8(uint32_t a, uint8_t v)
{
    a &= QL_ADDR_MASK;
    if (a >= 0x18000 && a < 0x1C000) {
        ql_io_write8(a, v);
    } else if (ql.trump_rom && ((a >= 0x1C000 && a < 0x1C004) || a == 0x1E000)) {
        ql_flp_write8(a, v);
    }
}

static inline uint8_t read8(uint32_t a)
{
    const uint8_t *p = ql.rmap[(a & QL_ADDR_MASK) >> QL_PAGE_SHIFT];
    return p ? p[a & QL_ADDR_MASK] : slow_read8(a);
}

static inline void write8(uint32_t a, uint8_t v)
{
    uint8_t *p = ql.wmap[(a & QL_ADDR_MASK) >> QL_PAGE_SHIFT];
    if (p) {
        p[a & QL_ADDR_MASK] = v;
    } else {
        slow_write8(a, v);
    }
}

/* The displayed screen written: the picture changes */
static inline void screen_write(uint32_t a)
{
    uint32_t base = (ql.mode & 0x80) ? 0x28000 : 0x20000;
    if ((a & QL_ADDR_MASK) - base < 0x8000) {
        ql.screen_dirty = true;
    }
}

void ql_mem_init(void)
{
    memset(ql.rmap, 0, sizeof(ql.rmap));
    memset(ql.wmap, 0, sizeof(ql.wmap));
    for (uint32_t a = 0; a < QL_ROM_SIZE; a += 1u << QL_PAGE_SHIFT) {
        ql.rmap[a >> QL_PAGE_SHIFT] = ql.rom;
    }
    for (uint32_t a = QL_RAM_BASE; a < ql.ram_end; a += 1u << QL_PAGE_SHIFT) {
        ql.rmap[a >> QL_PAGE_SHIFT] = ql.ram - QL_RAM_BASE;
        ql.wmap[a >> QL_PAGE_SHIFT] = ql.ram - QL_RAM_BASE;
    }
    ql_mem_reset();
}

void ql_mem_reset(void)
{
    if (ql.trump_rom) {
        trump_map(false);
    }
}

uint8_t ql_peek8(uint32_t a)
{
    return read8(a & QL_ADDR_MASK);
}

void ql_poke8(uint32_t a, uint8_t v)
{
    write8(a & QL_ADDR_MASK, v);
}

/* ---- Musashi callbacks ---- */

#define CHUNK           12      /* CPU clocks: 24 of the ZX8301's */
#define CONTENDED       8       /* a byte access in a busy chunk: 12 clocks, not 4 */

/* bytes accesses to address a: in the on-board RAM, each waits if the
 * ZX8301 is reading a chunk of the screen where the CPU is in the line
 */
static inline void contend(uint32_t a, int bytes)
{
    if (((a & QL_ADDR_MASK) >> QL_PAGE_SHIFT) - (0x20000 >> QL_PAGE_SHIFT) >= (0x20000 >> QL_PAGE_SHIFT)) {
        return;
    }
    for (int i = 0; i < bytes; i++) {
        int pos = ql.cycle_in_line + (m68ki_initial_cycles - m68ki_remaining_cycles);
        if (pos >= QL_LINE_CYCLES) {
            pos -= QL_LINE_CYCLES;              /* (into the next line: about the same) */
        }
        if (pos / CHUNK < ql.busy_chunks) {
            WAIT(CONTENDED);
        }
    }
}

unsigned int QL_HOT m68k_read_memory_8(unsigned int a)
{
    contend(a, 1);
    return read8(a);
}

unsigned int QL_HOT m68k_read_memory_16(unsigned int a)
{
    WAIT(BYTE_CYCLE);
    contend(a, 2);
    return (read8(a) << 8) | read8(a + 1);
}

unsigned int QL_HOT m68k_read_memory_32(unsigned int a)
{
    WAIT(2 * BYTE_CYCLE);
    contend(a, 4);
    return ((uint32_t)read8(a) << 24) | (read8(a + 1) << 16) | (read8(a + 2) << 8) | read8(a + 3);
}

void QL_HOT m68k_write_memory_8(unsigned int a, unsigned int v)
{
    contend(a, 1);
    write8(a, v);
    screen_write(a);
}

void QL_HOT m68k_write_memory_16(unsigned int a, unsigned int v)
{
    WAIT(BYTE_CYCLE);
    contend(a, 2);
    write8(a, v >> 8);
    write8(a + 1, v);
    screen_write(a);
}

void QL_HOT m68k_write_memory_32(unsigned int a, unsigned int v)
{
    WAIT(2 * BYTE_CYCLE);
    contend(a, 4);
    write8(a, v >> 24);
    write8(a + 1, v >> 16);
    write8(a + 2, v >> 8);
    write8(a + 3, v);
    screen_write(a);
    screen_write(a + 3);
}

/* The disassembler reads without side effects */
unsigned int m68k_read_disassembler_8(unsigned int a)
{
    a &= QL_ADDR_MASK;
    const uint8_t *p = ql.rmap[a >> QL_PAGE_SHIFT];
    if (p == NULL && ql.trump_rom && a >= TRUMP_ROM && a < TRUMP_ROM + QL_TRUMP_ROM_SIZE) {
        return ql.trump_rom[a - TRUMP_ROM];
    }
    return p ? p[a] : 0;
}

unsigned int m68k_read_disassembler_16(unsigned int a)
{
    return (m68k_read_disassembler_8(a) << 8) | m68k_read_disassembler_8(a + 1);
}

unsigned int m68k_read_disassembler_32(unsigned int a)
{
    return ((uint32_t)m68k_read_disassembler_16(a) << 16) | m68k_read_disassembler_16(a + 2);
}
