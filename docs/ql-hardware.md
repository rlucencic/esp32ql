# Sinclair QL hardware, for a clean-room emulator core

Research notes, 2026-11-18. Every fact carries a source tag (key in
[Sources](#sources)). Marks used:

- **[confirmed]**: two or more independent sources agree.
- **[one source]**: a single source, not contradicted.
- **[derived]**: worked out here from code or cycle counts, not stated
  anywhere.
- **[UNCERTAIN]**: sources disagree, or nothing settles it.

GPL sources (sQLux, uQLx, QLay, Minerva) were read only to confirm
behaviour; nothing from them is quoted beyond a word or two. MAME is
BSD-3-Clause.

### Summary

Certain (several independent sources agree): the memory map and
register addresses; the $18021 interrupt bits and masks, level 2
autovector; the $18063 bits 1, 3, 7 and the pixel formats; 312 lines
of 64 us, 256 visible, 50.08 Hz; the IPC link as the 68008 drives it
($0C/$0E to $18003, busy in $18020 bit 6, data in bit 7, MSB first);
the IPC command numbers and bit counts; the keyboard matrix and key
codes (code = 8 x (7 - row) + bit); the microdrive registers, record
layout and checksum; the QLay .mdv format; the 68008 8-bit timing rule;
what the JS ROM needs to reach the SuperBASIC cursor.

Uncertain (marked in the text): CPU contention outside the 256
picture lines; the flash period; $18063 bit 6 (NTSC); $18021 bits 5-7
on read and what raises the interface interrupt; whether CTRL-ALT-7 is
level 5 or 7; the BEEP duration unit (the manual's 72 us vs 44-66 us
from the code) and all sound timings (cycle counts, not measured); the
frame interrupt's position (one measured source); single bytes vs byte
pairs on the microdrive receiver.

Contents

1. [Memory map](#1-memory-map)
2. [ZX8301: display and DRAM](#2-zx8301-display-and-dram-controller)
3. [ZX8302: peripheral registers](#3-zx8302-peripheral-controller)
4. [The IPC (8049) and the keyboard](#4-the-ipc-8049-as-the-68008-sees-it)
5. [Sound](#5-sound)
6. [Microdrives](#6-microdrives)
7. [QDOS boot (JS) and Minerva](#7-qdos-boot-js-rom-and-minerva)
8. [68008 timing](#8-68008-timing)
9. [Software to test with](#9-software-to-test-with)
10. [Floppy disks: the Trump Card](#10-floppy-disks-the-trump-card)
11. [Sources](#sources)

---

## 1. Memory map

The 68008 in the QL is the 48-pin DIP: 20 address lines (1 MB, A0-A19),
8-bit data bus [TG 2325 "one megabyte of address space"; MAME ql.cpp
`ql_mem` maps $000000-$0FFFFF]. Addresses wrap at 1 MB: an emulator
built on a 24- or 32-bit 68000 core must mask addresses to 20 bits
(Minerva's ROM scan runs until the address wraps, BOOT report 8).

TG's map (TG 2329-2345) [confirmed by MAME `read`/`write`]:

| Range | Size | Contents |
|---|---|---|
| $00000-$0BFFF | 48K | on-board ROM (QDOS + SuperBASIC): IC33 32K at $0000, IC34 16K at $8000 (MAME ROM_LOAD) |
| $0C000-$0FFFF | 16K | plug-in ROM cartridge port; recognised by the long $4AFB0001 at $0C000 [TG 635-640] |
| $10000-$1FFFF | 64K | on-board I/O, partially decoded; registers at $18000-$18003, $18020-$18023, $18063 |
| $20000-$27FFF | 32K | screen 0 (on-board RAM) |
| $28000-$2FFFF | 32K | screen 1 / QDOS system variables (SV_BASE = $28000) |
| $30000-$3FFFF | 64K | on-board RAM (128K in all, $20000-$3FFFF) |
| $40000-$BFFFF | 512K | add-on RAM (expansion connector) |
| $C0000-$DFFFF | 128K | peripheral cards: 8 slots of 16K, card n at $C0000 + n x $4000, selected by A14-A17 = SP0-SP3 [TG 2740-2758] |
| $E0000-$FFFFF | 128K | add-on ROM |

Decoding facts that matter:

- **On a bare QL the ZX8301 ignores A18 and A19**, so $00000-$3FFFF
  repeats four times over the 1 MB [TG 2698 "An unexpanded QL does
  not look at address lines A19 and A18"; Nasta 875-877] [confirmed].
  Every address is acknowledged (the ZX8301 makes DTACK), so there are
  no bus errors.
- The ZX8301 decodes A17:A16 [Nasta 877-892]: 00 + read = ROM (DTACK =
  DS, no waits); 00 + write = DTACK only, the write does nothing; 01 =
  I/O; 1x = RAM (A16 picks the 64K bank, CAS0/CAS1).
- Expansion cards disable the on-board decode by pulling DSMCL high
  and supply their own DTACKL or VPAL [TG 2701-2712].
- I/O: the ZX8302 is selected over $18000-$1BFFF, address bits 13-7 and
  4-2 not decoded; on older QLs bit 6 is not decoded either, later
  ones decode mc_stat ($18063) by bit 6 = 1 [Minerva `inc/pc`]; the
  ZX8301 register aliases over 16K, 32K or the whole 64K depending on
  the ULA version [Nasta 1373-1381]. Use exactly $18000-$18003,
  $18020-$18023 and $18063; reads elsewhere are undefined (MAME
  returns 0).
- What unmapped space must return [BOOT report 2]: the JS ROM sizes
  RAM by writing the address to $40000, $50000, ... and stops at the
  first mismatch or alias. On a real 128K QL $40000 reads ROM ($00030000)
  and RAMTOP becomes $40000. Returning 0 or $FF also works (MAME: 0).
  **A mirror of on-board RAM at $40000 breaks it** (JS would size to
  $60000, Minerva to $48000). Recommended: ROM-mirror like the real
  board, or a constant; never mirror RAM there.
- With the real A18/A19 aliasing the cartridge ROM reappears at
  $CC000, and the JS ROM's expansion scan initialises a cartridge
  twice (Minerva skips duplicates) [BOOT report 3]; MAME doesn't
  mirror. Either works.
- Some ROMs need extra areas: the Danish QL has an extra ROM at $1C000
  (MAME ql.cpp:1181 comment); Minerva reads $BFDC-$BFDF (in ROM) as an
  I2C bit-bang port: on a plain QL these reads must just return ROM
  bytes [BOOT report 8].

## 2. ZX8301 display and DRAM controller

The ZX8301 ("master chip") makes the CPU clock, decodes addresses,
controls the 128K of DRAM and generates the picture [TG 2372; Nasta].

### 2.1 Clocks

- 15 MHz crystal; CPU clock = 15 MHz / 2 = **7.5 MHz**; pixel clock
  10 MHz (15 MHz / 1.5) = the mode 4 dot clock [Nasta 8-30, 1205; MAME
  ql.cpp X1 = 15 MHz, `M68008(config, m_maincpu, X1/2)`] [confirmed].
- [UNCERTAIN, same result] sQLux's `zx8301.c` models a 10.5 MHz pixel
  clock and 672 pixel clocks a line: also 64 us a line.

### 2.2 $18063 display control (MC_STAT), write only

| Bit | Name | 0 | 1 |
|---|---|---|---|
| 0 | | must be 0 | |
| 1 | MC..BLNK | display on | display blanked (black); the RAM is still read |
| 2 | | must be 0 | |
| 3 | MC..M256 | mode 4: 512 x 256, 4 colours | mode 8: 256 x 256, 8 colours + flash |
| 4, 5 | | must be 0 | |
| 6 | (MC..NTSC) | PAL | NTSC [UNCERTAIN: Minerva `inc/mc` names it "PAL625 / NTSC495"; Nasta says only bits 1, 3, 7 exist] |
| 7 | MC..SCRN | screen at $20000 | screen at $28000 |

Sources: TG 2419-2432, TG 6676-6680, Nasta 1373-1404, Minerva `inc/mc`,
MAME `zx8301_device::control_w` (bits 1, 3, 7) [confirmed for 1, 3, 7].
Reads return undefined data (MAME ignores them). JS writes $00 (mode 4)
during the RAM test and $08 (mode 8) before the F1/F2 prompt;
MT.DMODE writes SV_MCSTA [BOOT-W 222, 257].

### 2.3 Pixel format

Display memory starts at the screen base and runs in raster order,
128 bytes a line, 256 lines (32768 bytes). Each 16-bit word (even
byte first) holds 8 mode-4 pixels or 4 mode-8 pixels, the leftmost in
bit 7 [TG 2400-2414]:

| Mode | Even byte (A0 = 0) D7..D0 | Odd byte (A0 = 1) D7..D0 |
|---|---|---|
| 4 (512 px) | G7 G6 G5 G4 G3 G2 G1 G0 | R7 R6 R5 R4 R3 R2 R1 R0 |
| 8 (256 px) | G3 F3 G2 F2 G1 F1 G0 F0 | R3 B3 R2 B2 R1 B1 R0 B0 |

(index 7 = leftmost pixel, 0 = rightmost.) MAME's renderer reads the
same bits (`draw_line_mode4`: green = even byte bit 7, red = odd byte
bit 7; `draw_line_mode8`: G = even bit 7, F = even bit 6, R = odd bit
7, B = odd bit 6) [confirmed]. A mode 8 pixel is two mode 4 pixels
wide; Nasta (1781-1787) notes mode 8 is made from pairs of mode 4
pixels inside the chip.

Colours (digital RGB, one bit per gun) [UG Concepts "colour"; MAME
`PALETTE_ZX8301`, `ZX8301_COLOR_MODE4 = {0,2,4,7}`]:

| Mode 8 GRB | Colour | Mode 4 (G,R) | Colour |
|---|---|---|---|
| 000 | black | 0,0 | black |
| 001 | blue | 0,1 | red |
| 010 | red | 1,0 | green |
| 011 | magenta | 1,1 | white |
| 100 | green | | |
| 101 | cyan | | |
| 110 | yellow | | |
| 111 | white | | |

(TG 2387-2389 says that in mode 4 "the GREEN and BLUE signals are tied
together" but names the colours black, white, green and red; the User
Guide and MAME give green, not cyan, for G alone. Use black, red,
green, white.)

**Flash** (mode 8 only) [TG 2390-2395]: the F bit is a toggle along the
line. The first set F bit freezes the background colour at that pixel's
RGB and starts flashing "at the next bit in the line"; the next set F
bit stops it; flashing is cleared at the start of every raster line.
While the flash phase is on, the pixels in the flashing span show the
frozen colour, otherwise their own. MAME and sQLux agree on this
[confirmed]. **Flash period [UNCERTAIN]**: MAME toggles the phase
every 0.5 s (`m_flash_timer` 2 Hz); sQLux every 32 frames (frame
counter bit 5, 0.64 s); no primary source found.

### 2.4 Frame timing (PAL QL)

| Item | Value | Source |
|---|---|---|
| Line | 64 us = 640 pixel clocks = 480 CPU clocks | Nasta 225-274; sQLux `zx8301.c` |
| Visible pixels | 512 a line (51.2 us); 32 chunks of 16 pixels | Nasta 466-525 |
| Lines | 312 a frame, non-interlaced ("50 Hz 624-line non-interlaced" in TG terms) | Nasta 225-236; TG 2826-2828 |
| Visible lines | 256 | TG; Nasta 547 |
| Frame | 312 x 64 us = 19.968 ms = 50.08 Hz = 149 760 CPU clocks | Nasta 1000ff; MAME `set_refresh_hz(50.08)`; sQLux |
| Frame interrupt | ZX8301 VSYNCH goes to the ZX8302, which sets $18021 bit 3 "every time a vertical retrace begins" | Nasta 909-912, 1164-1167; MAME `vsync_wr_callback` |
| Interrupt to first visible line | 32 lines (measured with a copy loop) | sQLux `zx8301.c` (from the MiSTer QL core's ql_timing) [one source] |
| Line breakdown | 256 visible + 15 + 6 + 35 = 312 | sQLux `zx8301.c` [one source] |

MAME raises the frame interrupt from a 50 Hz timer not tied to the beam
(its TODO says "proper video timing").

NTSC [UNCERTAIN]: TG 2826-2835: US QLs use the same 50 Hz 312-line
signal in monitor mode, and a 60 Hz 524-line (262) signal in TV mode
with 192 usable lines. MAME `ql_us`: 60 Hz, 262 lines. sQLux: 262 lines
of 63.2 us (474 CPU clocks), 60.39 Hz. Minerva names $18063 bit 6
NTSC. For a UK machine, ignore NTSC.

### 2.5 Contention with the CPU

The ZX8301 owns the RAM bus; the CPU waits for it on every access to
the on-board RAM $20000-$3FFFF (both 64K banks, not only the screen)
[Nasta 418-430, 391]. ROM and expansion RAM run at full speed. On issue
5 and earlier boards the ZX8302 sits on the RAM data bus, so its
register accesses are contended too; later boards (with the HAL, IC38)
moved it to the CPU bus [Nasta 930-946; QLschem.txt].

Per line: 40 chunks of 16 pixels = 24 ZX8301 clocks = **12 CPU
clocks** each. In a busy chunk the ZX8301 reads 4 screen bytes (16 of
the 24 cycles) and leaves one CPU byte access (8 cycles = one 4-clock
68008 bus cycle); in a free chunk the CPU gets 3 accesses [Nasta
466-528]. RAM-bound code runs at about 46% speed (56 of 120 access
slots a line) [Nasta 524-528].

| Model | Busy chunks, visible lines | Busy chunks, other lines | Source |
|---|---|---|---|
| Nasta (logic analyser) | 32 | 32 (RAS-only refresh in the same slots) | Nasta 534-541, 905-918 |
| MiSTer ql_timing / sQLux (copy loop timed on a real QL) | 32 | 8 (refresh) | sQLux `zx8301.c` header |
| MAME (approximation) | 32: a flat 8 CPU clocks lost per RAM access | 0 | MAME zx8301.h `CONTENDED_CYCLES_LOST`, `is_owning_bus()` |

[UNCERTAIN: the two hardware-based sources disagree on the lines
outside the picture.] sQLux's finer rules: in a busy chunk the CPU can
only start a RAM access at the chunk's first cycle (at most one byte
every 12 clocks); a read costs no wait if the RAM is free, a write
always at least one wait state (DS comes a cycle later on writes)
[sQLux `zx8301.c`, one source]. The microdrive and network code is
timed for ROM execution; Tobias Fröschle notes it does not work from
contended RAM [Microdrives.pdf].

---

## 3. ZX8302 peripheral controller

Register map at $18000-$18023 [TG 2360-2370, TG 6615-6631; MAME ql.cpp
`read`/`write`] [confirmed]. Bytes; the 68008 has an 8-bit bus.

| Address | Read | Write | TG name |
|---|---|---|---|
| $18000 | RTC byte 0 (bits 31-24) | RTC reset (any value) | PC_CLOCK |
| $18001 | RTC byte 1 (bits 23-16) | RTC step | |
| $18002 | RTC byte 2 (bits 15-8) | Transmit control | PC_TCTRL |
| $18003 | RTC byte 3 (bits 7-0) | IPC link (see 4.1) | PC_IPCWR |
| $18020 | Microdrive/serial status + IPC link bits | Microdrive control | PC_MCTRL / PC_IPCRD |
| $18021 | Interrupt status | Interrupt clear + masks | PC_INTR |
| $18022 | Microdrive track 1 data | Transmit data (serial, net, microdrive) | PC_TRAK1 / PC_TDATA |
| $18023 | Microdrive track 2 data | (none; see note) | PC_TRAK2 |

Note: TG's table (TG 2362) lists "$18023 write: Display control". The
ROM, Minerva and Nasta all use $18063 for display control; the JS ROM
writes $00 to $18023 as part of `MOVE.L #$061F0000,$18020` at reset.
Decode display control at $18063 only [UNCERTAIN on very early
ZX8301s; BOOT-W 259, Nasta 1373-1385].

The ZX8301 selects the ZX8302 for A17:A16 = 01 with A6 = 0; the
ZX8302 decodes its registers from A0, A1 and A5 [Nasta 885-890,
1341-1346]. So $18000-$18003 and $18020-$18023 alias across the I/O
block on a real QL (how far depends on the ULA version). The ROMs use
only the addresses above.

### 3.1 $18021 interrupt register (read)

| Bit | Value | Name (TG) | Meaning | Set by |
|---|---|---|---|---|
| 0 | $01 | PC.INTRG | gap interrupt | microdrive gap, only while the gap mask (bit 5) is set |
| 1 | $02 | PC.INTRI | interface interrupt | [UNCERTAIN] see below |
| 2 | $04 | PC.INTRT | transmit interrupt | serial/network transmit buffer emptied |
| 3 | $08 | PC.INTRF | frame interrupt | ZX8301 VSYNC, every frame (50 Hz PAL) |
| 4 | $10 | PC.INTRE | external interrupt | EXTINTL on the expansion connector (active low) |
| 5-7 | | | see note | |

Sources: TG 6633-6645 [confirmed by MAME zx8302.h `INT_*` and the JS
level-2 handler, BOOT-W 402-425].

Bits 5-7 on read [one source: Minerva `inc/pc`; UNCERTAIN]:

- bit 7: the baud-rate clock, "pulsing up and down once per bit";
- bit 6: normally 1, 0 while a microdrive is running;
- bit 5: the lsb of 1/65536ths of a second (the 32768 Hz crystal
  doubled).

MAME returns only bits 0-4; the JS handler ignores 5-7. But Minerva's
serial-to-microdrive switch counts 12 toggles of bit 7 (Minerva
`ss/iser.asm` 26-50): an emulator that runs Minerva must toggle bit 7
at the transmit baud rate, or Minerva's first microdrive access after
serial output hangs.

### 3.2 $18021 interrupt register (write)

| Bits | Effect |
|---|---|
| 0-4 | 1 = clear that pending bit (gap, interface, transmit, frame, external) |
| 5 | PC.MASKG: 1 = gap interrupt enabled |
| 6 | PC.MASKI: 1 = interface interrupt enabled |
| 7 | PC.MASKT: 1 = transmit interrupt enabled |

Sources: TG 6646-6648 (names), TG 2519-2530 (the select code: "and.b
#OFFh-pc.maskt ... disable transmit interrupt", "or.b #pc.maskt ...
enable transmit interrupt": so 1 = enabled), MAME `irq_acknowledge_w`
(mask = data & $E0, pending &= ~(data & $1F)) [confirmed].

- The mask bits are not readable; QDOS keeps their value in system
  variable SV_PCINT ($28035) and ORs it into every acknowledge write
  [TG 6146ff; BOOT-W 1011-1024]. At boot SV_PCINT = $C0 (transmit and
  interface enabled); the gap mask $20 is set only while a microdrive
  runs.
- Frame and external interrupts have no mask: they are always pending
  when they occur [TG 6633-6648; MAME].
- **The gap interrupt must re-assert**: if the gap is present when the
  gap mask is written as 1 (every acknowledge write ORs SV_PCINT in),
  the gap bit is set again (MAME zx8302.cpp:543-547 "Minerva relies on
  this"; Minerva `ss/int2.asm` 33-38). Without it the JS ROM hangs at
  boot opening `mdv1_boot` with no cartridge (see section 7).
- The acknowledge values the ROMs write: gap `(SV_PCINT|$01)&$DF` then
  `SV_PCINT`; interface `SV_PCINT|$02` (after every IPC exchange);
  transmit `SV_PCINT|$04`; frame `SV_PCINT|$08`; external
  `SV_PCINT|$10` [BOOT-W 402-425, 4919-4923, 5043-5046].

### 3.3 Interrupt level

- The ZX8302's IPL1L output pulls the 68008's IPL1 low: **level 2,
  autovectored** (vector $68) [TG 611-615; MAME ql.cpp:911] [confirmed].
  Assert it while any of bits 0-4 is pending, release it when all are
  clear (MAME `trigger_interrupt`, `irq_acknowledge_w`).
- An interrupt with no pending bit makes the JS handler return without
  acknowledging, i.e. an endless interrupt storm: never assert IPL2
  with nothing pending [BOOT report section 5].
- The IPC also drives the interrupt lines (P2.2 to IPL0L+IPL2L, P2.3 to
  IPL1L, wired with the ZX8302's IPL1L): see 4.5.
- Level 7 (NMI) comes from CTRL-ALT-7 through the IPC; JS vectors it to
  an RTE unless a job redirected it [TG 617-620; BOOT-W 52-110].

### 3.4 The interface interrupt (bit 1) [UNCERTAIN]

- TG names it, the JS level-2 handler services it (an IPC status poll,
  BOOT-W ~L2CCC) and both JS and Minerva write `SV_PCINT|$02` after
  every IPC exchange, clearing it [BOOT-W 5043-5046; IPC report].
- Minerva `inc/pc`: the interface interrupt "was useless" (ignored by
  Minerva until 1.94); the Hermes IPC replacement uses it to signal
  serial input (every 8 bytes); "it actually also happens whenever data
  transfer goes on between the CPU and the IPC", and "all internal
  routines clear this interrupt before returning". The original IPC
  firmware never drives anything for it, and MAME never sets it.
- Recommendation: set it at the end of each IPC exchange, or never;
  both ROMs clear it straight after every exchange, so either is
  harmless. With the mask (bit 6) clear it cannot interrupt anyway.

### 3.5 $18020 status (read)

| Bit | TG name | Meaning |
|---|---|---|
| 0 | | network input (MAME: "Network port", not modelled) |
| 1 | PC..TXFL | transmit buffer full (microdrive/serial/net) |
| 2 | PC..RXRD | microdrive read buffer ready |
| 3 | PC..GAP | microdrive gap |
| 4 | PC..DTR1 | DTR on port 1 (SER1 handshake input) |
| 5 | PC..CTS2 | CTS on port 2 (SER2 handshake input) |
| 6 | (pc..ipch) | IPC link busy: 1 while a link bit is in progress |
| 7 | | IPC link data (COMDATA latched for the CPU) |

Sources: TG 6655-6667 (bits 1-5), TG 2462-2466 (DTR/CTS), MAME
`status_r` (all eight), JS `L2F6E` (`moveq #pc..ipch,d6`, handshake
bit; reply data "is bit 7") [confirmed].

Minerva's comment calls bit 6 "set when bit 7 is OK and IPC is ready",
but both ROMs wait for it to read **0** (JS L2F8E `btst d6,(a0); bne`;
Minerva `inc/pc` on pc_ipcwr: "waiting for it to go to zero").
Bit 3 (gap) reads 1 normally, and while a gap passes under the head of
a running drive; 0 while data passes [Minerva `inc/pc`; MAME resets it
to 1].

The $18020 write (microdrive control) is in section 6.2.

### 3.6 Real-time clock ($18000-$18003)

- A 32-bit counter of seconds since **00:00:00 1 January 1961** [TG
  2922; UG Concepts "Clock": "will run from 1961 JAN 01 00:00:00"],
  clocked from the 32.768 kHz crystal (MAME `set_rtc_clock(X2)`, ticks
  once a second).
- Read: the four bytes at $18000 (most significant) to $18003. Both
  ROMs read the long twice and repeat until two reads agree (JS $195A;
  Minerva `mt/rclck.asm`) [BOOT report 6]. MAME starts it at host time
  + 283 996 800 s (the seconds from 1961 to 1970; MAME's comment
  "1979" is wrong).
- Write $18000 (any value): resets the counter to 0 [Minerva `inc/pc`;
  TG 2369 "Real-time clock reset"; MAME doesn't implement it].
- Write $18001 ("step"): a 0 in bit 4 increments byte 0 (the msb, as
  read at $18000), a 0 in bit 3 / 2 / 1 increments the next bytes down;
  bits 7-5 and 0 ignored [Minerva `inc/pc`]. Carries are odd: once a
  byte's bit 7 is set, the next byte up can only change by carry, so
  the ROMs set the msb first. MT.SCLCK: reset, then for each byte msb
  first write $EF / $F7 / $FB / $FD that byte's value times [BOOT
  report 6]. An emulator can treat each write as adding 1<<24, 1<<16,
  1<<8 or 1.
- The RTC keeps time only while powered (no battery) [TG 10.6].

### 3.7 $18002 transmit control (write only)

| Bits | Name | Meaning |
|---|---|---|
| 0-2 | PC.BMASK | baud: 0 = 19200, 1 = 9600, 2 = 4800, 3 = 2400, 4 = 1200, 5 = 600, 6 = 300, 7 = 75 |
| 3 | PC..SERN | serial port number (0 = SER1, 1 = SER2) |
| 4 | PC..SERB | 0 = serial I/O, 1 = microdrive / network |
| 3-4 | (mode) | 00 SER1, 01 SER2, 10 microdrive (PC.MDVMD $10), 11 network (PC.NETMD $18) |
| 5-6 | | 0 |
| 7 | PC..DIRO | direct output (network) |

Sources: TG 6649-6654, Minerva `inc/pc`, MAME zx8302.h enums
[confirmed]. Baud values: MAME's enum and the IPC's table (IPC $02DB);
MAME's `control_w` computes 19200 >> n, which gives 150 for 7, not 75
(a MAME slip). The register is shared with the IPC's BAUDX4 clock (the
IPC samples received bits by it, section 4.10) and can't be read back:
QDOS keeps a copy in SV_TMODE ($280A0) [TG 6146; Microdrives.pdf].
19200 is transmit only [UG Concepts "Serial"].

### 3.8 $18022 transmit data

- Write: the byte to send on the device $18002 selects; $18020 bit 1
  (TXFL) = 1 until the ZX8302 has taken it; the transmit interrupt
  ($18021 bit 2, mask bit 7) when the buffer empties [TG 6628; MAME
  `data_w`, `tra_complete`].
- Serial frame: start bit, 8 data bits, at least 2 stop bits; parity
  is done in software [UG Concepts "Serial": "will always transmit data
  with at least two stop bits"; MAME `set_data_frame(1, 8, NONE, 2)`].
  SER1 (wired as DCE) and SER2 (DTE) [MAME]; the transmit handshakes
  are DTR1 / CTS2 in $18020 bits 4/5 [TG 2462-2466].
- Microdrive mode: bytes go to the tape (section 6). Network mode:
  NETOUT; the QL network (Sinclair's ZX Net protocol, 11.2 us a bit,
  8-byte headers) is bit-banged by the ROM through $18002 bit 7 and
  $18020 bit 0 [QL_Network_Protocols.pdf; TG 10.7]; not needed to
  boot.

---

## 4. The IPC (8049) as the 68008 sees it

The IPC is an Intel 8049 (8749 in early machines) with its own 11 MHz
crystal: one machine cycle = 15 crystal periods = 1.364 us (733.3 kHz)
[MAME ql.cpp:109, 884 (X4 = 11 MHz, I8749)]. It scans the keyboard,
makes the sound, receives both serial ports and talks to the 68008
over a two-wire serial link through the ZX8302 [TG 2436-2445]. Sources
for this section: the IPC report (Venema's annotated disassembly
`ipc8049.txt`, cited by 8049 address as "IPC $xxxx"), the JS ROM
disassembly (Westbury, cited "JS Lxxxx" by ROM address), TG, MAME.

### 4.1 The link, 68008 side (what to emulate)

Wires: COMDATA (IPC P2.7, wired-AND, either end can pull it low) and
COMCTL (the IPC's WR strobe; every MOVX pulses it) [MAME ql.cpp
`ipc_w`, `ipc_port2_w`; IPC $074F] [confirmed].

**Write to $18003** loads four bits into the ZX8302 [MAME zx8302.cpp
`ipc_command_w`, comment at 420-429; IPC report 4.1]:

| Bit | Meaning |
|---|---|
| 0 | start bit (put on COMDATA at once) |
| 1 | data bit |
| 2 | stop bit |
| 3 | extra stop bit |
| 4-7 | ignored |

Values the ROMs write [JS L2F82-L2F9C, verified in the Westbury
listing; BOOT-W 261]:

| Value | Use |
|---|---|
| $0C | send a 0 bit (start 0, data 0, stop 1, extra 1) |
| $0E | send a 1 bit, **and** clock in one reply bit |
| $01 | once at reset (start bit = 1): drives the line high (idle). **Not** a bit transaction; it must not set busy. The IPC waits for COMDATA high after its own reset before it serves the CPU (IPC $0043). |

Minerva also writes $01 to $18003, as the low byte of a word write
`$0001` to $18002 [BOOT-Min `ss/init.asm`]. Treat any write with
bit 0 = 1 as "line idle", any write with bit 0 = 0 as one bit
transaction [derived from MAME + IPC report].

**$18020 bit 6 (busy)** goes to 1 on the $18003 write that starts a
transaction and back to 0 when the IPC has clocked the bit (its second
COMCTL pulse). **$18020 bit 7** is COMDATA as latched by the ZX8302 at
the IPC's COMCTL pulses: the IPC's reply bit [MAME `transmit_ipc_data`,
`comctl_w`, `status_r`] [confirmed by JS usage].

68008 routines (JS ROM 1.10, Westbury disassembly; Minerva the same
protocol, BOOT-Min `inc/ipcmd`, `ip/int.asm`):

- **Send one bit b**: write $0C (b = 0) or $0E (b = 1) to $18003, then
  loop while $18020 bit 6 = 1 [JS L2F82-L2F92].
- **Send a nibble**: 4 bits, **most significant first** (JS L2F7C:
  `lsl.b #4` then an end-stop bit, shift out until zero).
- **Send a byte**: high nibble then low nibble, i.e. 8 bits MSB first
  [JS L02C72 per the IPC report].
- **Receive one bit**: write $0E to $18003, loop while bit 6 = 1, read
  $18020; the bit is bit 7 [JS L2F9C-L2FA6].
- **Receive a nibble / byte**: 4 / 8 bits, MSB first (JS L2F96 with
  end-stop $10, L2F9A with end-stop $01).
- No timeouts. The JS ROM does each exchange with SR |= $0700
  (interrupts masked). If bit 6 never clears the machine hangs.
- Minerva sends and receives in one loop (`ip/int.asm` ip_rdwr): wait
  for bit 6 = 0, read $18020 (reply bit in bit 7), and at once write
  the next $0C/$0E, then do its bookkeeping while the IPC works. So
  the reply bit is always read before the next write, as in JS; it
  reads bit 7 during the sending phase too and ignores it there.
  Minerva `inc/ipcmd` describes the same protocol ("%000011x0",
  "most significant bit first", sound's 16-bit values low byte first).
  An emulator that clears busy instantly satisfies both ROMs.

**HLE model** [derived]: keep a bit-level state machine.

1. Idle: the next transaction bit starts a command; collect 4 bits
   (MSB first) = command number.
2. Collect the command's parameter bits (table 4.3), taking bit 1 of
   each $18003 write.
3. Shift out the reply bits (table 4.3) one per $18003 write, MSB
   first, into $18020 bit 7.
4. Clear busy at the end of each bit (instantly is fine for JS and
   Minerva; for realism, about 15-20 us per bit, the first bit of a
   command up to ~0.42 ms later: the IPC tests COMDATA once per main
   loop pass) [IPC report 4.1, derived from cycle counts].
5. The bit counts must be exact. A mismatch desynchronises every later
   exchange; the firmware has no resynchronisation except reset or
   CTRL-ALT-7 [IPC report 4.9].

### 4.2 The link, IPC side (for reference)

- IPC receives a bit (IPC $074F): wait for COMDATA low (the start bit),
  MOVX (pulse 1), read P2.7, MOVX (pulse 2), shift the bit in MSB
  first. Venema's comment at $0766 ("until comdata=H") is wrong: it
  waits for low [IPC report].
- IPC sends a bit (IPC $0762): wait for COMDATA low, MOVX, drive P2.7
  with the bit, MOVX (the ZX8302 latches the wired-AND, i.e. the IPC's
  bit, since the CPU sent data = 1), release P2.7.
- Main loop: tests COMDATA once per pass (IPC $0200); if low, reads a
  4-bit command and dispatches through the table at IPC $020C.
- After its reset the IPC clears its RAM, sets 9600 baud, and loops
  until COMDATA is high (IPC $0009-$0046).

### 4.3 Command table

"In" = 68008 to IPC after the 4-bit command; "Out" = IPC to 68008. All
fields MSB first. Names from TG 6593-6612. All 16 are implemented in
the IPC ROM (dispatch table IPC $020C) [IPC report 4.2].

| # | Name (TG) | In | Out | Effect |
|---|---|---|---|---|
| 0 | RSET_CMD | none | none | full IPC re-initialise (IPC $021C to $0009); then waits for COMDATA high |
| 1 | STAT_CMD | none | 8 bits | status byte, 4.4 |
| 2 | OPS1_CMD | none | none | open SER1 receive (IPCSTAT bit 4) |
| 3 | OPS2_CMD | none | none | open SER2 receive (bit 5) |
| 4 | CLS1_CMD | none | none | close SER1 (buffer not flushed) |
| 5 | CLS2_CMD | none | none | close SER2 |
| 6 | RDS1_CMD | none | 8 + 8n bits | SER1 data: first byte bits 5-0 = n (0-23), bit 6 = SER1 framing error, bit 7 = SER2 framing error; then n bytes, oldest first |
| 7 | RDS2_CMD | none | 8 + 8n bits | the same for SER2 |
| 8 | RDKB_CMD | none | 4 + 12n bits | read keyboard, 4.6 |
| 9 | KBDR_CMD | 4 bits: row (bits 2-0 used) | 8 bits | keyboard direct read (KEYROW), 4.7 |
| 10 | INSO_CMD | 64 bits (8 bytes) | none | initiate sound, section 5 |
| 11 | KISO_CMD | none | none | kill sound |
| 12 | MDRS_CMD | 4 bits | none | "Microdrive reduced sensitivity": sets P2.3 = bit 0, and P2.3 is the CPU's IPL1L line (0 asserts level 2!) [IPC $02C3] |
| 13 | BAUD_CMD | 4 bits (bits 2-0) | none | baud index 0-7 = 19200, 9600, 4800, 2400, 1200, 600, 300, 75 (IPC table $02DB) |
| 14 | RAND_CMD | none | 16 bits | random number, low byte first (IPC $02E3) |
| 15 | TEST_CMD | 8 bits | 8 bits | echoes the byte (IPC $02F0) |

Notes:

- Bit counts agree with sQLux `ipc.c`'s link decoder (GPL, confirms).
- TG says only 9, 10 and 11 are for applications; the OS uses 1, 8,
  2-7, 13 [TG 3288-3306].
- RAND's 16-bit reply cannot be requested through MT.IPCOM (reply codes
  give 4 or 8 bits) [IPC report].
- TEST [UNCERTAIN]: the disassembly and Minerva say "echo"; the old
  uQLx HLE returns the complement (Minerva says only Hermes does that).
- MDRS: Minerva notes it causes "permanent level 2 interrupts" (the
  ZX8302 shows no pending bit, so the handler never clears it). HLE:
  ignore it, or drive IPL1 if you model the lines.
- What the JS ROM sends at boot: nothing until the first frame
  interrupt after the jump into BASIC, then STAT every frame. Minerva
  sends BAUD (command 13, nibble 1) during reset with interrupts off
  [BOOT report 1, 8].

MT.IPCOM (TRAP #1, D0 = $11) is the OS interface; its parameter block
is: command byte (low nibble), parameter count byte, a long of 2-bit
codes per parameter in reverse order (00 = send low 4 bits, 10 = send
8 bits, x1 = send nothing), the parameter bytes, and a reply-length
byte (bits 1-0: 00 = 4 bits, 10 = 8 bits, x1 = none) [TG 3252-3285].

### 4.4 STAT (command 1) reply

Built at IPC $0228-$0249 [IPC report 4.3]:

| Bit | Meaning |
|---|---|
| 0 | key(s) in the 7-key buffer, **or** a key is being held (needed for autorepeat) |
| 1 | sound running (set by INSO, cleared by KISO or when the duration ends) |
| 2 | modifier combination changed while a non-modifier key was down |
| 3 | key held after being stored |
| 4 | SER1 receive buffer not empty |
| 5 | SER2 receive buffer not empty |
| 6 | IPC pin P2.6 [UNCERTAIN: Venema and Minerva "not connected"; MAME and sQLux read 0; JS sets SV.WP = $FF when it is 0, and nothing reads SV.WP. Return 0.] |
| 7 | transient serial-receive timeout flag; return 0 |

What the JS ROM does with it (JS L02D20): bit 1 to SV.SOUND ($96, read
by BEEPING); bit 0 set and the keyboard queue exists: send RDKB; bit 4
or 5 and that channel's queue has at least 25 bytes free: send RDS1 or
RDS2 [IPC report 4.3]. The old uQLx HLE builds the same bits 0, 1, 4,
5 (sQLux QL_hardware.c `DoIPCCommand`, confirms).

### 4.5 Interrupts from the IPC

- Keyboard, sound and serial events never interrupt: the 68008 polls
  with STAT on every frame interrupt (JS polled task L2D08, 50 Hz on a
  PAL QL) [IPC report 4.3; BOOT report 5; Venema's notes].
- IPC P2.2 drives IPL0L and IPL2L together, P2.3 drives IPL1L (shared
  with the ZX8302's IPL1L). P2.2 low alone = level 5, P2.3 low alone =
  level 2, both low = level 7 [IPC $0012/$0014; MAME ql.cpp
  `ipc_port2_w`, which maps 0 to IRQ7, 1 to IRQ5, 2 to IRQ2, 3 to none]
  [confirmed].
- The firmware moves them only for CTRL-ALT-7 (P2.2) and MDRS (P2.3).
- **CTRL-ALT-7** (IPC $0570-$058B): when the stored key is code $3F
  ('7') with exactly CTRL+ALT, the key is not stored; the IPC pulls
  P2.2 low, waits about 179 ms (255 x 514 machine cycles), then
  re-initialises (key buffer empty, sound off, serial closed, 9600
  baud), which releases P2.2.
  [UNCERTAIN level]: TG 617-620 says level 7; P2.2 alone is level 5
  (JS vector $74 is a bare RTE); it becomes 7 while the ZX8302 also
  pulls IPL1L (any pending interrupt, e.g. the next frame within
  20 ms). MAME asserts IRQ5. An emulator can simply raise level 7 for
  ~180 ms, then reset its IPC state.

### 4.6 RDKB (command 8): read keyboard

Reply [IPC $024D-$0274; JS L02E58; sQLux QL_hardware.c (old HLE)
confirms the layout]:

1. A nibble: bit 3 = a key is held (IPCSTAT bit 3); bits 2-0 = n, the
   number of buffered keys (0-7).
2. n times: a nibble of modifiers, then a byte key code:
   - modifier nibble: bit 0 ALT, bit 1 CTRL, bit 2 SHIFT, bit 3
     overflow (set on the 7th entry when a key was lost because the
     buffer was full; JS masks it off with ANDI #7);
   - key code byte: `00cccccc`, the key's matrix code 0-63 (4.8).
3. The buffer is emptied and IPCSTAT bit 0 cleared.

- Buffer: 7 entries of 2 bytes (IPC RAM $2C-$39) [IPC report; TG:
  "seven characters"].
- Debounce: a key must be seen in 6 consecutive scan passes (one to
  latch, then a count to 4) before it is stored, about 2.5 ms
  [IPC $056D, derived pass time ~0.42 ms].
- Only one non-modifier key at a time: with two or more down nothing
  is stored and the state resets; when back to one, that key is stored
  again (even if it was stored before) [IPC $053F].
- Modifiers alone are never reported as keys (row 7 bits 0-2 masked at
  IPC $0522); they come only as the modifier nibble of the next key,
  or through KEYROW.
- **Autorepeat is done by the 68008, not the IPC.** The IPC only
  reports "held" (status bit 0 stays set and the RDKB nibble's bit 3).
  JS: SV.ARDEL = 30 frames delay, SV.ARFRQ = 2 frames rate; while bit
  3 is set it counts down and re-queues the last key (JS L02E58,
  L02E96; TG 1216-1219) [confirmed]. If the emulated IPC drops the
  held bit, autorepeat stops working.
- HLE recipe [derived]: queue (modifiers, code) on each key press
  (max 7, set bit 3 of the 7th modifier on overflow), report held
  while the last pressed non-modifier key stays down, never report a
  key while two non-modifier keys are down.
- The JS ROM translates code + modifiers into QL character codes with
  its keyboard table (country specific); CAPS LOCK (code 33) toggles
  caps in the OS; CTRL-Space is break, CTRL-F5 freeze, CTRL-C switch
  queue [JS L0B352, L02EEC; User Guide Concepts].

### 4.7 KBDR (command 9): KEYROW

- Parameter: one nibble, the row 0-7 (bits 2-0 used). Reply: one byte,
  the raw state of that row, 1 = pressed, bit b = key code
  8 x (7 - row) + b [IPC $0276-$0287; UG Keywords KEYROW].
- No debounce, no side effects in the IPC. The "type-ahead buffer is
  cleared" behaviour the User Guide describes is SuperBASIC's: its
  KEYROW empties the console queue on the 68008 side (BASIC3 LKEYROW)
  [IPC report 4.5; corrects UG].
- Pressing three keys at the corners of a rectangle in the matrix shows
  the fourth as pressed too (no diodes), except for SHIFT, CTRL and ALT
  [UG Keywords KEYROW]. An emulator may ignore ghosting.

### 4.8 Keyboard matrix and key codes

KEYROW row r, bit b; IPC key code = 8 x (7 - r) + b. Bit values as
KEYROW returns them: bit 0 = 1, bit 7 = 128.

| Row | bit 0 (1) | bit 1 (2) | bit 2 (4) | bit 3 (8) | bit 4 (16) | bit 5 (32) | bit 6 (64) | bit 7 (128) | codes |
|---|---|---|---|---|---|---|---|---|---|
| 7 | SHIFT | CTRL | ALT | X | V | / | N | , | 0-7 |
| 6 | 8 | 2 | 6 | Q | E | 0 | T | U | 8-15 |
| 5 | 9 | W | I | TAB | R | - | Y | O | 16-23 |
| 4 | L | 3 | H | 1 | A | P | D | J | 24-31 |
| 3 | [ | CAPS LOCK | K | S | F | = | G | ; | 32-39 |
| 2 | ] | Z | . | C | B | £ | M | ' | 40-47 |
| 1 | ENTER | Left | Up | ESC | Right | \ | SPACE | Down | 48-55 |
| 0 | F4 | F1 | 5 | F2 | F3 | F5 | 4 | 7 | 56-63 |

Sources [confirmed, no disagreement]: MAME ql.cpp INPUT_PORTS Y0-Y7
(Yn = IPC P1 bit n = KEYROW row n); the IPC's code formula (IPC
$05EC-$05F8); the JS ROM keyboard table (ENTER 48, Left 49, Up 50, ESC
51, Right 52, SPACE 54, Down 55, F1 57, F2 59, F3 60, F4 56, F5 61,
CAPS 33, TAB 19); sQLux `include/qlkeys.h` (QLay's key defines, GPL,
same numbers) and `SDL2screen.c` (row = 7 - code/8); the User Guide
KEYROW table (UG Keywords; its OCR shows "." for the "," at row 7 bit
7 and "~" for the "'" at row 2 bit 7, both OCR errors); the corrected
physical matrix by Lee Privett (`hardware/keyboard.png`, J11/J12
wiring), whose J11 rows 7 and 9 together make KEYROW row 7.

Same table by code (for the RDKB reply):

| Code | Key | Code | Key | Code | Key | Code | Key |
|---|---|---|---|---|---|---|---|
| 0 | (SHIFT) | 16 | 9 | 32 | [ | 48 | ENTER |
| 1 | (CTRL) | 17 | W | 33 | CAPS LOCK | 49 | Left |
| 2 | (ALT) | 18 | I | 34 | K | 50 | Up |
| 3 | X | 19 | TAB | 35 | S | 51 | ESC |
| 4 | V | 20 | R | 36 | F | 52 | Right |
| 5 | / | 21 | - | 37 | = | 53 | \ |
| 6 | N | 22 | Y | 38 | G | 54 | SPACE |
| 7 | , | 23 | O | 39 | ; | 55 | Down |
| 8 | 8 | 24 | L | 40 | ] | 56 | F4 |
| 9 | 2 | 25 | 3 | 41 | Z | 57 | F1 |
| 10 | 6 | 26 | H | 42 | . | 58 | 5 |
| 11 | Q | 27 | 1 | 43 | C | 59 | F2 |
| 12 | E | 28 | A | 44 | B | 60 | F3 |
| 13 | 0 | 29 | P | 45 | £ | 61 | F5 |
| 14 | T | 30 | D | 46 | M | 62 | 4 |
| 15 | U | 31 | J | 47 | ' | 63 | 7 |

(Codes 0-2 never appear in an RDKB reply.) Minerva's `inc/ipcmd`
describes the code byte as "00colrow ... as keyrow table": the same
numbers, but there the upper three bits (7 - KEYROW row) are called
"col" and the lower three (the KEYROW bit) "row"; don't let the names
mislead. Key caps (UK): the shifted
symbols are 1 !, 2 @, 3 #, 4 $, 5 %, 6 ^, 7 &, 8 *, 9 (, 0 ), - _, = +,
[ {, ] }, ; :, ' ", , <, . >, / ?, \ |, £ ~; ESC also carries a
copyright sign (MAME PORT_CHAR; keyboard.png). Non-UK QLs move keys
(e.g. German Y/Z swapped); KEYROW and the codes are physical positions
[TG 2861-2864; MAME ql_de etc.].

Joysticks (CTL1, CTL2) are wired across the matrix and look like keys
[UG Concepts "Joystick"; MAME JOY0/JOY1]:

| Port | Up | Down | Left | Right | Fire | KEYROW row |
|---|---|---|---|---|---|---|
| CTL1 | Up | Down | Left | Right | SPACE | 1 |
| CTL2 | F4 | F2 | F1 | F3 | F5 | 0 |

(MAME labels the F-key joystick PLAYER(1) and the cursor one
PLAYER(2); the User Guide calls the cursor one CTL1.)

### 4.9 Character codes QDOS makes from the keys

Not an IPC matter (the ROM's table does it), but useful for testing
[UG Concepts "Character set and keys"]:

- Cursor keys $C0-$DF: Left $C0, Right $C8, Up $D0, Down $D8, plus
  ALT +1, CTRL +2, SHIFT +4.
- CAPS LOCK $E0 (+ALT 1, CTRL 2, SHIFT 4); trapped by QDOS.
- F1 $E8, F2 $EC, F3 $F0, F4 $F4, F5 $F8; +CTRL 1, +SHIFT 2.
- SHIFT SPACE $FC, SHIFT TAB $FD, SHIFT ENTER $FE.
- ALT with any other key: $FF followed by the code without ALT.
- ENTER = 10 (CTRL J), TAB = 9, ESC = 27, SHIFT ESC = 127.
- The F1/F2 boot prompt accepts only $E8 and $EC (no modifiers) [BOOT
  report 4].

### 4.10 Serial receive (IPC side)

- The IPC only receives; transmitting is the ZX8302's ($18022 data,
  $18002 control) [TG 6625-6660; IPC report 4.7].
- Bits are sampled by counting 4 BAUDx4 edges from the ZX8302 on the
  IPC's T1 per bit: the actual rate is set by $18002 bits 0-2, the
  IPC's BAUD command only sets its idle timeout [IPC $0106-$0109; MAME
  `ipc_t1_r`].
- 8 data bits LSB first and a stop bit; 23-byte buffer per channel;
  the handshake output (P2.4 SER1 CTS, P2.5 SER2 DTR; low = may send)
  drops at 20 bytes [IPC $0177, $018A; MAME `write_dtr(!P2.5)`].
- HLE: a 23-byte FIFO per open channel, return it through RDS1/RDS2
  (at most 23 bytes a reply), framing bits 0.

---

## 5. Sound

The 8049 toggles a speaker pin (P2.1) in its timer interrupt: a square
wave, one bit [MAME ql.cpp `ipc_port2_w`; IPC report 4.6]. Sources:
IPC report (cycle counts from the disassembly), UG Keywords BEEP, TG
3297-3306.

### 5.1 BEEP and the INSO parameters

`BEEP duration, pitch [, pitch_2, grad_x, grad_y [, wrap [, fuzzy [,
random]]]]`; ranges: duration -32768..32767, pitch 0..255, grad_x
-32768..32767, grad_y -8..7, wrap 0..15, fuzzy 0..15, random 0..15;
BEEP with no parameters kills the sound; duration 0 runs until the next
BEEP; "pitch 1 is high and 255 is low"; wrap 15 wraps forever [UG
Keywords BEEP].

INSO (command 10) sends 8 bytes, each high nibble first, stored at IPC
RAM $3A-$41 [IPC $0300]:

| Byte | Contents |
|---|---|
| 1 | pitch 1 |
| 2 | pitch 2 |
| 3, 4 | interval (grad_x), **low byte first** |
| 5, 6 | duration, **low byte first**; 0 = forever |
| 7 | high nibble = step (grad_y, signed -8..+7); low nibble = wrap |
| 8 | high nibble = random; low nibble = fuzzy |

- Little-endian 16-bit values: IPC 16-bit arithmetic ($0716, $070B),
  the JS BEEP byte swap (BASIC2 LBEEP `ROR.W #8`), Minerva [confirmed].
- In MT.IPCOM terms: six 8-bit parameters then four nibbles (step,
  wrap, random, fuzzy); TG lists them as "pitch 1, pitch 2, interval
  between steps (16), duration (16), step in pitch (4), wrap (4),
  randomness of step (4), fuzziness (4)" [TG 3297-3306].
  JS BEEP uses parameter codes $4444AA66 [IPC report].
- **SuperBASIC adds 1 to pitch and pitch_2** before sending, so BASIC
  p is IPC byte p+1 and BASIC 255 becomes byte 0 (= 256 ticks)
  [IPC report, JS BASIC2 LBEEP].
- `BEEP d,p` sends pitch 2 = pitch 1 and zeros for the rest; plain
  `BEEP` sends KISO (11) [IPC report].
- KISO clears the sound-on bit; the pin goes low at the next timer
  interrupt.

### 5.2 The generator (IPC timer ISR $0327-$0414)

The 8049 timer counts machine cycles / 32: one tick = 43.64 us
(22 917 Hz) [IPC report; sQLux uses the same rate].

At INSO: pitch := P2 if step < 0 else P1; base := half := pitch;
counters and wrap count := 0; status bit 1 set. The first edge comes at
the next timer overflow.

At each timer overflow (h = half, or 256 if half = 0):

1. Sound off: speaker low, stop.
2. Duration not 0: dcount += h + 6; when dcount >= duration the main
   loop clears status bit 1 and the next overflow stops the sound.
3. icount += h + (duration not 0 ? 6 : 4).
4. If icount >= interval (so interval 0 = a step every half cycle):
   icount := 0; pitch := (pitch + step) mod 256; then, **unless
   P1 < pitch < P2** (unsigned):
   - wrap = 15: pitch := its start value (forever);
   - 1 <= wrap <= 14 and wraps < wrap: wraps += 1, pitch := start
     value;
   - otherwise: wraps := 0 and step := -step (4-bit; -8 stays -8):
     the sound bounces, pitch not reset.
   Then base := (pitch + rnd(random)) mod 256.
5. Toggle the speaker (P2.1).
6. half := (base + rnd(fuzzy)) mod 256.
7. Reload the timer for `half` ticks (256 if 0).

rnd(v) = 0 if v < 8; else one step of the IPC's random generator,
masked to 2^((v & 7) + 1) - 1 (v = 8: 0-1, v = 15: 0-255). So
"random" perturbs each pitch step and "fuzzy" each half cycle
[IPC $0415, $0422, $073A].

Note the bounce condition: with P1 < P2 the pitch moves inside the
open interval (P1, P2); if P1 >= P2 there is no inside and every step
"leaves the range" [derived from the condition above; the UG only says
the sound "bounces" between the pitches].

### 5.3 Frequency and duration

Half period = 32 x half + K machine cycles, K the ISR overhead: about
305 for a plain `BEEP d,p` (interval 0, step 0, duration set, so every
half cycle is a "step" that bounces); 134-203 without a step; more with
random/fuzzy (+180-256 each) [derived, IPC report].

For a plain `BEEP d,p` (IPC byte P = p + 1):

    f = 733333 / (2 x (32 x P + 305)) = 11458 / (P + 9.53) Hz
      = 11458 / (p + 10.53) Hz

[derived; sQLux's HLE uses p + 10.6, which agrees].

| BASIC p | f (Hz) | us per duration unit |
|---|---|---|
| 0 | 1088 | 65.6 |
| 1 | 994 | 62.9 |
| 10 | 558 | 52.7 |
| 50 | 189.3 | 46.3 |
| 100 | 103.7 | 45.1 |
| 254 | 43.4 | 44.2 |
| 255 | 43.2 | 44.3 |

Duration: counted in timer ticks (43.64 us) of nominal half periods,
so the real time per unit is 44-66 us depending on pitch (the counted
overhead 6 is smaller than the real ~9.5). The User Guide says "units
of 72 microseconds" [UG Keywords BEEP]: **not reproduced by the code**
[UNCERTAIN; sQLux uses 43.64 us]. Nothing here was measured on a real
QL; all timings are cycle counts.

---

## 6. Microdrives

Sources: the boot/ROM report (JS ROM listing "BOOT-W", Minerva `md/*`),
MAME `microdrv.cpp` and `zx8302.cpp`, the djw "MDV image file formats"
PDF ("MDVPDF"), Tobias Fröschle's "Microdrives – The Inner Workings"
(Microdrives.pdf), TG 10.8 and 18.13.

### 6.1 The medium

- An endless tape loop with **two tracks read and written in
  parallel**; a record's bytes alternate between them: byte 0 on
  track 1 ($18022), byte 1 on track 2 ($18023), and so on [BOOT report
  7.5; MAME `raw1_w`/`raw2_w`, byte pairs].
- Nominal **40 us a byte** (i.e. a byte pair every 80 us, 100 kbit/s a
  track); drives are specified to 10% [Minerva `md/read.asm` header:
  "The nominal time for a single byte on a microdrive is 40us", "A
  nominal variation of 10% is documented"; MAME `MDV_BITRATE` 100 kHz]
  [confirmed].
- Each sector is two records separated by gaps: a **header** (written
  only when formatting) and a **block** (block header + 512 data bytes,
  rewritten on every write). The QL detects gaps (no recorded signal)
  in hardware: $18020 bit 3 and the gap interrupt [Microdrives.pdf].
- Formatting writes sectors 255 down to 0 around the loop; the later
  (lower) ones overwrite the first, and the highest sector number that
  survives gives the capacity: typically ~220-230 sectors; a format
  with fewer than 200 good sectors fails [Microdrives.pdf; MDVPDF p.6;
  BOOT report 7.7]. Sectors pass the head in **descending** order.
- Tape revolution: MAME's model (255 sectors x 378 byte pairs x 80 us)
  7.7 s; sQLux docs "about 8.3 seconds" [both models, not
  measurements].

### 6.2 $18020 microdrive control (write)

| Bit | TG name | Meaning |
|---|---|---|
| 0 | PC..SEL | select bit, shifted into the drive chain |
| 1 | PC..SCLK | select clock: a 1 to 0 transition shifts |
| 2 | PC..WRIT | write (1 = write) |
| 3 | PC..ERAS | erase (1 = erase on) |
| 4-7 | | 0 |

Named values [TG 6670-6675; Minerva `inc/pc`]: PC.READ = PC.DESEL =
%0010 ($02, read or idle), PC.SELEC = %0011 ($03), PC.ERASE = %1010
($0A, erase on, write off), PC.WRITE = %1110 ($0E, erase and write).

**Drive selection** is a shift register along the chain of drives
[TG 2567-2585 code; Microdrives.pdf; Minerva `inc/pc`; MAME
`microdrive_image_device::clk_w`]:

- On each SCLK 1 to 0 edge, drive 1 latches SEL and every drive passes
  its previous bit to the next one (MAME: COMMS_OUT := COMMS_IN on the
  falling edge; drive 1's COMMS_IN is $18020 bit 0, drive 2's is drive
  1's COMMS_OUT).
- A drive whose bit is 1 is selected and its motor runs.
- Select drive n: write $03, wait, $01, wait (a 1 into drive 1), then
  (n-1) times $02, wait, $00, wait. Deselect all: 8 times $02, $00.
  Wait = `moveq #$39,d0; ror.l d0,d0` = 134 clocks, ~18 us; the
  hardware should manage one transition every 25 us [BOOT-W 4992-5009;
  TG 2546-2560; Minerva `inc/pc`].
- Normal operation keeps SCLK = 1 ($02 = read/idle), so other writes
  don't shift the chain.

**Writing $02 (bits 2-3 clear) re-arms the receiver**: it resets the
read logic, which then waits for the `$FF $FF` of a preamble before
delivering bytes (the ROM writes $02, waits 21.6 us, writes $02 again:
"the first resets the PLL, the second the receiver", Minerva `md/
endgp.asm`) [BOOT report 7.4; MAME `mdv_control_w` arms the sync
search on a write with bit 1 set].

### 6.3 Status, data and interrupts

- $18020 read bit 3 GAP: 1 while no data passes (and when no drive
  runs or the drive is empty), 0 while data passes under the head
  [Minerva `inc/pc`; MAME].
- bit 2 RXRD: a byte (MAME: a byte pair) is ready; the ROM reads $18022
  for even bytes and $18023 for odd ones, polling RXRD before each
  [BOOT report 7.5]. MAME clears RXRD when $18023 is read and sets it
  every 80 us with both bytes [UNCERTAIN: Minerva's code reads single
  bytes every 40 us; both models pass the ROMs' timing windows].
- bit 1 TXFL: the transmit buffer is full; in microdrive mode the ROM
  waits for 0 before each byte written to $18022, **with no timeout**
  (FORMAT runs with interrupts off: TXFL must keep clearing about every
  40 us even with no cartridge, or FORMAT hangs; MAME stalls it with no
  cartridge) [BOOT report 7.6, 7.9].
- Gap interrupt ($18021 bit 0, mask bit 5): raised when the gap is
  present and the mask is set, **including when the mask is written as
  1 while the gap is already there** (section 3.2). The JS ROM's whole
  microdrive driver runs from the gap interrupt [BOOT report 7.3].
- A **selected empty drive must give GAP = 1 and a gap interrupt**,
  or the boot hangs opening `mdv1_boot` (section 7.4) [BOOT report 4;
  MAME microdrv.cpp:496-501].
- $18002 must be in microdrive mode (bits 4-3 = 10). JS writes
  `SV_TMODE & $E7 | $10` = $11 at boot (9600 baud bits kept), after
  waiting for serial output to finish [BOOT-W 492-506].

### 6.4 Record layout on tape

Every record is a preamble, then data. The hardware swallows the
preamble and syncs on its `$FF $FF` [MAME `raw2_w` sync search;
MDVPDF; BOOT report 7.5]:

| Record | Preamble | Bytes after the sync |
|---|---|---|
| Sector header | 10 x $00, $FF, $FF | $FF (flag), sector number, medium name (10 bytes), random number (2 bytes), checksum (2) = 16 bytes |
| Block header | 10 x $00, $FF, $FF | file number, block number, checksum (2) = 4 bytes |
| Data (straight after the block header, same write) | 6 x $00, $FF, $FF | 512 data bytes, checksum (2) |

- **Checksum** = ($0F0F + the sum of the record's bytes) mod 65536,
  stored **low byte first**; the header's sum includes the $FF flag
  (14 bytes) [MDVPDF p.5; Minerva `md/read.asm` (starts at $0F0F, adds
  every byte read); BOOT report 7.5] [confirmed].
- Writing a block (MD.WRITE): $18020 = $0A (erase on), wait 3.557 ms,
  $0E twice, then the bytes: 10 x $00, $FF $FF, file, block, checksum,
  6 x $00, $FF $FF, 512 bytes, checksum, each after TXFL = 0; wait
  118 us; $02 [BOOT-W 8995-9044; Minerva `md/write.asm`].
- Reading: wait for a gap (up to ~0.5 s for GAP = 1), then for its end
  (24 consecutive polls with GAP = 0, ~160 us), write $02, 21.6 us, $02
  (arm), then read 16 bytes (header) or 4 (block header), re-arm and
  read 514 [BOOT report 7.4-7.5].
- ROM timing windows: first byte within ~1 ms of arming; between bytes
  JS times out ~75 us after the previous read, Minerva ~90 us; fastest
  read loop ~23 us (JS), ~17.6 us (Minerva) [BOOT report 7.8].
- Format: 0.5 s spin-up with interrupts off, then for sectors 255..0:
  2.750 ms, a header record, 2.755 ms, one 610-byte "long block" (the
  block header $FD $00, preamble and 512 x $AA55 pattern with filler,
  all written as plain data); then two verify passes [BOOT-W 8839-8985;
  Minerva `md/formt.asm`]. So a formatted sector is roughly 2.75 ms
  gap + 28 bytes + 2.75 ms gap + 610 bytes ~ 31 ms of tape [derived].
- MAME's model gaps: 2.8 ms between header and block, 4.8 ms between
  sectors (microdrv.cpp:53-61) [one source].

### 6.5 Filesystem on the cartridge (QDOS)

[MDVPDF p.5-6; TG 1813, 6383-6395]

- Sector 0 holds the **map** (file $F8, block 0; the PDF notes it may
  also be $80): 255 pairs (file, block), one per sector; $FD xx =
  vacant, $FE = bad (failed verify), $FF = bad; the last byte of the
  512 holds the most recently allocated sector.
- File 0 is the directory: 64-byte entries, 8 per block, copies of the
  file headers. File numbers 1-240 ($F0) are files.
- A QDOS file header (64 bytes) [filehedr.txt; TG]: $00 long length
  (including the 64-byte header on microdrive), $04 access byte, $05
  type (0 data/BASIC, 1 executable, 2 SROFF relocatable, 255
  directory), $06 8 bytes type info (type 1: long dataspace), $0E word
  name length, $10 36 bytes name, $34 long update date, $38 reference
  date, $3C backup date. Executables lose their type and dataspace if
  copied without the header (section 9).
- Capacity: at most ~236 usable sectors (~118K) [MDVPDF p.6].

### 6.6 Cartridge image formats

| Format | Size | Layout | Used by |
|---|---|---|---|
| **QLay .mdv** | fixed **174 930** bytes = 255 x 686 | per sector: $000 10 x $00 + $FF $FF (header preamble), $00C 14 header bytes + checksum, $01C 10 x $00 + $FF $FF, $028 file, block, checksum, $02C 6 x $00 + $FF $FF, $034 512 data, $234 checksum, $236 120 x $5A (gap filler) | QLay; **MAME** (`microdrv.cpp`: 255 x 686, rejects other sizes; reconstructs the gaps); **sQLux** and the MiSTer QL core ("255 sectors of 686 bytes"); vDrive (its page: "may also be used with ... QLay and Q-Emulator") |
| **Q-emuLator .mdv** ("Mdv*Dump", Mdump1/2) | variable | 46-byte header: $00 "Mdv*Dump", $08 header length, $0C offset of sector data (46), $10 bytes a sector (528, Mdump2 530), $12 sectors in dump, $13 sectors in the original, ... $24 flags (1/2), $28 offset of data in a sector (16), $2C offset of block header (14); then sectors of 14-byte header (no checksum) + file/block + 512 data (+ checksum in Mdump2, msb first); sector 0 first, then the highest good sector counting down | Q-emuLator (which also reads QLay images, per QL Forum users) |
| **MDI** | 136 170 = 255 x 534 | per sector: 16-byte header (with checksum), 4-byte block header, 512 data + 2 checksum; no preambles | the MDI driver (SMSQ/E, Q68); `CONVERT2_MDI` converts QLay images |

Sources: MDVPDF (all three; it is the djw "MDV image file formats"
document), MAME `microdrv.cpp` constants and `call_load`, sQLux
`docs/sqlux.md` (MDV1/MDV2), djw vDrive page, QL Forum
viewtopic.php?p=7087 and p=11366 (Q-emuLator).

Notes:

- QLay images store sector 0 first, then the others in ascending,
  descending or any order [MDVPDF p.2]. MAME plays the file order as
  the tape order; sQLux has an `MDV_REVERSE` option. Real tapes pass in
  descending sector order; QDOS finds sectors by number, so either
  works, only speed differs [derived].
- Unused/bad slots in QLay images: header flag $00, sector $00, name
  zeros, checksums $0F0F [MDVPDF p.2]. MAME treats a slot without the
  header's $FF $FF sync as unformatted tape.
- **Most common in collections: the QLay format** (MAME's software list
  `ql_cass` has quill.mdv, abacus.mdv, archive.mdv, easel.mdv and
  josiah.mdv, all 174 930 bytes; the djw vDrive game images are listed
  at 171K, i.e. 174 930 bytes) [MAME hash/ql_cass.xml; djw vDrive page].

## 7. QDOS boot (JS ROM) and Minerva

Sources: the boot report, from David Westbury's JS ROM disassembly
("W n" = line n of `jsrom.asm`; Lxxxx = ROM address; checked here for
L016A-L01C2 and the hardware set-up), the JM disassembly, Minerva 1.98
sources, TG 2.4, the QL Service Manual section 4 ("SM").

### 7.1 Reset to the jump into BASIC (JS 1.10)

1. Vectors: SSP = $00030000, PC = $0000016A [W 38-40].
2. RAM sizing (L016A): A4 = $40000; write A4 to (A4); stop if it
   doesn't read back or if $40000 no longer holds $40000 (an alias);
   else A4 += $10000 and repeat. RAMTOP = A4 [verified in the listing].
   $20000-$3FFFF is assumed present. See section 1 for what $40000 must
   return.
3. RAM test (L0184-L01CA) [verified in the listing]:
   - pass 1, for each long from $20000 to RAMTOP: write 0 and verify,
     write $FFFFFFFF and verify, write a long of ROM data (ROM $0000-
     $1FE3, cycled) and verify; `SF $18063` first (display on, mode 4,
     screen 0): the "tweed" pattern shows on screen [SM 2.2.6];
   - pass 2: verify each ROM long again (testing DRAM refresh after the
     delay pass 1 took), then clear it;
   - failure: fill screen 0 forever with $FFFF (white, pass 1) or
     $FF00 (green, pass 2) [L015C; SM 2.2.10].
   Estimated ~3 s for 128K [derived, BOOT report 2].
4. Hardware set-up (after the test) [verified in the listing]:

   | Write | Meaning |
   |---|---|
   | $18063 = $08 | display on, mode 8, screen 0 |
   | $18002 = $00 | serial mode, SER1, baud bits 0 ("net out high") |
   | long $061F0000 to $18020 | $18020 = $06, $18021 = $1F (clear all, masks off), $18022 = $00, $18023 = $00 |
   | $18021 = $1F | again (Minerva: the first sometimes fails) |
   | $18003 = $01 | "condition IPC write port" (line idle high; not a bit) |
   | SV_PCINT = $C0 | in RAM only: transmit + interface masks for later acknowledges |
   | 8 x ($18020 = $02, ~18 us, $00, ~18 us) | deselect all drives (L2C50) |

5. System variables at $28000 (SV_IDENT $D254), SSP $28480, tables,
   heap; BASIC at the top of RAM; SV_MCSTA = 8, SV_TMODE = 1 (9600)
   [W 265-362]. Then SR = $0000 (user mode, interrupts on) and the
   jump to BASIC.
6. No IPC command is sent during reset. The first IPC traffic is the
   STAT poll in the first frame interrupt [BOOT report 1].

### 7.2 ROM probes

- $0C000 (cartridge) then $C0000, $C4000 ... $FC000 in 16K steps: a ROM
  is present if its first long is $4AFB0001; then the ROM's name
  string (offset 8) is printed, its BASIC procedures (offset 4)
  linked, its init (offset 6) called [TG 2.4, 11.4; W 8302-8330].
- Minerva: $C000, $10000, $14000, then from RAMTOP upwards in 16K steps
  until the address wraps (needs the 20-bit bus); skips duplicates
  [BOOT report 8].

### 7.3 The F1/F2 prompt (JS)

- Windows are opened in mode 8 with the banner and the prompt
  `F1...monitor` / `F2...TV` [W 8319-8357; G SYS.MES].
- The key is read with IO.FBYTE (TRAP #3) on the console channel, no
  timeout, i.e. through the 50 Hz IPC poll (STAT, then RDKB): the IPC
  must deliver key code 57 (F1) or 59 (F2) with modifier nibble 0,
  giving $E8 or $EC; anything else is ignored [verified at L4AF2-L4B0E].
- F1: MT.DMODE mode 4, monitor (windows #0 512x50 at 0,206; #1, #2
  256x202). F2: mode 8, TV (windows 448 wide) [L4AF2-L4B0E; W
  8351; JM agrees; Goeller's modified listing says mode 4 for F2,
  which is his edit]. MT.DMODE clears $20000-$27FFF and writes $18063.

### 7.4 The boot file and an empty microdrive

- JS opens `BOOT` (no such device on a bare QL: fails without touching
  hardware), then `MDV1_BOOT` [W 8366-8380; TG 2.4].
- The microdrive open sets "open pending", selects drive 1, enables
  the gap interrupt ($18021 = SV_PCINT | $20 = $60), and **busy-waits
  until the gap interrupt handler resolves it** [W 2623-2637].
- With no cartridge: the drive is selected, GAP = 1, the gap interrupt
  fires; the handler waits for the gap to end, times out after
  ~0.58 s, marks the medium bad, deselects, and the open returns
  "not found"; the `>` cursor of SuperBASIC follows [BOOT report 4].
- **If no gap interrupt comes, the boot hangs forever** (the cursor
  never appears). So: a selected empty drive must show GAP = 1, and the
  gap bit must be (re)raised when the mask is enabled while the gap is
  present (section 3.2).
- With a cartridge: QDOS waits for sector 0 (the map; up to one tape
  revolution), reads the directory, then loads the file.

### 7.5 What the running system uses

- Level-2 handler (vector $68 -> $0352) reads $18021 once and services,
  in order: gap (microdrive), interface (IPC poll), transmit (next
  serial byte), frame (polled tasks: the IPC STAT/RDKB/RDS poll, the
  scheduler), external [W 402-425; section 3.2 for the acknowledges].
- Level 7 vector ($7C) -> RTE unless redirected; levels 1, 3-6 -> RTE
  [W 52-110, 179-187].
- RTC read as a long, twice [section 3.6].

### 7.6 Minerva (1.98) differences

[BOOT report 8, from the Minerva sources; GPL, read only]

- Reset SSP $4C024C02 (deliberately odd: $24C02 on 20 lines); ~157 ms
  delay; an I2C probe by reads of $BFDC-$BFDF (must just return ROM
  data); a read of $4000A looking for a Gold Card.
- Writes $18063 = $00; word $0001 to $18002 ($18002 = $00, $18003 =
  $01); $18021 = $1F, $18020 = $06, $18021 = $1F; deselects; then sends
  **IPC command 13 (BAUD) with nibble 1 while interrupts are still
  off** (the first IPC exchange; no timeout).
- RAM test in 16K steps from $28000, aliasing re-checked; RTC-seeded
  pattern; quick reset skips pass 1; on failure it prints the address
  and waits 15 s on the RTC.
- Prompt in mode 4: F1/F2 monitor/TV, F3/F4 the same with **dual
  screen** (system variables moved to $30000, so both $20000 and
  $28000 are screens, $18063 bit 7 flips them), SHIFT = 128K only,
  CTRL = skip the ROM scan; any Shift/Ctrl combination accepted
  ($E8-$F7). **Times out after 528 frames (~10.6 s)** and continues
  (TV, mode 8).
- A changed dual-screen/RAM choice causes a software reset: TRAP #0,
  the `RESET` instruction, then a jump through the reset vectors:
  **the 68008 `RESET` instruction must not clear CPU registers** (A5,
  D1 carry the choice).
- Interrupt priority external, frame, transmit, interface, gap
  (reverse of JS); services the interface interrupt since 1.94; needs
  $18021 bit 7 to toggle (baud clock) after serial output (3.1).
- Same IPC protocol (pipelined), same microdrive format and sequences,
  wider read tolerance (20-80 us a byte).

### 7.7 Minimum hardware to reach the SuperBASIC cursor

1. ROM at $00000-$0BFFF, writes there ignored; $40000 not aliasing
   RAM; 20-bit wrap.
2. $18063 bits 1, 3, 7.
3. Frame interrupt on $18021 bit 3 at 50 Hz, level 2 autovector, IPL
   only while bits 0-4 are pending, write semantics of 3.2.
4. IPC link: every $0C/$0E write completes (bit 6 back to 0); STAT
   answers 8 bits; RDKB the 4.6 format (F1 = 57, F2 = 59); Minerva also
   BAUD.
5. RTC ticking, readable as a long.
6. Microdrive: GAP = 1 when idle or empty; gap interrupt (re)raised
   with the mask; SCLK falling-edge selection.
7. Minerva only: `RESET` keeps registers; $18021 bit 7 toggling.

Timing estimates (no source gives them; derived from cycle counts):
reset to the F1/F2 prompt ~3 s (the RAM test); F1/F2 to the cursor
with no cartridge ~0.6-0.7 s; with a cartridge, up to a tape
revolution (~7-8 s) plus loading.

## 8. 68008 timing

### 8.1 The CPU in the QL

- MC68008, 48-pin DIP, **7.5 MHz** (the ZX8301's 15 MHz / 2) [Nasta
  8-15, 1205; MAME ql.cpp:881 `M68008(..., X1/2)`] [confirmed].
- 8-bit data bus, 20 address lines; a byte bus cycle is **4 clocks**
  with no wait states; a word access is two byte cycles (8 clocks), a
  long four (16) [M68000UM section 7 introduction: "it is assumed that
  both memory read and write cycles consist of four clock periods"].
- The 48-pin 68008 has IPL0 and IPL2 tied together (IPL0/2) and IPL1:
  only levels 2, 5 and 7 can be requested; the QL uses autovectors
  (VPA) for all of them [TG 611-615; MAME ql.cpp `ipc_port2_w`].
- Wait states on the QL: ROM none (DTACK = DS) [Nasta 879-880];
  on-board RAM per section 2.5; ZX8302 registers contended on issue 5
  and earlier boards [Nasta 941-946]; expansion cards their own.

### 8.2 Instruction times

Motorola publishes separate 8-bit tables (M68000 Family User's
Manual, section 7 "8-Bit Instruction Execution Times": MC68008,
MC68HC001/MC68EC000 in 8-bit mode). They follow one rule against the
16-bit tables (section 8): **every word bus cycle of the 68000 becomes
two byte cycles, so the 68008 time = the 68000 time + 4 clocks for
each word transferred (+8 for each long)**; byte operands cost the
same. Examples from the two tables [M68000UM tables 7-x / 8-x]:

| Instruction | 68000 | 68008 |
|---|---|---|
| NOP | 4(1/0) | 8(2/0) |
| MOVE.B Dn,Dn | 4(1/0) | 8(2/0) |
| MOVE.B (An),Dn | 8(2/0) | 12(3/0) |
| MOVE.W (An),Dn | 8(2/0) | 16(4/0) |
| MOVE.L Dn,(An) | 12(1/2) | 24(2/4) |
| MOVE.L (An),Dn | 12(3/0) | 24(6/0) |
| JMP (An) | 8(2/0) | 16(4/0) |
| RTS | 16(4/0) | 32(8/0) |
| RESET | 132(1/0) | 136(2/0) |
| Interrupt (autovector) | 44(5/3) | 72(9/6) |

(n(r/w): clocks (reads/writes), word cycles on the 68000, byte cycles
on the 68008.)

How emulators do it:

- **MAME**: a separate 68008 device generated from the same bus-cycle
  microcode with 8-bit handlers (`m68008.h`, generated handler tables
  `s_handlers_df8`, etc.), so each byte cycle takes its 4 clocks plus
  any wait the address map adds; the ZX8301 contention is a flat
  `adjust_icount(-8)` per contended RAM access [MAME m68008.h,
  zx8301.cpp `data_r`/`data_w`].
- **sQLux** (GPL, read only): its own table built from Motorola's
  section 8 figures, split into internal cycles and accesses: 68008
  cycles = internal + 4 x (2 x program words + byte reads + 2 x word
  reads + 4 x long reads + the same for writes); register-divisor
  DIVU/DIVS by Jorge Cwik's exact algorithm ("checked on a real QL:
  DIVU 100000/7 takes 122 cycles"); contention separately
  (`cycles68k.c`, `zx8301.c`).
- **Musashi** (used by esp32emu) has no 68008 type (only 68000 and
  up; `m68k.h` CPU types). Use the 68000 type, mask addresses to 20
  bits, and charge extra clocks per access in the memory callbacks:
  +0 for a byte, +4 for a word (8 instead of 4), +8 for a long (16
  instead of 8), instruction words included [derived from the rule
  above]. The interrupt acknowledge then comes to 72 clocks.
- The autovector acknowledge synchronises with the E clock (CLK / 10),
  adding 0-9 clocks; sQLux leaves it out [sQLux `cycles68k.c`; Motorola
  MC68008 data sheet].

Per frame: 149 760 clocks (PAL), of which contention takes a large
share for code and data in on-board RAM (section 2.5).

## 9. Software to test with

The user asked for URLs only; nothing here was downloaded (only page
listings were read).

### 9.1 ROM images (djw "QL ROM images" page)

https://sinclairql.net/djw/qlrom/index.html, files relative to it:

- JS (QDOS 1.10): https://sinclairql.net/djw/qlrom/jsrom.zip (35K)
- JM (1.03): https://sinclairql.net/djw/qlrom/jmrom.zip; AH, PM, TB,
  MG and the country versions likewise.
- Minerva 1.98: https://sinclairql.net/djw/qlrom/min198.zip ("freely
  distributable"); 1.89 (released for emulators): min189.zip; a 48K-
  padded 1.98 for Q-emuLator: min198_qem_rom.zip; Marcel Kilgus's I/O
  fix: minerva_198_io_fix.zip.
- No IPC ROM image there (only the disassembly).
- Permission (as the page states): AH, JM, JS, MG and TB "released for
  use in Europe"; North American rights with Paul Holmgren and Frank
  Davis (ask them); the Minerva ROMs "OK to use with emulators etc
  worldwide".
- MAME's ROM set uses JS as `ql.js 0000.ic33` (32K) + `ql.js 8000.ic34`
  (16K), CRCs 1bbad3b8 / c970800e; Minerva 1.89 `minerva.rom` 48K
  [MAME ql.cpp ROM_START].

### 9.2 Psion Quill, Abacus, Archive, Easel

Page: https://sinclairql.net/djw/psions/index.html ("freely copied for
QL users", Psion keeps the copyright).

- **Microdrive images, ready to insert** (the "vDrive versions",
  v2.35, from Rich Mellor), listed at 160K:
  https://sinclairql.net/djw/psions/quill.mdv,
  https://sinclairql.net/djw/psions/abacus.mdv,
  https://sinclairql.net/djw/psions/archive.mdv,
  https://sinclairql.net/djw/psions/easel.mdv.
  vDrive images are QLay-compatible (the djw vDrive page: they "may
  also be used with emulators which support the .MDV format (QLay and
  Q-Emulator)") [UNCERTAIN: the exact size, 160K here vs 171K =
  174 930 bytes on the vDrive page, was not checked; HEAD requests to
  the .mdv files returned HTTP 500].
- Zips, microdrive versions ("m" = microdrive, "f" = floppy):
  Quill 2.35 https://sinclairql.net/djw/psions/qui235m.zip (55 308
  bytes), Quill 2.30 https://sinclairql.net/djw/psions/mquil230.zip
  (55 220 bytes), Abacus aba235m.zip / mabac230.zip, Archive
  arc238m.zip / march230.zip, Easel eas235m.zip / measl230.zip.
  Their contents aren't described on the page: presumably the QDOS
  files zipped with QDOS Info-ZIP, which keeps each file's 64-byte
  header in a zip extra field (ID $FB4A, "QZHD" or "QDOS" + the header,
  big-endian) [Info-ZIP `proginfo/extrafld.txt`] [UNCERTAIN for these
  particular files]. Unzipped on a PC the headers are lost and
  executables fail with "bad parameter" [djw "Transferring files"
  page]. An emulator loading such zips must read that extra field.
- 720K floppy images (for disk systems): Quill235.img etc. on the same
  page.
- MAME's software list `ql_cass` names the same four as QLay-format
  `quill.mdv` etc., 174 930 bytes each, with CRCs (no download URL)
  [MAME hash/ql_cass.xml].

### 9.3 First test software

- Microdrive images (QLay format, playable directly): the djw vDrive
  page https://sinclairql.net/djw/vdrive/index.html, e.g.
  https://sinclairql.net/djw/vdrive/QLGames.mdv,
  https://sinclairql.net/djw/vdrive/PACMAN.MDV,
  https://sinclairql.net/djw/vdrive/SPACEINV.MDV,
  https://sinclairql.net/djw/vdrive/FROGGY.MDV,
  https://sinclairql.net/djw/vdrive/CHESS.MDV (Psion Chess),
  https://sinclairql.net/djw/vdrive/MCrunch.MDV; zipped sets:
  https://sinclairql.net/djw/vdrive/Freeware%20vDrive%20Images.zip and
  https://sinclairql.net/djw/vdrive/Freeware%20vDrive%20Images2.zip.
- Games page https://sinclairql.net/djw/games/index.html: microdrive
  versions for a 128K QL, e.g. QSlam ("MDV version, runs in 128K RAM")
  https://sinclairql.net/djw/games/qslam.zip, Pitman (runs unexpanded)
  https://sinclairql.net/djw/games/Pitman_mdv.zip, M-Crunch
  https://sinclairql.net/djw/games/mcrunch_mdv.zip, Royal Game of Ur
  (zip with URql.mdv) https://sinclairql.net/djw/games/ur.zip, the
  original Sinclair games cartridge (needs Toolkit 2)
  https://sinclairql.net/djw/games/QL_Games_Cartridge_Tk2.zip.
- Benchmarks page https://sinclairql.net/djw/benchmrk/index.html:
  QSBB, "the QLay SuperBASIC Benchmark program"
  https://sinclairql.net/djw/benchmrk/qsbb.zip (636 bytes: presumably a
  BASIC listing to LOAD; the page doesn't say); Test 909 (loop, maths,
  text, scroll and graphics tests)
  https://sinclairql.net/djw/benchmrk/test909.zip; QSI (QL Speed Index)
  https://sinclairql.net/djw/benchmrk/qsi.zip; Dhrystone/Sieve
  https://sinclairql.net/djw/benchmrk/bench.zip. The page gives no
  timings for a standard QL and doesn't say which need Toolkit 2.
- No software at all is needed for the first milestones: the RAM test
  pattern, the F1/F2 prompt, the SuperBASIC cursor with an empty mdv1
  (section 7), then typed BASIC (`PRINT DATE$`, `BEEP 10000,10`,
  `KEYROW`, `MODE 8`, `FLASH`).

---

## 10. Floppy disks: the Trump Card

Added 2026-11-18, after the rest. QL disk interfaces: Sinclair's/Micro
Peripherals' (device FDK1_), Sandy's SuperQBoard and the Miracle Trump
Card and Gold Card (QJump's driver: FLP1_), 720 KB double-density the
common format (the Gold Card adds 1.44 and 3.2 MB). The emulator has the
Trump Card: its ROM is on the djw ROM page (`trumpcard131.zip`, V1.31,
32 KB, CRC32 584c7835, MAME's default) and MAME's `trumpcard.cpp`
(BSD-3-Clause) gives its map [one source; the ROM's behaviour agrees]:

- ROM at $10000-$17FFF. Any read of $0C000-$0FFFF (QDOS's probe of the
  ROM port) makes it appear at $C0000-$C7FFF too, where QDOS finds its
  $4AFB0001 header and runs its initialisation; any read of
  $10000-$17FFF hides it there again (its code then runs at $10000).
- RAM from $40000 (256, 512 or 768 KB).
- A WD1772 at $1C000-$1C003 (status/command, track, sector, data), 8 MHz,
  double density; no interrupt line: the driver polls busy and DRQ.
- A write-only latch at $1E000: bit 0 the second drive, bit 1 the first,
  bit 2 the motor, bit 3 the side.

What the V1.31 ROM does with it [derived: `QL_FLP_LOG`, `QL_DASM`]:
restore and seek with verify; read sector ($88) and write sector ($AA) one
at a time; Write Track ($FA) with 10 IDs a track (the tenth's data doesn't
fit: 9 sectors of 512 stored, interleave 1); it tells one side from two by
formatting side 1 of track 0 with IDs saying track 1, then side 0, then a
Read Address ($C8) on side 1 whose bytes it never reads: when busy drops
it takes the ID's track from the sector register (1: two sides). A write
sector to sector 255 forced off at once (a delay or a head load, it
seems). Toolkit 2's commands are linked by `TK2_EXT`; `FLP_USE mdv` makes
the floppy driver answer to MDV. With a disk in FLP1_ the card boots
`flp1_boot`, else `mdv1_boot`. QDOS's floppy format: 1440 sectors in
blocks of 3, the header and map in block 0 ("QL5A", the medium name, the
geometry and the sector tables; physical sectors 1, 4 and 7 of track 0),
the logical sectors interleaved and skewed by 5 a cylinder [derived from
the card's own disks; CLAUDE.md has the whole layout].

## Sources

Tags used in the text, then URLs:

- **TG n**: Sinclair QL Technical Guide (Tony Tebby, David Karlin,
  Sinclair Research 1985), text of
  https://sinclairql.net/djw/docs/manuals/qltm.pdf (also .doc), line n
  of `pdftotext -layout`.
- **UG**: QL User Guide, online:
  https://sinclairql.net/djw/docs/ebooks/olqlug/index.html (Concepts:
  `QL%20Manual%20-%20Concepts.htm`, Keywords: `QL%20Manual%20-%20Keywords.htm`).
- **MAME**: github.com/mamedev/mame, master on 2026-11-18:
  `src/mame/sinclair/ql.cpp`, `zx8301.cpp/.h`, `zx8302.cpp/.h`,
  `src/devices/imagedev/microdrv.cpp`, `hash/ql_cass.xml` (BSD-3).
- **IPC report / IPC $xxxx**: Jan Venema's annotated IPC 8049
  disassembly (1997), https://sinclairql.net/djw/docs/disassem/ipc8049.zip,
  analysed for this document (cycle counts are this analysis's).
- **JS Lxxxx / BOOT-W n**: JS ROM disassembly by David Westbury,
  https://sinclairql.net/djw/docs/disassem/jsromasm.zip (labels are ROM
  addresses); also https://sinclairql.net/djw/docs/disassem/jsrom.zip
  (Goeller, modified source) and jmrom.zip (JM).
- **BOOT-Min / Minerva**: Minerva 1.98 sources,
  https://sinclairql.net/djw/docs/disassem/m198src.zip (GPL v2 per
  web reports; read only).
- **Nasta n**: Nasta's ZX8301 notes from the QL Forum, compiled by
  Derek Stewart, https://sinclairql.net/djw/docs/hardware/8301.zip,
  line n of the PDF text.
- **sQLux**: github.com/SinclairQL/sQLux (GPL; read only).
- **keyboard.png**: https://sinclairql.net/djw/docs/hardware/keyboard.png
  (Lee Privett's corrected matrix).
- **BOOT report**: the analysis of the JS ROM (Westbury), JM and
  Minerva sources made for this document (section 7); "W n" = line n
  of Westbury's `jsrom.asm`. The reset sequence, RAM sizing/test and
  F1/F2 code were re-checked in the listing for this file.
- **Microdrives.pdf**: Tobias Fröschle, "Microdrives – The Inner
  Workings", https://sinclairql.net/djw/docs/hardware/Microdrives.pdf
- **MDVPDF**: "MDV image file formats" (QLay, Q-emuLator Mdump 1/2, MDI,
  cartridge format),
  https://sinclairql.net/djw/docs/formats/MDV%20image%20file%20formats.pdf
- **filehedr.txt**: QDOS file headers,
  https://sinclairql.net/djw/docs/formats/filehedr.txt
- **QLschem.txt**: corrections to the issue 5/6 schematics (ZX8302 on
  the CPU data bus on issue 6),
  https://sinclairql.net/djw/docs/hardware/QLschem.txt
- **QL_Network_Protocols.pdf**:
  https://sinclairql.net/djw/docs/hardware/QL_Network_Protocols.pdf
- **SM**: QL Service Manual, online
  https://www.sinclairql.net/srv/index.html (section 4:
  https://www.sinclairql.net/srv/qlsm4.html); scan
  https://sinclairql.net/djw/docs/manuals/QL%20Service%20Manual.pdf
- **M68000UM**: Motorola M68000 8-/16-/32-Bit Microprocessors User's
  Manual (section 7: 8-bit instruction execution times; section 8:
  16-bit), https://web-docs.gsi.de/~kraemer/COLLECTION/MOTOROLA/m68000um.pdf
  (Rev 8 also at
  https://cdn.hackaday.io/files/1805367724052224/M68000UM_AD_M68000_Microprocessor_Users_Manual_Rev8_1993.pdf);
  MC68008 data sheet
  https://bitsavers.mirrorservice.org/components/motorola/68000/MC68008_Data_Sheet_Apr85.pdf
- **Info-ZIP extrafld.txt** (QDOS extra field $FB4A):
  https://raw.githubusercontent.com/LuaDist/zip/master/proginfo/extrafld.txt
- **djw pages**: documents index https://sinclairql.net/djw/docs/index.html,
  hardware https://sinclairql.net/djw/docs/hardware/index.html,
  manuals https://sinclairql.net/djw/docs/manuals/index.html,
  formats https://sinclairql.net/djw/docs/formats/index.html,
  disassemblies https://sinclairql.net/djw/docs/disassem/index.html,
  ROMs https://sinclairql.net/djw/qlrom/index.html,
  Psion https://sinclairql.net/djw/psions/index.html,
  vDrive https://sinclairql.net/djw/vdrive/index.html,
  games https://sinclairql.net/djw/games/index.html,
  benchmarks https://sinclairql.net/djw/benchmrk/index.html,
  file transfer and zip https://sinclairql.net/djw/gen/pcqlxfer/index.html.
  Also the 1984 Software Developers Guide (an earlier Technical Guide):
  https://www.sinclairql.net/downloads/1984-00_Sinclair_QL_Software_Developers_Guide_by_Tony_Tebby_and_David_Karlin-OCRed-SQPP.pdf
  (not read).
- **QL Forum** (Q-emuLator .mdv): https://www.theqlforum.com/viewtopic.php?p=7087,
  https://www.theqlforum.com/viewtopic.php?p=11366
- **MAME files**:
  https://github.com/mamedev/mame/blob/master/src/mame/sinclair/ql.cpp,
  .../sinclair/zx8301.cpp, .../sinclair/zx8302.cpp,
  .../src/devices/imagedev/microdrv.cpp,
  .../src/devices/cpu/m68000/m68008.h, .../hash/ql_cass.xml
- **sQLux files** (GPL, read only): `ipc.c`, `QL_hardware.c`,
  `zx8301.c`, `mdv.c`, `cycles68k.c`, `include/qlkeys.h`,
  `src/SDL2screen.c`, `docs/sqlux.md` at
  https://github.com/SinclairQL/sQLux (commit b1f8dda, 2026-10-14).
  uQLx and QLay sources were not read separately (sQLux descends from
  uQLx; `qlkeys.h` is QLay's).

Not found or not checked: a primary statement of the flash period; a
real-QL measurement of BEEP frequency and duration; the exact
interface-interrupt source; the NTSC bit; the contention outside the
picture (two hardware sources disagree); the sizes and contents of the
Psion zips and .mdv files (not downloaded).
