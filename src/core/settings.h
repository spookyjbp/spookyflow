/* settings.h - global settings + per-game settings, simple text file. */
#ifndef SF_SETTINGS_H
#define SF_SETTINGS_H

#include "layout.h"

#define SET_KEY_MAX      128
#define SET_MAX_GAMES    512

/* Per-game tri-state for auto-play */
enum { AP_DEFAULT = 0, AP_ON = 1, AP_OFF = 2 };
/* Background styles (values are stored in settings.ini; keep them stable) */
enum { BG_ANIMATED = 0 /* "Drift" */, BG_IMAGE = 1, BG_AURORA = 2, BG_FIREFLIES = 3,
       BG_RIBBONS = 4, BG_HALO = 5, BG_TWILIGHT = 6, BG_STAGE = 7, BG_COUNT = 8 };
const char *settings_background_name(int bg);
enum { UI_COLOR_BLUE = 0, UI_COLOR_RED, UI_COLOR_GREEN, UI_COLOR_PURPLE, UI_COLOR_PINK,
       UI_COLOR_ORANGE, UI_COLOR_WHITE, UI_COLOR_COUNT };

typedef struct {
    char key[SET_KEY_MAX];   /* title ID or file name */
    signed char autoplay;    /* AP_* */
    signed char emulator;    /* PS1: 0 Standard 1 Netemu; PS2: 0 Default 1 Netemu */
    signed char offline;     /* AP_DEFAULT (follow the global setting), AP_ON, AP_OFF */
} GameCfg;

typedef struct {
    int autoplay;            /* global, 0/1, default 0 */
    int offline;             /* global offline mode for games that support it, 0/1, default 0 */
    int music_volume;        /* 0..100, default 60 */
    int shuffle;             /* default 1 */
    int sfx_volume;          /* UI sound effects volume 0..100 (0 = off), default 50 */
    int background;          /* BG_*, default animated */
    int ui_color;            /* UI_COLOR_*, default blue */
    int cases;               /* draw covers inside case images (ManaGunZ's / your own), default 1 */
    int emu_full;            /* 1 = launch PS1/PS2 games with Full Screen (XMB Upscaler), 0 = leave it, default 0 */
    int emu_smooth;          /* 1 = launch PS1/PS2 games with Smoothing on, 0 = leave it, default 0 */
    int sleep_min;           /* minutes without input before the screen dims, 0 = never, default 5 */
    int reflections;         /* mirror image of the cases on the floor, default 1 */
    int show_info;           /* genre / players / size chips under the case, default 1 */
    int ignore_the;          /* default 0 */
    int xmb_top;             /* pin spookyflow to the top of the XMB Game column, default 0 */
    int last_tab;            /* 0..2 */
    char last_key[PLAT_COUNT][SET_KEY_MAX];
    GameCfg games[SET_MAX_GAMES];
    int game_count;
} Settings;

void settings_defaults(Settings *s);
const char *settings_color_name(int ui_color);
/* Returns 0 on success, -1 if the file could not be opened (defaults kept). */
int  settings_load(Settings *s, const char *path);
int  settings_save(const Settings *s, const char *path);
/* Parse/serialize from memory (used by load/save and tests). */
void settings_parse_line(Settings *s, const char *line);

/* Per-game access. get returns a default-valued record if absent. */
GameCfg settings_game_get(const Settings *s, const char *key);
/* Store (or remove when all defaults). Returns 0, or -1 if the table is full. */
int  settings_game_set(Settings *s, const GameCfg *cfg);

/* Effective auto-play for a game: per-game override or the global value. */
int  settings_autoplay_effective(const Settings *s, const GameCfg *cfg);
/* Effective offline mode for a game (PS2/PS3): per-game override or the global value. */
int  settings_offline_effective(const Settings *s, const GameCfg *cfg);

#endif
