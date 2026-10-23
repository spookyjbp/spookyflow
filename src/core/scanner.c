/* scanner.c - incremental library scanner. */
#include "scanner.h"
#include "title.h"
#include "log.h"
#include "util.h"
#include <stdio.h>
#include <string.h>

static const ScanRoot k_roots[] = {
    { PLAT_PS1, "/dev_hdd0/PSXISO",   SRC_HDD_ISO_DIR },
    { PLAT_PS1, "/dev_hdd0/PSXGAMES", SRC_HDD_ISO_DIR },
    { PLAT_PS2, "/dev_hdd0/PS2ISO",   SRC_HDD_ISO_DIR },
    { PLAT_PS3, "/dev_hdd0/PS3ISO",   SRC_HDD_ISO_DIR },
    { PLAT_PS3, "/dev_hdd0/GAMES",    SRC_HDD_GAME_FOLDERS },
    { PLAT_PS3, "/dev_hdd0/GAMEZ",    SRC_HDD_GAME_FOLDERS },
};

/* Scratch buffers (single scanner instance; keeps the stack small). */
static FsEntry s_ent[SCAN_DIR_MAX];
#define CUE_MAX 256
static char s_cue_title[CUE_MAX][TITLE_MAX];
static int s_cue_game[CUE_MAX];

const ScanRoot *scanner_default_roots(int *n)
{
    *n = SF_ARRAY_LEN(k_roots);
    return k_roots;
}

void scanner_begin(Scanner *s, const ScanRoot *roots, int nroots)
{
    memset(s, 0, sizeof(*s));
    s->roots = roots;
    s->nroots = nroots;
}

static int join(char *out, size_t cap, const char *dir, const char *name)
{
    int n = snprintf(out, cap, "%s/%s", dir, name);
    return n > 0 && (size_t)n < cap;
}

/* ".iso" or ".iso.N" -> returns N (0 for plain .iso is reported as -1) */
static int iso_part(const char *name, size_t *stem_len)
{
    size_t n = strlen(name), k = n;
    while (k > 0 && sf_isdigit(name[k - 1])) k--;
    if (k < n && k >= 5 && name[k - 1] == '.' && sf_strnicmp(name + k - 5, ".iso", 4) == 0) {
        int v = 0; size_t i;
        for (i = k; i < n && v < 100000; i++) v = v * 10 + (name[i] - '0');
        *stem_len = k;          /* includes the trailing '.' */
        return v;
    }
    if (sf_ends_with_i(name, ".iso")) { *stem_len = n; return -1; }
    return -2;
}

static int is_ps1_image(const char *name)
{
    return sf_ends_with_i(name, ".bin") || sf_ends_with_i(name, ".img") ||
           sf_ends_with_i(name, ".iso") || sf_ends_with_i(name, ".mdf");
}

static void scan_iso_dir(Scanner *s, Library *lib, int plat, const char *dir, int n, int depth)
{
    int i, j, ncue = 0;
    char path[SCAN_PATH_MAX];

    for (i = 0; i < n; i++) {
        const FsEntry *e = &s_ent[i];
        if (e->name[0] == '.' ) continue;
        if (e->is_dir) {
            if (depth == 0 && s->qn < SCAN_QUEUE_MAX && join(path, sizeof(path), dir, e->name))
                sf_strlcpy(s->queue[s->qn++], path, SCAN_PATH_MAX);
            continue;
        }
        if (plat == PLAT_PS1) {
            if (sf_ends_with_i(e->name, ".cue") && join(path, sizeof(path), dir, e->name)) {
                int gi = lib_add(lib, plat, path, e->name, e->size, NULL, 0, 0, 0);
                if (gi >= 0) {
                    s->found[plat]++;
                    if (ncue < CUE_MAX) {
                        title_clean(e->name, s_cue_title[ncue], TITLE_MAX);
                        s_cue_game[ncue] = gi;
                        ncue++;
                    }
                }
            }
        } else {
            size_t stem;
            int part = iso_part(e->name, &stem);
            if (part == -1 || part == 0) {
                uint64_t size = e->size;
                if (part == 0) {      /* split image: sum .iso.1, .iso.2 ... */
                    for (j = 0; j < n; j++) {
                        size_t st2;
                        int p2;
                        if (j == i || s_ent[j].is_dir) continue;
                        p2 = iso_part(s_ent[j].name, &st2);
                        if (p2 > 0 && st2 == stem && strncmp(s_ent[j].name, e->name, stem) == 0) size += s_ent[j].size;
                    }
                }
                if (join(path, sizeof(path), dir, e->name) &&
                    lib_add(lib, plat, path, e->name, size, NULL, 0, 0, 0) >= 0)
                    s->found[plat]++;
            }
        }
    }

    if (plat == PLAT_PS1) {
        /* .bin/.img: attach to a .cue with the same cleaned title, else standalone */
        for (i = 0; i < n; i++) {
            const FsEntry *e = &s_ent[i];
            char t[TITLE_MAX];
            int matched = 0;
            if (e->is_dir || e->name[0] == '.' || !is_ps1_image(e->name)) continue;
            title_clean(e->name, t, sizeof(t));
            for (j = 0; j < ncue; j++) {
                if (sf_stricmp(t, s_cue_title[j]) == 0) {
                    lib->tab[plat].games[s_cue_game[j]].size += e->size;
                    matched = 1;
                    break;
                }
            }
            if (!matched && join(path, sizeof(path), dir, e->name) &&
                lib_add(lib, plat, path, e->name, e->size, NULL, 0, 0, 0) >= 0)
                s->found[plat]++;
        }
    }
}

static void scan_folders(Scanner *s, Library *lib, int plat, const char *dir, int n)
{
    int i;
    char path[SCAN_PATH_MAX];
    for (i = 0; i < n; i++) {
        const FsEntry *e = &s_ent[i];
        if (!e->is_dir || e->name[0] == '.') continue;
        /* size unknown in milestone 1 (would need a recursive walk) */
        if (join(path, sizeof(path), dir, e->name) &&
            lib_add(lib, plat, path, e->name, 0, NULL, 0, 0, GAME_DIR) >= 0)
            s->found[plat]++;
    }
}

static void process(Scanner *s, Library *lib, const ScanRoot *r, const char *dir, int depth, FsListFn list, void *ctx)
{
    int n = list(ctx, dir, s_ent, SCAN_DIR_MAX);
    s->dirs_listed++;
    if (n < 0) {
        if (depth == 0) log_printf("scan: %s not found", dir);
        else { log_printf("scan: cannot open %s", dir); s->errors++; }
        return;
    }
    if (n >= SCAN_DIR_MAX) log_printf("scan: %s has more than %d entries; extra ignored", dir, SCAN_DIR_MAX);
    log_printf("scan: %s (%d entries)", dir, n);
    switch (r->kind) {
    case SRC_HDD_GAME_FOLDERS: if (depth == 0) scan_folders(s, lib, r->plat, dir, n); break;
    case SRC_HDD_ISO_DIR:
    default: scan_iso_dir(s, lib, r->plat, dir, n, depth); break;
    }
}

int scanner_step(Scanner *s, Library *lib, FsListFn list, void *ctx)
{
    const ScanRoot *r;
    if (s->done) return 1;
    if (s->root_i >= s->nroots) { s->done = 1; return 1; }
    r = &s->roots[s->root_i];
    if (s->phase == 0) {
        s->qn = 0; s->qi = 0;
        process(s, lib, r, r->path, 0, list, ctx);
        s->phase = 1;
    } else if (s->qi < s->qn) {
        process(s, lib, r, s->queue[s->qi], 1, list, ctx);
        s->qi++;
    } else {
        s->root_i++;
        s->phase = 0;
    }
    if (s->root_i >= s->nroots) s->done = 1;
    return s->done;
}

int scanner_platform(const Scanner *s)
{
    if (s->nroots <= 0) return PLAT_PS3;
    if (s->root_i >= s->nroots) return s->roots[s->nroots - 1].plat;
    return s->roots[s->root_i].plat;
}

float scanner_progress(const Scanner *s)
{
    float sub = 0.0f;
    if (s->nroots <= 0 || s->done) return 1.0f;
    if (s->phase == 1) sub = (float)(s->qi + 1) / (float)(s->qn + 1);
    return sf_clampf(((float)s->root_i + sub) / (float)s->nroots, 0.0f, 1.0f);
}
