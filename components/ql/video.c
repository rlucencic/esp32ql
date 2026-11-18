/*
 * ZX8301: the picture from screen RAM (32 KB at $20000, or $28000 with MODE
 * bit 7), 128 bytes a line, 256 lines.
 *
 *   Mode 4 (MODE bit 3 clear), 512 x 256, four colours: each word is 8
 *   pixels, the even byte their green bits, the odd byte their red bits
 *   (bit 7 the leftmost): black, red, green, white.
 *   Mode 8 (bit 3 set), 256 x 256, eight colours: each word is 4 pixels,
 *   the even byte G F pairs, the odd byte R B pairs (bits 7-6 the
 *   leftmost); colour G R B: black, blue, red, magenta, green, cyan, yellow,
 *   white. F toggles flashing from that pixel on (to the line's end): while
 *   it flashes, its pixels show the colour of the pixel that started it
 *   during the flash's off half.
 *   MODE bit 1 blanks the picture.
 *
 * Each frame that changed the displayed screen (or MODE) is copied into one
 * of three snapshots at its end: the presenter (another task) draws the
 * latest finished one while the CPU writes the next.
 */
#include <stdlib.h>
#include <string.h>
#include "ql_internal.h"

#define LINE_BYTES      128
#define SCREEN_BYTES    (LINE_BYTES * QL_SCREEN_HEIGHT)
#define SNAPSHOTS       3
#define FLASH_FRAMES    32              /* half a flash period */

typedef struct {
    uint8_t mode;
    bool flash_off;                     /* the flash's off half */
    uint8_t bytes[SCREEN_BYTES];
} snapshot_t;

static snapshot_t *s_snap;
static volatile int s_latest;           /* the finished snapshot to draw */
static volatile uint32_t s_signature;
static bool s_mode_changed;
static bool s_flash_off;
static uint32_t s_flash_frames;

static const uint16_t colour4[4] = { 0x0000, 0xF800, 0x07E0, 0xFFFF };
static const uint16_t colour8[8] = { 0x0000, 0x001F, 0xF800, 0xF81F, 0x07E0, 0x07FF, 0xFFE0, 0xFFFF };

void ql_video_init(void)
{
    if (s_snap == NULL) {
        s_snap = calloc(SNAPSHOTS, sizeof(snapshot_t));
    }
}

void ql_video_reset(void)
{
    ql.mode = 0;
    s_mode_changed = true;
    s_flash_off = false;
    s_flash_frames = 0;
}

void ql_video_write_mode(uint8_t v)
{
    if (v != ql.mode) {
        ql.mode = v;
        s_mode_changed = true;
    }
}

/* Any flash bit on the screen (mode 8): only then does the flash change it */
static bool any_flash(const uint8_t *screen)
{
    for (int i = 0; i < SCREEN_BYTES; i += 2) {
        if (screen[i] & 0x55) {
            return true;
        }
    }
    return false;
}

void ql_video_frame_end(void)
{
    bool flash_turned = false;
    if (++s_flash_frames >= FLASH_FRAMES) {
        s_flash_frames = 0;
        s_flash_off = !s_flash_off;
        flash_turned = (ql.mode & 0x08) != 0;
    }
    if (!ql.screen_dirty && !s_mode_changed && !flash_turned) {
        return;
    }
    if (s_snap == NULL) {
        return;
    }
    const uint8_t *screen = ql.ram + ((ql.mode & 0x80) ? 0x8000 : 0);
    if (flash_turned && !ql.screen_dirty && !s_mode_changed && !any_flash(screen)) {
        return;
    }
    int next = (s_latest + 1) % SNAPSHOTS;
    snapshot_t *s = &s_snap[next];
    s->mode = ql.mode;
    s->flash_off = s_flash_off;
    memcpy(s->bytes, screen, SCREEN_BYTES);
    s_latest = next;
    s_signature++;
    ql.screen_dirty = false;
    s_mode_changed = false;
}

uint32_t ql_screen_signature(void)
{
    return s_signature;
}

void ql_render_line_rgb565(int y, uint16_t *dst)
{
    if (s_snap == NULL || y < 0 || y >= QL_SCREEN_HEIGHT) {
        return;
    }
    const snapshot_t *s = &s_snap[s_latest];
    if (s->mode & 0x02) {
        memset(dst, 0, QL_SCREEN_WIDTH * sizeof(uint16_t));
        return;
    }
    const uint8_t *p = s->bytes + y * LINE_BYTES;
    if (!(s->mode & 0x08)) {
        for (int w = 0; w < LINE_BYTES; w += 2) {
            uint8_t g = p[w], r = p[w + 1];
            for (int bit = 7; bit >= 0; bit--) {
                *dst++ = colour4[(((g >> bit) & 1) << 1) | ((r >> bit) & 1)];
            }
        }
        return;
    }
    bool flashing = false;
    uint16_t flash_colour = 0;
    for (int w = 0; w < LINE_BYTES; w += 2) {
        uint8_t gf = p[w], rb = p[w + 1];
        for (int shift = 6; shift >= 0; shift -= 2) {
            int c = (((gf >> (shift + 1)) & 1) << 2) | (((rb >> (shift + 1)) & 1) << 1) | ((rb >> shift) & 1);
            uint16_t colour = colour8[c];
            if ((gf >> shift) & 1) {
                flashing = !flashing;
                flash_colour = colour;
            }
            uint16_t shown = flashing && s->flash_off ? flash_colour : colour;
            *dst++ = shown;
            *dst++ = shown;
        }
    }
}
