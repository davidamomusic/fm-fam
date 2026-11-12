/* SPDX-License-Identifier: MPL-2.0 */
/* FM-FAM: the sequencer, the controls and the screen. The sound is in dsp.c. Part of the unity build, after the
 * platform: gfx.c (cv_*, fonts, colours) is in scope. */
#include <stddef.h>
#include "fastmath.h"
#include "dsp.h"

#define SND_MAGIC 0x4D464733u            /* "FM3" */
#define PAT_MAGIC 0x4D465035u            /* "FP5": the banks of v0.18: the kick's steps have a pitch and a volume */
#define PAT_MAGIC4 0x4D465034u           /* "FP4": the banks of v0.9 to v0.17, each pattern with its own sound (read, then written as FP5) */
#define PAT_MAGIC8 0x4D465033u           /* "FP3": the banks of v0.8 (read, then written as FP5) */
#define PRE_MAGIC 0x4D465233u            /* "FR3" */
#define PRJ_MAGIC 0x4D464A33u            /* "FJ3" */
#define NBANK 11                         /* one bank per black key, 16 patterns each (the white keys) */
#define NPB 16
#define NPAT (NBANK * NPB)
#define NSTEP 16
#define LEN_DEF 8                        /* a new pattern is 8 steps long */
#define KLEN_DEF 16                      /* and its kick sequence 16 */
#define GATE_FULL 33                     /* no gate: the envelope decays by itself (1..32: that many 32nds of the step) */
#define NUSER 32                         /* user presets of each kind */
#define NPROJ 4                          /* project slots */
#define PROJ_BANKS 3                     /* a project keeps its sound and the banks A..C (48 patterns) */
#define PN 9                             /* a name: 8 characters and the end */
#define NRP 23                           /* the random pages */

/* the kinds of preset a pattern can recall, and the settings of the scale */
enum { PK_SYN, PK_KICK, PK_FX, PK_PATCH, PK_SCALE, NPK };
enum { SC_ROOT, SC_TYPE, SC_OCT, SC_QUANT, SC_TRANS, SC_FINE, SC_HUM, SC_SPREAD, NSC };
/* a pattern's own sound as bytes: the synth, the kick, the effects and which are on, the patch bay, the scale (what pre_cap gives) */
#define SNDSZ (NSYN + NKICK + (NFX + 1) + (2 * NPATCH + 1) + NSC)
#define PPK 238                          /* a pattern as stored: 16 steps of 9 bytes (72 bits), 3 of length, 5 of preset marks, its sound */
#define KVOL_DEF 127                     /* a kick step's volume: full, so a pattern without one sounds as it always did */

/* ------------------------------------------------------------------ data --- */
typedef struct {
    uint8_t trig[NSTEP];                 /* 0 off, 1 trig, 2 accent */
    int8_t pit[NSTEP];                   /* semitones from C3 */
    uint8_t vel[NSTEP];                  /* 0 = rest */
    uint8_t cond[NSTEP];                 /* index in COND */
    uint8_t ktrig[NSTEP];                /* the kick's steps: 0 off, 1 trig, 2 accent */
    uint8_t gate[NSTEP];                 /* 1..32: 32nds of a step; GATE_FULL: no gate */
    uint8_t rat[NSTEP];                  /* hits in the step - 1 (0..3) */
    uint8_t lk_p[NSTEP][2];              /* a locked parameter + 1 (0 none) */
    uint8_t lk_v[NSTEP][2];
    uint8_t kcond[NSTEP];                /* the kick's condition and ratchet for each step */
    uint8_t krat[NSTEP];
    int8_t kpit[NSTEP];                  /* the kick's pitch for each step: semitones from its tune, -24..24 */
    uint8_t kvol[NSTEP];                 /* and its volume, 0..127 */
    uint8_t len;                         /* steps in the pattern, 1..16 */
    uint8_t swing;                       /* 0..127 */
    uint8_t klen;                        /* steps in the kick's own sequence, 1..16 */
    uint8_t pre[NPK];                    /* the preset each kind of its sound came from: its list index + 1, 0 none */
    uint8_t sok;                         /* the sound below is this pattern's own (a pattern never visited has none yet) */
    uint8_t snd[SNDSZ];                  /* its sound */
} pat_t;
typedef struct {                         /* the pattern of v0.8, to read its banks */
    uint8_t trig[NSTEP], pit[NSTEP], vel[NSTEP], cond[NSTEP], ktrig[NSTEP], gate[NSTEP], rat[NSTEP];
    uint8_t lk_p[NSTEP][2], lk_v[NSTEP][2], kcond[NSTEP], krat[NSTEP];
    uint8_t len, swing, klen, pre[NPK], rsv[2];
} pat8_t;
typedef struct {
    uint32_t magic;
    uint8_t syn[NSYN], kick[NKICK], fx[NFX], src[NPATCH], amt[NPATCH], sc[NSC];
    uint16_t bpm;
    uint8_t pre[NPK];                    /* each kind's current preset: its list index (factory ones, then the user's) */
    uint8_t song_on, fxen, pon, rsv0;
    uint8_t song[32];
    uint8_t rnd_en[NRP];                 /* the random pages: bit k = knob k takes part */
    uint8_t rnd_amt[NRP][4];             /* and how much (0..127) */
    char pname[PN];                      /* the project's name */
} snd_t;
typedef struct {
    uint32_t magic;
    uint8_t d[NPB][PPK];                 /* the patterns, packed (pat_pack) */
} bank_t;
typedef char bank_fits[(sizeof(bank_t) <= 3840 && sizeof(bank_t) >= sizeof(uint32_t) + NPB * sizeof(pat8_t)) ? 1 : -1];
typedef struct {                         /* the user presets of one kind */
    uint32_t magic;
    struct { char n[PN]; uint8_t d[27]; } s[NUSER];
} upre_t;
typedef struct {                         /* a project: its name, then the sound; its banks are the next objects */
    uint32_t magic;
    char name[PN];
    uint8_t rsv[3];
    snd_t s;
} prj_t;

static snd_t sd;                         /* the sound as saved */
static uint8_t *const syn = sd.syn;      /* the knobs' values; syn_eff (dsp.c) is what plays */
static pat_t pats[NPAT] PLAT_POOL;
static upre_t up[NPK];
static bank_t bk_buf;
static uint32_t bank_sig[NBANK], snd_sig;  /* what each object holds in flash (a hash): autosave writes the ones that differ */
static char pj_name[NPROJ][PN];
static uint8_t pj_used[NPROJ];
static uint8_t dirty;                    /* something has changed that the flash does not hold yet */
static uint32_t edit_t;                  /* when it last changed (the autosave waits for a quiet moment) */
static void touch(void) { dirty = 1; edit_t = plat_ms(); }
static uint8_t bk_hold;                  /* a backup to the computer is streaming (app_backup_hold): nothing is saved meanwhile */
static int store_put(uint32_t obj, const void *p, uint32_t n) { return bk_hold ? -1 : plat_store_save(obj, p, n); }

/* ---- the scales: the notes of each above its root ---- */
#define B(i) (1u << (i))
static const struct { const char *n; uint16_t m; } SCALES[] = {
    {"CHROMATIC", 0xFFF},
    {"MAJOR", B(0) | B(2) | B(4) | B(5) | B(7) | B(9) | B(11)},
    {"MINOR", B(0) | B(2) | B(3) | B(5) | B(7) | B(8) | B(10)},
    {"DORIAN", B(0) | B(2) | B(3) | B(5) | B(7) | B(9) | B(10)},
    {"PHRYGIAN", B(0) | B(1) | B(3) | B(5) | B(7) | B(8) | B(10)},
    {"LYDIAN", B(0) | B(2) | B(4) | B(6) | B(7) | B(9) | B(11)},
    {"MIXOLYD", B(0) | B(2) | B(4) | B(5) | B(7) | B(9) | B(10)},
    {"LOCRIAN", B(0) | B(1) | B(3) | B(5) | B(6) | B(8) | B(10)},
    {"HARM MIN", B(0) | B(2) | B(3) | B(5) | B(7) | B(8) | B(11)},
    {"MEL MIN", B(0) | B(2) | B(3) | B(5) | B(7) | B(9) | B(11)},
    {"PENT MAJ", B(0) | B(2) | B(4) | B(7) | B(9)},
    {"PENT MIN", B(0) | B(3) | B(5) | B(7) | B(10)},
    {"BLUES", B(0) | B(3) | B(5) | B(6) | B(7) | B(10)},
    {"WHOLE", B(0) | B(2) | B(4) | B(6) | B(8) | B(10)},
    {"DIMINISH", B(0) | B(2) | B(3) | B(5) | B(6) | B(8) | B(9) | B(11)},
    {"HIRAJOSHI", B(0) | B(2) | B(3) | B(7) | B(8)},
};
#define NSCALE 16
static const char *const NOTE12[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

static int scale_has(int pc)             /* is this pitch class (0 = C) in the scale? */
{
    int rel = ((pc - (int)sd.sc[SC_ROOT]) % 12 + 12) % 12;
    return (SCALES[sd.sc[SC_TYPE] % NSCALE].m >> rel) & 1u;
}
static int scale_snap(int note)          /* the nearest note of the scale (a tie goes down) */
{
    for (int d = 0; d < 7; d++) {
        if (scale_has(((note - d) % 12 + 12) % 12))
            return note - d;
        if (scale_has(((note + d) % 12 + 12) % 12))
            return note + d;
    }
    return note;
}

/* ---- the factory presets ---- */
static const struct { const char *n; uint8_t v[NSYN]; } SPRE[] = {
    {"FAM BASS", {64, 12, 100, 20, 62, 12, 80, 20, 0, 0, 0, 50, 40, 55, 0, 80, 60, 0, 75, 100, 30, 80, 0, 64}},
    {"ZAP", {80, 4, 90, 127, 70, 20, 60, 110, 70, 127, 0, 45, 100, 40, 70, 60, 40, 0, 55, 100, 20, 80, 0, 64}},
    {"FM CLANG", {64, 4, 70, 20, 90, 4, 70, 20, 100, 0, 10, 60, 85, 30, 40, 40, 70, 0, 85, 100, 10, 80, 0, 64}},
    {"ACID", {64, 12, 110, 10, 64, 4, 0, 0, 0, 0, 0, 40, 35, 95, 0, 110, 45, 0, 70, 100, 40, 120, 20, 64}},
    {"SNARE", {78, 4, 70, 60, 100, 20, 40, 20, 0, 0, 110, 28, 100, 20, 100, 30, 30, 0, 40, 110, 10, 100, 0, 64}},
    {"TOM", {52, 4, 110, 60, 64, 4, 0, 0, 0, 0, 5, 40, 90, 10, 40, 30, 40, 0, 65, 105, 10, 80, 0, 64}},
    {"METAL", {70, 20, 80, 30, 93, 20, 80, 30, 90, 127, 40, 40, 110, 30, 100, 30, 40, 0, 50, 90, 30, 80, 0, 64}},
    {"DRONE", {64, 12, 90, 0, 65, 12, 90, 0, 20, 0, 0, 40, 50, 50, 0, 20, 120, 40, 120, 90, 20, 60, 40, 64}},
    {"ORGAN", {64, 68, 90, 0, 88, 68, 50, 0, 0, 0, 0, 60, 100, 20, 0, 20, 40, 0, 90, 100, 10, 60, 0, 64}},
    {"VOWEL", {64, 76, 100, 0, 66, 76, 70, 0, 0, 0, 5, 60, 90, 30, 0, 30, 50, 0, 80, 100, 10, 60, 0, 64}},
    {"BRASS", {64, 84, 100, 30, 64, 84, 80, 30, 0, 0, 0, 45, 50, 25, 0, 90, 55, 15, 85, 100, 20, 70, 0, 64}},
    {"BELL", {64, 124, 90, 0, 76, 124, 60, 0, 40, 0, 0, 100, 110, 10, 0, 10, 80, 0, 110, 90, 0, 60, 0, 64}},
    {"FOLD LEAD", {64, 92, 100, 0, 65, 92, 60, 0, 20, 0, 0, 70, 80, 40, 0, 60, 80, 0, 90, 100, 40, 80, 20, 64}},
    {"DIGITAL", {64, 100, 100, 0, 64, 100, 70, 0, 10, 0, 0, 40, 95, 20, 0, 40, 45, 0, 70, 100, 20, 80, 0, 64}},
    {"PWM PAD", {64, 44, 90, 0, 63, 44, 90, 0, 0, 0, 0, 100, 60, 20, 0, 20, 100, 30, 120, 90, 0, 60, 30, 64}},
    {"SUPERSAW", {64, 52, 100, 0, 64, 52, 100, 0, 0, 0, 0, 80, 85, 20, 0, 30, 90, 0, 100, 90, 10, 70, 0, 64}},
};
#define NSPRE ((int)(sizeof SPRE / sizeof SPRE[0]))
static const struct { const char *n; uint8_t v[NKICK]; } KPRE[] = {
    {"808 KICK", {30, 40, 45, 70, 20, 20, 100, 0, 0, 0, 0, 0}},
    {"909 PUNCH", {34, 70, 30, 55, 60, 50, 105, 22, 0, 0, 0, 0}},
    {"TECHNO", {38, 55, 35, 85, 30, 80, 100, 43, 0, 0, 0, 0}},
    {"ZAP KICK", {22, 127, 60, 50, 10, 30, 95, 0, 0, 0, 0, 0}},
    {"FM TOMS", {50, 25, 40, 65, 0, 15, 100, 64, 60, 40, 0, 0}},
    {"DEEP SUB", {18, 10, 40, 110, 0, 10, 110, 0, 0, 0, 0, 0}},
    {"808 SUB", {22, 20, 50, 95, 5, 40, 105, 0, 0, 0, 0, 0}},
    {"MUSICBOX", {80, 0, 20, 70, 40, 0, 80, 64, 40, 90, 0, 0}},
    {"HATS", {70, 0, 10, 25, 0, 20, 70, 107, 0, 0, 40, 90}},
};
#define NKPRE ((int)(sizeof KPRE / sizeof KPRE[0]))
/*                    CMP: thr mix spd gain | STU: size mix dec spd | DLY: time fb mix tone | REV: size tone mix width | DJF: cut res mix slope | LIM: ceil drive out soft */
static const struct { const char *n; uint8_t v[NFX]; uint8_t en; } FPRE[] = {
    {"DEFAULT", {40, 80, 40, 20, 40, 100, 0, 64, 40, 50, 30, 80, 80, 60, 25, 10, 0, 30, 127, 0, 110, 0, 100, 127}, FX_DLY | FX_REV | FX_LIM},
    {"DRY", {40, 80, 40, 20, 40, 100, 0, 64, 40, 50, 0, 80, 80, 60, 0, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_LIM},
    {"DUB ECHO", {40, 80, 40, 20, 40, 100, 0, 64, 80, 100, 80, 40, 100, 50, 20, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_DLY | FX_REV | FX_LIM},
    {"HALL", {40, 80, 40, 20, 40, 100, 0, 64, 40, 50, 0, 80, 120, 40, 70, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_REV | FX_LIM},
    {"PUNCH", {90, 110, 20, 50, 40, 100, 0, 64, 40, 50, 15, 80, 80, 60, 0, 127, 0, 30, 127, 0, 110, 30, 100, 127}, FX_CMP | FX_DLY | FX_LIM},
    {"DJ SWEEP", {40, 80, 40, 20, 40, 100, 0, 64, 40, 50, 20, 80, 80, 60, 20, 127, 60, 85, 127, 127, 110, 0, 100, 127}, FX_DJF | FX_DLY | FX_LIM},
    {"STUTTER", {40, 80, 40, 20, 56, 127, 50, 64, 40, 50, 20, 80, 80, 60, 0, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_DLY | FX_LIM},
    {"SPACE", {40, 80, 40, 20, 40, 100, 0, 64, 50, 85, 60, 60, 127, 30, 90, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_DLY | FX_REV | FX_LIM},
    {"LOFI", {100, 127, 60, 40, 40, 100, 0, 64, 40, 50, 0, 80, 80, 60, 0, 127, 0, 30, 127, 0, 50, 100, 90, 0}, FX_CMP | FX_LIM},
    {"WIDE ROOM", {40, 80, 40, 20, 40, 100, 0, 64, 30, 40, 25, 80, 60, 70, 55, 127, 0, 30, 127, 0, 110, 0, 100, 127}, FX_DLY | FX_REV | FX_LIM},
};
#define NFPRE ((int)(sizeof FPRE / sizeof FPRE[0]))
static const struct { const char *n; uint8_t src[NPATCH], amt[NPATCH], pon; } PPRE[] = {
    {"NO CABLES", {0, 0, 0, 0, 0, 0, 0, 0}, {64, 64, 64, 64, 64, 64, 64, 64}, 0},
    {"SEQ>CUT", {0, 0, 0, S_SEQP, 0, 0, 0, 0}, {64, 64, 64, 110, 64, 64, 64, 64}, 8},
    {"KICK DUCK", {0, 0, 0, S_KEG, 0, 0, 0, 0}, {64, 64, 64, 10, 64, 64, 64, 64}, 8},
    {"PITCH ENV", {S_VEG, S_VEG, 0, 0, 0, 0, 0, 0}, {100, 100, 64, 64, 64, 64, 64, 64}, 3},
    {"S&H PITCH", {0, S_NSE, 0, 0, 0, 0, 0, 0}, {64, 90, 64, 64, 64, 64, 64, 64}, 2},
    {"WAVE SWEEP", {0, 0, S_AEG, 0, 0, 0, 0, S_FEG}, {64, 64, 90, 64, 64, 64, 64, 120}, 132},
    {"ACID", {0, 0, 0, S_VEG, S_SEQV, 0, 0, 0}, {64, 64, 64, 110, 100, 64, 64, 64}, 24},
};
#define NPPRE ((int)(sizeof PPRE / sizeof PPRE[0]))
/*                              root type oct quant trans fine hum spread */
static const struct { const char *n; uint8_t v[NSC]; } CPRE[] = {
    {"C MAJOR", {0, 1, 2, 1, 12, 50, 0, 64}},
    {"A MINOR", {9, 2, 2, 1, 12, 50, 0, 64}},
    {"D DORIAN", {2, 3, 2, 1, 12, 50, 0, 64}},
    {"E PHRYG", {4, 4, 2, 1, 12, 50, 0, 64}},
    {"F LYDIAN", {5, 5, 2, 1, 12, 50, 0, 64}},
    {"G MIXO", {7, 6, 2, 1, 12, 50, 0, 64}},
    {"C PENT MN", {0, 11, 2, 1, 12, 50, 0, 64}},
    {"C PENT MJ", {0, 10, 2, 1, 12, 50, 0, 64}},
    {"C BLUES", {0, 12, 2, 1, 12, 50, 0, 64}},
    {"C WHOLE", {0, 13, 2, 1, 12, 50, 0, 64}},
    {"CHROMATIC", {0, 0, 2, 0, 12, 50, 0, 64}},
    {"HUMAN MIN", {9, 2, 2, 1, 12, 50, 60, 64}},
    {"WIDE LEAP", {0, 11, 2, 1, 12, 50, 0, 110}},
};
#define NCPRE ((int)(sizeof CPRE / sizeof CPRE[0]))
static const uint8_t NFACT[NPK] = {NSPRE, NKPRE, NFPRE, NPPRE, NCPRE};
static const uint8_t PSZ[NPK] = {NSYN, NKICK, NFX + 1, 2 * NPATCH + 1, NSC};
static const char PKC[NPK] = {'P', 'K', 'F', 'C', 'S'};

static const char *const COND[] = {"1:1", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "RND"};
#define NCOND 11
static const uint8_t COND_I[NCOND] = {1, 1, 2, 1, 2, 3, 1, 2, 3, 4, 0}, COND_N[NCOND] = {1, 2, 2, 3, 3, 3, 4, 4, 4, 4, 0};

static uint32_t rnd_s = 0x9E3779B9u;
static uint32_t rnd(void)
{
    rnd_s ^= rnd_s << 13;
    rnd_s ^= rnd_s >> 17;
    rnd_s ^= rnd_s << 5;
    return rnd_s;
}

static void text_cat(char *d, const char *s)
{
    while (*d)
        d++;
    while (*s)
        *d++ = *s++;
    *d = 0;
}
static void int_text(int32_t v, char *b)
{
    char t[12];
    int n = 0;
    uint32_t u = (uint32_t)(v < 0 ? -v : v);
    do
        t[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) && n < 11);
    if (v < 0)
        *b++ = '-';
    while (n)
        *b++ = t[--n];
    *b = 0;
}
static void scopy(char *d, const char *s, int n)
{
    int i = 0;
    while (s[i] && i < n - 1) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void pat_name(int pi, char *b)    /* "A01": the bank's letter and the pattern in it */
{
    b[0] = (char)('A' + pi / NPB);
    b[1] = (char)('0' + (pi % NPB + 1) / 10);
    b[2] = (char)('0' + (pi % NPB + 1) % 10);
    b[3] = 0;
}

static void publish(void);

/* ---- presets: a list of each kind, the factory ones then the user's 32 ---- */
static int pre_count(int k) { return NFACT[k] + NUSER; }
static int pre_used(int k, int i) { return i < NFACT[k] || up[k].s[i - NFACT[k]].n[0] != 0; }
static const char *pre_name(int k, int i)
{
    if (i >= NFACT[k])
        return up[k].s[i - NFACT[k]].n;
    switch (k) {
    case PK_SYN: return SPRE[i].n;
    case PK_KICK: return KPRE[i].n;
    case PK_FX: return FPRE[i].n;
    case PK_PATCH: return PPRE[i].n;
    default: return CPRE[i].n;
    }
}
static void pre_cap(int k, uint8_t *d)   /* the live settings of a kind as bytes */
{
    int i;
    switch (k) {
    case PK_SYN:
        for (i = 0; i < NSYN; i++)
            d[i] = syn[i];
        break;
    case PK_KICK:
        for (i = 0; i < NKICK; i++)
            d[i] = kick_p[i];
        break;
    case PK_FX:
        for (i = 0; i < NFX; i++)
            d[i] = sd.fx[i];
        d[NFX] = (uint8_t)(sd.fxen & ~FX_STU);
        break;
    case PK_PATCH:
        for (i = 0; i < NPATCH; i++) {
            d[i] = sd.src[i];
            d[NPATCH + i] = sd.amt[i];
        }
        d[2 * NPATCH] = sd.pon;
        break;
    default:
        for (i = 0; i < NSC; i++)
            d[i] = sd.sc[i];
        break;
    }
}
static void pre_fact(int k, int i, uint8_t *d)   /* a factory preset as bytes */
{
    int j;
    switch (k) {
    case PK_SYN:
        for (j = 0; j < NSYN; j++)
            d[j] = SPRE[i].v[j];
        break;
    case PK_KICK:
        for (j = 0; j < NKICK; j++)
            d[j] = KPRE[i].v[j];
        break;
    case PK_FX:
        for (j = 0; j < NFX; j++)
            d[j] = FPRE[i].v[j];
        d[NFX] = FPRE[i].en;
        break;
    case PK_PATCH:
        for (j = 0; j < NPATCH; j++) {
            d[j] = PPRE[i].src[j];
            d[NPATCH + j] = PPRE[i].amt[j];
        }
        d[2 * NPATCH] = PPRE[i].pon;
        break;
    default:
        for (j = 0; j < NSC; j++)
            d[j] = CPRE[i].v[j];
        break;
    }
}
static void pre_put(int k, const uint8_t *d)     /* bytes into the live settings */
{
    int i;
    switch (k) {
    case PK_SYN:
        for (i = 0; i < NSYN; i++)
            syn[i] = d[i];
        break;
    case PK_KICK:
        for (i = 0; i < NKICK; i++)
            kick_p[i] = d[i];
        break;
    case PK_FX:
        for (i = 0; i < NFX; i++)
            sd.fx[i] = d[i];
        sd.fxen = (uint8_t)((d[NFX] & ~FX_STU) | (sd.fxen & FX_STU));      /* the stutter stays as it is */
        break;
    case PK_PATCH:
        for (i = 0; i < NPATCH; i++) {
            sd.src[i] = d[i];
            sd.amt[i] = d[NPATCH + i];
        }
        sd.pon = d[2 * NPATCH];
        break;
    default:
        for (i = 0; i < NSC; i++)
            sd.sc[i] = d[i];
        break;
    }
}
static void pre_apply(int k, int i)      /* load preset i of a kind (and say it is the current one) */
{
    uint8_t d[28];
    int n = pre_count(k);
    i = (i % n + n) % n;
    if (!pre_used(k, i))
        return;
    if (i < NFACT[k]) {
        pre_fact(k, i, d);
    } else {
        for (int j = 0; j < 27; j++)
            d[j] = up[k].s[i - NFACT[k]].d[j];
    }
    pre_put(k, d);
    sd.pre[k] = (uint8_t)i;
    publish();
}
static int pre_same(int k, int i)        /* is what is playing of a kind still the preset i? */
{
    uint8_t d[28], c[28];
    int n = PSZ[k];
    if (i < NFACT[k]) {
        pre_fact(k, i, d);
    } else {
        for (int j = 0; j < 27; j++)
            d[j] = up[k].s[i - NFACT[k]].d[j];
    }
    pre_cap(k, c);
    if (k == PK_FX)
        d[NFX] &= (uint8_t)~FX_STU;
    for (int j = 0; j < n; j++)
        if (c[j] != d[j])
            return 0;
    return 1;
}
static void pre_step(int k, int dir)     /* the next used preset of a kind */
{
    int n = pre_count(k), i = sd.pre[k];
    for (int t = 0; t < n; t++) {
        i = (i + dir + n) % n;
        if (pre_used(k, i)) {
            pre_apply(k, i);
            return;
        }
    }
}

static void pat_clear(pat_t *p)          /* the steps empty; the sound of the pattern is left as it is */
{
    int i;
    for (i = 0; i < (int)offsetof(pat_t, pre); i++)
        ((uint8_t *)p)[i] = 0;
    for (i = 0; i < NSTEP; i++) {
        p->vel[i] = 100;
        p->gate[i] = GATE_FULL;
        p->kvol[i] = KVOL_DEF;
    }
    p->len = LEN_DEF;
    p->klen = KLEN_DEF;
}
static void pat_wipe(pat_t *p)           /* everything: a pattern that was never used (it takes the sound that is playing when it is first visited) */
{
    for (int i = 0; i < (int)sizeof *p; i++)
        ((uint8_t *)p)[i] = 0;
    pat_clear(p);
}
static void pat_demo(pat_t *p)           /* a first pattern, so the unit makes sound at once */
{
    static const uint8_t T[LEN_DEF] = {2, 0, 1, 1, 0, 0, 1, 0};
    static const int8_t P[LEN_DEF] = {0, 0, 12, 0, 0, 0, 7, 0};
    pat_clear(p);
    for (int i = 0; i < LEN_DEF; i++) {
        p->trig[i] = T[i];
        p->pit[i] = P[i];
    }
    for (int i = 0; i < KLEN_DEF; i += 4)
        p->ktrig[i] = 1;
}

/* the random pages start as a whole new draw (100 %), every knob on, but: the steps are a pitch and a velocity (a page of
 * its own for the rests), the kick sequence only the steps, the patch bay only a set of new cables, and the scale, the
 * presets and the tempo stay out (so a random does not change the key or the speed). RND_VER marks these settings: a
 * sound saved with an older one gets them again once. */
#define RND_VER 12
static void rnd_defaults(void)
{
    for (int i = 0; i < NRP; i++) {
        sd.rnd_en[i] = 0x0F;
        for (int k = 0; k < 4; k++)
            sd.rnd_amt[i][k] = 127;
    }
    sd.rnd_en[0] = 0x03;                 /* SEQ page 1: pitch and velocity (gate and ratchet are not part of a draw) */
    sd.rnd_en[1] = 0x02;                 /* SEQ page 2: the rests */
    sd.rnd_en[8] = 0x01;                 /* the patch bay: two to four new cables */
    sd.rnd_en[12] = 0x01;                /* KICK SEQ: the steps */
    sd.rnd_en[20] = 0;                   /* SCALE */
    sd.rnd_en[21] = 0;
    sd.rnd_en[22] = 0;                   /* GLOBAL: BPM, preset changes */
    sd.rsv0 = RND_VER;
}

static void sound_defaults(void)         /* the sound of a first power-on: the first synth, kick and FX presets, no cables, C major */
{
    int i;
    for (i = 0; i < NPATCH; i++) {
        sd.src[i] = S_OFF;
        sd.amt[i] = 64;
    }
    sd.pon = 0;
    sd.sc[SC_ROOT] = 0;
    sd.sc[SC_TYPE] = 1;
    sd.sc[SC_OCT] = 2;
    sd.sc[SC_QUANT] = 0;
    sd.sc[SC_TRANS] = 12;
    sd.sc[SC_FINE] = 50;
    sd.sc[SC_HUM] = 0;
    sd.sc[SC_SPREAD] = 64;
    for (i = 0; i < NPK; i++)
        if (i != PK_SCALE && i != PK_PATCH)
            pre_apply(i, 0);
    sd.pre[PK_PATCH] = 0;
    sd.pre[PK_SCALE] = 0;
}

static void defaults(void)
{
    int i;
    for (i = 0; i < (int)sizeof sd; i++)
        ((uint8_t *)&sd)[i] = 0;
    sd.magic = SND_MAGIC;
    sound_defaults();
    sd.bpm = 118;
    sd.song_on = 0;
    for (i = 0; i < 32; i++)
        sd.song[i] = 0xFF;
    rnd_defaults();
    scopy(sd.pname, "PROJECT", PN);
    for (i = 0; i < NPAT; i++)
        pat_wipe(&pats[i]);
    pat_demo(&pats[0]);
}

static void publish(void)                /* what is saved into what the engine plays */
{
    int i;
    for (i = 0; i < NSYN; i++)
        syn_eff[i] = syn[i];
    for (i = 0; i < NFX; i++)
        fx_p[i] = sd.fx[i];
    for (i = 0; i < NPATCH; i++) {
        patch_src[i] = (sd.pon >> i) & 1u ? sd.src[i] : S_OFF;
        patch_amt[i] = sd.amt[i];
    }
    fx_en = sd.fxen;
}

/* ------------------------------------------------------------- sequencer --- */
static struct {
    volatile uint8_t running;
    volatile uint8_t step;               /* the synth step playing (or last played) */
    volatile uint8_t kstep;              /* and the kick's, in its own sequence */
    volatile uint8_t cur;                /* the pattern playing */
    volatile int16_t queued;             /* the pattern for the next bar, or -1 */
    volatile uint8_t song_pos;
    volatile uint8_t recalled;           /* a pattern's own sound came up (the screen redraws) */
    volatile uint32_t bar, kbar;
    float left, len, dur;                /* samples to the next step; one 16th; the step just started */
    float rleft, rper, rgate;            /* the ratchet: samples to the next hit, between hits, the gate of a hit */
    uint32_t rn;                         /* hits still to come in this step */
    float rnote, rvel, rseq;
    int racc;
    float kleft, kper;                   /* the kick's ratchet */
    float kpit, kgain;                   /* the step's pitch and volume, for its repeats */
    uint32_t kn;
    volatile uint32_t trigs;             /* synth notes started since boot (the host's checks) */
    volatile uint32_t ktrigs;
    volatile uint8_t mute_k, mute_s;     /* OCT+ and the last, the last but one white key: the kick, the synth make no sound */
    volatile uint8_t rec;                /* OCT- + SAVE in SEQ: the MIDI notes that come in are written into the steps */
    volatile uint8_t rec_wrote;          /* a note was written (the main loop marks the pattern changed) */
    uint8_t skip_s;                      /* a step a note was written into just before it plays (it has sounded): 1 + its index */
} sq;
static uint8_t rec_sel0, *rec_sel = &rec_sel0;   /* the selected step (ui.sel, once the UI is up): where a note goes when stopped */

/* MIDI sync (GLO page 6): with it on, the clock that comes in (USB, the MIDI jack, Bluetooth) plays the steps, a 16th
 * every six clocks, and its start, continue and stop run the sequencer. Each clock has the time it landed on the FM-1
 * (fm1.toml midi_stamp): a step sounds XS_LAT samples after its clock, the same for every step whenever in the audio
 * block the clock came, so the steps keep the clock's own spacing. Without a clock for half a second the tempo is the
 * FM-1's own again. The render (the audio interrupt) owns all of it but `on`. */
#ifndef FM1B_MIDI_STAMP
#define FM1B_MIDI_STAMP 0
#endif
#if FM1B_MIDI_STAMP
#define XS_UNIT (24.0f * 1000000.0f / 44100.0f)      /* the clock's times are in plat_ticks24's ticks: this many a sample */
#else
#define XS_UNIT 1.0f                     /* (no stamps, the host: in samples, as the render reads them) */
#endif
#define XS_LAT 288.0f                    /* a block and a bit: a clock is read at most that long after it landed */
#define XS_WAIT 1.0e9f                   /* sq.left while the next step waits for the clock */
#define XS_WIN 96                        /* the tempo is measured over this many clocks (a bar), once twelve have come */
static struct {
    volatile uint8_t on;                 /* the setting (kept with Bluetooth's, in OBJ_BT) */
    volatile uint8_t live;               /* a clock is coming in */
    volatile uint16_t bpm;               /* its tempo to the nearest BPM, 0 not measured yet */
    uint8_t ck;                          /* clocks since the last 16th, 0..5 */
    uint8_t c24;                         /* clocks since the last quarter note, 0..23 (the pulse sync's count) */
    uint8_t n, w;                        /* clock times kept, and where the next one goes */
    uint32_t now, last;                  /* samples rendered; when the last clock was read */
    uint32_t tnow;                       /* this block's start, on the clock's time base */
    float per;                           /* samples a clock, as measured (0 not yet) */
    uint32_t t[XS_WIN];
} xs;
static volatile uint32_t loud_at;        /* xs.now when the output was last above a whisper (the autosave waits for silence) */

static volatile uint8_t midi_set[8];     /* the MIDI pages' settings (kept in OBJ_BT, btst_t.set): */
#define MS_INCH midi_set[0]              /* notes in: 0 every channel, 1..16 that one */
#define MS_OUTCH midi_set[1]             /* the synth's steps out as notes: 0 off, 1..16 the channel */
#define MS_KICKCH midi_set[2]            /* the kick's steps out as note 36: 0 off, 1..16 the channel */
#define MS_CKOUT midi_set[3]             /* 1: the MIDI clock, start and stop go out */
#define MS_PULSE midi_set[4]             /* the pulse sync on the jack: 0 off, 1..4 = 1, 2, 4, 24 pulses a quarter note */
static const uint8_t PULSE_PPQN[5] = {0, 1, 2, 4, 24};

/* What goes out. The clock: 24 a quarter note while the sequencer runs on its own tempo, counted in samples by the render
 * (so it cannot drift from the steps); each is handed, with the time it is due, to the 1 kHz tick, which sends it (on the
 * host the render sends them, a block at a time). Start goes out when the sequencer starts, stop when it stops. Following a
 * clock that comes in, the FM-1 sends none. The pulse sync: a 5 ms pulse on the left of the FM-1's own output (the tip of
 * the jack) every 24 / n clocks, the sound in mono on the right; app_line_out writes it, sample by sample. The notes: the
 * synth's steps on a channel, the kick's as note 36 on another. */
#define CO_LAT 256.0f                    /* a clock leaves this many samples after the render placed it: when its block sounds */
#define CO_Q 16u
#define PULSE_LEN 220                    /* samples: 5 ms */
#define PULSE_AMP 7500000                /* of 8388607 */
static struct {
    float left;                          /* samples to the next clock */
    uint32_t n;                          /* clocks since the start */
    uint32_t q[CO_Q];                    /* when each is due (XS_UNIT's time base) */
    volatile uint8_t head, tail;         /* the render puts at head, the tick takes from tail */
    uint8_t ext_start;                   /* seq_start comes from the clock that comes in: no start goes out */
    float pwait;                         /* samples from the block's start to the next pulse (< 0: none due) */
    int32_t pleft;                       /* samples of pulse still to write */
    uint32_t pulses;                     /* pulses written (a test counts them) */
    uint32_t notes_in;                   /* note-ons taken (a test counts them) */
    int8_t note, knote;                  /* the notes sounding out there (-1 none), */
    uint8_t nch, kch;                    /* the channels they went out on, */
    float nleft, kleft;                  /* and the samples until their note-offs */
} co = {.pwait = -1.0f, .note = -1, .knote = -1};
static void mo_send(uint32_t status, uint32_t d1, uint32_t d2)
{
    plat_midi_send((status >> 4) | (status << 8) | (d1 << 16) | (d2 << 24), TO_USB | TO_BLE);
}
static void mo_rt(uint32_t b) { plat_midi_send(0x0Fu | (b << 8), TO_USB | TO_BLE); }
static void mo_offs(void)                /* the notes out there end */
{
    if (co.note >= 0)
        mo_send(0x80u | co.nch, (uint32_t)co.note, 0);
    if (co.knote >= 0)
        mo_send(0x80u | co.kch, (uint32_t)co.knote, 0);
    co.note = co.knote = -1;
}
static void mo_note(float note, float vel, float len)    /* a hit of the synth: its note out */
{
    int nn = (int)(note + 0.5f), v = (int)(vel * 127.0f + 0.5f);
    if (!MS_OUTCH || plat_midi_out_room(TO_USB | TO_BLE) < 4u)
        return;
    if (co.note >= 0)
        mo_send(0x80u | co.nch, (uint32_t)co.note, 0);
    co.nch = (uint8_t)(MS_OUTCH - 1u);
    co.note = (int8_t)(nn < 0 ? 0 : nn > 127 ? 127 : nn);
    co.nleft = len;
    mo_send(0x90u | co.nch, (uint32_t)co.note, (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v));
}
static void mo_kick(float gain, int acc) /* a hit of the kick: note 36 out */
{
    int v = (int)(gain * (acc ? 127.0f : 100.0f) + 0.5f);
    if (!MS_KICKCH || plat_midi_out_room(TO_USB | TO_BLE) < 4u)
        return;
    if (co.knote >= 0)
        mo_send(0x80u | co.kch, (uint32_t)co.knote, 0);
    co.kch = (uint8_t)(MS_KICKCH - 1u);
    co.knote = 36;
    co.kleft = 2205.0f;                  /* 50 ms */
    mo_send(0x90u | co.kch, 36u, (uint32_t)(v < 1 ? 1 : v > 127 ? 127 : v));
}
static void clock_send(uint32_t now)     /* the clocks that are due leave (the tick; the render on the host) */
{
    while (co.tail != co.head && (int32_t)(now - co.q[co.tail]) >= 0) {
        mo_rt(0xF8u);
        co.tail = (uint8_t)((co.tail + 1u) % CO_Q);
    }
}

static void tempo_set(void)
{
    sq.len = 44100.0f * 60.0f / ((float)sd.bpm * 4.0f);
    dsp_tempo(sq.len);
}

static int cond_pass(uint8_t c, uint32_t bar)
{
    if (c >= NCOND)
        return 1;
    if (!COND_N[c])
        return (rnd() & 1u) != 0;
    return (bar % COND_N[c]) + 1u == COND_I[c];
}

/* Every pattern has a sound of its own. The sound being edited (sd, kick_p) is the copy of the pattern playing: it is kept
 * in the pattern (snd_sync) before another one comes up, and the one coming up loads its own (snd_pull). A pattern that
 * was never visited has none: it takes the sound that is playing, as its own, the first time. */
static const uint8_t SOFF[NPK] = {0, NSYN, NSYN + NKICK, NSYN + NKICK + NFX + 1, NSYN + NKICK + NFX + 1 + 2 * NPATCH + 1};
static void snd_cap(uint8_t *d)
{
    for (int k = 0; k < NPK; k++)
        pre_cap(k, d + SOFF[k]);
}
static void snd_sync_to(int pi)          /* the sound being played, into pattern pi's own copy (and its marks of where it came from) */
{
    pat_t *p = &pats[pi];
    uint8_t d[SNDSZ];
    int k, same = p->sok;
    snd_cap(d);
    for (k = 0; same && k < SNDSZ; k++)
        same = d[k] == p->snd[k];
    for (k = 0; same && k < NPK; k++)
        same = p->pre[k] == sd.pre[k] + 1;
    if (same)
        return;
    for (k = 0; k < SNDSZ; k++)
        p->snd[k] = d[k];
    for (k = 0; k < NPK; k++)
        p->pre[k] = (uint8_t)(sd.pre[k] + 1);
    p->sok = 1;
    touch();
}
static void snd_sync(void)               /* (the main loop: the audio interrupt may switch the pattern meanwhile) */
{
    uint32_t f = plat_lock();
    snd_sync_to(sq.cur);
    plat_unlock(f);
}
static void snd_pull(int pi)             /* the pattern's own sound into the live one; none yet: the live one becomes its own */
{
    const pat_t *p = &pats[pi];
    if (!p->sok) {
        snd_sync_to(pi);
    } else {
        for (int k = 0; k < NPK; k++) {
            pre_put(k, p->snd + SOFF[k]);
            if (p->pre[k] && p->pre[k] <= pre_count(k) && pre_used(k, p->pre[k] - 1))
                sd.pre[k] = (uint8_t)(p->pre[k] - 1);
        }
        publish();
    }
    sq.recalled = 1;
}

static float note_for(int pit)           /* a step's pitch into a MIDI note: spread, the scale, the transposition, the fine tune */
{
    int n = pit, sp = sd.sc[SC_SPREAD];
    if (sp != 64)
        n = (2 * n * sp + (n < 0 ? -64 : 64)) / 128;
    n += 48;
    if (sd.sc[SC_QUANT])
        n = scale_snap(n);
    n += ((int)sd.sc[SC_TRANS] - 12) + ((int)sd.sc[SC_OCT] - 2) * 12;
    n = clamp(n, 12, 120);
    return (float)n + ((float)sd.sc[SC_FINE] - 50.0f) * 0.01f;
}

static void kick_trigger(const pat_t *p, uint32_t s)
{
    sq.kn = 0;
    if (p->ktrig[s] && !sq.mute_k && cond_pass(p->kcond[s], sq.kbar)) {
        uint32_t hits = (uint32_t)(p->krat[s] & 3u) + 1u;
        sq.kpit = (float)p->kpit[s];
        sq.kgain = (float)p->kvol[s] * (1.0f / 127.0f);
        dsp_kick_hit(1.0f, p->ktrig[s] == 2, sq.kpit, sq.kgain);
        mo_kick(sq.kgain, p->ktrig[s] == 2);
        sq.ktrigs++;
        if (hits > 1u) {
            sq.kn = hits - 1u;
            sq.kper = sq.dur / (float)hits;
            sq.kleft = sq.kper;
        }
    }
}

static void step_trigger(uint32_t s)
{
    pat_t *p = &pats[sq.cur];
    sq.rn = 0;
    if (sq.skip_s && sq.skip_s - 1u == s) {      /* (a recorded note, already heard: once) */
        sq.skip_s = 0;
        return;
    }
    if (p->trig[s] && p->vel[s] && !sq.mute_s && cond_pass(p->cond[s], sq.bar)) {
        uint32_t hits = (uint32_t)(p->rat[s] & 3u) + 1u;
        float per = sq.dur / (float)hits, gate = 0.0f;
        int v = p->vel[s], h = sd.sc[SC_HUM] / 4;
        for (int k = 0; k < NSYN; k++)
            syn_eff[k] = syn[k];
        for (int l = 0; l < 2; l++)
            if (p->lk_p[s][l] && p->lk_p[s][l] <= NSYN)
                syn_eff[p->lk_p[s][l] - 1] = p->lk_v[s][l];
        if (p->gate[s] < GATE_FULL)                  /* 32 = one whole step, split over the hits */
            gate = (float)p->gate[s] * (1.0f / 32.0f) * per;
        if (h)
            v = clamp(v + (int)(rnd() % (2u * (uint32_t)h + 1u)) - h, 1, 127);
        sq.rnote = note_for(p->pit[s]);
        sq.rvel = (float)v * (1.0f / 127.0f);
        sq.rseq = (float)p->pit[s];
        sq.racc = p->trig[s] == 2;
        dsp_syn_trig(sq.rnote, sq.rvel, sq.racc, sq.rseq, gate);
        mo_note(sq.rnote, sq.rvel, gate > 0.0f ? gate : per * 0.9f);
        sq.trigs++;
        if (hits > 1u) {
            sq.rn = hits - 1u;
            sq.rper = per;
            sq.rleft = per;
            sq.rgate = gate;
        }
    }
}

static void ratchet_hit(void)
{
    dsp_syn_trig(sq.rnote, sq.rvel * 0.9f, 0, sq.rseq, sq.rgate);
    mo_note(sq.rnote, sq.rvel * 0.9f, sq.rgate > 0.0f ? sq.rgate : sq.rper * 0.9f);
    sq.trigs++;
}

static void seq_step(void)
{
    uint32_t s = (uint32_t)sq.step + 1u, len = pats[sq.cur].len, ks;
    uint8_t old = sq.cur;
    if (s >= len) {                      /* a new bar: the queued pattern or the song's next */
        s = 0;
        sq.bar++;
        if (sd.song_on) {
            int i, n = sq.song_pos;
            for (i = 0; i < 32; i++) {
                n = (n + 1) & 31;
                if (sd.song[n] < NPAT) {
                    sq.song_pos = (uint8_t)n;
                    sq.cur = sd.song[n];
                    break;
                }
            }
        } else if (sq.queued >= 0) {
            sq.cur = (uint8_t)sq.queued;
            sq.queued = -1;
        }
        if (sq.cur != old) {             /* another pattern: its kick sequence starts over, and its own sound plays */
            sq.kstep = (uint8_t)(pats[sq.cur].klen - 1u);
            snd_sync_to(old);
            snd_pull(sq.cur);
        }
    }
    sq.step = (uint8_t)s;
    {                                    /* swing: the odd steps late, the even ones long, the pair as long as ever */
        float sw = (float)pats[sq.cur].swing * (0.33f / 127.0f);
        sq.dur = sq.len * ((s & 1u) ? 1.0f - sw : 1.0f + sw);
    }
    ks = (uint32_t)sq.kstep + 1u;        /* the kick's own sequence: its length, its bars */
    if (ks >= pats[sq.cur].klen) {
        ks = 0;
        sq.kbar++;
    }
    sq.kstep = (uint8_t)ks;
    kick_trigger(&pats[sq.cur], ks);
    step_trigger(s);
}

static void seq_rewind(void)
{
    sq.step = (uint8_t)(pats[sq.cur].len - 1u);
    sq.kstep = (uint8_t)(pats[sq.cur].klen - 1u);
    sq.bar = (uint32_t)-1;               /* the first bar is bar 0 once the step wraps */
    sq.kbar = (uint32_t)-1;
    sq.left = xs.on && xs.live ? XS_WAIT : 0.0f;     /* (a clock coming in: the first step at its next 16th) */
    sq.rn = 0;
    sq.kn = 0;
}
static void seq_start(void)
{
    seq_rewind();
    if (sd.song_on) {
        int i;
        for (i = 0; i < 32; i++)
            if (sd.song[i] < NPAT) {
                sq.song_pos = (uint8_t)((i + 31) & 31);
                break;
            }
    }
    sq.running = 1;
    co.left = 0.0f;                      /* the clock out: its first with the first step */
    co.n = 0;
    co.tail = co.head;
    if (MS_CKOUT && !co.ext_start)
        mo_rt(0xFAu);
}
static void seq_stop(void)
{
    if (sq.running && MS_CKOUT && !(xs.on && xs.live))
        mo_rt(0xFCu);
    sq.running = 0;
    sq.rn = 0;
    sq.kn = 0;
    co.tail = co.head;
    co.pwait = -1.0f;
    mo_offs();
}

/* --------------------------------------------------------------- store --- */
#define PJ_OBJ(p, j) (OBJ_PROJ0 + (uint32_t)(p) * 4u + (uint32_t)(j))

static void snd_clean(uint8_t *d)        /* a stored sound with every value in its range */
{
    int i;
    for (i = 0; i < NSYN + NKICK; i++)
        d[i] &= 127u;
    for (i = 0; i < NFX; i++)
        d[SOFF[PK_FX] + i] &= 127u;
    d[SOFF[PK_FX] + NFX] &= (uint8_t)(FX_CMP | FX_DLY | FX_REV | FX_DJF | FX_LIM);
    for (i = 0; i < NPATCH; i++) {
        if (d[SOFF[PK_PATCH] + i] >= NSRC)
            d[SOFF[PK_PATCH] + i] = S_OFF;
        d[SOFF[PK_PATCH] + NPATCH + i] &= 127u;
    }
    d[SOFF[PK_SCALE] + SC_ROOT] %= 12;
    d[SOFF[PK_SCALE] + SC_TYPE] %= NSCALE;
    if (d[SOFF[PK_SCALE] + SC_OCT] > 4)
        d[SOFF[PK_SCALE] + SC_OCT] = 2;
    if (d[SOFF[PK_SCALE] + SC_QUANT] > 1)
        d[SOFF[PK_SCALE] + SC_QUANT] = 0;
    if (d[SOFF[PK_SCALE] + SC_TRANS] > 24)
        d[SOFF[PK_SCALE] + SC_TRANS] = 12;
    if (d[SOFF[PK_SCALE] + SC_FINE] > 100)
        d[SOFF[PK_SCALE] + SC_FINE] = 50;
    d[SOFF[PK_SCALE] + SC_HUM] &= 127u;
    d[SOFF[PK_SCALE] + SC_SPREAD] &= 127u;
}

static void sanitize(void)
{
    int i;
    if (sd.bpm < 40 || sd.bpm > 240)
        sd.bpm = 118;
    sd.sc[SC_ROOT] %= 12;
    sd.sc[SC_TYPE] %= NSCALE;
    if (sd.sc[SC_OCT] > 4)
        sd.sc[SC_OCT] = 2;
    if (sd.sc[SC_QUANT] > 1)
        sd.sc[SC_QUANT] = 0;
    if (sd.sc[SC_TRANS] > 24)
        sd.sc[SC_TRANS] = 12;
    if (sd.sc[SC_FINE] > 100)
        sd.sc[SC_FINE] = 50;
    for (i = 0; i < NPK; i++)
        if (sd.pre[i] >= pre_count(i))
            sd.pre[i] = 0;
    for (i = 0; i < NRP; i++)
        for (int k = 0; k < 4; k++)
            if (sd.rnd_amt[i][k] > 127)
                sd.rnd_amt[i][k] = 64;
    if (sd.rsv0 != RND_VER)              /* the random settings of an older version */
        rnd_defaults();
    sd.pname[PN - 1] = 0;
    for (i = 0; i < NPAT; i++) {
        if (pats[i].len < 1 || pats[i].len > NSTEP)
            pats[i].len = LEN_DEF;
        if (pats[i].klen < 1 || pats[i].klen > NSTEP)
            pats[i].klen = KLEN_DEF;
        for (int k = 0; k < NPK; k++)
            if (pats[i].pre[k] > pre_count(k))
                pats[i].pre[k] = 0;
        for (int k = 0; k < NSTEP; k++) {
            if (pats[i].cond[k] >= NCOND)
                pats[i].cond[k] = 0;
            if (pats[i].kcond[k] >= NCOND)
                pats[i].kcond[k] = 0;
            pats[i].kpit[k] = (int8_t)clamp(pats[i].kpit[k], -24, 24);
            if (pats[i].kvol[k] > 127)
                pats[i].kvol[k] = KVOL_DEF;
        }
        snd_clean(pats[i].snd);
    }
}

/* a pattern as stored (FP5): for each step 72 bits (9 bytes, filled from the low bit): trig, kick trig, ratchet, kick ratchet 2 each,
 * condition and kick condition 4 each, pitch 6 (two's complement), velocity 7, gate 6, the two locked parameters 5 each and their
 * values 7 each, the kick's pitch 6 (two's complement) and volume 7. Then length, swing, kick length (the top bit of the length:
 * the pattern has a sound of its own), the marks of the presets, and the sound. */
static void bits_put(uint8_t *o, uint32_t *pos, uint32_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++, (*pos)++)
        if ((v >> i) & 1u)
            o[*pos >> 3] |= (uint8_t)(1u << (*pos & 7u));
}
static uint32_t bits_get(const uint8_t *o, uint32_t *pos, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++, (*pos)++)
        v |= (uint32_t)((o[*pos >> 3] >> (*pos & 7u)) & 1u) << i;
    return v;
}
static int32_t sext6(uint32_t v) { return (int32_t)(v & 31u) - (int32_t)(v & 32u); }   /* 6 bits, two's complement */
static void pat_pack(const pat_t *p, uint8_t *o)
{
    int i;
    for (i = 0; i < NSTEP; i++, o += 9) {
        uint32_t b = 0;
        for (int j = 0; j < 9; j++)
            o[j] = 0;
        bits_put(o, &b, p->trig[i], 2);
        bits_put(o, &b, p->ktrig[i], 2);
        bits_put(o, &b, p->rat[i], 2);
        bits_put(o, &b, p->krat[i], 2);
        bits_put(o, &b, p->cond[i], 4);
        bits_put(o, &b, p->kcond[i], 4);
        bits_put(o, &b, (uint32_t)p->pit[i], 6);
        bits_put(o, &b, p->vel[i], 7);
        bits_put(o, &b, p->gate[i], 6);
        bits_put(o, &b, p->lk_p[i][0], 5);
        bits_put(o, &b, p->lk_p[i][1], 5);
        bits_put(o, &b, p->lk_v[i][0], 7);
        bits_put(o, &b, p->lk_v[i][1], 7);
        bits_put(o, &b, (uint32_t)p->kpit[i], 6);
        bits_put(o, &b, p->kvol[i], 7);
    }
    o[0] = (uint8_t)(p->len | (p->sok ? 0x80u : 0u));
    o[1] = p->swing;
    o[2] = p->klen;
    for (i = 0; i < NPK; i++)
        o[3 + i] = p->pre[i];
    for (i = 0; i < SNDSZ; i++)
        o[3 + NPK + i] = p->snd[i];
}
static void pat_tail(pat_t *p, const uint8_t *o)     /* what follows the steps, the same in FP4 and FP5 */
{
    p->len = o[0] & 0x7Fu;
    p->sok = (o[0] & 0x80u) != 0;
    p->swing = o[1];
    p->klen = o[2];
    for (int i = 0; i < NPK; i++)
        p->pre[i] = o[3 + i];
    for (int i = 0; i < SNDSZ; i++)
        p->snd[i] = o[3 + NPK + i];
}
static void pat_unpack(pat_t *p, const uint8_t *o)
{
    int i;
    for (i = 0; i < NSTEP; i++, o += 9) {
        uint32_t b = 0;
        p->trig[i] = (uint8_t)bits_get(o, &b, 2);
        p->ktrig[i] = (uint8_t)bits_get(o, &b, 2);
        p->rat[i] = (uint8_t)bits_get(o, &b, 2);
        p->krat[i] = (uint8_t)bits_get(o, &b, 2);
        p->cond[i] = (uint8_t)bits_get(o, &b, 4);
        p->kcond[i] = (uint8_t)bits_get(o, &b, 4);
        p->pit[i] = (int8_t)sext6(bits_get(o, &b, 6));
        p->vel[i] = (uint8_t)bits_get(o, &b, 7);
        p->gate[i] = (uint8_t)bits_get(o, &b, 6);
        p->lk_p[i][0] = (uint8_t)bits_get(o, &b, 5);
        p->lk_p[i][1] = (uint8_t)bits_get(o, &b, 5);
        p->lk_v[i][0] = (uint8_t)bits_get(o, &b, 7);
        p->lk_v[i][1] = (uint8_t)bits_get(o, &b, 7);
        p->kpit[i] = (int8_t)sext6(bits_get(o, &b, 6));
        p->kvol[i] = (uint8_t)bits_get(o, &b, 7);
    }
    pat_tail(p, o);
}
static void pat_unpack4(pat_t *p, const uint8_t *o)  /* a pattern of FP4 (v0.9 to v0.17): 9 bytes a step, no kick pitch or volume */
{
    int i;
    for (i = 0; i < NSTEP; i++, o += 9) {
        p->trig[i] = o[0] & 3u;
        p->ktrig[i] = (o[0] >> 2) & 3u;
        p->rat[i] = (o[0] >> 4) & 3u;
        p->krat[i] = (o[0] >> 6) & 3u;
        p->cond[i] = o[1] & 15u;
        p->kcond[i] = o[1] >> 4;
        p->pit[i] = (int8_t)o[2];
        p->vel[i] = o[3];
        p->gate[i] = o[4];
        p->lk_p[i][0] = o[5];
        p->lk_p[i][1] = o[6];
        p->lk_v[i][0] = o[7];
        p->lk_v[i][1] = o[8];
        p->kpit[i] = 0;
        p->kvol[i] = KVOL_DEF;
    }
    pat_tail(p, o);
}
static void pat_from_v8(pat_t *p, const pat8_t *q)   /* a v0.8 pattern: the steps; no sound of its own yet */
{
    int i;
    pat_wipe(p);
    for (i = 0; i < NSTEP; i++) {
        p->trig[i] = q->trig[i];
        p->pit[i] = (int8_t)q->pit[i];
        p->vel[i] = q->vel[i];
        p->cond[i] = q->cond[i];
        p->ktrig[i] = q->ktrig[i];
        p->gate[i] = q->gate[i];
        p->rat[i] = q->rat[i];
        p->lk_p[i][0] = q->lk_p[i][0];
        p->lk_p[i][1] = q->lk_p[i][1];
        p->lk_v[i][0] = q->lk_v[i][0];
        p->lk_v[i][1] = q->lk_v[i][1];
        p->kcond[i] = q->kcond[i];
        p->krat[i] = q->krat[i];
    }
    p->len = q->len;
    p->swing = q->swing;
    p->klen = q->klen;
}
static uint32_t sig_of(const void *v, uint32_t n)    /* a hash of bytes (FNV-1a) */
{
    const uint8_t *b = (const uint8_t *)v;
    uint32_t h = 2166136261u;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static void bank_pack(int first)         /* the 16 patterns from pattern FIRST on, into bk_buf */
{
    bk_buf.magic = PAT_MAGIC;
    for (int j = 0; j < NPB; j++)
        pat_pack(&pats[first + j], bk_buf.d[j]);
}
static int bank_load(uint32_t obj, int first)    /* the object's 16 patterns into pats[first..]; 0 when it holds none (they are cleared) */
{
    int n = plat_store_load(obj, &bk_buf, sizeof bk_buf), ok = 0;
    if (n == (int)sizeof bk_buf && bk_buf.magic == PAT_MAGIC) {
        for (int j = 0; j < NPB; j++)
            pat_unpack(&pats[first + j], bk_buf.d[j]);
        ok = 1;
    } else if (n == (int)sizeof bk_buf && bk_buf.magic == PAT_MAGIC4) {
        for (int j = 0; j < NPB; j++)
            pat_unpack4(&pats[first + j], bk_buf.d[j]);
        ok = 1;
    } else if (n == (int)(sizeof(uint32_t) + NPB * sizeof(pat8_t)) && bk_buf.magic == PAT_MAGIC8) {
        for (int j = 0; j < NPB; j++) {
            pat8_t q;
            const uint8_t *src = (const uint8_t *)&bk_buf + sizeof(uint32_t) + (uint32_t)j * sizeof(pat8_t);
            for (uint32_t i = 0; i < sizeof q; i++)
                ((uint8_t *)&q)[i] = src[i];
            pat_from_v8(&pats[first + j], &q);
        }
        ok = 1;
    }
    if (!ok)
        for (int j = 0; j < NPB; j++)
            pat_wipe(&pats[first + j]);
    return ok;
}
static void sd_sync_kick(void)
{
    for (int i = 0; i < NKICK; i++)
        sd.kick[i] = kick_p[i];
    sd.magic = SND_MAGIC;
}
static void sigs_init(void)              /* the objects as they are now count as saved */
{
    sd_sync_kick();
    snd_sig = sig_of(&sd, sizeof sd);
    for (int i = 0; i < NBANK; i++) {
        bank_pack(i * NPB);
        bank_sig[i] = sig_of(&bk_buf, sizeof bk_buf);
    }
}

/* ---- Bluetooth: the sound to headphones and speakers (plat.h plat_bta_*) ---- */
/* One object of the app's own: whether the radio starts at power-on (it does only if the player left it on) and what the
 * radio keeps (its addresses, the bond, the four audio devices remembered). The radio's changes are held here and written
 * when the sequencer is stopped and the player has stopped touching things: a write blocks the main loop. */
#define BT_MAGIC 0x31544246u             /* "FBT1" */
typedef struct {
    uint32_t magic;
    uint8_t on, has, quality, midioff;   /* on: Bluetooth is on; has: r holds what the radio kept; quality: 0 HIGH 1 MID 2 LOW (the
                                          * stream's bits); midioff: the Bluetooth MIDI advertising is off while headphones play */
    struct plat_ble_rec r;
    uint8_t set[8];                      /* the MIDI pages' settings (MS_*): added in 0.2.7, a record without them reads as zeros */
} btst_t;
static btst_t bts;
#define BT_QUAL ((uint32_t)bts.quality & 3u)   /* the stream's quality (bit 7 of the byte is the MIDI sync) */
#define BQ_SYNC 0x80u                    /* the same byte's other bits: the MIDI sync on, */
#define BQ_LINE_OFF 0x40u                /* the speaker and the jack off, */
#define BQ_USB_OFF 0x20u                 /* the USB recording off, */
#define BQ_USB_FULL 0x10u                /* the USB recording at full level, whatever MASTER says */
static void out_apply(void)              /* the AUDIO page's switches to the platform */
{
    plat_out_off(((bts.quality & BQ_USB_OFF) ? PLAT_OUT_USB : 0u) | ((bts.quality & BQ_LINE_OFF) ? PLAT_OUT_LINE : 0u));
    plat_usb_audio_full((bts.quality & BQ_USB_FULL) != 0);
}
static uint8_t bts_dirty;
static uint8_t bt_up, bt_scanned;        /* the audio radio has been asked to start; a scan was asked for since it was switched on */
static uint32_t bts_t;
int bt_boot(void) { return bts.on; }
int app_ble_load(struct plat_ble_rec *r)
{
    if (!bts.has)
        return 0;
    *r = bts.r;
    return 1;
}
int app_ble_save(const struct plat_ble_rec *r)
{
    bts.r = *r;
    bts.has = 1;
    bts_dirty = 1;
    bts_t = plat_ms();
    return 0;                            /* (kept: written later, by bt_store) */
}
static void bt_load(void)
{
    uint8_t *b = (uint8_t *)&bts;
    int got;
    for (uint32_t i = 0; i < sizeof bts; i++)
        b[i] = 0;
    got = plat_store_load(OBJ_BT, &bts, sizeof bts);
    if ((got != (int)sizeof bts && got != (int)(sizeof bts - sizeof bts.set)) || bts.magic != BT_MAGIC) {
        for (uint32_t i = 0; i < sizeof bts; i++)
            b[i] = 0;
        bts.magic = BT_MAGIC;
    }
    if (got != (int)sizeof bts)          /* (a record of before 0.2.7: the MIDI settings at their first values) */
        for (uint32_t i = 0; i < sizeof bts.set; i++)
            bts.set[i] = 0;
    for (uint32_t i = 0; i < sizeof bts.set; i++)
        midi_set[i] = bts.set[i];
    if (MS_INCH > 16u)
        MS_INCH = 0;
    if (MS_OUTCH > 16u)
        MS_OUTCH = 0;
    if (MS_KICKCH > 16u)
        MS_KICKCH = 0;
    if (MS_CKOUT > 1u)
        MS_CKOUT = 0;
    if (MS_PULSE > 4u)
        MS_PULSE = 0;
    if (bts.on > 1u)
        bts.on = 0;
    if (bts.has > 1u)
        bts.has = 0;
    if ((bts.quality & 0x0Fu) > 2u)       /* (bits 4..7: the AUDIO and MIDI pages' switches, BQ_*) */
        bts.quality &= 0xF0u;
    xs.on = (uint8_t)((bts.quality & BQ_SYNC) != 0);
    out_apply();
    if (bts.midioff > 1u)
        bts.midioff = 0;
    bts_dirty = 0;
    bt_up = bt_scanned = 0;
}
static void bt_store(void)
{
    if (bts_dirty) {
        store_put(OBJ_BT, &bts, sizeof bts);
        bts_dirty = 0;
    }
}

static int store_all(void)               /* SAVE: everything, whether it changed or not */
{
    int i, rc = 0;
    bt_store();
    snd_sync();
    sd_sync_kick();
    rc |= store_put(OBJ_SOUND, &sd, sizeof sd);
    snd_sig = sig_of(&sd, sizeof sd);
    for (i = 0; i < NBANK; i++) {
        bank_pack(i * NPB);
        rc |= store_put(OBJ_BANK0 + (uint32_t)i, &bk_buf, sizeof bk_buf);
        bank_sig[i] = sig_of(&bk_buf, sizeof bk_buf);
    }
    return rc;
}

/* The autosave: when the sequencer is stopped and nothing has been touched for a moment, the objects that differ from what
 * the flash holds are written, one at a time (a write blocks the main loop; with the sequencer running it would stop the sound). */
static uint8_t as_idx;
static int autosave_tick(void)           /* 1: one object written or checked; the number of clean ones in a row ends it */
{
    static uint8_t clean;
    int i = as_idx;
    as_idx = (uint8_t)((as_idx + 1) % (NBANK + 1));
    snd_sync();
    if (i == 0) {
        sd_sync_kick();
        if (sig_of(&sd, sizeof sd) != snd_sig) {
            store_put(OBJ_SOUND, &sd, sizeof sd);
            snd_sig = sig_of(&sd, sizeof sd);
            clean = 0;
            return 0;
        }
    } else {
        bank_pack((i - 1) * NPB);
        uint32_t g = sig_of(&bk_buf, sizeof bk_buf);
        if (g != bank_sig[i - 1]) {
            store_put(OBJ_BANK0 + (uint32_t)(i - 1), &bk_buf, sizeof bk_buf);
            bank_sig[i - 1] = g;
            clean = 0;
            return 0;
        }
    }
    if (++clean >= NBANK + 1) {
        clean = 0;
        return 1;
    }
    return 0;
}

static void pj_scan(void)                /* the project slots: which hold one, and its name */
{
    struct { uint32_t magic; char name[PN]; uint8_t rsv[3]; } h;
    for (int p = 0; p < NPROJ; p++) {
        pj_used[p] = plat_store_head(PJ_OBJ(p, 0), &h, sizeof h) == (int)sizeof h && h.magic == PRJ_MAGIC;
        pj_name[p][0] = 0;
        if (pj_used[p])
            scopy(pj_name[p], h.name, PN);
    }
}

static void load_data(void)              /* everything but what Bluetooth keeps (a backup does not hold that) */
{
    int i, ok = plat_store_load(OBJ_SOUND, &sd, sizeof sd) == (int)sizeof sd && sd.magic == SND_MAGIC;
    if (!ok)
        defaults();
    for (i = 0; i < NPK; i++)
        if (plat_store_load(OBJ_PRE0 + (uint32_t)i, &up[i], sizeof up[i]) != (int)sizeof up[i] || up[i].magic != PRE_MAGIC)
            for (int j = 0; j < NUSER; j++)
                up[i].s[j].n[0] = 0;
    if (ok) {
        for (i = 0; i < NBANK; i++)
            bank_load(OBJ_BANK0 + (uint32_t)i, i * NPB);
        for (i = 0; i < NKICK; i++)
            kick_p[i] = sd.kick[i];
        sanitize();
    }
    pj_scan();
    sigs_init();
    if (!ok)                             /* no sound in the flash yet: the first autosave writes it, or the banks would not be read back (they follow the sound) */
        snd_sig ^= 1u;
}
static void load_all(void)
{
    bt_load();
    load_data();
}

static int pre_save_obj(int k)
{
    up[k].magic = PRE_MAGIC;
    return store_put(OBJ_PRE0 + (uint32_t)k, &up[k], sizeof up[k]);
}
static int pre_save_slot(int k, int slot)        /* the live settings of a kind into a user slot */
{
    uint8_t d[28];
    pre_cap(k, d);
    if (!up[k].s[slot].n[0]) {
        char n[12];
        scopy(up[k].s[slot].n, "USER ", PN);
        int_text(slot + 1, n);
        if (slot < 9)
            text_cat(up[k].s[slot].n, "0");
        text_cat(up[k].s[slot].n, n);
    }
    for (int j = 0; j < 27; j++)
        up[k].s[slot].d[j] = j < PSZ[k] ? d[j] : 0;
    sd.pre[k] = (uint8_t)(NFACT[k] + slot);
    return pre_save_obj(k);
}
static int pre_clear_slot(int k, int slot)
{
    up[k].s[slot].n[0] = 0;
    if (sd.pre[k] == NFACT[k] + slot)
        sd.pre[k] = 0;
    return pre_save_obj(k);
}

static int prj_save(int p)
{
    static prj_t m;
    int j, rc;
    snd_sync();
    sd_sync_kick();
    m.magic = PRJ_MAGIC;
    scopy(m.name, sd.pname, PN);
    m.s = sd;
    m.s.magic = SND_MAGIC;
    rc = store_put(PJ_OBJ(p, 0), &m, sizeof m);
    for (j = 0; j < PROJ_BANKS; j++) {
        bank_pack(j * NPB);
        rc |= store_put(PJ_OBJ(p, 1 + j), &bk_buf, sizeof bk_buf);
    }
    if (!rc) {
        pj_used[p] = 1;
        scopy(pj_name[p], sd.pname, PN);
    }
    return rc;
}
static int prj_load(int p)
{
    static prj_t m;
    int j;
    if (!pj_used[p] || plat_store_load(PJ_OBJ(p, 0), &m, sizeof m) != (int)sizeof m || m.magic != PRJ_MAGIC)
        return -1;
    seq_stop();
    dsp_panic();
    sd = m.s;
    sd.magic = SND_MAGIC;
    scopy(sd.pname, m.name, PN);
    for (j = 0; j < PROJ_BANKS; j++)
        bank_load(PJ_OBJ(p, 1 + j), j * NPB);
    for (j = 0; j < NKICK; j++)
        kick_p[j] = sd.kick[j];
    sanitize();
    publish();
    tempo_set();
    sq.cur = 0;
    sq.queued = -1;
    snd_pull(0);
    seq_rewind();
    return 0;
}
static int prj_clear(int p)
{
    static prj_t m;
    int rc;
    m.magic = 0;
    rc = store_put(PJ_OBJ(p, 0), &m, 16);
    if (!rc) {
        pj_used[p] = 0;
        pj_name[p][0] = 0;
    }
    return rc;
}

/* --------------------------------------------------------------- audio --- */
static void sync_off(void)               /* no clock any more (or the setting off): the FM-1's own tempo, and a step that waited plays */
{
    xs.live = 0;
    xs.bpm = 0;
    xs.per = 0.0f;
    xs.n = xs.w = xs.ck = xs.c24 = 0;
    if (sq.left > XS_WAIT * 0.5f)
        sq.left = 0.0f;
    tempo_set();
}
static float sync_at(uint32_t t)         /* samples from this block's start to where what landed at t sounds */
{
    float d = XS_LAT - (float)(uint32_t)(xs.tnow - t) * (1.0f / XS_UNIT);
    return d > 0.0f ? d : 0.0f;
}
static void sync_rt(uint32_t b, uint32_t t)      /* a real-time byte that landed at t, with the sync on */
{
    if (b == 0xF8u) {
        if (!xs.live) {
            xs.live = 1;
            xs.n = xs.w = 0;
            xs.per = 0.0f;
            if (sq.running && sq.left < XS_WAIT * 0.5f)      /* running on its own tempo: from here on the clock's steps */
                sq.left = XS_WAIT;
        }
        if (xs.n >= 12u) {               /* the tempo, for the gates, the ratchets and the delay: the clock's period over up
                                          * to a bar, taken when it has moved (the steps themselves need no tempo) */
            uint32_t k = xs.n < XS_WIN ? xs.n : XS_WIN, span = t - xs.t[(xs.w + XS_WIN - k) % XS_WIN];
            float per = (float)span * (1.0f / XS_UNIT) / (float)k, tol = k >= 48u ? 0.004f : 0.02f;
            if (per > 18.0f && per < 6000.0f && (xs.per == 0.0f || per > xs.per * (1.0f + tol) || per < xs.per * (1.0f - tol))) {
                xs.per = per;
                xs.bpm = (uint16_t)(44100.0f * 60.0f / 24.0f / per + 0.5f);
                sq.len = 6.0f * per;
                dsp_tempo(sq.len);
            }
        }
        xs.t[xs.w] = t;
        xs.w = (uint8_t)((xs.w + 1u) % XS_WIN);
        if (xs.n < XS_WIN)
            xs.n++;
        xs.last = xs.now;
        if (xs.ck == 0u && sq.running) { /* a 16th: the next step, an odd one late by the swing */
            uint32_t st = (uint32_t)sq.step + 1u;
            if (st >= pats[sq.cur].len)
                st = 0;
            sq.left = sync_at(t) + ((st & 1u) ? sq.len * (float)pats[sq.cur].swing * (0.33f / 127.0f) : 0.0f);
        }
        xs.ck = (uint8_t)((xs.ck + 1u) % 6u);
        if (MS_PULSE && sq.running && xs.c24 % (24u / PULSE_PPQN[MS_PULSE]) == 0u)   /* the pulse sync follows the clock too */
            co.pwait = sync_at(t);
        xs.c24 = (uint8_t)((xs.c24 + 1u) % 24u);
    } else if (b == 0xFAu) {             /* start: from the first step, at the next clock */
        xs.ck = 0;
        xs.c24 = 0;
        co.ext_start = 1;
        seq_start();
        co.ext_start = 0;
        sq.left = XS_WAIT;
    } else if (b == 0xFBu) {             /* continue: where it stopped */
        if (!sq.running) {
            sq.running = 1;
            sq.left = XS_WAIT;
        }
    } else if (b == 0xFCu) {
        seq_stop();
    }
}
/* A MIDI note recorded (in the render, as it comes in). Running: into the step it is nearest to, the one that plays or the
 * next (that one then does not play it again this time round). Stopped: into the selected step, and the next is selected.
 * The pitch is what makes the step sound the note played, given the transposition and the octave; the velocity is the
 * note's, and over 110 an accent. The step's condition and ratchet are cleared, its gate and locks stay. */
static void rec_note(uint32_t note, uint32_t vel)
{
    pat_t *p = &pats[sq.cur];
    uint32_t n = p->len, t;
    int pit = (int)note - 48 - ((int)sd.sc[SC_TRANS] - 12) - ((int)sd.sc[SC_OCT] - 2) * 12;
    if (!n)
        return;
    if (sq.running) {
        int next;
        if (xs.live)                     /* following a clock: past the middle of the step when 3 of its 6 clocks have come */
            next = xs.ck >= 4u || xs.ck == 0u;
        else
            next = sq.left < XS_WAIT * 0.5f && sq.left < sq.dur * 0.5f;
        t = ((uint32_t)sq.step + (next ? 1u : 0u)) % n;
        if (next)
            sq.skip_s = (uint8_t)(t + 1u);
    } else {
        t = *rec_sel < n ? *rec_sel : 0u;
        *rec_sel = (uint8_t)((t + 1u) % n);
    }
    p->pit[t] = (int8_t)clamp(pit, -24, 24);
    p->vel[t] = (uint8_t)(vel ? vel : 1u);
    p->trig[t] = (uint8_t)(vel > 110u ? 2 : 1);
    p->cond[t] = 0;
    p->rat[t] = 0;
    sq.rec_wrote = 1;
}
static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xF0u, d1 = (pkt >> 16) & 0x7Fu, d2 = pkt >> 24;
        if (((pkt >> 8) & 0xFFu) >= 0xF8u) {
            if (xs.on)
                sync_rt((pkt >> 8) & 0xFFu, FM1B_MIDI_STAMP ? plat_midi_in_t24() : xs.now);
            continue;
        }
        if (MS_INCH && st >= 0x80u && st <= 0xE0u && ((pkt >> 8) & 0x0Fu) != (uint32_t)MS_INCH - 1u)
            continue;                    /* (another channel) */
        if (st == 0x90u && d2)
            co.notes_in++;
        if (st == 0x90u && d2) {
            if (d1 == 36u) {
                if (!sq.mute_k)
                    dsp_kick_trig((float)d2 * (1.0f / 127.0f), d2 > 110u);
            } else {
                if (sq.rec)
                    rec_note(d1, d2);
                if (!sq.mute_s) {
                    for (int k = 0; k < NSYN; k++)
                        syn_eff[k] = syn[k];
                    dsp_syn_trig((float)d1, (float)d2 * (1.0f / 127.0f), d2 > 110u, (float)d1 - 48.0f, 0.0f);
                }
            }
        }
    }
}

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f * 8300000.0f;
    uint32_t done = 0;
    xs.tnow = FM1B_MIDI_STAMP ? plat_ticks24() : xs.now;
    midi_in();
    if (xs.live && (!xs.on || xs.now - xs.last > 22050u))    /* half a second without a clock */
        sync_off();
    if (co.note >= 0 && (co.nleft -= (float)n) <= 0.0f) {    /* the notes out there: their ends */
        mo_send(0x80u | co.nch, (uint32_t)co.note, 0);
        co.note = -1;
    }
    if (co.knote >= 0 && (co.kleft -= (float)n) <= 0.0f) {
        mo_send(0x80u | co.kch, (uint32_t)co.knote, 0);
        co.knote = -1;
    }
    if (sq.running && !xs.live && (MS_CKOUT || MS_PULSE)) {  /* the clock out, on the FM-1's own tempo */
        float pos = co.left, per = sq.len * (1.0f / 6.0f);
        while (pos < (float)n) {
            if (MS_CKOUT) {
                uint8_t h = (uint8_t)((co.head + 1u) % CO_Q);
                if (h != co.tail) {
                    co.q[co.head] = xs.tnow + (uint32_t)(((pos < 0.0f ? 0.0f : pos) + CO_LAT) * XS_UNIT);
                    co.head = h;
                }
            }
            if (MS_PULSE && co.n % (24u / PULSE_PPQN[MS_PULSE]) == 0u)
                co.pwait = pos < 0.0f ? 0.0f : pos;
            co.n++;
            pos += per;
        }
        co.left = pos - (float)n;
    }
    xs.now += n;
    while (done < n) {
        uint32_t run = n - done;
        if (sq.running) {
            float lim;
            while (sq.left <= 0.0f) {
                seq_step();
                sq.left = xs.live ? XS_WAIT : sq.left + sq.dur;      /* (the clock's steps: the next one when it says) */
            }
            while (sq.rn && sq.rleft <= 0.0f) {
                ratchet_hit();
                sq.rn--;
                sq.rleft += sq.rper;
            }
            while (sq.kn && sq.kleft <= 0.0f) {
                dsp_kick_hit(0.85f, 0, sq.kpit, sq.kgain);
                mo_kick(sq.kgain * 0.85f, 0);
                sq.ktrigs++;
                sq.kn--;
                sq.kleft += sq.kper;
            }
            lim = sq.left;
            if (sq.rn && sq.rleft < lim)
                lim = sq.rleft;
            if (sq.kn && sq.kleft < lim)
                lim = sq.kleft;
            if ((uint32_t)lim < 1u)
                lim = 1.0f;
            if (run > (uint32_t)lim)
                run = (uint32_t)lim;
        }
        dsp_render(lr + 2 * done, run, gain);
        if (sq.running) {
            sq.left -= (float)run;
            if (sq.rn)
                sq.rleft -= (float)run;
            if (sq.kn)
                sq.kleft -= (float)run;
        }
        done += run;
    }
    {                                    /* is anything to be heard? (a flash write stops the sound: the autosave asks) */
        int32_t pk = 0;
        for (uint32_t i = 0; i < 2u * n; i += 4u) {
            int32_t v = lr[i] < 0 ? -lr[i] : lr[i], w = lr[i + 1u] < 0 ? -lr[i + 1u] : lr[i + 1u];
            pk |= v | w;
        }
        if (pk > 4096)                   /* (about -66 dB of full scale) */
            loud_at = xs.now;
    }
#if !FM1B_MIDI_STAMP
    clock_send(xs.now + (uint32_t)CO_LAT);       /* (no tick clock here: the block's clocks leave with it) */
#endif
}
void app_tick(void)                      /* 1 kHz, in the timer interrupt: the clocks that are due leave */
{
#if FM1B_MIDI_STAMP
    clock_send(plat_ticks24());
#endif
}
void app_line_out(int32_t *lr, uint32_t n)       /* the FM-1's own output, after the USB recording took the block: the pulse sync */
{
    if (!MS_PULSE)
        return;
    for (uint32_t i = 0; i < n; i++) {
        int32_t m = (lr[2u * i] >> 1) + (lr[2u * i + 1u] >> 1);
        if (co.pwait >= 0.0f && (float)i >= co.pwait) {
            co.pleft = PULSE_LEN;
            co.pwait = -1.0f;
            co.pulses++;
        }
        lr[2u * i] = co.pleft > 0 ? PULSE_AMP : 0;       /* left (the tip): the pulse */
        if (co.pleft > 0)
            co.pleft--;
        lr[2u * i + 1u] = m;                             /* right (the ring): the sound, in mono */
    }
    if (co.pwait >= 0.0f)
        co.pwait -= (float)n;
}

void app_panic(void)
{
    sq.running = 0;
    sq.rn = 0;
    sq.kn = 0;
    dsp_panic();
}

void app_init(void)
{
    dsp_init();
    load_all();
    sq.queued = -1;
    sq.cur = 0;
    snd_pull(0);                         /* the sound of the first pattern (it has none yet: it takes the one saved) */
    publish();
    tempo_set();
    sq.step = (uint8_t)(pats[0].len - 1u);
    sq.kstep = (uint8_t)(pats[0].klen - 1u);
    sq.dur = sq.len;
}

/* Backup and restore over USB (fm1.toml backup = true, the mirror page's Projects panel): the sound, the banks, the user
 * presets and the projects; never the panel's calibration (the unit's) or what Bluetooth keeps. */
int app_backup_playing(void) { return sq.running; }
void app_backup_flush(void)              /* a backup starts with the sequencer stopped: what is not saved yet, saved */
{
    for (int n = 0; n < 4 * (NBANK + 1) && !autosave_tick(); n++)
        ;
    dirty = 0;
    if (bts_dirty)
        bt_store();
}
void app_backup_hold(int on) { bk_hold = (uint8_t)(on != 0); }
void app_backup_restored(void)           /* the flash holds the restored set: loaded as at power-on */
{
    load_data();
    sq.queued = -1;
    sq.cur = 0;
    snd_pull(0);
    publish();
    tempo_set();
    sq.step = (uint8_t)(pats[0].len - 1u);
    sq.kstep = (uint8_t)(pats[0].klen - 1u);
    sq.dur = sq.len;
    dirty = 0;
}

/* ------------------------------------------------------------------- UI --- */
/* The screen: a header (mode, page, preset, tempo), a strip of steps, the picture of the page, and a footer of four
 * quadrants, one for each of the knobs K1..K4. */
enum { M_SEQ, M_SYN, M_PATCH, M_KICK, M_FX, M_PATT, M_RAND, M_SCALE, M_HOME, M_SET, NMODE };
enum { SP_AUDIO, SP_MIDI, SP_MIDI2, SP_BT, SP_VER, NSP };  /* ENV's pages (M_SET), in the order they come */
static const uint8_t NPAGE[NMODE] = {3, 6, 1, 5, 7, 4, NRP, 2, 6, NSP};
enum { F_NUM, F_SEMI, F_WAV16, F_WAV6, F_MODE, F_SW, F_TIME, F_NOTE, F_SCL, F_OCT, F_TRN, F_FINE, F_PCT, F_SPREAD, F_SLOPE, F_SLICE, F_ONOFF, F_SPEED };
static const char *const SRC_NAME[NSRC] = {"NONE", "SEQP", "SEQV", "VEG", "FEG", "AEG", "S&H", "KEG"};
static const char *const SRC_LONG[NSRC] = {"NONE", "SEQ PITCH", "SEQ VELOC", "VCO ENV", "VCF ENV", "VCA ENV", "S&H NOISE", "KICK ENV"};
static const char *const DEST_NAME[NPATCH] = {"VC1", "VC2", "FM", "CUT", "RES", "DEC", "NSE", "WAV"};
static const char *const DEST_LONG[NPATCH] = {"VCO1 PITCH", "VCO2 PITCH", "FM AMOUNT", "CUTOFF", "RESONANCE", "DECAY", "NOISE LVL", "VCO1 WAVE"};
static const char *const WAV16[16] = {"TRI", "SAW", "SQR", "PLS", "SIN", "PWM", "SSAW", "FM", "ORGAN", "VOWEL", "BRASS", "FOLD", "DIGI", "METAL", "NOISE", "BELL"};
static const char *const WAV6[6] = {"SIN", "TRI", "SQR", "FM", "NSE", "MTL"};
static const char *const MODE4[4] = {"LP4", "LP2", "BP", "HP"};
static const char *const TIME8[8] = {"1/16", "1/8", "3/16", "1/4", "3/8", "1/2", "3/4", "1"};
static const char *const SLICE8[8] = {"1/8", "3/32", "1/16", "3/64", "1/32", "3/128", "1/64", "1/128"};
static const char *const CMD_NAME[5] = {"PLAY", "COPY", "PASTE", "CLEAR", "RAND"};
static const char *const FX_NAME[6] = {"CMP", "DLY", "REV", "STU", "DJF", "LIM"};
static const char *const MODE_NAME[NMODE] = {"SEQ", "SYNTH", "PATCH", "KICK", "FX", "PATTERNS", "RANDOM", "SCALE", "SAVE", "SETUP"};
static const char *const ACT_NAME[4] = {"LOAD", "SAVE", "RENAME", "CLEAR"};
static const char *const KIND_NAME[NPK] = {"SYNTH", "KICK", "FX", "PATCH", "SCALE"};
static const char *const HOME_TITLE[10] = {"PROJECTS", "SYNTH PRESETS", "KICK PRESETS", "FX PRESETS", "PATCH PRESETS", "SCALE PRESETS", "BUTTONS", "STEPS AND KEYS", "KNOBS AND FX", "SOUND"};
static const char *const PATT_TITLE[4] = {"PATTERNS", "SONG", "PATTERN SOUND 1", "PATTERN SOUND 2"};
static const char *const SET_TITLE[NSP] = {"OUTPUTS", "CLOCK AND SYNC", "CHANNELS", "HEADPHONES AND MIDI", "FIRMWARE"};
static const char *const SET_NAME[NSP] = {"AUDIO", "MIDI", "MIDI", "BLUETOOTH", "VERSION"};
static const char *const SEQ_TITLE[3] = {"STEP EDIT", "STEP AND PATTERN", "TEMPO"};
static const char CHARS[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-";
#define NCHARS 38
/* the effect bits of the six effect pages (CMP, DLY, REV, STU, DJF, LIM), and of the mixer page (DLY, REV, DJF, STU, CMP, LIM) */
static const uint8_t FXB_PAGE[6] = {FX_CMP, FX_DLY, FX_REV, FX_STU, FX_DJF, FX_LIM};
static const uint8_t FXB_MIX[6] = {FX_DLY, FX_REV, FX_DJF, FX_STU, FX_CMP, FX_LIM};
static const uint8_t FXI_PAGE[6] = {0, 1, 2, 3, 4, 5};      /* the effect's index (FX_NAME) a key stands for */
static const uint8_t FXI_MIX[6] = {1, 2, 4, 3, 0, 5};

/* the colours: the four quadrants, and one for each mode */
#define CL_G RGB(70, 235, 120)
#define CL_C RGB(50, 225, 240)
#define CL_M RGB(255, 70, 200)
#define CL_O RGB(255, 140, 40)
#define CL_Y RGB(245, 235, 60)
#define CL_B RGB(80, 150, 255)
#define CL_W RGB(236, 240, 246)
#define CL_L RGB(150, 160, 185)           /* labels */
#define CL_DG RGB(44, 50, 68)             /* empty cells */
#define CL_PNL RGB(66, 78, 112)           /* panel rules */
#define CL_R RGB(255, 80, 80)
static const uint16_t QC[4] = {CL_G, CL_C, CL_M, CL_O};
static const uint16_t MC[NMODE] = {CL_G, CL_C, CL_M, CL_O, CL_Y, CL_B, CL_R, RGB(170, 120, 255), CL_L, CL_B};

typedef struct {
    const char *lab;
    uint8_t fmt;
    uint8_t *p;
    uint8_t mx;                          /* the largest value */
    uint8_t lo, hi;                      /* what a random draws from */
    uint8_t pz, z;                       /* the odds (%) that the draw is z instead: a knob that is often off */
    uint8_t dk;                          /* a draw of its own (DK_...) */
} knob_t;
enum { DK_RANGE, DK_V1, DK_V2, DK_CUT, DK_TYPE, DK_KWAV };
typedef struct {
    const char *title;
    knob_t k[4];
    uint8_t n;
} row_t;

#define KN(l, f, a) {l, f, &(a), 127, 0, 127, 0, 0, DK_RANGE}
#define KR(l, f, a, lo, hi) {l, f, &(a), 127, lo, hi, 0, 0, DK_RANGE}
#define KM(l, f, a, m, lo, hi) {l, f, &(a), m, lo, hi, 0, 0, DK_RANGE}
#define KZ(l, f, a, lo, hi, pz) {l, f, &(a), 127, lo, hi, pz, 0, DK_RANGE}        /* off (0) in pz % of the draws */
#define KD(l, f, a, d) {l, f, &(a), 127, 0, 127, 0, 0, d}
static const row_t SYN_ROWS[] = {
    {"OSCILLATORS", {KD("VCO1 FRQ", F_SEMI, sd.syn[P_V1FRQ], DK_V1), KD("VCO2 FRQ", F_SEMI, sd.syn[P_V2FRQ], DK_V2), KN("VCO1 WAVE", F_WAV16, sd.syn[P_V1WAV]), KN("VCO2 WAVE", F_WAV16, sd.syn[P_V2WAV])}, 4},
    {"FILTER AND NOISE", {KD("CUTOFF", F_NUM, sd.syn[P_CUT], DK_CUT), KR("RESO", F_NUM, sd.syn[P_RES], 0, 50), KZ("NOISE", F_NUM, sd.syn[P_NSE], 10, 70, 61), KD("TYPE", F_MODE, sd.syn[P_MODE], DK_TYPE)}, 4},
    {"ENVELOPES", {KR("VCO DEC", F_NUM, sd.syn[P_VDEC], 10, 85), KR("VCF DEC", F_NUM, sd.syn[P_FDEC], 20, 100), KR("VCA DEC", F_NUM, sd.syn[P_ADEC], 35, 100), KZ("FM 1>2", F_NUM, sd.syn[P_FM], 10, 65, 54)}, 4},
    {"MIXER", {KR("VCO1 LVL", F_NUM, sd.syn[P_V1LVL], 75, 127), KZ("VCO2 LVL", F_NUM, sd.syn[P_V2LVL], 40, 115, 25), KZ("SYNC", F_SW, sd.syn[P_SYN], 0, 127, 74), KR("VOLUME", F_NUM, sd.syn[P_VOL], 100, 100)}, 4},
    {"MODULATION", {KZ("VCO1 EG", F_NUM, sd.syn[P_V1EG], 4, 80, 49), KZ("VCO2 EG", F_NUM, sd.syn[P_V2EG], 4, 80, 51), KZ("VCF EG", F_NUM, sd.syn[P_FEG], 0, 127, 20), KR("ACCENT", F_NUM, sd.syn[P_ACC], 60, 127)}, 4},
    {"OUTPUT", {KZ("ATTACK", F_NUM, sd.syn[P_ATK], 8, 24, 92), KZ("DRIVE", F_NUM, sd.syn[P_DRV], 10, 60, 50), KZ("GLIDE", F_NUM, sd.syn[P_GLD], 2, 10, 70), KR("TUNE", F_SEMI, sd.syn[P_TUN], 64, 64)}, 4},
};
static const row_t KICK_ROWS[] = {
    {"PITCH AND DECAY", {KR("TUNE", F_NUM, kick_p[K_TUN], 14, 42), KR("SWEEP", F_NUM, kick_p[K_SWP], 15, 120), KR("PITCH DEC", F_NUM, kick_p[K_PDC], 15, 60), KR("AMP DEC", F_NUM, kick_p[K_ADC], 35, 85)}, 4},
    {"TONE", {KR("CLICK", F_NUM, kick_p[K_CLK], 0, 9), KZ("DRIVE", F_NUM, kick_p[K_DRV], 8, 105, 40), KR("LEVEL", F_NUM, kick_p[K_LVL], 90, 100), KD("WAVE", F_WAV6, kick_p[K_WAV], DK_KWAV)}, 4},
    {"FM AND NOISE", {KZ("FM AMT", F_NUM, kick_p[K_FMA], 5, 60, 70), KN("FM RATIO", F_NUM, kick_p[K_FMR]), KZ("NOISE", F_NUM, kick_p[K_NSE], 5, 40, 70), KN("TONE", F_NUM, kick_p[K_TON])}, 4},
    {"KICK SEQUENCE", {{0}}, 0},
};
static const row_t FX_ROWS[] = {
    {"COMPRESSOR", {KN("THRESH", F_NUM, sd.fx[F_CMA]), KR("SPEED", F_NUM, sd.fx[F_CMS], 10, 100), KR("MAKEUP", F_NUM, sd.fx[F_CMG], 0, 60), KR("MIX", F_NUM, sd.fx[F_CMM], 40, 127)}, 4},
    {"DELAY", {KN("TIME", F_TIME, sd.fx[F_DLT]), KR("FEEDBACK", F_NUM, sd.fx[F_DLF], 0, 100), KN("TONE", F_NUM, sd.fx[F_DLK]), KR("MIX", F_NUM, sd.fx[F_DLM], 0, 80)}, 4},
    {"REVERB", {KN("SIZE", F_NUM, sd.fx[F_RVS]), KN("TONE", F_NUM, sd.fx[F_RVT]), KR("WIDTH", F_NUM, sd.fx[F_RVW], 0, 20), KR("MIX", F_NUM, sd.fx[F_RVM], 0, 70)}, 4},
    {"STUTTER", {KN("SLICE", F_SLICE, sd.fx[F_STS]), KR("SPEED", F_SPEED, sd.fx[F_STP], 32, 96), KR("DECAY", F_NUM, sd.fx[F_STD], 0, 90), KR("MIX", F_NUM, sd.fx[F_STM], 60, 127)}, 4},
    {"DJ FILTER", {KR("CUTOFF", F_NUM, sd.fx[F_DJC], 0, 70), KR("RESO", F_NUM, sd.fx[F_DJR], 0, 100), KN("SLOPE", F_SLOPE, sd.fx[F_DJS]), KR("MIX", F_NUM, sd.fx[F_DJM], 80, 127)}, 4},
    {"LIMITER", {KR("CEILING", F_NUM, sd.fx[F_LIM], 60, 127), KR("DRIVE", F_NUM, sd.fx[F_LMD], 0, 50), KR("OUTPUT", F_NUM, sd.fx[F_LMO], 70, 110), KN("SOFT", F_NUM, sd.fx[F_LMS])}, 4},
    {"FX MIXER", {KR("DLY MIX", F_NUM, sd.fx[F_DLM], 0, 80), KR("REV MIX", F_NUM, sd.fx[F_RVM], 0, 70), KR("CUTOFF", F_NUM, sd.fx[F_DJC], 0, 70), KN("SLICE", F_SLICE, sd.fx[F_STS])}, 4},
};
static const row_t SCALE_ROWS[] = {
    {"SCALE AND ROOT", {KM("ROOT", F_NOTE, sd.sc[SC_ROOT], 11, 0, 11), KM("SCALE", F_SCL, sd.sc[SC_TYPE], NSCALE - 1, 0, NSCALE - 1), KM("OCTAVE", F_OCT, sd.sc[SC_OCT], 4, 1, 3), KM("QUANTIZE", F_ONOFF, sd.sc[SC_QUANT], 1, 0, 1)}, 4},
    {"TUNING", {KM("TRANSPOSE", F_TRN, sd.sc[SC_TRANS], 24, 6, 18), KM("FINE", F_FINE, sd.sc[SC_FINE], 100, 30, 70), KM("HUMANIZE", F_NUM, sd.sc[SC_HUM], 127, 0, 60), KM("SPREAD", F_SPREAD, sd.sc[SC_SPREAD], 127, 40, 90)}, 4},
};

/* the random pages mirror the pages of the other modes: SEQ 0..1, SYNTH 2..7, PATCH 8, KICK 9..12, FX 13..19, SCALE 20..21, then the global one */
static const char *const RSPEC[NRP][4] = {
    [0] = {"PITCH", "VELOCITY", "GATE", "RATCHET"},
    [1] = {"CONDITION", "TRIG", "LENGTH", "SWING"},
    [8] = {"SOURCES", "DEPTH", "CONNECT", "-"},
    [12] = {"TRIG", "CONDITION", "RATCHET", "LENGTH"},
    [22] = {"BPM", "SYN PRESET", "KICK PRESET", "FX PRESET"},
};
static const row_t *rp_row(int rp)
{
    if (rp >= 2 && rp <= 7)
        return &SYN_ROWS[rp - 2];
    if (rp >= 9 && rp <= 11)
        return &KICK_ROWS[rp - 9];
    if (rp >= 13 && rp <= 19)
        return &FX_ROWS[rp - 13];
    if (rp == 20 || rp == 21)
        return &SCALE_ROWS[rp - 20];
    return 0;
}
static int rp_of(int mode, int page)    /* the random page that mirrors a page, or -1 */
{
    switch (mode) {
    case M_SEQ: return page < 2 ? page : 22;     /* the tempo page: the global one (the tempo and the presets) */
    case M_SYN: return 2 + page;
    case M_PATCH: return 8;
    case M_KICK: return page >= 2 ? 7 + (int)page : 12;  /* (the sequence is the first two pages of KICK, the last of RANDOM) */
    case M_FX: return 13 + page;
    case M_SCALE: return 20 + page;
    default: return -1;
    }
}
static const char *rp_mode(int rp)
{
    return rp < 2 ? "SEQ" : rp < 8 ? "SYNTH" : rp == 8 ? "PATCH" : rp < 13 ? "KICK" : rp < 20 ? "FX" : rp < 22 ? "SCALE" : "GLOBAL";
}
static const char *rp_lab(int rp, int k)
{
    const row_t *r = rp_row(rp);
    if (r)
        return r->k[k].lab;
    return RSPEC[rp][k] ? RSPEC[rp][k] : "-";
}
static const char *rp_title(int rp)
{
    const row_t *r = rp_row(rp);
    if (r)
        return r->title;
    return rp == 0 ? SEQ_TITLE[0] : rp == 1 ? SEQ_TITLE[1] : rp == 8 ? "VIRTUAL PATCHBAY" : rp == 12 ? "KICK SEQUENCE" : "BPM AND PRESETS";
}

static struct {
    uint8_t mode, page[NMODE];
    uint8_t sel, ksel;                   /* the selected step (SEQ), and the kick's (KICK SEQ) */
    uint8_t bank;                        /* PATTERNS: the bank in view (the black keys) */
    uint8_t cmd;                         /* PATTERNS: what a pattern key does */
    uint8_t slot;                        /* SONG: the slot */
    uint8_t pdst;                        /* PATCH: the destination (its cable) */
    int16_t clip;                        /* the copied pattern, or -1 */
    uint32_t btn, keys;
    uint32_t held_step;                  /* the lowest step key held, 0xFF none */
    uint8_t turned;                      /* a knob turned while a key was held */
    uint8_t stut_mom;                    /* FX: the stutter was switched on by a key still down */
    uint8_t fx_kept;                     /* FX mixer: the effects the last black key switched off (it switches them back on) */
    uint32_t stut_t, play_t, init_t;     /* init_t: when OCT+ and REC went down together (0 none) */
    uint8_t init_hint;                   /* HOLD TO INIT has been shown for this press */

    uint8_t rec_chord;                   /* REC was used with other keys: it is not a tap that opens RANDOM */
    uint8_t fast;                        /* OCT- held (without REC): the knobs K1-K4 move faster */
    uint8_t fast_was;                    /* fast on the last frame (the footer changes when OCT- goes down or up) */
    int8_t nudge, nudge_all;             /* OCT- held in SEQ or the kick's sequence: the steps K4 and K3 moved since OCT- went down */
    uint8_t rmode;                       /* where RANDOM was opened from */
    uint8_t hidx[6], hact, hcur, hren;   /* HOME: the item of each list, the action, the name's cursor, a name changed */
    uint8_t bt_sel, bt_act;              /* BLUETOOTH (PATTERNS page 5): the device in the list, the action */
    char msg[2][24];
    uint32_t msg_until;
    uint8_t d_head, d_strip, d_vis, d_foot;
    uint32_t last_sig, cpu_t, cpu_last;
} ui;

static const uint8_t WK[16] = PLAT_WHITE_KEYS, BK[11] = PLAT_BLACK_KEYS;
static void bt_move(int32_t e);          /* (BLUETOOTH's list, further down) */

static uint32_t pg(void) { return ui.page[ui.mode]; }
static uint32_t kpg(void) { return ui.page[M_KICK] >= 2u ? ui.page[M_KICK] - 2u : 3u + ui.page[M_KICK]; }   /* KICK: its first two pages are the sequence (3 and 4); then the three of knobs (0..2) */
static const row_t *row_of(uint32_t mode, uint32_t page)
{
    switch (mode) {
    case M_SYN: return &SYN_ROWS[page];
    case M_KICK: return page >= 2u ? &KICK_ROWS[page - 2u] : 0;   /* (pages 0 and 1: the kick's sequence) */
    case M_FX: return &FX_ROWS[page];
    case M_SCALE: return &SCALE_ROWS[page];
    default: return 0;
    }
}
static pat_t *cur_pat(void) { return &pats[sq.cur]; }
static int32_t fastk(int32_t e) { return ui.fast ? e * 4 : e; }      /* OCT- held: four clicks for one */
static int edit_pi(void) { return sq.queued >= 0 ? sq.queued : sq.cur; }
static uint32_t cur_len(void) { return cur_pat()->len; }
static uint32_t cur_klen(void) { return cur_pat()->klen; }
static uint32_t steps_len(void) { return ui.mode == M_KICK ? cur_klen() : cur_len(); }

static void dirty_all(void) { ui.d_head = ui.d_strip = ui.d_vis = ui.d_foot = 1; }
void app_redraw(void) { dirty_all(); }          /* the mirror lost a part of the screen: every part again */

void app_say(const char *a, const char *b)
{
    uint32_t i;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1200u;
    ui.d_head = 1;
}

static void signed_text(int v, char *t)
{
    char n[12];
    t[0] = 0;
    if (v > 0)
        text_cat(t, "+");
    int_text(v, n);
    text_cat(t, n);
}

static void fmt_val(uint8_t fmt, int v, char *t)
{
    char n[12];
    t[0] = 0;
    switch (fmt) {
    case F_SEMI: signed_text((v - 64) / 2, t); break;
    case F_WAV16: text_cat(t, WAV16[(v >> 3) & 15]); break;
    case F_WAV6: text_cat(t, WAV6[(v * 6) >> 7]); break;
    case F_MODE: text_cat(t, MODE4[(v >> 5) & 3]); break;
    case F_SW: text_cat(t, v >= 64 ? "ON" : "OFF"); break;
    case F_TIME: text_cat(t, TIME8[(v * 8) >> 7]); break;
    case F_NOTE: text_cat(t, NOTE12[v % 12]); break;
    case F_SCL: text_cat(t, SCALES[v % NSCALE].n); break;
    case F_OCT: signed_text(v - 2, t); break;
    case F_TRN: signed_text(v - 12, t); break;
    case F_FINE: signed_text(v - 50, t); break;
    case F_PCT: int_text(v * 100 / 127, t); text_cat(t, "%"); break;
    case F_SPREAD: int_text(v * 100 / 64, t); text_cat(t, "%"); break;
    case F_SLOPE: text_cat(t, v >= 64 ? "24DB" : "12DB"); break;
    case F_SLICE: text_cat(t, SLICE8[(v * 8) >> 7]); break;
    case F_ONOFF: text_cat(t, v ? "ON" : "OFF"); break;
    case F_SPEED: {
        int s10 = (int)(10.0f * fm_exp2f(((float)v - 64.0f) * (1.0f / 64.0f)) + 0.5f);
        text_cat(t, "X");
        int_text(s10 / 10, n);
        text_cat(t, n);
        text_cat(t, ".");
        int_text(s10 % 10, n);
        text_cat(t, n);
        break;
    }
    default: int_text(v, t); break;
    }
}

/* the effective value of a knob, for the screen: a held step's lock shows over the sound */
static int lock_find(uint32_t step, uint32_t pid)
{
    pat_t *p = cur_pat();
    for (int l = 0; l < 2; l++)
        if (p->lk_p[step][l] == pid + 1u)
            return l;
    return -1;
}
static int is_syn_knob(const uint8_t *p) { return p >= syn && p < syn + NSYN; }

static void bpm_add(int32_t e)
{
    sd.bpm = (uint16_t)clamp((int32_t)sd.bpm + e, 40, 240);
    tempo_set();
    touch();
    ui.d_head = 1;
}

/* A choice (a wave, a filter type, a delay time, a slice) is kept as 0..127 and cut in n equal parts; a click moves to the
 * next part (the middle of it, as the random puts it), so every click changes what the screen says. A value that is not in a
 * middle (a patch loaded from before) moves to the middle of the part next to it. At the end of the list nothing changes. */
static int choice_n(uint8_t fmt)
{
    switch (fmt) {
    case F_WAV16: return 16;
    case F_WAV6: return 6;
    case F_MODE: return 4;
    case F_TIME:
    case F_SLICE: return 8;
    default: return 0;
    }
}
static int choice_step(uint8_t fmt, int v, int e)
{
    int n = choice_n(fmt), i, j;
    switch (fmt) {                                  /* the part v is in: the one fmt_val shows */
    case F_WAV16: i = (v >> 3) & 15; break;
    case F_WAV6: i = (v * 6) >> 7; break;
    case F_MODE: i = (v >> 5) & 3; break;
    default: i = (v * 8) >> 7; break;
    }
    j = clamp(i + e, 0, n - 1);
    if (j == i)
        return v;
    switch (fmt) {
    case F_WAV16: return j * 8 + 4;
    case F_WAV6: return j * 21 + 10;
    case F_MODE: return j * 32 + 16;
    default: return j * 16 + 8;
    }
}

static void knob_turn(uint32_t kn, int32_t e)
{
    const row_t *r = row_of(ui.mode, pg());
    uint8_t *p;
    int v, mx, ch;
    if (!r || kn >= r->n)
        return;
    p = r->k[kn].p;
    mx = r->k[kn].mx;
    ch = choice_n(r->k[kn].fmt) != 0;
    if (ch)                                                     /* a choice: a click a part, as many as the encoder gave */
        ;
    else if (r->k[kn].fmt == F_SW || r->k[kn].fmt == F_SLOPE)  /* a switch: the first click flips it, up is on */
        e = e > 0 ? 127 : -127;
    else if (mx > 20 && ui.fast)                                /* OCT- held: eight steps a click */
        e *= 8;
    else if (mx > 20 && (e > 3 || e < -3))
        e *= 3;
    if (ui.mode == M_SYN && ui.held_step != 0xFFu && is_syn_knob(p)) {      /* a held step: a lock */
        pat_t *pt = cur_pat();
        uint32_t pid = (uint32_t)(p - syn), st = ui.held_step;
        int l = lock_find(st, pid);
        if (l < 0)
            l = pt->lk_p[st][0] == 0 ? 0 : pt->lk_p[st][1] == 0 ? 1 : 0;
        if (pt->lk_p[st][l] != pid + 1u) {
            pt->lk_p[st][l] = (uint8_t)(pid + 1u);
            pt->lk_v[st][l] = *p;
        }
        v = ch ? choice_step(r->k[kn].fmt, pt->lk_v[st][l], e) : clamp(pt->lk_v[st][l] + e, 0, mx);
        pt->lk_v[st][l] = (uint8_t)v;
        ui.turned = 1;
        touch();
        ui.d_vis = ui.d_foot = ui.d_strip = 1;
        return;
    }
    v = ch ? choice_step(r->k[kn].fmt, *p, e) : clamp(*p + e, 0, mx);
    *p = (uint8_t)v;
    publish();
    touch();
    ui.d_vis = ui.d_foot = 1;
}

static int pat_used(const pat_t *p)
{
    for (int i = 0; i < NSTEP; i++)
        if (p->trig[i] | p->ktrig[i])
            return 1;
    return 0;
}

/* ---------------------------------------------------------------- random --- */
static int rpr(int amt) { return (int)(rnd() % 127u) < amt; }              /* true with the odds amt/127 */
static int lerp_i(int a, int b, int amt) { return a + (b - a) * amt / 127; }
static int fmt_disc(uint8_t f)
{
    return f == F_WAV16 || f == F_WAV6 || f == F_MODE || f == F_SW || f == F_TIME || f == F_NOTE || f == F_SCL || f == F_OCT ||
           f == F_SLOPE || f == F_SLICE || f == F_ONOFF;
}
/* A random is the draw of a drum synth: a sound that is mostly one of two kinds (a
 * bass-like one through a low-pass, or, 15 % of the times, a high "hat" one through a high-pass), a knob that is often off
 * (noise, FM, sync, the second oscillator, the envelope depths), and steps that are the notes of a minor pentatonic, one in
 * four a rest. rnd_hat is the kind of this random (the same for every page of it). */
static int rnd_hat;
static int rnd_wide;                     /* OCT+ + REC, the general random as 0.2.3 drew it: the resonance up to 85, the kick's level 100..110 */
static int rnd_pz(const knob_t *kn) { return kn->pz && (int)(rnd() % 100u) < kn->pz; }
static void rnd_knob(const knob_t *kn, int amt)  /* a number moves toward a draw by the amount; a choice changes with those odds */
{
    int t;
    switch (kn->dk) {
    case DK_V1:                          /* the pitch of the sound: 24..55 semitones around the note, the hat ones higher */
        t = 64 + 2 * ((rnd_hat ? 48 + (int)(rnd() % 24u) : 24 + (int)(rnd() % 32u)) - 40);
        break;
    case DK_V2: {                        /* the second oscillator: the first one plus a unison, a fifth, a fourth or an octave, now and then off tune */
        static const int8_t IV[6] = {0, 0, 7, 12, -12, 5};
        int det = rnd() % 10u < 7u ? (int)(rnd() % 21u) - 10 : 0;
        t = (int)sd.syn[P_V1FRQ] + 2 * IV[rnd() % 6u] + (det < -4 ? -1 : det > 4 ? 1 : 0);
        break;
    }
    case DK_CUT:                         /* a bright low-pass, or the low cutoff of a hat's high-pass */
        t = rnd_hat ? 15 + (int)(rnd() % 41u) : 62 + (int)(rnd() % 59u);
        break;
    case DK_TYPE:                        /* LP4 or LP2; a hat: HP */
        t = (rnd_hat ? 3 : (int)(rnd() & 1u)) * 32 + 16;
        break;
    case DK_KWAV:                        /* the kick: sine, triangle or square */
        t = (int)(rnd() % 3u) * 21 + 10;
        break;
    default:
        switch (kn->fmt) {
        case F_WAV16: t = (int)(rnd() % 16u) * 8 + 4; break;
        case F_WAV6: t = (int)(rnd() % 6u) * 21 + 10; break;
        case F_MODE: t = (int)(rnd() % 4u) * 32 + 16; break;
        case F_SW:
            t = kn->pz ? (rnd_pz(kn) ? 0 : 127) : (rnd() & 1u) ? 127 : 0;
            break;
        case F_SLOPE: t = (rnd() & 1u) ? 127 : 0; break;
        case F_TIME:
        case F_SLICE: t = (int)(rnd() % 8u) * 16 + 8; break;
        default:
            {
                int lo = kn->lo, hi = kn->hi;
                if (rnd_wide && kn->p == &sd.syn[P_RES])
                    hi = 85;
                else if (rnd_wide && kn->p == &kick_p[K_LVL])
                    lo = 100, hi = 110;
                t = rnd_pz(kn) ? (int)kn->z : lo + (int)(rnd() % (uint32_t)(hi - lo + 1));
            }
            break;
        }
        break;
    }
    if (fmt_disc(kn->fmt)) {
        if (rpr(amt))
            *kn->p = (uint8_t)clamp(t, 0, kn->mx);
    } else {
        *kn->p = (uint8_t)clamp(lerp_i(*kn->p, t, amt), 0, kn->mx);
    }
}
static int rnd_pitch(int ext)            /* a step's pitch: the minor pentatonic (ext: and the notes above it), 15 % an octave down */
{
    static const int8_t S8[8] = {0, 3, 5, 7, 10, 12, 15, 19};
    int v = S8[rnd() % (ext ? 8u : 5u)];
    return rnd() % 100u < 15u ? v - 12 : v;
}
static void rnd_rests(pat_t *p, int amt, int vel)  /* every step but the first is a rest one time in four; a step that plays gets a velocity of 70..127 */
{
    for (uint32_t i = 0, n = p->len; i < n; i++) {
        if (vel)
            p->vel[i] = (uint8_t)lerp_i(p->vel[i], 70 + (int)(rnd() % 58u), amt);
        if (rpr(amt))
            p->trig[i] = (uint8_t)(i == 0 || rnd() % 4u ? (p->trig[i] ? p->trig[i] : 1) : 0);
    }
}
static void rnd_kick_steps(pat_t *p, int amt)      /* the kick: always on step 1, then one of three patterns, no two steps in a row, one in four an accent */
{
    int style = (int)(rnd() % 3u);
    for (uint32_t i = 0, n = p->klen; i < n; i++) {
        int r = (int)(rnd() % 100u), on = i == 0;
        if (!on) {
            if (style == 0)
                on = (i & 3u) == 0 || r < 12;               /* four on the floor, and a few more */
            else if (style == 1)
                on = i == 10 || r < 30;                     /* step 11 and a busier mix */
            else
                on = i == 8 ? r < 60 : r < 15;              /* the half bar, then a few */
            if (on && p->ktrig[i - 1u])
                on = 0;
        }
        if (rpr(amt))
            p->ktrig[i] = (uint8_t)(i == 0 ? 2 : on ? (rnd() % 4u == 0 ? 2 : 1) : 0);
    }
}
enum { RF_PATT, RF_EUCLID, RF_SOFT };   /* the randoms of everything with steps of their own (random_flavour, below) */
static void pat_flavour(pat_t *p, int kind);
static void random_flavour(int kind);
static void rnd_cables(int amt)          /* two to four new cables, from any source into any input, a depth of 20..80 % (seven in ten positive) */
{
    uint32_t n = 2u + rnd() % 3u;
    for (int i = 0; i < NPATCH; i++) {
        sd.src[i] = S_OFF;
        sd.amt[i] = 64;
    }
    sd.pon = 0;
    while (n--) {
        uint32_t d = rnd() % NPATCH;
        int mag = 13 + (int)(rnd() % 39u);
        sd.src[d] = (uint8_t)(1 + rnd() % (NSRC - 1u));
        sd.amt[d] = (uint8_t)(64 + (rnd() % 10u < 7u ? mag : -mag));
        sd.pon |= (uint8_t)(1u << d);
    }
    (void)amt;
}
static void rnd_apply(int rp, int k, int amt)    /* one knob of a random page */
{
    const row_t *r = rp_row(rp);
    pat_t *p = cur_pat();
    uint32_t i, n;
    if (r) {
        rnd_knob(&r->k[k], amt);
        return;
    }
    switch (rp) {
    case 0:
        if (k == 0) {
            int ext = rnd() % 10u < 3u;  /* a pattern in three uses the eight notes, the others the five */
            for (i = 0, n = p->len; i < n; i++)
                if (rpr(amt))
                    p->pit[i] = (int8_t)(i == 0 ? 0 : rnd_pitch(ext));
        } else if (k == 1) {
            rnd_rests(p, amt, 1);
        } else {
            for (i = 0, n = p->len; i < n; i++) {
                if (k == 2) {
                    if (rpr(amt))
                        p->gate[i] = (uint8_t)(rnd() % 3u == 0 ? GATE_FULL : 4 + rnd() % 29u);
                } else if (rpr(amt)) {
                    p->rat[i] = (uint8_t)(rnd() % 5u == 0 ? 1 + rnd() % 3u : 0);
                }
            }
        }
        if (k < 2)                       /* new steps: the old locks do not belong to them */
            for (i = 0; i < NSTEP; i++)
                p->lk_p[i][0] = p->lk_p[i][1] = p->lk_v[i][0] = p->lk_v[i][1] = 0;
        break;
    case 1:
        if (k == 2) {
            static const uint8_t L[5] = {4, 6, 8, 12, 16};
            if (rpr(amt)) {
                p->len = L[rnd() % 5u];
                if (sq.step >= p->len)
                    sq.step = (uint8_t)(p->len - 1u);
                if (ui.sel >= p->len)
                    ui.sel = (uint8_t)(p->len - 1u);
            }
        } else if (k == 3) {
            p->swing = (uint8_t)lerp_i(p->swing, (int)(rnd() % 90u), amt);
        } else if (k == 1) {
            rnd_rests(p, amt, 0);
        } else {
            for (i = 0, n = p->len; i < n; i++)
                if (rpr(amt))
                    p->cond[i] = (uint8_t)(rnd() % 4u == 0 ? rnd() % NCOND : 0);
        }
        break;
    case 8:
        if (k == 0) {
            if (rpr(amt))
                rnd_cables(amt);
        } else {
            for (i = 0; i < NPATCH; i++) {
                if (k == 1) {
                    if ((sd.pon >> i) & 1u) {
                        int mag = 13 + (int)(rnd() % 39u);
                        sd.amt[i] = (uint8_t)lerp_i(sd.amt[i], 64 + (rnd() % 10u < 7u ? mag : -mag), amt);
                    }
                } else if (k == 2 && rpr(amt)) {
                    if (rnd() % 3u == 0)
                        sd.pon |= (uint8_t)(1u << i);
                    else
                        sd.pon &= (uint8_t)~(1u << i);
                }
            }
        }
        break;
    case 12:
        if (k == 3) {
            static const uint8_t L[4] = {4, 8, 12, 16};
            if (rpr(amt)) {
                p->klen = L[rnd() % 4u];
                if (sq.kstep >= p->klen)
                    sq.kstep = (uint8_t)(p->klen - 1u);
                if (ui.ksel >= p->klen)
                    ui.ksel = (uint8_t)(p->klen - 1u);
            }
        } else if (k == 0) {
            rnd_kick_steps(p, amt);
        } else {
            for (i = 0, n = p->klen; i < n; i++)
                if (rpr(amt)) {
                    if (k == 1)
                        p->kcond[i] = (uint8_t)(rnd() % 4u == 0 ? rnd() % NCOND : 0);
                    else
                        p->krat[i] = (uint8_t)(rnd() % 5u == 0 ? 1 + rnd() % 3u : 0);
                }
        }
        break;
    default:                             /* the global page */
        if (k == 0)
            bpm_add(lerp_i(sd.bpm, 80 + (int)(rnd() % 81u), amt) - sd.bpm);
        else if (rpr(amt))
            pre_apply(k == 1 ? PK_SYN : k == 2 ? PK_KICK : PK_FX, (int)(rnd() % NFACT[k == 1 ? PK_SYN : k == 2 ? PK_KICK : PK_FX]));
        break;
    }
}
static int rnd_page(int rp)              /* the knobs of a page that are on; the number of them */
{
    int n = 0;
    for (int k = 0; k < 4; k++)
        if ((sd.rnd_en[rp] >> k) & 1u) {
            rnd_apply(rp, k, sd.rnd_amt[rp][k]);
            n++;
        }
    return n;
}
static int rnd_in_all(int rp) { return rp <= 7 || (rp >= 9 && rp <= 12); }   /* what ALL draws: the steps, the synth and the kick (not the cables, the effects, the scale, the tempo) */
/* all 0, OCT- + REC: this page. The general random draws the steps, the synth and the kick: all 1, OCT+ + HOME (and key 7 of
 * a RANDOM page), with the resonance at 50 and the kick's level at 100 or under; all 2, OCT+ + REC (a tap), as 0.2.3 drew it. */
static void random_run(int all)
{
    int n = 0;
    rnd_hat = (int)(rnd() % 100u) < 15;
    if (all) {
        rnd_wide = all == 2;
        for (int rp = 0; rp < NRP; rp++)
            if (rnd_in_all(rp))
                n += rnd_page(rp);
        rnd_wide = 0;
        if (n && all == 1) {                         /* whatever the amounts say: the resonance stays at 50 or under, the kick's level at 100 */
            if (sd.syn[P_RES] > 50u)
                sd.syn[P_RES] = 50;
            if (kick_p[K_LVL] > 100u)
                kick_p[K_LVL] = 100;
        }
        publish();
        app_say("RANDOM", n ? "STEPS + SOUNDS" : "ALL OFF");
    } else {
        int rp = ui.mode == M_RAND ? (int)pg() : rp_of(ui.mode, pg());
        if (rp < 0) {
            if (ui.mode == M_PATT) {     /* the pattern that plays: its steps and its sound; another one: its steps */
                if (edit_pi() == (int)sq.cur) {
                    random_flavour(RF_PATT);
                    return;
                }
                pat_flavour(&pats[edit_pi()], RF_PATT);
                n = 1;
            }
            publish();
            app_say("RANDOM", n ? "STEPS" : "NO PAGE");
        } else {
            const row_t *r = rp_row(rp);
            n = rnd_page(rp);
            if (n && r && r == &FX_ROWS[rp - 13] && rp >= 13 && rp <= 19)
                for (int q = 0; q < 4; q++)      /* the reverb's width: 20 or under after a random, whatever its amount */
                    if (r->k[q].p == &sd.fx[F_RVW] && sd.fx[F_RVW] > 20u)
                        sd.fx[F_RVW] = 20;
            publish();
            app_say("RANDOM", n ? rp_title(rp) : "ALL OFF");
        }
    }
    touch();
    dirty_all();
}

/* Three more randoms of everything, each with steps of its own on top of what the random pages draw.
 *   RF_PATT, RAND in PATTERNS on the pattern that plays (and OCT- + REC there): the sound as OCT+ + REC draws it; the steps
 *     keep the beat (every fourth step plays, the others rest one time in three), pitches over the wider range half the time.
 *   RF_EUCLID, OCT+ + SEQ: the sound as OCT+ + REC draws it; the trigs are a number of hits spread evenly over the length
 *     and turned by a few steps, the pitches a four-step figure said again with a note changed each time, and the kick's
 *     line is spread the same way.
 *   RF_SOFT, OCT- + SEQ: the sound with the resonance at 50 and the kick's level at 100 or under; the second half of the
 *     steps answers the first (the same, its last notes changed); and the effects are drawn too, but low: the reverb's
 *     width under 12, the mixes, the feedback and the drive small, the DJ filter open and the stutter off. */
static void rf_spread(uint8_t *t, uint32_t n, uint32_t hits, uint32_t turn)      /* hits spread evenly over n steps, turned */
{
    for (uint32_t i = 0; i < n; i++)
        t[(i + turn) % n] = (uint8_t)(((i * hits) % n) < hits);
}
static void pat_flavour(pat_t *p, int kind)      /* the steps and the kick's line of a flavour, in any pattern */
{
    uint32_t i, n = p->len, kn = p->klen;
    uint8_t on[NSTEP];
    int ext = rnd() % 10u < (kind == RF_PATT ? 5u : 3u), fig[4];
    for (i = 0; i < NSTEP; i++)
        p->lk_p[i][0] = p->lk_p[i][1] = p->lk_v[i][0] = p->lk_v[i][1] = 0;
    if (kind == RF_PATT) {
        for (i = 0; i < n; i++) {
            p->pit[i] = (int8_t)(i == 0 ? 0 : rnd_pitch(ext));
            p->trig[i] = (uint8_t)((i & 3u) == 0 ? (i == 0 || rnd() % 3u == 0 ? 2 : 1) : rnd() % 3u ? 1 : 0);
            p->vel[i] = (uint8_t)((i & 3u) == 0 ? 100 + rnd() % 28u : 70 + rnd() % 45u);
        }
        rnd_kick_steps(p, 127);
    } else if (kind == RF_EUCLID) {
        uint32_t hits = n / 2u + rnd() % (n / 3u + 1u);
        rf_spread(on, n, hits > n ? n : hits, rnd() % n);
        for (i = 0; i < 4u; i++)
            fig[i] = i == 0 ? 0 : rnd_pitch(ext);
        for (i = 0; i < n; i++) {
            int v = fig[i & 3u];
            if (i >= 4u && (i & 3u) == (i / 4u) % 4u)
                v = rnd_pitch(ext);      /* said again, a note changed each time */
            p->pit[i] = (int8_t)(i == 0 ? 0 : v);
            p->trig[i] = (uint8_t)(i == 0 ? 2 : on[i] ? ((i & 3u) == 0 && rnd() % 2u ? 2 : 1) : 0);
            p->vel[i] = (uint8_t)(75 + rnd() % 50u);
        }
        rf_spread(on, kn, 3u + rnd() % 4u, 0);
        for (i = 0; i < kn; i++)
            p->ktrig[i] = (uint8_t)(i == 0 ? 2 : on[i] ? (rnd() % 4u == 0 ? 2 : 1) : 0);
    } else {
        uint32_t half = n / 2u ? n / 2u : 1u;
        for (i = 0; i < half; i++) {
            p->pit[i] = (int8_t)(i == 0 ? 0 : rnd_pitch(ext));
            p->trig[i] = (uint8_t)(i == 0 ? 2 : rnd() % 4u ? 1 : 0);
            p->vel[i] = (uint8_t)(70 + rnd() % 50u);
        }
        for (i = half; i < n; i++) {     /* the answer: the first half again, its last notes its own */
            uint32_t j = i - half;
            p->pit[i] = p->pit[j % half];
            p->trig[i] = p->trig[j % half] ? 1 : 0;
            p->vel[i] = p->vel[j % half];
            if (i + 2u >= n) {
                p->pit[i] = (int8_t)rnd_pitch(ext);
                p->trig[i] = (uint8_t)(rnd() % 3u ? 1 : 0);
            }
        }
        rnd_kick_steps(p, 127);
    }
}
static void random_flavour(int kind)
{
    static const char *const NAME[3] = {"PATTERN", "SPREAD", "SOFT + FX"};
    pat_t *p = cur_pat();
    int n = 0, wide = kind != RF_SOFT;
    uint32_t lf;
    rnd_hat = (int)(rnd() % 100u) < 15;
    rnd_wide = wide;
    for (int rp = 0; rp < NRP; rp++)
        if (rnd_in_all(rp))
            n += rnd_page(rp);
    rnd_wide = 0;
    if (!wide) {
        if (sd.syn[P_RES] > 50u)
            sd.syn[P_RES] = 50;
        if (kick_p[K_LVL] > 100u)
            kick_p[K_LVL] = 100;
    }
    lf = plat_lock();
    pat_flavour(p, kind);
    plat_unlock(lf);
    if (kind == RF_SOFT) {               /* the effects, drawn low */
        uint8_t *x = sd.fx;
        for (int rp = 13; rp <= 19; rp++)
            (void)rnd_page(rp);
        x[F_RVW] = (uint8_t)(rnd() % 12u);
        x[F_RVM] = (uint8_t)(8 + rnd() % 23u);
        x[F_DLM] = (uint8_t)(5 + rnd() % 21u);
        if (x[F_DLF] > 45u)
            x[F_DLF] = (uint8_t)(15 + rnd() % 31u);
        if (x[F_LMD] > 20u)
            x[F_LMD] = (uint8_t)(rnd() % 21u);
        x[F_DJC] = 0;                    /* (the DJ filter open) */
        if (x[F_DJR] > 30u)
            x[F_DJR] = (uint8_t)(rnd() % 31u);
        sd.fxen &= (uint8_t)~FX_STU;
        ui.stut_mom = 0;
    }
    publish();
    app_say("RANDOM", NAME[kind]);
    (void)n;
    touch();
    dirty_all();
}

/* OCT+ + HOME: a random for percussion between the mids and the lows. The steps and the kick are drawn as the
 * general random draws them; the synth becomes an electronic tom or a low electronic drum: a sine or a triangle (now and
 * then a square or FM) at a low pitch that falls and takes its time, no attack, a decay that rings a while, a low-pass,
 * half the time a second oscillator for body, now and then a little noise at the hit. No high "hat" sounds. The cables, the effects, the scale and the tempo stay. */
static void random_perc(void)
{
    static const uint8_t WV[8] = {4, 4, 4, 4, 0, 0, 2, 7};       /* SIN (half the time) TRI SQR FM */
    static const int8_t IV[6] = {0, 0, 7, 5, 12, -12};           /* the second oscillator: unison, a fifth, a fourth, an octave */
    uint8_t *y = sd.syn;
    int n = 0, body = (int)(rnd() % 10u) < 5;                    /* half the time a second oscillator gives it body */
    rnd_hat = 0;
    for (int rp = 0; rp < NRP; rp++)
        if (rnd_in_all(rp) && !(rp >= 2 && rp <= 7))
            n += rnd_page(rp);
    y[P_V1FRQ] = (uint8_t)(64 + 2 * ((int)(rnd() % 17u) - 16));  /* 24..40 semitones: a floor tom to a middle one */
    y[P_V1WAV] = (uint8_t)(WV[rnd() % 8u] * 8u + 4u);
    y[P_V1LVL] = (uint8_t)(105 + rnd() % 23u);
    y[P_V1EG] = (uint8_t)(50 + rnd() % 46u);                     /* the pitch falls, as an electronic tom's does, */
    y[P_VDEC] = (uint8_t)(30 + rnd() % 41u);                     /* and takes its time */
    y[P_V2FRQ] = (uint8_t)clamp((int)y[P_V1FRQ] + 2 * IV[rnd() % 6u] + (rnd() % 3u == 0 ? 1 : 0), 0, 127);
    y[P_V2WAV] = (uint8_t)(WV[rnd() % 6u] * 8u + 4u);            /* (a sine or a triangle) */
    y[P_V2LVL] = (uint8_t)(body ? 30 + rnd() % 41u : 0);
    y[P_V2EG] = (uint8_t)clamp((int)y[P_V1EG] + (int)(rnd() % 21u) - 10, 0, 127);
    y[P_FM] = (uint8_t)(rnd() % 10u < 7u ? 0 : 10 + rnd() % 26u);
    y[P_SYN] = 0;
    y[P_NSE] = (uint8_t)(rnd() % 10u < 7u ? 0 : 5 + rnd() % 14u); /* now and then a little noise at the hit */
    y[P_CUT] = (uint8_t)(55 + rnd() % 41u);
    y[P_RES] = (uint8_t)(rnd() % 26u);
    y[P_MODE] = (uint8_t)((rnd() & 1u) * 32u + 16u);             /* LP4 or LP2 */
    y[P_FEG] = (uint8_t)(10 + rnd() % 41u);
    y[P_FDEC] = (uint8_t)(25 + rnd() % 36u);
    y[P_ATK] = 0;
    y[P_ADEC] = (uint8_t)(35 + rnd() % 41u);                     /* it rings a while */
    y[P_VOL] = 100;
    y[P_DRV] = (uint8_t)(5 + rnd() % 36u);
    y[P_ACC] = (uint8_t)(70 + rnd() % 41u);
    y[P_GLD] = 0;
    y[P_TUN] = 64;
    if (kick_p[K_LVL] > 100u)
        kick_p[K_LVL] = 100;
    publish();
    app_say("RANDOM", n ? "PERCUSSION" : "PERC SOUND");
    touch();
    dirty_all();
}

/* OCT- + REC held 0.7 s: the page is initialised. A page of sound parameters (SYNTH, KICK's sound pages, the effects): its
 * four knobs go back to the first power-on sound (the first factory preset of its kind); the patch bay: every cable off;
 * a step page of SEQ: what its four knobs set, on every step, as in a new pattern (page 1 pitch, velocity, gate, ratchet,
 * and every trig off; page 2 condition, trig, length, swing); the kick's line: empty, as in a new pattern; a SCALE page: its four knobs as at
 * first power-on. -1 on a page that has no such thing (the tempo, the patterns and settings, SAVE). */
static int page_init_rp(void)
{
    int rp = ui.mode == M_RAND ? (int)pg() : rp_of(ui.mode, pg());
    return rp >= 0 && rp <= 21 ? rp : -1;
}
static void page_init(void)
{
    int rp = page_init_rp(), q, k;
    const row_t *r;
    uint8_t d[28];
    if (rp < 0)
        return;
    if (rp == 8) {
        for (q = 0; q < NPATCH; q++) {
            sd.src[q] = S_OFF;
            sd.amt[q] = 64;
        }
        sd.pon = 0;
    } else if (rp <= 1 || rp == 12) {    /* the steps, the kick's line */
        pat_t *pt = cur_pat();
        uint32_t lf = plat_lock();
        for (q = 0; q < NSTEP; q++) {
            if (rp == 0) {
                pt->trig[q] = 0;         /* (the first step page takes the trigs away as well: an empty line) */
                pt->pit[q] = 0;
                pt->vel[q] = 100;
                pt->gate[q] = GATE_FULL;
                pt->rat[q] = 0;
            } else if (rp == 1) {
                pt->cond[q] = 0;
                pt->trig[q] = 0;
            } else {
                pt->ktrig[q] = 0;
                pt->kcond[q] = 0;
                pt->krat[q] = 0;
                pt->kpit[q] = 0;
                pt->kvol[q] = KVOL_DEF;
            }
        }
        if (rp == 1) {
            pt->len = LEN_DEF;
            pt->swing = 0;
            if (sq.step >= pt->len)
                sq.step = (uint8_t)(pt->len - 1u);
            if (ui.sel >= pt->len)
                ui.sel = (uint8_t)(pt->len - 1u);
        } else if (rp == 12) {
            pt->klen = KLEN_DEF;
            if (sq.kstep >= pt->klen)
                sq.kstep = (uint8_t)(pt->klen - 1u);
            if (ui.ksel >= pt->klen)
                ui.ksel = (uint8_t)(pt->klen - 1u);
        }
        plat_unlock(lf);
    } else if (rp == 20) {               /* the scale: C major, the middle octave, the quantizer off */
        sd.sc[SC_ROOT] = 0;
        sd.sc[SC_TYPE] = 1;
        sd.sc[SC_OCT] = 2;
        sd.sc[SC_QUANT] = 0;
    } else if (rp == 21) {
        sd.sc[SC_TRANS] = 12;
        sd.sc[SC_FINE] = 50;
        sd.sc[SC_HUM] = 0;
        sd.sc[SC_SPREAD] = 64;
    } else {
        const uint8_t *base;
        int cnt;
        r = rp_row(rp);
        k = rp <= 7 ? PK_SYN : rp <= 11 ? PK_KICK : PK_FX;
        base = k == PK_SYN ? sd.syn : k == PK_KICK ? kick_p : sd.fx;
        cnt = k == PK_SYN ? NSYN : k == PK_KICK ? NKICK : NFX;
        pre_fact(k, 0, d);
        for (q = 0; r && q < 4; q++) {
            uint8_t *p = r->k[q].p;
            if (p && p >= base && p < base + cnt)
                *p = d[p - base];
        }
    }
    publish();
    app_say("INIT PAGE", rp_title(rp));
    touch();
    dirty_all();
}

/* OCT- + HOME: a random for melodies. The synth becomes a voice that holds a pitch (plain waves, the second oscillator in
 * unison, a fifth or an octave, no noise, sync or pitch envelope, little FM, a low-pass, a longer decay), the cables are
 * switched off, the scale is one of six with the quantizer on (the root stays), and the steps are a phrase: a four-step
 * figure that moves by neighbouring notes of the scale, said again and varied, ending on the root. The kick's sound and
 * the effects are left as they are; its line becomes four on the floor. */
static void random_melody(void)
{
    static const uint8_t WV[10] = {0, 1, 2, 3, 4, 5, 6, 8, 10, 15};      /* TRI SAW SQR PLS SIN PWM SSAW ORGAN BRASS BELL */
    static const uint8_t SCL[6] = {1, 2, 3, 6, 10, 11};                  /* MAJOR MINOR DORIAN MIXOLYD PENT MAJ PENT MIN */
    static const int8_t IV[5] = {0, 0, 12, -12, 7};
    pat_t *p = cur_pat();
    int8_t deg[24];
    int nd = 0, fig[4], d, root = (int)sd.sc[SC_ROOT] - (sd.sc[SC_ROOT] > 6u ? 12 : 0);
    uint32_t i, n;
    uint8_t *y = sd.syn;
    /* the voice */
    y[P_V1FRQ] = (uint8_t)(64 + 2 * ((int)(rnd() % 13u) - 6));
    y[P_V1WAV] = (uint8_t)(WV[rnd() % 10u] * 8u + 4u);
    y[P_V1LVL] = (uint8_t)(95 + rnd() % 33u);
    y[P_V1EG] = y[P_V2EG] = 0;
    y[P_V2FRQ] = (uint8_t)clamp((int)y[P_V1FRQ] + 2 * IV[rnd() % 5u] + (rnd() % 10u < 3u ? 1 : 0), 0, 127);
    y[P_V2WAV] = (uint8_t)(WV[rnd() % 10u] * 8u + 4u);
    y[P_V2LVL] = (uint8_t)(rnd() % 10u < 4u ? 0 : 50 + rnd() % 51u);
    y[P_FM] = (uint8_t)(rnd() % 10u < 8u ? 0 : 5 + rnd() % 21u);
    y[P_SYN] = 0;
    y[P_NSE] = 0;
    y[P_CUT] = (uint8_t)(60 + rnd() % 51u);
    y[P_RES] = (uint8_t)(rnd() % 41u);
    y[P_MODE] = (uint8_t)((rnd() & 1u) * 32u + 16u);     /* LP4 or LP2 */
    y[P_FEG] = (uint8_t)(20 + rnd() % 61u);
    y[P_FDEC] = (uint8_t)(40 + rnd() % 61u);
    y[P_ATK] = (uint8_t)(rnd() % 10u < 7u ? 0 : 8 + rnd() % 23u);
    y[P_ADEC] = (uint8_t)(55 + rnd() % 56u);
    y[P_VOL] = 100;
    y[P_DRV] = (uint8_t)(rnd() % 31u);
    y[P_ACC] = (uint8_t)(60 + rnd() % 41u);
    y[P_GLD] = (uint8_t)(rnd() % 10u < 6u ? 0 : 10 + rnd() % 31u);
    y[P_TUN] = 64;
    sd.pon = 0;                          /* no cable bends the pitch */
    /* the scale, and its notes over an octave and a half */
    sd.sc[SC_TYPE] = SCL[rnd() % 6u];
    sd.sc[SC_QUANT] = 1;
    for (d = 0; d < 20 && nd < 24; d++)
        if ((SCALES[sd.sc[SC_TYPE]].m >> (d % 12)) & 1u)
            deg[nd++] = (int8_t)d;
    /* the phrase */
    p->len = rnd() % 10u < 7u ? 16 : 8;
    p->swing = (uint8_t)(rnd() % 21u);
    if (sq.step >= p->len)
        sq.step = (uint8_t)(p->len - 1u);
    if (ui.sel >= p->len)
        ui.sel = (uint8_t)(p->len - 1u);
    d = 0;
    for (i = 0; i < 4u; i++) {           /* the figure: mostly a step up or down the scale, now and then a leap or the same note */
        uint32_t r = rnd() % 10u;
        fig[i] = d;
        d += r < 6u ? ((rnd() & 1u) ? 1 : -1) : r < 8u ? ((rnd() & 1u) ? 2 : -2) : r < 9u ? ((rnd() & 1u) ? 4 : -3) : 0;
        d = clamp(d, 0, nd - 1);
    }
    for (i = 0, n = p->len; i < n; i++) {
        int bar = (int)(i / 4u), g = fig[i % 4u] + (bar == 1 ? 2 : bar == 3 ? 1 : 0);    /* said again, a little higher */
        if (bar && rnd() % 5u == 0)
            g += (rnd() & 1u) ? 1 : -1;  /* and varied */
        if (i == n - 1u)
            g = rnd() % 3u ? 0 : nd > 4 ? 4 : 0;         /* the end: home (or, a time in three, up the scale) */
        g = clamp(g, 0, nd - 1);
        p->pit[i] = (int8_t)(root + deg[g]);
        p->trig[i] = (uint8_t)(i == 0 ? 2 : rnd() % 6u == 0 ? 0 : (i % 4u == 0 && rnd() % 3u == 0) ? 2 : 1);
        p->vel[i] = (uint8_t)(i % 4u == 0 ? 105 + rnd() % 16u : 72 + rnd() % 30u);
        p->gate[i] = (uint8_t)(rnd() % 3u == 0 ? GATE_FULL : 18 + rnd() % 15u);
        p->rat[i] = 0;
        p->cond[i] = 0;
        p->lk_p[i][0] = p->lk_p[i][1] = p->lk_v[i][0] = p->lk_v[i][1] = 0;
    }
    /* the kick: four on the floor */
    p->klen = 16;
    for (i = 0; i < NSTEP; i++) {
        p->ktrig[i] = (uint8_t)(i % 4u == 0 ? (i == 0 ? 2 : 1) : 0);
        p->kcond[i] = 0;
        p->krat[i] = 0;
    }
    if (sq.kstep >= p->klen)
        sq.kstep = 0;
    publish();
    app_say("RANDOM", "MELODY");
    touch();
    dirty_all();
}

/* ---------------------------------------------------------------- library --- */
/* HOME: the projects (page 1) and the user's presets (pages 2..6): a list, an action (load, save, rename, clear) and a K4 turn to do it */
static int lib_n(int t) { return t == 0 ? NPROJ : pre_count(t - 1); }
static int lib_used(int t, int i) { return t == 0 ? pj_used[i] : pre_used(t - 1, i); }
static const char *lib_name(int t, int i) { return t == 0 ? pj_name[i] : pre_name(t - 1, i); }
static char *lib_ren(int t, int i)       /* the name RENAME edits: the project's own, or a user preset's */
{
    if (t == 0)
        return sd.pname;
    if (i >= NFACT[t - 1])
        return up[t - 1].s[i - NFACT[t - 1]].n;
    return 0;
}
static void lib_commit(void)             /* a preset's new name goes to flash when the editing is over */
{
    if (ui.hren) {
        pre_save_obj(ui.hren - 1);
        ui.hren = 0;
    }
}
static void home_enter(void)
{
    for (int t = 1; t <= 5; t++)
        ui.hidx[t] = sd.pre[t - 1];
}
static void name_edit(char *s, int pos, int e)
{
    int n = 0, ci = 0;
    while (s[n])
        n++;
    while (n <= pos)
        s[n++] = ' ';
    s[n] = 0;
    for (int c = 0; c < NCHARS; c++)
        if (CHARS[c] == s[pos])
            ci = c;
    ci = (ci + e + NCHARS * 8) % NCHARS;
    if (pos == 0 && ci == 0)
        ci = e > 0 ? 1 : NCHARS - 1;
    s[pos] = CHARS[ci];
    while (n > 0 && s[n - 1] == ' ')
        s[--n] = 0;
}
static void lib_exec(int t, int i, int act)
{
    int k = t - 1, rc;
    if (act == 0) {                      /* LOAD */
        if (!lib_used(t, i)) {
            app_say("EMPTY", "");
        } else if (t == 0) {
            if (prj_load(i) == 0) {
                touch();
                app_say("PROJECT LOADED", pj_name[i]);
            } else {
                app_say("LOAD FAILED", "");
            }
        } else {
            pre_apply(k, i);
            touch();
            app_say("LOADED", pre_name(k, i));
        }
    } else if (act == 1) {               /* SAVE */
        if (t == 0) {
            rc = prj_save(i);
            app_say(rc ? "SAVE FAILED" : "PROJECT SAVED", rc ? "" : "BANKS A-C");
        } else if (i < NFACT[k]) {
            app_say("FACTORY PRESET", "PICK A USER SLOT");
        } else {
            rc = pre_save_slot(k, i - NFACT[k]);
            app_say(rc ? "SAVE FAILED" : "PRESET SAVED", rc ? "" : pre_name(k, i));
        }
    } else if (act == 3) {               /* CLEAR */
        if (t == 0) {
            rc = prj_clear(i);
            app_say(rc ? "FAILED" : "PROJECT CLEARED", "");
        } else if (i < NFACT[k]) {
            app_say("FACTORY PRESET", "CANNOT CLEAR");
        } else {
            rc = pre_clear_slot(k, i - NFACT[k]);
            app_say(rc ? "FAILED" : "PRESET CLEARED", "");
        }
    }
    dirty_all();
}
static void home_move(int32_t e)
{
    int t = (int)pg();
    if (t > 5)
        return;
    lib_commit();
    ui.hidx[t] = (uint8_t)clamp((int32_t)ui.hidx[t] + e, 0, lib_n(t) - 1);
    ui.d_vis = ui.d_foot = 1;
}
static void home_knob(uint32_t kn, int32_t e)
{
    int t = (int)pg();
    if (t > 5)
        return;
    if (kn == 0) {
        home_move(e);
    } else if (kn == 1) {
        lib_commit();
        ui.hact = (uint8_t)clamp((int32_t)ui.hact + (e > 0 ? 1 : -1), 0, 3);
    } else if (kn == 2) {
        if (ui.hact == 2)
            ui.hcur = (uint8_t)clamp((int32_t)ui.hcur + (e > 0 ? 1 : -1), 0, PN - 2);
    } else if (ui.hact == 2) {
        char *s = lib_ren(t, ui.hidx[t]);
        if (s) {
            name_edit(s, ui.hcur, e > 0 ? 1 : -1);
            if (t > 0)
                ui.hren = (uint8_t)t;
            else
                touch();
        } else {
            app_say("FACTORY PRESET", "CANNOT RENAME");
        }
    } else if (e > 0) {
        lib_exec(t, ui.hidx[t], ui.hact);
    }
    ui.d_vis = ui.d_foot = 1;
}

/* ------------------------------------------------------------- patterns --- */
static void pick_pattern(int pi)
{
    if (pi < 0 || pi >= NPAT)
        return;
    if (ui.mode == M_PATT && ui.cmd && pg() == 0) {
        char n[8];
        pat_name(pi, n);
        if (ui.cmd == 1) {
            ui.clip = (int16_t)pi;
            app_say("COPY", n);
        } else if (ui.cmd == 2 && ui.clip >= 0) {
            uint32_t f = plat_lock();
            snd_sync_to(sq.cur);         /* (the one playing may be the one copied) */
            pats[pi] = pats[ui.clip];    /* the steps and the sound with them */
            if (pi == sq.cur)
                snd_pull(pi);
            plat_unlock(f);
            touch();
            app_say("PASTE", n);
        } else if (ui.cmd == 3) {
            pat_clear(&pats[pi]);
            touch();
            app_say("CLEAR", n);
        } else if (ui.cmd == 4) {
            pat_clear(&pats[pi]);
            if (pi == (int)sq.cur) {     /* the pattern that plays: its steps and its sound, as OCT+ + REC but with steps of its own */
                random_flavour(RF_PATT);
            } else {                     /* another one: its steps (its sound is not the live one) */
                pat_flavour(&pats[pi], RF_PATT);
                touch();
                app_say("RAND", n);
            }
        }
        ui.d_vis = ui.d_foot = ui.d_strip = 1;
        return;
    }
    if (sq.running) {
        sq.queued = (int16_t)pi;
    } else {
        uint32_t f = plat_lock();
        snd_sync_to(sq.cur);
        sq.cur = (uint8_t)pi;
        sq.queued = -1;
        sq.step = (uint8_t)(pats[pi].len - 1u);
        sq.kstep = (uint8_t)(pats[pi].klen - 1u);
        snd_pull(pi);
        plat_unlock(f);
        if (ui.sel >= pats[pi].len)
            ui.sel = (uint8_t)(pats[pi].len - 1u);
        if (ui.ksel >= pats[pi].klen)
            ui.ksel = (uint8_t)(pats[pi].klen - 1u);
    }
    ui.d_vis = ui.d_foot = ui.d_strip = ui.d_head = 1;
}

static void go_mode(uint32_t m)
{
    lib_commit();
    if (sq.rec && m != M_SEQ) {          /* the recording is SEQ's: leaving SEQ ends it */
        sq.rec = 0;
        app_say("MIDI REC", "OFF");
    }
    ui.mode = (uint8_t)m;
    ui.nudge = ui.nudge_all = 0;
    if (m == M_HOME)
        home_enter();
    dirty_all();
}

static void rand_toggle(void)            /* REC: the random pages, on the page that mirrors the one we are on; again: back */
{
    if (ui.mode == M_RAND) {
        go_mode(ui.rmode);
    } else {
        int rp = rp_of(ui.mode, pg());
        ui.rmode = ui.mode;
        if (rp >= 0)
            ui.page[M_RAND] = (uint8_t)rp;
        go_mode(M_RAND);
    }
}

static void pattern_init(void)           /* OCT+ and REC held: the pattern we are in goes back to a new one, with the sound of a first power-on */
{
    uint32_t lf;
    char n[12];
    lf = plat_lock();
    pat_wipe(&pats[sq.cur]);             /* the steps, the kick's, its length and swing: empty */
    sound_defaults();                    /* the sound playing (it is this pattern's own) takes the first power-on one */
    publish();
    snd_sync_to(sq.cur);
    if (sq.step >= pats[sq.cur].len)
        sq.step = (uint8_t)(pats[sq.cur].len - 1u);
    if (sq.kstep >= pats[sq.cur].klen)
        sq.kstep = (uint8_t)(pats[sq.cur].klen - 1u);
    plat_unlock(lf);
    if (ui.sel >= pats[sq.cur].len)
        ui.sel = (uint8_t)(pats[sq.cur].len - 1u);
    if (ui.ksel >= pats[sq.cur].klen)
        ui.ksel = (uint8_t)(pats[sq.cur].klen - 1u);
    ui.held_step = 0xFFu;
    ui.turned = 0;
    touch();                             /* the autosave keeps it */
    pat_name(sq.cur, n);
    app_say("INIT", n);
    dirty_all();
}

/* the mode keys: the first press goes to the mode, every other press on the same mode goes to its next page (and from the
 * last one back to the first) */
static void page_to(uint32_t m, uint32_t p)
{
    lib_commit();
    ui.mode = (uint8_t)m;
    ui.page[m] = (uint8_t)p;
    ui.nudge = ui.nudge_all = 0;
    if (m == M_HOME)
        home_enter();
    dirty_all();
}
static void mode_key(uint32_t m, uint32_t first)    /* first: the page a key opens when it comes from another mode */
{
    if (ui.mode == m)
        page_to(m, (ui.page[m] + 1u) % NPAGE[m]);
    else if (first != 0xFFu)
        page_to(m, first);
    else
        go_mode(m);
}

static void on_button(uint32_t b)
{
    switch (b) {
    case B_SEQ:                                  /* OCT- held: the soft random, with the effects low; OCT+: the spread one */
        if ((plat_buttons() >> B_OCTDN) & 1u)
            random_flavour(RF_SOFT);
        else if ((plat_buttons() >> B_OCTUP) & 1u)
            random_flavour(RF_EUCLID);
        else
            mode_key(M_SEQ, 0xFFu);
        break;
    case B_EDIT: mode_key(M_SYN, 0xFFu); break;
    case B_ENV: mode_key(M_SET, 0xFFu); break;   /* the settings: audio, MIDI, Bluetooth, version */
    case B_LFO: mode_key(M_PATCH, 0xFFu); break;
    case B_FX: mode_key(M_FX, 0xFFu); break;
    case B_ARP: mode_key(M_KICK, 0xFFu); break;
    case B_SEL: mode_key(M_SCALE, 0xFFu); break;
    case B_HOME:                                 /* alone it does nothing (SAVE opens its pages). OCT- held: a melody; OCT+: percussion */
        if ((plat_buttons() >> B_OCTDN) & 1u)
            random_melody();
        else if ((plat_buttons() >> B_OCTUP) & 1u)
            random_perc();
        break;
    case B_GLO:                                  /* OCT- held: this page is initialised; OCT+: the pattern, its steps and its sound */
        if ((plat_buttons() >> B_OCTDN) & 1u) {
            if (page_init_rp() >= 0)
                page_init();
            else
                app_say("NO INIT", "ON THIS PAGE");
        } else if ((plat_buttons() >> B_OCTUP) & 1u) {
            pattern_init();
        } else {
            mode_key(M_PATT, 0xFFu);
        }
        break;
    case B_PLAY:
        ui.play_t = plat_ms();
        break;
    case B_SAVE:                                 /* SAVE opens HOME: the projects and presets (the autosave keeps the rest); */
        if ((plat_buttons() >> B_OCTDN) & 1u) {  /* with OCT- held, in SEQ: the MIDI recording on or off */
            if (ui.mode == M_SEQ) {
                sq.rec = (uint8_t)!sq.rec;
                app_say("MIDI REC", sq.rec ? "ON" : "OFF");
                ui.d_head = 1;
            } else {
                app_say("MIDI REC", "IN SEQ ONLY");
            }
        } else {
            mode_key(M_HOME, 0xFFu);
        }
        break;
    default:
        break;
    }
}

static void on_select(int32_t e)         /* SELECT: the page, inside the mode */
{
    int32_t p = clamp((int32_t)pg() + e, 0, (int32_t)NPAGE[ui.mode] - 1);
    if (p != (int32_t)pg()) {
        lib_commit();
        ui.page[ui.mode] = (uint8_t)p;
        ui.nudge = ui.nudge_all = 0;
        if (ui.mode == M_HOME)
            home_enter();
        dirty_all();
    }
}

static uint32_t cur_step_for_edit(void) { return ui.held_step != 0xFFu ? ui.held_step : (ui.mode == M_KICK ? ui.ksel : ui.sel); }

static void on_algo(int32_t e)           /* ALGORITHM: to move inside a page (else the tempo) */
{
    switch (ui.mode) {
    case M_SEQ:
        if (pg() == 2) {
            bpm_add(e);
            break;
        }
        ui.sel = (uint8_t)clamp((int32_t)ui.sel + e, 0, (int32_t)cur_len() - 1);
        ui.d_vis = ui.d_foot = ui.d_strip = 1;
        break;
    case M_KICK:
        if (kpg() >= 3u) {
            ui.ksel = (uint8_t)clamp((int32_t)ui.ksel + e, 0, (int32_t)cur_klen() - 1);
            ui.d_vis = ui.d_foot = ui.d_strip = 1;
        } else {
            bpm_add(e);
        }
        break;
    case M_PATCH:
        ui.pdst = (uint8_t)clamp((int32_t)ui.pdst + e, 0, NPATCH - 1);
        ui.d_vis = ui.d_foot = 1;
        break;
    case M_SET:
        if (pg() == SP_BT)
            bt_move(e);
        else
            bpm_add(e);
        break;
    case M_PATT:
        if (pg() == 1)
            ui.slot = (uint8_t)clamp((int32_t)ui.slot + e, 0, 31);
        else
            ui.bank = (uint8_t)clamp((int32_t)ui.bank + e, 0, NBANK - 1);
        ui.d_vis = ui.d_foot = 1;
        break;
    case M_HOME:
        home_move(e);
        break;
    default:
        bpm_add(e);
        break;
    }
}

static void rot_steps(uint8_t *a, uint32_t stride, uint32_t len, uint32_t sh)   /* a step's byte moves sh steps on, wrapping in len */
{
    uint8_t t[NSTEP];
    uint32_t i;
    for (i = 0; i < len; i++)
        t[(i + sh) % len] = a[i * stride];
    for (i = 0; i < len; i++)
        a[i * stride] = t[i];
}
static void seq_nudge(int32_t e)         /* SEQ, OCT- held, K4: the whole sequence moves e steps (right when e > 0), wrapping inside its length */
{
    pat_t *p = cur_pat();
    uint32_t len = p->len;
    int32_t r;
    if (len < 2u)
        return;
    r = e % (int32_t)len;
    if (r < 0)
        r += (int32_t)len;
    if (r) {
        uint32_t lf = plat_lock(), sh = (uint32_t)r;      /* the player reads these while it runs */
        rot_steps(p->trig, 1, len, sh);
        rot_steps((uint8_t *)p->pit, 1, len, sh);
        rot_steps(p->vel, 1, len, sh);
        rot_steps(p->cond, 1, len, sh);
        rot_steps(p->gate, 1, len, sh);
        rot_steps(p->rat, 1, len, sh);
        rot_steps(&p->lk_p[0][0], 2, len, sh);            /* a step's locks go with it */
        rot_steps(&p->lk_p[0][1], 2, len, sh);
        rot_steps(&p->lk_v[0][0], 2, len, sh);
        rot_steps(&p->lk_v[0][1], 2, len, sh);
        plat_unlock(lf);
    }
    touch();
    ui.d_vis = ui.d_foot = ui.d_strip = 1;
}
static void seq_knob(uint32_t kn, int32_t e)    /* SEQ: page 1 pitch, velocity, gate, ratchet; page 2 condition, trig, length, swing */
{
    pat_t *p = cur_pat();
    uint32_t s = cur_step_for_edit();
    if (pg() == 2) {                     /* the tempo: the only place a knob changes it (and ALGORITHM here) */
        if (kn == 0)
            bpm_add(fastk(e > 3 || e < -3 ? e * 2 : e));
        else if (kn == 1)
            bpm_add(e > 0 ? 10 : -10);
        return;
    }
    if (e > 3 || e < -3)
        e *= 2;
    if (pg() == 0) {
        if (kn < 3u)
            e = fastk(e);
        if (kn == 0)
            p->pit[s] = (int8_t)clamp(p->pit[s] + e, -24, 24);
        else if (kn == 1)
            p->vel[s] = (uint8_t)clamp((int32_t)p->vel[s] + e * 2, 0, 127);
        else if (kn == 2)
            p->gate[s] = (uint8_t)clamp((int32_t)p->gate[s] + e, 1, GATE_FULL);
        else
            p->rat[s] = (uint8_t)clamp((int32_t)p->rat[s] + e, 0, 3);
    } else {
        if (kn == 0) {
            p->cond[s] = (uint8_t)clamp((int32_t)p->cond[s] + e, 0, NCOND - 1);
        } else if (kn == 1) {
            p->trig[s] = (uint8_t)clamp((int32_t)p->trig[s] + (e > 0 ? 1 : -1), 0, 2);
        } else if (kn == 2) {
            p->len = (uint8_t)clamp((int32_t)p->len + e, 1, NSTEP);
            if (ui.sel >= p->len)
                ui.sel = (uint8_t)(p->len - 1u);
            if (sq.step >= p->len)
                sq.step = (uint8_t)(p->len - 1u);
        } else {
            p->swing = (uint8_t)clamp((int32_t)p->swing + e * 2, 0, 127);
        }
    }
    ui.turned = 1;
    touch();
    ui.d_vis = ui.d_foot = ui.d_strip = 1;
}

static void kick_nudge(int32_t e)        /* KICK SEQUENCE 2, K4: the kick's whole sequence moves e steps (right when e > 0), wrapping inside its length */
{
    pat_t *p = cur_pat();
    uint32_t len = p->klen;
    int32_t r;
    if (len < 2u)
        return;
    r = e % (int32_t)len;
    if (r < 0)
        r += (int32_t)len;
    if (r) {
        uint32_t lf = plat_lock(), sh = (uint32_t)r;      /* the player reads these while it runs */
        rot_steps(p->ktrig, 1, len, sh);
        rot_steps(p->kcond, 1, len, sh);
        rot_steps(p->krat, 1, len, sh);
        rot_steps((uint8_t *)p->kpit, 1, len, sh);        /* a step's pitch and volume go with it */
        rot_steps(p->kvol, 1, len, sh);
        plat_unlock(lf);
    }
    touch();
    ui.d_vis = ui.d_foot = ui.d_strip = 1;
}
/* OCT- held in SEQ or in the kick's sequence pages: K3 moves both sequences together, K4 the one of the page */
static void nudge_knob(uint32_t k, int32_t e)
{
    if (k == 2u) {
        seq_nudge(e);
        kick_nudge(e);
        ui.nudge_all = (int8_t)clamp((int32_t)ui.nudge_all + e, -99, 99);
    } else if (k == 3u) {
        if (ui.mode == M_SEQ)
            seq_nudge(e);
        else
            kick_nudge(e);
        ui.nudge = (int8_t)clamp((int32_t)ui.nudge + e, -99, 99);
    }
    ui.d_foot = 1;
}
/* KICK SEQUENCE 1: trig, pitch, volume, length. KICK SEQUENCE 2: condition, ratchet */
static void kseq_knob(uint32_t kn, int32_t e)
{
    pat_t *p = cur_pat();
    uint32_t s = cur_step_for_edit();
    int32_t e0 = e;                      /* (the nudge is a step for each click, whatever the speed) */
    if (e > 3 || e < -3)
        e *= 2;
    if (kpg() == 3u) {
        if (kn == 0) {
            p->ktrig[s] = (uint8_t)clamp((int32_t)p->ktrig[s] + (e > 0 ? 1 : -1), 0, 2);
        } else if (kn == 1) {
            p->kpit[s] = (int8_t)clamp(p->kpit[s] + fastk(e), -24, 24);
        } else if (kn == 2) {
            p->kvol[s] = (uint8_t)clamp((int32_t)p->kvol[s] + fastk(e) * 2, 0, 127);
        } else {
            p->klen = (uint8_t)clamp((int32_t)p->klen + e, 1, NSTEP);
            if (ui.ksel >= p->klen)
                ui.ksel = (uint8_t)(p->klen - 1u);
            if (sq.kstep >= p->klen)
                sq.kstep = (uint8_t)(p->klen - 1u);
        }
    } else if (kn == 0) {
        p->kcond[s] = (uint8_t)clamp((int32_t)p->kcond[s] + e, 0, NCOND - 1);
    } else if (kn == 1) {
        p->krat[s] = (uint8_t)clamp((int32_t)p->krat[s] + e, 0, 3);
    }
    (void)e0;
    ui.turned = 1;
    touch();
    ui.d_vis = ui.d_foot = ui.d_strip = 1;
}

static void patch_knob(uint32_t kn, int32_t e)  /* PATCH: source, destination, depth, on/off */
{
    uint32_t d = ui.pdst;
    if (kn == 0) {
        int v = sd.src[d];
        if (v == 0 && e < 0)
            e = 0;
        sd.src[d] = (uint8_t)clamp(v + e, 1, NSRC - 1);
    } else if (kn == 1) {
        ui.pdst = (uint8_t)clamp((int32_t)ui.pdst + e, 0, NPATCH - 1);
    } else if (kn == 2) {
        if (e > 3 || e < -3)
            e *= 2;
        sd.amt[d] = (uint8_t)clamp((int32_t)sd.amt[d] + fastk(e), 1, 127);
    } else {
        if (e > 0) {
            if (sd.src[d] == 0)
                sd.src[d] = S_SEQP;
            sd.pon |= (uint8_t)(1u << d);
        } else {
            sd.pon &= (uint8_t)~(1u << d);
        }
    }
    publish();
    touch();
    ui.d_vis = ui.d_foot = 1;
}

static void snd_copy_to(int first, int n)    /* the sound being played, as the own sound of the patterns first .. first+n-1 */
{
    uint32_t f = plat_lock();
    snd_sync_to(sq.cur);
    for (int i = first; i < first + n; i++)
        if (i != sq.cur) {
            for (int k = 0; k < SNDSZ; k++)
                pats[i].snd[k] = pats[sq.cur].snd[k];
            for (int k = 0; k < NPK; k++)
                pats[i].pre[k] = pats[sq.cur].pre[k];
            pats[i].sok = 1;
        }
    plat_unlock(f);
}

/* BLUETOOTH, the fifth page of PATTERNS: K1 switches it on or off, K2 and ALGORITHM go through the devices (the ones kept,
 * then the ones a scan found), K3 is the action, K4 turned to the right does it. */
enum { BA_SCAN, BA_CONNECT, BA_HANGUP, BA_FORGET, BA_OUTPUT, BA_QUALITY, BA_MIDI, NBA };
static const char *const BT_ACT[NBA] = {"SCAN", "CONNECT", "HANG UP", "FORGET", "OUTPUT", "QUALITY", "BT MIDI"};
static const char *const BT_Q[3] = {"HIGH", "MID", "LOW"};
#define BT_NENT 12
static struct { uint8_t n, kn, idx[BT_NENT]; char name[BT_NENT][17]; } bt;   /* the list: kn kept devices, then the found ones */
static int streq(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void bt_list(void)                /* the devices on the page: the ones kept, then the found ones that are not among them */
{
    char nm[17];
    int i, j;
    bt.n = bt.kn = 0;
    if (!bts.on)
        return;
    for (i = 0; i < 4 && bt.n < BT_NENT && plat_bta_known(i, nm); i++) {
        scopy(bt.name[bt.n], nm, 17);
        bt.idx[bt.n++] = (uint8_t)i;
    }
    bt.kn = bt.n;
    for (i = 0; i < 8 && bt.n < BT_NENT && plat_bta_found(i, nm); i++) {
        for (j = 0; j < bt.kn && !streq(nm, bt.name[j]); j++)
            ;
        if (j < bt.kn)
            continue;
        scopy(bt.name[bt.n], nm, 17);
        bt.idx[bt.n++] = (uint8_t)i;
    }
    if (ui.bt_sel >= bt.n)
        ui.bt_sel = bt.n ? (uint8_t)(bt.n - 1u) : 0;
}
static const char *bt_word(int st)
{
    static char f[16];
    switch (st) {
    case PLAT_BTA_SCANNING: return "SCANNING";
    case PLAT_BTA_FOUND:
        int_text((int32_t)(bt.n - bt.kn), f);
        text_cat(f, " FOUND");
        return f;
    case PLAT_BTA_NONE: return bt_scanned ? "NOTHING FOUND" : "READY";
    case PLAT_BTA_PAGING: return "CONNECTING";
    case PLAT_BTA_LINKED: return "LINKED";
    case PLAT_BTA_SETUP: return "SETTING UP";
    case PLAT_BTA_STREAMING: return plat_bta_out_device() ? "STREAMING" : "LINKED";
    case PLAT_BTA_REFUSED: return plat_bta_timed_out() ? "NO ANSWER" : "REFUSED";
    case PLAT_BTA_NO_LAST: return "NO DEVICE KEPT";
    case PLAT_BTA_FAILED: return "FAILED";
    case PLAT_BTA_READY: return "READY";
    default: return "OFF";
    }
}
static void bt_start(void)               /* the audio radio, once: at power-on if it was left on, or when it is switched on */
{
    if (bts.on && !bt_up) {
        bt_up = 1;
        (void)plat_bta_quality(BT_QUAL);
        plat_bta_on(1);
    }
}
static void bt_set(int on)
{
    if (on == bts.on)
        return;
    bts.on = (uint8_t)on;
    bts_dirty = 1;
    bts_t = plat_ms();
    if (on) {
        plat_ble_enable(1);
        (void)plat_bta_quality(BT_QUAL);
        bt_start();
        app_say("BLUETOOTH", "ON");
    } else {
        plat_bta_hangup();
        plat_bta_on(0);
        plat_ble_enable(0);
        bt_up = 0;
        bt_scanned = 0;
        app_say("BLUETOOTH", "OFF");
    }
    ui.bt_sel = 0;
    ui.d_vis = ui.d_foot = 1;
}
static void bt_do(void)                  /* K4: the action on the device in the list */
{
    int s = ui.bt_sel;
    if (!bts.on) {
        app_say("BLUETOOTH IS OFF", "K1: SWITCH IT ON");
        return;
    }
    bt_list();
    switch (ui.bt_act) {
    case BA_SCAN:
        bt_scanned = 1;
        plat_bta_scan(1);
        app_say("BLUETOOTH", "SCANNING");
        break;
    case BA_CONNECT:
        if (s >= bt.n) {
            app_say("NO DEVICE", "SCAN FIRST");
            break;
        }
        if (s < bt.kn)
            (void)plat_bta_reconnect(bt.idx[s]);
        else
            plat_bta_pick(bt.idx[s]);
        app_say("CONNECTING", bt.name[s]);
        break;
    case BA_HANGUP:
        plat_bta_hangup();
        app_say("BLUETOOTH", "DISCONNECTED");
        break;
    case BA_FORGET:
        if (s < bt.kn) {
            plat_bta_forget(bt.idx[s]);
            app_say("FORGOTTEN", bt.name[s]);
        } else {
            app_say("NOT KEPT", "ONLY KEPT DEVICES");
        }
        break;
    case BA_QUALITY:                     /* HIGH > MID > LOW > HIGH: fewer bits and less of the air for the stream */
        bts.quality = (uint8_t)((bts.quality & 0xF0u) | ((BT_QUAL + 1u) % 3u));
        (void)plat_bta_quality(BT_QUAL);
        bts_dirty = 1;
        bts_t = plat_ms();
        app_say("BT QUALITY", BT_QUAL == 0 ? "HIGH 289K" : BT_QUAL == 1 ? "MID 212K" : "LOW 174K");
        break;
    case BA_MIDI:                        /* Bluetooth MIDI's advertising, which shares the air with the sound */
        bts.midioff = (uint8_t)!bts.midioff;
        plat_ble_enable(!bts.midioff);
        bts_dirty = 1;
        bts_t = plat_ms();
        app_say("BT MIDI", bts.midioff ? "OFF" : "ON");
        break;
    default:
        plat_bta_output(!plat_bta_out_device());
        app_say("SOUND TO", plat_bta_out_device() ? "BLUETOOTH" : "FM-1 OUTPUT");
        break;
    }
    ui.d_vis = ui.d_foot = 1;
}
static void bt_move(int32_t e)
{
    bt_list();
    if (bt.n)
        ui.bt_sel = (uint8_t)clamp((int32_t)ui.bt_sel + e, 0, (int32_t)bt.n - 1);
    ui.d_vis = ui.d_foot = 1;
}
static void bt_knob(uint32_t kn, int32_t e)
{
    if (kn == 0u)
        bt_set(e > 0);
    else if (kn == 1u)
        bt_move(e);
    else if (kn == 2u)
        ui.bt_act = (uint8_t)clamp((int32_t)ui.bt_act + e, 0, NBA - 1);
    else if (e > 0)
        bt_do();
    ui.d_vis = ui.d_foot = 1;
}
static void bt_poll(void)                /* the radio's news: a link that ended says so, and the page follows what it finds */
{
    static uint32_t t, last;
    static uint8_t was_streaming;
    uint32_t now = plat_ms(), h = 0;
    int w;
    if (!bts.on)
        return;
    {
        uint8_t sti = (uint8_t)(plat_bta_state() == PLAT_BTA_STREAMING);
        if (sti && !was_streaming && bts.midioff)
            plat_ble_enable(0);          /* the link is up: the radio's start-up would have switched the advertising on again */
        was_streaming = sti;
    }
    if ((w = plat_bta_lost()) != PLAT_BTA_LOST_NONE)
        app_say("BLUETOOTH", w == PLAT_BTA_LOST_LINK ? "LINK LOST" : w == PLAT_BTA_LOST_PAIRING ? "PAIRING FAILED" :
                w == PLAT_BTA_LOST_ENCRYPTION ? "SECURITY FAILED" : w == PLAT_BTA_LOST_SETUP ? "SET-UP FAILED" :
                w == PLAT_BTA_LOST_STREAMING ? "STREAM LOST" : "REFUSED");
    if (ui.mode != M_SET || ui.page[M_SET] != SP_BT || now - t < 200u)
        return;
    t = now;
    bt_list();
    h = (uint32_t)plat_bta_state() * 31u + (uint32_t)plat_bta_out_device() + bt.n * 977u + bt.kn * 7919u;
    if (plat_bta_state() == PLAT_BTA_STREAMING && plat_bta_out_device()) {    /* (the live readout follows) */
        uint32_t lk[7];
        h += plat_cpu_pct() * 104729u + (uint32_t)(plat_bta_latency_us() / 1000) * 1299709u;
        if (plat_bta_link(lk))
            h += lk[0] * 15485863u + lk[1] * 32452843u + lk[2] * 49979687u + lk[3] * 67867967u + lk[4] * 86028121u +
                 lk[5] * 104395301u + lk[6] * 122949829u;
    }
    for (int i = 0; i < bt.n; i++)
        for (const char *c = bt.name[i]; *c; c++)
            h = h * 33u + (uint8_t)*c;
    if (h != last) {
        last = h;
        ui.d_vis = ui.d_foot = 1;
    }
}

static void audio_knob(uint32_t kn, int32_t e)  /* AUDIO: K1 the USB recording, K2 the speaker and the jack, K3 the USB level; right ON (FULL), left OFF (MASTER) */
{
    static const uint8_t BIT[3] = {BQ_USB_OFF, BQ_LINE_OFF, BQ_USB_FULL};
    static const char *const NM[3] = {"USB AUDIO", "SPEAKER + JACK", "USB LEVEL"};
    if (kn < 3u) {
        uint8_t set = (uint8_t)(kn == 2u ? e > 0 : e < 0), was = (uint8_t)((bts.quality & BIT[kn]) != 0);
        if (set != was) {
            bts.quality = (uint8_t)((bts.quality & (uint8_t)~BIT[kn]) | (set ? BIT[kn] : 0u));
            out_apply();
            bts_dirty = 1;
            bts_t = plat_ms();
            app_say(NM[kn], kn == 2u ? (set ? "FULL" : "MASTER") : (set ? "OFF" : "ON"));
        }
    }
    ui.d_vis = ui.d_foot = 1;
}
static void ms_keep(void)                /* a MIDI setting changed: to the record, written when things are quiet */
{
    for (uint32_t i = 0; i < sizeof bts.set; i++)
        bts.set[i] = midi_set[i];
    bts_dirty = 1;
    bts_t = plat_ms();
}
static void midi_knob(uint32_t kn, int32_t e)   /* MIDI page 1: K1 the clock in, K2 the clock out (right ON, left OFF), K3 the pulse sync */
{
    static const char *const PP[5] = {"OFF", "1 PPQN", "2 PPQN", "4 PPQN", "24 PPQN"};
    if (kn == 0u) {
        uint8_t on = (uint8_t)(e > 0);
        if (on != xs.on) {
            xs.on = on;                  /* (off: the render lets the clock go at its next block) */
            bts.quality = (uint8_t)((bts.quality & (uint8_t)~BQ_SYNC) | (on ? BQ_SYNC : 0u));
            bts_dirty = 1;
            bts_t = plat_ms();
            app_say("CLOCK IN", on ? "ON" : "OFF");
        }
    } else if (kn == 1u) {
        uint8_t on = (uint8_t)(e > 0);
        if (on != MS_CKOUT) {
            MS_CKOUT = on;
            ms_keep();
            app_say("CLOCK OUT", on ? "ON" : "OFF");
        }
    } else if (kn == 2u) {
        uint8_t v = (uint8_t)clamp((int32_t)MS_PULSE + (e > 0 ? 1 : -1), 0, 4);
        if (v != MS_PULSE) {
            MS_PULSE = v;
            ms_keep();
            app_say("PULSE OUT", PP[v]);
        }
    }
    ui.d_vis = ui.d_foot = ui.d_head = 1;
}
static void midi2_knob(uint32_t kn, int32_t e)  /* MIDI page 2: the channels, a click a channel */
{
    if (kn < 3u) {
        volatile uint8_t *p = kn == 0u ? &MS_INCH : kn == 1u ? &MS_OUTCH : &MS_KICKCH;
        uint8_t v = (uint8_t)clamp((int32_t)*p + e, 0, 16);
        if (v != *p) {
            if (kn)
                mo_offs();               /* (a note out there ends on the channel it went out on) */
            *p = v;
            ms_keep();
        }
    }
    ui.d_vis = ui.d_foot = 1;
}
static void set_knob(uint32_t kn, int32_t e)    /* ENV's pages */
{
    if (pg() == SP_AUDIO)
        audio_knob(kn, e);
    else if (pg() == SP_MIDI)
        midi_knob(kn, e);
    else if (pg() == SP_MIDI2)
        midi2_knob(kn, e);
    else if (pg() == SP_BT)
        bt_knob(kn, e);
}
static void patt_knob(uint32_t kn, int32_t e)   /* PATTERNS: pick, command, bank, song / SONG: slot, pattern, clear, song / presets / Bluetooth */
{
    if (pg() == 0) {
        if (kn == 0)
            pick_pattern(clamp(((sq.queued >= 0 ? sq.queued : sq.cur) + e), 0, NPAT - 1));
        else if (kn == 1)
            ui.cmd = (uint8_t)clamp((int32_t)ui.cmd + e, 0, 4);
        else if (kn == 2)
            ui.bank = (uint8_t)clamp((int32_t)ui.bank + e, 0, NBANK - 1);
        else
            sd.song_on = (uint8_t)(e > 0 ? 1 : 0);
    } else if (pg() == 1) {
        if (kn == 0) {
            ui.slot = (uint8_t)clamp((int32_t)ui.slot + e, 0, 31);
        } else if (kn == 1) {
            int v = (int)sd.song[ui.slot] >= NPAT ? 0 : sd.song[ui.slot] + 1;
            v = clamp(v + e, 0, NPAT);
            sd.song[ui.slot] = (uint8_t)(v ? v - 1 : 0xFF);
        } else if (kn == 2) {
            if (e)
                sd.song[ui.slot] = 0xFF;
        } else {
            sd.song_on = (uint8_t)(e > 0 ? 1 : 0);
        }
    } else if (pg() == 2) {              /* the sound of the pattern playing: a preset of each kind into it (the others keep theirs) */
        pre_step((int)kn, e > 0 ? 1 : -1);
    } else if (kn == 0) {                /* the scale preset */
        pre_step(PK_SCALE, e > 0 ? 1 : -1);
    } else if (e > 0) {
        if (kn == 1) {                   /* this sound into the whole bank */
            snd_copy_to((int)(sq.cur / NPB) * NPB, NPB);
            app_say("SOUND TO BANK", "");
        } else if (kn == 2) {            /* ... into every pattern */
            snd_copy_to(0, NPAT);
            app_say("SOUND TO ALL", "");
        } else {                         /* back to the sound of a first power-on */
            sound_defaults();
            publish();
            app_say("SOUND RESET", "");
        }
    }
    touch();
    ui.d_vis = ui.d_foot = ui.d_strip = 1;
}

static void rand_knob(uint32_t kn, int32_t e)   /* RANDOM: how much of each knob */
{
    uint8_t *a = &sd.rnd_amt[pg()][kn];
    if (e > 3 || e < -3)
        e *= 2;
    *a = (uint8_t)clamp((int32_t)*a + fastk(e) * 2, 0, 127);
    touch();
    ui.d_vis = ui.d_foot = 1;
}

static void fx_four(void)                /* the mixer's last black key: delay, reverb, DJ filter and stutter off together, and back as they were */
{
    const uint8_t four = FX_DLY | FX_REV | FX_DJF | FX_STU;
    if (sd.fxen & four) {
        ui.fx_kept = (uint8_t)(sd.fxen & four);
        sd.fxen &= (uint8_t)~four;
        ui.stut_mom = 0;
        app_say("DLY REV DJF STU", "OFF");
    } else {
        sd.fxen |= ui.fx_kept ? ui.fx_kept : (uint8_t)(FX_DLY | FX_REV | FX_DJF);   /* (none remembered: the three that play on) */
        app_say("DLY REV DJF STU", "BACK ON");
    }
    publish();
    touch();
    ui.d_vis = ui.d_foot = 1;
}
static void fx_key(uint32_t k, int down)        /* FX: the keys switch the effects; the stutter latches on a tap, plays on a hold */
{
    const uint8_t *B = pg() == 6u ? FXB_MIX : FXB_PAGE, *I = pg() == 6u ? FXI_MIX : FXI_PAGE;
    if (k >= 6u)
        return;
    if (B[k] == FX_STU) {
        if (down) {
            if (sd.fxen & FX_STU) {
                sd.fxen &= (uint8_t)~FX_STU;
                ui.stut_mom = 0;
            } else {
                sd.fxen |= FX_STU;
                ui.stut_t = plat_ms();
                ui.stut_mom = 1;
            }
        } else if (ui.stut_mom) {
            if (plat_ms() - ui.stut_t >= 350u)
                sd.fxen &= (uint8_t)~FX_STU;
            ui.stut_mom = 0;
        }
        publish();
    } else if (down) {
        sd.fxen ^= B[k];
        publish();
        touch();
        app_say(FX_NAME[I[k]], (sd.fxen & B[k]) ? "ON" : "OFF");
    }
    ui.d_vis = ui.d_foot = 1;
}

static void rand_key(uint32_t k)        /* RANDOM: keys 1-4 switch the knobs, 5 all of them, 6 a random now, 7 a random of every page */
{
    uint32_t rp = pg();
    if (k < 4u)
        sd.rnd_en[rp] ^= (uint8_t)(1u << k);
    else if (k == 4u)
        sd.rnd_en[rp] = (sd.rnd_en[rp] & 0x0Fu) == 0x0Fu ? 0 : 0x0Fu;
    else if (k == 5u)
        random_run(0);
    else if (k == 6u)
        random_run(2);
    touch();
    ui.d_vis = ui.d_foot = 1;
}

static void preset_turn(int32_t e)      /* PRESETS: the sound of the mode */
{
    int k;
    switch (ui.mode) {
    case M_SET:
        if (pg() == SP_BT)
            bt_move(e);
        return;
    case M_PATT:
        pick_pattern(clamp(((sq.queued >= 0 ? sq.queued : sq.cur) + e), 0, NPAT - 1));
        return;
    case M_HOME:
        home_move(e);
        return;
    case M_RAND:
        on_select(e);
        return;
    case M_KICK: k = PK_KICK; break;
    case M_FX: k = PK_FX; break;
    case M_PATCH: k = PK_PATCH; break;
    case M_SCALE: k = PK_SCALE; break;
    default: k = PK_SYN; break;
    }
    pre_step(k, e > 0 ? 1 : -1);
    app_say(pre_name(k, sd.pre[k]), "");
    touch();
    dirty_all();
}

static void input(void)
{
    uint32_t btn = plat_buttons(), keys = plat_keys(), k, now = plat_ms();
    int32_t e;
    uint32_t dn = (btn >> B_OCTDN) & 1u, upb = (btn >> B_OCTUP) & 1u, rec = (btn >> B_REC) & 1u;
    uint32_t pdn = (ui.btn >> B_OCTDN) & 1u, pup = (ui.btn >> B_OCTUP) & 1u, prec = (ui.btn >> B_REC) & 1u;
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (k != B_REC && (btn & bit) && !(ui.btn & bit))
            on_button(k);
    }
    if (((btn >> B_PLAY) & 1u) && !((ui.btn >> B_PLAY) & 1u)) {   /* PLAY pressed: run / stop at once */
        if (sq.running)
            seq_stop();
        else
            seq_start();
        ui.d_strip = ui.d_head = ui.d_vis = 1;
    }
    if (ui.play_t && ((btn >> B_PLAY) & 1u) && now - ui.play_t >= 700u) {   /* held: stopped, back to the first step */
        seq_stop();
        seq_rewind();
        ui.play_t = 0;
        app_say("STOP", "RESET");
        ui.d_strip = ui.d_head = 1;
    }
    if (!((btn >> B_PLAY) & 1u))
        ui.play_t = 0;
    /* REC: a tap (alone) opens the random pages. With OCT- held: a random of this page. With OCT+ held: a random of the
     * steps, the synth and the kick. Both at once, as REC goes down: nothing waits for a hold. */
    if (rec && !prec) {
        ui.rec_chord = 0;
        if (ui.held_step != 0xFFu && ui.mode != M_KICK && ui.mode != M_RAND && ui.mode != M_FX && ui.mode != M_PATT &&
            ui.mode != M_HOME && ui.mode != M_SET && ui.mode != M_SCALE) {           /* a step held: its locks and its settings cleared */
            pat_t *p = cur_pat();
            uint32_t s = ui.held_step;
            p->lk_p[s][0] = p->lk_p[s][1] = 0;
            p->pit[s] = 0;
            p->vel[s] = 100;
            p->cond[s] = 0;
            p->gate[s] = GATE_FULL;
            p->rat[s] = 0;
            ui.turned = 1;
            ui.rec_chord = 1;
            touch();
            app_say("STEP", "CLEARED");
            ui.d_vis = ui.d_foot = ui.d_strip = 1;
        } else if (ui.mode == M_KICK && ui.held_step != 0xFFu) {  /* a kick step held: cleared */
            pat_t *p = cur_pat();
            uint32_t s = ui.held_step;
            p->ktrig[s] = 0;
            p->kcond[s] = 0;
            p->krat[s] = 0;
            p->kpit[s] = 0;
            p->kvol[s] = KVOL_DEF;
            ui.turned = 1;
            ui.rec_chord = 1;
            touch();
            app_say("KICK STEP", "CLEARED");
            ui.d_vis = ui.d_foot = ui.d_strip = 1;
        } else if (dn) {                 /* OCT- was down first: a random of this page (OCT+ as well changes nothing) */
            random_run(0);
            ui.rec_chord = 2;
        } else if (upb) {                /* OCT+ was down first: the random of the steps, the synth and the kick */
            random_run(2);
            ui.rec_chord = 2;
        }
    } else if (rec && prec && ui.rec_chord != 2) {
        if (dn && !pdn) {                /* OCT- came down with REC held */
            random_run(0);
            ui.rec_chord = 2;
        } else if (upb && !pup && !dn) { /* OCT+ came down with REC held */
            random_run(2);
            ui.rec_chord = 2;
        }
    }
    if (!rec && prec && !ui.rec_chord)
        rand_toggle();
    /* the white keys */
    ui.held_step = 0xFFu;
    for (k = 0; k < 16u; k++) {
        uint32_t bit = 1u << WK[k];
        int d = (keys & bit) != 0, was = (ui.keys & bit) != 0;
        if (upb && k >= 14u) {           /* OCT+ held, any page: the last white key mutes the kick, the one before it the synth */
            if (d && !was) {
                if (k == 15u) {
                    sq.mute_k = (uint8_t)!sq.mute_k;
                    sq.kn = 0;
                    app_say("KICK", sq.mute_k ? "MUTED" : "ON");
                } else {
                    sq.mute_s = (uint8_t)!sq.mute_s;
                    sq.rn = 0;
                    app_say("SYNTH", sq.mute_s ? "MUTED" : "ON");
                }
                ui.d_head = 1;
            }
            continue;
        }
        if (ui.mode == M_FX) {
            if (d != was)
                fx_key(k, d);
        } else if (ui.mode == M_SET) {
            ;                            /* (the settings pages have no keys) */
        } else if (ui.mode == M_PATT) {
            if (d && !was)
                pick_pattern((int)ui.bank * NPB + (int)k);
        } else if (ui.mode == M_RAND) {
            if (d && !was)
                rand_key(k);
        } else if (ui.mode == M_HOME) {
            if (d && !was && pg() == 0 && k < NPROJ) {
                lib_commit();
                ui.hidx[0] = (uint8_t)k;
                ui.d_vis = ui.d_foot = 1;
            }
        } else if (ui.mode == M_SCALE) {
            /* all the keys set the root, below */
        } else if (k < steps_len()) {
            if (d) {
                if (ui.held_step == 0xFFu)
                    ui.held_step = k;
                if (!was) {
                    if (ui.mode == M_KICK)
                        ui.ksel = (uint8_t)k;
                    else
                        ui.sel = (uint8_t)k;
                    ui.turned = 0;
                    ui.d_vis = ui.d_foot = ui.d_strip = 1;
                }
            } else if (was) {            /* released: a tap toggles the step (unless a knob or REC was used) */
                if (!ui.turned) {
                    uint8_t *t = ui.mode == M_KICK ? &cur_pat()->ktrig[k] : &cur_pat()->trig[k];
                    *t = (uint8_t)((*t + 1u) % 3u);
                    touch();
                }
                ui.d_vis = ui.d_foot = ui.d_strip = 1;
            }
        }
    }
    if (ui.mode == M_FX && pg() == 6u && (keys & (1u << BK[10])) && !(ui.keys & (1u << BK[10])))
        fx_four();
    if (ui.mode == M_PATT) {             /* the black keys: the banks */
        for (k = 0; k < 11u; k++) {
            uint32_t bit = 1u << BK[k];
            if ((keys & bit) && !(ui.keys & bit)) {
                ui.bank = (uint8_t)k;
                ui.d_vis = ui.d_foot = 1;
            }
        }
    } else if (ui.mode == M_SCALE) {     /* every key sets the root (key 0 is an F) */
        for (k = 0; k < 27u; k++)
            if ((keys & (1u << k)) && !(ui.keys & (1u << k))) {
                sd.sc[SC_ROOT] = (uint8_t)((5u + k) % 12u);
                touch();
                ui.d_vis = ui.d_foot = 1;
            }
    }
    /* encoders */
    if ((e = plat_enc(EN_ALGO)) != 0)
        on_algo(e);
    if ((e = plat_enc(EN_PRESET)) != 0)
        preset_turn(e);
    if ((e = plat_enc(EN_SELECT)) != 0)
        on_select(e);
    ui.fast = (uint8_t)(dn && !rec);
    if (ui.fast != ui.fast_was) {        /* OCT- down or up: SEQ's footer is the nudge while it is down */
        ui.fast_was = ui.fast;
        if (ui.mode == M_SEQ || (ui.mode == M_KICK && kpg() >= 3u)) {
            ui.nudge = ui.nudge_all = 0;
            ui.d_foot = 1;
        }
    }
    for (k = 0; k < 4u; k++) {
        if ((e = plat_enc((int)(EN_K1 + k))) == 0)
            continue;
        if (ui.fast && (ui.mode == M_SEQ || (ui.mode == M_KICK && kpg() >= 3u))) {   /* OCT- held: the nudges */
            nudge_knob(k, e);
            continue;
        }
        if (ui.mode == M_SEQ)
            seq_knob(k, e);
        else if (ui.mode == M_PATCH)
            patch_knob(k, e);
        else if (ui.mode == M_PATT)
            patt_knob(k, e);
        else if (ui.mode == M_SET)
            set_knob(k, e);
        else if (ui.mode == M_RAND)
            rand_knob(k, e);
        else if (ui.mode == M_HOME)
            home_knob(k, e);
        else if (ui.mode == M_KICK && kpg() >= 3u)
            kseq_knob(k, e);
        else
            knob_turn(k, e);
    }
    ui.btn = btn;
    ui.keys = keys;
    /* the lights: the playing step bright, the steps that sound dim, accents a notch brighter. In FX the keys that are
     * on; in PATTERNS the pattern playing (bright) and the one queued (dim), the black keys the bank; in SCALE the notes. */
    {
        uint32_t lit_k = 0, dim_k = 0, mid_k = 0;
        uint32_t lit_b = 1u << (ui.mode == M_SEQ ? B_SEQ : ui.mode == M_SYN ? B_EDIT :
                         ui.mode == M_PATCH ? B_LFO : ui.mode == M_KICK ? B_ARP : ui.mode == M_FX ? B_FX :
                         ui.mode == M_PATT ? B_GLO : ui.mode == M_RAND ? B_REC : ui.mode == M_SCALE ? B_SEL :
                         ui.mode == M_SET ? B_ENV : B_SAVE);
        pat_t *p = cur_pat();
        if (sq.rec)                      /* recording: SAVE lit too */
            lit_b |= 1u << B_SAVE;
        if (ui.mode == M_FX) {
            const uint8_t *B = pg() == 6u ? FXB_MIX : FXB_PAGE;
            for (k = 0; k < 6u; k++)
                if (sd.fxen & B[k])
                    lit_k |= 1u << WK[k];
                else
                    dim_k |= 1u << WK[k];
            if (pg() == 6u) {            /* the last black key: the four off (lit: they wait to come back) */
                if (!(sd.fxen & (FX_DLY | FX_REV | FX_DJF | FX_STU)) && ui.fx_kept)
                    lit_k |= 1u << BK[10];
                else
                    dim_k |= 1u << BK[10];
            }
        } else if (ui.mode == M_RAND) {
            for (k = 0; k < 7u; k++)
                if (k < 4u && ((sd.rnd_en[pg()] >> k) & 1u))
                    lit_k |= 1u << WK[k];
                else
                    dim_k |= 1u << WK[k];
        } else if (ui.mode == M_SCALE) {
            for (k = 0; k < 27u; k++) {
                int pc = (int)((5u + k) % 12u);
                if (pc == sd.sc[SC_ROOT])
                    lit_k |= 1u << k;
                else if (scale_has(pc))
                    mid_k |= 1u << k;
            }
        } else if (ui.mode == M_HOME) {
            if (pg() == 0)
                for (k = 0; k < NPROJ; k++) {
                    if (k == ui.hidx[0])
                        lit_k |= 1u << WK[k];
                    else if (pj_used[k])
                        dim_k |= 1u << WK[k];
                }
        } else if (ui.mode == M_PATT) {
            for (k = 0; k < 16u; k++) {
                int pi = (int)ui.bank * NPB + (int)k;
                if (pi == sq.cur)
                    lit_k |= 1u << WK[k];
                else if (pi == sq.queued)
                    dim_k |= 1u << WK[k];
                else if (pat_used(&pats[pi]))
                    mid_k |= 1u << WK[k];
            }
            for (k = 0; k < 11u; k++) {
                if (k == ui.bank)
                    lit_k |= 1u << BK[k];
                else if ((int)k == sq.cur / NPB)
                    dim_k |= 1u << BK[k];
            }
        } else {
            uint32_t n = steps_len();
            for (k = 0; k < n; k++) {
                uint8_t t = ui.mode == M_KICK ? p->ktrig[k] : p->trig[k];
                if (t == 1u)
                    dim_k |= 1u << WK[k];
                else if (t == 2u)
                    mid_k |= 1u << WK[k];
            }
            if (sq.running) {
                uint32_t ps = ui.mode == M_KICK ? sq.kstep : sq.step;
                if (ps < n)
                    lit_k |= 1u << WK[ps];
            }
        }
        if (sq.running)
            lit_b |= LED_PLAY_GREEN;
        plat_lights(lit_b, lit_k, 0, dim_k, 1);
        plat_lights_mid(0, mid_k);
    }
}

void app_input_only(void) { input(); }
/* ----------------------------------------------------------------- draw --- */
/* The screen is four canvases: the header (0..31), the strip of steps (32..41), the picture (42..167) and the footer
 * of four quadrants (170..239). Each is drawn only when something it shows has changed. */
static int16_t ks[25], kc[25];           /* a dial's arc: 270 degrees, from the lower left round the top to the lower right */

static uint32_t hash32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}
static uint16_t shade(uint16_t c, uint32_t k)    /* a colour at k/16 of its strength */
{
    uint32_t r = (c >> 11) & 31u, g = (c >> 5) & 63u, b = c & 31u;
    return (uint16_t)(((r * k / 16u) << 11) | ((g * k / 16u) << 5) | (b * k / 16u));
}
static void centre_text(int32_t x, int32_t w, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text(x + (w - text_w(f, s)) / 2, y, f, s, c);
}
static void line2(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)     /* a neon line: two pixels thick */
{
    cv_line(x0, y0, x1, y1, c);
    cv_line(x0, y0 + 1, x1, y1 + 1, c);
}

static void knob_draw(int32_t cx, int32_t cy, int32_t r, int v, int vmax, uint16_t on, uint16_t off)
{
    int i, lit = v * 24 / vmax;
    for (i = 0; i < 24; i++) {
        uint16_t c = i < lit ? on : off;
        cv_line(cx + kc[i] * r / 256, cy + ks[i] * r / 256, cx + kc[i + 1] * r / 256, cy + ks[i + 1] * r / 256, c);
        cv_line(cx + kc[i] * (r - 1) / 256, cy + ks[i] * (r - 1) / 256, cx + kc[i + 1] * (r - 1) / 256, cy + ks[i + 1] * (r - 1) / 256, c);
    }
    cv_line(cx + kc[lit] * 5 / 256, cy + ks[lit] * 5 / 256, cx + kc[lit] * (r - 3) / 256, cy + ks[lit] * (r - 3) / 256, C_WHITE);
    cv_rect(cx - 1, cy - 1, 3, 3, on);
}

/* the value a knob shows: the held step's lock, else the sound */
static int sval(uint32_t pid)
{
    if (ui.mode == M_SYN && ui.held_step != 0xFFu) {
        int l = lock_find(ui.held_step, pid);
        if (l >= 0)
            return cur_pat()->lk_v[ui.held_step][l];
    }
    return syn[pid];
}
static int kval(const uint8_t *p, int *locked)
{
    *locked = 0;
    if (is_syn_knob(p)) {
        uint32_t pid = (uint32_t)(p - syn);
        if (ui.mode == M_SYN && ui.held_step != 0xFFu && lock_find(ui.held_step, pid) >= 0)
            *locked = 1;
        return sval(pid);
    }
    return *p;
}


/* ---------------------------------------------------------------- header --- */
static void pre_label(int k, char *t)    /* "P03 ZAP": the kind's letter and the place in the list, then the name */
{
    char n[12];
    int i = sd.pre[k];
    t[0] = 0;
    if (i < NFACT[k]) {
        t[0] = PKC[k];
        t[1] = 0;
        if (i + 1 < 10)
            text_cat(t, "0");
        int_text(i + 1, n);
    } else {
        scopy(t, "U", 4);
        if (i - NFACT[k] + 1 < 10)
            text_cat(t, "0");
        int_text(i - NFACT[k] + 1, n);
    }
    text_cat(t, n);
    text_cat(t, " ");
    text_cat(t, pre_name(k, i));
}
static const char *page_title(void)
{
    const row_t *r = row_of(ui.mode, pg());
    switch (ui.mode) {
    case M_SEQ: return SEQ_TITLE[pg()];
    case M_PATCH: return "VIRTUAL PATCHBAY";
    case M_PATT: return PATT_TITLE[pg()];
    case M_SET: return SET_TITLE[pg()];
    case M_HOME: return HOME_TITLE[pg()];
    case M_RAND: return rp_title((int)pg());
    case M_KICK: return kpg() == 3u ? "KICK SEQUENCE 1" : kpg() == 4u ? "KICK SEQUENCE 2" : r->title;
    default: return r ? r->title : "";
    }
}
static uint16_t head_col(void) { return ui.mode == M_SET && pg() == SP_BT ? CL_G : MC[ui.mode]; }   /* BLUETOOTH: green */
static void draw_head(void)
{
    char t[40], n[12];
    int32_t w, x, i;
    uint16_t mc = head_col();
    const char *mn = ui.mode == M_SET ? SET_NAME[pg()] : MODE_NAME[ui.mode];
    cv_begin(240, 32, C_BLACK);
    w = text_w(&FONT_B, mn);
    cv_rect(3, 1, w + 10, 18, mc);
    cv_text(8, 2, &FONT_B, mn, C_BLACK);
    x = 3 + w + 10 + 7;
    if (NPAGE[ui.mode] > 9u) {           /* too many pages for dots: the number */
        int_text((int32_t)pg() + 1, t);
        text_cat(t, "/");
        int_text(NPAGE[ui.mode], n);
        text_cat(t, n);
        cv_text(x, 6, &FONT_T, t, mc);
    } else if (NPAGE[ui.mode] > 1u) {
        for (i = 0; i < (int32_t)NPAGE[ui.mode]; i++, x += 8)
            cv_rect(x, 8, 5, 5, i == (int32_t)pg() ? mc : CL_DG);
    }
    if (sq.rec)                          /* recording: a red dot */
        cv_rect(138, 6, 8, 8, CL_R);
    if (sq.mute_s)                       /* the mutes: the synth's and the kick's letters, in red */
        cv_text(168, 6, &FONT_T, "S", CL_R);
    if (sq.mute_k)
        cv_text(176, 6, &FONT_T, "K", CL_R);
    if (sq.running) {                    /* the run mark */
        for (i = 0; i < 6; i++)
            cv_rect(152 + i * 2, 4 + i, 2, 12 - 2 * i, CL_G);
    } else {
        cv_rect(152, 4, 4, 12, CL_L);
        cv_rect(159, 4, 4, 12, CL_L);
    }
    {                                    /* the tempo: the clock's while one comes in (EXT) */
        uint32_t xb = xs.live ? xs.bpm : 0u;
        const char *bl = xs.live ? "EXT" : "BPM";
        if (xs.live && !xb)
            scopy(n, "---", 12);
        else
            int_text(xb ? (int32_t)xb : sd.bpm, n);
        w = text_w(&FONT_B, n);
        cv_text(236 - w, 2, &FONT_B, n, C_WHITE);
        cv_text(236 - w - 4 - text_w(&FONT_T, bl), 6, &FONT_T, bl, xs.live ? CL_G : CL_L);
    }
    cv_rect(3, 21, 234, 1, CL_PNL);
    if (plat_ms() < ui.msg_until) {
        x = cv_text(4, 20, &FONT_T, ui.msg[0], C_WHITE);
        cv_text(x + 6, 20, &FONT_T, ui.msg[1], mc);
    } else {
        cv_text(4, 20, &FONT_T, page_title(), mc);
        t[0] = 0;
        if (ui.mode == M_KICK) {
            pre_label(PK_KICK, t);
        } else if (ui.mode == M_FX) {
            pre_label(PK_FX, t);
        } else if (ui.mode == M_PATCH) {
            pre_label(PK_PATCH, t);
        } else if (ui.mode == M_SCALE) {
            pre_label(PK_SCALE, t);
        } else if (ui.mode == M_RAND) {
            scopy(t, rp_mode((int)pg()), 40);
        } else if (ui.mode == M_SET) {
            t[0] = 0;
        } else if (ui.mode == M_HOME) {
            if (pg() >= 6u)
                scopy(t, "V" APP_VERSION, 40);
            else
                scopy(t, sd.pname, 40);
        } else {
            if (ui.mode == M_SEQ || ui.mode == M_PATT) {
                pat_name(sq.cur, n);
                text_cat(t, n);
                if (sq.queued >= 0) {
                    text_cat(t, ">");
                    pat_name(sq.queued, n);
                    text_cat(t, n);
                }
                text_cat(t, " ");
            }
            if (ui.mode == M_PATT && pg() >= 2u)
                t[0] = 0;
            else {
                char l[24];
                pre_label(PK_SYN, l);
                text_cat(t, l);
            }
        }
        cv_text(236 - text_w(&FONT_T, t), 20, &FONT_T, t, CL_L);
    }
    cv_blit(0, 0);
}

/* ----------------------------------------------------------------- strip --- */
/* the columns of a sequence, the strip above and the picture under it: len of them, each cw wide and two apart, together in the
 * middle of 4..236. The division leaves some pixels over: the strip and the picture must both start where this says. */
static int32_t col_x0(int32_t len, int32_t *cw)
{
    int32_t w = (232 - 2 * (len - 1)) / len;
    *cw = w;
    return 4 + (232 - (w * len + 2 * (len - 1))) / 2;
}

static void draw_strip(void)
{
    pat_t *p = &pats[sq.cur];
    int kick = ui.mode == M_KICK;
    int32_t len = kick ? p->klen : p->len, cw, x0, i;
    uint32_t ps = kick ? sq.kstep : sq.step;
    x0 = col_x0(len, &cw);
    cv_begin(240, 10, C_BLACK);
    for (i = 0; i < len; i++) {
        int32_t x = x0 + i * (cw + 2);
        uint8_t t = kick ? p->ktrig[i] : p->trig[i];
        uint16_t c = t == 2 ? CL_M : t == 1 ? shade(CL_G, 11) : CL_DG;
        if (sq.running && ps == (uint32_t)i)
            c = C_WHITE;
        cv_rect(x, 1, cw, 5, c);
        if (kick ? ui.ksel == i : (ui.mode == M_SEQ && ui.sel == i))
            cv_rect(x, 7, cw, 2, head_col());
    }
    cv_blit(0, 32);
}

/* ---------------------------------------------------------------- footer --- */
typedef struct {
    char lab[16], val[16];
    int v;                               /* 0..127 for the bar, -1 none */
    uint8_t lk, dim;
} fcell_t;

static void cell_set(fcell_t *c, const char *lab, const char *val, int v, int lk)
{
    scopy(c->lab, lab, 16);
    scopy(c->val, val, 16);
    c->v = v;
    c->lk = (uint8_t)lk;
    c->dim = 0;
}
static void pre_short(int k, int v, char *t)     /* a pattern's preset of a kind: its name, or - */
{
    if (v == 0)
        scopy(t, "-", 16);
    else
        scopy(t, pre_name(k, v - 1), 16);
}

static void foot_cells(fcell_t *c)
{
    char t[16], n[12];
    const row_t *r = row_of(ui.mode, pg());
    int k;
    for (k = 0; k < 4; k++)
        cell_set(&c[k], "", "", -1, 0);
    if (r) {
        for (k = 0; k < 4; k++) {
            int lk, v = kval(r->k[k].p, &lk);
            fmt_val(r->k[k].fmt, v, t);
            cell_set(&c[k], r->k[k].lab, t, v * 127 / r->k[k].mx, lk);
        }
        return;
    }
    if (ui.fast && (ui.mode == M_SEQ || (ui.mode == M_KICK && kpg() >= 3u))) {   /* OCT- held: two empty cells and the nudges */
        signed_text(ui.nudge_all, t);
        cell_set(&c[2], "NUDGE ALL", t, clamp(64 + ui.nudge_all * 8, 0, 127), 0);
        signed_text(ui.nudge, t);
        cell_set(&c[3], ui.mode == M_SEQ ? "NUDGE" : "NUDGE KICK", t, clamp(64 + ui.nudge * 8, 0, 127), 0);
        return;
    }
    if (ui.mode == M_SEQ) {
        pat_t *p = cur_pat();
        uint32_t s = cur_step_for_edit();
        if (pg() == 2) {
            int_text(sd.bpm, t);
            cell_set(&c[0], "BPM", t, (sd.bpm - 40) * 127 / 200, 0);
            cell_set(&c[1], "BPM +/- 10", t, (sd.bpm - 40) * 127 / 200, 0);
        } else if (pg() == 0) {
            signed_text(p->pit[s], t);
            cell_set(&c[0], "PITCH", t, (p->pit[s] + 24) * 127 / 48, 0);
            int_text(p->vel[s], t);
            cell_set(&c[1], "VELOCITY", t, p->vel[s], 0);
            if (p->gate[s] >= GATE_FULL) {
                scopy(t, "FULL", 16);
            } else {
                int_text(p->gate[s] * 100 / 32, t);
                text_cat(t, "%");
            }
            cell_set(&c[2], "GATE", t, p->gate[s] * 127 / GATE_FULL, 0);
            t[0] = 'X';
            int_text(p->rat[s] + 1, t + 1);
            cell_set(&c[3], "RATCHET", t, p->rat[s] * 127 / 3, 0);
        } else {
            cell_set(&c[0], "CONDITION", COND[p->cond[s]], p->cond[s] * 127 / (NCOND - 1), 0);
            cell_set(&c[1], "TRIG", p->trig[s] == 2 ? "ACC" : p->trig[s] ? "TRIG" : "OFF", p->trig[s] * 63, 0);
            int_text(p->len, t);
            cell_set(&c[2], "LENGTH", t, p->len * 127 / NSTEP, 0);
            int_text(p->swing * 100 / 127, t);
            text_cat(t, "%");
            cell_set(&c[3], "SWING", t, p->swing, 0);
        }
    } else if (ui.mode == M_KICK) {                       /* the kick's own sequence */
        pat_t *p = cur_pat();
        uint32_t s = cur_step_for_edit();
        if (kpg() == 3u) {               /* sequence 1: trig, pitch, volume, length */
            cell_set(&c[0], "TRIG", p->ktrig[s] == 2 ? "ACC" : p->ktrig[s] ? "TRIG" : "OFF", p->ktrig[s] * 63, 0);
            signed_text(p->kpit[s], t);
            cell_set(&c[1], "PITCH", t, (p->kpit[s] + 24) * 127 / 48, 0);
            int_text(p->kvol[s] * 100 / 127, t);
            text_cat(t, "%");
            cell_set(&c[2], "VOLUME", t, p->kvol[s], 0);
            int_text(p->klen, t);
            cell_set(&c[3], "LENGTH", t, p->klen * 127 / NSTEP, 0);
        } else {                         /* sequence 2: condition, ratchet, nothing, nudge */
            cell_set(&c[0], "CONDITION", COND[p->kcond[s]], p->kcond[s] * 127 / (NCOND - 1), 0);
            t[0] = 'X';
            int_text(p->krat[s] + 1, t + 1);
            cell_set(&c[1], "RATCHET", t, p->krat[s] * 127 / 3, 0);
        }
    } else if (ui.mode == M_PATCH) {
        uint32_t d = ui.pdst;
        int depth = clamp(((int)sd.amt[d] - 64) * 100 / 63, -100, 100);
        cell_set(&c[0], "SOURCE", SRC_NAME[sd.src[d]], sd.src[d] * 127 / (NSRC - 1), 0);
        cell_set(&c[1], "DEST", DEST_NAME[d], (int)d * 127 / (NPATCH - 1), 0);
        signed_text(depth, t);
        cell_set(&c[2], "DEPTH", t, sd.amt[d], 0);
        cell_set(&c[3], "PATCH", (sd.pon >> d) & 1u ? "ON" : "OFF", (sd.pon >> d) & 1u ? 127 : 0, 0);
    } else if (ui.mode == M_PATT) {
        int pi = edit_pi();
        if (pg() == 0) {
            pat_name(pi, t);
            cell_set(&c[0], "PATTERN", t, pi * 127 / (NPAT - 1), 0);
            cell_set(&c[1], "ACTION", CMD_NAME[ui.cmd], ui.cmd * 127 / 4, 0);
            n[0] = (char)('A' + ui.bank);
            n[1] = 0;
            cell_set(&c[2], "BANK", n, ui.bank * 127 / (NBANK - 1), 0);
            cell_set(&c[3], "SONG", sd.song_on ? "ON" : "OFF", sd.song_on ? 127 : 0, 0);
        } else if (pg() == 1) {
            int_text(ui.slot + 1, t);
            cell_set(&c[0], "SLOT", t, ui.slot * 127 / 31, 0);
            if (sd.song[ui.slot] < NPAT)
                pat_name(sd.song[ui.slot], t);
            else
                scopy(t, "---", 16);
            cell_set(&c[1], "PATTERN", t, sd.song[ui.slot] < NPAT ? sd.song[ui.slot] * 127 / (NPAT - 1) : 0, 0);
            cell_set(&c[2], "CLEAR", "TURN", -1, 0);
            cell_set(&c[3], "SONG", sd.song_on ? "ON" : "OFF", sd.song_on ? 127 : 0, 0);
        } else if (pg() == 2) {
            for (k = 0; k < 4; k++) {
                pre_short(k, sd.pre[k] + 1, t);
                cell_set(&c[k], KIND_NAME[k], t, sd.pre[k] * 127 / pre_count(k), 0);
            }
        } else if (pg() == 3) {
            pre_short(PK_SCALE, sd.pre[PK_SCALE] + 1, t);
            cell_set(&c[0], "SCALE", t, sd.pre[PK_SCALE] * 127 / pre_count(PK_SCALE), 0);
            cell_set(&c[1], "TO BANK", "TURN >", -1, 0);
            cell_set(&c[2], "TO ALL", "TURN >", -1, 0);
            cell_set(&c[3], "RESET", "TURN >", -1, 0);
        }
    } else if (ui.mode == M_SET) {
        static const char *const PP[5] = {"OFF", "1 PPQN", "2 PPQN", "4 PPQN", "24 PPQN"};
        if (pg() == SP_MIDI) {
            cell_set(&c[0], "CLOCK IN", xs.on ? "ON" : "OFF", xs.on ? 127 : 0, 0);
            cell_set(&c[1], "CLOCK OUT", MS_CKOUT ? "ON" : "OFF", MS_CKOUT ? 127 : 0, 0);
            cell_set(&c[2], "PULSE OUT", PP[MS_PULSE], MS_PULSE * 127 / 4, 0);
            cell_set(&c[3], "-", "", -1, 0);
            c[3].dim = 1;
        } else if (pg() == SP_MIDI2) {
            if (MS_INCH)
                int_text(MS_INCH, t);
            else
                scopy(t, "ALL", 16);
            cell_set(&c[0], "IN CH", t, MS_INCH * 127 / 16, 0);
            if (MS_OUTCH)
                int_text(MS_OUTCH, t);
            else
                scopy(t, "OFF", 16);
            cell_set(&c[1], "OUT CH", t, MS_OUTCH * 127 / 16, 0);
            if (MS_KICKCH)
                int_text(MS_KICKCH, t);
            else
                scopy(t, "OFF", 16);
            cell_set(&c[2], "KICK CH", t, MS_KICKCH * 127 / 16, 0);
            cell_set(&c[3], "-", "", -1, 0);
            c[3].dim = 1;
        } else if (pg() == SP_VER) {
            for (k = 0; k < 4; k++) {
                cell_set(&c[k], "-", "", -1, 0);
                c[k].dim = 1;
            }
        } else if (pg() == SP_AUDIO) {
            int uo = !(bts.quality & BQ_USB_OFF), lo = !(bts.quality & BQ_LINE_OFF), fu = (bts.quality & BQ_USB_FULL) != 0;
            cell_set(&c[0], "USB AUDIO", uo ? "ON" : "OFF", uo ? 127 : 0, 0);
            cell_set(&c[1], "SPK+JACK", lo ? "ON" : "OFF", lo ? 127 : 0, 0);
            cell_set(&c[2], "USB LEVEL", fu ? "FULL" : "MASTER", fu ? 127 : 0, 0);
            cell_set(&c[3], "-", "", -1, 0);
            c[3].dim = 1;
        } else {                           /* Bluetooth */
            bt_list();
            cell_set(&c[0], "BLUETOOTH", bts.on ? "ON" : "OFF", bts.on ? 127 : 0, 0);
            if (bt.n) {
                int_text(ui.bt_sel + 1, t);
                text_cat(t, "/");
                int_text(bt.n, n);
                text_cat(t, n);
            } else {
                scopy(t, "NONE", 16);
            }
            cell_set(&c[1], "DEVICE", t, bt.n > 1 ? ui.bt_sel * 127 / (bt.n - 1) : 0, 0);
            cell_set(&c[2], "ACTION", BT_ACT[ui.bt_act], ui.bt_act * 127 / (NBA - 1), 0);
            cell_set(&c[3], "DO", "TURN >", -1, 0);
        }
    } else if (ui.mode == M_RAND) {
        int rp = (int)pg();
        for (k = 0; k < 4; k++) {
            int on = (sd.rnd_en[rp] >> k) & 1;
            if (!on) {
                scopy(t, "OFF", 16);
            } else {
                int_text(sd.rnd_amt[rp][k] * 100 / 127, t);
                text_cat(t, "%");
            }
            cell_set(&c[k], rp_lab(rp, k), t, sd.rnd_amt[rp][k], 0);
            c[k].dim = (uint8_t)!on;
        }
    } else if (ui.mode == M_HOME && pg() < 6u) {
        int t_ = (int)pg(), i = ui.hidx[t_];
        if (t_ == 0) {
            int_text(i + 1, t);
        } else if (i < NFACT[t_ - 1]) {
            t[0] = PKC[t_ - 1];
            t[1] = 0;
            int_text(i + 1, n);
            text_cat(t, n);
        } else {
            scopy(t, "U", 4);
            int_text(i - NFACT[t_ - 1] + 1, n);
            text_cat(t, n);
        }
        cell_set(&c[0], t_ == 0 ? "PROJECT" : "PRESET", t, i * 127 / (lib_n(t_) - 1), 0);
        cell_set(&c[1], "ACTION", ACT_NAME[ui.hact], ui.hact * 127 / 3, 0);
        if (ui.hact == 2) {
            int_text(ui.hcur + 1, t);
            cell_set(&c[2], "CURSOR", t, ui.hcur * 127 / (PN - 2), 0);
            t[0] = lib_ren(t_, i) && lib_ren(t_, i)[ui.hcur] ? lib_ren(t_, i)[ui.hcur] : '_';
            t[1] = 0;
            cell_set(&c[3], "CHAR", t, -1, 0);
        } else {
            cell_set(&c[2], "", "", -1, 0);
            cell_set(&c[3], ACT_NAME[ui.hact], "TURN >", -1, 0);
        }
    }
}

static void draw_foot(void)
{
    fcell_t c[4];
    int q;
    foot_cells(c);
    cv_begin(240, 70, C_BLACK);
    for (q = 0; q < 4; q++) {
        int32_t x0 = q * 60;
        uint16_t col = (ui.mode == M_HOME && pg() >= 6u) ? CL_PNL : c[q].dim ? CL_PNL : QC[q];
        char lab[16];
        const felucca_font_t *f = &FONT_M;
        int32_t y = 17, n;
        scopy(lab, c[q].lab, 16);
        for (n = 0; lab[n]; n++)
            ;
        while (n > 0 && text_w(&FONT_T, lab) > 56)
            lab[--n] = 0;
        cv_rect(x0 + 2, 0, 56, 2, col);
        if (q)
            cv_rect(x0, 8, 1, 56, CL_PNL);
        centre_text(x0, 60, 4, &FONT_T, lab, col);
        if (text_w(f, c[q].val) > 56) {
            f = &FONT_B;
            y = 21;
        }
        if (text_w(f, c[q].val) > 56) {
            f = &FONT_S;
            y = 23;
        }
        if (text_w(f, c[q].val) > 56) {
            f = &FONT_XS;
            y = 24;
        }
        centre_text(x0, 60, y, f, c[q].val, c[q].lk ? CL_Y : c[q].dim ? CL_L : C_WHITE);
        if (c[q].v >= 0) {
            int32_t fw = 52 * clamp(c[q].v, 0, 127) / 127;
            cv_rect(x0 + 4, 51, 52, 5, CL_DG);
            cv_rect(x0 + 4, 51, fw, 5, c[q].lk ? CL_Y : col);
        }
        if (c[q].lk)
            centre_text(x0, 60, 57, &FONT_T, "LOCK", CL_Y);
    }
    cv_blit(0, 170);
}
/* SEQ: a level meter, a column a step. Each has two blocks: the pitch (above) and the intensity (below), then the gate
 * and the ratchet under it. */
static void vis_tempo(void)              /* the tempo: big, with the four beats and the speed of a step */
{
    char t[16], n[12];
    int32_t i, w, ms = 60000 / sd.bpm, beat = sq.running ? (int32_t)(sq.step & 15u) / 4 : -1;
    int_text(sd.bpm, t);
    w = text_w(&FONT_L, t);
    cv_text(120 - w / 2, 10, &FONT_L, t, C_WHITE);
    cv_text(120 + w / 2 + 6, 24, &FONT_B, "BPM", CL_L);
    for (i = 0; i < 4; i++) {
        uint16_t col = i == 0 ? CL_O : CL_G;
        cv_rect(30 + i * 46, 52, 40, 16, i == beat ? col : CL_DG);
        if (i != beat)
            cv_rect(31 + i * 46, 53, 38, 14, C_BLACK);
        centre_text(30 + i * 46, 40, 56, &FONT_T, i == 0 ? "1" : i == 1 ? "2" : i == 2 ? "3" : "4", i == beat ? C_BLACK : CL_L);
    }
    cv_rect(8, 86, 224, 1, CL_PNL);                      /* 40 .. 240 */
    cv_rect(8 + (sd.bpm - 40) * 224 / 200 - 2, 80, 5, 13, CL_Y);
    cv_text(8, 96, &FONT_T, "40", CL_L);
    cv_text(232 - text_w(&FONT_T, "240"), 96, &FONT_T, "240", CL_L);
    scopy(t, "BEAT ", 16);
    int_text(ms, n);
    text_cat(t, n);
    text_cat(t, " MS");
    centre_text(0, 240, 106, &FONT_XS, t, CL_L);
}
static void vis_seq(void)
{
    pat_t *p;
    if (pg() == 2) {
        vis_tempo();
        return;
    }
    p = cur_pat();
    int32_t len = p->len, cw, x0, i, s;
    x0 = col_x0(len, &cw);
    for (i = 0; i < len; i++) {
        int32_t x = x0 + i * (cw + 2);
        int on = p->trig[i] && p->vel[i];
        int play = sq.running && sq.step == i, sel = ui.sel == i;
        int np = on ? 1 + (p->pit[i] + 24) * 13 / 48 : 0, nv = on ? 1 + p->vel[i] * 13 / 127 : 0;
        uint16_t off = on ? RGB(34, 40, 62) : RGB(20, 24, 38);
        if (play)
            cv_rect(x - 1, 0, cw + 2, 124, RGB(18, 50, 40));
        else if (sel)
            cv_rect(x - 1, 0, cw + 2, 124, RGB(26, 30, 58));
        if (p->trig[i] == 2)
            cv_rect(x, 0, cw, 2, CL_M);
        for (s = 0; s < 14; s++) {
            uint16_t cp = s < 9 ? CL_G : s < 12 ? CL_Y : CL_O;
            uint16_t cv = s < 9 ? CL_C : s < 12 ? CL_B : CL_M;
            cv_rect(x, 42 - 3 * s, cw, 2, s < np ? (play && s == np - 1 ? C_WHITE : cp) : off);
            cv_rect(x, 88 - 3 * s, cw, 2, s < nv ? (play && s == nv - 1 ? C_WHITE : cv) : off);
        }
        cv_rect(x, 92, cw, 2, play ? C_WHITE : CL_PNL);
        /* gate, and ratchet, under the column */
        cv_rect(x, 96, cw, 3, CL_DG);
        cv_rect(x, 96, cw * p->gate[i] / GATE_FULL, 3, CL_M);
        if (cw >= 24) {
            char t[12];
            if (p->gate[i] >= GATE_FULL) {
                scopy(t, "FULL", 12);
            } else {
                int_text(p->gate[i] * 100 / 32, t);
            }
            centre_text(x, cw, 100, &FONT_T, t, p->gate[i] >= GATE_FULL ? CL_L : CL_M);
            t[0] = 'X';
            int_text(p->rat[i] + 1, t + 1);
            centre_text(x, cw, 112, &FONT_T, t, p->rat[i] ? CL_O : CL_L);
        } else {
            for (s = 0; s <= p->rat[i]; s++)
                cv_rect(x + s * 3, 104, 2, 2, CL_O);
        }
        if (sel)
            cv_rect(x, 124, cw, 2, C_WHITE);
    }
}

/* the filter: its response from 20 Hz to 20 kHz, the cutoff, the peak of the resonance and the noise */
static float filt_db(uint32_t mode, float w, float q)
{
    float w2 = w * w, d = (1.0f - w2) * (1.0f - w2) + w2 / (q * q), m;
    if (d < 1e-6f)
        d = 1e-6f;
    switch (mode) {
    case 0: m = fm_rsqrtf(d) * fm_rsqrtf(1.0f + w2 * w2); break;
    case 1: m = fm_rsqrtf(d); break;
    case 2: m = (w / q) * fm_rsqrtf(d); break;
    default: m = w2 * fm_rsqrtf(d); break;
    }
    if (m < 1e-4f)
        m = 1e-4f;
    return 6.0206f * fm_log2f(m);
}
static void vis_filter(void)
{
    int32_t x, py = 84, peak_x = 0, peak_y = 84, xc, i;
    float cutoct = (float)sval(P_CUT) * (10.0f / 127.0f), q = 0.55f + (float)sval(P_RES) * (7.0f / 127.0f);
    uint32_t mode = (uint32_t)sval(P_MODE) >> 5;
    for (i = 0; i < 5; i++)
        cv_rect(4, 4 + i * 20, 232, 1, CL_DG);
    for (x = 0; x < 232; x++) {
        float octs = 10.0f * (float)x / 232.0f, db = filt_db(mode, fm_exp2f(octs - cutoct), q);
        int32_t y = clamp(4 + (int32_t)((18.0f - db) * (80.0f / 60.0f)), 4, 84);
        cv_rect(4 + x, y, 1, 85 - y, RGB(10, 44, 30));
        if (x)
            line2(3 + x, py, 4 + x, y, CL_G);
        if (y < peak_y) {
            peak_y = y;
            peak_x = x;
        }
        py = y;
    }
    xc = sval(P_CUT) * 232 / 127;
    for (i = 4; i < 84; i += 5)
        cv_rect(4 + xc, i, 1, 3, CL_Y);
    if (sval(P_RES) > 20)
        cv_rect(2 + peak_x, peak_y - 3, 5, 5, CL_C);
    cv_text(204, 4, &FONT_B, MODE4[(uint32_t)sval(P_MODE) >> 5], CL_O);
    cv_rect(4 + 54, 84, 1, 3, CL_PNL);
    cv_rect(4 + 131, 84, 1, 3, CL_PNL);
    cv_rect(4 + 208, 84, 1, 3, CL_PNL);
    cv_text(4 + 54 - 6, 87, &FONT_T, "100", CL_L);
    cv_text(4 + 131 - 4, 87, &FONT_T, "1K", CL_L);
    cv_text(4 + 208 - 8, 87, &FONT_T, "10K", CL_L);
    cv_text(4, 103, &FONT_T, "NOISE", CL_M);
    for (x = 0; x < 232; x += 3) {           /* the noise: more of it, taller specks */
        int32_t h = (int32_t)(hash32((uint32_t)x * 7u + 1u) % (uint32_t)(1 + sval(P_NSE) * 20 / 127));
        cv_rect(4 + x, 123 - h, 2, 2, CL_M);
    }
}


/* the waveforms of the two oscillators, one over the other: any of the 16 types */
static void plot_wave(uint32_t w, float cycles, int32_t cy, int32_t amp, uint16_t col)
{
    int32_t x, py = cy;
    for (x = 0; x < 232; x++) {
        float ph = cycles * (float)x / 232.0f;
        int32_t y;
        ph -= fm_floorf(ph);
        y = cy - (int32_t)(fm_clampf(dsp_wave(w, ph), -1.2f, 1.2f) * (float)amp);
        if (x)
            line2(3 + x, py, 4 + x, y, col);
        py = y;
    }
}
static void vis_osc(void)
{
    char t[24], n[12];
    int32_t i;
    float d = ((float)sval(P_V2FRQ) - (float)sval(P_V1FRQ)) * 0.5f, c2;
    c2 = fm_clampf(2.0f * fm_exp2f(d * (1.0f / 12.0f)), 0.5f, 8.0f);
    for (i = 0; i < 5; i++)
        cv_rect(4, 4 + i * 20, 232, 1, i == 2 ? CL_PNL : CL_DG);
    for (i = 0; i < 9; i++)
        cv_rect(4 + i * 29, 4, 1, 80, CL_DG);
    plot_wave((uint32_t)sval(P_V2WAV) >> 3, c2, 44, 8 + 24 * sval(P_V2LVL) / 127, CL_C);
    plot_wave((uint32_t)sval(P_V1WAV) >> 3, 2.0f, 44, 8 + 24 * sval(P_V1LVL) / 127, CL_G);
    scopy(t, "VCO1 ", 24);
    text_cat(t, WAV16[(sval(P_V1WAV) >> 3) & 15]);
    text_cat(t, " ");
    fmt_val(F_SEMI, sval(P_V1FRQ), n);
    text_cat(t, n);
    cv_text(6, 90, &FONT_XS, t, CL_G);
    scopy(t, "VCO2 ", 24);
    text_cat(t, WAV16[(sval(P_V2WAV) >> 3) & 15]);
    text_cat(t, " ");
    fmt_val(F_SEMI, sval(P_V2FRQ), n);
    text_cat(t, n);
    cv_text(122, 90, &FONT_XS, t, CL_C);
    scopy(t, "SYNC ", 24);
    text_cat(t, sval(P_SYN) >= 64 ? "ON" : "OFF");
    cv_text(6, 107, &FONT_XS, t, sval(P_SYN) >= 64 ? CL_Y : CL_L);
    scopy(t, "FM 1>2 ", 24);
    int_text(sval(P_FM), n);
    text_cat(t, n);
    cv_text(122, 107, &FONT_XS, t, CL_O);
}

/* KICK SEQUENCE: a column a step, as tall as the hit; the condition and the ratchet under it */
static void vis_kseq(void)
{
    pat_t *p = cur_pat();
    int32_t len = p->klen, cw, x0, i, s;
    int one = kpg() == 3u;               /* sequence 1: the bars are the volumes and a lane below has the pitches; sequence 2: conditions and ratchets */
    char t[16];
    x0 = col_x0(len, &cw);
    if (one)
        cv_rect(4, 84, 232, 1, CL_DG);   /* the pitch lane: no shift on this line */
    for (i = 0; i < len; i++) {
        int32_t x = x0 + i * (cw + 2), h = p->ktrig[i] == 2 ? 62 : p->ktrig[i] ? 44 : 0;
        int play = sq.running && sq.kstep == i, sel = ui.ksel == i;
        uint16_t col = p->ktrig[i] == 2 ? CL_Y : CL_O;
        if (one && h)
            h = 8 + p->kvol[i] * 54 / 127;
        if (play)
            cv_rect(x - 1, 0, cw + 2, 124, RGB(18, 50, 40));
        else if (sel)
            cv_rect(x - 1, 0, cw + 2, 124, RGB(26, 30, 58));
        cv_rect(x, 66, cw, 2, play ? C_WHITE : CL_PNL);
        if (h) {
            cv_rect(x, 66 - h, cw, h, play ? C_WHITE : col);
            for (s = 66 - h + 4; s < 64; s += 6)
                cv_rect(x, s, cw, 1, RGB(20, 24, 38));
        } else {
            cv_rect(x, 62, cw, 3, CL_DG);
        }
        if (one) {
            int32_t y = 84 - p->kpit[i] * 12 / 24;
            cv_rect(x, y - 1, cw, 3, play ? C_WHITE : p->ktrig[i] ? CL_C : CL_DG);
        } else {
            if (p->kcond[i]) {
                if (cw >= 24)
                    centre_text(x, cw, 71, &FONT_T, COND[p->kcond[i]], CL_C);
                else
                    cv_rect(x, 72, cw, 3, CL_C);
            } else if (cw >= 24) {
                centre_text(x, cw, 71, &FONT_T, COND[0], CL_DG);
            }
            if (p->krat[i]) {
                if (cw >= 24) {
                    t[0] = 'X';
                    int_text(p->krat[i] + 1, t + 1);
                    centre_text(x, cw, 85, &FONT_T, t, CL_M);
                } else {
                    for (s = 0; s <= p->krat[i]; s++)
                        cv_rect(x + s * 3, 86, 2, 3, CL_M);
                }
            }
        }
        if (sel)
            cv_rect(x, 124, cw, 2, C_WHITE);
    }
    scopy(t, "LEN ", 16);
    {
        char n[12];
        int_text(len, n);
        text_cat(t, n);
    }
    cv_text(6, 100, &FONT_XS, t, CL_O);
    {
        char n[12];
        scopy(t, "STEP ", 16);
        int_text(ui.ksel + 1, n);
        text_cat(t, n);
        cv_text(236 - text_w(&FONT_XS, t), 100, &FONT_XS, t, CL_W);
    }
    cv_text(6, 112, &FONT_T, one ? "TAP: OFF/TRIG/ACC  HOLD+K: STEP" : "K4: MOVE ALL THE STEPS", CL_L);
}

/* FX: the chips (they are the keys), then a picture of the effect of the page */
static const uint16_t FXC[6] = {CL_G, CL_C, CL_M, CL_O, CL_B, CL_Y};      /* in FX_NAME order */
static float sat_ui(float x)
{
    x = fm_clampf(x, -1.5f, 1.5f);
    return x - x * x * x * (4.0f / 27.0f);
}
static void vis_fx(void)
{
    const uint8_t *B = pg() == 6u ? FXB_MIX : FXB_PAGE, *I = pg() == 6u ? FXI_MIX : FXI_PAGE;
    int32_t i, x;
    for (i = 0; i < 6; i++) {
        int on = (sd.fxen & B[i]) != 0;
        int32_t cx = 4 + i * 39;
        uint16_t col = FXC[I[i]];
        if (on) {
            cv_rect(cx, 2, 36, 17, col);
            centre_text(cx, 36, 5, &FONT_T, FX_NAME[I[i]], C_BLACK);
        } else {
            cv_rect(cx, 2, 36, 17, CL_DG);
            cv_rect(cx + 1, 3, 34, 15, C_BLACK);
            centre_text(cx, 36, 5, &FONT_T, FX_NAME[I[i]], CL_L);
        }
        if (pg() < 6u && (int32_t)pg() == i)
            cv_rect(cx, 21, 36, 2, C_WHITE);
    }
    if (pg() == 0) {                     /* the compressor: input against output, with the makeup gain */
        int on = sd.fxen & FX_CMP;
        float th = 1.0f - (float)sd.fx[F_CMA] * (0.0077f), mix = (float)sd.fx[F_CMM] * (1.0f / 127.0f);
        float mk = 1.0f + (float)sd.fx[F_CMG] * (1.2f / 127.0f);
        int32_t py = 118;
        uint16_t col = on ? CL_G : shade(CL_G, 5);
        cv_rect(8, 28, 1, 91, CL_PNL);
        cv_rect(8, 118, 91, 1, CL_PNL);
        cv_line(8, 118, 98, 28, CL_DG);
        for (x = 0; x <= 90; x++) {
            float in = (float)x / 90.0f, o = in < th ? in : th + (in - th) * 0.25f, y1;
            int32_t y;
            y1 = in * (1.0f - mix) + o * mk * mix;
            y = 118 - (int32_t)(fm_clampf(y1, 0.0f, 1.05f) * 90.0f);
            if (x)
                line2(8 + x - 1, py, 8 + x, y, col);
            py = y;
        }
        {
            char t[24], n[12];
            int32_t rel = 1 + (int32_t)(1000.0f / (1.0f + 20.0f * (float)sd.fx[F_CMS] * (1.0f / 127.0f) * 5.0f));
            scopy(t, "THRESH ", 24);
            int_text(sd.fx[F_CMA], n);
            text_cat(t, n);
            cv_text(116, 40, &FONT_XS, t, CL_G);
            scopy(t, "SPEED ", 24);
            int_text(sd.fx[F_CMS], n);
            text_cat(t, n);
            cv_text(116, 58, &FONT_XS, t, CL_C);
            scopy(t, "MAKEUP X", 24);
            int_text((int32_t)(mk * 10.0f) / 10, n);
            text_cat(t, n);
            text_cat(t, ".");
            int_text((int32_t)(mk * 10.0f) % 10, n);
            text_cat(t, n);
            cv_text(116, 76, &FONT_XS, t, CL_Y);
            scopy(t, "MIX ", 24);
            int_text(sd.fx[F_CMM], n);
            text_cat(t, n);
            cv_text(116, 94, &FONT_XS, t, CL_M);
            (void)rel;
        }
    } else if (pg() == 1) {              /* the delay: the echoes, left then right */
        int on = sd.fxen & FX_DLY;
        int32_t sp = ((int32_t)((sd.fx[F_DLT] * 8) >> 7) + 1) * 14;
        float fb = (float)sd.fx[F_DLF] * (0.95f / 127.0f), amp = 1.0f;
        int32_t k;
        cv_rect(8, 62, 224, 1, CL_PNL);
        cv_rect(8, 112, 224, 1, CL_PNL);
        cv_text(0, 30, &FONT_T, "L", CL_C);
        cv_text(0, 80, &FONT_T, "R", CL_M);
        cv_rect(10, 28, 3, 34, on ? C_WHITE : CL_DG);
        cv_rect(10, 78, 3, 34, on ? C_WHITE : CL_DG);
        for (k = 1; 12 + k * sp < 232 && amp > 0.04f; k++) {
            int32_t h;
            amp = (k == 1 ? 0.3f + (float)sd.fx[F_DLM] * (0.7f / 127.0f) : amp * fb);
            if (amp > 1.0f)
                amp = 1.0f;
            h = (int32_t)(34.0f * amp);
            if (k & 1)
                cv_rect(10 + k * sp, 62 - h, 3, h, on ? shade(CL_C, 6 + (int32_t)sd.fx[F_DLK] * 10 / 127) : CL_DG);
            else
                cv_rect(10 + k * sp, 112 - h, 3, h, on ? shade(CL_M, 6 + (int32_t)sd.fx[F_DLK] * 10 / 127) : CL_DG);
        }
    } else if (pg() == 2) {              /* the reverb: the tail, and how wide it is */
        int on = sd.fxen & FX_REV;
        float size = 10.0f + (float)sd.fx[F_RVS] * 1.2f;
        for (x = 0; x < 150; x += 2) {
            float e = fm_expf(-(float)x / size) * (float)sd.fx[F_RVM] * (1.0f / 127.0f) * 1.6f;
            int32_t h;
            if (e > 1.0f)
                e = 1.0f;
            h = (int32_t)(e * (float)(10 + (int32_t)(hash32((uint32_t)x + 9u) % 30u)));
            cv_rect(10 + x, 73 - h, 2, 2 * h + 1, on ? shade(CL_M, 6 + (int32_t)sd.fx[F_RVT] * 10 / 127) : CL_DG);
        }
        cv_rect(10, 73, 150, 1, CL_PNL);
        cv_text(172, 28, &FONT_T, "WIDTH", on ? CL_M : CL_L);
        cv_rect(172, 46, 58, 12, CL_DG);
        cv_rect(173, 47, 56, 10, C_BLACK);
        cv_rect(173, 47, 56 * (int32_t)sd.fx[F_RVW] / 127, 10, on ? CL_M : CL_L);
        cv_text(172, 62, &FONT_T, "MONO", CL_L);
        cv_text(230 - text_w(&FONT_T, "WIDE"), 62, &FONT_T, "WIDE", CL_L);
    } else if (pg() == 3) {              /* the stutter: slices of the last beat, repeated, each quieter, at its speed */
        int on = sd.fxen & FX_STU;
        static const float SM[8] = {2.0f, 1.5f, 1.0f, 0.75f, 0.5f, 0.375f, 0.25f, 0.125f};
        float sl = SM[(sd.fx[F_STS] * 8) >> 7], spd = fm_exp2f(((float)sd.fx[F_STP] - 64.0f) * (1.0f / 64.0f));
        float dec = 1.0f - (float)sd.fx[F_STD] * (0.5f / 127.0f), g = 1.0f;
        int32_t bw = (int32_t)(sl * 46.0f / spd), xx = 10;
        uint16_t sc = on ? CL_O : shade(CL_O, 8);
        if (bw < 3)
            bw = 3;
        for (i = 0; xx + bw <= 232 && i < 40; i++) {
            int32_t h = (int32_t)(g * (float)(10 + (int32_t)sd.fx[F_STM] * 70 / 127));
            cv_rect(xx, 118 - h, bw - 2, h, i & 1 ? shade(sc, 10) : sc);
            xx += bw;
            g *= dec;
        }
        cv_text(10, 28, &FONT_T, on ? "STUTTER ON" : "TAP LATCH  HOLD PLAY", on ? CL_O : CL_L);
    } else if (pg() == 4) {              /* the DJ filter: a high-pass, 12 or 24 dB */
        int on = sd.fxen & FX_DJF;
        float cutoct = (float)sd.fx[F_DJC] * (8.6f / 127.0f), q = 0.55f + (float)sd.fx[F_DJR] * (7.0f / 127.0f);
        int32_t py = 118, xc = (int32_t)(cutoct * 232.0f / 10.0f);
        float sl = sd.fx[F_DJS] >= 64 ? 2.0f : 1.0f;
        for (i = 28; i < 118; i += 4)
            cv_rect(4, i, 232, 1, CL_DG);
        for (x = 0; x < 232; x++) {
            float octs = 10.0f * (float)x / 232.0f, db = filt_db(3, fm_exp2f(octs - cutoct), q) * sl;
            int32_t y = clamp(28 + (int32_t)((12.0f - db) * (90.0f / 60.0f)), 28, 118);
            if (on)
                cv_rect(4 + x, y, 1, 119 - y, RGB(6, 24, 52));
            if (x)
                line2(3 + x, py, 4 + x, y, on ? CL_B : CL_L);
            py = y;
        }
        for (i = 28; i < 118; i += 5)
            cv_rect(4 + xc, i, 1, 3, CL_Y);
        cv_text(4 + 54 - 6, 112, &FONT_T, "100", CL_L);
        cv_text(4 + 131 - 4, 112, &FONT_T, "1K", CL_L);
        cv_text(4 + 208 - 8, 112, &FONT_T, "10K", CL_L);
    } else if (pg() == 5) {              /* the limiter: input against output */
        int on = sd.fxen & FX_LIM;
        float lim = 0.3f + (float)sd.fx[F_LIM] * (0.7f / 127.0f), drv = 1.3f * (1.0f + (float)sd.fx[F_LMD] * (2.0f / 127.0f));
        float soft = (float)sd.fx[F_LMS] * (1.0f / 127.0f), out = (0.4f + 0.7f * (float)sd.fx[F_LMO] * (1.0f / 127.0f)) * (1.0f / 0.95f);
        int32_t py = 118;
        cv_rect(8, 28, 1, 91, CL_PNL);
        cv_rect(8, 118, 150, 1, CL_PNL);
        cv_line(8, 118, 98, 28, CL_DG);
        for (x = 0; x <= 150; x++) {
            float in = (float)x / 90.0f * drv, h = fm_clampf(in, -lim, lim), sf = lim * 0.95f * sat_ui(in / lim), o;
            int32_t y;
            o = on ? (h * (1.0f - soft) + sf * soft) * out : in * 0.95f * out;
            y = 118 - (int32_t)(fm_clampf(o, 0.0f, 1.05f) * 90.0f);
            if (x)
                line2(8 + x - 1, py, 8 + x, y, on ? CL_Y : CL_L);
            py = y;
        }
        {
            char t[24], n[12];
            scopy(t, "CEIL ", 24);
            int_text(sd.fx[F_LIM], n);
            text_cat(t, n);
            cv_text(172, 40, &FONT_XS, t, CL_Y);
            scopy(t, "DRIVE ", 24);
            int_text(sd.fx[F_LMD], n);
            text_cat(t, n);
            cv_text(172, 58, &FONT_XS, t, CL_C);
            scopy(t, "OUT ", 24);
            int_text(sd.fx[F_LMO], n);
            text_cat(t, n);
            cv_text(172, 76, &FONT_XS, t, CL_M);
            cv_text(172, 94, &FONT_XS, soft > 0.5f ? "SOFT" : "HARD", CL_O);
        }
    } else {                             /* the mixer: the mix of the four effects together */
        static const uint8_t MI[4] = {F_DLM, F_RVM, F_DJC, F_STS};     /* the two mixes, the cutoff, the slice */
        for (i = 0; i < 4; i++) {
            int on = (sd.fxen & B[i]) != 0;
            int32_t cx = 12 + i * 58, h = sd.fx[MI[i]] * 70 / 127;
            char t[12];
            uint16_t col = FXC[I[i]];
            cv_rect(cx, 30, 34, 72, CL_DG);
            cv_rect(cx + 1, 31, 32, 70, C_BLACK);
            cv_rect(cx + 2, 101 - h, 30, h, on ? col : shade(col, 4));
            if (i == 3) {
                scopy(t, SLICE8[(sd.fx[MI[i]] * 8) >> 7], 12);
            } else {
                int_text(sd.fx[MI[i]] * 100 / 127, t);
                text_cat(t, "%");
            }
            centre_text(cx - 10, 54, 104, &FONT_T, t, on ? C_WHITE : CL_L);
        }
        cv_text(6, 114, &FONT_T, "LAST BLACK KEY: DLY REV DJF STU OFF / ON", CL_L);
    }
}
/* the envelopes: three exponential decays on one time axis */
static void tau_text(float tau, char *t)
{
    char n[12];
    t[0] = 0;
    if (tau < 1.0f) {
        int_text((int32_t)(tau * 1000.0f), n);
        text_cat(t, n);
        text_cat(t, "MS");
    } else {
        int32_t d = (int32_t)(tau * 10.0f);
        int_text(d / 10, n);
        text_cat(t, n);
        text_cat(t, ".");
        int_text(d % 10, n);
        text_cat(t, n);
        text_cat(t, "S");
    }
}
static float dec_tau(int d127, float octs) { return 0.004f * fm_exp2f(octs * (float)d127 * (1.0f / 127.0f)); }
static void plot_env(float tau, uint16_t col, int fill)
{
    int32_t x, py = 8;
    for (x = 0; x < 232; x++) {
        float t = 1.2f * (float)x / 232.0f;
        int32_t y = 90 - (int32_t)(82.0f * fm_expf(-t / tau));
        if (fill)
            cv_rect(4 + x, y, 1, 90 - y, shade(col, 3));
        if (x)
            line2(3 + x, py, 4 + x, y, col);
        py = y;
    }
}
static void vis_env(void)
{
    char t[24], n[12];
    float tv = dec_tau(sval(P_VDEC), 9.0f), tf = dec_tau(sval(P_FDEC), 9.0f), ta = dec_tau(sval(P_ADEC), 9.5f);
    int32_t i;
    for (i = 0; i < 7; i++)
        cv_rect(4 + i * 232 / 6, 4, 1, 86, CL_DG);
    cv_rect(4, 90, 232, 1, CL_PNL);
    plot_env(ta, CL_M, 1);
    plot_env(tf, CL_C, 0);
    plot_env(tv, CL_G, 0);
    cv_text(4 + 77 - 6, 92, &FONT_T, "0.4S", CL_L);
    cv_text(4 + 155 - 6, 92, &FONT_T, "0.8S", CL_L);
    cv_text(4 + 225 - 6, 92, &FONT_T, "1.2S", CL_L);
    for (i = 0; i < 4; i++) {
        static const char *const NM[4] = {"VCO ", "VCF ", "VCA ", "FM "};
        float tt = i == 0 ? tv : i == 1 ? tf : ta;
        int32_t x = 4 + i * 58;
        cv_rect(x, 109, 5, 5, QC[i]);
        scopy(t, NM[i], 24);
        if (i < 3) {
            tau_text(tt, n);
        } else {
            int_text(sval(P_FM), n);
        }
        text_cat(t, n);
        cv_text(x + 8, 106, &FONT_T, t, QC[i]);
    }
}

/* the four dials: a knob each, in its quadrant's colour */
static void vis_dials(const row_t *r)
{
    int q;
    for (q = 0; q < 4; q++) {
        int lk, v = kval(r->k[q].p, &lk);
        char t[16];
        int32_t cx = 30 + 60 * q;
        knob_draw(cx, 52, 25, v, r->k[q].mx, lk ? CL_Y : QC[q], CL_DG);
        fmt_val(r->k[q].fmt, v, t);
        centre_text(q * 60, 60, 86, &FONT_B, t, C_WHITE);
        centre_text(q * 60, 60, 106, &FONT_T, r->k[q].lab, QC[q]);
    }
}

/* KICK: the pitch falling and the level falling, the shape of the wave, the FM and the noise */
static float kick_wave(uint32_t w, float ph, float fmi, float ratio)
{
    switch (w) {
    case 0: return fm_sin_turns(ph);
    case 1: return ph - fm_floorf(ph) < 0.5f ? 4.0f * (ph - fm_floorf(ph)) - 1.0f : 3.0f - 4.0f * (ph - fm_floorf(ph));
    case 2: return ph - fm_floorf(ph) < 0.5f ? 0.8f : -0.8f;
    case 3: return fm_sin_turns(ph + fmi * fm_sin_turns(ph * ratio));
    case 4: return (float)(hash32((uint32_t)(ph * 40.0f)) & 1023u) * (1.0f / 512.0f) - 1.0f;
    default: return (fm_sin_turns(ph) + fm_sin_turns(ph * 2.76f) * 0.8f + fm_sin_turns(ph * 5.4f) * 0.6f) * 0.5f;
    }
}
static void vis_kick(void)
{
    char t[24], n[12];
    int32_t x, i;
    if (kpg() >= 3u) {
        vis_kseq();
        return;
    }
    if (kpg() == 0u) {
        float base = 25.0f * fm_exp2f((float)kick_p[K_TUN] * (4.5f / 127.0f)), sw = (float)kick_p[K_SWP] * (12.0f / 127.0f);
        float tp = 0.004f * fm_exp2f(6.5f * (float)kick_p[K_PDC] * (1.0f / 127.0f));
        float ta = 0.02f * fm_exp2f(6.0f * (float)kick_p[K_ADC] * (1.0f / 127.0f));
        int32_t py = 98, pa = 98;
        for (i = 0; i < 5; i++)
            cv_rect(4, 8 + i * 22, 232, 1, CL_DG);
        for (x = 0; x < 232; x++) {
            float tm = 0.6f * (float)x / 232.0f;
            float f = base * (1.0f + sw * fm_expf(-tm / tp));
            float e = fm_expf(-tm / ta);
            int32_t yf = 98 - (int32_t)(90.0f * fm_clampf(fm_log2f(f / 20.0f) / 9.5f, 0.0f, 1.0f)), ya = 98 - (int32_t)(90.0f * e);
            cv_rect(4 + x, ya, 1, 98 - ya, shade(CL_G, 3));
            if (x) {
                line2(3 + x, pa, 4 + x, ya, CL_G);
                line2(3 + x, py, 4 + x, yf, CL_C);
            }
            py = yf;
            pa = ya;
        }
        scopy(t, "PITCH ", 24);
        int_text((int32_t)(base * (1.0f + sw)), n);
        text_cat(t, n);
        text_cat(t, ">");
        int_text((int32_t)base, n);
        text_cat(t, n);
        text_cat(t, "HZ");
        cv_text(6, 104, &FONT_XS, t, CL_C);
        scopy(t, "AMP ", 24);
        tau_text(ta, n);
        text_cat(t, n);
        cv_text(150, 104, &FONT_XS, t, CL_G);
        cv_text(6, 4, &FONT_T, "HZ", CL_L);
    } else {
        uint32_t w = ((uint32_t)kick_p[K_WAV] * 6u) >> 7;
        float drv = 1.0f + (float)kick_p[K_DRV] * (5.0f / 127.0f), fmi = (float)kick_p[K_FMA] * (6.0f / 127.0f) * (kpg() == 2 ? 1.0f : 0.5f);
        float ratio = 0.5f + (float)kick_p[K_FMR] * (7.5f / 127.0f), cyc = kpg() == 1 ? 2.0f : 2.5f;
        int32_t amp = 10 + 32 * kick_p[K_LVL] / 127, py = 50;
        for (i = 0; i < 5; i++)
            cv_rect(4, 10 + i * 20, 232, 1, i == 2 ? CL_PNL : CL_DG);
        for (x = 0; x < 232; x++) {
            float ph = cyc * (float)x / 232.0f, s = kpg() == 1 ? kick_wave(w, ph, fmi, ratio) : kick_wave(3, ph, fmi, ratio);
            int32_t y;
            if (kpg() == 2)
                s += ((float)(hash32((uint32_t)x * 13u + 5u) & 1023u) * (1.0f / 512.0f) - 1.0f) * (float)kick_p[K_NSE] * (0.6f / 127.0f);
            s = fm_tanhf(drv * s) / fm_tanhf(drv);
            y = 50 - (int32_t)(s * (float)amp);
            if (x)
                line2(3 + x, py, 4 + x, y, kpg() == 1 ? CL_O : CL_C);
            py = y;
        }
        if (kpg() == 1) {                 /* the click: a spike at the start */
            int32_t h = 4 + 40 * kick_p[K_CLK] / 127;
            cv_rect(6, 50 - h, 2, 2 * h, C_WHITE);
            cv_rect(8, 50 - h / 2, 1, h, CL_Y);
            scopy(t, "WAVE ", 24);
            text_cat(t, WAV6[((uint32_t)kick_p[K_WAV] * 6u) >> 7]);
            cv_text(6, 90, &FONT_XS, t, CL_O);
            scopy(t, "DRIVE ", 24);
            int_text(kick_p[K_DRV], n);
            text_cat(t, n);
            cv_text(122, 90, &FONT_XS, t, CL_Y);
            scopy(t, "CLICK ", 24);
            int_text(kick_p[K_CLK], n);
            text_cat(t, n);
            cv_text(6, 107, &FONT_XS, t, C_WHITE);
            scopy(t, "LEVEL ", 24);
            int_text(kick_p[K_LVL], n);
            text_cat(t, n);
            cv_text(122, 107, &FONT_XS, t, CL_G);
        } else {
            int32_t d10 = (int32_t)(ratio * 10.0f);
            scopy(t, "FM ", 24);
            int_text(kick_p[K_FMA], n);
            text_cat(t, n);
            cv_text(6, 90, &FONT_XS, t, CL_C);
            scopy(t, "RATIO ", 24);
            int_text(d10 / 10, n);
            text_cat(t, n);
            text_cat(t, ".");
            int_text(d10 % 10, n);
            text_cat(t, n);
            cv_text(122, 90, &FONT_XS, t, CL_M);
            scopy(t, "NOISE ", 24);
            int_text(kick_p[K_NSE], n);
            text_cat(t, n);
            cv_text(6, 107, &FONT_XS, t, CL_O);
            scopy(t, "TONE ", 24);
            int_text(kick_p[K_TON], n);
            text_cat(t, n);
            cv_text(122, 107, &FONT_XS, t, CL_G);
        }
    }
}

/* PATCH: two lists joined by a curved cable */
static void cable(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t col, int dashed, int thick, int32_t *mx, int32_t *my)
{
    int32_t px = x0, py = y0, i;
    float off = (float)(x1 - x0) * 0.45f;
    for (i = 1; i <= 20; i++) {
        float t = (float)i / 20.0f, u = 1.0f - t, b0 = u * u * u, b1 = 3.0f * u * u * t, b2 = 3.0f * u * t * t, b3 = t * t * t;
        int32_t x = (int32_t)(b0 * (float)x0 + b1 * ((float)x0 + off) + b2 * ((float)x1 - off) + b3 * (float)x1);
        int32_t y = (int32_t)(b0 * (float)y0 + b1 * (float)y0 + b2 * (float)y1 + b3 * (float)y1);
        if (!dashed || (i & 1)) {
            cv_line(px, py, x, y, col);
            if (thick)
                cv_line(px, py + 1, x, y + 1, col);
        }
        px = x;
        py = y;
        if (i == 10) {
            *mx = x;
            *my = y;
        }
    }
}
static void vis_patch(void)
{
    int32_t i, d, mx = 120, my = 60;
    uint32_t sel = ui.pdst;
    int32_t ssrc = sd.src[sel];
    for (i = 0; i < 8; i++) {
        int32_t y = 4 + i * 15;
        int sa = (i == ssrc), da = ((int32_t)sel == i);
        cv_rect(4, y, 76, 14, sa ? CL_G : RGB(18, 22, 36));
        cv_text(8, y - 1, &FONT_XS, SRC_LONG[i], sa ? C_BLACK : CL_L);
        cv_rect(160, y, 76, 14, da ? CL_C : RGB(18, 22, 36));
        cv_text(164, y - 1, &FONT_XS, DEST_LONG[i], da ? C_BLACK : CL_L);
        cv_rect(80, y + 5, 4, 4, sa ? CL_G : CL_DG);
        cv_rect(156, y + 5, 4, 4, da ? CL_C : CL_DG);
    }
    for (d = 0; d < NPATCH; d++) {       /* the other cables first, the chosen one over them */
        int32_t s = sd.src[d], dx, dy;
        if (d == (int32_t)sel || s == 0)
            continue;
        cable(84, 4 + s * 15 + 7, 156, 4 + d * 15 + 7, ((sd.pon >> d) & 1u) ? RGB(130, 146, 214) : RGB(52, 60, 92), !((sd.pon >> d) & 1u), 0, &dx, &dy);
    }
    {
        int on = (sd.pon >> sel) & 1u;
        char t[12];
        int depth = clamp(((int)sd.amt[sel] - 64) * 100 / 63, -100, 100);
        cable(84, 4 + ssrc * 15 + 7, 156, 4 + (int32_t)sel * 15 + 7, on ? CL_M : CL_L, !on, 1, &mx, &my);
        signed_text(depth, t);
        cv_rect(mx - 17, my - 8, 34, 15, C_BLACK);
        centre_text(mx - 17, 34, my - 8, &FONT_S, t, on ? CL_M : CL_L);
    }
}


/* PATTERNS: a bank for each black key, a pattern for each white one */
static void vis_bt(void)                 /* BLUETOOTH: its state, and the devices (the ones kept, then the ones found) */
{
    int32_t i, y, n, first, rows, st = plat_bta_state();
    int live;
    const char *w;
    uint16_t col = CL_L;
    bt_list();
    w = bt_word(st);
    if (!bts.on) {
        cv_text(6, 2, &FONT_M, "SWITCHED OFF", CL_L);
        cv_text(6, 50, &FONT_T, "K1 TURNED TO THE RIGHT: SWITCH IT ON", CL_Y);
        cv_text(6, 80, &FONT_T, "THE SOUND GOES TO HEADPHONES AND SPEAKERS", CL_L);
        cv_text(6, 94, &FONT_T, "AND BLUETOOTH MIDI WORKS WITH A PHONE", CL_L);
        cv_text(6, 108, &FONT_T, "OR A COMPUTER", CL_L);
        return;
    }
    col = st == PLAT_BTA_STREAMING ? CL_G : (st == PLAT_BTA_PAGING || st == PLAT_BTA_LINKED || st == PLAT_BTA_SETUP) ? CL_C :
          st == PLAT_BTA_SCANNING ? CL_Y : (st == PLAT_BTA_REFUSED || st == PLAT_BTA_FAILED || st == PLAT_BTA_NO_LAST) ? CL_M : CL_L;
    cv_text(6, 2, &FONT_M, w, col);
    cv_rect(4, 28, 232, 1, CL_PNL);
    n = bt.n;
    live = st == PLAT_BTA_STREAMING && plat_bta_out_device();   /* (the air's readout takes the foot of the page: three rows of devices) */
    rows = live ? 3 : 5;
    first = clamp((int32_t)ui.bt_sel - rows / 2, 0, n > rows ? n - rows : 0);
    for (i = 0; i < rows && first + i < n; i++) {
        int32_t k = first + i, sel = k == ui.bt_sel;
        const char *tag = k < bt.kn ? "KEPT" : "NEW";
        uint16_t tc = k < bt.kn ? CL_L : CL_Y;
        y = 32 + i * 16;
        if (sel) {
            cv_rect(4, y, 232, 15, CL_PNL);
            cv_rect(5, y + 1, 230, 13, RGB(14, 22, 44));
        }
        if (k == 0 && bt.kn && (st == PLAT_BTA_PAGING || st == PLAT_BTA_LINKED || st == PLAT_BTA_SETUP || st == PLAT_BTA_STREAMING)) {
            tag = w;                     /* the device in use is the last one kept */
            tc = col;
        }
        cv_text(10, y, &FONT_XS, bt.name[k], sel ? C_WHITE : CL_W);
        cv_text(230 - text_w(&FONT_XS, tag), y, &FONT_XS, tag, tc);
    }
    if (st == PLAT_BTA_STREAMING && !plat_bta_out_device())
        w = "THE SOUND IS ON THE FM-1 OUTPUT (K3: OUTPUT)";
    else if (st == PLAT_BTA_STREAMING) {      /* the audio interrupt's load, the FM-1 side's queue, and the device's own delay */
        static char lv[40];
        char nb[12];
        int32_t us = plat_bta_latency_us();
        lv[0] = 0;
        text_cat(lv, BT_Q[BT_QUAL]);
        text_cat(lv, bts.midioff ? " MIDI OFF  CPU " : " MIDI ON  CPU ");
        int_text((int32_t)plat_cpu_pct(), nb);
        text_cat(lv, nb);
        text_cat(lv, "%  QUEUE ");
        int_text(us < 0 ? 0 : us / 1000, nb);
        text_cat(lv, nb);
        text_cat(lv, " MS");
        w = lv;
    }
    else if (st == PLAT_BTA_SCANNING)
        w = "LOOKING FOR HEADPHONES AND SPEAKERS...";
    else if (!n)
        w = "PUT THE DEVICE IN PAIRING MODE, THEN SCAN";
    else
        w = "K2 PICKS A DEVICE, K3 THE ACTION, K4 DOES IT";
    if (!live)
        cv_text(6, 114, &FONT_T, w, CL_L);
    else {                               /* what the air is doing, while the sound goes out by Bluetooth */
        uint32_t lk[7];
        cv_text(6, 84, &FONT_T, w, CL_L);
        if (plat_bta_link(lk)) {
            static char l2[40], l3[40];
            char nb[12];
            l2[0] = l3[0] = 0;
            text_cat(l2, "RX GOOD ");
            int_text((int32_t)lk[0], nb);
            text_cat(l2, nb);
            text_cat(l2, "  BAD ");
            int_text((int32_t)lk[1], nb);
            text_cat(l2, nb);
            text_cat(l2, "  LOST ");
            int_text((int32_t)lk[2], nb);
            text_cat(l2, nb);
            text_cat(l3, "SLOTS ");
            int_text((int32_t)lk[4], nb);
            text_cat(l3, nb);
            text_cat(l3, "  BP ");
            int_text((int32_t)lk[6], nb);
            text_cat(l3, nb);
            text_cat(l3, "  WAIT ");
            int_text((int32_t)lk[3], nb);
            text_cat(l3, nb);
            text_cat(l3, "  PREF ");
            int_text((int32_t)lk[5], nb);
            text_cat(l3, nb);
            cv_text(6, 98, &FONT_T, l2, (lk[1] * 5u > lk[0] || lk[2]) ? CL_M : CL_L);
            cv_text(6, 112, &FONT_T, l3, lk[4] && lk[4] < 5u ? CL_Y : CL_L);
        }
    }
}

static void vis_midi(void)               /* MIDI page 1: the clock in, the clock out, the pulse sync */
{
    char t[44], n[12];
    int32_t x;
    x = cv_text(6, 2, &FONT_M, "CLOCK IN ", CL_W);
    cv_text(x, 2, &FONT_M, xs.on ? "ON" : "OFF", xs.on ? CL_G : CL_L);
    if (!xs.on) {
        cv_text(6, 26, &FONT_T, "THE FM-1 KEEPS ITS OWN TEMPO", CL_L);
    } else if (xs.live) {
        scopy(t, "FOLLOWING A CLOCK: ", 44);
        if (xs.bpm) {
            int_text(xs.bpm, n);
            text_cat(t, n);
            text_cat(t, " BPM");
        } else {
            text_cat(t, "...");
        }
        cv_text(6, 26, &FONT_T, t, CL_G);
    } else {
        cv_text(6, 26, &FONT_T, "NO CLOCK YET (USB, MIDI JACK, BLUETOOTH)", CL_Y);
    }
    x = cv_text(6, 42, &FONT_M, "CLOCK OUT ", CL_W);
    cv_text(x, 42, &FONT_M, MS_CKOUT ? "ON" : "OFF", MS_CKOUT ? CL_G : CL_L);
    cv_text(6, 66, &FONT_T, MS_CKOUT ? (xs.on && xs.live ? "WAITS WHILE A CLOCK COMES IN" : "CLOCK, START, STOP TO USB AND BLUETOOTH") :
            "NO CLOCK IS SENT", MS_CKOUT ? CL_G : CL_L);
    x = cv_text(6, 82, &FONT_M, "PULSE OUT ", CL_W);
    if (MS_PULSE) {
        int_text(PULSE_PPQN[MS_PULSE], n);
        scopy(t, n, 44);
        text_cat(t, " PPQN");
        cv_text(x, 82, &FONT_M, t, CL_O);
        cv_text(6, 106, &FONT_T, "JACK: PULSES ON THE LEFT, SOUND ON THE RIGHT", CL_O);
    } else {
        cv_text(x, 82, &FONT_M, "OFF", CL_L);
        cv_text(6, 106, &FONT_T, "SYNC PULSES FOR GEAR WITH A SYNC INPUT", CL_L);
    }
}
static void vis_midi2(void)              /* MIDI page 2: the channels */
{
    char t[44], n[12];
    int32_t x;
    x = cv_text(6, 2, &FONT_M, "IN ", CL_W);
    if (MS_INCH) {
        int_text(MS_INCH, n);
        scopy(t, "CHANNEL ", 44);
        text_cat(t, n);
    } else {
        scopy(t, "ALL CHANNELS", 44);
    }
    cv_text(x, 2, &FONT_M, t, CL_G);
    cv_text(6, 26, &FONT_T, "NOTES PLAY THE SYNTH; NOTE 36 (C1), THE KICK", CL_L);
    x = cv_text(6, 42, &FONT_M, "OUT ", CL_W);
    if (MS_OUTCH) {
        int_text(MS_OUTCH, n);
        scopy(t, "CHANNEL ", 44);
        text_cat(t, n);
    } else {
        scopy(t, "OFF", 44);
    }
    cv_text(x, 42, &FONT_M, t, MS_OUTCH ? CL_C : CL_L);
    cv_text(6, 66, &FONT_T, "THE SYNTH'S STEPS AS NOTES", CL_L);
    x = cv_text(6, 82, &FONT_M, "KICK ", CL_W);
    if (MS_KICKCH) {
        int_text(MS_KICKCH, n);
        scopy(t, "CHANNEL ", 44);
        text_cat(t, n);
    } else {
        scopy(t, "OFF", 44);
    }
    cv_text(x, 82, &FONT_M, t, MS_KICKCH ? CL_O : CL_L);
    cv_text(6, 106, &FONT_T, "THE KICK'S STEPS AS NOTE 36. USB AND BLUETOOTH", CL_L);
}
static void vis_version(void)            /* VERSION: the firmware and what the audio costs */
{
    char t[24], n[12];
    int32_t x;
    cv_text(6, 2, &FONT_M, "FM-FAM", CL_G);
    x = cv_text(6, 30, &FONT_M, "VERSION ", CL_L);
    cv_text(x, 30, &FONT_M, APP_VERSION, CL_W);
    scopy(t, "", 24);
    int_text((int32_t)plat_cpu_pct(), n);
    text_cat(t, n);
    text_cat(t, " %");
    x = cv_text(6, 58, &FONT_M, "CPU ", CL_L);
    cv_text(x, 58, &FONT_M, t, plat_cpu_pct() > 80u ? CL_R : CL_W);
    cv_rect(6, 88, 228, 1, CL_PNL);
    cv_text(6, 93, &FONT_T, "BETA SOFTWARE: IT CAN FAIL, USE IT AT", CL_O);
    cv_text(6, 106, &FONT_T, "YOUR OWN RISK. NO WARRANTY (MPL-2.0)", CL_O);
}
static void vis_audio(void)              /* AUDIO: where the sound goes */
{
    int uo = !(bts.quality & BQ_USB_OFF), lo = !(bts.quality & BQ_LINE_OFF), fu = (bts.quality & BQ_USB_FULL) != 0;
    int32_t x;
    x = cv_text(6, 2, &FONT_M, "USB ", CL_W);
    cv_text(x, 2, &FONT_M, uo ? "ON" : "OFF", uo ? CL_G : CL_R);
    cv_text(6, 27, &FONT_T, uo ? (fu ? "TO THE COMPUTER, AT FULL LEVEL" : "TO THE COMPUTER, AT MASTER'S LEVEL") : "THE COMPUTER RECORDS SILENCE", CL_L);
    x = cv_text(6, 44, &FONT_M, "SPEAKER + JACK ", CL_W);
    cv_text(x, 44, &FONT_M, lo ? "ON" : "OFF", lo ? CL_G : CL_R);
    cv_text(6, 69, &FONT_T, "THE SPEAKER AND THE MINI JACK ARE ONE", CL_L);
    cv_text(6, 82, &FONT_T, "OUTPUT: A PLUG SWITCHES THE SPEAKER OFF", CL_L);
    cv_rect(6, 99, 228, 1, CL_PNL);
    cv_text(6, 104, &FONT_T, "K1 USB  K2 SPEAKER + JACK  K3 USB LEVEL", CL_Y);
}
static void vis_patt(void)
{
    int32_t i, k;
    if (pg() == 0) {
        for (i = 0; i < NBANK; i++) {
            char l[2];
            int32_t x = 4 + i * 18, sel = ui.bank == i, play = sq.cur / NPB == i;
            l[0] = (char)('A' + i);
            l[1] = 0;
            cv_rect(x, 2, 16, 15, sel ? CL_B : play ? CL_G : CL_DG);
            if (!sel && !play)
                cv_rect(x + 1, 3, 14, 13, C_BLACK);
            else if (!sel)
                cv_rect(x + 1, 3, 14, 13, RGB(10, 36, 24));
            centre_text(x, 16, 4, &FONT_T, l, sel ? C_BLACK : play ? CL_G : CL_L);
        }
        centre_text(204, 34, 4, &FONT_T, CMD_NAME[ui.cmd], ui.cmd ? CL_Y : CL_L);
        for (k = 0; k < NPB; k++) {
            int pi = (int)ui.bank * NPB + (int)k;
            int32_t x = 4 + (k % 4) * 59, y = 22 + (k / 4) * 26;
            int used = pat_used(&pats[pi]), play = pi == sq.cur, qd = pi == sq.queued;
            uint16_t bc = play ? CL_G : qd ? CL_O : used ? CL_PNL : CL_DG;
            char nm[8];
            cv_rect(x, y, 55, 23, bc);
            cv_rect(x + 1, y + 1, 53, 21, play ? RGB(12, 46, 30) : C_BLACK);
            pat_name(pi, nm);
            cv_text(x + 4, y + 1, &FONT_T, nm, play ? C_WHITE : used ? CL_W : CL_L);
            for (i = 0; i < pats[pi].len; i++) {
                uint8_t tr = pats[pi].trig[i];
                cv_rect(x + 4 + i * 3, y + 15, 2, 4, tr == 2 ? CL_M : tr ? CL_G : CL_DG);
            }
            if (pats[pi].ktrig[0] | pats[pi].ktrig[4])
                cv_rect(x + 40, y + 3, 7, 4, CL_O);
            {
                int np = 0;
                for (i = 0; i < NPK; i++)
                    np += pats[pi].pre[i] != 0;
                if (np)
                    cv_rect(x + 48, y + 3, 4, 4, CL_C);
            }
        }
    } else if (pg() == 1) {
        for (k = 0; k < 32; k++) {
            int32_t x = 4 + (k % 8) * 29, y = 4 + (k / 8) * 24;
            int have = sd.song[k] < NPAT, play = sd.song_on && sq.running && sq.song_pos == k;
            char nm[8];
            cv_rect(x, y, 27, 21, ui.slot == k ? C_WHITE : play ? CL_G : have ? CL_PNL : CL_DG);
            cv_rect(x + 1, y + 1, 25, 19, play ? RGB(12, 46, 30) : C_BLACK);
            if (have)
                pat_name(sd.song[k], nm);
            else
                scopy(nm, "--", 8);
            centre_text(x, 27, y + 4, &FONT_T, nm, have ? C_WHITE : CL_DG);
        }
        cv_text(6, 104, &FONT_S, sd.song_on ? "SONG ON" : "SONG OFF", sd.song_on ? CL_G : CL_L);
        {
            char t[16], n[12];
            scopy(t, "SLOT ", 16);
            int_text(ui.slot + 1, n);
            text_cat(t, n);
            text_cat(t, "/32");
            cv_text(236 - text_w(&FONT_S, t), 104, &FONT_S, t, CL_Y);
        }
    } else {                             /* the sound of the pattern playing: where each part of it came from, and whether it was changed since */
        int pi = (int)sq.cur;
        char nm[8], t[24];
        static const uint16_t KC[NPK] = {CL_G, CL_O, CL_Y, CL_M, CL_B};
        pat_name(pi, nm);
        cv_text(6, 2, &FONT_B, nm, C_WHITE);
        cv_text(70, 6, &FONT_T, "ITS OWN SOUND", CL_L);
        for (k = 0; k < NPK; k++) {
            int32_t y = 26 + k * 19;
            int idx = sd.pre[k];
            cv_rect(6, y, 8, 12, KC[k]);
            cv_text(20, y - 1, &FONT_XS, KIND_NAME[k], KC[k]);
            scopy(t, pre_name(k, idx), 24);
            cv_text(84, y - 1, &FONT_XS, t, C_WHITE);
            if (!pre_same(k, idx))
                cv_text(84 + text_w(&FONT_XS, t) + 6, y - 1, &FONT_XS, "EDITED", CL_L);
            if ((pg() == 2 && k < 4) || (pg() == 3 && k == 4))
                cv_rect(2, y, 2, 12, C_WHITE);
        }
        if (pg() == 3)
            cv_text(6, 118 - 4, &FONT_T, "TO BANK / TO ALL: THIS SOUND INTO OTHER PATTERNS", CL_L);
    }
}

/* RANDOM: for each knob of the page it mirrors: on or off, and how much */
static void rnd_range(int rp, int q, char *t)    /* what a knob of a random page draws, in a few letters */
{
    const row_t *r = rp_row(rp);
    char n[12];
    scopy(t, "", 16);
    if (!r)
        return;
    {
        const knob_t *kn = &r->k[q];
        if (kn->dk != DK_RANGE) {
            scopy(t, "AUTO", 16);        /* (a draw of its own: the pitch, the cutoff, the filter type, the kick's wave) */
        } else if (fmt_disc(kn->fmt)) {
            scopy(t, "ANY", 16);
        } else if (kn->lo == kn->hi) {
            int_text(kn->lo, n);
            text_cat(t, n);
        } else {
            int_text(kn->lo, n);
            text_cat(t, n);
            text_cat(t, "-");
            int_text(kn->hi, n);
            text_cat(t, n);
        }
    }
}
static void vis_rand(void)
{
    int rp = (int)pg(), q, all = rnd_in_all(rp);
    char t[40];
    const char *tag = all ? "IN OCT+ + REC" : "NOT IN OCT+ + REC";
    scopy(t, rp_mode(rp), 40);
    text_cat(t, "  ");
    text_cat(t, rp_title(rp));
    cv_text(4, 2, &FONT_XS, t, CL_W);
    cv_text(236 - text_w(&FONT_T, tag), 5, &FONT_T, tag, all ? CL_G : CL_O);     /* is this page part of the general random? */
    for (q = 0; q < 4; q++) {
        int on = (sd.rnd_en[rp] >> q) & 1, amt = sd.rnd_amt[rp][q];
        int32_t x = 4 + q * 58, h = amt * 36 / 127;
        uint16_t col = on ? QC[q] : CL_DG;
        cv_rect(x, 22, 54, 14, on ? col : CL_DG);
        if (!on)
            cv_rect(x + 1, 23, 52, 12, C_BLACK);
        centre_text(x, 54, 24, &FONT_T, on ? "ON" : "OFF", on ? C_BLACK : CL_L);
        cv_rect(x + 10, 40, 34, 38, CL_DG);
        cv_rect(x + 11, 41, 32, 36, C_BLACK);
        cv_rect(x + 12, 77 - h, 30, h, on ? shade(col, 14) : shade(CL_L, 4));
        cv_rect(x + 12, 77 - h, 30, 2, on ? col : CL_L);
        rnd_range(rp, q, t);             /* the values it draws */
        centre_text(x, 54, 81, &FONT_T, t, on ? CL_W : CL_L);
    }
    cv_text(4, 96, &FONT_T, "KEYS 1-4 ON/OFF  5 ALL ON  6 THIS PAGE  7 ALL", CL_L);
    cv_text(4, 110, &FONT_T, all ? "OCT- + REC: THIS PAGE  OCT+ + REC: ALL" : "OCT- + REC: THIS PAGE (ONLY WAY TO DRAW IT)", CL_Y);
}

/* SCALE: the twelve notes (the root, the notes of the scale), and a melody through the scale, the spread and the transposition */
static void vis_scale(void)
{
    int32_t i, x, py = 0;
    char t[40];
    if (pg() == 0) {
        for (i = 0; i < 12; i++) {
            int pc = (int)(((unsigned)i + 0u) % 12u), root = pc == sd.sc[SC_ROOT], in = scale_has(pc);
            int32_t cx = 4 + i * 19;
            int blk = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            uint16_t col = root ? CL_Y : in ? CL_G : CL_DG;
            cv_rect(cx, blk ? 4 : 18, 17, blk ? 22 : 34, col);
            if (!in && !root)
                cv_rect(cx + 1, blk ? 5 : 19, 15, blk ? 20 : 32, C_BLACK);
            centre_text(cx, 17, blk ? 8 : 36, &FONT_T, NOTE12[pc], root ? C_BLACK : in ? (blk ? C_BLACK : C_BLACK) : CL_L);
        }
        scopy(t, NOTE12[sd.sc[SC_ROOT] % 12], 40);
        text_cat(t, " ");
        text_cat(t, SCALES[sd.sc[SC_TYPE] % NSCALE].n);
        cv_text(6, 62, &FONT_B, t, C_WHITE);
        cv_text(6, 82, &FONT_XS, sd.sc[SC_QUANT] ? "QUANTIZE ON: STEPS SNAP TO THE SCALE" : "QUANTIZE OFF: STEPS KEEP THEIR PITCH", sd.sc[SC_QUANT] ? CL_G : CL_L);
        cv_text(6, 100, &FONT_XS, "ANY KEY SETS THE ROOT. THE KICK IS NOT TUNED.", CL_L);
    }
    if (pg() == 1) {                     /* a test melody (semitones) as it will sound */
        static const int8_t MEL[8] = {0, 2, 3, 7, 12, 10, 5, 1};
        cv_text(6, 2, &FONT_XS, "TEST MELODY THROUGH THE SETTINGS", CL_L);
        for (i = 0; i < 6; i++)
            cv_rect(4, 20 + i * 18, 232, 1, CL_DG);
        for (i = 0; i < 8; i++) {
            float nt = note_for(MEL[i]);
            int32_t y = clamp(108 - (int32_t)((nt - 44.0f) * 3.2f), 20, 108);
            x = 12 + i * 29;
            if (i)
                line2(x - 29, py, x, y, CL_C);
            cv_rect(x - 3, y - 2, 7, 5, CL_G);
            py = y;
        }
    }
    if (pg() == 1) {
        char n[12];
        scopy(t, "TRANSPOSE ", 40);
        fmt_val(F_TRN, sd.sc[SC_TRANS], n);
        text_cat(t, n);
        cv_text(6, 112, &FONT_T, t, CL_C);
        scopy(t, "SPREAD ", 40);
        fmt_val(F_SPREAD, sd.sc[SC_SPREAD], n);
        text_cat(t, n);
        cv_text(122, 112, &FONT_T, t, CL_O);
    }
}

/* HOME: the list of the page, the action, and the name being edited */
static void vis_home(void)
{
    int t = (int)pg(), n = lib_n(t), sel = ui.hidx[t], top, i;
    char s[24], nm[16];
    top = clamp(sel - 3, 0, n > 6 ? n - 6 : 0);
    for (i = 0; i < 6 && top + i < n; i++) {
        int idx = top + i, used = lib_used(t, idx);
        int32_t y = 2 + i * 16;
        int cur = t > 0 && sd.pre[t - 1] == idx;
        if (idx == sel)
            cv_rect(2, y, 236, 15, RGB(30, 44, 96));
        if (t == 0) {
            int_text(idx + 1, nm);
        } else if (idx < NFACT[t - 1]) {
            nm[0] = PKC[t - 1];
            nm[1] = 0;
            int_text(idx + 1, s);
            text_cat(nm, s);
        } else {
            scopy(nm, "U", 4);
            int_text(idx - NFACT[t - 1] + 1, s);
            text_cat(nm, s);
        }
        cv_text(8, y - 1, &FONT_XS, nm, idx == sel ? CL_Y : CL_L);
        if (used)
            cv_text(46, y - 1, &FONT_XS, lib_name(t, idx), idx == sel ? C_WHITE : CL_W);
        else
            cv_text(46, y - 1, &FONT_XS, "- EMPTY -", CL_DG);
        if (t == 0 && used)
            cv_text(184, y - 1, &FONT_XS, "A-C", CL_L);
        if (t > 0 && idx >= NFACT[t - 1])
            cv_text(214, y - 1, &FONT_XS, "USR", CL_C);
        if (cur)
            cv_rect(2, y + 3, 3, 9, CL_G);
    }
    /* the line under: the action, and the name when it is being renamed */
    cv_rect(2, 102, 236, 1, CL_PNL);
    if (ui.hact == 2) {
        char *nmp = lib_ren(t, sel);
        int32_t x = 8;
        cv_text(8, 106, &FONT_XS, nmp ? "NAME" : "FACTORY: NO RENAME", CL_L);
        if (nmp) {
            for (i = 0; i < PN - 1; i++) {
                char c[2];
                c[0] = nmp[0] && i < 8 && nmp[i] ? nmp[i] : (i < (int)ui.hcur + 1 ? ' ' : ' ');
                c[1] = 0;
                if (i > 0 && !nmp[i - 1] && !nmp[i])
                    c[0] = ' ';
                x = 60 + i * 14;
                cv_rect(x, 104, 12, 18, i == ui.hcur ? CL_Y : CL_DG);
                cv_rect(x + 1, 105, 10, 16, C_BLACK);
                if (c[0] != ' ')
                    cv_text(x + 2, 105, &FONT_B, c, i == ui.hcur ? CL_Y : C_WHITE);
            }
            if (t == 0)
                cv_text(180, 108, &FONT_T, "THEN SAVE", CL_L);
        }
    } else {
        scopy(s, ACT_NAME[ui.hact], 24);
        text_cat(s, " > ");
        if (t == 0 && ui.hact == 1)
            text_cat(s, "SLOT ");
        if (t == 0 && ui.hact != 1) {
            text_cat(s, "SLOT ");
        }
        {
            char d[12];
            if (t == 0)
                int_text(sel + 1, d);
            else
                d[0] = 0;
            text_cat(s, d);
        }
        cv_text(8, 106, &FONT_XS, s, ui.hact == 3 ? CL_R : CL_G);
        scopy(s, t == 0 ? "NOW: " : "LIVE: ", 24);
        text_cat(s, t == 0 ? sd.pname : pre_name(t - 1, sd.pre[t - 1]));
        cv_text(236 - text_w(&FONT_XS, s), 106, &FONT_XS, s, CL_L);
    }
}


static void draw_vis(void)
{
    const row_t *r = row_of(ui.mode, pg());
    cv_begin(240, 126, C_BLACK);
    switch (ui.mode) {
    case M_SEQ: vis_seq(); break;
    case M_SYN:
        if (pg() == 0)
            vis_osc();
        else if (pg() == 1)
            vis_filter();
        else if (pg() == 2)
            vis_env();
        else
            vis_dials(r);
        break;
    case M_PATCH: vis_patch(); break;
    case M_KICK: vis_kick(); break;
    case M_FX: vis_fx(); break;
    case M_PATT: vis_patt(); break;
    case M_SET:
        if (pg() == SP_AUDIO)
            vis_audio();
        else if (pg() == SP_MIDI)
            vis_midi();
        else if (pg() == SP_MIDI2)
            vis_midi2();
        else if (pg() == SP_BT)
            vis_bt();
        else
            vis_version();
        break;
    case M_RAND: vis_rand(); break;
    case M_SCALE: vis_scale(); break;
    default:
        vis_home();
        break;
    }
    cv_blit(0, 42);
}

void app_ui_init(void)
{
    uint32_t i;
    rec_sel = &ui.sel;                   /* (the MIDI recording, stopped, writes at the selected step) */
    for (i = 0; i < 25u; i++) {
        float a = (135.0f + 270.0f * (float)i / 24.0f) * 3.14159265f / 180.0f;
        kc[i] = (int16_t)(fm_cosf(a) * 256.0f);
        ks[i] = (int16_t)(fm_sinf(a) * 256.0f);
    }
    ui.held_step = 0xFFu;
    ui.clip = -1;
    ui.mode = M_SEQ;
    bt_start();                          /* (Bluetooth, if it was left on) */
    home_enter();
    lcd_fill(0, 0, 240, 240, C_BLACK);
    dirty_all();
}

void app_frame(void)
{
    uint32_t now = plat_ms(), sig;
    static uint32_t last_k;
    int quiet;
    input();
    snd_sync();                          /* what was changed belongs to the pattern playing */
    quiet = (uint32_t)(xs.now - loud_at) > 13230u;     /* nothing to hear for 0.3 s: a flash write (it stops the sound) cuts no tail */
    if (dirty && !bk_hold && !sq.running && now - edit_t >= 1500u && (quiet || now - edit_t >= 15000u) && autosave_tick()) {    /* the autosave: all saved */
        dirty = 0;
        ui.d_head = 1;
    }
    if (bts_dirty && !bk_hold && !sq.running && now - bts_t >= 1500u && (quiet || now - bts_t >= 15000u))    /* what Bluetooth keeps, or its switch */
        bt_store();
    bt_poll();
    if (sq.rec_wrote) {                  /* notes were recorded: the pattern is changed (autosave), the steps redrawn */
        sq.rec_wrote = 0;
        touch();
        ui.d_vis = ui.d_foot = ui.d_strip = 1;
    }
    {
        static uint32_t xlast;
        uint32_t x = (uint32_t)xs.live << 16 | xs.bpm;
        if (x != xlast) {
            xlast = x;
            ui.d_head = 1;
            if (ui.mode == M_SET && pg() == SP_MIDI)
                ui.d_vis = 1;
        }
    }
    if (now - ui.cpu_t >= 500u) {
        ui.cpu_t = now;
        if (plat_cpu_pct() != ui.cpu_last) {
            ui.cpu_last = plat_cpu_pct();
            if (ui.mode == M_SET && pg() == SP_VER)
                ui.d_vis = 1;
        }
    }
    sig = (uint32_t)sq.step * 2u + sq.running + (uint32_t)sq.cur * 64u + (uint32_t)(sq.queued + 1) * 16384u + (uint32_t)sq.song_pos * 0x400000u;
    if (sig != ui.last_sig) {
        ui.last_sig = sig;
        ui.d_strip = ui.d_head = 1;
        if (ui.mode == M_SEQ || ui.mode == M_PATT)
            ui.d_vis = 1;
    }
    if (ui.mode == M_KICK && last_k != sq.kstep + 1u + sq.running * 256u) {
        last_k = sq.kstep + 1u + sq.running * 256u;
        ui.d_strip = 1;
        if (kpg() >= 3u)
            ui.d_vis = 1;
    }
    if (sq.recalled) {                   /* another pattern's own sound came up */
        sq.recalled = 0;
        dirty_all();
    }
    if (ui.msg_until && now >= ui.msg_until) {
        ui.msg_until = 0;
        ui.d_head = 1;
    }
    if (ui.d_head) {
        ui.d_head = 0;
        draw_head();
    }
    if (ui.d_strip) {
        ui.d_strip = 0;
        draw_strip();
    }
    if (ui.d_vis) {
        ui.d_vis = 0;
        draw_vis();
    }
    if (ui.d_foot) {
        ui.d_foot = 0;
        draw_foot();
    }
}

#ifdef PLAT_HOST
static uint32_t sum_of(int which)        /* a sum of a group of values: the tests ask whether a group changed */
{
    uint32_t h = 0, i;
    switch (which) {
    case 0: for (i = 0; i < NSYN; i++) h = h * 31u + syn[i]; break;
    case 1: for (i = 0; i < NKICK; i++) h = h * 31u + kick_p[i]; break;
    case 2: for (i = 0; i < NFX; i++) h = h * 31u + sd.fx[i]; break;
    case 3: for (i = 0; i < NSC; i++) h = h * 31u + sd.sc[i]; break;
    default: for (i = 0; i < NPATCH; i++) h = h * 31u + sd.src[i] + 7u * sd.amt[i]; h = h * 31u + sd.pon; break;
    }
    return h;
}
static uint32_t snap_sum[5];

/* the self-checks of the random draws (expect rndsteps, rndkick, rndsyn, rndcables: the number of rules the draws broke,
 * over some thousands of them; the sound and the pattern come back as they were) */
static int rnd_selftest(const char *what)
{
    static uint8_t keep_syn[NSYN], keep_kick[NKICK];
    pat_t keep_p = pats[sq.cur], *p = &pats[sq.cur];
    uint8_t keep_src[NPATCH], keep_amt[NPATCH], keep_pon = sd.pon;
    int bad = 0, i, j, n = 0, c0 = 0, c1 = 0, c2 = 0;
    uint32_t sv = rnd_s;
    for (i = 0; i < NSYN; i++)
        keep_syn[i] = syn[i];
    for (i = 0; i < NKICK; i++)
        keep_kick[i] = kick_p[i];
    for (i = 0; i < NPATCH; i++) {
        keep_src[i] = sd.src[i];
        keep_amt[i] = sd.amt[i];
    }
    if (!strcmp(what, "rndsteps")) {     /* pitches from the notes of the draw, step 1 on and at 0, 70..127, rests one in four */
        static const int8_t OK[13] = {-12, -9, -7, -5, -2, 0, 3, 5, 7, 10, 12, 15, 19};
        int rest = 0, total = 0;
        p->len = 8;
        for (j = 0; j < 1500; j++) {
            for (i = 0; i < 8; i++) {
                p->trig[i] = 1;
                p->lk_p[i][0] = 1;
            }
            rnd_apply(0, 0, 127);
            rnd_apply(0, 1, 127);
            if (p->pit[0] != 0 || !p->trig[0])
                bad++;
            for (i = 0; i < 8; i++) {
                int ok = 0;
                for (int q = 0; q < 13; q++)
                    ok |= p->pit[i] == OK[q];
                bad += !ok;
                bad += p->vel[i] < 70 || p->vel[i] > 127;
                bad += p->lk_p[i][0] != 0;
                if (i) {
                    total++;
                    rest += p->trig[i] == 0;
                }
            }
        }
        bad += !(rest * 100 > total * 22 && rest * 100 < total * 28);          /* 25 % */
    } else if (!strcmp(what, "rndkick")) {
        int hits = 0, acc = 0, h0 = 0, h8 = 0, h1 = 0;
        p->klen = 16;
        for (j = 0; j < 3000; j++) {
            rnd_apply(12, 0, 127);
            bad += p->ktrig[0] != 2 || p->ktrig[1] != 0;
            for (i = 1; i < 16; i++) {
                bad += p->ktrig[i] && p->ktrig[i - 1];
                hits += p->ktrig[i] != 0;
                acc += p->ktrig[i] == 2;
            }
            h0 += p->ktrig[4] != 0;
            h8 += p->ktrig[8] != 0;
            h1 += p->ktrig[13] != 0;
        }
        bad += !(acc * 100 > hits * 20 && acc * 100 < hits * 30);              /* one in four an accent */
        bad += !(h0 * 100 > 3000 * 38 && h0 * 100 < 3000 * 47);                /* step 5: 42 % */
        bad += !(h8 * 100 > 3000 * 50 && h8 * 100 < 3000 * 60);                /* step 9: 55 % */
        bad += !(h1 * 100 > 3000 * 8 && h1 * 100 < 3000 * 16);                 /* step 14: 12 % */
    } else if (!strcmp(what, "rndsyn")) {
        int nz_nse = 0, nz_fm = 0, on_syn = 0, nz_v2 = 0, nz_eg1 = 0, nz_atk = 0, hp = 0, hat_runs = 0, runs = 2000;
        for (j = 0; j < runs; j++) {
            rnd_hat = (int)(rnd() % 100u) < 15;
            for (i = 2; i < 8; i++)
                rnd_page(i);
            for (i = 9; i < 12; i++)
                rnd_page(i);
            nz_nse += syn[P_NSE] != 0;
            nz_fm += syn[P_FM] != 0;
            on_syn += syn[P_SYN] >= 64;
            nz_v2 += syn[P_V2LVL] != 0;
            nz_eg1 += syn[P_V1EG] != 0;
            nz_atk += syn[P_ATK] != 0;
            hp += (syn[P_MODE] >> 5) == 3;
            hat_runs += rnd_hat;
            bad += syn[P_CUT] < 15 || syn[P_CUT] > 120 || syn[P_RES] > 50 || syn[P_V1LVL] < 75 || syn[P_VOL] != 100;
            bad += rnd_hat ? ((syn[P_MODE] >> 5) != 3 || syn[P_CUT] > 55) : ((syn[P_MODE] >> 5) > 1 || syn[P_CUT] < 62);
            bad += syn[P_VDEC] < 10 || syn[P_VDEC] > 85 || syn[P_ADEC] < 35 || syn[P_ADEC] > 100;
            bad += kick_p[K_TUN] < 14 || kick_p[K_TUN] > 42 || kick_p[K_ADC] < 35 || kick_p[K_ADC] > 85 || kick_p[K_WAV] > 63;
            bad += kick_p[K_LVL] < 90 || kick_p[K_LVL] > 100;
            n++;
        }
        c0 = nz_nse;
        bad += !(nz_nse * 100 > n * 33 && nz_nse * 100 < n * 45);               /* noise on in 39 % */
        bad += !(nz_fm * 100 > n * 40 && nz_fm * 100 < n * 52);                 /* FM in 46 % */
        bad += !(on_syn * 100 > n * 20 && on_syn * 100 < n * 32);               /* sync in 26 % */
        bad += !(nz_v2 * 100 > n * 69 && nz_v2 * 100 < n * 81);                 /* the second oscillator in 75 % */
        bad += !(nz_eg1 * 100 > n * 45 && nz_eg1 * 100 < n * 57);               /* the pitch envelope in 51 % */
        bad += !(nz_atk * 100 > n * 4 && nz_atk * 100 < n * 12);                /* the attack in 8 % */
        bad += !(hat_runs * 100 > n * 12 && hat_runs * 100 < n * 18 && hp * 100 >= hat_runs * 100);
        c1 = hp;
        c2 = hat_runs;
    } else if (!strcmp(what, "rndcables")) {
        int pos = 0, tot = 0, hist[9] = {0};
        for (j = 0; j < 3000; j++) {
            rnd_apply(8, 0, 127);
            int cnt = 0;
            for (i = 0; i < NPATCH; i++)
                if ((sd.pon >> i) & 1u) {
                    int d = (int)sd.amt[i] - 64;
                    cnt++;
                    tot++;
                    pos += d > 0;
                    bad += !(sd.src[i] >= 1 && sd.src[i] < NSRC) || (d > -13 && d < 13) || d > 51 || d < -51;
                } else {
                    bad += sd.src[i] != S_OFF || sd.amt[i] != 64;
                }
            hist[cnt]++;
            bad += cnt < 1 || cnt > 4;
        }
        bad += !(pos * 100 > tot * 64 && pos * 100 < tot * 76);                  /* seven in ten positive */
        bad += hist[3] == 0 || hist[4] == 0;
    } else {
        bad = -1;
    }
    (void)c0; (void)c1; (void)c2;
    pats[sq.cur] = keep_p;
    for (i = 0; i < NSYN; i++)
        syn[i] = keep_syn[i];
    for (i = 0; i < NKICK; i++)
        kick_p[i] = keep_kick[i];
    for (i = 0; i < NPATCH; i++) {
        sd.src[i] = keep_src[i];
        sd.amt[i] = keep_amt[i];
    }
    sd.pon = keep_pon;
    rnd_s = sv;
    publish();
    return bad;
}

int app_expect(const char *what, int *got)
{
    pat_t *p = &pats[sq.cur];
    if (!strcmp(what, "stepsok")) {      /* the steps of a draw: step 1 on at pitch 0, the others the draw's pitches, a velocity of 70..127 */
        static const int8_t OK[13] = {-12, -9, -7, -5, -2, 0, 3, 5, 7, 10, 12, 15, 19};
        int ok = p->len >= 1 && p->pit[0] == 0 && p->trig[0] != 0;
        for (uint32_t i = 0; i < p->len; i++) {
            int f = 0;
            for (int q = 0; q < 13; q++)
                f |= p->pit[i] == OK[q];
            ok &= f && p->vel[i] >= 70 && p->vel[i] <= 127;
        }
        *got = ok;
        return 1;
    }
    if (!strcmp(what, "kickok")) {       /* the kick of a draw: step 1 an accent, no two steps in a row */
        int ok = p->ktrig[0] == 2;
        for (uint32_t i = 1; i < p->klen; i++)
            ok &= !(p->ktrig[i] && p->ktrig[i - 1u]);
        *got = ok;
        return 1;
    }
    if (!strcmp(what, "ponn") || !strcmp(what, "ponok")) {   /* ponn: the cables that are connected; ponok: one to four of them */
        int c = 0;
        for (int i = 0; i < NPATCH; i++)
            c += (sd.pon >> i) & 1;
        *got = what[3] == 'o' ? c >= 1 && c <= 4 : c;
        return 1;
    }
    if (!strncmp(what, "rnd", 3) && (!strcmp(what, "rndsteps") || !strcmp(what, "rndkick") || !strcmp(what, "rndsyn") || !strcmp(what, "rndcables"))) {
        *got = rnd_selftest(what);
        return 1;
    }
    if (!strncmp(what, "bt", 2) && (!strcmp(what, "bton") || !strcmp(what, "btsel") || !strcmp(what, "btact") || !strcmp(what, "btn") ||
                                     !strcmp(what, "btkn") || !strcmp(what, "btstate") || !strcmp(what, "btdirty") || !strcmp(what, "btname") ||
                                     !strcmp(what, "btq") || !strcmp(what, "btqp") || !strcmp(what, "btmidioff"))) {
        bt_list();                       /* the Bluetooth page: bton, btsel, btact, btn (devices listed), btkn (kept), btstate, btdirty; btname: the first letter of the selected one */
        *got = !strcmp(what, "bton") ? bts.on : !strcmp(what, "btsel") ? ui.bt_sel : !strcmp(what, "btact") ? ui.bt_act :
               !strcmp(what, "btn") ? bt.n : !strcmp(what, "btkn") ? bt.kn : !strcmp(what, "btstate") ? plat_bta_state() :
               !strcmp(what, "btdirty") ? bts_dirty : !strcmp(what, "btq") ? (int)BT_QUAL : !strcmp(what, "btqp") ? plat_bta_quality(-1) :
               !strcmp(what, "btmidioff") ? bts.midioff : bt.n ? bt.name[ui.bt_sel][0] : 0;
        return 1;
    }
    if (!strcmp(what, "fp5rt") || !strcmp(what, "fp4load")) {
        /* fp5rt: a pattern full of awkward values goes through pat_pack and pat_unpack and comes back the same (1).
         * fp4load: a bank of v0.9 to v0.17 (FP4, written here the way they did) is read by bank_load: the steps are as
         * they were, the kick steps take no pitch and full volume (1). Bank K's object is used and put back. */
        static pat_t a, b, keep[NPB];
        static uint8_t buf[PPK];
        int ok = 1, i, j;
        pat_wipe(&a);
        for (i = 0; i < NSTEP; i++) {
            a.trig[i] = (uint8_t)(i % 3);
            a.pit[i] = (int8_t)((i * 7) % 49 - 24);
            a.vel[i] = (uint8_t)((i * 9) % 128);
            a.cond[i] = (uint8_t)(i % NCOND);
            a.gate[i] = (uint8_t)(1 + (i * 5) % GATE_FULL);
            a.rat[i] = (uint8_t)(i % 4);
            a.lk_p[i][0] = (uint8_t)(i % (NSYN + 1));
            a.lk_p[i][1] = (uint8_t)((NSYN - i) % (NSYN + 1));
            a.lk_v[i][0] = (uint8_t)((i * 11) % 128);
            a.lk_v[i][1] = (uint8_t)(127 - (i * 5) % 128);
            a.ktrig[i] = (uint8_t)((i + 1) % 3);
            a.kcond[i] = (uint8_t)((i * 3) % NCOND);
            a.krat[i] = (uint8_t)((i + 1) % 4);
            a.kpit[i] = (int8_t)((i * 5) % 49 - 24);
            a.kvol[i] = (uint8_t)((i * 17) % 128);
        }
        a.pit[0] = -24, a.pit[1] = 24, a.kpit[0] = -24, a.kpit[1] = 24, a.kvol[0] = 0, a.kvol[1] = 127, a.vel[1] = 127, a.gate[1] = GATE_FULL;
        a.lk_p[1][0] = NSYN, a.lk_v[1][0] = 127, a.cond[1] = NCOND - 1, a.kcond[1] = NCOND - 1;
        a.len = 13, a.swing = 77, a.klen = 11, a.sok = 1;
        for (i = 0; i < NPK; i++)
            a.pre[i] = (uint8_t)(i + 1);
        for (i = 0; i < SNDSZ; i++)
            a.snd[i] = (uint8_t)(i * 3 + 1);
        if (!strcmp(what, "fp5rt")) {
            pat_pack(&a, buf);
            pat_wipe(&b);
            pat_unpack(&b, buf);
            *got = memcmp(&a, &b, sizeof a) == 0;
            return 1;
        }
        for (j = 0; j < NPB; j++)
            keep[j] = pats[10 * NPB + j];
        bk_buf.magic = PAT_MAGIC4;
        for (j = 0; j < NPB; j++) {
            uint8_t *o = bk_buf.d[j];
            for (i = 0; i < NSTEP; i++, o += 9) {
                o[0] = (uint8_t)((a.trig[i] & 3u) | (uint32_t)(a.ktrig[i] & 3u) << 2 | (uint32_t)(a.rat[i] & 3u) << 4 | (uint32_t)(a.krat[i] & 3u) << 6);
                o[1] = (uint8_t)((a.cond[i] & 15u) | (uint32_t)(a.kcond[i] & 15u) << 4);
                o[2] = (uint8_t)a.pit[i], o[3] = a.vel[i], o[4] = a.gate[i];
                o[5] = a.lk_p[i][0], o[6] = a.lk_p[i][1], o[7] = a.lk_v[i][0], o[8] = a.lk_v[i][1];
            }
            o[0] = (uint8_t)(a.len | 0x80u), o[1] = (uint8_t)(a.swing + j), o[2] = a.klen;
            for (i = 0; i < NPK; i++)
                o[3 + i] = a.pre[i];
            for (i = 0; i < SNDSZ; i++)
                o[3 + NPK + i] = a.snd[i];
        }
        store_put(OBJ_BANK0 + 10u, &bk_buf, sizeof bk_buf);
        ok = bank_load(OBJ_BANK0 + 10u, 10 * NPB);
        for (j = 0; j < NPB; j++) {
            const pat_t *q = &pats[10 * NPB + j];
            for (i = 0; i < NSTEP; i++)
                ok &= q->trig[i] == a.trig[i] && q->pit[i] == a.pit[i] && q->vel[i] == a.vel[i] && q->cond[i] == a.cond[i] &&
                      q->gate[i] == a.gate[i] && q->rat[i] == a.rat[i] && q->lk_p[i][0] == a.lk_p[i][0] && q->lk_v[i][1] == a.lk_v[i][1] &&
                      q->ktrig[i] == a.ktrig[i] && q->kcond[i] == a.kcond[i] && q->krat[i] == a.krat[i] && q->kpit[i] == 0 && q->kvol[i] == KVOL_DEF;
            ok &= q->len == 13 && q->sok == 1 && q->swing == 77 + j && q->klen == 11 && q->snd[5] == a.snd[5];
        }
        for (j = 0; j < NPB; j++)
            pats[10 * NPB + j] = keep[j];
        bank_pack(10 * NPB);
        store_put(OBJ_BANK0 + 10u, &bk_buf, sizeof bk_buf);
        *got = ok;
        return 1;
    }
    if (!strcmp(what, "snap")) {         /* snap: remember the groups; chgsyn, chgkick, chgfx, chgsc, chgpatch: changed since? */
        for (int i = 0; i < 5; i++)
            snap_sum[i] = sum_of(i);
        *got = 0;
        return 1;
    }
    if (!strncmp(what, "chg", 3)) {
        int i = !strcmp(what + 3, "syn") ? 0 : !strcmp(what + 3, "kick") ? 1 : !strcmp(what + 3, "fx") ? 2 :
                !strcmp(what + 3, "sc") ? 3 : !strcmp(what + 3, "patch") ? 4 : -1;
        if (i < 0)
            return 0;
        *got = sum_of(i) != snap_sum[i];
        return 1;
    }
    if (!strcmp(what, "step"))
        *got = sq.step;
    else if (!strcmp(what, "kstep"))
        *got = sq.kstep;
    else if (!strcmp(what, "running"))
        *got = sq.running;
    else if (!strcmp(what, "outbits"))
        *got = bts.quality >> 4;
    else if (!strncmp(what, "ms", 2) && what[2])
        *got = midi_set[atoi(what + 2) & 7];
    else if (!strcmp(what, "rec"))
        *got = sq.rec;
    else if (!strcmp(what, "sel"))
        *got = ui.sel;
    else if (!strcmp(what, "mute_k"))
        *got = sq.mute_k;
    else if (!strcmp(what, "mute_s"))
        *got = sq.mute_s;
    else if (!strcmp(what, "notes_in"))
        *got = (int)co.notes_in;
    else if (!strcmp(what, "pulses"))
        *got = (int)co.pulses;
    else if (!strcmp(what, "sync"))
        *got = xs.on;
    else if (!strcmp(what, "extlive"))
        *got = xs.live;
    else if (!strcmp(what, "extbpm"))
        *got = xs.bpm;
    else if (!strcmp(what, "bpm"))
        *got = sd.bpm;
    else if (!strcmp(what, "pat"))
        *got = sq.cur;
    else if (!strcmp(what, "queued"))
        *got = sq.queued;
    else if (!strcmp(what, "trigs"))
        *got = (int)sq.trigs;
    else if (!strcmp(what, "ktrigs"))
        *got = (int)sq.ktrigs;
    else if (!strcmp(what, "mode"))
        *got = ui.mode;
    else if (!strcmp(what, "page"))
        *got = (int)pg();
    else if (!strcmp(what, "sel"))
        *got = ui.sel;
    else if (!strcmp(what, "ksel"))
        *got = ui.ksel;
    else if (!strcmp(what, "bank"))
        *got = ui.bank;
    else if (!strcmp(what, "len"))
        *got = p->len;
    else if (!strcmp(what, "klen"))
        *got = p->klen;
    else if (!strcmp(what, "swing"))
        *got = p->swing;
    else if (!strcmp(what, "fxen"))
        *got = sd.fxen;
    else if (!strcmp(what, "pon"))
        *got = sd.pon;
    else if (!strcmp(what, "dirty"))
        *got = dirty;
    else if (!strcmp(what, "bkhold") || !strcmp(what, "bkfree"))      /* a backup starts streaming / ends (app_backup_hold) */
    {
        app_backup_hold(what[2] == 'h');
        *got = bk_hold;
    }
    else if (!strcmp(what, "bkflush"))                               /* a backup starts, stopped: what is not saved yet is saved */
    {
        app_backup_flush();
        *got = dirty;
    }
    else if (!strcmp(what, "bkrestore"))                             /* the flash holds a restored set: everything loaded again */
    {
        app_backup_restored();
        *got = 0;
    }
    else if (!strcmp(what, "bkplaying"))
        *got = app_backup_playing();
    else if (!strcmp(what, "bkcopyp"))                               /* project slot 1 into slot 2 (from 0), object by object (a restored file's way) */
    {
        static uint8_t b[3840];
        int rc = 0;
        for (int j = 0; j < 4; j++) {
            int n = plat_store_load(PJ_OBJ(1, j), b, sizeof b);
            rc |= n > 0 ? store_put(PJ_OBJ(2, j), b, (uint32_t)n) : 0;
        }
        *got = rc;
    }
    else if (!strcmp(what, "sok"))
        *got = p->sok;
    else if (!strncmp(what, "sokp", 4) && what[4])           /* sokpN: pattern N has a sound of its own */
        *got = pats[atoi(what + 4) % NPAT].sok;
    else if (!strncmp(what, "psnd", 4) && what[4])           /* psndN: byte N of the sound kept in this pattern */
        *got = p->snd[atoi(what + 4) % SNDSZ];
    else if (!strcmp(what, "initt"))
        *got = ui.init_t != 0;
    else if (!strcmp(what, "nudge_all"))
        *got = ui.nudge_all;
    else if (!strcmp(what, "nudge"))
        *got = ui.nudge;
    else if (!strcmp(what, "hact"))
        *got = ui.hact;
    else if (!strncmp(what, "hidx", 4) && what[4])
        *got = ui.hidx[atoi(what + 4) % 6];
    else if (!strncmp(what, "syn", 3))
        *got = syn[atoi(what + 3) % NSYN];
    else if (!strncmp(what, "kick", 4) && what[4])
        *got = kick_p[atoi(what + 4) % NKICK];
    else if (!strncmp(what, "fx", 2) && what[2] && what[2] != 'e')
        *got = sd.fx[atoi(what + 2) % NFX];
    else if (!strncmp(what, "sc", 2) && what[2])
        *got = sd.sc[atoi(what + 2) % NSC];
    else if (!strncmp(what, "pre", 3) && what[3])
        *got = sd.pre[atoi(what + 3) % NPK];
    else if (!strncmp(what, "ppre", 4) && what[4])
        *got = p->pre[atoi(what + 4) % NPK];
    else if (!strncmp(what, "rnden", 5) && what[5])
        *got = sd.rnd_en[atoi(what + 5) % NRP];
    else if (!strncmp(what, "rndamt", 6) && what[6])
        *got = sd.rnd_amt[atoi(what + 6) % NRP][0];
    else if (!strncmp(what, "pj", 2) && what[2])
        *got = pj_used[atoi(what + 2) % NPROJ];
    else if (!strncmp(what, "uused", 5) && what[5])           /* uusedK: user slots used in kind K */
    {
        int k = atoi(what + 5) % NPK, c = 0;
        for (int i = 0; i < NUSER; i++)
            c += up[k].s[i].n[0] != 0;
        *got = c;
    }
    else if (!strncmp(what, "trig", 4) && what[4])
        *got = p->trig[atoi(what + 4) % NSTEP];
    else if (!strncmp(what, "pit", 3) && what[3])
        *got = p->pit[atoi(what + 3) % NSTEP];
    else if (!strncmp(what, "vel", 3) && what[3])
        *got = p->vel[atoi(what + 3) % NSTEP];
    else if (!strncmp(what, "gate", 4) && what[4])
        *got = p->gate[atoi(what + 4) % NSTEP];
    else if (!strncmp(what, "rat", 3) && what[3])
        *got = p->rat[atoi(what + 3) % NSTEP];
    else if (!strncmp(what, "lkv", 3) && what[3])
        *got = p->lk_v[atoi(what + 3) % NSTEP][0];
    else if (!strncmp(what, "lk", 2) && what[2])
        *got = p->lk_p[atoi(what + 2) % NSTEP][0];
    else if (!strncmp(what, "ktrig", 5) && what[5])
        *got = p->ktrig[atoi(what + 5) % NSTEP];
    else if (!strncmp(what, "kcond", 5) && what[5])
        *got = p->kcond[atoi(what + 5) % NSTEP];
    else if (!strncmp(what, "krat", 4) && what[4])
        *got = p->krat[atoi(what + 4) % NSTEP];
    else if (!strncmp(what, "kpit", 4) && what[4])           /* kpitN: the pitch of kick step N */
        *got = p->kpit[atoi(what + 4) % NSTEP];
    else if (!strncmp(what, "kvol", 4) && what[4])           /* kvolN: the volume of kick step N */
        *got = p->kvol[atoi(what + 4) % NSTEP];
    else if (!strcmp(what, "kpg"))                           /* KICK's page as the code names it: 3 and 4 the sequence, 0..2 the knobs */
        *got = (int)kpg();
    else if (!strncmp(what, "pused", 5) && what[5])
        *got = pat_used(&pats[atoi(what + 5) % NPAT]);
    else if (!strcmp(what, "song_on"))
        *got = sd.song_on;
    else if (!strncmp(what, "src", 3) && what[3])
        *got = sd.src[atoi(what + 3) % NPATCH];
    else if (!strncmp(what, "amt", 3) && what[3])
        *got = sd.amt[atoi(what + 3) % NPATCH];
    else
        return 0;
    return 1;
}
#endif
