/*
 * The IPC's beeper: the 8049 toggles its speaker pin (P2.1) from its timer
 * interrupt, a square wave whose half periods the interrupt routine works
 * out each time from the eight bytes of IPC command 10 (INSO; docs/
 * ql-hardware.md 5, read from Jan Venema's annotated IPC disassembly, IPC
 * $0300-$0441, $0700-$074E):
 *
 *   $3A pitch 1, $3B pitch 2, $3C/$3D the interval between steps, $3E/$3F
 *   the duration (both low byte first; duration 0: for ever), $40 the step
 *   (high nibble, signed) and wrap (low), $41 random (high) and fuzzy (low)
 *
 * Each interrupt: unless the sound is off (then the pin goes low and stays
 * there), the duration count grows by the half period's ticks + 6 and,
 * past the duration, the main loop turns the sound off; the interval count
 * by the ticks + 6 (+ 4 without a duration), and once past the interval
 * (at once when it is 0) the pitch takes a step: outside pitch 1 .. pitch 2
 * (exclusive) it starts again (wrap 15: always, else up to wrap times) or
 * the step turns round; random perturbs a step, fuzzy each half period
 * (bit 3 on: a random number masked to 2^(n+1) - 1). Then the pin toggles
 * and the timer is loaded with the half period, in ticks of 32 machine
 * cycles (0: 256).
 *
 * A half period is those ticks plus the interrupt routine's own time from
 * the timer's overflow to its restart, which depends on the path taken
 * (252 machine cycles for a plain BEEP without a duration, 307 with one):
 * each branch below adds its machine cycles, counted from the listing (an
 * 8049 instruction takes 1 or 2), so the pitches come out as the real
 * IPC's: f = 733333 / (2 x (32 x pitch + overhead)) Hz. Checked against a
 * small 8049 interpreter running the listing's instructions (half periods
 * equal for steps, wraps, bounces, random and fuzz).
 *
 * The 8049 runs at 11 MHz, a machine cycle 15 clocks: 733333 a second.
 * Time here is in units of 1/22 of the 68008's clock (7.5 MHz): a machine
 * cycle is 225 of them, a 32 us sample 5280.
 */
#include <stdio.h>
#include <string.h>
#include "ql_internal.h"

#define UNITS_PER_CLOCK     22                  /* 68008 clocks */
#define UNITS_PER_CYCLE     225                 /* 8049 machine cycles */
#define SAMPLE_UNITS        (QL_LINE_CYCLES / 2 * UNITS_PER_CLOCK)
#define LATENCY             3                   /* the instruction under way, the interrupt's call */
#define AMPLITUDE           7000

static struct {
    /* INSO's parameters ($3A-$41) */
    uint8_t p1, p2;
    uint16_t interval, duration;
    uint8_t stepwrap, randfuzz;
    /* the routine's variables ($42-$49) */
    uint8_t pitch, base, half;
    uint16_t icount, dcount;
    uint8_t wraps;
    /* IPCSTAT bit 1: sound on; the timer running for the sound; the pin */
    bool on, running, pin;
    /* the random generator: $25/$26 and bank 1's R4 (the multiplier's high byte) */
    uint16_t rnd;
    uint8_t r4;
} b;

static uint64_t s_next;                         /* units: the timer's next overflow */
static bool s_starting;                         /* the next "overflow" starts the sound (IPC $004A) */
static bool s_edge_pending;                     /* an edge worked out, still ahead */
static uint64_t s_edge_at;
static bool s_edge_level;
static uint64_t s_now;                          /* units: the sound made up to here */
static int32_t s_x, s_y;                        /* the speaker's coupling: a DC-blocking filter */

/* SUB16 (IPC $070B): 14 cycles when the high bytes differ, else 28 */
static int cmp_cost(uint16_t a, uint16_t c)
{
    return (a >> 8) != (c >> 8) ? 14 : 28;
}

/* The IPC's random step (IPC $073A): ($25,$26) from $26 x (R4:$8D), the
 * multiplier shifted through the carry as the 8049's MUL does
 */
static uint8_t random_byte(int *cycles)
{
    uint8_t r2 = b.rnd >> 8, r3 = 0x8D, r4 = b.r4, r5 = 0, r6 = 0;
    int c = 0;
    *cycles += 22 + 7 + 2 + 2 * 10;             /* the routine, MUL's start and end, two ADD16s */
    for (int i = 0; i < 8; i++) {
        int bit = r2 & 1;
        r2 = (r2 >> 1) | (c << 7);
        c = bit;
        *cycles += 5 + 8;
        if (c) {
            int s = r3 + r5;
            r5 = s;
            c = s >> 8;
            s = r4 + r6 + c;
            r6 = s;
            c = s >> 8;
            *cycles += 6;
        }
        int n3 = ((r3 << 1) | c) & 0xFF;
        c = r3 >> 7;
        r3 = n3;
        int n4 = ((r4 << 1) | c) & 0xFF;
        c = r4 >> 7;
        r4 = n4;
    }
    b.r4 = r4;
    uint16_t v = (uint16_t)(r6 << 8) | (b.rnd & 0xFF);
    v = (uint16_t)(v + r5);
    v = (uint16_t)(v + 0x8B);
    b.rnd = v;
    return v & 0xFF;
}

/* "update sound random/fuzz" (IPC $0422): a nibble's random amount */
static uint8_t rnd(uint8_t nibble, int *cycles)
{
    if (!(nibble & 8)) {
        *cycles += 8;
        return 0;
    }
    int n = (nibble & 7) + 1;
    *cycles += 5 + 19 + 4 * n + 3;
    uint8_t r = random_byte(cycles);
    return r & ((1 << n) - 1);
}

/* "choose pitch" (IPC $0415): pitch 2 when the step is negative */
static uint8_t start_pitch(void)
{
    return (b.stepwrap & 0x80) ? b.p2 : b.p1;
}

/* The timer's interrupt with the sound on: the cycles to the pin's edge
 * (from the overflow) and after it to the timer's restart
 */
static void interrupt(int *to_edge, int *after)
{
    int c = LATENCY + 22 + 8;                   /* to $0100, its checks, $0327 */
    c += 5;                                     /* the duration's low byte */
    if (!(b.duration & 0xFF)) {
        c += 4;                                 /* and its high byte */
    }
    uint8_t h = b.half ? b.half : 0xFF;
    int over = 4;
    if (b.duration) {
        over += 2;
        c += 25 + (b.half ? 0 : 2) + 20;
        b.dcount = (uint16_t)(b.dcount + h + over);
        c += cmp_cost(b.dcount, b.duration);
        if (b.dcount >= b.duration) {
            b.on = false;                       /* $22 bit 5: the main loop ends it */
            c += 6;
        }
    }
    c += 21 + (b.half ? 0 : 2) + 20;
    b.icount = (uint16_t)(b.icount + h + over);
    c += cmp_cost(b.icount, b.interval);
    if (b.icount >= b.interval) {
        /* a step in pitch */
        b.icount = 0;
        int step = (b.stepwrap >> 4) & 0x0F;
        if (step & 8) {
            step -= 16;
        }
        c += 31 + (step >= 0 ? 2 : 0) + 8;
        b.pitch = (uint8_t)(b.pitch + step);
        bool out = b.p1 >= b.pitch;
        if (!out) {
            /* pitch 2 against it: equal leaves at the JZ, before the JC */
            c += b.p2 == b.pitch ? 14 : 16;
            out = b.p2 <= b.pitch;
        }
        if (out) {
            int wrap = b.stepwrap & 0x0F;
            bool again;
            c += 14;
            if (wrap == 15) {
                again = true;
            } else {
                c += 5;
                if (wrap == 0) {
                    again = false;
                } else {
                    c += 5;
                    again = b.wraps < wrap;
                }
            }
            if (again) {
                b.wraps++;
                b.pitch = start_pitch();
                c += 5 + ((b.stepwrap & 0x80) ? 13 : 14);   /* a negative step skips a DEC */
            } else {
                /* the step turns round, the pitch stays where it went */
                b.wraps = 0;
                b.stepwrap = (uint8_t)((((-step) & 0x0F) << 4) | (b.stepwrap & 0x0F));
                c += 17;
            }
        }
        c += 17;
        b.base = (uint8_t)(b.pitch + rnd(b.randfuzz >> 4, &c));
    }
    c += 9;                                     /* to the pin */
    *to_edge = c;
    int a = 6 + 14;                             /* the toggle's rest, the timer's reload */
    b.half = (uint8_t)(b.base + rnd(b.randfuzz & 0x0F, &a));
    *after = a;
}

/* The timer's overflow at s_next: the edge it makes and the next overflow */
static void overflow(void)
{
    int to_edge = 0, after = 6 + 14;
    if (s_starting) {
        /* the sound starts: the pin high, the timer loaded (IPC $004A, $03F3, $0400) */
        s_starting = false;
        s_edge_level = true;
        b.half = (uint8_t)(b.base + rnd(b.randfuzz & 0x0F, &after));
    } else if (!b.on) {
        /* the sound is off: the pin low, the timer back to the serial side */
        to_edge = LATENCY + 22 + 8 + 10;
        s_edge_level = false;
        b.running = false;
    } else {
        interrupt(&to_edge, &after);
        s_edge_level = !b.pin;
    }
    s_edge_at = s_next + (uint64_t)to_edge * UNITS_PER_CYCLE;
    s_edge_pending = true;
    uint32_t ticks = b.half ? b.half : 256;
    s_next = s_edge_at + ((uint64_t)after + 32 * ticks) * UNITS_PER_CYCLE;
}

/* The pin's time high from s_now up to `until` (units) */
static uint32_t run_until(uint64_t until)
{
    uint32_t high = 0;
    uint64_t t = s_now;
    for (;;) {
        if (s_edge_pending) {
            if (s_edge_at >= until) {
                break;
            }
            if (s_edge_at > t) {
                high += b.pin ? (uint32_t)(s_edge_at - t) : 0;
                t = s_edge_at;
            }
            b.pin = s_edge_level;
            s_edge_pending = false;
            continue;
        }
        if (!b.running || s_next >= until) {
            break;
        }
        overflow();
    }
    if (until > t) {
        high += b.pin ? (uint32_t)(until - t) : 0;
    }
    s_now = until;
    return high;
}

/* ---- The IPC's commands, the samples ---- */

void ql_beep_reset(void)
{
    memset(&b, 0, sizeof(b));
    s_starting = s_edge_pending = false;
    s_x = s_y = 0;
}

bool ql_debug_beep;                     /* the host's QL_BEEP_LOG: each command */

/* INSO (command 10): the eight bytes as the IPC stores them at $3A */
void ql_beep_start(const uint8_t p[8])
{
#ifndef ESP_PLATFORM
    if (ql_debug_beep) {
        printf("beep: frame %lu: INSO %02x %02x %02x %02x %02x %02x %02x %02x\n", (unsigned long)ql.frames, p[0], p[1],
               p[2], p[3], p[4], p[5], p[6], p[7]);
    }
#endif
    b.p1 = p[0];
    b.p2 = p[1];
    b.interval = p[2] | (p[3] << 8);
    b.duration = p[4] | (p[5] << 8);
    b.stepwrap = p[6];
    b.randfuzz = p[7];
    b.pitch = start_pitch();
    b.base = b.half = b.pitch;
    b.icount = b.dcount = 0;
    b.wraps = 0;
    b.on = true;
    if (!b.running) {
        /* the timer's next interrupt sees the sound on and starts it */
        uint64_t now = ql_clock() * UNITS_PER_CLOCK;
        b.running = true;
        s_starting = true;
        s_next = now > s_now ? now : s_now;
    }
}

/* KISO (command 11): off at the next interrupt */
void ql_beep_stop(void)
{
#ifndef ESP_PLATFORM
    if (ql_debug_beep) {
        printf("beep: frame %lu: KISO\n", (unsigned long)ql.frames);
    }
#endif
    b.on = false;
}

/* IPCSTAT bit 1, as command 1 reports it */
bool ql_beep_on(void)
{
    return b.on;
}

/* One sample up to `end` (68008 clocks): the pin's average through the
 * speaker's coupling (y = x - x' + 0.996 y')
 */
int16_t ql_beep_sample(uint64_t end)
{
    uint64_t until = end * UNITS_PER_CLOCK;
    if (until <= s_now) {
        return (int16_t)s_y;
    }
    if (!b.running && !s_edge_pending && !b.pin && s_x == 0 && s_y == 0) {
        s_now = until;                          /* silence */
        return 0;
    }
    uint64_t span = until - s_now;
    uint32_t high = run_until(until);
    int32_t x = span == SAMPLE_UNITS ? (int32_t)(high * AMPLITUDE / SAMPLE_UNITS)
                                     : (int32_t)((uint64_t)high * AMPLITUDE / span);
    s_y = x - s_x + s_y * 32637 / 32768;        /* (towards zero: a shift would stick below 0) */
    s_x = x;
    if (!b.running && !s_edge_pending && x == 0 && s_y > -8 && s_y < 8) {
        s_y = 0;                                /* settled */
    }
    return (int16_t)s_y;
}

#ifndef ESP_PLATFORM
/* The host's check against the IPC's own code: n edges of a sound alone,
 * in machine cycles from the first; the random generator's state given
 */
int ql_beep_edges(const uint8_t p[8], uint8_t r4, uint16_t seed, uint32_t *cycles, int n)
{
    ql_beep_reset();
    b.r4 = r4;
    b.rnd = seed;
    b.p1 = p[0];
    b.p2 = p[1];
    b.interval = p[2] | (p[3] << 8);
    b.duration = p[4] | (p[5] << 8);
    b.stepwrap = p[6];
    b.randfuzz = p[7];
    b.pitch = start_pitch();
    b.base = b.half = b.pitch;
    b.on = b.running = s_starting = true;
    s_next = 0;
    int got = 0;
    while (got < n && b.running) {
        overflow();
        cycles[got++] = (uint32_t)(s_edge_at / UNITS_PER_CYCLE);
        b.pin = s_edge_level;
        s_edge_pending = false;
    }
    for (int i = got - 1; i >= 0; i--) {
        cycles[i] -= cycles[0];
    }
    return got;
}
#endif
