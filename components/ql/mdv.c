/*
 * The Microdrives: two drives on the ZX8302, cartridges as QLay images
 * (174930 bytes: 255 sectors of 686 bytes, docs/ql-hardware.md 6.6).
 *
 * The tape: each drive's loop holds the image's 255 sectors in file order,
 * each a slot of GAP_A byte-times of gap, the header record (the image's
 * $000-$01B: preamble 10 x $00 $FF $FF, then the 16-byte header), GAP_B of
 * gap and the block record ($01C-$235: preamble, file, block, checksum, the
 * data's preamble 6 x $00 $FF $FF, 512 bytes, checksum; then the image's
 * filler, to 624 bytes: FORMAT writes a long block of a preamble, 610 bytes
 * and a checksum, and its verify compares all 610). A byte-time is 40 us
 * (300 clocks); a slot 790 of them (31.6 ms, about the period of QDOS's
 * FORMAT, JS $5092-$50BA), a turn 8.1 s. The tape moves while the drive's motor
 * runs (it is selected), from the clock (ql_clock()).
 *
 * Reading: $18020 bit 3 (GAP) is set over gaps, and whenever no drive with
 * a cartridge turns. A write of $02 (read, the select clock high) arms the
 * receiver: it waits for the next $FF $FF in the record under the head (or
 * the next record), and from then each byte is ready ($18020 bit 2) once
 * the tape has carried it past; reads of $18022 or $18023 take them in
 * order. Writing: the bytes written to $18022 while $18020 bit 2 (write)
 * is set make a burst; when it ends the burst goes back into the image: a
 * short one starting with a header after its sync is the header of the
 * slot under the head (FORMAT), a long one the block of the slot whose
 * header passed last. Writes to a protected cartridge are dropped.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ql_internal.h"

#define SECTORS         255
#define SECTOR_BYTES    686
#define IMAGE_BYTES     (SECTORS * SECTOR_BYTES)    /* 174930 */
#define BYTE_CLOCKS     300                         /* 40 us */

#define GAP_A           69
#define HDR_LEN         28
#define GAP_B           69
#define BLK_LEN         624     /* the block, then the image's filler: FORMAT's long block */
#define SLOT            (GAP_A + HDR_LEN + GAP_B + BLK_LEN)
#define SHORT_TAPE      8       /* blank slots at the image's end that make it a shorter loop */
#define HDR_START       GAP_A
#define BLK_START       (GAP_A + HDR_LEN + GAP_B)
#define BURST_MAX       800

enum { GAP, HEADER, BLOCK };

typedef struct {
    uint8_t *image;
    char path[300];
    bool loaded, protect, dirty;
    bool turning;
    uint64_t pos;                       /* tape position (byte-times) at clock_ref */
    uint64_t clock_ref;
    uint64_t tape;                      /* the loop's length (byte-times) */
    uint32_t reads, writes;
} drive_t;

static drive_t s_drive[QL_MDV_DRIVES];
static uint8_t s_control;               /* the last write to $18020 */
static uint8_t s_select;                /* the select chain: bit n drive n + 1 */

/* The receiver */
static bool s_synced;
static int s_rx_drive;
static uint64_t s_rx_next, s_rx_end;    /* tape positions: the next byte, the record's end */
static uint64_t s_rx_start;             /* the record's first byte */
static int s_rx_slot, s_rx_kind;

/* The transmitter */
static bool s_writing;
static uint8_t s_burst[BURST_MAX];
static int s_burst_len, s_burst_drive;
static uint64_t s_burst_pos;
static uint64_t s_tx_until;

static int selected(void)
{
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        if (s_select & (1u << d)) {
            return d;
        }
    }
    return -1;
}

static uint64_t tape_pos(int d, uint64_t clock)
{
    drive_t *dr = &s_drive[d];
    return dr->turning ? dr->pos + (clock - dr->clock_ref) / BYTE_CLOCKS : dr->pos;
}

static const uint8_t *record(int d, int slot, int kind)
{
    return s_drive[d].image + slot * SECTOR_BYTES + (kind == HEADER ? 0 : HDR_LEN);
}

/* A record was written there: its preamble's sync is in the image. A blank
 * (never formatted) slot is erased tape: a gap all the way, as on a new
 * cartridge (QDOS then finds no sector and gives up, as with no cartridge)
 */
static bool recorded(int d, int slot, int kind)
{
    const uint8_t *rec = record(d, slot, kind);
    return rec[10] == 0xFF && rec[11] == 0xFF;
}

/* What passes the head of drive d at a tape position: GAP, HEADER or
 * BLOCK, its slot, the offset in the record and where the record starts
 */
static int element(int d, uint64_t pos, int *slot, int *offset, uint64_t *start)
{
    if (s_drive[d].tape == 0) {
        *slot = *offset = 0;
        *start = 0;
        return GAP;                             /* no cartridge yet */
    }
    uint64_t p = pos % s_drive[d].tape;
    *slot = p / SLOT;
    int o = p % SLOT;
    uint64_t slot_start = pos - o;
    if (o >= HDR_START && o < HDR_START + HDR_LEN && recorded(d, *slot, HEADER)) {
        *offset = o - HDR_START;
        *start = slot_start + HDR_START;
        return HEADER;
    }
    if (o >= BLK_START && o < BLK_START + BLK_LEN && recorded(d, *slot, BLOCK)) {
        *offset = o - BLK_START;
        *start = slot_start + BLK_START;
        return BLOCK;
    }
    *offset = 0;
    *start = 0;
    return GAP;
}

static int record_len(int kind)
{
    return kind == HEADER ? HDR_LEN : BLK_LEN;
}

/* The motors: the selected drive's runs, the others' stop where they are */
static void motors(uint64_t clock)
{
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        drive_t *dr = &s_drive[d];
        bool on = (s_select & (1u << d)) != 0;
        if (on != dr->turning) {
            dr->pos = tape_pos(d, clock);
            dr->clock_ref = clock;
            dr->turning = on;
        }
    }
}

bool ql_mdv_gap(uint64_t clock)
{
    int d = selected();
    if (d < 0 || !s_drive[d].loaded) {
        return true;
    }
    int slot, offset;
    uint64_t start;
    return element(d, tape_pos(d, clock), &slot, &offset, &start) == GAP;
}

/* Arm the receiver: find the next $FF $FF from the head on, in the record
 * under it or the next one
 */
static void arm(uint64_t clock)
{
    s_synced = false;
    int d = selected();
    if (d < 0 || !s_drive[d].loaded) {
        return;
    }
    uint64_t pos = tape_pos(d, clock);
    for (int tries = 0; tries < 2; tries++) {
        int slot, offset;
        uint64_t start;
        int kind = element(d, pos, &slot, &offset, &start);
        if (kind == GAP) {
            /* to the next record (within the next slot: erased tape has none) */
            uint64_t p = pos;
            int left = 2 * SLOT;
            do {
                p++;
                kind = element(d, p, &slot, &offset, &start);
            } while (kind == GAP && --left > 0);
            if (kind == GAP) {
                return;
            }
            pos = p;
        }
        const uint8_t *rec = record(d, slot, kind);
        int len = record_len(kind);
        for (int i = offset; i + 1 < len; i++) {
            if (rec[i] == 0xFF && rec[i + 1] == 0xFF) {
                s_synced = true;
                s_rx_drive = d;
                s_rx_slot = slot;
                s_rx_kind = kind;
                s_rx_start = start;
                s_rx_next = start + i + 2;
                s_rx_end = start + len;
                if (kind == BLOCK && i >= 20) {
                    s_drive[d].reads++;         /* a block's data (its own sync) */
                }
                return;
            }
        }
        pos = start + len;              /* none here: the next record */
    }
}

static void commit(void)
{
    s_writing = false;
    int d = s_burst_drive;
    if (d < 0 || !s_drive[d].loaded || s_drive[d].protect || s_burst_len == 0) {
        return;
    }
    /* the first sync */
    int i = 0;
    while (i + 1 < s_burst_len && !(s_burst[i] == 0xFF && s_burst[i + 1] == 0xFF)) {
        i++;
    }
    if (i + 1 >= s_burst_len) {
        return;
    }
    const uint8_t *after = s_burst + i + 2;
    int n = s_burst_len - i - 2;
    drive_t *dr = &s_drive[d];
    if (n >= 16 && n < 40 && after[0] == 0xFF) {
        /* a sector header (FORMAT): the slot under the head */
        int slot = (s_burst_pos % dr->tape) / SLOT;
        uint8_t *rec = dr->image + slot * SECTOR_BYTES;
        memset(rec, 0, 10);
        rec[10] = rec[11] = 0xFF;
        memcpy(rec + 12, after, 16);
    } else if (n >= 4) {
        /* a block: the slot whose header passed last */
        uint64_t p = s_burst_pos + dr->tape - (HDR_START + HDR_LEN);
        int slot = (p % dr->tape) / SLOT;
        uint8_t *rec = dr->image + slot * SECTOR_BYTES + HDR_LEN;
        memset(rec, 0, 10);
        rec[10] = rec[11] = 0xFF;
        memcpy(rec + 12, after, 4);
        /* the data: after its own sync */
        int k = 4;
        while (k + 1 < n && !(after[k] == 0xFF && after[k + 1] == 0xFF)) {
            k++;
        }
        memset(rec + 16, 0, 6);
        rec[22] = rec[23] = 0xFF;
        if (k + 1 < n) {
            /* the data, its checksum and what follows (FORMAT's long block
             * is 610 bytes; the image's filler takes the rest)
             */
            int m = n - k - 2, room = SECTOR_BYTES - HDR_LEN - 24;
            memcpy(rec + 24, after + k + 2, m < room ? m : room);
        }
    } else {
        return;
    }
    dr->dirty = true;
    dr->writes++;
}

/* $18020 written */
void ql_mdv_control(uint8_t v, uint64_t clock)
{
    uint8_t old = s_control;
    s_control = v;
    if ((old & 0x02) && !(v & 0x02)) {
        /* the select clock falls: drive 1 takes the select bit, each drive passes its own on */
        s_select = (s_select << 1) | (v & 1);
        motors(clock);
    }
    if (!(old & 0x04) && (v & 0x04)) {
        s_writing = true;
        s_burst_len = 0;
        s_burst_drive = selected();
        s_burst_pos = s_burst_drive >= 0 ? tape_pos(s_burst_drive, clock) : 0;
    } else if ((old & 0x04) && !(v & 0x04) && s_writing) {
        commit();
    }
    if ((v & 0x0E) == 0x02) {
        arm(clock);                     /* read: the receiver waits for a sync */
    }
}

/* $18020 bits 1-3: transmit buffer full, a byte to read, gap */
uint8_t ql_mdv_status(uint64_t clock)
{
    uint8_t v = 0;
    if (clock < s_tx_until) {
        v |= 0x02;
    }
    if (s_synced && s_rx_next < s_rx_end && tape_pos(s_rx_drive, clock) > s_rx_next) {
        v |= 0x04;
    }
    if (ql_mdv_gap(clock)) {
        v |= 0x08;
    }
    return v;
}

/* $18022 / $18023 read: the next byte */
uint8_t ql_mdv_read(uint64_t clock)
{
    (void)clock;
    if (!s_synced || s_rx_next >= s_rx_end) {
        return 0;
    }
    uint8_t b = record(s_rx_drive, s_rx_slot, s_rx_kind)[s_rx_next - s_rx_start];
    s_rx_next++;
    return b;
}

/* $18022 written in microdrive mode: a byte to the tape */
void ql_mdv_write(uint8_t v, uint64_t clock)
{
    s_tx_until = clock + BYTE_CLOCKS;
    if (s_writing && s_burst_len < BURST_MAX) {
        s_burst[s_burst_len++] = v;
    }
}

void ql_mdv_reset(void)
{
    uint64_t clock = ql_clock();
    s_select = 0;
    motors(clock);
    s_control = 0;
    s_synced = false;
    s_writing = false;
    s_tx_until = 0;
}

int ql_mdv_motor(void)
{
    int d = selected();
    return d >= 0 ? d + 1 : 0;
}

void ql_mdv_counts(uint32_t *reads, uint32_t *writes)
{
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        reads[d] = s_drive[d].reads;
        writes[d] = s_drive[d].writes;
    }
}

/* ---- Cartridges ---- */

/* The checksums of each slot's sector header and block header made right
 * (low byte first, $0F0F + the record's bytes). Images prepared for the
 * vDrive (the djw site's game cartridges) have them stale: the tool that
 * put the files on wrote each block's file and block numbers but kept the
 * free block's checksum, or renamed the medium without the headers'
 * checksums; the vDrive works them out as it plays, QDOS rejects them
 * here. The data's own checksums stay as they are (a damaged block stays
 * damaged). The image changes in memory only, until the cartridge is
 * written to.
 */
static int fix_checksums(uint8_t *img)
{
    int fixed = 0;
    for (int s = 0; s < SECTORS; s++) {
        uint8_t *r = img + s * SECTOR_BYTES;
        struct { int from, len; } rec[2] = { { 12, 14 }, { 40, 2 } };
        if (r[10] != 0xFF || r[11] != 0xFF || r[12] != 0xFF) {
            continue;                               /* not a formatted slot */
        }
        for (int k = 0; k < 2; k++) {
            if (k == 1 && (r[38] != 0xFF || r[39] != 0xFF)) {
                continue;                           /* no block header */
            }
            uint16_t sum = 0x0F0F;
            for (int i = 0; i < rec[k].len; i++) {
                sum += r[rec[k].from + i];
            }
            uint8_t *c = r + rec[k].from + rec[k].len;
            if (c[0] != (sum & 0xFF) || c[1] != (sum >> 8)) {
                c[0] = sum & 0xFF;
                c[1] = sum >> 8;
                fixed++;
            }
        }
    }
    return fixed;
}

bool ql_mdv_insert(int drive, const char *path)
{
    if (drive < 0 || drive >= QL_MDV_DRIVES) {
        return false;
    }
    drive_t *dr = &s_drive[drive];
    if (dr->image == NULL) {
        dr->image = malloc(IMAGE_BYTES);
        if (dr->image == NULL) {
            return false;
        }
    }
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    bool ok = size == IMAGE_BYTES && fread(dr->image, 1, IMAGE_BYTES, f) == IMAGE_BYTES;
    fclose(f);
    if (!ok) {
        QL_LOGW("%s: not a QLay cartridge image (%ld bytes, %d expected)", path, size, IMAGE_BYTES);
        dr->loaded = false;
        return false;
    }
    /* a file that can't be opened for writing is a protected cartridge */
    f = fopen(path, "r+b");
    dr->protect = f == NULL;
    if (f) {
        fclose(f);
    }
    snprintf(dr->path, sizeof(dr->path), "%s", path);
    dr->loaded = true;
    dr->dirty = false;
    /* A recorded image whose end is blank is a shorter tape (the QLay
     * format pads to 255 slots): the loop ends after its last recorded slot,
     * as a real cartridge of that many sectors does, rather than playing a
     * second of gap each turn (JS reads that as "bad or changed medium":
     * Cavern from the djw vDrive page, 215 sectors). A blank cartridge
     * stays a full loop of gap, for FORMAT.
     */
    int last = -1;
    for (int s = 0; s < SECTORS; s++) {
        const uint8_t *r = dr->image + s * SECTOR_BYTES;
        if (r[10] == 0xFF && r[11] == 0xFF && r[12] == 0xFF) {
            last = s;
        }
    }
    int slots = last >= 0 && SECTORS - 1 - last >= SHORT_TAPE ? last + 1 : SECTORS;
    dr->tape = (uint64_t)slots * SLOT;
    if (slots < SECTORS) {
        QL_LOGI("%s: a tape of %d slots (the rest of the image is blank)", path, slots);
    }
    int fixed = fix_checksums(dr->image);
    if (fixed) {
        QL_LOGI("%s: %d header checksums put right (an image made for the vDrive?)", path, fixed);
    }
    return true;
}

void ql_mdv_eject(int drive)
{
    if (drive < 0 || drive >= QL_MDV_DRIVES) {
        return;
    }
    if (s_drive[drive].dirty) {
        ql_mdv_flush();
    }
    s_drive[drive].loaded = false;
    s_drive[drive].path[0] = 0;
}

const char *ql_mdv_path(int drive)
{
    return drive >= 0 && drive < QL_MDV_DRIVES && s_drive[drive].loaded ? s_drive[drive].path : NULL;
}

bool ql_mdv_write_protected(int drive)
{
    return drive >= 0 && drive < QL_MDV_DRIVES && s_drive[drive].loaded && s_drive[drive].protect;
}

/* A blank cartridge: unformatted tape (no records QDOS can sync on) */
bool ql_mdv_create(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    static const uint8_t zeros[SECTOR_BYTES];
    bool ok = true;
    for (int s = 0; s < SECTORS && ok; s++) {
        ok = fwrite(zeros, 1, SECTOR_BYTES, f) == SECTOR_BYTES;
    }
    fclose(f);
    return ok;
}

bool ql_mdv_dirty(void)
{
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        if (s_drive[d].loaded && s_drive[d].dirty) {
            return true;
        }
    }
    return false;
}

void ql_mdv_flush(void)
{
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        drive_t *dr = &s_drive[d];
        if (!dr->loaded || !dr->dirty) {
            continue;
        }
        FILE *f = fopen(dr->path, "r+b");
        if (f == NULL) {
            QL_LOGW("%s: can't write the cartridge back", dr->path);
            continue;
        }
        if (fwrite(dr->image, 1, IMAGE_BYTES, f) == IMAGE_BYTES) {
            dr->dirty = false;
        }
        fclose(f);
    }
}
