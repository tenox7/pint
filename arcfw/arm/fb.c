//
// VideoCore framebuffer (HDMI console backend).
//
// Allocates a 32-bpp linear framebuffer from the VC via the mailbox property
// channel (mailbox.c) and exposes it as a pixel surface. A text console is layered
// on top in fbcon.c. This is the "video(0)monitor(0)" half of the console that the
// x86 loader has and the headless serial port lacks - see ARCHITECTURE.md §1.
//
// The request shape (set physical+virtual size, depth, pixel order, allocate, then
// read pitch in one combined message) follows U-Boot's bcm2835 set_video_params
// (arch/arm/mach-bcm283x/msg.c). The returned buffer address is a VC bus address
// and is converted to an ARM physical pointer with mbox_bus_to_phys().
//

int mbox_prop_call(volatile unsigned int *buf);
unsigned int mbox_bus_to_phys(unsigned int bus);

//
// Display geometry is fixed at 1920x1080. The VC may clamp the request to the
// attached panel, so the granted width/height/pitch are always read back from
// the reply.
//
#define FB_WIDTH   1920u
#define FB_HEIGHT  1080u
#define FB_BPP     32u

#define TAG_SET_PHYS_WH   0x00048003u
#define TAG_SET_VIRT_WH   0x00048004u
#define TAG_SET_DEPTH     0x00048005u
#define TAG_SET_PIXORDER  0x00048006u
#define TAG_ALLOC_BUFFER  0x00040001u
#define TAG_GET_PITCH     0x00040008u

#define PIXORDER_BGR 0u
#define PIXORDER_RGB 1u

//
// Granted framebuffer geometry, filled by fb_init(). fb_base is an ARM-physical
// pointer (MMU off => usable directly). Zero width means fb_init() has not run or
// failed; callers must check fb_ok() before drawing.
//
unsigned int *fb_base;
unsigned int  fb_width;
unsigned int  fb_height;
unsigned int  fb_pitch;     // bytes per scanline (>= width*4)
unsigned int  fb_size;
unsigned int  fb_order;     // granted pixel order: PIXORDER_RGB or PIXORDER_BGR

// 16-byte-aligned property buffer (mailbox requires the low 4 bits free).
static volatile unsigned int mbox[36] __attribute__((aligned(16)));

int fb_ok(void)
{
    return fb_base != 0 && fb_width != 0 && fb_height != 0;
}

//
// Allocate and configure the framebuffer. Returns 0 on success. One combined
// property message: set physical/virtual size + depth + pixel order, allocate the
// buffer, and query the resulting pitch. The granted pixel order is read back so
// fb_pack() can place color channels correctly on both RGB and BGR framebuffers.
//
int fb_init(void)
{
    unsigned int req_w = FB_WIDTH, req_h = FB_HEIGHT;
    int i = 0;

    mbox[i++] = 0;                  // total size in bytes (patched below)
    mbox[i++] = 0;                  // request code

    int phys_idx = i;
    mbox[i++] = TAG_SET_PHYS_WH;
    mbox[i++] = 8;                  // value buffer size
    mbox[i++] = 8;                  // request length
    mbox[i++] = req_w;
    mbox[i++] = req_h;

    mbox[i++] = TAG_SET_VIRT_WH;
    mbox[i++] = 8;
    mbox[i++] = 8;
    mbox[i++] = req_w;
    mbox[i++] = req_h;

    mbox[i++] = TAG_SET_DEPTH;
    mbox[i++] = 4;
    mbox[i++] = 4;
    mbox[i++] = FB_BPP;

    int order_idx = i;
    mbox[i++] = TAG_SET_PIXORDER;
    mbox[i++] = 4;
    mbox[i++] = 4;
    mbox[i++] = PIXORDER_RGB;

    int alloc_idx = i;
    mbox[i++] = TAG_ALLOC_BUFFER;
    mbox[i++] = 8;                  // value buffer holds {base,size} on reply
    mbox[i++] = 4;                  // request length: just the alignment
    mbox[i++] = 16;                 // requested alignment; reply overwrites -> base
    mbox[i++] = 0;                  // reply -> size

    int pitch_idx = i;
    mbox[i++] = TAG_GET_PITCH;
    mbox[i++] = 4;
    mbox[i++] = 0;
    mbox[i++] = 0;                  // reply -> pitch

    mbox[i++] = 0;                  // end tag
    mbox[0] = (unsigned int)(i * 4);

    if (mbox_prop_call(mbox) != 0)
        return -1;

    fb_width  = mbox[phys_idx + 3];
    fb_height = mbox[phys_idx + 4];
    fb_order  = mbox[order_idx + 3];
    fb_base   = (unsigned int *)(unsigned long)mbox_bus_to_phys(mbox[alloc_idx + 3]);
    fb_size   = mbox[alloc_idx + 4];
    fb_pitch  = mbox[pitch_idx + 3];

    if (fb_base == 0 || fb_width == 0 || fb_pitch == 0)
        return -1;

    return 0;
}

//
// Pack 8-bit r/g/b into a 32-bpp pixel for the granted channel order: PIXORDER_RGB
// puts red in the low byte (byte 0 = R), PIXORDER_BGR puts blue there. White is the
// same value either way. fb_order is read back from the VC in fb_init().
//
unsigned int fb_pack(unsigned int r, unsigned int g, unsigned int b)
{
    if (fb_order == PIXORDER_RGB)
        return (b << 16) | (g << 8) | r;
    return (r << 16) | (g << 8) | b;
}

//
// Pixel/area helpers. Pitch is in bytes, so index by (pitch/4) per row to handle a
// pitch wider than width*4.
//
void fb_putpixel(unsigned int x, unsigned int y, unsigned int rgb)
{
    if (x >= fb_width || y >= fb_height)
        return;
    fb_base[y * (fb_pitch >> 2) + x] = rgb;
}

void fb_fill(unsigned int rgb)
{
    unsigned int stride = fb_pitch >> 2;
    unsigned int x, y;
    for (y = 0; y < fb_height; y += 1)
        for (x = 0; x < fb_width; x += 1)
            fb_base[y * stride + x] = rgb;
}

void fb_fill_rect(unsigned int x0, unsigned int y0, unsigned int w, unsigned int h,
                  unsigned int rgb)
{
    unsigned int x, y;
    for (y = 0; y < h; y += 1)
        for (x = 0; x < w; x += 1)
            fb_putpixel(x0 + x, y0 + y, rgb);
}
