# Audio testing

Everything that can be measured is measured, so the sound can be judged by ear on the things that cannot. All of it runs in Docker (`scripts/test.sh`, `scripts/health.sh`, `tools/presetgen/calib.sh`).
Keep the container limits at 2 CPUs and 3 GB and run one job at a time: three jobs with 4 CPUs each crashed the development PC.

| Tool | What it measures | Hard-fails when |
| --- | --- | --- |
| `tests/host_test.c` | every preset is finite, bounded, audible, free of DC; arp patterns, LFO, matrix, FX bank, bank files, state | any check fails (also under ASan/UBSan and the RT guard) |
| `tests/loudness.c` | A-weighted loudness (loudest 150 ms) of a held C3 major chord; counts note starts for the Arp bank | a preset is more than 2.5 dB from -19 dB, or an Arp preset repeats fewer than 8 times in 4.5 s |
| `tests/register.c` | level on five notes across the preset's home register (A-weighted; plain RMS for bass), velocity 40 vs 127, a 150 ms note | the level moves more than 6 dB across the register (unless the limiter headroom caps the fit) |
| `tests/brightness.c` | spectral centroid of one held note (used to lift the dullest static filters) | (fitting tool only) |
| `tests/health.c` | see below | see below |
| `tools/presetgen/units.py` | every preset in real units (ms, Hz, seconds) | (report only) |

## Health checks (`tests/health.c`, `scripts/health.sh`)

One fresh instance per run. Notes are played in the preset's home register (the same table as `register.c`; C3 = MIDI 60).

| Column | Meaning | Hard flag |
| --- | --- | --- |
| `clip_pct` | share of samples at the limiter (above 0.98 full scale) on a hard-played low, middle and high note | `CLIP` above 3% |
| `crest_db` | peak to RMS on the middle note (a triangle is about 4.8 dB, a square 0 dB; very low = squashed) | `NOTCLEAN` for the presets described as clean (Bass Sub 1, Log Tri, Glide Tri, Void Walker 1 Sub) below 3 dB; `squashed?` (note only) below 2 dB |
| `rel_ms` | time for the level to fall 20 dB after note-off | `ABRUPTREL` under 10 ms on a sustained preset (a click) |
| `tail_s` | time until the sound is below -60 dBFS after note-off | `LONGTAIL` over 6 s |
| `cents` | measured pitch against the played note (oscillator coarse and Octave included) | (note only: FM, noise and zaps are meant to be off) |
| `*_db`, `*_bright` | level and brightness change when the Mod Wheel, Aftertouch, Stick X, Stick Y go to full | `wheel?` / `aftertouch?` (note only): the preset routes the controller, and the level, brightness, level wobble and pitch wobble all stay put |
| `tempo_ratio` | arp presets: onset rate at 150 BPM against 90 BPM (1.67 expected) | `TEMPO` outside 1.15 to 2.2 (tied arps are skipped) |
| `cpu_avg_us`, `cpu_max_us` | host time per 128-frame block (the device is roughly 4-6 times slower) | `cpu?` (note only) above 60 us |
| (non-finite output) | | `NONFINITE` |

## Ideas not built yet

* Aliasing at the top of the register (the share of energy that is not at harmonics), using the `audio_eval.c` metric per preset.
* A golden snapshot of the health CSV in the repo, with a diff report so an unintended change to a preset shows up in the build.
* A spectral similarity table inside each bank (near-duplicates), from `preset_audit.c`.
* Chord and two-note behaviour for the Para bank (does the second oscillator stay in tune with the first at every register).
* Onset click detection (a spectral thump at note-on) for the presets with a zero attack.
