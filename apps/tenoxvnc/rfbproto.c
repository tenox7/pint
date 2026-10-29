/*
 *  Copyright (C) 2000-2006 Constantin Kaplinsky.  All Rights Reserved.
 *  Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
 *  Copyright (C) 1999 AT&T Laboratories Cambridge.  All Rights Reserved.
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307,
 *  USA.
 */

/*
 * rfbproto.c - functions to deal with client side of RFB protocol.
 */

#include <unistd.h>
#include <errno.h>
#include <pwd.h>
#include <vncviewer.h>
#include <vncauth.h>
#include <zlib.h>
#include <jpeg.h>

static void InitCapabilities(void);
static Bool SetupTunneling(void);
static int ReadSecurityType(void);
static int SelectSecurityType(void);
static Bool PerformAuthenticationTight(void);
static Bool AuthenticateVNC(void);
static Bool AuthenticateNone(void);
static Bool ReadAuthenticationResult(void);
static Bool ReadInteractionCaps(void);
static Bool ReadCapabilityList(CapsContainer *caps, int count);

static Bool HandleRRE8(int rx, int ry, int rw, int rh);
static Bool HandleRRE16(int rx, int ry, int rw, int rh);
static Bool HandleRRE32(int rx, int ry, int rw, int rh);
static Bool HandleCoRRE8(int rx, int ry, int rw, int rh);
static Bool HandleCoRRE16(int rx, int ry, int rw, int rh);
static Bool HandleCoRRE32(int rx, int ry, int rw, int rh);
static Bool HandleHextile8(int rx, int ry, int rw, int rh);
static Bool HandleHextile16(int rx, int ry, int rw, int rh);
static Bool HandleHextile32(int rx, int ry, int rw, int rh);
static Bool HandleZlib8(int rx, int ry, int rw, int rh);
static Bool HandleZlib16(int rx, int ry, int rw, int rh);
static Bool HandleZlib32(int rx, int ry, int rw, int rh);
static Bool HandleTight8(int rx, int ry, int rw, int rh);
static Bool HandleTight16(int rx, int ry, int rw, int rh);
static Bool HandleTight32(int rx, int ry, int rw, int rh);
static Bool HandleZRLE(int rx, int ry, int rw, int rh);
static void ResetZRLEState(void);
static void ResetDecoders(void);

static void ReadConnFailedReason(void);
static long ReadCompactLen (void);

static Bool HandleExtendedDesktopSize(int reason, int result, int w, int h);
static Bool HandleDesktopName(void);
static void FramebufferSizeChanged(int width, int height);
static void ReportRectEncoding(CARD32 enc);

static void JpegInitSource(j_decompress_ptr cinfo);
static boolean JpegFillInputBuffer(j_decompress_ptr cinfo);
static void JpegSkipInputData(j_decompress_ptr cinfo, long num_bytes);
static void JpegTermSource(j_decompress_ptr cinfo);
static void JpegSetSrcManager(j_decompress_ptr cinfo, CARD8 *compressedData,
                              int compressedLen);


int rfbsock;
char *desktopName;
rfbPixelFormat myFormat;
rfbServerInitMsg si;
char *serverCutText = NULL;
Bool newServerCutText = False;

/* TigerVNC extensions state */
Bool supportsSetDesktopSize = False;
Bool pendingDesktopResize = False;
Bool supportsFence = False;
Bool supportsCU = False;
Bool cuActive = False;
static Bool firstUpdate = True;
static CARD32 screenId = 0;
static CARD32 screenFlags = 0;

/* reporting of what the server actually sends */
static CARD32 reportedEncs[16];
static int nReportedEncs = 0;
static Bool tightJpegReported = False;

int endianTest = 1;

/* exported for the title bar info */
int protocolMinorVersion;
Bool tightVncProtocol = False;

/* Set when the server rejects our password, so that main() knows to offer
   the connection dialog again rather than just giving up. */
Bool authFailed = False;
char titleEncName[16] = "";

/* The password the current server accepted, kept so that a reconnect from
   the F8 settings panel does not have to ask for it again. */
static char sessionPasswd[9];
static Bool sessionPasswdSet = False;

static CapsContainer *tunnelCaps;    /* known tunneling/encryption methods */
static CapsContainer *authCaps;	     /* known authentication schemes       */
static CapsContainer *serverMsgCaps; /* known non-standard server messages */
static CapsContainer *clientMsgCaps; /* known non-standard client messages */
static CapsContainer *encodingCaps;  /* known encodings besides Raw        */


/* Note that the CoRRE encoding uses this buffer and assumes it is big enough
   to hold 255 * 255 * 32 bits -> 260100 bytes.  640*480 = 307200 bytes.
   Hextile also assumes it is big enough to hold 16 * 16 * 32 bits.
   Tight encoding assumes BUFFER_SIZE is at least 16384 bytes. */

#define BUFFER_SIZE RFB_BUFFER_SIZE
static char buffer[BUFFER_SIZE];


/* The zlib encoding requires expansion/decompression/deflation of the
   compressed data in the "buffer" above into another, result buffer.
   However, the size of the result buffer can be determined precisely
   based on the bitsPerPixel, height and width of the rectangle.  We
   allocate this buffer one time to be the full size of the buffer. */

static int raw_buffer_size = -1;
static char *raw_buffer;

static z_stream decompStream;
static Bool decompStreamInited = False;


/*
 * Variables for the ``tight'' encoding implementation.
 */

/* Four independent compression streams for zlib library. */
static z_stream zlibStream[4];
static Bool zlibStreamActive[4] = {
  False, False, False, False
};

/* Filter stuff. Should be initialized by filter initialization code. */
static Bool cutZeros;
static int rectWidth, rectColors;
static char tightPalette[256*4];
static CARD8 tightPrevRow[2048*3*sizeof(CARD16)];

/* JPEG decoder state. */
static Bool jpegError;


/*
 * InitCapabilities.
 */

static void
InitCapabilities(void)
{
  tunnelCaps    = CapsNewContainer();
  authCaps      = CapsNewContainer();
  serverMsgCaps = CapsNewContainer();
  clientMsgCaps = CapsNewContainer();
  encodingCaps  = CapsNewContainer();

  /* Supported authentication methods */
  CapsAdd(authCaps, rfbAuthNone, rfbStandardVendor, sig_rfbAuthNone,
	  "No authentication");
  CapsAdd(authCaps, rfbAuthVNC, rfbStandardVendor, sig_rfbAuthVNC,
	  "Standard VNC password authentication");

  /* Supported encoding types */
  CapsAdd(encodingCaps, rfbEncodingCopyRect, rfbStandardVendor,
	  sig_rfbEncodingCopyRect, "Standard CopyRect encoding");
  CapsAdd(encodingCaps, rfbEncodingRRE, rfbStandardVendor,
	  sig_rfbEncodingRRE, "Standard RRE encoding");
  CapsAdd(encodingCaps, rfbEncodingCoRRE, rfbStandardVendor,
	  sig_rfbEncodingCoRRE, "Standard CoRRE encoding");
  CapsAdd(encodingCaps, rfbEncodingHextile, rfbStandardVendor,
	  sig_rfbEncodingHextile, "Standard Hextile encoding");
  CapsAdd(encodingCaps, rfbEncodingZlib, rfbTridiaVncVendor,
	  sig_rfbEncodingZlib, "Zlib encoding from TridiaVNC");
  CapsAdd(encodingCaps, rfbEncodingTight, rfbTightVncVendor,
	  sig_rfbEncodingTight, "Tight encoding by Constantin Kaplinsky");
  CapsAdd(encodingCaps, rfbEncodingZRLE, rfbStandardVendor,
	  sig_rfbEncodingZRLE, "Standard ZRLE encoding");

  /* Supported "fake" encoding types */
  CapsAdd(encodingCaps, rfbEncodingCompressLevel0, rfbTightVncVendor,
	  sig_rfbEncodingCompressLevel0, "Compression level");
  CapsAdd(encodingCaps, rfbEncodingQualityLevel0, rfbTightVncVendor,
	  sig_rfbEncodingQualityLevel0, "JPEG quality level");
  CapsAdd(encodingCaps, rfbEncodingXCursor, rfbTightVncVendor,
	  sig_rfbEncodingXCursor, "X-style cursor shape update");
  CapsAdd(encodingCaps, rfbEncodingRichCursor, rfbTightVncVendor,
	  sig_rfbEncodingRichCursor, "Rich-color cursor shape update");
  CapsAdd(encodingCaps, rfbEncodingPointerPos, rfbTightVncVendor,
	  sig_rfbEncodingPointerPos, "Pointer position update");
  CapsAdd(encodingCaps, rfbEncodingLastRect, rfbTightVncVendor,
	  sig_rfbEncodingLastRect, "LastRect protocol extension");
}


/*
 * ResetDecoders drops every inflate stream, so that a new connection starts
 * them again from the server's first compressed rectangle.  A zlib stream
 * runs the length of a connection and carries state across rectangles, so
 * one left over from the last server would decode the next one to rubbish.
 */

static void
ResetDecoders(void)
{
  int i;

  if (decompStreamInited) {
    inflateEnd(&decompStream);
    decompStreamInited = False;
  }

  for (i = 0; i < 4; i++) {
    if (zlibStreamActive[i]) {
      inflateEnd(&zlibStream[i]);
      zlibStreamActive[i] = False;
    }
  }

  ResetZRLEState();
}


/*
 * ConnectToRFBServer.
 */

Bool
ConnectToRFBServer(const char *hostname, int port)
{
  unsigned int host;

  if (!StringToIPAddr(hostname, &host)) {
    ConnError("Unknown VNC server host \"%.60s\"", hostname);
    return False;
  }

  rfbsock = ConnectToTcpAddr(host, port);

  if (rfbsock < 0) {
    ConnError("Unable to connect to %.60s:%d: %s", hostname, port,
	      strerror(errno));
    return False;
  }

  if (!SetNonBlocking(rfbsock)) {
    close(rfbsock);
    rfbsock = -1;
    return False;
  }

  return True;
}


/*
 * SanitizeDesktopName - servers may send the desktop name as UTF-8, in some
 * other 8-bit charset, or with embedded control characters.  The window
 * title and our stderr messages can only carry plain ASCII, so filter the
 * name in place, collapsing each run of anything outside printable 7-bit
 * ASCII into a single '?'.  Leaves an empty string if nothing survives, in
 * which case the title falls back to the default.
 */

static void
SanitizeDesktopName(char *name)
{
  char *out = name;
  Bool prevBad = False;

  for (; *name; name++) {
    if ((unsigned char)*name >= 0x20 && (unsigned char)*name < 0x7F) {
      *out++ = *name;
      prevBad = False;
      continue;
    }
    if (!prevBad)
      *out++ = '?';
    prevBad = True;
  }
  *out = '\0';
}


/*
 * InitialiseRFBConnection.
 */

Bool
InitialiseRFBConnection(void)
{
  rfbProtocolVersionMsg pv;
  int server_major, server_minor;
  rfbClientInitMsg ci;
  int secType;

  StatsInit();

  /* A retry after a failed attempt starts from scratch: anything the last
     server left buffered, or told us about itself, is not ours to reuse. */
  ResetReadBuffer();
  tightVncProtocol = False;
  if (desktopName) {
    free(desktopName);
    desktopName = NULL;
  }

  /* reset TigerVNC extensions state for this session */
  supportsSetDesktopSize = False;
  pendingDesktopResize = False;
  supportsFence = False;
  supportsCU = False;
  cuActive = False;
  firstUpdate = True;
  screenId = 0;
  screenFlags = 0;
  nReportedEncs = 0;
  tightJpegReported = False;
  titleEncName[0] = '\0';
  ResetDecoders();

  /* if the connection is immediately closed, don't report anything, so
       that pmw's monitor can make test connections */

  if (listenSpecified)
    errorMessageOnReadFailure = False;

  if (!ReadFromRFBServer(pv, sz_rfbProtocolVersionMsg))
    return False;

  errorMessageOnReadFailure = True;

  pv[sz_rfbProtocolVersionMsg] = 0;

  if (sscanf(pv, rfbProtocolVersionFormat,
	     &server_major, &server_minor) != 2) {
    ConnError("Not a VNC server");
    return False;
  }

  if (server_major == 3 && server_minor >= 8) {
    /* the server supports protocol 3.8 or higher version */
    protocolMinorVersion = 8;
  } else if (server_major == 3 && server_minor == 7) {
    /* the server supports protocol 3.7 */
    protocolMinorVersion = 7;
  } else {
    /* any other server version, request the standard 3.3 */
    protocolMinorVersion = 3;
  }

  fprintf(stderr, "Connected to RFB server, using protocol version 3.%d\n",
	  protocolMinorVersion);

#ifdef VNCSTATS
  {
    char note[48];

    sprintf(note, "connected, protocol 3.%d", protocolMinorVersion);
    StatsLog(-1, note, 0.0, 0.0);
  }
#endif

  sprintf(pv, rfbProtocolVersionFormat, 3, protocolMinorVersion);

  if (!WriteExact(rfbsock, pv, sz_rfbProtocolVersionMsg))
    return False;

  /* Read or select the security type. */
  if (protocolMinorVersion >= 7) {
    secType = SelectSecurityType();
  } else {
    secType = ReadSecurityType();
  }
  if (secType == rfbSecTypeInvalid)
    return False;

  switch (secType) {
  case rfbSecTypeNone:
    if (!AuthenticateNone())
      return False;
    break;
  case rfbSecTypeVncAuth:
    if (!AuthenticateVNC())
      return False;
    break;
  case rfbSecTypeTight:
    tightVncProtocol = True;
    InitCapabilities();
    if (!SetupTunneling())
      return False;
    if (!PerformAuthenticationTight())
      return False;
    break;
  default:                      /* should never happen */
    ConnError("Internal error: invalid security type");
    return False;
  }

  ci.shared = (appData.shareDesktop ? 1 : 0);

  if (!WriteExact(rfbsock, (char *)&ci, sz_rfbClientInitMsg))
    return False;

  if (!ReadFromRFBServer((char *)&si, sz_rfbServerInitMsg))
    return False;

  si.framebufferWidth = Swap16IfLE(si.framebufferWidth);
  si.framebufferHeight = Swap16IfLE(si.framebufferHeight);
  si.format.redMax = Swap16IfLE(si.format.redMax);
  si.format.greenMax = Swap16IfLE(si.format.greenMax);
  si.format.blueMax = Swap16IfLE(si.format.blueMax);
  si.nameLength = Swap32IfLE(si.nameLength);

  if (!RfbValidServerStringLength(si.nameLength, 1)) {
    ConnError("Desktop name length too large (%lu bytes)",
	      (unsigned long)si.nameLength);
    return False;
  }

  desktopName = malloc(si.nameLength + 1);
  if (!desktopName) {
    ConnError("Error allocating memory for desktop name, %lu bytes",
	      (unsigned long)si.nameLength);
    return False;
  }

  if (!ReadFromRFBServer(desktopName, si.nameLength)) return False;

  desktopName[si.nameLength] = 0;
  SanitizeDesktopName(desktopName);

  fprintf(stderr,"Desktop name \"%s\"\n",desktopName);

  fprintf(stderr,"VNC server default format:\n");
  PrintPixelFormat(&si.format);

  if (tightVncProtocol) {
    /* Read interaction capabilities (protocol 3.7t, 3.8t) */
    if (!ReadInteractionCaps())
      return False;
  }

  return True;
}


/*
 * Read security type from the server (protocol 3.3)
 */

static int
ReadSecurityType(void)
{
  CARD32 secType;

  /* Read the security type */
  if (!ReadFromRFBServer((char *)&secType, sizeof(secType)))
    return rfbSecTypeInvalid;

  secType = Swap32IfLE(secType);

  if (secType == rfbSecTypeInvalid) {
    ReadConnFailedReason();
    return rfbSecTypeInvalid;
  }

  if (secType != rfbSecTypeNone && secType != rfbSecTypeVncAuth) {
    ConnError("Unknown security type from the VNC server: %d", (int)secType);
    return rfbSecTypeInvalid;
  }

  return (int)secType;
}


/*
 * Select security type from the server's list (protocol 3.7 and above)
 */

static int
SelectSecurityType(void)
{
  CARD8 nSecTypes;
  CARD8 knownSecTypes[] = {rfbSecTypeNone, rfbSecTypeVncAuth};
  int nKnownSecTypes = sizeof(knownSecTypes);
  CARD8 *secTypes;
  CARD8 secType = rfbSecTypeInvalid;
  int i, j;

  /* Read the list of secutiry types. */
  if (!ReadFromRFBServer((char *)&nSecTypes, sizeof(nSecTypes)))
    return rfbSecTypeInvalid;

  if (nSecTypes == 0) {
    ReadConnFailedReason();
    return rfbSecTypeInvalid;
  }

  secTypes = malloc(nSecTypes);
  if (!ReadFromRFBServer((char *)secTypes, nSecTypes))
    return rfbSecTypeInvalid;

  /* Find out if the server supports TightVNC protocol extensions */
  for (j = 0; j < (int)nSecTypes; j++) {
    if (secTypes[j] == rfbSecTypeTight) {
      free(secTypes);
      secType = rfbSecTypeTight;
      if (!WriteExact(rfbsock, (char *)&secType, sizeof(secType)))
        return rfbSecTypeInvalid;
      fprintf(stderr, "Enabling tight protocol extensions\n");
      return rfbSecTypeTight;
    }
  }

  /* Find first supported security type */
  for (j = 0; j < (int)nSecTypes; j++) {
    for (i = 0; i < nKnownSecTypes; i++) {
      if (secTypes[j] == knownSecTypes[i]) {
        secType = secTypes[j];
        if (!WriteExact(rfbsock, (char *)&secType, sizeof(secType))) {
          free(secTypes);
          return rfbSecTypeInvalid;
        }
        break;
      }
    }
    if (secType != rfbSecTypeInvalid) break;
  }

  free(secTypes);

  if (secType == rfbSecTypeInvalid)
    ConnError("Server did not offer a security type we support");

  return (int)secType;
}


/*
 * Setup tunneling (protocol 3.7t, 3.8t).
 */

static Bool
SetupTunneling(void)
{
  rfbTunnelingCapsMsg caps;
  CARD32 tunnelType;

  /* In protocols 3.7t/3.8t, the server informs us about
     supported tunneling methods. Here we read this information. */

  if (!ReadFromRFBServer((char *)&caps, sz_rfbTunnelingCapsMsg))
    return False;

  caps.nTunnelTypes = Swap32IfLE(caps.nTunnelTypes);

  if (caps.nTunnelTypes) {
    if (!ReadCapabilityList(tunnelCaps, caps.nTunnelTypes))
      return False;

    /* We cannot do tunneling anyway yet. */
    tunnelType = Swap32IfLE(rfbNoTunneling);
    if (!WriteExact(rfbsock, (char *)&tunnelType, sizeof(tunnelType)))
      return False;
  }

  return True;
}


/*
 * Negotiate authentication scheme (protocol 3.7t, 3.8t)
 */

static Bool
PerformAuthenticationTight(void)
{
  rfbAuthenticationCapsMsg caps;
  CARD32 authScheme;
  int i;

  /* In protocols 3.7t/3.8t, the server informs us about supported
     authentication schemes. Here we read this information. */

  if (!ReadFromRFBServer((char *)&caps, sz_rfbAuthenticationCapsMsg))
    return False;

  caps.nAuthTypes = Swap32IfLE(caps.nAuthTypes);

  /* Special case - empty capability list stands for no authentication. */
  if (!caps.nAuthTypes)
    return AuthenticateNone();

  if (!ReadCapabilityList(authCaps, caps.nAuthTypes))
    return False;

  /* Try server's preferred authentication scheme. */
  for (i = 0; i < CapsNumEnabled(authCaps); i++) {
    authScheme = CapsGetByOrder(authCaps, i);
    if (authScheme != rfbAuthVNC && authScheme != rfbAuthNone)
      continue;                 /* unknown scheme - cannot use it */
    authScheme = Swap32IfLE(authScheme);
    if (!WriteExact(rfbsock, (char *)&authScheme, sizeof(authScheme)))
      return False;
    authScheme = Swap32IfLE(authScheme); /* convert it back */

    switch (authScheme) {
    case rfbAuthNone:
      return AuthenticateNone();
    case rfbAuthVNC:
      return AuthenticateVNC();
    default:                      /* should never happen */
      ConnError("Internal error: invalid authentication type");
      return False;
    }
  }

  ConnError("Server did not offer an authentication scheme we support");
  return False;
}


/*
 * Null authentication.
 */

static Bool
AuthenticateNone(void)
{
  fprintf(stderr, "No authentication needed\n");

  if (protocolMinorVersion >= 8) {
    if (!ReadAuthenticationResult())
      return False;
  }

  return True;
}


/*
 * Standard VNC authentication.
 */

static Bool
AuthenticateVNC(void)
{
  CARD8 challenge[CHALLENGESIZE];
  char *passwd;
  char *envpw;
  char  buffer[64];
  char  pw[9];
  char* cstatus;
  int   len;
  Bool  ok;

  fprintf(stderr, "Performing standard VNC authentication\n");

  if (!ReadFromRFBServer((char *)challenge, CHALLENGESIZE))
    return False;

  /* A reconnect from the F8 settings panel authenticates again, and there is
     nowhere to ask a second time from: a password file is the only source
     that could be read twice.  So the one that worked is kept for the life of
     the viewer, and dropped the moment a server refuses it. */

  if (sessionPasswdSet) {
    memcpy(pw, sessionPasswd, sizeof(pw));
    passwd = NULL;
  } else if (appData.passwordFile) {
    passwd = vncDecryptPasswdFromFile(appData.passwordFile);
    if (!passwd) {
      ConnError("Cannot read a valid password from file \"%.50s\"",
		appData.passwordFile);
      return False;
    }
  } else if (appData.autoPass) {
    passwd = buffer;
    cstatus = fgets(buffer, sizeof buffer, stdin);
    if (cstatus == NULL)
       buffer[0] = '\0';
    else
    {
       len = strlen(buffer);
       if (len > 0 && buffer[len - 1] == '\n')
	  buffer[len - 1] = '\0';
    }
  } else if ((envpw = getenv("VNC_PASSWORD")) != NULL && envpw[0] != '\0') {
    passwd = buffer;
    strncpy(buffer, envpw, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
  } else if (appData.passwordDialog) {
    passwd = DoPasswordDialog();
  } else {
#ifdef __VMS
    passwd = VmsPrompt("Password: ", False);	/* no getpass() in the CRTL */
#else
    passwd = getpass("Password: ");
#endif
  }

  if (!sessionPasswdSet) {
    if (!passwd || strlen(passwd) == 0) {
      ConnError("Reading the password failed");
      return False;
    }

    strncpy(pw, passwd, 8);
    pw[8] = '\0';

    /* Lose the password from wherever it was typed or read */
    memset(passwd, '\0', strlen(passwd));
    if (appData.passwordFile)
      free(passwd);
  }

  vncEncryptBytes(challenge, pw);

  if (!WriteExact(rfbsock, (char *)challenge, CHALLENGESIZE)) {
    memset(pw, '\0', sizeof(pw));
    return False;
  }

  ok = ReadAuthenticationResult();

  if (ok) {
    memcpy(sessionPasswd, pw, sizeof(sessionPasswd));
    sessionPasswdSet = True;
  } else {
    ForgetSessionPassword();
  }

  memset(pw, '\0', sizeof(pw));

  return ok;
}


/*
 * ForgetSessionPassword drops the retained password, so that the next
 * connection asks for one again.  Called when a server refuses it, and from
 * the connection dialog when it is about to ask for a new one.
 */

void
ForgetSessionPassword(void)
{
  memset(sessionPasswd, '\0', sizeof(sessionPasswd));
  sessionPasswdSet = False;
}

/*
 * Read and report the result of authentication.
 */

static Bool
ReadAuthenticationResult(void)
{
  CARD32 authResult;

  if (!ReadFromRFBServer((char *)&authResult, 4))
    return False;

  authResult = Swap32IfLE(authResult);

  switch (authResult) {
  case rfbAuthOK:
    fprintf(stderr, "Authentication successful\n");
    break;
  case rfbAuthFailed:
    if (protocolMinorVersion >= 8) {
      ReadConnFailedReason();
    } else {
      ConnError("Authentication failure");
    }
    authFailed = True;
    return False;
  case rfbAuthTooMany:
    ConnError("Authentication failure, too many tries");
    authFailed = True;
    return False;
  default:
    ConnError("Unknown result of authentication (%d)", (int)authResult);
    return False;
  }

  return True;
}

/*
 * In protocols 3.7t/3.8t, the server informs us about supported
 * protocol messages and encodings. Here we read this information.
 */

static Bool
ReadInteractionCaps(void)
{
  rfbInteractionCapsMsg intr_caps;

  /* Read the counts of list items following */
  if (!ReadFromRFBServer((char *)&intr_caps, sz_rfbInteractionCapsMsg))
    return False;
  intr_caps.nServerMessageTypes = Swap16IfLE(intr_caps.nServerMessageTypes);
  intr_caps.nClientMessageTypes = Swap16IfLE(intr_caps.nClientMessageTypes);
  intr_caps.nEncodingTypes = Swap16IfLE(intr_caps.nEncodingTypes);

  /* Read the lists of server- and client-initiated messages */
  return (ReadCapabilityList(serverMsgCaps, intr_caps.nServerMessageTypes) &&
	  ReadCapabilityList(clientMsgCaps, intr_caps.nClientMessageTypes) &&
	  ReadCapabilityList(encodingCaps, intr_caps.nEncodingTypes));
}


/*
 * Read the list of rfbCapabilityInfo structures and enable corresponding
 * capabilities in the specified container. The count argument specifies how
 * many records to read from the socket.
 */

static Bool
ReadCapabilityList(CapsContainer *caps, int count)
{
  rfbCapabilityInfo msginfo;
  int i;

  for (i = 0; i < count; i++) {
    if (!ReadFromRFBServer((char *)&msginfo, sz_rfbCapabilityInfo))
      return False;
    msginfo.code = Swap32IfLE(msginfo.code);
    CapsEnable(caps, &msginfo);
  }

  return True;
}


/*
 * SetFormatAndEncodings, and the two halves it is made of.
 *
 * They are separate because the encoding list may be re-sent at any point in
 * the stream - the server simply uses the new one for the next rectangle it
 * encodes - while the pixel format may not, since it decides how many bytes
 * each rectangle occupies.  So the F8 settings panel sends encodings on their
 * own, and a format change waits for the clean stream boundary a reconnect
 * gives it.
 */

Bool
SetFormatAndEncodings()
{
  return SendPixelFormat() && SendEncodings();
}

Bool
SendPixelFormat()
{
  rfbSetPixelFormatMsg spf;

  spf.type = rfbSetPixelFormat;
  spf.format = myFormat;
  spf.format.redMax = Swap16IfLE(spf.format.redMax);
  spf.format.greenMax = Swap16IfLE(spf.format.greenMax);
  spf.format.blueMax = Swap16IfLE(spf.format.blueMax);

  if (!WriteExact(rfbsock, (char *)&spf, sz_rfbSetPixelFormatMsg))
    return False;

  STATS(vncStats.msgsOut++);
  StatsLog(1, "SetPixelFormat", sz_rfbSetPixelFormatMsg, 0.0);

  return True;
}

Bool
SendEncodings()
{
  char buf[sz_rfbSetEncodingsMsg + MAX_ENCODINGS * 4];
  rfbSetEncodingsMsg *se = (rfbSetEncodingsMsg *)buf;
  CARD32 *encs = (CARD32 *)(&buf[sz_rfbSetEncodingsMsg]);
  int len = 0;
  Bool requestCompressLevel = False;
  Bool requestQualityLevel = False;
  Bool requestLastRectEncoding = False;

  /* Say again which encoding the server settles on.  Each one is announced
     only once per connection, so without this a list sent from the settings
     panel would change what arrives with nothing said about it. */
  nReportedEncs = 0;

  se->type = rfbSetEncodings;
  se->nEncodings = 0;

  if (appData.encodingsString) {
    char *encStr = appData.encodingsString;
    int encStrLen;
    do {
      char *nextEncStr = strchr(encStr, ' ');
      if (nextEncStr) {
	encStrLen = nextEncStr - encStr;
	nextEncStr++;
      } else {
	encStrLen = strlen(encStr);
      }

      if (strncasecmp(encStr,"raw",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRaw);
      } else if (strncasecmp(encStr,"copyrect",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCopyRect);
      } else if (strncasecmp(encStr,"tight",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingTight);
	requestLastRectEncoding = True;
	if (appData.compressLevel >= 0 && appData.compressLevel <= 9)
	  requestCompressLevel = True;
	if (appData.enableJPEG)
	  requestQualityLevel = True;
      } else if (strncasecmp(encStr,"hextile",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingHextile);
      } else if (strncasecmp(encStr,"zrle",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingZRLE);
	if (appData.compressLevel >= 0 && appData.compressLevel <= 9)
	  requestCompressLevel = True;
      } else if (strncasecmp(encStr,"zlib",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingZlib);
	if (appData.compressLevel >= 0 && appData.compressLevel <= 9)
	  requestCompressLevel = True;
      } else if (strncasecmp(encStr,"corre",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCoRRE);
      } else if (strncasecmp(encStr,"rre",encStrLen) == 0) {
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRRE);
      } else {
	fprintf(stderr,"Unknown encoding '%.*s'\n",encStrLen,encStr);
      }

      encStr = nextEncStr;
    } while (encStr && se->nEncodings < MAX_ENCODINGS);

    if (se->nEncodings < MAX_ENCODINGS && requestCompressLevel) {
      encs[se->nEncodings++] = Swap32IfLE(appData.compressLevel +
					  rfbEncodingCompressLevel0);
    }

    if (se->nEncodings < MAX_ENCODINGS && requestQualityLevel) {
      if (appData.qualityLevel < 0 || appData.qualityLevel > 9)
        appData.qualityLevel = 5;
      encs[se->nEncodings++] = Swap32IfLE(appData.qualityLevel +
					  rfbEncodingQualityLevel0);
    }

    if (se->nEncodings < MAX_ENCODINGS && requestQualityLevel && appData.forceJPEG)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingSubsampGray);

    if (appData.useRemoteCursor) {
      if (se->nEncodings < MAX_ENCODINGS)
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingXCursor);
      if (se->nEncodings < MAX_ENCODINGS)
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRichCursor);
      if (se->nEncodings < MAX_ENCODINGS)
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingPointerPos);
    }

    if (se->nEncodings < MAX_ENCODINGS && requestLastRectEncoding) {
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingLastRect);
    }

    if (se->nEncodings < MAX_ENCODINGS)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingNewFBSize);
    if (se->nEncodings < MAX_ENCODINGS)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingExtendedDesktopSize);
    if (se->nEncodings < MAX_ENCODINGS)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingDesktopName);
    if (se->nEncodings < MAX_ENCODINGS)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingFence);
    if (se->nEncodings < MAX_ENCODINGS)
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingContinuousUpdates);
  }
  else {
    /* The list is in the order we would like the server to use, so asking for
       a particular encoding is just a matter of putting it at the front.  The
       rest still follow it, since a server that cannot do our first choice
       has to be left something it can do. */
    static const CARD32 fallbackEncs[] = {
      rfbEncodingTight, rfbEncodingZRLE, rfbEncodingHextile,
      rfbEncodingZlib, rfbEncodingCoRRE, rfbEncodingRRE
    };
    int i;

    if (appData.preferredEncoding >= 0) {
      fprintf(stderr,"Preferring %s encoding\n",
	      EncodingName(appData.preferredEncoding));
      encs[se->nEncodings++] = Swap32IfLE(appData.preferredEncoding);
    } else if (SameMachine(rfbsock)) {
      if (!tunnelSpecified) {
	fprintf(stderr,"Same machine: preferring raw encoding\n");
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRaw);
      } else {
	fprintf(stderr,"Tunneling active: preferring tight encoding\n");
      }
    }

    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCopyRect);

    for (i = 0; i < (int)XtNumber(fallbackEncs); i++)
      if ((int)fallbackEncs[i] != appData.preferredEncoding)
	encs[se->nEncodings++] = Swap32IfLE(fallbackEncs[i]);

    if (appData.compressLevel >= 0 && appData.compressLevel <= 9) {
      encs[se->nEncodings++] = Swap32IfLE(appData.compressLevel +
					  rfbEncodingCompressLevel0);
    } else if (!tunnelSpecified) {
      /* If -tunnel option was provided, we assume that server machine is
	 not in the local network so we use default compression level for
	 tight encoding instead of fast compression. Thus we are
	 requesting level 1 compression only if tunneling is not used. */
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingCompressLevel1);
    }

    if (appData.enableJPEG) {
      if (appData.qualityLevel < 0 || appData.qualityLevel > 9)
	appData.qualityLevel = 5;
      encs[se->nEncodings++] = Swap32IfLE(appData.qualityLevel +
					  rfbEncodingQualityLevel0);
      if (appData.forceJPEG)
	encs[se->nEncodings++] = Swap32IfLE(rfbEncodingSubsampGray);
    }

    if (appData.useRemoteCursor) {
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingXCursor);
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingRichCursor);
      encs[se->nEncodings++] = Swap32IfLE(rfbEncodingPointerPos);
    }

    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingLastRect);

    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingNewFBSize);
    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingExtendedDesktopSize);
    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingDesktopName);
    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingFence);
    encs[se->nEncodings++] = Swap32IfLE(rfbEncodingContinuousUpdates);
  }

  {
    const char *primaryEnc = "raw";
    int compLevel = -1, qualLevel = -1;
    Bool jpegEnabled = False;
    int i;
    for (i = 0; i < se->nEncodings; i++) {
      CARD32 enc = Swap32IfLE(encs[i]);
      if (i == 0 || (i == 1 && Swap32IfLE(encs[0]) == rfbEncodingCopyRect)) {
        switch (enc) {
        case rfbEncodingRaw:      primaryEnc = "raw";      break;
        case rfbEncodingCopyRect: primaryEnc = "copyrect"; break;
        case rfbEncodingRRE:      primaryEnc = "rre";      break;
        case rfbEncodingCoRRE:    primaryEnc = "corre";    break;
        case rfbEncodingHextile:  primaryEnc = "hextile";  break;
        case rfbEncodingZlib:     primaryEnc = "zlib";     break;
        case rfbEncodingTight:    primaryEnc = "tight";    break;
        case rfbEncodingZRLE:     primaryEnc = "zrle";     break;
        }
      }
      if (enc >= rfbEncodingCompressLevel0 && enc <= rfbEncodingCompressLevel0 + 9)
        compLevel = enc - rfbEncodingCompressLevel0;
      if (enc >= rfbEncodingQualityLevel0 && enc <= rfbEncodingQualityLevel0 + 9) {
        qualLevel = enc - rfbEncodingQualityLevel0;
        jpegEnabled = True;
      }
    }
    fprintf(stderr, "Requesting %s encoding", primaryEnc);
    if (compLevel >= 0)
      fprintf(stderr, ", zlib compression level %d", compLevel);
    if (jpegEnabled)
      fprintf(stderr, ", JPEG quality level %d", qualLevel);
    if (appData.useRemoteCursor)
      fprintf(stderr, ", remote cursor");
    fprintf(stderr, "\n");

    strcpy(titleEncName, primaryEnc);
    UpdateWindowTitle();
  }

  len = sz_rfbSetEncodingsMsg + se->nEncodings * 4;

  se->nEncodings = Swap16IfLE(se->nEncodings);

  if (!WriteExact(rfbsock, buf, len)) return False;

  STATS(vncStats.msgsOut++);
  StatsLog(1, "SetEncodings", len, 0.0);

  return True;
}


/*
 * SendIncrementalFramebufferUpdateRequest.
 */

Bool
SendIncrementalFramebufferUpdateRequest()
{
  return SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
				      si.framebufferHeight, True);
}


/*
 * SendFramebufferUpdateRequest.
 */

Bool
SendFramebufferUpdateRequest(int x, int y, int w, int h, Bool incremental)
{
  rfbFramebufferUpdateRequestMsg fur;

  fur.type = rfbFramebufferUpdateRequest;
  fur.incremental = incremental ? 1 : 0;
  fur.x = Swap16IfLE(x);
  fur.y = Swap16IfLE(y);
  fur.w = Swap16IfLE(w);
  fur.h = Swap16IfLE(h);

  if (!WriteExact(rfbsock, (char *)&fur, sz_rfbFramebufferUpdateRequestMsg))
    return False;

  STATS(vncStats.msgsOut++);
  if (incremental) {
    STATS(vncStats.sentUpdateReq++);
    StatsUpdateRequested();
    StatsLog(1, "FramebufferUpdateRequest incr",
	     sz_rfbFramebufferUpdateRequestMsg, 0.0);
  } else {
    STATS(vncStats.sentFullReq++);
    StatsLog(1, "FramebufferUpdateRequest full",
	     sz_rfbFramebufferUpdateRequestMsg, 0.0);
  }

  return True;
}


/*
 * SendPointerEvent.
 */

Bool
SendPointerEvent(int x, int y, int buttonMask)
{
  rfbPointerEventMsg pe;

  pe.type = rfbPointerEvent;
  pe.buttonMask = buttonMask;
  if (x < 0) x = 0;
  if (y < 0) y = 0;

  if (!appData.useX11Cursor)
    SoftCursorMove(x, y);

  pe.x = Swap16IfLE(x);
  pe.y = Swap16IfLE(y);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentPointer++);
  StatsLog(1, "PointerEvent", sz_rfbPointerEventMsg, 0.0);

  return WriteExact(rfbsock, (char *)&pe, sz_rfbPointerEventMsg);
}


/*
 * SendKeyEvent.
 */

Bool
SendKeyEvent(CARD32 key, Bool down)
{
  rfbKeyEventMsg ke;

  ke.type = rfbKeyEvent;
  ke.down = down ? 1 : 0;
  ke.key = Swap32IfLE(key);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentKey++);
  StatsLog(1, down ? "KeyEvent down" : "KeyEvent up", sz_rfbKeyEventMsg, 0.0);

  return WriteExact(rfbsock, (char *)&ke, sz_rfbKeyEventMsg);
}


/*
 * SendClientCutText.
 */

Bool
SendClientCutText(char *str, int len)
{
  rfbClientCutTextMsg cct;

  if (serverCutText)
    free(serverCutText);
  serverCutText = NULL;

  cct.type = rfbClientCutText;
  cct.length = Swap32IfLE(len);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentCutText++);
  StatsLog(1, "ClientCutText", sz_rfbClientCutTextMsg + len, 0.0);

  return  (WriteExact(rfbsock, (char *)&cct, sz_rfbClientCutTextMsg) &&
	   WriteExact(rfbsock, str, len));
}


/*
 * SendSetDesktopSize - ask the server to resize its framebuffer (TigerVNC).
 * We report a single screen covering the whole framebuffer, preserving the
 * id and flags of the server's primary screen.
 */

Bool
SendSetDesktopSize(int width, int height)
{
  char buf[sz_rfbSetDesktopSizeMsg + sz_rfbScreenInfo];
  rfbSetDesktopSizeMsg *sds = (rfbSetDesktopSizeMsg *)buf;
  rfbScreenInfo *screen = (rfbScreenInfo *)&buf[sz_rfbSetDesktopSizeMsg];

  if (!supportsSetDesktopSize)
    return True;

  if (appData.debug)
    fprintf(stderr, "Requesting remote desktop resize to %dx%d\n",
	    width, height);

  sds->type = rfbSetDesktopSize;
  sds->pad1 = 0;
  sds->width = Swap16IfLE(width);
  sds->height = Swap16IfLE(height);
  sds->numberOfScreens = 1;
  sds->pad2 = 0;

  screen->id = Swap32IfLE(screenId);
  screen->x = 0;
  screen->y = 0;
  screen->width = Swap16IfLE(width);
  screen->height = Swap16IfLE(height);
  screen->flags = Swap32IfLE(screenFlags);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentSetDesktopSize++);
  StatsLog(1, "SetDesktopSize", sz_rfbSetDesktopSizeMsg + sz_rfbScreenInfo,
	   0.0);

  return WriteExact(rfbsock, buf, sz_rfbSetDesktopSizeMsg + sz_rfbScreenInfo);
}


/*
 * SendEnableContinuousUpdates (TigerVNC).
 */

Bool
SendEnableContinuousUpdates(Bool enable, int x, int y, int w, int h)
{
  rfbEnableContinuousUpdatesMsg ecu;

  ecu.type = rfbEnableContinuousUpdates;
  ecu.enable = enable ? 1 : 0;
  ecu.x = Swap16IfLE(x);
  ecu.y = Swap16IfLE(y);
  ecu.w = Swap16IfLE(w);
  ecu.h = Swap16IfLE(h);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentEnableCU++);
  StatsLog(1, enable ? "EnableContinuousUpdates on" :
	   "EnableContinuousUpdates off", sz_rfbEnableContinuousUpdatesMsg,
	   0.0);

  return WriteExact(rfbsock, (char *)&ecu, sz_rfbEnableContinuousUpdatesMsg);
}


/*
 * ToggleContinuousUpdates is an action which switches continuous updates on
 * or off during an active connection.  Disabling is asynchronous: the server
 * confirms with EndOfContinuousUpdates, whose handler resumes polling.
 */

void
ToggleContinuousUpdates(Widget w, XEvent *ev, String *params,
			Cardinal *num_params)
{
  if (!supportsCU) {
    fprintf(stderr, "Server does not support continuous updates\n");
    return;
  }

  /* Keep what was asked for as well as what is in force: the settings panel
     shows the request, and a reconnect starts from it. */
  appData.useContinuousUpdates = !cuActive;

  if (cuActive) {
    SendEnableContinuousUpdates(False, 0, 0, 0, 0);
  } else {
    fprintf(stderr, "Continuous updates enabled\n");
    cuActive = True;
    SendEnableContinuousUpdates(True, 0, 0, si.framebufferWidth,
				si.framebufferHeight);
  }
}


/*
 * SetContinuousUpdatesState is an action which sets the "state" resource of a
 * toggle widget to reflect whether continuous updates are active, greyed out
 * until the server has announced support.
 */

void
SetContinuousUpdatesState(Widget w, XEvent *ev, String *params,
			  Cardinal *num_params)
{
  XtVaSetValues(w, "state", cuActive, XtNsensitive, supportsCU, NULL);
}


/*
 * SendFence - send a fence message, either our own request or a response to
 * a server fence (TigerVNC).
 */

Bool
SendFence(CARD32 flags, int len, char *data)
{
  char buf[sz_rfbFenceMsg + 64];
  rfbFenceMsg *f = (rfbFenceMsg *)buf;

  if (len > 64) {
    fprintf(stderr, "SendFence: payload too large\n");
    return True;
  }

  memset(buf, 0, sz_rfbFenceMsg);
  f->type = rfbClientFence;
  f->flags = Swap32IfLE(flags);
  f->length = (CARD8)len;
  if (len > 0)
    memcpy(&buf[sz_rfbFenceMsg], data, len);

  STATS(vncStats.msgsOut++);
  STATS(vncStats.sentFence++);
  StatsLog(1, (flags & rfbFenceFlagRequest) ? "Fence request" :
	   "Fence response", sz_rfbFenceMsg + len, 0.0);

  return WriteExact(rfbsock, buf, sz_rfbFenceMsg + len);
}


/*
 * FramebufferSizeChanged - common tail for NewFBSize and ExtendedDesktopSize.
 * Resizes the local framebuffer and refreshes the screen contents, which are
 * undefined after a resize.
 */

static void
FramebufferSizeChanged(int width, int height)
{
  if (width == si.framebufferWidth && height == si.framebufferHeight)
    return;

  ResizeDesktopFramebuffer(width, height);

  if (cuActive)
    SendEnableContinuousUpdates(True, 0, 0, width, height);

  SendFramebufferUpdateRequest(0, 0, width, height, False);
}


/*
 * HandleExtendedDesktopSize - the server tells us about its screen layout,
 * either on its own initiative or as a reply to our SetDesktopSize.  Receipt
 * of this rect also tells us the server accepts SetDesktopSize requests.
 */

static Bool
HandleExtendedDesktopSize(int reason, int result, int w, int h)
{
  CARD8 hdr[4];
  rfbScreenInfo screen;
  Bool firstEDS;
  int i;

  if (!ReadFromRFBServer((char *)hdr, 4))
    return False;

  for (i = 0; i < (int)hdr[0]; i++) {
    if (!ReadFromRFBServer((char *)&screen, sz_rfbScreenInfo))
      return False;
    if (i == 0) {
      screenId = Swap32IfLE(screen.id);
      screenFlags = Swap32IfLE(screen.flags);
    }
  }

  firstEDS = !supportsSetDesktopSize;
  supportsSetDesktopSize = True;

  if (firstEDS)
    fprintf(stderr, "Server supports remote desktop resizing "
	    "(SetDesktopSize)\n");

  if (reason == rfbEDSReasonClient) {
    pendingDesktopResize = False;
    if (result != rfbEDSResultSuccess) {
      fprintf(stderr, "Server refused desktop resize, error %d\n", result);
      return True;
    }
  }

  FramebufferSizeChanged(w, h);

  /* Now that we know the server can resize, sync it with our window size
     (matters when the window started smaller than the remote desktop, or
     with an explicit -geometry). */
  if (firstEDS)
    DesktopSizeSupportLearned();

  return True;
}


/*
 * HandleDesktopName - the server's desktop name has changed (TigerVNC).
 */

static Bool
HandleDesktopName(void)
{
  CARD32 nameLen;
  char *name;

  if (!ReadFromRFBServer((char *)&nameLen, 4))
    return False;
  nameLen = Swap32IfLE(nameLen);

  if (!RfbValidServerStringLength(nameLen, 1)) {
    fprintf(stderr, "Desktop name length too large (%lu bytes)\n",
            (unsigned long)nameLen);
    return False;
  }

  name = malloc(nameLen + 1);
  if (name == NULL)
    return False;

  if (!ReadFromRFBServer(name, nameLen)) {
    free(name);
    return False;
  }
  name[nameLen] = '\0';
  SanitizeDesktopName(name);

  free(desktopName);
  desktopName = name;

  /* fprintf(stderr, "Desktop name changed to \"%s\"\n", desktopName); */
  UpdateWindowTitle();

  return True;
}


/*
 * EncodingName - the name the -encodings option, the connection dialog and
 * the diagnostics all use for an encoding.  NULL for one we cannot decode.
 */

const char *
EncodingName(CARD32 enc)
{
  switch (enc) {
  case rfbEncodingRaw:      return "raw";
  case rfbEncodingCopyRect: return "copyrect";
  case rfbEncodingRRE:      return "rre";
  case rfbEncodingCoRRE:    return "corre";
  case rfbEncodingHextile:  return "hextile";
  case rfbEncodingZlib:     return "zlib";
  case rfbEncodingTight:    return "tight";
  case rfbEncodingZRLE:     return "zrle";
  }

  return NULL;
}


/*
 * ReportRectEncoding - print, once per encoding per session, which
 * encodings the server actually sends (may differ from what we requested).
 */

static void
ReportRectEncoding(CARD32 enc)
{
  const char *name;
  int i;

  for (i = 0; i < nReportedEncs; i++)
    if (reportedEncs[i] == enc)
      return;
  if (nReportedEncs >= 16)
    return;
  reportedEncs[nReportedEncs++] = enc;

  name = EncodingName(enc);
  if (!name)
    return;

  fprintf(stderr, "Server using %s encoding\n", name);

#ifdef VNCSTATS
  {
    char note[48];

    sprintf(note, "server using %s encoding", name);
    StatsLog(-1, note, 0.0, 0.0);
  }
#endif
}


/*
 * HandleRFBServerMessage.
 */

Bool
HandleRFBServerMessage()
{
  rfbServerToClientMsg msg;

  if (!ReadFromRFBServer((char *)&msg, 1))
    return False;

  STATS(vncStats.msgsIn++);

  switch (msg.type) {

  case rfbSetColorMapEntries:
  {
    int i;
    CARD16 rgb[3];
    XColor xc;

    STATS(vncStats.msgColorMap++);
    StatsLog(0, "SetColorMapEntries", sz_rfbSetColorMapEntriesMsg, 0.0);

    if (!ReadFromRFBServer(((char *)&msg) + 1,
			   sz_rfbSetColorMapEntriesMsg - 1))
      return False;

    msg.scme.firstColor = Swap16IfLE(msg.scme.firstColor);
    msg.scme.nColors = Swap16IfLE(msg.scme.nColors);

    for (i = 0; i < msg.scme.nColors; i++) {
      if (!ReadFromRFBServer((char *)rgb, 6))
	return False;
      xc.pixel = msg.scme.firstColor + i;
      xc.red = Swap16IfLE(rgb[0]);
      xc.green = Swap16IfLE(rgb[1]);
      xc.blue = Swap16IfLE(rgb[2]);
      xc.flags = DoRed|DoGreen|DoBlue;
      XStoreColor(dpy, cmap, &xc);
    }

    break;
  }

  case rfbFramebufferUpdate:
  {
    rfbFramebufferUpdateRectHeader rect;
    int linesToRead;
    int bytesPerLine;
    int i;
#ifdef VNCSTATS
    int rectW, rectH;
    VncRectProfile prof;
#endif

    STATS(vncStats.msgUpdate++);
    StatsUpdateStart();

    if (!ReadFromRFBServer(((char *)&msg.fu) + 1,
			   sz_rfbFramebufferUpdateMsg - 1))
      return False;

    msg.fu.nRects = Swap16IfLE(msg.fu.nRects);

    /* Decoding writes over the shared image, so the server has to be done
       reading it first.  Once per update, not once per put: the puts within
       one update are for disjoint areas. */
    SyncShmPuts();

    for (i = 0; i < msg.fu.nRects; i++) {
      StatsRectBegin(&prof);

      if (!ReadFromRFBServer((char *)&rect, sz_rfbFramebufferUpdateRectHeader))
	return False;

      rect.encoding = Swap32IfLE(rect.encoding);
      if (rect.encoding == rfbEncodingLastRect) {
	StatsRectEnd(&prof, rect.encoding, 0, 0);
	break;
      }

      rect.r.x = Swap16IfLE(rect.r.x);
      rect.r.y = Swap16IfLE(rect.r.y);
      rect.r.w = Swap16IfLE(rect.r.w);
      rect.r.h = Swap16IfLE(rect.r.h);

      /* the raw decoder walks rect.r.y/h as it goes, so keep the real size */
#ifdef VNCSTATS
      rectW = rect.r.w;
      rectH = rect.r.h;
#endif

      if (rect.encoding == rfbEncodingXCursor ||
	  rect.encoding == rfbEncodingRichCursor) {
	/* 0x0 is a valid "no cursor" shape, and HandleCursorShape() does the
	   RFB_MAX_CURSOR_DIMENSION check itself */
	if (!HandleCursorShape(rect.r.x, rect.r.y, rect.r.w, rect.r.h,
			      rect.encoding)) {
	  return False;
	}
	StatsRectEnd(&prof, rect.encoding, rectW, rectH);
	continue;
      }

      if (rect.encoding == rfbEncodingPointerPos) {
	if (!HandleCursorPos(rect.r.x, rect.r.y)) {
	  return False;
	}
	StatsRectEnd(&prof, rect.encoding, 0, 0);
	continue;
      }

      if (rect.encoding == rfbEncodingNewFBSize) {
	FramebufferSizeChanged(rect.r.w, rect.r.h);
	StatsRectEnd(&prof, rect.encoding, 0, 0);
	continue;
      }

      if (rect.encoding == rfbEncodingExtendedDesktopSize) {
	if (!HandleExtendedDesktopSize(rect.r.x, rect.r.y,
				       rect.r.w, rect.r.h)) {
	  return False;
	}
	StatsRectEnd(&prof, rect.encoding, 0, 0);
	continue;
      }

      if (rect.encoding == rfbEncodingDesktopName) {
	if (!HandleDesktopName()) {
	  return False;
	}
	StatsRectEnd(&prof, rect.encoding, 0, 0);
	continue;
      }

      if ((rect.r.x + rect.r.w > si.framebufferWidth) ||
	  (rect.r.y + rect.r.h > si.framebufferHeight))
	{
	  fprintf(stderr,"Rect too large: %dx%d at (%d, %d)\n",
		  rect.r.w, rect.r.h, rect.r.x, rect.r.y);
	  return False;
	}

      if (rect.r.h * rect.r.w == 0) {
	fprintf(stderr,"Zero size rect - ignoring\n");
	continue;
      }

      ReportRectEncoding(rect.encoding);

      /* If RichCursor encoding is used, we should prevent collisions
	 between framebuffer updates and cursor drawing operations. */
      SoftCursorLockArea(rect.r.x, rect.r.y, rect.r.w, rect.r.h);

      switch (rect.encoding) {

      case rfbEncodingRaw:

	bytesPerLine = rect.r.w * myFormat.bitsPerPixel / 8;

	/* The wire format is the image format unless we asked for a reduced
	   one, so the rows can be read straight into the image instead of
	   through a scratch buffer and a second copy. */

	if (!useColorMap && appData.rawDelay == 0) {
	  char *row = ImageRow(rect.r.x, rect.r.y);
	  int stride = ImageStride();
	  int n;

	  if (bytesPerLine == stride) {
	    if (!ReadFromRFBServer(row, (unsigned int)bytesPerLine * rect.r.h))
	      return False;
	  } else {
	    for (n = 0; n < rect.r.h; n++, row += stride)
	      if (!ReadFromRFBServer(row, bytesPerLine))
		return False;
	  }

	  STATS(vncStats.blitPixels += (double)rect.r.w * rect.r.h);
	  PutImageRect(rect.r.x, rect.r.y, rect.r.w, rect.r.h);
	  break;
	}

	/* A reduced format still has to be translated, but it can be
	   translated straight out of the socket buffer.  Reduced formats are
	   one byte per pixel, so a run of bytes is a run of pixels. */

	if (useColorMap && appData.rawDelay == 0) {
	  unsigned int left = (unsigned int)rect.r.w * rect.r.h;
	  int cx = 0, cy = 0;

	  while (left > 0) {
	    char *p, *q;
	    unsigned int got, n;

	    if (!ReadFromRFBServerPeek(&p, left, &got))
	      return False;

	    q = p;
	    left -= got;
	    while (got > 0) {
	      n = (unsigned int)(rect.r.w - cx);
	      if (n > got)
		n = got;

	      CopyDataToImage(q, rect.r.x + cx, rect.r.y + cy, (int)n, 1);

	      q += n;
	      got -= n;
	      cx += (int)n;
	      if (cx == rect.r.w) {
		cx = 0;
		cy++;
	      }
	    }
	    ReadFromRFBServerSkip((unsigned int)(q - p));
	  }

	  PutImageRect(rect.r.x, rect.r.y, rect.r.w, rect.r.h);
	  break;
	}

	linesToRead = BUFFER_SIZE / bytesPerLine;

	while (rect.r.h > 0) {
	  if (linesToRead > rect.r.h)
	    linesToRead = rect.r.h;

	  if (!ReadFromRFBServer(buffer,bytesPerLine * linesToRead))
	    return False;

	  CopyDataToScreen(buffer, rect.r.x, rect.r.y, rect.r.w,
			   linesToRead);

	  rect.r.h -= linesToRead;
	  rect.r.y += linesToRead;

	}
	break;

      case rfbEncodingCopyRect:
      {
	rfbCopyRect cr;

	STATS(vncStats.copyAreas++);

	if (!ReadFromRFBServer((char *)&cr, sz_rfbCopyRect))
	  return False;

	cr.srcX = Swap16IfLE(cr.srcX);
	cr.srcY = Swap16IfLE(cr.srcY);

	/* If RichCursor encoding is used, we should extend our
	   "cursor lock area" (previously set to destination
	   rectangle) to the source rectangle as well. */
	SoftCursorLockArea(cr.srcX, cr.srcY, rect.r.w, rect.r.h);

	if (appData.copyRectDelay != 0) {
	  XFillRectangle(dpy, desktopWin, srcGC, cr.srcX, cr.srcY,
			 rect.r.w, rect.r.h);
	  XFillRectangle(dpy, desktopWin, dstGC, rect.r.x, rect.r.y,
			 rect.r.w, rect.r.h);
	  XSync(dpy,False);
	  Msleep(appData.copyRectDelay);
	  XFillRectangle(dpy, desktopWin, dstGC, rect.r.x, rect.r.y,
			 rect.r.w, rect.r.h);
	  XFillRectangle(dpy, desktopWin, srcGC, cr.srcX, cr.srcY,
			 rect.r.w, rect.r.h);
	}

	/* Move it in the image as well, or the image stops being a mirror of
	   the window everywhere the remote desktop has ever scrolled. */
	CopyImageRect(cr.srcX, cr.srcY, rect.r.w, rect.r.h,
		      rect.r.x, rect.r.y);

	XCopyArea(dpy, desktopWin, desktopWin, gc, cr.srcX, cr.srcY,
		  rect.r.w, rect.r.h, rect.r.x, rect.r.y);

	break;
      }

      case rfbEncodingRRE:
      {
	switch (myFormat.bitsPerPixel) {
	case 8:
	  if (!HandleRRE8(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 16:
	  if (!HandleRRE16(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 32:
	  if (!HandleRRE32(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	}
	break;
      }

      case rfbEncodingCoRRE:
      {
	switch (myFormat.bitsPerPixel) {
	case 8:
	  if (!HandleCoRRE8(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 16:
	  if (!HandleCoRRE16(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 32:
	  if (!HandleCoRRE32(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	}
	break;
      }

      case rfbEncodingHextile:
      {
	switch (myFormat.bitsPerPixel) {
	case 8:
	  if (!HandleHextile8(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 16:
	  if (!HandleHextile16(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 32:
	  if (!HandleHextile32(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	}
	break;
      }

      case rfbEncodingZlib:
      {
	switch (myFormat.bitsPerPixel) {
	case 8:
	  if (!HandleZlib8(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 16:
	  if (!HandleZlib16(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 32:
	  if (!HandleZlib32(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	}
	break;
     }

      case rfbEncodingTight:
      {
	switch (myFormat.bitsPerPixel) {
	case 8:
	  if (!HandleTight8(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 16:
	  if (!HandleTight16(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	case 32:
	  if (!HandleTight32(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	    return False;
	  break;
	}
	break;
      }

      case rfbEncodingZRLE:
      {
	if (!HandleZRLE(rect.r.x,rect.r.y,rect.r.w,rect.r.h))
	  return False;
	break;
      }

      default:
	fprintf(stderr,"Unknown rect encoding %d\n",
		(int)rect.encoding);
	return False;
      }

      /* Now we may discard "soft cursor locks". */
      SoftCursorUnlockScreen();

      StatsRectEnd(&prof, rect.encoding, rectW, rectH);

      /* Under continuous updates the socket may never block, so yield to
	 X regularly even inside a large update. */
      if ((i & 15) == 15)
	ProcessPendingXEvents();
    }

#ifdef MITSHM
    /* if using shared memory PutImage, make sure that the X server has
       updated its framebuffer before we reuse the shared memory.  This is
       mainly to avoid copyrect using invalid screen contents - not sure
       if we'd need it otherwise. */

    if (appData.useShm)
      XSync(dpy, False);
#endif

    /* If the server supports continuous updates, enable them once the
       first update is on the screen and stop polling with update
       requests (TigerVNC). */

    if (firstUpdate) {
      firstUpdate = False;
      if (supportsCU && appData.useContinuousUpdates) {
	fprintf(stderr, "Continuous updates enabled\n");
	StatsLog(-1, "continuous updates enabled", 0.0, 0.0);
	cuActive = True;
	if (!SendEnableContinuousUpdates(True, 0, 0, si.framebufferWidth,
					 si.framebufferHeight))
	  return False;
      }
    }

    StatsUpdateEnd();

    if (!cuActive) {
      if (!SendIncrementalFramebufferUpdateRequest())
	return False;
    }

    break;
  }

  case rfbBell:
  {
    Window toplevelWin;

    STATS(vncStats.msgBell++);
    StatsLog(0, "Bell", 1, 0.0);

    XBell(dpy, 0);

    if (appData.raiseOnBeep) {
      toplevelWin = XtWindow(toplevel);
      XMapRaised(dpy, toplevelWin);
    }

    break;
  }

  case rfbServerCutText:
  {
    STATS(vncStats.msgCutText++);

    if (!ReadFromRFBServer(((char *)&msg) + 1,
			   sz_rfbServerCutTextMsg - 1))
      return False;

    msg.sct.length = Swap32IfLE(msg.sct.length);

    if (!RfbValidServerStringLength(msg.sct.length, 1)) {
      fprintf(stderr, "ServerCutText length too large (%lu bytes)\n",
              (unsigned long)msg.sct.length);
      return False;
    }

    if (serverCutText)
      free(serverCutText);

    serverCutText = malloc(msg.sct.length+1);
    if (serverCutText == NULL) {
      fprintf(stderr, "ServerCutText: out of memory\n");
      return False;
    }

    if (!ReadFromRFBServer(serverCutText, msg.sct.length))
      return False;

    serverCutText[msg.sct.length] = 0;

    StatsLog(0, "ServerCutText", sz_rfbServerCutTextMsg + msg.sct.length, 0.0);

    newServerCutText = True;

    break;
  }

  case rfbServerFence:
  {
    char fdata[64];

    if (!ReadFromRFBServer(((char *)&msg) + 1, sz_rfbFenceMsg - 1))
      return False;

    msg.fence.flags = Swap32IfLE(msg.fence.flags);

    if (msg.fence.length > 64) {
      fprintf(stderr, "Fence payload too large from server\n");
      return False;
    }
    if (msg.fence.length > 0) {
      if (!ReadFromRFBServer(fdata, msg.fence.length))
	return False;
    }

    if (!supportsFence)
      fprintf(stderr, "Server using fence extension\n");
    supportsFence = True;
    STATS(vncStats.msgFence++);

    /* the echo of our own latency probe stops here */
    if (!(msg.fence.flags & rfbFenceFlagRequest) &&
	StatsFencePong(msg.fence.length, fdata)) {
      StatsLog(0, "Fence latency probe echo",
	       sz_rfbFenceMsg + msg.fence.length, 0.0);
      break;
    }

    StatsLog(0, "Fence", sz_rfbFenceMsg + msg.fence.length, 0.0);

    if (appData.debug)
      fprintf(stderr, "Fence from server (flags 0x%lx, %d bytes)\n",
	      (unsigned long)msg.fence.flags, (int)msg.fence.length);

    /* We handle all messages synchronously, so we can simply accept any
       blocking flags and echo the fence straight back (TigerVNC). */
    if (msg.fence.flags & rfbFenceFlagRequest) {
      if (!SendFence(msg.fence.flags & (rfbFenceFlagBlockBefore |
					rfbFenceFlagBlockAfter),
		     msg.fence.length, fdata))
	return False;
    }

    break;
  }

  case rfbEndOfContinuousUpdates:
  {
    /* Nothing follows the message type byte.  First receipt announces that
       the server supports continuous updates; when continuous updates are
       active it marks them disabled, so fall back to polling. */

    STATS(vncStats.msgEndCU++);
    StatsLog(0, "EndOfContinuousUpdates", 1, 0.0);
    supportsCU = True;

    if (cuActive) {
      cuActive = False;
      fprintf(stderr, "Continuous updates disabled\n");
      StatsLog(-1, "continuous updates disabled", 0.0, 0.0);
      if (!SendIncrementalFramebufferUpdateRequest())
	return False;
    }

    RefreshPopup();
    break;
  }

  default:
    fprintf(stderr,"Unknown message type %d from VNC server\n",msg.type);
    return False;
  }

  return True;
}


#define GET_PIXEL8(pix, ptr) ((pix) = *(ptr)++)

#define GET_PIXEL16(pix, ptr) (((CARD8*)&(pix))[0] = *(ptr)++, \
			       ((CARD8*)&(pix))[1] = *(ptr)++)

#define GET_PIXEL32(pix, ptr) (((CARD8*)&(pix))[0] = *(ptr)++, \
			       ((CARD8*)&(pix))[1] = *(ptr)++, \
			       ((CARD8*)&(pix))[2] = *(ptr)++, \
			       ((CARD8*)&(pix))[3] = *(ptr)++)

/* CONCAT2 concatenates its two arguments.  CONCAT2E does the same but also
   expands its arguments if they are macros */

#define CONCAT2(a,b) a##b
#define CONCAT2E(a,b) CONCAT2(a,b)

#define BPP 8
#include "rre.c"
#include "corre.c"
#include "hextile.c"
#include "zlibenc.c"
#include "tight.c"
#undef BPP
#define BPP 16
#include "rre.c"
#include "corre.c"
#include "hextile.c"
#include "zlibenc.c"
#include "tight.c"
#undef BPP
#define BPP 32
#include "rre.c"
#include "corre.c"
#include "hextile.c"
#include "zlibenc.c"
#include "tight.c"
#undef BPP

/* ZRLE works on opaque pixel bytes, one decoder for all depths */
#include "zrle.c"

/*
 * Read the string describing the reason for a connection failure.
 */

static void
ReadConnFailedReason(void)
{
  CARD32 reasonLen;
  char *reason = NULL;

  if (ReadFromRFBServer((char *)&reasonLen, sizeof(reasonLen))) {
    reasonLen = Swap32IfLE(reasonLen);
    if (!RfbValidServerStringLength(reasonLen, 0)) {
      ConnError("Connection failure reason too long (%lu bytes)",
		(unsigned long)reasonLen);
      return;
    }
    if ((reason = malloc(reasonLen)) != NULL &&
        ReadFromRFBServer(reason, reasonLen)) {
      /* The server writes this text, so only as much of it as the dialog
	 can hold is worth passing on. */
      ConnError("%.*s", (int)(reasonLen < CONN_ERROR_LEN ?
			       reasonLen : CONN_ERROR_LEN - 1), reason);
      free(reason);
      return;
    }
  }

  ConnError("VNC connection failed");

  if (reason != NULL)
    free(reason);
}

/*
 * PrintPixelFormat.
 */

void
PrintPixelFormat(rfbPixelFormat *format)
{
  if (format->bitsPerPixel == 1) {
    fprintf(stderr,"  Single bit per pixel.\n");
    fprintf(stderr,
	    "  %s significant bit in each byte is leftmost on the screen.\n",
	    (format->bigEndian ? "Most" : "Least"));
  } else {
    fprintf(stderr,"  %d bits per pixel.\n",format->bitsPerPixel);
    if (format->bitsPerPixel != 8) {
      fprintf(stderr,"  %s significant byte first in each pixel.\n",
	      (format->bigEndian ? "Most" : "Least"));
    }
    if (format->trueColor) {
      fprintf(stderr,"  True color: max red %d green %d blue %d",
	      format->redMax, format->greenMax, format->blueMax);
      fprintf(stderr,", shift red %d green %d blue %d\n",
	      format->redShift, format->greenShift, format->blueShift);
    } else {
      fprintf(stderr,"  Color map (not true color).\n");
    }
  }
}

/*
 * Read an integer value encoded in 1..3 bytes. This function is used
 * by the Tight decoder.
 */

static long
ReadCompactLen (void)
{
  long len;
  CARD8 b;

  if (!ReadFromRFBServer((char *)&b, 1))
    return -1;
  len = (int)b & 0x7F;
  if (b & 0x80) {
    if (!ReadFromRFBServer((char *)&b, 1))
      return -1;
    len |= ((int)b & 0x7F) << 7;
    if (b & 0x80) {
      if (!ReadFromRFBServer((char *)&b, 1))
	return -1;
      len |= ((int)b & 0xFF) << 14;
    }
  }
  return len;
}


/*
 * JPEG source manager functions for JPEG decompression in Tight decoder.
 */

static struct jpeg_source_mgr jpegSrcManager;
static JOCTET *jpegBufferPtr;
static size_t jpegBufferLen;

static void
JpegInitSource(j_decompress_ptr cinfo)
{
  jpegError = False;
}

static boolean
JpegFillInputBuffer(j_decompress_ptr cinfo)
{
  jpegError = True;
  jpegSrcManager.bytes_in_buffer = jpegBufferLen;
  jpegSrcManager.next_input_byte = (JOCTET *)jpegBufferPtr;

  return TRUE;
}

static void
JpegSkipInputData(j_decompress_ptr cinfo, long num_bytes)
{
  if (num_bytes < 0 || (size_t) num_bytes > jpegSrcManager.bytes_in_buffer) {
    jpegError = True;
    jpegSrcManager.bytes_in_buffer = jpegBufferLen;
    jpegSrcManager.next_input_byte = (JOCTET *)jpegBufferPtr;
  } else {
    jpegSrcManager.next_input_byte += (size_t) num_bytes;
    jpegSrcManager.bytes_in_buffer -= (size_t) num_bytes;
  }
}

static void
JpegTermSource(j_decompress_ptr cinfo)
{
  /* No work necessary here. */
}

static void
JpegSetSrcManager(j_decompress_ptr cinfo, CARD8 *compressedData,
		  int compressedLen)
{
  jpegBufferPtr = (JOCTET *)compressedData;
  jpegBufferLen = (size_t)compressedLen;

  jpegSrcManager.init_source = JpegInitSource;
  jpegSrcManager.fill_input_buffer = JpegFillInputBuffer;
  jpegSrcManager.skip_input_data = JpegSkipInputData;
  jpegSrcManager.resync_to_restart = jpeg_resync_to_restart;
  jpegSrcManager.term_source = JpegTermSource;
  jpegSrcManager.next_input_byte = jpegBufferPtr;
  jpegSrcManager.bytes_in_buffer = jpegBufferLen;

  cinfo->src = &jpegSrcManager;
}

