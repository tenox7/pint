/*
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
 * misc.c - miscellaneous functions.
 */

#include <vncviewer.h>
#include <signal.h>
#include <fcntl.h>
#include <stdarg.h>
#ifdef __VMS
#include <lib$routines.h>	/* lib$wait - see Msleep below */
#else
#include <sys/wait.h>
#endif

static void CleanupSignalHandler(int sig);
static int CleanupXErrorHandler(Display *dpy, XErrorEvent *error);
static int CleanupXIOErrorHandler(Display *dpy);
#ifndef _X_NORETURN
#define _X_NORETURN
#endif
static void CleanupXtErrorHandler(String message) _X_NORETURN;
static Bool IconifyNamedWindow(Window w, char *name, Bool undo);

char connErrorMsg[CONN_ERROR_LEN];

/*
 * ConnError prints a message the way the viewer always has and keeps a copy
 * of it, so that anything that goes wrong while a connection is being made
 * can be put back up in the connection dialog rather than only on a console
 * nobody is looking at.  The copy is truncated to what the dialog can show.
 */

void
ConnError(const char *format, ...)
{
  char msg[1024];
  va_list args;
  int i;

  va_start(args, format);
  vsprintf(msg, format, args);
  va_end(args);

  fprintf(stderr, "%s\n", msg);

  strncpy(connErrorMsg, msg, CONN_ERROR_LEN - 1);
  connErrorMsg[CONN_ERROR_LEN - 1] = '\0';

  /* Part of the text can come from the server, and the dialog draws it as a
     single line of plain ASCII. */
  for (i = 0; connErrorMsg[i]; i++)
    if ((unsigned char)connErrorMsg[i] < 0x20 || (unsigned char)connErrorMsg[i] >= 0x7F)
      connErrorMsg[i] = ' ';
}


/*
 * Overflow-checked size_t arithmetic for server-derived allocation sizes.
 */

Bool
RfbMulSize(size_t a, size_t b, size_t c, size_t *result)
{
  size_t ab;

  if (result == NULL)
    return False;

  if (a == 0 || b == 0 || c == 0) {
    *result = 0;
    return True;
  }

  if (b != 0 && a > ((size_t)-1) / b)
    return False;
  ab = a * b;
  if (c != 0 && ab > ((size_t)-1) / c)
    return False;
  *result = ab * c;
  return True;
}

Bool
RfbCheckAddSize(size_t base, size_t extra, size_t *result)
{
  if (result == NULL)
    return False;
  if (base > ((size_t)-1) - extra)
    return False;
  *result = base + extra;
  return True;
}

Bool
RfbValidServerStringLength(CARD32 len, size_t extra)
{
  size_t total;

  if (len > RFB_MAX_STRING_LENGTH)
    return False;
  return RfbCheckAddSize((size_t)len, extra, &total);
}

#ifndef _WIN32		/* the rest is X only; win32/ has its own */

Dimension dpyWidth, dpyHeight;
Atom wmDeleteWindow, wmState;

static Bool xloginIconified = False;
static XErrorHandler defaultXErrorHandler;
static XIOErrorHandler defaultXIOErrorHandler;
static XtErrorHandler defaultXtErrorHandler;


/*
 * ToplevelInitBeforeRealization sets the title, geometry and other resources
 * on the toplevel window.
 */

void
ToplevelInitBeforeRealization()
{
  char *geometry;

  UpdateWindowTitle();

  XtVaSetValues(toplevel, XtNmaxWidth, si.framebufferWidth,
		XtNmaxHeight, si.framebufferHeight, NULL);

  dpyWidth = WidthOfScreen(DefaultScreenOfDisplay(dpy));
  dpyHeight = HeightOfScreen(DefaultScreenOfDisplay(dpy));

  if (appData.fullScreen) {

    /* full screen - set position to 0,0, but defer size calculation until
       widgets are realized */

    XtVaSetValues(toplevel, XtNoverrideRedirect, True,
		  XtNgeometry, "+0+0", NULL);

  } else {

    /* not full screen - work out geometry for middle of screen unless
       specified by user */

    XtVaGetValues(toplevel, XtNgeometry, &geometry, NULL);

    if (geometry == NULL) {
      Dimension toplevelX, toplevelY;
      Dimension toplevelWidth = si.framebufferWidth;
      Dimension toplevelHeight = si.framebufferHeight;

      if ((toplevelWidth + appData.wmDecorationWidth) >= dpyWidth)
	toplevelWidth = dpyWidth - appData.wmDecorationWidth;

      if ((toplevelHeight + appData.wmDecorationHeight) >= dpyHeight)
	toplevelHeight = dpyHeight - appData.wmDecorationHeight;

      toplevelX = (dpyWidth - toplevelWidth - appData.wmDecorationWidth) / 2;

      toplevelY = (dpyHeight - toplevelHeight - appData.wmDecorationHeight) /2;

      /* set position via "geometry" so that window manager thinks it's a
	 user-specified position and therefore honours it */

      geometry = XtMalloc(256);

      sprintf(geometry, "%dx%d+%d+%d",
	      toplevelWidth, toplevelHeight, toplevelX, toplevelY);
      XtVaSetValues(toplevel, XtNgeometry, geometry, NULL);
    }
  }

  /* Test if the keyboard is grabbed.  If so, it's probably because the
     XDM login window is up, so try iconifying it to release the grab */

  if (XGrabKeyboard(dpy, DefaultRootWindow(dpy), False, GrabModeSync,
		    GrabModeSync, CurrentTime) == GrabSuccess) {
    XUngrabKeyboard(dpy, CurrentTime);
  } else {
    wmState = XInternAtom(dpy, "WM_STATE", False);

    if (IconifyNamedWindow(DefaultRootWindow(dpy), "xlogin", False)) {
      xloginIconified = True;
      XSync(dpy, False);
      sleep(1);
    }
  }

  /* Set handlers for signals and X errors to perform cleanup */

  signal(SIGHUP, CleanupSignalHandler);
  signal(SIGINT, CleanupSignalHandler);
  signal(SIGTERM, CleanupSignalHandler);
  defaultXErrorHandler = XSetErrorHandler(CleanupXErrorHandler);
  defaultXIOErrorHandler = XSetIOErrorHandler(CleanupXIOErrorHandler);
  defaultXtErrorHandler = XtAppSetErrorHandler(appContext,
					       CleanupXtErrorHandler);
}


/*
 * UpdateWindowTitle sets the window and icon titles from the current
 * desktopName plus brief session info: encoding, color mode, protocol
 * version and view-only state.  Called again whenever any of it changes
 * (encodings negotiated, DesktopName updates).  Falls back to "TenoxVNC"
 * if the server did not supply a desktop name.
 */

void
UpdateWindowTitle(void)
{
  char info[96];
  char *title, *name;

  sprintf(info, "%s%s%s 3.%d%s%s",
	  titleEncName[0] ? titleEncName : "", titleEncName[0] ? " " : "",
	  ColorModeName(), protocolMinorVersion, tightVncProtocol ? "t" : "",
	  appData.viewOnly ? " ro" : "");

  name = (desktopName && desktopName[0]) ? desktopName : "TenoxVNC";

  title = XtMalloc(strlen(name) + strlen(info) + 32);
  sprintf(title, "%s (%s) [F8 Menu]", name, info);
  XtVaSetValues(toplevel, XtNtitle, title,
		XtNiconName, name, NULL);
  XtFree(title);
}


/*
 * ToplevelInitAfterRealization initialises things which require the X windows
 * to exist.  It goes into full-screen mode if appropriate, and tells the
 * window manager we accept the "delete window" message.
 */

void
ToplevelInitAfterRealization()
{
  if (appData.fullScreen) {
    FullScreenOn();
  }

  XSetWMProtocols(dpy, XtWindow(toplevel), &wmDeleteWindow, 1);
  XtOverrideTranslations
      (toplevel, XtParseTranslationTable ("<Message>WM_PROTOCOLS: Quit()"));
}


/*
 * TimeFromEvent() gets the time field out of the given event.  It returns
 * CurrentTime if the event has no time field.
 */

Time
TimeFromEvent(XEvent *ev)
{
  switch (ev->type) {
  case KeyPress:
  case KeyRelease:
    return ev->xkey.time;
  case ButtonPress:
  case ButtonRelease:
    return ev->xbutton.time;
  case MotionNotify:
    return ev->xmotion.time;
  case EnterNotify:
  case LeaveNotify:
    return ev->xcrossing.time;
  case PropertyNotify:
    return ev->xproperty.time;
  case SelectionClear:
    return ev->xselectionclear.time;
  case SelectionRequest:
    return ev->xselectionrequest.time;
  case SelectionNotify:
    return ev->xselection.time;
  default:
    return CurrentTime;
  }
}


/*
 * Pause is an action which pauses for a number of milliseconds (100 by
 * default).  It is sometimes useful to space out "fake" pointer events
 * generated by SendRFBEvent.
 */

void
Pause(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  int msec;

  if (*num_params == 0) {
    msec = 100;
  } else {
    msec = atoi(params[0]);
  }

  Msleep(msec);
}


/*
 * Millisecond sleep via select(); usleep() does not exist on older
 * systems such as HP-UX 9.
 */

void
Msleep(int msec)
{
#ifdef __VMS
  /* VMS has no usleep, and its select() ignores a timeout given with no
     descriptors to watch - it returns 0 immediately with errno set. */
  float seconds = (float)msec / 1000.0;

  lib$wait(&seconds);
#else
  struct timeval tv;

  tv.tv_sec = msec / 1000;
  tv.tv_usec = (msec % 1000) * 1000;
  select(0, NULL, NULL, NULL, &tv);
#endif
}


#ifdef __VMS

/*
 * Run an arbitrary command.  VMS has no fork()/execvp(), so hand the
 * arguments to DCL as one command line.
 */
void
RunCommand(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  char cmd[1024];
  Cardinal i;
  size_t len = 0;

  if (*num_params == 0)
    return;

  for (i = 0; i < *num_params; i++) {
    size_t n = strlen(params[i]);

    if (len + n + 2 > sizeof(cmd))
      break;
    if (len)
      cmd[len++] = ' ';
    memcpy(cmd + len, params[i], n);
    len += n;
  }
  cmd[len] = '\0';

  if (system(cmd) != 0)
    fprintf(stderr, "%s: RunCommand failed: %s\n", programName, cmd);
}

#else /* !__VMS */

/*
 * Run an arbitrary command via execvp()
 */
void
RunCommand(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  int childstatus;

  if (*num_params == 0)
    return;

  if (fcntl (ConnectionNumber (dpy), F_SETFD, 1L) == -1)
      fprintf(stderr, "warning: file descriptor %d unusable for spawned program", ConnectionNumber(dpy));
  
  if (fcntl (rfbsock, F_SETFD, 1L) == -1)
      fprintf(stderr, "warning: file descriptor %d unusable for spawned program", rfbsock);

  switch (fork()) {
  case -1: 
    perror("fork"); 
    break;
  case 0:
      /* Child 1. Fork again. */
      switch (fork()) {
      case -1:
	  perror("fork");
	  break;

      case 0:
	  /* Child 2. Do some work. */
	  execvp(params[0], params);
	  perror("exec");
	  exit(1);
	  break;  

      default:
	  break;
      }

      /* Child 1. Exit, and let init adopt our child */
      exit(0);

  default:
    break;
  }

  /* Wait for Child 1 to die */
  wait(&childstatus);
  
  return;
}

#endif /* !__VMS */


/*
 * Quit action - called when we get a "delete window" message.
 */

void
Quit(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  Cleanup();
  exit(0);
}


/*
 * Cleanup - perform any cleanup operations prior to exiting.
 */

void
Cleanup()
{
  if (xloginIconified) {
    IconifyNamedWindow(DefaultRootWindow(dpy), "xlogin", True);
    XFlush(dpy);
  }
#ifdef MITSHM
  if (appData.useShm)
    ShmCleanup();
#endif
}

static int
CleanupXErrorHandler(Display *dpy, XErrorEvent *error)
{
  fprintf(stderr,"CleanupXErrorHandler called\n");
  Cleanup();
  return (*defaultXErrorHandler)(dpy, error);
}

static int
CleanupXIOErrorHandler(Display *dpy)
{
  fprintf(stderr,"CleanupXIOErrorHandler called\n");
  Cleanup();
  return (*defaultXIOErrorHandler)(dpy);
}

static void
CleanupXtErrorHandler(String message)
{
  fprintf(stderr,"CleanupXtErrorHandler called\n");
  Cleanup();
  (*defaultXtErrorHandler)(message);
  exit(1);
}

static void
CleanupSignalHandler(int sig)
{
  fprintf(stderr,"CleanupSignalHandler called\n");
  Cleanup();
  exit(1);
}


/*
 * IconifyNamedWindow iconifies another client's window with the given name.
 */

static Bool
IconifyNamedWindow(Window w, char *name, Bool undo)
{
  Window *children, dummy;
  unsigned int nchildren;
  int i;
  char *window_name;
  Atom type = None;
  int format;
  unsigned long nitems, after;
  unsigned char *data;

  if (XFetchName(dpy, w, &window_name)) {
    if (strcmp(window_name, name) == 0) {
      if (undo) {
	XMapWindow(dpy, w);
      } else {
	XIconifyWindow(dpy, w, DefaultScreen(dpy));
      }
      XFree(window_name);
      return True;
    }
    XFree(window_name);
  }

  XGetWindowProperty(dpy, w, wmState, 0, 0, False,
		     AnyPropertyType, &type, &format, &nitems,
		     &after, &data);
  if (type != None) {
    XFree(data);
    return False;
  }

  if (!XQueryTree(dpy, w, &dummy, &dummy, &children, &nchildren))
    return False;

  for (i = 0; i < (int) nchildren; i++) {
    if (IconifyNamedWindow(children[i], name, undo)) {
      XFree ((char *)children);
      return True;
    }
  }
  if (children) XFree ((char *)children);
  return False;
}

#endif /* !_WIN32 */
