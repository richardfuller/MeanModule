/* Library loudness: plays a held three-note chord on every factory preset and measures how loud it is, A-weighted, over its loudest 150 ms.
 * Usage: loudness dsp.so measure [target_db]          prints one CSV row per preset; exits 1 if any preset is more than 2.5 dB from the target
 *        loudness dsp.so calibrate [target_db] > trims.csv   searches each preset's Level Trim until it hits the target
 * Arp presets are heard as the arpeggio they are (the chord is held), the others as the last held note. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

#define SR 44100.0
#define BLK 128
#define HOLD_BLOCKS 1550     /* ~4.5 s: long enough for the slowest attacks */
#define FLUSH_BLOCKS 500
#define TRIM_DB_RANGE 48.0   /* Level Trim 0..1 spans -24..+24 dB */

typedef struct { double b0, b1, b2, a1, a2, z1, z2; } bq_t;
static void bq_analog(bq_t *q, double b2, double b1, double b0, double a2, double a1, double a0) {   /* bilinear transform */
    double k = 2.0 * SR;
    double nb0 = b2 * k * k + b1 * k + b0, nb1 = -2.0 * b2 * k * k + 2.0 * b0, nb2 = b2 * k * k - b1 * k + b0;
    double da0 = a2 * k * k + a1 * k + a0, da1 = -2.0 * a2 * k * k + 2.0 * a0, da2 = a2 * k * k - a1 * k + a0;
    q->b0 = nb0 / da0; q->b1 = nb1 / da0; q->b2 = nb2 / da0; q->a1 = da1 / da0; q->a2 = da2 / da0; q->z1 = q->z2 = 0;
}
static double bq_run(bq_t *q, double x) {
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y;
    return y;
}
static double bq_mag(const bq_t *q, double f) {
    double w = 2.0 * M_PI * f / SR, c1 = cos(w), s1 = sin(w), c2 = cos(2 * w), s2 = sin(2 * w);
    double nr = q->b0 + q->b1 * c1 + q->b2 * c2, ni = -(q->b1 * s1 + q->b2 * s2), dr = 1 + q->a1 * c1 + q->a2 * c2, di = -(q->a1 * s1 + q->a2 * s2);
    return sqrt((nr * nr + ni * ni) / (dr * dr + di * di));
}
static bq_t A[3]; static double a_gain;
static void a_weight_init(void) {
    double w1 = 2 * M_PI * 20.598997, w2 = 2 * M_PI * 107.65265, w3 = 2 * M_PI * 737.86223, w4 = 2 * M_PI * 12194.217;
    bq_analog(&A[0], 1, 0, 0, 1, 2 * w1, w1 * w1);
    bq_analog(&A[1], 1, 0, 0, 1, w2 + w3, w2 * w3);
    bq_analog(&A[2], 0, 0, w4 * w4, 1, 2 * w4, w4 * w4);
    a_gain = 1.0 / (bq_mag(&A[0], 1000) * bq_mag(&A[1], 1000) * bq_mag(&A[2], 1000));
}
/* A-weighted level (dB re full scale) of the loudest 150 ms of a stereo int16 buffer */
static double loud_db(const int16_t *x, int frames, double *peak_db) {
    enum { HOP = 2205 }; static double hop_ms[4000]; int nh = frames / HOP; if (nh > 4000) nh = 4000;
    double acc = 0, pk = 0; int h = 0, n = 0;
    for (int b = 0; b < 3; b++) { A[b].z1 = A[b].z2 = 0; }
    for (int i = 0; i < frames && h < nh; i++) {
        double v = x[2 * i] / 32768.0; if (fabs(v) > pk) pk = fabs(v);
        double y = bq_run(&A[2], bq_run(&A[1], bq_run(&A[0], v))) * a_gain;
        acc += y * y; if (++n == HOP) { hop_ms[h++] = acc / HOP; acc = 0; n = 0; }
    }
    double best = 0;
    for (int i = 0; i + 3 <= h; i++) { double m = (hop_ms[i] + hop_ms[i + 1] + hop_ms[i + 2]) / 3.0; if (m > best) best = m; }
    if (peak_db) *peak_db = 20.0 * log10(pk > 1e-9 ? pk : 1e-9);
    return 10.0 * log10(best > 1e-12 ? best : 1e-12);
}

static int g_onsets;   /* note starts heard in the last play(): clear rises of the 10 ms envelope */
static void count_onsets(const int16_t *x, int frames) {
    enum { H = 441 }; int n = frames / H; static double e[2000]; if (n > 2000) n = 2000; double mx = 0;
    for (int h = 0; h < n; h++) { double a = 0; for (int i = 0; i < H; i++) { double v = x[2 * (h * H + i)] / 32768.0; a += v * v; } e[h] = sqrt(a / H); if (e[h] > mx) mx = e[h]; }
    g_onsets = 0; int last = -10;
    for (int h = 2; h < n; h++) {   /* an onset is a clear rise over the quieter of the two previous 10 ms steps, at least 30 ms after the last one */
        double base = e[h - 1] < e[h - 2] ? e[h - 1] : e[h - 2];
        if (e[h] > 0.1 * mx && e[h] > 1.35 * base && h - last >= 3) { g_onsets++; last = h; }
    }
}
static plugin_api_v2_t *api;
static void midi(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static void setp(void *i, const char *k, const char *v) { api->set_param(i, k, v); }
static int16_t buf[(HOLD_BLOCKS + FLUSH_BLOCKS) * BLK * 2];

static double play(void *inst, double *peak_db) {
    int chord[3] = {48, 52, 55};
    for (int k = 0; k < 3; k++) midi(inst, 0x90, chord[k], 100);
    for (int b = 0; b < HOLD_BLOCKS; b++) api->render_block(inst, buf + b * BLK * 2, BLK);
    double l = loud_db(buf, HOLD_BLOCKS * BLK, peak_db); count_onsets(buf, HOLD_BLOCKS * BLK);
    for (int k = 0; k < 3; k++) midi(inst, 0x80, chord[k], 0);
    midi(inst, 0xB0, 123, 0);
    for (int b = 0; b < FLUSH_BLOCKS; b++) api->render_block(inst, buf, BLK);
    return l;
}

/* one run of one preset on a fresh instance (LFO phase, random state and envelopes start the same every time); trim < 0 keeps the preset's own */
static double run_preset(int i, double trim, double *peak, double *trim_out, char *name, size_t nn) {
    char v[64];
    void *inst = api->create_instance(".", NULL);
    snprintf(v, sizeof v, "%d", i); setp(inst, "preset", v);
    api->get_param(inst, "preset_full", name, (int)nn);
    if (trim >= 0) { snprintf(v, sizeof v, "%.4f", trim); setp(inst, "trim", v); }
    api->get_param(inst, "trim", v, sizeof v); *trim_out = atof(v);
    double l = play(inst, peak);
    api->destroy_instance(inst);
    return l;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s dsp.so measure|calibrate [target_db]\n", argv[0]); return 2; }
    int calibrate = !strcmp(argv[2], "calibrate");
    double target = argc > 3 ? atof(argv[3]) : -19.0;
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    api = init(&host);
    a_weight_init();
    char v[64], nm[96];
    { void *t = api->create_instance(".", NULL); api->get_param(t, "preset_count", v, sizeof v); api->destroy_instance(t); }
    int cnt = atoi(v), bad = 0;
    printf("idx,name,trim,loud_before_db,loud_after_db,peak_db,onsets\n");
    for (int i = 0; i < cnt; i++) {
        double peak, trim, before = run_preset(i, -1, &peak, &trim, nm, sizeof nm), after = before;
        if (calibrate) {
            for (int it = 0; it < 6 && fabs(after - target) > 0.4; it++) {
                double nt = trim + (target - after) / TRIM_DB_RANGE; if (nt < 0) nt = 0; if (nt > 1) nt = 1;
                if (nt == trim) break;
                double pk2; double l2 = run_preset(i, nt, &pk2, &trim, nm, sizeof nm);
                if (l2 < target && pk2 > -3.0) {   /* transient-heavy: back off to a -3 dB peak instead of chasing the target */
                    nt -= (pk2 + 3.0) / TRIM_DB_RANGE; if (nt < 0) nt = 0;
                    l2 = run_preset(i, nt, &pk2, &trim, nm, sizeof nm); after = l2; peak = pk2; break;
                }
                after = l2; peak = pk2;
            }
        }
        if (fabs(after - target) > 2.5) bad++;
        if (!strncmp(nm, "Arp ", 4) && g_onsets < 8) { bad++; fprintf(stderr, "%s repeats only %d times in 4.5 s\n", nm, g_onsets); }   /* the Arp bank must arpeggiate or repeat */
        printf("%d,\"%s\",%.4f,%.1f,%.1f,%.1f,%d\n", i, nm, trim, before, after, peak, g_onsets);
    }
    fprintf(stderr, "%d of %d presets are more than 2.5 dB from the %.1f dB target\n", bad, cnt, target);
    return (!calibrate && bad) ? 1 : 0;
}
