#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alive-Stable-7254
"""The front page's video, part 1: a simulator script that plays the tour and takes the screen 30 times a second.

  python3 album/tools/video/tour.py OUT_DIR       writes OUT_DIR/tour.txt and OUT_DIR/tour.json
  album/build/host/album_host OUT_DIR/tour.txt OUT_DIR
  python3 album/tools/video/compose.py OUT_DIR album/site/img/album-01.mp4
  python3 album/tools/video/tour.py OUT_DIR --probe   the same, and the notes held every 100 ms, for
                                                     tools/harmonics.py (run the simulator with its output saved)

The script keeps the simulator's clock itself: every frame is a "shot" on the 30 a second grid, and each command
costs what the simulator spends on it (a key or a button 2 ms, a detent 1 ms), so the pictures and the sound
(seg0.wav, seg1.wav, ...) stay together. tour.json holds, per frame, the keys held, and the captions."""
import json
import sys
from pathlib import Path

FPS = 30
WHITE_PC = {0, 2, 4, 5, 7, 9, 11}
KEYS = [k for k in range(27)]
WHITES = [k for k in KEYS if (53 + k) % 12 in WHITE_PC]
BLACKS = [k for k in KEYS if (53 + k) % 12 not in WHITE_PC]


def key_index(name):
    return WHITES[int(name[1:])] if name[0] == "w" else BLACKS[int(name[1:])]


class Tour:
    def __init__(self):
        self.lines, self.t, self.rec = [], 0, False
        self.frames, self.caps, self.held = [], [], 0
        self.seg, self.seg_t0, self.seg_n = 0, 0, 0
        self.key_events, self.seg_starts = [], []   # (ms, key, 1 down / 0 up); each sound file's start, ms
        self.probe, self.next_probe = False, 0     # heldnotes every 100 ms while recording (for harmonics.py)

    def _next_frame(self):
        return self.seg_t0 + round(self.seg_n * 1000 / FPS)

    def wait(self, ms):
        target = self.t + ms
        while self.rec and self._next_frame() <= target:
            f = self._next_frame()
            if f > self.t:
                self.lines.append(f"wait {f - self.t}")
                self.t = f
            if self.probe and self.t >= self.next_probe:
                self.lines.append("heldnotes")
                self.next_probe = self.t + 100
            self.lines.append(f"shot f{len(self.frames):05d}.png")
            self.frames.append(self.held)
            self.seg_n += 1
        if target > self.t:
            self.lines.append(f"wait {target - self.t}")
            self.t = target

    def cmd(self, line, cost):
        self.lines.append(line)
        self.t += cost

    def start(self):                     # a stretch of the video: its own sound file
        self.cmd(f"wav seg{self.seg}.wav", 0)
        self.rec, self.seg_t0, self.seg_n = True, self.t, 0
        self.seg_starts.append(self.t)

    def stop(self):                      # (on a frame's edge, so the sound is as long as the pictures)
        self.wait(max(0, self._next_frame() - self.t - 1))
        if self._next_frame() > self.t:
            self.lines.append(f"wait {self._next_frame() - self.t}")
            self.t = self._next_frame()
        self.cmd("wavstop", 0)
        self.rec = False
        self.seg += 1

    def cap(self, title, sub=""):
        self.caps.append([len(self.frames), title, sub])

    def down(self, k, vel=96):
        self.held |= 1 << key_index(k)
        self.key_events.append((self.t, key_index(k), 1))
        self.cmd(f"key {k} down {vel}", 2)

    def up(self, k):
        self.held &= ~(1 << key_index(k))
        self.key_events.append((self.t, key_index(k), 0))
        self.cmd(f"key {k} up", 2)

    def note(self, k, ms, vel=96):
        self.down(k, vel)
        self.wait(ms)
        self.up(k)

    def tap(self, b, ms=60):
        self.cmd(f"press {b}", 2)
        self.wait(ms)
        self.cmd(f"release {b}", 2)

    def spin(self, enc, n, gap=45):      # one detent at a time, so a knob moves its slow step
        for _ in range(abs(n)):
            self.cmd(f"spin {enc} {1 if n > 0 else -1}", 1)
            self.wait(gap)

    def midi(self, *b):
        self.cmd("midi " + " ".join(b), 2)


def tour(probe=False):
    T = Tour()
    T.probe = probe
    # the boot photo melting into the first patch, ALBUM 01 fading
    T.start()
    T.cap("ALBUM 01", "Firmware for the M-VAVE FM-1: twelve sounds, each made from a photo")
    T.wait(4300)
    T.stop()
    T.wait(4900)                         # (the rest of the intro, off camera)
    T.cmd("master 3400", 40)
    T.wait(1200)

    T.start()
    # RED DOOR: four chords, the fuzz at 10 and Glide at 70 (CC 20 and CC 16: the knobs land on the value exactly)
    T.cap("RED DOOR", "Four chords with the fuzz at 10 and Glide at 70")
    T.midi("B0", "14", "0D")
    T.wait(1000)
    T.midi("B0", "10", "59")
    T.wait(1000)
    T.cap("RED DOOR", "C, A minor, F, B flat. One key plays each chord")
    for k in ("w4", "w9", "w7", "b7"):
        T.down(k, 100)
        T.wait(2700)
        T.up(k)
        T.wait(20)
    T.down("w4", 100)
    T.wait(3000)
    T.up("w4")
    T.wait(1800)

    # the five knobs
    T.cap("K1  GLIDE", "How far the chord bends, as a tremolo arm does. The photo wavers with it")
    T.down("w4", 96)
    T.wait(300)
    T.spin("K1", 15)
    T.wait(1600)
    T.spin("K1", -15)
    T.wait(300)
    T.up("w4")
    T.wait(200)
    T.cap("K2  SWELL", "The reverse reverb: a quick lift, or a slow rise")
    T.spin("K2", 12)
    T.wait(200)
    T.note("w9", 2600, 96)
    T.wait(200)
    T.spin("K2", -12)
    T.cap("K3  TONE", "Dark to bright, the reverb too")
    T.down("w7", 96)
    T.wait(300)
    T.spin("K3", -20)
    T.wait(800)
    T.spin("K3", 36)
    T.wait(800)
    T.spin("K3", -16)
    T.wait(200)
    T.up("w7")
    T.cap("K4  BLUR", "Clear to all wash")
    T.down("b7", 96)
    T.wait(300)
    T.spin("K4", -30)
    T.wait(1100)
    T.spin("K4", 30)
    T.wait(600)
    T.up("b7")
    T.cap("ALGORITHM  FUZZ", "A fuzz pedal on the whole chord. FX takes it out and puts it back")
    T.down("w4", 100)
    T.wait(300)
    T.spin("ALGO", 26, 35)
    T.wait(1300)
    T.tap("FX")
    T.wait(1200)
    T.tap("FX")
    T.wait(700)
    T.spin("ALGO", -26, 30)
    T.wait(300)
    T.up("w4")
    T.wait(800)

    # the two effects, under EDIT
    T.cap("EDIT + K1  JET", "A slow jet-plane swirl over everything. The colors swirl with it")
    T.down("w9", 100)
    T.wait(300)
    T.cmd("press EDIT", 2)
    T.spin("K1", 40, 30)
    T.cmd("release EDIT", 2)
    T.wait(4200)
    T.up("w9")
    T.down("w7", 100)
    T.wait(2600)
    T.cmd("press EDIT", 2)
    T.spin("K1", -40, 20)
    T.cmd("release EDIT", 2)
    T.up("w7")
    T.wait(300)
    T.cap("EDIT + K2  ECHO", "A dark tape echo, in time with the tempo. The photo trails it")
    T.cmd("press EDIT", 2)
    T.spin("K2", 33, 30)
    T.cmd("release EDIT", 2)
    T.wait(200)
    for k in ("w4", "w9"):
        T.note(k, 260, 104)
        T.wait(2000)
    T.note("b7", 260, 104)
    T.wait(2600)
    T.cmd("press EDIT", 2)
    T.spin("K2", -33, 20)
    T.cmd("release EDIT", 2)
    T.wait(600)

    # the other ten patches, each with its own photo
    PATCHES = [
        ("MAPLE", "A soft reed organ that sways slowly", "w7"),
        ("DAHLIA", "Bright fuzz chords, bent hard", "w4"),
        ("FLAME", "A low chord chopped in time with the tempo", "w9"),
        ("CLEMATIS", "A breathy pad, half tone and half air", "w7"),
        ("WHIRL", "A wide chord bent deep, carried side to side", "w5"),
        ("WINDOW", "A soft electric piano, rain on the pane", "w8"),
        ("PASTURE", "Open fifths, slow and wide", "w4"),
        ("BLUESTEM", "A reed whistle: the chord comes as an arpeggio", "w9"),
        ("SNOWFALL", "Hollow glass notes sinking into a hush", "w7"),
        ("TOWER", "A tall chord that glows open after each note", "w4"),
        ("LATE BLOOM", "A soft bell, its echoes falling like the leaves", "w7"),
    ]
    for name, what, k in PATCHES:
        T.spin("PRESET", 1, 1)
        T.cap(name, what)
        T.wait(350)
        T.down(k, 100)
        T.wait(2900)
        T.up(k)
        T.wait(500)
    T.spin("PRESET", 1, 1)               # (round to RED DOOR again)
    T.wait(500)

    # how the keys play
    T.cap("CHORD MODE", "Every key plays a chord, the black keys too, spelled on the photo")
    for k in ("w5", "b3", "w8"):
        T.note(k, 1250, 96)
        T.wait(30)
    T.wait(300)
    T.tap("ARP")
    T.cap("ARP  ACCORDION", "Chords on the black keys, a tune on the white keys")
    T.wait(300)
    T.down("b5", 80)
    for k in ("w8", "w9", "w11", "w9"):
        T.note(k, 420, 96)
    T.up("b5")
    T.down("b6", 80)
    for k in ("w10", "w8", "w9"):
        T.note(k, 420, 96)
    T.wait(400)
    T.up("b6")
    T.tap("ARP")
    T.spin("PRESET", 6, 40)              # (WINDOW, for single notes: RED DOOR blooms each one back over the next)
    T.cap("ARP  NOTES", "One note a key. Here on WINDOW")
    T.wait(1200)                         # (ACCORDION's last chord out of the room first)
    for k in ("w4", "w6", "w8", "w11"):
        T.note(k, 560, 96)
    T.wait(900)
    T.tap("ARP")
    T.cap("SELECT  THE KEY", "A semitone a click. Here, up a tone to D")
    T.wait(200)
    T.spin("SELECT", 2, 150)
    T.wait(300)
    T.note("w5", 1700, 100)
    T.spin("SELECT", -2, 60)
    T.cap("OCT-  OCT+", "Two octaves down to two up")
    T.tap("OCT+")
    T.wait(150)
    T.note("w4", 1300, 96)
    T.tap("OCT-")
    T.tap("OCT-")
    T.wait(150)
    T.note("w4", 1300, 96)
    T.tap("OCT+")
    T.wait(600)

    T.spin("PRESET", -6, 40)             # (back to RED DOOR for the beat and the loop)
    T.wait(500)

    # the beat, then the chord loop over it, all on the bar lines (RED DOOR: 86 BPM)
    bar = 240000 / 86
    T.cap("SEQ  THE BEAT", "Every patch has its own, soft and muffled")
    T.cmd("press SEQ", 2)
    T.wait(60)
    T.cmd("release SEQ", 2)              # (the beat starts as SEQ comes up)
    t0 = T.t + 3
    def at(b, ms=0):                     # bar b's line, plus ms
        T.wait(max(0, round(t0 + b * bar + ms) - T.t))
    at(0, 60)
    T.down("w4", 96)
    at(1, -20)
    T.up("w4")
    T.cap("GLO  THE BASS", "On every kick, the chord's lowest note")
    T.tap("GLO", 30)
    at(1, 60)
    T.down("w9", 96)
    at(2, -20)
    T.up("w9")
    T.cap("LFO  BUSY", "Sparse, as written, or busy")
    T.tap("LFO", 30)
    at(2, 60)
    T.down("w7", 96)
    at(3, -20)
    T.up("w7")
    T.cap("ENV  A FILL", "Toms and the snare, to the end of the bar")
    T.tap("LFO", 30)
    T.tap("LFO", 30)                     # (busy, sparse, as written)
    at(3, 60)
    T.down("b7", 96)
    T.tap("ENV", 80)
    at(4, -320)
    T.up("b7")
    T.cap("REC  A CHORD LOOP", "Play chords, one a bar, and it loops them")
    T.tap("REC", 40)
    for b, k in enumerate(("w4", "w9", "w7", "b7")):
        at(4 + b, 80)
        T.down(k, 100)
        at(5 + b, -40)
        T.up(k)
    at(8, 60)
    T.tap("REC", 40)
    T.cap("REC  A CHORD LOOP", "REC again: four bars, looping. Now play over it")
    tune = [(0, "w13", 2), (0.5, "w12", 1), (0.75, "w11", 1), (1, "w9", 3), (1.75, "w8", 1),
            (2, "w9", 1), (2.25, "w11", 1), (2.5, "w12", 2), (3, "w10", 2), (3.5, "w8", 2)]
    for b, k, q in tune:                 # (q: quarter notes long)
        at(8 + b, 40)
        T.down(k, 92)
        at(8 + b, 40 + q * bar / 4 - 60)
        T.up(k)
    at(12, 60)
    T.cap("PLAY", "Stops the loop, and starts it again from the top")
    T.tap("PLAY", 40)
    at(12, 900)
    T.tap("SEQ", 40)
    T.wait(3000)
    T.cap("__END__", "")
    T.wait(5200)
    T.stop()
    return T


def main(out, probe=False):
    out = Path(out)
    out.mkdir(parents=True, exist_ok=True)
    T = tour(probe)
    (out / "tour.txt").write_text("# the front page's video (tools/video/tour.py): do not edit\n" + "\n".join(T.lines) + "\n")
    (out / "tour.json").write_text(json.dumps({"fps": FPS, "frames": len(T.frames), "segments": T.seg,
                                               "seg_starts": T.seg_starts, "key_events": T.key_events,
                                               "keys": T.frames, "captions": T.caps}))
    print(f"tour: {len(T.frames)} frames, {len(T.frames) / FPS:.1f} s, {T.seg} segments")


if __name__ == "__main__":
    if len(sys.argv) not in (2, 3) or (len(sys.argv) == 3 and sys.argv[2] != "--probe"):
        raise SystemExit(__doc__)
    main(sys.argv[1], len(sys.argv) == 3)
