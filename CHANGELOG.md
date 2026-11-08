# Changelog

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
