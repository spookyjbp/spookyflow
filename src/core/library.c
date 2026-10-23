/* library.c - per-platform game lists, sorting, letter table. */
#include "library.h"
#include "title.h"
#include "util.h"
#include <stdlib.h>
#include <string.h>

void lib_clear(Library *lib)
{
    int p;
    lib->pool[0] = '\0';
    lib->pool_used = 1;           /* offset 0 is the empty string */
    lib->pool_full = 0;
    for (p = 0; p < PLAT_COUNT; p++) lib_clear_tab(lib, p);
}

void lib_clear_tab(Library *lib, int plat)
{
    int b;
    GameList *gl = &lib->tab[plat];
    gl->count = 0; gl->demo = 0; gl->dropped = 0;
    for (b = 0; b < LIB_BUCKETS; b++) gl->first[b] = -1;
}

static uint32_t pool_add(Library *lib, const char *s)
{
    size_t n = strlen(s) + 1;
    uint32_t off;
    if (n == 1) return 0;
    if (lib->pool_used + n > LIB_POOL_SIZE) { lib->pool_full = 1; return 0; }
    off = lib->pool_used;
    memcpy(lib->pool + off, s, n);
    lib->pool_used += (uint32_t)n;
    return off;
}

int lib_add(Library *lib, int plat, const char *path, const char *raw_name, uint64_t size,
            const char *genre, int pmin, int pmax, int flags)
{
    GameList *gl;
    Game *g;
    char title[TITLE_MAX], id[TITLEID_MAX];
    if (plat < 0 || plat >= PLAT_COUNT) return -1;
    gl = &lib->tab[plat];
    if (gl->count >= LIB_MAX_GAMES) { gl->dropped++; return -1; }
    title_clean(raw_name, title, sizeof(title));
    title_extract_id(raw_name, id, sizeof(id));
    g = &gl->games[gl->count];
    memset(g, 0, sizeof(*g));
    g->title = pool_add(lib, title);
    g->path = pool_add(lib, path);
    g->id = pool_add(lib, id);
    if (lib->pool_full) { gl->dropped++; return -1; }
    g->size = size;
    g->genre = genre;
    g->pmin = (uint8_t)sf_clampi(pmin, 0, 255);
    g->pmax = (uint8_t)sf_clampi(pmax, 0, 255);
    g->flags = (uint8_t)flags;
    g->bucket = (uint8_t)title_bucket(title, 0);
    return gl->count++;
}

const char *lib_str(const Library *lib, uint32_t off)
{
    if (off >= lib->pool_used) return "";
    return lib->pool + off;
}
const char *lib_title(const Library *lib, const Game *g) { return lib_str(lib, g->title); }
const char *lib_path(const Library *lib, const Game *g)  { return lib_str(lib, g->path); }
const char *lib_id(const Library *lib, const Game *g)    { return lib_str(lib, g->id); }

const char *lib_key(const Library *lib, const Game *g)
{
    const char *id = lib_id(lib, g), *p, *base;
    if (id[0]) return id;
    base = lib_path(lib, g);
    for (p = base; *p; p++) if (*p == '/') base = p + 1;
    return base;
}

/* qsort has no context argument; sorting runs on one thread only. */
static const Library *s_sort_lib;
static int s_sort_ignore_the;

static int cmp_games(const void *a, const void *b)
{
    const Game *ga = (const Game *)a, *gb = (const Game *)b;
    int c = title_compare(lib_title(s_sort_lib, ga), lib_title(s_sort_lib, gb), s_sort_ignore_the);
    if (c) return c;
    c = strcmp(lib_path(s_sort_lib, ga), lib_path(s_sort_lib, gb));
    if (c) return c;
    return (ga->path > gb->path) - (ga->path < gb->path);
}

void lib_sort(Library *lib, int plat, int ignore_the)
{
    GameList *gl = &lib->tab[plat];
    int i, b;
    s_sort_lib = lib;
    s_sort_ignore_the = ignore_the;
    if (gl->count > 1) qsort(gl->games, (size_t)gl->count, sizeof(Game), cmp_games);
    for (b = 0; b < LIB_BUCKETS; b++) gl->first[b] = -1;
    for (i = 0; i < gl->count; i++) {
        Game *g = &gl->games[i];
        g->bucket = (uint8_t)title_bucket(lib_title(lib, g), ignore_the);
        if (gl->first[g->bucket] < 0) gl->first[g->bucket] = i;
    }
}

int lib_find_key(const Library *lib, int plat, const char *key)
{
    const GameList *gl = &lib->tab[plat];
    int i;
    if (!key || !key[0]) return -1;
    for (i = 0; i < gl->count; i++)
        if (strcmp(lib_key(lib, &gl->games[i]), key) == 0) return i;
    return -1;
}

int lib_letter_prev(const GameList *gl, int sel)
{
    int b, k;
    if (gl->count <= 0) return 0;
    sel = sf_clampi(sel, 0, gl->count - 1);
    b = gl->games[sel].bucket;
    if (gl->first[b] >= 0 && sel > gl->first[b]) return gl->first[b];   /* go to start of this letter */
    for (k = b - 1; k >= 0; k--)                                         /* no wrap: stops at the first letter */
        if (gl->first[k] >= 0) return gl->first[k];
    return sel;
}

int lib_letter_next(const GameList *gl, int sel)
{
    int b, k;
    if (gl->count <= 0) return 0;
    sel = sf_clampi(sel, 0, gl->count - 1);
    b = gl->games[sel].bucket;
    for (k = b + 1; k < LIB_BUCKETS; k++)                                /* no wrap: stops at the last letter */
        if (gl->first[k] >= 0) return gl->first[k];
    return sel;
}
