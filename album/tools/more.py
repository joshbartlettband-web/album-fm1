#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Hausway
"""Album's front page joins Josh's other projects: the "Other projects by Josh" menu at the top and the cards at the
bottom, as on the Rainbow mode, Double Rainbow and Fretbound pages (the same markup, in Album's paper and ink), and
their pictures from site/more/.

  tools/more.py SITE_DIR        after base/tools/make_site.py: SITE_DIR/index.html, SITE_DIR/more/

Each project is one entry below. When a project is added or a card's picture changes, change it here and copy its
picture into site/more/ from that project's landing/more/ (they share the files)."""
import shutil
import sys
from pathlib import Path

APP = Path(__file__).resolve().parents[1]

PROJECTS = [
    ("https://joshbartlettband-web.github.io/felucca-rainbow/", "rainbow-mode.gif", "Rainbow mode",
     "A music toy for little hands, on the FM-1",
     "A music toy for little hands on the M-VAVE FM-1. 94 friends, 21 beats and a color for every note."),
    ("https://joshbartlettband-web.github.io/Double-Rainbow/", "double-rainbow.gif", "Double Rainbow",
     "Synth characters for the FM-1, ten and up",
     "Firmware for the M-VAVE FM-1. 24 sounds got out of the Keeper's jars, and each one is a real synth engine you "
     "learn by playing. For ages ten and up."),
    ("https://joshbartlettband-web.github.io/Fretbound/", "fretbound.gif", "Fretbound",
     "Ear training for guitarists, in your browser",
     "An ear-training game for guitarists. A rival plays a phrase and you play it back on the fretboard. It runs in "
     "your browser."),
]

CSS = """<style>
  .jbm-top { display: flex; justify-content: flex-end; position: relative; z-index: 5; margin: 0 0 4px; }
  .jbm-top details { position: relative; background: none; padding: 0; margin: 0; border-radius: 0; }
  .jbm-top summary { list-style: none; cursor: pointer; display: inline-flex; align-items: center; gap: 8px; min-height: 40px;
    padding: 0 14px; border-radius: 999px; font: 600 15px/1 system-ui, -apple-system, "Segoe UI", sans-serif;
    color: var(--ink); background: var(--sheet); box-shadow: inset 0 0 0 1px var(--rule); }
  .jbm-top summary::-webkit-details-marker { display: none; }
  .jbm-top summary::after { content: ""; width: 7px; height: 7px; border: solid currentColor; border-width: 0 2px 2px 0;
    transform: translateY(-2px) rotate(45deg); transition: transform .15s; }
  .jbm-top details[open] summary::after { transform: translateY(1px) rotate(-135deg); }
  .jbm-top summary:focus-visible, .jbm-top a:focus-visible, .jbm-bottom a:focus-visible { outline: 3px solid var(--hot); outline-offset: 3px; }
  .jbm-top ul { position: absolute; right: 0; top: calc(100% + 8px); width: min(320px, calc(100vw - 32px)); margin: 0; padding: 8px;
    list-style: none; background: var(--paper); border-radius: 16px; box-shadow: 0 12px 40px rgba(30, 43, 90, .25), inset 0 0 0 1px var(--rule); }
  .jbm-top li { margin: 0; }
  .jbm-top li a { display: flex; gap: 12px; align-items: center; padding: 8px; border-radius: 12px; text-decoration: none; color: var(--ink); }
  .jbm-top li a:hover { background: var(--sheet); }
  .jbm-top li img { width: 52px; height: 52px; object-fit: cover; object-position: center; border-radius: 10px; flex: none; image-rendering: pixelated; margin: 0; }
  .jbm-top li b { display: block; font: 700 16px/1.25 system-ui, -apple-system, "Segoe UI", sans-serif; }
  .jbm-top li span { display: block; font: 13px/1.35 system-ui, -apple-system, "Segoe UI", sans-serif; color: var(--muted); }
  .jbm-bottom { margin: 56px 0 0; }
  .jbm-bottom h2 { margin: 0 0 14px; }
  .jbm-bottom ul { display: grid; gap: 16px; margin: 0; padding: 0; list-style: none; }
  .jbm-bottom li { margin: 0; display: flex; }
  .jbm-bottom a { flex: 1; display: grid; grid-template-columns: 120px 1fr; gap: 16px; align-items: center; padding: 12px; border-radius: 18px;
    text-decoration: none; color: inherit; background: var(--sheet); box-shadow: inset 0 0 0 1px var(--rule); }
  .jbm-bottom a:hover { background: var(--hot-t); }
  .jbm-bottom img { width: 120px; height: 120px; object-fit: cover; object-position: center; border-radius: 12px; image-rendering: pixelated; margin: 0; }
  .jbm-bottom b { display: block; font-size: 1.15rem; }
  .jbm-bottom span { display: block; font-size: .95rem; line-height: 1.4; color: var(--muted); }
  .jbm-bottom em { display: block; margin-top: 6px; font-style: normal; font-weight: 700; font-size: .95rem; color: var(--hot-ink); }
  @media (min-width: 640px) {
    .jbm-bottom ul { grid-template-columns: repeat(3, 1fr); }
    .jbm-bottom a { grid-template-columns: 1fr; align-items: start; }
    .jbm-bottom img { width: 100%; height: auto; aspect-ratio: 1; }
  }
</style>
"""

SCRIPT = """<script>
  // the menu closes on a click outside it or on Escape
  (function () {
    var d = document.querySelector(".jbm-top details");
    if (!d) return;
    document.addEventListener("click", function (e) { if (d.open && !d.contains(e.target)) d.open = false; });
    document.addEventListener("keydown", function (e) { if (e.key === "Escape" && d.open) { d.open = false; d.querySelector("summary").focus(); } });
  })();
</script>
"""


def top():
    items = "".join(f'      <li><a href="{url}"><img src="more/{img}" width="52" height="52" alt=""><span><b>{name}</b>'
                    f'<span>{short}</span></span></a></li>\n' for url, img, name, short, _ in PROJECTS)
    return ('<!--MORE-TOP-->\n<nav class="jbm-top" aria-label="Other projects by Josh">\n  <details>\n'
            '    <summary>Other projects by Josh</summary>\n    <ul>\n' + items + '    </ul>\n  </details>\n</nav>\n'
            '<!--/MORE-TOP-->\n')


def bottom():
    items = "".join(f'    <li><a href="{url}"><img src="more/{img}" width="240" height="240" loading="lazy" '
                    f'alt="{name}, a few of its screens"><div><b>{name}</b><span>{long}</span><em>Take a look</em></div>'
                    f'</a></li>\n' for url, img, name, _, long in PROJECTS)
    return ('<!--MORE-BOTTOM-->\n<section class="jbm-bottom" aria-labelledby="jbm-h">\n'
            '  <h2 id="jbm-h">Other projects by Josh</h2>\n  <ul>\n' + items + '  </ul>\n</section>\n<!--/MORE-BOTTOM-->\n')


def put(html, mark, text, what):
    if html.count(mark) != 1:
        raise SystemExit(f"more: {what}: expected {mark!r} once in the page")
    return html.replace(mark, text, 1)


def main(site):
    site = Path(site)
    page = site / "index.html"
    html = page.read_text(encoding="utf-8")
    if "<!--MORE-TOP-->" in html:
        raise SystemExit("more: the page already has the projects")
    html = put(html, "</head>", CSS + "</head>", "the style")
    html = put(html, '<div class="page">\n', '<div class="page">\n' + top(), "the menu")
    html = put(html, "\n</main>", "\n" + bottom() + "</main>", "the cards")
    html = put(html, "</body>", SCRIPT + "</body>", "the script")
    page.write_text(html, encoding="utf-8")
    (site / "more").mkdir(exist_ok=True)
    for _, img, *_ in PROJECTS:
        shutil.copy2(APP / "site" / "more" / img, site / "more" / img)
    print(f"more: {page}: {len(PROJECTS)} projects, top and bottom")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])
