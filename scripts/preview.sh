#!/usr/bin/env bash
# rebuild contract + render pages with Schwung's previewer (needs Docker)
set -e
cd "$(dirname "$0")/.."
mkdir -p build/prev
export PATH="$PATH:/c/Program Files/Docker/Docker/resources/bin"
MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD:/build" -w /build mean-module-builder bash -c "gcc -std=gnu11 -O1 -Isrc/host -fPIC -shared src/dsp/mean_module_plugin.c -o build/prev/dsp.so -lm && gcc -std=gnu11 tests/dump_contract.c -o build/prev/dump -ldl && build/prev/dump build/prev/dsp.so > build/prev/entry.json"
python -c "
import json
e=json.load(open('build/prev/entry.json',encoding='utf-8'))
f=json.load(open('ref/schwung/tests/fixtures/module-contracts.json',encoding='utf-8'))
f['modules']=[m for m in f['modules'] if m['id']!='meanmodule']+[e]
json.dump(f,open('build/prev/fixture.json','w',encoding='utf-8'))"
rm -rf build/prev/png && mkdir -p build/prev/png
MSYS_NO_PATHCONV=1 docker run --rm -v "$PWD/ref/schwung:/s:ro" -v "$PWD/build/prev/fixture.json:/s/tests/fixtures/module-contracts.json:ro" -v "$PWD/build/prev/png:/out" -w /s node:20-slim node tools/param-pages/preview.mjs meanmodule --layout movy --all --png /out --scale 3 "$@" | grep -E "page|error" | head -40
