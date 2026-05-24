//
// BCM2835/2836 VideoCore mailbox - property channel (channel 8).
//
// The VideoCore "GPU" controls SoC boot and hides much of the hardware behind a
// message protocol carried over the mailbox peripheral (0x3F00B880 on RPi2). The
// ARM writes the 16-byte-aligned physical address of a tag buffer to the mailbox;
// the VC processes it in place and writes the same address back. This is the one
// path to a framebuffer (no ARC/BIOS equivalent), so it is the ARM analog of the
// RISC firmware's display setup - see ARCHITECTURE.md "Two lineages".
//
// Register layout, status bits, channel, and the bus-address translation are taken
// verbatim from the working U-Boot RPi2 reference (arch/arm/mach-bcm283x/{mbox.c,
// mbox.h,phys2bus.c}): the buffer pointer is sent as the uncached VC alias
// (0xC0000000 | phys), and addresses the VC returns are masked back with
// ~0xC0000000. That is the form U-Boot uses and that QEMU's raspi2b mailbox honors.
//

#define MBOX_BASE    0x3F00B880u
#define MBOX_READ    (*(volatile unsigned int *)(MBOX_BASE + 0x00))
#define MBOX_STATUS  (*(volatile unsigned int *)(MBOX_BASE + 0x18))
#define MBOX_WRITE   (*(volatile unsigned int *)(MBOX_BASE + 0x20))

#define MBOX_STATUS_WR_FULL  0x80000000u
#define MBOX_STATUS_RD_EMPTY 0x40000000u

#define MBOX_CHAN_PROP 8u
#define MBOX_CHAN_MASK 0xfu

#define GPU_BUS_ALIAS 0xC0000000u   // uncached VC view of ARM RAM (BCM2836; u-boot phys2bus.c)

#define MBOX_RESP_SUCCESS 0x80000000u

//
// Convert an ARM physical address to the VC bus address the mailbox expects, and
// back. Identity-mapped RAM (MMU off) means the ARM physical address is just the
// pointer value; the alias makes the VC read it through its uncached window.
//
unsigned int mbox_phys_to_bus(unsigned int phys)
{
    return GPU_BUS_ALIAS | phys;
}

unsigned int mbox_bus_to_phys(unsigned int bus)
{
    return bus & ~GPU_BUS_ALIAS;
}

//
// Send a property-tag buffer (already populated, 16-byte aligned) on channel 8 and
// wait for the VC to complete it. Returns 0 on success, -1 if the VC reports a
// non-success header code. Mirrors bcm2835_mbox_call_raw + call_prop.
//
int mbox_prop_call(volatile unsigned int *buf)
{
    unsigned int phys = (unsigned int)(unsigned long)buf;
    unsigned int msg = (mbox_phys_to_bus(phys) & ~MBOX_CHAN_MASK) | MBOX_CHAN_PROP;
    unsigned int resp;

    while (MBOX_STATUS & MBOX_STATUS_WR_FULL)
        ;
    MBOX_WRITE = msg;

    for (;;) {
        while (MBOX_STATUS & MBOX_STATUS_RD_EMPTY)
            ;
        resp = MBOX_READ;
        if ((resp & MBOX_CHAN_MASK) == MBOX_CHAN_PROP)
            break;
    }

    return (buf[1] == MBOX_RESP_SUCCESS) ? 0 : -1;
}

//
// Ask the VideoCore to set a peripheral's power state (BCM2835 SET_POWER_STATE, tag
// 0x00028001). The Pi leaves the USB controller (device id 3, USB_HCD) powered OFF
// until asked - u-boot does exactly this in board_init() (board/raspberrypi/rpi/rpi.c)
// before it touches the DWC2 core. Without it the core answers register reads and even
// detects a port, but its PHY/transaction engine never runs, so host channels enable
// and never execute. ON|WAIT blocks until the rail is stable. Returns 0 on success.
//
#define MBOX_TAG_SET_POWER_STATE 0x00028001u
#define MBOX_POWER_STATE_ON      (1u << 0)
#define MBOX_POWER_STATE_WAIT    (1u << 1)

int mbox_set_power_state(unsigned int device_id, int on)
{
    static volatile unsigned int buf[8] __attribute__((aligned(16)));
    int i = 0;

    buf[i++] = 0;                          // total size in bytes (patched below)
    buf[i++] = 0;                          // request code
    buf[i++] = MBOX_TAG_SET_POWER_STATE;
    buf[i++] = 8;                          // value buffer size {device_id, state}
    buf[i++] = 8;                          // request length
    buf[i++] = device_id;
    buf[i++] = on ? (MBOX_POWER_STATE_ON | MBOX_POWER_STATE_WAIT) : MBOX_POWER_STATE_WAIT;
    buf[i++] = 0;                          // end tag
    buf[0] = (unsigned int)(i * 4);

    return mbox_prop_call(buf);
}
