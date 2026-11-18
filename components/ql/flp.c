/*
 * The Miracle Trump Card's floppy side (as MAME's ql/trumpcard.cpp maps
 * it): a WD1772 at $1C000-$1C003 (status/command, track, sector, data) and
 * a write-only latch at $1E000: bit 1 drive 1 (FLP1_), bit 0 drive 2, bit 2
 * motor, bit 3 side. No interrupts: the card's driver polls the status
 * register (busy, DRQ).
 *
 * Disks are raw sector images of QDOS's 720 KB format (80 tracks, 2 sides,
 * 9 sectors of 512 bytes; 40-track and 10-sector images by their size),
 * track by track, side 0 then 1; kept in memory, written back to their file
 * when the board flushes.
 *
 * The WD1772 runs on its own clock, as the real one does: a sector is
 * found a millisecond after the command, then a byte passes the head every
 * 32 us (DRQ); one the CPU hasn't taken (or, writing, given) by the next is
 * lost data. The card's driver relies on that: its Read Address takes the
 * track from the sector register once the command ends, never reading the
 * bytes. Steps take 3 ms. Write Track (FORMAT) takes 6250 bytes, a turn,
 * and stores the sectors whose ID and data fields it finds in them ($F5 a
 * sync, $FE an ID mark, $FB a data mark). The motor runs from a command
 * until ten turns after it.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ql_internal.h"

#define SECTOR          512
#define MAX_TRACKS      84              /* where the head stops */
#define MAX_IMAGE       (80 * 2 * 10 * SECTOR)
#define TRACK_BYTES     6250            /* a DD track at 250 kbit/s */
#define BYTE_CLOCKS     240             /* 32 us */
#define FIND_CLOCKS     7500            /* 1 ms to the sector */
#define STEP_CLOCKS     22500           /* 3 ms a step */
#define REVOLUTION      1500000         /* 300 rpm */
#define NOT_FOUND       (5 * REVOLUTION)
#define INDEX_CLOCKS    30000           /* the index pulse, 4 ms */

/* Status bits */
#define S_BUSY          0x01
#define S_DRQ           0x02            /* type I: index */
#define S_INDEX         0x02
#define S_LOST          0x04            /* type I: track 0 */
#define S_TRACK0        0x04
#define S_CRC           0x08
#define S_RNF           0x10
#define S_SPUNUP        0x20            /* type II/III: record type */
#define S_WPROT         0x40
#define S_MOTOR         0x80

enum { IDLE, READ, WRITE, READ_ADDRESS, WRITE_TRACK };

typedef struct {
    uint8_t *image;
    char path[300];
    bool loaded, protect, dirty;
    int tracks, spt;
    int cyl;                            /* the head's track */
    /* The track number in each track's IDs, as a Write Track gave them
     * (-1: the track's own). FORMAT tells one side from two by writing
     * side 1 of track 0 as track 1, then side 0, then reading an ID on side 1.
     */
    int8_t id_track[MAX_TRACKS][2];
    uint32_t reads, writes;
} drive_t;

static drive_t s_drive[QL_FLP_DRIVES];
static uint8_t s_latch;

/* The WD1772 */
static uint8_t s_status, s_track, s_sector, s_data;
static int s_type;                      /* the last command's type: 1-4 */
static int s_op;                        /* IDLE: waiting for the end */
static uint8_t s_end_status;            /* IDLE: the status bits it ends with */
static bool s_multi;
static uint64_t s_ready;                /* when the next byte (or the end) is due */
static uint64_t s_motor_off;            /* when the motor stops */
static int s_pos, s_len;
static uint8_t s_buf[TRACK_BYTES];
static int s_step_dir = 1;
static int s_next_id;                   /* read address: the next sector to pass */

bool ql_debug_flp;                      /* the host's QL_FLP_LOG: each command */

static int selected(void)
{
    return (s_latch & 0x02) ? 0 : (s_latch & 0x01) ? 1 : -1;
}

/* The selected drive, NULL if none (its head and track 0 sensor work
 * without a disk)
 */
static drive_t *unit(void)
{
    int d = selected();
    return d >= 0 ? &s_drive[d] : NULL;
}

/* The selected drive with a disk in, NULL if none */
static drive_t *drive(void)
{
    drive_t *dr = unit();
    return dr && dr->loaded ? dr : NULL;
}

static int side(void)
{
    return (s_latch >> 3) & 1;
}

static uint8_t *sector_at(drive_t *dr, int cyl, int head, int sector)
{
    if (dr == NULL || cyl < 0 || cyl >= dr->tracks || sector < 1 || sector > dr->spt) {
        return NULL;
    }
    return dr->image + ((uint32_t)(cyl * 2 + head) * dr->spt + (sector - 1)) * SECTOR;
}

/* The track number in the IDs under the head */
static int id_track(drive_t *dr)
{
    int t = dr->id_track[dr->cyl][side()];
    return t >= 0 ? t : dr->cyl;
}

/* The sector the registers name under the head, NULL if there's none */
static uint8_t *this_sector(void)
{
    drive_t *dr = drive();
    return dr && id_track(dr) == s_track ? sector_at(dr, dr->cyl, side(), s_sector) : NULL;
}

static uint16_t crc16(uint16_t crc, uint8_t v)
{
    crc ^= v << 8;
    for (int i = 0; i < 8; i++) {
        crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    return crc;
}

static void finish(uint8_t extra)
{
    s_status = (s_status & ~(S_BUSY | S_DRQ)) | extra;
    s_op = IDLE;
    s_motor_off = ql_clock() + 10ull * REVOLUTION;
}

/* A command that ends at `at` with these status bits */
static void end_at(uint64_t at, uint8_t status)
{
    s_op = IDLE;
    s_end_status = status;
    s_ready = at;
}

/* Type I: restore, seek, step */
static void type1(uint8_t cmd, uint64_t clock)
{
    s_type = 1;
    drive_t *dr = unit();
    int steps = 0;
    if ((cmd & 0xF0) == 0x00) {                 /* restore */
        steps = dr ? dr->cyl : 0;
        if (dr) {
            dr->cyl = 0;
        }
        s_track = 0;
    } else if ((cmd & 0xF0) == 0x10) {          /* seek to the data register's track */
        int to = s_data;
        steps = abs(to - s_track);
        if (dr) {
            dr->cyl += to - s_track;
            dr->cyl = dr->cyl < 0 ? 0 : dr->cyl >= MAX_TRACKS ? MAX_TRACKS - 1 : dr->cyl;
        }
        s_track = to;
    } else {
        if ((cmd & 0xE0) == 0x40) {
            s_step_dir = 1;                     /* step in */
        } else if ((cmd & 0xE0) == 0x60) {
            s_step_dir = -1;                    /* step out */
        }
        steps = 1;
        if (dr) {
            dr->cyl += s_step_dir;
            dr->cyl = dr->cyl < 0 ? 0 : dr->cyl >= MAX_TRACKS ? MAX_TRACKS - 1 : dr->cyl;
        }
        if (cmd & 0x10) {                       /* update the track register */
            s_track += s_step_dir;
        }
    }
    s_status = S_BUSY | S_SPUNUP;
    uint8_t end = 0;
    if ((cmd & 0x04) && (drive() == NULL || id_track(dr) != s_track || dr->cyl >= dr->tracks)) {
        end = S_RNF;                            /* verify: no ID with the register's track */
    }
    end_at(clock + (uint64_t)(steps ? steps : 1) * STEP_CLOCKS + (end ? NOT_FOUND : 0), end);
}

/* Type II: read and write sector */
static void type2(uint8_t cmd, uint64_t clock)
{
    s_type = 2;
    s_multi = (cmd & 0x10) != 0;
    s_status = S_BUSY;
    drive_t *dr = drive();
    bool write = (cmd & 0x20) != 0;
    if (write && dr && dr->protect) {
        finish(S_WPROT);
        return;
    }
    uint8_t *p = this_sector();
    if (p == NULL) {
        end_at(clock + NOT_FOUND, S_RNF);
        return;
    }
    s_pos = 0;
    s_len = SECTOR;
    if (write) {
        s_op = WRITE;
        s_status |= S_DRQ;                      /* the first byte: until the data field */
    } else {
        s_op = READ;
        memcpy(s_buf, p, SECTOR);
    }
    s_ready = clock + FIND_CLOCKS;
}

/* Type III: read address, write track (read track isn't here) */
static void type3(uint8_t cmd, uint64_t clock)
{
    s_type = 3;
    s_status = S_BUSY;
    drive_t *dr = drive();
    s_pos = 0;
    if ((cmd & 0xF0) == 0xC0) {                 /* read address: the next ID to pass */
        if (dr == NULL || dr->cyl >= dr->tracks) {
            end_at(clock + NOT_FOUND, S_RNF);
            return;
        }
        uint8_t id[4] = { id_track(dr), side(), 1 + (s_next_id++ % dr->spt), 2 };
        uint16_t crc = 0xFFFF;
        crc = crc16(crc16(crc16(crc16(crc, 0xA1), 0xA1), 0xA1), 0xFE);
        for (int i = 0; i < 4; i++) {
            s_buf[i] = id[i];
            crc = crc16(crc, id[i]);
        }
        s_buf[4] = crc >> 8;
        s_buf[5] = crc;
        s_len = 6;
        s_op = READ_ADDRESS;
        s_ready = clock + FIND_CLOCKS;
    } else if ((cmd & 0xF0) == 0xF0) {          /* write track, from the index */
        if (dr == NULL || dr->protect) {
            finish(dr ? S_WPROT : S_RNF);
            return;
        }
        s_len = TRACK_BYTES;
        s_op = WRITE_TRACK;
        s_status |= S_DRQ;
        s_ready = clock + FIND_CLOCKS;
    } else {
        end_at(clock + NOT_FOUND, S_RNF);
    }
}

/* The sectors a written track holds into the image */
static void store_track(void)
{
    drive_t *dr = drive();
    if (dr == NULL) {
        return;
    }
    int r = -1, n = 2;
    for (int i = 0; i + 1 < s_len; i++) {
        if (s_buf[i] == 0xF5 && s_buf[i + 1] == 0xFE && i + 5 < s_len) {
            r = s_buf[i + 4];                   /* C H R N */
            n = s_buf[i + 5] & 3;
            dr->id_track[dr->cyl][side()] = s_buf[i + 2] == dr->cyl ? -1 : s_buf[i + 2];
#ifndef ESP_PLATFORM
            if (ql_debug_flp) {
                printf("flp: ID %02x %02x %02x %02x at %d (head %d side %d)\n", s_buf[i + 2], s_buf[i + 3], s_buf[i + 4],
                       s_buf[i + 5], i, dr->cyl, side());
            }
#endif
            i += 5;
        } else if (s_buf[i] == 0xF5 && s_buf[i + 1] == 0xFB && r >= 0) {
            int size = 128 << n;
            uint8_t *p = sector_at(dr, dr->cyl, side(), r);
            if (p && size == SECTOR && i + 2 + size <= s_len) {
                memcpy(p, s_buf + i + 2, SECTOR);
                dr->dirty = true;
            }
            i += 1 + size;
            r = -1;
        }
    }
    dr->writes++;
}

/* A sector read: the next one (multi-sector), or the end */
static void read_done(void)
{
    drive_t *dr = drive();
    if (dr) {
        dr->reads++;
    }
    if (s_op == READ_ADDRESS) {
        s_sector = s_buf[0];                    /* the ID's track goes there */
        finish(0);
    } else if (s_multi) {
        s_sector++;
        uint8_t *p = this_sector();
        if (p == NULL) {
            end_at(s_ready + NOT_FOUND, S_RNF);
            return;
        }
        memcpy(s_buf, p, SECTOR);
        s_pos = 0;
        s_ready += FIND_CLOCKS;
    } else {
        finish(0);
    }
}

/* A sector written: into the image, then the next one or the end */
static void write_done(void)
{
    drive_t *dr = drive();
    if (s_op == WRITE_TRACK) {
        store_track();
        finish(0);
        return;
    }
    uint8_t *p = this_sector();
    if (p) {
        memcpy(p, s_buf, SECTOR);
        dr->dirty = true;
        dr->writes++;
    }
    if (s_multi) {
        s_sector++;
        if (this_sector() == NULL) {
            end_at(s_ready + NOT_FOUND, S_RNF);
            return;
        }
        s_pos = 0;
        s_status |= S_DRQ;
        s_ready += FIND_CLOCKS;
    } else {
        finish(0);
    }
}

/* Bring the controller up to the clock: the bytes that passed, the end */
static void update(uint64_t clock)
{
    while ((s_status & S_BUSY) && clock >= s_ready) {
        switch (s_op) {
        case IDLE:
            finish(s_end_status);
            return;
        case READ:
        case READ_ADDRESS:
            if (s_pos < s_len) {
                if (s_status & S_DRQ) {
                    s_status |= S_LOST;         /* the last byte not taken */
                }
                s_data = s_buf[s_pos++];
                s_status |= S_DRQ;
                /* after the last byte its CRC passes, then the end */
                s_ready += s_pos < s_len ? BYTE_CLOCKS : 3 * BYTE_CLOCKS;
            } else {
                read_done();
            }
            break;
        case WRITE:
        case WRITE_TRACK:
            /* the byte going onto the disk now: the CPU's, or lost */
            if (s_status & S_DRQ) {
                s_status = (s_status & ~S_DRQ) | S_LOST;
                s_buf[s_pos] = 0;
            }
            if (++s_pos >= s_len) {
                write_done();
            } else {
                s_status |= S_DRQ;
                s_ready += BYTE_CLOCKS;
            }
            break;
        }
    }
}

uint8_t ql_flp_read8(uint32_t a)
{
    if (a < 0x1C000 || a > 0x1C003) {
        return 0;
    }
    uint64_t clock = ql_clock();
    update(clock);
    switch (a & 3) {
    case 0: {
        uint8_t v = s_status;
        drive_t *dr = unit();
        if ((s_status & S_BUSY) || clock < s_motor_off) {
            v |= S_MOTOR;
        }
        if (s_type == 1) {
            v &= ~(S_INDEX | S_TRACK0 | S_WPROT);
            if (dr) {
                v |= dr->cyl == 0 ? S_TRACK0 : 0;
                /* no disk: no index pulses, and the sensor sees no tab */
                v |= !dr->loaded || dr->protect ? S_WPROT : 0;
                v |= dr->loaded && (clock % REVOLUTION) < INDEX_CLOCKS ? S_INDEX : 0;
            }
        }
        return v;
    }
    case 1:
        return s_track;
    case 2:
        return s_sector;
    default:
        s_status &= ~S_DRQ;
        return s_data;
    }
}

void ql_flp_write8(uint32_t a, uint8_t v)
{
    uint64_t clock = ql_clock();
    if (a == 0x1E000) {
        s_latch = v;
        return;
    }
    if (a < 0x1C000 || a > 0x1C003) {
        return;
    }
    update(clock);
    switch (a & 3) {
    case 0:
#ifndef ESP_PLATFORM
        if (ql_debug_flp) {
            printf("flp: frame %lu: command %02x, latch %02x, track %d sector %d data %d, head %d, status %02x\n",
                   (unsigned long)ql.frames, v, s_latch, s_track, s_sector, s_data,
                   drive() ? drive()->cyl : -1, s_status);
        }
#endif
        if ((v & 0xF0) == 0xD0) {               /* force interrupt */
            if (!(s_status & S_BUSY)) {
                s_type = 1;                     /* idle: type I status */
            }
            finish(0);
        } else if (s_status & S_BUSY) {
            /* ignored while busy */
        } else if (!(v & 0x80)) {
            type1(v, clock);
        } else if (!(v & 0x40)) {
            type2(v, clock);
        } else {
            type3(v, clock);
        }
        break;
    case 1:
        s_track = v;
        break;
    case 2:
        s_sector = v;
        break;
    default:
        s_data = v;
        if ((s_op == WRITE || s_op == WRITE_TRACK) && (s_status & S_BUSY) && s_pos < s_len) {
            s_buf[s_pos] = v;
            s_status &= ~S_DRQ;
        }
        break;
    }
}

void ql_flp_reset(void)
{
    s_latch = 0;
    s_status = 0;
    s_op = IDLE;
    s_track = s_sector = s_data = 0;
    s_type = 1;
}

void ql_flp_status(uint8_t *light, uint8_t *track, uint32_t *reads, uint32_t *writes)
{
    int d = selected();
    *light = (d >= 0 && (s_status & S_BUSY)) ? d + 1 : 0;
    for (int i = 0; i < QL_FLP_DRIVES; i++) {
        track[i] = s_drive[i].cyl;
        reads[i] = s_drive[i].reads;
        writes[i] = s_drive[i].writes;
    }
}

/* ---- Disk images ---- */

bool ql_flp_insert(int drive_no, const char *path)
{
    if (drive_no < 0 || drive_no >= QL_FLP_DRIVES) {
        return false;
    }
    drive_t *dr = &s_drive[drive_no];
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        return false;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    int tracks = 0, spt = 0;
    if (size == 80L * 2 * 9 * SECTOR) {
        tracks = 80, spt = 9;
    } else if (size == 80L * 2 * 10 * SECTOR) {
        tracks = 80, spt = 10;
    } else if (size == 40L * 2 * 9 * SECTOR) {
        tracks = 40, spt = 9;
    }
    if (tracks == 0) {
        fclose(f);
        QL_LOGW("%s: %ld bytes: not a 720 KB (or 360/800 KB) disk image", path, size);
        return false;
    }
    if (dr->image == NULL) {
        dr->image = malloc(MAX_IMAGE);
        if (dr->image == NULL) {
            fclose(f);
            return false;
        }
    }
    bool ok = fread(dr->image, 1, size, f) == (size_t)size;
    fclose(f);
    if (!ok) {
        return false;
    }
    f = fopen(path, "r+b");
    dr->protect = f == NULL;
    if (f) {
        fclose(f);
    }
    snprintf(dr->path, sizeof(dr->path), "%s", path);
    memset(dr->id_track, -1, sizeof(dr->id_track));
    dr->tracks = tracks;
    dr->spt = spt;
    dr->loaded = true;
    dr->dirty = false;
    return true;
}

void ql_flp_eject(int drive_no)
{
    if (drive_no < 0 || drive_no >= QL_FLP_DRIVES) {
        return;
    }
    if (s_drive[drive_no].dirty) {
        ql_flp_flush();
    }
    s_drive[drive_no].loaded = false;
    s_drive[drive_no].path[0] = 0;
}

const char *ql_flp_path(int drive_no)
{
    return drive_no >= 0 && drive_no < QL_FLP_DRIVES && s_drive[drive_no].loaded ? s_drive[drive_no].path : NULL;
}

bool ql_flp_write_protected(int drive_no)
{
    return drive_no >= 0 && drive_no < QL_FLP_DRIVES && s_drive[drive_no].loaded && s_drive[drive_no].protect;
}

/* A blank 720 KB disk: zeros, for FORMAT */
bool ql_flp_create(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        return false;
    }
    static const uint8_t zeros[SECTOR];
    bool ok = true;
    for (int s = 0; s < 80 * 2 * 9 && ok; s++) {
        ok = fwrite(zeros, 1, SECTOR, f) == SECTOR;
    }
    fclose(f);
    return ok;
}

bool ql_flp_dirty(void)
{
    for (int d = 0; d < QL_FLP_DRIVES; d++) {
        if (s_drive[d].loaded && s_drive[d].dirty) {
            return true;
        }
    }
    return false;
}

void ql_flp_flush(void)
{
    for (int d = 0; d < QL_FLP_DRIVES; d++) {
        drive_t *dr = &s_drive[d];
        if (!dr->loaded || !dr->dirty) {
            continue;
        }
        FILE *f = fopen(dr->path, "r+b");
        if (f == NULL) {
            QL_LOGW("%s: can't write the disk back", dr->path);
            continue;
        }
        size_t size = (size_t)dr->tracks * 2 * dr->spt * SECTOR;
        if (fwrite(dr->image, 1, size, f) == size) {
            dr->dirty = false;
        }
        fclose(f);
    }
}
