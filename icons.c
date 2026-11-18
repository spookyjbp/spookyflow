/* icons.c - decode the planar RLE badge atlas. */
#include "icons.h"
#include "font.h"   /* sf_rle_decode */

int icons_decode_rgba(unsigned char *out_rgba, unsigned char *scratch, unsigned size)
{
    unsigned n = (unsigned)g_icons.w * (unsigned)g_icons.h, i;
    if (size < n * 4u) return -1;
    if (sf_rle_decode(g_icons.rle, g_icons.rle_len, scratch, n * 4u) != 0) return -1;
    for (i = 0; i < n; i++) {
        out_rgba[i * 4 + 0] = scratch[i];
        out_rgba[i * 4 + 1] = scratch[n + i];
        out_rgba[i * 4 + 2] = scratch[2 * n + i];
        out_rgba[i * 4 + 3] = scratch[3 * n + i];
    }
    return 0;
}

float icons_aspect(int icon)
{
    if (icon < 0 || icon >= g_icons.count) return 1.0f;
    return (float)g_icons.rects[icon].w / (float)g_icons.h;
}
