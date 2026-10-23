/* icons.h - pre-rendered button badge atlas (tools/gen_icons.py). */
#ifndef SF_ICONS_H
#define SF_ICONS_H

enum {
    ICON_CROSS = 0, ICON_TRIANGLE = 1, ICON_CIRCLE = 2, ICON_START = 3,
    ICON_L1 = 4, ICON_R1 = 5, ICON_L2 = 6, ICON_R2 = 7, ICON_COUNT = 8
};

typedef struct { int x, w; } IconRect;   /* horizontal extent in the atlas; height = atlas h */

typedef struct {
    int w, h;          /* atlas size */
    int count;
    const IconRect *rects;
    const unsigned char *rle;   /* planar R,G,B,A */
    unsigned rle_len;
} IconAtlas;

extern const IconAtlas g_icons;

/* Decode to interleaved RGBA (w*h*4 bytes). scratch must hold w*h*4 bytes
 * too (planar staging). Returns 0 on success. */
/* Width/height ratio of a badge (1.0 for round ones). */
float icons_aspect(int icon);

int icons_decode_rgba(unsigned char *out_rgba, unsigned char *scratch, unsigned size);

#endif
