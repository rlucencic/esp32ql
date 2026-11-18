<h1 align="center">esp32ql</h1>

<p align="center">
  <b>A Sinclair QL on a 7-inch ESP32-S3 touch-screen board or an M5Stack Tab5.</b><br>
  A 68008 with the QL's 8-bit bus timing, the ZX8301's two display modes, the JS or Minerva ROM, 128 or 640 KB.
</p>

<p align="center">
  <img alt="ESP32-S3" src="https://img.shields.io/badge/ESP32--S3-N16R8-e7352c">
  <img alt="ESP32-P4" src="https://img.shields.io/badge/ESP32--P4-M5Stack%20Tab5-e7352c">
  <img alt="ESP-IDF" src="https://img.shields.io/badge/ESP--IDF-v5.5-blue">
  <img alt="LVGL" src="https://img.shields.io/badge/LVGL-9.4-5e48e8">
  <img alt="68008" src="https://img.shields.io/badge/CPU-68008-555">
  <img alt="MIT licence" src="https://img.shields.io/badge/licence-MIT-green">
</p>

<p align="center">
  <b>Early development:</b> QDOS, SuperBASIC, the keyboard and the Microdrives work on both
  boards; sound and floppy disks are next.
</p>

<p align="center">
  <img alt="Psion Quill 2.35 on the Tab5, loaded from a Microdrive cartridge image: a document typed and saved, Quill asking Same document?" src="docs/images/tab5-quill-saved.png" width="49%">
  <img alt="SuperBASIC in mode 8 on the Tab5: a FOR loop drawing 70 circles in the seven inks" src="docs/images/tab5-basic-circles.png" width="49%">
  <br>
  <img alt="SuperBASIC on the MaTouch: PRINT 2+2 typed in the command window, 4 in the output window" src="docs/images/matouch-basic-print.png" width="49%">
  <img alt="The setup screen on the Tab5: ROM JS or Minerva, Memory 128 KB or 640 KB, Sound" src="docs/images/tab5-setup.png" width="49%">
  <br>
  <sub>Psion Quill 2.35 from a Microdrive cartridge on the Tab5, a document just saved back to it;
  SuperBASIC drawing circles in mode 8 (256 × 256, eight colours); PRINT 2+2 on the MaTouch; the
  setup screen. Screenshots from the boards.</sub>
</p>

esp32ql turns the Makerfabs MaTouch ESP32-S3 Parallel TFT with Touch 7", or an
M5Stack Tab5, into a Sinclair QL. A setup screen on the touch panel, and a web
page on your network, handle the machine's settings and its Microdrive
cartridges on the microSD card.

It's the sibling of [esp32ste](https://github.com/rlucencic/esp32ste) (Atari
STE), [esp32mac](https://github.com/rlucencic/esp32mac) (Macintosh Plus, SE,
Classic), [esp32mac2](https://github.com/rlucencic/esp32mac2) (Macintosh LC
and II, Tab5 only), [esp32mac3](https://github.com/rlucencic/esp32mac3)
(Macintosh LC II and LC III, Tab5 only),
[esp32cpc](https://github.com/rlucencic/esp32cpc) (Amstrad CPC),
[esp32zx](https://github.com/rlucencic/esp32zx) (ZX Spectrum 128K, +2, +3),
[esp32cbm](https://github.com/rlucencic/esp32cbm) (Commodore 64 and 128),
[esp32xl](https://github.com/rlucencic/esp32xl) (Atari 800XL and 130XE),
[esp32msx](https://github.com/rlucencic/esp32msx) (MSX1 and MSX2),
[esp32gs](https://github.com/rlucencic/esp32gs) (Apple IIGS),
[esp32coco](https://github.com/rlucencic/esp32coco) (Tandy Color Computer 2
and 3), [esp32arc](https://github.com/rlucencic/esp32arc) (Acorn Archimedes,
Tab5 only), [esp32pc](https://github.com/rlucencic/esp32pc) (IBM PC/XT and
PC/AT) and [esp32ami](https://github.com/rlucencic/esp32ami) (Amiga 500,
2000, 600 and 1200, Tab5 only); all are built on the shared runtime
[esp32emu](https://github.com/rlucencic/esp32emu), which also holds the 68000
core (Musashi) this one shares with the ST, the Macs and the Amiga.

## Status

**Working**
- The 68008: Musashi's 68000 with the QL's 8-bit bus (a word costs two byte
  cycles), 20 address lines, 7.5 MHz, a 50 Hz PAL frame of 312 lines.
- Memory: the 48 KB ROM, 128 KB RAM, or 640 KB with a 512 KB expansion.
- The ZX8301's display: mode 4 (512 × 256, four colours) and mode 8 (256 ×
  256, eight colours, flashing), the two screen banks, blanking.
- The ZX8302's interrupts (frame, gap, interface, with their masks), its
  clock and its link to the IPC.
- The IPC (the 8049) at the protocol level: the keyboard (keys, SHIFT,
  CTRL, ALT, QDOS's autorepeat, KEYROW) and its status.
- The keyboard: Bluetooth and USB keyboards (the Tab5 Keyboard too) by
  position, Backspace and Delete as the QL's CTRL + ← and CTRL + →.
- The JS ROM (QDOS 1.10) and Minerva 1.98 boot to SuperBASIC on both boards
  at full speed (the Tab5's emulation core ~22% busy, the MaTouch's ~64%).
- **Microdrives**: MDV1_ and MDV2_ with cartridge images on the SD card
  (QLay `.mdv`, 174930 bytes, the format of the QL software collections),
  a tape loop with its gaps and records under the ZX8302's control:
  FORMAT (a blank cartridge from the setup screen or web page gives ~230
  sectors, as a real one does), SAVE, LOAD, DIR, and cartridges booting
  their `BOOT` program. Psion Quill 2.35 loads from its cartridge, and a
  document saved in it comes back. Written cartridges go back to the card
  two seconds after the last write.
- `tools/mdvtool.py` puts QL software from a zip (with the QDOS headers QL
  zips keep) onto a cartridge image; `tools/mdvcheck.py` checks one.

**Next**, in order: floppy disks, QL zips turned into cartridges on the web
page, sound.

## Hardware

- **Makerfabs MaTouch ESP32-S3 Parallel TFT with Touch 7"** (ESP32-S3,
  16 MB flash, 8 MB PSRAM, 1024 × 600), or
- **M5Stack Tab5** (ESP32-P4, 1280 × 720).
- A microSD card (FAT32 or exFAT) for the Microdrive cartridges, a Bluetooth
  keyboard (the Tab5: USB too).

## Getting started

You need ESP-IDF 5.5 and [esp32emu](https://github.com/rlucencic/esp32emu)
cloned next to this repository.

1. Put the ROM images into `roms/` (`js.rom`, `minerva.rom`: see
   [roms/README.md](roms/README.md)).
2. Build and flash (`flash` also writes the ROMs into their partition):
   ```sh
   # MaTouch
   idf.py build && idf.py -p PORT flash
   # Tab5
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig build
   idf.py -B build-tab5 -DIDF_TARGET=esp32p4 -DSDKCONFIG=$PWD/build-tab5/sdkconfig -p PORT flash
   ```
3. On the setup screen choose the ROM and memory and tap Start. The web page
   is at `http://sinclair-ql.local`.
4. Answer the QL's question with F1 (monitor) or F2 (TV) on a Bluetooth or
   USB keyboard; SuperBASIC follows (it first looks for `mdv1_boot`: a
   cartridge with one in MDV1_ starts its program).
5. Cartridges: copy `.mdv` images into the SD card's `ql` folder, or upload
   them on the web page; insert them in MDV1_ or MDV2_ from the setup
   screen's Drives page or the web page. A new, blank cartridge needs
   `FORMAT mdv1_name` first. To put QL software from a zip on one:
   ```sh
   tools/mdvtool.py add formatted.mdv software.zip
   ```

## Development

The machine (`components/ql`) is platform independent; `components/ql/host`
builds it for a PC, far faster than real time:

```sh
make -C components/ql/host run          # 500 frames of the JS ROM, build/screen.ppm
QL_IO=1 ./components/ql/host/build/ql_host roms/js.rom 200 out.ppm   # every I/O access
```

`host_main.c` lists its other switches (traces, a stop at a PC, typed
text, keys, memory dumps, disassembly of the ROM).

## Credits

- The 68000 core: [Musashi](https://github.com/kstenerud/Musashi) by Karl
  Stenerud (MIT), in esp32emu.
- The QL's ROMs: Sinclair Research's QDOS and SuperBASIC (Tony Tebby, Jan
  Jones and others); Minerva by Laurence Reeves. Not part of this
  repository: see roms/README.md.
- The QL's hardware as described in Sinclair's QL Technical Guide and the
  QL community's documentation (Dilwyn Jones's site, sinclairql.net).

## Licence

MIT, see [LICENSE](LICENSE). The ROMs are not covered by it.
