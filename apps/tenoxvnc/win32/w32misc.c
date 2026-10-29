/*
 * w32misc.c - the clipboard, the socket helpers behind w32sock.h, and what
 * is left of misc.c, listen.c and tunnel.c.
 */

#include <windows.h>
#include <winsock.h>
#include "vncviewer.h"
#include "w32vnc.h"

#undef strerror

Bool listenSpecified = False;
int listenPort = 0;
Bool tunnelSpecified = False;

static HWND nextViewer = NULL;
static Bool clipViewerSet = False;
static Bool firstDraw = True;
static Bool clipNew = False;	/* the local clipboard changed since sent */


void
listenForIncomingConnections(int *argc, char **argv, int listenArgIndex)
{
  fprintf(stderr, "-listen is not supported on Windows\n");
  exit(1);
}

Bool
createTunnel(int *argc, char **argv, int tunnelArgIndex)
{
  fprintf(stderr, "%s is not supported on Windows\n", argv[tunnelArgIndex]);
  return False;
}

void
Msleep(int msec)
{
  Sleep(msec);
}

void
Pause(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  Msleep(*num_params == 0 ? 100 : atoi(params[0]));
}

void
RunCommand(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
}

void
Quit(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  w32QuietExit = True;
  Cleanup();
  exit(0);
}

void
Cleanup()
{
  if (clipViewerSet) {
    clipViewerSet = False;
    ChangeClipboardChain(w32Wnd, nextViewer);
  }
}


/*
 * The clipboard.  As with the X selection, what is copied here goes to the
 * server when the viewer is next active, and what is copied there comes
 * here when the viewer is left.  Text only, with Windows line ends here.
 */

static void
ClipboardToVNC(void)
{
  HANDLE h;
  char *p, *text;
  int n = 0;

  clipNew = False;

  if (!OpenClipboard(w32Wnd))
    return;

  if ((h = GetClipboardData(CF_TEXT)) != NULL &&
      (p = GlobalLock(h)) != NULL) {
    text = malloc(strlen(p) + 1);
    if (text) {
      for (; *p; p++)
	if (!(p[0] == '\r' && p[1] == '\n'))
	  text[n++] = *p;
    }
    GlobalUnlock(h);
    CloseClipboard();

    if (text) {
      SendClientCutText(text, n);
      free(text);
    }
    return;
  }

  CloseClipboard();
}

static void
ClipboardFromVNC(void)
{
  HGLOBAL h;
  char *p;
  const char *s;
  size_t n = 1;

  newServerCutText = False;

  if (!serverCutText)
    return;

  for (s = serverCutText; *s; s++)
    n += (*s == '\n') ? 2 : 1;

  if ((h = GlobalAlloc(GMEM_MOVEABLE | GMEM_DDESHARE, n)) == NULL)
    return;
  p = GlobalLock(h);
  for (s = serverCutText; *s; s++) {
    if (*s == '\n' && (s == serverCutText || s[-1] != '\r'))
      *p++ = '\r';
    *p++ = *s;
  }
  *p = '\0';
  GlobalUnlock(h);

  if (OpenClipboard(w32Wnd)) {
    EmptyClipboard();
    SetClipboardData(CF_TEXT, h);
    CloseClipboard();
  } else {
    GlobalFree(h);
  }
}

void
InitialiseSelection()
{
  firstDraw = True;
  nextViewer = SetClipboardViewer(w32Wnd);
  clipViewerSet = True;
}

void
W32DrawClipboard(void)
{
  /* joining the viewer chain sends one at once: what was there before */
  if (firstDraw)
    firstDraw = False;
  else if (GetClipboardOwner() != w32Wnd) {
    clipNew = True;
    if (GetActiveWindow() == w32Wnd)
      ClipboardToVNC();
  }

  if (nextViewer)
    SendMessage(nextViewer, WM_DRAWCLIPBOARD, 0, 0);
}

void
W32ChangeCbChain(WPARAM wParam, LPARAM lParam)
{
  if ((HWND)wParam == nextViewer)
    nextViewer = (HWND)lParam;
  else if (nextViewer)
    SendMessage(nextViewer, WM_CHANGECBCHAIN, wParam, lParam);
}

void
W32Activated(Bool active)
{
  if (active && clipNew)
    ClipboardToVNC();
  else if (!active && newServerCutText)
    ClipboardFromVNC();
}

/* the F8 menu's way of moving it whether it is new or not */

void
SelectionToVNC(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  if (clipNew || (*num_params == 1 && strcmp(params[0], "always") == 0))
    ClipboardToVNC();
}

void
SelectionFromVNC(Widget w, XEvent *event, String *params,
		 Cardinal *num_params)
{
  if (newServerCutText ||
      (*num_params == 1 && strcmp(params[0], "always") == 0))
    ClipboardFromVNC();
}


/*
 * Winsock under the BSD names, for sockets.c by way of w32sock.h.
 */

int
W32SockError(int result)
{
  if (result == SOCKET_ERROR)
    errno = WSAGetLastError();
  return result;
}

int
W32Connect(int s, const struct sockaddr *addr, int len)
{
  struct sockaddr_in sin;

  if (len != sizeof(sin))
    return connect((SOCKET)s, addr, len);

  /* Unix takes no host to mean this machine; Winsock does not */
  memcpy(&sin, addr, sizeof(sin));
  if (sin.sin_addr.s_addr == 0)
    sin.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  return connect((SOCKET)s, (struct sockaddr *)&sin, len);
}

int
W32SetNonBlocking(int s)
{
  u_long one = 1;

  return W32SockError(ioctlsocket((SOCKET)s, FIONBIO, &one));
}

int
W32CloseSocket(int s)
{
  return closesocket((SOCKET)s);
}

char *
W32StrError(int e)
{
  static const struct {
    int code;
    char *text;
  } wsa[] = {
    { WSAEINTR, "Interrupted system call" },
    { WSAEWOULDBLOCK, "Operation would block" },
    { WSAENOTSOCK, "Not a socket" },
    { WSAEADDRINUSE, "Address already in use" },
    { WSAEADDRNOTAVAIL, "Cannot assign requested address" },
    { WSAENETDOWN, "Network is down" },
    { WSAENETUNREACH, "Network is unreachable" },
    { WSAECONNABORTED, "Software caused connection abort" },
    { WSAECONNRESET, "Connection reset by peer" },
    { WSAENOBUFS, "No buffer space available" },
    { WSAENOTCONN, "Socket is not connected" },
    { WSAETIMEDOUT, "Connection timed out" },
    { WSAECONNREFUSED, "Connection refused" },
    { WSAEHOSTDOWN, "Host is down" },
    { WSAEHOSTUNREACH, "No route to host" },
    { WSANOTINITIALISED, "Winsock is not started" }
  };
  static char text[32];
  int i;

  for (i = 0; i < (int)XtNumber(wsa); i++)
    if (wsa[i].code == e)
      return wsa[i].text;

  if (e >= WSABASEERR) {
    sprintf(text, "Winsock error %d", e);
    return text;
  }

  return strerror(e);
}
