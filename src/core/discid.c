/* discid.c - title IDs from inside disc images (see discid.h). */
#include "discid.h"
#include "layout.h"   /* PLAT_* */
#include "sfo.h"
#include "title.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

#define SECTOR        2048u
#define ROOT_MAX_SECT 8u        /* root directory sectors examined */

static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int discid_parse_system_cnf(const char *txt, size_t len, char *out, size_t cap)
{
    size_t i = 0;
    int pass;
    /* BOOT2 (PS2) wins over BOOT (PS1) if a disc somehow has both */
    for (pass = 0; pass < 2; pass++) {
        const char *key = pass == 0 ? "BOOT2" : "BOOT";
        size_t kl = strlen(key);
        for (i = 0; i < len; ) {
            size_t ls = i, le = i, p;
            while (le < len && txt[le] != '\n' && txt[le] != '\r') le++;
            p = ls;
            while (p < le && (txt[p] == ' ' || txt[p] == '\t')) p++;
            if (le - p > kl && sf_strnicmp(txt + p, key, kl) == 0) {
                size_t q = p + kl, start, end;
                while (q < le && (txt[q] == ' ' || txt[q] == '\t')) q++;
                if (q < le && txt[q] == '=') {
                    q++;
                    while (q < le && (txt[q] == ' ' || txt[q] == '\t')) q++;
                    start = q;
                    for (end = q; end < le && txt[end] != ';' && txt[end] != ' ' && txt[end] != '\t'; end++) {
                        if (txt[end] == '\\' || txt[end] == '/' || txt[end] == ':') start = end + 1;
                    }
                    if (end > start && title_normalize_id(txt + start, end - start, out, cap)) return 1;
                }
            }
            i = le + 1;
        }
    }
    if (cap) out[0] = '\0';
    return 0;
}

int discid_cue_first_file(const char *cue_path, const char *txt, size_t len, char *out, size_t cap)
{
    size_t i;
    for (i = 0; i < len; ) {
        size_t le = i, p = i;
        while (le < len && txt[le] != '\n' && txt[le] != '\r') le++;
        while (p < le && (txt[p] == ' ' || txt[p] == '\t')) p++;
        if (le - p > 5 && sf_strnicmp(txt + p, "FILE", 4) == 0 && (txt[p + 4] == ' ' || txt[p + 4] == '\t')) {
            size_t s = p + 5, e;
            char name[256];
            const char *slash = strrchr(cue_path, '/');
            size_t dir_len = slash ? (size_t)(slash - cue_path) + 1 : 0, n;
            while (s < le && (txt[s] == ' ' || txt[s] == '\t')) s++;
            if (s < le && txt[s] == '"') {
                s++;
                for (e = s; e < le && txt[e] != '"'; e++) { }
            } else {
                for (e = s; e < le && txt[e] != ' ' && txt[e] != '\t'; e++) { }
            }
            n = e - s;
            if (n == 0 || n >= sizeof(name)) return 0;
            memcpy(name, txt + s, n);
            name[n] = '\0';
            /* names in a cue are relative to its folder; keep only the base name */
            {
                const char *b = name, *q;
                for (q = name; *q; q++) if (*q == '/' || *q == '\\') b = q + 1;
                if (dir_len + strlen(b) + 1 > cap) return 0;
                memcpy(out, cue_path, dir_len);
                strcpy(out + dir_len, b);
            }
            return 1;
        }
        i = le + 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* ISO 9660                                                            */

typedef struct {
    ReadAtFn rd;
    void *ctx;
    const char *path;
    uint32_t pitch, data_off;   /* bytes per sector in the file, user data offset */
} Iso;

static int read_sector(const Iso *iso, uint32_t lba, unsigned char *buf)
{
    uint64_t off = (uint64_t)lba * iso->pitch + iso->data_off;
    return iso->rd(iso->ctx, iso->path, off, buf, SECTOR) == (int)SECTOR ? 0 : -1;
}

static int iso_open(Iso *iso, unsigned char *buf)
{
    static const uint32_t layouts[][2] = { { 2048u, 0u }, { 2352u, 24u }, { 2352u, 16u } };
    int i;
    for (i = 0; i < SF_ARRAY_LEN(layouts); i++) {
        iso->pitch = layouts[i][0];
        iso->data_off = layouts[i][1];
        if (read_sector(iso, 16, buf) == 0 && buf[0] == 1 && memcmp(buf + 1, "CD001", 5) == 0) return 0;
    }
    return -1;
}

static int is_exe_name(const unsigned char *n, size_t len)
{
    /* "SLUS_209.14;1" */
    size_t i;
    if (len < 11) return 0;
    for (i = 0; i < 4; i++) if (!sf_isalpha((char)n[i])) return 0;
    if (n[4] != '_' || n[8] != '.') return 0;
    for (i = 5; i < 8; i++) if (!sf_isdigit((char)n[i])) return 0;
    return sf_isdigit((char)n[9]) && sf_isdigit((char)n[10]) && (len == 11 || n[11] == ';');
}

static int psx_id(const Iso *iso, unsigned char *buf, char *out, size_t cap)
{
    uint32_t root_lba, root_len, s, nsect;
    uint32_t cnf_lba = 0, cnf_len = 0;
    char exe[TITLEID_MAX] = "";
    root_lba = le32(buf + 156 + 2);
    root_len = le32(buf + 156 + 10);
    nsect = (root_len + SECTOR - 1) / SECTOR;
    if (nsect == 0 || nsect > ROOT_MAX_SECT) nsect = nsect == 0 ? 1 : ROOT_MAX_SECT;
    for (s = 0; s < nsect && !cnf_lba; s++) {
        uint32_t i = 0;
        if (read_sector(iso, root_lba + s, buf) != 0) return 0;
        while (i + 33 < SECTOR) {
            uint32_t rl = buf[i], nl;
            const unsigned char *name;
            if (rl == 0) break;                       /* rest of this sector is padding */
            if (rl < 34 || i + rl > SECTOR) break;
            nl = buf[i + 32];
            name = buf + i + 33;
            if (33u + nl <= rl && !(buf[i + 25] & 2)) {
                if (nl >= 10 && sf_strnicmp((const char *)name, "SYSTEM.CNF", 10) == 0 && (nl == 10 || name[10] == ';')) {
                    cnf_lba = le32(buf + i + 2);
                    cnf_len = le32(buf + i + 10);
                    break;
                }
                if (!exe[0] && is_exe_name(name, nl)) title_normalize_id((const char *)name, 11, exe, sizeof(exe));
            }
            i += rl;
        }
    }
    if (cnf_lba) {
        if (read_sector(iso, cnf_lba, buf) == 0) {
            size_t n = cnf_len < SECTOR ? cnf_len : SECTOR;
            if (discid_parse_system_cnf((const char *)buf, n, out, cap)) return 1;
        }
    }
    if (exe[0]) { sf_strlcpy(out, exe, cap); return 1; }
    return 0;
}

static int ps3_iso_id(ReadAtFn rd, void *ctx, const char *path, char *out, size_t cap)
{
    unsigned char b[16];
    if (rd(ctx, path, 0x810, b, 10) != 10) return 0;
    return title_normalize_id((const char *)b, 10, out, cap);
}

static int ps3_folder_id(ReadAtFn rd, void *ctx, const char *path, char *out, size_t cap)
{
    enum { SFO_MAX = 64 * 1024 };
    char p[600], id[64];
    unsigned char *buf;
    int n, ok = 0;
    sf_strlcpy(p, path, sizeof(p));
    sf_strlcat(p, "/PS3_GAME/PARAM.SFO", sizeof(p));
    buf = (unsigned char *)malloc(SFO_MAX);
    if (!buf) return 0;
    n = rd(ctx, p, 0, buf, SFO_MAX);
    if (n > 0 && sfo_get_str(buf, (size_t)n, "TITLE_ID", id, sizeof(id)) == 0)
        ok = title_normalize_id(id, strlen(id), out, cap);
    free(buf);
    return ok;
}

int discid_read(int plat, const char *path, int is_dir, ReadAtFn rd, void *ctx, char *out, size_t cap)
{
    char img[600];
    unsigned char *buf;
    Iso iso;
    int ok = 0;
    if (cap) out[0] = '\0';
    if (!rd || !path || !path[0] || cap == 0) return 0;
    if (plat == PLAT_PS3) {
        ok = is_dir ? ps3_folder_id(rd, ctx, path, out, cap) : ps3_iso_id(rd, ctx, path, out, cap);
        if (!ok) out[0] = '\0';
        return ok;
    }
    if (is_dir) return 0;
    if (sf_ends_with_i(path, ".enc") || sf_ends_with_i(path, ".chd") || sf_ends_with_i(path, ".pbp")) return 0;
    buf = (unsigned char *)malloc(SECTOR);
    if (!buf) return 0;
    sf_strlcpy(img, path, sizeof(img));
    if (sf_ends_with_i(path, ".cue")) {
        int n = rd(ctx, path, 0, buf, SECTOR - 1);
        if (n <= 0 || !discid_cue_first_file(path, (const char *)buf, (size_t)n, img, sizeof(img))) { free(buf); return 0; }
    }
    iso.rd = rd; iso.ctx = ctx; iso.path = img;
    if (iso_open(&iso, buf) == 0) ok = psx_id(&iso, buf, out, cap);
    free(buf);
    if (!ok) out[0] = '\0';
    return ok;
}
