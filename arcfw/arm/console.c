//
// Unified loader console - mirrors every byte to both the PL011 serial line and the
// HDMI framebuffer console. Serial stays live so headless QEMU runs and real-hardware
// bring-up keep a text log; the framebuffer is the visible output on a monitor.
//
// All loader output funnels through here: BlPrint (clib.c) and the ArcWrite backend
// AEWrite (arcemul.c) both call console_putc/console_puts instead of the raw uart_*
// helpers. fbcon_putc is a no-op until the framebuffer is initialized, so calling
// this before fb_init() simply prints to serial only.
//
void uart_putc(int c);
void fbcon_putc(int c);

void console_putc(int c)
{
    if (c == '\n')
        uart_putc('\r');        // serial wants CRLF; the loader emits bare \n
    uart_putc(c);
    fbcon_putc(c);
}

void console_puts(const char *s)
{
    for (; *s; s += 1)
        console_putc((unsigned char)*s);
}
