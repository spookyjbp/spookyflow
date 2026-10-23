/* app.h - portable application state machine (loading, flow, panels). */
#ifndef SF_APP_H
#define SF_APP_H

#include "anim.h"
#include "cover_cache.h"
#include "coverart.h"
#include "input.h"
#include "library.h"
#include "menu.h"
#include "scanner.h"
#include "settings.h"

#define APP_NAME        "spookyflow"
#define APP_VERSION     "0.2.10 (milestone 2, fix 10)"

#define LOAD_MIN_TIME   5.0f    /* startup loading screen stays at least this long: many monitors stay black
                                   for 2-3 s after the PS3 switches video mode. Not skippable. */
#define POWEROFF_CONFIRM_TIME 3.0f   /* seconds to press ✖ a second time */
#define RESCAN_MIN_TIME 1.2f    /* "Rescan library" has no video mode switch */
#define INTRO_TIME      0.40f   /* loading -> flow cross-fade */
#define TAB_ANIM_TIME   0.30f
#define PANEL_ANIM_TIME 0.20f
#define XFADE_TIME      0.18f
#define LETTER_TIME     1.00f
#define TOAST_TIME      2.20f
#define EXIT_FADE_TIME  0.30f
#define PANEL_VISIBLE_ROWS 6
#define SLEEP_TIME      300.0f  /* default idle time before the screen dims (Flow Settings > Sleep Mode) */
#define SLEEP_DIM       0.72f   /* how dark sleep gets: the screen stays visible */
#define SLEEP_FADE_TIME 1.5f
#define WAKE_FADE_TIME  0.25f

typedef enum { MODE_LOADING = 0, MODE_FLOW = 1 } AppMode;

typedef enum {
    LOAD_START = 0, LOAD_SETTINGS, LOAD_PREPARE, LOAD_SCAN, LOAD_SORT, LOAD_READY
} LoadStep;

/* Host services (filled by src/ps3/main.c or the preview tool). */
typedef struct {
    FsListFn fs_list;
    void *fs_ctx;
    const char *settings_path;     /* NULL = don't persist */
    const ScanRoot *roots;          /* NULL = scanner_default_roots() */
    int nroots;
    void (*prepare)(void);          /* optional: extra GPU setup during loading */
    /* optional: run fn(arg) on a background thread; return 0 if it started.
     * When absent (or it fails) the scan is stepped on the main thread. */
    int (*run_async)(void (*fn)(void *), void *arg);
    /* optional: spookyflow's own installed PARAM.SFO, for "Pin to top of XMB" */
    const char *own_sfo_path;
    /* optional: ask the system to power off cleanly; return 0 if it accepted
     * (the app then exits to let the shutdown proceed) */
    int (*power_off)(void);
    /* optional: real cover art (worker thread already running). NULL = placeholders */
    CoverLoader *covers;
} AppHost;

typedef struct {
    Spring scroll;      /* continuous scroll position (index units) */
    int sel;            /* selected (target) index */
} TabView;

typedef struct {
    AppHost host;
    Library *lib;
    Settings settings;
    Scanner scanner;
    CoverCache covers;
    Input input;
    HoldExit hold;

    AppMode mode;
    double time;

    /* loading screen */
    LoadStep load_step;
    int settings_ready;     /* settings file was read (or found missing): safe to save */
    float load_t;           /* time in loading mode */
    float load_target;      /* progress target 0..1 */
    float load_shown;       /* displayed progress (eased) */
    char load_status[64];
    int rescan;             /* loading was triggered by "Rescan library" */
    int scan_async;         /* scan running on a worker thread */
    volatile int scan_busy; /* worker still scanning (main thread must not touch lib) */
    float intro;            /* 0..1 flow fade-in after loading */

    /* tabs */
    int tab, tab_prev, tab_dir;
    float tab_anim;         /* 0..1, 1 = finished */
    Spring tab_hl;          /* active pill position (tab index units) */
    TabView view[PLAT_COUNT];

    /* title / chips cross-fade */
    int info_tab, info_idx, prev_tab, prev_idx;
    float info_t;           /* 0..1 */
    Spring title_w;         /* animated title pill width (logical units, set by ui) */
    int title_w_init;

    /* panels */
    int panel;              /* PANEL_* currently shown (or closing) */
    int panel_open;         /* target state */
    float panel_anim;       /* 0..1 */
    RowKind rows[MENU_MAX_ROWS];
    int nrows, row;
    Spring row_hl;          /* highlight position in rows */
    Spring list_scroll;     /* first visible row (float) */
    int panel_plat;         /* platform of the game being edited */
    int panel_game;         /* index of game being edited */
    GameCfg panel_cfg;

    /* overlays */
    char letter[4];
    float letter_t;         /* seconds since shown; >= LETTER_TIME hidden */
    char toast[160];
    char last_launch[160];  /* name of the last game/disc launched (no pop-up is shown) */
    float toast_t;          /* seconds since shown */

    /* exit */
    float exit_fade;        /* 0..1 */
    int exiting;
    int quit;               /* host should leave the main loop */
    int poweroff;           /* ...and then power the console off */
    float poweroff_arm;     /* >0: "press ✖ again" window (seconds left) */

    /* sleep: after SLEEP_TIME without input the screen fades to black; any
     * button wakes it, and that press does nothing else */
    float idle_t;
    int asleep;
    int wake_block;         /* swallow input until the waking press is released */
    float sleep_t;          /* 0..1 fade to black */
} App;

void app_init(App *a, Library *lib, const AppHost *host);
/* Advance one frame. held = abstract button mask (SFB_*), dt = seconds. */
void app_update(App *a, uint32_t held, float dt);
/* Request a clean exit (sysutil exit event, Quit row...). */
void app_request_exit(App *a, const char *why);
/* Save settings now (also called on exit). */
void app_save(App *a);

/* Helpers used by ui.c and tests */
const Game *app_selected_game(const App *a, int tab);
int  app_tab_has_games(const App *a, int tab);
int  app_main_context(const App *a);
void app_select(App *a, int tab, int index, int animate);
void app_open_panel(App *a, int panel);
void app_close_panel(App *a);
void app_show_toast(App *a, const char *msg);
/* The cover cache loader the app installs when host.covers is set. */
CcLoadFn app_cover_loader(void);

#endif
