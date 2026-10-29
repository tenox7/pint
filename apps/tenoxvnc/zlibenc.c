/*
 *  Copyright (C) 2000 Tridia Corporation.  All Rights Reserved.
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
 * zlib.c - handle zlib encoding.
 *
 * This file shouldn't be compiled directly.  It is included multiple times by
 * rfbproto.c, each time with a different definition of the macro BPP.  For
 * each value of BPP, this file defines a function which handles an zlib
 * encoded rectangle with BPP bits per pixel.
 */

#define HandleZlibBPP CONCAT2E(HandleZlib,BPP)
#define CARDBPP CONCAT2E(CARD,BPP)

static Bool
HandleZlibBPP (int rx, int ry, int rw, int rh)
{
  rfbZlibHeader hdr;
  int remaining;
  int inflateResult;
  int toRead;
  size_t needSize;

  /* First make sure we have a large enough raw buffer to hold the
   * decompressed data.  In practice, with a fixed BPP, fixed frame
   * buffer size and the first update containing the entire frame
   * buffer, this buffer allocation should only happen once, on the
   * first update.
   */
  if (!RfbMulSize((size_t)rw, (size_t)rh, (size_t)(BPP / 8), &needSize)) {
    fprintf(stderr, "Zlib: rectangle pixel data size overflow\n");
    return False;
  }
  if (needSize > RFB_MAX_ALLOC_SIZE) {
    fprintf(stderr, "Zlib: rectangle pixel data too large\n");
    return False;
  }

  /* raw_buffer_size is -1 until the first allocation, so test the sign
     before comparing as size_t. */
  if (raw_buffer_size < 0 || (size_t)raw_buffer_size < needSize) {

    if (raw_buffer != NULL) {

      free(raw_buffer);

    }

    raw_buffer_size = (int)needSize;
    raw_buffer = (char*) malloc(needSize);
    if (raw_buffer == NULL) {
      raw_buffer_size = -1;
      fprintf(stderr, "Zlib: out of memory\n");
      return False;
    }

  }

  if (!ReadFromRFBServer((char *)&hdr, sz_rfbZlibHeader))
    return False;

  remaining = Swap32IfLE(hdr.nBytes);

  STATS(vncStats.zlibIn += remaining);
  STATS(vncStats.zlibOut += (double)rw * rh * (BPP / 8));

  /* Need to initialize the decompressor state. */
  decompStream.next_in   = Z_NULL;
  decompStream.avail_in  = 0;
  decompStream.next_out  = ( Bytef * )raw_buffer;
  decompStream.avail_out = raw_buffer_size;
  decompStream.data_type = Z_BINARY;

  /* Initialize the decompression stream structures on the first invocation. */
  if ( decompStreamInited == False ) {

    inflateResult = inflateInit( &decompStream );

    if ( inflateResult != Z_OK ) {
      fprintf(stderr,
              "inflateInit returned error: %d, msg: %s\n",
              inflateResult,
              decompStream.msg);
      return False;
    }

    /* The RFB stream is framed and TCP already checksums it, so skip the
       adler32 zlib would otherwise run over every decompressed byte. */
    inflateValidate( &decompStream, 0 );

    decompStreamInited = True;

  }

  inflateResult = Z_OK;

  /* Process buffer full of data until no more to process, or
   * some type of inflater error, or Z_STREAM_END.
   */
  while (( remaining > 0 ) &&
         ( inflateResult == Z_OK )) {

    char *inPtr;
    unsigned int inLen;

    /* Inflate straight out of the socket buffer, no staging copy. */
    if (!ReadFromRFBServerPeek(&inPtr, (unsigned int)remaining, &inLen))
      return False;

    decompStream.next_in  = ( Bytef * )inPtr;
    decompStream.avail_in = inLen;

    /* Need to uncompress buffer full. */
    inflateResult = inflate( &decompStream, Z_SYNC_FLUSH );

    /* We never supply a dictionary for compression. */
    if ( inflateResult == Z_NEED_DICT ) {
      fprintf(stderr,"zlib inflate needs a dictionary!\n");
      return False;
    }
    if ( inflateResult < 0 ) {
      fprintf(stderr,
              "zlib inflate returned error: %d, msg: %s\n",
              inflateResult,
              decompStream.msg);
      return False;
    }

    /* Result buffer allocated to be at least large enough.  We should
     * never run out of space!
     */
    if (( decompStream.avail_in > 0 ) &&
        ( decompStream.avail_out <= 0 )) {
      fprintf(stderr,"zlib inflate ran out of space!\n");
      return False;
    }

    toRead = (int)(inLen - decompStream.avail_in);
    if ( toRead <= 0 ) {
      fprintf(stderr,"zlib inflate made no progress!\n");
      return False;
    }
    ReadFromRFBServerSkip((unsigned int)toRead);
    remaining -= toRead;

  } /* while ( remaining > 0 ) */

  if ( inflateResult == Z_OK ) {

    /* Put the uncompressed contents of the update on the screen. */
    CopyDataToScreen(raw_buffer, rx, ry, rw, rh);

  }
  else {

    fprintf(stderr,
            "zlib inflate returned error: %d, msg: %s\n",
            inflateResult,
            decompStream.msg);
    return False;

  }

  return True;
}
