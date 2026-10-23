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
#include "../../src/core/stage.h"
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
    {
        /* shape like real covers of the game's platform */
        int t, i, plat = PLAT_PS2;
        size_t n = strlen(path) - 4;   /* without ".JPG" */
        for (t = 0; t < PLAT_COUNT; t++)
            for (i = 0; i < g_lib.tab[t].count; i++) {
                const char *gp = lib_path(&g_lib, &g_lib.tab[t].games[i]);
                if (strncmp(gp, path, n) == 0) plat = t;
            }
        if (plat == PLAT_PS1) { W = 600; H = 600; }
        else if (plat == PLAT_PS3) { W = 520; H = 605; }
    }
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
            /* sample years for the preview (demo games have no title IDs) */
            g_lib.tab[t].games[i].year = on ? (uint16_t)(1996 + t * 4 + (int)(sf_hash(lib_title(&g_lib, &g_lib.tab[t].games[i])) % 7u)) : 0;
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

static int load_raw(const char *path, int tex, int *ow, int *oh)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    int w = 0, h = 0, ok = -1;
    if (f && fscanf(f, "%d %d", &w, &h) == 2 && fgetc(f) == '\n' && w > 0 && h > 0) {
        unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
        if (px && fread(px, 1, (size_t)w * h * 4, f) == (size_t)w * h * 4 && r_texture_upload_rgba(tex, w, h, px) == 0) ok = 0;
        free(px);
    }
    if (f) fclose(f);
    if (ow) *ow = w;
    if (oh) *oh = h;
    return ok;
}

/* raw dump of an alpha (brightness) image: "w h\n" + w*h bytes */
static int load_raw_alpha(const char *path, int tex, int hi_tex)
{
    FILE *f = path ? fopen(path, "rb") : NULL;
    int w = 0, h = 0, ok = -1;
    if (f && fscanf(f, "%d %d", &w, &h) == 2 && fgetc(f) == '\n' && w > 0 && h > 0) {
        unsigned char *px = (unsigned char *)malloc((size_t)w * h * 2);
        if (px && fread(px, 1, (size_t)w * h, f) == (size_t)w * h && r_texture_upload_alpha(tex, w, h, px) == 0) {
            stage_highlight(px, w * h, px + (size_t)w * h);
            if (r_texture_upload_alpha(hi_tex, w, h, px + (size_t)w * h) == 0) ui_set_stage_highlight(hi_tex);
            ok = 0;
        }
        free(px);
    }
    if (f) fclose(f);
    return ok;
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
    {   /* A-Z rail: frames after two quick R2 presses (it glides) */
        int f;
        char nm[64];
        app_select(&g_app, PLAT_PS1, find_title(PLAT_PS1, "Crash Bandicoot"), 0);
        step(0, 1.3f);
        press(SFB_R2); step(0, 0.2f);
        press(SFB_R2);
        for (f = 0; f < 4; f++) {
            step(0, f == 0 ? 0.06f : 0.12f);
            snprintf(nm, sizeof(nm), "36_letter_rail_%d", f);
            render(nm, 1280, 720);
        }
        step(0, 1.2f);
        app_select(&g_app, PLAT_PS1, find_title(PLAT_PS1, "Crash Bandicoot"), 0);
        step(0, 0.5f);
    }

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
    render("12_after_launch", 1280, 720);
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
    g_app.settings.ui_color = UI_COLOR_BLUE;
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
    /* case images (raw RGBA dumps; only used if SF_PREVIEW_CASES points at them) */
    {
        static const float geo[3][6] = { { 275, 250, 30, 5, 240, 240 }, { 270, 390, 0, 10, 260, 370 }, { 270, 340, 0, 30, 260, 300 } };
        const char *dir = getenv("SF_PREVIEW_CASES");
        int p, loaded = 0;
        for (p = 0; dir && p < 3; p++) {
            char fn[512];
            FILE *f;
            int w = 0, h = 0;
            snprintf(fn, sizeof(fn), "%s/ps%d.rgba", dir, p + 1);
            f = fopen(fn, "rb");
            if (f && fscanf(f, "%d %d", &w, &h) == 2 && fgetc(f) == '\n' && w > 0 && h > 0) {
                unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
                if (px && fread(px, 1, (size_t)w * h * 4, f) == (size_t)w * h * 4 &&
                    r_texture_upload_rgba(TEX_CASE_PS1 + p, w, h, px) == 0) {
                    layout_set_case_image(p, (float)w, (float)h, geo[p][2] / geo[p][0] * w, geo[p][3] / geo[p][1] * h,
                                          geo[p][4] / geo[p][0] * w, geo[p][5] / geo[p][1] * h);
                    ui_set_case_image(p, TEX_CASE_PS1 + p);
                    loaded++;
                }
                free(px);
            }
            if (f) fclose(f);
        }
        if (loaded) {
            cc_invalidate(&g_app.covers);
            cover_cancel_all(&g_pv_loader);
            app_select(&g_app, PLAT_PS3, 6, 0);
            press(SFB_R1); step_covers(1.0f);
            render("25_case_images_ps3", 1280, 720);
            press(SFB_L1); step_covers(1.0f);
            render("26_case_images_ps2", 1280, 720);
            press(SFB_L1); step_covers(1.0f);
            render("27_case_images_ps1", 1280, 720);
            press(SFB_R1); step_covers(0.6f);    /* PS2 */
            {
                int st;
                char nm[64];
                for (st = 0; st < 4; st++) {
                    ui_set_info_style(st);
                    snprintf(nm, sizeof(nm), "30_info_style_%d", st);
                    render(nm, 1280, 720);
                }
                ui_set_info_style(2);
            }
            {
                static const int bgs[] = { BG_ANIMATED, BG_AURORA, BG_FIREFLIES, BG_RIBBONS, BG_HALO, BG_TWILIGHT };
                int b, c2;
                char nm[64];
                double t0 = g_app.time;
                for (b = 0; b < 6; b++) {
                    g_app.settings.background = bgs[b];
                    g_app.time = t0 + 7.0 + b * 3.1;
                    snprintf(nm, sizeof(nm), "31_bg_%d_%s", b, settings_background_name(bgs[b]));
                    render(nm, 1280, 720);
                }
                /* the same styles in other UI colors */
                for (c2 = 1; c2 < 5; c2 += 3)
                    for (b = 1; b < 6; b++) {
                        g_app.settings.ui_color = c2;
                        g_app.settings.background = bgs[b];
                        snprintf(nm, sizeof(nm), "32_bg_%s_%s", settings_background_name(bgs[b]), settings_color_name(c2));
                        render(nm, 1280, 720);
                    }
                g_app.settings.ui_color = UI_COLOR_BLUE;
                g_app.settings.background = BG_ANIMATED;
                g_app.time = t0;
            }
            /* the user's background picture (raw dump; only with SF_PREVIEW_BG) */
            {
                const char *bgf = getenv("SF_PREVIEW_BG");
                FILE *f = bgf ? fopen(bgf, "rb") : NULL;
                int w = 0, h = 0;
                if (f && fscanf(f, "%d %d", &w, &h) == 2 && fgetc(f) == '\n' && w > 0 && h > 0) {
                    unsigned char *px = (unsigned char *)malloc((size_t)w * h * 4);
                    if (px && fread(px, 1, (size_t)w * h * 4, f) == (size_t)w * h * 4 &&
                        r_texture_upload_rgba(TEX_BG_IMAGE, w, h, px) == 0) {
                        ui_set_background_image(TEX_BG_IMAGE);
                        g_app.settings.background = BG_IMAGE;
                        render("33_bg_image_ps2", 1280, 720);
                        render("33_bg_image_ps2_1080p", 1920, 1080);
                        press(SFB_R1); step_covers(0.8f);
                        render("33_bg_image_ps3", 1280, 720);
                        press(SFB_L1); press(SFB_L1); step_covers(0.8f);
                        render("33_bg_image_ps1", 1280, 720);
                        press(SFB_R1); step_covers(0.6f);
                        g_app.settings.background = BG_ANIMATED;
                    }
                    free(px);
                }
                if (f) fclose(f);
            }
            /* the built-in Stage background (raw dumps of art/stage.*; SF_PREVIEW_STAGE dir) */
            {
                const char *sdir = getenv("SF_PREVIEW_STAGE");
                char a1[512], a2[512];
                if (sdir) {
                    snprintf(a1, sizeof(a1), "%s/stage.rgba", sdir);
                    snprintf(a2, sizeof(a2), "%s/shapes.rgba", sdir);
                    if (load_raw_alpha(a1, TEX_STAGE, TEX_STAGE_HI) == 0 && load_raw(a2, TEX_STAGE_SHAPES, NULL, NULL) == 0) {
                        char a3[512];
                        FILE *ff;
                        snprintf(a3, sizeof(a3), "%s/rib.rgba", sdir);
                        ff = fopen(a3, "rb");
                        if (ff) {
                            int rw = 0, rh = 0;
                            if (fscanf(ff, "%d %d", &rw, &rh) == 2 && fgetc(ff) == '\n') {
                                unsigned char *px = (unsigned char *)malloc((size_t)rw * rh);
                                if (px && fread(px, 1, (size_t)rw * rh, ff) == (size_t)rw * rh &&
                                    r_texture_upload_alpha(TEX_STAGE_RIB, rw, rh, px) == 0) ui_set_stage_ribbons(TEX_STAGE_RIB);
                                free(px);
                            }
                            fclose(ff);
                        }
                        snprintf(a3, sizeof(a3), "%s/glow.rgba", sdir);
                        ff = fopen(a3, "rb");
                        if (ff) {
                            int gw = 0, gh = 0;
                            if (fscanf(ff, "%d %d", &gw, &gh) == 2 && fgetc(ff) == '\n') {
                                unsigned char *px = (unsigned char *)malloc((size_t)gw * gh);
                                if (px && fread(px, 1, (size_t)gw * gh, ff) == (size_t)gw * gh &&
                                    r_texture_upload_alpha(TEX_STAGE_GLOW, gw, gh, px) == 0) ui_set_stage_glow(TEX_STAGE_GLOW);
                                free(px);
                            }
                            fclose(ff);
                        }
                        snprintf(a3, sizeof(a3), "%s/fx.rgba", sdir);
                        ff = fopen(a3, "rb");
                        if (ff) {
                            int fw = 0, fh = 0;
                            if (fscanf(ff, "%d %d", &fw, &fh) == 2 && fgetc(ff) == '\n') {
                                unsigned char *px = (unsigned char *)malloc((size_t)fw * fh);
                                if (px && fread(px, 1, (size_t)fw * fh, ff) == (size_t)fw * fh &&
                                    r_texture_upload_alpha(TEX_STAGE_FX, fw, fh, px) == 0) ui_set_stage_fx(TEX_STAGE_FX);
                                free(px);
                            }
                            fclose(ff);
                        }
                        int f;
                        char nm[64];
                        double t0 = g_app.time;
                        ui_set_stage(TEX_STAGE, TEX_STAGE_SHAPES);
                        g_app.settings.background = BG_STAGE;
                        render("34_stage_ps2", 1280, 720);
                        render("34_stage_ps2_1080p", 1920, 1080);
                        press(SFB_R2); step(0, 0.25f);
                        render("37_letter_rail_stage", 1280, 720);
                        step(0, 1.2f);
                        g_app.time = t0;
                        {
                            int uc;
                            for (uc = 0; uc < UI_COLOR_COUNT; uc++) {
                                g_app.settings.ui_color = uc;
                                snprintf(nm, sizeof(nm), "35_stage_%d_%s", uc, settings_color_name(uc));
                                render(nm, 1280, 720);
                            }
                            g_app.settings.ui_color = UI_COLOR_BLUE;
                        }
                        for (f = 0; f < 48; f++) {           /* 24 s of motion, played back fast in a GIF */
                            g_app.time = t0 + f * 0.5;
                            snprintf(nm, sizeof(nm), "anim/stage_%02d", f);
                            render(nm, 640, 360);
                        }
                        g_app.time = t0;
                        g_app.settings.background = BG_ANIMATED;
                    }
                }
            }
            g_app.sleep_t = 1.0f;
            render("28_sleep_dimmed", 1280, 720);
            g_app.sleep_t = 0.0f;
            g_app.settings.cases = 0;
            render("29_custom_cases_off", 1280, 720);
            g_app.settings.cases = 1;
            ui_clear_case_images();
        }
    }

    /* fix 1: letter jump in a UI color, Show Game Info off */
    g_app.settings.ui_color = 0;
    press(SFB_R2); step(0, 0.12f);
    render("23_letter_blue", 1280, 720);
    step(0, 1.2f);
    g_app.settings.ui_color = UI_COLOR_BLUE;
    g_app.settings.show_info = 0;
    render("24_no_game_info", 1280, 720);
    g_app.settings.show_info = 1;
    pv_covers_on(0);
    soft_shutdown();
    return 0;
}
