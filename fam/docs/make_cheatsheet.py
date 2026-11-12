#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# Builds the FM-FAM cheat sheet: one self-contained HTML page (dark on screen, light in print) and its A4 PDF
# (Chrome prints it). cheatsheet-1.png and cheatsheet-2.png, the previews in README.md, are its two pages at 100 dpi.
#   python3 fam/docs/make_cheatsheet.py fam/card.png fam/docs
# Change VERSION and the text below with every release; the controls are those of fam/app.c.
import base64, html, os, subprocess, sys

card_png, out_dir = sys.argv[1], sys.argv[2]
os.makedirs(out_dir, exist_ok=True)

VERSION = "0.3.1"
DESCRIPTION = ("DFAM style 16-step sequencer and a drum synth for the FM-1. It has two oscillators with 16 wave "
               "types, a kick with its own sequencer, parameter locks, a sound saved per pattern, an effects "
               "chain, random pages, scales, presets and projects. Bluetooth audio is experimental.")
REPO = "https://github.com/davidamomusic/fm-fam"
BETA = ("Use it at your own risk. It can have bugs and fail (wrong behaviour, lost data, an FM-1 "
        "that does not start until you install a firmware again), and installing it or restoring a backup writes to the "
        "FM-1's flash. Save what matters first and never unplug the USB cable while it writes. It is provided \u201cas is\u201d, "
        "without warranty (MPL-2.0, sections 6 and 7); the author is not responsible for any damage to your FM-1, your data "
        "or anything connected to it.")
RELEASE = REPO + "/releases/tag/v" + VERSION

# (section title, [(controls, description), ...]); `x` in controls marks a button, key or knob.
SECTIONS = [
    ("Getting around", [
        ("`SEQ` `EDIT` `LFO` `ARP` `FX` `GLO` `SCL` `SAVE` `ENV`",
         "Mode buttons: sequencer, synth, patch bay, kick, effects, patterns, scale, projects and presets, "
         "settings. The first tap goes to the mode, on the page you left; each further tap moves to the next page and "
         "wraps after the last. `HOME` does nothing alone."),
        ("`SELECT` turn", "Change page inside the mode (does not wrap)."),
        ("`ALGORITHM` turn",
         "Move the cursor of the page: the step (SEQ pages 1-2, kick sequence), the target (patch bay), the bank or "
         "slot (patterns), the item (SAVE). Everywhere else it changes the tempo."),
        ("`PRESETS` turn",
         "Next or previous preset of the mode (synth, kick, FX, patch bay, scale). In patterns it picks the pattern."),
        ("`KNOB 1`-`KNOB 4`",
         "The four parameters named at the bottom of the screen: green, cyan, magenta and orange."),
        ("`OCT-` + `KNOB`",
         "Coarse: 8 steps per click on 0-127 knobs, x4 on pitch, velocity, gate, kick pitch and volume, random and "
         "patch-bay amounts. Small selectors stay as they are. In SEQ and the kick's sequence pages it is the nudges."),
        ("`PLAY`", "Start or stop. Hold 0.7 s to stop and go back to step 1."),
    ]),
    ("Playing", [
        ("`STEP KEY` tap",
         "The 16 white keys are the steps in SEQ, SYNTH, patch bay and kick pages. Each tap cycles OFF, TRIG, ACCENT."),
        ("`STEP KEY` hold + `KNOB`",
         "Edit that step. In SYNTH pages it sets a parameter lock for the step (the value turns yellow, LOCK). On kick "
         "pages it edits the kick sequence."),
        ("`STEP KEY` hold + `REC`",
         "Clear the step (SEQ or kick sequence). A kick step also goes back to pitch 0 and volume 100%."),
        ("MIDI note in",
         "By USB, or by Bluetooth with the BLUETOOTH page on. A note plays the synth; note 12 (C0) triggers the kick as it "
         "is, without step pitch or volume."),
    ]),
    ("Sequencer", [
        ("`SEQ` page 1",
         "K1 pitch, K2 velocity, K3 gate (1-32 thirty-seconds of a step, or FULL), K4 ratchet (X1-X4)."),
        ("`SEQ` page 2",
         "K1 condition (1:1, 1:2, 2:2, 1:3 ... 4:4, RND), K2 trig, K3 pattern length (1-16), K4 swing."),
        ("`SEQ` page 3 TEMPO", "K1 BPM ±1 (it accelerates), K2 BPM ±10. The BPM is shown large."),
        ("`OCT-` hold + `KNOB 4`",
         "NUDGE: shifts the whole sequence one step per click, wrapping inside the pattern length. Trig, pitch, "
         "velocity, condition, gate, ratchet and locks travel with their steps; the kick sequence stays. The counter "
         "shows the shift since you pressed `OCT-`."),
        ("`OCT-` hold + `KNOB 3`",
         "NUDGE ALL: the same, with the kick's sequence moving too (each inside its own length)."),
    ]),
    ("Kick", [
        ("`ARP` page 1 KICK SEQUENCE 1",
         "A sequence of its own, up to 16 steps, apart from the synth pattern. K1 trig (off, trig, accent), K2 pitch "
         "(-24 to +24 semitones), K3 volume (0-100%), K4 length (1-16). Step bars show the volume, accents in yellow."),
        ("`ARP` page 2 KICK SEQUENCE 2",
         "K1 condition, K2 ratchet (X1-X4)."),
        ("`OCT-` hold, on both pages", "K3 NUDGE ALL (the kick's sequence and the synth's), K4 NUDGE KICK (the kick's alone)."),
        ("`ARP` pages 3-5",
         "3: pitch and decay. 4: tone (click, drive, level, wave). 5: FM and noise. 9 presets."),
        ("Which step",
         "The one you hold with a white key or, if none, the one `ALGORITHM` points to."),
    ]),
    ("Sound", [
        ("`EDIT` (SYNTH), 6 pages",
         "Two oscillators with 16 types each (TRI SAW SQR PLS SIN PWM SSAW FM ORGAN VOWEL BRASS FOLD DIGI METAL NOISE "
         "BELL); filter and noise (LP4, LP2, BP, HP); envelopes; mixer (levels, sync, volume); "
         "modulation (envelope to VCO 1, VCO 2 and filter, accent); output (attack, drive, glide, tune). 16 presets."),
        ("Switches (SYNC, SLOPE ...)", "A switch changes at the first click: right is ON, left is OFF."),
        ("Choices (waves, filter type, delay time, slices)", "Each click moves to the next choice."),
        ("`LFO` (patch bay)",
         "Eight virtual cables: source to target, depth -100 to +100, connected or not. Step keys work here too. "
         "7 presets."),
    ]),
    ("Effects", [
        ("`FX`, 7 pages",
         "COMPRESSOR, DELAY (ping-pong), REVERB (with width), STUTTER, DJ FILTER (high-pass with resonance, 12 or 24 "
         "dB), LIMITER and FX MIXER. 10 presets."),
        ("Order", "Compressor, stutter, delay, reverb, DJ filter, limiter."),
        ("`KEY 1`-`KEY 6`",
         "White keys 1-6 on the effect pages: compressor, delay, reverb, stutter, DJ filter, limiter on or off."),
        ("FX MIXER",
         "K1 delay mix, K2 reverb mix, K3 DJ filter cutoff, K4 stutter slice (1/8 to 1/128). White keys: 1 delay, 2 reverb, "
         "3 DJ filter, 4 stutter, 5 compressor, 6 limiter. The last black key turns delay, reverb, DJ filter and "
         "stutter off together, and back on as they were."),
        ("Stutter key", "Tap to latch it. Hold 350 ms or more to stutter only while it is pressed."),
    ]),
    ("Random", [
        ("`REC`",
         "Opens the RANDOM page of the page you are on (23 pages); press again to go back. Each knob has an ON/OFF "
         "switch and an amount (0-100%)."),
        ("What a page says",
         "IN or NOT IN `OCT+` + `REC`: whether RANDOM ALL draws this page. Under each bar, what that knob draws on "
         "a random of its page: a range (0-50), ANY for a choice, AUTO for a draw of its own."),
        ("RANDOM keys",
         "White keys 1-4 switch a knob on or off, 5 all of them, 6 randomize this page, 7 RANDOM ALL."),
        ("`OCT-` + `REC`",
         "Random of this page: whatever is switched on there (patch bay, FX and scale included). The reverb's "
         "width stays at 20 or under."),
        ("`OCT+` + `REC`",
         "RANDOM ALL: steps, synth sound and kick in one go. Cables, FX, scale and BPM stay."),
        ("`OCT-` + `HOME`",
         "RANDOM ALL for melodies: a voice that holds a pitch (no noise, sync or cables), one of six scales with "
         "the quantizer on (the root stays), a phrase built from a four-step figure and a kick on every beat."),
        ("`OCT+` + `HOME`",
         "RANDOM ALL for electronic toms and low percussion: steps and kick as RANDOM ALL, and a synth that is a "
         "tom (a sine or triangle at a low pitch that falls, no attack, a decay that rings)."),
        ("`OCT-` + `SAVE` (in SEQ)",
         "MIDI REC on or off (red dot). MIDI notes go into the steps as plain trigs: running, the nearest step; "
         "stopped, the selected one, then the next. Note 12 (C0, kick) is not recorded."),
        ("`OCT-` + `SEQ`",
         "RANDOM ALL, softer: the second half of the steps answers the first, and the FX are drawn low (reverb "
         "width under 12, small mixes, stutter off)."),
        ("`OCT+` + `SEQ`",
         "RANDOM ALL with the hits spread evenly over the steps, a four-step figure of pitches and a kick line "
         "spread the same way."),
        ("`OCT+` + last white keys",
         "On any page: the last white key mutes the kick, the one before it the synth; again, back on. A red S or "
         "K in the header says which is muted."),
        ("`OCT-` + `GLO`",
         "INIT PAGE. SYNTH, kick sound and FX pages: the knobs go back to the first factory preset. Patch bay: every "
         "cable off. SEQ step pages: as in a new pattern, on every step, and no trigs. Kick line: empty. SCALE "
         "pages: as at first power-on."),
        ("`OCT+` + `GLO`",
         "INIT ALL: resets the current pattern, its steps and its sound. Tempo, other patterns, song and presets "
         "stay."),
        ("No holds", "Every one of these acts at once, when the second button goes down."),
    ]),
    ("Patterns & song", [
        ("`GLO` page 1",
         "Pattern grid: black keys are the banks A-K (11), white keys the patterns 1-16 of a bank (176 in all). While "
         "playing, the chosen pattern starts at the next bar. Actions: PLAY, COPY, PASTE, CLEAR, RAND."),
        ("`GLO` page 2 SONG", "32 slots."),
        ("`GLO` pages 3-4 PATTERN SOUND",
         "Every pattern keeps its own sound (synth, kick, FX, patch bay, scale) and loads it when it plays. Page 3 shows "
         "the presets it came from (EDITED if changed). Page 4: scale preset, TO BANK and TO ALL (copy this sound to the "
         "bank or to all 176 patterns), RESET."),
    ]),
    ("Scale", [
        ("`SCL`, 2 pages",
         "Root, scale (16: chromatic, major, minor, dorian, phrygian, lydian, mixolydian, locrian, harmonic and melodic "
         "minor, pentatonic major and minor, blues, whole tone, diminished, hirajoshi), octave, quantize on/off; "
         "transpose, fine tune, humanize, spread. Any key sets the root. Everything but the kick is quantized. "
         "13 presets."),
    ]),
    ("Saving & loading", [
        ("`SAVE` page 1 PROJECTS",
         "4 slots (white keys 1-4). Pages 2-6: presets of SYNTH, KICK, FX, PATCH and SCALE (32 user slots each)."),
        ("`SAVE` knobs",
         "K1 item, K2 action (LOAD, SAVE, RENAME, CLEAR), K3 name cursor, K4 turn right to run it, or change the "
         "letter."),
        ("A project", "Keeps the whole sound and banks A-C (48 patterns). Loading it replaces them."),
        ("Projects and backup",
         "On the screen mirror page (Chrome or Edge, USB): Read the FM-1, then save a project to a file or replace one "
         "with a file, or save and load everything (sound, patterns, presets, projects). Stop the sequencer first; the "
         "FM-1 asks `SEL` to confirm, `HOME` to cancel."),
        ("Autosave",
         "There is no save key. 1.5 s after your last change, with the sequencer stopped and nothing sounding; only "
         "what changed. While playing nothing is written (a flash write silences the audio for a moment). The SAVE "
         "action of a project or preset writes at once."),
    ]),
    ("Settings (ENV), 5 pages", [
        ("Page 1 AUDIO",
         "K1 USB audio ON/OFF (off: the computer records silence). K2 speaker + jack ON/OFF: one output, a plug "
         "switches the speaker off. K3 USB level: MASTER's, or FULL. Kept when you switch off: no sound? look here."),
        ("Page 2 MIDI: clock and sync",
         "K1 CLOCK IN: the MIDI clock from USB, the MIDI jack or Bluetooth plays the steps; its start and stop run "
         "the sequencer (header: EXT). K2 CLOCK OUT: clock, start, stop to USB and Bluetooth. K3 PULSE OUT, for "
         "gear with a sync input: 1, 2, 4 or 24 a quarter note; on the jack, pulses left (tip), sound right (ring)."),
        ("Page 3 MIDI: channels",
         "K1 IN: OFF, ALL or 1-16 (note 12, C0, is the kick). K2 OUT: OFF or 1-16, the synth's steps as notes. K3 KICK: OFF or "
         "1-16, its steps as note 12. Out to USB and Bluetooth."),
        ("Pages 4 and 5", "BLUETOOTH (below) and VERSION: the firmware's version (" + VERSION + ") and the CPU load. "
         "The MIDI jack is an input only and there is no pulse input: the FM-1 follows MIDI clock, not pulses."),
    ]),
    ("Bluetooth (experimental)", [
        ("`ENV` page 4",
         "The BLUETOOTH page. K1 ON/OFF (right is ON). K2, `ALGORITHM` or `PRESETS` scroll the devices (KEPT are "
         "remembered, NEW were found). K3 picks the action: SCAN, CONNECT, HANG UP, FORGET, OUTPUT, QUALITY, BT MIDI. "
         "K4 turned right runs it."),
        ("Pair headphones",
         "Put them in pairing mode, K1 ON, SCAN, pick one with K2, CONNECT. Next time, CONNECT on the KEPT device "
         "(four are remembered)."),
        ("OUTPUT", "While linked, the sound goes to Bluetooth and the cable output is muted. OUTPUT switches it back."),
        ("QUALITY", "HIGH, MID or LOW with each turn of K4. A read-out shows slots, bitpool and drops."),
        ("BT MIDI",
         "Switches the Bluetooth MIDI announcement on or off (on by default). Notes sound as they do by USB."),
        ("Limits",
         "The sound arrives 150-300 ms late: for listening, not for playing live. A device that accepts only one-slot "
         "packets sounds worse. After changing firmware, forget the FM-1 on the phone or speaker and pair again. "
         "Bluetooth is off at power-on unless you left it on."),
    ]),
    ("Good to know", [
        ("Random character",
         "Each draw picks one: mostly low sounds, 15% of the time bright hi-hat sounds. Pitched steps use a minor "
         "pentatonic."),
        ("Screen mirror", "Mirrors the screen and panel over USB (Chrome or Edge). The same page saves projects and backups, and installs versions."),
        ("Leave FM-FAM",
         "Install any other firmware package with The Firmware Hub or M-UPGRADE. The package identity is FM-1_111."),
    ]),
]


def md(text):
    out, parts = [], text.split("`")
    for i, part in enumerate(parts):
        part = html.escape(part)
        out.append("<code>%s</code>" % part if i % 2 else part)
    return "".join(out)


def section(title, rows):
    body = "".join('<div class="e"><div class="k">%s</div><div class="d">%s</div></div>' % (md(k), md(d))
                   for k, d in rows)
    return '<section><h2>%s</h2>%s</section>' % (html.escape(title), body)


with open(card_png, "rb") as f:
    card = base64.b64encode(f.read()).decode()

PAGE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>FM-FAM {version} cheat sheet</title>
<meta name="description" content="{desc_attr}">
<style>
:root {{ --bg:#151213; --panel:#1e1a1c; --line:#35302f; --text:#ece7e8; --dim:#a9a2a4; --acc:#4ADE80; --code:#26211f; }}
* {{ box-sizing:border-box; }}
body {{ margin:0; background:var(--bg); color:var(--text);
  font:15px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Inter,Roboto,Helvetica,Arial,sans-serif; }}
main {{ max-width:1080px; margin:0 auto; padding:28px 20px 40px; }}
.hd {{ display:flex; gap:22px; align-items:flex-start; margin-bottom:22px; }}
.hd img {{ width:168px; height:168px; border-radius:10px; border:1px solid var(--line); image-rendering:pixelated; flex:none; }}
.kick {{ color:var(--acc); font-size:12px; letter-spacing:.12em; text-transform:uppercase; font-weight:600; }}
h1 {{ margin:4px 0 2px; font-size:32px; line-height:1.15; }}
.ver {{ color:var(--dim); margin:0 0 10px; }}
.sum {{ margin:0 0 14px; max-width:60ch; }}
.btns a {{ display:inline-block; margin:0 8px 6px 0; padding:7px 14px; border-radius:7px; border:1px solid var(--acc);
  color:var(--acc); text-decoration:none; font-weight:600; font-size:14px; }}
.btns a.solid {{ background:var(--acc); color:#06210f; }}
.legend {{ color:var(--dim); font-size:13px; margin:0 0 18px; }}
.beta {{ margin:0 0 16px; padding:9px 14px; border:1px solid var(--line); border-left:5px solid var(--acc); border-radius:8px; background:var(--panel); font-size:13.5px; }}
.beta b {{ color:var(--acc); }}
.cols {{ columns:2 480px; column-gap:26px; }}
section {{ break-inside:avoid; margin:0 0 18px; background:var(--panel); border:1px solid var(--line); border-radius:10px; padding:4px 14px 8px; }}
h2 {{ font-size:15px; margin:10px 0 6px; color:var(--acc); letter-spacing:.02em; }}
.e {{ display:grid; grid-template-columns:38% 1fr; gap:4px 12px; padding:6px 0; border-top:1px solid var(--line); }}
.e:first-of-type {{ border-top:0; }}
.k {{ font-size:13px; }} .d {{ font-size:13.5px; }}
code {{ font:600 11.5px/1.2 ui-monospace,SFMono-Regular,Menlo,Consolas,monospace; background:var(--code);
  border:1px solid var(--line); border-radius:4px; padding:1px 5px; white-space:nowrap; color:var(--text); }}
footer {{ margin-top:10px; color:var(--dim); font-size:12.5px; border-top:1px solid var(--line); padding-top:12px; }}
footer a {{ color:var(--acc); }}
@page {{ size:A4; margin:9mm 9mm 9mm 9mm; }}
@media print {{
  :root {{ --bg:#fff; --panel:#fff; --line:#cfd4d1; --text:#161616; --dim:#555; --acc:#117a3c; --code:#f1f3f2; }}
  * {{ -webkit-print-color-adjust:exact; print-color-adjust:exact; }}
  body {{ font-size:7.4pt; line-height:1.2; }}
  main {{ max-width:none; padding:0; }}
  .hd {{ gap:12px; margin-bottom:5px; }} .hd img {{ width:27mm; height:27mm; border-radius:4px; }}
  h1 {{ font-size:17pt; margin:2px 0 1px; }} .ver {{ margin-bottom:4px; }} .sum {{ max-width:none; margin-bottom:4px; }}
  .btns {{ display:none; }} .legend {{ margin-bottom:5px; font-size:7.4pt; }}
  .beta {{ margin-bottom:2.2mm; padding:1mm 2.6mm; font-size:7.4pt; line-height:1.25; border-radius:3px; }}
  .cols {{ columns:2; column-gap:4.5mm; }}
  section {{ margin:0 0 1.8mm; padding:0.4mm 2.6mm 0.8mm; border-radius:4px; }}
  h2 {{ font-size:9.4pt; margin:1.4mm 0 0.6mm; }}
  .e {{ padding:0.55mm 0; grid-template-columns:30% 1fr; gap:1mm 2.4mm; }}
  .k, .d {{ font-size:6.9pt; }} code {{ font-size:6.4pt; padding:0 1mm; }}
  footer {{ font-size:7.4pt; margin-top:2mm; padding-top:2mm; }}
}}
@media (max-width:640px) {{ .hd {{ flex-direction:column; }} .e {{ grid-template-columns:1fr; }} }}
</style>
</head>
<body>
<main>
<div class="hd">
  <img src="data:image/png;base64,{card}" alt="FM-FAM on the screen of the FM-1: the sequencer page">
  <div>
    <div class="kick">FM-FAM &middot; Cheat sheet</div>
    <h1>FM-FAM at a glance</h1>
    <p class="ver">For FM-FAM {version} on the M-VAVE FM-1</p>
    <p class="sum">{desc}</p>
    <div class="btns">
      <a class="solid" href="{release}">Install FM-FAM {version}</a>
      <a href="{repo}">Source code</a>
      <a href="#" onclick="window.print();return false">Print</a>
    </div>
  </div>
</div>
<p class="beta"><b>FM-FAM is beta software.</b> {beta}</p>
<p class="legend"><code>BUTTON</code> a button, key or encoder of the FM-1 &nbsp;&middot;&nbsp; tap, hold, turn &nbsp;&middot;&nbsp;
<code>A</code> + <code>B</code> hold A while you use B &nbsp;&middot;&nbsp; K1-K4 are the four knobs, in the order of the screen footer &nbsp;&middot;&nbsp;
the white keys are the steps</p>
<div class="cols">
{sections}
</div>
<footer>Community-made for the M-VAVE FM-1; not affiliated with or endorsed by M-VAVE. Everything on the screen is in
English. FM-FAM is open source under the MPL-2.0:
<a href="{repo}">{repo_short}</a>. All product names belong to their owners.</footer>
</main>
</body>
</html>
"""

page = PAGE.format(version=VERSION, desc=html.escape(DESCRIPTION), beta=html.escape(BETA), desc_attr=html.escape(DESCRIPTION, quote=True),
                   card=card, release=RELEASE, repo=REPO, repo_short=REPO.replace("https://", ""),
                   sections="\n".join(section(t, r) for t, r in SECTIONS))

html_path = os.path.join(out_dir, "FM-FAM-cheatsheet.html")
pdf_path = os.path.join(out_dir, "FM-FAM-cheatsheet.pdf")
with open(html_path, "w", encoding="utf-8") as f:
    f.write(page)

subprocess.run(["google-chrome", "--headless=new", "--no-sandbox", "--disable-gpu", "--no-pdf-header-footer",
                "--print-to-pdf=" + pdf_path, "file://" + html_path], check=True,
               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print(html_path, pdf_path)
