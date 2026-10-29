/*
 *  Copyright (C) 2000, 2001 Const Kaplinsky.  All Rights Reserved.
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
 * vncviewer.h
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#ifdef _AIX
#include <sys/select.h>	/* AIX keeps fd_set here, not in types.h/time.h */
#include <strings.h>	/* FD_ZERO expands to bzero() */
#endif
/* SunOS 4 libc is BSD: no ANSI memmove or strerror, but bcopy is defined to
   handle overlapping copies and sys_errlist is what perror prints.  Its
   headers prototype none of them. */
#if defined(sun) && !defined(__SVR4) && !defined(__svr4__)
void bcopy();
#define memmove(d, s, n) bcopy((s), (d), (n))
extern char *sys_errlist[];
extern int sys_nerr;
#define strerror(e) ((e) > 0 && (e) < sys_nerr ? sys_errlist[e] : "I/O error")
#endif
#include <unistd.h>
#include <pwd.h>
#ifdef _WIN32
#include "w32compat.h"		/* win32/, in place of the X headers */
#else
#include <X11/IntrinsicP.h>
#include <X11/StringDefs.h>
#include <X11/Shell.h>
#include <X11/Xmd.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#endif
#ifdef __VMS
#include "vms.h"
#endif
#include "rfbproto.h"
#include "caps.h"
#include "version.h"

extern int endianTest;

#define Swap16IfLE(s) \
    (*(char *)&endianTest ? ((((s) & 0xff) << 8) | (((s) >> 8) & 0xff)) : (s))

#define Swap32IfLE(l) \
    (*(char *)&endianTest ? ((((l) & 0xff000000) >> 24) | \
			     (((l) & 0x00ff0000) >> 8)  | \
			     (((l) & 0x0000ff00) << 8)  | \
			     (((l) & 0x000000ff) << 24))  : (l))

#define MAX_ENCODINGS 32

/* Limits on server-controlled lengths and dimensions (malicious-server hardening). */
#define RFB_BUFFER_SIZE (640*480)
#define RFB_TIGHT_MAX_WIDTH 2048
#define RFB_MAX_STRING_LENGTH 65536
#define RFB_MAX_CURSOR_DIMENSION 4096
#define RFB_MAX_RRE_SUBRECTS 65535
#define RFB_MAX_ALLOC_SIZE ((size_t)0x7fffffff)

#define LISTEN_PORT_OFFSET 5500
#define TUNNEL_PORT_OFFSET 5500
#define SERVER_PORT_OFFSET 5900

#define DEFAULT_SSH_CMD "/usr/bin/ssh"
#define DEFAULT_TUNNEL_CMD  \
  (DEFAULT_SSH_CMD " -f -L %L:localhost:%R %H sleep 20")
#define DEFAULT_VIA_CMD     \
  (DEFAULT_SSH_CMD " -f -L %L:%H:%R %G sleep 20")


/* argsresources.c */

/* How much color to ask the server for when we are not simply using the
   X server's own format.  Anything but full is an 8-bit-per-pixel true
   color format translated through colorToPixel[]; fewer colors cost
   fewer colormap cells on an 8-bit display and compress a good deal
   better on a slow link. */
enum {
  COLOR_FULL,			/* whatever the X visual gives us */
  COLOR_MEDIUM,			/* 256 colors - BGR233 */
  COLOR_LOW,			/* 64 colors - BGR222 */
  COLOR_VERYLOW			/* 8 colors - BGR111 */
};

#define COLOR_DEFAULT	COLOR_MEDIUM	/* when -colorlevel isn't given */

typedef struct {
  Bool shareDesktop;
  Bool viewOnly;
  Bool fullScreen;
  Bool grabKeyboard;
  Bool raiseOnBeep;

  String encodingsString;
  String preferredEncodingString;
  int preferredEncoding;	/* rfbEncoding*, or -1 to let the viewer pick */

  String colorLevelString;
  int colorLevel;
  int nColors;
  Bool useSharedColors;
  Bool forceOwnCmap;
  Bool forceTrueColor;
  int requestedDepth;

  Bool useShm;

  int wmDecorationWidth;
  int wmDecorationHeight;

  char *userLogin;

  char *passwordFile;
  Bool passwordDialog;

  int rawDelay;
  int copyRectDelay;

  Bool debug;

  int popupButtonCount;

  int bumpScrollTime;
  int bumpScrollPixels;

  int compressLevel;
  int qualityLevel;
  Bool enableJPEG;
  Bool forceJPEG;
  Bool useRemoteCursor;
  Bool useX11Cursor;
  Bool useHwCursor;		/* let the X server draw 2-color remote cursors */
  String localCursor;
  Bool autoPass;

  Bool useRemoteResize;
  Bool useContinuousUpdates;

#ifdef VNCSTATS
  Bool showStats;		/* -stats: open the diagnostics window at once */
#endif

} AppData;

extern AppData appData;

extern char *fallback_resources[];
extern char vncServerHost[];
extern int vncServerPort;
extern Bool listenSpecified;
extern int listenPort;

extern XrmOptionDescRec cmdLineOptions[];
extern int numCmdLineOptions;

extern void removeArgs(int *argc, char** argv, int idx, int nargs);
extern void usage(void);
extern void GetArgsAndResources(int argc, char **argv);
extern Bool SetServerName(char *vncServerName);

/* color.c */

/* Set when the server sends us 8bpp pixels that have to be looked up rather
   than used as they stand.  colorToPixel[] is indexed by such a pixel and
   only its first 1 << myFormat.depth entries are ever used. */
extern Bool useColorMap;
extern unsigned long colorToPixel[];

extern Colormap cmap;
extern Visual *vis;
extern unsigned int visdepth, visbpp;

extern void SetVisualAndCmap();
extern void ReloadColorFormat(void);
extern Bool ColorLevelSettable(void);
extern const char *ColorModeName(void);

/* cursor.c */

extern Bool HandleCursorShape(int xhot, int yhot, int width, int height,
                              CARD32 enc);
extern Bool HandleCursorPos(int x, int y);
extern void SoftCursorLockArea(int x, int y, int w, int h);
extern void SoftCursorUnlockScreen(void);
extern void SoftCursorMove(int x, int y);
extern void ForgetRemoteCursor(void);
extern void ToggleHardwareCursor(Widget w, XEvent *event, String *params,
				 Cardinal *num_params);

/* desktop.c */

extern Atom wmDeleteWindow;
extern Widget form, viewport, desktop;
extern Window desktopWin;
extern Cursor dotCursor;
extern GC gc;
extern GC srcGC, dstGC;
extern Dimension dpyWidth, dpyHeight;

extern void DesktopInitBeforeRealization();
extern void DesktopInitAfterRealization();
extern void SendRFBEvent(Widget w, XEvent *event, String *params,
			 Cardinal *num_params);
extern void CopyDataToScreen(char *buf, int x, int y, int width, int height);
extern void CopyDataToImage(char *buf, int x, int y, int width, int height);
extern void PutImageRect(int x, int y, int width, int height);
extern XImage *CreateLocalImage(char *buf, int width, int height);
extern void SyncShmPuts(void);
extern void FillImageRect(int x, int y, int width, int height,
			  unsigned long pixel);
extern void CopyImageRect(int srcX, int srcY, int width, int height,
			  int dstX, int dstY);
extern char *ImageRow(int x, int y);
extern int ImageStride(void);
extern Cursor LocalCursor(void);
extern Bool LocalCursorChosen(void);
extern void RepaintScreen(Widget w, XEvent *event, String *params,
			  Cardinal *num_params);
extern void CycleLocalCursor(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern void SetLocalCursorState(Widget w, XEvent *event, String *params,
				Cardinal *num_params);
extern const char *LocalCursorName(void);
extern void SynchroniseScreen();
extern void ResizeDesktopFramebuffer(int width, int height);
extern void RebuildDesktopFramebuffer(void);
extern void TrackDesktopResizes(void);
extern void DesktopSizeSupportLearned(void);

/* dialogs.c */

extern Bool connectDialogUsed;

extern void CancelDialog(Widget w, XEvent *event, String *params,
			 Cardinal *num_params);
extern char *DoConnectDialog(const char *message);
extern char *DoPasswordDialog();
extern Bool DoSettingsDialog(void);
extern void AskForServer(const char *message);
extern void ForgetPassword(void);

/* fullscreen.c */

extern void ToggleFullScreen(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);
extern void SetFullScreenState(Widget w, XEvent *event, String *params,
			       Cardinal *num_params);
extern Bool BumpScroll(XEvent *ev);
extern void FullScreenOn();
extern void FullScreenOff();

/* listen.c */

extern void listenForIncomingConnections(int *argc, char **argv,
					 int listenArgIndex);

/* misc.c */

extern void ToplevelInitBeforeRealization();
extern void ToplevelInitAfterRealization();
extern Time TimeFromEvent(XEvent *ev);
extern void UpdateWindowTitle(void);
extern void Msleep(int msec);
extern void Pause(Widget w, XEvent *event, String *params,
		  Cardinal *num_params);
extern void RunCommand(Widget w, XEvent *event, String *params,
		       Cardinal *num_params);
extern void Quit(Widget w, XEvent *event, String *params,
		 Cardinal *num_params);
extern void Cleanup();

/* Whatever went wrong with the last connection attempt, ready to be shown in
   the connection dialog.  Empty when nothing has failed yet. */
#define CONN_ERROR_LEN 96
extern char connErrorMsg[CONN_ERROR_LEN];

extern void ConnError(const char *format, ...);

extern Bool RfbMulSize(size_t a, size_t b, size_t c, size_t *result);
extern Bool RfbCheckAddSize(size_t base, size_t extra, size_t *result);
extern Bool RfbValidServerStringLength(CARD32 len, size_t extra);

/* scroll.c */

extern void ScrollInit(void);
extern void ScrollResize(void);
extern void ScrollAllowBars(Bool on);
extern void ScrollTo(int x, int y);
extern void ScrollGetPos(int *x, int *y);
extern void ScrollGetVisible(int *w, int *h);

/* popup.c */

extern Widget popup;
extern void ShowPopup(Widget w, XEvent *event, String *params,
		      Cardinal *num_params);
extern void HidePopup(Widget w, XEvent *event, String *params,
		      Cardinal *num_params);
extern void CreatePopup();
extern void RefreshPopup(void);

/* rfbproto.c */

extern int rfbsock;
extern Bool canUseCoRRE;
extern Bool canUseHextile;
extern char *desktopName;
extern rfbPixelFormat myFormat;
extern rfbServerInitMsg si;
extern char *serverCutText;
extern Bool newServerCutText;

extern int protocolMinorVersion;
extern Bool tightVncProtocol;
extern Bool authFailed;
extern char titleEncName[];

extern Bool supportsSetDesktopSize;
extern Bool pendingDesktopResize;
extern Bool supportsFence;
extern Bool supportsCU;
extern Bool cuActive;

extern Bool ConnectToRFBServer(const char *hostname, int port);
extern Bool InitialiseRFBConnection();
extern Bool SetFormatAndEncodings();
extern Bool SendPixelFormat();
extern Bool SendEncodings();
extern const char *EncodingName(CARD32 enc);
extern Bool SendIncrementalFramebufferUpdateRequest();
extern Bool SendFramebufferUpdateRequest(int x, int y, int w, int h,
					 Bool incremental);
extern Bool SendPointerEvent(int x, int y, int buttonMask);
extern Bool SendKeyEvent(CARD32 key, Bool down);
extern Bool SendClientCutText(char *str, int len);
extern Bool SendSetDesktopSize(int width, int height);
extern Bool SendEnableContinuousUpdates(Bool enable, int x, int y,
					int w, int h);
extern void ToggleContinuousUpdates(Widget w, XEvent *ev, String *params,
				    Cardinal *num_params);
extern void SetContinuousUpdatesState(Widget w, XEvent *ev, String *params,
				      Cardinal *num_params);
extern Bool SendFence(CARD32 flags, int len, char *data);
extern Bool HandleRFBServerMessage();
extern void ForgetSessionPassword(void);

extern void PrintPixelFormat(rfbPixelFormat *format);

/* stats.c
 *
 * The diagnostics are opt-in at compile time: collecting the counters costs
 * a few instructions in the socket, protocol and decoder hot paths plus two
 * gettimeofday() calls per rectangle while profiling, which is not free on
 * the machines this viewer targets.  Build with -DVNCSTATS to get the F8
 * "Diagnostics..." window; without it every STATS() below compiles to
 * nothing and stats.c is an empty object.
 */

#ifdef VNCSTATS

#define STATS(x) do { x; } while (0)

enum {
  STAT_ENC_RAW = 0,
  STAT_ENC_COPYRECT,
  STAT_ENC_RRE,
  STAT_ENC_CORRE,
  STAT_ENC_HEXTILE,
  STAT_ENC_ZLIB,
  STAT_ENC_TIGHT,
  STAT_ENC_ZRLE,
  STAT_ENC_COUNT
};

typedef struct {
  unsigned long rects;
  double bytes;			/* protocol bytes consumed */
  double pixels;
  double time;			/* seconds spent decoding, profiling only */
} VncEncStats;

/* X11 events are grouped rather than counted per type: what matters is which
   kind of traffic is burning the client, not the exact event number. */

enum {
  STAT_XEV_MOTION = 0,
  STAT_XEV_KEY,
  STAT_XEV_BUTTON,
  STAT_XEV_CROSSING,
  STAT_XEV_FOCUS,
  STAT_XEV_KEYMAP,
  STAT_XEV_EXPOSE,
  STAT_XEV_STRUCTURE,
  STAT_XEV_PROPERTY,
  STAT_XEV_SELECTION,
  STAT_XEV_CLIENTMSG,
  STAT_XEV_OTHER,
  STAT_XEV_COUNT
};

/* buckets for the distribution histograms */
#define STAT_NBUCKETS 8

/* one rectangle being decoded, for the per encoding profile */
typedef struct {
  double bytes, time, wait;
} VncRectProfile;

/* Byte and pixel totals are doubles: a long session easily passes the 4GB
   an unsigned long would hold on 32 bit systems. */

typedef struct {
  double startTime;

  /* socket layer */
  double sockIn, sockOut;
  unsigned long sockReads, sockWrites, sockWaits;
  double waitTime;		/* seconds blocked waiting for the server */

  /* protocol layer */
  double streamIn;		/* bytes consumed from the server stream */
  unsigned long msgsIn, msgsOut;
  unsigned long msgUpdate, msgColorMap, msgBell, msgCutText, msgFence,
		msgEndCU;
  unsigned long sentKey, sentPointer, sentUpdateReq, sentFullReq, sentCutText,
		sentFence, sentSetDesktopSize, sentEnableCU;

  unsigned long updates, rects, pseudoRects;
  double pixels, rawEquiv, rectBytes, pseudoBytes;
  VncEncStats enc[STAT_ENC_COUNT];

  unsigned long cursorShapes, cursorMoves, lastRects, fbResizes, nameChanges;

  /* tight decoder */
  unsigned long tightFill, tightJpeg, tightBasic, tightRaw;
  unsigned long tightCopy, tightPalette, tightGradient, tightResets;
  double tightJpegBytes, tightJpegPixels, tightJpegTime;

  /* zlib and zrle decoders */
  double zlibIn, zlibOut, zrleIn, zrleOut;
  unsigned long zrleTiles;

  /* X11 output */
  unsigned long putImages, shmPutImages, copyAreas, fillRects;
  double blitPixels;

  /* X11 input: what the local event loop costs.  xDispatch overlaps waitTime,
     since dispatching is what the viewer does while the socket is dry. */
  unsigned long xEvents, xev[STAT_XEV_COUNT];
  double xevTime[STAT_XEV_COUNT];
  double xDispatch;
  double cpuUser, cpuSys;	/* process CPU seconds at the last sample */

  /* timing */
  double decodeTime;
  double lastDecodeMs, maxDecodeMs, minDecodeMs, lastWaitMs;
  double latencyMs, latencyMin, latencyMax, latencySum;
  unsigned long latencyCount;
  double updateLatencyMs;

  /* per update distributions and extremes */
  double minUpdBytes, maxUpdBytes;
  double minInterval, maxInterval, sumInterval;
  unsigned long nInterval;
  double minRects, maxRects;
  unsigned long histRectPix[STAT_NBUCKETS];
  unsigned long histUpdBytes[STAT_NBUCKETS];
  unsigned long histUpdRects[STAT_NBUCKETS];
  unsigned long histInterval[STAT_NBUCKETS];
  unsigned long histLatency[STAT_NBUCKETS];
  unsigned long histDecode[STAT_NBUCKETS];
} VncStats;

extern VncStats vncStats;
extern Bool statsProfiling;	/* per rectangle timing, on once opened */

extern double StatsTime(void);
extern void StatsInit(void);
extern void StatsRectBegin(VncRectProfile *r);
extern void StatsRectEnd(VncRectProfile *r, CARD32 enc, int w, int h);
extern void StatsUpdateStart(void);
extern void StatsUpdateEnd(void);
extern void StatsUpdateRequested(void);
extern void StatsLog(int dir, const char *text, double bytes, double aux);
extern Bool StatsFencePong(int len, char *data);
extern void StatsProcessEvent(XtInputMask mask);
extern void StatsStartup(void);
extern void ShowStats(Widget w, XEvent *ev, String *params,
		      Cardinal *num_params);
extern void HideStats(Widget w, XEvent *ev, String *params,
		      Cardinal *num_params);
extern void ResetStats(Widget w, XEvent *ev, String *params,
		       Cardinal *num_params);
extern void StatsPage(Widget w, XEvent *ev, String *params,
		      Cardinal *num_params);
extern void PauseStats(Widget w, XEvent *ev, String *params,
		       Cardinal *num_params);

#else /* !VNCSTATS - compile the whole package out */

#define STATS(x)
#define StatsTime() 0.0
#define StatsInit()
#define StatsRectBegin(r)
#define StatsRectEnd(r, enc, w, h)
#define StatsUpdateStart()
#define StatsUpdateEnd()
#define StatsUpdateRequested()
#define StatsLog(dir, text, bytes, aux)
#define StatsFencePong(len, data) False
#define StatsProcessEvent(mask) XtAppProcessEvent(appContext, (mask))
#define StatsStartup()

typedef struct { int unused; } VncRectProfile;

#endif /* VNCSTATS */

/* selection.c */

extern void InitialiseSelection();
extern void SelectionToVNC(Widget w, XEvent *event, String *params,
			   Cardinal *num_params);
extern void SelectionFromVNC(Widget w, XEvent *event, String *params,
			     Cardinal *num_params);

/* shm.c */

extern XImage *CreateShmImage();
extern void ShmDetachImage(XImage *img);
extern void ShmCleanup();

/* sockets.c */

extern Bool errorMessageOnReadFailure;

extern void ResetReadBuffer(void);
extern Bool ReadFromRFBServer(char *out, unsigned int n);
extern Bool ReadFromRFBServerPeek(char **ptr, unsigned int max,
				  unsigned int *len);
extern void ReadFromRFBServerSkip(unsigned int n);
extern Bool WriteExact(int sock, char *buf, int n);
extern int FindFreeTcpPort(void);
extern int ListenAtTcpPort(int port);
extern int ConnectToTcpAddr(unsigned int host, int port);
extern int AcceptTcpConnection(int listenSock);
extern Bool SetNonBlocking(int sock);

extern int StringToIPAddr(const char *str, unsigned int *addr);
extern Bool SameMachine(int sock);

/* tunnel.c */

extern Bool tunnelSpecified;

extern Bool createTunnel(int *argc, char **argv, int tunnelArgIndex);

/* vncviewer.c */

extern char *programName;
extern XtAppContext appContext;
extern Display* dpy;
extern Widget toplevel;

extern Bool sessionRestartPending;

extern void ProcessPendingXEvents(void);
extern void ShowSettings(Widget w, XEvent *event, String *params,
			 Cardinal *num_params);
extern void PrintBanner(void);
