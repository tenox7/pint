//
// Minimal serial shell - the handoff-test payload and stand-in for the kernel.
//
// Freestanding ARMv7, no libc, its own PL011 I/O. Loaded into RAM by the NT ARC
// loader and entered via start.S. Its banner proves the loader -> loaded-code
// handoff works; its commands (peek/poke/dump/reboot) make it a live bring-up
// debugger over serial. This is NOT an NT kernel - none exists for ARM - it is
// the interactive stub that occupies the kernel slot for now. See ARCHITECTURE.md
// for the rationale.
//
typedef unsigned int  u32;
typedef unsigned char u8;

#define UART0   0x3F201000u
#define UART_DR (*(volatile u32 *)(UART0 + 0x00))
#define UART_FR (*(volatile u32 *)(UART0 + 0x18))
#define UART_CR (*(volatile u32 *)(UART0 + 0x30))
#define FR_TXFF 0x20u
#define FR_RXFE 0x10u
#define CR_UARTEN 0x001u
#define CR_TXE    0x100u
#define CR_RXE    0x200u

#define PM_BASE     0x3F100000u
#define PM_RSTC     (*(volatile u32 *)(PM_BASE + 0x1c))
#define PM_WDOG     (*(volatile u32 *)(PM_BASE + 0x24))
#define PM_PASSWORD 0x5a000000u

static void uart_putc(int c)
{
    while (UART_FR & FR_TXFF)
        ;
    UART_DR = (u32)(u8)c;
}

static void uart_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s);
    }
}

static int uart_getc(void)
{
    while (UART_FR & FR_RXFE)
        ;
    return (int)(UART_DR & 0xff);
}

static void put_hex(u32 v)
{
    static const char digits[] = "0123456789abcdef";
    int i;
    for (i = 28; i >= 0; i -= 4)
        uart_putc(digits[(v >> i) & 0xf]);
}

static int readline(char *buf, int max)
{
    int n = 0;
    for (;;) {
        int c = uart_getc();
        if (c == '\r' || c == '\n') {
            uart_putc('\r');
            uart_putc('\n');
            break;
        }
        if (c == 0x7f || c == 0x08) {            // DEL / Backspace
            if (n > 0) {
                n -= 1;
                uart_putc('\b');
                uart_putc(' ');
                uart_putc('\b');
            }
            continue;
        }
        if (c >= 0x20 && c < 0x7f && n < max - 1) {
            buf[n++] = (char)c;
            uart_putc(c);
        }
    }
    buf[n] = 0;
    return n;
}

static const char *skip_sp(const char *p)
{
    while (*p == ' ' || *p == '\t')
        p += 1;
    return p;
}

// Parse a hex token at *pp, advancing past it; returns 1 if any digit consumed.
static int parse_hex(const char **pp, u32 *out)
{
    const char *p = skip_sp(*pp);
    u32 v = 0;
    int any = 0;
    for (;;) {
        char c = *p;
        int d;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
        else break;
        v = (v << 4) | (u32)d;
        any = 1;
        p += 1;
    }
    *pp = p;
    *out = v;
    return any;
}

// Match a space/end-terminated command word at *pp; advance *pp past it if so.
static int is_cmd(const char **pp, const char *name)
{
    const char *p = skip_sp(*pp);
    const char *q = name;
    while (*q && *p == *q) {
        p += 1;
        q += 1;
    }
    if (*q == 0 && (*p == 0 || *p == ' ' || *p == '\t')) {
        *pp = p;
        return 1;
    }
    return 0;
}

static void cmd_dump(u32 addr, u32 count)
{
    u32 i;
    if (count == 0)
        count = 8;
    for (i = 0; i < count; i += 1) {
        if ((i & 3) == 0) {
            uart_putc('\n');
            put_hex(addr + i * 4);
            uart_puts(": ");
        }
        put_hex(*(volatile u32 *)(addr + i * 4));
        uart_putc(' ');
    }
    uart_putc('\n');
}

void shell_main(void)
{
    static char line[128];

    // Enable the PL011 for RX. TX works from QEMU's reset state (the banners
    // print), but RX bytes are only delivered when the UART is enabled, so set
    // UARTEN|TXE|RXE via OR (leaving any reset bits intact - never clears TX).
    UART_CR |= CR_UARTEN | CR_TXE | CR_RXE;

    uart_puts("\n*** ARM payload shell ***  (loaded by the NT ARC loader; kernel stand-in)\n");
    uart_puts("commands: help  peek <hex>  poke <hex> <hex>  dump <hex> [n]  reboot\n");

    for (;;) {
        const char *p = line;

        uart_puts("payload> ");
        readline(line, sizeof(line));

        if (is_cmd(&p, "help")) {
            uart_puts("help              this text\n");
            uart_puts("peek <hex>        read a 32-bit word\n");
            uart_puts("poke <hex> <hex>  write a 32-bit word\n");
            uart_puts("dump <hex> [n]    hexdump n words (default 8)\n");
            uart_puts("reboot            reset via the PM watchdog\n");
        } else if (is_cmd(&p, "peek")) {
            u32 a;
            if (parse_hex(&p, &a)) {
                put_hex(a);
                uart_puts(": ");
                put_hex(*(volatile u32 *)a);
                uart_putc('\n');
            } else {
                uart_puts("usage: peek <hex>\n");
            }
        } else if (is_cmd(&p, "poke")) {
            u32 a, v;
            if (parse_hex(&p, &a) && parse_hex(&p, &v)) {
                *(volatile u32 *)a = v;
                put_hex(a);
                uart_puts(" <- ");
                put_hex(v);
                uart_putc('\n');
            } else {
                uart_puts("usage: poke <hex> <hex>\n");
            }
        } else if (is_cmd(&p, "dump")) {
            u32 a, n;
            if (parse_hex(&p, &a)) {
                if (!parse_hex(&p, &n))
                    n = 0;
                cmd_dump(a, n);
            } else {
                uart_puts("usage: dump <hex> [n]\n");
            }
        } else if (is_cmd(&p, "reboot")) {
            uart_puts("rebooting...\n");
            PM_WDOG = PM_PASSWORD | 10;
            PM_RSTC = PM_PASSWORD | 0x20;
            for (;;)
                ;
        } else if (line[0]) {
            uart_puts("? unknown command (try 'help')\n");
        }
    }
}
