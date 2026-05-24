//
// Framebuffer text console - the HDMI analog of the RISC firmware's screen
// console (FW/MIPS/JXDISP.C), layered on the VC framebuffer (fb.c). Renders an
// 8x8 monochrome font (font8x8.h, public-domain IBM VGA bitmaps) as bright-white
// glyphs on a blue background, tracks a cursor, wraps at the right margin, and
// scrolls when the bottom is reached.
//
// It also interprets the ANSI/CSI control sequences the ARC console expects, so
// the arcdos shell's full-screen line editor works: cursor positioning (CSI H),
// erase-to-end-of-line (CSI K), erase-screen (CSI J), and reverse video (CSI 7m /
// CSI 0m). Both the 8-bit CSI introducer 0x9B (what arcdos's Ven* macros emit) and
// the 7-bit ESC '[' form are recognized. The cursor (cur_col/cur_row) is the
// authoritative position GetDisplayStatus reports - tracked even with no monitor
// attached, so the editor still positions correctly over a bare serial line.
// The state machine mirrors JXDISP.C's DisplayWrite; the cursor convention matches
// it exactly (0-based internally, GetDisplayStatus returns +1, CSI H subtracts 1).
//
// Colors are built with fb_pack() (fb.c), which honors the pixel order the VC
// granted - read back in fb_init() - so the blue stays blue and the text stays
// white whether the framebuffer is RGB or BGR, on QEMU and on real hardware.
//
#include "string.h"
#include "font8x8.h"

int fb_ok(void);
void fb_fill(unsigned int rgb);
void fb_putpixel(unsigned int x, unsigned int y, unsigned int rgb);
unsigned int fb_pack(unsigned int r, unsigned int g, unsigned int b);
extern unsigned int *fb_base;
extern unsigned int fb_width, fb_height, fb_pitch;

//
// 8x8 glyphs drawn 1:1 (no scaling) - 240x135 characters at 1920x1080.
//
#define GLYPH_W 8
#define GLYPH_H 8
#define SCALE   1
#define CELL_W  (GLYPH_W * SCALE)
#define CELL_H  (GLYPH_H * SCALE)

// Console size fallback when no framebuffer is present (serial-only): keeps the
// cursor math and GetDisplayStatus sane so the arcdos editor still works headless.
#define DEFAULT_COLS 80
#define DEFAULT_ROWS 25

// Foreground/background pixels, packed for the granted channel order in fbcon_init().
static unsigned int fg, bg;

static unsigned int g_cols, g_rows;     // console size in cells
static unsigned int cur_col, cur_row;

// ANSI/CSI parser state.
static int esc_seen;                    // ESC received, awaiting '['
static int in_csi;                      // inside a CSI ... <final> sequence
static int inverse;                     // reverse-video (CSI 7m) for drawn glyphs
#define CSI_MAX_PARAM 8
static int param[CSI_MAX_PARAM];
static int pcount;                      // index of the current parameter

//
// Scroll the text area up by one cell row and clear the vacated bottom row. Moves
// raw scanlines through the pitch (>= width*4), so a padded pitch is handled.
//
static void fbcon_scroll(void)
{
    unsigned int stride = fb_pitch >> 2;            // pixels per scanline
    unsigned int shift  = CELL_H * stride;          // pixels in one cell row
    unsigned int total  = g_rows * CELL_H * stride; // pixels in the text area
    unsigned int i;

    if (!fb_ok())
        return;
    memmove(fb_base, fb_base + shift, (total - shift) * sizeof(unsigned int));
    for (i = total - shift; i < total; i += 1)
        fb_base[i] = bg;
}

static void draw_glyph(unsigned int cx, unsigned int cy, unsigned char ch)
{
    const unsigned char *glyph = (const unsigned char *)font8x8_basic[ch & 0x7F];
    unsigned int px = cx * CELL_W;
    unsigned int py = cy * CELL_H;
    unsigned int efg = inverse ? bg : fg;       // reverse video swaps fg/bg
    unsigned int ebg = inverse ? fg : bg;
    int row, col, sy, sx;

    if (!fb_ok())
        return;
    for (row = 0; row < GLYPH_H; row += 1) {
        unsigned char bits = glyph[row];
        for (col = 0; col < GLYPH_W; col += 1) {
            unsigned int color = (bits & (1u << col)) ? efg : ebg;   // bit0 = leftmost
            for (sy = 0; sy < SCALE; sy += 1)
                for (sx = 0; sx < SCALE; sx += 1)
                    fb_putpixel(px + col * SCALE + sx, py + row * SCALE + sy, color);
        }
    }
}

void fbcon_init(void)
{
    fg = fb_pack(0xFF, 0xFF, 0xFF);
    bg = fb_pack(0x00, 0x00, 0xAA);     // classic NT loader blue
    if (fb_ok()) {
        g_cols = fb_width / CELL_W;
        g_rows = fb_height / CELL_H;
        fb_fill(bg);
    } else {
        g_cols = DEFAULT_COLS;          // serial-only: keep cursor math valid
        g_rows = DEFAULT_ROWS;
    }
    cur_col = 0;
    cur_row = 0;
    esc_seen = 0;
    in_csi = 0;
    inverse = 0;
}

//
// Advance to the next line, scrolling if already on the bottom row.
//
static void next_line(void)
{
    cur_col = 0;
    if (cur_row + 1 >= g_rows) {
        fbcon_scroll();
        cur_row = g_rows ? g_rows - 1 : 0;
    } else {
        cur_row += 1;
    }
}

//
// Erase part of the current line by drawing background-colored cells, leaving the
// cursor where it was. mode 0 = cursor..end (the only one arcdos uses), 1 =
// start..cursor, 2 = whole line. Matches JXDISP.C's CSI K handling.
//
static void erase_in_line(int mode)
{
    unsigned int save = cur_col;
    unsigned int x;
    unsigned int from = (mode == 1) ? 0 : cur_col;
    unsigned int to   = (mode == 0) ? (g_cols ? g_cols - 1 : 0) :
                        (mode == 1) ? cur_col : (g_cols ? g_cols - 1 : 0);

    for (x = from; x <= to && x < g_cols; x += 1)
        draw_glyph(x, cur_row, ' ');
    cur_col = save;
}

//
// Final byte of a CSI sequence: cursor moves (H/f, A/B/C/D), erase (K/J), and
// Select Graphic Rendition (m). Only the subset arcdos emits is acted on; other
// finals just end the sequence. Mirrors JXDISP.C DisplayWrite.
//
static void csi_dispatch(int final)
{
    int n, i;

    switch (final) {
    case 'H':
    case 'f': {
        int row = param[0] ? param[0] - 1 : 0;
        int col = (pcount >= 1 && param[1]) ? param[1] - 1 : 0;
        if (row >= (int)g_rows) row = g_rows ? (int)g_rows - 1 : 0;
        if (col >= (int)g_cols) col = g_cols ? (int)g_cols - 1 : 0;
        cur_row = (unsigned)row;
        cur_col = (unsigned)col;
        break;
    }
    case 'A':
        n = param[0] ? param[0] : 1;
        cur_row = ((unsigned)n > cur_row) ? 0 : cur_row - n;
        break;
    case 'B':
        n = param[0] ? param[0] : 1;
        cur_row += n;
        if (cur_row >= g_rows) cur_row = g_rows ? g_rows - 1 : 0;
        break;
    case 'C':
        n = param[0] ? param[0] : 1;
        cur_col += n;
        if (cur_col >= g_cols) cur_col = g_cols ? g_cols - 1 : 0;
        break;
    case 'D':
        n = param[0] ? param[0] : 1;
        cur_col = ((unsigned)n > cur_col) ? 0 : cur_col - n;
        break;
    case 'K':
        erase_in_line(param[0]);
        break;
    case 'J':
        if (fb_ok()) fb_fill(bg);       // arcdos uses CSI 2J (whole screen)
        cur_col = 0;
        cur_row = 0;
        break;
    case 'm':
        for (i = 0; i <= pcount; i += 1) {
            if (param[i] == 0) inverse = 0;        // attributes off
            else if (param[i] == 7) inverse = 1;   // reverse video
            // 1 (intensity), 4 (underscore), 30-47 (color): fixed scheme, ignored
        }
        break;
    default:
        break;
    }
}

//
// Emit one character. Interprets ESC/CSI control sequences (cursor, erase, SGR)
// and the C0 controls; printable ASCII draws a glyph and advances, wrapping at the
// right margin and scrolling at the bottom. Cursor state is maintained even with
// no framebuffer (draw_glyph/scroll self-no-op), so GetDisplayStatus stays correct.
//
void fbcon_putc(int c)
{
    c &= 0xFF;

    //
    // Inside a CSI: accumulate decimal parameters separated by ';', then dispatch
    // on the first non-digit/non-';' (the final byte).
    //
    if (in_csi) {
        if (c >= '0' && c <= '9') {
            param[pcount] = param[pcount] * 10 + (c - '0');
            return;
        }
        if (c == ';') {
            if (pcount < CSI_MAX_PARAM - 1) {
                pcount += 1;
                param[pcount] = 0;
            }
            return;
        }
        csi_dispatch(c);
        in_csi = 0;
        return;
    }

    if (esc_seen) {
        esc_seen = 0;
        if (c == '[') {
            in_csi = 1;
            pcount = 0;
            param[0] = 0;
        }
        return;
    }

    switch (c) {
    case 0x1b:                          // ESC - start of 7-bit sequence
        esc_seen = 1;
        return;
    case 0x9b:                          // 8-bit CSI (arcdos's ASCII_CSI)
        in_csi = 1;
        pcount = 0;
        param[0] = 0;
        return;
    case '\n':
        next_line();
        return;
    case '\r':
        cur_col = 0;
        return;
    case '\t':
        cur_col = (cur_col + 8) & ~7u;
        if (g_cols && cur_col >= g_cols) cur_col = g_cols - 1;
        return;
    case '\b':
        if (cur_col > 0) cur_col -= 1;
        return;
    default:
        if (c < 0x20 || c > 0x7E)
            return;
        if (g_cols && cur_col >= g_cols)
            next_line();
        draw_glyph(cur_col, cur_row, (unsigned char)c);
        cur_col += 1;
        return;
    }
}

//
// Report the cursor position and screen extent for the ArcGetDisplayStatus backend
// (arcfw/arm/arcemul.c). All values are 0-based here; the caller adds 1 to match
// the ARC 1-based convention, exactly as JXDISP.C's FwGetDisplayStatus does.
//
void fbcon_status(unsigned int *col, unsigned int *row,
                  unsigned int *maxcol, unsigned int *maxrow)
{
    *col = cur_col;
    *row = cur_row;
    *maxcol = g_cols ? g_cols - 1 : DEFAULT_COLS - 1;
    *maxrow = g_rows ? g_rows - 1 : DEFAULT_ROWS - 1;
}
