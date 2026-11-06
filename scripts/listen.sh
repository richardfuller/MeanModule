#!/usr/bin/env bash
# Render the listening suite to out/listen/*.wav (runs in Docker).
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f /.dockerenv ]; then
  docker build -q -t mean-module-builder -f "$ROOT/scripts/Dockerfile" "$ROOT/scripts" >/dev/null
  MSYS_NO_PATHCONV=1 docker run --rm -v "$ROOT:/build" -w /build mean-module-builder ./scripts/listen.sh
  exit 0
fi
cd "$ROOT"; mkdir -p build/test out/listen
gcc -std=gnu11 -O2 -Isrc/host -fPIC -shared src/dsp/mean_module_plugin.c -o build/test/dsp.so -lm
gcc -std=gnu11 -O2 -Isrc/host tests/render.c -o build/test/render -ldl -lm
R="build/test/render build/test/dsp.so"
O=out/listen
# name secs hold note params...
$R $O/01_saw_c2.wav        3 2 36
$R $O/02_saw_c4.wav        3 2 60
$R $O/03_saw_c7_alias.wav  3 2 96
$R $O/04_tri_c3.wav        3 2 48 osc1_wave=2
$R $O/05_pulse_width_mod.wav     4 3 48 osc1_wave=4 shape_mod=1 lfo_rate=0.4
$R $O/06_saw_pulse_detune.wav 4 3 48 osc1_wave=5 osc2_wave=1 osc2_fine=12 osc2_level=0.8
$R $O/07_shape_sweep_saw.wav 4 3 48 osc1_shape=0.1 shape_mod=1 lfo_rate=0.3
$R $O/08_xmod_bell.wav     3 2 60 osc1_wave=2 osc2_wave=2 osc2_coarse=7 xmod_2to1=0.5 env2_d=0.6 env2_s=0
$R $O/09_env_fm_pluck.wav  2 1 48 osc1_fm1=0.5 env1_d=0.3 env2_d=0.35 env2_s=0
$R $O/10_vibrato_glide.wav 3 2.5 55 vibrato_depth=0.6 glide=0.4
$R $O/11_drone_noise.wav   4 4 40 drone=1 noise_level=0.6 osc1_level=0.3
$R $O/12_x_mod_extreme_alias.wav 3 2 84 osc1_wave=7 osc2_wave=7 xmod_2to1=1 osc1_fm2=1 osc2_fm1=1
$R $O/13_selfosc_noise_kick.wav 3 2.8 48 osc1_level=0 osc2_level=0 resonance=1 cutoff=0.5 key_track=0 flt_fm1=0 env2_s=1
$R $O/14_lp_sweep_reso.wav 4 3.5 36 osc1_wave=7 resonance=0.55 flt_fm1=1 env1_d=0.7 env1_s=0 cutoff=0.3
$R $O/15_hot_input_squeeze.wav 4 3.5 36 osc1_wave=7 osc2_wave=7 osc2_level=0.8 resonance=0.55 filter_drive=1 flt_fm1=1 env1_d=0.7 env1_s=0 cutoff=0.3
$R $O/16_modes_bp.wav 3 2.5 48 filter_mode=1 resonance=0.6 cutoff=0.55
$R $O/17_modes_hp.wav 3 2.5 48 filter_mode=2 resonance=0.4 cutoff=0.55
$R $O/18_modes_notch.wav 3 2.5 48 filter_mode=3 resonance=0.4 cutoff=0.55
$R $O/19_drive_max.wav 3 2.5 36 drive=1 osc1_wave=5 osc2_wave=1
$R $O/m1_noise_lp_slope.wav 6 6 48 osc1_level=0 osc2_level=0 noise_level=1 drone=1 resonance=0 cutoff=0.5 key_track=0 flt_fm1=0 filter_drive=0 drive=0
$R $O/m2_selfosc_c0.4.wav 3 3 48 osc1_level=0 osc2_level=0 resonance=1 cutoff=0.4 key_track=0 flt_fm1=0 drone=1 drive=0
$R $O/m3_selfosc_c0.6.wav 3 3 48 osc1_level=0 osc2_level=0 resonance=1 cutoff=0.6 key_track=0 flt_fm1=0 drone=1 drive=0
mkdir -p out/presets
for i in $(seq 0 60); do
  nm=$($R out/presets/tmp.wav 4 2.5 48 preset=$i) || break
  { [ -z "$nm" ] || { [ "$i" -gt 0 ] && [ "$nm" = "Init" ]; }; } && break
  safe=$(printf "%s" "$nm" | tr -c 'A-Za-z0-9\n' '_')
  mv out/presets/tmp.wav "out/presets/$(printf %02d $i)_$safe.wav"
done
ls out/presets
