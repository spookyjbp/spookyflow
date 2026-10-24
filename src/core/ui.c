/* ui.c - draws a full frame from App state. Portable: only draw.h/render.h. */
#include "ui.h"
#include "cats.h"
#include "icons.h"
#include "render.h"
#include "stage.h"
#include "theme.h"
#include "title.h"
#include "util.h"
#include <math.h>
#include <stdlib.h>
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

static int g_case_tex[PLAT_COUNT] = { -1, -1, -1 };
void ui_set_case_image(int plat, int tex) { if (plat >= 0 && plat < PLAT_COUNT) g_case_tex[plat] = tex; }
void ui_clear_case_images(void) { int i; for (i = 0; i < PLAT_COUNT; i++) g_case_tex[i] = -1; layout_clear_case_images(); }

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
static int g_accent = UI_COLOR_BLUE;

void ui_set_color(int idx) { g_accent = (idx >= 0 && idx < UI_COLOR_COUNT) ? idx : UI_COLOR_WHITE; }
static int accent_white(void) { return g_accent == UI_COLOR_WHITE; }
static uint32_t accent(void) { return k_accent[g_accent]; }

/* All outlines breathe together: a slow, gentle glow (brighter and more
 * opaque at the peak), one cycle every OUTLINE_PULSE_PERIOD seconds. */
#define OUTLINE_PULSE_PERIOD 4.0f
static float g_glow = 1.0f;
static void set_glow(double time)
{
    float ph = (float)fmod(time, (double)OUTLINE_PULSE_PERIOD) / OUTLINE_PULSE_PERIOD;
    g_glow = 0.5f - 0.5f * cosf(ph * 2.0f * PI_F);       /* 0..1, smooth */
}
static uint32_t glow(uint32_t c)
{
    uint32_t lit = sf_col_mix(c, (c & 0xFFFFFF00u) == 0 ? c : (0xFFFFFF00u | (c & 255u)), 0.22f * g_glow);
    return sf_col_alpha(lit, 0.50f + 0.50f * g_glow);
}
/* accent at a given strength, or the original white at the original strength */
static uint32_t accent_or(uint32_t white_col, float color_alpha)
{
    return accent_white() ? white_col : sf_col_alpha(accent(), color_alpha);
}

static float W(void) { return d_screen()->W; }
static float PA(void) { return d_screen()->pa; }   /* x units -> vertical units */
static float H(void) { return d_screen()->H; }
static float clamp01(float v) { return sf_clampf(v, 0.0f, 1.0f); }

/* ------------------------------------------------------------------ */
/* Background                                                          */

/* ---- Backgrounds (Flow Settings > Background). All procedural, tinted by
 * the UI color, a handful of soft textured quads or thin strips each. ---- */

/* the UI color as a background tint (white -> a calm silver blue) */
static uint32_t bg_tint(void) { return accent_white() ? 0x6F86B4FFu : (accent() | 0xFFu); }

static uint32_t hue_shift(uint32_t c, float amt)
{
    /* rotate the hue a little (blend each channel towards its neighbor) */
    float r = (float)((c >> 24) & 255), g = (float)((c >> 16) & 255), b = (float)((c >> 8) & 255);
    float k = amt < 0.0f ? -amt : amt;
    if (amt >= 0.0f) { float t = r; r = sf_lerp(r, b, k); b = sf_lerp(b, g, k); g = sf_lerp(g, t, k); }
    else { float t = g; g = sf_lerp(g, b, k); b = sf_lerp(b, r, k); r = sf_lerp(r, t, k); }
    return sf_rgba((int)r, (int)g, (int)b, (int)(c & 255));
}

static float hash01(uint32_t i) { uint32_t h = i * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13; return (float)(h & 0xFFFF) / 65535.0f; }

static void vignette(float alpha)
{
    float e = W() * 0.16f, v = H() * 0.22f;
    d_rect_hgrad(0, 0, e, H(), sf_col_alpha(0x000000FFu, 0.55f * alpha), 0x00000000u);
    d_rect_hgrad(W() - e, 0, e, H(), 0x00000000u, sf_col_alpha(0x000000FFu, 0.55f * alpha));
    d_rect_grad(0, 0, W(), v, sf_col_alpha(0x000000FFu, 0.45f * alpha), 0x00000000u);
    d_rect_grad(0, H() - v, W(), v, 0x00000000u, sf_col_alpha(0x000000FFu, 0.55f * alpha));
}

static void bg_drift(float t, float alpha)
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
    for (i = 0; i < 6; i++) {
        const float *b = k_blob[i];
        float cx = W() * (b[0] + 0.10f * sinf(t * b[3] * 2.0f + b[5]));
        float cy = H() * (b[1] + 0.08f * cosf(t * b[4] * 2.0f + b[5] * 1.3f));
        float r = H() * b[2] * (1.0f + 0.06f * sinf(t * 0.11f + b[5]));
        uint32_t shape = accent_white() ? TH_BG_SHAPE : sf_col_mix(TH_BG_SHAPE, accent(), 0.35f);
        d_blob(cx, cy, r, sf_col_alpha(shape, TH_BG_SHAPE_ALPHA * alpha * (0.6f + 0.08f * (float)i)));
    }
}

/* Aurora: wide soft curtains of light drifting across the upper sky */
static void bg_aurora(float t, float alpha)
{
    uint32_t c0 = bg_tint();
    int i;
    for (i = 0; i < 5; i++) {
        float ph = (float)i * 1.37f;
        float cx = W() * (0.15f + 0.18f * (float)i + 0.08f * sinf(t * 0.045f + ph));
        float cy = H() * (0.24f + 0.07f * sinf(t * 0.06f + ph * 1.7f) + 0.03f * (float)(i & 1));
        float rx = W() * 0.62f * d_screen()->pa * (0.8f + 0.2f * sinf(t * 0.05f + ph));
        float ry = H() * (0.10f + 0.04f * sinf(t * 0.07f + ph * 0.6f));
        uint32_t c = hue_shift(c0, (float)(i - 2) * 0.22f);
        d_blob_ellipse(cx, cy, rx, ry, sf_col_alpha(sf_col_mix(c, 0x000000FFu, 0.35f), 0.33f * alpha));
    }
    /* a faint second layer, lower and slower */
    for (i = 0; i < 3; i++) {
        float ph = 4.0f + (float)i * 2.1f;
        d_blob_ellipse(W() * (0.25f + 0.25f * (float)i + 0.06f * sinf(t * 0.03f + ph)), H() * (0.52f + 0.04f * sinf(t * 0.05f + ph)),
                       W() * 0.5f * d_screen()->pa, H() * 0.07f, sf_col_alpha(sf_col_mix(hue_shift(c0, 0.35f), 0x000000FFu, 0.5f), 0.20f * alpha));
    }
    vignette(alpha * 0.8f);
}

/* Fireflies: small glowing motes rising slowly, twinkling */
static void bg_fireflies(float t, float alpha)
{
    uint32_t c = sf_col_mix(bg_tint(), 0xFFFFFFFFu, 0.35f);
    int i;
    d_blob(W() * 0.5f, H() * 1.05f, H() * 0.75f, sf_col_alpha(sf_col_mix(bg_tint(), 0x000000FFu, 0.55f), 0.30f * alpha));
    for (i = 0; i < 46; i++) {
        float sp = 0.012f + 0.020f * hash01((uint32_t)i * 7u + 1u);
        float y = 1.08f - fmodf(hash01((uint32_t)i * 3u + 2u) + t * sp, 1.16f);
        float x = hash01((uint32_t)i * 5u + 3u) + 0.025f * sinf(t * (0.3f + 0.4f * hash01((uint32_t)i)) + (float)i);
        float tw = 0.55f + 0.45f * sinf(t * (0.6f + 0.9f * hash01((uint32_t)i * 11u)) + (float)i * 2.3f);
        float r = H() * (0.004f + 0.006f * hash01((uint32_t)i * 13u + 5u));
        float edge = sf_clampf(sf_minf(y, 1.0f - y) * 6.0f, 0.0f, 1.0f);     /* fade near top/bottom */
        float al = alpha * tw * edge;
        d_blob(W() * x, H() * y, r * 6.0f, sf_col_alpha(c, 0.10f * al));      /* glow */
        d_blob(W() * x, H() * y, r * 1.6f, sf_col_alpha(0xFFFFFFFFu, 0.55f * al));   /* core */
    }
    vignette(alpha * 0.7f);
}

/* Ribbons: three thin glowing ribbons flowing across the lower screen */
/* one ribbon: a soft band along a wavy line across the whole width (core
 * color in the middle, clear at both edges) */
typedef struct { float base, slope, amp, k, w, ph, thick; } RibbonDef;
static void ribbon_band(const RibbonDef *d, float t, float scale, uint32_t core)
{
    enum { SEG = 44 };
    uint32_t clear = core & 0xFFFFFF00u;
    int i;
    if ((core & 255) == 0) return;
    r_tris_begin();
    for (i = 0; i < SEG; i++) {
        float u[2], y[2], h[2];
        int j;
        u[0] = (float)i / SEG; u[1] = (float)(i + 1) / SEG;
        for (j = 0; j < 2; j++) {
            float uu = u[j] * 1.1f - 0.05f;                    /* a little past both edges */
            y[j] = d->base + d->slope * (uu - 0.5f)
                 + d->amp * sinf(uu * d->k * 3.14159f + t * d->w + d->ph)
                 + d->amp * 0.35f * sinf(uu * 6.3f - t * d->w * 1.7f + d->ph * 1.3f);
            h[j] = d->thick * scale * (0.55f + 0.45f * sinf(uu * 4.7f + t * 0.17f + d->ph));
            u[j] = W() * uu;
        }
        r_vtx(u[0], y[0] - h[0], clear); r_vtx(u[1], y[1] - h[1], clear); r_vtx(u[1], y[1], core);
        r_vtx(u[0], y[0] - h[0], clear); r_vtx(u[1], y[1], core); r_vtx(u[0], y[0], core);
        r_vtx(u[0], y[0], core); r_vtx(u[1], y[1], core); r_vtx(u[1], y[1] + h[1], clear);
        r_vtx(u[0], y[0], core); r_vtx(u[1], y[1] + h[1], clear); r_vtx(u[0], y[0] + h[0], clear);
    }
    r_tris_end();
}

/* Ribbons: flowing bands of light over the whole screen, from the top to
 * the floor, each with a wide soft glow; they sway and drift on their own */
static void bg_ribbons(float t, float alpha)
{
    static const RibbonDef k_rib[] = {
        /* base   slope   amp    k     w      ph    thick (fractions of H) */
        { 0.10f,  0.10f, 0.045f, 1.6f, 0.13f, 0.0f, 0.014f },
        { 0.20f, -0.14f, 0.060f, 2.1f, 0.17f, 1.3f, 0.020f },
        { 0.31f,  0.18f, 0.050f, 1.4f, 0.11f, 2.6f, 0.012f },
        { 0.42f, -0.08f, 0.070f, 2.5f, 0.15f, 3.9f, 0.022f },
        { 0.53f,  0.12f, 0.055f, 1.8f, 0.19f, 5.2f, 0.016f },
        { 0.64f, -0.16f, 0.065f, 2.2f, 0.12f, 0.7f, 0.024f },
        { 0.75f,  0.09f, 0.060f, 2.0f, 0.16f, 2.0f, 0.018f },
        { 0.86f, -0.11f, 0.050f, 1.7f, 0.14f, 3.3f, 0.022f },
        { 0.95f,  0.07f, 0.040f, 2.4f, 0.18f, 4.6f, 0.015f },
    };
    uint32_t c0 = bg_tint();
    int n = (int)(sizeof(k_rib) / sizeof(k_rib[0])), r;
    for (r = 0; r < n; r++) {
        RibbonDef d = k_rib[r];
        float breathe = 0.75f + 0.25f * sinf(t * (0.21f + 0.03f * (float)r) + d.ph);
        uint32_t c = hue_shift(c0, ((float)(r % 3) - 1.0f) * 0.22f);
        d.base *= H(); d.slope *= H(); d.amp *= H(); d.thick *= H();
        d.base += H() * 0.012f * sinf(t * 0.05f + d.ph);           /* slow drift up and down */
        ribbon_band(&d, t, 5.0f, sf_col_alpha(c, 0.13f * alpha * breathe));                                     /* glow */
        ribbon_band(&d, t, 1.4f, sf_col_alpha(c, 0.42f * alpha * breathe));                                   /* body */
        ribbon_band(&d, t, 0.35f, sf_col_alpha(sf_col_mix(c, 0xFFFFFFFFu, 0.55f), 0.55f * alpha * breathe)); /* bright core */
    }
    vignette(alpha * 0.45f);
}

/* Halo: a calm glow behind the selected game, breathing very slowly */
static void bg_halo(float t, float alpha)
{
    uint32_t c = sf_col_mix(bg_tint(), 0x000000FFu, 0.35f);
    float br = 0.85f + 0.15f * sinf(t * 0.35f);
    d_blob(W() * 0.5f, H() * layout_flow_cy(), H() * 0.62f * br, sf_col_alpha(c, 0.42f * alpha));
    d_blob(W() * 0.5f, H() * layout_flow_cy(), H() * 0.30f, sf_col_alpha(sf_col_mix(c, 0xFFFFFFFFu, 0.2f), 0.18f * alpha * br));
    d_blob_ellipse(W() * 0.5f, H() * 1.02f, W() * 0.6f * d_screen()->pa, H() * 0.18f, sf_col_alpha(c, 0.25f * alpha));
    vignette(alpha);
}

/* Twilight: deep sky fading to a soft glowing horizon, with slow low mist */
static void bg_twilight(float t, float alpha)
{
    uint32_t c = bg_tint(), sky = sf_col_mix(c, 0x05060AFFu, 0.88f), hor = sf_col_mix(c, 0x05060AFFu, 0.62f);
    int i;
    d_rect_grad(0, 0, W(), H() * 0.62f, sf_col_alpha(0x05060AFFu, alpha), sf_col_alpha(sky, alpha));
    d_rect_grad(0, H() * 0.62f, W(), H() * 0.38f, sf_col_alpha(sky, alpha), sf_col_alpha(hor, alpha));
    d_blob_ellipse(W() * 0.5f, H() * 0.98f, W() * 0.8f * d_screen()->pa, H() * 0.20f, sf_col_alpha(sf_col_mix(c, 0xFFFFFFFFu, 0.15f), 0.22f * alpha));
    for (i = 0; i < 4; i++) {
        float ph = (float)i * 1.9f;
        float x = fmodf(0.2f + 0.3f * (float)i + t * (0.004f + 0.002f * (float)i), 1.6f) - 0.3f;
        d_blob_ellipse(W() * x, H() * (0.84f + 0.04f * sinf(t * 0.05f + ph)), W() * 0.35f * d_screen()->pa, H() * 0.06f,
                       sf_col_alpha(sf_col_mix(c, 0xFFFFFFFFu, 0.4f), 0.07f * alpha));
    }
    vignette(alpha * 0.6f);
}


/* ---- Stage: the built-in picture with its button shapes floating ---- */
static int g_stage_tex = -1, g_stage_shapes = -1, g_stage_hi = -1;
static int g_bg_tex = -1;          /* the user's picture (BG_CUSTOM), loaded by the host */
void ui_set_background_image(int tex) { g_bg_tex = tex; }
void ui_set_stage(int bg_tex, int shapes_tex) { g_stage_tex = bg_tex; g_stage_shapes = shapes_tex; }
void ui_set_stage_highlight(int tex) { g_stage_hi = tex; }

static int g_stage_fx = -1;
void ui_set_stage_fx(int tex) { g_stage_fx = tex; }
static int g_stage_rib = -1;
void ui_set_stage_ribbons(int tex) { g_stage_rib = tex; }
static int g_stage_glow = -1;
void ui_set_stage_glow(int tex) { g_stage_glow = tex; }

/* one atlas element stretched over (x, y, w, h), tinted; flip = mirrored left-right */
static void stage_fx_flip(int el, float x, float y, float w, float h, int flip, uint32_t tint)
{
    const int *r = g_stage_fx_rect[el];
    float aw = (float)g_stage_fx_size[0], ah = (float)g_stage_fx_size[1];
    float u0 = ((float)r[0] + 0.5f) / aw, u1 = ((float)(r[0] + r[2]) - 0.5f) / aw;
    if (flip) { float tmp = u0; u0 = u1; u1 = tmp; }
    float v0 = ((float)r[1] + 0.5f) / ah, v1 = ((float)(r[1] + r[3]) - 0.5f) / ah;
    if ((tint & 255) == 0) return;
    r_quads_begin(g_stage_fx);
    r_qvtx(x, y, u0, v0, tint);
    r_qvtx(x + w, y, u1, v0, tint);
    r_qvtx(x + w, y + h, u1, v1, tint);
    r_qvtx(x, y + h, u0, v1, tint);
    r_quads_end();
}
static void stage_fx(int el, float x, float y, float w, float h, uint32_t tint) { stage_fx_flip(el, x, y, w, h, 0, tint); }

/* one cell of the STAGE_FX_DOTS grid, centered on (cx, cy) */
static void stage_dot(int cell, float cx, float cy, float sz, uint32_t tint)
{
    const int *r = g_stage_fx_rect[STAGE_FX_DOTS];
    float aw = (float)g_stage_fx_size[0], ah = (float)g_stage_fx_size[1];
    float cw = (float)r[2] / (float)STAGE_DOT_COLS, chh = (float)r[3] / (float)STAGE_DOT_ROWS;
    float x0 = (float)r[0] + (float)(cell % STAGE_DOT_COLS) * cw, y0 = (float)r[1] + (float)(cell / STAGE_DOT_COLS) * chh;
    float u0 = (x0 + 0.5f) / aw, u1 = (x0 + cw - 0.5f) / aw, v0 = (y0 + 0.5f) / ah, v1 = (y0 + chh - 0.5f) / ah;
    float h = sz * 0.5f, w = d_xu(sz) * 0.5f;
    if ((tint & 255) == 0) return;
    r_quads_begin(g_stage_fx);
    r_qvtx(cx - w, cy - h, u0, v0, tint);
    r_qvtx(cx + w, cy - h, u1, v0, tint);
    r_qvtx(cx + w, cy + h, u1, v1, tint);
    r_qvtx(cx - w, cy + h, u0, v1, tint);
    r_quads_end();
}

/* 0..1 from an integer (stable per particle and per life) */
static float stage_rand(unsigned n)
{
    n ^= n >> 16; n *= 0x7feb352du; n ^= n >> 15; n *= 0x846ca68bu; n ^= n >> 16;
    return (float)(n & 0xFFFFFFu) / 16777216.0f;
}

/* Particles, each on its own: born at a random spot, it fades in, drifts and
 * rises at a pace set by its depth (near = bigger, faster, brighter),
 * sways and twinkles, fades out and is born again somewhere else. */
#define STAGE_N_SPARK 80
#define STAGE_N_GLOW  30
static void stage_particles(float t, float alpha, float hy, uint32_t lite)
{
    int i, n = STAGE_N_SPARK + STAGE_N_GLOW;
    for (i = 0; i < n; i++) {
        int kind = i < STAGE_N_GLOW ? 1 : 0;              /* glow dots drawn first */
        unsigned seed = (unsigned)i * 7919u + 17u;
        float life = 7.0f + 7.0f * stage_rand(seed);
        float tt = t + life * stage_rand(seed + 1u) + 1000.0f;
        float cyc = floorf(tt / life), ph = tt / life - cyc;
        unsigned cs = seed ^ ((unsigned)cyc * 2654435761u);
        float z = stage_rand(cs + 3u), age = ph * life;
        float dir = stage_rand(cs + 6u) < 0.5f ? -1.0f : 1.0f;
        float x = W() * (-0.05f + 1.10f * stage_rand(cs + 4u)) + dir * W() * (0.003f + 0.010f * z) * age
                  + H() * 0.012f * z * sinf(t * (0.35f + 0.4f * stage_rand(cs + 7u)) + 6.28f * stage_rand(cs + 8u));
        float y = hy * (0.04f + 0.92f * stage_rand(cs + 5u)) - H() * (0.003f + 0.010f * z) * age;
        float env = sinf(3.14159f * ph), sz, al, tw;
        int cell;
        env = env * env * (3.0f - 2.0f * env);                       /* soft in and out */
        if (kind == 0) {                                             /* spark: tiny, sharp, twinkling */
            tw = 0.5f + 0.5f * sinf(t * (1.5f + 3.0f * stage_rand(cs + 9u)) + 6.28f * stage_rand(cs + 10u));
            sz = H() * (0.016f + 0.028f * z);
            al = (0.55f + 0.45f * z) * (0.35f + 0.65f * tw * tw);
            cell = STAGE_DOT_SPARK + (int)(stage_rand(cs + 11u) * STAGE_DOT_NSPARK);
        } else {                                                     /* glow dot: soft, breathing */
            tw = 0.75f + 0.25f * sinf(t * (0.6f + stage_rand(cs + 9u)) + 6.28f * stage_rand(cs + 10u));
            sz = H() * (0.024f + 0.036f * z);
            al = (0.40f + 0.45f * z) * tw;
            cell = STAGE_DOT_GLOW + (int)(stage_rand(cs + 11u) * STAGE_DOT_NGLOW);
        }
        if (cell >= STAGE_DOT_GLOW + STAGE_DOT_NGLOW) cell = STAGE_DOT_GLOW + STAGE_DOT_NGLOW - 1;
        stage_dot(cell, x, y, sz, sf_col_alpha(lite, alpha * al * env));
    }
}

typedef struct { unsigned char kind, angle; float x, y, size, alpha; } StageShape;
/* kind: 0 triangle, 1 circle, 2 cross, 3 square; angle: one of two views;
 * position as a fraction of the screen, size as a fraction of its height.
 * They float in the open sky beside the title, big ones at the outside. */
static const StageShape k_stage[] = {
    { 0, 0, 0.080f, 0.150f, 0.180f, 1.00f }, { 1, 1, 0.195f, 0.070f, 0.100f, 0.85f }, { 2, 1, 0.205f, 0.250f, 0.070f, 0.70f },
    { 3, 1, 0.920f, 0.150f, 0.175f, 1.00f }, { 2, 0, 0.850f, 0.060f, 0.095f, 0.85f }, { 1, 0, 0.795f, 0.250f, 0.070f, 0.70f },
};

/* the UI color at full brightness (its largest channel at 255); White -> silver */
static uint32_t stage_tint(void)
{
    uint32_t c = accent_white() ? 0xC4CEE2FFu : (accent() | 0xFFu);
    int r = (int)(c >> 24), g = (int)((c >> 16) & 255), b = (int)((c >> 8) & 255), m = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (m <= 0) return 0xFFFFFFFFu;
    return sf_rgba(r * 255 / m, g * 255 / m, b * 255 / m, 255);
}

/* ---- Stage, pre-composited (0.6.5) ------------------------------------
 * The base scene, its white-hot cores and its glow were three full-screen
 * blended layers, and each ribbon set three big ones; at 1080p that was
 * more than the RSX can fill in a frame. They are now combined on the CPU
 * into one opaque scene texture and one RGBA texture for both ribbon sets,
 * redone when the UI color changes (the tint is baked in). */
static const unsigned char *g_src_base, *g_src_rib, *g_src_glow;
static int g_src_bw, g_src_bh, g_src_rw, g_src_rh, g_src_gw, g_src_gh;
static int g_stage_baked;          /* 1: TEX_STAGE / TEX_STAGE_RIB hold the composites */
static float g_intro_shapes = 1.0f, g_intro_parts = 1.0f;   /* loading reveal: shapes / particles fading in */
static uint32_t g_baked_tint;
static unsigned char *g_bake_buf;
static size_t g_bake_cap;
static unsigned char *g_prep_glow;     /* bw x bh: 0.55 x glow over the scene */
static unsigned char *g_prep_rib;      /* rw x rh/2 x 4: A, wg, wb, wc (0..255) */
static int g_job_phase = -1, g_job_row, g_prepared;   /* the composite job (stage_job) */

void ui_set_stage_sources(const unsigned char *base, int bw, int bh, const unsigned char *rib, int rw, int rh,
                          const unsigned char *glow, int gw, int gh)
{
    g_src_base = base; g_src_bw = bw; g_src_bh = bh;
    g_src_rib = rib; g_src_rw = rw; g_src_rh = rh;
    g_src_glow = glow; g_src_gw = gw; g_src_gh = gh;
    g_stage_baked = 0;
    free(g_prep_glow); free(g_prep_rib);
    g_prep_glow = g_prep_rib = NULL;
    g_prepared = 0;
    g_job_phase = -1;
}

int ui_stage_baked(void) { return g_stage_baked; }

/* Upsample the glow map (bilinear, fixed point: no int<->float conversions,
 * which stall the PS3's PPU) from region gx0..gx0+gw_r, gy0..gy0+gh_r (in
 * 1/256 glow pixels) to w x h, scaled by k/256. */
static void glow_resample(unsigned char *out, int w, int h, int gx0, int gw_r, int gy0, int gh_r, int k, int y_from, int y_to)
{
    int x, y;
    for (y = y_from; y < y_to && y < h; y++) {
        int gy = gy0 + (int)(((long long)(2 * y + 1) * gh_r) / (2 * h)) - 128, y0, y1, fy;
        if (gy < 0) gy = 0;
        y0 = gy >> 8; fy = gy & 255;
        if (y0 >= g_src_gh - 1) { y0 = g_src_gh - 1; fy = 0; }
        y1 = y0 + 1 < g_src_gh ? y0 + 1 : y0;
        for (x = 0; x < w; x++) {
            int gx = gx0 + (int)(((long long)(2 * x + 1) * gw_r) / (2 * w)) - 128, x0, x1, fx, a, b, v;
            const unsigned char *r0, *r1;
            if (gx < 0) gx = 0;
            x0 = gx >> 8; fx = gx & 255;
            if (x0 >= g_src_gw - 1) { x0 = g_src_gw - 1; fx = 0; }
            x1 = x0 + 1 < g_src_gw ? x0 + 1 : x0;
            r0 = g_src_glow + y0 * g_src_gw; r1 = g_src_glow + y1 * g_src_gw;
            a = r0[x0] * (256 - fx) + r0[x1] * fx;
            b = r1[x0] * (256 - fx) + r1[x1] * fx;
            v = (a * (256 - fy) + b * fy) >> 16;          /* 0..255 */
            out[(size_t)y * w + x] = (unsigned char)((v * k) >> 8);
        }
    }
}

/* 0.7.5: the composite is made by a job in small steps (rows), so a UI
 * Color change (or the first Stage composite after starting on another
 * background) never stalls a frame: the old composite stays on screen
 * until the new one is complete, then both textures are replaced at once.
 *   phase 0  colour-independent: the glow over the scene (0.55)
 *   phase 1  colour-independent: per ribbon pixel, the weights of glow /
 *            body / core in one "over" layer
 *   phase 2  the scene in the job's tint
 *   phase 3  the ribbons in the job's tint, then both textures are updated */
#define STAGE_JOB_STEPS 12          /* a phase takes about this many frames */
static unsigned char *g_bake_rib, *g_gl_tmp;
static uint32_t g_job_tint;
static unsigned char g_job_lut[256][3];
static int g_job_lite[3];

static int stage_alloc(void)
{
    int rw = g_src_rw, rh = g_src_rh / 2, half = rw / 2;
    size_t need_b = (size_t)g_src_bw * g_src_bh * 4u;
    if (!g_prep_glow) g_prep_glow = (unsigned char *)malloc((size_t)g_src_bw * g_src_bh);
    if (!g_prep_rib) g_prep_rib = (unsigned char *)malloc((size_t)rw * rh * 4u);
    if (!g_gl_tmp) g_gl_tmp = (unsigned char *)malloc((size_t)half * rh * 2u);
    if (!g_bake_rib) g_bake_rib = (unsigned char *)malloc((size_t)rw * rh * 4u);
    if (g_bake_cap < need_b) {
        free(g_bake_buf);
        g_bake_buf = (unsigned char *)malloc(need_b);
        g_bake_cap = g_bake_buf ? need_b : 0;
    }
    return g_prep_glow && g_prep_rib && g_gl_tmp && g_bake_rib && g_bake_buf;
}

static void stage_job_tint(uint32_t tint)
{
    int tr = (int)(tint >> 24), tg = (int)((tint >> 16) & 255), tb = (int)((tint >> 8) & 255), i;
    g_job_tint = tint;
    g_job_lite[0] = tr + (255 - tr) * 89 / 255;       /* lite: 35% white */
    g_job_lite[1] = tg + (255 - tg) * 89 / 255;
    g_job_lite[2] = tb + (255 - tb) * 89 / 255;
    /* black -> base (tint x brightness) -> white-hot cores (0.35) */
    for (i = 0; i < 256; i++) {
        float v = (float)i / 255.0f, hx = (float)i - 150.0f, hi = 0.0f, a, c[3];
        int k;
        if (hx > 0.0f) { hi = hx * hx / (105.0f * 105.0f); if (hi > 1.0f) hi = 1.0f; }   /* = stage_highlight */
        a = 0.35f * hi;
        c[0] = (float)tr / 255.0f * v; c[1] = (float)tg / 255.0f * v; c[2] = (float)tb / 255.0f * v;
        for (k = 0; k < 3; k++) g_job_lut[i][k] = (unsigned char)((c[k] + (1.0f - c[k]) * a) * 255.0f + 0.5f);
    }
}

/* one step of the job (all of it when sync); returns 1 when the textures
 * hold `tint` */
static int stage_job(uint32_t tint, int sync)
{
    int rw = g_src_rw, rh = g_src_rh / 2, half = rw / 2;
    int split = (int)((float)g_src_gh * STAGE_GLOW_SPLIT + 0.5f);   /* glow rows 0..split: the scene */
    if (!g_src_base || !g_src_rib || !g_src_glow) return 0;
    if (g_stage_baked && g_baked_tint == tint && g_job_phase < 0) return 1;
    if (g_job_phase < 0 || (g_job_phase >= 2 && g_job_tint != tint)) {     /* start, or restart in the new tint */
        if (!stage_alloc()) {        /* no memory: the layered drawing, from the brightness maps */
            if (r_texture_upload_alpha(TEX_STAGE, g_src_bw, g_src_bh, g_src_base) == 0) g_stage_tex = TEX_STAGE;
            g_stage_baked = 0;
            g_src_base = NULL;
            return 0;
        }
        g_job_phase = g_prepared ? 2 : 0;
        g_job_row = 0;
    }
    if (g_job_tint != tint || g_job_phase < 2) stage_job_tint(tint);
    do {
        int rows, y1, y, x, i;
        switch (g_job_phase) {
        case 0:                  /* the glow over the scene */
            rows = sync ? g_src_bh : (g_src_bh + STAGE_JOB_STEPS - 1) / STAGE_JOB_STEPS;
            y1 = g_job_row + rows;
            glow_resample(g_prep_glow, g_src_bw, g_src_bh, 0, g_src_gw * 256, 0, split * 256, 141, g_job_row, y1);
            g_job_row = y1;
            if (g_job_row >= g_src_bh) { g_job_phase = 1; g_job_row = 0; }
            break;
        case 1:                  /* ribbon weights: left | right set (glow columns 0..gw/2 | gw/2..gw, rows split..gh) */
            rows = sync ? rh : (rh + STAGE_JOB_STEPS - 1) / STAGE_JOB_STEPS;
            y1 = g_job_row + rows;
            if (y1 > rh) y1 = rh;
            for (i = 0; i < 2; i++)
                glow_resample(g_gl_tmp + (size_t)i * half * rh, half, rh, i * (g_src_gw / 2) * 256, (g_src_gw / 2) * 256,
                              split * 256, (g_src_gh - split) * 256, 141, g_job_row, y1);
            for (y = g_job_row; y < y1; y++) {
                const unsigned char *body = g_src_rib + (size_t)y * rw, *core = g_src_rib + (size_t)(y + rh) * rw;
                for (i = 0; i < 2; i++) {
                    const unsigned char *gl = g_gl_tmp + (size_t)i * half * rh + (size_t)y * half;
                    for (x = 0; x < half; x++) {
                        int c = i * half + x;
                        int ag = gl[x], ab = body[c], ac = (core[c] * 217) >> 8;   /* 0.85 */
                        int qb = 255 - ab, qc = 255 - ac, qg = 255 - ag;
                        int wg = ag * qb / 255 * qc / 255, wb = ab * qc / 255, wc = ac;
                        int A = 255 - qg * qb / 255 * qc / 255;
                        unsigned char *o = g_prep_rib + ((size_t)y * rw + c) * 4u;
                        o[0] = (unsigned char)A; o[1] = (unsigned char)wg; o[2] = (unsigned char)wb; o[3] = (unsigned char)wc;
                    }
                }
            }
            g_job_row = y1;
            if (g_job_row >= rh) { g_job_phase = 2; g_job_row = 0; g_prepared = 1; }
            break;
        case 2: {                /* the scene in the tint, glow on top (lite) */
            int lr = g_job_lite[0], lg = g_job_lite[1], lb = g_job_lite[2];
            rows = sync ? g_src_bh : (g_src_bh + STAGE_JOB_STEPS - 1) / STAGE_JOB_STEPS;
            y1 = g_job_row + rows;
            if (y1 > g_src_bh) y1 = g_src_bh;
            for (i = g_job_row * g_src_bw; i < y1 * g_src_bw; i++) {
                const unsigned char *c = g_job_lut[g_src_base[i]];
                int a = g_prep_glow[i];
                unsigned char *o = g_bake_buf + (size_t)i * 4u;
                o[0] = (unsigned char)(c[0] + ((lr - c[0]) * a) / 255);
                o[1] = (unsigned char)(c[1] + ((lg - c[1]) * a) / 255);
                o[2] = (unsigned char)(c[2] + ((lb - c[2]) * a) / 255);
                o[3] = 255;
            }
            g_job_row = y1;
            if (g_job_row >= g_src_bh) { g_job_phase = 3; g_job_row = 0; }
            break;
        }
        case 3: {                /* ribbons: colour = (lite * wg + tint * wb + white * wc) / A */
            int lr = g_job_lite[0], lg = g_job_lite[1], lb = g_job_lite[2];
            int tr = (int)(g_job_tint >> 24), tg = (int)((g_job_tint >> 16) & 255), tb = (int)((g_job_tint >> 8) & 255);
            rows = sync ? rh : (rh + STAGE_JOB_STEPS - 1) / STAGE_JOB_STEPS;
            y1 = g_job_row + rows;
            if (y1 > rh) y1 = rh;
            for (i = g_job_row * rw; i < y1 * rw; i++) {
                const unsigned char *w = g_prep_rib + (size_t)i * 4u;
                unsigned char *o = g_bake_rib + (size_t)i * 4u;
                int A = w[0];
                if (A > 0) {
                    int r = (lr * w[1] + tr * w[2] + 255 * w[3]) / A, g = (lg * w[1] + tg * w[2] + 255 * w[3]) / A,
                        b = (lb * w[1] + tb * w[2] + 255 * w[3]) / A;
                    o[0] = (unsigned char)(r > 255 ? 255 : r); o[1] = (unsigned char)(g > 255 ? 255 : g); o[2] = (unsigned char)(b > 255 ? 255 : b);
                } else {
                    o[0] = (unsigned char)lr; o[1] = (unsigned char)lg; o[2] = (unsigned char)lb;
                }
                o[3] = (unsigned char)A;
            }
            g_job_row = y1;
            if (g_job_row >= rh) {
                g_job_phase = -1;
                if (r_texture_update_rgba(TEX_STAGE, g_src_bw, g_src_bh, g_bake_buf) == 0 &&
                    r_texture_update_rgba(TEX_STAGE_RIB, g_src_rw, rh, g_bake_rib) == 0) {
                    g_stage_tex = TEX_STAGE;
                    g_stage_rib = TEX_STAGE_RIB;
                    g_stage_baked = 1;
                    g_baked_tint = g_job_tint;
                }
                return g_stage_baked && g_baked_tint == tint;
            }
            break;
        }
        default:
            g_job_phase = -1;
            return 0;
        }
    } while (sync);
    return 0;
}

/* everything at once (loading screen, or Stage shown with nothing made yet) */
static void stage_bake(uint32_t tint) { stage_job(tint, 1); }

static void bg_stage(float t, float alpha)
{
    uint32_t tint = stage_tint(), lite = sf_col_mix(tint, 0xFFFFFFFFu, 0.35f);
    float hy = H() * LY_HORIZON_Y;
    int i;
    if (g_src_base && !g_stage_baked) stage_bake(tint);              /* nothing to show yet: now */
    else if (g_src_base && g_baked_tint != tint) stage_job(tint, 0);  /* new colour: in steps, the old one meanwhile */
    if (g_stage_baked) {
        /* one opaque layer: no blending, so the RSX doesn't read the screen back */
        if (alpha >= 0.999f) r_set_blend(0);
        d_image(g_stage_tex, 0, 0, W(), H(), sf_col_alpha(0xFFFFFFFFu, alpha));
        if (alpha >= 0.999f) r_set_blend(1);
    } else {
    /* base scene: brightness tinted with the UI color, white-hot cores on top */
    d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, alpha));
    d_image(g_stage_tex, 0, 0, W(), H(), sf_col_alpha(tint, alpha));
    if (g_stage_hi >= 0) d_image(g_stage_hi, 0, 0, W(), H(), sf_col_alpha(0xFFFFFFFFu, 0.35f * alpha));
    }
    /* glow: soft halos around the lights, breathing slowly */
    if (g_stage_glow >= 0 && !g_stage_baked)
        d_image_rot(g_stage_glow, W() * 0.5f, H() * 0.5f, W() * PA(), H(), 0.0f, 0.0f, 1.0f, 0.0f, STAGE_GLOW_SPLIT,
                    sf_col_alpha(lite, alpha * (0.55f + 0.15f * sinf(t * 0.45f))));
    /* light ribbons: the left and right wave sets sway, tilt and breathe on
     * their own (slightly oversized so their outer edges never show) */
    if (g_stage_rib >= 0) {
        for (i = 0; i < 2; i++) {
            float ph = (float)i * 2.3f, sgn = i ? 1.0f : -1.0f;
            float sx = 1.07f + 0.02f * sinf(t * 0.08f + ph), sy = 1.06f + 0.03f * sinf(t * 0.06f + ph * 1.7f);
            /* d_image_rot takes widths in vertical units (it rotates a true
             * shape), so a width measured in screen x units is scaled by PA() */
            float w = W() * (1.0f - STAGE_RIB_SPLIT) * sx * PA(), h = hy * sy;
            float cx = W() * (0.5f + sgn * (0.5f - (1.0f - STAGE_RIB_SPLIT) * 0.5f)) + sgn * W() * 0.01f
                       + W() * 0.012f * sinf(t * 0.05f + ph);
            /* bob only upward, and kept clear of the horizon even when tilted */
            float cy = hy - h * 0.5f - H() * (0.022f + 0.012f * (1.0f + sinf(t * 0.09f + ph * 1.4f)));
            float ang = sgn * 0.015f * sinf(t * 0.07f + ph);
            float al = alpha * (0.80f + 0.20f * sinf(t * 0.21f + ph));
            float u0 = (float)i * 0.5f;
            if (g_stage_baked) {      /* glow, body and core in one texture */
                d_image_rot(g_stage_rib, cx, cy, w, h, ang, u0, u0 + 0.5f, 0.0f, 1.0f, sf_col_alpha(0xFFFFFFFFu, al));
                continue;
            }
            if (g_stage_glow >= 0)    /* the ribbons' own glow moves with them */
                d_image_rot(g_stage_glow, cx, cy, w, h, ang, u0, u0 + 0.5f, STAGE_GLOW_SPLIT, 1.0f, sf_col_alpha(lite, al * 0.55f));
            d_image_rot(g_stage_rib, cx, cy, w, h, ang, u0, u0 + 0.5f, 0.0f, 0.5f, sf_col_alpha(tint, al));
            d_image_rot(g_stage_rib, cx, cy, w, h, ang, u0, u0 + 0.5f, 0.5f, 1.0f, sf_col_alpha(0xFFFFFFFFu, al * 0.85f));
        }
    }
    if (g_stage_fx >= 0) {
        /* horizon: a bright line breathing along it, glows pulsing at the floor
         * lights */
        {
            static const float lx[3] = { 0.146f, 0.495f, 0.846f };
            float lw = W() * 1.05f, lh = H() * 0.05f;
            stage_fx(STAGE_FX_LINE, W() * 0.5f - lw * 0.5f, hy - lh * 0.5f, lw, lh,
                     sf_col_alpha(lite, alpha * (0.45f + 0.15f * sinf(t * 0.3f))));
            for (i = 0; i < 3; i++) {
                float p = 0.75f + 0.25f * sinf(t * (0.5f + 0.13f * (float)i) + (float)i * 2.4f);
                float fw = W() * 0.22f * p, fh = H() * 0.07f * p;
                stage_fx(STAGE_FX_GLOW, W() * lx[i] - fw * 0.5f, hy - fh * 0.5f, fw, fh, sf_col_alpha(lite, alpha * 0.75f * p));
            }
        }
        stage_particles(t, alpha * g_intro_parts, hy, lite);
        /* fog hugging the floor: three seamless layers at different depths drifting
         * at their own slow pace (near = bigger, faster), each thickening and
         * thinning over time; a far, thin veil also lies across the horizon */
        {
            static const struct { float w, h, y, sp, a, ph; } fl[3] = {
                { 1.70f, 0.12f, -0.035f,  0.0040f, 0.42f, 0.0f },   /* veil on the horizon */
                { 1.35f, 0.20f,  0.045f, -0.0060f, 0.38f, 2.1f },   /* mid floor */
                { 1.10f, 0.30f,  0.170f,  0.0095f, 0.30f, 4.3f },   /* near floor */
            };
            uint32_t fc = sf_col_mix(tint, 0xFFFFFFFFu, 0.45f);
            for (i = 0; i < 3; i++) {
                float tw = W() * fl[i].w, th = H() * fl[i].h;
                float y = hy + H() * fl[i].y - th * 0.5f + H() * 0.006f * sinf(t * 0.05f + fl[i].ph);
                float off = fmodf(t * W() * fl[i].sp, tw), x;
                float al = alpha * fl[i].a * (0.80f + 0.20f * sinf(t * 0.07f + fl[i].ph));
                if (off > 0.0f) off -= tw;
                for (x = off; x < W(); x += tw) stage_fx(STAGE_FX_MIST, x, y, tw, th, sf_col_alpha(fc, al));
            }
        }
    }
    if (g_stage_shapes < 0) return;
    /* the shapes float, bob, tilt and breathe, each at its own pace */
    for (i = 0; i < (int)(sizeof(k_stage) / sizeof(k_stage[0])); i++) {
        const StageShape *s = &k_stage[i];
        float p = (float)i * 1.7f, k = 0.5f + s->size * 2.5f;                 /* big = near: moves more */
        float sz = s->size * H();
        float cx = W() * (s->x + 0.012f * k * sinf(t * 0.11f + p) + 0.005f * sinf(t * 0.29f + p * 2.1f));
        float cy = H() * (s->y + 0.018f * k * sinf(t * 0.15f + p * 1.3f) + 0.08f * (1.0f - g_intro_shapes));   /* drift in from below */
        float ang = 0.14f * sinf(t * 0.09f + p) + 0.04f * sinf(t * 0.23f + p * 0.7f);
        float sc = 1.0f + 0.05f * sinf(t * 0.19f + p * 1.9f);
        float al = s->alpha * (0.82f + 0.18f * sinf(t * 0.35f + p * 2.7f)) * alpha * g_intro_shapes;
        float u0 = (float)s->kind * 0.25f, u1 = u0 + 0.25f, v0 = (float)s->angle * 0.5f;
        /* cells are 256x224: keep that shape */
        float w = sz * sc * (256.0f / 224.0f), h = sz * sc;
        d_image_rot(g_stage_shapes, cx, cy, w, h, ang, u0, u1, v0, v0 + 0.25f, sf_col_alpha(tint, al));            /* glass */
        d_image_rot(g_stage_shapes, cx, cy, w, h, ang, u0, u1, v0 + 0.25f, v0 + 0.5f, sf_col_alpha(0xFFFFFFFFu, al * 0.9f));  /* edges */
    }
}

static void draw_background_as(const App *a, float alpha, int animated, int bg);
static void draw_background(const App *a, float alpha, int animated)
{
    draw_background_as(a, alpha, animated, a->mode == MODE_LOADING ? BG_ANIMATED : a->settings.background);
}

static void draw_background_as(const App *a, float alpha, int animated, int bg)
{
    float t = animated ? (float)a->time : 0.0f;
    if (bg == BG_STAGE && (g_stage_tex >= 0 || g_src_base) && alpha >= 0.999f) { bg_stage(t, alpha); return; }   /* covers the screen */
    d_rect_grad(0, 0, W(), H(), sf_col_alpha(TH_BG_TOP, alpha), sf_col_alpha(TH_BG_BOTTOM, alpha));
    if (bg == BG_STAGE && (g_stage_tex >= 0 || g_src_base)) { bg_stage(t, alpha); return; }
    if (bg == BG_CUSTOM) {
        if (g_bg_tex >= 0) {           /* the picture fills the screen (fitted to 16:9 when loaded) */
            if (alpha >= 0.999f) r_set_blend(0);
            d_image(g_bg_tex, 0, 0, W(), H(), sf_col_alpha(0xFFFFFFFFu, alpha));
            if (alpha >= 0.999f) r_set_blend(1);
        } else {
            bg_drift(t, alpha);        /* not loaded: Drift */
        }
        return;
    }
    switch (bg) {
    case BG_AURORA:    bg_aurora(t, alpha); break;
    case BG_FIREFLIES: bg_fireflies(t, alpha); break;
    case BG_RIBBONS:   bg_ribbons(t, alpha); break;
    case BG_HALO:      bg_halo(t, alpha); break;
    case BG_TWILIGHT:  bg_twilight(t, alpha); break;
    default:           bg_drift(t, alpha); break;
    }
}

/* a background alone, full screen (tools/preview makes the Background
 * thumbnails from these: tools/make_thumbs.py) */
void ui_draw_background_only(App *a, int bg)
{
    ui_set_color(a->settings.ui_color);
    set_glow(a->time);
    layout_set_floor(bg == BG_STAGE);
    draw_background_as(a, 1.0f, 1, bg);
}

/* ------------------------------------------------------------------ */
/* Covers                                                              */

#define COVER_FADE_FRAMES 14.0f   /* ~0.23 s at 60 fps */

/* ---- Cases: with Custom Cases on and a case image loaded (ManaGunZ's or
 * your own), the cover sits in the image and the image is drawn over it.
 * Otherwise just the cover is shown. Either way a thin outline in the UI
 * color goes around it. ---- */

typedef struct { float x, y, w, h; } Rect;

static uint32_t shade(uint32_t c, float b, float al) { return sf_col_alpha(sf_col_dim(c, b), al); }

static void case_window(int plat, float x, float y, float w, float h, Rect *r)
{
    CaseWindow cw;
    layout_case_window(plat, &cw);
    r->x = x + w * cw.x0;
    r->w = w * (cw.x1 - cw.x0);
    r->y = y + h * cw.y0;
    r->h = h * (cw.y1 - cw.y0);
}

/* ---- reflections: a mirror image of the bottom of each case on the floor,
 * fading out with distance. The mirror line is the case's bottom edge. ---- */
#define REFLECT_DEPTH 0.25f   /* how far it reaches, fraction of case height */
#define REFLECT_ALPHA 0.32f   /* strength right under the case */
#define DISPLAY_H     0.70f   /* display mode (Square): the big case's height, fraction of the screen */
#define DISPLAY_CY    0.505f  /* its vertical center: between the tabs and the controls bar */
#define DISPLAY_DIM   0.22f   /* the other cases' opacity */

/* mirror the part of rect (y..y+h) that lies within `depth` above axis A
 * to below it, as texture rows v(y)=0..1 */
static void reflect_tex(int tex, float x, float y, float w, float h, float A, float depth, uint32_t tint, float k)
{
    float top = A, bot = A + depth, o0, o1, v0, v1, a0, a1;
    /* mirrored point m (below A) shows original row 2A - m */
    float m0 = 2.0f * A - (y + h), m1 = 2.0f * A - y;     /* mirrored rect */
    if (m0 < top) m0 = top;
    if (m1 > bot) m1 = bot;
    if (m1 <= m0 || h <= 0.0f) return;
    o0 = 2.0f * A - m0; o1 = 2.0f * A - m1;             /* original rows (o0 > o1) */
    v0 = (o0 - y) / h; v1 = (o1 - y) / h;
    a0 = k * (1.0f - (m0 - A) / depth); a1 = k * (1.0f - (m1 - A) / depth);
    d_image_v(tex, x, m0, w, m1 - m0, v0, v1, sf_col_alpha(tint, a0), sf_col_alpha(tint, a1));
}

static void reflect_rect(float x, float y, float w, float h, float A, float depth, uint32_t col, float k)
{
    float m0 = 2.0f * A - (y + h), m1 = 2.0f * A - y;
    if (m0 < A) m0 = A;
    if (m1 > A + depth) m1 = A + depth;
    if (m1 <= m0) return;
    d_rect_grad(x, m0, w, m1 - m0, sf_col_alpha(col, k * (1.0f - (m0 - A) / depth)), sf_col_alpha(col, k * (1.0f - (m1 - A) / depth)));
}

/* no_cover: the game has no cover (title card); otherwise an empty dark
 * front is shown until the cover (tex, faded in by art) arrives */
static void draw_cover(const App *a, int plat, const Game *g, const CoverPos *p, float alpha, int tex, float art, int no_cover,
                       float outline)
{
    float b = p->bright;
    float x = p->cx - p->w * 0.5f, y = p->cy - p->h * 0.5f, w = p->w, h = p->h;
    int cased = layout_case_image_active(plat) && g_case_tex[plat] >= 0;
    Rect win;
    if (alpha <= 0.003f) return;
    if (tex < 0) art = 0.0f;
    case_window(plat, x, y, w, h, &win);
    /* reflection first (it lies under everything) */
    if (a->settings.reflections) {
        float A = y + h, depth = h * REFLECT_DEPTH, k = REFLECT_ALPHA * alpha * b;
        uint32_t white = 0xFFFFFFFFu;
        if (art < 1.0f) reflect_rect(win.x, win.y, win.w, win.h, A, depth, 0x1E1F24FFu, k * (1.0f - art));
        if (art > 0.0f) reflect_tex(tex, win.x, win.y, win.w, win.h, A, depth, white, k * art);
        if (cased) reflect_tex(g_case_tex[plat], x, y, w, h, A, depth, white, k);
    }
    /* contact shadow: grounds the case on the floor */
    if (layout_floor()) d_blob_ellipse(p->cx, y + h, w * 0.62f, h * 0.045f, sf_col_alpha(0x000000FFu, 0.70f * alpha));
    if (art < 1.0f) {
        float pa = alpha * (1.0f - art);
        d_rect(win.x, win.y, win.w, win.h, shade(0x1E1F24FFu, b, pa));     /* empty front */
        if (no_cover) {
            const char *title = lib_title(a->lib, g);
            float hue = (float)(sf_hash(title) & 0xFFFF) / 65535.0f;
            d_rect(win.x, win.y, win.w, win.h, shade(sf_hsv(hue, 0.42f, 0.58f, 255), b, pa));
            d_rect(win.x, win.y, win.w, win.h * 0.085f, shade(TH_CASE_HEADER, b, pa * 0.85f));
            d_text_fit(&g_font_main, win.x + d_xu(win.h * 0.025f), win.y + win.h * 0.045f, win.h * 0.050f,
                       shade(0xFFFFFFD0u, b, pa), k_plat_case[plat], ALIGN_LEFT, win.w * 0.9f);
            d_text_box(&g_font_main, win.x + win.w * 0.5f, win.y + win.h * 0.40f, win.w * 0.84f, h * 0.068f, 4,
                       shade(0xFFFFFFF0u, b, pa), title);
        }
    }
    if (art > 0.0f) d_image(tex, win.x, win.y, win.w, win.h, shade(0xFFFFFFFFu, b, alpha * art));
    if (cased) d_image(g_case_tex[plat], x, y, w, h, shade(0xFFFFFFFFu, b, alpha));   /* plastic + gloss over the art */
    (void)outline;
}

/* Draw one tab's cover row. xoff = horizontal slide (logical), alpha = fade. */
static void draw_flow_row(App *a, int tab, float xoff, float alpha)
{
    const GameList *gl = &a->lib->tab[tab];
    float scroll = a->view[tab].scroll.x;
    int center = (int)floorf(scroll + 0.5f), k;
    const Screen *s = d_screen();
    /* display mode (Square): the selected case grows to DISPLAY_H of the
     * screen, centered; the others slide aside and dim */
    /* 0.8.0: Game Settings / Choose Disc show the big case on the left */
    float pg = (tab == a->tab && (a->panel == PANEL_GAME || a->panel == PANEL_DISC)) ? ease_in_out(a->panel_anim) : 0.0f;
    float de = tab == a->tab ? sf_maxf(ease_in_out(a->display_t), pg) : 0.0f;
    CoverPos big;
    layout_cover(s, tab, 0.0f, &big);
    {
        float sc = DISPLAY_H * H() / ((layout_floor() ? LY_SEL_H : LY_C_SEL_H) * H());
        big.h *= sc; big.w *= sc;
        big.cy = DISPLAY_CY * H();
        big.cx = sf_lerp(big.cx, W() * 0.265f, pg) + xoff;
        big.bright = 1.0f;
    }
    if (gl->count <= 0) {
        d_text(&g_font_main, W() * 0.5f + xoff, H() * layout_flow_cy(), H() * 0.03f, sf_col_alpha(TH_TEXT_DIM, alpha), "No games", ALIGN_CENTER);
        return;
    }
    /* a soft line in the UI color under the selected case: brightest in the
     * middle and fading out at both ends, glowing with the outlines */
    {
        CoverPos c0;
        float frac = sf_absf(scroll - (float)center);
        float la = alpha * (1.0f - sf_minf(1.0f, frac * 2.0f) * 0.6f) * (1.0f - de);
        layout_cover(s, tab, 0.0f, &c0);
        if (la > 0.0f) {
            uint32_t col = accent_or(0xFFFFFFFFu, 1.0f) | 0xFFu, clear = col & 0xFFFFFF00u;
            float lw = c0.w * 1.15f, cx = c0.cx + xoff, ly = c0.cy + c0.h * 0.5f + H() * 0.022f;
            float th = H() * 0.0040f, gh = H() * 0.016f;
            uint32_t core = sf_col_alpha(glow(col), la), haze = sf_col_alpha(glow(col), la * 0.22f);
            /* haze */
            d_rect_hgrad(cx - lw * 0.5f, ly - gh * 0.5f, lw * 0.5f, gh, clear, haze);
            d_rect_hgrad(cx, ly - gh * 0.5f, lw * 0.5f, gh, haze, clear);
            /* core line */
            d_rect_hgrad(cx - lw * 0.5f, ly - th * 0.5f, lw * 0.5f, th, clear, core);
            d_rect_hgrad(cx, ly - th * 0.5f, lw * 0.5f, th, core, clear);
        }
    }
    for (k = LY_MAX_DRAW; k >= 0; k--) {
        int side;
        for (side = 0; side < (k == 0 ? 1 : 2); side++) {
            int idx = side == 0 ? center - k : center + k;
            CoverPos p;
            float ra;
            if (idx < 0 || idx >= gl->count) continue;
            layout_cover(s, tab, (float)idx - scroll, &p);
            p.cx += xoff;
            if (de > 0.0f) {
                float dd = (float)idx - scroll, ad = sf_minf(1.0f, sf_absf(dd)), f = de * (1.0f - ad);
                float w0 = p.w;
                p.h = sf_lerp(p.h, big.h, f);
                p.w = sf_lerp(p.w, big.w, f);
                p.cy = sf_lerp(p.cy, big.cy, f);
                p.bright = sf_lerp(p.bright, 1.0f, f);
                p.cx = sf_lerp(p.cx, p.cx + (big.cx - xoff - W() * 0.5f), f);       /* follows the big case */
                /* neighbours make room for the big case (and go when the settings are beside it) */
                p.cx += (dd < 0.0f ? -1.0f : 1.0f) * ad * de * ((big.w - w0) * 0.5f + big.w * 0.04f);
                ra = sf_lerp(1.0f, DISPLAY_DIM, de * ad) * (1.0f - pg * ad);
            } else {
                ra = 1.0f;
            }
            if (p.cx + p.w * 0.5f < 0.0f || p.cx - p.w * 0.5f > W()) continue;
            {
                int slot = cc_lookup(&a->covers, tab, idx), tex = -1, no_cover = 1;
                float art = 0.0f;
                if (slot >= 0) {
                    const CcSlot *cs = &a->covers.slot[slot];
                    if (cs->state == CC_READY && cs->tex >= 0) {
                        tex = cs->tex;
                        art = sf_clampf((float)cc_ready_age(&a->covers, slot) / COVER_FADE_FRAMES, 0.0f, 1.0f);
                    }
                    /* while the cover is on its way, show an empty case, not a title card */
                    if (cs->state == CC_LOADING || cs->tex >= 0) no_cover = 0;
                } else if (a->host.covers && !(gl->games[idx].flags & GAME_DEMO)) {
                    no_cover = 0;     /* not queued yet: also on its way */
                }
                draw_cover(a, tab, &gl->games[idx], &p, alpha * ra, tex, art, no_cover,
                           sf_clampf(1.0f - sf_absf((float)idx - scroll) * 2.0f, 0.0f, 1.0f));
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
    /* active tab: an outline in the UI color (the pill itself stays dark) */
    d_rrect_stroke(hx - hw * 0.5f, y - m.tab_h * 0.5f, hw, m.tab_h, m.tab_h * 0.5f, 0.0040f * H(),
                   sf_col_alpha(glow(accent_or(TH_TAB_ACTIVE, 1.0f)), alpha));
    for (i = 0; i < PLAT_COUNT; i++) {
        float k = 1.0f - sf_minf(1.0f, sf_absf(hl - (float)i));
        uint32_t tc = sf_col_mix(TH_TEXT_DIM, TH_TEXT, k);
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

/* 0.8.0: the title and info line move with the cover flow (Flow Settings drawer) */
static float g_info_dx;
#define INFO_CX (W() * 0.5f + g_info_dx)

static int chip_texts(const App *a, int tab, int idx, char out[4][32])
{
    const GameList *gl = &a->lib->tab[tab];
    const Game *g;
    int n = 0;
    if (idx < 0 || idx >= gl->count) return 0;
    g = &gl->games[idx];
    snprintf(out[n++], 32, "%d of %d", idx + 1, gl->count);      /* position in this tab */
    if (g->year > 0) snprintf(out[n++], 32, "%d", g->year);          /* release year (was genre) */
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
    x = INFO_CX - total * 0.5f;
    for (i = 0; i < n; i++) {
        d_rrect(x, m->chips_y - m->chip_h * 0.5f, w[i], m->chip_h, m->chip_h * 0.5f, sf_col_alpha(TH_CHIP, alpha));
        d_rrect_stroke(x, m->chips_y - m->chip_h * 0.5f, w[i], m->chip_h, m->chip_h * 0.5f, 0.0028f * H(),
                       sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), alpha));
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
    if (trunc) d_text_fit(&g_font_main, INFO_CX, m->title_y, em, sf_col_alpha(TH_TEXT, alpha), t, ALIGN_CENTER, maxw);
    else d_text(&g_font_main, INFO_CX, m->title_y, em, sf_col_alpha(TH_TEXT, alpha), t, ALIGN_CENTER);
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

/* ---- title / info styles (preview of options) ---- */
static int g_info_style = 2;   /* 0 pills, 1 minimal, 2 minimal + accent line (the design), 3 card */
void ui_set_info_style(int s) { g_info_style = s; }

#define MIDDOT "  \xC2\xB7  "

/* "Racing · 1-2 players · 7.6 GB · 25 of 73" pieces (count last) */
static int info_pieces(const App *a, int tab, int idx, char out[4][32])
{
    char t[4][32];
    int n = chip_texts(a, tab, idx, t), i;
    if (n <= 0) return 0;
    for (i = 1; i < n; i++) sf_strlcpy(out[i - 1], t[i], 32);
    sf_strlcpy(out[n - 1], t[0], 32);
    return n;
}

/* one centered line; separators (and the count) in the UI color when accent_seps */
static float info_line(const App *a, int tab, int idx, float y, float em, float alpha, int accent_seps, int dry)
{
    char p[4][32];
    int n = info_pieces(a, tab, idx, p), i;
    float sw = d_text_width(&g_font_main, em, MIDDOT), w = 0.0f, x;
    uint32_t sep = accent_seps ? glow(accent_or(0xFFFFFFFFu, 1.0f) | 0xFFu) : TH_TEXT_FAINT;
    if (n == 0) return 0.0f;
    for (i = 0; i < n; i++) w += d_text_width(&g_font_main, em, p[i]) + (i ? sw : 0.0f);
    if (dry || alpha <= 0.0f) return w;
    x = INFO_CX - w * 0.5f;
    for (i = 0; i < n; i++) {
        uint32_t c = (i == n - 1) ? TH_TEXT_FAINT : TH_TEXT_DIM;
        if (i) { d_text(&g_font_main, x, y, em, sf_col_alpha(sep, alpha), MIDDOT, ALIGN_LEFT); x += sw; }
        d_text(&g_font_main, x, y, em, sf_col_alpha(c, alpha), p[i], ALIGN_LEFT);
        x += d_text_width(&g_font_main, em, p[i]);
    }
    return w;
}

static void shadow_text(const Font *f, float x, float y, float em, uint32_t col, const char *t, float maxw, float alpha)
{
    float o = em * 0.06f;
    d_text_fit(f, x + d_xu(o), y + o, em, sf_col_alpha(0x00000090u, alpha), t, ALIGN_CENTER, maxw);
    d_text_fit(f, x, y, em, sf_col_alpha(col, alpha), t, ALIGN_CENTER, maxw);
}

/* the A-Z rail takes the info line's place while it shows (0.7.1) */
static float g_info_fade = 1.0f;
static float rail_alpha(const App *a)
{
    float t = a->letter_t, al;
    if (t >= LETTER_TIME || !a->letter[0]) return 0.0f;
    al = t < 0.45f ? 1.0f : sf_maxf(0.0f, 1.0f - (t - 0.45f) / (LETTER_TIME - 0.45f));
    return al * sf_minf(1.0f, a->letter_in / 0.12f);   /* fades in once, not on every jump */
}

static void draw_info_styled(App *a, float alpha, const UiMetrics *m, float ty, float cy)
{
    float t = ease_in_out(a->info_t), maxw = W() * 0.80f;
    int k;
    for (k = 0; k < 2; k++) {
        int tab = k ? a->info_tab : a->prev_tab, idx = k ? a->info_idx : a->prev_idx;
        float al = alpha * (k ? t : 1.0f - t);
        const char *title = info_title(a, tab, idx);
        if (al <= 0.003f || !title[0]) continue;
        if (g_info_style == 1 || g_info_style == 2) {
            float em = m->title_em * 1.25f;
            shadow_text(&g_font_main, INFO_CX, ty, em, TH_TEXT, title, maxw, al);
            if (g_info_style == 2) {   /* tapered accent line under the title */
                float tw = sf_minf(d_text_width(&g_font_main, em, title), maxw) + d_xu(H() * 0.06f);
                float ly = ty + em * 0.75f, th = H() * 0.0030f;
                uint32_t col = glow(accent_or(0xFFFFFFFFu, 1.0f) | 0xFFu), clear = col & 0xFFFFFF00u;
                d_rect_hgrad(INFO_CX - tw * 0.5f, ly, tw * 0.5f, th, clear, sf_col_alpha(col, al));
                d_rect_hgrad(INFO_CX, ly, tw * 0.5f, th, sf_col_alpha(col, al), clear);
            }
            if (a->settings.show_info) info_line(a, tab, idx, cy, m->chip_em * 1.10f, al * g_info_fade, g_info_style == 2, 0);
        } else if (g_info_style == 3) {
            /* one frosted card under the case: title + info line */
            float tem = m->title_em * 1.05f, iem = m->chip_em * 1.05f;
            float iw = a->settings.show_info ? info_line(a, tab, idx, 0, iem, 0, 1, 1) : 0.0f;
            float tw = sf_minf(d_text_width(&g_font_main, tem, title), maxw);
            float cw = sf_maxf(tw, iw) + d_xu(H() * 0.09f), ch = a->settings.show_info ? H() * 0.105f : H() * 0.068f;
            float cx = INFO_CX - cw * 0.5f, ctop = cy - ch * 0.5f;
            if (k == 1 || t < 1.0f) {
                d_rrect(cx, ctop, cw, ch, H() * 0.022f, sf_col_alpha(TH_CHIP, al * 0.92f));
                d_rrect_stroke(cx, ctop, cw, ch, H() * 0.022f, 0.0028f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), al));
            }
            if (a->settings.show_info) {
                d_text_fit(&g_font_main, INFO_CX, ctop + ch * 0.36f, tem, sf_col_alpha(TH_TEXT, al), title, ALIGN_CENTER, maxw);
                info_line(a, tab, idx, ctop + ch * 0.72f, iem, al, 1, 0);
            } else {
                d_text_fit(&g_font_main, INFO_CX, cy, tem, sf_col_alpha(TH_TEXT, al), title, ALIGN_CENTER, maxw);
            }
        }
    }
}

static void draw_info(App *a, float alpha, const UiMetrics *m0)
{
    UiMetrics mm = *m0;
    const UiMetrics *m = &mm;
    /* centered between tabs/case and case/bar for the current tab's case
     * size (PS1 cases are shorter); glides during a tab switch */
    {
        float ty, cy;
        layout_info_y(d_screen(), m0, a->tab, &ty, &cy);
        if (a->tab_anim < 1.0f && a->tab_prev != a->tab) {
            float ty0, cy0, e = ease_in_out(a->tab_anim);
            layout_info_y(d_screen(), m0, a->tab_prev, &ty0, &cy0);
            ty = sf_lerp(ty0, ty, e);
            cy = sf_lerp(cy0, cy, e);
        }
        mm.title_y = ty;
        mm.chips_y = cy;
    }
    g_info_fade = 1.0f - ease_in_out(rail_alpha(a));
    if (g_info_style != 0) { ui_update_targets(a); draw_info_styled(a, alpha, m, mm.title_y, mm.chips_y); return; }
    {
    const char *cur = info_title(a, a->info_tab, a->info_idx);
    float pad = d_xu(m->title_pad);
    float maxw = W() * m->title_max_w - 2.0f * pad;
    float pw, t = ease_in_out(a->info_t);
    ui_update_targets(a);
    pw = sf_clampf(a->title_w.x, d_xu(m->title_h), W() * m->title_max_w);
    if (cur[0]) {
        d_rrect(INFO_CX - pw * 0.5f, m->title_y - m->title_h * 0.5f, pw, m->title_h, m->title_h * 0.5f, sf_col_alpha(TH_CHIP, alpha));
        d_rrect_stroke(INFO_CX - pw * 0.5f, m->title_y - m->title_h * 0.5f, pw, m->title_h, m->title_h * 0.5f,
                       0.0028f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), alpha));
    }
    /* clip text to the (animating) pill so a wide old title never spills out */
    maxw = sf_minf(maxw, pw - 2.0f * pad * 0.6f);
    if (t < 1.0f) {
        draw_title_text(info_title(a, a->prev_tab, a->prev_idx), alpha * (1.0f - t), m, maxw);
        if (a->settings.show_info) draw_chips(a, a->prev_tab, a->prev_idx, alpha * (1.0f - t) * g_info_fade, m);
    }
    draw_title_text(cur, alpha * t, m, maxw);
    if (a->settings.show_info) draw_chips(a, a->info_tab, a->info_idx, alpha * t * g_info_fade, m);
    }
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
        group_add_badge(&L->g[1], ICON_SQUARE, L->icon);   L->g[1].label = "Display";
        group_add_badge(&L->g[2], ICON_TRIANGLE, L->icon); L->g[2].label = "Game Settings";
        group_add_badge(&L->g[3], ICON_L2, L->icon);
        group_add_badge(&L->g[3], ICON_R2, L->icon);
        L->g[3].label = "A\xE2\x80\x93Z";
        group_add_badge(&L->g[4], ICON_SELECT, L->icon);   L->g[4].label = "Next Song";
        group_add_badge(&L->g[5], ICON_START, L->icon);    L->g[5].label = "Flow Settings";
        group_add_badge(&L->g[6], ICON_CIRCLE, L->icon);   L->g[6].label = "Exit (Hold)";
        L->n = 7;
    } else if (panel_mode == 1) {        /* 0.8.2: a value row: <- / -> change it */
        group_add_badge(&L->g[0], ICON_DPAD_LR, L->icon);  L->g[0].label = "Change";
        group_add_badge(&L->g[1], ICON_CIRCLE, L->icon);   L->g[1].label = "Back";
        L->n = 2;
    } else if (panel_mode == 5) {        /* the About card */
        group_add_badge(&L->g[0], ICON_CIRCLE, L->icon);   L->g[0].label = "Back";
        L->n = 1;
    } else if (panel_mode == 3) {
        group_add_badge(&L->g[0], ICON_CROSS, L->icon);    L->g[0].label = "Launch";
        group_add_badge(&L->g[1], ICON_TRIANGLE, L->icon); L->g[1].label = "Game Settings";
        group_add_badge(&L->g[2], ICON_CIRCLE, L->icon);   L->g[2].label = "Back";
        L->n = 3;
    } else {
        group_add_badge(&L->g[0], ICON_CROSS, L->icon);
        L->g[0].label = panel_mode == 4 ? "Select" : panel_mode >= 2 ? "Launch" : "Change";
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
    BarLayout main_l, panel_l, sel_l, disp_l, about_l;
    float pe = ease_out_cubic(a->panel_anim), de = ease_in_out(a->display_t), ab = ease_in_out(a->about_t);
    static float g_bar_sel = 0.0f;      /* 0: "Change" (a value row), 1: "Select" (a group or an action) */
    static double g_bar_time;
    int want_sel = a->panel != PANEL_NONE && a->row >= 0 && a->row < a->nrows && menu_is_action(a->rows[a->row]);
    float dt = (float)(a->time - g_bar_time);
    int jump = dt < 0.0f || dt > 0.2f;         /* not drawn for a while: no animation to catch up on */
    g_bar_time = a->time;
    if (a->panel_anim < 0.05f || jump) g_bar_sel = (float)want_sel;
    else anim_approach(&g_bar_sel, (float)want_sel, 1.0f / 0.15f, dt);
    ui_bar_layout(0, &main_l);
    ui_bar_layout(1, &panel_l);
    ui_bar_layout(a->panel == PANEL_DISC ? 2 : 4, &sel_l);
    ui_bar_layout(3, &disp_l);
    ui_bar_layout(5, &about_l);
    d_rrect(main_l.bar_x, main_l.bar_y, main_l.bar_w, main_l.bar_h, main_l.bar_h * 0.5f, sf_col_alpha(TH_BAR, alpha));
    d_rrect_stroke(main_l.bar_x, main_l.bar_y, main_l.bar_w, main_l.bar_h, main_l.bar_h * 0.5f, 0.0028f * H(),
                   sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), alpha));
    {
        /* display mode: the main controls fade out, then Launch and Back fade in */
        float mo = 1.0f - sf_clampf(de * 2.0f, 0.0f, 1.0f), di = sf_clampf(de * 2.0f - 1.0f, 0.0f, 1.0f);
        if (pe < 1.0f && mo > 0.0f) draw_bar_groups(a, &main_l, alpha * (1.0f - pe) * mo, 1);
        if (pe < 1.0f && di > 0.0f) draw_bar_groups(a, &disp_l, alpha * (1.0f - pe) * di, 0);
    }
    if (pe > 0.0f) {
        /* the panel's controls follow the selected row (crossfade); the About card has only Back */
        float pa = alpha * pe * (1.0f - ab);
        if (g_bar_sel < 1.0f && pa > 0.0f) draw_bar_groups(a, &panel_l, pa * (1.0f - g_bar_sel), 0);
        if (g_bar_sel > 0.0f && pa > 0.0f) draw_bar_groups(a, &sel_l, pa * g_bar_sel, 0);
        if (ab > 0.0f) draw_bar_groups(a, &about_l, alpha * pe * ab, 0);
    }
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

/* room one of draw_value's ‹ › takes */

/* ---- Settings panels (0.8.0) ----------------------------------------
 * Flow Settings is a drawer on the right (the cover flow slides left and
 * dims); Game Settings and Choose Disc sit in a column beside the big case
 * (the display-mode case, moved to the left). Values are shown as what
 * they are: switches, Default / On / Off choices, colour dots, background
 * thumbnails, sliders. Rows cascade in when a page appears. */
typedef struct {
    float x, w;            /* the rows' left edge and width (x units) */
    float card_x, card_y, card_w, card_h;   /* the drawer card */
    float title_y, line_y, list_y, bottom;
    float rh, rg, em, alpha, slide;   /* slide: x offset of the whole panel while it opens */
    int vis, drawer;
} PanelGeo;

#define PANEL_BG_ROW_K 1.80f     /* the Background row is this much taller (thumbnails) */

static void panel_geo(const App *a, PanelGeo *g)
{
    float e = ease_out_cubic(a->panel_anim);
    memset(g, 0, sizeof(*g));
    g->vis = app_panel_visible_rows(a);
    g->alpha = clamp01(a->panel_anim * 1.6f);
    if (a->panel == PANEL_FLOW) {
        /* 0.8.1: a whole card that slides in, its glowing outline all round */
        float dw = sf_minf(d_xu(0.645f * H()), W() * 0.56f), pad = d_xu(0.040f * H());   /* 0.36 W at 16:9, wider share at 4:3 */
        float mx = d_xu(0.032f * H());
        float x0 = W() - mx - dw + (1.0f - e) * (dw + 2.0f * mx);
        g->drawer = 1;
        g->card_x = x0; g->card_w = dw; g->card_y = 0.032f * H(); g->card_h = 0.866f * H() - g->card_y;
        g->x = x0 + pad; g->w = dw - 2.0f * pad;
        g->title_y = g->card_y + 0.070f * H();
        g->line_y = g->title_y + 0.040f * H();
        g->list_y = g->line_y + 0.034f * H();
        g->bottom = g->card_y + g->card_h - 0.02f * H();
        g->rh = 0.072f * H(); g->rg = 0.014f * H(); g->em = 0.0275f * H();
    } else {
        g->x = W() * 0.535f; g->w = W() * 0.435f;
        g->slide = (1.0f - e) * W() * 0.06f;
        g->x += g->slide;
        g->title_y = 0.250f * H(); g->line_y = 0.292f * H();
        g->list_y = 0.330f * H();
        g->bottom = 0.880f * H();
        g->rh = 0.078f * H(); g->rg = 0.019f * H(); g->em = 0.029f * H();
    }
}

static float panel_row_h(const App *a, const PanelGeo *g, int i)
{
    return (i >= 0 && i < a->nrows && a->rows[i] == ROW_F_BACKGROUND) ? g->rh * PANEL_BG_ROW_K : g->rh;
}

/* top of row position fi (fractional: the highlight glides between rows) */
static float panel_row_y(const App *a, const PanelGeo *g, float fi)
{
    int i, n = (int)floorf(fi);
    float y = 0.0f, f = fi - (float)n;
    for (i = 0; i < n; i++) y += panel_row_h(a, g, i) + g->rg;
    if (n >= 0) y += f * (panel_row_h(a, g, n) + g->rg);
    return y;
}

/* per-row animated values (switch knobs, the chosen choice, the ring) */
static float g_row_anim[MENU_MAX_ROWS];
static int g_row_anim_page = -1;
static double g_row_anim_time;

static float approach_now(float *v, float target, float rate, float dt, int snap)
{
    if (snap) *v = target;
    else *v += (target - *v) * sf_minf(1.0f, dt * rate);
    return *v;
}

static void draw_switch(float xr, float cy, float on_t, int sel, float al)
{
    float h = 0.034f * H(), w = d_xu(0.064f * H()), x = xr - w;
    uint32_t off = 0x464A58FFu, onc = accent_white() ? 0xD8DCE6FFu : (accent() | 0xFFu);
    uint32_t track = sf_col_mix(off, onc, on_t);
    float kx = x + d_xu(h * 0.5f) + (w - d_xu(h)) * on_t;
    d_rrect(x, cy - h * 0.5f, w, h, h * 0.5f, sf_col_alpha(track, al));
    if (sel) d_rrect_stroke(x, cy - h * 0.5f, w, h, h * 0.5f, 0.0022f * H(), sf_col_alpha(0xFFFFFF55u, al));
    d_disc(kx, cy, h * 0.38f, sf_col_alpha(0xFAFAFCFFu, al));
}

static void draw_segments(float xr, float cy, float em, const char *const *names, int n, float pos, int sel, float al)
{
    float ws[4], total = 0.0f, h = 0.040f * H(), x, pad = d_xu(em * 0.85f);
    int i;
    float hx, hw;
    for (i = 0; i < n && i < 4; i++) { ws[i] = d_text_width(&g_font_main, em * 0.82f, names[i]) + 2.0f * pad; total += ws[i]; }
    x = xr - total;
    d_rrect(x, cy - h * 0.5f, total, h, h * 0.5f, sf_col_alpha(0x0C0E14C0u, al));
    /* the chosen one: a pill that glides between the choices */
    {
        int i0 = sf_clampi((int)floorf(pos), 0, n - 1), i1 = i0 + 1 < n ? i0 + 1 : i0;
        float f = clamp01(pos - (float)i0), x0 = x;
        for (i = 0; i < i0; i++) x0 += ws[i];
        hx = x0 + f * ws[i0];
        hw = sf_lerp(ws[i0], ws[i1], f);
    }
    d_rrect(hx, cy - h * 0.5f, hw, h, h * 0.5f,
            sf_col_alpha(sel ? (accent_white() ? 0xD8DCE6FFu : (accent() | 0xFFu)) : 0x505668FFu, al));
    for (i = 0; i < n && i < 4; i++) {
        float d = sf_absf(pos - (float)i), on = 1.0f - sf_minf(1.0f, d);
        uint32_t c = sf_col_mix(TH_TEXT_DIM, (sel && accent_white()) ? 0x101218FFu : TH_TEXT, on);
        d_text(&g_font_main, x + ws[i] * 0.5f, cy, em * 0.82f, sf_col_alpha(c, al), names[i], ALIGN_CENTER);
        x += ws[i];
    }
}

static void draw_color_dots(float xr, float cy, float ring, float al)
{
    float r = 0.0125f * H(), step = d_xu(0.040f * H()), x0 = xr - step * (float)(UI_COLOR_COUNT - 1) - d_xu(r);
    int i;
    for (i = 0; i < UI_COLOR_COUNT; i++) d_disc(x0 + step * (float)i, cy, r, sf_col_alpha(k_accent[i] | 0xFFu, al));
    d_arc(x0 + step * ring, cy, r * 1.62f, 0.0028f * H(), 0.0f, 2.0f * PI_F, sf_col_alpha(0xFFFFFFE6u, al));
}

/* the background choices in the order <-/-> go through them: Stage first,
 * the user's pictures (one thumbnail) last */
static int bg_choices(const App *a, int *out)
{
    static const int order[] = { BG_STAGE, BG_ANIMATED, BG_AURORA, BG_FIREFLIES, BG_RIBBONS, BG_HALO, BG_TWILIGHT };
    int n = 0, i;
    for (i = 0; i < (int)(sizeof(order) / sizeof(order[0])); i++) out[n++] = order[i];
    if (a->nbg_files > 0) out[n++] = BG_CUSTOM;
    return n;
}

static void draw_bg_thumbs(const App *a, float x, float y, float w, float ring, float al)
{
    int ch[BG_COUNT], n = bg_choices(a, ch), i;
    float gap = d_xu(0.010f * H()), tw = (w - gap * (float)(n - 1)) / (float)n;
    float th = sf_minf(d_xu(1.0f) > 0.0f ? (tw / d_xu(1.0f)) * 9.0f / 16.0f : 0.0f, 0.060f * H());
    uint32_t tint = sf_col_mix(bg_tint(), 0xFFFFFFFFu, 0.40f);
    tw = d_xu(th * 16.0f / 9.0f);
    x += (w - (tw * (float)n + gap * (float)(n - 1))) * 0.5f;
    for (i = 0; i < n; i++) {
        float tx = x + (tw + gap) * (float)i;
        int b = ch[i];
        if (b == BG_CUSTOM) {
            if (g_bg_tex >= 0) d_image(g_bg_tex, tx, y, tw, th, sf_col_alpha(0xFFFFFFFFu, al));
            else {
                d_rrect(tx, y, tw, th, th * 0.14f, sf_col_alpha(0x2A2E3AFFu, al));
                d_disc(tx + tw * 0.68f, y + th * 0.34f, th * 0.12f, sf_col_alpha(0xC8CCD8FFu, al));
                d_rect(tx + tw * 0.12f, y + th * 0.66f, tw * 0.76f, th * 0.06f, sf_col_alpha(0x8A90A0FFu, al));
            }
        } else {
            int icon = ICON_THUMB0 + (b == BG_ANIMATED ? 0 : b - 1);
            float cx = tx + tw * 0.5f, cy = y + th * 0.5f;
            /* the stage thumb carries its own colours only when baked: tint all alike */
            if (icon >= ICON_THUMB0 && icon < ICON_COUNT) d_badge(icon, cx, cy, th, sf_col_alpha(tint, al));
        }
    }
    /* the chosen one: a ring that glides */
    {
        float rx = x + (tw + gap) * ring - d_xu(0.004f * H());
        d_rrect_stroke(rx, y - 0.004f * H(), tw + d_xu(0.008f * H()), th + 0.008f * H(), th * 0.20f, 0.0030f * H(),
                       sf_col_alpha(0xFFFFFFF0u, al));
    }
}

/* what a row's animation heads for (switch 0/1, the chosen choice, the
 * colour, the background, the slider); 0 = the row has none */
static int row_anim_target(const App *a, RowKind k, float *t)
{
    const char *const *names = NULL;
    int cur = 0;
    if (k == ROW_F_BACKGROUND) {
        int ch[BG_COUNT], n = bg_choices(a, ch), j;
        for (j = 0; j < n; j++) if (ch[j] == a->settings.background) cur = j;
        *t = (float)cur;
        return 1;
    }
    if (k == ROW_F_UI_COLOR) { *t = (float)a->settings.ui_color; return 1; }
    if (menu_toggle(k, &a->settings) >= 0) { *t = (float)menu_toggle(k, &a->settings); return 1; }
    if (menu_is_slider(k)) { *t = menu_slider_value(k, &a->settings); return 1; }
    if ((a->panel == PANEL_GAME) && menu_segments(k, a->panel_plat, &a->panel_cfg, &names, &cur) > 0) { *t = (float)cur; return 1; }
    return 0;
}

/* how wide a row's value is drawn (so its name can take the rest) */
static float row_value_w(const App *a, RowKind k, const char *val, float em, int sel)
{
    const char *const *names = NULL;
    int cur = 0, n, i;
    float w = 0.0f, arrows = 2.0f * (d_text_width(&g_font_main, em, "\xE2\x80\xBA") + d_xu(em * 0.45f));
    if (menu_is_group(k)) return d_text_width(&g_font_main, em * 1.25f, "\xE2\x80\xBA");
    if (k == ROW_F_UI_COLOR) return d_xu(0.040f * H()) * (float)(UI_COLOR_COUNT - 1) + d_xu(0.0125f * H() * 3.3f);
    if (menu_toggle(k, &a->settings) >= 0) return d_xu(0.064f * H());
    if (menu_is_slider(k)) return d_xu(0.15f * H()) + d_xu(em * 0.6f) + d_text_width(&g_font_main, em, val);
    if (a->panel == PANEL_GAME && (n = menu_segments(k, a->panel_plat, &a->panel_cfg, &names, &cur)) > 0) {
        for (i = 0; i < n; i++) w += d_text_width(&g_font_main, em * 0.82f, names[i]) + 2.0f * d_xu(em * 0.85f);
        return w;
    }
    if (!val[0]) return 0.0f;
    (void)sel;      /* the arrows' room is kept when not selected too: nothing moves when the highlight comes */
    return d_text_width(&g_font_main, em, val) + (!menu_is_action(k) ? arrows : 0.0f);
}

static void draw_panel(App *a)
{
    PanelGeo g;
    const Game *game = NULL;
    int per_game = a->panel == PANEL_GAME || a->panel == PANEL_DISC, i;
    float dt, scroll_y, clip_top, clip_bot;
    uint32_t hot = accent_white() ? 0xFFFFFFFFu : (accent() | 0xFFu);
    char gtitle[64];
    const char *ptitle = a->panel == PANEL_GAME ? "Game Settings" : a->panel == PANEL_DISC ? "Choose Disc" : "Flow Settings";
    int page_key = a->panel * 64 + a->flow_group + 1, snap;
    if (a->panel_anim <= 0.0f) return;
    if (per_game) {
        if (a->panel_game < 0 || a->panel_game >= a->lib->tab[a->panel_plat].count) return;
        game = &a->lib->tab[a->panel_plat].games[a->panel_game];
    }
    if (a->panel == PANEL_FLOW && a->flow_group >= 0) {
        sf_strlcpy(gtitle, menu_label((RowKind)a->flow_group), sizeof(gtitle));
        ptitle = gtitle;
    }
    panel_geo(a, &g);
    /* per-row animations: snap on a new page */
    dt = (float)(a->time - g_row_anim_time);
    g_row_anim_time = a->time;
    snap = page_key != g_row_anim_page || a->page_t < 0.02f || dt < 0.0f || dt > 0.2f;
    g_row_anim_page = page_key;

    if (g.drawer) {
        /* the drawer: a whole card with a soft shadow and the breathing outline all round */
        float r = 0.040f * H();
        d_rrect_glow(g.card_x, g.card_y, g.card_w, g.card_h, r, 0.040f * H(), sf_col_alpha(0x000000FFu, 0.55f * g.alpha));
        d_rrect(g.card_x, g.card_y, g.card_w, g.card_h, r, sf_col_alpha(TH_PANEL | 0xFFu, 0.985f * g.alpha));
        d_rrect_glow(g.card_x, g.card_y, g.card_w, g.card_h, r, 0.014f * H(),
                     sf_col_alpha(hot, 0.22f * g.alpha * (0.6f + 0.4f * g_glow)));
        d_rrect_stroke(g.card_x, g.card_y, g.card_w, g.card_h, r, 0.0035f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF99u, 0.95f)), g.alpha));
    } else {
        /* beside the big case: darken the right side a little so the rows read */
        d_rect_hgrad(W() * 0.42f, 0.0f, W() * 0.58f, H(), 0x00000000u, sf_col_alpha(0x000000FFu, 0.50f * g.alpha));
    }

    /* title (cascades in first) */
    {
        float rt = clamp01((a->page_t - 0.02f) / 0.24f), re = ease_out_cubic(rt);
        float ox = (1.0f - re) * (float)a->page_dir * d_xu(0.05f * H()), al = g.alpha * re;
        float tem = g.drawer ? 0.040f * H() : 0.045f * H();
        d_text_fit(&g_font_main, g.x + ox + g.w * 0.5f, g.title_y, tem, sf_col_alpha(TH_TEXT, al), ptitle, ALIGN_CENTER, g.w);   /* 0.8.2: centered */
        {   /* 0.8.1: a full glowing line under the title, bright in the middle, fading at both ends
             * (like the line under the selected case) */
            uint32_t col = glow(hot), clear = col & 0xFFFFFF00u;
            float lx = g.x + ox, lw = g.w, th = 0.0036f * H(), gh = 0.016f * H();
            uint32_t core = sf_col_alpha(col, al), haze = sf_col_alpha(col, al * 0.24f);
            d_rect_hgrad(lx, g.line_y - gh * 0.5f, lw * 0.5f, gh, clear, haze);
            d_rect_hgrad(lx + lw * 0.5f, g.line_y - gh * 0.5f, lw * 0.5f, gh, haze, clear);
            d_rect_hgrad(lx, g.line_y - th * 0.5f, lw * 0.5f, th, clear, core);
            d_rect_hgrad(lx + lw * 0.5f, g.line_y - th * 0.5f, lw * 0.5f, th, core, clear);
        }
    }

    scroll_y = panel_row_y(a, &g, a->list_scroll.x);
    clip_top = g.list_y - g.rh * 0.6f;
    clip_bot = g.bottom - 0.01f * H();
    /* the gliding highlight, glowing in the UI colour */
    {
        float hy = g.list_y + panel_row_y(a, &g, a->row_hl.x) - scroll_y;
        int r0 = (int)floorf(a->row_hl.x + 0.5f);
        float hh = panel_row_h(a, &g, sf_clampi(r0, 0, a->nrows - 1));
        float rt = clamp01((a->page_t - 0.04f - (float)(r0 + 1) * 0.035f) / 0.24f), re = ease_out_cubic(rt);
        float ox = (1.0f - re) * (float)a->page_dir * d_xu(0.06f * H()), al = g.alpha * re;
        float grow = d_xu(0.004f * H());
        if (hy + hh > clip_top && hy < clip_bot && al > 0.0f) {
            d_rrect_glow(g.x + ox - grow, hy - 0.004f * H(), g.w + 2.0f * grow, hh + 0.008f * H(), g.rh * 0.5f, 0.022f * H(),
                         sf_col_alpha(hot, 0.30f * al * (0.75f + 0.25f * g_glow)));
            d_rrect(g.x + ox - grow, hy - 0.004f * H(), g.w + 2.0f * grow, hh + 0.008f * H(), g.rh * 0.5f,
                    sf_col_alpha(accent_white() ? 0x5A5E6CFFu : sf_col_mix(accent() | 0xFFu, 0x101420FFu, 0.42f), al));
            /* 0.8.2: the glowing outline the selected console tab has */
            d_rrect_stroke(g.x + ox - grow, hy - 0.004f * H(), g.w + 2.0f * grow, hh + 0.008f * H(), g.rh * 0.5f, 0.0040f * H(),
                           sf_col_alpha(glow(accent_or(TH_TAB_ACTIVE, 1.0f)), al));
        }
    }
    for (i = 0; i < a->nrows; i++) {        /* 0.8.1: every row's animation, drawn or not */
        float tgt;
        if (row_anim_target(a, a->rows[i], &tgt)) approach_now(&g_row_anim[i], tgt, 15.0f, dt, snap);
    }
    for (i = 0; i < a->nrows; i++) {
        RowKind k = a->rows[i];
        float ry = g.list_y + panel_row_y(a, &g, (float)i) - scroll_y, rh = panel_row_h(a, &g, i);
        float rt = clamp01((a->page_t - 0.04f - (float)(i + 1) * 0.035f) / 0.24f), re = ease_out_cubic(rt);
        float ox = (1.0f - re) * (float)a->page_dir * d_xu(0.06f * H());
        float fa = g.alpha * re, cy = ry + g.rh * 0.5f, xl, xr, xl0, xr0;
        float slot = d_text_width(&g_font_main, g.em, "\xE2\x80\xBA") + d_xu(g.em * 0.45f);   /* one of the ‹ › */
        int sel = i == a->row;
        uint32_t tc;
        char val[64];
        if (ry + rh < clip_top || ry > clip_bot) continue;
        if (ry < g.list_y - 0.002f * H()) fa *= clamp01(1.0f - (g.list_y - ry) / (g.rh * 0.6f));   /* scrolled off the top */
        if (ry + rh > clip_bot - g.rh * 0.6f) fa *= clamp01((clip_bot - ry - rh) / (g.rh * 0.6f) + 1.0f);
        if (fa <= 0.003f) continue;
        xl = g.x + ox + d_xu(g.rh * 0.36f);
        xr = g.x + ox + g.w - d_xu(g.rh * 0.34f) - (a->nrows > g.vis ? d_xu(0.016f * H()) : 0.0f);
        if (!sel) d_rrect(g.x + ox, ry, g.w, rh, g.rh * 0.5f, sf_col_alpha(g.drawer ? 0x22252EFFu : 0x161922D8u, fa));
        tc = sel ? TH_TEXT : TH_TEXT_DIM;
        if (k == ROW_D_DISC) {
            const Disc *dd = lib_game_disc(a->lib, game, i);
            char lab[TITLE_MAX];
            lib_disc_name(a->lib, dd, i, lab, sizeof(lab));
            val[0] = '\0';
            if (dd && dd->size) sf_format_size(dd->size, val, sizeof(val));
            {   /* 0.8.2: name and size centered together */
                float gap = d_xu(g.em * 1.4f), vw = val[0] ? d_text_width(&g_font_main, g.em * 0.9f, val) : 0.0f;
                float lw = sf_minf(d_text_width(&g_font_main, g.em, lab), xr - xl - vw - (val[0] ? gap : 0.0f));
                float x0 = (xl + xr) * 0.5f - (lw + (val[0] ? gap + vw : 0.0f)) * 0.5f;
                d_text_fit(&g_font_main, x0, cy, g.em, sf_col_alpha(tc, fa), lab, ALIGN_LEFT, lw);
                if (val[0]) d_text(&g_font_main, x0 + lw + gap + vw, cy, g.em * 0.9f, sf_col_alpha(TH_TEXT_DIM, fa), val, ALIGN_RIGHT);
            }
            continue;
        }
        app_row_value(a, k, val, sizeof(val));
        xl0 = xl; xr0 = xr;
        {   /* 0.8.2: the name and its value centered together in the row (the
             * name gets all the room the value leaves); the value is drawn
             * ending at xr */
            float vw = row_value_w(a, k, val, g.em, sel), gap = vw > 0.0f ? d_xu(g.em * (menu_is_group(k) ? 0.7f : 1.4f)) : 0.0f;
            float lw = sf_minf(d_text_width(&g_font_main, g.em, menu_label(k)), sf_maxf(xr - xl - vw - gap, (xr - xl) * 0.30f));
            float x0 = (xl + xr) * 0.5f - (lw + gap + vw) * 0.5f;
            d_text_fit(&g_font_main, x0, cy, g.em, sf_col_alpha(tc, fa), menu_label(k), ALIGN_LEFT, lw);
            xr = x0 + lw + gap + vw;
        }
        if (menu_is_group(k)) {
            d_text(&g_font_main, xr, cy, g.em * 1.25f, sf_col_alpha(tc, fa), "\xE2\x80\xBA", ALIGN_RIGHT);
        } else if (k == ROW_F_BACKGROUND) {
            int ch[BG_COUNT], n = bg_choices(a, ch), j, cur = 0;
            for (j = 0; j < n; j++) if (ch[j] == a->settings.background) cur = j;
            (void)cur;
            draw_value(sel ? xr : xr - slot, cy, g.em, val, sel, sel, fa);
            draw_bg_thumbs(a, xl0, ry + g.rh * 0.86f, xr0 - xl0, g_row_anim[i], fa);
        } else if (k == ROW_F_UI_COLOR) {
            draw_color_dots(xr, cy, g_row_anim[i], fa);
        } else if (menu_toggle(k, &a->settings) >= 0) {
            draw_switch(xr, cy, g_row_anim[i], sel, fa);
        } else if (per_game && k != ROW_G_PS2CFG) {
            const char *const *names = NULL;
            int cur = 0, n = menu_segments(k, a->panel_plat, &a->panel_cfg, &names, &cur);
            if (n > 0) draw_segments(xr, cy, g.em, names, n, g_row_anim[i], sel, fa);
            else if (val[0]) draw_value(sel ? xr : xr - slot, cy, g.em, val, sel, sel, fa);
        } else if (menu_is_slider(k)) {
            float tw = d_xu(0.15f * H()), th2 = 0.0075f * H(), tx = xr - tw;
            float v = g_row_anim[i];
            d_rrect(tx, cy - th2 * 0.5f, tw, th2, th2 * 0.5f, sf_col_alpha(0x464A58FFu, fa));
            d_rrect(tx, cy - th2 * 0.5f, tw * v, th2, th2 * 0.5f, sf_col_alpha(hot, fa));
            d_disc(tx + tw * v, cy, 0.0125f * H(), sf_col_alpha(0xFAFAFCFFu, fa));
            draw_value(tx - d_xu(g.em * 0.6f), cy, g.em, val, sel, 0, fa);
        } else if (val[0]) {
            draw_value((sel || menu_is_action(k)) ? xr : xr - slot, cy, g.em, val, sel, sel && !menu_is_action(k), fa);
        }
    }
    /* scrollbar */
    if (a->nrows > g.vis) {
        float top = g.list_y, span = panel_row_y(a, &g, (float)g.vis) - g.rg, total = panel_row_y(a, &g, (float)a->nrows) - g.rg;
        float frac = span / total, pos = scroll_y / sf_maxf(1.0f, total - span), bw = d_xu(0.005f * H());
        float tx = g.x + g.w - bw;
        d_rrect(tx, top, bw, span, 0.0025f * H(), sf_col_alpha(TH_SCROLL_TRACK, g.alpha));
        d_rrect(tx, top + (span * (1.0f - frac)) * clamp01(pos), bw, span * frac, 0.0025f * H(), sf_col_alpha(accent_or(TH_SCROLL_THUMB, 0.85f), g.alpha));
    }
}

/* ---- About spookyflow (0.8.2): a centered card over Flow Settings ---- */
static const char *const k_about_features[] = {
    "Your PS1, PS2 and PS3 games in one cover flow, with covers and case images",
    "Games on the internal drive and on NTFS / exFAT drives",
    "Launched through webMAN MOD, with Auto-Play, emulator and offline options",
    "PS2 configs, multi-disc games, display mode and the A\xE2\x80\x93Z jump",
    "Background music, eight backgrounds and your own pictures",
};
static const char *const k_about_tools[] = {
    "PSL1GHT SDK  \xC2\xB7  Tiny3D  \xC2\xB7  webMAN MOD",
    "minimp3 by lieff  \xC2\xB7  DejaVu fonts",
    "Written with Claude by Anthropic",
};

static void title_line(float lx, float lw, float y, uint32_t hot, float al)
{
    uint32_t col = glow(hot), clear = col & 0xFFFFFF00u;
    float th = 0.0036f * H(), gh = 0.016f * H();
    uint32_t core = sf_col_alpha(col, al), haze = sf_col_alpha(col, al * 0.24f);
    d_rect_hgrad(lx, y - gh * 0.5f, lw * 0.5f, gh, clear, haze);
    d_rect_hgrad(lx + lw * 0.5f, y - gh * 0.5f, lw * 0.5f, gh, haze, clear);
    d_rect_hgrad(lx, y - th * 0.5f, lw * 0.5f, th, clear, core);
    d_rect_hgrad(lx + lw * 0.5f, y - th * 0.5f, lw * 0.5f, th, core, clear);
}

static void draw_about(const App *a)
{
    float e = ease_out_cubic(a->about_t), al = ease_in_out(a->about_t), sc = 0.94f + 0.06f * e;
    float w = sf_minf(d_xu(1.22f * H()), W() * 0.90f) * sc, h = 0.80f * H() * sc;
    float x = W() * 0.5f - w * 0.5f, y = 0.462f * H() - h * 0.5f, cx = W() * 0.5f, r = 0.040f * H();
    float iw = w * 0.86f, yy;
    uint32_t hot = accent_white() ? 0xFFFFFFFFu : (accent() | 0xFFu);
    char ver[64];
    int i;
    if (al <= 0.003f) return;
    d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, 0.55f * al));
    d_rrect_glow(x, y, w, h, r, 0.040f * H(), sf_col_alpha(0x000000FFu, 0.6f * al));
    d_rrect(x, y, w, h, r, sf_col_alpha(TH_PANEL | 0xFFu, 0.985f * al));
    d_rrect_glow(x, y, w, h, r, 0.014f * H(), sf_col_alpha(hot, 0.22f * al * (0.6f + 0.4f * g_glow)));
    d_rrect_stroke(x, y, w, h, r, 0.0035f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF99u, 0.95f)), al));
    yy = y + 0.075f * h / 0.80f;
    d_text(&g_font_big, cx, yy, 0.068f * H() * sc, sf_col_alpha(TH_WORDMARK, al), APP_NAME, ALIGN_CENTER);
    snprintf(ver, sizeof(ver), "Version %s", APP_VERSION);
    yy += 0.058f * H() * sc;
    d_text(&g_font_main, cx, yy, 0.022f * H() * sc, sf_col_alpha(TH_TEXT_DIM, al), ver, ALIGN_CENTER);
    yy += 0.036f * H() * sc;
    title_line(cx - iw * 0.5f, iw, yy, hot, al);
    yy += 0.068f * H() * sc;
    d_text_box(&g_font_main, cx, yy, iw, 0.025f * H() * sc, 3, sf_col_alpha(TH_TEXT, al),
               "A cover-flow launcher for the PlayStation 3. Browse your games with their real covers and cases, "
               "change how each one starts, and launch it in a press.");
    yy += 0.082f * H() * sc;
    d_text(&g_font_main, cx, yy, 0.027f * H() * sc, sf_col_alpha(sf_col_mix(hot, 0xFFFFFFFFu, 0.45f), al), "Features", ALIGN_CENTER);
    yy += 0.044f * H() * sc;
    for (i = 0; i < (int)(sizeof(k_about_features) / sizeof(k_about_features[0])); i++) {
        d_text_fit(&g_font_main, cx, yy, 0.0225f * H() * sc, sf_col_alpha(TH_TEXT_DIM, al), k_about_features[i], ALIGN_CENTER, iw);
        yy += 0.037f * H() * sc;
    }
    yy += 0.022f * H() * sc;
    d_text(&g_font_main, cx, yy, 0.027f * H() * sc, sf_col_alpha(sf_col_mix(hot, 0xFFFFFFFFu, 0.45f), al), "Made with", ALIGN_CENTER);
    yy += 0.044f * H() * sc;
    for (i = 0; i < (int)(sizeof(k_about_tools) / sizeof(k_about_tools[0])); i++) {
        d_text_fit(&g_font_main, cx, yy, 0.0225f * H() * sc, sf_col_alpha(TH_TEXT_DIM, al), k_about_tools[i], ALIGN_CENTER, iw);
        yy += 0.037f * H() * sc;
    }
}

/* ------------------------------------------------------------------ */
/* Overlays                                                            */

/* A-Z jump feedback (L2/R2): the alphabet as a strip above the controls bar.
 * The letter you're on grows and glows in the UI color, the focus glides from
 * the letter you came from, letters with no games are dimmed; it fades out
 * after LETTER_TIME. */
static void draw_letter(const App *a)
{
    const GameList *gl = &a->lib->tab[a->tab];
    float t = a->letter_t, al;
    float span = W() * 0.62f, x0 = W() * 0.5f - span * 0.5f, step = span / (float)(LIB_BUCKETS - 1);
    float y = H() * 0.868f, ph = H() * 0.062f, k, pos;
    uint32_t acc;
    int b;
    if (t >= LETTER_TIME || !a->letter[0]) return;
    if (a->settings.show_info) {
        /* in the info line's place: the line fades out as the rail fades in */
        UiMetrics m;
        float ty, cy;
        layout_ui(d_screen(), &m);
        layout_info_y(d_screen(), &m, a->tab, &ty, &cy);
        y = cy;
    } else {
        /* no title/info line: the rail sits halfway between the selected case and the controls bar */
        CoverPos c0;
        BarLayout L;
        float case_bot;
        layout_cover(d_screen(), a->tab, 0.0f, &c0);
        ui_bar_layout(0, &L);
        case_bot = c0.cy + c0.h * 0.5f;
        y = sf_clampf((case_bot + L.bar_y) * 0.5f, case_bot + ph * 0.6f, L.bar_y - ph * 0.6f);
    }
    al = ease_in_out(rail_alpha(a));
    k = ease_out_cubic(sf_minf(1.0f, t / 0.30f));
    pos = sf_lerp((float)a->letter_prev_bucket, (float)a->letter_bucket, k);
    acc = accent_white() ? TH_TEXT : sf_col_mix(accent() | 0xFFu, 0xFFFFFFFFu, 0.30f);
    /* same fill and breathing outline as the controls bar */
    d_rrect(x0 - step * 1.2f, y - ph * 0.5f, span + step * 2.4f, ph, ph * 0.5f, sf_col_alpha(TH_BAR, al));
    d_rrect_stroke(x0 - step * 1.2f, y - ph * 0.5f, span + step * 2.4f, ph, ph * 0.5f, 0.0028f * H(),
                   sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), al));
    d_blob(x0 + pos * step, y, H() * 0.045f, sf_col_alpha(acc, 0.35f * al));
    for (b = 0; b < LIB_BUCKETS; b++) {
        char c[2];
        float d = (float)b - pos, f = clamp01(1.0f - sf_absf(d)), em = H() * (0.019f + 0.027f * f);   /* 0.8.2: neighbours stay small */
        int has = gl->first[b] >= 0;
        uint32_t col;
        c[0] = title_bucket_char(b); c[1] = '\0';
        if (sf_absf(d) < 0.5f) col = sf_col_alpha(acc, al);
        else col = sf_col_alpha(TH_TEXT, al * (has ? 0.35f + 0.45f * f : 0.13f));
        d_text(&g_font_main, x0 + (float)b * step, y, em, col, c, ALIGN_CENTER);
    }
}

/* Background music: a "now playing" card in the top-left corner when a song
 * starts (or SELECT skips): the kittens, a little equalizer, the song name.
 * Same fill and glowing outline as the controls bar; slides in from the
 * left, stays MUSIC_CARD_TIME, slides back out. */
#define MCARD_SCROLL_WAIT  1.5f    /* s before a long name starts to scroll */
#define MCARD_SCROLL_END   1.6f    /* s it rests at the end of the name */
#define MCARD_OUT          0.5f    /* slide-out time */

static void draw_music_card(App *a, float ia)
{
    float t = a->music_card_t, len, in, out, al, slide, over, speed, off = 0.0f, swap, ny;
    float h = 0.112f * H(), r = h * 0.5f, x, y = 0.028f * H(), w;
    float kh, kw, kx, ky, tx, em_l = 0.017f * H(), em_n = 0.027f * H(), maxw, namew, tw, fade;
    float now = (float)a->time;
    int i;
    if (!a->music_name[0]) return;
    /* kittens on the left, inside the rounded end */
    kh = h * 0.86f;
    kw = d_xu(kh * (float)g_cats_w / (float)g_cats_h);
    maxw = W() * (0.283f - 0.022f) - (d_xu(h * 0.30f) + kw + d_xu(h * 0.22f) + d_xu(h * 0.38f));   /* clear of the L1 badge */
    tw = d_text_width(&g_font_main, em_n, a->music_name);
    over = tw - maxw;
    speed = d_xu(0.075f * H());                           /* scroll speed (x units per s) */
    /* a long name keeps the card up until it has scrolled all the way */
    len = MUSIC_CARD_TIME;
    if (over > 0.0f) len = sf_maxf(len, MCARD_SCROLL_WAIT + over / speed + MCARD_SCROLL_END + MCARD_OUT);
    a->music_card_len = len;
    if (t >= len || a->asleep) return;
    in = ease_out_cubic(sf_minf(1.0f, t / 0.45f));
    out = t > len - MCARD_OUT ? ease_in_out((t - (len - MCARD_OUT)) / MCARD_OUT) : 0.0f;
    al = sf_clampf(in * (1.0f - out), 0.0f, 1.0f) * ia;
    if (al <= 0.0f) return;
    if (over > 0.0f) off = sf_clampf((t - MCARD_SCROLL_WAIT) * speed, 0.0f, over);
    namew = sf_minf(tw, maxw);
    namew = sf_maxf(namew, d_text_width(&g_font_main, em_l, "NOW PLAYING") + d_xu(0.05f * H()));
    w = d_xu(h * 0.30f) + kw + d_xu(h * 0.22f) + namew + d_xu(h * 0.38f);
    swap = a->music_swap_t < 1.0f && a->music_prev_name[0] ? ease_in_out(a->music_swap_t) : 1.0f;
    if (swap < 1.0f && a->music_prev_w > 0.0f) w = sf_lerp(a->music_prev_w, w, swap);   /* the card eases to the new width */
    a->music_cur_w = w;
    a->music_cur_off = off;
    slide = (1.0f - in) + out;
    x = 0.022f * W() - slide * (w + 0.03f * W());
    d_rrect(x, y, w, h, r, sf_col_alpha(TH_BAR, al));
    d_rrect_stroke(x, y, w, h, r, 0.0028f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), al));
    /* the kittens bob a little to the beat */
    kx = x + d_xu(h * 0.30f);
    ky = y + h * 0.5f - kh * 0.5f - fabsf(sinf(now * 3.4f)) * 0.006f * H();
    d_image(TEX_CATS, kx, ky, kw, kh, sf_col_alpha(0xFFFFFFFFu, al));
    /* label + equalizer, then the song name */
    tx = kx + kw + d_xu(h * 0.22f);
    d_text(&g_font_main, tx, y + h * 0.32f, em_l, sf_col_alpha(accent_or(TH_TEXT_DIM, 1.0f), al), "NOW PLAYING", ALIGN_LEFT);
    {
        float bx = tx + d_text_width(&g_font_main, em_l, "NOW PLAYING") + d_xu(0.010f * H());
        float bw = d_xu(0.0045f * H()), gap = d_xu(0.0035f * H()), base = y + h * 0.32f + em_l * 0.36f;
        for (i = 0; i < 4; i++) {
            float lv = 0.35f + 0.65f * fabsf(sinf(now * (4.1f + 1.3f * (float)i) + (float)i * 1.7f));
            float bh = em_l * 0.95f * lv;
            d_rect(bx + (float)i * (bw + gap), base - bh, bw, bh, sf_col_alpha(accent_or(TH_TEXT_DIM, 1.0f), al));
        }
    }
    fade = d_xu(0.016f * H());
    ny = y + h * 0.68f;
    if (swap < 1.0f) {
        /* the old name rises and fades out, then the new one comes up into place */
        float st = a->music_swap_t, go = ease_in_out(sf_clampf(st * 1.8f, 0.0f, 1.0f));
        d_text_window(&g_font_main, tx - a->music_prev_off, ny - go * h * 0.16f, em_n, sf_col_alpha(TH_TEXT, al * (1.0f - go)),
                      a->music_prev_name, tx - fade + sf_minf(a->music_prev_off, fade), tx + maxw + fade * 0.5f, fade);
        swap = ease_in_out(sf_clampf((st - 0.4f) / 0.6f, 0.0f, 1.0f));
        ny += (1.0f - swap) * h * 0.16f;
    }
    if (over <= 0.0f) {
        d_text(&g_font_main, tx, ny, em_n, sf_col_alpha(TH_TEXT, al * swap), a->music_name, ALIGN_LEFT);
    } else {
        /* too long: scrolls left until the end shows; the letters fade at the edges */
        d_text_window(&g_font_main, tx - off, ny, em_n, sf_col_alpha(TH_TEXT, al * swap), a->music_name,
                      tx - fade + sf_minf(off, fade), tx + maxw + fade * 0.5f, fade);   /* no left fade before it moves */
    }
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
    d_rrect_stroke(W() * 0.5f - w * 0.5f, y - h * 0.5f, w, h, h * 0.5f, 0.0025f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), al));
    d_text_fit(&g_font_main, W() * 0.5f, y, em, sf_col_alpha(TH_TEXT, al), a->toast, ALIGN_CENTER, w - d_xu(h));
}

/* ------------------------------------------------------------------ */
/* Loading screen                                                      */


/* the "spookyflow" wordmark with its ghostly glow */
/* the name on the loading screens (0.8.2: plain, no glow) */
static void draw_wordmark(const App *a, float wy, float em, float alpha)
{
    (void)a;
    d_text(&g_font_big, W() * 0.5f, wy, em, sf_col_alpha(TH_WORDMARK, alpha), APP_NAME, ALIGN_CENTER);
}

/* ---- Stage reveal (0.6.9): the opening when the background is Stage -----
 * From black, the horizon line draws itself out from the middle, the scene
 * opens up from it, the shapes and particles drift in, then the name and the
 * kittens (standing on the horizon, mirrored in the floor) appear. The cover
 * flow then comes in over the same scene. rt = seconds since it started. */
#define REVEAL_LINE_T0   0.15f
#define REVEAL_LINE_T    1.10f
#define REVEAL_OPEN_T0   0.90f
#define REVEAL_OPEN_T    1.70f
#define REVEAL_ITEMS_T0  2.00f
#define REVEAL_ITEMS_T   0.90f

static void stage_reveal_overlay(float rt)
{
    float hy = H() * LY_HORIZON_Y;
    float open = ease_in_out(clamp01((rt - REVEAL_OPEN_T0) / REVEAL_OPEN_T));
    uint32_t lite = sf_col_mix(stage_tint(), 0xFFFFFFFFu, 0.45f), clear = lite & 0xFFFFFF00u;
    if (open < 1.0f) {
        float g = 0.22f * H(), hb = open * (sf_maxf(hy, H() - hy) + g), gw = sf_minf(g, hb);
        float top = hy - hb, bot = hy + hb;
        if (top > 0.0f) d_rect(0, 0, W(), top, 0x000000FFu);
        if (gw > 0.0f) {
            d_rect_grad(0, top, W(), gw, 0x000000FFu, 0x00000000u);
            d_rect_grad(0, bot - gw, W(), gw, 0x00000000u, 0x000000FFu);
        }
        if (bot < H()) d_rect(0, bot, W(), H() - bot, 0x000000FFu);
        if (hb <= 0.0f) d_rect(0, 0, W(), H(), 0x000000FFu);
    }
    /* the line drawing itself across, then handing over to the scene's own */
    {
        float grow = ease_out_cubic(clamp01((rt - REVEAL_LINE_T0) / REVEAL_LINE_T));
        float la = 1.0f - ease_in_out(clamp01((rt - 1.8f) / 1.0f));
        if (grow > 0.0f && la > 0.0f) {
            float hw = W() * 0.5f * grow, th = 0.0035f * H(), gh = 0.030f * H();
            uint32_t core = sf_col_alpha(lite, la), haze = sf_col_alpha(lite, 0.30f * la);
            d_rect_hgrad(W() * 0.5f - hw, hy - gh * 0.5f, hw, gh, clear, haze);
            d_rect_hgrad(W() * 0.5f, hy - gh * 0.5f, hw, gh, haze, clear);
            d_rect_hgrad(W() * 0.5f - hw, hy - th * 0.5f, hw, th, clear, core);
            d_rect_hgrad(W() * 0.5f, hy - th * 0.5f, hw, th, core, clear);
            d_blob_ellipse(W() * 0.5f, hy, (0.06f + 0.22f * grow) * W() * PA(), 0.030f * H(), sf_col_alpha(lite, 0.40f * la));
        }
    }
}

/* ---- the kittens on the loading screens (0.7.1) ------------------------
 * They stand exactly where the selected game's case will stand: their feet
 * on its bottom edge, its contact shadow and floor reflection starting at
 * the same line, so the cover flow takes over the same spot. */
typedef struct { float cx, bottom, kh, kw, case_w, case_h; } KitSpot;
static void kitten_spot(const App *a, KitSpot *k)
{
    CoverPos c;
    layout_cover(d_screen(), a->tab, 0.0f, &c);
    k->cx = c.cx; k->bottom = c.cy + c.h * 0.5f; k->case_w = c.w; k->case_h = c.h;
    k->kh = c.h * 0.60f;
    k->kw = d_xu(k->kh * (float)g_cats_w / (float)g_cats_h);
}

/* one kitten sprite (or both: TEX_CATS): centre x, feet at `bottom`, squash
 * sx/sy around the feet, lifted by `lift`, turned by `ang`; the reflection
 * and contact shadow fade as it leaves the ground */
static void draw_kitten(const App *a, const KitSpot *k, int tex, float x, float lift, float sx, float sy, float ang, float alpha,
                        int reflect)
{
    float w = k->kw * sx, h = k->kh * sy, ground = 1.0f - clamp01(lift / (0.06f * H()));
    float y = k->bottom - lift - h;
    if (alpha <= 0.002f) return;
    if (ground > 0.0f && ang == 0.0f) {
        if (a->settings.reflections && reflect)
            reflect_tex(tex, x - w * 0.5f, y, w, h, k->bottom, k->case_h * REFLECT_DEPTH, 0xFFFFFFFFu, REFLECT_ALPHA * alpha * ground);
    }
    if (layout_floor() && ground > 0.0f)
        d_blob_ellipse(x, k->bottom, w * 0.40f, k->case_h * 0.045f, sf_col_alpha(0x000000FFu, 0.55f * alpha * ground));
    if (ang == 0.0f) d_image(tex, x - w * 0.5f, y, w, h, sf_col_alpha(0xFFFFFFFFu, alpha));
    else d_image_rot(tex, x, y + h * 0.5f, w * PA(), h, ang, 0.0f, 1.0f, 0.0f, 1.0f, sf_col_alpha(0xFFFFFFFFu, alpha));
}

/* where the name sits: centred above the kittens */
static float wordmark_y(const KitSpot *k) { return k->bottom - k->kh - 0.085f * H(); }
#define WORDMARK_EM (0.095f)

/* name and kittens over the Stage (no status text or progress line: 0.7.1) */
static void draw_loading_stage(const App *a, float alpha, float rt)
{
    float t = (float)a->time;
    float ea = alpha * ease_in_out(clamp01((rt - REVEAL_ITEMS_T0) / REVEAL_ITEMS_T));
    float rise = 1.0f - ease_out_cubic(clamp01((rt - REVEAL_ITEMS_T0) / (REVEAL_ITEMS_T * 1.3f)));
    KitSpot k;
    if (ea <= 0.002f) return;
    kitten_spot(a, &k);
    draw_wordmark(a, wordmark_y(&k) + 0.02f * H() * rise, WORDMARK_EM * H(), ea);
    draw_kitten(a, &k, TEX_CATS, k.cx, 0.03f * H() * rise, 1.0f, 1.0f + 0.010f * sinf(t * 2.1f), 0.0f, ea, 1);
}

/* ---- Falling letters (0.7.4): the loading screen of every background
 * but Stage. The kittens fade in (no reflection), then the name's letters
 * drop in one by one from above with a little bounce, and the name settles
 * into its ghostly glow. Same spot as the Stage screen: the kittens' feet on
 * the selected case's bottom edge, the name above them. */
#define ENTRANCE_START 2.0f /* seconds into loading the animation begins */
#define FALL_KIT0  0.35f    /* the kittens fade in */
#define FALL_KITD  1.00f
#define FALL_N0    0.85f    /* the first letter starts falling */
#define FALL_NL    0.075f   /* each next letter */
#define FALL_D     0.55f    /* how long a letter falls */
#define FALL_DROP  0.14f    /* from how high (fraction of the screen) */

static void draw_name_falling(const App *a, float cx, float wy, float em, float rt, float alpha)
{
    const char *name = APP_NAME;
    float total = d_text_width(&g_font_big, em, name), x = cx - total * 0.5f;
    int i, n = (int)strlen(name);
    float done = clamp01((rt - (FALL_N0 + (float)(n - 1) * FALL_NL + FALL_D)) / 0.5f);
    if (done > 0.0f) draw_wordmark(a, wy, em, alpha * done);     /* the settled name, with its ghostly glow */
    if (done >= 1.0f) return;
    for (i = 0; i < n; i++) {
        char c[2];
        float cw, u, la, dy;
        c[0] = name[i]; c[1] = '\0';
        cw = d_text_width(&g_font_big, em, c);
        u = clamp01((rt - (FALL_N0 + (float)i * FALL_NL)) / FALL_D);
        if (u > 0.0f) {
            la = clamp01(u * 3.0f);
            dy = (1.0f - ease_out_back(u)) * FALL_DROP * H();
            d_text(&g_font_big, x, wy - dy, em, sf_col_alpha(TH_WORDMARK, alpha * la * (1.0f - done)), c, ALIGN_LEFT);
        }
        x += cw;
    }
}

static void draw_loading_entrance(const App *a, float alpha, float rt)
{
    float t = (float)a->time;
    float ka = ease_in_out(clamp01((rt - FALL_KIT0) / FALL_KITD));
    KitSpot k;
    if (alpha <= 0.002f) return;
    kitten_spot(a, &k);
    if (ka > 0.0f)
        draw_kitten(a, &k, TEX_CATS, k.cx, 0.0f, 1.0f, 1.0f + 0.010f * sinf(t * 2.1f), 0.0f, alpha * ka, 0);
    draw_name_falling(a, k.cx, wordmark_y(&k), WORDMARK_EM * H(), rt, alpha);
}

/* 1: the loading screen is the Stage reveal; 2: the kittens' entrance over
 * the chosen background; -1: black (settings / Stage not ready yet) */
static int loading_style(const App *a)
{
    int stage_ok = g_stage_tex >= 0 || g_src_base != NULL;
    if (!a->settings_ready) return -1;
    if (a->settings.background != BG_STAGE) return 2;
    if (stage_ok) return 1;
    return a->load_step <= LOAD_PREPARE ? -1 : 2;        /* Stage failed to load: the entrance */
}

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

/* the cover flow and everything over it; ia = its opacity (0.7.1: also used
 * to fade it out when Rescan Library starts) */
static void draw_flow_ui(App *a, const UiMetrics *mm, float intro, float ia)
{
    UiMetrics m = *mm;
    /* 0.8.0: the Flow Settings drawer pushes the cover flow to the left */
    float flow_dx = a->panel == PANEL_FLOW ? -W() * 0.15f * ease_in_out(a->panel_anim) : 0.0f;
    float pgame = (a->panel == PANEL_GAME || a->panel == PANEL_DISC) ? ease_in_out(a->panel_anim) : 0.0f;
    /* tabs */
    {   /* 0.8.1: display mode dims the console tabs like the other cases; Game Settings hides them */
        float ta = ia * sf_lerp(1.0f, DISPLAY_DIM, ease_in_out(a->display_t)) * (1.0f - pgame);
        if (ta > 0.003f) draw_tabs(a, ta, -(1.0f - intro) * 0.05f * H());
    }
    /* cover rows (tab switch: old slides out, new slides in) */
    if (a->tab_anim < 1.0f && a->tab_prev != a->tab) {
        float e = ease_out_cubic(a->tab_anim), dist = W() * 0.45f;
        draw_flow_row(a, a->tab_prev, -(float)a->tab_dir * e * dist, (1.0f - e) * ia);
        draw_flow_row(a, a->tab, (float)a->tab_dir * (1.0f - e) * dist, e * ia);
    } else {
        /* after the kittens' loading screens the selected case appears where they stood */
        draw_flow_row(a, a->tab, (a->reveal_start > -999.0f ? 0.0f : (1.0f - intro) * W() * 0.12f) + flow_dx, ia);
    }
    {   /* display mode / Game Settings: the title and info fade out; Flow Settings: they move with the row */
        float fade = 1.0f - sf_maxf(ease_in_out(a->display_t), pgame);
        g_info_dx = flow_dx;
        if (fade > 0.0f) draw_info(a, ia * fade, &m);
        g_info_dx = 0.0f;
    }
    if (a->panel != PANEL_NONE) {
        if (a->panel == PANEL_FLOW)        /* the cover flow dims under the drawer */
            d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, 0.40f * ease_in_out(a->panel_anim)));
        draw_panel(a);
        if (a->about_t > 0.0f) draw_about(a);
    }
    draw_bar(a, ia);
    draw_letter(a);
    draw_music_card(a, ia);
    draw_toast(a);
}

void ui_draw(App *a)
{
    UiMetrics m;
    float intro, ia;
    int animated = 1;
    ui_set_color(a->settings.ui_color);
    set_glow(a->time);
    /* composite the Stage during loading, so the cover flow doesn't start with a hitch */
    if (a->mode == MODE_LOADING && a->settings_ready && a->settings.background == BG_STAGE && g_src_base &&
        (!g_stage_baked || g_baked_tint != stage_tint()))
        stage_bake(stage_tint());
    /* 0.7.5: on another background, Stage is made ready in small steps, so
     * choosing it later doesn't stall a frame */
    else if (a->mode != MODE_LOADING && a->settings_ready && a->settings.background != BG_STAGE && g_src_base &&
             a->intro >= 1.0f && (!g_stage_baked || g_baked_tint != stage_tint()))
        stage_job(stage_tint(), 0);
    layout_use_case_images(a->settings.cases);
    layout_set_floor(a->settings.background == BG_STAGE);   /* Stage: on its floor; others: centered */
    layout_ui(d_screen(), &m);
    g_intro_shapes = g_intro_parts = 1.0f;
    if (a->mode != MODE_LOADING) draw_background(a, 1.0f, animated);

    if (a->mode == MODE_LOADING) {
        int style = loading_style(a);
        /* Rescan Library (0.7.1): the cover flow fades out over the same
         * background while the kittens and the name fade in */
        float rf = a->rescan ? ease_in_out(clamp01((a->load_t - RESCAN_OUT_TIME * 0.6f) / 0.5f)) : 1.0f;
        if (style == 1) {
            float rt;
            if (a->reveal_start <= -999.0f) a->reveal_start = a->rescan ? -100.0f : sf_maxf(a->load_t, 0.5f);
            rt = a->load_t - a->reveal_start;
            g_intro_shapes = ease_in_out(clamp01((rt - 1.6f) / 1.6f));
            g_intro_parts = ease_in_out(clamp01((rt - 1.2f) / 1.6f));
            draw_background_as(a, 1.0f, 1, BG_STAGE);
            g_intro_shapes = g_intro_parts = 1.0f;
            stage_reveal_overlay(rt);
            draw_loading_stage(a, rf, rt);
        } else if (style == 2) {
            float rt, fade;
            int bg = a->settings.background == BG_STAGE ? BG_ANIMATED : a->settings.background;
            /* 0.8.1: many TVs stay black for 2-3 s after the PS3 switches video
             * mode, so the animation starts at ENTRANCE_START (it ends well within LOAD_MIN_TIME) */
            if (a->reveal_start <= -999.0f) a->reveal_start = a->rescan ? -100.0f : sf_maxf(a->load_t, ENTRANCE_START);
            rt = a->load_t - a->reveal_start;
            draw_background_as(a, 1.0f, animated, bg);
            fade = 1.0f - ease_in_out(clamp01(rt / 1.2f));      /* the background fades in from black */
            if (fade > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, fade));
            draw_loading_entrance(a, rf, rt);
        } else {
            d_rect(0, 0, W(), H(), 0x000000FFu);
        }
        if (a->rescan && a->load_t < RESCAN_OUT_TIME)
            draw_flow_ui(a, &m, 1.0f, 1.0f - ease_in_out(a->load_t / RESCAN_OUT_TIME));
    } else {
        intro = ease_out_cubic(a->intro);
        ia = ease_in_out(sf_clampf(a->intro * 1.4f - 0.25f, 0.0f, 1.0f));   /* UI fades in as the loading screen fades out */
        draw_flow_ui(a, &m, intro, ia);
        if (a->intro < 1.0f) {
            /* the loading screen (with its own background) cross-fades out on top */
            float la = 1.0f - ease_in_out(sf_clampf(a->intro * 1.25f, 0.0f, 1.0f));
            if (la > 0.0f && a->reveal_start > -999.0f && a->settings.background == BG_STAGE &&
                (g_stage_tex >= 0 || g_src_base)) {
                /* Stage reveal: same scene underneath, only the name and kittens fade */
                draw_loading_stage(a, la, 100.0f);
            } else if (la > 0.0f && a->reveal_start > -999.0f) {
                /* the kittens and the name fade; the cases take their place */
                draw_loading_entrance(a, la, 100.0f);
            } else if (la > 0.0f) {
                draw_background_as(a, la, 1, BG_ANIMATED);
                draw_loading(a, la);
            }
        }
    }
    if (a->exit_fade > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(TH_FADE, ease_in_out(a->exit_fade)));
    if (a->sleep_t > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, SLEEP_DIM * ease_in_out(a->sleep_t)));
}
