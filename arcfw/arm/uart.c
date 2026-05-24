//
// PL011 UART0 (serial0) - the loader's serial console / debug line. On QEMU the
// PL011 is usable from reset; on real hardware GPIO14/15 must be muxed to the UART
// and the baud rate programmed. uart_init() does the full real-hardware sequence and
// is also harmless on QEMU, so it runs unconditionally at startup (main.c cmain).
//
// Register offsets and the init sequence follow the working U-Boot RPi2 PL011 driver
// (drivers/serial/serial_pl01x.c) and the BCM2836 GPIO doc. Baud is fixed for a
// 48 MHz UART reference clock (the firmware's clock when enable_uart=1 in config.txt,
// which also pins core_freq so the rate does not drift): 48e6/(16*115200) = 26.04 ->
// IBRD 26, FBRD round(0.04*64) = 3.
//
#define UART0   0x3F201000u
#define UART_DR   (*(volatile unsigned int *)(UART0 + 0x00))
#define UART_FR   (*(volatile unsigned int *)(UART0 + 0x18))
#define UART_IBRD (*(volatile unsigned int *)(UART0 + 0x24))
#define UART_FBRD (*(volatile unsigned int *)(UART0 + 0x28))
#define UART_LCRH (*(volatile unsigned int *)(UART0 + 0x2C))
#define UART_CR   (*(volatile unsigned int *)(UART0 + 0x30))
#define UART_IMSC (*(volatile unsigned int *)(UART0 + 0x38))
#define UART_ICR  (*(volatile unsigned int *)(UART0 + 0x44))

#define FR_TXFF   0x20u
#define FR_RXFE   0x10u
#define LCRH_FEN  0x10u
#define LCRH_WLEN8 0x60u
#define CR_UARTEN 0x001u
#define CR_TXE    0x100u
#define CR_RXE    0x200u

#define GPIO_BASE 0x3F200000u
#define GPFSEL1   (*(volatile unsigned int *)(GPIO_BASE + 0x04))
#define GPPUD     (*(volatile unsigned int *)(GPIO_BASE + 0x94))
#define GPPUDCLK0 (*(volatile unsigned int *)(GPIO_BASE + 0x98))

static void short_delay(int n)
{
    while (n-- > 0)
        __asm__ volatile ("nop");
}

//
// Full PL011 bring-up: mux GPIO14/15 to ALT0 (TXD0/RXD0), neutralize their pulls,
// then disable the UART, set 115200-8N1 with FIFOs, mask interrupts (we poll), and
// enable TX+RX. Idempotent and safe to call on QEMU (where it is effectively a
// no-op beyond what reset already provides).
//
void uart_init(void)
{
    unsigned int r;

    r = GPFSEL1;
    r &= ~((7u << 12) | (7u << 15));        // clear FSEL14, FSEL15
    r |=  ((4u << 12) | (4u << 15));        // ALT0 (0b100) for both
    GPFSEL1 = r;

    GPPUD = 0;                              // no pull-up/down
    short_delay(150);
    GPPUDCLK0 = (1u << 14) | (1u << 15);    // clock it into 14/15
    short_delay(150);
    GPPUDCLK0 = 0;

    UART_CR = 0;                            // disable while configuring
    UART_ICR = 0x7FF;                       // clear all pending interrupts
    UART_IBRD = 26;                         // 115200 @ 48 MHz
    UART_FBRD = 3;
    UART_LCRH = LCRH_WLEN8 | LCRH_FEN;      // 8N1, FIFOs on
    UART_IMSC = 0;                          // polled: mask every interrupt
    UART_CR = CR_UARTEN | CR_TXE | CR_RXE;
}

void uart_putc(int c)
{
    while (UART_FR & FR_TXFF)
        ;
    UART_DR = (unsigned int)(unsigned char)c;
}

void uart_puts(const char *s)
{
    for (; *s; s++) {
        if (*s == '\n')
            uart_putc('\r');
        uart_putc(*s);
    }
}

// RX side, for the ARC console-input vector (AERead/AEReadStatus).
// uart_rx_ready() reflects the RXFE (receive-FIFO-empty) flag; uart_getc()
// blocks until a byte arrives. Raw bytes only - no echo, no CR/LF rewriting.

int uart_rx_ready(void)
{
    return (UART_FR & FR_RXFE) == 0;
}

int uart_getc(void)
{
    while (UART_FR & FR_RXFE)
        ;
    return (int)(UART_DR & 0xff);
}
