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
 * corre.c - handle CoRRE encoding.
 *
 * This file shouldn't be compiled directly.  It is included multiple times by
 * rfbproto.c, each time with a different definition of the macro BPP.  For
 * each value of BPP, this file defines a function which handles a CoRRE
 * encoded rectangle with BPP bits per pixel.
 */

#define HandleCoRREBPP CONCAT2E(HandleCoRRE,BPP)
#define CARDBPP CONCAT2E(CARD,BPP)

#if (BPP == 8)
#define CORRE_PIXEL(p) (useColorMap ? colorToPixel[p] : (unsigned long)(p))
#else
#define CORRE_PIXEL(p) ((unsigned long)(p))
#endif

static Bool
HandleCoRREBPP (int rx, int ry, int rw, int rh)
{
    rfbRREHeader hdr;
    int i;
    CARDBPP pix;
    CARD8 *ptr;
    int x, y, w, h;

    if (!ReadFromRFBServer((char *)&hdr, sz_rfbRREHeader))
	return False;

    hdr.nSubrects = Swap32IfLE(hdr.nSubrects);

    {
      size_t subrectSize = 4 + (BPP / 8);
      size_t readSize;

      if (subrectSize == 0 || hdr.nSubrects > BUFFER_SIZE / subrectSize) {
	fprintf(stderr, "CoRRE: too many subrects (%lu)\n",
		(unsigned long)hdr.nSubrects);
	return False;
      }
      readSize = (size_t)hdr.nSubrects * subrectSize;
      if (readSize > BUFFER_SIZE) {
	fprintf(stderr, "CoRRE: subrect data too large (%lu bytes)\n",
		(unsigned long)readSize);
	return False;
      }
    }

    if (!ReadFromRFBServer((char *)&pix, sizeof(pix)))
	return False;

    FillImageRect(rx, ry, rw, rh, CORRE_PIXEL(pix));

    if (!ReadFromRFBServer(buffer, hdr.nSubrects * (4 + (BPP / 8))))
	return False;

    ptr = (CARD8 *)buffer;

    for (i = 0; i < (int) hdr.nSubrects; i++) {
	pix = *(CARDBPP *)ptr;
	ptr += BPP/8;
	x = *ptr++;
	y = *ptr++;
	w = *ptr++;
	h = *ptr++;

	FillImageRect(rx + x, ry + y, w, h, CORRE_PIXEL(pix));
    }

    PutImageRect(rx, ry, rw, rh);

    return True;
}

#undef CORRE_PIXEL
