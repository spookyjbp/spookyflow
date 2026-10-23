/* preview.c - host tool: runs the real core (app + layout + ui) against the
 * software rasterizer and writes PPM frames (converted to PNG by
 * ppm2png.py). Simulates the PS3 mapping: Tiny3D's 848x512 virtual space
 * stretched to 1280x720 / 1920x1080, exactly like the console build. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "render_soft.h"
#include "../../src/core/app.h"
#include "../../src/core/log.h"
#include "../../src/core/title.h"
#include "../../src/core/ui.h"
#include "../../src/core/render.h"
#include "../../src/core/util.h"
#include <math.h>

static Library g_lib;
static App g_app;
static unsigned char g_scratch[UI_SCRATCH_BYTES];
static const char *g_outdir = "preview";
static const float DT = 1.0f / 60.0f;

static int fake_list(void *ctx, const char *dir, FsEntry *out, int max)
{
    (void)ctx; (void)dir; (void)out; (void)max;
    return -1;   /* no HDD folders -> demo library */
}

static void set_screen(int pw, int ph)
{
    Screen s;
    s.W = 848.0f; s.H = 512.0f;
    s.pa = ((float)pw / 848.0f) / ((float)ph / 512.0f);
    s.px = (float)ph / 512.0f;
    d_set_screen(&s);
    soft_init(pw, ph, s.W, s.H);
}

static void render(const char *name, int pw, int ph)
{
    char path[512];
    set_screen(pw, ph);
    g_soft_vertex_count = 0;
    ui_draw(&g_app);
    snprintf(path, sizeof(path), "%s/%s.ppm", g_outdir, name);
    if (soft_write_ppm(path) != 0) fprintf(stderr, "cannot write %s\n", path);
    printf("wrote %s (%ld vertices)\n", path, g_soft_vertex_count);
    set_screen(1280, 720);
}

static void step(uint32_t held, float secs)
{
    int n = (int)(secs / DT + 0.5f), i;
    for (i = 0; i < n; i++) { app_update(&g_app, held, DT); ui_update_targets(&g_app); }
}

static void press(uint32_t b) { app_update(&g_app, b, DT); ui_update_targets(&g_app); app_update(&g_app, 0, DT); ui_update_targets(&g_app); }

/* ---- synthetic cover art (original abstract designs, no real box art) ---- */
static int pv_is_file(void *ctx, const char *path)
{
    (void)ctx;
    /* "<game>.JPG" beside the game for 4 of every 5 games */
    return strstr(path, "/") == NULL && sf_ends_with_i(path, ".JPG") && (sf_hash(path) % 5u) != 0;
}

static int pv_decode(void *ctx, const char *path, int *w, int *h, uint8_t **rgba)
{
    uint32_t hs = sf_hash(path);
    float hue = (float)(hs & 0xFFFF) / 65535.0f, hue2 = hue + 0.18f + (float)((hs >> 16) & 255) / 900.0f;
    int x, y, W = 520, H = 740;
    (void)ctx;
    if (g_app.tab == PLAT_PS1) { W = 600; H = 600; }          /* jewel case front */
    else if (g_app.tab == PLAT_PS3) { W = 520; H = 605; }     /* Blu-ray case front */
    *rgba = (uint8_t *)malloc((size_t)W * H * 4);
    if (!*rgba) return -1;
    for (y = 0; y < H; y++)
        for (x = 0; x < W; x++) {
            float fy = (float)y / (float)H, fx = (float)x / (float)W;
            uint32_t c = sf_col_mix(sf_hsv(hue, 0.65f, 0.95f, 255), sf_hsv(hue2, 0.80f, 0.30f, 255), fy);
            float dx = fx - 0.62f, dy = fy - 0.38f;
            uint8_t *q = *rgba + ((size_t)y * W + x) * 4;
            if (dx * dx + dy * dy < 0.07f) c = sf_col_mix(c, sf_hsv(hue + 0.5f, 0.5f, 1.0f, 255), 0.55f);
            if (fabsf((fx + fy) - 1.15f) < 0.05f) c = sf_col_mix(c, 0xFFFFFFFFu, 0.7f);
            if (fy < 0.14f) c = sf_col_mix(c, 0x101010FFu, 0.75f);          /* banner strip */
            q[0] = (uint8_t)(c >> 24); q[1] = (uint8_t)(c >> 16); q[2] = (uint8_t)(c >> 8); q[3] = 255;
        }
    *w = W; *h = H;
    return 0;
}

static CoverLoader g_pv_loader;

static void pv_covers_on(int on)
{
    int t, i;
    for (t = 0; t < PLAT_COUNT; t++)
        for (i = 0; i < g_lib.tab[t].count; i++) {
            if (on) g_lib.tab[t].games[i].flags &= (uint8_t)~GAME_DEMO;
            else g_lib.tab[t].games[i].flags |= GAME_DEMO;
        }
    g_app.host.covers = on ? &g_pv_loader : NULL;
    g_app.covers.load = NULL;
    cc_invalidate(&g_app.covers);
    if (on) {
        CoverIO io;
        memset(&io, 0, sizeof(io));
        io.is_file = pv_is_file;
        io.decode = pv_decode;
        io.pixels = r_cover_pixels;
        cover_loader_init(&g_pv_loader, &io);
        cc_init(&g_app.covers, CC_DEFAULT_BUDGET, NULL, NULL);
        g_app.covers.load = app_cover_loader();
        g_app.covers.user = &g_app;
    }
}

/* frames with the worker keeping up (it is ~10x faster than real time here) */
static void step_covers(float secs)
{
    int n = (int)(secs / DT + 0.5f), i;
    for (i = 0; i < n; i++) {
        app_update(&g_app, 0, DT);
        ui_update_targets(&g_app);
        cover_worker_step(&g_pv_loader);
        cover_worker_step(&g_pv_loader);
    }
}

static int find_title(int plat, const char *t)
{
    const GameList *gl = &g_lib.tab[plat];
    int i;
    for (i = 0; i < gl->count; i++) if (strcmp(lib_title(&g_lib, &gl->games[i]), t) == 0) return i;
    return 0;
}

int main(int argc, char **argv)
{
    AppHost host;
    int guard = 0;
    if (argc > 1) g_outdir = argv[1];
    log_set_echo(0);
    set_screen(1280, 720);
    if (ui_upload_textures(g_scratch, sizeof(g_scratch)) != 0) { fprintf(stderr, "texture upload failed\n"); return 1; }
    memset(&host, 0, sizeof(host));
    host.fs_list = fake_list;
    app_init(&g_app, &g_lib, &host);

    /* loading: stop while scanning so the status line shows a platform */
    while (g_app.load_step != LOAD_SCAN && guard++ < 100) app_update(&g_app, 0, DT);
    step(0, 0.30f);
    if (g_app.load_step == LOAD_SCAN || g_app.load_step == LOAD_READY) {
        /* status/progress as they'd read mid-scan */
        snprintf(g_app.load_status, sizeof(g_app.load_status), "Scanning PS2 games\xE2\x80\xA6");
        g_app.load_shown = 0.46f;
    }
    render("01_loading", 1280, 720);
    guard = 0;
    while (g_app.mode != MODE_FLOW && guard++ < 600) app_update(&g_app, 0, DT);
    step(0, INTRO_TIME * 0.45f);
    render("02_intro_crossfade", 1280, 720);
    step(0, 1.0f);

    app_select(&g_app, PLAT_PS2, find_title(PLAT_PS2, "Ape Escape - Pumped & Primed"), 0);
    step(0, 0.5f);
    render("03_flow_ps2", 1280, 720);
    render("04_flow_ps2_1080p", 1920, 1080);

    press(SFB_R1); step(0, TAB_ANIM_TIME * 0.35f);
    render("05_tab_switch_mid", 1280, 720);
    step(0, 0.6f);
    app_select(&g_app, PLAT_PS3, find_title(PLAT_PS3, "Metal Gear Solid 4 - Guns of the Patriots"), 0);
    step(0, 0.5f);
    render("06_flow_ps3_long_title", 1280, 720);

    press(SFB_L1); press(SFB_L1); step(0, 0.6f);
    app_select(&g_app, PLAT_PS1, find_title(PLAT_PS1, "Crash Bandicoot"), 0);
    step(0, 0.5f);
    render("07_flow_ps1", 1280, 720);

    press(SFB_R2); step(0, 0.12f);
    render("08_letter_jump", 1280, 720);
    step(0, 1.2f);

    press(SFB_R1); step(0, 0.5f);   /* PS2 */
    app_select(&g_app, PLAT_PS2, find_title(PLAT_PS2, "Gran Turismo 4"), 0);
    step(0, 0.5f);
    press(SFB_TRIANGLE); step(0, 0.3f);
    press(SFB_DOWN); step(0, 0.1f);
    press(SFB_DOWN); step(0, 0.1f);
    press(SFB_DOWN); step(0, 0.3f);
    render("09_game_settings_ps2", 1280, 720);
    press(SFB_CIRCLE); step(0, 0.4f);

    press(SFB_START); step(0, 0.3f);
    press(SFB_DOWN); step(0, 0.1f);
    render("10_flow_settings", 1280, 720);
    {
        int i;
        for (i = 0; i < 6; i++) { press(SFB_DOWN); step(0, 0.08f); }
        step(0, 0.4f);
    }
    render("11_flow_settings_scrolled", 1280, 720);
    press(SFB_CIRCLE); step(0, 0.4f);

    press(SFB_CROSS); step(0, 0.4f);
    render("12_launch_toast", 1280, 720);
    step(0, 2.5f);

    /* hold circle ~half way */
    step(SFB_CIRCLE, HOLD_EXIT_TIME * 0.5f);
    render("13_exit_hold_half", 1280, 720);
    step(SFB_CIRCLE, HOLD_EXIT_TIME * 0.5f + 0.05f);
    step(0, EXIT_FADE_TIME * 0.5f);
    render("14_exit_fade", 1280, 720);

    /* sample user tab images: a wide single-color logo with a transparent
     * border (as the PS3 loader receives it after trimming) and a small
     * square full-color one */
    {
        static unsigned char wide[240 * 60], sq[64 * 64 * 4];
        int x, y;
        for (y = 0; y < 60; y++)
            for (x = 0; x < 240; x++)
                wide[y * 240 + x] = ((x / 30) % 2 == 0 && y > 6 && y < 54) ? 255 : 0;
        for (y = 0; y < 64; y++)
            for (x = 0; x < 64; x++) {
                unsigned char *q = sq + (y * 64 + x) * 4;
                int in = (x - 32) * (x - 32) + (y - 32) * (y - 32) < 30 * 30;
                q[0] = (unsigned char)(x * 4); q[1] = 80; q[2] = (unsigned char)(255 - y * 4); q[3] = in ? 255 : 0;
            }
        if (r_texture_upload_alpha(TEX_TAB_PS2, 240, 60, wide) == 0) ui_set_tab_image(PLAT_PS2, TEX_TAB_PS2, 240, 60, 1);
        if (r_texture_upload_rgba(TEX_TAB_PS1, 64, 64, sq) == 0) ui_set_tab_image(PLAT_PS1, TEX_TAB_PS1, 64, 64, 0);
        app_init(&g_app, &g_lib, &host);
        guard = 0;
        while (g_app.mode != MODE_FLOW && guard++ < 1200) app_update(&g_app, 0, DT);
        step(0, 1.0f);
        render("15_tab_images", 1280, 720);
    }

    /* every UI color: cover flow, then Flow Settings open on the UI Color row */
    {
        static const char *const names[UI_COLOR_COUNT] = { "blue", "red", "green", "purple", "pink", "orange", "white" };
        int c, k;
        char nm[64];
        for (c = 0; c < UI_COLOR_COUNT; c++) {
            g_app.settings.ui_color = c;
            snprintf(nm, sizeof(nm), "16_color_%d_%s_flow", c, names[c]);
            render(nm, 1280, 720);
        }
        press(SFB_START); step(0, 0.3f);
        for (k = 0; k < 6; k++) { press(SFB_DOWN); step(0, 0.05f); }
        step(0, 0.4f);
        for (c = 0; c < UI_COLOR_COUNT; c++) {
            g_app.settings.ui_color = c;
            snprintf(nm, sizeof(nm), "17_color_%d_%s_settings", c, names[c]);
            render(nm, 1280, 720);
        }
        press(SFB_CIRCLE); step(0, 0.4f);
    }

    /* milestone 2: real cover art (synthetic stand-ins; 1 in 5 has none) */
    g_app.settings.ui_color = UI_COLOR_WHITE;
    pv_covers_on(1);
    app_select(&g_app, PLAT_PS2, find_title(PLAT_PS2, "Gran Turismo 4"), 0);
    g_app.tab = PLAT_PS2;
    step_covers(1.0f);
    render("18_covers_ps2", 1280, 720);
    render("19_covers_ps2_1080p", 1920, 1080);
    press(SFB_R1); step_covers(1.0f);
    render("20_covers_ps3", 1280, 720);
    press(SFB_L1); press(SFB_L1); step_covers(1.0f);
    render("21_covers_ps1", 1280, 720);
    /* a few frames after the covers arrive: mid fade-in */
    press(SFB_R1); step(0, 0.5f);
    app_select(&g_app, PLAT_PS2, find_title(PLAT_PS2, "Ape Escape - Pumped & Primed"), 0);
    cc_invalidate(&g_app.covers);
    cover_cancel_all(&g_pv_loader);
    step(0, 0.02f);
    while (cover_worker_step(&g_pv_loader)) { }
    step(0, 0.10f);
    render("22_covers_fading_in", 1280, 720);
    pv_covers_on(0);
    soft_shutdown();
    return 0;
}
