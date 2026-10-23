/* layout.c - screen-fraction layout math. */
#include "layout.h"
#include "anim.h"
#include "util.h"

float layout_case_aspect(int plat)
{
    switch (plat) {
    case PLAT_PS1: return 1.0f;    /* square jewel case */
    case PLAT_PS2: return 0.70f;   /* tall DVD keep case */
    default:       return 0.84f;   /* Blu-ray case */
    }
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
    m->title_y = 0.150f * H; m->title_h = 0.060f * H; m->title_em = 0.034f * H; m->title_pad = 0.030f * H; m->title_max_w = 0.80f;
    {
        /* chips sit 2.5% of H below the highlight frame */
        float ftop, fbot;
        layout_frame_extent(s, &ftop, &fbot);
        m->chip_h = 0.038f * H;
        m->chips_y = fbot + 0.025f * H + m->chip_h * 0.5f;
    }
    m->chip_em = 0.021f * H; m->chip_pad = 0.018f * H; m->chip_gap = 0.010f * H;
    m->bar_y = 0.935f * H; m->bar_h = 0.054f * H; m->bar_em = 0.023f * H; m->bar_margin_x = 0.015f;
    m->icon_size = 0.036f * H; m->icon_gap = 0.012f * H;
}

void layout_frame_extent(const Screen *s, float *top, float *bottom)
{
    float half = (LY_SEL_H * 0.5f + LY_FRAME_PAD) * s->H;
    *top = LY_FLOW_CY * s->H - half;
    *bottom = LY_FLOW_CY * s->H + half;
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
