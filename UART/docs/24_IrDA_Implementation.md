# 24. IrDA Implementation

**Theory & Architecture**
- Full IrDA protocol stack diagram (IrPHY → IrLAP → IrLMP → IrOBEX/IrCOMM)
- 3/16 pulse encoding explained with timing calculations for 9600 and 115200 baud
- IrLAP frame structure, byte stuffing, and CRC-CCITT-16 details
- IrLAP connection sequence (discovery, SNRM/UA handshake, disconnect)

**C/C++ Code Examples**
- POSIX `termios` UART initialization for Linux
- CRC-CCITT implementation (bit-by-bit, MCU-friendly)
- IrLAP frame builder with escape encoding and BOF/EOF wrapping
- State-machine frame decoder
- Blocking send/receive with timeout
- STM32 HAL IrDA SIR mode (hardware pulse shaping)
- FreeRTOS task skeleton for embedded use

**Rust Code Examples**
- `Cargo.toml` dependencies (`serialport`, `crc`, `thiserror`)
- CRC-CCITT function
- Custom `IrDaError` enum with `thiserror`
- `build_frame()` using idiomatic iterators
- `IrDaDecoder` state machine with `Default` and proper enum states
- `IrDaChannel` struct wrapping `serialport` with `send()` / `receive()`
- XID discovery frame sketch

**Practical Guidance**
- Transceiver IC comparison table (TFDU4101, SFH7743, HSDL-3201, TFBS4650)
- Common failure modes table with mitigations
- Retry logic pattern
- Linux `irattach` notes for legacy kernels

## Infrared Data Association Protocol Using UART Physical Layer

---

## Table of Contents

1. [Introduction](#introduction)
2. [IrDA Protocol Stack Overview](#irda-protocol-stack-overview)
3. [Physical Layer (IrDA SIR)](#physical-layer-irda-sir)
4. [UART Configuration for IrDA](#uart-configuration-for-irda)
5. [IrDA Framing and Encoding](#irda-framing-and-encoding)
6. [IrLAP – Link Access Protocol](#irlap--link-access-protocol)
7. [Hardware Considerations](#hardware-considerations)
8. [C/C++ Implementation Examples](#cc-implementation-examples)
9. [Rust Implementation Examples](#rust-implementation-examples)
10. [Error Handling and Robustness](#error-handling-and-robustness)
11. [Platform Examples (Linux, Embedded)](#platform-examples-linux-embedded)
12. [Summary](#summary)

---

## Introduction

**IrDA (Infrared Data Association)** is a set of specifications for short-range, point-to-point infrared wireless communication. Originally standardized in 1994, IrDA was widely used in PDAs, laptops, mobile phones, and printers before Bluetooth and Wi-Fi became dominant. It remains relevant in industrial, embedded, and low-power environments where license-free, directional, interference-resistant communication is required.

Key characteristics of IrDA:

- **Range**: Typically 0–1 meter (standard), up to 2 meters
- **Direction**: Narrow cone (~15–30° half-angle), point-to-point only
- **Data rates**: 9.6 kbps (SIR) up to 16 Mbps (VFIR)
- **Physical medium**: Near-infrared light (850–900 nm)
- **Physical layer transport**: Standard UART hardware (for SIR mode)
- **Protocol stack**: Layered – IrPHY → IrLAP → IrLMP → IrIAS / IrCOMM / IrOBEX

The most common mode for microcontroller and embedded work is **SIR (Serial Infrared)**, which directly maps to standard UART operation with a 3/16 pulse encoding.

---

## IrDA Protocol Stack Overview

```
+---------------------------+
|  Applications             |  (File transfer, serial emulation, etc.)
+---------------------------+
|  IrOBEX / IrCOMM / IrIAS |  (Object Exchange, Serial Comm, Info Access)
+---------------------------+
|  IrLMP                    |  (Link Management Protocol)
+---------------------------+
|  IrLAP                    |  (Link Access Protocol – framing, CRC, ARQ)
+---------------------------+
|  IrPHY / IrSIR            |  (Physical – UART + IR transceiver)
+---------------------------+
|  UART Hardware            |  (Standard async serial, typically 115200 baud)
+---------------------------+
|  IR LED / Photodetector   |  (Hardware transceiver module)
+---------------------------+
```

For most embedded implementations, only the **IrPHY** and **IrLAP** layers are needed for raw data exchange. Higher layers (IrLMP, IrOBEX, IrCOMM) add complexity but enable interoperability with standard IrDA stacks on PCs and phones.

---

## Physical Layer (IrDA SIR)

### Pulse Encoding: 3/16 Duty Cycle

IrDA SIR does **not** transmit raw UART logic levels through the IR LED. Instead, each UART zero-bit (space) is replaced by a short infrared pulse of **3/16 of the bit period**. UART one-bits (mark) produce **no light**.

This encoding serves two purposes:
- Reduces average IR power consumption
- Enables the receiver to distinguish valid IrDA pulses from ambient IR noise

For 115200 baud:
- Bit period = 1/115200 ≈ 8.68 µs
- IrDA pulse width = 3/16 × 8.68 µs ≈ 1.63 µs

For 9600 baud:
- Bit period = 1/9600 ≈ 104.2 µs
- IrDA pulse width = 3/16 × 104.2 µs ≈ 19.5 µs

### UART Idle State

- UART idle = logic HIGH = **IR LED OFF** (no light)
- Start bit = logic LOW = **IR LED pulse** (light burst)
- Data '0' bit = logic LOW = **IR LED pulse**
- Data '1' bit = logic HIGH = **IR LED OFF**

### Inverted Logic

Many IrDA transceivers have **active-low RXD output** — the photodetector outputs a low pulse when it detects IR light. Some UART peripherals have a dedicated IrDA mode that handles this inversion transparently. If not, you may need to invert the RX signal in software or hardware.

---

## UART Configuration for IrDA

IrDA SIR uses a standard asynchronous UART frame:

| Parameter     | Value                          |
|---------------|-------------------------------|
| Data bits     | 8                              |
| Parity        | None                           |
| Stop bits     | 1                              |
| Flow control  | None (IrDA handles this at LAP)|
| Baud rate     | 9600 to 115200 bps (SIR)      |

The UART itself is configured identically to a normal serial port. The key difference is the **IR transceiver module** (e.g., TFDU4101, SFH7743, HSDL-3201) between the UART and the IR medium, which implements the 3/16 pulse encoding/decoding in hardware.

Some MCU UART peripherals (e.g., STM32, LPC series) have a built-in **IrDA SIR mode** that handles the pulse shaping directly in silicon.

---

## IrDA Framing and Encoding

### IrLAP Frame Structure

All data exchanged over IrDA SIR is wrapped in IrLAP frames. Each frame uses:

- **BOF (Beginning of Frame)**: One or more `0xC0` bytes
- **Data payload**: Escaped using HDLC-like byte stuffing
- **CRC**: 16-bit CRC-CCITT over address + control + data
- **EOF (End of Frame)**: `0xC1` byte

```
+--------+--------+--------+----------+--------+--------+
| BOF(s) | Addr   | Control| Data...  | CRC16  |  EOF   |
| 0xC0   | 1 byte | 1 byte | variable | 2 bytes| 0xC1   |
+--------+--------+--------+----------+--------+--------+
```

**Extra BOF bytes** are sent before the actual frame to give the remote receiver time to wake its demodulator. The number of extra BOFs depends on the minimum turn-around time (MinTAT) parameter negotiated during connection.

### Byte Stuffing (Transparency Encoding)

To prevent `0xC0` and `0xC1` appearing inside the data payload, IrLAP uses escape encoding:

- `0xC0` in data → transmit `0x7D`, `0xC0 XOR 0x20` = `0x7D`, `0xE0`
- `0xC1` in data → transmit `0x7D`, `0xC1 XOR 0x20` = `0x7D`, `0xE1`
- `0x7D` in data → transmit `0x7D`, `0x7D XOR 0x20` = `0x7D`, `0x5D`

### CRC Calculation

IrDA uses **CRC-CCITT (polynomial 0x1021)**, initialized to `0xFFFF`, transmitted LSB first, complemented before transmission.

---

## IrLAP – Link Access Protocol

IrLAP manages connection establishment, data sequencing, and acknowledgement. Key concepts:

### Device Addresses
Each IrDA device has a 32-bit device address (randomly generated at power-on). During discovery, devices exchange addresses using XID (Exchange Station Identification) frames.

### Station Roles
- **Primary (P)**: Initiates connection, controls the link (poll/final bit)
- **Secondary (S)**: Responds to primary

### Frame Types
- **I-frames**: Information frames carrying upper-layer data
- **S-frames**: Supervisory (RR = Receive Ready, RNR = Receive Not Ready, REJ = Reject)
- **U-frames**: Unnumbered (SNRM = Set Normal Response Mode, UA = Unnumbered Acknowledgement, DISC = Disconnect)

### Connection Sequence
```
Primary                      Secondary
   |                              |
   |--- XID (discovery) --------> |
   |<-- XID (response) ---------- |
   |                              |
   |--- SNRM (connect req) -----> |
   |<-- UA (connected) ---------- |
   |                              |
   |=== I-frames (data) ========= |
   |                              |
   |--- DISC (disconnect) ------> |
   |<-- UA (acknowledged) ------- |
```

---

## Hardware Considerations

### Transceiver Modules

Common IrDA SIR transceiver ICs:

| Part         | Manufacturer | Max Rate  | Notes                          |
|--------------|--------------|-----------|-------------------------------|
| TFDU4101     | Vishay       | 115.2 kbps| 5V, standard choice            |
| SFH7743      | OSRAM        | 115.2 kbps| 3.3V friendly                  |
| HSDL-3201    | Broadcom     | 115.2 kbps| Very compact SMD package       |
| TFBS4650     | Vishay       | 4 Mbps    | MIR capable                    |

### Wiring a UART to an IrDA Transceiver

```
MCU UART TX ──────────────────────> TXD pin of transceiver
MCU UART RX <── (possible invert) ── RXD pin of transceiver
                                      │
                                  [IR LED] → → → IR light
                                  [Photodetector] ← ← ← IR light
```

Note that most transceivers output an **active-low** signal on RXD. If your UART peripheral does not natively support IrDA mode (with built-in inversion), you need either:
- A hardware inverter (e.g., single NAND gate)
- A software bit-banging approach
- An MCU UART with configurable polarity inversion

### Minimum Turn-Around Time (MinTAT)

After transmitting, a device must wait a minimum period before it can receive again. This allows the remote transceiver to recover and switch from receive to transmit. Typical MinTAT values: 10 ms (default), can be negotiated down to 500 µs.

---

## C/C++ Implementation Examples

### 1. UART Initialization for IrDA (Linux / POSIX)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>
#include <errno.h>

/* Linux IrDA UART mode flag */
#ifndef TIOCM_RTS
#define TIOCM_RTS 0x004
#endif

/**
 * Open and configure a serial port for IrDA SIR communication.
 *
 * @param device  Path to serial device, e.g., "/dev/ttyS1"
 * @param baud    Baud rate constant, e.g., B9600 or B115200
 * @return        File descriptor, or -1 on error
 */
int irda_open_uart(const char *device, speed_t baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    /* Set baud rate */
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    /* 8N1 raw mode — required for IrDA */
    tty.c_cflag  = (tty.c_cflag & ~CSIZE) | CS8; /* 8 data bits */
    tty.c_cflag &= ~PARENB;                        /* No parity */
    tty.c_cflag &= ~CSTOPB;                        /* 1 stop bit */
    tty.c_cflag &= ~CRTSCTS;                       /* No HW flow control */
    tty.c_cflag |=  CREAD | CLOCAL;               /* Enable receiver */

    /* Raw input */
    tty.c_iflag  = 0;  /* No software flow control, no special handling */
    tty.c_oflag  = 0;  /* Raw output */
    tty.c_lflag  = 0;  /* Non-canonical, no echo */

    /* Blocking read with timeout: 100 ms */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  /* 100ms in units of 1/10 second */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}
```

### 2. CRC-CCITT Calculation

```c
#include <stdint.h>
#include <stddef.h>

/**
 * Compute CRC-CCITT (polynomial 0x1021) over a buffer.
 * IrDA initializes CRC to 0xFFFF and complements the final result.
 *
 * @param data    Pointer to data buffer
 * @param length  Number of bytes
 * @return        16-bit CRC value (not yet complemented)
 */
uint16_t crc_ccitt(const uint8_t *data, size_t length)
{
    uint16_t crc = 0xFFFF;

    for (size_t i = 0; i < length; i++) {
        uint8_t byte = data[i];
        for (int bit = 0; bit < 8; bit++) {
            int mix = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8408;  /* Reversed polynomial 0x1021 */
            }
            byte >>= 1;
        }
    }
    return crc;
}

/**
 * Compute IrDA frame CRC (complement of CRC-CCITT).
 */
uint16_t irda_frame_crc(const uint8_t *data, size_t length)
{
    return ~crc_ccitt(data, length);
}
```

### 3. IrLAP Frame Encoding (Byte Stuffing + Framing)

```c
#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define IRDA_BOF        0xC0
#define IRDA_EOF        0xC1
#define IRDA_ESC        0x7D
#define IRDA_ESC_XOR    0x20

#define IRDA_MAX_FRAME  2048
#define IRDA_EXTRA_BOFS 10   /* Number of extra BOFs before frame */

/**
 * Escape a single byte for IrDA frame transparency encoding.
 * Returns number of bytes written to output (1 or 2).
 */
static int irda_escape_byte(uint8_t byte, uint8_t *out)
{
    if (byte == IRDA_BOF || byte == IRDA_EOF || byte == IRDA_ESC) {
        out[0] = IRDA_ESC;
        out[1] = byte ^ IRDA_ESC_XOR;
        return 2;
    }
    out[0] = byte;
    return 1;
}

/**
 * Build a complete IrLAP frame ready for transmission.
 *
 * @param addr       Station address (1 byte)
 * @param control    Control byte (U/S/I frame type)
 * @param payload    Upper-layer data (may be NULL if len == 0)
 * @param payload_len Length of payload
 * @param out_buf    Output buffer (caller must provide IRDA_MAX_FRAME bytes)
 * @return           Total bytes in out_buf, or -1 on overflow
 */
int irda_build_frame(uint8_t addr, uint8_t control,
                     const uint8_t *payload, size_t payload_len,
                     uint8_t *out_buf)
{
    uint8_t raw[512];    /* Pre-escape: addr + ctrl + payload */
    size_t  raw_len = 0;

    /* Assemble raw content for CRC calculation */
    raw[raw_len++] = addr;
    raw[raw_len++] = control;
    if (payload && payload_len > 0) {
        if (raw_len + payload_len > sizeof(raw)) return -1;
        memcpy(raw + raw_len, payload, payload_len);
        raw_len += payload_len;
    }

    /* Compute IrDA CRC */
    uint16_t crc = irda_frame_crc(raw, raw_len);
    raw[raw_len++] = (uint8_t)(crc & 0xFF);         /* CRC LSB first */
    raw[raw_len++] = (uint8_t)((crc >> 8) & 0xFF);

    /* Build output: extra BOFs + BOF + escaped content + EOF */
    int out_pos = 0;

    /* Extra BOFs (wake-up for remote receiver) */
    for (int i = 0; i < IRDA_EXTRA_BOFS; i++) {
        out_buf[out_pos++] = IRDA_BOF;
    }

    /* Frame BOF */
    out_buf[out_pos++] = IRDA_BOF;

    /* Escaped frame content */
    for (size_t i = 0; i < raw_len; i++) {
        uint8_t tmp[2];
        int n = irda_escape_byte(raw[i], tmp);
        if (out_pos + n >= IRDA_MAX_FRAME) return -1;
        memcpy(out_buf + out_pos, tmp, n);
        out_pos += n;
    }

    /* EOF */
    out_buf[out_pos++] = IRDA_EOF;

    return out_pos;
}
```

### 4. IrLAP Frame Decoder (State Machine)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

typedef enum {
    IRDA_PARSE_IDLE,      /* Waiting for BOF */
    IRDA_PARSE_IN_FRAME,  /* Collecting frame bytes */
    IRDA_PARSE_ESCAPE,    /* Last byte was ESC, unescape next */
} IrDAParseState;

typedef struct {
    IrDAParseState state;
    uint8_t        buf[512];
    size_t         len;
    bool           frame_ready;
    /* Parsed fields (valid when frame_ready == true) */
    uint8_t        addr;
    uint8_t        control;
    uint8_t        data[480];
    size_t         data_len;
} IrDADecoder;

void irda_decoder_init(IrDADecoder *dec)
{
    memset(dec, 0, sizeof(*dec));
    dec->state = IRDA_PARSE_IDLE;
}

/**
 * Feed one received byte into the IrDA frame decoder.
 * Returns true when a complete, CRC-valid frame has been received.
 */
bool irda_decoder_feed(IrDADecoder *dec, uint8_t byte)
{
    dec->frame_ready = false;

    switch (dec->state) {
    case IRDA_PARSE_IDLE:
        if (byte == IRDA_BOF) {
            dec->len = 0;
            dec->state = IRDA_PARSE_IN_FRAME;
        }
        break;

    case IRDA_PARSE_IN_FRAME:
        if (byte == IRDA_BOF) {
            /* Extra BOF or frame restart — reset buffer */
            dec->len = 0;
        } else if (byte == IRDA_EOF) {
            /* End of frame — validate CRC */
            if (dec->len < 4) {
                /* Too short: addr + ctrl + CRC16 minimum */
                dec->state = IRDA_PARSE_IDLE;
                break;
            }
            size_t content_len = dec->len - 2;
            uint16_t rx_crc = (uint16_t)dec->buf[content_len]
                            | ((uint16_t)dec->buf[content_len + 1] << 8);
            uint16_t calc_crc = irda_frame_crc(dec->buf, content_len);

            if (rx_crc == calc_crc) {
                dec->addr    = dec->buf[0];
                dec->control = dec->buf[1];
                dec->data_len = content_len - 2;
                if (dec->data_len > 0) {
                    memcpy(dec->data, dec->buf + 2, dec->data_len);
                }
                dec->frame_ready = true;
            }
            dec->state = IRDA_PARSE_IDLE;
        } else if (byte == IRDA_ESC) {
            dec->state = IRDA_PARSE_ESCAPE;
        } else {
            if (dec->len < sizeof(dec->buf)) {
                dec->buf[dec->len++] = byte;
            } else {
                dec->state = IRDA_PARSE_IDLE; /* Buffer overflow — discard */
            }
        }
        break;

    case IRDA_PARSE_ESCAPE:
        if (dec->len < sizeof(dec->buf)) {
            dec->buf[dec->len++] = byte ^ IRDA_ESC_XOR;
        }
        dec->state = IRDA_PARSE_IN_FRAME;
        break;
    }

    return dec->frame_ready;
}
```

### 5. Sending and Receiving an IrDA Frame (Linux, Blocking)

```c
#include <unistd.h>
#include <stdio.h>

/**
 * Transmit an IrDA IrLAP frame over a UART file descriptor.
 * The caller is responsible for honouring MinTAT before calling this
 * after a receive operation.
 */
int irda_send_frame(int fd, uint8_t addr, uint8_t control,
                    const uint8_t *data, size_t data_len)
{
    uint8_t frame_buf[IRDA_MAX_FRAME];
    int frame_len = irda_build_frame(addr, control, data, data_len, frame_buf);
    if (frame_len < 0) {
        fprintf(stderr, "irda_build_frame: overflow\n");
        return -1;
    }

    ssize_t written = write(fd, frame_buf, frame_len);
    if (written < 0) {
        perror("write");
        return -1;
    }
    if ((size_t)written != (size_t)frame_len) {
        fprintf(stderr, "Short write: %zd of %d\n", written, frame_len);
        return -1;
    }
    return 0;
}

/**
 * Receive loop — feed bytes from UART into decoder until a frame arrives
 * or timeout occurs.  Returns 0 on success, -1 on error/timeout.
 */
int irda_receive_frame(int fd, IrDADecoder *dec, unsigned int timeout_ms)
{
    uint8_t byte;
    unsigned int elapsed = 0;
    const unsigned int poll_interval_ms = 1;

    irda_decoder_init(dec);

    while (elapsed < timeout_ms) {
        ssize_t n = read(fd, &byte, 1);
        if (n == 1) {
            if (irda_decoder_feed(dec, byte)) {
                return 0;  /* Frame received and CRC valid */
            }
        } else {
            usleep(poll_interval_ms * 1000);
            elapsed += poll_interval_ms;
        }
    }
    return -1;  /* Timeout */
}

/* --- Example usage --- */
int main(void)
{
    const uint8_t BROADCAST_ADDR = 0xFF;
    const uint8_t CTRL_UI        = 0x03;  /* Unnumbered Information frame */

    int fd = irda_open_uart("/dev/ttyS1", B115200);
    if (fd < 0) return 1;

    const char *message = "Hello IrDA!";
    printf("Sending: %s\n", message);

    if (irda_send_frame(fd, BROADCAST_ADDR, CTRL_UI,
                        (const uint8_t *)message,
                        strlen(message)) < 0) {
        close(fd);
        return 1;
    }

    /* Minimum turn-around time: wait 10 ms before receiving */
    usleep(10 * 1000);

    IrDADecoder dec;
    if (irda_receive_frame(fd, &dec, 1000) == 0) {
        printf("Received frame: addr=0x%02X ctrl=0x%02X data_len=%zu\n",
               dec.addr, dec.control, dec.data_len);
        if (dec.data_len > 0) {
            printf("Data: %.*s\n", (int)dec.data_len, dec.data);
        }
    } else {
        printf("Receive timeout.\n");
    }

    close(fd);
    return 0;
}
```

### 6. STM32 Embedded IrDA SIR Mode (HAL, C)

```c
/**
 * STM32 IrDA SIR initialization using STM32 HAL.
 * USART2 is used; TXD/RXD connected to TFDU4101 transceiver.
 */
#include "stm32f4xx_hal.h"

UART_HandleTypeDef huart2;
IRDA_HandleTypeDef hirda2;

void irda_stm32_init(void)
{
    /* USART2 base configuration */
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;

    /* IrDA-specific handle wraps the UART */
    hirda2.Instance          = USART2;
    hirda2.Init.BaudRate     = 115200;
    hirda2.Init.WordLength   = IRDA_WORDLENGTH_8B;
    hirda2.Init.Parity       = IRDA_PARITY_NONE;
    hirda2.Init.Mode         = IRDA_MODE_TX_RX;
    /* Prescaler controls 3/16 pulse width: PSC = fPCLK / (16 * baud) */
    hirda2.Init.Prescaler    = 1;
    hirda2.Init.PowerMode    = IRDA_POWERMODE_NORMAL;

    if (HAL_IRDA_Init(&hirda2) != HAL_OK) {
        /* Initialization error */
        Error_Handler();
    }
}

/**
 * Transmit raw bytes via STM32 IrDA (hardware handles 3/16 pulse encoding).
 */
HAL_StatusTypeDef irda_stm32_transmit(const uint8_t *data, uint16_t len,
                                       uint32_t timeout_ms)
{
    return HAL_IRDA_Transmit(&hirda2, data, len, timeout_ms);
}

/**
 * Receive bytes via STM32 IrDA.
 */
HAL_StatusTypeDef irda_stm32_receive(uint8_t *buf, uint16_t len,
                                      uint32_t timeout_ms)
{
    return HAL_IRDA_Receive(&hirda2, buf, len, timeout_ms);
}

/**
 * Example: send an IrLAP UI frame from the STM32.
 * The frame_buf is prepared by irda_build_frame() from the generic
 * framing code shown earlier in this document.
 */
void irda_stm32_example(void)
{
    irda_stm32_init();

    uint8_t frame[IRDA_MAX_FRAME];
    const char *msg = "STM32 IrDA";
    int flen = irda_build_frame(0xFF, 0x03,
                                (const uint8_t *)msg, strlen(msg),
                                frame);
    if (flen > 0) {
        irda_stm32_transmit(frame, (uint16_t)flen, 100);
    }
}
```

---

## Rust Implementation Examples

### 1. Dependencies (`Cargo.toml`)

```toml
[dependencies]
serialport = "4"       # Cross-platform UART access
crc       = "3"        # CRC computation
thiserror = "1"        # Ergonomic error types
```

### 2. CRC-CCITT in Rust

```rust
/// Compute the IrDA CRC-CCITT value for a byte slice.
/// IrDA uses CRC-CCITT init=0xFFFF, poly=0x1021, reflected input/output,
/// final XOR=0xFFFF (complement).
pub fn irda_crc(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        let mut b = byte;
        for _ in 0..8 {
            let mix = ((crc ^ b as u16) & 0x0001) != 0;
            crc >>= 1;
            if mix {
                crc ^= 0x8408; // Reversed 0x1021
            }
            b >>= 1;
        }
    }
    !crc // Final complement
}
```

### 3. IrDA Error Type

```rust
use thiserror::Error;

#[derive(Debug, Error)]
pub enum IrDaError {
    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),

    #[error("Frame too large")]
    FrameTooLarge,

    #[error("CRC mismatch: expected {expected:#06X}, got {actual:#06X}")]
    CrcMismatch { expected: u16, actual: u16 },

    #[error("Receive timeout")]
    Timeout,

    #[error("Frame decode error: {0}")]
    Decode(String),

    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
}

pub type Result<T> = std::result::Result<T, IrDaError>;
```

### 4. Frame Builder in Rust

```rust
pub const IRDA_BOF: u8 = 0xC0;
pub const IRDA_EOF: u8 = 0xC1;
pub const IRDA_ESC: u8 = 0x7D;
pub const IRDA_ESC_XOR: u8 = 0x20;
pub const EXTRA_BOFS: usize = 10;

/// Build a complete IrLAP frame with escape encoding, CRC, BOFs, and EOF.
///
/// # Arguments
/// * `addr`    - Station address byte
/// * `control` - Control byte (frame type)
/// * `payload` - Upper-layer data to include in frame
///
/// # Returns
/// A `Vec<u8>` ready for transmission, or an error if the frame is too large.
pub fn build_frame(addr: u8, control: u8, payload: &[u8]) -> Result<Vec<u8>> {
    // Assemble raw bytes for CRC: addr + control + payload
    let mut raw: Vec<u8> = Vec::with_capacity(2 + payload.len() + 2);
    raw.push(addr);
    raw.push(control);
    raw.extend_from_slice(payload);

    // Append CRC (LSB first)
    let crc = irda_crc(&raw);
    raw.push((crc & 0xFF) as u8);
    raw.push(((crc >> 8) & 0xFF) as u8);

    // Build output buffer with BOFs, escaped content, EOF
    let mut frame: Vec<u8> = Vec::with_capacity(EXTRA_BOFS + 2 + raw.len() * 2 + 1);

    // Extra BOFs
    frame.extend(std::iter::repeat(IRDA_BOF).take(EXTRA_BOFS));
    // Frame BOF
    frame.push(IRDA_BOF);

    // Escape each byte
    for &byte in &raw {
        if byte == IRDA_BOF || byte == IRDA_EOF || byte == IRDA_ESC {
            frame.push(IRDA_ESC);
            frame.push(byte ^ IRDA_ESC_XOR);
        } else {
            frame.push(byte);
        }
    }

    // EOF
    frame.push(IRDA_EOF);

    Ok(frame)
}
```

### 5. Frame Decoder State Machine in Rust

```rust
#[derive(Debug, Default, PartialEq)]
enum ParseState {
    #[default]
    Idle,
    InFrame,
    Escape,
}

/// Decoded IrLAP frame contents.
#[derive(Debug)]
pub struct IrDaFrame {
    pub addr:    u8,
    pub control: u8,
    pub data:    Vec<u8>,
}

/// Stateful IrDA frame decoder.
/// Feed bytes one at a time via `feed()`; it returns `Some(frame)` when
/// a complete, CRC-valid frame has been decoded.
#[derive(Default)]
pub struct IrDaDecoder {
    state: ParseState,
    buf:   Vec<u8>,
}

impl IrDaDecoder {
    pub fn new() -> Self {
        Self::default()
    }

    /// Feed one received byte into the decoder.
    /// Returns `Some(IrDaFrame)` if a valid frame was completed, `None` otherwise.
    pub fn feed(&mut self, byte: u8) -> Option<IrDaFrame> {
        match self.state {
            ParseState::Idle => {
                if byte == IRDA_BOF {
                    self.buf.clear();
                    self.state = ParseState::InFrame;
                }
                None
            }
            ParseState::InFrame => match byte {
                IRDA_BOF => {
                    // Repeated BOF or frame restart
                    self.buf.clear();
                    None
                }
                IRDA_EOF => {
                    self.state = ParseState::Idle;
                    self.try_decode_frame()
                }
                IRDA_ESC => {
                    self.state = ParseState::Escape;
                    None
                }
                _ => {
                    self.buf.push(byte);
                    None
                }
            },
            ParseState::Escape => {
                self.buf.push(byte ^ IRDA_ESC_XOR);
                self.state = ParseState::InFrame;
                None
            }
        }
    }

    fn try_decode_frame(&mut self) -> Option<IrDaFrame> {
        // Minimum: addr(1) + ctrl(1) + CRC(2) = 4 bytes
        if self.buf.len() < 4 {
            return None;
        }

        let content_len = self.buf.len() - 2;
        let rx_crc = (self.buf[content_len] as u16)
                   | ((self.buf[content_len + 1] as u16) << 8);
        let calc_crc = irda_crc(&self.buf[..content_len]);

        if rx_crc != calc_crc {
            return None; // CRC mismatch — discard frame
        }

        let addr    = self.buf[0];
        let control = self.buf[1];
        let data    = self.buf[2..content_len].to_vec();

        Some(IrDaFrame { addr, control, data })
    }
}
```

### 6. IrDA UART Channel (Full Rust Example)

```rust
use serialport::{SerialPort, SerialPortBuilder};
use std::time::{Duration, Instant};

pub struct IrDaChannel {
    port:    Box<dyn SerialPort>,
    decoder: IrDaDecoder,
}

impl IrDaChannel {
    /// Open a serial port configured for IrDA SIR.
    ///
    /// # Example
    /// ```no_run
    /// let mut ch = IrDaChannel::open("/dev/ttyS1", 115_200).unwrap();
    /// ```
    pub fn open(device: &str, baud_rate: u32) -> Result<Self> {
        let port = serialport::new(device, baud_rate)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .timeout(Duration::from_millis(10))
            .open()?;

        Ok(Self {
            port,
            decoder: IrDaDecoder::new(),
        })
    }

    /// Transmit an IrLAP frame.
    pub fn send(&mut self, addr: u8, control: u8, data: &[u8]) -> Result<()> {
        let frame = build_frame(addr, control, data)?;
        self.port.write_all(&frame)?;
        Ok(())
    }

    /// Receive the next valid IrDA frame, with timeout.
    pub fn receive(&mut self, timeout: Duration) -> Result<IrDaFrame> {
        let deadline = Instant::now() + timeout;
        let mut byte_buf = [0u8; 1];

        loop {
            if Instant::now() >= deadline {
                return Err(IrDaError::Timeout);
            }
            match self.port.read(&mut byte_buf) {
                Ok(1) => {
                    if let Some(frame) = self.decoder.feed(byte_buf[0]) {
                        return Ok(frame);
                    }
                }
                Ok(_) | Err(_) => {
                    // Timeout on individual byte read — keep looping
                }
            }
        }
    }
}

/// Demonstration: send a ping and wait for echo.
fn main() -> Result<()> {
    const BROADCAST: u8 = 0xFF;
    const CTRL_UI:   u8 = 0x03; // Unnumbered Information

    let mut ch = IrDaChannel::open("/dev/ttyS1", 115_200)?;

    let msg = b"Hello from Rust IrDA!";
    println!("Sending {} bytes...", msg.len());
    ch.send(BROADCAST, CTRL_UI, msg)?;

    // Honour MinTAT before switching to receive
    std::thread::sleep(Duration::from_millis(10));

    match ch.receive(Duration::from_secs(2)) {
        Ok(frame) => {
            println!(
                "Received: addr={:#04X} ctrl={:#04X} data={:?}",
                frame.addr,
                frame.control,
                String::from_utf8_lossy(&frame.data)
            );
        }
        Err(IrDaError::Timeout) => println!("No response within timeout."),
        Err(e) => return Err(e),
    }

    Ok(())
}
```

### 7. IrDA Discovery (XID Exchange Sketch in Rust)

```rust
/// Simplified XID (Exchange Station Identification) frame payload.
/// Used during IrDA device discovery.
#[derive(Debug)]
pub struct XidPayload {
    pub source_addr:  u32,  // Sender's 32-bit device address
    pub dest_addr:    u32,  // 0xFFFFFFFF = broadcast
    pub discovery_flags: u8,
    pub slot_number:  u8,
    pub version:      u8,
}

impl XidPayload {
    pub fn to_bytes(&self) -> Vec<u8> {
        let mut v = Vec::with_capacity(11);
        v.extend_from_slice(&self.source_addr.to_le_bytes());
        v.extend_from_slice(&self.dest_addr.to_le_bytes());
        v.push(self.discovery_flags);
        v.push(self.slot_number);
        v.push(self.version);
        v
    }
}

/// Broadcast an XID discovery frame.
pub fn irda_discover(ch: &mut IrDaChannel, own_addr: u32) -> Result<()> {
    const CTRL_XID: u8 = 0x2F; // Unnumbered XID frame

    let xid = XidPayload {
        source_addr:     own_addr,
        dest_addr:       0xFFFF_FFFF,
        discovery_flags: 0x00,
        slot_number:     0xFF, // Final XID slot
        version:         0x00,
    };

    ch.send(0xFF, CTRL_XID, &xid.to_bytes())?;
    println!("XID discovery broadcast sent.");
    Ok(())
}
```

---

## Error Handling and Robustness

### Common Failure Modes

| Issue                          | Cause                                     | Mitigation                              |
|-------------------------------|-------------------------------------------|-----------------------------------------|
| CRC errors                    | Misalignment, ambient IR noise            | Retry logic, shielding                  |
| No response (timeout)         | Device not present, wrong direction       | Check alignment, retry discovery        |
| Corrupted frames              | MinTAT violation (TX/RX collision)        | Increase MinTAT wait                    |
| RXD always active             | Transceiver inversion issue               | Invert RXD logic (hardware or software) |
| Garbage on startup            | Receiver saturated by ambient light       | Shield transceiver, use IR filter       |
| Missed frames at high baud    | CPU too slow to service UART interrupts   | Use DMA or reduce baud rate             |

### Retry Strategy (C Example)

```c
#define MAX_RETRIES   5
#define RETRY_DELAY_MS 20

int irda_send_with_retry(int fd, uint8_t addr, uint8_t control,
                         const uint8_t *data, size_t data_len)
{
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (irda_send_frame(fd, addr, control, data, data_len) == 0) {
            IrDADecoder dec;
            if (irda_receive_frame(fd, &dec, 200) == 0) {
                return 0;  /* Success */
            }
        }
        usleep(RETRY_DELAY_MS * 1000);
    }
    return -1;  /* All retries exhausted */
}
```

---

## Platform Examples (Linux, Embedded)

### Enabling IrDA on Linux (legacy `irattach`)

On older Linux kernels (< 4.14) with IrDA subsystem support:

```bash
# Attach serial port to IrDA stack
irattach /dev/ttyS1 -s

# Scan for IrDA devices
irdadump

# Use IrCOMM virtual serial port
# (appears as /dev/ircomm0 after peer connects)
```

On modern Linux (kernel ≥ 4.14, IrDA subsystem removed), user-space IrDA is implemented directly over a raw serial port as shown in the code examples above.

### FreeRTOS Embedded Task (C)

```c
#include "FreeRTOS.h"
#include "task.h"

#define IRDA_TASK_STACK  512
#define IRDA_TASK_PRIO   2

static void irda_task(void *pvParameters)
{
    (void)pvParameters;

    /* STM32 IrDA hardware already initialised before scheduler starts */
    uint8_t rx_buf[128];
    IrDADecoder dec;

    for (;;) {
        /* Receive one byte at a time (DMA or interrupt-driven preferred) */
        if (HAL_IRDA_Receive(&hirda2, rx_buf, 1, 5) == HAL_OK) {
            if (irda_decoder_feed(&dec, rx_buf[0])) {
                /* Process complete frame */
                process_irda_frame(&dec);
                irda_decoder_init(&dec);
            }
        }
        taskYIELD();
    }
}

void irda_start_task(void)
{
    xTaskCreate(irda_task, "IrDA", IRDA_TASK_STACK, NULL,
                IRDA_TASK_PRIO, NULL);
}
```

---

## Summary

IrDA implements short-range, directional, infrared communication on top of a standard UART physical layer. The key points for implementation are:

**Physical Layer**: IrDA SIR uses 3/16-duty-cycle pulse encoding — hardware transceiver modules handle this transparently. Configure your UART as 8N1 with no flow control. Be aware of potential active-low RXD inversion depending on the transceiver.

**Framing (IrLAP)**: All data is encapsulated in IrLAP frames consisting of extra BOFs, a frame BOF (`0xC0`), HDLC-escaped content (address + control + data), a CRC-CCITT-16, and an EOF byte (`0xC1`). The byte stuffing mechanism prevents BOF/EOF/ESC bytes from appearing inside the payload.

**CRC**: Use CRC-CCITT (poly `0x1021`, init `0xFFFF`, reflected, complemented), transmitted LSB-first. Both C and Rust implementations of the bit-by-bit algorithm are straightforward and suitable for microcontrollers without CRC hardware.

**Minimum Turn-Around Time (MinTAT)**: After any transmission, always wait the MinTAT (default 10 ms) before switching to receive mode. Violating MinTAT causes frame collisions and is a common source of hard-to-debug errors.

**C/C++**: A POSIX `termios`-based implementation works on Linux. Embedded targets with STM32-class MCUs benefit from the built-in HAL IRDA mode, which handles pulse shaping in hardware. A decoder implemented as a state machine is the cleanest approach for parsing incoming byte streams.

**Rust**: The `serialport` crate provides cross-platform UART access. Rust's type system and ownership model make the decoder state machine and error handling particularly clean. The `thiserror` crate is recommended for ergonomic error propagation.

**Higher-layer protocols** (IrLMP, IrOBEX, IrCOMM) are only necessary for full interoperability with standard IrDA stacks. For custom device-to-device communication, implementing IrLAP framing (as shown above) is sufficient and significantly simpler.

---

*Document generated for the UART Topics series — Topic 24: IrDA Implementation.*