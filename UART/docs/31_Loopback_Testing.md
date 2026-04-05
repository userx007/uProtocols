# 31. UART Loopback Testing

**Concepts covered:**
- What UART loopback is and how TX→RX routing works in both modes
- Internal loopback — register-level details (MCR bit 4 on 16550), what it tests and its limits
- External loopback — physical wiring options, virtual pairs via `socat` for CI
- A comparison table of both modes across 8 feature dimensions

**C/C++ code examples:**
- **Linux termios / internal loopback** — full `TIOCM_LOOP` ioctl flow with timeout-guarded receive
- **Linux termios / external loopback** — structured multi-pattern test runner (incrementing, zeros, 0xFF, alternating)
- **Bare-metal register-level driver** — 16550 MMIO UART init, polled TX/RX, and a full 0x00–0xFF sweep
- **Windows COMM API** — `CreateFile`, `DCB`, `COMMTIMEOUTS`, write/read/compare

**Rust code examples:**
- **`serialport` crate / internal loopback** — `TIOCM_LOOP` via `nix` ioctl, pattern comparison
- **Loopback validation framework** — reusable `LoopbackRunner` + `standard_suite()` with 6 named test cases including FIFO stress
- **`embedded-hal` / `no_std`** — `loopback_byte()` and `loopback_sweep()` for bare-metal Cortex-M targets

**Additional material:**
- LFSR pseudo-random pattern generator in C
- Diagnostic error enum with a root-cause table (framing, overrun, parity, timeout, mismatch)

## Internal and External Loopback Modes for Driver Validation

---

## Table of Contents

1. [Introduction](#introduction)
2. [What is UART Loopback?](#what-is-uart-loopback)
3. [Internal Loopback Mode](#internal-loopback-mode)
4. [External Loopback Mode](#external-loopback-mode)
5. [Hardware Considerations](#hardware-considerations)
6. [Programming in C/C++](#programming-in-cc)
   - [Linux termios API – Internal Loopback](#linux-termios-api--internal-loopback)
   - [Linux termios API – External Loopback](#linux-termios-api--external-loopback)
   - [Register-Level Driver (Bare-Metal / Embedded C)](#register-level-driver-bare-metal--embedded-c)
   - [Windows API Loopback Test](#windows-api-loopback-test)
7. [Programming in Rust](#programming-in-rust)
   - [Rust – Internal Loopback with serialport crate](#rust--internal-loopback-with-serialport-crate)
   - [Rust – Loopback Validation Framework](#rust--loopback-validation-framework)
   - [Rust – Bare-Metal / Embedded (no_std)](#rust--bare-metal--embedded-no_std)
8. [Test Patterns and Validation Strategies](#test-patterns-and-validation-strategies)
9. [Error Detection and Diagnostics](#error-detection-and-diagnostics)
10. [Summary](#summary)

---

## Introduction

Loopback testing is a fundamental diagnostic and validation technique for UART (Universal Asynchronous Receiver/Transmitter) communication. It allows a developer or driver to verify that the transmit and receive paths are functioning correctly by routing transmitted data back to the receiver — either within the UART hardware itself (internal loopback) or via a physical wire or connector (external loopback).

Loopback tests are widely used in:

- **Driver development** – verifying that a newly written UART driver correctly initializes hardware and handles data flow.
- **Hardware bring-up** – confirming that the UART peripheral and its physical connections are intact on a new PCB.
- **Manufacturing test** – automated go/no-go testing of boards coming off a production line.
- **Regression testing** – integration tests run in CI/CD pipelines using virtual serial ports or hardware-in-the-loop setups.

---

## What is UART Loopback?

In normal UART operation, the TX (transmit) line of one device is wired to the RX (receive) line of another device. In loopback mode, TX is connected back to RX on the **same device**, so every byte sent is immediately received by the same port.

```
Normal operation:
  Device A TX ──────────────► Device B RX
  Device A RX ◄────────────── Device B TX

Internal loopback (inside the UART silicon):
  TX ──► [internal loopback path] ──► RX
  (no external pin activity)

External loopback (physical wire or connector):
  TX pin ──[wire]──► RX pin
```

Both modes test the UART's ability to frame, transmit, and receive bytes. External loopback additionally exercises the physical drivers (line drivers, level shifters, transceivers) and the PCB traces.

---

## Internal Loopback Mode

Internal loopback is implemented entirely within the UART IP block. When enabled via a control register bit, the TX data path is fed back into the RX data path without ever appearing on the physical pins.

**Advantages:**
- No hardware changes required.
- Safe to run even if no cable is connected.
- Tests the digital core of the UART: baud rate generator, shift registers, FIFO logic, interrupt generation.

**Limitations:**
- Does not test line drivers, transceivers, PCB traces, or connectors.
- The exact register to enable internal loopback varies by UART IP (16550, PL011, STM32 USARTs, etc.).

### Typical register usage (16550-compatible UART)

The 16550 MCR (Modem Control Register) at offset `0x04` has bit 4 (`LOOP`) to enable internal loopback:

```
MCR[4] = 1  → Enable internal loopback
MCR[4] = 0  → Normal operation
```

When loopback is active on a 16550, the modem status inputs (CTS, DSR, RI, DCD) are driven by the modem control outputs (RTS, DTR, OUT1, OUT2) internally as well, allowing full modem-signal loopback testing.

---

## External Loopback Mode

External loopback connects the TX output pin to the RX input pin via a physical means:

- A **jumper wire** directly on the connector.
- A **loopback plug** (a small PCB or connector with TX–RX shorted).
- An **RS-232 loopback adapter** (which also loops CTS←RTS, DSR←DTR, etc.).
- A **cable** where pin 2 and pin 3 are crossed (DB9 null-modem without cross-coupling).

The UART is configured in its **normal operating mode** — no special loopback register bit is set. The driver simply transmits data and expects to receive the same data back, because the TX line is physically wired to RX.

**Advantages:**
- Tests the entire signal chain including transceivers and physical wiring.
- Validates modem control lines when a full loopback adapter is used.
- Closely approximates real operating conditions.

**Limitations:**
- Requires physical access to the port.
- Cannot run in software-only CI without a hardware fixture or virtual serial port pair.

---

## Hardware Considerations

| Feature | Internal Loopback | External Loopback |
|---|---|---|
| Requires physical connector | No | Yes |
| Tests line driver / transceiver | No | Yes |
| Tests PCB traces | No | Yes |
| Tests baud rate generator | Yes | Yes |
| Tests FIFO logic | Yes | Yes |
| Tests interrupt handling | Yes | Yes |
| Tests modem signals (if wired) | Yes (in 16550) | Yes (with full adapter) |
| Suitable for software-only CI | Yes | Only with virtual pairs (e.g. `socat`) |

**Creating virtual loopback pairs on Linux for CI:**

```bash
# Create a virtual serial port pair linked together
socat PTY,link=/tmp/ttyS_TX,raw,echo=0 PTY,link=/tmp/ttyS_RX,raw,echo=0 &

# Or use the kernel null-modem emulator (modprobe tty0tty)
```

The two pseudo-terminals `/tmp/ttyS_TX` and `/tmp/ttyS_RX` behave like two ends of a null-modem cable, enabling external loopback simulation in CI without hardware.

---

## Programming in C/C++

### Linux termios API – Internal Loopback

On Linux, internal loopback is enabled through the UART's device-specific `ioctl`. For standard 16550 UARTs exposed via `/dev/ttyS*`, the MCR can be accessed with the `TIOCMSET` ioctl combined with the `TIOCM_LOOP` flag (where supported by the driver), or directly via a memory-mapped register in a custom driver.

The example below uses the `TIOCM_LOOP` constant where available, with a fallback note for embedded drivers.

```c
/*
 * uart_internal_loopback.c
 *
 * Demonstrates enabling internal loopback on a 16550-compatible UART
 * under Linux, sending a test pattern, and verifying the received data.
 *
 * Compile: gcc -o uart_loopback uart_internal_loopback.c
 * Run:     sudo ./uart_loopback /dev/ttyS0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <time.h>

#define BAUD_RATE   B115200
#define TEST_LEN    256
#define TIMEOUT_SEC 2

/* Build a simple incrementing test pattern */
static void build_test_pattern(uint8_t *buf, size_t len)
{
    for (size_t i = 0; i < len; i++)
        buf[i] = (uint8_t)(i & 0xFF);
}

/* Configure the serial port with termios */
static int configure_port(int fd)
{
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    cfmakeraw(&tty);                    /* Raw mode: no echo, no special chars */
    cfsetispeed(&tty, BAUD_RATE);
    cfsetospeed(&tty, BAUD_RATE);

    tty.c_cflag |=  (CLOCAL | CREAD);  /* Ignore modem controls, enable read  */
    tty.c_cflag &= ~CRTSCTS;            /* No hardware flow control            */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;                /* 8-bit characters                    */
    tty.c_cflag &= ~PARENB;             /* No parity                           */
    tty.c_cflag &= ~CSTOPB;             /* 1 stop bit                          */

    tty.c_cc[VMIN]  = 0;               /* Non-blocking read                   */
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

/* Enable internal loopback via TIOCM_LOOP ioctl */
static int enable_internal_loopback(int fd)
{
    int modem_bits;

    if (ioctl(fd, TIOCMGET, &modem_bits) < 0) {
        perror("TIOCMGET");
        return -1;
    }

#ifdef TIOCM_LOOP
    modem_bits |= TIOCM_LOOP;
    if (ioctl(fd, TIOCMSET, &modem_bits) < 0) {
        perror("TIOCMSET (TIOCM_LOOP)");
        return -1;
    }
    printf("Internal loopback enabled via TIOCM_LOOP.\n");
#else
    fprintf(stderr, "TIOCM_LOOP not defined on this platform.\n");
    fprintf(stderr, "Set MCR bit 4 directly for your UART.\n");
    return -1;
#endif

    return 0;
}

/* Disable internal loopback */
static int disable_internal_loopback(int fd)
{
#ifdef TIOCM_LOOP
    int modem_bits;
    if (ioctl(fd, TIOCMGET, &modem_bits) < 0) return -1;
    modem_bits &= ~TIOCM_LOOP;
    if (ioctl(fd, TIOCMSET, &modem_bits) < 0) return -1;
#endif
    return 0;
}

/* Receive exactly `len` bytes with a timeout */
static ssize_t recv_exact(int fd, uint8_t *buf, size_t len, int timeout_sec)
{
    size_t received = 0;
    fd_set read_fds;
    struct timeval tv;

    while (received < len) {
        FD_ZERO(&read_fds);
        FD_SET(fd, &read_fds);
        tv.tv_sec  = timeout_sec;
        tv.tv_usec = 0;

        int ret = select(fd + 1, &read_fds, NULL, NULL, &tv);
        if (ret < 0)  { perror("select"); return -1; }
        if (ret == 0) { fprintf(stderr, "Timeout waiting for data.\n"); return (ssize_t)received; }

        ssize_t n = read(fd, buf + received, len - received);
        if (n < 0) { perror("read"); return -1; }
        received += (size_t)n;
    }
    return (ssize_t)received;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device>  e.g. /dev/ttyS0\n", argv[0]);
        return EXIT_FAILURE;
    }

    int fd = open(argv[1], O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    /* Switch to blocking I/O after open */
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~O_NONBLOCK);

    if (configure_port(fd)          != 0) goto fail;
    if (enable_internal_loopback(fd) != 0) goto fail;

    tcflush(fd, TCIOFLUSH);   /* Clear any stale data */

    /* Build and transmit test pattern */
    uint8_t tx_buf[TEST_LEN];
    uint8_t rx_buf[TEST_LEN];
    build_test_pattern(tx_buf, TEST_LEN);

    printf("Sending %d bytes in internal loopback mode...\n", TEST_LEN);
    ssize_t written = write(fd, tx_buf, TEST_LEN);
    if (written != TEST_LEN) {
        fprintf(stderr, "Short write: %zd/%d\n", written, TEST_LEN);
        goto fail;
    }

    /* Receive echoed data */
    ssize_t n = recv_exact(fd, rx_buf, TEST_LEN, TIMEOUT_SEC);
    if (n != TEST_LEN) {
        fprintf(stderr, "Short read: %zd/%d bytes received.\n", n, TEST_LEN);
        goto fail;
    }

    /* Compare TX and RX buffers */
    if (memcmp(tx_buf, rx_buf, TEST_LEN) == 0) {
        printf("PASS: All %d bytes matched.\n", TEST_LEN);
    } else {
        printf("FAIL: Data mismatch detected.\n");
        for (int i = 0; i < TEST_LEN; i++) {
            if (tx_buf[i] != rx_buf[i])
                printf("  Byte[%3d]: TX=0x%02X  RX=0x%02X\n", i, tx_buf[i], rx_buf[i]);
        }
    }

    disable_internal_loopback(fd);
    close(fd);
    return EXIT_SUCCESS;

fail:
    disable_internal_loopback(fd);
    close(fd);
    return EXIT_FAILURE;
}
```

---

### Linux termios API – External Loopback

For external loopback, no special modem-control bits are set. The UART is configured normally; the physical TX↔RX wire does the routing.

```c
/*
 * uart_external_loopback.c
 *
 * External loopback test: TX pin is physically wired to RX pin.
 * The driver sends data and reads it back over the normal data path.
 *
 * Compile: gcc -o ext_loopback uart_external_loopback.c
 * Run:     sudo ./ext_loopback /dev/ttyS0 115200
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>

/* Map integer baud rate to termios constant */
static speed_t baud_const(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        default:     return B115200;
    }
}

static int open_and_configure(const char *dev, int baud)
{
    int fd = open(dev, O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); return -1; }

    struct termios tty;
    cfmakeraw(&tty);
    cfsetispeed(&tty, baud_const(baud));
    cfsetospeed(&tty, baud_const(baud));

    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

/* Structured result for one test run */
typedef struct {
    int    bytes_sent;
    int    bytes_received;
    int    mismatches;
    double elapsed_ms;
} loopback_result_t;

static loopback_result_t run_loopback_test(int fd, const uint8_t *pattern,
                                            int len, int timeout_ms)
{
    loopback_result_t result = {0};
    struct timespec t0, t1;

    clock_gettime(CLOCK_MONOTONIC, &t0);

    ssize_t w = write(fd, pattern, len);
    result.bytes_sent = (int)w;

    uint8_t *rx = calloc(1, len);
    int got = 0;
    while (got < len) {
        fd_set fds;
        FD_ZERO(&fds); FD_SET(fd, &fds);
        struct timeval tv = { .tv_sec = 0, .tv_usec = timeout_ms * 1000 };
        if (select(fd + 1, &fds, NULL, NULL, &tv) <= 0) break;
        ssize_t n = read(fd, rx + got, len - got);
        if (n > 0) got += (int)n;
    }

    clock_gettime(CLOCK_MONOTONIC, &t1);
    result.bytes_received = got;
    result.elapsed_ms = (t1.tv_sec - t0.tv_sec) * 1000.0
                      + (t1.tv_nsec - t0.tv_nsec) / 1e6;

    for (int i = 0; i < got && i < len; i++)
        if (rx[i] != pattern[i]) result.mismatches++;

    free(rx);
    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device> <baud>\n", argv[0]);
        return 1;
    }

    int fd = open_and_configure(argv[1], atoi(argv[2]));
    if (fd < 0) return 1;

    /* Test 1: Incremental byte pattern */
    uint8_t pattern[256];
    for (int i = 0; i < 256; i++) pattern[i] = (uint8_t)i;

    printf("=== External Loopback Test: %s @ %s baud ===\n\n", argv[1], argv[2]);

    loopback_result_t r = run_loopback_test(fd, pattern, 256, 500);
    printf("Incremental pattern (256 bytes):\n");
    printf("  Sent:     %d bytes\n", r.bytes_sent);
    printf("  Received: %d bytes\n", r.bytes_received);
    printf("  Errors:   %d mismatches\n", r.mismatches);
    printf("  Time:     %.2f ms\n", r.elapsed_ms);
    printf("  Result:   %s\n\n",
           (r.bytes_received == 256 && r.mismatches == 0) ? "PASS" : "FAIL");

    /* Test 2: All-zeros pattern */
    uint8_t zeros[64] = {0};
    r = run_loopback_test(fd, zeros, 64, 200);
    printf("All-zeros pattern (64 bytes): %s\n",
           (r.bytes_received == 64 && r.mismatches == 0) ? "PASS" : "FAIL");

    /* Test 3: All-ones (0xFF) pattern */
    uint8_t ones[64];
    memset(ones, 0xFF, 64);
    r = run_loopback_test(fd, ones, 64, 200);
    printf("All-ones  pattern (64 bytes): %s\n",
           (r.bytes_received == 64 && r.mismatches == 0) ? "PASS" : "FAIL");

    /* Test 4: Alternating 0xAA / 0x55 */
    uint8_t alt[64];
    for (int i = 0; i < 64; i++) alt[i] = (i % 2) ? 0xAA : 0x55;
    r = run_loopback_test(fd, alt, 64, 200);
    printf("Alternating 0xAA/0x55 (64):  %s\n",
           (r.bytes_received == 64 && r.mismatches == 0) ? "PASS" : "FAIL");

    close(fd);
    return 0;
}
```

---

### Register-Level Driver (Bare-Metal / Embedded C)

On embedded targets (e.g. STM32, Cortex-M) a UART driver often directly manipulates memory-mapped registers. The following shows a generic register-level approach, adaptable to any 16550-compatible UART or MMIO peripheral.

```c
/*
 * uart_reg_loopback.c  –  Bare-metal / register-level loopback test
 *
 * Assumes a memory-mapped 16550-compatible UART.
 * Adapt BASE_ADDR and register offsets for your SoC.
 */

#include <stdint.h>
#include <stdbool.h>

/* ── Register map ────────────────────────────────────────────────── */
#define UART_BASE   0x40011000UL   /* Example: STM32 USART1 base    */

#define RBR  (*(volatile uint8_t *)(UART_BASE + 0x00))  /* RX Buffer  */
#define THR  (*(volatile uint8_t *)(UART_BASE + 0x00))  /* TX Holding */
#define IER  (*(volatile uint8_t *)(UART_BASE + 0x01))  /* Int Enable */
#define FCR  (*(volatile uint8_t *)(UART_BASE + 0x02))  /* FIFO Ctrl  */
#define LCR  (*(volatile uint8_t *)(UART_BASE + 0x03))  /* Line Ctrl  */
#define MCR  (*(volatile uint8_t *)(UART_BASE + 0x04))  /* Modem Ctrl */
#define LSR  (*(volatile uint8_t *)(UART_BASE + 0x05))  /* Line Status*/

/* LSR bits */
#define LSR_DR    (1 << 0)   /* Data ready (byte in RBR) */
#define LSR_THRE  (1 << 5)   /* TX holding register empty */
#define LSR_TEMT  (1 << 6)   /* Transmitter empty         */

/* MCR bits */
#define MCR_DTR   (1 << 0)
#define MCR_RTS   (1 << 1)
#define MCR_OUT1  (1 << 2)
#define MCR_OUT2  (1 << 3)
#define MCR_LOOP  (1 << 4)   /* Internal loopback enable  */

/* LCR bits */
#define LCR_DLAB  (1 << 7)   /* Divisor Latch Access Bit  */
#define LCR_8N1   (0x03)     /* 8 data, no parity, 1 stop */

/* ── UART initialisation (115200 baud, 8N1, loopback off) ─────── */
void uart_init(uint32_t sys_clk_hz, uint32_t baud)
{
    uint16_t divisor = (uint16_t)(sys_clk_hz / (16 * baud));

    LCR = LCR_DLAB;                 /* Access divisor latches   */
    /* DLL and DLH are at offsets 0x00/0x01 when DLAB=1 */
    *(volatile uint8_t *)(UART_BASE + 0x00) = (uint8_t)(divisor & 0xFF);
    *(volatile uint8_t *)(UART_BASE + 0x01) = (uint8_t)(divisor >> 8);

    LCR = LCR_8N1;                  /* 8N1, clear DLAB           */
    FCR = 0xC7;                     /* Enable & clear FIFOs, 14B trigger */
    MCR = MCR_DTR | MCR_RTS | MCR_OUT2;  /* Normal operation      */
    IER = 0x00;                     /* Disable all interrupts    */
}

/* ── Enable / disable internal loopback ─────────────────────────── */
void uart_set_loopback(bool enable)
{
    if (enable)
        MCR |=  MCR_LOOP;
    else
        MCR &= ~MCR_LOOP;
}

/* ── Transmit one byte (polled) ─────────────────────────────────── */
void uart_putc(uint8_t c)
{
    while (!(LSR & LSR_THRE))
        ;           /* Wait for TX holding register empty */
    THR = c;
}

/* ── Receive one byte with timeout ─────────────────────────────── */
bool uart_getc_timeout(uint8_t *c, uint32_t timeout_cycles)
{
    while (timeout_cycles--) {
        if (LSR & LSR_DR) {
            *c = RBR;
            return true;
        }
    }
    return false;
}

/* ── Full loopback test ─────────────────────────────────────────── */
typedef struct {
    uint32_t passed;
    uint32_t failed;
    uint32_t timeout;
} loopback_stats_t;

loopback_stats_t uart_loopback_test(void)
{
    loopback_stats_t stats = {0};

    uart_set_loopback(true);

    /* Test all 256 byte values */
    for (uint16_t tx_val = 0; tx_val <= 0xFF; tx_val++) {
        uint8_t rx_val;
        uart_putc((uint8_t)tx_val);

        if (!uart_getc_timeout(&rx_val, 100000)) {
            stats.timeout++;
        } else if (rx_val == (uint8_t)tx_val) {
            stats.passed++;
        } else {
            stats.failed++;
        }
    }

    uart_set_loopback(false);
    return stats;
}

/* ── Entry point (bare-metal startup calls this) ─────────────────── */
void loopback_main(void)
{
    uart_init(48000000UL, 115200);     /* 48 MHz clock, 115200 baud */

    loopback_stats_t r = uart_loopback_test();

    if (r.passed == 256 && r.failed == 0 && r.timeout == 0) {
        /* Signal PASS via LED or debug pin */
    } else {
        /* Signal FAIL and log stats via debug channel */
    }
}
```

---

### Windows API Loopback Test

```cpp
/*
 * uart_win_loopback.cpp
 *
 * External loopback test using the Windows COMM API.
 * Compile: cl uart_win_loopback.cpp
 * Run:     uart_win_loopback.exe COM3 115200
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static HANDLE open_port(const char *port_name, DWORD baud)
{
    char full[16];
    snprintf(full, sizeof(full), "\\\\.\\%s", port_name);

    HANDLE h = CreateFileA(full, GENERIC_READ | GENERIC_WRITE,
                           0, NULL, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s: error %lu\n", port_name, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary  = TRUE;
    dcb.fParity  = FALSE;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl  = RTS_CONTROL_DISABLE;
    SetCommState(h, &dcb);

    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout         = 50;
    to.ReadTotalTimeoutConstant    = 1000;
    to.ReadTotalTimeoutMultiplier  = 10;
    to.WriteTotalTimeoutConstant   = 1000;
    to.WriteTotalTimeoutMultiplier = 10;
    SetCommTimeouts(h, &to);

    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);
    return h;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <COMn> <baud>\n", argv[0]);
        return 1;
    }

    HANDLE h = open_port(argv[1], (DWORD)atoi(argv[2]));
    if (h == INVALID_HANDLE_VALUE) return 1;

    /* Build test pattern */
    const int N = 256;
    BYTE tx[256], rx[256] = {0};
    for (int i = 0; i < N; i++) tx[i] = (BYTE)i;

    DWORD written = 0, read_bytes = 0;
    WriteFile(h, tx, N, &written, NULL);
    ReadFile(h,  rx, N, &read_bytes, NULL);

    printf("Port:     %s @ %s baud\n", argv[1], argv[2]);
    printf("Sent:     %lu bytes\n", written);
    printf("Received: %lu bytes\n", read_bytes);

    int errors = 0;
    for (DWORD i = 0; i < read_bytes; i++)
        if (rx[i] != tx[i]) { errors++; printf("  Byte[%lu]: TX=%02X RX=%02X\n", i, tx[i], rx[i]); }

    printf("Result: %s\n", (read_bytes == N && errors == 0) ? "PASS" : "FAIL");

    CloseHandle(h);
    return (read_bytes == (DWORD)N && errors == 0) ? 0 : 1;
}
```

---

## Programming in Rust

### Rust – Internal Loopback with serialport crate

```toml
# Cargo.toml
[dependencies]
serialport = "4"
```

```rust
//! uart_internal_loopback.rs
//!
//! Internal loopback test using the `serialport` crate.
//! The TIOCM_LOOP ioctl is sent on Linux/macOS via the
//! `SerialPort::write_data_terminal_ready` workaround, or via
//! a direct `nix` ioctl call shown below.
//!
//! Run: cargo run --example internal_loopback /dev/ttyS0

use serialport::{SerialPort, SerialPortType};
use std::io::{Read, Write};
use std::time::{Duration, Instant};

#[cfg(unix)]
mod loopback_ioctl {
    use nix::libc;
    use std::os::unix::io::AsRawFd;

    // TIOCMSET / TIOCMGET ioctls for modem control lines
    #[cfg(target_os = "linux")]
    const TIOCMGET: libc::c_ulong = 0x5415;
    #[cfg(target_os = "linux")]
    const TIOCMSET: libc::c_ulong = 0x5418;
    #[cfg(target_os = "linux")]
    const TIOCM_LOOP: libc::c_int = 0x8000;

    /// Enable or disable the UART's internal loopback mode.
    pub fn set_loopback<T: AsRawFd>(port: &T, enable: bool) -> std::io::Result<()> {
        let fd = port.as_raw_fd();
        let mut bits: libc::c_int = 0;
        unsafe {
            if libc::ioctl(fd, TIOCMGET, &mut bits as *mut _) < 0 {
                return Err(std::io::Error::last_os_error());
            }
            if enable { bits |= TIOCM_LOOP; } else { bits &= !TIOCM_LOOP; }
            if libc::ioctl(fd, TIOCMSET, &bits as *const _) < 0 {
                return Err(std::io::Error::last_os_error());
            }
        }
        Ok(())
    }
}

/// Build an incrementing byte pattern.
fn test_pattern(len: usize) -> Vec<u8> {
    (0..len).map(|i| (i & 0xFF) as u8).collect()
}

/// Receive exactly `len` bytes within `timeout`.
fn recv_exact(port: &mut dyn SerialPort, len: usize, timeout: Duration) -> Vec<u8> {
    let mut buf = vec![0u8; len];
    let mut received = 0;
    let deadline = Instant::now() + timeout;

    while received < len && Instant::now() < deadline {
        match port.read(&mut buf[received..]) {
            Ok(n)  => received += n,
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
            Err(e) => { eprintln!("Read error: {e}"); break; }
        }
    }
    buf.truncate(received);
    buf
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let device = std::env::args().nth(1).unwrap_or_else(|| "/dev/ttyS0".into());

    let mut port = serialport::new(&device, 115_200)
        .timeout(Duration::from_millis(500))
        .open()
        .map_err(|e| format!("Failed to open {device}: {e}"))?;

    println!("Opened {} at 115200 baud", device);

    // Enable internal loopback (Linux only in this example)
    #[cfg(unix)]
    loopback_ioctl::set_loopback(port.as_ref(), true)?;

    // Flush stale bytes
    port.clear(serialport::ClearBuffer::All)?;

    let pattern = test_pattern(256);
    println!("Sending {} bytes in internal loopback mode...", pattern.len());

    port.write_all(&pattern)?;
    port.flush()?;

    let received = recv_exact(port.as_mut(), pattern.len(), Duration::from_secs(2));

    // Disable loopback before evaluating results
    #[cfg(unix)]
    loopback_ioctl::set_loopback(port.as_ref(), false)?;

    // Compare
    let mismatches: Vec<_> = pattern.iter().zip(received.iter())
        .enumerate()
        .filter(|(_, (t, r))| t != r)
        .collect();

    if received.len() == pattern.len() && mismatches.is_empty() {
        println!("PASS: All {} bytes matched.", pattern.len());
    } else {
        println!("FAIL:");
        println!("  Expected {} bytes, got {}", pattern.len(), received.len());
        for (i, (t, r)) in &mismatches {
            println!("  Byte[{i:3}]: TX={t:#04x}  RX={r:#04x}");
        }
        std::process::exit(1);
    }

    Ok(())
}
```

---

### Rust – Loopback Validation Framework

A more structured approach suitable for a test harness or CI integration:

```rust
//! loopback_framework.rs
//!
//! A reusable UART loopback test framework in Rust.
//! Runs multiple named test cases and reports structured results.

use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::{Duration, Instant};

// ─── Test case definition ────────────────────────────────────────────────────

#[derive(Debug, Clone)]
pub struct LoopbackCase {
    pub name:    &'static str,
    pub payload: Vec<u8>,
}

impl LoopbackCase {
    pub fn new(name: &'static str, payload: Vec<u8>) -> Self {
        Self { name, payload }
    }
}

// ─── Result ──────────────────────────────────────────────────────────────────

#[derive(Debug)]
pub struct LoopbackResult {
    pub case_name:  &'static str,
    pub sent:       usize,
    pub received:   usize,
    pub mismatches: usize,
    pub elapsed:    Duration,
    pub pass:       bool,
}

impl LoopbackResult {
    pub fn print(&self) {
        let status = if self.pass { "PASS ✓" } else { "FAIL ✗" };
        println!(
            "[{status}] {:<30}  sent={:<4}  recv={:<4}  errors={}  {:.1}ms",
            self.case_name,
            self.sent,
            self.received,
            self.mismatches,
            self.elapsed.as_secs_f64() * 1000.0
        );
    }
}

// ─── Runner ──────────────────────────────────────────────────────────────────

pub struct LoopbackRunner {
    port:    Box<dyn SerialPort>,
    timeout: Duration,
}

impl LoopbackRunner {
    pub fn new(port: Box<dyn SerialPort>, timeout: Duration) -> Self {
        Self { port, timeout }
    }

    fn drain_rx(&mut self) {
        let _ = self.port.clear(serialport::ClearBuffer::All);
    }

    fn recv_with_deadline(&mut self, expected: usize) -> Vec<u8> {
        let mut buf = vec![0u8; expected];
        let mut n = 0;
        let deadline = Instant::now() + self.timeout;

        while n < expected && Instant::now() < deadline {
            match self.port.read(&mut buf[n..]) {
                Ok(k)  => n += k,
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(_) => break,
            }
        }
        buf.truncate(n);
        buf
    }

    pub fn run_case(&mut self, case: &LoopbackCase) -> LoopbackResult {
        self.drain_rx();

        let start = Instant::now();
        let _ = self.port.write_all(&case.payload);
        let _ = self.port.flush();

        let received = self.recv_with_deadline(case.payload.len());
        let elapsed  = start.elapsed();

        let mismatches = case.payload.iter()
            .zip(received.iter())
            .filter(|(t, r)| t != r)
            .count();

        let pass = received.len() == case.payload.len() && mismatches == 0;

        LoopbackResult {
            case_name:  case.name,
            sent:       case.payload.len(),
            received:   received.len(),
            mismatches,
            elapsed,
            pass,
        }
    }

    pub fn run_suite(&mut self, cases: &[LoopbackCase]) -> Vec<LoopbackResult> {
        cases.iter().map(|c| self.run_case(c)).collect()
    }
}

// ─── Standard test suite factory ─────────────────────────────────────────────

pub fn standard_suite() -> Vec<LoopbackCase> {
    let mut suite = Vec::new();

    // Incrementing bytes 0x00..=0xFF
    suite.push(LoopbackCase::new(
        "Incrementing 0x00..0xFF",
        (0u8..=255).collect(),
    ));

    // All zeros
    suite.push(LoopbackCase::new(
        "All zeros (64 bytes)",
        vec![0x00u8; 64],
    ));

    // All ones
    suite.push(LoopbackCase::new(
        "All 0xFF (64 bytes)",
        vec![0xFFu8; 64],
    ));

    // Alternating 0xAA / 0x55
    suite.push(LoopbackCase::new(
        "Alternating 0xAA/0x55",
        (0..64).map(|i| if i % 2 == 0 { 0xAAu8 } else { 0x55u8 }).collect(),
    ));

    // Walking-1 pattern (one bit set per byte, cycling)
    suite.push(LoopbackCase::new(
        "Walking 1 bits",
        (0..8).map(|i| 1u8 << i).cycle().take(64).collect(),
    ));

    // Large burst (stress test FIFO)
    suite.push(LoopbackCase::new(
        "Large burst (1024 bytes)",
        (0..1024).map(|i| (i & 0xFF) as u8).collect(),
    ));

    suite
}

// ─── main ────────────────────────────────────────────────────────────────────

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let device = std::env::args().nth(1).unwrap_or_else(|| "/dev/ttyS0".into());
    let baud: u32 = std::env::args().nth(2)
        .and_then(|s| s.parse().ok())
        .unwrap_or(115_200);

    let port = serialport::new(&device, baud)
        .timeout(Duration::from_millis(200))
        .open()?;

    println!("=== UART Loopback Test Suite ===");
    println!("Device : {device}");
    println!("Baud   : {baud}\n");

    let suite = standard_suite();
    let mut runner = LoopbackRunner::new(port, Duration::from_millis(500));
    let results = runner.run_suite(&suite);

    let total  = results.len();
    let passed = results.iter().filter(|r| r.pass).count();
    let failed = total - passed;

    for r in &results { r.print(); }

    println!("\n─────────────────────────────");
    println!("Total: {total}  Passed: {passed}  Failed: {failed}");
    println!("Overall: {}", if failed == 0 { "PASS ✓" } else { "FAIL ✗" });

    std::process::exit(if failed == 0 { 0 } else { 1 });
}
```

---

### Rust – Bare-Metal / Embedded (no_std)

For embedded targets using the `embedded-hal` ecosystem:

```toml
# Cargo.toml  (embedded target, e.g. STM32)
[dependencies]
embedded-hal = "1"
nb           = "1"
cortex-m     = "0.7"
```

```rust
//! uart_embedded_loopback.rs  –  no_std loopback for embedded-hal targets
//!
//! This compiles for bare-metal Cortex-M targets.
//! Swap `UartDriver` with your HAL's concrete UART type.

#![no_std]

use embedded_hal::serial::{Read, Write};
use nb::block;

/// Outcome of a single loopback byte transfer.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ByteResult {
    Pass,
    Mismatch { sent: u8, received: u8 },
    Timeout,
}

/// Run a single-byte loopback test using any embedded-hal serial peripheral.
///
/// The caller is responsible for setting the hardware loopback bit before
/// calling this function, and clearing it afterwards.
pub fn loopback_byte<S>(serial: &mut S, value: u8, timeout: u32) -> ByteResult
where
    S: Write<u8> + Read<u8>,
    <S as Read<u8>>::Error: core::fmt::Debug,
    <S as Write<u8>>::Error: core::fmt::Debug,
{
    // Transmit
    if block!(serial.write(value)).is_err() {
        return ByteResult::Timeout;
    }
    let _ = block!(serial.flush());

    // Receive with a spin-loop timeout
    for _ in 0..timeout {
        match serial.read() {
            Ok(b)  => {
                return if b == value {
                    ByteResult::Pass
                } else {
                    ByteResult::Mismatch { sent: value, received: b }
                };
            }
            Err(nb::Error::WouldBlock) => { /* spin */ }
            Err(_) => return ByteResult::Timeout,
        }
        cortex_m::asm::nop();  // Prevent loop from being optimised away
    }
    ByteResult::Timeout
}

/// Run a full 0x00..=0xFF loopback sweep.
/// Returns (pass_count, fail_count, timeout_count).
pub fn loopback_sweep<S>(serial: &mut S, timeout: u32) -> (u32, u32, u32)
where
    S: Write<u8> + Read<u8>,
    <S as Read<u8>>::Error: core::fmt::Debug,
    <S as Write<u8>>::Error: core::fmt::Debug,
{
    let (mut pass, mut fail, mut tmo) = (0u32, 0u32, 0u32);

    for byte in 0u8..=255 {
        match loopback_byte(serial, byte, timeout) {
            ByteResult::Pass             => pass += 1,
            ByteResult::Mismatch { .. }  => fail += 1,
            ByteResult::Timeout          => tmo  += 1,
        }
    }
    (pass, fail, tmo)
}
```

---

## Test Patterns and Validation Strategies

Well-chosen test patterns catch different classes of bugs:

| Pattern | What it tests |
|---|---|
| `0x00..=0xFF` (full sweep) | All 256 byte values, basic data integrity |
| All `0x00` | Start-bit detection, ability to transmit a zero frame |
| All `0xFF` | Stop-bit handling, all-mark line condition |
| `0xAA` / `0x55` alternating | Transitions between 1 and 0, crosstalk sensitivity |
| Walking `1` (`0x01, 0x02, 0x04…`) | Individual bit sensitivity, bit-flip detection |
| Pseudo-random (LFSR) | Maximally stressed data with uniform bit statistics |
| Large bursts (1024+ bytes) | FIFO overflow, DMA transfer correctness, flow control |
| Single byte repeated | Framing stability over many identical frames |

### Pseudo-Random Pattern Generator (LFSR) in C

```c
/* 8-bit maximal-length LFSR (polynomial x^8 + x^6 + x^5 + x^4 + 1) */
static uint8_t lfsr8_next(uint8_t state)
{
    uint8_t feedback = ((state >> 7) ^ (state >> 5) ^ (state >> 4) ^ (state >> 3)) & 0x01;
    return (state << 1) | feedback;
}

void fill_lfsr_pattern(uint8_t *buf, size_t len, uint8_t seed)
{
    buf[0] = seed;
    for (size_t i = 1; i < len; i++)
        buf[i] = lfsr8_next(buf[i-1]);
}
```

---

## Error Detection and Diagnostics

When a loopback test fails, structured diagnostics help pinpoint the root cause:

```c
typedef enum {
    LB_ERR_NONE       = 0,
    LB_ERR_NO_DATA    = 1,  /* Timeout: no bytes received at all           */
    LB_ERR_SHORT_READ = 2,  /* Fewer bytes received than sent              */
    LB_ERR_MISMATCH   = 3,  /* Received bytes differ from transmitted      */
    LB_ERR_FRAMING    = 4,  /* UART reported framing error (LSR[FE])       */
    LB_ERR_OVERRUN    = 5,  /* UART reported overrun error (LSR[OE])       */
    LB_ERR_PARITY     = 6,  /* UART reported parity error  (LSR[PE])       */
} lb_error_t;

const char *lb_error_str(lb_error_t e)
{
    switch (e) {
        case LB_ERR_NONE:       return "No error";
        case LB_ERR_NO_DATA:    return "Timeout: no data received";
        case LB_ERR_SHORT_READ: return "Short read: fewer bytes than expected";
        case LB_ERR_MISMATCH:   return "Data mismatch";
        case LB_ERR_FRAMING:    return "Framing error (check baud rate / wiring)";
        case LB_ERR_OVERRUN:    return "Overrun error (read too slow / FIFO full)";
        case LB_ERR_PARITY:     return "Parity error (check parity config)";
        default:                return "Unknown error";
    }
}
```

**Diagnostic checklist based on failure mode:**

| Symptom | Likely cause |
|---|---|
| No bytes received | Loopback not physically connected; RX interrupt/DMA not started; baud rate misconfigured |
| Short read (some bytes missing) | FIFO overrun; DMA length mismatch; receiver not ready quickly enough |
| Data mismatch on specific bits | Bit-flip in transceiver; electrical noise; incorrect voltage levels |
| Framing errors | Baud rate mismatch between TX and RX (clock source issue) |
| Parity errors | Parity setting mismatch (one side enabled, other disabled) |
| All bytes correct but wrong count | Off-by-one in buffer size; null terminator consumed |
| Works at low baud, fails at high | Signal integrity; transceiver bandwidth; FIFO not flushed between tests |

---

## Summary

UART loopback testing is an essential technique for validating the correctness and reliability of UART drivers and hardware. The two main modes — **internal loopback** (configured via a hardware register bit, exercising the digital core) and **external loopback** (connecting TX to RX physically, testing the full analog signal path) — complement each other and together provide comprehensive coverage.

**Key takeaways:**

- **Internal loopback** requires only a register write and is ideal for early driver bring-up and software-only CI. It verifies baud rate generation, FIFO logic, and interrupt/DMA pipelines without needing a physical loopback wire.

- **External loopback** requires a physical connection (jumper, loopback plug, or virtual pair via `socat`) and additionally validates line drivers, transceivers, and PCB signal integrity.

- **Test patterns** should include full byte-sweep (0x00..0xFF), stress patterns (all-zeros, all-ones, alternating bits, LFSR pseudo-random), and large burst transfers to catch corner cases in FIFO and DMA handling.

- **Linux** exposes internal loopback via `TIOCM_LOOP` ioctl, external loopback through standard `termios` configuration; **Windows** uses the `COMM` API with the standard `DCB` structure; **bare-metal** targets set an MCR/CR register bit directly.

- **Rust** provides type-safe, ergonomic wrappers through the `serialport` crate for hosted targets and `embedded-hal` traits for no_std embedded environments — both support the same loopback test logic.

- **Structured diagnostics** (distinguishing timeout, short-read, mismatch, framing, overrun, and parity errors) are crucial for efficiently debugging failures during hardware bring-up and manufacturing test.

Loopback tests, run early and often, dramatically reduce the time spent debugging communication issues by providing a fast, repeatable, self-contained signal-path verification.

---

*Document: UART Programming Series – Topic 31: Loopback Testing*