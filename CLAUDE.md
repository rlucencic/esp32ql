# esp32ql: a Sinclair QL on the MaTouch ESP32-S3 7" and the M5Stack Tab5

A Sinclair QL (1984): a 68008 at 7.5 MHz, 128 KB RAM (640 KB with an
expansion), 48 KB ROM (Sinclair's JS, QDOS 1.10, or Minerva 1.98), the
ZX8301 (display, DRAM), the ZX8302 (clock, interrupts, Microdrives, serial
out, the link to the IPC), the IPC (an 8049: keyboard, sound, serial in) and
two Microdrives; floppy disks through a Miracle Trump Card (its ROM's
driver, a WD1772). Both boards (the user's call, 2026-11-18). The plan:
esp32emu's CLAUDE.md, "To do" (the QL's entry). **What's next: `TODO.md`**.

The board side (tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page,
console) and the 68000 core (Musashi) are the shared library in
`../esp32emu`; its CLAUDE.md has the runtime and the board-level lessons.
Changes there need all its projects built. A Raspberry Pi 5 sibling
(`rpi5ql`) is planned: keep `components/ql` platform independent.

The machine is our own (MIT), written from Sinclair's documentation (the QL
Technical Guide) and the QL community's; emulators (MAME's ql.cpp and
trumpcard.cpp are BSD-3-Clause; sQLux, uQLx, QLay are GPL) only to compare
behaviour against, never copied. The research notes: `docs/ql-hardware.md`.

## Build, flash, debug (the Linux PC)

```sh
# Tab5 (/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:FB:03:03-if00)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build'
# ... -p PORT flash (writes the roms/ too), app-flash for the app only
# MaTouch (its UART port: /dev/serial/by-id/usb-1a86_USB_Serial-*, 921600)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; idf.py build && idf.py -p PORT flash'
```

- The two builds share `managed_components/`: build one after the other,
  never both at once.
- `flash` writes the ROMs into 64 KB slots of `ql_roms` (0x810000): js.rom
  slot 0, minerva.rom slot 1 (48822 bytes: the rest reads $FF),
  trumpcard.rom slot 2 (32 KB). A file of the same name in the SD card's
  `ql/roms` overrides one; cartridges (`.mdv`) and floppy disks (`.img`)
  are in `ql/`.
- NVS: namespace `ql` (`rom`, `memory`, `floppy`), drives `qldisks`. mDNS
  `sinclair-ql.local` (the second board running it: `sinclair-ql-2.local`;
  `/api/info`'s `board` says which). Drives in the web API: 1 MDV1_, 2
  MDV2_, 3 FLP1_, 4 FLP2_; a blank floppy: `curl -X POST
  ".../api/new?size=737280"`.
- The MaTouch: use its UART port, not native USB (native USB's lines are
  the speaker amplifier's: any traffic screeches through it). The S3 build
  has the speaker on, so its console is on the UART at 921600 once the
  machine starts; opening that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | screenshot
  F.png | send "cmd" | info` with the IDF venv's Python (`-I`; `lcd` needs
  a `sips` stand-in on PATH on Linux). Start on the setup screen: `t 1065
  637` (Tab5), `t 850 532` (MaTouch).

Console commands (`main/main.c`): `k <n>` tap key n (`ql.h`'s `QL_KEY_*`:
57 F1, 59 F2, 48 ENTER, 54 SPACE), `K <n> 0|1`, `T <text>` (`\n` ENTER; a
key every 3 frames), `R <rom> [memory] [floppy]` restart with ROM 0 JS /
1 Minerva, memory 0 128 KB / 1 640 KB, floppy 0 none / 1 the Trump Card
(only once the machine runs: on the setup screen tap the rows). From the
setup screen to SuperBASIC: Start, `k 57` (F1), wait ~10 s (the empty
`mdv1_boot` attempt), `T PRINT 2+2\n`. The setup rows (Tab5): ROM y 227,
Memory 298, Floppy 369 (two buttons at x 426, 781); MaTouch: ROM 193,
Memory 253, Floppy 313 (x 341, 626).

## Layout

| Path | What |
|---|---|
| `main/main.c` | The QL as an `emu_machine_t`: memory, start, setup rows (ROM, Memory, Floppy), the drives (MDV1_, MDV2_, FLP1_, FLP2_), status, console |
| `main/keymap.c` | The board's keyboards (HID usages) to the QL's keys, by position |
| `main/rom_store.c` | The ROMs from their partition slots, or the SD card's `ql/roms` |
| `main/settings.c` | NVS `ql` |
| `main/web/qlfiles.js` | The web page's packer (served as /machine.js): dropped QL files and zips onto a new cartridge and floppy disk |
| `components/ql/` | The machine, platform independent (`include/ql.h`) |
| `components/ql/mem.c` | The 68008's memory map and bus timing (Musashi's callbacks) |
| `components/ql/ql.c` | Set up, reset, the frame loop, interrupts, the debugger |
| `components/ql/video.c` | The ZX8301's picture: modes 4 and 8, flashing, snapshots for the presenter |
| `components/ql/io.c` | On-board I/O at $18000: the ZX8302 (interrupts, the clock, the Microdrives' select and gap: empty drives so far), MODE |
| `components/ql/ipc.c` | The IPC (8049) at the protocol level: the link, the commands, the keyboard matrix and queue, typed text |
| `components/ql/sound.c` | The sound ring (two samples a line, 31250 Hz; it paces the emulation) |
| `components/ql/mdv.c` | The Microdrives: QLay images on a tape loop of gaps and records, the receiver and transmitter, writes back into the image |
| `components/ql/flp.c` | The Trump Card's WD1772 and drive latch; 720 KB sector images (`.img`) |
| `components/ql/host/` | Host build: `make -C components/ql/host run`, switches in host_main.c (`QL_IO=1` logs every I/O access) |
| `tools/mdvcheck.py` | A QLay image's slots, syncs and checksums, sector numbers, the map, the directory |
| `tools/mdvtool.py` | `ls`, `add`: files onto a QDOS-formatted cartridge image (from zips with their QDOS headers: executables keep their type and data space; blocks spaced along the tape as QDOS spaces them) |
| `tools/qlfiles_test.js` | qlfiles.js's test: `node tools/qlfiles_test.js [out folder] [x.zip ...]`: zips packed as the page does, both images read back and compared |
| `docs/ql-hardware.md` | The hardware research (2026-11-18): every register, the IPC, the Microdrive, the boot, with sources |
| `roms/` | The ROMs (not in git; README.md says where they come from) |

## The machine

- **Time**: the 68008's clocks; a line is 480 (64 us, 15625 lines a second),
  a frame 312 lines (50.08 Hz). The frame loop runs the CPU a line at a time
  (the overshoot carried into the next), then `ql_io_line()`.
- **Memory** (`mem.c`): 20 address lines (1 MB, wrapping); 64 pages of
  16 KB to host pointers (ROM, RAM), NULL to the slow path (I/O at
  $18000-$1BFFF, the Trump Card's below; everything else reads 0, ignores
  writes). Bytes in the
  68008's order. A word access costs 4 clocks more than Musashi counts, a
  long 8 (the 68008's second byte cycle), taken from Musashi's remaining
  cycles. No ZX8301 contention yet.
- **Display** (`video.c`): mode 4: a word is 8 pixels, the even byte green,
  the odd byte red (black, red, green, white); mode 8: a word is 4 pixels,
  G F pairs and R B pairs, F toggling flashing to the line's end (the flash
  colour that of the pixel starting it). MODE bit 3 mode 8, bit 7 the second
  screen ($28000), bit 1 blank. Frames that changed the screen are copied
  into one of three snapshots; the presenter draws the latest. Checked: the
  JS ROM's F1/F2 screen in the right colours.
- **Interrupts**: the ZX8302's at level 2, autovectored. $18021 read: bit 0
  gap, 1 interface, 2 transmit, 3 frame, 4 external (QDOS's handler at JS
  $35E tests them in that order); write: bits 0-4 clear those, bits 5-7
  enable gap, interface, transmit (QDOS keeps $C0 in sv_pcint, ORs in $20
  while a Microdrive runs); frame and external always interrupt. The frame
  interrupt at each frame's start.
- **The IPC link** (`ipc.c`, read from the JS ROM's own routines at
  $2F6E-$2FAC, `QL_DASM`): a bit to the IPC is a write to $18003, $0C for 0,
  $0E for 1, then QDOS waits while $18020 bit 6 is set (never, here); a bit
  back: write $0E, read $18020 bit 7. Commands are a nibble, MSB first;
  their parameter lengths are the IPC's to know (`params_for()`: 9 keyrow
  4, 10 sound 64, 13 baud 4, 15 test 8); then the reply bits as asked. Any
  other value written to $18003 (the ROM's $01 at reset) restarts the link.
  QDOS polls command 1 (status, 8 bits: bit 0 keys, 1 sound, 4/5 serial
  input) about every 5 frames; for bit 0 it sends 8: a nibble (bits 0-2 the
  keys that follow, bit 3 a key still held: QDOS repeats the last one
  itself), then per key a nibble of modifiers (bit 0 ALT, 1 CTRL, 2 SHIFT)
  and a byte, its code. MT.IPCOM (JS $2C72) sends any command from a
  parameter block.
- **The keyboard** (`ql.h`'s `QL_KEY_*`): the codes are the JS ROM's key
  translation table's (vector at $BFF2 -> $B352, tables at $B458: per
  modifier, codes 3-63), so they are the IPC's; 0-2 are SHIFT, CTRL, ALT
  (reported as modifiers; their order in KEYROW's row 7 not checked yet).
  KEYROW(r) reads code row 7 - r (row 7: F4 F1 5 F2 F3 F5 4 7, as the
  manual's KEYROW(0)). Keys from other tasks go through an event ring to
  the emulation task (`ql_ipc_frame()`); a key down queues it with the
  modifiers held then. Typed text: a character every 3 frames, from tables
  of the ROM's plain and shifted characters.
- **Microdrives** (`mdv.c`; docs/ql-hardware.md 6): cartridges are QLay
  images (174930 bytes, 255 slots of 686), kept in memory, written back
  to the card 2 s after the last write (`every_second()`). Each drive's
  tape loop plays the slots in file order: GAP_A (69 byte-times of 40 us),
  the header record (the image's $000-$01B), GAP_B (69), the block record
  ($01C on, 624 bytes: preamble, file/block/checksum, the data's preamble,
  512 bytes, checksum, then the image's filler), 790 byte-times a slot,
  8.1 s a turn; it moves while its drive is selected (`ql_clock()`). A
  slot whose record has no `$FF $FF` in its preamble is gap (erased tape:
  a blank cartridge). $18020 write: bit 1 the select clock (the select bit,
  bit 0, shifts in as it falls), bit 2 write, bit 3 erase; `$02` arms the
  receiver (it syncs on the next `$FF $FF` from the head, in this record or
  the next). Read: bit 3 gap (also with no drive or cartridge), bit 2 a
  byte ready (the tape has carried it past), bit 1 transmit full (40 us a
  byte). A write burst (bit 2 up to down) goes back into the image: a short
  one with a header after its sync is the header of the slot under the
  head (FORMAT), a long one the block of the slot whose header passed last.
  The gap interrupt: as a gap starts, and when its mask is written as 1
  during a gap (docs 3.2: the JS ROM needs it with an empty drive).
  Checked on the host (JS and Minerva): FORMAT of a blank cartridge
  ("230/251 sectors", Minerva "229/233"), SAVE, NEW, LOAD, RUN, DIR; Psion
  Quill 2.35 boots from its cartridge, a document saved and loaded again
  in a fresh Quill; on the Tab5 Quill from the SD card and a document saved
  (read back from the downloaded image); the MaTouch boots Quill.
- **The Trump Card** (setup row Floppy, on by default; `ql_config_t`'s
  `trump_rom`; MAME's trumpcard.cpp for the map): its 32 KB ROM (V1.31:
  the FLP_ driver, RAM disks, Toolkit 2.23) at $10000-$17FFF; a read of
  $0C000-$0FFFF (QDOS's ROM probe) also shows it at $C0000-$C7FFF, a read
  of $10000-$17FFF hides it again (`trump_map()`: while shown, the $10000
  pages take the slow path to see that read). A WD1772 at $1C000-$1C003
  (status/command, track, sector, data), a write-only latch at $1E000
  (bit 1 FLP1_, bit 0 FLP2_, bit 2 motor, bit 3 side); no interrupts, the
  driver polls. The banner: "Miracle Systems V1.31 ... Trump Card 640K
  RAM, CARE/QJUMP TK2.23". The card boots `flp1_boot` when a disk is in
  FLP1_ (a disk without one: no `mdv1_boot` either), else `mdv1_boot`.
  Toolkit 2's commands (WCOPY, WDIR, ...) come after `TK2_EXT` (before it
  "bad name", as the card's manual has boot files start with it);
  `FLP_USE mdv` makes MDV1_ mean FLP1_ (Quill's boot runs `EXEC_W
  MDV1_quill`).
- **The web page's packer** (`main/web/qlfiles.js`, esp32emu's
  `registerPacker`): files dropped on the page (loose, in folders, in zips)
  go onto a new cartridge (`<name>.mdv`, when they fit in its 254 sectors)
  and a new 720 KB disk (`<name>.img`), both uploaded. A zip entry's QDOS
  header (type, an executable's data space, its QDOS name) comes from its
  extra field (ID $FB4A, the last 64 bytes; the page passes each entry's
  `extra` since 2026-11-18), else a data file's, the path with `/` and `.`
  as `_`. The cartridge: as a good FORMAT leaves one (a real cartridge's
  layout, quill-before.mdv from the collection: all 255 sectors, sector 0
  in the first slot, then 254 down; free blocks $FD with FORMAT's $AA55
  long block and $5A filler), the map in sector 0, the directory file 0,
  each file's blocks 13 slots apart as QDOS has them. The disk: the Trump
  Card's QL5A layout (below), files from block 1 on (the directory first).
  Checked: `tools/qlfiles_test.js` (made-up files, Quill's zip); in the
  emulator QDOS and the card's driver take both: DIR ("TEST 244/255
  sectors", "qui235m 1227/1440 sectors", the same free count WCOPY left),
  SAVE onto them and LRUN back in a fresh run, Quill from either; the
  page's own script under node (stub DOM and fetch: the zip through its
  `unzip()`, two uploads); on the Tab5 the uploaded pair: the boot program
  from the disk loading Quill from the cartridge; both boards serve
  /machine.js.
- **QDOS's floppy format (QL5A)**, read from disks the Trump Card wrote:
  80 cylinders of 2 x 9 sectors of 512, blocks of 3 sectors. Logical
  sector l: cylinder l / 18; the table at $28 (18 bytes, bit 7 the side,
  then the sector - 1: 00 03 06 80 83 86 01 04 07 ...) gives its place in
  the cylinder, turned by the skew ($26: 5) a cylinder: physical sector
  `(t + 5 * c) mod 9`. Block 0 (logical 0-2): "QL5A", the name (10), the
  random number, updates (4), free/good/total sectors, 9, 18, 80, 3, the
  directory's end (sectors, bytes: its length), the skew, the table and
  its inverse ($3A), $FF to $5F, then 480 entries of 3 bytes: file (12
  bits) and block (12), $FDF/$FFF free, $F80 the map. The directory is
  file 0 (its first 64 bytes unused); files start with their 64-byte
  header as on a cartridge.
- **The WD1772** (`flp.c`): runs on its own clock: a sector is found 1 ms
  after the command, then a byte every 32 us (DRQ); a byte the CPU didn't
  take (or give) by the next is lost data; Read Address puts the ID's track
  in the sector register at its end; steps 3 ms; Write Track takes 6250
  bytes ($F5 a sync, $F6 an index mark, $F7 the CRC; $FE an ID mark, $FB a
  data mark) and stores the sectors it finds. Type I status: track 0 from
  the head with or without a disk, index pulses (4 ms a turn of 200 ms)
  and write-protect (no disk: protected). The motor bit for ten turns after
  a command. Images: raw sectors, track by track, side 0 then 1, 512
  bytes (720 KB: 80 x 2 x 9; 360 KB and 800 KB by size), in memory (800 KB
  each, PSRAM), written back 2 s after the last write. Each track keeps
  the track number its IDs were formatted with (`id_track`). Checked on
  the host (JS and Minerva, 128 and 640 KB): FORMAT of a blank image
  ("1440/1440 sectors"), SAVE, DIR, LOAD and RUN in a fresh run, WCOPY of
  Quill's cartridge to a disk, Quill booting from it (its editor 8 s after
  F1, from the cartridge ~37 s), a document saved and loaded in a fresh
  Quill.
  On the Tab5 (2026-11-18): the banner, FORMAT of a blank made through the
  web API, a program saved (the downloaded image LRUNs on the host), Quill
  from its disk (4 s after F1), a document saved into the image; on the
  MaTouch the same Quill disk, a document saved (core 1 59%).

## Lessons

- **Internal names need the `ql_` prefix**: the core's `mem_init()` linked
  against lwIP's of the same name on the board ("multiple definition").
- **The runtime paces a machine by its sound**: with the speaker on it
  waits while two frames of samples are queued; a machine that queues none
  runs as fast as it can (the QL ran at 490%). `sound.c` queues silence
  until there's sound.

- **FORMAT's long block is 624 bytes on tape** (a preamble, 610 bytes
  from $261 + 1, a checksum: JS $51A6) and its verify compares all 610
  after the first sync (JS $5354): with the block record cut at the QLay
  layout's 538 bytes the 599th byte never came and every FORMAT "failed"
  (`QL_STOP=5180` found it; the verify's exits at JS $50C8-$50E6).
- **A blank cartridge must read as gap**: with zeros as data records
  QDOS armed, never synced, and retried for ever (SuperBASIC never came);
  erased tape is gap all the way, so it gives up as with no cartridge.
- **Danish Quill (2.24 DK) won't save on a JS ROM** ("Dette program kan
  kun køre på en dansk QL"): use the English 2.35 (`qui235m.zip`; the
  djw site's ready `quill.mdv` answered HTTP 500 on 2026-11-18).
- **The WD1772 doesn't wait for the CPU**: the Trump Card's FORMAT tells
  one side from two by writing side 1 of track 0 with IDs saying track 1,
  then side 0, then a Read Address on side 1 whose bytes it never reads:
  it waits for busy to drop and takes the sector register (the ID's track).
  A controller that held each byte until the CPU took it never finished
  that command (forced off after 4 s) and the disk came out single-sided
  ("720/720 sectors"); and IDs made up from the head's position said track
  0 there. Hence its own byte clock with lost data, and `id_track`.
- **mdvtool put a file's blocks one slot behind each other**: sector
  numbers count down along the tape, and it gave a file free sectors
  upwards, so a channel reading the file in order (COPY, WCOPY) waited
  nearly a turn (8 s) for each block (52 KB copied in over 13 minutes).
  LOAD and EXEC read a whole file in any order, so Quill itself loaded
  fine. QDOS puts each next block 13 slots further on (the collection's
  cartridges): mdvtool does too now (the same COPY in ~70 s).
- **The page dropped a zip's extra fields**: the QL's zips keep each
  file's QDOS header there, so an executable from a zip lost its type and
  data space on the way to the packer (Quill would have become a data
  file: "9 QL files" without "1 executable" in the page's log). esp32emu's
  `unzip()` passes `extra` now. And a floppy's sectors aren't contiguous
  logically: block 0's map is in physical sectors 1, 4 and 7, and files
  from cylinder 1 on are turned by the skew; reading the image in order
  gave garbage, locating each block by content found the rule.
- **Read the ROM before guessing**: the JS ROM's own routines answered
  the IPC protocol, the keyboard codes and the Microdrive registers exactly
  (`QL_IO=1` for who touches which register, `QL_DASM` to read the code,
  `QL_STOP` for how it got there, `QL_DUMP` for QDOS's tables: the job
  table showed SuperBASIC waiting in the Microdrive code).

## Testing

- Host: `make -C components/ql/host run` (the JS ROM, 500 frames): the
  F1/F2 screen. `QL_LOG=50` shows the PC each 50 frames. SuperBASIC:
  `QL_KEYS="150:57:1;155:57:0" QL_TYPE='500:PRINT 2+2\n'
  ./build/ql_host <rom> 900 out.ppm` (F1 at frame 150): 4 in #1; the same
  with minerva.rom (its start screen shows the clock's date and time).
- Boards (2026-11-18): the JS ROM to its F1/F2 screen at 100% on both (Tab5
  core 1 ~21%, MaTouch ~64%); F1 and `PRINT 2+2` (console `k 57`, `T`) on
  both: 4; SuperBASIC's circles in mode 8 on the Tab5 (the README's
  picture). Not yet by hand with a Bluetooth or USB keyboard.
- Microdrives on the host: `head -c 174930 /dev/zero > blank.mdv`,
  `QL_MDV1=blank.mdv QL_KEYS="150:57:1;155:57:0" QL_TYPE='600:FORMAT
  mdv1_test\n'` (3000 frames), then `tools/mdvcheck.py blank.mdv`. Quill:
  a cartridge from `qui235m.zip` (`tools/mdvtool.py add <formatted>.mdv
  qui235m.zip`) in MDV1_, F1: Quill's editor by frame ~2000; F3 then S
  (`QL_KEYS` 60, 35) and a name to save, F3 then L (24) to load.
- Floppies on the host: `head -c 737280 /dev/zero > blank.img`,
  `QL_TRUMP=roms/trumpcard.rom QL_RAM_KB=640 QL_FLP1=blank.img
  QL_KEYS="600:57:1;603:57:0" QL_TYPE='800:FORMAT flp1_test\n'` (4000
  frames: "1440/1440 sectors"; F1 after the 640 KB RAM test, frame ~550).
  `QL_FLP_LOG=1` prints each WD1772 command and the IDs a Write Track
  writes. Quill on a disk: `TK2_EXT`, `WCOPY mdv1_ TO flp1_`, `A`; its boot
  needs `FLP_USE mdv` first, or its line 8 as `EXEC_W flp1_quill`.
- The web page's packer: `node tools/qlfiles_test.js out qui235m.zip`
  writes `out/qui235m.mdv` and `.img` (to boot with `QL_MDV1`, `QL_FLP1`).
- Microdrives on the boards: upload the image (`curl -T x.mdv
  http://sinclair-ql.local/disks/`), insert (`curl -X PUT --data "x.mdv"
  .../api/drive/1`), Start, `k 57`; the image back with `curl -o
  .../disks/x.mdv` (it may 404 for a moment while the board writes it).

## Working with the user

- Expects changes built (both boards), flashed and checked on the board,
  then a short report of what was verified and what wasn't. Flashing ends
  the session on the board: say so first.
- Keep their private data (WiFi name, IP, SD card contents) out of anything
  meant for publishing (the setup screen's status lines: cut them out).
