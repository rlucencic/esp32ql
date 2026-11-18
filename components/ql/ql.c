/*
 * The QL: the 68008 (Musashi as a 68000, 68008 bus timing in mem.c), the
 * frame loop, interrupts, the debugger.
 *
 * A frame is QL_LINES lines of QL_LINE_CYCLES clocks; at each line's end
 * the on-board I/O moves on (io.c: the clock, the IPC link, the
 * Microdrives, the sound).
 */
#include <stdio.h>
#include <string.h>
#include "m68k.h"
#include "emudbg.h"
#include "ql_internal.h"

ql_t ql;

uint32_t ql_now(void)
{
    return (uint32_t)ql.line * QL_LINE_CYCLES + ql.cycle_in_line + (ql.cpu_running ? m68k_cycles_run() : 0);
}

uint64_t ql_clock(void)
{
    return ql.frame_clock + ql_now();
}

/* The ZX8302's interrupt is level 2, autovectored */
void ql_update_irq(void)
{
    int level = ql_io_irq_level();
    if (level != ql.irq_level) {
        bool raised = level > ql.irq_level;
        ql.irq_level = level;
        m68k_set_irq(level);
        /* Musashi only samples the IRQ lines when a timeslice starts */
        if (raised && ql.cpu_running) {
            m68k_end_timeslice();
        }
    }
}

static int int_ack(int level)
{
    return ql_io_int_ack(level);
}

/* A double bus/address fault: the 68008 stops until the next reset */
void m68k_host_halted(void)
{
    if (ql.halted) {
        return;
    }
    ql.halted = true;
    QL_LOGW("CPU halted (double fault): PC $%05lx, SR $%04x, SSP $%05lx",
            (unsigned long)(m68k_get_reg(NULL, M68K_REG_PPC) & QL_ADDR_MASK), m68k_get_reg(NULL, M68K_REG_SR),
            (unsigned long)m68k_get_reg(NULL, M68K_REG_ISP));
}

void m68k_host_exception(unsigned int vector)
{
    (void)vector;
}

int m68k_host_illegal(int opcode)
{
    (void)opcode;
    return 0;
}

/* RESET instruction: the peripherals, not the CPU */
static void reset_instruction(void)
{
    ql_io_reset();
}

/* ---- The debugger (emudbg.h) ---- */

static uint8_t dbg_peek(void *ctx, uint32_t a)
{
    (void)ctx;
    return ql_peek8(a);
}

static void dbg_poke(void *ctx, uint32_t a, uint8_t v)
{
    (void)ctx;
    ql_poke8(a, v);
}

static emudbg_cpu_t s_dbg_cpus[] = {
    { .name = "68008", .arch = &emudbg_arch_68000, .peek = dbg_peek, .poke = dbg_poke },
};

static void dbg_where(char *buf, size_t len)
{
    snprintf(buf, len, "frame %lu, line %d", (unsigned long)ql.frames, ql.line);
}

static void dbg_screen(int *width, int *height)
{
    *width = QL_SCREEN_WIDTH;
    *height = QL_SCREEN_HEIGHT;
}

static emudbg_machine_t s_dbg = {
    .name = "Sinclair QL",
    .cpus = s_dbg_cpus,
    .cpu_count = 1,
    .where = dbg_where,
    .reset = ql_reset,
    .screen = dbg_screen,
    .render_line = ql_render_line_rgb565,
};

/* ---- Set up, reset, run ---- */

bool ql_init(const ql_config_t *config)
{
    if (config->rom == NULL || config->ram == NULL ||
        (config->ram_size != QL_RAM_128K && config->ram_size != QL_RAM_640K)) {
        return false;
    }
    memset(&ql, 0, sizeof(ql));
    ql.rom = config->rom;
    ql.ram = config->ram;
    ql.ram_size = config->ram_size;
    ql.ram_end = QL_RAM_BASE + config->ram_size;
    ql.trump_rom = config->trump_rom;
    ql_mem_init();
    ql_video_init();
    if (!ql_ser_init() || !ql_flp_init()) {
        return false;
    }

    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    if (config->opcode_tables != NULL) {
        m68k_use_table_copies(config->opcode_tables);
    }
    m68k_set_int_ack_callback(int_ack);
    m68k_set_reset_instr_callback(reset_instruction);

    QL_LOGI("%lu KB RAM%s", (unsigned long)(ql.ram_size / 1024), ql.trump_rom ? ", a Trump Card" : "");
    ql_reset();
    emudbg_set_machine(&s_dbg);
    return true;
}

void ql_reset(void)
{
    /* What QDOS finds in RAM at power-on doesn't matter: it tests and clears it */
    memset(ql.ram, 0, ql.ram_size);
    ql.frames = 0;
    ql.line = 0;
    ql.cycle_in_line = 0;
    ql.halted = false;
    ql.irq_level = 0;
    ql.screen_dirty = true;
    ql_mem_reset();
    ql_video_reset();
    ql_io_reset();
    ql_flp_reset();
    m68k_set_irq(0);
    m68k_pulse_reset();
}

bool ql_halted(void)
{
    return ql.halted;
}

static void run_cycles(int cycles)
{
    if (ql.halted) {
        ql.cycle_in_line = 0;
        return;
    }
    /* cycle_in_line carries the overshoot of the previous line */
    while (ql.cycle_in_line < cycles) {
        ql.cpu_running = true;
        int used = m68k_execute(cycles - ql.cycle_in_line);
        ql.cpu_running = false;
        ql.cycle_in_line += used;
    }
    ql.cycle_in_line -= cycles;
}

void ql_run_frame(void)
{
    ql_io_frame();
    for (ql.line = 0; ql.line < QL_LINES;) {
        ql.busy_chunks = ql.line >= QL_FIRST_LINE && ql.line < QL_FIRST_LINE + QL_SCREEN_HEIGHT ? QL_BUSY_PICTURE
                                                                                            : QL_BUSY_BLANK;
        run_cycles(QL_LINE_CYCLES);
        /* the overshoot counts from the next line's start: the line's end
         * is the next one's start (ql_now() stays right in ql_io_line())
         */
        ql.line++;
        ql_io_line();
    }
    ql.line = 0;
    ql.frame_clock += QL_LINES * QL_LINE_CYCLES;
    ql_video_frame_end();
    ql.frames++;
}

/* A long from the RAM ($20000 on), as the 68008 sees it */
static uint32_t ram_long(uint32_t a)
{
    const uint8_t *p = ql.ram + (a - QL_RAM_BASE);
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

void ql_get_machine(ql_machine_t *m)
{
    memset(m, 0, sizeof(*m));
    if (ql.ram == NULL) {
        return;
    }
    m->mode = ql.mode;
    ql_io_state(&m->tmode, &m->int_mask);
    m->supervisor = (m68k_get_reg(NULL, M68K_REG_SR) & 0x2000) != 0;
    ql_beep_state(m);
    for (int p = 0; p < QL_SER_PORTS; p++) {
        m->ser_open[p] = ql_ser_is_open(p);
    }
    /* QDOS's system variables: SV.IDENT $D254 at their start */
    static const uint32_t places[2] = { 0x28000, 0x30000 };
    for (int i = 0; i < 2; i++) {
        uint32_t sv = places[i];
        if (sv + 0x90 > QL_RAM_BASE + ql.ram_size || (ram_long(sv) >> 16) != 0xD254) {
            continue;
        }
        uint32_t free_base = ram_long(sv + 0x0C), basic = ram_long(sv + 0x10), ramtop = ram_long(sv + 0x20);
        if (free_base > basic || ramtop > QL_RAM_BASE + ql.ram_size) {
            continue;
        }
        m->qdos = true;
        m->sysvars = sv;
        m->ramtop = ramtop;
        m->free_bytes = basic - free_base > 1024 ? basic - free_base - 1024 : 0;   /* as FREE_MEM */
        m->caps_lock = ql.ram[sv + 0x88 - QL_RAM_BASE] != 0 || ql.ram[sv + 0x89 - QL_RAM_BASE] != 0;
        break;
    }
}

void ql_get_status(ql_status_t *status)
{
    memset(status, 0, sizeof(*status));
    status->frames = ql.frames;
    status->pc = m68k_get_reg(NULL, M68K_REG_PC) & QL_ADDR_MASK;
    status->sr = m68k_get_reg(NULL, M68K_REG_SR);
    status->mode = ql.mode;
    status->mdv_motor = ql_mdv_motor();
    ql_mdv_counts(status->mdv_reads, status->mdv_writes);
    ql_flp_status(&status->flp_light, status->flp_track, status->flp_reads, status->flp_writes);
}
