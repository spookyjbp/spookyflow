/* font.c - glyph lookup and RLE atlas decode. */
#include "font.h"

const FontGlyph *font_find(const Font *f, uint32_t cp)
{
    int lo = 0, hi = f->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint32_t c = f->glyphs[mid].cp;
        if (c == cp) return &f->glyphs[mid];
        if (c < cp) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

const FontGlyph *font_glyph(const Font *f, uint32_t cp)
{
    const FontGlyph *g = font_find(f, cp);
    if (!g && cp >= 'a' && cp <= 'z') g = font_find(f, cp - 32);   /* big font: upper fallback */
    if (!g) g = font_find(f, '?');
    if (!g) g = &f->glyphs[0];
    return g;
}

int sf_rle_decode(const unsigned char *in, unsigned in_len, unsigned char *out, unsigned out_size)
{
    unsigned i = 0, o = 0;
    while (i < in_len) {
        unsigned n = in[i++];
        if (n < 128) {
            unsigned k, cnt = n + 1;
            if (i >= in_len || o + cnt > out_size) return -1;
            for (k = 0; k < cnt; k++) out[o++] = in[i];
            i++;
        } else {
            unsigned k, cnt = n - 127;
            if (i + cnt > in_len || o + cnt > out_size) return -1;
            for (k = 0; k < cnt; k++) out[o++] = in[i++];
        }
    }
    return o == out_size ? 0 : -1;
}

int font_decode_atlas(const Font *f, unsigned char *out, unsigned out_size)
{
    unsigned need = (unsigned)f->atlas_w * (unsigned)f->atlas_h;
    if (out_size < need) return -1;
    return sf_rle_decode(f->rle, f->rle_len, out, need);
}
