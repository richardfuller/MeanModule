# Preset banks

The 273 factory presets are grouped into 17 banks (plus up to 8 user banks loaded from files, `docs/BANK_FILES.md`). The **Bank** knob (Main page, knob 7) picks a bank and jumps to its first preset; the jog
wheel browses inside that bank only, and the browser shows names without the bank word ("Acid Line", not "Bass Acid Line").

| Bank | Presets | What is in it |
| --- | --- | --- |
| Init | 6 | Clean, bright starting points: Init, Init Saw, Square, Detuned, Sub Octave, Noise |
| Bass | 31 | Acid, fuzz, wobble, key/velocity-responsive and a couple of clean ones |
| Sub | 9 | Low sines and triangles, drifting and pressure subs, dub sub |
| Lead | 34 | Screams, sync leads, plus clean saw, PWM sweep, glass sync, thin highpass |
| Pluck | 13 | Iron, koto, mallet, reso and trance plucks, glass tine, bright saw |
| Keys | 15 | Clavs, organs, metal bells, warm electric piano |
| Stab | 8 | Industrial, fifths, metal ring, bright chord |
| Brass | 14 | Fanfare, swells, horns, velocity bite, clean swell |
| Arp | 17 | Arpeggios from acid to bright sixteenths, a latched pulse and a triplet climb |
| Seq | 11 | Tempo-locked gate, pump and wobble patterns |
| Pad | 27 | Dark pads and open strings, PWM choir |
| Texture | 13 | Beds, static, hum, bright shimmer |
| Vox | 7 | Formant and vowel sounds |
| Drone | 11 | Held and latched drones |
| FX | 15 | Sirens, risers, falls, glitches |
| Horror | 18 | Haunted amusement park, crypt and creature sounds (below) |
| Sets | 24 | Eight themes of three presets that belong together (below) |

Program Change from an external controller: bank select (CC 0) of 0 keeps the old flat numbering (program = preset number); bank select 1..25
picks program N inside that bank (1 = Init, 2 = Bass, 3 = Sub ... 16 = Horror, 17 = Sets, 18..25 = user banks 1..8).

## Horror

Calliope (out of tune, with a Mod Wheel that detunes it further), Carousel (arpeggiated calliope), Crypt Organ, Music Box (flat and wandering),
Cellar Door (creaking glide), Slasher Stab, Dread Drone (latch), Whispers, Heartbeat (tempo-locked throb), Theremin Ghost (use aftertouch),
Fly Swarm, Toll (church bell), Rotting Pipes, Insect Nest (random arp), Possessed Choir, Grave Rumble, Shriek, Hive Mind.
Pitch clashes (minor second, tritone), heavy drive, resonance and random movement are on purpose: this bank is meant to unsettle.

## Sets

Put each preset of a set on its own Move track; the three are tuned to occupy different registers and share a character. Names read
"<Theme> <n> - <Role>".

| Theme | 1 | 2 | 3 |
| --- | --- | --- | --- |
| Gritty Punk | Bass | Lead | Stab |
| Acid Rain | Bass | Lead | Arp |
| Black Mass | Bass | Choir | Bells |
| Iron Works | Bass (tempo-locked wub) | Stab | Gate (tempo-locked) |
| Cold Wave | Bass | Lead | Pad |
| Dub Cellar | Sub | Stab | Siren |
| Doom | Drone Bass | Horns | Choir |
| Neon Static (bright) | Bass | PWM Lead | Arp |

## What the review found (0.9.2)

Measured with `tests/preset_audit.c`, which renders every preset (one held note) and prints level, spectral centroid, octave-band shape,
amplitude movement and tail, then compares the presets pairwise.

* The library skewed dark and narrow: at C3 the median spectral centroid of the Bass, Pluck, Keys, Stab and Texture banks sat between 150 and 260 Hz,
  and almost nothing used the highpass or bandpass filter modes, PWM (LFO to Shape), the Hold latch with the arp, or very clean oscillators.
  New presets fill that range (clean/open/bright across Init, Bass, Lead, Pluck, Keys, Stab, Brass, Arp, Seq, Pad, Texture, Vox) without
  touching the dark core.
* Removed as near-duplicates of a neighbour: Bass Overdriven, Bass Octave Stack, Brass Stab, Lead Grit Fifth, Pluck Slag, Keys Pluck,
  Keys Gritty Clav, Keys Grit Bell.
* Two presets were both named Pad Slow Swell; the one driven by Note Age is now Pad Age Swell.
* Moved to where they belong: Keys Reso/Rusty/Trance Pluck became Pluck Reso/Rusty/Trance; Bass Dub Sub and Bass Hollow Sub became Sub Dub and Sub Hollow.
* The new presets were checked the same way against the old ones, and four that sounded too close to an existing preset were reshaped.
* Nothing here was judged by ear. Levels, brightness, silence and duplicates are measured; taste is the open part.
