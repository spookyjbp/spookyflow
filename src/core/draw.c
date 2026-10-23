/* draw.c - anti-aliased shapes (vertex-color feathering) and text. */
#include "draw.h"
#include "icons.h"
#include "render.h"
#include "util.h"
#include <math.h>
#include <string.h>

#define PI_F 3.14159265f
#define MAX_PATH 64

static Screen g_scr = { 1280.0f, 720.0f, 1.0f, 1.0f };

void d_set_screen(const Screen *s) { g_scr = *s; }
const Screen *d_screen(void) { return &g_scr; }
float d_xu(float v) { return v / g_scr.pa; }

static float feather(void) { return 1.0f / (g_scr.px > 0.01f ? g_scr.px : 1.0f); }

/* ---- path of a rounded rectangle (clockwise, y down) ---- */
typedef struct { float x[MAX_PATH], y[MAX_PATH], nx[MAX_PATH], ny[MAX_PATH]; int n; } Path;

static void path_rrect(Path *p, float x, float y, float w, float h, float r)
{
    float ry, rx, cx[4], cy[4], a0[4];
    int seg, c, k;
    if (w < 0) w = 0;
    if (h < 0) h = 0;
    ry = r;
    if (ry > h * 0.5f) ry = h * 0.5f;
    if (ry * (1.0f / g_scr.pa) > w * 0.5f) ry = w * 0.5f * g_scr.pa;
    if (ry < 0) ry = 0;
    rx = ry / g_scr.pa;
    seg = (int)(ry * g_scr.px / 6.0f) + 2;   /* strokes/glows only: fills use the disc texture */
    seg = sf_clampi(seg, 2, 8);
    cx[0] = x + rx;     cy[0] = y + ry;     a0[0] = PI_F;
    cx[1] = x + w - rx; cy[1] = y + ry;     a0[1] = 1.5f * PI_F;
    cx[2] = x + w - rx; cy[2] = y + h - ry; a0[2] = 0.0f;
    cx[3] = x + rx;     cy[3] = y + h - ry; a0[3] = 0.5f * PI_F;
    p->n = 0;
    for (c = 0; c < 4; c++) {
        for (k = 0; k <= seg; k++) {
            float a = a0[c] + (0.5f * PI_F) * (float)k / (float)seg;
            float ca = cosf(a), sa = sinf(a);
            if (p->n >= MAX_PATH) return;
            p->x[p->n] = cx[c] + ca * rx;
            p->y[p->n] = cy[c] + sa * ry;
            p->nx[p->n] = ca / g_scr.pa;
            p->ny[p->n] = sa;
            p->n++;
        }
    }
}

static uint32_t grad_at(float py, float y, float h, uint32_t top, uint32_t bot)
{
    if (top == bot || h <= 0.0f) return top;
    return sf_col_mix(top, bot, (py - y) / h);
}

/* Ring band between offsets d0 (alpha a0) and d1 (alpha a1) along normals. */
static void band(const Path *p, float d0, uint32_t c0, float d1, uint32_t c1, int closed)
{
    int k, last = closed ? p->n : p->n - 1;
    for (k = 0; k < last; k++) {
        int j = (k + 1) % p->n;
        float ax = p->x[k] + p->nx[k] * d0, ay = p->y[k] + p->ny[k] * d0;
        float bx = p->x[j] + p->nx[j] * d0, by = p->y[j] + p->ny[j] * d0;
        float cx = p->x[j] + p->nx[j] * d1, cy = p->y[j] + p->ny[j] * d1;
        float dx = p->x[k] + p->nx[k] * d1, dy = p->y[k] + p->ny[k] * d1;
        r_vtx(ax, ay, c0); r_vtx(bx, by, c0); r_vtx(cx, cy, c1);
        r_vtx(ax, ay, c0); r_vtx(cx, cy, c1); r_vtx(dx, dy, c1);
    }
}

void d_rect(float x, float y, float w, float h, uint32_t col)
{
    d_rect_grad(x, y, w, h, col, col);
}

void d_rect_grad(float x, float y, float w, float h, uint32_t top, uint32_t bot)
{
    if ((top & 255) == 0 && (bot & 255) == 0) return;
    r_tris_begin();
    r_vtx(x, y, top); r_vtx(x + w, y, top); r_vtx(x + w, y + h, bot);
    r_vtx(x, y, top); r_vtx(x + w, y + h, bot); r_vtx(x, y + h, bot);
    r_tris_end();
}

void d_rect_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right)
{
    if ((left & 255) == 0 && (right & 255) == 0) return;
    if (w <= 0 || h <= 0) return;
    r_tris_begin();
    r_vtx(x, y, left); r_vtx(x + w, y, right); r_vtx(x + w, y + h, right);
    r_vtx(x, y, left); r_vtx(x + w, y + h, right); r_vtx(x, y + h, left);
    r_tris_end();
}

/* Rounded rectangle as a 9-slice of the anti-aliased disc texture: 9 quads
 * (36 vertices) whatever the size, instead of hundreds of feathered
 * triangles. Corner quads map the disc's quadrants; edges and center use the
 * disc's middle row/column. Gradient = per-vertex color by y. */
void d_rrect_grad(float x, float y, float w, float h, float r, uint32_t top, uint32_t bot)
{
    float ry, rx, xs[4], ys[4], us[4] = { 0.0f, 0.5f, 0.5f, 1.0f };
    uint32_t cs[4];
    int i, j;
    if (w <= 0 || h <= 0) return;
    if ((top & 255) == 0 && (bot & 255) == 0) return;
    ry = r;
    if (ry > h * 0.5f) ry = h * 0.5f;
    if (ry / g_scr.pa > w * 0.5f) ry = w * 0.5f * g_scr.pa;
    if (ry * g_scr.px < 0.75f) { d_rect_grad(x, y, w, h, top, bot); return; }
    rx = ry / g_scr.pa;
    xs[0] = x; xs[1] = x + rx; xs[2] = x + w - rx; xs[3] = x + w;
    ys[0] = y; ys[1] = y + ry; ys[2] = y + h - ry; ys[3] = y + h;
    for (j = 0; j < 4; j++) cs[j] = grad_at(ys[j], y, h, top, bot);
    r_quads_begin(TEX_DISC);
    for (j = 0; j < 3; j++) {
        for (i = 0; i < 3; i++) {
            if (xs[i + 1] - xs[i] <= 0.0f || ys[j + 1] - ys[j] <= 0.0f) continue;
            r_qvtx(xs[i],     ys[j],     us[i],     us[j],     cs[j]);
            r_qvtx(xs[i + 1], ys[j],     us[i + 1], us[j],     cs[j]);
            r_qvtx(xs[i + 1], ys[j + 1], us[i + 1], us[j + 1], cs[j + 1]);
            r_qvtx(xs[i],     ys[j + 1], us[i],     us[j + 1], cs[j + 1]);
        }
    }
    r_quads_end();
}

void d_gen_disc_alpha(unsigned char *buf, int size)
{
    int x, y;
    float c = (float)size * 0.5f, rad = c - 1.0f;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            float dx = (float)x + 0.5f - c, dy = (float)y + 0.5f - c;
            float d = sqrtf(dx * dx + dy * dy);
            float a = sf_clampf(rad - d + 0.5f, 0.0f, 1.0f);   /* 1-texel anti-aliased edge */
            buf[y * size + x] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
}

void d_gen_blob_alpha(unsigned char *buf, int size)
{
    int x, y;
    float c = (float)size * 0.5f;
    for (y = 0; y < size; y++) {
        for (x = 0; x < size; x++) {
            float dx = ((float)x + 0.5f - c) / c, dy = ((float)y + 0.5f - c) / c;
            float d = sf_clampf(sqrtf(dx * dx + dy * dy), 0.0f, 1.0f);
            float a = 1.0f - d * d;                /* smooth radial falloff, 0 at the rim */
            a = a * a * (3.0f - 2.0f * a);          /* smoothstep: soft center, soft edge */
            buf[y * size + x] = (unsigned char)(a * 255.0f + 0.5f);
        }
    }
}

void d_rrect(float x, float y, float w, float h, float r, uint32_t col)
{
    d_rrect_grad(x, y, w, h, r, col, col);
}

void d_rrect_stroke(float x, float y, float w, float h, float r, float thick, uint32_t col)
{
    Path p;
    float f = feather();
    uint32_t z = sf_col_alpha(col, 0.0f);
    if ((col & 255) == 0 || w <= 0 || h <= 0) return;
    path_rrect(&p, x, y, w, h, r);
    if (thick < f) { col = sf_col_alpha(col, thick / f); thick = f * 0.5f; }
    r_tris_begin();
    band(&p, f, z, 0.0f, col, 1);
    band(&p, 0.0f, col, -thick, col, 1);
    band(&p, -thick, col, -thick - f, z, 1);
    r_tris_end();
}

void d_rrect_glow(float x, float y, float w, float h, float r, float spread, uint32_t col)
{
    Path p;
    if ((col & 255) == 0 || w <= 0 || h <= 0) return;
    path_rrect(&p, x, y, w, h, r);
    r_tris_begin();
    band(&p, 0.0f, col, spread * 0.35f, sf_col_alpha(col, 0.45f), 1);
    band(&p, spread * 0.35f, sf_col_alpha(col, 0.45f), spread, sf_col_alpha(col, 0.0f), 1);
    r_tris_end();
}

static int circle_segs(float r)
{
    return sf_clampi((int)(r * g_scr.px / 2.0f) + 8, 12, 56);
}

void d_disc(float cx, float cy, float r, uint32_t col)
{
    int k, n = circle_segs(r);
    float f = feather();
    uint32_t z = sf_col_alpha(col, 0.0f);
    if ((col & 255) == 0 || r <= 0) return;
    r_tris_begin();
    for (k = 0; k < n; k++) {
        float a0 = 2 * PI_F * (float)k / (float)n, a1 = 2 * PI_F * (float)(k + 1) / (float)n;
        float c0 = cosf(a0) / g_scr.pa, s0 = sinf(a0), c1 = cosf(a1) / g_scr.pa, s1 = sinf(a1);
        r_vtx(cx, cy, col);
        r_vtx(cx + c0 * r, cy + s0 * r, col);
        r_vtx(cx + c1 * r, cy + s1 * r, col);
        r_vtx(cx + c0 * r, cy + s0 * r, col);
        r_vtx(cx + c0 * (r + f), cy + s0 * (r + f), z);
        r_vtx(cx + c1 * (r + f), cy + s1 * (r + f), z);
        r_vtx(cx + c0 * r, cy + s0 * r, col);
        r_vtx(cx + c1 * (r + f), cy + s1 * (r + f), z);
        r_vtx(cx + c1 * r, cy + s1 * r, col);
    }
    r_tris_end();
}

void d_blob(float cx, float cy, float r, uint32_t col)
{
    /* one textured quad (was ~600 vertices of feathered rings) */
    float rx;
    if ((col & 255) == 0 || r <= 0) return;
    rx = r / g_scr.pa;
    r_quads_begin(TEX_BLOB);
    r_qvtx(cx - rx, cy - r, 0.0f, 0.0f, col);
    r_qvtx(cx + rx, cy - r, 1.0f, 0.0f, col);
    r_qvtx(cx + rx, cy + r, 1.0f, 1.0f, col);
    r_qvtx(cx - rx, cy + r, 0.0f, 1.0f, col);
    r_quads_end();
}

void d_blob_ellipse(float cx, float cy, float rx, float ry, uint32_t col)
{
    if ((col & 255) == 0 || rx <= 0 || ry <= 0) return;
    rx /= g_scr.pa;
    r_quads_begin(TEX_BLOB);
    r_qvtx(cx - rx, cy - ry, 0.0f, 0.0f, col);
    r_qvtx(cx + rx, cy - ry, 1.0f, 0.0f, col);
    r_qvtx(cx + rx, cy + ry, 1.0f, 1.0f, col);
    r_qvtx(cx - rx, cy + ry, 0.0f, 1.0f, col);
    r_quads_end();
}

void d_arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t col)
{
    Path p;
    int k, n;
    float f = feather();
    uint32_t z = sf_col_alpha(col, 0.0f);
    float span = a1 - a0;
    if ((col & 255) == 0 || span <= 0.0001f) return;
    if (span > 2 * PI_F) span = 2 * PI_F;
    n = (int)((float)circle_segs(r) * span / (2 * PI_F)) + 2;
    if (n > MAX_PATH) n = MAX_PATH;
    p.n = n;
    for (k = 0; k < n; k++) {
        /* angle 0 = up, clockwise (y down) */
        float a = a0 + span * (float)k / (float)(n - 1) - 0.5f * PI_F;
        float ca = cosf(a), sa = sinf(a);
        float rm = r - thick * 0.5f;
        p.x[k] = cx + ca * rm / g_scr.pa;
        p.y[k] = cy + sa * rm;
        p.nx[k] = ca / g_scr.pa;
        p.ny[k] = sa;
    }
    r_tris_begin();
    band(&p, thick * 0.5f + f, z, thick * 0.5f, col, 0);
    band(&p, thick * 0.5f, col, -thick * 0.5f, col, 0);
    band(&p, -thick * 0.5f, col, -thick * 0.5f - f, z, 0);
    r_tris_end();
}

void d_line(float x0, float y0, float x1, float y1, float thick, uint32_t col)
{
    /* work in physical-proportional space (x scaled by pa) */
    float X0 = x0 * g_scr.pa, X1 = x1 * g_scr.pa;
    float dx = X1 - X0, dy = y1 - y0;
    float len = sqrtf(dx * dx + dy * dy), f = feather();
    float nx, ny, h = thick * 0.5f;
    uint32_t z = sf_col_alpha(col, 0.0f);
    if (len < 1e-6f || (col & 255) == 0) return;
    nx = -dy / len; ny = dx / len;
    {
        /* four parallel edges: -h-f, -h, +h, +h+f */
        float off[4] = { -h - f, -h, h, h + f };
        uint32_t cc[4] = { z, col, col, z };
        int i;
        r_tris_begin();
        for (i = 0; i < 3; i++) {
            float ax = (X0 + nx * off[i]) / g_scr.pa, ay = y0 + ny * off[i];
            float bx = (X1 + nx * off[i]) / g_scr.pa, by = y1 + ny * off[i];
            float cx = (X1 + nx * off[i + 1]) / g_scr.pa, cy = y1 + ny * off[i + 1];
            float ex = (X0 + nx * off[i + 1]) / g_scr.pa, ey = y0 + ny * off[i + 1];
            r_vtx(ax, ay, cc[i]); r_vtx(bx, by, cc[i]); r_vtx(cx, cy, cc[i + 1]);
            r_vtx(ax, ay, cc[i]); r_vtx(cx, cy, cc[i + 1]); r_vtx(ex, ey, cc[i + 1]);
        }
        r_tris_end();
    }
}

/* ---------------- text ---------------- */

static int font_tex(const Font *f) { return f == &g_font_big ? TEX_FONT_BIG : TEX_FONT_MAIN; }

static float text_width_n(const Font *f, float em, const char *s, size_t nbytes)
{
    float sc = em / (float)f->px, w = 0;
    const char *end = s + nbytes;
    while (s < end && *s) {
        uint32_t cp = sf_utf8_next(&s);
        w += font_glyph(f, cp)->adv;
    }
    return w * sc / g_scr.pa;
}

float d_text_width(const Font *f, float em, const char *s)
{
    return text_width_n(f, em, s, strlen(s));
}

static void text_raw(const Font *f, float x, float y, float em, uint32_t col, const char *s, size_t nbytes)
{
    float sc = em / (float)f->px;
    float base = y + (float)f->cap * sc * 0.5f;
    float pen = x;
    float iw = 1.0f / (float)f->atlas_w, ih = 1.0f / (float)f->atlas_h;
    const char *end = s + nbytes;
    if ((col & 255) == 0) return;
    r_quads_begin(font_tex(f));
    while (s < end && *s) {
        uint32_t cp = sf_utf8_next(&s);
        const FontGlyph *g = font_glyph(f, cp);
        if (g->w && g->h) {
            float gx = pen + (float)g->bx * sc / g_scr.pa, gy = base + (float)g->by * sc;
            float gw = (float)g->w * sc / g_scr.pa, gh = (float)g->h * sc;
            float u0 = (float)g->x * iw, v0 = (float)g->y * ih;
            float u1 = (float)(g->x + g->w) * iw, v1 = (float)(g->y + g->h) * ih;
            r_qvtx(gx, gy, u0, v0, col);
            r_qvtx(gx + gw, gy, u1, v0, col);
            r_qvtx(gx + gw, gy + gh, u1, v1, col);
            r_qvtx(gx, gy + gh, u0, v1, col);
        }
        pen += g->adv * sc / g_scr.pa;
    }
    r_quads_end();
}

float d_text(const Font *f, float x, float y, float em, uint32_t col, const char *s, int align)
{
    float w = d_text_width(f, em, s);
    if (align == ALIGN_CENTER) x -= w * 0.5f;
    else if (align == ALIGN_RIGHT) x -= w;
    text_raw(f, x, y, em, col, s, strlen(s));
    return w;
}

float d_text_fit(const Font *f, float x, float y, float em, uint32_t col, const char *s, int align, float maxw)
{
    char buf[256];
    size_t n = strlen(s), cut;
    float w = d_text_width(f, em, s);
    if (w <= maxw) return d_text(f, x, y, em, col, s, align);
    /* shrink byte count until "prefix…" fits; step back over UTF-8 continuation bytes */
    if (n > sizeof(buf) - 4) n = sizeof(buf) - 4;
    cut = n;
    while (cut > 0) {
        cut--;
        while (cut > 0 && ((unsigned char)s[cut] & 0xC0) == 0x80) cut--;
        memcpy(buf, s, cut);
        while (cut > 0 && buf[cut - 1] == ' ') cut--;
        memcpy(buf + cut, "\xE2\x80\xA6", 4); /* U+2026 + NUL */
        if (d_text_width(f, em, buf) <= maxw) break;
    }
    if (cut == 0) sf_strlcpy(buf, "\xE2\x80\xA6", sizeof(buf));
    return d_text(f, x, y, em, col, buf, align);
}

void d_text_box(const Font *f, float cx, float cy, float maxw, float em, int maxlines, uint32_t col, const char *s)
{
    enum { MAXL = 6 };
    size_t start[MAXL], len[MAXL];
    int nl = 0, i;
    size_t pos = 0, total = strlen(s);
    float lh = em * 1.18f;
    if (maxlines > MAXL) maxlines = MAXL;
    while (pos < total && nl < maxlines) {
        size_t line_end, q;
        while (pos < total && s[pos] == ' ') pos++;
        if (pos >= total) break;
        line_end = pos;
        q = pos;
        /* extend word by word; the first word is always accepted */
        for (;;) {
            size_t e = q;
            while (e < total && s[e] != ' ') e++;
            if (line_end == pos) line_end = e;
            else if (text_width_n(f, em, s + pos, e - pos) <= maxw) line_end = e;
            else break;
            if (e >= total) break;
            if (text_width_n(f, em, s + pos, line_end - pos) > maxw) break;
            q = e + 1;
        }
        start[nl] = pos; len[nl] = line_end - pos; nl++;
        pos = line_end;
    }
    if (nl == 0) return;
    {
        char buf[200];
        float y0 = cy - lh * (float)(nl - 1) * 0.5f;
        for (i = 0; i < nl; i++) {
            size_t L = len[i] < sizeof(buf) - 1 ? len[i] : sizeof(buf) - 1;
            memcpy(buf, s + start[i], L);
            buf[L] = '\0';
            if (i == nl - 1 && start[i] + len[i] < total) {
                /* more text remains: ellipsize the last line */
                sf_strlcat(buf, " \xE2\x80\xA6", sizeof(buf));
            }
            d_text_fit(f, cx, y0 + lh * (float)i, em, col, buf, ALIGN_CENTER, maxw);
        }
    }
}

void d_badge(int icon, float cx, float cy, float size, uint32_t tint)
{
    float hw, hh = size * 0.5f, u0, u1;
    if ((tint & 255) == 0 || icon < 0 || icon >= g_icons.count) return;
    hw = d_xu(size * icons_aspect(icon)) * 0.5f;
    u0 = (float)g_icons.rects[icon].x / (float)g_icons.w;
    u1 = (float)(g_icons.rects[icon].x + g_icons.rects[icon].w) / (float)g_icons.w;
    r_quads_begin(TEX_ICONS);
    r_qvtx(cx - hw, cy - hh, u0, 0.0f, tint);
    r_qvtx(cx + hw, cy - hh, u1, 0.0f, tint);
    r_qvtx(cx + hw, cy + hh, u1, 1.0f, tint);
    r_qvtx(cx - hw, cy + hh, u0, 1.0f, tint);
    r_quads_end();
}

void d_image_rot(int tex, float cx, float cy, float w, float h, float angle, float u0, float u1, uint32_t tint)
{
    static const float cxs[4] = { -1, 1, 1, -1 }, cys[4] = { -1, -1, 1, 1 };
    float c = cosf(angle), s = sinf(angle);
    int i;
    if ((tint & 255) == 0 || w <= 0 || h <= 0) return;
    r_quads_begin(tex);
    for (i = 0; i < 4; i++) {
        float dx = cxs[i] * w * 0.5f, dy = cys[i] * h * 0.5f;
        float rx = dx * c - dy * s, ry = dx * s + dy * c;
        r_qvtx(cx + rx / g_scr.pa, cy + ry, i == 0 || i == 3 ? u0 : u1, i < 2 ? 0.0f : 1.0f, tint);
    }
    r_quads_end();
}

void d_image_v(int tex, float x, float y, float w, float h, float v0, float v1, uint32_t top, uint32_t bot)
{
    if (((top & 255) == 0 && (bot & 255) == 0) || w <= 0 || h <= 0) return;
    r_quads_begin(tex);
    r_qvtx(x, y, 0.0f, v0, top);
    r_qvtx(x + w, y, 1.0f, v0, top);
    r_qvtx(x + w, y + h, 1.0f, v1, bot);
    r_qvtx(x, y + h, 0.0f, v1, bot);
    r_quads_end();
}

void d_image(int tex, float x, float y, float w, float h, uint32_t tint)
{
    if ((tint & 255) == 0 || w <= 0 || h <= 0) return;
    r_quads_begin(tex);
    r_qvtx(x, y, 0.0f, 0.0f, tint);
    r_qvtx(x + w, y, 1.0f, 0.0f, tint);
    r_qvtx(x + w, y + h, 1.0f, 1.0f, tint);
    r_qvtx(x, y + h, 0.0f, 1.0f, tint);
    r_quads_end();
}
