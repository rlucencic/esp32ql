# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **Sound**: the IPC's BEEP (pitch, sweep, duration, fuzz) into `sound.c`;
   the parameter length of command 10 checked against what QDOS sends.
2. **The keyboard by hand**: a Bluetooth and a USB keyboard on the boards
   (only the console's keys are checked); KEYROW's row 7 (SHIFT, CTRL, ALT
   order) against a program that reads it.
3. **Minerva**: its differences (the second screen, the clock's setting;
   $18021 bit 7 toggling at the baud rate after serial output, docs 3.1).
4. **More original software**: Abacus, Archive, Easel (Quill works);
   games from the djw vDrive page, a benchmark (sinclairql.net/djw).
5. **Timing**: the ZX8301's contention for the internal RAM, measured
   against a benchmark's known QL figures.
6. **The serial ports and the network**: later, if wanted.
