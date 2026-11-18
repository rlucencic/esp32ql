#!/usr/bin/env python3
"""Check a QLay Microdrive image (255 slots of 686 bytes): each slot's
header and block records, their syncs and checksums, and what QDOS made of
them (sector numbers, the map in sector 0, the directory).

    tools/mdvcheck.py image.mdv [-v]
"""
import sys

SLOTS, SLOT = 255, 686


def checksum(data):
    return (0x0F0F + sum(data)) & 0xFFFF


def word_lsb(b, i):
    return b[i] | (b[i + 1] << 8)


def main():
    path = sys.argv[1]
    verbose = "-v" in sys.argv
    img = open(path, "rb").read()
    if len(img) != SLOTS * SLOT:
        print(f"{path}: {len(img)} bytes, not {SLOTS * SLOT}: not a QLay image")
        return 1
    sectors = {}
    bad = []
    for s in range(SLOTS):
        rec = img[s * SLOT:(s + 1) * SLOT]
        if rec[10:12] != b"\xff\xff":
            if verbose:
                print(f"slot {s:3}: blank")
            continue
        hdr = rec[12:26]
        hok = word_lsb(rec, 26) == checksum(hdr) and hdr[0] == 0xFF
        number = hdr[1]
        name = hdr[2:12].decode("latin-1").rstrip()
        bok = rec[38:40] == b"\xff\xff" and word_lsb(rec, 42) == checksum(rec[40:42])
        dok = rec[50:52] == b"\xff\xff" and word_lsb(rec, 564) == checksum(rec[52:564])
        if rec[38:40] == b"\xff\xff" and rec[40] == 0xFD:
            bok = dok = True            # a free sector: FORMAT's long block, whatever its layout
        if verbose or not (hok and bok and dok):
            print(f"slot {s:3}: sector {number:3} '{name}' header {'ok' if hok else 'BAD'}"
                  f" block {'ok' if bok else 'BAD'} (file {rec[40]:02x} block {rec[41]:02x})"
                  f" data {'ok' if dok else 'BAD'}")
        if hok and bok and dok:
            if number in sectors:
                print(f"slot {s:3}: sector {number} again (first in slot {sectors[number]})")
            sectors[number] = s
        else:
            bad.append(s)
    print(f"{len(sectors)} good sectors (highest {max(sectors) if sectors else '-'}), {len(bad)} bad slots")
    if 0 in sectors:
        m = img[sectors[0] * SLOT + 52:sectors[0] * SLOT + 564]
        used = sum(1 for i in range(255) if m[2 * i] < 0xF8)
        free = sum(1 for i in range(255) if m[2 * i] == 0xFD)
        print(f"map (sector 0): file {img[sectors[0] * SLOT + 40]:02x}, {used} sectors used, {free} free")
        # the directory: file 0's blocks, 64-byte entries (the first is its own header)
        blocks = {}
        for number, s in sectors.items():
            rec = img[s * SLOT:(s + 1) * SLOT]
            if rec[40] == 0:
                blocks[rec[41]] = rec[52:564]
        directory = b"".join(blocks[b] for b in sorted(blocks))
        for i in range(64, len(directory) - 63, 64):
            e = directory[i:i + 64]
            length = int.from_bytes(e[0:4], "big")
            if length == 0:
                continue
            name = e[16:16 + int.from_bytes(e[14:16], "big")].decode("latin-1")
            info = int.from_bytes(e[6:10], "big")
            print(f"  {name:36} {length - 64:7} bytes  type {e[5]}" + (f"  dataspace {info}" if e[5] == 1 else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
