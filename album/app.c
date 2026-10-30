/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album 01: a few deep sounds and the photo each one came from. The keys play; PRESET picks the patch (and its
 * photo); the four knobs are Glide, Swell, Tone and Blur on every patch, and each moves the photo as it moves the
 * sound. ALGORITHM is a fifth knob, FUZZ, and FX takes the fuzz in and out. ARP turns chord mode on (chords.h) and
 * SELECT picks its key (or hold ARP and press a key); SEQ starts the patch's beat, and with SEQ held ALGORITHM sets
 * its tempo and SELECT its level. PLAY holds the notes; HOME puts the knobs and
 * the tempo back where the patch starts; SAVE keeps everything; REC links to another FM-1 (or any Bluetooth MIDI
 * device), held a second it turns Bluetooth off or on.
 *
 * Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c (the canvas, the
 * fonts), announce.c (the screen reader) and lib/btdev.c are in scope. The sound is dsp/engine.c, compiled apart. */
#include "fastmath.h"
#include "engine.h"
#include "photos.h"

#ifndef PLAT_HOST
#include "cpu1/supply.h"
#include "cpu1/cpu1.c"
#else
static uint8_t c1_on;

static int c1_kick(void) { return 0; }
static int c1_join(void) { return 1; }
static int c1_start(void) { return 0; }
static int c1_report(char *a, char *b) { (void)a; (void)b; return 0; }
#endif

static const char *const KNOB_NAME[NKNOB] = {"GLIDE", "SWELL", "TONE", "BLUR", "FUZZ", "FUZZ LOW", "FUZZ MID", "FUZZ HIGH"};
/* each knob's MIDI CC, in and out (21 is the beat's level) */
static const uint8_t KNOB_CC[NKNOB] = {16, 17, 18, 19, 20, 22, 23, 24};
static float knob_start(const patch_t *p, uint32_t k) { return k >= K_FZLO ? 0.5f : p->knob[k]; }   /* (the EQ: flat) */
static inline float sine_wave(float x) { return fm_sinf(x * FM_TWO_PI); }
#define MAX_PATCH 16u

/* --------------------------------------------------------------- settings --- */
enum { MODE_NOTES, MODE_CHORDS, MODE_ACCORDION };   /* set.chords: how the keys play (ARP) */
#define SET_MAGIC 0x414C4238u            /* "ALB8": the MIDI channel (earlier layouts start fresh) */
typedef struct {
    uint32_t magic;
    uint8_t patch;
    int8_t octave;                       /* -2 .. +2 */
    uint8_t ble_off;                     /* Bluetooth switched off (REC held) */
    uint8_t one_core;                    /* the second core switched off by hand (OCT- held at power-on) */
    uint8_t knob[MAX_PATCH][NKNOB];      /* each patch's knobs, 0..255 */
    uint8_t key;                         /* chord mode's key, 0 (C) .. 11 */
    uint8_t chords;                      /* chord mode on */
    uint8_t beat_level;                  /* 0..100 */
    uint8_t fuzz_off;                    /* FX: the fuzz bypassed */
    uint8_t beat_var;                    /* LFO: 0 sparse, 1 as written, 2 busy */
    uint8_t bass;                        /* GLO: the auto bass on */
    uint8_t two_cores;                   /* OCT+ held at power-on: the second core tried (until then, one) */
    uint8_t midi_ch;                     /* 0: every channel in, 1 out; 1..16: that one only, in and out */
    int8_t tempo[MAX_PATCH];             /* each patch's tempo, BPM from its beat's own */
} settings_t;
static settings_t set;
static uint8_t set_dirty;

int album_ble_off(void) { return set.ble_off; }

static void settings_defaults(void)
{
    uint32_t p, k;
    set.magic = SET_MAGIC;
    set.patch = 0;
    set.octave = 0;
    set.ble_off = 0;
    set.one_core = 0;
    set.key = 0;
    set.chords = 1;                      /* chord mode from the start */
    set.beat_level = 50;                 /* (100: twice as loud) */
    set.fuzz_off = 0;
    set.beat_var = 1;
    set.bass = 0;
    set.two_cores = 0;
    set.midi_ch = 0;
    for (p = 0; p < MAX_PATCH; p++)
        set.tempo[p] = 0;
    for (p = 0; p < MAX_PATCH; p++)
        for (k = 0; k < NKNOB; k++)
            set.knob[p][k] = (uint8_t)(p < (uint32_t)eng_patch_count()
                                           ? (int)(knob_start(eng_patch_info((int)p), k) * 255.0f + 0.5f) : 128);
}

static float knob_val(uint32_t k) { return (float)set.knob[set.patch][k] * (1.0f / 255.0f); }

static void knobs_to_engine(void)
{
    uint32_t k;
    for (k = 0; k < NKNOB; k++)
        eng_knob((int)k, knob_val(k));
}

static int tempo_now(void)               /* this patch's tempo, BPM */
{
    const beat_t *b = eng_patch_info(set.patch)->beat;
    return (b ? (int)b->bpm : 90) + set.tempo[set.patch];
}

static void beat_to_engine(void)
{
    eng_tempo((float)tempo_now());
    eng_beat_level((float)set.beat_level * 0.014f);   /* 50 the kit as each beat is written, 100 about 6 dB more */
}

/* ----------------------------------------------------------------- render --- */
/* MIDI in, from the render: notes straight to the engine; knobs, patches and panic to the main loop, which shows
 * them (and does not send them back out: two linked FM-1s would echo forever) */
static volatile int32_t midi_patch_req = -1, midi_beat_req = -1;   /* (CC 21: the beat's level) */
#define CC_BEAT 21u
static volatile uint8_t midi_knob_req[NKNOB], midi_knob_mask, midi_panic_req, midi_hold_req;
static volatile uint32_t midi_count;

static volatile uint8_t midi_rx_ch;     /* set.midi_ch, for the render's MIDI in */
static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
        if (st < 0xF0u && midi_rx_ch && (st & 0x0Fu) != midi_rx_ch - 1u)
            continue;                    /* another channel's */
        if (st == 0xF8u || st == 0xFAu || st == 0xFBu || st == 0xFCu) {
            eng_clock_in((int)st);       /* a linked FM-1's beat (or a computer's): its clock, its start and stop */
            continue;
        }
        if (st >= 0xF0u)
            continue;
        midi_count++;
        switch (st & 0xF0u) {
        case 0x90u:
            if (d2)
                eng_note_on_isr((int)d1, (int)d2);
            else
                eng_note_off_isr((int)d1);   /* velocity 0 is a note off */
            break;
        case 0x80u:
            eng_note_off_isr((int)d1);
            break;
        case 0xB0u:
            if (d1 == 64u) {
                eng_sustain_isr(d2 >= 64u);
                midi_hold_req = (uint8_t)(d2 >= 64u ? 2u : 1u);   /* the screen shows it (2 on, 1 off) */
            }
            else if (d1 >= 16u && d1 <= 24u && d1 != CC_BEAT) {
                uint32_t k = 0;
                while (k < NKNOB && KNOB_CC[k] != d1)
                    k++;
                eng_knob((int)k, (float)d2 / 127.0f);
                midi_knob_req[k] = (uint8_t)d2;
                midi_knob_mask |= (uint8_t)(1u << k);
            } else if (d1 == CC_BEAT) {
                midi_beat_req = (int32_t)d2;
            } else if (d1 == 123u || d1 == 120u)
                midi_panic_req = 1;
            break;
        case 0xC0u:
            midi_patch_req = (int32_t)d1;
            break;
        default:
            break;
        }
    }
}

static float gain_now;

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    float gain = (float)master_q12 / 4096.0f;
    midi_in();
    gain_now += (gain - gain_now) * 0.5f;    /* MASTER eases over a few blocks: no step */
    eng_begin(n);
    if (c1_kick()) {                     /* core 1 takes its half while core 0 does this one */
        eng_voices_a();
        c1_join();
    } else {
        eng_voices_a();
        eng_voices_b();
    }
    eng_end(lr, n, gain_now);
}

void app_panic(void) { eng_panic(); }

/* the render near its limit: one voice fewer, down to six. They come back once the load has eased (app_frame), so
 * one heavy moment (a save, a burst of MIDI) does not leave the rest of the session short of voices */
static uint32_t shed_top, calm_since;
void app_overload(void)
{
    int cap = eng_voice_cap(0);
    if (!shed_top)
        shed_top = (uint32_t)cap;
    if (cap > 6)
        eng_voice_cap(cap - 1);
    calm_since = plat_ms();
}

static void voices_back(uint32_t now)    /* a shed voice back after each two calm seconds */
{
    int cap, top = (int)shed_top;
    if (!shed_top)
        return;
#ifndef PLAT_HOST
    if (!c1_on && top > 8)               /* (one core since: eight is its whole) */
        top = 8;
#endif
    cap = eng_voice_cap(0);
    if (cap >= top)
        shed_top = 0;
    else if (plat_cpu_pct() >= 60u)
        calm_since = now;
    else if (now - calm_since >= 2000u) {
        eng_voice_cap(cap + 1);
        calm_since = now;
    }
}

void app_init(void)
{
    eng_init();
    if (plat_store_load(OBJ_SETTINGS, &set, sizeof set) != (int)sizeof set || set.magic != SET_MAGIC
        || set.octave < -2 || set.octave > 2 || set.ble_off > 1u || set.one_core > 1u || set.key > 11u
        || set.chords > 2u || set.beat_level > 100u || set.fuzz_off > 1u || set.beat_var > 2u || set.bass > 1u
        || set.two_cores > 1u || set.midi_ch > 16u)
        settings_defaults();
    if (set.patch >= (uint32_t)eng_patch_count())
        set.patch = 0;
    midi_rx_ch = set.midi_ch;
    eng_patch(set.patch);
    knobs_to_engine();
    beat_to_engine();
    eng_fuzz_on(!set.fuzz_off);
    eng_beat_var(set.beat_var);
    eng_bass(set.bass);
#ifndef PLAT_HOST
    /* one core unless asked: the second core's start hung a cold boot on hardware (a crash, then a restart on one
     * core). OCT+ held through the boot screen tries two cores and remembers it; OCT- held puts it back to one
     * (cores_hold, app_frame: the panel is not read yet this early) */
    if (set.two_cores && c1_bar.magic == C1_BAR_MAGIC && c1_bar.barred
        && (c1_bar.why == C1_WHY_CRASH || c1_bar.why == C1_WHY_NOSTART)) {
        set.two_cores = 0;               /* it crashed (or would not start) with two: one from now on, kept */
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
        c1_said_why = (uint8_t)c1_bar.why;   /* (the boot screen says why) */
    }
    if (set.two_cores && !set.one_core) {
        c1_wait = 1;                     /* started from app_frame, once Bluetooth's start is over (c1_radio) */
        eng_voice_cap(8);
    } else {
        c1_why = c1_said_why ? c1_said_why : C1_WHY_OFF;
        eng_voice_cap(8);                /* one core: eight voices */
    }
#endif
}

/* ------------------------------------------------------------------ photo --- */
/* LZ4 block (fm1-base tools/lz4blk.py writes them): 0 = damaged */
static uint32_t lz4_dec(const uint8_t *s, uint32_t n, uint8_t *dst, uint32_t cap)
{
    const uint8_t *e = s + n;
    uint8_t *d = dst, *de = dst + cap;
    while (s < e) {
        uint32_t tok = *s++, lit = tok >> 4, m, off;
        const uint8_t *r;
        if (lit == 15u) {
            uint32_t b;
            do {
                if (s >= e)
                    return 0;
                b = *s++;
                lit += b;
            } while (b == 255u);
        }
        if ((uint32_t)(e - s) < lit || (uint32_t)(de - d) < lit)
            return 0;
        while (lit--)
            *d++ = *s++;
        if (s >= e)
            break;
        if (e - s < 2)
            return 0;
        off = (uint32_t)s[0] | (uint32_t)s[1] << 8;
        s += 2;
        m = (tok & 15u) + 4u;
        if ((tok & 15u) == 15u) {
            uint32_t b;
            do {
                if (s >= e)
                    return 0;
                b = *s++;
                m += b;
            } while (b == 255u);
        }
        if (off == 0u || (uint32_t)(d - dst) < off || (uint32_t)(de - d) < m)
            return 0;
        r = d - off;
        while (m--)
            *d++ = *r++;
    }
    return (uint32_t)(d - dst);
}

#define STRIP 48u                        /* rows a strip: three bands */
static uint8_t band_px[PHOTO_SIDE * PHOTO_BAND / 2u] PLAT_POOL;   /* one band, packed */
static uint8_t strip_idx[PHOTO_SIDE * STRIP] PLAT_POOL; /* the strip's palette indices */

static struct {
    uint8_t photo;
    uint8_t dirty;                       /* draw the whole screen at the next frame */
    float t;                             /* the waver's time */
    uint16_t pal[16];                    /* the palette as the knobs and the sound colour it, byte-swapped RGB565 */
    uint8_t pr[16], pg[16], pb[16];
    float level;                         /* the warm light's flicker (the output's level) */
    uint32_t last_draw;
} ph;

/* the palette for this frame: Tone brightens, Swell lifts the reds, the sound makes the warm light flicker, the
 * beat's kick lifts it all a little */
static void photo_palette(void)
{
    const uint8_t *src = PHOTOS[ph.photo].pal;
    float tone = knob_val(K_TONE), swell = knob_val(K_SWELL);
    float bright = 0.7f + 0.55f * tone + 0.14f * eng_pulse(), lift = 0.55f * swell, fl = fm_minf(ph.level * 1.8f, 1.0f);
    float burn = set.fuzz_off ? 0.0f : knob_val(K_FUZZ);   /* the fuzz burns the colours: more contrast, more colour */
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        float r = src[3u * i], g = src[3u * i + 1u], b = src[3u * i + 2u];
        int fire = r > 190.0f && g > 110.0f;   /* the light's warm whites and orange: sun, a lamp */
        if (r > g + 30.0f && r > b)      /* the red light swells */
            r += (255.0f - r) * lift;
        r *= bright;
        g *= bright;
        b *= bright;
        if (burn > 0.0f) {
            float m = (r + g + b) * (1.0f / 3.0f), k = 1.0f + 0.9f * burn, sat = 1.0f + 0.8f * burn;
            r = 128.0f + (m + (r - m) * sat - 128.0f) * k;
            g = 128.0f + (m + (g - m) * sat - 128.0f) * k;
            b = 128.0f + (m + (b - m) * sat - 128.0f) * k;
        }
        if (fire) {
            float f = 0.72f + 0.45f * fl;
            r *= f;
            g *= f;
            b *= f;
        }
        ph.pr[i] = (uint8_t)fm_clampf(r, 0.0f, 255.0f);
        ph.pg[i] = (uint8_t)fm_clampf(g, 0.0f, 255.0f);
        ph.pb[i] = (uint8_t)fm_clampf(b, 0.0f, 255.0f);
        ph.pal[i] = swap16(RGB(ph.pr[i], ph.pg[i], ph.pb[i]));
    }
}

/* rows y0 .. y0 + STRIP of a photo into the canvas in the colours pal; fx: with the knobs' marks (the waver, the smear,
 * in ph's colours), else plain */
static void photo_strip(uint32_t y0, uint32_t photo, const uint16_t *pal, int fx)
{
    const photo_t *p = &PHOTOS[photo];
    float glide = fx ? knob_val(K_GLIDE) : 0.0f, blur = fx ? knob_val(K_BLUR) : 0.0f, arm = fx ? eng_arm() : 0.0f;
    int32_t rad = (int32_t)(blur * blur * blur * 6.0f);    /* Blur: a sideways smear, up to 6 pixels each way */
    float amp = glide * glide * 7.0f;                      /* Glide: the rows waver, as the pitch does */
    uint32_t b, y, x;
    for (b = 0; b < STRIP / PHOTO_BAND; b++) {
        uint32_t band = y0 / PHOTO_BAND + b, o = p->off[band];
        uint32_t got = lz4_dec(p->data + o, p->off[band + 1u] - o, band_px, sizeof band_px), i;
        if (got != sizeof band_px)
            for (i = 0; i < sizeof band_px; i++)
                band_px[i] = 0;
        for (i = 0; i < PHOTO_SIDE * PHOTO_BAND / 2u; i++) {
            strip_idx[b * PHOTO_SIDE * PHOTO_BAND + 2u * i] = band_px[i] >> 4;
            strip_idx[b * PHOTO_SIDE * PHOTO_BAND + 2u * i + 1u] = band_px[i] & 15u;
        }
    }
    cv_begin(240, STRIP, C_BLACK);
    for (y = 0; y < STRIP; y++) {
        const uint8_t *row = strip_idx + y * PHOTO_SIDE;
        uint16_t *out = cv_px + y * 240u;
        float yy = (float)(y0 + y);
        int32_t dx = (int32_t)(amp * (sine_wave(yy * (1.0f / 56.0f) + ph.t) + 0.6f * arm));
        if (rad == 0) {
            for (x = 0; x < 240u; x++) {
                int32_t sx = clamp((int32_t)x - dx, 0, 239);
                out[x] = pal[row[sx]];
            }
            continue;
        }
        {   /* the smear: a running sum over 2 rad + 1 pixels, each colour apart */
            int32_t sr = 0, sg = 0, sb = 0, n = 2 * rad + 1, xi;
            for (xi = -rad; xi <= rad; xi++) {
                uint8_t c = row[clamp(xi - dx, 0, 239)];
                sr += ph.pr[c];
                sg += ph.pg[c];
                sb += ph.pb[c];
            }
            for (x = 0; x < 240u; x++) {
                uint8_t cin = row[clamp((int32_t)x + rad + 1 - dx, 0, 239)], cout = row[clamp((int32_t)x - rad - dx, 0, 239)];
                out[x] = swap16(RGB((uint32_t)(sr / n), (uint32_t)(sg / n), (uint32_t)(sb / n)));
                sr += ph.pr[cin] - ph.pr[cout];
                sg += ph.pg[cin] - ph.pg[cout];
                sb += ph.pb[cin] - ph.pb[cout];
            }
        }
    }
}

/* -------------------------------------------------------------- the intro --- */
/* At power-on: the boot photo with ALBUM 01 in white over it (app_splash draws that first, so it is the first thing
 * the screen shows), then the photo melts into the first patch's, then the words turn pink, red, orange and gold and
 * fade away. The keys play all the while. */
#ifdef PHOTO_BOOT
#define INTRO_Y0 96u                     /* the strip the words are in, and their row in it */
#define INTRO_TY 8
static const uint32_t INTRO_HOLD = 1600u, INTRO_MELT = 2400u;   /* ms: the boot photo alone; it melting away */
static const struct { uint32_t t; uint8_t r, g, b; } INTRO_INK[] = {   /* the words' colour, ms from the start */
    {4000u, 255, 255, 255}, {4650u, 255, 128, 196}, {5300u, 236, 44, 70}, {5950u, 255, 142, 44},
    {6600u, 255, 204, 92}, {7000u, 255, 204, 92},
};
static const uint32_t INTRO_GONE = 8200u;   /* faded out by then */
static uint8_t intro_mask[240u * STRIP] PLAT_POOL;   /* the words' coverage, 0..63 */
static uint16_t intro_px[240u * STRIP] PLAT_POOL;    /* a strip of the boot photo, while it melts */
static struct {
    uint8_t on;
    uint32_t t0;
    uint16_t pal[16];                    /* the boot photo's own colours */
} intro;

static inline uint16_t mix565(uint16_t a, uint16_t b, uint32_t t)   /* (byte-swapped RGB565) a to b, t 0..256 */
{
    uint32_t x = swap16(a), y = swap16(b), u = 256u - t;
    uint32_t r = ((x >> 11) * u + (y >> 11) * t) >> 8, g = (((x >> 5) & 63u) * u + ((y >> 5) & 63u) * t) >> 8,
             bl = ((x & 31u) * u + (y & 31u) * t) >> 8;
    return swap16((uint16_t)(r << 11 | g << 5 | bl));
}

static void intro_prepare(void)          /* the boot photo's colours, and the words' coverage */
{
    const uint8_t *src = PHOTOS[PHOTO_BOOT].pal;
    uint32_t i;
    for (i = 0; i < 16u; i++)
        intro.pal[i] = swap16(RGB(src[3u * i], src[3u * i + 1u], src[3u * i + 2u]));
    cv_begin(240, STRIP, C_BLACK);
    cv_text((240 - text_w(&FONT_L, "ALBUM 01")) / 2, INTRO_TY, &FONT_L, "ALBUM 01", RGB(255, 255, 255));
    for (i = 0; i < 240u * STRIP; i++)
        intro_mask[i] = (uint8_t)((swap16(cv_px[i]) >> 5) & 63u);
}

static void intro_words(uint16_t ink, uint32_t alpha)   /* the words over the canvas (the strip at INTRO_Y0) */
{
    uint32_t i;
    if (!alpha)
        return;
    for (i = 0; i < 240u * STRIP; i++)
        if (intro_mask[i])
            cv_px[i] = mix565(cv_px[i], ink, intro_mask[i] * alpha / 63u);
}

void app_splash(void)                    /* the first screen: the boot photo and the words, white */
{
    uint32_t y;
    intro_prepare();
    for (y = 0; y < 240u; y += STRIP) {
        photo_strip(y, PHOTO_BOOT, intro.pal, 0);
        if (y == INTRO_Y0)
            intro_words(swap16(RGB(255, 255, 255)), 256u);
        cv_blit(0, y);
    }
}

static void intro_start(void)
{
    intro_prepare();
    intro.on = 1;
    intro.t0 = plat_ms();
}

/* a strip of the intro: the patch's photo (drawn already), the boot photo over it as it melts, the words */
static void intro_strip(uint32_t y0, uint32_t now)
{
    uint32_t t = now - intro.t0, i, melt, alpha = 256u;
    uint16_t ink;
    if (t < INTRO_HOLD + INTRO_MELT) {
        melt = t <= INTRO_HOLD ? 256u : 256u - (t - INTRO_HOLD) * 256u / INTRO_MELT;   /* the boot photo's share */
        for (i = 0; i < 240u * STRIP; i++)
            intro_px[i] = cv_px[i];
        photo_strip(y0, PHOTO_BOOT, intro.pal, 0);
        if (melt < 256u)
            for (i = 0; i < 240u * STRIP; i++)
                cv_px[i] = mix565(intro_px[i], cv_px[i], melt);
    }
    if (y0 != INTRO_Y0)
        return;
    for (i = 1; i + 1u < sizeof INTRO_INK / sizeof INTRO_INK[0] && t >= INTRO_INK[i].t; i++)
        ;
    {
        uint32_t a = INTRO_INK[i - 1u].t, b = INTRO_INK[i].t, k = t <= a ? 0u : t >= b ? 256u : (t - a) * 256u / (b - a);
        ink = mix565(swap16(RGB(INTRO_INK[i - 1u].r, INTRO_INK[i - 1u].g, INTRO_INK[i - 1u].b)),
                     swap16(RGB(INTRO_INK[i].r, INTRO_INK[i].g, INTRO_INK[i].b)), k);
    }
    {
        const uint32_t fade0 = INTRO_INK[sizeof INTRO_INK / sizeof INTRO_INK[0] - 1u].t;
        if (t > fade0)
            alpha = t >= INTRO_GONE ? 0u : 256u - (t - fade0) * 256u / (INTRO_GONE - fade0);
    }
    intro_words(ink, alpha);
}
#endif

/* ---------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a message over the photo: the patch, a knob, a button */
    uint8_t msg_chord;                   /* the message is a chord: drawn in its notes' colours (chord_show) */
    char chord_nt[8][4];                 /* its notes, spelled from the root: D F# A */
    uint8_t chord_pc[8], chord_n, chord_root;
    uint32_t msg_until, knob_until;
    int8_t knob_shown;                   /* the knob whose bar is up, -1 none */
    uint32_t keys, btn;
    int16_t key_notes[NKEYS][8];         /* the notes each held key started (one, or a chord) */
    uint8_t key_n[NKEYS], key_vel[NKEYS], key_arp[NKEYS];   /* how many; its velocity; sent to the arpeggio */
    uint8_t fx_used;                     /* the fuzz's EQ set while FX was held: the fuzz left in */
    uint8_t arp_used;                    /* a key pressed while ARP was held: the key set, chord mode left as it is */
    uint8_t boot_screen;                 /* the intro is on: OCT+ or OCT- held there chooses the cores */
    uint8_t oct_used;
    uint32_t oct_t;
    uint8_t seq_used;                    /* a knob turned while SEQ was held: tempo or level set, the beat left as is */
    uint8_t master;                      /* this FM-1 started the beat: it sends the clock */
    uint32_t ticks_sent;
    uint8_t hold;                        /* PLAY: the notes held */
    uint8_t looking;                     /* REC: looking for Bluetooth MIDI devices */
    uint32_t rec_t;
    uint32_t frames;
} ui;

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

void app_say(const char *a, const char *b)
{
    uint32_t i;
    char t[52];
    ui.msg_chord = 0;
    for (i = 0; i < 23u && a && a[i]; i++)
        ui.msg[0][i] = a[i];
    ui.msg[0][i] = 0;
    for (i = 0; i < 23u && b && b[i]; i++)
        ui.msg[1][i] = b[i];
    ui.msg[1][i] = 0;
    ui.msg_until = plat_ms() + 1600u;
    ph.dirty = 1;
    if (ann_on()) {
        t[0] = 0;
        text_cat(t, ui.msg[0]);
        if (ui.msg[1][0]) {
            text_cat(t, " ");
            text_cat(t, ui.msg[1]);
        }
        ann_say(t, 0, ANN_NOW);
    }
}

static void speak(void)                  /* a screen reader that asks hears the patch and the knobs */
{
    char t[96], v[8];
    uint32_t k;
    if (ann_on() && ann_where()) {
        t[0] = 0;
        text_cat(t, APP_NAME ". ");
        text_cat(t, eng_patch_info(set.patch)->name);
        for (k = 0; k < NKNOB; k++) {
            text_cat(t, ". ");
            text_cat(t, KNOB_NAME[k]);
            text_cat(t, " ");
            int_text((int32_t)(knob_val(k) * 100.0f + 0.5f), v);
            text_cat(t, v);
        }
        if (ui.hold)
            text_cat(t, ". HOLD");
        ann_say(t, 0, ANN_NOW);
    }
    ann_tick(plat_ms());
}

static void midi_send3(uint32_t st, uint32_t d1, uint32_t d2)   /* out over USB and Bluetooth (a linked FM-1) */
{
    if (st < 0xF0u)                      /* on the MIDI channel (ALL: on 1) */
        st = (st & 0xF0u) | (set.midi_ch ? set.midi_ch - 1u : 0u);
    plat_midi_send((st >> 4) | st << 8 | d1 << 16 | d2 << 24, TO_USB | TO_BLE);
}

/* ------------------------------------------------------------ the keys --- */
/* Each key starts one note, or in chord mode a chord (chords.h). A note two held chords share sounds once and lets
 * go when the last of them does: common tones ring on through a change of chord, as open strings do. The notes go
 * out over MIDI as played, so a linked FM-1 or a computer gets the chord itself. */
#include "chords.h"

static uint8_t note_ref[128];

static void note_down(int n, int v, int arp)
{
    if (note_ref[n]++ == 0u) {
        if (arp)
            eng_arp_note(n, 1);
        else
            eng_note_on(n, v);
        midi_send3(0x90u, (uint32_t)n, (uint32_t)v);
    }
}

static void note_up(int n, int arp)
{
    if (note_ref[n] && --note_ref[n] == 0u) {
        if (arp)
            eng_arp_note(n, 0);
        else
            eng_note_off(n);
        midi_send3(0x80u, (uint32_t)n, 0);
    }
}

/* ACCORDION's black keys, by row: the bottom row (F#3 to A#3) open chords, low (root, fifth, octave, the third on
 * top); the middle row the patch's own voicing (chord_build); the top row (C#5 to F#5) the chords extended: I maj9,
 * ii m9, IV 6/9 (and, an octave down, V 9sus4 and vi m11). deg: 0 2 5 7 9; root: the chord's root, in the key */
static int accordion_row(int row, int deg, int root, int16_t *out, char *name)
{
    static const int8_t OPEN_MAJ[4] = {0, 7, 12, 16}, OPEN_MIN[4] = {0, 7, 12, 15};
    static const struct { int8_t iv[5]; const char *suf; } EXT[12] = {
        [0] = {{0, 4, 7, 11, 14}, "maj9"}, [2] = {{0, 3, 7, 10, 14}, "m9"}, [5] = {{0, 4, 7, 9, 14}, "6/9"},
        [7] = {{0, 5, 7, 10, 14}, "9sus4"}, [9] = {{0, 3, 7, 10, 17}, "m11"},
    };
    int minor = deg == 2 || deg == 9, i, n;
    const char *const *nm = chord_flats(set.key) ? NOTE_FLAT : NOTE_SHARP;
    name[0] = 0;
    text_cat(name, nm[((root % 12) + 12) % 12]);
    if (row == 0) {
        const int8_t *iv = minor ? OPEN_MIN : OPEN_MAJ;
        while (root > 52)                /* low: the open strings' register */
            root -= 12;
        for (i = 0; i < 4; i++)
            out[i] = (int16_t)(root + iv[i]);
        if (minor)
            text_cat(name, "m");
        n = 4;
    } else {
        while (root > 62)                /* around middle C, so a ninth does not shrill */
            root -= 12;
        while (root < 50)
            root += 12;
        for (i = 0; i < 5; i++)
            out[i] = (int16_t)(root + EXT[deg].iv[i]);
        text_cat(name, EXT[deg].suf);
        n = 5;
    }
    return n;
}

/* a chord's notes, spelled from its root as a player would (D major: D F# A; G minor: G Bb D), stacked in thirds
 * (root, third, fifth, seventh, then ninth, eleventh, thirteenth), for the screen */
static void chord_show(const char *name, const int16_t *notes, int n)
{
    static const uint8_t NAT[7] = {0, 2, 4, 5, 7, 9, 11};   /* C D E F G A B */
    uint8_t has[12] = {0}, m = 0;
    int letter, root, s, has3, has7;
    if (name[0] < 'A' || name[0] > 'G' || n <= 0)
        return;
    letter = (name[0] - 'C' + 7) % 7;
    root = NAT[letter] + (name[1] == '#' ? 1 : name[1] == 'b' ? -1 : 0);
    root = (root + 12) % 12;
    has[0] = 1;                          /* (the root named, played or not) */
    for (s = 0; s < n; s++)
        has[((notes[s] - root) % 12 + 12) % 12] = 1;
    has3 = has[3] || has[4];
    has7 = has[10] || has[11];
    {
        /* each interval: where it stacks, and how many letters up from the root it is spelled */
        int rank[12], off[12], order[12], k = 0, a, b;
        for (s = 0; s < 12; s++) {
            switch (s) {
            case 0: rank[s] = 0, off[s] = 0; break;
            case 1: rank[s] = 7, off[s] = 1; break;                                   /* b9 */
            case 2: rank[s] = has3 ? 7 : 1, off[s] = 1; break;                        /* 9, or sus2 */
            case 3: case 4: rank[s] = 2, off[s] = 2; break;                           /* the third */
            case 5: rank[s] = has3 ? 8 : 3, off[s] = 3; break;                        /* 11, or sus4 */
            case 6: rank[s] = has[7] ? 8 : 4, off[s] = has[7] ? 3 : 4; break;         /* #11, or b5 */
            case 7: rank[s] = 4, off[s] = 4; break;
            case 8: rank[s] = has[7] ? 9 : 4, off[s] = has[7] ? 5 : 4; break;         /* b13, or #5 */
            case 9: rank[s] = has7 ? 9 : 5, off[s] = 5; break;                        /* 13, or 6 */
            default: rank[s] = 6, off[s] = 6; break;                                  /* the seventh */
            }
            if (has[s])
                order[k++] = s;
        }
        for (a = 1; a < k; a++)           /* (a few notes: insertion sort by rank) */
            for (b = a; b > 0 && rank[order[b]] < rank[order[b - 1]]; b--) {
                int t = order[b];
                order[b] = order[b - 1];
                order[b - 1] = t;
            }
        for (a = 0; a < k && m < 8u; a++) {
            int iv = order[a], pc = (root + iv) % 12, l = (letter + off[iv]) % 7;
            int acc = ((pc - NAT[l]) % 12 + 12) % 12;   /* 0, 1 #, 2 ##, 11 b, 10 bb */
            char *t = ui.chord_nt[m];
            t[0] = (char)('A' + (l + 2) % 7);
            t[1] = acc == 1 ? '#' : acc == 11 ? 'b' : acc == 2 ? '#' : acc == 10 ? 'b' : 0;
            t[2] = acc == 2 ? '#' : acc == 10 ? 'b' : 0;
            t[3] = 0;
            ui.chord_pc[m++] = (uint8_t)pc;
        }
    }
    ui.chord_n = m;
    ui.chord_root = (uint8_t)root;
    ui.msg_chord = 1;
}

static void key_down(uint32_t k, uint32_t v, int quiet)
{
    int bass = clamp(53 + (int32_t)k + 12 * set.octave, 0, 127), i;
    const patch_t *p = eng_patch_info(set.patch);
    ui.key_vel[k] = (uint8_t)v;
    int pc = bass % 12, black = (0x54A >> pc) & 1;
    if (set.chords == MODE_CHORDS || (set.chords == MODE_ACCORDION && black)) {
        char name[16];
        int root = bass;
        if (set.chords == MODE_ACCORDION) {   /* a black key: one of the key's five chords, I ii IV V vi */
            static const int8_t DEG[12] = {0, 0, 0, 2, 0, 0, 5, 0, 7, 0, 9, 0};
            root = bass - pc + (set.key + DEG[pc]) % 12;
            if (root > bass + 6)
                root -= 12;
            v = v * 3u / 4u;             /* (the chord under the tune, softer) */
            if (k < 7u || k >= 19u)      /* the bottom row and the top: their own voicings */
                ui.key_n[k] = (uint8_t)accordion_row(k < 7u ? 0 : 2, DEG[pc], root, ui.key_notes[k], name);
            else
                ui.key_n[k] = (uint8_t)chord_build(root, set.key, p->chord, ui.key_notes[k], name);
        } else {
            ui.key_n[k] = (uint8_t)chord_build(root, set.key, p->chord, ui.key_notes[k], name);
        }
        ui.key_arp[k] = p->mono;
        if (ui.key_n[k])
            eng_bass_root(ui.key_notes[k][0]);   /* the bass plays the chord's lowest note */
        if (!quiet) {
            app_say(name, "");
            chord_show(name, ui.key_notes[k], ui.key_n[k]);
            ui.msg_until = plat_ms() + 900u;
        }
    } else {
        int n = bass;
        if (set.chords == MODE_ACCORDION && !((0xAB5 >> ((pc - set.key + 12) % 12)) & 1))
            n += chord_flats(set.key) ? -1 : 1;   /* a white key in the key: F in G is F#, B in F is Bb */
        else if (set.chords == MODE_NOTES)
            eng_bass_root(n);
        ui.key_notes[k][0] = (int16_t)n;
        ui.key_n[k] = 1;
        ui.key_arp[k] = 0;
    }
    for (i = 0; i < ui.key_n[k]; i++)
        note_down(clamp(ui.key_notes[k][i], 0, 127), (int)v, ui.key_arp[k]);
}

static void key_up(uint32_t k)
{
    int i;
    for (i = 0; i < ui.key_n[k]; i++)
        note_up(clamp(ui.key_notes[k][i], 0, 127), ui.key_arp[k]);
    ui.key_n[k] = 0;
}

static void keys_again(void)             /* the held keys played afresh: a new patch, key or mode */
{
    uint32_t k;
    for (k = 0; k < NKEYS; k++)
        if (ui.keys >> k & 1u)
            key_up(k);
    for (k = 0; k < NKEYS; k++)
        if (ui.keys >> k & 1u)
            key_down(k, ui.key_vel[k], 1);
}

static void key_set(int key)
{
    set.key = (uint8_t)(((key % 12) + 12) % 12);
    set_dirty = 1;
    keys_again();
    app_say("KEY", KEY_NAME(set.key));
}

static void chords_set(int mode)          /* ARP: CHORDS, ACCORDION, NOTES */
{
    static const char *const NAME[3] = {"NOTES", "CHORDS", "ACCORDION"};
    set.chords = (uint8_t)((mode % 3 + 3) % 3);
    set_dirty = 1;
    keys_again();
    app_say(NAME[set.chords], set.chords ? KEY_NAME(set.key) : "");
}

static void beat_set(int on)             /* SEQ: start or stop; a linked FM-1 starts and stops with it */
{
    if (on) {
        beat_to_engine();
        eng_beat(1);
        midi_send3(0xFAu, 0, 0);
        ui.master = 1;
        ui.ticks_sent = 0;
    } else {
        eng_beat(0);
        midi_send3(0xFCu, 0, 0);
        ui.master = 0;
    }
    {
        char t[12];
        int_text(tempo_now(), t);
        text_cat(t, " BPM");
        app_say(on ? "BEAT" : "BEAT OFF", on ? t : "");
    }
}

static void tempo_step(int32_t d)
{
    const beat_t *b = eng_patch_info(set.patch)->beat;
    int base = b ? (int)b->bpm : 90, t = clamp(tempo_now() + d, 50, 200);
    char s[12];
    set.tempo[set.patch] = (int8_t)clamp(t - base, -100, 100);
    set_dirty = 1;
    beat_to_engine();
    int_text(tempo_now(), s);
    text_cat(s, " BPM");
    app_say("TEMPO", s);
}

#define SHOW_BEAT ((int8_t)NKNOB)         /* the knob bar showing the beat's level */
#define SHOW_CH ((int8_t)(NKNOB + 1))     /* ... the MIDI channel */
static void midi_ch_set(int32_t c)       /* EDIT held and SELECT: 0 (ALL), 1 .. 16 */
{
    char v[8];
    set.midi_ch = (uint8_t)clamp(c, 0, 16);
    midi_rx_ch = set.midi_ch;
    set_dirty = 1;
    ui.knob_shown = SHOW_CH;
    ui.knob_until = plat_ms() + 1500u;
    ph.dirty = 1;
    if (ann_on()) {
        char t[24] = "MIDI CHANNEL ";
        if (set.midi_ch)
            int_text(set.midi_ch, v);
        else
            v[0] = 'A', v[1] = 'L', v[2] = 'L', v[3] = 0;
        text_cat(t, v);
        ann_say(t, 0, ANN_NOW);
    }
}
static void beat_level_set(int32_t v, int send)   /* 0..100; shown as a knob's bar is, sent as CC 21 */
{
    v = clamp(v, 0, 100);
    if (set.beat_level == (uint8_t)v)
        return;
    set.beat_level = (uint8_t)v;
    set_dirty = 1;
    beat_to_engine();
    if (send)
        midi_send3(0xB0u, CC_BEAT, (uint32_t)v * 127u / 100u);
    ui.knob_shown = SHOW_BEAT;
    ui.knob_until = plat_ms() + 1200u;
    ph.dirty = 1;
    if (ann_on()) {
        char t[24] = "BEAT ", s[8];
        int_text(v, s);
        text_cat(t, s);
        ann_say(t, 0, ANN_NOW);
    }
}

static void beat_level_step(int32_t d) { beat_level_set((int32_t)set.beat_level + d, 1); }

static void show_patch(void)
{
    char t[24] = "";
    int_text((int32_t)set.patch + 1, t);
    text_cat(t, " / ");
    {
        char n[8];
        int_text(eng_patch_count(), n);
        text_cat(t, n);
    }
    app_say(eng_patch_info(set.patch)->name, t);
    ui.knob_until = 0;
}

static void patch_set(int32_t p, int send)
{
    int32_t n = eng_patch_count();
    p = ((p % n) + n) % n;
    set.patch = (uint8_t)p;
    set_dirty = 1;
    eng_patch(p);
    knobs_to_engine();
    beat_to_engine();                    /* the beat keeps its place; the new patch's pattern and tempo take over */
    keys_again();
    ph.photo = (uint8_t)(eng_patch_info(p)->photo % PHOTO_COUNT);
    if (send)
        midi_send3(0xC0u, (uint32_t)p, 0);
    show_patch();
}

static void knob_set(uint32_t k, int32_t v255, int send)
{
    v255 = clamp(v255, 0, 255);
    if (set.knob[set.patch][k] == (uint8_t)v255)
        return;
    set.knob[set.patch][k] = (uint8_t)v255;
    set_dirty = 1;
    eng_knob((int)k, knob_val(k));
    if (send)
        midi_send3(0xB0u, KNOB_CC[k], (uint32_t)v255 >> 1);
    ui.knob_shown = (int8_t)k;
    ui.knob_until = plat_ms() + 1200u;
    ph.dirty = 1;
    if (ann_on()) {
        char t[24], v[8];
        t[0] = 0;
        text_cat(t, KNOB_NAME[k]);
        text_cat(t, " ");
        int_text((int32_t)(knob_val(k) * 100.0f + 0.5f), v);
        text_cat(t, v);
        ann_say(t, 0, ANN_NOW);
    }
}

static void hold_set(int on)
{
    ui.hold = (uint8_t)(on != 0);
    eng_sustain(ui.hold);
    midi_send3(0xB0u, 64u, ui.hold ? 127u : 0u);
    app_say("HOLD", ui.hold ? "ON" : "OFF");
}

static void octave_step(int32_t d)
{
    int32_t o = clamp(set.octave + d, -2, 2);
    char t[4];
    if (o == set.octave)
        return;
    set.octave = (int8_t)o;
    set_dirty = 1;
    t[0] = o < 0 ? '-' : '+';
    t[1] = (char)('0' + (o < 0 ? -o : o));
    t[2] = 0;
    app_say("OCTAVE", t);
}

static void ble_device_say(void)
{
    char name[21];
    if (btdev_found(btdev_pick(), name, 0))
        app_say("LINK TO", name);
    else
        app_say("LINK", "LOOKING");
}

static void ble_device_button(void)      /* REC: look for another FM-1; again: link to the one ALGORITHM picks */
{
    int ev;
    if (!btdev_busy() && !ui.looking) {
        ui.looking = 1;
        app_say("LINK", "LOOKING");
        return;
    }
    ev = btdev_save();
    ui.looking = 0;
    app_say(ev == BTDEV_CONNECTING ? "LINKING" : "LINK", ev == BTDEV_CONNECTING ? btdev_name() : btdev_word(ev));
}

static void input(void)
{
    uint32_t keys, btn = plat_buttons(), k;
    int32_t e;
    touch_poll();
    keys = touch_keys();
    for (k = 0; k < NKEYS; k++) {        /* key 0 is F3 (MIDI 53) */
        uint32_t bit = 1u << k;
        if ((keys & bit) && !(ui.keys & bit)) {
            uint32_t v = touch_vel(k);
            v = v ? v : 100u;
            if (btn >> B_ARP & 1u) {     /* ARP held: this key's note is the key */
                ui.arp_used = 1;
                ui.key_n[k] = 0;
                if (!set.chords)
                    set.chords = 1;
                key_set(53 + (int32_t)k);
                continue;
            }
            key_down(k, v, 0);
            ph.dirty |= 2u;
        } else if (!(keys & bit) && (ui.keys & bit)) {
            key_up(k);
        }
    }
    for (k = 0; k < NB; k++) {
        uint32_t bit = 1u << k;
        if (!(btn & bit) || (ui.btn & bit))
            continue;
        switch (k) {
        case B_OCTDN:                    /* (on the boot screen: on its release, unless held to choose the cores) */
        case B_OCTUP:
            if (ui.boot_screen) {
                ui.oct_t = plat_ms();
                ui.oct_used = 0;
            } else {
                octave_step(k == B_OCTUP ? 1 : -1);
            }
            break;
        case B_PLAY:
            hold_set(!ui.hold);
            break;
        case B_HOME: {                   /* the knobs back where the patch starts */
            uint32_t j;
            for (j = 0; j < NKNOB; j++)
                knob_set(j, (int32_t)(knob_start(eng_patch_info(set.patch), j) * 255.0f + 0.5f), 1);
            set.tempo[set.patch] = 0;
            beat_to_engine();
            app_say("KNOBS", "AS THEY START");
            break;
        }
        case B_SAVE:
            if (plat_store_save(OBJ_SETTINGS, &set, sizeof set) == 0) {
                set_dirty = 0;
                app_say("SAVED", "");
            } else {
                app_say("SAVE", "FAILED");
            }
            break;
        case B_REC:
            if (FM1B_BLE)
                ui.rec_t = plat_ms();
            break;
        case B_ARP:                      /* chord mode, on its release (unless a key was set while it was held) */
            ui.arp_used = 0;
            break;
        case B_FX:                       /* the fuzz in or out on its release, unless its EQ was set while held */
            ui.fx_used = 0;
            break;
        case B_ENV:                      /* FILL: down, a fill to the bar's end; held, it keeps going */
            eng_fill(1);
            break;
        case B_LFO: {                    /* the beat: sparse, as written, busy */
            static const char *const V[3] = {"SPARSE", "AS WRITTEN", "BUSY"};
            set.beat_var = (uint8_t)((set.beat_var + 1u) % 3u);
            set_dirty = 1;
            eng_beat_var(set.beat_var);
            app_say("BEAT", V[set.beat_var]);
            break;
        }
        case B_GLO:                      /* the auto bass */
            set.bass = (uint8_t)!set.bass;
            set_dirty = 1;
            eng_bass(set.bass);
            app_say("BASS", set.bass ? "ON" : "OFF");
            break;
        case B_EDIT:                     /* the MIDI channel shown (EDIT held and SELECT sets it) */
            ui.knob_shown = SHOW_CH;
            ui.knob_until = plat_ms() + 1500u;
            ph.dirty = 1;
            break;
        case B_SEQ:                      /* the beat, on its release (unless its tempo or level was set while held) */
            ui.seq_used = 0;
            break;
        default:
            break;
        }
    }
    if (!(btn >> B_ARP & 1u) && (ui.btn >> B_ARP & 1u) && !ui.arp_used)
        chords_set(set.chords == MODE_CHORDS ? MODE_ACCORDION : set.chords == MODE_ACCORDION ? MODE_NOTES : MODE_CHORDS);
    if (!(btn >> B_FX & 1u) && (ui.btn >> B_FX & 1u) && !ui.fx_used) {
        set.fuzz_off = (uint8_t)!set.fuzz_off;
        set_dirty = 1;
        eng_fuzz_on(!set.fuzz_off);
        app_say("FUZZ", set.fuzz_off ? "OFF" : "ON");
    }
    if (!(btn >> B_ENV & 1u) && (ui.btn >> B_ENV & 1u))
        eng_fill(0);
    if (ui.boot_screen && !ui.oct_used) {        /* a short press on the boot screen: the octave, on release */
        if (!(btn >> B_OCTUP & 1u) && (ui.btn >> B_OCTUP & 1u))
            octave_step(1);
        if (!(btn >> B_OCTDN & 1u) && (ui.btn >> B_OCTDN & 1u))
            octave_step(-1);
    }
    if (!(btn >> B_SEQ & 1u) && (ui.btn >> B_SEQ & 1u) && !ui.seq_used)
        beat_set(!eng_beat_on());
    if (FM1B_BLE && ui.rec_t) {          /* REC held a second: Bluetooth off or on; a press: link */
        if (btn >> B_REC & 1u) {
            if (plat_ms() - ui.rec_t >= 1000u) {
                ui.rec_t = 0;
                set.ble_off = (uint8_t)!set.ble_off;
                set_dirty = 1;
                plat_ble_enable(!set.ble_off);
                app_say("BLUETOOTH", set.ble_off ? "OFF" : "ON");
            }
        } else {
            ui.rec_t = 0;
            if (FM1B_BLE_CENTRAL && !set.ble_off)
                ble_device_button();
            else
                app_say("BLUETOOTH", "IS OFF");
        }
    }
    for (k = 0; k < NE; k++) {
        if ((e = plat_enc((int)k)) == 0)
            continue;
        if (k == EN_PRESET) {
            patch_set((int32_t)set.patch + (e > 0 ? 1 : -1), 1);
        } else if (k == EN_ALGO && FM1B_BLE_CENTRAL && ui.looking) {
            btdev_choose(btdev_pick() + e);
            ble_device_say();
        } else if (k == EN_ALGO && (btn >> B_SEQ & 1u)) {   /* SEQ held: the beat's tempo */
            ui.seq_used = 1;
            tempo_step(e * ((e > 2 || e < -2) ? 4 : 1));
        } else if (k == EN_ALGO) {       /* the fuzz, a knob like K1 to K4 */
            int32_t step = (e > 2 || e < -2) ? 10 : 5;
            knob_set(K_FUZZ, (int32_t)set.knob[set.patch][K_FUZZ] + e * step, 1);
        } else if (k == EN_SELECT && (btn >> B_EDIT & 1u)) {   /* EDIT held: the MIDI channel */
            midi_ch_set((int32_t)set.midi_ch + (e > 0 ? 1 : -1));
        } else if (k == EN_SELECT && (btn >> B_SEQ & 1u)) {   /* SEQ held: the beat's level */
            ui.seq_used = 1;
            beat_level_step(e * ((e > 2 || e < -2) ? 10 : 5));
        } else if (k == EN_SELECT) {     /* chord mode's key */
            key_set((int)set.key + (e > 0 ? 1 : -1));
        } else if (k >= EN_K1 && k <= EN_K3 && (btn >> B_FX & 1u)) {   /* FX held: the fuzz's EQ, low, mid, high */
            int32_t step = (e > 2 || e < -2) ? 10 : 5;
            uint32_t q = K_FZLO + (k - EN_K1);
            ui.fx_used = 1;
            knob_set(q, (int32_t)set.knob[set.patch][q] + e * step, 1);
        } else if (k >= EN_K1 && k <= EN_K4) {
            int32_t step = (e > 2 || e < -2) ? 10 : 5;     /* a fast turn goes further */
            knob_set(k - EN_K1, (int32_t)set.knob[set.patch][k - EN_K1] + e * step, 1);
        }
    }
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds((set_dirty ? 1u << B_SAVE : 0u) | (ui.hold ? LED_PLAY_GREEN | 1u << B_PLAY : 0u)
                  | (set.chords ? 1u << B_ARP : 0u) | (set.fuzz_off ? 0u : 1u << B_FX)
                  | (eng_filling() ? 1u << B_ENV : 0u) | (set.beat_var != 1u ? 1u << B_LFO : 0u)
                  | (set.bass ? 1u << B_GLO : 0u)
                  | (eng_beat_on() && (eng_step() & 3u) == 0u ? 1u << B_SEQ : 0u)   /* SEQ blinks the beats */
                  | (btn & ~(1u << B_PLAY)), keys);
}

void app_input_only(void) { input(); }

/* the knobs' marks over the photo: a darkened band, then words */
static void shade(uint32_t y, uint32_t h)
{
    uint32_t i;
    for (i = y * 240u; i < (y + h) * 240u && i < 240u * STRIP; i++) {
        uint32_t c = swap16(cv_px[i]);
        cv_px[i] = swap16((c >> 1) & 0x7BEFu);   /* half as bright */
    }
}

/* a note's colour: round the colour wheel by semitone (C red, E yellow-green, G# blue, ...); over a red photo,
 * shades of red instead, dark to pale by the same order */
static uint16_t note_rgb(int pc, int red)
{
    if (red) {
        uint32_t t = (uint32_t)pc * 255u / 11u;
        return RGB(150u + t * 105u / 255u, 20u + t * 170u / 255u, 30u + t * 150u / 255u);
    } else {
        int h = pc * 6, x, r, g, b;      /* 72 steps round; a light, clear colour to read over a photo */
        int sx = h % 12;
        x = 110 + sx * 145 / 12;
        switch (h / 12) {
        case 0: r = 255, g = x, b = 110; break;
        case 1: r = 365 - x, g = 255, b = 110; break;
        case 2: r = 110, g = 255, b = x; break;
        case 3: r = 110, g = 365 - x, b = 255; break;
        case 4: r = x, g = 110, b = 255; break;
        default: r = 255, g = 110, b = 365 - x; break;
        }
        return RGB(r, g, b);
    }
}

/* text with a black edge all round, for words straight on the photo */
static int32_t text_ol(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    int dx, dy;
    for (dy = -1; dy <= 1; dy++)
        for (dx = -1; dx <= 1; dx++)
            if (dx || dy)
                cv_text(x + dx, y + dy, f, s, 0);
    return cv_text(x, y, f, s, c);
}

/* is the top of the photo mostly red (FLAME's room)? then the chord's letters go in shades of red */
static int top_is_red(void)
{
    uint32_t i, r = 0, g = 0, b = 0;
    for (i = 0; i < 240u * 30u; i += 3u) {
        uint32_t c = swap16(cv_px[i]);
        r += c >> 11;
        g += (c >> 6) & 31u;
        b += c & 31u;
    }
    i = 240u * 30u / 3u;                 /* (pixels counted) */
    return r > i * 12u && r * 2u > g * 5u && r * 2u > b * 5u;   /* bright enough, and red well over the rest */
}

static void chord_draw(void)
{
    const felucca_font_t *nf = &FONT_B;
    int32_t x = 10, w, i, ny = 6;
    int red = top_is_red();
    char nm[24];
    for (i = 0; i < 23 && ui.msg[0][i]; i++)
        nm[i] = ui.msg[0][i];
    nm[i] = 0;
    w = text_w(&FONT_B, nm) + text_w(&FONT_B, " - ");
    for (i = 0; i < ui.chord_n; i++)
        w += text_w(&FONT_B, ui.chord_nt[i]) + text_w(&FONT_B, " ");
    if (w > 222) {                       /* a long one (Bbmaj9): its notes smaller */
        nf = &FONT_XS;
        ny = 11;
    }
    x = text_ol(x, 6, &FONT_B, nm, note_rgb(ui.chord_root, red));
    x = text_ol(x, ny, nf, " - ", red ? RGB(255, 200, 190) : RGB(255, 245, 235));
    for (i = 0; i < ui.chord_n; i++) {
        x = text_ol(x, ny, nf, ui.chord_nt[i], note_rgb(ui.chord_pc[i], red));
        x += text_w(nf, " ");
    }
}

static void overlay(uint32_t y0, uint32_t now)
{
    if (y0 == 0 && now < ui.msg_until && ui.msg_chord) {
        chord_draw();                    /* a chord: no band, its letters on the photo */
    } else if (y0 == 0 && now < ui.msg_until) {
        shade(0, 34);
        cv_text(10, 6, &FONT_B, ui.msg[0], RGB(255, 236, 220));
        if (ui.msg[1][0])
            cv_text(240 - 10 - text_w(&FONT_XS, ui.msg[1]), 11, &FONT_XS, ui.msg[1], RGB(255, 120, 110));
    }
    if (y0 == 0 && FM1B_BLE_CENTRAL && plat_ble_device() == BLE_DEV_READY && now >= ui.msg_until)
        cv_text(206, 6, &FONT_XS, "LINK", RGB(255, 140, 120));
    if (y0 == 240u - STRIP && ui.knob_shown >= 0 && now < ui.knob_until) {
        uint32_t k = (uint32_t)ui.knob_shown;
        float val = ui.knob_shown == SHOW_BEAT ? (float)set.beat_level * 0.01f
                    : ui.knob_shown == SHOW_CH ? (float)set.midi_ch * (1.0f / 16.0f) : knob_val(k);
        uint32_t w = (uint32_t)(val * 196.0f + 0.5f);
        char v[8];
        shade(10, 38);
        cv_text(12, 14, &FONT_XS, ui.knob_shown == SHOW_BEAT ? "BEAT" : ui.knob_shown == SHOW_CH ? "MIDI CHANNEL"
                : KNOB_NAME[k], RGB(255, 236, 220));
        if (ui.knob_shown == SHOW_CH && !set.midi_ch)
            v[0] = 'A', v[1] = 'L', v[2] = 'L', v[3] = 0;
        else
            int_text(ui.knob_shown == SHOW_CH ? (int32_t)set.midi_ch : (int32_t)(val * 100.0f + 0.5f), v);
        cv_text(228 - text_w(&FONT_XS, v), 14, &FONT_XS, v, RGB(255, 236, 220));
        cv_rect(12, 32, 216, 6, RGB(70, 20, 30));
        cv_rect(12, 32, (int32_t)w + 20, 6, RGB(240, 60, 60));
    }
    if (y0 == 240u - STRIP && !(ui.knob_shown >= 0 && now < ui.knob_until)) {
        if (ui.hold)
            cv_text(12, 30, &FONT_XS, "HOLD", RGB(140, 255, 160));
        if (eng_beat_on()) {
            char t[12];
            int_text(tempo_now(), t);
            text_cat(t, " BPM");
            cv_text(70, 30, &FONT_XS, t, RGB(255, 236, 220));
        }
        if (set.chords) {
            char t[12] = "KEY ";
            text_cat(t, KEY_NAME(set.key));
            cv_text(228 - text_w(&FONT_XS, t), 30, &FONT_XS, t, RGB(255, 200, 120));
        }
    }
}

static void draw(void)
{
    uint32_t y, now = plat_ms();
    photo_palette();
    for (y = 0; y < 240u; y += STRIP) {
        photo_strip(y, ph.photo, ph.pal, 1);
#ifdef PHOTO_BOOT
        if (intro.on)
            intro_strip(y, now);
#endif
        overlay(y, now);
        cv_blit(0, y);
    }
    ph.last_draw = now;
}

void app_ui_init(void)
{
    touch_on(1);
    ui.knob_shown = -1;
    ph.photo = (uint8_t)(eng_patch_info(set.patch)->photo % PHOTO_COUNT);
    ph.dirty = 1;
#ifdef PHOTO_BOOT
    intro_start();                       /* the patch's name once the intro is done */
#else
    show_patch();
#endif
}

#ifndef PLAT_HOST
/* Bluetooth's start and the second core: the radio's calibration draws ~500 ms of high current, during which
 * what is read from the flash can come back wrong (radio.c, radio_quiet). With core 1 running through it, every
 * power-on crashed once, core 0 waiting inside the radio's start (shim_delay_us; the screen of the crash itself
 * garbled), and the restart after it, with Bluetooth skipped, ran fine. So core 1 starts after the radio's start,
 * and is held (not given up) through any later one. */
static void c1_radio(void)
{
    if (radio_quiet && c1_on) {
        uint32_t f = irq_save();         /* (the render must not ask a held core for its half) */
        c1_on = 0;
        c1_hold();
        irq_restore(f);
        c1_bus_back();
        eng_voice_cap(8);
        c1_wait = 1;
    } else if (c1_wait && !radio_quiet) {
        c1_wait = 0;
        if (set.two_cores && !set.one_core)
            c1_start();
    }
}
#endif

void app_frame(void)
{
    uint32_t now = plat_ms(), k;
    float glide = knob_val(K_GLIDE), lv = eng_level();
    static uint32_t last_ms;
    static uint8_t had_msg, had_knob, had_link, had_beat;
    uint8_t link = (uint8_t)(FM1B_BLE_CENTRAL && plat_ble_device() == BLE_DEV_READY);
#ifndef PLAT_HOST
    c1_radio();
#endif
    input();
    speak();
    voices_back(now);
    /* what came in over MIDI that the screen shows */
    if (midi_patch_req >= 0) {
        int32_t p = midi_patch_req;
        midi_patch_req = -1;
        patch_set(p, 0);
    }
    if (midi_beat_req >= 0) {            /* CC 21 in: the beat's level (not sent back) */
        int32_t b = midi_beat_req;
        midi_beat_req = -1;
        beat_level_set((b * 100 + 63) / 127, 0);
    }
    if (midi_knob_mask) {
        uint8_t m = midi_knob_mask;
        midi_knob_mask = 0;
        for (k = 0; k < NKNOB; k++)
            if (m >> k & 1u)
                knob_set(k, (int32_t)midi_knob_req[k] * 2 + (midi_knob_req[k] >> 6), 0);
    }
    if (midi_panic_req) {
        midi_panic_req = 0;
        eng_panic();
    }
    if (midi_hold_req) {                 /* hold from MIDI (a linked FM-1's PLAY): shown, not sent back */
        ui.hold = (uint8_t)(midi_hold_req == 2u);
        midi_hold_req = 0;
        ph.dirty = 1;
    }
    if (ui.master && eng_beat_on() && !eng_clock_ext()) {   /* the clock out: a linked FM-1 follows it */
        uint32_t t = eng_ticks();
        if (t - ui.ticks_sent > 12u)     /* (fallen far behind: catch up rather than burst) */
            ui.ticks_sent = t - 1u;
        while (ui.ticks_sent < t) {
            midi_send3(0xF8u, 0, 0);
            ui.ticks_sent++;
        }
    } else if (!eng_beat_on()) {
        ui.master = 0;
    }
    if (FM1B_BLE_CENTRAL) {
        static int found;
        int nfound = ui.looking ? btdev_count() : 0, ev;
        btdev_page(ui.looking && !set.ble_off);
        if (nfound > found && btdev_pick() < nfound)
            ble_device_say();
        found = nfound;
        while ((ev = btdev_event()) != BTDEV_NONE)
            if (ev == BTDEV_CONNECTED || ev == BTDEV_NO_ANSWER || ev == BTDEV_LOST)
                app_say("LINK", btdev_word(ev));
    }
    /* the photo moves with the sound: the waver runs while Glide is up and something sounds, the warm light follows
     * the level; drawn about 20 times a second then, else only when something changed */
    ph.t += (float)(now - last_ms) * 0.001f * (0.25f + 0.6f * glide);
    if (ph.t > 1000.0f)
        ph.t -= 1000.0f;
    last_ms = now;
    ph.level = ph.level * 0.6f + lv * 0.4f;
    if ((now < ui.msg_until) != had_msg || (now < ui.knob_until) != had_knob || link != had_link)
        ph.dirty = 1;
    had_msg = (uint8_t)(now < ui.msg_until);
    had_knob = (uint8_t)(now < ui.knob_until);
    had_link = link;
    if ((uint8_t)eng_beat_on() != had_beat)
        ph.dirty = 1;
    had_beat = (uint8_t)eng_beat_on();
    if (((lv > 0.01f && (glide > 0.03f || lv > 0.05f)) || had_beat) && now - ph.last_draw >= 50u)
        ph.dirty = 1;
#ifdef PHOTO_BOOT
    ui.boot_screen = intro.on;
    if (intro.on && !ui.oct_used && (ui.btn >> B_OCTUP & 1u || ui.btn >> B_OCTDN & 1u) && now - ui.oct_t >= 1500u) {
        int two = (ui.btn >> B_OCTUP & 1u) != 0;   /* held a second and a half on the boot screen: the cores */
        ui.oct_used = 1;
        set.two_cores = (uint8_t)two;
        plat_store_save(OBJ_SETTINGS, &set, sizeof set);
#ifndef PLAT_HOST
        if (two && !c1_on) {
            c1_supply();                 /* (M-VAVE's supply setup, once: only for two cores) */
            c1_unbar();
            if (radio_quiet)
                c1_wait = 1;             /* (Bluetooth still starting: c1_radio starts it after) */
            else
                c1_start();
        } else if (!two) {
            c1_wait = 0;
            if (c1_on)
                c1_retire(C1_WHY_OFF);
        }
        if (two && c1_wait)
            app_say("TWO CORES", "STARTING");
        else
            app_say(two ? (c1_on ? "TWO CORES" : "TWO CORES FAILED") : "ONE CORE",
                    two ? (c1_on ? "12 VOICES" : c1_fail_text()) : "8 VOICES");
#else
        app_say(two ? "TWO CORES" : "ONE CORE", two ? "12 VOICES" : "8 VOICES");
#endif
        ui.msg_until = now + 3000u;
    }
    {   /* every boot: which it is running on, a moment after the photo comes up */
        static uint8_t said_cores;
        if (intro.on && !said_cores && now - intro.t0 >= 600u && now >= ui.msg_until
#ifndef PLAT_HOST
            && !c1_wait                  /* (settled: after Bluetooth's start) */
#endif
            ) {
#ifndef PLAT_HOST
            int two = c1_on;
            const char *why = two ? "12 VOICES" : c1_fail_text();   /* (one core after a failure: which) */
#else
            int two = set.two_cores;
            const char *why = two ? "12 VOICES" : "8 VOICES";
#endif
            said_cores = 1;
            app_say(two ? "TWO CORES" : "ONE CORE", why);
            ui.msg_until = now + 3000u;
        }
    }
    if (intro.on) {                      /* the intro: about 30 frames a second, then the patch's name */
        if (now - intro.t0 >= INTRO_GONE) {
            char a[24], b[24];
            intro.on = 0;
            if (c1_report(a, b)) {       /* a crash with two cores: where it was, for ten seconds */
                app_say(a, b);
                ui.msg_until = now + 10000u;
            } else {
                show_patch();
            }
            ph.dirty = 1;
        } else if (now - ph.last_draw >= 33u) {
            ph.dirty = 1;
        }
    }
#endif
    if (ph.dirty) {
        ph.dirty = 0;
        draw();
    }
    ui.frames++;
}

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "patch"))
        *got = set.patch;
    else if (!strcmp(what, "octave"))
        *got = set.octave;
    else if (!strcmp(what, "dirty"))
        *got = set_dirty;
    else if (!strcmp(what, "hold"))
        *got = ui.hold;
    else if (!strcmp(what, "voices"))
        *got = (int)eng_voices_on();
    else if (!strcmp(what, "photo"))
        *got = ph.photo;
    else if (!strncmp(what, "knob", 4))  /* knobN: 0..255 */
        *got = set.knob[set.patch][atoi(what + 4) % NKNOB];
    else if (!strcmp(what, "level"))     /* the output level, in hundredths */
        *got = (int)(eng_level() * 100.0f);
    else if (!strcmp(what, "c1"))
        *got = c1_on;
#ifdef PHOTO_BOOT
    else if (!strcmp(what, "intro"))     /* the intro still running */
        *got = intro.on;
#endif
    else if (!strcmp(what, "key"))
        *got = set.key;
    else if (!strcmp(what, "chords"))
        *got = set.chords;
    else if (!strcmp(what, "beat"))
        *got = eng_beat_on();
    else if (!strcmp(what, "fuzz_off"))
        *got = set.fuzz_off;
    else if (!strcmp(what, "filling"))
        *got = eng_filling();
    else if (!strcmp(what, "beat_var"))
        *got = set.beat_var;
    else if (!strcmp(what, "bass"))
        *got = set.bass;
    else if (!strcmp(what, "two_cores"))
        *got = set.two_cores;
    else if (!strcmp(what, "midi_ch"))
        *got = set.midi_ch;
    else if (!strcmp(what, "beat_level"))
        *got = set.beat_level;
    else if (!strcmp(what, "tempo"))
        *got = tempo_now();
    else if (!strcmp(what, "step"))      /* sixteenths since the beat started */
        *got = (int)eng_step();
    else if (!strcmp(what, "notes")) {   /* notes the keys hold */
        int i, n = 0;
        for (i = 0; i < 128; i++)
            n += note_ref[i] != 0;
        *got = n;
    }
    else
        return 0;
    return 1;
}
#else
int app_expect(const char *what, int *got)
{
    (void)what, (void)got;
    return 0;
}
#endif
