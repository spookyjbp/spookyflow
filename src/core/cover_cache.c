/* cover_cache.c - fixed-budget cover texture residency. */
#include "cover_cache.h"
#include <string.h>

void cc_init(CoverCache *cc, int budget, CcLoadFn load, void *user)
{
    memset(cc, 0, sizeof(*cc));
    if (budget < 1) budget = 1;
    if (budget > CC_MAX_SLOTS) budget = CC_MAX_SLOTS;
    cc->budget = budget;
    cc->window = budget < CC_MAIN_WINDOW ? budget : CC_MAIN_WINDOW;
    cc->load = load;
    cc->user = user;
    cc_invalidate(cc);
}

void cc_invalidate(CoverCache *cc)
{
    int i;
    for (i = 0; i < CC_MAX_SLOTS; i++) {
        cc->slot[i].state = CC_EMPTY;
        cc->slot[i].tab = -1;
        cc->slot[i].index = -1;
        cc->slot[i].tex = -1;
        cc->slot[i].last_used = 0;
        cc->slot[i].ready_frame = 0;
        /* free_at is kept: the GPU may still be drawing these pixels */
    }
}

static int find(const CoverCache *cc, int tab, int index)
{
    int i;
    for (i = 0; i < cc->budget; i++)
        if (cc->slot[i].state != CC_EMPTY && cc->slot[i].tab == tab && cc->slot[i].index == index) return i;
    return -1;
}

int cc_lookup(CoverCache *cc, int tab, int index)
{
    int i = find(cc, tab, index);
    if (i >= 0) {
        cc->slot[i].last_used = cc->frame;
        cc->slot[i].free_at = cc->frame + CC_GPU_FRAMES;
    }
    return i;
}

int cc_resident_count(const CoverCache *cc)
{
    int i, n = 0;
    for (i = 0; i < cc->budget; i++) if (cc->slot[i].state != CC_EMPTY) n++;
    return n;
}

static int dist(int a, int b) { return a > b ? a - b : b - a; }

void cc_set_side(CoverCache *cc, int tab, int center, int count)
{
    if (tab < 0 || tab >= CC_TABS) return;
    cc->side_center[tab] = center;
    cc->side_count[tab] = count > 0 ? count : 0;
}

static int wanted(const CoverCache *cc, const CcSlot *s)
{
    return s->tab >= 0 && s->tab < CC_TABS && s->index >= cc->lo[s->tab] && s->index <= cc->hi[s->tab];
}

/* Pick a slot to (re)use: empty first, then unwanted slots of other tabs
 * (least recently used), then unwanted slots of this tab (farthest first). */
static int victim(const CoverCache *cc, int tab, int center)
{
    int i, best = -1;
    long best_score = -1;
    for (i = 0; i < cc->budget; i++) {
        const CcSlot *s = &cc->slot[i];
        long score;
        if (cc->frame < s->free_at) continue;          /* drawn too recently */
        if (s->state == CC_EMPTY) return i;
        if (wanted(cc, s)) continue;
        if (s->tab != tab) score = 1000000L + (long)((cc->frame - s->last_used) & 0xFFFF);
        else score = (long)dist(s->index, center) * 1000L + (long)((cc->frame - s->last_used) & 0x3FF);
        if (score > best_score) { best_score = score; best = i; }
    }
    return best;
}

static void window_of(int center, int count, int span, int *lo, int *hi)
{
    if (span > count) span = count;
    *lo = center - span / 2;
    if (*lo < 0) *lo = 0;
    *hi = *lo + span - 1;
    if (*hi > count - 1) { *hi = count - 1; *lo = *hi - span + 1; }
}

/* Load missing games of one window, nearest to center first. Returns 0 if
 * no slot could be found (stop for this frame). */
static int fill(CoverCache *cc, int tab, int center, int count, int *loads)
{
    int k, lo = cc->lo[tab], hi = cc->hi[tab];
    for (k = 0; k <= 2 * count && *loads < CC_LOADS_PER_FRAME; k++) {
        int off = (k + 1) / 2 * ((k & 1) ? 1 : -1);
        int idx = center + off, s;
        if (idx < lo - 1 && center + (k + 2) / 2 > hi) break;   /* both sides exhausted */
        if (idx < lo || idx > hi) continue;
        s = find(cc, tab, idx);
        if (s >= 0) { cc->slot[s].last_used = cc->frame; continue; }
        s = victim(cc, tab, center);
        if (s < 0) return 0;
        cc->slot[s].tab = tab;
        cc->slot[s].index = idx;
        cc->slot[s].tex = -1;
        cc->slot[s].last_used = cc->frame;
        cc->slot[s].state = CC_LOADING;
        cc->slot[s].state = cc->load ? cc->load(cc->user, tab, idx, s, &cc->slot[s]) : CC_READY;
        cc->slot[s].ready_frame = cc->frame;
        (*loads)++;
    }
    return 1;
}

void cc_update(CoverCache *cc, int tab, int center, int count)
{
    int loads = 0, t;
    cc->frame++;
    for (t = 0; t < CC_TABS; t++) { cc->lo[t] = 1; cc->hi[t] = 0; }
    if (count <= 0 || tab < 0 || tab >= CC_TABS) return;
    if (center < 0) center = 0;
    if (center >= count) center = count - 1;
    window_of(center, count, cc->window, &cc->lo[tab], &cc->hi[tab]);
    /* other tabs keep a smaller window, if there is room for it */
    if (cc->budget >= cc->window + CC_SIDE_WINDOW) {
        for (t = 0; t < CC_TABS; t++) {
            int sc = cc->side_center[t], n = cc->side_count[t];
            if (t == tab || n <= 0) continue;
            if (sc < 0) sc = 0;
            if (sc >= n) sc = n - 1;
            window_of(sc, n, CC_SIDE_WINDOW, &cc->lo[t], &cc->hi[t]);
        }
    }
    if (!fill(cc, tab, center, count, &loads)) return;
    for (t = 0; t < CC_TABS && loads < CC_LOADS_PER_FRAME; t++) {
        if (t == tab || cc->lo[t] > cc->hi[t]) continue;
        if (!fill(cc, t, cc->side_center[t] < 0 ? 0 : (cc->side_center[t] >= cc->side_count[t] ? cc->side_count[t] - 1 : cc->side_center[t]),
                  cc->side_count[t], &loads)) return;
    }
}

void cc_mark(CoverCache *cc, int slot, int tex)
{
    CcSlot *s;
    if (slot < 0 || slot >= cc->budget) return;
    s = &cc->slot[slot];
    if (s->state != CC_LOADING) return;
    s->tex = tex;
    s->state = tex >= 0 ? CC_READY : CC_FAILED;
    s->ready_frame = cc->frame;
}

uint32_t cc_ready_age(const CoverCache *cc, int slot)
{
    if (slot < 0 || slot >= cc->budget || cc->slot[slot].state != CC_READY) return 0;
    return cc->frame - cc->slot[slot].ready_frame;
}
