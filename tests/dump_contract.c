/* Prints this plugin's UI contract (ui_hierarchy + chain_params) as one JSON module entry, in the format of
 * Schwung's tests/fixtures/module-contracts.json, so its offline page previewer can draw our screens.
 * Usage: dump_contract path/to/dsp.so > meanmodule_contract.json */
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "../src/host/plugin_api_v1.h"

static const host_api_v1_t host;   /* all callbacks NULL: the plugin guards every use */

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: %s dsp.so\n", argv[0]); return 2; }
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "%s\n", dlerror()); return 1; }
    plugin_api_v2_t *(*init)(const host_api_v1_t *) = (plugin_api_v2_t *(*)(const host_api_v1_t *))dlsym(h, "move_plugin_init_v2");
    plugin_api_v2_t *api = init(&host);
    void *inst = api->create_instance(".", "{}");
    static char hier[65536], cp[65536];
    if (api->get_param(inst, "ui_hierarchy", hier, sizeof hier) < 0 || api->get_param(inst, "chain_params", cp, sizeof cp) < 0) return 1;
    printf("{\"id\":\"meanmodule\",\"category\":\"sound_generator\",\"component_key\":\"synth\",\"status\":\"ok\",\"name\":\"Mean Mod\",\"version\":\"0\",\"ui_hierarchy\":%s,\"chain_params\":%s,\"presets\":null}\n", hier, cp);
    return 0;
}
