// SPDX-License-Identifier: MPL-2.0
// FM-FAM's mirror page, its Projects and backup part (fam/mirror/extra.js), as tools/make_site.py fills it in:
//  1. the project files and the parts of a backup: slots, names, a project into a file and back, every damaged file refused,
//     a project put into another slot (only that slot's four parts change, the rest byte for byte), the ids tied to
//     fam/app.h's enum (OBJ_PROJ0) and its backup list;
//  2. the page's backup protocol against a simulated FM-1 (device/backup.c's frames, restore_rx()'s reads and question):
//     a backup, a project put in and restored, STOP FIRST, HOME at the question, a lost frame.
//   node fam/tests/projects_page_test.mjs        (from the repository's root)
import fs from "fs";
import path from "path";
import { execFileSync } from "child_process";
import { fileURLToPath } from "url";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
const html = execFileSync("python3", ["-c",
  "import sys; sys.path.insert(0, 'tools'); from pathlib import Path; import make_site; " +
  "sys.stdout.write(make_site.tool_pages(Path('fam'))['mirror/index.html'])"], { cwd: root, maxBuffer: 1 << 28 }).toString("utf8");
const appH = fs.readFileSync(path.join(root, "fam/app.h"), "utf8");
const PORTS_JS = fs.readFileSync(path.join(root, "web/fm1ports.js"), "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");

let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "ok  " : "FAIL"} ${msg}`); if (!ok) fails++; };

const pick = (a, b, what) => {
  const m = new RegExp(`// --- ${a}: begin[^\\n]*\\n([\\s\\S]*?)// --- ${b}: end`).exec(html);
  if (!m) { console.log(`FAIL no ${what} markers in the page`); process.exit(1); }
  return m[1];
};
const left = html.match(/\{\{[A-Za-z_]+\}\}/g);
check(!left, "the page has nothing left unfilled" + (left ? ": " + left.join(" ") : ""));
check(html.includes('id="projects"') && html.includes("mirrorTool.onports"), "the Projects and backup section is in the page");
const { PB, PJ } = new Function(`"use strict";\n${PORTS_JS}\n${pick("backup protocol", "backup protocol", "backup protocol")}\n` +
                                `${pick("projects", "projects", "projects")}\nreturn { PB, PJ };`)();
Object.assign(PB.DELAY, { poll: 5, idle: 300, ask: 500, first: 300, hs: 100, hsFull: 100 });

// ---------------------------------------------------------------- the firmware's own numbers, from fam/app.h
const enumBody = /enum\s*\{([^}]*OBJ_SOUND[^}]*)\}/.exec(appH)[1];
const ids = Object.fromEntries(enumBody.split(",").map((x) => x.trim()).filter(Boolean).map((n, i) => [n, i]));
const objsList = /#define APP_BACKUP_OBJS \{([^}]*)\}/.exec(appH)[1].split(",").map((x) => ids[x.trim()]);
const magic = Number(/#define APP_BACKUP_MAGIC (0x[0-9A-Fa-f]+)u/.exec(appH)[1]);
const version = Number(/#define APP_BACKUP_VERSION (\d+)u/.exec(appH)[1]);
const appVersion = /#define APP_VERSION "([^"]*)"/.exec(appH)[1];
check(objsList.length === 33 && !objsList.includes(ids.OBJ_PANEL) && !objsList.includes(ids.OBJ_BT) && objsList.every((v) => v !== undefined),
      "app.h's backup list: 33 parts, never the panel's calibration or the Bluetooth keys");
check(Buffer.from(PB.MAGIC).toString("latin1") === Buffer.from(Uint32Array.of(magic).buffer).toString("latin1") && PB.VERSION === version,
      `the page's magic ${Buffer.from(PB.MAGIC).toString("latin1")} and version ${PB.VERSION} are app.h's`);
check(PJ.APP === appVersion, `the page knows the app's version (${PJ.APP})`);
for (let p = 0; p < 4; p++) for (let j = 0; j < 4; j++)
  if (ids["OBJ_PROJ" + (p * 4 + j)] !== ids.OBJ_PROJ0 + p * 4 + j) check(false, "OBJ_PROJ ids are not in a row");

// ---------------------------------------------------------------- 1. a device's parts
let seed = 12345;
const rnd = () => (seed = (Math.imul(seed, 1103515245) + 12345) >>> 0) >>> 16;
const bytes = (n) => Uint8Array.from({ length: n }, () => rnd() & 255);
const prj = (name, len) => {                                     // prj_t: "3JFM", the name (9), then the sound
  const d = bytes(len);
  d.set([0x33, 0x4A, 0x46, 0x4D], 0);
  d.fill(0, 4, 13);
  d.set(Buffer.from(name, "latin1"), 4);
  return d;
};
function deviceParts() {
  const data = new Map();
  for (const id of objsList) data.set(id, bytes(id >= ids.OBJ_BANK0 && id <= ids.OBJ_BANK10 ? 3000 : 600));
  const P = (p, j) => ids.OBJ_PROJ0 + p * 4 + j;
  data.set(P(0, 0), prj("KICKS", 700)); [1, 2, 3].forEach((j) => data.set(P(0, j), bytes(3000)));
  data.set(P(1, 0), prj("ACID 1", 700)); [1, 2, 3].forEach((j) => data.set(P(1, j), bytes(2900)));
  data.set(P(2, 0), Uint8Array.from([0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0]));   // prj_clear: 16 bytes, magic 0
  [1, 2, 3].forEach((j) => data.set(P(2, j), new Uint8Array(0)));
  [0, 1, 2, 3].forEach((j) => data.set(P(3, j), new Uint8Array(0)));                       // never written
  return objsList.map((id) => ({ obj: id, data: data.get(id) }));
}
const objs = deviceParts();
const image = PJ.buildImage(objs, 0);
const parsed = PB.parse(image);
check(parsed.objs.length === 33 && parsed.flags === 0, `a backup image of the device's parts parses (${image.length} bytes)`);

const names = [0, 1, 2, 3].map((p) => PJ.slot(parsed.objs, p));
check(names[0].used && names[0].name === "KICKS" && names[1].used && names[1].name === "ACID 1", "slots 1 and 2: used, named");
check(!names[2].used && !names[3].used && names[2].name === "" && names[3].bytes === 0, "slot 3 (cleared) and slot 4 (never written): empty");

const file = PJ.projectFile(names[0]);
const back = PJ.readProjectFile(file);
check(back.name === "KICKS" && back.appVersion === appVersion && back.backupVersion === version &&
      back.parts.length === 4 && back.parts.every((d, j) => Buffer.compare(Buffer.from(d), Buffer.from(names[0].parts[j])) === 0),
      `a project into a file (${file.length} bytes) and back: the four parts byte for byte`);
let threw = null;
try { PJ.projectFile(names[2]); } catch (e) { threw = e; }
check(threw && threw.code === "file", "an empty slot can't be made a file");

const refused = (f, why) => { try { PJ.readProjectFile(f); check(false, `${why}: accepted`); } catch (e) { check(e.code === "file", `${why}: refused (${e.message})`); } };
for (const at of [0, 5, 7, 9, 12, 20, 300, file.length - 6, file.length - 1]) {
  const f = file.slice(); f[at] ^= 0x40; refused(f, `a changed byte at ${at}`);
}
refused(file.slice(0, 30), "a cut file");
refused(file.slice(0, file.length - 1), "a file short by a byte");
refused(Uint8Array.from([...file, 0]), "a file longer by a byte");
refused(Buffer.from("not a project at all, just some text of enough length"), "some other file");
refused(PJ.buildImage(objs, 0).slice(0, 60), "a backup given as a project");

const moved = PJ.putProject(parsed, 2, back.parts);
const mp = PB.parse(moved);
const same = (a, b) => Buffer.compare(Buffer.from(a), Buffer.from(b)) === 0;
check(mp.objs.length === 33 && mp.flags === 0 && mp.objs.every((o, i) => o.obj === parsed.objs[i].obj), "a project put into slot 3: the same 33 parts in the same order");
check(PJ.slot(mp.objs, 2).used && PJ.slot(mp.objs, 2).name === "KICKS" && PJ.slot(mp.objs, 2).parts.every((d, j) => same(d, names[0].parts[j])),
      "... slot 3 holds the project");
check(mp.objs.every((o, i) => {
  const inSlot = o.obj >= ids.OBJ_PROJ0 + 8 && o.obj < ids.OBJ_PROJ0 + 12;
  return inSlot || same(o.data, parsed.objs[i].data);
}), "... every other part byte for byte as it was (the sound, the banks, the presets, the other projects)");
check(PJ.slot(mp.objs, 0).name === "KICKS" && PJ.slot(mp.objs, 1).name === "ACID 1" && !PJ.slot(mp.objs, 3).used, "... slots 1, 2 and 4 as they were");
const bad = PB.parse(PJ.putProject(parsed, 3, PJ.readProjectFile(PJ.projectFile(names[1])).parts));
check(PJ.slot(bad.objs, 3).name === "ACID 1", "a project into slot 4, which was never written");

// ---------------------------------------------------------------- 2. a simulated FM-1
const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
const le32 = (v) => [v & 255, v >>> 8 & 255, v >>> 16 & 255, v >>> 24 & 255];
const devFrame = (cmd, body) => {                                // backup.c's bu_frame
  const s = body.reduce((a, v) => a + v, 0);
  return [0xF0, ...PB.pack7([0, 0x59, cmd, body.length & 255, body.length >> 8, 0, ...body, ~s & 255]), 0xF7];
};
class FakeFM1 {
  constructor(img, o = {}) {
    Object.assign(this, { image: img, playing: false, answer: true, dropFrame: null }, o);
    this.q = []; this.lost = false; this.asked = 0; this.wanted = null; this.pull = null;
  }
  send(pkt) { this.rx(pkt); return true; }
  drain() { this.q.length = 0; }
  async read(ms) { if (!this.q.length) await new Promise((r) => setTimeout(r, Math.min(ms, 2))); return this.q.shift() || null; }
  close() {}
  stream() {
    const flags = this.playing ? 1 : 0;
    const img = Uint8Array.from(this.image); img.set(le32(flags), 8); img.set(le32(PB.crc32(img, 0, 12)), 12);
    // (the playing flag is in the head, as backup.c writes it: the tail's CRC covers it)
    const tail = PB.crc32(img, 0, img.length - 4); img.set(le32(tail), img.length - 4);
    let n = 0;
    for (let off = 0; off < img.length; off += 128, n++) {
      if (n === this.dropFrame) continue;
      this.q.push(devFrame(PB.BU_REQ, [...le32(off), ...img.subarray(off, off + 128)]));
    }
    this.q.push(devFrame(PB.BU_END, [...le32(img.length), ...le32(PB.crc32(img)), 0, flags]));
  }
  *puller(n, c) {
    if (this.playing) { yield [(PB.FAIL_ADDR | PB.PLAYING) >>> 0, 8]; return; }
    const got = [...(yield [0, 16])];
    let pos = 16;
    for (let i = 0; i < 33; i++) {
      const r = yield [pos, 8];
      got.push(...r);
      const ln = r[2] | r[3] << 8;
      pos += 8;
      for (let k = 0; k < ln; k += 512) got.push(...(yield [pos + k, Math.min(512, ln - k)]));
      pos += ln;
    }
    got.push(...(yield [pos, 4]));
    const g = Uint8Array.from(got);
    if (g.length !== n || PB.crc32(g) !== c) { yield [(PB.FAIL_ADDR | 6) >>> 0, 8]; return; }
    try { PB.parse(g); } catch (_) { yield [(PB.FAIL_ADDR | 5) >>> 0, 8]; return; }
    yield [PB.ASK_ADDR, 8];
    this.asked++;
    if (!this.answer) { yield [(PB.FAIL_ADDR | PB.DECLINED) >>> 0, 8]; return; }
    this.image = g;
    yield [PB.OK_ADDR, 8];
  }
  ask(addr, n) {
    const body = [0, ...le32(addr >>> 0), n & 255, n >> 8, 0];
    this.q.push([0xF0, ...PB.pack7([0, 0x59, 0x30, 8, 0, 0, ...body, ~body.reduce((a, v) => a + v, 0) & 255]), 0xF7]);
    this.wanted = [addr >>> 0, n];
  }
  next(data) { const r = this.pull.next(data); if (r.done) { this.pull = null; return; } this.ask(...r.value); }
  rx(d) {
    const f = PB.parseFrame(d);
    if (f && f.cmd === PB.BU_REQ && Buffer.from(f.body).toString("latin1") === Buffer.from(PB.MAGIC).toString("latin1")) { this.stream(); return; }
    if (f && f.cmd === PB.RS_REQ && f.body.length === 8) { this.pull = this.puller(u32(f.body, 0), u32(f.body, 4)); this.next(undefined); return; }
    const u = PB.unpack7(d.slice(1, -1));
    if (this.pull && this.wanted && u.length >= 15 && u[2] === 0x30) {
      const [addr, n] = this.wanted;
      if (u32(u, 7) !== addr || (u[11] | u[12] << 8) !== n) return;
      this.next(Uint8Array.from(u.slice(14, 14 + n)));
    }
  }
}
const err = async (p) => { try { await p; return null; } catch (e) { return e; } };

{
  const dev = new FakeFM1(image);
  const r = await PB.backup(dev);
  check(same(r.image, image) && r.flags === 0, "simulated FM-1: a backup of all 33 parts, byte for byte");
  const pr = PB.parse(r.image);
  check([0, 1, 2, 3].map((p) => PJ.slot(pr.objs, p).used).join() === "true,true,false,false", "... the page reads which projects are in use");
}
{
  const dev = new FakeFM1(image, { playing: true });
  const r = await PB.backup(dev);
  check(r.flags === 1, "simulated FM-1 playing: the backup says so (flags bit 0), as the page checks before it puts a project in");
}
{
  const dev = new FakeFM1(image);
  const r = await PB.backup(dev);
  const merged = PJ.putProject(PB.parse(r.image), 2, back.parts);
  let asked = 0;
  const code = await PB.restore(dev, merged, (w) => { if (w === "ask") asked++; });
  check(code === 0 && asked === 1 && same(dev.image, merged), "a project put into slot 3: the FM-1 asks once and holds the merged backup byte for byte");
  const again = PB.parse((await PB.backup(dev)).image);
  check(PJ.slot(again.objs, 2).name === "KICKS" && PJ.slot(again.objs, 0).name === "KICKS" && PJ.slot(again.objs, 1).name === "ACID 1",
        "... and a backup after it shows the project in slots 1 and 3, slot 2 untouched");
}
{
  const dev = new FakeFM1(image, { playing: true });
  const code = await PB.restore(dev, PJ.putProject(parsed, 2, back.parts));
  check(code === PB.PLAYING && same(dev.image, image), "playing: STOP FIRST, nothing changed");
}
{
  const dev = new FakeFM1(image, { answer: false });
  const code = await PB.restore(dev, PJ.putProject(parsed, 2, back.parts));
  check(code === PB.DECLINED && same(dev.image, image) && dev.asked === 1, "HOME on the FM-1: not put in, nothing changed");
}
{
  const dev = new FakeFM1(image, { dropFrame: 7 });
  const e = await err(PB.backup(dev));
  check(e && e.code === "lost", "a lost frame in the backup: said, not a damaged project");
}
{
  const e = await err(PB.restore(new FakeFM1(image), PJ.putProject(parsed, 2, back.parts).slice(0, 500)));
  check(e && e.code === "file", "a cut image is never sent");
}

console.log(fails ? `\n${fails} FAILED` : "\nall ok");
process.exit(fails ? 1 : 0);
