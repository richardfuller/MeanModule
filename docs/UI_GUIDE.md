# UI guide

How Mean Mod is laid out on the Move, and why. The jog wheel turns pages; each page is a grid of eight knobs. Touch a knob to see its
name and value. Pages are in this order, with the things you reach for most first.

## Page map

| # | Page | What is on it |
| --- | --- | --- |
| 1 | **Main** | Filter graphic (Cutoff, Res, Mode), **Grit**, amp attack and release, **Cat** (preset category), Volume |
| 2 | **Perform** | Hold, Drone, Octave, Glide, and the whole arpeggiator (Arp, Rate, Arp Octaves, Gate) |
| 3 | **FX Bank** | Four touch effects: levels on top, effect types below |
| 4 | **Stick** | Stick X and Y, what each steers, bend range, vibrato, Hold |
| 5 | **Oscillators** | Wave and Shape for each oscillator (waveform pictures), then the mixer: Level 1, Level 2, Noise, Noise Color |
| 6 | **Filter** | The filter graphic with Mode, Input drive, Filter Env amount, Key tracking, Drive |
| 7 | **Amp Env** | ADSR graphic, Velocity to level, Drone |
| 8 | **Filter Env** | ADSR graphic, Env to cutoff, Env to each oscillator's FM, Velocity to cutoff |
| 9 | **LFO 1** | Wave, Rate and cutoff depth drawn as a moving wave; Sync; LFO to FM 1, FM 2 and Shape |
| 10 | **LFO 2** | Rate, Wave, Sync (used through the matrix) |
| 11 | **FM and X-Mod** | Osc 2 to Osc 1 cross modulation, envelope FM |
| 12 | **Tuning** | Coarse and fine for both oscillators, Octave, Tune, Drift, Bend range |
| 13 | **Voice** | Glide, Legato, Note priority, Drift, Vibrato, Bend range |
| 14 | **Response** | Velocity and aftertouch routing |
| 15-17 | **Matrix** | Six slots of source, target and amount |

The envelopes used to be called Env 1 and Env 2; they are now **Filter Env** and **Amp Env** everywhere, including the matrix sources.

## FX Bank

Knobs 1-4 are four performance effects. They act only while the FX Bank page is showing, so touching a knob to tweak something on
another page never fires an effect. (Voice page, **FX Touch**: *Any Page* restores the 0.9.0 behaviour of acting on every page.) The module
cannot ask the host which page is up; it notices that the host is re-reading the FX Bank's knobs (about 5 times a second while that page is
visible) and acts only within ~0.75 s of such a read. A touch that began elsewhere stays inert even if you jog to the FX Bank mid-touch.

* **Hold** the knob: its effect is on.
* **Turn** the knob (while holding it, or on the FX Bank page): how much. The level is remembered.
* **Double-tap** the knob: the next effect. The Type row on the FX Bank page shows what each knob does; OFF means nothing.

An effect starts once a touch has lasted about 90 ms, so the taps of a double tap stay silent, and the second tap of a double tap stays
silent for as long as you hold it. A first tap only counts toward a double tap if it was short (under about 0.2 s), so deliberately
re-touching a knob a moment later never changes its effect. Presets do not change the bank, so a performance setup survives browsing.

| Type | Does | At full level |
| --- | --- | --- |
| Grit | adds to the Grit macro | +100% (dirtier, darker) |
| Filter Open | cutoff up | +4 octaves |
| Filter Close | cutoff down | -5 octaves |
| Crunch | post-filter drive up | +1.0 |
| Resonance | resonance up | +0.8 |
| Noise | noise mixed in | +60% |
| Pitch Rise / Dive | pitch up / down | one octave |
| Vibrato | vibrato depth | +2 semitones |
| Wobble | cutoff swept by a triangle, tempo locked at 1/8 | 3 octaves |
| Gate Chop | tempo locked 1/16 gate | silent half of each step |
| Detune | Osc 2 pitch up | +1 semitone |
| Arp Faster | arpeggiator rate | up to 3 steps faster |
| Hold | Hold on while touched (level is ignored) | |

Two effects on the same type add (clamped), and every effect fades in over about 15 ms and out over about 50 ms.

## User journeys this layout was checked against

| I want to... | Before | Now |
| --- | --- | --- |
| Hear something and make it dirtier | Grit was there but the screen gave no hint what it did | Main page: filter picture, Grit, Cat, all on the first screen; the help's first page says what to try |
| Find a sound among 214 presets | One long list | **Cat** jumps to Bass, Lead, Pad ... and the jog browses within |
| Shape the amp or filter envelope | Pages called Env 1 and Env 2, with the envelope amounts on other pages | Amp Env and Filter Env pages, each with its graphic and its related controls |
| Play it live | Hold, Arp and Play on three pages, and a single touch effect | Perform page (Hold, Drone, arp) and the FX Bank |
| Understand a knob | Short names such as EFM 1 and LFM 2 | Plain names (EFM1, FM 1), values with units (%, st, cents, oct) |
| See what the filter does | Cutoff and resonance only | The filter graphic includes the Mode (LP, BP, HP and combinations) |
| See what the LFO does | Rate and wave as numbers | A moving wave that follows Wave, Rate and depth |
| Set levels | Dials | Faders for the oscillator levels, noise and volume |

## Previewing pages without a Move

`bash scripts/preview.sh` renders every page with Schwung's own screen renderer (through Docker) into `build/prev/png`. It needs the
`ref/schwung` clone. Values in the previews are synthetic, so judge layout and graphics, not settings.
