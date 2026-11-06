/* Offline render: render <dsp.so> <out.wav> <seconds> <hold_seconds> <note> [key=val ...]
 * Holds `note` for hold_seconds (then releases), renders `seconds` total. */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

int main(int argc, char **argv) {
    if (argc < 6) { fprintf(stderr, "usage: %s dsp.so out.wav secs hold note [k=v ...]\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 2; }
    move_plugin_init_v2_fn init = (move_plugin_init_v2_fn)dlsym(h, MOVE_PLUGIN_INIT_V2_SYMBOL);
    host_api_v1_t host; memset(&host, 0, sizeof host);
    plugin_api_v2_t *api = init(&host);
    void *inst = api->create_instance(".", NULL);
    for (int i = 6; i < argc; i++) {
        char *eq = strchr(argv[i], '='); if (!eq) continue;
        *eq = 0; api->set_param(inst, argv[i], eq + 1);
    }
    { char nm[64]; if (api->get_param(inst, "preset_name", nm, sizeof nm) > 0) printf("%s\n", nm); }
    int total = (int)(atof(argv[3]) * 44100) / 128 * 128, hold = (int)(atof(argv[4]) * 44100) / 128;
    int note = atoi(argv[5]);
    int16_t *buf = malloc((size_t)total * 4);
    uint8_t on[3] = {0x90, (uint8_t)note, 110}, off[3] = {0x80, (uint8_t)note, 0};
    api->on_midi(inst, on, 3, 2);
    for (int b = 0; b < total / 128; b++) {
        if (b == hold) api->on_midi(inst, off, 3, 2);
        api->render_block(inst, buf + b * 256, 128);
    }
    FILE *f = fopen(argv[2], "wb");
    uint32_t data = total * 4, rate = 44100, br = rate * 4, riff = 36 + data, fmt = 16;
    uint16_t pcm = 1, ch = 2, ba = 4, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmt, 4, 1, f);
    fwrite(&pcm, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&data, 4, 1, f);
    fwrite(buf, 4, total, f); fclose(f);
    api->destroy_instance(inst);
    return 0;
}
