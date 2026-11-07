/*
 * Mean Module: Schwung sound generator (plugin_api_v2).
 * Stage 1: two PolyBLEP oscillators + noise, FM/x-mod, two RC ADSRs, LFO, vibrato, glide,
 * drift, legato last-note-priority mono voice. Filter/drive stages are NOT implemented yet.
 *
 * Realtime rules: render/on_midi/set_param/get_param never allocate, lock, log or do I/O.
 * Instances come from a static pool (MAX_INSTANCES), so no entry point allocates, including create/destroy.
 * Per-instance JSON is formatted once in create_instance with snprintf (no heap use).
 */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "plugin_api_v1.h"

#define SR 44100.0f
#define MAX_NOTES 16
#define MAX_INSTANCES 16     /* static pool; create_instance returns NULL when exhausted */
#define JSON_MAX 32768
#define OS 2                 /* oversampling factor for the oscillator/nonlinear core */
#define HB_N 47              /* halfband decimation FIR length (odd) */
#define HB_NZ 25             /* nonzero taps: center + 12 odd offsets each side */

/* ---------------------------------------------------------------- parameters */
typedef enum { K_F, K_I, K_E } kind_t;
#define WAVES "Off,Saw,Tri,Saw+Tri,Pulse,Saw+Pulse,Tri+Pulse,All Three"
#define OFFON "Off,On"
#define LFOW "Triangle,Square"
#define DRONEOPT "Off,Hold,Latch"
#define FMODES "Lowpass,Bandpass,Highpass,LP+HP,BP+LP"
#define ATDEST "Off,Cutoff,Vibrato,Drive,FM"
#define PRIO "Last,Low,High"
#define XTO "Bend,Vibrato Speed,Cutoff,Grit"
#define YTO "Glide,Vibrato Depth,Cutoff,Grit"
#define ARPMODE "Off,Up,Down,Up and Down,Random,As Played"
#define ARPRATE "1/4,1/8,1/8 Triplet,1/16,1/16 Triplet,1/32"

/* id, key, label, kind, min, max, default, step, options */
#define PARAMS(X) \
 X(OSC1_WAVE, "osc1_wave", "Osc 1 Wave", K_E, 0, 7, 1, 1, WAVES) \
 X(OSC1_SHAPE, "osc1_shape", "Osc 1 Shape", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(OSC2_WAVE, "osc2_wave", "Osc 2 Wave", K_E, 0, 7, 0, 1, WAVES) \
 X(OSC2_SHAPE, "osc2_shape", "Osc 2 Shape", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(OSC2_COARSE, "osc2_coarse", "Osc 2 Coarse", K_I, -24, 24, 0, 1, NULL) \
 X(OSC2_FINE, "osc2_fine", "Osc 2 Fine", K_F, -50, 50, 7, 1, NULL) \
 X(OSC1_LEVEL, "osc1_level", "Osc 1 Level", K_F, 0, 1, 0.8, 0.02, NULL) \
 X(OSC2_LEVEL, "osc2_level", "Osc 2 Level", K_F, 0, 1, 0.6, 0.02, NULL) \
 X(ENV1_A, "env1_a", "Env 1 Attack", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV1_D, "env1_d", "Env 1 Decay", K_F, 0, 1, 0.45, 0.02, NULL) \
 X(ENV1_S, "env1_s", "Env 1 Sustain", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV1_R, "env1_r", "Env 1 Release", K_F, 0, 1, 0.3, 0.02, NULL) \
 X(ENV2_A, "env2_a", "Env 2 Attack", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV2_D, "env2_d", "Env 2 Decay", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(ENV2_S, "env2_s", "Env 2 Sustain", K_F, 0, 1, 1.0, 0.02, NULL) \
 X(ENV2_R, "env2_r", "Env 2 Release", K_F, 0, 1, 0.35, 0.02, NULL) \
 X(OSC1_FM1, "osc1_fm1", "Osc 1 Env FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC2_FM1, "osc2_fm1", "Osc 2 Env FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC1_FM2, "osc1_fm2", "Osc 1 LFO FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC2_FM2, "osc2_fm2", "Osc 2 LFO FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(XMOD, "xmod_2to1", "X-Mod 2 to 1", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(SHAPE_MOD, "shape_mod", "LFO to Shape", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(LFO_RATE, "lfo_rate", "LFO Rate", K_F, 0, 1, 0.35, 0.02, NULL) \
 X(LFO_WAVE, "lfo_wave", "LFO Wave", K_E, 0, 1, 0, 1, LFOW) \
 X(CUTOFF, "cutoff", "Cutoff", K_F, 0, 1, 0.677, 0.01, NULL)  X(RESONANCE, "resonance", "Resonance", K_F, 0, 1, 0.3, 0.02, NULL)  X(FILTER_DRIVE, "filter_drive", "Filter Drive", K_F, 0, 1, 0.5, 0.02, NULL)  X(FILTER_MODE, "filter_mode", "Filter Mode", K_E, 0, 4, 0, 1, FMODES)  X(FLT_FM1, "flt_fm1", "Env 1 to Cutoff", K_F, -1, 1, 0.4, 0.02, NULL)  X(FLT_FM2, "flt_fm2", "LFO to Cutoff", K_F, 0, 1, 0.0, 0.02, NULL)  X(KEY_TRACK, "key_track", "Key Track", K_F, 0, 1, 0.5, 0.02, NULL)  X(DRIVE, "drive", "Drive", K_F, 0, 1, 0.2, 0.02, NULL)  X(NOISE_LEVEL, "noise_level", "Noise", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(GLIDE, "glide", "Glide", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(VIB_DEPTH, "vibrato_depth", "Vibrato Depth", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(BEND_RANGE, "bend_range", "Bend Range", K_I, 1, 12, 2, 1, NULL) \
 X(DRIFT, "drift", "Analog Drift", K_F, 0, 1, 0.3, 0.02, NULL) \
 X(LEGATO, "legato", "Legato", K_E, 0, 1, 0, 1, OFFON) \
 X(DRONE, "drone", "Drone", K_E, 0, 2, 0, 1, DRONEOPT) \
 X(VOLUME, "volume", "Volume", K_F, 0, 1, 0.7, 0.02, NULL) \
 X(VEL_CUTOFF, "vel_cutoff", "Velocity to Cutoff", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(VEL_AMP, "vel_amp", "Velocity to Amp", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(AT_DEST, "at_dest", "Aftertouch To", K_E, 0, 4, 2, 1, ATDEST) \
 X(AT_AMOUNT, "at_amount", "Aftertouch Amount", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(PRIORITY, "priority", "Note Priority", K_E, 0, 2, 0, 1, PRIO) \
 X(OCTAVE, "octave", "Octave", K_I, -2, 2, 0, 1, NULL) \
 X(VIB_RATE, "vib_rate", "Vibrato Rate", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(TUNE, "tune", "Tune", K_F, -50, 50, 0, 1, NULL) \
 X(GRIT, "grit", "Grit and Dark", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(STICK_X, "stick_x", "Stick Left-Right", K_F, -1, 1, 0.0, 0.05, NULL) \
 X(STICK_Y, "stick_y", "Stick Up-Down", K_F, -1, 1, 0.0, 0.05, NULL) \
 X(STICK_X_TO, "stick_x_to", "Stick X To", K_E, 0, 3, 0, 1, XTO) \
 X(STICK_Y_TO, "stick_y_to", "Stick Y To", K_E, 0, 3, 1, 1, YTO) \
 X(HOLD, "hold", "Hold", K_E, 0, 1, 0, 1, OFFON) \
 X(ARP_MODE, "arp_mode", "Arp Mode", K_E, 0, 5, 0, 1, ARPMODE) \
 X(ARP_RATE, "arp_rate", "Arp Rate", K_E, 0, 5, 3, 1, ARPRATE) \
 X(ARP_OCT, "arp_octaves", "Arp Octaves", K_I, 1, 3, 1, 1, NULL) \
 X(ARP_GATE, "arp_gate", "Arp Gate", K_F, 0.1, 1, 0.6, 0.05, NULL)

typedef struct { const char *key, *label; kind_t kind; float min, max, def, step; const char *opts; } pdef_t;
enum {
#define X(id, ...) P_##id,
    PARAMS(X)
#undef X
    P_COUNT
};
static const pdef_t PD[P_COUNT] = {
#define X(id, key, label, kind, mn, mx, df, st, opts) {key, label, kind, mn, mx, df, st, opts},
    PARAMS(X)
#undef X
};

static const char *const SHORT_NAME[P_COUNT] = {
    [P_OSC1_WAVE]="Wave1", [P_OSC1_SHAPE]="Shp1", [P_OSC2_WAVE]="Wave2", [P_OSC2_SHAPE]="Shp2", [P_OSC2_COARSE]="Semi", [P_OSC2_FINE]="Fine",
    [P_OSC1_LEVEL]="Lvl1", [P_OSC2_LEVEL]="Lvl2",
    [P_ENV1_A]="Atk", [P_ENV1_D]="Dec", [P_ENV1_S]="Sus", [P_ENV1_R]="Rel", [P_ENV2_A]="Atk", [P_ENV2_D]="Dec", [P_ENV2_S]="Sus", [P_ENV2_R]="Rel",
    [P_OSC1_FM1]="EFM 1", [P_OSC2_FM1]="EFM 2", [P_OSC1_FM2]="LFM 1", [P_OSC2_FM2]="LFM 2", [P_XMOD]="XMod", [P_SHAPE_MOD]="Shape",
    [P_LFO_RATE]="Rate", [P_LFO_WAVE]="Wave",
    [P_CUTOFF]="Cutoff", [P_RESONANCE]="Res", [P_FILTER_DRIVE]="Input", [P_FILTER_MODE]="Mode", [P_FLT_FM1]="Env", [P_FLT_FM2]="LFO",
    [P_KEY_TRACK]="Key", [P_DRIVE]="Drive",
    [P_NOISE_LEVEL]="Noise", [P_GLIDE]="Glide", [P_VIB_DEPTH]="Vib", [P_BEND_RANGE]="Bend", [P_DRIFT]="Drift", [P_LEGATO]="Legato",
    [P_DRONE]="Drone", [P_VOLUME]="Vol",
    [P_VEL_CUTOFF]="VCut", [P_VEL_AMP]="VAmp", [P_AT_DEST]="AT", [P_AT_AMOUNT]="Amt", [P_PRIORITY]="Prio", [P_OCTAVE]="Oct",
    [P_VIB_RATE]="VibRt", [P_TUNE]="Tune", [P_GRIT]="Grit",
    [P_STICK_X]="StkX", [P_STICK_Y]="StkY", [P_STICK_X_TO]="X To", [P_STICK_Y_TO]="Y To", [P_HOLD]="Hold",
    [P_ARP_MODE]="Arp", [P_ARP_RATE]="Rate", [P_ARP_OCT]="Oct", [P_ARP_GATE]="Gate",
};
/* Graphics: envelopes draw as ADSR curves, cutoff+resonance as a filter response, volume as a fader. A group only draws when its
 * params sit contiguously on one row of a page: Env 1 / Env 2 have their four knobs first, Filter and root have cutoff, resonance first. */
static const char *const VIZ_GROUP[P_COUNT] = {
    [P_ENV1_A]="fenv", [P_ENV1_D]="fenv", [P_ENV1_S]="fenv", [P_ENV1_R]="fenv",
    [P_ENV2_A]="amp",  [P_ENV2_D]="amp",  [P_ENV2_S]="amp",  [P_ENV2_R]="amp",
    [P_CUTOFF]="filter", [P_RESONANCE]="filter",
};
static const char *const VIZ_ROLE[P_COUNT] = {
    [P_ENV1_A]="attack", [P_ENV1_D]="decay", [P_ENV1_S]="sustain", [P_ENV1_R]="release",
    [P_ENV2_A]="attack", [P_ENV2_D]="decay", [P_ENV2_S]="sustain", [P_ENV2_R]="release",
    [P_CUTOFF]="cutoff", [P_RESONANCE]="resonance",
};
static const char *const VIZ_KIND[P_COUNT] = { [P_VOLUME]="fader" };
static const char *const SHORT_OPTS[P_COUNT] = {
    [P_OSC1_WAVE]="OFF,SAW,TRI,S+T,PLS,S+P,T+P,ALL", [P_OSC2_WAVE]="OFF,SAW,TRI,S+T,PLS,S+P,T+P,ALL", [P_LFO_WAVE]="TRI,SQR",
    [P_FILTER_MODE]="LP,BP,HP,L+H,B+L", [P_LEGATO]="OFF,ON", [P_DRONE]="OFF,HLD,LTC", [P_AT_DEST]="OFF,CUT,VIB,DRV,FM", [P_PRIORITY]="LST,LOW,HI",
    [P_STICK_X_TO]="BND,VSP,CUT,GRT", [P_STICK_Y_TO]="GLD,VDP,CUT,GRT", [P_HOLD]="OFF,ON",
    [P_ARP_MODE]="OFF,UP,DWN,U+D,RND,ASP", [P_ARP_RATE]="1/4,1/8,8T,16,16T,32",
};

/* ---------------------------------------------------------------- DSP state */
enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_SUS, ENV_REL };
typedef struct { int stage; float level; } env_t;
typedef struct { float phase; float drift; } osc_t;

typedef struct {
    float p[P_COUNT];
    char ui_json[JSON_MAX];
    char chain_json[JSON_MAX];
    int ui_len, chain_len;

    /* voice */
    int notes[MAX_NOTES], vels[MAX_NOTES], n_notes;
    int gate;
    float cur_semi, tgt_semi;   /* glided pitch in MIDI semitones */
    float vel;
    float bend;                 /* semitones */
    float at, modwheel;         /* 0..1 */
    osc_t o1, o2;
    float o2_prev;
    env_t e1, e2;
    float lfo_ph, vib_ph;
    uint32_t rng;
    float dc_x, dc_y;
    float noise_lp1, noise_lp2;
    int preset;
    uint8_t phys[128]; int phys_n;       /* keys physically down */
    int held[MAX_NOTES], held_vel[MAX_NOTES], held_n;   /* notes held by keys, pedal or Hold (the arp / voice source) */
    int prev_arp, prev_hold;
    int arp_running, arp_idx, arp_note; float arp_pos;
    int touch_seen; uint8_t touched[2]; int stick_age[2]; int y_down;   /* joystick emulation */
    int sustain;                /* CC64 pedal down */
    uint8_t pend_off[128];      /* note-offs deferred by the pedal */
    int latched;                /* drone LATCH: sound holds after note-off until panic / preset / drone change */
    int was_idle;               /* voice idle (silent): render_block skips all DSP */
    int cur_note;               /* sounding note after priority resolution, -1 when none */
    float f_s1, f_s2, f_s3;                   /* Sallen-Key (Korg35-style) TPT states, at OS rate */
    float hb[HB_N]; int hb_pos;          /* decimator history ring */
    float tap_val[HB_NZ]; int tap_idx[HB_NZ];
} mm_t;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float frand(uint32_t *s) {  /* xorshift32 -> [-1,1) */
    uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; *s = x;
    return (float)(int32_t)x * (1.0f / 2147483648.0f);
}
static inline float env_time(float x) { return 0.0005f * powf(20000.0f, x); }  /* 0.5 ms .. 10 s */

static inline float poly_blep(float t, float dt) {
    if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
    if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
    return 0.0f;
}

/* Rational phase warp: k=1 identity; k>1 bows the ramp down, k<1 up. Cheap, no pow per sample. */
static inline float warp(float ph, float k) { return ph / (k + (1.0f - k) * ph); }

/* One oscillator sample. wave is a bitmask (1 saw, 2 tri, 4 pulse). */
static inline float osc_wave(float ph, float dt, int wave, float shape, float warp_k) {
    float out = 0.0f;
    int n = 0;
    if (wave & 1) {  /* saw, shape bows the ramp */
        float y = 2.0f * warp(ph, warp_k) - 1.0f;
        out += y - poly_blep(ph, dt);
        n++;
    }
    if (wave & 2) {  /* skewed triangle: breakpoint follows shape */
        float w = clampf(shape, 0.05f, 0.95f);
        float y = ph < w ? (-1.0f + 2.0f * ph / w) : (1.0f - 2.0f * (ph - w) / (1.0f - w));
        out += y;
        n++;
    }
    if (wave & 4) {  /* pulse, shape = width */
        float w = 0.05f + 0.9f * shape;
        float y = ph < w ? 1.0f : -1.0f;
        y += poly_blep(ph, dt);
        float t2 = ph - w; if (t2 < 0.0f) t2 += 1.0f;
        y -= poly_blep(t2, dt);
        out += y;
        n++;
    }
    return n ? out * (n == 1 ? 1.0f : (n == 2 ? 0.7f : 0.58f)) : 0.0f;
}

static float env_step(env_t *e, float a, float d, float s, float r) {
    /* a,d,r are per-sample coefficients (1-exp(-1/tau)). Attack aims at 1.3 so it crosses 1 in finite time. */
    switch (e->stage) {
    case ENV_ATK:
        e->level += (1.3f - e->level) * a;
        if (e->level >= 1.0f) { e->level = 1.0f; e->stage = ENV_DEC; }
        break;
    case ENV_DEC:
        e->level += (s - e->level) * d;
        if (e->level - s < 1e-4f) { e->level = s; e->stage = ENV_SUS; }
        break;
    case ENV_SUS: e->level = s; break;
    case ENV_REL:
        e->level -= e->level * r;
        if (e->level < 1e-4f) { e->level = 0.0f; e->stage = ENV_IDLE; }
        break;
    default: e->level = 0.0f;
    }
    return e->level;
}
static inline float coef(float t_sec, float divisor) { return 1.0f - expf(-divisor / (t_sec * SR)); }

/* ---------------------------------------------------------------- voice control */
static void gate_on(mm_t *m, int retrigger) {
    m->gate = 1;
    if (retrigger) {
        m->e1.stage = ENV_ATK; m->e2.stage = ENV_ATK;  /* retrigger from current level */
    }
}
static void remove_note(mm_t *m, int note) {
    for (int i = 0; i < m->n_notes; i++)
        if (m->notes[i] == note) {
            memmove(&m->notes[i], &m->notes[i + 1], (size_t)(m->n_notes - i - 1) * sizeof(int));
            memmove(&m->vels[i], &m->vels[i + 1], (size_t)(m->n_notes - i - 1) * sizeof(int));
            m->n_notes--;
            return;
        }
}
/* Index of the held note that wins under the current priority (n_notes > 0). */
static int pick_idx(const mm_t *m) {
    int prio = (int)m->p[P_PRIORITY], idx = m->n_notes - 1;
    for (int i = 0; i < m->n_notes; i++) {
        if (prio == 1 && m->notes[i] < m->notes[idx]) idx = i;
        if (prio == 2 && m->notes[i] > m->notes[idx]) idx = i;
    }
    return idx;
}
static void voice_on(mm_t *m, int note, int vel) {
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    remove_note(m, note);
    if (m->n_notes == MAX_NOTES) remove_note(m, m->notes[0]);
    int was_gate = m->gate;
    m->latched = ((int)m->p[P_DRONE] == 2);
    m->notes[m->n_notes] = note; m->vels[m->n_notes++] = vel;
    int idx = pick_idx(m), winner = m->notes[idx];
    if (was_gate && winner == m->cur_note) return;   /* lower-priority note: no pitch change, no retrigger */
    m->cur_note = winner; m->vel = m->vels[idx] / 127.0f; m->tgt_semi = (float)winner;
    /* glide only between overlapping notes or while a tail is still sounding; fresh notes start at pitch */
    if (!was_gate && m->e2.stage == ENV_IDLE) m->cur_semi = m->tgt_semi;
    gate_on(m, !(was_gate && m->p[P_LEGATO] >= 0.5f));
}
static void release_envs(mm_t *m) {
    if (m->e1.stage != ENV_IDLE) m->e1.stage = ENV_REL;
    if (m->e2.stage != ENV_IDLE) m->e2.stage = ENV_REL;
}
static void gate_off(mm_t *m) {
    m->gate = 0; m->cur_note = -1;
    if (!m->latched) release_envs(m);       /* LATCH keeps the envelopes sustaining */
}
/* Drop a drone latch (preset change, drone mode change, panic) and let the sound release. */
static void unlatch(mm_t *m) {
    if (!m->latched) return;
    m->latched = 0;
    if (!m->gate) release_envs(m);
}
static void voice_off(mm_t *m, int note) {
    remove_note(m, note);
    if (m->n_notes > 0) {  /* fall back to the winning held note */
        int idx = pick_idx(m), winner = m->notes[idx];
        if (winner != m->cur_note) {
            m->cur_note = winner; m->vel = m->vels[idx] / 127.0f; m->tgt_semi = (float)winner;
            if (m->p[P_LEGATO] < 0.5f) gate_on(m, 1);
        }
    } else if (m->gate) {
        gate_off(m);
    }
}

/* ---- input layer: keys, sustain pedal and Hold decide which notes are "held"; the held set feeds the voice or the arp. */
static void held_remove(mm_t *m, int note) {
    for (int i = 0; i < m->held_n; i++)
        if (m->held[i] == note) {
            memmove(&m->held[i], &m->held[i + 1], (size_t)(m->held_n - i - 1) * sizeof(int));
            memmove(&m->held_vel[i], &m->held_vel[i + 1], (size_t)(m->held_n - i - 1) * sizeof(int));
            m->held_n--;
            return;
        }
}
static void sink_off(mm_t *m, int note) { if ((int)m->p[P_ARP_MODE] == 0) voice_off(m, note); }
static void held_add(mm_t *m, int note, int vel) {
    held_remove(m, note);
    if (m->held_n == MAX_NOTES) { int old = m->held[0]; held_remove(m, old); sink_off(m, old); }
    m->held[m->held_n] = note; m->held_vel[m->held_n++] = vel;
}
static void in_on(mm_t *m, int note, int vel) {
    if (note < 0 || note > 127) return;
    if (!m->phys[note]) { m->phys[note] = 1; m->phys_n++; }
    m->pend_off[note] = 0;
    if ((int)m->p[P_HOLD] == 1 && m->phys_n == 1) {   /* first key of a new gesture replaces the latched notes */
        while (m->held_n > 0) { int old = m->held[0]; held_remove(m, old); sink_off(m, old); }
    }
    held_add(m, note, vel);
    if ((int)m->p[P_ARP_MODE] == 0) voice_on(m, note, vel);
}
static void in_off(mm_t *m, int note) {
    if (note < 0 || note > 127) return;
    if (m->phys[note]) { m->phys[note] = 0; m->phys_n--; }
    if (m->sustain || (int)m->p[P_HOLD] == 1) { m->pend_off[note] = 1; return; }   /* pedal or Hold keeps it sounding */
    held_remove(m, note);
    sink_off(m, note);
}
static void release_pending(mm_t *m) {
    for (int n = 0; n < 128; n++)
        if (m->pend_off[n]) {
            m->pend_off[n] = 0;
            if (!m->phys[n]) { held_remove(m, n); sink_off(m, n); }
        }
}
static void pedal(mm_t *m, int down) {
    m->sustain = down;
    if (!down && (int)m->p[P_HOLD] == 0) release_pending(m);
}

/* ---- arpeggiator: reads the held set, drives the voice one step at a time (clock: host tempo, free-running) */
static int arp_build(mm_t *m, int *seq) {   /* returns pattern length (<= 3 * MAX_NOTES) */
    int tmp[MAX_NOTES], n = m->held_n, mode = (int)m->p[P_ARP_MODE], oct = (int)m->p[P_ARP_OCT], len = 0;
    if (n == 0) return 0;
    for (int i = 0; i < n; i++) tmp[i] = m->held[i];
    if (mode != 5) for (int i = 1; i < n; i++) { int v = tmp[i], j = i - 1; while (j >= 0 && tmp[j] > v) { tmp[j + 1] = tmp[j]; j--; } tmp[j + 1] = v; }
    int up[3 * MAX_NOTES], ul = 0;
    for (int o = 0; o < oct; o++) for (int i = 0; i < n; i++) { int v = tmp[i] + 12 * o; up[ul++] = v > 127 ? 127 : v; }
    if (mode == 2) { for (int i = ul - 1; i >= 0; i--) seq[len++] = up[i]; }
    else if (mode == 3) { for (int i = 0; i < ul; i++) seq[len++] = up[i]; for (int i = ul - 2; i >= 1; i--) seq[len++] = up[i]; }
    else { for (int i = 0; i < ul; i++) seq[len++] = up[i]; }
    return len;
}
static void arp_stop(mm_t *m) {
    m->arp_running = 0;
    if (m->arp_note >= 0) { voice_off(m, m->arp_note); m->arp_note = -1; }
}
static void arp_step(mm_t *m) {
    int seq[3 * MAX_NOTES + 2], len = arp_build(m, seq);
    if (len == 0) { arp_stop(m); return; }
    int i;
    if ((int)m->p[P_ARP_MODE] == 4) {   /* random */
        uint32_t x = m->rng; x ^= x << 13; x ^= x >> 17; x ^= x << 5; m->rng = x;
        i = (int)(x % (uint32_t)len);
    } else {
        i = m->arp_idx % len; m->arp_idx = i + 1;
    }
    int note = seq[i], vel = m->held_vel[m->held_n - 1];
    int tie = m->p[P_ARP_GATE] >= 0.999f;
    if (!tie && m->arp_note >= 0) voice_off(m, m->arp_note);
    else if (tie && m->arp_note >= 0 && m->arp_note != note) { int old = m->arp_note; voice_on(m, note, vel); voice_off(m, old); m->arp_note = note; return; }
    voice_on(m, note, vel);
    m->arp_note = note;
}
static void arp_gate_off(mm_t *m) {
    if (m->arp_note >= 0) { voice_off(m, m->arp_note); m->arp_note = -1; }
}

/* Mode switches (arp on/off, Hold on/off) take effect here, wherever the param came from (knob, preset, state). */
static void sync_modes(mm_t *m) {
    int am = (int)m->p[P_ARP_MODE];
    if (am != m->prev_arp) {
        if (am == 0) {                       /* arp off: voice takes over the held notes again */
            arp_stop(m);
            m->n_notes = 0;
            for (int i = 0; i < m->held_n; i++) voice_on(m, m->held[i], m->held_vel[i]);
        } else if (m->prev_arp == 0) {       /* arp on: the arp now owns the voice */
            m->n_notes = 0; m->arp_running = 0; m->arp_idx = 0;
            if (m->gate) gate_off(m);
        }
        m->prev_arp = am;
    }
    int hd = (int)m->p[P_HOLD];
    if (hd != m->prev_hold) {
        m->prev_hold = hd;
        if (hd == 0 && !m->sustain) release_pending(m);
    }
}
static void panic(mm_t *m) {
    memset(m->pend_off, 0, sizeof m->pend_off); memset(m->phys, 0, sizeof m->phys);
    m->phys_n = 0; m->held_n = 0; m->sustain = 0; m->n_notes = 0; m->latched = 0;
    m->arp_running = 0; m->arp_note = -1;
    gate_off(m);
}

/* ---------------------------------------------------------------- factory presets (compiled in: no file I/O)
 * Each preset starts from the Init defaults and overrides a few params. Voiced from reasoning about the
 * architecture, NOT by ear or against hardware recordings: expect to retune. Values are normalised params:
 * cutoff x -> 20*2^(9.81x) Hz (0.24=100Hz .44=400 .54=800 .64=1.6k .74=3.2k .84=6.4k); env x -> 0.5ms*20000^x
 * (.3=10ms .5=70ms .6=190ms .7=0.5s .8=1.4s .9=3.7s); lfo x -> 0.05*800^x Hz (.35=0.5Hz .5=1.4Hz .65=3.8Hz .8=10Hz). */
typedef struct { int id; float v; } pv_t;
typedef struct { const char *name; const pv_t *v; int n; } preset_t;
#define PRE(name, ...) { name, (const pv_t[]){ __VA_ARGS__ }, (int)(sizeof((const pv_t[]){ __VA_ARGS__ }) / sizeof(pv_t)) }
static const preset_t PRESETS[] = {
    PRE("Init", {P_VOLUME, 0.7f}),
    PRE("Bass Acid Line",{P_CUTOFF,.35f},{P_RESONANCE,.6f},{P_FLT_FM1,.8f},{P_ENV1_D,.6f},{P_ENV2_D,.68f},{P_ENV2_S,.35f},
        {P_ENV2_R,.25f},{P_GLIDE,.15f},{P_LEGATO,1},{P_DRIVE,.4f}),
    PRE("Bass Acid Squelch",{P_OSC2_LEVEL,0},{P_CUTOFF,.28f},{P_RESONANCE,.78f},{P_FLT_FM1,1},{P_ENV1_D,.5f},{P_ENV2_D,.6f},
        {P_ENV2_S,.7f},{P_ENV2_R,.2f},{P_GLIDE,.12f},{P_LEGATO,1}),
    PRE("Bass Brute",{P_OSC1_WAVE,7},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,7},{P_OSC2_FINE,7},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.45f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,1},{P_DRIVE,1},{P_FLT_FM1,0.3f}),
    PRE("Bass Chewy",{P_FILTER_MODE,4},{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.5f},{P_OSC2_COARSE,-12},{P_CUTOFF,0.38f},
        {P_RESONANCE,0.6f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM2,0.45f},{P_LFO_RATE,0.55f}),
    PRE("Bass Dirty Sub",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,0.9f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.2f},{P_OSC2_COARSE,-12},
        {P_OSC2_LEVEL,0.4f},{P_CUTOFF,0.35f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.8f},{P_ENV2_R,0.3f}),
    PRE("Bass Dub Sub",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,.9f},{P_OSC2_LEVEL,0},{P_CUTOFF,.36f},{P_RESONANCE,.25f},{P_FLT_FM1,.1f},
        {P_ENV2_A,.1f},{P_ENV2_R,.4f}),
    PRE("Bass Fat Pulse",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,-12},
        {P_OSC2_FINE,6},{P_CUTOFF,.35f},{P_RESONANCE,.25f},{P_FLT_FM1,.6f}),
    PRE("Bass Funk",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.25f},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,.6f},{P_FILTER_MODE,4},
        {P_CUTOFF,.33f},{P_RESONANCE,.45f},{P_FLT_FM1,.85f},{P_ENV1_D,.42f},{P_ENV1_S,0},{P_ENV2_D,.55f},{P_ENV2_S,.6f}),
    PRE("Bass Fuzz Mono",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,5},{P_CUTOFF,0.5f},{P_RESONANCE,0.25f},
        {P_FILTER_DRIVE,0.95f},{P_DRIVE,0.95f},{P_GLIDE,0.12f},{P_LEGATO,1},{P_PRIORITY,1},{P_FLT_FM1,0.35f}),
    PRE("Bass Gnarl",{P_OSC1_WAVE,5},{P_OSC1_SHAPE,0.4f},{P_OSC2_WAVE,1},{P_OSC2_FINE,14},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.34f},
        {P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.7f},{P_FLT_FM1,0.6f},{P_ENV1_D,0.5f},{P_ENV1_S,0.1f},
        {P_ENV2_D,0.6f},{P_ENV2_S,0.8f}),
    PRE("Bass Gravel",{P_OSC1_WAVE,7},{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.6f},{P_NOISE_LEVEL,0.18f},
        {P_FILTER_MODE,4},{P_CUTOFF,0.42f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.75f},{P_DRIVE,0.8f},{P_FLT_FM1,0.35f}),
    PRE("Bass Grind",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,6},{P_OSC2_LEVEL,0.9f},{P_XMOD,0.7f},
        {P_CUTOFF,0.36f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.75f},{P_FLT_FM1,0.5f}),
    PRE("Bass Hollow Sub",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.5f},
        {P_RESONANCE,0},{P_FILTER_DRIVE,.3f}),
    PRE("Bass Mean Acid",{P_OSC2_LEVEL,0},{P_CUTOFF,0.26f},{P_RESONANCE,0.82f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,1},
        {P_ENV1_D,0.55f},{P_ENV1_S,0.05f},{P_ENV2_D,0.6f},{P_ENV2_S,0.7f},{P_ENV2_R,0.2f},{P_GLIDE,0.12f}),
    PRE("Bass Overdriven",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_CUTOFF,.5f},{P_RESONANCE,.35f},{P_FILTER_DRIVE,.9f},
        {P_DRIVE,.9f},{P_FLT_FM1,.3f},{P_ENV1_D,.5f}),
    PRE("Bass Pulse Walker",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.35f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.6f},{P_OSC2_COARSE,-12},
        {P_OSC2_FINE,4},{P_CUTOFF,.4f},{P_RESONANCE,.3f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV1_S,.2f},{P_PRIORITY,1}),
    PRE("Bass Rubber",{P_OSC1_WAVE,4},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_GLIDE,.2f},{P_LEGATO,1},
        {P_CUTOFF,.3f},{P_RESONANCE,.35f},{P_FLT_FM1,.7f}),
    PRE("Bass Rust",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_OSC2_WAVE,1},{P_OSC2_FINE,-11},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.4f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_NOISE_LEVEL,0.06f},{P_FLT_FM1,0.4f},{P_ENV1_D,0.55f},
        {P_ENV2_S,0.85f}),
    PRE("Bass Snarl",{P_OSC1_WAVE,5},{P_OSC1_SHAPE,.45f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,.30f},{P_RESONANCE,.5f},
        {P_FILTER_DRIVE,.6f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV2_D,.6f},{P_ENV2_S,.8f}),
    PRE("Bass Sub Saw",{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.38f},
        {P_RESONANCE,.2f},{P_FLT_FM1,.4f},{P_ENV1_D,.5f},{P_ENV2_R,.2f}),
    PRE("Bass Sync Growl",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_XMOD,.55f},{P_CUTOFF,.42f},
        {P_RESONANCE,.4f},{P_FLT_FM1,.4f},{P_ENV1_D,.55f},{P_FLT_FM2,.3f}),
    PRE("Bass Wobble",{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.3f},{P_RESONANCE,.55f},
        {P_FLT_FM2,.8f},{P_LFO_RATE,.6f}),
    PRE("Lead Broken Radio",{P_FILTER_MODE,1},{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,18},{P_NOISE_LEVEL,0.12f},
        {P_CUTOFF,0.62f},{P_RESONANCE,0.6f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.7f}),
    PRE("Lead Bubble",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_VIB_DEPTH,.3f},{P_OSC1_FM2,.15f},
        {P_LFO_RATE,.7f},{P_GLIDE,.3f},{P_LEGATO,1},{P_CUTOFF,.6f},{P_RESONANCE,.4f}),
    PRE("Lead Burn",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,4},{P_CUTOFF,0.56f},{P_RESONANCE,0.65f},
        {P_FILTER_DRIVE,1},{P_DRIVE,0.85f},{P_FLT_FM1,0.35f},{P_ENV1_D,0.6f},{P_ENV1_S,0.35f},{P_AT_DEST,1},{P_AT_AMOUNT,0.7f},
        {P_LEGATO,1}),
    PRE("Lead Dirty Square",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.35f},{P_OSC2_FINE,12},
        {P_CUTOFF,0.6f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.7f},{P_SHAPE_MOD,0.3f}),
    PRE("Lead Dive",{P_OSC1_WAVE,5},{P_OSC2_LEVEL,0},{P_OSC1_FM1,.35f},{P_ENV1_D,.36f},{P_ENV1_S,0},{P_CUTOFF,.7f},
        {P_RESONANCE,.35f},{P_FLT_FM1,.2f},{P_ENV2_S,.9f},{P_ENV2_R,.3f}),
    PRE("Lead Expressive",{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.5f},{P_RESONANCE,.45f},{P_FLT_FM1,.2f},
        {P_ENV1_D,.6f},{P_ENV1_S,.4f},{P_VEL_CUTOFF,.7f},{P_VEL_AMP,.4f},{P_AT_DEST,1},{P_AT_AMOUNT,.8f}),
    PRE("Lead FM Zap",{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_XMOD,.8f},{P_OSC1_FM1,.9f},{P_ENV1_D,.55f},
        {P_ENV2_D,.7f},{P_ENV2_S,.4f},{P_ENV2_R,.3f},{P_CUTOFF,.8f}),
    PRE("Lead Fifth",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.66f},
        {P_RESONANCE,.25f},{P_FLT_FM1,.25f},{P_ENV1_D,.55f},{P_ENV1_S,.4f},{P_GLIDE,.12f}),
    PRE("Lead Fuzz",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_CUTOFF,.7f},{P_RESONANCE,.3f},{P_FILTER_DRIVE,.95f},
        {P_DRIVE,.8f},{P_FLT_FM1,.2f},{P_ENV1_D,.6f},{P_ENV1_S,.5f},{P_GLIDE,.18f}),
    PRE("Lead Grit Fifth",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,0},{P_OSC2_LEVEL,0.75f},{P_XMOD,0.3f},
        {P_CUTOFF,0.6f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.7f},{P_NOISE_LEVEL,0.04f}),
    PRE("Lead Laser Sweep",{P_OSC2_LEVEL,0},{P_CUTOFF,.85f},{P_FLT_FM1,-.8f},{P_ENV1_D,.6f},{P_ENV1_S,0},{P_RESONANCE,.5f}),
    PRE("Lead Mono Sync",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,2},{P_XMOD,.7f},{P_CUTOFF,.68f},
        {P_RESONANCE,.3f},{P_FLT_FM1,.3f},{P_ENV1_D,.6f},{P_ENV1_S,.4f},{P_VIB_DEPTH,.2f},{P_GLIDE,.15f}),
    PRE("Lead Rasp",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.62f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},
        {P_DRIVE,0.6f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.6f},{P_ENV1_S,0.4f},{P_VIB_DEPTH,0.18f}),
    PRE("Lead Scream Hot",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_CUTOFF,0.7f},{P_RESONANCE,0.72f},
        {P_FILTER_DRIVE,0.95f},{P_DRIVE,0.8f},{P_FLT_FM1,0.25f},{P_GLIDE,0.2f},{P_LEGATO,1},{P_VIB_DEPTH,0.25f},
        {P_VIB_RATE,0.65f}),
    PRE("Lead Screamer",{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.62f},{P_RESONANCE,.7f},{P_FILTER_DRIVE,.3f},
        {P_GLIDE,.25f},{P_VIB_DEPTH,.25f},{P_ENV2_R,.3f},{P_LEGATO,1}),
    PRE("Lead Siren",{P_OSC1_WAVE,2},{P_OSC2_LEVEL,0},{P_OSC1_FM2,.5f},{P_LFO_RATE,.55f},{P_CUTOFF,.7f},{P_RESONANCE,.3f},
        {P_FLT_FM1,0},{P_DRONE,0},{P_ENV2_A,.3f}),
    PRE("Lead Snarl Sync",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,3},{P_XMOD,0.85f},{P_CUTOFF,0.65f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_OSC2_FM1,0.1f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.55f}),
    PRE("Lead Soft Tri",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.4f},{P_CUTOFF,.62f},{P_RESONANCE,.15f},
        {P_FLT_FM1,.2f},{P_ENV1_D,.6f},{P_ENV1_S,.5f},{P_ENV2_A,.2f},{P_ENV2_R,.4f},{P_GLIDE,.2f},{P_LEGATO,1}),
    PRE("Lead Whistle",{P_OSC1_LEVEL,.05f},{P_OSC2_LEVEL,0},{P_RESONANCE,1},{P_CUTOFF,.7f},{P_KEY_TRACK,1},{P_FLT_FM1,0},
        {P_FILTER_DRIVE,.3f},{P_VIB_DEPTH,.2f},{P_GLIDE,.15f}),
    PRE("Keys Bell Metal",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,0},{P_XMOD,.6f},{P_OSC1_FM1,.3f},
        {P_ENV1_D,.5f},{P_ENV2_D,.75f},{P_ENV2_S,0},{P_ENV2_R,.6f},{P_CUTOFF,.8f}),
    PRE("Keys Bounce",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.4f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,.4f},{P_CUTOFF,.5f},
        {P_RESONANCE,.4f},{P_FLT_FM1,.6f},{P_ENV1_D,.58f},{P_ENV2_D,.66f}),
    PRE("Keys Broken Piano",{P_OSC1_WAVE,5},{P_OSC1_SHAPE,0.3f},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.4f},
        {P_CUTOFF,0.5f},{P_RESONANCE,0.2f},{P_FILTER_DRIVE,0.75f},{P_DRIVE,0.6f},{P_FLT_FM1,0.5f},{P_ENV1_D,0.6f},
        {P_ENV2_D,0.72f},{P_ENV2_S,0.2f},{P_ENV2_R,0.35f},{P_SHAPE_MOD,0.2f}),
    PRE("Keys Crunch Stab",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_CUTOFF,0.46f},{P_RESONANCE,0.3f},
        {P_FILTER_DRIVE,0.85f},{P_DRIVE,0.75f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.3f},{P_ENV1_D,0.55f},{P_ENV1_S,0.3f},{P_ENV2_D,0.66f},
        {P_ENV2_S,0.45f},{P_ENV2_R,0.2f}),
    PRE("Keys Dirty Clav",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.2f},{P_OSC2_LEVEL,0},{P_FILTER_MODE,4},{P_CUTOFF,.35f},{P_RESONANCE,.3f},
        {P_FLT_FM1,.8f},{P_ENV1_D,.55f},{P_ENV2_D,.66f}),
    PRE("Keys Dirty Organ",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.5f},{P_OSC2_WAVE,4},{P_OSC2_COARSE,12},{P_OSC2_SHAPE,0.5f},
        {P_OSC2_FINE,0},{P_OSC2_LEVEL,0.6f},{P_CUTOFF,0.62f},{P_RESONANCE,0.15f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},
        {P_VIB_DEPTH,0.2f},{P_VIB_RATE,0.55f},{P_ENV2_A,0.2f}),
    PRE("Keys Grit Bell",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,5},{P_XMOD,0.5f},{P_CUTOFF,0.7f},
        {P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_ENV2_D,0.75f},{P_ENV2_S,0},{P_ENV2_R,0.5f},{P_ENV1_D,0.6f}),
    PRE("Keys Gritty Clav",{P_FILTER_MODE,4},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.2f},{P_OSC2_LEVEL,0},{P_CUTOFF,0.36f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.8f},{P_ENV1_D,0.55f},{P_ENV2_D,0.68f}),
    PRE("Keys Mallet",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.3f},{P_CUTOFF,.7f},
        {P_RESONANCE,.15f},{P_FLT_FM1,.3f},{P_ENV1_D,.5f},{P_ENV1_S,0},{P_ENV2_D,.72f},{P_ENV2_S,.15f}),
    PRE("Keys Metal Clang",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,5},{P_OSC2_FINE,40},{P_XMOD,1},{P_OSC1_FM2,.3f},
        {P_LFO_RATE,.8f},{P_CUTOFF,.9f},{P_RESONANCE,.3f},{P_ENV2_D,.72f},{P_ENV2_S,.2f},{P_ENV2_R,.5f}),
    PRE("Keys Pluck",{P_OSC2_LEVEL,0},{P_CUTOFF,.4f},{P_RESONANCE,.5f},{P_FLT_FM1,.85f},{P_ENV1_D,.62f},{P_ENV2_D,.7f},
        {P_ENV2_S,.3f},{P_ENV2_R,.35f}),
    PRE("Keys Reso Pluck",{P_OSC2_LEVEL,0},{P_CUTOFF,.3f},{P_RESONANCE,.65f},{P_FLT_FM1,.9f},{P_ENV1_D,.6f},{P_ENV2_D,.68f}),
    PRE("Keys Rusty Pluck",{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.4f},{P_CUTOFF,0.34f},
        {P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.55f},{P_FLT_FM1,0.9f},{P_ENV1_D,0.6f},{P_ENV1_S,0.05f},
        {P_ENV2_D,0.7f},{P_ENV2_S,0.25f}),
    PRE("Keys Soft",{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.3f},{P_OSC2_LEVEL,.35f},{P_CUTOFF,.58f},{P_RESONANCE,.1f},
        {P_FLT_FM1,.4f},{P_ENV1_D,.55f},{P_ENV1_S,.1f},{P_ENV2_D,.7f},{P_ENV2_S,.3f},{P_ENV2_R,.4f}),
    PRE("Keys Trance Pluck",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,.45f},{P_RESONANCE,.45f},{P_FLT_FM1,.7f},
        {P_ENV1_D,.6f},{P_ENV2_D,.68f}),
    PRE("Brass Fanfare",{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.4f},{P_OSC2_FINE,8},{P_OSC2_LEVEL,0.6f},{P_CUTOFF,0.52f},
        {P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM1,0.5f},{P_ENV1_A,0.36f},{P_ENV1_D,0.55f},
        {P_ENV1_S,0.55f},{P_ENV2_A,0.28f},{P_ENV2_S,0.85f},{P_ENV2_R,0.28f},{P_VIB_DEPTH,0.15f}),
    PRE("Brass Fat Stab",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.38f},
        {P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.8f},{P_ENV1_A,0.32f},{P_ENV1_D,0.55f},
        {P_ENV1_S,0.25f},{P_ENV2_A,0.25f},{P_ENV2_D,0.62f},{P_ENV2_S,0.6f},{P_ENV2_R,0.25f},{P_VEL_AMP,0.5f}),
    PRE("Brass Gritty Solo",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_CUTOFF,0.42f},{P_RESONANCE,0.3f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.75f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.45f},{P_ENV2_A,0.3f},
        {P_ENV2_S,0.9f},{P_ENV2_R,0.3f},{P_GLIDE,0.12f},{P_LEGATO,1},{P_VIB_DEPTH,0.2f},{P_VIB_RATE,0.6f},{P_AT_DEST,1},
        {P_AT_AMOUNT,0.6f}),
    PRE("Brass Low Horn",{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_FINE,4},{P_OSC2_LEVEL,0.6f},{P_CUTOFF,0.32f},{P_RESONANCE,0.15f},
        {P_FILTER_DRIVE,0.6f},{P_DRIVE,0.4f},{P_FLT_FM1,0.7f},{P_ENV1_A,0.48f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.38f},
        {P_ENV2_S,0.9f},{P_ENV2_R,0.35f},{P_OCTAVE,-1}),
    PRE("Brass Mean Section",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.4f},
        {P_RESONANCE,0.2f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.5f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},
        {P_ENV2_A,0.35f},{P_ENV2_S,0.9f},{P_ENV2_R,0.3f},{P_VIB_DEPTH,0.1f},{P_VIB_RATE,0.55f}),
    PRE("Brass Reedy",{P_FILTER_MODE,4},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.35f},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_OSC2_LEVEL,0.6f},
        {P_CUTOFF,0.5f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.75f},{P_DRIVE,0.5f},{P_FLT_FM1,0.45f},{P_ENV1_A,0.42f},
        {P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_ENV2_R,0.28f}),
    PRE("Brass Slow Swell",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_CUTOFF,0.34f},{P_RESONANCE,0.2f},
        {P_FILTER_DRIVE,0.65f},{P_DRIVE,0.4f},{P_FLT_FM1,0.85f},{P_ENV1_A,0.72f},{P_ENV1_D,0.75f},{P_ENV1_S,0.7f},
        {P_ENV2_A,0.62f},{P_ENV2_S,1},{P_ENV2_R,0.5f},{P_VIB_DEPTH,0.15f}),
    PRE("Brass Stab",{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_ENV1_A,.35f},{P_ENV1_D,.5f},{P_ENV1_S,.4f},{P_FLT_FM1,.6f},
        {P_CUTOFF,.45f},{P_RESONANCE,.2f},{P_ENV2_A,.2f},{P_ENV2_D,.5f}),
    PRE("Brass Sync Horn",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_XMOD,0.5f},{P_CUTOFF,0.46f},
        {P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.4f},
        {P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_ENV2_R,0.3f},{P_GLIDE,0.1f},{P_LEGATO,1}),
    PRE("Arp Acid Run",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,0.55f},{P_OSC2_LEVEL,0},{P_CUTOFF,0.3f},
        {P_RESONANCE,0.75f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},{P_FLT_FM1,1},{P_ENV1_D,0.5f},{P_ENV1_S,0.05f},{P_ENV2_D,0.55f},
        {P_ENV2_S,0.6f},{P_ENV2_R,0.2f},{P_VEL_CUTOFF,0.5f}),
    PRE("Arp Bass Walk",{P_ARP_MODE,5},{P_ARP_RATE,1},{P_ARP_OCT,1},{P_ARP_GATE,0.7f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},
        {P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.34f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},
        {P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_D,0.5f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f}),
    PRE("Arp Bell Cascade",{P_ARP_MODE,2},{P_ARP_RATE,3},{P_ARP_OCT,3},{P_ARP_GATE,0.8f},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},
        {P_OSC2_COARSE,12},{P_XMOD,0.3f},{P_CUTOFF,0.75f},{P_RESONANCE,0.2f},{P_ENV2_D,0.6f},{P_ENV2_S,0.2f},{P_ENV2_R,0.5f}),
    PRE("Arp Brass Pulse",{P_ARP_MODE,1},{P_ARP_RATE,1},{P_ARP_OCT,1},{P_ARP_GATE,0.6f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},
        {P_CUTOFF,0.42f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.7f},{P_ENV1_A,0.35f},
        {P_ENV1_D,0.55f},{P_ENV1_S,0.4f},{P_ENV2_A,0.25f},{P_ENV2_S,0.8f},{P_ENV2_R,0.25f}),
    PRE("Arp Dark Climb",{P_ARP_MODE,1},{P_ARP_RATE,4},{P_ARP_OCT,3},{P_ARP_GATE,0.6f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},
        {P_OSC2_SHAPE,0.4f},{P_OSC2_FINE,6},{P_CUTOFF,0.36f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0.7f},{P_ENV1_D,0.55f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f}),
    PRE("Arp Gritty Pulse",{P_ARP_MODE,3},{P_ARP_RATE,1},{P_ARP_OCT,2},{P_ARP_GATE,0.5f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},
        {P_OSC2_FINE,9},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.7f},{P_FLT_FM1,0.6f},
        {P_ENV1_D,0.5f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f}),
    PRE("Arp Random Grit",{P_ARP_MODE,4},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,0.45f},{P_OSC1_WAVE,7},{P_OSC2_LEVEL,0},
        {P_CUTOFF,0.5f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,1},{P_DRIVE,0.8f},{P_FLT_FM1,0.5f},{P_ENV1_D,0.45f},{P_ENV2_S,0.7f},
        {P_ENV2_R,0.2f},{P_VOLUME,0.6f}),
    PRE("Pad Breath Noise",{P_OSC1_LEVEL,.2f},{P_OSC2_LEVEL,0},{P_NOISE_LEVEL,.7f},{P_FILTER_MODE,1},{P_CUTOFF,.6f},
        {P_RESONANCE,.65f},{P_KEY_TRACK,.9f},{P_FLT_FM1,0}),
    PRE("Pad Burnt Strings",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.4f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.95f},{P_DRIVE,0.7f},{P_SHAPE_MOD,0.6f},{P_LFO_RATE,0.3f},{P_ENV2_A,0.6f},
        {P_ENV2_R,0.7f}),
    PRE("Pad Choir Reso",{P_OSC2_WAVE,1},{P_OSC2_FINE,-10},{P_FILTER_MODE,4},{P_RESONANCE,.7f},{P_CUTOFF,.5f},{P_ENV2_A,.5f},
        {P_ENV2_R,.7f},{P_VIB_DEPTH,.2f}),
    PRE("Pad Cinder",{P_OSC1_WAVE,7},{P_OSC1_LEVEL,0.5f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,8},{P_OSC2_LEVEL,0.7f},
        {P_CUTOFF,0.4f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.65f},{P_FLT_FM2,0.3f},{P_LFO_RATE,0.18f},
        {P_ENV2_A,0.75f}),
    PRE("Pad Dark",{P_OSC2_WAVE,1},{P_OSC2_FINE,-13},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.38f},{P_RESONANCE,.35f},{P_FLT_FM1,.2f},
        {P_FLT_FM2,.25f},{P_LFO_RATE,.2f},{P_ENV2_A,.7f},{P_ENV2_R,.8f},{P_ENV1_A,.6f}),
    PRE("Pad Detuned Saw",{P_OSC2_WAVE,1},{P_OSC2_FINE,14},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.55f},{P_RESONANCE,.2f},{P_ENV2_A,.7f},
        {P_ENV2_D,.5f},{P_ENV2_S,.9f},{P_ENV2_R,.8f},{P_FLT_FM1,.25f},{P_ENV1_A,.6f}),
    PRE("Pad Dusty Saw",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,17},{P_OSC2_LEVEL,0.75f},{P_NOISE_LEVEL,0.1f},
        {P_CUTOFF,0.52f},{P_RESONANCE,0.2f},{P_FILTER_DRIVE,0.6f},{P_DRIVE,0.4f},{P_ENV2_A,0.7f},{P_ENV2_R,0.85f},
        {P_FLT_FM1,0.2f},{P_ENV1_A,0.65f}),
    PRE("Pad Glass",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_FINE,6},{P_OSC2_LEVEL,.5f},{P_XMOD,.15f},
        {P_CUTOFF,.78f},{P_RESONANCE,.2f},{P_FLT_FM1,.1f},{P_ENV2_A,.65f},{P_ENV2_R,.8f}),
    PRE("Pad Gravel",{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,11},{P_NOISE_LEVEL,0.15f},{P_FILTER_MODE,4},{P_CUTOFF,0.5f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.55f},{P_FLT_FM2,0.3f},{P_LFO_RATE,0.25f}),
    PRE("Pad Hollow Grit",{P_FILTER_MODE,3},{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.5f},{P_OSC2_FINE,7},{P_CUTOFF,0.55f},
        {P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.55f},{P_FLT_FM2,0.4f},{P_LFO_RATE,0.3f}),
    PRE("Pad Phasing",{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,.7f},{P_FILTER_MODE,3},{P_CUTOFF,.6f},{P_RESONANCE,.45f}),
    PRE("Pad Pulse",{P_OSC1_WAVE,4},{P_OSC2_WAVE,4},{P_OSC2_FINE,10},{P_SHAPE_MOD,.9f},{P_LFO_RATE,.3f},{P_CUTOFF,.52f},
        {P_RESONANCE,.2f},{P_ENV2_A,.7f},{P_ENV2_R,.75f},{P_FLT_FM1,.2f}),
    PRE("Pad Pulse Strings",{P_OSC1_WAVE,4},{P_OSC2_WAVE,4},{P_OSC2_FINE,-9},{P_SHAPE_MOD,.8f},{P_LFO_RATE,.35f},{P_CUTOFF,.6f},
        {P_RESONANCE,.15f},{P_ENV2_A,.6f}),
    PRE("Pad Reso Vox",{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_FILTER_MODE,4},{P_CUTOFF,.55f},{P_RESONANCE,.6f},{P_FLT_FM2,.3f},
        {P_LFO_RATE,.28f},{P_ENV2_A,.65f}),
    PRE("Pad Rust",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,-14},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.44f},{P_RESONANCE,0.3f},
        {P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM1,0.3f},{P_ENV1_A,0.7f},{P_ENV1_S,0.5f},{P_ENV2_A,0.7f},{P_ENV2_R,0.8f}),
    PRE("Pad Slow Swell",{P_OSC2_WAVE,1},{P_OSC2_FINE,12},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.48f},{P_RESONANCE,.25f},{P_FLT_FM1,.5f},
        {P_ENV1_A,.85f},{P_ENV1_D,.8f},{P_ENV1_S,.6f},{P_ENV1_R,.8f},{P_ENV2_A,.8f}),
    PRE("Drone Chaos",{P_OSC1_WAVE,5},{P_OSC2_WAVE,5},{P_OSC2_COARSE,7},{P_OSC2_FINE,23},{P_XMOD,.8f},{P_OSC1_FM2,.4f},
        {P_OSC2_FM2,.3f},{P_LFO_RATE,.7f},{P_SHAPE_MOD,.8f},{P_CUTOFF,.6f},{P_RESONANCE,.55f},{P_FLT_FM2,.5f},{P_FLT_FM1,0},
        {P_DRONE,1}),
    PRE("Drone Growl",{P_DRONE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,25},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.45f},{P_RESONANCE,.55f},
        {P_FLT_FM2,.5f},{P_LFO_RATE,.2f},{P_DRIVE,.5f}),
    PRE("Drone Metal",{P_DRONE,1},{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,5},{P_OSC2_FINE,18},{P_XMOD,.6f},
        {P_OSC1_FM2,.2f},{P_LFO_RATE,.25f},{P_CUTOFF,.58f},{P_RESONANCE,.5f},{P_FLT_FM2,.4f},{P_FLT_FM1,0}),
    PRE("Drone Sub",{P_DRONE,1},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,4},{P_OSC2_LEVEL,.7f},
        {P_CUTOFF,.42f},{P_RESONANCE,.35f},{P_FLT_FM2,.2f},{P_LFO_RATE,.15f},{P_FLT_FM1,0}),
    PRE("Drone Wind",{P_OSC1_LEVEL,0},{P_NOISE_LEVEL,.9f},{P_FILTER_MODE,1},{P_RESONANCE,.8f},{P_CUTOFF,.55f},{P_FLT_FM2,.6f},
        {P_LFO_RATE,.25f},{P_DRONE,1}),
};
#define N_PRESETS ((int)(sizeof PRESETS / sizeof PRESETS[0]))

static void apply_preset(mm_t *m, int idx) {
    if (idx < 0 || idx >= N_PRESETS) return;
    for (int i = 0; i < P_COUNT; i++) m->p[i] = PD[i].def;
    for (int i = 0; i < PRESETS[idx].n; i++) {
        int id = PRESETS[idx].v[i].id;
        m->p[id] = clampf(PRESETS[idx].v[i].v, PD[id].min, PD[id].max);
    }
    m->preset = idx;
    unlatch(m);
    sync_modes(m);
}


/* ---------------------------------------------------------------- state (host autosave, My Presets, Module Presets)
 * Flat JSON of every parameter plus the current preset index: self-contained, no references into banks. */
static int state_write(const mm_t *m, char *buf, int n) {
    int w = snprintf(buf, (size_t)n, "{\"v\":1,\"preset\":%d", m->preset);
    for (int i = 0; i < P_COUNT && w < n; i++) w += snprintf(buf + w, (size_t)(n - w), ",\"%s\":%.5g", PD[i].key, (double)m->p[i]);
    if (w < n) w += snprintf(buf + w, (size_t)(n - w), "}");
    return w < n ? w : -1;
}
static void state_read(mm_t *m, const char *js) {
    char pat[48];
    for (int i = 0; i < P_COUNT; i++) {
        snprintf(pat, sizeof pat, "\"%s\":", PD[i].key);
        const char *q = strstr(js, pat);
        if (!q) continue;                         /* missing key (older state): keep current value */
        char *e; float v = strtof(q + strlen(pat), &e);
        if (e == q + strlen(pat) || !isfinite(v)) continue;
        v = clampf(v, PD[i].min, PD[i].max);
        m->p[i] = PD[i].kind == K_F ? v : floorf(v + 0.5f);
    }
    m->p[P_STICK_X] = 0.0f; m->p[P_STICK_Y] = 0.0f;   /* the stick always starts centred */
    if ((int)m->p[P_DRONE] != 2) unlatch(m);
    sync_modes(m);
    const char *q = strstr(js, "\"preset\":");
    if (q) { int pi = atoi(q + 9); if (pi >= 0 && pi < N_PRESETS) m->preset = pi; }
}

/* ---------------------------------------------------------------- JSON (built once in create_instance) */
/* Bounded append: never writes past n, returns the new length (clamped), so a too-big answer can never overflow. */
static int app(char *b, int n, int w, const char *fmt, ...) {
    if (w >= n - 1) return n - 1;
    va_list ap; va_start(ap, fmt);
    int r = vsnprintf(b + w, (size_t)(n - w), fmt, ap);
    va_end(ap);
    if (r < 0) return w;
    return (w + r >= n - 1) ? n - 1 : w + r;
}
static int app_list(char *b, int n, int w, const char *csv) {
    w = app(b, n, w, "[");
    const char *p = csv;
    while (*p) {
        const char *e = strchr(p, ','); int len = e ? (int)(e - p) : (int)strlen(p);
        w = app(b, n, w, "\"%.*s\"%s", len, p, e ? "," : "");
        p += len + (e ? 1 : 0);
    }
    return app(b, n, w, "]");
}
static int emit_param(char *b, int n, int w, int i) {
    const pdef_t *d = &PD[i];
    const char *type = d->kind == K_F ? "float" : (d->kind == K_I ? "int" : "enum");
    w = app(b, n, w, "{\"key\":\"%s\",\"name\":\"%s\",\"label\":\"%s\"", d->key, d->label, d->label);
    if (SHORT_NAME[i]) w = app(b, n, w, ",\"short_name\":\"%s\"", SHORT_NAME[i]);
    w = app(b, n, w, ",\"type\":\"%s\",\"min\":%g,\"max\":%g,\"default\":%g", type, (double)d->min, (double)d->max, (double)d->def);
    if (d->kind == K_F) w = app(b, n, w, ",\"step\":%g", (double)d->step);
    if (d->opts) { w = app(b, n, w, ",\"options\":"); w = app_list(b, n, w, d->opts); }
    if (SHORT_OPTS[i]) { w = app(b, n, w, ",\"short_options\":"); w = app_list(b, n, w, SHORT_OPTS[i]); }
    if (VIZ_GROUP[i]) w = app(b, n, w, ",\"viz\":{\"group\":\"%s\",\"role\":\"%s\"}", VIZ_GROUP[i], VIZ_ROLE[i]);
    else if (VIZ_KIND[i]) w = app(b, n, w, ",\"viz\":{\"kind\":\"%s\"}", VIZ_KIND[i]);
    return app(b, n, w, "}");
}

/* Pages (levels): each is a named grid page of up to 8 knobs. Root holds the performance macros and the preset browser.
 * Keys only here; metadata, full names and short cell names come from chain_params. Enforced by tests/host_test.c. */
#define K_ROOT  "\"cutoff\",\"resonance\",\"filter_drive\",\"grit\",\"env2_a\",\"env2_r\",\"drive\",\"volume\""
#define K_OSC   "\"osc1_wave\",\"osc1_shape\",\"osc2_wave\",\"osc2_shape\",\"osc2_coarse\",\"osc2_fine\",\"osc1_level\",\"osc2_level\""
#define K_FILT  "\"cutoff\",\"resonance\",\"filter_drive\",\"filter_mode\",\"flt_fm1\",\"flt_fm2\",\"key_track\",\"drive\""
#define K_ENV1  "\"env1_a\",\"env1_d\",\"env1_s\",\"env1_r\""
#define K_ENV2  "\"env2_a\",\"env2_d\",\"env2_s\",\"env2_r\""
#define K_MOD   "\"osc1_fm1\",\"osc2_fm1\",\"osc1_fm2\",\"osc2_fm2\",\"xmod_2to1\",\"shape_mod\",\"lfo_rate\",\"lfo_wave\""
#define K_PLAY  "\"noise_level\",\"glide\",\"vibrato_depth\",\"bend_range\",\"drift\",\"legato\",\"drone\",\"volume\""
#define K_PERF  "\"vel_cutoff\",\"vel_amp\",\"at_dest\",\"at_amount\",\"priority\",\"octave\",\"vib_rate\",\"tune\""
#define K_STICK "\"stick_x\",\"stick_y\",\"stick_x_to\",\"stick_y_to\",\"hold\",\"bend_range\",\"vibrato_depth\",\"glide\""
#define K_ARP   "\"arp_mode\",\"arp_rate\",\"arp_octaves\",\"arp_gate\",\"hold\""
#define LEVEL(id, title, keys) "\"" id "\":{\"name\":\"" title "\",\"children\":null,\"knobs\":[" keys "],\"params\":[" keys "]}"
static const char HIERARCHY[] =
    "{\"modes\":null,\"levels\":{"
    "\"root\":{\"name\":\"Mean Mod\",\"list_param\":\"preset\",\"count_param\":\"preset_count\",\"name_param\":\"preset_name\",\"children\":null,"
        "\"knobs\":[" K_ROOT "],"
        "\"params\":[{\"level\":\"osc\",\"label\":\"Oscillators\"},{\"level\":\"filter\",\"label\":\"Filter\"},{\"level\":\"env1\",\"label\":\"Env 1\"},"
        "{\"level\":\"env2\",\"label\":\"Env 2\"},{\"level\":\"mod\",\"label\":\"Modulation\"},{\"level\":\"play\",\"label\":\"Play\"},"
        "{\"level\":\"perform\",\"label\":\"Perform\"},{\"level\":\"stick\",\"label\":\"Stick\"},{\"level\":\"arp\",\"label\":\"Arp\"},"
        "{\"key\":\"env2_a\",\"short_name\":\"AmpA\"},{\"key\":\"env2_r\",\"short_name\":\"AmpR\"}]},"
    LEVEL("osc", "Oscillators", K_OSC) ","
    LEVEL("filter", "Filter", K_FILT) ","
    LEVEL("env1", "Env 1", K_ENV1) ","
    LEVEL("env2", "Env 2", K_ENV2) ","
    LEVEL("mod", "Modulation", K_MOD) ","
    LEVEL("play", "Play", K_PLAY) ","
    LEVEL("perform", "Perform", K_PERF) ","
    LEVEL("stick", "Stick", K_STICK) ","
    LEVEL("arp", "Arp", K_ARP)
    "}}";

static void build_json(mm_t *m) {
    int n = JSON_MAX, w = 0;
    char *b = m->chain_json;
    w = app(b, n, w, "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":%d}", N_PRESETS - 1);
    for (int i = 0; i < P_COUNT; i++) { w = app(b, n, w, ","); w = emit_param(b, n, w, i); }
    w = app(b, n, w, "]");
    if (w >= n - 1) { snprintf(b, (size_t)n, "[]"); w = 2; }   /* too big for the host: refuse cleanly rather than send a cut-off document */
    m->chain_len = w;
    m->ui_len = (int)strlen(HIERARCHY);
    memcpy(m->ui_json, HIERARCHY, (size_t)m->ui_len + 1);
}

/* ---------------------------------------------------------------- plugin API */
static const host_api_v1_t *g_host;

static void reset_state(mm_t *m) {
    m->n_notes = 0; m->gate = 0; m->cur_note = -1; m->arp_note = -1; m->cur_semi = m->tgt_semi = 48.0f; m->vel = 0.8f;
    m->bend = m->at = m->modwheel = 0.0f;
    memset(&m->e1, 0, sizeof m->e1); memset(&m->e2, 0, sizeof m->e2);
}

/*
 * Nonlinear zero-delay-feedback Sallen-Key (Korg35 LPF topology, Zavalishin/Pirkle TPT).
 * Two TPT one-pole lowpasses; feedback K * (1-G)*(y - s2) taken from the second stage (the highpass
 * sharing its state), which keeps unity DC gain at any K. The resonance path is saturated with Mystran's
 * cheap trick: tanh(c)/c evaluated from the K=0 estimate scales K, so a hotter signal squeezes the
 * resonance out (hot input squeezes the resonance out) with no iteration. A -80 dB noise floor kicks self-oscillation.
 * Mode taps: LP=y, BP=2(lp1-y), HP=u-2lp1+y (exact 2nd-order forms at K=0), combos are sums.
 */
static inline float flt_tick(mm_t *m, float x, float G, float K, float ig, float inv_sqrt_ig, int mode, float kick) {
    float u = tanhf(ig * x) * inv_sqrt_ig + kick;
    float v1 = (u - m->f_s1) * G, lp1 = v1 + m->f_s1; m->f_s1 = lp1 + v1;
    /* feedback = K * HP1(y), HP1 = y - LP(y) with its own state s3: hp = (1-G)(y - s3) */
    float omg = 1.0f - G;
    float y0 = G * (lp1 - m->f_s2) + m->f_s2;              /* K = 0 estimate of y */
    float c = 1.5f * omg * (y0 - m->f_s3);
    float t = fabsf(c) < 1e-3f ? 1.0f : tanhf(c) / c;      /* Mystran: hot signal -> less resonance */
    float a = G * K * t * omg;
    float y = (G * (lp1 - m->f_s2) + m->f_s2 - a * m->f_s3) / (1.0f - a);
    float hp = omg * (y - m->f_s3);
    float v2 = G * (lp1 + K * t * hp - m->f_s2); m->f_s2 = y + v2;
    float v3 = G * (y - m->f_s3); m->f_s3 = (v3 + m->f_s3) + v3;
    if (fabsf(m->f_s1) < 1e-18f) m->f_s1 = 0.0f;           /* flush denormals: they are very slow on ARM */
    if (fabsf(m->f_s2) < 1e-18f) m->f_s2 = 0.0f;
    if (fabsf(m->f_s3) < 1e-18f) m->f_s3 = 0.0f;
    if (!isfinite(y) || fabsf(y) > 1e4f) { m->f_s1 = m->f_s2 = m->f_s3 = 0.0f; return 0.0f; }
    float bp = 2.0f * (lp1 - y), hpo = u - 2.0f * lp1 + y;
    switch (mode) {
    case 1: return bp;
    case 2: return hpo;
    case 3: return y + hpo;
    case 4: return y + bp;
    default: return y;
    }
}

/* Blackman-windowed halfband lowpass (fc = fs_hi/4), normalised to unity DC gain. Built once at load. */
static void build_halfband(mm_t *m) {
    const int c = HB_N / 2;
    double h[HB_N], sum = 0.0;
    for (int k = 0; k < HB_N; k++) {
        double t = k - c, x = 0.5 * t;
        double sinc = (t == 0.0) ? 1.0 : sin(3.14159265358979 * x) / (3.14159265358979 * x);
        double w = 0.42 - 0.5 * cos(2.0 * 3.14159265358979 * k / (HB_N - 1)) + 0.08 * cos(4.0 * 3.14159265358979 * k / (HB_N - 1));
        h[k] = 0.5 * sinc * w;
        sum += h[k];
    }
    int n = 0;
    for (int k = 0; k < HB_N; k++) {
        int off = k - c;
        if (off == 0 || (off & 1)) { m->tap_val[n] = (float)(h[k] / sum); m->tap_idx[n] = k; n++; }  /* even offsets are ~0 */
    }
}

static mm_t g_pool[MAX_INSTANCES];
static int g_used[MAX_INSTANCES];   /* claimed with an atomic exchange: safe if two loader threads race */

static void *create_instance(const char *dir, const char *defaults) {
    (void)dir; (void)defaults;
    mm_t *m = NULL;
    for (int i = 0; i < MAX_INSTANCES; i++)
        if (!__atomic_exchange_n(&g_used[i], 1, __ATOMIC_ACQUIRE)) { m = &g_pool[i]; break; }
    if (!m) return NULL;
    memset(m, 0, sizeof *m);
    for (int i = 0; i < P_COUNT; i++) m->p[i] = PD[i].def;
    m->rng = 0x1234567u;
    reset_state(m);
    build_json(m);
    build_halfband(m);
    return m;
}
static void destroy_instance(void *inst) {
    mm_t *m = inst;
    if (m < g_pool || m >= g_pool + MAX_INSTANCES) return;   /* not ours */
    __atomic_store_n(&g_used[m - g_pool], 0, __ATOMIC_RELEASE);
}

static void on_midi(void *inst, const uint8_t *msg, int len, int src) {
    mm_t *m = inst;
    if (!m || !msg || len < 1) return;
    int st = msg[0] & 0xF0, d1 = len > 1 ? msg[1] & 0x7F : 0, d2 = len > 2 ? msg[2] & 0x7F : 0;
    if (src == MOVE_MIDI_SOURCE_TOUCH) {   /* capacitive touch of Knobs 1-8 (notes 0-7): never a played note */
        if ((st == 0x90 || st == 0x80) && d1 < 2) {
            int down = (st == 0x90 && d2 > 0);
            m->touched[d1] = (uint8_t)down;
            if (down) m->touch_seen = 1;
            else { m->p[d1 ? P_STICK_Y : P_STICK_X] = 0.0f; m->stick_age[d1] = 0; if (d1) m->y_down = 0; }   /* the stick springs back */
        }
        return;
    }
    switch (st) {
    case 0x90: if (d2 > 0) in_on(m, d1, d2); else in_off(m, d1); break;
    case 0x80: in_off(m, d1); break;
    case 0xE0: m->bend = ((((d2 << 7) | d1) - 8192) / 8192.0f) * m->p[P_BEND_RANGE]; break;
    case 0xA0: m->at = d2 / 127.0f; break;                 /* poly AT (mono voice: any key) */
    case 0xD0: m->at = d1 / 127.0f; break;                 /* channel pressure */
    case 0xB0:
        if (d1 == 1) m->modwheel = d2 / 127.0f;
        else if (d1 == 64) pedal(m, d2 >= 64);
        else if (d1 == 121) { m->modwheel = 0.0f; m->at = 0.0f; m->bend = 0.0f; pedal(m, 0); }   /* reset all controllers */
        else if (d1 == 123 || d1 == 120) panic(m);
        break;
    default: break;
    }
}

static void set_param(void *inst, const char *key, const char *val) {
    mm_t *m = inst;
    if (!m || !key || !val) return;
    if (!strcmp(key, "state")) { state_read(m, val); return; }
    if (!strcmp(key, "preset")) { char *e; float v = strtof(val, &e); if (e != val && isfinite(v)) apply_preset(m, (int)v); return; }
    for (int k = 0; k < P_COUNT; k++)
        if (!strcmp(key, PD[k].key)) {
            char *end; float v = strtof(val, &end);
            if (end == val || !isfinite(v)) return;  /* reject "", "nan", "inf" */
            v = clampf(v, PD[k].min, PD[k].max);
            if (PD[k].kind != K_F) v = floorf(v + 0.5f);
            m->p[k] = v;
            if (k == P_DRONE && (int)v != 2) unlatch(m);
            if (k == P_STICK_X) m->stick_age[0] = 0;
            if (k == P_STICK_Y) {
                m->stick_age[1] = 0;
                if (v <= -0.6f && !m->y_down) { m->y_down = 1; m->p[P_HOLD] = ((int)m->p[P_HOLD] == 1) ? 0.0f : 1.0f; }   /* stick down toggles Hold */
                else if (v > -0.3f) m->y_down = 0;
            }
            if (k == P_ARP_MODE || k == P_HOLD || k == P_STICK_Y) sync_modes(m);
            return;
        }
}
static int get_param(void *inst, const char *key, char *buf, int n) {
    mm_t *m = inst;
    if (!m || !key || !buf || n <= 0) return -1;
    for (int k = 0; k < P_COUNT; k++)
        if (!strcmp(key, PD[k].key))
            return PD[k].kind == K_F ? snprintf(buf, n, "%.4f", m->p[k]) : snprintf(buf, n, "%d", (int)m->p[k]);
    if (!strcmp(key, "name")) return snprintf(buf, (size_t)n, "Mean Mod");
    if (!strcmp(key, "state")) return state_write(m, buf, n);
    if (!strcmp(key, "preset")) return snprintf(buf, n, "%d", m->preset);
    if (!strcmp(key, "preset_count")) return snprintf(buf, n, "%d", N_PRESETS);
    if (!strcmp(key, "preset_name")) return snprintf(buf, n, "%s", PRESETS[m->preset].name);
    const char *src = NULL; int len = 0;
    if (!strcmp(key, "ui_hierarchy")) { src = m->ui_json; len = m->ui_len; }
    else if (!strcmp(key, "chain_params")) { src = m->chain_json; len = m->chain_len; }
    if (!src) return -1;
    if (len >= n) return -1;  /* over-size answers are rejected by the host, never truncate */
    memcpy(buf, src, (size_t)len + 1);
    return len;
}
static int get_error(void *i, char *buf, int n) { (void)i; (void)buf; (void)n; return 0; }

static void render_active(mm_t *m, int16_t *out, int frames) {
    const float *p = m->p;

    /* Joystick emulation: X and Y each drive one chosen target; both spring back to centre when the knob is released. */
    const int xt = (int)p[P_STICK_X_TO], yt = (int)p[P_STICK_Y_TO];
    const float xv = p[P_STICK_X], yv = p[P_STICK_Y], yu = yv > 0.0f ? yv : 0.0f;
    const float stick_bend = xt == 0 ? xv * p[P_BEND_RANGE] : 0.0f;
    const float stick_cut_oct = (xt == 2 ? xv * 2.0f : 0.0f) + (yt == 2 ? yu * 2.0f : 0.0f);
    const float grit_in = clampf(p[P_GRIT] + (xt == 3 ? xv : 0.0f) + (yt == 3 ? yu : 0.0f), -1.0f, 1.0f);
    const float g_glide = clampf(p[P_GLIDE] + (yt == 0 ? yu : 0.0f), 0.0f, 1.0f);

    /* block-rate constants */
    const int w1 = (int)p[P_OSC1_WAVE], w2 = (int)p[P_OSC2_WAVE];
    const float a1 = coef(env_time(p[P_ENV1_A]), 1.466f), d1 = coef(env_time(p[P_ENV1_D]), 4.0f), r1 = coef(env_time(p[P_ENV1_R]), 4.0f);
    const float a2 = coef(env_time(p[P_ENV2_A]), 1.466f), d2 = coef(env_time(p[P_ENV2_D]), 4.0f), r2 = coef(env_time(p[P_ENV2_R]), 4.0f);
    const float lfo_hz = 0.05f * powf(800.0f, p[P_LFO_RATE]);
    const float lfo_inc = lfo_hz / SR, vib_inc = clampf(3.0f + 5.0f * p[P_VIB_RATE] + (xt == 1 ? xv * 4.0f : 0.0f), 0.5f, 14.0f) / SR;
    const float glide_s = 2.0f * g_glide * g_glide;
    const float glide_c = glide_s > 0.0005f ? 1.0f - expf(-1.0f / (glide_s * SR * 0.25f)) : 1.0f;
    const int at_dest = (int)p[P_AT_DEST];
    const float at = p[P_AT_AMOUNT] * m->at;                    /* aftertouch, scaled */
    const float vib_depth = clampf(p[P_VIB_DEPTH] + 0.5f * m->modwheel + (at_dest == 2 ? at : 0.0f) + (yt == 1 ? yu : 0.0f), 0.0f, 1.0f) * 0.6f;  /* semitones */
    const float at_cut_oct = at_dest == 1 ? at * 3.0f : 0.0f;
    const float xmod_eff = clampf(p[P_XMOD] + (at_dest == 4 ? at * 0.5f : 0.0f), 0.0f, 1.0f);
    const float vel_cut_oct = p[P_VEL_CUTOFF] * (m->vel - 0.5f) * 6.0f;
    const float vel_scale = 1.0f - p[P_VEL_AMP] * (1.0f - m->vel);
    const float pitch_off = 12.0f * p[P_OCTAVE] + p[P_TUNE] / 100.0f;
    /* Grit / Dark macro, applied on top of the patch (the stored params are untouched). + = grittier and darker, - = cleaner and brighter. */
    const float gp = grit_in > 0.0f ? grit_in : 0.0f, gn = grit_in < 0.0f ? -grit_in : 0.0f;
    const float g_fdrive = clampf(p[P_FILTER_DRIVE] + 0.9f * gp - 0.4f * gn, 0.0f, 2.0f);   /* beyond 1: extra saturation only Grit can reach */
    const float g_drive  = clampf(p[P_DRIVE] + 0.9f * gp - 0.3f * gn, 0.0f, 1.6f);
    const float g_res    = clampf(p[P_RESONANCE] + 0.2f * gp - 0.1f * gn, 0.0f, 1.0f);
    const float g_noise  = clampf(p[P_NOISE_LEVEL] + 0.12f * gp, 0.0f, 1.0f);
    const float g_drift  = clampf(p[P_DRIFT] + 0.5f * gp - 0.2f * gn, 0.0f, 1.0f);
    const float g_cut_oct = -2.0f * gp + 1.0f * gn;
    const float g_makeup = 1.0f + 0.8f * gn + 0.5f * gp;     /* clean end loses filter-input gain: keep the level */
    const float drift_amt = g_drift * 0.03f;  /* semitones (3 cents) */
    const float drift_c = 1.0f - expf(-6.2831853f * 0.4f / SR);
    const float k1 = exp2f((0.5f - p[P_OSC1_SHAPE]) * 2.0f), k2 = exp2f((0.5f - p[P_OSC2_SHAPE]) * 2.0f);
    const float o2_off = (p[P_OSC2_COARSE] + p[P_OSC2_FINE] / 100.0f);
    const float noise_lvl = g_noise * 0.5f * 1.4142f /* keep in-band level after OS */, lv1 = p[P_OSC1_LEVEL], lv2 = p[P_OSC2_LEVEL];
    const float vol = p[P_VOLUME] * 0.8f;
    const int drone_mode = (int)p[P_DRONE], square = p[P_LFO_WAVE] >= 0.5f;
    const float s1 = p[P_ENV1_S], s2 = drone_mode ? 1.0f : p[P_ENV2_S];  /* drone: amp holds at full while the note is down */
    const float res_K = g_res * 3.6f;                 /* ~2 is linear self-oscillation */
    const float ig = 0.25f * exp2f(4.0f * g_fdrive), inv_sqrt_ig = 1.0f / sqrtf(ig);
    const int fmode = (int)p[P_FILTER_MODE];
    const float drv = clampf(g_drive + (at_dest == 3 ? at * 0.6f : 0.0f), 0.0f, 1.6f), drv_g = 1.0f + 5.0f * drv + 6.0f * drv * drv, drv_b = 0.15f * drv, drv_bias_out = tanhf(drv_b);
    const float drv_norm = 1.0f / sqrtf(drv_g);

    for (int i = 0; i < frames; i++) {
        /* control signals */
        float lfo = m->lfo_ph < 0.5f ? 4.0f * m->lfo_ph - 1.0f : 3.0f - 4.0f * m->lfo_ph;  /* triangle */
        if (square) lfo = m->lfo_ph < 0.5f ? 1.0f : -1.0f;
        m->lfo_ph += lfo_inc; if (m->lfo_ph >= 1.0f) m->lfo_ph -= 1.0f;
        m->vib_ph += vib_inc; if (m->vib_ph >= 1.0f) m->vib_ph -= 1.0f;
        float vib = (m->vib_ph < 0.5f ? 4.0f * m->vib_ph - 1.0f : 3.0f - 4.0f * m->vib_ph) * vib_depth;
        float e1 = env_step(&m->e1, a1, d1, s1, r1);
        float e2 = env_step(&m->e2, a2, d2, s2, r2);

        m->cur_semi += (m->tgt_semi - m->cur_semi) * glide_c;
        m->o1.drift += (frand(&m->rng) - m->o1.drift) * drift_c * 0.02f;  /* slow wander */
        m->o2.drift += (frand(&m->rng) - m->o2.drift) * drift_c * 0.02f;
        float semi = m->cur_semi + pitch_off + m->bend + stick_bend + vib;

        float cut_oct = p[P_CUTOFF] * 9.8138f + g_cut_oct + p[P_FLT_FM1] * e1 * 5.0f + p[P_FLT_FM2] * lfo * 3.0f
                      + p[P_KEY_TRACK] * (m->cur_semi + pitch_off + m->bend - 60.0f) / 12.0f + vel_cut_oct + at_cut_oct + stick_cut_oct;
        float fc = clampf(20.0f * exp2f(cut_oct), 15.0f, 19000.0f);
        float g = tanf(3.14159265f * fc / (SR * OS)), G = g / (1.0f + g);

        /* oscillator core at OS x the sample rate; halfband-decimated afterwards */
        float amp = e2 * vel_scale;
        for (int sub = 0; sub < OS; sub++) {
            float fm1_oct = p[P_OSC1_FM1] * e1 * 3.0f + p[P_OSC1_FM2] * lfo * 1.0f + xmod_eff * m->o2_prev * 2.0f;
            float f1 = 440.0f * exp2f((semi + m->o1.drift * drift_amt * 8.0f - 69.0f) / 12.0f + fm1_oct);
            float dt1 = clampf(f1 / (SR * OS), 0.0f, 0.45f);
            float sh1 = clampf(p[P_OSC1_SHAPE] + p[P_SHAPE_MOD] * lfo * 0.5f, 0.0f, 1.0f);
            float y1 = osc_wave(m->o1.phase, dt1, w1, sh1, k1);
            m->o1.phase += dt1; if (m->o1.phase >= 1.0f) m->o1.phase -= 1.0f;

            float fm2_oct = p[P_OSC2_FM1] * e1 * 3.0f + p[P_OSC2_FM2] * lfo * 1.0f;
            float f2 = 440.0f * exp2f((semi + o2_off + m->o2.drift * drift_amt * 8.0f - 69.0f) / 12.0f + fm2_oct);
            float dt2 = clampf(f2 / (SR * OS), 0.0f, 0.45f);
            float sh2 = clampf(p[P_OSC2_SHAPE] + p[P_SHAPE_MOD] * lfo * 0.5f, 0.0f, 1.0f);
            float y2 = osc_wave(m->o2.phase, dt2, w2, sh2, k2);
            m->o2.phase += dt2; if (m->o2.phase >= 1.0f) m->o2.phase -= 1.0f;
            m->o2_prev = y2;

            float noise = frand(&m->rng);
            float mix = y1 * lv1 + y2 * lv2 + noise * noise_lvl;
            float fl = flt_tick(m, mix, G, res_K, ig, inv_sqrt_ig, fmode, noise * 1e-4f);
            float va = fl * amp;                                       /* VCA */
            va = (tanhf(drv_g * va + drv_b) - drv_bias_out) * drv_norm; /* transistor-ish asymmetric drive */
            m->hb[m->hb_pos] = va * vol * g_makeup;
            m->hb_pos = (m->hb_pos + 1) % HB_N;
        }
        float y = 0.0f;  /* decimate: newest sample is at hb_pos-1 */
        for (int j = 0; j < HB_NZ; j++) {
            int idx = m->hb_pos - 1 - (HB_N - 1 - m->tap_idx[j]);
            idx %= HB_N; if (idx < 0) idx += HB_N;
            y += m->tap_val[j] * m->hb[idx];
        }

        /* DC blocker (~10 Hz) then soft limiter so output can never exceed full scale */
        float hp = y - m->dc_x + 0.9986f * m->dc_y; m->dc_x = y; m->dc_y = hp;
        if (fabsf(m->dc_y) < 1e-18f) m->dc_y = 0.0f;
        hp = tanhf(hp);
        int16_t s = (int16_t)(clampf(hp, -1.0f, 1.0f) * 32767.0f);
        out[2 * i] = s; out[2 * i + 1] = s;
    }
}


#if defined(__aarch64__)
/* Flush-to-zero while rendering: denormal arithmetic is very slow on Cortex-A72 and would steal the shared audio thread. */
static inline unsigned long fpcr_read(void) { unsigned long v; __asm__ volatile("mrs %0, fpcr" : "=r"(v)); return v; }
static inline void fpcr_write(unsigned long v) { __asm__ volatile("msr fpcr, %0" : : "r"(v)); }
#endif

static void render_seg(mm_t *m, int16_t *out, int frames) {
    if (m->e2.stage == ENV_IDLE) {      /* silent voice: output exact zeros and spend no CPU */
        if (!m->was_idle) {
            m->f_s1 = m->f_s2 = m->f_s3 = 0.0f; m->dc_x = m->dc_y = 0.0f;
            memset(m->hb, 0, sizeof m->hb);
            m->e1.stage = ENV_IDLE; m->e1.level = 0.0f;
            m->was_idle = 1;
        }
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }
    m->was_idle = 0;
    render_active(m, out, frames);
}

static void render_block(void *inst, int16_t *out, int frames) {
    mm_t *m = inst;
    if (!m || !out || frames <= 0) return;

    /* Stick safety net: a displaced axis that is not being touched recentres (covers a missed touch-release, or no touch events at all). */
    for (int k = 0; k < 2; k++) {
        int pi = k ? P_STICK_Y : P_STICK_X;
        if (m->p[pi] != 0.0f && !m->touched[k]) {
            if (++m->stick_age[k] > (m->touch_seen ? 150 : 120)) { m->p[pi] = 0.0f; m->stick_age[k] = 0; if (k) m->y_down = 0; }
        } else m->stick_age[k] = 0;
    }

#if defined(__aarch64__)
    unsigned long fpcr_old = fpcr_read();
    fpcr_write(fpcr_old | (1UL << 24));   /* FPCR.FZ */
#endif
    static const float STEPS_PER_BEAT[6] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
    float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    if (!(bpm >= 30.0f && bpm <= 300.0f)) bpm = 120.0f;
    const float step_len = SR * 60.0f / bpm / STEPS_PER_BEAT[(int)m->p[P_ARP_RATE] % 6];
    const float gate_len = step_len * m->p[P_ARP_GATE];
    const int tie = m->p[P_ARP_GATE] >= 0.999f;

    int remaining = frames;
    int16_t *ptr = out;
    while (remaining > 0) {
        int n = remaining;
        if ((int)m->p[P_ARP_MODE] == 0) {
            if (m->arp_running) arp_stop(m);
        } else {
            if (m->held_n > 0 && !m->arp_running) { m->arp_running = 1; m->arp_pos = step_len; m->arp_idx = 0; }
            else if (m->held_n == 0 && m->arp_running) arp_stop(m);
            while (m->arp_running && m->arp_pos >= step_len) { m->arp_pos -= step_len; arp_step(m); }
            if (m->arp_running) {
                float nx = step_len - m->arp_pos;
                if (m->arp_note >= 0 && !tie && m->arp_pos < gate_len && gate_len - m->arp_pos < nx) nx = gate_len - m->arp_pos;
                n = (int)ceilf(nx);
                if (n < 1) n = 1;
                if (n > remaining) n = remaining;
            }
        }
        render_seg(m, ptr, n);
        if (m->arp_running) {
            m->arp_pos += (float)n;
            if (m->arp_note >= 0 && !tie && m->arp_pos >= gate_len) arp_gate_off(m);
        }
        ptr += (size_t)n * 2;
        remaining -= n;
    }
#if defined(__aarch64__)
    fpcr_write(fpcr_old);
#endif
}

static plugin_api_v2_t g_api;
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host) {
    g_host = host; (void)g_host;
    memset(&g_api, 0, sizeof g_api);
    g_api.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_api.create_instance = create_instance; g_api.destroy_instance = destroy_instance;
    g_api.on_midi = on_midi; g_api.set_param = set_param; g_api.get_param = get_param;
    g_api.get_error = get_error; g_api.render_block = render_block;
    return &g_api;
}
