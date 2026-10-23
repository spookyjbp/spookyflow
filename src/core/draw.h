/* draw.h - anti-aliased shapes and text built on render.h primitives.
 * Distances called "r", "thick", "em" are in VERTICAL logical units; the
 * horizontal extent is divided by the pixel aspect so circles stay round on
 * any screen mapping (PS3 Tiny3D uses an 848x512 virtual space stretched to
 * 16:9 or 4:3). */
#ifndef SF_DRAW_H
#define SF_DRAW_H

#include <stdint.h>
#include "font.h"

typedef struct {
    float W, H;   /* logical size */
    float pa;     /* pixel aspect: physical width of 1 logical x unit / physical height of 1 logical y unit */
    float px;     /* physical pixels per logical y unit (for 1px AA feathering) */
} Screen;

enum { ALIGN_LEFT = 0, ALIGN_CENTER = 1, ALIGN_RIGHT = 2 };

void d_set_screen(const Screen *s);
const Screen *d_screen(void);

/* Convert a vertical-unit length to logical x units. */
float d_xu(float v);

void d_rect(float x, float y, float w, float h, uint32_t col);
void d_rect_grad(float x, float y, float w, float h, uint32_t top, uint32_t bot);
/* Horizontal gradient: left color -> right color. */
void d_rect_hgrad(float x, float y, float w, float h, uint32_t left, uint32_t right);
void d_rrect(float x, float y, float w, float h, float r, uint32_t col);
void d_rrect_grad(float x, float y, float w, float h, float r, uint32_t top, uint32_t bot);
void d_rrect_stroke(float x, float y, float w, float h, float r, float thick, uint32_t col);
/* Soft glow around a rounded rect: alpha col at the edge falling to 0 at `spread` outside. */
void d_rrect_glow(float x, float y, float w, float h, float r, float spread, uint32_t col);
void d_disc(float cx, float cy, float r, uint32_t col);
/* Soft radial blob: col in the middle fading to transparent at r. */
void d_blob(float cx, float cy, float r, uint32_t col);
/* Soft ellipse: rx is a physical (not logical) radius like r in d_blob. */
void d_blob_ellipse(float cx, float cy, float rx, float ry, uint32_t col);
/* Arc stroke from angle a0 to a1 (radians, 0 = up, clockwise). */
void d_arc(float cx, float cy, float r, float thick, float a0, float a1, uint32_t col);
void d_line(float x0, float y0, float x1, float y1, float thick, uint32_t col);

/* Text. y is the vertical CENTER of capital letters. em = font size.
 * Returns the advance width in logical x units. */
float d_text(const Font *f, float x, float y, float em, uint32_t col, const char *s, int align);
float d_text_width(const Font *f, float em, const char *s);
/* Draw text truncated with an ellipsis to fit maxw. Returns drawn width. */
float d_text_fit(const Font *f, float x, float y, float em, uint32_t col, const char *s, int align, float maxw);
/* Word-wrapped, centered block around (cx, cy). Up to maxlines lines. */
void d_text_box(const Font *f, float cx, float cy, float maxw, float em, int maxlines, uint32_t col, const char *s);

/* Pre-rendered button badge (icons.h ICON_*), size = badge HEIGHT in
 * vertical units (shoulder badges are wider, see icons_aspect). tint multiplies the badge (use 0xFFFFFFFF, or alpha). */
void d_badge(int icon, float cx, float cy, float size, uint32_t tint);

/* Whole-texture quad. */
void d_image(int tex, float x, float y, float w, float h, uint32_t tint);
/* Part of a texture (v0..v1 down the quad; v0 > v1 flips it) with a
 * top-to-bottom tint gradient. */
/* Rotated sub-image: cell u0..u1 of the texture, centered at (cx, cy), size
 * w x h physical units (h vertical), angle in radians. */
void d_image_rot(int tex, float cx, float cy, float w, float h, float angle, float u0, float u1, uint32_t tint);
void d_image_v(int tex, float x, float y, float w, float h, float v0, float v1, uint32_t top, uint32_t bot);

/* Fill TEX_BLOB / TEX_DISC alpha data (size x size) into buf. */
#define D_SHAPE_TEX 128
void d_gen_blob_alpha(unsigned char *buf, int size);
void d_gen_disc_alpha(unsigned char *buf, int size);

#endif
