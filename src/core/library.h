/* library.h - per-platform game lists, sorting, letter table. Fixed memory. */
#ifndef SF_LIBRARY_H
#define SF_LIBRARY_H

#include <stddef.h>
#include <stdint.h>
#include "layout.h"   /* PLAT_* */

#define LIB_MAX_GAMES   2000          /* per platform tab */
#define LIB_POOL_SIZE   (640 * 1024)  /* shared string pool */
#define LIB_BUCKETS     27            /* 0 = '#', 1..26 = A..Z */
#define LIB_MAX_DISCS   1024          /* discs of multi-disc games (all tabs) */
#define LIB_DISCS_PER_GAME 8

enum { GAME_DEMO = 1, GAME_DIR = 2 /* a game folder, not an image file */ };

typedef struct {
    uint32_t title;     /* pool offset: cleaned display title */
    uint32_t path;      /* pool offset: full path (or demo file name) */
    uint32_t id;        /* pool offset: title ID ("" if unknown) */
    uint64_t size;      /* bytes, 0 = unknown */
    const char *genre;  /* static string or NULL */
    uint8_t pmin, pmax; /* players, 0 = unknown */
    uint8_t bucket;
    uint8_t flags;
    uint8_t ndisc;      /* >1: a multi-disc game, its discs are lib->disc[disc0 ..] */
    uint16_t disc0;
} Game;

typedef struct {
    uint32_t path, id;  /* pool offsets */
    uint64_t size;
    uint8_t num;        /* disc number from the name, 1..; 0 = none (see label) */
    uint32_t label;     /* pool offset: name for a disc without a number ("USA, Bonus Disc") */
} Disc;

typedef struct {
    Game games[LIB_MAX_GAMES];
    int count;
    int first[LIB_BUCKETS];   /* first index of each bucket, -1 if empty */
    int demo;                 /* 1 if this tab shows the built-in demo list */
    int dropped;              /* games skipped because the tab was full */
} GameList;

typedef struct {
    GameList tab[PLAT_COUNT];
    Disc disc[LIB_MAX_DISCS];
    int ndiscs;
    char pool[LIB_POOL_SIZE];
    uint32_t pool_used;
    int pool_full;
} Library;

void lib_clear(Library *lib);
void lib_clear_tab(Library *lib, int plat);
/* Add a game. raw_name is the file/folder name used for the display title
 * and ID. Returns its index or -1 when full. */
int  lib_add(Library *lib, int plat, const char *path, const char *raw_name, uint64_t size,
             const char *genre, int pmin, int pmax, int flags);
const char *lib_str(const Library *lib, uint32_t off);
const char *lib_title(const Library *lib, const Game *g);
const char *lib_path(const Library *lib, const Game *g);
const char *lib_id(const Library *lib, const Game *g);
/* Key for per-game settings: title ID if known, else the file name. */
const char *lib_key(const Library *lib, const Game *g);

/* Sort a tab alphabetically (case-insensitive, '#' bucket first) and rebuild first[]. */
void lib_sort(Library *lib, int plat, int ignore_the);
/* Find a game by key in a tab, -1 if absent. */
int  lib_find_key(const Library *lib, int plat, const char *key);

/* Letter jumps. They stop at the first/last letter (no wrap): return sel. */
int  lib_letter_prev(const GameList *gl, int sel);
int  lib_letter_next(const GameList *gl, int sel);

/* Merge games that share a title once disc numbers are left out --
 * "Name (Disc 1)", "Name (Disc 2)", "Name CD3", and also "Name (USA)" with
 * "Name (USA) (Bonus Disc)" -- into one game "Name" (path/ID of its first
 * disc, sizes added) whose discs are listed in lib->disc. Call once after
 * scanning. Returns the number of games merged away. */
int  lib_group_discs(Library *lib, int plat);
/* Disc i (0..ndisc-1, in disc order) of a multi-disc game, NULL otherwise. */
const Disc *lib_game_disc(const Library *lib, const Game *g, int i);
/* "Disc 2", or the disc's own label ("USA, Bonus Disc"). */
void lib_disc_name(const Library *lib, const Disc *d, int i, char *out, size_t cap);

/* Built-in demo library (demo.c). Fills a tab with demo games. */
void demo_fill(Library *lib, int plat);
int  demo_count(int plat);

#endif
