/* SPDX-License-Identifier: MPL-2.0 */
/* FM-FAM: the sound engine (a two-oscillator voice, a drum synth, the effects). Float, 44.1 kHz. */
#pragma once
#include <stdint.h>

/* synth parameters, all 0..127 */
enum { P_V1FRQ, P_V1WAV, P_V1LVL, P_V1EG, P_V2FRQ, P_V2WAV, P_V2LVL, P_V2EG,
       P_FM, P_SYN, P_NSE, P_VDEC, P_CUT, P_RES, P_MODE, P_FEG,
       P_FDEC, P_ATK, P_ADEC, P_VOL, P_DRV, P_ACC, P_GLD, P_TUN, NSYN };
#define NWAVE 16                       /* oscillator types: the wave knob's 0..127 picks one, 8 values each */
/* kick / drum parameters */
enum { K_TUN, K_SWP, K_PDC, K_ADC, K_CLK, K_DRV, K_LVL, K_WAV, K_FMA, K_FMR, K_NSE, K_TON, NKICK };
/* effect parameters: compressor, stutter, delay, reverb, DJ filter, limiter */
enum { F_CMA, F_CMM, F_CMS, F_CMG,
       F_STS, F_STM, F_STD, F_STP,
       F_DLT, F_DLF, F_DLM, F_DLK,
       F_RVS, F_RVT, F_RVM, F_RVW,
       F_DJC, F_DJR, F_DJM, F_DJS,
       F_LIM, F_LMD, F_LMO, F_LMS, NFX };
/* the patch bay: 8 destinations, each with a source and an amount */
enum { D_VC1, D_VC2, D_FM, D_CUT, D_RES, D_DEC, D_NSE, D_WAV, NPATCH };
enum { S_OFF, S_SEQP, S_SEQV, S_VEG, S_FEG, S_AEG, S_NSE, S_KEG, NSRC };

/* which effects are on (the keys in FX mode) */
enum { FX_CMP = 1, FX_DLY = 2, FX_REV = 4, FX_STU = 8, FX_DJF = 16, FX_LIM = 32 };
extern uint8_t fx_en;
extern uint8_t syn_eff[NSYN];          /* what the voice plays: the sound with the step's locks applied */
extern uint8_t kick_p[NKICK];
extern uint8_t fx_p[NFX];
extern uint8_t patch_src[NPATCH];
extern uint8_t patch_amt[NPATCH];      /* 64 = nothing, 0 = -1, 127 = +1 */

void dsp_init(void);
void dsp_tempo(float step_samples);    /* one 16th, in samples (the delay and the stutter follow it) */
/* note: MIDI (fractional: the fine tune); gate: samples until the note is released (0: no gate) */
void dsp_syn_trig(float note, float vel, int accent, float seq_pitch, float gate);
void dsp_kick_trig(float vel, int accent);
/* the same with a step's own pitch (SEMIS, semitones from the sound's tune) and volume (GAIN 0..1) */
void dsp_kick_hit(float vel, int accent, float semis, float gain);
void dsp_stutter(int on);              /* holds the last slice (dsp_render does it by itself when fx_en has FX_STU) */
float dsp_wave(uint32_t type, float ph);   /* the oscillator's shape over one cycle, for the screen (-1..1) */
void dsp_render(int32_t *lr, uint32_t n, float gain);
void dsp_panic(void);
