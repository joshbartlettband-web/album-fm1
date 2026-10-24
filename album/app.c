/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album 01: a few deep sounds and the photo each one came from. The keys play; PRESET picks the patch (and its
 * photo); the four knobs are Glide, Swell, Tone and Blur on every patch, and each moves the photo as it moves the
 * sound. ARP turns chord mode on (chords.h) and SELECT picks its key (or hold ARP and press a key); SEQ starts the
 * patch's beat and ALGORITHM sets its tempo (with SEQ held, its level). PLAY holds the notes; HOME puts the knobs and
 * the tempo back where the patch starts; SAVE keeps everything; REC links to another FM-1 (or any Bluetooth MIDI
 * device), held a second it turns Bluetooth off or on.
 *
 * Part of the unity build (base/device/unity.c, base/host/plat_host.c), after the platform: gfx.c (the canvas, the
 * fonts), announce.c (the screen reader) and lib/btdev.c are in scope. The sound is dsp/engine.c, compiled apart. */
#include "fastmath.h"
#include "engine.h"
#include "photos.h"

#ifndef PLAT_HOST
#include "cpu1/cpu1.c"
#else
static uint8_t c1_on;

static int c1_kick(void) { return 0; }
static int c1_join(void) { return 1; }
static int c1_start(void) { return 0; }
#endif

static const char *const KNOB_NAME[NKNOB] = {"GLIDE", "SWELL", "TONE", "BLUR"};
static inline float sine_wave(float x) { return fm_sinf(x * FM_TWO_PI); }
#define CC_KNOB0 16u                     /* MIDI CC 16..19: the four knobs (in and out) */
#define MAX_PATCH 16u

/* --------------------------------------------------------------- settings --- */
#define SET_MAGIC 0x414C4235u            /* "ALB5": chord mode and the beats (earlier layouts start fresh) */
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
    uint8_t pad;
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
    set.chords = 0;
    set.beat_level = 70;
    for (p = 0; p < MAX_PATCH; p++)
        set.tempo[p] = 0;
    for (p = 0; p < MAX_PATCH; p++)
        for (k = 0; k < NKNOB; k++)
            set.knob[p][k] = (uint8_t)(p < (uint32_t)eng_patch_count()
                                           ? (int)(eng_patch_info((int)p)->knob[k] * 255.0f + 0.5f) : 128);
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
    eng_beat_level((float)set.beat_level * 0.01f);
}

/* ----------------------------------------------------------------- render --- */
/* MIDI in, from the render: notes straight to the engine; knobs, patches and panic to the main loop, which shows
 * them (and does not send them back out: two linked FM-1s would echo forever) */
static volatile int32_t midi_patch_req = -1;
static volatile uint8_t midi_knob_req[NKNOB], midi_knob_mask, midi_panic_req, midi_hold_req;
static volatile uint32_t midi_count;

static void midi_in(void)
{
    uint32_t pkt;
    int port;
    while (plat_midi_in(&pkt, &port)) {
        uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
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
            else if (d1 >= CC_KNOB0 && d1 < CC_KNOB0 + NKNOB) {
                eng_knob((int)(d1 - CC_KNOB0), (float)d2 / 127.0f);
                midi_knob_req[d1 - CC_KNOB0] = (uint8_t)d2;
                midi_knob_mask |= (uint8_t)(1u << (d1 - CC_KNOB0));
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
        || set.chords > 1u || set.beat_level > 100u)
        settings_defaults();
    if (set.patch >= (uint32_t)eng_patch_count())
        set.patch = 0;
    eng_patch(set.patch);
    knobs_to_engine();
    beat_to_engine();
#ifndef PLAT_HOST
    if (!set.one_core && !(plat_buttons() >> B_OCTDN & 1u))   /* OCT- held at power-on: one core, this time */
        c1_start();
    else
        c1_why = C1_WHY_OFF;
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
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        float r = src[3u * i], g = src[3u * i + 1u], b = src[3u * i + 2u];
        int fire = r > 190.0f && g > 110.0f;   /* the light's warm whites and orange: sun, a lamp */
        if (r > g + 30.0f && r > b)      /* the red light swells */
            r += (255.0f - r) * lift;
        r *= bright;
        g *= bright;
        b *= bright;
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

static void photo_strip(uint32_t y0)     /* rows y0 .. y0 + STRIP of the photo into the canvas, with the knobs' marks */
{
    const photo_t *p = &PHOTOS[ph.photo];
    float glide = knob_val(K_GLIDE), blur = knob_val(K_BLUR), arm = eng_arm();
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
                out[x] = ph.pal[row[sx]];
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

/* ---------------------------------------------------------------------- UI --- */
static struct {
    char msg[2][24];                     /* a message over the photo: the patch, a knob, a button */
    uint32_t msg_until, knob_until;
    int8_t knob_shown;                   /* the knob whose bar is up, -1 none */
    uint32_t keys, btn;
    int16_t key_notes[NKEYS][8];         /* the notes each held key started (one, or a chord) */
    uint8_t key_n[NKEYS], key_vel[NKEYS], key_arp[NKEYS];   /* how many; its velocity; sent to the arpeggio */
    uint8_t arp_used;                    /* a key pressed while ARP was held: the key set, chord mode left as it is */
    uint8_t seq_used;                    /* ALGORITHM turned while SEQ was held: the level set, the beat left as it is */
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

static void key_down(uint32_t k, uint32_t v, int quiet)
{
    int bass = clamp(53 + (int32_t)k + 12 * set.octave, 0, 127), i;
    const patch_t *p = eng_patch_info(set.patch);
    ui.key_vel[k] = (uint8_t)v;
    if (set.chords) {
        char name[16];
        ui.key_n[k] = (uint8_t)chord_build(bass, set.key, p->chord, ui.key_notes[k], name);
        ui.key_arp[k] = p->mono;
        if (!quiet) {
            app_say(name, "");
            ui.msg_until = plat_ms() + 900u;
        }
    } else {
        ui.key_notes[k][0] = (int16_t)bass;
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

static void chords_set(int on)
{
    set.chords = (uint8_t)(on != 0);
    set_dirty = 1;
    keys_again();
    app_say("CHORDS", set.chords ? KEY_NAME(set.key) : "OFF");
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

static void beat_level_step(int32_t d)
{
    char s[8];
    set.beat_level = (uint8_t)clamp((int32_t)set.beat_level + d, 0, 100);
    set_dirty = 1;
    beat_to_engine();
    int_text(set.beat_level, s);
    app_say("BEAT LEVEL", s);
}

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
        midi_send3(0xB0u, CC_KNOB0 + k, (uint32_t)v255 >> 1);
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
        case B_OCTDN:
            octave_step(-1);
            break;
        case B_OCTUP:
            octave_step(1);
            break;
        case B_PLAY:
            hold_set(!ui.hold);
            break;
        case B_HOME: {                   /* the knobs back where the patch starts */
            uint32_t j;
            for (j = 0; j < NKNOB; j++)
                knob_set(j, (int32_t)(eng_patch_info(set.patch)->knob[j] * 255.0f + 0.5f), 1);
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
        case B_SEQ:                      /* the beat, on its release (unless its level was set while it was held) */
            ui.seq_used = 0;
            break;
        default:
            break;
        }
    }
    if (!(btn >> B_ARP & 1u) && (ui.btn >> B_ARP & 1u) && !ui.arp_used)
        chords_set(!set.chords);
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
        } else if (k == EN_ALGO) {       /* the tempo; with SEQ held, the beat's level */
            int32_t step = (e > 2 || e < -2) ? 4 : 1;
            if (btn >> B_SEQ & 1u) {
                ui.seq_used = 1;
                beat_level_step(e * step);
            } else {
                tempo_step(e * step);
            }
        } else if (k == EN_SELECT) {     /* chord mode's key */
            key_set((int)set.key + (e > 0 ? 1 : -1));
        } else if (k >= EN_K1 && k <= EN_K4) {
            int32_t step = (e > 2 || e < -2) ? 10 : 5;     /* a fast turn goes further */
            knob_set(k - EN_K1, (int32_t)set.knob[set.patch][k - EN_K1] + e * step, 1);
        }
    }
    ui.keys = keys;
    ui.btn = btn;
    plat_glow(1);
    plat_leds((set_dirty ? 1u << B_SAVE : 0u) | (ui.hold ? LED_PLAY_GREEN | 1u << B_PLAY : 0u)
                  | (set.chords ? 1u << B_ARP : 0u)
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

static void overlay(uint32_t y0, uint32_t now)
{
    if (y0 == 0 && now < ui.msg_until) {
        shade(0, 34);
        cv_text(10, 6, &FONT_B, ui.msg[0], RGB(255, 236, 220));
        if (ui.msg[1][0])
            cv_text(240 - 10 - text_w(&FONT_XS, ui.msg[1]), 11, &FONT_XS, ui.msg[1], RGB(255, 120, 110));
    }
    if (y0 == 0 && FM1B_BLE_CENTRAL && plat_ble_device() == BLE_DEV_READY && now >= ui.msg_until)
        cv_text(206, 6, &FONT_XS, "LINK", RGB(255, 140, 120));
    if (y0 == 240u - STRIP && ui.knob_shown >= 0 && now < ui.knob_until) {
        uint32_t k = (uint32_t)ui.knob_shown, w = (uint32_t)(knob_val(k) * 196.0f + 0.5f);
        char v[8];
        shade(10, 38);
        cv_text(12, 14, &FONT_XS, KNOB_NAME[k], RGB(255, 236, 220));
        int_text((int32_t)(knob_val(k) * 100.0f + 0.5f), v);
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
        photo_strip(y);
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
    show_patch();
}

void app_frame(void)
{
    uint32_t now = plat_ms(), k;
    float glide = knob_val(K_GLIDE), lv = eng_level();
    static uint32_t last_ms;
    static uint8_t had_msg, had_knob, had_link, had_beat;
    uint8_t link = (uint8_t)(FM1B_BLE_CENTRAL && plat_ble_device() == BLE_DEV_READY);
    input();
    speak();
    voices_back(now);
    /* what came in over MIDI that the screen shows */
    if (midi_patch_req >= 0) {
        int32_t p = midi_patch_req;
        midi_patch_req = -1;
        patch_set(p, 0);
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
    else if (!strcmp(what, "key"))
        *got = set.key;
    else if (!strcmp(what, "chords"))
        *got = set.chords;
    else if (!strcmp(what, "beat"))
        *got = eng_beat_on();
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
