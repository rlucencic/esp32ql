#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_partition.h"
#include "emu/sdcard.h"
#include "rom_store.h"

#define ROM_PARTITION_TYPE  0x40
#define ROM_FOLDER          SDCARD_MOUNT_POINT "/ql/roms"
#define SLOT                65536

static const char *TAG = "rom";

static const struct {
    const char *name;
    uint32_t size, min;                 /* min: a shorter file isn't this ROM */
} roms[ROM_COUNT] = {
    [ROM_JS] = { "js.rom", 48 * 1024, 32 * 1024 },
    [ROM_MINERVA] = { "minerva.rom", 48 * 1024, 32 * 1024 },
    [ROM_TRUMP] = { "trumpcard.rom", 32 * 1024, 16 * 1024 },
};

const char *rom_store_name(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].name : "";
}

uint32_t rom_store_size(rom_id_t id)
{
    return id < ROM_COUNT ? roms[id].size : 0;
}

/* A system ROM: a 68000 reset vector with the PC inside it; an expansion
 * ROM (the Trump Card's): QDOS's $4AFB0001 at its start
 */
static bool plausible(rom_id_t id, const uint8_t *p)
{
    if (id == ROM_TRUMP) {
        return p[0] == 0x4A && p[1] == 0xFB && p[2] == 0x00 && p[3] == 0x01;
    }
    uint32_t pc = ((uint32_t)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    bool ff = true, zero = true;
    for (int i = 0; i < 64; i++) {
        ff &= p[i] == 0xFF;
        zero &= p[i] == 0x00;
    }
    return !ff && !zero && pc >= 8 && pc < roms[id].size && (pc & 1) == 0;
}

/* Through a mapping, not esp_partition_read(): that turns the caches off
 * while it reads, which stalls the LCD's refill (picture artefacts)
 */
static bool from_partition(rom_id_t id, uint8_t *buf)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ROM_PARTITION_TYPE,
                                                           "ql_roms");
    uint32_t offset = id * SLOT, size = roms[id].size;
    if (part == NULL || offset + size > part->size) {
        return false;
    }
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, offset, size, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        return false;
    }
    memcpy(buf, map, size);
    esp_partition_munmap(handle);
    return true;
}

/* ql/roms/<name> on the SD card (any case) */
static bool from_sd(rom_id_t id, uint8_t *buf)
{
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, roms[id].name);
    FILE *f = fopen(path, "rb");
    if (f == NULL) {
        for (char *p = path + strlen(ROM_FOLDER) + 1; *p; p++) {
            *p = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        }
        f = fopen(path, "rb");
    }
    if (f == NULL) {
        return false;
    }
    memset(buf, 0xFF, roms[id].size);
    size_t n = fread(buf, 1, roms[id].size, f);
    fclose(f);
    if (n < roms[id].min) {
        return false;
    }
    ESP_LOGI(TAG, "%s from the SD card (%u bytes)", roms[id].name, (unsigned)n);
    return true;
}

bool rom_store_load(rom_id_t id, uint8_t *buf)
{
    if (id >= ROM_COUNT) {
        return false;
    }
    if (from_sd(id, buf) && plausible(id, buf)) {
        return true;                    /* the card's overrides the built-in one */
    }
    return from_partition(id, buf) && plausible(id, buf);
}
