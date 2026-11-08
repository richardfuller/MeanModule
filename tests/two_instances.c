/* Two instances side by side (two Move tracks using Mean Module): playing, releasing, panicking or changing presets on one must never change the other.
 * Usage: two_instances dsp.so */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

static plugin_api_v2_t *api;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)
static void midi(void *i, int a, int b, int c) { uint8_t m[3] = {(uint8_t)a, (uint8_t)b, (uint8_t)c}; api->on_midi(i, m, 3, MOVE_MIDI_SOURCE_EXTERNAL); }
static double blocks(void *i, int n) {   /* render n blocks, return the rms of the last one */
    static int16_t buf[256]; double e = 0;
    for (int b = 0; b < n; b++) api->render_block(i, buf, 128);
    for (int k = 0; k < 256; k++) e += (buf[k] / 32768.0) * (buf[k] / 32768.0);
    return sqrt(e / 256);
}
/* render both like a host does (every block, both instances) and return the rms of the last block of each */
static void both(void *a, void *b, int n, double *ra, double *rb) { for (int k = 0; k < n; k++) { double x = blocks(a, 1), y = blocks(b, 1); if (k == n - 1) { *ra = x; *rb = y; } } }

int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    api = init(&host);
    void *A = api->create_instance(".", NULL), *B = api->create_instance(".", NULL);
    CHECK(A && B && A != B, "two distinct instances");
    api->set_param(A, "env2_s", "1"); api->set_param(B, "env2_s", "1");
    api->set_param(B, "preset", "30");
    double ra, rb;
    midi(A, 0x90, 48, 100); both(A, B, 300, &ra, &rb);
    printf("A playing: A %.4f B %.4f\n", ra, rb);
    CHECK(ra > 0.01 && rb < 1e-6, "only A sounds");
    double a0 = ra;
    midi(B, 0x90, 60, 100); both(A, B, 300, &ra, &rb);
    printf("B joins: A %.4f B %.4f\n", ra, rb);
    CHECK(ra > 0.5 * a0 && rb > 0.005, "both sound");
    midi(B, 0x80, 60, 0); both(A, B, 300, &ra, &rb);
    printf("B released: A %.4f B %.4f\n", ra, rb);
    CHECK(ra > 0.5 * a0, "releasing B must not silence A");
    midi(B, 0xB0, 123, 0); midi(B, 0xB0, 120, 0); midi(B, 0xB0, 121, 0); both(A, B, 100, &ra, &rb);
    CHECK(ra > 0.5 * a0, "a panic or reset sent to B must not silence A");
    api->set_param(B, "preset", "100"); both(A, B, 100, &ra, &rb);
    CHECK(ra > 0.5 * a0, "changing B's preset must not silence A");
    midi(B, 0x90, 48, 100); midi(B, 0x80, 48, 0); both(A, B, 300, &ra, &rb);   /* same note number on both */
    CHECK(ra > 0.5 * a0, "B playing and releasing the same note number must not stop A (%.4f)", ra);
    midi(A, 0x80, 48, 0); both(A, B, 400, &ra, &rb);
    CHECK(ra < 0.005, "A releases when told to");
    printf("two instances: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
