/*
    https for Windows NT 3.1: TLS 1.2 on connected sockets, with BearSSL
    (sec\bearssl). See ssl.h.

    Only what today's servers need: TLS 1.2, AES-128-GCM, ECDHE on curve
    P-256 or P-384 or RSA key exchange, RSA or ECDSA (P-256 or P-384)
    server keys, SHA-256.

    Certificates are NOT checked: there is no list of trusted authorities.
    The connection is private, but nothing proves the server is the one
    in the URL, so a "man in the middle" can still read it.
*/

#include "all.h"
#include "bearssl.h"

/* Records we send are at most this big (requests are small) */
#define SSL_OBUF_SIZE   4096

/* The certificate "check": takes the server's key from the first
   certificate, so that the key exchange is encrypted to it */
typedef struct
{
    const br_x509_class *vtable;
    br_x509_decoder_context dc;
    int nCerts;
} x509_any_context;

struct ssl_conn
{
    struct ssl_conn *next;
    int socket;
    long lEvents;               /* FD_* to wait for after SSL_WOULDBLOCK */
    BOOL bEOF;                  /* the server closed the connection */
    BOOL bSockErr;              /* send() or recv() failed */
    br_ssl_client_context cc;
    x509_any_context xc;
    unsigned char ibuf[BR_SSL_BUFSIZE_INPUT];
    unsigned char obuf[SSL_OBUF_SIZE];
};

static struct ssl_conn *gConns;

static const uint16_t gSuites[] =
{
    BR_TLS_ECDHE_ECDSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_ECDHE_RSA_WITH_AES_128_GCM_SHA256,
    BR_TLS_RSA_WITH_AES_128_GCM_SHA256
};

/*
    The curves we list: P-256, with BearSSL's fast code for it, and P-384,
    with its generic code. A server whose certificate is on P-384
    (lite.cnn.com) refuses a client that lists only P-256 (alert 40).
    BearSSL lists P-256 first, so servers still pick it for the key exchange.
*/
#define EC_IMPL(curve)  ((curve) == BR_EC_secp256r1 ? &br_ec_p256_m15 : &br_ec_prime_i15)

static const unsigned char *x_ECGenerator(int curve, size_t *len)
{
    return EC_IMPL(curve)->generator(curve, len);
}

static const unsigned char *x_ECOrder(int curve, size_t *len)
{
    return EC_IMPL(curve)->order(curve, len);
}

static size_t x_ECXoff(int curve, size_t *len)
{
    return EC_IMPL(curve)->xoff(curve, len);
}

static uint32_t x_ECMul(unsigned char *G, size_t Glen, const unsigned char *x, size_t xlen, int curve)
{
    return EC_IMPL(curve)->mul(G, Glen, x, xlen, curve);
}

static size_t x_ECMulgen(unsigned char *R, const unsigned char *x, size_t xlen, int curve)
{
    return EC_IMPL(curve)->mulgen(R, x, xlen, curve);
}

static uint32_t x_ECMuladd(unsigned char *A, const unsigned char *B, size_t len,
    const unsigned char *x, size_t xlen, const unsigned char *y, size_t ylen, int curve)
{
    return EC_IMPL(curve)->muladd(A, B, len, x, xlen, y, ylen, curve);
}

static const br_ec_impl x_ec =
{
    ((uint32_t) 1 << BR_EC_secp256r1) | ((uint32_t) 1 << BR_EC_secp384r1),
    x_ECGenerator,
    x_ECOrder,
    x_ECXoff,
    x_ECMul,
    x_ECMulgen,
    x_ECMuladd
};

static void x_XStartChain(const br_x509_class **ctx, const char *server_name)
{
    ((x509_any_context *) ctx)->nCerts = 0;
}

static void x_XStartCert(const br_x509_class **ctx, uint32_t length)
{
    x509_any_context *xc = (x509_any_context *) ctx;

    if (xc->nCerts == 0)
        br_x509_decoder_init(&xc->dc, NULL, NULL);
}

static void x_XAppend(const br_x509_class **ctx, const unsigned char *buf, size_t len)
{
    x509_any_context *xc = (x509_any_context *) ctx;

    if (xc->nCerts == 0)
        br_x509_decoder_push(&xc->dc, buf, len);
}

static void x_XEndCert(const br_x509_class **ctx)
{
    ((x509_any_context *) ctx)->nCerts++;
}

static unsigned x_XEndChain(const br_x509_class **ctx)
{
    x509_any_context *xc = (x509_any_context *) ctx;

    if (xc->nCerts == 0)
        return BR_ERR_X509_EMPTY_CHAIN;
    return br_x509_decoder_last_error(&xc->dc);
}

static const br_x509_pkey *x_XGetPkey(const br_x509_class *const *ctx, unsigned *usages)
{
    x509_any_context *xc = (x509_any_context *) ctx;

    if (usages)
        *usages = BR_KEYTYPE_KEYX | BR_KEYTYPE_SIGN;
    return br_x509_decoder_get_pkey(&xc->dc);
}

static const br_x509_class x509_any_vtable =
{
    sizeof(x509_any_context),
    x_XStartChain,
    x_XStartCert,
    x_XAppend,
    x_XEndCert,
    x_XEndChain,
    x_XGetPkey
};

/*
    NT 3.1 has no system random generator (CryptGenRandom came with NT 4.0),
    so the TLS keys are seeded from what is hard to predict here: the jitter
    of the high-resolution timer, the clock, memory and process state, and
    the mouse. One generator collects it for the whole process; each
    connection mixes in the current timer before taking its seed.
*/
static br_hmac_drbg_context gRng;
static BOOL gbRngSeeded;

static void x_SeedRng(void)
{
    struct
    {
        SYSTEMTIME st;
        MEMORYSTATUS ms;
        LARGE_INTEGER freq;
        DWORD dwPid, dwTid;
        void *pStack;
        DWORD aJitter[128];
    } s;
    LARGE_INTEGER t0, t1;
    int n;

    memset(&s, 0, sizeof(s));
    GetSystemTime(&s.st);
    s.ms.dwLength = sizeof(s.ms);
    GlobalMemoryStatus(&s.ms);
    QueryPerformanceFrequency(&s.freq);
    s.dwPid = GetCurrentProcessId();
    s.dwTid = GetCurrentThreadId();
    s.pStack = &s;
    for (n = 0; n < 128; n++)
    {
        /* How long a Sleep(0) takes varies with interrupts and threads */
        QueryPerformanceCounter(&t0);
        Sleep(0);
        QueryPerformanceCounter(&t1);
        s.aJitter[n] = (t1.LowPart - t0.LowPart) ^ GetTickCount();
    }
    br_hmac_drbg_init(&gRng, &br_sha256_vtable, &s, sizeof(s));
    gbRngSeeded = TRUE;
}

static void x_AddEntropy(void)
{
    struct
    {
        LARGE_INTEGER pc;
        DWORD dwTick;
        LONG lMsgTime;
        DWORD dwMsgPos;
        POINT pt;
    } e;

    memset(&e, 0, sizeof(e));
    QueryPerformanceCounter(&e.pc);
    e.dwTick = GetTickCount();
    e.lMsgTime = GetMessageTime();
    e.dwMsgPos = GetMessagePos();
    GetCursorPos(&e.pt);
    br_hmac_drbg_update(&gRng, &e, sizeof(e));
}

static BOOL x_InitClient(struct ssl_conn *c, const char *pszServer)
{
    br_ssl_engine_context *eng = &c->cc.eng;
    unsigned char seed[32];

    br_ssl_client_zero(&c->cc);
    br_ssl_engine_set_versions(eng, BR_TLS12, BR_TLS12);
    br_ssl_engine_set_suites(eng, gSuites, sizeof(gSuites) / sizeof(gSuites[0]));
    br_ssl_engine_set_hash(eng, br_sha256_ID, &br_sha256_vtable);
    br_ssl_engine_set_prf_sha256(eng, br_tls12_sha256_prf);
    br_ssl_engine_set_ec(eng, &x_ec);
    br_ssl_engine_set_ecdsa(eng, br_ecdsa_i15_vrfy_asn1);
    br_ssl_engine_set_rsavrfy(eng, br_rsa_i15_pkcs1_vrfy);
    br_ssl_client_set_rsapub(&c->cc, br_rsa_i15_public);
    br_ssl_engine_set_aes_ctr(eng, &br_aes_ct_ctr_vtable);
    br_ssl_engine_set_ghash(eng, br_ghash_ctmul32);
    br_ssl_engine_set_gcm(eng, &br_sslrec_in_gcm_vtable, &br_sslrec_out_gcm_vtable);
    c->xc.vtable = &x509_any_vtable;
    br_ssl_engine_set_x509(eng, &c->xc.vtable);
    br_ssl_engine_set_buffers_bidi(eng, c->ibuf, sizeof(c->ibuf), c->obuf, sizeof(c->obuf));
    br_ssl_engine_add_flags(eng, BR_OPT_NO_RENEGOTIATION);

    if (!gbRngSeeded)
        x_SeedRng();
    x_AddEntropy();
    br_hmac_drbg_generate(&gRng, seed, sizeof(seed));
    br_ssl_engine_inject_entropy(eng, seed, sizeof(seed));

    return br_ssl_client_reset(&c->cc, pszServer, 0);
}

static struct ssl_conn *x_Find(int socket)
{
    struct ssl_conn *c;

    for (c = gConns; c; c = c->next)
    {
        if (c->socket == socket)
            return c;
    }
    return NULL;
}

/* Sends the engine's pending records. Returns 1 when it sent some, 0 when
   the socket can't take more yet (wait for FD_WRITE), -1 on error. */
static int x_SendRecords(struct ssl_conn *c)
{
    unsigned char *buf;
    size_t len;
    int n;

    buf = br_ssl_engine_sendrec_buf(&c->cc.eng, &len);
    n = WS_SEND(c->socket, (char *) buf, (int) len, 0);
    if (n > 0)
    {
        br_ssl_engine_sendrec_ack(&c->cc.eng, n);
        return 1;
    }
    if (n < 0 && WS_WSAGETLASTERROR() == WSAEWOULDBLOCK)
    {
        c->lEvents |= FD_WRITE;
        return 0;
    }
    c->bSockErr = TRUE;
    return -1;
}

/* Moves records between the engine and the socket until the engine can
   take application data (want = BR_SSL_SENDAPP) or has some
   (BR_SSL_RECVAPP); the first time, that is the handshake. Returns 1 when
   it can, 0 when it must wait for the socket (c->lEvents says for what),
   -1 when the connection is over or broken. */
static int x_Run(struct ssl_conn *c, unsigned want)
{
    br_ssl_engine_context *eng = &c->cc.eng;
    unsigned state;
    unsigned char *buf;
    size_t len;
    int n;

    for (;;)
    {
        c->lEvents = 0;
        state = br_ssl_engine_current_state(eng);
        if (state & BR_SSL_CLOSED)
            return -1;
        if (state & BR_SSL_SENDREC)
        {
            n = x_SendRecords(c);
            if (n < 0)
                return -1;
            if (n > 0)
                continue;
        }
        if (state & want)
            return 1;
        if (state & BR_SSL_RECVREC)
        {
            buf = br_ssl_engine_recvrec_buf(eng, &len);
            n = WS_RECV(c->socket, (char *) buf, (int) len, 0);
            if (n > 0)
            {
                br_ssl_engine_recvrec_ack(eng, n);
                continue;
            }
            if (n == 0)
            {
                c->bEOF = TRUE;
                return -1;
            }
            if (WS_WSAGETLASTERROR() != WSAEWOULDBLOCK)
            {
                c->bSockErr = TRUE;
                return -1;
            }
            c->lEvents |= FD_READ | FD_CLOSE;
        }
        return c->lEvents ? 0 : -1;
    }
}

/* TRUE if the host is an IP address: those don't go in SNI */
static BOOL x_IsAddress(const char *pszHost)
{
    for (; *pszHost; pszHost++)
    {
        if ((*pszHost < '0' || *pszHost > '9') && *pszHost != '.')
            return FALSE;
    }
    return TRUE;
}

BOOL SSL_Start(int socket, const char *pszHost)
{
    struct ssl_conn *c;
    char szServer[256];
    const char *p;
    int n;

    SSL_Close(socket);      /* left over from a closed socket with this number */

    /* The server name is the host, without "user@" and ":port" */
    p = strrchr(pszHost, '@');
    p = p ? p + 1 : pszHost;
    for (n = 0; p[n] && p[n] != ':' && n < sizeof(szServer) - 1; n++)
        szServer[n] = p[n];
    szServer[n] = 0;

    c = GTR_MALLOC(sizeof(*c));
    if (!c)
        return FALSE;
    memset(c, 0, sizeof(*c));
    c->socket = socket;
    if (!x_InitClient(c, x_IsAddress(szServer) ? NULL : szServer))
    {
        XX_DMsg(DBG_SOCK, ("SSL_Start: error %d\n", br_ssl_engine_last_error(&c->cc.eng)));
        GTR_FREE(c);
        return FALSE;
    }
    c->next = gConns;
    gConns = c;
    return TRUE;
}

BOOL SSL_IsSecure(int socket)
{
    return x_Find(socket) != NULL;
}

/* Takes up to nLen bytes to send, once the handshake is done. Returns how
   many it took, SSL_WOULDBLOCK, or -1 on error. SSL_Flush() sends them. */
int SSL_Send(int socket, const void *pBuf, int nLen)
{
    struct ssl_conn *c;
    unsigned char *p;
    size_t len;
    int n;

    c = x_Find(socket);
    if (!c)
        return -1;
    n = x_Run(c, BR_SSL_SENDAPP);
    if (n <= 0)
        return n == 0 ? SSL_WOULDBLOCK : -1;
    p = br_ssl_engine_sendapp_buf(&c->cc.eng, &len);
    if (len > (size_t) nLen)
        len = nLen;
    memcpy(p, pBuf, len);
    br_ssl_engine_sendapp_ack(&c->cc.eng, len);
    return (int) len;
}

/* Sends what SSL_Send() took. Returns 0 when all of it is sent,
   SSL_WOULDBLOCK, or -1 on error. */
int SSL_Flush(int socket)
{
    struct ssl_conn *c;
    unsigned state;
    int n;

    c = x_Find(socket);
    if (!c)
        return -1;
    br_ssl_engine_flush(&c->cc.eng, 0);
    for (;;)
    {
        state = br_ssl_engine_current_state(&c->cc.eng);
        if (state & BR_SSL_CLOSED)
            return -1;
        if (!(state & BR_SSL_SENDREC))
            return 0;
        c->lEvents = 0;
        n = x_SendRecords(c);
        if (n <= 0)
            return n == 0 ? SSL_WOULDBLOCK : -1;
    }
}

/* Receives up to nLen bytes. Returns how many, 0 at the end of the data,
   SSL_WOULDBLOCK, or -1 on error. */
int SSL_Recv(int socket, void *pBuf, int nLen)
{
    struct ssl_conn *c;
    unsigned char *p;
    size_t len;
    int n;

    c = x_Find(socket);
    if (!c)
        return -1;
    n = x_Run(c, BR_SSL_RECVAPP);
    if (n == 0)
        return SSL_WOULDBLOCK;
    if (n < 0)
    {
        /* The end, with or without TLS's close_notify: many servers
           just close the connection */
        if (c->bEOF || (!c->bSockErr && br_ssl_engine_last_error(&c->cc.eng) == BR_ERR_OK))
            return 0;
        return -1;
    }
    p = br_ssl_engine_recvapp_buf(&c->cc.eng, &len);
    if (len > (size_t) nLen)
        len = nLen;
    memcpy(pBuf, p, len);
    br_ssl_engine_recvapp_ack(&c->cc.eng, len);
    return (int) len;
}

long SSL_Events(int socket)
{
    struct ssl_conn *c;

    c = x_Find(socket);
    return c ? c->lEvents : 0;
}

const char *SSL_ErrorText(int socket)
{
    static char szError[64];
    struct ssl_conn *c;
    int err;

    c = x_Find(socket);
    if (!c)
        return "";
    if (c->bSockErr)
        return "network error";
    if (c->bEOF)
        return "the server closed the connection";
    err = br_ssl_engine_last_error(&c->cc.eng);
    if (err >= BR_ERR_RECV_FATAL_ALERT && err < BR_ERR_SEND_FATAL_ALERT)
        sprintf(szError, "the server sent TLS alert %d", err - BR_ERR_RECV_FATAL_ALERT);
    else
        sprintf(szError, "TLS error %d", err);
    return szError;
}

void SSL_Close(int socket)
{
    struct ssl_conn **pp, *c;

    for (pp = &gConns; (c = *pp) != NULL; pp = &c->next)
    {
        if (c->socket == socket)
        {
            *pp = c->next;
            GTR_FREE(c);
            return;
        }
    }
}
