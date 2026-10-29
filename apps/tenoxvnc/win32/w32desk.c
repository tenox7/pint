/*
 * w32desk.c - the viewer window: the local copy of the remote desktop,
 * painting, scrolling, full screen, the pointer and the F8 menu.  The Win32
 * side of desktop.c, scroll.c, fullscreen.c and popup.c.
 */

#include <windows.h>
#include "vncviewer.h"
#include "w32vnc.h"

#ifndef WM_MOUSEWHEEL
#define WM_MOUSEWHEEL	0x020A	/* NT 4.0 sends it, NT 3.5 never does */
#endif
#ifndef SPI_GETWORKAREA
#define SPI_GETWORKAREA	48	/* NT 4.0; NT 3.5 just says no */
#endif

#define XK_Delete	0xffff
#define XK_F8		0xffc5
#define XK_Control_L	0xffe3
#define XK_Alt_L	0xffe9

#define WINDOW_STYLE	(WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL)
#define TIMER_RESIZE	1
#define TIMER_BUMP	2

enum {
  M_FULLSCREEN = 100, M_CONTINUOUS, M_REPAINT, M_REFRESH, M_CURSOR,
  M_HWCURSOR, M_CLIP_OUT, M_CLIP_IN, M_CTRLALTDEL, M_SENDF8, M_SETTINGS,
  M_DISMISS, M_QUIT
};

GC gc = (GC)1;
GC srcGC = (GC)2, dstGC = (GC)3;	/* the CopyRect debugging flashes */
Window desktopWin = 1;
Cursor dotCursor;
Widget form, viewport, desktop, popup;
Dimension dpyWidth, dpyHeight;
Atom wmDeleteWindow;

HWND w32Wnd = NULL;

/* the image is a bottom up DIB section, so its rows run backwards */
static HBITMAP image = NULL, oldBitmap;
static HDC imageDC = NULL;
static char *imageBits = NULL;
static int imageStride, imageHeight;
#define ROW(y)	(imageBits + (imageHeight - 1 - (y)) * imageStride)

static int scrollX = 0, scrollY = 0;	/* image pixel at the top left */
static int visW = 0, visH = 0;		/* how much of the image shows */
static Bool hBar = False, vBar = False;
static Bool barsAllowed = True;

static Bool trackResizes = False;
static int wantResizeW, wantResizeH;

static WINDOWPLACEMENT savedPlacement;	/* from before full screen */
static int buttonMask = 0, lastX = 0, lastY = 0;
static int bumpDX, bumpDY;
static Bool bumpTimer = False;
static char geometry[64] = "";

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam,
				LPARAM lParam);
static void Layout(void);


/*
 * The window, made hidden at start-up and shown once there is a desktop.
 */

void
W32CreateWindow(void)
{
  WNDCLASS wc;

  memset(&wc, 0, sizeof(wc));
  wc.lpfnWndProc = WndProc;
  wc.hInstance = w32Inst;
  wc.hIcon = LoadIcon(w32Inst, MAKEINTRESOURCE(IDI_TENOXVNC));
  if (!wc.hIcon)
    wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
  wc.lpszClassName = "TenoxVNC";
  RegisterClass(&wc);

  dpyWidth = (Dimension)GetSystemMetrics(SM_CXSCREEN);
  dpyHeight = (Dimension)GetSystemMetrics(SM_CYSCREEN);

  w32Wnd = CreateWindow("TenoxVNC", "TenoxVNC", WINDOW_STYLE,
			CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
			NULL, NULL, w32Inst, NULL);
  if (!w32Wnd) {
    fprintf(stderr, "Cannot create the viewer window\n");
    exit(1);
  }
}

void
W32ShowWindow(void)
{
  ShowWindow(w32Wnd, SW_SHOWNORMAL);
  UpdateWindow(w32Wnd);
}

void
W32SetGeometry(const char *spec)
{
  strncpy(geometry, spec, sizeof(geometry) - 1);
}

/* -geometry WxH+X+Y: the size of the desktop area, where the window goes */
static void
ApplyGeometry(RECT *win)
{
  const char *p = geometry;
  int w, h, x, y;
  char sx, sy;
  RECT r;

  if (sscanf(p, "%dx%d", &w, &h) == 2) {
    SetRect(&r, 0, 0, w, h);
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    win->right = win->left + r.right - r.left;
    win->bottom = win->top + r.bottom - r.top;
    p += strspn(p, "0123456789x");
  }

  if (sscanf(p, "%c%d%c%d", &sx, &x, &sy, &y) == 4 &&
      (sx == '+' || sx == '-') && (sy == '+' || sy == '-')) {
    w = win->right - win->left;
    h = win->bottom - win->top;
    win->left = (sx == '+') ? x : dpyWidth - x - w;
    win->top = (sy == '+') ? y : dpyHeight - y - h;
    win->right = win->left + w;
    win->bottom = win->top + h;
  }
}

/* the window size for the whole desktop, as far as the screen allows */
static void
SizeWindow(Bool place)
{
  RECT r, work;
  int w, h;

  if (!SystemParametersInfo(SPI_GETWORKAREA, 0, &work, 0))
    SetRect(&work, 0, 0, dpyWidth, dpyHeight);

  SetRect(&r, 0, 0, si.framebufferWidth, si.framebufferHeight);
  AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
  w = r.right - r.left;
  h = r.bottom - r.top;
  if (w > work.right - work.left)
    w = work.right - work.left;
  if (h > work.bottom - work.top)
    h = work.bottom - work.top;

  if (!place) {
    if (!IsZoomed(w32Wnd))
      SetWindowPos(w32Wnd, NULL, 0, 0, w, h,
		   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    return;
  }

  r.left = work.left + (work.right - work.left - w) / 2;
  r.top = work.top + (work.bottom - work.top - h) / 2;
  r.right = r.left + w;
  r.bottom = r.top + h;
  if (geometry[0])
    ApplyGeometry(&r);

  SetWindowPos(w32Wnd, NULL, r.left, r.top, r.right - r.left,
	       r.bottom - r.top, SWP_NOZORDER | SWP_NOACTIVATE);
}

void
ToplevelInitBeforeRealization(void)
{
  UpdateWindowTitle();
  SizeWindow(True);
}

void
ToplevelInitAfterRealization(void)
{
  if (appData.fullScreen)
    FullScreenOn();
}

void
UpdateWindowTitle(void)
{
  char info[96], title[400];
  char *name;

  sprintf(info, "%s%s%s 3.%d%s%s",
	  titleEncName[0] ? titleEncName : "", titleEncName[0] ? " " : "",
	  ColorModeName(), protocolMinorVersion, tightVncProtocol ? "t" : "",
	  appData.viewOnly ? " ro" : "");

  name = (desktopName && desktopName[0]) ? desktopName : "TenoxVNC";

  sprintf(title, "%.280s (%s) [F8 Menu]", name, info);
  SetWindowText(w32Wnd, title);
}


/*
 * The local image.  The pixels are in the format asked of the server, so
 * the decoders write straight into it.
 */

static void
FreeDesktopImage(void)
{
  if (imageDC) {
    SelectObject(imageDC, oldBitmap);
    DeleteDC(imageDC);
    imageDC = NULL;
  }
  if (image) {
    DeleteObject(image);
    image = NULL;
  }
  imageBits = NULL;
}

static void
CreateDesktopImage(void)
{
  struct {
    BITMAPINFOHEADER h;
    RGBQUAD colors[256];
  } bmi;
  int w = si.framebufferWidth > 0 ? si.framebufferWidth : 1;
  int h = si.framebufferHeight > 0 ? si.framebufferHeight : 1;
  HDC screen;

  FreeDesktopImage();

  memset(&bmi, 0, sizeof(bmi));
  bmi.h.biSize = sizeof(BITMAPINFOHEADER);
  bmi.h.biWidth = w;
  bmi.h.biHeight = h;
  bmi.h.biPlanes = 1;
  bmi.h.biBitCount = (WORD)visbpp;
  bmi.h.biCompression = BI_RGB;

  if (visbpp == 8) {
    W32ColorTable(bmi.colors);
  } else if (visbpp == 16) {
    DWORD *masks = (DWORD *)bmi.colors;

    bmi.h.biCompression = BI_BITFIELDS;
    masks[0] = 0xf800;
    masks[1] = 0x07e0;
    masks[2] = 0x001f;
  }

  screen = GetDC(NULL);
  image = CreateDIBSection(screen, (BITMAPINFO *)&bmi, DIB_RGB_COLORS,
			   (VOID **)&imageBits, NULL, 0);
  imageDC = CreateCompatibleDC(screen);
  ReleaseDC(NULL, screen);

  if (!image || !imageDC || !imageBits) {
    fprintf(stderr, "Cannot make a %dx%d image of the desktop\n", w, h);
    exit(1);
  }
  oldBitmap = SelectObject(imageDC, image);

  imageStride = ((w * visbpp + 31) / 32) * 4;
  imageHeight = h;
  memset(imageBits, 0, (size_t)imageStride * h);
}

void
DesktopInitBeforeRealization(void)
{
  CreateDesktopImage();
}

void
DesktopInitAfterRealization(void)
{
  W32CursorInit();
  ScrollInit();
}

void
ResizeDesktopFramebuffer(int width, int height)
{
  if (width == si.framebufferWidth && height == si.framebufferHeight)
    return;

  fprintf(stderr, "Framebuffer size changed to %dx%d\n", width, height);

  SoftCursorLockArea(0, 0, si.framebufferWidth, si.framebufferHeight);

  si.framebufferWidth = width;
  si.framebufferHeight = height;

  RebuildDesktopFramebuffer();

  SoftCursorUnlockScreen();
}

void
RebuildDesktopFramebuffer(void)
{
  CreateDesktopImage();

  if (!appData.fullScreen)
    SizeWindow(False);
  Layout();
  InvalidateRect(w32Wnd, NULL, FALSE);
}

/* the window with no scrollbars: what the remote desktop should be */
static void
DesktopArea(int *w, int *h)
{
  RECT rc;

  GetClientRect(w32Wnd, &rc);
  *w = rc.right + (vBar ? GetSystemMetrics(SM_CXVSCROLL) : 0);
  *h = rc.bottom + (hBar ? GetSystemMetrics(SM_CYHSCROLL) : 0);
}

static void
ScheduleRemoteResize(int width, int height)
{
  wantResizeW = width;
  wantResizeH = height;

  if (!trackResizes || !supportsSetDesktopSize || !appData.useRemoteResize ||
      appData.viewOnly)
    return;

  if (width == si.framebufferWidth && height == si.framebufferHeight)
    return;

  SetTimer(w32Wnd, TIMER_RESIZE, 250, NULL);	/* again: one for the lot */
}

static void
RemoteResizeTimer(void)
{
  KillTimer(w32Wnd, TIMER_RESIZE);

  if (!supportsSetDesktopSize || !appData.useRemoteResize || appData.viewOnly)
    return;

  if (pendingDesktopResize) {
    SetTimer(w32Wnd, TIMER_RESIZE, 100, NULL);	/* one still in flight */
    return;
  }

  if (wantResizeW == si.framebufferWidth &&
      wantResizeH == si.framebufferHeight)
    return;

  pendingDesktopResize = True;
  SendSetDesktopSize(wantResizeW, wantResizeH);
}

void
TrackDesktopResizes(void)
{
  trackResizes = True;
}

void
DesktopSizeSupportLearned(void)
{
  int w, h;

  if (!appData.useRemoteResize || appData.viewOnly || appData.fullScreen)
    return;

  DesktopArea(&w, &h);
  ScheduleRemoteResize(w, h);
}


/*
 * The image functions the decoders use, as in desktop.c.
 */

static Bool
ClipToImage(int *x, int *y, int *width, int *height)
{
  if (*width <= 0 || *height <= 0)
    return False;

  if (*x < 0) { *width += *x; *x = 0; }
  if (*y < 0) { *height += *y; *y = 0; }
  if (*x + *width > si.framebufferWidth)
    *width = si.framebufferWidth - *x;
  if (*y + *height > si.framebufferHeight)
    *height = si.framebufferHeight - *y;

  return (*width > 0 && *height > 0);
}

/* invert a rectangle on the screen, for -rawdelay and -copyrectdelay */
static void
DebugFlash(int x, int y, int width, int height)
{
  HDC dc = GetDC(w32Wnd);

  PatBlt(dc, x - scrollX, y - scrollY, width, height, DSTINVERT);
  ReleaseDC(w32Wnd, dc);
  GdiFlush();
}

void
PutImageRect(int x, int y, int width, int height)
{
  HDC dc;

  if (!imageDC || width <= 0 || height <= 0)
    return;

  /* only what shows; the rest is painted when it scrolls into view */
  if (x < scrollX) { width -= scrollX - x; x = scrollX; }
  if (y < scrollY) { height -= scrollY - y; y = scrollY; }
  if (x + width > scrollX + visW) width = scrollX + visW - x;
  if (y + height > scrollY + visH) height = scrollY + visH - y;
  if (width <= 0 || height <= 0)
    return;

  dc = GetDC(w32Wnd);
  W32SelectPalette(dc);
  BitBlt(dc, x - scrollX, y - scrollY, width, height, imageDC, x, y, SRCCOPY);
  ReleaseDC(w32Wnd, dc);
}

void
SyncShmPuts(void)
{
  GdiFlush();			/* nothing left reading the image */
}

void
CopyDataToImage(char *buf, int x, int y, int width, int height)
{
  int bpp = visbpp / 8;
  int rowBytes = width * bpp;
  int q;

  if (!imageBits)
    return;

  if (appData.rawDelay != 0) {
    DebugFlash(x, y, width, height);
    Msleep(appData.rawDelay);
  }

  for (q = 0; q < height; q++, buf += rowBytes)
    memcpy(ROW(y + q) + x * bpp, buf, (size_t)rowBytes);
}

void
CopyDataToScreen(char *buf, int x, int y, int width, int height)
{
  CopyDataToImage(buf, x, y, width, height);
  PutImageRect(x, y, width, height);
}

void
FillImageRect(int x, int y, int width, int height, unsigned long pixel)
{
  int q, p;

  if (!imageBits || !ClipToImage(&x, &y, &width, &height))
    return;

  for (q = y; q < y + height; q++) {
    char *row = ROW(q);

    switch (visbpp) {

    case 8:
      memset(row + x, (int)(pixel & 0xFF), (size_t)width);
      break;

    case 16: {
      CARD16 *d = (CARD16 *)row + x;
      CARD16 v = (CARD16)pixel;

      for (p = width; p >= 8; p -= 8) {
	d[0] = v; d[1] = v; d[2] = v; d[3] = v;
	d[4] = v; d[5] = v; d[6] = v; d[7] = v;
	d += 8;
      }
      while (p-- > 0)
	*d++ = v;
      break;
    }

    case 32: {
      CARD32 *d = (CARD32 *)row + x;
      CARD32 v = (CARD32)pixel;

      for (p = width; p >= 8; p -= 8) {
	d[0] = v; d[1] = v; d[2] = v; d[3] = v;
	d[4] = v; d[5] = v; d[6] = v; d[7] = v;
	d += 8;
      }
      while (p-- > 0)
	*d++ = v;
      break;
    }
    }
  }
}

void
CopyImageRect(int srcX, int srcY, int width, int height, int dstX, int dstY)
{
  int bpp = visbpp / 8;
  size_t bytes;
  int q;

  if (!imageBits || width <= 0 || height <= 0)
    return;

  /* clip both ends of the move, keeping them aligned */
  if (srcX < 0) { width += srcX; dstX -= srcX; srcX = 0; }
  if (srcY < 0) { height += srcY; dstY -= srcY; srcY = 0; }
  if (dstX < 0) { width += dstX; srcX -= dstX; dstX = 0; }
  if (dstY < 0) { height += dstY; srcY -= dstY; dstY = 0; }

  if (srcX + width > si.framebufferWidth)
    width = si.framebufferWidth - srcX;
  if (dstX + width > si.framebufferWidth)
    width = si.framebufferWidth - dstX;
  if (srcY + height > si.framebufferHeight)
    height = si.framebufferHeight - srcY;
  if (dstY + height > si.framebufferHeight)
    height = si.framebufferHeight - dstY;

  if (width <= 0 || height <= 0)
    return;

  bytes = (size_t)width * bpp;

  /* a move downward has to start at the bottom */
  if (dstY > srcY) {
    for (q = height - 1; q >= 0; q--)
      memmove(ROW(dstY + q) + dstX * bpp, ROW(srcY + q) + srcX * bpp, bytes);
  } else {
    for (q = 0; q < height; q++)
      memmove(ROW(dstY + q) + dstX * bpp, ROW(srcY + q) + srcX * bpp, bytes);
  }
}

/* rows run upwards in memory, hence the stride below zero */
char *
ImageRow(int x, int y)
{
  if (!imageBits)
    return NULL;

  return ROW(y) + x * (visbpp / 8);
}

int
ImageStride(void)
{
  return imageBits ? -imageStride : 0;
}

void
RepaintScreen(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  SoftCursorLockArea(0, 0, si.framebufferWidth, si.framebufferHeight);

  if (imageBits)
    memset(imageBits, 0, (size_t)imageStride * imageHeight);
  PutImageRect(0, 0, si.framebufferWidth, si.framebufferHeight);

  SoftCursorUnlockScreen();

  SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
			       si.framebufferHeight, False);
}

/* rfbproto.c and tight.c draw on the window, which shows the image */

int
XFillRectangle(Display *dpy, Drawable d, GC g, int x, int y,
	       unsigned int w, unsigned int h)
{
  if (g == srcGC || g == dstGC)
    DebugFlash(x, y, (int)w, (int)h);
  else
    PutImageRect(x, y, (int)w, (int)h);
  return 0;
}

int
XCopyArea(Display *dpy, Drawable src, Drawable dst, GC g, int srcX, int srcY,
	  unsigned int w, unsigned int h, int dstX, int dstY)
{
  PutImageRect(dstX, dstY, (int)w, (int)h);	/* CopyImageRect moved it */
  return 0;
}


/*
 * Scrolling, for a desktop bigger than the window.  The scrollbars only
 * show when they are needed, and never in full screen.
 */

static void
ClampScroll(int *x, int *y)
{
  if (*x > si.framebufferWidth - visW)
    *x = si.framebufferWidth - visW;
  if (*y > si.framebufferHeight - visH)
    *y = si.framebufferHeight - visH;
  if (*x < 0)
    *x = 0;
  if (*y < 0)
    *y = 0;
}

static void
Layout(void)
{
  static Bool busy = False;
  int cx = GetSystemMetrics(SM_CXVSCROLL);
  int cy = GetSystemMetrics(SM_CYHSCROLL);
  Bool wantH = False, wantV = False;
  int w, h;

  if (busy || !w32Wnd)
    return;
  busy = True;			/* the bars coming and going resize us */

  DesktopArea(&w, &h);

  if (barsAllowed && !IsIconic(w32Wnd)) {
    wantH = si.framebufferWidth > w;
    if (wantH)
      h -= cy;
    wantV = si.framebufferHeight > h;
    if (wantV) {
      w -= cx;
      if (!wantH && si.framebufferWidth > w) {
	wantH = True;
	h -= cy;
      }
    }
  }

  visW = w > 0 ? w : 0;
  visH = h > 0 ? h : 0;
  ClampScroll(&scrollX, &scrollY);

  hBar = wantH;
  vBar = wantV;
  SetScrollRange(w32Wnd, SB_HORZ, 0, hBar ? si.framebufferWidth - visW : 0,
		 TRUE);
  SetScrollRange(w32Wnd, SB_VERT, 0, vBar ? si.framebufferHeight - visH : 0,
		 TRUE);
  SetScrollPos(w32Wnd, SB_HORZ, scrollX, TRUE);
  SetScrollPos(w32Wnd, SB_VERT, scrollY, TRUE);

  busy = False;
}

void
ScrollInit(void)
{
  Layout();
}

void
ScrollResize(void)
{
  Layout();
}

void
ScrollAllowBars(Bool on)
{
  barsAllowed = on;
  Layout();
}

void
ScrollTo(int x, int y)
{
  int dx, dy;

  ClampScroll(&x, &y);
  dx = x - scrollX;
  dy = y - scrollY;
  if (dx == 0 && dy == 0)
    return;

  scrollX = x;
  scrollY = y;
  SetScrollPos(w32Wnd, SB_HORZ, scrollX, TRUE);
  SetScrollPos(w32Wnd, SB_VERT, scrollY, TRUE);
  ScrollWindow(w32Wnd, -dx, -dy, NULL, NULL);
  UpdateWindow(w32Wnd);
}

void
ScrollGetPos(int *x, int *y)
{
  *x = scrollX;
  *y = scrollY;
}

void
ScrollGetVisible(int *w, int *h)
{
  *w = visW;
  *h = visH;
}

static void
ScrollBarEvent(UINT msg, int code, int pos)
{
  int x = scrollX, y = scrollY;
  int *p = (msg == WM_HSCROLL) ? &x : &y;
  int page = (msg == WM_HSCROLL) ? visW : visH;

  switch (code) {
  case SB_LINEUP:	*p -= 16; break;
  case SB_LINEDOWN:	*p += 16; break;
  case SB_PAGEUP:	*p -= page; break;
  case SB_PAGEDOWN:	*p += page; break;
  case SB_THUMBTRACK:
  case SB_THUMBPOSITION: *p = pos; break;
  case SB_TOP:		*p = 0; break;
  case SB_BOTTOM:	*p = 32767; break;
  default:		return;
  }

  ScrollTo(x, y);
}

static void
Paint(void)
{
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(w32Wnd, &ps);
  HBRUSH black = (HBRUSH)GetStockObject(BLACK_BRUSH);
  int right = si.framebufferWidth - scrollX;	/* where the desktop ends */
  int bottom = si.framebufferHeight - scrollY;
  RECT r = ps.rcPaint, b;

  W32SelectPalette(dc);

  if (imageDC && r.left < right && r.top < bottom)
    BitBlt(dc, r.left, r.top,
	   (r.right < right ? r.right : right) - r.left,
	   (r.bottom < bottom ? r.bottom : bottom) - r.top,
	   imageDC, r.left + scrollX, r.top + scrollY, SRCCOPY);

  /* black around a desktop smaller than the window */
  if (r.right > right) {
    b = r;
    if (b.left < right)
      b.left = right;
    FillRect(dc, &b, black);
  }
  if (r.bottom > bottom) {
    b = r;
    if (b.top < bottom)
      b.top = bottom;
    if (b.right > right)
      b.right = right;
    FillRect(dc, &b, black);
  }

  EndPaint(w32Wnd, &ps);
}


/*
 * Full screen: the window loses its frame and covers the screen.  It stays
 * on top only while it is the active window.
 */

static void
BumpStop(void)
{
  if (bumpTimer) {
    KillTimer(w32Wnd, TIMER_BUMP);
    bumpTimer = False;
  }
}

static Bool
BumpStep(void)
{
  int x = scrollX + bumpDX * appData.bumpScrollPixels;
  int y = scrollY + bumpDY * appData.bumpScrollPixels;

  ClampScroll(&x, &y);
  if (x == scrollX && y == scrollY) {
    BumpStop();
    return False;
  }

  ScrollTo(x, y);
  if (!bumpTimer) {
    SetTimer(w32Wnd, TIMER_BUMP, appData.bumpScrollTime, NULL);
    bumpTimer = True;
  }
  return True;
}

/* the pointer against an edge scrolls a desktop bigger than the screen */
static Bool
BumpCheck(void)
{
  POINT pt;

  GetCursorPos(&pt);
  bumpDX = (pt.x >= dpyWidth - 3) ? 1 : (pt.x <= 2) ? -1 : 0;
  bumpDY = (pt.y >= dpyHeight - 3) ? 1 : (pt.y <= 2) ? -1 : 0;

  if (bumpDX || bumpDY)
    return bumpTimer ? True : BumpStep();

  BumpStop();
  return False;
}

void
FullScreenOn(void)
{
  savedPlacement.length = sizeof(savedPlacement);
  GetWindowPlacement(w32Wnd, &savedPlacement);

  appData.fullScreen = True;
  barsAllowed = False;

  SetWindowLong(w32Wnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
  SetWindowPos(w32Wnd, HWND_TOPMOST, 0, 0, dpyWidth, dpyHeight,
	       SWP_FRAMECHANGED);
  Layout();
}

void
FullScreenOff(void)
{
  appData.fullScreen = False;
  BumpStop();
  barsAllowed = True;

  SetWindowLong(w32Wnd, GWL_STYLE, WINDOW_STYLE | WS_VISIBLE);
  SetWindowPos(w32Wnd, HWND_NOTOPMOST, 0, 0, 0, 0,
	       SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED);
  SetWindowPlacement(w32Wnd, &savedPlacement);

  ScrollTo(0, 0);
  Layout();
  InvalidateRect(w32Wnd, NULL, FALSE);
}

void
ToggleFullScreen(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  if (appData.fullScreen)
    FullScreenOff();
  else
    FullScreenOn();
}

void
SetFullScreenState(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
}


/*
 * The pointer.  Buttons 1-3 are left, middle and right, the wheel is 4 and
 * 5, the same as an X server makes them.
 */

static void
PointerEvent(UINT msg, WPARAM wParam, LPARAM lParam)
{
  int x = (short)LOWORD(lParam) + scrollX;
  int y = (short)HIWORD(lParam) + scrollY;

  if (msg == WM_LBUTTONDOWN || msg == WM_MBUTTONDOWN || msg == WM_RBUTTONDOWN)
    SetCapture(w32Wnd);
  else if (msg != WM_MOUSEMOVE &&
	   !(wParam & (MK_LBUTTON | MK_MBUTTON | MK_RBUTTON)))
    ReleaseCapture();

  buttonMask = ((wParam & MK_LBUTTON) ? 1 : 0) |
	       ((wParam & MK_MBUTTON) ? 2 : 0) |
	       ((wParam & MK_RBUTTON) ? 4 : 0);

  if (appData.fullScreen && msg == WM_MOUSEMOVE && BumpCheck())
    return;

  if (appData.viewOnly)
    return;

  if (x >= si.framebufferWidth)
    x = si.framebufferWidth - 1;
  if (y >= si.framebufferHeight)
    y = si.framebufferHeight - 1;
  if (x < 0)
    x = 0;
  if (y < 0)
    y = 0;

  lastX = x;
  lastY = y;
  SendPointerEvent(x, y, buttonMask);
}

static void
WheelEvent(WPARAM wParam, LPARAM lParam)
{
  int delta = (short)HIWORD(wParam);
  int button = delta > 0 ? 8 : 16;
  int clicks = (delta < 0 ? -delta : delta) / 120;
  POINT pt;

  if (appData.viewOnly)
    return;

  pt.x = (short)LOWORD(lParam);
  pt.y = (short)HIWORD(lParam);
  ScreenToClient(w32Wnd, &pt);
  lastX = pt.x + scrollX;
  lastY = pt.y + scrollY;

  if (clicks == 0)
    clicks = 1;			/* a fine wheel still moves a notch */
  for (; clicks > 0; clicks--) {
    SendPointerEvent(lastX, lastY, buttonMask | button);
    SendPointerEvent(lastX, lastY, buttonMask);
  }
}

/* stop the server holding a button we can no longer see let go */
static void
ReleaseButtons(void)
{
  if (buttonMask && !appData.viewOnly)
    SendPointerEvent(lastX, lastY, 0);
  buttonMask = 0;
}

void
SendRFBEvent(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
}


/*
 * The F8 menu, built afresh each time so that it shows the state as it is.
 */

static void
SendCtrlAltDel(void)
{
  SendKeyEvent(XK_Control_L, True);
  SendKeyEvent(XK_Alt_L, True);
  SendKeyEvent(XK_Delete, True);
  SendKeyEvent(XK_Delete, False);
  SendKeyEvent(XK_Alt_L, False);
  SendKeyEvent(XK_Control_L, False);
}

void
CreatePopup(void)
{
  popup = NULL;
}

void
ShowPopup(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
  HMENU m = CreatePopupMenu();
  char cursorLabel[40];
  POINT pt;

  AppendMenu(m, MF_STRING | (appData.fullScreen ? MF_CHECKED : 0),
	     M_FULLSCREEN, "&Full screen");
  AppendMenu(m, MF_STRING | (cuActive ? MF_CHECKED : 0) |
	     (supportsCU ? 0 : MF_GRAYED), M_CONTINUOUS, "&Continuous updates");
  AppendMenu(m, MF_STRING, M_REPAINT, "Re&paint screen");
  AppendMenu(m, MF_STRING, M_REFRESH, "&Request refresh");
  AppendMenu(m, MF_SEPARATOR, 0, NULL);

  sprintf(cursorLabel, "&Local cursor: %s", LocalCursorName());
  AppendMenu(m, MF_STRING, M_CURSOR, cursorLabel);
  AppendMenu(m, MF_STRING | (appData.useHwCursor ? MF_CHECKED : 0),
	     M_HWCURSOR, "&Windows draws remote cursor");
  AppendMenu(m, MF_SEPARATOR, 0, NULL);

  AppendMenu(m, MF_STRING, M_CLIP_OUT, "Clipboard local -> rem&ote");
  AppendMenu(m, MF_STRING, M_CLIP_IN, "Clipboard local <- r&emote");
  AppendMenu(m, MF_SEPARATOR, 0, NULL);

  AppendMenu(m, MF_STRING, M_CTRLALTDEL, "Send Ctrl-Alt-&Del");
  AppendMenu(m, MF_STRING, M_SENDF8, "Send F&8");
  AppendMenu(m, MF_SEPARATOR, 0, NULL);

  AppendMenu(m, MF_STRING, M_SETTINGS, "&Settings...");
  AppendMenu(m, MF_STRING, M_DISMISS, "Dis&miss");
  AppendMenu(m, MF_STRING, M_QUIT, "&Quit viewer");

  GetCursorPos(&pt);
  TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, 0, w32Wnd,
		 NULL);
  DestroyMenu(m);
}

void
HidePopup(Widget w, XEvent *event, String *params, Cardinal *num_params)
{
}

void
RefreshPopup(void)
{
}

static void
MenuCommand(int id)
{
  String always = (String)"always";
  Cardinal one = 1;

  switch (id) {
  case M_FULLSCREEN:
    ToggleFullScreen(NULL, NULL, NULL, NULL);
    break;
  case M_CONTINUOUS:
    ToggleContinuousUpdates(NULL, NULL, NULL, NULL);
    break;
  case M_REPAINT:
    RepaintScreen(NULL, NULL, NULL, NULL);
    break;
  case M_REFRESH:
    SendFramebufferUpdateRequest(0, 0, si.framebufferWidth,
				 si.framebufferHeight, False);
    break;
  case M_CURSOR:
    CycleLocalCursor(NULL, NULL, NULL, NULL);
    break;
  case M_HWCURSOR:
    ToggleHardwareCursor(NULL, NULL, NULL, NULL);
    break;
  case M_CLIP_OUT:
    SelectionToVNC(NULL, NULL, &always, &one);
    break;
  case M_CLIP_IN:
    SelectionFromVNC(NULL, NULL, &always, &one);
    break;
  case M_CTRLALTDEL:
    if (!appData.viewOnly)
      SendCtrlAltDel();
    break;
  case M_SENDF8:
    if (!appData.viewOnly) {
      SendKeyEvent(XK_F8, True);
      SendKeyEvent(XK_F8, False);
    }
    break;
  case M_SETTINGS:
    ShowSettings(NULL, NULL, NULL, NULL);
    break;
  case M_QUIT:
    Quit(NULL, NULL, NULL, NULL);
    break;
  }
}


/*
 * The window procedure.  Keys and the pointer go to the server; F8 and the
 * Windows keys Windows keeps for itself (Alt-Tab, Ctrl-Esc) do not.
 */

static Bool
OurWindow(HWND hwnd)
{
  return hwnd && GetWindowThreadProcessId(hwnd, NULL) == GetCurrentThreadId();
}

static LRESULT CALLBACK
WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  int w, h;

  switch (msg) {

  case WM_RFBSOCKET:
    W32SocketEvent(wParam, lParam);
    return 0;

  case WM_PAINT:
    Paint();
    return 0;

  case WM_ERASEBKGND:
    return 1;			/* Paint covers it all */

  case WM_SIZE:
    Layout();
    if (wParam != SIZE_MINIMIZED && !appData.fullScreen) {
      DesktopArea(&w, &h);
      ScheduleRemoteResize(w, h);
    }
    return 0;

  case WM_GETMINMAXINFO:
    /* no bigger than the desktop, unless the desktop follows the window */
    if (imageBits && !appData.fullScreen &&
	!(supportsSetDesktopSize && appData.useRemoteResize)) {
      MINMAXINFO *mm = (MINMAXINFO *)lParam;
      RECT r;

      SetRect(&r, 0, 0, si.framebufferWidth, si.framebufferHeight);
      AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
      mm->ptMaxTrackSize.x = r.right - r.left;
      mm->ptMaxTrackSize.y = r.bottom - r.top;
      mm->ptMaxSize = mm->ptMaxTrackSize;
    }
    return 0;

  case WM_HSCROLL:
  case WM_VSCROLL:
    ScrollBarEvent(msg, LOWORD(wParam), HIWORD(wParam));
    return 0;

  case WM_MOUSEMOVE:
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
    PointerEvent(msg, wParam, lParam);
    return 0;

  case WM_MOUSEWHEEL:
    WheelEvent(wParam, lParam);
    return 0;

  case WM_KEYDOWN:
  case WM_SYSKEYDOWN:
    if (wParam == VK_F8) {
      if (!(lParam & 0x40000000))	/* not the auto repeat */
	ShowPopup(NULL, NULL, NULL, NULL);
      return 0;
    }
    W32KeyEvent(msg, wParam, lParam);
    return 0;

  case WM_KEYUP:
  case WM_SYSKEYUP:
    if (wParam != VK_F8)
      W32KeyEvent(msg, wParam, lParam);
    return 0;

  case WM_CHAR:
  case WM_SYSCHAR:
  case WM_DEADCHAR:
  case WM_SYSDEADCHAR:
    return 0;

  case WM_SETCURSOR:
    if (LOWORD(lParam) == HTCLIENT) {
      SetCursor(W32Cursor());
      return TRUE;
    }
    break;

  case WM_KILLFOCUS:
    W32ReleaseKeys();
    ReleaseButtons();
    break;

  case WM_ACTIVATE:
    if (appData.fullScreen) {
      if (LOWORD(wParam) != WA_INACTIVE)
	SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
      else if (!OurWindow((HWND)lParam))
	SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
		     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    W32Activated(LOWORD(wParam) != WA_INACTIVE);
    break;

  case WM_TIMER:
    if (wParam == TIMER_RESIZE)
      RemoteResizeTimer();
    else if (wParam == TIMER_BUMP)
      BumpStep();
    return 0;

  case WM_COMMAND:
    MenuCommand(LOWORD(wParam));
    return 0;

  case WM_DRAWCLIPBOARD:
    W32DrawClipboard();
    return 0;

  case WM_CHANGECBCHAIN:
    W32ChangeCbChain(wParam, lParam);
    return 0;

  case WM_QUERYNEWPALETTE:
  case WM_PALETTECHANGED:
    if (w32Palette && (msg == WM_QUERYNEWPALETTE || (HWND)wParam != hwnd)) {
      HDC dc = GetDC(hwnd);

      W32SelectPalette(dc);
      ReleaseDC(hwnd, dc);
      InvalidateRect(hwnd, NULL, FALSE);
      return TRUE;
    }
    return FALSE;

  case WM_CLOSE:
    Quit(NULL, NULL, NULL, NULL);
    return 0;

  case WM_DESTROY:
    Cleanup();
    return 0;
  }

  return DefWindowProc(hwnd, msg, wParam, lParam);
}
