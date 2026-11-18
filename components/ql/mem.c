/*
 * The 68008's memory: 20 address lines (1 MB, wrapping above it), an 8-bit
 * data bus.
 *
 *   $00000-$0BFFF  system ROM (48 KB)
 *   $0C000-$0FFFF  ROM cartridge port (empty here)
 *   $18000-$1BFFF  on-board I/O: ZX8302, ZX8301's MODE register (io.c)
 *   $20000-$3FFFF  RAM (128 KB; the screen at $20000, the second at $28000)
 *   $40000-$BFFFF  expansion RAM (640 KB machines)
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

static uint8_t slow_read8(uint32_t a)
{
    a &= QL_ADDR_MASK;
    if (a >= 0x18000 && a < 0x1C000) {
        return ql_io_read8(a);
    }
    return 0;
}

static void slow_write8(uint32_t a, uint8_t v)
{
    a &= QL_ADDR_MASK;
    if (a >= 0x18000 && a < 0x1C000) {
        ql_io_write8(a, v);
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
