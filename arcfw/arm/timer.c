//
// BCM2835/2836 system timer - real microsecond delays.
//
// The DWC2 USB bring-up (usb.c) needs honest delays: forcing host mode and
// resetting USB ports take tens of milliseconds, far longer than the calibrated
// NOP loops it used during early bring-up could reliably produce (those measured
// ~55 us on real silicon, so a "50 ms" reset was really microseconds - the
// real-HW host-mode switch never completed). This backs udelay/mdelay with the
// SoC's free-running 1 MHz counter instead.
//
// The system timer is a 64-bit 1 MHz free-running counter at 0x3F003000; CLO is
// its low 32 bits (0x3F003004). One tick = 1 us. 32-bit unsigned subtraction is
// wrap-safe (the counter wraps every ~71 min, but elapsed = now - start is always
// correct modulo 2^32 for any delay far short of that). Register base/offsets per
// the BCM2836 peripherals doc and u-boot's bcm283x system-timer use.
//
#define SYSTIMER_BASE 0x3F003000u
#define SYSTIMER_CLO  (*(volatile unsigned int *)(SYSTIMER_BASE + 0x04))

unsigned int timer_us(void)
{
    return SYSTIMER_CLO;
}

void udelay(unsigned int us)
{
    unsigned int start = SYSTIMER_CLO;

    while ((SYSTIMER_CLO - start) < us)
        ;
}

void mdelay(unsigned int ms)
{
    while (ms--)
        udelay(1000);
}
