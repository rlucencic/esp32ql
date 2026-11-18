/* QL files on the web page: files dropped on the page (loose, in folders or
 * in ZIP files) are put on a new Microdrive cartridge (.mdv) and a new
 * 720 KB floppy disk (.img) in the browser, which are uploaded like any
 * other image. The QL's zips keep each file's QDOS header (its type and an
 * executable's data space) in the entry's extra field (ID $FB4A, the
 * header its last 64 bytes): taken from there, else a data file's header.
 *
 * The board serves this file as /machine.js; the page loads it when
 * /api/info says "script": true, and it registers itself with
 * registerPacker(). It also runs under node (module.exports) for tests:
 * tools/qlfiles_test.js.
 *
 * The cartridge (docs/ql-hardware.md 6.4-6.6): a QLay image as QDOS's
 * FORMAT leaves a good one, 255 slots of 686 bytes, sector 0 in the first
 * and 254 down to 1 after it (the order the tape passes them); each slot a
 * header record ($FF, the sector, the medium's name, its random number) and
 * a block record (file, block, 512 bytes; a free one FORMAT's $FD and $AA55
 * long block). Sector 0 holds the map (file, block for each sector), file 0
 * the directory (64-byte entries, the first its length, then each file's
 * header). A file is its header and its bytes in 512-byte blocks, each the
 * first free slot 13 or more along the tape after the last, as QDOS spaces
 * them (tools/mdvtool.py does the same).
 *
 * The disk (CLAUDE.md, the Trump Card): QDOS's "QL5A" layout as the Trump
 * Card's driver formats it: 80 cylinders of 2 x 9 sectors, blocks of 3
 * sectors. Logical sector l is in cylinder l / 18; a table gives its side
 * and sector in the cylinder (interleave 3, the sides alternating by
 * block), turned by 5 sectors a cylinder. Block 0 is the map: a header
 * (name, random number, counts, the geometry, the directory's length, the
 * tables), then a 12-bit file and block number for each of the 480 blocks.
 * The directory's first entry is unused (its length is in the header).
 */
(function () {
"use strict";

class QlFileError extends Error {}
const fail = msg => { throw new QlFileError(msg); };

const QDOS_EPOCH = 283996800;           /* 1961-01-01 to 1970-01-01, seconds */
const HEADER = 64;

/* ---- QDOS file headers ---- */

/* The 64-byte header in a ZIP entry's SMS/QDOS extra field ($FB4A), or null */
function headerFromExtra(extra) {
  if (!extra) return null;
  for (let i = 0; i + 4 <= extra.length;) {
    const tag = extra[i] | (extra[i + 1] << 8), size = extra[i + 2] | (extra[i + 3] << 8);
    if (tag === 0xfb4a && size >= HEADER && i + 4 + size <= extra.length) {
      return extra.slice(i + 4 + size - HEADER, i + 4 + size);
    }
    i += 4 + size;
  }
  return null;
}

const be32 = (b, p, v) => { b[p] = v >>> 24; b[p + 1] = (v >> 16) & 255; b[p + 2] = (v >> 8) & 255; b[p + 3] = v & 255; };
const be16 = (b, p, v) => { b[p] = v >> 8; b[p + 1] = v & 255; };

/* {path, data, date, extra} -> {name, header, data}: the header's own name
 * if it has one, else the path's with "/" and "." as "_" (QDOS's way);
 * null for a directory's entry
 */
function qlFile(f) {
  const header = new Uint8Array(HEADER);
  const from = headerFromExtra(f.extra);
  if (from) header.set(from);
  if (from && from[5] === 255) return null;           /* a level-2 directory */
  let name = "";
  const nlen = (header[14] << 8) | header[15];
  if (from && nlen > 0 && nlen <= 36) {
    name = String.fromCharCode(...header.subarray(16, 16 + nlen));
  } else {
    name = f.path.replace(/[/\\.]/g, "_");
  }
  name = name.replace(/[^\x20-\x7e]/g, "_");
  if (name.length > 36) name = name.slice(0, 36);
  const data = f.data instanceof Uint8Array ? f.data : new Uint8Array(f.data);
  be32(header, 0, data.length + HEADER);              /* the length counts the header */
  be16(header, 14, name.length);
  header.fill(0, 16, 52);
  for (let i = 0; i < name.length; i++) header[16 + i] = name.charCodeAt(i);
  if (!from && f.date instanceof Date && !isNaN(f.date)) {
    be32(header, 0x34, Math.floor(f.date.getTime() / 1000) + QDOS_EPOCH);   /* update date */
  }
  return { name, header, data, executable: header[5] === 1 };
}

/* The medium's name: 10 characters, spaces after */
function mediumName(name) {
  const s = name.replace(/[^\x21-\x7e]/g, "").slice(0, 10) || "QL";
  return Uint8Array.from((s + "          ").slice(0, 10), c => c.charCodeAt(0));
}

const random16 = () => [Math.floor(Math.random() * 256), Math.floor(Math.random() * 256)];

/* ---- Microdrive cartridges (QLay .mdv) ---- */

const SLOTS = 255, SLOT = 686, SPACING = 13;
const MDV_BYTES = SLOTS * SLOT;
const FREE = 0xfd, MAP = 0xf8;

const sum = (b, from, to) => { let s = 0x0f0f; for (let i = from; i < to; i++) s += b[i]; return s & 0xffff; };
const ck = (b, at, v) => { b[at] = v & 255; b[at + 1] = v >> 8; };

/* A cartridge as FORMAT leaves it: every sector free; its sectors' slots */
function formatMdv(medium) {
  const img = new Uint8Array(MDV_BYTES);
  const name = mediumName(medium), rnd = random16();
  const slotOf = new Array(SLOTS);
  for (let slot = 0; slot < SLOTS; slot++) {
    const sector = slot === 0 ? 0 : SLOTS - slot;
    slotOf[sector] = slot;
    const o = slot * SLOT;
    /* the header: preamble, $FF, sector, name, random number, checksum */
    img[o + 10] = img[o + 11] = 0xff;
    img[o + 12] = 0xff;
    img[o + 13] = sector;
    img.set(name, o + 14);
    img[o + 24] = rnd[0];
    img[o + 25] = rnd[1];
    ck(img, o + 26, sum(img, o + 12, o + 26));
    /* FORMAT's long block: preamble, $FD 00 and its checksum, the data's
     * preamble, $AA55 to the 610 bytes' end, their checksum, filler; the
     * 512 data bytes' checksum where a block's goes
     */
    img[o + 38] = img[o + 39] = 0xff;
    img[o + 40] = FREE;
    ck(img, o + 42, sum(img, o + 40, o + 42));
    img[o + 50] = img[o + 51] = 0xff;
    for (let i = o + 52; i < o + 650; i += 2) { img[i] = 0xaa; img[i + 1] = 0x55; }
    ck(img, o + 564, sum(img, o + 52, o + 564));
    ck(img, o + 650, sum(img, o + 40, o + 650));
    img.fill(0x5a, o + 652, o + SLOT);
  }
  return { img, slotOf };
}

/* Files onto a new cartridge; null if they don't fit */
function makeMdv(medium, files) {
  const { img, slotOf } = formatMdv(medium);
  const map = new Uint8Array(512);
  for (let s = 0; s < SLOTS; s++) { map[2 * s] = FREE; map[2 * s + 1] = 0; }
  map[0] = MAP;
  const free = new Set(slotOf.slice(1));               /* sector 0 is the map */
  let last = 0;

  const write = (sector, file, block, data) => {
    const o = slotOf[sector] * SLOT;
    img[o + 40] = file;
    img[o + 41] = block;
    ck(img, o + 42, sum(img, o + 40, o + 42));
    img.fill(0, o + 52, o + 564);
    img.set(data.subarray(0, 512), o + 52);
    ck(img, o + 564, sum(img, o + 52, o + 564));
    map[2 * sector] = file;
    map[2 * sector + 1] = block;
    last = sector;
  };
  /* count free sectors along the tape, each SPACING slots or more after the
   * last, the first in the first free slot
   */
  const allocate = count => {
    if (count > free.size) return null;
    const sectorAt = new Map([...free].map(slot => [slot, SLOTS - slot]));
    const out = [];
    let slot = Math.min(...free);
    for (let n = 0; n < count; n++) {
      while (!free.has(slot)) slot = (slot + 1) % SLOTS;
      free.delete(slot);
      out.push(sectorAt.get(slot));
      slot = (slot + SPACING) % SLOTS;
    }
    return out;
  };

  const dir = new Uint8Array(HEADER * (files.length + 1));
  be32(dir, 0, dir.length);
  for (let n = 0; n < files.length; n++) {
    const f = files[n];
    const content = new Uint8Array(HEADER + f.data.length);
    content.set(f.header);
    content.set(f.data, HEADER);
    const blocks = Math.ceil(content.length / 512);
    if (blocks > 255) return null;
    const sectors = allocate(blocks);
    if (!sectors) return null;
    for (let b = 0; b < blocks; b++) write(sectors[b], n + 1, b, content.subarray(b * 512, (b + 1) * 512));
    dir.set(f.header, HEADER * (n + 1));
  }
  const dirSectors = allocate(Math.ceil(dir.length / 512));
  if (!dirSectors) return null;
  dirSectors.forEach((s, b) => write(s, 0, b, dir.subarray(b * 512, (b + 1) * 512)));
  map[511] = last;                                     /* the last sector given out */
  write(0, MAP, 0, map);
  return { img, used: SLOTS - 1 - free.size, total: SLOTS - 1 };
}

/* ---- Floppy disks: QDOS's 720 KB format (QL5A) ---- */

const FLP_BYTES = 80 * 2 * 9 * 512;
const SECTORS = 1440, BLOCKS = 480, BLOCK = 1536;
const L2P = [0x00, 0x03, 0x06, 0x80, 0x83, 0x86, 0x01, 0x04, 0x07, 0x81, 0x84, 0x87, 0x02, 0x05, 0x08, 0x82, 0x85, 0x88];
const P2L = [0x00, 0x06, 0x0c, 0x01, 0x07, 0x0d, 0x02, 0x08, 0x0e, 0x03, 0x09, 0x0f, 0x04, 0x0a, 0x10, 0x05, 0x0b, 0x11];
const SKEW = 5;

/* A logical sector's place in the image (track by track, side 0 then 1) */
function flpOffset(l) {
  const c = Math.floor(l / 18), p = L2P[l % 18];
  return ((c * 2 + (p >> 7)) * 9 + ((p & 0x7f) + c * SKEW) % 9) * 512;
}

/* Files onto a new disk; null if they don't fit */
function makeFlp(medium, files) {
  const img = new Uint8Array(FLP_BYTES);
  const map = new Uint32Array(BLOCKS).fill(0xfdffff);  /* free */
  map[0] = 0xf80000;                                    /* the map itself */
  let next = 1;
  const write = (file, block, data) => {
    if (next >= BLOCKS) return false;
    const b = next++;
    map[b] = (file << 12) | block;
    const buf = new Uint8Array(BLOCK);
    buf.set(data.subarray(0, BLOCK));
    for (let k = 0; k < 3; k++) img.set(buf.subarray(k * 512, (k + 1) * 512), flpOffset(b * 3 + k));
    return true;
  };

  /* the directory: its first entry unused, then each file's header */
  const dir = new Uint8Array(HEADER * (files.length + 1));
  files.forEach((f, n) => dir.set(f.header, HEADER * (n + 1)));
  for (let b = 0; b * BLOCK < dir.length; b++) {
    if (!write(0, b, dir.subarray(b * BLOCK, (b + 1) * BLOCK))) return null;
  }
  for (let n = 0; n < files.length; n++) {
    const f = files[n];
    const content = new Uint8Array(HEADER + f.data.length);
    content.set(f.header);
    content.set(f.data, HEADER);
    for (let b = 0; b * BLOCK < content.length; b++) {
      if (!write(n + 1, b, content.subarray(b * BLOCK, (b + 1) * BLOCK))) return null;
    }
  }

  /* block 0: the header and the map */
  const m = new Uint8Array(BLOCK);
  m.set([0x51, 0x4c, 0x35, 0x41]);                      /* "QL5A" */
  m.set(mediumName(medium), 4);
  m.set(random16(), 14);
  be32(m, 0x10, 0);                                     /* updates */
  be16(m, 0x14, SECTORS - next * 3);                    /* free sectors */
  be16(m, 0x16, SECTORS);                               /* good */
  be16(m, 0x18, SECTORS);                               /* total */
  be16(m, 0x1a, 9);                                     /* a track */
  be16(m, 0x1c, 18);                                    /* a cylinder */
  be16(m, 0x1e, 80);                                    /* cylinders */
  be16(m, 0x20, 3);                                     /* a block */
  be16(m, 0x22, dir.length >> 9);                       /* the directory's end: sector, byte */
  be16(m, 0x24, dir.length & 511);
  be16(m, 0x26, SKEW);
  m.set(L2P, 0x28);
  m.set(P2L, 0x3a);
  m.fill(0xff, 0x4c, 0x60);
  for (let b = 0; b < BLOCKS; b++) {
    m[0x60 + 3 * b] = map[b] >> 16;
    m[0x61 + 3 * b] = (map[b] >> 8) & 255;
    m[0x62 + 3 * b] = map[b] & 255;
  }
  for (let k = 0; k < 3; k++) img.set(m.subarray(k * 512, (k + 1) * 512), flpOffset(k));
  return { img, used: next * 3, total: SECTORS };
}

/* ---- The page's packer ---- */

/* name (the ZIP's or folder's), [{path, data, date, extra}] -> images */
async function pack(name, dropped, ctx = {}) {
  const log = ctx.log || (() => {});
  const byName = new Map();
  for (const f of dropped) {
    const q = qlFile(f);
    if (q) byName.set(q.name.toLowerCase(), q);        /* the last of the same name */
  }
  const files = [...byName.values()];
  if (!files.length) fail("no files to put on a disk");
  if (files.length > 239) fail(files.length + " files: a QL disk holds 239 at most");
  const base = name.replace(/[^\w.-]+/g, "_").replace(/^\.+/, "") || "QLFILES";
  const out = [];
  const what = files.length === 1 ? files[0].name : files.length + " QL files";
  const execs = files.filter(f => f.executable).length;
  const note = execs ? " (" + execs + " executable" + (execs > 1 ? "s" : "") + " with their QDOS headers)" : "";
  const mdv = makeMdv(name, files);
  if (mdv) {
    out.push({ name: base + ".mdv", data: mdv.img });
    log(what + note + " on " + base + ".mdv, a Microdrive cartridge (" + mdv.used + " of " + mdv.total + " sectors)");
  }
  const flp = makeFlp(name, files);
  if (!flp) fail(what + " don't fit on a 720 KB disk");
  out.push({ name: base + ".img", data: flp.img });
  log((mdv ? "and" : what + note + " too big for a cartridge: on") + " " + base +
      ".img, a 720 KB floppy disk for the Trump Card (" + flp.used + " of " + flp.total + " sectors)");
  return out;
}

const packer = {
  description: "QL files and folders",
  pack,
};

if (typeof window !== "undefined" && window.registerPacker) window.registerPacker(packer);
if (typeof module !== "undefined") {
  module.exports = { ...packer, headerFromExtra, qlFile, makeMdv, makeFlp, flpOffset, QlFileError };
}
})();
