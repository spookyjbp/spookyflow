/* scanner.h - incremental (time-sliced) library scanner over an abstract FS.
 *
 * Sources are a table of roots; each root has a source kind:
 *   SRC_HDD_ISO_DIR       a folder of disc images (PS3ISO, PS2ISO, PSXISO...)
 *   SRC_HDD_GAME_FOLDERS  a folder of PS3 game folders (GAMES, GAMEZ)
 *   SRC_NTFS_INDEX        webMAN MOD's index of NTFS/exFAT drives
 *                         (/dev_hdd0/tmp/wmtmp/<name>.ntfs[PS3ISO|PSXISO]).
 *
 * NTFS: the PS3 can't read NTFS itself; webMAN MOD's prepNTFS walks every
 * NTFS drive it has mounted (any number at once) and writes, per game, a
 * pointer file "<name>.ntfs[PS3ISO]" (rawseciso arguments: a 20-byte header
 * - u64 device, u32 emu mode, u32 sections, u32 tracks - then the sections'
 * start sectors and their lengths in 512-byte sectors, big-endian) plus the
 * PS3 game's PARAM.SFO as "<name>.SFO" and its icon/cover as "<name>.PNG".
 * Those files are what webMAN mounts, so spookyflow lists them and launches
 * them like any other path. (webMAN MOD source: include/scan/prepntfs.h,
 * include/mount/rawseciso.h.) prepNTFS indexes PS3ISO, BDISO, DVDISO and
 * PSXISO folders (at the drive root and under /PS3/), not PS2ISO. */
#ifndef SF_SCANNER_H
#define SF_SCANNER_H

#include <stdint.h>
#include "library.h"
#include "discid.h"     /* ReadAtFn */

#define FS_NAME_MAX     256
#define SCAN_DIR_MAX    4096    /* entries read per directory (wmtmp holds the NTFS index + webMAN's covers) */
#define SCAN_QUEUE_MAX  64      /* subfolders queued per root (one level deep) */
#define SCAN_PATH_MAX   512

typedef struct {
    char name[FS_NAME_MAX];
    int is_dir;
    uint64_t size;
} FsEntry;

/* List a directory. Return entry count (<= max), or -1 if it can't be opened. */
typedef int (*FsListFn)(void *ctx, const char *dir, FsEntry *out, int max);

enum { SRC_HDD_ISO_DIR = 0, SRC_HDD_GAME_FOLDERS = 1, SRC_NTFS_INDEX = 2 };

#define NTFS_INDEX_DIR   "/dev_hdd0/tmp/wmtmp"
#define NTFS_SECTOR      512u

typedef struct {
    int plat;
    const char *path;
    int kind;
} ScanRoot;

typedef struct {
    const ScanRoot *roots;
    int nroots;
    int root_i;
    int phase;                       /* 0 = list root next, 1 = walking queued subfolders */
    char queue[SCAN_QUEUE_MAX][SCAN_PATH_MAX];
    int qn, qi;
    int found[PLAT_COUNT];
    int found_ntfs;                  /* of found[], games from NTFS drives */
    ReadAtFn rd;                     /* optional: read files (NTFS sizes and title IDs) */
    void *rd_ctx;
    int dirs_listed, errors, skipped_long;
    int done;
} Scanner;

/* Default /dev_hdd0 roots. */
const ScanRoot *scanner_default_roots(int *n);

void  scanner_begin(Scanner *s, const ScanRoot *roots, int nroots);
/* Let the scanner read files (call after scanner_begin). Without it, NTFS
 * games still show, but with no size and no title ID from their .SFO. */
void  scanner_set_reader(Scanner *s, ReadAtFn rd, void *ctx);

/* NTFS index helpers (unit tested). */
/* "Name.ntfs[PS3ISO]" -> PLAT_PS3 (PSXISO -> PLAT_PS1), stem = "Name";
 * -1 for other kinds (BDISO, DVDISO, PSPISO...) and other files. */
int      scanner_ntfs_kind(const char *name, char *stem, size_t cap);
/* Size of the image a pointer file describes (sum of its sections), 0 if
 * the file doesn't look right. */
uint64_t scanner_ntfs_size(const unsigned char *buf, size_t len);
/* Do one directory's worth of work. Returns 1 once everything is scanned. */
int   scanner_step(Scanner *s, Library *lib, FsListFn list, void *ctx);
/* Platform currently being scanned (for "Scanning PS2 games…"). */
int   scanner_platform(const Scanner *s);
float scanner_progress(const Scanner *s);

#endif
