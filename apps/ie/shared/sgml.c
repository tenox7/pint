/*
   This file was derived from the libwww code, version 2.15, from CERN.
   A number of modifications have been made by Spyglass.

   eric@spyglass.com
 */
/*          General SGML Parser code        SGML.c
   **           ========================
   **
   **   This module implements an HTStream object. To parse an
   **   SGML file, create this object which is a parser. The object
   **   is (currently) created by being passed a DTD structure,
   **   and a target HTStructured oject at which to throw the parsed stuff.
   **   
   **    6 Feb 93  Binary seraches used. Intreface modified.
 */
#include "all.h"

#ifdef INVALID
#undef INVALID
#endif
#define INVALID (-1)

#ifdef _GIBRALTAR
    #define MAX_ATTR_LENGTH (2*MAX_URL_STRING)
#else
    #define MAX_ATTR_LENGTH MAX_URL_STRING
#endif // _GIBRALTAR

/*      Element Stack
   **       -------------
   **   This allows us to return down the stack reselcting styles.
   **   As we return, attribute values will be garbage in general.
 */
typedef struct _HTElement HTElement;
struct _HTElement
{
    HTElement *next;            /* Previously nested element or 0 */
    HTTag *tag;                 /* The tag at this level  */
};


/*  Internal Context Data Structure
   **   -------------------------------
 */
struct _HTStream
{

    CONST HTStreamClass *isa;   /* inherited from HTStream */

    CONST SGML_dtd *dtd;
    HTStructuredClass *actions; /* target class  */
    HTStructured *target;       /* target object */

    int expected_length;
    int bytes_processed;
    HTTag *current_tag;
    int current_attribute_number;
    HTChunk *string;
    HTElement *element_stack;
    int extra;                  /* extra info for specific states */
    enum sgml_state
    {
        S_text, S_literal, S_tag, S_tag_gap,
        S_attr, S_attr_gap, S_equals, S_value,
        S_ero, S_cro,
#ifdef ISO_2022_JP
        S_esc, S_dollar, S_paren, S_nonascii_text,
#endif
        S_squoted, S_dquoted, S_invalueent,
        S_end, S_entity, S_junk_tag,
        S_bang, S_bangdash, S_comment, S_commentdash, S_commentdashdash
#ifdef _GIBRALTAR
      , S_commentdashdashbang, S_EOF
#endif // _GIBRALTAR
    }
    state, old_state;
#ifdef _GIBRALTAR
    enum CR_LF_State
    {
        CRLF_None,
        CR_Seen,
        LF_Seen
    }
    crlf_state;
#endif // _GIBRALTAR

#ifdef CALLERDATA
    void *callerData;
#endif
    BOOL present[MAX_ATTRIBUTES];   /* Flags: attribute is present? */
    char *value[MAX_ATTRIBUTES];    /* malloc'd strings or NULL if none */

    struct Mwin *tw;
#ifdef _GIBRALTAR
    HTRequest *request;
    FILE *  fpDc;
    char *  pszDcFile;
    HTFormat format_inDc;
    BOOL fDCache;
    BOOL fInRecovery;
    int cbRecovery;                 /* in recovery, this is charpos in cs_stream */
    struct CharStream *cs_source;
#endif // _GIBRALTAR

    BOOL bUTF8;                     /* page is decoded from UTF-8 (htansi.c) */
    struct _ANSI_UTF8 utf8;
    int depth;                      /* elements on element_stack */

    /*  Elements with the hidden attribute or style="display:none" are not
        shown: nothing inside them reaches the target. */
    BOOL bHide;                     /* the tag being read asks to be hidden */
    HTTag *hide_tag;                /* hidden element we are in, or NULL */
    int hide_count;                 /* open elements of that kind in it */
    int hide_depth;                 /* depth when it began */
};

#define STYLE_ATTRIBUTE (-2)        /* current_attribute_number for style= */

PRIVATE void x_PutChar(HTStream * context, char c)
{
    if (!context->hide_tag)
        (*context->actions->put_character)(context->target, c);
}

#define PUTC(ch) x_PutChar(context, (char) (ch))

/*  Writes a Unicode character as the ANSI chars it maps to */
PRIVATE void x_PutUnicode(HTStream * context, long cp)
{
    char buf[ANSI_MAX_CHARS];
    int i, n;

    if (cp <= 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;                /* not a character */
    n = ANSI_FromUnicode((unsigned long) cp, buf);
    for (i = 0; i < n; i++)
        PUTC(buf[i]);
}

/*  The value of a numeric character reference: "8217" or "x2019",
    without the "&#".  Returns -1 if it is not one. */
PRIVATE long x_CharRefValue(CONST char *s)
{
    long v = 0;
    int base = 10;
    int d;

    if (*s == 'x' || *s == 'X')
    {
        base = 16;
        s++;
    }
    if (!*s)
        return -1;
    for (; *s; s++)
    {
        if (*s >= '0' && *s <= '9')
            d = *s - '0';
        else if (base == 16 && *s >= 'a' && *s <= 'f')
            d = *s - 'a' + 10;
        else if (base == 16 && *s >= 'A' && *s <= 'F')
            d = *s - 'A' + 10;
        else
            return -1;
        if (v <= 0x10FFFF)
            v = v * base + d;
    }
    return v;
}

/*  Does a style attribute say display:none? */
PRIVATE BOOL x_StyleHides(CONST char *s)
{
    CONST char *p, *q;

    for (p = s; *p; p++)
    {
        if ((p == s || p[-1] == ';' || WHITE(p[-1])) && 0 == GTR_strncmpi(p, "display", 7))
        {
            for (q = p + 7; *q && WHITE(*q); q++)
                ;
            if (*q != ':')
                continue;
            for (q++; *q && WHITE(*q); q++)
                ;
            if (0 == GTR_strncmpi(q, "none", 4) && !isalnum((unsigned char) q[4]) && q[4] != '-')
                return TRUE;
        }
    }
    return FALSE;
}



/*  Handle Attribute
   **   ----------------
 */
/* PUBLIC CONST char * SGML_default = "";   ?? */
#ifdef _GIBRALTAR

PRIVATE BOOL SGML_character(HTStream * context, char c);


/*  Lookup Attribute Name
 *
 *   ----------------
 */
/* PUBLIC CONST char * SGML_default = "";   ?? */
PRIVATE int find_attribute_name(HTStream * context, const char *s)
{
    HTTag *tag = context->current_tag;
    attr *attributes = tag->attributes;
    int high, low, i, diff;     /* Binary search for attribute name */

    for (low = 0, high = tag->number_of_attributes;
         high > low;
         diff < 0 ? (low = i + 1) : (high = i))
    {
        i = (low + (high - low) / 2);
        diff = strcasecomp(attributes[i].name, s);
        XX_DMsg(DBG_SGML, ("Checking %s\n", attributes[i].name));
        if (diff == 0) return i;

    }                           /* for */

    return INVALID; /* Invalid */
}
#else

/*  Handle Attribute
 *
 *   ----------------
 */
/* PUBLIC CONST char * SGML_default = "";   ?? */

PRIVATE void handle_attribute_name(HTStream * context, const char *s)
{

    int i;      /* Binary search for attribute name */

    XX_DMsg(DBG_SGML, ("Handling attribute %s for tag %s\n", s, context->current_tag->name));

    i = find_attribute_name(context, s);
    if (i != INVALID)
    {
        context->current_attribute_number = i;
        context->present[i] = YES;
        if (context->value[i])
        {
            GTR_FREE(context->value[i]);
            context->value[i] = NULL;
        }
    }
#ifdef XX_DEBUG
    else
    {                           /* for */
        XX_DMsg(DBG_SGML, ("Unknown attribute %s for tag %s\n", s, context->current_tag->name));
    }
#endif
    context->current_attribute_number = i;
}
#endif // _GIBRALTAR

#ifdef _GIBRALTAR
PRIVATE void handle_attribute_name(HTStream * context, const char *s)
{

    HTTag *tag = context->current_tag;
    attr *attributes = tag->attributes;

    int high, low, i, diff;     // Binary search for attribute name

    XX_DMsg(DBG_SGML, ("Handling attribute %s for tag %s\n", s, context->current_tag->name));

    /* hidden and style go with any element, see start_element */
    if (0 == strcasecomp(s, "hidden"))
    {
        context->bHide = TRUE;
        context->current_attribute_number = INVALID;
        return;
    }
    if (0 == strcasecomp(s, "style"))
    {
        context->current_attribute_number = STYLE_ATTRIBUTE;
        return;
    }

    for (low = 0, high = tag->number_of_attributes;
         high > low;
         diff < 0 ? (low = i + 1) : (high = i))
    {
        i = (low + (high - low) / 2);
        diff = strcasecomp(attributes[i].name, s);
        XX_DMsg(DBG_SGML, ("Checking %s\n", attributes[i].name));
        if (diff == 0)
        {                       //
            context->current_attribute_number = i;
            context->present[i] = YES;
            if (context->value[i])
            {
                GTR_FREE(context->value[i]);
                context->value[i] = NULL;
            }
            return;
        }                       

    }                           

    XX_DMsg(DBG_SGML, ("Unknown attribute %s for tag %s\n", s, context->current_tag->name));

    context->current_attribute_number = INVALID;    
}
#endif // _GIBRALTAR

/*  Handle attribute value
   **   ----------------------
 */
PRIVATE void handle_attribute_value(HTStream * context, const char *s)
{
    if (context->current_attribute_number == STYLE_ATTRIBUTE)
    {
        if (x_StyleHides(s))
        {
            context->bHide = TRUE;
        }
    }
    else if (context->current_attribute_number != INVALID)
    {
        if (context->value[context->current_attribute_number]) /* TODO is this needed? */
        {
            GTR_FREE(context->value[context->current_attribute_number]);
        }
        context->value[context->current_attribute_number] = GTR_strdup(s);
    }
    else
    {
        XX_DMsg(DBG_SGML, ("SGML: Attribute value %s ignored\n", s));
    }
    context->current_attribute_number = INVALID;    /* can't have two assignments! */
}
#ifdef _GIBRALTAR
/*  Handle entity
**  -------------
**
** On entry,
**   s   contains the entity name zero terminated
** Returns TRUE if context contains exactly an entity.
** If it contains no entity or trailing source, returns FALSE
*/
PRIVATE BOOL handle_entity(HTStream * context)
{

    CONST char **entities = context->dtd->entity_names;
    char *s = context->string->data;
    int cbSLen = strlen(s);
    int cbLen = cbSLen > MAX_ENTITY_LEN ? MAX_ENTITY_LEN : cbSLen;
    int cbPrefix;
    int high, low, i, diff;
    char szEntity[MAX_ENTITY_LEN+1];
    enum entitySpellings
    {
        esUntouched,
        esLower,
        esUpper,
        esFirstUpper,
        esFirst2Upper,
        esDone
    } curSpelling;

    curSpelling = esUntouched;
    while (curSpelling != esDone)
    {
        strncpy(szEntity, s, MAX_ENTITY_LEN);
        switch (curSpelling)
        {
            case esLower:
                for (i = 0; i < cbLen; i++)
                    szEntity[i] = tolower(szEntity[i]);
                break;
            case esUpper:
                for (i = 0; i < cbLen; i++)
                    szEntity[i] = toupper(szEntity[i]);
                break;
            case esFirstUpper:
                // NOTE, this will still work if cbLen < 2, but be wasted effort
                // this is low frequency (like never) so why waste code
                szEntity[0] = toupper(szEntity[0]);
                for (i = 1; i < cbLen; i++)
                    szEntity[i] = tolower(szEntity[i]);
                break;
            case esFirst2Upper:
                // NOTE, this will still work if cbLen < 3, but be wasted effort
                // this is low frequency (like never) so why waste code
                szEntity[0] = toupper(szEntity[0]);
                szEntity[1] = toupper(szEntity[1]);
                for (i = 2; i < cbLen; i++)
                    szEntity[i] = tolower(szEntity[i]);
                break;
        }
        cbPrefix = cbLen;   
        while (cbPrefix)
        {
            szEntity[cbPrefix] = '\0';
            for (low = 0, high = context->dtd->number_of_entities;
                 high > low;
                 diff < 0 ? (low = i + 1) : (high = i))
            {                           /* Binary search */
                i = (low + (high - low) / 2);
                diff = strcmp(entities[i], szEntity);  /* Case sensitive! */
                if (diff == 0)
                {                       /* success: found it */
                    if (!context->hide_tag)
                        (*context->actions->put_entity) (context->target, i);
                    if (cbPrefix == cbSLen) return TRUE;
                    goto exitWhile;
                }
            }
            cbPrefix--;
        }
        curSpelling++;
    }
    
    PUTC('&');
    /* If entity string not found, display as text */
    XX_DMsg(DBG_SGML, ("SGML: Unknown entity %s\n", s));

exitWhile:

    {
        CONST char *p;
        for (p = s + cbPrefix; *p; p++)
        {
            PUTC(*p);
        }
    }
    return FALSE;
}

#else

/*  Handle entity
**  -------------
**
** On entry,
**   s   contains the entity name zero terminated
*/
PRIVATE BOOL handle_entity(HTStream * context)
{

    CONST char **entities = context->dtd->entity_names;
    CONST char *s = context->string->data;

    int high, low, i, diff;
    for (low = 0, high = context->dtd->number_of_entities;
         high > low;
         diff < 0 ? (low = i + 1) : (high = i))
    {                           /* Binary serach */
        i = (low + (high - low) / 2);
        diff = strcmp(entities[i], s);  /* Csse sensitive! */
        if (diff == 0)
        {                       /* success: found it */
            (*context->actions->put_entity) (context->target, i);
            return TRUE;
        }
    }
    /* If entity string not found, display as text */
    XX_DMsg(DBG_SGML, ("SGML: Unknown entity %s\n", s));

    PUTC('&');
    {
        CONST char *p;
        for (p = s; *p; p++)
        {
            PUTC(*p);
        }
    }
    return FALSE;
}
#endif // _GIBRALTAR

/* Handle an entity inside an attribute value.  If the parameter
   points to an entity, it replaces it with the fixed up value.
   cTerm is the character that ended it. */
PRIVATE BOOL SGML_HandleValueEntity(HTChunk *string, const SGML_dtd *dtd, int index, char cTerm)
{
    char buf[256];
    char out[ANSI_MAX_CHARS];
    long cp;
    int i, n;

    XX_Assert((string->data[index] == '&'), ("Entity didn't start with '&'!"));

    if (string->size <= index + 1 || string->size > index + 254)
    {
        /* There's no way this is a valid entity! */
        return FALSE;
    }

    /* the name or number, without the '&' */
    memcpy(buf, string->data + index + 1, string->size - index - 1);
    buf[string->size - index - 1] = '\0';

    if (buf[0] == '#')
    {
        cp = x_CharRefValue(buf + 1);
        if (cp < 0)
        {
            return FALSE;
        }
        if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        {
            cp = 0xFFFD;
        }
    }
    else
    {
        CONST char **entities = dtd->entity_names;
        int high, low, diff;

        /* "?a=1&copy=2" in a link is not a reference (as in HTML 5) */
        if (cTerm == '=')
        {
            return FALSE;
        }

        cp = (cTerm == ';') ? ANSI_FindEntity(buf) : -1;
        if (cp < 0)
        {
            for (low = 0, high = dtd->number_of_entities;
                 high > low;
                 diff < 0 ? (low = i + 1) : (high = i))
            {                           /* Binary serach */
                i = (low + (high - low) / 2);
                diff = strcmp(entities[i], buf);  /* Csse sensitive! */
                if (diff == 0)
                {
                    string->size = index;
                    HTChunkPuts(string, dtd->entity_values[i]);
                    return TRUE;
                }
            }
            XX_DMsg(DBG_SGML, ("SGML: Unknown entity %s\n", buf));
            return FALSE;
        }
    }

    n = ANSI_FromUnicode((unsigned long) cp, out);
    string->size = index;
    for (i = 0; i < n; i++)
    {
        HTChunkPutc(string, out[i]);
    }
    return TRUE;
}

/*  Elements that never have content (end tags for them are ignored) */
PRIVATE BOOL x_IsVoid(HTStream * context, HTTag * tag)
{
    switch (tag - context->dtd->tags)
    {
        case HTML_AREA:
        case HTML_BASE:
        case HTML_BASEFONT:
        case HTML_BR:
        case HTML_FRAME:
        case HTML_HR:
        case HTML_IMAGE:
        case HTML_IMG:
        case HTML_INPUT:
        case HTML_ISINDEX:
        case HTML_LINK:
        case HTML_META:
        case HTML_NEXTID:
            return TRUE;
    }
    return FALSE;
}

/*  Elements whose end tag may be left out: the next one ends them */
PRIVATE BOOL x_EndsAtNext(HTStream * context, HTTag * tag)
{
    switch (tag - context->dtd->tags)
    {
        case HTML_DD:
        case HTML_DT:
        case HTML_LI:
        case HTML_OPTION:
        case HTML_P:
        case HTML_TD:
        case HTML_TH:
        case HTML_TR:
            return TRUE;
    }
    return FALSE;
}

/*  A start tag inside a hidden element.  Form fields still count when
    the form is sent, so INPUTs go on as hidden ones. */
PRIVATE void x_HiddenStart(HTStream * context, HTTag * tag)
{
    static char szHidden[] = "hidden";
    static char szOn[] = "on";
    CONST char *type;
    BOOL bTypePresent, bValuePresent;
    char *pType, *pValue;

    if (tag - context->dtd->tags != HTML_INPUT)
        return;

    type = context->present[HTML_INPUT_TYPE] ? context->value[HTML_INPUT_TYPE] : NULL;
    if (type && (0 == GTR_strcmpi(type, "checkbox") || 0 == GTR_strcmpi(type, "radio")))
    {
        if (!context->present[HTML_INPUT_CHECKED])
            return;
    }
    else if (type && (   0 == GTR_strcmpi(type, "submit") || 0 == GTR_strcmpi(type, "reset")
                      || 0 == GTR_strcmpi(type, "image") || 0 == GTR_strcmpi(type, "button")
                      || 0 == GTR_strcmpi(type, "file")))
    {
        return;
    }

    bTypePresent = context->present[HTML_INPUT_TYPE];
    bValuePresent = context->present[HTML_INPUT_VALUE];
    pType = context->value[HTML_INPUT_TYPE];
    pValue = context->value[HTML_INPUT_VALUE];

    context->present[HTML_INPUT_TYPE] = YES;
    context->value[HTML_INPUT_TYPE] = szHidden;
    if (type && !bValuePresent && 0 == GTR_strcmpi(type, "checkbox"))
    {
        context->present[HTML_INPUT_VALUE] = YES;
        context->value[HTML_INPUT_VALUE] = szOn;
    }

    (*context->actions->start_element) (context->target, HTML_INPUT,
                                        context->present, (CONST char **) context->value);

    context->present[HTML_INPUT_TYPE] = bTypePresent;
    context->value[HTML_INPUT_TYPE] = pType;
    context->present[HTML_INPUT_VALUE] = bValuePresent;
    context->value[HTML_INPUT_VALUE] = pValue;
}

/*  The hidden element ends, and what was opened in it and is still open
    ends with it: the target never saw those start, so must not see their
    end either. */
PRIVATE void x_EndHidden(HTStream * context)
{
    HTElement *N;

    while (context->depth > context->hide_depth && context->element_stack)
    {
        N = context->element_stack;
        context->element_stack = N->next;
        GTR_FREE(N);
        context->depth--;
    }
    context->hide_tag = NULL;
}

/*  An end tag of the hidden element's kind: is it the hidden one's?  For
    <li> and the like only at its own level (a list in it has its own). */
PRIVATE BOOL x_HiddenEnds(HTStream * context)
{
    if (x_EndsAtNext(context, context->hide_tag))
        return context->depth == context->hide_depth;
    return --context->hide_count == 0;
}

/*  Passes an end tag on, unless it is inside a hidden element */
PRIVATE void x_EndElement(HTStream * context, HTTag * tag)
{
    if (context->hide_tag)
    {
        if (context->depth < context->hide_depth)
        {
            context->hide_tag = NULL;   /* what held the hidden element ended */
        }
        else
        {
            if (tag == context->hide_tag && x_HiddenEnds(context))
                x_EndHidden(context);
            return;
        }
    }
    (*context->actions->end_element) (context->target, tag - context->dtd->tags);
}

/*  End element
**   -----------
*/
PRIVATE void end_element(HTStream * context, HTTag * old_tag)
{
    HTElement *N;
    HTTag *t;

#ifdef _GIBRALTAR
    BOOL bHeader;
#endif // _GIBRALTAR

    /*
       TODO when we have strict HTML flag and reporting,
       put some warnings in appropriate places here.
     */
    XX_DMsg(DBG_SGML, ("SGML: End   </%s>\n", old_tag->name));
    if (old_tag->contents == SGML_EMPTY)
    {
        XX_DMsg(DBG_SGML, ("SGML: Illegal end tag </%s> found.\n",
                           old_tag->name));
        if (old_tag == context->hide_tag && x_HiddenEnds(context))
            x_EndHidden(context);       /* a hidden <li> ... </li> */
        return;
    }
    if (old_tag->contents == SGML_NEST)
    {
        x_EndElement(context, old_tag);
        return;
    }

    /* Make sure that the tag exists in the stack at all, so that we
       don't close all open tags */         

#ifdef _GIBRALTAR
    bHeader = old_tag->tagclass == HTTAG_HEADER;            
#endif
    for (N = context->element_stack; N; N = N->next)
    {
        if (old_tag == N->tag
#ifdef _GIBRALTAR
            || (old_tag->tagclass == N->tag->tagclass && bHeader)
#endif _GIBRALTAR
            )
            break;
    }
    if (!N)
    {
        XX_DMsg(DBG_SGML, ("SGML: Found </%s> with no <%s>.\n", old_tag->name, old_tag->name));
        return;
    }

    while (context->element_stack)
    {                           /* Loop is error path only */
        N = context->element_stack;
        t = N->tag;
        if (old_tag != t)
        {                       /* Mismatch: syntax error */
                
            if (context->element_stack->next)
            {                   /* This is not the last level */
                XX_DMsg(DBG_SGML,
                    ("SGML: Found </%s> when expecting </%s>. </%s> assumed.\n",
                     old_tag->name, t->name, t->name));
            }
            else
            {                   /* last level */
                /* This should never happen, since it should have been caught before
                   we entered this loop. */
                XX_DMsg(DBG_SGML,
                    ("SGML: Found </%s> when expecting </%s>. </%s> Ignored.\n",
                     old_tag->name, t->name, old_tag->name));
                return;         /* Ignore */
            }
        }

        context->element_stack = N->next;   /* Remove from stack */
        GTR_FREE(N);
        context->depth--;
        x_EndElement(context, t);
        if (old_tag == t)
            return;             /* Correct sequence */

        /* Syntax error path only */
    }
    XX_DMsg(DBG_SGML,
         ("SGML: Extra end tag </%s> found and ignored.\n", old_tag->name));
}


/*  Start a element
 */
PRIVATE void start_element(HTStream * context)
{
    HTTag *new_tag = context->current_tag;
    BOOL bHide = context->bHide;

    XX_DMsg(DBG_SGML, ("SGML: Start <%s>\n", new_tag->name));

    context->bHide = FALSE;
    if (new_tag - context->dtd->tags == HTML_TEMPLATE)
    {
        bHide = TRUE;                   /* never shown */
    }

    if (new_tag == context->hide_tag)
    {
        if (!x_EndsAtNext(context, new_tag))
            context->hide_count++;
        else if (context->depth == context->hide_depth)
            x_EndHidden(context);       /* <li> after a hidden <li> ends it */
    }
    if (bHide && !context->hide_tag && !x_IsVoid(context, new_tag))
    {
        context->hide_tag = new_tag;
        context->hide_count = 1;
        context->hide_depth = context->depth;
    }

    if (bHide || context->hide_tag)
    {
        x_HiddenStart(context, new_tag);
    }
    else
    {
        (*context->actions->start_element) (
                                           context->target,
                                           new_tag - context->dtd->tags,
                                           context->present,
                                           (CONST char **) context->value);     /* coerce type for think c */
    }
    if (new_tag->contents != SGML_EMPTY && new_tag->contents != SGML_NEST)
    {                           /* i.e. tag not empty */
        HTElement *N = (HTElement *) GTR_MALLOC(sizeof(HTElement));
        if (N)
        {
            N->next = context->element_stack;
            N->tag = new_tag;
            context->element_stack = N;
            context->depth++;
        }
        else
        {
            /* TODO */
        }
    }
}


/*      Find Tag in DTD tag list
   **       ------------------------
   **
   ** On entry,
   **   dtd points to dtd structire including valid tag list
   **   string  points to name of tag in question
   **
   ** On exit,
   **   returns:
   **       NULL        tag not found
   **       else        address of tag structure in dtd
 */
PUBLIC HTTag *SGMLFindTag(CONST SGML_dtd * dtd, CONST char *string)
{
    int high, low, i, diff;
    for (low = 0, high = dtd->number_of_tags;
         high > low;
         diff < 0 ? (low = i + 1) : (high = i))
    {                           /* Binary serach */
        i = (low + (high - low) / 2);
        diff = strcasecomp(dtd->tags[i].name, string);  /* Case insensitive */
        if (diff == 0)
        {                       /* success: found it */
            return &dtd->tags[i];
        }
    }
    return NULL;
}

/*________________________________________________________________________
**          Public Methods
*/


/*  Could check that we are back to bottom of stack! @@  */

PRIVATE void SGML_free(HTStream * context)
{
    int cnt;
#ifdef _GIBRALTAR
    char *pool;
    int cbLength;
    int cbChar;
    int cbPosSync;
    int depth;
    char buf[ANSI_MAX_CHARS];

    /* the page may end in the middle of a UTF-8 sequence */
    cbLength = ANSI_FlushUTF8(&context->utf8, buf);
    for (cbChar = 0; cbChar < cbLength; cbChar++)
    {
        SGML_character(context, buf[cbChar]);
    }

    //
    //  Attempt to recovery from unterminated comments, which are quite common, due to
    //  Netscape's loose attitude.  Unsafe HTML!
    //
    while  (context->state == S_comment || context->state == S_commentdash || context->state == S_commentdashdash)
    {
        if (context->cs_source == NULL) 
        {
            break;
        }

        pool = CS_GetPool(context->cs_source);
        cbLength = CS_GetLength(context->cs_source);
        if (cbLength == 0) 
        {
            break;
        }
        
        depth = 0;
        cbPosSync = -1;
        context->fInRecovery = TRUE;
        for (cbChar = context->extra; cbChar < cbLength; cbChar++)
        {
            switch (pool[cbChar])
            {
                case '>':
                    depth--;
                    if (depth < 0) cbPosSync = cbChar;
                    break;
                case '<':
                    depth++;
                    if (cbPosSync >= 0) goto breakFor;
                    break;
            }
        }
    breakFor:
        if (cbPosSync < 0) break;
        context->state = S_text; 
        for (cbChar = cbPosSync + 1; cbChar < cbLength; cbChar++)
        {
            if (pool[cbChar] != LF)
            {
                context->cbRecovery = cbChar;
                SGML_character(context, pool[cbChar]);
            }
        }
    }

/*   
    if (context->fDCache)
        UpdateStreamDCache(context, dctExpires, dctLastModif, FALSE, context->tw);
*/
#endif

    while (context->element_stack)
    {                           /* Make sure, that all tags are gone */
        HTElement *ptr = context->element_stack;

        XX_DMsg(DBG_SGML, ("SGML: Non-matched tag found: <%s>\n",
                    context->element_stack->tag->name));
        context->element_stack = ptr->next;
        GTR_FREE(ptr);
    }
    (*context->actions->free) (context->target);
    HTChunkFree(context->string);
    for (cnt = 0; cnt < MAX_ATTRIBUTES; cnt++)  /* Leak fix Henrik 18/02-94 */
        if (context->value[cnt])
            GTR_FREE(context->value[cnt]);
    
    /* Show the transfer as being completed, even if the expected length didn't match the number
       of bytes we actually got. */
    if (context->expected_length)
        WAIT_SetTherm(context->tw, context->expected_length);
    GTR_FREE(context);
}

PRIVATE void SGML_abort(HTStream * context, HTError e)
{
    int cnt;

    while (context->element_stack)
    {                           /* Make sure, that all tags are gone */
        HTElement *ptr = context->element_stack;

        XX_DMsg(DBG_SGML, ("SGML: Non-matched tag found: <%s>\n",
                    context->element_stack->tag->name));
        context->element_stack = ptr->next;
        GTR_FREE(ptr);
    }
    (*context->actions->abort) (context->target, e);
    HTChunkFree(context->string);
    for (cnt = 0; cnt < MAX_ATTRIBUTES; cnt++)  /* Leak fix Henrik 18/02-94 */
        if (context->value[cnt])
            GTR_FREE(context->value[cnt]);
    GTR_FREE(context);
}


/*  Read and write user callback handle
**   -----------------------------------
**
**   The callbacks from the SGML parser have an SGML context parameter.
**   These calls allow the caller to associate his own context with a
**   particular SGML context.
*/

#ifdef CALLERDATA
PUBLIC void *SGML_callerData(HTStream * context)
{
    return context->callerData;
}

PUBLIC void SGML_setCallerData(HTStream * context, void *data)
{
    context->callerData = data;
}
#endif

#ifdef _GIBRALTAR
/*
 * SGML_patchurl
 * -------------
 *
 * called when we terminate quoted attribute value prematurely by detecting
 * a '>'.  this indicates mismatched quotes and the url field of the tag
 * has likely consumed the following attribute name.  we attempt to delete
 * that from the end of the src URL.
 *
 * case 1:  <img src="foo alt="bar">
 * case 2:  <img src="foo" alt="bar>
 * case 3:  <img src="foo ismap>
 * case 4:  <img src="foo>
 * 
 * we look for ...{whitespace}+ATTRIBUTENAME{whitespace}*[=], where
 * ATTRIBUTENAME is one of those valid for img tag
 */
PRIVATE void SGML_patchurl(HTStream *context)
{
    char *p;
    char *wsp;
    char *attr;
    char chSave;
    int attrId;
    int urlIdx;

    XX_DMsg(DBG_SGML, ("SGML: patch <%s>\n", context->current_tag));
    switch (context->current_tag - context->dtd->tags)
    {
        case HTML_A:
            urlIdx = HTML_A_HREF;
            break;
        case HTML_IMG:
            urlIdx = HTML_IMG_SRC;
            break;
        //case HTML_BGSOUND:
        //  urlIdx = HTML_BGSOUND_SRC;
        //  break;
        case HTML_BODY:
            urlIdx = HTML_BODY_BACKGROUND;
            break;
        //case HTML_FETCH:
        //  urlIdx = HTML_FETCH_SRC;
        //  break;
        case HTML_FORM:
            urlIdx = HTML_FORM_ACTION;
            break;
        case HTML_INPUT:
            urlIdx = HTML_INPUT_SRC;
            break;
        case HTML_ISINDEX:
            urlIdx = HTML_ISINDEX_ACTION;
            break;
        default:
            return;
    }   
    if (context->present[urlIdx] && (wsp = context->value[urlIdx]))
    {
        while (*wsp && !WHITE(*wsp))
        {
            wsp++;
        }
        if (!*wsp)
        {
            return;
        }
        attr = wsp;
        while (*attr && WHITE(*attr))
        {
            attr++;
        }
        if (!*attr)
        {
            return;
        }
        p = attr;
        while (*p && (!WHITE(*p)) && *p != '=')
        {
            p++;
        }
        chSave = *p;
        *p = '\0';
        attrId = find_attribute_name(context, attr);
        *p = chSave;
        if (attrId != INVALID)
        {
            *wsp = '\0';
        }
    }
}

#endif // _GIBRALTAR

#ifdef _GIBRALTAR
//  NOTE this procedure is not win32 thread safe (it has a static)
PRIVATE BOOL SGML_character(HTStream * context, char c)
{
    BOOL bStatus;
    CONST SGML_dtd *dtd = context->dtd;
    HTChunk *string = context->string;
    static int reentrant = 0;
    static const char szCRLF[3] = "\015\012";

    if (reentrant++ == 0)
    {
        if (!context->fInRecovery)
        {
            context->bytes_processed++;
            if (context->expected_length)
                WAIT_SetTherm(context->tw, context->bytes_processed);

            if (context->actions->add_source)
            {
                switch (c)
                {
                    case LF:
                        switch (context->crlf_state)
                        {
                            case CRLF_None:
                            case LF_Seen:
                                context->actions->add_source(context->target, szCRLF, 2);
                                context->crlf_state = LF_Seen;
                                break;
                            case CR_Seen:
                                context->crlf_state = CRLF_None;
                                break;
                        }
                        break;
                    case CR:
                        switch (context->crlf_state)
                        {
                            case CR_Seen:
                            case CRLF_None:
                                context->actions->add_source(context->target, szCRLF, 2);
                                context->crlf_state = CR_Seen;
                                break;
                            case LF_Seen:
                                context->crlf_state = CRLF_None;
                                break;
                        }
                        break;
                    default:
                        context->actions->add_source(context->target, &c, 1);
                        context->crlf_state = CRLF_None;
                        break;
                }
            }
        }
    }

    if (c == 0)
    {
        if (context->state != S_EOF) context->extra = context->state;
        context->state = S_EOF;
    }

restate:
    switch (context->state)
    {
        case S_EOF:
            if (c != 0)
            {
                context->state = context->extra;
                goto restate;
            }
            break;
        case S_text:
#ifdef ISO_2022_JP
            if (c == '\033')
            {
                context->state = S_esc;
                PUTC(c);
                break;
            }
#endif /* ISO_2022_JP */
            if (c == '&' && (!context->element_stack || (
                                              context->element_stack->tag &&
                        (context->element_stack->tag->contents == SGML_MIXED
                         ||context->element_stack->tag->contents == SGML_LITERAL
                         || context->element_stack->tag->contents ==
                         SGML_RCDATA)
                             )))
            {
                string->size = 0;
                context->state = S_ero;

            }
            else if (c == '<')
            {
                string->size = 0;
                context->state = (context->element_stack &&
                                  context->element_stack->tag &&
                    context->element_stack->tag->contents == SGML_LITERAL) ?
                    S_literal : S_tag;
            }
            else
                PUTC(c);
            break;

#ifdef ISO_2022_JP
        case S_esc:
            if (c == '$')
            {
                context->state = S_dollar;
            }
            else if (c == '(')
            {
                context->state = S_paren;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_dollar:
            if (c == '@' || c == 'B')
            {
                context->state = S_nonascii_text;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_paren:
            if (c == 'B' || c == 'J')
            {
                context->state = S_text;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_nonascii_text:
            if (c == '\033')
            {
                context->state = S_esc;
                PUTC(c);
            }
            else
            {
                PUTC(c);
            }
            break;
#endif /* ISO_2022_JP */

/*  In literal mode, waits only for specific end tag!
   **   Only foir compatibility with old servers.
 */
        case S_literal:
            HTChunkPutc(string, c);
            if (TOUPPER(c) != ((string->size == 1) ? '/'
                     : context->element_stack->tag->name[string->size - 2]))
            {
                int i;

                /*  If complete match, end literal.  (Not on "<>": with one
                    char there is no name to have matched.) */
                if (   string->size > 1
                    && !context->element_stack->tag->name[string->size - 2]
                    && (c == '>' || WHITE(c)))
                {
                    end_element(context, context->element_stack->tag);
                    string->size = 0;
                    context->current_attribute_number = INVALID;
                    context->state = (c == '>') ? S_text : S_junk_tag;  /* "</script >" */
                    break;
                }               /* If Mismatch: recover string. */
                PUTC('<');
                for (i = 0; i < string->size - 1; i++)  /* recover */
                    PUTC(string->data[i]);
                if (c == '<')
                {
                    string->size = 0;   /* this '<' may begin the end tag */
                    break;
                }
                PUTC(c);
                context->state = S_text;
            }

            break;

/*  Character reference or Entity
 */
        case S_ero:
            if (c == '#')
            {
                context->state = S_cro;     /*   &# is Char Ref Open */
                break;
            }
            context->state = S_entity;  /* Fall through! */

/*  Handle Entities
 */
        case S_entity:
            if (isalnum(c))
                HTChunkPutc(string, c);
            else
            {
                long cp = -1;

                HTChunkTerminate(string);
                if (c == ';')
                {
                    cp = ANSI_FindEntity(string->data);     /* &mdash; &rsquo; ... */
                }
                if (cp >= 0)
                {
                    x_PutUnicode(context, cp);
                    bStatus = TRUE;
                }
                else
                {
                    bStatus = handle_entity(context);
                }
                context->state = S_text;

                if (!bStatus || c != ';')
                {
                    /* Call ourselves reentrantly to handle the terminator
                       of an illegal entity, or one that is not ';' */
                    SGML_character(context, c);
                }
            }
            break;

/*  Character reference
 */
        case S_cro:
            if (isalnum(c))
                HTChunkPutc(string, c);     /* accumulate a character NUMBER */
            else
            {
                long value;
                CONST char *p;

                HTChunkTerminate(string);
                value = x_CharRefValue(string->data);   /* &#8217; or &#x2019; */
                if (value >= 0)
                {
                    x_PutUnicode(context, value);
                }
                else
                {
                    PUTC('&');                          /* not a number: show it */
                    PUTC('#');
                    for (p = string->data; *p; p++)
                        PUTC(*p);
                }
                context->state = S_text;
                if (value < 0 || c != ';')
                {
                    /* Call ourselves reentrantly to handle the terminator */
                    SGML_character(context, c);
                }
            }
            break;

/*      Tag
 */
        case S_tag:         /* new tag */
            if (isalnum((unsigned char) c) && (string->size || isalpha((unsigned char) c)))
                HTChunkPutc(string, c);
            else if (c == '!')
                context->state = S_bang;
            else if (!string->size && c != '/' && c != '?')
            {
                /* "a < b" or "<3": a '<' that starts no tag is text */
                PUTC('<');
                context->state = S_text;
                SGML_character(context, c);
            }
            else
            {                   /* End of tag name */
                HTTag *t;
                if (c == '/' && !string->size)
                {
                    context->state = S_end;
                    break;
                }
                /* a '/' after the name, as in <br/>, just ends the name */
                HTChunkTerminate(string);

                t = SGMLFindTag(dtd, string->data);
                if (!t)
                {
                    XX_DMsg(DBG_SGML, ("SGML: *** Unknown element %s\n",
                                string->data));
                    context->state = (c == '>') ? S_text : S_junk_tag;
                    break;
                }
                context->current_tag = t;

                /*  Clear out attributes
                 */

                {
                    int i;
                    for (i = 0; i < context->current_tag->number_of_attributes; i++)
                        context->present[i] = NO;
                }
                context->bHide = FALSE;
                string->size = 0;
                context->current_attribute_number = INVALID;

                if (c == '>')
                {
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                }
                else
                {
                    context->state = S_tag_gap;
                }
            }
            break;


        case S_tag_gap:     /* Expecting attribute or > */
            if (WHITE(c) || c == '/')
                break;          /* Gap between attributes, or <br /> */
            if (c == '>')
            {                   /* End of tag */
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_attr;    /* Get attribute */
            break;

            /* accumulating value */
        case S_attr:
            if (WHITE(c) || (c == '>') || (c == '='))
            {                   /* End of word */
                HTChunkTerminate(string);
                handle_attribute_name(context, string->data);
                string->size = 0;
                if (c == '>')
                {               /* End of tag */
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                    break;
                }
                context->state = (c == '=' ? S_equals : S_attr_gap);
            }
            else
            {
                HTChunkPutc(string, c);
            }
            break;

        case S_attr_gap:        /* Expecting attribute or = or > */
            if (WHITE(c) || c == '/')
                break;          /* Gap after attribute, or <input checked /> */
            if (c == '>')
            {                   /* End of tag */
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;
            }
            else if (c == '=')
            {
                context->state = S_equals;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_attr;    /* Get next attribute */
            break;

        case S_equals:          /* After attr = */
            if (WHITE(c))
                break;          /* Before attribute value */
            if (c == '>')
            {                   /* End of tag */
                XX_DMsg(DBG_SGML, ("SGML: found = but no value\n"));
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;

            }
            else if (c == '\'')
            {
                context->state = S_squoted;
                break;

            }
            else if (c == '"')
            {
                context->state = S_dquoted;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_value;
            break;

        case S_value:
            if (WHITE(c) || (c == '>'))
            {                   /* End of word */
                HTChunkTerminate(string);
                handle_attribute_value(context, string->data);
                string->size = 0;
                if (c == '>')
                {               /* End of tag */
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                    break;
                }
                else
                    context->state = S_tag_gap;
            }
            else if (c == '&')
            {
                /* Entities inside attribute values are not handled
                   identically to ones in regular markup, mostly for ease
                   of coding.  The 2.0 spec requires that entities end in
                   a semicolon.  Here we are a little more lenient and
                   assume as a terminator anything other than an alpha-
                   numeric character or a '#'. */
                context->extra = string->size;  /* Save where entity starts */
                HTChunkPutc(string, c);
                context->old_state = context->state;
                context->state = S_invalueent;
            }
            else
            {
                HTChunkPutc(string, c);
            }
            break;

        case S_squoted:     /* Quoted attribute value */
        case S_dquoted:     /* Quoted attribute value */
            // CMF: many htmls expect end of tag to end quoted string. CF: HTML Spec
            // 3.4.3: Attributes
            if ((context->state == S_squoted && c == '\'') ||
                (context->state == S_dquoted && c == '"') ||
                (c == '>')) 
            {
                /* End of attribute value */
                HTChunkTerminate(string);
                handle_attribute_value(context, string->data);
                string->size = 0;
                context->state = S_tag_gap;
                if (c == '>')
                {
                    /* Patch up src attribute if present to deal with mismatched
                       quotes */
                    SGML_patchurl(context);
                    /* Call ourselves reentrantly to handle the terminator
                       of an illegal entity */
                    SGML_character(context, c);
                }
            }
            else if (string->size < MAX_ATTR_LENGTH)
            {
                /* We restrict an attribute value to a finite length,
                   since if it goes longer than that it's probably an
                   error anyway. */
                if (c == '&')
                {
                    /* Entities inside attribute values are not handled
                       identically to ones in regular markup, mostly for ease
                       of coding.  The 2.0 spec requires that entities end in
                       a semicolon.  Here we are a little more lenient and
                       assume as a terminator anything other than an alpha-
                       numeric character or a '#'. */
                    context->extra = string->size;  /* Save where entity starts */
                    HTChunkPutc(string, c);
                    context->old_state = context->state;
                    context->state = S_invalueent;
                }
                else
                {
                    HTChunkPutc(string, c);
                }
            }
            break;

        case S_invalueent:  /* Accumulating entity in an attribute value */
            if (isalnum(c) || c == '#')
            {
                if (string->size < MAX_ATTR_LENGTH)
                {
                    /* We restrict an attribute value to a finite length,
                       since if it goes longer than that it's probably an
                       error anyway. */
                    HTChunkPutc(string, c);
                }
            }
            else
            {
                bStatus = SGML_HandleValueEntity(string, context->dtd, context->extra, c);
                context->state = context->old_state;
                if (!bStatus || c != ';')
                {
                    /* Call ourselves reentrantly to handle the terminator
                       of an illegal entity, or one that is not ';' (such
                       as the quote that ends the value) */
                    SGML_character(context, c);
                }
            }
            break;

        case S_end:         /* </ */
            if (isalnum(c))
                HTChunkPutc(string, c);
            else
            {                   /* End of end tag name */
                HTTag *t;
                HTChunkTerminate(string);
                if (!*string->data)
                {               /* Empty end tag */
                    if (context->element_stack)
                        t = context->element_stack->tag;
                    else
                        t = NULL;
                }
                else
                {
                    t = SGMLFindTag(dtd, string->data);
                }
                if (!t)
                {
                    XX_DMsg(DBG_SGML, ("Unknown end tag </%s>\n", string->data ? string->data : ""));
                }
                else
                {
                    context->current_tag = t;
                    end_element(context, context->current_tag);
                }

                string->size = 0;
                context->current_attribute_number = INVALID;
                if (c != '>')
                {
                    context->state = S_junk_tag;
                }
                else
                {
                    context->state = S_text;
                }
            }
            break;

        case S_junk_tag:
            if (c == '>')
            {
                context->state = S_text;
            }
            break;

        case S_bang:
            /* We've gotten "<!" */
            if (c == '-')
                context->state = S_bangdash;
            else if (c == '>')
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!>'\n"));
                context->state = S_text;
            }
            else
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!%c...'\n", c));
                context->state = S_junk_tag;
            }
            break;
        
        case S_bangdash:
            /* We've gotten "<!-" */
            if (c == '-')
            {
                context->state = S_comment;
                context->extra = context->fInRecovery ? 
                    context->cbRecovery : (context->cs_source ? CS_GetLength(context->cs_source) : 0);
            }
            else if (c == '>')
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!->'\n"));
                context->state = S_text;
            }
            else
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!-%c...'\n", c));
                context->state = S_junk_tag;
            }
            break;
        
        case S_comment:
            /* We've gotten "<!--", and are looking for "-->" */
            if (c == '-')
                context->state = S_commentdash;
            break;
        
        case S_commentdash:
            if (c == '-')
                context->state = S_commentdashdash;
            else if (c == '>')                  // Relaxed rule: allow "->" to end a comment
                context->state = S_text;
            else
                context->state = S_comment;
            break;
        
        case S_commentdashdash:
            if ( c == '!' )
                context->state = S_commentdashdashbang;
            else if (c == '>')
                context->state = S_text;
            else if (c != '-')
                context->state = S_comment;
            break;

        case S_commentdashdashbang:
            if (c == '>')
                context->state = S_text;
            else 
                context->state = S_comment;
            break;
    }                           /* switch on context->state */
    reentrant--;
    return TRUE;
}                               /* SGML_character */

#else

PRIVATE BOOL SGML_character(HTStream * context, char c)
{
    BOOL bStatus;
    CONST SGML_dtd *dtd = context->dtd;
    HTChunk *string = context->string;

    context->bytes_processed++;
    if (context->expected_length)
        WAIT_SetTherm(context->tw, context->bytes_processed);

#ifdef UNIX
    if (c != '\015')
#endif
    if (context->actions->add_source)
        context->actions->add_source(context->target, &c, 1);

#ifndef MAC
    if (c == '\015')
    {
        if (context->actions->add_source)
            context->actions->add_source(context->target, "\012", 1);
    }
#endif

    switch (context->state)
    {
        case S_text:
#ifdef ISO_2022_JP
            if (c == '\033')
            {
                context->state = S_esc;
                PUTC(c);
                break;
            }
#endif /* ISO_2022_JP */
            if (c == '&' && (!context->element_stack || (
                                              context->element_stack->tag &&
                        (context->element_stack->tag->contents == SGML_MIXED
                         || context->element_stack->tag->contents ==
                         SGML_RCDATA)
                             )))
            {
                string->size = 0;
                context->state = S_ero;

            }
            else if (c == '<')
            {
                string->size = 0;
                context->state = (context->element_stack &&
                                  context->element_stack->tag &&
                    context->element_stack->tag->contents == SGML_LITERAL) ?
                    S_literal : S_tag;
            }
            else
                PUTC(c);
            break;

#ifdef ISO_2022_JP
        case S_esc:
            if (c == '$')
            {
                context->state = S_dollar;
            }
            else if (c == '(')
            {
                context->state = S_paren;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_dollar:
            if (c == '@' || c == 'B')
            {
                context->state = S_nonascii_text;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_paren:
            if (c == 'B' || c == 'J')
            {
                context->state = S_text;
            }
            else
            {
                context->state = S_text;
            }
            PUTC(c);
            break;
        case S_nonascii_text:
            if (c == '\033')
            {
                context->state = S_esc;
                PUTC(c);
            }
            else
            {
                PUTC(c);
            }
            break;
#endif /* ISO_2022_JP */

/*  In literal mode, waits only for specific end tag!
   **   Only foir compatibility with old servers.
 */
        case S_literal:
            HTChunkPutc(string, c);
            if (TOUPPER(c) != ((string->size == 1) ? '/'
                     : context->element_stack->tag->name[string->size - 2]))
            {
                int i;

                /*  If complete match, end literal */
                if ((c == '>') && (!context->element_stack->tag->name[string->size - 2]))
                {
                    end_element(context, context->element_stack->tag);
                    string->size = 0;
                    context->current_attribute_number = INVALID;
                    context->state = S_text;
                    break;
                }               /* If Mismatch: recover string. */
                PUTC('<');
                for (i = 0; i < string->size; i++)  /* recover */
                    PUTC(string->data[i]);
                context->state = S_text;
            }

            break;

/*  Character reference or Entity
 */
        case S_ero:
            if (c == '#')
            {
                context->state = S_cro;     /*   &# is Char Ref Open */
                break;
            }
            context->state = S_entity;  /* Fall through! */

/*  Handle Entities
 */
        case S_entity:
            if (isalnum(c))
                HTChunkPutc(string, c);
            else
            {
                HTChunkTerminate(string);
                bStatus = handle_entity(context);
                context->state = S_text;

                if (!bStatus)
                {
                    /* Call ourselves reentrantly to handle the terminator
                       of an illegal entity */
                    SGML_character(context, c);
                }
            }
            break;

/*  Character reference
 */
        case S_cro:
            if (isalnum(c))
                HTChunkPutc(string, c);     /* accumulate a character NUMBER */
            else
            {
                int value;
                HTChunkTerminate(string);
                value = atoi(string->data);
                if (value)
                {
                    PUTC((char) value);
                }
                context->state = S_text;
            }
            break;

/*      Tag
 */
        case S_tag:         /* new tag */
            if (isalnum(c))
                HTChunkPutc(string, c);
            else if (c == '!')
                context->state = S_bang;
            else
            {                   /* End of tag name */
                HTTag *t;
                if (c == '/')
                {
                    context->state = S_end;
                    break;
                }
                HTChunkTerminate(string);

                t = SGMLFindTag(dtd, string->data);
                if (!t)
                {
                    XX_DMsg(DBG_SGML, ("SGML: *** Unknown element %s\n",
                                string->data));
                    context->state = (c == '>') ? S_text : S_junk_tag;
                    break;
                }
                context->current_tag = t;

                /*  Clear out attributes
                 */

                {
                    int i;
                    for (i = 0; i < context->current_tag->number_of_attributes; i++)
                        context->present[i] = NO;
                }
                string->size = 0;
                context->current_attribute_number = INVALID;

                if (c == '>')
                {
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                }
                else
                {
                    context->state = S_tag_gap;
                }
            }
            break;


        case S_tag_gap:     /* Expecting attribute or > */
            if (WHITE(c))
                break;          /* Gap between attributes */
            if (c == '>')
            {                   /* End of tag */
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_attr;    /* Get attribute */
            break;

            /* accumulating value */
        case S_attr:
            if (WHITE(c) || (c == '>') || (c == '='))
            {                   /* End of word */
                HTChunkTerminate(string);
                handle_attribute_name(context, string->data);
                string->size = 0;
                if (c == '>')
                {               /* End of tag */
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                    break;
                }
                context->state = (c == '=' ? S_equals : S_attr_gap);
            }
            else
            {
                HTChunkPutc(string, c);
            }
            break;

        case S_attr_gap:        /* Expecting attribute or = or > */
            if (WHITE(c))
                break;          /* Gap after attribute */
            if (c == '>')
            {                   /* End of tag */
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;
            }
            else if (c == '=')
            {
                context->state = S_equals;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_attr;    /* Get next attribute */
            break;

        case S_equals:          /* After attr = */
            if (WHITE(c))
                break;          /* Before attribute value */
            if (c == '>')
            {                   /* End of tag */
                XX_DMsg(DBG_SGML, ("SGML: found = but no value\n"));
                if (context->current_tag->name)
                    start_element(context);
                context->state = S_text;
                break;

            }
            else if (c == '\'')
            {
                context->state = S_squoted;
                break;

            }
            else if (c == '"')
            {
                context->state = S_dquoted;
                break;
            }
            HTChunkPutc(string, c);
            context->state = S_value;
            break;

        case S_value:
            if (WHITE(c) || (c == '>'))
            {                   /* End of word */
                HTChunkTerminate(string);
                handle_attribute_value(context, string->data);
                string->size = 0;
                if (c == '>')
                {               /* End of tag */
                    if (context->current_tag->name)
                        start_element(context);
                    context->state = S_text;
                    break;
                }
                else
                    context->state = S_tag_gap;
            }
            else if (c == '&')
            {
                /* Entities inside attribute values are not handled
                   identically to ones in regular markup, mostly for ease
                   of coding.  The 2.0 spec requires that entities end in
                   a semicolon.  Here we are a little more lenient and
                   assume as a terminator anything other than an alpha-
                   numeric character or a '#'. */
                context->extra = string->size;  /* Save where entity starts */
                HTChunkPutc(string, c);
                context->old_state = context->state;
                context->state = S_invalueent;
            }
            else
            {
                HTChunkPutc(string, c);
            }
            break;

        case S_squoted:     /* Quoted attribute value */
        case S_dquoted:     /* Quoted attribute value */
            if ((context->state == S_squoted && c == '\'') ||
                (context->state == S_dquoted && c == '"'))
            {
                /* End of attribute value */
                HTChunkTerminate(string);
                handle_attribute_value(context, string->data);
                string->size = 0;
                context->state = S_tag_gap;
            }
            else if (string->size < MAX_ATTR_LENGTH)
            {
                /* We restrict an attribute value to a finite length,
                   since if it goes longer than that it's probably an
                   error anyway. */
                if (c == '&')
                {
                    /* Entities inside attribute values are not handled
                       identically to ones in regular markup, mostly for ease
                       of coding.  The 2.0 spec requires that entities end in
                       a semicolon.  Here we are a little more lenient and
                       assume as a terminator anything other than an alpha-
                       numeric character or a '#'. */
                    context->extra = string->size;  /* Save where entity starts */
                    HTChunkPutc(string, c);
                    context->old_state = context->state;
                    context->state = S_invalueent;
                }
                else
                {
                    HTChunkPutc(string, c);
                }
            }
            break;

        case S_invalueent:  /* Accumulating entity in an attribute value */
            if (isalnum(c) || c == '#')
            {
                if (string->size < MAX_ATTR_LENGTH)
                {
                    /* We restrict an attribute value to a finite length,
                       since if it goes longer than that it's probably an
                       error anyway. */
                    HTChunkPutc(string, c);
                }
            }
            else
            {
                bStatus = SGML_HandleValueEntity(string, context->dtd, context->extra);
                context->state = context->old_state;
                if (!bStatus)
                {
                    /* Call ourselves reentrantly to handle the terminator
                       of an illegal entity */
                    SGML_character(context, c);
                }
            }
            break;

        case S_end:         /* </ */
            if (isalnum(c))
                HTChunkPutc(string, c);
            else
            {                   /* End of end tag name */
                HTTag *t;
                HTChunkTerminate(string);
                if (!*string->data)
                {               /* Empty end tag */
                    if (context->element_stack)
                        t = context->element_stack->tag;
                    else
                        t = NULL;
                }
                else
                {
                    t = SGMLFindTag(dtd, string->data);
                }
                if (!t)
                {
                    XX_DMsg(DBG_SGML, ("Unknown end tag </%s>\n", string->data ? string->data : ""));
                }
                else
                {
                    context->current_tag = t;
                    end_element(context, context->current_tag);
                }

                string->size = 0;
                context->current_attribute_number = INVALID;
                if (c != '>')
                {
                    context->state = S_junk_tag;
                }
                else
                {
                    context->state = S_text;
                }
            }
            break;

        case S_junk_tag:
            if (c == '>')
            {
                context->state = S_text;
            }
            break;

        case S_bang:
            /* We've gotten "<!" */
            if (c == '-')
                context->state = S_bangdash;
            else if (c == '>')
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!>'\n"));
                context->state = S_text;
            }
            else
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!%c...'\n", c));
                context->state = S_junk_tag;
            }
            break;
        
        case S_bangdash:
            /* We've gotten "<!-" */
            if (c == '-')
                context->state = S_comment;
            else if (c == '>')
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!->'\n"));
                context->state = S_text;
            }
            else
            {
                XX_DMsg(DBG_SGML, ("Malformed comment: '<!-%c...'\n", c));
                context->state = S_junk_tag;
            }
            break;
        
        case S_comment:
            /* We've gotten "<!--", and are looking for "-->" */
            if (c == '-')
                context->state = S_commentdash;
            break;
        
        case S_commentdash:
            if (c == '-')
                context->state = S_commentdashdash;
            else
                context->state = S_comment;
            break;
        
        case S_commentdashdash:
            if (c == '>')
                context->state = S_text;
            else if (c != '-')
                context->state = S_comment;
            break;

    }                           /* switch on context->state */
    return TRUE;
}                               /* SGML_character */
#endif // _GIBRALTAR

/*  One byte of the page as it came in: UTF-8 is decoded to ANSI first */
PRIVATE BOOL SGML_putByte(HTStream * context, char c)
{
    char buf[ANSI_MAX_CHARS];
    int i, n;

    if (!context->bUTF8)
        return SGML_character(context, c);

    n = ANSI_PutUTF8(&context->utf8, (unsigned char) c, buf);
    for (i = 0; i < n; i++)
        SGML_character(context, buf[i]);
    return TRUE;
}


PRIVATE BOOL SGML_string(HTStream * context, CONST char *str)
{
    CONST char *p;
    for (p = str; *p; p++)
        SGML_putByte(context, *p);
    /* Technically this should check the return values from SGML_character()
       but as of now that function always returns TRUE so I don't bother. */
    return TRUE;
}


PRIVATE BOOL SGML_write(HTStream * context, CONST char *str, int l)
{
    CONST char *p;
    CONST char *e = str + l;
    for (p = str; p < e; p++)
        SGML_putByte(context, *p);
    /* Technically this should check the return values from SGML_character()
       but as of now that function always returns TRUE so I don't bother. */
    if (*context->actions->block_done)
    {
        /* Notify the target stream that we've done a block's worth. */
        (*context->actions->block_done)(context->target);
    }
    return TRUE;
}

/*_______________________________________________________________________
*/

/*  Structured Object Class
   **   -----------------------
 */
PUBLIC HTStreamClass SGMLParser =
{
    "SGMLParser",
    SID_SGML_RECEIVING_HTML_S,                      
    SID_SGML_RECEIVING_HTML_S_S,                    
    NULL,
    SGML_free,
    SGML_abort,
    SGML_putByte,
    SGML_string,
    SGML_write,
};

/*  Create SGML Engine
**   ------------------
**
** On entry,
**   dtd     represents the DTD, along with
**   actions     is the sink for the data as a set of routines.
**
*/

PUBLIC HTStream *SGML_new(struct Mwin *tw, CONST SGML_dtd * dtd, HTStructured * target, HTRequest *request)
{
    int i;
    HTStream *context = (HTStream *) GTR_MALLOC(sizeof(*context));
    if (!context)
    {
        ERR_ReportError(tw, SID_DLG_OUT_OF_MEMORY, NULL, NULL);
        return NULL;
    }
    context->expected_length = request->content_length;

    if (context->expected_length)
        WAIT_SetRange(tw, 0, 100, context->expected_length);
    context->bytes_processed = 0;

    context->isa = &SGMLParser;
    context->string = HTChunkCreate(128);   /* Grow by this much */
    context->dtd = dtd;
    context->target = target;
    context->actions = (HTStructuredClass *) (((HTStream *) target)->isa);
    /* Ugh: no OO */
    context->state = S_text;
    context->crlf_state = CRLF_None;
    context->element_stack = 0; /* empty */
#ifdef CALLERDATA
    context->callerData = (void *) callerData;
#endif
    for (i = 0; i < MAX_ATTRIBUTES; i++)
        context->value[i] = 0;
    context->tw = tw;

    context->request = request;
    context->fInRecovery = FALSE;

    context->bUTF8 = ANSI_ReadAsUTF8(request);
    memset(&context->utf8, 0, sizeof(context->utf8));
    context->depth = 0;
    context->bHide = FALSE;
    context->hide_tag = NULL;
    context->hide_count = 0;
    context->hide_depth = 0;
    context->cs_source = (struct CharStream *) (context->actions->get_source ?  ((*context->actions->get_source)(context->target)) : NULL);

    return context;
}
