/* render.h - minimal primitive backend. Implemented by:
 *   src/ps3/render_tiny3d.c        (Tiny3D on PS3)
 *   tools/preview/render_soft.c    (software rasterizer for host PNG previews)
 * All coordinates are LOGICAL screen units (see Screen in layout.h).
 * Colors are 0xRRGGBBAA. */
#ifndef SF_RENDER_H
#define SF_RENDER_H

#include <stdint.h>

enum {
    TEX_FONT_MAIN = 0, TEX_FONT_BIG = 1, TEX_ICONS = 2,
    TEX_TAB_PS1 = 3, TEX_TAB_PS2 = 4, TEX_TAB_PS3 = 5,   /* optional user tab images */
    TEX_BLOB = 6,   /* generated: soft radial falloff (glows, background shapes) */
    TEX_DISC = 7,   /* generated: anti-aliased disc (9-slice rounded rectangles) */
    TEX_CATS = 8,   /* kitten artwork on the loading screen (cats_data.c) */
    TEX_CASE_PS1 = 9, TEX_CASE_PS2 = 10, TEX_CASE_PS3 = 11,   /* optional case images (the user's) */
    TEX_BG_IMAGE = 12,   /* the user's background picture (one at a time, 1280x720) */
    TEX_STAGE = 13, TEX_STAGE_SHAPES = 14,   /* built-in Stage background (brightness) + its floating shapes */
    TEX_STAGE_HI = 15,                        /* Stage's white-hot cores */
    TEX_STAGE_FX = 16,                        /* Stage's ribbons, flare and stars (brightness) */
    TEX_STAGE_RIB = 17,                       /* Stage's light ribbons */
    TEX_STAGE_GLOW = 18,                      /* Stage's glow halos */
    TEX_STAGE_B = 19, TEX_STAGE_RIB_B = 20,   /* 0.8.5: the Stage composites in the new colour while they crossfade */
    TEX_STAGE_BLUR = 21,                      /* 0.8.5: a small, blurred copy of the Stage (frosted panels) */
    TEX_BG_BLUR = 22,                         /* 0.8.5: a small, blurred copy of the user's picture */
    TEX_STAGE_BLUR_B = 23,                    /* 0.9.5: the blurred copy in the new colour, while they crossfade */
    TEX_LIVE0 = 24, TEX_LIVE1 = 25,           /* 0.9.6: r_snapshot: what is on screen, small and blurred */
    TEX_COUNT_FIXED = 26
};

/* Cover art: R_COVER_SLOTS textures TEX_COVER0 + slot, one per cover cache
 * slot, each R_COVER_SLOT_BYTES of A1R5G5B5 pixels (16-bit, top bit = opaque,
 * native byte order) allocated once by the backend. The cover worker thread
 * writes a slot's pixels only while that slot is not being drawn. */
#define R_COVER_SLOTS       64
#define R_COVER_MAX_W       384
#define R_COVER_MAX_H       448
#define R_COVER_SLOT_BYTES  (R_COVER_MAX_W * R_COVER_MAX_H * 2)
#define TEX_COVER0          TEX_COUNT_FIXED
#define TEX_COUNT_ALL       (TEX_COVER0 + R_COVER_SLOTS)

/* Untextured, per-vertex colored triangles. Emit vertices in groups of 3. */
void r_tris_begin(void);
void r_vtx(float x, float y, uint32_t rgba);
void r_tris_end(void);
/* 0.9.1: a triangle strip (vertices with r_vtx, closed with r_tris_end): each
 * vertex after the second makes a triangle with the two before it */
void r_strip_begin(void);

/* Textured quads (texture is white RGB + alpha; vertex color modulates).
 * Emit vertices in groups of 4: TL, TR, BR, BL. */
void r_quads_begin(int tex);
void r_qvtx(float x, float y, float u, float v, uint32_t rgba);
void r_quads_end(void);

/* Upload an alpha-only atlas as a white texture into texture slot `tex`.
 * Returns 0 on success. */
int r_texture_upload_alpha(int tex, int w, int h, const unsigned char *alpha);
/* Upload an RGBA (byte order R,G,B,A) image into texture slot `tex`. */
int r_texture_upload_rgba(int tex, int w, int h, const unsigned char *rgba);
/* Replace a texture's pixels (in place when the size is the same; used when
 * the Stage composite is redone for another UI color). */
int r_texture_update_rgba(int tex, int w, int h, const unsigned char *rgba);
/* 0.8.9: rows y0..y1 of the same w x h picture (rgba is all of it), so a big
 * update can be spread over frames. 0 = done, 1 = it had to make the texture
 * and wrote the whole picture, -1 = no memory. */
int r_texture_update_rgba_rows(int tex, int w, int h, int y0, int y1, const unsigned char *rgba);
/* Blending on (default) / off for the next batches: an opaque full-screen
 * layer drawn without blending costs the RSX much less. */
void r_set_blend(int on);
/* 0.9.6: frosted glass. Copies what has been drawn so far this frame into
 * texture TEX_LIVE0 + slot (slot 0 or 1), shrunk and blurred, covering the
 * whole screen (u = x / width, v = y / height). Returns that texture, or -1
 * when the backend can't (the caller draws its old frost instead). */
int r_snapshot(int slot);

/* Pixel memory of cover slot `slot` (R_COVER_SLOT_BYTES), NULL if the backend
 * has none. Width*2 must be a multiple of 64 bytes (RSX pitch). */
uint16_t *r_cover_pixels(int slot);
/* Main thread: texture TEX_COVER0 + slot now holds a w x h image. */
void r_cover_define(int slot, int w, int h);

#endif
