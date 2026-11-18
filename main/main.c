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
#include <time.h>
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
static int s_rom_id;                    /* the ROM the machine started with */

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
    s_rom_id = settings.rom;
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

/* A colour swatch (#rgb) */
static int swatch(char *p, size_t size, unsigned rgb)
{
    return snprintf(p, size, "<b style=\"background:#%03x\">&nbsp;&nbsp;&nbsp;</b>", rgb & 0xFFF);
}

static void status_rows(emu_json_t *j, const ql_status_t *s)
{
    char text[320];
    ql_machine_t m;
    ql_get_machine(&m);
    bool big = s_cfg.ram_size > QL_RAM_128K;
    snprintf(text, sizeof(text), "Sinclair QL: a 68008 at 7.5 MHz, %u KB RAM, the ZX8301 and ZX8302, the IPC "
             "(an 8049)", (unsigned)(s_cfg.ram_size / 1024));
    emu_json_row(j, "System", text);

    snprintf(text, sizeof(text), "%s, 48 KB at $00000, from %s%s", rom_full[s_rom_id],
             rom_store_from_sd(s_rom_id) ? "the SD card" : "the flash", s_cfg.trump_rom ?
             "; the Trump Card's 32 KB at $10000 (its FLP_ driver, RAM disks, Toolkit 2)" : "");
    emu_json_row(j, "ROM", text);

    int n = snprintf(text, sizeof(text), big ? "128 KB at $20000 (slowed by the screen's reads), 512 KB at "
                     "$40000-$BFFFF" : "128 KB at $20000-$3FFFF (slowed by the screen's reads)");
    if (m.qdos) {
        snprintf(text + n, sizeof(text) - n, "; QDOS's variables at $%05lX, RAMTOP $%05lX", (unsigned long)m.sysvars,
                 (unsigned long)m.ramtop);
    }
    emu_json_row(j, "Memory", text);
    /* QDOS's free area, as Toolkit 2's FREE_MEM gives it */
    if (m.qdos) {
        uint32_t total = s_cfg.ram_size;
        snprintf(text, sizeof(text), "%lu KB of %lu KB", (unsigned long)(m.free_bytes / 1024),
                 (unsigned long)(total / 1024));
        emu_json_row_bar(j, "Free RAM", text, 100 - (int)((uint64_t)m.free_bytes * 100 / total));
    }

    bool mode8 = m.mode & 0x08;
    snprintf(text, sizeof(text), "Mode %d: %s; screen %d at $%05X%s", mode8 ? 8 : 4,
             mode8 ? "256 × 256, 8 colours, flashing" : "512 × 256, 4 colours", (m.mode & 0x80) ? 1 : 0,
             (m.mode & 0x80) ? 0x28000 : 0x20000, (m.mode & 0x02) ? ", blanked" : "");
    emu_json_row(j, "Screen", text);
    /* mode 4: black, red, green, white; mode 8: G R B, black to white */
    static const uint16_t mode4[4] = { 0x000, 0xF00, 0x0F0, 0xFFF };
    static const uint16_t mode8_rgb[8] = { 0x000, 0x00F, 0xF00, 0xF0F, 0x0F0, 0x0FF, 0xFF0, 0xFFF };
    static char swatches[512];          /* (the web server's task only) */
    n = snprintf(swatches, sizeof(swatches), "%d colours: ", mode8 ? 8 : 4);
    for (int i = 0; i < (mode8 ? 8 : 4) && (size_t)n < sizeof(swatches); i++) {
        n += swatch(swatches + n, sizeof(swatches) - n, mode8 ? mode8_rgb[i] : mode4[i]);
    }
    emu_json_row(j, "Palette", swatches);

    /* The IPC's beeper: BEEP's pitch and duration (72 us units, 0 until stopped) */
    if (m.beep_on) {
        n = snprintf(text, sizeof(text), "The IPC's beeper: about %u Hz (BEEP's pitch %u)", m.beep_hz,
                     m.beep_pitch1);
        if (m.beep_steps) {
            n += snprintf(text + n, sizeof(text) - n, ", stepping between %u and %u", m.beep_pitch1, m.beep_pitch2);
        }
        if (m.beep_duration) {
            unsigned ms = (unsigned)((uint32_t)m.beep_duration * 72 / 1000);
            snprintf(text + n, sizeof(text) - n, ", for %u.%02u s", ms / 1000, ms % 1000 / 10);
        } else {
            snprintf(text + n, sizeof(text) - n, ", until stopped");
        }
    } else {
        snprintf(text, sizeof(text), "The IPC's beeper: quiet");
    }
    emu_json_row(j, "Sound", text);

    snprintf(text, sizeof(text), "Level 2 (the ZX8302's): the frame (50 Hz) and external always; gap %s, "
             "interface %s, transmit %s", (m.int_mask & 0x20) ? "on" : "off", (m.int_mask & 0x40) ? "on" : "off",
             (m.int_mask & 0x80) ? "on" : "off");
    emu_json_row(j, "Interrupts", text);

    snprintf(text, sizeof(text), "By position (US): ` £, Backspace CTRL+LEFT, Delete CTRL+RIGHT, F6-F10 "
             "SHIFT+F1-F5, Pause CTRL+SPACE (break); CAPS LOCK %s", m.caps_lock ? "on" : "off");
    emu_json_row(j, "Keyboard", text);

    snprintf(text, sizeof(text), "%s; QLay cartridges (.mdv), 255 sectors, 8.1 s a turn",
             s->mdv_motor ? (s->mdv_motor == 1 ? "MDV1_ turning" : "MDV2_ turning") : "Both still");
    emu_json_row(j, "Microdrives", text);
    if (s_cfg.trump_rom) {
        snprintf(text, sizeof(text), "Miracle Trump Card: a WD1772, FLP1_ and FLP2_ (720 KB .img, also 360 and 800 "
                 "KB)%s; Toolkit 2 after TK2_EXT", s->flp_light ? (s->flp_light == 1 ? ", FLP1_ busy" :
                 ", FLP2_ busy") : "");
        emu_json_row(j, "Floppy", text);
    } else {
        emu_json_row(j, "Floppy", "None (the setup screen's Floppy row: Trump Card)");
    }

    /* The ZX8302's transmitter: SER1, SER2, the Microdrives or the network */
    static const unsigned baud[8] = { 19200, 9600, 4800, 2400, 1200, 600, 300, 75 };
    static const char *const sends[4] = { "SER1", "SER2", "the Microdrives", "the network" };
    static const char *const states[4] = { "", " (QDOS reading)", " (connected)", " (connected, QDOS reading)" };
    n = snprintf(text, sizeof(text), "SER1 on TCP port %d%s, SER2 on %d%s; sending to %s", serial_tcp_port(0),
                 states[serial_connected(0) * 2 + m.ser_open[0]], serial_tcp_port(1),
                 states[serial_connected(1) * 2 + m.ser_open[1]], sends[(m.tmode >> 3) & 3]);
    if (!(m.tmode & 0x10)) {
        snprintf(text + n, sizeof(text) - n, " at %u baud", baud[m.tmode & 7]);
    }
    emu_json_row(j, "Serial", text);

    /* The QL's clock: the board's plus the offset SDATE/ADATE made (DATE$'s form) */
    time_t when = time(NULL) + ql_rtc_offset();
    struct tm tm;
    gmtime_r(&when, &tm);
    n = (int)strftime(text, sizeof(text), "%Y %b %d %H:%M:%S", &tm);
    if (ql_rtc_offset()) {
        snprintf(text + n, sizeof(text) - n, " (the board's time %+ld s, set with SDATE)", (long)ql_rtc_offset());
    } else {
        snprintf(text + n, sizeof(text) - n, " (the board's time)");
    }
    emu_json_row(j, "Clock", text);
}

static void status(emu_json_t *j, int section)
{
    ql_status_t s;
    ql_get_status(&s);
    if (section == EMU_STATUS_ROWS) {
        status_rows(j, &s);
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
 *   H <0|1>            log the keyboards' keys (HID usage, the QL's keys)
 *                      and the keys the IPC hands QDOS
 */
static bool console_command(const char *line)
{
    switch (line[0]) {
    case 'H': {
        extern bool ql_debug_keys;
        keymap_log = ql_debug_keys = strtoul(line + 1, NULL, 10) != 0;
        return true;
    }
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
