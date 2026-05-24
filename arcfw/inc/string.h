#ifndef _STRING_SHIM_
#define _STRING_SHIM_

#include <stddef.h>

//
// Freestanding <string.h> for the loader. Implemented in arcfw/arm/clib.c.
// Includes the Win32-spelled case-insensitive helpers (stricmp/strnicmp) the
// NT loader uses, which POSIX spells strcasecmp/strncasecmp.
//

void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
void  *memset(void *dst, int c, size_t n);
int    memcmp(const void *a, const void *b, size_t n);

size_t strlen(const char *s);
char  *strcpy(char *dst, const char *src);
char  *strncpy(char *dst, const char *src, size_t n);
char  *strcat(char *dst, const char *src);
char  *strncat(char *dst, const char *src, size_t n);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
char  *strchr(const char *s, int c);
char  *strrchr(const char *s, int c);
char  *strstr(const char *haystack, const char *needle);

int    stricmp(const char *a, const char *b);
int    strnicmp(const char *a, const char *b, size_t n);

#endif // _STRING_SHIM_
