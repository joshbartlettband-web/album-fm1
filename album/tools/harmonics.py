#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alive-Stable-7254
"""Album's harmonics test: does what sounds match what is played?

  python3 album/tools/video/tour.py DIR --probe
  album/build/host/album_host DIR/tour.txt DIR > DIR/run.log
  python3 album/tools/harmonics.py DIR [--max-stray 0.6] [--max-stuck 0]

What should sound comes from the script, not from the firmware: the keys tour.py pressed (tour.json's key_events),
each with the notes the FM-1 sent out over MIDI as it went down (midi_out.txt), and the chord the loop holds
(heldnotes). Against that, two things are measured, every 100 ms (heldnotes in run.log):

  stuck   notes the engine holds that no key down and no loop asked for: a chord that never let go. Always a bug.
  stray   the share of the sound (80 Hz to 4 kHz, seg1.wav) that is not on a note played now or its first four
          harmonics (within half a semitone, so the bends and the detune count as the note). The rest is tails:
          notes let go and the reverb of chords gone by. A wet patch always has some; a lot means they tangle.

It prints each scene (tour.json's captions) with its stray and any stuck notes, and exits 1 if a scene's stray is
over --max-stray or anything is stuck more than --max-stuck times (100 ms each)."""
import json
import re
import sys
import wave
from pathlib import Path

import numpy as np

GRACE = 120          # ms after a key lets go before its notes count as stuck (a block, a frame, the queue)
PRESS = 30           # ms after a key goes down in which its MIDI note-ons are its notes


def load(d):
    meta = json.loads((d / "tour.json").read_text())
    probes = []
    for ln in (d / "run.log").read_text().splitlines():
        m = re.match(r"heldnotes (\d+) oct (-?\d+) int (\d+): held([\d ]*) ; loop([\d ]*)$", ln)
        if m:
            probes.append((int(m.group(1)), int(m.group(2)), int(m.group(3)),
                           [int(x) for x in m.group(4).split()], [int(x) for x in m.group(5).split()]))
    ons = []                                         # (ms, note): the note-ons that went out
    for ln in (d / "midi_out.txt").read_text().splitlines():
        m = re.match(r"(\d+) ms: 9([0-9A-F]) ([0-9A-F]{2}) ([0-9A-F]{2})", ln)
        if m and int(m.group(4), 16):
            ons.append((int(m.group(1)), int(m.group(3), 16)))
    return meta, probes, ons


def intended_by_keys(meta, ons):
    """[(down ms, up ms, notes)] for every key press: its notes, the note-ons just after it went down"""
    presses, down = [], {}
    for t, k, is_down in meta["key_events"]:
        if is_down:
            down[k] = t
        elif k in down:
            t0 = down.pop(k)
            presses.append((t0, t, {n for tt, n in ons if t0 <= tt <= t0 + PRESS}))
    return presses


def spectrum_share(x, fs, t, notes, oct_, interval):
    """the share of 80 Hz .. 4 kHz on the notes' first four harmonics (and the patch's interval and octave)"""
    n = 4096
    s = int(t * fs)
    if s < 0 or s + n > len(x):
        return None, None
    seg = x[s:s + n] * np.hanning(n)
    p = np.abs(np.fft.rfft(seg)) ** 2
    f = np.fft.rfftfreq(n, 1 / fs)
    band = (f >= 80) & (f <= 4000)
    total = p[band].sum()
    level = 10 * np.log10(total / n + 1e-20)
    if not notes:
        return 0.0, level
    cents = 1200 * np.log2(np.maximum(f, 1e-6) / 440.0)
    on = np.zeros_like(band)
    for note in notes:
        for base in {note + oct_, note + oct_ + interval, note + oct_ - 12}:
            for k in (1, 2, 3, 4):
                c = (base - 69) * 100 + 1200 * np.log2(k)
                on |= np.abs(cents - c) <= 50
    return float(p[band & on].sum() / total), level


def main(argv):
    d = Path(argv[1])
    max_stray = float(argv[argv.index("--max-stray") + 1]) if "--max-stray" in argv else 0.6
    max_stuck = int(argv[argv.index("--max-stuck") + 1]) if "--max-stuck" in argv else 0
    meta, probes, ons = load(d)
    if not probes:
        raise SystemExit("harmonics: no heldnotes lines in run.log (tour.py --probe, and the simulator's output saved)")
    presses = intended_by_keys(meta, ons)
    w = wave.open(str(d / "seg1.wav"))
    fs = w.getframerate()
    x = np.frombuffer(w.readframes(w.getnframes()), "<i2").reshape(-1, 2).astype(np.float64).mean(1) / 32768
    t1 = meta["seg_starts"][1]
    caps, fps = meta["captions"], meta["fps"]
    frames0 = next(c[0] for c in caps if c[1] != "ALBUM 01")             # the first frame of seg1
    scenes = {}
    order = []
    for t, oct_, interval, held, loop in probes:
        if t < t1:
            continue
        keys = set().union(*([n for a, b, n in presses if a <= t < b] or [set()]))
        recent = set().union(*([n for a, b, n in presses if b <= t < b + GRACE] or [set()]))
        intended = keys | set(loop)
        stuck = sorted(set(held) - intended - recent)
        frame = frames0 + int((t - t1) * fps / 1000)
        cap = max((c for c in caps if c[0] <= frame), key=lambda c: c[0])
        name = cap[1] + (" / " + cap[2][:28] if cap[1] in ("RED DOOR", "REC  A CHORD LOOP") else "")
        if name not in scenes:
            scenes[name] = {"stray": [], "stuck": [], "from": (t - t1) / 1000 + frames0 / fps}
            order.append(name)
        share, level = spectrum_share(x, fs, (t - t1) / 1000 + 0.02, sorted(intended), oct_, interval)
        if share is not None and level > -55 and intended:
            scenes[name]["stray"].append(1 - share)
        if stuck:
            scenes[name]["stuck"].append(((t - t1) / 1000 + frames0 / fps, stuck))
    bad = False
    print(f"{'scene':48s} {'at':>6s} {'stray':>6s}  stuck")
    for name in order:
        s = scenes[name]
        stray = float(np.median(s["stray"])) if s["stray"] else float("nan")
        stuck = s["stuck"]
        flag = (s["stray"] and stray > max_stray) or len(stuck) > max_stuck
        bad |= bool(flag)
        note = ""
        if stuck:
            notes = sorted({n for _, ns in stuck for n in ns})
            note = f"{len(stuck)} x 100 ms, notes {notes} from {stuck[0][0]:.1f} s"
        print(f"{name:48s} {s['from']:6.1f} {stray:6.2f}  {note}{'   <-- ' if flag else ''}")
    return 1 if bad else 0


if __name__ == "__main__":
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    sys.exit(main(sys.argv))
