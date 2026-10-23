/* test_covers.h - milestone 2 tests (title IDs, cover lookup, processing,
 * cache, worker). Included by test_core.c (shares CHECK and g_app). */
#include "coverart.h"
#include "discid.h"
#include "render.h"

extern int g_null_cover_defined[R_COVER_SLOTS];

/* ---- a tiny in-memory file system ---- */
#define MEMFS_MAX 64
typedef struct { char path[COVER_PATH_MAX]; unsigned char *data; uint32_t len; int used; } MemFile;
static MemFile g_mf[MEMFS_MAX];
static const char *g_md[32];   /* directories that exist */
static int g_nmd, g_decodes, g_reads;

static void memfs_reset(void)
{
    int i;
    for (i = 0; i < MEMFS_MAX; i++) { free(g_mf[i].data); memset(&g_mf[i], 0, sizeof(g_mf[i])); }
    g_nmd = 0; g_decodes = 0; g_reads = 0;
}

static MemFile *memfs_find(const char *path)
{
    int i;
    for (i = 0; i < MEMFS_MAX; i++) if (g_mf[i].used && strcmp(g_mf[i].path, path) == 0) return &g_mf[i];
    return NULL;
}

static MemFile *memfs_put(const char *path, const void *data, uint32_t len)
{
    MemFile *f = memfs_find(path);
    int i;
    for (i = 0; !f && i < MEMFS_MAX; i++) if (!g_mf[i].used) f = &g_mf[i];
    if (!f) return NULL;
    free(f->data);
    f->used = 1;
    sf_strlcpy(f->path, path, sizeof(f->path));
    f->data = (unsigned char *)malloc(len ? len : 1);
    if (len) memcpy(f->data, data, len);
    f->len = len;
    return f;
}

static void memfs_dir(const char *d) { if (g_nmd < 32) g_md[g_nmd++] = d; }

static int mem_read_at(void *ctx, const char *path, uint64_t off, void *buf, uint32_t len)
{
    MemFile *f = memfs_find(path);
    (void)ctx;
    g_reads++;
    if (!f) return -1;
    if (off >= f->len) return 0;
    if (len > f->len - off) len = (uint32_t)(f->len - off);
    memcpy(buf, f->data + off, len);
    return (int)len;
}

static int mem_is_file(void *ctx, const char *path) { (void)ctx; return memfs_find(path) != NULL; }

static int mem_is_dir(void *ctx, const char *path)
{
    int i;
    (void)ctx;
    for (i = 0; i < g_nmd; i++) if (strcmp(g_md[i], path) == 0) return 1;
    return 0;
}

/* "IMG w h r g b a" -> solid image; "IMG2 w h" -> left half red, right half blue */
static int mem_decode(void *ctx, const char *path, int *w, int *h, uint8_t **rgba)
{
    MemFile *f = memfs_find(path);
    char txt[64];
    int r = 0, g = 0, b = 0, a = 255, i, split = 0;
    (void)ctx;
    g_decodes++;
    if (!f || f->len >= sizeof(txt)) return -1;
    memcpy(txt, f->data, f->len);
    txt[f->len] = '\0';
    if (sscanf(txt, "IMG %d %d %d %d %d %d", w, h, &r, &g, &b, &a) == 6) split = 0;
    else if (sscanf(txt, "IMG2 %d %d", w, h) == 2) split = 1;
    else return -1;
    *rgba = (uint8_t *)malloc((size_t)*w * (size_t)*h * 4u);
    if (!*rgba) return -1;
    for (i = 0; i < *w * *h; i++) {
        uint8_t *p = *rgba + (size_t)i * 4u;
        if (split) { int left = (i % *w) < *w / 2; p[0] = left ? 255 : 0; p[1] = 0; p[2] = left ? 0 : 255; p[3] = 255; }
        else { p[0] = (uint8_t)r; p[1] = (uint8_t)g; p[2] = (uint8_t)b; p[3] = (uint8_t)a; }
    }
    return 0;
}

static int mem_write(void *ctx, const char *path, const void *a, uint32_t alen, const void *b, uint32_t blen)
{
    unsigned char *tmp = (unsigned char *)malloc(alen + blen + 1);
    MemFile *f;
    (void)ctx;
    if (!tmp) return -1;
    memcpy(tmp, a, alen);
    memcpy(tmp + alen, b, blen);
    f = memfs_put(path, tmp, alen + blen);
    free(tmp);
    return f ? 0 : -1;
}

static int mem_remove(void *ctx, const char *path)
{
    MemFile *f = memfs_find(path);
    (void)ctx;
    if (!f) return -1;
    free(f->data);
    memset(f, 0, sizeof(*f));
    return 0;
}

static int mem_mkdir(void *ctx, const char *path) { (void)ctx; (void)path; return 0; }

static int mem_list(void *ctx, const char *dir, FsEntry *out, int max)
{
    int i, n = 0;
    size_t dl = strlen(dir);
    (void)ctx;
    for (i = 0; i < MEMFS_MAX && n < max; i++) {
        const char *p = g_mf[i].path;
        if (!g_mf[i].used || strncmp(p, dir, dl) != 0 || p[dl] != '/' || strchr(p + dl + 1, '/')) continue;
        sf_strlcpy(out[n].name, p + dl + 1, FS_NAME_MAX);
        out[n].is_dir = 0;
        out[n].size = g_mf[i].len;
        n++;
    }
    return n;
}

static void mem_sleep(void *ctx, int ms) { (void)ctx; (void)ms; }

static void mem_io(CoverIO *io, const char *cache)
{
    memset(io, 0, sizeof(*io));
    io->read_at = mem_read_at;
    io->is_file = mem_is_file;
    io->is_dir = mem_is_dir;
    io->decode = mem_decode;
    io->write_file = mem_write;
    io->remove = mem_remove;
    io->mkdir = mem_mkdir;
    io->list = mem_list;
    io->sleep_ms = mem_sleep;
    io->cache_dir = cache;
    io->pixels = r_cover_pixels;
}

/* ---- disc image builders ---- */

static void put_le32(unsigned char *p, uint32_t v) { p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8); p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24); }

static int dir_record(unsigned char *p, const char *name, uint32_t lba, uint32_t len, int is_dir)
{
    int nl = name[0] == '\0' || name[0] == '\1' ? 1 : (int)strlen(name);   /* "." and ".." are one byte */
    int rl = 33 + nl + ((33 + nl) & 1);                                        /* records have even length */
    memset(p, 0, (size_t)rl);
    p[0] = (unsigned char)rl;
    put_le32(p + 2, lba);
    put_le32(p + 10, len);
    p[25] = is_dir ? 2 : 0;
    p[32] = (unsigned char)nl;
    memcpy(p + 33, name, (size_t)nl);
    return rl;
}

/* 20 sectors of 2048: PVD at 16, root at 18, SYSTEM.CNF at 19. raw = 2352-byte
 * mode-2 sectors (user data at +24). Returns malloc'd image, *len set. */
static unsigned char *make_disc(const char *cnf, const char *exe, int raw, uint32_t *len)
{
    uint32_t pitch = raw ? 2352u : 2048u, off = raw ? 24u : 0u, s;
    unsigned char sec[20][2048];
    unsigned char *img;
    int o = 0;
    memset(sec, 0, sizeof(sec));
    sec[16][0] = 1; memcpy(sec[16] + 1, "CD001", 5);
    dir_record(sec[16] + 156, "\0", 18, 2048, 1);
    o += dir_record(sec[18] + o, "\0", 18, 2048, 1);
    o += dir_record(sec[18] + o, "\1", 18, 2048, 1);
    if (exe) o += dir_record(sec[18] + o, exe, 0, 1000, 0);
    if (cnf) {
        o += dir_record(sec[18] + o, "SYSTEM.CNF;1", 19, (uint32_t)strlen(cnf), 0);
        memcpy(sec[19], cnf, strlen(cnf));
    }
    *len = 20u * pitch;
    img = (unsigned char *)calloc(1, *len);
    for (s = 0; s < 20; s++) memcpy(img + s * pitch + off, sec[s], 2048);
    return img;
}

static size_t make_sfo_str(unsigned char *b, const char *key, const char *val)
{
    size_t kl = strlen(key) + 1, vl = strlen(val) + 1, kts = 20 + 16, dts = kts + ((kl + 3) & ~(size_t)3);
    memset(b, 0, 256);
    b[1] = 'P'; b[2] = 'S'; b[3] = 'F'; b[4] = 1; b[5] = 1;
    put_le32(b + 8, (uint32_t)kts); put_le32(b + 12, (uint32_t)dts); put_le32(b + 16, 1);
    b[20] = 0; b[22] = 4; b[23] = 2;               /* utf8 */
    put_le32(b + 24, (uint32_t)vl); put_le32(b + 28, 16); put_le32(b + 32, 0);
    memcpy(b + kts, key, kl);
    memcpy(b + dts, val, vl);
    return dts + 16;
}

static void test_discid(void)
{
    char id[16];
    unsigned char *img, sfo[256];
    uint32_t len;
    MemFile *f;
    printf("title IDs from disc images\n");
    CHECK(discid_parse_system_cnf("BOOT2 = cdrom0:\\SLUS_209.14;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n", strlen("BOOT2 = cdrom0:\\SLUS_209.14;1\r\nVER = 1.00\r\nVMODE = NTSC\r\n"), id, sizeof(id)));
    CHECK_STR(id, "SLUS20914");
    CHECK(discid_parse_system_cnf("BOOT = cdrom:\\SCUS_944.26;1\nTCB = 4\n", strlen("BOOT = cdrom:\\SCUS_944.26;1\nTCB = 4\n"), id, sizeof(id)));
    CHECK_STR(id, "SCUS94426");
    CHECK(discid_parse_system_cnf("BOOT=cdrom:SLES_123.45;1", strlen("BOOT=cdrom:SLES_123.45;1"), id, sizeof(id)));
    CHECK_STR(id, "SLES12345");
    CHECK(!discid_parse_system_cnf("BOOT = cdrom:\\MAIN.EXE;1\n", strlen("BOOT = cdrom:\\MAIN.EXE;1\n"), id, sizeof(id)) && id[0] == '\0');
    {   /* BOOT2 wins over a BOOT line (PS2 discs that also carry one) */
        const char *t = "BOOT = cdrom:\\SLPS_000.01;1\nBOOT2 = cdrom0:\\SLPM_650.02;1\n";
        CHECK(discid_parse_system_cnf(t, strlen(t), id, sizeof(id)));
        CHECK_STR(id, "SLPM65002");
    }
    {
        char out[256];
        const char *cue = "REM test\r\nFILE \"Crash Bandicoot (Track 1).bin\" BINARY\r\n  TRACK 01 MODE2/2352\r\n";
        CHECK(discid_cue_first_file("/dev_hdd0/PSXISO/Crash.cue", cue, strlen(cue), out, sizeof(out)));
        CHECK_STR(out, "/dev_hdd0/PSXISO/Crash Bandicoot (Track 1).bin");
        CHECK(discid_cue_first_file("/x/G.cue", "FILE G.bin BINARY\n", strlen("FILE G.bin BINARY\n"), out, sizeof(out)));
        CHECK_STR(out, "/x/G.bin");
        CHECK(!discid_cue_first_file("/x/G.cue", "TRACK 01\n", strlen("TRACK 01\n"), out, sizeof(out)));
    }
    memfs_reset();
    /* PS2 .iso, 2048-byte sectors */
    img = make_disc("BOOT2 = cdrom0:\\SLUS_209.14;1\r\nVER = 1.00\r\n", "SLUS_209.14;1", 0, &len);
    memfs_put("/dev_hdd0/PS2ISO/Game.iso", img, len); free(img);
    CHECK(discid_read(PLAT_PS2, "/dev_hdd0/PS2ISO/Game.iso", 0, mem_read_at, NULL, id, sizeof(id)));
    CHECK_STR(id, "SLUS20914");
    /* PS1 .cue -> raw 2352-byte .bin */
    img = make_disc("BOOT = cdrom:\\SCUS_944.26;1\r\n", NULL, 1, &len);
    memfs_put("/dev_hdd0/PSXISO/Crash.bin", img, len); free(img);
    memfs_put("/dev_hdd0/PSXISO/Crash.cue", "FILE \"Crash.bin\" BINARY\n", (uint32_t)strlen("FILE \"Crash.bin\" BINARY\n"));
    CHECK(discid_read(PLAT_PS1, "/dev_hdd0/PSXISO/Crash.cue", 0, mem_read_at, NULL, id, sizeof(id)));
    CHECK_STR(id, "SCUS94426");
    /* no SYSTEM.CNF: the executable's name is used */
    img = make_disc(NULL, "SLES_015.03;1", 1, &len);
    memfs_put("/dev_hdd0/PSXISO/Other.bin", img, len); free(img);
    CHECK(discid_read(PLAT_PS1, "/dev_hdd0/PSXISO/Other.bin", 0, mem_read_at, NULL, id, sizeof(id)));
    CHECK_STR(id, "SLES01503");
    /* not an ISO 9660 image */
    memfs_put("/dev_hdd0/PSXISO/Junk.bin", "hello", 5);
    CHECK(!discid_read(PLAT_PS1, "/dev_hdd0/PSXISO/Junk.bin", 0, mem_read_at, NULL, id, sizeof(id)) && id[0] == '\0');
    CHECK(!discid_read(PLAT_PS2, "/dev_hdd0/PS2ISO/Missing.iso", 0, mem_read_at, NULL, id, sizeof(id)));
    CHECK(!discid_read(PLAT_PS2, "/dev_hdd0/PS2ISO/Game.BIN.ENC", 0, mem_read_at, NULL, id, sizeof(id)));
    /* PS3 ISO: disc info sector */
    img = (unsigned char *)calloc(1, 0x1000);
    memcpy(img + 0x800, "PlayStation3\0\0\0\0", 16);
    memcpy(img + 0x810, "BLES-80608", 10);
    memfs_put("/dev_hdd0/PS3ISO/mm.iso", img, 0x1000); free(img);
    CHECK(discid_read(PLAT_PS3, "/dev_hdd0/PS3ISO/mm.iso", 0, mem_read_at, NULL, id, sizeof(id)));
    CHECK_STR(id, "BLES80608");
    /* PS3 folder: PARAM.SFO TITLE_ID */
    len = (uint32_t)make_sfo_str(sfo, "TITLE_ID", "BLUS30443");
    memfs_put("/dev_hdd0/GAMES/Demon/PS3_GAME/PARAM.SFO", sfo, len);
    CHECK(discid_read(PLAT_PS3, "/dev_hdd0/GAMES/Demon", 1, mem_read_at, NULL, id, sizeof(id)));
    CHECK_STR(id, "BLUS30443");
    {
        char s[32];
        CHECK(sfo_get_str(sfo, len, "TITLE_ID", s, sizeof(s)) == 0);
        CHECK_STR(s, "BLUS30443");
        CHECK(sfo_get_str(sfo, len, "NOPE", s, sizeof(s)) == -2);
        CHECK(sfo_get_str(sfo, len, "TITLE_ID", s, 4) == 0);        /* truncated, still terminated */
        CHECK_STR(s, "BLU");
    }
    f = memfs_find("/dev_hdd0/GAMES/Demon/PS3_GAME/PARAM.SFO");
    if (f) f->data[1] = 'X';
    CHECK(!discid_read(PLAT_PS3, "/dev_hdd0/GAMES/Demon", 1, mem_read_at, NULL, id, sizeof(id)));
    memfs_reset();
}

static int cand_has(const CoverCand *c, int n, const char *base)
{
    int i;
    for (i = 0; i < n; i++) if (strcmp(c[i].base, base) == 0) return i;
    return -1;
}

static void test_cover_lookup(void)
{
    CoverCand c[COVER_MAX_CANDS];
    unsigned char ok[16];
    int n, i;
    char stem[COVER_PATH_MAX], stem2[COVER_PATH_MAX];
    printf("cover lookup\n");
    n = cover_candidates(PLAT_PS2, "/dev_hdd0/PS2ISO/Game [SLUS-20914].iso", 0, "SLUS20914", NULL, c, COVER_MAX_CANDS);
    CHECK(n >= 10);
    CHECK_STR(c[0].base, "/dev_hdd0/PS2ISO/Game [SLUS-20914]");                 /* next to the game first */
    CHECK_STR(c[1].base, "/dev_hdd0/tmp/wmtmp/Game [SLUS-20914]");
    CHECK_STR(c[2].base, "/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/SLUS_209.14_COV");
    CHECK(cand_has(c, n, "/dev_hdd0/game/BLES80608/USRDIR/covers/SLUS20914") > 2);
    CHECK(cand_has(c, n, "/dev_hdd0/tmp/wmtmp/covers/SLUS20914") > 2);
    CHECK_STR(c[n - 1].base, "/dev_hdd0/PS2ISO/SLUS20914");                    /* game folder last */
    /* split images and decrypted PS3 ISOs lose every extension */
    n = cover_candidates(PLAT_PS3, "/dev_hdd0/PS3ISO/Game.dec.iso", 0, "", NULL, c, COVER_MAX_CANDS);
    CHECK(n == 2);
    CHECK_STR(c[0].base, "/dev_hdd0/PS3ISO/Game");
    n = cover_candidates(PLAT_PS3, "/dev_hdd0/PS3ISO/Big.iso.0", 0, "BLES80608", NULL, c, COVER_MAX_CANDS);
    CHECK_STR(c[0].base, "/dev_hdd0/PS3ISO/Big");
    CHECK(cand_has(c, n, "/dev_hdd0/game/BLES80608/USRDIR/covers/BLES80608") >= 0);
    CHECK(cand_has(c, n, "/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/BLES_806.08_COV") < 0);  /* PS3: no retro */
    /* folder games: nothing "next to" a folder */
    n = cover_candidates(PLAT_PS3, "/dev_hdd0/GAMES/Demon", 1, "BLUS30443", NULL, c, COVER_MAX_CANDS);
    CHECK_STR(c[0].base, "/dev_hdd0/tmp/wmtmp/Demon");
    CHECK(cand_has(c, n, "/dev_hdd0/GAMES/BLUS30443") < 0);
    /* missing folders are skipped */
    memset(ok, 0, sizeof(ok));
    for (i = 0; i < cover_folder_count(); i++)
        if (strcmp(cover_folder(i), "/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx") == 0) ok[i] = 1;
    n = cover_candidates(PLAT_PS1, "/dev_hdd0/PSXISO/Crash.cue", 0, "SCUS94426", ok, c, COVER_MAX_CANDS);
    CHECK(n == 3);
    CHECK_STR(c[0].base, "/dev_hdd0/PSXISO/Crash");
    CHECK_STR(c[1].base, "/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/SCUS_944.26_COV");
    CHECK_STR(c[2].base, "/dev_hdd0/PSXISO/SCUS94426");
    CHECK(cover_candidates(PLAT_PS1, "", 0, "X", NULL, c, COVER_MAX_CANDS) == 0);
    CHECK(cover_candidates(PLAT_PS2, "/a/b.iso", 0, "SLUS20914", NULL, c, 2) == 2);   /* respects max */
    /* cache names: stable, per platform, change with the file */
    cover_cache_stem("/c", PLAT_PS2, "/dev_hdd0/PS2ISO/A.iso", 100, stem, sizeof(stem));
    cover_cache_stem("/c", PLAT_PS2, "/dev_hdd0/PS2ISO/A.iso", 100, stem2, sizeof(stem2));
    CHECK_STR(stem, stem2);
    CHECK(strncmp(stem, "/c/ps2/", 7) == 0 && strlen(stem) == 7 + 16);
    cover_cache_stem("/c", PLAT_PS2, "/dev_hdd0/PS2ISO/A.iso", 101, stem2, sizeof(stem2));
    CHECK(strcmp(stem, stem2) != 0);
    cover_cache_stem("/c", PLAT_PS2, "/dev_hdd0/PS2ISO/B.iso", 100, stem2, sizeof(stem2));
    CHECK(strcmp(stem, stem2) != 0);
}

static void channel_mean(const uint16_t *px, int n, double m[3])
{
    int i;
    m[0] = m[1] = m[2] = 0.0;
    for (i = 0; i < n; i++) {
        m[0] += (px[i] >> 10) & 31; m[1] += (px[i] >> 5) & 31; m[2] += px[i] & 31;
    }
    m[0] /= n; m[1] /= n; m[2] /= n;
}

static void test_cover_process(void)
{
    static uint16_t px[R_COVER_SLOT_BYTES / 2];
    uint8_t *img;
    int i, tw, th, all_opaque = 1;
    double m[3];
    printf("cover processing\n");
    cover_tex_size(PLAT_PS1, &tw, &th);
    CHECK(tw == 384 && th == 384);
    cover_tex_size(PLAT_PS2, &tw, &th);
    CHECK(tw == 320 && th == 448);
    for (i = 0; i < PLAT_COUNT; i++) {
        cover_tex_size(i, &tw, &th);
        CHECK(tw <= R_COVER_MAX_W && th <= R_COVER_MAX_H && (tw * 2) % 64 == 0);   /* RSX pitch */
    }
    CHECK_NEAR(cover_art_aspect(PLAT_PS1), 1.0, 0.001);
    CHECK_NEAR(cover_art_aspect(PLAT_PS2), 0.70, 0.002);
    CHECK_NEAR(cover_art_aspect(PLAT_PS3), 0.86, 0.002);

    /* solid colors survive exactly (white -> 31, black -> 0) */
    img = (uint8_t *)malloc(500 * 700 * 4);
    for (i = 0; i < 500 * 700; i++) { img[i * 4] = 255; img[i * 4 + 1] = 0; img[i * 4 + 2] = 255; img[i * 4 + 3] = 255; }
    cover_process(img, 500, 700, cover_art_aspect(PLAT_PS2), px, tw, th);
    for (i = 0; i < tw * th; i++) if (px[i] != (0x8000 | (31 << 10) | 31)) { all_opaque = 0; break; }
    CHECK(all_opaque);
    /* mid gray: dithered, but averages to the true value */
    for (i = 0; i < 500 * 700; i++) { img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = 128; }
    cover_process(img, 500, 700, cover_art_aspect(PLAT_PS2), px, tw, th);
    channel_mean(px, tw * th, m);
    CHECK_NEAR(m[0], 128.0 * 31.0 / 255.0, 0.08);
    CHECK_NEAR(m[1], 128.0 * 31.0 / 255.0, 0.08);
    for (i = 1; i < tw * th && px[i] == px[0]; i++) { }
    CHECK(i < tw * th);                                    /* really dithered */
    /* transparent pixels show the dark backdrop, never garbage */
    for (i = 0; i < 500 * 700; i++) img[i * 4 + 3] = 0;
    cover_process(img, 500, 700, cover_art_aspect(PLAT_PS2), px, tw, th);
    channel_mean(px, tw * th, m);
    CHECK(m[0] < 4.0 && m[2] < 4.0 && (px[0] & 0x8000));
    free(img);

    /* nothing is ever cut off: a slightly different shape is stretched */
    img = (uint8_t *)malloc(100 * 100 * 4);
    for (i = 0; i < 100 * 100; i++) {
        int x = i % 100;
        img[i * 4] = (uint8_t)(x < 10 || x >= 90 ? 255 : 0); img[i * 4 + 1] = 0; img[i * 4 + 2] = (uint8_t)(x < 10 || x >= 90 ? 0 : 255); img[i * 4 + 3] = 255;
    }
    cover_process(img, 100, 100, 0.84f, px, 384, 448);     /* 1.0 -> 0.84: stretched */
    CHECK(((px[0] >> 10) & 31) == 31 && (px[0] & 31) == 0);             /* left border kept */
    CHECK(((px[383] >> 10) & 31) == 31 && (px[383] & 31) == 0);         /* right border kept */
    cover_process(img, 100, 100, 0.78f, px, 320, 448);     /* 1.0 -> 0.78: too different, letterboxed */
    CHECK(((px[0] >> 10) & 31) < 5);                                    /* backdrop above */
    CHECK(((px[224 * 320 + 2] >> 10) & 31) == 31);                      /* border still there */
    free(img);
    /* a very wide banner is shown whole: backdrop above and below */
    img = (uint8_t *)malloc(400 * 100 * 4);
    for (i = 0; i < 400 * 100; i++) { img[i * 4] = 255; img[i * 4 + 1] = 255; img[i * 4 + 2] = 255; img[i * 4 + 3] = 255; }
    cover_process(img, 400, 100, 0.70f, px, 320, 448);
    CHECK(((px[0] >> 10) & 31) < 5);                       /* top row: backdrop */
    CHECK(((px[224 * 320 + 160] >> 10) & 31) == 31);       /* middle: the image */
    free(img);
    /* tiny source enlarges without crashing */
    img = (uint8_t *)malloc(2 * 3 * 4);
    for (i = 0; i < 6; i++) { img[i * 4] = img[i * 4 + 1] = img[i * 4 + 2] = 200; img[i * 4 + 3] = 255; }
    cover_process(img, 2, 3, 0.70f, px, 320, 448);
    CHECK((px[0] & 0x8000) && ((px[0] >> 10) & 31) >= 23);
    free(img);
}

static void test_cover_loader(void)
{
    static CoverLoader L;
    CoverIO io;
    CoverCache cc;
    uint32_t len;
    unsigned char *img;
    int i, slot, tw, th, decodes, steps;
    char stem[COVER_PATH_MAX], path[COVER_PATH_MAX + 8];
    printf("cover loader\n");
    memfs_reset();
    memfs_dir("/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx");
    img = make_disc("BOOT2 = cdrom0:\\SLUS_209.14;1\r\n", NULL, 0, &len);
    memfs_put("/dev_hdd0/PS2ISO/Game.iso", img, len); free(img);
    memfs_put("/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/SLUS_209.14_COV.JPG", "IMG 50 70 255 0 0 255", 21);
    memfs_put("/dev_hdd0/PS2ISO/Other.iso", "junk", 4);
    memfs_put("/dev_hdd0/PS2ISO/Side.iso", "junk", 4);
    memfs_put("/dev_hdd0/PS2ISO/Side.png", "IMG 70 100 0 0 255 255", 22);   /* next to the game wins */
    mem_io(&io, "/cache");
    cover_loader_init(&L, &io);
    cc_init(&cc, 8, NULL, NULL);
    cover_tex_size(PLAT_PS2, &tw, &th);

    /* found through the title ID read from the image */
    cc.slot[0].state = CC_LOADING;
    cover_request(&L, 0, PLAT_PS2, PLAT_PS2, 0, "/dev_hdd0/PS2ISO/Game.iso", 0, len, "");
    CHECK(cover_worker_step(&L) == 1);
    CHECK(L.probed && L.n_art == 1);
    memset(g_null_cover_defined, 0, sizeof(g_null_cover_defined));
    cover_poll(&L, &cc);
    CHECK(cc.slot[0].state == CC_READY && cc.slot[0].tex == TEX_COVER0);
    CHECK(g_null_cover_defined[0] == 1);
    CHECK(r_cover_pixels(0)[0] == (0x8000 | (31 << 10)));     /* red */
    cover_cache_stem("/cache", PLAT_PS2, "/dev_hdd0/PS2ISO/Game.iso", len, stem, sizeof(stem));
    snprintf(path, sizeof(path), "%s.cvr", stem);
    CHECK(memfs_find(path) && memfs_find(path)->len == 16u + (uint32_t)(tw * th * 2));
    CHECK(cover_worker_step(&L) == 0);                         /* nothing left */

    /* same-name image beside the game, and a missing cover */
    cc.slot[1].state = CC_LOADING;
    cc.slot[2].state = CC_LOADING;
    cover_request(&L, 1, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_request(&L, 2, PLAT_PS2, PLAT_PS2, 2, "/dev_hdd0/PS2ISO/Other.iso", 0, 4, "");
    while (cover_worker_step(&L)) { }
    cover_poll(&L, &cc);
    CHECK(cc.slot[1].state == CC_READY && (r_cover_pixels(1)[0] & 31) == 31);   /* blue */
    CHECK(cc.slot[2].state == CC_FAILED);
    cover_cache_stem("/cache", PLAT_PS2, "/dev_hdd0/PS2ISO/Other.iso", 4, stem, sizeof(stem));
    snprintf(path, sizeof(path), "%s.none", stem);
    CHECK(memfs_find(path) != NULL);                           /* searched only once */
    decodes = g_decodes;
    cc.slot[2].state = CC_LOADING;
    cover_request(&L, 2, PLAT_PS2, PLAT_PS2, 2, "/dev_hdd0/PS2ISO/Other.iso", 0, 4, "");
    g_reads = 0;
    cover_worker_step(&L);
    cover_poll(&L, &cc);
    CHECK(cc.slot[2].state == CC_FAILED && g_reads <= 1 && g_decodes == decodes);

    /* second run: straight from the cache, no decoding, even if the art is gone */
    mem_remove(NULL, "/dev_hdd0/game/BLES80608/USRDIR/covers_retro/psx/SLUS_209.14_COV.JPG");
    cover_loader_stop(&L);
    cover_loader_init(&L, &io);
    cc_init(&cc, 8, NULL, NULL);
    cc.slot[3].state = CC_LOADING;
    cover_request(&L, 3, PLAT_PS2, PLAT_PS2, 0, "/dev_hdd0/PS2ISO/Game.iso", 0, len, "");
    decodes = g_decodes;
    cover_worker_step(&L);
    cover_poll(&L, &cc);
    CHECK(cc.slot[3].state == CC_READY && L.n_cache == 1 && g_decodes == decodes);
    CHECK(r_cover_pixels(3)[0] == (0x8000 | (31 << 10)));
    CHECK(!L.probed);                                          /* cache hits never touch the cover folders */

    /* a slot re-requested before the worker got to it only delivers the newer game */
    cc.slot[4].state = CC_LOADING;
    cover_request(&L, 4, PLAT_PS2, PLAT_PS2, 5, "/dev_hdd0/PS2ISO/Other.iso", 0, 4, "");
    cover_request(&L, 4, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    steps = 0;
    while (cover_worker_step(&L)) steps++;
    CHECK(steps == 1);
    cover_poll(&L, &cc);
    CHECK(cc.slot[4].state == CC_READY && (r_cover_pixels(4)[0] & 31) == 31);
    /* cancel: nothing is processed or marked */
    cc.slot[5].state = CC_LOADING;
    cover_request(&L, 5, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_cancel_all(&L);
    CHECK(cover_worker_step(&L) == 0);
    /* nearest to the selection first */
    for (i = 0; i < 3; i++) cc.slot[i].state = CC_LOADING;
    cover_request(&L, 0, PLAT_PS2, PLAT_PS2, 40, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_request(&L, 1, PLAT_PS1, PLAT_PS1, 11, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_request(&L, 2, PLAT_PS2, PLAT_PS2, 9, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_set_focus(&L, PLAT_PS2, 10);
    cover_worker_step(&L);
    CHECK(L.job[2].done_gen == L.job[2].req_gen && L.job[0].done_gen != L.job[0].req_gen);
    cover_worker_step(&L);
    CHECK(L.job[0].done_gen == L.job[0].req_gen && L.job[1].done_gen != L.job[1].req_gen);   /* other tab last */
    cover_worker_step(&L);

    /* Rescan Library clears the cache */
    cover_wipe_cache(&L);
    CHECK(cover_worker_step(&L) == 1);
    snprintf(path, sizeof(path), "%s.none", stem);
    CHECK(memfs_find(path) == NULL);
    for (i = 0, slot = 0; i < MEMFS_MAX; i++) if (g_mf[i].used && strncmp(g_mf[i].path, "/cache/", 7) == 0) slot++;
    CHECK(slot == 0);
    CHECK(memfs_find("/dev_hdd0/PS2ISO/Side.png") != NULL);    /* user files untouched */

    /* no disk cache configured: still works */
    mem_io(&io, NULL);
    cover_loader_stop(&L);
    cover_loader_init(&L, &io);
    cc.slot[6].state = CC_LOADING;
    cover_request(&L, 6, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_worker_step(&L);
    cover_poll(&L, &cc);
    CHECK(cc.slot[6].state == CC_READY);
    /* a decoder failure is a missing cover, not a crash */
    memfs_put("/dev_hdd0/PS2ISO/Side.png", "garbage", 7);
    cc.slot[7].state = CC_LOADING;
    cover_request(&L, 7, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
    cover_worker_step(&L);
    cover_poll(&L, &cc);
    CHECK(cc.slot[7].state == CC_FAILED);
    /* an oversized PNG is refused from its header, never decoded */
    {
        unsigned char png[24] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 13, 'I', 'H', 'D', 'R',
                                  0, 0, 0x0B, 0xB8, 0, 0, 0x0B, 0xB8 };   /* 3000 x 3000 */
        memfs_put("/dev_hdd0/PS2ISO/Side.png", png, 24);
        decodes = g_decodes;
        cc.slot[7].state = CC_LOADING;
        cover_request(&L, 7, PLAT_PS2, PLAT_PS2, 1, "/dev_hdd0/PS2ISO/Side.iso", 0, 4, "");
        cover_worker_step(&L);
        cover_poll(&L, &cc);
        CHECK(cc.slot[7].state == CC_FAILED && g_decodes == decodes);
    }
    cover_loader_stop(&L);
    memfs_reset();
}

static void test_cover_cache_gpu(void)
{
    CoverCache cc;
    int s, i, reused = 0;
    printf("cover cache gpu guard\n");
    cc_init(&cc, 4, NULL, NULL);
    cc_update(&cc, 0, 0, 100);                 /* loads 0..3 (4 per frame) */
    CHECK(cc_resident_count(&cc) == 4);
    s = cc_lookup(&cc, 0, 0);                  /* slot drawn this frame */
    CHECK(s >= 0);
    cc_update(&cc, 0, 50, 100);                /* jump away: everything is out of the window */
    CHECK(cc.slot[s].index == 0);              /* ...but the drawn slot is still in use by the GPU */
    for (i = 0; i < CC_GPU_FRAMES + 1; i++) cc_update(&cc, 0, 50, 100);
    CHECK(cc.slot[s].index != 0);              /* reused once the GPU is done with it */
    /* cc_mark: async completion and fade age */
    cc_init(&cc, 4, NULL, NULL);
    cc.slot[1].state = CC_LOADING;
    cc_mark(&cc, 1, TEX_COVER0 + 1);
    CHECK(cc.slot[1].state == CC_READY && cc.slot[1].tex == TEX_COVER0 + 1 && cc_ready_age(&cc, 1) == 0);
    cc_update(&cc, 0, 0, 1);
    cc_update(&cc, 0, 0, 1);
    CHECK(cc_ready_age(&cc, 1) == 2);
    cc.slot[2].state = CC_LOADING;
    cc_mark(&cc, 2, -1);
    CHECK(cc.slot[2].state == CC_FAILED && cc_ready_age(&cc, 2) == 0);
    cc_mark(&cc, 3, 5);                        /* not loading: ignored */
    CHECK(cc.slot[3].state != CC_READY || cc.slot[3].tex != 5);
    (void)reused;
}

/* The app hands real games to the loader, keeps placeholders for demo games
 * and drops pending work when the library changes. */
static void test_app_covers(void)
{
    static CoverLoader L;
    CoverIO io;
    AppHost host;
    int i, loading = 0, ready = 0;
    printf("app covers\n");
    memfs_reset();
    mem_io(&io, NULL);
    cover_loader_init(&L, &io);
    memset(&host, 0, sizeof(host));
    host.fs_list = fake_list;
    host.covers = &L;
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.0f);
    CHECK(g_app.mode == MODE_FLOW);
    for (i = 0; i < g_app.covers.budget; i++) if (g_app.covers.slot[i].state == CC_LOADING) loading++;
    CHECK(loading == g_lib.tab[0].count + g_lib.tab[1].count + g_lib.tab[2].count);   /* every tab's games queued */
    while (cover_worker_step(&L)) { }
    run_frames(0, 0.1f);
    for (i = 0, loading = 0; i < g_app.covers.budget; i++) {
        if (g_app.covers.slot[i].state == CC_LOADING) loading++;
        if (g_app.covers.slot[i].state == CC_READY) ready++;
    }
    CHECK(loading == 0);                                       /* no art in the fake fs: all FAILED */
    CHECK(ready == 0);
    /* demo games never go to the worker */
    host.fs_list = fake_none;
    cover_loader_stop(&L);
    cover_loader_init(&L, &io);
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.0f);
    CHECK(g_lib.tab[g_app.tab].demo);
    CHECK(cover_worker_step(&L) == 0);
    for (i = 0, ready = 0; i < g_app.covers.budget; i++) if (g_app.covers.slot[i].state == CC_READY) ready++;
    CHECK(ready > 0);
    ui_draw(&g_app);                                           /* draws with and without art */
    cover_loader_stop(&L);
    memfs_reset();
}

/* fix 1: sleep after 5 idle minutes, any button wakes without acting;
 * Show Game Info persists */
static void test_sleep_and_info(void)
{
    AppHost host;
    int sel;
    Settings s1, s2;
    printf("sleep mode, game info toggle\n");
    memset(&host, 0, sizeof(host));
    host.fs_list = fake_none;
    app_init(&g_app, &g_lib, &host);
    run_frames(0, LOAD_MIN_TIME + 1.5f);
    CHECK(g_app.mode == MODE_FLOW && !g_app.asleep);
    run_frames(0, SLEEP_TIME - 10.0f);
    CHECK(!g_app.asleep);
    tap(SFB_RIGHT);                                   /* activity resets the timer */
    run_frames(0, SLEEP_TIME - 10.0f);
    CHECK(!g_app.asleep);
    run_frames(0, 12.0f + SLEEP_FADE_TIME);
    CHECK(g_app.asleep && g_app.sleep_t >= 0.999f);
    ui_draw(&g_app);                                  /* asleep: draws nothing */
    sel = g_app.view[g_app.tab].sel;
    run_frames(SFB_RIGHT, 0.6f);                      /* the waking press doesn't move... */
    run_frames(0, 0.1f);
    CHECK(!g_app.asleep && g_app.view[g_app.tab].sel == sel);
    run_frames(0, WAKE_FADE_TIME + 0.05f);
    CHECK(g_app.sleep_t == 0.0f);
    tap(SFB_RIGHT);                                   /* ...the next one does */
    CHECK(g_app.view[g_app.tab].sel == sel + 1);
    run_frames(0, SLEEP_TIME + 2.0f);
    CHECK(g_app.asleep);
    run_frames(SFB_CIRCLE, HOLD_EXIT_TIME + 1.0f);    /* holding ◯ to wake never exits */
    CHECK(!g_app.asleep && !g_app.exiting);
    run_frames(0, 0.1f);

    settings_defaults(&s1);
    CHECK(s1.show_info == 1);
    s1.show_info = 0;
    CHECK(settings_save(&s1, "build-host/test_info.ini") == 0);
    settings_defaults(&s2);
    CHECK(settings_load(&s2, "build-host/test_info.ini") == 0 && s2.show_info == 0);
    {
        GameCfg g;
        char txt[32];
        memset(&g, 0, sizeof(g));
        CHECK(menu_change(ROW_F_INFO, 0, 0, &s1, &g) == MENU_FX_CHANGED && s1.show_info == 1);
        menu_value_text(ROW_F_INFO, 0, &s1, &g, txt, sizeof(txt));
        CHECK_STR(txt, "On");
        CHECK_STR(menu_label(ROW_F_INFO), "Show Game Info");
    }
    g_app.settings.show_info = 0;
    ui_draw(&g_app);
}

/* fix 2: the other tabs' visible covers stay loaded, so switching tabs
 * shows covers at once */
static void test_cover_cache_tabs(void)
{
    CoverCache cc;
    int f, i, ok = 1;
    printf("cover cache keeps other tabs\n");
    cc_init(&cc, CC_DEFAULT_BUDGET, NULL, NULL);
    cc_set_side(&cc, 0, 40, 300);
    cc_set_side(&cc, 2, 0, 5);
    for (f = 0; f < 60; f++) cc_update(&cc, 1, 100, 500);
    for (i = 100 - 7; i <= 100 + 7; i++) if (cc_lookup(&cc, 1, i) < 0) ok = 0;
    for (i = 40 - 7; i <= 40 + 7; i++) if (cc_lookup(&cc, 0, i) < 0) ok = 0;
    for (i = 0; i < 5; i++) if (cc_lookup(&cc, 2, i) < 0) ok = 0;
    CHECK(ok);
    CHECK(cc_resident_count(&cc) <= CC_DEFAULT_BUDGET);
    /* switching to tab 0 (the app always reports every tab): nothing new needs loading */
    cc_set_side(&cc, 1, 100, 500);
    for (f = 0; f < 4; f++) cc_update(&cc, 0, 40, 300);
    for (i = 40 - 7, ok = 1; i <= 40 + 7; i++) if (cc_lookup(&cc, 0, i) < 0) ok = 0;
    CHECK(ok);
    /* scrolling far on one tab never steals the others' visible covers */
    for (f = 0; f < 200; f++) cc_update(&cc, 0, 40 + f, 300);
    for (i = 100 - 7, ok = 1; i <= 100 + 7; i++) if (cc_lookup(&cc, 1, i) < 0) ok = 0;
    CHECK(ok);
}

/* fix 3: case images (ManaGunZ's layout) set the case shape and cover window */
static void test_case_images(void)
{
    CaseWindow w;
    printf("case images\n");
    layout_set_case_image(PLAT_PS3, 270.0f, 340.0f, 0.0f, 30.0f, 260.0f, 300.0f);
    CHECK_NEAR(layout_case_aspect(PLAT_PS3), 270.0 / 340.0, 1e-5);
    layout_case_window(PLAT_PS3, &w);
    CHECK_NEAR(w.x0, 0.0, 1e-6); CHECK_NEAR(w.x1, 260.0 / 270.0, 1e-5);
    CHECK_NEAR(w.y0, 30.0 / 340.0, 1e-5); CHECK_NEAR(w.y1, 330.0 / 340.0, 1e-5);
    CHECK_NEAR(layout_window_aspect(PLAT_PS3), 260.0 / 300.0, 1e-4);    /* = the cover's shape */
    ui_set_case_image(PLAT_PS3, TEX_CASE_PS3);
    g_app.tab = PLAT_PS3;
    ui_draw(&g_app);
    ui_clear_case_images();
    CHECK_NEAR(layout_window_aspect(PLAT_PS3), 0.86, 0.002);             /* back to the drawn case */
    layout_set_case_image(PLAT_PS1, 0.0f, 250.0f, 30.0f, 5.0f, 240.0f, 240.0f);   /* bad size: ignored */
    CHECK_NEAR(layout_window_aspect(PLAT_PS1), 1.0, 0.002);
}
