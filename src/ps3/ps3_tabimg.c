/* ps3_tabimg.c - optional user tab images:
 *   /dev_hdd0/tmp/spookyflow/tabs/ps1.png, ps2.png, ps3.png
 * Verified against PSL1GHT pngdec/pngdec.h (pngData {bmp_out, pitch, width,
 * height}; pngLoadFromFile) and sysmodule.h (sysModuleLoad(SYSMODULE_PNGDEC)).
 * The sprites2D sample uploads bmp_out directly as A8R8G8B8 and free()s it. */
#include <pngdec/pngdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sysmodule/sysmodule.h>
#include "layout.h"
#include "log.h"
#include "ps3_platform.h"
#include "render.h"
#include "ui.h"
#include "imgscale.h"
#include "stage.h"
#include <jpgdec/jpgdec.h>

#define TAB_IMG_MAX_W 1024
#define TAB_IMG_MAX_H 256

static void load_one(int plat)
{
    static const char *const names[PLAT_COUNT] = { "ps1.png", "ps2.png", "ps3.png" };
    char path[160];
    pngData png;
    unsigned char *rgba = NULL;
    u32 x, y, w, h;
    int tintable = 1, have_ref = 0;
    unsigned char ref[3] = { 0, 0, 0 };

    snprintf(path, sizeof(path), "%s/%s", SF_TABS_DIR, names[plat]);
    if (!ps3_fs_exists(path)) { log_printf("tabs: %s not found, using text tab", path); return; }
    memset(&png, 0, sizeof(png));
    ps3_decode_lock();
    if (pngLoadFromFile(path, &png) != 0 || !png.bmp_out) {
        ps3_decode_unlock();
        log_printf("tabs: %s could not be decoded", path);
        return;
    }
    ps3_decode_unlock();
    w = png.width; h = png.height;
    if (w == 0 || h == 0 || w > TAB_IMG_MAX_W || h > TAB_IMG_MAX_H) {
        log_printf("tabs: %s is %ux%u, must be at most %dx%d", path, (unsigned)w, (unsigned)h, TAB_IMG_MAX_W, TAB_IMG_MAX_H);
        free(png.bmp_out);
        return;
    }
    rgba = (unsigned char *)malloc((size_t)w * h * 4u);   /* one-time, at startup */
    if (!rgba) { free(png.bmp_out); return; }
    for (y = 0; y < h; y++) {
        const unsigned char *row = (const unsigned char *)png.bmp_out + (size_t)y * png.pitch;
        for (x = 0; x < w; x++) {
            const unsigned char *p = row + x * 4u;            /* A, R, G, B bytes */
            unsigned char *d = rgba + ((size_t)y * w + x) * 4u;
            d[0] = p[1]; d[1] = p[2]; d[2] = p[3]; d[3] = p[0];
            if (p[0] > 24) {
                if (!have_ref) { ref[0] = p[1]; ref[1] = p[2]; ref[2] = p[3]; have_ref = 1; }
                else if (abs((int)p[1] - ref[0]) > 40 || abs((int)p[2] - ref[1]) > 40 || abs((int)p[3] - ref[2]) > 40) tintable = 0;
            }
        }
    }
    free(png.bmp_out);
    /* trim the transparent border so the logo itself fills the tab */
    {
        u32 x0 = w, y0 = h, x1 = 0, y1 = 0;
        for (y = 0; y < h; y++)
            for (x = 0; x < w; x++)
                if (rgba[((size_t)y * w + x) * 4u + 3u] > 8) {
                    if (x < x0) x0 = x;
                    if (x > x1) x1 = x;
                    if (y < y0) y0 = y;
                    if (y > y1) y1 = y;
                }
        if (x0 <= x1 && y0 <= y1 && (x0 > 0 || y0 > 0 || x1 < w - 1 || y1 < h - 1)) {
            u32 nw = x1 - x0 + 1, nh = y1 - y0 + 1;
            for (y = 0; y < nh; y++)        /* rows move up/left in place: never overlaps forward */
                memmove(rgba + (size_t)y * nw * 4u, rgba + (((size_t)(y + y0) * w) + x0) * 4u, (size_t)nw * 4u);
            log_printf("tabs: %s trimmed %ux%u -> %ux%u", path, (unsigned)w, (unsigned)h, (unsigned)nw, (unsigned)nh);
            w = nw; h = nh;
        }
    }
    /* shrink once, smoothly, to the size it's drawn at (the GPU's own
     * filtering skips pixels when minifying, which made thin lines jagged) */
    {
        float bw, bh;
        int ow, oh;
        ui_tab_image_box_px(&bw, &bh);
        img_fit_size((int)w, (int)h, bw - 4.0f, bh - 4.0f, &ow, &oh);   /* room for the 2 px margin */
        if (ow < (int)w || oh < (int)h) {
            unsigned char *small = (unsigned char *)malloc((size_t)ow * (size_t)oh * 4u);
            if (small) {
                img_resample_rgba(rgba, (int)w, (int)h, small, ow, oh);
                log_printf("tabs: %s scaled %ux%u -> %dx%d for a %.0fx%.0f px tab", path,
                           (unsigned)w, (unsigned)h, ow, oh, bw, bh);
                free(rgba);
                rgba = small;
                w = (u32)ow; h = (u32)oh;
            }
        }
    }
    /* 2-pixel transparent margin: belt and braces with the CLAMP wrap mode */
    {
        unsigned char *padded = (unsigned char *)malloc((size_t)(w + 4) * (size_t)(h + 4) * 4u);
        if (padded) {
            img_pad_rgba(rgba, (int)w, (int)h, 2, padded);
            free(rgba);
            rgba = padded;
            w += 4; h += 4;
        }
    }
    if (tintable) {
        /* single-color logo: keep only alpha so the UI can tint it (dark on the white pill) */
        size_t i, n = (size_t)w * h;
        for (i = 0; i < n; i++) rgba[i] = rgba[i * 4u + 3u];
        if (r_texture_upload_alpha(TEX_TAB_PS1 + plat, (int)w, (int)h, rgba) == 0)
            ui_set_tab_image(plat, TEX_TAB_PS1 + plat, (int)w, (int)h, 1);
    } else if (r_texture_upload_rgba(TEX_TAB_PS1 + plat, (int)w, (int)h, rgba) == 0) {
        ui_set_tab_image(plat, TEX_TAB_PS1 + plat, (int)w, (int)h, 0);
    }
    log_printf("tabs: loaded %s (%ux%u, %s)", path, (unsigned)w, (unsigned)h, tintable ? "single color, tinted" : "full color");
    free(rgba);
}

/* ---- case images --------------------------------------------------
 * spookyflow ships no case art. Optional images, used when Flow Settings >
 * Custom Cases is On:
 *   /dev_hdd0/tmp/spookyflow/cases/ps1.png, ps2.png, ps3.png
 * The cover goes in the same spot as in ManaGunZ's case images (PS1 30,5
 * 240x240 of 275x250; PS2 0,10 260x370 of 270x390; PS3 0,30 260x300 of
 * 270x340), as fractions, so images with that layout work at any size. */
#define CASE_IMG_MAX 1024

static const float k_case_geo[PLAT_COUNT][6] = {
    /* ref w, ref h, cover x, y, w, h */
    { 275.0f, 250.0f, 30.0f,  5.0f, 240.0f, 240.0f },
    { 270.0f, 390.0f,  0.0f, 10.0f, 260.0f, 370.0f },
    { 270.0f, 340.0f,  0.0f, 30.0f, 260.0f, 300.0f },
};

static int load_case(int plat, const char *path)
{
    pngData png;
    unsigned char *rgba;
    u32 x, y, w, h;
    const float *g = k_case_geo[plat];
    if (!ps3_fs_exists(path)) return 0;
    memset(&png, 0, sizeof(png));
    ps3_decode_lock();
    if (pngLoadFromFile(path, &png) != 0 || !png.bmp_out) {
        ps3_decode_unlock();
        log_printf("cases: %s could not be decoded", path);
        return 0;
    }
    ps3_decode_unlock();
    w = png.width; h = png.height;
    if (w == 0 || h == 0 || w > CASE_IMG_MAX || h > CASE_IMG_MAX) {
        log_printf("cases: %s is %ux%u, must be at most %d px", path, (unsigned)w, (unsigned)h, CASE_IMG_MAX);
        free(png.bmp_out);
        return 0;
    }
    rgba = (unsigned char *)malloc((size_t)w * h * 4u);
    if (!rgba) { free(png.bmp_out); return 0; }
    for (y = 0; y < h; y++) {
        const unsigned char *row = (const unsigned char *)png.bmp_out + (size_t)y * png.pitch;
        for (x = 0; x < w; x++) {
            const unsigned char *p = row + x * 4u;          /* A, R, G, B */
            unsigned char *d = rgba + ((size_t)y * w + x) * 4u;
            d[0] = p[1]; d[1] = p[2]; d[2] = p[3]; d[3] = p[0];
        }
    }
    free(png.bmp_out);
    if (r_texture_upload_rgba(TEX_CASE_PS1 + plat, (int)w, (int)h, rgba) != 0) {
        log_printf("cases: no texture memory for %s", path);
        free(rgba);
        return 0;
    }
    free(rgba);
    layout_set_case_image(plat, (float)w, (float)h,
                          g[2] / g[0] * (float)w, g[3] / g[1] * (float)h, g[4] / g[0] * (float)w, g[5] / g[1] * (float)h);
    ui_set_case_image(plat, TEX_CASE_PS1 + plat);
    log_printf("cases: using %s (%ux%u)", path, (unsigned)w, (unsigned)h);
    return 1;
}

static void load_case_images(void)
{
    static const char *const mine[PLAT_COUNT] = { SF_CASES_DIR "/ps1.png", SF_CASES_DIR "/ps2.png", SF_CASES_DIR "/ps3.png" };
    int p;
    for (p = 0; p < PLAT_COUNT; p++)
        if (!load_case(p, mine[p]))
            log_printf("cases: %s not found, %s covers are shown without a case", mine[p], p == 0 ? "PS1" : p == 1 ? "PS2" : "PS3");
}

/* ---- built-in Stage background (src/core/stage_data.c) ---- */
static int upload_png_buffer(int tex, const unsigned char *buf, unsigned size, int *ow, int *oh)
{
    pngData png;
    unsigned char *rgba;
    u32 x, y;
    int rc;
    memset(&png, 0, sizeof(png));
    ps3_decode_lock();
    rc = pngLoadFromBuffer(buf, size, &png);
    ps3_decode_unlock();
    if (rc != 0 || !png.bmp_out) return -1;
    rgba = (unsigned char *)malloc((size_t)png.width * png.height * 4u);
    if (!rgba) { free(png.bmp_out); return -1; }
    for (y = 0; y < png.height; y++) {
        const unsigned char *row = (const unsigned char *)png.bmp_out + (size_t)y * png.pitch;
        for (x = 0; x < png.width; x++) {
            const unsigned char *p = row + x * 4u;
            unsigned char *d = rgba + ((size_t)y * png.width + x) * 4u;
            d[0] = p[1]; d[1] = p[2]; d[2] = p[3]; d[3] = p[0];
        }
    }
    *ow = (int)png.width; *oh = (int)png.height;
    free(png.bmp_out);
    rc = r_texture_upload_rgba(tex, *ow, *oh, rgba);
    free(rgba);
    return rc;
}

/* decode a brightness-only JPEG (R = G = B): one byte per pixel (malloc'd) */
static unsigned char *decode_gray_jpg(const unsigned char *buf, unsigned int size, int *ow, int *oh)
{
    jpgData j;
    int rc;
    u32 x, y;
    unsigned char *a8;
    memset(&j, 0, sizeof(j));
    ps3_decode_lock();
    rc = jpgLoadFromBuffer(buf, size, &j);
    ps3_decode_unlock();
    if (rc != 0 || !j.bmp_out) return NULL;
    a8 = (unsigned char *)malloc((size_t)j.width * j.height);
    if (!a8) { free(j.bmp_out); return NULL; }
    for (y = 0; y < j.height; y++) {
        const unsigned char *row = (const unsigned char *)j.bmp_out + (size_t)y * j.pitch;
        for (x = 0; x < j.width; x++) a8[(size_t)y * j.width + x] = row[x * 4u + 1u];
    }
    *ow = (int)j.width; *oh = (int)j.height;
    free(j.bmp_out);
    return a8;
}

static unsigned char *g_src_base;     /* the Stage scene's brightness, kept for ui.c */
static int g_src_bw, g_src_bh;

static void load_stage(void)
{
    jpgData jpg;
    int rc, w = 0, h = 0;
    unsigned char *rgba;
    u32 x, y;
    if (sysModuleIsLoaded(SYSMODULE_JPGDEC) != 0) sysModuleLoad(SYSMODULE_JPGDEC);
    memset(&jpg, 0, sizeof(jpg));
    ps3_decode_lock();
    rc = jpgLoadFromBuffer(g_stage_jpg, g_stage_jpg_size, &jpg);
    ps3_decode_unlock();
    if (rc != 0 || !jpg.bmp_out) { log_printf("stage: background could not be decoded (%d)", rc); return; }
    /* the JPEG is brightness only: R = G = B; keep one byte per pixel */
    rgba = (unsigned char *)malloc((size_t)jpg.width * jpg.height);
    if (!rgba) { free(jpg.bmp_out); return; }
    for (y = 0; y < jpg.height; y++) {
        const unsigned char *row = (const unsigned char *)jpg.bmp_out + (size_t)y * jpg.pitch;
        for (x = 0; x < jpg.width; x++) rgba[(size_t)y * jpg.width + x] = row[x * 4u + 1u];
    }
    free(jpg.bmp_out);
    /* 0.6.5: the brightness maps stay in memory; ui.c composites them with
     * the UI color into two textures (ui_set_stage_sources) */
    g_src_base = rgba; g_src_bw = (int)jpg.width; g_src_bh = (int)jpg.height;
    /* ribbons / flare / stars atlas: a grayscale PNG -> alpha texture */
    {
        pngData fx;
        int frc;
        memset(&fx, 0, sizeof(fx));
        ps3_decode_lock();
        frc = pngLoadFromBuffer(g_stage_fx_png, g_stage_fx_png_size, &fx);
        ps3_decode_unlock();
        if (frc == 0 && fx.bmp_out) {
            unsigned char *a8 = (unsigned char *)malloc((size_t)fx.width * fx.height);
            if (a8) {
                for (y = 0; y < fx.height; y++) {
                    const unsigned char *row = (const unsigned char *)fx.bmp_out + (size_t)y * fx.pitch;
                    for (x = 0; x < fx.width; x++) a8[(size_t)y * fx.width + x] = row[x * 4u + 1u];
                }
                if (r_texture_upload_alpha(TEX_STAGE_FX, (int)fx.width, (int)fx.height, a8) == 0) ui_set_stage_fx(TEX_STAGE_FX);
                free(a8);
            }
            free(fx.bmp_out);
        } else {
            log_printf("stage: effects could not be decoded (%d)", frc);
        }
    }
    /* light ribbons and glow halos: grayscale JPEGs, kept for the composite */
    {
        unsigned char *rib, *glow;
        int rw = 0, rh = 0, gw = 0, gh = 0;
        rib = decode_gray_jpg(g_stage_rib_jpg, g_stage_rib_jpg_size, &rw, &rh);
        glow = decode_gray_jpg(g_stage_glow_jpg, g_stage_glow_jpg_size, &gw, &gh);
        if (!rib || !glow) {
            log_printf("stage: ribbons / glow could not be decoded");
            free(rib); free(glow);
            if (r_texture_upload_alpha(TEX_STAGE, g_src_bw, g_src_bh, g_src_base) != 0) { log_printf("stage: no texture memory"); return; }
        } else {
            ui_set_stage_sources(g_src_base, g_src_bw, g_src_bh, rib, rw, rh, glow, gw, gh);
        }
    }
    if (upload_png_buffer(TEX_STAGE_SHAPES, g_stage_shapes_png, g_stage_shapes_png_size, &w, &h) != 0) {
        log_printf("stage: shapes could not be decoded");
        ui_set_stage(TEX_STAGE, -1);
        return;
    }
    ui_set_stage(TEX_STAGE, TEX_STAGE_SHAPES);
    log_printf("stage: background ready (shapes %dx%d)", w, h);
}

void ps3_load_tab_images(void)
{
    static int done;
    int p;
    if (done) return;                 /* textures are loaded once (rescan keeps them) */
    done = 1;
    /* the module stays loaded: the cover worker decodes PNG covers too */
    if (sysModuleIsLoaded(SYSMODULE_PNGDEC) != 0 && sysModuleLoad(SYSMODULE_PNGDEC) != 0) {
        log_printf("tabs: PNG decoder module failed to load");
        return;
    }
    for (p = 0; p < PLAT_COUNT; p++) load_one(p);
    load_case_images();
    load_stage();
}
