#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Alive-Stable-7254
"""The front page's video, part 3: the screenshot on the front page (site/landing.md's picture) becomes the video,
with the screenshot as its poster until it plays, and a link to download it.

  python3 album/tools/video/page.py SITE_DIR      after base/tools/make_site.py: SITE_DIR/index.html, SITE_DIR/img/

The video and its poster are in site/img/ (make_site.py copies it); tour.py and compose.py make them."""
import re
import sys
from pathlib import Path

VIDEO, POSTER = "img/album-01.mp4", "img/album-01-poster.png"
SHOT = "img/screen-playing.png"
CSS = """<style>
  figure.tour { margin: 1.5rem auto; max-width: 432px; }
  figure.tour video { display: block; width: 100%; height: auto; aspect-ratio: 4 / 5; border-radius: 16px; background: #100b10; }
  figure.tour figcaption { margin-top: .6rem; }
</style>
"""
FIG = ('<figure class="tour"><video controls playsinline preload="none" poster="{poster}" aria-label="A two and a '
       'half minute tour of Album 01 on the FM-1: Red Door playing four chords, the five knobs, all eleven patches '
       'and their photos, chord mode, accordion and notes, the beat, and recording a chord loop to play over.">'
       '<source src="{video}" type="video/mp4"></video><figcaption>Every patch, the knobs, the chord modes, the beat '
       'and the chord loop, in two and a half minutes. Recorded from the emulator, the same code the FM-1 runs. '
       '<a href="{video}" download="album-01.mp4">Download the video</a>.</figcaption></figure>')


def main(site):
    page = Path(site) / "index.html"
    html = page.read_text(encoding="utf-8")
    for f in (VIDEO, POSTER):
        if not (Path(site) / f).exists():
            raise SystemExit(f"video: no {f} in the site")
    figs = re.findall(r'<div class="figs"><figure class="shot"><img src="' + re.escape(SHOT) + r'".*?</div>', html)
    if len(figs) != 1:
        raise SystemExit(f"video: expected the front page's one picture ({SHOT}), found {len(figs)}")
    html = html.replace(figs[0], FIG.format(video=VIDEO, poster=POSTER), 1)
    if html.count("</head>") != 1:
        raise SystemExit("video: expected </head> once in the page")
    html = html.replace("</head>", CSS + "</head>", 1)
    page.write_text(html, encoding="utf-8")
    print(f"video: {page}: {VIDEO}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
