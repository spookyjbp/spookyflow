#include <string.h>
/* null_render.c - render.h backend that only counts calls (unit tests). */
#include "render.h"

long g_null_vertices, g_null_batches;

void r_tris_begin(void) { g_null_batches++; }
void r_vtx(float x, float y, uint32_t rgba) { (void)x; (void)y; (void)rgba; g_null_vertices++; }
void r_tris_end(void) { }
static int g_null_stage_mask;   /* which Stage composites were drawn (bit 0: TEX_STAGE, bit 1: TEX_STAGE_B) */
void r_quads_begin(int tex)
{
    g_null_batches++;
    if (tex == TEX_STAGE) g_null_stage_mask |= 1;
    if (tex == TEX_STAGE_B) g_null_stage_mask |= 2;
}
int g_null_bound_mask_stage(void) { int m = g_null_stage_mask; g_null_stage_mask = 0; return m; }
void r_qvtx(float x, float y, float u, float v, uint32_t rgba) { (void)x; (void)y; (void)u; (void)v; (void)rgba; g_null_vertices++; }
void r_quads_end(void) { }
int r_texture_upload_alpha(int tex, int w, int h, const unsigned char *alpha) { (void)tex; (void)w; (void)h; (void)alpha; return 0; }
int r_texture_upload_rgba(int tex, int w, int h, const unsigned char *rgba) { (void)tex; (void)w; (void)h; (void)rgba; return 0; }

static uint16_t g_null_cover[R_COVER_SLOTS][R_COVER_SLOT_BYTES / 2];
int g_null_cover_defined[R_COVER_SLOTS];
uint16_t *r_cover_pixels(int slot) { return (slot >= 0 && slot < R_COVER_SLOTS) ? g_null_cover[slot] : 0; }
void r_cover_define(int slot, int w, int h) { if (slot >= 0 && slot < R_COVER_SLOTS && w > 0 && h > 0) g_null_cover_defined[slot]++; }
int g_null_blend_off;
int g_null_updates, g_null_update_tex, g_null_update_w, g_null_update_h;
unsigned char g_null_update_px[4];
int r_texture_update_rgba(int tex, int w, int h, const unsigned char *rgba)
{
    g_null_updates++; g_null_update_tex = tex; g_null_update_w = w; g_null_update_h = h;
    memcpy(g_null_update_px, rgba, 4);
    return 0;
}
void r_set_blend(int on) { if (!on) g_null_blend_off++; }
