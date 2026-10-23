/* menu.c - rows of the Game settings and Flow settings panels. */
#include "menu.h"
#include "util.h"
#include <stdio.h>

static const char *const k_rsx[RSX_PRESET_COUNT] = { "Default", "500 MHz", "550 MHz", "600 MHz", "650 MHz" };

int menu_rows(int panel, int plat, RowKind *out, int max)
{
    RowKind tmp[MENU_MAX_ROWS];
    int n = 0, i;
    if (panel == PANEL_GAME) {
        tmp[n++] = ROW_G_AUTOPLAY;
        if (plat == PLAT_PS1) { tmp[n++] = ROW_G_EMULATOR; }
        else if (plat == PLAT_PS2) {
            tmp[n++] = ROW_G_EMULATOR; tmp[n++] = ROW_G_OFFLINE;
            tmp[n++] = ROW_G_RSX; tmp[n++] = ROW_G_PS2CFG;
        } else { tmp[n++] = ROW_G_OFFLINE; tmp[n++] = ROW_G_RSX; }
    } else if (panel == PANEL_FLOW) {
        tmp[n++] = ROW_F_AUTOPLAY; tmp[n++] = ROW_F_OFFLINE; tmp[n++] = ROW_F_SFX; tmp[n++] = ROW_F_VOLUME; tmp[n++] = ROW_F_SHUFFLE;
        tmp[n++] = ROW_F_BACKGROUND; tmp[n++] = ROW_F_UI_COLOR; tmp[n++] = ROW_F_INFO; tmp[n++] = ROW_F_COVERS; tmp[n++] = ROW_F_IGNORE_THE;
        tmp[n++] = ROW_F_XMB_TOP; tmp[n++] = ROW_F_RESCAN; tmp[n++] = ROW_F_QUIT;
        tmp[n++] = ROW_F_POWEROFF;
    }
    if (n > max) n = max;
    for (i = 0; i < n; i++) out[i] = tmp[i];
    return n;
}

const char *menu_label(RowKind k)
{
    switch (k) {
    case ROW_G_AUTOPLAY: return "Auto-Play";
    case ROW_G_EMULATOR: return "Emulator";
    case ROW_G_OFFLINE: return "Offline Mode";
    case ROW_G_RSX: return "RSX Clock";
    case ROW_G_PS2CFG: return "PS2 Config";
    case ROW_F_AUTOPLAY: return "Auto-Play";
    case ROW_F_OFFLINE: return "Offline Mode";
    case ROW_F_SFX: return "Sound Effects";
    case ROW_F_VOLUME: return "Music Volume";
    case ROW_F_SHUFFLE: return "Shuffle Music";
    case ROW_F_BACKGROUND: return "Background";
    case ROW_F_UI_COLOR: return "UI Color";
    case ROW_F_INFO: return "Show Game Info";
    case ROW_F_COVERS: return "Download Missing Covers";
    case ROW_F_IGNORE_THE: return "Ignore \"The\" When Sorting";
    case ROW_F_XMB_TOP: return "Pin to Top of XMB";
    case ROW_F_RESCAN: return "Rescan Library";
    case ROW_F_QUIT: return "Quit to XMB";
    case ROW_F_POWEROFF: return "Power Off PS3";
    }
    return "";
}

int menu_is_action(RowKind k)
{
    return k == ROW_G_PS2CFG || k == ROW_F_COVERS || k == ROW_F_RESCAN || k == ROW_F_QUIT || k == ROW_F_POWEROFF;
}

int menu_is_slider(RowKind k) { return k == ROW_F_VOLUME || k == ROW_F_SFX; }

static const char *onoff(int v) { return v ? "On" : "Off"; }

void menu_value_text(RowKind k, int plat, const Settings *s, const GameCfg *g, char *out, size_t cap)
{
    out[0] = '\0';
    switch (k) {
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
    case ROW_G_RSX: sf_strlcpy(out, k_rsx[sf_clampi(g->rsx, 0, RSX_PRESET_COUNT - 1)], cap); break;
    case ROW_G_PS2CFG: sf_strlcpy(out, "Edit", cap); break;
    case ROW_F_AUTOPLAY: sf_strlcpy(out, onoff(s->autoplay), cap); break;
    case ROW_F_OFFLINE: sf_strlcpy(out, onoff(s->offline), cap); break;
    case ROW_F_SFX:
        if (s->sfx_volume == 0) sf_strlcpy(out, "Off", cap);
        else snprintf(out, cap, "%d%%", s->sfx_volume);
        break;
    case ROW_F_VOLUME: snprintf(out, cap, "%d%%", s->music_volume); break;
    case ROW_F_SHUFFLE: sf_strlcpy(out, onoff(s->shuffle), cap); break;
    case ROW_F_BACKGROUND: sf_strlcpy(out, s->background == BG_IMAGE ? "Image" : "Animated", cap); break;
    case ROW_F_UI_COLOR: sf_strlcpy(out, settings_color_name(s->ui_color), cap); break;
    case ROW_F_INFO: sf_strlcpy(out, onoff(s->show_info), cap); break;
    case ROW_F_COVERS: sf_strlcpy(out, "Start", cap); break;
    case ROW_F_IGNORE_THE: sf_strlcpy(out, onoff(s->ignore_the), cap); break;
    case ROW_F_XMB_TOP: sf_strlcpy(out, onoff(s->xmb_top), cap); break;
    case ROW_F_RESCAN: sf_strlcpy(out, "Start", cap); break;
    case ROW_F_QUIT: sf_strlcpy(out, "", cap); break;
    case ROW_F_POWEROFF: sf_strlcpy(out, "", cap); break;
    }
}

static int cycle(int v, int n, int dir)
{
    int d = dir == 0 ? 1 : dir;
    return ((v + d) % n + n) % n;
}

int menu_change(RowKind k, int plat, int dir, Settings *s, GameCfg *g)
{
    (void)plat;
    switch (k) {
    case ROW_G_AUTOPLAY: g->autoplay = (signed char)cycle(g->autoplay, 3, dir); return MENU_FX_CHANGED;
    case ROW_G_EMULATOR: g->emulator = (signed char)cycle(g->emulator, 2, dir); return MENU_FX_CHANGED;
    case ROW_G_OFFLINE: g->offline = (signed char)cycle(g->offline, 3, dir); return MENU_FX_CHANGED;
    case ROW_G_RSX: g->rsx = (signed char)cycle(g->rsx, RSX_PRESET_COUNT, dir); return MENU_FX_CHANGED;
    case ROW_G_PS2CFG: return dir == 0 ? MENU_FX_PS2CFG_STUB : MENU_FX_NONE;
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
    case ROW_F_BACKGROUND: s->background = cycle(s->background, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_UI_COLOR: s->ui_color = cycle(s->ui_color, UI_COLOR_COUNT, dir); return MENU_FX_CHANGED;
    case ROW_F_INFO: s->show_info = cycle(s->show_info, 2, dir); return MENU_FX_CHANGED;
    case ROW_F_COVERS: return dir == 0 ? MENU_FX_COVERS_STUB : MENU_FX_NONE;
    case ROW_F_IGNORE_THE: s->ignore_the = cycle(s->ignore_the, 2, dir); return MENU_FX_RESORT;
    case ROW_F_XMB_TOP: s->xmb_top = cycle(s->xmb_top, 2, dir); return MENU_FX_XMB_TOP;
    case ROW_F_RESCAN: return dir == 0 ? MENU_FX_RESCAN : MENU_FX_NONE;
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
