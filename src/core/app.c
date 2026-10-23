/* app.c - portable application state machine. */
#include "app.h"
#include "sfo.h"
#include "sound.h"
#include "log.h"
#include "title.h"
#include "util.h"
#include <stdio.h>
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

int app_main_context(const App *a)
{
    return a->mode == MODE_FLOW && a->panel == PANEL_NONE && !a->exiting;
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
    v->sel = sf_clampi(index, 0, gl->count - 1);
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
    if (a->panel != PANEL_NONE) a->panel_open = 0;
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
    drop_covers(a);
    /* "Rescan Library" also refreshes covers (new art, a new cover pack) */
    if (rescan && a->host.covers) cover_wipe_cache(a->host.covers);
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
static void scan_worker(void *arg)
{
    App *a = (App *)arg;
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
        lib_clear(a->lib);
        scanner_begin(&a->scanner, a->host.roots, a->host.nroots);
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
        a->load_target = 0.15f;
        a->load_step = LOAD_SCAN;
        break;
    case LOAD_SCAN:
        if (a->scan_async) {
            if (a->scan_busy) {
                snprintf(a->load_status, sizeof(a->load_status), "Scanning %s games\xE2\x80\xA6",
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
            log_printf("scan: done, %d dirs, found PS1=%d PS2=%d PS3=%d",
                       a->scanner.dirs_listed, a->scanner.found[0], a->scanner.found[1], a->scanner.found[2]);
            sf_strlcpy(a->load_status, "Sorting\xE2\x80\xA6", sizeof(a->load_status));
            a->load_target = 0.90f;
            a->load_step = LOAD_SORT;
        }
        break;
    case LOAD_SORT:
        finish_library(a);
        sf_strlcpy(a->load_status, "Doing the Heavy Lifting", sizeof(a->load_status));
        a->load_target = 1.0f;
        a->load_step = LOAD_READY;
        break;
    case LOAD_READY:
        /* the loading screen can't be skipped: it always runs its full time */
        if (a->load_t >= (a->rescan ? RESCAN_MIN_TIME : LOAD_MIN_TIME) && a->load_shown >= 0.999f) {
            a->mode = MODE_FLOW;
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

void app_open_panel(App *a, int panel)
{
    const Game *g = app_selected_game(a, a->tab);
    if (panel == PANEL_GAME && !g) return;
    a->panel = panel;
    a->panel_open = 1;
    a->nrows = menu_rows(panel, a->tab, a->rows, MENU_MAX_ROWS);
    a->row = 0;
    spring_snap(&a->row_hl, 0.0f);
    spring_snap(&a->list_scroll, 0.0f);
    a->panel_plat = a->tab;
    a->panel_game = a->view[a->tab].sel;
    if (g) a->panel_cfg = settings_game_get(&a->settings, lib_key(a->lib, g));
    sound_play(SND_OPEN);
    log_printf("panel: open %s%s%s", panel == PANEL_GAME ? "Game Settings" : "Flow Settings",
               g && panel == PANEL_GAME ? " for " : "", g && panel == PANEL_GAME ? lib_title(a->lib, g) : "");
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

static void apply_fx(App *a, RowKind k, int fx)
{
    char val[48];
    if (fx == MENU_FX_CHANGED || fx == MENU_FX_RESORT || fx == MENU_FX_XMB_TOP) sound_play(SND_CHANGE);
    else if (fx != MENU_FX_NONE && fx != MENU_FX_QUIT && fx != MENU_FX_RESCAN) sound_play(SND_CONFIRM);
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
    case MENU_FX_POWEROFF:
        if (a->poweroff_arm > 0.0f) {
            a->poweroff_arm = 0.0f;
            app_save(a);
            if (a->host.power_off && a->host.power_off() == 0) {
                a->poweroff = 1;
                app_request_exit(a, "Power off PS3 row");
            } else {
                log_printf("power off: not available");
                app_show_toast(a, "Power off needs webMAN MOD running");
            }
        } else {
            a->poweroff_arm = POWEROFF_CONFIRM_TIME;
            app_show_toast(a, "Press again to power off");
        }
        break;
    case MENU_FX_COVERS_STUB:
        log_printf("action: download missing covers (not in milestone 1)");
        app_show_toast(a, "Cover download arrives in a later milestone");
        break;
    case MENU_FX_XMB_TOP:
        log_printf("flow setting: Pin to top of XMB = %s", a->settings.xmb_top ? "On" : "Off");
        app_save(a);
        if (sync_xmb_priority(a) < 0) app_show_toast(a, "Couldn't update the XMB position (see log)");
        else app_show_toast(a, a->settings.xmb_top ? "Pinned to the top of the XMB" : "No longer pinned in the XMB");
        break;
    case MENU_FX_PS2CFG_STUB:
        log_printf("action: PS2 config editor (not in milestone 1) for [%s]", a->panel_cfg.key);
        app_show_toast(a, "PS2 config editor arrives in a later milestone");
        break;
    }
}

static void panel_input(App *a)
{
    const Input *in = &a->input;
    RowKind k;
    int vis = PANEL_VISIBLE_ROWS, first;
    if (!a->panel_open || a->nrows <= 0) return;
    if (in->pressed & SFB_CIRCLE) { app_close_panel(a); return; }
    if ((in->pressed & SFB_START) && a->panel == PANEL_FLOW) { app_close_panel(a); return; }
    if ((in->pressed & SFB_TRIANGLE) && a->panel == PANEL_GAME) { app_close_panel(a); return; }
    /* Settings menus: D-pad only (the stick is ignored), one step per press */
    {
        int before = a->row;
        if (in->pressed & SFB_UP) a->row = sf_clampi(a->row - 1, 0, a->nrows - 1);
        if (in->pressed & SFB_DOWN) a->row = sf_clampi(a->row + 1, 0, a->nrows - 1);
        if (a->row != before) sound_play(SND_MOVE);
    }
    if (in->pressed & (SFB_UP | SFB_DOWN)) a->poweroff_arm = 0.0f;
    k = a->rows[a->row];
    if (!menu_is_action(k)) {
        if (in->pressed & SFB_LEFT) apply_fx(a, k, menu_change(k, a->panel_plat, -1, &a->settings, &a->panel_cfg));
        if (in->pressed & SFB_RIGHT) apply_fx(a, k, menu_change(k, a->panel_plat, +1, &a->settings, &a->panel_cfg));
    }
    if (in->pressed & SFB_CROSS) apply_fx(a, k, menu_change(k, a->panel_plat, 0, &a->settings, &a->panel_cfg));
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

static void launch(App *a)
{
    const Game *g = app_selected_game(a, a->tab);
    GameCfg c;
    char msg[160];
    if (!g) return;
    c = settings_game_get(&a->settings, lib_key(a->lib, g));
    log_printf("LAUNCH (stub, milestone 1): %s \"%s\" path=\"%s\" id=%s autoplay=%s emulator=%d offline=%d rsx=%d",
               k_plat_name[a->tab], lib_title(a->lib, g), lib_path(a->lib, g),
               lib_id(a->lib, g)[0] ? lib_id(a->lib, g) : "-",
               settings_autoplay_effective(&a->settings, &c) ? "on" : "off", c.emulator, settings_offline_effective(&a->settings, &c), c.rsx);
    sound_play(SND_CONFIRM);
    snprintf(msg, sizeof(msg), "Launch: %s", lib_title(a->lib, g));
    app_show_toast(a, msg);
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
    a->letter[0] = title_bucket_char(g->bucket);
    a->letter[1] = '\0';
    a->letter_t = 0.0f;
}

static void flow_input(App *a)
{
    const Input *in = &a->input;
    const GameList *gl = &a->lib->tab[a->tab];
    TabView *v = &a->view[a->tab];
    if (in->pressed & SFB_L1) { switch_tab(a, -1); return; }
    if (in->pressed & SFB_R1) { switch_tab(a, +1); return; }
    if (gl->count > 0) {
        /* D-pad: one game per press, holding does not repeat.
         * Left stick: holding scrolls with accelerating repeat (as before).
         * Menus repeat for both. */
        {
            uint32_t go_l = (in->pressed & SFB_LEFT) | (in->fired & SFB_STICK_LEFT);
            uint32_t go_r = (in->pressed & SFB_RIGHT) | (in->fired & SFB_STICK_RIGHT);
            if (go_l && v->sel > 0) { app_select(a, a->tab, v->sel - 1, 1); sound_play(SND_MOVE); }
            if (go_r && v->sel < gl->count - 1) { app_select(a, a->tab, v->sel + 1, 1); sound_play(SND_MOVE); }
        }
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
}

static void animate(App *a, float dt)
{
    int t;
    for (t = 0; t < PLAT_COUNT; t++) spring_update(&a->view[t].scroll, dt);
    anim_approach(&a->tab_anim, 1.0f, 1.0f / TAB_ANIM_TIME, dt);
    spring_update(&a->tab_hl, dt);
    spring_update(&a->row_hl, dt);
    spring_update(&a->list_scroll, dt);
    spring_update(&a->title_w, dt);
    anim_approach(&a->intro, 1.0f, 1.0f / INTRO_TIME, dt);
    anim_approach(&a->panel_anim, a->panel_open ? 1.0f : 0.0f, 1.0f / PANEL_ANIM_TIME, dt);
    if (!a->panel_open && a->panel_anim <= 0.0f) a->panel = PANEL_NONE;
    if (a->letter_t < LETTER_TIME) a->letter_t += dt;
    if (a->toast_t < TOAST_TIME) a->toast_t += dt;
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
    if (!a->asleep && a->mode == MODE_FLOW && !a->exiting && a->idle_t >= SLEEP_TIME) {
        a->asleep = 1;
        log_printf("sleep (no input for %.0f s)", (double)SLEEP_TIME);
    }
    anim_approach(&a->sleep_t, a->asleep ? 1.0f : 0.0f, 1.0f / (a->asleep ? SLEEP_FADE_TIME : WAKE_FADE_TIME), dt);

    input_update(&a->input, held, dt);

    if (a->exiting) {
        anim_approach(&a->exit_fade, 1.0f, 1.0f / EXIT_FADE_TIME, dt);
        if (a->mode == MODE_FLOW) animate(a, dt);
        if (a->exit_fade >= 1.0f && !a->quit) { a->quit = 1; log_printf("exit: fade complete, leaving main loop"); }
        return;
    }
    sound_set_enabled(a->settings.sfx_volume > 0);
    sound_set_volume((float)a->settings.sfx_volume / 100.0f);
    if (a->mode == MODE_LOADING) { loading_update(a, dt); return; }
    if (a->poweroff_arm > 0.0f) {
        a->poweroff_arm -= dt;
        if (a->poweroff_arm < 0.0f || a->panel == PANEL_NONE) a->poweroff_arm = 0.0f;
    }

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
