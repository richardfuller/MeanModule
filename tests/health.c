/* Sound health of every factory preset: things that can be measured instead of listened for.
 *   clip       share of samples at the limiter (|x| > 0.98 FS) on a hard-played low, middle and high note of the home register
 *   crest      peak to RMS in dB on the middle note (a very low crest = a squashed, square-ish sound)
 *   rel_ms     time for the level to fall by 20 dB after note-off: a very short release is a click
 *   tail_s     time until the sound is below -60 dBFS after note-off: a long tail is a stuck or hanging sound
 *   cents      the measured pitch against the played note (oscillator coarse and Octave included); big errors are octave slips, noise or FM chaos
 *   wheel/at/stx/sty  level change (dB) and brightness change (%) when that controller goes to full; "dead" = the preset routes it but nothing changes
 *   tempo      arp presets: how many times faster the note onsets get when the host tempo goes from 90 to 150 BPM (1.67 expected)
 *   cpu_us     host CPU per 128-frame block (the device is roughly 4-6 times slower)
 * Usage: health dsp.so [first last]      CSV on stdout, flags on stderr, exit 1 on a hard flag.
 * Hard flags: clipping above 3%, a tail over 6 s, a sustained preset with a release under 10 ms, a dead controller route, an arp that does not follow the tempo, non-finite output. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "plugin_api_v1.h"
#define SR 44100.0
#include "loud.h"
#include "regs.h"

#define BLK 128
#define HOLD_BLOCKS 690      /* ~2 s */
#define TAIL_BLOCKS 1900     /* ~5.5 s */

static plugin_api_v2_t *api;
static float g_bpm = 120.0f;
static float host_bpm(void) { return g_bpm; }
static void midi(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static void touch(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_TOUCH); }
static void setp(void *i, const char *k, const char *v) { api->set_param(i, k, v); }
static int16_t buf[(HOLD_BLOCKS + TAIL_BLOCKS) * BLK * 2];

typedef struct { int nan; double clip, peak_db, crest_db, rel_ms, tail_s, cpu_avg, cpu_max, hold_loud, centroid, onsets, env_cov, pitch_cov, nonharm_db, onset_hf_db, p1_cents, p2_cents, p2_db; } run_t;

typedef struct { int wheel, at, stx, sty; } ctl_t;   /* controllers pushed to full */

static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }

static void fft(double *re, double *im, int n) {
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t; t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2 * M_PI / len, wr = cos(ang), wi = sin(ang);
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
/* power-averaged spectral centroid of the held part (40 Hz .. 10 kHz) */
static double centroid_of(const int16_t *x, int frames) {
    enum { N = 4096 }; static double re[N], im[N], pw[N / 2]; memset(pw, 0, sizeof pw);
    for (int s = frames / 3; s + N <= frames; s += N) {
        for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / N); re[i] = x[2 * (s + i)] / 32768.0 * w; im[i] = 0; }
        fft(re, im, N);
        for (int k = 1; k < N / 2; k++) pw[k] += re[k] * re[k] + im[k] * im[k];
    }
    double num = 0, den = 0;
    for (int k = 1; k < N / 2; k++) { double f = k * SR / N; if (f < 40 || f > 10000) continue; num += f * pw[k]; den += pw[k]; }
    return den > 1e-12 ? num / den : 0;
}


static double g_f0, g_pf1, g_pf2;   /* expected fundamental (alias check) and the two oscillator frequencies (Para tuning check); 0 = skip */
/* share of the spectrum that is not at a harmonic of f0 (dB): high = aliasing, noise or inharmonic content */
static double nonharm_db(const int16_t *x, int hold, double f0) {
    enum { N = 8192 }; static double re[N], im[N]; if (f0 < 30 || hold < N * 2) return -99;
    int s0 = hold / 3;
    for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / N); re[i] = x[2 * (s0 + i)] / 32768.0 * w; im[i] = 0; }
    fft(re, im, N);
    double tot = 0, harm = 0;
    for (int k = 2; k < N / 2; k++) {
        double f = k * SR / N; if (f < 60 || f > 16000) continue;
        double p = re[k] * re[k] + im[k] * im[k], hn = f / f0, nearest = floor(hn + 0.5);
        tot += p;
        if (nearest >= 1 && fabs(f - nearest * f0) < fmax(3.0 * SR / N, 0.02 * nearest * f0)) harm += p;
    }
    return tot > 1e-12 ? 10 * log10(fmax(tot - harm, 1e-12) / tot) : -99;
}
/* the strongest spectral peak within +-60 cents of fexp: its error in cents and its level against the strongest peak overall (dB) */
static void peak_near(const int16_t *x, int hold, double fexp, double *cents, double *db) {
    enum { N = 16384 }; static double re[N], im[N], pw[N / 2]; *cents = 0; *db = -99; if (fexp < 30 || hold < N + hold / 3) return;
    int s0 = hold / 3;
    for (int i = 0; i < N; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / N); re[i] = x[2 * (s0 + i)] / 32768.0 * w; im[i] = 0; }
    fft(re, im, N);
    double mx = 0, best = 0; int bk = 0;
    for (int k = 2; k < N / 2; k++) { pw[k] = re[k] * re[k] + im[k] * im[k]; if (pw[k] > mx) mx = pw[k]; }
    int klo = (int)(fexp * pow(2, -60.0 / 1200) * N / SR), khi = (int)(fexp * pow(2, 60.0 / 1200) * N / SR) + 1;
    for (int k = klo < 2 ? 2 : klo; k <= khi && k < N / 2 - 1; k++) if (pw[k] > best) { best = pw[k]; bk = k; }
    if (!bk || best <= 0) return;
    double a = log(pw[bk - 1] + 1e-30), b = log(pw[bk] + 1e-30), c = log(pw[bk + 1] + 1e-30), d = a - 2 * b + c, off = d != 0 ? 0.5 * (a - c) / d : 0;
    double f = (bk + off) * SR / N; *cents = 1200.0 * log2(f / fexp); *db = 10 * log10(best / (mx + 1e-30));
}
/* energy above 4 kHz in the first 6 ms of the note against 30-60 ms later: a big jump is a click */
static double onset_hf_db(const int16_t *x) {
    enum { N = 512 }; static double re[N], im[N]; double e[2];
    int st[2] = {0, 1323};
    for (int w = 0; w < 2; w++) {
        for (int i = 0; i < N; i++) { double ww = i < 264 ? 1.0 : 0.0; re[i] = x[2 * (st[w] + i)] / 32768.0 * ww * (i < 264 ? 0.5 - 0.5 * cos(2 * M_PI * i / 264) : 0); im[i] = 0; }
        fft(re, im, N); e[w] = 0;
        for (int k = 1; k < N / 2; k++) if (k * SR / N > 4000) e[w] += re[k] * re[k] + im[k] * im[k];
    }
    return 10 * log10((e[0] + 1e-12) / (e[1] + 1e-12));
}

static void run_note(int preset, int note, int vel, ctl_t c, int arp_chord, run_t *r) {
    char v[64];
    void *inst = api->create_instance(".", NULL);
    snprintf(v, sizeof v, "%d", preset); setp(inst, "preset", v);
    if (c.wheel) midi(inst, 0xB0, 1, 127);
    if (c.at) midi(inst, 0xD0, 127, 0);
    if (c.stx || c.sty) {   /* hold the stick knobs "touched" so they do not spring back */
        if (c.stx) { touch(inst, 0x90, 0, 127); setp(inst, "stick_x", "1"); }
        if (c.sty) { touch(inst, 0x90, 1, 127); setp(inst, "stick_y", "1"); }
    }
    if (arp_chord) { midi(inst, 0x90, note, vel); midi(inst, 0x90, note + 4, vel); midi(inst, 0x90, note + 7, vel); } else midi(inst, 0x90, note, vel);
    double t0 = now_us(), cmax = 0; int b = 0;
    for (; b < HOLD_BLOCKS; b++) { double a = now_us(); api->render_block(inst, buf + b * BLK * 2, BLK); double d = now_us() - a; if (d > cmax) cmax = d; }
    double cavg = (now_us() - t0) / HOLD_BLOCKS;
    if (arp_chord) { midi(inst, 0x80, note, 0); midi(inst, 0x80, note + 4, 0); midi(inst, 0x80, note + 7, 0); } else midi(inst, 0x80, note, 0);
    for (; b < HOLD_BLOCKS + TAIL_BLOCKS; b++) api->render_block(inst, buf + b * BLK * 2, BLK);
    api->destroy_instance(inst);

    int hold = HOLD_BLOCKS * BLK, total = (HOLD_BLOCKS + TAIL_BLOCKS) * BLK;
    long clip = 0; double pk = 0, e = 0; r->nan = 0;
    for (int i = 0; i < hold; i++) {
        double x = buf[2 * i] / 32768.0; if (!isfinite(x)) r->nan = 1;
        if (fabs(x) > 0.98) clip++;
        if (fabs(x) > pk) pk = fabs(x);
        if (i >= hold / 3) e += x * x;
    }
    double rms = sqrt(e / (hold - hold / 3));
    r->clip = 100.0 * clip / hold; r->peak_db = 20 * log10(pk > 1e-9 ? pk : 1e-9); r->crest_db = 20 * log10((pk > 1e-9 ? pk : 1e-9) / (rms > 1e-9 ? rms : 1e-9));
    double lp = 0; r->hold_loud = loud_db(buf, hold, &lp); r->centroid = centroid_of(buf, hold);
    /* release and tail: 10 ms RMS steps after note-off, against the 100 ms before it */
    double pre = 0; for (int i = hold - 4410; i < hold; i++) { double x = buf[2 * i] / 32768.0; pre += x * x; } pre = sqrt(pre / 4410);
    r->rel_ms = 1e9; r->tail_s = 0;
    for (int s = hold; s + 441 <= total; s += 441) {
        double a = 0; for (int i = 0; i < 441; i++) { double x = buf[2 * (s + i)] / 32768.0; a += x * x; } a = sqrt(a / 441);
        double t = (s - hold + 441) * 1000.0 / SR;
        if (r->rel_ms > 1e8 && a < 0.1 * pre) r->rel_ms = t;
        if (a > 0.001) r->tail_s = (s - hold + 441) / SR;
    }
    if (r->rel_ms > 1e8) r->rel_ms = (total - hold) * 1000.0 / SR;
    r->cpu_avg = cavg; r->cpu_max = cmax;
    /* onsets in the hold (for the tempo test): clear rises of the 10 ms envelope */
    int n = hold / 441; double ev[400], mx = 0; if (n > 400) n = 400;
    for (int h = 0; h < n; h++) { double a = 0; for (int i = 0; i < 441; i++) { double x = buf[2 * (h * 441 + i)] / 32768.0; a += x * x; } ev[h] = sqrt(a / 441); if (ev[h] > mx) mx = ev[h]; }
    { double sm = 0, sq = 0; int nn = 0, z0 = -1; double fm = 0, fq = 0; int nf = 0;
      for (int h = n / 3; h < n; h++) { sm += ev[h]; sq += ev[h] * ev[h]; nn++; }
      double mean = nn ? sm / nn : 0; r->env_cov = mean > 1e-6 ? sqrt(fmax(sq / nn - mean * mean, 0)) / mean : 0;
      for (int s0 = hold / 3; s0 + 2205 <= hold; s0 += 2205) {   /* pitch per 50 ms from rising zero crossings */
          int zc = 0; for (int i = s0 + 1; i < s0 + 2205; i++) if (buf[2 * (i - 1)] < 0 && buf[2 * i] >= 0) zc++;
          double f = zc / (2205 / SR); fm += f; fq += f * f; nf++; (void)z0;
      }
      double fmean = nf ? fm / nf : 0; r->pitch_cov = fmean > 1 ? sqrt(fmax(fq / nf - fmean * fmean, 0)) / fmean : 0; }
    r->nonharm_db = nonharm_db(buf, hold, g_f0); r->onset_hf_db = onset_hf_db(buf);
    r->p1_cents = r->p2_cents = 0; r->p2_db = -99; if (g_pf1 > 0) { double d; peak_near(buf, hold, g_pf1, &r->p1_cents, &d); } if (g_pf2 > 0) peak_near(buf, hold, g_pf2, &r->p2_cents, &r->p2_db);
    r->onsets = 0; int last = -10;
    for (int h = 2; h < n; h++) { double base = ev[h - 1] < ev[h - 2] ? ev[h - 1] : ev[h - 2]; if (ev[h] > 0.1 * mx && ev[h] > 1.35 * base && h - last >= 3) { r->onsets++; last = h; } }
}

/* pitch of the held middle note by autocorrelation, in cents against the expected frequency (searched over +-1 octave) */
static double pitch_cents(const int16_t *x, double f0) {
    int start = 40000, len = 8192;
    int lmin = (int)(SR / (f0 * 2.05)), lmax = (int)(SR / (f0 * 0.49));
    if (lmax > 4000) lmax = 4000;
    static double ac[4100]; double best = 0;
    for (int lag = lmin; lag <= lmax; lag++) {
        double a = 0; for (int i = 0; i < len; i += 2) a += (x[2 * (start + i)] / 32768.0) * (x[2 * (start + i + lag)] / 32768.0);
        ac[lag] = a; if (a > best) best = a;
    }
    if (best <= 0) return 0;
    int bl = 0;
    for (int lag = lmin + 1; lag < lmax; lag++) if (ac[lag] >= 0.85 * best && ac[lag] >= ac[lag - 1] && ac[lag] >= ac[lag + 1]) { bl = lag; break; }   /* the shortest strong peak */
    if (!bl) return 0;
    double f = SR / bl; return 1200.0 * log2(f / f0);
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s dsp.so [first last]\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host); host.get_bpm = host_bpm;
    api = init(&host);
    a_weight_init();
    char v[96], nm[96];
    { void *t = api->create_instance(".", NULL); api->get_param(t, "preset_count", v, sizeof v); api->destroy_instance(t); }
    int cnt = atoi(v), first = argc > 3 ? atoi(argv[2]) : 0, last = argc > 3 ? atoi(argv[3]) : cnt, hard = 0;
    if (last > cnt) last = cnt;
    if (first == 0) printf("name,clip_pct,peak_db,crest_db,rel_ms,tail_s,cents,wheel_db,wheel_bright,at_db,at_bright,stx_db,stx_bright,sty_db,sty_bright,tempo_ratio,cpu_avg_us,cpu_max_us,nonharm_db,onset_hf_db,p1_cents,p2_cents,p2_db,flags\n");
    for (int i = first; i < last; i++) {
        char flags[512] = ""; int arp = 0, sustains = 0;
        void *t = api->create_instance(".", NULL); snprintf(v, sizeof v, "%d", i); setp(t, "preset", v); api->get_param(t, "preset_full", nm, sizeof nm);
        api->get_param(t, "arp_mode", v, sizeof v); arp = atoi(v) > 0;
        api->get_param(t, "arp_gate", v, sizeof v); int tied = atof(v) >= 0.999;
        int src[12], nsrc = 0; const char *keys[] = {"m1_src", "m2_src", "m3_src", "m4_src", "m5_src", "m6_src", "m1_via", "m2_via", "m3_via", "m4_via", "m5_via", "m6_via"};
        for (int k = 0; k < 12; k++) { api->get_param(t, keys[k], v, sizeof v); src[nsrc++] = atoi(v); }
        api->get_param(t, "at_dest", v, sizeof v); int at_dest = atoi(v);
        api->get_param(t, "octave", v, sizeof v); int oct = atoi(v);
        api->get_param(t, "osc1_wave", v, sizeof v); int w1 = atoi(v);
        api->get_param(t, "osc1_level", v, sizeof v); double l1 = atof(v);
        api->get_param(t, "osc1_coarse", v, sizeof v); int c1 = atoi(v);
        api->get_param(t, "osc2_coarse", v, sizeof v); int c2 = atoi(v);
        api->get_param(t, "osc1_fm1", v, sizeof v); double fm1 = atof(v);
        api->get_param(t, "osc2_fm1", v, sizeof v); double fm2e = atof(v);
        api->get_param(t, "osc2_wave", v, sizeof v); int w2 = atoi(v);
        api->get_param(t, "osc2_level", v, sizeof v); double l2 = atof(v);
        api->get_param(t, "osc1_fine", v, sizeof v); double fine1 = atof(v);
        api->get_param(t, "osc2_fine", v, sizeof v); double fine2 = atof(v);
        api->get_param(t, "stick_x_to", v, sizeof v); int xto = atoi(v);
        api->get_param(t, "stick_y_to", v, sizeof v); int yto = atoi(v);
        api->destroy_instance(t);
        int uses_wheel = 0, uses_at = (at_dest == 1 || at_dest == 3 || at_dest == 4), uses_sx = 1, uses_sy = 1;
        for (int k = 0; k < nsrc; k++) { if (src[k] == 5) uses_wheel = 1; if (src[k] == 6) uses_at = 1; if (src[k] == 7) uses_sx = 1; if (src[k] == 8) uses_sy = 1; }
        (void)xto; (void)yto;
        const int *reg = reg_of(nm);
        run_t lo, mid, hi, base, rw, ra, rx, ry, slow, fast; ctl_t none = {0, 0, 0, 0};
        g_bpm = 120;
        #define FREQ(semi) (440.0 * pow(2.0, ((semi) - 69.0) / 12.0))
        int osc1_heard = w1 > 0 && l1 > 0.05;
        double base_off = 12.0 * oct + (osc1_heard ? c1 + fine1 / 100.0 : c2 + fine2 / 100.0);
        g_f0 = 0; g_pf1 = g_pf2 = 0;
        run_note(i, reg[0], 127, none, 0, &lo); run_note(i, reg[2], 127, none, 0, &mid);
        g_f0 = arp ? 0 : FREQ(reg[4] + base_off); run_note(i, reg[4], 127, none, 0, &hi); g_f0 = 0;
        int is_para = !strncmp(nm, "Para ", 5) && !arp && w1 > 0 && w2 > 0 && l1 > 0.3 && l2 > 0.3 && fm1 < 0.01 && fm2e < 0.01;   /* a pitch envelope moves the interval on purpose */
        if (is_para) { g_pf1 = FREQ(reg[2] + 12.0 * oct + c1 + fine1 / 100.0); g_pf2 = FREQ(reg[2] + 12.0 * oct + c2 + fine2 / 100.0); }
        run_note(i, reg[2], 100, none, 0, &base); g_pf1 = g_pf2 = 0;
        sustains = base.hold_loud > -40 && mid.tail_s > 0.0;
        double clip = lo.clip > mid.clip ? (lo.clip > hi.clip ? lo.clip : hi.clip) : (mid.clip > hi.clip ? mid.clip : hi.clip);
        /* pitch: expected from the first oscillator that is heard */
        double exp_semi = reg[2] + 12.0 * oct + ((w1 > 0 && l1 > 0.05) ? c1 : c2);
        double cents = arp ? 0 : pitch_cents(buf, 440.0 * pow(2.0, (exp_semi - 69.0) / 12.0));
        /* controllers: only the ones the preset uses, otherwise 0 */
        double wd = 0, wb = 0, ad = 0, ab = 0, xd = 0, xb = 0, yd = 0, yb = 0, wm = 0, am = 0;   /* wm, am: change in level wobble and pitch wobble */
        if (uses_wheel) { ctl_t c = {1, 0, 0, 0}; run_note(i, reg[2], 100, c, 0, &rw); wd = rw.hold_loud - base.hold_loud; wb = 100.0 * (rw.centroid / (base.centroid + 1e-9) - 1.0); wm = fabs(rw.env_cov - base.env_cov) * 10.0 + fabs(rw.pitch_cov - base.pitch_cov) * 200.0; }
        if (uses_at) { ctl_t c = {0, 1, 0, 0}; run_note(i, reg[2], 100, c, 0, &ra); ad = ra.hold_loud - base.hold_loud; ab = 100.0 * (ra.centroid / (base.centroid + 1e-9) - 1.0); am = fabs(ra.env_cov - base.env_cov) * 10.0 + fabs(ra.pitch_cov - base.pitch_cov) * 200.0; }
        if (uses_sx) { ctl_t c = {0, 0, 1, 0}; run_note(i, reg[2], 100, c, 0, &rx); xd = rx.hold_loud - base.hold_loud; xb = 100.0 * (rx.centroid / (base.centroid + 1e-9) - 1.0); }
        if (uses_sy) { ctl_t c = {0, 0, 0, 1}; run_note(i, reg[2], 100, c, 0, &ry); yd = ry.hold_loud - base.hold_loud; yb = 100.0 * (ry.centroid / (base.centroid + 1e-9) - 1.0); }
        double tempo = 0;
        if (arp && !tied) { g_bpm = 90; run_note(i, reg[2], 100, none, 1, &slow); g_bpm = 150; run_note(i, reg[2], 100, none, 1, &fast); g_bpm = 120; tempo = slow.onsets > 0 ? fast.onsets / slow.onsets : 0; }
        double cpu = base.cpu_avg > mid.cpu_avg ? base.cpu_avg : mid.cpu_avg, cmax = base.cpu_max > mid.cpu_max ? base.cpu_max : mid.cpu_max;
        /* flags */
        if (lo.nan || mid.nan || hi.nan) { strcat(flags, "NONFINITE "); hard++; }
        if (clip > 3.0) { strcat(flags, "CLIP "); hard++; }
        if (mid.tail_s > 6.0 || lo.tail_s > 6.0) { strcat(flags, "LONGTAIL "); hard++; }
        if (sustains && base.rel_ms < 10.0) { strcat(flags, "ABRUPTREL "); hard++; }
        if (uses_wheel && fabs(wd) < 0.3 && fabs(wb) < 3.0 && wm < 0.2) strcat(flags, "wheel? ");
        if (uses_at && fabs(ad) < 0.3 && fabs(ab) < 3.0 && am < 0.2) strcat(flags, "aftertouch? ");
        if (arp && !tied && (tempo < 1.15 || tempo > 2.2)) { strcat(flags, "TEMPO "); hard++; }
                if (cpu > 60.0) strcat(flags, "cpu? ");
        if (base.onset_hf_db > 25.0) strcat(flags, "click? ");
        if (is_para && (fabs(base.p2_cents) > 40.0 || base.p2_db < -40.0 || fabs(base.p1_cents) > 40.0)) { strcat(flags, "PARATUNE "); hard++; }
        if (mid.crest_db < 2.0) strcat(flags, "squashed? ");
        printf("\"%s\",%.2f,%.1f,%.1f,%.0f,%.2f,%.0f,%.1f,%.0f,%.1f,%.0f,%.1f,%.0f,%.1f,%.0f,%.2f,%.0f,%.0f,%.1f,%.1f,%.0f,%.0f,%.1f,\"%s\"\n", nm, clip, mid.peak_db, mid.crest_db, base.rel_ms, mid.tail_s, cents, wd, wb, ad, ab, xd, xb, yd, yb, tempo, cpu, cmax, hi.nonharm_db, base.onset_hf_db, base.p1_cents, base.p2_cents, base.p2_db, flags);
        fflush(stdout);
    }
    fprintf(stderr, "health: %d hard flags in presets %d..%d\n", hard, first, last);
    return hard ? 1 : 0;
}
