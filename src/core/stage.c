/* stage.c - helpers for the built-in Stage background. */
#include "stage.h"

void stage_highlight(const unsigned char *v, int n, unsigned char *out)
{
    int i;
    for (i = 0; i < n; i++) {
        int x = (int)v[i] - 150;
        if (x <= 0) { out[i] = 0; continue; }
        /* (x / 105)^2, so only the hottest cores turn white */
        x = x * x * 255 / (105 * 105);
        out[i] = (unsigned char)(x > 255 ? 255 : x);
    }
}
