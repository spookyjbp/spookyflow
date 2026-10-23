/* gamedb.c - release year lookup (binary search in the generated table). */
#include "gamedb.h"
#include "title.h"
#include <string.h>

int gamedb_year(const char *id)
{
    char n[TITLEID_MAX];
    int p, lo, hi;
    uint32_t key, num = 0;
    if (!id || !id[0] || !title_normalize_id(id, strlen(id), n, sizeof(n))) return 0;
    for (p = 0; p < g_gamedb_nprefix; p++) if (memcmp(g_gamedb_prefix[p], n, 4) == 0) break;
    if (p == g_gamedb_nprefix) return 0;
    for (lo = 4; lo < 9; lo++) num = num * 10u + (uint32_t)(n[lo] - '0');
    key = ((uint32_t)p << 17) | num;
    lo = 0; hi = g_gamedb_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (g_gamedb_key[mid] == key) return 1994 + g_gamedb_year[mid];
        if (g_gamedb_key[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}
