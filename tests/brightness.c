/* Library brightness: plays one held note (C3) on every factory preset and measures the spectral centroid of the sound.
 * Usage: brightness dsp.so measure                      CSV: name,cut,centroid_hz
 *        brightness dsp.so calibrate targets.csv        CSV: name,cut,before_hz,after_hz,target_hz  (raises Cutoff, never lowers it, until the centroid reaches the
 *                                                       preset's target; targets.csv lines are "name,target_hz")
 * The cutoff knob is exponential (20 Hz * 2^(9.8 x)), so a knob value that "looks" mid-way is often far too dark. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

#define SR 44100.0
#define BLK 128
#define HOLD_BLOCKS 420      /* ~1.2 s */
#define NFFT 4096

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double a = -2 * M_PI / len, wr = cos(a), wi = sin(a);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                double ur = re[i + k], ui = im[i + k];
                double vr = re[i + k + len / 2] * cr - im[i + k + len / 2] * ci, vi = re[i + k + len / 2] * ci + im[i + k + len / 2] * cr;
                re[i + k] = ur + vr; im[i + k] = ui + vi; re[i + k + len / 2] = ur - vr; im[i + k + len / 2] = ui - vi;
                double t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
            }
        }
    }
}

static plugin_api_v2_t *api;
static void midi(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static void setp(void *i, const char *k, const char *v) { api->set_param(i, k, v); }
static int16_t buf[HOLD_BLOCKS * BLK * 2];

/* power-averaged spectral centroid (40 Hz .. 10 kHz) of the held note */
static double centroid(int preset, double cut, double *cut_out) {
    char v[64];
    void *inst = api->create_instance(".", NULL);
    snprintf(v, sizeof v, "%d", preset); setp(inst, "preset", v);
    if (cut >= 0) { snprintf(v, sizeof v, "%.4f", cut); setp(inst, "cutoff", v); }
    api->get_param(inst, "cutoff", v, sizeof v); *cut_out = atof(v);
    midi(inst, 0x90, 48, 100);
    for (int b = 0; b < HOLD_BLOCKS; b++) api->render_block(inst, buf + b * BLK * 2, BLK);
    midi(inst, 0x80, 48, 0); midi(inst, 0xB0, 123, 0);
    api->destroy_instance(inst);
    static double re[NFFT], im[NFFT], pw[NFFT / 2];
    memset(pw, 0, sizeof pw);
    int frames = HOLD_BLOCKS * BLK;
    for (int s = 0; s + NFFT <= frames; s += NFFT / 2) {
        for (int i = 0; i < NFFT; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / NFFT); re[i] = buf[2 * (s + i)] / 32768.0 * w; im[i] = 0; }
        fft(re, im, NFFT);
        for (int k = 1; k < NFFT / 2; k++) pw[k] += re[k] * re[k] + im[k] * im[k];
    }
    double num = 0, den = 0;
    for (int k = 1; k < NFFT / 2; k++) { double f = k * SR / NFFT; if (f < 40 || f > 10000) continue; num += f * pw[k]; den += pw[k]; }
    return den > 1e-12 ? num / den : 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s dsp.so measure | calibrate targets.csv\n", argv[0]); return 2; }
    int calibrate = !strcmp(argv[2], "calibrate");
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    api = init(&host);
    static char tname[512][96]; static double ttarget[512]; int nt = 0;
    if (calibrate && argc > 3) {
        FILE *f = fopen(argv[3], "r"); char ln[256];
        while (f && fgets(ln, sizeof ln, f) && nt < 512) { char *c = strrchr(ln, ','); if (!c) continue; *c = 0; ttarget[nt] = atof(c + 1); snprintf(tname[nt], sizeof tname[nt], "%s", ln); nt++; }
        if (f) fclose(f);
    }
    char v[64], nm[96];
    { void *t = api->create_instance(".", NULL); api->get_param(t, "preset_count", v, sizeof v); api->destroy_instance(t); }
    int cnt = atoi(v);
    printf(calibrate ? "name,cut,before_hz,after_hz,target_hz\n" : "name,cut,centroid_hz\n");
    for (int i = 0; i < cnt; i++) {
        { void *t = api->create_instance(".", NULL); snprintf(v, sizeof v, "%d", i); setp(t, "preset", v); api->get_param(t, "preset_full", nm, sizeof nm); api->destroy_instance(t); }
        double cut, c0 = centroid(i, -1, &cut), c = c0, target = 0;
        for (int k = 0; k < nt; k++) if (!strcmp(tname[k], nm)) target = ttarget[k];
        if (calibrate && target > c0 && cut < 0.78) {
            double lo = cut, hi = 0.78;   /* never wide open: the filter is the character */                 /* smallest cutoff that reaches the target (the centroid rises with the cutoff) */
            double top; double ch = centroid(i, hi, &top);
            if (ch < target) { cut = hi; c = ch; }
            else {
                for (int it = 0; it < 9; it++) { double mid = 0.5 * (lo + hi), mc, cm = centroid(i, mid, &mc); if (cm >= target) hi = mid; else lo = mid; }
                cut = hi; c = centroid(i, hi, &top);
            }
        }
        if (calibrate) printf("\"%s\",%.4f,%.0f,%.0f,%.0f\n", nm, cut, c0, c, target); else printf("\"%s\",%.4f,%.0f\n", nm, cut, c0);
    }
    return 0;
}
