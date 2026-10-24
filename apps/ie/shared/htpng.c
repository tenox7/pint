/*
   PNG images (Windows), decoded with stb_image.h (public domain, cut down
   to PNG). Like htxbm.c, the whole file is collected, then decoded.
 */

#include "all.h"

#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

extern DWORD vga_colors[16];    /* bitmaps.c */

/*      Stream Object
   **       ------------
 */

#define BLOCK_SIZE  32768

struct _HTStream
{
    CONST HTStreamClass *isa;
    HTRequest *request;
    int count;
    int expected_length;
    int space;
    unsigned char *data;
    struct Mwin *tw;
};

/*
    x_PNGToImage: the decoded pixels (RGBA, top row first) in the form IE
    keeps full color images, the same as JPEGs (see htjpeg.c): bottom row
    first, rows padded to 4 bytes; on a true color screen 3 bytes a pixel,
    blue-green-red; with 256 colors the index of a color of the global
    6x6x6 cube, dithered; with 16 colors a VGA color, dithered.
    Transparent parts show the window background, like transparent GIFs.
*/
PRIVATE unsigned char *x_PNGToImage(unsigned char *rgba, int width, int height)
{
    COLORREF bg;
    int bgc[3];
    int bpp;                        /* bytes per pixel */
    long rowbytes;
    unsigned char *image, *in, *out;
    int *err, *cur, *next, *t;      /* dithering errors (x16), this row and the next */
    int x, y, i, a, e, best;
    int c[3], q[3];
    long d, dbest;

    bg = PREF_GetBackgroundColor();
    bgc[0] = GetRValue(bg);
    bgc[1] = GetGValue(bg);
    bgc[2] = GetBValue(bg);

    bpp = (wg.eColorMode == 8 || wg.eColorMode == 4) ? 1 : 3;
    rowbytes = ((long) width * bpp + 3) & ~3L;
    image = GTR_CALLOC(rowbytes * height, 1);
    err = GTR_CALLOC(2 * (width + 2) * 3, sizeof(int));
    if (!image || !err)
    {
        if (image)
            GTR_FREE(image);
        if (err)
            GTR_FREE(err);
        return NULL;
    }
    cur = err;
    next = err + (width + 2) * 3;

    for (y = 0; y < height; y++)
    {
        in = rgba + (long) y * width * 4;
        out = image + (long) (height - 1 - y) * rowbytes;
        for (x = 0; x < width; x++, in += 4)
        {
            a = in[3];
            for (i = 0; i < 3; i++)
                c[i] = (in[i] * a + bgc[i] * (255 - a) + 127) / 255;

            if (bpp == 3)
            {
                *out++ = c[2];
                *out++ = c[1];
                *out++ = c[0];
                continue;
            }
            if (wg.eColorMode == 8 && a == 0)
            {
                *out++ = BACKGROUND_COLOR_INDEX;
                continue;
            }

            /* Floyd-Steinberg: add the error left by the pixels before */
            for (i = 0; i < 3; i++)
            {
                c[i] += cur[(x + 1) * 3 + i] / 16;
                if (c[i] < 0)
                    c[i] = 0;
                if (c[i] > 255)
                    c[i] = 255;
            }
            if (wg.eColorMode == 8)
            {
                /* the cube's levels are 0, 51 .. 255 (see w_pal.c) */
                for (i = 0; i < 3; i++)
                    q[i] = (c[i] + 25) / 51;
                *out++ = (unsigned char) (q[0] * GREEN_COLOR_LEVELS * BLUE_COLOR_LEVELS
                                          + q[1] * BLUE_COLOR_LEVELS + q[2]);
                for (i = 0; i < 3; i++)
                    q[i] *= 51;
            }
            else
            {
                best = 0;
                dbest = LONG_MAX;
                for (i = 0; i < 16; i++)
                {
                    d = (long) (c[0] - GetRValue(vga_colors[i])) * (c[0] - GetRValue(vga_colors[i]))
                      + (long) (c[1] - GetGValue(vga_colors[i])) * (c[1] - GetGValue(vga_colors[i]))
                      + (long) (c[2] - GetBValue(vga_colors[i])) * (c[2] - GetBValue(vga_colors[i]));
                    if (d < dbest)
                    {
                        dbest = d;
                        best = i;
                    }
                }
                *out++ = (unsigned char) best;
                q[0] = GetRValue(vga_colors[best]);
                q[1] = GetGValue(vga_colors[best]);
                q[2] = GetBValue(vga_colors[best]);
            }

            /* pass the error on: 7/16 right, 3/16 down left, 5/16 down, 1/16 down right */
            for (i = 0; i < 3; i++)
            {
                e = c[i] - q[i];
                cur[(x + 2) * 3 + i] += e * 7;
                next[x * 3 + i] += e * 3;
                next[(x + 1) * 3 + i] += e * 5;
                next[(x + 2) * 3 + i] += e;
            }
        }
        t = cur;
        cur = next;
        next = t;
        memset(next, 0, (width + 2) * 3 * sizeof(int));
    }

    GTR_FREE(err);
    return image;
}

/*
    ReadPNG: a whole PNG file to an image as x_PNGToImage makes it
    (also used by the image viewer, winview.c)
*/
unsigned char *ReadPNG(unsigned char *pMem, long len, long *w, long *h)
{
    unsigned char *rgba, *image;
    int width, height, n;

    rgba = stbi_load_from_memory(pMem, (int) len, &width, &height, &n, 4);
    if (!rgba)
        return NULL;
    image = x_PNGToImage(rgba, width, height);
    stbi_image_free(rgba);
    *w = width;
    *h = height;
    return image;
}

PRIVATE BOOL HTPNG_write(HTStream * me, CONST char *s, int l)
{
    if (me->data && me->count + l > me->space)
    {
        unsigned char *p;
        int space;

        space = me->space * 2;
        if (space < me->count + l)
            space = me->count + l;
        p = (unsigned char *) GTR_MALLOC(space);
        if (p)
            memcpy(p, me->data, me->count);
        GTR_FREE(me->data);
        me->data = p;
        me->space = space;
    }
    if (me->data)
        memcpy(me->data + me->count, s, l);
    me->count += l;
    if (me->expected_length)
        WAIT_SetTherm(me->tw, me->count);
    return (me->data) ? TRUE : FALSE;
}

PRIVATE BOOL HTPNG_put_character(HTStream * me, char c)
{
    return HTPNG_write(me, &c, 1);
}

PRIVATE BOOL HTPNG_put_string(HTStream * me, CONST char *s)
{
    /* Should never get called */
    return FALSE;
}

PRIVATE void HTPNG_free(HTStream * me)
{
    unsigned char *image;
    long width, height;

    if (!me->data)
    {
        Image_SetImageData(me->request, NULL, 0, IMG_NOTLOADED, NULL, -1, 0);
        GTR_FREE(me);
        return;
    }

    image = ReadPNG(me->data, me->count, &width, &height);
    if (image)
        Image_SetImageData(me->request, image, width, height, NULL, -1, IMG_JPEG);
    else
        Image_SetImageData(me->request, NULL, 0, IMG_ERROR, NULL, -1, 0);

    GTR_FREE(me->data);
    GTR_FREE(me);
}

PRIVATE void HTPNG_abort(HTStream * me, HTError e)
{
    if (me->data)
        GTR_FREE(me->data);
    Image_SetImageData(me->request, NULL, 0, (e != HTERROR_CANCELLED) ? IMG_ERROR : IMG_NOTLOADED, NULL, -1, 0);
    GTR_FREE(me);
}


/*  Image stream
   **   ----------
 */
PRIVATE CONST HTStreamClass HTPNGClass =
{
    "PNG",
    SID_HTPNG_RECEIVING_INLINE_S,
    SID_HTPNG_RECEIVING_INLINE_S_S,
    NULL,
    HTPNG_free,
    HTPNG_abort,
    HTPNG_put_character, HTPNG_put_string,
    HTPNG_write
};


/*  Image creation
 */
PUBLIC HTStream *Image_PNG(struct Mwin *tw, HTRequest * request, void *param, HTFormat input_format, HTFormat output_format, HTStream * output_stream)
{
    HTStream *me = (HTStream *) GTR_MALLOC(sizeof(*me));

    if (!me)
        return NULL;

    me->isa = &HTPNGClass;
    me->request = request;
    me->expected_length = request->content_length;
    if (me->expected_length)
    {
        WAIT_SetRange(tw, 0, 100, me->expected_length);
        me->space = me->expected_length + 1;
    }
    else
        me->space = BLOCK_SIZE;
    me->data = (unsigned char *) GTR_MALLOC(me->space);
    me->count = 0;
    me->tw = tw;
    return me;
}
