/*++

Module Name:

    usb.c

Abstract:

    This module implements the USB host of the Raspberry Pi 2 the keyboard boot
    driver (jxkbd.c) reads: the Synopsys DesignWare DWC2 OTG core at
    0x3F980000, with the register sequences of the U-Boot DWC2 driver. The root
    port of the core is a hub on both QEMU and the board (its LAN9514), so the
    hub is enumerated and its ports searched for a keyboard of the HID boot
    protocol, whose interrupt endpoint is then polled for reports; a full or
    low speed keyboard behind the high speed hub is reached with split
    transactions.

    The core masters the bus, so transfer buffers are given to it at their
    uncached VideoCore bus addresses, as the mailbox does.

--*/

#include "fwp.h"
#include "string.h"

#define POWER_DEVID_USB_HCD 3u     // BCM2835 mailbox power device id for the USB HCD

// ---- DWC2 registers (offsets from dwc2_core.h; subset we use) --------------

#define DWC2_BASE 0x3F980000u
#define REG(off)  (*(volatile unsigned int *)(DWC2_BASE + (off)))


#define GOTGCTL   REG(0x000)
#define GAHBCFG   REG(0x008)
#define GUSBCFG   REG(0x00C)
#define GRSTCTL   REG(0x010)
#define GINTSTS   REG(0x014)
#define GINTMSK   REG(0x018)
#define GRXFSIZ   REG(0x024)
#define GNPTXFSIZ REG(0x028)
#define GSNPSID   REG(0x040)
#define GHWCFG2   REG(0x048)
#define HPTXFSIZ  REG(0x100)
#define HCFG      REG(0x400)
#define HFNUM     REG(0x408)     // frame number/remaining (advances iff SOFs run)
#define HPRT0     REG(0x440)
#define PCGCCTL   REG(0xE00)     // power & clock gating control (struct offset 0xE00)

#define GAHBCFG_DMA_EN        (1u << 5)
#define GAHBCFG_GLBL_INTR_EN  (1u << 0)
#define GAHBCFG_HBSTLEN_INCR4 (3u << 1)     // AHB burst length field [4:1] = INCR4
#define GUSBCFG_FORCEHOSTMODE (1u << 29)
#define GUSBCFG_ULPI_CLK_SUSP_M (1u << 19)
#define GUSBCFG_ULPI_FS_LS    (1u << 17)
#define GUSBCFG_PHYSEL        (1u << 6)     // 1 = FS serial PHY; 0 = HS UTMI/ULPI PHY
#define GUSBCFG_ULPI_UTMI_SEL (1u << 4)     // 1 = ULPI, 0 = UTMI+
#define GUSBCFG_PHYIF16       (1u << 3)     // UTMI+ width: 1 = 16-bit, 0 = 8-bit
#define GRSTCTL_AHBIDLE       (1u << 31)
#define GRSTCTL_TXFFLSH       (1u << 5)
#define GRSTCTL_RXFFLSH       (1u << 4)
#define GRSTCTL_CSFTRST       (1u << 0)
#define GRSTCTL_TXFNUM_ALL    (0x10u << 6)  // flush-all encoding for TXFNUM
#define GINTSTS_CURMODE_HOST  (1u << 0)

#define HCFG_FSLSPCLKSEL_30_60 0u          // bits[1:0]: UTMI PHY 30/60 MHz FS/LS clock
#define HCFG_FSLSSUPP         (1u << 2)

#define HPRT0_PWR       (1u << 12)
#define HPRT0_RST       (1u << 8)
#define HPRT0_ENACHG    (1u << 3)
#define HPRT0_ENA       (1u << 2)
#define HPRT0_CONNDET   (1u << 1)
#define HPRT0_CONNSTS   (1u << 0)
#define HPRT0_SPD_SHIFT 17
#define HPRT0_SPD_MASK  (3u << 17)
// Write-1-to-clear bits (incl. ENA: writing it 1 *disables* the port). Mask these
// off before any read-modify-write of HPRT0 so we never clear them by accident.
#define HPRT0_W1C_MASK  (HPRT0_CONNDET | HPRT0_ENA | HPRT0_ENACHG | (1u << 5))

// Host channel N registers (0x500 + 0x20*N). We use channel 0 for everything.
#define HCCHAR(c)  REG(0x500 + 0x20 * (c))
#define HCSPLT(c)  REG(0x504 + 0x20 * (c))
#define HCINT(c)   REG(0x508 + 0x20 * (c))
#define HCTSIZ(c)  REG(0x510 + 0x20 * (c))
#define HCDMA(c)   REG(0x514 + 0x20 * (c))

#define HCCHAR_CHENA      (1u << 31)
#define HCCHAR_CHDIS      (1u << 30)
#define HCCHAR_ODDFRM     (1u << 29)
#define HCCHAR_MULTICNT_SH   20
#define HCCHAR_MULTICNT_MASK (3u << 20)
#define HCCHAR_EPTYPE_SH  18
#define HCCHAR_LSPDDEV    (1u << 17)
#define HCCHAR_EPDIR_IN   (1u << 15)
#define HCCHAR_EPNUM_SH   11
#define HCCHAR_DEVADDR_SH 22

#define HCINT_XFERCOMPL (1u << 0)
#define HCINT_CHHLTD    (1u << 1)
#define HCINT_STALL     (1u << 3)
#define HCINT_NAK       (1u << 4)
#define HCINT_ACK       (1u << 5)
#define HCINT_NYET      (1u << 6)
#define HCINT_XACTERR   (1u << 7)
#define HCINT_FRMOVRUN  (1u << 9)
#define HCINT_ALL       0x3fffu

// Split-transaction control (HCSPLT) + frame number (HFNUM) - for FS/LS devices behind
// a high-speed hub. Mirror u-boot dwc2_core.h: SPLTENA bit31, COMPSPLT bit16, hub addr
// in [13:7], hub port in [6:0]; HFNUM low 16 bits are the (micro)frame number.
#define HCSPLT_SPLTENA    (1u << 31)
#define HCSPLT_COMPSPLT   (1u << 16)
#define HCSPLT_HUBADDR_SH 7
#define HFNUM_FRNUM_MASK  0xFFFFu

#define DWC2_MAX_PACKET_COUNT  511u
#define DWC2_MAX_TRANSFER_SIZE 65535u

#define TSIZ_PID_SH     29
#define TSIZ_PKTCNT_SH  19
#define TSIZ_XFERSIZE_MASK 0x7ffffu

#define PID_DATA0 0u
#define PID_DATA1 2u
#define PID_SETUP 3u

#define EPTYPE_CONTROL 0u
#define EPTYPE_INTR    3u

// Link speed encoding - matches HPRT0_SPD (0 high / 1 full / 2 low), reused as the
// device-speed argument threaded through the transfer helpers: it sets LSPDDEV and
// drives the split-transaction decision.
#define USB_SPEED_HIGH 0u
#define USB_SPEED_FULL 1u
#define USB_SPEED_LOW  2u

// Transfer status, mirroring u-boot's chunk_msg/wait_for_chhltd returns: 0 = complete,
// XFER_AGAIN = NAK / frame-overrun / split-not-ready (caller retries the stage; an
// interrupt poll treats it as "no data yet"), XFER_ERR = stall / xact error / timeout.
#define XFER_AGAIN (-2)
#define XFER_ERR   (-1)

// ---- USB standard + hub-class + HID requests (USB 2.0 / HID 1.11 specs) -----

#define REQ_GET_STATUS     0x00
#define REQ_CLEAR_FEATURE  0x01
#define REQ_SET_FEATURE    0x03
#define REQ_SET_ADDRESS    0x05
#define REQ_GET_DESCRIPTOR 0x06
#define REQ_SET_CONFIG     0x09
#define REQ_SET_IDLE       0x0A     // HID class
#define REQ_SET_PROTOCOL   0x0B     // HID class

#define DESC_DEVICE 0x01
#define DESC_CONFIG 0x02
#define DESC_HUB    0x29

// Hub port features (USB 2.0 11.24.2) and wPortStatus bits (11.24.2.7.1).
#define PORT_FEAT_RESET        4
#define PORT_FEAT_POWER        8
#define PORT_FEAT_C_CONNECTION 16
#define PORT_FEAT_C_RESET      20
#define PORTSTAT_CONNECTION (1u << 0)
#define PORTSTAT_ENABLE     (1u << 1)
#define PORTSTAT_LOWSPEED   (1u << 9)
#define PORTSTAT_HIGHSPEED  (1u << 10)

// USB device classes / HID sub+protocol (boot-protocol keyboard).
#define CLASS_HID            0x03
#define HID_SUBCLASS_BOOT    0x01
#define HID_PROTOCOL_KEYBOARD 0x01

// Addresses we hand out: the hub on the root port, then the keyboard behind it.
#define ADDR_HUB 1u
#define ADDR_KBD 2u

// DMA buffers handed to the controller (bus master). 64-byte aligned; in BSS so they
// sit in identity RAM, reached via the uncached VC alias (MailboxPhysicalToBus).
static __declspec(align(64)) unsigned char setup_buf[8];
static __declspec(align(64)) unsigned char data_buf[256];
static __declspec(align(64)) unsigned char zlp_buf[4];
static __declspec(align(64)) unsigned char report_buf[8];
// Fixed DMA bounce buffer. u-boot never DMAs straight to the caller's buffer - every
// chunk goes through one aligned scratch buffer, then memcpy to/from the caller. We
// match that (it's the one structural thing the loader was NOT doing vs u-boot).
static __declspec(align(64)) unsigned char dma_bounce[512];

// Discovered keyboard, filled by enumeration and consumed by the HID poll. present=0
// until a boot-protocol keyboard is found and configured.
static struct {
    int          present;
    unsigned int addr;       // device address we assigned (ADDR_KBD)
    unsigned int speed;      // USB_SPEED_*
    unsigned int mps0;       // control endpoint 0 max packet size
    unsigned int ep;         // interrupt-IN endpoint number
    unsigned int ep_mps;     // interrupt-IN endpoint max packet size
    unsigned int toggle;     // interrupt-IN data toggle (PID_DATA0/PID_DATA1)
    unsigned int hub_addr;   // parent hub address (for future split transactions)
    unsigned int hub_port;   // parent hub port (1-based)
} kbd;

// ---- small helpers ---------------------------------------------------------

// Bounded wait for (REG & mask) to become (set?mask:0). Returns 1 on success, 0 on
// timeout - so a missing or wedged controller can never hang the loader. ~1 s bound
// (100000 x 10 us) on the real BCM2835 system timer (timer.c).
static int wait_bit(volatile unsigned int *reg, unsigned int mask, int set)
{
    int tries = 100000;
    while (tries-- > 0) {
        unsigned int v = *reg & mask;
        if (set ? (v == mask) : (v == 0))
            return 1;
        FwStallExecution(10);
    }
    return 0;
}

// ---- core + root port bring-up ---------------------------------------------

static int dwc2_core_reset(void)
{
    if (!wait_bit(&GRSTCTL, GRSTCTL_AHBIDLE, 1))
        return 0;
    GRSTCTL |= GRSTCTL_CSFTRST;
    if (!wait_bit(&GRSTCTL, GRSTCTL_CSFTRST, 0))   // self-clears when done
        return 0;
    FwStallExecution(10000);
    return 1;
}

static void dwc2_flush_fifos(void)
{
    GRSTCTL = GRSTCTL_TXFNUM_ALL | GRSTCTL_TXFFLSH;
    wait_bit(&GRSTCTL, GRSTCTL_TXFFLSH, 0);
    GRSTCTL = GRSTCTL_RXFFLSH;
    wait_bit(&GRSTCTL, GRSTCTL_RXFFLSH, 0);
}

// ---- transfers --------------------------------------------------------------

//
// The transfer engine below is a faithful port of u-boot's DWC2 host path
// (drivers/usb/host/dwc2.c): wait_for_chhltd -> transfer_chunk -> chunk_msg, with the
// SETUP/DATA/STATUS sequencing in usb_control. Everything runs on host channel 0 (one
// transfer in flight at a time), DMA'd through the dma_bounce scratch buffer as u-boot
// does. Caches are off, so the bounce is for structural parity, not coherency.
//

//
// Wait for the channel to halt, then classify from HCINT (u-boot wait_for_chhltd).
// *sub gets the residual transfer size and *pid the data toggle, both read back from
// HCTSIZ. Returns 0 (XFERCOMPL), XFER_AGAIN (NAK / frame-overrun - retry), or XFER_ERR
// (stall / xact error, or the halt never arrived).
//
static int wait_for_chhltd(unsigned int *sub, unsigned int *pid)
{
    unsigned int hcint, hctsiz;

    if (!wait_bit(&HCINT(0), HCINT_CHHLTD, 1))
        return XFER_ERR;                           // channel never halted (timeout)

    hcint  = HCINT(0);
    hctsiz = HCTSIZ(0);
    *sub = hctsiz & TSIZ_XFERSIZE_MASK;
    *pid = (hctsiz >> TSIZ_PID_SH) & 3;

    if (hcint & HCINT_XFERCOMPL)
        return 0;
    if (hcint & (HCINT_NAK | HCINT_FRMOVRUN))
        return XFER_AGAIN;
    return XFER_ERR;
}

//
// Program one packet group and run it (u-boot transfer_chunk). HCCHAR's device/ep/type
// fields are already set by chunk_msg; here we set HCTSIZ (size/count/pid) and the DMA
// address, clear HCINT, then enable the channel (MULTICNT=1, ODDFRM for periodic,
// CHENA, CHDIS clear). On an IN transfer *actual_len is len minus the HW residual.
//
static int transfer_chunk(unsigned int *pid, int dir_in, void *buffer,
                          unsigned int num_packets, unsigned int xfer_len,
                          unsigned int *actual_len, int odd_frame)
{
    unsigned int sub, hcchar;
    int ret;

    // OUT: stage the caller's data in the bounce buffer before the DMA.
    if (!dir_in && xfer_len)
        memcpy(dma_bounce, buffer, xfer_len);

    HCTSIZ(0) = ((*pid) << TSIZ_PID_SH) | (num_packets << TSIZ_PKTCNT_SH)
              | (xfer_len & TSIZ_XFERSIZE_MASK);
    HCDMA(0)  = MailboxPhysicalToBus((unsigned int)(unsigned long)dma_bounce);
    HCINT(0)  = HCINT_ALL;

    hcchar = HCCHAR(0);
    hcchar &= ~(HCCHAR_MULTICNT_MASK | HCCHAR_CHENA | HCCHAR_CHDIS | HCCHAR_ODDFRM);
    hcchar |= (1u << HCCHAR_MULTICNT_SH) | (odd_frame ? HCCHAR_ODDFRM : 0) | HCCHAR_CHENA;
    HCCHAR(0) = hcchar;

    ret = wait_for_chhltd(&sub, pid);
    if (ret < 0)
        return ret;

    if (dir_in) {
        xfer_len -= sub;
        if (xfer_len)                              // IN: copy received data to the caller
            memcpy(buffer, dma_bounce, xfer_len);
    }
    *actual_len = xfer_len;
    return 0;
}

//
// One logical transfer on channel 0 (u-boot chunk_msg). Sets the channel up for the
// device/endpoint, then loops issuing packet groups until len is done or a short packet
// stops it. For a full/low-speed device behind a high-speed hub it runs the two-step
// SPLIT protocol: start-split (COMPSPLT=0) waits for ACK, then complete-split
// (COMPSPLT=1) repeats until XFERCOMPL, retrying on NYET inside a 4-frame window. The
// only split-needing device here is the keyboard, whose parent is kbd.hub_{addr,port}
// (recorded during hub enumeration), so the split branch reads those directly. Returns
// 0 (with *act_len set), XFER_AGAIN (retry the whole stage), or XFER_ERR. *pid is the
// data toggle, updated from the hardware so an interrupt endpoint stays in sync.
//
static int chunk_msg(unsigned int devaddr, unsigned int epnum, int dir_in,
                     unsigned int eptype, unsigned int mps, unsigned int speed,
                     unsigned int *pid, void *buffer, unsigned int len,
                     unsigned int *act_len)
{
    unsigned int done = 0, max_xfer_len, num_packets, rootspeed, hcchar;
    int do_split = 0, complete_split = 0, stop_transfer = 0;
    unsigned int ssplit_frame_num = 0;
    int ret = 0;

    // Channel base setup (u-boot dwc_otg_hc_init): device addr / ep / dir / type / mps.
    hcchar = ((devaddr & 0x7f) << HCCHAR_DEVADDR_SH)
           | ((epnum & 0xf) << HCCHAR_EPNUM_SH)
           | (dir_in ? HCCHAR_EPDIR_IN : 0)
           | ((eptype & 3) << HCCHAR_EPTYPE_SH)
           | (mps & 0x7ff);
    if (speed == USB_SPEED_LOW)
        hcchar |= HCCHAR_LSPDDEV;
    HCCHAR(0) = hcchar;
    HCSPLT(0) = 0;

    max_xfer_len = DWC2_MAX_PACKET_COUNT * mps;
    if (max_xfer_len > DWC2_MAX_TRANSFER_SIZE)
        max_xfer_len = DWC2_MAX_TRANSFER_SIZE;
    if (max_xfer_len > sizeof(dma_bounce))         // never exceed the bounce buffer
        max_xfer_len = sizeof(dma_bounce);
    num_packets = max_xfer_len / mps;
    max_xfer_len = num_packets * mps;

    // FS/LS device behind a HS hub -> program HCSPLT and switch to one-packet splits.
    rootspeed = (HPRT0 & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT;
    if (speed != USB_SPEED_HIGH && rootspeed == USB_SPEED_HIGH) {
        HCSPLT(0) = HCSPLT_SPLTENA
                  | ((kbd.hub_addr & 0x7f) << HCSPLT_HUBADDR_SH)
                  | (kbd.hub_port & 0x7f);
        do_split = 1;
        num_packets = 1;
        max_xfer_len = mps;
    }

    do {
        unsigned int actual_len = 0, xfer_len, hcint;
        int odd_frame = 0;

        xfer_len = len - done;
        if (xfer_len > max_xfer_len)
            xfer_len = max_xfer_len;
        else if (xfer_len > mps)
            num_packets = (xfer_len + mps - 1) / mps;
        else
            num_packets = 1;

        if (complete_split)
            HCSPLT(0) |= HCSPLT_COMPSPLT;
        else if (do_split)
            HCSPLT(0) &= ~HCSPLT_COMPSPLT;

        if (eptype == EPTYPE_INTR && !(HFNUM & 1))
            odd_frame = 1;

        ret = transfer_chunk(pid, dir_in, (unsigned char *)buffer + done,
                             num_packets, xfer_len, &actual_len, odd_frame);

        hcint = HCINT(0);
        if (complete_split) {
            stop_transfer = 0;
            if (hcint & HCINT_NYET) {
                unsigned int frame_num = HFNUM & HFNUM_FRNUM_MASK;
                ret = 0;
                if (((frame_num - ssplit_frame_num) & HFNUM_FRNUM_MASK) > 4)
                    ret = XFER_AGAIN;              // CSPLIT window blown - retry stage
            } else {
                complete_split = 0;               // complete-split done
            }
        } else if (do_split) {
            if (hcint & HCINT_ACK) {
                ssplit_frame_num = HFNUM & HFNUM_FRNUM_MASK;
                ret = 0;                           // start-split ACKed (not an error)
                complete_split = 1;                // do the complete-split next
            }
        }

        if (ret)
            break;
        if (actual_len < xfer_len)
            stop_transfer = 1;
        done += actual_len;
    } while (((done < len) && !stop_transfer) || complete_split);

    HCINT(0) = HCINT_ALL;
    *act_len = done;
    return ret;
}

//
// A control transfer, mirroring u-boot _submit_control_msg: SETUP (PID SETUP), an
// optional DATA stage (PID DATA1), then the zero-length STATUS in the opposite
// direction (PID DATA1). Each stage is retried while the device answers NAK
// (XFER_AGAIN), bounded so a wedged device cannot hang the loader. Returns the DATA
// byte count (>=0) or -1 on error.
//
#define CTRL_AGAIN_MAX 500

static int usb_ctrl_stage(unsigned int devaddr, int dir_in, unsigned int mps,
                          unsigned int speed, unsigned int pid, void *buf,
                          unsigned int len, unsigned int *act)
{
    int ret, tries = 0;

    for (;;) {
        ret = chunk_msg(devaddr, 0, dir_in, EPTYPE_CONTROL, mps, speed, &pid, buf, len, act);
        if (ret != XFER_AGAIN || ++tries > CTRL_AGAIN_MAX)
            break;
        FwStallExecution(125);                               // ~1 microframe between NAK retries
    }
    return ret;
}

static int usb_control(unsigned int devaddr, unsigned int mps, unsigned int speed,
                       const unsigned char *setup, void *data, unsigned int datalen,
                       int dir_in)
{
    unsigned int act = 0, total = 0;
    int status_dir;

    memcpy(setup_buf, setup, 8);
    if (usb_ctrl_stage(devaddr, 0, mps, speed, PID_SETUP, setup_buf, 8, &act) != 0)
        return -1;

    if (datalen > 0) {
        if (usb_ctrl_stage(devaddr, dir_in, mps, speed, PID_DATA1, data, datalen, &act) != 0)
            return -1;
        total = act;
        status_dir = dir_in ? 0 : 1;
    } else {
        status_dir = 1;                            // no-data control: STATUS is IN
    }

    if (usb_ctrl_stage(devaddr, status_dir, mps, speed, PID_DATA1, zlp_buf, 0, &act) != 0)
        return -1;

    return (int)total;
}

static const char *speed_name(unsigned int s)
{
    return s == USB_SPEED_HIGH ? "high" : s == USB_SPEED_FULL ? "full" : "low";
}

static void put16(unsigned char *p, unsigned int v)
{
    p[0] = (unsigned char)(v & 0xff);
    p[1] = (unsigned char)((v >> 8) & 0xff);
}

//
// Build the 8-byte SETUP packet and run a control transfer. Direction is taken from
// bmRequestType's top bit; data/wLength is the optional data stage. Returns the
// usb_control result (data-stage byte count, or -1).
//
static int usb_ctrl_req(unsigned int addr, unsigned int mps, unsigned int speed,
                        unsigned int bmRequestType, unsigned int bRequest,
                        unsigned int wValue, unsigned int wIndex,
                        void *data, unsigned int wLength)
{
    unsigned char setup[8];

    setup[0] = (unsigned char)bmRequestType;
    setup[1] = (unsigned char)bRequest;
    put16(&setup[2], wValue);
    put16(&setup[4], wIndex);
    put16(&setup[6], wLength);

    return usb_control(addr, mps, speed, setup, data, wLength,
                       (bmRequestType & 0x80) ? 1 : 0);
}

static int usb_kbd_enumerate(unsigned int dev_speed, unsigned int addr);

//
// Enumerate the hub on the root port - on QEMU the built-in NEC hub (0x0409:0x55aa),
// on the real Pi 2 the onboard LAN9514. Assign it ADDR_HUB, select its configuration,
// read the hub-class descriptor for the port count, power every downstream port, then
// reset and probe EVERY connected port, trying to enumerate each device as a HID
// keyboard. The LAN9514 carries its own (high-speed) ethernet on one port, so the
// keyboard is not necessarily the first device found - keep going until a HID boot
// keyboard is configured. Each probed device gets a distinct address so that addressed
// devices never clash at address 0. Returns 0 (keyboard found + ready), or -1.
//
static int usb_hub_enumerate(unsigned int hub_speed)
{
    unsigned int mps0, nports, pgood_ms, i, next_addr;

    //
    // Device descriptor, first 8 bytes at the always-safe control mps of 8, to learn
    // the real bMaxPacketSize0 (a HS hub reports 64) before issuing any larger
    // transfer - reading more than one packet at the wrong mps risks a babble error.
    //
    if (usb_ctrl_req(0, 8, hub_speed, 0x80, REQ_GET_DESCRIPTOR, DESC_DEVICE << 8, 0,
                     data_buf, 8) < 0)
        return -1;
    mps0 = data_buf[7];

    // Assign the hub its address, then let it settle (2 ms set-address recovery).
    if (usb_ctrl_req(0, 8, hub_speed, 0x00, REQ_SET_ADDRESS, ADDR_HUB, 0, NULL, 0) < 0)
        return -1;
    FwStallExecution(2 * 1000);

    // Config descriptor (the 9-byte descriptor itself) -> bConfigurationValue, then
    // select that configuration so the hub will power and report its ports.
    if (usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x80, REQ_GET_DESCRIPTOR,
                     DESC_CONFIG << 8, 0, data_buf, 9) < 0)
        return -1;
    if (usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x00, REQ_SET_CONFIG,
                     data_buf[5], 0, NULL, 0) < 0)
        return -1;

    // Hub-class descriptor (type 0x29, class request 0xA0) -> port count + power-good.
    if (usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0xA0, REQ_GET_DESCRIPTOR,
                     DESC_HUB << 8, 0, data_buf, 16) < 0)
        return -1;
    nports   = data_buf[2];
    pgood_ms = (unsigned int)data_buf[5] * 2;      // bPwrOn2PwrGood is in 2 ms units
    FwPrint("USB: hub: %u ports\r\n", nports);

    // Power every port, then wait power-good (+ margin).
    for (i = 1; i <= nports; i += 1)
        usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x23, REQ_SET_FEATURE,
                     PORT_FEAT_POWER, i, NULL, 0);
    FwStallExecution((pgood_ms + 10) * 1000);

    // Probe every port: reset each connected one and try to enumerate the device behind
    // it as a HID keyboard. The first device is often the LAN9514's own HS ethernet, not
    // the keyboard, so keep going. Distinct address per device avoids addr-0 clashes.
    next_addr = ADDR_KBD;
    for (i = 1; i <= nports; i += 1) {
        unsigned int status, dev_speed;

        if (usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0xA3, REQ_GET_STATUS, 0, i,
                         data_buf, 4) < 0)
            return -1;
        status = (unsigned int)(data_buf[0] | (data_buf[1] << 8));
        if (!(status & PORTSTAT_CONNECTION))
            continue;

        usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x23, REQ_SET_FEATURE,
                     PORT_FEAT_RESET, i, NULL, 0);
        FwStallExecution(60 * 1000);
        if (usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0xA3, REQ_GET_STATUS, 0, i,
                         data_buf, 4) < 0)
            return -1;
        status = (unsigned int)(data_buf[0] | (data_buf[1] << 8));
        usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x23, REQ_CLEAR_FEATURE,
                     PORT_FEAT_C_RESET, i, NULL, 0);
        usb_ctrl_req(ADDR_HUB, mps0, hub_speed, 0x23, REQ_CLEAR_FEATURE,
                     PORT_FEAT_C_CONNECTION, i, NULL, 0);

        dev_speed = (status & PORTSTAT_LOWSPEED) ? USB_SPEED_LOW
                  : (status & PORTSTAT_HIGHSPEED) ? USB_SPEED_HIGH
                  : USB_SPEED_FULL;
        // The split branch in chunk_msg keys off kbd.hub_{addr,port}, so set them to
        // the port being probed before enumerating the device behind it.
        kbd.hub_addr = ADDR_HUB;
        kbd.hub_port = i;

        if (usb_kbd_enumerate(dev_speed, next_addr) == 0)
            return 0;                              // HID keyboard found and configured
        next_addr += 1;                            // not a keyboard - keep its address
    }

    return -1;
}

//
// Enumerate the device the hub just reset onto its connected port (it answers at
// address 0 - it is the only enabled downstream port, so no address clash). Two-step
// bMaxPacketSize0 read, assign ADDR_KBD, then read and parse the configuration
// descriptor for a HID boot-protocol keyboard interface and its interrupt-IN
// endpoint, select that configuration, and switch the device into boot protocol.
// Fills kbd.* and sets kbd.present on success. Returns 0 on success, -1 otherwise.
//
static int usb_kbd_enumerate(unsigned int dev_speed, unsigned int addr)
{
    unsigned int mps0, total, rd, confval, off;
    int in_kbd, have_ep;
    unsigned int iface_num;

    // First 8 bytes of the device descriptor at the safe mps 8 -> bMaxPacketSize0.
    if (usb_ctrl_req(0, 8, dev_speed, 0x80, REQ_GET_DESCRIPTOR, DESC_DEVICE << 8, 0,
                     data_buf, 8) < 0)
        return -1;
    mps0 = data_buf[7];
    if (mps0 == 0)
        mps0 = 8;

    // Assign this device its address, settle, then talk to it at addr/mps0.
    if (usb_ctrl_req(0, 8, dev_speed, 0x00, REQ_SET_ADDRESS, addr, 0, NULL, 0) < 0)
        return -1;
    FwStallExecution(2 * 1000);

    // Config descriptor: 9-byte header first for wTotalLength + bConfigurationValue,
    // then the whole thing (capped to the DMA buffer) so the interface/endpoint
    // descriptors that follow can be walked.
    if (usb_ctrl_req(addr, mps0, dev_speed, 0x80, REQ_GET_DESCRIPTOR,
                     DESC_CONFIG << 8, 0, data_buf, 9) < 0)
        return -1;
    total   = (unsigned int)(data_buf[2] | (data_buf[3] << 8));
    confval = data_buf[5];
    rd = total > sizeof(data_buf) ? sizeof(data_buf) : total;
    if (usb_ctrl_req(addr, mps0, dev_speed, 0x80, REQ_GET_DESCRIPTOR,
                     DESC_CONFIG << 8, 0, data_buf, rd) < 0)
        return -1;

    //
    // Walk the descriptor list. Interface (type 4) carries class/subclass/protocol;
    // the endpoint (type 5) that follows the matching HID-boot-keyboard interface and
    // is interrupt-IN is the report endpoint we will poll.
    //
    in_kbd = 0;
    have_ep = 0;
    iface_num = 0;
    for (off = 0; off + 2 <= rd; ) {
        unsigned int blen  = data_buf[off];
        unsigned int btype = data_buf[off + 1];

        if (blen == 0)
            break;
        if (btype == 0x04 && off + 8 <= rd) {          // INTERFACE
            in_kbd = (data_buf[off + 5] == CLASS_HID &&
                      data_buf[off + 6] == HID_SUBCLASS_BOOT &&
                      data_buf[off + 7] == HID_PROTOCOL_KEYBOARD);
            if (in_kbd)
                iface_num = data_buf[off + 2];
        } else if (btype == 0x05 && in_kbd && !have_ep && off + 6 <= rd) {  // ENDPOINT
            unsigned int epaddr = data_buf[off + 2];
            unsigned int attr   = data_buf[off + 3];
            if ((epaddr & 0x80) && (attr & 0x03) == 0x03) {     // interrupt IN
                kbd.ep     = epaddr & 0x0f;
                kbd.ep_mps = (unsigned int)(data_buf[off + 4] | (data_buf[off + 5] << 8));
                have_ep = 1;
            }
        }
        off += blen;
    }

    if (!have_ep)
        return -1;                                 // not a HID boot keyboard; try next port

    // Select the configuration, then put the keyboard in boot protocol and idle-on-
    // change. SET_PROTOCOL/SET_IDLE are best-effort: some keyboards STALL them yet
    // still report (u-boot ignores their status too).
    if (usb_ctrl_req(addr, mps0, dev_speed, 0x00, REQ_SET_CONFIG,
                     confval, 0, NULL, 0) < 0) {
        FwPrint("USB: kbd SET_CONFIGURATION failed\r\n");
        return -1;
    }
    usb_ctrl_req(addr, mps0, dev_speed, 0x21, REQ_SET_PROTOCOL, 0, iface_num, NULL, 0);
    usb_ctrl_req(addr, mps0, dev_speed, 0x21, REQ_SET_IDLE, 0, iface_num, NULL, 0);

    kbd.present = 1;
    kbd.addr    = addr;
    kbd.speed   = dev_speed;
    kbd.mps0    = mps0;
    kbd.toggle  = PID_DATA0;        // interrupt-IN data toggle starts at DATA0
    FwPrint("USB: keyboard ready (addr=%u ep=%u mps=%u iface=%u)\r\n",
            kbd.addr, kbd.ep, kbd.ep_mps, iface_num);
    return 0;
}

// ---- HID boot-protocol keyboard ---------------------------------------------

BOOLEAN
UsbKeyboardPresent (
    VOID
    )
{
    return kbd.present != 0;
}

//
// One interrupt-IN poll of the keyboard report endpoint: TRUE with the 8-byte boot
// report in Report, or FALSE if the keyboard has no new report (it NAKed).
//
BOOLEAN
UsbKeyboardReport (
    OUT UCHAR Report[8]
    )
{
    unsigned int got = 0;

    if (!kbd.present)
        return FALSE;
    if (chunk_msg(kbd.addr, kbd.ep, 1, EPTYPE_INTR, kbd.ep_mps, kbd.speed,
                  &kbd.toggle, report_buf, 8, &got) != 0 || got < 3)
        return FALSE;
    memset(Report, 0, 8);
    memcpy(Report, report_buf, got < 8 ? got : 8);
    return TRUE;
}

//
// Bring up the host controller and the root port, and find the keyboard behind the
// hub. Returns TRUE if a keyboard is ready.
//
BOOLEAN
UsbInitialize (
    VOID
    )
{
    unsigned int id, hprt;
    int speed;

    //
    // Power on the USB controller through the VideoCore mailbox FIRST. The Pi leaves the
    // USB HCD powered off at boot; u-boot does this in board_init() before any DWC2
    // access. Skipping it is what kept the core half-alive on real HW (registers + port
    // detect worked, but channels enabled and never executed). Non-fatal: QEMU does not
    // model this power domain and does not need it, so a failure is only a warning.
    //
    if (MailboxSetPowerState(POWER_DEVID_USB_HCD, TRUE) != 0)
        FwPrint("USB: mailbox USB power-on failed (continuing - OK on QEMU)\r\n");
    FwStallExecution(40 * 1000);

    id = GSNPSID;
    if ((id & 0xfffff000u) != 0x4f542000u) {       // "OT2" signature prefix
        FwPrint("USB: no DWC2 core at 0x%lx\r\n", (unsigned long)DWC2_BASE);
        return FALSE;
    }

    if (!dwc2_core_reset()) {
        FwPrint("USB: core reset timeout\r\n");
        return FALSE;
    }

    //
    // PHY selection (u-boot dwc_otg_core_init, HS/UTMI path). The Pi's DWC2 is wired to
    // the on-chip UTMI+ high-speed PHY, NOT an external full-speed serial transceiver,
    // so clear PHYSEL (use the HS PHY), select UTMI+ over ULPI, and 8-bit UTMI width.
    // The PHY selection only latches across a core reset, so program it THEN reset
    // again. The earlier version selected the FS serial PHY (PHYSEL=1) and reset
    // *before* this, which left the controller on a PHY it isn't wired to: it never
    // clocked the USB bus, so every channel enabled but never halted - the real-HW
    // "hcint=0xffffffff" symptom. (QEMU models neither PHY, so it tolerated the bug.)
    //
    GUSBCFG &= ~(GUSBCFG_PHYSEL | GUSBCFG_ULPI_UTMI_SEL | GUSBCFG_PHYIF16);
    if (!dwc2_core_reset()) {
        FwPrint("USB: core reset (post-PHY) timeout\r\n");
        return FALSE;
    }

    // Clear ULPI FS/LS + clock-suspend, then force host mode and confirm it. The mode
    // switch takes tens of ms on real silicon (poll CURMODE_HOST with a real ~200 ms
    // timeout); QEMU is already host on entry, so the loop falls straight through.
    GUSBCFG &= ~(GUSBCFG_ULPI_FS_LS | GUSBCFG_ULPI_CLK_SUSP_M);
    GUSBCFG |= GUSBCFG_FORCEHOSTMODE;
    {
        int ms;
        for (ms = 0; ms < 200; ms += 1) {
            if (GINTSTS & GINTSTS_CURMODE_HOST)
                break;
            FwStallExecution(1 * 1000);
        }
        if (!(GINTSTS & GINTSTS_CURMODE_HOST)) {
            FwPrint("USB: not in host mode after 200 ms (gintsts=0x%lx)\r\n",
                    (unsigned long)GINTSTS);
            return FALSE;
        }
    }

    // DMA on, global interrupts off (we poll). HBSTLEN=INCR4 matches u-boot's setting
    // for the Pi's internal-DMA DWC2 (dwc2.c INT_DMA_ARCH path); without a burst length
    // (HBSTLEN=0/single) real-silicon DMA transfers fail, though QEMU tolerates it.
    GAHBCFG = GAHBCFG_DMA_EN | GAHBCFG_HBSTLEN_INCR4;
    GINTMSK = 0;

    // Restart the PHY clock (u-boot host-init step 1). If the PHY clock stays gated the
    // core cannot clock the USB bus and channels enable but never halt.
    PCGCCTL = 0;

    // Host config: FS/LS clock = the UTMI PHY's 30/60 MHz (NOT 48 MHz, the dedicated
    // FS-serial-PHY value). Match stock u-boot exactly - enumerate at HIGH speed (no
    // FSLSSUPP). The earlier FSLSSUPP "force full-speed" trick was DWC2-undefined on a
    // UTMI PHY (the real-HW dump showed the channel enabling but never executing on a
    // live bus). At HS the HS hub talks natively and the FS/LS keyboard behind it goes
    // through the split transactions chunk_msg now implements.
    HCFG = (HCFG & ~7u) | HCFG_FSLSPCLKSEL_30_60;

    // FIFO depths (words) - u-boot's defaults for the Pi DWC2.
    GRXFSIZ   = 516 + 16;
    GNPTXFSIZ = (0x100u << 16) | (516 + 16);
    HPTXFSIZ  = (0x200u << 16) | ((516 + 16) + 0x100);
    dwc2_flush_fifos();

    // Power the root port.
    hprt = HPRT0 & ~HPRT0_W1C_MASK;
    HPRT0 = hprt | HPRT0_PWR;
    FwStallExecution(50 * 1000);

    // Wait for a device to connect.
    if (!wait_bit(&HPRT0, HPRT0_CONNSTS, 1)) {
        FwPrint("USB: no device on root port\r\n");
        return FALSE;
    }

    // Reset the port to enable it and latch the device speed.
    hprt = HPRT0 & ~HPRT0_W1C_MASK;
    HPRT0 = hprt | HPRT0_RST;
    FwStallExecution(60 * 1000);                                    // >= 50 ms reset
    hprt = HPRT0 & ~HPRT0_W1C_MASK;
    HPRT0 = hprt & ~HPRT0_RST;
    FwStallExecution(50 * 1000);

    hprt = HPRT0;
    if (!(hprt & HPRT0_ENA)) {
        FwPrint("USB: port did not enable (hprt=0x%lx)\r\n", (unsigned long)hprt);
        return FALSE;
    }
    speed = (int)((hprt & HPRT0_SPD_MASK) >> HPRT0_SPD_SHIFT);
    FwPrint("USB: port enabled, speed=%s\r\n", speed_name((unsigned int)speed));

    //
    // The root-port device is a hub on both targets (QEMU's NEC hub; the Pi 2's
    // onboard LAN9514), so a USB keyboard always hangs off it. Enumerate the hub and
    // search every port for a HID boot keyboard (the LAN9514 also presents its own
    // ethernet on one port, so the keyboard is not necessarily the first device).
    //
    if (usb_hub_enumerate((unsigned int)speed) != 0) {
        FwPrint("USB: no keyboard found\r\n");
        return FALSE;
    }


    return TRUE;
}
