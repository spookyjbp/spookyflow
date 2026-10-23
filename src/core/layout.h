/* layout.h - screen-fraction layout math (pure, testable).
 * Vertical sizes are fractions of screen height H. Horizontal logical sizes
 * divide physical widths by the pixel aspect (Screen.pa). */
#ifndef SF_LAYOUT_H
#define SF_LAYOUT_H

#include "draw.h"

enum { PLAT_PS1 = 0, PLAT_PS2 = 1, PLAT_PS3 = 2, PLAT_COUNT = 3 };

/* Cover flow (fractions of H) */
#define LY_SEL_H      0.53f    /* selected case height */
#define LY_CASE_BORDER 0.016f  /* case rim around the art, fraction of case height */
#define LY_NB_H       0.34f    /* neighbor case height (~0.64x selected) */
#define LY_FLOW_CY    0.485f   /* vertical center of the row (centered between title pill and chips) */
#define LY_FRAME_PAD  0.020f   /* highlight frame padding around the selected case */
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

/* UI anchors, all in fractions of H unless noted */
typedef struct {
    float tabs_y, tab_h, tab_w, tab_gap, tab_em;
    float small_h, small_w, small_gap, small_em;
    float title_y, title_h, title_em, title_pad, title_max_w; /* title_max_w: fraction of W */
    float chips_y, chip_h, chip_em, chip_pad, chip_gap;
    float bar_y, bar_h, bar_em, bar_margin_x;   /* bar_margin_x: fraction of W */
    float icon_size, icon_gap;                  /* inside bar groups */
} UiMetrics;

void layout_ui(const Screen *s, UiMetrics *m);

/* Vertical extent of the selected-cover highlight frame (largest platform). */
void layout_frame_extent(const Screen *s, float *top, float *bottom);

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
