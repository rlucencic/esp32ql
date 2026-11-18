#!/usr/bin/env python3
"""The QL's screen from a board over WiFi, through the debugger (TCP 6502).

qlshot.py HOST out.png [8] [second]

Reads the 32 KB screen ($20000, or $28000 with "second") with the
debugger's `read` and draws it as mode 4 (512 x 256, black red green white),
or with "8" as mode 8 (256 x 256 doubled, eight colours, flashing not
shown). For when the board's USB console isn't to hand; the debugger takes
one client at a time and lets the machine run on when this one leaves.
"""
import json
import socket
import sys

from PIL import Image


def main():
    host, out = sys.argv[1], sys.argv[2]
    mode8 = "8" in sys.argv[3:]
    base = 0x28000 if "second" in sys.argv[3:] else 0x20000
    s = socket.create_connection((host, 6502), timeout=10)
    f = s.makefile("rw")

    def req(i, cmd):
        f.write("%d %s\n" % (i, cmd))
        f.flush()
        while True:
            d = json.loads(f.readline())
            if d.get("id") == i:
                if not d.get("ok"):
                    sys.exit("%s: %s" % (cmd, d.get("error")))
                return d

    req(1, "hello")
    mem = bytearray()
    for k in range(8):
        mem += bytes.fromhex(req(10 + k, "read 0 %d 4096" % (base + 4096 * k))["data"])
    req(99, "detach")

    img = Image.new("RGB", (512, 256))
    px = img.load()
    for y in range(256):
        for x in range(0, 512, 8):
            a = y * 128 + x // 4
            hi, lo = mem[a], mem[a + 1]
            if mode8:
                for p in range(4):          # G F pairs in the even byte, R B in the odd
                    g = (hi >> (7 - 2 * p)) & 1
                    r = (lo >> (7 - 2 * p)) & 1
                    b = (lo >> (6 - 2 * p)) & 1
                    c = (255 * r, 255 * g, 255 * b)
                    px[x + 2 * p, y] = px[x + 2 * p + 1, y] = c
            else:
                for p in range(8):          # green bits in the even byte, red in the odd
                    g = (hi >> (7 - p)) & 1
                    r = (lo >> (7 - p)) & 1
                    px[x + p, y] = (255 * r, 255 * g, 255 * (g & r))
    img.save(out)


if __name__ == "__main__":
    main()
