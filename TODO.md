# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **The Microdrives**: the ZX8302's Microdrive data (reading and writing
   through $18020-$18023, the tape's timing and gaps; empty drives work
   already), `.mdv` images (the common format), formatting, reading and
   writing; blank cartridges from the setup screen and web page. Check:
   `FORMAT mdv1_test`, `SAVE`, `DIR`, `LOAD`, the image read back.
2. **Sound**: the IPC's BEEP (pitch, sweep, duration, fuzz) into `sound.c`;
   the parameter length of command 10 checked against what QDOS sends.
3. **The keyboard by hand**: a Bluetooth and a USB keyboard on the boards
   (only the console's keys are checked); KEYROW's row 7 (SHIFT, CTRL, ALT
   order) against a program that reads it.
4. **Minerva**: its differences (the second screen, the clock's setting).
5. **Original software**: Psion Quill from its cartridge, a document saved
   and loaded again; games, a benchmark (sinclairql.net/djw).
6. **Timing**: the ZX8301's contention for the internal RAM, measured
   against a benchmark's known QL figures.
7. **The serial ports and the network**: later, if wanted.
