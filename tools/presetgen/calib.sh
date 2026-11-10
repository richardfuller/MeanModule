#!/usr/bin/env bash
# Regenerate the PRESETS table from the bank_*.py files, then level-match it: tools/presetgen/calib.sh [passes]   (needs Docker)
# Each pass rebuilds the plugin, runs tests/loudness.c calibrate (-19 dB A-weighted) and stores the Level Trims in trims.json.
cd "$(dirname "$0")/../.."
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
for i in $(seq 1 ${1:-3}); do
  python tools/presetgen/build_all.py >/dev/null
  MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/build" -w /build mean-module-builder bash -c "gcc -std=gnu11 -O2 -fPIC -shared -Isrc/host src/dsp/mean_module_plugin.c -o build/test/dsp.so -lm -ldl -lpthread && gcc -std=gnu11 -O2 -Isrc/host tests/loudness.c -o build/test/loudness -ldl -lm && build/test/loudness build/test/dsp.so calibrate -19 > build/test/loud_cal.csv"
  python -c "
import csv, json
rows = list(csv.DictReader(open('build/test/loud_cal.csv')))
json.dump({r['name']: float(r['trim']) for r in rows if r['name'] != 'Init'}, open('tools/presetgen/trims.json', 'w'), indent=0)
"
done
python tools/presetgen/build_all.py
