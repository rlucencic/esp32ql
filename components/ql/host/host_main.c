/*
 * Run the QL on the host: boot a ROM for a number of frames and write the
 * screen as a PPM image.
 *
 *   ./ql_host <rom> [frames] [out.ppm]
 *
 * Environment:
 *   QL_RAM_KB=128|640      memory (128)
 *   QL_LOG=<n>             the PC and SR every n frames
 *   QL_SHOTS=<n>           a picture every n frames (out.ppm's name + frame)
 *   QL_RING=<n>            the last n instructions at the end
 *   QL_STOP=<pc>[:<frame>] on reaching pc (hex) after that frame: the 80
 *                          instructions before, the registers, and exit
 *   QL_TRACE=<pc>:<n>      from pc on, the next n instructions
 *   QL_IO=1                every access to the on-board I/O
 *   QL_TYPE=<frame>:<text> type text from that frame (\n is ENTER)
 *   QL_KEYS=<frame>:<key>:<0|1>[;...]  matrix keys down or up
 *   QL_MDV1, QL_MDV2       cartridge images (written: use copies)
 *   QL_WAV=<file>          the sound
 *   QL_DEBUG=<port>        the debugger (real time)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ql.h"
#include "m68k.h"
#include "emudbg.h"

static uint32_t s_frame;

static void disassemble(uint32_t pc, int count)
{
    char buf[100];
    for (int i = 0; i < count; i++) {
        unsigned int len = m68k_disassemble(buf, pc, M68K_CPU_TYPE_68000);
        printf("  $%05lx  %s\n", (unsigned long)pc, buf);
        pc += len;
    }
}

static void registers(void)
{
    printf("  D0-D7");
    for (int r = M68K_REG_D0; r <= M68K_REG_D7; r++) {
        printf(" %08x", m68k_get_reg(NULL, r));
    }
    printf("\n  A0-A7");
    for (int r = M68K_REG_A0; r <= M68K_REG_A7; r++) {
        printf(" %08x", m68k_get_reg(NULL, r));
    }
    printf("\n  PC %05x SR %04x USP %08x SSP %08x\n", m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFF,
           m68k_get_reg(NULL, M68K_REG_SR), m68k_get_reg(NULL, M68K_REG_USP), m68k_get_reg(NULL, M68K_REG_ISP));
}

#define RING_SIZE 4096
static uint32_t ring[RING_SIZE];
static uint32_t ring_pos;

void m68k_host_instruction(unsigned int pc)
{
    pc &= 0xFFFFF;
    ring[ring_pos++ % RING_SIZE] = pc;

    static uint32_t stop_pc = 1, stop_after;
    if (stop_pc == 1) {
        const char *e = getenv("QL_STOP");
        stop_pc = e ? strtoul(e, NULL, 16) : 0;
        stop_after = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
    }
    if (stop_pc && pc == stop_pc && s_frame >= stop_after) {
        printf("-- reached $%05x at frame %lu after:\n", pc, (unsigned long)s_frame);
        for (uint32_t i = ring_pos - 80; i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE], 1);
        }
        registers();
        exit(0);
    }

    static uint32_t trace_pc = 1;
    static int trace_left;
    if (trace_pc == 1) {
        const char *e = getenv("QL_TRACE");
        trace_pc = e ? strtoul(e, NULL, 16) : 0;
        trace_left = e && strchr(e, ':') ? atoi(strchr(e, ':') + 1) : 0;
        if (trace_pc == 0) {
            trace_left = 0;
        }
    }
    if (trace_left > 0 && (trace_pc == 0 || pc == trace_pc)) {
        trace_pc = 0;
        trace_left--;
        disassemble(pc, 1);
    }
}

static void write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) {
        perror(path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", QL_SCREEN_WIDTH, QL_SCREEN_HEIGHT);
    uint16_t line[QL_SCREEN_WIDTH];
    for (int y = 0; y < QL_SCREEN_HEIGHT; y++) {
        ql_render_line_rgb565(y, line);
        for (int x = 0; x < QL_SCREEN_WIDTH; x++) {
            uint16_t c = line[x];
            uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 0x3F) << 2), (uint8_t)((c & 0x1F) << 3) };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
}

/* QL_KEYS=<frame>:<key>:<0|1>;... */
static void keys_for_frame(uint32_t frame)
{
    const char *e = getenv("QL_KEYS");
    while (e && *e) {
        unsigned f, key, down;
        if (sscanf(e, "%u:%u:%u", &f, &key, &down) == 3 && f == frame) {
            ql_key(key, down);
        }
        e = strchr(e, ';');
        e = e ? e + 1 : NULL;
    }
}

static FILE *s_wav;
static uint32_t s_wav_frames;

static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t bytes = frames * 4, rate = QL_AUDIO_HZ, byte_rate = rate * 4;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f);
    uint32_t riff = 36 + bytes;
    fwrite(&riff, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmt_len = 16;
    uint16_t pcm = 1, channels = 2, align = 4, bits = 16;
    fwrite(&fmt_len, 4, 1, f);
    fwrite(&pcm, 2, 1, f);
    fwrite(&channels, 2, 1, f);
    fwrite(&rate, 4, 1, f);
    fwrite(&byte_rate, 4, 1, f);
    fwrite(&align, 2, 1, f);
    fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f);
    fwrite(&bytes, 4, 1, f);
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <rom> [frames] [out.ppm]\n", argv[0]);
        return 1;
    }
    static uint8_t rom[QL_ROM_SIZE];
    memset(rom, 0xFF, sizeof(rom));
    FILE *f = fopen(argv[1], "rb");
    if (f == NULL) {
        perror(argv[1]);
        return 1;
    }
    size_t n = fread(rom, 1, sizeof(rom), f);
    fclose(f);
    printf("ROM %s: %zu bytes\n", argv[1], n);
    extern bool ql_debug_io;
    ql_debug_io = getenv("QL_IO") != NULL;

    uint32_t frames = argc > 2 ? strtoul(argv[2], NULL, 10) : 500;
    const char *out = argc > 3 ? argv[3] : "screen.ppm";
    uint32_t ram_kb = getenv("QL_RAM_KB") ? strtoul(getenv("QL_RAM_KB"), NULL, 10) : 128;

    static uint8_t ram[QL_RAM_640K];
    ql_config_t cfg = { .rom = rom, .ram = ram, .ram_size = ram_kb * 1024 };
    if (!ql_init(&cfg)) {
        fprintf(stderr, "ql_init failed\n");
        return 1;
    }
    for (int d = 0; d < QL_MDV_DRIVES; d++) {
        const char *path = getenv(d == 0 ? "QL_MDV1" : "QL_MDV2");
        if (path && !ql_mdv_insert(d, path)) {
            fprintf(stderr, "can't insert %s\n", path);
        }
    }
    if (getenv("QL_WAV")) {
        s_wav = fopen(getenv("QL_WAV"), "wb");
        if (s_wav) {
            wav_header(s_wav, 0);
            ql_audio_enable(true);
        }
    }
    /* QL_DEBUG=<port>: serve the debugger (esp32emu's docs/debugger.md) on
     * 127.0.0.1:<port> and run in real time until killed
     */
    emudbg_host_run("QL_DEBUG", ql_run_frame, QL_SCREEN_HZ);

    uint32_t log_every = getenv("QL_LOG") ? strtoul(getenv("QL_LOG"), NULL, 10) : 0;
    uint32_t shots = getenv("QL_SHOTS") ? strtoul(getenv("QL_SHOTS"), NULL, 10) : 0;
    const char *type = getenv("QL_TYPE");
    uint32_t type_frame = type ? strtoul(type, NULL, 10) : 0;
    const char *type_text = type && strchr(type, ':') ? strchr(type, ':') + 1 : NULL;
    static char text[512];
    if (type_text) {
        size_t k = 0;
        for (const char *s = type_text; *s && k < sizeof(text) - 1; s++) {
            if (s[0] == '\\' && s[1] == 'n') {
                text[k++] = '\n';
                s++;
            } else {
                text[k++] = *s;
            }
        }
        text[k] = 0;
    }

    for (s_frame = 0; s_frame < frames; s_frame++) {
        keys_for_frame(s_frame);
        if (type_text && s_frame >= type_frame) {
            if (ql_type(text)) {
                type_text = NULL;
            }
        }
        ql_run_frame();
        if (s_wav) {
            int16_t buf[4096 * 2];
            int got;
            while ((got = ql_audio_render(buf, 4096)) > 0) {
                fwrite(buf, 4, got, s_wav);
                s_wav_frames += got;
            }
        }
        if (log_every && s_frame % log_every == 0) {
            ql_status_t s;
            ql_get_status(&s);
            printf("frame %u: PC $%05lx SR %04x mode %02x\n", s_frame, (unsigned long)s.pc, s.sr, s.mode);
        }
        if (shots && s_frame % shots == shots - 1) {
            char name[300];
            snprintf(name, sizeof(name), "%.*s-%05u.ppm", (int)(strlen(out) > 4 ? strlen(out) - 4 : strlen(out)), out,
                     s_frame + 1);
            write_ppm(name);
        }
        if (ql_halted()) {
            printf("halted at frame %u\n", s_frame);
            break;
        }
    }
    ql_mdv_flush();
    if (s_wav) {
        wav_header(s_wav, s_wav_frames);
        fclose(s_wav);
    }
    write_ppm(out);
    ql_status_t s;
    ql_get_status(&s);
    printf("after %u frames: PC $%05lx SR %04x mode %02x\n", s_frame, (unsigned long)s.pc, s.sr, s.mode);
    const char *ring_env = getenv("QL_RING");
    if (ring_env) {
        int count = atoi(ring_env);
        count = count > RING_SIZE ? RING_SIZE : count;
        for (uint32_t i = ring_pos - count; i != ring_pos; i++) {
            disassemble(ring[i % RING_SIZE], 1);
        }
        registers();
    }
    return 0;
}
