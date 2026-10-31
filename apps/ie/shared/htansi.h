/*
   Unicode text in the Windows ANSI character set (code page 1252, the
   one NT 3.1 fonts have).  UTF-8 pages and character references such as
   &#8217; &#x2019; and &rsquo; become those characters, or a close ASCII
   stand-in.
 */

#ifndef HTANSI_H
#define HTANSI_H

#define ANSI_MAX_CHARS  4       /* most chars one call hands back */

struct _ANSI_UTF8               /* UTF-8 decoder state, zeroed to start */
{
    unsigned long   cp;         /* code point decoded so far */
    int             need;       /* continuation bytes still to come */
    int             have;       /* bytes of the sequence held in buf */
    unsigned char   buf[4];
};

int ANSI_FromUnicode(unsigned long cp, char *out);
int ANSI_PutUTF8(struct _ANSI_UTF8 *u, int c, char *out);
int ANSI_FlushUTF8(struct _ANSI_UTF8 *u, char *out);
long ANSI_FindEntity(CONST char *name);
BOOL ANSI_ReadAsUTF8(HTRequest *request);

#endif /* HTANSI_H */
