# Verified platform notes (Step 0)

Read directly from `charlesvestal/schwung` @ `673e67ab643806abf74e9fedf25f5911a6b14c8e`
and `charlesvestal/schwung-braids` (shallow clones in `ref/`, not part of the module).

## ABI (from `src/host/plugin_api_v1.h`, vendored byte-for-byte to `src/host/plugin_api_v1.h`)

sha256 `99ffaaf83393c97d010e3df0a71e151ec9896f7e505ac8d24c25d3bea8c3c829`

```c
typedef struct plugin_api_v2 {
    uint32_t api_version;                       // MOVE_PLUGIN_API_VERSION_2 == 2
    void* (*create_instance)(const char *module_dir, const char *json_defaults);
    void  (*destroy_instance)(void *instance);
    void  (*on_midi)(void *instance, const uint8_t *msg, int len, int source);
    void  (*set_param)(void *instance, const char *key, const char *val);
    int   (*get_param)(void *instance, const char *key, char *buf, int buf_len);
    int   (*get_error)(void *instance, char *buf, int buf_len);
    void  (*render_block)(void *instance, int16_t *out_interleaved_lr, int frames);
} plugin_api_v2_t;
// export: plugin_api_v2_t* move_plugin_init_v2(const host_api_v1_t *host);
```

- `host_api_v1_t` ends at `get_beat_position` (+112), then `void *reserved[8]` starting at **+120**.
  Never declare extra fields; a static assert in the header enforces the layout.
- Audio: 44100 Hz, 128 frames, interleaved int16 `[L0,R0,...]`. `render_block` **overwrites** the buffer.
- MIDI: `msg` is 3 bytes `[status,d1,d2]`; `source` is INTERNAL 0 / EXTERNAL 2 / HOST 3 / FX_BROADCAST 4 / TOUCH 5.
  Poly AT = status `0xA0`, channel pressure `0xD0` (standard MIDI; still to confirm on device, and `capabilities.aftertouch` should be true).
- `get_param` returns length written, or -1 if not found. `get_param` for `ui_hierarchy` / `chain_params` must fit the buffer
  (64 KB limit for `chain_params`; `ui_hierarchy` shares that budget).
- Optional: `move_plugin_render_split` is a separate exported symbol (per-voice buses). Not needed for a mono synth.

## Realtime contract (confirmed in header)
Every entry point, **including create_instance, set_param and get_param**, runs on the SPI callback (SCHED_FIFO 70, core 3,
~2370 us slack per block). No malloc/free, file I/O, locks, logging, dlopen. Modules are *loaded* (dlopen, create_instance,
state-restoring set_param) on a SCHED_OTHER thread on cores 0-2, but must still obey the rules, and process-global init must
be thread-safe. Any worker thread must demote itself to SCHED_OTHER and pin to cores 0-2 as its first action.

## module.json (from schwung-braids)
Keys: `id, name, abbrev, version, description, author, license, dsp:"dsp.so", api_version:2, standalone:false,
capabilities{audio_out, audio_in, midi_in, midi_out, aftertouch, chainable, component_type:"sound_generator"}`. Loader cap: 8 KB.
Sound generators must serve `ui_hierarchy` (and `chain_params`) from `get_param`; the copy in module.json is ignored per docs/MODULES.md.
`knobs` = positional list of up to 8 keys per page (null = empty slot). Install path:
`/data/UserData/schwung/modules/sound_generators/<id>/dsp.so`.

## Corrections to the research brief
- Braids' own `braids_plugin.cpp` **inlines its own copy of the API structs** (lines ~29-55), a drift risk. We use the vendored header instead.
- Braids builds with `g++ -O3 -fPIC -shared` via Docker (debian:bookworm + `gcc-aarch64-linux-gnu`); `scripts/build.sh`, `install.sh`,
  `release.json` (`version`, `download_url` to a `*-module.tar.gz`) is the template to copy.
- The brief's `render_block`/`on_midi` signatures are confirmed correct.

## Still unverified
- Whether host exposes poly AT to sound generators in practice; per-module CPU guard; typical slot count.
- Other templates not yet read: schwung-obxd, schwung-setbfree (RT guard test), moveforge (render/golden harness).
- Dev environment: this Windows machine has **no gcc, aarch64 cross-compiler, or Docker** installed.
