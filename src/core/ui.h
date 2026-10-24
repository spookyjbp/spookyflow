/* ui.h - draws a full frame from App state through draw.h / render.h. */
#ifndef SF_UI_H
#define SF_UI_H

#include "app.h"
#include "draw.h"
#include "layout.h"

/* Upload fonts + badge atlas through the render backend. scratch must hold
 * at least UI_SCRATCH_BYTES. Returns 0 on success. */
#define UI_SCRATCH_BYTES (1024u * 512u * 2u)
int  ui_upload_textures(unsigned char *scratch, unsigned scratch_size);

/* Optional user tab images (PNG loaded by the host). tintable = single-color
 * image with alpha, uploaded white so it can be tinted dark on the active
 * (white) tab. */
void ui_set_tab_image(int plat, int tex, int w, int h, int tintable);
void ui_clear_tab_images(void);
/* Case image for a platform (texture holds it; layout_set_case_image has its
 * geometry). It is drawn over the cover, as ManaGunZ does. */
void ui_set_case_image(int plat, int tex);
void ui_clear_case_images(void);
/* Title/info look: 0 pills, 1 minimal, 2 accent, 3 card. */
void ui_set_info_style(int s);
/* Background > Image uses this texture (a full-screen picture). */
/* The built-in Stage background (picture + shape atlas textures). */
void ui_set_stage(int bg_tex, int shapes_tex);
void ui_set_stage_highlight(int tex);
void ui_set_stage_fx(int tex);
void ui_set_stage_ribbons(int tex);
void ui_set_stage_glow(int tex);
/* 0.6.5: give the Stage brightness maps instead (kept by the caller): ui.c
 * composites them into TEX_STAGE (opaque scene) and TEX_STAGE_RIB (both
 * ribbon sets, RGBA, 1024 x rib_h/2) with the UI color, and redoes it when
 * the color changes. */
void ui_set_stage_sources(const unsigned char *base, int bw, int bh, const unsigned char *rib, int rw, int rh,
                          const unsigned char *glow, int gw, int gh);
int  ui_stage_baked(void);
/* the user's background picture (BG_CUSTOM): texture id, -1 = none */
void ui_set_background_image(int tex);
/* the kittens' entrance frames (cats.h atlas) as a texture; -1 = none (a plain fade instead) */
/* UI color (accent, UI_COLOR_* from settings.h): tabs, highlighted rows,
 * sliders, selection frame, loading bar, exit ring, background shapes */
void ui_set_color(int idx);
/* Largest size (output pixels) a tab logo is drawn at, for pre-scaling. */
void ui_tab_image_box_px(float *w_px, float *h_px);

/* Set measured animation targets (title pill width). Call every frame
 * after app_update; ui_draw also calls it. */
void ui_update_targets(App *a);

/* Draw one frame. Mutates only animation targets (title pill width). */
void ui_draw(App *a);

/* ---- bottom bar layout (testable) ---- */
#define BAR_MAX_GROUPS 7
#define BAR_MAX_ITEMS  3
typedef struct {
    int icon;            /* ICON_* badge */
    float w;             /* measured width (logical) */
} BarItem;

typedef struct {
    BarItem item[BAR_MAX_ITEMS];
    int nitems;
    const char *label;
    float label_w;
    float w;             /* total group width (logical) */
    float x;             /* left edge after layout */
} BarGroup;

typedef struct {
    BarGroup g[BAR_MAX_GROUPS];
    int n;
    float bar_x, bar_y, bar_w, bar_h;   /* bar rect (logical) */
    float gap;                          /* even gap between groups and ends */
    float em, icon, icon_gap, item_gap;
} BarLayout;

/* panel_mode = 0: main bar (Launch, Game settings, L1 R1 A-Z, Flow settings, Exit)
 * panel_mode = 1: panel bar (Change, Back); 2: Choose Disc (Launch, Back);
 * 3: display mode, Square (Launch, Game Settings, Back).
 * Uses the current d_screen(). */
void ui_bar_layout(int panel_mode, BarLayout *out);

/* Title pill fitting: returns the text width; *em_out = font size used and
 * *truncated = 1 if the text needs an ellipsis at that size. */
float ui_title_fit(const char *title, float maxw, float em, float *em_out, int *truncated);

#endif
