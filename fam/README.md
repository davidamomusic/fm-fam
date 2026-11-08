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
the PDF and the web page; it needs Chrome).

## Identity and package

The identity is `FM-1_111` in every release; `fam/release.sh` sets it. The same text is in the package and in the
firmware. The version of the app is `APP_VERSION` in `fam/app.h`, shown on the last four HOME pages.

`fam/release.sh` writes `fam/build/fm-fam.fwsc`, the package of a release: the firmware and fm1-base's own update
loader (felucca-bsp's, built from source in `bsp/loader`). With the same toolchain, a release asset is what this
repository builds at its tag.

## Licence

MPL-2.0 (`LICENSE`, and an SPDX line in every source file). The parts of fm1-base keep the licences listed in
fm1-base's `LICENSING.md`.
