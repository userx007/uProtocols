# 70. Port Expander Integration

**Architecture & Theory** — explains why port expanders are needed, how the host communicates over I2C/SPI to a register-mapped UART IC, and provides a visual topology diagram.

**IC Reference Tables** — lists the most common I2C expanders (SC16IS740/750/752) and SPI expanders (MAX3107/3109, XR16M570, TL16C554) with their channel counts, max baud rates, and FIFO sizes.

**C/C++ Examples:**
- **SC16IS752 via I2C** — full register-level driver with `open`, `init_channel` (baud divisor calculation, FIFO reset, 8N1 config), blocking `write`, and non-blocking `read`
- **MAX3107 via SPI** — C++ class using Linux `spidev`, with burst-read support and FIFO-level polling
- **Linux termios integration** — shows how the kernel `sc16is7xx` driver exposes channels as `/dev/ttySC*`, usable with standard POSIX serial APIs plus a Device Tree snippet

**Rust Examples:**
- **SC16IS752 via I2C** — idiomatic Rust driver using the `i2cdev` crate, with a custom `UartError` type, safe register abstractions using `const fn`, and clean channel init/read/write methods
- **MAX3107 via SPI** — Rust driver using `spidev` with burst-read as a `Vec<u8>` return

**Interrupt-Driven Operation** — C and Rust skeletons for IRQ-based RX using GPIO edge events, avoiding CPU-intensive polling.

**Flow Control** — code snippets for both software XON/XOFF and hardware RTS/CTS auto-flow modes via the Enhanced Feature Register.

**Common Pitfalls table** — covers garbled data, I2C NAK, TX data loss, FIFO overflow, SPI mode mismatch, baud prescaler errors, and missing IRQ pull-ups.

## Using I2C/SPI UART Expanders for Additional Serial Ports

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Port Expanders?](#why-port-expanders)
3. [Architecture Overview](#architecture-overview)
4. [Common Port Expander ICs](#common-port-expander-ics)
5. [I2C-Based UART Expanders](#i2c-based-uart-expanders)
6. [SPI-Based UART Expanders](#spi-based-uart-expanders)
7. [Programming in C/C++](#programming-in-cc)
   - [SC16IS752 via I2C (C/C++)](#sc16is752-via-i2c-cc)
   - [MAX3107 via SPI (C/C++)](#max3107-via-spi-cc)
   - [Linux sysfs / termios Integration (C)](#linux-sysfs--termios-integration-c)
8. [Programming in Rust](#programming-in-rust)
   - [SC16IS752 via I2C (Rust)](#sc16is752-via-i2c-rust)
   - [SPI UART Expander (Rust)](#spi-uart-expander-rust)
9. [Interrupt-Driven Operation](#interrupt-driven-operation)
10. [Buffering and Flow Control](#buffering-and-flow-control)
11. [Common Pitfalls](#common-pitfalls)
12. [Summary](#summary)

---

## Introduction

In embedded and industrial systems, the number of hardware UART ports is often limited — a typical microcontroller may expose only one to four UARTs, while a system-on-chip (SoC) running Linux may have even fewer accessible to application software. **Port expander ICs** solve this problem by multiplying available serial ports using a single I2C or SPI bus connection to the host.

A UART port expander (also called a UART bridge or serial port multiplexer) is an integrated circuit that presents one or more fully independent UART channels to the outside world, while being controlled by the host over a secondary bus (I2C or SPI). From the host's perspective the device appears as a set of registers; from the peripheral's perspective it appears as a standard RS-232 / RS-485 / TTL UART.

---

## Why Port Expanders?

| Scenario | Problem | Solution |
|---|---|---|
| IoT gateway | Must talk to 8+ sensors over RS-485 | Add dual-channel UART expander per pair |
| Industrial PLC | Legacy RS-232 devices outnumber UART pins | SPI expander with 4/8 channels |
| Raspberry Pi project | Only one hardware UART available | I2C expander adds 2 more |
| Microcontroller node | Pin-limited; GPS + GSM + RFID needed | Quad UART over SPI |

Key advantages of hardware UART expanders over software (bit-banged) UARTs:

- **Accurate baud rate generation** independent of CPU load
- **Hardware FIFOs** (typically 64–128 bytes) reduce interrupt rate
- **True RS-232 / RS-485 line driver** integration in some devices
- **Hardware flow control** (RTS/CTS) without CPU intervention
- **Sleep/low-power modes** with interrupt-on-receive wake-up

---

## Architecture Overview

```
Host MCU / SoC
┌─────────────────────────────────┐
│  Application                    │
│  ─────────────────────────────  │
│  I2C Master  │  SPI Master      │
└──────┬───────┴──────┬───────────┘
       │ I2C          │ SPI
       │              │
┌──────▼──────┐ ┌─────▼──────────┐
│ SC16IS752   │ │   MAX3107       │
│ (I2C, 2ch)  │ │  (SPI, 1ch)    │
├─────────────┤ ├────────────────┤
│ CH A  CH B  │ │   Channel      │
└──┬──────┬───┘ └───────┬────────┘
   │      │             │
 UART-A UART-B        UART-C
 TTL    TTL           TTL/RS232
   │      │             │
 Dev-1  Dev-2         Dev-3
```

The host accesses each UART channel by selecting a **register address** space. For dual-channel devices, a **channel-select bit** or **address offset** distinguishes Channel A from Channel B.

---

## Common Port Expander ICs

### I2C-Attached

| IC | Manufacturer | Channels | Max Baud | FIFO | Notes |
|---|---|---|---|---|---|
| **SC16IS740** | NXP | 1 | 5 Mbps | 64 B each | GPIO pins included |
| **SC16IS750** | NXP | 1 | 5 Mbps | 64 B each | IrDA support |
| **SC16IS752** | NXP | 2 | 5 Mbps | 64 B each | Most popular dual-channel |
| **SC16IS762** | NXP | 2 | 5 Mbps | 64 B each | Extended temp range |
| **MCP2221A** | Microchip | 1 | 115200 | — | USB-to-UART/I2C bridge |

### SPI-Attached

| IC | Manufacturer | Channels | Max Baud | FIFO | Notes |
|---|---|---|---|---|---|
| **MAX3107** | Maxim/Analog | 1 | 24 Mbps | 128 B each | Internal oscillator |
| **MAX3109** | Maxim/Analog | 2 | 24 Mbps | 128 B each | Dual channel MAX3107 |
| **XR16M570** | Exar | 1 | 15 Mbps | 64 B each | Low voltage |
| **TL16C554** | TI | 4 | 1.5 Mbps | 16 B each | Classic quad UART |
| **XR17V354** | Exar | 4 | 25 Mbps | 128 B each | Industrial quad |

---

## I2C-Based UART Expanders

The **SC16IS752** is the reference device for I2C-based expansion. It communicates at standard I2C speeds (100 kHz / 400 kHz / 1 MHz Fast+).

### Register Map (SC16IS7xx Family)

The SC16IS7xx uses a **sub-address byte** sent after the I2C address. The sub-address encodes the register index and the channel:

```
Sub-address byte: [REG3 REG2 REG1 REG0 CH1 CH0 X X]
                   ────────────────────  ───────
                   Register index (4b)   Channel (2b)
```

Key registers:

| Register | Abbr | R/W | Description |
|---|---|---|---|
| 0x00 | RHR / THR | R/W | Receive / Transmit Holding Register |
| 0x01 | IER | R/W | Interrupt Enable Register |
| 0x02 | IIR / FCR | R/W | Interrupt Identification / FIFO Control |
| 0x03 | LCR | R/W | Line Control Register (word length, parity, stop bits) |
| 0x04 | MCR | R/W | Modem Control Register (RTS, CTS) |
| 0x05 | LSR | R | Line Status Register (data ready, TX empty, errors) |
| 0x06 | MSR | R | Modem Status Register |
| 0x0B | TXLVL | R | TX FIFO level |
| 0x0C | RXLVL | R | RX FIFO level |
| 0x0D | IODIR | R/W | GPIO direction |
| 0x0E | IOSTATE | R/W | GPIO state |

When the **Divisor Latch Access Bit** (DLAB) in LCR is set, addresses 0x00 and 0x01 map to the baud rate divisor registers (DLL / DLH) instead.

---

## SPI-Based UART Expanders

The **MAX3107** uses a standard 4-wire SPI interface (CPOL=0, CPHA=0). Register access uses a one-byte header:

```
First byte: [RW] [A6 A5 A4 A3 A2 A1 A0]
              │    ─────────────────────
              │    7-bit register address
              └─── 1 = Read, 0 = Write
```

A burst read/write continues for as many bytes as the CS line stays asserted.

---

## Programming in C/C++

### SC16IS752 via I2C (C/C++)

This example targets Linux (using `/dev/i2c-*`) but the register logic applies equally to bare-metal environments.

```c
/*
 * sc16is752.c  –  SC16IS752 dual UART over I2C
 * Tested on Raspberry Pi / Linux I2C subsystem
 *
 * Build:  gcc -o sc16is752_demo sc16is752.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>

/* ── Device I2C address (A1=0, A0=0) ── */
#define SC16IS752_I2C_ADDR   0x48

/* ── Channel selectors ── */
#define CHANNEL_A   0
#define CHANNEL_B   1

/* ── Register addresses (4-bit reg + 2-bit channel, shifted) ── */
#define REG(reg, ch)    (uint8_t)(((reg) << 3) | ((ch) << 1))

#define RHR   0x00   /* Receive Holding Register  */
#define THR   0x00   /* Transmit Holding Register */
#define IER   0x01   /* Interrupt Enable          */
#define FCR   0x02   /* FIFO Control              */
#define IIR   0x02   /* Interrupt Identification  */
#define LCR   0x03   /* Line Control              */
#define MCR   0x04   /* Modem Control             */
#define LSR   0x05   /* Line Status               */
#define TXLVL 0x0B   /* TX FIFO level             */
#define RXLVL 0x0C   /* RX FIFO level             */
#define DLL   0x00   /* Baud divisor low  (DLAB=1)*/
#define DLH   0x01   /* Baud divisor high (DLAB=1)*/

/* ── LCR bits ── */
#define LCR_WORD_LEN_8   0x03
#define LCR_STOP_1       0x00
#define LCR_PARITY_NONE  0x00
#define LCR_DLAB         0x80

/* ── LSR bits ── */
#define LSR_DATA_READY   0x01
#define LSR_THRE         0x20   /* Transmit Holding Register Empty */
#define LSR_OE           0x02   /* Overrun Error */
#define LSR_PE           0x04   /* Parity Error  */
#define LSR_FE           0x08   /* Framing Error */

/* ── FCR bits ── */
#define FCR_FIFO_EN      0x01
#define FCR_RX_RESET     0x02
#define FCR_TX_RESET     0x04
#define FCR_RX_TRIG_8    0x40   /* RX FIFO trigger at  8 bytes */

/* ────────────────────────────────────────────────────────────── */

typedef struct {
    int   fd;        /* I2C file descriptor */
    int   i2c_addr;
} sc16is752_t;

/* Low-level register write */
static int sc16_write_reg(const sc16is752_t *dev, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    if (write(dev->fd, buf, 2) != 2) {
        perror("sc16_write_reg");
        return -1;
    }
    return 0;
}

/* Low-level register read */
static uint8_t sc16_read_reg(const sc16is752_t *dev, uint8_t reg)
{
    uint8_t sub = reg;
    if (write(dev->fd, &sub, 1) != 1) { perror("sc16_read_reg write"); return 0xFF; }
    uint8_t val = 0;
    if (read(dev->fd, &val, 1) != 1)  { perror("sc16_read_reg read");  return 0xFF; }
    return val;
}

/* Open I2C bus and initialise handle */
int sc16_open(sc16is752_t *dev, const char *i2c_bus, int i2c_addr)
{
    dev->i2c_addr = i2c_addr;
    dev->fd = open(i2c_bus, O_RDWR);
    if (dev->fd < 0) { perror("open i2c bus"); return -1; }
    if (ioctl(dev->fd, I2C_SLAVE, i2c_addr) < 0) {
        perror("ioctl I2C_SLAVE"); close(dev->fd); return -1;
    }
    return 0;
}

/*
 * Initialise one channel at a given baud rate.
 * crystal_hz: external crystal frequency (e.g. 1843200 or 14745600).
 */
int sc16_init_channel(const sc16is752_t *dev, int ch, uint32_t baud, uint32_t crystal_hz)
{
    /* 1. Enable and reset FIFOs */
    sc16_write_reg(dev, REG(FCR, ch),
                   FCR_FIFO_EN | FCR_RX_RESET | FCR_TX_RESET | FCR_RX_TRIG_8);

    /* 2. Set DLAB to access baud divisor registers */
    sc16_write_reg(dev, REG(LCR, ch), LCR_DLAB);

    /* Divisor = crystal / (16 * baud) */
    uint16_t div = (uint16_t)(crystal_hz / (16 * baud));
    sc16_write_reg(dev, REG(DLL, ch), (uint8_t)(div & 0xFF));
    sc16_write_reg(dev, REG(DLH, ch), (uint8_t)(div >> 8));

    /* 3. Clear DLAB; configure 8N1 */
    sc16_write_reg(dev, REG(LCR, ch),
                   LCR_WORD_LEN_8 | LCR_STOP_1 | LCR_PARITY_NONE);

    /* 4. Disable all interrupts (polled mode) */
    sc16_write_reg(dev, REG(IER, ch), 0x00);

    return 0;
}

/* Write a buffer to the UART TX FIFO (blocking, byte-by-byte poll) */
int sc16_write(const sc16is752_t *dev, int ch, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        /* Wait for THRE – TX holding register empty */
        uint8_t lsr;
        do {
            lsr = sc16_read_reg(dev, REG(LSR, ch));
        } while (!(lsr & LSR_THRE));

        sc16_write_reg(dev, REG(THR, ch), data[i]);
    }
    return (int)len;
}

/* Read up to `len` bytes from the RX FIFO (non-blocking) */
int sc16_read(const sc16is752_t *dev, int ch, uint8_t *buf, size_t len)
{
    uint8_t avail = sc16_read_reg(dev, REG(RXLVL, ch));
    if (avail == 0) return 0;
    if (avail > len) avail = (uint8_t)len;

    for (uint8_t i = 0; i < avail; i++) {
        uint8_t lsr = sc16_read_reg(dev, REG(LSR, ch));
        if (lsr & (LSR_OE | LSR_PE | LSR_FE)) {
            fprintf(stderr, "UART channel %d line error: LSR=0x%02X\n", ch, lsr);
        }
        buf[i] = sc16_read_reg(dev, REG(RHR, ch));
    }
    return avail;
}

/* ── Example usage ── */
int main(void)
{
    sc16is752_t dev;
    if (sc16_open(&dev, "/dev/i2c-1", SC16IS752_I2C_ADDR) < 0)
        return EXIT_FAILURE;

    /* Crystal: 14.7456 MHz → clean 115200 baud divisor of 8 */
    sc16_init_channel(&dev, CHANNEL_A, 115200, 14745600);
    sc16_init_channel(&dev, CHANNEL_B,   9600, 14745600);

    /* Send "Hello" on Channel A */
    const char *msg = "Hello from Channel A!\r\n";
    sc16_write(&dev, CHANNEL_A, (const uint8_t *)msg, strlen(msg));

    /* Poll Channel B for incoming data */
    uint8_t rxbuf[64] = {0};
    int n = sc16_read(&dev, CHANNEL_B, rxbuf, sizeof(rxbuf));
    if (n > 0) printf("Channel B received %d bytes: %.*s\n", n, n, rxbuf);

    close(dev.fd);
    return EXIT_SUCCESS;
}
```

---

### MAX3107 via SPI (C/C++)

```cpp
/*
 * max3107.cpp  –  MAX3107 single UART over SPI
 * Uses Linux SPI userspace interface (/dev/spidev*)
 *
 * Build:  g++ -std=c++17 -o max3107_demo max3107.cpp
 */

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

/* ── Register addresses ── */
namespace MAX3107_REG {
    static constexpr uint8_t RHR      = 0x00;  /* RX data           */
    static constexpr uint8_t THR      = 0x00;  /* TX data           */
    static constexpr uint8_t IRQEN    = 0x01;  /* IRQ enable        */
    static constexpr uint8_t ISR      = 0x02;  /* IRQ status        */
    static constexpr uint8_t LSR_IRQ  = 0x03;  /* Line status IRQ   */
    static constexpr uint8_t LSR      = 0x04;  /* Line status       */
    static constexpr uint8_t SPCR     = 0x06;  /* Special purpose   */
    static constexpr uint8_t LCR      = 0x0B;  /* Line control      */
    static constexpr uint8_t DIVLSB   = 0x1C;  /* Baud divisor LSB  */
    static constexpr uint8_t DIVMSB   = 0x1D;  /* Baud divisor MSB  */
    static constexpr uint8_t CLKSRC   = 0x1E;  /* Clock source      */
    static constexpr uint8_t TXFIFOLVL= 0x10;  /* TX FIFO level     */
    static constexpr uint8_t RXFIFOLVL= 0x11;  /* RX FIFO level     */
    static constexpr uint8_t FIFOCTRL = 0x07;  /* FIFO control      */
    static constexpr uint8_t FLOWCTRL = 0x08;  /* Flow control      */
}

/* ── SPI header bit ── */
static constexpr uint8_t SPI_READ_FLAG  = 0x80;
static constexpr uint8_t SPI_WRITE_FLAG = 0x00;

class MAX3107 {
public:
    explicit MAX3107(const char *spidev, uint32_t speed_hz = 4'000'000)
    {
        fd_ = open(spidev, O_RDWR);
        if (fd_ < 0) throw std::runtime_error("Cannot open SPI device");

        uint8_t  mode  = SPI_MODE_0;
        uint8_t  bits  = 8;
        uint32_t speed = speed_hz;
        ioctl(fd_, SPI_IOC_WR_MODE,           &mode);
        ioctl(fd_, SPI_IOC_WR_BITS_PER_WORD,  &bits);
        ioctl(fd_, SPI_IOC_WR_MAX_SPEED_HZ,   &speed);
    }

    ~MAX3107() { if (fd_ >= 0) close(fd_); }

    /* Write a single register */
    void write_reg(uint8_t reg, uint8_t val)
    {
        uint8_t tx[2] = { (uint8_t)(SPI_WRITE_FLAG | reg), val };
        spi_transfer(tx, nullptr, 2);
    }

    /* Read a single register */
    uint8_t read_reg(uint8_t reg)
    {
        uint8_t tx[2] = { (uint8_t)(SPI_READ_FLAG | reg), 0x00 };
        uint8_t rx[2] = {};
        spi_transfer(tx, rx, 2);
        return rx[1];
    }

    /* Burst-read `len` bytes from THR (TX FIFO) / RHR (RX FIFO) */
    void burst_read(uint8_t reg, uint8_t *dst, size_t len)
    {
        std::vector<uint8_t> tx(len + 1, 0x00);
        std::vector<uint8_t> rx(len + 1, 0x00);
        tx[0] = SPI_READ_FLAG | reg;
        spi_transfer(tx.data(), rx.data(), len + 1);
        memcpy(dst, rx.data() + 1, len);
    }

    /*
     * Initialise UART: baud rate using internal PLL (MAX3107 can use
     * its internal 3.6864 MHz oscillator × PLL to produce desired freq).
     *
     * For simplicity, use external 3.6864 MHz crystal and set divisor.
     * crystal_hz = 3686400, example baud = 115200 → divisor = 2
     */
    void init_uart(uint32_t baud, uint32_t crystal_hz)
    {
        using namespace MAX3107_REG;

        /* Select crystal (CLKSRC bit 0 = 0: external crystal) */
        write_reg(CLKSRC, 0x00);

        /* Set baud divisor: DLL:DLH = crystal / (16 × baud) */
        uint16_t div = static_cast<uint16_t>(crystal_hz / (16 * baud));
        write_reg(DIVLSB, static_cast<uint8_t>(div & 0xFF));
        write_reg(DIVMSB, static_cast<uint8_t>(div >> 8));

        /* 8N1: LCR = 0b00000011 (8-bit, no parity, 1 stop) */
        write_reg(LCR, 0x03);

        /* Enable FIFOs */
        write_reg(FIFOCTRL, 0x01);

        /* Disable interrupts (polled mode) */
        write_reg(IRQEN, 0x00);
    }

    /* Send bytes (blocking poll on TX FIFO space) */
    void send(const uint8_t *data, size_t len)
    {
        using namespace MAX3107_REG;
        for (size_t i = 0; i < len; i++) {
            /* Wait until TX FIFO has at least 1 free byte */
            while (read_reg(TXFIFOLVL) >= 128)
                usleep(100);
            write_reg(THR, data[i]);
        }
    }

    /* Read available bytes from RX FIFO */
    int recv(uint8_t *buf, size_t maxlen)
    {
        using namespace MAX3107_REG;
        uint8_t avail = read_reg(RXFIFOLVL);
        if (avail == 0) return 0;
        size_t n = (avail < maxlen) ? avail : maxlen;
        burst_read(RHR, buf, n);
        return static_cast<int>(n);
    }

private:
    int fd_{-1};

    void spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
    {
        spi_ioc_transfer tr{};
        tr.tx_buf        = reinterpret_cast<uintptr_t>(tx);
        tr.rx_buf        = reinterpret_cast<uintptr_t>(rx);
        tr.len           = static_cast<uint32_t>(len);
        tr.speed_hz      = 4'000'000;
        tr.bits_per_word = 8;
        if (ioctl(fd_, SPI_IOC_MESSAGE(1), &tr) < 0)
            throw std::runtime_error("SPI transfer failed");
    }

    // required for burst_read vector
    #include <vector>
};

int main()
{
    MAX3107 uart("/dev/spidev0.0");
    uart.init_uart(115200, 3686400);

    const char *hello = "Hello from MAX3107!\r\n";
    uart.send(reinterpret_cast<const uint8_t *>(hello), strlen(hello));

    uint8_t rxbuf[128];
    int n = uart.recv(rxbuf, sizeof(rxbuf));
    if (n > 0) printf("Received: %.*s\n", n, rxbuf);

    return 0;
}
```

---

### Linux sysfs / termios Integration (C)

On Linux, the kernel driver `sc16is7xx` exposes SC16IS7xx channels as standard `/dev/ttySC*` devices. Once the device tree overlay is loaded, you can use normal POSIX `termios` calls:

```c
/*
 * termios_sc16.c – Access an SC16IS752 channel as /dev/ttyS* on Linux
 * The sc16is7xx kernel driver must be loaded (Device Tree overlay).
 *
 * Build:  gcc -o termios_demo termios_sc16.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <unistd.h>

static int open_serial(const char *device, int baud_const)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) { perror(device); return -1; }

    struct termios tty;
    memset(&tty, 0, sizeof tty);
    if (tcgetattr(fd, &tty) != 0) { perror("tcgetattr"); close(fd); return -1; }

    cfsetispeed(&tty, baud_const);
    cfsetospeed(&tty, baud_const);

    /* 8N1, no flow control */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;
    tty.c_iflag = IGNPAR | IGNBRK;
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    /* Non-blocking read, return immediately if no data */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  /* 100 ms timeout */

    tcflush(fd, TCIOFLUSH);
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr"); close(fd); return -1;
    }
    return fd;
}

int main(void)
{
    /*
     * With the sc16is7xx kernel driver and DT overlay:
     *   Channel A → /dev/ttySC0
     *   Channel B → /dev/ttySC1
     */
    int fd_a = open_serial("/dev/ttySC0", B115200);
    int fd_b = open_serial("/dev/ttySC1",   B9600);

    if (fd_a < 0 || fd_b < 0) return EXIT_FAILURE;

    /* Write to channel A */
    const char *msg = "AT\r\n";
    write(fd_a, msg, strlen(msg));

    /* Read response on channel A (modem response) */
    char rx[64];
    int n = read(fd_a, rx, sizeof(rx) - 1);
    if (n > 0) {
        rx[n] = '\0';
        printf("Response: %s\n", rx);
    }

    close(fd_a);
    close(fd_b);
    return EXIT_SUCCESS;
}
```

Device Tree snippet for Raspberry Pi:

```dts
/* sc16is752-overlay.dts */
&i2c1 {
    status = "okay";
    sc16is752: sc16is752@48 {
        compatible    = "nxp,sc16is752";
        reg           = <0x48>;
        clocks        = <&sc16is752_clk>;
        interrupt-parent = <&gpio>;
        interrupts    = <24 2>;       /* GPIO24, falling edge */
        #gpio-cells   = <2>;
    };
};
```

---

## Programming in Rust

### SC16IS752 via I2C (Rust)

```rust
//! sc16is752.rs – SC16IS752 UART expander driver for Linux I2C
//!
//! Cargo.toml dependencies:
//!   i2cdev  = "0.6"
//!   thiserror = "1"

use i2cdev::core::I2CDevice;
use i2cdev::linux::LinuxI2CDevice;
use std::time::Duration;
use std::thread;

// ── Register helpers ────────────────────────────────────────────
const fn reg(register: u8, channel: u8) -> u8 {
    (register << 3) | (channel << 1)
}

#[allow(dead_code)]
mod regs {
    pub const RHR:   u8 = 0x00;
    pub const THR:   u8 = 0x00;
    pub const IER:   u8 = 0x01;
    pub const FCR:   u8 = 0x02;
    pub const IIR:   u8 = 0x02;
    pub const LCR:   u8 = 0x03;
    pub const MCR:   u8 = 0x04;
    pub const LSR:   u8 = 0x05;
    pub const MSR:   u8 = 0x06;
    pub const TXLVL: u8 = 0x0B;
    pub const RXLVL: u8 = 0x0C;
    pub const DLL:   u8 = 0x00; // DLAB = 1
    pub const DLH:   u8 = 0x01; // DLAB = 1
}

mod lsr_bits {
    pub const DATA_READY: u8 = 0x01;
    pub const THRE:       u8 = 0x20;
    pub const OE:         u8 = 0x02;
    pub const PE:         u8 = 0x04;
    pub const FE:         u8 = 0x08;
}

mod lcr_bits {
    pub const WORD_8:    u8 = 0x03;
    pub const STOP_1:    u8 = 0x00;
    pub const NO_PARITY: u8 = 0x00;
    pub const DLAB:      u8 = 0x80;
}

mod fcr_bits {
    pub const FIFO_EN:   u8 = 0x01;
    pub const RX_RESET:  u8 = 0x02;
    pub const TX_RESET:  u8 = 0x04;
    pub const RX_TRIG_8: u8 = 0x40;
}

// ── Error type ───────────────────────────────────────────────────
#[derive(Debug, thiserror::Error)]
pub enum UartError {
    #[error("I2C error: {0}")]
    I2c(#[from] i2cdev::linux::LinuxI2CError),
    #[error("Channel {0} line error: LSR = 0x{1:02X}")]
    LineError(u8, u8),
}

pub type Result<T> = std::result::Result<T, UartError>;

// ── Driver struct ────────────────────────────────────────────────
pub struct Sc16is752 {
    i2c: LinuxI2CDevice,
}

impl Sc16is752 {
    /// Open the device on the given I2C bus and address.
    pub fn new(bus: &str, address: u16) -> Result<Self> {
        let i2c = LinuxI2CDevice::new(bus, address)?;
        Ok(Self { i2c })
    }

    /// Write a value to a register on `channel` (0 = A, 1 = B).
    pub fn write_reg(&mut self, register: u8, channel: u8, value: u8) -> Result<()> {
        let sub = reg(register, channel);
        self.i2c.smbus_write_byte_data(sub, value)?;
        Ok(())
    }

    /// Read a register on `channel`.
    pub fn read_reg(&mut self, register: u8, channel: u8) -> Result<u8> {
        let sub = reg(register, channel);
        let v = self.i2c.smbus_read_byte_data(sub)?;
        Ok(v)
    }

    /// Initialise a UART channel to the given baud rate.
    ///
    /// `crystal_hz`: external crystal frequency in Hz
    ///   (e.g. 14_745_600 for a clean 115200 divisor of 8)
    pub fn init_channel(&mut self, ch: u8, baud: u32, crystal_hz: u32) -> Result<()> {
        use regs::*;
        use fcr_bits::*;
        use lcr_bits::*;

        // Enable and reset FIFOs
        self.write_reg(FCR, ch, FIFO_EN | RX_RESET | TX_RESET | RX_TRIG_8)?;

        // Set DLAB to access divisor registers
        self.write_reg(LCR, ch, DLAB)?;
        let div = crystal_hz / (16 * baud);
        self.write_reg(DLL, ch, (div & 0xFF) as u8)?;
        self.write_reg(DLH, ch, (div >> 8) as u8)?;

        // Clear DLAB, configure 8N1
        self.write_reg(LCR, ch, WORD_8 | STOP_1 | NO_PARITY)?;

        // Disable interrupts (polled mode)
        self.write_reg(IER, ch, 0x00)?;

        Ok(())
    }

    /// Transmit a byte slice on `channel` (blocking).
    pub fn write(&mut self, ch: u8, data: &[u8]) -> Result<()> {
        for &byte in data {
            // Wait for Transmit Holding Register Empty
            loop {
                let lsr = self.read_reg(regs::LSR, ch)?;
                if lsr & lsr_bits::THRE != 0 { break; }
                thread::sleep(Duration::from_micros(10));
            }
            self.write_reg(regs::THR, ch, byte)?;
        }
        Ok(())
    }

    /// Receive available bytes from `channel` (non-blocking).
    ///
    /// Returns the number of bytes placed into `buf`.
    pub fn read(&mut self, ch: u8, buf: &mut [u8]) -> Result<usize> {
        let avail = self.read_reg(regs::RXLVL, ch)? as usize;
        let n = avail.min(buf.len());
        for slot in buf.iter_mut().take(n) {
            let lsr = self.read_reg(regs::LSR, ch)?;
            if lsr & (lsr_bits::OE | lsr_bits::PE | lsr_bits::FE) != 0 {
                return Err(UartError::LineError(ch, lsr));
            }
            *slot = self.read_reg(regs::RHR, ch)?;
        }
        Ok(n)
    }
}

// ── Example entry point ──────────────────────────────────────────
fn main() -> Result<()> {
    let mut uart = Sc16is752::new("/dev/i2c-1", 0x48)?;

    // Crystal: 14.7456 MHz – divisor 8 → exact 115 200 baud
    uart.init_channel(0, 115_200, 14_745_600)?;
    uart.init_channel(1,   9_600, 14_745_600)?;

    // Transmit on channel A
    uart.write(0, b"Hello from Rust on Channel A!\r\n")?;

    // Receive on channel B
    let mut rx = [0u8; 64];
    let n = uart.read(1, &mut rx)?;
    if n > 0 {
        println!("Channel B got: {:?}", std::str::from_utf8(&rx[..n]));
    }
    Ok(())
}
```

---

### SPI UART Expander (Rust)

```rust
//! max3107_spi.rs – MAX3107 single UART over Linux SPI
//!
//! Cargo.toml dependencies:
//!   spidev   = "0.6"
//!   thiserror = "1"

use spidev::{Spidev, SpidevOptions, SpidevTransfer, SpiModeFlags};
use std::io;

const READ_FLAG: u8  = 0x80;
const WRITE_FLAG: u8 = 0x00;

mod reg {
    pub const RHR:      u8 = 0x00;
    pub const THR:      u8 = 0x00;
    pub const IRQEN:    u8 = 0x01;
    pub const LCR:      u8 = 0x0B;
    pub const DIVLSB:   u8 = 0x1C;
    pub const DIVMSB:   u8 = 0x1D;
    pub const CLKSRC:   u8 = 0x1E;
    pub const FIFOCTRL: u8 = 0x07;
    pub const TXLVL:    u8 = 0x10;
    pub const RXLVL:    u8 = 0x11;
}

pub struct Max3107 {
    spi: Spidev,
}

impl Max3107 {
    pub fn new(device: &str) -> io::Result<Self> {
        let mut spi = Spidev::open(device)?;
        let opts = SpidevOptions::new()
            .bits_per_word(8)
            .max_speed_hz(4_000_000)
            .mode(SpiModeFlags::SPI_MODE_0)
            .build();
        spi.configure(&opts)?;
        Ok(Self { spi })
    }

    pub fn write_reg(&mut self, register: u8, value: u8) -> io::Result<()> {
        let tx = [WRITE_FLAG | register, value];
        let mut rx = [0u8; 2];
        let mut transfer = SpidevTransfer::read_write(&tx, &mut rx);
        self.spi.transfer(&mut transfer)?;
        Ok(())
    }

    pub fn read_reg(&mut self, register: u8) -> io::Result<u8> {
        let tx = [READ_FLAG | register, 0x00];
        let mut rx = [0u8; 2];
        let mut transfer = SpidevTransfer::read_write(&tx, &mut rx);
        self.spi.transfer(&mut transfer)?;
        Ok(rx[1])
    }

    /// Burst read `n` bytes from `register` (RHR for RX data).
    pub fn burst_read(&mut self, register: u8, n: usize) -> io::Result<Vec<u8>> {
        let tx: Vec<u8> = std::iter::once(READ_FLAG | register)
            .chain(std::iter::repeat(0x00).take(n))
            .collect();
        let mut rx = vec![0u8; tx.len()];
        let mut transfer = SpidevTransfer::read_write(&tx, &mut rx);
        self.spi.transfer(&mut transfer)?;
        Ok(rx[1..].to_vec())
    }

    pub fn init_uart(&mut self, baud: u32, crystal_hz: u32) -> io::Result<()> {
        // External crystal, no PLL
        self.write_reg(reg::CLKSRC, 0x00)?;

        let div = crystal_hz / (16 * baud);
        self.write_reg(reg::DIVLSB, (div & 0xFF) as u8)?;
        self.write_reg(reg::DIVMSB, (div >> 8)   as u8)?;

        // 8N1
        self.write_reg(reg::LCR, 0x03)?;

        // Enable FIFOs
        self.write_reg(reg::FIFOCTRL, 0x01)?;

        // Disable interrupts
        self.write_reg(reg::IRQEN, 0x00)?;

        Ok(())
    }

    pub fn send(&mut self, data: &[u8]) -> io::Result<()> {
        for &byte in data {
            // Poll TX FIFO level – MAX3107 has 128-byte FIFO
            while self.read_reg(reg::TXLVL)? >= 128 {
                std::thread::sleep(std::time::Duration::from_micros(50));
            }
            self.write_reg(reg::THR, byte)?;
        }
        Ok(())
    }

    pub fn recv(&mut self, max: usize) -> io::Result<Vec<u8>> {
        let avail = self.read_reg(reg::RXLVL)? as usize;
        if avail == 0 { return Ok(vec![]); }
        let n = avail.min(max);
        self.burst_read(reg::RHR, n)
    }
}

fn main() -> io::Result<()> {
    let mut uart = Max3107::new("/dev/spidev0.0")?;
    uart.init_uart(115_200, 3_686_400)?;

    uart.send(b"Hello from MAX3107 in Rust!\r\n")?;

    let rx = uart.recv(128)?;
    if !rx.is_empty() {
        println!("Received: {:?}", String::from_utf8_lossy(&rx));
    }
    Ok(())
}
```

---

## Interrupt-Driven Operation

Polling the RXLVL register is simple but wastes CPU cycles. Most expander ICs provide an active-low `IRQ` or `INT` pin. Connect it to a GPIO with interrupt capability:

```c
/* Interrupt-driven SC16IS752 skeleton (Linux GPIO + epoll) */
#include <gpiod.h>
#include <sys/epoll.h>

/* IER bit: enable RX data available interrupt */
#define IER_RHR_INT  0x01

void sc16_enable_rx_interrupt(const sc16is752_t *dev, int ch)
{
    sc16_write_reg(dev, REG(IER, ch), IER_RHR_INT);
}

/*
 * In your main loop:
 *   1. Configure GPIO for falling-edge interrupt using gpiod_line_request_falling_edge_events()
 *   2. Monitor with epoll / poll / select
 *   3. On event: read IIR to identify source, drain RX FIFO, clear interrupt
 */
uint8_t sc16_handle_irq(sc16is752_t *dev, int ch, uint8_t *rxbuf, size_t buflen)
{
    uint8_t iir = sc16_read_reg(dev, REG(IIR, ch));
    /* IIR[2:1] = 10 means RX data available */
    if ((iir & 0x06) == 0x04) {
        return (uint8_t)sc16_read(dev, ch, rxbuf, buflen);
    }
    return 0;
}
```

Rust equivalent with `gpio-cdev` crate:

```rust
use gpio_cdev::{Chip, EventType, LineRequestFlags};

fn wait_for_uart_irq(chip: &mut Chip, gpio_offset: u32) {
    let line = chip.get_line(gpio_offset).unwrap();
    let evt = line
        .events(
            LineRequestFlags::INPUT,
            EventType::FallingEdge,
            "uart-irq",
        )
        .unwrap();

    for event in evt {
        let _e = event.unwrap();
        // Handle IRQ: read IIR, drain FIFO
        println!("UART IRQ fired!");
        break;
    }
}
```

---

## Buffering and Flow Control

### Software Flow Control (XON/XOFF)

Both SC16IS7xx and MAX3107 support configurable XON/XOFF characters:

```c
/* SC16IS752: configure software flow control via EFR / XON1 / XOFF1 */
#define EFR  0x02  /* Enhanced Feature Register (accessible when LCR=0xBF) */

void sc16_enable_sw_flowctrl(const sc16is752_t *dev, int ch)
{
    /* Access Enhanced Registers: LCR must be 0xBF */
    sc16_write_reg(dev, REG(LCR, ch), 0xBF);
    uint8_t efr = sc16_read_reg(dev, REG(EFR, ch));
    efr |= (1 << 1) | (1 << 0); /* Enable TX/RX XON/XOFF */
    sc16_write_reg(dev, REG(EFR, ch), efr);
    /* Restore LCR */
    sc16_write_reg(dev, REG(LCR, ch), LCR_WORD_LEN_8);
}
```

### Hardware Flow Control (RTS/CTS)

```c
/* Enable auto-RTS / auto-CTS in the MCR / EFR */
#define MCR_TCRTLR 0x04  /* Access TLR register     */
#define EFR_CTS_EN 0x80  /* Auto-CTS enable          */
#define EFR_RTS_EN 0x40  /* Auto-RTS enable          */

void sc16_enable_hw_flowctrl(const sc16is752_t *dev, int ch)
{
    /* Enable Enhanced Functions and set auto RTS/CTS */
    sc16_write_reg(dev, REG(LCR, ch), 0xBF);
    uint8_t efr = sc16_read_reg(dev, REG(EFR, ch));
    efr |= EFR_CTS_EN | EFR_RTS_EN | 0x10; /* bit 4: Enhanced Functions Enable */
    sc16_write_reg(dev, REG(EFR, ch), efr);
    sc16_write_reg(dev, REG(LCR, ch), LCR_WORD_LEN_8);
}
```

---

## Common Pitfalls

| Pitfall | Cause | Fix |
|---|---|---|
| **Garbled data** | Wrong crystal frequency in divisor calculation | Verify crystal frequency with oscilloscope; use known-good values (14.7456 MHz → exact 115200) |
| **I2C NAK on first byte** | Wrong I2C address (A1/A0 pin states) | Check PCB pull-up resistors; address = 0x48 + 2×A1 + A0 |
| **TX bytes lost** | Not waiting for THRE before writing | Always poll THRE or check TXLVL before loading THR |
| **RX FIFO overflow** | RXLVL not drained quickly enough | Enable interrupts or increase polling frequency; use DMA on high-throughput systems |
| **Channel B unresponsive** | Sub-address channel bit miscalculated | Double-check: channel A = bit[2:1] = 00, channel B = bit[2:1] = 10 |
| **SPI data corruption** | SPI mode mismatch (CPOL/CPHA) | SC16IS752 SPI uses Mode 0; MAX3107 uses Mode 0; verify with logic analyser |
| **Baud rate off by factor** | Prescaler not set to 1 | MCR[7] (Clock divisor): set to 0 for ÷1, 1 for ÷4; default is ÷1 |
| **Interrupt never fires** | IRQ pin is open-drain, no pull-up | Add 4.7 kΩ pull-up resistor to the IRQ line |

---

## Summary

UART port expanders let embedded and Linux-based systems **multiply their serial port count** with minimal host pin usage — a single I2C bus can support up to eight SC16IS752 devices (16 additional UART channels), while an SPI bus can chain multiple MAX3107/3109 devices.

**Key takeaways:**

- **I2C expanders** (SC16IS752) are ideal when pin count is critical; they add two full UART channels at the cost of just two signal wires (SDA/SCL) plus an optional IRQ line. Address pins allow up to four devices per bus.
- **SPI expanders** (MAX3107/3109) offer higher throughput (up to 24 Mbps baud) and more deterministic timing, making them preferable for high-speed or real-time applications.
- **Register access** follows a consistent pattern: set the sub-address (or SPI header byte), then read/write data registers. Baud rate is programmed via a divisor loaded when the DLAB bit is set in LCR.
- **Interrupt-driven operation** dramatically reduces CPU overhead compared to polling, especially when multiple channels are active simultaneously.
- **Hardware flow control** (RTS/CTS) can be offloaded entirely to the expander IC, ensuring no data loss at high baud rates without CPU involvement.
- On Linux, the **`sc16is7xx` kernel driver** abstracts the hardware completely, exposing `/dev/ttySC*` devices that work identically to native UARTs via standard `termios` APIs.
- In **Rust**, the `i2cdev` and `spidev` crates map cleanly to the underlying Linux subsystems, enabling idiomatic, safe driver code with the `embedded-hal` trait system available for bare-metal targets.

> **Rule of thumb:** choose an expander crystal whose frequency is an exact integer multiple of 16 × baud-rate (e.g. 14.7456 MHz for standard baud rates up to 921600) to eliminate baud-rate error entirely.