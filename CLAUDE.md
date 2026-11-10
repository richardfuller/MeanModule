# Mean Module

Gritty analog-style mono synth for Ableton Move, built as a Schwung `sound_generator` (plugin_api_v2, native C,
aarch64 `dsp.so`). Module id `meanmodule`, abbrev `MEAN`. Two PolyBLEP oscillators + noise, nonlinear Sallen-Key
(Korg35-topology) filter, two ADSRs, LFO, post-VCA drive; oscillator/filter/drive run 2x oversampled.

## Rules
- `src/host/plugin_api_v1.h` is vendored byte-for-byte from Schwung (commit 673e67ab). Never edit it or add fields
  (a mismatched header has boot-looped devices).
- Every entry point, including create_instance/set_param/get_param, runs on the audio callback: no malloc, locks, I/O,
  logging. Instances come from a static pool of 16; JSON is formatted once per instance with snprintf.
  The one exception to "no I/O" is the bank-file worker thread (`bank_worker`, docs/BANK_FILES.md): started once at library load (`move_plugin_init_v2`),
  demotes itself to SCHED_OTHER on cores 0-2, pins the library with `dlopen(RTLD_NODELETE)`, and talks to the audio side only through atomics and one of
  three static buffers published with a release store. Never add I/O or locks to an entry point; post a request flag instead.
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
- Presets are named "<Bank> <Name>"; the first word is the bank (Init, Bass, Lead, Drone, Arp, Para, Chaos, Horror, and Set = the Sets bank, named "Set <Theme> <n> - <Role>"; 0.11.0 replaced the older 17-bank library). Banks are built at load from the names (`banks_init`), array order is kept inside a bank, the host browser lists only the current bank (`bank_preset` / `bank_count`) and shows names without the bank word (`preset_name`; `preset_full` has the whole name). No percussion; dark/industrial theme with bright and clean sounds across the banks. See `docs/PRESET_BANKS.md`; `tests/preset_audit.c` renders every preset for duplicate and gap checks. A preset must stay audible after 1.2 s held (host test), so plucks keep a little amp sustain. Matrix slots in presets use the `MX(slot, S_*, D_*, amt)` helper.

- LFOs: two (`lfo_*`, `lfo2_*`), waves Tri/Sq/SawDn/SawUp/Random, Sync = 1/2..1/32 locked to `get_beat_position` (free-run at bpm when stopped), or Audio (value 8: 20 Hz..2 kHz).
  `lfo_gate` (LFO Retrigger: Off/LFO 1/LFO 2) restarts both envelopes on each cycle of that LFO while a note is held.
- Mod matrix: 6 slots (`m1_src/dst/amt/via`..; Via = a second source that scales the slot amount, 0 = Off), 20 sources x 30 targets (Env 1/2 are named Filter Env / Amp Env in the UI; Noise, Osc 1/2, Pitch Bend, Arp Step, Note Age, Smooth Random, Sustain Pedal are sources; Osc 1/2 FM, Grit, Glide, Vib Depth, LFO 1/2 Depth, Env Amount, Drift, Osc 1 Pitch, Filter/Amp Env Attack/Decay are targets; LFO Depth is a gain with 0 = silent), summed per sample in render_active; design and ranges in
  `docs/MOD_MATRIX.md`. Stick Y target 4 = Hold Toggle is the only thing that flips Hold from the stick.

- MIDI from EXTERNAL sources only: CC 5/7/11/71-74, bank CC0 + Program Change, RPN 0 bend range (see `docs/MOD_MATRIX.md`). Never map
  CCs from Move's own surface: CC 71-78 are its knobs.

- FX Bank (no claimed buttons, no JS UI): knobs 1-4 are four touch effects (`fx1..4_type`, `fx1..4_lvl`; touch notes 0-3 via `touch_observe`). Acts only while the FX Bank page is showing (`fx_scope` 0, default): the host cannot say which page is up, so
  `get_param` stamps `fx_page_t` whenever an FX key is read (the host re-reads the visible page ~5x/s) and `fx_here()` requires a stamp within `FX_PAGE_BLK`;
  a touch is "armed" (`fx_armed`) only if it began then. `fx_scope` 1 = any page. Inferred, not confirmed on the device; the Remote UI reading all keys would act as Any Page.
  Touch applies the assigned effect at its level once the touch has lasted `FX_DELAY_BLK` (a tap stays silent); turning sets the level (kept);
  a double tap (short first touch, second within `FX_DTAP_BLK`) steps the type and keeps the second touch silent. Effect amounts slew
  (15 ms in, 50 ms out) so nothing clicks. 14 types (`FXT_*`): Grit, Filter Open/Close, Crunch, Resonance, Noise, Pitch Rise/Dive, Vibrato, Wobble
  (tempo-locked 1/8), Gate Chop (tempo-locked 1/16), Detune, Arp Faster (+0..3 rate steps), Hold. Presets never touch the bank.
  Stick X/Y can also flick Arp Rate / Arp Mode. Root page order: Perform, FX Bank, Stick first.
- Knob smoothing: cutoff (+stick/aftertouch cutoff), resonance, filter drive, drive, grit and volume follow their knobs through ~12 ms one-poles
  (`sm_*`), so a stepped knob never zippers; modulation is not smoothed. A note starting from (near) silence snaps to the knobs.
- Preset Bank (`preset_cat`, knob "Bank") jumps to the first preset of that bank and follows the current preset. External Program Change only acts when `prog_chg` = Accept (default Ignore; `last_change`/`pc_seen` are diagnostics): CC0 = 0 flat, CC0 = n picks program inside bank n-1. Noise Color is a level-compensated one-pole (0 = exact white).
- Audio-path changes are measured, not guessed: `tests/audio_eval.c` (alias share, noise floor, self-oscillation, zipper, cross-mod, noise tilt, cost).
  Measured and rejected: soft limiter inside the 2x loop (no aliasing change, +6% CPU), IIR halfband (the FIR is ~3 us/block of ~35), 4x oversampling
  (-3..-7 dB aliasing at ~1.9x CPU; revisit only with device CPU numbers).
- UI previews: `bash scripts/preview.sh` renders every page with Schwung's own renderer (needs Docker and the `ref/schwung` clone) into build/prev/png.
  Short names are <= 5 characters (6 fit but crowd); avoid the letter O next to digits (it reads as zero).

## Exponential knobs (units to remember when writing presets)
- Note names follow Move and Live: MIDI 60 = C3 (36 = C1). The Main page shows `note_now` (read-only int 0..127 named "...note": Schwung renders it as a note name). The bottom-right "i1" while a note is held is Schwung's own MIDI-channel indicator, not ours.
- Env times: 0.0005 x 20000^x s (0.4 = 26 ms, 0.5 = 70 ms, 0.6 = 190 ms, 0.7 = 510 ms, 0.8 = 1.4 s, 1.0 = 10 s). LFO rate: 0.05 x 800^x Hz (0.5 = 1.4 Hz, 0.7 = 5.4 Hz, 0.9 = 20 Hz); Audio sync: 20 Hz x 100^x.
- Cutoff: 20 Hz x 2^(9.81 x) (0.5 = 600 Hz, 0.68 = 1.9 kHz, 0.8 = 5 kHz) before Key Track, envelope (Filter Env to Cutoff x 5 octaves) and the Grit macro. Glide: 2 x x^2 s. Filter Drive gain 0.25 x 2^(4x); Drive gain 1 + 5d + 6d^2.
- `tools/presetgen/units.py` prints every preset in these units; `tests/brightness.c`, `tests/loudness.c` and `tests/register.c` fit cutoff, level (Level Trim) and register tilt (Key Level, dB/octave around C3, bass judged by plain RMS) by measurement; `tools/presetgen/calib.sh` runs them all.

## Releasing
- Bump the version in four places: `src/module.json`, `release.json`, `MM_VERSION`/`MM_VERSION_S` in the plugin, and the first line of `src/help.json`
  (plus `MM_VERSION_TEST` in `tests/host_test.c`); the host test fails if they disagree. Then update `CHANGELOG.md`.

## Adding a preset / parameter
- Preset: the table is generated by `tools/presetgen/` (edit a `bank_*.py`, run `tools/presetgen/calib.sh`, which also refits each preset's Level Trim so the library plays at one loudness: `tests/loudness.c`, checked by `scripts/test.sh`; Arp-bank presets must arpeggiate). A hand-written `PRE("Name", {P_ID, value}, ...)` row also works (starts from defaults) but needs a trim and is overwritten by the next generator run. Count is derived.
- Parameter: add one `X(...)` row to `PARAMS` (use a proper-case label), a `SHORT_NAME` (<= 5 chars; the knob cell is
  ~5 chars wide) and `SHORT_OPTS` for enums (3-char squares), then put its key in a page macro (`K_OSC`, `K_FILT`, ... at most 8
  per page) near `HIERARCHY`. It is automatically in state, chain_params and the stress tests; the host test fails if a param
  has no name, no short_name or is on no page.
- UI: pages are levels in `HIERARCHY` (Perform, FX Bank, Stick first, then Oscillators, Filter, Amp Env, Filter Env, LFO 1, LFO 2, FM and X-Mod, Tuning, Voice, Response, Matrix 1-2/3-4/5-6); root (Main: filter graphic, Grit, amp A/R, Cat, Volume) carries the
  performance macros and preset browser. `get_param("name")` returns "Mean Module"; module.json `name` is "Mean Mod" (the slot header
  cuts longer names) and abbrev is "MM".
