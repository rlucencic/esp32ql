/* Test main/web/qlfiles.js: QL zips unpacked as the web page does (each
 * entry's extra field with it), packed onto a cartridge and a floppy disk,
 * then both images read back here (the cartridge's checksums, map and
 * directory; the disk's map through its sector tables) and each file
 * compared with the zip's.
 *   node tools/qlfiles_test.js [out folder] file.zip ...
 * Without zips: a made-up set of files (a data file, an "executable" with
 * a QDOS header in its extra field, one too big for a cartridge). With an
 * out folder (an argument not ending in .zip) the images are written there
 * (to try in the emulator: QL_MDV1, QL_FLP1).
 */
"use strict";
const fs = require("fs"), path = require("path"), zlib = require("zlib");
const ql = require("../main/web/qlfiles.js");

let failures = 0;
const check = (ok, what) => { if (!ok) { failures++; console.log("  FAIL " + what); } };

/* A zip's entries as the page's unzip() gives them to the packer */
function unzip(buf) {
  const v = new DataView(buf.buffer, buf.byteOffset, buf.byteLength);
  let eocd = -1;
  for (let i = buf.length - 22; i >= 0; i--) if (v.getUint32(i, true) === 0x06054b50) { eocd = i; break; }
  if (eocd < 0) throw new Error("not a zip");
  const count = v.getUint16(eocd + 10, true);
  let p = v.getUint32(eocd + 16, true);
  const out = [];
  for (let n = 0; n < count; n++) {
    const method = v.getUint16(p + 10, true), csize = v.getUint32(p + 20, true);
    const nlen = v.getUint16(p + 28, true), xlen = v.getUint16(p + 30, true), clen = v.getUint16(p + 32, true);
    const local = v.getUint32(p + 42, true);
    const full = buf.subarray(p + 46, p + 46 + nlen).toString();
    const extra = new Uint8Array(buf.subarray(p + 46 + nlen, p + 46 + nlen + xlen));
    p += 46 + nlen + xlen + clen;
    if (full.endsWith("/")) continue;
    const start = local + 30 + v.getUint16(local + 26, true) + v.getUint16(local + 28, true);
    const raw = buf.subarray(start, start + csize);
    const data = new Uint8Array(method === 0 ? raw : zlib.inflateRawSync(raw));
    out.push({ path: full, data, extra, date: new Date() });
  }
  return out;
}

/* ---- Reading the images back ---- */

const u16le = (b, p) => b[p] | (b[p + 1] << 8);
const be32 = (b, p) => ((b[p] << 24) | (b[p + 1] << 16) | (b[p + 2] << 8) | b[p + 3]) >>> 0;
const be16 = (b, p) => (b[p] << 8) | b[p + 1];
const sum = (b, from, to) => { let s = 0x0f0f; for (let i = from; i < to; i++) s += b[i]; return s & 0xffff; };

/* {name: Uint8Array (header + data)} from a cartridge */
function readMdv(img) {
  check(img.length === 174930, "cartridge size " + img.length);
  const slotOf = {}, names = new Set(), randoms = new Set();
  for (let s = 0; s < 255; s++) {
    const o = s * 686;
    check(img[o + 10] === 0xff && img[o + 11] === 0xff && img[o + 12] === 0xff, "slot " + s + " header sync");
    check(u16le(img, o + 26) === sum(img, o + 12, o + 26), "slot " + s + " header checksum");
    check(u16le(img, o + 42) === sum(img, o + 40, o + 42), "slot " + s + " block header checksum");
    check(u16le(img, o + 564) === sum(img, o + 52, o + 564), "slot " + s + " data checksum");
    slotOf[img[o + 13]] = s;
    names.add(String.fromCharCode(...img.subarray(o + 14, o + 24)));
    randoms.add(u16le(img, o + 24));
  }
  check(Object.keys(slotOf).length === 255, "255 sector numbers");
  check(names.size === 1 && randoms.size === 1, "one medium name and random number");
  const block = sec => img.subarray(slotOf[sec] * 686 + 52, slotOf[sec] * 686 + 564);
  const map = block(0);
  check(map[0] === 0xf8 && img[slotOf[0] * 686 + 40] === 0xf8, "the map in sector 0");
  const fileOf = f => {
    const blocks = [];
    for (let s = 0; s < 255; s++) if (map[2 * s] === f) blocks[map[2 * s + 1]] = block(s);
    return Buffer.concat(blocks.map(b => Buffer.from(b)));
  };
  const dir = fileOf(0), dirLen = be32(dir, 0);
  const out = {};
  for (let n = 1; n < dirLen / 64; n++) {
    const e = dir.subarray(n * 64, n * 64 + 64), len = be32(e, 0);
    const name = e.subarray(16, 16 + be16(e, 14)).toString("latin1");
    const f = fileOf(n).subarray(0, len);
    check(Buffer.compare(f.subarray(0, 14), e.subarray(0, 14)) === 0, name + ": its header as the directory's");
    out[name] = f;
  }
  /* blocks of a file 13 slots or more apart along the tape */
  for (let n = 1; n < dirLen / 64; n++) {
    const slots = [];
    for (let s = 0; s < 255; s++) if (map[2 * s] === n) slots[map[2 * s + 1]] = slotOf[s];
    for (let b = 1; b < slots.length; b++) {
      check((slots[b] - slots[b - 1] + 255) % 255 >= 13, "file " + n + " block " + b + " spacing");
    }
  }
  return { files: out, free: [...map.subarray(0, 510)].filter((x, i) => i % 2 === 0 && x === 0xfd).length };
}

/* {name: Uint8Array (header + data)} from a disk, through its own tables */
function readFlp(img) {
  check(img.length === 737280, "disk size " + img.length);
  check(String.fromCharCode(...img.subarray(0, 4)) === "QL5A", "QL5A");
  const l2p = img.subarray(0x28, 0x3a), skew = be16(img, 0x26);
  const off = l => {
    const c = Math.floor(l / 18), p = l2p[l % 18];
    return ((c * 2 + (p >> 7)) * 9 + ((p & 0x7f) + c * skew) % 9) * 512;
  };
  const sector = l => img.subarray(off(l), off(l) + 512);
  const b0 = Buffer.concat([0, 1, 2].map(l => Buffer.from(sector(l))));
  check(be16(b0, 0x16) === 1440 && be16(b0, 0x1a) === 9 && be16(b0, 0x1c) === 18 && be16(b0, 0x1e) === 80 &&
        be16(b0, 0x20) === 3, "geometry");
  const owner = [];
  for (let b = 0; b < 480; b++) {
    const v = (b0[0x60 + 3 * b] << 16) | (b0[0x61 + 3 * b] << 8) | b0[0x62 + 3 * b];
    owner.push([v >> 12, v & 0xfff]);
  }
  check(owner[0][0] === 0xf80, "the map in block 0");
  const used = owner.filter(([f]) => f !== 0xfdf).length;
  check(be16(b0, 0x14) === 1440 - used * 3, "free sectors " + be16(b0, 0x14));
  const fileOf = f => {
    const blocks = [];
    owner.forEach(([o, bl], b) => { if (o === f) blocks[bl] = b; });
    return Buffer.concat(blocks.flatMap(b => [0, 1, 2].map(k => Buffer.from(sector(b * 3 + k)))));
  };
  const dirLen = be16(b0, 0x22) * 512 + be16(b0, 0x24);
  const dir = fileOf(0), out = {};
  for (let n = 1; n < dirLen / 64; n++) {
    const e = dir.subarray(n * 64, n * 64 + 64), len = be32(e, 0);
    const name = e.subarray(16, 16 + be16(e, 14)).toString("latin1");
    out[name] = fileOf(n).subarray(0, len);
    check(Buffer.compare(out[name].subarray(0, 14), e.subarray(0, 14)) === 0, name + ": its header as the directory's");
  }
  return { files: out, free: be16(b0, 0x14) };
}

/* ---- Run ---- */

async function run(label, entries, outDir) {
  const logs = [];
  const images = await ql.pack(label, entries, { log: t => logs.push(t) });
  for (const t of logs) console.log("  " + t);
  const expect = {};
  for (const e of entries) {
    const q = ql.qlFile(e);
    if (q) expect[q.name] = q;
  }
  for (const im of images) {
    const r = im.name.endsWith(".mdv") ? readMdv(im.data) : readFlp(im.data);
    const names = Object.keys(r.files).sort(), want = Object.keys(expect).sort();
    check(JSON.stringify(names) === JSON.stringify(want), im.name + ": files " + names.join(","));
    for (const n of want) {
      const got = r.files[n];
      if (!got) continue;
      check(Buffer.compare(Buffer.from(got.subarray(64)), Buffer.from(expect[n].data)) === 0, im.name + ": " + n + " data");
      check(got[5] === expect[n].header[5] && Buffer.compare(Buffer.from(got.subarray(6, 14)),
            Buffer.from(expect[n].header.subarray(6, 14))) === 0, im.name + ": " + n + " type and data space");
    }
    console.log("  " + im.name + ": " + names.length + " files read back, " + r.free + " free");
    if (outDir) fs.writeFileSync(path.join(outDir, im.name), im.data);
  }
}

(async () => {
  const args = process.argv.slice(2);
  const outDir = args.find(a => !a.toLowerCase().endsWith(".zip"));
  if (outDir) fs.mkdirSync(outDir, { recursive: true });
  const zips = args.filter(a => a.toLowerCase().endsWith(".zip"));
  if (!zips.length) {
    /* made up: a BASIC file, an executable whose header is in the extra field, a big one */
    const hdr = new Uint8Array(64);
    hdr[5] = 1;                                         /* executable */
    hdr.set([0, 0, 0x10, 0], 6);                        /* data space 4096 */
    hdr[15] = 4; hdr.set(Buffer.from("prog"), 16);
    const extra = new Uint8Array(4 + 8 + 64);
    extra.set([0x4a, 0xfb, 72, 0]);
    extra.set(Buffer.from("QZHD0001"), 4);
    extra.set(hdr, 12);
    const exe = new Uint8Array(3000).map((_, i) => (i * 7) & 255);
    console.log("made-up files:");
    await run("TEST", [
      { path: "boot", data: Buffer.from("10 PRINT \"hello\"\n"), date: new Date() },
      { path: "prog.bin", data: exe, extra, date: new Date() },
      { path: "docs/read.me", data: Buffer.from("text"), date: new Date() },
    ], outDir);
    console.log("too big for a cartridge:");
    await run("BIG", [{ path: "big_dat", data: new Uint8Array(200 * 1024).fill(1), date: new Date() }], outDir);
  }
  for (const z of zips) {
    console.log(path.basename(z) + ":");
    await run(path.basename(z).replace(/\.zip$/i, ""), unzip(fs.readFileSync(z)), outDir);
  }
  console.log(failures ? failures + " failures" : "all right");
  process.exit(failures ? 1 : 0);
})();
