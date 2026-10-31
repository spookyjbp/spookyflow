/* app.h - portable application state machine (loading, flow, panels). */
#ifndef SF_APP_H
#define SF_APP_H

#include "anim.h"
#include "cover_cache.h"
#include "coverart.h"
#include "input.h"
#include "launch.h"
#include "library.h"
#include "menu.h"
#include "scanner.h"
#include "settings.h"

#define APP_NAME        "spookyflow"
#define APP_VERSION     "1.1"

#define LOAD_MIN_TIME   5.0f    /* startup loading screen stays at least this long: many monitors stay black
                                   for 2-3 s after the PS3 switches video mode. Not skippable. */
#define RESCAN_MIN_TIME 1.6f    /* "Rescan library" has no video mode switch */
#define RESCAN_OUT_TIME 0.60f   /* Rescan: the drawer slides away and the cover flow fades out first (0.9.2: 0.40) */
#define INTRO_TIME      1.10f   /* loading -> flow cross-fade */
#define TAB_ANIM_TIME   0.30f
#define PANEL_ANIM_TIME 0.20f
#define ABOUT_TIME      0.30f   /* the About card fades / grows in */
#define DISPLAY_TIME    0.45f   /* Square: the selected case grows to fill the screen */
#define XFADE_TIME      0.18f
#define LETTER_TIME     1.00f
#define TOAST_TIME      2.20f
#define MUSIC_CARD_TIME 4.50f
#define MUSIC_SWAP_TIME 0.45f  /* the name change on the card */  /* the "now playing" card (top left) stays this long */
#define EXIT_FADE_TIME  0.30f
#define PANEL_VISIBLE_ROWS 6        /* Game Settings / Choose Disc (beside the big case) */
#define PANEL_VISIBLE_ROWS_FLOW 7   /* the Flow Settings drawer (0.8.0) */
#define SLEEP_TIME      300.0f  /* default idle time before the screen dims (Flow Settings > Sleep Mode) */
#define SLEEP_DIM       0.72f   /* how dark sleep gets: the screen stays visible */
#define SLEEP_FADE_TIME 1.5f
#define WAKE_FADE_TIME  0.25f
#define MUSIC_GAIN_MAX  0.5f   /* gain at Music Volume 100% */
#define MUSIC_START_DELAY 2.0f /* s after the cover flow has come in */
#define MUSIC_SLEEP_DUCK 0.6f  /* music drops to 40% while the screen sleeps */

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
    /* 0.9.2: GET a URL (Download Missing Covers); NULL = not available */
    CoverHttpFn http_get;
    void *http_ctx;
    /* 1.1: different on every start (the real clock); seeds the music shuffle.
     * 0 = none (then the app's own time, which is the same every start) */
    uint32_t seed;
    /* optional: send a launch request to webMAN MOD (launch.h); return 0 if
     * webMAN took it (the app then exits to the XMB, where webMAN mounts and
     * starts the game). NULL = launching not available. */
    int (*launch)(const char *request);
    /* optional: read webMAN's settings (wm_config.bin); leave cfg->valid 0 if unknown */
    void (*read_wm_config)(WmConfig *cfg);
    /* optional: read part of a file (fs_ctx is passed); lets the scanner read
     * NTFS index files (sizes, title IDs) */
    ReadAtFn read_at;
    /* optional: ask webMAN to re-index the NTFS drives (/refresh.ps3?prepntfs);
     * blocks until done; returns the number of games webMAN found, or -1.
     * Run before the scan on "Rescan Library" (on the scan thread). */
    int (*ntfs_refresh)(void);
    /* optional: folder with the background music (.mp3/.wav, see music.h);
     * needs read_at. The host runs music_decode_step() on a thread and
     * music_mix() in its audio callback. NULL = no music. */
    const char *music_dir;
    /* optional: small file operations for the PS2 config picker (with
     * read_at). is_file gets fs_ctx; write_file replaces the file; both
     * return 0 / 1 as named. Without them the PS2 Config row is unavailable. */
    int (*is_file)(void *ctx, const char *path);
    int (*write_file)(const char *path, const void *data, uint32_t len);   /* 0 = written */
    int (*remove_file)(const char *path);                                   /* 0 = removed */
    /* optional: the user's background pictures (0.7.0): the folder, and a
     * loader that decodes one, fits it to the screen and makes it the
     * background texture (ui_set_background_image). 0 = loaded. */
    const char *bg_dir;
    int (*load_bg_image)(const char *path);
} AppHost;

typedef struct {
    Spring scroll;      /* continuous scroll position (index units) */
    int sel;            /* selected (target) index */
    double step_t;      /* time of the last one-game step */
    float step_rate;    /* how fast one-game steps are coming (steps/s, 0 = not scrolling) */
    int glide;          /* 0.8.4: -1 / +1 while the left stick scrolls continuously, else 0 */
    float glide_pos;    /* the row's position while gliding (index units) */
    float glide_hold;   /* how long the stick has been held */
    float flat;         /* 0.8.5: 0..1, the row's cases all one size while gliding fast (eases back) */
} TabView;

/* The row eases to a single step in ~300 ms; while steps come fast (holding the
 * stick) it stiffens so it never trails the selection by more than about
 * FLOW_MAX_LAG games: the selected game stays in the middle. */
#define FLOW_STEP_WINDOW 0.25   /* steps further apart than this are "not scrolling" */
#define FLOW_MAX_LAG     0.20f
#define FLOW_OMEGA_MAX   400.0f
#define GLIDE_MAX_SPEED  16.0f   /* games/s (0.8.7: was 24; faster looks doubled on a TV even with the motion blur) */
#define GLIDE_FLAT_FROM   6.0f   /* games/s where the row starts going flat ... (0.8.6: was 10) */
#define GLIDE_FLAT_FULL  12.0f   /* ... and is flat (0.8.6: was 18, 0.8.7: 12) */
enum { CARD_ABOUT = 0, CARD_STORAGE = 1, CARD_DOWNLOAD = 2 };
#define HIDE_UI_TIME      0.35f  /* the cover flow's UI fades out / in (Hide Cover Flow UI) */
#define GLIDE_BRAKE       7.0f   /* 1/s: near an end the speed is at most this x the games left ... */
#define GLIDE_BRAKE_MIN   0.6f   /* ... plus this (games/s), so it arrives */

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
    void *sizes;            /* 0.9.0: the folder games' sizes being worked out (SizeJob, app.c) */
    void *sizes_orphan;     /* a job a rescan cancelled, freed when its thread is done */
    CoverDl *dl;            /* 0.9.2: Download Missing Covers, while it runs */
    CoverDl *dl_orphan;     /* one a rescan stopped, freed when its thread is done */
    int   dl_applied;       /* its results already shown */
    int   dl_ran;           /* a download ran since start-up (the card shows its result) */
    int   dl_got, dl_missing, dl_have, dl_noid, dl_error, dl_total, dl_tls;
    char  dl_note[96];
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
    float page_t;           /* 0.8.0: seconds since this page of rows appeared (they cascade in) */
    float input_dt;         /* this frame's time step (the stick glide) */
    int   about;            /* 0.8.2: the About spookyflow card is up (over Flow Settings) */
    float about_t;          /* 0..1, follows about */
    int   card;             /* 0.8.7: which card `about` shows: CARD_ABOUT or CARD_STORAGE */
    float hide_t;           /* 0.8.7: 0..1, follows settings.hide_ui */
    int   page_dir;         /* +1: the page comes in from the right (opened / a group entered), -1: from the left (back) */
    int   display;          /* 1 = display mode (Square): one big case, only Launch and Back */
    float display_t;        /* 0..1, follows display */
    RowKind rows[MENU_MAX_ROWS];
    int nrows, row;
    Spring row_hl;          /* highlight position in rows */
    Spring list_scroll;     /* first visible row (float) */
    int panel_plat;         /* platform of the game being edited */
    int panel_game;         /* index of game being edited */
    GameCfg panel_cfg;
    int usb_roots;                  /* 1.1: the default roots: FAT32 USB drives are added at each scan */
    int flow_group;         /* Flow Settings: the group open (ROW_FG_*), -1 = the first page */
    /* the user's background pictures (in host.bg_dir) */
#define APP_MAX_BG_IMAGES 32
    char bg_files[APP_MAX_BG_IMAGES][128];
    int nbg_files;
    char bg_loaded[128];    /* the picture in the texture now ("" = none) */
    int flow_top_row;       /* row of that group on the first page (◯ goes back to it) */
    /* PS2 config picker, worked out when Game Settings opens on a PS2 game */
    int ps2cfg_ok;          /* 1: a PS2 ISO with a known ID and the host can copy files */
    unsigned ps2cfg_avail;  /* bit n: PS2CFG type n exists for this game (bit 0 = Auto, always) */
    int ps2cfg_auto;        /* what Auto gives: -1 nothing, a PS2CFG_* type or PS2CFG_OWN */
    char ps2cfg_gid[16];    /* SLUS_209.14 */
    int ps2cfg_wm_off;      /* webMAN's own CONFIG lookup is turned off (its setup) */

    /* overlays */
    char letter[4];
    int letter_bucket, letter_prev_bucket;   /* the A-Z rail glides from the previous letter */
    int music_gen;          /* last track counter seen (music_current) */
    int music_scanned;      /* the music folder has been read */
    char music_name[128];   /* song shown on the now-playing card */
    float music_card_t;     /* time since the card appeared (>= music_card_len: hidden) */
    float music_card_len;
    /* song change while the card shows: the old name fades out as the new one comes in */
    char music_prev_name[128];
    float music_swap_t;     /* 0..1 (>= 1: no change going on) */
    float music_prev_off, music_prev_w;   /* the old name's scroll offset and the card's width then */
    float music_cur_off, music_cur_w;     /* written by ui.c each frame */
    float reveal_start;
    float flow_t;           /* time since the cover flow appeared */     /* load_t when the Stage reveal started (-1000 = not used) */   /* how long it stays: longer while a long name scrolls (set by ui.c) */
    float letter_t;         /* seconds since shown; >= LETTER_TIME hidden */
    float letter_in;        /* seconds since the rail appeared (kept while jumping again: no flicker) */
    char toast[160];
    volatile int ntfs_busy;  /* 1 while webMAN re-indexes the NTFS drives (Rescan) */
    int launched;           /* 1 = exiting because a game was launched */
    char last_launch[160];  /* name of the last game/disc launched (no pop-up is shown) */
    float toast_t;          /* seconds since shown */

    /* exit */
    float exit_fade;        /* 0..1 */
    int exiting;
    int quit;               /* host should leave the main loop */
    int poweroff;           /* ...and then power the console off */

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
float music_gain(int percent);
/* Text of a settings row's value as shown (the PS2 Config row knows more
 * than menu_value_text: what exists for this game and what Auto picks). */
void app_row_value(const App *a, RowKind k, char *out, size_t cap);   /* Music Volume setting -> music_set_volume() */
/* Save settings now (also called on exit). */
void app_save(App *a);

/* Helpers used by ui.c and tests */
const Game *app_selected_game(const App *a, int tab);
int app_sizes_busy(const App *a);   /* 0.9.0: folder games' sizes still being worked out */
/* 0.9.2: Download Missing Covers: 1 while it runs; *pos / *n = games looked at / all */
int app_download_busy(const App *a, int *pos, int *n, int *got);
int  app_tab_has_games(const App *a, int tab);
int  app_main_context(const App *a);
/* rows a panel shows at once (more scroll) */
int  app_panel_visible_rows(const App *a);
void app_select(App *a, int tab, int index, int animate);
void app_open_panel(App *a, int panel);
void app_close_panel(App *a);
void app_show_toast(App *a, const char *msg);
/* The cover cache loader the app installs when host.covers is set. */
CcLoadFn app_cover_loader(void);

#endif
