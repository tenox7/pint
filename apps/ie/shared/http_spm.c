/*
   Enhanced NCSA Mosaic from Spyglass
   "Guitar"

   Copyright 1994 Spyglass, Inc.
   All Rights Reserved

   Author(s):
      Jim Seidman       jim@spyglass.com
      Jeff Hostetler    jeff@spyglass.com

   Portions of this file were derived from
   the CERN libwww, version 2.15.
*/

#include "all.h"


#define HTTP_VERSION    "HTTP/1.1"

#define VERSION_LENGTH      20  /* for returned protocol version */

struct _HTStream
{
    HTStreamClass *isa;         /* all we need to know */
};

struct Data_LoadHTTP;   /* so this will compile on GCC */

PRIVATE int HTLoadHTTP_Async(struct Mwin *tw, int nState, void **ppInfo);
#ifdef FEATURE_SSL
PRIVATE int HTLoadHTTPS_Async(struct Mwin *tw, int nState, void **ppInfo);
#endif

PRIVATE int x_DoInitPart2(struct Mwin * tw, struct Data_LoadHTTP * pData);
static int x_AltUAFor(CONST char * url);
static void x_AltUserAgent(int nAltUA, char * line);

char szStbServerResponse[256] = "";
char szStbClientResponse[256] = "";

/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** Section to Construct an HTTP command.                         ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/

static BOOL x_SetCommandFields(HTHeader * h,
                               CONST unsigned char * url,
                               CONST HTMethod method,
                               int use_proxy)
{
    unsigned char *uri;
    CONST unsigned char *uri_1;
    unsigned char *command;
    BOOL bResult;

    if (use_proxy)
    {
        uri = NULL;
        uri_1 = url;
    }
    else
        uri_1 = uri = HTParse(url, "", PARSE_PATH | PARSE_PUNCTUATION);
    
    command = ((method != METHOD_INVALID) ? HTMethod_name(method) : "GET");
    bResult = HTHeader_SetCommandFields(h, command, uri_1, HTTP_VERSION);

    if (uri)
        GTR_FREE(uri);
    return bResult;
}

/*********************************************************************
 *
 * x_SetHostAndPort() -- Set server host and port info in header.
 *
 */
static BOOL x_SetHostAndPort(HTHeader * h,
                             CONST unsigned char * url,
                             int use_proxy)
{
    unsigned char * host;
    BOOL bResult;

    switch (use_proxy)
    {
        case PROXY_HTTP:
            host = HTParse(gPrefs.szProxyHTTP, "", PARSE_HOST);
            break;
        case PROXY_FTP:
            host = HTParse(gPrefs.szProxyFTP, "", PARSE_HOST);
            break;
        case PROXY_GOPHER:
            host = HTParse(gPrefs.szProxyGOPHER, "", PARSE_HOST);
            break;
        default:
            host = HTParse(url, "", PARSE_HOST);
            break;
    }

    bResult = HTHeader_SetHostAndPort(h,host);
    GTR_FREE(host);
    return bResult;
}

/*********************************************************************
 *
 * x_SetHostHeader() -- Create or update "Host: <host[:port]>" header.
 *
 * HTTP/1.1 requires it in every request.  It names the server in the
 * URL, also when the request goes through a proxy.
 *
 */
static BOOL x_SetHostHeader(HTHeader * h, CONST unsigned char * url)
{
    unsigned char * host;
    unsigned char * at;
    HTHeaderList * hl;
    BOOL bResult;

    host = HTParse(url, "", PARSE_HOST);
    if (!host)
        return FALSE;

    at = strrchr(host, '@');            /* drop any "user:password@" */

    hl = HTHeaderList_FindFirstHeader(h,"Host");
    if (!hl)
        hl = HTHeaderList_Append(h,HTHeaderList_New());
    bResult = HTHeaderList_SetNameValue(hl,"Host",(at ? at + 1 : host));
    GTR_FREE(host);
    return bResult;
}

/*********************************************************************
 *
 * x_CreateAcceptHeader() -- Create "Accept: <list>" lines.
 *
 * Accept: foo/bar
 * Accept: xxx/yyy, q=.5
 *
 * TODO:  For now, we build a full header line for each accept
 * TODO:  rather than use the ';' syntax and combine them on
 * TODO:  one line.
 */
static BOOL x_CreateAcceptHeader(HTHeader * h,
                                 CONST HTAtom output_format,
                                 HTList * conversions)
{
    HTAtom present;
    HTList *cur = conversions;
    HTPresentation *pres;

    present = ((output_format) ? output_format : WWW_PRESENT);

    while ((pres = (HTPresentation *) HTList_nextObject(cur)))
    {
        if (pres->rep_out == present)
        {
            HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
            if (!hl)
                return FALSE;
                
            if (!HTHeaderList_SetNameValue(hl,"Accept",HTAtom_name(pres->rep)))
                return FALSE;
            
            if (pres->quality != 1.0)
            {
                char v[10];
                
                HTHeaderSVList * svl = HTHeaderSVList_Append(hl,HTHeaderSVList_New());
                if (!svl)
                    return FALSE;

                sprintf(v,"%.3f",pres->quality);
                if (!HTHeaderSVList_SetNameValue(svl,"q", v, ";"))
                    return FALSE;
            }
        }
    }

    return TRUE;
}

/*********************************************************************
 *
 * x_CreateAcceptLanguageHeader() -- Create "Accept-Language: <...>" header.
 *
 */
static BOOL x_CreateAcceptLanguageHeader(HTHeader * h)
{
    if (gPrefs.szAcceptLanguageHeader[0])
    {
        HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
        if (!hl)
            return FALSE;
        if (!HTHeaderList_SetNameValue(hl,"Accept-Language",gPrefs.szAcceptLanguageHeader))
            return FALSE;
    }
    return TRUE;
}

/*********************************************************************
 *
 * x_CreateConnectionHeader() -- Create "Connection: close" header.
 *
 * HTTP/1.1 connections stay open by default, so we ask the server to
 * close each one after its response.  We don't keep HTTP connections:
 * the per-window connection cache loses its input buffer when a
 * response header fills more than one buffer, which is common with
 * HTTP/1.1 servers.
 *
 */
static BOOL x_CreateConnectionHeader(HTHeader * h)
{
    HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
    if (!hl)
        return FALSE;
    if (!HTHeaderList_SetNameValue(hl,"Connection","close"))
        return FALSE;

    return TRUE;
}

/*********************************************************************
 *
 * x_CreateUserAgentHeader() -- Create "User-Agent: <...>" header.
 *
 */
static BOOL x_CreateUserAgentHeader(HTHeader * h, CONST char * url)
{
    char line[256];
    int nAltUA;

    HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
    if (!hl)
        return FALSE;

    /*
        NOTE: vv_UserAgentString is declared in generic/shared/useragnt.h
        Please read the comments in that file
    */
    nAltUA = x_AltUAFor(url);
    if (nAltUA)
        x_AltUserAgent(nAltUA, line);   /* see x_ResendWithAltUA */
    else
        sprintf(line,"%s", vv_UserAgentString);

    if (!HTHeaderList_SetNameValue(hl,"User-Agent",line))
        return FALSE;
    
    return TRUE;
}

/*
    The following routine is used when accessing a RELOAD through
    a PROXY.  It passes the header "Pragma: no-cache", which tells
    the caching proxy server not to access its cache when returning
    the document.
*/
static BOOL x_CreateNoCacheHeader(HTHeader *h)
{
    HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
    if (!hl)
        return FALSE;

    if (!HTHeaderList_SetNameValue(hl,"Pragma","no-cache"))
        return FALSE;
    
    return TRUE;
}

/*********************************************************************
 *
 * x_CreateRefererHeader() -- Create "Referer: <ref>" header.
 *
 * Referer: foo
 *
 */
static BOOL x_CreateRefererHeader(HTHeader *h, CONST char *szReferer)
{
    if (szReferer)
    {
        HTHeaderList * hl = HTHeaderList_Append(h,HTHeaderList_New());
        if (!hl)
            return FALSE;

        if (!HTHeaderList_SetNameValue(hl,"Referer",szReferer))
            return FALSE;
    }

    return TRUE;
}

/*********************************************************************
 *
 * x_CreateContentHeaders() -- Create "Content-Type: <type>" header.
 *                          -- Create "Content-Length: <len>" header.
 *
 */
static BOOL x_CreateContentHeaders(HTHeader * h,
                                   CONST char * content_type,
                                   long content_length)
{
    char buf[30];
    HTHeaderList * hl;

    hl = HTHeaderList_Append(h,HTHeaderList_New());
    if (!hl)
        return FALSE;

    if (!HTHeaderList_SetNameValue(hl,"Content-type",content_type))
        return FALSE;

    hl = HTHeaderList_Append(h,HTHeaderList_New());
    if (!hl)
        return FALSE;

    sprintf(buf,"%ld",content_length);
    if (!HTHeaderList_SetNameValue(hl,"Content-length",buf))
        return FALSE;

    return TRUE;
}

/****************************************************************
 * HTTP_DealWithUrl() -- escape special characters in url string.
 *
 */
static BOOL HTTP_DealWithUrl(unsigned char * url, HTHeader * h, CONST HTMethod method, int use_proxy)
{
    unsigned char *url_20;
    unsigned char *p;
    unsigned char *p_20;
    unsigned long k;
    BOOL bSuccess;
    
    /* we need to convert spaces in the URL to %20.
     * count how many spaces there are in the URL.
     */

    for (p=url,k=0; *p; p++)
        if (*p==' ')
            k++;

    /* create a buffer to copy the new string
     * we substitute 3 chars ("%20") for each
     * space.  if we cannot malloc the string
     * just send it as it.
     */

    url_20 = NULL;
    if (k && ((url_20 = GTR_MALLOC(strlen(url) + k*2 + 1))))
    {
        for (p=url,p_20=url_20; *p; p++)
            if (*p==' ')
            {
                *p_20++ = '%';
                *p_20++ = '2';
                *p_20++ = '0';
            }
            else
                *p_20++ = *p;
        *p_20 = 0;
    }
    else
        url_20 = url;

    bSuccess = (   x_SetCommandFields(h,url_20,method,use_proxy)
                && x_SetHostAndPort(h,url_20,use_proxy)
                && x_SetHostHeader(h,url_20));

    if (k && url_20)
        GTR_FREE(url_20);

    return bSuccess;
}


/*********************************************************************
 *
 * x_CreateStandardRequest() -- Create a HTTP request using info
 *                           -- in the given HTRequest.
 *
 * Caller is responsible for freeing buffer when finished with it.
 *
 */
static HTHeader * x_CreateStandardRequest(HTRequest * request)
{
    HTHeader * h = HTHeader_New();

    if (h)
    {
        BOOL bSuccess = TRUE;

        h->bUsingProxy = request->destination->use_proxy;

        if (request->method == METHOD_POST)
        {
#ifndef FEATURE_SUPPORT_WRAPPING
            /* Per NSA suggestion, when wrapping is not supported,
             * the SPM cannot have acces to the actual
             * outgoing data for fear that the SPM might encrypt it behind
             * our back.  Therefore, we make a copy available to them (in
             * case they need to compute a hash on it as part of the
             * authentication process).
             *
             * To avoid bi-modal state, we always copy it here.
             */
#endif
            h->ubPostData = (UniBuffer *)GTR_strdup(request->szPostData);
            bSuccess &= (h->ubPostData != NULL);
        }
        
        /*
            Note that we send "Connection: close" on every HTTP request, so the
            server closes the connection after the response.
        */
        bSuccess = bSuccess && (   HTTP_DealWithUrl(request->destination->szActualURL,
                                         h,
                                         request->method,
                                         request->destination->use_proxy)
                     && x_CreateAcceptHeader(h,
                                             request->output_format,
                                             request->conversions)
                     && x_CreateUserAgentHeader(h, request->destination->szActualURL)
                     && x_CreateAcceptLanguageHeader(h)
                     && x_CreateConnectionHeader(h)
                     && x_CreateRefererHeader(h, request->referer));

#ifdef FEATURE_HTTP_COOKIES
        bSuccess = bSuccess && Cookie_SendCookies(h,request);
#endif /* FEATURE_HTTP_COOKIES */

        if (bSuccess && h->bUsingProxy && (request->iFlags & HTREQ_RELOAD))
        {
            bSuccess = bSuccess && x_CreateNoCacheHeader(h);    
        }

        if (   bSuccess
            && (request->method == METHOD_POST))
            bSuccess = x_CreateContentHeaders(h,
                                              HTAtom_name(request->content_type),
                                              UniBufferSize(h->ubPostData));

        if (bSuccess)
            return h;

        HTHeader_Delete(h);
    }

    return NULL;
}

/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** End Section to Construct an HTTP Command.                     ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/


/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** Begin Section to Asynchronously Communicate an HTTP Command.  ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/

struct Data_LoadHTTP {
    HTRequest *         request;
    int *               pStatus;
    int                 cWaiting;
    HTInputSocket *     isoc;
    const char *        arg;
    int                 s;              /* Socket number for returned data */
    int                 net_status;
    int                 bytes;          /* Number of bytes read */
    char *              status_line;
    HTStream *          target;
    BOOL                bRedirect;
    char *              complete;
    struct _CachedConn  *pCon;
    unsigned long       where;
    struct MultiAddress address;
    unsigned short      port;
    char *              pszHost;

    int                 unwrap_status;
    HTInputSocket *     isocUnwrap;
    HTHeader *          hReq;
    HTHeader *          hRes;
    HTHeaderList *      hlProtocol;
    HTSPM *             htspm;
    int                 ht_status;
    unsigned int        nAttempt;
    HTHeader *          hNewReq;
    HTSPMStatusCode     htspm_status;
    int                 nModalDialogStatus;

    BOOL                bAltUA;         /* sent again with another User-Agent, see x_ResendWithAltUA */
    int                 nRefusalReads;  /* extra reads for x_RefusesUA */

    BOOL                bChunked;       /* Transfer-Encoding: chunked */
    int                 chunk_state;    /* CHUNK_* */
    long                chunk_left;     /* bytes left in chunk, -1 before its size */

    OpaqueOSData        osd;

#ifdef WIN32
    int                 connect_attempts;
#endif /* WIN32 */
#ifdef FEATURE_SSL
       char security;
#endif
};

#define STATE_HTTP_GOTHOST          (STATE_OTHER + 0)
#define STATE_HTTP_CONNECTED        (STATE_OTHER + 1)
#define STATE_HTTP_SENT             (STATE_OTHER + 2)
#define STATE_HTTP_GOTSTATUS        (STATE_OTHER + 3)
#define STATE_HTTP_COPYING          (STATE_OTHER + 5)
#define STATE_HTTP_GOT_SEM_FOR_401402   (STATE_OTHER + 6)
#define STATE_HTTP_RAN401402DIALOG  (STATE_OTHER + 7)
#define STATE_HTTP_DIDUNWRAP        (STATE_OTHER + 8)
#define STATE_HTTP_DIDLOADFORUNWRAP (STATE_OTHER + 9)
#define STATE_HTTP_GOT_SEM_FOR_PPRQ (STATE_OTHER + 10)
#define STATE_HTTP_RAN_PPRQ_DIALOG  (STATE_OTHER + 11)
#define STATE_HTTP_DIDWRAP          (STATE_OTHER + 12)
#define STATE_HTTP_GOTMOREBODY      (STATE_OTHER + 13)

/* chunk_state values, for Transfer-Encoding: chunked */
#define CHUNK_SIZE          0       /* chunk-size hex digits */
#define CHUNK_EXT           1       /* rest of the chunk-size line */
#define CHUNK_DATA          2       /* chunk-data */
#define CHUNK_DATA_END      3       /* CRLF after chunk-data */
#define CHUNK_TRAILER       4       /* start of a trailer line */
#define CHUNK_TRAILER_LINE  5       /* rest of a trailer line */
#define CHUNK_DONE          6       /* last-chunk and trailer read */


/*****************************************************************/
/*****************************************************************/

static int HTTP_CleanUp(struct Data_LoadHTTP *pData)
{
    XX_Assert((pData->cWaiting==0), ("HTTP_CleanUp: WAIT stack not fully popped\n"));
    if (pData->s && !pData->request->bKeepAlive)
    {
        XX_DMsg(DBG_WWW, ("HTTP: close socket %d.\n", pData->s));
        Net_Close(pData->s);
        pData->s = 0;
    }

    if (pData->isoc && !pData->request->bKeepAlive)
    {
        if (pData->isoc == pData->pCon->isoc)
        {
            TW_DisposeConnection(pData->pCon);
        }
        else
        {
            XX_DMsg(DBG_WWW, ("Killing isoc: 0x%x\n", pData->isoc));
            HTInputSocket_freeChain(pData->isoc);
        }
        pData->isoc = NULL;
    }
    if (pData->status_line)
    {
        GTR_FREE(pData->status_line);
        pData->status_line = NULL;
    }
    
    pData->request->content_length = 0;

    if (pData->hReq)
    {
        HTHeader_Delete(pData->hReq);
        pData->hReq = NULL;
    }
    if (pData->hRes)
    {
        HTHeader_Delete(pData->hRes);
        pData->hRes = NULL;
    }

    return STATE_DONE;
}

/*****************************************************************/
/*****************************************************************/

static BOOL x_spm_init(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    HTHeader * hNewReq = NULL;

    pData->osd.tw = tw;
    pData->osd.request = pData->request;
    
    pData->nAttempt = 0;
    
    pData->hReq = x_CreateStandardRequest(pData->request);
    if (!pData->hReq)
        return FALSE;

    /* Give SPM's a chance to state their existance */
    
    HTSPM_OS_DoListAbilities((void *)&pData->osd,pData->hReq);

    /* Give SPM's a chance to guess and preload auth info */

    pData->hNewReq = NULL;
    pData->htspm_status = HTSPM_OS_TryPPReq((void *)&pData->osd,pData->hReq,&pData->hNewReq,&pData->htspm);

    return TRUE;
}
    
#ifdef FEATURE_SUPPORT_WRAPPING
static int x_WrapRequest(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    struct Params_WrapRequest * prq;
    
    WAIT_Update(tw,waitSameInteract,GTR_GetString(SID_INF_SECURING_DOCUMENT));

    prq = GTR_CALLOC(1,sizeof(*prq));
    if (!prq)
    {
        XX_DMsg(DBG_LOAD,("x_WrapRequest: malloc failed for wrap setup.\n"));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }
    
    prq->pStatus = &pData->unwrap_status;
    prq->htspm_status = &pData->htspm_status;
    prq->htspm = pData->htspm;
    prq->pw.hRequest = pData->hReq;
    prq->pw.phNewRequest = &pData->hNewReq;
    prq->osd.tw = tw;
    prq->osd.request = pData->request;

    pData->unwrap_status = 0;
    pData->hNewReq = NULL;
    Async_DoCall(Wrap_Async,prq);
    return STATE_HTTP_DIDWRAP;
}

static int HTTP_DidWrap(struct Mwin * tw, void ** ppInfo)
{
    struct Data_LoadHTTP * pData = *ppInfo;

    if (   (pData->unwrap_status < 0)
        || (HTSPM_IsAnyError(pData->htspm_status)))
    {
        XX_DMsg(DBG_LOAD,("HTTP_DidWrap: Wrap failed.\n"));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    return x_DoInitPart2(tw,pData);
}
#endif /* FEATURE_SUPPORT_WRAPPING */


static int x_DoInitPart2(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    struct Params_MultiParseInet *ppi;

    if (pData->htspm_status == HTSPM_STATUS_MUST_WRAP)
    {
#ifdef FEATURE_SUPPORT_WRAPPING
        return x_WrapRequest(tw,pData);
#else
        {
            char buf[512], szError[1024];

            strcpy(buf, GTR_GetString(SID_ERR_REQUEST_ABORTED_DUE_TO_NO_ENVELOPING_1));
            strcat(buf, GTR_GetString(SID_ERR_REQUEST_ABORTED_DUE_TO_NO_ENVELOPING_2));
            strcat(buf, GTR_GetString(SID_ERR_REQUEST_ABORTED_DUE_TO_NO_ENVELOPING_3));

            sprintf(szError, buf, pData->htspm->szProtocolName,vv_Application);

            ERR_ReportError(tw, SID_ERR_SIMPLY_SHOW_ARGUMENTS_S_S, szError, NULL);
            *pData->pStatus = -2;
            return STATE_DONE;
        }
#endif /* FEATURE_SUPPORT_WRAPPING */
    }
    
    if (pData->htspm_status == HTSPM_STATUS_SUBMIT_NEW)
    {
        HTHeader_Delete(pData->hReq);
        pData->hReq = pData->hNewReq;
        pData->hNewReq = NULL;
    }
    
    pData->isoc = NULL;
    pData->request->content_length = 0;
    pData->arg = pData->request->destination->szActualURL;
    pData->bytes = 0;

    if (!pData->arg || !*pData->arg)
    {
        /* Illegal if no name or zero-length */
        *pData->pStatus = -2;
        return STATE_DONE;
    }
    
    /*  Set up defaults */
#ifdef FEATURE_SSL
        if (pData->security == SECURITY_SSL)
          pData->port = WS_HTONS(TCP_SSL_PORT);
        else if (pData->security == SECURITY_NONE)
#endif
    pData->port = WS_HTONS(TCP_PORT);       /* Default: http port    */

    /* Get node name and optional port number */
    switch (pData->request->destination->use_proxy)
    {
        case PROXY_HTTP:
            pData->pszHost = HTParse(gPrefs.szProxyHTTP, "", PARSE_HOST);
            break;
        case PROXY_FTP:
            pData->pszHost = HTParse(gPrefs.szProxyFTP, "", PARSE_HOST);
            break;
        case PROXY_GOPHER:
            pData->pszHost = HTParse(gPrefs.szProxyGOPHER, "", PARSE_HOST);
            break;
        default:
            pData->pszHost = HTParse(pData->arg, "", PARSE_HOST);
            break;
    }

    ppi = GTR_MALLOC(sizeof(*ppi));
    ppi->pAddress = &pData->address;
    ppi->pPort = &pData->port;
    ppi->str = pData->pszHost;
    ppi->pStatus = &pData->net_status;
    Async_DoCall(Net_MultiParse_Async, ppi);
    return STATE_HTTP_GOTHOST;
}

struct xx_pprq
{
    HTSPMStatusCode * htspm_status;
    OpaqueOSData osd;
    struct Mwin * tw;
    HTRequest * request;
    HTSPM * htspm;
    HTHeader * hReq;
    HTHeader ** hNewReq;
#ifdef MAC
    ThreadID    tid;
#endif
};

#ifndef UNIX
static void xx_CallPreProcessRequest(void * p)
{
    /* make (possibly) blocking call to _PreProcessRequest method. */

    struct xx_pprq * ppprq = p;

    ppprq->osd.tw = ppprq->tw;
    ppprq->osd.request = ppprq->request;
    
    *ppprq->htspm_status = HTSPM_OS_PreProcessRequest(UI_UserInterface,
                                                      (void *)&ppprq->osd,
                                                      ppprq->htspm,
                                                      ppprq->hReq,
                                                      ppprq->hNewReq,
                                                      FALSE);

#ifdef MAC
    Async_UnblockThread(ppprq->tid);
    Sem_SignalSem_Sync(&gModalDialogSemaphore);
    GTR_FREE(ppprq);
#endif
    
    return;
}
#endif  /* !UNIX */

#ifdef MAC  /* Do _PreProcessRequest Modal Dialog from Non-Thread Context */
static int x_SetUpForPPReqDialog(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* Get the modal dialog semaphore */
    struct Params_SemData *ppsd;
    
    ppsd = GTR_MALLOC(sizeof(*ppsd));
    ppsd->pStatus = &pData->nModalDialogStatus;
    ppsd->semaphore = &gModalDialogSemaphore;
    Async_DoCall(Sem_WaitSem_Async, ppsd);
    return STATE_HTTP_GOT_SEM_FOR_PPRQ;
}

static int HTTP_DoGotSemForPPRQ(struct Mwin *tw, void **ppInfo)
{
    struct xx_pprq * ppprq;
    struct Data_LoadHTTP    *pData;

    pData = *ppInfo;
    
    if (!pData->nModalDialogStatus)
    {
        /* Something went wrong in the semaphore code. */
        /* go on as if no guess was attempted. */
        return x_DoInitPart2(tw,pData);
    }
    
    ppprq = GTR_MALLOC(sizeof(*ppprq));
    ppprq->tw = tw;
    ppprq->request = pData->request;
    ppprq->htspm = pData->htspm;
    ppprq->hReq = pData->hReq;
    ppprq->hNewReq = &pData->hNewReq;
    ppprq->htspm_status = &pData->htspm_status;
    ppprq->tid = Async_GetCurrentThread();
    
    MacGlobals.mdft.pfnCallback = xx_CallPreProcessRequest;
    MacGlobals.mdft.pData = ppprq;
    Async_BlockThread(ppprq->tid);
    return STATE_HTTP_RAN_PPRQ_DIALOG;
}
#endif  /* MAC */

#ifdef WIN32    /* Do _PreProcessRequest Modal Dialog from Non-Thread Context */
static int x_SetUpForPPReqDialog(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* a blocking modal dialog is required by _PreProcessRequest, force it to
     * go up in a non-thread context.
     */
    struct Params_mdft * pmdft;
    struct xx_pprq * ppprq;

    //
    // Load the status bar message if it hasn't been loaded before
    //
    if (!*szStbClientResponse)
    {
        GTR_GetStringAbsolute(SID_INF_PROCESSING_CLIENT_RESPONSE, 
            szStbClientResponse, sizeof(szStbClientResponse));
    }
    
    pmdft = GTR_CALLOC(1,sizeof(*pmdft)+sizeof(struct xx_pprq));
    if (!pmdft)
    {
        /* could not get enough memory to wait on the dialog. */
        /* go on as if no guess was attempted. */
        return x_DoInitPart2(tw,pData);
    }
        
    pmdft->tw = tw;
    pmdft->pStatus = &pData->nModalDialogStatus;
    pmdft->fn = xx_CallPreProcessRequest;
    ppprq = (struct xx_pprq *)(((unsigned char *)pmdft)+sizeof(*pmdft));
    pmdft->args = ppprq;
    pmdft->msg1 = szStbClientResponse;
    ppprq->tw = tw;
    ppprq->request = pData->request;
    ppprq->htspm = pData->htspm;
    ppprq->hReq = pData->hReq;
    ppprq->hNewReq = &pData->hNewReq;
    pData->hNewReq = NULL;
    ppprq->htspm_status = &pData->htspm_status;
    Async_DoCall(MDFT_RunModalDialog_Async,pmdft);
    return STATE_HTTP_RAN_PPRQ_DIALOG;
}
#endif /* WIN32 */

#ifndef UNIX
static int HTTP_DoRanPPRQDialog(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP *pData;

    pData = *ppInfo;
    if (pData->nModalDialogStatus != 1)
        pData->htspm_status = HTSPM_ERROR;

    return x_DoInitPart2(tw,pData);
}
#endif /* !UNIX */

static int HTTP_DoInit(struct Mwin *tw, void **ppInfo)
{
    /* Do all of the HTTP stuff up to the connect call */

    struct Data_LoadHTTP    *pData;

    /* Copy the parameters we were passed into our own, larger structure. */
    {
        struct Params_LoadAsync *pParams;
        pParams = *ppInfo;
        pData = GTR_MALLOC(sizeof(struct Data_LoadHTTP));
        memset(pData, 0, sizeof(*pData));
        pData->request = pParams->request;
        pData->pStatus = pParams->pStatus;
#ifdef FEATURE_SSL
                pData->security = pParams->security;
#endif

        GTR_FREE(pParams);
        *ppInfo = pData;
    }

    if (!x_spm_init(tw,pData))
    {
        *pData->pStatus = -2;
        return STATE_DONE;
    }

#ifndef UNIX
    if (pData->htspm_status == HTSPM_STATUS_WOULD_BLOCK)
        return x_SetUpForPPReqDialog(tw,pData);
#endif /* !UNIX */
        
    return x_DoInitPart2(tw,pData);
}


static int HTTP_DoGotHost(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP    *pData;

    pData = *ppInfo;

    if (pData->pszHost)
    {
        GTR_FREE(pData->pszHost);
        pData->pszHost = NULL;
    }

    if (pData->net_status)
    {
        XX_DMsg(DBG_LOAD, ("Inet_Parse_Async returned %d\n", pData->net_status));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    pData->cWaiting++;
    WAIT_Push(tw, waitSameInteract, GTR_GetString(SID_INF_CONNECTING_TO_HTTP_SERVER));

    /* See if our cached connection is for this site and port
       (We cache one connection per window) */
    pData->pCon = &tw->cached_conn;
    if  (   
            (pData->pCon->type == CONN_HTTP)
            && Net_CompareAddresses(pData->pCon->addr, &pData->address)
            && (pData->pCon->port == pData->port)
        )
    {
        /* The address is correct.  Confirm that the socket
           is still open. */
        if (!Net_FlushSocket(pData->pCon->socket))
        {
            XX_DMsg(DBG_LOAD, ("KEEPALIVE: Using cached connection!\n"));
            pData->s = pData->pCon->socket;
            pData->isoc = pData->pCon->isoc;
            return STATE_HTTP_CONNECTED;
        }
        else
        {
            /* The other side closed the connection on us. */
            XX_DMsg(DBG_LOAD, ("HTTP: Cached connection closed by other side\n"));
        }
    }

    /* The cached connection wasn't useful.  Get rid of it. */
    TW_DisposeConnection(pData->pCon);
            
    {
        /* Do connect call */
        struct Params_MultiConnect *ppc;

#ifdef WIN32
        pData->connect_attempts++;
#endif /* WIN32 */

        ppc = GTR_MALLOC(sizeof(*ppc));
        ppc->pSocket = &pData->s;
        ppc->pAddress = &pData->address;
        ppc->nPort = pData->port;
        ppc->pWhere = &pData->where;
        ppc->pStatus = &pData->net_status;

#ifdef FEATURE_SOCKS_LOW_LEVEL
        ppc->bUseSocksProxy = pData->request->destination->bUseSocksProxy;
#endif

        Async_DoCall(Net_MultiConnect_Async, ppc);
    }
    return STATE_HTTP_CONNECTED;
}

static int HTTP_DoConnected(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP    *pData;

    pData = *ppInfo;

    WAIT_Pop(tw);
    pData->cWaiting--;
    if (pData->net_status < 0)
    {
#ifdef WIN32
        /*
            This code checks to see if the reason that the connect failed was
            an ENOBUFS error.  If so, we return to try and do it again.
        */
        if (WS_WSAGETLASTERROR() == WSAENOBUFS)
        {
            XX_DMsg(DBG_SOCK, ("connect failed on ENOBUFS -- attempt %d\n", pData->connect_attempts));
            if (pData->connect_attempts < 32)
            {
                pData->net_status = 0;
                return STATE_HTTP_GOTHOST;
            }
        }
#endif /* WIN32 */

        XX_DMsg(DBG_LOAD | DBG_WWW,
                ("Unable to connect to remote host for %s (errno = %d)\n",
                 pData->arg, errno));
        *pData->pStatus = HTInetStatus("connect");
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    XX_DMsg(DBG_WWW, ("HTTP connected, socket %d\n", pData->s));

#ifdef FEATURE_SSL
    if (pData->security == SECURITY_SSL)
    {
        /* TLS with the host in the URL (https never goes through a proxy).
           The handshake happens when the request is sent. */
        char *pszHost;
        BOOL bStarted;

        pszHost = HTParse(pData->arg, "", PARSE_HOST);
        bStarted = pszHost && SSL_Start(pData->s, pszHost);
        if (pszHost)
            GTR_FREE(pszHost);
        if (!bStarted)
        {
            *pData->pStatus = -1;
            HTTP_CleanUp(pData);
            return STATE_DONE;
        }
    }
#endif /* FEATURE_SSL */

    WAIT_Push(tw,
              waitPartialInteract,
              GTR_GetString(SID_INF_CONNECTING_TO_HTTP_SERVER));
    pData->cWaiting++;

#ifndef FEATURE_SUPPORT_WRAPPING
    if (pData->request->method == METHOD_POST)
    {
        /* We gave the SPM a copy of the post data.  If we do not support
         * wrapping, we need to refresh the copy (per NSA suggestion, (in
         * case the SPM modified it in violation of our security policy)).
         */
        strcpy((char *)pData->hReq->ubPostData,(char *)pData->request->szPostData);
    }
#endif

    /* Convert HTHeader data structure to a large text block. */
    
    pData->complete = HTHeader_TranslateToBuffer(pData->hReq);
    if (!pData->complete)
    {
        XX_DMsg(DBG_LOAD | DBG_WWW, ("Unable to Translate Request to buffer.\n"));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    /* Do the send */
    {
        struct Params_Send *pps;

        pps = GTR_MALLOC(sizeof(*pps));
        pps->socket = pData->s;
        pps->pBuf = pData->complete;
        pps->nBufLen = strlen(pData->complete);
        pps->pStatus = &pData->net_status;
        Async_DoCall(Net_Send_Async, pps);
        return STATE_HTTP_SENT;
    }
}

/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** End Section to Asynchronously Communicate an HTTP Command.    ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/

/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** Begin Section to Asynchronously Receive an HTTP Response.     ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/

static int HTTP_DoSent(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP    *pData;
    struct Params_Isoc_GetHeader *pigh;

    pData = *ppInfo;

    GTR_FREE(pData->complete);
    pData->complete = NULL;
    WAIT_Pop(tw);
    pData->cWaiting--;
    if (pData->net_status < 0)
    {
        XX_DMsg(DBG_WWW, ("HTTPAccess: Unable to send command.\n"));
        XX_DMsg(DBG_LOAD, ("Unable to send command (status = %d)\n", pData->net_status));
#ifdef FEATURE_SSL
        if (pData->security == SECURITY_SSL)
            ERR_ReportError(tw, SID_ERR_SSL_FAILED_S_S, pData->request->destination->szRequestedURL, SSL_ErrorText(pData->s));
#endif /* FEATURE_SSL */
        *pData->pStatus = HTInetStatus("send");
#ifdef _GIBRALTAR
        if ( pData->isoc == tw->cached_conn.isoc )
        {
            /* If Send fails on Kept-alive connection,  */
            /* retry on a new connection.  - CWilso     */
            TW_DisposeConnection(pData->pCon);
            return STATE_HTTP_GOTHOST;
        }
#endif
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    if (!pData->isoc)
    {
        /*
            pData->isoc MIGHT already be set via a keepalive
        */
        pData->isoc = HTInputSocket_new(pData->s);
    }

    /* Get header data in a chain of blocks */

    pigh = GTR_MALLOC(sizeof(*pigh));
    pigh->isoc = pData->isoc;
    pigh->isocChain = pData->isoc;
    pigh->pStatus = &pData->net_status;
    pigh->ndxEOH1 = 0;
    pigh->ndxEOH2 = 0;
    Async_DoCall(Isoc_GetHeader_Async, pigh);

    return STATE_HTTP_GOTSTATUS;
}

/*********************************************************************
 *
 * x_DealWithVersion0Server() -- Check for HTTP 0 server.
 *
 */
static HTFormat x_DealWithVersion0Server(CONST char * arg,
                                         HTRequest * request,
                                         HTInputSocket * isoc)
{
    /* HACK for HTTP0 servers.
    ** -----------------
    **
    **   HTTP0 servers must return ASCII style text, though it can in
    **   principle be just text without any markup at all.
    **   Full HTTP servers must return a response
    **   line and RFC822 style header.  The response must therefore in
    **   either case have a CRLF somewhere soon.
    **
    **   This is the theory.  In practice, there are (1993) unfortunately
    **   many binary documents just served up with HTTP0.9.  This
    **   means we have to preserve the binary buffer (on the assumption that
    **   conversion from ASCII may lose information) in case it turns
    **   out that we want the binary original.
    */

    /* Kludge to trap binary responses from illegal HTTP0.9 servers.
    ** First time we have enough, look at the stub in ASCII
    ** and get out of here if it doesn't look right.
    **
    ** We also check for characters above 128 in the first few bytes, and
    ** if we find them we forget the html default.
    **
    ** Bugs: A HTTP0.9 server returning a document starting "HTTP/"
    **   will be taken as a HTTP 1.0 server.  Failure.
    **   An HTTP 0.9 server returning a binary document with
    **   characters < 128 will be read as ASCII.
    */

    HTFormat format_in;
    
    format_in = HTFileFormat(arg, &request->content_encoding, &request->content_language);
    if (format_in == WWW_BINARY && !(HTInputSocket_seemsBinary(isoc)))
    {
        format_in = WWW_HTML;
    }

    return format_in;
}

/*********************************************************************
 *
 * x_ExtractServerStatus() -- Get server status code from status line.
 *
 */
static int x_ExtractServerStatus(CONST char * status_line)
{
    char server_version[VERSION_LENGTH + 1];
    int server_status;

    XX_DMsg(DBG_WWW, ("HTTP Status Line: Rx: %.70s\n", status_line));

    sscanf(status_line, "%20s%ld", server_version, &server_status);

    return server_status;
}

/*********************************************************************
 *
 * x_HasToken() -- See if a header value contains a word, in any case.
 *
 */
static BOOL x_HasToken(CONST char * value, CONST char * token)
{
    int len = strlen(token);

    for ( ; *value; value++)
        if (0 == GTR_strncmpi(value, token, len))
            return TRUE;
    return FALSE;
}

/*
    Some vendors want Mosaic to show the error messages returned by the server.
    If this routine returns HT_OK, then any message object returned will be
    shown.  If this routine returns HT_LOADED, then any message object will
    be ignored.
*/

/*********************************************************************
 *
 * x_FilterServerStatus() -- Convert server status to error/action code.
 *
 */
static int x_FilterServerStatus(struct Mwin *tw, int server_status, struct Data_LoadHTTP * pData)
{
    switch (server_status / 100)
    {
    case 2:                             /* Good: Got MIME object */
        if (server_status == 204)
        {
            /* Not so good - this link doesn't go anywhere */
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
                ERR_ReportError(tw, SID_ERR_NO_DESTINATION_FOR_LINK_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }
        }

        /* Assume any other 2xx response will come with a MIME object */
        return HT_OK;

    case 3:                             /* Various forms of redirection */
        pData->bRedirect = TRUE;
        return HT_REDIRECTION_ON_FLY;
        
    case 4:                             /* Access Authorization problem */
        switch (server_status)
        {
        case 400:
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
                ERR_ReportError(tw, SID_ERR_SERVER_SAYS_INVALID_REQUEST_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }

        case 401:
            return HT_401;

        case 402:
            return HT_402;
            
        case 403:
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
                ERR_ReportError(tw, SID_ERR_SERVER_DENIED_ACCESS_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }

        case 404:
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
                ERR_ReportError(tw, SID_ERR_SERVER_COULD_NOT_FIND_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }

#ifdef SHTTP_STATUS_CODES
        case 420:
            return HT_401;              /* map '420 SecurityRetry' into 401*/
            
        case 421:                       /* complain on '421 BogusHeader' */
            ERR_ReportError(tw, SID_ERR_SIMPLY_SHOW_ARGUMENTS_S_S, GTR_GetString(SID_ERR_SHTTP_ERROR), pData->status_line);
            return HT_LOADED;
#endif

        default:                        /* bad number */
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
                ERR_ReportError(tw, SID_ERR_STRANGE_HTTP_SERVER_RESPONSE_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }
        }

#ifdef _GIBRALTAR
        //
        // 500 Series errors
        //
        case 5:
            if (server_status == 502)
            {
                char * pch = pData->status_line;
                //char * pch2;
                //
                // Messages will be of the format "HTTML1/0 502 10022 Invalid parameter (something)\r\n".
                // The relevant portion consists of the  502 until the end of the message
                //
                while (*pch && (*pch != ' '))
                {
                    ++pch;
                }
                XX_Assert((*pch == ' '), ("Status line invalid format"));
                if (*pch == ' ')
                {
                    ++pch;
                }
                /*
                if (pch2 = strchr(pch, '\r'))
                {
                    *pch2 = '\0';
                }
                */
                if (gPrefs.bShowServerErrors)
                {
                    return HT_OK;
                }
                else
                {
                    ERR_ReportError(tw, SID_ERR_SERVER_ERR_5XX, pData->request->destination->szRequestedURL, pch);
                    return HT_LOADED;
                }
            }
            else
            {
                if (gPrefs.bShowServerErrors)
                {
                    return HT_OK;
                }
                else
                {
                    ERR_ReportError(tw, SID_ERR_COULD_NOT_FIND_ADDRESS_S, pData->request->destination->szRequestedURL, NULL);
                    return HT_LOADED;
                }
            }
#else
    case 5:                             /* I think you goofed */
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
            ERR_ReportError(tw, SID_ERR_COULD_NOT_FIND_ADDRESS_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }
#endif /* _GIBRALTAR */

    default:                            /* bad number */
            if (gPrefs.bShowServerErrors)
            {
                return HT_OK;
            }
            else
            {
            ERR_ReportError(tw, SID_ERR_STRANGE_HTTP_SERVER_RESPONSE_S, pData->request->destination->szRequestedURL, NULL);
                return HT_LOADED;
            }
    }
    /*NOTREACHED*/
}

static int x_ConvertMonth(char *s)
{
    if (0 == GTR_strncmpi(s, "jan", 3))
    {
        return 0;
    }
    if (0 == GTR_strncmpi(s, "feb", 3))
    {
        return 1;
    }
    if (0 == GTR_strncmpi(s, "mar", 3))
    {
        return 2;
    }
    if (0 == GTR_strncmpi(s, "apr", 3))
    {
        return 3;
    }
    if (0 == GTR_strncmpi(s, "may", 3))
    {
        return 4;
    }
    if (0 == GTR_strncmpi(s, "jun", 3))
    {
        return 5;
    }
    if (0 == GTR_strncmpi(s, "jul", 3))
    {
        return 6;
    }
    if (0 == GTR_strncmpi(s, "aug", 3))
    {
        return 7;
    }
    if (0 == GTR_strncmpi(s, "sep", 3))
    {
        return 8;
    }
    if (0 == GTR_strncmpi(s, "oct", 3))
    {
        return 9;
    }
    if (0 == GTR_strncmpi(s, "nov", 3))
    {
        return 10;
    }
    if (0 == GTR_strncmpi(s, "dec", 3))
    {
        return 11;
    }
}

/*
    According to the HTTP spec, dates can come to us in any of three formats.  This code attempts to be
    as forgiving as possible, and accept any of the three formats.
*/
static time_t x_ParseHTTPDate(char *s)
{
    char *p;
    struct tm tim;
    int year;
    time_t result;

    memset(&tim, 0, sizeof(tim));

    p = s;
    while (*p && !isdigit(*p)) p++;
    if (!*p) return 0;

#ifdef _GIBRALTAR
    //
    // This is not in the official http specs, but allowed by NetScape:
    // expires 0 should mean "always out of date".  Since 0 is an invalid
    // value, we use 1, which is just as good
    //
    if (*p == '0')
    {
        return 1;
    }
#endif // _GIBRALTAR

    tim.tm_mday = atoi(p);

    while (*p && !isalpha(*p))
    {
        p++;
    }

    if (*p)
    {
        tim.tm_mon = x_ConvertMonth(p);
        while (*p && !isdigit(*p)) p++;
        year = atoi(p);
        if (year > 99)
        {
            year -= 1900;
        }
        tim.tm_year = year;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_hour = atoi(p);
        while (*p && (*p != ':')) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_min = atoi(p);
        while (*p && (*p != ':')) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_sec = atoi(p);

        result = mktime(&tim);
        if (result == ((time_t) -1))
        {
            return 0;
        }
        else
        {
            return result;
        }
    }
    else
    {
        /* This date must be in asctime() format -- yech */

        p = s;
        while (*p && !isspace((unsigned char)*p)) p++;
        if (!*p) return 0;
        while (*p && !isalpha((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_mon = x_ConvertMonth(p);
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_mday = atoi(p);
        while (*p && !isspace((unsigned char)*p)) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_hour = atoi(p);
        while (*p && (*p != ':')) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_min = atoi(p);
        while (*p && (*p != ':')) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        tim.tm_sec = atoi(p);
        while (*p && !isspace((unsigned char)*p)) p++;
        if (!*p) return 0;
        while (*p && !isdigit((unsigned char)*p)) p++;
        if (!*p) return 0;
        year = atoi(p);
        if (year > 99)
        {
            year -= 1900;
        }
        tim.tm_year = year;

        result = mktime(&tim);
        if (result == ((time_t) -1))
        {
            return 0;
        }
        else
        {
            return result;
        }
    }
    /* not reached */
}

static int x_ExtractMimeInfo(struct Data_LoadHTTP * pData)
{
    HTHeaderList * hl;
    int format_in;
                                    
    XX_Assert((pData->hRes),("HTTP_DoGotStatus: hRes null for WWW_MIME."));

#ifdef FEATURE_HTTP_COOKIES
    Cookie_FetchCookies(pData->hRes,pData->request);
#endif /* FEATURE_HTTP_COOKIES */

    /*
        We sent "Connection: close" (see x_CreateConnectionHeader), so we
        never keep the connection, whatever the server sends back.
    */
    pData->request->bKeepAlive = FALSE;

    pData->request->atomCharset = 0;    /* none named, see ANSI_ReadAsUTF8 */

    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Content-Type");
    if (hl && hl->value)
    {
        HTHeaderList *hl_new;
        HTHeaderSVList *sv;

        hl_new = HTHeaderList_ParseValue(hl);

        /*
            It's necessary to parse out sub-values on Content-type, because
            there could be a charset or a multipart MIME boundary there.
        */

        format_in = HTAtom_for(GTR_MakeStringLowerCase(hl_new->value));
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found content-type switching to [%s]\n",hl->value));

        /* XHTML is HTML here (Google sends it to phones, see x_RefusesUA) */
        if (format_in == HTAtom_for("application/xhtml+xml"))
            format_in = WWW_HTML;

        for (sv = hl_new->sub_value; sv; sv = sv->next)
        {
            if (0 == GTR_strcmpi(sv->name, "charset"))
            {
                pData->request->atomCharset = HTAtom_for(sv->value);
            }
            else if (0 == GTR_strcmpi(sv->name, "boundary"))
            {
                pData->request->atomBoundary = HTAtom_for(sv->value);
            }
        }

        HTHeaderList_Delete(hl_new);
    }
    else
    {
        format_in = 0;
        /* See fragment at end of this function */
    }

    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Content-Length");
    if (hl && hl->value)
    {
        char * junk;
        pData->request->content_length = strtol(hl->value,&junk,10);
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found content-length of [%s]\n",hl->value));
    }

    /*
        HTTP/1.1 servers may send the body in chunks.  The chunks give the
        length, so any Content-Length must be ignored.
    */
    pData->bChunked = FALSE;
    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Transfer-Encoding");
    if (hl && hl->value && x_HasToken(hl->value, "chunked"))
    {
        pData->bChunked = TRUE;
        pData->chunk_state = CHUNK_SIZE;
        pData->chunk_left = -1;
        pData->request->content_length = 0;
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found transfer-encoding of [%s]\n",hl->value));
    }

    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Last-Modified");
    if (hl && hl->value)
    {
        pData->request->last_modified = x_ParseHTTPDate(hl->value);
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found Last-Modified of [%s]\n",hl->value));
    }
    
    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Expires");
    if (hl && hl->value)
    {
        /* TODO check for +5 syntax */
        pData->request->expires = x_ParseHTTPDate(hl->value);
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found Expires of [%s]\n",hl->value));
    }
    
    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Content-Transfer-Encoding");
    if (hl && hl->value)
    {
        pData->request->content_encoding = HTAtom_for(hl->value);
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found content-encoding switching to [%s]\n",hl->value));
    }

    hl = HTHeaderList_FindFirstHeader(pData->hRes,"Location");
    if (hl && hl->value)
    {
        char *url;
        char *temp;

        /*
            The code below is designed to handle relative redirects,
            which are supported by NSCP but illegal according to the
            IETF draft specs.
        */
        if (0 == (temp = HTParse(hl->value, "", PARSE_ACCESS)))
        {
            GTR_FREE(temp);

            url = HTParse(hl->value, pData->request->destination->szActualURL, PARSE_ALL);
            if (url)
            {
                Dest_UpdateActual(pData->request->destination, url);
                XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found RELATIVE (illegal by spec) location redirect switching to [%s]\n",url));
                GTR_FREE(url);
            }
            else
            {
                XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found RELATIVE (illegal by spec), but HTParse failed!\n"));
            }
        }
        else
        {
            Dest_UpdateActual(pData->request->destination, hl->value);
            XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: found location redirect switching to [%s]\n",hl->value));
        }
    }

#if 1
    if (pData->request->destination->use_proxy)
    {
        if ((format_in == WWW_PLAINTEXT) || (format_in == WWW_BINARY))
        {
            HTAtom old_format_in;
            HTAtom old_content_encoding;

            old_format_in = format_in;
            old_content_encoding = pData->request->content_encoding;

            format_in = HTFileFormat(pData->request->destination->szActualURL, &pData->request->content_encoding, NULL);

            if ((format_in == WWW_PLAINTEXT) || (format_in == WWW_BINARY))
            {
                format_in = old_format_in;
                 pData->request->content_encoding = old_content_encoding;
            }
            else
            {
                XX_DMsg(DBG_WWW, ("NOTE: Retrieving URL: %s\n\tUsing proxy server, and received MIME type of %s(%s).  Overriding, based on suffix of actual URL to %s(%s)\n",
                    pData->request->destination->szActualURL,
                    HTAtom_name(old_format_in), HTAtom_name(old_content_encoding),
                    HTAtom_name(format_in), HTAtom_name(pData->request->content_encoding)
                    ));
            }
        }
    }
#endif

    if (!format_in)
    {
        /*
            format_in was set to 0 above if Content-type was absent.  If Content-Transfer-Encoding was
            present, this will override it.
        */  
#if 0
        format_in = WWW_BINARY;
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: content-type not found -- assuming [application/octet-stream]\n"));
#else
#ifdef _GIBRALTAR
        //
        // For http: protocols, if there's no content-type, we assume html to be the default.
        // for all other protocols, we look at the extention
        //
        // Apparently this is the way netscape handles this:
        //
        /* Editorial comment -- the following line is really an ugly hack. -EWS */
        if ( *pData->request->destination->szActualURL == 'h')
        {
            format_in = WWW_HTML;
        }
        else
        {
            format_in = HTFileFormat(pData->request->destination->szActualURL, &pData->request->content_encoding, &pData->request->content_language);
            XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: content-type not found -- guessing it based on URL suffix\n"));
        }
#else
        format_in = HTFileFormat(pData->request->destination->szActualURL, &pData->request->content_encoding, &pData->request->content_language);
        XX_DMsg(DBG_LOAD | DBG_WWW, ("HTTP_DoGotStatus: content-type not found -- guessing it based on URL suffix\n"));
#endif /* !_GIBRALTAR */
#endif
    }

    return format_in;
}

static int x_SetUpForCopying(struct Mwin * tw,
                             struct Data_LoadHTTP * pData,
                             HTFormat format_in,
                             HTAtom encoding)
{
    /*  Set up the stream stack to handle the body of the message */

    /*
        At this point, the last_modified field in pData->request should
        be valid.  If it is set to 0, then we will refuse to use any
        cached version.
    */
    if (gPrefs.bEnableDiskCache && pData->request->last_modified && (!(pData->request->iFlags & HTREQ_RELOAD)))
    {
        struct CacheFileInformation *cfi;

        cfi = DCACHE_CheckForCachedURL(pData->request->destination->szActualURL, NULL, NULL, NULL);
        if (cfi)
        {
            /*
                The only way we could get here is that the object is in the cache, but
                it was decided earlier that the the cached version needs to be compared
                against the real copy.
            */

            if (pData->request->last_modified > cfi->tLastModified)
            {

                /*
                    The copy on the wire is newer.  Let's delete the cached version now.
                */
                DCACHE_DeleteCachedURL(pData->request->destination->szActualURL);
            }
            else
            {
                /*
                    If we get here, then we've checked the Last-Modified date,
                    and found that the copy in the cache is ok, so we need to
                    kill off what we were doing HTTP-wise, and just get the
                    copy in the cache.
                */
                extern  HTProtocol HTDCache;
                struct Params_LoadAsync *p2;

                cfi->bVerifiedThisSession = TRUE;

                DCACHE_RegisterCacheHit(cfi);

                p2 = GTR_CALLOC(sizeof(*p2), 1);
                if (p2)
                {
                    memcpy(p2, pData, sizeof(*p2));
                    Async_DoCall(HTDCache.load_async, p2);
                }
                /*
                    TODO do we need to check a result here?
                */

                HTTP_CleanUp(pData);
                return STATE_DONE;
            }
        }
    }

    pData->target = HTStreamStack(tw, format_in, pData->request);
    if (!pData->target)
    {
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    if (!(pData->request->iFlags & HTREQ_BINARY))
    {
        if (wild_match(HTAtom_for("text/*"),format_in)
            || encoding == HTAtom_for("8bit")
            || encoding == HTAtom_for("7bit"))
        {
            pData->target = HTNetToText(pData->target); /* Pipe through CR stripper */
            if (!pData->target)
            {
                *pData->pStatus = -1;
                HTTP_CleanUp(pData);
                return STATE_DONE;
            }
        }
    }

    HTSetStreamStatus(tw, pData->target, pData->request);

    /*
        TODO when we support encoding negotation, then here is
        where we create the unzip stream.
    */

    /*
        Note that we refuse to cache anything which does not have
        Last-Modified set properly
    */
    if (gPrefs.bEnableDiskCache)
    {
        if (pData->request->last_modified)
        {
            struct CacheFileInformation *cfi;

            /*
                We are about to retrieve a new HTTP object, so we need to prepare
                the main disk cache to contain it, by starting a new cache entry.

                This just starts the new entry and creates a CacheWriter stream.  The
                CacheWriter stream will actually write the data to disk, and it
                will complete the cache entry when its free() method is called.
            */
            cfi = DCACHE_BeginNewCacheEntry(pData->request->destination->szActualURL, format_in, pData->request->content_length);
            if (cfi)
            {
                HTStream *cachewriter;

                cachewriter = HTCacheWriter_create(pData->target, cfi, pData->request->destination->szActualURL);
                if (cachewriter)
                {
                    DCACHE_RegisterCacheHit(cfi);
                    cfi->tLastModified = pData->request->last_modified;
                    cfi->tExpires = pData->request->expires;

                    pData->target = cachewriter;
                }
            }
        }
        else
        {
            /*
                OK, the disk cache is active, but we can't cache the object we're currently getting,
                since it's Last-Modified date isn't valid.  We now need to check to be sure we don't
                have this object in the cache already, and purge it if we do.
            */
            struct CacheFileInformation *cfi;

            cfi = DCACHE_CheckForCachedURL(pData->request->destination->szActualURL, NULL, NULL, NULL);
            if (cfi)
            {
                DCACHE_DeleteCachedURL(pData->request->destination->szActualURL);
            }
        }
    }

    /*  Push the data down the stream.
    **
    **  We have to remember the end of the partial buffer (containing
    **  the end of the header) that we just read.
    **
    **  Instead of processing the first chunk now, we just fix up our
    **  internal state and let the DoCopying process it.
    */
    pData->net_status = 1;
    pData->bytes = 0;

    /*
        Now is the time to check to see if the server promised to leave our
        connection open, and act properly.  If so, then

        1.  We should trust the Content-Length header explicitly, and
            end our data when we've read that many bytes.

        2.  We should remember the socket we're working with, so we
            can re-use it next time.
    */

    if (pData->request->bKeepAlive)
    {
        /* Fill in connection information */
        if (pData->pCon->type != CONN_HTTP)
        {
            pData->pCon->addr = pData->where;
            pData->pCon->socket = pData->s;
            pData->pCon->type = CONN_HTTP;
            pData->pCon->isoc = pData->isoc;
            pData->pCon->port = pData->port;
        }
    }

    if (pData->target->isa->init_Async)
    {
        /* The stream has an async initialization function that needs to be called
           (probably to put up a dialog) before we continue. */
        struct Params_InitStream *pis;

        pis = GTR_CALLOC(1,sizeof(*pis));
        if (pis)
        {
            pis->me = pData->target;
            pis->request = pData->request;
            pis->pResult = &pData->net_status;
            Async_DoCall(pData->target->isa->init_Async, pis);
        }
        else
        {
            return STATE_ABORT;
        }
    }
    return STATE_HTTP_COPYING;
}

/****************************************************************/
/****************************************************************/
/** Deal with 401/402                                          **/
/****************************************************************/
/****************************************************************/


void SPM_set_url(struct Mwin * tw, HTRequest * request, UI_SetUrl * psu)
{
    /* This is needed to allow redirects within 401/402.
     * (useful for a 'click here to apply for an account'
     *  at the bottom of a 'please enter your account'
     *  dialog.)
     */

    Dest_UpdateActual(request->destination,psu->szUrl);
    HTTP_DealWithUrl(psu->szUrl,psu->hRequest,
                     HTMethod_enum(psu->hRequest->command),
                     request->destination->use_proxy);
    return;
}


static int x_PostProcess_401_402(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* direct control based upon _ProcessResponse return code. */
    
    switch (pData->htspm_status)
    {
    case HTSPM_STATUS_RESUBMIT_OLD:     /* assume that spm has fixed-up original request */
        pData->hNewReq = pData->hReq;
        pData->hReq = NULL;
        /* fall-thru intended */
    case HTSPM_STATUS_SUBMIT_NEW:       /* assume that spm has created a new request */
        HTTP_CleanUp(pData);
        pData->hReq = pData->hNewReq;
        return (x_DoInitPart2(tw,pData));   /* loop back into our state machine */

    default:
        /* user probably pressed cancel (or the dialog
         * forced it (eg too many bad pin's)).
         * we must assume that the module explained to
         * the user why we are not going to try again.
         * fall-thru and put up the default server
         * response (which may have some important
         * information on it).
         */
        return x_SetUpForCopying(tw, pData,
                                 x_ExtractMimeInfo(pData),
                                 pData->request->content_encoding);
    }
    /*NOTREACHED*/
}


#ifndef UNIX
struct xx_cpr
{
    HTSPMStatusCode * htspm_status;
    OpaqueOSData osd;
    struct Mwin * tw;
    HTRequest * request;
    HTSPM * htspm;
    HTHeaderList * hlProtocol;
    HTHeader * hReq;
    HTHeader * hRes;
    HTHeader ** hNewReq;

#ifdef MAC
    ThreadID tid;
#endif
};

static void xx_CallProcessResponse(void * p)
{
    /* make (possibly) blocking call to _ProcessResponse method. */

    struct xx_cpr * pcpr = p;

    pcpr->osd.tw = pcpr->tw;
    pcpr->osd.request = pcpr->request;
    
    *pcpr->htspm_status = HTSPM_OS_ProcessResponse(UI_UserInterface,
                                                   (void *)&pcpr->osd,
                                                   pcpr->htspm,
                                                   pcpr->hlProtocol,
                                                   pcpr->hReq,
                                                   pcpr->hRes,
                                                   pcpr->hNewReq,
                                                   FALSE);
    
#ifdef MAC
    Async_UnblockThread(pcpr->tid);
    Sem_SignalSem_Sync(&gModalDialogSemaphore);
    GTR_FREE(pcpr);
#endif
    return;
}
#endif /* !UNIX */

#ifdef MAC  /* Do _PreProcessRequest Modal Dialog from Non-Thread Context */
static int x_SetUpForModalDialog(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* Get the modal dialog semaphore */
    struct Params_SemData *ppsd;
    
    ppsd = GTR_MALLOC(sizeof(*ppsd));
    ppsd->pStatus = &pData->nModalDialogStatus;
    ppsd->semaphore = &gModalDialogSemaphore;
    Async_DoCall(Sem_WaitSem_Async, ppsd);
    return STATE_HTTP_GOT_SEM_FOR_401402;
}

static int HTTP_DoGotSemFor401402(struct Mwin * tw, void **ppInfo)
{
    struct xx_cpr * pcpr;
    struct Data_LoadHTTP    *pData;

    pData = *ppInfo;
    
    if (!pData->nModalDialogStatus)
    {
        /* Something went wrong in the semaphore code. */
        /* copy server response (text body accompanying 401/402) */
        HTFormat format_in;
        format_in = x_ExtractMimeInfo(pData);
        return x_SetUpForCopying(tw,pData, format_in, pData->request->content_encoding);
    }
    
    pcpr = GTR_MALLOC(sizeof(*pcpr));
    pcpr->tw = tw;
    pcpr->request = pData->request;
    pcpr->htspm = pData->htspm;
    pcpr->hlProtocol = pData->hlProtocol;
    pcpr->hReq = pData->hReq;
    pcpr->hRes = pData->hRes;
    pcpr->hNewReq = &pData->hNewReq;
    pcpr->htspm_status = &pData->htspm_status;
    pcpr->tid = Async_GetCurrentThread();
    
    MacGlobals.mdft.pfnCallback = xx_CallProcessResponse;
    MacGlobals.mdft.pData = pcpr;
    Async_BlockThread(pcpr->tid);
    return STATE_HTTP_RAN401402DIALOG;
}
#endif  /* MAC */

#ifdef WIN32
static int x_SetUpForModalDialog(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* a blocking modal dialog is required by _ProcessRequest, force it to
     * go up in a non-thread context.
     */
    struct Params_mdft * pmdft;
    struct xx_cpr * pcpr;

    //
    // Load message if this is the first time this function is called
    //
    if (!*szStbServerResponse)
    {
        GTR_GetStringAbsolute(SID_INF_PROCESSING_SERVER_RESPONSE, 
            szStbServerResponse, sizeof(szStbServerResponse));
    }

    pmdft = GTR_CALLOC(1,sizeof(*pmdft)+sizeof(struct xx_cpr));
    if (!pmdft)
    {
        /* could not get enough memory to wait on the dialog. */
        /* copy server response (text body accompanying 401/402) */
        HTFormat format_in;
        format_in = x_ExtractMimeInfo(pData);
        return x_SetUpForCopying(tw,pData, format_in, pData->request->content_encoding);
    }
        
    pmdft->tw = tw;
    pmdft->pStatus = &pData->nModalDialogStatus;
    pmdft->fn = xx_CallProcessResponse;
    pcpr = (struct xx_cpr *)(((unsigned char *)pmdft)+sizeof(*pmdft));
    pmdft->args = pcpr;
    pmdft->msg1 = szStbServerResponse;
    pcpr->tw = tw;
    pcpr->request = pData->request;
    pcpr->htspm = pData->htspm;
    pcpr->hlProtocol = pData->hlProtocol;
    pcpr->hReq = pData->hReq;
    pcpr->hRes = pData->hRes;
    pcpr->hNewReq = &pData->hNewReq;
    pData->hNewReq = NULL;
    pcpr->htspm_status = &pData->htspm_status;
    Async_DoCall(MDFT_RunModalDialog_Async,pmdft);
    return STATE_HTTP_RAN401402DIALOG;
}
#endif /* WIN32 */

#ifndef UNIX
static int HTTP_DoRan401402Dialog(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP *pData;

    pData = *ppInfo;
    if (pData->nModalDialogStatus != 1)
        pData->htspm_status = HTSPM_ERROR;

    return x_PostProcess_401_402(tw,pData);
}
#endif

#ifdef UNIX
static int x_SetUpForModalDialog(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    /* on unix, dialog startup code returns immediately.
     * so for us, the first call does the dialog setup.
     * we do the second call after the dialog has been
     * taken down.
     */

    WAIT_Update(tw,waitSameInteract,GTR_GetString(SID_INF_PROCESSING_SERVER_RESPONSE));
    return STATE_HTTP_RAN401402DIALOG;
}

static int HTTP_DoRan401402Dialog(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP *pData;

    pData = *ppInfo;

    pData->osd.tw = tw;
    pData->osd.request = pData->request;
    pData->htspm_status = HTSPM_OS_ProcessResponse(UI_UserInterface, (void *)&pData->osd,
                                                   pData->htspm, pData->hlProtocol,
                                                   pData->hReq, pData->hRes, &pData->hNewReq,
                                                   FALSE);

    return x_PostProcess_401_402(tw,pData);
}
#endif /* UNIX */


static int x_Process_401_402(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    HTFormat format_in;

    if (pData->nAttempt > 2)        /* hack to prevent infinite loops */
    {
        /* they are just guessing.  put up server default message
         * incase it may be of help to them.  they can always try
         * again later.
         */
        ERR_ReportError(tw, SID_ERR_AUTHENTICATION_FAILED_ACCESS_DENIED, NULL, NULL);

        /* copy server response (text body accompanying 401/402) */
        format_in = x_ExtractMimeInfo(pData);
        return x_SetUpForCopying(tw,pData, format_in, pData->request->content_encoding);
    }

    /* let the spm's do their thing.  (put up a password dialog or supply a cached one.) */
        
    pData->nAttempt++;
    pData->hlProtocol = NULL;
    pData->htspm = HTSPM_SelectProtocol(pData->ht_status,pData->hRes,&pData->hlProtocol);
    if (!pData->htspm)
    {
        /* no matching protocol module was found. */

        /* copy server response (text body accompanying 401/402) */
        format_in = x_ExtractMimeInfo(pData);
        return x_SetUpForCopying(tw,pData, format_in, pData->request->content_encoding);
    }

    /* an appropriate protocol module is available
     * to process the 401/402.  so let them have it.
     * first check to see if the spm can process it
     * without blocking (no modal dialogs).
     */

    pData->osd.tw = tw;
    pData->osd.request = pData->request;

    pData->hNewReq = NULL;
    pData->htspm_status = HTSPM_OS_ProcessResponse(UI_UserInterface, (void *)&pData->osd,
                                                   pData->htspm, pData->hlProtocol,
                                                   pData->hReq, pData->hRes, &pData->hNewReq,
                                                   TRUE);

    if (pData->htspm_status == HTSPM_STATUS_WOULD_BLOCK)
        return x_SetUpForModalDialog(tw,pData);

    return x_PostProcess_401_402(tw,pData);
}

#ifdef FEATURE_SUPPORT_UNWRAPPING
static HTInputSocket * x_GetLastIsoc(HTInputSocket * isoc)
{
    /* search chain and get last item */
    
    if (!isoc)
        return NULL;

    while (isoc->isocNext)
        isoc=isoc->isocNext;
    return isoc;            
}
static int x_get_content_length(HTHeader * hResponse)
{
    int len = 0;
    HTHeaderList * hl = HTHeaderList_FindFirstHeader(hResponse,"Content-Length");

    if (hl && hl->value)
    {
        char * junk;
        len = strtol(hl->value,&junk,10);
        XX_DMsg(DBG_SPM, ("Unwraping: found content-length of [%s]\n",hl->value));
    }

    return len;
}
static int x_SetUpForUnwrap(struct Mwin * tw, struct Data_LoadHTTP * pData)
{
    if (pData->isoc->input_file_number > 0)
    {
        /* accumulate entire document (sans http header) in isoc chain,
         * give spm the chain, accept back a new chain (which includes
         * http header), and loop back into our state machine to process
         * the new chain.
         */
        struct Params_Isoc_GetWholeDoc * pg;

        /* Let GetWholeDoc read the entire document from the network. */

        pg = GTR_CALLOC(1,sizeof(struct Params_Isoc_GetWholeDoc));
        pg->isoc = x_GetLastIsoc(pData->isoc);
        pg->isocCurrent = NULL;
        pg->pStatus = &pData->net_status;
        pg->content_length = x_get_content_length(pData->hRes);

        WAIT_Update(tw,waitSameInteract,GTR_GetString(SID_INF_LOADING_SECURED_DOCUMENT));

        Async_DoCall(Isoc_GetWholeDoc_Async,pg);
    }
    else
    {
        /* must be a recursive unwrap */
    }
    
    return STATE_HTTP_DIDLOADFORUNWRAP;
}

static int HTTP_DidLoadForUnwrap(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP * pData = *ppInfo;
    struct Params_UnwrapResponse * pur;
    
    if (pData->net_status < 0)
    {
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    WAIT_Update(tw,waitSameInteract,GTR_GetString(SID_INF_OPENING_SECURED_DOCUMENT));

    pur = GTR_CALLOC(1,sizeof(*pur));
    pur->pStatus = &pData->unwrap_status;
    pur->htspm_status = &pData->htspm_status;
    pur->pisocInput = &pData->isoc;
    pur->htspm = pData->htspm;
    pur->pd.hlProtocol = pData->hlProtocol;
    pur->pd.hRequest = pData->hReq;
    pur->pd.hResponse = pData->hRes;
    pur->osd.tw = tw;
    pur->osd.request = pData->request;

    pData->unwrap_status = 0;
    pData->hNewReq = NULL;
    pData->isocUnwrap = NULL;

    Async_DoCall(Unwrap_Async,pur);
    return STATE_HTTP_DIDUNWRAP;
}

static int HTTP_DidUnwrap(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP * pData = *ppInfo;

    if (   (pData->unwrap_status < 0)
        || (HTSPM_IsAnyError(pData->htspm_status)))
    {
        XX_DMsg(DBG_LOAD,("HTTP_DidUnwrap: unwrap failed.\n"));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    /* take the unwrapped chain that we just received from
     * the module and make it the input chain and pretend
     * that we just got it off the net.  jump back into our
     * state machine.  we do some clean up here to put us
     * back in the correct state.
     *
     * note: this transformation should be safe enough to
     * allow multiple sequential unwrappings.
     */

    {
        HTInputSocket * isocTemp;
        HTHeader * hReqTemp;

        isocTemp = pData->isoc; pData->isoc = NULL;
        hReqTemp = pData->hReq; pData->hReq = NULL;
        (void)HTTP_CleanUp(pData);
        pData->isoc = isocTemp;
        pData->hReq = hReqTemp;
    }

    pData->net_status = 1;
    return STATE_HTTP_GOTSTATUS;
}
#endif /* FEATURE_SUPPORT_UNWRAPPING */

/*********************************************************************
 *
 * x_SkipInterimResponse() -- Skip a 1xx response (eg. "100 Continue").
 *
 * HTTP/1.1 servers may send these before the real response.  We throw
 * away its header, then look for the end of the next header in what
 * we have buffered.  Returns 1 if it is all there, 0 if we started
 * reading the rest from the network, -1 on error.
 *
 */
static int x_SkipInterimResponse(struct Data_LoadHTTP * pData)
{
    static CONST char eoh[4] = { CR, LF, CR, LF };     /* end-of-header */
    struct Params_Isoc_GetHeader * pigh;
    HTInputSocket * isoc;
    HTHeader * h;
    char * p;
    int ndxEOH1 = 0;                /* how much of CR LF CR LF we have seen */
    int ndxEOH2 = 0;                /* how much of LF LF we have seen */

    XX_DMsg(DBG_WWW, ("HTTP: skipping interim response [%s]\n", pData->status_line));

    GTR_FREE(pData->status_line);
    pData->status_line = NULL;

    h = HTHeader_TranslateFromBuffer(pData->isoc);
    if (h)
        HTHeader_Delete(h);

    for (isoc = pData->isoc; ; isoc = isoc->isocNext)
    {
        for (p = isoc->input_pointer; p < isoc->input_limit; p++)
        {
            ndxEOH1 = (*p == eoh[ndxEOH1]) ? ndxEOH1 + 1 : (*p == CR);
            ndxEOH2 = (*p == LF) ? ndxEOH2 + 1 : 0;
            if (ndxEOH1 == 4 || ndxEOH2 == 2)
                return 1;
        }
        if (!isoc->isocNext)
            break;
    }

    /* Read the rest of the header into a new buffer at the end of the chain. */

    isoc->isocNext = HTInputSocket_new(pData->s);
    pigh = GTR_MALLOC(sizeof(*pigh));
    if (!isoc->isocNext || !pigh)
    {
        if (pigh)
            GTR_FREE(pigh);
        return -1;
    }
    pigh->isoc = pData->isoc;
    pigh->isocChain = isoc->isocNext;
    pigh->pStatus = &pData->net_status;
    pigh->ndxEOH1 = ndxEOH1;
    pigh->ndxEOH2 = ndxEOH2;
    Async_DoCall(Isoc_GetHeader_Async, pigh);
    return 0;
}

/*********************************************************************
 *
 * x_DoGotServerStatus() -- Act on the status of a full response.
 *
 */
static int x_DoGotServerStatus(struct Mwin *tw, struct Data_LoadHTTP * pData, int server_status)
{
    HTFormat format_in;             /* Format arriving in the message */

    pData->ht_status = x_FilterServerStatus(tw, server_status,pData);

    switch (pData->ht_status)
    {
    case HT_LOADED:
        *pData->pStatus = HT_LOADED;
        HTTP_CleanUp(pData);
        return STATE_DONE;

    case HT_401:
    case HT_402:
        pData->hRes = HTHeader_TranslateFromBuffer(pData->isoc);
        XX_DMsg(DBG_SPM,("HTLoadHTTP: Received %d.\n",pData->ht_status));
        return x_Process_401_402(tw,pData);

    case HT_OK:                     /* 200 */
#ifdef WIN32    /* experimental.  I've ifdef-ed this Win32 to prevent impact on the other platforms, which are about to ship */
        if (pData->request->method == METHOD_HEAD)
        {
            int len;

            len = pData->isoc->input_limit - pData->isoc->input_pointer;

            pData->request->pHeadData = GTR_CALLOC(len, 1);
            if (pData->request->pHeadData)
            {
                memcpy(pData->request->pHeadData, pData->isoc->input_pointer, len); 
            }
            return STATE_DONE;
        }
#endif

        pData->hlProtocol = NULL;
        pData->hRes = HTHeader_TranslateFromBuffer(pData->isoc);
        pData->htspm = HTSPM_SelectProtocol(pData->ht_status,pData->hRes,&pData->hlProtocol);
        if (pData->htspm)
        {
            /* let spm do any bookkeeping (eg. maintain checkbook balance)
             * and/or see if any unwrapping is necessary.
             */
            HTSPMStatusCode htspm_status;
            
            pData->osd.tw = tw;
            pData->osd.request = pData->request;

            htspm_status = HTSPM_OS_Check200(UI_UserInterface, (void *)&pData->osd,
                                             pData->htspm, pData->hlProtocol,
                                             pData->hReq, pData->hRes);
            if (htspm_status == HTSPM_STATUS_MUST_UNWRAP)
            {
#ifdef FEATURE_SUPPORT_UNWRAPPING
                /* spm would like to unwrap the
                 * response (thus generating a new one).
                 */
                return x_SetUpForUnwrap(tw,pData);
#else
                {
                    char buf[512], szError[1024];

                    strcpy(buf, GTR_GetString(SID_ERR_NO_DEENVELOPING_AVAILABLE_1));
                    strcat(buf, GTR_GetString(SID_ERR_NO_DEENVELOPING_AVAILABLE_2));
                    strcat(buf, GTR_GetString(SID_ERR_NO_DEENVELOPING_AVAILABLE_3));

                    sprintf(szError, buf, pData->htspm->szProtocolName, vv_Application);
                    ERR_ReportError(tw, SID_ERR_SIMPLY_SHOW_ARGUMENTS_S_S, buf, NULL);
                    /* fall thru and display the response as is. */
                }
#endif /* FEATURE_SUPPORT_UNWRAPPING */
            }
        }

        /* process the response as is. */
        
        format_in = x_ExtractMimeInfo(pData);       /* extract information from the mime headers */
#ifdef FEATURE_IAPI
        tw->mimeType = format_in;
#endif
        return x_SetUpForCopying(tw,pData,format_in, pData->request->content_encoding);


    case HT_REDIRECTION_ON_FLY:
        pData->hRes = HTHeader_TranslateFromBuffer(pData->isoc);
        format_in = x_ExtractMimeInfo(pData);       /* extract information from the mime headers */

        *pData->pStatus = HT_REDIRECTION_ON_FLY;    /* we do not want to display the */
        HTTP_CleanUp(pData);                        /* server's message accompanying */
        return STATE_DONE;                          /* redirect.  just go to new doc. */

    default:
        pData->hRes = HTHeader_TranslateFromBuffer(pData->isoc);
        format_in = x_ExtractMimeInfo(pData);       /* extract information from the mime headers */
        return x_SetUpForCopying(tw,pData,format_in, pData->request->content_encoding);
    }
    /*NOTREACHED*/
}


/*********************************************************************
 *
 * Two kinds of site refuse IE 1.5's "Mozilla/1.22" User-Agent.  Only
 * when one does is the request sent once more with another name in its
 * place (the rest stays), and later requests to that host use it
 * straight away, until IE exits:
 *
 *   Cloudflare's browser check answers 403 "error code: 1010": sent
 *   again as "Mozilla/4.0 (compatible; MSIE 1.5; Windows NT)".
 *
 *   Google answers "Update your browser" (200 from "Server: gws", with a
 *   link to support.google.com/websearch/answer/16515119): sent again as
 *   "Nokia6230/2.0 (compatible; MSIE 1.5; Windows NT)".  Google still
 *   sends old phones its plain pages (XHTML, no JavaScript), while every
 *   other browser has to run its JavaScript to search.
 *
 */
#define MAX_ALTUA_HOSTS     16
#define MAX_REFUSAL_READS   4       /* extra reads for the body of a refusal */
#define REFUSAL_BODY        4096    /* how much of the body x_RefusesUA looks at */

#define ALTUA_MOZILLA4      1       /* for Cloudflare */
#define ALTUA_NOKIA         2       /* for Google */

static char * x_aAltUAHosts[MAX_ALTUA_HOSTS];
static int x_aAltUAKinds[MAX_ALTUA_HOSTS];     /* ALTUA_* for each host */
static int x_nAltUAHosts = 0;

static char * x_HostOf(CONST char * url)
{
    char * host;
    char * at;

    host = HTParse(url, "", PARSE_HOST);
    if (host)
    {
        at = strrchr(host, '@');            /* drop any "user:password@" */
        if (at)
            memmove(host, at + 1, strlen(at + 1) + 1);
    }
    return host;
}

/* The ALTUA_* that this URL's host gets, or 0 for the original User-Agent */
static int x_AltUAFor(CONST char * url)
{
    char * host;
    int nAltUA = 0;
    int i;

    if (!x_nAltUAHosts || !url)
        return 0;
    host = x_HostOf(url);
    if (host)
    {
        for (i = 0; i < x_nAltUAHosts && !nAltUA; i++)
            if (0 == GTR_strcmpi(x_aAltUAHosts[i], host))
                nAltUA = x_aAltUAKinds[i];
        GTR_FREE(host);
    }
    return nAltUA;
}

static void x_RememberAltUA(CONST char * url, int nAltUA)
{
    char * host;

    if (x_nAltUAHosts >= MAX_ALTUA_HOSTS || x_AltUAFor(url))
        return;
    host = x_HostOf(url);
    if (host)
    {
        x_aAltUAKinds[x_nAltUAHosts] = nAltUA;
        x_aAltUAHosts[x_nAltUAHosts++] = host;
    }
}

static void x_AltUserAgent(int nAltUA, char * line)
{
    CONST char * rest = strchr(vv_UserAgentString, ' ');

    sprintf(line, "%s%s", (nAltUA == ALTUA_NOKIA) ? "Nokia6230/2.0" : "Mozilla/4.0",
            rest ? rest : "");
}

/*
    Is this reply one of the refusals above?  Looks at the header and the
    start of the body (without chunk sizes) in what has been read so far,
    without using it up.  Returns the ALTUA_* to send again with, 0 if it
    is no refusal, and -1 if the header says Cloudflare or Google but the
    body is not all here yet.
*/
static int x_RefusesUA(struct Data_LoadHTTP * pData, int server_status)
{
    HTInputSocket * isoc;
    char * p;
    char line[64];                  /* start of a header line */
    char body[REFUSAL_BODY];        /* start of the body */
    int nLine = 0;                  /* chars kept in line */
    int nLineLen = 0;               /* length of the whole line */
    int nBody = 0;
    long lLength = -1;              /* Content-Length */
    long lChunk = -1;               /* chunk bytes left, -1 in a chunk-size line */
    long lChunkSize = 0;
    BOOL bChunkExt = FALSE;         /* past the chunk size's hex digits */
    BOOL bChunked = FALSE;
    BOOL bBody = FALSE;
    BOOL bAll = FALSE;              /* seen all of the body we look at */
    int nAltUA = 0;                 /* the server is one of the two */

    for (isoc = pData->isoc; isoc && !bAll; isoc = isoc->isocNext)
    {
        for (p = isoc->input_pointer; p < isoc->input_limit && !bAll; p++)
        {
            if (bBody)
            {
                if (!bChunked || lChunk > 0)
                {
                    body[nBody++] = *p;
                    if (lChunk > 0)
                        lChunk--;
                    bAll = (nBody == (int) sizeof(body) - 1 || nBody == lLength);
                }
                else if (*p == LF)
                {
                    if (lChunk == 0)
                        lChunk = -1;        /* the line end after a chunk */
                    else if (lChunkSize == 0)
                        bAll = TRUE;        /* the last chunk */
                    else
                        lChunk = lChunkSize;
                    lChunkSize = 0;
                    bChunkExt = FALSE;
                }
                else if (lChunk < 0 && !bChunkExt)
                {
                    if (isdigit((unsigned char) *p))
                        lChunkSize = lChunkSize * 16 + (*p - '0');
                    else if (isxdigit((unsigned char) *p))
                        lChunkSize = lChunkSize * 16 + (tolower((unsigned char) *p) - 'a' + 10);
                    else
                        bChunkExt = TRUE;   /* ";name=value" or CR */
                }
            }
            else if (*p == LF)
            {
                line[nLine] = 0;
                if (!nLineLen)
                {
                    bBody = TRUE;   /* the empty line ends the header */
                    bAll = (lLength == 0);
                }
                else if (0 == GTR_strncmpi(line, "server:", 7))
                {
                    for (nLine = 7; line[nLine] == ' ' || line[nLine] == '\t'; nLine++)
                        ;
                    if (server_status == 403 && 0 == GTR_strncmpi(line + nLine, "cloudflare", 10))
                        nAltUA = ALTUA_MOZILLA4;
                    else if (server_status == 200 && 0 == GTR_strcmpi(line + nLine, "gws"))
                        nAltUA = ALTUA_NOKIA;
                }
                else if (0 == GTR_strncmpi(line, "content-length:", 15))
                {
                    lLength = atol(line + 15);
                }
                else if (0 == GTR_strncmpi(line, "transfer-encoding:", 18))
                {
                    bChunked = x_HasToken(line + 18, "chunked");
                }
                nLine = 0;
                nLineLen = 0;
            }
            else if (*p != CR)
            {
                if (nLine < (int) sizeof(line) - 1)
                    line[nLine++] = *p;
                nLineLen++;
            }
        }
    }

    if (!nAltUA)
        return 0;
    body[nBody] = 0;
    if (strstr(body, (nAltUA == ALTUA_NOKIA) ? "/websearch/answer/16515119" : "error code: 1010"))
        return nAltUA;
    return bAll ? 0 : -1;
}

/* Reads the next buffer of the reply onto the end of the chain */
static int x_ReadMoreBody(struct Data_LoadHTTP * pData)
{
    HTInputSocket * isoc;
    struct Params_Isoc_Fill * pif;

    for (isoc = pData->isoc; isoc->isocNext; isoc = isoc->isocNext)
        ;
    isoc->isocNext = HTInputSocket_new(pData->s);
    pif = GTR_MALLOC(sizeof(*pif));
    if (!isoc->isocNext || !pif)
    {
        if (isoc->isocNext)
        {
            HTInputSocket_free(isoc->isocNext);
            isoc->isocNext = NULL;
        }
        if (pif)
            GTR_FREE(pif);
        return -1;
    }
    pData->nRefusalReads++;
    pif->isoc = isoc->isocNext;
    pif->pStatus = &pData->net_status;
    Async_DoCall(Isoc_Fill_Async, pif);
    return STATE_HTTP_GOTMOREBODY;
}

/* Sends the same request again, with the other User-Agent */
static int x_ResendWithAltUA(struct Mwin *tw, struct Data_LoadHTTP * pData,
                             int nAltUA, int server_status)
{
    HTHeader * h = pData->hReq;
    HTHeaderList * hl;
    char line[256];

    x_AltUserAgent(nAltUA, line);
    hl = HTHeaderList_FindFirstHeader(h, "User-Agent");
    if (!hl || !HTHeaderList_SetNameValue(hl, "User-Agent", line))
        return x_DoGotServerStatus(tw, pData, server_status);

    XX_DMsg(DBG_WWW, ("HTTP: User-Agent refused, sending again as [%s]\n", line));
    x_RememberAltUA(pData->request->destination->szActualURL, nAltUA);
    pData->bAltUA = TRUE;

    pData->request->bKeepAlive = FALSE;     /* close this connection */
    pData->hReq = NULL;                     /* keep the request */
    HTTP_CleanUp(pData);
    pData->hReq = h;
    pData->htspm_status = HTSPM_STATUS_RESUBMIT_OLD;   /* just send it, see x_DoInitPart2 */
    return x_DoInitPart2(tw, pData);
}

static int x_CheckRefusal(struct Mwin *tw, struct Data_LoadHTTP * pData, int server_status)
{
    int nAltUA = x_RefusesUA(pData, server_status);
    int next;

    if (nAltUA > 0)
        return x_ResendWithAltUA(tw, pData, nAltUA, server_status);
    if (nAltUA < 0 && pData->net_status > 0 && pData->nRefusalReads < MAX_REFUSAL_READS)
    {
        next = x_ReadMoreBody(pData);
        if (next >= 0)
            return next;
    }
    return x_DoGotServerStatus(tw, pData, server_status);
}

static int HTTP_DoGotMoreBody(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP * pData = *ppInfo;

    return x_CheckRefusal(tw, pData, x_ExtractServerStatus(pData->status_line));
}


static int HTTP_DoGotStatus(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP * pData;
    HTFormat format_in;             /* Format arriving in the message */

    pData = *ppInfo;

    if (   (pData->net_status <= 0)
        && (pData->isoc->input_limit==pData->isoc->input_pointer))
    {
        XX_DMsg(DBG_LOAD, ("HTTP_DoGotStatus: error or no data read from socket!\n"));
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }
    
    pData->status_line = HTInputSocket_getStatusLine(pData->isoc);
    if (!pData->status_line)
    {
        /* old (pre1.0) server */
        
        format_in = x_DealWithVersion0Server(pData->arg, pData->request, pData->isoc);
        return x_SetUpForCopying(tw,pData,format_in, pData->request->content_encoding);
    }
    else
    {
        /* Decode full HTTP response from modern server. */
        
        /*
        ** We now have a terminated server status line, and we have
        ** checked that it is most probably a legal one.  Parse it.
        */

        int server_status = x_ExtractServerStatus(pData->status_line);

        if (server_status >= 100 && server_status < 200)
        {
            switch (x_SkipInterimResponse(pData))
            {
            case 1:                     /* next header is already here */
                return HTTP_DoGotStatus(tw, ppInfo);
            case 0:                     /* reading it */
                return STATE_HTTP_GOTSTATUS;
            default:
                *pData->pStatus = -1;
                HTTP_CleanUp(pData);
                return STATE_DONE;
            }
        }

        if (   (server_status == 403 || server_status == 200)
            && !pData->bAltUA
            && !x_AltUAFor(pData->request->destination->szActualURL))
        {
            return x_CheckRefusal(tw, pData, server_status);
        }
        return x_DoGotServerStatus(tw, pData, server_status);
    }
    /*NOTREACHED*/
}


static int x_DoCopyingCleanup(struct Data_LoadHTTP * pData)
{
    /* We're done! */

    (*pData->target->isa->free)(pData->target);
    pData->target = NULL;

    if (pData->bRedirect)
    {
        *pData->pStatus = HT_REDIRECTION_ON_FLY;
    }
    else
    {
        *pData->pStatus = HT_LOADED;
    }
    HTTP_CleanUp(pData);
    return STATE_DONE;
}

/*********************************************************************
 *
 * x_PutChunked() -- Decode a block of "Transfer-Encoding: chunked"
 *                   data and push the chunk data down the stream.
 *
 * Returns 0 when it wants more data, 1 when the body is complete (or
 * the stream wants no more), -1 on bad chunked data.
 *
 */
static int x_PutChunked(struct Data_LoadHTTP * pData, CONST char * p, int len)
{
    CONST char * end = p + len;
    int c;
    int n;

    while (p < end)
    {
        switch (pData->chunk_state)
        {
        case CHUNK_SIZE:
            c = *p++;
            if (c >= '0' && c <= '9')
                n = c - '0';
            else if (c >= 'a' && c <= 'f')
                n = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F')
                n = c - 'A' + 10;
            else
                n = -1;

            if (n >= 0)
            {
                if (pData->chunk_left < 0)
                    pData->chunk_left = 0;
                if (pData->chunk_left > 0x07FFFFFF)
                    return -1;
                pData->chunk_left = pData->chunk_left * 16 + n;
            }
            else if (pData->chunk_left < 0)
            {
                if (c != CR && c != LF)     /* allow blank lines before a size */
                    return -1;
            }
            else if (c == LF)
                pData->chunk_state = (pData->chunk_left ? CHUNK_DATA : CHUNK_TRAILER);
            else
                pData->chunk_state = CHUNK_EXT;     /* CR, ";extension" or white space */
            break;

        case CHUNK_EXT:
            if (*p++ == LF)
                pData->chunk_state = (pData->chunk_left ? CHUNK_DATA : CHUNK_TRAILER);
            break;

        case CHUNK_DATA:
            n = end - p;
            if (n > pData->chunk_left)
                n = (int) pData->chunk_left;
            if ( ! (*pData->target->isa->put_block)(pData->target, p, n))
                return 1;
            p += n;
            pData->chunk_left -= n;
            if (!pData->chunk_left)
                pData->chunk_state = CHUNK_DATA_END;
            break;

        case CHUNK_DATA_END:
            c = *p++;
            if (c == LF)
            {
                pData->chunk_state = CHUNK_SIZE;
                pData->chunk_left = -1;
            }
            else if (c != CR)
                return -1;
            break;

        case CHUNK_TRAILER:
            c = *p++;
            if (c == LF)
            {
                pData->chunk_state = CHUNK_DONE;
                return 1;
            }
            if (c != CR)
                pData->chunk_state = CHUNK_TRAILER_LINE;
            break;

        case CHUNK_TRAILER_LINE:
            if (*p++ == LF)
                pData->chunk_state = CHUNK_TRAILER;
            break;

        default:                        /* CHUNK_DONE */
            return 1;
        }
    }

    return 0;
}

static int HTTP_DoCopying(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP    *pData;
    struct Params_Isoc_Fill *pif;

    pData = *ppInfo;

    if (pData->net_status < 0)
    {
        /* Error reading */
        (*pData->target->isa->abort)(pData->target, 0);
        pData->target = NULL;
        *pData->pStatus = -1;
        HTTP_CleanUp(pData);
        return STATE_DONE;
    }

    if (pData->net_status == 0)
        return x_DoCopyingCleanup(pData);

    /* we assume that we have at least one data block.
     * loop to drain the chain.
     */
    
    while (1)
    {
        HTInputSocket * isocTemp;
        int amount_obtained;

        if (pData->isoc->input_limit > pData->isoc->input_pointer)
        {
            amount_obtained = pData->isoc->input_limit - pData->isoc->input_pointer;

            XX_DMsg(DBG_WWW|DBG_LOAD,("HTTP_DoCopying: stuffing buffer [size %d]\n",
                                      amount_obtained));
            pData->bytes += amount_obtained;
            if (pData->request->content_length)
                WAIT_SetTherm(tw, pData->bytes);

            if (pData->bChunked)
            {
                int chunk_status = x_PutChunked(pData,
                                                pData->isoc->input_pointer,
                                                amount_obtained);
                if (chunk_status < 0)
                {
                    XX_DMsg(DBG_WWW|DBG_LOAD,("HTTP_DoCopying: bad chunked data\n"));
                    (*pData->target->isa->abort)(pData->target, 0);
                    pData->target = NULL;
                    *pData->pStatus = -1;
                    HTTP_CleanUp(pData);
                    return STATE_DONE;
                }
                if (chunk_status > 0)
                    return x_DoCopyingCleanup(pData);
            }
            else if ( ! (*pData->target->isa->put_block)(pData->target,
                                                         pData->isoc->input_pointer,
                                                         amount_obtained))
                return x_DoCopyingCleanup(pData);
        }
        
        if (!pData->isoc->isocNext)
            break;
        
        isocTemp = pData->isoc->isocNext;
        XX_DMsg(DBG_WWW, ("Killing isoc: 0x%x\n", pData->isoc));
        HTInputSocket_free(pData->isoc);
        pData->isoc = isocTemp;
    }

    /* See if we got it all */
    if (pData->request->content_length && pData->request->content_length <= pData->bytes)
        return x_DoCopyingCleanup(pData);

#ifdef FEATURE_SUPPORT_UNWRAPPING
    if (pData->isoc->input_file_number == 0)        /* if we were unwrapped, we don't have a socket, */
        return x_DoCopyingCleanup(pData);           /* so we don't need to fetch next buffer from net. */
#endif

    /* Get next block of data */

    XX_DMsg(DBG_WWW|DBG_LOAD,("HTTP_DoCopying: fetching next buffer\n"));

    pif = GTR_MALLOC(sizeof(*pif));
    pif->isoc = pData->isoc;
    pif->pStatus = &pData->net_status;
    Async_DoCall(Isoc_Fill_Async, pif);
    return STATE_HTTP_COPYING;
}

/*********************************************************************/
/*********************************************************************/
/*********************************************************************/
/*** End Section to Asynchronously Receive an HTTP Response.       ***/
/*********************************************************************/
/*********************************************************************/
/*********************************************************************/

static int HTTP_Abort(struct Mwin *tw, void **ppInfo)
{
    struct Data_LoadHTTP    *pData;

    pData = *ppInfo;
    *pData->pStatus = -1;
    if (pData->target)
    {
        (*pData->target->isa->abort)(pData->target, HTERROR_CANCELLED);
    }

    if (pData->pCon && (pData->pCon->type == CONN_HTTP))
    {
        TW_DisposeConnection(pData->pCon);
    }
    else
    {
        if (pData->s)
        {
            XX_DMsg(DBG_WWW, ("HTTP: close socket %d.\n", pData->s));
            Net_Close(pData->s);
        }
        if (pData->isoc)
        {
            XX_DMsg(DBG_WWW, ("Killing isoc: 0x%x\n", pData->isoc));
            HTInputSocket_free(pData->isoc);
        }
    }
    if (pData->status_line)
        GTR_FREE(pData->status_line);
    if (pData->complete)
        GTR_FREE(pData->complete);
    if (pData->pszHost)
        GTR_FREE(pData->pszHost);
    pData->request->content_length = 0;

    if (pData->hReq)
        HTHeader_Delete(pData->hReq);
    if (pData->hRes)
        HTHeader_Delete(pData->hRes);

    if (pData->cWaiting)
    {
        WAIT_Pop(tw);
        pData->cWaiting--;
        XX_Assert((pData->cWaiting==0),
                  ("HTTP_Abort: wait stack counter off [%d].",pData->cWaiting));
    }
    
    return STATE_DONE;
}

#ifdef FEATURE_SSL
/*      Load a Document from the HTTP server using SSL
**  If we are in the INIT state, set the secure flag, otherwise
** just act as if it's a normal HTTP retrieve
*/
PRIVATE int HTLoadHTTPS_Async(struct Mwin *tw, int nState, void **ppInfo)
{
  struct Params_LoadAsync *p = *ppInfo;
  if (nState == STATE_INIT)
    p->security = SECURITY_SSL;
  return HTLoadHTTP_Async(tw, nState, ppInfo);
}
#endif

/*      Load Document from HTTP Server 
**      ==============================
**
**   Given a hypertext address, this routine loads a document.
**
**   On entry, *pInfo is of type (struct Params_LoadAsync).
*/
PRIVATE int HTLoadHTTP_Async(struct Mwin *tw, int nState, void **ppInfo)
{
    switch (nState)
    {
        case STATE_INIT:
            return HTTP_DoInit(tw, ppInfo);

        case STATE_HTTP_GOTHOST:
            return HTTP_DoGotHost(tw, ppInfo);

        case STATE_HTTP_CONNECTED:
            return HTTP_DoConnected(tw, ppInfo);

        case STATE_HTTP_SENT:
            return HTTP_DoSent(tw, ppInfo);
        
        case STATE_HTTP_GOTSTATUS:
            return HTTP_DoGotStatus(tw, ppInfo);

        case STATE_HTTP_COPYING:
            return HTTP_DoCopying(tw, ppInfo);

        case STATE_HTTP_GOTMOREBODY:
            return HTTP_DoGotMoreBody(tw, ppInfo);

#ifdef MAC
        case STATE_HTTP_GOT_SEM_FOR_401402:
            return HTTP_DoGotSemFor401402(tw, ppInfo);
#endif

        case STATE_HTTP_RAN401402DIALOG:
            return HTTP_DoRan401402Dialog(tw, ppInfo);

#ifndef UNIX
        case STATE_HTTP_RAN_PPRQ_DIALOG:
            return HTTP_DoRanPPRQDialog(tw, ppInfo);
#endif

#ifdef MAC
        case STATE_HTTP_GOT_SEM_FOR_PPRQ:
            return HTTP_DoGotSemForPPRQ(tw, ppInfo);
#endif
    
#ifdef FEATURE_SUPPORT_UNWRAPPING
        case STATE_HTTP_DIDUNWRAP:
            return HTTP_DidUnwrap(tw, ppInfo);

        case STATE_HTTP_DIDLOADFORUNWRAP:
            return HTTP_DidLoadForUnwrap(tw, ppInfo);
#endif /* FEATURE_SUPPORT_UNWRAPPING */

#ifdef FEATURE_SUPPORT_WRAPPING
        case STATE_HTTP_DIDWRAP:
            return HTTP_DidWrap(tw, ppInfo);
#endif /* FEATURE_SUPPORT_WRAPPING */

        case STATE_ABORT:
            return HTTP_Abort(tw, ppInfo);

        default:
            XX_Assert(0, ("HTLoadHTTP_Async: nState = %d\n", nState));
            return STATE_DONE;
    }
}

/*  Protocol descriptor
 */

GLOBALDEF PUBLIC HTProtocol HTTP = {"http", NULL, HTLoadHTTP_Async};
#ifdef FEATURE_SSL
GLOBALDEF PUBLIC HTProtocol HTTPS = {"https", NULL, HTLoadHTTPS_Async};
#endif

#ifdef SHTTP_ACCESS_TYPE
GLOBALDEF PUBLIC HTProtocol SHTTP = {"shttp", NULL, HTLoadHTTP_Async};
#endif

void HTDisposeConversions(void)
{
    if (HTConversions)
    {
        HTList_delete(HTConversions);
        HTConversions = NULL;
    }
}

void HTTP_DisposeHTTPConnection(struct _CachedConn *pCon)
{
    XX_DMsg(DBG_WWW, ("Killing cached connection: 0x%x\n", pCon));
    XX_Assert((pCon->type == CONN_HTTP), ("HTTP_DisposeHTTPConnection: connection type is %d!", pCon->type));
    XX_Assert((pCon->addr != 0), ("HTTP_DisposeHTTPConnection: connection has no address!"));
    pCon->addr = 0;
    Net_Close(pCon->socket);
    pCon->socket = -1;
    pCon->type = CONN_NONE;
    pCon->port = 0;
    if (pCon->isoc)
    {
        XX_DMsg(DBG_WWW, ("Killing isoc: 0x%x\n", pCon->isoc));
        HTInputSocket_freeChain(pCon->isoc);
        pCon->isoc = NULL;
    }
}
