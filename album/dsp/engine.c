/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album's sound engine (engine.h has the plan). Single precision throughout, no libm: fm1-base's fastmath.h, so the
 * host simulator computes the same samples as the FM-1. */
#include "engine.h"
#include "fastmath.h"
#include "plat.h"
#include "patches.h"

/* ------------------------------------------------------------------ tools --- */
#define SINE_N 1024u
static float sine_tab[SINE_N + 1u] PLAT_POOL;

static inline float sine01(float ph)     /* sin(2 pi ph), ph in [0, 1) */
{
    float x = ph * (float)SINE_N;
    int32_t i = (int32_t)x;
    float f = x - (float)i;
    i &= (int32_t)(SINE_N - 1u);
    return sine_tab[i] + (sine_tab[i + 1] - sine_tab[i]) * f;
}
static inline float wrap01(float ph) { return ph - fm_floorf(ph); }

static inline uint32_t rnd(uint32_t *s)  /* xorshift32 */
{
    uint32_t x = *s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static inline float rndf(uint32_t *s) { return (float)(rnd(s) >> 8) * (1.0f / 16777216.0f); }   /* 0..1 */

static inline float blep(float t, float dt)   /* polyBLEP: the step's correction near a wrap */
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

static inline float soft(float x)        /* tanh-like, for |x| < 3 */
{
    if (x > 3.0f)
        return 1.0f;
    if (x < -3.0f)
        return -1.0f;
    return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
}

static inline float coef_t(float seconds, float rate)   /* one-pole coefficient: 1/e in `seconds` at `rate` */
{
    if (seconds <= 0.0f)
        return 0.0f;
    return fm_expf(-1.0f / (seconds * rate));
}

/* ------------------------------------------------------------- the queue --- */
/* two single-producer queues (the main loop's, the render's MIDI), one consumer (eng_begin) */
enum { EV_ON, EV_OFF, EV_SUS, EV_PATCH, EV_PANIC };
typedef struct { uint8_t type, a, b, pad; } ev_t;
#define QN 64u
typedef struct { ev_t ev[QN]; volatile uint32_t w, r; } queue_t;
static queue_t q_ui, q_isr;

static void q_push(queue_t *q, uint8_t type, int a, int b)
{
    uint32_t w = q->w;
    if (w - q->r >= QN)
        return;                          /* full: dropped (64 events inside 5.8 ms) */
    q->ev[w % QN].type = type;
    q->ev[w % QN].a = (uint8_t)a;
    q->ev[w % QN].b = (uint8_t)b;
    __asm__ volatile("" ::: "memory");
    q->w = w + 1u;
}

void eng_note_on(int note, int vel) { q_push(&q_ui, EV_ON, note, vel); }
void eng_note_off(int note) { q_push(&q_ui, EV_OFF, note, 0); }
void eng_sustain(int on) { q_push(&q_ui, EV_SUS, on != 0, 0); }
void eng_patch(int p) { q_push(&q_ui, EV_PATCH, p, 0); }
void eng_panic(void) { q_push(&q_ui, EV_PANIC, 0, 0); }
void eng_note_on_isr(int note, int vel) { q_push(&q_isr, EV_ON, note, vel); }
void eng_note_off_isr(int note) { q_push(&q_isr, EV_OFF, note, 0); }
void eng_sustain_isr(int on) { q_push(&q_isr, EV_SUS, on != 0, 0); }
void eng_patch_isr(int p) { q_push(&q_isr, EV_PATCH, p, 0); }

static volatile float knob_in[NKNOB] = {0.5f, 0.5f, 0.5f, 0.5f};
void eng_knob(int k, float v)
{
    if (k >= 0 && k < NKNOB)
        knob_in[k] = fm_clampf(v, 0.0f, 1.0f);
}

/* ---------------------------------------------------------------- voices --- */
enum { ST_OFF, ST_ATT, ST_DEC, ST_REL };
typedef struct {
    int16_t note;                        /* -1: free */
    uint8_t gate, held, stage, quick;    /* quick: a fast release (a patch change, a steal) */
    uint32_t age;
    float vel, pitch, target;            /* pitch and its slide target, in semitones */
    float ph[3], drift[3], dgoal[3];
    float fm_ph[3], fm_env, sub_ph;
    float env, fenv;
    float s1l, s2l, s1r, s2r;            /* the two low-pass filters' states (left, right) */
    float hpl, hpr;
    float crk;                           /* the crackle's burst */
    float pan;
    uint32_t rng;
} voice_t;

static voice_t V[ENG_VOICES];
static uint32_t age_now, voice_cap = ENG_VOICES;
static uint8_t sustain_on;
static int8_t mono_stack[8];
static uint32_t mono_n;

/* the block's shared values: written by eng_begin, read by both cores */
static struct {
    const patch_t *p;
    uint32_t n, nsub;
    float arm[ENG_BLOCK_MAX / ENG_SUB + 1u];   /* cents, at each sub-block's start (and the block's end) */
    float knob[NKNOB];
    float cut, drift, breath;            /* cutoff (Hz) from Tone; drift (cents) from Glide; the breath now (octaves) */
    float att_inc, dec_c, rel_c, quick_c, fenv_c, fm_c, slide_c;
    float hp_c, vgain, crackle_p;
    float window, wet, ap_g, wet_lp;     /* the reverb: window (s), share, diffusion, its low-pass coefficient */
} B;

static float dry_a[2u * ENG_BLOCK_MAX] PLAT_POOL, dry_b[2u * ENG_BLOCK_MAX] PLAT_POOL;
static float dry_last[2u * ENG_BLOCK_MAX] PLAT_POOL;   /* the last block's mix: the reverb's input (core 1) */
static float wet_out[2u * ENG_BLOCK_MAX] PLAT_POOL;    /* the reverb's output for this block (core 1) */
static uint32_t n_last;

static int cur_patch;
static float level_now, arm_now;
static uint32_t voices_on;

int eng_patch_count(void) { return NPATCH; }
const patch_t *eng_patch_info(int p) { return &PATCHES[(p % NPATCH + NPATCH) % NPATCH]; }
float eng_level(void) { return level_now; }
float eng_arm(void) { return arm_now; }
uint32_t eng_voices_on(void) { return voices_on; }
int eng_voice_cap(int n)
{
    if (n >= 2 && n <= (int)ENG_VOICES)
        voice_cap = (uint32_t)n;
    return (int)voice_cap;
}

static void voice_start(voice_t *v, int note, float vel, int retrig)
{
    uint32_t o;
    v->note = (int16_t)note;
    v->vel = vel;
    v->gate = 1;
    v->held = 0;
    v->quick = 0;
    v->stage = ST_ATT;
    v->age = ++age_now;
    if (retrig || v->env < 0.0001f) {     /* a new voice: the oscillators start anywhere, as strings do */
        v->pitch = v->target = (float)note;
        for (o = 0; o < 3u; o++) {
            v->ph[o] = rndf(&v->rng);
            v->fm_ph[o] = 0.0f;
            v->drift[o] = (rndf(&v->rng) - 0.5f) * B.drift;
            v->dgoal[o] = v->drift[o];
        }
    } else {
        v->pitch = v->target = (float)note;
    }
    v->fenv = 1.0f;
    v->fm_env = 1.0f;
}

static void voice_release(voice_t *v)
{
    v->gate = 0;
    v->held = 0;
    if (v->stage != ST_OFF)
        v->stage = ST_REL;
}

static uint32_t half_load(uint32_t lo)   /* voices sounding in one core's half */
{
    uint32_t i, n = 0;
    for (i = lo; i < lo + ENG_HALF; i++)
        n += V[i].stage != ST_OFF;
    return n;
}

static voice_t *voice_for(int note)
{
    uint32_t i, on = 0, best = 0, best_age = 0xFFFFFFFFu, lo;
    for (i = 0; i < ENG_VOICES; i++)     /* the same note again: its own voice */
        if (V[i].stage != ST_OFF && V[i].note == note)
            return &V[i];
    for (i = 0; i < ENG_VOICES; i++)
        on += V[i].stage != ST_OFF;
    if (on < voice_cap) {                /* a free voice, on the core with less to do */
        lo = half_load(0) <= half_load(ENG_HALF) ? 0u : ENG_HALF;
        for (i = 0; i < ENG_VOICES; i++) {
            uint32_t k = (lo + i) % ENG_VOICES;
            if (V[k].stage == ST_OFF)
                return &V[k];
        }
    }
    for (i = 0; i < ENG_VOICES; i++)     /* else the oldest released, else the oldest */
        if (V[i].stage == ST_REL && V[i].age < best_age) {
            best = i;
            best_age = V[i].age;
        }
    if (best_age != 0xFFFFFFFFu)
        return &V[best];
    for (i = 0; i < ENG_VOICES; i++)
        if (V[i].stage != ST_OFF && V[i].age < best_age) {
            best = i;
            best_age = V[i].age;
        }
    return &V[best];
}

static void note_on(int note, int vel)
{
    float v = (float)vel / 127.0f;
    if (B.p->mono) {
        uint32_t i;
        voice_t *m = &V[0];
        for (i = 0; i < mono_n; i++)     /* the key to the top of the stack */
            if (mono_stack[i] == note)
                break;
        for (; i + 1u < mono_n; i++)
            mono_stack[i] = mono_stack[i + 1u];
        if (i < mono_n)
            mono_n--;
        if (mono_n == 8u) {
            for (i = 0; i < 7u; i++)
                mono_stack[i] = mono_stack[i + 1u];
            mono_n = 7u;
        }
        mono_stack[mono_n++] = (int8_t)note;
        if (m->gate && m->stage != ST_OFF) {   /* legato: slide there, no new attack */
            m->note = (int16_t)note;
            m->target = (float)note;
            m->vel = v;
            return;
        }
        voice_start(m, note, v, m->stage == ST_OFF);
        return;
    }
    {
        voice_t *t = voice_for(note);
        voice_start(t, note, v, t->stage == ST_OFF || t->note != note);
    }
}

static void note_off(int note)
{
    uint32_t i;
    if (B.p->mono) {
        voice_t *m = &V[0];
        for (i = 0; i < mono_n; i++)
            if (mono_stack[i] == note)
                break;
        if (i == mono_n)
            return;
        for (; i + 1u < mono_n; i++)
            mono_stack[i] = mono_stack[i + 1u];
        mono_n--;
        if (mono_n) {                    /* back to the key still held */
            m->note = mono_stack[mono_n - 1u];
            m->target = (float)m->note;
        } else if (sustain_on) {
            m->held = 1;
        } else {
            voice_release(m);
        }
        return;
    }
    for (i = 0; i < ENG_VOICES; i++)
        if (V[i].gate && V[i].note == note) {
            if (sustain_on)
                V[i].held = 1;
            else
                voice_release(&V[i]);
        }
}

static void set_sustain(int on)
{
    uint32_t i;
    sustain_on = (uint8_t)(on != 0);
    if (!sustain_on)
        for (i = 0; i < ENG_VOICES; i++)
            if (V[i].held)
                voice_release(&V[i]);
}

static void all_quick(void)              /* everything out within ~30 ms, reverb tails kept */
{
    uint32_t i;
    for (i = 0; i < ENG_VOICES; i++)
        if (V[i].stage != ST_OFF) {
            V[i].gate = 0;
            V[i].held = 0;
            V[i].stage = ST_REL;
            V[i].quick = 1;
        }
    mono_n = 0;
}

/* --------------------------------------------------------------- the arm --- */
/* the tremolo arm: one hand bending the whole chord down and letting it back up, at irregular times, with a little
 * shake on top. Shared by every voice, as a guitar's arm moves all its strings */
static struct {
    float pos, goal, left, shake_ph;
    uint32_t rng;
} arm;

static void arm_step(float dt, float rate)
{
    arm.left -= dt;
    if (arm.left <= 0.0f) {
        float r = rndf(&arm.rng);
        arm.goal = r < 0.22f ? -0.04f : -(0.25f + 0.75f * rndf(&arm.rng));   /* now and then, back up to pitch */
        arm.left = (0.22f + 0.9f * rndf(&arm.rng)) / (rate > 0.05f ? rate : 0.05f);
    }
    arm.pos += (arm.goal - arm.pos) * (1.0f - fm_expf(-dt / 0.13f));
    arm.shake_ph = wrap01(arm.shake_ph + dt * 4.7f);
}

/* ------------------------------------------------------------ the reverb --- */
/* A reverse reverb in the manner of the SPX90's reverse gate: the input read back at 32 delays spread over the
 * window, the later ones louder, so a note swells up and then stops at the window's end. Run at 22.05 kHz (the
 * mix decimated by 2) on a 16-bit line of 1.49 s, then four allpasses a side smear the taps into a wash. */
#define RV_LEN 32768u
#define RV_MASK (RV_LEN - 1u)
#define RV_FS (ENG_FS * 0.5f)
#define NTAP 32u
static int16_t rv_line[RV_LEN] PLAT_POOL;
static uint32_t rv_w;
static int32_t tap_l[NTAP], tap_r[NTAP];
static float gain_l[NTAP], gain_r[NTAP];
static float rv_win;                     /* the window now (s), easing to B.window */
static const float JIT_L[NTAP] = {0.11f, -0.27f, 0.33f, -0.05f, 0.21f, -0.38f, 0.02f, 0.29f, -0.17f, 0.36f, -0.31f,
                                  0.08f, -0.12f, 0.25f, -0.36f, 0.14f, 0.31f, -0.22f, 0.05f, -0.09f, 0.38f, -0.29f,
                                  0.17f, -0.02f, 0.27f, -0.34f, 0.09f, -0.19f, 0.35f, -0.07f, 0.22f, -0.25f};
static const float JIT_R[NTAP] = {-0.21f, 0.34f, -0.08f, 0.19f, -0.32f, 0.12f, 0.37f, -0.14f, 0.26f, -0.35f, 0.04f,
                                  0.31f, -0.24f, 0.07f, 0.29f, -0.38f, 0.15f, -0.03f, 0.33f, -0.18f, 0.01f, 0.24f,
                                  -0.3f, 0.36f, -0.11f, 0.18f, -0.26f, 0.32f, -0.06f, 0.13f, -0.37f, 0.28f};
#define AP_N 4u
static const uint16_t AP_LEN_L[AP_N] = {113, 199, 317, 421}, AP_LEN_R[AP_N] = {131, 211, 337, 449};
static float ap_buf[1050u + 1128u] PLAT_POOL;
static uint16_t ap_pos[2u * AP_N];
static float rv_lp_l, rv_lp_r, rv_hp_l, rv_hp_r, rv_prev_l, rv_prev_r, rv_dec;   /* rv_dec: the decimator's half */

static void taps_set(float window)
{
    uint32_t t;
    float n = window * RV_FS, norm = 0.0f;
    if (n > (float)(RV_LEN - 4u))
        n = (float)(RV_LEN - 4u);
    for (t = 0; t < NTAP; t++) {
        float x = ((float)t + 1.0f) / (float)NTAP, g = x * x;   /* louder as it goes: the swell */
        tap_l[t] = 1 + (int32_t)(n * ((float)t + 0.5f + JIT_L[t]) / (float)NTAP);
        tap_r[t] = 1 + (int32_t)(n * ((float)t + 0.5f + JIT_R[t]) / (float)NTAP);
        gain_l[t] = (t & 1u) ? -g : g;
        gain_r[t] = (t % 3u == 1u) ? -g : g;
        norm += g * g;
    }
    norm = 1.0f / fm_sqrtf(norm);
    for (t = 0; t < NTAP; t++) {
        gain_l[t] *= norm;
        gain_r[t] *= norm;
    }
}

static inline float allpass(float *buf, uint16_t *pos, uint32_t len, float x, float g)
{
    float d = buf[*pos], y = d - g * x, in = x + g * y;
    buf[*pos] = (in > 1e-12f || in < -1e-12f) ? in : 0.0f;   /* (silence decays to zero, not to denormals) */
    if (++*pos >= len)
        *pos = 0;
    return y;
}

static void reverb(const float *in, float *out, uint32_t n)   /* n frames at 44.1 kHz, stereo interleaved */
{
    uint32_t i, t;
    const float g = B.ap_g, lp = B.wet_lp;
    rv_win += (B.window - rv_win) * 0.06f;   /* the window eases (a Swell turn sweeps, not jumps) */
    taps_set(rv_win);
    for (i = 0; i + 1u < n; i += 2u) {
        float x = 0.25f * (in[2u * i] + in[2u * i + 1u] + in[2u * i + 2u] + in[2u * i + 3u]);
        float yl = 0.0f, yr = 0.0f;
        int32_t q;
        uint32_t k, base = 0;
        x = 0.5f * (x + rv_dec);         /* a gentle low-pass before halving the rate */
        rv_dec = x;
        q = (int32_t)(x * 16384.0f);
        rv_line[rv_w] = (int16_t)(q > 32767 ? 32767 : q < -32768 ? -32768 : q);
        for (t = 0; t < NTAP; t++) {
            yl += gain_l[t] * (float)rv_line[(rv_w - (uint32_t)tap_l[t]) & RV_MASK];
            yr += gain_r[t] * (float)rv_line[(rv_w - (uint32_t)tap_r[t]) & RV_MASK];
        }
        rv_w = (rv_w + 1u) & RV_MASK;
        yl *= 1.0f / 16384.0f;
        yr *= 1.0f / 16384.0f;
        for (k = 0; k < AP_N; k++) {
            yl = allpass(ap_buf + base, &ap_pos[k], AP_LEN_L[k], yl, g);
            base += AP_LEN_L[k];
        }
        for (k = 0; k < AP_N; k++) {
            yr = allpass(ap_buf + base, &ap_pos[AP_N + k], AP_LEN_R[k], yr, g);
            base += AP_LEN_R[k];
        }
        if (fm_fabsf(rv_lp_l) < 1e-12f)  /* (no denormals in the silence after) */
            rv_lp_l = rv_hp_l = 0.0f;
        if (fm_fabsf(rv_lp_r) < 1e-12f)
            rv_lp_r = rv_hp_r = 0.0f;
        rv_lp_l += lp * (yl - rv_lp_l);  /* Tone darkens the wash too */
        rv_lp_r += lp * (yr - rv_lp_r);
        rv_hp_l += 0.011f * (rv_lp_l - rv_hp_l);   /* and no rumble below ~40 Hz */
        rv_hp_r += 0.011f * (rv_lp_r - rv_hp_r);
        yl = rv_lp_l - rv_hp_l;
        yr = rv_lp_r - rv_hp_r;
        out[2u * i] = 0.5f * (rv_prev_l + yl);   /* back to 44.1 kHz: halfway, then the sample */
        out[2u * i + 1u] = 0.5f * (rv_prev_r + yr);
        out[2u * i + 2u] = yl;
        out[2u * i + 3u] = yr;
        rv_prev_l = yl;
        rv_prev_r = yr;
    }
}

/* ---------------------------------------------------------------- render --- */
static const float SPREAD3[3] = {-0.5f, 0.5f, 0.0f};

static void render_voice(voice_t *v, float *o, uint32_t n)
{
    const patch_t *p = B.p;
    uint32_t j, i, oscs = p->oscs ? p->oscs : 1u;
    const float vgain = B.vgain * (0.35f + 0.65f * v->vel);
    const float pl = 0.5f - 0.3f * v->pan, pr = 0.5f + 0.3f * v->pan;
    for (j = 0; j * ENG_SUB < n; j++) {
        float dt[3], sub_dt, cut, g, k, a1, a2, a3, armc = B.arm[j];
        uint32_t o2;
        if (p->mono)                     /* the slide between notes */
            v->pitch = v->target + (v->pitch - v->target) * B.slide_c;
        for (o2 = 0; o2 < oscs; o2++) {
            float cents, semis, f;
            if (rnd(&v->rng) < 0x00300000u)   /* each string wanders on its own: somewhere new every ~0.5 s */
                v->dgoal[o2] = (rndf(&v->rng) - 0.5f) * 2.0f * B.drift;
            v->drift[o2] += (v->dgoal[o2] - v->drift[o2]) * 0.004f;   /* ~90 ms to get there */
            cents = armc + v->drift[o2] + (oscs > 1u ? SPREAD3[o2] * p->detune : 0.0f);
            semis = v->pitch + p->octave + cents * 0.01f;
            f = 440.0f * fm_exp2f((semis - 69.0f) * (1.0f / 12.0f));
            dt[o2] = fm_minf(f * (1.0f / ENG_FS), 0.45f);
        }
        sub_dt = dt[0] * 0.5f;
        /* the filter for these 16 samples: Tone, the envelope, the breath, a little key tracking */
        cut = B.cut * fm_exp2f(p->fenv * v->fenv + B.breath + (v->pitch - 60.0f) * (0.3f / 12.0f));
        cut = fm_clampf(cut, 30.0f, 0.42f * ENG_FS);
        g = fm_tanf(FM_PI * cut * (1.0f / ENG_FS));
        k = 2.0f - 1.9f * p->res;
        a1 = 1.0f / (1.0f + g * (g + k));
        a2 = g * a1;
        a3 = g * a2;
        for (i = 0; i < ENG_SUB && j * ENG_SUB + i < n; i++) {
            float sl = 0.0f, sr = 0.0f, s, e, yl, yr, v1, v2, v3;
            /* the level */
            switch (v->stage) {
            case ST_ATT:
                v->env += B.att_inc;
                if (v->env >= 1.0f) {
                    v->env = 1.0f;
                    v->stage = ST_DEC;
                }
                break;
            case ST_DEC:
                v->env = p->sus + (v->env - p->sus) * B.dec_c;
                if (p->sus <= 0.0f && v->env < 0.0001f)
                    v->stage = ST_REL;
                break;
            case ST_REL:
                v->env *= v->quick ? B.quick_c : B.rel_c;
                break;
            default:
                break;
            }
            e = v->env;
            /* the oscillators */
            for (o2 = 0; o2 < oscs; o2++) {
                float ph = v->ph[o2], d = dt[o2];
                switch (p->wave) {
                case WAVE_SAW:
                    s = 2.0f * ph - 1.0f - blep(ph, d);
                    break;
                case WAVE_PULSE: {
                    float ph2 = ph + 1.0f - p->pw;
                    if (ph2 >= 1.0f)
                        ph2 -= 1.0f;
                    s = (ph < p->pw ? 1.0f : -1.0f) + blep(ph, d) - blep(ph2, d);
                    break;
                }
                case WAVE_TRI:
                    s = 1.0f - 4.0f * fm_fabsf(ph - 0.5f);
                    break;
                case WAVE_FM: {
                    float m = sine01(v->fm_ph[o2]) * p->fm_index * v->fm_env * (1.0f / FM_TWO_PI);
                    s = sine01(wrap01(ph + m));
                    v->fm_ph[o2] = wrap01(v->fm_ph[o2] + d * p->fm_ratio);
                    break;
                }
                default:
                    s = sine01(ph);
                    break;
                }
                ph += d;
                if (ph >= 1.0f)
                    ph -= 1.0f;
                v->ph[o2] = ph;
                if (oscs == 1u) {
                    sl += s;
                    sr += s;
                } else if (o2 == 0u) {
                    sl += s;
                } else if (o2 == 1u) {
                    sr += s;
                } else {
                    sl += 0.6f * s;
                    sr += 0.6f * s;
                }
            }
            if (oscs == 3u) {
                sl *= 0.625f;
                sr *= 0.625f;
            }
            if (p->noise > 0.0f) {       /* air, a little different on each side */
                sl += (rndf(&v->rng) - 0.5f) * 2.0f * p->noise;
                sr += (rndf(&v->rng) - 0.5f) * 2.0f * p->noise;
            }
            if (p->sub > 0.0f) {
                float u = sine01(v->sub_ph) * p->sub;
                v->sub_ph += sub_dt;
                if (v->sub_ph >= 1.0f)
                    v->sub_ph -= 1.0f;
                sl += u;
                sr += u;
            }
            /* the two low-pass filters (state variable, trapezoidal) */
            v3 = sl - v->s2l;
            v1 = a1 * v->s1l + a2 * v3;
            v2 = v->s2l + a2 * v->s1l + a3 * v3;
            v->s1l = 2.0f * v1 - v->s1l;
            v->s2l = 2.0f * v2 - v->s2l;
            yl = v2;
            v3 = sr - v->s2r;
            v1 = a1 * v->s1r + a2 * v3;
            v2 = v->s2r + a2 * v->s1r + a3 * v3;
            v->s1r = 2.0f * v1 - v->s1r;
            v->s2r = 2.0f * v2 - v->s2r;
            yr = v2;
            if (p->hp > 0.0f) {
                v->hpl += B.hp_c * (yl - v->hpl);
                v->hpr += B.hp_c * (yr - v->hpr);
                yl -= v->hpl;
                yr -= v->hpr;
            }
            if (p->crackle > 0.0f) {     /* the fire: a click now and then, bright, under the voice's level */
                if (rndf(&v->rng) < B.crackle_p)
                    v->crk = (rndf(&v->rng) - 0.5f) * 3.0f * p->crackle;
                yl += v->crk * (0.6f + 0.4f * rndf(&v->rng));
                yr += v->crk * (0.6f + 0.4f * rndf(&v->rng));
                v->crk *= 0.9f;
                if (v->crk < 1e-6f && v->crk > -1e-6f)
                    v->crk = 0.0f;       /* (no denormals: they are slow) */
            }
            e *= vgain;
            o[2u * (j * ENG_SUB + i)] += yl * e * pl * 2.0f;
            o[2u * (j * ENG_SUB + i) + 1u] += yr * e * pr * 2.0f;
        }
        v->fenv = v->fenv > 1e-6f ? v->fenv * B.fenv_c : 0.0f;
        v->fm_env = v->fm_env > 1e-6f ? v->fm_env * B.fm_c : 0.0f;
        if (v->stage == ST_REL && v->env < 0.00005f) {   /* done: free, and quiet for the next note */
            v->stage = ST_OFF;
            v->env = 0.0f;
            v->note = -1;
            v->s1l = v->s2l = v->s1r = v->s2r = v->hpl = v->hpr = v->crk = 0.0f;
            break;
        }
    }
}

static void render_half(uint32_t lo, float *o)
{
    uint32_t i;
    for (i = 0; i < 2u * B.n; i++)
        o[i] = 0.0f;
    for (i = lo; i < lo + ENG_HALF; i++)
        if (V[i].stage != ST_OFF)
            render_voice(&V[i], o, B.n);
}

/* --------------------------------------------------------- the four steps --- */
static void patch_apply(int p)
{
    cur_patch = (p % NPATCH + NPATCH) % NPATCH;
    all_quick();
}

static void drain(queue_t *q)
{
    while (q->r != q->w) {
        ev_t e = q->ev[q->r % QN];
        __asm__ volatile("" ::: "memory");
        q->r = q->r + 1u;
        switch (e.type) {
        case EV_ON:
            note_on(e.a, e.b ? e.b : 1);
            break;
        case EV_OFF:
            note_off(e.a);
            break;
        case EV_SUS:
            set_sustain(e.a);
            break;
        case EV_PATCH:
            patch_apply(e.a);
            B.p = &PATCHES[cur_patch];
            break;
        case EV_PANIC:
            all_quick();
            set_sustain(0);
            break;
        default:
            break;
        }
    }
}

void eng_begin(uint32_t n)
{
    const patch_t *p;
    uint32_t j, k;
    float glide, tone, sub_dt = (float)ENG_SUB / ENG_FS, hz;
    if (n > ENG_BLOCK_MAX)
        n = ENG_BLOCK_MAX;
    B.p = &PATCHES[cur_patch];
    for (k = 0; k < NKNOB; k++)
        B.knob[k] = knob_in[k];
    glide = B.knob[K_GLIDE];
    B.drift = B.p->drift_cents * glide;
    drain(&q_ui);
    drain(&q_isr);
    p = B.p = &PATCHES[cur_patch];
    B.n = n;
    B.nsub = (n + ENG_SUB - 1u) / ENG_SUB;
    tone = B.knob[K_TONE];
    B.cut = p->cut_lo * fm_exp2f(fm_log2f(p->cut_hi / p->cut_lo) * tone);
    B.att_inc = 1.0f / (fm_maxf(p->att, 0.001f) * ENG_FS);
    B.dec_c = coef_t(p->dec * 0.35f, ENG_FS);
    B.rel_c = coef_t(p->rel * 0.3f, ENG_FS);
    B.quick_c = coef_t(0.012f, ENG_FS);
    B.fenv_c = coef_t(p->fenv_decay, ENG_FS / (float)ENG_SUB);
    B.fm_c = coef_t(p->fm_decay > 0.0f ? p->fm_decay : 1.0f, ENG_FS / (float)ENG_SUB);
    B.slide_c = coef_t(p->slide > 0.0f ? p->slide * 0.4f : 0.0f, ENG_FS / (float)ENG_SUB);
    B.hp_c = p->hp > 0.0f ? 1.0f - fm_expf(-FM_TWO_PI * p->hp / ENG_FS) : 0.0f;
    B.vgain = 0.24f * p->level;
    B.crackle_p = p->crackle * 10.0f / ENG_FS;
    {
        static float breath_ph;
        breath_ph = wrap01(breath_ph + (float)n * (0.13f / ENG_FS));
        B.breath = p->breath * sine01(breath_ph);
    }
    /* the reverb's settings: Swell its window, Blur its share and smear, Tone its colour */
    B.window = p->swell_lo * fm_exp2f(fm_log2f(p->swell_hi / p->swell_lo) * B.knob[K_SWELL]);
    B.wet = p->wet_lo + (p->wet_hi - p->wet_lo) * B.knob[K_BLUR];
    B.ap_g = 0.3f + 0.42f * B.knob[K_BLUR] * p->diffuse;
    hz = 1400.0f * fm_exp2f(2.6f * tone);
    B.wet_lp = 1.0f - fm_expf(-FM_TWO_PI * hz / RV_FS);
    /* the arm's curve for this block */
    {
        float reach = p->arm_cents * fm_powf(glide > 0.0001f ? glide : 0.0001f, 1.2f);
        for (j = 0; j <= B.nsub; j++) {
            float shake = 0.06f * sine01(arm.shake_ph);
            B.arm[j] = reach * (arm.pos + shake * (0.4f + 0.6f * glide));
            if (j < B.nsub)
                arm_step(sub_dt, p->arm_rate);
        }
        arm_now = reach > 0.5f ? fm_clampf(B.arm[0] / p->arm_cents, -1.0f, 0.0f) : 0.0f;
    }
    for (k = 0, j = 0; k < ENG_VOICES; k++)
        j += V[k].stage != ST_OFF;
    voices_on = j;
}

void eng_voices_a(void) { render_half(0, dry_a); }

void eng_voices_b(void)
{
    render_half(ENG_HALF, dry_b);
    reverb(dry_last, wet_out, n_last ? n_last : B.n);
}

void eng_drop_b(void)
{
    uint32_t i;
    for (i = 0; i < 2u * B.n; i++)
        dry_b[i] = wet_out[i] = 0.0f;
    for (i = ENG_HALF; i < ENG_VOICES; i++) {   /* their state may be half advanced, or misread: start again */
        V[i].stage = ST_OFF;
        V[i].env = 0.0f;
        V[i].note = -1;
        V[i].s1l = V[i].s2l = V[i].s1r = V[i].s2r = V[i].hpl = V[i].hpr = V[i].crk = 0.0f;
    }
}

static inline int finite(float x) { return x == x && x < 1e6f && x > -1e6f; }

int eng_b_sane(void)
{
    uint32_t i;
    for (i = 0; i < 2u * B.n; i += 7u)
        if (!finite(dry_b[i]) || !finite(wet_out[i]))
            return 0;
    for (i = ENG_HALF; i < ENG_VOICES; i++)
        if (!finite(V[i].env) || !finite(V[i].s2l) || !finite(V[i].s2r))
            return 0;
    return finite(rv_lp_l) && finite(rv_lp_r);
}

/* tremolo, wow and saturation: core 0, after the join */
static float trem_l_ph, trem_r_ph, wow_ph, wow_flut;
#define WOW_N 512u
static float wow_buf[2u * WOW_N] PLAT_POOL;
static uint32_t wow_w;
static uint32_t wow_rng = 0x9E3779B9u;

void eng_end(int32_t *lr, uint32_t n, float gain)
{
    const patch_t *p = B.p;
    uint32_t i;
    const float wet = B.wet, dry = 1.0f - wet, wet_gain = 1.6f;
    const float tl = p->trem_l / ENG_FS, tr = p->trem_r / ENG_FS, td = p->trem_depth;
    const float tape = p->tape, drive = 1.0f + 2.2f * tape, make_up = (1.0f + 0.4f * tape) / drive;
    const float out_gain = gain * 8000000.0f;
    float peak = 0.0f;
    if (n > B.n)
        n = B.n;
    wow_flut += ((rndf(&wow_rng) - 0.5f) - wow_flut) * 0.05f;
    for (i = 0; i < n; i++) {
        float l = dry_a[2u * i] + dry_b[2u * i], r = dry_a[2u * i + 1u] + dry_b[2u * i + 1u];
        float ol, or_, d, fr, w0l, w0r, w1l, w1r;
        uint32_t di, a0, a1;
        if (td > 0.0f) {                 /* two amps, two speeds */
            float ml = sine01(trem_l_ph), mr = sine01(trem_r_ph);
            if (p->trem_square) {        /* a chop: the sine pushed hard into a soft square */
                ml = soft(ml * 6.0f);
                mr = soft(mr * 6.0f);
            }
            l *= 1.0f - td * 0.5f * (1.0f + ml);
            r *= 1.0f - td * 0.5f * (1.0f + mr);
            trem_l_ph = wrap01(trem_l_ph + tl);
            trem_r_ph = wrap01(trem_r_ph + tr);
        }
        dry_last[2u * i] = l;            /* the reverb hears the tremolo too */
        dry_last[2u * i + 1u] = r;
        ol = l * dry + wet_out[2u * i] * wet * wet_gain;
        or_ = r * dry + wet_out[2u * i + 1u] * wet * wet_gain;
        /* tape: a slow wow and a little flutter on a short delay, then saturation */
        wow_buf[2u * wow_w] = ol;
        wow_buf[2u * wow_w + 1u] = or_;
        d = 44.0f + tape * (26.0f * sine01(wow_ph) + 9.0f * wow_flut);
        wow_ph = wrap01(wow_ph + 0.55f / ENG_FS);
        di = (uint32_t)d;
        fr = d - (float)di;
        a0 = (wow_w + WOW_N - di) & (WOW_N - 1u);
        a1 = (a0 + WOW_N - 1u) & (WOW_N - 1u);
        w0l = wow_buf[2u * a0];
        w0r = wow_buf[2u * a0 + 1u];
        w1l = wow_buf[2u * a1];
        w1r = wow_buf[2u * a1 + 1u];
        wow_w = (wow_w + 1u) & (WOW_N - 1u);
        ol = w0l + (w1l - w0l) * fr;
        or_ = w0r + (w1r - w0r) * fr;
        ol = soft(ol * drive) * make_up;
        or_ = soft(or_ * drive) * make_up;
        ol = fm_clampf(ol, -1.0f, 1.0f);
        or_ = fm_clampf(or_, -1.0f, 1.0f);
        peak = fm_maxf(peak, fm_maxf(fm_fabsf(ol), fm_fabsf(or_)));
        lr[2u * i] = (int32_t)(ol * out_gain);
        lr[2u * i + 1u] = (int32_t)(or_ * out_gain);
    }
    n_last = n;
    level_now = peak > level_now ? peak : level_now * 0.96f + peak * 0.04f;
}

void eng_init(void)
{
    uint32_t i;
    for (i = 0; i <= SINE_N; i++)
        sine_tab[i] = fm_sinf((float)i * (FM_TWO_PI / (float)SINE_N));
    for (i = 0; i < ENG_VOICES; i++) {
        V[i].note = -1;
        V[i].stage = ST_OFF;
        V[i].rng = 74565u + 7919u * i;
        V[i].pan = (float)((int)(i % 3u) - 1) * 0.6f;
    }
    arm.rng = 0xA5A5F00Du;
    arm.pos = arm.goal = -0.1f;
    cur_patch = 0;
    B.p = &PATCHES[0];
    B.n = ENG_BLOCK_MAX;
    rv_win = PATCHES[0].swell_lo;
    taps_set(rv_win);
}
