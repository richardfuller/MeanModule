/* Renders every factory preset (one note, held then released) and prints one CSV row of sound features per preset, so near-duplicates
 * and gaps can be found by ear-free measurement.  Usage: preset_audit dsp.so [note] > audit.csv
 * Columns: idx,name,rms_db,peak_db,attack_ms,centroid_hz,flatness,b0..b9 (octave bands from 55 Hz, dB re total),amp_mod,tail_db,pitch_hz */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

#define SR 44100
#define NFFT 8192
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

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s dsp.so [note]\n", argv[0]); return 2; }
    int note = argc > 2 ? atoi(argv[2]) : 48;
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    plugin_api_v2_t *api = init(&host);
    char cnt[16]; void *probe = api->create_instance(".", NULL);
    api->get_param(probe, "preset_count", cnt, sizeof cnt); int np = atoi(cnt); api->destroy_instance(probe);
    const int total = 2 * SR / 128 * 128, hold = (int)(1.2 * SR) / 128;
    static int16_t buf[2 * SR * 2]; static double mono[2 * SR], re[NFFT], im[NFFT];
    printf("idx,name,rms_db,peak_db,attack_ms,centroid_hz,flatness,b0,b1,b2,b3,b4,b5,b6,b7,b8,b9,amp_mod,tail_db,pitch_hz,fdrive,drive,res,noise,cutoff,grit\n");
    for (int p = 0; p < np; p++) {
        void *inst = api->create_instance(".", NULL);
        char v[16], nm[64]; snprintf(v, sizeof v, "%d", p);
        api->set_param(inst, "preset", v); api->get_param(inst, "preset_full", nm, sizeof nm);
        uint8_t on[3] = {0x90, (uint8_t)note, 100}, off[3] = {0x80, (uint8_t)note, 0};
        api->on_midi(inst, on, 3, 2);
        for (int b = 0; b < total / 128; b++) { if (b == hold) api->on_midi(inst, off, 3, 2); api->render_block(inst, buf + b * 256, 128); }
        for (int i = 0; i < total; i++) mono[i] = (buf[2 * i] + buf[2 * i + 1]) / 65536.0;
        double pk = 0; int pki = 0;
        for (int i = 0; i < hold * 128; i++) if (fabs(mono[i]) > pk) { pk = fabs(mono[i]); pki = i; }
        int att = 0; for (int i = 0; i < hold * 128; i++) if (fabs(mono[i]) >= 0.9 * pk) { att = i; break; }
        (void)pki;
        int w0 = (int)(0.4 * SR);
        double e = 0; for (int i = w0; i < w0 + NFFT; i++) e += mono[i] * mono[i];
        double rms = sqrt(e / NFFT);
        for (int i = 0; i < NFFT; i++) { double w = 0.5 - 0.5 * cos(2 * M_PI * i / NFFT); re[i] = mono[w0 + i] * w; im[i] = 0; }
        fft(re, im, NFFT);
        double band[10] = {0}, tot = 1e-30, cs = 0, lg = 0, ar = 0; int nb = 0, pbin = 1; double pmax = 0;
        for (int k = 1; k < NFFT / 2; k++) {
            double f = (double)k * SR / NFFT, pw = re[k] * re[k] + im[k] * im[k];
            tot += pw; cs += f * pw;
            if (f > 40 && f < 12000) { lg += log(pw + 1e-20); ar += pw; nb++; }
            if (pw > pmax && f > 40 && f < 2000) { pmax = pw; pbin = k; }
            int bi = (int)floor(log2(f / 55.0)); if (bi < 0) bi = 0; if (bi > 9) bi = 9; band[bi] += pw;
        }
        double flat = nb ? exp(lg / nb) / (ar / nb + 1e-30) : 0;
        /* amplitude movement: rms of 20 ms frames between 0.3 s and 1.1 s */
        double fr[40]; int nf = 0; double fm = 0;
        for (int s = (int)(0.3 * SR); s + 882 <= (int)(1.1 * SR); s += 882) { double a = 0; for (int i = 0; i < 882; i++) a += mono[s + i] * mono[s + i]; fr[nf] = sqrt(a / 882); fm += fr[nf]; nf++; }
        fm /= nf; double fv = 0; for (int i = 0; i < nf; i++) fv += (fr[i] - fm) * (fr[i] - fm); double amod = fm > 1e-9 ? sqrt(fv / nf) / fm : 0;
        double te = 0; for (int i = (int)(1.6 * SR); i < (int)(2.0 * SR); i++) te += mono[i] * mono[i];
        printf("%d,\"%s\",%.1f,%.1f,%.0f,%.0f,%.3f", p, nm, 20 * log10(rms + 1e-9), 20 * log10(pk + 1e-9), att * 1000.0 / SR, cs / tot, flat);
        for (int b = 0; b < 10; b++) printf(",%.1f", 10 * log10(band[b] / tot + 1e-12));
        printf(",%.3f,%.1f,%.1f", amod, 10 * log10(te / (0.4 * SR) + 1e-18), pbin * (double)SR / NFFT);
        { const char *ks[6] = {"filter_drive", "drive", "resonance", "noise_level", "cutoff", "grit"}; char q[24]; for (int k = 0; k < 6; k++) { api->get_param(inst, ks[k], q, sizeof q); printf(",%s", q); } printf("\n"); }
        api->destroy_instance(inst);
    }
    return 0;
}
