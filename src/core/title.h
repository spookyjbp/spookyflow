/* title.h - display-title cleanup, title-ID extraction, sort keys. */
#ifndef SF_TITLE_H
#define SF_TITLE_H

#include <stddef.h>

#define TITLE_MAX 128
#define TITLEID_MAX 16

/* Clean a raw file/folder name into a display title:
 *  - drop directories and known image extensions (.iso, .iso.0, .bin, .cue ...)
 *  - drop anything inside () [] {} (region, languages, IDs)
 *  - underscores -> spaces only if the name has no spaces
 *  - collapse whitespace, trim, drop trailing " -" / "_" / "," junk */
void title_clean(const char *raw, char *out, size_t cap);

/* Extract a title ID such as "SLUS20914" from tags like "[SLUS_209.14]",
 * "[BLUS-30443]" or a leading "BLUS30443-Name". Returns 1 if found. */
int title_extract_id(const char *raw, char *out, size_t cap);
/* Normalize s[0..n) like "SLUS_209.14", "BLES-80608" or "slus20914" to
 * "SLUS20914" (4 letters + 5 digits). Returns 1 on success. */
int title_normalize_id(const char *s, size_t n, char *out, size_t cap);
/* "Final Fantasy VII (Disc 2)" -> base "Final Fantasy VII", *disc = 2.
 * Returns 0 (base = title, *disc = 0) if the title has no disc number. */
int title_disc_split(const char *title, char *base, size_t cap, int *disc);

/* Pointer into `title` where sorting starts (skips a leading "The " when
 * ignore_the is set). */
const char *title_sort_key(const char *title, int ignore_the);

/* Letter bucket: 0 = '#' (digits/symbols/non-ASCII), 1..26 = A..Z. */
int title_bucket(const char *title, int ignore_the);

/* Compare two titles for library order: bucket first, then case-insensitive. */
int title_compare(const char *a, const char *b, int ignore_the);

/* Character shown for a bucket ('#', 'A'..'Z'). */
char title_bucket_char(int bucket);

#endif
