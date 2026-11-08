#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
# The screenshots of README.md: takes the PNGs that the host simulator writes for fam/tests/readme.txt (rm_*.png)
# and saves them, with the same pixels, as small palette PNGs in fam/docs/screens.
#
#   python3 tools/build.py fam --host
#   mkdir -p fam/build/shots && fam/build/host/fm-fam_host fam/tests/readme.txt fam/build/shots
#   python3 fam/docs/make_screens.py fam/build/shots fam/docs/screens
import os, re, sys
from PIL import Image

src, dst = sys.argv[1], sys.argv[2]
os.makedirs(dst, exist_ok=True)
names = sorted(f for f in os.listdir(src) if re.fullmatch(r"rm_[a-z_]+\d*\.png", f))
if not names:
    sys.exit("no rm_*.png in " + src)
for f in names:
    m = re.fullmatch(r"rm_([a-z]+)(\d+)\.png", f)
    out = "%s-%s.png" % (m.group(1), m.group(2)) if m else {"rm_rand_syn1.png": "random-synth.png",
                                                            "rm_rand_kick.png": "random-kick.png"}[f]
    im = Image.open(os.path.join(src, f)).convert("RGB")
    assert im.size == (240, 240), (f, im.size)
    if len(im.getcolors(1 << 16)) <= 256:                        # the usual case: the palette holds every pixel
        im = im.quantize(colors=256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    im.save(os.path.join(dst, out), optimize=True)
    print(out, os.path.getsize(os.path.join(dst, out)))
