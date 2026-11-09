/*
 * Mean Module: Schwung sound generator (plugin_api_v2).
 * Stage 1: two PolyBLEP oscillators + noise, FM/x-mod, two RC ADSRs, LFO, vibrato, glide,
 * drift, legato last-note-priority mono voice. Filter/drive stages are NOT implemented yet.
 *
 * Realtime rules: render/on_midi/set_param/get_param never allocate, lock, log or do I/O.
 * Instances come from a static pool (MAX_INSTANCES), so no entry point allocates, including create/destroy.
 * Per-instance JSON is formatted once in create_instance with snprintf (no heap use).
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dirent.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "plugin_api_v1.h"

#if defined(__aarch64__) && defined(__GLIBC__)
/* The bank-file worker thread uses pthread_create, dlopen and dladdr. A build against glibc 2.34+ would otherwise bind them to GLIBC_2.34 and refuse to
 * load on an older device; the aarch64 baseline versions are always available, so bind to those and keep the module's requirement at GLIBC_2.27. */
__asm__(".symver pthread_create,pthread_create@GLIBC_2.17");
__asm__(".symver dlopen,dlopen@GLIBC_2.17");
__asm__(".symver dladdr,dladdr@GLIBC_2.17");
#endif

#define MM_VERSION "0.10.2"   /* keep in step with src/module.json and release.json (the host test checks) */
#define MM_VERSION_S "0.10"
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
#define FXSC "FX Page Only,Any Page"
#define LFOW "Triangle,Square,Saw Down,Saw Up,Random"
#define LSYNC "Free,1/2,1/4,1/8,1/8 Triplet,1/16,1/16 Triplet,1/32"
#define DRONEOPT "Off,Hold,Latch"
#define FMODES "Lowpass,Bandpass,Highpass,LP+HP,BP+LP"
#define ATDEST "Off,Cutoff,Vibrato,Drive,FM"
#define PRIO "Last,Low,High"
#define XTO "Bend,Vibrato Speed,Cutoff,Grit,Arp Rate,Arp Mode"
#define YTO "Glide,Vibrato Depth,Cutoff,Grit,Hold Toggle,Arp Rate,Arp Mode"
#define FXT "Off,Grit,Filter Open,Filter Close,Crunch,Resonance,Noise,Pitch Rise,Pitch Dive,Vibrato,Wobble,Gate Chop,Detune,Arp Faster,Hold"
#define PCATS "Init,Bass,Sub,Lead,Pluck,Keys,Stab,Brass,Arp,Seq,Pad,Texture,Vox,Drone,FX,Horror,Sets,User 1,User 2,User 3,User 4,User 5,User 6,User 7,User 8"
#define N_BANKS 17           /* factory banks */
#define MAX_UBANKS 8         /* user banks (files in the bank folder) */
#define UB_PRE 48            /* presets per user bank */
#define UB_PV 96             /* settings per user preset */
#define USER_BASE 1000       /* preset codes from here are user presets: USER_BASE + bank * USER_STRIDE + position */
#define USER_STRIDE 64
#define BANK_MAX 64          /* most presets one factory bank may hold */
#define MAX_PRESETS 512
#define FXT_S "OFF,GRT,OPN,CLS,CRN,RES,NSE,RIS,DIV,VIB,WOB,CHP,DTN,ARP,HLD"
#define N_FXT 15             /* FX bank effect types (0 = Off) */
enum { FXT_OFF, FXT_GRIT, FXT_OPEN, FXT_CLOSE, FXT_CRUNCH, FXT_RES, FXT_NOISE, FXT_RISE, FXT_DIVE, FXT_VIB, FXT_WOB, FXT_CHOP, FXT_DETUNE, FXT_ARP, FXT_HOLD };
#define FX_DELAY_BLK 31      /* a touch must last ~90 ms before its effect starts, so a tap (half of a double tap) stays silent */
#define FX_PAGE_BLK 260      /* the FX Bank page counts as on screen if the host read one of its knobs within ~0.75 s (it re-reads the visible page ~5x/s) */
#define FX_DTAP_BLK 150     /* second tap must start within ~0.43 s of the first */
#define FX_SHORT_BLK 70      /* ... and the first tap must have been shorter than ~0.2 s */
#define MXSRC "Off,LFO,Filter Env,Amp Env,Velocity,Mod Wheel,Aftertouch,Stick X,Stick Y,Key,Random,LFO 2,Noise,Osc 2,Pitch Bend,Arp Step,Note Age,Smooth Random,Osc 1,Sustain Pedal"
#define MXDST "Off,Pitch,Osc 2 Pitch,Osc 1 Shape,Osc 2 Shape,Osc 1 Level,Osc 2 Level,Noise,Cutoff,Resonance,Filter Input,Drive,Amp,X-Mod,LFO Rate,LFO 2 Rate,Osc 1 FM,Osc 2 FM,Grit,Glide Time,Vibrato Depth,LFO Depth,LFO 2 Depth,Env Amount,Drift Amount"
#define MXSRC_S "OFF,LFO,FEN,AEN,VEL,MOD,AFT,STX,STY,KEY,RND,LF2,NSE,OS2,BND,ARP,AGE,SMR,OS1,PED"
#define MXDST_S "OFF,PCH,PC2,SH1,SH2,LV1,LV2,NSE,CUT,RES,INP,DRV,AMP,XMD,LFR,LR2,FM1,FM2,GRT,GLD,VIB,LD1,LD2,ENV,DFT"
#define N_SLOTS 6            /* mod matrix slots */
#define N_MXSRC 20
#define N_MXDST 25
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
 X(ENV1_A, "env1_a", "Filter Env Attack", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV1_D, "env1_d", "Filter Env Decay", K_F, 0, 1, 0.45, 0.02, NULL) \
 X(ENV1_S, "env1_s", "Filter Env Sustain", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV1_R, "env1_r", "Filter Env Release", K_F, 0, 1, 0.3, 0.02, NULL) \
 X(ENV2_A, "env2_a", "Amp Env Attack", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(ENV2_D, "env2_d", "Amp Env Decay", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(ENV2_S, "env2_s", "Amp Env Sustain", K_F, 0, 1, 1.0, 0.02, NULL) \
 X(ENV2_R, "env2_r", "Amp Env Release", K_F, 0, 1, 0.35, 0.02, NULL) \
 X(OSC1_FM1, "osc1_fm1", "Filter Env to Osc 1 FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC2_FM1, "osc2_fm1", "Filter Env to Osc 2 FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC1_FM2, "osc1_fm2", "LFO to Osc 1 FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(OSC2_FM2, "osc2_fm2", "LFO to Osc 2 FM", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(XMOD, "xmod_2to1", "X-Mod Osc 2 to Osc 1", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(SHAPE_MOD, "shape_mod", "LFO to Shape", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(LFO_RATE, "lfo_rate", "LFO Rate", K_F, 0, 1, 0.35, 0.02, NULL) \
 X(LFO_WAVE, "lfo_wave", "LFO Wave", K_E, 0, 4, 0, 1, LFOW) \
 X(CUTOFF, "cutoff", "Cutoff", K_F, 0, 1, 0.677, 0.01, NULL)  X(RESONANCE, "resonance", "Resonance", K_F, 0, 1, 0.3, 0.02, NULL)  X(FILTER_DRIVE, "filter_drive", "Filter Drive", K_F, 0, 1, 0.5, 0.02, NULL)  X(FILTER_MODE, "filter_mode", "Filter Mode", K_E, 0, 4, 0, 1, FMODES)  X(FLT_FM1, "flt_fm1", "Filter Env to Cutoff", K_F, -1, 1, 0.4, 0.02, NULL)  X(FLT_FM2, "flt_fm2", "LFO to Cutoff", K_F, 0, 1, 0.0, 0.02, NULL)  X(KEY_TRACK, "key_track", "Key Track", K_F, 0, 1, 0.5, 0.02, NULL)  X(DRIVE, "drive", "Drive", K_F, 0, 1, 0.2, 0.02, NULL)  X(NOISE_LEVEL, "noise_level", "Noise", K_F, 0, 1, 0.0, 0.02, NULL) \
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
 X(STICK_X_TO, "stick_x_to", "Stick X To", K_E, 0, 5, 0, 1, XTO) \
 X(STICK_Y_TO, "stick_y_to", "Stick Y To", K_E, 0, 6, 1, 1, YTO) \
 X(HOLD, "hold", "Hold", K_E, 0, 1, 0, 1, OFFON) \
 X(ARP_MODE, "arp_mode", "Arp Mode", K_E, 0, 5, 0, 1, ARPMODE) \
 X(ARP_RATE, "arp_rate", "Arp Rate", K_E, 0, 5, 3, 1, ARPRATE) \
 X(ARP_OCT, "arp_octaves", "Arp Octaves", K_I, 1, 3, 1, 1, NULL) \
 X(ARP_GATE, "arp_gate", "Arp Gate", K_F, 0.1, 1, 0.6, 0.05, NULL) \
 X(M1_SRC, "m1_src", "Matrix 1 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M1_DST, "m1_dst", "Matrix 1 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M1_AMT, "m1_amt", "Matrix 1 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(M2_SRC, "m2_src", "Matrix 2 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M2_DST, "m2_dst", "Matrix 2 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M2_AMT, "m2_amt", "Matrix 2 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(M3_SRC, "m3_src", "Matrix 3 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M3_DST, "m3_dst", "Matrix 3 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M3_AMT, "m3_amt", "Matrix 3 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(M4_SRC, "m4_src", "Matrix 4 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M4_DST, "m4_dst", "Matrix 4 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M4_AMT, "m4_amt", "Matrix 4 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(M5_SRC, "m5_src", "Matrix 5 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M5_DST, "m5_dst", "Matrix 5 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M5_AMT, "m5_amt", "Matrix 5 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(M6_SRC, "m6_src", "Matrix 6 Source", K_E, 0, 19, 0, 1, MXSRC) \
 X(M6_DST, "m6_dst", "Matrix 6 Target", K_E, 0, 24, 0, 1, MXDST) \
 X(M6_AMT, "m6_amt", "Matrix 6 Amount", K_F, -1, 1, 0.0, 0.02, NULL) \
 X(LFO2_RATE, "lfo2_rate", "LFO 2 Rate", K_F, 0, 1, 0.3, 0.02, NULL) \
 X(LFO2_WAVE, "lfo2_wave", "LFO 2 Wave", K_E, 0, 4, 0, 1, LFOW) \
 X(LFO_SYNC, "lfo_sync", "LFO Sync", K_E, 0, 7, 0, 1, LSYNC) \
 X(LFO2_SYNC, "lfo2_sync", "LFO 2 Sync", K_E, 0, 7, 0, 1, LSYNC) \
 X(OSC1_COARSE, "osc1_coarse", "Osc 1 Coarse", K_I, -24, 24, 0, 1, NULL) \
 X(OSC1_FINE, "osc1_fine", "Osc 1 Fine", K_F, -50, 50, 0, 1, NULL) \
 X(FX1_TYPE, "fx1_type", "FX 1 Effect", K_E, 0, 14, 0, 1, FXT) \
 X(FX2_TYPE, "fx2_type", "FX 2 Effect", K_E, 0, 14, 0, 1, FXT) \
 X(FX3_TYPE, "fx3_type", "FX 3 Effect", K_E, 0, 14, 0, 1, FXT) \
 X(FX4_TYPE, "fx4_type", "FX 4 Effect", K_E, 0, 14, 0, 1, FXT) \
 X(FX1_LVL, "fx1_lvl", "FX 1 Level", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(FX2_LVL, "fx2_lvl", "FX 2 Level", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(FX3_LVL, "fx3_lvl", "FX 3 Level", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(FX4_LVL, "fx4_lvl", "FX 4 Level", K_F, 0, 1, 0.5, 0.02, NULL) \
 X(NOISE_COLOR, "noise_color", "Noise Color", K_F, 0, 1, 0.0, 0.02, NULL) \
 X(FX_SCOPE, "fx_scope", "FX Touch", K_E, 0, 1, 0, 1, FXSC) \
 X(PRESET_CAT, "preset_cat", "Preset Bank", K_E, 0, 24, 0, 1, PCATS) \
 X(BANK_IO, "bank_io", "Bank Files", K_E, 0, 2, 0, 1, "Idle,Export Bank,Reload Folder") \
 X(BANK_STATUS, "bank_status", "Bank File Status", K_E, 0, 4, 0, 1, "Ready,Working,Exported,Loaded,Error") \
 X(USER_BANKS, "user_banks", "User Banks Loaded", K_I, 0, 8, 0, 1, NULL) \
 X(VERSION, "version", "Version", K_E, 0, 0, 0, 1, MM_VERSION) \
 X(PROG_CHG, "prog_chg", "Program Change", K_E, 0, 1, 0, 1, "Ignore,Accept") \
 X(LAST_CHANGE, "last_change", "Last Preset Change By", K_E, 0, 5, 0, 1, "None,Preset Set,Bank Pos,Bank,Prog Chg,State Load") \
 X(PC_SEEN, "pc_seen", "Program Changes Seen", K_I, 0, 127, 0, 1, NULL) \
 X(M1_VIA, "m1_via", "Matrix 1 Scaled By", K_E, 0, 19, 0, 1, MXSRC) \
 X(M2_VIA, "m2_via", "Matrix 2 Scaled By", K_E, 0, 19, 0, 1, MXSRC) \
 X(M3_VIA, "m3_via", "Matrix 3 Scaled By", K_E, 0, 19, 0, 1, MXSRC) \
 X(M4_VIA, "m4_via", "Matrix 4 Scaled By", K_E, 0, 19, 0, 1, MXSRC) \
 X(M5_VIA, "m5_via", "Matrix 5 Scaled By", K_E, 0, 19, 0, 1, MXSRC) \
 X(M6_VIA, "m6_via", "Matrix 6 Scaled By", K_E, 0, 19, 0, 1, MXSRC)

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
    [P_OSC1_WAVE]="Wave1", [P_OSC1_SHAPE]="Shp1", [P_OSC2_WAVE]="Wave2", [P_OSC2_SHAPE]="Shp2", [P_OSC2_COARSE]="Semi2", [P_OSC2_FINE]="Fine2", [P_OSC1_COARSE]="Semi1", [P_OSC1_FINE]="Fine1", [P_FX1_LVL]="FX 1", [P_FX2_LVL]="FX 2", [P_FX3_LVL]="FX 3", [P_FX4_LVL]="FX 4", [P_FX1_TYPE]="Type", [P_FX2_TYPE]="Type", [P_FX3_TYPE]="Type", [P_FX4_TYPE]="Type", [P_NOISE_COLOR]="Color", [P_FX_SCOPE]="Touch",[P_PRESET_CAT]="Bank", [P_BANK_IO]="Files", [P_BANK_STATUS]="State", [P_USER_BANKS]="User", [P_VERSION]="Ver", [P_PROG_CHG]="PgmCh", [P_LAST_CHANGE]="Last", [P_PC_SEEN]="PCs", [P_M1_VIA]="VIA1", [P_M2_VIA]="VIA2", [P_M3_VIA]="VIA3", [P_M4_VIA]="VIA4", [P_M5_VIA]="VIA5", [P_M6_VIA]="VIA6",
    [P_OSC1_LEVEL]="Lvl1", [P_OSC2_LEVEL]="Lvl2",
    [P_ENV1_A]="Atk", [P_ENV1_D]="Dec", [P_ENV1_S]="Sus", [P_ENV1_R]="Rel", [P_ENV2_A]="Atk", [P_ENV2_D]="Dec", [P_ENV2_S]="Sus", [P_ENV2_R]="Rel",
    [P_OSC1_FM1]="EFM1", [P_OSC2_FM1]="EFM2", [P_OSC1_FM2]="FM 1", [P_OSC2_FM2]="FM 2", [P_XMOD]="XMod", [P_SHAPE_MOD]="Shape",
    [P_LFO_RATE]="Rate", [P_LFO_WAVE]="Wave",
    [P_CUTOFF]="Cutoff", [P_RESONANCE]="Res", [P_FILTER_DRIVE]="Input", [P_FILTER_MODE]="Mode", [P_FLT_FM1]="FEnv", [P_FLT_FM2]="Cutoff",
    [P_KEY_TRACK]="Key", [P_DRIVE]="Drive",
    [P_NOISE_LEVEL]="Noise", [P_GLIDE]="Glide", [P_VIB_DEPTH]="Vib", [P_BEND_RANGE]="Bend", [P_DRIFT]="Drift", [P_LEGATO]="Legato",
    [P_DRONE]="Drone", [P_VOLUME]="Vol",
    [P_VEL_CUTOFF]="VCut", [P_VEL_AMP]="VAmp", [P_AT_DEST]="AT", [P_AT_AMOUNT]="Amt", [P_PRIORITY]="Prio", [P_OCTAVE]="Oct",
    [P_VIB_RATE]="VibRt", [P_TUNE]="Tune", [P_GRIT]="Grit",
    [P_STICK_X]="StkX", [P_STICK_Y]="StkY", [P_STICK_X_TO]="X To", [P_STICK_Y_TO]="Y To", [P_HOLD]="Hold",
    [P_ARP_MODE]="Arp", [P_ARP_RATE]="Rate", [P_ARP_OCT]="ArpOc", [P_ARP_GATE]="Gate", [P_M1_SRC]="Src1", [P_M1_DST]="Dst1", [P_M1_AMT]="Amt1", [P_M2_SRC]="Src2", [P_M2_DST]="Dst2", [P_M2_AMT]="Amt2", [P_M3_SRC]="Src3", [P_M3_DST]="Dst3", [P_M3_AMT]="Amt3", [P_M4_SRC]="Src4", [P_M4_DST]="Dst4", [P_M4_AMT]="Amt4", [P_M5_SRC]="Src5", [P_M5_DST]="Dst5", [P_M5_AMT]="Amt5", [P_M6_SRC]="Src6", [P_M6_DST]="Dst6", [P_M6_AMT]="Amt6", [P_LFO2_RATE]="Rt2", [P_LFO2_WAVE]="Wv2", [P_LFO_SYNC]="Sync", [P_LFO2_SYNC]="Syn2",
};
/* Graphics: envelopes draw as ADSR curves, cutoff+resonance as a filter response, volume as a fader. A group only draws when its
 * params sit contiguously on one row of a page: Env 1 / Env 2 have their four knobs first, Filter and root have cutoff, resonance first. */
static const char *const VIZ_GROUP[P_COUNT] = {
    [P_ENV1_A]="fenv", [P_ENV1_D]="fenv", [P_ENV1_S]="fenv", [P_ENV1_R]="fenv",
    [P_ENV2_A]="amp",  [P_ENV2_D]="amp",  [P_ENV2_S]="amp",  [P_ENV2_R]="amp",
    [P_CUTOFF]="filter", [P_RESONANCE]="filter", [P_FILTER_MODE]="filter",
    [P_LFO_WAVE]="lfo1", [P_LFO_RATE]="lfo1", [P_FLT_FM2]="lfo1",
};
static const char *const VIZ_ROLE[P_COUNT] = {
    [P_ENV1_A]="attack", [P_ENV1_D]="decay", [P_ENV1_S]="sustain", [P_ENV1_R]="release",
    [P_ENV2_A]="attack", [P_ENV2_D]="decay", [P_ENV2_S]="sustain", [P_ENV2_R]="release",
    [P_CUTOFF]="cutoff", [P_RESONANCE]="resonance", [P_FILTER_MODE]="mode",
    [P_LFO_WAVE]="shape", [P_LFO_RATE]="rate", [P_FLT_FM2]="depth",
};
static const char *const VIZ_KIND[P_COUNT] = { [P_VOLUME]="fader", [P_OSC1_LEVEL]="fader", [P_OSC2_LEVEL]="fader", [P_NOISE_LEVEL]="fader",
                                                [P_FX1_LVL]="fader", [P_FX2_LVL]="fader", [P_FX3_LVL]="fader", [P_FX4_LVL]="fader" };
/* Units: how the host prints a held knob's value (percent for 0..1 amounts, semitones, cents). */
static const char *const UNIT[P_COUNT] = {
    [P_OSC1_SHAPE]="%",
    [P_OSC2_SHAPE]="%",
    [P_OSC1_LEVEL]="%",
    [P_OSC2_LEVEL]="%",
    [P_ENV1_S]="%",
    [P_ENV2_S]="%",
    [P_OSC1_FM1]="%",
    [P_OSC2_FM1]="%",
    [P_OSC1_FM2]="%",
    [P_OSC2_FM2]="%",
    [P_XMOD]="%",
    [P_SHAPE_MOD]="%",
    [P_RESONANCE]="%",
    [P_FILTER_DRIVE]="%",
    [P_FLT_FM1]="%",
    [P_FLT_FM2]="%",
    [P_KEY_TRACK]="%",
    [P_DRIVE]="%",
    [P_NOISE_LEVEL]="%",
    [P_NOISE_COLOR]="%",
    [P_GLIDE]="%",
    [P_VIB_DEPTH]="%",
    [P_DRIFT]="%",
    [P_VOLUME]="%",
    [P_VEL_CUTOFF]="%",
    [P_VEL_AMP]="%",
    [P_AT_AMOUNT]="%",
    [P_GRIT]="%",
    [P_STICK_X]="%",
    [P_STICK_Y]="%",
    [P_ARP_GATE]="%",
    [P_FX1_LVL]="%",
    [P_FX2_LVL]="%",
    [P_FX3_LVL]="%",
    [P_FX4_LVL]="%",
    [P_M1_AMT]="%",
    [P_M2_AMT]="%",
    [P_M3_AMT]="%",
    [P_M4_AMT]="%",
    [P_M5_AMT]="%",
    [P_M6_AMT]="%",
    [P_OSC1_COARSE]="st",
    [P_OSC2_COARSE]="st",
    [P_BEND_RANGE]="st",
    [P_OCTAVE]="oct",
    [P_ARP_OCT]="oct",
    [P_OSC1_FINE]="cents",
    [P_OSC2_FINE]="cents",
    [P_TUNE]="cents",
};
static const char *const SHORT_OPTS[P_COUNT] = {
    [P_OSC1_WAVE]="OFF,SAW,TRI,S+T,PLS,S+P,T+P,ALL", [P_OSC2_WAVE]="OFF,SAW,TRI,S+T,PLS,S+P,T+P,ALL", [P_LFO_WAVE]="TRI,SQR,SAW,RMP,RND", [P_LFO2_WAVE]="TRI,SQR,SAW,RMP,RND",
    [P_LFO_SYNC]="FRE,1/2,1/4,1/8,8T,16,16T,32", [P_LFO2_SYNC]="FRE,1/2,1/4,1/8,8T,16,16T,32",
    [P_FILTER_MODE]="LP,BP,HP,L+H,B+L", [P_LEGATO]="OFF,ON", [P_DRONE]="OFF,HLD,LTC", [P_AT_DEST]="OFF,CUT,VIB,DRV,FM", [P_PRIORITY]="LST,LOW,HI",
    [P_STICK_X_TO]="BND,VSP,CUT,GRT,ARR,ARM", [P_STICK_Y_TO]="GLD,VDP,CUT,GRT,HLD,ARR,ARM", [P_FX1_TYPE]=FXT_S, [P_FX2_TYPE]=FXT_S, [P_FX3_TYPE]=FXT_S, [P_FX4_TYPE]=FXT_S, [P_PRESET_CAT]="INI,BAS,SUB,LED,PLK,KEY,STB,BRS,ARP,SEQ,PAD,TXT,VOX,DRN,FX,HOR,SET,U1,U2,U3,U4,U5,U6,U7,U8", [P_BANK_IO]="IDL,EXP,RLD", [P_BANK_STATUS]="RDY,WRK,EXP,LOD,ERR", [P_VERSION]=MM_VERSION_S, [P_M1_VIA]=MXSRC_S, [P_M2_VIA]=MXSRC_S, [P_M3_VIA]=MXSRC_S, [P_M4_VIA]=MXSRC_S, [P_M5_VIA]=MXSRC_S, [P_M6_VIA]=MXSRC_S,[P_HOLD]="OFF,ON", [P_FX_SCOPE]="PAG,ANY",
    [P_ARP_MODE]="OFF,UP,DWN,U+D,RND,ASP", [P_ARP_RATE]="1/4,1/8,8T,16,16T,32",
    [P_M1_SRC]=MXSRC_S, [P_M1_DST]=MXDST_S, [P_M2_SRC]=MXSRC_S, [P_M2_DST]=MXDST_S, [P_M3_SRC]=MXSRC_S, [P_M3_DST]=MXDST_S, [P_M4_SRC]=MXSRC_S, [P_M4_DST]=MXDST_S, [P_M5_SRC]=MXSRC_S, [P_M5_DST]=MXDST_S, [P_M6_SRC]=MXSRC_S, [P_M6_DST]=MXDST_S,
};

/* ---------------------------------------------------------------- DSP state */
enum { ENV_IDLE, ENV_ATK, ENV_DEC, ENV_SUS, ENV_REL };
typedef struct { int stage; float level; } env_t;
typedef struct { float phase; float drift; } osc_t;
typedef struct { float fdrive, drive, res, noise, drift; } gbase_t;   /* knob values the Grit macro starts from */

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
    float lfo2_ph, lfo_rv[2], mx_lr2;   /* LFO 2 phase, random-wave values, previous LFO 2 rate mod */
    float blk_bpm; double blk_beat;     /* tempo and transport beat at the start of this block (beat < 0: transport stopped) */
    uint8_t touch_mask;                    /* knobs 1-8 currently touched (notes 0-7) */
    uint32_t clk;                          /* render_block counter */
    uint32_t fx_page_t;                    /* clk+1 when the host last read an FX Bank key (0 = never): that page is on screen */
    uint8_t fx_armed;                      /* touches of knobs 1-4 that began while the FX Bank was allowed to act */
    uint32_t tap_t[4], touch_t[4];         /* FX bank: block of the last tap start / of the current touch start */
    uint8_t tap_ok, tap_short, fx_sup;     /* bit k: a first tap is waiting for its partner / it was short / this touch was a double tap (silent until release) */
    float fxa[N_FXT], fx_tgt[N_FXT];       /* effect amounts by type: slewed (heard) and target (touch state) */
    int fx_hold_on, fx_hold_saved;         /* the Hold effect forced Hold on; the value to put back */
    float chop_ph, wob_ph, chop_g;         /* Gate Chop / Wobble phases (tempo locked) and the smoothed chop gain */
    gbase_t sm_gb; float sm_grit, sm_cut, sm_vol; int sm_init;   /* knob values smoothed per sample (cutoff, resonance, drives, grit, volume): no zipper from stepped knobs */
    int stk_lock[2];                                       /* stick flick-to-step latch per axis */
    float expr;                 /* CC11 expression, scales the output (1 = full) */
    int cc_bank, rpn_msb, rpn_lsb;   /* CC0 bank for Program Change; selected RPN */
    int why, pc_seen;                /* what changed the preset last (diagnostic), Program Change messages seen (any source) */
    float rnd;                  /* Random mod source: sample-and-hold, new value per note trigger */
    float mx_lfr;               /* previous sample's LFO-rate modulation (breaks the LFO feedback loop) */
    float mx_ld[2];             /* previous sample's LFO 1 / LFO 2 Depth modulation (applied one sample late: no loop) */
    float bend_n, arp_ramp, age, srnd, srnd1, srnd_tgt, ped, o1_prev; int srnd_cnt;   /* matrix sources: bend -1..1, arp step 0..1, note age 0..1, smooth random, pedal (slewed), osc 1 output */
    uint32_t rng;
    float dc_x, dc_y;
    float noise_lp1, noise_lp2;
    int preset;
    uint8_t phys[128]; int phys_n;       /* keys physically down */
    int held[MAX_NOTES], held_vel[MAX_NOTES], held_n;   /* notes held by keys, pedal or Hold (the arp / voice source) */
    int prev_arp, prev_hold;
    int arp_running, arp_idx, arp_note; float arp_pos;
    int touch_seen; uint8_t touched[2]; int stick_age[2]; int y_down, y_dwell;   /* joystick emulation */
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

static const float SYNC_BEATS[8] = {0.0f, 2.0f, 1.0f, 0.5f, 1.0f / 3.0f, 0.25f, 1.0f / 6.0f, 0.125f};   /* beats per LFO cycle */
static inline float lfo_shape(float ph, int wave, float rv) {
    switch (wave) {
    case 1: return ph < 0.5f ? 1.0f : -1.0f;
    case 2: return 1.0f - 2.0f * ph;
    case 3: return 2.0f * ph - 1.0f;
    case 4: return rv;
    default: return ph < 0.5f ? 4.0f * ph - 1.0f : 3.0f - 4.0f * ph;
    }
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
        m->rnd = frand(&m->rng);
        m->age = 0.0f;
        if (m->e2.level < 0.01f) m->sm_init = 0;   /* a note starting from (near) silence takes the knobs as they are now: no glide from stale values */
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
    m->arp_running = 0; m->arp_ramp = 0.0f;
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
    m->arp_ramp = len > 1 ? (float)i / (float)(len - 1) : 0.0f;
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
    m->touch_mask = 0; m->tap_ok = 0; m->fx_sup = 0;
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
/* Readability helpers for matrix slots: MX(slot, source, target, amount) */
#define S_LFO 1
#define S_E1 2
#define S_E2 3
#define S_VEL 4
#define S_WHL 5
#define S_AT 6
#define S_SX 7
#define S_SY 8
#define S_KEY 9
#define S_RND 10
#define S_LF2 11
#define D_PCH 1
#define D_PC2 2
#define D_SH1 3
#define D_SH2 4
#define D_LV1 5
#define D_LV2 6
#define D_NSE 7
#define D_CUT 8
#define D_RES 9
#define D_INP 10
#define D_DRV 11
#define D_AMP 12
#define D_XMD 13
#define D_LFR 14
#define D_LR2 15
#define S_NSE 12
#define S_OS2 13
#define D_FM1 16
#define D_FM2 17
#define S_BND 14
#define S_ARP 15
#define S_AGE 16
#define S_SMR 17
#define S_OS1 18
#define S_PED 19
#define D_GRT 18
#define D_GLD 19
#define D_VIB 20
#define D_LD1 21
#define D_LD2 22
#define D_ENV 23
#define D_DFT 24
#define MX(n, s, d, a) {P_M##n##_SRC, s}, {P_M##n##_DST, d}, {P_M##n##_AMT, a}
static const preset_t PRESETS[] = {
    /* ---- Init ---- */
    PRE("Init", {P_VOLUME, 0.7f}),
    PRE("Init Saw",{P_CUTOFF,.88f},{P_RESONANCE,.1f},{P_FILTER_DRIVE,.2f},{P_DRIVE,.05f},{P_FLT_FM1,0},{P_DRIFT,.1f}),
    PRE("Init Square",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.5f},{P_CUTOFF,.82f},{P_RESONANCE,.1f},{P_FILTER_DRIVE,.2f},{P_DRIVE,.05f},{P_FLT_FM1,0},{P_DRIFT,.1f}),
    PRE("Init Detuned",{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.85f},{P_RESONANCE,.1f},{P_FILTER_DRIVE,.2f},{P_DRIVE,.05f},{P_FLT_FM1,0}),
    PRE("Init Sub Octave",{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.8f},{P_RESONANCE,.1f},{P_FILTER_DRIVE,.2f},{P_DRIVE,.05f},{P_FLT_FM1,0}),
    PRE("Init Noise",{P_OSC1_LEVEL,0},{P_NOISE_LEVEL,.8f},{P_FILTER_MODE,1},{P_CUTOFF,.62f},{P_RESONANCE,.4f},{P_KEY_TRACK,.7f},{P_FLT_FM1,0}),
    /* ---- Bass ---- */
    PRE("Bass Acid Line",{P_FILTER_DRIVE,0.75f},{P_CUTOFF,.35f},{P_RESONANCE,.6f},{P_FLT_FM1,.8f},{P_ENV1_D,.6f},{P_ENV2_D,.68f},{P_ENV2_S,.35f},
        {P_ENV2_R,.25f},{P_GLIDE,.15f},{P_LEGATO,1},{P_DRIVE,0.5f}),
    PRE("Bass Acid Squelch",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC2_LEVEL,0},{P_CUTOFF,.28f},{P_RESONANCE,.78f},{P_FLT_FM1,1},{P_ENV1_D,.5f},{P_ENV2_D,.6f},
        {P_ENV2_S,.7f},{P_ENV2_R,.2f},{P_GLIDE,.12f},{P_LEGATO,1}),
    PRE("Bass Brute",{P_OSC1_WAVE,7},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,7},{P_OSC2_FINE,7},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.45f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,1},{P_DRIVE,1},{P_FLT_FM1,0.3f}),
    PRE("Bass Chewy",{P_FILTER_MODE,4},{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.5f},{P_OSC2_COARSE,-12},{P_CUTOFF,0.38f},
        {P_RESONANCE,0.6f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM2,0.45f},{P_LFO_RATE,0.55f}),
    PRE("Bass Dirty Sub",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,0.9f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.2f},{P_OSC2_COARSE,-12},
        {P_OSC2_LEVEL,0.4f},{P_CUTOFF,0.35f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.8f},{P_ENV2_R,0.3f}),
    PRE("Bass Fat Pulse",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,-12},
        {P_OSC2_FINE,6},{P_CUTOFF,.35f},{P_RESONANCE,.25f},{P_FLT_FM1,.6f}),
    PRE("Bass Funk",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.25f},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,.6f},{P_FILTER_MODE,4},
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
    PRE("Bass Mean Acid",{P_OSC2_LEVEL,0},{P_CUTOFF,0.26f},{P_RESONANCE,0.82f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,1},
        {P_ENV1_D,0.55f},{P_ENV1_S,0.05f},{P_ENV2_D,0.6f},{P_ENV2_S,0.7f},{P_ENV2_R,0.2f},{P_GLIDE,0.12f}),
    PRE("Bass Pulse Walker",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.35f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.6f},{P_OSC2_COARSE,-12},
        {P_OSC2_FINE,4},{P_CUTOFF,.4f},{P_RESONANCE,.3f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV1_S,.2f},{P_PRIORITY,1}),
    PRE("Bass Rubber",{P_DRIVE,0.55f},{P_FILTER_DRIVE,0.85f},{P_OSC1_WAVE,4},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_GLIDE,.2f},{P_LEGATO,1},
        {P_CUTOFF,.3f},{P_RESONANCE,.35f},{P_FLT_FM1,.7f}),
    PRE("Bass Rust",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_OSC2_WAVE,1},{P_OSC2_FINE,-11},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.4f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_NOISE_LEVEL,0.06f},{P_FLT_FM1,0.4f},{P_ENV1_D,0.55f},
        {P_ENV2_S,0.85f}),
    PRE("Bass Snarl",{P_DRIVE,0.6f},{P_OSC1_WAVE,5},{P_OSC1_SHAPE,.45f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,.30f},{P_RESONANCE,.5f},
        {P_FILTER_DRIVE,0.85f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV2_D,.6f},{P_ENV2_S,.8f}),
    PRE("Bass Sub Saw",{P_DRIVE,0.45f},{P_FILTER_DRIVE,0.75f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.38f},
        {P_RESONANCE,.2f},{P_FLT_FM1,.4f},{P_ENV1_D,.5f},{P_ENV2_R,.2f}),
    PRE("Bass Sync Growl",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_XMOD,.55f},{P_CUTOFF,.42f},
        {P_RESONANCE,.4f},{P_FLT_FM1,.4f},{P_ENV1_D,.55f},{P_FLT_FM2,.3f}),
    PRE("Bass Wobble",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.3f},{P_RESONANCE,.55f},
        {P_FLT_FM2,.8f},{P_LFO_RATE,.6f}),
    PRE("Bass Wub Matrix",{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,6},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.3f},{P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.5f},{P_ENV2_S,0.9f},{P_LFO_RATE,0.45f},{P_M1_SRC,1},{P_M1_DST,8},{P_M1_AMT,0.45f},{P_M2_SRC,8},{P_M2_DST,14},{P_M2_AMT,0.4f},{P_M3_SRC,7},{P_M3_DST,9},{P_M3_AMT,0.4f}),
    PRE("Bass Grind Stick*",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,9},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.3f},
        {P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},{P_FLT_FM1,0.5f},{P_ENV1_D,0.5f},{P_STICK_X_TO,2},{P_STICK_Y_TO,3},
        MX(1,S_SY,D_INP,0.5f),MX(2,S_SX,D_RES,0.3f)),
    PRE("Bass Iron Wub",{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_CUTOFF,0.32f},{P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0.2f},{P_LFO_SYNC,3},{P_LFO2_SYNC,5},{P_LFO2_WAVE,2},MX(1,S_LFO,D_CUT,0.45f),MX(2,S_LF2,D_DRV,0.2f)),
    PRE("Bass Rust Crawl",{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,12},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.27f},{P_RESONANCE,0.45f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.6f},{P_NOISE_LEVEL,0.08f},{P_LFO2_RATE,0.15f},{P_LFO2_WAVE,4},{P_GLIDE,0.1f},{P_LEGATO,1},
        MX(1,S_LF2,D_CUT,0.35f),MX(2,S_RND,D_RES,0.25f)),
    PRE("Bass Gnash",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_OSC2_LEVEL,0.8f},{P_XMOD,0.7f},{P_CUTOFF,0.42f},
        {P_RESONANCE,0.3f},{P_FILTER_DRIVE,1},{P_DRIVE,0.9f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.45f},MX(1,S_E1,D_XMD,0.3f),MX(2,S_VEL,D_DRV,0.4f)),
    PRE("Bass Furnace",{P_OSC1_WAVE,7},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_NOISE_LEVEL,0.15f},{P_CUTOFF,0.36f},{P_RESONANCE,0.4f},
        {P_FILTER_DRIVE,1},{P_DRIVE,1},{P_FLT_FM1,0.5f},{P_VEL_AMP,0.3f},{P_AT_DEST,0},MX(1,S_VEL,D_CUT,0.3f),MX(2,S_AT,D_INP,0.5f)),
    PRE("Bass Key Rumble",{P_FILTER_MODE,4},{P_OSC1_WAVE,2},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.2f},{P_OSC2_COARSE,-12},
        {P_OSC2_LEVEL,0.5f},{P_CUTOFF,0.3f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_LFO_RATE,0.4f},
        MX(1,S_KEY,D_RES,0.4f),MX(2,S_LFO,D_SH2,0.5f)),
    PRE("Bass Audio Cutoff",{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,3},{P_OSC2_LEVEL,0},{P_OSC1_COARSE,-12},{P_CUTOFF,0.36f},{P_RESONANCE,0.5f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},{P_FLT_FM1,0.2f},MX(1,S_OS2,D_CUT,0.12f)),
    PRE("Bass Key Grit",{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.4f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.7f},
        {P_DRIVE,0.5f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.5f},{P_ENV2_S,0.9f},MX(1,S_KEY,D_GRT,-0.9f)),
    PRE("Bass Velocity Grit",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,6},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.36f},{P_RESONANCE,0.35f},
        {P_FILTER_DRIVE,0.7f},{P_DRIVE,0.5f},{P_FLT_FM1,0.2f},{P_ENV1_D,0.45f},{P_ENV2_S,0.9f},{P_VEL_AMP,0.3f},MX(1,S_VEL,D_GRT,0.8f),MX(2,S_VEL,D_ENV,0.5f)),
    PRE("Bass Clean Pulse",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.4f},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.66f},{P_RESONANCE,.2f},
        {P_FILTER_DRIVE,.2f},{P_DRIVE,.1f},{P_FLT_FM1,.5f},{P_ENV1_D,.4f},{P_ENV2_D,.6f},{P_ENV2_S,.8f},{P_ENV2_R,.15f}),
    PRE("Bass Round Tri",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.5f},{P_RESONANCE,.1f},{P_FILTER_DRIVE,.2f},
        {P_DRIVE,.15f},{P_FLT_FM1,.3f},{P_ENV2_R,.2f}),
    /* ---- Sub ---- */
    PRE("Sub Dub",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.7f},{P_OSC1_WAVE,2},{P_OSC1_LEVEL,.9f},{P_OSC2_LEVEL,0},{P_CUTOFF,.36f},{P_RESONANCE,.25f},{P_FLT_FM1,.1f},
        {P_ENV2_A,.1f},{P_ENV2_R,.4f}),
    PRE("Sub Hollow",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.5f},
        {P_RESONANCE,0},{P_FILTER_DRIVE,.3f}),
    PRE("Sub Black Hole",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,1},{P_OSC2_LEVEL,0},{P_OCTAVE,-1},{P_CUTOFF,0.3f},{P_RESONANCE,0.15f},
        {P_FILTER_DRIVE,0.7f},{P_DRIVE,0.6f},{P_FLT_FM1,0},{P_ENV2_A,0.05f},{P_ENV2_R,0.4f}),
    PRE("Sub Pressure",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_FINE,5},{P_OCTAVE,-1},{P_CUTOFF,0.33f},{P_RESONANCE,0.2f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},{P_FLT_FM1,0},{P_LFO_RATE,0.3f},MX(1,S_LFO,D_DRV,0.25f)),
    PRE("Sub Tremor",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.3f},{P_NOISE_LEVEL,0.1f},
        {P_OCTAVE,-1},{P_CUTOFF,0.3f},{P_RESONANCE,0.25f},{P_DRIVE,0.7f},{P_FLT_FM1,0},{P_LFO_SYNC,3},MX(1,S_LFO,D_AMP,0.25f)),
    PRE("Sub Glide Drop",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,1},{P_OSC2_LEVEL,0},{P_OCTAVE,-1},{P_GLIDE,0.45f},{P_LEGATO,1},{P_FLT_FM1,0},
        {P_DRIVE,0.6f},{P_RESONANCE,0.2f},{P_CUTOFF,0.35f},{P_STICK_Y_TO,0}),
    PRE("Sub Grit Floor",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.4f},{P_OSC2_LEVEL,0},{P_OCTAVE,-1},{P_CUTOFF,0.31f},{P_RESONANCE,0.35f},
        {P_FILTER_DRIVE,1},{P_DRIVE,1},{P_GRIT,0.3f},{P_FLT_FM1,0.1f}),
    PRE("Sub Reactor",{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_FINE,3},{P_OCTAVE,-1},{P_XMOD,0.25f},{P_CUTOFF,0.34f},{P_RESONANCE,0.4f},
        {P_FILTER_DRIVE,0.85f},{P_DRIVE,0.7f},{P_ENV1_D,0.6f},{P_ENV1_S,0.3f},{P_FLT_FM1,0.3f},{P_LFO2_RATE,0.25f},
        MX(1,S_E1,D_CUT,0.3f),MX(2,S_LF2,D_XMD,0.3f)),
    PRE("Sub Loose Tuning",{P_OSC1_WAVE,2},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,2},{P_OSC2_LEVEL,0.6f},{P_OCTAVE,-1},{P_CUTOFF,0.35f},{P_RESONANCE,0.2f},
        {P_DRIVE,0.4f},{P_FLT_FM1,0},{P_DRIFT,0.5f},{P_ENV2_S,0.9f},MX(1,S_SMR,D_DFT,0.5f),MX(2,S_SMR,D_PC2,0.015f)),
    /* ---- Lead ---- */
    PRE("Lead Broken Radio",{P_FILTER_MODE,1},{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,18},{P_NOISE_LEVEL,0.12f},
        {P_CUTOFF,0.62f},{P_RESONANCE,0.6f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.7f}),
    PRE("Lead Bubble",{P_DRIVE,0.45f},{P_FILTER_DRIVE,0.7f},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_VIB_DEPTH,.3f},{P_OSC1_FM2,.15f},
        {P_LFO_RATE,.7f},{P_GLIDE,.3f},{P_LEGATO,1},{P_CUTOFF,.6f},{P_RESONANCE,.4f}),
    PRE("Lead Burn",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,4},{P_CUTOFF,0.56f},{P_RESONANCE,0.65f},
        {P_FILTER_DRIVE,1},{P_DRIVE,0.85f},{P_FLT_FM1,0.35f},{P_ENV1_D,0.6f},{P_ENV1_S,0.35f},{P_AT_DEST,1},{P_AT_AMOUNT,0.7f},
        {P_LEGATO,1}),
    PRE("Lead Dirty Square",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.35f},{P_OSC2_FINE,12},
        {P_CUTOFF,0.6f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.7f},{P_SHAPE_MOD,0.3f}),
    PRE("Lead Dive",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,5},{P_OSC2_LEVEL,0},{P_OSC1_FM1,.35f},{P_ENV1_D,.36f},{P_ENV1_S,0},{P_CUTOFF,.7f},
        {P_RESONANCE,.35f},{P_FLT_FM1,.2f},{P_ENV2_S,.9f},{P_ENV2_R,.3f}),
    PRE("Lead Expressive",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.5f},{P_RESONANCE,.45f},{P_FLT_FM1,.2f},
        {P_ENV1_D,.6f},{P_ENV1_S,.4f},{P_VEL_CUTOFF,.7f},{P_VEL_AMP,.4f},{P_AT_DEST,1},{P_AT_AMOUNT,.8f}),
    PRE("Lead FM Zap",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_XMOD,.8f},{P_OSC1_FM1,.9f},{P_ENV1_D,.55f},
        {P_ENV2_D,.7f},{P_ENV2_S,.4f},{P_ENV2_R,.3f},{P_CUTOFF,.8f}),
    PRE("Lead Fifth",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.66f},
        {P_RESONANCE,.25f},{P_FLT_FM1,.25f},{P_ENV1_D,.55f},{P_ENV1_S,.4f},{P_GLIDE,.12f}),
    PRE("Lead Fuzz",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_CUTOFF,.7f},{P_RESONANCE,.3f},{P_FILTER_DRIVE,.95f},
        {P_DRIVE,.8f},{P_FLT_FM1,.2f},{P_ENV1_D,.6f},{P_ENV1_S,.5f},{P_GLIDE,.18f}),
    PRE("Lead Laser Sweep",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_OSC2_LEVEL,0},{P_CUTOFF,.85f},{P_FLT_FM1,-.8f},{P_ENV1_D,.6f},{P_ENV1_S,0},{P_RESONANCE,.5f}),
    PRE("Lead Mono Sync",{P_DRIVE,0.55f},{P_FILTER_DRIVE,0.85f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,2},{P_XMOD,.7f},{P_CUTOFF,.68f},
        {P_RESONANCE,.3f},{P_FLT_FM1,.3f},{P_ENV1_D,.6f},{P_ENV1_S,.4f},{P_VIB_DEPTH,.2f},{P_GLIDE,.15f}),
    PRE("Lead Rasp",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.62f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},
        {P_DRIVE,0.6f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.6f},{P_ENV1_S,0.4f},{P_VIB_DEPTH,0.18f}),
    PRE("Lead Scream Hot",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_CUTOFF,0.7f},{P_RESONANCE,0.72f},
        {P_FILTER_DRIVE,0.95f},{P_DRIVE,0.8f},{P_FLT_FM1,0.25f},{P_GLIDE,0.2f},{P_LEGATO,1},{P_VIB_DEPTH,0.25f},
        {P_VIB_RATE,0.65f}),
    PRE("Lead Screamer",{P_DRIVE,0.5f},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.62f},{P_RESONANCE,.7f},{P_FILTER_DRIVE,0.8f},
        {P_GLIDE,.25f},{P_VIB_DEPTH,.25f},{P_ENV2_R,.3f},{P_LEGATO,1}),
    PRE("Lead Siren",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.75f},{P_OSC1_WAVE,2},{P_OSC2_LEVEL,0},{P_OSC1_FM2,.5f},{P_LFO_RATE,.55f},{P_CUTOFF,.7f},{P_RESONANCE,.3f},
        {P_FLT_FM1,0},{P_DRONE,0},{P_ENV2_A,.3f}),
    PRE("Lead Snarl Sync",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,3},{P_XMOD,0.85f},{P_CUTOFF,0.65f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_OSC2_FM1,0.1f},{P_FLT_FM1,0.3f},{P_ENV1_D,0.55f}),
    PRE("Lead Soft Tri",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.4f},{P_CUTOFF,.62f},{P_RESONANCE,.15f},
        {P_FLT_FM1,.2f},{P_ENV1_D,.6f},{P_ENV1_S,.5f},{P_ENV2_A,.2f},{P_ENV2_R,.4f},{P_GLIDE,.2f},{P_LEGATO,1}),
    PRE("Lead Whistle",{P_OSC1_LEVEL,.05f},{P_OSC2_LEVEL,0},{P_RESONANCE,1},{P_CUTOFF,.7f},{P_KEY_TRACK,1},{P_FLT_FM1,0},
        {P_FILTER_DRIVE,.3f},{P_VIB_DEPTH,.2f},{P_GLIDE,.15f}),
    PRE("Lead Stick Scream*",{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.35f},{P_OSC2_FINE,8},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.55f},{P_GLIDE,0.12f},{P_LEGATO,1},{P_ENV2_S,0.9f},{P_M1_SRC,8},{P_M1_DST,10},{P_M1_AMT,0.7f},{P_M2_SRC,6},{P_M2_DST,4},{P_M2_AMT,0.5f},{P_M3_SRC,5},{P_M3_DST,1},{P_M3_AMT,0.08f}),
    PRE("Lead Stick Siren*",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.4f},{P_OSC2_WAVE,1},{P_OSC2_FINE,12},{P_CUTOFF,0.62f},{P_RESONANCE,0.35f},
        {P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_GLIDE,0.15f},{P_LEGATO,1},{P_BEND_RANGE,5},MX(1,S_SY,D_CUT,0.5f),MX(2,S_SY,D_SH1,0.5f)),
    PRE("Lead Acid Spit",{P_OSC2_LEVEL,0},{P_CUTOFF,0.45f},{P_RESONANCE,0.7f},{P_FLT_FM1,0.9f},{P_ENV1_D,0.4f},{P_DRIVE,0.6f},
        {P_FILTER_DRIVE,0.9f},{P_GLIDE,0.08f},{P_LEGATO,1},{P_AT_DEST,0},MX(1,S_VEL,D_RES,0.3f),MX(2,S_AT,D_CUT,0.5f)),
    PRE("Lead Wasp",{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.3f},{P_OSC2_FINE,9},{P_XMOD,0.4f},{P_CUTOFF,0.6f},{P_RESONANCE,0.4f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},{P_VIB_DEPTH,0.15f},{P_LFO2_RATE,0.45f},MX(1,S_LF2,D_SH2,0.6f),MX(2,S_WHL,D_XMD,0.4f)),
    PRE("Lead Hard Sync",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_XMOD,0.6f},{P_FLT_FM1,0.5f},{P_CUTOFF,0.55f},{P_DRIVE,0.7f},
        {P_ENV1_D,0.55f},MX(1,S_E1,D_PC2,0.35f)),
    PRE("Lead Brute Mono",{P_OSC1_WAVE,7},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,7},{P_OSC2_FINE,8},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.55f},
        {P_RESONANCE,0.3f},{P_FILTER_DRIVE,1},{P_DRIVE,1},{P_GLIDE,0.1f},{P_LEGATO,1},{P_PRIORITY,2},MX(1,S_KEY,D_CUT,0.3f)),
    PRE("Lead Cutting Wire",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.2f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.4f},{P_CUTOFF,0.7f},
        {P_RESONANCE,0.45f},{P_FILTER_MODE,4},{P_FILTER_DRIVE,0.8f},{P_LFO_RATE,0.62f},MX(1,S_LFO,D_PCH,0.02f),MX(2,S_AT,D_CUT,0.4f)),
    PRE("Lead Noise Bite",{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,0.58f},{P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},
        {P_GLIDE,0.1f},{P_LEGATO,1},MX(1,S_NSE,D_FM1,0.06f),MX(2,S_VEL,D_CUT,0.3f)),
    PRE("Lead Bend Open",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.42f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},
        {P_GLIDE,0.1f},{P_LEGATO,1},{P_BEND_RANGE,5},{P_ENV2_S,0.9f},MX(1,S_BND,D_CUT,0.5f),MX(2,S_BND,D_GRT,0.6f)),
    PRE("Lead Wheel Vibrato",{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_FINE,7},{P_CUTOFF,0.55f},{P_RESONANCE,0.3f},{P_DRIVE,0.4f},{P_GLIDE,0.12f},{P_LEGATO,1},
        {P_ENV2_S,0.9f},MX(1,S_WHL,D_VIB,0.8f),MX(2,S_AT,D_GRT,0.6f)),
    PRE("Lead Pedal Scream",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_CUTOFF,0.5f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.4f},
        {P_GLIDE,0.1f},{P_LEGATO,1},{P_ENV2_S,0.9f},MX(1,S_PED,D_GRT,0.8f),MX(2,S_PED,D_RES,0.25f)),
    PRE("Lead Soft Slide",{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_FINE,6},{P_CUTOFF,0.5f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.45f},
        {P_GLIDE,0.3f},{P_LEGATO,1},{P_ENV2_S,0.9f},MX(1,S_VEL,D_GLD,-0.28f)),
    PRE("Lead Clean Saw",{P_OSC2_WAVE,1},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.88f},{P_RESONANCE,.15f},{P_FILTER_DRIVE,.2f},{P_DRIVE,.1f},{P_FLT_FM1,.15f},
        {P_ENV1_D,.5f},{P_ENV1_S,.5f},{P_VIB_DEPTH,.12f},{P_GLIDE,.1f},{P_LEGATO,1}),
    PRE("Lead PWM Sweep",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.5f},{P_OSC2_FINE,6},{P_SHAPE_MOD,.7f},{P_LFO_RATE,.25f},{P_CUTOFF,.8f},
        {P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_FLT_FM1,.15f},{P_ENV2_R,.3f}),
    PRE("Lead Glass Sync",{P_OSC2_WAVE,1},{P_OSC2_COARSE,19},{P_OSC2_FINE,0},{P_XMOD,.5f},{P_CUTOFF,.92f},{P_RESONANCE,.15f},{P_FILTER_DRIVE,.2f},{P_FLT_FM1,.1f},
        {P_VIB_DEPTH,.15f},MX(1,S_WHL,D_XMD,.45f)),
    PRE("Lead Thin Highpass",{P_OSC1_WAVE,5},{P_FILTER_MODE,2},{P_CUTOFF,.55f},{P_RESONANCE,.35f},{P_FILTER_DRIVE,.5f},{P_DRIVE,.3f},{P_FLT_FM1,0},{P_VIB_DEPTH,.1f}),
    /* ---- Pluck ---- */
    PRE("Pluck Iron Wire",{P_OSC2_WAVE,4},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.3f},{P_CUTOFF,0.5f},{P_RESONANCE,0.35f},{P_FLT_FM1,0.8f},
        {P_ENV1_D,0.45f},{P_ENV2_D,0.55f},{P_ENV2_S,0.08f},{P_ENV2_R,0.3f},{P_DRIVE,0.4f}),
    PRE("Pluck Rusty Nail",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_NOISE_LEVEL,0.05f},{P_CUTOFF,0.45f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.8f},
        {P_DRIVE,0.5f},{P_FLT_FM1,0.9f},{P_ENV1_D,0.4f},{P_ENV2_D,0.5f},{P_ENV2_S,0.08f},{P_ENV2_R,0.25f}),
    PRE("Pluck Dark Koto",{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,5},{P_OSC2_LEVEL,0.3f},{P_XMOD,0.15f},
        {P_CUTOFF,0.42f},{P_RESONANCE,0.4f},{P_FLT_FM1,0.7f},{P_ENV1_D,0.45f},{P_ENV2_D,0.6f},{P_ENV2_S,0.08f},{P_ENV2_R,0.4f}),
    PRE("Pluck Grit Mallet",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_LEVEL,0.4f},{P_OSC1_FM1,0.25f},{P_CUTOFF,0.5f},
        {P_RESONANCE,0.2f},{P_ENV2_D,0.5f},{P_ENV2_S,0.08f},{P_ENV2_R,0.35f},{P_DRIVE,0.5f}),
    PRE("Pluck Hollow Pipe",{P_FILTER_MODE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.5f},{P_CUTOFF,0.5f},{P_RESONANCE,0.6f},{P_FLT_FM1,0.4f},
        {P_ENV1_D,0.5f},{P_ENV2_D,0.55f},{P_ENV2_S,0.08f}),
    PRE("Pluck Chain Gang",{P_OSC2_WAVE,1},{P_OSC2_FINE,15},{P_CUTOFF,0.4f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},
        {P_FLT_FM1,0.9f},{P_ENV1_D,0.42f},{P_ENV2_D,0.5f},{P_ENV2_S,0.08f},MX(1,S_VEL,D_DRV,0.5f)),
    PRE("Pluck Stick Pluck*",{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_FLT_FM1,0.8f},{P_ENV2_D,0.55f},{P_ENV2_S,0.08f},{P_STICK_X_TO,2},{P_STICK_Y_TO,3},
        MX(1,S_SY,D_RES,0.4f),MX(2,S_SX,D_LV2,0.5f)),
    PRE("Pluck Hard Hit",{P_OSC1_WAVE,1},{P_OSC2_WAVE,2},{P_OSC2_FINE,8},{P_CUTOFF,0.35f},{P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.4f},
        {P_FLT_FM1,0.2f},{P_ENV1_D,0.35f},{P_ENV1_S,0},{P_ENV2_D,0.55f},{P_ENV2_S,0.1f},{P_ENV2_R,0.3f},MX(1,S_VEL,D_ENV,0.8f),MX(2,S_VEL,D_DRV,0.4f)),
    PRE("Pluck Reso",{P_OSC2_LEVEL,0},{P_CUTOFF,.3f},{P_RESONANCE,.65f},{P_FLT_FM1,.9f},{P_ENV1_D,.6f},{P_ENV2_D,.68f}),
    PRE("Pluck Rusty",{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.4f},{P_CUTOFF,0.34f},
        {P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.7f},{P_DRIVE,0.55f},{P_FLT_FM1,0.9f},{P_ENV1_D,0.6f},{P_ENV1_S,0.05f},
        {P_ENV2_D,0.7f},{P_ENV2_S,0.25f}),
    PRE("Pluck Trance",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,.45f},{P_RESONANCE,.45f},{P_FLT_FM1,.7f},
        {P_ENV1_D,.6f},{P_ENV2_D,.68f}),
    PRE("Pluck Glass Tine",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.3f},{P_XMOD,.5f},{P_OSC1_FM1,.6f},{P_CUTOFF,.85f},
        {P_RESONANCE,.1f},{P_FLT_FM1,0},{P_ENV1_D,.45f},{P_ENV2_D,.6f},{P_ENV2_S,.12f},{P_ENV2_R,.5f}),
    PRE("Pluck Bright Saw",{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_CUTOFF,.7f},{P_RESONANCE,.25f},{P_FLT_FM1,.7f},{P_ENV1_D,.4f},{P_ENV2_D,.55f},{P_ENV2_S,.12f},{P_ENV2_R,.3f}),
    /* ---- Keys ---- */
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
    PRE("Keys Mallet",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.3f},{P_CUTOFF,.7f},
        {P_RESONANCE,.15f},{P_FLT_FM1,.3f},{P_ENV1_D,.5f},{P_ENV1_S,0},{P_ENV2_D,.72f},{P_ENV2_S,.15f}),
    PRE("Keys Metal Clang",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,5},{P_OSC2_FINE,40},{P_XMOD,1},{P_OSC1_FM2,.3f},
        {P_LFO_RATE,.8f},{P_CUTOFF,.9f},{P_RESONANCE,.3f},{P_ENV2_D,.72f},{P_ENV2_S,.2f},{P_ENV2_R,.5f}),
    PRE("Keys Soft",{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.3f},{P_OSC2_LEVEL,.35f},{P_CUTOFF,.58f},{P_RESONANCE,.1f},
        {P_FLT_FM1,.4f},{P_ENV1_D,.55f},{P_ENV1_S,.1f},{P_ENV2_D,.7f},{P_ENV2_S,.3f},{P_ENV2_R,.4f}),
    PRE("Keys Cold Clav",{P_FILTER_MODE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.35f},{P_CUTOFF,0.5f},{P_RESONANCE,0.5f},{P_FLT_FM1,0.6f},
        {P_ENV1_D,0.4f},{P_ENV2_D,0.55f},{P_ENV2_S,0.1f},MX(1,S_VEL,D_CUT,0.5f)),
    PRE("Keys Static Organ",{P_OSC1_WAVE,7},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,0.5f},{P_CUTOFF,0.6f},{P_RESONANCE,0.15f},
        {P_DRIVE,0.5f},{P_FLT_FM1,0},{P_LFO2_RATE,0.6f},MX(1,S_LF2,D_SH1,0.4f)),
    PRE("Keys Mourn Wurly",{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_FINE,4},{P_OSC2_LEVEL,0.3f},{P_CUTOFF,0.5f},{P_RESONANCE,0.25f},
        {P_DRIVE,0.6f},{P_ENV2_D,0.6f},{P_ENV2_S,0.3f},{P_ENV2_R,0.3f},{P_FLT_FM1,0.3f},{P_VIB_DEPTH,0.1f},
        MX(1,S_VEL,D_DRV,0.5f),MX(2,S_WHL,D_LV2,0.5f)),
    PRE("Keys Stick Grind*",{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.5f},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},
        {P_DRIVE,0.6f},{P_ENV2_D,0.55f},{P_ENV2_S,0.4f},{P_STICK_X_TO,2},{P_STICK_Y_TO,3},MX(1,S_SY,D_INP,0.6f),MX(2,S_SX,D_XMD,0.3f)),
    PRE("Keys Warm EP",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_FINE,3},{P_OSC2_LEVEL,.35f},{P_OSC1_FM1,.25f},{P_CUTOFF,.6f},{P_RESONANCE,.1f},
        {P_FLT_FM1,.2f},{P_ENV1_D,.6f},{P_ENV1_S,.2f},{P_ENV2_D,.8f},{P_ENV2_S,.4f},{P_ENV2_R,.4f},{P_VEL_AMP,.5f},{P_VEL_CUTOFF,.4f},{P_LFO_RATE,.3f},MX(1,S_LFO,D_AMP,.2f)),
    PRE("Keys Clean Organ",{P_OSC1_WAVE,6},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.5f},{P_OSC2_COARSE,12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.75f},{P_RESONANCE,.1f},
        {P_FLT_FM1,0},{P_ENV2_R,.15f},{P_KEY_TRACK,.8f},{P_VIB_DEPTH,.15f}),
    /* ---- Stab ---- */
    PRE("Stab Hoover Dark",{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,22},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.5f},{P_RESONANCE,0.3f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},{P_FLT_FM1,0.5f},{P_ENV1_D,0.5f},{P_ENV2_D,0.6f},{P_ENV2_S,0.5f},{P_ENV2_R,0.3f},
        {P_GLIDE,0.2f},{P_VIB_DEPTH,0.3f},MX(1,S_E1,D_PCH,0.12f)),
    PRE("Stab Industrial",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,5},{P_OSC2_FINE,10},{P_XMOD,0.5f},{P_CUTOFF,0.55f},
        {P_RESONANCE,0.35f},{P_FILTER_DRIVE,1},{P_DRIVE,0.9f},{P_FLT_FM1,0.7f},{P_ENV1_D,0.45f},{P_ENV2_D,0.5f},{P_ENV2_S,0.2f},{P_ENV2_R,0.2f}),
    PRE("Stab Fifth Slam",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_CUTOFF,0.5f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0.7f},{P_ENV2_D,0.5f},{P_ENV2_S,0.3f},{P_ENV2_R,0.25f}),
    PRE("Stab Brass Scrape",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_FILTER_MODE,4},{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_NOISE_LEVEL,0.12f},{P_CUTOFF,0.45f},{P_RESONANCE,0.5f},
        {P_FLT_FM1,0.8f},{P_ENV1_A,0.3f},{P_ENV1_D,0.5f},{P_ENV2_A,0.25f},{P_ENV2_D,0.55f},{P_ENV2_S,0.3f},{P_ENV2_R,0.25f}),
    PRE("Stab Pulse Choke",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.2f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.6f},{P_OSC2_FINE,8},{P_CUTOFF,0.5f},
        {P_RESONANCE,0.45f},{P_FLT_FM1,0.8f},{P_ENV1_D,0.35f},{P_ENV2_D,0.45f},{P_ENV2_S,0.08f},{P_ENV2_R,0.2f},{P_DRIVE,0.6f}),
    PRE("Stab Stick Chord*",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},{P_FLT_FM1,0.6f},{P_ENV2_D,0.55f},{P_ENV2_S,0.4f},{P_BEND_RANGE,5},
        MX(1,S_SY,D_CUT,0.5f),MX(2,S_SX,D_PC2,0.2f)),
    PRE("Stab Metal Ring",{P_FILTER_DRIVE,0.85f},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,4},{P_CUTOFF,0.6f},{P_RESONANCE,0.3f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0.5f},{P_ENV1_D,0.45f},{P_ENV2_D,0.5f},{P_ENV2_S,0.1f},{P_ENV2_R,0.3f},MX(1,S_OS2,D_FM1,0.35f),MX(2,S_E1,D_FM1,0.2f)),
    PRE("Stab Bright Chord",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,6},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.8f},{P_RESONANCE,.2f},{P_DRIVE,.3f},{P_FLT_FM1,.5f},
        {P_ENV1_D,.35f},{P_ENV2_D,.4f},{P_ENV2_S,.12f},{P_ENV2_R,.25f}),
    /* ---- Brass ---- */
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
        {P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_FLT_FM1,0.7f},{P_ENV1_A,0.48f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.38f},
        {P_ENV2_S,0.9f},{P_ENV2_R,0.35f},{P_OCTAVE,-1}),
    PRE("Brass Mean Section",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.4f},
        {P_RESONANCE,0.2f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.5f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},
        {P_ENV2_A,0.35f},{P_ENV2_S,0.9f},{P_ENV2_R,0.3f},{P_VIB_DEPTH,0.1f},{P_VIB_RATE,0.55f}),
    PRE("Brass Reedy",{P_FILTER_MODE,4},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.35f},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_OSC2_LEVEL,0.6f},
        {P_CUTOFF,0.5f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.75f},{P_DRIVE,0.5f},{P_FLT_FM1,0.45f},{P_ENV1_A,0.42f},
        {P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_ENV2_R,0.28f}),
    PRE("Brass Slow Swell",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_CUTOFF,0.34f},{P_RESONANCE,0.2f},
        {P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},{P_FLT_FM1,0.85f},{P_ENV1_A,0.72f},{P_ENV1_D,0.75f},{P_ENV1_S,0.7f},
        {P_ENV2_A,0.62f},{P_ENV2_S,1},{P_ENV2_R,0.5f},{P_VIB_DEPTH,0.15f}),
    PRE("Brass Sync Horn",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,3},{P_XMOD,0.5f},{P_CUTOFF,0.46f},
        {P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.4f},
        {P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_ENV2_R,0.3f},{P_GLIDE,0.1f},{P_LEGATO,1}),
    PRE("Brass Velocity Bite",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.36f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.55f},{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_FLT_FM1,0.5f},{P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_VEL_AMP,0.4f},{P_M1_SRC,4},{P_M1_DST,8},{P_M1_AMT,0.5f},{P_M2_SRC,4},{P_M2_DST,11},{P_M2_AMT,0.4f}),
    PRE("Brass Stick Swell*",{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.38f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},
        {P_FLT_FM1,0.8f},{P_ENV1_A,0.55f},{P_ENV1_D,0.7f},{P_ENV1_S,0.6f},{P_ENV2_A,0.45f},{P_ENV2_S,1},{P_VIB_DEPTH,0.15f},{P_STICK_Y_TO,2},
        MX(1,S_SY,D_DRV,0.4f),MX(2,S_AT,D_CUT,0.4f)),
    PRE("Brass Mean Horn",{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.4f},{P_OSC2_FINE,8},{P_CUTOFF,0.42f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.9f},
        {P_DRIVE,0.7f},{P_FLT_FM1,0.6f},{P_ENV1_A,0.45f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.35f},{P_ENV2_S,0.9f},{P_LFO_RATE,0.62f},
        MX(1,S_VEL,D_INP,0.4f),MX(2,S_LFO,D_PCH,0.015f)),
    PRE("Brass Fall Apart",{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_CUTOFF,0.4f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0.6f},{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_LFO2_RATE,0.45f},{P_LFO2_WAVE,4},
        MX(1,S_LF2,D_PC2,0.04f),MX(2,S_RND,D_CUT,0.2f)),
    PRE("Brass Wheel Swell",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.4f},{P_RESONANCE,0.25f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.55f},
        {P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.5f},{P_FLT_FM1,0.4f},{P_ENV2_A,0.3f},{P_ENV2_S,0.9f},{P_LFO_RATE,0.35f},
        MX(1,S_WHL,D_LD1,1.0f),MX(2,S_LFO,D_CUT,0.35f),MX(3,S_WHL,D_VIB,0.5f)),
    PRE("Brass Clean Swell",{P_OSC2_WAVE,1},{P_OSC2_FINE,7},{P_CUTOFF,.55f},{P_RESONANCE,.15f},{P_DRIVE,.2f},{P_FLT_FM1,.6f},{P_ENV1_A,.55f},{P_ENV1_D,.7f},{P_ENV1_S,.6f},
        {P_ENV2_A,.35f},{P_ENV2_S,.9f},{P_ENV2_R,.35f},{P_AT_DEST,1},{P_AT_AMOUNT,.6f}),
    /* ---- Arp ---- */
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
    PRE("Arp Matrix Run",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,0.55f},{P_OSC2_LEVEL,0},{P_CUTOFF,0.35f},{P_RESONANCE,0.5f},
        {P_FILTER_DRIVE,0.8f},{P_FLT_FM1,0.7f},{P_ENV1_D,0.5f},{P_ENV2_D,0.55f},{P_ENV2_S,0.6f},{P_ENV2_R,0.2f},{P_LFO_SYNC,2},
        MX(1,S_RND,D_CUT,0.35f),MX(2,S_LFO,D_RES,0.2f)),
    PRE("Arp Stick Acid*",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,0.5f},{P_OSC2_LEVEL,0},{P_CUTOFF,0.3f},{P_RESONANCE,0.75f},
        {P_FILTER_DRIVE,0.85f},{P_DRIVE,0.5f},{P_FLT_FM1,1},{P_ENV1_D,0.5f},{P_ENV2_D,0.55f},{P_ENV2_S,0.6f},{P_ENV2_R,0.2f},
        {P_STICK_X_TO,2},{P_STICK_Y_TO,3},MX(1,S_SY,D_RES,0.2f),MX(2,S_VEL,D_CUT,0.3f)),
    PRE("Arp Dark Pendulum",{P_ARP_MODE,3},{P_ARP_RATE,2},{P_ARP_OCT,2},{P_ARP_GATE,0.7f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},
        {P_CUTOFF,0.35f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_D,0.5f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f},
        MX(1,S_KEY,D_CUT,0.4f)),
    PRE("Arp Random Sync",{P_ARP_MODE,4},{P_ARP_RATE,4},{P_ARP_OCT,3},{P_ARP_GATE,0.5f},{P_OSC2_WAVE,4},{P_OSC2_FINE,8},{P_CUTOFF,0.4f},
        {P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},{P_FLT_FM1,0.3f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f},{P_LFO_WAVE,3},{P_LFO_SYNC,2},
        MX(1,S_LFO,D_CUT,0.3f),MX(2,S_RND,D_DRV,0.3f)),
    PRE("Arp Iron Cascade",{P_ARP_MODE,2},{P_ARP_RATE,3},{P_ARP_OCT,3},{P_ARP_GATE,0.5f},{P_OSC1_WAVE,5},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},
        {P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0.6f},{P_ENV1_D,0.5f},{P_ENV2_S,0.8f},{P_ENV2_R,0.2f},{P_LFO_SYNC,1},MX(1,S_LFO,D_SH1,0.5f)),
    PRE("Arp Chainsaw",{P_ARP_MODE,1},{P_ARP_RATE,5},{P_ARP_OCT,1},{P_ARP_GATE,0.6f},{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_FINE,9},
        {P_CUTOFF,0.5f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,1},{P_DRIVE,1},{P_FLT_FM1,0.4f},{P_ENV2_S,0.8f},{P_ENV2_R,0.15f},{P_LFO2_SYNC,3},
        MX(1,S_LF2,D_CUT,0.4f)),
    PRE("Arp Step Climb",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,0.6f},{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_FINE,7},{P_CUTOFF,0.3f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},{P_FLT_FM1,0.1f},MX(1,S_ARP,D_CUT,0.6f),MX(2,S_ARP,D_GRT,0.5f)),
    PRE("Arp Bright Sixteenth",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.3f},{P_OSC2_FINE,7},{P_OSC2_LEVEL,.6f},
        {P_CUTOFF,.8f},{P_RESONANCE,.3f},{P_FLT_FM1,.4f},{P_ENV1_D,.4f},{P_ENV2_D,.45f},{P_ENV2_S,.2f},{P_ENV2_R,.2f}),
    PRE("Arp Latch Pulse",{P_ARP_MODE,3},{P_ARP_RATE,1},{P_ARP_OCT,2},{P_ARP_GATE,.7f},{P_HOLD,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.4f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},
        {P_OSC2_FINE,6},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.55f},{P_RESONANCE,.35f},{P_FLT_FM1,.5f},{P_ENV1_D,.5f},{P_ENV2_S,.7f},{P_ENV2_R,.2f}),
    PRE("Arp Triplet Climb",{P_ARP_MODE,1},{P_ARP_RATE,2},{P_ARP_OCT,3},{P_ARP_GATE,.6f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_LEVEL,.5f},
        {P_CUTOFF,.7f},{P_RESONANCE,.3f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV2_D,.5f},{P_ENV2_S,.4f},{P_ENV2_R,.25f}),
    /* ---- Seq ---- */
    PRE("Seq Gate Chop",{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},{P_FLT_FM1,0},
        {P_LFO_WAVE,1},{P_LFO_SYNC,5},MX(1,S_LFO,D_AMP,0.9f)),
    PRE("Seq Pump Bass",{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.4f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.85f},
        {P_DRIVE,0.7f},{P_FLT_FM1,0},{P_LFO_WAVE,2},{P_LFO_SYNC,2},MX(1,S_LFO,D_AMP,0.8f)),
    PRE("Seq Wobble Eighths",{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_CUTOFF,0.36f},{P_RESONANCE,0.55f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},
        {P_FLT_FM1,0},{P_LFO_SYNC,3},MX(1,S_LFO,D_CUT,0.5f)),
    PRE("Seq Triplet Drag",{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0},{P_LFO_WAVE,1},{P_LFO_SYNC,4},{P_LFO2_SYNC,1},MX(1,S_LFO,D_AMP,0.8f),MX(2,S_LF2,D_CUT,0.3f)),
    PRE("Seq Broken Gate",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_CUTOFF,0.45f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.85f},
        {P_DRIVE,0.6f},{P_FLT_FM1,0},{P_LFO_WAVE,4},{P_LFO_SYNC,5},MX(1,S_LFO,D_AMP,0.8f),MX(2,S_LFO,D_CUT,0.4f)),
    PRE("Seq Dread Sweep",{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_CUTOFF,0.35f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},
        {P_FLT_FM1,0},{P_LFO_WAVE,3},{P_LFO_SYNC,1},MX(1,S_LFO,D_CUT,0.5f)),
    PRE("Seq Stutter Grit*",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},{P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.9f},
        {P_DRIVE,0.7f},{P_FLT_FM1,0},{P_LFO_WAVE,1},{P_LFO_SYNC,6},{P_STICK_Y_TO,3},MX(1,S_LFO,D_AMP,0.85f),MX(2,S_SY,D_DRV,0.5f)),
    PRE("Seq Machine Pulse",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,0.6f},{P_OSC2_FINE,8},{P_CUTOFF,0.45f},
        {P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},{P_FLT_FM1,0},{P_LFO_WAVE,2},{P_LFO_SYNC,5},MX(1,S_LFO,D_AMP,0.7f),MX(2,S_LFO,D_CUT,0.3f)),
    PRE("Seq Dark Tremolo",{P_FILTER_DRIVE,0.8f},{P_OSC2_WAVE,1},{P_OSC2_FINE,13},{P_CUTOFF,0.45f},{P_RESONANCE,0.3f},{P_DRIVE,0.6f},{P_FLT_FM1,0},
        {P_LFO_SYNC,3},{P_LFO2_SYNC,1},MX(1,S_LFO,D_AMP,0.5f),MX(2,S_LF2,D_CUT,0.3f)),
    PRE("Seq Rotor",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.4f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0},{P_LFO_WAVE,3},{P_LFO_SYNC,5},{P_LFO2_SYNC,7},MX(1,S_LFO,D_CUT,0.4f),MX(2,S_LF2,D_DRV,0.25f)),
    PRE("Seq Bright Pulse",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.4f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.6f},{P_OSC2_FINE,6},{P_CUTOFF,.8f},{P_RESONANCE,.2f},{P_FLT_FM1,0},
        {P_LFO_WAVE,1},{P_LFO_SYNC,5},MX(1,S_LFO,D_AMP,.8f)),
    /* ---- Pad ---- */
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
    PRE("Pad Random Walk",{P_OSC2_WAVE,1},{P_OSC2_FINE,12},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},{P_ENV2_A,0.7f},{P_ENV2_R,0.8f},{P_FLT_FM1,0.2f},{P_LFO_RATE,0.2f},{P_M1_SRC,10},{P_M1_DST,8},{P_M1_AMT,0.35f},{P_M2_SRC,1},{P_M2_DST,3},{P_M2_AMT,0.5f},{P_M3_SRC,1},{P_M3_DST,2},{P_M3_AMT,0.02f}),
    PRE("Pad Stick Fog*",{P_OSC2_WAVE,1},{P_OSC2_FINE,13},{P_CUTOFF,0.45f},{P_RESONANCE,0.3f},{P_DRIVE,0.5f},{P_ENV2_A,0.7f},{P_ENV2_R,0.8f},
        {P_FLT_FM1,0.2f},{P_ENV1_A,0.6f},{P_STICK_X_TO,2},{P_STICK_Y_TO,3},MX(1,S_SY,D_CUT,0.5f),MX(2,S_SX,D_RES,0.3f)),
    PRE("Pad Cold Machine",{P_OSC1_WAVE,4},{P_OSC2_WAVE,4},{P_OSC2_FINE,10},{P_CUTOFF,0.5f},{P_RESONANCE,0.35f},{P_ENV2_A,0.65f},{P_ENV2_R,0.75f},
        {P_FLT_FM1,0.1f},{P_LFO2_SYNC,1},MX(1,S_LF2,D_CUT,0.3f)),
    PRE("Pad Ash Choir",{P_FILTER_MODE,4},{P_OSC2_WAVE,1},{P_OSC2_FINE,12},{P_CUTOFF,0.5f},{P_RESONANCE,0.55f},{P_ENV2_A,0.6f},{P_ENV2_R,0.75f},
        {P_FLT_FM1,0.1f},MX(1,S_RND,D_CUT,0.25f)),
    PRE("Pad Bunker",{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,6},{P_CUTOFF,0.35f},{P_RESONANCE,0.3f},{P_DRIVE,0.6f},
        {P_FILTER_DRIVE,0.8f},{P_ENV2_A,0.65f},{P_ENV2_R,0.8f},{P_FLT_FM1,0.1f},{P_LFO_RATE,0.2f},MX(1,S_LFO,D_DRV,0.3f)),
    PRE("Pad Corrosion",{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,10},{P_NOISE_LEVEL,0.12f},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},{P_DRIVE,0.7f},
        {P_FILTER_DRIVE,0.85f},{P_ENV2_A,0.6f},{P_ENV2_R,0.75f},{P_FLT_FM1,0.1f},{P_LFO_RATE,0.3f},{P_LFO2_RATE,0.22f},
        MX(1,S_LFO,D_SH1,0.5f),MX(2,S_LF2,D_PC2,0.03f)),
    PRE("Pad Wire Swarm",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_FINE,20},{P_XMOD,0.3f},{P_CUTOFF,0.55f},{P_RESONANCE,0.35f},{P_DRIVE,0.6f},
        {P_ENV2_A,0.5f},{P_ENV2_R,0.7f},{P_FLT_FM1,0.1f},{P_LFO2_RATE,0.4f},{P_LFO2_WAVE,4},MX(1,S_LF2,D_PCH,0.02f)),
    PRE("Pad Age Swell",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,0.8f},{P_CUTOFF,0.38f},{P_RESONANCE,0.35f},{P_FILTER_DRIVE,0.7f},
        {P_DRIVE,0.4f},{P_FLT_FM1,0},{P_ENV2_A,0.65f},{P_ENV2_R,0.8f},{P_LFO_RATE,0.3f},MX(1,S_AGE,D_CUT,0.55f),MX(2,S_AGE,D_LD1,1.0f),MX(3,S_LFO,D_CUT,0.25f)),
    PRE("Pad Wander",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,13},{P_OSC2_LEVEL,0.7f},{P_CUTOFF,0.42f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.5f},
        {P_FLT_FM1,0},{P_ENV2_A,0.6f},{P_ENV2_R,0.8f},{P_DRIFT,0.4f},MX(1,S_SMR,D_CUT,0.3f),MX(2,S_SMR,D_SH2,0.4f),MX(3,S_SMR,D_DFT,0.6f)),
    PRE("Pad Open Strings",{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.72f},{P_RESONANCE,.1f},{P_FLT_FM1,0},{P_ENV2_A,.55f},{P_ENV2_R,.7f},
        {P_DRIFT,.5f},{P_VIB_DEPTH,.08f}),
    PRE("Pad PWM Choir",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.5f},{P_OSC2_FINE,8},{P_OSC2_LEVEL,.8f},{P_SHAPE_MOD,.6f},{P_LFO_RATE,.2f},
        {P_CUTOFF,.6f},{P_RESONANCE,.2f},{P_FLT_FM1,0},{P_ENV2_A,.55f},{P_ENV2_R,.75f}),
    /* ---- Texture ---- */
    PRE("Texture Static Bed",{P_FILTER_MODE,1},{P_OSC1_LEVEL,0.1f},{P_NOISE_LEVEL,0.8f},{P_CUTOFF,0.6f},{P_RESONANCE,0.6f},{P_FLT_FM1,0},
        {P_ENV2_A,0.5f},{P_ENV2_R,0.8f},{P_LFO2_RATE,0.3f},{P_LFO2_WAVE,4},MX(1,S_LF2,D_CUT,0.3f)),
    PRE("Texture Rust Bed",{P_OSC2_WAVE,1},{P_OSC2_FINE,14},{P_NOISE_LEVEL,0.2f},{P_CUTOFF,0.42f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.9f},
        {P_DRIVE,0.8f},{P_FLT_FM1,0},{P_ENV2_A,0.6f},{P_ENV2_R,0.8f},{P_LFO_RATE,0.25f},MX(1,S_LFO,D_CUT,0.3f)),
    PRE("Texture Machine Hum",{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_OSC2_FINE,3},{P_CUTOFF,0.4f},{P_RESONANCE,0.3f},
        {P_DRIVE,0.6f},{P_FLT_FM1,0},{P_LFO_WAVE,1},{P_LFO_SYNC,2},MX(1,S_LFO,D_AMP,0.15f)),
    PRE("Texture Wire Rain",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_OSC2_WAVE,4},{P_OSC2_COARSE,12},{P_OSC2_FINE,9},{P_CUTOFF,0.55f},{P_RESONANCE,0.7f},
        {P_FLT_FM1,0},{P_LFO_WAVE,4},{P_LFO_SYNC,5},MX(1,S_LFO,D_CUT,0.5f)),
    PRE("Texture Dead Radio",{P_FILTER_MODE,1},{P_OSC1_LEVEL,0.3f},{P_NOISE_LEVEL,0.5f},{P_CUTOFF,0.55f},{P_RESONANCE,0.75f},{P_FLT_FM1,0},
        {P_LFO2_RATE,0.35f},{P_LFO2_WAVE,4},MX(1,S_LF2,D_CUT,0.4f)),
    PRE("Texture Slow Churn",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,17},{P_XMOD,0.7f},{P_CUTOFF,0.5f},{P_RESONANCE,0.3f},{P_DRIVE,0.6f},
        {P_FLT_FM1,0},{P_LFO_RATE,0.15f},MX(1,S_LFO,D_XMD,0.4f)),
    PRE("Texture Smelter",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_FINE,18},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,1},{P_DRIVE,1},
        {P_FLT_FM1,0},{P_LFO_RATE,0.25f},MX(1,S_LFO,D_INP,0.5f)),
    PRE("Texture Under Ice",{P_FILTER_MODE,2},{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,12},{P_OSC2_FINE,6},{P_CUTOFF,0.5f},{P_RESONANCE,0.5f},
        {P_FLT_FM1,0},{P_ENV2_A,0.6f},{P_ENV2_R,0.8f},{P_LFO_RATE,0.2f},MX(1,S_LFO,D_CUT,0.3f)),
    PRE("Texture Grinder",{P_OSC1_WAVE,5},{P_NOISE_LEVEL,0.3f},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.9f},{P_FLT_FM1,0},
        {P_LFO_WAVE,2},{P_LFO_SYNC,3},MX(1,S_LFO,D_CUT,0.5f)),
    PRE("Texture Dark Wake",{P_OSC2_WAVE,1},{P_OSC2_FINE,16},{P_CUTOFF,0.4f},{P_RESONANCE,0.35f},{P_DRIVE,0.6f},{P_ENV2_A,0.7f},{P_ENV2_R,0.85f},
        {P_FLT_FM1,0.1f},{P_LFO_RATE,0.2f},{P_LFO2_RATE,0.12f},MX(1,S_LF2,D_PC2,0.015f),MX(2,S_LFO,D_CUT,0.25f)),
    PRE("Texture Shortwave",{P_OSC2_WAVE,1},{P_OSC2_FINE,14},{P_FILTER_MODE,4},{P_CUTOFF,0.5f},{P_RESONANCE,0.55f},{P_DRIVE,0.7f},{P_FLT_FM1,0},
        {P_ENV2_A,0.5f},{P_ENV2_R,0.8f},{P_LFO_RATE,0.22f},MX(1,S_NSE,D_FM2,0.12f),MX(2,S_LFO,D_CUT,0.35f)),
    PRE("Texture Cross FM",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,7},{P_OSC2_FINE,5},{P_CUTOFF,0.6f},{P_RESONANCE,0.3f},{P_DRIVE,0.5f},{P_FLT_FM1,0},
        {P_ENV2_S,0.9f},MX(1,S_OS1,D_FM2,0.3f),MX(2,S_OS2,D_FM1,0.25f),MX(3,S_SMR,D_FM1,0.1f)),
    PRE("Texture Bright Shimmer",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_FINE,11},{P_OSC2_LEVEL,.7f},{P_NOISE_LEVEL,.1f},{P_CUTOFF,.9f},{P_RESONANCE,.3f},
        {P_FLT_FM1,0},{P_ENV2_A,.6f},{P_ENV2_R,.8f},{P_LFO2_RATE,.2f},{P_LFO2_WAVE,4},{P_DRIFT,.6f},MX(1,S_LF2,D_CUT,.2f),MX(2,S_SMR,D_FM1,.15f)),
    /* ---- Vox ---- */
    PRE("Vox Dead Choir*",{P_FILTER_MODE,4},{P_OSC2_WAVE,1},{P_OSC2_FINE,12},{P_CUTOFF,0.5f},{P_RESONANCE,0.75f},{P_ENV2_A,0.5f},{P_ENV2_R,0.6f},
        {P_FLT_FM1,0},{P_STICK_Y_TO,2},MX(1,S_SY,D_CUT,0.35f)),
    PRE("Vox Throat*",{P_FILTER_MODE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.4f},{P_CUTOFF,0.45f},{P_RESONANCE,0.8f},{P_FLT_FM1,0},{P_LFO_RATE,0.55f},
        MX(1,S_LFO,D_CUT,0.3f),MX(2,S_SY,D_RES,0.1f)),
    PRE("Vox Machine Speak",{P_FILTER_MODE,4},{P_CUTOFF,0.5f},{P_RESONANCE,0.7f},{P_FLT_FM1,0},{P_LFO_WAVE,4},{P_LFO_SYNC,5},MX(1,S_LFO,D_CUT,0.35f)),
    PRE("Vox Hollow Moan",{P_FILTER_MODE,4},{P_OSC1_WAVE,2},{P_OSC2_WAVE,4},{P_OSC2_FINE,6},{P_CUTOFF,0.45f},{P_RESONANCE,0.65f},{P_GLIDE,0.2f},
        {P_LEGATO,1},{P_VIB_DEPTH,0.25f},{P_ENV2_A,0.4f},{P_FLT_FM1,0}),
    PRE("Vox Grit Formant",{P_FILTER_MODE,3},{P_OSC1_WAVE,7},{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},{P_RESONANCE,0.6f},{P_DRIVE,0.8f},
        {P_FILTER_DRIVE,0.9f},{P_FLT_FM1,0},MX(1,S_E1,D_CUT,0.4f),{P_ENV1_A,0.4f},{P_ENV1_D,0.6f},{P_ENV1_S,0.3f}),
    PRE("Vox Stick Vowel*",{P_FILTER_MODE,4},{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,0.5f},{P_RESONANCE,0.75f},{P_FLT_FM1,0},{P_STICK_X_TO,2},
        {P_STICK_Y_TO,3},MX(1,S_SY,D_RES,0.2f)),
    PRE("Vox Open Ahh",{P_FILTER_MODE,4},{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_CUTOFF,.6f},{P_RESONANCE,.6f},{P_FLT_FM1,0},{P_GLIDE,.1f},{P_LEGATO,1},
        {P_VIB_DEPTH,.2f},{P_ENV2_A,.25f},{P_ENV2_R,.4f}),
    /* ---- Drone ---- */
    PRE("Drone Chaos",{P_DRIVE,0.7f},{P_FILTER_DRIVE,0.9f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,5},{P_OSC2_COARSE,7},{P_OSC2_FINE,23},{P_XMOD,.8f},{P_OSC1_FM2,.4f},
        {P_OSC2_FM2,.3f},{P_LFO_RATE,.7f},{P_SHAPE_MOD,.8f},{P_CUTOFF,.6f},{P_RESONANCE,.55f},{P_FLT_FM2,.5f},{P_FLT_FM1,0},
        {P_DRONE,1}),
    PRE("Drone Growl",{P_DRONE,1},{P_OSC2_WAVE,1},{P_OSC2_FINE,25},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.45f},{P_RESONANCE,.55f},
        {P_FLT_FM2,.5f},{P_LFO_RATE,.2f},{P_DRIVE,.5f}),
    PRE("Drone Metal",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_DRONE,1},{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,5},{P_OSC2_FINE,18},{P_XMOD,.6f},
        {P_OSC1_FM2,.2f},{P_LFO_RATE,.25f},{P_CUTOFF,.58f},{P_RESONANCE,.5f},{P_FLT_FM2,.4f},{P_FLT_FM1,0}),
    PRE("Drone Sub",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.7f},{P_DRONE,1},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_OSC2_FINE,4},{P_OSC2_LEVEL,.7f},
        {P_CUTOFF,.42f},{P_RESONANCE,.35f},{P_FLT_FM2,.2f},{P_LFO_RATE,.15f},{P_FLT_FM1,0}),
    PRE("Drone Wind",{P_OSC1_LEVEL,0},{P_NOISE_LEVEL,.9f},{P_FILTER_MODE,1},{P_RESONANCE,.8f},{P_CUTOFF,.55f},{P_FLT_FM2,.6f},
        {P_LFO_RATE,.25f},{P_DRONE,1}),
    PRE("Drone Stick Mass*",{P_DRONE,1},{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_FINE,16},{P_XMOD,0.5f},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},
        {P_DRIVE,0.7f},{P_FLT_FM1,0},MX(1,S_SX,D_XMD,0.5f),MX(2,S_SY,D_INP,0.5f)),
    PRE("Drone Iron Lung",{P_DRIVE,0.6f},{P_FILTER_DRIVE,0.85f},{P_DRONE,1},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-12},{P_NOISE_LEVEL,0.1f},{P_CUTOFF,0.45f},{P_RESONANCE,0.5f},{P_FLT_FM1,0},
        {P_LFO_SYNC,1},MX(1,S_LFO,D_CUT,0.35f)),
    PRE("Drone Black Mass",{P_DRONE,2},{P_OSC1_WAVE,2},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,5},{P_CUTOFF,0.38f},{P_RESONANCE,0.35f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},{P_FLT_FM1,0},{P_LFO2_RATE,0.18f},MX(1,S_LF2,D_INP,0.3f)),
    PRE("Drone Machine Room",{P_DRONE,1},{P_OSC1_WAVE,4},{P_OSC2_WAVE,4},{P_OSC2_FINE,9},{P_NOISE_LEVEL,0.3f},{P_FILTER_MODE,4},{P_CUTOFF,0.5f},
        {P_RESONANCE,0.6f},{P_FLT_FM1,0},{P_LFO_WAVE,4},{P_LFO_RATE,0.3f},MX(1,S_LFO,D_CUT,0.3f)),
    PRE("Drone Fifth Decay",{P_FILTER_DRIVE,0.85f},{P_DRONE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,6},{P_CUTOFF,0.42f},{P_RESONANCE,0.4f},{P_DRIVE,0.7f},
        {P_FLT_FM1,0},{P_LFO2_RATE,0.2f},MX(1,S_LF2,D_PC2,0.01f)),
    PRE("Drone Furnace Hum",{P_DRONE,1},{P_OSC1_WAVE,5},{P_OSC2_WAVE,7},{P_OSC2_COARSE,-12},{P_CUTOFF,0.4f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,1},
        {P_DRIVE,1},{P_FLT_FM1,0},{P_LFO_RATE,0.2f},MX(1,S_LFO,D_AMP,0.1f)),
    /* ---- FX ---- */
    PRE("FX Siren Fall",{P_FILTER_DRIVE,0.8f},{P_OSC2_WAVE,4},{P_OSC2_FINE,8},{P_CUTOFF,0.6f},{P_RESONANCE,0.4f},{P_ENV1_D,0.8f},{P_ENV1_S,0},{P_FLT_FM1,0.3f},{P_ENV2_R,0.5f},
        {P_DRIVE,0.7f},MX(1,S_E1,D_PCH,1.0f)),
    PRE("FX Rise",{P_FILTER_DRIVE,0.8f},{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,0.45f},{P_RESONANCE,0.4f},{P_ENV1_A,0.75f},{P_ENV1_D,0.8f},{P_ENV1_S,1},{P_FLT_FM1,0.6f},
        {P_ENV2_A,0.6f},{P_ENV2_R,0.6f},{P_DRIVE,0.7f},MX(1,S_E1,D_PCH,0.8f)),
    PRE("FX Alarm",{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.5f},{P_CUTOFF,0.6f},{P_RESONANCE,0.4f},{P_FLT_FM1,0},{P_DRIVE,0.7f},{P_LFO_WAVE,1},{P_LFO_RATE,0.6f},
        MX(1,S_LFO,D_PCH,0.25f)),
    PRE("FX Klaxon",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,8},{P_CUTOFF,0.55f},{P_RESONANCE,0.3f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},
        {P_FLT_FM1,0},{P_LFO_RATE,0.4f},MX(1,S_LFO,D_PCH,0.6f)),
    PRE("FX Machinery",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,0.3f},{P_NOISE_LEVEL,0.3f},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},{P_FLT_FM1,0},{P_DRIVE,0.7f},
        {P_LFO_WAVE,1},{P_LFO_SYNC,5},MX(1,S_LFO,D_AMP,0.6f)),
    PRE("FX Steam Vent",{P_FILTER_MODE,1},{P_OSC1_LEVEL,0},{P_NOISE_LEVEL,0.9f},{P_CUTOFF,0.65f},{P_RESONANCE,0.5f},{P_FLT_FM1,0},{P_ENV2_A,0.3f},
        {P_ENV2_R,0.6f},{P_LFO2_RATE,0.3f},MX(1,S_LF2,D_CUT,0.3f)),
    PRE("FX Radio Sweep",{P_FILTER_MODE,1},{P_OSC1_WAVE,2},{P_OSC1_LEVEL,0.4f},{P_NOISE_LEVEL,0.6f},{P_CUTOFF,0.5f},{P_RESONANCE,0.8f},{P_FLT_FM1,0},
        {P_LFO_RATE,0.25f},MX(1,S_LFO,D_CUT,0.8f)),
    PRE("FX Reactor Core",{P_OSC1_WAVE,7},{P_OSC2_WAVE,7},{P_OSC2_COARSE,7},{P_OSC2_FINE,12},{P_XMOD,0.8f},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},
        {P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},{P_FLT_FM1,0},{P_LFO_RATE,0.5f},{P_LFO2_RATE,0.3f},MX(1,S_LFO,D_XMD,0.4f),MX(2,S_LF2,D_INP,0.4f)),
    PRE("FX Glitch Seed",{P_DRIVE,0.5f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,9},{P_CUTOFF,0.5f},{P_RESONANCE,0.5f},{P_FILTER_DRIVE,0.85f},{P_FLT_FM1,0},
        {P_LFO_WAVE,4},{P_LFO_RATE,0.85f},{P_LFO2_WAVE,4},{P_LFO2_RATE,0.7f},MX(1,S_LFO,D_PCH,0.3f),MX(2,S_LF2,D_CUT,0.4f)),
    PRE("FX Scrap Metal",{P_OSC2_WAVE,1},{P_OSC2_COARSE,13},{P_OSC2_FINE,31},{P_XMOD,1},{P_NOISE_LEVEL,0.2f},{P_CUTOFF,0.55f},{P_RESONANCE,0.3f},
        {P_FILTER_DRIVE,1},{P_DRIVE,1},{P_FLT_FM1,0.2f},{P_ENV1_D,0.6f},MX(1,S_E1,D_XMD,0.3f)),
    PRE("FX Dread Whoosh",{P_FILTER_MODE,1},{P_OSC1_LEVEL,0.2f},{P_NOISE_LEVEL,0.7f},{P_CUTOFF,0.4f},{P_RESONANCE,0.85f},{P_ENV1_A,0.6f},{P_ENV1_D,0.7f},
        {P_ENV1_S,0},{P_FLT_FM1,0.8f},{P_ENV2_A,0.5f},{P_ENV2_R,0.8f}),
    PRE("FX Stick Scratch*",{P_NOISE_LEVEL,0.15f},{P_CUTOFF,0.5f},{P_RESONANCE,0.45f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.7f},{P_FLT_FM1,0.2f},{P_BEND_RANGE,12},
        {P_STICK_Y_TO,2},MX(1,S_SY,D_CUT,0.6f),MX(2,S_SX,D_DRV,0.5f)),
    PRE("FX Static FM",{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,9},{P_CUTOFF,0.55f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.9f},{P_DRIVE,0.8f},
        {P_FLT_FM1,0},MX(1,S_NSE,D_FM1,0.5f),MX(2,S_NSE,D_FM2,0.3f)),
    PRE("FX Ring Drop",{P_FILTER_DRIVE,0.8f},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_FINE,18},{P_CUTOFF,0.6f},{P_RESONANCE,0.3f},{P_DRIVE,0.7f},
        {P_ENV1_D,0.8f},{P_ENV1_S,0},{P_FLT_FM1,0.2f},{P_ENV2_R,0.5f},MX(1,S_OS2,D_PCH,0.08f),MX(2,S_E1,D_PCH,0.6f)),
    PRE("FX Pedal Collapse",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,0.5f},{P_RESONANCE,0.4f},{P_FILTER_DRIVE,0.8f},{P_DRIVE,0.6f},
        {P_ENV2_S,0.9f},MX(1,S_PED,D_GRT,1.0f),MX(2,S_PED,D_PCH,-0.2f)),
    /* ---- Horror ---- */
    PRE("Horror Calliope",{P_ARP_MODE,1},{P_ARP_RATE,5},{P_ARP_OCT,1},{P_ARP_GATE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,12},{P_OSC2_FINE,-38},{P_OSC2_LEVEL,.75f},{P_NOISE_LEVEL,.05f},
        {P_NOISE_COLOR,.2f},{P_CUTOFF,.55f},{P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_DRIVE,.12f},{P_FLT_FM1,.6f},{P_ENV1_A,0},{P_ENV1_D,.5f},{P_ENV1_S,0},{P_ENV1_R,.2f},
        {P_ENV2_A,0},{P_ENV2_D,.55f},{P_ENV2_S,.9f},{P_ENV2_R,.08f},{P_DRIFT,1},{P_VIB_DEPTH,0},{P_AT_DEST,2},{P_AT_AMOUNT,.6f},{P_KEY_TRACK,.8f},
        {P_LFO2_RATE,.4f},{P_LFO2_WAVE,4},MX(1,S_RND,D_PCH,.04f),MX(2,S_SMR,D_PCH,.03f),MX(3,S_LF2,D_PC2,.035f),MX(4,S_E1,D_NSE,.5f),
        {P_M5_SRC,S_SMR},{P_M5_DST,D_PC2},{P_M5_AMT,.2f},{P_M5_VIA,S_WHL},MX(6,S_AGE,D_PCH,-.02f)),
    PRE("Horror Calliope Solo",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,12},{P_OSC2_FINE,-38},{P_OSC2_LEVEL,.75f},{P_NOISE_LEVEL,.05f},
        {P_NOISE_COLOR,.2f},{P_CUTOFF,.55f},{P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_DRIVE,.12f},{P_FLT_FM1,.6f},{P_ENV1_A,0},{P_ENV1_D,.5f},{P_ENV1_S,0},{P_ENV1_R,.2f},
        {P_ENV2_A,0},{P_ENV2_D,.55f},{P_ENV2_S,.9f},{P_ENV2_R,.08f},{P_DRIFT,1},{P_VIB_DEPTH,0},{P_AT_DEST,2},{P_AT_AMOUNT,.6f},{P_KEY_TRACK,.8f},
        {P_LFO2_RATE,.4f},{P_LFO2_WAVE,4},MX(1,S_RND,D_PCH,.04f),MX(2,S_SMR,D_PCH,.03f),MX(3,S_LF2,D_PC2,.035f),MX(4,S_E1,D_NSE,.5f),
        {P_M5_SRC,S_SMR},{P_M5_DST,D_PC2},{P_M5_AMT,.2f},{P_M5_VIA,S_WHL},MX(6,S_AGE,D_PCH,-.02f)),
    PRE("Horror Calliope Fifths",{P_ARP_MODE,1},{P_ARP_RATE,5},{P_ARP_OCT,1},{P_ARP_GATE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,7},{P_OSC2_FINE,-30},{P_OSC2_LEVEL,.75f},{P_NOISE_LEVEL,.05f},
        {P_NOISE_COLOR,.2f},{P_CUTOFF,.55f},{P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_DRIVE,.12f},{P_FLT_FM1,.6f},{P_ENV1_A,0},{P_ENV1_D,.5f},{P_ENV1_S,0},{P_ENV1_R,.2f},
        {P_ENV2_A,0},{P_ENV2_D,.55f},{P_ENV2_S,.9f},{P_ENV2_R,.08f},{P_DRIFT,1},{P_VIB_DEPTH,0},{P_AT_DEST,2},{P_AT_AMOUNT,.6f},{P_KEY_TRACK,.8f},
        {P_LFO2_RATE,.4f},{P_LFO2_WAVE,4},MX(1,S_RND,D_PCH,.04f),MX(2,S_SMR,D_PCH,.03f),MX(3,S_LF2,D_PC2,.035f),MX(4,S_E1,D_NSE,.5f),
        {P_M5_SRC,S_SMR},{P_M5_DST,D_PC2},{P_M5_AMT,.2f},{P_M5_VIA,S_WHL},MX(6,S_AGE,D_PCH,-.02f)),
    PRE("Horror Carousel",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,.7f},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.7f},{P_OSC2_COARSE,12},{P_OSC2_FINE,-34},{P_OSC2_LEVEL,.75f},{P_NOISE_LEVEL,.05f},
        {P_NOISE_COLOR,.2f},{P_CUTOFF,.55f},{P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_DRIVE,.12f},{P_FLT_FM1,.6f},{P_ENV1_A,0},{P_ENV1_D,.45f},{P_ENV1_S,0},{P_ENV1_R,.2f},
        {P_ENV2_A,0},{P_ENV2_D,.55f},{P_ENV2_S,.9f},{P_ENV2_R,.08f},{P_DRIFT,1},{P_VIB_DEPTH,0},{P_AT_DEST,2},{P_AT_AMOUNT,.6f},{P_KEY_TRACK,.8f},
        {P_LFO2_RATE,.4f},{P_LFO2_WAVE,4},MX(1,S_RND,D_PCH,.04f),MX(2,S_SMR,D_PCH,.03f),MX(3,S_LF2,D_PC2,.035f),MX(4,S_E1,D_NSE,.5f),
        {P_M5_SRC,S_SMR},{P_M5_DST,D_PC2},{P_M5_AMT,.2f},{P_M5_VIA,S_WHL},MX(6,S_AGE,D_PCH,-.02f)),
    PRE("Horror Crypt Organ",{P_OSC1_WAVE,6},{P_OSC2_WAVE,1},{P_OSC2_COARSE,6},{P_OSC2_FINE,8},{P_OSC2_LEVEL,.5f},{P_NOISE_LEVEL,.08f},{P_CUTOFF,.55f},{P_RESONANCE,.45f},
        {P_FILTER_DRIVE,.8f},{P_DRIVE,.6f},{P_FLT_FM1,0},{P_ENV2_A,.3f},{P_ENV2_R,.6f},{P_VIB_DEPTH,.25f},{P_DRIFT,.8f},{P_KEY_TRACK,.8f},{P_LFO_WAVE,4},{P_LFO_RATE,.35f},
        MX(1,S_LFO,D_AMP,.3f),MX(2,S_LFO,D_CUT,.25f)),
    PRE("Horror Music Box",{P_OCTAVE,1},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,18},{P_OSC2_FINE,-45},{P_OSC2_LEVEL,.5f},{P_OSC1_FM1,.7f},{P_CUTOFF,.8f},{P_RESONANCE,.3f},
        {P_FILTER_DRIVE,.7f},{P_DRIVE,.4f},{P_FLT_FM1,0},{P_ENV1_D,.45f},{P_ENV2_D,.65f},{P_ENV2_S,.12f},{P_ENV2_R,.6f},{P_DRIFT,1},{P_LFO_WAVE,4},{P_LFO_RATE,.4f},
        MX(1,S_SMR,D_PCH,.03f),MX(2,S_LFO,D_AMP,.45f)),
    PRE("Horror Cellar Door",{P_FILTER_MODE,1},{P_OSC1_WAVE,1},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.4f},{P_OSC2_FINE,17},{P_NOISE_LEVEL,.12f},{P_CUTOFF,.55f},{P_RESONANCE,.8f},
        {P_FILTER_DRIVE,.9f},{P_DRIVE,.6f},{P_FLT_FM1,0},{P_GLIDE,.55f},{P_LEGATO,1},{P_ENV2_A,.15f},{P_ENV2_R,.5f},{P_DRIFT,.9f},{P_LFO_WAVE,4},{P_LFO_RATE,.35f},
        MX(1,S_SMR,D_CUT,.5f),MX(2,S_LFO,D_PCH,.06f)),
    PRE("Horror Slasher Stab",{P_OCTAVE,1},{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,1},{P_OSC2_FINE,10},{P_OSC2_LEVEL,.85f},{P_CUTOFF,.85f},{P_RESONANCE,.45f},{P_FILTER_DRIVE,.9f},
        {P_DRIVE,.7f},{P_FLT_FM1,.3f},{P_ENV1_D,.3f},{P_ENV2_D,.3f},{P_ENV2_S,.12f},{P_ENV2_R,.2f},{P_VIB_DEPTH,.5f},{P_VIB_RATE,.85f},{P_AT_DEST,2},{P_AT_AMOUNT,.8f}),
    PRE("Horror Dread Drone",{P_DRONE,2},{P_OSC1_WAVE,1},{P_OSC1_COARSE,-12},{P_OSC1_LEVEL,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,6},{P_OSC2_FINE,13},{P_OSC2_LEVEL,.8f},{P_XMOD,.45f},
        {P_NOISE_LEVEL,.1f},{P_CUTOFF,.4f},{P_RESONANCE,.5f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.8f},{P_FLT_FM1,0},{P_DRIFT,.8f},{P_LFO2_RATE,.1f},{P_LFO2_WAVE,4},
        MX(1,S_LF2,D_CUT,.5f),MX(2,S_SMR,D_PC2,.05f)),
    PRE("Horror Whispers",{P_FILTER_MODE,1},{P_OSC1_WAVE,2},{P_OSC1_LEVEL,.1f},{P_NOISE_LEVEL,.85f},{P_CUTOFF,.6f},{P_RESONANCE,.9f},{P_FILTER_DRIVE,.8f},{P_KEY_TRACK,.8f},{P_FLT_FM1,0},
        {P_ENV2_A,.5f},{P_ENV2_R,.8f},{P_LFO_WAVE,4},{P_LFO_RATE,.35f},{P_LFO2_RATE,.25f},{P_LFO2_WAVE,4},MX(1,S_LFO,D_CUT,.5f),MX(2,S_LF2,D_RES,.15f),MX(3,S_LF2,D_AMP,.3f)),
    PRE("Horror Heartbeat",{P_OSC1_WAVE,2},{P_OCTAVE,-1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,3},{P_OSC2_LEVEL,.4f},{P_CUTOFF,.3f},{P_RESONANCE,.35f},{P_FILTER_DRIVE,.9f},
        {P_DRIVE,.85f},{P_FLT_FM1,0},{P_LFO_WAVE,2},{P_LFO_SYNC,2},{P_LFO2_WAVE,2},{P_LFO2_SYNC,4},MX(1,S_LFO,D_AMP,.95f),MX(2,S_LF2,D_AMP,.6f),MX(3,S_LFO,D_DRV,.3f)),
    PRE("Horror Theremin Ghost",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_FINE,35},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.85f},{P_RESONANCE,.55f},{P_FILTER_DRIVE,.7f},{P_DRIVE,.5f},{P_FLT_FM1,0},
        {P_GLIDE,.6f},{P_LEGATO,1},{P_VIB_DEPTH,.6f},{P_VIB_RATE,.35f},{P_AT_DEST,2},{P_AT_AMOUNT,.9f},{P_ENV2_A,.25f},{P_ENV2_R,.5f},{P_DRIFT,.8f}),
    PRE("Horror Fly Swarm",{P_FILTER_MODE,1},{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.12f},{P_OSC2_WAVE,1},{P_OSC2_FINE,50},{P_OSC2_LEVEL,.6f},{P_NOISE_LEVEL,.45f},{P_CUTOFF,.74f},
        {P_RESONANCE,.7f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.7f},{P_FLT_FM1,0},{P_LFO_WAVE,4},{P_LFO_RATE,.9f},{P_ENV2_A,.3f},{P_ENV2_R,.5f},MX(1,S_LFO,D_PCH,.09f),MX(2,S_LFO,D_CUT,.35f)),
    PRE("Horror Toll",{P_OCTAVE,-1},{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,16},{P_OSC2_FINE,-20},{P_OSC2_LEVEL,.6f},{P_XMOD,.7f},{P_OSC1_FM1,.7f},{P_CUTOFF,.65f},{P_RESONANCE,.35f},
        {P_FILTER_DRIVE,.7f},{P_DRIVE,.5f},{P_FLT_FM1,0},{P_ENV1_D,.55f},{P_ENV2_D,.9f},{P_ENV2_S,.12f},{P_ENV2_R,.85f},{P_DRIFT,.7f}),
    PRE("Horror Rotting Pipes",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.4f},{P_OSC2_COARSE,1},{P_OSC2_FINE,45},{P_OSC2_LEVEL,.7f},{P_SHAPE_MOD,.5f},{P_LFO_RATE,.12f},
        {P_NOISE_LEVEL,.18f},{P_CUTOFF,.52f},{P_RESONANCE,.5f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.7f},{P_FLT_FM1,0},{P_ENV2_A,.25f},{P_ENV2_R,.55f},{P_DRIFT,1},{P_LFO2_RATE,.1f},
        {P_LFO2_WAVE,4},MX(1,S_SMR,D_CUT,.45f),MX(2,S_SMR,D_PC2,.06f)),
    PRE("Horror Insect Nest",{P_ARP_MODE,4},{P_ARP_RATE,5},{P_ARP_OCT,3},{P_ARP_GATE,.3f},{P_FILTER_MODE,1},{P_OSC1_WAVE,5},{P_OSC2_WAVE,4},{P_OSC2_FINE,15},{P_NOISE_LEVEL,.2f},
        {P_CUTOFF,.7f},{P_RESONANCE,.75f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.7f},{P_FLT_FM1,.4f},{P_ENV1_D,.35f},{P_ENV2_D,.4f},{P_ENV2_S,.3f},{P_ENV2_R,.15f}),
    PRE("Horror Possessed Choir",{P_FILTER_MODE,4},{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,6},{P_OSC2_FINE,14},{P_CUTOFF,.5f},{P_RESONANCE,.85f},{P_FILTER_DRIVE,.85f},{P_DRIVE,.6f},
        {P_FLT_FM1,0},{P_GLIDE,.25f},{P_LEGATO,1},{P_ENV2_A,.4f},{P_ENV2_R,.7f},{P_VIB_DEPTH,.35f},{P_DRIFT,.8f},{P_LFO_SYNC,1},MX(1,S_LFO,D_CUT,.45f),MX(2,S_SMR,D_PCH,.03f)),
    PRE("Horror Grave Rumble",{P_OSC1_WAVE,1},{P_OCTAVE,-1},{P_OSC2_WAVE,5},{P_OSC2_COARSE,-12},{P_OSC2_LEVEL,.5f},{P_NOISE_LEVEL,.28f},{P_NOISE_COLOR,.5f},{P_CUTOFF,.26f},{P_RESONANCE,.5f},
        {P_FILTER_DRIVE,.9f},{P_DRIVE,.8f},{P_FLT_FM1,.2f},{P_LFO2_RATE,.1f},MX(1,S_LF2,D_RES,.3f),MX(2,S_SMR,D_CUT,.2f)),
    PRE("Horror Shriek",{P_OSC1_WAVE,1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,1},{P_OSC2_FINE,-12},{P_OSC2_LEVEL,.9f},{P_XMOD,.5f},{P_NOISE_LEVEL,.1f},{P_CUTOFF,.8f},{P_RESONANCE,.8f},
        {P_FILTER_DRIVE,.95f},{P_DRIVE,.85f},{P_FLT_FM1,.2f},{P_GLIDE,.4f},{P_LEGATO,1},{P_VIB_DEPTH,.4f},{P_VIB_RATE,.9f},{P_AT_DEST,2},{P_AT_AMOUNT,.8f},{P_LFO2_RATE,.3f},
        {P_LFO2_WAVE,4},MX(1,S_LF2,D_CUT,.3f),MX(2,S_SMR,D_PCH,.04f)),
    PRE("Horror Hive Mind",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,1},{P_OSC2_FINE,9},{P_OSC2_LEVEL,.8f},{P_NOISE_LEVEL,.3f},{P_NOISE_COLOR,.3f},{P_FILTER_MODE,4},{P_CUTOFF,.5f},
        {P_RESONANCE,.6f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.7f},{P_FLT_FM1,0},{P_ENV2_A,.6f},{P_ENV2_R,.8f},{P_DRIFT,1},{P_LFO_WAVE,4},{P_LFO_RATE,.3f},{P_LFO2_RATE,.15f},
        {P_LFO2_WAVE,4},MX(1,S_LFO,D_CUT,.4f),MX(2,S_LF2,D_PC2,.04f),MX(3,S_LF2,D_AMP,.25f)),
    /* ---- Set ---- */
    PRE("Set Gritty Punk 1 - Bass",{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,9},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.42f},{P_RESONANCE,.3f},{P_FILTER_DRIVE,.9f},
        {P_DRIVE,.8f},{P_FLT_FM1,.5f},{P_ENV1_D,.45f},{P_ENV2_D,.6f},{P_ENV2_S,.8f},{P_ENV2_R,.15f}),
    PRE("Set Gritty Punk 2 - Lead",{P_OSC2_WAVE,1},{P_OSC2_FINE,6},{P_XMOD,.35f},{P_CUTOFF,.68f},{P_RESONANCE,.35f},{P_FILTER_DRIVE,.85f},{P_DRIVE,.7f},{P_FLT_FM1,.3f},
        {P_ENV1_D,.55f},{P_ENV1_S,.3f},{P_GLIDE,.1f},{P_LEGATO,1},{P_VIB_DEPTH,.15f}),
    PRE("Set Gritty Punk 3 - Stab",{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.6f},{P_RESONANCE,.3f},{P_FILTER_DRIVE,.9f},{P_DRIVE,.8f},
        {P_FLT_FM1,.4f},{P_ENV1_D,.3f},{P_ENV2_D,.35f},{P_ENV2_S,.12f},{P_ENV2_R,.15f}),
    PRE("Set Acid Rain 1 - Bass",{P_OSC2_LEVEL,0},{P_CUTOFF,.28f},{P_RESONANCE,.78f},{P_FILTER_DRIVE,.7f},{P_DRIVE,.4f},{P_FLT_FM1,1},{P_ENV1_D,.5f},{P_ENV2_D,.6f},
        {P_ENV2_S,.7f},{P_ENV2_R,.2f},{P_GLIDE,.12f},{P_LEGATO,1}),
    PRE("Set Acid Rain 2 - Lead",{P_OSC2_LEVEL,0},{P_OCTAVE,1},{P_CUTOFF,.5f},{P_RESONANCE,.7f},{P_FILTER_DRIVE,.8f},{P_DRIVE,.5f},{P_FLT_FM1,.8f},{P_ENV1_D,.4f},{P_ENV2_S,.9f},
        {P_ENV2_R,.2f},{P_GLIDE,.1f},{P_LEGATO,1},{P_VIB_DEPTH,.1f},{P_AT_DEST,1},{P_AT_AMOUNT,.5f}),
    PRE("Set Acid Rain 3 - Arp",{P_ARP_MODE,1},{P_ARP_RATE,3},{P_ARP_OCT,2},{P_ARP_GATE,.5f},{P_OSC2_LEVEL,0},{P_CUTOFF,.38f},{P_RESONANCE,.65f},{P_FILTER_DRIVE,.7f},
        {P_FLT_FM1,.9f},{P_ENV1_D,.45f},{P_ENV2_D,.5f},{P_ENV2_S,.4f},{P_ENV2_R,.2f}),
    PRE("Set Black Mass 1 - Bass",{P_OSC1_WAVE,2},{P_OCTAVE,-1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.4f},{P_CUTOFF,.32f},{P_RESONANCE,.3f},
        {P_FILTER_DRIVE,.8f},{P_DRIVE,.7f},{P_FLT_FM1,.2f},{P_ENV2_R,.4f},{P_DRIFT,.6f}),
    PRE("Set Black Mass 2 - Choir",{P_DRIVE,0.5f},{P_FILTER_DRIVE,0.8f},{P_FILTER_MODE,4},{P_OSC1_WAVE,5},{P_OSC2_WAVE,1},{P_OSC2_FINE,13},{P_CUTOFF,.5f},{P_RESONANCE,.7f},{P_FLT_FM1,0},{P_ENV2_A,.5f},{P_ENV2_R,.75f},
        {P_VIB_DEPTH,.2f},{P_DRIFT,.7f},{P_LFO_SYNC,1},MX(1,S_LFO,D_CUT,.3f)),
    PRE("Set Black Mass 3 - Bells",{P_OSC1_WAVE,2},{P_OSC2_WAVE,2},{P_OSC2_COARSE,19},{P_OSC2_FINE,-18},{P_OSC2_LEVEL,.35f},{P_OSC1_FM1,.5f},{P_OCTAVE,1},{P_CUTOFF,.8f},
        {P_RESONANCE,.15f},{P_FLT_FM1,0},{P_ENV1_D,.45f},{P_ENV2_D,.7f},{P_ENV2_S,.12f},{P_ENV2_R,.7f},{P_DRIFT,.8f}),
    PRE("Set Iron Works 1 - Bass",{P_OSC2_WAVE,4},{P_OSC2_COARSE,-12},{P_CUTOFF,.32f},{P_RESONANCE,.55f},{P_FILTER_DRIVE,.85f},{P_DRIVE,.6f},{P_FLT_FM1,.2f},{P_LFO_SYNC,3},
        MX(1,S_LFO,D_CUT,.45f)),
    PRE("Set Iron Works 2 - Stab",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.3f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.65f},{P_OSC2_COARSE,7},{P_OSC2_FINE,10},{P_OSC2_LEVEL,.8f},{P_XMOD,.4f},
        {P_CUTOFF,.62f},{P_RESONANCE,.4f},{P_FILTER_DRIVE,.85f},{P_DRIVE,.7f},{P_FLT_FM1,.5f},{P_ENV1_D,.3f},{P_ENV2_D,.35f},{P_ENV2_S,.12f},{P_ENV2_R,.2f}),
    PRE("Set Iron Works 3 - Gate",{P_OSC2_WAVE,1},{P_OSC2_FINE,10},{P_CUTOFF,.5f},{P_RESONANCE,.35f},{P_FILTER_DRIVE,.8f},{P_DRIVE,.6f},{P_FLT_FM1,0},{P_LFO_WAVE,1},{P_LFO_SYNC,5},
        MX(1,S_LFO,D_AMP,.85f)),
    PRE("Set Cold Wave 1 - Bass",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.25f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.36f},{P_RESONANCE,.3f},
        {P_FILTER_DRIVE,.5f},{P_DRIVE,.4f},{P_FLT_FM1,.7f},{P_ENV1_D,.35f},{P_ENV2_D,.5f},{P_ENV2_S,.75f},{P_ENV2_R,.12f},{P_KEY_TRACK,.7f}),
    PRE("Set Cold Wave 2 - Lead",{P_OSC1_WAVE,5},{P_OSC2_WAVE,2},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.5f},{P_CUTOFF,.75f},{P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_DRIVE,.2f},
        {P_FLT_FM1,.2f},{P_ENV1_D,.55f},{P_ENV1_S,.5f},{P_VIB_DEPTH,.2f},{P_VIB_RATE,.5f},{P_ENV2_R,.4f},{P_GLIDE,.08f}),
    PRE("Set Cold Wave 3 - Pad",{P_OSC2_WAVE,1},{P_OSC2_FINE,11},{P_OSC2_LEVEL,.8f},{P_CUTOFF,.62f},{P_RESONANCE,.15f},{P_FLT_FM1,0},{P_ENV2_A,.6f},{P_ENV2_R,.75f},{P_DRIFT,.5f},
        {P_LFO_RATE,.2f},MX(1,S_LFO,D_CUT,.15f)),
    PRE("Set Dub Cellar 1 - Sub",{P_OSC1_WAVE,6},{P_OSC1_SHAPE,.3f},{P_OCTAVE,-1},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,0},{P_OSC2_LEVEL,.3f},{P_CUTOFF,.42f},
        {P_RESONANCE,.45f},{P_FILTER_DRIVE,.8f},{P_DRIVE,.7f},{P_FLT_FM1,.35f},{P_ENV1_D,.5f},{P_ENV2_A,.02f},{P_ENV2_R,.55f},{P_GLIDE,.15f},{P_LEGATO,1}),
    PRE("Set Dub Cellar 2 - Stab",{P_FILTER_DRIVE,0.7f},{P_FILTER_MODE,4},{P_OSC2_WAVE,1},{P_OSC2_COARSE,7},{P_OSC2_FINE,6},{P_OSC2_LEVEL,.7f},{P_CUTOFF,.55f},{P_RESONANCE,.55f},{P_FLT_FM1,.4f},
        {P_ENV1_D,.4f},{P_ENV2_D,.5f},{P_ENV2_S,.1f},{P_ENV2_R,.4f},{P_DRIVE,0.55f}),
    PRE("Set Dub Cellar 3 - Siren",{P_OSC1_WAVE,2},{P_OSC2_LEVEL,0},{P_OSC1_FM2,.45f},{P_LFO_RATE,.5f},{P_CUTOFF,.7f},{P_RESONANCE,.3f},{P_FLT_FM1,0},{P_ENV2_A,.2f},{P_ENV2_R,.4f},
        {P_GLIDE,.2f},{P_LEGATO,1}),
    PRE("Set Doom 1 - Drone Bass",{P_DRONE,1},{P_OSC1_WAVE,1},{P_OSC1_COARSE,-12},{P_OSC2_WAVE,2},{P_OSC2_COARSE,-5},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.38f},
        {P_RESONANCE,.3f},{P_FILTER_DRIVE,.8f},{P_DRIVE,.7f},{P_FLT_FM1,0},{P_DRIFT,.6f},{P_LFO2_RATE,.1f},MX(1,S_LF2,D_CUT,.2f)),
    PRE("Set Doom 2 - Horns",{P_OSC2_WAVE,1},{P_OSC2_FINE,9},{P_CUTOFF,.5f},{P_RESONANCE,.3f},{P_FILTER_DRIVE,0.85f},{P_DRIVE,0.65f},{P_FLT_FM1,.6f},{P_ENV1_A,.5f},{P_ENV1_D,.7f},
        {P_ENV1_S,.5f},{P_ENV2_A,.4f},{P_ENV2_S,.9f},{P_ENV2_R,.5f},{P_AT_DEST,1},{P_AT_AMOUNT,.6f}),
    PRE("Set Doom 3 - Choir",{P_DRIVE,0.45f},{P_FILTER_DRIVE,0.7f},{P_FILTER_MODE,4},{P_OSC1_WAVE,5},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_FINE,6},{P_CUTOFF,.55f},{P_RESONANCE,.6f},{P_FLT_FM1,0},{P_ENV2_A,.65f},
        {P_ENV2_R,.85f},{P_DRIFT,.6f},{P_VIB_DEPTH,.15f}),
    PRE("Set Neon Static 1 - Bass",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.35f},{P_OSC2_WAVE,1},{P_OSC2_COARSE,-12},{P_OSC2_FINE,5},{P_OSC2_LEVEL,.6f},{P_CUTOFF,.58f},{P_RESONANCE,.25f},
        {P_FILTER_DRIVE,.4f},{P_DRIVE,.3f},{P_FLT_FM1,.5f},{P_ENV1_D,.4f},{P_ENV2_D,.55f},{P_ENV2_S,.8f},{P_ENV2_R,.12f}),
    PRE("Set Neon Static 2 - Lead",{P_OSC1_WAVE,4},{P_OSC1_SHAPE,.5f},{P_OSC2_WAVE,4},{P_OSC2_SHAPE,.5f},{P_OSC2_FINE,7},{P_SHAPE_MOD,.6f},{P_LFO_RATE,.22f},{P_CUTOFF,.85f},
        {P_RESONANCE,.2f},{P_FILTER_DRIVE,.3f},{P_FLT_FM1,.15f},{P_VIB_DEPTH,.12f},{P_GLIDE,.08f},{P_LEGATO,1}),
    PRE("Set Neon Static 3 - Arp",{P_ARP_MODE,3},{P_ARP_RATE,3},{P_ARP_OCT,3},{P_ARP_GATE,.35f},{P_OSC1_WAVE,5},{P_OSC2_WAVE,2},{P_OSC2_COARSE,12},{P_OSC2_FINE,0},
        {P_OSC2_LEVEL,.5f},{P_CUTOFF,.88f},{P_RESONANCE,.15f},{P_FLT_FM1,.2f},{P_ENV1_D,.35f},{P_ENV2_D,.4f},{P_ENV2_S,.15f},{P_ENV2_R,.15f}),
};
#define N_PRESETS ((int)(sizeof PRESETS / sizeof PRESETS[0]))

/* Banks: a factory preset belongs to the bank named by the first word of its name ("Set" is the Sets bank). The tables are built once at load;
 * a bank keeps the presets in array order, and the host's browser lists only the current bank.
 * User banks (files in the bank folder, see docs/BANK_FILES.md) follow the factory banks: bank N_BANKS + i is the i-th file in name order. A user
 * preset is addressed by the code USER_BASE + i * USER_STRIDE + position, so `preset` numbers below N_PRESETS stay the factory presets. */
static const char *const PCAT[N_BANKS] = {"Init", "Bass", "Sub", "Lead", "Pluck", "Keys", "Stab", "Brass", "Arp", "Seq", "Pad", "Texture", "Vox", "Drone", "FX", "Horror", "Set"};
static const char *const BANK_FILE_NAME[N_BANKS] = {"Init", "Bass", "Sub", "Lead", "Pluck", "Keys", "Stab", "Brass", "Arp", "Seq", "Pad", "Texture", "Vox", "Drone", "FX", "Horror", "Sets"};
_Static_assert(sizeof PRESETS / sizeof PRESETS[0] <= MAX_PRESETS, "raise MAX_PRESETS");
static int g_bank_n[N_BANKS], g_bank_idx[N_BANKS][BANK_MAX], g_pbank[MAX_PRESETS], g_ppos[MAX_PRESETS];
static void banks_init(void) {
    memset(g_bank_n, 0, sizeof g_bank_n);
    for (int i = 0; i < N_PRESETS && i < MAX_PRESETS; i++) {
        const char *nm = PRESETS[i].name; int b = 0;
        for (int c = 0; c < N_BANKS; c++) { size_t L = strlen(PCAT[c]); if (!strncmp(nm, PCAT[c], L) && (nm[L] == ' ' || nm[L] == 0)) { b = c; break; } }
        if (g_bank_n[b] >= BANK_MAX) b = 0;
        g_pbank[i] = b; g_ppos[i] = g_bank_n[b]; g_bank_idx[b][g_bank_n[b]++] = i;
    }
}

/* ---- user banks: loaded from files by a background thread, published to the audio side as one of three static buffers ---- */
typedef struct { char name[40]; int n; pv_t v[UB_PV]; } upre_t;
typedef struct { char name[32]; int n; upre_t p[UB_PRE]; } ubank_t;
typedef struct { int nb; ubank_t b[MAX_UBANKS]; } ubuf_t;
static ubuf_t g_ub[3];
static ubuf_t *g_pub;                 /* the buffer the audio side may read (NULL until the first load finished) */
static int g_req, g_req_bank, g_status; /* worker requests / status, all accessed atomically */
enum { REQ_RELOAD = 1, REQ_EXPORT = 2 };
enum { ST_READY, ST_WORKING, ST_EXPORTED, ST_LOADED, ST_ERROR };
static char g_base_dir[200];
static const ubuf_t *user_view(void) { return __atomic_load_n(&g_pub, __ATOMIC_ACQUIRE); }
static int user_bank_count(void) { const ubuf_t *u = user_view(); return u ? u->nb : 0; }

static int preset_cat_of(int idx) {
    if (idx >= USER_BASE) return N_BANKS + (idx - USER_BASE) / USER_STRIDE;
    return (idx >= 0 && idx < N_PRESETS && idx < MAX_PRESETS) ? g_pbank[idx] : 0;
}
static int preset_pos_of(int idx) { return idx >= USER_BASE ? (idx - USER_BASE) % USER_STRIDE : (idx >= 0 && idx < MAX_PRESETS ? g_ppos[idx] : 0); }
static int bank_size(int b) {
    if (b >= 0 && b < N_BANKS) return g_bank_n[b];
    const ubuf_t *u = user_view();
    return (u && b >= N_BANKS && b - N_BANKS < u->nb) ? u->b[b - N_BANKS].n : 0;
}
static int bank_code(int b, int k) {   /* preset code of position k in bank b, or -1 */
    if (k < 0 || k >= bank_size(b)) return -1;
    return b < N_BANKS ? g_bank_idx[b][k] : USER_BASE + (b - N_BANKS) * USER_STRIDE + k;
}
static int preset_cat_first(int cat) { return bank_code(cat, 0); }
static int preset_ref(int code, preset_t *out) {   /* resolves a preset code (factory or user) to its definition */
    if (code >= 0 && code < N_PRESETS) { *out = PRESETS[code]; return 1; }
    const ubuf_t *u = user_view();
    if (u && code >= USER_BASE) {
        int ub = (code - USER_BASE) / USER_STRIDE, k = (code - USER_BASE) % USER_STRIDE;
        if (ub < u->nb && k < u->b[ub].n) { out->name = u->b[ub].p[k].name; out->v = u->b[ub].p[k].v; out->n = u->b[ub].p[k].n; return 1; }
    }
    return 0;
}
/* The name shown in the browser: without the bank word (the Bank knob already says it); Init and user banks keep their names whole. */
static const char *preset_label(int code, const char **full) {
    preset_t pr; static const char none[] = "-";
    if (!preset_ref(code, &pr)) { *full = none; return none; }
    *full = pr.name;
    int b = preset_cat_of(code);
    if (b == 0 || b >= N_BANKS) return pr.name;
    size_t L = strlen(PCAT[b]);
    return pr.name[L] == ' ' ? pr.name + L + 1 : pr.name;
}

static void apply_preset(mm_t *m, int idx) {
    preset_t pr;
    if (!preset_ref(idx, &pr)) return;
    float keep[10];                                      /* the FX bank is a performance setup: presets leave it alone */
    for (int k = 0; k < 4; k++) { keep[k] = m->p[P_FX1_TYPE + k]; keep[4 + k] = m->p[P_FX1_LVL + k]; }
    keep[8] = m->p[P_FX_SCOPE]; keep[9] = m->p[P_PROG_CHG];
    for (int i = 0; i < P_COUNT; i++) m->p[i] = PD[i].def;
    for (int k = 0; k < 4; k++) { m->p[P_FX1_TYPE + k] = keep[k]; m->p[P_FX1_LVL + k] = keep[4 + k]; }
    m->p[P_FX_SCOPE] = keep[8]; m->p[P_PROG_CHG] = keep[9];
    for (int i = 0; i < pr.n; i++) {
        int id = pr.v[i].id;
        m->p[id] = clampf(pr.v[i].v, PD[id].min, PD[id].max);
    }
    m->preset = idx;
    m->p[P_PRESET_CAT] = (float)preset_cat_of(idx);
    unlatch(m);
    sync_modes(m);
}

/* ---- bank files (worker thread only below this line until bank_worker_start) ---------------------------------------------------------
 * Format (text, one setting per line, '#' starts a comment):   mmbank 1 / bank <name> / preset <name> / <param key> <value> ...
 * Only the settings that differ from the defaults need to be listed; unknown keys are ignored, values are clamped to their ranges. */
static int key_is_forbidden(int id) {   /* performance state, never part of a sound */
    return (id >= P_FX1_TYPE && id <= P_FX4_LVL) || id == P_FX_SCOPE || id == P_PRESET_CAT || id == P_STICK_X || id == P_STICK_Y ||
           id == P_BANK_IO || id == P_BANK_STATUS || id == P_USER_BANKS || id == P_VERSION || id == P_PROG_CHG || id == P_LAST_CHANGE || id == P_PC_SEEN;
}
static void copy_name(char *dst, size_t n, const char *s) {
    size_t w = 0;
    while (*s == ' ' || *s == '\t') s++;
    for (; *s && *s != '\r' && *s != '\n' && w + 1 < n; s++) dst[w++] = (*s >= 32 && *s < 127) ? *s : '?';
    while (w > 0 && dst[w - 1] == ' ') w--;
    dst[w] = 0;
}
static void parse_bank_file(const char *path, const char *fallback_name, ubuf_t *dst) {
    FILE *f = fopen(path, "r");
    if (!f) return;
    char line[256]; ubank_t *bk = NULL; upre_t *pr = NULL;
    while (fgets(line, sizeof line, f)) {
        char *s = line; while (*s == ' ' || *s == '\t') s++;
        if (!*s || *s == '#' || *s == '\n' || *s == '\r') continue;
        if (!strncmp(s, "mmbank", 6)) continue;
        if (!strncmp(s, "bank ", 5)) {
            if (dst->nb >= MAX_UBANKS) break;
            bk = &dst->b[dst->nb++]; memset(bk, 0, sizeof *bk); copy_name(bk->name, sizeof bk->name, s + 5); pr = NULL;
            continue;
        }
        if (!bk) {   /* settings before any "bank" line: the file name is the bank name */
            if (dst->nb >= MAX_UBANKS) break;
            bk = &dst->b[dst->nb++]; memset(bk, 0, sizeof *bk); copy_name(bk->name, sizeof bk->name, fallback_name);
        }
        if (!strncmp(s, "preset ", 7)) {
            if (bk->n >= UB_PRE) { pr = NULL; continue; }
            pr = &bk->p[bk->n++]; memset(pr, 0, sizeof *pr); copy_name(pr->name, sizeof pr->name, s + 7);
            if (!pr->name[0]) snprintf(pr->name, sizeof pr->name, "Preset %d", bk->n);
            continue;
        }
        if (!pr) continue;
        char key[48]; float v; int len = 0;
        if (sscanf(s, "%47s %f%n", key, &v, &len) < 2 || !isfinite(v)) continue;
        for (int id = 0; id < P_COUNT; id++) {
            if (strcmp(key, PD[id].key) || key_is_forbidden(id)) continue;
            v = clampf(v, PD[id].min, PD[id].max);
            if (PD[id].kind != K_F) v = floorf(v + 0.5f);
            int j = 0; while (j < pr->n && pr->v[j].id != id) j++;
            if (j < pr->n) pr->v[j].v = v;
            else if (pr->n < UB_PV) { pr->v[pr->n].id = id; pr->v[pr->n].v = v; pr->n++; }
            break;
        }
    }
    fclose(f);
}
static int name_cmp(const void *a, const void *b) { return strcmp((const char *)a, (const char *)b); }
static void make_dirs(void) {
    char p[256];
    snprintf(p, sizeof p, "%s", g_base_dir);
    for (char *c = p + 1; *c; c++) if (*c == '/') { *c = 0; mkdir(p, 0777); *c = '/'; }
    mkdir(p, 0777);
    snprintf(p, sizeof p, "%s/banks", g_base_dir); mkdir(p, 0777);
    snprintf(p, sizeof p, "%s/exports", g_base_dir); mkdir(p, 0777);
}
static void bank_reload(void) {
    static char names[64][256];
    char dir[300]; snprintf(dir, sizeof dir, "%s/banks", g_base_dir);
    int nf = 0; DIR *d = opendir(dir);
    if (!d) { __atomic_store_n(&g_status, ST_ERROR, __ATOMIC_RELEASE); return; }
    for (struct dirent *e; nf < 64 && (e = readdir(d)); ) {
        size_t L = strlen(e->d_name);
        if (L > 7 && L < sizeof names[0] && e->d_name[0] != '.' && !strcasecmp(e->d_name + L - 7, ".mmbank")) snprintf(names[nf++], sizeof names[0], "%s", e->d_name);
    }
    closedir(d);
    qsort(names, (size_t)nf, sizeof names[0], name_cmp);
    static int next;
    ubuf_t *nb = &g_ub[next]; if (nb == g_pub) { next = (next + 1) % 3; nb = &g_ub[next]; }
    next = (next + 1) % 3;
    memset(nb, 0, sizeof *nb);
    for (int i = 0; i < nf && nb->nb < MAX_UBANKS; i++) {
        char path[600], base[256]; snprintf(path, sizeof path, "%.299s/%.255s", dir, names[i]);
        snprintf(base, sizeof base, "%.255s", names[i]); base[strlen(base) - 7] = 0;
        const int before = nb->nb;
        parse_bank_file(path, base, nb);
        for (int j = before; j < nb->nb; j++) if (nb->b[j].n == 0) { memmove(&nb->b[j], &nb->b[j + 1], (size_t)(nb->nb - j - 1) * sizeof nb->b[0]); nb->nb--; j--; }
    }
    __atomic_store_n(&g_pub, nb, __ATOMIC_RELEASE);
    __atomic_store_n(&g_status, ST_LOADED, __ATOMIC_RELEASE);
}
static void bank_export(int b) {
    char name[64];
    if (b >= 0 && b < N_BANKS) snprintf(name, sizeof name, "%s", BANK_FILE_NAME[b]);
    else { const ubuf_t *u = user_view(); if (!u || b < N_BANKS || b - N_BANKS >= u->nb) { __atomic_store_n(&g_status, ST_ERROR, __ATOMIC_RELEASE); return; } snprintf(name, sizeof name, "%s", u->b[b - N_BANKS].name); }
    for (char *c = name; *c; c++) if (!((*c >= '0' && *c <= '9') || (*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || *c == '-' || *c == '_' || *c == ' ')) *c = '_';
    char path[512], tmp[520]; snprintf(path, sizeof path, "%s/exports/%s.mmbank", g_base_dir, name); snprintf(tmp, sizeof tmp, "%s.tmp", path);
    make_dirs();
    FILE *f = fopen(tmp, "w");
    if (!f) { __atomic_store_n(&g_status, ST_ERROR, __ATOMIC_RELEASE); return; }
    fprintf(f, "# Mean Module bank. Move this file into the banks folder to load it (docs/BANK_FILES.md).\nmmbank 1\nbank %s\n", name);
    for (int k = 0; k < bank_size(b); k++) {
        preset_t pr; const char *full; int code = bank_code(b, k);
        if (!preset_ref(code, &pr)) continue;
        fprintf(f, "\npreset %s\n", preset_label(code, &full));
        for (int i = 0; i < pr.n; i++) fprintf(f, "%s %g\n", PD[pr.v[i].id].key, (double)pr.v[i].v);
    }
    int ok = !ferror(f); ok = (fclose(f) == 0) && ok;
    if (ok) ok = rename(tmp, path) == 0;
    __atomic_store_n(&g_status, ok ? ST_EXPORTED : ST_ERROR, __ATOMIC_RELEASE);
}
static void *bank_worker(void *arg) {
    (void)arg;
    struct sched_param sp; memset(&sp, 0, sizeof sp);
    pthread_setschedparam(pthread_self(), SCHED_OTHER, &sp);   /* never inherit the audio thread's realtime priority */
    cpu_set_t cs; CPU_ZERO(&cs); for (int c = 0; c < 3; c++) CPU_SET(c, &cs);
    sched_setaffinity(0, sizeof cs, &cs);
    make_dirs();
    bank_reload();
    for (;;) {
        usleep(50000);
        int r = __atomic_exchange_n(&g_req, 0, __ATOMIC_ACQ_REL);
        if (r & REQ_EXPORT) bank_export(__atomic_load_n(&g_req_bank, __ATOMIC_ACQUIRE));
        if (r & REQ_RELOAD) bank_reload();
    }
    return NULL;
}
/* Started once, when the library is loaded (never from the audio callback). The library pins itself in memory so the thread can never
 * outlive its code if the host unloads the module. */
static void bank_worker_start(void) {
    static int started;
    if (__atomic_exchange_n(&started, 1, __ATOMIC_ACQ_REL)) return;
    const char *e = getenv("MEANMOD_BANK_DIR");
    snprintf(g_base_dir, sizeof g_base_dir, "%s", (e && *e) ? e : "/data/UserData/schwung/mean-module");
    Dl_info info;
    if (dladdr((void *)bank_worker_start, &info) && info.dli_fname) dlopen(info.dli_fname, RTLD_NOW | RTLD_NODELETE);
    pthread_t th; pthread_attr_t at; pthread_attr_init(&at); pthread_attr_setdetachstate(&at, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &at, bank_worker, NULL) != 0) __atomic_store_n(&g_status, ST_ERROR, __ATOMIC_RELEASE);
    pthread_attr_destroy(&at);
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
    if (q) { int pi = atoi(q + 9); if ((pi >= 0 && pi < N_PRESETS) || pi >= USER_BASE) m->preset = pi; }
    m->p[P_PRESET_CAT] = (float)preset_cat_of(m->preset);
    m->why = 5;
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
    if (UNIT[i]) w = app(b, n, w, ",\"unit\":\"%s\"", UNIT[i]);
    if (i == P_BANK_STATUS || i == P_USER_BANKS || i == P_VERSION || i == P_LAST_CHANGE || i == P_PC_SEEN) w = app(b, n, w, ",\"access\":\"read\"");
    if (VIZ_GROUP[i]) w = app(b, n, w, ",\"viz\":{\"group\":\"%s\",\"role\":\"%s\"}", VIZ_GROUP[i], VIZ_ROLE[i]);
    else if (VIZ_KIND[i]) w = app(b, n, w, ",\"viz\":{\"kind\":\"%s\"}", VIZ_KIND[i]);
    return app(b, n, w, "}");
}

/* Pages (levels): each is a named grid page of up to 8 knobs. Root holds the performance macros and the preset browser.
 * Keys only here; metadata, full names and short cell names come from chain_params. Enforced by tests/host_test.c. */
#define K_ROOT  "\"cutoff\",\"resonance\",\"filter_mode\",\"grit\",\"env2_a\",\"env2_r\",\"preset_cat\",\"volume\""
#define K_PERF  "\"hold\",\"drone\",\"octave\",\"glide\",\"arp_mode\",\"arp_rate\",\"arp_octaves\",\"arp_gate\""
#define K_FXB   "\"fx1_lvl\",\"fx2_lvl\",\"fx3_lvl\",\"fx4_lvl\",\"fx1_type\",\"fx2_type\",\"fx3_type\",\"fx4_type\""
#define K_STICK "\"stick_x\",\"stick_y\",\"stick_x_to\",\"stick_y_to\",\"bend_range\",\"vibrato_depth\",\"vib_rate\",\"hold\""
#define K_OSC   "\"osc1_wave\",\"osc1_shape\",\"osc2_wave\",\"osc2_shape\",\"osc1_level\",\"osc2_level\",\"noise_level\",\"noise_color\""
#define K_FILT  "\"cutoff\",\"resonance\",\"filter_mode\",\"filter_drive\",\"flt_fm1\",\"key_track\",\"drive\""
#define K_ENV1  "\"env1_a\",\"env1_d\",\"env1_s\",\"env1_r\",\"flt_fm1\",\"osc1_fm1\",\"osc2_fm1\",\"vel_cutoff\""
#define K_ENV2  "\"env2_a\",\"env2_d\",\"env2_s\",\"env2_r\",\"vel_amp\",\"drone\""
#define K_LFO1  "\"lfo_wave\",\"lfo_rate\",\"flt_fm2\",\"lfo_sync\",\"osc1_fm2\",\"osc2_fm2\",\"shape_mod\""
#define K_LFO2  "\"lfo2_wave\",\"lfo2_rate\",\"lfo2_sync\""
#define K_OMOD  "\"xmod_2to1\",\"osc1_fm1\",\"osc2_fm1\""
#define K_TUNE  "\"osc1_coarse\",\"osc1_fine\",\"osc2_coarse\",\"osc2_fine\",\"octave\",\"tune\",\"drift\",\"bend_range\""
#define K_VOICE "\"glide\",\"legato\",\"priority\",\"drift\",\"vibrato_depth\",\"vib_rate\",\"bend_range\",\"fx_scope\""
#define K_RESP  "\"vel_cutoff\",\"vel_amp\",\"at_dest\",\"at_amount\""
#define K_BANKS "\"preset_cat\",\"bank_io\",\"bank_status\",\"user_banks\",\"version\",\"prog_chg\",\"last_change\",\"pc_seen\""
#define K_MX1   "\"m1_src\",\"m1_dst\",\"m1_amt\",\"m1_via\",\"m2_src\",\"m2_dst\",\"m2_amt\",\"m2_via\""
#define K_MX2   "\"m3_src\",\"m3_dst\",\"m3_amt\",\"m3_via\",\"m4_src\",\"m4_dst\",\"m4_amt\",\"m4_via\""
#define K_MX3   "\"m5_src\",\"m5_dst\",\"m5_amt\",\"m5_via\",\"m6_src\",\"m6_dst\",\"m6_amt\",\"m6_via\""
#define LEVEL(id, title, keys) "\"" id "\":{\"name\":\"" title "\",\"children\":null,\"knobs\":[" keys "],\"params\":[" keys "]}"
static const char HIERARCHY[] =
    "{\"modes\":null,\"levels\":{"
    "\"root\":{\"name\":\"Mean Mod\",\"list_param\":\"bank_preset\",\"count_param\":\"bank_count\",\"name_param\":\"preset_name\",\"children\":null,"
        "\"knobs\":[" K_ROOT "],"
        "\"params\":[{\"level\":\"perform\",\"label\":\"Perform\"},{\"level\":\"fxbank\",\"label\":\"FX Bank\"},{\"level\":\"stick\",\"label\":\"Stick\"},"
        "{\"level\":\"osc\",\"label\":\"Oscillators\"},{\"level\":\"filter\",\"label\":\"Filter\"},{\"level\":\"env2\",\"label\":\"Amp Env\"},{\"level\":\"env1\",\"label\":\"Filter Env\"},"
        "{\"level\":\"lfo\",\"label\":\"LFO 1\"},{\"level\":\"lfo2\",\"label\":\"LFO 2\"},{\"level\":\"omod\",\"label\":\"FM and X-Mod\"},{\"level\":\"tune\",\"label\":\"Tuning\"},"
        "{\"level\":\"voice\",\"label\":\"Voice\"},{\"level\":\"resp\",\"label\":\"Response\"},{\"level\":\"banks\",\"label\":\"Bank Files\"},"
        "{\"level\":\"mx1\",\"label\":\"Matrix 1-2\"},{\"level\":\"mx2\",\"label\":\"Matrix 3-4\"},{\"level\":\"mx3\",\"label\":\"Matrix 5-6\"},"
        "{\"key\":\"env2_a\",\"short_name\":\"AmpA\"},{\"key\":\"env2_r\",\"short_name\":\"AmpR\"}]},"
    LEVEL("perform", "Perform", K_PERF) ","
    LEVEL("fxbank", "FX Bank", K_FXB) ","
    LEVEL("stick", "Stick", K_STICK) ","
    LEVEL("osc", "Oscillators", K_OSC) ","
    LEVEL("filter", "Filter", K_FILT) ","
    LEVEL("env2", "Amp Env", K_ENV2) ","
    LEVEL("env1", "Filter Env", K_ENV1) ","
    LEVEL("lfo", "LFO 1", K_LFO1) ","
    LEVEL("lfo2", "LFO 2", K_LFO2) ","
    LEVEL("omod", "FM and X-Mod", K_OMOD) ","
    LEVEL("tune", "Tuning", K_TUNE) ","
    LEVEL("voice", "Voice", K_VOICE) ","
    LEVEL("resp", "Response", K_RESP) ","
    LEVEL("banks", "Bank Files", K_BANKS) ","
    LEVEL("mx1", "Matrix 1-2", K_MX1) ","
    LEVEL("mx2", "Matrix 3-4", K_MX2) ","
    LEVEL("mx3", "Matrix 5-6", K_MX3)
    "}}";

static void build_json(mm_t *m) {
    int n = JSON_MAX, w = 0;
    char *b = m->chain_json;
    w = app(b, n, w, "[{\"key\":\"preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":%d}", N_PRESETS - 1);
    w = app(b, n, w, ",{\"key\":\"bank_preset\",\"name\":\"Preset\",\"type\":\"int\",\"min\":0,\"max\":%d}", BANK_MAX - 1);
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
    m->bend = m->at = m->modwheel = 0.0f; m->bend_n = m->arp_ramp = m->age = m->srnd = m->srnd1 = m->srnd_tgt = m->ped = m->o1_prev = 0.0f; m->srnd_cnt = 0; m->mx_ld[0] = m->mx_ld[1] = 0.0f;
    m->touch_mask = 0; m->tap_ok = m->tap_short = m->fx_sup = 0; m->fx_armed = 0; m->fx_page_t = 0; m->fx_hold_on = 0; m->chop_g = 1.0f; m->sm_init = 0;
    memset(m->fxa, 0, sizeof m->fxa); memset(m->fx_tgt, 0, sizeof m->fx_tgt);
    m->expr = 1.0f; m->rpn_msb = m->rpn_lsb = 127;
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

/* FX bank: knobs 1-4. A double tap (a short first touch, then a second touch within ~0.43 s) steps that knob's effect to the next type
 * and keeps the second touch silent; any other touch applies the assigned effect once it has lasted FX_DELAY_BLK blocks. */
/* The host cannot say which page is showing, but it re-reads the visible page's knobs several times a second (get_param stamps fx_page_t).
 * So the FX Bank acts only while one of its own keys was read recently, unless FX Touch is set to Any Page. */
static int fx_here(const mm_t *m) {
    return (int)m->p[P_FX_SCOPE] == 1 || (m->fx_page_t && m->clk + 1 - m->fx_page_t < FX_PAGE_BLK);
}
static void fx_touch_on(mm_t *m, int k) {
    const uint32_t now = m->clk, bit = 1u << k;
    if ((m->tap_ok & bit) && (m->tap_short & bit) && now - m->tap_t[k] < FX_DTAP_BLK) {
        m->p[P_FX1_TYPE + k] = (float)(((int)m->p[P_FX1_TYPE + k] + 1) % N_FXT);
        m->tap_ok &= (uint8_t)~bit; m->fx_sup |= (uint8_t)bit;
    } else {
        m->tap_ok |= (uint8_t)bit; m->tap_short &= (uint8_t)~bit; m->tap_t[k] = now; m->fx_sup &= (uint8_t)~bit;
    }
    m->touch_t[k] = now;
}
static void fx_touch_off(mm_t *m, int k) {
    const uint32_t bit = 1u << k;
    if (m->fx_sup & bit) { m->fx_sup &= (uint8_t)~bit; return; }
    if (m->clk - m->touch_t[k] < FX_SHORT_BLK) m->tap_short |= (uint8_t)bit; else m->tap_ok &= (uint8_t)~bit;
}

static void on_midi(void *inst, const uint8_t *msg, int len, int src) {
    mm_t *m = inst;
    if (!m || !msg || len < 1) return;
    int st = msg[0] & 0xF0, d1 = len > 1 ? msg[1] & 0x7F : 0, d2 = len > 2 ? msg[2] & 0x7F : 0;
    if (src == MOVE_MIDI_SOURCE_TOUCH) {   /* capacitive touch of Knobs 1-8 (notes 0-7): never a played note */
        if ((st == 0x90 || st == 0x80) && d1 < 8) {
            int down = (st == 0x90 && d2 > 0), was = (m->touch_mask >> d1) & 1;
            if (down && !was && d1 < 4 && fx_here(m)) { m->fx_armed |= (uint8_t)(1u << d1); fx_touch_on(m, d1); }
            if (!down && was && d1 < 4 && ((m->fx_armed >> d1) & 1)) { m->fx_armed &= (uint8_t)~(1u << d1); fx_touch_off(m, d1); }
            if (down) m->touch_mask |= (uint8_t)(1u << d1); else m->touch_mask &= (uint8_t)~(1u << d1);
        }
        if ((st == 0x90 || st == 0x80) && d1 < 2) {
            int down = (st == 0x90 && d2 > 0);
            m->touched[d1] = (uint8_t)down;
            if (down) m->touch_seen = 1;
            else { m->p[d1 ? P_STICK_Y : P_STICK_X] = 0.0f; m->stick_age[d1] = 0; if (d1) { m->y_down = 0; m->y_dwell = 0; } }   /* the stick springs back */
        }
        return;
    }
    switch (st) {
    case 0x90: if (d2 > 0) in_on(m, d1, d2); else in_off(m, d1); break;
    case 0x80: in_off(m, d1); break;
    case 0xE0: m->bend_n = (float)(((d2 << 7) | d1) - 8192) / 8192.0f; m->bend = m->bend_n * m->p[P_BEND_RANGE]; break;
    case 0xA0: m->at = d2 / 127.0f; break;                 /* poly AT (mono voice: any key) */
    case 0xD0: m->at = d1 / 127.0f; break;                 /* channel pressure */
    case 0xB0:
        if (d1 == 1) m->modwheel = d2 / 127.0f;
        else if (d1 == 64) pedal(m, d2 >= 64);
        else if (d1 == 121) { m->modwheel = 0.0f; m->at = 0.0f; m->bend = 0.0f; m->bend_n = 0.0f; m->expr = 1.0f; m->rpn_msb = m->rpn_lsb = 127; pedal(m, 0); }   /* reset all controllers */
        else if (d1 == 123 || d1 == 120) panic(m);
        else if (src == MOVE_MIDI_SOURCE_EXTERNAL) {   /* common controller CCs; never from Move's own surface (CC 71-78 are its knobs) */
            const float v = d2 / 127.0f;
            switch (d1) {
            case 0: m->cc_bank = d2; break;                          /* bank select: preset = bank * 128 + program */
            case 5: m->p[P_GLIDE] = v; break;                        /* portamento time */
            case 7: m->p[P_VOLUME] = v; break;
            case 11: m->expr = v; break;
            case 71: m->p[P_RESONANCE] = v; break;
            case 72: m->p[P_ENV2_R] = v; break;
            case 73: m->p[P_ENV2_A] = v; break;
            case 74: m->p[P_CUTOFF] = v; break;
            case 101: m->rpn_msb = d2; break;
            case 100: m->rpn_lsb = d2; break;
            case 6: if (m->rpn_msb == 0 && m->rpn_lsb == 0) m->p[P_BEND_RANGE] = clampf((float)d2, 1.0f, 12.0f); break;   /* RPN 0: pitch bend range */
            default: break;
            }
        }
        break;
    case 0xC0:   /* Program Change from outside: bank select 0 = the whole list by number, bank select n = program d1 inside bank n (1 = Init, 2 = Bass ...) */
        m->pc_seen = (m->pc_seen + 1) & 127;   /* counted from every source, acted on only from outside and only when Program Change = Accept */
        if (src == MOVE_MIDI_SOURCE_EXTERNAL && (int)m->p[P_PROG_CHG] == 1) {
            if (m->cc_bank == 0) { apply_preset(m, d1); m->why = 4; }
            else { int code = bank_code(m->cc_bank - 1, d1); if (code >= 0) { apply_preset(m, code); m->why = 4; } }
        }
        break;
    default: break;
    }
}

static void set_param(void *inst, const char *key, const char *val) {
    mm_t *m = inst;
    if (!m || !key || !val) return;
    if (!strcmp(key, "state")) { state_read(m, val); return; }
    if (!strcmp(key, "preset")) { char *e; float v = strtof(val, &e); if (e != val && isfinite(v)) { apply_preset(m, (int)v); m->why = 1; } return; }
    if (!strcmp(key, "bank_preset")) {   /* position inside the current bank (what the jog wheel browses) */
        char *e; float v = strtof(val, &e); const int b = preset_cat_of(m->preset), cnt = bank_size(b);
        if (e != val && isfinite(v) && cnt > 0) { int k = (int)v; if (k < 0) k = 0; if (k >= cnt) k = cnt - 1; apply_preset(m, bank_code(b, k)); m->why = 2; }
        return;
    }
    for (int k = 0; k < P_COUNT; k++)
        if (!strcmp(key, PD[k].key)) {
            char *end; float v = strtof(val, &end);
            if (end == val || !isfinite(v)) return;  /* reject "", "nan", "inf" */
            v = clampf(v, PD[k].min, PD[k].max);
            if (PD[k].kind != K_F) v = floorf(v + 0.5f);
            if (k == P_BANK_STATUS || k == P_USER_BANKS || k == P_VERSION || k == P_LAST_CHANGE || k == P_PC_SEEN) return;   /* read-only: the worker owns these */
            if (k == P_BANK_IO) {   /* 1 = write the current bank to the exports folder, 2 = re-read the banks folder (a worker does the file work) */
                const int a = (int)v;
                if (a == 1) { __atomic_store_n(&g_req_bank, preset_cat_of(m->preset), __ATOMIC_RELEASE); __atomic_store_n(&g_status, ST_WORKING, __ATOMIC_RELEASE); __atomic_fetch_or(&g_req, REQ_EXPORT, __ATOMIC_ACQ_REL); }
                else if (a == 2) { __atomic_store_n(&g_status, ST_WORKING, __ATOMIC_RELEASE); __atomic_fetch_or(&g_req, REQ_RELOAD, __ATOMIC_ACQ_REL); }
                return;
            }
            if (k == P_PRESET_CAT) { int f = preset_cat_first((int)v); if (f >= 0) { apply_preset(m, f); m->why = 3; } return; }   /* jump to the first preset of that bank; an empty user bank is ignored */
            m->p[k] = v;
            if (k == P_DRONE && (int)v != 2) unlatch(m);
            if (k == P_STICK_X) m->stick_age[0] = 0;
            if (k == P_STICK_Y) {
                m->stick_age[1] = 0;
                if (v > -0.3f) { m->y_down = 0; m->y_dwell = 0; }   /* re-arms the Hold toggle (see render_block) */
            }
            if (k == P_ARP_MODE || k == P_HOLD || k == P_STICK_Y) sync_modes(m);
            return;
        }
}
static int get_param(void *inst, const char *key, char *buf, int n) {
    mm_t *m = inst;
    if (!m || !key || !buf || n <= 0) return -1;
    if (!strcmp(key, "bank_status")) return snprintf(buf, n, "%d", __atomic_load_n(&g_status, __ATOMIC_ACQUIRE));
    if (!strcmp(key, "user_banks")) return snprintf(buf, n, "%d", user_bank_count());
    if (!strcmp(key, "last_change")) return snprintf(buf, n, "%d", m->why);
    if (!strcmp(key, "pc_seen")) return snprintf(buf, n, "%d", m->pc_seen);
    for (int k = 0; k < P_COUNT; k++)
        if (!strcmp(key, PD[k].key)) {
            if (k >= P_FX1_TYPE && k <= P_FX4_LVL) m->fx_page_t = m->clk + 1;
            return PD[k].kind == K_F ? snprintf(buf, n, "%.4f", m->p[k]) : snprintf(buf, n, "%d", (int)m->p[k]);
        }
    if (!strcmp(key, "name")) return snprintf(buf, (size_t)n, "Mean Mod");
    if (!strcmp(key, "state")) return state_write(m, buf, n);
    if (!strcmp(key, "preset")) return snprintf(buf, n, "%d", m->preset);
    if (!strcmp(key, "preset_count")) return snprintf(buf, n, "%d", N_PRESETS);
    if (!strcmp(key, "preset_name")) { const char *full; return snprintf(buf, n, "%s", preset_label(m->preset, &full)); }
    if (!strcmp(key, "preset_full")) { const char *full; preset_label(m->preset, &full); return snprintf(buf, n, "%s", full); }
    if (!strcmp(key, "bank_preset")) return snprintf(buf, n, "%d", preset_pos_of(m->preset));
    if (!strcmp(key, "bank_count")) return snprintf(buf, n, "%d", bank_size(preset_cat_of(m->preset)));
    const char *src = NULL; int len = 0;
    if (!strcmp(key, "ui_hierarchy")) { src = m->ui_json; len = m->ui_len; }
    else if (!strcmp(key, "chain_params")) { src = m->chain_json; len = m->chain_len; }
    if (!src) return -1;
    if (len >= n) return -1;  /* over-size answers are rejected by the host, never truncate */
    memcpy(buf, src, (size_t)len + 1);
    return len;
}
static int get_error(void *i, char *buf, int n) { (void)i; (void)buf; (void)n; return 0; }

/* Grit / Dark macro maths, shared by the block-rate value and the per-sample Grit matrix target. */
typedef struct { float fdrive, drive, res, noise, drift, cut_oct, makeup; } grit_t;
static grit_t grit_calc(const gbase_t *b, float gi) {
    const float gp = gi > 0.0f ? gi : 0.0f, gn = gi < 0.0f ? -gi : 0.0f;
    grit_t g;
    g.fdrive  = clampf(b->fdrive + 0.9f * gp - 0.4f * gn, 0.0f, 2.0f);   /* beyond 1: extra saturation only Grit can reach */
    g.drive   = clampf(b->drive + 0.9f * gp - 0.3f * gn, 0.0f, 1.6f);
    g.res     = clampf(b->res + 0.2f * gp - 0.1f * gn, 0.0f, 1.0f);
    g.noise   = clampf(b->noise + 0.12f * gp, 0.0f, 1.0f);
    g.drift   = clampf(b->drift + 0.5f * gp - 0.2f * gn, 0.0f, 1.0f);
    g.cut_oct = -2.0f * gp + 1.0f * gn;
    g.makeup  = 1.0f + 0.8f * gn + 0.5f * gp;     /* clean end loses filter-input gain: keep the level */
    return g;
}

static void render_active(mm_t *m, int16_t *out, int frames) {
    const float *p = m->p;

    /* Joystick emulation: X and Y each drive one chosen target; both spring back to centre when the knob is released. */
    const int xt = (int)p[P_STICK_X_TO], yt = (int)p[P_STICK_Y_TO];
    const float xv = p[P_STICK_X], yv = p[P_STICK_Y], yu = yv > 0.0f ? yv : 0.0f;
    const float stick_bend = xt == 0 ? xv * fmaxf(7.0f, 2.0f * p[P_BEND_RANGE]) : 0.0f;   /* a fifth at least, like a real stick throw */
    const float stick_cut_oct = (xt == 2 ? xv * 4.0f : 0.0f) + (yt == 2 ? yu * 4.0f : 0.0f);
    const float grit_in = clampf(p[P_GRIT] + (xt == 3 ? xv : 0.0f) + (yt == 3 ? yu : 0.0f), -1.0f, 1.0f);
    const float g_glide = clampf(p[P_GLIDE] + (yt == 0 ? sqrtf(yu) : 0.0f), 0.0f, 1.0f);   /* sqrt: a small push is already audible */

    /* block-rate constants */
    const int w1 = (int)p[P_OSC1_WAVE], w2 = (int)p[P_OSC2_WAVE];
    const float a1 = coef(env_time(p[P_ENV1_A]), 1.466f), d1 = coef(env_time(p[P_ENV1_D]), 4.0f), r1 = coef(env_time(p[P_ENV1_R]), 4.0f);
    const float a2 = coef(env_time(p[P_ENV2_A]), 1.466f), d2 = coef(env_time(p[P_ENV2_D]), 4.0f), r2 = coef(env_time(p[P_ENV2_R]), 4.0f);
    const float lfo_hz = 0.05f * powf(800.0f, p[P_LFO_RATE]);
    const float lfo_inc = lfo_hz / SR, vib_inc = clampf(3.0f + 5.0f * p[P_VIB_RATE] + (xt == 1 ? xv * 9.0f : 0.0f), 0.5f, 18.0f) / SR;
    const float glide_s = 2.0f * g_glide * g_glide;
    const float glide_c = glide_s > 0.0005f ? 1.0f - expf(-1.0f / (glide_s * SR * 0.25f)) : 1.0f;
    const int at_dest = (int)p[P_AT_DEST];
    const float at = p[P_AT_AMOUNT] * m->at;                    /* aftertouch, scaled */
    const float vib_depth = clampf(p[P_VIB_DEPTH] + 0.5f * m->modwheel + (at_dest == 2 ? at : 0.0f), 0.0f, 1.0f) * 0.6f + (yt == 1 ? yu * 1.5f : 0.0f);  /* semitones; the stick adds up to 1.5 on top */
    const float at_cut_oct = at_dest == 1 ? at * 3.0f : 0.0f;
    const float xmod_eff = clampf(p[P_XMOD] + (at_dest == 4 ? at * 0.5f : 0.0f), 0.0f, 1.0f);
    const float vel_cut_oct = p[P_VEL_CUTOFF] * (m->vel - 0.5f) * 6.0f;
    const float vel_scale = 1.0f - p[P_VEL_AMP] * (1.0f - m->vel);
    const float pitch_off = 12.0f * p[P_OCTAVE] + p[P_TUNE] / 100.0f;
    /* Grit / Dark macro, applied on top of the patch (the stored params are untouched). + = grittier and darker, - = cleaner and brighter. */
    const gbase_t gb_t = {p[P_FILTER_DRIVE], p[P_DRIVE], p[P_RESONANCE], p[P_NOISE_LEVEL], p[P_DRIFT]};
    const grit_t G0 = grit_calc(&gb_t, grit_in);
    const float g_fdrive = G0.fdrive, g_drive = G0.drive, g_res = G0.res, g_noise = G0.noise, g_drift = G0.drift;
    const float drift_amt = g_drift * 0.03f;  /* semitones (3 cents) */
    const float drift_c = 1.0f - expf(-6.2831853f * 0.4f / SR);
    const float k1 = exp2f((0.5f - p[P_OSC1_SHAPE]) * 2.0f), k2 = exp2f((0.5f - p[P_OSC2_SHAPE]) * 2.0f);
    const float o2_off = (p[P_OSC2_COARSE] + p[P_OSC2_FINE] / 100.0f);
    const float o1_off = (p[P_OSC1_COARSE] + p[P_OSC1_FINE] / 100.0f);
    const float noise_lvl = g_noise * 0.5f * 1.4142f /* keep in-band level after OS */, lv1 = p[P_OSC1_LEVEL], lv2 = p[P_OSC2_LEVEL];
    const float vol = p[P_VOLUME] * 0.8f * m->expr;
    const int drone_mode = (int)p[P_DRONE];
    const int lw1 = (int)p[P_LFO_WAVE], lw2 = (int)p[P_LFO2_WAVE], ls1 = (int)p[P_LFO_SYNC] & 7, ls2 = (int)p[P_LFO2_SYNC] & 7;
    const float sync_hz = m->blk_bpm / 60.0f;
    const float lfo1_sync_inc = ls1 ? sync_hz / SYNC_BEATS[ls1] / SR : 0.0f, lfo2_sync_inc = ls2 ? sync_hz / SYNC_BEATS[ls2] / SR : 0.0f;
    const float lfo2_base = p[P_LFO2_RATE];
    const float s1 = p[P_ENV1_S], s2 = drone_mode ? 1.0f : p[P_ENV2_S];  /* drone: amp holds at full while the note is down */
    const float res_K = g_res * 3.6f;                 /* ~2 is linear self-oscillation */
    const float ig = 0.25f * exp2f(4.0f * g_fdrive), inv_sqrt_ig = 1.0f / sqrtf(ig);
    const int fmode = (int)p[P_FILTER_MODE];
    const float drv = clampf(g_drive + (at_dest == 3 ? at * 0.6f : 0.0f), 0.0f, 1.6f), drv_g = 1.0f + 5.0f * drv + 6.0f * drv * drv, drv_b = 0.15f * drv, drv_bias_out = tanhf(drv_b);
    const float drv_norm = 1.0f / sqrtf(drv_g);

    /* Mod matrix: active slots only (source set, target set, amount non-zero). Sources are sampled per sample, destinations summed. */
    int mx_n = 0, mx_s[N_SLOTS], mx_d[N_SLOTS], mx_v[N_SLOTS], has_d[N_MXDST] = {0};
    float mx_a[N_SLOTS];
    for (int k = 0; k < N_SLOTS; k++) {
        int s = (int)p[P_M1_SRC + 3 * k], d = (int)p[P_M1_DST + 3 * k]; float a = p[P_M1_AMT + 3 * k];
        if (s > 0 && s < N_MXSRC && d > 0 && d < N_MXDST && a != 0.0f) { const int vi = (int)p[P_M1_VIA + k]; mx_s[mx_n] = s; mx_d[mx_n] = d; mx_a[mx_n] = a; mx_v[mx_n] = (vi > 0 && vi < N_MXSRC) ? vi : 0; mx_n++; has_d[d] = 1; }
    }
    const float lfo_base = p[P_LFO_RATE];
    const float fx_att = 1.0f - expf(-1.0f / (0.015f * SR)), fx_rel = 1.0f - expf(-1.0f / (0.05f * SR));
    const float chop_inc = m->blk_bpm / 60.0f * 4.0f / SR, wob_inc = m->blk_bpm / 60.0f * 2.0f / SR;
    int fx_any = 0; for (int i = 1; i < N_FXT; i++) if (m->fx_tgt[i] != 0.0f || m->fxa[i] != 0.0f) fx_any = 1;
    const float *fa = m->fxa;
    const float ncol_a = 1.0f - 0.93f * p[P_NOISE_COLOR], ncol_g = powf((2.0f - ncol_a) / ncol_a, 0.25f);   /* half the level compensation: perceived loudness stays within ~2 dB */
    const float sm_c = 1.0f - expf(-1.0f / (0.012f * SR));            /* ~12 ms: smooths a knob stepping at the UI rate, not the modulation */
    const float cut_tgt = p[P_CUTOFF] * 9.8138f + stick_cut_oct + at_cut_oct;
    if (!m->sm_init) { m->sm_gb = gb_t; m->sm_grit = grit_in; m->sm_cut = cut_tgt; m->sm_vol = vol; m->sm_init = 1; }
    m->sm_gb.noise = gb_t.noise; m->sm_gb.drift = gb_t.drift;
    const float age_inc = 1.0f / (5.0f * SR), srnd_c = 1.0f - expf(-6.2831853f * 1.0f / SR), ped_c = 1.0f - expf(-1.0f / (0.005f * SR));

    for (int i = 0; i < frames; i++) {
        /* control signals */
        float lfo = lfo_shape(m->lfo_ph, lw1, m->lfo_rv[0]);
        float lfo2 = lfo_shape(m->lfo2_ph, lw2, m->lfo_rv[1]);
        if (has_d[21]) lfo *= clampf(m->mx_ld[0], 0.0f, 2.0f);      /* LFO Depth: the slot total scales the LFO (0 = silent) */
        if (has_d[22]) lfo2 *= clampf(m->mx_ld[1], 0.0f, 2.0f);
        m->lfo_ph += ls1 ? lfo1_sync_inc : (has_d[14] ? 0.05f * exp2f(9.6439f * clampf(lfo_base + m->mx_lfr, 0.0f, 1.3f)) / SR : lfo_inc);
        if (m->lfo_ph >= 1.0f) { m->lfo_ph -= 1.0f; m->lfo_rv[0] = frand(&m->rng); }
        m->lfo2_ph += ls2 ? lfo2_sync_inc : 0.05f * exp2f(9.6439f * clampf(lfo2_base + (has_d[15] ? m->mx_lr2 : 0.0f), 0.0f, 1.3f)) / SR;
        if (m->lfo2_ph >= 1.0f) { m->lfo2_ph -= 1.0f; m->lfo_rv[1] = frand(&m->rng); }
        m->vib_ph += vib_inc; if (m->vib_ph >= 1.0f) m->vib_ph -= 1.0f;
        const float vib_tri = (m->vib_ph < 0.5f ? 4.0f * m->vib_ph - 1.0f : 3.0f - 4.0f * m->vib_ph);
        float e1 = env_step(&m->e1, a1, d1, s1, r1);
        float e2 = env_step(&m->e2, a2, d2, s2, r2);

        m->age += age_inc; if (m->age > 1.0f) m->age = 1.0f;
        if (fx_any) for (int i = 1; i < N_FXT; i++) {            /* FX bank amounts slew toward the touch targets */
            float tg = m->fx_tgt[i], cur = m->fxa[i];
            if (tg != cur) { cur += (tg - cur) * (tg > cur ? fx_att : fx_rel); if (fabsf(tg - cur) < 1e-4f) cur = tg; m->fxa[i] = cur; }
        }
        m->sm_cut += (cut_tgt - m->sm_cut) * sm_c; m->sm_vol += (vol - m->sm_vol) * sm_c; m->sm_grit += (grit_in - m->sm_grit) * sm_c;
        m->sm_gb.fdrive += (gb_t.fdrive - m->sm_gb.fdrive) * sm_c; m->sm_gb.drive += (gb_t.drive - m->sm_gb.drive) * sm_c; m->sm_gb.res += (gb_t.res - m->sm_gb.res) * sm_c;
        const int moving = fabsf(grit_in - m->sm_grit) > 1e-4f || fabsf(gb_t.fdrive - m->sm_gb.fdrive) > 1e-4f || fabsf(gb_t.drive - m->sm_gb.drive) > 1e-4f || fabsf(gb_t.res - m->sm_gb.res) > 1e-4f;
        m->chop_ph += chop_inc; if (m->chop_ph >= 1.0f) m->chop_ph -= 1.0f;
        m->wob_ph += wob_inc; if (m->wob_ph >= 1.0f) m->wob_ph -= 1.0f;
        const float wob_tri = m->wob_ph < 0.5f ? 4.0f * m->wob_ph - 1.0f : 3.0f - 4.0f * m->wob_ph;
        float mod[N_MXDST] = {0};
        if (mx_n) {
            float sv[N_MXSRC];
            sv[1] = lfo; sv[2] = e1; sv[3] = e2; sv[4] = m->vel; sv[5] = m->modwheel; sv[6] = m->at; sv[7] = xv; sv[8] = yv;
            sv[9] = clampf((m->cur_semi - 60.0f) * (1.0f / 24.0f), -1.0f, 1.0f); sv[10] = m->rnd; sv[11] = lfo2; sv[12] = frand(&m->rng); sv[13] = m->o2_prev; sv[0] = 0.0f;
            if (--m->srnd_cnt <= 0) { m->srnd_cnt = (int)(0.4f * SR); m->srnd_tgt = frand(&m->rng); }
            m->srnd1 += (m->srnd_tgt - m->srnd1) * srnd_c;      /* two cascaded slews: no corners */
            m->srnd += (m->srnd1 - m->srnd) * srnd_c;
            m->ped += ((m->sustain ? 1.0f : 0.0f) - m->ped) * ped_c;
            sv[14] = m->bend_n; sv[15] = m->arp_ramp; sv[16] = m->age; sv[17] = m->srnd; sv[18] = m->o1_prev; sv[19] = m->ped;
            for (int k = 0; k < mx_n; k++) mod[mx_d[k]] += sv[mx_s[k]] * mx_a[k] * (mx_v[k] ? sv[mx_v[k]] : 1.0f);   /* Via: a second source scales the amount */
            m->mx_lfr = mod[14]; m->mx_lr2 = mod[15]; m->mx_ld[0] = mod[21]; m->mx_ld[1] = mod[22];
        }
        const int gm = has_d[18] || fa[FXT_GRIT] != 0.0f || moving;                                    /* Grit target: the macro is recomputed per sample */
        grit_t gs = G0; float drv_i = drv;
        if (gm) { gs = grit_calc(&m->sm_gb, clampf(m->sm_grit + mod[18] + fa[FXT_GRIT], -1.0f, 1.0f)); drv_i = clampf(gs.drive + (at_dest == 3 ? at * 0.6f : 0.0f), 0.0f, 1.6f); }
        const float drift_i = (gm || has_d[24]) ? clampf(gs.drift + mod[24], 0.0f, 1.0f) * 0.03f : drift_amt;
        float gc = glide_c;
        if (has_d[19]) { float gg = clampf(g_glide + mod[19], 0.0f, 1.0f), gsec = 2.0f * gg * gg; gc = gsec > 0.0005f ? 1.0f - expf(-1.0f / (gsec * SR * 0.25f)) : 1.0f; }
        m->cur_semi += (m->tgt_semi - m->cur_semi) * gc;
        const float vib = vib_tri * fmaxf(vib_depth + 1.5f * mod[20] + 2.0f * fa[FXT_VIB], 0.0f);
        m->o1.drift += (frand(&m->rng) - m->o1.drift) * drift_c * 0.02f;  /* slow wander */
        m->o2.drift += (frand(&m->rng) - m->o2.drift) * drift_c * 0.02f;
        float semi = m->cur_semi + pitch_off + m->bend + stick_bend + vib + 12.0f * mod[1] + 12.0f * (fa[FXT_RISE] - fa[FXT_DIVE]);

        float cut_oct = m->sm_cut + gs.cut_oct + p[P_FLT_FM1] * e1 * 5.0f + p[P_FLT_FM2] * lfo * 3.0f
                      + p[P_KEY_TRACK] * (m->cur_semi + pitch_off + m->bend - 60.0f) / 12.0f + vel_cut_oct + 5.0f * mod[8] + 5.0f * e1 * mod[23]
                      + 4.0f * fa[FXT_OPEN] - 5.0f * fa[FXT_CLOSE] + 3.0f * fa[FXT_WOB] * wob_tri;
        float fc = clampf(20.0f * exp2f(cut_oct), 15.0f, 19000.0f);
        float g = tanf(3.14159265f * fc / (SR * OS)), G = g / (1.0f + g);

        /* oscillator core at OS x the sample rate; halfband-decimated afterwards */
        float amp = e2 * vel_scale;
        float amp_mx = has_d[12] ? clampf(1.0f + mod[12], 0.0f, 2.0f) : 1.0f; amp *= amp_mx;
        if (fa[FXT_CHOP] != 0.0f || m->chop_g != 1.0f) {            /* Gate Chop: tempo-locked 1/16 gate, depth = level, smoothed so it never clicks */
            float tgc = m->chop_ph < 0.5f ? 1.0f : 1.0f - fa[FXT_CHOP];
            m->chop_g += (tgc - m->chop_g) * 0.03f; if (fabsf(1.0f - m->chop_g) < 1e-4f && fa[FXT_CHOP] == 0.0f) m->chop_g = 1.0f;
            amp *= m->chop_g;
        }
        const float lv1e = has_d[5] ? clampf(lv1 + mod[5], 0.0f, 1.5f) : lv1, lv2e = has_d[6] ? clampf(lv2 + mod[6], 0.0f, 1.5f) : lv2;
        const float nle = (has_d[7] || gm || fa[FXT_NOISE] != 0.0f) ? clampf(gs.noise + mod[7] + 0.6f * fa[FXT_NOISE], 0.0f, 1.0f) * 0.5f * 1.4142f : noise_lvl;
        const float xme = has_d[13] ? clampf(xmod_eff + mod[13], 0.0f, 1.0f) : xmod_eff;
        const float resK = (has_d[9] || gm || fa[FXT_RES] != 0.0f) ? clampf(gs.res + mod[9] + 0.8f * fa[FXT_RES], 0.0f, 1.0f) * 3.6f : res_K;
        float igi = ig, isqi = inv_sqrt_ig;
        if (has_d[10] || gm) { igi = 0.25f * exp2f(4.0f * clampf(gs.fdrive + mod[10], 0.0f, 2.0f)); isqi = 1.0f / sqrtf(igi); }
        float dg = drv_g, db = drv_b, dbo = drv_bias_out, dn = drv_norm;
        if (has_d[11] || gm || fa[FXT_CRUNCH] != 0.0f) {
            float dv = clampf(drv_i + mod[11] + fa[FXT_CRUNCH], 0.0f, 1.6f);
            dg = 1.0f + 5.0f * dv + 6.0f * dv * dv; db = 0.15f * dv; dbo = tanhf(db); dn = 1.0f / sqrtf(dg);
        }
        const float o2o = o2_off + 12.0f * mod[2] + fa[FXT_DETUNE];
        for (int sub = 0; sub < OS; sub++) {
            float fm2_oct = p[P_OSC2_FM1] * e1 * 3.0f + p[P_OSC2_FM2] * lfo * 1.0f + 2.0f * mod[17];
            float f2 = 440.0f * exp2f((semi + o2o + m->o2.drift * drift_i * 8.0f - 69.0f) / 12.0f + fm2_oct);
            float dt2 = clampf(f2 / (SR * OS), 0.0f, 0.45f);
            float sh2 = clampf(p[P_OSC2_SHAPE] + p[P_SHAPE_MOD] * lfo * 0.5f + 0.5f * mod[4], 0.0f, 1.0f);
            float y2 = osc_wave(m->o2.phase, dt2, w2, sh2, k2);
            m->o2.phase += dt2; if (m->o2.phase >= 1.0f) m->o2.phase -= 1.0f;
            m->o2_prev = y2;

            float fm1_oct = p[P_OSC1_FM1] * e1 * 3.0f + p[P_OSC1_FM2] * lfo * 1.0f + xme * y2 * 2.0f + 2.0f * mod[16];
            float f1 = 440.0f * exp2f((semi + o1_off + m->o1.drift * drift_i * 8.0f - 69.0f) / 12.0f + fm1_oct);
            float dt1 = clampf(f1 / (SR * OS), 0.0f, 0.45f);
            float sh1 = clampf(p[P_OSC1_SHAPE] + p[P_SHAPE_MOD] * lfo * 0.5f + 0.5f * mod[3], 0.0f, 1.0f);
            float y1 = osc_wave(m->o1.phase, dt1, w1, sh1, k1);
            m->o1.phase += dt1; if (m->o1.phase >= 1.0f) m->o1.phase -= 1.0f;
            m->o1_prev = y1;


            float white = frand(&m->rng);
            m->noise_lp1 += ncol_a * (white - m->noise_lp1);                 /* Noise Color: one-pole low-pass, level-compensated (0 = white, exact) */
            float noise = ncol_a >= 0.999f ? white : m->noise_lp1 * ncol_g;
            float mix = y1 * lv1e + y2 * lv2e + noise * nle;
            float fl = flt_tick(m, mix, G, resK, igi, isqi, fmode, (resK > 1.8f ? white * 1e-4f * (resK - 1.8f) : 0.0f));   /* a kick only where resonance can sustain self-oscillation */
            float va = fl * amp;                                       /* VCA */
            va = (tanhf(dg * va + db) - dbo) * dn; /* transistor-ish asymmetric drive */
            m->hb[m->hb_pos] = va * m->sm_vol * gs.makeup;
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
            m->sm_init = 0;
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
            if (++m->stick_age[k] > (m->touch_seen ? 150 : 120)) { m->p[pi] = 0.0f; m->stick_age[k] = 0; if (k) { m->y_down = 0; m->y_dwell = 0; } }
        } else m->stick_age[k] = 0;
    }
    /* Flick-to-step stick targets (Arp Rate / Arp Mode): one step each time the axis is pushed past 0.8, re-armed below 0.4. */
    for (int k = 0; k < 2; k++) {
        int tgt = (int)m->p[k ? P_STICK_Y_TO : P_STICK_X_TO], kind = k ? (tgt == 5 ? 1 : (tgt == 6 ? 2 : 0)) : (tgt == 4 ? 1 : (tgt == 5 ? 2 : 0));
        float v = m->p[k ? P_STICK_Y : P_STICK_X];
        if (!kind) { m->stk_lock[k] = 0; continue; }
        if (m->stk_lock[k] && fabsf(v) < 0.4f) m->stk_lock[k] = 0;
        else if (!m->stk_lock[k] && fabsf(v) >= 0.8f) {
            m->stk_lock[k] = 1;
            int dir = v > 0 ? 1 : -1;
            if (kind == 1) m->p[P_ARP_RATE] = (float)(((int)m->p[P_ARP_RATE] + dir < 0) ? 0 : (((int)m->p[P_ARP_RATE] + dir > 5) ? 5 : (int)m->p[P_ARP_RATE] + dir));
            else m->p[P_ARP_MODE] = (float)(((int)m->p[P_ARP_MODE] + dir + 6) % 6);
            sync_modes(m);
        }
    }
    /* FX bank: each of knobs 1-4 applies its effect at its level while it is touched (after the tap delay, never for a double tap). */
    m->clk++;
    int hold_act = 0;
    {
        float tg[N_FXT] = {0};
        for (int k = 0; k < 4; k++) {
            int ty = (int)m->p[P_FX1_TYPE + k];
            if (ty > 0 && ty < N_FXT && ((m->touch_mask >> k) & 1) && ((m->fx_armed >> k) & 1) && !((m->fx_sup >> k) & 1) && m->clk - m->touch_t[k] >= FX_DELAY_BLK) {
                tg[ty] += m->p[P_FX1_LVL + k];
                if (ty == FXT_HOLD) hold_act = 1;
            }
        }
        for (int i = 1; i < N_FXT; i++) m->fx_tgt[i] = tg[i] > 1.0f ? 1.0f : tg[i];
        if (hold_act && !m->fx_hold_on) { m->fx_hold_saved = (int)m->p[P_HOLD]; m->p[P_HOLD] = 1.0f; m->fx_hold_on = 1; sync_modes(m); }
        else if (!hold_act && m->fx_hold_on) { m->p[P_HOLD] = (float)m->fx_hold_saved; m->fx_hold_on = 0; sync_modes(m); }
    }
    /* With Y assigned to "Hold Toggle", stick down toggles Hold, but only when Y is pushed all the way down and kept there (~0.23 s):
     * sweeping the knob through the lower half, or a quick wiggle, must never flip Hold. */
    if ((int)m->p[P_STICK_Y_TO] == 4 && m->p[P_STICK_Y] <= -0.999f && !m->y_down) {
        if (++m->y_dwell >= 80) { m->y_down = 1; m->y_dwell = 0; m->p[P_HOLD] = ((int)m->p[P_HOLD] == 1) ? 0.0f : 1.0f; sync_modes(m); }
    } else if (m->p[P_STICK_Y] > -0.999f) m->y_dwell = 0;

#if defined(__aarch64__)
    unsigned long fpcr_old = fpcr_read();
    fpcr_write(fpcr_old | (1UL << 24));   /* FPCR.FZ */
#endif
    static const float STEPS_PER_BEAT[6] = {1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 8.0f};
    float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    if (!(bpm >= 30.0f && bpm <= 300.0f)) bpm = 120.0f;
    const int arp_ri = ((int)m->p[P_ARP_RATE] + (int)(m->fx_tgt[FXT_ARP] * 3.0f + 0.5f)) > 5 ? 5 : ((int)m->p[P_ARP_RATE] + (int)(m->fx_tgt[FXT_ARP] * 3.0f + 0.5f));
    const float step_len = SR * 60.0f / bpm / STEPS_PER_BEAT[arp_ri % 6];
    const float gate_len = step_len * m->p[P_ARP_GATE];
    const int tie = m->p[P_ARP_GATE] >= 0.999f;

    /* Transport sync: when Move's transport runs, steps sit on the beat grid (position inside the current step = fractional beat * steps/beat).
     * The first note after a key press still sounds at once; later steps then fall on the grid. Stopped transport -> free-running. */
    double beat = (g_host && g_host->get_beat_position) ? g_host->get_beat_position() : -1.0;
    float grid_pos = -1.0f;   /* samples since the last grid step boundary at the start of this block, or -1 if unsynced */
    m->blk_bpm = bpm; m->blk_beat = beat;
    if (beat >= 0.0 && beat < 1e9) {   /* synced LFOs lock to the beat grid too */
        int s1 = (int)m->p[P_LFO_SYNC] & 7, s2 = (int)m->p[P_LFO2_SYNC] & 7;
        if (s1) { double c = beat / (double)SYNC_BEATS[s1]; m->lfo_ph = (float)(c - floor(c)); }
        if (s2) { double c = beat / (double)SYNC_BEATS[s2]; m->lfo2_ph = (float)(c - floor(c)); }
        { double c4 = beat * 4.0, c2 = beat * 2.0; m->chop_ph = (float)(c4 - floor(c4)); m->wob_ph = (float)(c2 - floor(c2)); }   /* Gate Chop 1/16, Wobble 1/8 */
        double s = beat * (double)STEPS_PER_BEAT[arp_ri % 6];
        grid_pos = (float)((s - floor(s)) * (double)step_len);
    }
    if (grid_pos >= 0.0f && m->arp_running) {
        int pend = m->arp_pos >= step_len;
        float cur = pend ? m->arp_pos - step_len : m->arp_pos, diff = grid_pos - cur;
        if (diff > step_len * 0.5f) diff -= step_len; else if (diff < -step_len * 0.5f) diff += step_len;
        if (diff > 1.5f || diff < -1.5f) {
            float np = cur + diff;
            if (np < 0.0f) np += step_len;
            m->arp_pos = np + (pend ? step_len : 0.0f);
        }
    }

    int remaining = frames;
    int16_t *ptr = out;
    while (remaining > 0) {
        int n = remaining;
        if ((int)m->p[P_ARP_MODE] == 0) {
            if (m->arp_running) arp_stop(m);
        } else {
            if (m->held_n > 0 && !m->arp_running) {
                m->arp_running = 1; m->arp_idx = 0;
                m->arp_pos = step_len + (grid_pos >= 0.0f ? grid_pos : 0.0f);   /* step now; afterwards continue from the grid phase */
            }
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
    banks_init();
    bank_worker_start();
    memset(&g_api, 0, sizeof g_api);
    g_api.api_version = MOVE_PLUGIN_API_VERSION_2;
    g_api.create_instance = create_instance; g_api.destroy_instance = destroy_instance;
    g_api.on_midi = on_midi; g_api.set_param = set_param; g_api.get_param = get_param;
    g_api.get_error = get_error; g_api.render_block = render_block;
    return &g_api;
}
