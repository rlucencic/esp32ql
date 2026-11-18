/*
 * Sound: 16-bit stereo at QL_AUDIO_HZ, two samples each line (15625 lines a
 * second), into a ring the board's speaker task drains (which paces the
 * emulation: the runtime waits while two frames are queued). The IPC's
 * beeper sets the level (ql_sound_level()).
 */
#include <string.h>
#include "ql_internal.h"

#define RING            8192            /* stereo frames, a power of two */

static int16_t s_ring[RING][2];
static volatile uint32_t s_head, s_tail;
static int16_t s_level;

void ql_sound_reset(void)
{
    s_level = 0;
}

void ql_sound_level(int16_t level)
{
    s_level = level;
}

void ql_sound_line(void)
{
    for (int i = 0; i < 2; i++) {
        uint32_t head = s_head;
        if (head - s_tail >= RING) {
            return;                     /* nobody drains it (the host without a WAV) */
        }
        s_ring[head % RING][0] = s_level;
        s_ring[head % RING][1] = s_level;
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
