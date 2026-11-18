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
#define MIN_SIZE            (32 * 1024) /* a shorter file isn't a system ROM */

static const char *TAG = "rom";

static const char *const names[ROM_COUNT] = { "js.rom", "minerva.rom" };

const char *rom_store_name(rom_id_t id)
{
    return id < ROM_COUNT ? names[id] : "";
}

/* A 68000 reset vector at the start: not erased flash, not zeros, and a
 * reset PC inside the ROM
 */
static bool plausible(const uint8_t *p)
{
    uint32_t pc = ((uint32_t)p[4] << 24) | (p[5] << 16) | (p[6] << 8) | p[7];
    bool ff = true, zero = true;
    for (int i = 0; i < 64; i++) {
        ff &= p[i] == 0xFF;
        zero &= p[i] == 0x00;
    }
    return !ff && !zero && pc >= 8 && pc < ROM_STORE_MAX && (pc & 1) == 0;
}

/* Through a mapping, not esp_partition_read(): that turns the caches off
 * while it reads, which stalls the LCD's refill (picture artefacts)
 */
static bool from_partition(uint32_t offset, uint8_t *buf)
{
    const esp_partition_t *part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ROM_PARTITION_TYPE,
                                                           "ql_roms");
    if (part == NULL || offset + ROM_STORE_MAX > part->size) {
        return false;
    }
    const void *map;
    esp_partition_mmap_handle_t handle;
    if (esp_partition_mmap(part, offset, ROM_STORE_MAX, ESP_PARTITION_MMAP_DATA, &map, &handle) != ESP_OK) {
        return false;
    }
    memcpy(buf, map, ROM_STORE_MAX);
    esp_partition_munmap(handle);
    return true;
}

/* ql/roms/<name> on the SD card (any case), up to 48 KB */
static bool from_sd(const char *name, uint8_t *buf)
{
    char path[96];
    snprintf(path, sizeof(path), "%s/%s", ROM_FOLDER, name);
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
    memset(buf, 0xFF, ROM_STORE_MAX);
    size_t n = fread(buf, 1, ROM_STORE_MAX, f);
    fclose(f);
    if (n < MIN_SIZE) {
        return false;
    }
    ESP_LOGI(TAG, "%s from the SD card (%u bytes)", name, (unsigned)n);
    return true;
}

bool rom_store_load(rom_id_t id, uint8_t *buf)
{
    if (id >= ROM_COUNT) {
        return false;
    }
    if (from_sd(names[id], buf) && plausible(buf)) {
        return true;                    /* the card's overrides the built-in one */
    }
    return from_partition(id * SLOT, buf) && plausible(buf);
}
