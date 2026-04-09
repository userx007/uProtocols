# 83. ZigBee Serial Protocol — Using UART to Control ZigBee Modules

**Architecture & Concepts** — ZigBee device roles (Coordinator, Router, End Device), network topology, 64-bit vs 16-bit addressing, and a comparison table against BLE/Wi-Fi/LoRa.

**AT Command Mode** — The `+++` escape sequence, all major AT commands (`ATID`, `ATAP`, `ATEE`, `ATWR`, etc.), and a full C example that enters command mode, reads firmware version, address, association status, and configures the PAN ID.

**API Frame Mode** — Complete frame structure breakdown (`0x7E` start, 2-byte big-endian length, frame type, data, checksum), the checksum algorithm, and a reference table of all major frame types (`0x10` TX Request, `0x8B` TX Status, `0x90` RX Packet, `0x08` AT Command, `0x17` Remote AT, `0x8A` Modem Status).

**C/C++ Implementation** — A full `zigbee_api.h/.c` library with frame builders, a byte-level state-machine parser, and RX/TX decoders. A C++ `ZigBeeController` class wraps this with callbacks, `poll()`, synchronous `send_at()` with timeout, and a practical sensor gateway application.

**Rust Implementation** — Idiomatic Rust with `thiserror` error types, `FrameType`/`DeliveryStatus` enums, a `FrameParser` state machine, and a `ZigBeeController` using the `serialport` crate with closure-based callbacks.

**Reliability** — Delivery status code tables, association indicator codes, and a pitfall table covering voltage mismatches, flow control, sleeping end devices, and frame ID management.

---

## Table of Contents

1. [Introduction](#introduction)
2. [ZigBee Architecture Overview](#zigbee-architecture-overview)
3. [UART Interface to ZigBee Modules](#uart-interface-to-zigbee-modules)
4. [AT Command Mode](#at-command-mode)
5. [API Frame Mode (XBee API)](#api-frame-mode-xbee-api)
6. [Frame Structure Deep Dive](#frame-structure-deep-dive)
7. [Common API Frame Types](#common-api-frame-types)
8. [C/C++ Implementation](#cc-implementation)
9. [Rust Implementation](#rust-implementation)
10. [Error Handling and Reliability](#error-handling-and-reliability)
11. [Practical Application: Sensor Network](#practical-application-sensor-network)
12. [Summary](#summary)

---

## Introduction

ZigBee is a low-power, low-data-rate wireless mesh networking standard based on the IEEE 802.15.4 specification. It operates in the 2.4 GHz ISM band (globally) and also in 868/915 MHz sub-GHz bands. ZigBee is widely deployed in home automation, industrial monitoring, smart energy systems, and wireless sensor networks (WSN).

Most embedded systems interact with ZigBee radio modules (such as Digi XBee, Texas Instruments CC2530, Silicon Labs EM357, or EBYTE E18) exclusively over **UART** — a simple, universal serial interface. The host microcontroller or processor sends and receives structured serial data to configure the module and exchange wireless packets with peer devices.

### Why UART for ZigBee?

- ZigBee modules contain their own 802.15.4/ZigBee MAC+PHY stack in firmware
- The host only needs to pass data and commands — no RF or protocol stack needed on the host
- UART is universally available on all MCUs, SBCs, and embedded Linux systems
- Typical baud rates: **9600, 38400, 57600, 115200** bps (module-dependent, default usually 9600)
- Optional hardware flow control: **RTS/CTS** lines prevent buffer overflow at high data rates

### ZigBee vs Other Wireless Protocols

| Property          | ZigBee          | Bluetooth LE    | Wi-Fi           | LoRa            |
|-------------------|-----------------|-----------------|-----------------|-----------------|
| Range             | 10–100 m        | 10–50 m         | 50–100 m        | 1–15 km         |
| Data Rate         | 250 kbps        | 1–2 Mbps        | 10–600 Mbps     | 0.3–50 kbps     |
| Topology          | Mesh            | Star/Mesh       | Star            | Star            |
| Power             | Very Low        | Low             | High            | Very Low        |
| Node Count        | Up to 65,000    | ~7              | ~250            | ~1000s          |
| Stack Complexity  | Medium          | Medium          | High            | Low             |

---

## ZigBee Architecture Overview

ZigBee defines three device types that form the mesh network:

```
  [End Device]    [End Device]    [End Device]
       |               |               |
  [Router] --------- [Router] ------ [Router]
       \                                /
        \______ [Coordinator] _________/
                      |
               [Host via UART]
```

### Device Roles

**Coordinator (ZC):**
- Unique per network — there is exactly one
- Starts and manages the network (PAN ID, channel selection)
- Acts as a Trust Centre for security key distribution
- Typically mains-powered; always awake
- Connected to the host application via UART

**Router (ZR):**
- Extends network range by forwarding packets
- Must remain awake to relay messages
- Can also originate/receive application data
- Typically mains-powered

**End Device (ZED):**
- Leaf node — cannot route packets
- Can sleep deeply to conserve battery
- Must periodically poll its parent router for buffered messages
- Suitable for battery-powered sensors

### Network Addressing

ZigBee uses two address types:
- **64-bit Extended (IEEE) Address (EUI-64):** Factory-assigned, globally unique (e.g., `0x0013A20041234567`)
- **16-bit Short Address (NWK Address):** Assigned at join time by the Coordinator (e.g., `0xAB12`)

Broadcast addresses:
- `0xFFFF` — All devices
- `0xFFFD` — All routers and coordinator
- `0xFFFC` — All routers only

---

## UART Interface to ZigBee Modules

### Physical Connection

```
Host MCU / Linux SBC          ZigBee Module (e.g., XBee)
─────────────────────         ──────────────────────────
  TX  ──────────────────────►  DIN  (Data In)
  RX  ◄──────────────────────  DOUT (Data Out)
  GND ──────────────────────── GND
  3.3V ─────────────────────── VCC  (3.3V, check module)
  RTS ──────────────────────►  CTS  (optional HW flow)
  CTS ◄──────────────────────  RTS  (optional HW flow)
  DTR/SLEEP ────────────────►  SLEEP_RQ (optional)
  RESET ────────────────────►  RESET    (optional)
```

> **Voltage Warning:** Most XBee modules are 3.3 V logic. Do NOT connect directly to 5V UART without a level shifter. Modules may be destroyed or give unreliable readings.

### UART Configuration (Typical Defaults)

| Parameter     | Value                  |
|---------------|------------------------|
| Baud Rate     | 9600 (configurable)    |
| Data Bits     | 8                      |
| Parity        | None                   |
| Stop Bits     | 1                      |
| Flow Control  | None (or RTS/CTS)      |

### Operating Modes

ZigBee modules like the XBee series support two distinct operating modes selected at firmware level or via command:

| Mode            | Description                                              |
|-----------------|----------------------------------------------------------|
| **Transparent** | Everything received over UART is broadcast wirelessly; received wireless data goes straight to UART TX. Simple pipe. |
| **API Mode**    | Structured binary frames with addressing, checksum, and metadata. Required for multi-node networks, unicast, and remote configuration. |

---

## AT Command Mode

In **Transparent Mode**, ZigBee modules expose an AT command interface for configuration. The host must enter command mode by sending the escape sequence `+++` (with a 1-second guard time before and after).

### Entering/Exiting AT Command Mode

```
1. Wait 1 second (guard time — no data)
2. Send:  +++
3. Wait for: OK\r
4. Now in command mode — send AT commands
5. Exit with: ATCN\r   (or wait for timeout ~10s)
```

### Common AT Commands (XBee ZigBee)

| Command    | Description                        | Example             |
|------------|------------------------------------|---------------------|
| `ATID`     | Get/Set PAN ID                     | `ATID1234\r`        |
| `ATCH`     | Get/Set channel mask               | `ATCH7FFF\r`        |
| `ATMY`     | Read 16-bit network address        | `ATMY\r`            |
| `ATSH`     | Read 64-bit address high word      | `ATSH\r`            |
| `ATSL`     | Read 64-bit address low word       | `ATSL\r`            |
| `ATNI`     | Node Identifier string             | `ATNICOORD_1\r`     |
| `ATBD`     | Baud rate (0=1200 … 7=115200)      | `ATBD6\r` (57600)   |
| `ATAP`     | API mode (0=off, 1=on, 2=escaped)  | `ATAP1\r`           |
| `ATEE`     | Encryption enable                  | `ATEE1\r`           |
| `ATKY`     | AES-128 encryption key             | `ATKY0102...1E1F\r` |
| `ATWR`     | Write settings to flash            | `ATWR\r`            |
| `ATFR`     | Software reset                     | `ATFR\r`            |
| `ATVR`     | Firmware version                   | `ATVR\r`            |
| `ATAI`     | Association indication (0=joined)  | `ATAI\r`            |

### AT Command C Example

```c
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>

/* Open and configure UART port */
int uart_open(const char *device, int baud) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) return -1;

    struct termios tty = {0};
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8; /* 8-bit chars */
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;  /* 1.0 s read timeout */
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);

    tcsetattr(fd, TCSANOW, &tty);
    return fd;
}

/* Read a line terminated by \r from UART */
int uart_readline(int fd, char *buf, size_t maxlen, int timeout_ms) {
    size_t i = 0;
    char c;
    while (i < maxlen - 1) {
        if (read(fd, &c, 1) == 1) {
            if (c == '\r') break;
            buf[i++] = c;
        } else {
            if (timeout_ms-- <= 0) return -1;
            usleep(1000);
        }
    }
    buf[i] = '\0';
    return (int)i;
}

/* Enter AT command mode */
int xbee_enter_command_mode(int fd) {
    char resp[32];
    usleep(1100000);              /* 1.1s guard time before */
    write(fd, "+++", 3);
    usleep(1100000);              /* 1.1s guard time after  */
    int n = uart_readline(fd, resp, sizeof(resp), 3000);
    return (n > 0 && strncmp(resp, "OK", 2) == 0) ? 0 : -1;
}

/* Send AT command and get response */
int xbee_at_command(int fd, const char *cmd, char *resp, size_t resp_len) {
    char line[64];
    snprintf(line, sizeof(line), "%s\r", cmd);
    write(fd, line, strlen(line));
    usleep(50000);  /* 50ms for module to process */
    return uart_readline(fd, resp, resp_len, 2000);
}

int main(void) {
    int fd = uart_open("/dev/ttyUSB0", B9600);
    if (fd < 0) { perror("uart_open"); return 1; }

    /* Enter AT command mode */
    if (xbee_enter_command_mode(fd) != 0) {
        fprintf(stderr, "Failed to enter command mode\n");
        return 1;
    }
    printf("Entered AT command mode\n");

    char resp[64];

    /* Read firmware version */
    xbee_at_command(fd, "ATVR", resp, sizeof(resp));
    printf("Firmware: %s\n", resp);

    /* Read 64-bit address */
    xbee_at_command(fd, "ATSH", resp, sizeof(resp));
    printf("Addr High: 0x%s\n", resp);
    xbee_at_command(fd, "ATSL", resp, sizeof(resp));
    printf("Addr Low:  0x%s\n", resp);

    /* Read 16-bit network address */
    xbee_at_command(fd, "ATMY", resp, sizeof(resp));
    printf("NWK Addr:  0x%s\n", resp);

    /* Check association status (0x00 = successfully joined) */
    xbee_at_command(fd, "ATAI", resp, sizeof(resp));
    printf("Assoc Status: 0x%s %s\n", resp,
           strcmp(resp, "0") == 0 ? "(Joined)" : "(Not Joined)");

    /* Set PAN ID to 0xABCD and write to flash */
    xbee_at_command(fd, "ATIDABCD", resp, sizeof(resp));
    xbee_at_command(fd, "ATWR", resp, sizeof(resp));
    printf("PAN ID set and saved: %s\n", resp);

    /* Exit command mode */
    xbee_at_command(fd, "ATCN", resp, sizeof(resp));
    printf("Exited command mode\n");

    close(fd);
    return 0;
}
```

---

## API Frame Mode (XBee API)

API Mode replaces the simple transparent pipe with a **structured binary frame protocol**. Each frame carries addressing, payload length, frame type, and a checksum. API Mode is essential when you need:

- Unicast to a specific device by 64-bit or 16-bit address
- Receive metadata (RSSI, source address, ACK status)
- Remote AT command configuration of peer modules
- Multi-hop routing awareness
- Detection of transmission success/failure

Enable API Mode with `ATAP1` (unescaped) or `ATAP2` (escaped — wraps special bytes `0x7E, 0x7D, 0x11, 0x13` with escape byte `0x7D` XOR `0x20`).

---

## Frame Structure Deep Dive

Every XBee API frame follows this structure:

```
Byte:  [0]      [1][2]      [3]         [4 .. 3+Length]   [3+Length+1]
       0x7E    Length(BE)  Frame Type   Frame Data          Checksum
       Start   MSB  LSB                 (variable)          (1 byte)
```

### Field Descriptions

| Field         | Size    | Description                                              |
|---------------|---------|----------------------------------------------------------|
| Start Delim   | 1 byte  | Always `0x7E` — marks start of frame                    |
| Length        | 2 bytes | Big-endian count of bytes from Frame Type to last data byte (NOT including start, length, or checksum) |
| Frame Type    | 1 byte  | Identifies the frame purpose (see table below)           |
| Frame Data    | N bytes | Contents depend on Frame Type                            |
| Checksum      | 1 byte  | `0xFF - (sum of all Frame Data bytes & 0xFF)`            |

### Checksum Calculation

```c
uint8_t xbee_checksum(const uint8_t *data, uint16_t len) {
    uint8_t sum = 0;
    for (uint16_t i = 0; i < len; i++) sum += data[i];
    return 0xFF - sum;
}
```

The checksum covers bytes from Frame Type onward (i.e., everything after the 2-byte length field). On the receive side, sum all frame data bytes including the checksum — the result must be `0xFF`.

---

## Common API Frame Types

| Frame Type | Hex    | Direction     | Description                              |
|------------|--------|---------------|------------------------------------------|
| AT Command | `0x08` | Host → Module | Local AT command                         |
| AT Cmd Queued | `0x09` | Host → Module | Queued (apply with `AC` or `WR`)      |
| ZigBee TX Request | `0x10` | Host → Module | Send data to remote node          |
| Explicit TX | `0x11` | Host → Module | Send with ZigBee cluster/endpoint      |
| Remote AT Cmd | `0x17` | Host → Module | Send AT command to remote module       |
| AT Response | `0x88` | Module → Host | Response to `0x08` or `0x09`           |
| Modem Status | `0x8A` | Module → Host | Network events (joined, disassoc.)     |
| ZigBee TX Status | `0x8B` | Module → Host | Delivery status of TX Request        |
| ZigBee RX Packet | `0x90` | Module → Host | Received data from remote node       |
| RX Indicator (Explicit) | `0x91` | Module → Host | Received with cluster/endpoint  |
| Node ID Indicator | `0x95` | Module → Host | Node discovery response             |
| Remote AT Response | `0x97` | Module → Host | Response from `0x17`               |

---

### Frame 0x10 — ZigBee Transmit Request

Used to send data to a specific remote device.

```
Frame Type: 0x10
[0]    Frame ID      (1 byte, 0x00 = no TX Status reply)
[1-8]  Dest 64-bit   (8 bytes, big-endian)
[9-10] Dest 16-bit   (2 bytes, 0xFFFE = unknown, let module resolve)
[11]   Broadcast Rad (1 byte, 0x00 = use NH, 0xFF = 1 hop)
[12]   Options       (1 byte, 0x00 = normal)
[13+]  RF Data       (variable, up to 72 bytes for ZigBee)
```

**Broadcast address shortcuts:**
- 64-bit: `0x000000000000FFFF` → broadcast all
- 64-bit: `0x000000000000FFFE` → broadcast routers+coordinator

### Frame 0x90 — ZigBee Receive Packet

```
[0-7]  Source 64-bit address   (8 bytes)
[8-9]  Source 16-bit address   (2 bytes)
[10]   Receive Options         (1 byte: 0x01=ACKed, 0x02=broadcast)
[11+]  RF Data                 (variable)
```

### Frame 0x8B — ZigBee Transmit Status

```
[0]    Frame ID                (matches TX Request frame ID)
[1-2]  Dest 16-bit (resolved)  (2 bytes)
[3]    Retry Count             (number of MAC retries)
[4]    Delivery Status         (0x00=success, 0x01=MAC ACK fail, 0x21=network ACK fail...)
[5]    Discovery Status        (0x00=no overhead, 0x02=route discovery)
```

---

## C/C++ Implementation

### C — Full API Frame Library

```c
/* zigbee_api.h */
#ifndef ZIGBEE_API_H
#define ZIGBEE_API_H

#include <stdint.h>
#include <stddef.h>

#define XBEE_START_DELIM       0x7E
#define XBEE_FRAME_AT_CMD      0x08
#define XBEE_FRAME_AT_RESP     0x88
#define XBEE_FRAME_TX_REQ      0x10
#define XBEE_FRAME_TX_STATUS   0x8B
#define XBEE_FRAME_RX_PKT      0x90
#define XBEE_FRAME_MODEM_STAT  0x8A
#define XBEE_FRAME_REMOTE_AT   0x17
#define XBEE_FRAME_REMOTE_RESP 0x97

#define XBEE_MAX_FRAME_SIZE    128
#define XBEE_MAX_RF_PAYLOAD    72

/* Delivery status codes */
#define XBEE_DELIVERY_SUCCESS    0x00
#define XBEE_DELIVERY_MAC_FAIL   0x01
#define XBEE_DELIVERY_NET_FAIL   0x21
#define XBEE_DELIVERY_NO_ROUTE   0x25

typedef struct {
    uint8_t  frame_type;
    uint16_t data_len;
    uint8_t  data[XBEE_MAX_FRAME_SIZE];
} xbee_frame_t;

typedef struct {
    uint64_t dest64;        /* 64-bit destination address           */
    uint16_t dest16;        /* 16-bit destination (0xFFFE=unknown)  */
    uint8_t  frame_id;      /* 0 = no TX Status response            */
    uint8_t  broadcast_rad; /* 0x00 = use network hops setting      */
    uint8_t  options;       /* 0x00 = normal                        */
    const uint8_t *payload;
    uint8_t  payload_len;
} xbee_tx_request_t;

typedef struct {
    uint64_t src64;
    uint16_t src16;
    uint8_t  options;
    uint8_t  payload[XBEE_MAX_RF_PAYLOAD];
    uint8_t  payload_len;
} xbee_rx_packet_t;

typedef struct {
    uint8_t  frame_id;
    uint16_t dest16;
    uint8_t  retry_count;
    uint8_t  delivery_status;
    uint8_t  discovery_status;
} xbee_tx_status_t;

/* Build raw bytes for a TX Request frame into 'out'. Returns total frame length. */
int  xbee_build_tx_request(const xbee_tx_request_t *req,
                            uint8_t *out, size_t out_size);

/* Build raw bytes for a local AT command frame. Returns total frame length. */
int  xbee_build_at_command(uint8_t frame_id, const char *at_cmd,
                            const uint8_t *param, uint8_t param_len,
                            uint8_t *out, size_t out_size);

/* Parse a raw received byte stream into an xbee_frame_t.
 * Call repeatedly with each new byte. Returns 1 when a complete frame
 * has been received, 0 if more bytes are needed, -1 on checksum error. */
int  xbee_parse_byte(uint8_t byte, xbee_frame_t *frame, int *state,
                     uint16_t *rx_len, uint8_t *rx_checksum);

/* Decode a Frame Type 0x90 (RX Packet) from frame->data */
int  xbee_decode_rx_packet(const xbee_frame_t *frame, xbee_rx_packet_t *pkt);

/* Decode a Frame Type 0x8B (TX Status) from frame->data */
int  xbee_decode_tx_status(const xbee_frame_t *frame, xbee_tx_status_t *status);

#endif /* ZIGBEE_API_H */
```

```c
/* zigbee_api.c */
#include "zigbee_api.h"
#include <string.h>

/* ── Checksum ───────────────────────────────────────────────── */
static uint8_t calc_checksum(const uint8_t *data, uint16_t len) {
    uint8_t s = 0;
    for (uint16_t i = 0; i < len; i++) s += data[i];
    return 0xFF - s;
}

/* ── Build TX Request Frame ─────────────────────────────────── */
int xbee_build_tx_request(const xbee_tx_request_t *req,
                           uint8_t *out, size_t out_size) {
    /* Data section: type(1) + frame_id(1) + dest64(8) + dest16(2)
     *               + bcast_rad(1) + options(1) + payload(N) */
    uint16_t data_len = 14 + req->payload_len;
    size_t   total    = 4 + data_len; /* start+len(2)+data+chk */

    if (total > out_size) return -1;

    uint8_t *p = out;
    *p++ = XBEE_START_DELIM;
    *p++ = (data_len >> 8) & 0xFF;
    *p++ = data_len & 0xFF;

    uint8_t *frame_start = p;   /* checksum covers from here */

    *p++ = XBEE_FRAME_TX_REQ;
    *p++ = req->frame_id;

    /* 64-bit destination, big-endian */
    for (int i = 7; i >= 0; i--)
        *p++ = (req->dest64 >> (i * 8)) & 0xFF;

    /* 16-bit destination, big-endian */
    *p++ = (req->dest16 >> 8) & 0xFF;
    *p++ = req->dest16 & 0xFF;

    *p++ = req->broadcast_rad;
    *p++ = req->options;

    memcpy(p, req->payload, req->payload_len);
    p += req->payload_len;

    *p++ = calc_checksum(frame_start, data_len);

    return (int)(p - out);
}

/* ── Build Local AT Command Frame ───────────────────────────── */
int xbee_build_at_command(uint8_t frame_id, const char *at_cmd,
                           const uint8_t *param, uint8_t param_len,
                           uint8_t *out, size_t out_size) {
    uint16_t data_len = 3 + param_len;  /* type + frame_id + cmd(2) + param */
    size_t   total    = 4 + data_len;

    if (total > out_size) return -1;

    uint8_t *p = out;
    *p++ = XBEE_START_DELIM;
    *p++ = (data_len >> 8) & 0xFF;
    *p++ = data_len & 0xFF;

    uint8_t *frame_start = p;
    *p++ = XBEE_FRAME_AT_CMD;
    *p++ = frame_id;
    *p++ = (uint8_t)at_cmd[0];
    *p++ = (uint8_t)at_cmd[1];

    if (param && param_len > 0) {
        memcpy(p, param, param_len);
        p += param_len;
    }

    *p++ = calc_checksum(frame_start, data_len);
    return (int)(p - out);
}

/* ── Incremental Frame Parser State Machine ─────────────────── */
typedef enum {
    STATE_IDLE = 0,
    STATE_LEN_MSB,
    STATE_LEN_LSB,
    STATE_DATA,
    STATE_CHECKSUM
} parse_state_t;

int xbee_parse_byte(uint8_t byte, xbee_frame_t *frame, int *state,
                    uint16_t *rx_len, uint8_t *rx_checksum) {
    switch ((parse_state_t)*state) {

    case STATE_IDLE:
        if (byte == XBEE_START_DELIM) {
            frame->data_len = 0;
            *rx_len = 0;
            *rx_checksum = 0;
            *state = STATE_LEN_MSB;
        }
        break;

    case STATE_LEN_MSB:
        frame->data_len = (uint16_t)byte << 8;
        *state = STATE_LEN_LSB;
        break;

    case STATE_LEN_LSB:
        frame->data_len |= byte;
        if (frame->data_len == 0 || frame->data_len > XBEE_MAX_FRAME_SIZE) {
            *state = STATE_IDLE;  /* invalid length */
        } else {
            *state = STATE_DATA;
        }
        break;

    case STATE_DATA:
        frame->data[*rx_len] = byte;
        *rx_checksum += byte;
        (*rx_len)++;
        if (*rx_len == frame->data_len)
            *state = STATE_CHECKSUM;
        break;

    case STATE_CHECKSUM:
        *state = STATE_IDLE;
        frame->frame_type = frame->data[0];
        if ((uint8_t)(*rx_checksum + byte) == 0xFF)
            return 1;   /* complete valid frame */
        else
            return -1;  /* checksum mismatch */
    }

    return 0;  /* frame incomplete */
}

/* ── Decode RX Packet (0x90) ────────────────────────────────── */
int xbee_decode_rx_packet(const xbee_frame_t *frame, xbee_rx_packet_t *pkt) {
    if (frame->frame_type != XBEE_FRAME_RX_PKT) return -1;
    if (frame->data_len < 12) return -1;

    const uint8_t *d = frame->data + 1;  /* skip frame type byte */

    /* 64-bit source address */
    pkt->src64 = 0;
    for (int i = 0; i < 8; i++)
        pkt->src64 = (pkt->src64 << 8) | d[i];

    /* 16-bit source address */
    pkt->src16 = ((uint16_t)d[8] << 8) | d[9];

    pkt->options     = d[10];
    pkt->payload_len = frame->data_len - 12;

    if (pkt->payload_len > XBEE_MAX_RF_PAYLOAD) return -1;
    memcpy(pkt->payload, d + 11, pkt->payload_len);

    return 0;
}

/* ── Decode TX Status (0x8B) ────────────────────────────────── */
int xbee_decode_tx_status(const xbee_frame_t *frame, xbee_tx_status_t *status) {
    if (frame->frame_type != XBEE_FRAME_TX_STATUS) return -1;
    if (frame->data_len < 6) return -1;

    const uint8_t *d = frame->data + 1;
    status->frame_id         = d[0];
    status->dest16           = ((uint16_t)d[1] << 8) | d[2];
    status->retry_count      = d[3];
    status->delivery_status  = d[4];
    status->discovery_status = d[5];

    return 0;
}
```

### C++ — Object-Oriented ZigBee Controller

```cpp
/* ZigBeeController.hpp */
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include <optional>

extern "C" {
#include "zigbee_api.h"
}

class ZigBeeController {
public:
    /* Callback types */
    using RxCallback = std::function<void(const xbee_rx_packet_t&)>;
    using TxCallback = std::function<void(const xbee_tx_status_t&)>;

    explicit ZigBeeController(const std::string& port, int baud = 9600);
    ~ZigBeeController();

    /* Prevent copy */
    ZigBeeController(const ZigBeeController&)            = delete;
    ZigBeeController& operator=(const ZigBeeController&) = delete;

    bool open();
    void close();

    /* Send data to a 64-bit addressed ZigBee node.
     * Returns frame_id on success, -1 on failure. */
    int send(uint64_t dest64, uint16_t dest16,
             const uint8_t* data, uint8_t len,
             bool request_ack = true);

    /* Send a local AT command with optional parameter.
     * Synchronous — waits for response up to timeout_ms. */
    std::optional<std::vector<uint8_t>>
    send_at(const std::string& cmd,
            const std::vector<uint8_t>& param = {},
            int timeout_ms = 2000);

    /* Send remote AT command to a peer module */
    int send_remote_at(uint64_t dest64, const std::string& cmd,
                       const std::vector<uint8_t>& param = {});

    /* Register callbacks for async processing */
    void on_receive(RxCallback cb)   { rx_cb_ = std::move(cb); }
    void on_tx_status(TxCallback cb) { tx_cb_ = std::move(cb); }

    /* Call regularly in the main loop to process incoming data */
    void poll();

    /* Broadcast to all nodes on network */
    int broadcast(const uint8_t* data, uint8_t len);

    bool is_open() const { return fd_ >= 0; }

private:
    int  uart_read_byte(uint8_t& byte, int timeout_ms);
    void process_frame(const xbee_frame_t& frame);

    std::string port_;
    int         baud_;
    int         fd_     {-1};
    uint8_t     next_frame_id_ {1};

    xbee_frame_t rx_frame_ {};
    int          parse_state_   {0};
    uint16_t     parse_rx_len_  {0};
    uint8_t      parse_checksum_{0};

    RxCallback rx_cb_;
    TxCallback tx_cb_;
};
```

```cpp
/* ZigBeeController.cpp */
#include "ZigBeeController.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <cstring>
#include <cassert>
#include <chrono>
#include <stdexcept>

static int baud_to_const(int baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     throw std::invalid_argument("Unsupported baud rate");
    }
}

ZigBeeController::ZigBeeController(const std::string& port, int baud)
    : port_(port), baud_(baud) {}

ZigBeeController::~ZigBeeController() { close(); }

bool ZigBeeController::open() {
    fd_ = ::open(port_.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0) return false;

    struct termios tty{};
    int speed = baud_to_const(baud_);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);
    tty.c_cflag  = CS8 | CLOCAL | CREAD;
    tty.c_iflag  = 0;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;
    tcflush(fd_, TCIFLUSH);
    tcsetattr(fd_, TCSANOW, &tty);
    return true;
}

void ZigBeeController::close() {
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
}

int ZigBeeController::uart_read_byte(uint8_t& byte, int timeout_ms) {
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);
    struct timeval tv{ timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
    if (select(fd_ + 1, &fds, nullptr, nullptr, &tv) <= 0) return 0;
    return (int)read(fd_, &byte, 1);
}

int ZigBeeController::send(uint64_t dest64, uint16_t dest16,
                            const uint8_t* data, uint8_t len,
                            bool request_ack) {
    xbee_tx_request_t req{};
    req.dest64        = dest64;
    req.dest16        = dest16;
    req.frame_id      = request_ack ? next_frame_id_++ : 0;
    req.broadcast_rad = 0x00;
    req.options       = 0x00;
    req.payload       = data;
    req.payload_len   = len;

    if (next_frame_id_ == 0) next_frame_id_ = 1;  /* avoid frame_id=0 when ack wanted */

    uint8_t buf[XBEE_MAX_FRAME_SIZE + 8];
    int n = xbee_build_tx_request(&req, buf, sizeof(buf));
    if (n <= 0) return -1;

    return write(fd_, buf, n) == n ? (int)req.frame_id : -1;
}

std::optional<std::vector<uint8_t>>
ZigBeeController::send_at(const std::string& cmd,
                           const std::vector<uint8_t>& param,
                           int timeout_ms) {
    uint8_t fid = next_frame_id_++;
    uint8_t buf[32];
    int n = xbee_build_at_command(fid, cmd.c_str(),
                                   param.empty() ? nullptr : param.data(),
                                   (uint8_t)param.size(), buf, sizeof(buf));
    if (n <= 0 || write(fd_, buf, n) != n) return std::nullopt;

    /* Wait for matching AT Response frame */
    auto deadline = std::chrono::steady_clock::now()
                  + std::chrono::milliseconds(timeout_ms);
    xbee_frame_t frame{};
    int state = 0; uint16_t rxl = 0; uint8_t rxcs = 0;

    while (std::chrono::steady_clock::now() < deadline) {
        uint8_t byte;
        if (uart_read_byte(byte, 10) != 1) continue;
        int rc = xbee_parse_byte(byte, &frame, &state, &rxl, &rxcs);
        if (rc == 1 && frame.frame_type == XBEE_FRAME_AT_RESP) {
            /* frame.data: [frame_type][frame_id][cmd(2)][status][value...] */
            if (frame.data[1] == fid && frame.data[4] == 0x00) {
                /* Status 0x00 = OK */
                uint16_t val_len = frame.data_len - 5;
                return std::vector<uint8_t>(frame.data + 5,
                                            frame.data + 5 + val_len);
            }
            return std::nullopt; /* AT error */
        }
    }
    return std::nullopt; /* timeout */
}

int ZigBeeController::broadcast(const uint8_t* data, uint8_t len) {
    /* 0x000000000000FFFF = ZigBee broadcast all */
    return send(0x000000000000FFFFULL, 0xFFFF, data, len, false);
}

void ZigBeeController::process_frame(const xbee_frame_t& frame) {
    switch (frame.frame_type) {
    case XBEE_FRAME_RX_PKT: {
        xbee_rx_packet_t pkt{};
        if (xbee_decode_rx_packet(&frame, &pkt) == 0 && rx_cb_)
            rx_cb_(pkt);
        break;
    }
    case XBEE_FRAME_TX_STATUS: {
        xbee_tx_status_t status{};
        if (xbee_decode_tx_status(&frame, &status) == 0 && tx_cb_)
            tx_cb_(status);
        break;
    }
    case XBEE_FRAME_MODEM_STAT: {
        /* frame.data[1]: 0x00=HW reset, 0x02=joined, 0x03=disassoc */
        uint8_t modem_status = frame.data[1];
        const char* desc = (modem_status == 0x02) ? "Joined network"
                         : (modem_status == 0x03) ? "Disassociated"
                         : (modem_status == 0x06) ? "Coordinator started"
                         : "Other";
        (void)desc;  /* log as needed */
        break;
    }
    default:
        break;
    }
}

void ZigBeeController::poll() {
    uint8_t byte;
    while (uart_read_byte(byte, 0) == 1) {
        int rc = xbee_parse_byte(byte, &rx_frame_,
                                  &parse_state_, &parse_rx_len_,
                                  &parse_checksum_);
        if (rc == 1)       process_frame(rx_frame_);
        else if (rc == -1) { /* checksum error — reset parser */ parse_state_ = 0; }
    }
}

/* ── Usage Example ─────────────────────────────────────────── */
#ifdef ZIGBEE_EXAMPLE_MAIN
#include <iostream>
#include <thread>

int main() {
    ZigBeeController zbee("/dev/ttyUSB0", 9600);
    if (!zbee.open()) { std::cerr << "Failed to open UART\n"; return 1; }

    /* Register callbacks */
    zbee.on_receive([](const xbee_rx_packet_t& pkt) {
        printf("[RX] From %016llX: ", (unsigned long long)pkt.src64);
        fwrite(pkt.payload, 1, pkt.payload_len, stdout);
        printf("\n");
    });

    zbee.on_tx_status([](const xbee_tx_status_t& s) {
        if (s.delivery_status == XBEE_DELIVERY_SUCCESS)
            printf("[TX] Frame %d delivered OK (retries=%d)\n",
                   s.frame_id, s.retry_count);
        else
            printf("[TX] Frame %d FAILED: 0x%02X\n",
                   s.frame_id, s.delivery_status);
    });

    /* Read module's NWK address via AT command */
    auto result = zbee.send_at("MY");
    if (result && result->size() == 2) {
        uint16_t my_addr = ((*result)[0] << 8) | (*result)[1];
        printf("My NWK address: 0x%04X\n", my_addr);
    }

    /* Send a unicast message to node 0x0013A20041234567 */
    const char *msg = "Hello ZigBee";
    uint64_t target = 0x0013A20041234567ULL;
    int fid = zbee.send(target, 0xFFFE,
                        (const uint8_t*)msg, (uint8_t)strlen(msg));
    printf("Sent with frame_id=%d\n", fid);

    /* Main loop */
    while (true) {
        zbee.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return 0;
}
#endif /* ZIGBEE_EXAMPLE_MAIN */
```

---

## Rust Implementation

### Cargo.toml Dependencies

```toml
[package]
name = "zigbee-uart"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4.3"
thiserror  = "1.0"
log        = "0.4"
```

### Core Types and Error Handling

```rust
// src/error.rs
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ZigBeeError {
    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),

    #[error("Frame too large: {0} bytes")]
    FrameTooLarge(usize),

    #[error("Checksum mismatch: expected {expected:#04x}, got {got:#04x}")]
    ChecksumMismatch { expected: u8, got: u8 },

    #[error("Invalid frame type: {0:#04x}")]
    InvalidFrameType(u8),

    #[error("Buffer too small")]
    BufferTooSmall,

    #[error("AT command failed with status {0:#04x}")]
    AtCommandFailed(u8),

    #[error("Timeout waiting for response")]
    Timeout,

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

pub type ZigBeeResult<T> = Result<T, ZigBeeError>;
```

```rust
// src/frame.rs
use crate::error::{ZigBeeError, ZigBeeResult};

pub const START_DELIM: u8 = 0x7E;
pub const MAX_FRAME_DATA: usize = 128;
pub const MAX_RF_PAYLOAD:  usize = 72;

/// ZigBee broadcast 64-bit address (all devices)
pub const ADDR64_BROADCAST_ALL: u64 = 0x000000000000FFFF;
/// ZigBee broadcast 64-bit address (routers + coordinator)
pub const ADDR64_BROADCAST_ROUTERS: u64 = 0x000000000000FFFE;
/// Unknown 16-bit address — let module discover via routing
pub const ADDR16_UNKNOWN: u16 = 0xFFFE;

/// Frame type identifiers
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum FrameType {
    AtCommand      = 0x08,
    AtResponse     = 0x88,
    TxRequest      = 0x10,
    TxStatus       = 0x8B,
    RxPacket       = 0x90,
    ModemStatus    = 0x8A,
    RemoteAtCmd    = 0x17,
    RemoteAtResp   = 0x97,
}

impl TryFrom<u8> for FrameType {
    type Error = ZigBeeError;
    fn try_from(v: u8) -> ZigBeeResult<Self> {
        match v {
            0x08 => Ok(Self::AtCommand),
            0x88 => Ok(Self::AtResponse),
            0x10 => Ok(Self::TxRequest),
            0x8B => Ok(Self::TxStatus),
            0x90 => Ok(Self::RxPacket),
            0x8A => Ok(Self::ModemStatus),
            0x17 => Ok(Self::RemoteAtCmd),
            0x97 => Ok(Self::RemoteAtResp),
            other => Err(ZigBeeError::InvalidFrameType(other)),
        }
    }
}

/// Delivery status for TX Status frames
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum DeliveryStatus {
    Success,
    MacAckFailure,
    NetworkAckFailure,
    NoRoute,
    Other(u8),
}

impl From<u8> for DeliveryStatus {
    fn from(v: u8) -> Self {
        match v {
            0x00 => Self::Success,
            0x01 => Self::MacAckFailure,
            0x21 => Self::NetworkAckFailure,
            0x25 => Self::NoRoute,
            other => Self::Other(other),
        }
    }
}

/// Parsed ZigBee API frame
#[derive(Debug)]
pub struct Frame {
    pub frame_type: u8,
    pub data: [u8; MAX_FRAME_DATA],
    pub data_len: usize,
}

impl Frame {
    pub fn new() -> Self {
        Self { frame_type: 0, data: [0u8; MAX_FRAME_DATA], data_len: 0 }
    }
}

/// Decoded RX Packet (frame type 0x90)
#[derive(Debug)]
pub struct RxPacket {
    pub src64:       u64,
    pub src16:       u16,
    pub options:     u8,
    pub payload:     [u8; MAX_RF_PAYLOAD],
    pub payload_len: usize,
}

/// Decoded TX Status (frame type 0x8B)
#[derive(Debug)]
pub struct TxStatus {
    pub frame_id:         u8,
    pub dest16:           u16,
    pub retry_count:      u8,
    pub delivery_status:  DeliveryStatus,
    pub discovery_status: u8,
}

/// Compute XBee frame checksum over `data`
fn checksum(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
    0xFF_u8.wrapping_sub(sum)
}

/// Serialize a TX Request (0x10) into `buf`. Returns bytes written.
pub fn build_tx_request(
    dest64: u64, dest16: u16, frame_id: u8,
    broadcast_rad: u8, options: u8,
    payload: &[u8], buf: &mut [u8],
) -> ZigBeeResult<usize> {
    let data_len = 14 + payload.len();
    let total    =  4 + data_len;
    if total > buf.len() { return Err(ZigBeeError::BufferTooSmall); }
    if total > MAX_FRAME_DATA + 4 { return Err(ZigBeeError::FrameTooLarge(total)); }

    buf[0] = START_DELIM;
    buf[1] = ((data_len >> 8) & 0xFF) as u8;
    buf[2] = (data_len & 0xFF) as u8;

    let d = &mut buf[3..];
    d[0] = FrameType::TxRequest as u8;
    d[1] = frame_id;

    // 64-bit destination (big-endian)
    for i in 0..8 {
        d[2 + i] = ((dest64 >> (56 - i * 8)) & 0xFF) as u8;
    }

    // 16-bit destination (big-endian)
    d[10] = ((dest16 >> 8) & 0xFF) as u8;
    d[11] = (dest16 & 0xFF) as u8;

    d[12] = broadcast_rad;
    d[13] = options;

    d[14..14 + payload.len()].copy_from_slice(payload);

    buf[3 + data_len] = checksum(&buf[3..3 + data_len]);
    Ok(total)
}

/// Serialize a local AT Command (0x08) into `buf`. Returns bytes written.
pub fn build_at_command(
    frame_id: u8, cmd: &[u8; 2],
    param: Option<&[u8]>, buf: &mut [u8],
) -> ZigBeeResult<usize> {
    let param_len = param.map_or(0, |p| p.len());
    let data_len  = 4 + param_len;
    let total     = 4 + data_len;

    if total > buf.len() { return Err(ZigBeeError::BufferTooSmall); }

    buf[0] = START_DELIM;
    buf[1] = ((data_len >> 8) & 0xFF) as u8;
    buf[2] = (data_len & 0xFF) as u8;

    let d = &mut buf[3..];
    d[0] = FrameType::AtCommand as u8;
    d[1] = frame_id;
    d[2] = cmd[0];
    d[3] = cmd[1];

    if let Some(p) = param {
        d[4..4 + param_len].copy_from_slice(p);
    }

    buf[3 + data_len] = checksum(&buf[3..3 + data_len]);
    Ok(total)
}

/// Decode a received Frame into an RxPacket
pub fn decode_rx_packet(frame: &Frame) -> ZigBeeResult<RxPacket> {
    if frame.frame_type != FrameType::RxPacket as u8 {
        return Err(ZigBeeError::InvalidFrameType(frame.frame_type));
    }
    if frame.data_len < 12 { return Err(ZigBeeError::BufferTooSmall); }

    let d = &frame.data[1..]; // skip frame_type byte
    let mut src64 = 0u64;
    for i in 0..8 { src64 = (src64 << 8) | d[i] as u64; }

    let src16 = ((d[8] as u16) << 8) | d[9] as u16;
    let options = d[10];
    let payload_len = frame.data_len - 12;
    if payload_len > MAX_RF_PAYLOAD { return Err(ZigBeeError::FrameTooLarge(payload_len)); }

    let mut payload = [0u8; MAX_RF_PAYLOAD];
    payload[..payload_len].copy_from_slice(&d[11..11 + payload_len]);

    Ok(RxPacket { src64, src16, options, payload, payload_len })
}

/// Decode a received Frame into a TxStatus
pub fn decode_tx_status(frame: &Frame) -> ZigBeeResult<TxStatus> {
    if frame.frame_type != FrameType::TxStatus as u8 {
        return Err(ZigBeeError::InvalidFrameType(frame.frame_type));
    }
    if frame.data_len < 6 { return Err(ZigBeeError::BufferTooSmall); }

    let d = &frame.data[1..];
    Ok(TxStatus {
        frame_id:         d[0],
        dest16:           ((d[1] as u16) << 8) | d[2] as u16,
        retry_count:      d[3],
        delivery_status:  DeliveryStatus::from(d[4]),
        discovery_status: d[5],
    })
}
```

### Frame Parser (State Machine)

```rust
// src/parser.rs
use crate::frame::{Frame, MAX_FRAME_DATA, START_DELIM};
use crate::error::{ZigBeeError, ZigBeeResult};

#[derive(Debug, Default, PartialEq)]
enum State {
    #[default]
    Idle,
    LenMsb,
    LenLsb,
    Data,
    Checksum,
}

/// Incremental byte-by-byte frame parser
#[derive(Default)]
pub struct FrameParser {
    state:    State,
    frame:    Frame,
    rx_len:   usize,
    checksum: u8,
    data_len: usize,
}

impl FrameParser {
    pub fn new() -> Self { Self::default() }

    /// Feed one byte. Returns `Ok(Some(frame))` when a complete valid frame
    /// is assembled, `Ok(None)` while still collecting bytes, or `Err` on
    /// a checksum mismatch.
    pub fn feed(&mut self, byte: u8) -> ZigBeeResult<Option<&Frame>> {
        match self.state {
            State::Idle => {
                if byte == START_DELIM {
                    self.rx_len   = 0;
                    self.checksum = 0;
                    self.data_len = 0;
                    self.state    = State::LenMsb;
                }
            }
            State::LenMsb => {
                self.data_len = (byte as usize) << 8;
                self.state    = State::LenLsb;
            }
            State::LenLsb => {
                self.data_len |= byte as usize;
                if self.data_len == 0 || self.data_len > MAX_FRAME_DATA {
                    self.state = State::Idle;
                } else {
                    self.frame.data_len = self.data_len;
                    self.state = State::Data;
                }
            }
            State::Data => {
                self.frame.data[self.rx_len] = byte;
                self.checksum = self.checksum.wrapping_add(byte);
                self.rx_len  += 1;
                if self.rx_len == self.data_len {
                    self.state = State::Checksum;
                }
            }
            State::Checksum => {
                self.state = State::Idle;
                self.frame.frame_type = self.frame.data[0];

                let expected = 0xFF_u8.wrapping_sub(self.checksum);
                if byte == expected {
                    return Ok(Some(&self.frame));
                } else {
                    return Err(ZigBeeError::ChecksumMismatch {
                        expected,
                        got: byte,
                    });
                }
            }
        }
        Ok(None)
    }

    pub fn reset(&mut self) { self.state = State::Idle; }
}
```

### ZigBee Controller

```rust
// src/controller.rs
use std::time::{Duration, Instant};
use serialport::SerialPort;
use crate::error::{ZigBeeError, ZigBeeResult};
use crate::frame::{
    self, Frame, RxPacket, TxStatus, FrameType,
    ADDR16_UNKNOWN,
};
use crate::parser::FrameParser;

pub struct ZigBeeController {
    port:          Box<dyn SerialPort>,
    parser:        FrameParser,
    next_frame_id: u8,
    tx_buf:        [u8; 256],
}

impl ZigBeeController {
    pub fn open(path: &str, baud: u32) -> ZigBeeResult<Self> {
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(10))
            .open()?;

        Ok(Self {
            port,
            parser:        FrameParser::new(),
            next_frame_id: 1,
            tx_buf:        [0u8; 256],
        })
    }

    fn next_id(&mut self) -> u8 {
        let id = self.next_frame_id;
        self.next_frame_id = self.next_frame_id.wrapping_add(1);
        if self.next_frame_id == 0 { self.next_frame_id = 1; }
        id
    }

    /// Send data to a specific 64-bit addressed node.
    /// Returns the frame ID used (needed to match TX Status responses).
    pub fn send(&mut self, dest64: u64, payload: &[u8]) -> ZigBeeResult<u8> {
        let fid = self.next_id();
        let n = frame::build_tx_request(
            dest64, ADDR16_UNKNOWN, fid,
            0x00, 0x00,
            payload, &mut self.tx_buf,
        )?;
        self.port.write_all(&self.tx_buf[..n])?;
        Ok(fid)
    }

    /// Broadcast data to all nodes on the network.
    pub fn broadcast(&mut self, payload: &[u8]) -> ZigBeeResult<()> {
        let n = frame::build_tx_request(
            frame::ADDR64_BROADCAST_ALL, 0xFFFF, 0,
            0x00, 0x00,
            payload, &mut self.tx_buf,
        )?;
        self.port.write_all(&self.tx_buf[..n])?;
        Ok(())
    }

    /// Send a local AT command. Returns the response parameter bytes
    /// (may be empty for set commands).
    pub fn send_at(
        &mut self, cmd: &[u8; 2],
        param: Option<&[u8]>,
        timeout: Duration,
    ) -> ZigBeeResult<Vec<u8>> {
        let fid = self.next_id();
        let n = frame::build_at_command(fid, cmd, param, &mut self.tx_buf)?;
        self.port.write_all(&self.tx_buf[..n])?;

        let deadline = Instant::now() + timeout;
        loop {
            if Instant::now() >= deadline {
                return Err(ZigBeeError::Timeout);
            }
            match self.read_one_frame()? {
                Some(f) if f.frame_type == FrameType::AtResponse as u8 => {
                    // data: [frame_type][frame_id][cmd(2)][status][value...]
                    if f.data[1] != fid { continue; }
                    let status = f.data[4];
                    if status != 0x00 {
                        return Err(ZigBeeError::AtCommandFailed(status));
                    }
                    let value_len = f.data_len.saturating_sub(5);
                    return Ok(f.data[5..5 + value_len].to_vec());
                }
                _ => {}
            }
        }
    }

    /// Non-blocking poll — read and decode any available incoming frames.
    /// Calls the provided closures for RX packets and TX status frames.
    pub fn poll<RxFn, TxFn>(
        &mut self,
        mut on_rx: RxFn,
        mut on_tx: TxFn,
    ) -> ZigBeeResult<()>
    where
        RxFn: FnMut(RxPacket),
        TxFn: FnMut(TxStatus),
    {
        while let Some(f) = self.read_one_frame()? {
            match FrameType::try_from(f.frame_type) {
                Ok(FrameType::RxPacket) => {
                    if let Ok(pkt) = frame::decode_rx_packet(f) {
                        on_rx(pkt);
                    }
                }
                Ok(FrameType::TxStatus) => {
                    if let Ok(status) = frame::decode_tx_status(f) {
                        on_tx(status);
                    }
                }
                Ok(FrameType::ModemStatus) => {
                    let s = f.data[1];
                    log::info!("Modem status: {:#04x} ({})",
                        s, modem_status_str(s));
                }
                _ => {}
            }
        }
        Ok(())
    }

    fn read_one_frame(&mut self) -> ZigBeeResult<Option<&Frame>> {
        let mut byte = [0u8; 1];
        match self.port.read(&mut byte) {
            Ok(1) => match self.parser.feed(byte[0]) {
                Ok(result) => Ok(result),
                Err(ZigBeeError::ChecksumMismatch { .. }) => {
                    log::warn!("Frame checksum error, resetting parser");
                    self.parser.reset();
                    Ok(None)
                }
                Err(e) => Err(e),
            },
            Ok(_) | Err(_) => Ok(None),
        }
    }
}

fn modem_status_str(s: u8) -> &'static str {
    match s {
        0x00 => "Hardware reset",
        0x01 => "Watchdog timer reset",
        0x02 => "Joined network",
        0x03 => "Disassociated",
        0x06 => "Coordinator started",
        0x0D => "Voltage supply limit exceeded",
        _    => "Unknown",
    }
}
```

### Rust Main — Sensor Gateway Example

```rust
// src/main.rs
mod controller;
mod error;
mod frame;
mod parser;

use controller::ZigBeeController;
use frame::DeliveryStatus;
use std::time::Duration;

fn main() -> error::ZigBeeResult<()> {
    env_logger::init();

    let mut zb = ZigBeeController::open("/dev/ttyUSB0", 9600)?;
    println!("ZigBee controller opened");

    // Query firmware version via AT command
    let vr = zb.send_at(b"VR", None, Duration::from_secs(2))?;
    if vr.len() >= 2 {
        println!("Firmware version: {:04X}", ((vr[0] as u16) << 8) | vr[1] as u16);
    }

    // Query our 16-bit network address
    let my = zb.send_at(b"MY", None, Duration::from_secs(2))?;
    if my.len() >= 2 {
        println!("NWK address: {:#06X}", ((my[0] as u16) << 8) | my[1] as u16);
    }

    // Send a unicast message to a known sensor node
    let target: u64 = 0x0013A20041ABCDEF;
    let payload = b"REQ:TEMP";
    let fid = zb.send(target, payload)?;
    println!("Sent request, frame_id={}", fid);

    // Event loop
    let mut received_count = 0usize;
    loop {
        zb.poll(
            |pkt| {
                received_count += 1;
                let text = std::str::from_utf8(&pkt.payload[..pkt.payload_len])
                    .unwrap_or("<binary>");
                println!(
                    "[RX #{}] From {:016X} (NWK:{:04X}): {}",
                    received_count, pkt.src64, pkt.src16, text
                );
            },
            |status| {
                match status.delivery_status {
                    DeliveryStatus::Success =>
                        println!("[TX OK] frame_id={} retries={}",
                                 status.frame_id, status.retry_count),
                    other =>
                        println!("[TX FAIL] frame_id={} status={:?}",
                                 status.frame_id, other),
                }
            },
        )?;

        std::thread::sleep(Duration::from_millis(5));
    }
}
```

---

## Error Handling and Reliability

### ZigBee-Specific UART Pitfalls

| Issue                   | Symptom                              | Mitigation                                    |
|-------------------------|--------------------------------------|-----------------------------------------------|
| Checksum mismatch       | Garbled frames                       | Validate every frame; drop on mismatch        |
| Buffer overflow         | Missing frames under heavy load      | Enable hardware RTS/CTS flow control          |
| Module not in API mode  | Raw bytes arrive, no `0x7E` frames   | Send `ATAP1\r` in AT mode; write with `ATWR`  |
| 3.3V vs 5V mismatch     | Erratic behavior or module damage    | Use a voltage level shifter                   |
| End device sleeping     | TX fails to sleeping leaf nodes      | Configure End Device poll rate; use buffering |
| Network not formed      | TX returns 0x25 (no route)           | Check `ATAI` == 0x00; check PAN ID / channel  |
| Duplicate frame IDs     | TX Status matched to wrong request   | Maintain incrementing, non-zero frame ID      |

### Delivery Status Codes (TX Status Frame 0x8B)

| Code   | Meaning                                              |
|--------|------------------------------------------------------|
| `0x00` | Success                                              |
| `0x01` | MAC ACK failure (direct neighbor not responding)     |
| `0x02` | CCA failure (channel busy)                           |
| `0x15` | Invalid destination endpoint                         |
| `0x21` | Network ACK failure (route exists but remote failed) |
| `0x22` | Not joined to network                                |
| `0x25` | Route not found                                      |
| `0x31` | Internal resource error                              |
| `0x32` | Resource error (retry limit exceeded)                |
| `0x74` | Data payload too large                               |

### Association Status (`ATAI` Response)

| Code   | Meaning                              |
|--------|--------------------------------------|
| `0x00` | Successfully joined                  |
| `0x21` | Scan found no PANs                   |
| `0x22` | Scan found no valid PANs             |
| `0x23` | No joinable beacons found            |
| `0x24` | Unexpected MAC status                |
| `0x27` | Network join failed                  |
| `0xFF` | Attempting to join                   |

---

## Practical Application: Sensor Network

The following shows a realistic coordinator gateway scenario where the host MCU polls multiple sensor end-devices and logs their temperature readings.

### Network Topology

```
  [Temp Sensor A]  [Temp Sensor B]  [Temp Sensor C]
   ZED 0x...AA      ZED 0x...BB      ZED 0x...CC
        |                |                |
   [Router 1] ---- [Router 2] ---- [Router 3]
        \                                /
         \___ [Coordinator 0x...00] ___/
                        |
                    [UART 9600]
                        |
               [Raspberry Pi / MCU]
               (runs coordinator code)
```

### Simple Application Protocol

Define a compact binary payload format between coordinator and sensors:

```
Command Packet (Coordinator → Sensor):
  Byte 0: Command  (0x01=ReadTemp, 0x02=ReadHumid, 0x03=SetSampleRate)
  Byte 1: Param    (for SetSampleRate: interval in seconds)

Response Packet (Sensor → Coordinator):
  Byte 0: Command echo  (mirrors request command)
  Byte 1: Status        (0x00=OK, 0x01=Error)
  Byte 2-3: Value MSB:LSB (temperature in 0.01°C, e.g. 0x0965 = 24.05°C)
```

### Gateway C++ Usage with Error Recovery

```cpp
/* sensor_gateway.cpp */
#include "ZigBeeController.hpp"
#include <unordered_map>
#include <chrono>
#include <thread>
#include <cstdio>

static const uint64_t SENSOR_ADDRS[] = {
    0x0013A20041AAAA00ULL,
    0x0013A20041BBBB00ULL,
    0x0013A20041CCCC00ULL,
};
static const int N_SENSORS = 3;

struct SensorState {
    float   last_temp   {0.0f};
    int     fail_count  {0};
    bool    alive       {true};
};

int main() {
    ZigBeeController zbee("/dev/ttyUSB0", 9600);
    if (!zbee.open()) { fprintf(stderr, "Cannot open UART\n"); return 1; }

    std::unordered_map<uint64_t, SensorState> sensors;
    for (auto addr : SENSOR_ADDRS) sensors[addr] = {};

    /* Handle incoming sensor responses */
    zbee.on_receive([&sensors](const xbee_rx_packet_t& pkt) {
        if (pkt.payload_len < 4) return;
        if (pkt.payload[0] != 0x01) return;  /* only temp responses */
        if (pkt.payload[1] != 0x00) { /* sensor reported error */
            fprintf(stderr, "Sensor %016llX error\n",
                    (unsigned long long)pkt.src64);
            return;
        }
        uint16_t raw  = ((uint16_t)pkt.payload[2] << 8) | pkt.payload[3];
        float    temp = raw / 100.0f;
        sensors[pkt.src64].last_temp  = temp;
        sensors[pkt.src64].fail_count = 0;
        sensors[pkt.src64].alive      = true;
        printf("[%.3f] Sensor %016llX: %.2f°C\n",
               (double)clock() / CLOCKS_PER_SEC,
               (unsigned long long)pkt.src64, temp);
    });

    /* Handle TX status to detect unreachable nodes */
    zbee.on_tx_status([&sensors, &SENSOR_ADDRS](const xbee_tx_status_t& s) {
        (void)SENSOR_ADDRS;
        if (s.delivery_status != XBEE_DELIVERY_SUCCESS) {
            /* We can't directly map frame_id to addr here without
             * a pending-requests table; simplified for example */
            fprintf(stderr, "Delivery failure: frame_id=%d status=0x%02X\n",
                    s.frame_id, s.delivery_status);
        }
    });

    /* Poll sensors every 10 seconds */
    const uint8_t read_temp_cmd[] = { 0x01, 0x00 };
    while (true) {
        for (auto addr : SENSOR_ADDRS) {
            zbee.send(addr, 0xFFFE,
                      read_temp_cmd, sizeof(read_temp_cmd),
                      /*request_ack=*/true);
        }

        /* Process responses for up to 5 seconds */
        auto deadline = std::chrono::steady_clock::now()
                      + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            zbee.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        /* Wait remaining time before next poll cycle */
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
    return 0;
}
```

---

## Summary

ZigBee modules connect to host processors exclusively over **UART**, making it one of the most important UART applications in embedded networking. Here is a concise recap of the key concepts:

### Architecture
ZigBee networks consist of one **Coordinator** (connected to the host over UART), optional **Routers** (range extenders), and **End Devices** (battery-powered leaf nodes). The mesh topology enables self-healing routes with up to 65,000 nodes.

### UART Operating Modes
- **Transparent Mode** with **AT Commands** (`+++` escape sequence): suitable for simple point-to-point links and configuration. Commands like `ATID`, `ATAP`, `ATEE`, and `ATWR` configure the module's PAN ID, API mode, encryption, and persist settings to flash.
- **API Frame Mode** (`ATAP1`/`ATAP2`): structures all communication as typed binary frames with a `0x7E` start delimiter, 2-byte length, frame type, variable data, and a 1-byte checksum. This is the recommended mode for any application with more than two nodes.

### Key API Frames
- `0x10` **TX Request** — send a unicast or broadcast payload to a remote node by 64-bit address
- `0x8B` **TX Status** — delivery confirmation with retry count and failure reason
- `0x90` **RX Packet** — received data with source 64-bit and 16-bit addresses
- `0x08` **AT Command** / `0x88` **AT Response** — local module configuration at runtime
- `0x17` **Remote AT Command** / `0x97` **Remote AT Response** — configure a peer module over the air
- `0x8A` **Modem Status** — network join, disassociation, and hardware reset events

### Implementation Pattern
Both the C/C++ and Rust implementations use the same approach: a **state-machine byte parser** that accumulates incoming UART bytes into frames (Idle → LenMSB → LenLSB → Data → Checksum), validates the checksum, and dispatches decoded frames to application callbacks. Outgoing frames are assembled into a stack-allocated buffer using big-endian serialization and a trailing checksum byte.

### Reliability Considerations
- Always validate the checksum; silently reset the parser on mismatch
- Use non-zero, incrementing **frame IDs** to correlate TX Requests with TX Status responses
- Enable **RTS/CTS hardware flow control** at higher baud rates to prevent buffer overflow
- Monitor **ATAI** (Association Indicator) at startup — a value other than `0x00` means the module has not yet joined a network
- Handle all **delivery status codes** from TX Status frames, especially `0x25` (route not found) and `0x21` (network ACK failure), which require application-level retry logic

ZigBee over UART is the foundation of virtually all commercial ZigBee gateway and coordinator implementations — from smart home hubs to industrial wireless sensor networks.