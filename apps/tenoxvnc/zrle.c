/*
 *  Copyright (C) 2026 tenoxvnc project.
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
 * zrle.c - handle ZRLE encoding, backported from TigerVNC/RealVNC.
 *
 * This file is #included by rfbproto.c (once, not per-BPP).  Pixels are
 * treated as opaque byte strings in the client's requested format, so a
 * single decoder handles 8, 16 and 32 bits per pixel, including the
 * compressed 3-byte "CPIXEL" form used for 32bpp with depth <= 24.
 */

#define rfbZRLETileWidth 64
#define rfbZRLETileHeight 64

static z_stream zrleStream;
static Bool zrleStreamActive = False;
static char *zrleBuf = NULL;
static int zrleBufSize = 0;
static Bool zrleReported = False;
static CARD8 zrleTile[rfbZRLETileWidth * rfbZRLETileHeight * 4];

static void
ResetZRLEState(void)
{
  if (zrleStreamActive) {
    inflateEnd(&zrleStream);
    zrleStreamActive = False;
  }
  zrleReported = False;
}

/*
 * Store one pixel, and fill a run of them.
 *
 * These used to be memcpy(dst, src, bytesPP) per pixel.  bytesPP is a
 * variable, so that can never be inlined - it was a libc call for every
 * pixel of every tile, 4095 of them to fill one solid 64x64 tile.
 */

#define ZRLE_PUT(dst, src) \
  { \
    (dst)[0] = (src)[0]; \
    if (bytesPP > 1) { \
      (dst)[1] = (src)[1]; \
      if (bytesPP > 2) { \
	(dst)[2] = (src)[2]; \
	(dst)[3] = (src)[3]; \
      } \
    } \
  }

static void
ZrleFill(CARD8 *dst, const CARD8 *src, int n, int bytesPP)
{
  CARD8 a, b, c, d;

  if (bytesPP == 1) {
    a = src[0];
    while (n-- > 0)
      *dst++ = a;
    return;
  }

  if (bytesPP == 2) {
    a = src[0]; b = src[1];
    while (n-- > 0) {
      *dst++ = a; *dst++ = b;
    }
    return;
  }

  a = src[0]; b = src[1]; c = src[2]; d = src[3];
  while (n-- > 0) {
    *dst++ = a; *dst++ = b; *dst++ = c; *dst++ = d;
  }
}

/*
 * Read one run length (byte 255 adds 255 and continues; total is sum + 1).
 */

#define ZRLE_RUN_LENGTH(len) \
  { \
    CARD8 _b; \
    (len) = 1; \
    do { \
      if (p >= end) goto corrupt; \
      _b = *p++; \
      (len) += _b; \
    } while (_b == 255); \
  }

/*
 * Read one CPIXEL into dst (cpixelBytes bytes from the stream, expanded to
 * bytesPP bytes of client pixel data).
 */

#define ZRLE_CPIXEL(dst) \
  { \
    if (p + cpixelBytes > end) goto corrupt; \
    if (cpixelBytes == bytesPP) { \
      ZRLE_PUT((dst), p); \
    } else if (cpixelLS != myFormat.bigEndian) { \
      /* color bytes first, pad byte last */ \
      (dst)[0] = p[0]; (dst)[1] = p[1]; (dst)[2] = p[2]; (dst)[3] = 0; \
    } else { \
      /* pad byte first, color bytes last */ \
      (dst)[0] = 0; (dst)[1] = p[0]; (dst)[2] = p[1]; (dst)[3] = p[2]; \
    } \
    p += cpixelBytes; \
  }

static Bool
HandleZRLE(int rx, int ry, int rw, int rh)
{
  rfbZlibHeader hdr;
  int remaining, toRead, err;
  int bytesPP, cpixelBytes;
  Bool cpixelLS = False;
  CARD8 *p, *end;
  int tx, ty, tw, th;

  bytesPP = myFormat.bitsPerPixel / 8;

  /* Decide the CPIXEL size: 3 bytes when 32bpp, depth <= 24 and all color
     bits live in three consecutive bytes at one end of the pixel. */

  cpixelBytes = bytesPP;
  if (myFormat.bitsPerPixel == 32 && myFormat.depth <= 24) {
    CARD32 colorBits = ((CARD32)myFormat.redMax << myFormat.redShift) |
			((CARD32)myFormat.greenMax << myFormat.greenShift) |
			((CARD32)myFormat.blueMax << myFormat.blueShift);
    if ((colorBits & 0xFF000000) == 0) {
      cpixelBytes = 3;
      cpixelLS = True;		/* color in least significant 3 bytes */
    } else if ((colorBits & 0x000000FF) == 0) {
      cpixelBytes = 3;
      cpixelLS = False;		/* color in most significant 3 bytes */
    }
  }

  if (!zrleReported) {
    zrleReported = True;
    fprintf(stderr, "ZRLE using %d-byte cpixels\n", cpixelBytes);
  }

  if (!ReadFromRFBServer((char *)&hdr, sz_rfbZlibHeader))
    return False;

  remaining = Swap32IfLE(hdr.nBytes);

  /* Make sure the inflate buffer is large enough for the whole rect: worst
     case is plain RLE with single-pixel runs (pixel + length byte each),
     plus a little per-tile overhead. */

  {
    size_t tileWorstCase;
    size_t toReadSize;

    if (!RfbMulSize((size_t)rw, (size_t)rh, (size_t)(bytesPP + 1),
		    &tileWorstCase)) {
      fprintf(stderr, "ZRLE: rectangle pixel data size overflow\n");
      return False;
    }
    if (!RfbCheckAddSize(tileWorstCase, 65536, &toReadSize)) {
      fprintf(stderr, "ZRLE: inflate buffer size overflow\n");
      return False;
    }
    if (toReadSize > RFB_MAX_ALLOC_SIZE) {
      fprintf(stderr, "ZRLE: inflate buffer too large\n");
      return False;
    }
    toRead = (int)toReadSize;
  }

  if (zrleBufSize < toRead) {
    if (zrleBuf != NULL)
      free(zrleBuf);
    zrleBufSize = toRead;
    zrleBuf = malloc(zrleBufSize);
    if (zrleBuf == NULL) {
      zrleBufSize = 0;
      fprintf(stderr, "ZRLE: out of memory\n");
      return False;
    }
  }

  if (!zrleStreamActive) {
    zrleStream.zalloc = Z_NULL;
    zrleStream.zfree = Z_NULL;
    zrleStream.opaque = Z_NULL;
    if (inflateInit(&zrleStream) != Z_OK) {
      fprintf(stderr, "ZRLE: inflateInit failed\n");
      return False;
    }
    /* The RFB stream is framed and TCP already checksums it, so skip the
       adler32 zlib would otherwise run over every decompressed byte. */
    inflateValidate(&zrleStream, 0);
    zrleStreamActive = True;
  }

  /* Inflate the entire rect into zrleBuf.  The zlib stream persists across
     rects for the lifetime of the connection. */

  zrleStream.next_out = (Bytef *)zrleBuf;
  zrleStream.avail_out = zrleBufSize;

  while (remaining > 0) {
    char *inPtr;
    unsigned int inLen;

    /* Inflate straight out of the socket buffer, no staging copy. */
    if (!ReadFromRFBServerPeek(&inPtr, (unsigned int)remaining, &inLen))
      return False;

    zrleStream.next_in = (Bytef *)inPtr;
    zrleStream.avail_in = inLen;

    err = inflate(&zrleStream, Z_SYNC_FLUSH);
    if (err != Z_OK && err != Z_STREAM_END && err != Z_BUF_ERROR) {
      fprintf(stderr, "ZRLE: inflate error: %d\n", err);
      return False;
    }
    if (zrleStream.avail_in > 0 && zrleStream.avail_out == 0) {
      fprintf(stderr, "ZRLE: inflate buffer overflow\n");
      return False;
    }

    toRead = (int)(inLen - zrleStream.avail_in);
    if (toRead <= 0) {
      fprintf(stderr, "ZRLE: inflate made no progress\n");
      return False;
    }
    ReadFromRFBServerSkip((unsigned int)toRead);
    remaining -= toRead;
  }

  p = (CARD8 *)zrleBuf;
  end = (CARD8 *)zrleBuf + (zrleBufSize - zrleStream.avail_out);

  STATS(vncStats.zrleIn += Swap32IfLE(hdr.nBytes));
  STATS(vncStats.zrleOut += (double)(end - p));

  /* Walk the 64x64 tiles. */

  for (ty = 0; ty < rh; ty += rfbZRLETileHeight) {
    th = rh - ty;
    if (th > rfbZRLETileHeight)
      th = rfbZRLETileHeight;

    for (tx = 0; tx < rw; tx += rfbZRLETileWidth) {
      CARD8 subenc;
      CARD8 palette[128 * 4];
      int i, npixels;

      STATS(vncStats.zrleTiles++);

      tw = rw - tx;
      if (tw > rfbZRLETileWidth)
	tw = rfbZRLETileWidth;

      npixels = tw * th;

      if (p >= end) goto corrupt;
      subenc = *p++;

      if (subenc == 0) {
	/* raw pixels */
	CARD8 *t = zrleTile;

	for (i = 0; i < npixels; i++, t += bytesPP)
	  ZRLE_CPIXEL(t);

      } else if (subenc == 1) {
	/* solid color tile */
	ZRLE_CPIXEL(&zrleTile[0]);
	ZrleFill(&zrleTile[bytesPP], &zrleTile[0], npixels - 1, bytesPP);

      } else if (subenc >= 2 && subenc <= 16) {
	/* packed palette */
	int psize = subenc;
	int bppal, x, y, nbits;
	CARD8 b;

	for (i = 0; i < psize; i++)
	  ZRLE_CPIXEL(&palette[i * 4]);

	bppal = (psize > 4) ? 4 : ((psize > 2) ? 2 : 1);

	/* walked with a pointer: the indexed form cost two multiplies per
	   pixel and no compiler may hoist them, since the tile and the
	   palette are both CARD8 * and so alias by definition */

	for (y = 0; y < th; y++) {
	  CARD8 *t = zrleTile + y * tw * bytesPP;

	  nbits = 0;
	  b = 0;
	  for (x = 0; x < tw; x++, t += bytesPP) {
	    if (nbits == 0) {
	      if (p >= end) goto corrupt;
	      b = *p++;
	      nbits = 8;
	    }
	    nbits -= bppal;
	    i = (b >> nbits) & ((1 << bppal) - 1);
	    if (i >= psize) goto corrupt;
	    ZRLE_PUT(t, &palette[i * 4]);
	  }
	}

      } else if (subenc == 128) {
	/* plain RLE */
	int len;
	CARD8 pix[4];

	i = 0;
	while (i < npixels) {
	  ZRLE_CPIXEL(pix);
	  ZRLE_RUN_LENGTH(len);
	  if (i + len > npixels) goto corrupt;
	  ZrleFill(&zrleTile[i * bytesPP], pix, len, bytesPP);
	  i += len;
	}

      } else if (subenc >= 130) {
	/* palette RLE */
	int psize = subenc - 128;
	int len;
	CARD8 idx;

	for (i = 0; i < psize; i++)
	  ZRLE_CPIXEL(&palette[i * 4]);

	i = 0;
	while (i < npixels) {
	  if (p >= end) goto corrupt;
	  idx = *p++;
	  if (idx < 128) {
	    len = 1;
	  } else {
	    idx -= 128;
	    ZRLE_RUN_LENGTH(len);
	  }
	  if (idx >= psize) goto corrupt;
	  if (i + len > npixels) goto corrupt;
	  ZrleFill(&zrleTile[i * bytesPP], &palette[idx * 4], len, bytesPP);
	  i += len;
	}

      } else {
	fprintf(stderr, "ZRLE: unknown subencoding %d\n", (int)subenc);
	return False;
      }

      CopyDataToImage((char *)zrleTile, rx + tx, ry + ty, tw, th);
    }

    /* One request per row of tiles rather than per tile.  The small
       rectangles that dominate are a single row, so this costs the same as
       one request for the whole rectangle, while a tall rectangle still
       paints as it decodes instead of arriving in one late jump. */

    PutImageRect(rx, ry + ty, rw, th);
  }

  return True;

corrupt:
  fprintf(stderr, "ZRLE: corrupt data received\n");
  return False;
}
