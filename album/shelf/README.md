# The shelf

Patches kept for later, not built into the firmware.

Album gives each patch its own photo and draws the sound from that photo. The first six patches all came from one
photo, the fire pit. RED DOOR is the one that photo kept; the other five are here:

| Patch | From the fire pit photo | Sound |
| --- | --- | --- |
| EMBER | The fire pit | A warm low drone with a quiet crackle; the filter breathes |
| LANTERN | The lamp by the door | A small, clear FM bell with a short reverse swell |
| SHIVER | The cold night | A bright chord through two tremolos at different speeds |
| BLUR | The moving figures | One note at a time, sliding and never landing cleanly |
| FLARE | The violet flare | A high shimmer an octave up that blooms in after each note |

Everything they use is still in the engine (the crackle, the FM bell, the mono slide), so any of them can come back
as it was, given a photo of its own.

## Bringing one back

1. Put its photo in `album/photos/` and add it to `album/photos/photos.json`.
2. Copy the patch from `patches.h` here into `album/dsp/patches.h` and set `.photo` to that photo's place in
   `photos.json`, counting from 0. Retune it to the new photo.
3. Add the patch's name to `album/words.tsv` and a row to the manual's patch table.
