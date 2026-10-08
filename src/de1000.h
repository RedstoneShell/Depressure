#ifndef DE1000_H
#define DE1000_H

#include "dtypes.h"

#define DE1000_VENDOR_ID  0x8086
#define DE1000_DEVICE_ID  0x100E

#define DE1000_MMIO_SIZE  0x20000

#define E1000_CTRL   0x0000
#define E1000_STATUS 0x0008
#define E1000_ICR    0x00C0
#define E1000_IMS    0x00D0
#define E1000_RCTL   0x0100
#define E1000_TCTL   0x0400
#define E1000_RDBAL  0x2800
#define E1000_RDBAH  0x2804
#define E1000_RDLEN  0x2808
#define E1000_RDH    0x2810
#define E1000_RDT    0x2818
#define E1000_TDBAL  0x3800
#define E1000_TDBAH  0x3804
#define E1000_TDLEN  0x3808
#define E1000_TDH    0x3810
#define E1000_TDT    0x3818
#define E1000_RAL    0x5400
#define E1000_RAH    0x5404
#define E1000_MTA    0x5200

#define E1000_TX_DESC_COUNT 8
#define E1000_RX_DESC_COUNT 8
#define E1000_PKT_BUF_SIZE  2048

typedef struct e1000_tx_desc {
    u64 addr;
    u16 length;
    u8  cso;
    u8  cmd;
    u8  status;
    u8  css;
    u16 special;
} __attribute__((packed)) e1000_tx_desc_t;

typedef struct e1000_rx_desc {
    u64 addr;
    u16 length;
    u16 csum;
    u8  status;
    u8  errors;
    u16 special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct de1000_api {
    u32 version;
    u32 _pad;
    u8  mac[6];
    int  (*send)(const void *buf, u16 len);
    int  (*recv)(void *buf, u16 *len);
    u32  (*get_base)(void);
} de1000_api_t;

void de1000_main(void *api, void *fb_info);

#endif