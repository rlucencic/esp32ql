/*
 * Sinclair QL emulator on the Makerfabs MaTouch ESP32-S3 7" and the M5Stack
 * Tab5.
 *
 * The board side (tasks, LCD, touch, WiFi, Bluetooth, setup screen, web
 * page, console) is the shared runtime in ../esp32emu; this file plugs the
 * QL into it: memory, ROMs, settings, keyboard map, Microdrives and the
 * Trump Card's floppy drives, status.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "emu/emu.h"
#include "ql.h"
#include "keymap.h"
#include "rom_store.h"
#include "serial.h"
#include "settings.h"

static const char *TAG = "main";

static const char *const rom_names[] = { "JS", "Minerva", "" };
static const char *const rom_full[] = { "Sinclair JS (QDOS 1.10)", "Minerva 1.98" };
static const char *const memory_names[] = { "128 KB", "640 KB", "" };
static const char *const floppy_names[] = { "None", "Trump Card", "" };

static ql_config_t s_cfg;
static uint8_t *s_rom, *s_trump;
static bool s_started;

/* ---- Memory ----
 *
 * Everything at power-on, before the screen, WiFi and Bluetooth fragment
 * PSRAM: the ROM, 640 KB of RAM whatever the setting, the CPU core's tables.
 */
static void reserve_memory(void)
{
    settings_load();
    s_rom = heap_caps_malloc(QL_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_trump = heap_caps_malloc(QL_TRUMP_ROM_SIZE, MALLOC_CAP_SPIRAM);
    s_cfg.ram = heap_caps_malloc(QL_RAM_640K, MALLOC_CAP_SPIRAM);
    s_cfg.opcode_tables = heap_caps_malloc(QL_OPCODE_TABLES_SIZE, MALLOC_CAP_SPIRAM);
}

static void storage(bool sd_card)
{
    emu_set_drive_info(sd_card ? "Microdrive cartridges and floppy disks on the SD card" :
                       "No SD card: no Microdrives or floppy disks");
}

/* ---- Starting ---- */

static bool start(void)
{
    if (s_rom == NULL || s_trump == NULL || s_cfg.ram == NULL) {
        emu_set_message("Not enough memory: see the log.");
        return false;
    }
    if (!rom_store_load(settings.rom, s_rom)) {
        static char message[160];
        snprintf(message, sizeof(message), "No %s ROM: put %s into roms/ and flash with idf.py flash.",
                 rom_names[settings.rom], rom_store_name(settings.rom));
        emu_set_message(message);
        return false;
    }
    s_cfg.rom = s_rom;
    s_cfg.ram_size = settings.memory ? QL_RAM_640K : QL_RAM_128K;
    s_cfg.trump_rom = NULL;
    if (settings.floppy) {
        if (!rom_store_load(ROM_TRUMP, s_trump)) {
            static char message[160];
            snprintf(message, sizeof(message), "No Trump Card ROM: put %s into roms/ and flash with idf.py flash, "
                     "or set Floppy to None.", rom_store_name(ROM_TRUMP));
            emu_set_message(message);
            return false;
        }
        s_cfg.trump_rom = s_trump;
    }
    if (!ql_init(&s_cfg)) {
        emu_set_message("The QL could not be set up: see the log.");
        return false;
    }
    ql_rtc_set_offset(settings.rtc_ofs);
    s_started = true;
    ESP_LOGI(TAG, "%s, %s%s", rom_full[settings.rom], memory_names[settings.memory],
             settings.floppy ? ", a Trump Card" : "");
    return true;
}

static void screen_mode(emu_screen_t *mode)
{
    mode->width = QL_SCREEN_WIDTH;
    mode->height = QL_SCREEN_HEIGHT;
    mode->hz = QL_SCREEN_HZ;
}

/* ---- Setup screen choices ---- */

static int rom_get(void)
{
    return settings.rom;
}

static void rom_set(int i)
{
    if (i >= 0 && i <= ROM_MINERVA) {
        settings.rom = i;
        settings_save();
    }
}

static int memory_get(void)
{
    return settings.memory;
}

static void memory_set(int i)
{
    if (i >= 0 && i <= 1) {
        settings.memory = i;
        settings_save();
    }
}

static int floppy_get(void)
{
    return settings.floppy;
}

static void floppy_set(int i)
{
    if (i >= 0 && i <= 1) {
        settings.floppy = i;
        settings_save();
    }
}

static const emu_choice_t choices[] = {
    { "ROM", rom_names, rom_get, rom_set, false },
    { "Memory", memory_names, memory_get, memory_set, false },
    { "Floppy", floppy_names, floppy_get, floppy_set, false },
};

/* ---- Microdrives and floppy drives ---- */

enum { DRIVE_MDV1, DRIVE_MDV2, DRIVE_FLP1, DRIVE_FLP2, DRIVES };

static const char *const extensions[] = { ".mdv", ".img", NULL };
static const char *const drive_names[] = { "MDV1_", "MDV2_", "FLP1_", "FLP2_" };
#define MDV_BYTES       174930          /* a blank cartridge image */
static const disks_size_t sizes[] = {
    { MDV_BYTES, "Microdrive cartridge (.MDV)" },
    { QL_FLP_IMAGE_SIZE, "720 KB floppy disk (.IMG)" },
};

static bool is_floppy(int drive)
{
    return drive >= DRIVE_FLP1;
}

static bool insert(int drive, const char *path)
{
    return is_floppy(drive) ? ql_flp_insert(drive - DRIVE_FLP1, path) : ql_mdv_insert(drive, path);
}

static void eject(int drive)
{
    if (is_floppy(drive)) {
        ql_flp_eject(drive - DRIVE_FLP1);
    } else {
        ql_mdv_eject(drive);
    }
}

static const char *path(int drive)
{
    return is_floppy(drive) ? ql_flp_path(drive - DRIVE_FLP1) : ql_mdv_path(drive);
}

static bool write_protected(int drive)
{
    return is_floppy(drive) ? ql_flp_write_protected(drive - DRIVE_FLP1) : ql_mdv_write_protected(drive);
}

static bool create(const char *path, uint32_t bytes)
{
    return bytes == MDV_BYTES ? ql_mdv_create(path) : ql_flp_create(path);
}

static bool ends_with(const char *name, const char *ext)
{
    size_t n = strlen(name), e = strlen(ext);
    return n > e && strcasecmp(name + n - e, ext) == 0;
}

/* Cartridges in the Microdrives, disk images in the floppy drives */
static bool accepts(int drive, const char *name)
{
    return ends_with(name, is_floppy(drive) ? ".img" : ".mdv");
}

static uint32_t default_size(int drive)
{
    return drive >= 0 && is_floppy(drive) ? QL_FLP_IMAGE_SIZE : MDV_BYTES;
}

static const char *extension_for(uint32_t bytes)
{
    return bytes == MDV_BYTES ? ".mdv" : ".img";
}

static const disks_config_t disks = {
    .folder = "ql",
    .extensions = extensions,
    .description = "Microdrive cartridges (.MDV), floppy disks (.IMG)",
    .title = "Drives",
    .button = "Drives",
    .nvs_namespace = "qldisks",
    .drives = DRIVES,
    .drive_names = drive_names,
    .new_prefix = "BLANK",
    .new_extension = ".mdv",
    .sizes = sizes,
    .size_count = sizeof(sizes) / sizeof(sizes[0]),
    .max_upload = 4 * 1024 * 1024,
    .insert = insert,
    .eject = eject,
    .path = path,
    .write_protected = write_protected,
    .create = create,
    .accepts = accepts,
    .default_size = default_size,
    .extension_for = extension_for,
};

/* Cartridge and disk writes go to the SD card from here (core 0), between
 * frames, once nothing has been written for a second
 */
static void flush(void *arg)
{
    ql_mdv_flush();
    ql_flp_flush();
}

/* The QL's clock set (SDATE steps it a byte at a time): kept once it has
 * stood for two seconds
 */
static void keep_clock(void)
{
    static int32_t last;
    static int still;
    int32_t now = ql_rtc_offset();
    if (now == settings.rtc_ofs) {
        still = 0;
    } else if (now != last) {
        last = now;
        still = 0;
    } else if (++still >= 2) {
        settings.rtc_ofs = now;
        settings_save();
        ESP_LOGI(TAG, "the QL's clock: %ld s off the board's", (long)now);
        still = 0;
    }
}

static void every_second(void)
{
    static int dirty_for;
    if (s_started) {
        keep_clock();
        serial_start();                         /* (after the speaker's buffers: serial.c) */
    }
    if (ql_mdv_dirty() || ql_flp_dirty()) {
        if (++dirty_for >= 2) {
            emu_paused(flush, NULL);
            dirty_for = 0;
        }
    } else {
        dirty_for = 0;
    }
}

/* ---- Status ---- */

static void status(emu_json_t *j, int section)
{
    char text[160];
    ql_status_t s;
    ql_get_status(&s);
    if (section == EMU_STATUS_ROWS) {
        snprintf(text, sizeof(text), "Sinclair QL, %s", memory_names[settings.memory]);
        emu_json_row(j, "System", text);
        emu_json_row(j, "ROM", rom_full[settings.rom]);
        emu_json_row(j, "Floppy", settings.floppy ? "Miracle Trump Card (FLP1_, FLP2_; Toolkit 2: TK2_EXT)" :
                     "None");
        snprintf(text, sizeof(text), "SER1 on TCP port %d%s, SER2 on %d%s", serial_tcp_port(0),
                 serial_connected(0) ? " (connected)" : "", serial_tcp_port(1), serial_connected(1) ? " (connected)" : "");
        emu_json_row(j, "Serial", text);
        snprintf(text, sizeof(text), "%s, screen at $%05X%s", (s.mode & 0x08) ? "256 × 256, 8 colours" :
                 "512 × 256, 4 colours", (s.mode & 0x80) ? 0x28000 : 0x20000, (s.mode & 0x02) ? ", blanked" : "");
        emu_json_row(j, "Screen", text);
        return;
    }
    for (int d = 0; d < DRIVES; d++) {
        const char *name = disks_name(d);
        if (is_floppy(d)) {
            int f = d - DRIVE_FLP1;
            char detail[32];
            snprintf(detail, sizeof(detail), "track %u", s.flp_track[f]);
            emu_json_drive(j, drive_names[d], name, ql_flp_write_protected(f), s.flp_light == f + 1,
                           s.flp_reads[f], s.flp_writes[f], name ? detail : NULL);
        } else {
            emu_json_drive(j, drive_names[d], name, ql_mdv_write_protected(d), s.mdv_motor == d + 1,
                           s.mdv_reads[d], s.mdv_writes[d], NULL);
        }
    }
}

/* ---- Console ---- */

static void console_info(char *buf, size_t len)
{
    ql_status_t s;
    ql_get_status(&s);
    snprintf(buf, len, "frames %lu PC $%05lX SR %04X mode %02X", (unsigned long)s.frames, (unsigned long)s.pc, s.sr,
             s.mode);
}

/*   k <n>              tap matrix key n (row * 8 + column)
 *   K <n> <0|1>        key n down (1) or up (0)
 *   T <text>           type text (\n is ENTER)
 *   R <rom> [memory] [floppy]   restart with ROM 0 JS, 1 Minerva; memory 0
 *                      128 KB, 1 640 KB; floppy 0 none, 1 the Trump Card
 */
static bool console_command(const char *line)
{
    switch (line[0]) {
    case 'k': {
        unsigned key = strtoul(line + 1, NULL, 10);
        ql_key(key, true);
        vTaskDelay(pdMS_TO_TICKS(100));
        ql_key(key, false);
        return true;
    }
    case 'K': {
        unsigned key = 0, down = 0;
        if (sscanf(line + 1, "%u %u", &key, &down) == 2) {
            ql_key(key, down);
        }
        return true;
    }
    case 'T': {
        static char text[200];
        const char *s = line + 1;
        while (*s == ' ') {
            s++;
        }
        size_t n = 0;
        for (; *s && n < sizeof(text) - 1; s++) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[n++] = '\n';
                s++;
            } else {
                text[n++] = *s;
            }
        }
        text[n] = 0;
        ql_type(text);
        return true;
    }
    case 'R': {
        int rom = 0, memory = settings.memory, floppy = settings.floppy;
        if (sscanf(line + 1, "%d %d %d", &rom, &memory, &floppy) >= 1 && rom >= 0 && rom <= ROM_MINERVA &&
            memory >= 0 && memory <= 1 && floppy >= 0 && floppy <= 1) {
            settings.rom = rom;
            settings.memory = memory;
            settings.floppy = floppy;
            settings_save();
            emu_restart();
        }
        return true;
    }
    default:
        return false;
    }
}

/* ---- The machine ---- */

#if CONFIG_EMU_WEB
/* web/qlfiles.js: QL files put on a new cartridge and floppy disk in the browser */
extern const char qlfiles_js_start[] asm("_binary_qlfiles_js_start");
extern const char qlfiles_js_end[] asm("_binary_qlfiles_js_end");
#endif

static const emu_machine_t machine = {
    .name = "Sinclair QL",
    .short_name = "QL",
    .view_width = 800,                  /* 512 x 256 on a 4:3 screen */
    .scale = EMU_SCALE_SMOOTH,
    .reserve_memory = reserve_memory,
    .storage = storage,
    .start = start,
    .reset = ql_reset,
    .run_frame = ql_run_frame,
    .halted = ql_halted,
    .screen_mode = screen_mode,
    .screen_signature = ql_screen_signature,
    .render_line = ql_render_line_rgb565,
    .audio_hz = QL_AUDIO_HZ,
    .audio_enable = ql_audio_enable,
    .audio_render = ql_audio_render,
    .audio_pending = ql_audio_pending,
    .hid_key = keymap_hid_key,
    .choices = choices,
    .choice_count = sizeof(choices) / sizeof(choices[0]),
    .disks = &disks,
#if CONFIG_EMU_WEB
    .web_script = qlfiles_js_start,
    .web_script_end = qlfiles_js_end,
#endif
    .status = status,
    .console_info = console_info,
    .console_command = console_command,
    .every_second = every_second,
};

void app_main(void)
{
    emu_run(&machine);
}
