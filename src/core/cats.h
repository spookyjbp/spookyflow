/* cats.h - kitten artwork for the loading screen (generated pixels). */
#ifndef SF_CATS_H
#define SF_CATS_H
extern const int g_cats_w, g_cats_h;
extern const unsigned char g_cats_rgba[];   /* g_cats_w * g_cats_h * 4, RGBA */
/* the kittens drawn in light (0.7.3): outlines of that picture, traced by
 * tools/gen_cats.py. Outline i is points g_kitline_start[i] .. [i+1]-1, in
 * texture coordinates (u, v: 0..1); kind 0 = silhouette, 1 = where the white
 * fur meets the black, 2 = an eye. */
extern const int g_kitline_count;
extern const int g_kitline_start[];
extern const unsigned char g_kitline_kind[];
extern const float g_kitline_uv[][2];
#endif
