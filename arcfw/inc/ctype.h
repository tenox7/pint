#ifndef _CTYPE_SHIM_
#define _CTYPE_SHIM_

//
// Freestanding <ctype.h> for the loader. NTDEF.H pulls this in; the loader
// sources use very little of it. Implemented as static inlines so unused
// classifiers cost nothing and require no separate translation unit.
//

static inline int isdigit(int c)  { return c >= '0' && c <= '9'; }
static inline int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
static inline int isupper(int c)  { return c >= 'A' && c <= 'Z'; }
static inline int islower(int c)  { return c >= 'a' && c <= 'z'; }
static inline int isalpha(int c)  { return isupper(c) || islower(c); }
static inline int isalnum(int c)  { return isalpha(c) || isdigit(c); }
static inline int isspace(int c)  { return c == ' ' || (c >= '\t' && c <= '\r'); }
static inline int isprint(int c)  { return c >= 0x20 && c < 0x7f; }
static inline int toupper(int c)  { return islower(c) ? c - 'a' + 'A' : c; }
static inline int tolower(int c)  { return isupper(c) ? c - 'A' + 'a' : c; }

#endif // _CTYPE_SHIM_
