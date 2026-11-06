#!/usr/bin/env bash
# Desktop host tests (x86_64, in Docker): ASan/UBSan run, then RT-guard run.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f /.dockerenv ]; then
  docker build -q -t mean-module-builder -f "$ROOT/scripts/Dockerfile" "$ROOT/scripts" >/dev/null
  MSYS_NO_PATHCONV=1 docker run --rm --security-opt seccomp=unconfined -v "$ROOT:/build" -w /build mean-module-builder ./scripts/test.sh
  exit 0
fi
cd "$ROOT"; mkdir -p build/test
CF="-std=gnu11 -g -O1 -Wall -Wextra -Isrc/host"
# ASan and UBSan run separately: the combined build crashes during ASan init under Docker Desktop/WSL2.
for san in address undefined; do
  gcc $CF -fsanitize=$san -fno-omit-frame-pointer -fPIC -shared src/dsp/mean_module_plugin.c -o build/test/dsp_$san.so -lm
  gcc $CF -fsanitize=$san tests/host_test.c -o build/test/host_$san -ldl -lm
  echo "== $san sanitizer run =="
  ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 setarch "$(uname -m)" -R build/test/host_$san build/test/dsp_$san.so build/test
done
gcc $CF -O2 -fPIC -shared src/dsp/mean_module_plugin.c -o build/test/dsp.so -lm
gcc $CF -O2 tests/host_test.c -o build/test/host -ldl -lm -rdynamic
gcc -O2 -fPIC -shared tests/rt_guard.c -o build/test/rt_guard.so -ldl
echo "== RT guard run =="
LD_PRELOAD=$PWD/build/test/rt_guard.so build/test/host build/test/dsp.so build/test
