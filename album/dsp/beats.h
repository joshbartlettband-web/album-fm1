/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Alive-Stable-7254 */
/* Album 01: the beat library. Any patch can play one of these instead of its own (SEQ held and PRESET); each is one
 * bar of sixteenths a drum, as a patch's own beat is ('.' a rest, '-' 'o' 'x' soft, medium, full). Kept soft and
 * muffled, partly into the reverse reverb, as the patches' own are. After these come the beats made in the beat
 * maker (album/beats/*.json, tools/beats.py: beats_mine.h). Names: 16 characters at most, for the screen */
#include "beats_mine.h"

static const beat_entry_t BEATS_LIB[] = {
    /* shoegaze and dream-pop: a wash of open hat, the snare far back in the reverb */
    {"DREAM POP", {.bpm = 92.0f, .tone = 4200.0f, .send = 0.5f, .level = 0.85f,
        .kick = "x.......x.x.....", .snare = "....o.......o...", .hat = "-.-.-.-.-.-.-.-.", .open = "..............-."}},
    {"SHOEGAZE WASH", {.bpm = 80.0f, .tone = 3600.0f, .send = 0.65f, .level = 0.9f,
        .kick = "x.....x...x.....", .snare = "....x.......x...", .hat = "..-...-...-...-.", .open = "o.......o.......",
        .fill = "....x.....x.xxxx"}},
    {"SHOEGAZE DRIVE", {.bpm = 116.0f, .tone = 5000.0f, .send = 0.45f, .level = 0.6f,
        .kick = "x.x...x.x.x...x.", .snare = "....x.......x...", .hat = "o.o.o.o.o.o.o.o.", .open = "......-.......-.",
        .fill = "....x.......xxxx"}},
    /* trip-hop: slow, heavy, swung, dusty */
    {"TRIP-HOP", {.bpm = 84.0f, .swing = 0.18f, .tone = 2600.0f, .send = 0.35f, .level = 0.95f,
        .kick = "x......x..x.....", .snare = "....x.......x...", .hat = "-.o.-.o.-.o.-.oo"}},
    {"TRIP-HOP DUSTY", {.bpm = 76.0f, .swing = 0.22f, .tone = 1700.0f, .send = 0.3f, .level = 1.0f,
        .kick = "x.....x.....x...", .rim = "....x.......x..-", .hat = "o.-.o.-.o.-.o.-."}},
    {"TRIP-HOP SWING", {.bpm = 90.0f, .swing = 0.32f, .tone = 3000.0f, .send = 0.3f, .level = 0.9f,
        .kick = "x.......x..x..x.", .snare = "....x..-....x...", .hat = "x.o.x.o.x.o.x.o."}},
    /* motorik: the steady straight road */
    {"MOTORIK", {.bpm = 120.0f, .tone = 3800.0f, .send = 0.25f, .level = 0.8f,
        .kick = "x...x...x...x...", .snare = "....x.......x...", .hat = "o.o.o.o.o.o.o.o."}},
    {"MOTORIK SLOW", {.bpm = 100.0f, .tone = 2400.0f, .send = 0.4f, .level = 0.75f,
        .kick = "x...x...x...x...", .snare = "....o.......o...", .hat = "-o-o-o-o-o-o-o-o"}},
    /* half-time: a fast tempo, the snare once a bar */
    {"HALF-TIME", {.bpm = 140.0f, .tone = 4000.0f, .send = 0.4f, .level = 0.85f,
        .kick = "x.........x.....", .snare = "........x.......", .hat = "o.-.o.-.o.-.o.-."}},
    {"HALF-TIME HEAVY", {.bpm = 150.0f, .tone = 3000.0f, .send = 0.5f, .level = 0.65f,
        .kick = "x.x.......x..x..", .snare = "........x......-", .hat = "x...x...x...x...", .open = "......o.........",
        .fill = "........x.x.xxxx"}},
    /* brushes */
    {"BRUSHES BALLAD", {.bpm = 66.0f, .tone = 1500.0f, .send = 0.5f, .level = 1.15f, .brush = 1,
        .kick = "x.......-.......", .snare = "-.-.o.-.-.-.o.-."}},
    {"BRUSHES SHUFFLE", {.bpm = 96.0f, .swing = 0.33f, .tone = 2000.0f, .send = 0.4f, .level = 0.9f, .brush = 1,
        .kick = "x.....-.x.....-.", .snare = "-.o.-.o.-.o.-.o.", .hat = "....x.......x..."}},
    {"BRUSHES SLOW", {.bpm = 58.0f, .tone = 1200.0f, .send = 0.6f, .level = 1.15f, .brush = 1,
        .kick = "x...............", .snare = "-...o...-...o...", .rim = "......-.......-."}},
    /* broken beats: the kick and snare off the grid's obvious places */
    {"BROKEN BEAT", {.bpm = 100.0f, .swing = 0.12f, .tone = 3400.0f, .send = 0.3f, .level = 0.85f,
        .kick = "x..x...x..x.....", .snare = "....x..-..-.x...", .hat = "-.o..o-.o.-..o.-"}},
    {"BROKEN SHUFFLE", {.bpm = 94.0f, .swing = 0.25f, .tone = 2800.0f, .send = 0.3f, .level = 0.85f,
        .kick = "x.....x..x...x..", .snare = "....x.-.....x.-.", .hat = "o.-o.-o.-o.-o.-.", .tom = "..........-....."}},
    {"BROKEN STEPPER", {.bpm = 128.0f, .tone = 3600.0f, .send = 0.3f, .level = 0.72f,
        .kick = "x..x..x...x..x..", .snare = ".......-......-.", .hat = "-.o.-.o.-.o.-.o.", .rim = "....x.......x..."}},
};
#define BEATS_LIB_N (sizeof BEATS_LIB / sizeof BEATS_LIB[0])
