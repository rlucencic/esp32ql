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
    uint8_t *ram;
    uint32_t ram_size, ram_end;     /* RAM is $20000 to ram_end */

    /* Host pointers for reads and writes, each less its page's address
     * (address a's byte is rmap[a >> 14][a]); NULL: the slow path
     */
    const uint8_t *rmap[QL_PAGES];
    uint8_t *wmap[QL_PAGES];

    /* Time */
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

/* Interrupts: the level the 68008 sees changed */
void ql_update_irq(void);

/* Memory (mem.c) */
void ql_mem_init(void);
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

/* Sound (sound.c) */
void ql_sound_reset(void);
void ql_sound_level(int16_t level);
void ql_sound_line(void);           /* two samples, at each line's end */

/* ZX8301 display (video.c) */
void ql_video_init(void);
void ql_video_reset(void);
void ql_video_write_mode(uint8_t v);
void ql_video_frame_end(void);
