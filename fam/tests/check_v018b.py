#!/usr/bin/env python3 -I
# Checks what fam/tests/v018b.txt recorded: the first kick of each file, from its start (a step's own pitch and volume are heard).
# usage: python3 -I check_v018b.py DIR     (DIR holds kick18a.wav, kick18b.wav, kick18c.wav)
import sys, wave, struct

def load(path):
    w = wave.open(path)
    n = w.getnframes()
    raw = w.readframes(n)
    s = struct.unpack('<%dh' % (n * w.getnchannels()), raw)
    return [s[i] / 32768.0 for i in range(0, len(s), w.getnchannels())]

def first_hit(x, look=0.45):
    # from the first sample over the noise to 0.45 s later (the loop is half a second)
    start = next(i for i, v in enumerate(x) if abs(v) > 0.02)
    return x[start:start + int(44100 * look)]

def zc(x, ms):
    n = int(44100 * ms / 1000)
    return sum(1 for i in range(1, n) if (x[i - 1] < 0) != (x[i] < 0))

d = sys.argv[1]
a, b, c = (first_hit(load('%s/kick18%s.wav' % (d, k))) for k in 'abc')
def rms(h, ms=250):
    n = int(44100 * ms / 1000)
    return (sum(v * v for v in h[:n]) / n) ** 0.5

pa, pb, pc = (max(abs(v) for v in h) for h in (a, b, c))
ra, rc = rms(a), rms(c)
za, zb = zc(a, 120), zc(b, 120)
print('peak as it was %.3f   volume 67/127 %.3f   ratio %.2f' % (pa, pc, pc / pa))
print('level (rms of 250 ms) as it was %.4f   volume 67/127 %.4f   ratio %.2f (0.53 is the linear one; the limiter takes some)' % (ra, rc, rc / ra))
print('zero crossings in 120 ms: as it was %d   +12 semitones %d   ratio %.2f (2.00 is an octave)' % (za, zb, zb / max(za, 1)))
ok = 0.35 < rc / ra < 0.70 and pc < pa and 1.6 < zb / max(za, 1) < 2.4
print('ok' if ok else 'FAILED')
sys.exit(0 if ok else 1)
