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
 * vncviewer.c - the Xt-based VNC viewer.
 */

#include "vncviewer.h"

char *programName;
XtAppContext appContext;
Display* dpy;

Widget toplevel;

/* Set by the settings panel when what it was given needs a new connection to
   take effect.  It is only ever acted on from the main loop below: the panel
   runs several frames deep inside HandleRFBServerMessage, which is no place
   to drop the socket the decoders are reading from. */
Bool sessionRestartPending = False;

static Bool ConnectSession(Bool allowDialog);
static Bool RestartSession(void);
static void ApplySettings(const AppData *old);

/*
 * ProcessPendingXEvents - handle queued X events, due timers etc. without
 * blocking.  The classic viewer only processes X events when the socket
 * read would block; with TigerVNC continuous updates the socket may never
 * run dry on busy screens, so the message loop must yield to X explicitly
 * or user input starves.
 */

void
ProcessPendingXEvents(void)
{
  while (XtAppPending(appContext))
    StatsProcessEvent(XtIMAll);
}

/*
 * PrintBanner - what and which version we are, above whatever else the run
 * puts on the console.  Only printed when the viewer was told what to do on
 * the command line: started bare it is driven from the connect dialog, and
 * there is generally no console to print to.
 */

void
PrintBanner(void)
{
  fprintf(stderr, "TenoxVNC %s\n", TENOXVNC_VERSION);
}

/*
 * ConnectSession dials the server and runs the RFB handshake.
 *
 * Anything that goes wrong - an unknown host, a refused or timed out
 * connection, something that is not a VNC server, a rejected password - is
 * worth a second chance when there is a dialog to show it in: it goes back up
 * with the message above the fields, and the retry redials from scratch,
 * since the server has dropped us by then.  Started from the command line
 * there is nobody to show a dialog to, so a failure is fatal as it was.
 */

static Bool
ConnectSession(Bool allowDialog)
{
  for (;;) {
    connErrorMsg[0] = '\0';
    authFailed = False;

    /* The handshake below dispatches X events too, so the settings panel can
       ask for a restart in the middle of one.  The connection being made is
       already the restart it wants, and leaving the flag set would abort this
       handshake as well - and every one after it. */
    sessionRestartPending = False;

    if (ConnectToRFBServer(vncServerHost, vncServerPort)) {
      if (InitialiseRFBConnection())
	return True;
      close(rfbsock);
      rfbsock = -1;
    }

    if (!allowDialog)
      return False;

    if (authFailed) {
      ForgetPassword();
      AskForServer("Authentication failed - try again.");
      continue;
    }

    AskForServer(connErrorMsg[0] ? connErrorMsg : "Unable to connect.");
  }
}


/*
 * RestartSession drops the connection and makes a new one, which is how the
 * settings that are fixed at connect time - the shared flag and the pixel
 * format behind the color level - are changed from the F8 menu.
 *
 * The window stays where it is.  Everything that was on it is lost, because
 * it is in the old pixel format and its colors are old colormap cells, so the
 * new connection is asked for the whole screen once it is up.
 */

static Bool
RestartSession(void)
{
  ForgetRemoteCursor();

  if (rfbsock >= 0) {
    close(rfbsock);
    rfbsock = -1;
  }

  if (!ConnectSession(True))
    return False;

  /* Free the old colormap cells and take the format the new level asks for
     before anything is decoded in it. */
  ReloadColorFormat();

  RebuildDesktopFramebuffer();

  /* SetFormatAndEncodings ends by putting the new desktop name, encoding and
     color mode in the title. */
  if (!SetFormatAndEncodings())
    return False;

  return SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
				      si.framebufferHeight, False);
}


/*
 * ApplySettings works out what the settings panel changed and does the least
 * that will make it so.  The encoding list may be re-sent at any point in the
 * stream, and the rest of it is ours alone; only the pixel format and the
 * shared flag need the connection made again.
 */

static void
ApplySettings(const AppData *old)
{
  if (appData.colorLevel != old->colorLevel ||
      appData.shareDesktop != old->shareDesktop) {
    sessionRestartPending = True;
    return;
  }

  if (appData.preferredEncoding != old->preferredEncoding ||
      appData.compressLevel != old->compressLevel ||
      appData.qualityLevel != old->qualityLevel ||
      appData.enableJPEG != old->enableJPEG)
    SendEncodings();

  if (appData.viewOnly != old->viewOnly)
    UpdateWindowTitle();

  if (appData.useContinuousUpdates != cuActive && supportsCU)
    ToggleContinuousUpdates(NULL, NULL, NULL, NULL);

  /* Turning remote resize on asks the server for the size the window already
     is, the way learning the server supports it does. */
  if (appData.useRemoteResize && !old->useRemoteResize &&
      supportsSetDesktopSize)
    DesktopSizeSupportLearned();
}


/*
 * ShowSettings is the action behind the F8 menu's "Settings..." entry.  The
 * panel writes straight into appData as it is used, so the old values are
 * kept to put back if it is cancelled, and to compare against if it is not.
 */

void
ShowSettings(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  AppData old;

  old = appData;

  if (!DoSettingsDialog()) {
    appData = old;
    return;
  }

  ApplySettings(&old);
}

int
main(int argc, char **argv)
{
  int i;
  programName = argv[0];

  /* Handle -h/-help/--help and -v/-version before any Xt initialisation so
     that printing them does not require an X display. */

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "-help") == 0 ||
	strcmp(argv[i], "--help") == 0)
      usage();
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "-version") == 0 ||
	strcmp(argv[i], "--version") == 0) {
      PrintBanner();
      exit(0);
    }
  }

  /* The -listen option is used to make us a daemon process which listens for
     incoming connections from servers, rather than actively connecting to a
     given server. The -tunnel and -via options are useful to create
     connections tunneled via SSH port forwarding. We must test for the
     -listen option before invoking any Xt functions - this is because we use
     forking, and Xt doesn't seem to cope with forking very well. For -listen
     option, when a successful incoming connection has been accepted,
     listenForIncomingConnections() returns, setting the listenSpecified
     flag. */

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-listen") == 0) {
      PrintBanner();
      listenForIncomingConnections(&argc, argv, i);
      break;
    }
    if (strcmp(argv[i], "-tunnel") == 0 || strcmp(argv[i], "-via") == 0) {
      if (!createTunnel(&argc, argv, i))
	exit(1);
      break;
    }
  }

  /* Call the main Xt initialisation function.  It parses command-line options,
     generating appropriate resource specs, and makes a connection to the X
     display. */

  toplevel = XtVaAppInitialize(&appContext, "Vncviewer",
			       cmdLineOptions, numCmdLineOptions,
			       &argc, argv, fallback_resources,
			       XtNborderWidth, 0, NULL);

  dpy = XtDisplay(toplevel);

  /* The connection dialog below puts up a window of its own long before the
     toplevel is realized, and it needs this to tell the window manager what
     its close button means. */

  wmDeleteWindow = XInternAtom(dpy, "WM_DELETE_WINDOW", False);

  /* Interpret resource specs and process any remaining command-line arguments
     (i.e. the VNC server name).  If the server name isn't specified on the
     command line, getArgsAndResources() will pop up a dialog box and wait
     for one to be entered. */

  GetArgsAndResources(argc, argv);

  /* Unless we accepted an incoming connection, make a TCP connection to the
     given VNC server, and initialise the VNC connection, which includes
     reading the password.  A failure is only worth retrying in the dialog
     when the dialog is what we were driven from - see ConnectSession. */

  if (listenSpecified) {
    if (!InitialiseRFBConnection()) exit(1);
  } else if (!ConnectSession(connectDialogUsed)) {
    exit(1);
  }

  /* Create the "popup" widget - this won't actually appear on the screen until
     some user-defined event causes the "ShowPopup" action to be invoked */

  CreatePopup();

  /* Find the best pixel format and X visual/colormap to use */

  SetVisualAndCmap();

  /* Create the "desktop" widget, and perform initialisation which needs doing
     before the widgets are realized */

  ToplevelInitBeforeRealization();

  DesktopInitBeforeRealization();

  /* "Realize" all the widgets, i.e. actually create and map their X windows */

  XtRealizeWidget(toplevel);

  /* Perform initialisation that needs doing after realization, now that the X
     windows exist */

  InitialiseSelection();

  ToplevelInitAfterRealization();

  DesktopInitAfterRealization();

  /* Watch for window resizes so we can ask the server to resize its
     framebuffer to match (TigerVNC SetDesktopSize) */

  TrackDesktopResizes();

  /* Tell the VNC server which pixel format and encodings we want to use */

  SetFormatAndEncodings();

  /* -stats: the diagnostics window, without going through the F8 menu */

  StatsStartup();

  /* Ask for the first screenful.  Every later request is the incremental one
     the update handler sends; Expose repaints from the local image and no
     longer asks the server for anything. */

  SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
			       si.framebufferHeight, False);

  /* Now enter the main loop, processing VNC messages.  X events will
     automatically be processed whenever the VNC connection is idle. */

  while (1) {
    ProcessPendingXEvents();

    /* The settings panel runs from the X events HandleRFBServerMessage
       dispatches while it waits for the server, and asks for a new
       connection by making that read give up.  So a read that failed is
       only a real failure when no restart is waiting behind it - this is
       the one place in the viewer where the socket may be dropped. */

    if (!sessionRestartPending && !HandleRFBServerMessage() &&
	!sessionRestartPending)
      break;

    if (sessionRestartPending && !RestartSession())
      break;
  }

  Cleanup();

  return 0;
}
