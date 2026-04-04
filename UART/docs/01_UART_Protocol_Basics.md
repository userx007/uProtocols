# 01. UART Protocol Basics

**Concepts:**
- What UART is and its key properties (async, full-duplex, point-to-point)
- How synchronisation works without a shared clock (start bit as the trigger)
- Physical signal layers: TTL, RS-232, RS-485 with voltage tables
- Complete data framing: start bit, 5–8 data bits (LSB first!), parity (Even/Odd/None), and stop bits — with ASCII timing diagrams
- Baud rate maths: bit period, frame duration, and ±2–5% tolerance rule
- All common formats: 8N1, 8E1, 7E1, 8N2, etc.

**C/C++ code examples:**
1. **Linux `termios` API** — full open/send/recv/parity functions for `/dev/ttyUSB0`
2. **AVR ATmega328P bare-metal** — direct UCSR register configuration, blocking TX/RX, frame/parity error checking
3. **Software frame builder/decoder** — manually constructs wire-order bit patterns with parity, useful for bit-bang UART or protocol analysis

**Rust code examples:**
1. **`serialport` crate** — cross-platform host serial port with typed config
2. **RP2040 (Raspberry Pi Pico) bare-metal** — `rp-hal` + `embedded-hal` traits, no_std, 8N1 echo loop
3. **Parity & frame logic** — idiomatic Rust with enums, `count_ones()`, and a visual frame diagram printer

**Also includes:** error type table (framing, parity, overrun), and a "Common Pitfalls" section covering the six mistakes every beginner makes.

> **Understanding asynchronous serial communication, start/stop bits, and data framing fundamentals**

---

## Table of Contents

1. [What is UART?](#1-what-is-uart)
2. [Asynchronous Communication Fundamentals](#2-asynchronous-communication-fundamentals)
3. [Physical Layer & Signal Levels](#3-physical-layer--signal-levels)
4. [Data Framing](#4-data-framing)
   - 4.1 [Start Bit](#41-start-bit)
   - 4.2 [Data Bits](#42-data-bits)
   - 4.3 [Parity Bit](#43-parity-bit)
   - 4.4 [Stop Bit(s)](#44-stop-bits)
5. [Baud Rate](#5-baud-rate)
6. [Common UART Configurations](#6-common-uart-configurations)
7. [Programming UART in C/C++](#7-programming-uart-in-cc)
   - 7.1 [Linux termios API](#71-linux-termios-api)
   - 7.2 [Bare-Metal Microcontroller (AVR/STM32-style)](#72-bare-metal-microcontroller-avrstm32-style)
   - 7.3 [Frame Construction and Parity Calculation](#73-frame-construction-and-parity-calculation)
8. [Programming UART in Rust](#8-programming-uart-in-rust)
   - 8.1 [Using the `serialport` crate (Linux/Windows/macOS)](#81-using-the-serialport-crate)
   - 8.2 [Bare-Metal Embedded Rust (RTIC / embassy-style)](#82-bare-metal-embedded-rust)
   - 8.3 [Parity Calculation in Rust](#83-parity-calculation-in-rust)
9. [Error Detection & Common Pitfalls](#9-error-detection--common-pitfalls)
10. [Summary](#10-summary)

---

## 1. What is UART?

**UART** (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems and computing. It defines a method for two devices to exchange data **serially** — one bit at a time — over a single wire in each direction.

Key properties:

| Property | Value |
|---|---|
| Communication type | Asynchronous, full-duplex |
| Number of data wires | 2 (TX and RX) |
| Clock shared? | No — each device uses its own clock |
| Typical voltages | TTL (0/5 V), 3.3 V, RS-232 (±12 V) |
| Common baud rates | 9600, 19200, 38400, 57600, 115200, ... |
| Topology | Point-to-point (1:1) |

UART is used everywhere: Arduino boards, GPS modules, GSM modems, debug consoles, Bluetooth adapters (HC-05), and virtually every microcontroller ever made includes at least one UART peripheral.

---

## 2. Asynchronous Communication Fundamentals

In **synchronous** protocols (SPI, I²C), a shared clock signal tells both sides exactly when to sample each bit. UART has **no shared clock** — it is *asynchronous*. Instead, both sides must agree in advance on the **baud rate** (bits per second). The receiver uses this agreed rate to know when to sample incoming bits.

### How Synchronisation Works Without a Clock

Because there is no external clock, the receiver must synchronise itself to each incoming character individually:

1. The line is **idle high** (logic 1) between transmissions.
2. When the transmitter wants to send a byte, it pulls the line **low** for exactly one bit period. This is the **start bit** — it acts as a trigger for the receiver.
3. The receiver detects the falling edge, waits half a bit period (to sample in the middle of the start bit and confirm it is valid), then samples subsequent bits at full bit-period intervals.
4. After all data bits (and optional parity bit), one or more **stop bits** (logic high) ensure the line returns to idle, giving the receiver time to prepare for the next frame.

```
Idle   Start  D0  D1  D2  D3  D4  D5  D6  D7  Parity Stop  Idle
  ___         ___         ___     ___________________         ____
 |   |_______|   |_______|   |___|                   |_______|
      ^                                                       ^
   Falling edge                                          Stop bit
   triggers receiver
```

This self-synchronising-per-frame approach means UART is simple and robust, but also means the baud rates must match to within approximately **±2–5%** or framing errors will occur.

---

## 3. Physical Layer & Signal Levels

UART defines the *framing protocol*, not the electrical levels. Several physical standards exist:

| Standard | Logic 0 (Space) | Logic 1 (Mark) | Max distance | Notes |
|---|---|---|---|---|
| TTL (5 V) | 0 V | 5 V | Short (<1 m) | Microcontroller-to-microcontroller |
| CMOS (3.3 V) | 0 V | 3.3 V | Short | Modern MCUs, Raspberry Pi GPIO |
| RS-232 | +3 to +15 V | -3 to -15 V | ~15 m | PC COM ports (inverted logic!) |
| RS-485 | Differential | Differential | ~1200 m | Multi-drop, half-duplex |

> ⚠️ **Important:** RS-232 uses **inverted** and **higher voltage** logic. Never connect an RS-232 port directly to an MCU GPIO — use a level-shifter IC such as the MAX232.

### Wiring for Point-to-Point TTL UART

```
  Device A                      Device B
  --------                      --------
    TX  ─────────────────────►  RX
    RX  ◄─────────────────────  TX
   GND  ─────────────────────── GND
```

TX of one device always connects to RX of the other. Ground must always be shared.

---

## 4. Data Framing

A single UART **frame** (also called a *character*) consists of:

```
[ Start | Data bits (5–9) | Parity (optional) | Stop bit(s) ]
```

### 4.1 Start Bit

- Always exactly **1 bit** long.
- Always a **logic 0** (line driven low).
- Signals the beginning of a new frame and re-synchronises the receiver's internal timer.

### 4.2 Data Bits

- Can be **5, 6, 7, or 8 bits** wide (8 is almost universal today).
- Transmitted **LSB first** (Least Significant Bit first) — this is an important and often surprising detail.
- Example: sending `0x41` ('A', binary `0100 0001`):
  - Transmitted bit order: 1, 0, 0, 0, 0, 0, 1, 0 (LSB → MSB)

```
Bit position:  Start  b0  b1  b2  b3  b4  b5  b6  b7  Stop
                 0     1   0   0   0   0   0   1   0    1
                       ↑                           ↑
                      LSB                         MSB
```

### 4.3 Parity Bit

Optional. Provides a single-bit error detection mechanism:

| Mode | Description |
|---|---|
| **None** | No parity bit (most common) |
| **Even** | The parity bit is set so the total number of `1` bits (data + parity) is **even** |
| **Odd** | The parity bit is set so the total number of `1` bits is **odd** |
| **Mark** | Parity bit is always `1` |
| **Space** | Parity bit is always `0` |

Even parity example: `0x41` = `0100 0001` → two `1` bits (already even) → parity bit = **0**.
Odd parity example:  `0x41` = `0100 0001` → two `1` bits → needs to be odd → parity bit = **1**.

Parity can detect single-bit errors but cannot correct them.

### 4.4 Stop Bit(s)

- **1, 1.5, or 2 stop bits** (1 is by far the most common).
- Always **logic 1** (line driven high — idle state).
- Gives the receiver time to process the received byte and prepare for the next frame.
- 2 stop bits are sometimes used at very low baud rates or with slow receivers.

### Complete Frame Timing Diagram

```
         1     1     1     1     1     1     1     1     1     1
bit-     bit   bit   bit   bit   bit   bit   bit   bit   bit   bit
time:    S     D0    D1    D2    D3    D4    D5    D6    D7    P   Stop
         ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
Voltage  │Start│ b0  │ b1  │ b2  │ b3  │ b4  │ b5  │ b6  │ b7  │Par │Stop│ Idle
_________|  0  │     │     │     │     │     │     │     │     │    │ 1  |_____
         └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴────┘
```

---

## 5. Baud Rate

**Baud rate** = number of *symbol changes per second*. In UART, one symbol = one bit, so baud rate equals bits per second (bps).

### Bit Period Calculation

```
Bit period (T) = 1 / Baud rate

At 9600 baud:   T = 1 / 9600  ≈ 104.17 µs
At 115200 baud: T = 1 / 115200 ≈  8.68 µs
```

### Frame Duration

For 8N1 (8 data bits, no parity, 1 stop bit) = 10 bits per frame:

```
At 9600 baud:   10 bits × 104.17 µs = 1.04 ms per byte → ~960 bytes/second
At 115200 baud: 10 bits × 8.68 µs   = 86.8 µs per byte → ~11,520 bytes/second
```

### Baud Rate Error Tolerance

The receiver samples each bit in its centre. Accumulated timing error must not exceed ±50% of a bit period by the last bit sampled (bit 9 for 8N1). In practice, the tolerance is kept to **±2–5%** to provide margin.

```
Max allowable baud rate mismatch = 50% / (N + 1.5) bits ≈ 4.3% for 8N1
```

---

## 6. Common UART Configurations

The configuration is often written as `<data bits><parity><stop bits>`:

| Config | Data | Parity | Stop | Notes |
|---|---|---|---|---|
| **8N1** | 8 | None | 1 | Most common, default for most systems |
| **8E1** | 8 | Even | 1 | Industrial / legacy |
| **8O1** | 8 | Odd | 1 | Industrial / legacy |
| **7E1** | 7 | Even | 1 | Older terminals, ASCII |
| **8N2** | 8 | None | 2 | Slow receivers, some embedded |
| **9N1** | 9 | None | 1 | Multidrop addressing (rare) |

---

## 7. Programming UART in C/C++

### 7.1 Linux termios API

On Linux/macOS, serial ports appear as device files (`/dev/ttyUSB0`, `/dev/ttyS0`, `/dev/ttyAMA0`, etc.). They are configured via the POSIX `termios` API.

```c
/*
 * uart_linux.c
 * UART configuration and basic I/O on Linux using termios
 * Compile: gcc -o uart_linux uart_linux.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

/* ------------------------------------------------------------------ */
/*  Open and configure a serial port                                    */
/*  Returns fd on success, -1 on error                                  */
/* ------------------------------------------------------------------ */
int uart_open(const char *device, int baud_rate)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("uart_open: open");
        return -1;
    }

    /* Make reads blocking */
    fcntl(fd, F_SETFL, 0);

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("uart_open: tcgetattr");
        close(fd);
        return -1;
    }

    /* --- Baud rate --- */
    speed_t speed;
    switch (baud_rate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:
            fprintf(stderr, "uart_open: unsupported baud rate %d\n", baud_rate);
            close(fd);
            return -1;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    /* --- Control flags (8N1) --- */
    options.c_cflag &= ~PARENB;          /* No parity                  */
    options.c_cflag &= ~CSTOPB;          /* 1 stop bit (clear = 1)     */
    options.c_cflag &= ~CSIZE;           /* Clear data size bits        */
    options.c_cflag |=  CS8;             /* 8 data bits                 */
    options.c_cflag &= ~CRTSCTS;         /* No hardware flow control   */
    options.c_cflag |=  CREAD | CLOCAL; /* Enable receiver, local mode */

    /* --- Input flags --- */
    options.c_iflag &= ~(IXON | IXOFF | IXANY); /* No software flow ctrl */
    options.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    /* --- Output flags: raw output --- */
    options.c_oflag &= ~OPOST;

    /* --- Local flags: raw mode (no echo, no signals) --- */
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    /* --- Read timeout/blocking: block until at least 1 byte --- */
    options.c_cc[VMIN]  = 1;   /* Minimum bytes before read() returns */
    options.c_cc[VTIME] = 10;  /* Timeout in tenths of a second       */

    /* Apply settings immediately */
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH); /* Flush any stale data */
    return fd;
}

/* ------------------------------------------------------------------ */
/*  Send a buffer                                                       */
/* ------------------------------------------------------------------ */
int uart_send(int fd, const uint8_t *buf, size_t len)
{
    ssize_t written = write(fd, buf, len);
    if (written < 0) {
        perror("uart_send: write");
        return -1;
    }
    return (int)written;
}

/* ------------------------------------------------------------------ */
/*  Receive up to max_len bytes (blocking with timeout via VTIME)       */
/* ------------------------------------------------------------------ */
int uart_recv(int fd, uint8_t *buf, size_t max_len)
{
    ssize_t n = read(fd, buf, max_len);
    if (n < 0) {
        perror("uart_recv: read");
        return -1;
    }
    return (int)n;
}

/* ------------------------------------------------------------------ */
/*  Configure parity (call after uart_open if non-default needed)       */
/*  parity: 'N'=none, 'E'=even, 'O'=odd                                */
/* ------------------------------------------------------------------ */
int uart_set_parity(int fd, char parity)
{
    struct termios options;
    tcgetattr(fd, &options);

    options.c_cflag &= ~(PARENB | PARODD); /* Clear existing parity */

    switch (parity) {
        case 'N': case 'n':
            /* No parity — already cleared */
            options.c_iflag &= ~INPCK;
            break;
        case 'E': case 'e':
            options.c_cflag |=  PARENB;   /* Enable parity          */
            options.c_cflag &= ~PARODD;   /* Even (cleared = even)  */
            options.c_iflag |=  INPCK;    /* Enable parity checking */
            break;
        case 'O': case 'o':
            options.c_cflag |= PARENB;    /* Enable parity          */
            options.c_cflag |= PARODD;    /* Odd                    */
            options.c_iflag |= INPCK;
            break;
        default:
            fprintf(stderr, "uart_set_parity: unknown parity '%c'\n", parity);
            return -1;
    }

    return tcsetattr(fd, TCSANOW, &options);
}

/* ------------------------------------------------------------------ */
/*  Main demonstration                                                   */
/* ------------------------------------------------------------------ */
int main(void)
{
    const char *port = "/dev/ttyUSB0";
    int fd = uart_open(port, 115200);
    if (fd < 0) return EXIT_FAILURE;

    printf("UART opened: %s @ 115200 8N1\n", port);

    /* Send a test string */
    const char *msg = "Hello UART!\r\n";
    uart_send(fd, (const uint8_t *)msg, strlen(msg));

    /* Echo loop: read a byte and print it */
    uint8_t buf[64];
    printf("Waiting for data...\n");
    for (int i = 0; i < 5; i++) {
        int n = uart_recv(fd, buf, sizeof(buf));
        if (n > 0) {
            buf[n] = '\0';
            printf("Received %d byte(s): %s\n", n, buf);
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### 7.2 Bare-Metal Microcontroller (AVR/STM32-style)

On embedded systems without an OS, UART registers are configured directly. The example below shows a generic approach representative of AVR (ATmega) and ARM Cortex-M (STM32) register patterns.

#### AVR ATmega328P (Arduino Uno hardware layer)

```c
/*
 * uart_avr.c
 * Low-level UART driver for ATmega328P (8 MHz or 16 MHz crystal)
 * Demonstrates start/stop bit framing at register level.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define F_CPU       16000000UL   /* 16 MHz crystal                    */
#define BAUD        9600UL
#define UBRR_VALUE  ((F_CPU / (16UL * BAUD)) - 1)  /* Normal speed   */

/* ------------------------------------------------------------------ */
/*  Initialise USART0                                                   */
/*  Frame format: 8 data bits, no parity, 1 stop bit (8N1)            */
/* ------------------------------------------------------------------ */
void uart_init(void)
{
    /* Set baud rate registers */
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    /*
     * UCSR0C – USART Control and Status Register C
     *   UMSEL01:UMSEL00 = 00 → Asynchronous UART mode
     *   UPM01:UPM00     = 00 → No parity
     *   USBS0           =  0 → 1 stop bit
     *   UCSZ01:UCSZ00   = 11 → 8 data bits (with UCSZ02=0 in UCSR0B)
     */
    UCSR0C = (0 << UMSEL01) | (0 << UMSEL00)  /* Async UART         */
           | (0 << UPM01)   | (0 << UPM00)    /* No parity          */
           | (0 << USBS0)                      /* 1 stop bit         */
           | (1 << UCSZ01)  | (1 << UCSZ00);  /* 8-bit data         */

    /*
     * UCSR0B – Control Register B
     *   RXEN0 = 1 → Enable receiver
     *   TXEN0 = 1 → Enable transmitter
     *   UCSZ02 = 0 → (combined with UCSZ01:00 above = 8-bit)
     */
    UCSR0B = (1 << RXEN0) | (1 << TXEN0);

    /* UCSR0A: use default (no double speed, no multi-processor mode) */
}

/* ------------------------------------------------------------------ */
/*  Transmit one byte (blocking)                                        */
/*  Hardware automatically prepends the START bit and appends STOP bit  */
/* ------------------------------------------------------------------ */
void uart_putc(uint8_t data)
{
    /* Wait until the transmit data register is empty (UDRE0 flag) */
    while (!(UCSR0A & (1 << UDRE0)));
    UDR0 = data;   /* Writing to UDR0 triggers the hardware to frame  */
                   /* and transmit: [START][D0..D7][STOP]              */
}

/* ------------------------------------------------------------------ */
/*  Transmit a null-terminated string                                   */
/* ------------------------------------------------------------------ */
void uart_puts(const char *str)
{
    while (*str) {
        uart_putc((uint8_t)*str++);
    }
}

/* ------------------------------------------------------------------ */
/*  Receive one byte (blocking)                                         */
/*  Hardware strips start and stop bits; only data bits are returned.  */
/* ------------------------------------------------------------------ */
uint8_t uart_getc(void)
{
    /* Wait until receive complete flag is set */
    while (!(UCSR0A & (1 << RXC0)));

    /* Check for frame error (invalid stop bit) or data overrun */
    if (UCSR0A & ((1 << FE0) | (1 << DOR0) | (1 << UPE0))) {
        (void)UDR0; /* Discard corrupted byte */
        return 0xFF; /* Error indicator         */
    }

    return UDR0;  /* Reading UDR0 clears RXC0 flag */
}

/* ------------------------------------------------------------------ */
/*  Enable parity (8E1 or 8O1)                                         */
/* ------------------------------------------------------------------ */
void uart_enable_parity(uint8_t odd)
{
    /* UPM01:UPM00 = 10 → Even parity, 11 → Odd parity */
    if (odd) {
        UCSR0C |= (1 << UPM01) | (1 << UPM00);  /* Odd  */
    } else {
        UCSR0C |= (1 << UPM01);                  /* Even */
        UCSR0C &= ~(1 << UPM00);
    }
}

int main(void)
{
    uart_init();
    uart_puts("ATmega328P UART ready (8N1 @ 9600)\r\n");

    while (1) {
        uint8_t c = uart_getc();
        uart_putc(c);  /* Echo back */
    }
}
```

---

### 7.3 Frame Construction and Parity Calculation

Understanding *how* a UART frame is built is essential for software UART implementations and protocol analysers.

```c
/*
 * uart_frame.c
 * Manually construct and decode UART frames in software.
 * Useful for bit-bang UART or protocol analysis.
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    PARITY_NONE,
    PARITY_EVEN,
    PARITY_ODD
} parity_t;

/* ------------------------------------------------------------------ */
/*  Calculate parity bit for a given data byte                          */
/* ------------------------------------------------------------------ */
uint8_t calc_parity(uint8_t data, parity_t parity)
{
    /* Count number of set bits using Brian Kernighan's algorithm */
    uint8_t count = 0;
    uint8_t tmp = data;
    while (tmp) {
        count += tmp & 1;
        tmp >>= 1;
    }

    switch (parity) {
        case PARITY_EVEN:
            return (count % 2 == 0) ? 0 : 1; /* 0 if already even   */
        case PARITY_ODD:
            return (count % 2 != 0) ? 0 : 1; /* 0 if already odd    */
        default:
            return 0; /* No parity                                    */
    }
}

/* ------------------------------------------------------------------ */
/*  Build a UART frame as a uint16_t bit pattern                        */
/*  Bits are ordered as they appear on the wire (LSB of data first)    */
/*  Returns the frame and sets *frame_bits to total bit count           */
/* ------------------------------------------------------------------ */
uint16_t build_uart_frame(uint8_t data,
                          uint8_t data_bits,   /* 5–8         */
                          parity_t parity,
                          uint8_t stop_bits,   /* 1 or 2      */
                          uint8_t *frame_bits)
{
    uint16_t frame = 0;
    uint8_t  pos   = 0;

    /* Start bit: logic 0 */
    frame |= (0 << pos++);

    /* Data bits: LSB first, mask to data_bits width */
    uint8_t mask = (data_bits < 8) ? ((1 << data_bits) - 1) : 0xFF;
    uint8_t d    = data & mask;
    for (uint8_t i = 0; i < data_bits; i++) {
        frame |= ((d >> i) & 1) << pos++;
    }

    /* Parity bit (optional) */
    if (parity != PARITY_NONE) {
        frame |= calc_parity(d, parity) << pos++;
    }

    /* Stop bit(s): logic 1 */
    for (uint8_t s = 0; s < stop_bits; s++) {
        frame |= (1 << pos++);
    }

    *frame_bits = pos;
    return frame;
}

/* ------------------------------------------------------------------ */
/*  Decode a received UART frame (assumes no framing errors)            */
/* ------------------------------------------------------------------ */
bool decode_uart_frame(uint16_t frame,
                       uint8_t data_bits,
                       parity_t parity,
                       uint8_t stop_bits,
                       uint8_t *out_data,
                       bool    *parity_ok)
{
    uint8_t pos = 0;

    /* Verify start bit is 0 */
    if ((frame >> pos++) & 1) {
        printf("Frame error: start bit is not 0\n");
        return false;
    }

    /* Extract data bits */
    uint8_t data = 0;
    for (uint8_t i = 0; i < data_bits; i++) {
        data |= ((frame >> pos++) & 1) << i;
    }
    *out_data = data;

    /* Check parity */
    *parity_ok = true;
    if (parity != PARITY_NONE) {
        uint8_t recv_parity = (frame >> pos++) & 1;
        uint8_t exp_parity  = calc_parity(data, parity);
        *parity_ok = (recv_parity == exp_parity);
    }

    /* Verify stop bit(s) */
    for (uint8_t s = 0; s < stop_bits; s++) {
        if (!((frame >> pos++) & 1)) {
            printf("Framing error: stop bit %d is not 1\n", s + 1);
            return false;
        }
    }

    return true;
}

/* ------------------------------------------------------------------ */
/*  Print frame bits for visualisation                                  */
/* ------------------------------------------------------------------ */
void print_frame(uint16_t frame, uint8_t bits,
                 uint8_t data_bits, parity_t parity, uint8_t stop_bits)
{
    printf("Wire (LSB first): [");
    for (int i = 0; i < bits; i++) {
        printf("%d", (frame >> i) & 1);
        if (i == 0) printf("|");               /* After start bit     */
        else if (i == data_bits) {
            if (parity != PARITY_NONE) printf("|");
            else printf("|");                  /* Before stop bit(s)  */
        } else if (parity != PARITY_NONE && i == data_bits + 1) printf("|");
    }
    printf("]\n");
    printf("       Labels:    [S|  Data bits  |P|St]\n");
}

int main(void)
{
    uint8_t  data       = 'A';   /* 0x41 = 0100 0001 */
    uint8_t  data_bits  = 8;
    parity_t parity     = PARITY_EVEN;
    uint8_t  stop_bits  = 1;
    uint8_t  frame_bits = 0;

    printf("=== UART Frame Builder ===\n");
    printf("Data: 0x%02X ('%c') | Format: %dE%d\n\n",
           data, data, data_bits, stop_bits);

    uint16_t frame = build_uart_frame(data, data_bits, parity, stop_bits, &frame_bits);
    printf("Built frame (hex): 0x%04X  (%d bits total)\n", frame, frame_bits);
    print_frame(frame, frame_bits, data_bits, parity, stop_bits);

    /* Decode it back */
    uint8_t decoded;
    bool    parity_ok;
    bool    valid = decode_uart_frame(frame, data_bits, parity, stop_bits,
                                      &decoded, &parity_ok);

    printf("\nDecoded: 0x%02X ('%c')  |  Parity OK: %s  |  Frame valid: %s\n",
           decoded, decoded,
           parity_ok ? "yes" : "NO",
           valid     ? "yes" : "NO");

    return 0;
}
```

**Expected output:**
```
=== UART Frame Builder ===
Data: 0x41 ('A') | Format: 8E1

Built frame (hex): 0x02C3  (11 bits total)
Wire (LSB first): [0|10000010|0|1]
       Labels:    [S|  Data bits  |P|St]

Decoded: 0x41 ('A')  |  Parity OK: yes  |  Frame valid: yes
```

---

## 8. Programming UART in Rust

### 8.1 Using the `serialport` crate

The [`serialport`](https://crates.io/crates/serialport) crate provides a cross-platform API for serial ports on Linux, Windows, and macOS.

Add to `Cargo.toml`:
```toml
[dependencies]
serialport = "4"
```

```rust
//! uart_host.rs
//! UART communication on a host OS using the `serialport` crate.
//! Run: cargo run --bin uart_host

use std::io::{self, Read, Write};
use std::time::Duration;
use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};

/// Open and configure a serial port with the given settings.
fn open_uart(
    port_name: &str,
    baud_rate: u32,
    data_bits: DataBits,
    parity: Parity,
    stop_bits: StopBits,
    timeout: Duration,
) -> Result<Box<dyn SerialPort>, serialport::Error> {
    let port = serialport::new(port_name, baud_rate)
        .data_bits(data_bits)
        .parity(parity)
        .stop_bits(stop_bits)
        .flow_control(FlowControl::None)
        .timeout(timeout)
        .open()?;

    println!(
        "Opened {} @ {} baud ({:?}, {:?}, {:?})",
        port_name, baud_rate, data_bits, parity, stop_bits
    );
    Ok(port)
}

/// Send a byte slice, returns number of bytes written.
fn uart_send(port: &mut dyn SerialPort, data: &[u8]) -> io::Result<usize> {
    let n = port.write(data)?;
    port.flush()?;
    Ok(n)
}

/// Receive up to `max_len` bytes with a single read call.
fn uart_recv(port: &mut dyn SerialPort, max_len: usize) -> io::Result<Vec<u8>> {
    let mut buf = vec![0u8; max_len];
    match port.read(&mut buf) {
        Ok(n) => {
            buf.truncate(n);
            Ok(buf)
        }
        Err(ref e) if e.kind() == io::ErrorKind::TimedOut => Ok(vec![]),
        Err(e) => Err(e),
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // List available ports
    let ports = serialport::available_ports()?;
    println!("Available ports:");
    for p in &ports {
        println!("  {}", p.port_name);
    }

    let port_name = "/dev/ttyUSB0"; // Change as needed

    let mut port = open_uart(
        port_name,
        115_200,
        DataBits::Eight,
        Parity::None,         // 8N1
        StopBits::One,
        Duration::from_millis(1000),
    )?;

    // Send a test message
    let msg = b"Hello from Rust UART!\r\n";
    let sent = uart_send(port.as_mut(), msg)?;
    println!("Sent {} bytes: {:?}", sent, String::from_utf8_lossy(msg));

    // Echo loop
    println!("Listening for 5 seconds...");
    let deadline = std::time::Instant::now() + Duration::from_secs(5);
    while std::time::Instant::now() < deadline {
        match uart_recv(port.as_mut(), 256)? {
            bytes if bytes.is_empty() => {} // Timeout, try again
            bytes => {
                print!("RX: {:?}", String::from_utf8_lossy(&bytes));
                // Echo back
                uart_send(port.as_mut(), &bytes)?;
            }
        }
    }

    Ok(())
}
```

---

### 8.2 Bare-Metal Embedded Rust

For embedded targets, the [`embedded-hal`](https://crates.io/crates/embedded-hal) trait abstracts UART hardware. Below shows a real implementation for the RP2040 (Raspberry Pi Pico) using the `rp-hal` crate.

```toml
# Cargo.toml (excerpt for RP2040 / Raspberry Pi Pico)
[dependencies]
rp2040-hal  = { version = "0.10", features = ["rt"] }
embedded-hal = "0.2"
nb           = "1"
cortex-m-rt  = "0.7"
panic-halt   = "0.2"
```

```rust
//! uart_embedded.rs
//! Bare-metal UART on Raspberry Pi Pico (RP2040)
//! Demonstrates 8N1 configuration at register level via rp-hal.

#![no_std]
#![no_main]

use panic_halt as _;
use rp2040_hal::{
    self as hal,
    clocks::{init_clocks_and_plls, Clock},
    pac,
    uart::{DataBits, StopBits, UartConfig, UartPeripheral},
    watchdog::Watchdog,
    Sio,
};
use embedded_hal::serial::{Read, Write};
use nb::block;
use cortex_m_rt::entry;

// External 12 MHz crystal on Pico board
const XOSC_CRYSTAL_FREQ: u32 = 12_000_000;

#[entry]
fn main() -> ! {
    // Grab peripheral singletons
    let mut pac    = pac::Peripherals::take().unwrap();
    let core       = pac::CorePeripherals::take().unwrap();
    let mut watchdog = Watchdog::new(pac.WATCHDOG);
    let sio        = Sio::new(pac.SIO);

    // Initialise clocks (system clock = 125 MHz)
    let clocks = init_clocks_and_plls(
        XOSC_CRYSTAL_FREQ,
        pac.XOSC,
        pac.CLOCKS,
        pac.PLL_SYS,
        pac.PLL_USB,
        &mut pac.RESETS,
        &mut watchdog,
    )
    .ok()
    .unwrap();

    let mut delay = cortex_m::delay::Delay::new(
        core.SYST,
        clocks.system_clock.freq().to_Hz(),
    );

    // Configure GPIO pins for UART0 (GP0 = TX, GP1 = RX on Pico)
    let pins = hal::gpio::Pins::new(
        pac.IO_BANK0,
        pac.PADS_BANK0,
        sio.gpio_bank0,
        &mut pac.RESETS,
    );
    let uart_tx = pins.gpio0.into_function::<hal::gpio::FunctionUart>();
    let uart_rx = pins.gpio1.into_function::<hal::gpio::FunctionUart>();

    // Configure UART0:
    //   - Baud rate : 115200
    //   - Data bits : 8
    //   - Stop bits : 1
    //   - Parity    : None
    // This maps exactly to the 8N1 UART frame format.
    let uart = UartPeripheral::new(
        pac.UART0,
        (uart_tx, uart_rx),
        &mut pac.RESETS,
    )
    .enable(
        UartConfig::new(
            115_200_u32.Hz(),
            DataBits::Eight,
            None,          // Parity::None
            StopBits::One,
        ),
        clocks.peripheral_clock.freq(),
    )
    .unwrap();

    // Transmit a greeting
    // Hardware automatically applies start/stop bits around each byte
    uart.write_full_blocking(b"RP2040 UART ready (8N1 @ 115200)\r\n");

    // Echo loop: read a byte, echo it back
    loop {
        match block!(uart.read()) {
            Ok(byte) => {
                // Optional: simple processing
                let echo = if byte == b'\r' { b'\n' } else { byte };
                let _ = block!(uart.write(echo));
            }
            Err(_) => {
                // Framing or parity error — send error indicator
                let _ = block!(uart.write(b'?'));
            }
        }
    }
}
```

---

### 8.3 Parity Calculation in Rust

```rust
//! uart_frame.rs
//! UART frame construction, parity calculation, and decoding in Rust.
//! Run: cargo run --bin uart_frame

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Parity {
    None,
    Even,
    Odd,
}

/// Count the number of set bits (popcount) in a byte.
fn popcount(mut byte: u8) -> u8 {
    byte.count_ones() as u8  // Rust's built-in popcount
}

/// Calculate the parity bit for the given data byte and parity mode.
pub fn calc_parity(data: u8, parity: Parity) -> u8 {
    let ones = popcount(data);
    match parity {
        Parity::None => 0,
        Parity::Even => (ones % 2 != 0) as u8,  // Set bit to make total even
        Parity::Odd  => (ones % 2 == 0) as u8,  // Set bit to make total odd
    }
}

/// Represents a decoded UART frame.
#[derive(Debug)]
pub struct UartFrame {
    pub data: u8,
    pub data_bits: u8,
    pub parity: Parity,
    pub stop_bits: u8,
    pub parity_ok: bool,
    pub frame_valid: bool,
}

/// Build a UART frame as a u16 bit pattern (wire order: LSB first).
pub fn build_frame(
    data: u8,
    data_bits: u8,
    parity: Parity,
    stop_bits: u8,
) -> (u16, u8) {
    assert!((5..=8).contains(&data_bits), "data_bits must be 5–8");
    assert!((1..=2).contains(&stop_bits), "stop_bits must be 1 or 2");

    let mut frame: u16 = 0;
    let mut pos: u8 = 0;

    // Start bit: logic 0
    // (bit is already 0, so we just advance position)
    pos += 1;

    // Data bits: LSB first
    let mask: u8 = if data_bits < 8 { (1 << data_bits) - 1 } else { 0xFF };
    let d = data & mask;
    for i in 0..data_bits {
        frame |= (((d >> i) & 1) as u16) << pos;
        pos += 1;
    }

    // Parity bit
    if parity != Parity::None {
        frame |= (calc_parity(d, parity) as u16) << pos;
        pos += 1;
    }

    // Stop bit(s): logic 1
    for _ in 0..stop_bits {
        frame |= 1 << pos;
        pos += 1;
    }

    (frame, pos)
}

/// Decode a UART frame from a u16 bit pattern.
pub fn decode_frame(
    frame: u16,
    data_bits: u8,
    parity: Parity,
    stop_bits: u8,
) -> UartFrame {
    let mut pos: u8 = 0;

    // Verify start bit
    let start_ok = ((frame >> pos) & 1) == 0;
    pos += 1;

    // Extract data bits
    let mut data: u8 = 0;
    for i in 0..data_bits {
        data |= (((frame >> pos) & 1) as u8) << i;
        pos += 1;
    }

    // Check parity
    let parity_ok = if parity != Parity::None {
        let recv = ((frame >> pos) & 1) as u8;
        pos += 1;
        recv == calc_parity(data, parity)
    } else {
        true
    };

    // Verify stop bit(s)
    let mut stop_ok = true;
    for _ in 0..stop_bits {
        if ((frame >> pos) & 1) == 0 {
            stop_ok = false;
        }
        pos += 1;
    }

    UartFrame {
        data,
        data_bits,
        parity,
        stop_bits,
        parity_ok,
        frame_valid: start_ok && stop_ok,
    }
}

/// Visualise the frame bits with labels.
pub fn print_frame_diagram(frame: u16, total_bits: u8, data_bits: u8, parity: Parity) {
    print!("Wire (LSB→MSB): [");
    for i in 0..total_bits {
        let bit = (frame >> i) & 1;
        if i == 0 {
            print!("S={} | ", bit);
        } else if i <= data_bits {
            print!("D{}={}", i - 1, bit);
            if i < data_bits { print!(" "); }
        } else if parity != Parity::None && i == data_bits + 1 {
            print!(" | P={}", bit);
        } else {
            print!(" | ST={}", bit);
        }
    }
    println!("]");
}

fn main() {
    let data      = b'A'; // 0x41
    let data_bits = 8u8;
    let parity    = Parity::Even;
    let stop_bits = 1u8;

    println!("=== Rust UART Frame Demo ===");
    println!("Encoding: 0x{:02X} ('{}') with {:?} parity", data, data as char, parity);

    let (frame, total_bits) = build_frame(data, data_bits, parity, stop_bits);
    println!("Frame bits: {total_bits} | Hex: 0x{frame:04X} | Binary: {frame:011b}");
    print_frame_diagram(frame, total_bits, data_bits, parity);

    let decoded = decode_frame(frame, data_bits, parity, stop_bits);
    println!("\nDecoded frame: {decoded:#?}");

    // Test with a bit error
    let corrupted = frame ^ (1 << 3); // Flip bit 3 (data bit 2)
    println!("\n--- Simulating single-bit error (bit 3 flipped) ---");
    let decoded_err = decode_frame(corrupted, data_bits, parity, stop_bits);
    println!("Parity OK: {} (expected: false)", decoded_err.parity_ok);
}
```

---

## 9. Error Detection & Common Pitfalls

### Error Types

| Error | Cause | How Detected |
|---|---|---|
| **Framing Error** | Stop bit received as logic 0 | Hardware FE flag; `FE0` in AVR |
| **Parity Error** | Parity bit doesn't match data | Hardware PE flag; `UPE0` in AVR |
| **Overrun Error** | New byte arrives before previous read | Hardware OE/DOR flag |
| **Break Condition** | Line held low for > 1 frame period | Used to signal reset/attention |

### Common Pitfalls and Solutions

**1. Mismatched baud rates**
Symptom: garbled output (`????` or `0xFF` bytes).
Fix: Verify both sides agree exactly. Measure with an oscilloscope: bit period at 9600 baud should be exactly 104.17 µs.

**2. Mismatched voltage levels**
Symptom: no communication, or damaged pins.
Fix: Use a level shifter when connecting 5 V and 3.3 V devices. Never connect RS-232 directly to GPIO.

**3. TX/RX crossed (or not crossed)**
Symptom: no communication.
Fix: TX of device A → RX of device B, and vice versa. Always cross over. Many beginners forget this.

**4. Missing ground connection**
Symptom: random noise, garbled data.
Fix: Always share a common ground between both UART devices.

**5. Floating RX pin**
Symptom: spurious bytes received when idle.
Fix: The idle state should be logic HIGH. If TX is disconnected, pull RX high with a 10 kΩ resistor.

**6. Incorrect data bit order assumptions**
Symptom: bytes received appear bit-reversed.
Fix: Remember UART sends **LSB first**. If receiving `0x82` when sending `0x41`, you have byte-reversal somewhere in software.

---

## 10. Summary

| Topic | Key Point |
|---|---|
| **Asynchronous** | No shared clock; synchronisation per frame via start bit |
| **Start bit** | Logic 0; triggers receiver's internal timer |
| **Data bits** | 5–8 bits; transmitted **LSB first** |
| **Parity bit** | Optional; Even/Odd/None; detects single-bit errors |
| **Stop bit(s)** | Logic 1; returns line to idle; 1 is standard |
| **Baud rate** | Must match within ±2–5%; determines bit period |
| **8N1** | The universal default: 8 data bits, no parity, 1 stop bit |
| **Physical layers** | TTL/CMOS for short runs; RS-232 for PC COM ports; RS-485 for long runs |
| **C/Linux** | `termios` API: `tcgetattr` / `tcsetattr` / `cfsetispeed` |
| **C/Embedded** | Directly configure UCSR (AVR) or USART_CR (STM32) registers |
| **Rust/Host** | `serialport` crate: cross-platform, type-safe |
| **Rust/Embedded** | `embedded-hal` traits + HAL crates (rp-hal, stm32f4xx-hal, etc.) |
| **Error types** | Framing, parity, overrun — check hardware flags after every read |

UART remains indispensable because of its extreme simplicity: two wires, an agreed baud rate, and a well-defined 10–12 bit frame. Master these fundamentals and every higher-level serial protocol (Modbus, GPS NMEA, AT commands, MIDI) becomes straightforward to understand and implement.

---

*Next: [02. Baud Rate Configuration & Clocking](02_Baud_Rate_Configuration.md)*

---

# UART Protocol Basics: A Comprehensive Guide

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is a fundamental hardware communication protocol used for asynchronous serial data transmission. Unlike TCP/IP which operates at higher network layers, UART is a physical layer protocol commonly used in embedded systems, microcontrollers, and device-to-device communication. While not directly part of the TCP/IP stack, UART often serves as the physical transport layer for protocols that eventually connect to TCP/IP networks (such as PPP over serial or AT command modems).

## Core Concepts

### Asynchronous Communication

UART is **asynchronous**, meaning it doesn't use a shared clock signal between transmitter and receiver. Instead, both devices must agree on a communication speed (baud rate) beforehand. This is fundamentally different from synchronous protocols like SPI or I2C that use clock lines.

**Key characteristics:**
- No clock signal required
- Independent timing on each device
- Requires precise baud rate matching (typically ±2% tolerance)
- Point-to-point communication (one transmitter to one receiver)

### Data Framing

A UART data frame consists of several components:

1. **Idle State**: Line is held HIGH when no data is being transmitted
2. **Start Bit**: A single LOW bit that signals the beginning of a frame
3. **Data Bits**: 5-9 bits of actual data (most commonly 8 bits)
4. **Parity Bit** (optional): For basic error detection
5. **Stop Bits**: 1, 1.5, or 2 HIGH bits marking the end of transmission

```
Idle | Start | D0 | D1 | D2 | D3 | D4 | D5 | D6 | D7 | Parity | Stop | Idle
HIGH | LOW   | data bits (LSB first)      | opt.  | HIGH | HIGH
```

### Common UART Parameters

- **Baud Rate**: 9600, 19200, 38400, 57600, 115200, 230400, etc.
- **Data Bits**: 7 or 8 (8 is standard)
- **Parity**: None, Even, Odd, Mark, Space
- **Stop Bits**: 1 or 2
- **Flow Control**: None, Hardware (RTS/CTS), Software (XON/XOFF)

A common configuration is "8N1" meaning 8 data bits, No parity, 1 stop bit.

---

## Programming UART in C/C++

### Linux/POSIX Example

On Linux systems, UART devices appear as `/dev/ttyS*`, `/dev/ttyUSB*`, or `/dev/ttyAMA*`. Here's a complete example:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

int uart_init(const char *device, int baudrate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("Error opening serial port");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    // Get current settings
    if (tcgetattr(fd, &tty) != 0) {
        perror("Error from tcgetattr");
        close(fd);
        return -1;
    }

    // Set baud rate
    speed_t speed;
    switch(baudrate) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:     speed = B9600;   break;
    }
    
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    // 8N1 mode
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;  // 8 data bits
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CRTSCTS;  // No hardware flow control
    tty.c_cflag |= (CLOCAL | CREAD);  // Enable receiver, ignore modem controls

    // Raw input mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  // No software flow control
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // Raw output mode
    tty.c_oflag &= ~OPOST;

    // Timeout settings (0.5 seconds)
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 5;

    // Apply settings
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("Error from tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

int uart_write(int fd, const char *data, size_t len) {
    int bytes_written = write(fd, data, len);
    if (bytes_written < 0) {
        perror("Error writing to UART");
        return -1;
    }
    return bytes_written;
}

int uart_read(int fd, char *buffer, size_t max_len) {
    int bytes_read = read(fd, buffer, max_len);
    if (bytes_read < 0) {
        perror("Error reading from UART");
        return -1;
    }
    return bytes_read;
}

int main() {
    int uart_fd = uart_init("/dev/ttyUSB0", 115200);
    if (uart_fd < 0) {
        return 1;
    }

    // Send data
    const char *message = "Hello UART!\n";
    uart_write(uart_fd, message, strlen(message));

    // Read response
    char buffer[256];
    int bytes = uart_read(uart_fd, buffer, sizeof(buffer) - 1);
    if (bytes > 0) {
        buffer[bytes] = '\0';
        printf("Received: %s\n", buffer);
    }

    close(uart_fd);
    return 0;
}
```

### Windows Example (C++)

```cpp
#include <windows.h>
#include <iostream>
#include <string>

class UARTPort {
private:
    HANDLE hSerial;
    
public:
    UARTPort(const std::string& portName, DWORD baudRate) {
        // Open COM port
        hSerial = CreateFileA(portName.c_str(),
                             GENERIC_READ | GENERIC_WRITE,
                             0, NULL, OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL, NULL);
        
        if (hSerial == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("Failed to open COM port");
        }
        
        // Configure port
        DCB dcbSerialParams = {0};
        dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
        
        if (!GetCommState(hSerial, &dcbSerialParams)) {
            CloseHandle(hSerial);
            throw std::runtime_error("Failed to get COM state");
        }
        
        dcbSerialParams.BaudRate = baudRate;
        dcbSerialParams.ByteSize = 8;
        dcbSerialParams.StopBits = ONESTOPBIT;
        dcbSerialParams.Parity = NOPARITY;
        
        if (!SetCommState(hSerial, &dcbSerialParams)) {
            CloseHandle(hSerial);
            throw std::runtime_error("Failed to set COM state");
        }
        
        // Set timeouts
        COMMTIMEOUTS timeouts = {0};
        timeouts.ReadIntervalTimeout = 50;
        timeouts.ReadTotalTimeoutConstant = 50;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.WriteTotalTimeoutConstant = 50;
        timeouts.WriteTotalTimeoutMultiplier = 10;
        
        SetCommTimeouts(hSerial, &timeouts);
    }
    
    ~UARTPort() {
        if (hSerial != INVALID_HANDLE_VALUE) {
            CloseHandle(hSerial);
        }
    }
    
    bool write(const std::string& data) {
        DWORD bytesWritten;
        return WriteFile(hSerial, data.c_str(), data.size(), 
                        &bytesWritten, NULL);
    }
    
    std::string read(size_t maxBytes = 256) {
        char buffer[256];
        DWORD bytesRead;
        
        if (ReadFile(hSerial, buffer, maxBytes, &bytesRead, NULL)) {
            return std::string(buffer, bytesRead);
        }
        return "";
    }
};

int main() {
    try {
        UARTPort uart("COM3", CBR_115200);
        uart.write("Hello from Windows!\n");
        
        std::string response = uart.read();
        std::cout << "Received: " << response << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
    }
    return 0;
}
```

---

## Programming UART in Rust

### Cross-Platform Example using `serialport` crate

Add to `Cargo.toml`:
```toml
[dependencies]
serialport = "4.2"
```

```rust
use serialport::{SerialPort, SerialPortBuilder};
use std::time::Duration;
use std::io::{Read, Write};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open serial port
    let mut port = serialport::new("/dev/ttyUSB0", 115200)
        .timeout(Duration::from_millis(500))
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .open()?;

    println!("Opened {} at 115200 baud", port.name().unwrap_or("unknown"));

    // Write data
    let message = b"Hello from Rust!\n";
    port.write_all(message)?;
    port.flush()?;
    println!("Sent: {}", String::from_utf8_lossy(message));

    // Read response
    let mut buffer = [0u8; 256];
    match port.read(&mut buffer) {
        Ok(bytes_read) => {
            let response = String::from_utf8_lossy(&buffer[..bytes_read]);
            println!("Received {} bytes: {}", bytes_read, response);
        }
        Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {
            println!("Read timeout - no data received");
        }
        Err(e) => return Err(e.into()),
    }

    Ok(())
}
```

### Advanced Example with Error Handling and Framing

```rust
use serialport::{SerialPort, SerialPortBuilder};
use std::time::Duration;
use std::io::{Read, Write, Result as IoResult};

pub struct UartConnection {
    port: Box<dyn SerialPort>,
}

impl UartConnection {
    pub fn new(port_name: &str, baud_rate: u32) -> IoResult<Self> {
        let port = serialport::new(port_name, baud_rate)
            .timeout(Duration::from_millis(1000))
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .open()?;

        Ok(UartConnection { port })
    }

    pub fn send_frame(&mut self, data: &[u8]) -> IoResult<()> {
        // Simple framing: [START_BYTE][LENGTH][DATA][CHECKSUM]
        const START_BYTE: u8 = 0xAA;
        
        let mut frame = Vec::new();
        frame.push(START_BYTE);
        frame.push(data.len() as u8);
        frame.extend_from_slice(data);
        
        // Simple checksum (sum of all bytes)
        let checksum: u8 = data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
        frame.push(checksum);
        
        self.port.write_all(&frame)?;
        self.port.flush()?;
        
        Ok(())
    }

    pub fn receive_frame(&mut self) -> IoResult<Vec<u8>> {
        const START_BYTE: u8 = 0xAA;
        let mut byte = [0u8; 1];
        
        // Wait for start byte
        loop {
            self.port.read_exact(&mut byte)?;
            if byte[0] == START_BYTE {
                break;
            }
        }
        
        // Read length
        self.port.read_exact(&mut byte)?;
        let length = byte[0] as usize;
        
        // Read data
        let mut data = vec![0u8; length];
        self.port.read_exact(&mut data)?;
        
        // Read and verify checksum
        self.port.read_exact(&mut byte)?;
        let received_checksum = byte[0];
        let calculated_checksum: u8 = data.iter().fold(0u8, |acc, &x| acc.wrapping_add(x));
        
        if received_checksum != calculated_checksum {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                "Checksum mismatch"
            ));
        }
        
        Ok(data)
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut uart = UartConnection::new("/dev/ttyUSB0", 115200)?;
    
    // Send a frame
    let message = b"Hello with framing!";
    uart.send_frame(message)?;
    println!("Sent frame with {} bytes", message.len());
    
    // Receive a frame
    match uart.receive_frame() {
        Ok(data) => {
            println!("Received frame: {}", String::from_utf8_lossy(&data));
        }
        Err(e) => {
            eprintln!("Error receiving frame: {}", e);
        }
    }
    
    Ok(())
}
```

---

## Summary

**UART Protocol Basics** covers the fundamentals of asynchronous serial communication, which remains crucial in embedded systems and hardware interfacing despite the prevalence of higher-level protocols like TCP/IP.

**Key Takeaways:**

1. **Asynchronous Design**: UART operates without a shared clock, requiring precise baud rate agreement between devices with typical tolerance of ±2%.

2. **Frame Structure**: Data transmission uses a defined frame with start bit (LOW), data bits (5-9, commonly 8), optional parity bit, and stop bits (HIGH), with the line idle at HIGH.

3. **Configuration**: The most common setup is "8N1" (8 data bits, no parity, 1 stop bit) at standard baud rates like 9600, 115200, or higher.

4. **Implementation Complexity**: While conceptually simple, proper UART implementation requires careful handling of timing, buffering, error detection, and platform-specific APIs.

5. **Cross-Platform Programming**: Modern languages like Rust with the `serialport` crate provide excellent cross-platform abstractions, while C/C++ requires platform-specific code (POSIX termios on Linux, Windows API on Windows).

6. **Error Handling**: Real-world applications need robust error detection through parity bits, checksums, or more sophisticated framing protocols built on top of basic UART.

UART serves as a foundation for many communication scenarios, from direct microcontroller interfacing to serving as the physical layer for protocols that eventually connect to TCP/IP networks, making it an essential protocol to understand for systems programming and embedded development.

