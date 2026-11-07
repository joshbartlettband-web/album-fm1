#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Hausway
"""The front page's video, part 2: tour.py's frames and sound into an MP4, 1080 x 1350 at 30 frames a second. The
FM-1's screen at 3.6 times, the keys held under it in the colours the screen spells their notes in, the captions
over it.

  python3 album/tools/video/compose.py DIR OUT.mp4 [POSTER.png]     DIR: where the simulator ran tour.txt
  python3 album/tools/video/compose.py DIR --still FRAME OUT.png      one frame, to check"""
import json
import subprocess
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

FONTS = Path(__file__).resolve().parents[3] / "base" / "assets" / "fonts"   # Barlow Semi Condensed (OFL)
F_TITLE = ImageFont.truetype(str(FONTS / "BarlowSemiCondensed-Bold.ttf"), 66)
F_SUB = ImageFont.truetype(str(FONTS / "BarlowSemiCondensed-Medium.ttf"), 36)
F_LIST = ImageFont.truetype(str(FONTS / "BarlowSemiCondensed-SemiBold.ttf"), 36)
F_FOOT = ImageFont.truetype(str(FONTS / "BarlowSemiCondensed-Medium.ttf"), 28)
W, H = 1080, 1350
BG, INK, SOFT, DIM, DARK = (16, 11, 16), (255, 246, 240), (236, 196, 186), (150, 120, 120), (6, 4, 7)
STRIPE = [(240, 138, 64), (224, 24, 24), (224, 64, 176), (122, 72, 184)]   # the fire pit's and the boot photo's
SX, SY, SS = 108, 196, 864                      # the screen: 240 px at 3.6
KX0, KX1, KY0, KY1 = 40, 1040, 1104, 1262       # the keys: F3 .. G5, 16 white
WHITE_PC = {0, 2, 4, 5, 7, 9, 11}
END = ["Eleven patches, each from its own photo", "Chord mode, ACCORDION and NOTES, in any key",
       "A beat for every patch, a bass, fills", "A chord loop to play over",
       "Two FM-1s linked over Bluetooth play as one", "MIDI in and out: USB, Bluetooth, TRS"]


def note_rgb(pc):                               # app.c note_rgb: round the colour wheel by semitone
    h = pc * 6
    x = 110 + (h % 12) * 145 // 12
    return [(255, x, 110), (365 - x, 255, 110), (110, 255, x), (110, 365 - x, 255), (x, 110, 255),
            (255, 110, 365 - x)][min(h // 12, 5)]


def keys_layout():
    whites, blacks, wi = [], [], 0
    ww = (KX1 - KX0) / 16
    for k in range(27):
        pc = (53 + k) % 12
        if pc in WHITE_PC:
            whites.append((k, pc, KX0 + wi * ww, KX0 + (wi + 1) * ww))
            wi += 1
        else:
            x = KX0 + wi * ww
            blacks.append((k, pc, x - ww * 0.31, x + ww * 0.31))
    return whites, blacks


WHITES, BLACKS = keys_layout()


def base():
    im = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(im)
    for i, c in enumerate(STRIPE):
        d.rectangle([i * W // 4, 0, (i + 1) * W // 4, 10], fill=c)
    d.rounded_rectangle([SX - 20, SY - 20, SX + SS + 20, SY + SS + 20], radius=30, fill=DARK)
    d.rounded_rectangle([KX0 - 14, KY0 - 12, KX1 + 14, KY1 + 12], radius=20, fill=DARK)
    d.text((W // 2, H - 40), "Album 01  ·  firmware for the M-VAVE FM-1  ·  free and open source", font=F_FOOT,
           fill=DIM, anchor="mm")
    return im


BASE = base()


def fade(c, a, bg=BG):
    return tuple(int(bg[i] + (c[i] - bg[i]) * a) for i in range(3))


def fit(text, font, width):                     # the caption on one line, smaller if it must be
    while font.getlength(text) > width and font.size > 24:
        font = ImageFont.truetype(font.path, font.size - 2)
    return font


def keyboard(d, held):
    for k, pc, x0, x1 in WHITES:
        on = (held >> k) & 1
        d.rounded_rectangle([x0 + 2, KY0, x1 - 2, KY1], radius=7, fill=note_rgb(pc) if on else (232, 226, 222))
    for k, pc, x0, x1 in BLACKS:
        on = (held >> k) & 1
        d.rounded_rectangle([x0, KY0, x1, KY0 + 94], radius=6, fill=note_rgb(pc) if on else (40, 32, 40))


def frame(f, meta, d_in):
    caps = meta["captions"]
    cur = max((c for c in caps if c[0] <= f), key=lambda c: c[0])
    a = min(1.0, (f - cur[0]) / 8)              # (a caption fades in over a quarter second)
    im = BASE.copy()
    scr = Image.open(d_in / f"f{f:05d}.png").convert("RGB").resize((SS, SS), Image.NEAREST)
    im.paste(scr, (SX, SY))
    d = ImageDraw.Draw(im)
    keyboard(d, meta["keys"][f])
    title, sub = cur[1], cur[2]
    if title == "__END__":                      # the last card, over a dimmed screen
        ov = Image.new("RGBA", (SS, SS), (12, 8, 12, int(215 * a)))
        im.paste(ov, (SX, SY), ov)
        d = ImageDraw.Draw(im)
        d.text((W // 2, 92), "ALBUM 01", font=F_TITLE, fill=fade(INK, a), anchor="mm")
        d.text((W // 2, 150), "Install it from Chrome or Edge, or try it in your browser first",
               font=fit("Install it from Chrome or Edge, or try it in your browser first", F_SUB, 980),
               fill=fade(SOFT, a), anchor="mm")
        for i, item in enumerate(END):
            y = SY + 150 + i * 96
            d.ellipse([SX + 70, y - 9, SX + 88, y + 9], fill=fade(STRIPE[i % 4], a, (12, 8, 12)))
            d.text((SX + 112, y), item, font=F_LIST, fill=fade(INK, a, (12, 8, 12)), anchor="lm")
        d.text((W // 2, SY + SS - 70), "joshbartlettband-web.github.io/album-fm1", font=F_SUB,
               fill=fade(SOFT, a, (12, 8, 12)), anchor="mm")
        return im
    d.text((W // 2, 92), title, font=fit(title, F_TITLE, 1000), fill=fade(INK, a), anchor="mm")
    if sub:
        d.text((W // 2, 152), sub, font=fit(sub, F_SUB, 1010), fill=fade(SOFT, a), anchor="mm")
    return im


def main():
    d_in = Path(sys.argv[1])
    meta = json.loads((d_in / "tour.json").read_text())
    if sys.argv[2] == "--still":
        frame(int(sys.argv[3]), meta, d_in).save(sys.argv[4])
        return
    out = Path(sys.argv[2])
    wavs = [d_in / f"seg{i}.wav" for i in range(meta["segments"])]
    (d_in / "segs.txt").write_text("".join(f"file '{w}'\n" for w in wavs))
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "concat", "-safe", "0", "-i", str(d_in / "segs.txt"),
                    "-c", "copy", str(d_in / "sound.wav")], check=True)
    enc = subprocess.Popen(["ffmpeg", "-loglevel", "error", "-y", "-f", "rawvideo", "-pix_fmt", "rgb24",
                            "-s", f"{W}x{H}", "-r", str(meta["fps"]), "-i", "-", "-i", str(d_in / "sound.wav"),
                            "-c:v", "libx264", "-preset", "slow", "-crf", "24", "-pix_fmt", "yuv420p",
                            "-c:a", "aac", "-b:a", "160k", "-shortest", "-movflags", "+faststart", str(out)],
                           stdin=subprocess.PIPE)
    for f in range(meta["frames"]):
        enc.stdin.write(frame(f, meta, d_in).tobytes())
    enc.stdin.close()
    if enc.wait():
        raise SystemExit("compose: ffmpeg failed")
    if len(sys.argv) > 3:                       # the poster: RED DOOR with a chord held
        poster = next(c[0] for c in meta["captions"] if c[2].startswith("C, A minor")) + 50
        frame(poster, meta, d_in).save(sys.argv[3])
    print(f"compose: {out}: {meta['frames']} frames, {meta['frames'] / meta['fps']:.1f} s")


if __name__ == "__main__":
    if len(sys.argv) < 3:
        raise SystemExit(__doc__)
    main()
