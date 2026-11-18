# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

1. **The keyboard by hand**: a Bluetooth and a USB keyboard on the boards
   (only the console's keys are checked so far), with `tools/keytest.bas`
   (`keytest.img` is in both boards' FLP1_ since 2026-11-18): every key in
   option 2 (its name: position and KEYROW), option 1 for the codes
   (modifiers, F6-F10, the keypad, Backspace/Delete), autorepeat, CTRL +
   SPACE breaking a program; the Tab5 Keyboard too. So far (the user,
   2026-11-18): a Bluetooth keyboard and mouse on the Tab5 ran the test,
   CTRL+SPACE broke it; no findings reported yet. KEYROW's row 7
   (SHIFT, CTRL, ALT in bits 0-2) agrees with the manual's table and
   MAME's on the host.
2. **The network** (NET1_, the QL network): later, if wanted (the user,
   2026-11-18: the serial ports first, done). The ROM bit-bangs it (ZX Net:
   11.2 us a bit through $18002 bit 7 out and $18020 bit 0 in, timed by
   the CPU's own loops; docs/ql-hardware.md 3.8), so two boards over WiFi
   can't pass bits in real time. A sketch: catch the ROM's network driver
   at its packet level (a header and up to 255 bytes) and pass packets
   between boards over UDP; first find which QDOS routines send and
   receive a packet, and whether any software but QDOS's own NET driver
   (file servers: Toolkit 2's FSERVE) needs the bit level.
