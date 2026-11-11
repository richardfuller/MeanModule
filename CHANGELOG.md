# Changelog

## 0.13.3

### More sound checks
* `tests/health.c` now also checks the tuning of the two oscillators in the Para bank (the second oscillator must sit on its interval within 40 cents and be audible; all 25 do, with errors of a few cents), reports a non-harmonic energy share at the top of the register and an onset brightness jump, and skips the interval check on presets with a pitch envelope.
* `scripts/health_diff.py` compares each health run with the committed snapshot `tests/golden/health.csv`, so an unintended change to a preset shows up; `scripts/similar.py` lists near-duplicate presets from the spectral audit (only 8 pairs of 221 are close, two of them in the same bank: Arp Snap Tri / Nail Gun and Para Hollow Fourth / Grit Chime).
* Every preset now has an amp attack of at least 2.2 ms (the knob at 0 is 0.5 ms, which starts mid-cycle on a low note). The brightness jump at note-on is mostly the filter-envelope snap of the plucks, which is intended; only the extreme ones (Drone Radar, Bass Thump, Para Pluck Fifth, Chaos Glitch Box) are flagged.
* The osc1-coarse test measures over a longer window, since a zero-crossing count is only good to one crossing.

## 0.13.2

### Sound health checks
* New `tests/health.c` (`scripts/health.sh`) measures what used to need listening: clipping at the limiter on low, middle and high notes, abrupt releases (clicks), hanging tails, pitch against the played note, whether the Mod Wheel, Aftertouch and Stick change the sound where a preset routes them, whether the arps follow the tempo (90 vs 150 BPM), and CPU per preset. It is documented in `docs/TESTING.md`.
* First full run: no clipping, no abrupt releases, no hanging tails, every arp follows the tempo, CPU is even (median 33 us per block on the host, none above 45). It found four weak controller routes (Lead Squeal and Keys Drawbar Organ on the Mod Wheel, Drone Dust Storm and Horror Nerve Zap on Aftertouch), now stronger, and four presets described as clean that the character pass had saturated into near-squares: Bass Sub 1, Bass Log Tri, Lead Glide Tri and Set Void Walker 1 - Sub. Those four now skip the character floors, and the health check fails if they are squashed again.
* Levels and registers were refitted.

## 0.13.1

### Ghost Sub and Mothership character for softer presets
Two presets were singled out as showing the instrument off best: Bass Ghost Sub (a pulse with slow PWM, a hollow high-pass filter, the Stick on the cutoff) and Chaos Mothership (deep detuned saws with a slow filter-envelope swell). Their traits were carried into twelve softer presets: slow PWM or shape movement from LFO 2 on Bass Sub 1, Set Void Walker 1 - Sub, Lead Glide Tri, Drone Stasis, Drone Glass Fog and Horror Ghost Organ (with the Stick on the cutoff); a slow filter-envelope swell on Lead Flute Mod, Drone Ground Swell, Para Fifth Drone, Para Sixth Pad and Set Void Walker 3 - Texture; a detuned second voice that drifts on Lead Theremin; and a deep detuned saw layer with a swell on Horror Chant. Levels and register were refitted.

## 0.13.0

### Matching the hardware's arpeggiator and octave shift
* Arp Octaves now go up to 6 (the hardware's range), was 3.
* A second Up/Down type: **Up and Down 1** (end notes played once, as before) and **Up and Down 2** (both end notes played twice), the two types the hardware lists. Arp Mode has 7 settings; the stick's Arp Mode flick cycles through all of them.
* Octave now shifts +-4 octaves (the hardware's range), was +-2.

### Keys bank
* A new bank, **Keys** (20 presets, after Sets): electric pianos (Tine EP, Wurly, Suitcase, Bell EP, Dirty EP), clavs (Clav, Funk Clav, Pick Clav, Dirty Clav), organs (Combo, Drawbar, Church, Perc), and struck or plucked keys (Harpsichord, Toy Piano, Marimba, Vibes, Muted Piano, Honky Piano, FM Piano). Built for dynamics: Velocity drives the level, the filter and, through the matrix, the FM depth or the drive, and the Keys bank keeps lighter drive than the rest of the library so the dynamics are not squashed. 221 presets in all.
* Bank numbers: Keys is bank 10 for Program Change (CC0 = 10); user banks moved to 11..18 (Bank knob 11..18).

## 0.12.1

### Note readout and Move's octave numbering
* The Main page shows the note you last played (read-only `note_now`, with the Octave setting applied) as a note name, 36 = C1, 60 = C3, the way Move and Live name notes. It takes the place of Filter Mode on that page; Filter Mode is still on the Filter page. The small "i1" in the corner of the screen while a note is held is Schwung's own MIDI-channel indicator, drawn by the host (it can be switched off in Schwung's settings); a module cannot change it.
* My register tests of 0.12.0 used note names one octave lower than Move's (the test called MIDI 48 "C3"). The register fit, the loudness chord and the Key Level pivot now use Move's numbering: Bass C1-C3 = MIDI 36-60, Lead/Arp/Para C3-C5 = 60-84, Drone/Chaos/Horror C2-C4 = 48-72, loudness chord C3 major (60, 64, 67), Key Level pivots on C3 (MIDI 60). Everything was fitted again.

## 0.12.0

### Register, velocity and short-note testing
* New parameter Key Level (Response page, `key_lvl`, -1..+1 = -6..+6 dB per octave around C3). A sweep with `tests/register.c` (five notes across each preset's home register: Bass C1-C3, Lead/Arp/Para C3-C5, Drone/Chaos/Horror C2-C4, Sets by role) found the median preset got about 3 dB louder per octave and 110 of 201 moved more than 6 dB across its own register. Each preset's Key Level is now fitted so the level is even (median spread 0.6 dB for the non-bass banks). The bass register is judged by plain RMS rather than A-weighted loudness, and the fit eases back where a low note would otherwise hit the limiter.
* The same sweep showed most presets ignored velocity (a median 0 dB between velocity 40 and 127). Played presets (bass, leads, arps, paraphonic, the plucked/struck Horror and Sets presets) now have a floor of Velocity to Amp and Velocity to Cutoff; drones and ambient presets stay flat on purpose. Heavy drive compresses level differences, so the effect is mostly brightness.
* `scripts/test.sh` also runs `register.c`. A short 150 ms note is only much quieter than a held one on the slow-attack pads, drones and sweeps, as intended.

## 0.11.3

### Exponential knobs
The first drafts of the new presets read several exponential knobs as if they were linear. A review of every preset in real units (`tools/presetgen/units.py`) found:
* **Release**: nearly every preset had a 1-16 ms amp release (notes cut off at once). Releases are now at least 40-55 ms.
* **Plucks and filter sweeps**: amp decays of 26-70 ms and filter-envelope decays of 30-70 ms (a 30 ms "squelch" is a click). Plucks now decay over about 190 ms, sweeps about 110 ms, except the deliberate short hits (Thump, Snap Tri, Nail Gun, Glass Break, Door Slam ...). LFO-retriggered presets keep their short decays.
* **Cutoff**: many static filters sat at 100-400 Hz at C3 (the knob is 20 Hz x 2^(9.8 x)). The dullest static-filter presets were lifted until a held C3 reached a target brightness (`tests/brightness.c`, never fully open, swept filters untouched). Dark-by-design presets (subs, Crypt, Deep Void ...) keep low targets.
* Horror The Hive's LFO was 0.7 Hz where a swarm needs about 5 Hz.
* Levels were fitted again afterwards (all 201 within about 1 dB).

## 0.11.2

* Arp bank notes are longer. The first draft used amp decays of 26-70 ms and releases of about 2 ms (the time knobs are exponential: 0.4 = 26 ms, 0.5 = 70 ms, 0.6 = 190 ms, 0.7 = 510 ms), so each note was gone almost as soon as it sounded, at any tempo. Arp presets now decay over roughly 190-310 ms with a 40-70 ms release and a little sustain (the snappy ones, Snap Tri, Nail Gun, Ice Pick, Glass and Rez Pop, are shorter). Levels were refitted afterwards.
* `tests/loudness.c` counts note starts with a rise detector, so ringing notes are counted correctly.

## 0.11.1

### Library review
* Only the presets from the brief remain: the extra Init presets and the three extra calliope presets (Solo, Fifths, Carousel) are gone, and Horror Calliope was rebuilt from the brief (plus the pitch wander and chord cycling asked for earlier). 201 presets: one Init and 200 from the brief.
* The Arp bank now arpeggiates or repeats: every Arp preset (and the two Sets arps) has an arp mode, rate, octave range and gate of its own (Up, Down, Up and Down, Random, As Played at 1/8, 1/8T, 1/16, 1/16T). The tests fail if an Arp preset does not repeat.
* Level matching: a new Level Trim parameter (Response page, +-24 dB, 0.5 = unity) is set per preset by measurement so every preset plays at the same A-weighted loudness (within about 1 dB of -19 dB, measured over the loudest 150 ms of a held chord). Before this, presets ranged over 25 dB. `tests/loudness.c` measures and calibrates; `scripts/test.sh` fails if any preset is more than 2.5 dB off.
* Character pass: every preset now has a floor of filter drive, drive, resonance and drift, a little hiss and some of the Grit macro, so the nonlinear filter and hard-driven VCA are heard in every bank (ambient presets get a gentler floor). The level matching was done after this pass.

## 0.11.0

### New library
* All factory presets were replaced: 209 presets in 9 banks (Init, Bass, Lead, Drone, Arp, Paraphonic, Chaos, Horror, Sets). The four steam-pipe calliope presets are kept in Horror. See `docs/PRESET_BANKS.md`, including how the patch-style descriptions map onto the engine.
* Bank numbers changed: bank select 1..9 is Init, Bass, Lead, Drone, Arp, Para, Chaos, Horror, Sets; user banks are 10..17 (Bank knob 9..16). Saved projects keep their sound, but the preset name shown for an old saved number now points at the new list.

### Engine additions the new library needed
* LFO Sync has an Audio setting: the LFO runs 20 Hz to 2 kHz (FM, tones, audio-rate gating).
* LFO Retrigger (LFO 2 page): each cycle of LFO 1 or LFO 2 restarts both envelopes while a note is held (stutters, gated drones, re-struck plucks).
* Five new matrix targets: Osc 1 Pitch, Filter Env Attack, Filter Env Decay, Amp Env Attack, Amp Env Decay (30 targets in all). Velocity or Key can now change envelope times per note.
* New tests for all of the above. Oscillator hard sync is not implemented (the sync-style presets use LFO Retrigger).

## 0.10.2

### Preset stays put
* New setting Program Change (Bank Files page), default Ignore: outside Program Change messages no longer switch the preset unless set to Accept. Reported: a sequenced track occasionally jumped to another preset during playback.
* Two read-only diagnostics on the Bank Files page: Last Preset Change By (None, Preset Set, Bank Pos, Bank, Prog Chg, State Load) and Program Changes Seen (count of Program Change messages received from any source, ignored or not). If a preset flips again, these show what did it.

## 0.10.1

### Calliope
* The four calliope presets (Calliope, Solo, Fifths, Carousel) now lean into steam-pipe instability instead of distortion: filter drive and drive are far lower, and the pitch is random in three ways. Every note starts a little off pitch (per-note random), the pitch wanders slowly like a changing steam temperature (smooth random), and the second pipe also jumps now and then (LFO 2 random). Held notes sag slightly. The Mod Wheel widens the second pipe's wander (Matrix slot 5, Via = Mod Wheel). Vibrato is off, since a calliope has none.

## 0.10.0

Bank files (export and import), a harsher Horror bank, and a bite review of the whole library.

* The version number is now shown in the module: **Version** on the Bank Files page (read-only) and the first line of the Start here help page.

### Matrix
* Each matrix slot now has its own row on the Matrix pages: **Src, Dst, Amt** and a new fourth control, **Via** (Scaled By): a second source that multiplies the slot's amount, for example LFO to Cutoff via Mod Wheel to fade a wobble in, or Noise to Pitch via Aftertouch. Off (the default) behaves exactly as before, so every existing preset is unchanged. 6 new parameters (`m1_via` to `m6_via`).

### Bank files
* New **Bank Files** page (after Response): **Files** = *Export Bank* (writes the shown bank to `exports/<Bank>.mmbank`) or *Reload Folder* (re-reads `banks/`); **State** and **User** show progress and how many user banks are loaded.
* Put `.mmbank` files in `/data/UserData/schwung/mean-module/banks/` (Schwung Manager's file browser can upload there). They load when the module loads and on Reload Folder, and appear after the factory banks on the Bank knob as **U1** to **U8** (alphabetical by file name, up to 8 banks of 48 presets).
* Plain-text format, only non-default settings needed, unknown keys ignored, values clamped; the FX Bank, stick position and bank controls can never be part of a sound. See `docs/BANK_FILES.md` and `banks/Example.mmbank`.
* Done by a background thread started when the library loads; the audio path never touches files. Program Change bank select 18..25 reaches user banks.
* New `tests/host_test.c` coverage: reload, ranges, forbidden keys, user-bank browsing, state round trip, export, an exported bank sounding like the original, and the shipped example file.

### Horror
* **Calliope** now has a fast attack, a short bright burst (filter and breath noise) at the start of each note that closes quickly, a very short release, more rasp and drive, and keeps its out-of-tune pitch wander (Mod Wheel detunes it further; held notes sag slightly).
* **Calliope** now fakes a chord: the synth is mono, so held notes are cycled very quickly (a tied 1/32 arpeggio locked to Move's tempo), like several pipes speaking at once; a single note stays steady. **Calliope Solo** is the plain mono version and **Calliope Fifths** puts the second pipe a (slightly flat) fifth above instead of an octave.
* The whole bank is harsher and more unsettling: minor-second and tritone clashes, heavy drive and resonance, random pitch and filter movement, noise. Two new presets: **Shriek** and **Hive Mind** (20 in the bank with Calliope Solo and Calliope Fifths).

### Library bite review
* Rendered and ranked every preset by drive, resonance, noise and brightness (`tests/preset_audit.c`). 44 presets that were meant to bite but sat at default drive got more filter drive and drive (Bass, Lead, Brass, Stab, Drone, FX and a few Sets). Deliberately clean, soft or bright presets were left alone.
* 275 presets in total. Nothing here was judged by ear; levels, peaks and audibility are checked by the test suite.

## 0.9.2

Preset banks, Sets, a Horror bank, and a review of the whole library. Details and the method: `docs/PRESET_BANKS.md`.

### Banks
* The presets are in 17 banks: Init, Bass, Sub, Lead, Pluck, Keys, Stab, Brass, Arp, Seq, Pad, Texture, Vox, Drone, FX, Horror, Sets. The Main page **Cat** knob is now **Bank**; the jog wheel browses only the current bank, and names no longer repeat the bank word.
* External Program Change: bank select 0 keeps the flat numbering; bank select 1..17 picks a program inside that bank.
* Presets saved in a set before this version keep their sound (all parameters are stored), but the preset number they remember may now point at a different name.

### Sets (24 presets)
Eight themes of three presets meant to be played together on three Move tracks: Gritty Punk, Acid Rain, Black Mass, Iron Works, Cold Wave, Dub Cellar, Doom and Neon Static (the bright one). Named like "Gritty Punk 1 - Bass".

### Horror (16 presets)
Haunted-park and crypt sounds, including an out-of-tune **Calliope** (Mod Wheel detunes it further) and an arpeggiated **Carousel**, plus Crypt Organ, Music Box, Cellar Door, Slasher Stab, Dread Drone, Whispers, Heartbeat, Theremin Ghost, Fly Swarm, Toll, Rotting Pipes, Insect Nest, Possessed Choir and Grave Rumble.

### Library review
* Measured every preset (new `tests/preset_audit.c`). The library skewed dark and narrow; 25 new presets add clean, bright and open sounds across the banks (Init Saw/Square/Detuned/Sub Octave/Noise, clean pulse bass, PWM, glass sync and highpass leads, tines, electric piano, clean organ, bright chord, clean brass swell, bright and latched arps, strings, PWM choir, shimmer). The dark core is untouched.
* Removed eight near-duplicates (Bass Overdriven, Bass Octave Stack, Brass Stab, Lead Grit Fifth, Pluck Slag, Keys Pluck, Keys Gritty Clav, Keys Grit Bell); fixed a duplicate name (the Note Age pad is now Pad Age Swell); moved five presets to the bank they belong in.
* 271 presets in total. New presets are measured (level, brightness, duplicates), not yet tuned by ear.

## 0.9.1

### FX Bank acts only on its own page
* Touching a knob on any other page no longer fires an FX Bank effect or counts toward a double tap; the effects play only while the **FX Bank** page is showing.
* How: the host does not tell a module which page is up, but it re-reads the visible page's knobs several times a second. The FX Bank acts only within ~0.75 s of such a read, and a touch that began on another page stays inert even if you jog to the FX Bank while holding it.
* New **FX Touch** setting (Voice page, last knob): *FX Page Only* (default) or *Any Page* for the 0.9.0 behaviour. Presets leave it alone, like the rest of the bank.
* 93 parameters. If an effect ever fails to respond on the FX Bank page, set FX Touch to Any Page and tell me; the page detection is inferred from the host's behaviour and not yet confirmed on the device.


## 0.9.0

A new way to perform, a re-organised and illustrated UI, and a round of audio-path measurements.

### FX Bank (replaces the single "touch knob" gesture)
* Knobs 1-4 are four touch effects. Hold a knob to play its effect, turn it to set how much (the level is remembered), double-tap it to step to the next effect.
* 14 effects: Grit, Filter Open, Filter Close, Crunch, Resonance, Noise, Pitch Rise, Pitch Dive, Vibrato, Wobble, Gate Chop, Detune, Arp Faster, Hold.
* A new **FX Bank** page shows the four levels as faders over the four effect types.
* Taps are told apart from playing: an effect only starts after a ~90 ms touch, and the second tap of a double tap stays silent.
* Effects fade in and out (no clicks). Wobble and Gate Chop lock to Move's tempo. Presets never change the bank.
* Removed: the old Touch Knob and Touch Does parameters (no factory preset used them; a saved state that had them set simply ignores them).

### UI
* Page order is now Main, Perform, FX Bank, Stick, Oscillators, Filter, Amp Env, Filter Env, LFO 1, LFO 2, FM and X-Mod, Tuning, Voice, Response, Matrix. The Live, Play and Arp pages are merged into **Perform**; the old Perform page is **Response**.
* **Main** page: filter graphic with Mode, Grit, amp attack/release, a **Cat** knob that jumps to the first preset of a category (Bass, Lead, Pad ...), and the volume fader.
* **Env 1 / Env 2** are now **Filter Env / Amp Env** on every page and in the matrix, and each envelope page carries its related controls.
* New graphics: the filter picture shows the filter Mode; LFO 1 draws a moving wave from Wave, Rate and cutoff depth; the oscillators show waveform pictures; level knobs are faders.
* Values read with units (percent, semitones, cents, octaves). Clearer knob names (no more EFM 1 / LFM 2, no letter-O-looks-like-zero labels, no two knobs both called Oct).
* On-device help rewritten: a **Start here** page, FX Bank and FX list pages, and every page updated.
* New docs: `docs/UI_GUIDE.md` (page map, FX Bank, the journeys the layout was checked against).

### Sound
* **Knob smoothing**: cutoff, resonance, filter drive, drive, grit and volume follow their knobs over ~12 ms, so sweeping with the encoders no longer steps. Stepped-knob "zipper" energy fell from 12% above a smooth sweep to within 1.4%. Matrix and LFO modulation are not smoothed. A note that starts from silence takes the knobs as they are.
* **Noise Color** (Oscillators page): white to dark. 0 is exactly the old noise.
* Osc 2 is now computed before Osc 1, so X-Mod uses the current Osc 2 sample rather than one from a moment earlier (the X-Mod patches sound slightly different, about 25 dB below the signal).
* The filter's self-oscillation kick is now only injected when resonance can sustain it. Idle noise floor on clean patches is now digital zero (it was -112 dBFS, below 16-bit resolution).
* Measured and left alone: moving the limiter into the oversampled loop (no change in aliasing, +6% CPU), an IIR halfband (the existing filter costs about 3 us of a ~35 us block), 4x oversampling (3-7 dB less aliasing for ~1.9x the CPU; waiting for real device CPU numbers). See `tests/audio_eval.c`.

### Other
* 92 parameters; 214 presets. New `Preset Category` and `Noise Color` parameters.
* `tests/dump_contract.c` and `scripts/preview.sh` render the device pages offline.

## 0.8.0
Six new matrix sources (Pitch Bend, Arp Step, Note Age, Smooth Random, Osc 1, Sustain Pedal), seven new targets (Grit, Glide Time, Vibrato Depth, LFO Depth, LFO 2 Depth, Env Amount, Drift Amount), 14 presets.

## 0.7.0
Live page, stick flick steps Arp Rate and Arp Mode, knob-touch gestures.

## 0.6.0
Osc 1 tuning, Noise and Osc 2 as matrix sources, Osc FM targets, controller CCs, Program Change, RPN bend range.

## 0.5.x
Six-slot mod matrix, second LFO with tempo sync, a large preset library in 15 categories, stick and matrix marking.

## 0.1 - 0.4
Oscillators, nonlinear filter, envelopes, Grit macro, joystick emulation, arpeggiator.
