/* layout.h - screen-fraction layout math (pure, testable).
 * Vertical sizes are fractions of screen height H. Horizontal logical sizes
 * divide physical widths by the pixel aspect (Screen.pa). */
#ifndef SF_LAYOUT_H
#define SF_LAYOUT_H

#include "draw.h"

enum { PLAT_PS1 = 0, PLAT_PS2 = 1, PLAT_PS3 = 2, PLAT_COUNT = 3 };

/* Cover flow (fractions of H) */
#define LY_SEL_H      0.53f    /* selected case height */
#define LY_NB_H       0.34f    /* neighbor case height (~0.64x selected) */
#define LY_FLOW_CY    0.485f   /* vertical center of the row */
#define LY_GAP_NB     0.12f    /* gap between neighbors, fraction of neighbor width */
#define LY_GAP_SEL    0.30f    /* gap selected<->first neighbor, fraction of neighbor width */
#define LY_MAX_DRAW   7        /* draw items with |d| <= 7  (15 covers) */

typedef struct {
    float cx, cy;    /* center (logical) */
    float w, h;      /* size (logical) */
    float bright;    /* 0..1 brightness multiplier */
} CoverPos;

/* Physical width/height ratio of a platform's case. */
float layout_case_aspect(int plat);
/* Height scale of the platform's case relative to the base fractions. */
float layout_case_scale(int plat);
/* Position of the item at distance d (= index - scroll) from the center. */
void  layout_cover(const Screen *s, int plat, float d, CoverPos *out);

/* Where the cover sits on the front of a case, as fractions of the case's
 * width and height (the rest is the hinge/spine and the plastic rim). */
typedef struct { float x0, y0, x1, y1; } CaseWindow;
void  layout_case_window(int plat, CaseWindow *w);
/* Physical width/height of that window (what covers are cropped to). */
float layout_window_aspect(int plat);
/* Use a case image's shape instead of the drawn case: the image is
 * img_w x img_h and the cover goes in the rectangle (cx, cy, cw, ch) of it.
 * Call before covers are loaded (during the loading screen). */
void  layout_set_case_image(int plat, float img_w, float img_h, float cx, float cy, float cw, float ch);
void  layout_clear_case_images(void);
/* Flow Settings > Custom Cases: use loaded case images (1) or show bare covers (0). */
void  layout_use_case_images(int on);
int   layout_case_image_active(int plat);

/* UI anchors, all in fractions of H unless noted */
typedef struct {
    float tabs_y, tab_h, tab_w, tab_gap, tab_em;
    float small_h, small_w, small_gap, small_em;
    float title_y, title_h, title_em, title_pad, title_max_w; /* title_max_w: fraction of W */
    float chips_y, chip_h, chip_em, chip_pad, chip_gap;
    float bar_y, bar_h, bar_em, bar_margin_x;   /* bar_margin_x: fraction of W */
    float icon_size, icon_gap;                  /* inside bar groups */
} UiMetrics;

/* title_y / chips_y are for a full-height case (PS2/PS3); see layout_info_y. */
void layout_ui(const Screen *s, UiMetrics *m);

/* Title pill centered between the tabs and the selected case of `plat`, and
 * the info chips centered between that case and the bottom bar. */
void layout_info_y(const Screen *s, const UiMetrics *m, int plat, float *title_y, float *chips_y);

/* Tab bar positions. tab_w[3] = width of each tab pill (logical; pass NULL
 * for the default width). shoulder_w = width of the L2/R2 badges (logical).
 * out_x[0] = L2 center, out_x[1..3] = tab centers, out_x[4] = R2 center. */
void layout_tabs(const Screen *s, const UiMetrics *m, const float *tab_w, float shoulder_w, float out_x[5]);

/* "space-evenly" distribution of n groups with widths w[] inside
 * [x0, x0+span]. Writes each group's left x into out_x and returns the gap.
 * If the groups do not fit, the gap is 0 and groups are packed from x0
 * (callers shrink text first; see ui.c). */
float layout_space_evenly(float x0, float span, const float *w, int n, float *out_x);

#endif
