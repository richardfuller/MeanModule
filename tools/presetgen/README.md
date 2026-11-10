# Preset generator

The factory `PRESETS[]` table in `src/dsp/mean_module_plugin.c` is generated from these files, so a whole bank can be edited as compact Python and every preset gets the
same character floor and a measured Level Trim.

* `dsl.py` the `pre()` helper (short parameter names, matrix slots as tuples, the character floors in `apply_character`, the trims from `trims.json`)
* `bank_*.py` one file per bank, `arps.py` the arpeggiator settings of the Arp presets
* `build_all.py` rewrites the table in the plugin source (everything from `/* ---- Init ---- */` to the end of `PRESETS`)
* `calib.sh` regenerates, then runs `tests/loudness.c calibrate` (a few passes) to refit the Level Trims. Run it after adding or changing any preset.

Editing the table by hand still works (see CLAUDE.md), but the next `build_all.py` run would overwrite it.
