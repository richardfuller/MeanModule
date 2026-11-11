#!/usr/bin/env bash
cd "$(dirname "$0")/.."
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
export MSYS_NO_PATHCONV=1
docker run --rm --cpus=2 --memory=3g -v "$PWD:/build" -w /build mean-module-builder bash -c "gcc -std=gnu11 -O2 -Wall -Isrc/host tests/health.c -o build/test/health -ldl -lm" || exit 1
rm -f build/test/health_chunk_*.csv
NP=$(grep -c '^    PRE(' src/dsp/mean_module_plugin.c)
for s in $(seq 0 40 $((NP - 1))); do
  ( docker run --rm --cpus=2 --memory=3g -v "$PWD:/build" -w /build mean-module-builder bash -c "build/test/health build/test/dsp.so $s $((s + 40)) > build/test/health_chunk_$s.csv" 2> build/test/health_chunk_$s.err; true ) &
  while [ "$(jobs -rp | wc -l)" -ge 1 ]; do sleep 2; done
done
wait
python - <<'PY'
import csv, glob
rows = []
hdr = None
for f in sorted(glob.glob('build/test/health_chunk_*.csv'), key=lambda p: int(p.split('_')[-1].split('.')[0])):
    r = list(csv.reader(open(f)))
    if r:
        if hdr is None: hdr = r[0]
        rows += [x for x in r if x and x[0] != 'name']
csv.writer(open('out/health.csv', 'w', newline='')).writerows([hdr] + rows)
print(len(rows), 'presets')
PY
python scripts/health_diff.py
