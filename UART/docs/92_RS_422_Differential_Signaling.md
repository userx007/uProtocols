# 92. RS-422 Differential Signaling

**Conceptual coverage** — differential signaling fundamentals (A−B voltage, common-mode noise cancellation), a side-by-side RS-232 vs RS-422 comparison table, electrical specs (voltage levels, speed/distance trade-off table), cabling requirements, UART frame structure, and common transceiver ICs.

**C/C++ examples** include a `termios`-based port open/configure function, write/read helpers with proper retry loops and `tcdrain()`, a complete framed-packet echo program (SOF + length + CRC-8), and a C++ RAII `Rs422Port` class.

**Rust examples** use the `serialport` crate with a `Builder`-pattern config struct, `write_all`/`read_exact` helpers, and a full framed-protocol module using `thiserror` for typed errors.

**Practical guidance** covers multi-drop topology wiring, line termination (why and where to place the 120 Ω resistor), fail-safe biasing, and a troubleshooting table for common symptoms.

## Long-Distance Point-to-Point Communication with RS-422

---

## Table of Contents

1. [Introduction](#introduction)
2. [RS-422 vs RS-232: Key Differences](#rs-422-vs-rs-232-key-differences)
3. [Electrical Characteristics](#electrical-characteristics)
4. [Physical Layer and Cabling](#physical-layer-and-cabling)
5. [RS-422 Frame and Timing](#rs-422-frame-and-timing)
6. [Driver and Receiver ICs](#driver-and-receiver-ics)
7. [Programming RS-422 in C/C++](#programming-rs-422-in-cc)
8. [Programming RS-422 in Rust](#programming-rs-422-in-rust)
9. [Multi-Drop Topologies](#multi-drop-topologies)
10. [Error Handling and Termination](#error-handling-and-termination)
11. [Summary](#summary)

---

## Introduction

RS-422 (formally TIA/EIA-422-B) is a serial communication standard that uses **balanced differential signaling** to achieve high-speed, long-distance, noise-immune data transmission. It was developed as a successor to RS-232 to overcome the latter's strict distance (≤15 m) and speed (≤20 kbps) limitations.

RS-422 is widely used in industrial automation, machine control, professional audio/video equipment, avionics, medical instrumentation, and any environment where cables run long distances through electrically noisy surroundings.

### Core Concept: Differential Signaling

In RS-232, data is represented as a voltage relative to a common ground. In RS-422, data is carried as the **voltage difference between two wires** (A and B, sometimes labeled + and −). The receiver only cares about whether A−B > 0 (logic 1) or A−B < 0 (logic 0). Any noise picked up equally on both conductors (common-mode noise) is automatically cancelled, which is the fundamental reason for RS-422's superior noise immunity.

```
       Driver                        Receiver
  ┌─────────────┐     Wire A (+)   ┌─────────────┐
  │  TX Logic   ├────────────────►─┤  +          │
  │             │     Wire B (−)   │      ─►  RX │
  │             ├────────────────►─┤  −          │
  └─────────────┘                  └─────────────┘
        Differential output: A − B
        Logic 1: A > B  (typically +2V to +6V differential)
        Logic 0: B > A  (typically −2V to −6V differential)
```

---

## RS-422 vs RS-232: Key Differences

| Feature               | RS-232           | RS-422                     |
|-----------------------|------------------|----------------------------|
| Signaling             | Single-ended     | Differential (balanced)    |
| Max cable length      | ~15 m            | Up to 1200 m               |
| Max data rate         | 20 kbps          | 10 Mbps (short cables)     |
| Noise immunity        | Low              | Excellent (CMRR)           |
| Number of drivers     | 1                | 1 driver, up to 10 receivers |
| Voltage levels        | ±3V to ±15V      | ±2V min differential       |
| Ground reference      | Required         | Not required (isolated possible) |
| Topology              | Point-to-point   | Point-to-point or multi-drop |
| Typical use           | PC peripherals   | Industrial, long runs      |

---

## Electrical Characteristics

### Voltage Levels

| Parameter                        | Value              |
|----------------------------------|--------------------|
| Driver output voltage (loaded)   | ±2.0 V min         |
| Driver output voltage (unloaded) | ±6.0 V max         |
| Receiver input sensitivity       | ±200 mV            |
| Receiver input range             | −7 V to +12 V      |
| Common-mode voltage range        | −7 V to +7 V       |
| Fail-safe voltage (open line)    | Defined by biasing |

A receiver must detect a differential signal as small as **±200 mV**, giving a comfortable 10× margin when a driver outputs ±2 V.

### Speed vs. Distance Trade-off

RS-422 operates on the principle that the bit period must be much longer than the signal propagation time along the cable (to avoid reflections distorting bits):

| Cable Length | Max Reliable Data Rate |
|--------------|------------------------|
| 1200 m       | ~100 kbps              |
| 300 m        | ~1 Mbps                |
| 30 m         | ~10 Mbps               |
| 1 m          | ~35 Mbps (driver limit)|

The rule of thumb: **data rate (bps) × distance (m) ≤ 1×10⁸**.

---

## Physical Layer and Cabling

### Cable Requirements

- Use **twisted-pair cable** — the twist rejects common-mode noise.
- Characteristic impedance of the cable should match the termination resistor (typically **100–120 Ω** for balanced pairs).
- Shielded cable is preferred in high-EMI environments; connect shield to chassis ground at **one end only** to avoid ground loops.
- Each differential pair (TX+/TX−, RX+/RX−) should be on its own twisted pair.

### Connector Pinout (Typical DB-9 RS-422 Assignment)

```
 DB-9 Pin  Signal     Direction
 ────────  ─────────  ─────────
    1       GND        —
    2       TX−  (B)   Output
    3       TX+  (A)   Output
    4       RX+  (A)   Input
    5       RX−  (B)   Input
    6       RTS−       Output (optional flow control)
    7       RTS+       Output (optional flow control)
    8       CTS+       Input  (optional flow control)
    9       CTS−       Input  (optional flow control)
```

### Full-Duplex 4-Wire Wiring Diagram

```
  Host (DTE)                         Remote Device
  ──────────                         ─────────────
  TX+  ────────────────────────────► RX+
  TX−  ────────────────────────────► RX−
  RX+  ◄──────────────────────────── TX+
  RX−  ◄──────────────────────────── TX−
  GND  ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ GND (optional reference)
                    RT (100Ω)
                    across RX+/RX− at each end
```

---

## RS-422 Frame and Timing

RS-422 defines only the **physical/electrical layer**. The framing is identical to standard UART asynchronous serial:

```
  Idle  Start  D0   D1   D2   D3   D4   D5   D6   D7  Parity Stop
  ─────┐      ┌────┬────┬────┬────┬────┬────┬────┬────┬──────┐─────
       │      │    │    │    │    │    │    │    │    │      │
       └──────┘    │    │    │    │    │    │    │    │      └─────
  (Mark)  (Space)  └────┴────┴────┴────┴────┴────┴────┘
                          8 data bits (example)
```

- **Idle state**: Line held at Mark (A > B, differential positive).
- **Start bit**: One bit of Space (B > A, differential negative).
- **Data bits**: LSB first, 5–9 bits configurable.
- **Parity bit**: Optional — Even, Odd, Mark, Space, or None.
- **Stop bits**: 1, 1.5, or 2 bits of Mark.

On Linux, the standard `termios` API controls all of these parameters, and RS-422 hardware is presented to software as a normal serial (`/dev/ttyS*` or `/dev/ttyUSB*`) port.

---

## Driver and Receiver ICs

Common RS-422 transceiver chips:

| IC            | Manufacturer | Channels | Notes                            |
|---------------|-------------|----------|----------------------------------|
| MAX490/MAX491 | Maxim        | 1 TX / 1 RX | Standard RS-422/485, 2.5 Mbps |
| SN75176       | TI           | 1 TX / 1 RX | Classic, 5V only               |
| DS26C31/32    | TI           | Quad TX / Quad RX | High-speed, 20 Mbps   |
| ADM2587E      | Analog Devices | 1 full-duplex | Isolated, 500 kbps      |
| SP490         | Sipex        | 1 TX / 1 RX | Low power                      |

Most microcontrollers and SoCs with UART peripherals connect to RS-422 transceivers via simple logic-level TX/RX lines. The transceiver handles the differential conversion; software sees a normal UART.

---

## Programming RS-422 in C/C++

From a software perspective on Linux, RS-422 appears as a standard serial port. The code below works for any RS-422 adapter (USB-to-RS422 dongle, PCI/PCIe serial card, embedded SoC UART + transceiver).

### 1. Opening and Configuring the Port

```c
// rs422_config.c
// Compile: gcc -o rs422_config rs422_config.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

/**
 * Open an RS-422 serial port and configure it.
 *
 * @param device   Path to device, e.g. "/dev/ttyS0" or "/dev/ttyUSB0"
 * @param baudrate Baud rate constant, e.g. B115200
 * @param databits 5–8 (use CS8 for 8)
 * @param parity   'N' = none, 'E' = even, 'O' = odd
 * @param stopbits 1 or 2
 * @return file descriptor, or -1 on error
 */
int rs422_open(const char *device, speed_t baudrate,
               int databits, char parity, int stopbits)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("rs422_open: open");
        return -1;
    }

    // Ensure reads block
    fcntl(fd, F_SETFL, 0);

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("rs422_open: tcgetattr");
        close(fd);
        return -1;
    }

    // ── Baud rate ──────────────────────────────────────────────────────────
    cfsetispeed(&tty, baudrate);
    cfsetospeed(&tty, baudrate);

    // ── Data bits ──────────────────────────────────────────────────────────
    tty.c_cflag &= ~CSIZE;
    switch (databits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default:
        case 8: tty.c_cflag |= CS8; break;
    }

    // ── Parity ─────────────────────────────────────────────────────────────
    if (parity == 'E') {
        tty.c_cflag |=  PARENB;        // enable parity
        tty.c_cflag &= ~PARODD;        // even
    } else if (parity == 'O') {
        tty.c_cflag |=  PARENB;
        tty.c_cflag |=  PARODD;        // odd
    } else {
        tty.c_cflag &= ~PARENB;        // no parity
    }

    // ── Stop bits ──────────────────────────────────────────────────────────
    if (stopbits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    // ── Hardware flow control ──────────────────────────────────────────────
    // RS-422 typically uses no hardware flow control (point-to-point).
    // Enable CRTSCTS only if your adapter wires RTS/CTS for flow control.
    tty.c_cflag &= ~CRTSCTS;

    // ── Receiver enable, ignore modem status lines ─────────────────────────
    tty.c_cflag |= (CREAD | CLOCAL);

    // ── Raw mode (no canonical processing, no echo, no signals) ───────────
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // ── Disable software flow control ─────────────────────────────────────
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    // ── Disable output processing ─────────────────────────────────────────
    tty.c_oflag &= ~OPOST;

    // ── Read timeout: block until at least 1 byte, or 100 ms timeout ──────
    tty.c_cc[VMIN]  = 0;   // 0 = pure timeout mode
    tty.c_cc[VTIME] = 10;  // 10 × 100 ms = 1 second timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("rs422_open: tcsetattr");
        close(fd);
        return -1;
    }

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);
    return fd;
}

void rs422_close(int fd)
{
    if (fd >= 0)
        close(fd);
}
```

### 2. Sending and Receiving Data

```c
// rs422_io.c – read/write helpers

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

/**
 * Write a buffer to the RS-422 port.
 * Returns number of bytes written, or -1 on error.
 */
ssize_t rs422_write(int fd, const uint8_t *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = write(fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;   // retry on signal
            perror("rs422_write");
            return -1;
        }
        total += (size_t)n;
    }
    // Ensure all data is physically transmitted before returning
    tcdrain(fd);
    return (ssize_t)total;
}

/**
 * Read up to max_len bytes with a per-call timeout.
 * Returns bytes read (0 on timeout), or -1 on error.
 */
ssize_t rs422_read(int fd, uint8_t *buf, size_t max_len)
{
    ssize_t n = read(fd, buf, max_len);
    if (n < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK)
            return 0;                       // timeout
        perror("rs422_read");
        return -1;
    }
    return n;
}

/**
 * Read exactly 'len' bytes, retrying until complete or error.
 */
ssize_t rs422_read_exact(int fd, uint8_t *buf, size_t len)
{
    size_t total = 0;
    while (total < len) {
        ssize_t n = read(fd, buf + total, len - total);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("rs422_read_exact");
            return -1;
        }
        if (n == 0) {
            fprintf(stderr, "rs422_read_exact: timeout after %zu/%zu bytes\n",
                    total, len);
            return (ssize_t)total;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}
```

### 3. Complete Point-to-Point Echo Example

```c
// rs422_echo.c – Full example: send a packet, receive echo
// Usage: ./rs422_echo /dev/ttyUSB0 115200

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include "rs422_config.h"   // contains rs422_open / rs422_close
#include "rs422_io.h"       // contains rs422_write / rs422_read_exact

/* Simple framed packet:
 *   [SOF 0xAA] [LEN 1 byte] [PAYLOAD LEN bytes] [CRC8 1 byte]
 */

static uint8_t crc8(const uint8_t *data, size_t len)
{
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++)
            crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
    }
    return crc;
}

#define SOF   0xAA
#define MAXPL 252

ssize_t packet_send(int fd, const uint8_t *payload, uint8_t len)
{
    if (len > MAXPL) return -1;
    uint8_t frame[MAXPL + 3];
    frame[0] = SOF;
    frame[1] = len;
    memcpy(&frame[2], payload, len);
    frame[2 + len] = crc8(payload, len);
    return rs422_write(fd, frame, 3 + len);
}

ssize_t packet_recv(int fd, uint8_t *payload_out, size_t max_out)
{
    uint8_t hdr[2];
    if (rs422_read_exact(fd, hdr, 2) != 2) return -1;
    if (hdr[0] != SOF) {
        fprintf(stderr, "packet_recv: bad SOF 0x%02X\n", hdr[0]);
        return -1;
    }
    uint8_t len = hdr[1];
    if (len > max_out) { fprintf(stderr, "packet_recv: payload too large\n"); return -1; }

    uint8_t buf[MAXPL + 1];
    if (rs422_read_exact(fd, buf, len + 1) != (ssize_t)(len + 1)) return -1;
    uint8_t expected_crc = crc8(buf, len);
    if (buf[len] != expected_crc) {
        fprintf(stderr, "CRC mismatch: got 0x%02X expected 0x%02X\n",
                buf[len], expected_crc);
        return -1;
    }
    memcpy(payload_out, buf, len);
    return len;
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device> <baudrate>\n", argv[0]);
        return 1;
    }

    speed_t speed;
    int baud = atoi(argv[2]);
    switch (baud) {
        case 9600:   speed = B9600;   break;
        case 19200:  speed = B19200;  break;
        case 38400:  speed = B38400;  break;
        case 57600:  speed = B57600;  break;
        case 115200: speed = B115200; break;
        default:
            fprintf(stderr, "Unsupported baud rate: %d\n", baud);
            return 1;
    }

    int fd = rs422_open(argv[1], speed, 8, 'N', 1);
    if (fd < 0) return 1;

    const uint8_t msg[] = "Hello RS-422!";
    printf("Sending: %s\n", msg);

    if (packet_send(fd, msg, sizeof(msg) - 1) < 0) {
        fprintf(stderr, "Send failed\n");
        rs422_close(fd);
        return 1;
    }

    uint8_t reply[MAXPL];
    ssize_t rlen = packet_recv(fd, reply, sizeof(reply));
    if (rlen < 0) {
        fprintf(stderr, "Receive failed\n");
    } else {
        reply[rlen] = '\0';
        printf("Echo received (%zd bytes): %s\n", rlen, (char *)reply);
    }

    rs422_close(fd);
    return 0;
}
```

### 4. C++ Class Wrapper

```cpp
// Rs422Port.hpp – C++ RAII wrapper for an RS-422 serial port
#pragma once

#include <string>
#include <vector>
#include <stdexcept>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>

class Rs422Port {
public:
    /**
     * Open and configure the RS-422 port.
     * @throws std::runtime_error on failure.
     */
    explicit Rs422Port(const std::string &device,
                       speed_t           baudrate  = B115200,
                       int               databits  = 8,
                       char              parity    = 'N',
                       int               stopbits  = 1,
                       unsigned          timeout_ds = 10)  // deciseconds
        : fd_(-1)
    {
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0)
            throw std::runtime_error("Rs422Port: cannot open " + device +
                                     ": " + std::strerror(errno));

        ::fcntl(fd_, F_SETFL, 0);

        struct termios tty{};
        if (::tcgetattr(fd_, &tty) != 0)
            throw std::runtime_error("tcgetattr failed");

        ::cfsetispeed(&tty, baudrate);
        ::cfsetospeed(&tty, baudrate);

        tty.c_cflag &= ~CSIZE;
        switch (databits) {
            case 5: tty.c_cflag |= CS5; break;
            case 6: tty.c_cflag |= CS6; break;
            case 7: tty.c_cflag |= CS7; break;
            default: tty.c_cflag |= CS8; break;
        }

        if (parity == 'E')      { tty.c_cflag |= PARENB; tty.c_cflag &= ~PARODD; }
        else if (parity == 'O') { tty.c_cflag |= PARENB; tty.c_cflag |=  PARODD; }
        else                      tty.c_cflag &= ~PARENB;

        if (stopbits == 2)  tty.c_cflag |=  CSTOPB;
        else                tty.c_cflag &= ~CSTOPB;

        tty.c_cflag &= ~CRTSCTS;
        tty.c_cflag |= (CREAD | CLOCAL);
        tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
        tty.c_iflag &= ~(IXON | IXOFF | IXANY | INLCR | ICRNL);
        tty.c_oflag &= ~OPOST;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = static_cast<cc_t>(timeout_ds);

        if (::tcsetattr(fd_, TCSANOW, &tty) != 0)
            throw std::runtime_error("tcsetattr failed");

        ::tcflush(fd_, TCIOFLUSH);
    }

    ~Rs422Port() { if (fd_ >= 0) ::close(fd_); }

    // Non-copyable, movable
    Rs422Port(const Rs422Port &)            = delete;
    Rs422Port &operator=(const Rs422Port &) = delete;
    Rs422Port(Rs422Port &&o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

    /**
     * Write all bytes in buf to the port.
     * @throws std::runtime_error on write error.
     */
    void write(const std::vector<uint8_t> &buf)
    {
        size_t total = 0;
        while (total < buf.size()) {
            ssize_t n = ::write(fd_, buf.data() + total, buf.size() - total);
            if (n < 0) {
                if (errno == EINTR) continue;
                throw std::runtime_error("Rs422Port::write: " +
                                         std::string(std::strerror(errno)));
            }
            total += static_cast<size_t>(n);
        }
        ::tcdrain(fd_);
    }

    /**
     * Read up to max_bytes. Returns empty vector on timeout.
     */
    std::vector<uint8_t> read(size_t max_bytes)
    {
        std::vector<uint8_t> buf(max_bytes);
        ssize_t n = ::read(fd_, buf.data(), max_bytes);
        if (n < 0) {
            if (errno == EAGAIN) return {};
            throw std::runtime_error("Rs422Port::read: " +
                                     std::string(std::strerror(errno)));
        }
        buf.resize(static_cast<size_t>(n));
        return buf;
    }

    /**
     * Read exactly len bytes.
     * @throws std::runtime_error on timeout or error.
     */
    std::vector<uint8_t> readExact(size_t len)
    {
        std::vector<uint8_t> buf;
        buf.reserve(len);
        while (buf.size() < len) {
            auto chunk = read(len - buf.size());
            if (chunk.empty())
                throw std::runtime_error("Rs422Port::readExact: timeout");
            buf.insert(buf.end(), chunk.begin(), chunk.end());
        }
        return buf;
    }

    int fd() const { return fd_; }

private:
    int fd_;
};
```

```cpp
// main.cpp – Using the C++ Rs422Port class
#include <iostream>
#include <vector>
#include <string>
#include "Rs422Port.hpp"

int main()
{
    try {
        Rs422Port port("/dev/ttyUSB0", B115200);

        std::string msg = "Hello RS-422 from C++!";
        std::vector<uint8_t> tx(msg.begin(), msg.end());

        std::cout << "Sending: " << msg << "\n";
        port.write(tx);

        auto rx = port.readExact(tx.size());
        std::string reply(rx.begin(), rx.end());
        std::cout << "Echo: " << reply << "\n";

    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

## Programming RS-422 in Rust

Rust's ecosystem provides the `serialport` crate, which wraps platform serial APIs in a safe, cross-platform interface.

Add to `Cargo.toml`:

```toml
[dependencies]
serialport = "4"
thiserror = "1"
```

### 1. Basic Port Configuration

```rust
// src/rs422.rs  – RS-422 port abstraction

use serialport::{SerialPort, SerialPortBuilder, DataBits, FlowControl, Parity, StopBits};
use std::time::Duration;
use std::io::{self, Read, Write};

/// Configuration for an RS-422 serial port.
pub struct Rs422Config {
    pub device:    String,
    pub baud_rate: u32,
    pub data_bits: DataBits,
    pub parity:    Parity,
    pub stop_bits: StopBits,
    pub timeout:   Duration,
}

impl Default for Rs422Config {
    fn default() -> Self {
        Self {
            device:    "/dev/ttyUSB0".to_string(),
            baud_rate: 115_200,
            data_bits: DataBits::Eight,
            parity:    Parity::None,
            stop_bits: StopBits::One,
            timeout:   Duration::from_millis(1000),
        }
    }
}

/// Open an RS-422 port from the given configuration.
pub fn open(cfg: &Rs422Config) -> serialport::Result<Box<dyn SerialPort>> {
    serialport::new(&cfg.device, cfg.baud_rate)
        .data_bits(cfg.data_bits)
        .parity(cfg.parity)
        .stop_bits(cfg.stop_bits)
        .flow_control(FlowControl::None)  // RS-422 point-to-point: no flow control
        .timeout(cfg.timeout)
        .open()
}

/// Write all bytes, returning an io::Error on failure.
pub fn write_all(port: &mut dyn SerialPort, data: &[u8]) -> io::Result<()> {
    port.write_all(data)?;
    port.flush()?;
    Ok(())
}

/// Read exactly `n` bytes, retrying until complete or timeout.
pub fn read_exact(port: &mut dyn SerialPort, n: usize) -> io::Result<Vec<u8>> {
    let mut buf = vec![0u8; n];
    let mut total = 0usize;

    while total < n {
        match port.read(&mut buf[total..]) {
            Ok(0) => {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    format!("read_exact: timeout after {}/{} bytes", total, n),
                ))
            }
            Ok(k) => total += k,
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                return Err(io::Error::new(
                    io::ErrorKind::TimedOut,
                    format!("read_exact: timeout after {}/{} bytes", total, n),
                ))
            }
            Err(e) => return Err(e),
        }
    }

    Ok(buf)
}
```

### 2. Complete Rust Echo Example

```rust
// src/main.rs – RS-422 point-to-point echo demo

mod rs422;

use rs422::{Rs422Config, open, write_all, read_exact};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cfg = Rs422Config {
        device:    "/dev/ttyUSB0".to_string(),
        baud_rate: 115_200,
        timeout:   Duration::from_millis(2000),
        ..Default::default()
    };

    println!("Opening {} @ {} baud", cfg.device, cfg.baud_rate);
    let mut port = open(&cfg)?;
    println!("Port opened successfully.");

    let message = b"Hello RS-422 from Rust!";
    println!("Sending ({} bytes): {}", message.len(),
             std::str::from_utf8(message).unwrap());

    write_all(port.as_mut(), message)?;

    let reply = read_exact(port.as_mut(), message.len())?;
    println!("Echo ({} bytes): {}", reply.len(),
             std::str::from_utf8(&reply).unwrap_or("<invalid UTF-8>"));

    Ok(())
}
```

### 3. Framed Protocol in Rust with Error Types

```rust
// src/protocol.rs – CRC-8 framing over RS-422

use std::io;
use thiserror::Error;

#[derive(Debug, Error)]
pub enum ProtocolError {
    #[error("IO error: {0}")]
    Io(#[from] io::Error),

    #[error("Invalid SOF byte: 0x{0:02X}")]
    BadSof(u8),

    #[error("CRC mismatch: got 0x{got:02X}, expected 0x{expected:02X}")]
    CrcMismatch { got: u8, expected: u8 },

    #[error("Payload too large: {0} > 252")]
    PayloadTooLarge(usize),
}

const SOF: u8 = 0xAA;

fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0x07
            } else {
                crc << 1
            };
        }
    }
    crc
}

/// Encode a payload into a framed packet.
/// Frame: [SOF] [LEN] [PAYLOAD...] [CRC8]
pub fn encode_packet(payload: &[u8]) -> Result<Vec<u8>, ProtocolError> {
    if payload.len() > 252 {
        return Err(ProtocolError::PayloadTooLarge(payload.len()));
    }
    let mut frame = Vec::with_capacity(3 + payload.len());
    frame.push(SOF);
    frame.push(payload.len() as u8);
    frame.extend_from_slice(payload);
    frame.push(crc8(payload));
    Ok(frame)
}

/// Decode a packet from a port, verifying SOF and CRC.
pub fn decode_packet(
    port: &mut dyn serialport::SerialPort,
) -> Result<Vec<u8>, ProtocolError> {
    use crate::rs422::read_exact;

    // Read header: SOF + LEN
    let hdr = read_exact(port, 2)?;
    if hdr[0] != SOF {
        return Err(ProtocolError::BadSof(hdr[0]));
    }
    let len = hdr[1] as usize;

    // Read payload + CRC
    let mut body = read_exact(port, len + 1)?;
    let received_crc = body[len];
    body.truncate(len);

    let expected_crc = crc8(&body);
    if received_crc != expected_crc {
        return Err(ProtocolError::CrcMismatch {
            got:      received_crc,
            expected: expected_crc,
        });
    }

    Ok(body)
}
```

```rust
// src/main.rs – using the framed protocol
mod rs422;
mod protocol;

use rs422::{Rs422Config, open, write_all};
use protocol::{encode_packet, decode_packet};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let cfg = Rs422Config {
        device: "/dev/ttyUSB0".to_string(),
        baud_rate: 115_200,
        timeout: Duration::from_millis(2000),
        ..Default::default()
    };

    let mut port = open(&cfg)?;

    let payload = b"Sensor reading: 42.7 degC";
    let frame = encode_packet(payload)?;

    println!("Sending framed packet ({} bytes frame).", frame.len());
    write_all(port.as_mut(), &frame)?;

    match decode_packet(port.as_mut()) {
        Ok(reply) => println!(
            "Received: {}",
            std::str::from_utf8(&reply).unwrap_or("<binary>")
        ),
        Err(e) => eprintln!("Protocol error: {}", e),
    }

    Ok(())
}
```

### 4. Listing Available RS-422 Ports in Rust

```rust
// List all serial ports (useful for finding USB-to-RS422 adapters)
fn list_ports() {
    match serialport::available_ports() {
        Ok(ports) => {
            println!("Available serial ports:");
            for p in ports {
                println!("  {} — {:?}", p.port_name, p.port_type);
            }
        }
        Err(e) => eprintln!("Could not list ports: {}", e),
    }
}
```

---

## Multi-Drop Topologies

Although RS-422 is strictly **one driver / up to ten receivers**, it supports a useful multi-drop (broadcast) configuration:

```
  Master TX+  ──────────────────────────────────────────────►
  Master TX−  ──────────────────────────────────────────────►
                   │                 │                 │
               Slave 1 RX        Slave 2 RX        Slave 3 RX
               (+ biasing       (+ biasing       (+ biasing
                resistors)       resistors)       resistors)
```

- Only one driver may be active at a time.
- Each slave needs its own RX pair back to the master for full-duplex (this becomes 4-wire RS-485-like wiring).
- Termination resistors (100–120 Ω) go at **both ends** of the bus.
- Fail-safe biasing resistors pull A high and B low when the line is idle/open.

---

## Error Handling and Termination

### Line Termination

Without termination, high-frequency signals reflect off the cable end and corrupt data. Always terminate with a resistor matching the cable impedance:

```
Twisted pair cable (120 Ω)
 ─────────────────────────────────────────────┐
                                              │ 120 Ω
 ─────────────────────────────────────────────┘
 (termination resistor placed at far end from driver)
```

Many RS-422 ICs or modules include a solder jumper or DIP switch to connect an internal termination resistor. At lower baud rates (< 19200), termination is less critical on short cables.

### Fail-Safe Biasing

When the cable is open, shorted, or the driver is in high-impedance (tri-state), the differential voltage is undefined. Fail-safe resistors guarantee a known idle state:

```
 Vcc (e.g. 5 V)
    │
   [560 Ω]  ← pull-up on A (+)
    │
    A ─────────────────────
    B ─────────────────────
    │
   [560 Ω]  ← pull-down on B (−)
    │
   GND
```

This ensures A > B (Mark = logic 1) when the line is unterminated.

### Common Software Errors and Remedies

| Error Symptom                  | Likely Cause                     | Remedy                                  |
|-------------------------------|----------------------------------|-----------------------------------------|
| Framing errors, garbage data  | Baud rate mismatch               | Verify both ends configured identically |
| Data truncated at 1024 bytes  | OS buffer full                   | Read more frequently; increase UART FIFO|
| All `0xFF` bytes received     | A and B wires swapped            | Swap the differential pair              |
| CRC errors on long cables     | Missing termination              | Add 120 Ω terminator at cable end       |
| Occasional bit errors         | Ground potential difference      | Use isolated RS-422 transceiver         |
| Works slowly, fails at speed  | Cable capacitance too high       | Reduce cable length or lower baud rate  |

---

## Summary

RS-422 extends UART serial communication to long distances and noisy environments through **balanced differential signaling**. Key takeaways:

**Electrically**, the signal is the voltage difference between two conductors (A and B), giving excellent common-mode noise rejection. A receiver needs only ±200 mV to distinguish logic levels, but a driver provides ±2–6 V, giving a 10× noise margin.

**Distance and speed** follow an inverse relationship. At 1200 m you can achieve ~100 kbps; at 30 m you can push ~10 Mbps. The governing rule is: baud_rate × distance ≤ 10⁸.

**Topology** is point-to-point (one driver) or one-to-many (one driver, up to ten receivers on a daisy-chain). Full-duplex uses four wires (TX+/TX− and RX+/RX−). Termination resistors (100–120 Ω) at the far end prevent reflections, and fail-safe biasing resistors define the idle state when the line is open.

**From a software perspective**, RS-422 is transparent: the OS presents the port as a standard UART (`/dev/ttyS*`, `/dev/ttyUSB*`). In **C/C++** the `termios` API controls baud rate, data bits, parity, and stop bits. A proper application wraps reads and writes with retry loops, uses `tcdrain()` after writes, and implements an application-level framing protocol (e.g., SOF + length + CRC). In **Rust** the `serialport` crate provides a safe, cross-platform abstraction with `Builder` pattern configuration, and Rust's `Result`/`Error` types allow clean, composable error handling for both transport and protocol layers.

RS-422 remains the workhorse standard wherever data must travel beyond the 15-metre reach of RS-232 but a full Ethernet/fieldbus solution would be over-engineered — from factory floors and telescopes to studio talkback systems and spacecraft instrumentation.

---

*Document: 92 – RS-422 Differential Signaling | UART Series*