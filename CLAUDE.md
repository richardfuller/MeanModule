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

## Adding a preset / parameter
- Preset: add a `PRE("Name", {P_ID, value}, ...)` row in `PRESETS` (starts from defaults). Count is derived.
- Parameter: add one `X(...)` row to `PARAMS`; add its key to a `PAGES` row if it should be on an encoder page
  (8 per page, positional). It is automatically in state, ui_hierarchy, chain_params and the stress tests.
