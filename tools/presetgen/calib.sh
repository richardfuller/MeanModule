#!/usr/bin/env bash
# Regenerate the PRESETS table from the bank_*.py files, then fit Cutoff (brightness) and Level Trim (loudness): tools/presetgen/calib.sh [loudness passes]   (needs Docker)
cd "$(dirname "$0")/../.."
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
d() { docker run --rm --cpus=2 --memory=3g -v "$PWD:/build" -w /build mean-module-builder bash -c "$1"; }
export MSYS_NO_PATHCONV=1
BUILD="gcc -std=gnu11 -O2 -fPIC -shared -Isrc/host src/dsp/mean_module_plugin.c -o build/test/dsp.so -lm -ldl -lpthread"
rm -f tools/presetgen/cuts.json tools/presetgen/keylvls.json
[ -n "$FRESH" ] && rm -f build/test/reg_chunk_*.csv   # FRESH=1 discards finished register chunks; without it an interrupted run resumes
python tools/presetgen/build_all.py >/dev/null
python tools/presetgen/targets.py > build/test/targets.csv
d "$BUILD && gcc -std=gnu11 -O2 -Isrc/host tests/brightness.c -o build/test/brightness -ldl -lm && build/test/brightness build/test/dsp.so calibrate build/test/targets.csv > build/test/bright_cal.csv"
python -c "
import csv, json
rows = list(csv.DictReader(open('build/test/bright_cal.csv')))
json.dump({r['name']: float(r['cut']) for r in rows if float(r['target_hz']) > float(r['before_hz'])}, open('tools/presetgen/cuts.json', 'w'), indent=0)
print('brightness: raised the cutoff of', sum(1 for r in rows if float(r['target_hz']) > float(r['before_hz'])), 'presets')
"
# register fit: Key Level per preset so the level is even across its home register (tests/register.c)
python tools/presetgen/build_all.py >/dev/null
d "$BUILD && gcc -std=gnu11 -O2 -Isrc/host tests/register.c -o build/test/register -ldl -lm"
# the register fit is the long step, so it runs in chunks of 20 presets; a chunk that finished is not repeated if the run is interrupted (rm build/test/reg_chunk_* to start over)
NP=$(grep -c '^    PRE(' src/dsp/mean_module_plugin.c)
for s in $(seq 0 20 $((NP - 1))); do
  [ -s build/test/reg_chunk_$s.csv ] && continue
  ( d "build/test/register build/test/dsp.so calibrate $s $((s + 20)) > build/test/reg_chunk_$s.csv.tmp" && mv build/test/reg_chunk_$s.csv.tmp build/test/reg_chunk_$s.csv ) &
  while [ "$(jobs -rp | wc -l)" -ge 1 ]; do sleep 2; done   # one chunk at a time (three at once with 4 CPUs each crashed the PC)
done
wait
python -c "
import csv, glob
rows = []
for f in sorted(glob.glob('build/test/reg_chunk_*.csv'), key=lambda p: int(p.split('_')[-1].split('.')[0])): rows += list(csv.DictReader(open(f)))
w = csv.DictWriter(open('build/test/reg_cal.csv', 'w', newline=''), fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
"
python -c "
import csv, json
rows = list(csv.DictReader(open('build/test/reg_cal.csv')))
json.dump({r['name']: round(float(r['key_lvl']), 3) for r in rows if abs(float(r['key_lvl'])) > 0.001}, open('tools/presetgen/keylvls.json', 'w'), indent=0)
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
