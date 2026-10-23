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
}
