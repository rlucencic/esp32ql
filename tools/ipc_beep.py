#!/usr/bin/env python3
"""The IPC's beeper against beep.c: a minimal 8049 interpreter runs the
sound routine from Jan Venema's annotated IPC disassembly (ipc8049.txt in
https://sinclairql.net/djw/docs/disassem/ipc8049.zip: not in this
repository), the host build gives beep.c's edges (QL_BEEP_EDGES), and each
speaker edge's time in machine cycles is compared.

    tools/ipc_beep.py ipc8049.txt [cases] [seed]

Some fixed cases (plain beeps, steps, wraps, bounces, random, fuzz), then
random parameter sets; 300 edges each. The interpreter knows only the
instructions the sound path uses (IPC $004A, $0100-$0115, $0300-$0441,
$0700-$074E); an instruction costs 2 machine cycles if it has two bytes or
is RET, RETR, IN, OUTL, MOVX, MOVP or JMPP, else 1 (the listing's table).
The interrupt's latency: 1 cycle for the instruction under way, 2 for its
call (beep.c's LATENCY 3).
"""
import os
import random
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "../components/ql/host/build/ql_host")
ROM = os.path.join(HERE, "../roms/js.rom")

code = {}


def load(path):
    for l in open(path, encoding="latin-1").read().splitlines():
        m = re.match(r'^([0-9A-F]{4}): ((?:[0-9A-F]{2} )+)\s*([A-Z0-9]+)\s*([^ ]*)', l)
        if m:
            code[int(m.group(1), 16)] = (len(m.group(2).split()), m.group(3), m.group(4))


TWO = {'RET', 'RETR', 'IN', 'OUTL', 'MOVX', 'MOVP', 'JMPP'}


class CPU:
    def __init__(self):
        self.ram = [0] * 128
        self.A = 0
        self.C = 0
        self.bank = 0
        self.F0 = 1
        self.F1 = 0
        self.P2 = 0
        self.T = 0
        self.stack = []
        self.cycles = 0
        self.edges = []
        self.started = None         # cycle count at STRT T

    def reg(self, n):
        return n + (0x18 if self.bank else 0)

    def val(self, op):
        if op.startswith('#'):
            return int(op[1:], 16)
        if op.startswith('@R'):
            return self.ram[self.ram[self.reg(int(op[2]))] & 0x7f]
        if op.startswith('R'):
            return self.ram[self.reg(int(op[1]))]
        if op == 'A':
            return self.A
        if op == 'T':
            return self.T
        raise ValueError(op)

    def put(self, op, v):
        v &= 0xff
        if op == 'A':
            self.A = v
        elif op == 'T':
            self.T = v
        elif op.startswith('@R'):
            self.ram[self.ram[self.reg(int(op[2]))] & 0x7f] = v
        elif op.startswith('R'):
            self.ram[self.reg(int(op[1]))] = v
        else:
            raise ValueError(op)

    def run(self, pc, stop):
        """Run from pc until a RET/RETR at depth 0 or pc in stop; the pc"""
        depth = 0
        while pc not in stop:
            n, mn, ops = code[pc]
            self.cycles += 2 if n == 2 or mn in TWO else 1
            nxt = pc + n
            a = ops.split(',')
            target = None
            if mn in ('JMP', 'CALL', 'JZ', 'JNZ', 'JC', 'JNC', 'JF0', 'JF1', 'DJNZ') or mn.startswith('JB'):
                target = int(a[-1][1:], 16)
            if mn == 'MOV':
                self.put(a[0], self.val(a[1]))
            elif mn in ('ADD', 'ADDC'):
                s = self.A + self.val(a[1]) + (self.C if mn == 'ADDC' else 0)
                self.C = 1 if s > 255 else 0
                self.A = s & 0xff
            elif mn == 'ANL':
                if a[0] == 'P2':
                    self.P2 &= self.val(a[1])
                else:
                    self.A &= self.val(a[1])
            elif mn == 'ORL':
                if a[0] == 'P2':
                    self.P2 |= self.val(a[1])
                else:
                    self.A |= self.val(a[1])
            elif mn == 'INC':
                self.put(a[0], self.val(a[0]) + 1)
            elif mn == 'DEC':
                self.put(a[0], self.val(a[0]) - 1)
            elif mn == 'CPL':
                if a[0] == 'A':
                    self.A ^= 0xff
                else:
                    self.C ^= 1
            elif mn == 'CLR':
                if a[0] == 'A':
                    self.A = 0
                elif a[0] == 'C':
                    self.C = 0
                elif a[0] == 'F1':
                    self.F1 = 0
            elif mn == 'SWAP':
                self.A = ((self.A << 4) | (self.A >> 4)) & 0xff
            elif mn == 'RLC':
                c = self.A >> 7
                self.A = ((self.A << 1) | self.C) & 0xff
                self.C = c
            elif mn == 'RRC':
                c = self.A & 1
                self.A = (self.A >> 1) | (self.C << 7)
                self.C = c
            elif mn == 'JMP':
                nxt = target
            elif mn in ('JZ', 'JNZ', 'JC', 'JNC', 'JF0', 'JF1') or mn.startswith('JB'):
                cond = {'JZ': self.A == 0, 'JNZ': self.A != 0, 'JC': self.C == 1, 'JNC': self.C == 0,
                        'JF0': self.F0 == 1, 'JF1': self.F1 == 1}.get(mn)
                if mn.startswith('JB'):
                    cond = (self.A >> int(mn[2])) & 1
                if cond:
                    nxt = (pc & 0xf00) | (target & 0xff)
            elif mn == 'DJNZ':
                v = (self.val(a[0]) - 1) & 0xff
                self.put(a[0], v)
                if v:
                    nxt = (pc & 0xf00) | (target & 0xff)
            elif mn == 'CALL':
                self.stack.append((nxt, self.C, self.bank, self.F0))
                depth += 1
                nxt = target
            elif mn in ('RET', 'RETR'):
                if depth == 0:
                    return None
                nxt, c, b, f0 = self.stack.pop()
                depth -= 1
                if mn == 'RETR':
                    self.C, self.bank, self.F0 = c, b, f0
            elif mn == 'SEL':
                self.bank = 1 if ops == 'RB1' else 0
            elif mn == 'STRT':
                if ops == 'T':
                    self.started = self.cycles
            elif mn in ('STOP', 'EN', 'DIS', 'NOP'):
                pass
            else:
                raise ValueError(f'{pc:04X} {mn} {ops}')
            if mn in ('ANL', 'ORL') and a[0] == 'P2':
                self.edges.append((self.cycles, (self.P2 >> 1) & 1))
            pc = nxt
        return pc


def beep(p1, p2, interval, duration, step, wrap, random, fuzzy, edges=40, latency=1):
    cpu = CPU()
    cpu.ram[0x1C] = 0x40            # bank 1's R4: the multiplier's high byte in the random step
    cpu.ram[0x25], cpu.ram[0x26] = 0x12, 0x34
    params = [p1, p2, interval & 255, interval >> 8, duration & 255, duration >> 8,
              ((step & 15) << 4) | (wrap & 15), ((random & 15) << 4) | (fuzzy & 15)]
    cpu.ram[0x3A:0x42] = params
    cpu.bank = 0
    cpu.run(0x030A, {0x031D})       # INSO after the bytes: choose the pitch, counters, sound on
    cpu.bank = 0
    cpu.run(0x004A, set())          # the main loop starts it: the first edge, the timer
    out = []
    for _ in range(edges * 4):
        if len(cpu.edges) >= edges:
            break
        t = cpu.T if cpu.T else 256
        # STRT T to the overflow, the interrupt's call, then the ISR
        cpu.cycles = cpu.started + (256 - cpu.T if cpu.T else 256) * 32 + latency
        cpu.stack.append((None, cpu.C, cpu.bank, cpu.F0))   # the interrupt's call
        cpu.cycles += 2
        pc = cpu.run(0x0007, {0x004A})
        cpu.stack.pop()
        if pc == 0x004A:            # sound off: the pin went low, the timer goes to the serial side
            break
        if cpu.ram[0x22] & 0x20:    # the main loop ends the sound (IPC $0600)
            cpu.ram[0x20] &= 0xFD
    t0 = cpu.edges[0][0]
    return [(t - t0, lv) for t, lv in cpu.edges[:edges]]


def model(p, n):
    """beep.c's edges for the INSO bytes p (the same random state)"""
    out = subprocess.run([HOST, ROM, str(n)], env={"QL_BEEP_EDGES": ",".join(map(str, p + [0x40, 0x3412]))},
                         capture_output=True, text=True).stdout.split()
    return [int(x) for x in out[4:]]   # after "ROM <path>: N bytes"


def oracle(p, n):
    p1, p2, ilo, ihi, dlo, dhi, sw, rf = p
    e = beep(p1, p2, ilo | ihi << 8, dlo | dhi << 8, sw >> 4, sw & 15, rf >> 4, rf & 15, edges=n)
    return [t for t, _ in e]


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    load(sys.argv[1])
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 300
    rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)
    cases = [[51, 51, 0, 0, 0, 0, 0, 0], [51, 51, 0, 0, 0xe8, 3, 0, 0], [0, 0, 0, 0, 0, 0, 0, 0],
             [10, 60, 0, 0, 0, 0, 0x10, 0], [60, 10, 0, 0, 0, 0, 0xf0, 0], [10, 60, 100, 0, 0, 0, 0x33, 0],
             [10, 200, 50, 0, 0, 0, 0x5f, 0], [20, 40, 0, 0, 0, 0, 0x22, 0x00], [30, 30, 0, 0, 0, 0, 0, 0x8b],
             [20, 90, 10, 0, 0, 0, 0x30, 0xc0], [20, 90, 10, 0, 0x10, 0x27, 0x30, 0x99]]
    for _ in range(count):
        cases.append([rng.randrange(256), rng.randrange(256), rng.randrange(256), rng.choice([0, 0, 1]),
                      rng.choice([0, rng.randrange(256)]), rng.choice([0, 0, 1, 40]), rng.randrange(256),
                      rng.choice([0, 0, rng.randrange(256)])])
    bad = 0
    for p in cases:
        m, o = model(p, 300), oracle(p, 300)
        k = min(len(m), len(o))
        diff = next((i for i in range(k) if m[i] != o[i]), None)
        if diff is not None or len(m) != len(o):
            bad += 1
            if bad <= 8:
                i = diff if diff is not None else k
                print("differ:", p, "edge", i, "beep.c", m[max(0, i - 2):i + 3], "IPC", o[max(0, i - 2):i + 3])
    print(len(cases), "cases,", bad, "differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
