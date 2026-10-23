/* cover_cache.h - fixed-budget cover texture residency.
 *
 * Each slot owns one preallocated texture (render.h TEX_COVER0 + slot). The
 * loader (coverart.c) queues a slot for the cover worker thread and returns
 * CC_LOADING; when the worker is done the app calls cc_mark(). Covers are
 * never all decoded at once: only the `budget` games around the selection
 * are resident.
 *
 * GPU safety: the RSX may still be drawing the previous frame while the next
 * one is prepared, so a slot that was drawn (cc_lookup) is not handed out
 * for new pixels until CC_GPU_FRAMES frames later. */
#ifndef SF_COVER_CACHE_H
#define SF_COVER_CACHE_H

#include <stdint.h>

#define CC_MAX_SLOTS        32
#define CC_DEFAULT_BUDGET   30
#define CC_LOADS_PER_FRAME  4
#define CC_GPU_FRAMES       3

enum { CC_EMPTY = 0, CC_LOADING = 1, CC_READY = 2, CC_FAILED = 3 };

typedef struct {
    int state;
    int tab, index;          /* which game this slot holds */
    int tex;                 /* backend texture handle, -1 = placeholder */
    uint32_t last_used;      /* frame counter */
    uint32_t free_at;        /* frame from which the slot's pixels may be rewritten */
    uint32_t ready_frame;    /* frame it became READY (fade-in) */
} CcSlot;

/* Loader: fill `slot` for (tab, index). Return CC_READY, CC_LOADING (async,
 * call cc_mark later) or CC_FAILED. A LOADING slot may be reassigned to
 * another game before it finishes: the loader is simply called again for
 * the same slot_id and must forget the earlier request. */
typedef int (*CcLoadFn)(void *user, int tab, int index, int slot_id, CcSlot *slot);

typedef struct {
    CcSlot slot[CC_MAX_SLOTS];
    int budget;
    uint32_t frame;
    CcLoadFn load;
    void *user;
} CoverCache;

void cc_init(CoverCache *cc, int budget, CcLoadFn load, void *user);
/* Drop everything (library rescanned / re-sorted). */
void cc_invalidate(CoverCache *cc);
/* Once per frame: keep the `budget` games nearest to `center` resident,
 * loading at most CC_LOADS_PER_FRAME new ones (nearest first) and evicting
 * the least useful (other tab, then farthest, then least recently used). */
void cc_update(CoverCache *cc, int tab, int center, int count);
/* Slot holding (tab, index) or -1. Touches it. */
int  cc_lookup(CoverCache *cc, int tab, int index);
int  cc_resident_count(const CoverCache *cc);
/* Async load finished for slot: tex >= 0 = READY with that texture, else FAILED. */
void cc_mark(CoverCache *cc, int slot, int tex);
/* Frames since the slot became READY (0 when not ready). */
uint32_t cc_ready_age(const CoverCache *cc, int slot);

#endif
