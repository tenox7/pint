//
// Kernel-side HAL display - ported from PRIVATE/NTOS/NTHALS/HALFXS/MIPS/JXDISP.C
// (HalDisplayString / HalpDisplayCharacter / HalpOutputCharacter / a gutted
// HalpInitializeDisplay0). This is the genuine NT routine the kernel's HAL uses to
// put text on the boot screen; it is what makes a real-NT-code "hello world" appear
// on HDMI, instead of the stand-in kernel bit-banging a UART.
//
// Two architecture deltas vs. the verbatim MIPS source, both forced and both local:
//
//  (1) MMU off, no JAZZ G364. The MIPS HAL maps VIDEO_MEMORY_BASE through a PDE/TB
//      entry, programs a G364 controller, and walks the ARC configuration tree for
//      the monitor geometry. The Pi has none of that: it runs MMU-off (identity) and
//      its VideoCore framebuffer is already allocated and configured by the firmware
//      emulator (arcfw/arm/fb.c via the mailbox). So HalpInitializeDisplay0 is gutted
//      to keep only its data-flow shape - take the font from LoaderBlock->OemFontFile
//      and the framebuffer base/geometry from the loader block's ARM fields - and
//      HalDisplayString drops the IRQL raise + TB/PTE video mapping (nothing to map).
//
//  (2) 8 bpp palette -> 32 bpp direct color. The G364 framebuffer is one palette byte
//      per pixel (HalpOutputCharacter writes index 0 = foreground, 1 = background; the
//      scroll fills with 1). The VC framebuffer is 32-bpp packed color, so the
//      destination is PULONG, the per-pixel store writes a packed foreground/background
//      color, and the scanline stride is the granted pitch (in pixels) rather than the
//      horizontal resolution (the VC may pad the pitch). The glyph-decode arithmetic
//      and the cursor/scroll logic are otherwise verbatim.
//

#include "kernel.h"

//
// PIXORDER from fb.c, passed through the loader block. Used to pack colors the way
// the VideoCore granted, so blue is blue on both RGB and BGR framebuffers.
//
#define PIXORDER_BGR 0
#define PIXORDER_RGB 1

//
// Display variables (verbatim names from JXDISP.C, plus the framebuffer/pitch/color
// state the 32-bpp VC backend needs in place of VIDEO_MEMORY_BASE + the palette).
//
POEM_FONT_FILE_HEADER HalpFontHeader;
ULONG HalpBytesPerRow;
ULONG HalpCharacterHeight;
ULONG HalpCharacterWidth;
ULONG HalpColumn;
ULONG HalpRow;
ULONG HalpDisplayText;
ULONG HalpDisplayWidth;
ULONG HalpScrollLine;
ULONG HalpScrollLength;

PULONG HalpFrameBuffer;             // VC framebuffer base (was VIDEO_MEMORY_BASE)
ULONG  HalpPitchPixels;             // scanline stride in pixels (was HorizontalResolution)
ULONG  HalpForeground;              // packed fg color  (was palette index 0)
ULONG  HalpBackground;              // packed bg color  (was palette index 1)

VOID HalpDisplayCharacter(UCHAR Character);
VOID HalpOutputCharacter(PUCHAR Glyph);

//
// memmove for the scroll (the MIPS HAL calls RtlMoveMemory). Copies forward when the
// destination is below the source (the scroll-up case), which is all we use.
//
static VOID
HalpMoveMemory(PVOID Dst, PVOID Src, ULONG Length)
{
    PUCHAR d = (PUCHAR)Dst;
    PUCHAR s = (PUCHAR)Src;
    ULONG i;

    if (d <= s) {
        for (i = 0; i < Length; i += 1)
            d[i] = s[i];
    } else {
        for (i = Length; i > 0; i -= 1)
            d[i - 1] = s[i - 1];
    }
}

static ULONG
HalpPackColor(ULONG Order, ULONG Red, ULONG Green, ULONG Blue)
{
    if (Order == PIXORDER_RGB)
        return (Blue << 16) | (Green << 8) | Red;
    return (Red << 16) | (Green << 8) | Blue;
}

BOOLEAN
HalpInitializeDisplay0(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
    )

/*++

Routine Description:

    This routine sets the address of the font file header, reads the framebuffer
    geometry the OS loader supplied, computes the character output display
    parameters, and clears the screen.

    N.B. The MIPS original (HalpInitializeDisplay0) additionally maps the video
         memory + control registers via a PDE entry, programs the G364 display
         controller, and finds the monitor geometry in the ARC configuration tree.
         Under the ARM port the framebuffer is already mapped (MMU off) and
         configured (firmware emulator), so only the font + geometry data flow and
         the screen clear remain.

Arguments:

    LoaderBlock - Supplies a pointer to the loader parameter block.

Return Value:

    TRUE if a usable framebuffer + font were supplied, FALSE otherwise.

--*/

{
    POEM_FONT_FILE_HEADER FontHeader;
    ULONG VerticalResolution;
    ULONG HorizontalResolution;
    ULONG Index;
    ULONG Pixels;

    FontHeader = (POEM_FONT_FILE_HEADER)LoaderBlock->OemFontFile;
    HalpFrameBuffer = (PULONG)(unsigned long)LoaderBlock->Arm.FrameBuffer;

    if (FontHeader == NULL || HalpFrameBuffer == NULL)
        return FALSE;

    //
    // Set the address of the font file header and compute display variables.
    //
    HalpFontHeader = FontHeader;
    HalpBytesPerRow = (FontHeader->PixelWidth + 7) / 8;
    HalpCharacterHeight = FontHeader->PixelHeight;
    HalpCharacterWidth = FontHeader->PixelWidth;

    //
    // Take the monitor geometry from the loader block (firmware-emulator framebuffer)
    // rather than the ARC configuration tree the MIPS HAL walks.
    //
    HorizontalResolution = LoaderBlock->Arm.FrameBufferWidth;
    VerticalResolution   = LoaderBlock->Arm.FrameBufferHeight;
    HalpPitchPixels      = LoaderBlock->Arm.FrameBufferPitch / sizeof(ULONG);

    HalpForeground = HalpPackColor(LoaderBlock->Arm.FrameBufferPixelOrder,
                                   0xFF, 0xFF, 0xFF);            // white
    HalpBackground = HalpPackColor(LoaderBlock->Arm.FrameBufferPixelOrder,
                                   0x00, 0x00, 0xAA);            // classic NT blue

    //
    // Compute character output display parameters.
    //
    // N.B. HalpScrollLine and the glyph scanline stride use the framebuffer pitch
    //      (in pixels) so a padded pitch is handled; HalpDisplayWidth uses the visible
    //      width so text still wraps at the right edge of the screen.
    //
    HalpDisplayText  = VerticalResolution / HalpCharacterHeight;
    HalpScrollLine   = HalpPitchPixels * HalpCharacterHeight;
    HalpScrollLength = HalpScrollLine * (HalpDisplayText - 1);
    HalpDisplayWidth = HorizontalResolution / HalpCharacterWidth;

    //
    // Clear the screen to the background color and home the cursor.
    //
    Pixels = HalpPitchPixels * VerticalResolution;
    for (Index = 0; Index < Pixels; Index += 1)
        HalpFrameBuffer[Index] = HalpBackground;

    HalpColumn = 0;
    HalpRow = 0;
    return TRUE;
}

VOID
HalDisplayString (
    PUCHAR String
    )

/*++

Routine Description:

    This routine displays a character string on the display screen.

    N.B. The MIPS original raises IRQL, takes the display adapter spin lock, and
         maps the frame buffer into the current address space via the page tables.
         The ARM port runs MMU-off and single-threaded with the framebuffer already
         identity-mapped, so all of that is unnecessary - the routine reduces to the
         output loop.

Arguments:

    String - Supplies a pointer to the characters that are to be displayed.

Return Value:

    None.

--*/

{
    if (HalpFrameBuffer == NULL)
        return;

    //
    // Display characters until a null byte is encountered.
    //
    while (*String != 0) {
        HalpDisplayCharacter(*String++);
    }

    return;
}

VOID
HalpDisplayCharacter (
    IN UCHAR Character
    )

/*++

Routine Description:

    This routine displays a character at the current x and y positions in
    the frame buffer. If a newline is encounter, then the frame buffer is
    scrolled. If characters extend below the end of line, then they are not
    displayed.

Arguments:

    Character - Supplies a character to be displayed.

Return Value:

    None.

--*/

{
    PULONG Destination;
    ULONG Index;

    //
    // If the character is a newline, then scroll the screen up, blank the
    // bottom line, and reset the x position.
    //
    if (Character == '\n') {
        HalpColumn = 0;
        if (HalpRow < (HalpDisplayText - 1)) {
            HalpRow += 1;

        } else {
            HalpMoveMemory((PVOID)HalpFrameBuffer,
                           (PVOID)(HalpFrameBuffer + HalpScrollLine),
                           HalpScrollLength * sizeof(ULONG));

            Destination = HalpFrameBuffer + HalpScrollLength;
            for (Index = 0; Index < HalpScrollLine; Index += 1) {
                *Destination++ = HalpBackground;
            }
        }

    } else if (Character == '\r') {
        HalpColumn = 0;

    } else {
        if ((Character < HalpFontHeader->FirstCharacter) ||
            (Character > HalpFontHeader->LastCharacter)) {
            Character = HalpFontHeader->DefaultCharacter;
        }

        Character -= HalpFontHeader->FirstCharacter;
        HalpOutputCharacter((PUCHAR)HalpFontHeader + HalpFontHeader->Map[Character].Offset);
    }

    return;
}

VOID
HalpOutputCharacter(
    IN PUCHAR Glyph
    )

/*++

Routine Description:

    This routine insert a set of pixels into the display at the current x
    cursor position. If the current x cursor position is at the end of the
    line, then a newline is displayed before the specified character.

Arguments:

    Glyph - Supplies a pointer to the glyph bitmap to be displayed.

Return Value:

    None.

--*/

{
    PULONG Destination;
    ULONG FontValue;
    ULONG I;
    ULONG J;

    //
    // If the current x cursor position is at the end of the line, then
    // output a line feed before displaying the character.
    //
    if (HalpColumn == HalpDisplayWidth) {
        HalpDisplayCharacter('\n');
    }

    //
    // Output the specified character and update the x cursor position.
    //
    Destination = HalpFrameBuffer +
                (HalpRow * HalpScrollLine) + (HalpColumn * HalpCharacterWidth);

    for (I = 0; I < HalpCharacterHeight; I += 1) {
        FontValue = 0;
        for (J = 0; J < HalpBytesPerRow; J += 1) {
            FontValue |= *(Glyph + (J * HalpCharacterHeight)) << (24 - (J * 8));
        }

        Glyph += 1;
        for (J = 0; J < HalpCharacterWidth ; J += 1) {
            *Destination++ = ((FontValue >> 31) ^ 1) ? HalpBackground : HalpForeground;
            FontValue <<= 1;
        }

        Destination += (HalpPitchPixels - HalpCharacterWidth);
    }

    HalpColumn += 1;
    return;
}
