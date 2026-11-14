/* SPDX-License-Identifier: MPL-2.0 */
/* FM-FAM's own exports for its browser pages (app.h APP_WEB): fm1-base's web/emu/emu_web.c takes this in after the
 * whole app (host/plat_host.c, app.c), so it reaches the app's own state. Render Audio (fam/web/emu/render.js, on the
 * player and the mirror page) runs a copy of the app in a worker (render_worker.js): it chooses a pattern, what is
 * heard, and how many 16ths, then asks for audio (web_render) until the sequencer stops by itself. Never on the FM-1. */

#define WEB_EXPORT __attribute__((used, visibility("default")))

WEB_EXPORT int fam_npat(void) { return NPAT; }
WEB_EXPORT int fam_pat_cur(void) { return sq.cur; }
WEB_EXPORT int fam_bpm(void) { return sd.bpm; }
WEB_EXPORT float fam_step_len(void) { return sq.len; }          /* one 16th, in samples (44.1 kHz) */
WEB_EXPORT int fam_running(void) { return sq.running; }

/* 1: pattern pi has a step on, the synth's or the kick's */
WEB_EXPORT int fam_pat_used(int pi)
{
    int s;
    if (pi < 0 || pi >= NPAT)
        return 0;
    for (s = 0; s < NSTEP; s++)
        if ((pats[pi].trig[s] && pats[pi].vel[s]) || pats[pi].ktrig[s])
            return 1;
    return 0;
}

/* a render from the first step: pattern pi (-1 the one playing), what 0 all, 1 the synth's sequence only, 2 the kick
 * only, for steps 16ths. The sound starts from silence (the effects too) on the app's own tempo, the pattern alone
 * (no song, no clock from outside); what was already made and not yet asked for is dropped */
WEB_EXPORT int fam_render_begin(int what, int pi, int steps)
{
    seq_stop();
    xs.on = 0;
    xs.live = 0;
    sd.song_on = 0;
    sq.queued = -1;
    if (pi >= 0 && pi < NPAT && pi != sq.cur) {    /* as when the sequencer moves to another pattern */
        snd_sync_to(sq.cur);
        sq.cur = (uint8_t)pi;
        snd_pull(pi);
    }
    sq.mute_k = (uint8_t)(what == 1);
    sq.mute_s = (uint8_t)(what == 2);
    dsp_init();
    publish();
    tempo_set();
    sq.dur = sq.len;
    seq_start();
    web_steps = steps > 0 ? steps : -1;
    ring_r = ring_w;
    return sq.cur;
}
