/* util.c - small portable helpers. */
#include "util.h"
#include <stdio.h>
#include <string.h>

size_t sf_strlcpy(char *dst, const char *src, size_t cap)
{
    size_t n = 0;
    if (cap == 0) return 0;
    if (src) {
        while (src[n] && n + 1 < cap) { dst[n] = src[n]; n++; }
    }
    dst[n] = '\0';
    return n;
}

size_t sf_strlcat(char *dst, const char *src, size_t cap)
{
    size_t len = 0;
    if (cap == 0) return 0;
    while (len < cap && dst[len]) len++;
    if (len >= cap) { dst[cap - 1] = '\0'; return cap - 1; }
    return len + sf_strlcpy(dst + len, src, cap - len);
}

char sf_tolower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }
char sf_toupper(char c) { return (c >= 'a' && c <= 'z') ? (char)(c - 'a' + 'A') : c; }
int sf_isalpha(char c)  { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
int sf_isdigit(char c)  { return c >= '0' && c <= '9'; }
int sf_isspace(char c)  { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

int sf_stricmp(const char *a, const char *b)
{
    for (;; a++, b++) {
        int ca = (unsigned char)sf_tolower(*a), cb = (unsigned char)sf_tolower(*b);
        if (ca != cb || ca == 0) return ca - cb;
    }
}

int sf_strnicmp(const char *a, const char *b, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) {
        int ca = (unsigned char)sf_tolower(a[i]), cb = (unsigned char)sf_tolower(b[i]);
        if (ca != cb || ca == 0) return ca - cb;
    }
    return 0;
}

int sf_ends_with_i(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lx = strlen(suffix);
    if (lx > ls) return 0;
    return sf_stricmp(s + ls - lx, suffix) == 0;
}

uint32_t sf_hash(const char *s)
{
    uint32_t h = 2166136261u;
    while (*s) { h ^= (unsigned char)*s++; h *= 16777619u; }
    return h;
}

uint32_t sf_utf8_next(const char **ps)
{
    const unsigned char *s = (const unsigned char *)*ps;
    uint32_t c = s[0];
    if (c < 0x80) { *ps += 1; return c; }
    if ((c & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        *ps += 2; return ((c & 0x1F) << 6) | (s[1] & 0x3F);
    }
    if ((c & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        *ps += 3; return ((c & 0x0F) << 12) | ((uint32_t)(s[1] & 0x3F) << 6) | (s[2] & 0x3F);
    }
    if ((c & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *ps += 4;
        return ((c & 0x07) << 18) | ((uint32_t)(s[1] & 0x3F) << 12) | ((uint32_t)(s[2] & 0x3F) << 6) | (s[3] & 0x3F);
    }
    *ps += 1;           /* invalid: treat as Latin-1 byte */
    return c;
}

void sf_format_size(uint64_t bytes, char *out, size_t cap)
{
    const double gb = 1024.0 * 1024.0 * 1024.0, mb = 1024.0 * 1024.0;
    if (bytes == 0) { if (cap) out[0] = '\0'; return; }
    if ((double)bytes >= gb) snprintf(out, cap, "%.1f GB", (double)bytes / gb);
    else if ((double)bytes >= mb) snprintf(out, cap, "%d MB", (int)((double)bytes / mb + 0.5));
    else snprintf(out, cap, "%d KB", (int)((bytes + 1023) / 1024));
}

uint32_t sf_col_alpha(uint32_t c, float f)
{
    int a = (int)((float)(c & 255) * sf_clampf(f, 0.0f, 1.0f) + 0.5f);
    return (c & 0xFFFFFF00u) | (uint32_t)a;
}

uint32_t sf_col_dim(uint32_t c, float f)
{
    f = sf_clampf(f, 0.0f, 1.0f);
    int r = (int)((float)((c >> 24) & 255) * f + 0.5f);
    int g = (int)((float)((c >> 16) & 255) * f + 0.5f);
    int b = (int)((float)((c >> 8) & 255) * f + 0.5f);
    return sf_rgba(r, g, b, (int)(c & 255));
}

uint32_t sf_col_mix(uint32_t a, uint32_t b, float t)
{
    t = sf_clampf(t, 0.0f, 1.0f);
    int out[4], i;
    for (i = 0; i < 4; i++) {
        int sh = 24 - 8 * i;
        float va = (float)((a >> sh) & 255), vb = (float)((b >> sh) & 255);
        out[i] = (int)(va + (vb - va) * t + 0.5f);
    }
    return sf_rgba(out[0], out[1], out[2], out[3]);
}

uint32_t sf_hsv(float h, float s, float v, int a)
{
    float r = v, g = v, b = v;
    h = h - (float)(int)h;
    if (h < 0) h += 1.0f;
    if (s > 0.0f) {
        float hf = h * 6.0f;
        int i = (int)hf;
        float f = hf - (float)i;
        float p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
        switch (i % 6) {
        case 0: r = v; g = t; b = p; break;
        case 1: r = q; g = v; b = p; break;
        case 2: r = p; g = v; b = t; break;
        case 3: r = p; g = q; b = v; break;
        case 4: r = t; g = p; b = v; break;
        default: r = v; g = p; b = q; break;
        }
    }
    return sf_rgba((int)(r * 255.0f + 0.5f), (int)(g * 255.0f + 0.5f), (int)(b * 255.0f + 0.5f), a);
}
