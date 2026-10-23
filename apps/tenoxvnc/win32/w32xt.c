/*
 * w32xt.c - the X and Xt calls the shared code makes, done with Win32: the
 * command line options, the resources, the event loop and the socket input.
 * Also the real main(), which keeps stderr for a message box at exit.
 */

#include <windows.h>
#include <winsock.h>
#include <fcntl.h>
#include "vncviewer.h"
#include "w32vnc.h"

#undef main

HINSTANCE w32Inst;
Bool w32QuietExit = False;

struct _W32Widget { int unused; };
struct _W32Display { int unused; };
struct _W32AppContext { int unused; };
static struct _W32Widget toplevelRec;
static struct _W32Display displayRec;
static struct _W32AppContext appRec;

/* resources set on the command line, by name without the leading '*' */
#define MAX_CMD_RES 64
static struct { char *name; char *value; } cmdRes[MAX_CMD_RES];
static int nCmdRes = 0;

/* the one input source: the server socket */
static XtInputCallbackProc inputProc = NULL;
static XtPointer inputClosure;
static int inputSock = -1;

/* stderr goes to a temp file, shown at exit unless quitting was asked for */
#define LOG_LINES_EARLY	60	/* before the window is up: usage and the like */
#define LOG_LINES_LATE	3	/* after: what ended the session */
static char logPath[MAX_PATH];
static Bool windowShown = False;


/*
 * Command line options, as XtVaAppInitialize parses them: an option may be
 * cut down to any prefix that is still unique.
 */

static XrmOptionDescRec *
FindOption(XrmOptionDescRec *opts, Cardinal n, const char *arg)
{
  XrmOptionDescRec *found = NULL;
  size_t len = strlen(arg);
  Cardinal i;

  for (i = 0; i < n; i++)
    if (strcmp(opts[i].option, arg) == 0)
      return &opts[i];

  for (i = 0; i < n; i++) {
    if (strncmp(opts[i].option, arg, len) == 0) {
      if (found)
	return NULL;		/* ambiguous */
      found = &opts[i];
    }
  }

  return found;
}

static void
AddCmdRes(char *specifier, char *value)
{
  if (nCmdRes == MAX_CMD_RES)
    return;

  if (*specifier == '*' || *specifier == '.')
    specifier++;
  cmdRes[nCmdRes].name = specifier;
  cmdRes[nCmdRes].value = value;
  nCmdRes++;
}

static char *
CmdRes(const char *name)
{
  int i;

  for (i = nCmdRes - 1; i >= 0; i--)	/* the last one given wins */
    if (strcmp(cmdRes[i].name, name) == 0)
      return cmdRes[i].value;

  return NULL;
}

Widget
XtVaAppInitialize(XtAppContext *app, String appClass,
		  XrmOptionDescRec *opts, Cardinal numOpts,
		  int *argc, char **argv, String *fallback, ...)
{
  int i = 1, j;

  while (i < *argc) {
    XrmOptionDescRec *o;
    int used = 1;

    if (strcmp(argv[i], "-geometry") == 0 && i + 1 < *argc) {
      W32SetGeometry(argv[i + 1]);
      used = 2;
    } else if (argv[i][0] == '-' &&
	       (o = FindOption(opts, numOpts, argv[i])) != NULL) {
      if (o->argKind == XrmoptionSepArg) {
	if (i + 1 >= *argc) {
	  i++;			/* left for usage() */
	  continue;
	}
	AddCmdRes(o->specifier, argv[i + 1]);
	used = 2;
      } else {
	AddCmdRes(o->specifier, (char *)o->value);
      }
    } else {
      i++;
      continue;
    }

    for (j = i; j + used < *argc; j++)
      argv[j] = argv[j + used];
    *argc -= used;
    argv[*argc] = NULL;
  }

  *app = &appRec;

  /* the socket needs a window for its messages before there is a desktop */
  W32CreateWindow();

  return &toplevelRec;
}

void
XtGetApplicationResources(Widget w, XtPointer base, XtResource *res,
			  Cardinal num, void *args, Cardinal numArgs)
{
  Cardinal i;

  for (i = 0; i < num; i++) {
    char *field = (char *)base + res[i].resource_offset;
    char *value = CmdRes(res[i].resource_name);

    if (strcmp(res[i].resource_type, XtRString) == 0) {
      char *s = value ? value : (char *)res[i].default_addr;

      memcpy(field, &s, sizeof(s));
    } else {
      int n;

      if (!value)
	n = (int)(long)res[i].default_addr;
      else if (strcmp(res[i].resource_type, XtRBool) == 0)
	n = (_stricmp(value, "true") == 0 || _stricmp(value, "on") == 0 ||
	     _stricmp(value, "yes") == 0 || strcmp(value, "1") == 0);
      else
	n = atoi(value);
      memcpy(field, &n, sizeof(n));
    }
  }
}

void
XtAppAddActions(XtAppContext app, XtActionsRec *actions, Cardinal num)
{
}


/*
 * The event loop.  Dispatching is all the viewer ever does while it waits
 * for the server, the same as under Xt.  Keys for the viewer window are not
 * translated: w32key.c runs the keyboard layout itself.
 */

XtInputMask
XtAppPending(XtAppContext app)
{
  MSG msg;

  return PeekMessage(&msg, NULL, 0, 0, PM_NOREMOVE) ? XtIMAll : 0;
}

void
XtAppProcessEvent(XtAppContext app, XtInputMask mask)
{
  MSG msg;

  if (!GetMessage(&msg, NULL, 0, 0)) {
    w32QuietExit = True;
    Cleanup();
    exit(0);
  }

  if (msg.hwnd != w32Wnd || msg.message < WM_KEYFIRST ||
      msg.message > WM_KEYLAST)
    TranslateMessage(&msg);
  DispatchMessage(&msg);
}

XtInputId
XtAppAddInput(XtAppContext app, int source, XtPointer condition,
	      XtInputCallbackProc proc, XtPointer closure)
{
  inputProc = proc;
  inputClosure = closure;
  inputSock = source;

  /* again every time: a socket may reuse a closed one's number */
  WSAAsyncSelect((SOCKET)source, w32Wnd, WM_RFBSOCKET, FD_READ | FD_CLOSE);
  return 1;
}

void
XtRemoveInput(XtInputId id)
{
  inputProc = NULL;
}

void
W32SocketEvent(WPARAM wParam, LPARAM lParam)
{
  XtInputCallbackProc proc = inputProc;
  int fd = inputSock;
  XtInputId id = 1;

  if (proc && (int)wParam == inputSock)
    proc(inputClosure, &fd, &id);
}


/*
 * The rest of what the shared code asks of X.  There is one window, so the
 * widgets, windows and the display are all just tokens.
 */

Display *
XtDisplay(Widget w)
{
  return &displayRec;
}

Window
XtWindow(Widget w)
{
  return 1;
}

Atom
XInternAtom(Display *dpy, const char *name, Bool onlyIfExists)
{
  return 1;
}

void
XtRealizeWidget(Widget w)
{
  W32ShowWindow();
  windowShown = True;
}

void
XtVaSetValues(Widget w, ...)
{
}

int
XBell(Display *dpy, int percent)
{
  MessageBeep(MB_OK);
  return 0;
}

int
XMapRaised(Display *dpy, Window w)
{
  if (IsIconic(w32Wnd))
    ShowWindow(w32Wnd, SW_RESTORE);
  BringWindowToTop(w32Wnd);
  return 0;
}

int
XSync(Display *dpy, Bool discard)
{
  GdiFlush();
  return 0;
}

int
XStoreColor(Display *dpy, Colormap cmap, XColor *color)
{
  return 0;			/* we never ask for a colormapped format */
}

int
XChangeGC(Display *dpy, GC gc, unsigned long mask, XGCValues *values)
{
  return 0;
}


/*
 * stderr has nowhere to go from a GUI program, so it goes to a temp file,
 * and what it says is put up when the viewer exits on its own: usage, a
 * failed connection, a dropped one.
 */

static void
OpenLog(void)
{
  char dir[MAX_PATH];

  if (GetTempPath(sizeof(dir), dir) == 0 ||
      GetTempFileName(dir, "vnc", 0, logPath) == 0) {
    logPath[0] = '\0';
    return;
  }

  if (!freopen(logPath, "w+", stderr)) {
    DeleteFile(logPath);
    logPath[0] = '\0';
    return;
  }
  setvbuf(stderr, NULL, _IONBF, 0);
}

static void
ShowLog(void)
{
  static char text[4096];
  char buf[2048];
  long end, start;
  int n, i, lines, maxLines;
  char *p, *q;

  if (!logPath[0])
    return;

  if (w32Wnd) {
    DestroyWindow(w32Wnd);	/* nothing is drawn or sent from here on */
    w32Wnd = NULL;
  }

  fflush(stderr);
  end = ftell(stderr);

  if (!w32QuietExit && end > 0) {
    start = end > (long)sizeof(buf) - 1 ? end - (long)sizeof(buf) + 1 : 0;
    fseek(stderr, start, SEEK_SET);
    n = (int)fread(buf, 1, sizeof(buf) - 1, stderr);
    buf[n > 0 ? n : 0] = '\0';

    /* keep the last few lines, dropping the one cut in half at the front */
    maxLines = windowShown ? LOG_LINES_LATE : LOG_LINES_EARLY;
    i = (int)strlen(buf);
    while (i > 0 && buf[i - 1] == '\n')
      buf[--i] = '\0';
    for (lines = 0; i > 0; i--)
      if (buf[i - 1] == '\n' && ++lines == maxLines)
	break;
    if (i == 0 && start > 0 && (p = strchr(buf, '\n')) != NULL)
      i = (int)(p - buf) + 1;

    for (p = buf + i, q = text; *p && q < text + sizeof(text) - 2; p++) {
      if (*p == '\n')
	*q++ = '\r';
      *q++ = *p;
    }
    *q = '\0';

    if (text[0])
      MessageBox(NULL, text, "TenoxVNC", MB_OK | MB_ICONINFORMATION);
  }

  fclose(stderr);
  DeleteFile(logPath);
}


/*
 * The real main.  vncviewer.c's own main is W32Main, which it is once the
 * C library, Winsock and the log are ready.
 */

int
main(int argc, char **argv)
{
  WSADATA wsa;
  char *name;

  w32Inst = GetModuleHandle(NULL);

  /* vncauth.c reads the password file with "r", which would be text mode */
  _fmode = _O_BINARY;

  OpenLog();
  atexit(ShowLog);

  if (WSAStartup(MAKEWORD(1, 1), &wsa) != 0) {
    fprintf(stderr, "TenoxVNC needs Winsock 1.1\n");
    exit(1);
  }

  /* messages and usage name the program, not the path it was run from */
  name = strrchr(argv[0], '\\');
  name = name ? name + 1 : argv[0];
  if (strchr(name, '.'))
    *strchr(name, '.') = '\0';
  argv[0] = name;

  return W32Main(argc, argv);
}
