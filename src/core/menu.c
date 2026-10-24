/* menu.c - rows of the Game settings and Flow settings panels. */
#include "menu.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

int menu_rows(int panel, int plat, RowKind *out, int max)
{
    RowKind tmp[MENU_MAX_ROWS];
    int n = 0, i;
    if (panel == PANEL_GAME) {
        tmp[n++] = ROW_G_AUTOPLAY;
        if (plat == PLAT_PS1) { tmp[n++] = ROW_G_EMULATOR; }
        else if (plat == PLAT_PS2) {
            tmp[n++] = ROW_G_EMULATOR; tmp[n++] = ROW_G_OFFLINE; tmp[n++] = ROW_G_PS2CFG;
        } else { tmp[n++] = ROW_G_OFFLINE; }
    } else if (panel == PANEL_FLOW) {
        /* 0.7.0: grouped; Quit and Power Off stay on the first page */
        tmp[n++] = ROW_FG_LOOK; tmp[n++] = ROW_FG_SOUND; tmp[n++] = ROW_FG_GAMES; tmp[n++] = ROW_FG_SYSTEM;
        tmp[n++] = ROW_F_ABOUT; tmp[n++] = ROW_F_QUIT; tmp[n++] = ROW_F_POWEROFF;
    }
    if (n > max) n = max;
    for (i = 0; i < n; i++) out[i] = tmp[i];
    return n;
}

static const RowKind k_g_look[] = { ROW_F_BACKGROUND, ROW_F_UI_COLOR, ROW_F_INFO, ROW_F_CASES, ROW_F_REFLECT };
static const RowKind k_g_sound[] = { ROW_F_SFX, ROW_F_VOLUME, ROW_F_SHUFFLE };   /* 0.8.2: Sound Effects first */
static const RowKind k_g_games[] = { ROW_F_AUTOPLAY, ROW_F_OFFLINE, ROW_F_IGNORE_THE, ROW_F_COVERS, ROW_F_RESCAN };
static const RowKind k_g_system[] = { ROW_F_SLEEP, ROW_F_XMB_TOP };

static const RowKind *group_list(RowKind g, int *n)
{
    switch (g) {
    case ROW_FG_LOOK:   *n = (int)(sizeof(k_g_look) / sizeof(k_g_look[0])); return k_g_look;
    case ROW_FG_SOUND:  *n = (int)(sizeof(k_g_sound) / sizeof(k_g_sound[0])); return k_g_sound;
    case ROW_FG_GAMES:  *n = (int)(sizeof(k_g_games) / sizeof(k_g_games[0])); return k_g_games;
    case ROW_FG_SYSTEM: *n = (int)(sizeof(k_g_system) / sizeof(k_g_system[0])); return k_g_system;
    default: *n = 0; return NULL;
    }
}

int menu_group_rows(RowKind group, RowKind *out, int max)
{
    int n, i;
    const RowKind *l = group_list(group, &n);
    if (n > max) n = max;
    for (i = 0; i < n; i++) out[i] = l[i];
    return n;
}

int menu_is_group(RowKind k) { return k == ROW_FG_LOOK || k == ROW_FG_SOUND || k == ROW_FG_GAMES || k == ROW_FG_SYSTEM; }

int menu_group_of(RowKind k)
{
    static const RowKind gs[4] = { ROW_FG_LOOK, ROW_FG_SOUND, ROW_FG_GAMES, ROW_FG_SYSTEM };
    int g, i, n;
    for (g = 0; g < 4; g++) {
        const RowKind *l = group_list(gs[g], &n);
        for (i = 0; i < n; i++) if (l[i] == k) return (int)gs[g];
    }
    return -1;
}

const char *menu_label(RowKind k)
{
    switch (k) {
    case ROW_FG_LOOK: return "Appearance";
    case ROW_FG_SOUND: return "Music & Sound";
    case ROW_FG_GAMES: return "Games & Library";
    case ROW_FG_SYSTEM: return "System";
    case ROW_G_AUTOPLAY: return "Auto-Play";
    case ROW_G_EMULATOR: return "Emulator";
    case ROW_G_OFFLINE: return "Offline Mode";
    case ROW_G_PS2CFG: return "PS2 Config";
    case ROW_F_AUTOPLAY: return "Auto-Play";
    case ROW_F_OFFLINE: return "Offline Mode";
    case ROW_F_SFX: return "Sound Effects";
    case ROW_F_VOLUME: return "Music Volume";
    case ROW_F_SHUFFLE: return "Shuffle Music";
    case ROW_F_BACKGROUND: return "Background";
    case ROW_F_UI_COLOR: return "UI Color";
    case ROW_D_DISC: return "Disc";
    case ROW_F_INFO: return "Show Game Info";
    case ROW_F_CASES: return "Custom Cases";
    case ROW_F_REFLECT: return "Reflections";
    case ROW_F_SLEEP: return "Sleep Mode";
    case ROW_F_COVERS: return "Download Missing Covers";
    case ROW_F_IGNORE_THE: return "Ignore \"The\" When Sorting";
    case ROW_F_XMB_TOP: return "Pin to Top of XMB";
    case ROW_F_RESCAN: return "Rescan Library";
    case ROW_F_ABOUT: return "About";
    case ROW_F_QUIT: return "Quit to XMB";
    case ROW_F_POWEROFF: return "Power Off PS3";
    }
    return "";
}

int menu_is_action(RowKind k)
{
    return k == ROW_D_DISC || menu_is_group(k) || k == ROW_F_COVERS || k == ROW_F_RESCAN || k == ROW_F_QUIT || k == ROW_F_POWEROFF || k == ROW_F_ABOUT;
}

int menu_is_slider(RowKind k) { return k == ROW_F_VOLUME || k == ROW_F_SFX; }

int menu_toggle(RowKind k, const Settings *s)
{
    switch (k) {
    case ROW_F_AUTOPLAY: return s->autoplay != 0;
    case ROW_F_OFFLINE: return s->offline != 0;
    case ROW_F_SHUFFLE: return s->shuffle != 0;
    case ROW_F_INFO: return s->show_info != 0;
    case ROW_F_CASES: return s->cases != 0;
    case ROW_F_REFLECT: return s->reflections != 0;
    case ROW_F_IGNORE_THE: return s->ignore_the != 0;
    case ROW_F_XMB_TOP: return s->xmb_top != 0;
    default: return -1;
    }
}

int menu_segments(RowKind k, int plat, const GameCfg *g, const char *const **names, int *cur)
{
    static const char *const tri[3] = { "Default", "On", "Off" };        /* AP_DEFAULT, AP_ON, AP_OFF */
    static const char *const emu1[2] = { "Standard", "Netemu" }, *const emu2[2] = { "Default", "Netemu" };
    switch (k) {
    case ROW_G_AUTOPLAY: *names = tri; *cur = sf_clampi(g->autoplay, 0, 2); return 3;
    case ROW_G_OFFLINE: *names = tri; *cur = sf_clampi(g->offline, 0, 2); return 3;
    case ROW_G_EMULATOR: *names = plat == PLAT_PS1 ? emu1 : emu2; *cur = g->emulator ? 1 : 0; return 2;
    default: return 0;
    }
}

static const char *onoff(int v) { return v ? "On" : "Off"; }

void menu_value_text(RowKind k, int plat, const Settings *s, const GameCfg *g, char *out, size_t cap)
{
    out[0] = '\0';
    switch (k) {
    case ROW_D_DISC: break;          /* drawn by ui.c from the disc list */
    case ROW_G_AUTOPLAY:
        if (g->autoplay == AP_ON) sf_strlcpy(out, "On", cap);
        else if (g->autoplay == AP_OFF) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "Default (%s)", onoff(s->autoplay));
        break;
    case ROW_G_EMULATOR:
        if (plat == PLAT_PS1) sf_strlcpy(out, g->emulator ? "Netemu" : "Standard", cap);
        else sf_strlcpy(out, g->emulator ? "Netemu" : "Default", cap);
        break;
    case ROW_G_OFFLINE:
        if (g->offline == AP_ON) sf_strlcpy(out, "On", cap);
        else if (g->offline == AP_OFF) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "Default (%s)", onoff(s->offline));
        break;
    case ROW_G_PS2CFG: {   /* the app shows more (what Auto picks, what exists): app_row_value */
        static const char *const n[SET_PS2CFG_MAX + 1] = { "Auto", "CUSTOM", "NET", "GX", "SOFT", "PS2 Config app" };
        sf_strlcpy(out, n[sf_clampi(g->ps2cfg, 0, SET_PS2CFG_MAX)], cap);
        break;
    }
    /* groups (0.7.3): no summary, just the name, centered */
    case ROW_FG_LOOK: case ROW_FG_SOUND: case ROW_FG_GAMES: case ROW_FG_SYSTEM: out[0] = '\0'; break;
    case ROW_F_AUTOPLAY: sf_strlcpy(out, onoff(s->autoplay), cap); break;
    case ROW_F_OFFLINE: sf_strlcpy(out, onoff(s->offline), cap); break;
    case ROW_F_SFX:
        if (s->sfx_volume == 0) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "%d%%", s->sfx_volume);
        break;
    case ROW_F_VOLUME:      /* 0.8.5: 0% reads Off */
        if (s->music_volume == 0) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "%d%%", s->music_volume);
        break;
    case ROW_F_SHUFFLE: sf_strlcpy(out, onoff(s->shuffle), cap); break;
    case ROW_F_BACKGROUND: sf_strlcpy(out, settings_background_name(s->background), cap); break;
    case ROW_F_UI_COLOR: sf_strlcpy(out, settings_color_name(s->ui_color), cap); break;
    case ROW_F_INFO: sf_strlcpy(out, onoff(s->show_info), cap); break;
    case ROW_F_CASES: sf_strlcpy(out, onoff(s->cases), cap); break;
    case ROW_F_REFLECT: sf_strlcpy(out, onoff(s->reflections), cap); break;
    case ROW_F_SLEEP:
        if (s->sleep_min <= 0) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "After %d min", s->sleep_min);
        break;
    case ROW_F_COVERS: sf_strlcpy(out, "Start", cap); break;
    case ROW_F_IGNORE_THE: sf_strlcpy(out, onoff(s->ignore_the), cap); break;
    case ROW_F_XMB_TOP: sf_strlcpy(out, onoff(s->xmb_top), cap); break;
    case ROW_F_RESCAN: sf_strlcpy(out, "Start", cap); break;
    case ROW_F_ABOUT: out[0] = '\0'; break;
    case ROW_F_QUIT: sf_strlcpy(out, "", cap); break;
    case ROW_F_POWEROFF: sf_strlcpy(out, "", cap); break;
    }
}

/* <- / ->: one step, stopping at the first and last choice (0.8.2); dir 0
 * (only used for actions now) steps forward with wrap */
static int cycle(int v, int n, int dir)
{
    if (dir == 0) return (v + 1) % n;
    return sf_clampi(v + dir, 0, n - 1);
}

static int menu_change_raw(RowKind k, int plat, int dir, Settings *s, GameCfg *g);

int menu_change(RowKind k, int plat, int dir, Settings *s, GameCfg *g)
{
    char before[64], after[64];
    int fx;
    menu_value_text(k, plat, s, g, before, sizeof(before));
    fx = menu_change_raw(k, plat, dir, s, g);
    if (fx == MENU_FX_CHANGED || fx == MENU_FX_RESORT || fx == MENU_FX_XMB_TOP) {
        menu_value_text(k, plat, s, g, after, sizeof(after));
        if (strcmp(before, after) == 0) return MENU_FX_NONE;   /* at the end already: nothing changes */
    }
    return fx;
}

static int menu_change_raw(RowKind k, int plat, int dir, Settings *s, GameCfg *g)
{
    (void)plat;
    switch (k) {
    case ROW_D_DISC: return MENU_FX_NONE;   /* handled by the app (launch that disc) */
    case ROW_G_AUTOPLAY: g->autoplay = (signed char)cycle(g->autoplay, 3, dir); return MENU_FX_CHANGED;
    case ROW_G_EMULATOR: g->emulator = (signed char)cycle(g->emulator, 2, dir); return MENU_FX_CHANGED;
    case ROW_G_OFFLINE: g->offline = (signed char)cycle(g->offline, 3, dir); return MENU_FX_CHANGED;
    case ROW_G_PS2CFG: return MENU_FX_PS2CFG;   /* the app cycles through the configs that exist */
    case ROW_FG_LOOK: case ROW_FG_SOUND: case ROW_FG_GAMES: case ROW_FG_SYSTEM:
        return dir == 0 ? MENU_FX_GROUP : MENU_FX_NONE;
    case ROW_F_AUTOPLAY: s->autoplay = cycle(s->autoplay, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_OFFLINE: s->offline = cycle(s->offline, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_SFX: {
        int v = s->sfx_volume;
        if (dir == 0) v = v >= 100 ? 0 : v + 10;
        else v += dir * 5;
        v = sf_clampi(v, 0, 100);
        if (v == s->sfx_volume) return MENU_FX_NONE;
        s->sfx_volume = v;
        return MENU_FX_CHANGED;
    }
    case ROW_F_VOLUME: {
        int v = s->music_volume;
        if (dir == 0) v = v >= 100 ? 0 : v + 10;
        else v += dir * 5;
        v = sf_clampi(v, 0, 100);
        if (v == s->music_volume) return MENU_FX_NONE;
        s->music_volume = v;
        return MENU_FX_CHANGED;
    }
    case ROW_F_SHUFFLE: s->shuffle = cycle(s->shuffle, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_BACKGROUND: {
        /* menu order */
        static const int order[] = { BG_STAGE, BG_ANIMATED, BG_AURORA, BG_FIREFLIES, BG_RIBBONS, BG_HALO, BG_TWILIGHT };
        const int n = (int)(sizeof(order) / sizeof(order[0]));
        int i, cur = 0;
        for (i = 0; i < n; i++) if (order[i] == s->background) cur = i;
        s->background = order[cycle(cur, n, dir)];
        return MENU_FX_CHANGED;
    }
    case ROW_F_UI_COLOR: s->ui_color = cycle(s->ui_color, UI_COLOR_COUNT, dir); return MENU_FX_CHANGED;
    case ROW_F_INFO: s->show_info = cycle(s->show_info, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_CASES: s->cases = cycle(s->cases, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_REFLECT: s->reflections = cycle(s->reflections, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_SLEEP: {
        static const int steps[] = { 0, 1, 5, 10, 15, 30, 60 };
        int i, cur = 0, n = (int)(sizeof(steps) / sizeof(steps[0]));
        for (i = 0; i < n; i++) if (steps[i] == s->sleep_min) cur = i;
        s->sleep_min = steps[cycle(cur, n, dir)];
        return MENU_FX_CHANGED;
    }
    case ROW_F_COVERS: return dir == 0 ? MENU_FX_COVERS_STUB : MENU_FX_NONE;
    case ROW_F_IGNORE_THE: s->ignore_the = cycle(s->ignore_the, 2, dir); return MENU_FX_RESORT;
    case ROW_F_XMB_TOP: s->xmb_top = cycle(s->xmb_top, 2, dir); return MENU_FX_XMB_TOP;
    case ROW_F_RESCAN: return dir == 0 ? MENU_FX_RESCAN : MENU_FX_NONE;
    case ROW_F_ABOUT: return dir == 0 ? MENU_FX_ABOUT : MENU_FX_NONE;
    case ROW_F_QUIT: return dir == 0 ? MENU_FX_QUIT : MENU_FX_NONE;
    case ROW_F_POWEROFF: return dir == 0 ? MENU_FX_POWEROFF : MENU_FX_NONE;
    }
    return MENU_FX_NONE;
}

float menu_slider_value(RowKind k, const Settings *s)
{
    if (k == ROW_F_VOLUME) return (float)s->music_volume / 100.0f;
    if (k == ROW_F_SFX) return (float)s->sfx_volume / 100.0f;
    return 0.0f;
}
