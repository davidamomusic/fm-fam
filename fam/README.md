# FM-FAM

A drum synth and 16-step sequencer firmware for the M-VAVE FM-1, built on [fm1-base](https://github.com/charlesvestal/fm1-base).

- A synth voice with two oscillators (16 types each), hard sync, FM, noise, a ladder or state-variable filter,
  three envelopes and an 8-cable patch bay.
- A drum voice with its own 16-step sequencer: pitch, volume, condition, ratchet and nudge for every step.
- A 16-step sequencer with parameter locks, step conditions, ratchets, swing and nudge; 176 patterns in 11
  banks, a 32-slot song, and a sound of its own saved with every pattern.
- An effects chain (compressor, stutter, delay, reverb, DJ filter, limiter), random pages, 16 scales, presets
  and projects.
- Bluetooth audio and Bluetooth MIDI on fm1-base's own stack (experimental), and screen mirroring over USB.

Everything on the screen is in English. The manual is built into the firmware (HOME pages 7 to 10).

## Install

Install the `.fwsc` package of a release with any FM-1 firmware installer: The Firmware Hub, M-UPGRADE, or
`tools/fm1_install.py` from fm1-base. To leave FM-FAM, install another firmware's package the same way.

M-UPGRADE refuses a package whose identity is the one already on the FM-1 ("The selected firmware matches the current device
version"), and every FM-FAM release has the same identity. To update FM-FAM over FM-FAM use `tools/fm1_install.py` (it compares only
the model), the Install a version section of the screen mirror page (below), or install another firmware first.

## Build

FM-FAM lives in `fam/` of a copy of fm1-base at commit `b5cf814`, with its submodule `bsp` (felucca-bsp) at
`91583ec`. Outside `fam/` these files differ from fm1-base (`git diff b5cf814 -- . ':!fam' ':!fmhub.json'`
lists the changes):

- `bt/br/bt_acl.c`, `bt/br/bt_page.c`, `bt/br/bt_piece.h` (new), `device/bt_own_audio.c`, `include/plat.h`: the
  Bluetooth audio stack honours the slot limit a device announces and sends one-slot packets to devices that
  only accept those;
- `host/plat_host.c`, `tests/bt/bt_sbc_test.c`, `tests/bt/bt_piece_test.c` (new), `tests/bt/run.sh`: the
  simulator's stand-in for the radio and the tests of the above;
- `tools/build.py`: `FM1B_ID` sets the package identity.

The firmware needs the JieLi pi32v2 toolchain and the AC79 SDK, as every fm1-base firmware does (see fm1-base's
README):

    export JIELI_TOOLCHAIN=/path/to/pi32v2-llvm-dropin AC79_SDK=/path/to/ac79-sdk
    fam/release.sh                              # fam/build/fm-fam.fwsc

The host simulator runs the app on a PC, with a simulated Bluetooth radio:

    python3 tools/build.py fam --host
    ./fam/build/host/fm-fam_host fam/tests/basic.txt fam/build/host     # exit 0 and no FAIL lines: it passed
    python3 fam/tests/check_v018b.py fam/build/host                     # after v018b.txt: the audio of a kick step

Every script in `fam/tests` is a test (keys, knobs, screens, saving across a reboot, Bluetooth states, audio).

`fam/docs` has what the main README shows: the screenshots (`fam/tests/readme.txt` takes them in the simulator and
`fam/docs/make_screens.py` turns them into `fam/docs/screens`) and the cheat sheet (`fam/docs/make_cheatsheet.py` builds
the PDF and the web page; it needs Chrome). `fam/docs/FM-FAM-mirror.html` is the screen mirror page that
`tools/make_site.py` fills in for FM-FAM (`tool_pages` on `fam`, from `web/mirror/index.html` and FM-FAM's own part of it, `fam/mirror/extra.html` and
`extra.js`: the Projects and backup section). To make it again: `python3 -c "import sys; sys.path.insert(0, 'tools'); from pathlib import Path; import make_site; sys.stdout.write(make_site.tool_pages(Path('fam'))['mirror/index.html'])" > fam/docs/FM-FAM-mirror.html`.
With GitHub Pages on for the branch, it is a live page at https://davidamomusic.github.io/fm-fam/fam/docs/FM-FAM-mirror.html.

Projects and backup (0.20): `backup = true` in `fam/fm1.toml` turns on fm1-base's backup and restore over USB (`device/backup.c`);
`fam/app.h` lists what a backup holds (the sound, the eleven banks, the five user presets and the four projects; never the panel's
calibration or the Bluetooth keys) and `fam/app.c` has the four hooks (`app_backup_playing`, `_flush`, `_hold`, `_restored`).
A backup file is fm1-base's (`tools/fm1_backup.py` reads and writes the same file: `--magic FAMB`); a project file holds one project's
four parts. The page and its protocol are tested by `node fam/tests/projects_page_test.mjs` (files, slots, a project into another slot,
a simulated FM-1) and `node fam/tests/projects_browser_test.mjs` (the page in headless Chromium on a fake Web MIDI; skipped without
playwright-core and a Chromium); the app's side by `fam/tests/v020a.txt`.

Install a version (0.20): the same page installs a release from GitHub, or a package file, with the installer's own update code
(`web/fm1ota.js` and `fm1pkg.js`, put into the page by `ota_js()` in `tools/make_site.py`); it does not compare identities. A browser
cannot download the file attached to a release (GitHub sends no CORS headers for it), so the page reads the list of releases from the
API and takes the copy of the package kept in `fam/releases/` (the contents API), and writes it only if its SHA-256 is the one GitHub
shows for the release's own file. For every release the package goes into `fam/releases/` under the name of the file attached to the
release (`fam/release.sh` puts it there), in a commit before or after the release. The page is tested by
`node fam/tests/install_page_test.mjs` (the list of releases, the packages kept, what is refused, the bar) and
`node fam/tests/install_browser_test.mjs` (headless Chromium, with GitHub and the update protocol simulated).

Choices and columns (0.2.1): a choice (a wave, a filter type, a delay time, a slice) is kept as 0..127 and cut in equal parts; a click of its
encoder moves to the next part (`choice_step()` in `fam/app.c`), and the lock of a held step does the same. The strip of steps and the columns
of SEQ and of the KICK sequence start at the same x (`col_x0()`). Tested by `fam/tests/v021a.txt` (the choices) and by `fam/tests/v021b.txt` with
`python3 fam/tests/align_check.py DIRECTORY` (the picture, at seven lengths).

Versions have three numbers from 0.2.1 on (`APP_VERSION` in `fam/app.h`; the package is `fm-fam-0.2.1.fwsc` and the tag `v0.2.1`); the
releases before it are 0.19 and 0.20.

## Identity and package

The identity is `FM-1_111` in every release; `fam/release.sh` sets it. The same text is in the package and in the
firmware. The version of the app is `APP_VERSION` in `fam/app.h`, shown on the last four HOME pages.

`fam/release.sh` writes `fam/build/fm-fam.fwsc`, the package of a release: the firmware and fm1-base's own update
loader (felucca-bsp's, built from source in `bsp/loader`). With the same toolchain, a release asset is what this
repository builds at its tag.

## Licence

MPL-2.0 (`LICENSE`, and an SPDX line in every source file). The parts of fm1-base keep the licences listed in
fm1-base's `LICENSING.md`.
