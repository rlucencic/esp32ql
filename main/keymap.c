/*
 * The board's keyboards to the QL's keyboard matrix (ql_key()). Not there
 * yet: the keys need the IPC (the 8049), which comes next.
 */
#include "ql.h"
#include "keymap.h"

void keymap_hid_key(uint8_t usage, bool pressed)
{
    (void)usage;
    (void)pressed;
}
