/* menu.h - rows of the Game settings and Flow settings panels. */
#ifndef SF_MENU_H
#define SF_MENU_H

#include <stddef.h>
#include "settings.h"

typedef enum {
    /* Game settings (per game) */
    ROW_G_AUTOPLAY, ROW_G_EMULATOR, ROW_G_OFFLINE, ROW_G_PS2CFG,
    /* Flow settings (global) */
    ROW_F_AUTOPLAY, ROW_F_OFFLINE, ROW_F_SFX, ROW_F_VOLUME, ROW_F_SHUFFLE, ROW_F_BACKGROUND, ROW_F_UI_COLOR, ROW_F_INFO, ROW_F_CASES, ROW_F_REFLECT, ROW_F_SLEEP, ROW_F_COVERS,
    ROW_F_IGNORE_THE, ROW_F_XMB_TOP, ROW_F_RESCAN, ROW_F_QUIT, ROW_F_POWEROFF, ROW_F_ABOUT,
    ROW_F_HIDE_UI, ROW_F_STORAGE,          /* 0.8.7 */
    ROW_F_CASE_LIGHT,                      /* 0.9.8 */
    /* Flow settings groups (0.7.0): the first page lists these, ✖ opens one */
    ROW_FG_LOOK, ROW_FG_SOUND, ROW_FG_GAMES, ROW_FG_SYSTEM,
    /* Choose Disc (multi-disc games): row i = disc i */
    ROW_D_DISC
} RowKind;

enum { PANEL_NONE = 0, PANEL_GAME = 1, PANEL_FLOW = 2, PANEL_DISC = 3 };

#define MENU_MAX_ROWS 20

/* Effects a change can request from the app. */
enum {
    MENU_FX_NONE = 0,
    MENU_FX_CHANGED,        /* a value changed: save settings */
    MENU_FX_RESORT,         /* ignore-the changed: re-sort tabs */
    MENU_FX_RESCAN,
    MENU_FX_QUIT,
    MENU_FX_COVERS_STUB,
    MENU_FX_PS2CFG,         /* PS2 config row: the app knows which configs exist */
    MENU_FX_XMB_TOP,        /* pin-to-top changed: patch our PARAM.SFO */
    MENU_FX_POWEROFF,       /* power off the console */
    MENU_FX_GROUP,          /* open a Flow Settings group */
    MENU_FX_ABOUT,          /* the About spookyflow card (0.8.2) */
    MENU_FX_STORAGE         /* the Storage card: sizes by console (0.8.7) */
};

int  menu_rows(int panel, int plat, RowKind *out, int max);   /* PANEL_FLOW: the group page */
/* the rows of a Flow Settings group (ROW_FG_*) */
int  menu_group_rows(RowKind group, RowKind *out, int max);
int  menu_is_group(RowKind k);
/* the group a Flow Settings row belongs to (ROW_FG_*), or -1 for rows on the first page */
int  menu_group_of(RowKind k);
const char *menu_label(RowKind k);
/* Is this an action row (no value, ✖ activates)? */
int  menu_is_action(RowKind k);
int  menu_is_slider(RowKind k);
/* Text of the current value. For the per-game auto-play this shows
 * "Default (Off)" / "Default (On)" reflecting the global setting. */
void menu_value_text(RowKind k, int plat, const Settings *s, const GameCfg *g, char *out, size_t cap);
/* Change a value: dir = -1 / +1 (left/right), or 0 for ✖ (cycle forward /
 * activate). Returns a MENU_FX_* code. */
int  menu_change(RowKind k, int plat, int dir, Settings *s, GameCfg *g);

/* 0.8.0, for drawing the values: an on/off row (0 / 1), else -1 */
int  menu_toggle(RowKind k, const Settings *s);
/* a row shown as a few choices side by side (Default / On / Off ...):
 * the number of choices (0 = not such a row), their names, and which one
 * is chosen */
int  menu_segments(RowKind k, int plat, const GameCfg *g, const char *const **names, int *cur);

/* Slider value 0..1 for drawing. */
float menu_slider_value(RowKind k, const Settings *s);

#endif
