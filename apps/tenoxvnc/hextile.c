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
 * hextile.c - handle hextile encoding.
 *
 * This file shouldn't be compiled directly.  It is included multiple times by
 * rfbproto.c, each time with a different definition of the macro BPP.  For
 * each value of BPP, this file defines a function which handles a hextile
 * encoded rectangle with BPP bits per pixel.
 */

#define HandleHextileBPP CONCAT2E(HandleHextile,BPP)
#define CARDBPP CONCAT2E(CARD,BPP)
#define GET_PIXEL CONCAT2E(GET_PIXEL,BPP)

/*
 * Every tile is built in the local image and a row of tiles goes to the
 * screen in one request.  Drawing it with XFillRectangle instead cost an
 * XChangeGC and an XFillRectangle for every subrect - a full screen of
 * tiles is thousands of requests - and left the image stale, since nothing
 * the server sent as Hextile ever reached it.
 */

#if (BPP == 8)
#define HEXTILE_PIXEL(p) (useColorMap ? colorToPixel[p] : (unsigned long)(p))
#else
#define HEXTILE_PIXEL(p) ((unsigned long)(p))
#endif

static Bool
HandleHextileBPP (int rx, int ry, int rw, int rh)
{
  CARDBPP bg, fg;
  int i;
  CARD8 *ptr;
  int x, y, w, h;
  int sx, sy, sw, sh;
  CARD8 subencoding;
  CARD8 nSubrects;

  for (y = ry; y < ry+rh; y += 16) {
    h = 16;
    if (ry+rh - y < 16)
      h = ry+rh - y;

    for (x = rx; x < rx+rw; x += 16) {
      w = 16;
      if (rx+rw - x < 16)
	w = rx+rw - x;

      if (!ReadFromRFBServer((char *)&subencoding, 1))
	return False;

      if (subencoding & rfbHextileRaw) {
	if (!ReadFromRFBServer(buffer, w * h * (BPP / 8)))
	  return False;

	CopyDataToImage(buffer, x, y, w, h);
	continue;
      }

      if (subencoding & rfbHextileBackgroundSpecified)
	if (!ReadFromRFBServer((char *)&bg, sizeof(bg)))
	  return False;

      FillImageRect(x, y, w, h, HEXTILE_PIXEL(bg));

      if (subencoding & rfbHextileForegroundSpecified)
	if (!ReadFromRFBServer((char *)&fg, sizeof(fg)))
	  return False;

      if (!(subencoding & rfbHextileAnySubrects)) {
	continue;
      }

      if (!ReadFromRFBServer((char *)&nSubrects, 1))
	return False;

      ptr = (CARD8 *)buffer;

      if (subencoding & rfbHextileSubrectsColored) {
	if (!ReadFromRFBServer(buffer, nSubrects * (2 + (BPP / 8))))
	  return False;

	for (i = 0; i < nSubrects; i++) {
	  GET_PIXEL(fg, ptr);
	  sx = rfbHextileExtractX(*ptr);
	  sy = rfbHextileExtractY(*ptr);
	  ptr++;
	  sw = rfbHextileExtractW(*ptr);
	  sh = rfbHextileExtractH(*ptr);
	  ptr++;
	  FillImageRect(x+sx, y+sy, sw, sh, HEXTILE_PIXEL(fg));
	}

      } else {
	unsigned long pixel;

	if (!ReadFromRFBServer(buffer, nSubrects * 2))
	  return False;

	pixel = HEXTILE_PIXEL(fg);

	for (i = 0; i < nSubrects; i++) {
	  sx = rfbHextileExtractX(*ptr);
	  sy = rfbHextileExtractY(*ptr);
	  ptr++;
	  sw = rfbHextileExtractW(*ptr);
	  sh = rfbHextileExtractH(*ptr);
	  ptr++;
	  FillImageRect(x+sx, y+sy, sw, sh, pixel);
	}
      }
    }

    PutImageRect(rx, y, rw, h);
  }

  return True;
}

#undef HEXTILE_PIXEL
