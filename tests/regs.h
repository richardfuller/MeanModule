/* Home register of every preset, shared by register.c and health.c. Note names follow Move and Live: MIDI 60 = C3. */
#include <string.h>
#define NNOTES 5
/* home register of a preset: five notes, low to high */
static const int REG_BASS[NNOTES] = {36, 43, 48, 55, 60}, REG_LEAD[NNOTES] = {60, 67, 72, 79, 84}, REG_MID[NNOTES] = {48, 55, 60, 67, 72};
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

