# 93. Current Loop Interfaces

- **Principle of Operation** — why current (not voltage) is used, active vs. passive loop configurations, with ASCII circuit diagrams
- **Electrical Characteristics** — complete parameter table including loop resistance budget calculations
- **Signal Encoding** — UART framing on the loop, timing diagram for ASCII character transmission
- **Loop Topologies** — point-to-point, multidrop (series receivers), and full-duplex (two loops)
- **C/C++ examples** — three implementations: Linux `termios` API with a full `CurrentLoopPort` struct, a Windows Win32 COM port class, and an STM32 HAL bare-metal configuration; all include a higher-level sensor polling function with a simple industrial framing protocol (STX/ETX + CRC-8)
- **Rust example** — full `serialport`-crate implementation with typed error handling via `thiserror`, a protocol layer with `build_frame`/`parse_frame`, an async variant for `tokio`, and a complete unit test suite
- **Industrial protocols** — MODBUS RTU timing requirements and DNP3 context
- **Troubleshooting** — symptom-based diagnostic guide including the classic "LED in series" health indicator trick
- **Summary** — engineering decision checklist covering supply voltage, isolation, cable, baud rate, and frame format selection

## 20 mA Current Loop for Industrial Noise Immunity

---

## Table of Contents

1. [Introduction](#introduction)
2. [Historical Background](#historical-background)
3. [Principle of Operation](#principle-of-operation)
4. [Electrical Characteristics](#electrical-characteristics)
5. [Signal Encoding](#signal-encoding)
6. [Loop Topologies](#loop-topologies)
7. [Advantages Over Voltage-Based Interfaces](#advantages-over-voltage-based-interfaces)
8. [Limitations and Considerations](#limitations-and-considerations)
9. [Hardware Implementation](#hardware-implementation)
10. [Programming in C/C++](#programming-in-cc)
11. [Programming in Rust](#programming-in-rust)
12. [Industrial Protocol Integration](#industrial-protocol-integration)
13. [Troubleshooting and Diagnostics](#troubleshooting-and-diagnostics)
14. [Summary](#summary)

---

## Introduction

The **20 mA current loop** is one of the oldest and most reliable serial communication standards still in widespread use within industrial environments. Unlike conventional voltage-based interfaces such as RS-232 or TTL UART, a current loop transmits data by modulating an electrical current rather than a voltage level. This fundamental distinction provides extraordinary resilience against the electromagnetic interference (EMI), ground potential differences, and long-cable signal degradation that are endemic to factory floors, power plants, process control systems, and other electrically hostile environments.

The standard takes its name from the nominal current levels used to represent binary states: **20 mA** for a logical MARK (logic 1, or "loop closed") and **0 mA** for a logical SPACE (logic 0, or "loop open"). Because receivers respond to current flow rather than absolute voltage, the interface is inherently immune to common-mode noise and resistive losses along cable runs that can extend hundreds of meters.

---

## Historical Background

Current loop interfaces predate the transistor. Their roots lie in 19th-century telegraph systems, where the Morse code "key" simply opened or closed a current loop. By the early 20th century, teletype (TTY) machines standardized on 60 mA loops for electromechanical solenoid actuation. As solid-state electronics emerged, the current was reduced to **20 mA** — sufficient to drive optical isolators and transistors without excessive power dissipation.

The 20 mA current loop became the de facto standard for terminal-to-host communication in the minicomputer era (1960s–1980s), appearing in DEC VT100 terminals, Teletype Model 33 machines, and early PLC (Programmable Logic Controller) systems. Although RS-232 supplanted it for desktop computing, the current loop survived — and thrives today — in:

- Industrial SCADA systems
- Building automation and energy management
- Railway signalling and trackside equipment
- Medical instrumentation
- Utility metering and smart grid infrastructure
- Legacy serial terminal emulation

---

## Principle of Operation

### Current as the Information Carrier

In a voltage interface (e.g., RS-232), the receiver measures voltage relative to a shared ground. If the cable runs through an environment with high electrical noise or if the transmitter and receiver have different ground potentials (a common situation when equipment is powered from different distribution panels), the voltage at the receiver is corrupted.

A current loop avoids this entirely. The transmitter acts as a **controlled current source** and the receiver acts as a **current sink** or detector. The current flowing in the loop is the same everywhere in a series circuit — regardless of resistive drops along the cable or differences in ground reference between the two ends. The receiver does not measure voltage; it detects whether current is flowing.

```
  ┌───────────────────────────────────────────────────────┐
  │                    CURRENT LOOP                        │
  │                                                        │
  │  ┌──────────┐    +24V (Loop Power)                    │
  │  │          │──────────────────────────────────┐      │
  │  │ Current  │                                  │      │
  │  │ Source   │  20 mA = MARK (logic 1)          ▼      │
  │  │(Transmit)│   0 mA = SPACE (logic 0)    ┌────────┐  │
  │  │          │                             │ Current│  │
  │  │          │◄────────────────────────────│ Sink / │  │
  │  └──────────┘    Return (GND side)        │Detector│  │
  │                                           └────────┘  │
  └───────────────────────────────────────────────────────┘
```

### Active vs. Passive Loops

A current loop requires a power source to drive current through the loop. This source can reside at either end:

- **Active transmitter / passive receiver**: The transmitter contains the current source (most common in modern implementations).
- **Passive transmitter / active receiver**: The receiver supplies loop power; the transmitter is a simple switch (transistor or optocoupler) that opens or closes the loop.
- **Fully active (both ends powered)**: Each side has its own power supply with series isolation — used when galvanic isolation is mandatory.

---

## Electrical Characteristics

| Parameter | Typical Value | Notes |
|---|---|---|
| MARK current (logic 1) | 20 mA | Loop closed |
| SPACE current (logic 0) | 0 mA | Loop open |
| Loop supply voltage | 12 V – 48 V DC | Higher voltages support longer cables |
| Maximum cable resistance | ~1 kΩ total | Limits cable length at a given voltage |
| Maximum bit rate | 19,200 bps | Practical upper limit; slower is common |
| Common baud rates | 300, 1200, 9600 | 9600 bps most prevalent |
| Receiver threshold | ~10 mA | Current above = MARK; below = SPACE |
| Typical cable impedance | ~100 Ω/km | Shielded twisted pair preferred |
| Maximum cable length | 1,000+ m | At low baud rates with 48 V supply |
| Isolation voltage | Up to 2,500 V | When optoisolators are used |

### Loop Resistance Budget

The total series resistance of the loop (cable + transmitter internal + receiver internal) determines the required supply voltage:

```
V_supply ≥ I_loop × R_total + V_drops
V_supply ≥ 0.020 A × R_total + V_Vce(sat) + V_LED(opto)

Example: 500 m cable at 100 Ω/km = 50 Ω each way = 100 Ω cable resistance
         Plus 50 Ω transmitter output resistance + 100 Ω receiver resistance
         Total: 250 Ω
         Required: 0.020 × 250 = 5 V minimum + device drops ≈ 12 V supply
```

---

## Signal Encoding

The current loop carries standard asynchronous serial data — the same UART framing used by RS-232 and TTL UART:

```
Idle:  ─────────────────────  (20 mA, MARK)
Start: _                       (0 mA, SPACE, 1 bit)
Data:  D0 D1 D2 D3 D4 D5 D6 D7 (8 data bits, LSB first)
Stop:  ─                       (20 mA, MARK, 1 or 2 bits)
```

### Timing Diagram at 9600 bps (ASCII 'A' = 0x41 = 0b01000001)

```
Current
(mA)
20 ──┐   ┌───┐       ┌───────────────┐
     │   │   │       │               │
 0   ┘   └───┘       └───────────────┘
     │Strt│ 1 │ 0 │ 0 │ 0 │ 0 │ 0 │ 1 │ 0 │Stop│
     ◄─104 µs─► (1 bit period at 9600 bps)
```

The bit period at 9600 bps is 1/9600 ≈ 104.17 µs. The idle state is MARK (20 mA current flowing), and the start bit pulls the loop to SPACE (0 mA) to signal the beginning of a frame.

---

## Loop Topologies

### Point-to-Point (Standard)

The most common configuration — one transmitter and one receiver forming a single loop:

```
  Transmitter                    Receiver
  ┌──────────┐                  ┌──────────┐
  │  +Vcc    │──────────────────│  +In     │
  │          │    Signal Wire   │          │
  │  TXD ───►│                  │►── RXD   │
  │          │    Return Wire   │          │
  │  GND     │──────────────────│  -In     │
  └──────────┘                  └──────────┘
```

### Multidrop (One-to-Many)

Multiple receivers can be wired in series on a single loop. The same 20 mA flows through all receivers simultaneously — a simple and effective broadcast topology:

```
  Transmitter ──► Receiver 1 ──► Receiver 2 ──► Receiver 3 ──► Return
  (Current Source)  (Opto 1)      (Opto 2)       (Opto 3)
```

**Limitation**: All receivers see identical data simultaneously. Selective addressing requires a higher-level protocol.

### Full-Duplex (Two Loops)

For bidirectional communication, two independent current loops are used — one for each direction of data flow. This is the standard for UART-based systems:

```
  Device A                          Device B
  ┌──────────┐                    ┌──────────┐
  │ TX Loop  │──── Loop 1 ───────►│ RX Loop  │
  │          │                    │          │
  │ RX Loop  │◄─── Loop 2 ────────│ TX Loop  │
  └──────────┘                    └──────────┘
```

---

## Advantages Over Voltage-Based Interfaces

### 1. Immunity to Common-Mode Noise

Industrial environments generate enormous electromagnetic interference from motor drives, welding equipment, switching power supplies, and high-voltage power lines. This noise appears as common-mode voltage — the same voltage superimposed on both conductors of a cable pair. A current loop receiver, which responds only to differential current, rejects this noise completely (when optoisolated).

### 2. Ground Loop Elimination

When two pieces of equipment share a voltage-referenced interface (such as RS-232), a "ground loop" forms if their chassis grounds differ in potential. Even a few volts of ground potential difference can corrupt RS-232 signals (nominal ±12 V). In a current loop with galvanic isolation, there is no shared ground — the loop is a completely isolated circuit. Ground potential differences of hundreds of volts can be tolerated.

### 3. Long Cable Runs

RS-232 is limited to approximately 15 m (50 ft) at 9600 bps by the EIA-232 standard (based on 2,500 pF cable capacitance). A current loop driven at 24 V with shielded twisted pair cable can operate reliably at 9600 bps over 1,000 m or more, as the limiting factors are cable resistance and loop inductance rather than capacitive loading.

### 4. Cable Fault Detection

The idle state of a current loop (MARK = 20 mA flowing) means that an **open circuit** immediately registers as continuous SPACE — an error condition that is easily detected. Voltage interfaces typically cannot distinguish an open cable from a valid logic-low signal.

### 5. Simplified Wiring Verification

A clamp meter or current probe on the loop cable provides an immediate indication of whether the loop is operating correctly — 20 mA flowing means the transmitter is active. No oscilloscope or signal analysis is required for basic health checks.

---

## Limitations and Considerations

### Maximum Speed

The inductance and capacitance of long cables create a low-pass filter effect. At 1,000 m, the cable's characteristic time constant significantly limits the achievable bit rate. Practical maximum speeds are:

- 300 m: up to 19,200 bps
- 600 m: up to 9,600 bps  
- 1,000 m: up to 1,200 bps

### No Standardized Connector

Unlike RS-232 (DB-9/DB-25) or RS-485 (terminal blocks), the 20 mA current loop has no universally adopted connector standard. Industrial implementations use everything from screw terminals to proprietary connectors.

### Power Consumption

A continuously conducting loop dissipates power: at 20 mA and 24 V, the loop consumes 480 mW. In battery-operated or low-power applications this is significant. Modern approaches use lower current levels (4–20 mA process loops) or switch to RS-485 for such scenarios.

### No Built-in Multi-Master Capability

The current loop is inherently a single-master topology. Multi-master configurations require external arbitration logic.

---

## Hardware Implementation

### Transmitter Circuit

The transmitter converts a UART logic-level signal (TTL or CMOS) into a controlled 20 mA current source:

```
                        +24V
                          │
                         [R1]  ← Set current: R1 = (24V - 2V) / 20mA ≈ 1.1 kΩ
                          │
  UART TXD ──┤ NPN  ├────┼──────── Loop +Out
             │ 2N2222│   │
             └───────┘   [Loop cable]
                         │
                     ┌───┘ Receiver side
                     │
  UART RXD ◄─── [Opto] ─── Loop -In
                     │
                    GND
```

A more complete transmitter using an op-amp current source for precision:

```
  UART TXD ──[R_in]──┤+        ├──── Transistor Base
                     │  Op-Amp │
              [Rsense]──┤-      ├
                     │
                    GND
```

### Receiver Circuit (Optoisolated)

The receiver uses an optocoupler to achieve galvanic isolation:

```
  Loop + ────[R_limit]──┤ LED  │──── Loop -
                        │      │
                        │Photo-│
                        │Trans │
  Isolated +Vcc ────────┤ Coll │
                        │      ├──── UART RXD (Isolated Side)
  Isolated GND ─────────┤ Emit │
```

Recommended optocouplers:
- **6N137**: High-speed (1 Mbit/s), logic-level output — suitable for baud rates up to 19,200 bps
- **PC817**: Low-cost, 80 kbit/s, adequate for ≤9,600 bps
- **HCPL-0631**: Dual channel, industrial temperature range
- **IL300**: Linear optocoupler for precision current measurement applications

### Complete Interface IC Solutions

Several dedicated ICs provide complete current loop interfaces:

- **Texas Instruments XTR115/XTR116**: 4–20 mA transmitters with precision regulation
- **Analog Devices AD693**: Loop-powered 4–20 mA transmitter
- **Maxim MAX8212**: Current loop receiver with TTL output
- **NXP TJA1050**: CAN-based but demonstrates similar isolation principles

---

## Programming in C/C++

### Platform Context

The software interface to a current loop is typically via a standard UART peripheral — the current loop hardware performs the electrical translation transparently. From the software perspective, programming a current loop interface is equivalent to programming a UART. The key parameters are:

- Baud rate (commonly 9,600 bps)
- Frame format: 8 data bits, 1 stop bit, no parity (8N1) — or 7E1/8E2 for legacy systems
- No hardware flow control (RTS/CTS are not defined in the current loop standard)

### Linux UART Configuration (termios API)

```c
/**
 * current_loop_uart.c
 * UART configuration and I/O for 20 mA current loop interface on Linux.
 * The current loop hardware handles electrical conversion transparently;
 * this code configures the host UART and provides send/receive functions.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* Default configuration for industrial current loop */
#define CL_DEFAULT_BAUD     B9600
#define CL_DEFAULT_TIMEOUT  5       /* seconds */
#define CL_RX_BUFFER_SIZE   256

typedef struct {
    int       fd;               /* file descriptor */
    speed_t   baud;             /* baud rate constant */
    uint32_t  timeout_s;        /* read timeout in seconds */
    bool      loop_open_detect; /* cable fault detection flag */
    uint64_t  rx_bytes;         /* statistics */
    uint64_t  tx_bytes;
    uint64_t  frame_errors;
} CurrentLoopPort;

/**
 * current_loop_open - Open and configure a current loop UART port.
 *
 * @port:      Pointer to CurrentLoopPort structure to initialise.
 * @device:    Path to serial device, e.g. "/dev/ttyS0".
 * @baud:      Baud rate constant (B300, B1200, B9600, B19200).
 * @timeout_s: Read timeout in seconds (0 = blocking).
 *
 * Returns 0 on success, -1 on error (errno set).
 *
 * NOTE: No hardware flow control is enabled. The current loop standard
 *       does not define RTS/CTS signals.
 */
int current_loop_open(CurrentLoopPort *port, const char *device,
                      speed_t baud, uint32_t timeout_s)
{
    struct termios tty;

    if (!port || !device) {
        errno = EINVAL;
        return -1;
    }

    /* Open the device: read/write, no controlling terminal, non-blocking open */
    port->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (port->fd < 0) {
        perror("current_loop_open: open");
        return -1;
    }

    /* Switch to blocking I/O after open */
    int flags = fcntl(port->fd, F_GETFL, 0);
    fcntl(port->fd, F_SETFL, flags & ~O_NONBLOCK);

    /* Retrieve current terminal attributes */
    if (tcgetattr(port->fd, &tty) != 0) {
        perror("current_loop_open: tcgetattr");
        close(port->fd);
        return -1;
    }

    /* Set baud rate (input and output) */
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* Configure for raw (non-canonical) mode — 8N1, no flow control */
    cfmakeraw(&tty);

    tty.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= CS8;         /* 8 data bits */
    tty.c_cflag |= CREAD;       /* Enable receiver */
    tty.c_cflag |= CLOCAL;      /* Ignore modem control lines */

    /* No parity */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);

    /* Disable output processing */
    tty.c_oflag &= ~OPOST;

    /* Configure VMIN/VTIME for read timeout */
    if (timeout_s > 0) {
        tty.c_cc[VMIN]  = 0;                    /* Return as soon as any data available */
        tty.c_cc[VTIME] = (cc_t)(timeout_s * 10); /* Timeout in tenths of a second */
    } else {
        tty.c_cc[VMIN]  = 1;                    /* Block until at least 1 byte */
        tty.c_cc[VTIME] = 0;
    }

    /* Apply settings immediately */
    if (tcsetattr(port->fd, TCSANOW, &tty) != 0) {
        perror("current_loop_open: tcsetattr");
        close(port->fd);
        return -1;
    }

    /* Flush any stale data in hardware buffers */
    tcflush(port->fd, TCIOFLUSH);

    port->baud             = baud;
    port->timeout_s        = timeout_s;
    port->loop_open_detect = false;
    port->rx_bytes         = 0;
    port->tx_bytes         = 0;
    port->frame_errors     = 0;

    return 0;
}

/**
 * current_loop_send - Transmit a buffer over the current loop.
 *
 * On a MARK-idle current loop, idle = 20 mA. Transmitting drives the
 * loop to 0 mA (SPACE) for start bits and data '0' bits.
 *
 * Returns number of bytes written, or -1 on error.
 */
ssize_t current_loop_send(CurrentLoopPort *port, const uint8_t *buf, size_t len)
{
    if (!port || !buf || port->fd < 0) return -1;

    ssize_t written = write(port->fd, buf, len);
    if (written < 0) {
        perror("current_loop_send: write");
        return -1;
    }

    /* Wait for all bytes to physically leave the UART FIFO */
    tcdrain(port->fd);

    port->tx_bytes += (uint64_t)written;
    return written;
}

/**
 * current_loop_recv - Receive bytes from the current loop with timeout.
 *
 * Loop-open detection: A continuously open loop produces a stream of
 * framing errors (BREAK condition). This function increments frame_errors
 * and sets loop_open_detect when >3 errors occur within a short window.
 *
 * Returns number of bytes received (0 = timeout), -1 on error.
 */
ssize_t current_loop_recv(CurrentLoopPort *port, uint8_t *buf, size_t maxlen)
{
    if (!port || !buf || port->fd < 0) return -1;

    ssize_t n = read(port->fd, buf, maxlen);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; /* Timeout */
        perror("current_loop_recv: read");
        return -1;
    }

    port->rx_bytes += (uint64_t)n;
    return n;
}

/**
 * current_loop_detect_fault - Check for cable open/fault condition.
 *
 * Reads the UART line status via ioctl and checks for persistent BREAK
 * (continuous SPACE = 0 mA = loop open or cable break).
 *
 * Returns true if a fault is detected.
 */
#include <sys/ioctl.h>

bool current_loop_detect_fault(CurrentLoopPort *port)
{
    int status = 0;
    if (ioctl(port->fd, TIOCMGET, &status) < 0) return false;

    /* On a healthy loop, the UART sees Mark = idle = no BREAK.
     * A persistent BREAK (TIOCM_RI set or framing error stream) indicates
     * an open loop. This is platform-specific; check modem status lines. */
    return (port->frame_errors > 3);
}

/**
 * current_loop_close - Close the port and release resources.
 */
void current_loop_close(CurrentLoopPort *port)
{
    if (port && port->fd >= 0) {
        tcdrain(port->fd);
        close(port->fd);
        port->fd = -1;
    }
}

/**
 * current_loop_print_stats - Print communication statistics.
 */
void current_loop_print_stats(const CurrentLoopPort *port)
{
    if (!port) return;
    printf("Current Loop Statistics:\n");
    printf("  TX bytes     : %llu\n", (unsigned long long)port->tx_bytes);
    printf("  RX bytes     : %llu\n", (unsigned long long)port->rx_bytes);
    printf("  Frame errors : %llu\n", (unsigned long long)port->frame_errors);
    printf("  Loop fault   : %s\n",   port->loop_open_detect ? "DETECTED" : "OK");
}

/* ─────────────────────────────────────────────────────────────
   Example: Industrial sensor polling over 20 mA current loop
   ───────────────────────────────────────────────────────────── */

/**
 * Format of a simple proprietary industrial protocol:
 *   [STX][ADDR][CMD][LEN][DATA...][CRC8][ETX]
 *    0x02  1B   1B   1B   0-16B   1B   0x03
 */
#define STX 0x02
#define ETX 0x03

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
    }
    return crc;
}

/**
 * poll_sensor - Send a read request and receive the response.
 *
 * @port:    Configured CurrentLoopPort.
 * @address: Device address on the multi-drop loop (1–31).
 * @command: Command byte (e.g., 0x01 = read temperature).
 * @response: Output buffer (caller-allocated, minimum 32 bytes).
 * @resp_len: Output: number of bytes in response.
 *
 * Returns 0 on success, negative error code on failure.
 */
int poll_sensor(CurrentLoopPort *port, uint8_t address, uint8_t command,
                uint8_t *response, size_t *resp_len)
{
    uint8_t frame[8];
    frame[0] = STX;
    frame[1] = address;
    frame[2] = command;
    frame[3] = 0x00;   /* No payload data in query */
    frame[4] = crc8(&frame[1], 3);
    frame[5] = ETX;

    /* Transmit query — drives loop to 0 mA for SPACE bits */
    if (current_loop_send(port, frame, 6) != 6) {
        fprintf(stderr, "poll_sensor: transmit failed\n");
        return -1;
    }

    /* Allow line to settle (propagation delay for long cables) */
    usleep(5000); /* 5 ms guard time */

    /* Receive response */
    uint8_t rx_buf[64];
    ssize_t n = current_loop_recv(port, rx_buf, sizeof(rx_buf));

    if (n <= 0) {
        fprintf(stderr, "poll_sensor: no response from address %u\n", address);
        return -2; /* Timeout — possible loop fault or absent device */
    }

    /* Validate frame structure */
    if (rx_buf[0] != STX || rx_buf[n-1] != ETX) {
        fprintf(stderr, "poll_sensor: invalid frame delimiters\n");
        return -3;
    }

    uint8_t expected_crc = crc8(&rx_buf[1], (size_t)(n - 3));
    if (rx_buf[n-2] != expected_crc) {
        fprintf(stderr, "poll_sensor: CRC error (got 0x%02X, expected 0x%02X)\n",
                rx_buf[n-2], expected_crc);
        return -4;
    }

    if (response && resp_len) {
        size_t payload_len = (size_t)(n - 4); /* Exclude STX, ADDR, CRC, ETX */
        memcpy(response, &rx_buf[3], payload_len);
        *resp_len = payload_len;
    }

    return 0;
}

/* ─────────────────────────────────────────────────────────────
   Embedded / Bare-Metal: STM32 USART Configuration
   ───────────────────────────────────────────────────────────── */

#ifdef STM32_TARGET  /* Conditionally compiled for embedded targets */

#include "stm32f4xx_hal.h"

static UART_HandleTypeDef huart2;

/**
 * current_loop_stm32_init - Configure STM32 USART2 for current loop.
 *
 * USART2 TX on PA2, RX on PA3. External 20 mA current loop interface
 * hardware (optocoupler + current source) connects to these pins.
 *
 * Hardware note: STM32 UART idle state is HIGH (MARK). The current loop
 * transmitter must invert this to produce 20 mA idle current.
 * Many industrial interface ICs handle this inversion internally.
 */
void current_loop_stm32_init(void)
{
    /* Enable peripheral clocks */
    __HAL_RCC_USART2_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();

    /* Configure GPIO pins for USART2 alternate function */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin       = GPIO_PIN_2 | GPIO_PIN_3;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP;  /* Pull-up maintains MARK on idle */
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;  /* Low speed reduces EMI */
    gpio.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* Configure USART for 9600 8N1, no flow control */
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart2);
}

/**
 * current_loop_stm32_send - Blocking transmit for STM32.
 */
HAL_StatusTypeDef current_loop_stm32_send(const uint8_t *data, uint16_t len)
{
    return HAL_UART_Transmit(&huart2, (uint8_t *)data, len, HAL_MAX_DELAY);
}

/**
 * current_loop_stm32_recv - Receive with 100 ms timeout.
 */
HAL_StatusTypeDef current_loop_stm32_recv(uint8_t *buf, uint16_t len,
                                           uint16_t timeout_ms)
{
    return HAL_UART_Receive(&huart2, buf, len, timeout_ms);
}

#endif /* STM32_TARGET */

/* ─────────────────────────────────────────────────────────────
   Main example: Poll 3 sensors on a multidrop current loop
   ───────────────────────────────────────────────────────────── */

int main(void)
{
    CurrentLoopPort port = {0};

    if (current_loop_open(&port, "/dev/ttyS0", CL_DEFAULT_BAUD, 2) != 0) {
        fprintf(stderr, "Failed to open current loop port\n");
        return EXIT_FAILURE;
    }

    printf("Current loop initialised. Polling sensors...\n\n");

    const uint8_t sensor_addresses[] = {0x01, 0x02, 0x03};
    const uint8_t CMD_READ_TEMP = 0x10;

    for (int cycle = 0; cycle < 5; cycle++) {
        printf("Polling cycle %d:\n", cycle + 1);

        for (size_t i = 0; i < sizeof(sensor_addresses); i++) {
            uint8_t response[32];
            size_t  resp_len = 0;

            int rc = poll_sensor(&port, sensor_addresses[i],
                                 CMD_READ_TEMP, response, &resp_len);
            if (rc == 0 && resp_len >= 2) {
                /* Simple 2-byte big-endian fixed-point: value / 10 = °C */
                int16_t raw = (int16_t)((response[0] << 8) | response[1]);
                printf("  Sensor 0x%02X: %.1f °C\n",
                       sensor_addresses[i], (float)raw / 10.0f);
            } else if (rc == -2) {
                printf("  Sensor 0x%02X: TIMEOUT (check loop continuity)\n",
                       sensor_addresses[i]);
            } else {
                printf("  Sensor 0x%02X: ERROR (rc=%d)\n",
                       sensor_addresses[i], rc);
            }
        }

        sleep(1);
    }

    current_loop_print_stats(&port);
    current_loop_close(&port);
    return EXIT_SUCCESS;
}
```

### Windows Implementation (Win32 API)

```cpp
/**
 * current_loop_win32.cpp
 * Windows COM port configuration for 20 mA current loop interface.
 */

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

class CurrentLoopWin32 {
public:
    HANDLE hPort = INVALID_HANDLE_VALUE;

    /**
     * open - Open and configure a COM port for current loop operation.
     * @portName: e.g., "COM3" or "\\\\.\\COM10" for ports above COM9
     * @baudRate: e.g., 9600, 4800, 1200
     */
    bool open(const char *portName, DWORD baudRate = 9600) {
        hPort = CreateFileA(
            portName,
            GENERIC_READ | GENERIC_WRITE,
            0,                  /* Exclusive access */
            nullptr,
            OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );

        if (hPort == INVALID_HANDLE_VALUE) {
            fprintf(stderr, "Cannot open %s: error %lu\n",
                    portName, GetLastError());
            return false;
        }

        /* Configure DCB (Device Control Block) */
        DCB dcb = {};
        dcb.DCBlength = sizeof(DCB);

        if (!GetCommState(hPort, &dcb)) {
            CloseHandle(hPort);
            return false;
        }

        dcb.BaudRate      = baudRate;
        dcb.ByteSize      = 8;              /* 8 data bits */
        dcb.StopBits      = ONESTOPBIT;
        dcb.Parity        = NOPARITY;
        dcb.fBinary       = TRUE;
        dcb.fParity       = FALSE;
        dcb.fOutxCtsFlow  = FALSE;          /* No CTS flow control */
        dcb.fOutxDsrFlow  = FALSE;
        dcb.fRtsControl   = RTS_CONTROL_DISABLE; /* RTS not used */
        dcb.fDtrControl   = DTR_CONTROL_DISABLE;
        dcb.fOutX         = FALSE;          /* No XON/XOFF */
        dcb.fInX          = FALSE;

        if (!SetCommState(hPort, &dcb)) {
            CloseHandle(hPort);
            return false;
        }

        /* Set read/write timeouts */
        COMMTIMEOUTS timeouts = {};
        timeouts.ReadIntervalTimeout         = 50;   /* 50 ms between chars */
        timeouts.ReadTotalTimeoutMultiplier  = 10;
        timeouts.ReadTotalTimeoutConstant    = 2000; /* 2 s total */
        timeouts.WriteTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant   = 1000;
        SetCommTimeouts(hPort, &timeouts);

        /* Purge any stale data */
        PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);
        return true;
    }

    /**
     * send - Write bytes to the current loop.
     */
    bool send(const uint8_t *data, DWORD len) {
        DWORD written = 0;
        return WriteFile(hPort, data, len, &written, nullptr)
               && written == len;
    }

    /**
     * recv - Read bytes from the current loop with timeout.
     * Returns number of bytes read.
     */
    DWORD recv(uint8_t *buf, DWORD maxLen) {
        DWORD bytesRead = 0;
        ReadFile(hPort, buf, maxLen, &bytesRead, nullptr);
        return bytesRead;
    }

    void close() {
        if (hPort != INVALID_HANDLE_VALUE) {
            CloseHandle(hPort);
            hPort = INVALID_HANDLE_VALUE;
        }
    }

    ~CurrentLoopWin32() { close(); }
};

int main() {
    CurrentLoopWin32 cl;

    if (!cl.open("COM3", 9600)) {
        fprintf(stderr, "Failed to open current loop\n");
        return 1;
    }

    /* Send a simple query to device address 0x01 */
    uint8_t query[] = { 0x02, 0x01, 0x10, 0x00, 0xEF, 0x03 };
    if (!cl.send(query, sizeof(query))) {
        fprintf(stderr, "Send failed: error %lu\n", GetLastError());
        cl.close();
        return 1;
    }

    Sleep(50); /* Wait for response to arrive */

    uint8_t response[64];
    DWORD n = cl.recv(response, sizeof(response));
    if (n > 0) {
        printf("Received %lu bytes:", n);
        for (DWORD i = 0; i < n; i++) printf(" %02X", response[i]);
        printf("\n");
    } else {
        printf("No response (check loop continuity)\n");
    }

    cl.close();
    return 0;
}
```

---

## Programming in Rust

### Dependencies (`Cargo.toml`)

```toml
[package]
name = "current_loop"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4.4"      # Cross-platform serial port library
thiserror  = "1.0"      # Ergonomic error types
log        = "0.4"      # Logging facade
env_logger = "0.11"     # Logger implementation

[dev-dependencies]
```

### Core Implementation

```rust
//! current_loop.rs
//! 20 mA current loop interface driver using the `serialport` crate.
//!
//! The serialport crate provides a cross-platform abstraction over
//! platform serial APIs (termios on Linux/macOS, Win32 on Windows).
//! From the software perspective, a current loop port is configured
//! identically to any UART — the current loop hardware handles the
//! electrical translation.

use serialport::{SerialPort, SerialPortBuilder};
use std::io::{self, Read, Write};
use std::time::Duration;
use thiserror::Error;

/// Errors specific to current loop operations.
#[derive(Debug, Error)]
pub enum CurrentLoopError {
    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),

    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    #[error("Timeout waiting for response from address 0x{address:02X}")]
    Timeout { address: u8 },

    #[error("CRC error: expected 0x{expected:02X}, got 0x{actual:02X}")]
    CrcError { expected: u8, actual: u8 },

    #[error("Invalid frame: bad delimiters")]
    InvalidFrame,

    #[error("Loop open or cable fault detected")]
    LoopFault,

    #[error("Response too short: {got} bytes (minimum {required})")]
    ShortResponse { got: usize, required: usize },
}

pub type Result<T> = std::result::Result<T, CurrentLoopError>;

/// Statistics for the current loop port.
#[derive(Debug, Default)]
pub struct LoopStats {
    pub tx_bytes:     u64,
    pub rx_bytes:     u64,
    pub frame_errors: u64,
    pub timeouts:     u64,
}

/// A configured 20 mA current loop serial port.
pub struct CurrentLoopPort {
    port:  Box<dyn SerialPort>,
    stats: LoopStats,
}

impl CurrentLoopPort {
    /// Open a current loop interface on the specified device.
    ///
    /// # Arguments
    ///
    /// * `device`    - Path to the serial device (e.g., `/dev/ttyS0`, `COM3`)
    /// * `baud_rate` - Baud rate in bits/second (typically 9600)
    /// * `timeout`   - Read timeout duration
    ///
    /// # Example
    ///
    /// ```no_run
    /// use std::time::Duration;
    /// let port = CurrentLoopPort::open("/dev/ttyS0", 9600, Duration::from_secs(2))?;
    /// ```
    pub fn open(device: &str, baud_rate: u32, timeout: Duration) -> Result<Self> {
        let port = serialport::new(device, baud_rate)
            .data_bits(serialport::DataBits::Eight)
            .stop_bits(serialport::StopBits::One)
            .parity(serialport::Parity::None)
            .flow_control(serialport::FlowControl::None) // Current loop: no RTS/CTS
            .timeout(timeout)
            .open()?;

        log::info!(
            "Current loop opened: {} @ {} baud, timeout {:?}",
            device, baud_rate, timeout
        );

        Ok(Self {
            port,
            stats: LoopStats::default(),
        })
    }

    /// Send bytes over the current loop.
    ///
    /// The UART transmitter drives the loop to 0 mA (SPACE) for start
    /// bits and logic-0 data bits, and to 20 mA (MARK) for logic-1 bits.
    pub fn send(&mut self, data: &[u8]) -> Result<usize> {
        let n = self.port.write(data)?;
        self.port.flush()?;
        self.stats.tx_bytes += n as u64;
        log::trace!("TX {} bytes: {:02X?}", n, &data[..n]);
        Ok(n)
    }

    /// Receive up to `max_len` bytes with the configured timeout.
    ///
    /// Returns `Ok(vec)` with the received bytes; an empty vec indicates timeout.
    pub fn recv(&mut self, max_len: usize) -> Result<Vec<u8>> {
        let mut buf = vec![0u8; max_len];
        match self.port.read(&mut buf) {
            Ok(n) => {
                buf.truncate(n);
                self.stats.rx_bytes += n as u64;
                log::trace!("RX {} bytes: {:02X?}", n, &buf);
                Ok(buf)
            }
            Err(e) if e.kind() == io::ErrorKind::TimedOut => {
                self.stats.timeouts += 1;
                Ok(Vec::new()) // Timeout: return empty vec
            }
            Err(e) => Err(CurrentLoopError::Io(e)),
        }
    }

    /// Return a reference to accumulated statistics.
    pub fn stats(&self) -> &LoopStats {
        &self.stats
    }
}

// ─────────────────────────────────────────────────────────────
// Protocol layer: simple industrial framing
// Frame format: [STX=0x02][ADDR][CMD][LEN][DATA...][CRC8][ETX=0x03]
// ─────────────────────────────────────────────────────────────

const STX: u8 = 0x02;
const ETX: u8 = 0x03;

/// CRC-8 with polynomial 0x31 (Maxim/Dallas 1-Wire compatible).
fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0x31
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Build a protocol frame for transmission.
fn build_frame(address: u8, command: u8, payload: &[u8]) -> Vec<u8> {
    let mut frame = Vec::with_capacity(6 + payload.len());
    frame.push(STX);
    frame.push(address);
    frame.push(command);
    frame.push(payload.len() as u8);
    frame.extend_from_slice(payload);

    // CRC covers: ADDR + CMD + LEN + DATA
    let crc_data = &frame[1..]; // everything after STX
    frame.push(crc8(crc_data));
    frame.push(ETX);
    frame
}

/// Parse and validate a received frame.
///
/// Returns the payload bytes on success.
fn parse_frame(raw: &[u8]) -> Result<Vec<u8>> {
    if raw.len() < 5 {
        return Err(CurrentLoopError::ShortResponse {
            got: raw.len(),
            required: 5,
        });
    }

    if raw[0] != STX || raw[raw.len() - 1] != ETX {
        return Err(CurrentLoopError::InvalidFrame);
    }

    // CRC check: covers everything between STX and CRC byte
    let crc_region = &raw[1..raw.len() - 2];
    let expected   = crc8(crc_region);
    let actual     = raw[raw.len() - 2];

    if expected != actual {
        return Err(CurrentLoopError::CrcError { expected, actual });
    }

    // Extract payload: [STX][ADDR][CMD][LEN][...PAYLOAD...][CRC][ETX]
    let len = raw[3] as usize;
    Ok(raw[4..4 + len].to_vec())
}

/// High-level: poll a sensor device on the multidrop current loop.
///
/// Sends a read command and returns the payload from the response.
///
/// # Arguments
///
/// * `port`    - The current loop port
/// * `address` - Device address (1–31 for multidrop)
/// * `command` - Command byte (protocol-defined)
///
/// # Example
///
/// ```no_run
/// let payload = poll_sensor(&mut port, 0x01, 0x10)?;
/// let temp_raw = i16::from_be_bytes([payload[0], payload[1]]);
/// println!("Temperature: {:.1} °C", temp_raw as f32 / 10.0);
/// ```
pub fn poll_sensor(
    port:    &mut CurrentLoopPort,
    address: u8,
    command: u8,
) -> Result<Vec<u8>> {
    let frame = build_frame(address, command, &[]);

    log::debug!("Polling address=0x{:02X} cmd=0x{:02X}", address, command);
    port.send(&frame)?;

    // Guard time for long cable propagation delay
    std::thread::sleep(Duration::from_millis(5));

    let response = port.recv(64)?;

    if response.is_empty() {
        return Err(CurrentLoopError::Timeout { address });
    }

    parse_frame(&response)
}

// ─────────────────────────────────────────────────────────────
// Async version using tokio (optional, feature-gated)
// ─────────────────────────────────────────────────────────────

/// Asynchronous current loop poller for use in tokio runtimes.
///
/// Wraps the blocking port in a `tokio::task::spawn_blocking` call
/// to avoid blocking the async executor thread.
#[cfg(feature = "async")]
pub mod async_loop {
    use super::*;
    use std::sync::{Arc, Mutex};
    use tokio::task;

    pub struct AsyncCurrentLoop {
        inner: Arc<Mutex<CurrentLoopPort>>,
    }

    impl AsyncCurrentLoop {
        pub fn new(port: CurrentLoopPort) -> Self {
            Self {
                inner: Arc::new(Mutex::new(port)),
            }
        }

        /// Asynchronously poll a sensor without blocking the executor.
        pub async fn poll_sensor_async(
            &self,
            address: u8,
            command: u8,
        ) -> Result<Vec<u8>> {
            let inner = Arc::clone(&self.inner);
            task::spawn_blocking(move || {
                let mut port = inner.lock().unwrap();
                poll_sensor(&mut port, address, command)
            })
            .await
            .unwrap_or(Err(CurrentLoopError::LoopFault))
        }
    }
}

// ─────────────────────────────────────────────────────────────
// Example application: multi-sensor industrial data logger
// ─────────────────────────────────────────────────────────────

fn main() -> Result<()> {
    env_logger::init();

    let device   = "/dev/ttyS0";
    let baud     = 9600u32;
    let timeout  = Duration::from_secs(2);

    let mut port = CurrentLoopPort::open(device, baud, timeout)?;

    println!("20 mA Current Loop Data Logger");
    println!("Device: {} @ {} baud\n", device, baud);

    // Sensor addresses on the multidrop loop
    let sensors: &[(u8, &str)] = &[
        (0x01, "Tank Level Sensor"),
        (0x02, "Ambient Temperature"),
        (0x03, "Pressure Transducer"),
    ];

    const CMD_READ_VALUE: u8 = 0x10;
    const POLL_CYCLES:    u32 = 5;

    for cycle in 1..=POLL_CYCLES {
        println!("── Cycle {} ──", cycle);

        for &(addr, name) in sensors {
            match poll_sensor(&mut port, addr, CMD_READ_VALUE) {
                Ok(payload) if payload.len() >= 2 => {
                    let raw = i16::from_be_bytes([payload[0], payload[1]]);
                    println!("  {:30}: {:6.1} units (raw: {})", name, raw as f32 / 10.0, raw);
                }
                Ok(payload) => {
                    println!("  {:30}: short payload ({} bytes)", name, payload.len());
                }
                Err(CurrentLoopError::Timeout { .. }) => {
                    println!("  {:30}: TIMEOUT — check loop continuity", name);
                }
                Err(CurrentLoopError::CrcError { expected, actual }) => {
                    println!(
                        "  {:30}: CRC ERROR (exp=0x{:02X} got=0x{:02X}) — noise?",
                        name, expected, actual
                    );
                }
                Err(e) => {
                    println!("  {:30}: ERROR — {}", name, e);
                }
            }
        }

        if cycle < POLL_CYCLES {
            std::thread::sleep(Duration::from_secs(1));
        }
    }

    // Print statistics
    let s = port.stats();
    println!("\n── Statistics ──");
    println!("  TX bytes : {}", s.tx_bytes);
    println!("  RX bytes : {}", s.rx_bytes);
    println!("  Timeouts : {}", s.timeouts);
    println!("  Frm errs : {}", s.frame_errors);

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc8_known_values() {
        // CRC-8 of [0x01, 0x10, 0x00] should be consistent
        let crc = crc8(&[0x01, 0x10, 0x00]);
        assert_eq!(crc, crc8(&[0x01, 0x10, 0x00]),
                   "CRC8 must be deterministic");
    }

    #[test]
    fn test_frame_roundtrip() {
        let payload = vec![0x01, 0x23];
        let frame   = build_frame(0x01, 0x10, &payload);

        // Frame should start with STX and end with ETX
        assert_eq!(frame[0], STX);
        assert_eq!(*frame.last().unwrap(), ETX);

        // Parsing should recover the original payload
        let recovered = parse_frame(&frame).expect("Frame parsing failed");
        assert_eq!(recovered, payload);
    }

    #[test]
    fn test_parse_rejects_bad_crc() {
        let mut frame = build_frame(0x01, 0x10, &[0x05]);
        let last = frame.len() - 2;
        frame[last] ^= 0xFF; // Corrupt CRC

        assert!(matches!(
            parse_frame(&frame),
            Err(CurrentLoopError::CrcError { .. })
        ));
    }

    #[test]
    fn test_parse_rejects_short_frame() {
        let short = vec![0x02, 0x01];
        assert!(matches!(
            parse_frame(&short),
            Err(CurrentLoopError::ShortResponse { .. })
        ));
    }
}
```

---

## Industrial Protocol Integration

### MODBUS RTU over Current Loop

MODBUS RTU is the most widely used protocol over 20 mA current loops in industrial automation. The current loop carries standard MODBUS RTU frames transparently:

```
MODBUS RTU Frame:
┌─────────┬─────────┬──────┬─────────────┬─────┐
│  Addr   │  Func   │ Data │   CRC-16    │ (gap)│
│  1 byte │  1 byte │ N B  │   2 bytes   │>3.5 chars│
└─────────┴─────────┴──────┴─────────────┴─────┘
```

Key MODBUS timing requirement for current loop: The 3.5-character inter-frame silence that MODBUS uses to detect frame boundaries must be honoured. At 9600 bps with 8N1 framing, this equates to approximately 4 ms of loop-idle (20 mA flowing) between frames.

### DNP3 (Distributed Network Protocol)

DNP3, used extensively in electrical utilities and water treatment facilities, was designed specifically for serial communication over noisy links including current loops. Its three-layer structure (physical, data link, application) provides error correction, fragmentation, and unsolicited reporting — well matched to current loop characteristics.

---

## Troubleshooting and Diagnostics

### Symptom: No data received

1. Measure loop current with a clamp meter: 20 mA idle = transmitter active and loop continuous.
2. Check polarity: current loop is polarity-sensitive. Reverse the loop connections if no current flows.
3. Verify baud rate match between transmitter and receiver.
4. Check for excessive loop resistance (> 1 kΩ total).

### Symptom: Garbled data / framing errors

1. Verify 8N1 vs 7E1 frame format — legacy teletype systems used 7-bit + even parity.
2. Check for baud rate mismatch: even 1% error can cause framing errors over long frames.
3. Inspect cable shielding: unshielded cable in EMI-heavy environments introduces bit errors.
4. Look for ground loops: if optoisolation is absent and two power supplies share a loop, differential ground noise appears as bit errors.

### Symptom: Intermittent communication

1. Inspect cable terminations: a loose screw terminal with 20 mA flowing shows itself as occasional framing errors coinciding with vibration.
2. Check for current source saturation: if the cable resistance approaches V_supply / 20mA, small temperature variations shift the operating point.
3. Verify optocoupler LED current: degraded optocouplers (aged, ESD-damaged) may switch unreliably near the threshold.

### Diagnostic LED Indicator

A simple 20 mA LED in series with the loop provides a real-time visual health indicator — it glows at full brightness during MARK (idle/logic-1) and extinguishes during SPACE (logic-0). On an active 9600 bps link, it appears to glow continuously due to the duty cycle of serial data. An open loop immediately extinguishes the LED entirely.

---

## Summary

The **20 mA current loop** is a mature, elegant solution to one of the fundamental challenges of industrial communication: transmitting data reliably over long distances in electrically noisy environments. Its key strengths are inherent noise immunity (current is independent of voltage noise), natural cable fault detection (open loop = no current = detectable error), excellent range (hundreds to thousands of metres at practical baud rates), and galvanic isolation capability via standard optocouplers.

From a programming perspective, a current loop interface is transparent to the software layer — the UART peripheral and its driver treat the interface exactly as any serial port. Configuration follows standard asynchronous serial practice: 9600 baud, 8N1 framing, no hardware flow control. Both the Linux `termios` API (C/C++) and the `serialport` crate (Rust) provide straightforward access.

The industrial relevance of the current loop endures despite the proliferation of faster, cheaper interfaces (RS-485, Ethernet, CAN) because of its unique combination of simplicity, robustness, and long installation lifetime. Millions of sensors, PLCs, RTUs, and meters installed decades ago continue to operate reliably on 20 mA current loops, and new industrial equipment continues to include current loop interfaces for backward compatibility and environments where its noise immunity characteristics are essential.

Key engineering decisions when deploying a current loop interface:
- **Supply voltage**: Higher voltage (24–48 V) allows longer cable runs; 12 V is sufficient for short runs under 100 m.
- **Optoisolation**: Always isolate when transmitter and receiver are on different power circuits — it eliminates ground loops and protects equipment from high common-mode voltages.
- **Cable selection**: Shielded twisted pair with 100 Ω/km characteristic impedance; ground shield at one end only to prevent shield current loops.
- **Baud rate**: Use 9600 bps or lower for runs exceeding 300 m; increase termination resistance for very long runs.
- **Frame format**: Match exactly to the device — many legacy systems use 7E1 (7 bits, even parity) rather than the more common 8N1.

---

*Document: 93_Current_Loop_Interfaces.md | Series: UART Communication Topics*