#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ql.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_attr.h"
#define QL_LOGI(...)    ESP_LOGI("ql", __VA_ARGS__)
#define QL_LOGW(...)    ESP_LOGW("ql", __VA_ARGS__)
#define QL_HOT          IRAM_ATTR
#else
#include <stdio.h>
#define QL_LOGI(...)    do { printf("ql: " __VA_ARGS__); printf("\n"); } while (0)
#define QL_LOGW(...)    do { printf("ql: warning: " __VA_ARGS__); printf("\n"); } while (0)
#define QL_HOT
#endif

/* ---- Time: the 68008's clocks (7.5 MHz) ---- */
#define QL_CPU_HZ           7500000
#define QL_LINE_CYCLES      480         /* 64 us */
#define QL_LINES            312         /* a PAL frame: 149760 clocks, 50.08 Hz */
#define QL_FIRST_LINE       40          /* the first of the 256 displayed lines */

/* ---- Memory: 64 pages of 16 KB over the 68008's 1 MB ---- */
#define QL_PAGE_SHIFT       14
#define QL_PAGES            64
#define QL_ADDR_MASK        0xFFFFF

typedef struct {
    const uint8_t *rom;
    const uint8_t *trump_rom;       /* the Trump Card's, NULL without one */
    bool trump_rom_en;              /* its ROM showing at $C0000 too */
    uint8_t *ram;
    uint32_t ram_size, ram_end;     /* RAM is $20000 to ram_end */

    /* Host pointers for reads and writes, each less its page's address
     * (address a's byte is rmap[a >> 14][a]); NULL: the slow path
     */
    const uint8_t *rmap[QL_PAGES];
    uint8_t *wmap[QL_PAGES];

    /* Time */
    uint64_t frame_clock;           /* clocks since power-on at this frame's start */
    int line;
    int cycle_in_line;              /* the CPU's overshoot into the next line */
    bool cpu_running;
    uint32_t frames;
    bool halted;
    int irq_level;

    /* ZX8301 */
    uint8_t mode;                   /* MODE register ($18063) */
    bool screen_dirty;              /* the displayed screen written this frame */
} ql_t;

extern ql_t ql;

/* The CPU's position in the frame, in clocks */
uint32_t ql_now(void);

/* Clocks since power-on, to the CPU's position now */
uint64_t ql_clock(void);

/* Interrupts: the level the 68008 sees changed */
void ql_update_irq(void);

/* Memory (mem.c) */
void ql_mem_init(void);
void ql_mem_reset(void);
uint8_t ql_peek8(uint32_t a);
void ql_poke8(uint32_t a, uint8_t v);

/* On-board I/O, $18000-$1BFFF (io.c) */
uint8_t ql_io_read8(uint32_t a);
void ql_io_write8(uint32_t a, uint8_t v);
void ql_io_reset(void);
void ql_io_line(void);                 /* at each line's end */
void ql_io_frame(void);                /* at the frame's start */
int ql_io_irq_level(void);
int ql_io_int_ack(int level);

/* The IPC: keyboard, sound, serial input (ipc.c) */
void ql_ipc_reset(void);
void ql_ipc_write(uint8_t v);       /* $18003 */
uint8_t ql_ipc_bit(void);           /* $18020 bit 7 */
void ql_ipc_frame(void);            /* keys and typed text, at each frame's start */

/* The Microdrives (mdv.c) */
void ql_mdv_reset(void);
void ql_mdv_control(uint8_t v, uint64_t clock);     /* $18020 written */
uint8_t ql_mdv_status(uint64_t clock);              /* $18020 bits 1-3 */
uint8_t ql_mdv_read(uint64_t clock);                /* $18022/3 read */
void ql_mdv_write(uint8_t v, uint64_t clock);       /* $18022 written */
bool ql_mdv_gap(uint64_t clock);                    /* a gap under the head (or none turning) */
int ql_mdv_motor(void);                             /* the drive turning (1, 2), 0 none */
void ql_mdv_counts(uint32_t *reads, uint32_t *writes);

/* The Trump Card's floppy controller (flp.c) */
void ql_flp_reset(void);
uint8_t ql_flp_read8(uint32_t a);                   /* $1C000-$1C003 */
void ql_flp_write8(uint32_t a, uint8_t v);          /* $1C000-$1C003, $1E000 */
void ql_flp_status(uint8_t *light, uint8_t *track, uint32_t *reads, uint32_t *writes);

/* The IPC's beeper (beep.c) */
void ql_beep_reset(void);
void ql_beep_start(const uint8_t params[8]);    /* command 10 */
void ql_beep_stop(void);                        /* command 11 */
bool ql_beep_on(void);                          /* the status's bit 1 */
int16_t ql_beep_sample(uint64_t end);           /* a sample up to that clock */

/* Sound (sound.c) */
void ql_sound_line(void);           /* two samples, at each line's end */

/* ZX8301 display (video.c) */
void ql_video_init(void);
void ql_video_reset(void);
void ql_video_write_mode(uint8_t v);
void ql_video_frame_end(void);
