/* gamedb.h - release year by title ID (table from GameDB, see gamedb_data.c). */
#ifndef SF_GAMEDB_H
#define SF_GAMEDB_H

#include <stdint.h>

extern const char g_gamedb_prefix[][5];
extern const int g_gamedb_nprefix;
extern const uint32_t g_gamedb_key[];
extern const uint8_t g_gamedb_year[];
extern const int g_gamedb_count;

/* id like "SLUS20914" (any case, '-'/'_'/'.' allowed). Returns the release
 * year, or 0 if unknown. Read-only: safe from any thread. */
int gamedb_year(const char *id);

#endif
