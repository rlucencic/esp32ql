#pragma once

#include <stdbool.h>
#include <stdint.h>

/* A key of the board's keyboards (HID usage, $E0-$E7 the modifiers) to the
 * QL's keys
 */
void keymap_hid_key(uint8_t usage, bool pressed);

/* Log each key (its HID usage, the QL keys it becomes): the console's H 1 */
extern bool keymap_log;
