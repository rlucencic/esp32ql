# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **The IPC and the keyboard**: the ZX8302's link to the 8049 ($18003
   writes, $18020 reads), the IPC's commands at the protocol level
   (keyboard, keyrow, interrupt status, sound), the QL's keyboard matrix and
   `main/keymap.c` (Bluetooth/USB keyboards, the Tab5 Keyboard), `ql_type()`.
   Check: F1 on the start screen, SuperBASIC's prompt, `PRINT 2+2`.
2. **The Microdrives**: the ZX8302's Microdrive control and data, the tape's
   timing and gap interrupt, `.mdv` images (the common format), formatting,
   reading and writing; blank cartridges from the setup screen and web page.
   Check: `FORMAT mdv1_test`, `SAVE`, `DIR`, `LOAD`, the image read back.
3. **Sound**: the IPC's BEEP (pitch, sweep, duration, fuzz) into `sound.c`.
4. **Minerva**: boots, its differences (the second screen, the clock).
5. **Original software**: Psion Quill from its cartridge, a document saved
   and loaded again; games, a benchmark (sinclairql.net/djw).
6. **Timing**: the ZX8301's contention for the internal RAM, measured
   against a benchmark's known QL figures.
7. **The serial ports and the network**: later, if wanted.
