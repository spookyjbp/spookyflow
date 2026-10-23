/* render_tiny3d.c - render.h implemented with Tiny3D (2D mode).
 *
 * Verified against wargio/tiny3D lib/include/tiny3d.h + samples/sprites2D:
 *   tiny3d_SetPolygon(TINY3D_TRIANGLES / TINY3D_QUADS), tiny3d_VertexPos(x,y,z),
 *   tiny3d_VertexColor(u32 0xRRGGBBAA), tiny3d_VertexTexture(u,v), tiny3d_End(),
 *   tiny3d_SetTexture(unit, offset, w, h, stride, TINY3D_TEX_FORMAT_A8R8G8B8, TEXTURE_LINEAR),
 *   tiny3d_AllocTexture(size), tiny3d_TextureOffset(ptr).
 * Per-vertex order Pos -> Color -> Texture (VertexPos commits the previous vertex).
 *
 * Depth: tiny3d_Clear enables depth test with LESS-OR-EQUAL. Every batch gets
 * a slightly smaller z than the previous one, so later draws always pass and
 * painter's order is preserved. */
#include <string.h>
#include <tiny3d.h>
#include "render.h"
#include "ps3_platform.h"

typedef struct { u32 offset; int w, h, valid; u32 fmt, bpp; } T3Tex;

static T3Tex g_tex[TEX_COUNT_ALL];
static u16 *g_cover_mem[R_COVER_SLOTS];   /* RSX memory, R_COVER_SLOT_BYTES each */
static u8 *g_pool;
static u32 g_pool_size, g_pool_used;
static float g_z = 65000.0f;
static int g_pending_type = -1;   /* polygon type to open on first vertex, -1 = none */
static int g_open;                /* SetPolygon issued for this batch */
static int g_cur_tex = -1;

int ps3_render_init(u32 pool_bytes)
{
    g_pool = (u8 *)tiny3d_AllocTexture(pool_bytes);
    if (!g_pool) return -1;
    g_pool_size = pool_bytes;
    g_pool_used = 0;
    memset(g_tex, 0, sizeof(g_tex));
    memset(g_cover_mem, 0, sizeof(g_cover_mem));
    return 0;
}

/* Cover textures: one block for all slots, allocated separately from the UI
 * pool so a failure here only costs the covers. 16-bit A1R5G5B5 (alpha bit
 * set = opaque) halves the memory of A8R8G8B8: 32 x 336 KB = 10.5 MB. */
int ps3_render_init_covers(void)
{
    u8 *mem = (u8 *)tiny3d_AllocTexture((u32)R_COVER_SLOTS * (u32)R_COVER_SLOT_BYTES);
    int i;
    if (!mem) return -1;
    for (i = 0; i < R_COVER_SLOTS; i++) {
        g_cover_mem[i] = (u16 *)(mem + (size_t)i * R_COVER_SLOT_BYTES);   /* 336 KB: keeps 128-byte alignment */
        memset(g_cover_mem[i], 0, R_COVER_SLOT_BYTES);
    }
    return 0;
}

uint16_t *r_cover_pixels(int slot)
{
    if (slot < 0 || slot >= R_COVER_SLOTS) return NULL;
    return (uint16_t *)g_cover_mem[slot];
}

void r_cover_define(int slot, int w, int h)
{
    T3Tex *t;
    if (slot < 0 || slot >= R_COVER_SLOTS || !g_cover_mem[slot] || w <= 0 || h <= 0 ||
        w * h * 2 > R_COVER_SLOT_BYTES) return;
    t = &g_tex[TEX_COVER0 + slot];
    t->offset = tiny3d_TextureOffset(g_cover_mem[slot]);
    t->w = w; t->h = h;
    t->fmt = TINY3D_TEX_FORMAT_A1R5G5B5;
    t->bpp = 2;
    t->valid = 1;
}

void ps3_render_frame_begin(void) { g_z = 65000.0f; }

static float next_z(void)
{
    if (g_z > 2.0f) g_z -= 1.0f;
    return g_z;
}

static u32 *pool_take(u32 bytes)
{
    u32 *p;
    bytes = (bytes + 127u) & ~127u;               /* keep textures 128-byte aligned */
    if (g_pool_used + bytes > g_pool_size) return NULL;
    p = (u32 *)(g_pool + g_pool_used);
    g_pool_used += bytes;
    return p;
}

int r_texture_upload_alpha(int tex, int w, int h, const unsigned char *alpha)
{
    u32 *dst;
    int i, n = w * h;
    if (tex < 0 || tex >= TEX_COUNT_FIXED || w <= 0 || h <= 0) return -1;
    dst = pool_take((u32)n * 4u);
    if (!dst) return -1;
    for (i = 0; i < n; i++) dst[i] = ((u32)alpha[i] << 24) | 0x00FFFFFFu;   /* A8R8G8B8, white */
    g_tex[tex].offset = tiny3d_TextureOffset(dst);
    g_tex[tex].w = w; g_tex[tex].h = h; g_tex[tex].valid = 1;
    g_tex[tex].fmt = TINY3D_TEX_FORMAT_A8R8G8B8; g_tex[tex].bpp = 4;
    return 0;
}

int r_texture_upload_rgba(int tex, int w, int h, const unsigned char *rgba)
{
    u32 *dst;
    int i, n = w * h;
    if (tex < 0 || tex >= TEX_COUNT_FIXED || w <= 0 || h <= 0) return -1;
    dst = pool_take((u32)n * 4u);
    if (!dst) return -1;
    for (i = 0; i < n; i++) {
        const unsigned char *p = rgba + (size_t)i * 4u;
        dst[i] = ((u32)p[3] << 24) | ((u32)p[0] << 16) | ((u32)p[1] << 8) | (u32)p[2];
    }
    g_tex[tex].offset = tiny3d_TextureOffset(dst);
    g_tex[tex].w = w; g_tex[tex].h = h; g_tex[tex].valid = 1;
    g_tex[tex].fmt = TINY3D_TEX_FORMAT_A8R8G8B8; g_tex[tex].bpp = 4;
    return 0;
}

/* Polygons are opened lazily on the first vertex so empty batches (e.g. an
 * empty string) never reach Tiny3D. */
static void open_if_needed(void)
{
    if (g_open || g_pending_type < 0) return;
    if (g_cur_tex >= 0) {
        const T3Tex *t = &g_tex[g_cur_tex];
        /* CLAMP, not tiny3d_SetTexture's REPEAT: with repeat, filtering at an
         * image's edge blends in the opposite edge, which drew thin lines
         * across trimmed tab logos (tiny3d.h: tiny3d_SetTextureWrap, TEXTWRAP_CLAMP) */
        tiny3d_SetTextureWrap(0, t->offset, (u32)t->w, (u32)t->h, (u32)t->w * t->bpp, (text_format)t->fmt,
                              TEXTWRAP_CLAMP, TEXTWRAP_CLAMP, TEXTURE_LINEAR);
    }
    tiny3d_SetPolygon((type_polygon)g_pending_type);
    g_open = 1;
}

void r_tris_begin(void)
{
    g_pending_type = TINY3D_TRIANGLES;
    g_cur_tex = -1;
    g_open = 0;
    next_z();
}

void r_vtx(float x, float y, uint32_t rgba)
{
    open_if_needed();
    tiny3d_VertexPos(x, y, g_z);
    tiny3d_VertexColor(rgba);
}

void r_tris_end(void)
{
    if (g_open) tiny3d_End();
    g_open = 0;
    g_pending_type = -1;
}

void r_quads_begin(int tex)
{
    g_open = 0;
    if (tex < 0 || tex >= TEX_COUNT_ALL || !g_tex[tex].valid) { g_pending_type = -1; return; }
    g_pending_type = TINY3D_QUADS;
    g_cur_tex = tex;
    next_z();
}

void r_qvtx(float x, float y, float u, float v, uint32_t rgba)
{
    if (g_pending_type < 0) return;      /* texture missing: skip silently */
    open_if_needed();
    tiny3d_VertexPos(x, y, g_z);
    tiny3d_VertexColor(rgba);
    tiny3d_VertexTexture(u, v);
}

void r_quads_end(void)
{
    if (g_open) tiny3d_End();
    g_open = 0;
    g_pending_type = -1;
    g_cur_tex = -1;
}
