# Album

A series of firmwares for the M-VAVE FM-1, one per record: a few deep sounds and the photographs they came from,
each sound moving its photo as it plays. **Album 01** follows one record: chords bent the whole
time they ring, heard through a reverse reverb.

- Eleven patches so far, up to twelve: RED DOOR, MAPLE, DAHLIA, FLAME, CLEMATIS, WHIRL, WINDOW, PASTURE, BLUESTEM,
  SNOWFALL, TOWER.
  One photo, one patch, the sound drawn from the photo. Five earlier patches that shared RED DOOR's photo are on
  the shelf (`album/shelf/`)
- Five knobs on every patch: Glide, Swell, Tone, Blur, and Fuzz on ALGORITHM (a fuzz pedal on the whole chord, FX
  in and out), each acting on the sound and the photo
- Chord mode (ARP): every key plays a chord, the black keys too, in any key (SELECT); chords drawn from My Bloody
  Valentine's records and voiced differently by every patch
- A beat for every patch (SEQ), soft and muffled, partly into the reverse reverb; tempo on SEQ and ALGORITHM; two linked
  FM-1s keep time together over MIDI clock
- Both of the FM-1's cores: half of the voices and the reverb on the second, with a fall back to one core
- Two FM-1s linked over Bluetooth MIDI play as one
- The manual: [album/docs/MANUAL.md](album/docs/MANUAL.md)

## Layout

| Path | What |
| --- | --- |
| `album/` | the firmware: `app.c` (the screen, the controls, MIDI), `dsp/` (the sound), `cpu1/` (the second core), `photos/` (the photos and their palettes), `tests/` (simulator scripts), `shelf/` (patches kept for later, not built), `site/` (the front page; `more/` holds the cards for Josh's other projects, which `tools/more.py` adds to it) |
| `base/` | [fm1-base](https://github.com/charlesvestal/fm1-base) (submodule): the platform, the simulator, the build and install tools |
| `.github/workflows/` | the build: tests, the firmware with and without Bluetooth, the site (GitHub Pages); tags make attested releases |

## Build

```sh
git clone --recursive https://github.com/joshbartlettband-web/album-fm1
pip install Pillow numpy
python3 base/tools/build.py album --host                      # the simulator
album/build/host/album_host album/tests/basic.txt out          # a test: WAVs and screenshots in out/
python3 base/tools/build.py album                             # the firmware (JieLi's toolchain: base/tools/get_toolchain.sh)
```

Bluetooth needs JieLi's AC79 SDK (`$AC79_SDK`); `--no-ble` builds without it. Every push to `main` builds both on
GitHub and publishes the install page.

## Adding a patch and its photo

Each patch gets its own photo, and the sound comes from what is in it.

1. Put the photo in `album/photos/` and add it to `photos.json` (its crop, the colours its palette must keep).
2. Add the patch to `album/dsp/patches.h` with that photo's place in `photos.json`, counting from 0.
3. Build the simulator and render it: `album/tests/demo.txt` shows how.

## Licence

MIT (`LICENSE`), except `album/cpu1/entry.S`, whose four instructions come from felucca-bsp and stay MPL-2.0. Built on
fm1-base by Charles Vestal and felucca-bsp by Leo Kuroshita, Hügelton Instruments (both MPL-2.0, in `base/`). The Bluetooth build links JieLi's AC79 SDK libraries (Apache-2.0, not in this repository). The photos are
Hausway's. M-VAVE and FM-1 are trademarks of their owners; Album is not affiliated with M-VAVE.
