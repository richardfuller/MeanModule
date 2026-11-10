/* A-weighted loudness measuring, shared by loudness.c and register.c */
#include <math.h>
#include <stdint.h>
#ifndef SR
#define SR 44100.0
#endif
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
static int g_flat;   /* 1 = no weighting (used for the bass register, where A-weighting discounts the low notes by 20-30 dB and would ask the limiter for the impossible) */
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
        double y = g_flat ? v : bq_run(&A[2], bq_run(&A[1], bq_run(&A[0], v))) * a_gain;
        acc += y * y; if (++n == HOP) { hop_ms[h++] = acc / HOP; acc = 0; n = 0; }
    }
    double best = 0;
    for (int i = 0; i + 3 <= h; i++) { double m = (hop_ms[i] + hop_ms[i + 1] + hop_ms[i + 2]) / 3.0; if (m > best) best = m; }
    if (peak_db) *peak_db = 20.0 * log10(pk > 1e-9 ? pk : 1e-9);
    return 10.0 * log10(best > 1e-12 ? best : 1e-12);
}

