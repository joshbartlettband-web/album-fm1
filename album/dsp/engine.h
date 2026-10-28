/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album's sound engine: detuned voices that never sit still in pitch (the tremolo arm of glide guitar), through a
 * fully wet reverse reverb, a two-speed tremolo and a little tape.
 *
 * The render is four steps so that the FM-1's two cores can share it:
 *   eng_begin        core 0: the queued notes and patch changes, the knobs, the arm's curve for this block
 *   eng_voices_a     core 0: the first half of the voices
 *   eng_voices_b     core 1 (or core 0 alone): the second half of the voices, and the reverse reverb of the last
 *                    block's mix (a block late, 5.8 ms, inside a swell of 80 ms or more)
 *   eng_end          core 0: the two halves summed, the dry and the wet mixed, tremolo, tape, out
 * Between eng_begin and eng_end each core touches only its own voices and buffers; everything shared is written
 * in eng_begin, before the second core starts, and read by both. */
#pragma once
#include <stdint.h>

#define ENG_FS 44100.0f
#define ENG_VOICES 12u                   /* 6 a core */
#define ENG_HALF (ENG_VOICES / 2u)
#define ENG_BLOCK_MAX 256u
#define ENG_SUB 16u                      /* samples between pitch, filter and tremolo updates */

/* K_FUZZ: ALGORITHM; K_FZLO, K_FZMID, K_FZHI: the fuzz's own EQ (FX held and K1 to K3), 0.5 flat */
enum { K_GLIDE, K_SWELL, K_TONE, K_BLUR, K_FUZZ, K_FZLO, K_FZMID, K_FZHI, NKNOB };
enum { WAVE_SAW, WAVE_PULSE, WAVE_TRI, WAVE_SINE, WAVE_FM };
/* chord mode's voicings, one a patch (the voicings themselves are the app's: chords.h) */
enum { CH_OPEN, CH_ORGAN, CH_POWER, CH_DARK, CH_SPREAD, CH_CLUSTER, CH_SEVENTH, CH_FIFTHS, CH_ARP, CH_ICE, CH_TALL,
       NCHORD };

/* a patch's beat: one bar of sixteenths a drum, written as text, '.' a rest and '-' 'o' 'x' soft, medium, full */
typedef struct {
    float bpm;
    float swing;                         /* the off sixteenths late by this share of a sixteenth, 0..0.5 */
    float tone;                          /* the kit's low-pass (Hz): how muffled */
    float send;                          /* how much of the kit goes into the reverse reverb, 0..1 */
    float level;
    uint8_t brush;                       /* 1: the snare is a brush */
    const char *kick, *snare, *hat, *open, *tom, *rim;
    const char *fill;                    /* the snare in every fourth bar, if any (the roll into the next phrase) */
} beat_t;

typedef struct {
    const char *name;
    uint8_t photo;                       /* the photo it shows (photos/photos.json order) */
    uint8_t wave;                        /* WAVE_* */
    uint8_t oscs;                        /* 1..3 oscillators a voice, detuned against each other */
    uint8_t mono;                        /* 1: one voice, sliding from note to note */
    float detune;                        /* cents between the outer oscillators */
    float octave;                        /* semitones added to every note */
    float interval;                      /* semitones added to the third oscillator (7: an open fifth), 0: none */
    float sub;                           /* a sine an octave down, 0..1 */
    float crackle;                       /* sparse noise clicks while a note sounds, 0..1 */
    float noise;                         /* air: white noise beside the oscillators, through the filter, 0..1 */
    float pw;                            /* WAVE_PULSE: pulse width */
    float fm_ratio, fm_index, fm_decay;  /* WAVE_FM: modulator ratio, its index, and how fast the index falls (s) */
    float cut_lo, cut_hi;                /* low-pass cutoff (Hz) at Tone 0 and Tone 1 */
    float res;                           /* resonance 0..1 */
    float fenv, fenv_decay;              /* filter envelope: octaves off the cutoff at a note's start (below it if <0), its return (s) */
    float breath;                        /* a slow sway of the cutoff, octaves */
    float hp;                            /* high-pass (Hz), 0: none */
    float att, dec, sus, rel;            /* the level's envelope: s, s, 0..1, s */
    float arm_cents;                     /* the tremolo arm's reach at Glide 1 (cents, down) */
    float drift_cents;                   /* each oscillator's own wander at Glide 1 (cents) */
    float arm_rate;                      /* how often the arm moves (moves a second) */
    float slide;                         /* mono: the slide between notes (s) */
    float trem_l, trem_r, trem_depth;    /* tremolo: its two speeds (Hz), left and right, and depth 0..1 */
    uint8_t trem_square;                 /* 1: the tremolo chops (a square, its edges softened) instead of swaying */
    uint8_t trem_pan;                    /* 1: the tremolo pans the whole sound, reverb too: right opposite left, at trem_l */
    float swell_lo, swell_hi;            /* the reverse reverb's window (s) at Swell 0 and Swell 1 */
    float wet_lo, wet_hi;                /* the reverb's share at Blur 0 and Blur 1 (1: no dry at all) */
    float diffuse;                       /* how much Blur smears the reverb, 0..1 */
    float tape;                          /* saturation and wow, 0..1 */
    float level;                         /* the patch's loudness trim */
    float knob[NKNOB];                   /* the knobs where the patch starts */
    uint8_t chord;                       /* CH_*: how chord mode voices a chord on this patch */
    uint8_t arp_div;                     /* mono patches in chord mode: sixteenths between the arpeggio's notes */
    const beat_t *beat;                  /* its beat */
} patch_t;

void eng_init(void);
/* from the main loop (the panel); the render's MIDI uses the _isr forms. Both are queued and played at the next
 * block's start */
void eng_note_on(int note, int vel);
void eng_note_off(int note);
void eng_sustain(int on);
void eng_patch(int p);
void eng_panic(void);
void eng_note_on_isr(int note, int vel);
void eng_note_off_isr(int note);
void eng_sustain_isr(int on);
void eng_patch_isr(int p);
void eng_knob(int k, float v);           /* 0..1, any context: the next block takes it */
void eng_fuzz_on(int on);                /* FX: the fuzz in (1) or bypassed (0), its knob kept */
void eng_fill(int held);                 /* FILL down (1): a fill to the bar's end, on while held; up (0) */
void eng_beat_var(int v);                /* 0 sparse, 1 as written, 2 busy */
void eng_bass(int on);                   /* the auto bass: the chord's root, on the kick, while the beat runs */
void eng_bass_root(int note);            /* the note it plays (any octave: it finds its own) */
int eng_filling(void);                   /* a fill is playing */
void eng_arp_note(int note, int on);     /* a mono patch in chord mode: a note into (or out of) the arpeggio */

/* the beat and the clock it shares with the arpeggio. SEQ starts it at a bar's start; a patch change keeps its place.
 * Clock in (MIDI F8, from the render) takes over from the tempo while it comes, as a linked FM-1's does */
void eng_beat(int on);                   /* main loop */
void eng_tempo(float bpm);               /* main loop: this patch's tempo */
void eng_beat_level(float v);            /* 0..2: 0.7 the kit as written */
void eng_clock_in(int what);             /* the render's MIDI: 0xF8 a tick, 0xFA start, 0xFC stop */
int eng_beat_on(void);
uint32_t eng_ticks(void);                /* clock ticks (24 a beat) since the beat started: the main loop sends them */
uint32_t eng_step(void);                 /* sixteenths since the beat started */
float eng_pulse(void);                   /* the kick, 0..1, decaying: the photo's pulse */
int eng_clock_ext(void);                 /* 1 while a clock comes in */

int eng_patch_count(void);
const patch_t *eng_patch_info(int p);
float eng_level(void);                   /* the output's level, 0..~1, smoothed: the screen's flicker */
float eng_arm(void);                     /* the arm now, -1 (bent all the way down) .. 0: the screen's waver */
uint32_t eng_voices_on(void);            /* voices sounding */
int eng_voice_cap(int n);                /* voices allowed (the overload's shedding); returns the cap */

void eng_begin(uint32_t n);
void eng_voices_a(void);
void eng_voices_b(void);
void eng_end(int32_t *lr, uint32_t n, float gain);
void eng_drop_b(void);                   /* core 1 failed mid-job: its half is silence this block, its state reset */
int eng_b_sane(void);                    /* core 1's results look like numbers (a misread can leave NaN): 1 = yes */
