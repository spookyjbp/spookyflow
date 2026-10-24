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

static void bg_stage(float t, float alpha)
{
    uint32_t tint = stage_tint(), lite = sf_col_mix(tint, 0xFFFFFFFFu, 0.35f);
    float hy = H() * LY_HORIZON_Y;
    int i;
    /* base scene: brightness tinted with the UI color, white-hot cores on top */
    d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, alpha));
    d_image(g_stage_tex, 0, 0, W(), H(), sf_col_alpha(tint, alpha));
    if (g_stage_hi >= 0) d_image(g_stage_hi, 0, 0, W(), H(), sf_col_alpha(0xFFFFFFFFu, 0.35f * alpha));
    /* glow: soft halos around the lights, breathing slowly */
    if (g_stage_glow >= 0)
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
        stage_particles(t, alpha, hy, lite);
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
        float cy = H() * (s->y + 0.018f * k * sinf(t * 0.15f + p * 1.3f));
        float ang = 0.14f * sinf(t * 0.09f + p) + 0.04f * sinf(t * 0.23f + p * 0.7f);
        float sc = 1.0f + 0.05f * sinf(t * 0.19f + p * 1.9f);
        float al = s->alpha * (0.82f + 0.18f * sinf(t * 0.35f + p * 2.7f)) * alpha;
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
    d_rect_grad(0, 0, W(), H(), sf_col_alpha(TH_BG_TOP, alpha), sf_col_alpha(TH_BG_BOTTOM, alpha));
    if (bg == BG_STAGE && g_stage_tex >= 0) { bg_stage(t, alpha); return; }
    switch (bg) {
    case BG_AURORA:    bg_aurora(t, alpha); break;
    case BG_FIREFLIES: bg_fireflies(t, alpha); break;
    case BG_RIBBONS:   bg_ribbons(t, alpha); break;
    case BG_HALO:      bg_halo(t, alpha); break;
    case BG_TWILIGHT:  bg_twilight(t, alpha); break;
    default:           bg_drift(t, alpha); break;
    }
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
    if (gl->count <= 0) {
        d_text(&g_font_main, W() * 0.5f + xoff, H() * layout_flow_cy(), H() * 0.03f, sf_col_alpha(TH_TEXT_DIM, alpha), "No games", ALIGN_CENTER);
        return;
    }
    /* a soft line in the UI color under the selected case: brightest in the
     * middle and fading out at both ends, glowing with the outlines */
    {
        CoverPos c0;
        float frac = sf_absf(scroll - (float)center);
        float la = alpha * (1.0f - sf_minf(1.0f, frac * 2.0f) * 0.6f);
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
            if (idx < 0 || idx >= gl->count) continue;
            layout_cover(s, tab, (float)idx - scroll, &p);
            p.cx += xoff;
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
                draw_cover(a, tab, &gl->games[idx], &p, alpha, tex, art, no_cover,
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
    x = W() * 0.5f - total * 0.5f;
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
    x = W() * 0.5f - w * 0.5f;
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
            shadow_text(&g_font_main, W() * 0.5f, ty, em, TH_TEXT, title, maxw, al);
            if (g_info_style == 2) {   /* tapered accent line under the title */
                float tw = sf_minf(d_text_width(&g_font_main, em, title), maxw) + d_xu(H() * 0.06f);
                float ly = ty + em * 0.75f, th = H() * 0.0030f;
                uint32_t col = glow(accent_or(0xFFFFFFFFu, 1.0f) | 0xFFu), clear = col & 0xFFFFFF00u;
                d_rect_hgrad(W() * 0.5f - tw * 0.5f, ly, tw * 0.5f, th, clear, sf_col_alpha(col, al));
                d_rect_hgrad(W() * 0.5f, ly, tw * 0.5f, th, sf_col_alpha(col, al), clear);
            }
            if (a->settings.show_info) info_line(a, tab, idx, cy, m->chip_em * 1.10f, al, g_info_style == 2, 0);
        } else if (g_info_style == 3) {
            /* one frosted card under the case: title + info line */
            float tem = m->title_em * 1.05f, iem = m->chip_em * 1.05f;
            float iw = a->settings.show_info ? info_line(a, tab, idx, 0, iem, 0, 1, 1) : 0.0f;
            float tw = sf_minf(d_text_width(&g_font_main, tem, title), maxw);
            float cw = sf_maxf(tw, iw) + d_xu(H() * 0.09f), ch = a->settings.show_info ? H() * 0.105f : H() * 0.068f;
            float cx = W() * 0.5f - cw * 0.5f, ctop = cy - ch * 0.5f;
            if (k == 1 || t < 1.0f) {
                d_rrect(cx, ctop, cw, ch, H() * 0.022f, sf_col_alpha(TH_CHIP, al * 0.92f));
                d_rrect_stroke(cx, ctop, cw, ch, H() * 0.022f, 0.0028f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), al));
            }
            if (a->settings.show_info) {
                d_text_fit(&g_font_main, W() * 0.5f, ctop + ch * 0.36f, tem, sf_col_alpha(TH_TEXT, al), title, ALIGN_CENTER, maxw);
                info_line(a, tab, idx, ctop + ch * 0.72f, iem, al, 1, 0);
            } else {
                d_text_fit(&g_font_main, W() * 0.5f, cy, tem, sf_col_alpha(TH_TEXT, al), title, ALIGN_CENTER, maxw);
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
    if (g_info_style != 0) { ui_update_targets(a); draw_info_styled(a, alpha, m, mm.title_y, mm.chips_y); return; }
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
                       0.0028f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), alpha));
    }
    /* clip text to the (animating) pill so a wide old title never spills out */
    maxw = sf_minf(maxw, pw - 2.0f * pad * 0.6f);
    if (t < 1.0f) {
        draw_title_text(info_title(a, a->prev_tab, a->prev_idx), alpha * (1.0f - t), m, maxw);
        if (a->settings.show_info) draw_chips(a, a->prev_tab, a->prev_idx, alpha * (1.0f - t), m);
    }
    draw_title_text(cur, alpha * t, m, maxw);
    if (a->settings.show_info) draw_chips(a, a->info_tab, a->info_idx, alpha * t, m);
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
        group_add_badge(&L->g[1], ICON_TRIANGLE, L->icon); L->g[1].label = "Game Settings";
        group_add_badge(&L->g[2], ICON_L2, L->icon);
        group_add_badge(&L->g[2], ICON_R2, L->icon);
        L->g[2].label = "A\xE2\x80\x93Z";
        group_add_badge(&L->g[3], ICON_SELECT, L->icon);   L->g[3].label = "Next Song";
        group_add_badge(&L->g[4], ICON_START, L->icon);    L->g[4].label = "Flow Settings";
        group_add_badge(&L->g[5], ICON_CIRCLE, L->icon);   L->g[5].label = "Exit (Hold)";
        L->n = 6;
    } else {
        group_add_badge(&L->g[0], ICON_CROSS, L->icon);    L->g[0].label = panel_mode == 2 ? "Launch" : "Change";
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
    ui_bar_layout(a->panel == PANEL_DISC ? 2 : 1, &panel_l);
    d_rrect(main_l.bar_x, main_l.bar_y, main_l.bar_w, main_l.bar_h, main_l.bar_h * 0.5f, sf_col_alpha(TH_BAR, alpha));
    d_rrect_stroke(main_l.bar_x, main_l.bar_y, main_l.bar_w, main_l.bar_h, main_l.bar_h * 0.5f, 0.0028f * H(),
                   sf_col_alpha(glow(accent_or(0xFFFFFF80u, 0.90f)), alpha));
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
    int per_game = a->panel == PANEL_GAME || a->panel == PANEL_DISC;
    const char *ptitle = a->panel == PANEL_GAME ? "Game Settings" : a->panel == PANEL_DISC ? "Choose Disc" : "Flow Settings";
    const Game *g;
    if (e <= 0.0f) return;
    if (per_game && (a->panel_game < 0 || a->panel_game >= a->lib->tab[a->panel_plat].count)) return;
    g = per_game ? &a->lib->tab[a->panel_plat].games[a->panel_game] : NULL;

    d_rrect(px, py, pw, ph, 0.05f * H(), sf_col_alpha(TH_PANEL, e));
    d_rrect_stroke(px, py, pw, ph, 0.05f * H(), 0.0035f * H(), sf_col_alpha(glow(accent_or(0xFFFFFF99u, 0.95f)), e));
    d_text(&g_font_main, ix + d_xu(rh * 0.3f), py + pad + th * 0.45f, 0.034f * H() * sc, sf_col_alpha(TH_TEXT, e),
           ptitle, ALIGN_LEFT);
    if (g) {
        float tw = d_text_width(&g_font_main, 0.034f * H() * sc, ptitle);
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
        if (k == ROW_D_DISC) {
            const Disc *dd = lib_game_disc(a->lib, g, i);
            char lab[TITLE_MAX];
            lib_disc_name(a->lib, dd, i, lab, sizeof(lab));
            d_text(&g_font_main, ix + d_xu(rh * 0.42f), cy, em, sf_col_alpha(sel ? TH_TEXT : TH_TEXT_DIM, fa), lab, ALIGN_LEFT);
            val[0] = '\0';
            if (dd && dd->size) sf_format_size(dd->size, val, sizeof(val));
            if (val[0]) draw_value(xr, cy, em, val, sel, 0, fa);
            continue;
        }
        d_text(&g_font_main, ix + d_xu(rh * 0.42f), cy, em, sf_col_alpha(sel ? TH_TEXT : TH_TEXT_DIM, fa), menu_label(k), ALIGN_LEFT);
        app_row_value(a, k, val, sizeof(val));
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
    al = t < 0.45f ? 1.0f : sf_maxf(0.0f, 1.0f - (t - 0.45f) / (LETTER_TIME - 0.45f));
    al *= sf_minf(1.0f, t / 0.08f);                                     /* quick fade in */
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
        float d = (float)b - pos, f = expf(-d * d / 1.8f), em = H() * (0.019f + 0.027f * f);
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
    float t = a->music_card_t, len, in, out, al, slide, over, speed, off = 0.0f;
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
    if (over <= 0.0f) {
        d_text(&g_font_main, tx, y + h * 0.68f, em_n, sf_col_alpha(TH_TEXT, al), a->music_name, ALIGN_LEFT);
    } else {
        /* too long: scrolls left until the end shows; the letters fade at the edges */
        fade = d_xu(0.016f * H());
        d_text_window(&g_font_main, tx - off, y + h * 0.68f, em_n, sf_col_alpha(TH_TEXT, al), a->music_name,
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
    int animated = 1;
    ui_set_color(a->settings.ui_color);
    set_glow(a->time);
    layout_use_case_images(a->settings.cases);
    layout_set_floor(a->settings.background == BG_STAGE);   /* Stage: on its floor; others: centered */
    layout_ui(d_screen(), &m);
    draw_background(a, 1.0f, animated);

    if (a->mode == MODE_LOADING) {
        draw_loading(a, 1.0f);
    } else {
        intro = ease_out_cubic(a->intro);
        ia = ease_in_out(sf_clampf(a->intro * 1.4f - 0.25f, 0.0f, 1.0f));   /* UI fades in as the loading screen fades out */
        /* tabs */
        draw_tabs(a, ia, -(1.0f - intro) * 0.05f * H());
        /* cover rows (tab switch: old slides out, new slides in) */
        if (a->tab_anim < 1.0f && a->tab_prev != a->tab) {
            float e = ease_out_cubic(a->tab_anim), dist = W() * 0.45f;
            draw_flow_row(a, a->tab_prev, -(float)a->tab_dir * e * dist, (1.0f - e) * ia);
            draw_flow_row(a, a->tab, (float)a->tab_dir * (1.0f - e) * dist, e * ia);
        } else {
            draw_flow_row(a, a->tab, (1.0f - intro) * W() * 0.12f, ia);
        }
        draw_info(a, ia, &m);
        if (a->panel != PANEL_NONE) {
            float pe = ease_out_cubic(a->panel_anim);
            d_rect(0, 0, W(), H(), sf_col_alpha(TH_DIM_OVERLAY, TH_DIM_MAX * pe));
            draw_panel(a);
        }
        draw_bar(a, ia);
        draw_letter(a);
        draw_music_card(a, ia);
        draw_toast(a);
        if (a->intro < 1.0f) {
            /* the loading screen (with its own background) cross-fades out on top */
            float la = 1.0f - ease_in_out(sf_clampf(a->intro * 1.25f, 0.0f, 1.0f));
            if (la > 0.0f) {
                draw_background_as(a, la, 1, BG_ANIMATED);
                draw_loading(a, la);
            }
        }
    }
    if (a->exit_fade > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(TH_FADE, ease_in_out(a->exit_fade)));
    if (a->sleep_t > 0.0f) d_rect(0, 0, W(), H(), sf_col_alpha(0x000000FFu, SLEEP_DIM * ease_in_out(a->sleep_t)));
}
