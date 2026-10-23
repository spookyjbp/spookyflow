/* layout.c - screen-fraction layout math. */
#include "layout.h"
#include "anim.h"
#include "util.h"

/* With a case image (Flow Settings > Custom Cases), the cover sits in the
 * image's cover rectangle. Without one, only the cover is shown, in the
 * shape of real cover art. */
static float window_shape(int plat)
{
    switch (plat) {
    case PLAT_PS1: return 1.00f;   /* PS1 fronts are square */
    case PLAT_PS2: return 0.70f;   /* PS2 DVD covers */
    default:       return 0.86f;   /* PS3 covers (webMAN COV packs) */
    }
}

static struct { int set; float aspect; CaseWindow win; } g_case_img[PLAT_COUNT];
static int g_cases_on = 1;

void layout_use_case_images(int on) { g_cases_on = on != 0; }
int  layout_case_image_active(int plat) { return plat >= 0 && plat < PLAT_COUNT && g_cases_on && g_case_img[plat].set; }

void layout_set_case_image(int plat, float img_w, float img_h, float cx, float cy, float cw, float ch)
{
    if (plat < 0 || plat >= PLAT_COUNT || img_w <= 0 || img_h <= 0 || cw <= 0 || ch <= 0) return;
    g_case_img[plat].aspect = img_w / img_h;
    g_case_img[plat].win.x0 = cx / img_w;
    g_case_img[plat].win.x1 = (cx + cw) / img_w;
    g_case_img[plat].win.y0 = cy / img_h;
    g_case_img[plat].win.y1 = (cy + ch) / img_h;
    g_case_img[plat].set = 1;
}

void layout_clear_case_images(void)
{
    int i;
    for (i = 0; i < PLAT_COUNT; i++) g_case_img[i].set = 0;
}

float layout_case_aspect(int plat)
{
    if (layout_case_image_active(plat)) return g_case_img[plat].aspect;
    return window_shape(plat);
}

void layout_case_window(int plat, CaseWindow *w)
{
    if (layout_case_image_active(plat)) { *w = g_case_img[plat].win; return; }
    w->x0 = 0.0f; w->x1 = 1.0f;
    w->y0 = 0.0f; w->y1 = 1.0f;
}

float layout_window_aspect(int plat)
{
    CaseWindow w;
    layout_case_window(plat, &w);
    return layout_case_aspect(plat) * (w.x1 - w.x0) / (w.y1 - w.y0);
}

float layout_case_scale(int plat)
{
    /* A square case at full height is very wide; trim it slightly so more
     * neighbors stay visible (decision noted in README). */
    return plat == PLAT_PS1 ? 0.88f : 1.0f;
}

void layout_cover(const Screen *s, int plat, float d, CoverPos *out)
{
    float H = s->H, asp = layout_case_aspect(plat), sc = layout_case_scale(plat);
    float hs = LY_SEL_H * H * sc, hn = LY_NB_H * H * sc;
    float ws = d_xu(hs * asp), wn = d_xu(hn * asp);
    float x1 = ws * 0.5f + wn * LY_GAP_SEL + wn * 0.5f;   /* center of first neighbor */
    float pitch = wn * (1.0f + LY_GAP_NB);
    float a = sf_absf(d), sign = d < 0.0f ? -1.0f : 1.0f;
    float x, h, b;
    if (a <= 1.0f) {
        float t = ease_in_out(a);
        x = a * x1;
        h = sf_lerp(hs, hn, t);
        b = sf_lerp(1.0f, 0.80f, a);
    } else {
        x = x1 + (a - 1.0f) * pitch;
        h = hn;
        b = sf_maxf(0.36f, 0.80f - (a - 1.0f) * 0.14f);
    }
    out->cx = s->W * 0.5f + sign * x;
    out->cy = LY_FLOW_CY * H;
    out->h = h;
    out->w = d_xu(h * asp);
    out->bright = b;
}

void layout_ui(const Screen *s, UiMetrics *m)
{
    float H = s->H;
    m->tabs_y = 0.072f * H; m->tab_h = 0.072f * H; m->tab_w = 0.165f * H; m->tab_gap = 0.016f * H; m->tab_em = 0.037f * H;
    m->small_h = 0.040f * H; m->small_w = 0.074f * H; m->small_gap = 0.030f * H; m->small_em = 0.021f * H;
    m->title_h = 0.060f * H; m->title_em = 0.034f * H; m->title_pad = 0.030f * H; m->title_max_w = 0.80f;
    m->chip_h = 0.038f * H;
    m->chip_em = 0.021f * H; m->chip_pad = 0.018f * H; m->chip_gap = 0.010f * H;
    m->bar_y = 0.935f * H; m->bar_h = 0.054f * H; m->bar_em = 0.023f * H; m->bar_margin_x = 0.015f;
    m->icon_size = 0.036f * H; m->icon_gap = 0.012f * H;
    layout_info_y(s, m, PLAT_PS2, &m->title_y, &m->chips_y);
}

void layout_info_y(const Screen *s, const UiMetrics *m, int plat, float *title_y, float *chips_y)
{
    CoverPos c;
    float case_t, case_b, tabs_b = m->tabs_y + m->tab_h * 0.5f, bar_t = m->bar_y - m->bar_h * 0.5f;
    layout_cover(s, plat, 0.0f, &c);
    case_t = c.cy - c.h * 0.5f;
    case_b = c.cy + c.h * 0.5f;
    *title_y = (tabs_b + case_t) * 0.5f;
    *chips_y = (case_b + bar_t) * 0.5f;
}

void layout_tabs(const Screen *s, const UiMetrics *m, const float *tab_w, float shoulder_w, float out_x[5])
{
    float w[3], total, x;
    int i;
    for (i = 0; i < 3; i++) w[i] = tab_w ? tab_w[i] : d_xu(m->tab_w);
    total = w[0] + w[1] + w[2] + 2.0f * d_xu(m->tab_gap);
    x = s->W * 0.5f - total * 0.5f;
    for (i = 0; i < 3; i++) { out_x[i + 1] = x + w[i] * 0.5f; x += w[i] + d_xu(m->tab_gap); }
    out_x[0] = s->W * 0.5f - total * 0.5f - d_xu(m->small_gap) - shoulder_w * 0.5f;
    out_x[4] = s->W * 0.5f + total * 0.5f + d_xu(m->small_gap) + shoulder_w * 0.5f;
}

float layout_space_evenly(float x0, float span, const float *w, int n, float *out_x)
{
    float sum = 0.0f, gap, x;
    int i;
    if (n <= 0) return 0.0f;
    for (i = 0; i < n; i++) sum += w[i];
    gap = (span - sum) / (float)(n + 1);
    if (gap < 0.0f) gap = 0.0f;
    x = x0 + gap;
    for (i = 0; i < n; i++) { out_x[i] = x; x += w[i] + gap; }
    return gap;
}
