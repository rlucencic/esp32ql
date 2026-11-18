# esp32ql: a Sinclair QL on the MaTouch ESP32-S3 7" and the M5Stack Tab5

A Sinclair QL (1984): a 68008 at 7.5 MHz, 128 KB RAM (640 KB with an
expansion), 48 KB ROM (Sinclair's JS, QDOS 1.10, or Minerva 1.98), the
ZX8301 (display, DRAM), the ZX8302 (clock, interrupts, Microdrives, serial
out, the link to the IPC), the IPC (an 8049: keyboard, sound, serial in) and
two Microdrives. Both boards (the user's call, 2026-11-18). The plan:
esp32emu's CLAUDE.md, "To do" (the QL's entry). **What's next: `TODO.md`**.

The board side (tasks, LCD, touch, SD, WiFi, BLE, setup screen, web page,
console) and the 68000 core (Musashi) are the shared library in
`../esp32emu`; its CLAUDE.md has the runtime and the board-level lessons.
Changes there need all its projects built. A Raspberry Pi 5 sibling
(`rpi5ql`) is planned: keep `components/ql` platform independent.

The machine is our own (MIT), written from Sinclair's documentation (the QL
Technical Guide) and the QL community's; emulators (MAME's ql.cpp is
BSD-3-Clause; sQLux, uQLx, QLay are GPL) only to compare behaviour against,
never copied.

## Build, flash, debug (the Linux PC)

```sh
# Tab5 (/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_60:55:F9:FB:03:03-if00)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; \
  idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build'
# ... -p PORT flash (writes roms/js.rom and minerva.rom too), app-flash for the app only
# MaTouch (its UART port: /dev/serial/by-id/usb-1a86_USB_Serial-*, 921600)
bash -c '. ~/.espressif/tools/activate_idf_v5.5.6.sh >/dev/null 2>&1; idf.py build && idf.py -p PORT flash'
```

- The two builds share `managed_components/`: build one after the other,
  never both at once.
- `flash` writes the ROMs into 64 KB slots of `ql_roms` (0x810000): js.rom
  slot 0, minerva.rom slot 1 (48822 bytes: the rest reads $FF). A file of
  the same name in the SD card's `ql/roms` overrides one; cartridges are in
  `ql/` (`.mdv`).
- NVS: namespace `ql` (`rom`, `memory`), drives `qldisks`. mDNS
  `sinclair-ql.local` (the second board running it: `sinclair-ql-2.local`).
- The MaTouch: use its UART port, not native USB (native USB's lines are
  the speaker amplifier's: any traffic screeches through it). The S3 build
  has the speaker on, so its console is on the UART at 921600 once the
  machine starts; opening that port resets the board.
- `../esp32emu/tools/emu_console.py PORT log N | lcd F.png | screenshot
  F.png | send "cmd" | info` with the IDF venv's Python (`-I`; `lcd` needs
  a `sips` stand-in on PATH on Linux). Start on the setup screen: `t 1065
  637` (Tab5), `t 850 532` (MaTouch).

Console commands (`main/main.c`): `k <n>` tap matrix key n (row * 8 +
column), `K <n> 0|1`, `T <text>` (`\n` ENTER), `R <rom> [memory]` restart
with ROM 0 JS / 1 Minerva, memory 0 128 KB / 1 640 KB. (Keys do nothing
until the IPC is there.)

## Layout

| Path | What |
|---|---|
| `main/main.c` | The QL as an `emu_machine_t`: memory, start, setup rows (ROM, Memory), Microdrives, status, console |
| `main/keymap.c` | The board's keyboards to the QL's matrix (a stub until the IPC) |
| `main/rom_store.c` | The ROMs from their partition slots, or the SD card's `ql/roms` |
| `main/settings.c` | NVS `ql` |
| `components/ql/` | The machine, platform independent (`include/ql.h`) |
| `components/ql/mem.c` | The 68008's memory map and bus timing (Musashi's callbacks) |
| `components/ql/ql.c` | Set up, reset, the frame loop, interrupts, the debugger |
| `components/ql/video.c` | The ZX8301's picture: modes 4 and 8, flashing, snapshots for the presenter |
| `components/ql/io.c` | On-board I/O at $18000 (a first version: the frame interrupt, the clock, MODE) |
| `components/ql/sound.c` | The sound ring (two samples a line, 31250 Hz; it paces the emulation) |
| `components/ql/host/` | Host build: `make -C components/ql/host run`, switches in host_main.c (`QL_IO=1` logs every I/O access) |
| `roms/` | The ROMs (not in git; README.md says where they come from) |

## The machine

- **Time**: the 68008's clocks; a line is 480 (64 us, 15625 lines a second),
  a frame 312 lines (50.08 Hz). The frame loop runs the CPU a line at a time
  (the overshoot carried into the next), then `ql_io_line()`.
- **Memory** (`mem.c`): 20 address lines (1 MB, wrapping); 64 pages of
  16 KB to host pointers (ROM, RAM), NULL to the slow path (I/O at
  $18000-$1BFFF; everything else reads 0, ignores writes). Bytes in the
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
- **Interrupts**: the ZX8302's at level 2, autovectored; the frame interrupt
  at each frame's start (status $18021 bit 3, cleared by writing the bit).

## Lessons

- **Internal names need the `ql_` prefix**: the core's `mem_init()` linked
  against lwIP's of the same name on the board ("multiple definition").
- **The runtime paces a machine by its sound**: with the speaker on it
  waits while two frames of samples are queued; a machine that queues none
  runs as fast as it can (the QL ran at 490%). `sound.c` queues silence
  until there's sound.

## Testing

- Host: `make -C components/ql/host run` (the JS ROM, 500 frames): the
  F1/F2 screen. `QL_LOG=50` shows the PC each 50 frames.
- Boards (2026-11-18): the JS ROM to its F1/F2 screen at 100% on both (Tab5
  core 1 ~21%, MaTouch ~64%).

## Working with the user

- Expects changes built (both boards), flashed and checked on the board,
  then a short report of what was verified and what wasn't. Flashing ends
  the session on the board: say so first.
- Keep their private data (WiFi name, IP, SD card contents) out of anything
  meant for publishing (the setup screen's status lines: cut them out).
