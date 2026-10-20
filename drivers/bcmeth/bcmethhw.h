/*++

Module Name:

    bcmethhw.h

Abstract:

    The Ethernet controller of the Raspberry Pi as the firmware describes
    it: a USB function (the LAN9514's on the Pi 2 and 3 Model B, the LAN7515's
    on the Pi 3 Model B+, or a CDC Ethernet device), the NetworkController of
    the internal bus. Its configuration data holds the interrupt and the
    registers of the USB host, then, as Jazz gives the SONIC's, the station
    address, here that of the board from the VideoCore.

--*/

#ifndef _BCMETHHW_
#define _BCMETHHW_

#define USB_VENDOR_SMSC         0x0424
#define USB_PRODUCT_LAN9500     0xEC00
#define USB_PRODUCT_LAN7800     0x7800
#define USB_CLASS_COMMUNICATION 0x02

#define BCMETH_ID_LAN9500       "LAN9500"
#define BCMETH_ID_LAN7800       "LAN7800"
#define BCMETH_ID_CDC           "CDC-ETHERNET"

typedef struct _CM_BCMETH_DEVICE_DATA {
    USHORT Version;
    USHORT Revision;
    UCHAR EthernetAddress[8];
} CM_BCMETH_DEVICE_DATA, *PCM_BCMETH_DEVICE_DATA;

#endif // _BCMETHHW_
