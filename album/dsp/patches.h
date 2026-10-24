/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Album 01, m b v: the patches. The first six come from the fire pit photo (photo 0); each later patch brings its own
 * photo. Every patch shares the four knobs: Glide (how far pitch wanders), Swell (the reverse reverb's length), Tone
 * (dark to bright) and Blur (clear to washed out). Included by engine.c only. */
#pragma once
#include "engine.h"

static const patch_t PATCHES[] = {
    /* Red Door: the red wash over everything. The main glide chord bed: three detuned saws bent all the time, heard
     * mostly through the reverse reverb. */
    {.name = "RED DOOR", .photo = 0, .wave = WAVE_SAW, .oscs = 3, .detune = 16.0f, .sub = 0.12f,
     .cut_lo = 520.0f, .cut_hi = 6200.0f, .res = 0.12f, .fenv = 0.7f, .fenv_decay = 1.4f, .breath = 0.0f,
     .att = 0.12f, .dec = 1.8f, .sus = 0.82f, .rel = 1.4f,
     .arm_cents = 85.0f, .drift_cents = 9.0f, .arm_rate = 1.1f,
     .swell_lo = 0.14f, .swell_hi = 1.35f, .wet_lo = 0.45f, .wet_hi = 1.0f, .diffuse = 0.85f,
     .tape = 0.45f, .level = 1.0f, .knob = {0.55f, 0.55f, 0.45f, 0.82f}},
    /* Ember: the fire pit. A warm low drone with a quiet crackle; the filter breathes. */
    {.name = "EMBER", .photo = 0, .wave = WAVE_SAW, .oscs = 2, .detune = 7.0f, .octave = -12.0f, .sub = 0.55f,
     .crackle = 0.6f, .cut_lo = 140.0f, .cut_hi = 1900.0f, .res = 0.28f, .fenv = 0.0f, .fenv_decay = 1.0f,
     .breath = 0.55f, .att = 0.9f, .dec = 2.5f, .sus = 1.0f, .rel = 2.6f,
     .arm_cents = 28.0f, .drift_cents = 6.0f, .arm_rate = 0.5f,
     .swell_lo = 0.25f, .swell_hi = 1.4f, .wet_lo = 0.3f, .wet_hi = 0.9f, .diffuse = 0.7f,
     .tape = 0.6f, .level = 1.15f, .knob = {0.3f, 0.6f, 0.35f, 0.6f}},
    /* Lantern: the lamp by the door. A small, clear FM bell with a short reverse swell: the one sharp point in the
     * haze. */
    {.name = "LANTERN", .photo = 0, .wave = WAVE_FM, .oscs = 2, .detune = 5.0f, .fm_ratio = 3.5f,
     .fm_index = 2.4f, .fm_decay = 0.9f, .cut_lo = 2400.0f, .cut_hi = 12000.0f, .res = 0.0f,
     .att = 0.002f, .dec = 2.6f, .sus = 0.0f, .rel = 1.8f,
     .arm_cents = 16.0f, .drift_cents = 3.0f, .arm_rate = 0.8f,
     .swell_lo = 0.08f, .swell_hi = 0.55f, .wet_lo = 0.22f, .wet_hi = 0.8f, .diffuse = 0.5f,
     .tape = 0.2f, .level = 1.1f, .knob = {0.2f, 0.35f, 0.6f, 0.45f}},
    /* Shiver: the cold night. A bright chord through two tremolos at unrelated speeds, left and right (two amps
     * facing each other). */
    {.name = "SHIVER", .photo = 0, .wave = WAVE_PULSE, .oscs = 2, .detune = 10.0f, .pw = 0.42f,
     .cut_lo = 1100.0f, .cut_hi = 8000.0f, .res = 0.1f, .fenv = 0.3f, .fenv_decay = 0.6f,
     .att = 0.02f, .dec = 0.9f, .sus = 0.88f, .rel = 0.8f,
     .arm_cents = 30.0f, .drift_cents = 5.0f, .arm_rate = 1.4f,
     .trem_l = 5.3f, .trem_r = 7.9f, .trem_depth = 0.85f,
     .swell_lo = 0.1f, .swell_hi = 0.75f, .wet_lo = 0.18f, .wet_hi = 0.85f, .diffuse = 0.6f,
     .tape = 0.35f, .level = 0.95f, .knob = {0.3f, 0.3f, 0.65f, 0.35f}},
    /* Blur: the moving figures. A lead that slides between notes and never lands cleanly. */
    {.name = "BLUR", .photo = 0, .wave = WAVE_SAW, .oscs = 2, .detune = 9.0f, .mono = 1, .slide = 0.32f,
     .cut_lo = 800.0f, .cut_hi = 6500.0f, .res = 0.3f, .fenv = 0.5f, .fenv_decay = 0.9f,
     .att = 0.07f, .dec = 1.2f, .sus = 0.9f, .rel = 0.9f,
     .arm_cents = 110.0f, .drift_cents = 12.0f, .arm_rate = 1.3f,
     .swell_lo = 0.15f, .swell_hi = 1.1f, .wet_lo = 0.35f, .wet_hi = 0.95f, .diffuse = 0.8f,
     .tape = 0.4f, .level = 1.05f, .knob = {0.6f, 0.5f, 0.5f, 0.65f}},
    /* Flare: the violet flare. A high shimmer, an octave up, that blooms in after each note. */
    {.name = "FLARE", .photo = 0, .wave = WAVE_TRI, .oscs = 3, .detune = 8.0f, .octave = 12.0f,
     .cut_lo = 2200.0f, .cut_hi = 11000.0f, .res = 0.05f, .hp = 380.0f,
     .att = 1.1f, .dec = 2.2f, .sus = 0.9f, .rel = 2.4f,
     .arm_cents = 40.0f, .drift_cents = 7.0f, .arm_rate = 0.7f,
     .swell_lo = 0.35f, .swell_hi = 1.45f, .wet_lo = 0.65f, .wet_hi = 1.0f, .diffuse = 1.0f,
     .tape = 0.2f, .level = 1.25f, .knob = {0.35f, 0.8f, 0.7f, 0.9f}},
};
#define NPATCH ((int)(sizeof PATCHES / sizeof PATCHES[0]))
