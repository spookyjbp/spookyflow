/* ps3_covers.c - cover art on the PS3: file access, JPEG/PNG decoding and
 * the cover worker thread (portable logic in src/core/coverart.c).
 *
 * Verified against PSL1GHT headers:
 *   lv2/sysfs.h  sysFsOpen(path, flags, &fd, NULL, 0), sysFsRead(fd, p, len, &n),
 *                sysFsWrite(fd, p, len, &n), sysFsLseek(fd, off, whence, &pos),
 *                sysFsClose, sysFsUnlink, sysFsStat, sysFsMkdir; SYS_O_* flags
 *   jpgdec.h     jpgLoadFromFile(path, jpgData*) -> bmp_out (malloc'd, freed with
 *                free()), pitch, width, height; ARGB bytes like pngLoadFromFile
 *   sysmodule.h  SYSMODULE_JPGDEC 0x0f, SYSMODULE_PNGDEC 0x18
 *   sys/thread.h sysThreadCreate(.., THREAD_JOINABLE, ..), sysThreadJoin, sysThreadExit
 *   sys/systime.h sysUsleep */
#include <malloc.h>
#include <ppu-asm.h>
#include <jpgdec/jpgdec.h>
#include <pngdec/pngdec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysmodule/sysmodule.h>
#include "coverart.h"
#include "log.h"
#include "ps3_platform.h"
#include "render.h"
#include "util.h"

#define SF_S_IFMT  0170000
#define SF_S_IFDIR 0040000

static CoverLoader g_loader;
static sys_ppu_thread_t g_tid;
static int g_started;
static volatile int g_dec_lock;

/* The tab images (main thread, during loading) and the cover worker both
 * decode; one decode at a time keeps the decoder modules out of trouble. */
void ps3_decode_lock(void)
{
    while (__sync_lock_test_and_set(&g_dec_lock, 1)) sysUsleep(200);
}

void ps3_decode_unlock(void) { __sync_lock_release(&g_dec_lock); }

static int io_read_at(void *ctx, const char *path, uint64_t off, void *buf, uint32_t len)
{
    s32 fd;
    u64 pos = 0, got = 0, total = 0;
    (void)ctx;
    if (sysFsOpen(path, SYS_O_RDONLY, &fd, NULL, 0) != 0) return -1;
    if (off && sysFsLseek(fd, (s64)off, 0 /* SEEK_SET */, &pos) != 0) { sysFsClose(fd); return -1; }
    while (total < len) {
        if (sysFsRead(fd, (u8 *)buf + total, len - total, &got) != 0) { sysFsClose(fd); return total ? (int)total : -1; }
        if (got == 0) break;
        total += got;
    }
    sysFsClose(fd);
    return (int)total;
}

static int io_is_file(void *ctx, const char *path)
{
    sysFSStat st;
    (void)ctx;
    return sysFsStat(path, &st) == 0 && (st.st_mode & SF_S_IFMT) != SF_S_IFDIR;
}

static int io_is_dir(void *ctx, const char *path)
{
    sysFSStat st;
    (void)ctx;
    return sysFsStat(path, &st) == 0 && (st.st_mode & SF_S_IFMT) == SF_S_IFDIR;
}

/* ARGB rows (pitch bytes) -> tightly packed RGBA, in place (no second
 * full-size buffer). Rows only ever move towards the start. */
static void argb_to_rgba_inplace(u8 *buf, u32 pitch, u32 w, u32 h)
{
    u32 x, y;
    for (y = 0; y < h; y++) {
        u8 *src = buf + (size_t)y * pitch, *d = buf + (size_t)y * w * 4u;
        if (src != d) memmove(d, src, (size_t)w * 4u);
        for (x = 0; x < w; x++, d += 4) {
            u8 a = d[0];
            d[0] = d[1]; d[1] = d[2]; d[2] = d[3]; d[3] = a;
        }
    }
}

static void *jpg_malloc(u32 size, void *arg) { (void)arg; return malloc(size); }
static void jpg_free(void *ptr, void *arg) { (void)arg; free(ptr); }

/* jpgLoadFromFile (PSL1GHT sprx/libjpgdec/jpgdec.c) with two changes: the
 * decoder's own priority is below the UI threads, and big covers are
 * decoded at 1/2, 1/4 or 1/8 size by the decoder itself (much faster) as
 * long as the result still covers the slot texture. */
static s32 decode_jpeg(const char *path, int min_w, int min_h, jpgData *out)
{
    s32 mh, sh, ret;
    jpgDecSource src;
    jpgDecInfo info;
    jpgDecInParam in;
    jpgDecOpnInfo open_info;
    jpgDecOutParam op;
    jpgDecDataInfo dinfo;
    jpgDecThreadInParam tin;
    jpgDecThreadOutParam tout;
    jpgDecDataCtrlParam dctl;
    jpgCbCtrlMalloc fm = jpg_malloc;
    jpgCbCtrlFree ff = jpg_free;

    memset(&src, 0, sizeof(src));
    src.stream_sel = JPGDEC_FILE;
    src.file_name = path;
    src.spu_enable = JPGDEC_SPU_THREAD_DISABLE;
    memset(&tin, 0, sizeof(tin));
    tin.spu_enable = JPGDEC_SPU_THREAD_DISABLE;
    tin.ppu_prio = 1700;
    tin.spu_prio = 200;
    tin.malloc_func = (jpgCbCtrlMalloc)__get_opd32(fm);
    tin.malloc_arg = NULL;
    tin.free_func = (jpgCbCtrlFree)__get_opd32(ff);
    tin.free_arg = NULL;

    out->bmp_out = NULL;
    ret = jpgDecCreate(&mh, &tin, &tout);
    if (ret != 0) return ret;
    ret = jpgDecOpen(mh, &sh, &src, &open_info);
    if (ret == 0) {
        ret = jpgDecReadHeader(mh, sh, &info);
        if (ret == 0 && info.color_space == 0) ret = -1;
        if (ret == 0) {
            u32 scale = 8;
            while (scale > 1 && ((int)(info.width / scale) < min_w || (int)(info.height / scale) < min_h)) scale >>= 1;
            memset(&in, 0, sizeof(in));
            in.cmd_ptr = NULL;
            in.down_scale = scale;
            in.quality_mode = JPGDEC_FAST;
            in.output_mode = JPGDEC_TOP_TO_BOTTOM;
            in.color_space = JPGDEC_ARGB;
            in.alpha = 0xff;
            ret = jpgDecSetParameter(mh, sh, &in, &op);
        }
        if (ret == 0) {
            out->pitch = op.width * 4;
            out->bmp_out = malloc((size_t)out->pitch * op.height);
            if (!out->bmp_out) ret = -1;
            else {
                memset(&dctl, 0, sizeof(dctl));
                dctl.output_bytes_per_line = out->pitch;
                ret = jpgDecDecodeData(mh, sh, out->bmp_out, &dctl, &dinfo);
                if (ret == 0 && dinfo.decode_status == 0) {
                    out->width = op.width;
                    out->height = op.height;
                } else if (ret == 0) {
                    ret = -1;
                }
            }
        }
        jpgDecClose(mh, sh);
    }
    if (ret != 0 && out->bmp_out) { free(out->bmp_out); out->bmp_out = NULL; }
    jpgDecDestroy(mh);
    return ret;
}

static int decode_any(const char *path, int min_w, int min_h, int max_dim, int *w, int *h, uint8_t **rgba)
{
    int is_png = sf_ends_with_i(path, ".png");
    void *bmp = NULL;
    u32 pitch = 0, bw = 0, bh = 0;
    s32 rc;
    *rgba = NULL;
    ps3_decode_lock();
    if (is_png) {
        pngData d;
        memset(&d, 0, sizeof(d));
        rc = pngLoadFromFile(path, &d);
        bmp = d.bmp_out; pitch = d.pitch; bw = d.width; bh = d.height;
    } else {
        jpgData d;
        memset(&d, 0, sizeof(d));
        rc = decode_jpeg(path, min_w, min_h, &d);
        bmp = d.bmp_out; pitch = d.pitch; bw = d.width; bh = d.height;
    }
    ps3_decode_unlock();
    if (rc != 0 || !bmp || bw == 0 || bh == 0 || pitch < bw * 4u) { free(bmp); return -1; }
    if ((int)bw > max_dim || (int)bh > max_dim) { free(bmp); *w = (int)bw; *h = (int)bh; return -1; }
    argb_to_rgba_inplace((u8 *)bmp, pitch, bw, bh);
    *rgba = (uint8_t *)bmp;              /* malloc'd by the decoder wrapper: free() is right */
    *w = (int)bw;
    *h = (int)bh;
    return 0;
}

static int io_decode(void *ctx, const char *path, int *w, int *h, uint8_t **rgba)
{
    (void)ctx;
    return decode_any(path, R_COVER_MAX_W, R_COVER_MAX_H, COVER_SRC_MAX_DIM, w, h, rgba);
}

/* For other images (the background): JPEGs are shrunk by the decoder only
 * while they stay at least min_w x min_h. */
int ps3_decode_image(const char *path, int min_w, int min_h, int max_dim, int *w, int *h, uint8_t **rgba)
{
    if (sysModuleIsLoaded(SYSMODULE_JPGDEC) != 0) sysModuleLoad(SYSMODULE_JPGDEC);
    if (sysModuleIsLoaded(SYSMODULE_PNGDEC) != 0) sysModuleLoad(SYSMODULE_PNGDEC);
    return decode_any(path, min_w, min_h, max_dim, w, h, rgba);
}

static int io_write(void *ctx, const char *path, const void *a, uint32_t alen, const void *b, uint32_t blen)
{
    s32 fd;
    u64 n = 0;
    int ok = 1;
    (void)ctx;
    if (sysFsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, NULL, 0) != 0) return -1;
    if (alen && (sysFsWrite(fd, a, alen, &n) != 0 || n != alen)) ok = 0;
    if (ok && blen && (sysFsWrite(fd, b, blen, &n) != 0 || n != blen)) ok = 0;
    sysFsClose(fd);
    if (!ok) { sysFsUnlink(path); return -1; }   /* never leave a half cache file */
    return 0;
}

static int io_remove(void *ctx, const char *path) { (void)ctx; return sysFsUnlink(path) == 0 ? 0 : -1; }
static int io_mkdir(void *ctx, const char *path) { (void)ctx; sysFsMkdir(path, 0777); return 0; }
static void io_sleep(void *ctx, int ms) { (void)ctx; sysUsleep(ms > 0 ? (u32)ms * 1000u : 100u); }

static void worker_entry(void *arg)
{
    cover_worker_main(arg);
    sysThreadExit(0);
}

CoverLoader *ps3_covers_start(void)
{
    CoverIO io;
    memset(&io, 0, sizeof(io));
    if (!r_cover_pixels(0)) { log_printf("covers: no texture memory, showing placeholders"); return NULL; }
    if (sysModuleIsLoaded(SYSMODULE_JPGDEC) != 0 && sysModuleLoad(SYSMODULE_JPGDEC) != 0)
        log_printf("covers: JPEG decoder module failed to load");
    if (sysModuleIsLoaded(SYSMODULE_PNGDEC) != 0 && sysModuleLoad(SYSMODULE_PNGDEC) != 0)
        log_printf("covers: PNG decoder module failed to load");
    io.read_at = io_read_at;
    io.is_file = io_is_file;
    io.is_dir = io_is_dir;
    io.decode = io_decode;
    io.write_file = io_write;
    io.remove = io_remove;
    io.mkdir = io_mkdir;
    io.list = ps3_fs_list;
    io.sleep_ms = io_sleep;
    io.cache_dir = SF_COVERS_DIR;
    io.pixels = r_cover_pixels;
    cover_loader_init(&g_loader, &io);
    /* priority 1600: below the main thread and the scan, so drawing never waits;
     * 256 KB stack (the decode work itself uses the heap) */
    if (sysThreadCreate(&g_tid, worker_entry, &g_loader, 1600, 256 * 1024, THREAD_JOINABLE, (char *)"sf_covers") != 0) {
        log_printf("covers: worker thread could not start, showing placeholders");
        return NULL;
    }
    g_started = 1;
    log_printf("covers: worker started, cache in %s", SF_COVERS_DIR);
    return &g_loader;
}

void ps3_covers_stop(void)
{
    u64 rv = 0;
    if (!g_started) return;
    g_started = 0;
    cover_loader_stop(&g_loader);
    if (!g_loader.running) sysThreadJoin(g_tid, &rv);
    else log_printf("covers: worker did not stop in time");
}
