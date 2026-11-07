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
| LFO | -1..+1 | the one LFO (Rate, Wave on the Matrix 1-2 page) |
| Env 1, Env 2 | 0..1 | Env 2 is also the VCA |
| Velocity | 0..1 | of the sounding note |
| Mod Wheel | 0..1 | CC1 |
| Aftertouch | 0..1 | raw pressure (not scaled by the Perform page's AT Amount) |
| Stick X, Stick Y | -1..+1 | the two stick knobs; they spring back when released |
| Key | -1..+1 | pitch of the note: C4 = 0, two octaves each way = +-1 |
| Random | -1..+1 | sample and hold, a new value for each note (each arp step) |

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
| LFO Rate | +1.0 on the 0..1 rate knob | from the previous sample, so it cannot feed back on itself |

Examples: LFO -> Cutoff 0.4 (wobble); Velocity -> Drive 0.4 (harder = dirtier); Stick Y -> Filter Input 0.7 (push into
saturation); Random -> Cutoff 0.35 (each note a different brightness); Env 2 -> Osc 2 Pitch (pitch drop).

## UI

Root -> Matrix 1-2, Matrix 3-4, Matrix 5-6 (immediately after Modulation). Each slot is three knobs, Src / Dst / Amt,
shown with three-letter options. Matrix 1-2 also carries LFO Rate and LFO Wave.

Cost: only active slots are evaluated; sources are read per sample, targets that need extra maths (Filter Input, Drive,
LFO Rate) recompute only when a slot uses them.
