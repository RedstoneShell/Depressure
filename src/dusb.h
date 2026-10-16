// SPDX-License-Identifier: LicenseRef-Depressure-BSL-1.1
// Copyright (c) 2026 RedstoneShell
//
// Change Date: October 11, 2158
// Change License: GPL-3.0-or-later
// Additional Use Grant: Compilation of third-party software using these headers is permitted without restrictions.

#ifndef DUSB_H
#define DUSB_H

#include "dtypes.h"
#include "dstat.h"

#define DUSB_MAX_PORTS       8
#define DUSB_MAX_DEVICES     16
#define DUSB_MAX_DESC_LEN    256

/* UHCI I/O register offsets (from BAR0) */
#define UHCI_REG_USBCMD      0x00  /* 16-bit */
#define UHCI_REG_USBSTS      0x02  /* 16-bit */
#define UHCI_REG_USBINTR     0x04  /* 16-bit */
#define UHCI_REG_FRNUM       0x06  /* 16-bit */
#define UHCI_REG_FRBASEADD   0x08  /* 32-bit */
#define UHCI_REG_SOFMOD      0x0C  /* 8-bit  */
#define UHCI_REG_PORTSC1     0x10  /* 16-bit, then +2 per port */

/* USBCMD bits */
#define UHCI_CMD_RS          (1 << 0)   /* Run/Stop */
#define UHCI_CMD_HCRESET     (1 << 1)   /* Host Controller Reset */
#define UHCI_CMD_GRESET      (1 << 2)   /* Global Reset */
#define UHCI_CMD_EGSM        (1 << 3)   /* Enter Global Suspend Mode */
#define UHCI_CMD_FGR         (1 << 4)   /* Force Global Resume */
#define UHCI_CMD_SWDBG       (1 << 5)
#define UHCI_CMD_CF          (1 << 6)   /* Configure Flag */
#define UHCI_CMD_MAXP        (1 << 7)   /* Max Packet (64 bytes) */

/* USBSTS bits */
#define UHCI_STS_USBINT      (1 << 0)
#define UHCI_STS_USBERRINT   (1 << 1)
#define UHCI_STS_RESUME      (1 << 2)
#define UHCI_STS_HSE         (1 << 3)   /* Host System Error */
#define UHCI_STS_HCPE        (1 << 4)   /* Host Controller Process Error */
#define UHCI_STS_HCH         (1 << 5)   /* Host Controller Halted */

/* PORTSC bits */
#define UHCI_PORT_CCS        (1 << 0)   /* Current Connect Status */
#define UHCI_PORT_CSC        (1 << 1)   /* Connect Status Change */
#define UHCI_PORT_PED        (1 << 2)   /* Port Enabled/Disabled */
#define UHCI_PORT_PEDC       (1 << 3)   /* Port Enable/Disable Change */
#define UHCI_PORT_LSDA       (1 << 8)   /* Low Speed Device Attached */
#define UHCI_PORT_RD         (1 << 9)   /* Resume Detect */
#define UHCI_PORT_LS         (1 << 10)  /* Line Status */
#define UHCI_PORT_RST        (1 << 12)  /* Port Reset */
#define UHCI_PORT_SUSPEND    (1 << 13)
#define UHCI_PORT_RW         (0x1F00)   /* read/write bits */

/* Link Pointer bits (in TD / QH) */
#define UHCI_LP_TERM         (1 << 0)   /* Terminate */
#define UHCI_LP_QH           (1 << 1)   /* Queue Head (points to QH, not TD) */
#define UHCI_LP_VF           (1 << 2)   /* Depth-First (for QH link) */

/* TD status bits */
#define UHCI_TD_STS_ACTIVE   (1 << 23)
#define UHCI_TD_STS_STALLED  (1 << 22)
#define UHCI_TD_STS_DBERR    (1 << 21)
#define UHCI_TD_STS_BABBLE   (1 << 20)
#define UHCI_TD_STS_NAK      (1 << 19)
#define UHCI_TD_STS_CRC      (1 << 18)
#define UHCI_TD_STS_BITSTUFF (1 << 17)
#define UHCI_TD_STS_ACTLEN_MASK 0x7FF

/* TD token bits */
#define UHCI_TD_PID_SETUP   0x2D
#define UHCI_TD_PID_IN      0x69
#define UHCI_TD_PID_OUT     0xE1

#define UHCI_TD_ERRCNT(n)   (((n) & 3) << 27)
#define UHCI_TD_LS          (1 << 26)   /* Low Speed */
#define UHCI_TD_3PACK       (1 << 24)   /* 3 packets without toggle */
#define UHCI_TD_IOC         (1 << 23)   /* Interrupt on Complete */
#define UHCI_TD_MAXLEN(n)   (((n) & 0x7FF) << 21)
#define UHCI_TD_TOGGLE      (1 << 19)

/* USB descriptor types */
#define USB_DT_DEVICE        1
#define USB_DT_CONFIG        2
#define USB_DT_STRING        3
#define USB_DT_INTERFACE     4
#define USB_DT_ENDPOINT      5

typedef struct usb_device_desc {
    u8  bLength;
    u8  bDescriptorType;
    u16 bcdUSB;
    u8  bDeviceClass;
    u8  bDeviceSubClass;
    u8  bDeviceProtocol;
    u8  bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8  iManufacturer;
    u8  iProduct;
    u8  iSerialNumber;
    u8  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

typedef struct usb_config_desc {
    u8  bLength;
    u8  bDescriptorType;
    u16 wTotalLength;
    u8  bNumInterfaces;
    u8  bConfigurationValue;
    u8  iConfiguration;
    u8  bmAttributes;
    u8  bMaxPower;
} __attribute__((packed)) usb_config_desc_t;

typedef struct usb_device {
    u8   port;              /* root port index */
    u8   addr;              /* USB address (0 = not assigned) */
    u8   low_speed;         /* 1 = low-speed (1.5 Mbps) */
    u8   max_packet0;       /* max packet size for EP0 */
    u16  vendor;
    u16  product;
    u8   configured;
    u8   _pad;
    usb_device_desc_t desc;
} usb_device_t;

typedef struct dusb_api {
    u32 version;
    u32 _pad;
    u32 (*get_device_count)(void);
    int (*get_device)(u32 index, const usb_device_t **out);
    int (*control_transfer)(u8 addr, u8 low_speed, u8 max_packet0,
                            u8 bmRequestType, u8 bRequest,
                            u16 wValue, u16 wIndex,
                            void *data, u16 wLength);
    u32 (*get_base)(void);
} dusb_api_t;

void dusb_main(void *api, void *fb_info);

#endif /* DUSB_H */