// SPDX-License-Identifier: MPL-2.0
// FM-FAM's mirror page, Install a version, in a real (headless) Chromium: a fake Web MIDI with the FM-1's mirror behind it, a
// GitHub simulated at the network (the API's list of releases, the contents API's copy of the package, as the page asks for
// them, with the CORS header the real API sends), and the update protocol itself stubbed (its own tests are tests/installer_*:
// this one is about the page). What the person does: check GitHub, pick a version, say no, say yes; what the page must do:
// download, check the SHA-256, ask, give the ports up while the update runs and take them back after, say what went wrong
// when the copy is missing or is another file, when GitHub limits the requests, when the FM-1 is not found; install a file
// from the computer; finish an update that was cut off.
//   node fam/tests/install_browser_test.mjs        (from the repository's root; needs playwright-core and a Chromium:
//   PLAYWRIGHT_CORE and CHROMIUM in the environment, or /opt/npm-tools and /opt/pw-browsers; skipped without them)
import fs from "fs";
import os from "os";
import path from "path";
import { execFileSync } from "child_process";
import { fileURLToPath } from "url";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
const core = process.env.PLAYWRIGHT_CORE || "/opt/npm-tools/node_modules/playwright-core/index.mjs";
const chromePath = process.env.CHROMIUM || (fs.existsSync("/opt/pw-browsers") ? fs.readdirSync("/opt/pw-browsers").filter((d) => d.startsWith("chromium-"))
  .map((d) => `/opt/pw-browsers/${d}/chrome-linux64/chrome`).find((p) => fs.existsSync(p)) : null);
if (!fs.existsSync(core) || !chromePath) { console.log("SKIP no playwright-core or Chromium"); process.exit(0); }
const { chromium } = await import(core);

let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "ok  " : "FAIL"} ${msg}`); if (!ok) fails++; };

const html = execFileSync("python3", ["-c",
  "import sys; sys.path.insert(0, 'tools'); from pathlib import Path; import make_site; " +
  "sys.stdout.write(make_site.tool_pages(Path('fam'))['mirror/index.html'])"], { cwd: root, maxBuffer: 1 << 28 }).toString("utf8");
const work = fs.mkdtempSync(path.join(os.tmpdir(), "fam-install-"));
const pagePath = path.join(work, "mirror.html");
// the page's names live in a block (so that its decoder can run in Node): the test reaches the three it stubs and reads
// through window.__t, put in by this test into its own copy of the page
const HOOK_AT = "// keep-alive (the FM-1 stops after 6 s without one)";
if (html.split(HOOK_AT).length !== 2) { console.log("FAIL the page's keep-alive marker is not there once"); process.exit(1); }
fs.writeFileSync(pagePath, html.replace(HOOK_AT, "window.__t = { OTA, mirrorTool, TOOLS };\n" + HOOK_AT));

const PK = { "fm-fam-0.20.fwsc": "5cdbe6025e64a75e7204d6e5515d465501e5a6de256929c944298b5abf1aafa2",
             "fm-fam-0.19.fwsc": "1d6b8a6e984df78a8657ef4f04c7942cdbcd9ed3816bfbd8b2628bad76636c26" };
const pkg = (n) => fs.readFileSync(path.join(root, "fam/releases", n));
const releases = (shas = PK) => [
  { tag_name: "v0.20", name: "fm-fam v0.20", draft: false, prerelease: true, published_at: "2026-11-08T10:27:42Z",
    assets: [{ name: "fm-fam-0.20.fwsc", size: 614684, digest: "sha256:" + shas["fm-fam-0.20.fwsc"] }] },
  { tag_name: "v0.19", name: "fm-fam v0.19", draft: false, prerelease: true, published_at: "2026-11-08T08:26:48Z",
    assets: [{ name: "fm-fam-0.19.fwsc", size: 614684, digest: "sha256:" + shas["fm-fam-0.19.fwsc"] }] },
];

// ---- the browser's side: a fake Web MIDI with the FM-1's mirror behind it
const fakeMidi = `(() => {
  const dev = window.__fm1 = { mirrorOps: [] };
  const pack7 = (d) => { const o = []; let acc = 0, nb = 0; for (const b of d) { acc |= b << nb; nb += 8; while (nb >= 7) { o.push(acc & 127); acc >>>= 7; nb -= 7; } } if (nb) o.push(acc & 127); return o; };
  const unpack7 = (s) => { const o = []; let acc = 0, nb = 0; for (const b of s) { acc |= (b & 127) << nb; nb += 7; while (nb >= 8) { o.push(acc & 255); acc >>>= 8; nb -= 8; } } return o; };
  const frame = (cmd, body) => { const s = body.reduce((a, v) => a + v, 0); return [0xF0, ...pack7([0, 0x59, cmd, body.length & 255, body.length >> 8, 0, ...body, ~s & 255]), 0xF7]; };
  const input = { id: "in1", name: "FM-FAM FM-1", type: "input", state: "connected", connection: "open", onmidimessage: null, open: async () => input, close: async () => input };
  const emit = (arr) => { if (input.onmidimessage) input.onmidimessage({ data: Uint8Array.from(arr) }); };
  const output = { id: "out1", name: "FM-FAM FM-1", type: "output", state: "connected", connection: "open", open: async () => output, close: async () => output,
    send(arr) {
      const u = unpack7(Array.from(Uint8Array.from(arr).slice(1, -1)));
      if (u[0] !== 0 || u[1] !== 0x59 || u[2] !== 0x50) return;
      dev.mirrorOps.push(u[6]);
      setTimeout(() => emit(frame(0x50, [4, u[6] ? 1 : 0, 239, 239])));
    } };
  const access = { inputs: new Map([[input.id, input]]), outputs: new Map([[output.id, output]]), onstatechange: null, sysexEnabled: true };
  navigator.requestMIDIAccess = async () => access;
  Object.defineProperty(navigator, "permissions", { value: { query: () => Promise.reject(new Error("no")) }, configurable: true });
})();`;

// ---- GitHub, as the page asks for it
const gh = { releasesStatus: 200, shas: PK, copy: (name) => ({ status: 200, body: pkg(name) }), seen: [] };
const CORS = { "access-control-allow-origin": "*" };

const browser = await chromium.launch({ executablePath: chromePath, args: ["--no-sandbox"] });
const ctx = await browser.newContext({ acceptDownloads: true, viewport: { width: 1200, height: 1300 } });
const page = await ctx.newPage();
const errors = [];
page.on("pageerror", (e) => errors.push(e.message));
page.on("console", (m) => { if (m.type() === "error" && !/Failed to load resource/.test(m.text())) errors.push(m.text()); });
await page.route("https://api.github.com/**", (route) => {
  const url = new URL(route.request().url());
  gh.seen.push({ path: url.pathname, accept: route.request().headers().accept || "" });
  if (url.pathname === "/repos/davidamomusic/fm-fam/releases") {
    if (gh.releasesStatus !== 200) return route.fulfill({ status: gh.releasesStatus, headers: CORS, contentType: "application/json", body: '{"message":"x"}' });
    return route.fulfill({ status: 200, headers: CORS, contentType: "application/json", body: JSON.stringify(releases(gh.shas)) });
  }
  const m = /^\/repos\/davidamomusic\/fm-fam\/contents\/fam\/releases\/([A-Za-z0-9._-]+)$/.exec(url.pathname);
  if (m) {
    const r = gh.copy(m[1]);
    return route.fulfill({ status: r.status, headers: CORS, contentType: "application/octet-stream", body: r.body || "{}" });
  }
  return route.fulfill({ status: 404, headers: CORS, body: "{}" });
});
await page.addInitScript(fakeMidi);
await page.goto("file://" + pagePath);

const text = (sel) => page.textContent(sel).then((t) => t.trim());
const waitIn = (re, ms = 15000) => page.waitForFunction((src) => new RegExp(src).test(document.getElementById("inStatus").textContent), re.source, { timeout: ms });
const U = () => page.evaluate(() => JSON.parse(JSON.stringify(window.__u)));

// ---- before Connect
check((await text("#inStatus")).startsWith("Connect"), "before Connect: the section says to connect first");
check(await page.isDisabled("#inFile") && await page.isEnabled("#inCheck"), "... Install a file is off; Check GitHub is on (it needs no FM-1)");
await page.click("#inCheck");
await waitIn(/2 versions on GitHub/);
check(await page.$$eval("#inList button", (l) => l.every((b) => b.disabled)), "Check GitHub before Connect: the versions are listed, their Install buttons are off");
const row0 = await page.$eval("#inList li:nth-child(1)", (li) => li.textContent.replace(/\s+/g, " ").trim());
check(/v0\.20/.test(row0) && /fm-fam v0\.20/.test(row0) && /8 Nov 2026/.test(row0) && /600 kB/.test(row0) && /Latest/.test(row0) && /Pre-release/.test(row0) && /Install$/.test(row0),
      `... a row reads: ${row0}`);
const row1 = await page.$eval("#inList li:nth-child(2)", (li) => li.textContent);
check(/v0\.19/.test(row1) && !/Latest/.test(row1), "... only the newest is marked Latest");
check(gh.seen.length === 1 && /vnd\.github/.test(gh.seen[0].accept), "... one request to GitHub's API, nothing downloaded yet");

await page.click("#connect");
await page.waitForFunction(() => /Mirroring/.test(document.getElementById("status").textContent), null, { timeout: 8000 });
check(await page.isEnabled("#inFile") && await page.$$eval("#inList button", (l) => l.every((b) => !b.disabled)), "Connect: Install a file and the Install buttons are on");

// the update protocol stubbed: what the page hands it, and what the page is like while it runs
await page.evaluate(() => {
  const { OTA, mirrorTool } = window.__t;
  const P = OTA.Updater.prototype;
  const u = window.__u = { calls: [], during: null, fail: null, loader: null, resumeOk: true };
  const snap = () => ({ ports: !!mirrorTool.ports(), connected: mirrorTool.connected, hasAccess: !!mirrorTool.access, pjRead: document.getElementById("pjRead").disabled,
                        inCheck: document.getElementById("inCheck").disabled, inFile: document.getElementById("inFile").disabled, status: document.getElementById("status").textContent });
  P.find = async function () { return u.loader; };
  P.install = async function (image, product, step, sha) {
    u.calls.push({ kind: "install", product, sha, len: image.length, allowed: [...this.allowed] });
    u.during = snap();
    if (u.fail) throw Object.assign(new Error(u.fail), { code: "notfound" });
    step("start", "FM-1_111"); step("verify", 3, 3000); step("loader"); step("write", 5, 200000, 40); step("write", 9, 400000, 80); step("reboot"); step("done", product);
    await new Promise((r) => setTimeout(r, 150));
    return product;
  };
  P.resume = async function (image, step, product) {
    u.calls.push({ kind: "resume", product, len: image.length });
    u.during = snap();
    if (u.resumeOk) { step("write", 9, 400000, 90); step("reboot"); step("done", product); }
    return u.resumeOk;
  };
});

// ---- a version: cancel, then install
const ops0 = await page.evaluate(() => window.__fm1.mirrorOps.length);
await page.click('#inList button[data-i="0"]');
await page.waitForSelector("#inAsk:not([hidden])");
const q = await text("#inAskText");
check(/Install fm-fam v0\.20 on the FM-1/.test(q) && /do not unplug/.test(q) && /not touched/.test(q), `Install: the page downloaded, checked and asks first ("${q.slice(0, 60)}...")`);
check(/5cdbe6025e64/.test(await text("#inStatus")), "... and says the SHA-256 it checked, the release's own");
const contentsReq = gh.seen.find((s) => /contents/.test(s.path));
check(contentsReq && contentsReq.path.endsWith("/contents/fam/releases/fm-fam-0.20.fwsc") && /raw/.test(contentsReq.accept), "... the copy came from the repository's fam/releases through the contents API, raw");
check((await U()).calls.length === 0, "... nothing handed to the update before the answer");
check(await page.isDisabled("#pjRead") && await page.isDisabled("#inCheck"), "... the other buttons are off while the question is open (the ports are this tool's until it ends)");
await page.click("#inAskNo");
await waitIn(/Cancelled\. Nothing was written/);
check((await U()).calls.length === 0 && await page.isHidden("#inAsk"), "No: said, nothing handed to the update");

await page.click('#inList button[data-i="0"]');
await page.waitForSelector("#inAsk:not([hidden])");
await page.click("#inAskYes");
await waitIn(/Done: the FM-1 now runs FM-1_111/, 20000);
let u = await U();
check(u.calls.length === 1 && u.calls[0].kind === "install" && u.calls[0].product === "FM-1_111" && u.calls[0].sha === PK["fm-fam-0.20.fwsc"] && u.calls[0].len === 614684 - 20,
      "Yes: the update got the package's image, FM-1_111 and its SHA-256");
check(u.calls[0].allowed.includes("FM-FAM FM-1"), "... the port the mirror was using is allowed to the update");
check(!u.during.ports && !u.during.connected && u.during.hasAccess && /Updating the firmware/.test(u.during.status), "while it ran, the mirror had let go of the ports and said so");
check(u.during.pjRead && u.during.inCheck && u.during.inFile, "... the Projects and the Install buttons were off");
check(await page.evaluate((n) => window.__fm1.mirrorOps.slice(n).includes(2), ops0), "after it, the mirror took the ports back and asked for the screen");
await page.waitForFunction(() => /Mirroring/.test(document.getElementById("status").textContent), null, { timeout: 8000 });
check(await page.isEnabled("#pjRead") && await page.isEnabled("#inCheck") && await page.isEnabled("#inFile") && await page.isHidden("#inBar"), "... and the buttons are on again, the bar away, \"Done\" said");

// ---- the copy missing, another file, GitHub limiting
gh.copy = (n) => (n === "fm-fam-0.19.fwsc" ? { status: 404, body: null } : { status: 200, body: pkg(n) });
await page.click('#inList button[data-i="1"]');
await waitIn(/no copy of fm-fam-0\.19\.fwsc/);
check((await U()).calls.length === 1 && await page.isHidden("#inAsk"), "a release whose copy is not in the repository: said (use Install a file), nothing asked or written");
gh.copy = () => ({ status: 200, body: pkg("fm-fam-0.19.fwsc") });
await page.click('#inList button[data-i="0"]');
await waitIn(/not the file attached to the release/);
check((await U()).calls.length === 1 && await page.isHidden("#inAsk"), "a copy that is not the release's file: refused by its SHA-256, nothing asked or written");
gh.copy = (n) => ({ status: 200, body: pkg(n) });
gh.releasesStatus = 403;
await page.click("#inCheck");
await waitIn(/limiting requests/);
check(true, "GitHub answering 403: the page says it limits requests and offers the file");
gh.releasesStatus = 200;
gh.shas = { ...PK, "fm-fam-0.20.fwsc": "" };
await page.click("#inCheck");
await waitIn(/2 versions on GitHub/);
await page.click('#inList button[data-i="0"]');
await waitIn(/gave no checksum/);
check((await U()).calls.length === 1, "a release GitHub gives no checksum for: its copy is not used");
gh.shas = PK;

// ---- the FM-1 not found: said, and the mirror comes back
await page.click("#inCheck");
await waitIn(/2 versions on GitHub/);
await page.evaluate(() => { window.__u.fail = "FM-1 not found (USB cable, and no other app using it?)"; });
await page.click('#inList button[data-i="0"]');
await page.waitForSelector("#inAsk:not([hidden])");
await page.click("#inAskYes");
await waitIn(/FM-1 not found/);
check(await page.isEnabled("#inCheck") && await page.isEnabled("#pjRead"), "an update that fails (FM-1 not found): the message is shown, the page is usable again");
await page.waitForFunction(() => /Mirroring/.test(document.getElementById("status").textContent), null, { timeout: 8000 });
check(true, "... and the mirror is back");
await page.evaluate(() => { window.__u.fail = null; });

// ---- a file from the computer
const file = path.join(work, "my.fwsc"); fs.writeFileSync(file, pkg("fm-fam-0.20.fwsc"));
let [chooser] = await Promise.all([page.waitForEvent("filechooser"), page.click("#inFile")]);
await chooser.setFiles(file);
await page.waitForSelector("#inAsk:not([hidden])");
check(/my\.fwsc is an FM-1 package \(FM-1_111/.test(await text("#inStatus")) && /Install "my\.fwsc" on the FM-1/.test(await text("#inAskText")), "Install a file: the page reads it (FM-1_111) and asks first");
await page.click("#inAskYes");
await waitIn(/Done: the FM-1 now runs FM-1_111/, 20000);
u = await U();
check(u.calls.length === 3 && u.calls[2].kind === "install" && u.calls[2].sha === PK["fm-fam-0.20.fwsc"], "... Yes: the update got that package");
const junk = path.join(work, "junk.fwsc"); fs.writeFileSync(junk, Buffer.alloc(5000, 7));
[chooser] = await Promise.all([page.waitForEvent("filechooser"), page.click("#inFile")]);
await chooser.setFiles(junk);
await waitIn(/not an FM-1 package/);
check(await page.isHidden("#inAsk") && (await U()).calls.length === 3, "a file that is not a package: said, nothing asked or written");
const bad = Buffer.from(pkg("fm-fam-0.20.fwsc")); bad[200000] ^= 1;
const badPath = path.join(work, "bad.fwsc"); fs.writeFileSync(badPath, bad);
[chooser] = await Promise.all([page.waitForEvent("filechooser"), page.click("#inFile")]);
await chooser.setFiles(badPath);
await waitIn(/damaged/);
check(await page.isHidden("#inAsk") && (await U()).calls.length === 3, "a damaged package: said (checksum), nothing asked or written");

// ---- an update that was cut off: the FM-1 is in its update loader, the page finishes it
await page.evaluate(() => { window.__u.loader = { link: { close() {} }, id: { model: "ota-FM-1_111" } }; });
[chooser] = await Promise.all([page.waitForEvent("filechooser"), page.click("#inFile")]);
await chooser.setFiles(file);
await page.waitForSelector("#inAsk:not([hidden])");
await page.click("#inAskYes");
await waitIn(/Done: the FM-1 now runs FM-1_111/, 20000);
u = await U();
check(u.calls.length === 4 && u.calls[3].kind === "resume" && u.calls[3].product === "FM-1_111", "the FM-1 waiting in its update loader: the page finishes that update (resume), not a new one");
await page.evaluate(() => { window.__u.resumeOk = false; });
[chooser] = await Promise.all([page.waitForEvent("filechooser"), page.click("#inFile")]);
await chooser.setFiles(file);
await page.waitForSelector("#inAsk:not([hidden])");
await page.click("#inAskYes");
await waitIn(/did not finish/);
check(true, "... an update that does not finish: said, with what to do (replug and install again)");

check(errors.length === 0, "no errors in the page's console" + (errors.length ? ": " + errors.slice(0, 3).join(" | ") : ""));
await page.screenshot({ path: path.join(work, "page.png"), fullPage: true });
console.log("screenshot " + path.join(work, "page.png"));
await browser.close();
console.log(fails ? `\n${fails} FAILED` : "\nall ok");
process.exit(fails ? 1 : 0);
