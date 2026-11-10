#!/usr/bin/env bash
# Regenerate the PRESETS table from the bank_*.py files, then fit Cutoff (brightness) and Level Trim (loudness): tools/presetgen/calib.sh [loudness passes]   (needs Docker)
cd "$(dirname "$0")/../.."
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
d() { docker run --rm -v "$PWD:/build" -w /build mean-module-builder bash -c "$1"; }
export MSYS_NO_PATHCONV=1
BUILD="gcc -std=gnu11 -O2 -fPIC -shared -Isrc/host src/dsp/mean_module_plugin.c -o build/test/dsp.so -lm -ldl -lpthread"
rm -f tools/presetgen/cuts.json
python tools/presetgen/build_all.py >/dev/null
python tools/presetgen/targets.py > build/test/targets.csv
d "$BUILD && gcc -std=gnu11 -O2 -Isrc/host tests/brightness.c -o build/test/brightness -ldl -lm && build/test/brightness build/test/dsp.so calibrate build/test/targets.csv > build/test/bright_cal.csv"
python -c "
import csv, json
rows = list(csv.DictReader(open('build/test/bright_cal.csv')))
json.dump({r['name']: float(r['cut']) for r in rows if float(r['target_hz']) > float(r['before_hz'])}, open('tools/presetgen/cuts.json', 'w'), indent=0)
print('brightness: raised the cutoff of', sum(1 for r in rows if float(r['target_hz']) > float(r['before_hz'])), 'presets')
"
for i in $(seq 1 ${1:-3}); do
  python tools/presetgen/build_all.py >/dev/null
  d "$BUILD && gcc -std=gnu11 -O2 -Isrc/host tests/loudness.c -o build/test/loudness -ldl -lm && build/test/loudness build/test/dsp.so calibrate -19 > build/test/loud_cal.csv"
  python -c "
import csv, json
rows = list(csv.DictReader(open('build/test/loud_cal.csv')))
json.dump({r['name']: float(r['trim']) for r in rows if r['name'] != 'Init'}, open('tools/presetgen/trims.json', 'w'), indent=0)
"
done
python tools/presetgen/build_all.py
