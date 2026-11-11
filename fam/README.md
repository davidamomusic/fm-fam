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

What 0.2.8 changed (the key combinations below are the ones that count; older notes further down name older ones):

| Keys | What they do |
|---|---|
| `OCT-` + `REC` | a random of the page you are on (`random_run(0)`) |
| `OCT+` + `REC` | a random of everything: the steps, the synth, the kick, as 0.2.3 drew it (`random_run(2)`) |
| `OCT-` + `GLO` | the page is initialised (`page_init()`) |
| `OCT+` + `GLO` | the pattern is initialised, its steps and its sound (`pattern_init()`) |
| `OCT-` + `HOME` | a random of everything, for melodies (`random_melody()`) |
| `OCT+` + `HOME` | a random of everything, for low and mid percussion (`random_perc()`) |

Each acts at once, when the second button goes down: no hold does anything any more. The general random with the resonance at 50 and
the kick's level at 100 or under (`random_run(1)`) is on no key now. With `OCT-` held in SEQ and in the kick's two sequence pages, K3
is NUDGE ALL (the synth's sequence and the kick's move together, each inside its own length) and K4 the nudge of that page's own
sequence; the second page of the kick's sequence has no nudge knob without `OCT-`, and in those pages `OCT-` no longer makes the knobs
coarse. Test: `fam/tests/v028a.txt`.

What 0.2.7 added (it moves some of what follows: read this first):

- `ENV` opens the settings, five pages in this order: AUDIO, MIDI (clock and sync), MIDI (channels), BLUETOOTH, VERSION. `GLO` has
  its four pages of patterns again.
- MIDI clock out (MIDI page 1, K2): 24 clocks a quarter note, start and stop, to USB and Bluetooth, while the sequencer runs on its
  own tempo. The render counts the clocks in samples, so they cannot drift from the steps, and hands each with its time to the 1 kHz
  tick (`app_tick`, `clock_send()`), which sends it; following a clock that comes in, none goes out.
- Pulse sync out (MIDI page 1, K3), for gear that takes sync pulses on a jack: 1, 2, 4 or 24 pulses a quarter note, 5 ms each, on the
  left channel of the FM-1's own output (the tip of the headphone jack), with the sound in mono on the right. `app_line_out()` writes
  it, a hook FM-FAM adds to the platform after the USB recording has taken the block (`APP_LINE_OUT` in `device/main_fm1.c` and
  `host/plat_host.c`), so the recording stays clean. With a clock coming in, the pulses follow that clock.
- The FM-1 has no way to take sync pulses in: its MIDI jack is an input for MIDI only and there is no analog input. It follows MIDI clock.
- Channels (MIDI page 2): K1 the channel notes come in on (all, or one), K2 the channel the synth's steps go out on as notes, K3 the
  channel the kick's steps go out on as note 36; out is off at first. The FM-1's MIDI jack is an input, so notes and clock go out by
  USB and Bluetooth.
- The settings live in `btst_t.set` (the Bluetooth object grew by eight bytes; a record of an older version loads as it is, with the
  MIDI settings at their first values). Test: `fam/tests/v027a.txt`.

What 0.2.2 to 0.2.6 added:

- Buttons. `SAVE` opens the projects and the presets (six pages; the mode is called SAVE). `HOME` and `ENV` do nothing alone;
  `OCT-` + `HOME` is the melody random and `OCT+` + `HOME` the general random. There is no save key: the autosave writes a moment after the last change, with the sequencer
  stopped and nothing sounding (a flash write stops the sound for an instant), and the SAVE actions of the projects and presets write at once.
- `GLO` has eight pages. After the four of the patterns: BLUETOOTH (its header in green), MIDI, AUDIO and VERSION.
- MIDI sync (`GLO` page 6, K1; off at first). With it on, the MIDI clock that comes in from USB, the MIDI jack or Bluetooth plays the
  steps, a 16th every six clocks, and its start, continue and stop run the sequencer; the header shows `EXT` and the clock's tempo.
  MIDI in is stamped where it lands (`midi_stamp = true` in `fm1.toml`) and a step sounds 288 samples (6.5 ms) after its clock,
  the same for every step; a clock over Bluetooth has no stamp and can be up to a block (5.8 ms) off. Half a second without a clock
  and the FM-1's own tempo is back. FM-FAM does not send a clock. (`sync_rt()`, `sync_at()` in `fam/app.c`.)
- Audio outputs (`GLO` page 7): K1 the USB recording on or off, K2 the speaker and the headphone jack on or off, K3 the USB level
  (MASTER's, or full). The speaker and the jack are one output (one converter; a plug switches the speaker off), so they go together.
  `plat_out_off()` is FM-FAM's addition to the platform (`include/plat.h`, `device/plat_fm1.c`, `device/main_fm1.c`,
  `host/plat_host.c`). The settings are kept with Bluetooth's and survive a power-off.
- Random. A random draws the kick's click from 0 to 9, the resonance from 0 to 50 and the kick's level from 90 to 100, and the general
  random of `OCT+` + `HOME` (0.2.5; `random_run(1)`) keeps the last two at 50 and 100 or under. `OCT+` + `REC` (a tap) is the general
  random as 0.2.3 drew it (`random_run(2)`): the resonance up to 85 and the kick's level from 100 to 110. Each RANDOM page says whether the general random draws it (the
  steps, the synth and the kick: yes; the patch bay, the effects, the scale and the tempo: no) and what each knob draws.
  `OCT-` + `HOME` (`random_melody()`): a voice that holds a pitch, the cables off, one of six scales with the quantizer on (the root
  stays), a phrase built from a four-step figure, and a kick on every beat; the kick's sound, the effects and the tempo stay.
- 0.2.6. A random draws the reverb's width from 0 to 20 and never leaves it over 20. `OCT-` + `REC` held 0.7 s initialises the page (`page_init()`); a tap is still the random of the page. On a page of sound
  parameters (SYNTH, the kick's sound pages, the effects) the four knobs take the values of the first factory preset of their kind; in
  the patch bay every cable goes off; on a step page of SEQ what its knobs set goes back to a new pattern's on every step (page 1 pitch,
  velocity, gate and ratchet, and every trig off; page 2 condition, trig, length and swing: both leave the line empty); the kick's line becomes empty; a
  SCALE page takes its first power-on values. The tempo page, `GLO` and `SAVE` have no init.
  `OCT+` + `HOME` held 0.7 s is a percussion random (`random_perc()`): the steps and the kick as the general random draws them, and a
  synth that is a drum between the mids and the lows (a pitch that falls, no attack, a short decay, no hat sounds).
- Tests: `fam/tests/v026a.txt`, `fam/tests/v022a.txt` (the sync, the AUDIO page, the buttons) and `fam/tests/v024a.txt` (the random's caps, the pages, the melody).

Versions have three numbers from 0.2.1 on (`APP_VERSION` in `fam/app.h`; a package is `fm-fam-0.2.8.fwsc` and its tag `v0.2.8`); the
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
