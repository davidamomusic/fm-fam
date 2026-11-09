// ---------------------------------------------------------------- Projects and backup (fam/mirror/extra.js, fam/mirror/extra.html;
// fam/tests/projects_page_test.mjs). The FM-1's backup and restore (device/backup.c) over the ports this page has, the mirror
// stopped meanwhile: a backup of everything {{NAME}} saves, and a project taken out of it or put into it. Below it, Install a
// version: a release of this firmware, or a package file, written to the FM-1 (fam/tests/install_page_test.mjs).
{{BACKUP_PROTOCOL}}
{{OTA_JS}}
// the ports are one tool's at a time: a backup, a project, an update (each tool's own update() is hooked in)
const TOOLS = { on: false, hooks: [], set(v) { this.on = v; this.hooks.forEach((f) => f()); } };
// the beta notice (extra.html, #beta) goes to the top of the page, under the title; it also ends every question that writes to the FM-1
const BETA_NOTE = "{{NAME}} is beta software and can fail: you do this at your own risk.";
{
  const beta = document.getElementById("beta"), head = document.querySelector("main > header");
  if (beta && head) head.after(beta);
}

// --- projects: begin (fam/tests/projects_page_test.mjs runs this part in Node, after the backup protocol above)
// A project is four of the backup's parts: its sound and name (app.c prj_t), then the patterns of banks A, B and C. Slot p
// (0..3) holds parts {{OBJ_PROJ0}} + 4 * p + 0..3 (app.h). A project file: "FAMP", u16 file version, u16 backup version,
// u16 count (4), u16 length of the app's version, that text, then the four parts (u16 index, u16 length, u32 CRC-32, the
// bytes) and the CRC-32 of everything before.
const PJ = (() => {
  const PROJ0 = {{OBJ_PROJ0}}, SLOTS = 4, PER = 4, MAXLEN = 3840, PN = 9, FILE_VERSION = 1, APP = "{{APP_VERSION}}";
  const PRJ_MAGIC = [0x33, 0x4A, 0x46, 0x4D];                    // app.c PRJ_MAGIC 0x4D464A33, little-endian
  const FILE_MAGIC = [0x46, 0x41, 0x4D, 0x50];                   // "FAMP"
  const u16 = (b, o) => b[o] | b[o + 1] << 8;
  const u32 = (b, o) => (b[o] | b[o + 1] << 8 | b[o + 2] << 16 | b[o + 3] << 24) >>> 0;
  const le16 = (v) => [v & 0xFF, (v >>> 8) & 0xFF];
  const le32 = (v) => [v & 0xFF, (v >>> 8) & 0xFF, (v >>> 16) & 0xFF, (v >>> 24) & 0xFF];
  const fail = (m) => Object.assign(new Error(m), { code: "file" });
  const id = (p, j) => PROJ0 + p * PER + j;
  const concat = (parts) => {
    const out = new Uint8Array(parts.reduce((a, p) => a + p.length, 0));
    let o = 0;
    for (const p of parts) { out.set(p, o); o += p.length; }
    return out;
  };

  // slot p of a backup's parts ([{obj, data}]) -> {used, name, parts: 4 x Uint8Array, bytes}
  function slot(objs, p) {
    const parts = Array.from({ length: PER }, (_, j) => (objs.find((o) => o.obj === id(p, j)) || { data: new Uint8Array(0) }).data);
    const head = parts[0];
    const used = head.length >= 16 && PRJ_MAGIC.every((b, i) => head[i] === b);
    let name = "";
    if (used) for (let i = 4; i < 4 + PN && head[i]; i++) name += head[i] >= 32 && head[i] < 127 ? String.fromCharCode(head[i]) : "?";
    return { used, name, parts, bytes: parts.reduce((a, d) => a + d.length, 0) };
  }
  // a project (from slot) as a file
  function projectFile(s) {
    if (!s.used) throw fail("There is no project in that slot.");
    const app = [...APP].map((c) => c.charCodeAt(0));
    const out = [...FILE_MAGIC, ...le16(FILE_VERSION), ...le16(PB.VERSION), ...le16(PER), ...le16(app.length), ...app];
    const bytes = [Uint8Array.from(out)];
    s.parts.forEach((d, j) => bytes.push(Uint8Array.from([...le16(j), ...le16(d.length), ...le32(PB.crc32(d))]), d));
    const body = concat(bytes);
    return concat([body, Uint8Array.from(le32(PB.crc32(body)))]);
  }
  // a project file checked whole -> {appVersion, backupVersion, name, parts}; throws (code "file") with what is wrong in words
  function readProjectFile(f) {
    if (f.length < 16 || FILE_MAGIC.some((b, i) => f[i] !== b)) throw fail(`This file is not a project saved from ${PB.NAME}.`);
    const fileVer = u16(f, 4), backupVer = u16(f, 6), n = u16(f, 8), al = u16(f, 10);
    if (fileVer !== FILE_VERSION) throw fail(`This project file is from another version of this page (file version ${fileVer}).`);
    if (backupVer !== PB.VERSION) throw fail(`This project was saved by another version of ${PB.NAME}'s backup (${backupVer}). It can't be put in.`);
    if (n !== PER || 12 + al > f.length) throw fail("This project file is damaged.");
    const appVersion = String.fromCharCode(...f.subarray(12, 12 + al));
    let pos = 12 + al;
    const parts = [];
    for (let j = 0; j < PER; j++) {
      if (pos + 8 > f.length) throw fail("This project file is cut short.");
      const idx = u16(f, pos), ln = u16(f, pos + 2), crc = u32(f, pos + 4);
      pos += 8;
      if (idx !== j || ln > MAXLEN || pos + ln + 4 > f.length) throw fail(`This project file is damaged (part ${j + 1}).`);
      const d = f.slice(pos, pos + ln);
      if (PB.crc32(d) !== crc) throw fail(`This project file is damaged (part ${j + 1}: a byte changed).`);
      parts.push(d);
      pos += ln;
    }
    if (pos + 4 !== f.length || u32(f, pos) !== PB.crc32(f, 0, pos)) throw fail("This project file is damaged (the whole file's check).");
    const s = slot(parts.map((data, j) => ({ obj: id(0, j), data })), 0);
    if (!s.used) throw fail("This file holds no project.");
    return { appVersion, backupVersion: backupVer, name: s.name, parts };
  }
  // a backup image from parts ([{obj, data}]): the head, each part with its CRC-32, the CRC-32 of all of it
  function buildImage(objs, flags = 0) {
    const head = Uint8Array.from([...PB.MAGIC, ...le16(PB.VERSION), ...le16(objs.length), ...le32(flags)]);
    const out = [head, Uint8Array.from(le32(PB.crc32(head)))];
    for (const { obj, data } of objs) out.push(Uint8Array.from([...le16(obj), ...le16(data.length), ...le32(PB.crc32(data))]), data);
    const body = concat(out);
    return concat([body, Uint8Array.from(le32(PB.crc32(body)))]);
  }
  // a backup (PB.parse's result) with project p's four parts replaced -> the backup image to restore
  function putProject(parsed, p, parts) {
    const objs = parsed.objs.map((o) => ({ obj: o.obj, data: o.data }));
    for (let j = 0; j < PER; j++) {
      const at = objs.findIndex((o) => o.obj === id(p, j));
      if (at < 0) throw fail(`This FM-1's backup has no room for project ${p + 1}.`);
      objs[at] = { obj: id(p, j), data: parts[j] };
    }
    return buildImage(objs, 0);
  }
  return { SLOTS, APP, slot, projectFile, readProjectFile, buildImage, putProject };
})();
// --- projects: end

{
// the page's side: the ports (mirrorTool) taken for a while, the buttons, the files
const $ = (i) => document.getElementById(i);
const readBtn = $("pjRead"), allBtn = $("pjAll"), allLoadBtn = $("pjAllLoad"), statusEl = $("pjStatus"), bar = $("pjBar"), list = $("pjList");
const askBox = $("pjAsk"), askText = $("pjAskText"), askYes = $("pjAskYes"), askNo = $("pjAskNo");
const fileProject = $("pjFileProject"), fileAll = $("pjFileAll");
let busy = false, last = null, wantSlot = 0, lastSaid = 0;

function pjSay(text, kind = "") {
  statusEl.textContent = text;
  statusEl.className = "msg" + (kind ? " " + kind : "");
  lastSaid = performance.now();
}
function pjProgress(v) {                                         // false: none, null: busy without a figure, 0..100
  bar.hidden = v === false;
  if (typeof v === "number") bar.value = v; else bar.removeAttribute("value");
}
const stamp = () => {                                            // the computer's own date and time: 2026-11-08-1116
  const d = new Date(), z = (n) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${z(d.getMonth() + 1)}-${z(d.getDate())}-${z(d.getHours())}${z(d.getMinutes())}`;
};
const safe = (t) => String(t).replace(/[^A-Za-z0-9._-]+/g, "-").replace(/^-+|-+$/g, "") || "project";
function download(bytes, name) {
  const a = document.createElement("a");
  a.href = URL.createObjectURL(new Blob([bytes], { type: "application/octet-stream" }));
  a.download = name;
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
}
const pause = (ms) => new Promise((r) => setTimeout(r, ms));

// one MIDI pair with the SysEx messages put together from whatever pieces the browser delivers
function makeLink(input, output) {
  const q = []; let waiter = null, cur = [], inSx = false;
  return {
    sink(data) {
      for (const v of data) {
        if (v === 0xF0) { inSx = true; cur = [v]; }
        else if (!inSx || v >= 0xF8) continue;
        else if (v === 0xF7) { inSx = false; cur.push(v); q.push(cur); if (waiter) { const w = waiter; waiter = null; w(); } }
        else if (v & 0x80) inSx = false;
        else cur.push(v);
      }
    },
    send(bytes) { try { output.send(bytes); return true; } catch (_) { return false; } },
    drain() { q.length = 0; },
    async read(ms) {
      if (!q.length) {
        let tm;
        await Promise.race([new Promise((r) => { waiter = r; }), new Promise((r) => { tm = setTimeout(r, Math.max(ms, 0)); })]);
        clearTimeout(tm);
        waiter = null;
      }
      return q.shift() || null;
    },
    get lost() { return input.state === "disconnected" || output.state === "disconnected"; },
    close() {},
  };
}
async function session(fn) {
  const ports = mirrorTool.ports();
  if (!ports) throw Object.assign(new Error(`Connect to the FM-1 first (Connect, above).`), { code: "noport" });
  const link = makeLink(ports.input, ports.output);
  mirrorTool.take(link.sink);
  try { return await fn(link); } finally { mirrorTool.give(); }
}

function update() {
  const on = mirrorTool.connected && !busy && !TOOLS.on;
  readBtn.disabled = allBtn.disabled = allLoadBtn.disabled = !on;
  list.querySelectorAll("button").forEach((b) => { b.disabled = !on || b.dataset.needs === "never" || (b.dataset.needs === "used" && !last); });
  if (TOOLS.on && !busy) return;                                 // (another tool has the ports: its own words)
  if (!mirrorTool.connected && !busy) pjSay("Connect to the FM-1 above first.");
  else if (mirrorTool.connected && !busy && !last && statusEl.textContent.startsWith("Connect")) pjSay(`Press "Read the FM-1" to see its projects.`);
}
function render() {
  list.replaceChildren();
  for (let p = 0; p < PJ.SLOTS; p++) {
    const s = last ? PJ.slot(last.parsed.objs, p) : null;
    const li = document.createElement("li");
    const n = p + 1;
    li.innerHTML = `<span class="slot">Project ${n}</span><span class="pname"></span>` +
      `<button type="button" data-do="save" data-needs="used" aria-label="Save project ${n} to a file">Save to file</button>` +
      `<button type="button" data-do="put" aria-label="Replace project ${n} with a file">Replace with a file&hellip;</button>`;
    const nm = li.querySelector(".pname");
    nm.textContent = !s ? "Not read yet" : s.used ? s.name || "(no name)" : "Empty";
    nm.classList.toggle("empty", !s || !s.used);
    const save = li.querySelector('[data-do="save"]');
    save.dataset.slot = li.querySelector('[data-do="put"]').dataset.slot = String(p);
    if (s && !s.used) save.dataset.needs = "never";
    list.appendChild(li);
  }
  update();
}

async function readDevice() {
  pjSay("Reading the FM-1…");
  pjProgress(null);
  let r;
  try {
    r = await session((link) => PB.backup(link, (n) => {
      if (performance.now() - lastSaid > 1500) pjSay(`Reading the FM-1… ${Math.round(n / 1024)} kB`);
    }));
  } catch (e) {
    if (e.code === "stopped" && /didn't answer/.test(e.message)) {
      e.message = `${PB.NAME} didn't answer. Projects and backup need ${PB.NAME} ${PJ.APP} or later on the FM-1: install it first, and plug the FM-1 in with the screen on.`;
    }
    throw e;
  } finally {
    pjProgress(false);
  }
  last = { image: r.image, flags: r.flags, parsed: PB.parse(r.image), at: new Date() };
  render();
  return last;
}

// an image to the FM-1 (it asks for its pieces) -> true when restored; false (and said) when not
async function sendImage(image) {
  pjSay("Sending to the FM-1…");
  pjProgress(null);
  let code;
  try {
    code = await session((link) => PB.restore(link, image, (ev, pct) => {
      if (ev === "ask") { pjProgress(false); pjSay("Press SEL on the FM-1 to confirm, or HOME to cancel.", "wait"); }
      else if (ev === "progress" && performance.now() - lastSaid > 1500) { pjProgress(pct); pjSay(`Sending to the FM-1… ${Math.round(pct)}%`); }
    }));
  } finally {
    pjProgress(false);
  }
  if (code === 0) return true;
  if (code === PB.PLAYING) pjSay(`${PB.NAME} is playing. Press PLAY on the FM-1 to stop it, then try again. Nothing was changed.`, "err");
  else if (code === PB.DECLINED) pjSay("HOME was pressed on the FM-1. Nothing was changed.");
  else pjSay(`The FM-1 did not take it: ${PB.ERRORS[code] || "error " + code}. Nothing was changed${code === 12 ? ", unless it finishes the last step when it is switched on again" : ""}.`, "err");
  return false;
}

function confirmAsk(text, yes) {
  return new Promise((resolve) => {
    askText.textContent = text;
    askYes.textContent = yes;
    askBox.hidden = false;
    askYes.focus();
    const done = (v) => { askBox.hidden = true; askYes.onclick = askNo.onclick = null; resolve(v); };
    askYes.onclick = () => done(true);
    askNo.onclick = () => done(false);
  });
}

async function run(fn) {
  if (busy || TOOLS.on) return;
  busy = true;
  TOOLS.set(true);
  try { await fn(); }
  catch (e) { pjSay(e.message || String(e), "err"); }
  finally { busy = false; pjProgress(false); TOOLS.set(false); }
}

readBtn.addEventListener("click", () => run(async () => {
  await readDevice();
  const used = [0, 1, 2, 3].filter((p) => PJ.slot(last.parsed.objs, p).used).length;
  pjSay(`Read the FM-1: ${used} of ${PJ.SLOTS} projects in use.`);
}));
allBtn.addEventListener("click", () => run(async () => {
  await readDevice();
  download(last.image, `${safe(PB.NAME)}-backup-${stamp()}.{{EXT}}`);
  pjSay(`Saved everything to a file (${Math.round(last.image.length / 1024)} kB).`);
}));
list.addEventListener("click", (e) => {
  const b = e.target.closest("button");
  if (!b || b.disabled) return;
  const p = Number(b.dataset.slot);
  if (b.dataset.do === "save") {
    run(async () => {
      await readDevice();
      const s = PJ.slot(last.parsed.objs, p);
      if (!s.used) { pjSay(`Project ${p + 1} is empty. Nothing to save.`); return; }
      download(PJ.projectFile(s), `${safe(PB.NAME)}-project-${safe(s.name)}-${stamp()}.fampj`);
      pjSay(`Saved project ${p + 1} (${s.name}) to a file.`);
    });
  } else if (b.dataset.do === "put") {
    wantSlot = p;
    fileProject.value = "";
    fileProject.click();
  }
});
fileProject.addEventListener("change", () => {
  const f = fileProject.files && fileProject.files[0];
  if (!f) return;
  const p = wantSlot;
  run(async () => {
    const proj = PJ.readProjectFile(new Uint8Array(await f.arrayBuffer()));
    const note = proj.appVersion && proj.appVersion !== PJ.APP ? ` It was saved with ${PB.NAME} ${proj.appVersion}; this page is for ${PJ.APP}.` : "";
    const yes = await confirmAsk(`Replace project ${p + 1} with "${proj.name}" from this file? What project ${p + 1} holds now is lost.${note} ` +
      `Stop the sequencer first. The FM-1 asks you to confirm with SEL. ${BETA_NOTE}`, `Replace project ${p + 1}`);
    if (!yes) { pjSay("Cancelled. Nothing was changed."); return; }
    const d = await readDevice();                                // the merge is made on what the FM-1 holds now
    if (d.flags & 1) { pjSay(`${PB.NAME} is playing. Press PLAY on the FM-1 to stop it, then try again. Nothing was changed.`, "err"); return; }
    if (!(await sendImage(PJ.putProject(d.parsed, p, proj.parts)))) return;
    pjSay(`Project ${p + 1} is now "${proj.name}".`);
    await pause(1500);
    try { await readDevice(); pjSay(`Project ${p + 1} is now "${proj.name}".`); } catch (_) { /* (the list stays as it was: Read the FM-1 again) */ }
  });
});
allLoadBtn.addEventListener("click", () => { fileAll.value = ""; fileAll.click(); });
fileAll.addEventListener("change", () => {
  const f = fileAll.files && fileAll.files[0];
  if (!f) return;
  run(async () => {
    const image = new Uint8Array(await f.arrayBuffer());
    PB.parse(image);                                             // (checked whole: a bad file is never sent)
    const yes = await confirmAsk(`Load everything from "${f.name}"? It replaces all that ${PB.NAME} has saved in the FM-1: the sound, the patterns, ` +
      `the presets and the four projects. Stop the sequencer first. The FM-1 asks you to confirm with SEL. ${BETA_NOTE}`, "Load everything");
    if (!yes) { pjSay("Cancelled. Nothing was changed."); return; }
    if (!(await sendImage(image))) return;
    pjSay("Everything is loaded.");
    await pause(1500);
    try { await readDevice(); pjSay("Everything is loaded."); } catch (_) { last = null; render(); }
  });
});
mirrorTool.onports(() => { if (!busy) update(); });
TOOLS.hooks.push(update);
render();
}

// --- install: begin (fam/tests/install_page_test.mjs runs this part in Node, after OTA)
// A release of this firmware from GitHub, or a package file, written to the FM-1 with the installer's own update protocol
// (OTA: web/fm1ota.js, fm1pkg.js). A browser cannot download the file attached to a release (GitHub sends no CORS headers
// for it), so the page reads the list of releases from the API and downloads the copy of that file kept in the repository
// (fam/releases/NAME.fwsc, through the contents API), which must have the SHA-256 the release shows for its file.
const INST = (() => {
  const REPO = "davidamomusic/fm-fam", DIR = "fam/releases", LISTED = 8;
  const API = `https://api.github.com/repos/${REPO}`;
  const err = (message, code) => Object.assign(new Error(message), { code });

  // the API's releases -> [{tag, title, date, pre, asset, size, sha}], newest first as GitHub lists them: no drafts, the
  // first .fwsc of each (a name safe in a URL), its SHA-256 when GitHub gives one
  function releases(json) {
    if (!Array.isArray(json)) throw err("GitHub's answer was not a list of releases.", "github");
    const out = [];
    for (const r of json) {
      if (!r || r.draft) continue;
      const a = (r.assets || []).find((x) => x && /^[A-Za-z0-9._-]+\.fwsc$/.test(x.name || ""));
      if (!a) continue;
      const m = /^sha256:([0-9a-f]{64})$/.exec(a.digest || "");
      out.push({ tag: String(r.tag_name || ""), title: String(r.name || r.tag_name || ""), date: r.published_at || "", pre: !!r.prerelease,
                 asset: a.name, size: Number(a.size) || 0, sha: m ? m[1] : "" });
      if (out.length >= LISTED) break;
    }
    return out;
  }

  // the bytes of a package -> {product, image, sha}, or an Error with a code (sha, badpkg): a file that is not an FM-1
  // package with an update loader of Felucca's, a damaged one, or, with want, one whose SHA-256 is another
  async function prepare(raw, want) {
    const sha = await OTA.sha256hex(raw);
    if (want && sha !== want)
      throw err(`That copy is not the file attached to the release (its SHA-256 differs). Nothing was written. Download the release's file from GitHub and use "Install a file".`, "sha");
    let product;
    try { product = OTA.productOf(raw); } catch (_) { throw err("This is not an FM-1 package.", "badpkg"); }
    if (!/^FM-1_\d+$/.test(product)) throw err(`This is not an FM-1 package (it says ${JSON.stringify(product)}).`, "badpkg");
    const image = OTA.logicalImage(raw);
    try { OTA.checkUfw(image); } catch (e) { throw err(`${e.message}. Nothing was written.`, "badpkg"); }
    if (!OTA.hasLoader(image)) throw err("This package has no update loader of Felucca's, so this page does not write it. Use the updater it came with.", "badpkg");
    return { product, image, sha };
  }

  // the bar: Updater's steps -> its value 0..100, or null to leave it (the installer page's measure: the bytes the device
  // reads for this package, the check of the whole package first, then the write; held at 99 until the FM-1 is back)
  function barOf(image) {
    const ent = OTA.ufwEntries(image), fl = ent[0], ldr = ent[100];
    const CHECK = 0x400 + 0x400 + (ldr ? ldr.len - 0x20 : 0);
    const WRITE = fl && fl.len > 0x4000 && fl.len <= 0x100000 ? 0x400 + 0x20 + fl.len + (fl.len - 0x4000) : image.length;
    const total = CHECK + WRITE;
    return (k, a, b, pct) => {
      if (k === "verify") return (Math.min(b, CHECK) / total) * 100;
      if (k === "write") return Math.min(99, ((CHECK + (pct == null ? b : (pct / 100) * WRITE)) / total) * 100);
      if (k === "done") return 100;
      return null;
    };
  }

  const when = (iso) => { const d = new Date(iso); return isNaN(d) ? "" : d.toLocaleDateString("en-GB", { day: "numeric", month: "short", year: "numeric" }); };
  const kb = (n) => `${Math.round(n / 1024)} kB`;
  return { API, DIR, releases, prepare, barOf, when, kb, err };
})();
// --- install: end

{
const $ = (i) => document.getElementById(i);
const checkBtn = $("inCheck"), fileBtn = $("inFile"), statusEl = $("inStatus"), bar = $("inBar"), list = $("inList"), input = $("inFileInput");
const askBox = $("inAsk"), askText = $("inAskText"), askYes = $("inAskYes"), askNo = $("inAskNo");
const NOT_YET = "Connect to the FM-1 above first.", READY = "Check GitHub for the versions, or choose a package file.";
let shown = [];

function say(text, kind = "") {
  statusEl.textContent = text;
  statusEl.className = "msg" + (kind ? " " + kind : "");
}
function progress(v) {                                           // false: none, null: busy without a figure, 0..100
  bar.hidden = v === false;
  if (typeof v === "number") bar.value = v; else bar.removeAttribute("value");
}
function update() {
  const connected = !!mirrorTool.access, off = TOOLS.on;
  checkBtn.disabled = off;
  fileBtn.disabled = off || !connected;
  list.querySelectorAll("button").forEach((b) => { b.disabled = off || !connected; });
  if (off) return;
  if (!connected && statusEl.textContent === READY) say(NOT_YET);
  else if (connected && statusEl.textContent === NOT_YET) say(READY);
}
function confirmAsk(text) {
  return new Promise((resolve) => {
    askText.textContent = text;
    askBox.hidden = false;
    askYes.focus();
    const done = (v) => { askBox.hidden = true; askYes.onclick = askNo.onclick = null; resolve(v); };
    askYes.onclick = () => done(true);
    askNo.onclick = () => done(false);
  });
}
async function exclusive(fn) {
  if (TOOLS.on) return;
  TOOLS.set(true);
  try { await fn(); }
  catch (e) { say(e.message || String(e), "err"); }
  finally { progress(false); TOOLS.set(false); }
}
async function github(path, accept) {
  let r;
  try { r = await fetch(`${INST.API}/${path}`, { headers: { Accept: accept } }); }
  catch (_) { throw INST.err(`Could not reach GitHub. Check the connection, or download the package from the release page and use "Install a file".`, "net"); }
  if (r.status === 403 || r.status === 429) throw INST.err(`GitHub is limiting requests from this network right now. Try again later, or choose a package file.`, "limit");
  return r;
}

function renderList() {
  list.replaceChildren();
  shown.forEach((rel, i) => {
    const li = document.createElement("li");
    const chips = (i === 0 ? `<span class="chip now">Latest</span>` : "") + (rel.pre ? `<span class="chip">Pre-release</span>` : "");
    li.innerHTML = `<span class="slot"></span><span class="meta"></span>${chips}<button type="button" data-i="${i}"></button>`;
    li.querySelector(".slot").textContent = rel.tag;
    li.querySelector(".meta").textContent = [rel.title, INST.when(rel.date), INST.kb(rel.size)].filter(Boolean).join(" · ");
    const b = li.querySelector("button");
    b.textContent = "Install";
    b.setAttribute("aria-label", `Install ${rel.title || rel.tag}`);
    list.appendChild(li);
  });
  update();
}

// the package is on its way to the FM-1: the mirror lets go of the ports (it drops them and picks them afresh after, the FM-1
// restarts twice), the Updater of fm1ota.js does the rest, and a device already in update mode is finished
async function install(pkg) {
  const access = mirrorTool.access;
  if (!access) throw INST.err(NOT_YET, "noport");
  const up = new OTA.Updater(access, () => {});
  const here = mirrorTool.ports();
  if (here) up.allow(here.input.name);
  const value = INST.barOf(pkg.image);
  const words = { start: "Starting the update. Do not unplug the FM-1.", loader: "Switching to update mode…", reboot: "Restarting…" };
  const step = (k, a, b, pct) => {
    const v = value(k, a, b, pct);
    if (v !== null) { progress(v); if (k === "verify") say(`Checking the package… ${Math.round(v)}%`, "wait"); if (k === "write") say(`Writing… ${Math.round(v)}%`, "wait"); }
    else if (words[k]) say(words[k], "wait");
  };
  say("Looking for the FM-1…", "wait");
  progress(null);
  mirrorTool.suspend();
  let now = "";
  try {
    const loader = await up.find((id) => /^ota-/i.test(id.model));
    if (loader) {
      loader.link.close();
      if (!(await up.resume(pkg.image, step, pkg.product))) throw INST.err("The update did not finish. Replug the USB cable and install again: it carries on where it stopped.", "stopped");
      now = pkg.product;
    } else now = await up.install(pkg.image, pkg.product, step, pkg.sha);
  } finally { mirrorTool.resume(); }
  progress(100);
  say(`Done: the FM-1 now runs ${now}. Its screen comes back in a moment.`);
}
const ASK = (what) => `Install ${what} on the FM-1? The sound and the screen stop for about a minute while it writes: do not unplug the USB cable or switch the FM-1 off meanwhile. ` +
  `What {{NAME}} has saved is not touched, but save everything first if it matters. ${BETA_NOTE}`;

checkBtn.addEventListener("click", () => exclusive(async () => {
  say("Asking GitHub for the versions…", "wait");
  progress(null);
  const r = await github("releases?per_page=12", "application/vnd.github+json");
  if (!r.ok) throw INST.err(`GitHub answered ${r.status}.`, "github");
  shown = INST.releases(await r.json());
  renderList();
  say(shown.length ? `${shown.length} version${shown.length > 1 ? "s" : ""} on GitHub. Pick one to install.` : "GitHub has no release with a package yet.");
}));
list.addEventListener("click", (e) => {
  const b = e.target.closest("button");
  const rel = b && !b.disabled ? shown[Number(b.dataset.i)] : null;
  if (!rel) return;
  exclusive(async () => {
    if (!mirrorTool.access) throw INST.err(NOT_YET, "noport");
    if (!rel.sha) throw INST.err(`GitHub gave no checksum for this release, so its copy cannot be checked. Download the file from the release page and use "Install a file".`, "sha");
    say(`Downloading ${rel.asset}…`, "wait");
    progress(null);
    const r = await github(`contents/${INST.DIR}/${rel.asset}`, "application/vnd.github.raw+json");
    if (r.status === 404) throw INST.err(`The repository has no copy of ${rel.asset} yet. Download it from the release page and use "Install a file".`, "nocopy");
    if (!r.ok) throw INST.err(`GitHub answered ${r.status}.`, "github");
    const pkg = await INST.prepare(new Uint8Array(await r.arrayBuffer()), rel.sha);
    progress(false);
    say(`${rel.asset} is ready (SHA-256 ${pkg.sha.slice(0, 12)}…, the release's own).`);
    if (!(await confirmAsk(ASK(rel.title || rel.tag)))) { say("Cancelled. Nothing was written."); return; }
    await install(pkg);
  });
});
fileBtn.addEventListener("click", () => { input.value = ""; input.click(); });
input.addEventListener("change", () => {
  const f = input.files && input.files[0];
  if (!f) return;
  exclusive(async () => {
    if (!mirrorTool.access) throw INST.err(NOT_YET, "noport");
    const pkg = await INST.prepare(new Uint8Array(await f.arrayBuffer()), "");
    say(`${f.name} is an FM-1 package (${pkg.product}, SHA-256 ${pkg.sha.slice(0, 12)}…).`);
    if (!(await confirmAsk(ASK(`"${f.name}"`)))) { say("Cancelled. Nothing was written."); return; }
    await install(pkg);
  });
});
mirrorTool.onports(update);
TOOLS.hooks.push(update);
say(NOT_YET);
update();
}
