/* sfo.h - read/patch 32-bit integer fields of a PS3 PARAM.SFO.
 * Used for ITEM_PRIORITY (position of spookyflow in the XMB Game column). */
#ifndef SF_SFO_H
#define SF_SFO_H

#include <stddef.h>
#include <stdint.h>

/* XMB position values: higher = nearer the top of the column. The maximum
 * positive 32-bit value is used so it stays "highest" whether the XMB reads
 * the field as signed or unsigned. */
#define SFO_PRIORITY_TOP     0x7FFFFFFFu
#define SFO_PRIORITY_NORMAL  0u

/* In-memory. Return 0 on success, -1 bad file, -2 key missing, -3 not an int32. */
int sfo_get_u32(const unsigned char *buf, size_t len, const char *key, uint32_t *out);
int sfo_set_u32(unsigned char *buf, size_t len, const char *key, uint32_t value);
/* String field (e.g. "TITLE_ID"), copied NUL-terminated into out. Return
 * codes as above (-3 = not a string). */
int sfo_get_str(const unsigned char *buf, size_t len, const char *key, char *out, size_t cap);

/* On disk (patches 4 bytes in place; never changes the file size).
 * Return codes as above, or -4 if the file can't be read/written. */
int sfo_file_get_u32(const char *path, const char *key, uint32_t *out);
int sfo_file_set_u32(const char *path, const char *key, uint32_t value);

#endif
