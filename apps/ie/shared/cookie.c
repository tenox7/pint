/* cookie.c -- Support for HTTP Cookies (based upon
 *             preliminary specification available
 *             November 1995).
 *
 * Implements cookie parsing/generation in HTTP messages.
 *
 * Jeff Hostetler, Spyglass, Inc., 1995.
 */

#include "all.h"

#ifdef FEATURE_HTTP_COOKIES

static char * gszServerCookie = "Set-Cookie";
static char * gszClientCookie = "Cookie";

/*****************************************************************/
/*****************************************************************/

struct cookie * Cookie_Free(struct cookie * p)
{
    struct cookie * next = NULL;
    
    if (p)
    {
        next = p->next;
        
        if (p->szName)
            GTR_FREE(p->szName);
        if (p->szValue)
            GTR_FREE(p->szValue);
        if (p->szExpires)
            GTR_FREE(p->szExpires);
        if (p->szDomain)
            GTR_FREE(p->szDomain);
        if (p->szPath)
            GTR_FREE(p->szPath);

        GTR_FREE(p);
    }

    return next;
}

/*****************************************************************/
/*****************************************************************/

#define X_IsWhite(c)            (((c)==' ')||((c)=='\t'))
#define X_IsKeyword(p,np,k,nk)  ((np==nk)&&(GTR_strncmpi(p,k,nk)==0))
#define X_PrintString(sz)       (((sz)?(sz):"[null]"))

static BOOL x_DupString(char ** pp, char * p, int n)
{
    XX_Assert((!*pp),("Cookie: x_DupString -- target possibly not freed."));
    
    *pp = GTR_CALLOC(1,n+1);

    if (!*pp)
        return FALSE;

    strncpy(*pp,p,n);
    (*pp)[n] = 0;
    return TRUE;
}
    
static int x_GetToken(char * sz, int nmax, char cDelimiter, char ** pnext)
{
    char * q;

    /* find the end of the first token and return its length.
     * also return pointer to where the caller should start
     * looking for the next token.
     *
     * we assume that we have no leading white space.
     */

    if (!sz || !*sz)
    {
        if (pnext)
            *pnext = NULL;
        return 0;
    }

    for (q=sz; (*q && (q < sz+nmax) && (*q != cDelimiter)); q++)
        ;

    /* set beginning of next token */
    
    if (pnext)
        *pnext = ((*q) ? q+1 : NULL);

    /* dismiss whitespace prior to delimiter */
    
    for (q--; ((q>sz) && X_IsWhite(*q)); q--)
        ;
    return (q+1-sz);
}   
    
static char * x_SkipLeadingWhite(char * sz)
{
    if (!sz)
        return NULL;
    
    while (*sz && X_IsWhite(*sz))
        sz++;

    if (!*sz)
        return NULL;

    return sz;
}

/*
    Cookie dates (RFC 6265): "Wed, 21 Oct 2026 07:28:00 GMT", the older
    "Wednesday, 21-Oct-26 07:28:00 GMT", "Wed Oct 21 07:28:00 2026" and
    other orders.  Returns the time, or -1 if it is not a date.  Dates
    before 1970 give 0, dates after 2037 the last time a time_t holds.
*/

#define X_IsDateChar(c)     (isalnum((unsigned char) (c)) || (c) == ':' || ((unsigned char) (c)) >= 0x7F)

static int x_DateNumber(char ** pp, int nMin, int nMax)
{
    /* 1 to nMax digits at *pp, then no more digits; -1 if not */
    char * p = *pp;
    int n, v;

    for (n = 0, v = 0; isdigit((unsigned char) *p); n++, p++)
        v = v * 10 + (*p - '0');
    if (n < nMin || n > nMax)
        return -1;
    *pp = p;
    return v;
}

static int x_DateMonth(char * s)
{
    static CONST char szMonths[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    int i;

    for (i = 0; i < 12; i++)
    {
        if (0 == GTR_strncmpi(s, szMonths + i * 3, 3))
            return i;
    }
    return -1;
}

static time_t x_InternDate(char * szDate)
{
    static CONST int aDaysBefore[12] = { 0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334 };
    char tok[32];
    char * p;
    char * q;
    int n;
    int hour = -1, min = -1, sec = -1, mday = -1, mon = -1, year = -1;
    long days;

    if (!szDate)
        return ((time_t)-1);

    for (p = szDate; *p; )
    {
        while (*p && !X_IsDateChar(*p))
            p++;
        for (n = 0; *p && X_IsDateChar(*p); p++)
        {
            if (n < (int) sizeof(tok) - 1)
                tok[n++] = *p;
        }
        tok[n] = 0;
        if (!n)
            break;

        q = tok;
        if (hour < 0 && (n = x_DateNumber(&q, 1, 2)) >= 0 && *q == ':')
        {
            int h = n, m, s;

            q++;
            if ((m = x_DateNumber(&q, 1, 2)) >= 0 && *q++ == ':' && (s = x_DateNumber(&q, 1, 2)) >= 0)
            {
                hour = h;
                min = m;
                sec = s;
                continue;
            }
        }
        q = tok;
        if (mday < 0 && (n = x_DateNumber(&q, 1, 2)) >= 0)
        {
            mday = n;
            continue;
        }
        if (mon < 0 && (n = x_DateMonth(tok)) >= 0)
        {
            mon = n;
            continue;
        }
        q = tok;
        if (year < 0 && (n = x_DateNumber(&q, 2, 4)) >= 0)
        {
            year = n;
            continue;
        }
    }

    if (year >= 70 && year <= 99)
        year += 1900;
    else if (year >= 0 && year <= 69)
        year += 2000;
    if (   hour < 0 || hour > 23 || min > 59 || sec > 59
        || mday < 1 || mday > 31 || mon < 0 || year < 1601)
    {
        return ((time_t)-1);
    }

    if (year < 1970)
        return 0;
    if (year > 2037)
        return LONG_MAX;

    days = (long) (year - 1970) * 365 + aDaysBefore[mon] + (mday - 1)
         + ((year - 1) / 4 - (year - 1) / 100 + (year - 1) / 400)
         - (1969 / 4 - 1969 / 100 + 1969 / 400);
    if (mon > 1 && (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)))
        days++;
    return (time_t) (days * 86400L + hour * 3600L + min * 60L + sec);
}
        
struct cookie * Cookie_Intern(char * text)
{
    /* given a text-based-cookie, convert it into an internal cookie.
     * this is used to parse the value of incomming HTTP headers.
     *
     * return NULL on error.
     */

    struct cookie * pcookie = NULL;
    char * ptoken = NULL;
    char * pname = NULL;
    char * pvalue = NULL;
    char * pnext = NULL;
    int ntoken, nname, nvalue;
    BOOL bMaxAge = FALSE;
    time_t tMaxAge = 0;

    pcookie = (struct cookie *)GTR_CALLOC(1,sizeof(struct cookie));
    if (!pcookie)
        return NULL;

    XX_DMsg(DBG_COOKIE,("COOKIE: Intern [%s]\n",text));

    /* set default values */
    
    pcookie->tExpires = ((time_t)-1);
    pcookie->bSecure = FALSE;
    

    /* find: [<name>=<value>[;]]+ */
    
    while ( (ptoken = x_SkipLeadingWhite(text)) )
    {
        /* find whole <name>[=<value>][;] pattern in [pn]token.
         * then find <name> within it into [pn]name.
         */
        
        ntoken = x_GetToken(ptoken,strlen(ptoken),';',&pnext);

        pname = ptoken;
        nname = x_GetToken(pname,ntoken,'=',&pvalue);

        pvalue = x_SkipLeadingWhite(pvalue);
        nvalue = ntoken - (pvalue - ptoken);
        if (!pvalue || nvalue < 0)
            nvalue = 0;             /* no '=' (as in "secure"): no value */

        if (X_IsKeyword(pname,nname,"expires",7))
        {
            if (nvalue)
                if (!x_DupString(&pcookie->szExpires,pvalue,nvalue))
                    goto Fail;
                else
                    pcookie->tExpires = x_InternDate(pcookie->szExpires);
            else
                pcookie->tExpires = ((time_t)-1);
        }
        else if (X_IsKeyword(pname,nname,"max-age",7))
        {
            /* [; max-age=<seconds>] wins over expires (RFC 6265).
             * zero or less means it has expired.
             */
            if (nvalue && (isdigit((unsigned char) *pvalue) || *pvalue == '-'))
            {
                long lAge = atol(pvalue);
                time_t tNow = time(NULL);

                bMaxAge = TRUE;
                if (lAge <= 0)
                    tMaxAge = 0;
                else if (lAge > LONG_MAX - tNow)
                    tMaxAge = LONG_MAX;
                else
                    tMaxAge = tNow + lAge;
            }
        }
        else if (X_IsKeyword(pname,nname,"path",4))
        {
            if (nvalue)
                if (!x_DupString(&pcookie->szPath,pvalue,nvalue))
                    goto Fail;
        }
        else if (X_IsKeyword(pname,nname,"domain",6))
        {
            if (nvalue)
                if (!x_DupString(&pcookie->szDomain,pvalue,nvalue))
                    goto Fail;
        }
        else if (X_IsKeyword(pname,nname,"secure",6))
        {
            /* [; secure]
             *
             * 'secure' does not have a value; if present, it
             * indicates that the cookie will only be sent
             * back to the server over HTTPS (ie, SSL).
             */

            pcookie->bSecure = TRUE;
        }
        else
        {
            /* anything else is a generic name/value pair
             * (only one of these per cookie).  we ignore
             * additional pairs.
             */
            
            if (nname && !pcookie->szName)
            {
                if (!x_DupString(&pcookie->szName,pname,nname))
                    goto Fail;
                if (nvalue)
                    if (!x_DupString(&pcookie->szValue,pvalue,nvalue))
                        goto Fail;
            }
        }
        
        text = pnext;
    }

    if (bMaxAge)
        pcookie->tExpires = tMaxAge;

    /* Note: let caller verify 'domain' and 'path' based upon current URL. */

    XX_DMsg(DBG_COOKIE,
            ("COOKIE: Intern [%s %s]\n\t\t[expires %s ===>> %s]\n\t\t[domain %s]\n"
             "\t\t[path %s]\n\t\t[secure %d]\n",
             X_PrintString(pcookie->szName),
             X_PrintString(pcookie->szValue),
             X_PrintString(pcookie->szExpires),
             (  (pcookie->tExpires == ((time_t)-1))
              ? "[end-of-session]"
              : asctime(gmtime(&pcookie->tExpires))),
             X_PrintString(pcookie->szDomain),
             X_PrintString(pcookie->szPath),
             pcookie->bSecure));
    
    return pcookie;

 Fail:
    (void)Cookie_Free(pcookie);

    XX_DMsg(DBG_COOKIE,("COOKIE: could not intern\n"));

    return NULL;
}

static BOOL x_DomainNameTooShort(char * szDomain)
{
    /* verify that the given domainname is long enough
     * (ie, has at least 2 components)
     */

    int nDots;
    char * p;

    for (nDots=0, p=szDomain; ((*p) && (nDots<2)); p++)
        if (*p == '.')
            nDots++;

    XX_DMsg(DBG_COOKIE,("COOKIE: domain name contains [%d] dots.\n",nDots));
    
    return (nDots < 2);
}

static BOOL x_DetermineIfSecure(HTRequest * request)
{
    /* return TRUE iff we will use a secure channel. */

    BOOL bResult = FALSE;
    char * szProtocol = HTParse(request->destination->szActualURL,"",PARSE_ACCESS);

    if (szProtocol)
    {
        bResult = (   (GTR_strcmpi(szProtocol,"https")==0)
                   /* || any other choices... */
                   );
        GTR_FREE(szProtocol);
    }

    return bResult;
}

static char * x_ExtractFQDN(const char * szURL)
{
    /* extract the Fully-Qualified-Domain-Name from the
     * given url.
     *
     * returns a string which the caller must free.
     */
    
    char * szHostname;
    char * p;

    szHostname = HTParse(szURL,"",PARSE_HOST);

    /* trim port, if present */
    
    p = strchr(szHostname,':');
    if (p)
        *p = 0;

    /* NOTE: we cannot be guarenteed of a FQDN without a
     * NOTE: DNS and Reverse-DNS lookup.  this is a bit
     * NOTE: expensive.  we can get the DNS info from the
     * NOTE: DNS cache (in httcp.c), but we would still
     * NOTE: need to go back for the FQDN.
     * NOTE:
     * NOTE: as long as the network hyperlinks are using
     * NOTE: FQDN's everything is ok.
     */

    return szHostname;
}

static char * x_ExtractPath(const char * szURL)
{
    /* extract the Pathname portion from the given url.
     *
     * returns a string which the caller must free.
     */

    char * szPathname;
    char * p;
    
    szPathname = HTParse(szURL,"",PARSE_PATH|PARSE_PUNCTUATION);

    /* NOTE: The spec is a bit vague here.  It says that if a
     * NOTE: path is not specified in the cookie, it is assumed
     * NOTE: to be the same path as the document.  Does this
     * NOTE: mean the pathname with or without the filename ?
     * NOTE: The feature is a bit useless if include the filename
     * NOTE: portion, so let's assume it's without.
     * NOTE:
     * NOTE: We truncate the string just past the last slash.
     */

    p = strrchr(szPathname,'/');
    if (p)
        p[1]=0;
    
    return szPathname;
}

static void x_VerifyDomain(struct cookie * pcookie, char * szHostname)
{
    /* examine pcookie->szDomain and check its validity given
     * the hostname that we think we sent the request to.
     *
     * if the cookie had a domain=<domain_name> pair, we must
     * verify that it contains at least 2 dots (to prevent
     * devious servers from saying ".com") and verify that
     * the tails of the <domain_name> and our <hostname> match.
     * if it fails to pass our tests, we silently substitute
     * the hostname.
     *
     * if the cookie did not specify a domain (or did not
     * specify a value for it), we substitute the hostname.
     */
    
    if (pcookie->szDomain)
    {
        int nLenDomain, nLenHostname, nTail;

        nLenDomain = strlen(pcookie->szDomain);
        nLenHostname = strlen(szHostname);
        nTail = nLenHostname - nLenDomain;
        
        if (   (nTail >= 0)
            && (GTR_strncmpi(pcookie->szDomain,szHostname+nTail,nLenDomain)==0)
            && ( ! x_DomainNameTooShort(pcookie->szDomain)))
        {
            /* domain name is ok */
            return;
        }

        XX_DMsg(DBG_COOKIE,
                ("COOKIE: Fixed up domain name from [%s] to [%s]\n",
                 pcookie->szDomain,szHostname));
        
        GTR_FREE(pcookie->szDomain);
        pcookie->szDomain = NULL;
    }
    
    (void)x_DupString(&pcookie->szDomain,szHostname,strlen(szHostname));            

    return;
}

static void x_VerifyPath(struct cookie * pcookie, char * szPathname)
{
    /* examine pcookie->szPath and check its validity.
     *
     * if omitted or invalid, substitute the pathname of the
     * current url.
     *
     */

    /* TODO verify that filename is not included in given pathname. */
    
    if (!pcookie->szPath)
        (void)x_DupString(&pcookie->szPath,szPathname,strlen(szPathname));

    return;
}

/*****************************************************************/
/*****************************************************************/

void Cookie_AppendToStream(struct CharStream * cs,
                           struct cookie * pc,
                           BOOL bSecure)
{
    /* convert cookie into an external representation for sending
     * to the HTTP server.
     */

    /* if the cookie is marked as secure-only, only send it
     * if we're transmitting on a secure channel.
     */

    if (pc->bSecure && !bSecure)
    {
        XX_DMsg(DBG_COOKIE,
                (("COOKIE: [%s=%s] is marked secure; "
                  "cannot send on open channel.\n"),
                 pc->szName,pc->szValue));
        return;
    }
    
    XX_DMsg(DBG_COOKIE,("COOKIE: Sending [%s=%s]\n",
                        pc->szName,pc->szValue));
    
    if (CS_GetLength(cs))
        CS_AddString(cs,"; ",2);
    CS_AddString(cs,pc->szName,strlen(pc->szName));
    CS_AddString(cs,"=",1);
    CS_AddString(cs,pc->szValue,strlen(pc->szValue));

    return;
}

/*****************************************************************/
/*****************************************************************/

void Cookie_FetchCookies(HTHeader * header, HTRequest * request)
{
    /* extract cookies from the incomming header and update
     * our database.
     */

    HTHeaderList * hl;
    struct cookie * pcookie;
    char * szHostname = NULL;
    char * szPathname = NULL;

    for (hl=header->first; hl; hl=hl->next)
        if (GTR_strcmpi(hl->name,gszServerCookie)==0)
        {
            pcookie = Cookie_Intern(hl->value);

            if (pcookie)
            {
                if (!szHostname)
                    szHostname = x_ExtractFQDN(request->destination->szActualURL);
                if (!szPathname)
                    szPathname = x_ExtractPath(request->destination->szActualURL);

                x_VerifyDomain(pcookie,szHostname);
                x_VerifyPath(pcookie,szPathname);

                if (!CookieDB_AddToDatabase(pcookie))
                    Cookie_Free(pcookie);
            }
        }

    if (szHostname)
        GTR_FREE(szHostname);
    if (szPathname)
        GTR_FREE(szPathname);

#ifdef XX_DEBUG
    CookieDB_DebugDump();
#endif

    return;
}

/*****************************************************************/
/*****************************************************************/

BOOL Cookie_SendCookies(HTHeader * header, HTRequest * request)
{
    /* based upon the details of the current outgoing
     * HTTP request we examine our cookie database and
     * place the proper headers in the header list
     * being constructed.
     *
     * return FALSE if any error occurs.
     */

    char * szHostname = NULL;
    char * szPathname = NULL;
    struct cookie_domain * pcdExactMatch = NULL;
    struct cookie_domain * pcdTailMatch = NULL;
    struct CharStream * cs = NULL;
    BOOL bSecure;
    BOOL bResult = FALSE;
    
    bSecure = x_DetermineIfSecure(request);
    
    szHostname = x_ExtractFQDN(request->destination->szActualURL);
    szPathname = x_ExtractPath(request->destination->szActualURL);

    if (!szHostname || !szPathname)
        goto Done;
    
    /* this is not in the spec (the pseudo-spec does not include
     * details on such boundary conditions), but i think it is how
     * it should be done.
     *
     * first we do an exact match on the domain with the destination
     * hostname and then tail matches on domains.  we send any
     * appropriate cookies found under any domain match.  we send
     * them in the order the domains are identified.  that is,
     * we do the exact match first; domains from tail matches are 
     * in an undefined order.
     *
     * for example, if the cookie database contains domains for
     * foo.bar.spyglass.com, .bar.spyglass.com, and .spyglass.com
     * and we are visiting foo.bar.spyglass.com, we would send
     * cookies found under all three.  if we are visiting
     * www.bar.spyglass.com, we would send cookies found under
     * the second two domains.
     *
     * we capture all out-going cookies into a CharStream.
     */
    
    pcdExactMatch = CookieDB_GetDomain(szHostname,TRUE,NULL);
    if (pcdExactMatch)
    {
        cs = CS_Create();
        if (!cs)
            goto Done;
        CookieDB_GetCookies(pcdExactMatch,cs,szPathname,bSecure);
    }

    pcdTailMatch = CookieDB_GetDomain(szHostname,FALSE,NULL);
    while (pcdTailMatch)
    {
        if (!cs)
        {
            cs = CS_Create();
            if (!cs)
                goto Done;
        }
        if (pcdTailMatch != pcdExactMatch)
            CookieDB_GetCookies(pcdTailMatch,cs,szPathname,bSecure);

        pcdTailMatch = CookieDB_GetDomain(szHostname,FALSE,pcdTailMatch);
    }

    if (cs)
    {
        if (CS_GetLength(cs))               /* we have some cookies to deliver. */
            HTHeaderList_SetNameValue(HTHeaderList_Append(header,
                                                          HTHeaderList_New()),
                                      gszClientCookie,
                                      CS_GetPool(cs));

        CS_Destroy(cs);
    }

    bResult = TRUE;

 Done:
    
    if (szHostname)
        GTR_FREE(szHostname);
    if (szPathname)
        GTR_FREE(szPathname);

    return bResult;
}

#endif /* FEATURE_HTTP_COOKIES */
