/* imgscale.c - see imgscale.h */
#include "imgscale.h"
#include <stddef.h>

void img_fit_size(int w, int h, float max_w, float max_h, int *ow, int *oh)
{
    float s = 1.0f;
    if (w <= 0 || h <= 0) { *ow = *oh = 0; return; }
    if (max_w > 0.0f && (float)w * s > max_w) s = max_w / (float)w;
    if (max_h > 0.0f && (float)h * s > max_h) s = max_h / (float)h;
    *ow = (int)((float)w * s + 0.5f);
    *oh = (int)((float)h * s + 0.5f);
    if (*ow < 1) *ow = 1;
    if (*oh < 1) *oh = 1;
}

void img_resample_rgba(const uint8_t *src, int w, int h, uint8_t *dst, int ow, int oh)
{
    int x, y, pass;
    float sx = (float)w / (float)ow, sy = (float)h / (float)oh;
    for (y = 0; y < oh; y++) {
        float y0 = (float)y * sy, y1 = y0 + sy;
        for (x = 0; x < ow; x++) {
            float x0 = (float)x * sx, x1 = x0 + sx;
            float acc[4] = { 0, 0, 0, 0 }, area = 0.0f;
            int ix, iy;
            for (iy = (int)y0; iy < h && (float)iy < y1; iy++) {
                float wy = ((float)(iy + 1) < y1 ? (float)(iy + 1) : y1) - ((float)iy > y0 ? (float)iy : y0);
                if (wy <= 0.0f) continue;
                for (ix = (int)x0; ix < w && (float)ix < x1; ix++) {
                    const uint8_t *p = src + ((size_t)iy * (size_t)w + (size_t)ix) * 4u;
                    float wx = ((float)(ix + 1) < x1 ? (float)(ix + 1) : x1) - ((float)ix > x0 ? (float)ix : x0);
                    float wgt, a;
                    if (wx <= 0.0f) continue;
                    wgt = wx * wy;
                    a = (float)p[3] / 255.0f;
                    acc[0] += (float)p[0] * a * wgt;
                    acc[1] += (float)p[1] * a * wgt;
                    acc[2] += (float)p[2] * a * wgt;
                    acc[3] += a * wgt;
                    area += wgt;
                }
            }
            {
                uint8_t *d = dst + ((size_t)y * (size_t)ow + (size_t)x) * 4u;
                float a = area > 0.0f ? acc[3] / area : 0.0f;
                if (acc[3] > 0.0f) {
                    d[0] = (uint8_t)(acc[0] / acc[3] + 0.5f);
                    d[1] = (uint8_t)(acc[1] / acc[3] + 0.5f);
                    d[2] = (uint8_t)(acc[2] / acc[3] + 0.5f);
                } else {
                    d[0] = d[1] = d[2] = 0;
                }
                d[3] = (uint8_t)(a * 255.0f + 0.5f);
            }
        }
    }
    /* bleed color into fully transparent pixels (a few passes is plenty for
     * the 1-2 pixel reach of bilinear filtering) */
    for (pass = 0; pass < 3; pass++) {
        int changed = 0;
        for (y = 0; y < oh; y++) {
            for (x = 0; x < ow; x++) {
                uint8_t *d = dst + ((size_t)y * (size_t)ow + (size_t)x) * 4u;
                int k, n = 0, r = 0, g = 0, b = 0;
                static const int dx[4] = { 1, -1, 0, 0 }, dy[4] = { 0, 0, 1, -1 };
                if (d[3] != 0 || (d[0] | d[1] | d[2]) != 0) continue;
                for (k = 0; k < 4; k++) {
                    int nx = x + dx[k], ny = y + dy[k];
                    const uint8_t *q;
                    if (nx < 0 || ny < 0 || nx >= ow || ny >= oh) continue;
                    q = dst + ((size_t)ny * (size_t)ow + (size_t)nx) * 4u;
                    if (q[3] == 0 && (q[0] | q[1] | q[2]) == 0) continue;
                    r += q[0]; g += q[1]; b += q[2]; n++;
                }
                if (n) {
                    d[0] = (uint8_t)(r / n); d[1] = (uint8_t)(g / n); d[2] = (uint8_t)(b / n);
                    if ((d[0] | d[1] | d[2]) == 0) d[0] = 1;   /* mark as filled */
                    changed = 1;
                }
            }
        }
        if (!changed) break;
    }
}

void img_pad_rgba(const uint8_t *src, int w, int h, int pad, uint8_t *dst)
{
    int W = w + 2 * pad, H = h + 2 * pad, x, y;
    for (y = 0; y < H; y++) {
        int sy = y - pad < 0 ? 0 : (y - pad >= h ? h - 1 : y - pad);
        for (x = 0; x < W; x++) {
            int sx = x - pad < 0 ? 0 : (x - pad >= w ? w - 1 : x - pad);
            const uint8_t *p = src + ((size_t)sy * (size_t)w + (size_t)sx) * 4u;
            uint8_t *d = dst + ((size_t)y * (size_t)W + (size_t)x) * 4u;
            int inside = x >= pad && x < pad + w && y >= pad && y < pad + h;
            d[0] = p[0]; d[1] = p[1]; d[2] = p[2];
            d[3] = inside ? p[3] : 0;
        }
    }
}
