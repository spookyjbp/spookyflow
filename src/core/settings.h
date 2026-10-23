/* settings.h - global settings + per-game settings, simple text file. */
#ifndef SF_SETTINGS_H
#define SF_SETTINGS_H

#include "layout.h"

#define SET_KEY_MAX      128
#define SET_MAX_GAMES    512

/* Per-game tri-state for auto-play */
enum { AP_DEFAULT = 0, AP_ON = 1, AP_OFF = 2 };
enum { BG_ANIMATED = 0, BG_IMAGE = 1 };
enum { UI_COLOR_BLUE = 0, UI_COLOR_RED, UI_COLOR_GREEN, UI_COLOR_PURPLE, UI_COLOR_PINK,
       UI_COLOR_ORANGE, UI_COLOR_WHITE, UI_COLOR_COUNT };

#define RSX_PRESET_COUNT 5   /* 0 = Default, then presets (meaning decided in milestone 3) */

typedef struct {
    char key[SET_KEY_MAX];   /* title ID or file name */
    signed char autoplay;    /* AP_* */
    signed char emulator;    /* PS1: 0 Standard 1 Netemu; PS2: 0 Default 1 Netemu */
    signed char offline;     /* AP_DEFAULT (follow the global setting), AP_ON, AP_OFF */
    signed char rsx;         /* 0 Default, 1..RSX_PRESET_COUNT-1 */
} GameCfg;

typedef struct {
    int autoplay;            /* global, 0/1, default 0 */
    int offline;             /* global offline mode for games that support it, 0/1, default 0 */
    int music_volume;        /* 0..100, default 60 */
    int shuffle;             /* default 1 */
    int sfx_volume;          /* UI sound effects volume 0..100 (0 = off), default 50 */
    int background;          /* BG_*, default animated */
    int ui_color;            /* UI_COLOR_*, default white */
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
