# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **Floppy disks** (the user, 2026-11-18): a QL disk interface (a
   WD1772 and the interface's ROM with its FLP_ driver; esp32ste has a
   WD1772) or a native FLP_ driver; 720K QDOS disks (`.img`, e.g. the
   djw site's Quill235.img). Research: docs/ql-floppy.md (being written).
   Check: `DIR flp1_`, Quill from its disk, a document saved and loaded.
2. **QL software to cartridges on the web page**: `tools/mdvtool.py`'s
   work in the browser (a machine script like esp32ami's amifiles.js): a
   dropped QL zip (QDOS headers in its extra fields) becomes a formatted
   `.mdv` with its files.
3. **Sound**: the IPC's BEEP (pitch, sweep, duration, fuzz) into `sound.c`;
   the parameter length of command 10 checked against what QDOS sends.
4. **The keyboard by hand**: a Bluetooth and a USB keyboard on the boards
   (only the console's keys are checked); KEYROW's row 7 (SHIFT, CTRL, ALT
   order) against a program that reads it.
5. **Minerva**: its differences (the second screen, the clock's setting;
   $18021 bit 7 toggling at the baud rate after serial output, docs 3.1).
6. **More original software**: Abacus, Archive, Easel (Quill works);
   games from the djw vDrive page, a benchmark (sinclairql.net/djw).
7. **Timing**: the ZX8301's contention for the internal RAM, measured
   against a benchmark's known QL figures.
8. **The serial ports and the network**: later, if wanted.
