#!/usr/bin/env python3
# Build an OEM_FONT_FILE_HEADER font blob (font.fon) from the 8x8 bitmaps in
# font8x8.h, for the kernel-side NT HAL display path (arcfw/kernel/jxdisp.c).
#
# The genuine NT HAL (NTHALS/.../JXDISP.C) renders text from a font the OS loader
# hands it via LoaderBlock->OemFontFile, in the GDI raster .FNT layout described by
# INC/HAL.H's OEM_FONT_FILE_HEADER (a "#include pshpack1.h" byte-packed struct):
#
#     [ header (118 bytes, up to and incl. Filler) ]
#     [ Map[FirstCharacter..LastCharacter]: {USHORT Width; USHORT Offset} ]
#     [ glyph bitmaps, Map[c].Offset bytes from the header start ]
#
# HalpOutputCharacter reads each glyph MSB-first (bit 7 = leftmost pixel), while
# font8x8.h stores bits LSB-first (bit 0 = leftmost; see fbcon.c). So every glyph
# byte is bit-reversed here; skip that and the text comes out mirrored.
#
# Usage: mkfont.py font8x8.h font.fon

import re
import struct
import sys

HEADER_SIZE = 118       # offsetof(OEM_FONT_FILE_HEADER, Map), pshpack1
GLYPH_H     = 8
GLYPH_W     = 8
NCHARS      = 128       # font8x8_basic covers U+0000..U+007F


def reverse_bits(b):
    r = 0
    for i in range(8):
        if b & (1 << i):
            r |= 1 << (7 - i)
    return r


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src) as f:
        text = f.read()

    bytes_ = [int(h, 16) for h in re.findall(r'0x([0-9a-fA-F]{2})', text)]
    need = NCHARS * GLYPH_H
    if len(bytes_) < need:
        sys.exit("mkfont: found %d font bytes in %s, need %d" % (len(bytes_), src, need))
    glyphs = [reverse_bits(b) for b in bytes_[:need]]

    map_size  = NCHARS * 4              # {USHORT Width; USHORT Offset} per char
    glyph_off = HEADER_SIZE + map_size
    file_size = glyph_off + need

    # Header fields in declaration order (pshpack1 -> no padding). Only PixelWidth/
    # PixelHeight/FirstCharacter/LastCharacter/DefaultCharacter and Map are read by
    # the HAL; the rest are cosmetic but kept for a well-formed .FNT header.
    h = b''
    h += struct.pack('<H', 0x0300)              # Version
    h += struct.pack('<I', file_size)           # FileSize
    h += b'NT35 ARM port - font8x8 (public domain)\x00'.ljust(60, b'\x00')  # Copyright[60]
    h += struct.pack('<H', 0)                   # Type (raster, in memory)
    h += struct.pack('<H', 8)                   # Points
    h += struct.pack('<H', 96)                  # VerticleResolution
    h += struct.pack('<H', 96)                  # HorizontalResolution
    h += struct.pack('<H', GLYPH_H)             # Ascent
    h += struct.pack('<H', 0)                   # InternalLeading
    h += struct.pack('<H', 0)                   # ExternalLeading
    h += struct.pack('<B', 0)                   # Italic
    h += struct.pack('<B', 0)                   # Underline
    h += struct.pack('<B', 0)                   # StrikeOut
    h += struct.pack('<H', 400)                 # Weight (normal)
    h += struct.pack('<B', 255)                 # CharacterSet (OEM)
    h += struct.pack('<H', GLYPH_W)             # PixelWidth
    h += struct.pack('<H', GLYPH_H)             # PixelHeight
    h += struct.pack('<B', 0)                   # Family
    h += struct.pack('<H', GLYPH_W)             # AverageWidth
    h += struct.pack('<H', GLYPH_W)             # MaximumWidth
    h += struct.pack('<B', 0)                   # FirstCharacter
    h += struct.pack('<B', NCHARS - 1)          # LastCharacter
    h += struct.pack('<B', ord('?'))            # DefaultCharacter
    h += struct.pack('<B', ord(' '))            # BreakCharacter
    h += struct.pack('<H', (GLYPH_W + 7) // 8)  # WidthInBytes
    h += struct.pack('<I', 0)                   # Device
    h += struct.pack('<I', 0)                   # Face
    h += struct.pack('<I', 0)                   # BitsPointer
    h += struct.pack('<I', glyph_off)           # BitsOffset
    h += struct.pack('<B', 0)                   # Filler
    if len(h) != HEADER_SIZE:
        sys.exit("mkfont: header is %d bytes, expected %d (field/pack mismatch)"
                 % (len(h), HEADER_SIZE))

    m = b''
    for c in range(NCHARS):
        m += struct.pack('<HH', GLYPH_W, glyph_off + c * GLYPH_H)  # {Width, Offset}

    g = bytes(glyphs)

    blob = h + m + g
    if len(blob) != file_size:
        sys.exit("mkfont: blob is %d bytes, FileSize says %d" % (len(blob), file_size))
    with open(dst, 'wb') as f:
        f.write(blob)
    print("mkfont: wrote %s (%d bytes, %d glyphs, Map@%d glyphs@%d)"
          % (dst, file_size, NCHARS, HEADER_SIZE, glyph_off))


if __name__ == '__main__':
    main()
