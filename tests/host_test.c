/* Desktop host test: loads dsp.so like the Schwung chain host, renders, checks, writes WAV.
 * usage: host_test <dsp.so> <out_dir> */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>
#include "plugin_api_v1.h"

#define FRAMES 128
#define BLOCKS 690 /* ~2 s */

static void nop(void) {}
static void (*rt_enter)(void) = nop, (*rt_leave)(void) = nop;
static int (*rt_count)(void);
static plugin_api_v2_t *api;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void write_wav(const char *path, const int16_t *s, int frames) {
    FILE *f = fopen(path, "wb"); if (!f) return;
    uint32_t data = frames * 4, rate = 44100, br = rate * 4, riff = 36 + data, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(s, 4, frames, f); fclose(f);
}

typedef struct { double peak, rms, dc; int clipped; } metrics_t;
static metrics_t measure(const int16_t *s, int frames) {
    metrics_t m = {0}; double sum = 0, sq = 0;
    for (int i = 0; i < frames * 2; i++) {
        double v = s[i] / 32768.0; sum += v; sq += v * v;
        if (fabs(v) > m.peak) m.peak = fabs(v);
        if (s[i] == 32767 || s[i] == -32768) m.clipped++;
    }
    m.dc = sum / (frames * 2); m.rms = sqrt(sq / (frames * 2)); return m;
}

/* Mock transport: beat position derived from rendered samples at 120 BPM; g_beat_off shifts the grid, g_transport turns it on. */
static int g_transport; static double g_beat_samples, g_beat_off;
static double mock_beat(void) { return g_transport ? g_beat_off + g_beat_samples / 44100.0 * 2.0 : -1.0; }
static void render(void *inst, int16_t *out, int blocks) {
    for (int b = 0; b < blocks; b++) {
        rt_enter(); api->render_block(inst, out + b * FRAMES * 2, FRAMES); rt_leave();
        g_beat_samples += FRAMES;
    }
}
/* Release everything and render until the plugin outputs exact silence (returns blocks taken, or -1 if it never does). */
static int drain(void *inst, int16_t *out) {
    uint8_t m3[3] = {0xB0, 123, 0};
    rt_enter(); api->on_midi(inst, m3, 3, MOVE_MIDI_SOURCE_EXTERNAL); rt_leave();
    for (int b = 0; b < 9000; b++) {
        render(inst, out, 1);
        int nz = 0; for (int i = 0; i < FRAMES * 2; i++) if (out[i]) nz = 1;
        if (!nz) return b;
    }
    return -1;
}
static void midi_src(void *inst, uint8_t a, uint8_t b, uint8_t c, int src) {
    uint8_t m3[3] = {a, b, c}; rt_enter(); api->on_midi(inst, m3, 3, src); rt_leave();
}
/* Rising zero crossings per second in the left channel over [start, start+len) frames. */
static double pitch_hz(const int16_t *x, int start, int len) {
    int zc = 0;
    for (int i = start + 1; i < start + len; i++) if (x[2 * (i - 1)] < 0 && x[2 * i] >= 0) zc++;
    return zc / (len / 44100.0);
}
static double rms_win(const int16_t *x, int start, int len) {
    double a = 0; for (int i = start; i < start + len; i++) { double v = x[2 * i] / 32768.0; a += v * v; } return sqrt(a / len);
}
static void midi(void *inst, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t m[3] = {a, b, c}; rt_enter(); api->on_midi(inst, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); rt_leave();
}
static void setp(void *inst, const char *k, const char *v) { rt_enter(); api->set_param(inst, k, v); rt_leave(); }

/* Read chain_params, return the parameter keys (skipping "preset") and validate the UI contract:
 * every param has a proper-case name, a short_name of <= 6 chars, and appears in the hierarchy; module name is served. */
static int param_keys(void *inst, char keys[][48], int max) {
    static char cp[65536], hier[65536];
    rt_enter(); int n = api->get_param(inst, "chain_params", cp, sizeof cp); rt_leave();
    CHECK(n > 0 && n < (int)sizeof cp, "chain_params missing or oversize (%d)", n);
    rt_enter(); int nh = api->get_param(inst, "ui_hierarchy", hier, sizeof hier); rt_leave();
    CHECK(nh > 0 && nh < (int)sizeof hier, "ui_hierarchy missing or oversize (%d)", nh);
    char nm[64]; rt_enter(); int nn = api->get_param(inst, "name", nm, sizeof nm); rt_leave();
    CHECK(nn > 0 && !strcmp(nm, "Mean Mod"), "module name is '%s'", nn > 0 ? nm : "(none)");
    CHECK(strstr(cp, "\"group\":\"amp\"") && strstr(cp, "\"group\":\"fenv\"") && strstr(cp, "\"group\":\"filter\""), "viz groups missing from chain_params");
    int c = 0;
    for (char *p = cp; n > 0 && (p = strstr(p, "\"key\":\"")) && c < max; ) {
        p += 7; char *e = strchr(p, '"'); if (!e) break;
        char key[48]; snprintf(key, sizeof key, "%.*s", (int)(e - p), p);
        char *obj_end = strstr(e, "\"key\":\""); size_t span = obj_end ? (size_t)(obj_end - e) : strlen(e);
        char obj[1024]; snprintf(obj, sizeof obj, "%.*s", (int)(span < sizeof obj - 1 ? span : sizeof obj - 1), e);
        if (strcmp(key, "preset")) {
            snprintf(keys[c++], 48, "%s", key);
            char *q = strstr(obj, "\"name\":\""); CHECK(q != NULL, "param %s has no name", key);
            if (q) CHECK(q[8] >= 'A' && q[8] <= 'Z', "param %s name is not proper case", key);
            q = strstr(obj, "\"short_name\":\"");
            CHECK(q != NULL, "param %s has no short_name", key);
            if (q) { q += 14; char *qe = strchr(q, '"'); CHECK(qe && qe - q >= 1 && qe - q <= 6, "param %s short_name length", key); }
            char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
            CHECK(strstr(hier, pat) != NULL, "param %s is on no page", key);
        }
        p = e;
    }
    return c;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s dsp.so out_dir\n", argv[0]); return 2; }
    void (*en)(void) = dlsym(RTLD_DEFAULT, "rt_guard_enter");
    void (*lv)(void) = dlsym(RTLD_DEFAULT, "rt_guard_leave");
    rt_count = dlsym(RTLD_DEFAULT, "rt_guard_count");
    int guarded = en && lv && rt_count;
    if (guarded) { rt_enter = en; rt_leave = lv; }
    else printf("note: rt_guard not preloaded, skipping malloc/lock check\n");

    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    if (!init) { fprintf(stderr, "no %s\n", MOVE_PLUGIN_INIT_V2_SYMBOL); return 2; }

    host_api_v1_t host; memset(&host, 0, sizeof host);
    host.api_version = 1; host.sample_rate = MOVE_SAMPLE_RATE; host.frames_per_block = FRAMES; host.get_beat_position = mock_beat;
    api = init(&host);
    CHECK(api && api->api_version == 2, "bad api struct");
    CHECK(api->create_instance && api->destroy_instance && api->on_midi && api->set_param &&
          api->get_param && api->get_error && api->render_block, "NULL entry point");
    rt_enter(); void *inst = api->create_instance(argv[2], NULL); rt_leave();   /* create is held to the same no-alloc rule */
    CHECK(inst != NULL, "create_instance failed");
    if (!api || !inst) return 1;
    if (guarded) rt_count(); /* baseline taken below: only RT-section hits are counted by the guard */

    char keys[128][48]; int nk = param_keys(inst, keys, 128);
    printf("params: %d\n", nk);

    static int16_t out[BLOCKS * FRAMES * 2];
    char path[512];

    /* 1. default note render */
    midi(inst, 0x90, 48, 100); render(inst, out, BLOCKS / 2); midi(inst, 0x80, 48, 0);
    render(inst, out + (BLOCKS / 2) * FRAMES * 2, BLOCKS - BLOCKS / 2);
    metrics_t m = measure(out, BLOCKS * FRAMES);
    printf("default: peak %.3f rms %.3f dc %.4f clipped %d\n", m.peak, m.rms, m.dc, m.clipped);
    CHECK(m.peak < 1.0 && fabs(m.dc) < 0.01, "default render peak/dc out of range");
    snprintf(path, sizeof path, "%s/default.wav", argv[2]); write_wav(path, out, BLOCKS * FRAMES);
    if (nk > 0) {  /* pitch check: default patch is a saw at MIDI 48 (130.81 Hz): one rising crossing per cycle */
        int zc = 0, a = 2000, b = 40000;
        for (int i = a; i < b; i++) if (out[2 * (i - 1)] < 0 && out[2 * i] >= 0) zc++;
        double hz = zc / ((b - a) / 44100.0);
        printf("pitch estimate: %.1f Hz (expect ~130.8)\n", hz);
        CHECK(m.rms > 0.01 ? fabs(hz - 130.81) < 6.0 : 1, "pitch off: %.1f Hz", hz);
        CHECK(m.rms > 0.01, "default patch is silent");
    }

    /* 2. all params at min, then max (with low/mid/high notes) */
    for (int pass = 0; pass < 2; pass++) {
        for (int k = 0; k < nk; k++) setp(inst, keys[k], pass ? "1" : "0");
        for (int n = 0; n < 3; n++) {
            int note = 24 + n * 36;
            midi(inst, 0x90, note, 127); render(inst, out, BLOCKS / 3);
            m = measure(out, (BLOCKS / 3) * FRAMES);
            printf("stress %s note %d: peak %.3f rms %.3f dc %.4f\n", pass ? "max" : "min", note, m.peak, m.rms, m.dc);
            CHECK(isfinite(m.rms) && fabs(m.dc) < 0.05, "stress %d note %d bad (rms %f dc %f)", pass, note, m.rms, m.dc);
            midi(inst, 0x80, note, 0);
        }
    }
    /* 2b. every factory preset: finite, bounded, audible, no DC */
    {
        char b2[32]; rt_enter(); int pc = api->get_param(inst, "preset_count", b2, sizeof b2); rt_leave();
        int cnt = pc > 0 ? atoi(b2) : 0;
        printf("presets: %d\n", cnt);
        CHECK(drain(inst, out) >= 0, "plugin never went silent after the stress tests");
        for (int pi = 0; pi < cnt; pi++) {
            snprintf(b2, sizeof b2, "%d", pi); setp(inst, "preset", b2);
            char nm[64]; rt_enter(); api->get_param(inst, "preset_name", nm, sizeof nm); rt_leave();
            render(inst, out, 40);   /* selecting a preset must never make sound by itself (drones used to) */
            m = measure(out, 40 * FRAMES);
            CHECK(m.peak < 0.0005, "preset %d '%s' sounds with no note held (peak %.4f)", pi, nm, m.peak);
            for (int note = 36; note <= 72; note += 36) {
                midi(inst, 0x90, note, 110); render(inst, out, 200); midi(inst, 0x80, note, 0); render(inst, out + 200 * FRAMES * 2, 100);
                m = measure(out, 300 * FRAMES);
                if (note == 36) printf("  %-22s peak %.2f rms %.3f dc %+.4f\n", nm, m.peak, m.rms, m.dc);
                CHECK(isfinite(m.rms) && m.peak < 1.0 && fabs(m.dc) < 0.02 && m.peak > 0.005,
                      "preset %d '%s' note %d: peak %.3f rms %.3f dc %.4f", pi, nm, note, m.peak, m.rms, m.dc);
            }
            /* a held note must still be audible after ~1.2 s (catches "very brief" presets) */
            midi(inst, 0x90, 48, 110); render(inst, out, 400);
            m = measure(out + 320 * FRAMES * 2, 80 * FRAMES);
            if (m.rms < 0.02) printf("  note: %-22s tail rms %.4f at 1.2 s\n", nm, m.rms);
            CHECK(m.rms > 0.002, "preset %d '%s' dies while a note is held (rms %.4f at 1.2 s)", pi, nm, m.rms);
            midi(inst, 0x80, 48, 0);
            CHECK(drain(inst, out) >= 0, "preset %d '%s' never goes silent after release", pi, nm);
            render(inst, out, 400);   /* let slow release tails finish so the next preset starts from true silence */
        }
        setp(inst, "preset", "9999"); setp(inst, "preset", "-5");  /* out of range must be ignored */
        setp(inst, "preset", "0");
    }
    /* 2e. drone modes: HOLD follows the key, LATCH holds until panic / preset / drone change */
    {
        setp(inst, "preset", "0"); setp(inst, "env2_r", "0.3");
        setp(inst, "drone", "1"); midi(inst, 0x90, 48, 100); render(inst, out, 100); midi(inst, 0x80, 48, 0); render(inst, out, 300);
        m = measure(out + 200 * FRAMES * 2, 100 * FRAMES);
        CHECK(m.peak == 0.0, "drone HOLD should stop after release (peak %.4f)", m.peak);
        setp(inst, "drone", "2"); midi(inst, 0x90, 48, 100); render(inst, out, 100); midi(inst, 0x80, 48, 0); render(inst, out, 300);
        m = measure(out + 200 * FRAMES * 2, 100 * FRAMES);
        CHECK(m.rms > 0.05, "drone LATCH should keep sounding after note-off (rms %.4f)", m.rms);
        CHECK(drain(inst, out) >= 0, "drone LATCH should stop on all-notes-off");
        midi(inst, 0x90, 48, 100); render(inst, out, 50); midi(inst, 0x80, 48, 0); render(inst, out, 50);
        setp(inst, "preset", "0");   /* preset change must release the latch */
        CHECK(drain(inst, out) >= 0, "preset change should release a drone latch");
        midi(inst, 0x90, 48, 100); render(inst, out, 50); midi(inst, 0x80, 48, 0); render(inst, out, 50);
        setp(inst, "drone", "0");    /* ... and so must switching the drone off */
        CHECK(drain(inst, out) >= 0, "drone off should release a latch");
        setp(inst, "preset", "0");
        /* idle voice renders exact silence */
        render(inst, out, 4); m = measure(out, 4 * FRAMES); CHECK(m.peak == 0.0, "idle voice not silent");
    }
    /* 2f. sustain pedal (CC64): note-off is deferred while the pedal is down */
    {
        setp(inst, "preset", "0"); setp(inst, "env2_r", "0.2"); setp(inst, "env2_s", "1"); setp(inst, "drone", "0");
        midi(inst, 0xB0, 64, 127); midi(inst, 0x90, 55, 100); render(inst, out, 60); midi(inst, 0x80, 55, 0); render(inst, out, 200);
        m = measure(out + 100 * FRAMES * 2, 100 * FRAMES);
        CHECK(m.rms > 0.05, "pedal down should hold the note (rms %.4f)", m.rms);
        midi(inst, 0xB0, 64, 0); render(inst, out, 400);
        m = measure(out + 300 * FRAMES * 2, 100 * FRAMES);
        CHECK(m.peak < 0.001, "pedal up should release the held note (peak %.4f)", m.peak);
        midi(inst, 0xB0, 123, 0); drain(inst, out);
        setp(inst, "preset", "0");
    }
    /* 2g. Grit/Dark macro: + is darker and more saturated, - is brighter; centre leaves the patch alone */
    {
        double bright[3], rmsv[3]; const char *gv[3] = {"-1", "0", "1"};
        for (int g = 0; g < 3; g++) {
            setp(inst, "preset", "0"); setp(inst, "osc1_wave", "7"); setp(inst, "grit", gv[g]); setp(inst, "env2_s", "1");
            midi(inst, 0x90, 48, 110); render(inst, out, 200);
            const int16_t *x = out + 100 * FRAMES * 2; int N = 100 * FRAMES; double d = 0, a = 0, r2 = 0;
            for (int i = 1; i < N; i++) { d += fabs((double)x[2 * i] - x[2 * (i - 1)]); a += fabs((double)x[2 * i]); r2 += (double)x[2 * i] * x[2 * i]; }
            bright[g] = d / (a + 1.0); rmsv[g] = sqrt(r2 / N) / 32768.0;
            midi(inst, 0xB0, 123, 0); drain(inst, out);
        }
        printf("grit -1/0/+1: brightness %.3f %.3f %.3f  rms %.3f %.3f %.3f\n", bright[0], bright[1], bright[2], rmsv[0], rmsv[1], rmsv[2]);
        CHECK(bright[0] > bright[1] && bright[1] > bright[2], "grit should darken monotonically");
        CHECK(rmsv[2] > 0.01 && rmsv[0] > 0.01, "grit extremes should stay audible");
        setp(inst, "preset", "0");
    }
    /* 2h. Hold: notes keep sounding after release; a fresh press replaces them; Hold off releases */
    {
        setp(inst, "preset", "0"); setp(inst, "env2_r", "0.2"); setp(inst, "hold", "1");
        midi(inst, 0x90, 60, 100); midi(inst, 0x90, 64, 100); midi(inst, 0x90, 67, 100); render(inst, out, 40);
        midi(inst, 0x80, 60, 0); midi(inst, 0x80, 64, 0); midi(inst, 0x80, 67, 0); render(inst, out, 200);
        CHECK(rms_win(out, 150 * FRAMES, 40 * FRAMES) > 0.05, "Hold should keep the notes sounding after release");
        midi(inst, 0x90, 72, 100); render(inst, out, 120);   /* new gesture: replaces the latched chord (last-note priority) */
        double hz = pitch_hz(out, 60 * FRAMES, 50 * FRAMES);
        printf("hold replace pitch: %.1f Hz (expect ~523.3)\n", hz);
        CHECK(fabs(hz - 523.3) < 25.0, "fresh press under Hold should replace latched notes (%.1f Hz)", hz);
        midi(inst, 0x80, 72, 0); render(inst, out, 60);
        CHECK(rms_win(out, 30 * FRAMES, 30 * FRAMES) > 0.05, "Hold should keep the new note");
        setp(inst, "hold", "0"); render(inst, out, 400);
        CHECK(rms_win(out, 300 * FRAMES, 80 * FRAMES) < 0.001, "Hold off should release latched notes");
        setp(inst, "preset", "0");
    }
    /* 2i. arpeggiator: pattern order, gate length, latching with Hold (host tempo defaults to 120 BPM in this harness) */
    {
        const double fexp[3] = {130.81, 164.81, 196.0}; const char *modes[2] = {"1", "2"};
        for (int mi = 0; mi < 2; mi++) {   /* Up, then Down */
            setp(inst, "preset", "0"); setp(inst, "env2_r", "0.2"); setp(inst, "arp_mode", modes[mi]); setp(inst, "arp_rate", "3"); setp(inst, "arp_gate", "1");
            midi(inst, 0x90, 55, 100); midi(inst, 0x90, 48, 100); midi(inst, 0x90, 52, 100);   /* played out of order on purpose */
            render(inst, out, 300);
            int bad = 0;
            for (int k = 0; k < 6; k++) {
                double hz = pitch_hz(out, (int)(k * 5512.5) + 882, 4410);
                int want = mi == 0 ? k % 3 : 2 - (k % 3);
                int got = 0; double bd = 1e9; for (int j = 0; j < 3; j++) if (fabs(hz - fexp[j]) < bd) { bd = fabs(hz - fexp[j]); got = j; }
                if (got != want || bd > 14.0) bad++;
            }
            printf("arp %s: pattern %s\n", mi ? "down" : "up", bad ? "WRONG" : "ok");
            CHECK(bad == 0, "arp %s pattern wrong (%d of 6 steps)", mi ? "down" : "up", bad);
            midi(inst, 0x80, 55, 0); midi(inst, 0x80, 48, 0); midi(inst, 0x80, 52, 0);
            render(inst, out, 80);
            CHECK(rms_win(out, 60 * FRAMES, 20 * FRAMES) < 0.001, "arp should stop when the keys are released");
            drain(inst, out);
        }
        /* gate: 50% of a 125 ms step -> sounding early in the step, silent late */
        setp(inst, "preset", "0"); setp(inst, "env2_r", "0.1"); setp(inst, "arp_mode", "1"); setp(inst, "arp_rate", "3"); setp(inst, "arp_gate", "0.5");
        midi(inst, 0x90, 48, 100); render(inst, out, 160);
        double early = rms_win(out, (int)(2 * 5512.5) + 441, 1800), late = rms_win(out, (int)(2 * 5512.5) + 4000, 1200);
        printf("arp gate .5: early rms %.3f late rms %.4f\n", early, late);
        CHECK(early > 0.05 && late < 0.01, "arp gate length wrong (early %.3f late %.4f)", early, late);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* latch: Hold keeps the arp running after the keys are up; Hold off stops it */
        setp(inst, "preset", "0"); setp(inst, "env2_r", "0.2"); setp(inst, "arp_mode", "1"); setp(inst, "arp_gate", "1"); setp(inst, "hold", "1");
        midi(inst, 0x90, 48, 100); midi(inst, 0x90, 55, 100); render(inst, out, 30); midi(inst, 0x80, 48, 0); midi(inst, 0x80, 55, 0); render(inst, out, 200);
        CHECK(rms_win(out, 150 * FRAMES, 40 * FRAMES) > 0.05, "arp should keep running while latched");
        setp(inst, "hold", "0"); render(inst, out, 80);
        CHECK(rms_win(out, 60 * FRAMES, 20 * FRAMES) < 0.001, "arp should stop when Hold is turned off");
        drain(inst, out); setp(inst, "preset", "0");
    }
    /* 2i2. arp transport sync: with a running transport, step onsets land on the beat grid (125 ms at 1/16, 120 BPM), wherever the key was pressed */
    {
        setp(inst, "preset", "0"); setp(inst, "env2_a", "0"); setp(inst, "env2_r", "0.05"); setp(inst, "arp_mode", "1"); setp(inst, "arp_rate", "3"); setp(inst, "arp_gate", "0.3");
        g_transport = 1; g_beat_samples = 0; g_beat_off = 0.1;   /* grid boundary every 5512.5 samples; first at beat 0.25 */
        render(inst, out, 7);   /* idle time offsets the key press off the grid */
        midi(inst, 0x90, 48, 100); render(inst, out, 200);
        /* beat of each onset: find rising edges of the left-channel envelope */
        int onsets = 0, last = -100000, bad = 0; double maxdev = 0;
        for (int i = 6000; i < 200 * FRAMES; i++) {   /* skip the immediate first note and its tail */
            if (fabs(out[2 * i] / 32768.0) > 0.02 && i - last > 3000) {
                last = i; onsets++;
                {
                    double beat = g_beat_off + ((double)(7 * FRAMES) + i) / 44100.0 * 2.0 - 0.0;
                    double s = beat * 4.0, dev = fabs(s - floor(s + 0.5)) * 5512.5;
                    printf("  onset %d at frame %d: %.0f samples from grid\n", onsets, i, (s - floor(s + 0.5)) * 5512.5);
                    if (dev > maxdev) maxdev = dev;
                    if (dev > 160) bad++;
                }
            }
        }
        printf("arp sync: %d onsets, max grid deviation %.0f samples\n", onsets, maxdev);
        CHECK(onsets >= 4 && bad == 0, "arp onsets must sit on the beat grid (%d onsets, %d off-grid, max %.0f samples)", onsets, bad, maxdev);
        midi(inst, 0x80, 48, 0); drain(inst, out); g_transport = 0; setp(inst, "preset", "0");
    }
    /* 2k. mod matrix: routing, scaling, unipolar/bipolar sources, stability over every source x target pair */
    {
        char v[16];
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "vibrato_depth", "0"); setp(inst, "drift", "0");
        midi(inst, 0x90, 55, 100); render(inst, out, 60);
        double base = pitch_hz(out, 20 * FRAMES, 40 * FRAMES);
        midi(inst, 0xB0, 1, 127);                                      /* mod wheel full */
        setp(inst, "m1_src", "5"); setp(inst, "m1_dst", "1"); setp(inst, "m1_amt", "0.5");   /* wheel -> pitch, +6 semitones */
        render(inst, out, 200);
        double up = pitch_hz(out, 150 * FRAMES, 40 * FRAMES);
        printf("matrix wheel->pitch: ratio %.3f (expect ~1.414)\n", up / base);
        CHECK(fabs(up / base - 1.4142) < 0.07, "matrix wheel->pitch ratio %.3f", up / base);
        setp(inst, "m1_amt", "-0.5"); render(inst, out, 200);
        double dn = pitch_hz(out, 150 * FRAMES, 40 * FRAMES);
        CHECK(fabs(dn / base - 0.7071) < 0.05, "matrix negative amount ratio %.3f", dn / base);
        setp(inst, "m1_dst", "12"); setp(inst, "m1_amt", "-1"); render(inst, out, 100);   /* wheel -> amp at -1: silent */
        CHECK(rms_win(out, 60 * FRAMES, 30 * FRAMES) < 0.001, "matrix wheel->amp -1 should silence the voice");
        setp(inst, "m1_src", "0"); render(inst, out, 100);                                 /* source Off: slot inert */
        CHECK(rms_win(out, 60 * FRAMES, 30 * FRAMES) > 0.05, "a slot with source Off must do nothing");
        setp(inst, "m1_src", "5"); setp(inst, "m1_amt", "0"); render(inst, out, 100);      /* amount 0: inert */
        CHECK(rms_win(out, 60 * FRAMES, 30 * FRAMES) > 0.05, "a slot with amount 0 must do nothing");
        midi(inst, 0xB0, 1, 0); midi(inst, 0x80, 55, 0); drain(inst, out);
        /* Random: a new value per note trigger */
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "m2_src", "10"); setp(inst, "m2_dst", "1"); setp(inst, "m2_amt", "0.6");
        double hz[4];
        for (int n = 0; n < 4; n++) { midi(inst, 0x90, 55, 100); render(inst, out, 40); hz[n] = pitch_hz(out, 10 * FRAMES, 30 * FRAMES); midi(inst, 0x80, 55, 0); drain(inst, out); }
        int distinct = 0; for (int n = 1; n < 4; n++) if (fabs(hz[n] - hz[0]) > 3.0) distinct++;
        printf("matrix random: %.1f %.1f %.1f %.1f Hz\n", hz[0], hz[1], hz[2], hz[3]);
        CHECK(distinct >= 2, "Random source should give a different pitch per note (%d distinct)", distinct);
        /* LFO sync: a synced square LFO on pitch (+-6 semitones) flips on the beat grid; LFO 2 works as a source */
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "vibrato_depth", "0"); setp(inst, "drift", "0");
        setp(inst, "osc1_wave", "2"); setp(inst, "resonance", "0"); setp(inst, "flt_fm1", "0"); setp(inst, "cutoff", "0.5"); setp(inst, "drive", "0");
        setp(inst, "lfo_wave", "1"); setp(inst, "lfo_sync", "2");                       /* square, 1/4 note = one cycle per beat */
        setp(inst, "m1_src", "1"); setp(inst, "m1_dst", "1"); setp(inst, "m1_amt", "0.5");
        g_transport = 1; g_beat_samples = 0; g_beat_off = 0.1;
        midi(inst, 0x90, 48, 100); render(inst, out, 200);
        double hi = pitch_hz(out, (int)(0.02 * 44100), 6000), lo = pitch_hz(out, (int)(0.23 * 44100), 9000);   /* beats 0.14-0.4 and 0.56-0.97 */
        printf("lfo sync square: high %.1f Hz, low %.1f Hz (ratio %.2f, expect 2)\n", hi, lo, hi / lo);
        CHECK(fabs(hi / lo - 2.0) < 0.2, "synced LFO should flip pitch on the beat grid (%.1f vs %.1f Hz)", hi, lo);
        g_transport = 0; midi(inst, 0x80, 48, 0); drain(inst, out);
        setp(inst, "lfo_sync", "0"); setp(inst, "lfo_wave", "0"); setp(inst, "lfo2_wave", "1"); setp(inst, "lfo2_rate", "0.5");
        setp(inst, "m1_src", "11"); setp(inst, "m1_amt", "0.5");
        midi(inst, 0x90, 48, 100); render(inst, out, 200);
        double mn = 1e9, mx = 0; for (int w = 0; w < 10; w++) { double h = pitch_hz(out, 2000 + w * 2400, 2000); if (h < mn) mn = h; if (h > mx) mx = h; }
        printf("lfo2 square on pitch: %.1f .. %.1f Hz\n", mn, mx);
        CHECK(mx / mn > 1.5, "LFO 2 as a matrix source should move pitch (%.1f..%.1f)", mn, mx);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* every source x target pair at both polarities stays finite, bounded and silent after release */
        setp(inst, "preset", "0"); setp(inst, "lfo_rate", "0.6");
        double worst = 0; int nodrain = 0;
        for (int s = 1; s <= 11; s++) for (int d = 1; d <= 15; d++) for (int pol = 0; pol < 2; pol++) {
            char sv[12], dv[12], wv[12], yv[12]; snprintf(sv, sizeof sv, "%d", s); snprintf(dv, sizeof dv, "%d", d);
            snprintf(wv, sizeof wv, "%d", (s + d) % 5); snprintf(yv, sizeof yv, "%d", (s * d) % 8);
            setp(inst, "lfo_wave", wv); setp(inst, "lfo2_wave", wv); setp(inst, "lfo2_sync", yv); setp(inst, "lfo_sync", d % 3 == 0 ? yv : "0");
            setp(inst, "m1_src", sv); setp(inst, "m1_dst", dv); setp(inst, "m1_amt", pol ? "-1" : "1");
            setp(inst, "stick_x", "1"); setp(inst, "stick_y", "0.7");
            midi(inst, 0xB0, 1, 127); midi(inst, 0xD0, 100, 0); midi(inst, 0x90, 48, 120); render(inst, out, 25);
            metrics_t mm = measure(out, 25 * FRAMES); if (mm.peak > worst) worst = mm.peak;
            CHECK(mm.peak <= 1.0 && mm.clipped < 25 * FRAMES / 2, "matrix s%d d%d pol%d unstable (peak %.2f clipped %d)", s, d, pol, mm.peak, mm.clipped);
            midi(inst, 0x80, 48, 0); if (drain(inst, out) < 0) nodrain++;
        }
        printf("matrix sweep: worst peak %.2f, undrained %d\n", worst, nodrain);
        CHECK(nodrain == 0, "matrix pairs left the voice sounding (%d)", nodrain);
        setp(inst, "stick_x", "0"); setp(inst, "stick_y", "0"); midi(inst, 0xB0, 1, 0); midi(inst, 0xD0, 0, 0); setp(inst, "preset", "0");
        (void)v;
    }
    /* 2j. stick: touch makes it hold, release springs it back; no touch events -> timed recentre; stick down toggles Hold */
    {
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1");
        midi_src(inst, 0x90, 0, 127, MOVE_MIDI_SOURCE_TOUCH); midi_src(inst, 0x80, 0, 0, MOVE_MIDI_SOURCE_TOUCH);
        render(inst, out, 20);
        CHECK(measure(out, 20 * FRAMES).peak == 0.0, "a touch event must never sound a note");
        char v[32];
        midi(inst, 0x90, 55, 100); render(inst, out, 140);
        double base = pitch_hz(out, 30 * FRAMES, 100 * FRAMES);
        midi_src(inst, 0x90, 0, 127, MOVE_MIDI_SOURCE_TOUCH); setp(inst, "stick_x", "1"); render(inst, out, 300);
        rt_enter(); api->get_param(inst, "stick_x", v, sizeof v); rt_leave();
        CHECK(atof(v) > 0.99, "stick should stay while the knob is touched (%s)", v);
        double bent = pitch_hz(out, 190 * FRAMES, 100 * FRAMES);
        printf("stick bend: base %.1f Hz, bent %.1f Hz (ratio %.3f, expect ~1.498, a fifth)\n", base, bent, bent / base);
        CHECK(fabs(bent / base - 1.4983) < 0.06, "stick X bend ratio %.3f", bent / base);
        midi_src(inst, 0x80, 0, 0, MOVE_MIDI_SOURCE_TOUCH);
        rt_enter(); api->get_param(inst, "stick_x", v, sizeof v); rt_leave();
        CHECK(atof(v) == 0.0, "stick should spring back on touch release (%s)", v);
        render(inst, out, 160);
        double back = pitch_hz(out, 40 * FRAMES, 100 * FRAMES);
        CHECK(fabs(back / base - 1.0) < 0.03, "pitch should return after release (%.1f vs %.1f)", back, base);
        /* with Y on its default target (Vibrato Depth), pushing Y fully down must never touch Hold (it used to stop the arp) */
        midi_src(inst, 0x90, 1, 127, MOVE_MIDI_SOURCE_TOUCH);
        setp(inst, "stick_y", "-1"); render(inst, out, 300); rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "Y at the bottom must not toggle Hold unless Y is assigned to Hold Toggle (%s)", v);
        midi_src(inst, 0x80, 1, 0, MOVE_MIDI_SOURCE_TOUCH);
        setp(inst, "stick_y_to", "4");
        /* sweeping Y down through the lower half and back, or a brief tap of the bottom, must NOT touch Hold */
        midi_src(inst, 0x90, 1, 127, MOVE_MIDI_SOURCE_TOUCH);
        for (int s = 0; s <= 20; s++) { char sv[16]; snprintf(sv, sizeof sv, "%.2f", -s * 0.05); setp(inst, "stick_y", sv); render(inst, out, 2); }
        for (int s = 20; s >= 0; s--) { char sv[16]; snprintf(sv, sizeof sv, "%.2f", -s * 0.05); setp(inst, "stick_y", sv); render(inst, out, 2); }
        rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "a sweep of Y must not toggle Hold (%s)", v);
        /* a deliberate push to the bottom, held, toggles Hold once */
        setp(inst, "stick_y", "-1"); render(inst, out, 40);
        rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "Hold must wait for the dwell (%s)", v);
        render(inst, out, 80); rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 1, "stick held down should switch Hold on (%s)", v);
        render(inst, out, 400); rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 1, "staying at the bottom must not keep toggling (%s)", v);
        midi_src(inst, 0x80, 1, 0, MOVE_MIDI_SOURCE_TOUCH);   /* release centres Y */
        midi_src(inst, 0x90, 1, 127, MOVE_MIDI_SOURCE_TOUCH);
        setp(inst, "stick_y", "-1"); render(inst, out, 120); rt_enter(); api->get_param(inst, "hold", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "second stick-down should switch Hold off (%s)", v);
        midi_src(inst, 0x80, 1, 0, MOVE_MIDI_SOURCE_TOUCH);
        midi(inst, 0xB0, 123, 0); drain(inst, out); setp(inst, "preset", "0");
        /* a fresh instance that never sees touch events: displaced stick recentres by itself */
        rt_enter(); void *i2 = api->create_instance(".", NULL); rt_leave();
        if (i2) {
            api->set_param(i2, "stick_x", "1"); for (int b = 0; b < 200; b++) api->render_block(i2, out, FRAMES);
            api->get_param(i2, "stick_x", v, sizeof v);
            CHECK(atof(v) == 0.0, "stick should recentre on its own without touch events (%s)", v);
            api->destroy_instance(i2);
        }
    }
    /* 2c. state round trip, tiny-buffer refusal, malformed state */
    {
        static char st1[8192], st2[8192]; char v[32];
        setp(inst, "preset", "5"); setp(inst, "cutoff", "0.31"); setp(inst, "at_dest", "3");
        rt_enter(); int n1 = api->get_param(inst, "state", st1, sizeof st1); rt_leave();
        CHECK(n1 > 100 && n1 < (int)sizeof st1, "state length %d", n1);
        printf("state bytes: %d\n", n1);
        setp(inst, "preset", "0"); setp(inst, "cutoff", "0.9");
        setp(inst, "state", st1);
        rt_enter(); int n2 = api->get_param(inst, "state", st2, sizeof st2); rt_leave();
        CHECK(n1 == n2 && strcmp(st1, st2) == 0, "state did not round-trip");
        rt_enter(); api->get_param(inst, "preset", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 5, "preset index not restored (%s)", v);
        char tiny[16]; rt_enter(); int nt = api->get_param(inst, "state", tiny, sizeof tiny); rt_leave();
        CHECK(nt < 0, "tiny buffer should be refused, got %d", nt);
        setp(inst, "state", "{garbage"); setp(inst, "state", ""); setp(inst, "state", "{\"cutoff\":nan,\"resonance\":1e99}");
        rt_enter(); api->get_param(inst, "resonance", v, sizeof v); rt_leave();
        CHECK(atof(v) <= 1.0, "resonance not clamped (%s)", v);
        setp(inst, "preset", "0");
    }
    /* 2d. note priority: hold 72 then 60 with LOW priority -> sounds 60 (261.6 Hz) */
    {
        setp(inst, "priority", "1"); setp(inst, "legato", "1");
        midi(inst, 0x90, 72, 100); render(inst, out, 30); midi(inst, 0x90, 60, 100); render(inst, out, 330);
        int zc = 0, a = 128 * 80, b = 128 * 330;
        for (int i = a; i < b; i++) if (out[2 * (i - 1)] < 0 && out[2 * i] >= 0) zc++;
        double hz = zc / ((b - a) / 44100.0);
        printf("low-priority pitch: %.1f Hz (expect ~261.6)\n", hz);
        CHECK(fabs(hz - 261.6) < 12.0, "low-note priority wrong: %.1f Hz", hz);
        midi(inst, 0xB0, 123, 0); render(inst, out, 40);
        setp(inst, "priority", "0"); setp(inst, "legato", "0");
    }
    /* 3. garbage input must not crash or poison state */
    setp(inst, "nonexistent", "x"); setp(inst, "cutoff", "nan"); setp(inst, "cutoff", "inf"); setp(inst, "cutoff", "");
    midi(inst, 0xE0, 0, 0x7F); midi(inst, 0xA0, 60, 127); midi(inst, 0xB0, 1, 127); midi(inst, 0xFF, 0, 0);
    midi(inst, 0x90, 60, 100);
    render(inst, out, 16);
    m = measure(out, 16 * FRAMES);
    CHECK(isfinite(m.rms), "non-finite after bad input");

    /* 3b. rough CPU cost (x86 host, informational only; the A72 will be several times slower) */
    {
        setp(inst, "preset", "0"); setp(inst, "osc2_wave", "7"); setp(inst, "osc1_wave", "7"); setp(inst, "resonance", "0.8"); setp(inst, "drive", "1");
        midi(inst, 0x90, 48, 100);
        struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
        for (int i = 0; i < 4000; i++) api->render_block(inst, out, FRAMES);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        double us = ((t1.tv_sec - t0.tv_sec) * 1e9 + (t1.tv_nsec - t0.tv_nsec)) / 1e3 / 4000.0;
        printf("render cost (heavy patch, host CPU): %.1f us/block (budget on device ~2370 us)\n", us);
        midi(inst, 0xB0, 123, 0); setp(inst, "preset", "0");
    }
    /* 4. realtime guard */
    if (guarded) { int v = rt_count(); printf("rt violations: %d\n", v); CHECK(v == 0, "%d malloc/lock calls inside RT entry points", v); }

    /* instance pool: 16 independent instances, 17th refused, slots reusable */
    {
        void *many[17]; int got = 0;
        for (int i = 0; i < 17; i++) { rt_enter(); many[i] = api->create_instance(argv[2], NULL); rt_leave(); if (many[i]) got++; }
        CHECK(got == 15, "pool should hand out 15 more (16 total with the test instance), got %d", got);
        api->set_param(many[0], "cutoff", "0.1"); api->set_param(many[1], "cutoff", "0.9");
        char c0[16], c1[16]; api->get_param(many[0], "cutoff", c0, 16); api->get_param(many[1], "cutoff", c1, 16);
        CHECK(atof(c0) < 0.2 && atof(c1) > 0.8, "instances share state (%s %s)", c0, c1);
        rt_enter(); for (int i = 0; i < 17; i++) if (many[i]) api->destroy_instance(many[i]); rt_leave();
        rt_enter(); void *again = api->create_instance(argv[2], NULL); rt_leave();
        CHECK(again != NULL, "slot not reusable after destroy");
        api->destroy_instance(again);
    }
    if (guarded) { int v = rt_count(); CHECK(v == 0, "%d malloc/lock calls in create/destroy", v); }
    rt_enter(); api->destroy_instance(inst); rt_leave(); dlclose(h);
    printf(fails ? "RESULT: FAIL (%d)\n" : "RESULT: PASS\n", fails);
    return fails ? 1 : 0;
}
