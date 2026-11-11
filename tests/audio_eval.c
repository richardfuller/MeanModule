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
/* frequency from rising zero crossings (interpolated), channel 0, 44.1 kHz */
static double zc_freq(const int16_t *x, int a, int n) {
    double first = -1, last = -1; int cnt = 0;
    for (int i = a + 1; i < a + n; i++) {
        double p = x[2 * (i - 1)], q = x[2 * i];
        if (p < 0 && q >= 0) { double t = i - 1 + (-p) / (q - p); if (first < 0) first = t; last = t; cnt++; }
    }
    return cnt > 1 ? (cnt - 1) * 44100.0 / (last - first) : 0.0;
}
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
    /* H. Aux Feedback (output back into the filter input) and oscillator levels past 1: rms, peak and crest of a held note, bounded and finite.
     *    Resonance squeeze for a hot input: output rms at resonance 0.8 against resonance 0, same patch. */
    {
        const double fbs[4] = {0.0, 0.3, 0.6, 1.0};
        for (int c = 0; c < 4; c++) {
            fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("env2_s", "1");
            setp("cutoff", "0.55"); setp("resonance", "0.5"); setp("drive", "0.3"); setf("aux_fb", fbs[c]); setf("osc1_level", 0.8);
            midi(0x90, 48, 100); free(render(80)); o = render(400);
            double pk = 0; int bad = 0; for (int k = 0; k < 400 * FR; k++) { double v = fabs(o[2 * k] / 32768.0); if (v > pk) pk = v; }
            for (int k = 0; k < 400 * FR * 2; k++) if (o[k] == 32767 || o[k] == -32768) bad++;
            printf("auxfb %.1f: rms %.4f dBFS %.1f, peak %.3f, full-scale samples %d\n", fbs[c], rms(o, 0, 400 * FR), db(rms(o, 0, 400 * FR)), pk, bad); free(o);
        }
        const double lvs[3] = {0.5, 1.0, 1.5};
        for (int rs = 0; rs < 2; rs++) for (int lv = 0; lv < 3; lv++) {
            fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drive", "0"); setp("drift", "0"); setp("flt_fm1", "0");
            setp("env2_s", "1"); setp("cutoff", "0.55"); setf("resonance", rs ? 0.8 : 0.0); setp("filter_drive", "0.5"); setf("osc1_level", lvs[lv]);
            midi(0x90, 48, 100); free(render(80)); o = render(200);
            printf("osclevel %.1f res %.1f: rms %.4f dBFS %.1f\n", lvs[lv], rs ? 0.8 : 0.0, rms(o, 0, 200 * FR), db(rms(o, 0, 200 * FR))); free(o);
        }
    }
    /* I. AM1 (am_env) and AM2 (am_lfo): a plucked patch (sustain 0) measured 150 ms and 600 ms after note-on, a 20 ms-window level swing under the LFO,
     *    and the largest sample step in the release (a click shows as a big step). */
    {
        const double a1[3] = {1.0, 0.5, 0.0};
        for (int c = 0; c < 3; c++) for (int rel_test = 0; rel_test < 2; rel_test++) {
            fresh(); reset_bank(); setp("osc1_wave", "2"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("cutoff", "0.9");
            setp("env2_s", "0"); setp("env2_d", "0.75"); setp("env2_r", "0.45"); setp("drive", "0"); setf("am_env", a1[c]);
            midi(0x90, 60, 100);
            if (!rel_test) { o = render(500); printf("am1 %.1f: level at 150 ms %.1f dB, at 600 ms %.1f dB, at 1.2 s %.1f dB\n", a1[c], db(rms(o, 150 * 44, 20 * 44)), db(rms(o, 600 * 44, 20 * 44)), db(rms(o, 1200 * 44, 20 * 44))); free(o); }
            else { free(render(100)); midi(0x80, 60, 0); int16_t *rel = render(400); double step = 0; for (int k = 1; k < 400 * FR; k++) { double d = fabs(rel[2 * k] / 32768.0 - rel[2 * k - 2] / 32768.0); if (d > step) step = d; }
                   printf("am1 %.1f: max sample step in the release %.4f\n", a1[c], step); free(rel); }
        }
        for (int c = 0; c < 2; c++) {
            fresh(); reset_bank(); setp("osc1_wave", "2"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("cutoff", "0.9");
            setp("env2_s", "1"); setp("lfo_rate", "0.6"); setp("lfo_wave", "0"); setp("drive", "0"); setf("am_lfo", c ? 0.8 : 0.0);
            midi(0x90, 60, 100); free(render(50)); o = render(1000); double lo = 9, hi = 0;
            for (int w = 0; w + 8 <= 1000; w += 8) { double r = rms(o, w * FR, 8 * FR); if (r < lo) lo = r; if (r > hi) hi = r; }
            printf("am2 %.1f: level swing %.1f dB (min %.4f, max %.4f)\n", c ? 0.8 : 0.0, db(hi) - db(lo), lo, hi); free(o);
        }
    }
    /* J. v0.14 analog-character options. Each prints a number that should move when the option is on and not when it is off. */
    {
        #define ZC(x, a, n) zc_freq((x), (a), (n))
        /* Shape Flip: level at shape 0.25 / 0.5 / 0.75 */
        for (int fl = 0; fl < 2; fl++) {
            double r[3]; const double sh[3] = {0.25, 0.5, 0.75};
            for (int k = 0; k < 3; k++) {
                fresh(); reset_bank(); setp("osc1_wave", "4"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "1"); setp("resonance", "0");
                setf("osc1_shape", sh[k]); setf("shape_flip", fl); midi(0x90, 48, 100); free(render(60)); o = render(100); r[k] = rms(o, 0, 100 * FR); free(o);
            }
            printf("flip %s: rms at shape .25 %.4f, .5 %.4f, .75 %.4f\n", fl ? "on" : "off", r[0], r[1], r[2]);
        }
        /* Saw Octave Up: pitch of a lone saw, note 48 */
        for (int so = 0; so < 2; so++) {
            fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "1"); setp("resonance", "0"); setp("drive", "0");
            setf("saw_oct", so); midi(0x90, 48, 100); free(render(60)); o = render(300);
            printf("sawoct %s: %.1f Hz (note 48 = 130.8 Hz)\n", so ? "on" : "off", ZC(o, 0, 300 * FR)); free(o);
        }
        /* Hard Sync: osc 2 alone is heard; its period should follow osc 1 when synced (autocorrelation at osc 1's period) */
        for (int sy = 0; sy < 3; sy++) {
            fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc2_wave", "1"); setp("osc1_level", sy == 2 ? "1" : "0"); setp("osc2_level", sy == 2 ? "0" : "1"); setp("osc2_coarse", "7"); setp("osc2_fine", "0"); setp("noise_level", "0"); setp("drift", "0");
            setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "1"); setp("resonance", "0"); setp("drive", "0"); setp("osc_sync", sy == 0 ? "0" : (sy == 1 ? "1" : "2"));
            midi(0x90, 60, 100); free(render(60)); o = render(200); int n = 200 * FR; double best = 0, e0 = 0;
            for (int i = 0; i < n - 400; i++) e0 += (o[2 * i] / 32768.0) * (o[2 * i] / 32768.0);
            for (int lag = (sy == 2 ? 110 : 166); lag <= (sy == 2 ? 115 : 171); lag++) { double c = 0; for (int i = 0; i < n - 400; i++) c += (o[2 * i] / 32768.0) * (o[2 * (i + lag)] / 32768.0); if (c / e0 > best) best = c / e0; }
            printf("sync %s: autocorrelation at osc 1's period %.3f\n", sy == 0 ? "off (osc 2 heard)" : (sy == 1 ? "1 to 2 (osc 2 heard)" : "2 to 1 (osc 1 heard, osc 2 the master)"), best); free(o);
        }
        /* X-Mod Linear vs Exponential: level and the largest sample */
        for (int xm = 0; xm < 2; xm++) {
            fresh(); reset_bank(); setp("osc1_wave", "2"); setp("osc2_wave", "2"); setp("osc2_level", "0"); setp("osc2_coarse", "7"); setp("xmod_2to1", "0.5"); setf("xmod_mode", xm); setp("noise_level", "0"); setp("drift", "0");
            setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "1"); setp("resonance", "0"); midi(0x90, 48, 100); free(render(60)); o = render(200); double pk = 0;
            for (int i = 0; i < 200 * FR; i++) { double v = fabs(o[2 * i] / 32768.0); if (v > pk) pk = v; }
            printf("xmod %s: rms %.4f, peak %.3f\n", xm ? "linear" : "exponential", rms(o, 0, 200 * FR), pk); free(o);
        }
        /* Resonance Onset: settled rms with no input at resonance 0.3 / 0.45 / 0.6 */
        for (int rc = 0; rc < 2; rc++) {
            const char *rv[3] = {"0.3", "0.45", "0.6"}; double r[3];
            for (int k = 0; k < 3; k++) {
                fresh(); reset_bank(); setp("osc1_level", "0"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("resonance", rv[k]); setp("cutoff", "0.6"); setp("env2_s", "1"); setp("drift", "0"); setp("filter_drive", "0.5"); setf("res_curve", rc);
                midi(0x90, 48, 100); free(render(800)); o = render(100); r[k] = rms(o, 0, 100 * FR); free(o);
            }
            printf("res onset %s: self-oscillation rms at res .3 %.4f, .45 %.4f, .6 %.4f\n", rc ? "on" : "off", r[0], r[1], r[2]);
        }
        /* Stretch Tuning: error in cents of notes 36 and 96 against equal temperament */
        for (int st = 0; st < 2; st++) {
            const int nn[2] = {36, 96}; double er[2];
            for (int k = 0; k < 2; k++) {
                fresh(); reset_bank(); setp("osc1_wave", "2"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "1"); setp("resonance", "0");
                setp("osc1_fine", "0"); setp("tune", "0"); setf("stretch", st ? 10 : 0); midi(0x90, nn[k], 100); free(render(60)); o = render(400);
                double f = ZC(o, 0, 400 * FR), fe = 440.0 * pow(2.0, (nn[k] - 69) / 12.0); er[k] = 1200.0 * log2(f / fe); free(o);
            }
            printf("stretch %s: note 36 %.1f cents, note 96 %.1f cents\n", st ? "+10 c/oct" : "0", er[0], er[1]);
        }
        /* Integrator Saturation: level and brightness (share of first-difference energy) of a hot saw */
        for (int is = 0; is < 2; is++) {
            fresh(); reset_bank(); setp("osc1_wave", "1"); setp("osc1_level", "1.2"); setp("osc2_level", "0"); setp("noise_level", "0"); setp("drift", "0"); setp("flt_fm1", "0"); setp("env2_s", "1"); setp("cutoff", "0.6"); setp("resonance", "0.4"); setp("drive", "0");
            setf("integ_sat", is); midi(0x90, 48, 100); free(render(60)); o = render(200); double d = 0, e = 0;
            for (int i = 1; i < 200 * FR; i++) { double a = o[2 * i] / 32768.0, b = o[2 * i - 2] / 32768.0; d += (a - b) * (a - b); e += a * a; }
            printf("integsat %s: rms %.4f, brightness (diff/level) %.3f\n", is ? "on" : "off", sqrt(e / (200 * FR)), sqrt(d / e)); free(o);
        }
    }
    /* G. render cost on a heavy patch, microseconds per block */
    fresh(); reset_bank(); setp("osc2_wave", "1"); setp("osc2_level", "0.8"); setp("resonance", "0.7"); setp("drive", "0.8"); setp("filter_drive", "0.9"); setp("env2_s", "1"); setp("xmod_2to1", "0.5");
    midi(0x90, 48, 100); free(render(50));
    struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0); o = render(3000); clock_gettime(CLOCK_MONOTONIC, &t1);
    printf("cost: %.1f us/block\n", ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 3000.0 / 1000.0); free(o);
    return 0;
}
