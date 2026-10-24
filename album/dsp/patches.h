/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album 01: the patches. Each has its own photo, and its sound is drawn from that photo; patches kept for later
 * are in shelf/. Every patch shares the four knobs: Glide (how far pitch wanders), Swell (the reverse reverb's
 * length), Tone (dark to bright) and Blur (clear to washed out). Included by engine.c only. */
#pragma once
#include "engine.h"

static const patch_t PATCHES[] = {
    /* Maple: the sun through a red leaf, late. A soft reed organ that sways slowly, the street's warmth on tape. */
    {.name = "MAPLE", .photo = 0, .wave = WAVE_PULSE, .oscs = 2, .detune = 6.0f, .pw = 0.5f, .sub = 0.25f,
     .cut_lo = 500.0f, .cut_hi = 4200.0f, .res = 0.08f, .fenv = 0.2f, .fenv_decay = 1.5f,
     .att = 0.35f, .dec = 2.0f, .sus = 0.92f, .rel = 1.6f,
     .arm_cents = 22.0f, .drift_cents = 6.0f, .arm_rate = 0.45f,
     .trem_l = 0.9f, .trem_r = 1.13f, .trem_depth = 0.22f,
     .swell_lo = 0.2f, .swell_hi = 1.2f, .wet_lo = 0.3f, .wet_hi = 0.9f, .diffuse = 0.7f,
     .tape = 0.75f, .level = 1.05f, .knob = {0.3f, 0.45f, 0.4f, 0.55f}},
    /* Dahlia: pink petals open in full sun. Bright fuzz chords, bent hard, the wash behind them rather than over. */
    {.name = "DAHLIA", .photo = 1, .wave = WAVE_SAW, .oscs = 3, .detune = 24.0f, .sub = 0.1f,
     .cut_lo = 1500.0f, .cut_hi = 11000.0f, .res = 0.22f, .fenv = 0.4f, .fenv_decay = 0.5f,
     .att = 0.01f, .dec = 1.2f, .sus = 0.85f, .rel = 0.7f,
     .arm_cents = 70.0f, .drift_cents = 10.0f, .arm_rate = 1.6f,
     .swell_lo = 0.1f, .swell_hi = 0.8f, .wet_lo = 0.15f, .wet_hi = 0.7f, .diffuse = 0.6f,
     .tape = 1.0f, .level = 0.85f, .knob = {0.5f, 0.35f, 0.7f, 0.35f}},
    /* Red Hall: one light hanging in a red room at night. A low chord chopped by a fast tremolo, each side at its
     * own speed, in a long dark space. */
    {.name = "RED HALL", .photo = 2, .wave = WAVE_SAW, .oscs = 2, .detune = 12.0f, .octave = -12.0f, .sub = 0.35f,
     .cut_lo = 260.0f, .cut_hi = 3000.0f, .res = 0.35f, .fenv = 0.5f, .fenv_decay = 2.0f,
     .att = 0.05f, .dec = 2.0f, .sus = 0.9f, .rel = 1.8f,
     .arm_cents = 45.0f, .drift_cents = 7.0f, .arm_rate = 0.8f,
     .trem_l = 6.0f, .trem_r = 4.5f, .trem_depth = 0.9f, .trem_square = 1,
     .swell_lo = 0.3f, .swell_hi = 1.45f, .wet_lo = 0.35f, .wet_hi = 0.95f, .diffuse = 0.9f,
     .tape = 0.5f, .level = 1.1f, .knob = {0.4f, 0.65f, 0.3f, 0.5f}},
    /* Clematis: a bush of small white stars. A breathy pad, half tone and half air, like a voice under the
     * guitars; each note a cloud. */
    {.name = "CLEMATIS", .photo = 3, .wave = WAVE_TRI, .oscs = 3, .detune = 10.0f, .noise = 0.35f,
     .cut_lo = 700.0f, .cut_hi = 5500.0f, .res = 0.55f, .fenv = 0.6f, .fenv_decay = 1.8f, .breath = 0.4f,
     .hp = 160.0f, .att = 0.45f, .dec = 2.4f, .sus = 0.8f, .rel = 2.0f,
     .arm_cents = 35.0f, .drift_cents = 8.0f, .arm_rate = 0.9f,
     .swell_lo = 0.25f, .swell_hi = 1.4f, .wet_lo = 0.5f, .wet_hi = 1.0f, .diffuse = 1.0f,
     .tape = 0.3f, .level = 1.2f, .knob = {0.35f, 0.6f, 0.5f, 0.75f}},
};
#define NPATCH ((int)(sizeof PATCHES / sizeof PATCHES[0]))
