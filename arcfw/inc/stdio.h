#ifndef _STDIO_SHIM_
#define _STDIO_SHIM_

#include <stdarg.h>

//
// Freestanding <stdio.h> subset for the loader. Implemented in arcfw/arm/clib.c.
// Only the formatting the NT loader needs (%c %s %d/%u/%x/%lx, width/zero pad).
//

int sprintf(char *buf, const char *fmt, ...);
int vsprintf(char *buf, const char *fmt, va_list ap);

#endif // _STDIO_SHIM_
