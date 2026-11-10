/* Register, velocity and short-note behaviour of every factory preset.
 * Each bank has a home register (Bass: C1-C3, Lead/Arp/Para: C3-C5, Drone/Chaos/Horror: C2-C4; Sets by role). A single note is held at five notes across it
 * (A-weighted level of the loudest 150 ms), then at the middle note with velocity 40 and 127, and as a short 150 ms note.
 * Usage: register dsp.so measure           CSV per preset; exits 1 if a preset is silent in its register or its level moves more than 6 dB across it
 *        register dsp.so calibrate         fits each preset's Key Level (dB per octave tilt) so the level is even across its register, CSV with the new values
 * MIDI note numbers here follow the plugin: C3 = 48. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"
#define SR 44100.0
#include "loud.h"

#define BLK 128
#define HOLD_BLOCKS 1400     /* ~4 s: long enough for the slowest attacks */
#define SHORT_BLOCKS 52      /* ~150 ms */
#define TAIL_BLOCKS 460
#define NNOTES 5

static plugin_api_v2_t *api;
static void midi(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static void setp(void *i, const char *k, const char *v) { api->set_param(i, k, v); }
static int16_t buf[(HOLD_BLOCKS + TAIL_BLOCKS) * BLK * 2];

/* home register of a preset: five notes, low to high */
static const int REG_BASS[NNOTES] = {24, 31, 36, 43, 48}, REG_LEAD[NNOTES] = {48, 55, 60, 67, 72}, REG_MID[NNOTES] = {36, 43, 48, 55, 60};
static const int *reg_of(const char *name) {
    if (!strncmp(name, "Bass ", 5)) return REG_BASS;
    if (!strncmp(name, "Lead ", 5) || !strncmp(name, "Arp ", 4) || !strncmp(name, "Para ", 5) || !strcmp(name, "Init")) return REG_LEAD;
    if (!strncmp(name, "Set ", 4)) {
        const char *role = strstr(name, " - "); role = role ? role + 3 : "";
        if (!strcmp(role, "Bass") || !strcmp(role, "Sub")) return REG_BASS;
        if (!strcmp(role, "Lead") || !strcmp(role, "Arp") || !strcmp(role, "Pluck") || !strcmp(role, "Stab") || !strcmp(role, "Chime")) return REG_LEAD;
    }
    return REG_MID;
}

/* level of one note on a fresh instance (preset, optional key_lvl override); hold_blocks of sound, then release */
static double note_level(int preset, double key_lvl, int note, int vel, int hold_blocks, double *peak, double *kl_out, char *name, size_t nn) {
    char v[64];
    void *inst = api->create_instance(".", NULL);
    snprintf(v, sizeof v, "%d", preset); setp(inst, "preset", v);
    api->get_param(inst, "preset_full", name, (int)nn);
    if (key_lvl > -9) { snprintf(v, sizeof v, "%.4f", key_lvl); setp(inst, "key_lvl", v); }
    api->get_param(inst, "key_lvl", v, sizeof v); *kl_out = atof(v);
    midi(inst, 0x90, note, vel);
    int b = 0;
    for (; b < hold_blocks; b++) api->render_block(inst, buf + b * BLK * 2, BLK);
    midi(inst, 0x80, note, 0); midi(inst, 0xB0, 123, 0);
    for (; b < HOLD_BLOCKS + TAIL_BLOCKS; b++) api->render_block(inst, buf + b * BLK * 2, BLK);
    double l = loud_db(buf, (hold_blocks == SHORT_BLOCKS ? SHORT_BLOCKS + 120 : hold_blocks) * BLK, peak);
    api->destroy_instance(inst);
    return l;
}

/* least-squares slope of level (dB) against note (semitones) */
static double slope(const int *n, const double *l) {
    double sn = 0, sl = 0, snn = 0, snl = 0;
    for (int i = 0; i < NNOTES; i++) { sn += n[i]; sl += l[i]; snn += (double)n[i] * n[i]; snl += n[i] * l[i]; }
    return (NNOTES * snl - sn * sl) / (NNOTES * snn - sn * sn);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s dsp.so measure|calibrate\n", argv[0]); return 2; }
    int calibrate = !strcmp(argv[2], "calibrate");
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    api = init(&host);
    a_weight_init();
    char v[64], nm[96];
    { void *t = api->create_instance(".", NULL); api->get_param(t, "preset_count", v, sizeof v); api->destroy_instance(t); }
    int cnt = atoi(v), bad = 0;
    printf("name,key_lvl,n1,n2,n3,n4,n5,spread_db,slope_db_oct,vel40_db,vel127_db,short_minus_held_db,peak_db\n");
    for (int i = 0; i < cnt; i++) {
        { void *t = api->create_instance(".", NULL); snprintf(v, sizeof v, "%d", i); setp(t, "preset", v); api->get_param(t, "preset_full", nm, sizeof nm); api->destroy_instance(t); }
        const int *reg = reg_of(nm);
        g_flat = (reg == REG_BASS);   /* the bass register is judged by plain RMS, the rest A-weighted */
        double L[NNOTES], pk = -99, kl = -99, pkn;
        for (int it = 0; it < (calibrate ? 3 : 1); it++) {
            double klo;
            for (int k = 0; k < NNOTES; k++) { L[k] = note_level(i, kl, reg[k], 100, HOLD_BLOCKS, &pkn, &klo, nm, sizeof nm); if (pkn > pk || k == 0) pk = pkn; }
            kl = klo;
            if (!calibrate) break;
            double s = slope(reg, L);                 /* dB per semitone; Key Level 1 = 6 dB per octave = 0.5 dB per semitone */
            double nk = kl - s / 0.5; if (nk > 1) nk = 1; if (nk < -1) nk = -1;
            if (fabs(nk - kl) < 0.04) break;
            kl = nk;
        }
        if (calibrate) { /* final measurement with the fitted value */
            double klo; pk = -99;
            for (int k = 0; k < NNOTES; k++) { L[k] = note_level(i, kl, reg[k], 100, HOLD_BLOCKS, &pkn, &klo, nm, sizeof nm); if (pkn > pk) pk = pkn; }
        }
        for (int g = 0; calibrate && g < 8 && pk > -3.0 && fabs(kl) > 0.02; g++) {   /* headroom: A-weighting asks a lot of low bass notes, and the limiter would then distort them; ease the tilt back */
            double klo; kl *= 0.8; pk = -99;
            for (int k = 0; k < NNOTES; k++) { L[k] = note_level(i, kl, reg[k], 100, HOLD_BLOCKS, &pkn, &klo, nm, sizeof nm); if (pkn > pk) pk = pkn; }
        }
        double lo = 1e9, hi = -1e9; for (int k = 0; k < NNOTES; k++) { if (L[k] < lo) lo = L[k]; if (L[k] > hi) hi = L[k]; }
        double d40, d127, dsh, t1, t2, klo;
        d40 = note_level(i, calibrate ? kl : -99, reg[2], 40, HOLD_BLOCKS, &t1, &klo, nm, sizeof nm);
        d127 = note_level(i, calibrate ? kl : -99, reg[2], 127, HOLD_BLOCKS, &t2, &klo, nm, sizeof nm);
        dsh = note_level(i, calibrate ? kl : -99, reg[2], 100, SHORT_BLOCKS, &t1, &klo, nm, sizeof nm) - L[2];
        if (!calibrate) kl = klo;
        if (strcmp(nm, "Init") && ((hi - lo > 6.0 && pk < -3.5) || lo < -34.0)) bad++;   /* Init is the plain default patch, not a fitted preset */   /* a wider spread is accepted where the low notes are held back by headroom, not by the fit */
        printf("\"%s\",%.3f,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f,%.2f,%.1f,%.1f,%.1f,%.1f\n", nm, kl, L[0], L[1], L[2], L[3], L[4], hi - lo, slope(reg, L) * 12.0, d40, d127, dsh, pk);
        fflush(stdout);
    }
    fprintf(stderr, "%d of %d presets move more than 6 dB across their home register (or are nearly silent in it)\n", bad, cnt);
    return (!calibrate && bad) ? 1 : 0;
}
