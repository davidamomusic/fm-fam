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

> [!WARNING]
> **FM-FAM is beta software. Use it at your own risk.** It is a community project made by one person, not by M-VAVE. It can have bugs
> and fail: it can behave wrongly, lose what you saved, or leave the FM-1 not starting until you install a firmware again. Installing
> it, and the screen mirror's installer and its restore of projects and backups, all write to the FM-1's flash memory. Before you do,
> save what matters (a backup from the screen mirror), and do not unplug the USB cable or switch the FM-1 off while it writes. To
> leave FM-FAM, install another firmware package with The Firmware Hub or M-UPGRADE.
>
> FM-FAM is provided "as is", without warranty of any kind, as the MPL-2.0 licence says (sections 6 and 7 of the [licence](../LICENSE)).
> The author is not responsible for any damage to your FM-1, your data or anything connected to it, and does not promise support.
> By installing or using FM-FAM you accept this.

Everything on the screen is in English. The controls are on the [cheat sheet](docs/FM-FAM-cheatsheet.pdf); the version of the firmware
is on the last page of `ENV` (VERSION), with the audio's CPU load.

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

## The browser player

`fam/docs/play/` is FM-FAM in a web page: the app itself, compiled to WebAssembly by fm1-base's host simulator (`web/emu`), with the
FM-1's panel drawn around its screen; a MIDI keyboard or a DAW can play it (`emu = ["midi"]` in `fm1.toml`). It is what `fmhub.json`'s
`"play"` points to, served by GitHub Pages; `"editor"` points to the screen mirror and `"cheat"` to the cheat sheet. It has the sound
and the screen of the firmware, not the FM-1's hardware: no Bluetooth, no USB audio, and what it saves stays in the browser.

To make it again after a change, with Emscripten installed: `python3 tools/build.py fam --web`, then copy `index.html`, `app.js`,
`app_worklet.js`, `worklet.js`, `app.wasm` and `parts/*.js` from `fam/build/emu/` to `fam/docs/play/`. Without Emscripten,
`fam/tools/emcc` stands in for it with clang, wasm-ld and a wasi-libc sysroot (`WASI_SYSROOT`; put its folder first in `PATH`): the
committed player was built that way (clang 18, wasi-libc `wasi-sdk-22`). **The player must be rebuilt for every version**; it is tested
by `node fam/tests/play_browser_test.mjs` (headless Chromium: the page loads, switches on and draws the screen).

## How FM-FAM 0.3.0 is driven

Buttons. `SEQ` the steps, `EDIT` the synth, `LFO` the patch bay, `ARP` the kick, `FX` the effects, `GLO` the patterns, `SEL` the scale,
`REC` the RANDOM pages, `SAVE` the projects and presets, `ENV` the settings. A tap goes to the mode, a further tap to its next page.
`HOME` does nothing alone. There is no save key: the autosave writes a moment after the last change, with the sequencer stopped and
nothing sounding (a flash write stops the sound for an instant), and the SAVE actions of the projects and presets write at once.

Key pairs. Each acts at once, when the second button goes down; no hold does anything.

| Keys | What they do |
|---|---|
| `OCT-` + `REC` | a random of the page you are on (`random_run(0)`) |
| `OCT+` + `REC` | a random of everything: the steps, the synth, the kick (`random_run(2)`) |
| `OCT-` + `GLO` | the page is initialised (`page_init()`) |
| `OCT+` + `GLO` | everything is initialised: the pattern you are in, its steps and its sound (`pattern_init()`) |
| `OCT-` + `HOME` | a random of everything, for melodies (`random_melody()`) |
| `OCT+` + `HOME` | a random of everything, for low and mid percussion (`random_perc()`) |
| `OCT-` + `SEQ` | a random of everything, softer, with the effects low (`random_flavour(RF_SOFT)`) |
| `OCT+` + `SEQ` | a random of everything, its hits spread evenly (`random_flavour(RF_EUCLID)`) |

- The random of everything draws the steps, the synth and the kick, and leaves the cables, the effects, the scale and the tempo. It
  draws the resonance up to 85 and the kick's level from 100 to 110, as 0.2.3 did; key 7 of a RANDOM page is the same random with the
  resonance at 50 and the kick's level at 100 or under (`random_run(1)`). A random draws the kick's click from 0 to 9 and the reverb's
  width from 0 to 20. Each RANDOM page says whether the random of everything draws it and what each knob draws (a range, `ANY` for a
  choice, `AUTO` for a draw of its own).
- The melody random: a voice that holds a pitch, the cables off, one of six scales with the quantizer on (the root stays), a phrase
  built from a four-step figure, and a kick on every beat; the kick's sound, the effects and the tempo stay.
- The percussion random: the steps and the kick as the random of everything draws them, and a synth that is an electronic tom or a
  low electronic drum: a sine or a triangle (now and then a square or FM) at a low pitch that falls and takes its time, no attack,
  a decay that rings a while, a low-pass, half the time a second oscillator for body, no hat sounds.
- Three more randoms of everything (0.3.0), each with steps of its own on top of what the random pages draw (`random_flavour()`,
  `pat_flavour()`). `OCT+` + `SEQ`: the sound as `OCT+` + `REC` draws it; the trigs are a number of hits spread evenly over the
  length and turned by a few steps, the pitches a four-step figure said again with a note changed each time, and the kick's line is
  spread the same way. `OCT-` + `SEQ`: the resonance at 50 and the kick's level at 100 or under; the second half of the steps
  answers the first; and the effects are drawn too, but low (the reverb's width under 12, the mixes, the feedback and the drive
  small, the DJ filter open, the stutter off). RAND in PATTERNS, on the pattern that plays: the sound as `OCT+` + `REC` draws it,
  and steps that keep the beat (every fourth step plays, the others rest one time in three); on another pattern, its steps only.
- First power-on (0.3.0): the reverb's width is 10 (the first FX preset, DEFAULT, which the inits use too). What a unit has saved
  is not touched: the autosave is what it starts with from then on.
- Mutes (0.2.9). With `OCT+` held, on any page, the last white key mutes the kick and the one before it the synth; the same keys
  switch them back on. A red S or K in the header says which is muted. A muted voice takes no hit from its sequence nor from MIDI,
  and sends no note. Nothing is kept: both sound again after a power-off.
- The FX mixer's last black key (0.2.9) switches delay, reverb, DJ filter and stutter off together, and back on as they were
  (`fx_four()`); the last white key does nothing there now.
- The init of a page. A page of sound parameters (SYNTH, the kick's sound pages, the effects): its four knobs take the values of the
  first factory preset of their kind. The patch bay: every cable off. A step page of SEQ: what its knobs set goes back to a new
  pattern's on every step, and the line is left empty (page 1 pitch, velocity, gate, ratchet and every trig; page 2 condition, trig,
  length and swing). The kick's line: empty. A SCALE page: its first power-on values. The tempo page, `GLO`, `SAVE` and `ENV` have none.
- The init of everything: the pattern you are in becomes a new one (its steps, the kick's line, the lengths, the swing) and its sound
  the first power-on one (the synth, the kick, the effects, the cables, the scale). The tempo, the other patterns, the song and the
  presets stay.

Nudge. With `OCT-` held in the step pages of SEQ and in the kick's two sequence pages, K3 is NUDGE ALL (the synth's sequence and the
kick's move together, each inside its own length) and K4 the nudge of that page's own sequence. In those pages `OCT-` does not make
the knobs coarse.

The settings (`ENV`, five pages):

1. AUDIO. K1 the USB recording on or off, K2 the speaker and the headphone jack on or off, K3 the USB level (MASTER's, or full). The
   speaker and the jack are one output (one converter; a plug switches the speaker off), so they go together. `plat_out_off()` is
   FM-FAM's addition to the platform (`include/plat.h`, `device/plat_fm1.c`, `device/main_fm1.c`, `host/plat_host.c`).
2. MIDI, clock and sync. K1 the clock in: the MIDI clock from USB, the MIDI jack or Bluetooth plays the steps, a 16th every six
   clocks, and its start, continue and stop run the sequencer; the header shows `EXT` and the clock's tempo. MIDI in is stamped where
   it lands (`midi_stamp = true` in `fm1.toml`) and a step sounds 288 samples (6.5 ms) after its clock, the same for every step; a
   clock over Bluetooth has no stamp and can be up to a block (5.8 ms) off; half a second without a clock and the FM-1's own tempo is
   back (`sync_rt()`, `sync_at()`). K2 the clock out: 24 clocks a quarter note, start and stop, to USB and Bluetooth, while the
   sequencer runs on its own tempo; the render counts them in samples, so they cannot drift from the steps, and the 1 kHz tick sends
   each at its time (`app_tick`, `clock_send()`). K3 the pulse sync out, for gear that takes sync pulses on a jack: 1, 2, 4 or 24
   pulses a quarter note, 5 ms each, on the left of the FM-1's own output (the tip of the headphone jack), with the sound in mono on
   the right; `app_line_out()` writes it, a hook FM-FAM adds to the platform after the USB recording has taken the block
   (`APP_LINE_OUT` in `device/main_fm1.c` and `host/plat_host.c`), so the recording stays clean.
3. MIDI, channels. K1 the channel notes come in on (all, or one; note 36 is the kick), K2 the channel the synth's steps go out on as
   notes, K3 the channel the kick's steps go out on as note 36. Out is off at first.
4. BLUETOOTH (its header in green): headphones and speakers, and Bluetooth MIDI.
5. VERSION: the firmware's version and the audio's CPU load.

What the FM-1 cannot do: its MIDI jack is an input only, so notes and clock go out by USB and Bluetooth; and it has no analog input,
so it cannot take sync pulses in. It follows MIDI clock. The settings of the AUDIO and MIDI pages are kept with Bluetooth's, in an
object that is in no backup, and survive a power-off (`btst_t`; it grew by eight bytes in 0.2.7, and a record of an older version
loads as it is).

Tests of these: `fam/tests/v022a.txt` (the clock in, the AUDIO page), `v024a.txt` (the random's caps, the RANDOM pages, the melody),
`v026a.txt` (the reverb's width, the init of a page, percussion), `v027a.txt` (ENV's pages, the clock out, the pulses, the channels),
`v028a.txt` (the key pairs, the nudges), `v029a.txt` (the mutes, the toms) and `v030a.txt` (the randoms of SEQ, RAND in PATTERNS).

Versions: 0.2.2 the clock in, 0.2.3 the AUDIO page and the stamped clock, 0.2.4 the RANDOM pages' labels, the melody random and the
VERSION page, 0.2.5 and 0.2.6 the randoms' keys, the init of a page and the percussion random, 0.2.7 the clock out, the pulse sync,
the notes out and the settings on `ENV`, 0.2.8 the six key pairs with no holds and NUDGE ALL, 0.2.9 the mutes, the mixer's black key and the percussion random as electronic toms, 0.3.0 the randoms of `OCT-` + `SEQ` and `OCT+` + `SEQ`, RAND in PATTERNS and the first power-on reverb.

Versions have three numbers from 0.2.1 on (`APP_VERSION` in `fam/app.h`; a package is `fm-fam-0.3.0.fwsc` and its tag `v0.3.0`); the
releases before it are 0.19 and 0.20.

## Identity and package

The identity is `FM-1_111` in every release; `fam/release.sh` sets it. The same text is in the package and in the
firmware. The version of the app is `APP_VERSION` in `fam/app.h`, shown on the VERSION page (`ENV`, the last page).

`fam/release.sh` writes `fam/build/fm-fam.fwsc`, the package of a release: the firmware and fm1-base's own update
loader (felucca-bsp's, built from source in `bsp/loader`). With the same toolchain, a release asset is what this
repository builds at its tag.

## Licence

MPL-2.0 (`LICENSE`, and an SPDX line in every source file). The parts of fm1-base keep the licences listed in
fm1-base's `LICENSING.md`.

The licence says the software is provided "as is", without warranty (section 6), and limits the liability of its authors (section 7).
FM-FAM is beta software: see the notice at the top of this page.
