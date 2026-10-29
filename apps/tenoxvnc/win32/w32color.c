/*
 * w32color.c - the pixel format asked of the server, which is also the
 * format of the local image, so nothing is ever translated.  The Win32 side
 * of color.c: full color is a 16 or 32 bit image, the reduced levels an 8
 * bit one whose color table is the level's color cube.
 */

#include <windows.h>
#include "vncviewer.h"
#include "w32vnc.h"

unsigned long colorToPixel[256];
Bool useColorMap = False;
Colormap cmap;
Visual *vis;
unsigned int visdepth, visbpp;

HPALETTE w32Palette = NULL;	/* for a display with a palette */

static const struct {
  int redMax, greenMax, blueMax;
} colorLevels[] = {
  { 0, 0, 0 },			/* COLOR_FULL - not a reduced format */
  { 7, 7, 3 },			/* COLOR_MEDIUM */
  { 3, 3, 3 },			/* COLOR_LOW */
  { 1, 1, 1 }			/* COLOR_VERYLOW */
};

static int screenBits = 0;
static Bool paletteDisplay = False;
static int activeLevel = COLOR_FULL;


static int
Bits(int max)
{
  int n = 0;

  while (max) {
    n++;
    max >>= 1;
  }
  return n;
}

static void
SetColorFormat(void)
{
  if (w32Palette) {
    DeleteObject(w32Palette);
    w32Palette = NULL;
  }

  /* a display with 256 colors or fewer cannot show full color at all */
  if (appData.colorLevel == COLOR_FULL && (paletteDisplay || screenBits <= 8))
    appData.colorLevel = COLOR_MEDIUM;

  activeLevel = appData.colorLevel;
  myFormat.trueColor = 1;
  myFormat.bigEndian = 0;

  if (activeLevel == COLOR_FULL) {
    if (screenBits == 15 || screenBits == 16) {
      visbpp = 16;
      visdepth = 16;
      myFormat.redMax = 31;
      myFormat.greenMax = 63;
      myFormat.blueMax = 31;
      myFormat.redShift = 11;
      myFormat.greenShift = 5;
      myFormat.blueShift = 0;
    } else {
      visbpp = 32;
      visdepth = 24;
      myFormat.redMax = 255;
      myFormat.greenMax = 255;
      myFormat.blueMax = 255;
      myFormat.redShift = 16;
      myFormat.greenShift = 8;
      myFormat.blueShift = 0;
    }
    myFormat.bitsPerPixel = visbpp;
    myFormat.depth = visdepth;

    fprintf(stderr, "Using %d bit true color.  Pixel format:\n", visdepth);
    PrintPixelFormat(&myFormat);
    return;
  }

  visbpp = 8;
  visdepth = 8;
  myFormat.bitsPerPixel = 8;
  myFormat.redMax = colorLevels[activeLevel].redMax;
  myFormat.greenMax = colorLevels[activeLevel].greenMax;
  myFormat.blueMax = colorLevels[activeLevel].blueMax;
  myFormat.redShift = 0;
  myFormat.greenShift = Bits(myFormat.redMax);
  myFormat.blueShift = myFormat.greenShift + Bits(myFormat.greenMax);
  myFormat.depth = myFormat.blueShift + Bits(myFormat.blueMax);

  fprintf(stderr, "Using %s.  Pixel format:\n", ColorModeName());
  PrintPixelFormat(&myFormat);

  if (paletteDisplay) {
    struct {
      WORD version, n;
      PALETTEENTRY e[256];
    } pal;
    RGBQUAD table[256];
    int i;

    W32ColorTable(table);
    pal.version = 0x300;
    pal.n = (WORD)(1 << myFormat.depth);
    for (i = 0; i < (int)pal.n; i++) {
      pal.e[i].peRed = table[i].rgbRed;
      pal.e[i].peGreen = table[i].rgbGreen;
      pal.e[i].peBlue = table[i].rgbBlue;
      pal.e[i].peFlags = 0;
    }
    w32Palette = CreatePalette((LOGPALETTE *)&pal);
  }
}

void
SetVisualAndCmap(void)
{
  HDC dc = GetDC(NULL);

  screenBits = GetDeviceCaps(dc, BITSPIXEL) * GetDeviceCaps(dc, PLANES);
  paletteDisplay = (GetDeviceCaps(dc, RASTERCAPS) & RC_PALETTE) != 0;
  ReleaseDC(NULL, dc);

  SetColorFormat();
}

void
ReloadColorFormat(void)
{
  SetColorFormat();
}

Bool
ColorLevelSettable(void)
{
  return True;			/* the image is remade for every connection */
}

const char *
ColorModeName(void)
{
  static char name[16];

  if (activeLevel == COLOR_FULL)
    sprintf(name, "%dbit", visdepth);
  else
    sprintf(name, "bgr%d%d%d", Bits(myFormat.blueMax),
	    Bits(myFormat.greenMax), Bits(myFormat.redMax));

  return name;
}

/* the color cube of a reduced level, by the pixel the server sends */
void
W32ColorTable(RGBQUAD *table)
{
  int i;

  for (i = 0; i < 256; i++) {
    int r = (i >> myFormat.redShift) & myFormat.redMax;
    int g = (i >> myFormat.greenShift) & myFormat.greenMax;
    int b = (i >> myFormat.blueShift) & myFormat.blueMax;

    if (i >> myFormat.depth)
      r = g = b = 0;		/* past the end of the cube */

    table[i].rgbRed = (BYTE)(myFormat.redMax ? r * 255 / myFormat.redMax : 0);
    table[i].rgbGreen = (BYTE)(myFormat.greenMax ?
			       g * 255 / myFormat.greenMax : 0);
    table[i].rgbBlue = (BYTE)(myFormat.blueMax ?
			      b * 255 / myFormat.blueMax : 0);
    table[i].rgbReserved = 0;
  }
}

void
W32SelectPalette(HDC dc)
{
  if (w32Palette) {
    SelectPalette(dc, w32Palette, FALSE);
    RealizePalette(dc);
  }
}
