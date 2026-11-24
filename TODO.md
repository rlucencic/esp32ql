# esp32ql: what's next

In order. A finished item goes into CLAUDE.md (what it is, how it was
checked) and leaves this list.

## Next work

1. **Stress SER1 and SER2 together.** Exercise `main/serial.c` on both boards
   with simultaneous bidirectional transfers, a slow reader and a
   disconnect/reconnect. Check byte order, isolate the two streams and verify
   that a Microdrive operation still completes while both ports are active.

2. **Cover QDOS media round trips across ROMs.** Use JS and Minerva with a
   scratch MDV and a Trump Card floppy. Save a Psion document, eject and
   remount, then reopen it after a reset. Capture the first failed device
   command if behavior differs by ROM.

## Deferred expansion

1. **The network** (NET1_, the QL network): later, if wanted (the user,
   2026-11-18: the serial ports first, done). The ROM bit-bangs it (ZX Net:
   11.2 us a bit through $18002 bit 7 out and $18020 bit 0 in, timed by
   the CPU's own loops; docs/ql-hardware.md 3.8), so two boards over WiFi
   can't pass bits in real time. A sketch: catch the ROM's network driver
   at its packet level (a header and up to 255 bytes) and pass packets
   between boards over UDP; first find which QDOS routines send and
   receive a packet, and whether any software but QDOS's own NET driver
   (file servers: Toolkit 2's FSERVE) needs the bit level.
