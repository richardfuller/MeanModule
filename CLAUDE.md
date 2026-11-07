# Mean Module

Gritty analog-style mono synth for Ableton Move, built as a Schwung `sound_generator` (plugin_api_v2, native C,
aarch64 `dsp.so`). Module id `meanmodule`, abbrev `MEAN`. Two PolyBLEP oscillators + noise, nonlinear Sallen-Key
(Korg35-topology) filter, two ADSRs, LFO, post-VCA drive; oscillator/filter/drive run 2x oversampled.

## Rules
- `src/host/plugin_api_v1.h` is vendored byte-for-byte from Schwung (commit 673e67ab). Never edit it or add fields
  (a mismatched header has boot-looped devices).
- Every entry point, including create_instance/set_param/get_param, runs on the audio callback: no malloc, locks, I/O,
  logging. Instances come from a static pool of 16; JSON is formatted once per instance with snprintf.
- Own code is MIT. No third-party code is included. Do not copy GPL code.
- `ref/` holds third-party clones for reading only; do not ship from it.

## Layout
- `src/dsp/mean_module_plugin.c` the whole plugin (params table, voice, DSP, presets, state, JSON).
- `src/module.json`, `src/help.json` manifest and on-device help. `release.json` placeholder for Schwung Manager.
- `tests/host_test.c` desktop host test; `tests/rt_guard.c` LD_PRELOAD malloc/lock guard; `tests/render.c` WAV renderer.
- `scripts/`: `build.sh` (aarch64 via Docker -> dist/), `test.sh` (ASan, UBSan, RT guard), `listen.sh` (WAV suites ->
  out/), `install.sh` (scp, needs SSH key; not set up). Docker Desktop required; on Windows add
  `/c/Program Files/Docker/Docker/resources/bin` to PATH in Git Bash.
- `docs/ABI_NOTES.md` verified platform facts; `docs/DEPLOY.md` how to build, package and install on the Move (works, no SSH).

## Behaviour notes
- Voice idles when env2 is idle: render_block writes exact zeros and skips all DSP (no CPU when silent).
- Drone: OFF / HOLD (amp open while a key is down) / LATCH (holds after release until CC123/120, preset change or drone off).
  A drone never sounds without a note.
- aarch64 builds set FPCR.FZ during render (restored after); filter/DC-blocker states are flushed of denormals.
- Grit/Dark (`grit`, -1..+1) is a macro applied at render time on top of the patch (filter drive, drive, resonance, noise, drift,
  cutoff); stored params are untouched; 0 = patch as saved. Envelope/filter graphics come from `VIZ_*` tables (chain_params `viz`).
- Input layer: keys / sustain pedal (CC64) / Hold decide the held set (`held[]`); it feeds the voice directly, or the arp when
  `arp_mode` != 0. Hold latches notes; the first key of a new gesture replaces them. Arp = host tempo (`get_bpm`, default 120), steps locked to the
  transport grid via `get_beat_position` when it runs (free-running otherwise; the first note sounds at once); gate 100% ties notes. render_block splits at arp events.
- Stick (joystick emulation): `stick_x`/`stick_y` live on the Stick page as knobs 1 and 2; `touch_observe` delivers knob touch
  edges (source 5, notes 0/1) and release springs the value to 0. With no touch events a timer recentres (120-150 blocks).
  with Y assigned to Hold Toggle, Y held fully down ~0.23 s toggles Hold (a sweep never does); stick bend is >= 7 semitones (2x Bend Range). Targets: X = Bend / Vibrato Speed / Cutoff / Grit; Y = Glide / Vibrato Depth / Cutoff / Grit.
- The 4-slot screen shows `get_param("name")` ("Mean Mod"); the Remote UI header also uses it.
- Presets are named "<Category> <Name>" and kept in category order: Init, Bass, Sub, Lead, Pluck, Keys, Stab, Brass, Arp, Seq, Pad, Texture, Vox, Drone, FX (no percussion; dark/industrial theme). Matrix slots in presets use the `MX(slot, S_*, D_*, amt)` helper.

- LFOs: two (`lfo_*`, `lfo2_*`), waves Tri/Sq/SawDn/SawUp/Random, Sync = 1/2..1/32 locked to `get_beat_position` (free-run at bpm when stopped).
- Mod matrix: 6 slots (`m1_src/dst/amt`..), 12 sources x 16 targets, summed per sample in render_active; design and ranges in
  `docs/MOD_MATRIX.md`. Stick Y target 4 = Hold Toggle is the only thing that flips Hold from the stick.

## Adding a preset / parameter
- Preset: add a `PRE("Name", {P_ID, value}, ...)` row in `PRESETS` (starts from defaults). Count is derived.
- Parameter: add one `X(...)` row to `PARAMS` (use a proper-case label), a `SHORT_NAME` (<= 5 chars; the knob cell is
  ~5 chars wide) and `SHORT_OPTS` for enums (3-char squares), then put its key in a page macro (`K_OSC`, `K_FILT`, ... at most 8
  per page) near `HIERARCHY`. It is automatically in state, chain_params and the stress tests; the host test fails if a param
  has no name, no short_name or is on no page.
- UI: pages are levels in `HIERARCHY` (Play, Stick, Arp first, then Oscillators, Filter, Env 1, Env 2, Modulation, LFOs, Matrix 1-2/3-4/5-6, Perform); root carries the
  performance macros and preset browser. `get_param("name")` returns "Mean Module"; module.json `name` is "Mean Mod" (the slot header
  cuts longer names) and abbrev is "MM".
