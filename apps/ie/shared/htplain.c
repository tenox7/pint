/*
   This file was derived from the libwww code, version 2.15, from CERN.
   A number of modifications have been made by Spyglass.

   eric@spyglass.com
 */
/*      Plain text object       HTWrite.c
   **       =================
   **
   **   This version of the stream object just writes to a socket.
   **   The socket is assumed open and left open.
   **
   **   Bugs:
   **       strings written must be less than buffer size.
 */
#include "all.h"

#define BUFFER_SIZE 4096;       /* Tradeoff */

/*      HTML Object
   **       -----------
 */

struct _HTStream
{
    CONST HTStreamClass *isa;

    int expected_length;
    int count;

    HText *text;
    struct Mwin *tw;

    BOOL bUTF8;                 /* text is decoded from UTF-8 (htansi.c) */
    struct _ANSI_UTF8 utf8;
};

/*_________________________________________________________________________
**
**          A C T I O N     R O U T I N E S
*/

/*  Character handling
   **   ------------------
 */

PRIVATE void HTPlain_append(HTStream * me, char c)
{
    char buf[ANSI_MAX_CHARS];
    int i, n;

    if (!me->bUTF8)
    {
        HText_appendCharacter(me->text, c);
        return;
    }
    n = ANSI_PutUTF8(&me->utf8, (unsigned char) c, buf);
    for (i = 0; i < n; i++)
        HText_appendCharacter(me->text, buf[i]);
}

PRIVATE BOOL HTPlain_put_character(HTStream * me, char c)
{
    me->count++;
    HTPlain_append(me, c);
    if (me->expected_length)
        WAIT_SetTherm(me->tw, me->count);
    return TRUE;
}



/*  String handling
   **   ---------------
   **
 */
PRIVATE BOOL HTPlain_put_string(HTStream * me, CONST char *s)
{
    me->count += strlen(s);
    for (; *s; s++)
        HTPlain_append(me, *s);
    if (me->expected_length)
        WAIT_SetTherm(me->tw, me->count);
    return TRUE;
}


PRIVATE BOOL HTPlain_write(HTStream * me, CONST char *s, int l)
{
    CONST char *p;
    CONST char *e = s + l;
    me->count += l;
    for (p = s; p < e; p++)
        HTPlain_append(me, *p);
    HText_update(me->text);
    if (me->expected_length)
        WAIT_SetTherm(me->tw, me->count);
    return TRUE;
}



/*  Free an HTML object
   **   -------------------
   **
   **   Note that the SGML parsing context is freed, but the created object is not,
   **   as it takes on an existence of its own unless explicitly freed.
 */
PRIVATE void HTPlain_free(HTStream * me)
{
    char buf[ANSI_MAX_CHARS];
    int i, n;

    n = ANSI_FlushUTF8(&me->utf8, buf);     /* text may end inside a sequence */
    for (i = 0; i < n; i++)
        HText_appendCharacter(me->text, buf[i]);
    HText_endAppend(me->text);
    GTR_FREE(me);
}

/*  End writing
 */

PRIVATE void HTPlain_abort(HTStream * me, HTError e)
{
    HText_abort(me->text);
    GTR_FREE(me);
}



/*      Structured Object Class
   **       -----------------------
 */
#ifdef MAC
PUBLIC CONST HTStreamClass HTPlain =
{
    "PlainText",
    NULL,
    NULL,
    NULL,
    HTPlain_free,
    HTPlain_abort,
    HTPlain_put_character, HTPlain_put_string, HTPlain_write,
};
void HTPlain_InitStaticStrings(void);
void HTPlain_InitStaticStrings(void)
{
    HTPlain.szStatusNoLength = GTR_GetString(HTPLAIN_RECEIVING_TEXT_S);
    HTPlain.szStatusWithLength = GTR_GetString(HTPLAIN_RECEIVING_TEXT_S_S);
}
#else
PUBLIC CONST HTStreamClass HTPlain =
{
    "PlainText",
    SID_HTPLAIN_RECEIVING_TEXT_S,
    SID_HTPLAIN_RECEIVING_TEXT_S_S,
    NULL,
    HTPlain_free,
    HTPlain_abort,
    HTPlain_put_character, HTPlain_put_string, HTPlain_write,
};
#endif

/*      New object
   **       ----------
 */
PUBLIC HTStream *HTPlainPresent(struct Mwin *tw, HTRequest * request, void *param, HTFormat input_format, HTFormat output_format, HTStream * output_stream)
{

    HTStream *me = (HTStream *) GTR_MALLOC(sizeof(*me));
    if (me)
    {
        me->isa = &HTPlain;

        me->text = HText_new2(tw, request, output_stream, NULL);

        me->expected_length = request->content_length;
        me->count = 0;
        if (me->expected_length)
        {
            WAIT_SetRange(tw, 0, 100, me->expected_length);
        }
        me->tw = tw;
        me->bUTF8 = ANSI_ReadAsUTF8(request);
        memset(&me->utf8, 0, sizeof(me->utf8));

        HText_beginAppend(me->text);
        HText_setStyle(me->text, HTML_STYLE_PRE);
    }
    return (HTStream *) me;
}
