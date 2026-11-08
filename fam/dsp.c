/* SPDX-License-Identifier: MPL-2.0 */
/* FM-FAM: the sound engine. One synth voice (two oscillators of 16 types with sync and FM, noise, a filter that is a
 * 4-pole ladder or a state-variable one, three envelopes, a patch bay), a drum synth voice, and the effects chain
 * (compressor, stutter, DJ filter, delay, reverb, limiter). Float DSP: the FM-1's chip has a single-precision FPU, no doubles. */
#include <stdint.h>
#include "plat.h"
#include "fastmath.h"
#include "dsp.h"

#define SR 44100.0f
#define PI_F 3.14159265f

uint8_t syn_eff[NSYN], kick_p[NKICK], fx_p[NFX], patch_src[NPATCH], patch_amt[NPATCH];
uint8_t fx_en = FX_DLY | FX_REV | FX_LIM;
static float pwm_w = 0.5f, pwm_ph;            /* the pulse width of the PWM wave, moved slowly */

static uint32_t rng = 0x2545F491u;
static float nz;                                /* this sample's noise, drawn once and shared by the voices */
static inline float noise(void) { return nz; }
static inline void noise_next(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    nz = (float)(int32_t)rng * (1.0f / 2147483648.0f);
}
static inline float sat(float x)               /* a soft clip with no divide: x - 4/27 x^3, flat at +-1.5 */
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static inline float dec_coef(float d127, float t0, float octs)   /* a decay time of t0 * 2^(octs*d/127) s -> per sample */
{
    float tau = t0 * fm_exp2f(octs * d127 * (1.0f / 127.0f));
    return fm_expf(-1.0f / (tau * SR));
}
static inline float wrap1(float p) { return p - fm_floorf(p); }

/* ------------------------------------------------------------ oscillators --- */
static float blep(float t, float dt)
{
    if (t < dt) {
        t /= dt;
        return t + t - t * t - 1.0f;
    }
    if (t > 1.0f - dt) {
        t = (t - 1.0f) / dt;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}
static inline float saw1(float p, float dt) { return 2.0f * p - 1.0f - blep(p, dt); }

/* the additive types: six harmonics of a sine, by the recurrence sin((n+1)x) = 2 cos x sin(nx) - sin((n-1)x) */
static const float HARM[3][6] = {{1.0f, 0.7f, 0.0f, 0.5f, 0.0f, 0.3f},        /* organ */
                                 {1.0f, 0.5f, 0.9f, 0.7f, 0.3f, 0.15f},       /* a vowel: "ah" */
                                 {1.0f, 0.9f, 0.8f, 0.6f, 0.4f, 0.25f}};      /* brass */
static const float HNORM[3] = {0.32f, 0.225f, 0.2f};
static float harm(uint32_t t, float ph)
{
    const float *a = HARM[t];
    float sn = fm_sin_turns(ph), c2 = 2.0f * fm_sin_turns(ph + 0.25f), sp = 0.0f, y = 0.0f, s2;
    for (int n = 0; n < 6; n++) {
        y += a[n] * sn;
        s2 = c2 * sn - sp;
        sp = sn;
        sn = s2;
    }
    return y * HNORM[t];
}

/* the 16 types: 0 TRI, 1 SAW, 2 SQR, 3 PLS, 4 SIN, 5 PWM, 6 SSW (three detuned saws), 7 FM (two operators), 8 ORG,
 * 9 VOW, 10 BRS (additive), 11 FLD (a folded sine), 12 DIG (a crushed triangle), 13 MTL (two squares, ring-modulated),
 * 14 NSE (noise held for a cycle), 15 BEL (inharmonic partials). ex: the type's own extra phases. */
static float osc(uint32_t w, float ph, float dt, float *ex)
{
    switch (w) {
    case 0:
        return 4.0f * fm_fabsf(ph - 0.5f) - 1.0f;
    case 1:
        return saw1(ph, dt);
    case 2:
        return (ph < 0.5f ? 1.0f : -1.0f) + blep(ph, dt) - blep(wrap1(ph + 0.5f), dt);
    case 3:
        return (ph < 0.18f ? 1.0f : -1.0f) + blep(ph, dt) - blep(wrap1(ph + 0.82f), dt);
    case 4:
        return fm_sin_turns(ph);
    case 5:
        return (ph < pwm_w ? 1.0f : -1.0f) + blep(ph, dt) - blep(wrap1(ph + 1.0f - pwm_w), dt);
    case 6:
        ex[0] = wrap1(ex[0] + dt * 0.9935f);
        ex[1] = wrap1(ex[1] + dt * 1.0079f);
        return (saw1(ph, dt) + saw1(ex[0], dt) + saw1(ex[1], dt)) * 0.42f;
    case 7:
        return fm_sin_turns(ph + 0.32f * fm_sin_turns(ph + ph));
    case 8:
    case 9:
    case 10:
        return harm(w - 8u, ph);
    case 11:
        return fm_sin_turns(0.38f * fm_sin_turns(ph));
    case 12:
        return fm_floorf((4.0f * fm_fabsf(ph - 0.5f) - 1.0f) * 3.0f + 0.5f) * (1.0f / 3.0f);
    case 13:
        ex[0] = wrap1(ex[0] + dt * 1.4471f);
        return (ph < 0.5f ? 0.7f : -0.7f) * (ex[0] < 0.5f ? 1.0f : -1.0f);
    case 14:
        if (ph < dt)
            ex[1] = noise();
        return ex[1];
    default:
        ex[0] = wrap1(ex[0] + dt * 2.756f);
        ex[1] = wrap1(ex[1] + dt * 5.404f);
        return (fm_sin_turns(ph) + 0.6f * fm_sin_turns(ex[0]) + 0.4f * fm_sin_turns(ex[1])) * 0.5f;
    }
}

/* the same shapes for the screen, over any number of cycles (no band-limiting, no state) */
float dsp_wave(uint32_t t, float ph)
{
    float p = wrap1(ph);
    switch (t) {
    case 0: return 4.0f * fm_fabsf(p - 0.5f) - 1.0f;
    case 1: return 2.0f * p - 1.0f;
    case 2: return p < 0.5f ? 1.0f : -1.0f;
    case 3: return p < 0.18f ? 1.0f : -1.0f;
    case 4: return fm_sin_turns(p);
    case 5: return p < 0.3f ? 1.0f : -1.0f;
    case 6: return (2.0f * p - 1.0f + 2.0f * wrap1(p + 0.19f) - 1.0f + 2.0f * wrap1(p * 1.01f + 0.62f) - 1.0f) * 0.4f;
    case 7: return fm_sin_turns(p + 0.32f * fm_sin_turns(p + p));
    case 8: case 9: case 10: return harm(t - 8u, p);
    case 11: return fm_sin_turns(0.38f * fm_sin_turns(p));
    case 12: return fm_floorf((4.0f * fm_fabsf(p - 0.5f) - 1.0f) * 3.0f + 0.5f) * (1.0f / 3.0f);
    case 13: return (p < 0.5f ? 0.7f : -0.7f) * (wrap1(ph * 1.4471f) < 0.5f ? 1.0f : -1.0f);
    case 14: return (float)(int32_t)(((uint32_t)(p * 6.0f) * 2654435761u) >> 24) * (1.0f / 128.0f) - 1.0f;
    default: return (fm_sin_turns(ph) + 0.6f * fm_sin_turns(ph * 2.756f) + 0.4f * fm_sin_turns(ph * 5.404f)) * 0.5f;
    }
}

/* ------------------------------------------------------------------ synth --- */
static struct {
    float ph1, ph2, note, note_t, vel, acc;
    float veg, feg, aeg, atk_inc;
    int atk, rel;
    float gate;                        /* samples left of the gate (0: none), then the note is released */
    float vc, fc, ac;
    float lad[4], y4, s1, s2;
    float glc_rate, inv;
    int silent;
    float seqp, sh;                    /* the patch bay's sequencer pitch and sample-and-hold sources */
    float ex[2][2];                    /* each oscillator's extra phases (SSW, MTL, BEL), or a held noise value (NSE) */
} sy;

#define CTL 16u
static struct {
    float inc1, inc2, fm, l1, l2, nse, drv, g, G, k, res, outg, vc16, fc16;
    uint32_t w1, w2, mode;
    uint32_t n;
} cs;

static inline float patch(uint32_t dest, const float *src)   /* the modulation for a destination, -1..1 */
{
    uint32_t s = patch_src[dest];
    if (!s || s >= NSRC)
        return 0.0f;
    return src[s] * ((float)patch_amt[dest] - 64.0f) * (1.0f / 63.0f);
}

void dsp_syn_trig(float note, float vel, int accent, float seq_pitch, float gate)
{
    const uint8_t *p = syn_eff;
    sy.note_t = note;
    if (p[P_GLD] < 3)
        sy.note = note;
    sy.vel = vel;
    sy.acc = accent ? (float)p[P_ACC] * (1.0f / 127.0f) : 0.0f;
    sy.veg = 1.0f;
    sy.feg = 1.0f;
    sy.aeg = p[P_ATK] < 2 ? 1.0f : 0.0f;
    sy.atk = p[P_ATK] >= 2;
    sy.rel = 0;
    sy.gate = gate > 0.0f ? (gate < 32.0f ? 32.0f : gate) : 0.0f;
    sy.atk_inc = 1.0f / (0.0005f * fm_exp2f(8.0f * (float)p[P_ATK] * (1.0f / 127.0f)) * SR);
    sy.vc = dec_coef((float)p[P_VDEC], 0.004f, 9.0f);
    sy.fc = dec_coef((float)p[P_FDEC], 0.004f, 9.0f);
    sy.ac = dec_coef((float)p[P_ADEC], 0.004f, 9.5f);
    sy.glc_rate = p[P_GLD] < 3 ? 1000.0f : 1.0f / (0.0005f * fm_exp2f(8.0f * (float)p[P_GLD] * (1.0f / 127.0f)) * SR);
    cs.vc16 = sy.vc * sy.vc;
    cs.vc16 *= cs.vc16;
    cs.vc16 *= cs.vc16;
    cs.fc16 = sy.fc * sy.fc;
    cs.fc16 *= cs.fc16;
    cs.fc16 *= cs.fc16;
    cs.n = 0;
    sy.silent = 0;
    sy.seqp = seq_pitch;
    sy.ex[0][0] = sy.ex[1][0] = 0.33f;
    sy.ex[0][1] = sy.ex[1][1] = 0.66f;
    noise_next();
    sy.sh = noise();
}


static void synth_ctl(const float *src)           /* every CTL samples: pitch, filter, levels (the exp2 / tan / divides) */
{
    const uint8_t *p = syn_eff;
    float p1, p2, tune, cutoct, fc;
    sy.veg *= cs.vc16;
    sy.feg *= cs.fc16;
    sy.note += (sy.note_t - sy.note) * (1.0f - fm_expf(-(float)CTL * sy.glc_rate));
    tune = ((float)p[P_TUN] - 64.0f) * (1.0f / 64.0f);
    p1 = sy.note + ((float)p[P_V1FRQ] - 64.0f) * 0.5f + sy.veg * (float)p[P_V1EG] * (36.0f / 127.0f) + patch(D_VC1, src) * 24.0f + tune;
    p2 = sy.note + ((float)p[P_V2FRQ] - 64.0f) * 0.5f + sy.veg * (float)p[P_V2EG] * (36.0f / 127.0f) + patch(D_VC2, src) * 24.0f + tune;
    cs.inc1 = fm_clampf(440.0f / SR * fm_exp2f((p1 - 69.0f) * (1.0f / 12.0f)), 0.0f, 0.45f);
    cs.inc2 = fm_clampf(440.0f / SR * fm_exp2f((p2 - 69.0f) * (1.0f / 12.0f)), 0.0f, 0.45f);
    cs.w1 = (uint32_t)(fm_clampf((float)p[P_V1WAV] + patch(D_WAV, src) * 64.0f, 0.0f, 127.0f)) >> 3;
    cs.w2 = (uint32_t)p[P_V2WAV] >> 3;
    pwm_ph = wrap1(pwm_ph + (float)CTL * (1.3f / SR));
    pwm_w = 0.5f + 0.34f * (4.0f * fm_fabsf(pwm_ph - 0.5f) - 1.0f);
    cs.fm = fm_clampf((float)p[P_FM] + patch(D_FM, src) * 64.0f, 0.0f, 127.0f) * (3.0f / 127.0f);
    cs.l1 = (float)p[P_V1LVL] * (0.6f / 127.0f);
    cs.l2 = (float)p[P_V2LVL] * (0.6f / 127.0f);
    cs.nse = fm_clampf((float)p[P_NSE] + patch(D_NSE, src) * 64.0f, 0.0f, 127.0f) * (0.6f / 127.0f);
    cs.drv = 1.0f + (float)p[P_DRV] * (3.0f / 127.0f);
    cutoct = (float)p[P_CUT] * (10.0f / 127.0f) + sy.feg * (float)p[P_FEG] * (8.0f / 127.0f) + patch(D_CUT, src) * 5.0f;
    fc = fm_clampf(20.0f * fm_exp2f(cutoct), 20.0f, 0.42f * SR);
    cs.g = fm_tanf(PI_F * fc / SR);
    cs.res = fm_clampf((float)p[P_RES] + patch(D_RES, src) * 64.0f, 0.0f, 127.0f) * (1.0f / 127.0f);
    cs.mode = (uint32_t)p[P_MODE] >> 5;
    cs.G = cs.g / (1.0f + cs.g);                          /* the ladder's one-pole gain */
    cs.k = 2.0f - 1.96f * cs.res;                         /* the state variable's damping */
    cs.outg = (0.35f + 0.65f * sy.vel) * (1.0f + sy.acc * 1.2f) * (float)p[P_VOL] * (1.0f / 127.0f) * 1.6f;
    {                                                     /* 1 / (1 + k g + g g), once */
        float d = 1.0f + cs.k * cs.g + cs.g * cs.g;
        sy.inv = 1.0f / d;
    }
}

static float synth_sample(const float *src)
{
    float x, o1, o2, out;
    if (sy.silent)
        return 0.0f;
    if (cs.n == 0u) {
        synth_ctl(src);
        cs.n = CTL;
    }
    cs.n--;
    if (sy.gate > 0.0f) {                                /* the gate ends: the note is released in a few ms */
        sy.gate -= 1.0f;
        if (sy.gate <= 0.0f) {
            sy.gate = 0.0f;
            sy.rel = 1;
            sy.atk = 0;
        }
    }
    if (sy.atk) {
        sy.aeg += sy.atk_inc;
        if (sy.aeg >= 1.0f) {
            sy.aeg = 1.0f;
            sy.atk = 0;
        }
    } else {
        sy.aeg *= sy.ac;
        if (sy.rel)
            sy.aeg *= 0.9965f;                           /* about 6 ms to a tenth */
        if (sy.aeg < 0.00003f) {                         /* the note is over: nothing to compute until the next */
            sy.silent = 1;
            sy.aeg = 0.0f;
            sy.lad[0] = sy.lad[1] = sy.lad[2] = sy.lad[3] = sy.y4 = sy.s1 = sy.s2 = 0.0f;
            return 0.0f;
        }
    }
    o1 = osc(cs.w1, sy.ph1, cs.inc1, sy.ex[0]);
    sy.ph1 += cs.inc1;
    if (sy.ph1 >= 1.0f) {
        sy.ph1 -= 1.0f;
        if (syn_eff[P_SYN] >= 64)
            sy.ph2 = 0.0f;
    }
    sy.ph2 += cs.inc2 * (1.0f + cs.fm * o1);
    sy.ph2 -= (float)(int32_t)sy.ph2;
    if (sy.ph2 < 0.0f)
        sy.ph2 += 1.0f;
    o2 = osc(cs.w2, sy.ph2, cs.inc2, sy.ex[1]);
    x = sat((o1 * cs.l1 + o2 * cs.l2 + noise() * cs.nse) * cs.drv);
    if (cs.mode == 0u) {                                   /* the 4-pole ladder, the feedback one sample late */
        float u = sat(x * (1.0f + cs.res * 1.5f) - cs.res * 3.8f * sy.y4), v;
        for (int i = 0; i < 4; i++) {
            v = (u - sy.lad[i]) * cs.G;
            out = v + sy.lad[i];
            sy.lad[i] = out + v;
            u = out;
        }
        sy.y4 = u;
        out = u;
    } else {                                               /* state variable: LP2, BP, HP */
        float hp, bp, lp;
        hp = (x - (cs.k + cs.g) * sy.s1 - sy.s2) * sy.inv;
        bp = cs.g * hp + sy.s1;
        lp = cs.g * bp + sy.s2;
        sy.s1 = cs.g * hp + bp;
        sy.s2 = cs.g * bp + lp;
        out = cs.mode == 1u ? lp : cs.mode == 2u ? bp * (0.5f + 0.5f * cs.k) : hp;
    }
    return out * sy.aeg * cs.outg;
}

/* ------------------------------------------------------------------- kick --- */
static struct {
    float ph, ph2, penv, aenv, click, vel, hpz, mph[6];
    float pc, ac, rate, gain;
    int live;
} kk;

/* a hit of the kick: VEL and ACCENT as ever; SEMIS moves its pitch (a step's own, in semitones) and GAIN (0..1) is that step's own volume */
void dsp_kick_hit(float vel, int accent, float semis, float gain)
{
    const uint8_t *p = kick_p;
    kk.ph = kk.ph2 = 0.0f;
    kk.penv = 1.0f;
    kk.aenv = 1.0f;
    kk.click = 1.0f;
    kk.vel = (0.5f + 0.5f * vel) * (accent ? 1.3f : 1.0f);
    kk.rate = semis == 0.0f ? 1.0f : fm_exp2f(semis * (1.0f / 12.0f));
    kk.gain = gain;
    kk.pc = dec_coef((float)p[K_PDC], 0.004f, 6.5f);
    kk.ac = dec_coef((float)p[K_ADC], 0.02f, 6.0f);
    kk.live = 1;
}
void dsp_kick_trig(float vel, int accent)
{
    dsp_kick_hit(vel, accent, 0.0f, 1.0f);
}

static const float METAL[6] = {1.0f, 1.4471f, 1.6170f, 1.9265f, 2.5028f, 2.6637f};

static float kick_sample(void)
{
    const uint8_t *p = kick_p;
    float f, o, base, drv;
    uint32_t w;
    if (!kk.live)
        return 0.0f;
    kk.penv *= kk.pc;
    kk.aenv *= kk.ac;
    kk.click *= 0.985f;
    if (kk.aenv < 0.0001f) {
        kk.live = 0;
        return 0.0f;
    }
    base = 25.0f * fm_exp2f((float)p[K_TUN] * (4.5f / 127.0f)) * kk.rate;
    f = base * (1.0f + (float)p[K_SWP] * (12.0f / 127.0f) * kk.penv);
    w = ((uint32_t)p[K_WAV] * 6u) >> 7;
    if (w == 5u) {                                      /* metal: six squares, high-passed noise, a hat */
        float s = 0.0f;
        for (int i = 0; i < 6; i++) {
            kk.mph[i] = wrap1(kk.mph[i] + base * 4.0f * METAL[i] / SR);
            s += kk.mph[i] < 0.5f ? 0.16f : -0.16f;
        }
        o = s + noise() * (float)p[K_NSE] * (1.0f / 127.0f) * 0.5f;
        {
            float a = 0.2f + (float)p[K_TON] * (0.7f / 127.0f), y = o - kk.hpz;
            kk.hpz += (o - kk.hpz) * (1.0f - a * 0.5f);    /* one pole: the low part out */
            o = y;
        }
    } else {
        kk.ph = wrap1(kk.ph + f / SR);
        switch (w) {
        case 0:
            o = fm_sinf(kk.ph * 2.0f * PI_F);
            break;
        case 1:
            o = 4.0f * fm_fabsf(kk.ph - 0.5f) - 1.0f;
            break;
        case 2:
            o = sat(fm_sinf(kk.ph * 2.0f * PI_F) * 3.0f);
            break;
        case 3: {                                       /* two operators */
            float ratio = 0.5f + (float)p[K_FMR] * (7.5f / 127.0f);
            kk.ph2 = wrap1(kk.ph2 + f * ratio / SR);
            o = fm_sinf(kk.ph * 2.0f * PI_F + fm_sinf(kk.ph2 * 2.0f * PI_F) * (float)p[K_FMA] * (6.0f / 127.0f) * kk.aenv);
            break;
        }
        default:                                        /* noise, low-passed by its pitch */
            kk.hpz += (noise() - kk.hpz) * fm_clampf(f * (6.0f / SR), 0.01f, 0.9f);
            o = kk.hpz * 2.0f;
            break;
        }
        o += noise() * (float)p[K_NSE] * (1.0f / 127.0f) * 0.4f * kk.aenv;
    }
    o = o * kk.aenv + noise() * kk.click * (float)p[K_CLK] * (1.0f / 127.0f) * 0.8f;
    drv = 1.0f + (float)p[K_DRV] * (5.0f / 127.0f);
    return sat(o * drv) * (float)p[K_LVL] * (1.0f / 127.0f) * kk.vel * kk.gain * 1.2f;
}

/* ------------------------------------------------------------ the effects --- */
/* the chain: compressor (in parallel), stutter, delay, reverb, DJ filter (a high-pass), limiter. */
#define DMAX 33000u
#define SMAX 16384u
static int16_t dly[2][DMAX] PLAT_POOL;
static int16_t stb[SMAX] PLAT_POOL;
#define RV_N 4
static const uint16_t RV_LEN[RV_N] = {1116, 1188, 1277, 1356};
static float rvc[RV_N][1400] PLAT_POOL;
static float rva[2][600] PLAT_POOL;
static float rvd[64] PLAT_POOL;                 /* the right channel: the same tail, 37 samples later, inverted */

static struct {
    uint32_t dw, sw, sstart, rvi[RV_N], rai[2], rdw;
    float step, dlen, comp_env, rvlp[RV_N], dfl[2];
    float sidx, sg, sspd;                       /* the stutter: where in its slice, its gain, its speed */
    int stut;
    uint32_t stut_len;
    float dj[2][2][2];                          /* the DJ filter: a channel, a stage, its two states */
} fx;

void dsp_tempo(float step_samples) { fx.step = step_samples; }

static const float SLICE_MUL[8] = {2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.375f, 0.25f, 0.125f};   /* in 16ths */
void dsp_stutter(int on)
{
    if (on && !fx.stut) {
        float len = fx.step * SLICE_MUL[((uint32_t)fx_p[F_STS] * 8u) >> 7];
        fx.stut_len = (uint32_t)fm_clampf(len, 64.0f, (float)SMAX - 2.0f);
        fx.sstart = (fx.sw + SMAX - fx.stut_len) % SMAX;
        fx.sidx = 0.0f;
        fx.sg = 1.0f;
    }
    fx.stut = on;
}

static void zero(void *p, uint32_t n)
{
    uint32_t *w = (uint32_t *)p;
    for (n >>= 2; n; n--)
        *w++ = 0;
}

void dsp_init(void)
{
    zero(dly, sizeof dly);
    zero(stb, sizeof stb);
    zero(rvc, sizeof rvc);
    zero(rva, sizeof rva);
    zero(rvd, sizeof rvd);
    zero(&fx, sizeof fx);
    zero(&sy, sizeof sy);
    zero(&kk, sizeof kk);
    fx.step = 5512.0f;
    fx.dlen = 11000.0f;
    fx.sg = 1.0f;
}

void dsp_panic(void)
{
    sy.aeg = 0.0f;
    kk.live = 0;
}

static const uint8_t DLY_MUL[8] = {1, 2, 3, 4, 6, 8, 12, 16};

void dsp_render(int32_t *lr, uint32_t n, float gain)
{
    float src[NSRC];
    float cthr = 1.0f - (float)fx_p[F_CMA] * (0.011f / 1.0f) * 0.7f, cmix = (fx_en & FX_CMP) ? (float)fx_p[F_CMM] * (1.0f / 127.0f) : 0.0f;
    float cmk = 1.0f + (float)fx_p[F_CMG] * (1.2f / 127.0f), crel = 1.0f - (0.00005f + (float)fx_p[F_CMS] * (0.001f / 127.0f));
    float dmix = (fx_en & FX_DLY) ? (float)fx_p[F_DLM] * (1.0f / 127.0f) : 0.0f, dfb = (float)fx_p[F_DLF] * (0.85f / 127.0f);
    float dlp = 0.15f + (float)fx_p[F_DLK] * (0.8f / 127.0f);
    float smix = (float)fx_p[F_STM] * (1.0f / 127.0f), sdec = 1.0f - (float)fx_p[F_STD] * (0.5f / 127.0f);
    float rmix = (fx_en & FX_REV) ? (float)fx_p[F_RVM] * (1.0f / 127.0f) : 0.0f, rfb = 0.70f + (float)fx_p[F_RVS] * (0.27f / 127.0f);
    float rdamp = 0.9f - (float)fx_p[F_RVT] * (0.8f / 127.0f), rwid = (float)fx_p[F_RVW] * (1.0f / 127.0f);
    float jmix = (fx_en & FX_DJF) ? (float)fx_p[F_DJM] * (1.0f / 127.0f) : 0.0f;
    float lim = 0.3f + (float)fx_p[F_LIM] * (0.7f / 127.0f), ldrv = 1.3f * (1.0f + (float)fx_p[F_LMD] * (2.0f / 127.0f));
    float lsoft = (float)fx_p[F_LMS] * (1.0f / 127.0f), lout = (0.4f + 0.7f * (float)fx_p[F_LMO] * (1.0f / 127.0f)) * (1.0f / 0.95f);
    float dtarget = fx.step * (float)DLY_MUL[((uint32_t)fx_p[F_DLT] * 8u) >> 7];
    float jg = 0.0f, jk = 0.0f, jinv = 0.0f;
    int jst = fx_p[F_DJS] >= 64 ? 2 : 1;
    uint32_t i;
    while (dtarget > (float)(DMAX - 2u))
        dtarget *= 0.5f;
    {
        int want = (fx_en & FX_STU) != 0;
        if (want != (fx.stut != 0))
            dsp_stutter(want);
    }
    fx.sspd = fm_exp2f(((float)fx_p[F_STP] - 64.0f) * (1.0f / 64.0f));
    if (jmix > 0.0f) {                                   /* a state-variable high-pass, 12 or 24 dB */
        float fc = fm_clampf(20.0f * fm_exp2f((float)fx_p[F_DJC] * (8.6f / 127.0f)), 20.0f, 0.4f * SR);
        jg = fm_tanf(PI_F * fc / SR);
        jk = 2.0f - 1.9f * (float)fx_p[F_DJR] * (1.0f / 127.0f);
        jinv = 1.0f / (1.0f + jk * jg + jg * jg);
    } else {
        zero(fx.dj, sizeof fx.dj);
    }
    src[S_SEQP] = sy.seqp * (1.0f / 24.0f);
    src[S_SEQV] = sy.vel;
    src[S_NSE] = sy.sh;
    for (i = 0; i < n; i++) {
        float s, k, m, c, dl, dr, wl, wr, rin, a, e;
        uint32_t r;
        noise_next();
        src[S_VEG] = sy.veg;
        src[S_FEG] = sy.feg;
        src[S_AEG] = sy.aeg;
        src[S_KEG] = kk.live ? kk.aenv : 0.0f;
        s = synth_sample(src);
        k = kick_sample();
        m = s + k;
        /* compressor, in parallel */
        if (cmix > 0.0f) {
            a = fm_fabsf(m);
            fx.comp_env = a > fx.comp_env ? a : fx.comp_env * crel;
            e = fx.comp_env;
            c = e > cthr ? (cthr + (e - cthr) * 0.25f) / e : 1.0f;
            m = m * (1.0f - cmix) + m * c * cmk * cmix;
        }
        /* stutter: the last slice, repeated while it is on, each time a little quieter and at its own speed */
        stb[fx.sw] = (int16_t)(fm_clampf(m, -1.0f, 1.0f) * 32000.0f);
        fx.sw = fx.sw + 1u >= SMAX ? 0u : fx.sw + 1u;
        if (fx.stut && smix > 0.0f) {
            uint32_t i0 = (uint32_t)fx.sidx, si = fx.sstart + i0, sj;
            float fr = fx.sidx - (float)i0, v;
            si = si >= SMAX ? si - SMAX : si;
            sj = si + 1u >= SMAX ? 0u : si + 1u;
            v = ((float)stb[si] * (1.0f - fr) + (float)stb[sj] * fr) * (1.0f / 32000.0f) * fx.sg;
            fx.sidx += fx.sspd;
            if (fx.sidx >= (float)fx.stut_len) {
                fx.sidx -= (float)fx.stut_len;
                fx.sg *= sdec;
            }
            m = m * (1.0f - smix) + v * smix;
        }
        /* delay: ping-pong, the feedback low-passed (nothing of it while its mix is 0) */
        wl = wr = m;
        if (dmix > 0.0f) {
            fx.dlen += (dtarget - fx.dlen) * 0.0005f;
            r = fx.dw + DMAX - (uint32_t)fx.dlen;
            r = r >= DMAX ? r - DMAX : r;
            dl = (float)dly[0][r] * (1.0f / 32000.0f);
            dr = (float)dly[1][r] * (1.0f / 32000.0f);
            fx.dfl[0] += (dr - fx.dfl[0]) * dlp;
            fx.dfl[1] += (dl - fx.dfl[1]) * dlp;
            dly[0][fx.dw] = (int16_t)(fm_clampf(m + fx.dfl[0] * dfb, -1.0f, 1.0f) * 32000.0f);
            dly[1][fx.dw] = (int16_t)(fm_clampf(fx.dfl[1] * dfb, -1.0f, 1.0f) * 32000.0f);
            fx.dw = fx.dw + 1u >= DMAX ? 0u : fx.dw + 1u;
            wl += dl * dmix;
            wr += dr * dmix;
        }
        /* reverb: four combs and two all-passes on the mono mix; the right side is the tail a moment later, inverted */
        if (rmix > 0.0f) {
            float acc = 0.0f, ra;
            rin = (wl + wr) * 0.5f * 0.12f;
            for (r = 0; r < RV_N; r++) {
                uint32_t ix = fx.rvi[r];
                float y = rvc[r][ix];
                fx.rvlp[r] += (y - fx.rvlp[r]) * (1.0f - rdamp);
                rvc[r][ix] = rin + fx.rvlp[r] * rfb;
                fx.rvi[r] = ix + 1u >= RV_LEN[r] ? 0u : ix + 1u;
                acc += y;
            }
            for (r = 0; r < 2u; r++) {
                uint32_t ix = fx.rai[r], len = r ? 441u : 556u;
                float y = rva[r][ix], v = acc - y * 0.5f;
                rva[r][ix] = v;
                acc = y + v * 0.5f;
                fx.rai[r] = ix + 1u >= len ? 0u : ix + 1u;
            }
            rvd[fx.rdw & 63u] = acc;
            ra = rvd[(fx.rdw - 37u) & 63u];
            fx.rdw++;
            wl += acc * rmix * 1.6f;
            wr += ((1.0f - rwid) * acc - rwid * ra) * rmix * 1.6f;
        }
        /* the DJ filter: everything below the cutoff goes (a high-pass), after the reverb so its tail goes too */
        if (jmix > 0.0f) {
            for (int ch = 0; ch < 2; ch++) {
                float x = ch ? wr : wl, in = x;
                for (int st = 0; st < jst; st++) {
                    float *z = fx.dj[ch][st], hp = (in - (jk + jg) * z[0] - z[1]) * jinv, bp = jg * hp + z[0], lp = jg * bp + z[1];
                    z[0] = jg * hp + bp;
                    z[1] = jg * bp + lp;
                    in = hp;
                }
                x = x * (1.0f - jmix) + in * jmix;
                if (ch)
                    wr = x;
                else
                    wl = x;
            }
        }
        /* the limiter, last: a pre-gain, a ceiling hard or soft, an output level */
        wl *= ldrv;
        wr *= ldrv;
        if (fx_en & FX_LIM) {
            float hl = fm_clampf(wl, -lim, lim), hr = fm_clampf(wr, -lim, lim);
            wl = (hl * (1.0f - lsoft) + lim * 0.95f * sat(wl / lim) * lsoft) * lout;
            wr = (hr * (1.0f - lsoft) + lim * 0.95f * sat(wr / lim) * lsoft) * lout;
        } else {                                         /* limiter off: a hard clip at the converter */
            wl *= 0.95f * lout;
            wr *= 0.95f * lout;
        }
        lr[2 * i] = (int32_t)(fm_clampf(wl, -1.0f, 1.0f) * gain);
        lr[2 * i + 1] = (int32_t)(fm_clampf(wr, -1.0f, 1.0f) * gain);
    }
}
