/* ps3_fs.c - directory listing for the scanner, via lv2 sysFs.
 * Verified against PSL1GHT ppu/include/lv2/sysfs.h: sysFsOpendir(path, &fd),
 * sysFsReaddir(fd, sysFSDirent*, u64 *read), sysFsClosedir(fd),
 * sysFsStat(path, sysFSStat*), sysFsMkdir(path, mode); sysFSDirent has
 * d_type, d_namlen, d_name; sysFSStat has s32 st_mode, u64 st_size. */
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include "ps3_platform.h"
#include "log.h"
#include "util.h"

/* UNVERIFIED: lv2 mode bits follow the POSIX layout (S_IFMT 0170000,
 * S_IFDIR 0040000) and d_type 1 = directory, as in the Cell SDK. */
#define SF_S_IFMT  0170000
#define SF_S_IFDIR 0040000
#define SF_DT_DIR  1

int ps3_fs_exists(const char *path)
{
    sysFSStat st;
    return sysFsStat(path, &st) == 0;
}

void ps3_fs_mkdirs(void)
{
    /* mode 0777 like PSL1GHT samples; errors (already exists) are fine */
    sysFsMkdir("/dev_hdd0/tmp", 0777);
    sysFsMkdir(SF_DATA_DIR, 0777);
    sysFsMkdir(SF_TABS_DIR, 0777);
    sysFsMkdir(SF_SOUNDS_DIR, 0777);
    sysFsMkdir(SF_MUSIC_DIR, 0777);
    sysFsMkdir(SF_BG_DIR, 0777);
    sysFsMkdir(SF_CASES_DIR, 0777);
    sysFsMkdir(SF_COVERS_DIR, 0777);
    sysFsMkdir(SF_COVERS_DIR "/ps1", 0777);
    sysFsMkdir(SF_COVERS_DIR "/ps2", 0777);
    sysFsMkdir(SF_COVERS_DIR "/ps3", 0777);
}

int ps3_fs_list(void *ctx, const char *dir, FsEntry *out, int max)
{
    s32 fd;
    sysFSDirent ent;
    u64 nread = 0;
    int n = 0, skipped = 0;
    char path[SCAN_PATH_MAX];
    (void)ctx;
    if (sysFsOpendir(dir, &fd) != 0) return -1;
    while (n < max) {
        sysFSStat st;
        size_t len;
        if (sysFsReaddir(fd, &ent, &nread) != 0 || nread == 0) break;
        if (ent.d_name[0] == '.' && (ent.d_name[1] == '\0' || (ent.d_name[1] == '.' && ent.d_name[2] == '\0'))) continue;
        len = strlen(ent.d_name);
        if (len >= FS_NAME_MAX) { skipped++; continue; }
        memcpy(out[n].name, ent.d_name, len + 1);
        out[n].is_dir = 0;
        out[n].size = 0;
        if (snprintf(path, sizeof(path), "%s/%s", dir, ent.d_name) < (int)sizeof(path) && sysFsStat(path, &st) == 0) {
            out[n].is_dir = (st.st_mode & SF_S_IFMT) == SF_S_IFDIR;
            out[n].size = out[n].is_dir ? 0 : (uint64_t)st.st_size;
        } else {
            out[n].is_dir = ent.d_type == SF_DT_DIR;
        }
        n++;
    }
    sysFsClosedir(fd);
    if (skipped) log_printf("scan: %s: %d names too long, skipped", dir, skipped);
    return n;
}

/* ---- small file operations (PS2 config picker) ---- */
int ps3_fs_is_file(void *ctx, const char *path)
{
    sysFSStat st;
    (void)ctx;
    return sysFsStat(path, &st) == 0 && (st.st_mode & SF_S_IFMT) != SF_S_IFDIR;
}

int ps3_fs_write_file(const char *path, const void *data, uint32_t len)
{
    s32 fd;
    u64 n = 0;
    int ok;
    if (sysFsOpen(path, SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &fd, NULL, 0) != 0) return -1;
    ok = len == 0 || (sysFsWrite(fd, data, len, &n) == 0 && n == len);
    sysFsClose(fd);
    if (!ok) { sysFsUnlink(path); return -1; }
    sysFsChmod(path, 0666);      /* other tools (ManaGunZ, FTP) can replace it */
    return 0;
}

int ps3_fs_remove_file(const char *path) { return sysFsUnlink(path) == 0 ? 0 : -1; }
