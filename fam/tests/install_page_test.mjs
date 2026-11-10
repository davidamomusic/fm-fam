// SPDX-License-Identifier: MPL-2.0
// FM-FAM's mirror page, its Install a version part (fam/mirror/extra.js, INST and OTA), as tools/make_site.py fills it in:
//  1. the page has the section and nothing left unfilled, and the update protocol (OTA) is in a scope of its own;
//  2. the list of releases from GitHub's API (newest first, no drafts, only files with a safe name, the SHA-256 GitHub gives);
//  3. the packages kept in fam/releases/: each is an FM-1 package with Felucca's update loader and the identity FM-1_111, and
//     its SHA-256 is the one in the table below (the file attached to the release of the same name);
//  4. prepare(): the right file passes; a copy with another SHA-256, a damaged file, a file that is not a package and a
//     package without Felucca's loader (M-VAVE's own) are refused, each with its code;
//  5. the bar never goes back and ends at 100 for the steps Updater reports.
//   node fam/tests/install_page_test.mjs        (from the repository's root)
import fs from "fs";
import path from "path";
import { execFileSync } from "child_process";
import { fileURLToPath } from "url";

const root = path.join(path.dirname(fileURLToPath(import.meta.url)), "../..");
const html = execFileSync("python3", ["-c",
  "import sys; sys.path.insert(0, 'tools'); from pathlib import Path; import make_site; " +
  "sys.stdout.write(make_site.tool_pages(Path('fam'))['mirror/index.html'])"], { cwd: root, maxBuffer: 1 << 28 }).toString("utf8");
const PORTS_JS = fs.readFileSync(path.join(root, "web/fm1ports.js"), "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");

let fails = 0;
const check = (ok, msg) => { console.log(`${ok ? "ok  " : "FAIL"} ${msg}`); if (!ok) fails++; };

// ---------------------------------------------------------------- 1. the page
const left = html.match(/\{\{[A-Za-z_]+\}\}/g);
check(!left, "the page has nothing left unfilled" + (left ? ": " + left.join(" ") : ""));
check(html.includes('id="install"') && html.includes('id="inList"') && html.includes('id="inFileInput"'), "the Install a version section is in the page");
check(/get access\(\)/.test(html) && /suspend\(\)/.test(html) && /resume\(\)/.test(html), "mirrorTool gives the ports up (access, suspend, resume)");
check((html.match(/^const OTA = /gm) || []).length === 1, "the update protocol is in the page once, as OTA");
check(!/^(export|import) /m.test(html), "no module syntax is left in the page");

const pick = (a, b) => {
  const m = new RegExp(`// --- ${a}: begin[^\\n]*\\n([\\s\\S]*?)// --- ${b}: end`).exec(html);
  if (!m) { console.log(`FAIL no ${a} markers in the page`); process.exit(1); }
  return m[1];
};
const otaStart = html.indexOf("const OTA = (() => {");
const otaEnd = html.indexOf("})();", html.lastIndexOf("return { Updater, productOf", html.length)) + 5;
const OTA_TEXT = html.slice(otaStart, otaEnd);
const { OTA, INST } = new Function(`"use strict";\n${PORTS_JS}\n${OTA_TEXT}\n${pick("install", "install")}\nreturn { OTA, INST };`)();
check(typeof OTA.Updater === "function" && typeof OTA.productOf === "function", "OTA brings Updater and the package helpers");

// ---------------------------------------------------------------- 2. the releases
const rel = (tag, extra = {}, assets) => ({
  tag_name: tag, name: "fm-fam " + tag, draft: false, prerelease: true, published_at: "2026-11-08T10:27:42Z",
  assets: assets || [{ name: `fm-fam-${tag.slice(1)}.fwsc`, size: 614684, digest: "sha256:" + "ab".repeat(32) }], ...extra,
});
{
  const list = INST.releases([rel("v0.20"), rel("v0.19", { draft: true }), rel("v0.18", {}, [{ name: "notes.txt", size: 5 }]),
    rel("v0.17", {}, [{ name: "../x.fwsc", size: 5 }]), rel("v0.16", {}, [{ name: "fm-fam-0.16.fwsc", size: 1000 }]), rel("v0.15", { prerelease: false })]);
  check(list.map((r) => r.tag).join() === "v0.20,v0.16,v0.15", `releases: newest first, no drafts, no release without a package, no unsafe name (${list.map((r) => r.tag)})`);
  check(list[0].sha === "ab".repeat(32) && list[1].sha === "" && list[0].asset === "fm-fam-0.20.fwsc" && list[0].pre && !list[2].pre,
        "releases: the SHA-256 GitHub gives (none when it gives none), the file, the pre-release mark");
  check(INST.releases(Array.from({ length: 20 }, (_, i) => rel("v1." + i))).length === 8, "releases: at most eight are listed");
  let c = null; try { INST.releases({ message: "Not Found" }); } catch (e) { c = e.code; }
  check(c === "github", "releases: an answer that is not a list is refused");
  check(INST.when("2026-11-08T10:27:42Z") === "8 Nov 2026" && INST.kb(614684) === "600 kB", "the date and the size read as in the list");
}

// ---------------------------------------------------------------- 3. the packages kept in fam/releases
const KEPT = {                                                   // the file attached to the release of the same name
  "fm-fam-0.19.fwsc": "1d6b8a6e984df78a8657ef4f04c7942cdbcd9ed3816bfbd8b2628bad76636c26",
  "fm-fam-0.2.1.fwsc": "e2ff7c525fce930ccfb29db7f205b8279ff789475d8c3491c4c8c711dd80c994",
  "fm-fam-0.2.4.fwsc": "5c63314fcdfd53baf941704cc5cf43281d9de0c42fd4e66996336bed6e9f3a3f",
  "fm-fam-0.20.fwsc": "5cdbe6025e64a75e7204d6e5515d465501e5a6de256929c944298b5abf1aafa2",
};
const dir = path.join(root, "fam/releases");
const files = fs.readdirSync(dir).filter((f) => f.endsWith(".fwsc")).sort();
check(files.join() === Object.keys(KEPT).join(), `fam/releases holds the packages of the releases (${files.join(", ")})`);
const code = async (f) => { try { await f(); } catch (e) { return e.code || e.message; } return null; };
const load = (n) => new Uint8Array(fs.readFileSync(path.join(dir, n)));
for (const [name, sha] of Object.entries(KEPT)) {
  const raw = load(name);
  const p = await INST.prepare(raw, sha);
  check(p.sha === sha && p.product === "FM-1_111" && p.image.length === raw.length - 20, `${name}: FM-1_111, SHA-256 as released, update loader found`);
}

// ---------------------------------------------------------------- 4. what prepare() refuses
{
  const raw = load("fm-fam-0.20.fwsc"), sha = KEPT["fm-fam-0.20.fwsc"];
  check(await code(() => INST.prepare(raw, KEPT["fm-fam-0.19.fwsc"])) === "sha", "another file than the release's: refused (sha)");
  const flip = (i) => { const b = raw.slice(); b[i] ^= 0x55; return b; };
  check(await code(() => INST.prepare(flip(100000), sha)) === "sha", "a flipped byte: refused by the release's SHA-256");
  check((await code(() => INST.prepare(flip(100000), "")))?.length > 0, "a flipped byte with no SHA-256 to check against: refused by the package's own checksums (checkUfw)");
  check((await code(() => INST.prepare(raw.slice(0, 5000), "")))?.length > 0, "a cut file: refused");
  check(await code(() => INST.prepare(new Uint8Array(3000), "")) === "badpkg", "a file that is not a package: refused (badpkg)");
  // a package that says FM-1_111 but has no Felucca loader mark: the marker bytes make the identity, the rest is not a package
  const noLoader = raw.slice();
  const image = OTA.logicalImage(noLoader);
  const at = Buffer.from(image).indexOf("FELUCCA-LOADER-1");
  check(at > 0, "the package carries Felucca's loader mark");
  check(OTA.hasLoader(image) && !OTA.hasLoader(image.map((v, i) => (i >= at && i < at + 16 ? 0 : v))), "hasLoader tells a package with the mark from one without");
  const ok = await INST.prepare(raw, sha);
  check(JSON.stringify(Object.keys(ok).sort()) === '["image","product","sha"]', "prepare gives the product, the image and the SHA-256");
}

// ---------------------------------------------------------------- 5. the bar
{
  const p = await INST.prepare(load("fm-fam-0.20.fwsc"), KEPT["fm-fam-0.20.fwsc"]);
  const bar = INST.barOf(p.image);
  const ent = OTA.ufwEntries(p.image);
  check(ent[0] && ent[0].len > 0x4000 && ent[100], "the package has flash.bin and its update loader (what the bar is measured on)");
  let v = 0, back = false;
  const feed = (x) => { if (x !== null) { if (x < v) back = true; v = x; } };
  const CHECK = 0x400 + 0x400 + ent[100].len - 0x20;
  for (let b = 0; b <= CHECK; b += 2000) feed(bar("verify", b / 512, b));
  const afterCheck = v;
  for (let pct = 1; pct <= 100; pct++) feed(bar("write", 1, 0, pct));
  check(!back && afterCheck > 0 && afterCheck < 50 && v === 99, `the bar only goes forward (the check ends at ${Math.round(afterCheck)}%, the write holds at ${Math.round(v)}%)`);
  check(bar("done") === 100 && bar("start") === null && bar("loader") === null && bar("reboot") === null, "done is 100; the steps without a figure leave the bar");
}

console.log(fails ? `\n${fails} failed` : "\nall ok");
process.exit(fails ? 1 : 0);
