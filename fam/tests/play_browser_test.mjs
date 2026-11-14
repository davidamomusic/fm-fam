// SPDX-License-Identifier: MPL-2.0
// FM-FAM's browser player (fam/docs/play, the page fmhub.json's "play" points to) in a real (headless) Chromium: the page
// loads, "Switch on" starts the module, and the FM-1's screen is drawn; no screen reader and no MIDI; Render Audio renders
// the pattern playing (whole, sequence only, kick only) into a waveform and a WAV file.
//   node fam/tests/play_browser_test.mjs        (from the repository's root; needs playwright-core and a Chromium:
//   PLAYWRIGHT_CORE and CHROMIUM in the environment, or /opt/npm-tools and /opt/pw-browsers; skipped without them)
import fs from "fs";
import http from "http";
import path from "path";
import { fileURLToPath } from "url";

const dir = path.join(path.dirname(fileURLToPath(import.meta.url)), "../docs/play");
const core = process.env.PLAYWRIGHT_CORE || "/opt/npm-tools/node_modules/playwright-core/index.mjs";
const chromePath = process.env.CHROMIUM || (fs.existsSync("/opt/pw-browsers") ? fs.readdirSync("/opt/pw-browsers").filter((d) => d.startsWith("chromium-"))
  .map((d) => `/opt/pw-browsers/${d}/chrome-linux64/chrome`).find((p) => fs.existsSync(p)) : null);
if (!fs.existsSync(core) || !chromePath) { console.log("SKIP no playwright-core or Chromium"); process.exit(0); }
const { chromium } = await import(core);

let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "ok  " : "FAIL"} ${msg}`); if (!ok) fails++; };
const TYPE = { ".html": "text/html", ".js": "text/javascript", ".wasm": "application/wasm" };
const missing = [];
const srv = http.createServer((q, r) => {
  let p = path.join(dir, decodeURIComponent(q.url.split("?")[0]));
  if (p.endsWith(path.sep)) p += "index.html";
  if (!p.startsWith(dir) || !fs.existsSync(p)) { missing.push(q.url); r.writeHead(404); r.end(); return; }
  r.writeHead(200, { "content-type": TYPE[path.extname(p)] || "application/octet-stream" });
  r.end(fs.readFileSync(p));
}).listen(0);
const browser = await chromium.launch({ executablePath: chromePath, args: ["--autoplay-policy=no-user-gesture-required"] });
const page = await browser.newPage({ viewport: { width: 1100, height: 900 } });
const errors = [];
page.on("pageerror", (e) => errors.push(String(e)));
await page.goto(`http://127.0.0.1:${srv.address().port}/`);
check((await page.title()).includes("FM-FAM"), "the page is FM-FAM's");
// the tip under the title is added by hand to fam/docs/play/index.html: put it back after copying a new build
check(((await page.textContent("header p.tip")) || "").trim() === "Press OCT+ and REC to randomize sequences",
      "the tip under the title is there");
await page.screenshot({ path: "/tmp/play_tip.png", clip: { x: 0, y: 0, width: 1100, height: 260 } });
await page.click("#go");
await page.waitForTimeout(4000);
const lit = await page.evaluate(() => {
  const d = document.getElementById("screen").getContext("2d").getImageData(0, 0, 240, 240).data;
  let n = 0;
  for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 60) n++;
  return n;
});
check(lit > 5000, `the screen is drawn (${lit} lit pixels)`);
// the computer's keyboard plays it from anywhere on the page (the panel not clicked): a held key holds the button or key
await page.click("h1");
const heldNames = () => page.evaluate(() => [...document.querySelectorAll("#controls [aria-pressed=true]")].map((d) => d.getAttribute("aria-label").replace(/, .*/, "")).join("|"));
await page.keyboard.down("x"); await page.keyboard.down("q");
await page.waitForTimeout(60);
const held = await heldNames();
await page.keyboard.up("q"); await page.keyboard.up("x");
await page.waitForTimeout(120);
check(/OCT plus/.test(held) && /White key 1/.test(held), `the keyboard holds OCT+ (X) and white key 1 (Q) with the panel not focused (${held})`);
check((await heldNames()) === "", "... and lets them go");
const knobAngle = () => page.evaluate(() => document.querySelectorAll("#panel g")[0] && [...document.querySelectorAll("#panel [transform]")].map((e) => e.getAttribute("transform")).join(";"));
const a0 = await knobAngle();
await page.keyboard.press("Alt+KeyQ");
check((await knobAngle()) !== a0, "Option (Alt) + Q turns KNOB1");
const caps = await page.$$eval("#kbdmap .cap", (c) => c.length);
check(caps === 41, `the map of the keys is drawn (${caps} keys)`);
check(!(await page.$("#speak")) && !(await page.$("text=Screen Reader")), "no screen reader on the page");
check(!(await page.$("text=MIDI ports")) && !fs.existsSync(path.join(dir, "parts/midi.js")), "no MIDI on the page");

// Render Audio: the three renders of the pattern playing, from the same copy of the player
const rendered = async (label) => {
  if (label) await page.click(`#render .seg button:has-text("${label}")`);
  else await page.click('#render [data-r="go"]');
  await page.waitForFunction(() => /Rendered|no sound|^The |did not/.test(document.querySelector('#render [data-r="msg"]').textContent)
    && !document.querySelector('#render [data-r="go"]').disabled, null, { timeout: 60000 });
  return page.evaluate(() => {
    const r = document.getElementById("render"), cv = r.querySelector("canvas"), d = cv.getContext("2d").getImageData(0, 0, cv.width, cv.height).data;
    let lit = 0;
    for (let i = 0; i < d.length; i += 4) if (d[i] + d[i + 1] + d[i + 2] > 300) lit++;
    return { msg: r.querySelector('[data-r="msg"]').textContent, info: r.querySelector('[data-r="info"]').textContent,
             file: r.querySelector('[data-r="file"]').textContent, save: !r.querySelector('[data-r="save"]').disabled, lit };
  });
};
const t0 = Date.now();
const full = await rendered();
check(full.msg === "Rendered." && full.save && full.lit > 2000, `Render Audio: the whole pattern (${full.msg} ${full.info}; ${full.lit} lit; ${Date.now() - t0} ms)`);
check(/^fm-fam-A\d\d-full-\d+bpm\.wav$/.test(full.file), `its file is named for the pattern (${full.file})`);
await page.screenshot({ path: "/tmp/play_render.png", fullPage: true });
const kick = await rendered("Kick only");
check(kick.msg === "Rendered." && /-kick-/.test(kick.file), `the kick only (${kick.msg} ${kick.info})`);
const seq = await rendered("Sequence only");
check(/-sequence-/.test(seq.file), `the sequence only (${seq.msg} ${seq.info})`);
const wavInfo = await page.evaluate(async () => {
  const a = document.querySelector('#render [data-r="drag"]');
  return a.getAttribute("aria-disabled");
});
check(wavInfo === "false", "the audio can be dragged out");
check(errors.length === 0, `no script error (${errors.slice(0, 2).join("; ")})`);
// the page asks for two things it can do without: the screen reader's words and an icon
const needed = missing.filter((u) => !u.includes("reader/") && !u.includes("favicon"));
check(needed.length === 0, `every file the page needs is there (${needed.join(" ")})`);
await browser.close();
srv.close();
console.log(fails ? `${fails} FAILED` : "all passed");
process.exit(fails ? 1 : 0);
