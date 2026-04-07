# 61. Modbus RTU over UART

- **Protocol overview** — master/slave topology, addressing (1–247), request/response flow
- **Frame structure** — address, function code, data, CRC-16 layout with byte-level diagrams
- **UART configuration** — 8N1 settings, character time calculations, T1.5/T3.5 timing rules
- **Function codes** — table of all major FC codes (0x01–0x10) and exception response format
- **CRC-16/Modbus** — algorithm explanation (poly 0xA001, reflected, init 0xFFFF)
- **C/C++ implementation** — complete, compilable code for POSIX UART init, CRC computation, FC 0x03/0x06/0x10 master functions, and a bare-metal slave dispatcher
- **Rust implementation** — typed error handling with `thiserror`, `serialport`-based master, slave with `Option<Vec<u8>>` response model, and unit tests for CRC
- **Frame walkthroughs** — annotated hex dumps for read/write transactions
- **Error handling** — exception code table, retry-with-backoff in both C and Rust
- **RS-485** — DE/RE pin control for bare-metal (STM32 HAL), Linux `TIOCSRS485` ioctl, and termination resistor guidance
- **Summary** — concise recap of all key concepts

> **Implementing Modbus serial protocol with UART physical layer**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Modbus RTU Protocol Overview](#modbus-rtu-protocol-overview)
3. [Frame Structure](#frame-structure)
4. [UART Physical Layer Configuration](#uart-physical-layer-configuration)
5. [Timing and Inter-Frame Gaps](#timing-and-inter-frame-gaps)
6. [Function Codes](#function-codes)
7. [CRC-16 Calculation](#crc-16-calculation)
8. [C/C++ Implementation](#cc-implementation)
9. [Rust Implementation](#rust-implementation)
10. [Master/Slave Communication Examples](#masterslave-communication-examples)
11. [Error Handling and Exception Responses](#error-handling-and-exception-responses)
12. [RS-485 Considerations](#rs-485-considerations)
13. [Summary](#summary)

---

## Introduction

**Modbus RTU** (Remote Terminal Unit) is the most widely deployed industrial communication protocol, operating over a serial bus. Originally developed by Modicon in 1979, it remains the *de facto* standard for connecting electronic devices in industrial automation, building management, and embedded systems.

Modbus RTU uses the **UART** (Universal Asynchronous Receiver/Transmitter) as its physical communication mechanism, typically combined with **RS-485** differential signaling for multi-drop networks. The "RTU" mode transmits data as binary bytes, making it compact and efficient compared to its ASCII counterpart.

### Key Characteristics

- **Topology**: Master/Slave (Client/Server in Modbus 2019 spec)
- **Max devices**: 247 slaves per segment (addresses 1–247; address 0 is broadcast)
- **Physical layer**: RS-232 (point-to-point), RS-485 (multi-drop, up to 32 devices without repeaters)
- **Baud rates**: Typically 9600, 19200, 38400, 57600, 115200 bps
- **Frame delimiter**: Silence gaps (3.5 character times)
- **Error detection**: CRC-16 (polynomial 0x8005, reflected)

---

## Modbus RTU Protocol Overview

Modbus RTU follows a strict **request/response** paradigm:

```
Master                          Slave (Address N)
  |                                    |
  |--- [Request Frame] --------------->|
  |                                    | (process request)
  |<-- [Response Frame] ---------------|
  |                                    |
```

- Only the **master** initiates communication.
- Each slave has a unique address (1–247).
- A slave only responds to frames addressed to it (or executes silently for broadcast address 0).
- The master must wait for a response or timeout before sending another request.

---

## Frame Structure

A Modbus RTU frame has the following structure:

```
┌──────────────┬───────────────┬──────────────────────────┬────────────┐
│ Address (1B) │ Function (1B) │     Data (0–252 bytes)   │  CRC (2B)  │
└──────────────┴───────────────┴──────────────────────────┴────────────┘
```

| Field         | Size     | Description                                      |
|---------------|----------|--------------------------------------------------|
| Address       | 1 byte   | Slave device address (1–247; 0 = broadcast)      |
| Function Code | 1 byte   | Indicates the action to perform                  |
| Data          | 0–252 B  | Parameters (register address, count, values, ...)|
| CRC           | 2 bytes  | CRC-16/IBM, little-endian (low byte first)       |

The **maximum frame size** is 256 bytes (1 + 1 + 252 + 2).

### Frame Boundaries

Unlike many protocols, Modbus RTU has **no explicit start/end delimiters**. Instead, frames are separated by **silence intervals** on the bus:

- A silence of **≥ 3.5 character times** signals the end of a frame.
- A silence of **≥ 1.5 character times** within a frame indicates a framing error.

---

## UART Physical Layer Configuration

Modbus RTU requires specific and consistent UART settings across all devices on the network:

| Parameter    | Value                    |
|--------------|--------------------------|
| Data bits    | 8                        |
| Parity       | None (most common), Even, or Odd |
| Stop bits    | 1 (with parity) or 2 (without parity) |
| Flow control | None                     |
| Baud rate    | Configurable (9600 default) |

The most common configuration is **8N1** (8 data bits, no parity, 1 stop bit) at **9600 baud**.

### Character Time Calculation

One character = 1 start bit + 8 data bits + parity (0 or 1) + stop bits.

For 8N1 at 9600 baud:
- Character time = 10 bits / 9600 bps ≈ **1.042 ms**
- 1.5 char time ≈ 1.563 ms (inter-character timeout)
- 3.5 char time ≈ 3.646 ms (inter-frame silence gap)

For baud rates ≥ 19200 bps, the Modbus specification allows fixed values:
- **1.5 char timeout** = 750 µs
- **3.5 char timeout** = 1750 µs

---

## Timing and Inter-Frame Gaps

Correct timing is critical in Modbus RTU. There are two key timers:

### T1.5 Timer — Inter-Character Timeout
If silence exceeds 1.5 character times *within* a frame, the frame is considered corrupted and should be discarded.

### T3.5 Timer — Inter-Frame Gap
A silence of at least 3.5 character times marks the end of a complete frame. A new frame may begin after this gap.

```
    [Frame N]     <-- 3.5T gap -->    [Frame N+1]
│▓▓▓▓▓▓▓▓▓▓│~~~~~~~~~~~~~~~~~~~~│▓▓▓▓▓▓▓▓▓▓│
             ↑ T3.5 silence gap ↑
```

Failing to observe these timers leads to frame merging, truncation, or CRC errors.

---

## Function Codes

The most common Modbus function codes:

| Code | Hex  | Name                        | Description                          |
|------|------|-----------------------------|--------------------------------------|
| 1    | 0x01 | Read Coils                  | Read 1–2000 coils (digital outputs)  |
| 2    | 0x02 | Read Discrete Inputs        | Read 1–2000 discrete inputs          |
| 3    | 0x03 | Read Holding Registers      | Read 1–125 16-bit holding registers  |
| 4    | 0x04 | Read Input Registers        | Read 1–125 16-bit input registers    |
| 5    | 0x05 | Write Single Coil           | Write one coil ON/OFF                |
| 6    | 0x06 | Write Single Register       | Write one 16-bit register            |
| 15   | 0x0F | Write Multiple Coils        | Write 1–1968 coils                   |
| 16   | 0x10 | Write Multiple Registers    | Write 1–123 registers                |

### Error/Exception Response
If a slave cannot process a request, it responds with the function code **ORed with 0x80**, followed by an exception code byte and CRC.

---

## CRC-16 Calculation

Modbus RTU uses **CRC-16/IBM** (also called CRC-16-Modbus):
- Polynomial: 0x8005 (reflected: 0xA001)
- Initial value: 0xFFFF
- Input/Output reflected: Yes
- No final XOR

The CRC is appended **low byte first**, then high byte.

---

## C/C++ Implementation

### 1. Platform Setup (Linux / POSIX UART)

```c
// modbus_uart.h
#ifndef MODBUS_UART_H
#define MODBUS_UART_H

#include <stdint.h>
#include <stddef.h>

#define MODBUS_MAX_PDU_SIZE     253
#define MODBUS_MAX_FRAME_SIZE   256
#define MODBUS_BROADCAST_ADDR   0
#define MODBUS_MIN_SLAVE_ADDR   1
#define MODBUS_MAX_SLAVE_ADDR   247

// Function codes
#define FC_READ_COILS               0x01
#define FC_READ_DISCRETE_INPUTS     0x02
#define FC_READ_HOLDING_REGS        0x03
#define FC_READ_INPUT_REGS          0x04
#define FC_WRITE_SINGLE_COIL        0x05
#define FC_WRITE_SINGLE_REG         0x06
#define FC_WRITE_MULTIPLE_COILS     0x0F
#define FC_WRITE_MULTIPLE_REGS      0x10

// Exception codes
#define EX_ILLEGAL_FUNCTION         0x01
#define EX_ILLEGAL_DATA_ADDRESS     0x02
#define EX_ILLEGAL_DATA_VALUE       0x03
#define EX_SERVER_DEVICE_FAILURE    0x04

typedef struct {
    int fd;           // File descriptor for UART
    uint8_t slave_id; // Our slave address (0 = master-only mode)
    uint32_t baud;
    int response_timeout_ms;
} modbus_ctx_t;

// Function prototypes
int  modbus_init(modbus_ctx_t *ctx, const char *port, uint32_t baud, uint8_t slave_id);
void modbus_close(modbus_ctx_t *ctx);
uint16_t modbus_crc16(const uint8_t *data, size_t len);

int modbus_read_holding_registers(modbus_ctx_t *ctx, uint8_t slave,
                                  uint16_t start_addr, uint16_t count,
                                  uint16_t *out);
int modbus_write_single_register(modbus_ctx_t *ctx, uint8_t slave,
                                 uint16_t reg_addr, uint16_t value);
int modbus_write_multiple_registers(modbus_ctx_t *ctx, uint8_t slave,
                                    uint16_t start_addr, uint16_t count,
                                    const uint16_t *values);

#endif // MODBUS_UART_H
```

### 2. CRC-16 Implementation

```c
// crc16.c
#include "modbus_uart.h"

/**
 * Compute CRC-16/Modbus over a byte buffer.
 * Polynomial: 0xA001 (reflected 0x8005)
 * Init: 0xFFFF
 */
uint16_t modbus_crc16(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

/**
 * Append CRC to frame buffer (low byte first, then high byte).
 * Returns total frame length including CRC.
 */
size_t modbus_append_crc(uint8_t *frame, size_t data_len) {
    uint16_t crc = modbus_crc16(frame, data_len);
    frame[data_len]     = (uint8_t)(crc & 0xFF);        // Low byte
    frame[data_len + 1] = (uint8_t)((crc >> 8) & 0xFF); // High byte
    return data_len + 2;
}

/**
 * Verify CRC of a received frame (last 2 bytes are CRC).
 * Returns 1 if valid, 0 if mismatch.
 */
int modbus_verify_crc(const uint8_t *frame, size_t total_len) {
    if (total_len < 4) return 0; // Minimum: addr + fc + 2 CRC bytes
    uint16_t computed = modbus_crc16(frame, total_len - 2);
    uint16_t received = (uint16_t)frame[total_len - 2]
                      | ((uint16_t)frame[total_len - 1] << 8);
    return (computed == received) ? 1 : 0;
}
```

### 3. UART Initialization (POSIX/Linux)

```c
// uart_posix.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/time.h>
#include "modbus_uart.h"

static speed_t baud_to_speed(uint32_t baud) {
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        default:     return B9600;
    }
}

/**
 * Open and configure UART port for Modbus RTU.
 * Settings: 8N1, no flow control, raw mode.
 */
int modbus_init(modbus_ctx_t *ctx, const char *port,
                uint32_t baud, uint8_t slave_id) {
    ctx->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (ctx->fd < 0) {
        perror("modbus_init: open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(ctx->fd, &tty) != 0) {
        perror("modbus_init: tcgetattr");
        close(ctx->fd);
        return -1;
    }

    speed_t speed = baud_to_speed(baud);
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // 8N1 raw mode
    tty.c_cflag &= ~PARENB;    // No parity
    tty.c_cflag &= ~CSTOPB;    // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         // 8 data bits
    tty.c_cflag &= ~CRTSCTS;    // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // No software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_oflag &= ~OPOST;     // Raw output
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);

    // Non-blocking read
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(ctx->fd, TCSANOW, &tty) != 0) {
        perror("modbus_init: tcsetattr");
        close(ctx->fd);
        return -1;
    }

    tcflush(ctx->fd, TCIOFLUSH);

    ctx->baud              = baud;
    ctx->slave_id          = slave_id;
    ctx->response_timeout_ms = 1000; // Default 1 second
    return 0;
}

void modbus_close(modbus_ctx_t *ctx) {
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

/**
 * Send raw frame over UART.
 */
static int uart_send(modbus_ctx_t *ctx, const uint8_t *buf, size_t len) {
    ssize_t written = write(ctx->fd, buf, len);
    if (written < 0 || (size_t)written != len) {
        perror("uart_send: write");
        return -1;
    }
    tcdrain(ctx->fd); // Wait for all data to be transmitted
    return 0;
}

/**
 * Receive response with timeout.
 * Returns number of bytes received, or -1 on error/timeout.
 */
static int uart_recv(modbus_ctx_t *ctx, uint8_t *buf, size_t max_len,
                     int timeout_ms) {
    fd_set rfds;
    struct timeval tv;
    size_t total = 0;

    // Compute inter-character timeout (1.5 char times, min 1 ms)
    uint32_t char_time_us = (10 * 1000000UL) / ctx->baud;
    uint32_t t15_us = (char_time_us * 3) / 2;
    if (t15_us < 1000) t15_us = 1000;

    // Wait for first byte up to response_timeout_ms
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    FD_ZERO(&rfds);
    FD_SET(ctx->fd, &rfds);

    int ret = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return (ret == 0) ? 0 : -1;

    // Read bytes, stopping when no new data arrives within T1.5
    while (total < max_len) {
        tv.tv_sec  = 0;
        tv.tv_usec = t15_us;

        FD_ZERO(&rfds);
        FD_SET(ctx->fd, &rfds);
        ret = select(ctx->fd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) break; // Timeout = end of frame

        ssize_t n = read(ctx->fd, buf + total, max_len - total);
        if (n <= 0) break;
        total += n;
    }
    return (int)total;
}
```

### 4. Read Holding Registers (FC 0x03)

```c
// modbus_master.c
#include <stdio.h>
#include <string.h>
#include "modbus_uart.h"

/**
 * FC 0x03 — Read Holding Registers
 * Reads `count` 16-bit registers starting at `start_addr` from `slave`.
 * Results stored in `out` (count elements).
 * Returns 0 on success, negative on error.
 */
int modbus_read_holding_registers(modbus_ctx_t *ctx, uint8_t slave,
                                  uint16_t start_addr, uint16_t count,
                                  uint16_t *out) {
    if (count < 1 || count > 125) return -1;

    // Build request frame
    uint8_t req[8];
    req[0] = slave;
    req[1] = FC_READ_HOLDING_REGS;
    req[2] = (start_addr >> 8) & 0xFF;
    req[3] =  start_addr       & 0xFF;
    req[4] = (count >> 8) & 0xFF;
    req[5] =  count       & 0xFF;

    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;         // CRC low byte
    req[7] = (crc >> 8) & 0xFF;  // CRC high byte

    // Flush receive buffer before sending
    tcflush(ctx->fd, TCIFLUSH);

    if (uart_send(ctx, req, sizeof(req)) < 0) return -2;

    // Expected response: addr(1) + fc(1) + byte_count(1) + data(2*count) + CRC(2)
    size_t expected = 5 + 2 * count;
    uint8_t resp[MODBUS_MAX_FRAME_SIZE];

    int n = uart_recv(ctx, resp, sizeof(resp), ctx->response_timeout_ms);
    if (n <= 0) {
        fprintf(stderr, "modbus_read_holding_registers: timeout/no response\n");
        return -3;
    }
    if ((size_t)n < expected) {
        fprintf(stderr, "modbus_read_holding_registers: short response (%d < %zu)\n",
                n, expected);
        return -4;
    }

    // Check for exception response (FC | 0x80)
    if (resp[1] == (FC_READ_HOLDING_REGS | 0x80)) {
        fprintf(stderr, "modbus_read_holding_registers: exception code 0x%02X\n",
                resp[2]);
        return -(int)resp[2];
    }

    // Validate CRC
    if (!modbus_verify_crc(resp, n)) {
        fprintf(stderr, "modbus_read_holding_registers: CRC error\n");
        return -5;
    }

    // Validate address and function code
    if (resp[0] != slave || resp[1] != FC_READ_HOLDING_REGS) return -6;

    uint8_t byte_count = resp[2];
    if (byte_count != count * 2) return -7;

    // Extract register values (big-endian pairs)
    for (uint16_t i = 0; i < count; i++) {
        out[i] = ((uint16_t)resp[3 + 2*i] << 8) | resp[4 + 2*i];
    }
    return 0;
}

/**
 * FC 0x06 — Write Single Register
 */
int modbus_write_single_register(modbus_ctx_t *ctx, uint8_t slave,
                                 uint16_t reg_addr, uint16_t value) {
    uint8_t req[8];
    req[0] = slave;
    req[1] = FC_WRITE_SINGLE_REG;
    req[2] = (reg_addr >> 8) & 0xFF;
    req[3] =  reg_addr       & 0xFF;
    req[4] = (value >> 8) & 0xFF;
    req[5] =  value       & 0xFF;

    uint16_t crc = modbus_crc16(req, 6);
    req[6] = crc & 0xFF;
    req[7] = (crc >> 8) & 0xFF;

    tcflush(ctx->fd, TCIFLUSH);
    if (uart_send(ctx, req, sizeof(req)) < 0) return -1;

    // Echo response: same 8 bytes
    uint8_t resp[8];
    int n = uart_recv(ctx, resp, sizeof(resp), ctx->response_timeout_ms);
    if (n != 8) return -2;
    if (!modbus_verify_crc(resp, 8)) return -3;
    if (memcmp(req, resp, 6) != 0) return -4; // Echo mismatch
    return 0;
}

/**
 * FC 0x10 — Write Multiple Registers
 */
int modbus_write_multiple_registers(modbus_ctx_t *ctx, uint8_t slave,
                                    uint16_t start_addr, uint16_t count,
                                    const uint16_t *values) {
    if (count < 1 || count > 123) return -1;

    uint8_t req[MODBUS_MAX_FRAME_SIZE];
    size_t idx = 0;

    req[idx++] = slave;
    req[idx++] = FC_WRITE_MULTIPLE_REGS;
    req[idx++] = (start_addr >> 8) & 0xFF;
    req[idx++] =  start_addr       & 0xFF;
    req[idx++] = (count >> 8) & 0xFF;
    req[idx++] =  count       & 0xFF;
    req[idx++] = (uint8_t)(count * 2); // Byte count

    for (uint16_t i = 0; i < count; i++) {
        req[idx++] = (values[i] >> 8) & 0xFF;
        req[idx++] =  values[i]       & 0xFF;
    }

    uint16_t crc = modbus_crc16(req, idx);
    req[idx++] = crc & 0xFF;
    req[idx++] = (crc >> 8) & 0xFF;

    tcflush(ctx->fd, TCIFLUSH);
    if (uart_send(ctx, req, idx) < 0) return -2;

    // Response: addr(1) + fc(1) + start_addr(2) + count(2) + CRC(2) = 8 bytes
    uint8_t resp[8];
    int n = uart_recv(ctx, resp, sizeof(resp), ctx->response_timeout_ms);
    if (n != 8) return -3;
    if (!modbus_verify_crc(resp, 8)) return -4;
    if (resp[0] != slave || resp[1] != FC_WRITE_MULTIPLE_REGS) return -5;
    return 0;
}
```

### 5. Simple Slave Implementation

```c
// modbus_slave.c — bare-metal / RTOS friendly

#include <string.h>
#include "modbus_uart.h"

// Slave register bank
#define SLAVE_HOLD_REG_COUNT 64
static uint16_t holding_regs[SLAVE_HOLD_REG_COUNT] = {0};

/**
 * Process an incoming Modbus RTU request frame.
 * `req`     — received bytes
 * `req_len` — number of bytes
 * `resp`    — output buffer for response frame
 * `slave_addr` — this device's address
 * Returns number of response bytes to send, 0 if no response needed (broadcast).
 */
int modbus_slave_process(uint8_t slave_addr,
                         const uint8_t *req, size_t req_len,
                         uint8_t *resp) {
    // Minimum frame: addr + fc + CRC = 4 bytes
    if (req_len < 4) return 0;

    // Address check
    if (req[0] != slave_addr && req[0] != MODBUS_BROADCAST_ADDR) return 0;
    int is_broadcast = (req[0] == MODBUS_BROADCAST_ADDR);

    // CRC check
    if (!modbus_verify_crc(req, req_len)) return 0;

    uint8_t fc  = req[1];
    size_t rlen = 0;

    // Helper: build exception response
    #define EXCEPTION(code) do { \
        resp[0] = slave_addr; \
        resp[1] = fc | 0x80; \
        resp[2] = (code); \
        uint16_t c = modbus_crc16(resp, 3); \
        resp[3] = c & 0xFF; resp[4] = (c >> 8) & 0xFF; \
        return is_broadcast ? 0 : 5; \
    } while(0)

    switch (fc) {
        case FC_READ_HOLDING_REGS: {
            if (req_len < 8) EXCEPTION(EX_ILLEGAL_DATA_VALUE);
            uint16_t start = ((uint16_t)req[2] << 8) | req[3];
            uint16_t count = ((uint16_t)req[4] << 8) | req[5];

            if (count < 1 || count > 125)
                EXCEPTION(EX_ILLEGAL_DATA_VALUE);
            if (start + count > SLAVE_HOLD_REG_COUNT)
                EXCEPTION(EX_ILLEGAL_DATA_ADDRESS);

            resp[0] = slave_addr;
            resp[1] = FC_READ_HOLDING_REGS;
            resp[2] = (uint8_t)(count * 2);
            for (uint16_t i = 0; i < count; i++) {
                resp[3 + 2*i] = (holding_regs[start + i] >> 8) & 0xFF;
                resp[4 + 2*i] =  holding_regs[start + i]       & 0xFF;
            }
            rlen = 3 + count * 2;
            break;
        }

        case FC_WRITE_SINGLE_REG: {
            if (req_len < 8) EXCEPTION(EX_ILLEGAL_DATA_VALUE);
            uint16_t addr  = ((uint16_t)req[2] << 8) | req[3];
            uint16_t value = ((uint16_t)req[4] << 8) | req[5];

            if (addr >= SLAVE_HOLD_REG_COUNT)
                EXCEPTION(EX_ILLEGAL_DATA_ADDRESS);

            holding_regs[addr] = value;
            // Echo request back (minus CRC, re-append)
            memcpy(resp, req, req_len - 2);
            rlen = req_len - 2;
            break;
        }

        case FC_WRITE_MULTIPLE_REGS: {
            if (req_len < 9) EXCEPTION(EX_ILLEGAL_DATA_VALUE);
            uint16_t start  = ((uint16_t)req[2] << 8) | req[3];
            uint16_t count  = ((uint16_t)req[4] << 8) | req[5];
            uint8_t  nbytes = req[6];

            if (count < 1 || count > 123 || nbytes != count * 2)
                EXCEPTION(EX_ILLEGAL_DATA_VALUE);
            if (start + count > SLAVE_HOLD_REG_COUNT)
                EXCEPTION(EX_ILLEGAL_DATA_ADDRESS);

            for (uint16_t i = 0; i < count; i++) {
                holding_regs[start + i] =
                    ((uint16_t)req[7 + 2*i] << 8) | req[8 + 2*i];
            }
            resp[0] = slave_addr;
            resp[1] = FC_WRITE_MULTIPLE_REGS;
            resp[2] = (start >> 8) & 0xFF;
            resp[3] =  start       & 0xFF;
            resp[4] = (count >> 8) & 0xFF;
            resp[5] =  count       & 0xFF;
            rlen = 6;
            break;
        }

        default:
            EXCEPTION(EX_ILLEGAL_FUNCTION);
    }

    // Append CRC
    uint16_t crc = modbus_crc16(resp, rlen);
    resp[rlen++] = crc & 0xFF;
    resp[rlen++] = (crc >> 8) & 0xFF;

    return is_broadcast ? 0 : (int)rlen;
}
```

### 6. Main Usage Example (C)

```c
// main.c
#include <stdio.h>
#include <stdlib.h>
#include "modbus_uart.h"

int main(void) {
    modbus_ctx_t ctx;

    if (modbus_init(&ctx, "/dev/ttyUSB0", 9600, 0) < 0) {
        fprintf(stderr, "Failed to open UART\n");
        return EXIT_FAILURE;
    }
    ctx.response_timeout_ms = 500;

    // Read 4 holding registers from slave 1, starting at address 100
    uint16_t regs[4];
    int ret = modbus_read_holding_registers(&ctx, 1, 100, 4, regs);
    if (ret == 0) {
        printf("Registers 100–103: %u, %u, %u, %u\n",
               regs[0], regs[1], regs[2], regs[3]);
    } else {
        printf("Read error: %d\n", ret);
    }

    // Write value 1234 to register 200 on slave 1
    ret = modbus_write_single_register(&ctx, 1, 200, 1234);
    printf("Write single: %s\n", ret == 0 ? "OK" : "Error");

    // Write 3 registers starting at 300
    uint16_t vals[3] = {100, 200, 300};
    ret = modbus_write_multiple_registers(&ctx, 1, 300, 3, vals);
    printf("Write multiple: %s\n", ret == 0 ? "OK" : "Error");

    modbus_close(&ctx);
    return EXIT_SUCCESS;
}
```

---

## Rust Implementation

### 1. Project Setup

```toml
# Cargo.toml
[package]
name = "modbus-rtu"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4.3"
thiserror  = "1.0"
```

### 2. Types and Error Handling

```rust
// src/lib.rs
use thiserror::Error;

pub const MODBUS_MAX_FRAME_SIZE: usize = 256;
pub const MODBUS_MAX_PDU_SIZE:   usize = 253;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum FunctionCode {
    ReadCoils              = 0x01,
    ReadDiscreteInputs     = 0x02,
    ReadHoldingRegisters   = 0x03,
    ReadInputRegisters     = 0x04,
    WriteSingleCoil        = 0x05,
    WriteSingleRegister    = 0x06,
    WriteMultipleCoils     = 0x0F,
    WriteMultipleRegisters = 0x10,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum ExceptionCode {
    IllegalFunction       = 0x01,
    IllegalDataAddress    = 0x02,
    IllegalDataValue      = 0x03,
    ServerDeviceFailure   = 0x04,
}

#[derive(Debug, Error)]
pub enum ModbusError {
    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("CRC mismatch (expected {expected:#06X}, got {got:#06X})")]
    CrcMismatch { expected: u16, got: u16 },

    #[error("Response timeout")]
    Timeout,

    #[error("Short response: expected {expected} bytes, got {got}")]
    ShortResponse { expected: usize, got: usize },

    #[error("Modbus exception: function {function:#04X}, code {code:#04X}")]
    Exception { function: u8, code: u8 },

    #[error("Invalid argument: {0}")]
    InvalidArgument(String),

    #[error("Unexpected response")]
    UnexpectedResponse,
}

pub type ModbusResult<T> = Result<T, ModbusError>;
```

### 3. CRC-16 in Rust

```rust
// src/crc.rs

/// Compute CRC-16/Modbus over a byte slice.
/// Polynomial: 0xA001 (reflected 0x8005), init 0xFFFF.
pub fn crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            if crc & 0x0001 != 0 {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    crc
}

/// Append CRC to a frame buffer (low byte, then high byte).
pub fn append_crc(frame: &mut Vec<u8>) {
    let crc = crc16(frame);
    frame.push((crc & 0xFF) as u8);
    frame.push(((crc >> 8) & 0xFF) as u8);
}

/// Verify CRC of a complete frame (last 2 bytes are the CRC).
pub fn verify_crc(frame: &[u8]) -> bool {
    if frame.len() < 4 { return false; }
    let computed = crc16(&frame[..frame.len() - 2]);
    let received = (frame[frame.len() - 2] as u16)
                 | ((frame[frame.len() - 1] as u16) << 8);
    computed == received
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc_known_frame() {
        // FC03 request: addr=1, fc=0x03, start=0x006B, count=0x0003
        let frame = [0x01u8, 0x03, 0x00, 0x6B, 0x00, 0x03];
        let crc = crc16(&frame);
        // Known correct CRC for this frame is 0x7687
        assert_eq!(crc, 0x7687, "CRC mismatch: got {:#06X}", crc);
    }

    #[test]
    fn test_append_and_verify() {
        let mut frame = vec![0x01u8, 0x06, 0x00, 0x01, 0x00, 0x03];
        append_crc(&mut frame);
        assert!(verify_crc(&frame));
    }
}
```

### 4. Modbus Master in Rust

```rust
// src/master.rs
use std::io::{Read, Write};
use std::time::Duration;
use serialport::SerialPort;
use crate::{crc, FunctionCode, ModbusError, ModbusResult, MODBUS_MAX_FRAME_SIZE};

pub struct ModbusMaster {
    port: Box<dyn SerialPort>,
    response_timeout: Duration,
    /// Approximate T1.5 inter-character gap
    t15_us: u64,
}

impl ModbusMaster {
    /// Open a serial port and configure it for Modbus RTU.
    pub fn new(
        port_name: &str,
        baud_rate: u32,
        response_timeout: Duration,
    ) -> ModbusResult<Self> {
        let port = serialport::new(port_name, baud_rate)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .timeout(Duration::from_millis(50))
            .open()?;

        // T1.5 = 1.5 * (10 bits / baud) seconds → microseconds
        let char_us = (10_000_000u64 + baud_rate as u64 - 1) / baud_rate as u64;
        let t15_us = (char_us * 3) / 2;
        let t15_us = t15_us.max(1000); // Min 1 ms

        Ok(Self { port, response_timeout, t15_us })
    }

    /// Send raw bytes and receive a response.
    fn transaction(&mut self, request: &[u8], expected_min: usize)
        -> ModbusResult<Vec<u8>>
    {
        // Drain receive buffer
        let mut discard = [0u8; 64];
        while self.port.read(&mut discard).unwrap_or(0) > 0 {}

        self.port.write_all(request)?;

        // Wait for initial byte with response_timeout
        self.port.set_timeout(self.response_timeout)?;

        let mut response = Vec::with_capacity(expected_min + 4);
        let mut buf = [0u8; 64];

        // Read first chunk
        match self.port.read(&mut buf) {
            Ok(0) | Err(_) => return Err(ModbusError::Timeout),
            Ok(n) => response.extend_from_slice(&buf[..n]),
        }

        // Switch to T1.5 timeout for subsequent bytes
        self.port.set_timeout(Duration::from_micros(self.t15_us))?;

        loop {
            match self.port.read(&mut buf) {
                Ok(0) => break,
                Ok(n) => {
                    response.extend_from_slice(&buf[..n]);
                    if response.len() >= MODBUS_MAX_FRAME_SIZE { break; }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => break,
                Err(e) => return Err(ModbusError::Io(e)),
            }
        }

        if response.len() < expected_min {
            return Err(ModbusError::ShortResponse {
                expected: expected_min,
                got: response.len(),
            });
        }
        Ok(response)
    }

    /// FC 0x03 — Read Holding Registers
    pub fn read_holding_registers(
        &mut self,
        slave: u8,
        start_addr: u16,
        count: u16,
    ) -> ModbusResult<Vec<u16>> {
        if count == 0 || count > 125 {
            return Err(ModbusError::InvalidArgument(
                format!("count must be 1–125, got {count}")
            ));
        }

        let mut req = vec![
            slave,
            FunctionCode::ReadHoldingRegisters as u8,
            (start_addr >> 8) as u8,
            (start_addr & 0xFF) as u8,
            (count >> 8) as u8,
            (count & 0xFF) as u8,
        ];
        crc::append_crc(&mut req);

        let expected = 5 + 2 * count as usize;
        let resp = self.transaction(&req, expected)?;

        // Check for exception
        if resp[1] == (FunctionCode::ReadHoldingRegisters as u8 | 0x80) {
            return Err(ModbusError::Exception {
                function: resp[1],
                code: resp[2],
            });
        }

        // Validate CRC
        if !crc::verify_crc(&resp) {
            let expected_crc = crc::crc16(&resp[..resp.len()-2]);
            let got_crc = (resp[resp.len()-2] as u16)
                        | ((resp[resp.len()-1] as u16) << 8);
            return Err(ModbusError::CrcMismatch {
                expected: expected_crc,
                got: got_crc,
            });
        }

        if resp[0] != slave || resp[1] != FunctionCode::ReadHoldingRegisters as u8 {
            return Err(ModbusError::UnexpectedResponse);
        }

        let byte_count = resp[2] as usize;
        if byte_count != count as usize * 2 {
            return Err(ModbusError::UnexpectedResponse);
        }

        let values: Vec<u16> = (0..count as usize)
            .map(|i| ((resp[3 + 2*i] as u16) << 8) | resp[4 + 2*i] as u16)
            .collect();

        Ok(values)
    }

    /// FC 0x06 — Write Single Register
    pub fn write_single_register(
        &mut self,
        slave: u8,
        reg_addr: u16,
        value: u16,
    ) -> ModbusResult<()> {
        let mut req = vec![
            slave,
            FunctionCode::WriteSingleRegister as u8,
            (reg_addr >> 8) as u8,
            (reg_addr & 0xFF) as u8,
            (value >> 8) as u8,
            (value & 0xFF) as u8,
        ];
        crc::append_crc(&mut req);

        let resp = self.transaction(&req, 8)?;

        if !crc::verify_crc(&resp) {
            return Err(ModbusError::CrcMismatch {
                expected: crc::crc16(&resp[..resp.len()-2]),
                got: (resp[resp.len()-2] as u16) | ((resp[resp.len()-1] as u16) << 8),
            });
        }
        // Echo must match first 6 bytes of request
        if resp[..6] != req[..6] {
            return Err(ModbusError::UnexpectedResponse);
        }
        Ok(())
    }

    /// FC 0x10 — Write Multiple Registers
    pub fn write_multiple_registers(
        &mut self,
        slave: u8,
        start_addr: u16,
        values: &[u16],
    ) -> ModbusResult<()> {
        let count = values.len();
        if count == 0 || count > 123 {
            return Err(ModbusError::InvalidArgument(
                format!("count must be 1–123, got {count}")
            ));
        }

        let mut req = vec![
            slave,
            FunctionCode::WriteMultipleRegisters as u8,
            (start_addr >> 8) as u8,
            (start_addr & 0xFF) as u8,
            (count >> 8) as u8,
            (count & 0xFF) as u8,
            (count * 2) as u8, // Byte count
        ];
        for &v in values {
            req.push((v >> 8) as u8);
            req.push((v & 0xFF) as u8);
        }
        crc::append_crc(&mut req);

        let resp = self.transaction(&req, 8)?;

        if !crc::verify_crc(&resp) {
            return Err(ModbusError::CrcMismatch {
                expected: crc::crc16(&resp[..resp.len()-2]),
                got: (resp[resp.len()-2] as u16) | ((resp[resp.len()-1] as u16) << 8),
            });
        }
        if resp[0] != slave || resp[1] != FunctionCode::WriteMultipleRegisters as u8 {
            return Err(ModbusError::UnexpectedResponse);
        }
        Ok(())
    }
}
```

### 5. Modbus Slave in Rust

```rust
// src/slave.rs
use crate::{crc, ExceptionCode, FunctionCode, ModbusError, ModbusResult};

const HOLD_REG_COUNT: usize = 64;

pub struct ModbusSlave {
    pub address: u8,
    pub holding_registers: [u16; HOLD_REG_COUNT],
}

impl ModbusSlave {
    pub fn new(address: u8) -> Self {
        assert!(address >= 1 && address <= 247, "Slave address must be 1–247");
        Self {
            address,
            holding_registers: [0u16; HOLD_REG_COUNT],
        }
    }

    /// Process a received Modbus RTU frame.
    /// Returns `Some(response_bytes)` if a response should be sent,
    /// or `None` for broadcast or ignored frames.
    pub fn process_frame(&mut self, frame: &[u8]) -> Option<Vec<u8>> {
        if frame.len() < 4 { return None; }

        let addr = frame[0];
        if addr != self.address && addr != 0 { return None; }

        let is_broadcast = addr == 0;

        if !crc::verify_crc(frame) { return None; }

        let fc = frame[1];
        let result = self.dispatch(fc, frame);

        if is_broadcast { return None; }

        let mut resp = match result {
            Ok(data) => data,
            Err(exc) => self.build_exception(fc, exc),
        };
        crc::append_crc(&mut resp);
        Some(resp)
    }

    fn dispatch(&mut self, fc: u8, frame: &[u8]) -> Result<Vec<u8>, u8> {
        match fc {
            0x03 => self.handle_read_holding(frame),
            0x06 => self.handle_write_single(frame),
            0x10 => self.handle_write_multiple(frame),
            _    => Err(ExceptionCode::IllegalFunction as u8),
        }
    }

    fn handle_read_holding(&self, frame: &[u8]) -> Result<Vec<u8>, u8> {
        if frame.len() < 8 {
            return Err(ExceptionCode::IllegalDataValue as u8);
        }
        let start = u16::from_be_bytes([frame[2], frame[3]]) as usize;
        let count = u16::from_be_bytes([frame[4], frame[5]]) as usize;

        if count == 0 || count > 125 {
            return Err(ExceptionCode::IllegalDataValue as u8);
        }
        if start + count > HOLD_REG_COUNT {
            return Err(ExceptionCode::IllegalDataAddress as u8);
        }

        let mut resp = vec![self.address, 0x03, (count * 2) as u8];
        for i in 0..count {
            let v = self.holding_registers[start + i];
            resp.push((v >> 8) as u8);
            resp.push((v & 0xFF) as u8);
        }
        Ok(resp)
    }

    fn handle_write_single(&mut self, frame: &[u8]) -> Result<Vec<u8>, u8> {
        if frame.len() < 8 {
            return Err(ExceptionCode::IllegalDataValue as u8);
        }
        let addr  = u16::from_be_bytes([frame[2], frame[3]]) as usize;
        let value = u16::from_be_bytes([frame[4], frame[5]]);

        if addr >= HOLD_REG_COUNT {
            return Err(ExceptionCode::IllegalDataAddress as u8);
        }
        self.holding_registers[addr] = value;

        // Echo: return same addr + fc + data (no CRC yet — appended later)
        Ok(vec![self.address, 0x06,
                frame[2], frame[3],
                frame[4], frame[5]])
    }

    fn handle_write_multiple(&mut self, frame: &[u8]) -> Result<Vec<u8>, u8> {
        if frame.len() < 9 {
            return Err(ExceptionCode::IllegalDataValue as u8);
        }
        let start   = u16::from_be_bytes([frame[2], frame[3]]) as usize;
        let count   = u16::from_be_bytes([frame[4], frame[5]]) as usize;
        let nbytes  = frame[6] as usize;

        if count == 0 || count > 123 || nbytes != count * 2 {
            return Err(ExceptionCode::IllegalDataValue as u8);
        }
        if start + count > HOLD_REG_COUNT {
            return Err(ExceptionCode::IllegalDataAddress as u8);
        }
        for i in 0..count {
            let v = u16::from_be_bytes([frame[7 + 2*i], frame[8 + 2*i]]);
            self.holding_registers[start + i] = v;
        }
        Ok(vec![self.address, 0x10,
                frame[2], frame[3],
                frame[4], frame[5]])
    }

    fn build_exception(&self, fc: u8, exc_code: u8) -> Vec<u8> {
        vec![self.address, fc | 0x80, exc_code]
    }
}
```

### 6. Main Usage Example (Rust)

```rust
// src/main.rs
use std::time::Duration;
use modbus_rtu::master::ModbusMaster;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut master = ModbusMaster::new("/dev/ttyUSB0", 9600,
                                       Duration::from_millis(500))?;

    // Read 4 holding registers from slave 1 at address 100
    let regs = master.read_holding_registers(1, 100, 4)?;
    println!("Registers 100–103: {:?}", regs);

    // Write single register
    master.write_single_register(1, 200, 1234)?;
    println!("Write single register OK");

    // Write multiple registers
    master.write_multiple_registers(1, 300, &[100, 200, 300])?;
    println!("Write multiple registers OK");

    Ok(())
}
```

---

## Master/Slave Communication Examples

### Read Holding Registers (FC 0x03) — Full Frame Walkthrough

**Request** (Master → Slave 1, read 3 registers starting at 0x006B):

```
01  03  00  6B  00  03  76  87
│   │   ├───┤   ├───┤   └───┘
│   │   start   count    CRC
│   FC=0x03
Slave addr=1
```

**Response** (Slave 1 → Master):

```
01  03  06  02  2B  00  00  00  63  XX  XX
│   │   │   ├───┤   ├───┤   ├───┤   └───┘
│   │   │   Reg0     Reg1    Reg2     CRC
│   │   byte_count=6
│   FC=0x03
Slave addr=1
```

Register values decoded: `0x022B = 555`, `0x0000 = 0`, `0x0063 = 99`

### Write Multiple Registers (FC 0x10) — Frame Walkthrough

**Request** (write 2 registers at address 0x0010 with values 0x00C8, 0x01F4):

```
01  10  00  10  00  02  04  00  C8  01  F4  CB  BE
│   │   ├───┤   ├───┤   │   ├───┤   ├───┤   └───┘
│   │   start   count  byte  val0    val1    CRC
│   FC=0x10          count=4
Slave=1
```

**Response** (echo of start + count):

```
01  10  00  10  00  02  XX  XX
│   │   ├───┤   ├───┤   └───┘
│   │   start   count   CRC
│   FC=0x10
Slave=1
```

---

## Error Handling and Exception Responses

When a slave detects an error, it replies with the function code OR'd with 0x80:

```
01  83  02  C0  F1
│   │   │   └───┘
│   │   exc=0x02 (Illegal Data Address)
│   FC=0x03 | 0x80
Slave=1
```

| Exception Code | Meaning                         | Typical Cause                             |
|----------------|---------------------------------|-------------------------------------------|
| 0x01           | Illegal Function                | Unsupported function code                 |
| 0x02           | Illegal Data Address            | Register address out of range             |
| 0x03           | Illegal Data Value              | Invalid count or data length              |
| 0x04           | Server Device Failure           | Hardware fault in slave                   |
| 0x05           | Acknowledge                     | Long processing time (slave ACKs, delays) |
| 0x06           | Server Device Busy              | Slave processing another request          |

### Timeout Handling Strategy

```c
// C: Retry with exponential backoff
#define MAX_RETRIES 3

int modbus_read_with_retry(modbus_ctx_t *ctx, uint8_t slave,
                           uint16_t addr, uint16_t count, uint16_t *out) {
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        int ret = modbus_read_holding_registers(ctx, slave, addr, count, out);
        if (ret == 0) return 0;
        if (ret < -4) break; // Protocol error, no point retrying
        // Wait before retry: 50ms, 100ms, 200ms
        usleep((50 << attempt) * 1000);
    }
    return -1;
}
```

```rust
// Rust: retry with exponential backoff
pub fn read_with_retry(
    master: &mut ModbusMaster,
    slave: u8, addr: u16, count: u16,
    max_retries: u32,
) -> ModbusResult<Vec<u16>> {
    let mut delay = Duration::from_millis(50);
    for attempt in 0..max_retries {
        match master.read_holding_registers(slave, addr, count) {
            Ok(v) => return Ok(v),
            Err(ModbusError::Timeout) if attempt + 1 < max_retries => {
                std::thread::sleep(delay);
                delay *= 2;
            }
            Err(e) => return Err(e),
        }
    }
    Err(ModbusError::Timeout)
}
```

---

## RS-485 Considerations

Most Modbus RTU deployments use **RS-485** for multi-drop wiring. RS-485 is a differential, half-duplex standard that supports up to 32 unit loads per segment (typically ~32 devices without repeaters, or up to 256 with 1/8-load transceivers).

### DE/RE Pin Control

RS-485 transceivers have a **Driver Enable (DE)** and **Receiver Enable (/RE)** pin. These must be asserted before transmission and de-asserted after:

```c
// Bare-metal C (e.g., STM32 HAL)
#define RS485_DE_PIN  GPIO_PIN_1
#define RS485_DE_PORT GPIOA

static void rs485_tx_enable(void) {
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);
}

static void rs485_rx_enable(void) {
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);
}

int uart_send_rs485(UART_HandleTypeDef *huart,
                   const uint8_t *buf, size_t len) {
    rs485_tx_enable();
    HAL_UART_Transmit(huart, (uint8_t*)buf, len, 100);
    // Wait for transmission complete
    while (__HAL_UART_GET_FLAG(huart, UART_FLAG_TC) == RESET);
    rs485_rx_enable();
    return 0;
}
```

### Linux RS-485 Mode (Kernel Support)

Modern Linux kernels can manage DE/RE automatically via the serial driver:

```c
#include <linux/serial.h>
#include <sys/ioctl.h>

int enable_rs485(int fd) {
    struct serial_rs485 rs485conf = {0};
    rs485conf.flags |= SER_RS485_ENABLED;
    rs485conf.flags |= SER_RS485_RTS_ON_SEND;   // DE high when sending
    rs485conf.flags &= ~SER_RS485_RTS_AFTER_SEND; // DE low after send
    rs485conf.delay_rts_before_send = 0;
    rs485conf.delay_rts_after_send  = 0;
    return ioctl(fd, TIOCSRS485, &rs485conf);
}
```

### Termination Resistors

- Place **120 Ω** resistors at **both ends** of the RS-485 bus cable.
- Use **biasing resistors** (470 Ω–1 kΩ pull-up on A, pull-down on B) to define the idle state.
- Keep stubs (branch connections) as short as possible, ideally < 1 m.

---

## Summary

Modbus RTU over UART is the bedrock of industrial serial communication, combining a straightforward binary framing protocol with the simplicity of UART hardware. The core elements are:

**Protocol fundamentals:** Every exchange is a master-initiated request addressed to a specific slave (1–247). Frames consist of a slave address, function code, data payload, and a 16-bit CRC checksum appended little-endian. Frame boundaries are determined purely by silence gaps (≥ 3.5 character times), not delimiter bytes.

**UART configuration:** 8N1 at a mutually agreed baud rate (9600 is the default, up to 115200 common). All devices on the bus must use identical settings. Timing discipline around T1.5 (intra-frame character gap) and T3.5 (inter-frame silence) is essential for correct framing.

**CRC-16/Modbus:** The error detection uses a reflected CRC-16 (polynomial 0xA001, init 0xFFFF). The CRC is appended low byte first. Both master and slave must recompute and verify the CRC on every received frame.

**Function codes:** The most used operations are FC 0x03 (read holding registers), FC 0x06 (write single register), and FC 0x10 (write multiple registers). Exception responses (FC | 0x80) signal slave-detected errors with a one-byte exception code.

**C/C++ implementation** centres on POSIX `termios` for UART configuration, manual frame building with CRC appending, and select-based receive with T1.5 inter-character timeout to detect frame ends. The slave implementation dispatches by function code and builds exception frames for unsupported or out-of-range requests.

**Rust implementation** uses the `serialport` crate for cross-platform UART access, typed enums for function and exception codes, and the `thiserror` crate for expressive error propagation. The slave processes frames immutably and returns `Option<Vec<u8>>` to cleanly separate broadcast (no response) from unicast communication.

**RS-485 integration:** In multi-drop deployments, the RS-485 Driver Enable (DE) line must be asserted before transmission and released immediately after the last stop bit. Linux kernel RS-485 mode (`TIOCSRS485`) can automate this. Proper bus termination (120 Ω at both ends) and biasing resistors are required for signal integrity.

Modbus RTU remains relevant today because of its simplicity, determinism, and the vast installed base of compatible devices — from PLCs and VFDs to temperature controllers and power meters — making it an essential protocol in any embedded systems or industrial automation toolkit.

---

*Document generated for the UART Topics Series — Topic 61*
