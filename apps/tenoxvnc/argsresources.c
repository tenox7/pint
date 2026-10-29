/*
 *  Copyright (C) 2002-2006 Constantin Kaplinsky.  All Rights Reserved.
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
 * argsresources.c - deal with command-line args and resources.
 */

#include "vncviewer.h"

/*
 * A remote cursor shape with no more than two colors can be handed to the X
 * server, which then draws it for free.  Build with HWCURSOR=false to have
 * that start off; the F8 menu switches it either way at any time.
 */

#ifdef NO_HW_CURSOR
#define HW_CURSOR_DEFAULT False
#else
#define HW_CURSOR_DEFAULT True
#endif

/*
 * fallback_resources - these are used if there is no app-defaults file
 * installed in one of the standard places.
 */

char *fallback_resources[] = {

  "Vncviewer.translations:\
    <Enter>: SelectionToVNC()\\n\
    <Leave>: SelectionFromVNC()",

  /* the border around a desktop smaller than the window */
  "*form.background: black",

  "*desktop.baseTranslations:\
     <Key>F8: ShowPopup()\\n\
     <ButtonPress>: SendRFBEvent()\\n\
     <ButtonRelease>: SendRFBEvent()\\n\
     <Motion>: SendRFBEvent()\\n\
     <KeyPress>: SendRFBEvent()\\n\
     <KeyRelease>: SendRFBEvent()",

  /* what the window manager close button does to the F8 menu */
  "*popup.translations: #override <Message>WM_PROTOCOLS: HidePopup()",

  /* and to the connection dialog, where it means Cancel */
  "*connectDialog.translations: #override <Message>WM_PROTOCOLS: CancelDialog()",

#ifdef VNCSTATS
  "*statsShell.title: TenoxVNC diagnostics",
  "*statsShell.iconName: TenoxVNC diagnostics",
  "*statsShell.translations: #override\\n\
     <Message>WM_PROTOCOLS: HideStats()\\n\
     <Key>q: HideStats()\\n\
     <Key>Escape: HideStats()\\n\
     <Key>r: ResetStats()\\n\
     <Key>p: PauseStats()\\n\
     <Key>Tab: StatsPage()\\n\
     <Key>1: StatsPage(1)\\n\
     <Key>2: StatsPage(2)\\n\
     <Key>3: StatsPage(3)\\n\
     <Key>4: StatsPage(4)\\n\
     <Key>5: StatsPage(5)",
  "*statsCanvas.translations: #override\\n\
     <Key>q: HideStats()\\n\
     <Key>Escape: HideStats()\\n\
     <Key>r: ResetStats()\\n\
     <Key>p: PauseStats()\\n\
     <Key>Tab: StatsPage()\\n\
     <Key>1: StatsPage(1)\\n\
     <Key>2: StatsPage(2)\\n\
     <Key>3: StatsPage(3)\\n\
     <Key>4: StatsPage(4)\\n\
     <Key>5: StatsPage(5)",
#endif

  NULL
};


/*
 * vncServerHost and vncServerPort are set either from the command line or
 * from a dialog box.
 */

char vncServerHost[256];
int vncServerPort = 0;


/*
 * appData is our application-specific data which can be set by the user with
 * application resource specs.  The AppData structure is defined in the header
 * file.
 */

AppData appData;

static XtResource appDataResourceList[] = {
  {"shareDesktop", "ShareDesktop", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, shareDesktop), XtRImmediate, (XtPointer) True},

  {"viewOnly", "ViewOnly", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, viewOnly), XtRImmediate, (XtPointer) False},

  {"fullScreen", "FullScreen", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, fullScreen), XtRImmediate, (XtPointer) False},

  {"raiseOnBeep", "RaiseOnBeep", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, raiseOnBeep), XtRImmediate, (XtPointer) True},

  {"passwordFile", "PasswordFile", XtRString, sizeof(String),
   XtOffsetOf(AppData, passwordFile), XtRImmediate, (XtPointer) 0},

  {"passwordDialog", "PasswordDialog", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, passwordDialog), XtRImmediate, (XtPointer) False},

  {"encodings", "Encodings", XtRString, sizeof(String),
   XtOffsetOf(AppData, encodingsString), XtRImmediate, (XtPointer) 0},

  {"preferredEncoding", "PreferredEncoding", XtRString, sizeof(String),
   XtOffsetOf(AppData, preferredEncodingString), XtRImmediate, (XtPointer) 0},

  {"colorLevel", "ColorLevel", XtRString, sizeof(String),
   XtOffsetOf(AppData, colorLevelString), XtRImmediate, (XtPointer) 0},

  {"nColors", "NColors", XtRInt, sizeof(int),
   XtOffsetOf(AppData, nColors), XtRImmediate, (XtPointer) 256},

  {"useSharedColors", "UseSharedColors", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useSharedColors), XtRImmediate, (XtPointer) True},

  {"forceOwnCmap", "ForceOwnCmap", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, forceOwnCmap), XtRImmediate, (XtPointer) False},

  {"forceTrueColor", "ForceTrueColor", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, forceTrueColor), XtRImmediate, (XtPointer) False},

  {"requestedDepth", "RequestedDepth", XtRInt, sizeof(int),
   XtOffsetOf(AppData, requestedDepth), XtRImmediate, (XtPointer) 0},

  {"useSharedMemory", "UseSharedMemory", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useShm), XtRImmediate, (XtPointer) True},

  {"wmDecorationWidth", "WmDecorationWidth", XtRInt, sizeof(int),
   XtOffsetOf(AppData, wmDecorationWidth), XtRImmediate, (XtPointer) 4},

  {"wmDecorationHeight", "WmDecorationHeight", XtRInt, sizeof(int),
   XtOffsetOf(AppData, wmDecorationHeight), XtRImmediate, (XtPointer) 24},

  {"popupButtonCount", "PopupButtonCount", XtRInt, sizeof(int),
   XtOffsetOf(AppData, popupButtonCount), XtRImmediate, (XtPointer) 0},

  {"debug", "Debug", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, debug), XtRImmediate, (XtPointer) False},

  {"rawDelay", "RawDelay", XtRInt, sizeof(int),
   XtOffsetOf(AppData, rawDelay), XtRImmediate, (XtPointer) 0},

  {"copyRectDelay", "CopyRectDelay", XtRInt, sizeof(int),
   XtOffsetOf(AppData, copyRectDelay), XtRImmediate, (XtPointer) 0},

  {"bumpScrollTime", "BumpScrollTime", XtRInt, sizeof(int),
   XtOffsetOf(AppData, bumpScrollTime), XtRImmediate, (XtPointer) 25},

  {"bumpScrollPixels", "BumpScrollPixels", XtRInt, sizeof(int),
   XtOffsetOf(AppData, bumpScrollPixels), XtRImmediate, (XtPointer) 20},

  {"compressLevel", "CompressionLevel", XtRInt, sizeof(int),
   XtOffsetOf(AppData, compressLevel), XtRImmediate, (XtPointer) -1},

  {"qualityLevel", "QualityLevel", XtRInt, sizeof(int),
   XtOffsetOf(AppData, qualityLevel), XtRImmediate, (XtPointer) 6},

  {"enableJPEG", "EnableJPEG", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, enableJPEG), XtRImmediate, (XtPointer) True},

  {"forceJPEG", "ForceJPEG", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, forceJPEG), XtRImmediate, (XtPointer) False},

  {"useRemoteCursor", "UseRemoteCursor", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useRemoteCursor), XtRImmediate, (XtPointer) True},

  {"useX11Cursor", "UseX11Cursor", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useX11Cursor), XtRImmediate, (XtPointer) False},

  {"useHardwareCursor", "UseHardwareCursor", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useHwCursor), XtRImmediate,
   (XtPointer) HW_CURSOR_DEFAULT},

  {"localCursor", "LocalCursor", XtRString, sizeof(String),
   XtOffsetOf(AppData, localCursor), XtRImmediate, (XtPointer) "dot"},

  {"grabKeyboard", "GrabKeyboard", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, grabKeyboard), XtRImmediate, (XtPointer) False},

  {"autoPass", "AutoPass", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, autoPass), XtRImmediate, (XtPointer) False},

  {"remoteResize", "RemoteResize", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useRemoteResize), XtRImmediate, (XtPointer) True},

  {"continuousUpdates", "ContinuousUpdates", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, useContinuousUpdates), XtRImmediate, (XtPointer) True}

#ifdef VNCSTATS
  ,
  {"showStats", "ShowStats", XtRBool, sizeof(Bool),
   XtOffsetOf(AppData, showStats), XtRImmediate, (XtPointer) False}
#endif
};


/*
 * The cmdLineOptions array specifies how certain app resource specs can be set
 * with command-line options.
 */

XrmOptionDescRec cmdLineOptions[] = {
  {"-shared",        "*shareDesktop",       XrmoptionNoArg,  "True"},
  {"-noshared",      "*shareDesktop",       XrmoptionNoArg,  "False"},
  {"-viewonly",      "*viewOnly",           XrmoptionNoArg,  "True"},
  {"-fullscreen",    "*fullScreen",         XrmoptionNoArg,  "True"},
  {"-noraiseonbeep", "*raiseOnBeep",        XrmoptionNoArg,  "False"},
  {"-passwd",        "*passwordFile",       XrmoptionSepArg, 0},
  {"-encodings",     "*encodings",          XrmoptionSepArg, 0},
  {"-preferredencoding", "*preferredEncoding", XrmoptionSepArg, 0},
  {"-colorlevel",    "*colorLevel",         XrmoptionSepArg, 0},
  {"-bgr233",        "*colorLevel",         XrmoptionNoArg,  "medium"},
  {"-owncmap",       "*forceOwnCmap",       XrmoptionNoArg,  "True"},
  {"-truecolor",     "*forceTrueColor",     XrmoptionNoArg,  "True"},
  {"-depth",         "*requestedDepth",     XrmoptionSepArg, 0},
  {"-compresslevel", "*compressLevel",      XrmoptionSepArg, 0},
  {"-quality",       "*qualityLevel",       XrmoptionSepArg, 0},
  {"-nojpeg",        "*enableJPEG",         XrmoptionNoArg,  "False"},
  {"-forcejpeg",     "*forceJPEG",          XrmoptionNoArg,  "True"},
  {"-nocursorshape", "*useRemoteCursor",    XrmoptionNoArg,  "False"},
  {"-x11cursor",     "*useX11Cursor",       XrmoptionNoArg,  "True"},
  {"-hwcursor",      "*useHardwareCursor",  XrmoptionNoArg,  "True"},
  {"-nohwcursor",    "*useHardwareCursor",  XrmoptionNoArg,  "False"},
  {"-cursor",        "*localCursor",        XrmoptionSepArg, 0},
  {"-autopass",      "*autoPass",           XrmoptionNoArg,  "True"},
  {"-remoteresize",  "*remoteResize",       XrmoptionNoArg,  "True"},
  {"-noremoteresize","*remoteResize",       XrmoptionNoArg,  "False"},
  {"-nocontinuous",  "*continuousUpdates",  XrmoptionNoArg,  "False"}
#ifdef VNCSTATS
  ,
  {"-stats",         "*showStats",          XrmoptionNoArg,  "True"}
#endif

};

int numCmdLineOptions = XtNumber(cmdLineOptions);


/*
 * actions[] specifies actions that can be used in widget resource specs.
 */

static XtActionsRec actions[] = {
    {"SendRFBEvent", SendRFBEvent},
    {"ShowPopup", ShowPopup},
    {"HidePopup", HidePopup},
    {"ToggleFullScreen", ToggleFullScreen},
    {"SetFullScreenState", SetFullScreenState},
    {"ToggleContinuousUpdates", ToggleContinuousUpdates},
    {"SetContinuousUpdatesState", SetContinuousUpdatesState},
    {"RepaintScreen", RepaintScreen},
    {"CycleLocalCursor", CycleLocalCursor},
    {"ToggleHardwareCursor", ToggleHardwareCursor},
    {"SetLocalCursorState", SetLocalCursorState},
    {"ShowSettings", ShowSettings},
#ifdef VNCSTATS
    {"ShowStats", ShowStats},
    {"HideStats", HideStats},
    {"ResetStats", ResetStats},
    {"PauseStats", PauseStats},
    {"StatsPage", StatsPage},
#endif
    {"SelectionFromVNC", SelectionFromVNC},
    {"SelectionToVNC", SelectionToVNC},
    {"CancelDialog", CancelDialog},
    {"Pause", Pause},
    {"RunCommand", RunCommand},
    {"Quit", Quit},
};


/*
 * removeArgs() is used to remove some of command line arguments.
 */

void
removeArgs(int *argc, char** argv, int idx, int nargs)
{
  int i;
  if ((idx+nargs) > *argc) return;
  for (i = idx+nargs; i < *argc; i++) {
    argv[i-nargs] = argv[i];
  }
  *argc -= nargs;
}

/*
 * The preferred encoding and the color level are given by name, so that
 * -preferredencoding zrle and -colorlevel low read the same way the
 * connection dialog does.  Both end up as the numbers the rest of the viewer
 * works in; an unknown name is a warning rather than a failure, since it only
 * means we go on choosing for ourselves.
 */

static const char *colorLevelNames[] = { "full", "medium", "low", "verylow" };

static int
ParsePreferredEncoding(const char *s)
{
  static const CARD32 encs[] = {
    rfbEncodingTight, rfbEncodingZRLE, rfbEncodingHextile,
    rfbEncodingZlib, rfbEncodingCoRRE, rfbEncodingRRE, rfbEncodingRaw
  };
  int i;

  if (!s || strcasecmp(s, "auto") == 0)
    return -1;

  for (i = 0; i < (int)XtNumber(encs); i++)
    if (strcasecmp(s, EncodingName(encs[i])) == 0)
      return (int)encs[i];

  fprintf(stderr, "%s: unknown preferred encoding \"%s\"\n", programName, s);
  return -1;
}

static int
ParseColorLevel(const char *s)
{
  int i;

  if (!s)
    return COLOR_DEFAULT;

  for (i = 0; i < (int)XtNumber(colorLevelNames); i++)
    if (strcasecmp(s, colorLevelNames[i]) == 0)
      return i;

  fprintf(stderr, "%s: unknown color level \"%s\"\n", programName, s);
  return COLOR_DEFAULT;
}


/*
 * usage() prints out the usage message.
 */

void
usage(void)
{
  fprintf(stderr,
	  "TenoxVNC Viewer (TightVNC 1.3.10 + TigerVNC backports)\n"
	  "\n"
	  "Usage: %s [<OPTIONS>] [<HOST>][:<DISPLAY#>]\n"
	  "       %s [<OPTIONS>] [<HOST>][::<PORT#>]\n"
	  "       %s [<OPTIONS>] -listen [<DISPLAY#>]\n"
	  "       %s -h|-help|--help\n"
	  "\n"
	  "<OPTIONS> are standard Xt options, or:\n"
	  "        -via <GATEWAY>\n"
	  "        -shared (set by default)\n"
	  "        -noshared\n"
	  "        -viewonly\n"
	  "        -fullscreen\n"
	  "        -noraiseonbeep\n"
	  "        -passwd <PASSWD-FILENAME> (standard VNC authentication)\n"
	  "        -encodings <ENCODING-LIST> (e.g. \"tight copyrect\")\n"
	  "        -preferredencoding auto|tight|zrle|hextile|zlib|corre|rre|raw\n"
	  "        -colorlevel full|medium|low|verylow (medium = -bgr233,\n"
	  "                     set by default)\n"
	  "        -bgr233\n"
	  "        -owncmap\n"
	  "        -truecolor\n"
	  "        -depth <DEPTH>\n"
	  "        -compresslevel <COMPRESS-VALUE> (0..9: 0-fast, 9-best)\n"
	  "        -quality <JPEG-QUALITY-VALUE> (0..9: 0-low, 9-high)\n"
	  "        -nojpeg\n"
	  "        -forcejpeg (grayscale; TigerVNC servers only)\n"
	  "        -nocursorshape\n"
	  "        -x11cursor\n"
	  "        -hwcursor | -nohwcursor (X server draws 2-color remote cursors)\n"
	  "        -cursor dot|arrow|none (local cursor shown over the desktop)\n"
	  "        -autopass\n"
	  "        -noremoteresize (don't resize remote desktop to fit window)\n"
	  "        -nocontinuous (don't use continuous updates)\n"
#ifdef VNCSTATS
	  "        -stats (open the diagnostics window at startup)\n"
#endif
	  "\n"
	  "If the VNC_PASSWORD environment variable is set, its value is used\n"
	  "as the password for standard VNC authentication.\n"
	  "\n"
	  "Option names may be abbreviated, e.g. -bgr instead of -bgr233.\n"
	  "See the manual page for more information."
	  "\n", programName, programName, programName, programName);
  exit(1);
}


/*
 * GetArgsAndResources() deals with resources and any command-line arguments
 * not already processed by XtVaAppInitialize().  It sets vncServerHost and
 * vncServerPort and all the fields in appData.
 */

void
GetArgsAndResources(int argc, char **argv)
{
  char *vncServerName = NULL;

  /* Turn app resource specs into our appData structure for the rest of the
     program to use */

  XtGetApplicationResources(toplevel, &appData, appDataResourceList,
			    XtNumber(appDataResourceList), 0, 0);

  appData.preferredEncoding =
    ParsePreferredEncoding(appData.preferredEncodingString);
  appData.colorLevel = ParseColorLevel(appData.colorLevelString);

  /* Add our actions to the actions table so they can be used in widget
     resource specs */

  XtAppAddActions(appContext, actions, XtNumber(actions));

  /* Check any remaining command-line arguments.  If -listen was specified
     there should be none.  Otherwise the only argument should be the VNC
     server name.  If not given then pop up a dialog box and wait for the
     server name to be entered. */

  if (listenSpecified) {
    if (argc != 1) {
      fprintf(stderr,"\n%s -listen: invalid command line argument: %s\n",
	      programName, argv[1]);
      usage();
    }
    return;
  }

  if (argc == 1) {
    appData.passwordDialog = True;
    AskForServer(NULL);
    return;
  }

  if (argc != 2)
    usage();

  vncServerName = argv[1];
  PrintBanner();

  if (!isatty(0))
    appData.passwordDialog = True;
  if (vncServerName[0] == '-')
    usage();

  if (!SetServerName(vncServerName))
    usage();
}


/*
 * SetServerName splits "host", "host:display" or "host::port" into
 * vncServerHost and vncServerPort.  Split out of GetArgsAndResources so that
 * the connection dialog can set a different server when an attempt is
 * retried; it returns False rather than quitting, so that what was typed
 * into the dialog can be corrected there.
 */

Bool
SetServerName(char *vncServerName)
{
  char *colonPos;
  int len, portOffset;
  int disp;

  if (strlen(vncServerName) > 255) {
    ConnError("VNC server name too long");
    return False;
  }

  colonPos = strchr(vncServerName, ':');
  if (colonPos == NULL) {
    /* No colon -- use default port number */
    strcpy(vncServerHost, vncServerName);
    vncServerPort = SERVER_PORT_OFFSET;
  } else {
    memcpy(vncServerHost, vncServerName, colonPos - vncServerName);
    vncServerHost[colonPos - vncServerName] = '\0';
    len = strlen(colonPos + 1);
    portOffset = SERVER_PORT_OFFSET;
    if (colonPos[1] == ':') {
      /* Two colons -- interpret as a port number */
      colonPos++;
      len--;
      portOffset = 0;
    }
    if (!len || (int) strspn(colonPos + 1, "0123456789") != len) {
      ConnError("Server must be host, host:display or host::port");
      return False;
    }
    disp = atoi(colonPos + 1);
    if (portOffset != 0 && disp >= 100)
      portOffset = 0;
    vncServerPort = disp + portOffset;
  }

  return True;
}
