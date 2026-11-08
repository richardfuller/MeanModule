/* Offline A/B measurements for audio-path changes: loads one dsp.so, renders fixed scenarios and prints numbers.
 *   audio_eval path/to/dsp.so [outfile.raw]
 * Run it on a baseline build and a candidate build and compare. Not part of the regular test run.
 * Metrics: aliasing share of a high note, idle noise floor, self-oscillation start, cutoff-step "zipper" energy,
 * cross-mod output (raw dump for diffing), noise spectrum, render cost. */
#include <dlfcn.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../src/host/plugin_api_v1.h"

static const host_api_v1_t host;
static plugin_api_v2_t *api;
static void *inst;
#define FR 128
static void setp(const char *k, const char *v) { api->set_param(inst, k, v); }
static void setf(const char *k, double v) { char b[32]; snprintf(b, sizeof b, "%.5f", v); setp(k, b); }
static void midi(uint8_t a, uint8_t b, uint8_t c) { uint8_t m[3] = {a, b, c}; api->on_midi(inst, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static int16_t *render(int blocks) {
    int16_t *o = malloc((size_t)blocks * FR * 4);
    for (int b = 0; b < blocks; b++) api->render_block(inst, o + (size_t)b * FR * 2, FR);
    return o;
}
static void fresh(void) {
    if (inst) api->destroy_instance(inst);
    inst = api->create_instance(".", "{}");
    setp("preset", "0");
}
static void reset_bank(void) { for (int k = 1; k <= 4; k++) { char b[16]; snprintf(b, sizeof b, "fx%d_type", k); setp(b, "0"); } }

/* radix-2 FFT of a real block (Hann), returns power spectrum in out[0..n/2] */
static void fft_power(const int16_t *x, int n, double *pw) {
    double *re = malloc(sizeof(double) * n), *im = calloc(n, sizeof(double));
    for (int i = 0; i < n; i++) re[i] = x[2 * i] / 32768.0 * (0.5 - 0.5 * cos(2 * M_PI * i / (n - 1)));
    for (int i = 1, j = 0; i < n; i++) { int bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; } }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double nr = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = nr;
            }
        }
    }
    for (int i = 0; i <= n / 2; i++) pw[i] = re[i] * re[i] + im[i] * im[i];
    free(re); free(im);
}
static double rms(const int16_t *x, int a, int n) { double s = 0; for (int i = a; i < a + n; i++) { double v = x[2 * i] / 32768.0; s += v * v; } return sqrt(s / n); }
static double db(double v) { return 20 * log10(v > 1e-12 ? v : 1e-12); }

/* share of power that is NOT on a harmonic of f0, in dB below the harmonic power */
static double alias_db(const int16_t *x, double f0, int n) {
    double *pw = malloc(sizeof(double) * (n / 2 + 1)); fft_power(x, n, pw);
    double harm = 0, other = 0; double binhz = 44100.0 / n;
    for (int i = 3; i <= n / 2; i++) {
        double f = i * binhz; double k = floor(f / f0 + 0.5); int on = k >= 1 && fabs(f - k * f0) < 3 * binhz;
        if (on) harm += pw[i]; else other += pw[i];
    }
    free(pw); return 10 * log10(other / harm);
}

int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW); if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    api = ((plugin_api_v2_t *(*)(const host_api_v1_t *))dlsym(h, "move_plugin_init_v2"))(&host);
    int16_t *o;

    /* A. aliasing of a high note through the drive: non-harmonic share, dB (lower = cleaner) */
    const int notes[3] = {84, 96, 100}; const char *driv[3] = {"0.3", "0.8", "1.0"};
    for (int c = 0; c < 3; c++) {
        fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_level", "0"); setp("cutoff", "1"); setp("resonance", "0"); setp("drive", driv[c]); setp("drift", "0");
        setp("vibrato_depth", "0"); setp("env2_s", "1"); setp("volume", "0.9"); setf("grit", c == 2 ? 0.6 : 0.0); setp("filter_drive", "0.8");
        midi(0x90, notes[c], 110); free(render(60)); o = render(200);
        double f0 = 440.0 * pow(2.0, (notes[c] - 69) / 12.0);
        printf("alias note %d drive %s: %.1f dB  (peak %.3f)\n", notes[c], driv[c], alias_db(o + 2 * FR * 20 * 0 + 2 * 4096 * 0, f0, 16384), 0.0);
        free(o);
    }
    /* B. idle noise floor with the gate open, nothing playing in the oscillators */
    for (int r = 0; r < 3; r++) {
        const char *rv[3] = {"0", "0.3", "0.5"};
        fresh(); reset_bank(); setp("osc1_level", "0"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("resonance", rv[r]); setp("env2_s", "1"); setp("drift", "0");
        midi(0x90, 48, 100); free(render(60)); o = render(200);
        printf("floor res %s: %.1f dBFS\n", rv[r], db(rms(o, 0, 200 * FR))); free(o);
    }
    /* C. self-oscillation from nothing: blocks until rms > 0.01, and the settled level */
    for (int r = 0; r < 2; r++) {
        const char *rv[2] = {"0.7", "1.0"};
        fresh(); reset_bank(); setp("osc1_level", "0"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("resonance", rv[r]); setp("cutoff", "0.6"); setp("env2_s", "1"); setp("drift", "0"); setp("filter_drive", "0.5");
        midi(0x90, 48, 100); int start = -1; double lvl = 0;
        for (int b = 0; b < 1500; b++) { int16_t *ob = render(1); double rr = rms(ob, 0, FR); free(ob); if (start < 0 && rr > 0.01) start = b; lvl = rr; }
        printf("selfosc res %s: starts at block %d, settled rms %.4f\n", rv[r], start, lvl);
    }
    /* D. zipper: cutoff stepped like a knob (0.02 every 16 ms) on a resonant patch; energy of the first difference vs a smooth reference sweep */
    for (int mode = 0; mode < 2; mode++) {
        fresh(); reset_bank(); setp("osc1_wave", "2"); setp("osc2_level", "0"); setp("resonance", "0.8"); setp("drive", "0.2"); setp("drift", "0"); setp("env2_s", "1"); setp("flt_fm1", "0");
        setp("filter_drive", "0.5"); setf("cutoff", 0.3); midi(0x90, 48, 100); free(render(40));
        double acc = 0, pk = 0; long cnt = 0; int16_t p1 = 0, p2 = 0;
        for (int step = 0; step < 20; step++) {
            for (int sub = 0; sub < (mode ? 16 : 1); sub++) {
                setf("cutoff", 0.3 + (step + (mode ? (sub + 1) / 16.0 : 1.0)) * 0.02);
                int nb = mode ? 1 : 6;   /* 6 blocks = 17 ms between steps; the smooth reference moves every block */
                o = render(nb);
                for (int i = 0; i < nb * FR; i++) {
                    double d2 = (o[2 * i] - 2.0 * p1 + p2) / 32768.0; p2 = p1; p1 = o[2 * i];   /* second difference: emphasises clicks and zipper edges */
                    acc += d2 * d2; cnt++; if (fabs(d2) > pk) pk = fabs(d2);
                }
                free(o);
            }
        }
        printf("zipper %s: second-difference rms %.5f, peak %.5f\n", mode ? "smooth reference" : "knob steps", sqrt(acc / cnt), pk);
    }
    /* E. cross-mod output, dumped raw for diffing (same start state, so two builds are comparable) */
    fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_wave", "1"); setp("osc2_level", "0.5"); setp("xmod_2to1", "1"); setp("osc2_coarse", "7"); setp("drift", "0");
    setp("resonance", "0.2"); setp("cutoff", "0.9"); setp("env2_s", "1"); midi(0x90, 60, 100); o = render(100);
    printf("xmod rms %.5f\n", rms(o, 0, 100 * FR));
    if (argc > 2) { FILE *f = fopen(argv[2], "wb"); fwrite(o, 4, (size_t)100 * FR, f); fclose(f); }
    free(o);
    /* F. noise: share of power above 6 kHz, and level */
    fresh(); reset_bank(); setp("osc1_level", "0"); setp("osc2_level", "0"); setp("noise_level", "1"); setp("cutoff", "1"); setp("resonance", "0"); setp("filter_drive", "0.3"); setp("drive", "0"); setp("env2_s", "1");
    for (int c = 0; c < 3; c++) {
        setf("noise_color", c * 0.5); midi(0x90, 48, 100); free(render(60)); o = render(200);
        double *pw = malloc(sizeof(double) * 8193); fft_power(o + 2 * 4096, 16384, pw); double hi = 0, all = 0; for (int i = 3; i <= 8192; i++) { all += pw[i]; if (i * 44100.0 / 16384 > 6000) hi += pw[i]; }
        printf("noise color %.1f: rms %.4f, power above 6 kHz %.1f%%\n", c * 0.5, rms(o, 0, 200 * FR), 100 * hi / all); free(pw); free(o); midi(0x80, 48, 0);
    }
    /* G. render cost on a heavy patch, microseconds per block */
    fresh(); reset_bank(); setp("osc2_wave", "1"); setp("osc2_level", "0.8"); setp("resonance", "0.7"); setp("drive", "0.8"); setp("filter_drive", "0.9"); setp("env2_s", "1"); setp("xmod_2to1", "0.5");
    midi(0x90, 48, 100); free(render(50));
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); o = render(3000); clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("cost: %.1f us/block\n", ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 3000.0 / 1000.0); free(o);
    return 0;
}
