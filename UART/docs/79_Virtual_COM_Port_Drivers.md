# 79. Virtual COM Port Drivers

- **Architecture** — how the two USB interfaces (Control + Data) are structured, with endpoint layout and class identifiers
- **Descriptors** — complete packed C struct for the entire configuration descriptor tree, ready to drop into firmware
- **Class Requests** — all four CDC ACM requests (`SET_LINE_CODING`, `GET_LINE_CODING`, `SET_CONTROL_LINE_STATE`, `SEND_BREAK`) with the `cdc_line_coding_t` structure
- **C/C++ Device side** — STM32 HAL callback implementation including ZLP handling and physical UART bridging
- **C/C++ Kernel driver** — Linux USB driver skeleton with URB-based async bulk I/O
- **C/C++ Host side** — libusb example for user-space VCP control
- **Rust Embedded** — full Embassy async CDC ACM with task splitting for concurrent RX/TX
- **Rust Host** — `serialport-rs` and `tokio-serial` examples for cross-platform port access
- **Platform integration** — Windows INF file and Linux udev rule
- **Debugging table** — 9 common failure modes with root causes and fixes

## Implementing USB CDC ACM for Virtual Serial Ports

---

## Table of Contents

1. [Introduction](#introduction)
2. [USB CDC ACM Architecture](#usb-cdc-acm-architecture)
3. [USB Descriptors for CDC ACM](#usb-descriptors-for-cdc-acm)
4. [CDC ACM Class Requests](#cdc-acm-class-requests)
5. [Implementation in C/C++](#implementation-in-cc)
   - [STM32 Example (HAL/LL)](#stm32-example-halll)
   - [Linux Kernel Driver Basics](#linux-kernel-driver-basics)
   - [libusb Host-Side Example](#libusb-host-side-example)
6. [Implementation in Rust](#implementation-in-rust)
   - [Embassy USB CDC ACM (Embedded)](#embassy-usb-cdc-acm-embedded)
   - [serialport-rs (Host Side)](#serialport-rs-host-side)
7. [Line Coding and Control Signals](#line-coding-and-control-signals)
8. [Data Flow and Endpoint Management](#data-flow-and-endpoint-management)
9. [Platform Driver Integration](#platform-driver-integration)
10. [Common Pitfalls and Debugging](#common-pitfalls-and-debugging)
11. [Summary](#summary)

---

## Introduction

A **Virtual COM Port (VCP)** allows a USB device to appear as a classic serial (COM/tty) port to the host operating system — without any RS-232 hardware. Applications open it with standard `open()`/`ReadFile()` calls, oblivious to the underlying USB transport.

The mechanism that makes this possible is the **USB Communications Device Class, Abstract Control Model** — universally abbreviated **CDC ACM** — defined in the USB CDC specification (v1.2). Every major OS ships an in-box driver for CDC ACM:

| OS | Built-in Driver |
|----|----------------|
| Windows 10/11 | `usbser.sys` (native, no INF needed ≥ Win10) |
| Linux | `cdc_acm.ko` → `/dev/ttyACM0` |
| macOS | `AppleUSBCDC.kext` → `/dev/tty.usbmodemXXX` |
| Android | Available via USB Host API |

This makes CDC ACM the **de-facto standard** for microcontroller debug consoles, bootloaders, test instruments, and any embedded device that needs a human-readable serial channel over USB.

---

## USB CDC ACM Architecture

CDC ACM uses **two USB interfaces** bundled as an Interface Association:

```
USB Configuration
└── Interface Association (CDC ACM)
    ├── Interface 0 — CDC Control Interface (class 0x02, subclass 0x02)
    │   ├── Endpoint 0 (Control — shared default)
    │   └── Endpoint N (Interrupt IN — notification endpoint)
    │       Carries: SERIAL_STATE notifications, network connection, etc.
    └── Interface 1 — CDC Data Interface (class 0x0A)
        ├── Endpoint M (Bulk IN  — device→host data)
        └── Endpoint K (Bulk OUT — host→device data)
```

The **Control Interface** carries class-specific requests (Set/Get Line Coding, Set Control Line State) and asynchronous notifications. The **Data Interface** carries the raw byte stream in both directions using bulk transfers for reliability and flow control.

### Key Identifiers

| Field | Value |
|-------|-------|
| bDeviceClass | 0x02 (or 0xEF for IAD) |
| bInterfaceClass (ctrl) | 0x02 — Communications |
| bInterfaceSubClass (ctrl) | 0x02 — Abstract Control Model |
| bInterfaceProtocol (ctrl) | 0x00 (no protocol) or 0x01 (AT commands) |
| bInterfaceClass (data) | 0x0A — CDC Data |

---

## USB Descriptors for CDC ACM

The descriptor tree is the foundation. Every byte matters — a malformed descriptor causes the host to reject the device silently.

### C Structure Approach (Embedded)

```c
/* cdc_acm_descriptors.h */
#include <stdint.h>

#define CDC_ACM_CTRL_IFACE   0
#define CDC_ACM_DATA_IFACE   1
#define CDC_NOTIF_EP         0x81   /* Interrupt IN */
#define CDC_DATA_IN_EP       0x82   /* Bulk IN      */
#define CDC_DATA_OUT_EP      0x02   /* Bulk OUT     */
#define CDC_NOTIF_EP_SIZE    8
#define CDC_DATA_EP_SIZE     64

/* Functional descriptor types (CS_INTERFACE = 0x24) */
#define CDC_HEADER_DESC_TYPE        0x00
#define CDC_CALL_MGMT_DESC_TYPE     0x01
#define CDC_ACM_DESC_TYPE           0x02
#define CDC_UNION_DESC_TYPE         0x06

#pragma pack(push, 1)

typedef struct {
    /* ---- Configuration Descriptor ---- */
    uint8_t  bLength_cfg;           /* 9  */
    uint8_t  bDescriptorType_cfg;   /* 0x02 */
    uint16_t wTotalLength;
    uint8_t  bNumInterfaces;        /* 2   */
    uint8_t  bConfigurationValue;   /* 1   */
    uint8_t  iConfiguration;        /* 0   */
    uint8_t  bmAttributes;          /* 0xC0 = self-powered */
    uint8_t  bMaxPower;             /* 50  = 100 mA        */

    /* ---- Interface Association Descriptor (IAD) ---- */
    uint8_t  bLength_iad;           /* 8   */
    uint8_t  bDescriptorType_iad;   /* 0x0B */
    uint8_t  bFirstInterface;       /* 0   */
    uint8_t  bInterfaceCount;       /* 2   */
    uint8_t  bFunctionClass;        /* 0x02 CDC */
    uint8_t  bFunctionSubClass;     /* 0x02 ACM */
    uint8_t  bFunctionProtocol;     /* 0x01 */
    uint8_t  iFunction;             /* 0   */

    /* ---- Control Interface ---- */
    uint8_t  bLength_ctrl;          /* 9   */
    uint8_t  bDescriptorType_ctrl;  /* 0x04 */
    uint8_t  bInterfaceNumber_ctrl; /* 0   */
    uint8_t  bAlternateSetting_ctrl;/* 0   */
    uint8_t  bNumEndpoints_ctrl;    /* 1   */
    uint8_t  bInterfaceClass_ctrl;  /* 0x02 */
    uint8_t  bInterfaceSubClass_ctrl;/* 0x02 */
    uint8_t  bInterfaceProtocol_ctrl;/* 0x00 */
    uint8_t  iInterface_ctrl;       /* 0   */

    /* ---- CDC Header Functional Descriptor ---- */
    uint8_t  bLength_hdr;           /* 5   */
    uint8_t  bDescriptorType_hdr;   /* 0x24 CS_INTERFACE */
    uint8_t  bDescriptorSubtype_hdr;/* 0x00 Header       */
    uint16_t bcdCDC;                /* 0x0120 CDC 1.2    */

    /* ---- CDC Call Management Functional Descriptor ---- */
    uint8_t  bLength_cm;            /* 5   */
    uint8_t  bDescriptorType_cm;    /* 0x24 */
    uint8_t  bDescriptorSubtype_cm; /* 0x01 Call Mgmt */
    uint8_t  bmCapabilities_cm;     /* 0x00 no call mgmt */
    uint8_t  bDataInterface;        /* 1   */

    /* ---- CDC ACM Functional Descriptor ---- */
    uint8_t  bLength_acm;           /* 4   */
    uint8_t  bDescriptorType_acm;   /* 0x24 */
    uint8_t  bDescriptorSubtype_acm;/* 0x02 ACM */
    uint8_t  bmCapabilities_acm;    /* 0x02 = supports Set/Get Line Coding,
                                              Set Control Line State,
                                              Serial State notification */

    /* ---- CDC Union Functional Descriptor ---- */
    uint8_t  bLength_union;         /* 5   */
    uint8_t  bDescriptorType_union; /* 0x24 */
    uint8_t  bDescriptorSubtype_union;/* 0x06 Union */
    uint8_t  bMasterInterface;      /* 0   */
    uint8_t  bSlaveInterface0;      /* 1   */

    /* ---- Notification Endpoint ---- */
    uint8_t  bLength_notif;         /* 7   */
    uint8_t  bDescriptorType_notif; /* 0x05 Endpoint */
    uint8_t  bEndpointAddress_notif;/* CDC_NOTIF_EP  */
    uint8_t  bmAttributes_notif;    /* 0x03 Interrupt */
    uint16_t wMaxPacketSize_notif;  /* CDC_NOTIF_EP_SIZE */
    uint8_t  bInterval_notif;       /* 16  (ms, FS) */

    /* ---- Data Interface ---- */
    uint8_t  bLength_data;          /* 9   */
    uint8_t  bDescriptorType_data;  /* 0x04 */
    uint8_t  bInterfaceNumber_data; /* 1   */
    uint8_t  bAlternateSetting_data;/* 0   */
    uint8_t  bNumEndpoints_data;    /* 2   */
    uint8_t  bInterfaceClass_data;  /* 0x0A CDC Data */
    uint8_t  bInterfaceSubClass_data;/* 0x00 */
    uint8_t  bInterfaceProtocol_data;/* 0x00 */
    uint8_t  iInterface_data;       /* 0   */

    /* ---- Bulk IN Endpoint ---- */
    uint8_t  bLength_in;            /* 7   */
    uint8_t  bDescriptorType_in;    /* 0x05 */
    uint8_t  bEndpointAddress_in;   /* CDC_DATA_IN_EP */
    uint8_t  bmAttributes_in;       /* 0x02 Bulk */
    uint16_t wMaxPacketSize_in;     /* CDC_DATA_EP_SIZE */
    uint8_t  bInterval_in;          /* 0   */

    /* ---- Bulk OUT Endpoint ---- */
    uint8_t  bLength_out;           /* 7   */
    uint8_t  bDescriptorType_out;   /* 0x05 */
    uint8_t  bEndpointAddress_out;  /* CDC_DATA_OUT_EP */
    uint8_t  bmAttributes_out;      /* 0x02 Bulk */
    uint16_t wMaxPacketSize_out;    /* CDC_DATA_EP_SIZE */
    uint8_t  bInterval_out;         /* 0   */
} __attribute__((packed)) cdc_acm_config_desc_t;

#pragma pack(pop)
```

---

## CDC ACM Class Requests

The host sends these requests over the default control endpoint (EP0) to the **Control Interface**:

| Request | bRequest | Direction | Description |
|---------|----------|-----------|-------------|
| SET_LINE_CODING | 0x20 | Host→Device | Baud, stop bits, parity, data bits |
| GET_LINE_CODING | 0x21 | Device→Host | Report current line coding |
| SET_CONTROL_LINE_STATE | 0x22 | Host→Device | DTR / RTS signals |
| SEND_BREAK | 0x23 | Host→Device | Generate RS-232 break condition |

### Line Coding Structure

```c
/* USB CDC spec Table 17 */
typedef struct {
    uint32_t dwDTERate;     /* Baud rate, e.g. 115200 */
    uint8_t  bCharFormat;   /* 0=1 stop, 1=1.5 stop, 2=2 stop */
    uint8_t  bParityType;   /* 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space */
    uint8_t  bDataBits;     /* 5, 6, 7, 8, or 16 */
} __attribute__((packed)) cdc_line_coding_t;
```

### Control Line State Bits

```c
#define CDC_CTRL_LINE_STATE_DTR  (1u << 0)  /* Data Terminal Ready */
#define CDC_CTRL_LINE_STATE_RTS  (1u << 1)  /* Request To Send     */
```

The device optionally responds with SERIAL_STATE notifications to report DCD, DSR, RING, BREAK, and framing/overrun errors back to the host.

---

## Implementation in C/C++

### STM32 Example (HAL/LL)

The STM32 USB Device library (STM32_USB_Device_Library) provides a CDC class template. Below is a minimal, clean implementation showing the key callback hooks.

```c
/* usbd_cdc_if.c — Device-side CDC ACM interface */
#include "usbd_cdc_if.h"
#include <string.h>

#define APP_RX_BUF_SIZE  512
#define APP_TX_BUF_SIZE  512

static uint8_t rx_buf[APP_RX_BUF_SIZE];
static uint8_t tx_buf[APP_TX_BUF_SIZE];
static volatile uint32_t tx_len = 0;
static volatile bool     tx_busy = false;

/* Forwarded to the USB stack via USBD_CDC_ItfTypeDef */
extern USBD_HandleTypeDef hUsbDeviceFS;

/* --------------------------------------------------------
 * CDC_Init_FS
 * Called by USB stack after enumeration completes.
 * -------------------------------------------------------- */
static int8_t CDC_Init_FS(void)
{
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_buf, 0);
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_buf);
    return USBD_OK;
}

/* --------------------------------------------------------
 * CDC_DeInit_FS
 * Called on USB disconnect or reset.
 * -------------------------------------------------------- */
static int8_t CDC_DeInit_FS(void)
{
    tx_busy = false;
    return USBD_OK;
}

/* --------------------------------------------------------
 * CDC_Control_FS
 * Handles SET_LINE_CODING, SET_CONTROL_LINE_STATE, etc.
 * The USB stack calls this from the EP0 IRQ context.
 * -------------------------------------------------------- */
static cdc_line_coding_t line_coding = {
    .dwDTERate   = 115200,
    .bCharFormat = 0,   /* 1 stop bit */
    .bParityType = 0,   /* None       */
    .bDataBits   = 8,
};

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)length;
    switch (cmd) {
    case CDC_SET_LINE_CODING:
        memcpy(&line_coding, pbuf, sizeof(cdc_line_coding_t));
        /* Optionally reconfigure a physical UART here:
           uart_reconfigure(line_coding.dwDTERate,
                            line_coding.bDataBits,
                            line_coding.bParityType,
                            line_coding.bCharFormat);
        */
        break;

    case CDC_GET_LINE_CODING:
        memcpy(pbuf, &line_coding, sizeof(cdc_line_coding_t));
        break;

    case CDC_SET_CONTROL_LINE_STATE: {
        /* pbuf[0] contains DTR/RTS bits in a 2-byte wValue */
        uint16_t ctrl = (uint16_t)pbuf[0] | ((uint16_t)pbuf[1] << 8);
        bool dtr = (ctrl & CDC_CTRL_LINE_STATE_DTR) != 0;
        bool rts = (ctrl & CDC_CTRL_LINE_STATE_RTS) != 0;
        (void)dtr; (void)rts;
        /* React to DTR: many bootloaders reset the MCU when DTR toggles */
        break;
    }

    case CDC_SEND_BREAK:
        /* Duration in ms encoded in wValue (0xFFFF = indefinite) */
        break;

    default:
        break;
    }
    return USBD_OK;
}

/* --------------------------------------------------------
 * CDC_Receive_FS
 * Called from USB OUT endpoint IRQ when a packet arrives.
 * IMPORTANT: Must call USBD_CDC_ReceivePacket() to re-arm
 * the endpoint before returning, or host will stall.
 * -------------------------------------------------------- */
static int8_t CDC_Receive_FS(uint8_t *buf, uint32_t *len)
{
    /* Echo back (loopback demo) */
    CDC_Transmit_FS(buf, (uint16_t)*len);

    /* Re-arm the OUT endpoint */
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

/* --------------------------------------------------------
 * CDC_Transmit_FS
 * Public API — call from application to send data.
 * Returns USBD_BUSY if a previous transfer is pending.
 * -------------------------------------------------------- */
uint8_t CDC_Transmit_FS(uint8_t *buf, uint16_t len)
{
    USBD_CDC_HandleTypeDef *hcdc =
        (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;

    if (hcdc->TxState != 0) {
        return USBD_BUSY;
    }
    USBD_CDC_SetTxBuffer(&hUsbDeviceFS, buf, len);
    return USBD_CDC_TransmitPacket(&hUsbDeviceFS);
}

/* ---- Register callbacks with USB stack ---- */
USBD_CDC_ItfTypeDef USBD_Interface_fops_FS = {
    CDC_Init_FS,
    CDC_DeInit_FS,
    CDC_Control_FS,
    CDC_Receive_FS,
};
```

#### Zero-Length Packet (ZLP) Handling

When a bulk transfer length is an exact multiple of the endpoint's `wMaxPacketSize`, the host cannot determine where the transfer ends without a ZLP. Always send it:

```c
uint8_t CDC_Transmit_ZLP_Safe(uint8_t *data, uint32_t len)
{
    uint8_t ret = CDC_Transmit_FS(data, (uint16_t)len);
    if (ret != USBD_OK) return ret;

    /* If len is a multiple of wMaxPacketSize, append ZLP */
    if ((len % CDC_DATA_EP_SIZE) == 0) {
        /* Wait for previous transfer to complete first */
        USBD_CDC_HandleTypeDef *hcdc =
            (USBD_CDC_HandleTypeDef *)hUsbDeviceFS.pClassData;
        uint32_t timeout = 100000;
        while (hcdc->TxState != 0 && --timeout) {}
        if (timeout == 0) return USBD_FAIL;

        ret = CDC_Transmit_FS(NULL, 0);
    }
    return ret;
}
```

---

### Linux Kernel Driver Basics

The `cdc_acm` kernel module binds automatically. To write a custom device driver that augments it:

```c
/* my_cdc_acm.c — minimal USB driver skeleton */
#include <linux/module.h>
#include <linux/usb.h>
#include <linux/tty.h>

#define MY_VENDOR_ID   0x1234
#define MY_PRODUCT_ID  0x5678

static const struct usb_device_id my_id_table[] = {
    { USB_DEVICE(MY_VENDOR_ID, MY_PRODUCT_ID) },
    { }
};
MODULE_DEVICE_TABLE(usb, my_id_table);

struct my_cdc_dev {
    struct usb_device   *udev;
    struct usb_interface *iface;
    struct urb          *rx_urb;
    uint8_t             *rx_buf;
    size_t               rx_buf_size;
    __u8                 bulk_in_ep;
    __u8                 bulk_out_ep;
};

/* URB completion handler — called from interrupt context */
static void my_rx_complete(struct urb *urb)
{
    struct my_cdc_dev *dev = urb->context;
    int status = urb->status;

    if (status) {
        if (status != -ENOENT && status != -ECONNRESET && status != -ESHUTDOWN)
            dev_err(&dev->iface->dev, "RX error: %d\n", status);
        return;
    }

    /* Process received data: urb->actual_length bytes in dev->rx_buf */
    dev_info(&dev->iface->dev, "Received %u bytes\n", urb->actual_length);

    /* Re-submit URB for continuous reception */
    usb_submit_urb(urb, GFP_ATOMIC);
}

static int my_probe(struct usb_interface *iface,
                    const struct usb_device_id *id)
{
    struct usb_host_interface *iface_desc = iface->cur_altsetting;
    struct my_cdc_dev *dev;
    struct usb_endpoint_descriptor *ep;
    int i, ret;

    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) return -ENOMEM;

    dev->udev  = usb_get_dev(interface_to_usbdev(iface));
    dev->iface = iface;

    /* Locate bulk endpoints */
    for (i = 0; i < iface_desc->desc.bNumEndpoints; i++) {
        ep = &iface_desc->endpoint[i].desc;
        if (usb_endpoint_is_bulk_in(ep))
            dev->bulk_in_ep = ep->bEndpointAddress;
        else if (usb_endpoint_is_bulk_out(ep))
            dev->bulk_out_ep = ep->bEndpointAddress;
    }

    /* Allocate and fill RX URB */
    dev->rx_buf_size = 512;
    dev->rx_buf = usb_alloc_coherent(dev->udev, dev->rx_buf_size,
                                     GFP_KERNEL, &dev->rx_urb->transfer_dma);
    dev->rx_urb = usb_alloc_urb(0, GFP_KERNEL);
    usb_fill_bulk_urb(dev->rx_urb, dev->udev,
                      usb_rcvbulkpipe(dev->udev, dev->bulk_in_ep),
                      dev->rx_buf, dev->rx_buf_size,
                      my_rx_complete, dev);
    dev->rx_urb->transfer_flags |= URB_NO_TRANSFER_DMA_MAP;

    usb_set_intfdata(iface, dev);

    ret = usb_submit_urb(dev->rx_urb, GFP_KERNEL);
    if (ret) {
        dev_err(&iface->dev, "Failed to submit RX URB: %d\n", ret);
        goto err;
    }
    return 0;

err:
    kfree(dev);
    return ret;
}

static void my_disconnect(struct usb_interface *iface)
{
    struct my_cdc_dev *dev = usb_get_intfdata(iface);
    usb_kill_urb(dev->rx_urb);
    usb_free_urb(dev->rx_urb);
    usb_free_coherent(dev->udev, dev->rx_buf_size,
                      dev->rx_buf, dev->rx_urb->transfer_dma);
    usb_put_dev(dev->udev);
    kfree(dev);
}

static struct usb_driver my_driver = {
    .name       = "my_cdc_acm",
    .id_table   = my_id_table,
    .probe      = my_probe,
    .disconnect = my_disconnect,
};
module_usb_driver(my_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Custom CDC ACM USB Driver");
```

---

### libusb Host-Side Example

For user-space host programs (no kernel driver required):

```c
/* host_cdc_acm.c — libusb-based CDC ACM host */
#include <libusb-1.0/libusb.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

#define VENDOR_ID    0x1234
#define PRODUCT_ID   0x5678
#define CTRL_IFACE   0
#define DATA_IFACE   1
#define BULK_IN_EP   0x82
#define BULK_OUT_EP  0x02
#define TIMEOUT_MS   1000

/* CDC ACM request codes */
#define CDC_SET_LINE_CODING          0x20
#define CDC_GET_LINE_CODING          0x21
#define CDC_SET_CONTROL_LINE_STATE   0x22

#pragma pack(push, 1)
typedef struct {
    uint32_t dwDTERate;
    uint8_t  bCharFormat;
    uint8_t  bParityType;
    uint8_t  bDataBits;
} cdc_line_coding_t;
#pragma pack(pop)

static int cdc_set_line_coding(libusb_device_handle *h,
                                uint32_t baud, uint8_t data_bits,
                                uint8_t parity, uint8_t stop_bits)
{
    cdc_line_coding_t lc = {
        .dwDTERate   = baud,
        .bCharFormat = stop_bits,
        .bParityType = parity,
        .bDataBits   = data_bits,
    };
    return libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        CDC_SET_LINE_CODING,
        0, CTRL_IFACE,
        (uint8_t *)&lc, sizeof(lc),
        TIMEOUT_MS);
}

static int cdc_set_control_line_state(libusb_device_handle *h,
                                       bool dtr, bool rts)
{
    uint16_t wValue = (dtr ? 0x01 : 0x00) | (rts ? 0x02 : 0x00);
    return libusb_control_transfer(h,
        LIBUSB_REQUEST_TYPE_CLASS | LIBUSB_RECIPIENT_INTERFACE,
        CDC_SET_CONTROL_LINE_STATE,
        wValue, CTRL_IFACE,
        NULL, 0,
        TIMEOUT_MS);
}

int main(void)
{
    libusb_context       *ctx  = NULL;
    libusb_device_handle *dev  = NULL;
    uint8_t  rx_buf[512];
    uint8_t  tx_buf[] = "Hello from libusb CDC ACM!\r\n";
    int      transferred = 0;
    int      ret;

    libusb_init(&ctx);
    libusb_set_option(ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    dev = libusb_open_device_with_vid_pid(ctx, VENDOR_ID, PRODUCT_ID);
    if (!dev) {
        fprintf(stderr, "Device not found\n");
        return 1;
    }

    /* Detach kernel driver on both interfaces if active */
    for (int iface = 0; iface <= 1; iface++) {
        if (libusb_kernel_driver_active(dev, iface) == 1) {
            libusb_detach_kernel_driver(dev, iface);
        }
        libusb_claim_interface(dev, iface);
    }

    /* Configure: 115200 8N1, assert DTR+RTS */
    cdc_set_line_coding(dev, 115200, 8, 0, 0);
    cdc_set_control_line_state(dev, true, true);

    /* Transmit */
    ret = libusb_bulk_transfer(dev, BULK_OUT_EP,
                               tx_buf, sizeof(tx_buf) - 1,
                               &transferred, TIMEOUT_MS);
    printf("Sent %d bytes (ret=%d)\n", transferred, ret);

    /* Receive */
    ret = libusb_bulk_transfer(dev, BULK_IN_EP,
                               rx_buf, sizeof(rx_buf),
                               &transferred, TIMEOUT_MS);
    if (ret == 0) {
        rx_buf[transferred] = '\0';
        printf("Received: %s\n", rx_buf);
    }

    /* Cleanup */
    cdc_set_control_line_state(dev, false, false);
    libusb_release_interface(dev, 0);
    libusb_release_interface(dev, 1);
    libusb_close(dev);
    libusb_exit(ctx);
    return 0;
}
```

---

## Implementation in Rust

### Embassy USB CDC ACM (Embedded)

[Embassy](https://embassy.dev/) is the premier async embedded Rust framework. Its `embassy-usb` crate provides a first-class CDC ACM implementation.

```toml
# Cargo.toml
[dependencies]
embassy-executor  = { version = "0.6", features = ["arch-cortex-m", "executor-thread"] }
embassy-usb       = { version = "0.3", features = ["defmt"] }
embassy-usb-driver= { version = "0.1" }
embassy-stm32     = { version = "0.1", features = ["stm32f401re", "time-driver-any"] }
defmt             = "0.3"
defmt-rtt         = "0.4"
static_cell       = "2"
```

```rust
// src/main.rs — Embassy CDC ACM on STM32F401
#![no_std]
#![no_main]

use defmt::*;
use embassy_executor::Spawner;
use embassy_stm32::time::Hertz;
use embassy_stm32::usb::{Driver, Instance};
use embassy_stm32::{bind_interrupts, peripherals, usb, Config};
use embassy_usb::class::cdc_acm::{CdcAcmClass, State};
use embassy_usb::driver::EndpointError;
use embassy_usb::{Builder, UsbDevice};
use static_cell::StaticCell;

// Bind USB interrupt to the embassy USB handler
bind_interrupts!(struct Irqs {
    OTG_FS => usb::InterruptHandler<peripherals::USB_OTG_FS>;
});

// Static buffers required by embassy-usb
static DEVICE_DESC:    StaticCell<[u8; 256]>  = StaticCell::new();
static CONFIG_DESC:    StaticCell<[u8; 256]>  = StaticCell::new();
static BOS_DESC:       StaticCell<[u8; 256]>  = StaticCell::new();
static CTRL_BUF:       StaticCell<[u8; 64]>   = StaticCell::new();
static CDC_STATE:      StaticCell<State>       = StaticCell::new();

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let mut config = Config::default();
    // Enable HSE and configure PLL for 84 MHz (STM32F401)
    config.rcc.hse = Some(Hertz(8_000_000));
    config.rcc.sys_ck = Some(Hertz(84_000_000));
    config.rcc.pll48 = true; // Required for USB

    let p = embassy_stm32::init(config);

    // Create USB driver from peripherals
    let driver = Driver::new_fs(
        p.USB_OTG_FS,
        Irqs,
        p.PA12, // D+ (DP)
        p.PA11, // D- (DM)
    );

    // USB device configuration
    let mut usb_config = embassy_usb::Config::new(0x16c0, 0x27dd);
    usb_config.manufacturer    = Some("Embedded Rust");
    usb_config.product         = Some("CDC ACM Serial");
    usb_config.serial_number   = Some("1234");
    usb_config.max_power       = 100;
    usb_config.max_packet_size_0 = 64;
    usb_config.device_class    = 0xEF; // Miscellaneous (for IAD)
    usb_config.device_sub_class = 0x02;
    usb_config.device_protocol  = 0x01;

    let mut builder = Builder::new(
        driver,
        usb_config,
        DEVICE_DESC.init([0; 256]),
        CONFIG_DESC.init([0; 256]),
        BOS_DESC.init([0; 256]),
        &mut [],
        CTRL_BUF.init([0; 64]),
    );

    // Add CDC ACM class — this registers both interfaces automatically
    let state = CDC_STATE.init(State::new());
    let class = CdcAcmClass::new(&mut builder, state, 64);

    let usb = builder.build();

    // Spawn USB task and the echo task
    spawner.spawn(usb_task(usb)).unwrap();
    spawner.spawn(echo_task(class)).unwrap();
}

// Task: runs the USB stack (handles enumeration, control transfers, etc.)
#[embassy_executor::task]
async fn usb_task(mut device: UsbDevice<'static, Driver<'static, peripherals::USB_OTG_FS>>) {
    device.run().await;
}

// Task: application-level CDC ACM echo loop
#[embassy_executor::task]
async fn echo_task(
    mut class: CdcAcmClass<'static, Driver<'static, peripherals::USB_OTG_FS>>,
) {
    loop {
        // Wait for host to open the port (DTR assert)
        class.wait_connection().await;
        info!("CDC ACM: host connected");

        // Run echo until disconnect
        let _ = echo(&mut class).await;
        info!("CDC ACM: host disconnected");
    }
}

// Echo bytes back to sender; returns on disconnect
async fn echo(
    class: &mut CdcAcmClass<'static, Driver<'static, peripherals::USB_OTG_FS>>,
) -> Result<(), EndpointError> {
    let mut buf = [0u8; 64];
    loop {
        let n = class.read_packet(&mut buf).await?;
        let data = &buf[..n];
        info!("RX {} bytes: {:x}", n, data);
        class.write_packet(data).await?;
    }
}
```

#### Splitting Sender and Receiver

For concurrent RX/TX from separate tasks, split the class:

```rust
use embassy_usb::class::cdc_acm::CdcAcmClass;

// After building the class:
let (mut sender, mut receiver) = class.split();

// Spawn independent tasks:
spawner.spawn(rx_task(receiver)).unwrap();
spawner.spawn(tx_task(sender)).unwrap();

#[embassy_executor::task]
async fn rx_task(
    mut rx: embassy_usb::class::cdc_acm::Receiver<
        'static,
        Driver<'static, peripherals::USB_OTG_FS>
    >,
) {
    let mut buf = [0u8; 64];
    loop {
        match rx.read_packet(&mut buf).await {
            Ok(n)  => info!("RX: {} bytes", n),
            Err(e) => { warn!("RX error: {:?}", e); break; }
        }
    }
}
```

---

### serialport-rs (Host Side)

On the host, a VCP shows up as a regular serial port. The `serialport` crate provides cross-platform access:

```toml
# Cargo.toml (host application)
[dependencies]
serialport = "4"
```

```rust
// src/main.rs — Host-side CDC ACM serial communication
use serialport::{available_ports, SerialPortType, SerialPortInfo};
use std::io::{self, Read, Write};
use std::time::Duration;

fn find_cdc_port() -> Option<String> {
    let ports = available_ports().expect("Failed to enumerate ports");
    for port in ports {
        if let SerialPortType::UsbPort(ref info) = port.port_type {
            println!(
                "Found USB port: {} (VID:{:04x} PID:{:04x})",
                port.port_name,
                info.vid,
                info.pid
            );
            // Match on known VID/PID or just return first USB serial
            if info.vid == 0x1234 && info.pid == 0x5678 {
                return Some(port.port_name.clone());
            }
        }
    }
    None
}

fn main() -> io::Result<()> {
    // List all available ports
    println!("Available serial ports:");
    for port in available_ports().unwrap_or_default() {
        println!("  {}: {:?}", port.port_name, port.port_type);
    }

    let port_name = find_cdc_port()
        .unwrap_or_else(|| "/dev/ttyACM0".to_string());

    println!("Opening {}", port_name);

    let mut port = serialport::new(&port_name, 115_200)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(Duration::from_millis(500))
        .open()
        .expect("Failed to open port");

    // Send a message
    let msg = b"Hello VCP!\r\n";
    port.write_all(msg)?;
    port.flush()?;
    println!("Sent: {:?}", std::str::from_utf8(msg).unwrap().trim());

    // Read response
    let mut response = vec![0u8; 64];
    match port.read(&mut response) {
        Ok(n) => {
            let s = String::from_utf8_lossy(&response[..n]);
            println!("Received: {}", s.trim());
        }
        Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
            println!("Read timed out (no data)");
        }
        Err(e) => return Err(e),
    }

    Ok(())
}
```

#### Async Host Serial with Tokio

```rust
// Async CDC ACM reading with tokio-serial
// Cargo.toml: tokio-serial = "5", tokio = { version = "1", features = ["full"] }
use tokio_serial::SerialPortBuilderExt;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

#[tokio::main]
async fn main() {
    let mut port = tokio_serial::new("/dev/ttyACM0", 115200)
        .open_native_async()
        .expect("Failed to open port");

    port.write_all(b"ping\r\n").await.unwrap();

    let mut buf = [0u8; 256];
    let n = port.read(&mut buf).await.unwrap();
    println!("Got: {}", String::from_utf8_lossy(&buf[..n]));
}
```

---

## Line Coding and Control Signals

Even though CDC ACM operates over USB (which has its own flow control), the line coding parameters serve important purposes:

1. **Bridge mode**: When the VCP bridges to a real UART, the MCU must reconfigure the hardware UART whenever SET_LINE_CODING arrives.
2. **Bootloaders**: Tools like `avrdude`, `esptool.py`, and Arduino IDE use specific baud rates as signals to trigger reset or bootloader entry.
3. **DTR/RTS**: The Arduino auto-reset mechanism toggles DTR to pulse the MCU's RESET pin via a 100 nF capacitor.

```c
/* Example: Apply line coding to physical UART (STM32 HAL) */
void apply_line_coding_to_uart(const cdc_line_coding_t *lc, UART_HandleTypeDef *huart)
{
    huart->Init.BaudRate = lc->dwDTERate;

    switch (lc->bDataBits) {
    case 7:  huart->Init.WordLength = UART_WORDLENGTH_8B; /* 7+parity=8b frame */ break;
    case 8:  huart->Init.WordLength = UART_WORDLENGTH_8B; break;
    case 9:  huart->Init.WordLength = UART_WORDLENGTH_9B; break;
    default: huart->Init.WordLength = UART_WORDLENGTH_8B; break;
    }

    switch (lc->bParityType) {
    case 0:  huart->Init.Parity = UART_PARITY_NONE; break;
    case 1:  huart->Init.Parity = UART_PARITY_ODD;  break;
    case 2:  huart->Init.Parity = UART_PARITY_EVEN; break;
    default: huart->Init.Parity = UART_PARITY_NONE; break;
    }

    switch (lc->bCharFormat) {
    case 0:  huart->Init.StopBits = UART_STOPBITS_1; break;
    case 2:  huart->Init.StopBits = UART_STOPBITS_2; break;
    default: huart->Init.StopBits = UART_STOPBITS_1; break;
    }

    HAL_UART_Init(huart);
}
```

---

## Data Flow and Endpoint Management

Understanding the data path prevents common bugs:

```
Host App (write)
     │
     ▼
  OS USB Stack (bulk OUT transfer)
     │   max 512 bytes/transfer (HS), 64 bytes (FS)
     ▼
  CDC Data OUT EP (0x02)  ──► Device Receive Buffer
                                     │
                              Application Callback
                                     │
                              Device Transmit Buffer
                                     │
  CDC Data IN EP (0x82)  ◄──  USB Stack (bulk IN transfer)
     │
     ▼
  Host App (read)
```

### Critical Rules

1. **Always re-arm the OUT endpoint.** After `CDC_Receive_FS` is called, you must call `USBD_CDC_ReceivePacket()` to prepare for the next packet. Forgetting this causes the host's write to stall indefinitely.

2. **Handle `USBD_BUSY`.** The IN endpoint can only carry one transfer at a time. If `CDC_Transmit_FS()` returns `USBD_BUSY`, buffer the data and retry, or implement a circular TX queue.

3. **ZLP on exact multiples.** See the ZLP section under C examples.

4. **Buffer alignment.** Many USB DMA controllers require 4-byte aligned buffers. Use `__attribute__((aligned(4)))` or `#pragma pack` appropriately.

---

## Platform Driver Integration

### Windows: Custom INF (pre-Win10)

For Windows 7/8, an `.inf` file is needed to bind `usbser.sys`:

```ini
; cdc_acm.inf
[Version]
Signature   = "$Windows NT$"
Class       = Ports
ClassGuid   = {4D36E978-E325-11CE-BFC1-08002BE10318}
Provider    = %ManufacturerName%
DriverVer   = 01/01/2024,1.0.0.0

[Manufacturer]
%ManufacturerName% = DeviceList, NTx86, NTamd64

[DeviceList.NTx86]
%DeviceName% = DriverInstall, USB\VID_1234&PID_5678

[DeviceList.NTamd64]
%DeviceName% = DriverInstall, USB\VID_1234&PID_5678

[DriverInstall]
Include     = mdmcpq.inf
CopyFiles   = FakeModemCopyFileSection
AddReg      = DriverAddReg

[DriverAddReg]
HKR,,DevLoader,,*ntkern
HKR,,NTMPDriver,,usbser.sys
HKR,,EnumPropPages32,,"MsPorts.dll,SerialPortPropPageProvider"

[Strings]
ManufacturerName = "My Company"
DeviceName       = "My CDC ACM Serial Port"
```

### Linux: udev Rule

```bash
# /etc/udev/rules.d/99-my-cdc.rules
SUBSYSTEM=="tty", ATTRS{idVendor}=="1234", ATTRS{idProduct}=="5678", \
    SYMLINK+="ttyMyCDC", MODE="0666", GROUP="dialout"
```

Reload with: `sudo udevadm control --reload-rules && sudo udevadm trigger`

---

## Common Pitfalls and Debugging

| Symptom | Root Cause | Fix |
|---------|-----------|-----|
| Device not enumerated | Incorrect descriptor (wrong bLength, wTotalLength mismatch) | Validate with `lsusb -v` or USBlyzer |
| Port opens then hangs | OUT endpoint not re-armed after receive | Always call `USBD_CDC_ReceivePacket()` in callback |
| Data truncated at 64 bytes | wMaxPacketSize mismatch between descriptor and endpoint config | Ensure consistency; handle multi-packet reads |
| Sporadic data corruption | Buffer not 4-byte aligned for DMA | Add `__attribute__((aligned(4)))` |
| Host sees `USBD_BUSY` constantly | TX not completing (no ZLP, endpoint stall) | Check ZLP logic; verify endpoint is not stalled |
| Windows asks for driver (Win10+) | Missing or wrong `bDeviceClass`/`bInterfaceClass` | Set `0x02` for CDC control interface |
| Arduino IDE fails to reset | DTR toggle not handled | Pulse NRST on DTR rising/falling edge |
| `ttyACM0` not created on Linux | `cdc_acm.ko` not matching descriptors | Ensure `bmCapabilities` and Union descriptor are correct |

### Debugging with Wireshark + USBPcap

Capture USB traffic on Windows:

```
wireshark -i \\.\USBPcap1 -k -f "usb.idVendor == 0x1234"
```

On Linux (usbmon):

```bash
sudo modprobe usbmon
sudo wireshark -i usbmon0 -k
# Filter: usb.idVendor == 0x1234 && usb.transfer_type == 0x03  (bulk)
```

---

## Summary

**USB CDC ACM** is the universally supported mechanism for creating Virtual COM Ports over USB. Key takeaways:

**Architecture**: CDC ACM uses two USB interfaces — a Control Interface (class 0x02) for class requests and notifications, and a Data Interface (class 0x0A) for bulk bidirectional data transfer. An Interface Association Descriptor (IAD) groups them for the host.

**Descriptors**: Precise descriptor construction is mandatory. The functional descriptors (Header, Call Management, ACM, Union) on the Control Interface tell the host exactly what capabilities the device supports. `bmCapabilities = 0x02` is the minimum for a functional VCP.

**Class Requests**: `SET_LINE_CODING`, `GET_LINE_CODING`, `SET_CONTROL_LINE_STATE`, and `SEND_BREAK` are the four requests a compliant VCP must handle. In bridge mode, line coding parameters must be applied to the physical UART.

**C/C++ on Embedded (STM32)**: The STM32 USB Device Library provides `USBD_CDC_ItfTypeDef` callbacks. Critical rules: always re-arm the OUT endpoint in the receive callback, handle `USBD_BUSY` for transmit, and send a ZLP when a transfer is exactly `wMaxPacketSize` bytes.

**C/C++ on Host (libusb/Linux kernel)**: libusb allows user-space VCP control without a kernel driver. The Linux `cdc_acm` module binds automatically when descriptors are correct; custom kernel drivers use URBs for asynchronous bulk I/O.

**Rust on Embedded (Embassy)**: Embassy's `embassy-usb` crate provides a high-level, async `CdcAcmClass` that handles the full CDC ACM protocol. The class can be split into independent sender/receiver halves for concurrent tasks. `wait_connection()` suspends until the host opens the port.

**Rust on Host (serialport-rs)**: Once enumerated, a VCP appears as `/dev/ttyACM*` (Linux), `COM*` (Windows), or `/dev/tty.usbmodem*` (macOS) and is accessed with the standard `serialport` or `tokio-serial` crates, identical to any hardware COM port.

**Platform Integration**: Windows 10+ requires no INF file. Earlier Windows versions need an INF to bind `usbser.sys`. Linux benefits from a udev rule for stable device naming. DTR/RTS signals enable Arduino-style auto-reset functionality.

---

*Document covers USB CDC specification v1.2 class implementation. Code examples target STM32F4 (C/C++) and STM32F401 with Embassy (Rust); adapt endpoint addresses and peripheral names for your specific hardware.*