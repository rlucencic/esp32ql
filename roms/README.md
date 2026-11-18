# Sinclair QL ROM images

Put the system ROM images here (not part of this repository). `idf.py flash`
writes them into 64 KB slots of the `ql_roms` flash partition; only the ones
you want are needed.

| File | What | Size | Setup screen |
|---|---|---|---|
| `js.rom` | Sinclair JS ROM: QDOS 1.10 and SuperBASIC (UK) | 48 KB | ROM: JS |
| `minerva.rom` | Minerva 1.98 (Laurence Reeves), a rewritten QDOS | 48822 bytes | ROM: Minerva |
| `trumpcard.rom` | Miracle Systems Trump Card V1.31: its floppy disk driver (FLP1_, FLP2_), RAM disks, Toolkit 2.23 | 32 KB (CRC32 584c7835) | Floppy: Trump Card |

A file of the same name in the `ql/roms` folder on the SD card is used
instead of the flashed one.

All three are on Dilwyn Jones's QL ROM page, https://sinclairql.net/djw/qlrom/
(`jsrom.zip`, `min198.zip`, `trumpcard131.zip`). Of its ROMs the page says
"All remain the copyright material of those who created them"; the
Sinclair ROMs' rights are held by Paul Holmgren and Frank Davis for North
America (ask before using them there), and "the Minerva ROMs are OK to use
with emulators etc worldwide"; Minerva's sources are GPL.
