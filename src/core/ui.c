/* ui.c - draws a full frame from App state. Portable: only draw.h/render.h. */
#include "ui.h"
#include "cats.h"
#include "icons.h"
#include "render.h"
#include "theme.h"
#include "title.h"
#include "util.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define PI_F 3.14159265f
#define ELLIPSIS "\xE2\x80\xA6"

static const char *const k_plat_short[PLAT_COUNT] = { "PS1", "PS2", "PS3" };
/* Case header label. Plain text only: no platform logos are drawn or bundled. */
static const char *const k_plat_case[PLAT_COUNT] = { "PS1", "PS2", "PS3" };

typedef struct { int present, tex, w, h, tintable; } TabImage;
static TabImage g_tab_img[PLAT_COUNT];

void ui_set_tab_image(int plat, int tex, int w, int h, int tintable)
{
    if (plat < 0 || plat >= PLAT_COUNT || w <= 0 || h <= 0) return;
    g_tab_img[plat].present = 1;
    g_tab_img[plat].tex = tex;
    g_tab_img[plat].w = w;
    g_tab_img[plat].h = h;
    g_tab_img[plat].tintable = tintable;
}

void ui_tab_image_box_px(float *w_px, float *h_px)
{
    UiMetrics m;
    const Screen *s = d_screen();
    layout_ui(s, &m);
    /* logical y units * px = pixels; d_xu(x) * px * pa = x * px pixels */
    *h_px = m.tab_h * 0.80f * s->px;
    *w_px = (m.tab_w - m.tab_h * 0.55f) * s->px;
}

void ui_clear_tab_images(void) { memset(g_tab_img, 0, sizeof(g_tab_img)); }

/* ------------------------------------------------------------------ */

int ui_upload_textures(unsigned char *scratch, unsigned scratch_size)
{
    unsigned need_icons = (unsigned)g_icons.w * (unsigned)g_icons.h * 4u;
    if (font_decode_atlas(&g_font_main, scratch, scratch_size) != 0) return -1;
    if (r_texture_upload_alpha(TEX_FONT_MAIN, g_font_main.atlas_w, g_font_main.atlas_h, scratch) != 0) return -2;
    if (font_decode_atlas(&g_font_big, scratch, scratch_size) != 0) return -3;
    if (r_texture_upload_alpha(TEX_FONT_BIG, g_font_big.atlas_w, g_font_big.atlas_h, scratch) != 0) return -4;
    if (scratch_size < need_icons * 2u) return -5;
    if (icons_decode_rgba(scratch, scratch + need_icons, need_icons) != 0) return -6;
    if (r_texture_upload_rgba(TEX_ICONS, g_icons.w, g_icons.h, scratch) != 0) return -7;
    if (scratch_size < (unsigned)(D_SHAPE_TEX * D_SHAPE_TEX)) return -8;
    d_gen_blob_alpha(scratch, D_SHAPE_TEX);
    if (r_texture_upload_alpha(TEX_BLOB, D_SHAPE_TEX, D_SHAPE_TEX, scratch) != 0) return -9;
    d_gen_disc_alpha(scratch, D_SHAPE_TEX);
    if (r_texture_upload_alpha(TEX_DISC, D_SHAPE_TEX, D_SHAPE_TEX, scratch) != 0) return -10;
    if (r_texture_upload_rgba(TEX_CATS, g_cats_w, g_cats_h, g_cats_rgba) != 0) return -11;
    return 0;
}

/* ---- UI color (accent) --------------------------------------------- */
static const uint32_t k_accent[UI_COLOR_COUNT] = {
    0x0070D1FFu,   /* Blue (PlayStation blue) */
    0xE0393EFFu,   /* Red */
    0x2BB35EFFu,   /* Green */
    0x7D4FD9FFu,   /* Purple */
    0xE2559FFFu,   /* Pink */
    0xF28A1EFFu,   /* Orange */
    0xFFFFFFFFu,   /* White (the original look) */
};
static int g_accent = UI_COLOR_WHITE;

void ui_set_color(int idx) { g_accent = (idx >= 0 && idx < UI_COLOR_COUNT) ? idx : UI_COLOR_WHITE; }
static int accent_white(void) { return g_accent == UI_COLOR_WHITE; }
static uint32_t accent(void) { return k_accent[g_accent]; }
/* accent at a given strength, or the original white at the original strength */
static uint32_t accent_or(uint32_t white_col, float color_alpha)
{
    return accent_white() ? white_col : sf_col_alpha(accent(), color_alpha);
}
static uint32_t on_accent(void) { return accent_white() ? TH_TEXT_ON_WHITE : 0xFFFFFFFFu; }

static float W(void) { return d_screen()->W; }
static float H(void) { return d_screen()->H; }

/* ------------------------------------------------------------------ */
/* Background                                                          */

static void draw_background(const App *a, float alpha, int animated)
{
    static const float k_blob[6][6] = {
        /* x0,   y0,   r,    speedx, speedy, phase */
        { 0.20f, 0.30f, 0.55f, 0.050f, 0.037f, 0.0f },
        { 0.78f, 0.25f, 0.48f, 0.041f, 0.053f, 1.7f },
        { 0.55f, 0.80f, 0.60f, 0.033f, 0.045f, 3.1f },
        { 0.10f, 0.85f, 0.40f, 0.058f, 0.029f, 4.4f },
        { 0.92f, 0.70f, 0.45f, 0.036f, 0.061f, 5.2f },
        { 0.45f, 0.15f, 0.35f, 0.047f, 0.039f, 2.4f },
    };
    int i;
    float t = animated ? (float)a->time : 0.0f;
    d_rect_grad(0, 0, W(), H(), sf_col_alpha(TH_BG_TOP, alpha), sf_col_alpha(TH_BG_BOTTOM, alpha));
    for (i = 0; i < 6; i++) {
        const float *b = k_blob[i];
        float cx = W() * (b[0] + 0.10f * sinf(t * b[3] * 2.0f + b[5]));
        float cy = H() * (b[1] + 0.08f * cosf(t * b[4] * 2.0f + b[5] * 1.3f));
        float r = H() * b[2] * (1.0f + 0.06f * sinf(t * 0.11f + b[5]));
        uint32_t shape = accent_white() ? TH_BG_SHAPE : sf_col_mix(TH_BG_SHAPE, accent(), 0.35f);
        d_blob(cx, cy, r, sf_col_alpha(shape, TH_BG_SHAPE_ALPHA * alpha * (0.6f + 0.08f * (float)i)));
    }
}

/* ------------------------------------------------------------------ */
/* Covers                                                              */

#define COVER_FADE_FRAMES 14.0f   /* ~0.23 s at 60 fps */

/* tex >= 0: real cover art in that texture, faded in by `art` (0..1). */
static void draw_cover(const App *a, int plat, const Game *g, const CoverPos *p, float alpha, int tex, float art)
{
    const char *title = lib_title(a->lib, g);
    uint32_t h32 = sf_hash(title);
    float hue = (float)(h32 & 0xFFFF) / 65535.0f;
    float b = p->bright;
    float x = p->cx - p->w * 0.5f, y = p->cy - p->h * 0.5f, w = p->w, h = p->h;
    float bord = h * LY_CASE_BORDER, hh = h * (plat == PLAT_PS1 ? 0.075f : 0.085f);
    float ax = x + d_xu(bord), aw = w - 2.0f * d_xu(bord);
    float ay = y + bord + hh, ah = h - 2.0f * bord - hh;
    uint32_t top = sf_hsv(hue, 0.42f, 0.58f, 255);
    if (alpha <= 0.003f) return;
    if (tex < 0) art = 0.0f;
    /* case body */
    d_rrect(x, y, w, h, h * 0.014f, sf_col_alpha(sf_col_dim(TH_CASE, b), alpha));
    if (art < 1.0f) {
        float pa = alpha * (1.0f - art);
        /* header strip with platform name */
        d_rect(ax, y + bord, aw, hh, sf_col_alpha(sf_col_dim(TH_CASE_HEADER, b), pa));
        d_text_fit(&g_font_main, ax + d_xu(hh * 0.30f), y + bord + hh * 0.52f, hh * 0.56f,
                   sf_col_alpha(sf_col_dim(0xFFFFFFE0u, b), pa), k_plat_case[plat], ALIGN_LEFT, aw * 0.9f);
        /* placeholder art: flat hashed color + title (no lighting effects) */
        d_rect(ax, ay, aw, ah, sf_col_alpha(sf_col_dim(top, b), pa));
        d_text_box(&g_font_main, ax + aw * 0.5f, ay + ah * 0.36f, aw * 0.84f, h * 0.068f, 4,
                   sf_col_alpha(sf_col_dim(0xFFFFFFF0u, b), pa), title);
    }
    if (art > 0.0f)   /* real cover fills the whole art area (no header strip) */
        d_image(tex, ax, y + bord, aw, h - 2.0f * bord, sf_col_alpha(sf_col_dim(0xFFFFFFFFu, b), alpha * art));
    if (plat == PLAT_PS1)   /* jewel case clear edge */
        d_rrect_stroke(x, y, w, h, h * 0.014f, h * 0.006f, sf_col_alpha(sf_col_dim(TH_CASE_JEWEL, b), alpha * 0.55f));
}

/* Draw one tab's cover row. xoff = horizontal slide (logical), alpha = fade. */
static void draw_flow_row(App *a, int tab, float xoff, float alpha, int with_frame)
{
    const GameList *gl = &a->lib->tab[tab];
    float scroll = a->view[tab].scroll.x;
    int center = (int)floorf(scroll + 0.5f), k;
    const Screen *s = d_screen();
    if (gl->count <= 0) {
        d_text(&g_font_main, W() * 0.5f + xoff, H() * LY_FLOW_CY, H() * 0.03f, sf_col_alpha(TH_TEXT_DIM, alpha), "No games", ALIGN_CENTER);
        return;
    }
    if (with_frame) {
        CoverPos p;
        float frac = sf_absf(scroll - (float)center);
        float fa = alpha * (1.0f - frac * 1.5f);
        float pad = LY_FRAME_PAD * H();
        layout_cover(s, tab, 0.0f, &p);
        if (fa > 0.0f) {
            float fx = p.cx - p.w * 0.5f - d_xu(pad) + xoff, fy = p.cy - p.h * 0.5f - pad;
            float fw = p.w + 2.0f * d_xu(pad), fh = p.h + 2.0f * pad;
            d_rrect(fx, fy, fw, fh, 0.03f * H(), sf_col_alpha(accent_or(TH_SEL_FRAME, 0.30f), fa));
        }
    }
    for (k = LY_MAX_DRAW; k >= 0; k--) {
        int side;
        for (side = 0; side < (k == 0 ? 1 : 2); side++) {
            int idx = side == 0 ? center - k : center + k;
            CoverPos p;
            if (idx < 0 || idx >= gl->count) continue;
            layout_cover(s, tab, (float)idx - scroll, &p);
            p.cx += xoff;
            if (p.cx + p.w * 0.5f < 0.0f || p.cx - p.w * 0.5f > W()) continue;
            {
                int slot = cc_lookup(&a->covers, tab, idx), tex = -1;
                float art = 0.0f;
                if (slot >= 0 && a->covers.slot[slot].state == CC_READY && a->covers.slot[slot].tex >= 0) {
                    tex = a->covers.slot[slot].tex;
                    art = sf_clampf((float)cc_ready_age(&a->covers, slot) / COVER_FADE_FRAMES, 0.0f, 1.0f);
                }
                draw_cover(a, tab, &gl->games[idx], &p, alpha, tex, art);
            }
        }
    }
}

/* ------------------------------------------------------------------ */
/* Tabs                                                                */

static void pill_text(float cx, float cy, float w, float h, uint32_t fill, const char *txt, float em, uint32_t tc)
{
    d_rrect(cx - w * 0.5f, cy - h * 0.5f, w, h, h * 0.5f, fill);
    d_text(&g_font_main, cx, cy, em, tc, txt, ALIGN_CENTER);
}

static void draw_tabs(const App *a, float alpha, float yoff)
{
    const Screen *s = d_screen();
    UiMetrics m;
    int i;
    float y, hl, hx, hw, tw[3], xs[5], img_h, shoulder_w;
    layout_ui(s, &m);
    y = m.tabs_y + yoff;
    /* user logos fill most of the pill: fit inside 80% of its height and the
     * straight middle part of its width, keeping the logo's proportions */
    img_h = m.tab_h * 0.80f;
    for (i = 0; i < PLAT_COUNT; i++) tw[i] = d_xu(m.tab_w);
    shoulder_w = d_xu(m.tab_h * 0.62f * icons_aspect(ICON_L1));
    layout_tabs(s, &m, tw, shoulder_w, xs);
    for (i = 0; i < PLAT_COUNT; i++)
        d_rrect(xs[i + 1] - tw[i] * 0.5f, y - m.tab_h * 0.5f, tw[i], m.tab_h, m.tab_h * 0.5f, sf_col_alpha(TH_TAB_INACTIVE, alpha));
    /* gliding active highlight (position and width interpolate) */
    hl = sf_clampf(a->tab_hl.x, 0.0f, 2.0f);
    {
        int i0 = (int)floorf(hl), i1 = i0 < 2 ? i0 + 1 : 2;
        float f = hl - (float)i0;
        hx = sf_lerp(xs[i0 + 1], xs[i1 + 1], f);
        hw = sf_lerp(tw[i0], tw[i1], f);
    }
    d_rrect(hx - hw * 0.5f, y - m.tab_h * 0.5f, hw, m.tab_h, m.tab_h * 0.5f, sf_col_alpha(accent_or(TH_TAB_ACTIVE, 1.0f), alpha));
    for (i = 0; i < PLAT_COUNT; i++) {
        float k = 1.0f - sf_minf(1.0f, sf_absf(hl - (float)i));
        uint32_t tc = sf_col_mix(TH_TEXT_DIM, on_accent(), k);
        if (g_tab_img[i].present) {
            const TabImage *ti = &g_tab_img[i];
            float box_w = tw[i] - d_xu(m.tab_h * 0.55f);           /* clear of the rounded ends */
            float ih = img_h, iw = d_xu(ih * (float)ti->w / (float)ti->h);
            uint32_t tint = ti->tintable ? tc : sf_col_dim(0xFFFFFFFFu, 0.6f + 0.4f * k);
            if (iw > box_w) { ih *= box_w / iw; iw = box_w; }        /* wide logo: fit the width */
            d_image(ti->tex, xs[i + 1] - iw * 0.5f, y - ih * 0.5f, iw, ih, sf_col_alpha(tint, alpha));
        } else {
            d_text(&g_font_main, xs[i + 1], y, m.tab_em, sf_col_alpha(tc, alpha), k_plat_short[i], ALIGN_CENTER);
        }
    }
    d_badge(ICON_L1, xs[0], y, m.tab_h * 0.62f, sf_col_alpha(0xFFFFFFFFu, alpha));
    d_badge(ICON_R1, xs[4], y, m.tab_h * 0.62f, sf_col_alpha(0xFFFFFFFFu, alpha));
    if (a->lib->tab[a->tab].demo) {
        const char *txt = "Demo library";
        float em = m.small_em * 0.92f;
        float cw = d_text_width(&g_font_main, em, txt) + d_xu(m.small_h * 0.9f);
        float cx = xs[4] + shoulder_w * 0.5f + d_xu(m.small_gap) + cw * 0.5f;
        pill_text(cx, y, cw, m.small_h * 0.85f, sf_col_alpha(TH_DEMO_CHIP, alpha), txt, em, sf_col_alpha(TH_TEXT_FAINT, alpha));
    }
}

/* ------------------------------------------------------------------ */
/* Title pill + chips                                                  */

float ui_title_fit(const char *title, float maxw, float em, float *em_out, int *truncated)
{
    float w = d_text_width(&g_font_main, em, title);
    *truncated = 0;
    *em_out = em;
    if (w <= maxw) return w;
    em *= 0.86f;
    *em_out = em;
    w = d_text_width(&g_font_main, em, title);
    if (w <= maxw) return w;
    *truncated = 1;
    return maxw;
}

static int chip_texts(const App *a, int tab, int idx, char out[4][32])
{
    const GameList *gl = &a->lib->tab[tab];
    const Game *g;
    int n = 0;
    if (idx < 0 || idx >= gl->count) return 0;
    g = &gl->games[idx];
    snprintf(out[n++], 32, "%d of %d", idx + 1, gl->count);      /* position in this tab */
    if (g->genre && g->genre[0]) sf_strlcpy(out[n++], g->genre, 32);
    if (g->pmax > 0) {
        if (g->pmin == g->pmax || g->pmin == 0) snprintf(out[n++], 32, "%d player%s", g->pmax, g->pmax == 1 ? "" : "s");
        else snprintf(out[n++], 32, "%d-%d players", g->pmin, g->pmax);
    }
    if (g->size > 0) sf_format_size(g->size, out[n++], 32);
    return n;
}

static void draw_chips(const App *a, int tab, int idx, float alpha, const UiMetrics *m)
{
    char txt[4][32];
    float w[4], total = 0.0f, x;
    int n = chip_texts(a, tab, idx, txt), i;
    if (n == 0 || alpha <= 0.0f) return;
    for (i = 0; i < n; i++) {
        w[i] = d_text_width(&g_font_main, m->chip_em, txt[i]) + 2.0f * d_xu(m->chip_pad);
        total += w[i];
    }
    total += (float)(n - 1) * d_xu(m->chip_gap);
    x = W() * 0.5f - total * 0.5f;
    for (i = 0; i < n; i++) {
        d_rrect(x, m->chips_y - m->chip_h * 0.5f, w[i], m->chip_h, m->chip_h * 0.5f, sf_col_alpha(TH_CHIP, alpha));
        d_text(&g_font_main, x + w[i] * 0.5f, m->chips_y, m->chip_em, sf_col_alpha(TH_TEXT_DIM, alpha), txt[i], ALIGN_CENTER);
        x += w[i] + d_xu(m->chip_gap);
    }
}

static const char *info_title(const App *a, int tab, int idx)
{
    const GameList *gl = &a->lib->tab[tab];
    if (idx < 0 || idx >= gl->count) return "";
    return lib_title(a->lib, &gl->games[idx]);
}

static void draw_title_text(const char *t, float alpha, const UiMetrics *m, float maxw)
{
    float em;
    int trunc;
    if (!t[0] || alpha <= 0.0f) return;
    ui_title_fit(t, maxw, m->title_em, &em, &trunc);
    if (trunc) d_text_fit(&g_font_main, W() * 0.5f, m->title_y, em, sf_col_alpha(TH_TEXT, alpha), t, ALIGN_CENTER, maxw);
    else d_text(&g_font_main, W() * 0.5f, m->title_y, em, sf_col_alpha(TH_TEXT, alpha), t, ALIGN_CENTER);
}

void ui_update_targets(App *a)
{
    UiMetrics mm;
    const UiMetrics *m = &mm;
    const char *cur;
    float pad, maxw, em, target;
    int trunc;
    if (a->mode != MODE_FLOW) return;
    layout_ui(d_screen(), &mm);
    cur = info_title(a, a->info_tab, a->info_idx);
    pad = d_xu(m->title_pad);
    maxw = W() * m->title_max_w - 2.0f * pad;
    target = ui_title_fit(cur, maxw, m->title_em, &em, &trunc) + 2.0f * pad;
    if (!a->title_w_init) { spring_snap(&a->title_w, target); a->title_w_init = 1; }
    else spring_set(&a->title_w, target);
}

static void draw_info(App *a, float alpha, const UiMetrics *m)
{
    const char *cur = info_title(a, a->info_tab, a->info_idx);
    float pad = d_xu(m->title_pad);
    float maxw = W() * m->title_max_w - 2.0f * pad;
    float pw, t = ease_in_out(a->info_t);
    ui_update_targets(a);
    pw = sf_clampf(a->title_w.x, d_xu(m->title_h), W() * m->title_max_w);
    if (cur[0]) {
        d_rrect(W() * 0.5f - pw * 0.5f, m->title_y - m->title_h * 0.5f, pw, m->title_h, m->title_h * 0.5f, sf_col_alpha(TH_CHIP, alpha));
        d_rrect_stroke(W() * 0.5f - pw * 0.5f, m->title_y - m->title_h * 0.5f, pw, m->title_h, m->title_h * 0.5f,
                       0.0022f * H(), sf_col_alpha(accent_or(0xFFFFFF33u, 0.70f), alpha));
    }
    /* clip text to the (animating) pill so a wide old title never spills out */
    maxw = sf_minf(maxw, pw - 2.0f * pad * 0.6f);
    if (t < 1.0f) {
        draw_title_text(info_title(a, a->prev_tab, a->prev_idx), alpha * (1.0f - t), m, maxw);
        draw_chips(a, a->prev_tab, a->prev_idx, alpha * (1.0f - t), m);
    }
    draw_title_text(cur, alpha * t, m, maxw);
    draw_chips(a, a->info_tab, a->info_idx, alpha * t, m);
}

/* ------------------------------------------------------------------ */
/* Bottom bar                                                          */

static void group_add_badge(BarGroup *g, int icon, float size)
{
    BarItem *it = &g->item[g->nitems++];
    it->icon = icon;
    it->w = d_xu(size * icons_aspect(icon));
}

void ui_bar_layout(int panel_mode, BarLayout *L)
{
    const Screen *s = d_screen();
    UiMetrics m;
    int i, j;
    layout_ui(s, &m);
    memset(L, 0, sizeof(*L));
    L->em = m.bar_em;
    L->icon = m.icon_size;
    L->icon_gap = m.icon_gap;
    L->item_gap = m.icon_gap * 0.45f;
    L->bar_x = s->W * m.bar_margin_x;
    L->bar_w = s->W * (1.0f - 2.0f * m.bar_margin_x);
    L->bar_h = m.bar_h;
    L->bar_y = m.bar_y - m.bar_h * 0.5f;
    if (!panel_mode) {
        group_add_badge(&L->g[0], ICON_CROSS, L->icon);    L->g[0].label = "Launch";
        group_add_badge(&L->g[1], ICON_TRIANGLE, L->icon); L->g[1].label = "Game Settings";
        group_add_badge(&L->g[2], ICON_L2, L->icon);
        group_add_badge(&L->g[2], ICON_R2, L->icon);
        L->g[2].label = "A\xE2\x80\x93Z";
        group_add_badge(&L->g[3], ICON_START, L->icon);    L->g[3].label = "Flow Settings";
        group_add_badge(&L->g[4], ICON_CIRCLE, L->icon);   L->g[4].label = "Exit (Hold)";
        L->n = 5;
    } else {
        group_add_badge(&L->g[0], ICON_CROSS, L->icon);    L->g[0].label = "Change";
        group_add_badge(&L->g[1], ICON_CIRCLE, L->icon);   L->g[1].label = "Back";
        L->n = 2;
    }
    {
        float widths[BAR_MAX_GROUPS], xs[BAR_MAX_GROUPS];
        for (i = 0; i < L->n; i++) {
            BarGroup *g = &L->g[i];
            float w = 0.0f;
            for (j = 0; j < g->nitems; j++) w += g->item[j].w + (j ? d_xu(L->item_gap) : 0.0f);
            g->label_w = d_text_width(&g_font_main, L->em, g->label);
            w += d_xu(L->icon_gap) + g->label_w;
            g->w = w;
            widths[i] = w;
        }
        L->gap = layout_space_evenly(L->bar_x, L->bar_w, widths, L->n, xs);
        for (i = 0; i < L->n; i++) L->g[i].x = xs[i];
    }
}

static void draw_bar_groups(const App *a, const BarLayout *L, float alpha, int show_ring)
{
    int i, j;
    float cy = L->bar_y + L->bar_h * 0.5f;
    for (i = 0; i < L->n; i++) {
        const BarGroup *g = &L->g[i];
        float x = g->x;
        for (j = 0; j < g->nitems; j++) {
            const BarItem *it = &g->item[j];
            if (j) x += d_xu(L->item_gap);
            {
                float bx = x + it->w * 0.5f;
                float ring = show_ring && it->icon == ICON_CIRCLE ? a->hold.shown : 0.0f;
                d_badge(it->icon, bx, cy, L->icon, sf_col_alpha(0xFFFFFFFFu, alpha));
                if (ring > 0.0f) {
                    d_disc(bx, cy, L->icon * 0.47f, sf_col_alpha(0xFFFFFF33u, alpha * ring));   /* brighten */
                    d_arc(bx, cy, L->icon * 0.5f + 0.0075f * H(), 0.0042f * H(), 0.0f, 2.0f * PI_F * ring,
                          sf_col_alpha(accent_or(TH_EXIT_RING, 1.0f), alpha));
                }
            }
            x += it->w;
        }
        x += d_xu(L->icon_gap);
        d_text(&g_font_main, x, cy, L->em, sf_col_alpha(TH_TEXT_DIM, alpha), g->label, ALIGN_LEFT);
    }
}

static void draw_bar(const App *a, float alpha)
{
    BarLayout main_l, panel_l;
    float pe = ease_out_cubic(a->panel_anim);
    ui_bar_layout(0, &main_l);
    ui_bar_layout(1, &panel_l);
    d_rrect(main_l.bar_x, main_l.bar_y, main_l.bar_w, main_l.bar_h, main_l.bar_h * 0.5f, sf_col_alpha(TH_BAR, alpha));
    if (pe < 1.0f) draw_bar_groups(a, &main_l, alpha * (1.0f - pe), 1);
    if (pe > 0.0f) draw_bar_groups(a, &panel_l, alpha * pe, 0);
}

/* ------------------------------------------------------------------ */
/* Panels                                                              */

static void draw_value(float xr, float cy, float em, const char *val, int selected, int arrows, float alpha)
{
    uint32_t c = sf_col_alpha(selected ? TH_TEXT : TH_TEXT_DIM, alpha);
    if (arrows && selected) {
        float aw = d_text_width(&g_font_main, em, "\xE2\x80\xBA");
        float vw;
        d_text(&g_font_main, xr, cy, em, c, "\xE2\x80\xBA", ALIGN_RIGHT);
        xr -= aw + d_xu(em * 0.45f);
        vw = d_text(&g_font_main, xr, cy, em, c, val, ALIGN_RIGHT);
        xr -= vw + d_xu(em * 0.45f);
        d_text(&g_font_main, xr, cy, em, c, "\xE2\x80\xB9", ALIGN_RIGHT);
    } else {
        d_text(&g_font_main, xr, cy, em, c, val, ALIGN_RIGHT);
    }
}

static void draw_panel(App *a)
{
    float e = ease_out_cubic(a->panel_anim);
    float sc = 0.92f + 0.08f * e;
    int vis = a->nrows < PANEL_VISIBLE_ROWS ? a->nrows : PANEL_VISIBLE_ROWS, i;
    float pad = 0.032f * H() * sc, th = 0.082f * H() * sc, rh = 0.064f * H() * sc, rg = 0.010f * H() * sc;
    float pw = sf_minf(d_xu(1.02f * H()), W() * 0.9f) * sc;
    float ph = pad + th + (float)vis * (rh + rg) - rg + pad;
    float px = W() * 0.5f - pw * 0.5f, py = H() * 0.49f - ph * 0.5f;
    float em = 0.026f * H() * sc;
    float list_y = py + pad + th, ix = px + d_xu(pad), iw = pw - 2.0f * d_xu(pad);
    float sbw = 0.0f;
    const Game *g = a->panel == PANEL_GAME ? &a->lib->tab[a->panel_plat].games[a->panel_game] : NULL;
    if (e <= 0.0f) return;
    if (a->panel == PANEL_GAME && (a->panel_game < 0 || a->panel_game >= a->lib->tab[a->panel_plat].count)) return;

    d_rrect(px, py, pw, ph, 0.05f * H(), sf_col_alpha(TH_PANEL, e));
    d_rrect_stroke(px, py, pw, ph, 0.05f * H(), 0.0021f * H(), sf_col_alpha(TH_PANEL_OUTLINE, e));
    d_text(&g_font_main, ix + d_xu(rh * 0.3f), py + pad + th * 0.45f, 0.034f * H() * sc, sf_col_alpha(TH_TEXT, e),
           a->panel == PANEL_GAME ? "Game Settings" : "Flow Settings", ALIGN_LEFT);
    if (g) {
        float tw = d_text_width(&g_font_main, 0.034f * H() * sc, "Game Settings");
        d_text_fit(&g_font_main, ix + iw - d_xu(rh * 0.3f), py + pad + th * 0.45f, em * 0.92f, sf_col_alpha(TH_TEXT_FAINT, e),
                   lib_title(a->lib, g), ALIGN_RIGHT, iw - tw - d_xu(rh * 1.2f));
    }
    if (a->nrows > vis) sbw = d_xu(0.022f * H());
    /* rows */
    for (i = 0; i < a->nrows; i++) {
        float rel = (float)i - a->list_scroll.x;
        float ry = list_y + rel * (rh + rg);
        float fa = 1.0f;
        if (rel < -0.98f || rel > (float)vis - 0.02f) continue;
        if (rel < 0.0f) fa = 1.0f + rel;                       /* fade partially scrolled rows */
        else if (rel > (float)vis - 1.0f) fa = (float)vis - rel;
        fa = sf_clampf(fa, 0.0f, 1.0f) * e;
        d_rrect(ix, ry, iw - sbw, rh, rh * 0.5f, sf_col_alpha(TH_ROW, fa));
    }
    /* gliding highlight */
    {
        float rel = a->row_hl.x - a->list_scroll.x;
        float ry = list_y + rel * (rh + rg);
        if (rel > -1.0f && rel < (float)vis)
            d_rrect(ix, ry, iw - sbw, rh, rh * 0.5f, sf_col_alpha(accent_or(TH_ROW_HI, 0.50f), e));
    }
    for (i = 0; i < a->nrows; i++) {
        float rel = (float)i - a->list_scroll.x;
        float ry = list_y + rel * (rh + rg), cy = ry + rh * 0.5f;
        float fa = 1.0f, xr = ix + iw - sbw - d_xu(rh * 0.42f);
        RowKind k = a->rows[i];
        int sel = i == a->row;
        char val[48];
        if (rel < -0.98f || rel > (float)vis - 0.02f) continue;
        if (rel < 0.0f) fa = 1.0f + rel;
        else if (rel > (float)vis - 1.0f) fa = (float)vis - rel;
        fa = sf_clampf(fa, 0.0f, 1.0f) * e;
        d_text(&g_font_main, ix + d_xu(rh * 0.42f), cy, em, sf_col_alpha(sel ? TH_TEXT : TH_TEXT_DIM, fa), menu_label(k), ALIGN_LEFT);
        menu_value_text(k, a->panel_plat, &a->settings, &a->panel_cfg, val, sizeof(val));
        if (menu_is_slider(k)) {
            float tw = d_xu(0.20f * H() * sc), tx = xr - tw, th2 = 0.008f * H();
            float v = menu_slider_value(k, &a->settings);
            d_rrect(tx, cy - th2 * 0.5f, tw, th2, th2 * 0.5f, sf_col_alpha(TH_SLIDER_TRACK, fa));
            d_rrect(tx, cy - th2 * 0.5f, tw * v, th2, th2 * 0.5f, sf_col_alpha(accent_or(TH_SLIDER_FILL, 1.0f), fa));
            d_disc(tx + tw * v, cy, th2 * 1.3f, sf_col_alpha(TH_TEXT, fa));
            draw_value(tx - d_xu(em * 0.9f), cy, em, val, sel, sel, fa);
        } else if (val[0]) {
            draw_value(xr, cy, em, val, sel, sel && !menu_is_action(k), fa);
        }
    }
    /* scrollbar */
    if (a->nrows > vis) {
        float tx = ix + iw - sbw * 0.35f, ty = list_y, thh = (float)vis * (rh + rg) - rg;
        float frac = (float)vis / (float)a->nrows;
        float pos = a->list_scroll.x / (float)(a->nrows - vis);
        float bw = d_xu(0.005f * H());
        d_rrect(tx - bw * 0.5f, ty, bw, thh, 0.0025f * H(), sf_col_alpha(TH_SCROLL_TRACK, e));
        d_rrect(tx - bw * 0.5f, ty + (thh * (1.0f - frac)) * sf_clampf(pos, 0.0f, 1.0f), bw, thh * frac, 0.0025f * H(),
                sf_col_alpha(accent_or(TH_SCROLL_THUMB, 0.85f), e));
    }
}

/* ------------------------------------------------------------------ */
/* Overlays                                                            */

static void draw_letter(const App *a)
{
    float t = a->letter_t, al, sc, sz;
    if (t >= LETTER_TIME || !a->letter[0]) return;
    al = t < 0.35f ? 1.0f : 1.0f - (t - 0.35f) / (LETTER_TIME - 0.35f);
    sc = 0.85f + 0.15f * ease_out_back(sf_minf(1.0f, t / 0.22f));
    sz = 0.24f * H() * sc;
    d_rrect(W() * 0.5f - d_xu(sz) * 0.5f, H() * LY_FLOW_CY - sz * 0.5f, d_xu(sz), sz, 0.05f * H() * sc, sf_col_alpha(TH_LETTER_BOX, al));
    d_rrect_stroke(W() * 0.5f - d_xu(sz) * 0.5f, H() * LY_FLOW_CY - sz * 0.5f, d_xu(sz), sz, 0.05f * H() * sc, 0.002f * H(),
                   sf_col_alpha(TH_PANEL_OUTLINE, al));
    d_text(&g_font_big, W() * 0.5f, H() * LY_FLOW_CY, 0.16f * H() * sc, sf_col_alpha(TH_TEXT, al), a->letter, ALIGN_CENTER);
}

static void draw_toast(const App *a)
{
    float t = a->toast_t, al, em = 0.024f * H(), w, h = 0.05f * H(), y;
    if (t >= TOAST_TIME || !a->toast[0]) return;
    al = sf_minf(1.0f, t / 0.15f);
    if (t > TOAST_TIME - 0.4f) al = (TOAST_TIME - t) / 0.4f;
    w = sf_minf(d_text_width(&g_font_main, em, a->toast) + d_xu(h * 1.2f), W() * 0.7f);
    y = H() * 0.868f - (1.0f - ease_out_cubic(sf_minf(1.0f, t / 0.2f))) * 0.01f * H();
    d_rrect(W() * 0.5f - w * 0.5f, y - h * 0.5f, w, h, h * 0.5f, sf_col_alpha(TH_TOAST, al));
    d_rrect_stroke(W() * 0.5f - w * 0.5f, y - h * 0.5f, w, h, h * 0.5f, 0.0018f * H(), sf_col_alpha(TH_PANEL_OUTLINE, al));
    d_text_fit(&g_font_main, W() * 0.5f, y, em, sf_col_alpha(TH_TEXT, al), a->toast, ALIGN_CENTER, w - d_xu(h));
}

/* ------------------------------------------------------------------ */
/* Loading screen                                                      */

static void draw_loading(const App *a, float alpha)
{
    float t = (float)a->time;
    float wy = H() * 0.565f, em = 0.105f * H(), breathe = 0.75f + 0.25f * sinf(t * 1.6f);
    int i;
    /* ghostly glow: soft blob + faint offset copies */
    d_blob(W() * 0.5f, wy, 0.34f * H(), sf_col_alpha(TH_WORDMARK_GLOW, 0.09f * alpha * breathe));
    {
        /* two soft rings of faint offset copies approximate a blur */
        static const float rr[2] = { 0.008f, 0.017f }, aa[2] = { 0.030f, 0.016f };
        int ring;
        for (ring = 0; ring < 2; ring++) {
            for (i = 0; i < 12; i++) {
                float ang = (float)i * PI_F / 6.0f + t * 0.25f + (float)ring * 0.26f, r = rr[ring] * H();
                d_text(&g_font_big, W() * 0.5f + d_xu(cosf(ang) * r), wy + sinf(ang) * r, em,
                       sf_col_alpha(TH_WORDMARK_GLOW, aa[ring] * alpha * breathe), APP_NAME, ALIGN_CENTER);
            }
        }
    }
    d_text(&g_font_big, W() * 0.5f, wy, em, sf_col_alpha(TH_WORDMARK, alpha), APP_NAME, ALIGN_CENTER);
    /* the kittens: a slow float and a gentle "breath", with a soft shadow */
    {
        float bob = sinf(t * 1.3f), breath = 1.0f + 0.012f * sinf(t * 2.1f);
        float kh = 0.30f * H() * breath;
        float kw = d_xu(kh * (float)g_cats_w / (float)g_cats_h);
        float kcy = H() * 0.305f - bob * 0.010f * H();
        /* faint moonlight halo so the black kitten reads against the dark */
        d_blob(W() * 0.5f, kcy, 0.30f * H(), sf_col_alpha(TH_WORDMARK_GLOW, 0.10f * alpha * breathe));
        d_blob(W() * 0.5f, H() * 0.455f, 0.13f * H() * (1.0f - 0.05f * bob), sf_col_alpha(0x000000FFu, 0.55f * alpha));
        d_image(TEX_CATS, W() * 0.5f - kw * 0.5f, kcy - kh * 0.5f, kw, kh, sf_col_alpha(0xFFFFFFFFu, alpha));
    }
    d_text(&g_font_main, W() * 0.5f, H() * 0.685f, 0.024f * H(), sf_col_alpha(TH_TEXT_DIM, alpha), a->load_status, ALIGN_CENTER);
    {
        float bw = d_xu(0.42f * H()), bh = 0.0065f * H(), bx = W() * 0.5f - bw * 0.5f, by = H() * 0.725f;
        d_rrect(bx, by, bw, bh, bh * 0.5f, sf_col_alpha(TH_PROGRESS_TRACK, alpha));
        d_rrect(bx, by, bw * sf_clampf(a->load_shown, 0.0f, 1.0f), bh, bh * 0.5f, sf_col_alpha(accent_or(TH_PROGRESS_FILL, 1.0f), alpha));
    }
}

/* ------------------------------------------------------------------ */

void ui_draw(App *a)
{
    UiMetrics m;
    float intro, ia;
    int animated = a->settings.background == BG_ANIMATED || a->mode == MODE_LOADING;
    ui_set_color(a->settings.ui_color);
    layout_ui(d_screen(), &m);
    draw_background(a, 1.0f, animated);

    if (a->mode == MODE_LOADING) {
        draw_loading(a, 1.0f);
    } else {
        intro = ease_out_cubic(a->intro);
        ia = sf_clampf(a->intro * 1.6f - 0.2f, 0.0f, 1.0f);
        /* tabs */
        draw_tabs(a, ia, -(1.0f - intro) * 0.05f * H());
        /* cover rows (tab switch: old slides out, new slides in) */
        if (a->tab_anim < 1.0f && a->tab_prev != a->tab) {
            float e = ease_out_cubic(a->tab_anim), dist = W() * 0.45f;
            draw_flow_row(a, a->tab_prev, -(float)a->tab_dir * e * dist, (1.0f - e) * ia, 1);
            draw_flow_row(a, a->tab, (float)a->tab_dir * (1.0f - e) * dist, e * ia, 1);
        } else {
            draw_flow_row(a, a->tab, (1.0f - intro) * W() * 0.12f, ia, 1);
        }
        draw_info(a, ia, &m);
        if (a->panel != PANEL_NONE) {
            float pe = ease_out_cubic(a->panel_anim);
            d_rect(0, 0, W(), H(), sf_col_alpha(TH_DIM_OVERLAY, TH_DIM_MAX * pe));
            draw_panel(a);
        }
        draw_bar(a, ia);
        draw_letter(a);
        draw_toast(a);
        if (a->intro < 1.0f) {
            /* loading screen cross-fades out on top */
            float la = 1.0f - intro;
            draw_background(a, la, 1);
            draw_loading(a, la);
        }
    }
    if (a->exit_fade > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(TH_FADE, ease_in_out(a->exit_fade)));
}
