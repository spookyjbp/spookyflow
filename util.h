/* util.h - small portable helpers (no PS3 headers). */
#ifndef SF_UTIL_H
#define SF_UTIL_H

#include <stddef.h>
#include <stdint.h>

#define SF_ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static inline float sf_clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline int   sf_clampi(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }
static inline float sf_lerp(float a, float b, float t)     { return a + (b - a) * t; }
static inline float sf_absf(float v)                       { return v < 0.0f ? -v : v; }
static inline float sf_minf(float a, float b)              { return a < b ? a : b; }
static inline float sf_maxf(float a, float b)              { return a > b ? a : b; }

/* Bounded string copy; always NUL-terminates when cap > 0. Returns length written. */
size_t sf_strlcpy(char *dst, const char *src, size_t cap);
/* Bounded append; always NUL-terminates. Returns resulting length. */
size_t sf_strlcat(char *dst, const char *src, size_t cap);
/* ASCII case-insensitive compare. */
int sf_stricmp(const char *a, const char *b);
int sf_strnicmp(const char *a, const char *b, size_t n);
/* Does s end with suffix (case-insensitive)? */
int sf_ends_with_i(const char *s, const char *suffix);
char sf_tolower(char c);
char sf_toupper(char c);
int sf_isalpha(char c);
int sf_isdigit(char c);
int sf_isspace(char c);

/* FNV-1a 32-bit hash. */
uint32_t sf_hash(const char *s);

/* Decode one UTF-8 code point from *s, advancing *s. Invalid bytes are
 * returned as-is (Latin-1 fallback) so filenames never break rendering. */
uint32_t sf_utf8_next(const char **s);

/* Format a byte size like "2.1 GB", "700 MB". */
void sf_format_size(uint64_t bytes, char *out, size_t cap);

/* Pack RGBA floats/bytes into 0xRRGGBBAA (Tiny3D's tiny3d_VertexColor order). */
static inline uint32_t sf_rgba(int r, int g, int b, int a)
{
    return ((uint32_t)(r & 255) << 24) | ((uint32_t)(g & 255) << 16) |
           ((uint32_t)(b & 255) << 8) | (uint32_t)(a & 255);
}
/* Multiply alpha of an RGBA color by f (0..1). */
uint32_t sf_col_alpha(uint32_t c, float f);
/* Multiply RGB of a color by f (0..1) keeping alpha. */
uint32_t sf_col_dim(uint32_t c, float f);
/* Linear blend of two colors. */
uint32_t sf_col_mix(uint32_t a, uint32_t b, float t);
/* HSV (0..1) to RGBA. */
uint32_t sf_hsv(float h, float s, float v, int a);

#endif
