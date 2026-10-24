/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Hausway */
/* Chord mode: a key plays a chord rooted on its own note, and where that note sits in the key decides the chord.
 *
 * The harmony is drawn from My Bloody Valentine's records. Kevin Shields has said that "a lot of the chord
 * progressions are quite basic"; what makes them his is the open tunings, which "leave a lot of room for odd
 * variations of a basic chord", their open strings that "clash against the others in interesting ways", and the
 * inversions (Guitar World, March 1992). So the voicings here are basic chords made odd: suspended seconds and
 * fourths, added ninths and sixths, fifths without thirds, slash chords over a pedal (Who Sees You's A/E and B/F#).
 * Outside the key, the borrowed chords the songs reach for, all four on Only Tomorrow (in G): the flat seventh (F),
 * the flat sixth (Ebmaj7), the flat third (Bb) and the major second (A7); and the flat second, for a darker turn. So
 * every key on the keyboard plays a chord that belongs:
 *
 *   I  bII  ii  bIII  iii  IV  II/3  V  bVI  vi  bVII  V/3      (C D E F G A B in C: the key's own chords)
 *
 * the two slash chords standing in for the diminished seventh (V over its third) and the sharp fourth (II over its
 * third). Each patch voices them its own way (CHORD_STYLES), RED DOOR's with the key's tonic ringing above every
 * chord as an open string would. Included by app.c only. */
#pragma once
#include <stdint.h>
#include "engine.h"

typedef struct {
    int8_t maj[6], min[6], alt[6];       /* intervals above the chord's root; alt the borrowed chords' (0 of them: maj) */
    uint8_t nmaj, nmin, nalt;
    uint8_t drone;                       /* the key's tonic above every chord, an open string ringing */
    uint8_t sus5;                        /* the V chord suspended: its third up to the fourth */
} chord_style_t;

static const chord_style_t CHORD_STYLES[NCHORD] = {
    /* RED DOOR: an open tuning's shapes, root, fifth, ninth and tenth, the second against the third, the tonic
     * droning over all; the V suspended */
    [CH_OPEN] = {.maj = {0, 7, 14, 16}, .min = {0, 7, 14, 15}, .nmaj = 4, .nmin = 4, .drone = 1, .sus5 = 1},
    /* MAPLE: an organ's close chords over a low root, sixths on the major, sevenths on the minor */
    [CH_ORGAN] = {.maj = {-12, 0, 4, 7, 9}, .min = {-12, 0, 3, 7, 10}, .nmaj = 5, .nmin = 5},
    /* DAHLIA: fuzz wants fifths: root, fifth, octave and the third high above */
    [CH_POWER] = {.maj = {0, 7, 12, 16}, .min = {0, 7, 12, 15}, .nmaj = 4, .nmin = 4},
    /* FLAME: low and sparse, an open tenth, nothing to muddy the chop */
    [CH_DARK] = {.maj = {0, 7, 16}, .min = {0, 7, 15}, .nmaj = 3, .nmin = 3},
    /* CLEMATIS: spread wide, add9 and the sixth on top; the V suspended */
    [CH_SPREAD] = {.maj = {0, 7, 14, 16, 21}, .min = {0, 7, 14, 15, 19}, .nmaj = 5, .nmin = 5, .sus5 = 1},
    /* WHIRL: clusters, the second packed against the third */
    [CH_CLUSTER] = {.maj = {0, 2, 4, 7, 12}, .min = {0, 2, 3, 7, 12}, .nmaj = 5, .nmin = 5},
    /* WINDOW: an electric piano's chords, major and minor sevenths with the ninth; the borrowed chords dominant */
    [CH_SEVENTH] = {.maj = {0, 4, 11, 14}, .min = {0, 3, 10, 14}, .alt = {0, 4, 10, 14}, .nmaj = 4, .nmin = 4,
                    .nalt = 4},
    /* PASTURE: each voice already sounds its fifth, so three notes make root, fifth, octave, ninth, tenth and more;
     * the V suspended */
    [CH_FIFTHS] = {.maj = {0, 12, 16}, .min = {0, 12, 15}, .nmaj = 3, .nmin = 3, .sus5 = 1},
    /* BLUESTEM: the chord's tones for the arpeggio */
    [CH_ARP] = {.maj = {0, 4, 7, 12}, .min = {0, 3, 7, 12}, .nmaj = 4, .nmin = 4},
    /* SNOWFALL: sixths and ninths, close at the top, glassy */
    [CH_ICE] = {.maj = {0, 7, 9, 14, 16}, .min = {0, 7, 10, 14, 15}, .nmaj = 5, .nmin = 5},
    /* TOWER: open triads; each voice doubles itself an octave up, so they stand tall */
    [CH_TALL] = {.maj = {0, 7, 16}, .min = {0, 7, 15}, .nmaj = 3, .nmin = 3},
};

enum { Q_MAJ, Q_MIN };
static const struct {
    int8_t root;                         /* the chord's root from the key pressed (-4: the key is its third) */
    uint8_t q, borrowed;
} DEGREE[12] = {
    {0, Q_MAJ, 0},                       /* I */
    {0, Q_MAJ, 1},                       /* bII */
    {0, Q_MIN, 0},                       /* ii */
    {0, Q_MAJ, 1},                       /* bIII */
    {0, Q_MIN, 0},                       /* iii */
    {0, Q_MAJ, 0},                       /* IV */
    {-4, Q_MAJ, 1},                      /* II over its third */
    {0, Q_MAJ, 0},                       /* V */
    {0, Q_MAJ, 1},                       /* bVI */
    {0, Q_MIN, 0},                       /* vi */
    {0, Q_MAJ, 1},                       /* bVII */
    {-4, Q_MAJ, 0},                      /* V over its third */
};

static const char *const NOTE_SHARP[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const NOTE_FLAT[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
#define KEY_NAME(k) NOTE_FLAT[(k) % 12]

static int chord_flats(int key) { return key == 1 || key == 3 || key == 5 || key == 6 || key == 8 || key == 10; }

/* the chord a key plays: its notes, lowest first, into out (8 at most), and its name; returns how many */
static int chord_build(int bass, int key, int style, int16_t *out, char *name)
{
    const chord_style_t *cs = &CHORD_STYLES[style % NCHORD];
    int rel = ((bass - key) % 12 + 12) % 12, root = bass + DEGREE[rel].root, slash = DEGREE[rel].root != 0;
    int sus = rel == 7 && cs->sus5, minor = DEGREE[rel].q == Q_MIN, n = 0, i, j;
    const int8_t *iv = minor ? cs->min : cs->maj;
    int ni = minor ? cs->nmin : cs->nmaj;
    if (DEGREE[rel].borrowed && cs->nalt) {
        iv = cs->alt;
        ni = cs->nalt;
    }
    if (slash)                           /* the key pressed under the chord */
        out[n++] = (int16_t)bass;
    for (i = 0; i < ni && n < 8; i++) {
        int x = root + iv[i];
        if (sus && (iv[i] % 12 == 4))
            x++;
        while (slash && x <= bass)
            x += 12;
        out[n++] = (int16_t)x;
    }
    if (cs->drone && n < 8) {            /* the tonic, the first above an octave over the bass */
        int d = bass + 12 + (((key - bass - 12) % 12) + 12) % 12;
        out[n++] = (int16_t)d;
    }
    for (i = 0; i < n; i++) {            /* in reach: folded into 28 .. 96 */
        while (out[i] > 96)
            out[i] -= 12;
        while (out[i] < 28)
            out[i] += 12;
    }
    for (i = 1; i < n; i++)              /* lowest first, each pitch once */
        for (j = i; j > 0 && out[j] < out[j - 1]; j--) {
            int16_t t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    for (i = 1, j = 1; i < n; i++)
        if (out[i] != out[j - 1])
            out[j++] = out[i];
    n = n ? j : 0;
    if (name) {
        const char *const *nm = (DEGREE[rel].borrowed && rel != 6) || chord_flats(key) ? NOTE_FLAT : NOTE_SHARP;
        name[0] = 0;
        text_cat(name, nm[((root % 12) + 12) % 12]);
        if (minor)
            text_cat(name, "m");
        if (sus)
            text_cat(name, "sus4");
        if (slash) {
            text_cat(name, "/");
            text_cat(name, nm[((bass % 12) + 12) % 12]);
        }
    }
    return n;
}
