/* render_soft.c - software implementation of render.h for host previews.
 * Rasterizes per-vertex-colored triangles and textured quads (bilinear),
 * non-AA pixel-center sampling like the RSX, straight alpha blending. */
#include "render_soft.h"
#include "../../src/core/render.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int w, h; unsigned char *rgba; } SoftTex;

static float *g_fb;               /* RGB float */
static int g_pw, g_ph;
static float g_sx = 1.0f, g_sy = 1.0f;
static SoftTex g_tex[TEX_COUNT_ALL];
static uint16_t g_cover_mem[R_COVER_SLOTS][R_COVER_SLOT_BYTES / 2];

typedef struct { float x, y, u, v, c[4]; } SV;
static SV g_v[4];
static int g_nv, g_mode, g_curtex;  /* mode 0 tris, 1 quads */
long g_soft_vertex_count;

int soft_init(int pw, int ph, float logical_w, float logical_h)
{
    free(g_fb);
    g_fb = (float *)calloc((size_t)pw * (size_t)ph * 3u, sizeof(float));
    if (!g_fb) return -1;
    g_pw = pw; g_ph = ph;
    g_sx = (float)pw / logical_w;
    g_sy = (float)ph / logical_h;
    return 0;
}

void soft_shutdown(void)
{
    int i;
    free(g_fb); g_fb = NULL;
    for (i = 0; i < TEX_COUNT_ALL; i++) { free(g_tex[i].rgba); g_tex[i].rgba = NULL; }
}

int soft_write_ppm(const char *path)
{
    FILE *f = fopen(path, "wb");
    int i, n = g_pw * g_ph;
    if (!f) return -1;
    fprintf(f, "P6\n%d %d\n255\n", g_pw, g_ph);
    for (i = 0; i < n * 3; i++) {
        float v = g_fb[i];
        int b = (int)(v * 255.0f + 0.5f);
        if (b < 0) b = 0;
        if (b > 255) b = 255;
        fputc(b, f);
    }
    fclose(f);
    return 0;
}

static void unpack(uint32_t c, float out[4])
{
    out[0] = (float)((c >> 24) & 255) / 255.0f;
    out[1] = (float)((c >> 16) & 255) / 255.0f;
    out[2] = (float)((c >> 8) & 255) / 255.0f;
    out[3] = (float)(c & 255) / 255.0f;
}

static void sample(const SoftTex *t, float u, float v, float out[4])
{
    float x = u * (float)t->w - 0.5f, y = v * (float)t->h - 0.5f;
    int x0 = (int)floorf(x), y0 = (int)floorf(y), k;
    float fx = x - (float)x0, fy = y - (float)y0;
    int xs[2] = { x0, x0 + 1 }, ys[2] = { y0, y0 + 1 };
    float acc[4] = { 0, 0, 0, 0 };
    for (k = 0; k < 4; k++) {
        int xi = xs[k & 1], yi = ys[k >> 1], c;
        float wgt = ((k & 1) ? fx : 1.0f - fx) * ((k >> 1) ? fy : 1.0f - fy);
        const unsigned char *p;
        if (xi < 0) xi = 0;
        if (xi >= t->w) xi = t->w - 1;
        if (yi < 0) yi = 0;
        if (yi >= t->h) yi = t->h - 1;
        p = t->rgba + ((size_t)yi * (size_t)t->w + (size_t)xi) * 4u;
        for (c = 0; c < 4; c++) acc[c] += wgt * (float)p[c] / 255.0f;
    }
    memcpy(out, acc, sizeof(acc));
}

static float edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

/* Tie-break for pixels exactly on an edge: antisymmetric in edge direction,
 * so an edge shared by two triangles is owned by exactly one (like the GPU's
 * top-left rule). Avoids double-blended seams in translucent fans. */
static int owns(float w, float dx, float dy)
{
    if (w > 0.0f) return 1;
    if (w < 0.0f) return 0;
    return dy > 0.0f || (dy == 0.0f && dx < 0.0f);
}

static void raster(const SV *a, const SV *b, const SV *c, int textured)
{
    float minx = fminf(a->x, fminf(b->x, c->x)), maxx = fmaxf(a->x, fmaxf(b->x, c->x));
    float miny = fminf(a->y, fminf(b->y, c->y)), maxy = fmaxf(a->y, fmaxf(b->y, c->y));
    float area = edge(a->x, a->y, b->x, b->y, c->x, c->y);
    int x0, x1, y0, y1, x, y;
    const SoftTex *t = textured ? &g_tex[g_curtex] : NULL;
    if (fabsf(area) < 1e-8f) return;
    if (area < 0.0f) { const SV *tmp = b; b = c; c = tmp; area = -area; }
    if (textured && (!t->rgba)) return;
    x0 = (int)floorf(minx); x1 = (int)ceilf(maxx);
    y0 = (int)floorf(miny); y1 = (int)ceilf(maxy);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_pw - 1) x1 = g_pw - 1;
    if (y1 > g_ph - 1) y1 = g_ph - 1;
    for (y = y0; y <= y1; y++) {
        for (x = x0; x <= x1; x++) {
            float px = (float)x + 0.5f, py = (float)y + 0.5f;
            float e0 = edge(b->x, b->y, c->x, c->y, px, py);
            float e1 = edge(c->x, c->y, a->x, a->y, px, py);
            float e2 = edge(a->x, a->y, b->x, b->y, px, py);
            float w0 = e0 / area, w1 = e1 / area, w2 = e2 / area;
            float col[4], al;
            float *d;
            int k;
            if (!owns(e0, c->x - b->x, c->y - b->y) || !owns(e1, a->x - c->x, a->y - c->y) ||
                !owns(e2, b->x - a->x, b->y - a->y)) continue;
            for (k = 0; k < 4; k++) col[k] = a->c[k] * w0 + b->c[k] * w1 + c->c[k] * w2;
            if (t) {
                float tc[4];
                sample(t, a->u * w0 + b->u * w1 + c->u * w2, a->v * w0 + b->v * w1 + c->v * w2, tc);
                for (k = 0; k < 4; k++) col[k] *= tc[k];
            }
            al = col[3];
            if (al <= 0.0f) continue;
            if (al > 1.0f) al = 1.0f;
            d = g_fb + ((size_t)y * (size_t)g_pw + (size_t)x) * 3u;
            for (k = 0; k < 3; k++) d[k] = col[k] * al + d[k] * (1.0f - al);
        }
    }
}

void r_tris_begin(void) { g_mode = 0; g_nv = 0; }
void r_vtx(float x, float y, uint32_t rgba)
{
    SV *v = &g_v[g_nv++];
    g_soft_vertex_count++;
    v->x = x * g_sx; v->y = y * g_sy; v->u = v->v = 0.0f;
    unpack(rgba, v->c);
    if (g_nv == 3) { raster(&g_v[0], &g_v[1], &g_v[2], 0); g_nv = 0; }
}
void r_tris_end(void) { g_nv = 0; }

void r_quads_begin(int tex) { g_mode = 1; g_nv = 0; g_curtex = (tex >= 0 && tex < TEX_COUNT_ALL) ? tex : 0; }
void r_qvtx(float x, float y, float u, float v, uint32_t rgba)
{
    SV *p = &g_v[g_nv++];
    g_soft_vertex_count++;
    p->x = x * g_sx; p->y = y * g_sy; p->u = u; p->v = v;
    unpack(rgba, p->c);
    if (g_nv == 4) {
        raster(&g_v[0], &g_v[1], &g_v[2], 1);
        raster(&g_v[0], &g_v[2], &g_v[3], 1);
        g_nv = 0;
    }
}
void r_quads_end(void) { g_nv = 0; }

int r_texture_upload_alpha(int tex, int w, int h, const unsigned char *alpha)
{
    size_t i, n = (size_t)w * (size_t)h;
    SoftTex *t;
    if (tex < 0 || tex >= TEX_COUNT_FIXED) return -1;
    t = &g_tex[tex];
    free(t->rgba);
    t->rgba = (unsigned char *)malloc(n * 4u);
    if (!t->rgba) return -1;
    t->w = w; t->h = h;
    for (i = 0; i < n; i++) {
        t->rgba[i * 4 + 0] = t->rgba[i * 4 + 1] = t->rgba[i * 4 + 2] = 255;
        t->rgba[i * 4 + 3] = alpha[i];
    }
    return 0;
}

int r_texture_upload_rgba(int tex, int w, int h, const unsigned char *rgba)
{
    size_t n = (size_t)w * (size_t)h * 4u;
    SoftTex *t;
    if (tex < 0 || tex >= TEX_COUNT_FIXED) return -1;
    t = &g_tex[tex];
    free(t->rgba);
    t->rgba = (unsigned char *)malloc(n);
    if (!t->rgba) return -1;
    memcpy(t->rgba, rgba, n);
    t->w = w; t->h = h;
    return 0;
}

uint16_t *r_cover_pixels(int slot)
{
    if (slot < 0 || slot >= R_COVER_SLOTS) return NULL;
    return g_cover_mem[slot];
}

/* A1R5G5B5 -> RGBA copy (the preview has no 16-bit textures) */
void r_cover_define(int slot, int w, int h)
{
    SoftTex *t;
    int i, n = w * h;
    if (slot < 0 || slot >= R_COVER_SLOTS || w <= 0 || h <= 0 || w * h * 2 > R_COVER_SLOT_BYTES) return;
    t = &g_tex[TEX_COVER0 + slot];
    free(t->rgba);
    t->rgba = (unsigned char *)malloc((size_t)n * 4u);
    if (!t->rgba) return;
    t->w = w; t->h = h;
    for (i = 0; i < n; i++) {
        uint16_t p = g_cover_mem[slot][i];
        int r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
        t->rgba[i * 4 + 0] = (unsigned char)((r << 3) | (r >> 2));
        t->rgba[i * 4 + 1] = (unsigned char)((g << 3) | (g >> 2));
        t->rgba[i * 4 + 2] = (unsigned char)((b << 3) | (b >> 2));
        t->rgba[i * 4 + 3] = (p & 0x8000) ? 255 : 0;
    }
}
