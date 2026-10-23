/* settings.c - global settings + per-game settings, simple text file.
 *
 * Format (one entry per line, '#' comments):
 *   autoplay=0
 *   music_volume=60
 *   ...
 *   last_key_ps2=SCUS97328
 *   game <autoplay> <emulator> <offline> <rsx> <key to end of line>
 * The key is last on "game" lines so it may contain spaces, '=', etc. */
#include "settings.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

const char *settings_color_name(int c)
{
    static const char *const names[UI_COLOR_COUNT] = {
        "Blue", "Red", "Green", "Purple", "Pink", "Orange", "White"
    };
    return (c >= 0 && c < UI_COLOR_COUNT) ? names[c] : "";
}

void settings_defaults(Settings *s)
{
    memset(s, 0, sizeof(*s));
    s->autoplay = 0;
    s->music_volume = 50;
    s->shuffle = 1;
    s->sfx_volume = 50;
    s->background = BG_ANIMATED;
    s->ui_color = UI_COLOR_WHITE;
    s->show_info = 1;
    s->ignore_the = 0;
    s->last_tab = PLAT_PS2;
}

static int parse_int(const char *v, int lo, int hi, int def)
{
    char *end;
    long n = strtol(v, &end, 10);
    if (end == v) return def;
    if (n < lo) n = lo;
    if (n > hi) n = hi;
    return (int)n;
}

static const char *const k_plat_suffix[PLAT_COUNT] = { "ps1", "ps2", "ps3" };

void settings_parse_line(Settings *s, const char *line_in)
{
    char line[512];
    char *eq, *key, *val;
    size_t n;
    sf_strlcpy(line, line_in, sizeof(line));
    n = strlen(line);
    while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
    key = line;
    while (*key == ' ' || *key == '\t') key++;
    if (*key == '#' || *key == '\0') return;

    if (strncmp(key, "game ", 5) == 0) {
        GameCfg g;
        int a, e, o, r, consumed = 0;
        memset(&g, 0, sizeof(g));
        if (sscanf(key + 5, "%d %d %d %d %n", &a, &e, &o, &r, &consumed) == 4 && consumed > 0) {
            const char *k = key + 5 + consumed;
            if (*k) {
                sf_strlcpy(g.key, k, sizeof(g.key));
                g.autoplay = (signed char)sf_clampi(a, 0, 2);
                g.emulator = (signed char)sf_clampi(e, 0, 1);
                /* 0 = Default, 1 = On, 2 = Off. Files from before the global
                 * option stored 0 = Off: that now reads as Default, which is
                 * Off unless the global option is turned on. */
                g.offline = (signed char)sf_clampi(o, 0, 2);
                g.rsx = (signed char)sf_clampi(r, 0, RSX_PRESET_COUNT - 1);
                settings_game_set(s, &g);
            }
        }
        return;
    }

    eq = strchr(key, '=');
    if (!eq) return;
    *eq = '\0';
    val = eq + 1;
    if (strcmp(key, "autoplay") == 0) s->autoplay = parse_int(val, 0, 1, 0);
    else if (strcmp(key, "offline") == 0) s->offline = parse_int(val, 0, 1, 0);
    else if (strcmp(key, "music_volume") == 0) s->music_volume = parse_int(val, 0, 100, 50);
    else if (strcmp(key, "shuffle") == 0) s->shuffle = parse_int(val, 0, 1, 1);
    else if (strcmp(key, "sound_effects") == 0) { if (parse_int(val, 0, 1, 1) == 0) s->sfx_volume = 0; }   /* fix 13 on/off */
    else if (strcmp(key, "sfx_volume") == 0) s->sfx_volume = parse_int(val, 0, 100, 50);
    else if (strcmp(key, "background") == 0) s->background = parse_int(val, 0, 1, 0);
    else if (strcmp(key, "ui_color") == 0) s->ui_color = parse_int(val, 0, UI_COLOR_COUNT - 1, UI_COLOR_WHITE);
    else if (strcmp(key, "show_game_info") == 0) s->show_info = parse_int(val, 0, 1, 1);
    else if (strcmp(key, "ignore_the") == 0) s->ignore_the = parse_int(val, 0, 1, 0);
    else if (strcmp(key, "xmb_top") == 0) s->xmb_top = parse_int(val, 0, 1, 0);
    else if (strcmp(key, "last_tab") == 0) s->last_tab = parse_int(val, 0, PLAT_COUNT - 1, PLAT_PS2);
    else if (strncmp(key, "last_key_", 9) == 0) {
        int p;
        for (p = 0; p < PLAT_COUNT; p++)
            if (strcmp(key + 9, k_plat_suffix[p]) == 0) sf_strlcpy(s->last_key[p], val, SET_KEY_MAX);
    }
}

int settings_load(Settings *s, const char *path)
{
    char line[512];
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        /* drop the remainder of over-long lines */
        if (!strchr(line, '\n') && !feof(f)) {
            int c;
            while ((c = fgetc(f)) != EOF && c != '\n') { }
        }
        settings_parse_line(s, line);
    }
    fclose(f);
    return 0;
}

int settings_save(const Settings *s, const char *path)
{
    int i, p;
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# spookyflow settings\n");
    fprintf(f, "autoplay=%d\n", s->autoplay);
    fprintf(f, "offline=%d\n", s->offline);
    fprintf(f, "music_volume=%d\n", s->music_volume);
    fprintf(f, "shuffle=%d\n", s->shuffle);
    fprintf(f, "sfx_volume=%d\n", s->sfx_volume);
    fprintf(f, "background=%d\n", s->background);
    fprintf(f, "ui_color=%d\n", s->ui_color);
    fprintf(f, "show_game_info=%d\n", s->show_info);
    fprintf(f, "ignore_the=%d\n", s->ignore_the);
    fprintf(f, "xmb_top=%d\n", s->xmb_top);
    fprintf(f, "last_tab=%d\n", s->last_tab);
    for (p = 0; p < PLAT_COUNT; p++)
        if (s->last_key[p][0] && !strchr(s->last_key[p], '\n'))
            fprintf(f, "last_key_%s=%s\n", k_plat_suffix[p], s->last_key[p]);
    fprintf(f, "# game <autoplay 0=default 1=on 2=off> <emulator> <offline 0=default 1=on 2=off> <rsx> <key>\n");
    for (i = 0; i < s->game_count; i++) {
        const GameCfg *g = &s->games[i];
        if (strchr(g->key, '\n')) continue;
        fprintf(f, "game %d %d %d %d %s\n", g->autoplay, g->emulator, g->offline, g->rsx, g->key);
    }
    fflush(f);
    fclose(f);
    return 0;
}

static int find_game(const Settings *s, const char *key)
{
    int i;
    for (i = 0; i < s->game_count; i++) if (strcmp(s->games[i].key, key) == 0) return i;
    return -1;
}

GameCfg settings_game_get(const Settings *s, const char *key)
{
    GameCfg g;
    int i = find_game(s, key);
    if (i >= 0) return s->games[i];
    memset(&g, 0, sizeof(g));
    sf_strlcpy(g.key, key, sizeof(g.key));
    return g;
}

int settings_game_set(Settings *s, const GameCfg *cfg)
{
    int i = find_game(s, cfg->key);
    int is_default = cfg->autoplay == 0 && cfg->emulator == 0 && cfg->offline == 0 && cfg->rsx == 0;
    if (!cfg->key[0]) return -1;
    if (i >= 0) {
        if (is_default) {
            s->games[i] = s->games[s->game_count - 1];
            s->game_count--;
        } else s->games[i] = *cfg;
        return 0;
    }
    if (is_default) return 0;
    if (s->game_count >= SET_MAX_GAMES) return -1;
    s->games[s->game_count++] = *cfg;
    return 0;
}

int settings_offline_effective(const Settings *s, const GameCfg *cfg)
{
    if (cfg->offline == AP_ON) return 1;
    if (cfg->offline == AP_OFF) return 0;
    return s->offline ? 1 : 0;
}

int settings_autoplay_effective(const Settings *s, const GameCfg *cfg)
{
    if (cfg->autoplay == AP_ON) return 1;
    if (cfg->autoplay == AP_OFF) return 0;
    return s->autoplay ? 1 : 0;
}
