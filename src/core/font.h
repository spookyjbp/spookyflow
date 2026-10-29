/* font.h - bitmap glyph atlases (generated into font_data.c). */
#ifndef SF_FONT_H
#define SF_FONT_H

#include <stdint.h>

typedef struct {
    uint16_t cp;        /* Unicode code point */
    uint16_t x, y;      /* top-left in atlas (px) */
    uint16_t w, h;      /* glyph bitmap size (px) */
    int16_t  bx, by;    /* offset of bitmap top-left from pen/baseline (px) */
    float    adv;       /* horizontal advance (px) */
} FontGlyph;

typedef struct {
    const FontGlyph *glyphs;   /* sorted by cp */
    int count;
    int px;                    /* em size the atlas was rendered at */
    int ascent, descent, cap;  /* metrics in px */
    int atlas_w, atlas_h;
    const unsigned char *rle;
    unsigned rle_len;
} Font;

extern const Font g_font_main;
extern const Font g_font_big;

/* Find glyph (binary search). Returns NULL if missing. */
const FontGlyph *font_find(const Font *f, uint32_t cp);
/* Glyph for cp, falling back to '?' then the first glyph. Never NULL. */
const FontGlyph *font_glyph(const Font *f, uint32_t cp);

/* Decode the atlas alpha into out (atlas_w*atlas_h bytes). Returns 0 on
 * success, -1 if the RLE stream is malformed or out is too small. */
int font_decode_atlas(const Font *f, unsigned char *out, unsigned out_size);
/* Generic RLE decoder used by the font (exposed for tests). */
int sf_rle_decode(const unsigned char *in, unsigned in_len, unsigned char *out, unsigned out_size);

#endif
