/*
 * Sound: 16-bit stereo at QL_AUDIO_HZ, two samples each line (15625 lines a
 * second), into a ring the board's speaker task drains (which paces the
 * emulation: the runtime waits while two frames are queued). The samples
 * are the IPC's beeper (beep.c), the line's two halves.
 */
#include <string.h>
#include "ql_internal.h"

#define RING            8192            /* stereo frames, a power of two */

static int16_t s_ring[RING][2];
static volatile uint32_t s_head, s_tail;

/* At the line's end (ql.line already the next one's) */
void ql_sound_line(void)
{
    uint64_t end = ql.frame_clock + (uint64_t)ql.line * QL_LINE_CYCLES;
    for (int i = 0; i < 2; i++) {
        int16_t level = ql_beep_sample(end - (1 - i) * (QL_LINE_CYCLES / 2));
        uint32_t head = s_head;
        if (head - s_tail >= RING) {
            continue;                   /* nobody drains it (the host without a WAV) */
        }
        s_ring[head % RING][0] = level;
        s_ring[head % RING][1] = level;
        s_head = head + 1;
    }
}

void ql_audio_enable(bool on)
{
    if (!on) {
        s_tail = s_head;
    }
}

int ql_audio_render(int16_t *dst, int frames)
{
    uint32_t tail = s_tail, n = s_head - tail;
    if (n > (uint32_t)frames) {
        n = frames;
    }
    for (uint32_t i = 0; i < n; i++) {
        dst[2 * i] = s_ring[(tail + i) % RING][0];
        dst[2 * i + 1] = s_ring[(tail + i) % RING][1];
    }
    s_tail = tail + n;
    return n;
}

int ql_audio_pending(void)
{
    return s_head - s_tail;
}
