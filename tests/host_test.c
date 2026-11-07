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

static void render(void *inst, int16_t *out, int blocks) {
    for (int b = 0; b < blocks; b++) {
        rt_enter(); api->render_block(inst, out + b * FRAMES * 2, FRAMES); rt_leave();
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
static void midi(void *inst, uint8_t a, uint8_t b, uint8_t c) {
    uint8_t m[3] = {a, b, c}; rt_enter(); api->on_midi(inst, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); rt_leave();
}
static void setp(void *inst, const char *k, const char *v) { rt_enter(); api->set_param(inst, k, v); rt_leave(); }

/* Extract "key":"..." names from ui_hierarchy. Returns count. */
static int param_keys(void *inst, char keys[][48], int max) {
    static char buf[65536];
    rt_enter(); int n = api->get_param(inst, "ui_hierarchy", buf, sizeof buf); rt_leave();
    CHECK(n > 0 && n < (int)sizeof buf, "ui_hierarchy missing or oversize (%d)", n);
    int c = 0;
    for (char *p = buf; n > 0 && (p = strstr(p, "\"key\":\"")) && c < max; ) {
        p += 7; char *e = strchr(p, '"'); if (!e) break;
        snprintf(keys[c++], 48, "%.*s", (int)(e - p), p); p = e;
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
    host.api_version = 1; host.sample_rate = MOVE_SAMPLE_RATE; host.frames_per_block = FRAMES;
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
