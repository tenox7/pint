/*
 * w32compat.h - the X, Xt and POSIX names the shared code uses, for the
 * Win32 build.  vncviewer.h includes it instead of the X headers.
 */

#ifndef W32COMPAT_H
#define W32COMPAT_H

#include <stddef.h>
#include <io.h>

/* vncviewer.c's main(); the real one in w32xt.c sets up and calls it */
#define main W32Main
extern int W32Main(int argc, char **argv);

typedef int Bool;
#define True 1
#define False 0
#define None 0L
#define CurrentTime 0L

typedef char Boolean;
typedef char *String;
typedef unsigned int Cardinal;
typedef unsigned short Dimension;
typedef short Position;
typedef void *XtPointer;
typedef unsigned long XID;
typedef XID Window, Drawable, Cursor, Colormap, Pixmap, KeySym;
typedef unsigned long Atom, Time;
typedef struct _W32Widget *Widget;
typedef struct _W32Display Display;
typedef struct _W32GC *GC;
typedef struct _W32Visual Visual;
typedef struct _W32Image XImage;
typedef struct _W32AppContext *XtAppContext;
typedef unsigned long XtInputId, XtIntervalId, XtInputMask;
typedef union { int type; long pad[24]; } XEvent;
typedef struct { int type; } XErrorEvent;

typedef unsigned char CARD8;
typedef unsigned short CARD16;
typedef unsigned long CARD32;

/* argsresources.c's option and resource tables */
typedef enum {
  XrmoptionNoArg, XrmoptionIsArg, XrmoptionStickyArg, XrmoptionSepArg,
  XrmoptionResArg, XrmoptionSkipArg, XrmoptionSkipLine, XrmoptionSkipNArgs
} XrmOptionKind;

typedef struct {
  char *option;
  char *specifier;
  XrmOptionKind argKind;
  XtPointer value;
} XrmOptionDescRec;

typedef struct {
  String resource_name;
  String resource_class;
  String resource_type;
  Cardinal resource_size;
  Cardinal resource_offset;
  String default_type;
  XtPointer default_addr;
} XtResource;

typedef void (*XtActionProc)(Widget, XEvent *, String *, Cardinal *);
typedef struct { String string; XtActionProc proc; } XtActionsRec;
typedef void (*XtInputCallbackProc)(XtPointer, int *, XtInputId *);

#define XtRBool		"Bool"
#define XtRInt		"Int"
#define XtRString	"String"
#define XtRImmediate	"Immediate"
#define XtNborderWidth	"borderWidth"
#define XtNsensitive	"sensitive"
#define XtIMAll		7L
#define XtInputReadMask	1L
#define XtOffsetOf(type, field)	((Cardinal)offsetof(type, field))
#define XtNumber(arr)	((Cardinal)(sizeof(arr) / sizeof((arr)[0])))

typedef struct {
  unsigned long pixel;
  unsigned short red, green, blue;
  char flags, pad;
} XColor;
#define DoRed		1
#define DoGreen		2
#define DoBlue		4

typedef struct { unsigned long foreground; } XGCValues;
#define GCForeground	4L

/* the X and Xt calls themselves, in w32xt.c */
extern Widget XtVaAppInitialize(XtAppContext *app, String appClass,
				XrmOptionDescRec *options, Cardinal numOptions,
				int *argc, char **argv, String *fallback, ...);
extern Display *XtDisplay(Widget w);
extern Window XtWindow(Widget w);
extern Atom XInternAtom(Display *dpy, const char *name, Bool onlyIfExists);
extern void XtRealizeWidget(Widget w);
extern void XtGetApplicationResources(Widget w, XtPointer base,
				      XtResource *res, Cardinal num,
				      void *args, Cardinal numArgs);
extern void XtAppAddActions(XtAppContext app, XtActionsRec *actions,
			    Cardinal num);
extern XtInputMask XtAppPending(XtAppContext app);
extern void XtAppProcessEvent(XtAppContext app, XtInputMask mask);
extern XtInputId XtAppAddInput(XtAppContext app, int source,
			       XtPointer condition, XtInputCallbackProc proc,
			       XtPointer closure);
extern void XtRemoveInput(XtInputId id);
extern void XtVaSetValues(Widget w, ...);
extern int XBell(Display *dpy, int percent);
extern int XMapRaised(Display *dpy, Window w);
extern int XSync(Display *dpy, Bool discard);
extern int XStoreColor(Display *dpy, Colormap cmap, XColor *color);
extern int XChangeGC(Display *dpy, GC gc, unsigned long mask,
		     XGCValues *values);
extern int XFillRectangle(Display *dpy, Drawable d, GC gc, int x, int y,
			  unsigned int w, unsigned int h);
extern int XCopyArea(Display *dpy, Drawable src, Drawable dst, GC gc,
		     int srcX, int srcY, unsigned int w, unsigned int h,
		     int dstX, int dstY);

/* POSIX */
#define strcasecmp	_stricmp
#define strncasecmp	_strnicmp
#define close(s)	W32CloseSocket(s)
#define strerror(e)	W32StrError(e)
#define getpass(prompt)	DoPasswordDialog()
extern int W32CloseSocket(int s);
extern char *W32StrError(int e);

#endif /* W32COMPAT_H */
