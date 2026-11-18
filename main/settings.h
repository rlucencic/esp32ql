#pragma once

#include <stdbool.h>
#include <stdint.h>

/* Machine settings kept in NVS (namespace "ql") */
typedef struct {
    uint8_t rom;            /* rom_id_t: 0 JS, 1 Minerva */
    uint8_t memory;         /* 0: 128 KB, 1: 640 KB (512 KB expansion) */
} settings_t;

extern settings_t settings;

void settings_load(void);
void settings_save(void);
