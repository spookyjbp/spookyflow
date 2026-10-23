/* library.c - per-platform game lists, sorting, letter table. */
#include "library.h"
#include "title.h"
#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void lib_clear(Library *lib)
{
    int p;
    lib->pool[0] = '\0';
    lib->pool_used = 1;           /* offset 0 is the empty string */
    lib->pool_full = 0;
    lib->ndiscs = 0;
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

const Disc *lib_game_disc(const Library *lib, const Game *g, int i)
{
    if (!g || g->ndisc < 2 || i < 0 || i >= g->ndisc || g->disc0 + i >= lib->ndiscs) return NULL;
    return &lib->disc[g->disc0 + i];
}

/* Name for a disc without a number: the bracket tags of its file name,
 * e.g. ".../Mortal Kombat - Deception (USA) (Bonus Disc).iso" -> "USA, Bonus Disc". */
static void disc_label(const char *path, char *out, size_t cap)
{
    const char *base = path, *p;
    size_t o = 0;
    int depth = 0;
    for (p = path; *p; p++) if (*p == '/') base = p + 1;
    out[0] = '\0';
    for (p = base; *p && o + 3 < cap; p++) {
        if (*p == '(' || *p == '[') {
            if (depth++ == 0 && o > 0) { out[o++] = ','; out[o++] = ' '; }
            continue;
        }
        if (*p == ')' || *p == ']') { if (depth > 0) depth--; continue; }
        if (depth > 0) out[o++] = *p;
    }
    out[o] = '\0';
    if (!out[0]) {                                   /* no tags: the file name */
        sf_strlcpy(out, base, cap);
        p = strrchr(out, '.');
        if (p && p != out) out[p - out] = '\0';
    }
}

void lib_disc_name(const Library *lib, const Disc *d, int i, char *out, size_t cap)
{
    if (d && d->num > 0) snprintf(out, cap, "Disc %d", d->num);
    else if (d && d->label) sf_strlcpy(out, lib_str(lib, d->label), cap);
    else snprintf(out, cap, "Disc %d", i + 1);
}

static int disc_after(const Library *lib, const Disc *x, const Disc *y)
{
    int kx = x->num ? x->num : 255, ky = y->num ? y->num : 255;
    size_t lx, ly;
    if (kx != ky) return kx > ky;
    lx = strlen(lib_str(lib, x->path)); ly = strlen(lib_str(lib, y->path));
    if (lx != ly) return lx > ly;
    return strcmp(lib_str(lib, x->path), lib_str(lib, y->path)) > 0;
}

int lib_group_discs(Library *lib, int plat)
{
    GameList *gl;
    int i, merged = 0;
    char base[TITLE_MAX], base2[TITLE_MAX], lab[TITLE_MAX];
    if (plat < 0 || plat >= PLAT_COUNT) return 0;
    gl = &lib->tab[plat];
    if (gl->count < 2) return 0;
    lib_sort(lib, plat, 0);              /* discs of one game end up next to each other */
    for (i = 0; i < gl->count; i++) {
        int num, j, a, b, n = 1, members[LIB_DISCS_PER_GAME];
        Disc tmp[LIB_DISCS_PER_GAME];
        uint64_t total = 0;
        uint32_t t;
        title_disc_split(lib_title(lib, &gl->games[i]), base, sizeof(base), &num);
        members[0] = i;
        for (j = i + 1; j < gl->count && j <= i + 2 * LIB_DISCS_PER_GAME && n < LIB_DISCS_PER_GAME; j++) {
            int num2;
            title_disc_split(lib_title(lib, &gl->games[j]), base2, sizeof(base2), &num2);
            if (sf_stricmp(base, base2) == 0) members[n++] = j;
            else if (sf_strnicmp(lib_title(lib, &gl->games[j]), base, strlen(base)) != 0) break;
        }
        if (n < 2 || lib->ndiscs + n > LIB_MAX_DISCS) continue;
        for (a = 0; a < n; a++) {
            const Game *m = &gl->games[members[a]];
            int dn;
            title_disc_split(lib_title(lib, m), base2, sizeof(base2), &dn);
            tmp[a].path = m->path; tmp[a].id = m->id; tmp[a].size = m->size; tmp[a].num = (uint8_t)dn;
            tmp[a].label = 0;
            if (dn == 0) { disc_label(lib_path(lib, m), lab, sizeof(lab)); tmp[a].label = pool_add(lib, lab); }
            total += m->size;
        }
        /* numbered discs in order, then the others (the plainest name first,
         * so "Game (USA)" comes before "Game (USA) (Bonus Disc)") */
        for (a = 1; a < n; a++)
            for (b = a; b > 0 && disc_after(lib, &tmp[b - 1], &tmp[b]); b--) {
                Disc d = tmp[b]; tmp[b] = tmp[b - 1]; tmp[b - 1] = d;
            }
        t = pool_add(lib, base);
        if (t == 0) continue;                               /* pool full: leave them separate */
        for (a = 0; a < n; a++) lib->disc[lib->ndiscs + a] = tmp[a];
        {
            Game *g = &gl->games[i];
            g->title = t;
            g->path = tmp[0].path;
            g->id = tmp[0].id;
            g->size = total;
            g->ndisc = (uint8_t)n;
            g->disc0 = (uint16_t)lib->ndiscs;
            g->bucket = (uint8_t)title_bucket(base, 0);
        }
        lib->ndiscs += n;
        /* drop the other discs' entries (all after i) */
        {
            int r, w = i + 1, c;
            for (r = i + 1; r < gl->count; r++) {
                int is_member = 0;
                for (c = 1; c < n; c++) if (members[c] == r) is_member = 1;
                if (!is_member) gl->games[w++] = gl->games[r];
            }
            merged += gl->count - w;
            gl->count = w;
        }
    }
    return merged;
}
