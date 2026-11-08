#!/usr/bin/env bash
# Build Mean Module dsp.so for Move (aarch64). Uses Docker unless CROSS_PREFIX is set / already in container.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "$CROSS_PREFIX" ] && [ ! -f /.dockerenv ]; then
  docker image inspect mean-module-builder >/dev/null 2>&1 || docker build -t mean-module-builder -f "$ROOT/scripts/Dockerfile" "$ROOT/scripts"
  MSYS_NO_PATHCONV=1 docker run --rm -v "$ROOT:/build" -w /build mean-module-builder ./scripts/build.sh
  exit 0
fi
CROSS_PREFIX="${CROSS_PREFIX:-aarch64-linux-gnu-}"
cd "$ROOT"
mkdir -p build dist/meanmodule
${CROSS_PREFIX}gcc -O3 -fPIC -shared -std=c11 -Wall -Wextra -Isrc/host \
  src/dsp/mean_module_plugin.c -o build/dsp.so -lm -ldl -lpthread
cat src/module.json > dist/meanmodule/module.json
cat build/dsp.so > dist/meanmodule/dsp.so
[ -f src/help.json ] && cat src/help.json > dist/meanmodule/help.json
cp LICENSE THIRD_PARTY_LICENSES dist/meanmodule/
chmod +x dist/meanmodule/dsp.so
# Explicit modes + root ownership: Schwung Manager extracts this on the Move and fails on read-only dirs.
find dist/meanmodule -type d -exec chmod 755 {} +
find dist/meanmodule -type f -exec chmod 644 {} +
chmod 755 dist/meanmodule/dsp.so
(cd dist && rm -f meanmodule-module.tar.gz && tar --owner=0 --group=0 --numeric-owner -czf meanmodule-module.tar.gz meanmodule/)
file build/dsp.so
