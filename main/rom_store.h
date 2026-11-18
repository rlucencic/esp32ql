#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * The QL's system ROMs: built in, from the ql_roms flash partition
 * (roms/<name> in the project, written by idf.py flash into 64 KB slots). A
 * file of the same name in the SD card's "ql/roms" folder overrides one.
 */
typedef enum { ROM_JS, ROM_MINERVA, ROM_COUNT } rom_id_t;

#define ROM_STORE_MAX   (48 * 1024)     /* $00000-$0BFFF */

const char *rom_store_name(rom_id_t id);    /* "js.rom" */

/* The ROM into buf (ROM_STORE_MAX bytes, the rest past a shorter image
 * $FF); false if there's none
 */
bool rom_store_load(rom_id_t id, uint8_t *buf);
