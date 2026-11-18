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
 */
#include <string.h>
#include "m68k.h"
#include "ql_internal.h"

extern int m68ki_remaining_cycles;

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

unsigned int QL_HOT m68k_read_memory_8(unsigned int a)
{
    return read8(a);
}

unsigned int QL_HOT m68k_read_memory_16(unsigned int a)
{
    WAIT(BYTE_CYCLE);
    return (read8(a) << 8) | read8(a + 1);
}

unsigned int QL_HOT m68k_read_memory_32(unsigned int a)
{
    WAIT(2 * BYTE_CYCLE);
    return ((uint32_t)read8(a) << 24) | (read8(a + 1) << 16) | (read8(a + 2) << 8) | read8(a + 3);
}

void QL_HOT m68k_write_memory_8(unsigned int a, unsigned int v)
{
    write8(a, v);
    screen_write(a);
}

void QL_HOT m68k_write_memory_16(unsigned int a, unsigned int v)
{
    WAIT(BYTE_CYCLE);
    write8(a, v >> 8);
    write8(a + 1, v);
    screen_write(a);
}

void QL_HOT m68k_write_memory_32(unsigned int a, unsigned int v)
{
    WAIT(2 * BYTE_CYCLE);
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
