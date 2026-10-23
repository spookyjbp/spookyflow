/* title.c - display-title cleanup, title-ID extraction, sort keys. */
#include "title.h"
#include "util.h"
#include <string.h>

static const char *const k_exts[] = {
    ".iso", ".bin", ".cue", ".img", ".mdf", ".mds", ".ccd", ".sub", ".chd",
    ".pbp", ".ecm", ".cso", ".zso", ".nrg", ".gz",
    ".dec", ".enc"   /* decrypted PS3 ISOs (Game.dec.iso), PS2 Classics (.BIN.ENC) */
};

/* Strip one known extension (optionally followed by a numeric split part
 * like ".iso.0") from s in place. Returns 1 if something was removed. */
static int strip_ext(char *s)
{
    size_t n = strlen(s), i;
    size_t end = n;
    /* numeric split suffix ".0", ".1", ... ".66600" */
    if (n > 2) {
        size_t k = n;
        while (k > 0 && sf_isdigit(s[k - 1])) k--;
        if (k < n && k > 0 && s[k - 1] == '.') {
            size_t cut = k - 1;
            for (i = 0; i < (size_t)SF_ARRAY_LEN(k_exts); i++) {
                size_t le = strlen(k_exts[i]);
                if (cut >= le && sf_strnicmp(s + cut - le, k_exts[i], le) == 0) { end = cut; break; }
            }
        }
    }
    for (i = 0; i < (size_t)SF_ARRAY_LEN(k_exts); i++) {
        size_t le = strlen(k_exts[i]);
        if (end > le && sf_strnicmp(s + end - le, k_exts[i], le) == 0) {
            s[end - le] = '\0';
            return 1;
        }
    }
    if (end != n) { s[end] = '\0'; return 1; }
    return 0;
}

/* If s starts a bracket group like "(Disc 2)", "[CD1]" or "(Disk 1 of 3)",
 * return its length including both brackets, else 0. */
static size_t disc_group_len(const char *s)
{
    static const char *const words[] = { "disc", "disk", "cd" };
    char close = s[0] == '(' ? ')' : s[0] == '[' ? ']' : '}';
    size_t w, n = 0;
    const char *q = s + 1;
    while (*q == ' ') q++;
    for (w = 0; w < SF_ARRAY_LEN(words); w++) {
        size_t lw = strlen(words[w]);
        if (sf_strnicmp(q, words[w], lw) == 0) { q += lw; break; }
    }
    if (w == SF_ARRAY_LEN(words)) return 0;
    while (*q == ' ' || *q == '-' || *q == '_') q++;
    if (!sf_isdigit(*q)) return 0;
    for (n = 1; s[n]; n++) {
        if (s[n] == close) return n + 1;
        if (s[n] == '(' || s[n] == '[' || s[n] == '{') return 0;
    }
    return 0;
}

void title_clean(const char *raw, char *out, size_t cap)
{
    char tmp[TITLE_MAX * 2];
    char res[TITLE_MAX * 2];
    const char *base = raw, *p;
    size_t i, o = 0;
    int depth = 0, has_space = 0, last_space = 1;

    if (cap == 0) return;
    for (p = raw; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    sf_strlcpy(tmp, base, sizeof(tmp));
    while (strip_ext(tmp)) { /* repeat for ".iso.0" etc. */ }

    /* remove bracketed groups, except disc numbers: "(Disc 2)" is kept so
     * multi-disc games stay distinguishable */
    for (i = 0; tmp[i]; i++) {
        char c = tmp[i];
        if (c == '(' || c == '[' || c == '{') {
            if (depth == 0) {
                size_t len = disc_group_len(tmp + i);
                if (len > 0) {
                    size_t k;
                    if (o + 1 < sizeof(res)) res[o++] = ' ';
                    if (o + 1 < sizeof(res)) res[o++] = '(';
                    for (k = 1; k + 1 < len && o + 1 < sizeof(res); k++) res[o++] = tmp[i + k];
                    if (o + 1 < sizeof(res)) res[o++] = ')';
                    i += len - 1;
                    continue;
                }
            }
            depth++;
            continue;
        }
        if ((c == ')' || c == ']' || c == '}') && depth > 0) { depth--; continue; }
        if (depth == 0 && o + 1 < sizeof(res)) res[o++] = c;
    }
    res[o] = '\0';

    for (i = 0; res[i]; i++) if (res[i] == ' ') { has_space = 1; break; }
    if (!has_space) for (i = 0; res[i]; i++) if (res[i] == '_') res[i] = ' ';

    /* collapse whitespace into tmp */
    o = 0;
    for (i = 0; res[i]; i++) {
        char c = res[i];
        if (sf_isspace(c)) {
            if (!last_space && o + 1 < sizeof(tmp)) tmp[o++] = ' ';
            last_space = 1;
        } else {
            if (o + 1 < sizeof(tmp)) tmp[o++] = c;
            last_space = 0;
        }
    }
    tmp[o] = '\0';
    /* trim trailing junk */
    while (o > 0 && (tmp[o - 1] == ' ' || tmp[o - 1] == '-' || tmp[o - 1] == '_' || tmp[o - 1] == ',')) tmp[--o] = '\0';
    /* trim leading junk */
    p = tmp;
    while (*p == ' ' || *p == '-' || *p == '_' || *p == ',') p++;

    if (*p == '\0') {
        /* everything was tags: fall back to the base name without extension */
        sf_strlcpy(tmp, base, sizeof(tmp));
        while (strip_ext(tmp)) { }
        p = tmp;
    }
    sf_strlcpy(out, p, cap);
}

/* Normalize candidate: keep alnum, require 4 letters + 5 digits. */
int title_normalize_id(const char *s, size_t n, char *out, size_t cap)
{
    char buf[TITLEID_MAX];
    size_t i, o = 0;
    for (i = 0; i < n; i++) {
        char c = s[i];
        if (sf_isalpha(c) || sf_isdigit(c)) {
            if (o + 1 >= sizeof(buf)) return 0;
            buf[o++] = sf_toupper(c);
        } else if (c == '_' || c == '-' || c == '.' || c == ' ') {
            continue;
        } else return 0;
    }
    buf[o] = '\0';
    if (o != 9) return 0;
    for (i = 0; i < 4; i++) if (!sf_isalpha(buf[i])) return 0;
    for (i = 4; i < 9; i++) if (!sf_isdigit(buf[i])) return 0;
    sf_strlcpy(out, buf, cap);
    return 1;
}

int title_extract_id(const char *raw, char *out, size_t cap)
{
    const char *p, *base = raw;
    int pass;
    for (p = raw; *p; p++) if (*p == '/' || *p == '\\') base = p + 1;
    for (pass = 0; pass < 2; pass++) {
        char open = pass == 0 ? '[' : '(', close = pass == 0 ? ']' : ')';
        for (p = base; *p; p++) {
            if (*p == open) {
                const char *q = strchr(p + 1, close);
                if (!q) break;
                if (title_normalize_id(p + 1, (size_t)(q - p - 1), out, cap)) return 1;
                p = q;
            }
        }
    }
    /* leading "BLUS30443-Name" / "BLUS30443 Name" / "BLUS30443" (folder style) */
    {
        size_t n = strlen(base), k = 0;
        if (n >= 9) {
            while (k < n && k < 11 && base[k] != '-' && base[k] != ' ' && base[k] != '[' && base[k] != '.') k++;
            if (title_normalize_id(base, k, out, cap)) return 1;
        }
    }
    if (cap) out[0] = '\0';
    return 0;
}

const char *title_sort_key(const char *title, int ignore_the)
{
    if (ignore_the && sf_strnicmp(title, "the ", 4) == 0 && title[4] != '\0') return title + 4;
    return title;
}

int title_bucket(const char *title, int ignore_the)
{
    const char *k = title_sort_key(title, ignore_the);
    char c = k[0];
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    return 0;
}

int title_compare(const char *a, const char *b, int ignore_the)
{
    int ba = title_bucket(a, ignore_the), bb = title_bucket(b, ignore_the);
    int c;
    if (ba != bb) return ba < bb ? -1 : 1;
    c = sf_stricmp(title_sort_key(a, ignore_the), title_sort_key(b, ignore_the));
    if (c) return c;
    return strcmp(a, b);
}

char title_bucket_char(int bucket)
{
    if (bucket <= 0 || bucket > 26) return '#';
    return (char)('A' + bucket - 1);
}
