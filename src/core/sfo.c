/* sfo.c - PARAM.SFO integer fields.
 * Layout (all little-endian): header { u32 magic "\0PSF", u32 version,
 * u32 key_table_start, u32 data_table_start, u32 entries }, then `entries`
 * index records { u16 key_offset, u16 data_fmt, u32 data_len, u32 data_max,
 * u32 data_offset }. Sony writes data_fmt 0x0404 for int32; PSL1GHT's sfo.py
 * writes the type as a single byte (4) after one other byte, so only the
 * type byte (index 3) is checked. Keys are NUL-terminated. */
#include "sfo.h"
#include <stdio.h>
#include <string.h>

#define SFO_MAX_BYTES (64u * 1024u)

static uint32_t rd32(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint32_t rd16(const unsigned char *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static void wr32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

/* Locate key; on success *type, *dlen and *data_off describe its value. */
static int find_entry(const unsigned char *b, size_t len, const char *key, uint32_t *type, uint32_t *dlen, size_t *data_off)
{
    uint32_t kts, dts, n, i;
    size_t klen = strlen(key);
    if (len < 20 || b[0] != 0 || b[1] != 'P' || b[2] != 'S' || b[3] != 'F') return -1;
    kts = rd32(b + 8); dts = rd32(b + 12); n = rd32(b + 16);
    if (kts > len || dts > len || n > 1024 || 20u + (size_t)n * 16u > len) return -1;
    for (i = 0; i < n; i++) {
        const unsigned char *e = b + 20 + (size_t)i * 16u;
        size_t ko = (size_t)kts + rd16(e);
        size_t doff = (size_t)dts + rd32(e + 12);
        if (ko + klen + 1 > len) return -1;
        if (memcmp(b + ko, key, klen) != 0 || b[ko + klen] != 0) continue;
        *type = e[3];
        *dlen = rd32(e + 4);
        if (doff > len || *dlen > len - doff) return -1;
        *data_off = doff;
        return 0;
    }
    return -2;
}

static int find_int(const unsigned char *b, size_t len, const char *key, size_t *data_off)
{
    uint32_t type, dlen;
    int rc = find_entry(b, len, key, &type, &dlen, data_off);
    if (rc != 0) return rc;
    if (type != 0x04 || dlen != 4) return -3;
    return 0;
}

int sfo_get_str(const unsigned char *buf, size_t len, const char *key, char *out, size_t cap)
{
    uint32_t type, dlen, i;
    size_t off;
    int rc;
    if (cap == 0) return -1;
    out[0] = '\0';
    rc = find_entry(buf, len, key, &type, &dlen, &off);
    if (rc != 0) return rc;
    if (type != 0x02 && type != 0x00) return -3;   /* utf8 (0x0204) or utf8-S (0x0004) */
    for (i = 0; i < dlen && i + 1 < cap && buf[off + i]; i++) out[i] = (char)buf[off + i];
    out[i] = '\0';
    return 0;
}

int sfo_get_u32(const unsigned char *buf, size_t len, const char *key, uint32_t *out)
{
    size_t off;
    int rc = find_int(buf, len, key, &off);
    if (rc == 0) *out = rd32(buf + off);
    return rc;
}

int sfo_set_u32(unsigned char *buf, size_t len, const char *key, uint32_t value)
{
    size_t off;
    int rc = find_int(buf, len, key, &off);
    if (rc == 0) wr32(buf + off, value);
    return rc;
}

static unsigned char g_sfo_buf[SFO_MAX_BYTES];

static long load(const char *path)
{
    FILE *f = fopen(path, "rb");
    size_t n;
    if (!f) return -1;
    n = fread(g_sfo_buf, 1, sizeof(g_sfo_buf), f);
    fclose(f);
    return (long)n;
}

int sfo_file_get_u32(const char *path, const char *key, uint32_t *out)
{
    long n = load(path);
    if (n <= 0) return -4;
    return sfo_get_u32(g_sfo_buf, (size_t)n, key, out);
}

int sfo_file_set_u32(const char *path, const char *key, uint32_t value)
{
    long n = load(path);
    size_t off;
    unsigned char v[4];
    FILE *f;
    int rc;
    if (n <= 0) return -4;
    rc = find_int(g_sfo_buf, (size_t)n, key, &off);
    if (rc != 0) return rc;
    f = fopen(path, "r+b");
    if (!f) return -4;
    wr32(v, value);
    if (fseek(f, (long)off, SEEK_SET) != 0 || fwrite(v, 1, 4, f) != 4) { fclose(f); return -4; }
    if (fclose(f) != 0) return -4;
    return 0;
}
