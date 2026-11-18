# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **QL software to cartridges and disks on the web page**:
   `tools/mdvtool.py`'s work in the browser (a machine script like
   esp32ami's amifiles.js): a dropped QL zip (QDOS headers in its extra
   fields) becomes a formatted `.mdv` with its files, blocks spaced as QDOS
   spaces them; the same for a 720 KB `.img` (QDOS's floppy layout: its
   map in sector 0, read from a disk the Trump Card formatted).
2. **Sound**: the IPC's BEEP (pitch, sweep, duration, fuzz) into `sound.c`;
   the parameter length of command 10 checked against what QDOS sends.
3. **The keyboard by hand**: a Bluetooth and a USB keyboard on the boards
   (only the console's keys are checked); KEYROW's row 7 (SHIFT, CTRL, ALT
   order) against a program that reads it.
4. **Minerva**: its differences (the second screen, the clock's setting;
   $18021 bit 7 toggling at the baud rate after serial output, docs 3.1).
5. **More original software**: Abacus, Archive, Easel (Quill works);
   games from the djw vDrive page, a benchmark (sinclairql.net/djw).
6. **Timing**: the ZX8301's contention for the internal RAM, measured
   against a benchmark's known QL figures.
7. **The serial ports and the network**: later, if wanted.
