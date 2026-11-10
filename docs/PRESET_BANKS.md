# Preset banks

The 201 factory presets are grouped into 9 banks (plus up to 8 user banks loaded from files, `docs/BANK_FILES.md`). The **Bank** knob
(Main page, knob 7) picks a bank and jumps to its first preset; the jog wheel browses inside that bank only, and the browser shows names
without the bank word ("Acid 1", not "Bass Acid 1").

| Bank | Presets | What is in it |
| --- | --- | --- |
| Init | 1 | The default patch (needed as the starting point; everything else is from the brief) |
| Bass | 25 | Sub (triangle rumbles), Acid and Pluck, Overdriven and Industrial, Modulated and SFX bass |
| Lead | 25 | Expressive and Glide, Hard Sync and Tearing, FM and Metallic, Distorted and Feedback |
| Drone | 25 | Evolving drones and textures: Sub-Harmonic, Noise and Windscapes, Complex FM, Rhythmic and Pulsing |
| Arp | 26 | Arps and sequences: Classic Plucks, Acid Sequences, FM and Metallic, Dark and Industrial |
| Para | 25 | Paraphonic intervals: fifths and fourths, minor thirds and sixths, tritones and clusters, detuned unisons and octaves |
| Chaos | 25 | Modular chaos and SFX: Glitch and Malfunction, Sci-Fi and Lasers, Industrial Noise and Impacts, Audio-Rate Mayhem |
| Horror | 25 | Haunted Keys (calliope), Cinematic Tension, Jumpscares and Stings, Ghostly Ambiences |
| Sets | 24 | Eight themes of three presets that belong together |

The sub-sections are only a way to organise the list; they are not shown in the Move. No percussion, a dark and industrial lean, with clean and
bright sounds across the banks.

Program Change from an external controller (needs Program Change = Accept on the Bank Files page): bank select (CC 0) of 0 keeps the flat
numbering (program = preset number); bank select 1..17 picks program N inside that bank (1 = Init, 2 = Bass, 3 = Lead, 4 = Drone, 5 = Arp,
6 = Para, 7 = Chaos, 8 = Horror, 9 = Sets, 10..17 = user banks 1..8).

## Horror

Calliope follows the brief (two pulses an octave apart, an LFO on both pitches, the Mod Wheel speeding the LFO like a failing motor) plus what was asked
for in chat: the pitch wanders like uneven steam temperature (a random offset per note and a slow smooth-random drift), and a held chord is cycled quickly (a tied 1/32 arp)
so it sounds like several pipes. The rest of the bank follows the brief: Music Box, Ghost Organ, Broken Toy, Dusty Piano, Cursed Bell; Anxiety Pulse,
Saw Blade, Creeping, The Hive, Dark Water, Pendulum; Scream Hit, Door Slam, Nerve Zap, Psycho, Glass Break, Sudden Drop; EVP, Banshee, Cold Breath,
Apparition, Crypt, Spirit Box, Chant. Pitch clashes, heavy drive, resonance and random movement are on purpose.

## Sets

Put each preset of a set on its own Move track; the three are tuned to occupy different registers and share a character. Names read
"<Theme> <n> - <Role>".

| Theme | 1 | 2 | 3 |
| --- | --- | --- | --- |
| Industrial Decay | Bass | Lead | Drone |
| Darkwave Drive | Bass | Stab | Arp |
| Void Walker | Sub | Lead | Texture |
| Acid Reactor | Bass | Pluck | SFX |
| Cybernetic | Bass | Arp | Pad |
| Cryptic Ritual | Bass | Chime | Drone |
| Warehouse Rave | Bass | Stab | Sweep |
| Alien Flora | Pluck | Lead | Pad |

## Levels and character (0.11.1)

* **Arp bank**: every preset arpeggiates or repeats (Up, Down, Up and Down, Random or As Played; 1/8 to 1/16T; one to three octaves). Hold one key and it repeats; hold a chord and it arpeggiates.
* **Level matching**: each preset carries a Level Trim (Response page, +-24 dB, 0.5 = unity) found by measurement so that a held chord plays at the same
  A-weighted loudness, about -19 dB over its loudest 150 ms (within about 1 dB for the whole library). `tests/loudness.c` measures and calibrates (`calibrate` rewrites the trims, `measure` checks);
  `scripts/test.sh` fails if any preset is more than 2.5 dB away or if an Arp preset repeats fewer than eight times in 4.5 s.
  Presets whose sound is mostly a short transient are limited by peak level (about -3 dB) rather than by the target.
* **Character**: the nonlinear filter and the hard-driven VCA are what make this synth sound like itself. Every preset starts from a floor of Filter Drive (0.85), Drive (0.5), Resonance (0.4),
  Drift (0.6), a little noise and some Grit macro; ambient and soft presets (the drones, the ghostly Horror presets, the clean tri subs) use a gentler floor.

## Register, velocity, short notes (0.12.0)

`tests/register.c` plays each preset on five notes across its home register (Bass C1-C3, Lead/Arp/Para C3-C5, Drone/Chaos/Horror C2-C4, Sets by role; note names as Move shows them, C3 = MIDI 60),
at velocity 40 and 127 on the middle note, and as a 150 ms note.

* **Key Level** (`key_lvl`, Response page) tilts the level by up to 6 dB per octave around C3 and is fitted per preset so the register is even. Before the fit, 110 of 201 presets moved more than 6 dB
  across their register. The bass register uses plain RMS (A-weighting would ask 20-30 dB more of the lowest notes than the limiter has room for) and the fit is capped so low notes keep 3 dB of headroom.
* **Velocity**: before this, most presets ignored it. Played presets now respond through Velocity to Amp and Cutoff; drones and ambient presets do not on purpose. Heavy Drive evens out level, so the main effect is brightness.
* **Short notes**: only the slow-attack pads, drones, sweeps and two slow-opening filters are much quieter on a 150 ms note, as intended.

## How the brief maps onto the engine (0.11.0)

The new library was written from a patch-cable description of a modular-style mono synth. Mean Module has no cables; the same ideas map like this.

| In the description | In Mean Module |
| --- | --- |
| VCO Triangle, Saw, Pulse, Square | Osc Wave Tri, Saw, Pulse; a Square is a Pulse at Shape 0.5; pulse width is Shape |
| VCA Overdrive, VCA Drive | Drive (after the VCA); Mixer past 12 o'clock = Osc Level and Filter Drive |
| Env 1, Env 2 | Filter Env, Amp Env (the Amp Env is also the VCA). Pitch and FM jobs use the Filter Env |
| Env to VCF FM1 / LFO to VCF FM2 | Filter Env to Cutoff / LFO to Cutoff |
| Env or LFO to VCO FM1 / FM2 / 1V per octave | Filter Env to Osc FM / LFO to Osc FM (pitch); VCO 2 into VCO 1 FM is X-Mod |
| Noise to VCF Aux | the Noise level and Noise Color |
| LFO Square to Env Gate (stutter, sync drone, broken pin) | **LFO Retrigger** (new): each LFO cycle restarts both envelopes |
| LFO at audio rates | **LFO Sync = Audio** (new): 20 Hz to 2 kHz |
| Velocity to Env Attack or Decay | **Matrix targets Filter/Amp Env Attack/Decay** (new) |
| LFO or Env to VCO 1 pitch only | **Matrix target Osc 1 Pitch** (new) |
| Joystick X / Y, Modwheel or touch strip (CC 1), Keytrack | Stick X / Y, Mod Wheel, Key |
| A randomized LFO | LFO wave Random, or the Smooth Random source |
| Looping Env 2 | LFO Retrigger |

What is not in the engine, and how the presets stand in:

* **True oscillator hard sync** is not implemented. The "sync" presets (Sync Rip, Hard Sync, Stutter, Sync Unison) use LFO Retrigger, which is
  what the description asks for ("LFO Square to Env 1 Gate"). Real oscillator sync would need band-limited resets; it is a candidate for later.
* **Velocity curves** (Log, Fixed) are a setting of the external controller. Velocity here is scaled by Velocity to Amp and Velocity to Cutoff on the Response page.
* A few "level" amounts (FM1 amount, VCF FM level) are routed through the matrix Osc FM / Env Amount / LFO Depth targets, so their feel is close
  but not numerically identical.
* The presets were tuned by measurement (level, silence, clipping, near-duplicates), not by ear.

## History

0.11.0 replaced the library (275 presets in 17 banks) with the banks above. The earlier review (0.9.2) measured the old library with
`tests/preset_audit.c`, which renders every preset (one held note) and prints level, spectral centroid, octave-band shape, amplitude movement and tail.
