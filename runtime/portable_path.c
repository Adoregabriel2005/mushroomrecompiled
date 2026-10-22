#include "portable_path.h"
#include <stdint.h>
#include <stdlib.h>
#include <limits.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#ifndef _WIN32
/* wchar_t is Unicode on POSIX. Encode explicitly: filenames must not depend
 * on the process locale (the default C locale cannot represent UTF-8). */
static char *utf8(const wchar_t *s) {
    if (!s) return NULL;
    size_t n = wcslen(s);
    if (n > (SIZE_MAX - 1) / 4) return NULL;
    char *out = malloc(n * 4 + 1), *p = out;
    if (!out) return NULL;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = (uint32_t)s[i];
        if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
            free(out); return NULL;
        }
        if (c < 0x80) *p++ = (char)c;
        else if (c < 0x800) { *p++ = (char)(0xc0 | (c >> 6)); *p++ = (char)(0x80 | (c & 63)); }
        else if (c < 0x10000) { *p++ = (char)(0xe0 | (c >> 12)); *p++ = (char)(0x80 | ((c >> 6) & 63)); *p++ = (char)(0x80 | (c & 63)); }
        else { *p++ = (char)(0xf0 | (c >> 18)); *p++ = (char)(0x80 | ((c >> 12) & 63)); *p++ = (char)(0x80 | ((c >> 6) & 63)); *p++ = (char)(0x80 | (c & 63)); }
    }
    *p = 0; return out;
}
#endif
FILE *mushroom_wfopen(const wchar_t *path, const wchar_t *mode) {
    if (!path || !mode) return NULL;
#ifdef _WIN32
    return _wfopen(path, mode);
#else
    char *p = utf8(path), *m = utf8(mode);
    FILE *f = p && m ? fopen(p, m) : NULL;
    free(p); free(m); return f;
#endif
}
FILE *mushroom_fopen_utf8(const char *path, const char *mode) {
    if (!path || !mode) return NULL;
#ifdef _WIN32
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,NULL,0);
    int m=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,mode,-1,NULL,0);
    if (!n || !m) return NULL;
    wchar_t *p=malloc((size_t)n*sizeof(*p)), *modes=malloc((size_t)m*sizeof(*modes));
    FILE *f=NULL;
    if (p && modes && MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,path,-1,p,n) &&
        MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,mode,-1,modes,m)) f=_wfopen(p,modes);
    free(p); free(modes); return f;
#else
    return fopen(path,mode);
#endif
}
