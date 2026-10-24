# The shelf

Patches and photos kept for later, not built into the firmware.

Album gives each patch its own photo and draws the sound from that photo. The first six patches all came from one
photo, the fire pit, so they are here for now with that photo:

| Patch | From the fire pit photo | Sound |
| --- | --- | --- |
| RED DOOR | The red wash | Three detuned saws bent all the time, mostly reverse reverb |
| EMBER | The fire pit | A warm low drone with a quiet crackle; the filter breathes |
| LANTERN | The lamp by the door | A small, clear FM bell with a short reverse swell |
| SHIVER | The cold night | A bright chord through two tremolos at different speeds |
| BLUR | The moving figures | One note at a time, sliding and never landing cleanly |
| FLARE | The violet flare | A high shimmer an octave up that blooms in after each note |

Everything they use is still in the engine (the crackle, the FM bell, the mono slide), so any of them can come back
as it was.

## Bringing one back

1. Move `fire-pit.jpg` back into `album/photos/` (or give the patch a new photo) and add its entry to
   `album/photos/photos.json`. The fire pit's settings were:
   `{"file": "fire-pit.jpg", "keep": ["#06020E", "#FFF0D8", "#F08A40", "#7A48B8"], "vivid": 1.3, "dither": false}`
2. Copy the patch from `patches.h` here into `album/dsp/patches.h` and set `.photo` to that photo's place in
   `photos.json`, counting from 0.
3. Add the patch's name to `album/words.tsv` and a row to the manual's patch table.
