/* Desktop host test: loads dsp.so like the Schwung chain host, renders, checks, writes WAV.
 * usage: host_test <dsp.so> <out_dir> */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>
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
        if (strcmp(key, "preset") && strcmp(key, "bank_preset")) {
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

#define MM_VERSION_TEST "0.12.0"   /* bump with every release */
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s dsp.so out_dir\n", argv[0]); return 2; }
    void (*en)(void) = dlsym(RTLD_DEFAULT, "rt_guard_enter");
    void (*lv)(void) = dlsym(RTLD_DEFAULT, "rt_guard_leave");
    rt_count = dlsym(RTLD_DEFAULT, "rt_guard_count");
    int guarded = en && lv && rt_count;
    if (guarded) { rt_enter = en; rt_leave = lv; }
    else printf("note: rt_guard not preloaded, skipping malloc/lock check\n");

    char bankdir[600];   /* bank files: the plugin reads and writes under this folder (a background thread does the file work) */
    snprintf(bankdir, sizeof bankdir, "%s/mmdata", argv[2]);
    { char cmd[700]; snprintf(cmd, sizeof cmd, "rm -rf '%s'", bankdir); if (system(cmd)) {} }
    setenv("MEANMOD_BANK_DIR", bankdir, 1);
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
            char nm[64]; rt_enter(); api->get_param(inst, "preset_full", nm, sizeof nm); rt_leave();
            if (!strncmp(nm, "Arp ", 4)) { char am[16]; rt_enter(); api->get_param(inst, "arp_mode", am, sizeof am); rt_leave(); CHECK(atoi(am) > 0, "Arp bank preset '%s' must arpeggiate or repeat", nm); }
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
    /* 2m. performance gestures: flick the stick to step Arp Mode / Arp Rate; touch a knob for a momentary action */
    {
        char v[32];
        setp(inst, "preset", "0"); setp(inst, "stick_x_to", "5"); setp(inst, "stick_y_to", "5");   /* X = Arp Mode, Y = Arp Rate */
        setp(inst, "arp_mode", "1"); setp(inst, "arp_rate", "3");
        setp(inst, "stick_x", "1"); render(inst, out, 4);
        rt_enter(); api->get_param(inst, "arp_mode", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 2, "a rightward flick should step Arp Mode up (%s)", v);
        render(inst, out, 200);
        rt_enter(); api->get_param(inst, "arp_mode", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 2, "holding the flick must step only once (%s)", v);
        setp(inst, "stick_x", "0"); render(inst, out, 4); setp(inst, "stick_x", "-1"); render(inst, out, 4);
        rt_enter(); api->get_param(inst, "arp_mode", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 1, "a leftward flick should step Arp Mode back (%s)", v);
        setp(inst, "stick_x", "0"); render(inst, out, 4);
        setp(inst, "arp_mode", "0"); setp(inst, "stick_x", "-1"); render(inst, out, 4);
        rt_enter(); api->get_param(inst, "arp_mode", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 5, "Arp Mode should wrap from Off to As Played (%s)", v);
        setp(inst, "stick_x", "0"); render(inst, out, 4); setp(inst, "arp_mode", "1");
        setp(inst, "stick_y", "1"); render(inst, out, 4);
        rt_enter(); api->get_param(inst, "arp_rate", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 4, "an upward flick should step Arp Rate (%s)", v);
        setp(inst, "stick_y", "0"); render(inst, out, 4); setp(inst, "arp_rate", "5"); setp(inst, "stick_y", "1"); render(inst, out, 4);
        rt_enter(); api->get_param(inst, "arp_rate", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 5, "Arp Rate must clamp at the fastest value (%s)", v);
        setp(inst, "stick_y", "0"); setp(inst, "stick_x", "0"); render(inst, out, 4);
        setp(inst, "stick_x_to", "0"); setp(inst, "stick_y_to", "1"); setp(inst, "arp_mode", "0"); setp(inst, "arp_rate", "3");
        setp(inst, "preset", "0");
    }
    /* 2n. FX bank: knobs 1-4 (touch notes 0-3). Touch applies the assigned effect at its level; a double tap steps the effect type */
    {
        char v[32];
        #define TOUCH_ON(k) midi_src(inst, 0x90, (k), 127, MOVE_MIDI_SOURCE_TOUCH)
        #define TOUCH_OFF(k) midi_src(inst, 0x80, (k), 0, MOVE_MIDI_SOURCE_TOUCH)
        #define GETP(key) (rt_enter(), api->get_param(inst, (key), v, sizeof v), rt_leave(), atoi(v))
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "drift", "0"); setp(inst, "fx_scope", "1");   /* Any Page: no screen is polled in this test */
        for (int k = 1; k <= 4; k++) { char kk[32]; snprintf(kk, sizeof kk, "fx%d_type", k); setp(inst, kk, "0"); snprintf(kk, sizeof kk, "fx%d_lvl", k); setp(inst, kk, "0.5"); }
        render(inst, out, 200);
        CHECK(GETP("fx1_type") == 0, "FX slots start Off");
        TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 10);       /* one short tap: nothing happens */
        CHECK(GETP("fx1_type") == 0, "a single tap must not change the effect");
        render(inst, out, 200);                                                         /* partner tap too late: still just a tap */
        TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 4);
        CHECK(GETP("fx1_type") == 0, "two taps far apart are not a double tap");
        render(inst, out, 200);
        TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 25); TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 5);
        CHECK(GETP("fx1_type") == 1, "a double tap steps the effect type (%s)", v);
        TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 25); TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 5);
        CHECK(GETP("fx1_type") == 2, "the next double tap steps again (%s)", v);
        render(inst, out, 200);
        TOUCH_ON(0); render(inst, out, 80); TOUCH_OFF(0); render(inst, out, 10); TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 5);
        CHECK(GETP("fx1_type") == 2, "a long hold followed by a tap is not a double tap");
        render(inst, out, 200);
        TOUCH_ON(1); render(inst, out, 4); TOUCH_OFF(1); render(inst, out, 25); TOUCH_ON(1); render(inst, out, 4); TOUCH_OFF(1); render(inst, out, 5);
        CHECK(GETP("fx1_type") == 2 && GETP("fx2_type") == 1, "each knob has its own effect (%d %d)", GETP("fx1_type"), GETP("fx2_type"));
        TOUCH_ON(5); render(inst, out, 4); TOUCH_OFF(5); render(inst, out, 25); TOUCH_ON(5); render(inst, out, 4); TOUCH_OFF(5); render(inst, out, 5);
        CHECK(GETP("fx1_type") == 2, "knobs outside the bank do nothing");
        render(inst, out, 200);
        /* the second tap of a double tap is silent even if held; the level persists across presets */
        setp(inst, "fx1_type", "2"); setp(inst, "fx1_lvl", "1"); setp(inst, "fx2_type", "0"); setp(inst, "cutoff", "0.3"); setp(inst, "resonance", "0"); setp(inst, "flt_fm1", "0");
        midi(inst, 0x90, 48, 100); render(inst, out, 60);
        double base = rms_win(out, 30 * FRAMES, 20 * FRAMES);
        TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 25); TOUCH_ON(0); render(inst, out, 80);   /* double tap, second touch held */
        double sup = rms_win(out, 50 * FRAMES, 20 * FRAMES);
        TOUCH_OFF(0); render(inst, out, 20);
        CHECK(fabs(sup / base - 1.0) < 0.1, "the held second tap of a double tap must stay silent (%.4f vs %.4f)", sup, base);
        CHECK(GETP("fx1_type") == 3, "that double tap moved Filter Open to the next type (%s)", v);
        setp(inst, "fx1_type", "2");
        TOUCH_ON(0); render(inst, out, 10); double early = rms_win(out, 2 * FRAMES, 4 * FRAMES);
        render(inst, out, 80); double open = rms_win(out, 50 * FRAMES, 20 * FRAMES);
        printf("fx bank filter open: untouched %.4f, early %.4f, held %.4f\n", base, early, open);
        CHECK(fabs(early / base - 1.0) < 0.15, "the effect waits for the tap delay (%.4f vs %.4f)", early, base);
        CHECK(open > 1.3 * base, "Filter Open at full level should brighten the sound (%.4f vs %.4f)", open, base);
        TOUCH_OFF(0); render(inst, out, 100); double back = rms_win(out, 60 * FRAMES, 20 * FRAMES);
        CHECK(fabs(back / base - 1.0) < 0.1, "releasing the knob must restore the sound (%.4f vs %.4f)", back, base);
        setp(inst, "fx1_lvl", "0.3"); TOUCH_ON(0); render(inst, out, 80); double part = rms_win(out, 50 * FRAMES, 20 * FRAMES); TOUCH_OFF(0); render(inst, out, 100);
        CHECK(part > base * 1.02 && part < open * 0.98, "a lower level gives a smaller effect (%.4f < %.4f < %.4f)", base, part, open);
        setp(inst, "preset", "8");
        CHECK(GETP("fx1_type") == 2, "presets must not touch the FX bank (type)");
        CHECK(fabs(atof(v) - 2.0) < 0.5, "type readback");
        rt_enter(); api->get_param(inst, "fx1_lvl", v, sizeof v); rt_leave();
        CHECK(fabs(atof(v) - 0.3) < 0.01, "presets must not touch the FX bank (level %s)", v);
        setp(inst, "preset", "0");
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* every effect type: finite, audible while touched, back to normal on release; Hold switches Hold on and restores it */
        for (int ty = 1; ty < 15; ty++) {
            char tv[8]; snprintf(tv, sizeof tv, "%d", ty);
            setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "fx1_type", tv); setp(inst, "fx1_lvl", "0.8");
            if (ty == 13) { setp(inst, "arp_mode", "1"); setp(inst, "arp_gate", "1"); }
            midi(inst, 0x90, 48, 100); if (ty == 13) midi(inst, 0x90, 52, 100); render(inst, out, 60);
            double b0 = rms_win(out, 40 * FRAMES, 20 * FRAMES);
            TOUCH_ON(0); render(inst, out, 80);
            metrics_t mm = measure(out, 80 * FRAMES);
            if (ty == 14) CHECK(GETP("hold") == 1, "the Hold effect should switch Hold on while touched");
            CHECK(isfinite(mm.rms) && mm.peak <= 1.0 && mm.peak > 0.005, "FX type %d must stay finite and audible (rms %.3f peak %.3f)", ty, mm.rms, mm.peak);
            TOUCH_OFF(0); render(inst, out, 120);
            if (ty == 14) CHECK(GETP("hold") == 0, "releasing should restore Hold");
            double b1 = rms_win(out, 80 * FRAMES, 20 * FRAMES);
            if (ty != 13) CHECK(fabs(b1 / b0 - 1.0) < 0.2 || ty == 7 || ty == 8, "FX type %d should restore the sound on release (%.4f vs %.4f)", ty, b0, b1);
            midi(inst, 0x80, 48, 0); midi(inst, 0x80, 52, 0); setp(inst, "arp_mode", "0"); drain(inst, out);
        }
        /* Gate Chop modulates the level; Pitch Rise raises pitch */
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "osc1_wave", "2"); setp(inst, "cutoff", "0.8"); setp(inst, "drift", "0"); setp(inst, "vibrato_depth", "0");
        setp(inst, "fx1_type", "11"); setp(inst, "fx1_lvl", "1");
        midi(inst, 0x90, 48, 100); render(inst, out, 40); TOUCH_ON(0); render(inst, out, 100);
        double lo = 1e9, hi = 0; for (int w = 0; w < 40; w++) { double r = rms_win(out, 20 * FRAMES + w * 512, 512); if (r < lo) lo = r; if (r > hi) hi = r; }
        printf("gate chop: window rms %.4f .. %.4f\n", lo, hi);
        CHECK(hi > 0.05 && lo < 0.3 * hi, "Gate Chop should gate the level (%.4f..%.4f)", lo, hi);
        TOUCH_OFF(0); render(inst, out, 60);
        setp(inst, "fx1_type", "7"); setp(inst, "fx1_lvl", "1");
        double p0 = pitch_hz(out, 20 * FRAMES, 30 * FRAMES);
        TOUCH_ON(0); render(inst, out, 120); double p1 = pitch_hz(out, 90 * FRAMES, 30 * FRAMES);
        printf("pitch rise: %.1f -> %.1f Hz\n", p0, p1);
        CHECK(p1 / p0 > 1.85 && p1 / p0 < 2.15, "Pitch Rise at full level is an octave (%.1f -> %.1f)", p0, p1);
        TOUCH_OFF(0); midi(inst, 0x80, 48, 0); drain(inst, out);
        setp(inst, "fx1_type", "0"); setp(inst, "preset", "0");
        /* FX Touch = FX Page Only (the default): the bank acts only while the host is reading its keys, i.e. while its page is showing */
        setp(inst, "fx_scope", "0"); setp(inst, "fx1_type", "2"); setp(inst, "fx1_lvl", "1");
        render(inst, out, 400);                                                         /* no FX key read for a while: another page is showing */
        #define DTAP0() do { TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 4); TOUCH_ON(0); render(inst, out, 4); TOUCH_OFF(0); render(inst, out, 10); } while (0)
        DTAP0();
        { char w[32]; rt_enter(); api->get_param(inst, "cutoff", w, sizeof w); rt_leave(); }   /* reading other pages' keys does not count */
        CHECK(GETP("fx1_type") == 2, "a double tap on another page must not change the effect");
        render(inst, out, 400);
        TOUCH_ON(0); render(inst, out, 120); TOUCH_OFF(0);
        CHECK(GETP("fx1_lvl") == 1 && GETP("fx1_type") == 2, "touching a knob on another page leaves the bank alone");
        render(inst, out, 20);
        DTAP0();                                                                        /* the FX Bank page is showing (its keys were just read) */
        CHECK(GETP("fx1_type") == 3, "a double tap with the FX Bank page showing steps the effect (%d)", GETP("fx1_type"));
        render(inst, out, 400);                                                         /* the page is left: reads stop */
        DTAP0();
        CHECK(GETP("fx1_type") == 3, "after leaving the page the double tap does nothing (%d)", GETP("fx1_type"));
        setp(inst, "fx_scope", "1"); setp(inst, "preset", "5");
        CHECK(GETP("fx_scope") == 1, "presets must not touch FX Touch");
        setp(inst, "fx_scope", "0"); setp(inst, "fx1_type", "0"); setp(inst, "preset", "0");
        #undef DTAP0
        #undef TOUCH_ON
        #undef TOUCH_OFF
        #undef GETP
    }
    /* 2o. audio path: knob smoothing, idle snap, noise colour, preset category jump */
    {
        char v[32];
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "drift", "0"); setp(inst, "resonance", "0"); setp(inst, "flt_fm1", "0");
        setp(inst, "cutoff", "0.15"); midi(inst, 0x90, 60, 100); render(inst, out, 60);
        double lo = rms_win(out, 30 * FRAMES, 20 * FRAMES);
        setp(inst, "cutoff", "0.9"); render(inst, out, 60);
        double first = rms_win(out, 0, FRAMES), later = rms_win(out, 50 * FRAMES, 10 * FRAMES);
        printf("cutoff jump: before %.4f, first block %.4f, settled %.4f\n", lo, first, later);
        CHECK(later > 1.5 * lo, "opening the cutoff should brighten the sound (%.4f -> %.4f)", lo, later);
        CHECK(first < lo + 0.6 * (later - lo), "a stepped knob must glide, not jump (first block %.4f, from %.4f to %.4f)", first, lo, later);
        midi(inst, 0x80, 60, 0); drain(inst, out);
        setp(inst, "cutoff", "0.15"); midi(inst, 0x90, 60, 100); render(inst, out, 40); midi(inst, 0x80, 60, 0); drain(inst, out);   /* last heard dark */
        setp(inst, "cutoff", "0.9"); midi(inst, 0x90, 60, 100); render(inst, out, 60);                                              /* knob moved while silent */
        double f2 = rms_win(out, 0, FRAMES), s2 = rms_win(out, 50 * FRAMES, 10 * FRAMES);
        CHECK(f2 > 0.6 * s2, "a note after an idle knob move must start at the new value, not glide from the old one (%.4f vs %.4f)", f2, s2);
        midi(inst, 0x80, 60, 0); drain(inst, out);
        /* Noise Color: white by default, darker (less high-frequency energy) as it rises */
        double hf[2];
        for (int c = 0; c < 2; c++) {
            setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "osc1_level", "0"); setp(inst, "osc2_level", "0"); setp(inst, "noise_level", "1");
            setp(inst, "cutoff", "1"); setp(inst, "drive", "0"); setp(inst, "noise_color", c ? "1" : "0");
            midi(inst, 0x90, 48, 100); render(inst, out, 80);
            double da = 0, ra = 0; for (int i = 20 * FRAMES + 1; i < 80 * FRAMES; i++) { double d = (out[2 * i] - out[2 * (i - 1)]) / 32768.0, r = out[2 * i] / 32768.0; da += d * d; ra += r * r; }
            hf[c] = sqrt(da / ra); midi(inst, 0x80, 48, 0); drain(inst, out);
        }
        printf("noise colour: first-difference ratio %.2f (white) -> %.2f (dark)\n", hf[0], hf[1]);
        CHECK(hf[1] < 0.5 * hf[0], "Noise Color should darken the noise (%.2f vs %.2f)", hf[1], hf[0]);
        /* Preset Category jumps to the first preset of that type, and follows the current preset */
        setp(inst, "preset_cat", "2");
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strncmp(v, "Lead", 4) == 0, "Preset Category 2 should land on the first Lead preset (%s)", v);
        setp(inst, "preset", "0");
        rt_enter(); api->get_param(inst, "preset_cat", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "Preset Category should follow the preset (%s)", v);
        setp(inst, "preset", "40");
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); char nm[48]; snprintf(nm, sizeof nm, "%s", v); api->get_param(inst, "preset_cat", v, sizeof v); rt_leave();
        const char *cats[9] = {"Init", "Bass", "Lead", "Drone", "Arp", "Para", "Chaos", "Horror", "Set"};
        CHECK(atoi(v) >= 0 && atoi(v) < 9 && strncmp(nm, cats[atoi(v)], strlen(cats[atoi(v)])) == 0, "Preset Bank matches the preset name (%s -> %s)", nm, v);
        /* Banks: every preset is in exactly one bank, banks are browsed by position, names are shown without the bank word, Program Change is per bank */
        {
            char b2[32]; rt_enter(); api->get_param(inst, "preset_count", b2, sizeof b2); rt_leave(); int total = atoi(b2), sum = 0;
            for (int bk = 0; bk < 9; bk++) {
                char kk[16]; snprintf(kk, sizeof kk, "%d", bk); setp(inst, "preset_cat", kk);
                rt_enter(); api->get_param(inst, "bank_count", b2, sizeof b2); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
                int cnt = atoi(b2); sum += cnt;
                CHECK(cnt > 0 && cnt <= 64, "bank %d (%s) has %d presets", bk, cats[bk], cnt);
                CHECK(strncmp(v, cats[bk], strlen(cats[bk])) == 0, "bank %d starts on a %s preset (%s)", bk, cats[bk], v);
                snprintf(kk, sizeof kk, "%d", cnt - 1); setp(inst, "bank_preset", kk);
                rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); api->get_param(inst, "bank_preset", b2, sizeof b2); rt_leave();
                CHECK(strncmp(v, cats[bk], strlen(cats[bk])) == 0 && atoi(b2) == cnt - 1, "last preset of bank %d stays in the bank (%s, pos %s)", bk, v, b2);
                setp(inst, "bank_preset", "999");   /* out of range clamps to the last one */
                rt_enter(); api->get_param(inst, "bank_preset", b2, sizeof b2); rt_leave();
                CHECK(atoi(b2) == cnt - 1, "bank_preset clamps (%s of %d)", b2, cnt);
            }
            CHECK(sum == total, "banks hold every preset once (%d vs %d)", sum, total);
            setp(inst, "preset_cat", "1"); setp(inst, "bank_preset", "1");
            rt_enter(); api->get_param(inst, "preset_name", v, sizeof v); api->get_param(inst, "preset_full", nm, sizeof nm); rt_leave();
            CHECK(strncmp(nm, "Bass ", 5) == 0 && strcmp(nm + 5, v) == 0, "browser name drops the bank word ('%s' vs '%s')", nm, v);
            setp(inst, "prog_chg", "1");
            midi(inst, 0xB0, 0, 3); midi(inst, 0xC0, 0, 0);   /* bank select 3 = Lead, program 0 */
            rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); api->get_param(inst, "preset_cat", b2, sizeof b2); rt_leave();
            CHECK(atoi(b2) == 2 && strncmp(v, "Lead ", 5) == 0, "Program Change with bank select 3 picks inside the Lead bank (%s)", v);
            midi(inst, 0xB0, 0, 0);
        }
        setp(inst, "preset", "0");
    }
    /* 2p. bank files: reload from the folder, export a bank, user banks behave like factory banks */
    {
        char v[64], path[700];
        #define BSTATUS() (rt_enter(), api->get_param(inst, "bank_status", v, sizeof v), rt_leave(), atoi(v))
        #define WAIT_STATUS(want) do { int tries_ = 0; while (BSTATUS() != (want) && tries_++ < 100) usleep(30000); } while (0)
        for (int tries = 0; tries < 100; tries++) { struct stat st; snprintf(path, sizeof path, "%s/banks", bankdir); if (stat(path, &st) == 0) break; usleep(30000); }
        WAIT_STATUS(3);
        rt_enter(); api->get_param(inst, "user_banks", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "an empty folder gives no user banks (%s)", v);
        snprintf(path, sizeof path, "%s/banks/Zed Test.mmbank", bankdir);
        FILE *f = fopen(path, "w");
        fprintf(f, "# comment\nmmbank 1\nbank Zed Bank\n\npreset Loud Saw\ncutoff 5\nresonance 0.5\nfx1_type 7\nstick_x 1\nnot_a_param 3\nosc2_wave 1\n\npreset Second\nosc1_wave 2\n");
        fclose(f);
        snprintf(path, sizeof path, "%s/banks/Alpha.mmbank", bankdir);
        f = fopen(path, "w"); fprintf(f, "preset First\ncutoff 0.3\n"); fclose(f);   /* no bank line: the file name is the bank name */
        snprintf(path, sizeof path, "%s/banks/notes.txt", bankdir);
        f = fopen(path, "w"); fprintf(f, "preset Ignored\n"); fclose(f);
        setp(inst, "bank_io", "2"); WAIT_STATUS(3);
        rt_enter(); api->get_param(inst, "user_banks", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 2, "two .mmbank files make two user banks (%s)", v);
        rt_enter(); api->get_param(inst, "bank_io", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "Bank Files returns to Idle (%s)", v);
        setp(inst, "fx1_type", "0");
        setp(inst, "preset_cat", "9");
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strcmp(v, "First") == 0, "User 1 is the first file by name (%s)", v);
        rt_enter(); api->get_param(inst, "preset_cat", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 9, "the Bank knob lands on User 1 (%s)", v);
        setp(inst, "preset_cat", "10");
        rt_enter(); api->get_param(inst, "bank_count", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 2, "User 2 holds two presets (%s)", v);
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strcmp(v, "Loud Saw") == 0, "User 2 starts on its first preset (%s)", v);
        rt_enter(); api->get_param(inst, "cutoff", v, sizeof v); rt_leave();
        CHECK(atof(v) <= 1.0001 && atof(v) > 0.99, "values are clamped to their range (cutoff %s)", v);
        rt_enter(); api->get_param(inst, "fx1_type", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "a bank file cannot change the FX bank (%s)", v);
        rt_enter(); api->get_param(inst, "stick_x", v, sizeof v); rt_leave();
        CHECK(atof(v) == 0.0, "a bank file cannot move the stick (%s)", v);
        setp(inst, "bank_preset", "1");
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strcmp(v, "Second") == 0, "bank_preset browses inside the user bank (%s)", v);
        setp(inst, "bank_preset", "99");
        rt_enter(); api->get_param(inst, "bank_preset", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 1, "bank_preset clamps inside a user bank (%s)", v);
        setp(inst, "preset_cat", "16");   /* an empty slot is ignored */
        rt_enter(); api->get_param(inst, "preset_cat", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 10, "an empty user bank cannot be selected (%s)", v);
        setp(inst, "bank_preset", "0"); setp(inst, "env2_s", "1");
        midi(inst, 0x90, 48, 100); render(inst, out, 100); metrics_t mu = measure(out, 100 * FRAMES); midi(inst, 0x80, 48, 0); drain(inst, out);
        CHECK(mu.rms > 0.01 && mu.peak < 1.0, "a user preset plays (rms %.3f)", mu.rms);
        /* state keeps working with a user preset code */
        char st[8192]; rt_enter(); int sl = api->get_param(inst, "state", st, sizeof st); rt_leave();
        CHECK(sl > 0 && strstr(st, "\"preset\":10") != NULL, "state records the user preset code");
        setp(inst, "preset", "0");
        rt_enter(); api->set_param(inst, "state", st); rt_leave();
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strcmp(v, "Loud Saw") == 0, "state restores the user preset (%s)", v);
        /* export a factory bank, move the file into the folder, and it plays like the original */
        setp(inst, "preset_cat", "1");
        rt_enter(); api->get_param(inst, "bank_count", v, sizeof v); rt_leave(); int nbass = atoi(v);
        setp(inst, "bank_io", "1"); WAIT_STATUS(2);
        CHECK(BSTATUS() == 2, "exporting a bank reports Exported (%d)", BSTATUS());
        snprintf(path, sizeof path, "%s/exports/Bass.mmbank", bankdir);
        f = fopen(path, "r"); int presets_in_file = 0; char ln[256];
        if (f) { while (fgets(ln, sizeof ln, f)) if (!strncmp(ln, "preset ", 7)) presets_in_file++; fclose(f); }
        CHECK(f != NULL && presets_in_file == nbass, "export wrote every preset of the bank (%d of %d)", presets_in_file, nbass);
        { char cmd[1500]; snprintf(cmd, sizeof cmd, "cp '%s/exports/Bass.mmbank' '%s/banks/Bass Copy.mmbank'", bankdir, bankdir); if (system(cmd)) {} }
        setp(inst, "bank_io", "2"); WAIT_STATUS(3);
        rt_enter(); api->get_param(inst, "user_banks", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 3, "the exported file loads as a third user bank (%s)", v);
        double rm[2];
        for (int which = 0; which < 2; which++) {
            setp(inst, "preset_cat", which ? "10" : "1");   /* Bass, then User 2 = Bass Copy */
            midi(inst, 0x90, 40, 100); render(inst, out, 120); rm[which] = measure(out, 120 * FRAMES).rms; midi(inst, 0x80, 40, 0); drain(inst, out);
        }
        printf("bank files: Bass first preset rms %.4f, exported copy %.4f\n", rm[0], rm[1]);
        CHECK(rm[0] > 0.005 && fabs(rm[0] - rm[1]) < 0.02 * rm[0], "an exported bank sounds like the original (%.4f vs %.4f)", rm[0], rm[1]);
        snprintf(path, sizeof path, "%s/banks/Bass Copy.mmbank", bankdir); remove(path);
        snprintf(path, sizeof path, "%s/banks/Zed Test.mmbank", bankdir); remove(path);
        snprintf(path, sizeof path, "%s/banks/Alpha.mmbank", bankdir); remove(path);
        setp(inst, "bank_io", "2"); WAIT_STATUS(3);
        rt_enter(); api->get_param(inst, "user_banks", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 0, "removing the files empties the user banks on reload (%s)", v);
        { char cmd[1500]; snprintf(cmd, sizeof cmd, "cp banks/Example.mmbank '%s/banks/'", bankdir); if (system(cmd)) {} }
        setp(inst, "bank_io", "2"); WAIT_STATUS(3);
        setp(inst, "preset_cat", "9");
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); api->get_param(inst, "bank_count", path, 16); rt_leave();
        CHECK(strcmp(v, "Fat Square Lead") == 0 && atoi(path) == 2, "the shipped Example.mmbank loads (%s, %s presets)", v, path);
        snprintf(path, sizeof path, "%s/banks/Example.mmbank", bankdir); remove(path); setp(inst, "bank_io", "2"); WAIT_STATUS(3);
        setp(inst, "preset", "0");
        #undef BSTATUS
        #undef WAIT_STATUS
    }
    /* version: the number shown in the module must match module.json, release.json and the help text */
    {
        static char cp[65536], mj[8192], hj[65536]; rt_enter(); int cl = api->get_param(inst, "chain_params", cp, sizeof cp); rt_leave();
        char want[64]; snprintf(want, sizeof want, "\"options\":[\"%s\"]", MM_VERSION_TEST);
        CHECK(cl > 0 && strstr(cp, want) != NULL, "chain_params shows the version %s", MM_VERSION_TEST);
        FILE *f = fopen("src/module.json", "r"); size_t n = f ? fread(mj, 1, sizeof mj - 1, f) : 0; if (f) fclose(f); mj[n] = 0;
        snprintf(want, sizeof want, "\"version\": \"%s\"", MM_VERSION_TEST);
        CHECK(n > 0 && strstr(mj, want) != NULL, "src/module.json has version %s", MM_VERSION_TEST);
        f = fopen("release.json", "r"); n = f ? fread(mj, 1, sizeof mj - 1, f) : 0; if (f) fclose(f); mj[n] = 0;
        CHECK(n > 0 && strstr(mj, want) != NULL, "release.json has version %s", MM_VERSION_TEST);
        f = fopen("src/help.json", "r"); n = f ? fread(hj, 1, sizeof hj - 1, f) : 0; if (f) fclose(f); hj[n] = 0;
        CHECK(n > 0 && strstr(hj, MM_VERSION_TEST) != NULL, "help text mentions version %s", MM_VERSION_TEST);
        setp(inst, "version", "0"); rt_enter(); char vv[16]; api->get_param(inst, "version", vv, sizeof vv); rt_leave();
        CHECK(atoi(vv) == 0, "version is read-only");
    }
    /* false polyphony: a chord on Horror Calliope is cycled through quickly (tied arp), so it carries several pitches; a single note stays steady */
    {
        char v[64], nm[48]; int idx = -1, cnt;
        rt_enter(); api->get_param(inst, "preset_count", v, sizeof v); rt_leave(); cnt = atoi(v);
        for (int i = 0; i < cnt && idx < 0; i++) { snprintf(v, sizeof v, "%d", i); setp(inst, "preset", v); rt_enter(); api->get_param(inst, "preset_full", nm, sizeof nm); rt_leave(); if (!strcmp(nm, "Horror Calliope")) idx = i; }
        CHECK(idx >= 0, "Horror Calliope exists");
        if (idx >= 0) {
            double lo = 1e9, hi = 0;
            midi(inst, 0x90, 48, 100); midi(inst, 0x90, 52, 100); midi(inst, 0x90, 55, 100); render(inst, out, 120);
            for (int w = 0; w < 5; w++) { double f = pitch_hz(out, 8000 + w * 1200, 1200); if (f > 20 && f < lo) lo = f; if (f > hi) hi = f; }
            printf("calliope chord: pitch estimates %.1f .. %.1f Hz\n", lo, hi);
            CHECK(hi > 1.15 * lo, "a chord on Horror Calliope cycles several pitches (%.1f..%.1f)", lo, hi);
            midi(inst, 0x80, 48, 0); midi(inst, 0x80, 52, 0); midi(inst, 0x80, 55, 0); drain(inst, out);
            double rmin = 1e9, rmax = 0;
            midi(inst, 0x90, 55, 100); render(inst, out, 120);
            for (int w = 0; w < 5; w++) { double r = measure(out + 2 * (8000 + w * 1200), 1200).rms; if (r < rmin) rmin = r; if (r > rmax) rmax = r; }
            CHECK(rmin > 0.005 && rmax < 1.5 * rmin, "a single note on Horror Calliope stays steady (level %.3f..%.3f)", rmin, rmax);
            midi(inst, 0x80, 55, 0); drain(inst, out);
        }
        setp(inst, "preset", "0");
    }
    /* matrix Via: a second source scales the slot amount (LFO -> Amp, scaled by the Mod Wheel) */
    {
        double spread[3];
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "flt_fm1", "0"); setp(inst, "lfo_rate", "0.75"); setp(inst, "lfo_wave", "0");
        setp(inst, "m1_src", "1"); setp(inst, "m1_dst", "12"); setp(inst, "m1_amt", "0.9");
        for (int c = 0; c < 3; c++) {   /* 0: Via = Mod Wheel with the wheel at 0, 1: wheel up, 2: Via Off */
            setp(inst, "m1_via", c == 2 ? "0" : "5"); midi(inst, 0xB0, 1, c == 1 ? 127 : 0);
            midi(inst, 0x90, 48, 100); render(inst, out, 40);
            double lo = 1e9, hi = 0;
            for (int w = 0; w < 12; w++) { render(inst, out, 10); double r = measure(out, 10 * FRAMES).rms; if (r < lo) lo = r; if (r > hi) hi = r; }
            spread[c] = lo > 1e-6 ? hi / lo : 1e6;
            midi(inst, 0x80, 48, 0); drain(inst, out);
        }
        printf("matrix via: level spread wheel at 0: %.2f, wheel up: %.2f, via off: %.2f\n", spread[0], spread[1], spread[2]);
        CHECK(spread[0] < 1.15, "Via with the source at 0 silences the slot (spread %.2f)", spread[0]);
        CHECK(spread[1] > 1.6, "Via with the source up lets the slot work (spread %.2f)", spread[1]);
        CHECK(spread[2] > 1.6, "Via Off leaves the slot unscaled (spread %.2f)", spread[2]);
        midi(inst, 0xB0, 1, 0); midi(inst, 0xB0, 121, 0);
        setp(inst, "m1_src", "0"); setp(inst, "m1_dst", "0"); setp(inst, "m1_amt", "0"); setp(inst, "m1_via", "0"); setp(inst, "preset", "0");
    }
    /* 2l. Osc 1 tuning, noise / osc 2 as matrix sources into FM, controller CCs, Program Change, RPN bend range */
    {
        char v[32];
        setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "osc1_wave", "2"); setp(inst, "resonance", "0"); setp(inst, "flt_fm1", "0");
        setp(inst, "cutoff", "0.5"); setp(inst, "drive", "0"); setp(inst, "drift", "0"); setp(inst, "vibrato_depth", "0");
        midi(inst, 0x90, 48, 100); render(inst, out, 100);
        double base = pitch_hz(out, 40 * FRAMES, 50 * FRAMES);
        setp(inst, "osc1_coarse", "12"); render(inst, out, 100);
        double up = pitch_hz(out, 40 * FRAMES, 50 * FRAMES);
        printf("osc1 coarse +12: ratio %.3f (expect 2)\n", up / base);
        CHECK(fabs(up / base - 2.0) < 0.1, "osc1 coarse +12 should double the pitch (%.3f)", up / base);
        setp(inst, "osc1_coarse", "0"); setp(inst, "osc1_fine", "50"); render(inst, out, 100);
        double fine = pitch_hz(out, 40 * FRAMES, 50 * FRAMES);
        CHECK(fabs(fine / base - 1.0293) < 0.03, "osc1 fine +50 cents ratio %.4f", fine / base);
        setp(inst, "osc1_fine", "0");
        /* noise into osc 1 FM roughens a clean triangle: many more zero crossings */
        setp(inst, "m1_src", "12"); setp(inst, "m1_dst", "16"); setp(inst, "m1_amt", "0.4"); render(inst, out, 100);
        double noisy = pitch_hz(out, 40 * FRAMES, 50 * FRAMES);
        printf("noise->osc1 FM: %.1f Hz vs %.1f Hz clean\n", noisy, base);
        CHECK(fabs(noisy - base) > base * 0.03 || noisy < 0.1, "noise -> Osc 1 FM should change the waveform (%.1f vs %.1f)", noisy, base);
        setp(inst, "m1_amt", "0"); midi(inst, 0x80, 48, 0); drain(inst, out);
        /* controller CCs: external only */
        setp(inst, "preset", "0");
        midi(inst, 0xB0, 74, 127);
        rt_enter(); api->get_param(inst, "cutoff", v, sizeof v); rt_leave();
        CHECK(atof(v) > 0.99, "CC74 should set cutoff (%s)", v);
        midi_src(inst, 0xB0, 71, 127, MOVE_MIDI_SOURCE_INTERNAL);
        rt_enter(); api->get_param(inst, "resonance", v, sizeof v); rt_leave();
        CHECK(atof(v) < 0.5, "CC71 from Move's own knobs must not remap resonance (%s)", v);
        midi(inst, 0xB0, 7, 64);
        rt_enter(); api->get_param(inst, "volume", v, sizeof v); rt_leave();
        CHECK(fabs(atof(v) - 0.5039) < 0.01, "CC7 should set volume (%s)", v);
        midi(inst, 0xB0, 101, 0); midi(inst, 0xB0, 100, 0); midi(inst, 0xB0, 6, 7);
        rt_enter(); api->get_param(inst, "bend_range", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 7, "RPN 0 should set the bend range (%s)", v);
        midi(inst, 0xB0, 6, 3);   /* RPN still selected: ok. now deselect (127,127) so a stray CC6 does nothing */
        midi(inst, 0xB0, 101, 127); midi(inst, 0xB0, 100, 127); midi(inst, 0xB0, 6, 11);
        rt_enter(); api->get_param(inst, "bend_range", v, sizeof v); rt_leave();
        CHECK(atoi(v) == 3, "CC6 without RPN 0 selected must be ignored (%s)", v);
        char pcb[16];
        setp(inst, "prog_chg", "0"); setp(inst, "preset", "0");
        midi(inst, 0xC0, 5, 0);
        rt_enter(); api->get_param(inst, "preset", v, sizeof v); api->get_param(inst, "pc_seen", pcb, sizeof pcb); rt_leave();
        CHECK(atoi(v) == 0, "Program Change must be ignored while Program Change = Ignore (%s)", v);
        CHECK(atoi(pcb) > 0, "ignored Program Changes are still counted (%s)", pcb);
        setp(inst, "prog_chg", "1");
        midi(inst, 0xC0, 5, 0);
        rt_enter(); api->get_param(inst, "preset", v, sizeof v); api->get_param(inst, "last_change", pcb, sizeof pcb); rt_leave();
        CHECK(atoi(v) == 5, "Program Change 5 should select preset 5 (%s)", v);
        CHECK(atoi(pcb) == 4, "last_change reports Prog Chg (%s)", pcb);
        midi(inst, 0xB0, 0, 2); midi(inst, 0xC0, 5, 0);
        rt_enter(); api->get_param(inst, "preset_full", v, sizeof v); rt_leave();
        CHECK(strncmp(v, "Bass ", 5) == 0, "bank select 2 (Bass) program 5 -> the sixth Bass preset (%s)", v);
        midi(inst, 0xB0, 0, 0);
        midi_src(inst, 0xC0, 9, 0, MOVE_MIDI_SOURCE_INTERNAL);
        rt_enter(); api->get_param(inst, "preset", v, sizeof v); rt_leave();
        CHECK(atoi(v) != 9, "Program Change from Move's own surface must be ignored (%s)", v);
        midi(inst, 0xB0, 121, 0); setp(inst, "preset", "0");
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
        for (int s = 1; s <= 19; s++) for (int d = 1; d <= 24; d++) for (int pol = 0; pol < 2; pol++) {
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
    /* 2m. matrix batch 2: Pitch Bend, Arp Step, Note Age, Smooth Random, Osc 1, Sustain Pedal sources; Grit, Glide, Vibrato Depth, LFO Depths, Env Amount targets */
    {
        #define CLEAN_TRI() do { setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "osc1_wave", "2"); setp(inst, "resonance", "0"); setp(inst, "flt_fm1", "0"); \
            setp(inst, "cutoff", "0.8"); setp(inst, "drive", "0"); setp(inst, "drift", "0"); setp(inst, "vibrato_depth", "0"); setp(inst, "bend_range", "0"); } while (0)
        /* Pitch Bend source: bend -> Amp at -1 silences the voice with the wheel up (Bend Range 0, so only the matrix sees it) */
        CLEAN_TRI(); setp(inst, "m1_src", "14"); setp(inst, "m1_dst", "12"); setp(inst, "m1_amt", "-1");
        midi(inst, 0x90, 48, 100); render(inst, out, 100);
        double b0 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        midi(inst, 0xE0, 127, 127); render(inst, out, 100);
        double b1 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        midi(inst, 0xE0, 0, 64); render(inst, out, 100);
        double b2 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        printf("pitch bend source -> amp: centre %.4f, up %.5f, back %.4f\n", b0, b1, b2);
        CHECK(b0 > 0.05 && b1 < 0.002 && b2 > 0.05, "Pitch Bend source (%.4f %.5f %.4f)", b0, b1, b2);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* Sustain Pedal source: slewed 0..1 */
        setp(inst, "m1_src", "19");
        midi(inst, 0x90, 48, 100); render(inst, out, 100);
        double p0 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        midi(inst, 0xB0, 64, 127); render(inst, out, 100);
        double p1 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        midi(inst, 0xB0, 64, 0); render(inst, out, 100);
        double p2 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        printf("pedal source -> amp: up %.4f, down %.5f, up %.4f\n", p0, p1, p2);
        CHECK(p0 > 0.05 && p1 < 0.002 && p2 > 0.05, "Sustain Pedal source (%.4f %.5f %.4f)", p0, p1, p2);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* Note Age: a slow 0..1 ramp from every new note (5 s); it restarts with the next note */
        CLEAN_TRI(); setp(inst, "m1_src", "16"); setp(inst, "m1_dst", "1"); setp(inst, "m1_amt", "1");
        midi(inst, 0x90, 48, 100); render(inst, out, 60);
        double ae = pitch_hz(out, 20 * FRAMES, 30 * FRAMES);
        render(inst, out, 600); render(inst, out, 600);
        double al = pitch_hz(out, 560 * FRAMES, 30 * FRAMES);
        midi(inst, 0x80, 48, 0); drain(inst, out); midi(inst, 0x90, 48, 100); render(inst, out, 60);
        double ar = pitch_hz(out, 20 * FRAMES, 30 * FRAMES);
        printf("note age -> pitch: early %.1f Hz, after 3.7 s %.1f Hz (ratio %.2f), new note %.1f Hz\n", ae, al, al / ae, ar);
        CHECK(al / ae > 1.4 && ar / ae < 1.15, "Note Age should ramp up and restart per note (%.1f %.1f %.1f)", ae, al, ar);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* Smooth Random: wanders, but never jumps */
        CLEAN_TRI(); setp(inst, "m1_src", "17"); setp(inst, "m1_dst", "1"); setp(inst, "m1_amt", "0.4");
        midi(inst, 0x90, 72, 100); render(inst, out, 600);
        double hmn = 1e9, hmx = 0, hjump = 1.0, hprev = 0;
        for (int w = 0; w < 28; w++) {
            double h = pitch_hz(out, 2000 + w * 2400, 2400);
            if (h < hmn) hmn = h;
            if (h > hmx) hmx = h;
            if (hprev > 0) { double r = h > hprev ? h / hprev : hprev / h; if (r > hjump) hjump = r; }
            hprev = h;
        }
        printf("smooth random -> pitch: %.1f .. %.1f Hz, biggest step %.3f\n", hmn, hmx, hjump);
        CHECK(hmx / hmn > 1.08 && hjump < 1.15, "Smooth Random should wander smoothly (%.1f..%.1f, step %.3f)", hmn, hmx, hjump);
        midi(inst, 0x80, 72, 0); drain(inst, out);
        /* Arp Step: 0 on the first arp note, 1 on the last; Amp at -1 makes the pattern fade across the steps */
        CLEAN_TRI(); setp(inst, "arp_mode", "1"); setp(inst, "arp_rate", "2"); setp(inst, "arp_gate", "1"); setp(inst, "arp_octaves", "1");
        midi(inst, 0x90, 48, 100); midi(inst, 0x90, 52, 100); midi(inst, 0x90, 55, 100);
        for (int pass = 0; pass < 2; pass++) {
            setp(inst, "m1_src", "15"); setp(inst, "m1_dst", "12"); setp(inst, "m1_amt", pass ? "0" : "-1");
            render(inst, out, 300);
            double lo = 1e9, hi = 0; for (int w = 0; w < 30; w++) { double r = rms_win(out, 6000 + w * 1024, 1024); if (r < lo) lo = r; if (r > hi) hi = r; }
            printf("arp step %s: window rms %.4f .. %.4f\n", pass ? "control" : "-> amp", lo, hi);
            if (!pass) CHECK(hi > 0.05 && lo < 0.4 * hi, "Arp Step should change level across the pattern (%.4f..%.4f)", lo, hi);
            else CHECK(lo > 0.5 * hi, "no matrix: arp level should be steady (%.4f..%.4f)", lo, hi);
        }
        midi(inst, 0x80, 48, 0); midi(inst, 0x80, 52, 0); midi(inst, 0x80, 55, 0); setp(inst, "arp_mode", "0"); drain(inst, out);
        /* Osc 1 as a source (even with its Level at 0): Osc 1 -> Osc 2 Level turns a silent patch into sound */
        CLEAN_TRI(); setp(inst, "osc1_level", "0"); setp(inst, "osc2_level", "0"); setp(inst, "osc2_wave", "1"); setp(inst, "m1_src", "18"); setp(inst, "m1_dst", "6"); setp(inst, "m1_amt", "0");
        midi(inst, 0x90, 48, 100); render(inst, out, 100);
        double o0 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        setp(inst, "m1_amt", "1"); render(inst, out, 100);
        double o1 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        printf("osc1 source -> osc2 level: %.5f silent, %.4f with the slot\n", o0, o1);
        CHECK(o0 < 0.002 && o1 > 0.02, "Osc 1 as a source should modulate (%.5f vs %.4f)", o0, o1);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* Grit target: Velocity -> Grit at +1 equals Stick X -> Grit at +1 */
        CLEAN_TRI(); setp(inst, "osc1_wave", "1"); setp(inst, "drive", "0.3"); setp(inst, "stick_x_to", "3"); setp(inst, "stick_x", "1");
        midi(inst, 0x90, 48, 127); render(inst, out, 100);
        double gs0 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        setp(inst, "stick_x", "0"); setp(inst, "stick_x_to", "0"); setp(inst, "m1_src", "4"); setp(inst, "m1_dst", "18"); setp(inst, "m1_amt", "1");
        midi(inst, 0x90, 48, 127); render(inst, out, 100);
        double gs1 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        setp(inst, "m1_amt", "0"); render(inst, out, 100);
        double gs2 = rms_win(out, 60 * FRAMES, 30 * FRAMES);
        printf("grit target: stick %.4f, matrix %.4f, none %.4f\n", gs0, gs1, gs2);
        CHECK(fabs(gs1 / gs0 - 1.0) < 0.08 && fabs(gs2 / gs0 - 1.0) > 0.1, "Grit target should match the stick Grit (%.4f vs %.4f, off %.4f)", gs1, gs0, gs2);
        midi(inst, 0x80, 48, 0); drain(inst, out);
        /* Glide Time target: with no Glide knob, Velocity -> Glide makes the second note slide */
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "glide", "0"); setp(inst, "m1_src", "4"); setp(inst, "m1_dst", "19"); setp(inst, "m1_amt", pass ? "0" : "1");
            midi(inst, 0x90, 48, 127); render(inst, out, 40); midi(inst, 0x90, 60, 127); render(inst, out, 120);
            double gh = pitch_hz(out, 90 * FRAMES, 30 * FRAMES);
            printf("glide target %s: %.1f Hz (note is 261.6)\n", pass ? "off" : "on", gh);
            if (!pass) CHECK(gh > 150 && gh < 225, "Glide Time target should slide (%.1f Hz)", gh);
            else CHECK(gh > 245, "no Glide Time slot: the note should be there (%.1f Hz)", gh);
            midi(inst, 0x80, 48, 0); midi(inst, 0x80, 60, 0); drain(inst, out);
        }
        /* Vibrato Depth target: Velocity -> Vibrato Depth gives a pitch wobble that the plain patch does not have */
        double vs[2];
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "m1_src", "4"); setp(inst, "m1_dst", "20"); setp(inst, "m1_amt", pass ? "0" : "1");
            midi(inst, 0x90, 84, 127); render(inst, out, 300);
            double lo = 1e9, hi = 0; for (int w = 0; w < 14; w++) { double h = pitch_hz(out, 3000 + w * 2500, 2500); if (h < lo) lo = h; if (h > hi) hi = h; }
            vs[pass] = hi / lo; midi(inst, 0x80, 84, 0); drain(inst, out);
        }
        printf("vibrato depth target: spread %.3f (off %.3f)\n", vs[0], vs[1]);
        CHECK(vs[0] > 1.07 && vs[1] < 1.04, "Vibrato Depth target (%.3f vs %.3f)", vs[0], vs[1]);
        /* LFO Depth / LFO 2 Depth: an LFO on pitch is silent with the wheel down and full with the wheel up */
        for (int which = 0; which < 2; which++) {
            double sp[2];
            for (int wh = 0; wh < 2; wh++) {
                CLEAN_TRI(); setp(inst, "lfo_wave", "1"); setp(inst, "lfo_rate", "0.45"); setp(inst, "lfo2_wave", "1"); setp(inst, "lfo2_rate", "0.45");
                setp(inst, "m1_src", which ? "11" : "1"); setp(inst, "m1_dst", "1"); setp(inst, "m1_amt", "0.5");
                setp(inst, "m2_src", "5"); setp(inst, "m2_dst", which ? "22" : "21"); setp(inst, "m2_amt", "1");
                midi(inst, 0xB0, 1, wh ? 127 : 0); midi(inst, 0x90, 72, 100); render(inst, out, 600);
                double lo = 1e9, hi = 0; for (int w = 0; w < 28; w++) { double h = pitch_hz(out, 2000 + w * 2400, 2400); if (h < lo) lo = h; if (h > hi) hi = h; }
                sp[wh] = hi / lo; midi(inst, 0xB0, 1, 0); midi(inst, 0x80, 72, 0); drain(inst, out);
            }
            printf("LFO %d depth: wheel down spread %.3f, wheel up %.3f\n", which + 1, sp[0], sp[1]);
            CHECK(sp[0] < 1.06 && sp[1] > 1.25, "LFO %d Depth (%.3f / %.3f)", which + 1, sp[0], sp[1]);
        }
        /* Env Amount target: Velocity -> Env Amount opens the filter further with the filter envelope */
        double ea[2];
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "osc1_wave", "1"); setp(inst, "cutoff", "0.3"); setp(inst, "env1_a", "0"); setp(inst, "env1_s", "1");
            setp(inst, "m1_src", "4"); setp(inst, "m1_dst", "23"); setp(inst, "m1_amt", pass ? "0" : "1");
            midi(inst, 0x90, 48, 127); render(inst, out, 150); ea[pass] = rms_win(out, 110 * FRAMES, 30 * FRAMES); midi(inst, 0x80, 48, 0); drain(inst, out);
        }
        printf("env amount target: rms %.4f vs %.4f\n", ea[0], ea[1]);
        CHECK(ea[0] > 1.3 * ea[1], "Env Amount target should open the filter (%.4f vs %.4f)", ea[0], ea[1]);
        /* Amp Env Attack target: the Mod Wheel at full stretches the attack to seconds, so the note starts far quieter */
        double at_[2];
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "env2_a", "0"); setp(inst, "m1_src", "5"); setp(inst, "m1_dst", "28"); setp(inst, "m1_amt", pass ? "0" : "1");
            midi(inst, 0xB0, 1, 127); midi(inst, 0x90, 48, 100); render(inst, out, 25); at_[pass] = rms_win(out, 10 * FRAMES, 14 * FRAMES);
            midi(inst, 0x80, 48, 0); midi(inst, 0xB0, 1, 0); drain(inst, out);
        }
        printf("env attack target: rms %.4f vs %.4f\n", at_[0], at_[1]);
        CHECK(at_[1] > 0.01 && at_[0] < 0.3 * at_[1], "Amp Env Attack target should slow the attack (%.4f vs %.4f)", at_[0], at_[1]);
        /* Osc 1 Pitch target moves only Osc 1 (+6 semitones at 0.5) */
        double pc_[2];
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "m1_src", "5"); setp(inst, "m1_dst", "25"); setp(inst, "m1_amt", "0.5");
            midi(inst, 0xB0, 1, pass ? 127 : 0); midi(inst, 0x90, 48, 100); render(inst, out, 80); pc_[pass] = pitch_hz(out, 30 * FRAMES, 40 * FRAMES);
            midi(inst, 0x80, 48, 0); midi(inst, 0xB0, 1, 0); drain(inst, out);
        }
        printf("osc 1 pitch target: %.1f Hz -> %.1f Hz (ratio %.3f, expect 1.414)\n", pc_[0], pc_[1], pc_[1] / pc_[0]);
        CHECK(fabs(pc_[1] / pc_[0] - 1.4142) < 0.06, "Osc 1 Pitch target ratio %.3f", pc_[1] / pc_[0]);
        /* LFO Retrigger: a decaying note with no sustain keeps re-striking while the LFO cycles */
        double rt_[2];
        for (int pass = 0; pass < 2; pass++) {
            CLEAN_TRI(); setp(inst, "env2_d", "0.3"); setp(inst, "env2_s", "0.02"); setp(inst, "lfo_wave", "1"); setp(inst, "lfo_rate", "0.75"); setp(inst, "lfo_gate", pass ? "1" : "0");
            midi(inst, 0x90, 48, 100); render(inst, out, 260); rt_[pass] = rms_win(out, 180 * FRAMES, 70 * FRAMES);
            midi(inst, 0x80, 48, 0); drain(inst, out);
        }
        printf("lfo retrigger: late rms %.4f off, %.4f on\n", rt_[0], rt_[1]);
        CHECK(rt_[1] > 3.0 * rt_[0] && rt_[1] > 0.01, "LFO Retrigger should keep striking the envelopes (%.4f vs %.4f)", rt_[1], rt_[0]);
        setp(inst, "lfo_gate", "0");
        /* Audio-rate LFO: a square LFO on the amp gates noise at ~200 Hz, which the slow range does not */
        double cov_[2];
        for (int pass = 0; pass < 2; pass++) {
            setp(inst, "preset", "0"); setp(inst, "env2_s", "1"); setp(inst, "osc1_level", "0"); setp(inst, "noise_level", "0.8"); setp(inst, "cutoff", "0.95"); setp(inst, "flt_fm1", "0");
            setp(inst, "lfo_wave", "1"); setp(inst, "lfo_rate", pass ? "0.5" : "0.2"); setp(inst, "lfo_sync", pass ? "8" : "0");
            setp(inst, "m1_src", "1"); setp(inst, "m1_dst", "12"); setp(inst, "m1_amt", "0.5");
            midi(inst, 0x90, 48, 100); cov_[pass] = 1e9;
            for (int rep = 0; rep < 2; rep++) {   /* two looks: a slow square LFO may flip once inside one of them */
                render(inst, out, 60);
                double sum = 0, sq = 0; int n = 80;
                for (int w = 0; w < n; w++) { double r = rms_win(out, 30 * FRAMES + w * 40, 40); sum += r; sq += r * r; }
                double mean = sum / n, var = sq / n - mean * mean, c = mean > 1e-6 ? sqrt(var > 0 ? var : 0) / mean : 0;
                if (c < cov_[pass]) cov_[pass] = c;
            }
            midi(inst, 0x80, 48, 0); drain(inst, out);
        }
        printf("audio-rate LFO: window level variation %.3f slow, %.3f audio\n", cov_[0], cov_[1]);
        CHECK(cov_[0] < 0.2 && cov_[1] > 0.35, "Audio range LFO gates at audio rate (%.3f slow, %.3f audio)", cov_[0], cov_[1]);
        setp(inst, "lfo_sync", "0");
        setp(inst, "m1_amt", "0"); setp(inst, "m2_amt", "0"); setp(inst, "preset", "0");
        #undef CLEAN_TRI
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
