/* scanner.h - incremental (time-sliced) library scanner over an abstract FS.
 *
 * Sources are a table of roots; each root has a source kind. Milestone 1
 * implements SRC_HDD_ISO_DIR and SRC_HDD_GAME_FOLDERS. A later SRC_NTFS_INDEX
 * (reading a prepared index of NTFS/exFAT drives) slots into the same table
 * and step() switch without touching the UI. */
#ifndef SF_SCANNER_H
#define SF_SCANNER_H

#include <stdint.h>
#include "library.h"

#define FS_NAME_MAX     256
#define SCAN_DIR_MAX    1024    /* entries read per directory */
#define SCAN_QUEUE_MAX  64      /* subfolders queued per root (one level deep) */
#define SCAN_PATH_MAX   512

typedef struct {
    char name[FS_NAME_MAX];
    int is_dir;
    uint64_t size;
} FsEntry;

/* List a directory. Return entry count (<= max), or -1 if it can't be opened. */
typedef int (*FsListFn)(void *ctx, const char *dir, FsEntry *out, int max);

enum { SRC_HDD_ISO_DIR = 0, SRC_HDD_GAME_FOLDERS = 1 /* , SRC_NTFS_INDEX (later) */ };

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
    int dirs_listed, errors, skipped_long;
    int done;
} Scanner;

/* Default /dev_hdd0 roots. */
const ScanRoot *scanner_default_roots(int *n);

void  scanner_begin(Scanner *s, const ScanRoot *roots, int nroots);
/* Do one directory's worth of work. Returns 1 once everything is scanned. */
int   scanner_step(Scanner *s, Library *lib, FsListFn list, void *ctx);
/* Platform currently being scanned (for "Scanning PS2 games…"). */
int   scanner_platform(const Scanner *s);
float scanner_progress(const Scanner *s);

#endif
