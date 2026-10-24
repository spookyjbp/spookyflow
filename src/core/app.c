/* app.c - portable application state machine. */
#include "app.h"
#include "sfo.h"
#include "sound.h"
#include "music.h"
#include "discid.h"
#include "render.h"
#include "log.h"
#include "title.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sync_xmb_priority(App *a);

static const char *const k_plat_name[PLAT_COUNT] = { "PS1", "PS2", "PS3" };

/* Cover cache loader: hand the game to the cover worker. Demo games keep
 * their drawn placeholder. */
static int load_cover(void *user, int tab, int index, int slot_id, CcSlot *slot)
{
    App *a = (App *)user;
    const GameList *gl = &a->lib->tab[tab];
    const Game *g;
    (void)slot;
    if (index < 0 || index >= gl->count) return CC_FAILED;
    g = &gl->games[index];
    if (g->flags & GAME_DEMO) return CC_READY;
    cover_request(a->host.covers, slot_id, tab, tab, index, lib_path(a->lib, g),
                  (g->flags & GAME_DIR) != 0, g->size, lib_id(a->lib, g));
    return CC_LOADING;
}

CcLoadFn app_cover_loader(void) { return load_cover; }

/* the cover worker found a game's release year (from its title ID) */
static void cover_result(void *user, int tab, int index, int year)
{
    App *a = (App *)user;
    if (year <= 0 || tab < 0 || tab >= PLAT_COUNT || index < 0 || index >= a->lib->tab[tab].count) return;
    a->lib->tab[tab].games[index].year = (uint16_t)year;
}

static void drop_covers(App *a)
{
    cc_invalidate(&a->covers);
    if (a->host.covers) cover_cancel_all(a->host.covers);
}

/* ------------------------------------------------------------------ */

void app_init(App *a, Library *lib, const AppHost *host)
{
    int t;
    memset(a, 0, sizeof(*a));
    a->lib = lib;
    if (host) a->host = *host;
    if (!a->host.roots) a->host.roots = scanner_default_roots(&a->host.nroots);
    settings_defaults(&a->settings);
    lib_clear(lib);
    cc_init(&a->covers, CC_DEFAULT_BUDGET, a->host.covers ? load_cover : NULL, a);
    if (a->host.covers) { a->host.covers->on_result = cover_result; a->host.covers->on_result_user = a; }
    input_reset(&a->input);
    hold_exit_reset(&a->hold);
    for (t = 0; t < PLAT_COUNT; t++) spring_init(&a->view[t].scroll, 0.0f, SPRING_OMEGA_300MS);
    spring_init(&a->tab_hl, 1.0f, SPRING_OMEGA_300MS);
    spring_init(&a->row_hl, 0.0f, 26.0f);
    spring_init(&a->list_scroll, 0.0f, 24.0f);
    spring_init(&a->title_w, 0.0f, 24.0f);
    a->mode = MODE_LOADING;
    a->load_step = LOAD_START;
    a->tab = a->tab_prev = PLAT_PS2;
    a->tab_anim = 1.0f;
    a->info_t = 1.0f;
    a->letter_t = LETTER_TIME;
    a->toast_t = TOAST_TIME;
    a->music_card_t = MUSIC_CARD_TIME;
    a->music_card_len = MUSIC_CARD_TIME;
    a->music_swap_t = 1.0f;
    a->reveal_start = -1000.0f;
    sf_strlcpy(a->load_status, "Starting\xE2\x80\xA6", sizeof(a->load_status));
}

const Game *app_selected_game(const App *a, int tab)
{
    const GameList *gl = &a->lib->tab[tab];
    int i = a->view[tab].sel;
    if (gl->count <= 0 || i < 0 || i >= gl->count) return NULL;
    return &gl->games[i];
}

int app_tab_has_games(const App *a, int tab) { return a->lib->tab[tab].count > 0; }

int app_panel_visible_rows(const App *a)
{
    return a->panel == PANEL_FLOW ? PANEL_VISIBLE_ROWS_FLOW : PANEL_VISIBLE_ROWS;
}

int app_main_context(const App *a)
{
    return a->mode == MODE_FLOW && a->panel == PANEL_NONE && !a->exiting && !a->display;
}

void app_show_toast(App *a, const char *msg)
{
    sf_strlcpy(a->toast, msg, sizeof(a->toast));
    a->toast_t = 0.0f;
}

void app_select(App *a, int tab, int index, int animate)
{
    const GameList *gl = &a->lib->tab[tab];
    TabView *v = &a->view[tab];
    if (gl->count <= 0) { v->sel = 0; spring_snap(&v->scroll, 0.0f); return; }
    index = sf_clampi(index, 0, gl->count - 1);
    if (animate) {
        double gap = a->time - v->step_t;
        int one = index == v->sel + 1 || index == v->sel - 1;
        v->step_rate = (one && gap > 0.0 && gap < FLOW_STEP_WINDOW) ? (float)(1.0 / gap) : 0.0f;
        if (one) v->step_t = a->time;
    }
    v->sel = index;
    if (animate) spring_set(&v->scroll, (float)v->sel);
    else spring_snap(&v->scroll, (float)v->sel);
}

static void remember_selection(App *a)
{
    int t;
    for (t = 0; t < PLAT_COUNT; t++) {
        const Game *g = app_selected_game(a, t);
        if (g) sf_strlcpy(a->settings.last_key[t], lib_key(a->lib, g), SET_KEY_MAX);
    }
    a->settings.last_tab = a->tab;
}

void app_save(App *a)
{
    if (!a->settings_ready) return;     /* never overwrite the file with defaults */
    if (a->mode == MODE_FLOW) remember_selection(a);
    if (!a->host.settings_path) return;
    if (settings_save(&a->settings, a->host.settings_path) != 0)
        log_printf("settings: could not write %s", a->host.settings_path);
}

void app_request_exit(App *a, const char *why)
{
    if (a->exiting) return;
    a->exiting = 1;
    a->exit_fade = 0.0f;
    log_printf("exit requested (%s)", why);
    app_save(a);
    /* 0.8.1: a panel stays on screen while the picture fades (Quit / Power Off rows) */
}

/* ------------------------------------------------------------------ */
/* Loading screen state machine                                        */

static void start_loading(App *a, int rescan)
{
    a->mode = MODE_LOADING;
    a->load_step = LOAD_START;
    a->load_t = 0.0f;
    a->load_target = 0.0f;
    a->load_shown = 0.0f;
    a->rescan = rescan;
    a->intro = 0.0f;
    a->reveal_start = -1000.0f;     /* ui.c starts the Stage reveal when it can */
    if (!rescan) drop_covers(a);    /* a rescan drops them once the cover flow has faded out */
}

static void sort_all(App *a, int keep_selection)
{
    int t;
    for (t = 0; t < PLAT_COUNT; t++) {
        char key[SET_KEY_MAX] = "";
        const Game *g = keep_selection ? app_selected_game(a, t) : NULL;
        if (g) sf_strlcpy(key, lib_key(a->lib, g), sizeof(key));
        lib_sort(a->lib, t, a->settings.ignore_the);
        if (keep_selection) {
            int idx = lib_find_key(a->lib, t, key);
            app_select(a, t, idx < 0 ? 0 : idx, 0);
        }
    }
    drop_covers(a);
}

static void finish_library(App *a)
{
    int t;
    for (t = 0; t < PLAT_COUNT; t++) {
        GameList *gl = &a->lib->tab[t];
        if (gl->count == 0) {
            demo_fill(a->lib, t);
            log_printf("library: %s has no games on HDD, showing demo library (%d titles)", k_plat_name[t], gl->count);
        } else {
            log_printf("library: %s %d games%s", k_plat_name[t], gl->count,
                       gl->dropped ? " (some skipped: list full)" : "");
        }
    }
    if (a->lib->pool_full) log_printf("library: string pool full, some games skipped");
    for (t = 0; t < PLAT_COUNT; t++) {
        if (a->lib->tab[t].demo) continue;
        {
            int merged = lib_group_discs(a->lib, t), i, d;
            if (merged > 0) log_printf("library: %s multi-disc games grouped (%d disc entries folded into their first disc)", k_plat_name[t], merged);
            for (i = 0; i < a->lib->tab[t].count; i++) {
                const Game *g = &a->lib->tab[t].games[i];
                if (g->ndisc < 2) continue;
                log_printf("library: %s \"%s\" has %d discs:", k_plat_name[t], lib_title(a->lib, g), g->ndisc);
                for (d = 0; d < g->ndisc; d++) {
                    char nm[TITLE_MAX];
                    const Disc *dd = lib_game_disc(a->lib, g, d);
                    lib_disc_name(a->lib, dd, d, nm, sizeof(nm));
                    log_printf("library:   %s = %s", nm, dd ? lib_str(a->lib, dd->path) : "?");
                }
            }
        }
    }
    sort_all(a, 0);
    for (t = 0; t < PLAT_COUNT; t++) {
        int idx = lib_find_key(a->lib, t, a->settings.last_key[t]);
        app_select(a, t, idx < 0 ? 0 : idx, 0);
    }
    a->tab = a->tab_prev = sf_clampi(a->settings.last_tab, 0, PLAT_COUNT - 1);
    a->tab_anim = 1.0f;
    spring_snap(&a->tab_hl, (float)a->tab);
    a->info_tab = a->tab;
    a->info_idx = a->view[a->tab].sel;
    a->info_t = 1.0f;
    a->title_w_init = 0;
}

/* Worker-thread scan: the whole library scan runs here so the loading screen
 * keeps animating at full frame rate no matter how slow the disk is. While
 * scan_busy is set, the main thread only reads scanner progress counters. */
/* "Rescan Library": webMAN re-indexes the NTFS drives first (it clears the old
 * index and walks every NTFS drive it has mounted), so games added to or
 * removed from a drive show up. Slow drives can take a while: it runs on the
 * scan thread when there is one. */
static void refresh_ntfs(App *a)
{
    int n;
    if (!a->rescan || !a->host.ntfs_refresh) return;
    a->ntfs_busy = 1;
    __sync_synchronize();
    n = a->host.ntfs_refresh();
    if (n >= 0) log_printf("ntfs: webMAN re-indexed the NTFS drives: %d games", n);
    else log_printf("ntfs: webMAN didn't answer the NTFS refresh; using the index as it is");
    a->ntfs_busy = 0;
    __sync_synchronize();
}

/* read the music folder (start-up and Rescan Library) */
/* ---- the user's background pictures ---------------------------------- */
static int cmp_name_i(const void *x, const void *y) { return sf_stricmp((const char *)x, (const char *)y); }

static void scan_backgrounds(App *a)
{
    static FsEntry ent[128];
    int n, i;
    a->nbg_files = 0;
    if (!a->host.bg_dir || !a->host.fs_list) return;
    n = a->host.fs_list(a->host.fs_ctx, a->host.bg_dir, ent, 128);
    for (i = 0; i < n && a->nbg_files < APP_MAX_BG_IMAGES; i++) {
        const char *nm = ent[i].name;
        if (ent[i].is_dir || nm[0] == '.' || strlen(nm) >= sizeof(a->bg_files[0])) continue;
        if (!sf_ends_with_i(nm, ".jpg") && !sf_ends_with_i(nm, ".jpeg") && !sf_ends_with_i(nm, ".png")) continue;
        sf_strlcpy(a->bg_files[a->nbg_files++], nm, sizeof(a->bg_files[0]));
    }
    qsort(a->bg_files, (size_t)a->nbg_files, sizeof(a->bg_files[0]), cmp_name_i);
    if (a->nbg_files) log_printf("backgrounds: %d picture%s in %s", a->nbg_files, a->nbg_files == 1 ? "" : "s", a->host.bg_dir);
}

static int bg_index(const App *a, const char *name)
{
    int i;
    for (i = 0; i < a->nbg_files; i++) if (strcmp(a->bg_files[i], name) == 0) return i;
    return -1;
}

/* make sure the picture the settings ask for is the one in the texture */
static void sync_bg_image(App *a)
{
    char path[512];
    if (a->settings.background != BG_CUSTOM) return;
    if (bg_index(a, a->settings.bg_image) < 0) {
        log_printf("backgrounds: \"%s\" is gone, using Stage", a->settings.bg_image);
        a->settings.background = BG_STAGE;
        return;
    }
    if (strcmp(a->bg_loaded, a->settings.bg_image) == 0) return;
    snprintf(path, sizeof(path), "%s/%s", a->host.bg_dir, a->settings.bg_image);
    if (a->host.load_bg_image && a->host.load_bg_image(path) == 0) {
        sf_strlcpy(a->bg_loaded, a->settings.bg_image, sizeof(a->bg_loaded));
        log_printf("backgrounds: using %s", path);
    } else {
        a->bg_loaded[0] = '\0';
        log_printf("backgrounds: %s could not be loaded", path);
        if (a->mode == MODE_FLOW) app_show_toast(a, "That picture couldn't be loaded (see log)");
    }
}

/* ←/→/✖ on Background: the styles, then each picture */
static void background_cycle(App *a, int dir)
{
    static const int styles[] = { BG_STAGE, BG_ANIMATED, BG_AURORA, BG_FIREFLIES, BG_RIBBONS, BG_HALO, BG_TWILIGHT };
    int ns = (int)(sizeof(styles) / sizeof(styles[0])), n = ns + a->nbg_files, cur = 0, i, d = dir == 0 ? 1 : dir;
    char val[160];
    if (a->settings.background == BG_CUSTOM) {
        i = bg_index(a, a->settings.bg_image);
        cur = i >= 0 ? ns + i : 0;
    } else {
        for (i = 0; i < ns; i++) if (styles[i] == a->settings.background) cur = i;
    }
    if (cur + d < 0 || cur + d >= n) return;            /* 0.8.2: the ends don't wrap */
    cur += d;
    if (cur < ns) a->settings.background = styles[cur];
    else {
        a->settings.background = BG_CUSTOM;
        sf_strlcpy(a->settings.bg_image, a->bg_files[cur - ns], sizeof(a->settings.bg_image));
        sync_bg_image(a);
    }
    sound_play(SND_CHANGE);
    app_row_value(a, ROW_F_BACKGROUND, val, sizeof(val));
    log_printf("flow setting: Background = %s", val);
    app_save(a);
}

static void scan_music(App *a)
{
    if (!a->host.music_dir || !a->host.fs_list || !a->host.read_at) return;
    music_init(a->host.read_at, a->host.fs_ctx);
    music_set_seed((uint32_t)(a->time * 1000.0) ^ 0x5F0F1234u);
    music_scan(a->host.fs_list, a->host.fs_ctx, a->host.music_dir);
    a->music_scanned = 1;
}

/* Music Volume % -> gain. Songs are mastered near full scale, so even 100%
 * stays well under the UI sounds' level, and the curve is squared so the
 * steps sound even: 50% = 0.125 (-18 dB), 100% = 0.5 (-6 dB). */
float music_gain(int percent)
{
    float v = (float)sf_clampi(percent, 0, 100) / 100.0f;
    return MUSIC_GAIN_MAX * v * v;
}

/* settings -> music, every frame: plays in the cover flow, fades out on exit
 * and launch, quieter while the screen sleeps */
static void music_tick(App *a)
{
    char name[128];
    int gen;
    if (!a->settings_ready) return;
    music_set_volume(music_gain(a->settings.music_volume));
    music_set_shuffle(a->settings.shuffle);
    music_set_duck(1.0f - MUSIC_SLEEP_DUCK * a->sleep_t);
    /* music starts MUSIC_START_DELAY after the cover flow has come in */
    music_set_playing(a->mode == MODE_FLOW && !a->exiting && a->music_scanned &&
                      a->intro >= 1.0f && a->flow_t >= INTRO_TIME + MUSIC_START_DELAY);
    gen = music_current(name, sizeof(name));
    if (gen != a->music_gen) {
        a->music_gen = gen;
        if (gen && a->mode == MODE_FLOW && !a->exiting && !a->asleep && name[0]) {
            /* a card that's still showing swaps the name smoothly (no second slide-in) */
            if (a->music_card_t >= a->music_card_len - 0.5f || !a->music_name[0]) {
                a->music_card_t = 0.0f;
                a->music_swap_t = 1.0f;
            } else {
                sf_strlcpy(a->music_prev_name, a->music_name, sizeof(a->music_prev_name));
                a->music_prev_off = a->music_cur_off;
                a->music_prev_w = a->music_cur_w;
                a->music_swap_t = 0.0f;
                if (a->music_card_t > 0.6f) a->music_card_t = 0.6f;
            }
            a->music_card_len = MUSIC_CARD_TIME;          /* ui.c lengthens it for a name that scrolls */
            sf_strlcpy(a->music_name, name, sizeof(a->music_name));
        }
    }
}

static void music_skip(App *a)
{
    if (music_track_count() > 0 && a->settings.music_volume > 0) { music_next(); return; }
    if (!a->host.music_dir) return;
    if (a->settings.music_volume == 0) app_show_toast(a, "Music is off (Music Volume in Flow Settings)");
    else app_show_toast(a, "No music: put .mp3 files in /dev_hdd0/tmp/spookyflow/music");
}

static void scan_worker(void *arg)
{
    App *a = (App *)arg;
    refresh_ntfs(a);
    while (!scanner_step(&a->scanner, a->lib, a->host.fs_list, a->host.fs_ctx)) { }
    __sync_synchronize();
    a->scan_busy = 0;
}

static void loading_update(App *a, float dt)
{
    int k;
    a->load_t += dt;
    switch (a->load_step) {
    case LOAD_START:
        if (a->rescan) {
            if (a->load_t < RESCAN_OUT_TIME) break;      /* the cover flow is still fading out */
            drop_covers(a);
            /* "Rescan Library" also refreshes covers (new art, a new cover pack) */
            if (a->host.covers) cover_wipe_cache(a->host.covers);
        }
        sf_strlcpy(a->load_status, a->rescan ? "Rescanning library\xE2\x80\xA6" : "Loading settings\xE2\x80\xA6", sizeof(a->load_status));
        a->load_target = 0.06f;
        a->load_step = LOAD_SETTINGS;      /* do the work next frame so this frame renders */
        break;
    case LOAD_SETTINGS:
        if (!a->rescan) {
            settings_defaults(&a->settings);
            if (a->host.settings_path) {
                if (settings_load(&a->settings, a->host.settings_path) == 0)
                    log_printf("settings: loaded %s (%d per-game entries)", a->host.settings_path, a->settings.game_count);
                else
                    log_printf("settings: %s not found, using defaults", a->host.settings_path);
            }
        } else {
            remember_selection(a);
        }
        a->settings_ready = 1;
        /* a PKG reinstall resets PARAM.SFO: re-apply the saved choice */
        if (!a->rescan && sync_xmb_priority(a) > 0)
            log_printf("xmb: position re-applied; it shows after the next PS3 restart");
        sf_strlcpy(a->load_status, "Preparing graphics\xE2\x80\xA6", sizeof(a->load_status));
        a->load_target = 0.12f;
        a->load_step = LOAD_PREPARE;
        break;
    case LOAD_PREPARE:
        if (a->host.prepare) a->host.prepare();
        scan_backgrounds(a);
        sync_bg_image(a);
        lib_clear(a->lib);
        scanner_begin(&a->scanner, a->host.roots, a->host.nroots);
        scanner_set_reader(&a->scanner, a->host.read_at, a->host.fs_ctx);
        a->scan_async = 0;
        a->scan_busy = 0;
        if (a->host.fs_list && a->host.run_async) {
            a->scan_busy = 1;
            __sync_synchronize();
            if (a->host.run_async(scan_worker, a) == 0) {
                a->scan_async = 1;
                log_printf("scan: running on a background thread");
            } else {
                a->scan_busy = 0;
                log_printf("scan: background thread failed, scanning on the main thread");
            }
        }
        if (!a->scan_async) refresh_ntfs(a);              /* no scan thread: do it here */
        a->load_target = 0.15f;
        a->load_step = LOAD_SCAN;
        break;
    case LOAD_SCAN:
        if (a->scan_async) {
            if (a->scan_busy) {
                if (a->ntfs_busy) sf_strlcpy(a->load_status, "Updating NTFS drives\xE2\x80\xA6", sizeof(a->load_status));
                else snprintf(a->load_status, sizeof(a->load_status), "Scanning %s games\xE2\x80\xA6",
                              k_plat_name[scanner_platform(&a->scanner)]);
                a->load_target = 0.15f + 0.70f * scanner_progress(&a->scanner);
                break;
            }
            __sync_synchronize();
        } else {
            /* fallback: a directory per frame keeps the animation moving */
            for (k = 0; k < 1; k++) {
                if (!a->host.fs_list) { a->scanner.done = 1; break; }
                if (scanner_step(&a->scanner, a->lib, a->host.fs_list, a->host.fs_ctx)) break;
            }
        }
        snprintf(a->load_status, sizeof(a->load_status), "Scanning %s games\xE2\x80\xA6",
                 k_plat_name[scanner_platform(&a->scanner)]);
        a->load_target = 0.15f + 0.70f * scanner_progress(&a->scanner);
        if (a->scanner.done || !a->host.fs_list) {
            log_printf("scan: done, %d dirs, found PS1=%d PS2=%d PS3=%d (%d of them on NTFS drives)",
                       a->scanner.dirs_listed, a->scanner.found[0], a->scanner.found[1], a->scanner.found[2],
                       a->scanner.found_ntfs);
            sf_strlcpy(a->load_status, "Sorting\xE2\x80\xA6", sizeof(a->load_status));
            a->load_target = 0.90f;
            a->load_step = LOAD_SORT;
        }
        break;
    case LOAD_SORT:
        finish_library(a);
        scan_music(a);
        sf_strlcpy(a->load_status, "Doing the Heavy Lifting", sizeof(a->load_status));
        a->load_target = 1.0f;
        a->load_step = LOAD_READY;
        break;
    case LOAD_READY:
        /* the loading screen can't be skipped: it always runs its full time */
        if (a->load_t >= (a->rescan ? RESCAN_MIN_TIME : LOAD_MIN_TIME) && a->load_shown >= 0.999f) {
            a->mode = MODE_FLOW;
            a->flow_t = 0.0f;
            a->intro = 0.0f;
            input_reset(&a->input);
            log_printf("ui: cover flow ready (tab %s)", k_plat_name[a->tab]);
        }
        break;
    }
    /* displayed progress glides toward the target */
    /* the bar fills steadily over the minimum time instead of jumping to full
     * in a quarter second and then sitting on the last message */
    {
        float pace = a->load_t / (a->rescan ? RESCAN_MIN_TIME : LOAD_MIN_TIME);
        float target = sf_minf(a->load_target, sf_maxf(pace, 0.0f));
        a->load_shown += (target - a->load_shown) * sf_minf(1.0f, dt * 6.0f);
        anim_approach(&a->load_shown, target, 0.6f, dt);
    }
}

/* ------------------------------------------------------------------ */
/* Panels                                                              */

static void ps2cfg_probe(App *a, const Game *g);

void app_open_panel(App *a, int panel)
{
    const Game *g = app_selected_game(a, a->tab);
    if ((panel == PANEL_GAME || panel == PANEL_DISC) && !g) return;
    if (panel == PANEL_DISC && g->ndisc < 2) return;
    a->panel = panel;
    a->panel_open = 1;
    if (panel == PANEL_DISC) {
        int i;
        a->nrows = g->ndisc < MENU_MAX_ROWS ? g->ndisc : MENU_MAX_ROWS;
        for (i = 0; i < a->nrows; i++) a->rows[i] = ROW_D_DISC;
    } else {
        a->nrows = menu_rows(panel, a->tab, a->rows, MENU_MAX_ROWS);
    }
    a->flow_group = -1;
    a->row = 0;
    spring_snap(&a->row_hl, 0.0f);
    spring_snap(&a->list_scroll, 0.0f);
    a->page_t = 0.0f; a->page_dir = 1;
    a->panel_plat = a->tab;
    a->panel_game = a->view[a->tab].sel;
    if (g) a->panel_cfg = settings_game_get(&a->settings, lib_key(a->lib, g));
    a->ps2cfg_ok = 0;
    if (g && panel == PANEL_GAME && a->tab == PLAT_PS2) ps2cfg_probe(a, g);
    sound_play(SND_OPEN);
    log_printf("panel: open %s%s%s", panel == PANEL_GAME ? "Game Settings" : panel == PANEL_DISC ? "Choose Disc" : "Flow Settings",
               g && panel != PANEL_FLOW ? " for " : "", g && panel != PANEL_FLOW ? lib_title(a->lib, g) : "");
}

void app_close_panel(App *a)
{
    if (a->panel == PANEL_NONE) return;
    if (a->panel_open) sound_play(SND_CLOSE);
    a->panel_open = 0;
}

/* Make our PARAM.SFO's ITEM_PRIORITY match the setting. Returns 1 if the
 * file was changed, 0 if it already matched, -1 on error (logged). */
static int sync_xmb_priority(App *a)
{
    uint32_t want = a->settings.xmb_top ? SFO_PRIORITY_TOP : SFO_PRIORITY_NORMAL, have = 0;
    int rc;
    if (!a->host.own_sfo_path) return 0;
    rc = sfo_file_get_u32(a->host.own_sfo_path, "ITEM_PRIORITY", &have);
    if (rc != 0) {
        log_printf("xmb: can't read ITEM_PRIORITY from %s (code %d)", a->host.own_sfo_path, rc);
        return -1;
    }
    if (have == want) return 0;
    rc = sfo_file_set_u32(a->host.own_sfo_path, "ITEM_PRIORITY", want);
    if (rc != 0) {
        log_printf("xmb: can't write ITEM_PRIORITY to %s (code %d)", a->host.own_sfo_path, rc);
        return -1;
    }
    log_printf("xmb: ITEM_PRIORITY %u -> %u (%s)", (unsigned)have, (unsigned)want, a->settings.xmb_top ? "pinned to top" : "normal");
    return 1;
}

/* ------------------------------------------------------------------ */
/* PS2 config picker (launch.h explains how webMAN uses CONFIG files)  */

static unsigned char g_cfg_a[64 * 1024], g_cfg_b[64 * 1024];

static App *g_isfile_app;
static int host_is_file(void *ctx, const char *path)
{
    (void)ctx;
    return g_isfile_app && g_isfile_app->host.is_file && g_isfile_app->host.is_file(g_isfile_app->host.fs_ctx, path);
}

/* whole small file -> buf; length, or -1 (missing / unreadable / too big) */
static int cfg_read(const App *a, const char *path, unsigned char *buf, int cap)
{
    int n;
    if (!a->host.read_at) return -1;
    n = a->host.read_at(a->host.fs_ctx, path, 0, buf, (uint32_t)cap);
    return (n < 0 || n >= cap) ? -1 : n;
}

/* the PS2 ID of a disc (from the library, else from the ISO itself) */
static int ps2_disc_id(const App *a, const char *path, const char *lib_id, char *gid, size_t cap)
{
    char raw[32];
    if (lib_id && ps2cfg_game_id(lib_id, gid, cap)) return 1;
    if (a->host.read_at && discid_read(PLAT_PS2, path, 0, a->host.read_at, a->host.fs_ctx, raw, sizeof(raw)))
        return ps2cfg_game_id(raw, gid, cap);
    return 0;
}

/* Is <iso>.CONFIG one of the standard files (a copy we can drop)? */
static int cfg_is_standard(App *a, const char *gid, const unsigned char *data, int len)
{
    int t;
    char src[COVER_PATH_MAX];
    g_isfile_app = a;
    for (t = PS2CFG_CUSTOM; t < PS2CFG_COUNT; t++) {
        int n;
        if (!ps2cfg_find(t, gid, host_is_file, NULL, src, sizeof(src))) continue;
        n = cfg_read(a, src, g_cfg_b, (int)sizeof(g_cfg_b));
        if (n == len && memcmp(g_cfg_b, data, (size_t)len) == 0) return 1;
    }
    return 0;
}

/* What webMAN would use for this disc if spookyflow leaves it to it */
static int cfg_auto_pick(App *a, const char *iso, const char *gid)
{
    char p[COVER_PATH_MAX];
    int n, t;
    static const int order[] = { PS2CFG_APP, PS2CFG_CUSTOM, PS2CFG_NET, PS2CFG_GX, PS2CFG_SOFT };
    g_isfile_app = a;
    snprintf(p, sizeof(p), "%s.CONFIG" PS2CFG_BACKUP_EXT, iso);
    if (host_is_file(NULL, p)) return PS2CFG_OWN;              /* put back on launch */
    snprintf(p, sizeof(p), "%s.CONFIG", iso);
    n = cfg_read(a, p, g_cfg_a, (int)sizeof(g_cfg_a));
    if (n >= 0 && !cfg_is_standard(a, gid, g_cfg_a, n)) return PS2CFG_OWN;
    if (a->ps2cfg_wm_off) return -1;                          /* webMAN won't copy one */
    for (t = 0; t < 5; t++) if (ps2cfg_find(order[t], gid, host_is_file, NULL, p, sizeof(p))) return order[t];
    return -1;
}

static void ps2cfg_probe(App *a, const Game *g)
{
    const Disc *d = lib_game_disc(a->lib, g, 0);
    const char *path = d ? lib_str(a->lib, d->path) : lib_path(a->lib, g);
    const char *lid = lib_str(a->lib, d ? d->id : g->id);
    char p[COVER_PATH_MAX];
    int t;
    a->ps2cfg_ok = 0; a->ps2cfg_avail = 1u; a->ps2cfg_auto = -1; a->ps2cfg_gid[0] = '\0'; a->ps2cfg_wm_off = 0;
    if ((g->flags & GAME_DEMO) || !ps2cfg_is_iso(path)) return;
    if (!a->host.is_file || !a->host.read_at || !a->host.write_file || !a->host.remove_file) return;
    if (!ps2_disc_id(a, path, lid, a->ps2cfg_gid, sizeof(a->ps2cfg_gid))) return;
    {
        WmConfig wm;
        memset(&wm, 0, sizeof(wm));
        if (a->host.read_wm_config) a->host.read_wm_config(&wm);
        a->ps2cfg_wm_off = wm.valid && wm.ps2config_off;
    }
    g_isfile_app = a;
    for (t = PS2CFG_CUSTOM; t < PS2CFG_COUNT; t++)
        if (ps2cfg_find(t, a->ps2cfg_gid, host_is_file, NULL, p, sizeof(p))) a->ps2cfg_avail |= 1u << t;
    a->ps2cfg_auto = cfg_auto_pick(a, path, a->ps2cfg_gid);
    a->ps2cfg_ok = 1;
    log_printf("ps2 config: %s, found:%s%s%s%s%s, Auto gives %s", a->ps2cfg_gid,
               (a->ps2cfg_avail & 2u) ? " CUSTOM" : "", (a->ps2cfg_avail & 4u) ? " NET" : "",
               (a->ps2cfg_avail & 8u) ? " GX" : "", (a->ps2cfg_avail & 16u) ? " SOFT" : "",
               (a->ps2cfg_avail & 32u) ? " PS2-Config-app" : "",
               a->ps2cfg_auto < 0 ? "none" : ps2cfg_name(a->ps2cfg_auto));
}

/* 0.7.5: copies written before spookyflow kept a list of them. Leaving a
 * config that spookyflow set (old > 0): a .CONFIG next to the game's ISOs
 * that is exactly that config is spookyflow's copy from a launch with it. */
static void ps2cfg_adopt_old_copies(App *a, int old)
{
    const Game *g;
    char src[COVER_PATH_MAX], cfg[COVER_PATH_MAX];
    int i, n, m, nd;
    if (old <= 0 || a->panel_game < 0 || a->panel_game >= a->lib->tab[a->panel_plat].count) return;
    g = &a->lib->tab[a->panel_plat].games[a->panel_game];
    g_isfile_app = a;
    if (!a->ps2cfg_gid[0] || !ps2cfg_find(old, a->ps2cfg_gid, host_is_file, NULL, src, sizeof(src))) return;
    m = cfg_read(a, src, g_cfg_b, (int)sizeof(g_cfg_b));
    if (m < 0) return;
    nd = g->ndisc > 0 ? g->ndisc : 1;
    for (i = 0; i < nd; i++) {
        const Disc *d = lib_game_disc(a->lib, g, i);
        const char *path = d ? lib_str(a->lib, d->path) : lib_path(a->lib, g);
        snprintf(cfg, sizeof(cfg), "%s.CONFIG", path);
        if (settings_cfg_copy_has(&a->settings, sf_hash(cfg))) continue;
        n = cfg_read(a, cfg, g_cfg_a, (int)sizeof(g_cfg_a));
        if (n == m && memcmp(g_cfg_a, g_cfg_b, (size_t)n) == 0) {
            settings_cfg_copy_add(&a->settings, sf_hash(cfg));
            log_printf("ps2 config: %s is spookyflow's %s copy (from before 0.7.5)", cfg, ps2cfg_name(old));
        }
    }
}

/* ←/→/✖ on the row: step through Auto and the configs that exist */
static void ps2cfg_cycle(App *a, int dir)
{
    int cur = sf_clampi(a->panel_cfg.ps2cfg, 0, PS2CFG_COUNT - 1), i, d = dir == 0 ? 1 : dir;
    char val[64];
    if (!a->ps2cfg_ok) {
        app_show_toast(a, "PS2 configs work with PS2 ISOs on the internal drive");
        return;
    }
    if (a->ps2cfg_avail == 1u) {
        app_show_toast(a, "No PS2 config files found for this game");
        return;
    }
    for (i = 0; i < PS2CFG_COUNT; i++) {
        cur += d;
        if (cur < 0 || cur >= PS2CFG_COUNT) return;      /* 0.8.2: the ends don't wrap */
        /* 0.8.1: the config Auto already picks isn't listed a second time */
        if (cur > 0 && cur == a->ps2cfg_auto && cur != a->panel_cfg.ps2cfg) continue;
        if (a->ps2cfg_avail & (1u << cur)) break;
    }
    if (cur >= PS2CFG_COUNT || cur == a->panel_cfg.ps2cfg) return;
    ps2cfg_adopt_old_copies(a, a->panel_cfg.ps2cfg);
    a->panel_cfg.ps2cfg = (signed char)cur;
    sound_play(SND_CHANGE);
    if (settings_game_set(&a->settings, &a->panel_cfg) != 0) log_printf("settings: per-game table full, change not stored");
    app_row_value(a, ROW_G_PS2CFG, val, sizeof(val));
    log_printf("game setting: [%s] PS2 Config = %s", a->panel_cfg.key, val);
    app_save(a);
}

void app_row_value(const App *a, RowKind k, char *out, size_t cap)
{
    if (k == ROW_F_BACKGROUND && a->settings.background == BG_CUSTOM) {
        char nm[128], *dot;
        sf_strlcpy(nm, a->settings.bg_image, sizeof(nm));
        dot = strrchr(nm, '.');
        if (dot && dot != nm) *dot = '\0';
        snprintf(out, cap, "Picture: %s", nm);
        return;
    }
    if (k != ROW_G_PS2CFG || a->panel != PANEL_GAME) {
        menu_value_text(k, a->panel_plat, &a->settings, &a->panel_cfg, out, cap);
        return;
    }
    if (!a->ps2cfg_ok) { sf_strlcpy(out, "Not available", cap); return; }
    if (a->panel_cfg.ps2cfg > 0 && (a->ps2cfg_avail & (1u << a->panel_cfg.ps2cfg)))
        sf_strlcpy(out, ps2cfg_name(a->panel_cfg.ps2cfg), cap);
    else if (a->panel_cfg.ps2cfg > 0)
        snprintf(out, cap, "%s (missing)", ps2cfg_name(a->panel_cfg.ps2cfg));
    else if (a->ps2cfg_auto < 0)
        sf_strlcpy(out, a->ps2cfg_wm_off ? "Auto (off in webMAN)" : "Auto (none found)", cap);
    else
        snprintf(out, cap, "Auto (%s)", ps2cfg_name(a->ps2cfg_auto));
}

static int cfg_copy(App *a, const char *src, const char *dst)
{
    int n = cfg_read(a, src, g_cfg_a, (int)sizeof(g_cfg_a));
    if (n < 0) return -1;
    return a->host.write_file(dst, g_cfg_a, (uint32_t)n);
}

/* Before a PS2 ISO launches: put the chosen config next to it as
 * <iso>.CONFIG, or for Auto take spookyflow's copy away again. */
static void ps2cfg_apply(App *a, const char *iso, const char *lib_id, int type)
{
    char gid[16], cfg[COVER_PATH_MAX], bak[COVER_PATH_MAX], src[COVER_PATH_MAX];
    int n, mine;
    if (!ps2cfg_is_iso(iso) || !a->host.is_file || !a->host.read_at || !a->host.write_file || !a->host.remove_file) return;
    if (!ps2_disc_id(a, iso, lib_id, gid, sizeof(gid))) { log_printf("ps2 config: no ID for %s, left to webMAN", iso); return; }
    g_isfile_app = a;
    snprintf(cfg, sizeof(cfg), "%s.CONFIG", iso);
    snprintf(bak, sizeof(bak), "%s.CONFIG" PS2CFG_BACKUP_EXT, iso);
    if (type > 0 && !ps2cfg_find(type, gid, host_is_file, NULL, src, sizeof(src))) {
        log_printf("ps2 config: %s has no %s config any more, using Auto", gid, ps2cfg_name(type));
        type = PS2CFG_AUTO;
    }
    n = cfg_read(a, cfg, g_cfg_a, (int)sizeof(g_cfg_a));
    mine = settings_cfg_copy_has(&a->settings, sf_hash(cfg));
    if (type == PS2CFG_AUTO) {
        if (host_is_file(NULL, bak)) {                       /* the user's own file comes back */
            if (cfg_copy(a, bak, cfg) == 0) {
                a->host.remove_file(bak);
                settings_cfg_copy_remove(&a->settings, sf_hash(cfg));
                log_printf("ps2 config: %s restored", cfg);
            }
        } else if (n >= 0 && mine) {
            /* 0.7.5: only a copy spookyflow wrote itself is taken away */
            if (a->host.remove_file(cfg) == 0) log_printf("ps2 config: Auto, removed spookyflow's copy %s (webMAN picks)", cfg);
            settings_cfg_copy_remove(&a->settings, sf_hash(cfg));
        } else if (n >= 0) {
            log_printf("ps2 config: Auto, %s is not spookyflow's: left as it is", cfg);
        }
        app_save(a);
        return;
    }
    if (n >= 0 && !mine && !host_is_file(NULL, bak)) {
        /* a config that is not spookyflow's (the user's, the PS2 Config
         * app's, ManaGunZ's...): keep it aside, it comes back with Auto */
        if (a->host.write_file(bak, g_cfg_a, (uint32_t)n) != 0) {
            log_printf("ps2 config: can't keep %s aside, not changing it", cfg);
            return;
        }
        log_printf("ps2 config: your %s kept as %s", cfg, bak);
    }
    if (cfg_copy(a, src, cfg) == 0) {
        settings_cfg_copy_add(&a->settings, sf_hash(cfg));
        log_printf("ps2 config: %s -> %s", src, cfg);
    } else {
        log_printf("ps2 config: couldn't write %s", cfg);
    }
    app_save(a);
}

/* Flow Settings groups: ✖ on a group opens its page, ◯ comes back */
static void flow_enter_group(App *a, RowKind g)
{
    a->flow_top_row = a->row;
    a->flow_group = (int)g;
    a->nrows = menu_group_rows(g, a->rows, MENU_MAX_ROWS);
    a->row = 0;
    spring_snap(&a->row_hl, 0.0f);
    spring_snap(&a->list_scroll, 0.0f);
    a->page_t = 0.0f; a->page_dir = 1;
    log_printf("panel: Flow Settings > %s", menu_label(g));
}

static void flow_leave_group(App *a)
{
    a->flow_group = -1;
    a->nrows = menu_rows(PANEL_FLOW, a->tab, a->rows, MENU_MAX_ROWS);
    a->row = sf_clampi(a->flow_top_row, 0, a->nrows - 1);
    spring_snap(&a->row_hl, (float)a->row);
    spring_snap(&a->list_scroll, 0.0f);
    a->page_t = 0.0f; a->page_dir = -1;
    sound_play(SND_CLOSE);
}

static void apply_fx(App *a, RowKind k, int fx)
{
    char val[48];
    if (fx == MENU_FX_CHANGED || fx == MENU_FX_RESORT || fx == MENU_FX_XMB_TOP) sound_play(SND_CHANGE);
    else if (fx != MENU_FX_NONE && fx != MENU_FX_QUIT && fx != MENU_FX_RESCAN && fx != MENU_FX_POWEROFF) sound_play(SND_CONFIRM);
    switch (fx) {
    case MENU_FX_NONE: return;
    case MENU_FX_CHANGED:
        menu_value_text(k, a->panel_plat, &a->settings, &a->panel_cfg, val, sizeof(val));
        if (a->panel == PANEL_GAME) {
            if (settings_game_set(&a->settings, &a->panel_cfg) != 0)
                log_printf("settings: per-game table full, change not stored");
            log_printf("game setting: [%s] %s = %s", a->panel_cfg.key, menu_label(k), val);
        } else {
            log_printf("flow setting: %s = %s", menu_label(k), val);
        }
        app_save(a);
        break;
    case MENU_FX_RESORT:
        log_printf("flow setting: Ignore \"The\" = %s", a->settings.ignore_the ? "On" : "Off");
        sort_all(a, 1);
        a->info_tab = a->tab; a->info_idx = a->view[a->tab].sel;
        app_save(a);
        break;
    case MENU_FX_RESCAN:
        log_printf("action: rescan library");
        app_close_panel(a);
        a->panel = PANEL_NONE; a->panel_anim = 0.0f;
        start_loading(a, 1);
        break;
    case MENU_FX_QUIT:
        app_request_exit(a, "Quit to XMB row");
        break;
    case MENU_FX_POWEROFF:      /* one press: no confirmation */
        app_save(a);
        if (a->host.power_off && a->host.power_off() == 0) {
            a->poweroff = 1;
            app_request_exit(a, "Power off PS3 row");
        } else {
            log_printf("power off: not available");
            app_show_toast(a, "Power off needs webMAN MOD running");
        }
        break;
    case MENU_FX_COVERS_STUB:
        log_printf("action: download missing covers (not in milestone 1)");
        app_show_toast(a, "Cover download arrives in a later milestone");
        break;
    case MENU_FX_XMB_TOP:
        log_printf("flow setting: Pin to top of XMB = %s", a->settings.xmb_top ? "On" : "Off");
        app_save(a);
        if (sync_xmb_priority(a) < 0) app_show_toast(a, "Couldn't update the XMB position (see log)");   /* only failures say anything */
        break;
    case MENU_FX_PS2CFG:        /* handled in panel_input (needs the direction) */
        break;
    case MENU_FX_GROUP:
        flow_enter_group(a, k);
        break;
    case MENU_FX_ABOUT:
        a->about = 1;
        log_printf("panel: About spookyflow");
        break;
    }
}

static void launch_disc(App *a, int disc);

static void panel_input(App *a)
{
    const Input *in = &a->input;
    RowKind k;
    int vis = app_panel_visible_rows(a), first;
    if (!a->panel_open || a->nrows <= 0) return;
    if (a->about) {                          /* the About card: only ◯ (back to Flow Settings) */
        if (in->pressed & SFB_CIRCLE) { a->about = 0; sound_play(SND_CLOSE); }
        return;
    }
    if (in->pressed & SFB_CIRCLE) {
        if (a->panel == PANEL_FLOW && a->flow_group >= 0) { flow_leave_group(a); return; }   /* back to the groups */
        app_close_panel(a);
        return;
    }
    /* only ◯ goes back (START / ▲ don't close the panel they opened) */
    /* Settings menus: D-pad only (the stick is ignored), one step per press */
    {
        int before = a->row;
        if (in->pressed & SFB_UP) a->row = sf_clampi(a->row - 1, 0, a->nrows - 1);
        if (in->pressed & SFB_DOWN) a->row = sf_clampi(a->row + 1, 0, a->nrows - 1);
        if (a->row != before) sound_play(SND_MOVE);
    }
    k = a->rows[a->row];
    if (a->panel == PANEL_DISC) {
        if (in->pressed & SFB_CROSS) { launch_disc(a, a->row); app_close_panel(a); }
    } else {
        /* 0.8.2: values change with <- / -> only; X opens a group or runs an action */
        if (k == ROW_G_PS2CFG) {
            if (in->pressed & SFB_LEFT) ps2cfg_cycle(a, -1);
            if (in->pressed & SFB_RIGHT) ps2cfg_cycle(a, +1);
        } else if (k == ROW_F_BACKGROUND) {
            if (in->pressed & SFB_LEFT) background_cycle(a, -1);
            if (in->pressed & SFB_RIGHT) background_cycle(a, +1);
        } else if (!menu_is_action(k)) {
            if (in->pressed & SFB_LEFT) apply_fx(a, k, menu_change(k, a->panel_plat, -1, &a->settings, &a->panel_cfg));
            if (in->pressed & SFB_RIGHT) apply_fx(a, k, menu_change(k, a->panel_plat, +1, &a->settings, &a->panel_cfg));
        } else if (in->pressed & SFB_CROSS) {
            apply_fx(a, k, menu_change(k, a->panel_plat, 0, &a->settings, &a->panel_cfg));
        }
    }
    if (a->mode != MODE_FLOW) return;      /* rescan switched modes */
    spring_set(&a->row_hl, (float)a->row);
    first = (int)(a->list_scroll.target + 0.5f);
    if (a->row < first) first = a->row;
    if (a->row >= first + vis) first = a->row - vis + 1;
    first = sf_clampi(first, 0, a->nrows > vis ? a->nrows - vis : 0);
    spring_set(&a->list_scroll, (float)first);
}

/* ------------------------------------------------------------------ */
/* Cover flow                                                          */

/* disc < 0: a single-disc game. Sends the webMAN request (launch.h) and
 * exits to the XMB, where webMAN mounts the game and starts it. */
static void launch_disc(App *a, int disc)
{
    const Game *g = app_selected_game(a, a->tab);
    const Disc *d;
    GameCfg c;
    LaunchReq req;
    WmConfig wm;
    char name[TITLE_MAX + 16], cmd[LAUNCH_CMD_MAX];
    const char *path;
    if (!g) return;
    if (g->flags & GAME_DEMO) { app_show_toast(a, "Demo game: add your games to launch them"); return; }
    d = lib_game_disc(a->lib, g, disc);
    path = d ? lib_str(a->lib, d->path) : lib_path(a->lib, g);
    if (d) {
        char dn[TITLE_MAX];
        lib_disc_name(a->lib, d, disc, dn, sizeof(dn));
        snprintf(name, sizeof(name), "%s (%s)", lib_title(a->lib, g), dn);
    }
    else sf_strlcpy(name, lib_title(a->lib, g), sizeof(name));
    c = settings_game_get(&a->settings, lib_key(a->lib, g));   /* one set of settings for all discs */
    memset(&req, 0, sizeof(req));
    req.plat = a->tab;
    req.path = path;
    req.autoplay = settings_autoplay_effective(&a->settings, &c);
    req.offline = a->tab != PLAT_PS1 && settings_offline_effective(&a->settings, &c);
    req.emulator = c.emulator;
    memset(&wm, 0, sizeof(wm));
    if (a->host.read_wm_config) a->host.read_wm_config(&wm);
    if (launch_build(&req, &wm, cmd, sizeof(cmd)) != 0) {
        log_printf("launch: can't build the request for \"%s\"", path);
        app_show_toast(a, "Can't launch this game (path too long?)");
        return;
    }
    log_printf("launch: %s \"%s\" autoplay=%s offline=%d emulator=%d webMAN config=%s",
               k_plat_name[a->tab], name, req.autoplay ? "on" : "off", req.offline, req.emulator,
               wm.valid ? (wm.autoplay ? "read (its Auto-play is on)" : "read") : "not found");
    log_printf("launch: request %s", cmd);
    if (a->tab == PLAT_PS2 && a->host.launch)
        ps2cfg_apply(a, path, lib_str(a->lib, d ? d->id : g->id), c.ps2cfg);
    if (!a->host.launch || a->host.launch(cmd) != 0) {
        app_show_toast(a, "Launching needs webMAN MOD running");
        return;
    }
    sound_play(SND_CONFIRM);
    sf_strlcpy(a->last_launch, name, sizeof(a->last_launch));   /* no pop-up on launch */
    a->launched = 1;
    app_request_exit(a, "launch");
}

static void launch(App *a)
{
    const Game *g = app_selected_game(a, a->tab);
    if (!g) return;
    if (g->ndisc > 1) app_open_panel(a, PANEL_DISC);   /* choose which disc */
    else launch_disc(a, -1);
}

static void switch_tab(App *a, int dir)
{
    int nt = a->tab + dir;
    if (nt < 0 || nt >= PLAT_COUNT) return;     /* tabs stop at PS1 and PS3, no wrap */
    a->tab_prev = a->tab;
    a->tab = nt;
    a->tab_dir = dir;
    a->tab_anim = 0.0f;
    spring_set(&a->tab_hl, (float)a->tab);
    a->letter_t = LETTER_TIME;
    sound_play(SND_TAB);
}

static void show_letter(App *a, int idx)
{
    const Game *g = &a->lib->tab[a->tab].games[idx];
    int still = a->letter_t < LETTER_TIME && a->letter[0];   /* jumping again while it shows: glide from there */
    a->letter_prev_bucket = still ? a->letter_bucket : g->bucket;
    a->letter_bucket = g->bucket;
    a->letter[0] = title_bucket_char(g->bucket);
    a->letter[1] = '\0';
    a->letter_t = 0.0f;
    if (!still) a->letter_in = 0.0f;
}

/* 0.8.4: holding the left stick scrolls the row continuously instead of in
 * steps (steps at ~28/s landed 2 or 3 frames apart and looked choppy). The
 * first push is one game; after REPEAT_DELAY the row glides, speeding up
 * from 1/REPEAT_SLOW to 1/REPEAT_FAST games a second over REPEAT_ACCEL;
 * the selection is the game in the middle; on release the row eases onto
 * the nearest game, keeping its speed for a smooth stop. */
static void stick_glide(App *a, TabView *v, const GameList *gl, const Input *in)
{
    int dir = (in->held & SFB_STICK_RIGHT) ? 1 : (in->held & SFB_STICK_LEFT) ? -1 : 0;
    float dt = a->input_dt, speed, k;
    if (dir == 0 || (v->glide && dir != v->glide) || a->display) {
        if (v->glide) {
            /* let go: coast to a stop on a game. A critically damped spring
             * started at speed v0 doesn't overshoot when its target is at
             * least v0 / omega ahead: stop on the first game past that. */
            float spd = v->scroll.v, ahead = v->glide_pos + spd / SPRING_OMEGA_300MS;
            int stop = sf_clampi(spd >= 0.0f ? (int)ceilf(ahead - 0.001f) : (int)floorf(ahead + 0.001f), 0, gl->count - 1);
            v->glide = 0;
            if (stop != v->sel) { v->sel = stop; sound_play(SND_MOVE); }
            v->step_rate = 0.0f;
            v->step_t = 0.0;
            spring_set(&v->scroll, (float)v->sel);
            v->scroll.omega = SPRING_OMEGA_300MS;
            v->scroll.v = spd;
        }
        v->glide_hold = 0.0f;
        if (dir == 0) return;
    }
    if (in->pressed & (SFB_STICK_LEFT | SFB_STICK_RIGHT)) { v->glide_hold = 0.0f; return; }   /* the first push: one game */
    v->glide_hold += dt;
    if (v->glide_hold < REPEAT_DELAY) return;
    k = sf_clampf((v->glide_hold - REPEAT_DELAY) / REPEAT_ACCEL, 0.0f, 1.0f);
    speed = 1.0f / (REPEAT_SLOW + (REPEAT_FAST - REPEAT_SLOW) * k);
    if (!v->glide) { v->glide = dir; v->glide_pos = v->scroll.x; }
    v->glide_pos = sf_clampf(v->glide_pos + (float)dir * speed * dt, 0.0f, (float)(gl->count - 1));
    {
        int now = sf_clampi((int)floorf(v->glide_pos + 0.5f), 0, gl->count - 1);
        if (now != v->sel) { v->sel = now; sound_play(SND_MOVE); }
    }
    /* the row is at the glide position, moving at its speed */
    v->scroll.x = v->scroll.target = v->glide_pos;
    v->scroll.v = (v->glide_pos > 0.0f && v->glide_pos < (float)(gl->count - 1)) ? (float)dir * speed : 0.0f;
    v->step_t = a->time;
    v->step_rate = speed;
}

static void flow_input(App *a)
{
    const Input *in = &a->input;
    const GameList *gl = &a->lib->tab[a->tab];
    TabView *v = &a->view[a->tab];
    if (a->display) {
        /* display mode: only Launch, Game Settings and Back (◯ in Game
         * Settings comes back here) */
        if (in->pressed & SFB_CIRCLE) { a->display = 0; sound_play(SND_CLOSE); return; }
        if ((in->pressed & SFB_CROSS) && gl->count > 0) launch(a);
        else if ((in->pressed & SFB_TRIANGLE) && gl->count > 0) app_open_panel(a, PANEL_GAME);
        return;
    }
    if ((in->pressed & SFB_SQUARE) && gl->count > 0) { a->display = 1; sound_play(SND_OPEN); return; }
    if (in->pressed & SFB_L1) { switch_tab(a, -1); return; }
    if (in->pressed & SFB_R1) { switch_tab(a, +1); return; }
    if (gl->count > 0) {
        /* D-pad: one game per press, holding does not repeat.
         * Left stick: holding scrolls with accelerating repeat (as before).
         * Menus repeat for both. */
        {
            uint32_t go_l = (in->pressed & SFB_LEFT) | (in->pressed & SFB_STICK_LEFT);
            uint32_t go_r = (in->pressed & SFB_RIGHT) | (in->pressed & SFB_STICK_RIGHT);
            if (go_l && v->sel > 0) { app_select(a, a->tab, v->sel - 1, 1); sound_play(SND_MOVE); }
            if (go_r && v->sel < gl->count - 1) { app_select(a, a->tab, v->sel + 1, 1); sound_play(SND_MOVE); }
        }
        stick_glide(a, v, gl, in);
        if (in->pressed & SFB_L2) {
            int to = lib_letter_prev(gl, v->sel);
            if (to != v->sel) { app_select(a, a->tab, to, 1); show_letter(a, v->sel); sound_play(SND_TAB); }
        }
        if (in->pressed & SFB_R2) {
            int to = lib_letter_next(gl, v->sel);
            if (to != v->sel) { app_select(a, a->tab, to, 1); show_letter(a, v->sel); sound_play(SND_TAB); }
        }
        if (in->pressed & SFB_CROSS) launch(a);
        if (in->pressed & SFB_TRIANGLE) app_open_panel(a, PANEL_GAME);
    }
    if (in->pressed & SFB_START) app_open_panel(a, PANEL_FLOW);
    if (in->pressed & SFB_SELECT) music_skip(a);
}

static void animate(App *a, float dt)
{
    int t;
    for (t = 0; t < PLAT_COUNT; t++) {
        TabView *v = &a->view[t];
        /* a critically damped spring trails a steady scroll by 2 * rate / omega */
        float rate = (a->time - v->step_t < FLOW_STEP_WINDOW) ? v->step_rate : 0.0f;
        if (v->glide) continue;          /* the stick moves the row itself (stick_glide) */
        v->scroll.omega = sf_clampf(2.0f * rate / FLOW_MAX_LAG, SPRING_OMEGA_300MS, FLOW_OMEGA_MAX);
        spring_update(&v->scroll, dt);
    }
    anim_approach(&a->tab_anim, 1.0f, 1.0f / TAB_ANIM_TIME, dt);
    spring_update(&a->tab_hl, dt);
    spring_update(&a->row_hl, dt);
    spring_update(&a->list_scroll, dt);
    spring_update(&a->title_w, dt);
    anim_approach(&a->intro, 1.0f, 1.0f / INTRO_TIME, dt);
    anim_approach(&a->panel_anim, a->panel_open ? 1.0f : 0.0f, 1.0f / PANEL_ANIM_TIME, dt);
    anim_approach(&a->display_t, a->display ? 1.0f : 0.0f, 1.0f / DISPLAY_TIME, dt);
    if (!a->panel_open) a->about = 0;
    anim_approach(&a->about_t, a->about ? 1.0f : 0.0f, 1.0f / ABOUT_TIME, dt);
    if (a->page_t < 5.0f) a->page_t += dt;
    if (!a->panel_open && a->panel_anim <= 0.0f) a->panel = PANEL_NONE;
    if (a->letter_t < LETTER_TIME) a->letter_t += dt;
    if (a->letter_in < 1.0f) a->letter_in += dt;
    if (a->toast_t < TOAST_TIME) a->toast_t += dt;
    if (a->music_card_t < a->music_card_len) a->music_card_t += dt;
    a->flow_t += dt;
    if (a->music_swap_t < 1.0f) a->music_swap_t = sf_minf(1.0f, a->music_swap_t + dt / MUSIC_SWAP_TIME);
    /* title/chips cross-fade on selection change */
    {
        int cur = a->view[a->tab].sel;
        if (cur != a->info_idx || a->tab != a->info_tab) {
            a->prev_tab = a->info_tab; a->prev_idx = a->info_idx;
            a->info_tab = a->tab; a->info_idx = cur;
            a->info_t = 0.0f;
        }
        anim_approach(&a->info_t, 1.0f, 1.0f / XFADE_TIME, dt);
    }
    if (a->host.covers) {
        cover_set_focus(a->host.covers, a->tab, a->view[a->tab].sel);
        cover_poll(a->host.covers, &a->covers);
    }
    {
        int pt;
        for (pt = 0; pt < PLAT_COUNT; pt++) cc_set_side(&a->covers, pt, a->view[pt].sel, a->lib->tab[pt].count);
    }
    cc_update(&a->covers, a->tab, a->view[a->tab].sel, a->lib->tab[a->tab].count);
}

void app_update(App *a, uint32_t held, float dt)
{
    int main_ctx;
    if (dt < 0.0f) dt = 0.0f;
    if (dt > 0.1f) dt = 0.1f;
    a->time += dt;
    log_set_time(a->time);

    /* sleep / wake */
    if (held) a->idle_t = 0.0f; else a->idle_t += dt;
    if (a->asleep && held) {
        a->asleep = 0;
        a->wake_block = 1;
        log_printf("wake (button pressed)");
    }
    if (a->wake_block) {
        if (held) held = 0; else a->wake_block = 0;
    }
    if (!a->asleep && a->mode == MODE_FLOW && !a->exiting && a->settings.sleep_min > 0 &&
        a->idle_t >= (float)a->settings.sleep_min * 60.0f) {
        a->asleep = 1;
        log_printf("sleep (no input for %d min)", a->settings.sleep_min);
    }
    anim_approach(&a->sleep_t, a->asleep ? 1.0f : 0.0f, 1.0f / (a->asleep ? SLEEP_FADE_TIME : WAKE_FADE_TIME), dt);

    input_update(&a->input, held, dt);
    a->input_dt = dt;
    music_tick(a);

    if (a->exiting) {
        anim_approach(&a->exit_fade, 1.0f, 1.0f / EXIT_FADE_TIME, dt);
        if (a->mode == MODE_FLOW) animate(a, dt);
        if (a->exit_fade >= 1.0f && !a->quit) { a->quit = 1; log_printf("exit: fade complete, leaving main loop"); }
        return;
    }
    sound_set_enabled(a->settings.sfx_volume > 0);
    sound_set_volume((float)a->settings.sfx_volume / 100.0f);
    if (a->mode == MODE_LOADING) { loading_update(a, dt); return; }

    /* evaluated BEFORE handling this frame's input, so a ◯ press that closes a
     * panel cannot arm the exit hold */
    main_ctx = app_main_context(a);
    if (hold_exit_update(&a->hold, (a->input.pressed & SFB_CIRCLE) != 0, (a->input.held & SFB_CIRCLE) != 0, main_ctx, dt)) {
        app_request_exit(a, "held circle");
        sound_play(SND_CLOSE);
        return;
    }
    if (a->panel != PANEL_NONE) panel_input(a);
    else flow_input(a);
    if (a->mode == MODE_FLOW) animate(a, dt);
}
