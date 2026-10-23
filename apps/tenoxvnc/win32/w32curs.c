/*
 * w32curs.c - the local cursor and the remote cursor shape.  A shape of
 * two colors or fewer becomes a Windows cursor; any other is drawn into
 * the image under the pointer.  The Win32 side of cursor.c.
 */

#include <windows.h>
#include "vncviewer.h"
#include "w32vnc.h"

#define RGB24_TO_PIXEL(bpp,r,g,b)                                       \
   ((((CARD##bpp)(r) & 0xFF) * myFormat.redMax + 127) / 255             \
    << myFormat.redShift |                                              \
    (((CARD##bpp)(g) & 0xFF) * myFormat.greenMax + 127) / 255           \
    << myFormat.greenShift |                                            \
    (((CARD##bpp)(b) & 0xFF) * myFormat.blueMax + 127) / 255            \
    << myFormat.blueShift)

enum { CURSOR_DOT, CURSOR_ARROW, CURSOR_NONE };
static const char *cursorModeNames[] = { "dot", "arrow", "none" };
static int cursorMode = CURSOR_DOT;
static Bool localCursorChosen = False;
static HCURSOR dotCursorW = NULL, blankCursor = NULL;
static HCURSOR remoteCursor = NULL;	/* a shape Windows draws */

/* the shape last sent, kept for drawing it another way */
static CARD8 *curSource = NULL;		/* pixels, server format */
static CARD8 *curMask = NULL;		/* 1 bit, leftmost pixel high */
static int curHotX, curHotY, curWidth, curHeight;
static Bool curShapeSet = False;

/* the shape drawn into the image */
static Bool softSet = False;
static CARD8 *softSaved = NULL;		/* what it covers */
static int rcCursorX = 0, rcCursorY = 0;
static int rcLockX, rcLockY, rcLockWidth, rcLockHeight;
static Bool rcCursorHidden, rcLockSet;

static Bool InstallCursorShape(void);
static void SoftCursorRect(int *x, int *y, int *w, int *h);
static void SoftCursorSave(void);
static void SoftCursorRestore(void);
static void SoftCursorDraw(void);
static void FreeSoftCursor(void);


/*
 * The cursor over the desktop, for WM_SETCURSOR: the server's shape when
 * Windows can draw it, otherwise the local one.
 */

static HCURSOR
MakeCursor(const char *rows[], int n, int hotX, int hotY)
{
  int cw = GetSystemMetrics(SM_CXCURSOR), ch = GetSystemMetrics(SM_CYCURSOR);
  int wb = (cw + 15) / 16 * 2;
  BYTE *andMask = malloc(wb * ch), *xorMask = calloc(wb, ch);
  HCURSOR c;
  int x, y;

  memset(andMask, 0xff, wb * ch);
  for (y = 0; y < n && y < ch; y++) {
    for (x = 0; rows[y][x] && x < cw; x++) {
      BYTE bit = (BYTE)(0x80 >> (x & 7));

      if (rows[y][x] == ' ')
	continue;
      andMask[y * wb + x / 8] &= (BYTE)~bit;
      if (rows[y][x] == 'o')
	xorMask[y * wb + x / 8] |= bit;	/* white; '#' is black */
    }
  }

  c = CreateCursor(w32Inst, hotX, hotY, cw, ch, andMask, xorMask);
  free(andMask);
  free(xorMask);
  return c;
}

void
W32CursorInit(void)
{
  int i;

  for (i = 0; i < 3; i++)
    if (_stricmp(appData.localCursor, cursorModeNames[i]) == 0)
      cursorMode = i;
}

HCURSOR
W32Cursor(void)
{
  static const char *dot[] = { " ooo ", "o###o", "o###o", "o###o", " ooo " };
  static const char *blank[] = { "" };

  if (remoteCursor)
    return remoteCursor;

  if (!dotCursorW) {
    dotCursorW = MakeCursor(dot, 5, 2, 2);
    blankCursor = MakeCursor(blank, 1, 0, 0);
  }

  switch (cursorMode) {
  case CURSOR_ARROW:
    return LoadCursor(NULL, IDC_ARROW);
  case CURSOR_NONE:
    return blankCursor;
  default:
    return dotCursorW;
  }
}

/* show a new cursor at once if the pointer is over the desktop */
static void
UpdatePointerShape(void)
{
  POINT pt;

  GetCursorPos(&pt);
  if (WindowFromPoint(pt) == w32Wnd)
    SetCursor(W32Cursor());
}

const char *
LocalCursorName(void)
{
  return cursorModeNames[cursorMode];
}

Bool
LocalCursorChosen(void)
{
  return localCursorChosen;
}

static void
RestoreLocalCursor(void)
{
  HCURSOR old = remoteCursor;

  if (!old)
    return;

  remoteCursor = NULL;
  UpdatePointerShape();
  DestroyCursor(old);
}


/*
 * A two-color shape as a Windows cursor.  Those are black and white only,
 * so the lighter color is drawn white and the darker one black - right for
 * the usual arrow and I-beam.
 */

static unsigned long
Brightness(CARD32 pixel)
{
  unsigned long r = (pixel >> myFormat.redShift) & myFormat.redMax;
  unsigned long g = (pixel >> myFormat.greenShift) & myFormat.greenMax;
  unsigned long b = (pixel >> myFormat.blueShift) & myFormat.blueMax;

  return (myFormat.redMax ? r * 30000 / myFormat.redMax : 0) +
	 (myFormat.greenMax ? g * 59000 / myFormat.greenMax : 0) +
	 (myFormat.blueMax ? b * 11000 / myFormat.blueMax : 0);
}

static CARD32
ShapePixel(int x, int y)
{
  int bpp = myFormat.bitsPerPixel / 8;
  CARD8 *p = &curSource[(y * curWidth + x) * bpp];

  switch (bpp) {
  case 1:  return *p;
  case 2:  return *(CARD16 *)p;
  default: return *(CARD32 *)p;
  }
}

static Bool
ShapeOpaque(int x, int y)
{
  return (curMask[y * ((curWidth + 7) / 8) + x / 8] & (0x80 >> (x & 7))) != 0;
}

static Bool
TryHardwareCursor(void)
{
  int cw = GetSystemMetrics(SM_CXCURSOR), ch = GetSystemMetrics(SM_CYCURSOR);
  int wb = (cw + 15) / 16 * 2;
  CARD32 colors[2], white;
  int nColors = 0, x, y, i;
  BYTE *andMask, *xorMask;
  HCURSOR c;

  if (curWidth > cw || curHeight > ch)
    return False;

  for (y = 0; y < curHeight; y++) {
    for (x = 0; x < curWidth; x++) {
      CARD32 pixel;

      if (!ShapeOpaque(x, y))
	continue;
      pixel = ShapePixel(x, y);
      for (i = 0; i < nColors; i++)
	if (colors[i] == pixel)
	  break;
      if (i == nColors) {
	if (nColors == 2)
	  return False;		/* real colors: draw it ourselves */
	colors[nColors++] = pixel;
      }
    }
  }

  if (nColors == 2)
    white = Brightness(colors[0]) > Brightness(colors[1]) ? colors[0]
							     : colors[1];
  else if (nColors == 1 && Brightness(colors[0]) >= 50000)
    white = colors[0];
  else
    white = ~(CARD32)0;		/* a single dark color, or none at all */

  andMask = malloc(wb * ch);
  xorMask = calloc(wb, ch);
  if (!andMask || !xorMask) {
    free(andMask);
    free(xorMask);
    return False;
  }
  memset(andMask, 0xff, wb * ch);

  for (y = 0; y < curHeight; y++) {
    for (x = 0; x < curWidth; x++) {
      BYTE bit = (BYTE)(0x80 >> (x & 7));

      if (!ShapeOpaque(x, y))
	continue;
      andMask[y * wb + x / 8] &= (BYTE)~bit;
      if (ShapePixel(x, y) == white)
	xorMask[y * wb + x / 8] |= bit;
    }
  }

  c = CreateCursor(w32Inst, curHotX < cw ? curHotX : cw - 1,
		   curHotY < ch ? curHotY : ch - 1, cw, ch, andMask, xorMask);
  free(andMask);
  free(xorMask);
  if (!c)
    return False;

  RestoreLocalCursor();
  remoteCursor = c;
  UpdatePointerShape();
  return True;
}


/*
 * HandleCursorShape reads an XCursor or RichCursor update, as in cursor.c.
 */

Bool
HandleCursorShape(int xhot, int yhot, int width, int height, CARD32 enc)
{
  int bytesPerPixel = myFormat.bitsPerPixel / 8;
  size_t bytesPerRow = (width + 7) / 8;
  size_t bytesMaskData = bytesPerRow * height;
  size_t pixelBytes;
  rfbXCursorColors rgb;
  CARD32 colors[2];
  CARD8 *source, *mask, *ptr;
  int x, y, b;

  FreeSoftCursor();

  if (width * height == 0) {
    RestoreLocalCursor();	/* the server is hiding the cursor */
    curShapeSet = False;
    return True;
  }

  if (width > RFB_MAX_CURSOR_DIMENSION || height > RFB_MAX_CURSOR_DIMENSION) {
    fprintf(stderr, "Cursor shape too large: %dx%d\n", width, height);
    return False;
  }

  if (!RfbMulSize((size_t)width, (size_t)height, (size_t)bytesPerPixel,
		  &pixelBytes)) {
    fprintf(stderr, "Cursor shape pixel data size overflow\n");
    return False;
  }

  source = malloc(pixelBytes);
  mask = malloc(bytesMaskData);
  if (!source || !mask) {
    free(source);
    free(mask);
    return False;
  }

  if (enc == rfbEncodingXCursor) {
    if (!ReadFromRFBServer((char *)&rgb, sz_rfbXCursorColors) ||
	!ReadFromRFBServer((char *)mask, bytesMaskData)) {
      free(source);
      free(mask);
      return False;
    }
    colors[0] = RGB24_TO_PIXEL(32, rgb.backRed, rgb.backGreen, rgb.backBlue);
    colors[1] = RGB24_TO_PIXEL(32, rgb.foreRed, rgb.foreGreen, rgb.foreBlue);

    /* the 1bpp data, read into mask for now, as pixels */
    ptr = source;
    for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
	b = (mask[y * bytesPerRow + x / 8] >> (7 - (x & 7))) & 1;
	switch (bytesPerPixel) {
	case 1: *ptr = (CARD8)colors[b]; break;
	case 2: *(CARD16 *)ptr = (CARD16)colors[b]; break;
	default: *(CARD32 *)ptr = colors[b]; break;
	}
	ptr += bytesPerPixel;
      }
    }
  } else if (!ReadFromRFBServer((char *)source, pixelBytes)) {
    free(source);
    free(mask);
    return False;
  }

  if (!ReadFromRFBServer((char *)mask, bytesMaskData)) {
    free(source);
    free(mask);
    return False;
  }

  free(curSource);
  free(curMask);
  curSource = source;
  curMask = mask;
  curHotX = xhot;
  curHotY = yhot;
  curWidth = width;
  curHeight = height;
  curShapeSet = True;

  return InstallCursorShape();
}

static Bool
InstallCursorShape(void)
{
  FreeSoftCursor();

  if (!curShapeSet)
    return True;

  if ((appData.useHwCursor || appData.useX11Cursor) && !localCursorChosen &&
      TryHardwareCursor())
    return True;

  RestoreLocalCursor();

  if (appData.useX11Cursor)
    return True;		/* only Windows may draw it: the local one then */

  softSaved = malloc((size_t)curWidth * curHeight * (myFormat.bitsPerPixel / 8));
  if (!softSaved)
    return False;

  softSet = True;
  rcLockSet = False;
  rcCursorHidden = True;
  SoftCursorUnlockScreen();	/* save, draw and show it */
  return True;
}

void
ToggleHardwareCursor(Widget w, XEvent *ev, String *params,
		     Cardinal *num_params)
{
  appData.useHwCursor = !appData.useHwCursor;

  InstallCursorShape();

  fprintf(stderr, "Remote cursor drawn by: %s\n",
	  appData.useHwCursor ? "Windows when it can" : "viewer");
}

void
CycleLocalCursor(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
  cursorMode = (cursorMode + 1) % 3;

  /* stop remote cursor shapes overriding the explicit choice */
  appData.useX11Cursor = False;
  localCursorChosen = True;

  InstallCursorShape();
  RestoreLocalCursor();
  UpdatePointerShape();
  fprintf(stderr, "Local cursor: %s\n", cursorModeNames[cursorMode]);
}

void
SetLocalCursorState(Widget w, XEvent *ev, String *params, Cardinal *num_params)
{
}

void
ForgetRemoteCursor(void)
{
  SoftCursorUnlockScreen();
  FreeSoftCursor();
  RestoreLocalCursor();

  free(curSource);
  free(curMask);
  curSource = NULL;
  curMask = NULL;
  curShapeSet = False;
}

Bool
HandleCursorPos(int x, int y)
{
  if (appData.useX11Cursor)
    return True;

  if (x >= si.framebufferWidth)
    x = si.framebufferWidth - 1;
  if (y >= si.framebufferHeight)
    y = si.framebufferHeight - 1;

  SoftCursorMove(x, y);
  return True;
}


/*
 * The cursor drawn into the image.  Locking an area takes it out while a
 * decoder writes there, and unlocking puts it back, as in cursor.c.
 */

static Bool
SoftCursorInLockedArea(void)
{
  return (rcLockX < rcCursorX - curHotX + curWidth &&
	  rcLockY < rcCursorY - curHotY + curHeight &&
	  rcLockX + rcLockWidth > rcCursorX - curHotX &&
	  rcLockY + rcLockHeight > rcCursorY - curHotY);
}

void
SoftCursorLockArea(int x, int y, int w, int h)
{
  int newX, newY;

  if (!softSet)
    return;

  if (!rcLockSet) {
    rcLockX = x;
    rcLockY = y;
    rcLockWidth = w;
    rcLockHeight = h;
    rcLockSet = True;
  } else {
    newX = (x < rcLockX) ? x : rcLockX;
    newY = (y < rcLockY) ? y : rcLockY;
    rcLockWidth = (x + w > rcLockX + rcLockWidth) ?
      (x + w - newX) : (rcLockX + rcLockWidth - newX);
    rcLockHeight = (y + h > rcLockY + rcLockHeight) ?
      (y + h - newY) : (rcLockY + rcLockHeight - newY);
    rcLockX = newX;
    rcLockY = newY;
  }

  if (!rcCursorHidden && SoftCursorInLockedArea()) {
    int cx, cy, cw, ch;

    SoftCursorRestore();
    rcCursorHidden = True;
    SoftCursorRect(&cx, &cy, &cw, &ch);
    PutImageRect(cx, cy, cw, ch);
  }
}

void
SoftCursorUnlockScreen(void)
{
  if (!softSet)
    return;

  if (rcCursorHidden) {
    int cx, cy, cw, ch;

    SoftCursorSave();
    SoftCursorDraw();
    rcCursorHidden = False;
    SoftCursorRect(&cx, &cy, &cw, &ch);
    PutImageRect(cx, cy, cw, ch);
  }
  rcLockSet = False;
}

void
SoftCursorMove(int x, int y)
{
  int ox = 0, oy = 0, ow = 0, oh = 0, nx, ny, nw, nh;

  if (softSet && !rcCursorHidden && x == rcCursorX && y == rcCursorY)
    return;

  if (softSet && !rcCursorHidden) {
    SoftCursorRect(&ox, &oy, &ow, &oh);
    SoftCursorRestore();
    rcCursorHidden = True;
  }

  rcCursorX = x;
  rcCursorY = y;

  if (softSet && !(rcLockSet && SoftCursorInLockedArea())) {
    SoftCursorSave();
    SoftCursorDraw();
    rcCursorHidden = False;
  }

  if (!softSet)
    return;

  /* one put for where it was and where it is, so it does not flicker */
  SoftCursorRect(&nx, &ny, &nw, &nh);
  if (ow <= 0 || oh <= 0) {
    PutImageRect(nx, ny, nw, nh);
  } else if (nw <= 0 || nh <= 0) {
    PutImageRect(ox, oy, ow, oh);
  } else {
    int x0 = ox < nx ? ox : nx, y0 = oy < ny ? oy : ny;
    int x1 = ox + ow > nx + nw ? ox + ow : nx + nw;
    int y1 = oy + oh > ny + nh ? oy + oh : ny + nh;

    PutImageRect(x0, y0, x1 - x0, y1 - y0);
  }
}

/* where the cursor is, cut down to the framebuffer */
static void
SoftCursorRect(int *x, int *y, int *w, int *h)
{
  *x = rcCursorX - curHotX;
  *y = rcCursorY - curHotY;
  *w = curWidth;
  *h = curHeight;

  if (*x < 0) { *w += *x; *x = 0; }
  if (*y < 0) { *h += *y; *y = 0; }
  if (*x + *w > si.framebufferWidth)
    *w = si.framebufferWidth - *x;
  if (*y + *h > si.framebufferHeight)
    *h = si.framebufferHeight - *y;
  if (*w < 0)
    *w = 0;
  if (*h < 0)
    *h = 0;
}

static void
SoftCursorSave(void)
{
  int bpp = myFormat.bitsPerPixel / 8;
  int x, y, w, h, q;

  SoftCursorRect(&x, &y, &w, &h);
  for (q = 0; q < h; q++)
    memcpy(softSaved + (size_t)q * w * bpp, ImageRow(x, y + q),
	   (size_t)w * bpp);
}

static void
SoftCursorRestore(void)
{
  int bpp = myFormat.bitsPerPixel / 8;
  int x, y, w, h, q;

  SoftCursorRect(&x, &y, &w, &h);
  for (q = 0; q < h; q++)
    memcpy(ImageRow(x, y + q), softSaved + (size_t)q * w * bpp,
	   (size_t)w * bpp);
}

static void
SoftCursorDraw(void)
{
  int bpp = myFormat.bitsPerPixel / 8;
  int x0 = rcCursorX - curHotX, y0 = rcCursorY - curHotY;
  int x, y;

  for (y = 0; y < curHeight; y++) {
    if (y0 + y < 0 || y0 + y >= si.framebufferHeight)
      continue;
    for (x = 0; x < curWidth; x++) {
      if (x0 + x < 0 || x0 + x >= si.framebufferWidth || !ShapeOpaque(x, y))
	continue;
      memcpy(ImageRow(x0 + x, y0 + y),
	     &curSource[((size_t)y * curWidth + x) * bpp], (size_t)bpp);
    }
  }
}

static void
FreeSoftCursor(void)
{
  if (!softSet)
    return;

  if (!rcCursorHidden) {
    int cx, cy, cw, ch;

    SoftCursorRestore();
    SoftCursorRect(&cx, &cy, &cw, &ch);
    PutImageRect(cx, cy, cw, ch);
  }

  free(softSaved);
  softSaved = NULL;
  softSet = False;
}
