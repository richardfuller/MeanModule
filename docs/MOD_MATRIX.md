# Signal path and mod matrix

## Audio path (per voice, mono)

```
 Osc 1 ──┐ (level 1)
 Osc 2 ──┼─ sum ─► Filter Input drive ─► Sallen-Key filter ─► VCA (Env 2) ─► Drive ─► Volume ─► DC block ─► limiter
 Noise ──┘ (level 2, noise)   (tanh)      (LP/BP/HP/combos)
   ▲  ▲
   │  └── X-Mod: Osc 2 output frequency-modulates Osc 1
   └───── Oscillators, filter, drive run 2x oversampled, halfband-decimated to 44.1 kHz
```

Fixed routings (the front-panel knobs): Env 1 and the LFO to Cutoff, to each oscillator's FM and to Shape; velocity to
cutoff / amp; aftertouch to one destination; Key Track; the Stick; the Grit/Dark macro, which sits on top of the patch.

## Mod matrix

Six slots, each **Source -> Target x Amount** (Amount -1..+1). Slots add together, and several slots may share a target.
A slot does nothing while its source is Off, its target is Off or its amount is 0. The matrix adds to the fixed routings above
and to the stored knob values; it never changes the stored values.

| Source | Range | Notes |
| --- | --- | --- |
| LFO, LFO 2 | -1..+1 | the two LFOs (LFOs page: rate, wave, sync) |
| Env 1, Env 2 | 0..1 | Env 2 is also the VCA |
| Velocity | 0..1 | of the sounding note |
| Mod Wheel | 0..1 | CC1 |
| Aftertouch | 0..1 | raw pressure (not scaled by the Perform page's AT Amount) |
| Stick X, Stick Y | -1..+1 | the two stick knobs; they spring back when released |
| Key | -1..+1 | pitch of the note: C4 = 0, two octaves each way = +-1 |
| Random | -1..+1 | sample and hold, a new value for each note (each arp step) |
| Noise | -1..+1 | white noise, a new value every sample (FM, cutoff, anything) |
| Osc 2 | -1..+1 | the Osc 2 audio output, whatever its Level: audio-rate modulation of anything |
| Osc 1 | -1..+1 | the Osc 1 audio output, whatever its Level (Osc 1 FM from this source is self-FM) |
| Pitch Bend | -1..+1 | the bend wheel, as a plain -1..+1 (independent of Bend Range, so Bend Range 0 gives a bend that only the matrix sees) |
| Arp Step | 0..1 | position of the current arp note in the pattern: first note 0, last note 1 (0 with the arp off) |
| Note Age | 0..1 | a ramp from 0 to 1 over 5 seconds from each new note; it stays at 1 until the next trigger (legato notes do not restart it) |
| Smooth Random | -1..+1 | a wandering value: a new random target every 0.4 s, slewed twice (about 1 Hz) so it never jumps |
| Sustain Pedal | 0..1 | CC64, slewed over about 5 ms so it does not click |

| Target | Amount 1.0 means | Notes |
| --- | --- | --- |
| Pitch | +12 semitones | both oscillators |
| Osc 2 Pitch | +12 semitones | Osc 2 only (detune, intervals) |
| Osc 1 Shape, Osc 2 Shape | +0.5 | added to the shape knob, clamped 0..1 |
| Osc 1 Level, Osc 2 Level | +1.0 | clamped 0..1.5 |
| Noise | +1.0 | clamped 0..1 |
| Cutoff | +5 octaves | |
| Resonance | +1.0 | clamped 0..1 |
| Filter Input | +1.0 | the filter-input drive knob, clamped 0..2 (with Grit) |
| Drive | +1.0 | post-VCA drive, clamped 0..1.6 |
| Amp | gain = 1 + amount x source | clamped 0..2; negative amounts duck the sound |
| X-Mod | +1.0 | clamped 0..1 |
| Osc 1 FM, Osc 2 FM | +2 octaves of exponential FM | adds to the FM knobs and X-Mod |
| LFO Rate, LFO 2 Rate | +1.0 on the 0..1 rate knob | from the previous sample, so it cannot feed back on itself; ignored while that LFO is synced |
| Grit | +1.0 on the Grit/Dark macro | added to the Grit knob and the Stick, clamped -1..+1 (see below) |
| Glide Time | +1.0 on the Glide knob | clamped 0..1; slides notes even with Glide at 0 |
| Vibrato Depth | +1.5 semitones | added to the Vibrato knob, wheel and aftertouch, never below 0 |
| LFO Depth, LFO 2 Depth | gain = the slot total | scales that LFO everywhere it is used (knob routings and as a matrix source), clamped 0..2. **0 means silent**: use it with a source that starts at 0 (Mod Wheel, Note Age, Env 1) to fade the LFO in. Applied one sample late |
| Env Amount | +1.0 | added to the filter envelope depth (the Env 1 -> Cutoff knob, 5 octaves at full), so Velocity or Key can scale the envelope |
| Drift Amount | +1.0 on the Drift knob | clamped 0..1 |

Grit as a target moves the whole macro at once (filter drive, drive, resonance, noise, drift, cutoff, makeup gain), per sample. Key -> Grit at a
negative amount makes low notes grittier; Pitch Bend -> Grit makes a push into the wheel dirtier.

Examples: LFO -> Cutoff 0.4 (wobble); Velocity -> Drive 0.4 (harder = dirtier); Stick Y -> Filter Input 0.7 (push into
saturation); Random -> Cutoff 0.35 (each note a different brightness); Env 2 -> Osc 2 Pitch (pitch drop); Note Age -> LFO Depth + LFO -> Cutoff (a wobble that fades in);
Arp Step -> Cutoff (each arp pass climbs); Smooth Random -> Drift Amount (a loose, wandering tuning).

## UI

Root -> Matrix 1-2, Matrix 3-4, Matrix 5-6 (after LFOs). Each slot is three knobs, Src / Dst / Amt,
shown with three-letter options.

Cost: only active slots are evaluated; sources are read per sample, targets that need extra maths (Filter Input, Drive,
LFO Rate, Grit, Glide Time) recompute only when a slot uses them.

## LFOs

Two LFOs on the LFOs page (Root -> LFOs, after Modulation): Rate, Wave, Sync each.

* Waves: Triangle, Square, Saw Down, Saw Up, Random (a new sample-and-hold value every cycle).
* Sync: Free, or 1/2, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 of the tempo, one cycle per division. While Move's transport runs the
  phase is locked to the beat position; with the transport stopped the LFO free-runs at the host tempo. Rate is ignored when synced.
* LFO 1 also feeds the fixed routings (Cutoff, osc FM, Shape). LFO 2 is available through the matrix only.

## Tuning

Root -> Tuning: Osc 1 and Osc 2 Coarse and Fine, Octave, Tune, Drift, Bend Range. Both oscillators have their own coarse (+-24 semitones)
and fine (+-50 cents) now, as on the hardware.

## MIDI controllers (external MIDI only, i.e. USB-A devices)

| Message | Effect |
| --- | --- |
| CC 1, 64 | mod wheel, sustain pedal |
| CC 5, 7, 11 | glide time, volume, expression (output level) |
| CC 71, 72, 73, 74 | resonance, Env 2 release, Env 2 attack, cutoff |
| CC 0 + Program Change | selects a preset: bank * 128 + program |
| RPN 0 (CC 101, 100, 6) | pitch bend range |
| CC 120, 121, 123 | sound off, reset controllers, all notes off |

These are ignored when they come from Move's own surface, because CC 71-78 there are the eight knobs.
Pitch bend, channel pressure and poly aftertouch are handled from every source.
