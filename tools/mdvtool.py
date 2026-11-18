#!/usr/bin/env python3
"""Files on QL Microdrive cartridge images (QLay .mdv: 255 slots of 686
bytes), in QDOS's own layout (docs/ql-hardware.md 6.5):

    tools/mdvtool.py ls image.mdv
    tools/mdvtool.py add image.mdv file|archive.zip ...

`add` needs a cartridge QDOS formatted (FORMAT mdv1_name in the emulator,
on a blank cartridge from the setup screen or web page). Files from a zip
keep the QDOS header of its SMS/QDOS extra field (type, data space: an
executable needs it, QDOS's own zip sets it); other files go on as data,
their name with '.' as '_'. Each file is its 64-byte header and its bytes
in 512-byte blocks, one a free sector; the map (sector 0: a file and block
number a sector) and the directory (file 0: a copy of each file's header)
are updated.
"""
import sys
import zipfile

SLOTS, SLOT = 255, 686
FREE, MAP = 0xFD, 0xF8


def checksum(data):
    return (0x0F0F + sum(data)) & 0xFFFF


class Cartridge:
    def __init__(self, path):
        self.path = path
        self.img = bytearray(open(path, "rb").read())
        if len(self.img) != SLOTS * SLOT:
            sys.exit(f"{path}: not a QLay cartridge image ({len(self.img)} bytes)")
        self.slot_of = {}               # sector number -> slot
        for s in range(SLOTS):
            r = self.img[s * SLOT:(s + 1) * SLOT]
            if r[10:12] == b"\xff\xff" and r[12] == 0xFF and \
                    int.from_bytes(r[26:28], "little") == checksum(r[12:26]):
                self.slot_of[r[13]] = s
        if 0 not in self.slot_of:
            sys.exit(f"{path}: no sector 0: not formatted (FORMAT it on the QL first)")
        self.map = bytearray(self.block_data(0))

    def block_data(self, sector):
        s = self.slot_of[sector] * SLOT
        return self.img[s + 52:s + 564]

    def write_block(self, sector, file, block, data):
        data = bytes(data).ljust(512, b"\0")[:512]
        s = self.slot_of[sector] * SLOT
        self.img[s + 28:s + 40] = b"\0" * 10 + b"\xff\xff"
        self.img[s + 40] = file
        self.img[s + 41] = block
        self.img[s + 42:s + 44] = checksum(bytes([file, block])).to_bytes(2, "little")
        self.img[s + 44:s + 52] = b"\0" * 6 + b"\xff\xff"
        self.img[s + 52:s + 564] = data
        self.img[s + 564:s + 566] = checksum(data).to_bytes(2, "little")
        self.map[2 * sector] = file
        self.map[2 * sector + 1] = block

    def blocks_of(self, file):
        found = {}
        for sector in range(255):
            if self.map[2 * sector] == file and sector in self.slot_of:
                found[self.map[2 * sector + 1]] = sector
        return found

    def read_file(self, file):
        blocks = self.blocks_of(file)
        return b"".join(bytes(self.block_data(blocks[b])) for b in sorted(blocks))

    def free_sectors(self):
        return [s for s in range(1, 255) if self.map[2 * s] == FREE and s in self.slot_of]

    def directory(self):
        d = bytearray(self.read_file(0)) if self.blocks_of(0) else bytearray(64)
        length = int.from_bytes(d[0:4], "big") or 64
        return d[:max(length, 64)]

    def entries(self):
        d = self.directory()
        for n in range(1, len(d) // 64):
            e = d[n * 64:(n + 1) * 64]
            if int.from_bytes(e[0:4], "big"):
                yield n, e

    def add(self, name, header, data):
        d = self.directory()
        used = {n for n, _ in self.entries()}
        number = next(n for n in range(1, 0xF0) if n not in used)
        # free the file's old blocks if the name is there already
        for n, e in list(self.entries()):
            if e[16:16 + int.from_bytes(e[14:16], "big")].decode("latin-1").lower() == name.lower():
                for sector in self.blocks_of(n).values():
                    self.map[2 * sector] = FREE
                    self.map[2 * sector + 1] = 0
                d[n * 64:(n + 1) * 64] = bytes(64)
                number = n
        h = bytearray(header.ljust(64, b"\0")[:64])
        h[0:4] = (len(data) + 64).to_bytes(4, "big")
        raw = name.encode("latin-1")[:36]
        h[14:16] = len(raw).to_bytes(2, "big")
        h[16:52] = raw.ljust(36, b"\0")
        content = bytes(h) + data
        free = self.free_sectors()
        blocks = (len(content) + 511) // 512
        if blocks > len(free):
            sys.exit(f"{name}: {blocks} blocks, {len(free)} free sectors: the cartridge is full")
        for b in range(blocks):
            self.write_block(free[b], number, b, content[b * 512:(b + 1) * 512])
        # the directory: entry n is file n's header
        if len(d) < (number + 1) * 64:
            d += bytes((number + 1) * 64 - len(d))
        d[number * 64:(number + 1) * 64] = h
        d[0:4] = len(d).to_bytes(4, "big")
        old = self.blocks_of(0)
        free = [s for s in self.free_sectors()]
        for b in range((len(d) + 511) // 512):
            sector = old.get(b) or free.pop(0)
            self.write_block(sector, 0, b, d[b * 512:(b + 1) * 512])
        print(f"  {name:36} {len(data):7} bytes  type {h[5]}" +
              (f"  dataspace {int.from_bytes(h[6:10], 'big')}" if h[5] == 1 else "") +
              f"  file {number}, {blocks} blocks")

    def save(self):
        self.write_block(0, MAP, 0, self.map)
        open(self.path, "wb").write(self.img)


def qdos_header(info):
    """The 64-byte QDOS header in a zip entry's SMS/QDOS extra field (ID $FB4A)"""
    extra = info.extra
    i = 0
    while i + 4 <= len(extra):
        tag = int.from_bytes(extra[i:i + 2], "little")
        size = int.from_bytes(extra[i + 2:i + 4], "little")
        if tag == 0xFB4A and size >= 64:
            return extra[i + 4 + size - 64:i + 4 + size]
        i += 4 + size
    return None


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    cmd, cart = sys.argv[1], Cartridge(sys.argv[2])
    if cmd == "ls":
        for n, e in cart.entries():
            name = e[16:16 + int.from_bytes(e[14:16], "big")].decode("latin-1")
            print(f"  {name:36} {int.from_bytes(e[0:4], 'big') - 64:7} bytes  type {e[5]}")
        print(f"  {len(cart.free_sectors())} sectors free")
        return 0
    if cmd != "add":
        print(__doc__)
        return 1
    for path in sys.argv[3:]:
        if path.lower().endswith(".zip"):
            with zipfile.ZipFile(path) as z:
                for info in z.infolist():
                    if info.is_dir():
                        continue
                    header = qdos_header(info)
                    name = info.filename.split("/")[-1]
                    if header and int.from_bytes(header[14:16], "big"):
                        name = header[16:16 + int.from_bytes(header[14:16], "big")].decode("latin-1")
                    else:
                        name = name.replace(".", "_")
                    cart.add(name, header or bytes(64), z.read(info))
        else:
            name = path.split("/")[-1].replace(".", "_")
            cart.add(name, bytes(64), open(path, "rb").read())
    cart.save()
    return 0


if __name__ == "__main__":
    sys.exit(main())
