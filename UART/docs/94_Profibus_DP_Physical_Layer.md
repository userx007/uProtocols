# 94. Profibus DP Physical Layer

**Theory & Specifications**
- Full RS-485 differential signalling fundamentals and voltage levels
- Profibus-specific electrical specs: the 3-element termination network (pull-up 390 Ω + 220 Ω + pull-down 390 Ω), pinout conventions, and the A/B polarity trap
- Type A cable parameters (impedance, capacitance, wire gauge)
- Segment length vs. baud rate table (9.6 kbps → 12 Mbps)
- FDL frame types (SD1, SD2, SD3, SD4, SC) with byte-level diagrams
- UART character format (8E1) and FCS (modulo-256) explained
- Token-passing bus access and timing parameters (tSYN, tSDR, TTR)

**C/C++ Code Examples**
- Linux serial port configuration for 8E1
- TX enable (DE/RE̅) control via RTS ioctl
- SD2 frame builder with FCS + transmit timing
- SD2 receive with timeout and validation
- C++ RAII port wrapper with move semantics
- Cyclic master poll loop

**Rust Code Examples**
- Port configuration using the `serialport` crate
- SD2 frame builder and parser with unit tests (roundtrip, FCS corruption, empty payload)
- Half-duplex transmit with calculated turnaround delay
- Async cyclic master with `tokio` and `tokio-serial`
- Linux `TIOCSRS485` ioctl for hardware-accurate direction switching

### Understanding Profibus Use of RS-485 Physical Layer

---

## Table of Contents

1. [Introduction](#introduction)
2. [Profibus DP Overview](#profibus-dp-overview)
3. [RS-485 Physical Layer Fundamentals](#rs-485-physical-layer-fundamentals)
4. [Profibus RS-485 Electrical Specifications](#profibus-rs-485-electrical-specifications)
5. [Network Topology and Cabling](#network-topology-and-cabling)
6. [Profibus Frame Structure](#profibus-frame-structure)
7. [Baud Rates and Timing](#baud-rates-and-timing)
8. [Bus Access — Token Passing](#bus-access--token-passing)
9. [Repeaters and Segment Isolation](#repeaters-and-segment-isolation)
10. [Implementation in C/C++](#implementation-in-cc)
11. [Implementation in Rust](#implementation-in-rust)
12. [Summary](#summary)

---

## Introduction

Profibus DP (Decentralized Periphery) is one of the most widely deployed fieldbus standards in industrial automation. Originally defined under German DIN 19245 and later standardized as IEC 61158 and IEC 61784, Profibus DP operates over the RS-485 physical layer for the majority of its installations. Understanding how Profibus uses RS-485 is essential for engineers designing, commissioning, or troubleshooting industrial field networks.

This document covers the physical layer characteristics, electrical specifications, cable topology, timing requirements, and practical implementation aspects of Profibus DP over RS-485, accompanied by C/C++ and Rust code examples suitable for embedded controllers and host-side software.

---

## Profibus DP Overview

Profibus DP is a **master-slave** fieldbus protocol:

- **Master devices** (Class 1: PLCs, DCS controllers; Class 2: engineering tools) own the communication token and initiate all transactions.
- **Slave devices** (I/O modules, drives, sensors, actuators) respond only when addressed.
- The protocol supports up to **126 addressable nodes** (addresses 0–125), with address 126 reserved for initial parameterization.

Profibus DP is structured in three layers:

| OSI Layer | Profibus Layer | Description |
|-----------|----------------|-------------|
| Layer 1   | Physical (RS-485) | Electrical signalling, cable, connectors |
| Layer 2   | FDL (Fieldbus Data Link) | Framing, token passing, CRC |
| Layer 7   | DP Application | GSD files, parameterization, cyclic I/O |

Layers 3–6 are empty in Profibus DP, providing a lean, deterministic stack.

---

## RS-485 Physical Layer Fundamentals

RS-485 (EIA/TIA-485) defines a **differential, multi-drop serial bus**:

- **Differential signalling**: Data is encoded on two lines, A (−) and B (+). The receiver detects the voltage difference between A and B, providing excellent common-mode noise rejection.
- **Multi-drop**: Multiple transmitters and receivers share the same wire pair; only one transmitter may drive the bus at a time (half-duplex).
- **Termination**: A resistor (typically 150 Ω to 220 Ω) at each end of the cable absorbs signal reflections.

### RS-485 Voltage Levels

| Condition | V(B) − V(A) | Logical Value |
|-----------|-------------|---------------|
| Mark (idle) | −0.2 V to −6 V | Logic 1 |
| Space       | +0.2 V to +6 V | Logic 0 |
| Indeterminate | −0.2 V to +0.2 V | Undefined |

RS-485 specifies a receiver threshold of ±200 mV, giving the differential signal very high immunity to induced interference — crucial in noisy industrial environments.

---

## Profibus RS-485 Electrical Specifications

Profibus DP uses RS-485 with several **tighter constraints** than generic RS-485:

### Signal Lines

Profibus names its lines differently from RS-485 notation:

| Profibus Label | RS-485 Equivalent | Description |
|----------------|-------------------|-------------|
| A (or RxD/TxD−) | A (negative) | Inverted data line |
| B (or RxD/TxD+) | B (positive) | Non-inverted data line |
| DGND | Signal ground | Common reference |
| VP (+5V) | Optional supply | Powers bus termination resistors |

> **Important**: Profibus connector pinouts on 9-pin D-Sub connectors define Pin 3 as B (+) and Pin 8 as A (−). This is opposite to some RS-485 conventions — a common source of wiring errors.

### Termination Network

Profibus RS-485 requires a **three-element termination** network at each cable end, not just a simple termination resistor:

```
VP (+5V) ---[390 Ω]--- B line
                        |
              [220 Ω termination]
                        |
A line ---[390 Ω]--- DGND
```

The pull-up (390 Ω to VP) and pull-down (390 Ω to GND) resistors define the bus state when all transmitters are tri-stated (idle), preventing the undefined receiver region. Many Profibus connectors have a built-in switch to enable/disable this termination.

### Cable Specification (Type A Cable)

IEC 61158-2 defines the recommended **Type A cable** for Profibus RS-485:

| Parameter | Specification |
|-----------|---------------|
| Characteristic impedance | 135–165 Ω at 3–20 MHz |
| Capacitance per unit length | ≤ 30 pF/m |
| Loop resistance | ≤ 110 Ω/km |
| Wire cross-section | ≥ 0.34 mm² (AWG 22) |
| Shield | Overall braided or foil |
| Pairs | 1 twisted pair |

Type B cable (unshielded or lower-spec) may be used at lower baud rates but is strongly discouraged for new installations.

---

## Network Topology and Cabling

Profibus RS-485 uses a **linear bus topology**:

```
[Master]---[Slave 1]---[Slave 2]---...---[Slave N]
   T                                          T
   (Terminator)                         (Terminator)
```

- Stubs (T-pieces, drops) are **not permitted** at higher baud rates. Each device must be daisy-chained.
- Bus connectors with internal switching allow devices to be disconnected without breaking the bus continuity.

### Maximum Segment Length vs. Baud Rate

| Baud Rate | Max Segment Length | Typical Application |
|-----------|--------------------|---------------------|
| 9.6 kbps  | 1200 m             | Long-distance installations |
| 19.2 kbps | 1200 m             | Long-distance installations |
| 93.75 kbps| 1200 m             | General fieldbus |
| 187.5 kbps| 1000 m             | Standard Profibus DP |
| 500 kbps  | 400 m              | Standard Profibus DP |
| 1.5 Mbps  | 200 m              | High-speed DP |
| 3 Mbps    | 100 m              | High-speed DP |
| 6 Mbps    | 100 m              | High-speed DP |
| 12 Mbps   | 100 m              | High-speed DP |

Up to **32 nodes per segment** (including repeaters). With 9 repeaters, up to 126 nodes on the network.

---

## Profibus Frame Structure

Profibus DP uses several FDL frame types, all transmitted as standard UART frames: **8 data bits, even parity, 1 stop bit** (8E1).

### UART Character Format

```
Start  D0  D1  D2  D3  D4  D5  D6  D7  Parity  Stop
  0    LSB                             MSB   Even    1
```

Even parity on every character provides character-level error detection in addition to the frame CRC.

### FDL Frame Types

#### SD1 — Frame without Data (Fixed Length, No Data)
```
+----+----+----+----+----+
| SD1| DA | SA | FC | ED |
| 10h|    |    |    | 16h|
+----+----+----+----+----+
```

#### SD2 — Frame with Variable Data Length
```
+----+----+-----+----+----+-----+-----+-----+----+
| SD2| LE |LEr  | SD2| DA | SA  | FC  | Data| FCS| ED |
| 68h|    |     | 68h|    |     |     | 0..246B|   | 16h|
+----+----+-----+----+----+-----+-----+-----+----+
```
- LE: Data length byte (DA + SA + FC + data)
- LEr: Repeat of LE (redundancy check)
- FCS: Frame Check Sum — arithmetic sum of DA, SA, FC, and all data bytes, modulo 256

#### SD3 — Fixed Length Frame with Data (8 bytes)
```
+----+----+----+----+----------+----+----+
| SD3| DA | SA | FC | Data[8]  | FCS| ED |
| A2h|    |    |    |          |    | 16h|
+----+----+----+----+----------+----+----+
```

#### Token Frame (SD4)
```
+----+----+----+
| SD4| DA | SA |
| DCh|    |    |
+----+----+----+
```
Passed between masters to transfer bus ownership.

#### Short Acknowledgement (SC)
```
+----+
| E5h|
+----+
```
Single-byte acknowledgement from slave to master.

### Address and Function Code Bytes

- **DA** (Destination Address): 0–126 for individual stations; 127 = broadcast (no response expected)
- **SA** (Source Address): 0–126
- **FC** (Function Code): Encodes request/response type, acknowledgement class, frame count bit

---

## Baud Rates and Timing

Profibus DP defines strict timing parameters to ensure deterministic bus behaviour.

### Key Timing Parameters (in bit times, tBit = 1/baud)

| Parameter | Symbol | Typical Value | Description |
|-----------|--------|---------------|-------------|
| Syn time  | tSYN   | ≥ 33 tBit     | Min idle time before a valid frame |
| SRD response time | tSDR | min 11 tBit, max configured | Time slave waits before responding |
| GAP update time | tGAP | configurable | Time for token ring maintenance |

At 1.5 Mbps, one bit time = 667 ns. The minimum SYN gap of 33 bits = ~22 µs.

### Direction Control (TX Enable / RX Enable)

Because RS-485 is half-duplex, the UART driver must control the **DE (Driver Enable)** and **RE̅ (Receiver Enable)** lines of the RS-485 transceiver chip. Timing is critical:

```
TX request:
  Assert DE  →  [transceiver propagation delay]  →  First start bit

TX complete:
  Last stop bit  →  [character guard time]  →  De-assert DE

RX window:
  DE de-asserted  →  RE̅ asserted  →  Receive response within tSDR_max
```

Failure to de-assert DE before the slave responds results in bus collision and corrupted data.

---

## Bus Access — Token Passing

Profibus DP uses a **token ring** among masters, combined with master-slave polling within each master's token hold time.

1. A master holds the token and polls all its assigned slaves cyclically (cyclic data exchange).
2. After completing its cycle, the master forwards the token to the next master address (logical ring).
3. If only one master exists, it holds the token permanently.

### Token Management Parameters

- **HSA** (Highest Station Address): Defines the upper bound of the token ring scan.
- **Target Rotation Time (TTR)**: Maximum time allowed for one full token rotation.
- **tRTO** (Real Rotation Time): Actual measured rotation time, used to adapt the next hold time.

---

## Implementation in C/C++

### Platform Context

The examples below target a typical embedded scenario (Linux with a USB-RS485 adapter or a microcontroller with a UART peripheral + RS-485 transceiver). They demonstrate physical-layer framing and direction control — the foundation on which a full Profibus DP stack builds.

### C Example 1: Opening and Configuring the Serial Port (Linux)

```c
/**
 * profibus_serial.c
 * Configure a Linux serial port for Profibus DP RS-485.
 * Settings: 8 data bits, even parity, 1 stop bit (8E1).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>

#define PROFIBUS_DEFAULT_BAUD B1500000   /* 1.5 Mbps — adjust to network */
#define PROFIBUS_SYNC_DELAY_US 200        /* >33 bit times at 1.5 Mbps    */

typedef struct {
    int  fd;
    int  rts_gpio; /* GPIO number for DE/RE control, -1 if using RTS */
} ProfibusPort;

/**
 * Open and configure the serial port for Profibus DP.
 * Returns 0 on success, -1 on failure.
 */
int profibus_open(ProfibusPort *port, const char *device, speed_t baud)
{
    struct termios tty;

    port->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (port->fd < 0) {
        perror("profibus_open: open");
        return -1;
    }

    if (tcgetattr(port->fd, &tty) != 0) {
        perror("profibus_open: tcgetattr");
        close(port->fd);
        return -1;
    }

    /* Raw mode — no processing */
    cfmakeraw(&tty);

    /* 8E1: 8 data bits, even parity, 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;         /* 8 data bits  */
    tty.c_cflag |= PARENB;      /* Enable parity */
    tty.c_cflag &= ~PARODD;     /* Even parity  */
    tty.c_cflag &= ~CSTOPB;     /* 1 stop bit   */
    tty.c_cflag &= ~CRTSCTS;    /* No hardware flow control */
    tty.c_cflag |= CREAD | CLOCAL;

    /* Timeouts: return immediately with whatever is in buffer */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    if (tcsetattr(port->fd, TCSANOW, &tty) != 0) {
        perror("profibus_open: tcsetattr");
        close(port->fd);
        return -1;
    }

    tcflush(port->fd, TCIOFLUSH);
    return 0;
}

/**
 * Assert or de-assert the RS-485 transmitter enable line via RTS.
 * On USB-RS485 adapters the RTS signal typically controls DE/RE̅.
 */
void profibus_set_tx_enable(ProfibusPort *port, int enable)
{
    int status;
    ioctl(port->fd, TIOCMGET, &status);
    if (enable)
        status |= TIOCM_RTS;
    else
        status &= ~TIOCM_RTS;
    ioctl(port->fd, TIOCMSET, &status);
}

void profibus_close(ProfibusPort *port)
{
    if (port->fd >= 0) {
        close(port->fd);
        port->fd = -1;
    }
}
```

### C Example 2: FCS Calculation and SD2 Frame Transmit

```c
/**
 * Compute Profibus FCS (Frame Check Sum).
 * Arithmetic sum of bytes, modulo 256.
 */
uint8_t profibus_fcs(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += data[i];
    return (uint8_t)(sum & 0xFF);
}

/* SD2 frame delimiters */
#define SD2  0x68
#define ED   0x16

/**
 * Build and transmit an SD2 variable-length frame.
 *
 * da   - Destination address
 * sa   - Source address
 * fc   - Function code
 * pdu  - Payload (may be NULL for zero-length)
 * plen - Payload length in bytes (0..242)
 *
 * Returns bytes written or -1 on error.
 */
int profibus_send_sd2(ProfibusPort *port,
                      uint8_t da, uint8_t sa, uint8_t fc,
                      const uint8_t *pdu, uint8_t plen)
{
    /* LE = DA + SA + FC + PDU length */
    uint8_t le = 3 + plen;

    /* Allocate frame: SD2(1) + LE(1) + LEr(1) + SD2(1) + DA(1) + SA(1)
     *                  + FC(1) + PDU(plen) + FCS(1) + ED(1) */
    size_t frame_len = 4 + 3 + plen + 2;
    uint8_t *frame = malloc(frame_len);
    if (!frame) return -1;

    size_t i = 0;
    frame[i++] = SD2;
    frame[i++] = le;
    frame[i++] = le;       /* LEr = LE */
    frame[i++] = SD2;
    frame[i++] = da;
    frame[i++] = sa;
    frame[i++] = fc;
    if (plen > 0) {
        memcpy(&frame[i], pdu, plen);
        i += plen;
    }
    /* FCS covers DA, SA, FC, and PDU */
    frame[i++] = profibus_fcs(&frame[4], 3 + plen);
    frame[i++] = ED;

    /* Assert TX enable, send, then release */
    profibus_set_tx_enable(port, 1);
    usleep(PROFIBUS_SYNC_DELAY_US);   /* Ensure SYN gap */

    ssize_t written = write(port->fd, frame, frame_len);
    tcdrain(port->fd);                /* Wait until all bytes are shifted out */
    profibus_set_tx_enable(port, 0);

    free(frame);
    return (int)written;
}
```

### C Example 3: Receive and Validate an SD2 Frame

```c
#include <time.h>

/**
 * Receive bytes from the serial port with a timeout in milliseconds.
 * Returns number of bytes read.
 */
static ssize_t read_timeout(int fd, uint8_t *buf, size_t len, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;
    ssize_t total = 0;

    while ((size_t)total < len) {
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);
        tv.tv_sec  = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;

        int ret = select(fd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) break;  /* Timeout or error */

        ssize_t n = read(fd, buf + total, len - total);
        if (n <= 0) break;
        total += n;
    }
    return total;
}

typedef struct {
    uint8_t da;
    uint8_t sa;
    uint8_t fc;
    uint8_t data[246];
    uint8_t data_len;
    int     valid;
} ProfibusFrame;

/**
 * Attempt to receive and validate an SD2 frame.
 * Returns 1 if a valid frame was received, 0 otherwise.
 */
int profibus_recv_sd2(ProfibusPort *port, ProfibusFrame *out, int timeout_ms)
{
    uint8_t header[4];
    memset(out, 0, sizeof(*out));

    /* Read the 4-byte header */
    if (read_timeout(port->fd, header, 4, timeout_ms) < 4)
        return 0;

    if (header[0] != SD2 || header[3] != SD2) return 0;  /* Bad delimiters */
    if (header[1] != header[2]) return 0;                 /* LE != LEr */

    uint8_t le = header[1];
    if (le < 3 || le > 249) return 0;  /* Sanity check */

    uint8_t body_len = le + 2;  /* DA+SA+FC+data + FCS + ED */
    uint8_t body[249];

    if (read_timeout(port->fd, body, body_len, timeout_ms) < body_len)
        return 0;

    if (body[body_len - 1] != ED) return 0;  /* No end delimiter */

    /* Verify FCS */
    uint8_t expected_fcs = profibus_fcs(body, le);
    if (body[le] != expected_fcs) return 0;

    out->da       = body[0];
    out->sa       = body[1];
    out->fc       = body[2];
    out->data_len = le - 3;
    memcpy(out->data, &body[3], out->data_len);
    out->valid    = 1;
    return 1;
}
```

### C++ Example: RAII Profibus Port Wrapper

```cpp
/**
 * ProfibusPort.hpp
 * C++ RAII wrapper around the Profibus RS-485 serial port.
 */

#pragma once
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>
#include <termios.h>

class ProfibusPort {
public:
    explicit ProfibusPort(const std::string &device, speed_t baud = B1500000);
    ~ProfibusPort();

    // Non-copyable, movable
    ProfibusPort(const ProfibusPort &) = delete;
    ProfibusPort &operator=(const ProfibusPort &) = delete;
    ProfibusPort(ProfibusPort &&other) noexcept;

    /** Send a Profibus SD2 frame */
    void sendSD2(uint8_t da, uint8_t sa, uint8_t fc,
                 const std::vector<uint8_t> &payload);

    /** Receive an SD2 frame, returns false on timeout or error */
    bool recvSD2(uint8_t &da, uint8_t &sa, uint8_t &fc,
                 std::vector<uint8_t> &payload, int timeoutMs = 50);

    /** Compute Profibus FCS */
    static uint8_t computeFCS(const uint8_t *data, size_t len);

private:
    int fd_;

    void setTxEnable(bool enable);
    ssize_t readWithTimeout(uint8_t *buf, size_t len, int timeoutMs);
};
```

```cpp
/**
 * ProfibusPort.cpp
 */

#include "ProfibusPort.hpp"
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <cstring>
#include <stdexcept>

static constexpr uint8_t SD2_BYTE = 0x68;
static constexpr uint8_t ED_BYTE  = 0x16;
static constexpr int     SYNC_US  = 200;

ProfibusPort::ProfibusPort(const std::string &device, speed_t baud)
    : fd_(-1)
{
    fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd_ < 0)
        throw std::runtime_error("Cannot open device: " + device);

    struct termios tty{};
    cfmakeraw(&tty);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | PARENB | CREAD | CLOCAL;
    tty.c_cflag &= ~(PARODD | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN] = 0; tty.c_cc[VTIME] = 0;
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    if (tcsetattr(fd_, TCSANOW, &tty) != 0)
        throw std::runtime_error("tcsetattr failed");

    tcflush(fd_, TCIOFLUSH);
}

ProfibusPort::~ProfibusPort()
{
    if (fd_ >= 0) close(fd_);
}

ProfibusPort::ProfibusPort(ProfibusPort &&other) noexcept
    : fd_(other.fd_) { other.fd_ = -1; }

uint8_t ProfibusPort::computeFCS(const uint8_t *data, size_t len)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) sum += data[i];
    return static_cast<uint8_t>(sum & 0xFF);
}

void ProfibusPort::setTxEnable(bool enable)
{
    int status = 0;
    ioctl(fd_, TIOCMGET, &status);
    enable ? (status |= TIOCM_RTS) : (status &= ~TIOCM_RTS);
    ioctl(fd_, TIOCMSET, &status);
}

void ProfibusPort::sendSD2(uint8_t da, uint8_t sa, uint8_t fc,
                           const std::vector<uint8_t> &payload)
{
    uint8_t le = static_cast<uint8_t>(3 + payload.size());
    std::vector<uint8_t> frame;
    frame.reserve(6 + payload.size() + 2);

    frame.push_back(SD2_BYTE);
    frame.push_back(le);
    frame.push_back(le);
    frame.push_back(SD2_BYTE);
    frame.push_back(da);
    frame.push_back(sa);
    frame.push_back(fc);
    frame.insert(frame.end(), payload.begin(), payload.end());

    std::vector<uint8_t> fcs_data = {da, sa, fc};
    fcs_data.insert(fcs_data.end(), payload.begin(), payload.end());
    frame.push_back(computeFCS(fcs_data.data(), fcs_data.size()));
    frame.push_back(ED_BYTE);

    setTxEnable(true);
    usleep(SYNC_US);
    write(fd_, frame.data(), frame.size());
    tcdrain(fd_);
    setTxEnable(false);
}

ssize_t ProfibusPort::readWithTimeout(uint8_t *buf, size_t len, int timeoutMs)
{
    fd_set rfds; FD_ZERO(&rfds); FD_SET(fd_, &rfds);
    struct timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
    ssize_t total = 0;
    while (static_cast<size_t>(total) < len) {
        FD_ZERO(&rfds); FD_SET(fd_, &rfds);
        if (select(fd_ + 1, &rfds, nullptr, nullptr, &tv) <= 0) break;
        ssize_t n = read(fd_, buf + total, len - (size_t)total);
        if (n <= 0) break;
        total += n;
    }
    return total;
}

bool ProfibusPort::recvSD2(uint8_t &da, uint8_t &sa, uint8_t &fc,
                           std::vector<uint8_t> &payload, int timeoutMs)
{
    uint8_t hdr[4];
    if (readWithTimeout(hdr, 4, timeoutMs) < 4) return false;
    if (hdr[0] != SD2_BYTE || hdr[3] != SD2_BYTE || hdr[1] != hdr[2]) return false;

    uint8_t le = hdr[1];
    if (le < 3) return false;

    std::vector<uint8_t> body(le + 2);
    if (readWithTimeout(body.data(), body.size(), timeoutMs)
            < static_cast<ssize_t>(body.size())) return false;

    if (body.back() != ED_BYTE) return false;
    if (computeFCS(body.data(), le) != body[le]) return false;

    da = body[0]; sa = body[1]; fc = body[2];
    payload.assign(body.begin() + 3, body.begin() + le);
    return true;
}
```

### C++ Example: Cyclic Master Poll Loop

```cpp
/**
 * Simplified Profibus DP master cyclic I/O loop.
 * Sends SRD (Send and Request Data) to each slave and collects responses.
 */

#include "ProfibusPort.hpp"
#include <iostream>
#include <array>

// Function codes relevant to DP master-slave exchange
static constexpr uint8_t FC_SRD_HIGH = 0x5C;   /* Send/Request Data, High priority */
static constexpr uint8_t FC_SDN_LOW  = 0x44;   /* Send Data with No Ack, Low prio  */

struct SlaveConfig {
    uint8_t  address;
    uint8_t  output_bytes;   /* Bytes master writes to slave */
    uint8_t  input_bytes;    /* Bytes master reads from slave */
};

void cyclicExchange(ProfibusPort &port,
                    const SlaveConfig &slave,
                    const std::vector<uint8_t> &outputs,
                    std::vector<uint8_t> &inputs)
{
    constexpr uint8_t MASTER_ADDR = 0x00;

    // Send outputs to slave, request inputs back
    port.sendSD2(slave.address, MASTER_ADDR, FC_SRD_HIGH, outputs);

    uint8_t da, sa, fc;
    if (!port.recvSD2(da, sa, fc, inputs, 20 /* ms */)) {
        inputs.clear();
        std::cerr << "No response from slave " << (int)slave.address << "\n";
    }
}

int main()
{
    try {
        ProfibusPort port("/dev/ttyUSB0", B1500000);

        SlaveConfig slave{ 0x03, 4, 4 };  /* Slave at addr 3, 4 bytes each way */
        std::vector<uint8_t> outputs = { 0x01, 0x02, 0x03, 0x04 };
        std::vector<uint8_t> inputs;

        for (int cycle = 0; cycle < 10; ++cycle) {
            cyclicExchange(port, slave, outputs, inputs);
            std::cout << "Cycle " << cycle << " inputs:";
            for (auto b : inputs) std::cout << " 0x" << std::hex << (int)b;
            std::cout << "\n";
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

## Implementation in Rust

### Rust Example 1: Serial Port Configuration with `serialport` Crate

```toml
# Cargo.toml
[dependencies]
serialport = "4"
```

```rust
// profibus_port.rs
//! Profibus DP RS-485 physical layer — serial port setup and framing.

use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};
use std::io::{self, Read, Write};
use std::time::Duration;

pub const SD2_BYTE: u8 = 0x68;
pub const ED_BYTE:  u8 = 0x16;
pub const SC_BYTE:  u8 = 0xE5;   /* Short Acknowledgement */

/// Compute Profibus FCS — arithmetic sum modulo 256.
pub fn profibus_fcs(data: &[u8]) -> u8 {
    data.iter().fold(0u32, |acc, &b| acc + b as u32) as u8
}

/// Open a serial port configured for Profibus DP (8E1).
pub fn open_profibus_port(device: &str, baud: u32) -> Result<Box<dyn SerialPort>, serialport::Error> {
    serialport::new(device, baud)
        .data_bits(DataBits::Eight)
        .parity(Parity::Even)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(50))
        .open()
}
```

### Rust Example 2: SD2 Frame Builder and Validator

```rust
// frame.rs
//! Profibus DP SD2 frame construction and parsing.

use crate::profibus_port::{profibus_fcs, ED_BYTE, SD2_BYTE};

/// A decoded Profibus FDL frame.
#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub destination: u8,
    pub source:      u8,
    pub function:    u8,
    pub data:        Vec<u8>,
}

/// Build a complete SD2 frame as a byte vector.
pub fn build_sd2_frame(
    destination: u8,
    source: u8,
    function: u8,
    payload: &[u8],
) -> Result<Vec<u8>, &'static str> {
    let data_len = payload.len();
    if data_len > 242 {
        return Err("Payload exceeds maximum SD2 PDU length (242 bytes)");
    }

    let le: u8 = (3 + data_len) as u8;  // DA + SA + FC + payload

    let mut frame = Vec::with_capacity(6 + data_len + 2);
    frame.push(SD2_BYTE);
    frame.push(le);
    frame.push(le);           // LEr — repeat of LE
    frame.push(SD2_BYTE);
    frame.push(destination);
    frame.push(source);
    frame.push(function);
    frame.extend_from_slice(payload);

    // FCS covers DA, SA, FC, and payload
    let fcs_data: Vec<u8> = [destination, source, function]
        .iter()
        .chain(payload.iter())
        .copied()
        .collect();
    frame.push(profibus_fcs(&fcs_data));
    frame.push(ED_BYTE);

    Ok(frame)
}

/// Parse and validate a raw byte buffer as an SD2 frame.
/// Returns `Some(ProfibusFrame)` on success, `None` on any error.
pub fn parse_sd2_frame(raw: &[u8]) -> Option<ProfibusFrame> {
    // Minimum frame: SD2+LE+LEr+SD2+DA+SA+FC+FCS+ED = 9 bytes
    if raw.len() < 9 {
        return None;
    }
    if raw[0] != SD2_BYTE || raw[3] != SD2_BYTE {
        return None;
    }
    let le = raw[1];
    if raw[2] != le {
        return None;  // LE != LEr
    }
    if le < 3 {
        return None;
    }

    let expected_len = (4 + le as usize) + 2;  // header + body + FCS + ED
    if raw.len() < expected_len {
        return None;
    }

    let body = &raw[4..(4 + le as usize)];
    let fcs_pos  = 4 + le as usize;
    let ed_pos   = fcs_pos + 1;

    if raw[ed_pos] != ED_BYTE {
        return None;
    }

    let expected_fcs = profibus_fcs(body);
    if raw[fcs_pos] != expected_fcs {
        return None;
    }

    let destination = body[0];
    let source      = body[1];
    let function    = body[2];
    let data        = body[3..].to_vec();

    Some(ProfibusFrame { destination, source, function, data })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_roundtrip_sd2() {
        let payload = vec![0xAA, 0xBB, 0xCC];
        let frame = build_sd2_frame(0x03, 0x00, 0x5C, &payload).unwrap();
        let parsed = parse_sd2_frame(&frame).unwrap();

        assert_eq!(parsed.destination, 0x03);
        assert_eq!(parsed.source,      0x00);
        assert_eq!(parsed.function,    0x5C);
        assert_eq!(parsed.data,        payload);
    }

    #[test]
    fn test_fcs_corruption_detected() {
        let payload = vec![0x01, 0x02];
        let mut frame = build_sd2_frame(0x05, 0x00, 0x44, &payload).unwrap();
        // Corrupt the FCS byte
        let fcs_idx = frame.len() - 2;
        frame[fcs_idx] ^= 0xFF;
        assert!(parse_sd2_frame(&frame).is_none());
    }

    #[test]
    fn test_empty_payload() {
        let frame = build_sd2_frame(0x01, 0x00, 0x4C, &[]).unwrap();
        let parsed = parse_sd2_frame(&frame).unwrap();
        assert!(parsed.data.is_empty());
    }
}
```

### Rust Example 3: Transmit with Direction Control

```rust
// transmit.rs
//! Half-duplex RS-485 transmit with RTS direction control.
//!
//! Note: Full RTS control depends on platform support in the serialport crate.
//! On Linux with RS485-capable UARTs, use the TIOCSRS485 ioctl for automatic
//! direction switching (see the rs485 example below).

use serialport::SerialPort;
use std::io::Write;
use std::thread;
use std::time::Duration;

pub struct ProfibusTransmitter {
    port: Box<dyn SerialPort>,
}

impl ProfibusTransmitter {
    pub fn new(port: Box<dyn SerialPort>) -> Self {
        Self { port }
    }

    /// Assert RTS (driver enable), send frame, drain, then de-assert RTS.
    pub fn send_frame(&mut self, frame: &[u8]) -> io::Result<()> {
        // Assert DE via RTS
        self.port.write_request_to_send(true)?;

        // SYN gap: >33 bit times. At 1.5 Mbps that is ~22 µs; use 200 µs margin.
        thread::sleep(Duration::from_micros(200));

        self.port.write_all(frame)?;
        self.port.flush()?;

        // Wait for all bytes to be physically transmitted before releasing DE.
        // Approximate: frame_len * bits_per_byte / baud_rate
        let bit_time_ns = 1_000_000_000u64 / self.port.baud_rate()? as u64;
        let frame_bits  = (frame.len() as u64) * 11; // start + 8 data + parity + stop
        thread::sleep(Duration::from_nanos(frame_bits * bit_time_ns + 100_000));

        // De-assert DE — bus returns to receive mode
        self.port.write_request_to_send(false)?;
        Ok(())
    }
}

use std::io;
```

### Rust Example 4: Cyclic Master Exchange (Async with tokio)

```toml
# Cargo.toml additions for async example
[dependencies]
tokio       = { version = "1", features = ["full"] }
tokio-serial = "5"
```

```rust
// async_master.rs
//! Asynchronous Profibus DP master cyclic I/O using tokio-serial.

use tokio::time::{sleep, Duration};
use tokio_serial::SerialPortBuilderExt;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

use crate::frame::{build_sd2_frame, parse_sd2_frame};

// Function codes
const FC_SRD_HIGH:  u8 = 0x5C;
const MASTER_ADDR:  u8 = 0x00;

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut port = tokio_serial::new("/dev/ttyUSB0", 1_500_000)
        .parity(tokio_serial::Parity::Even)
        .stop_bits(tokio_serial::StopBits::One)
        .data_bits(tokio_serial::DataBits::Eight)
        .open_native_async()?;

    let slave_addr: u8 = 0x03;
    let outputs = vec![0x01, 0x02, 0x03, 0x04];

    for cycle in 0..10u32 {
        // Build and send SRD request
        let frame = build_sd2_frame(slave_addr, MASTER_ADDR, FC_SRD_HIGH, &outputs)?;
        port.write_all(&frame).await?;

        // Allow slave response time (tSDR_min at 1.5 Mbps ≈ 7.3 µs, use 5 ms margin)
        sleep(Duration::from_millis(5)).await;

        // Read response (up to 260 bytes covers max SD2 frame)
        let mut buf = vec![0u8; 260];
        match tokio::time::timeout(
            Duration::from_millis(20),
            port.read(&mut buf)
        ).await {
            Ok(Ok(n)) if n > 0 => {
                if let Some(resp) = parse_sd2_frame(&buf[..n]) {
                    println!(
                        "Cycle {cycle}: slave={:#04x} fc={:#04x} inputs={:02X?}",
                        resp.source, resp.function, resp.data
                    );
                } else {
                    eprintln!("Cycle {cycle}: invalid frame from slave");
                }
            }
            _ => eprintln!("Cycle {cycle}: timeout waiting for slave {slave_addr:#04x}"),
        }
    }

    Ok(())
}
```

### Rust Example 5: Linux RS-485 UART Mode (Kernel TIOCSRS485)

```rust
// rs485_ioctl.rs
//! Configure a Linux UART for automatic RS-485 direction switching using the
//! TIOCSRS485 ioctl. This eliminates software-controlled RTS toggling and
//! is more accurate at high baud rates.

use std::os::unix::io::AsRawFd;
use std::fs::OpenOptions;
use std::io;

/// Linux serial_rs485 structure (from linux/serial.h)
#[repr(C)]
struct SerialRs485 {
    flags:          u32,
    delay_rts_before_send: u32,
    delay_rts_after_send:  u32,
    padding:        [u32; 5],
}

const SER_RS485_ENABLED:         u32 = 1 << 0;
const SER_RS485_RTS_ON_SEND:     u32 = 1 << 1;
const SER_RS485_RTS_AFTER_SEND:  u32 = 1 << 2;

// ioctl number for TIOCSRS485 on Linux
const TIOCSRS485: u64 = 0x542F;

pub fn enable_rs485_mode(device: &str) -> io::Result<()> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(device)?;

    let mut rs485 = SerialRs485 {
        flags: SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND,
        delay_rts_before_send: 0,   // microseconds before first bit
        delay_rts_after_send:  0,   // microseconds after last bit
        padding: [0u32; 5],
    };

    let ret = unsafe {
        libc::ioctl(file.as_raw_fd(), TIOCSRS485, &mut rs485 as *mut SerialRs485)
    };

    if ret != 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}
```

```toml
# Add to Cargo.toml for the ioctl example:
[dependencies]
libc = "0.2"
```

---

## Repeaters and Segment Isolation

Each RS-485 segment supports up to 32 node loads. When more nodes or longer cable runs are needed:

- **Repeaters** regenerate the signal and electrically isolate segments. They count as one node load on each segment.
- **Optical fibre links** (using OLM — Optical Link Modules) can bridge segments over kilometres with complete galvanic isolation — recommended for segments crossing buildings or areas with high ground potential differences.
- **Segment couplers** also provide power isolation for the bus termination.

Repeaters introduce propagation delays that must be accounted for in the bus cycle time calculation.

---

## Summary

Profibus DP builds its physical layer on top of RS-485 with several important specialisations:

**Electrical layer**: The differential RS-485 bus runs at 9.6 kbps to 12 Mbps with a mandatory three-element termination network (pull-up, pull-down, and termination resistor) at both cable ends. Type A shielded twisted-pair cable is mandatory for reliable operation, especially above 500 kbps.

**Topology**: A strict linear (daisy-chain) bus is required — no stubs. Maximum segment length ranges from 1200 m (at 9.6 kbps) down to 100 m (at 12 Mbps). Up to 32 nodes per segment, extended by repeaters to a maximum of 126 nodes total.

**UART format**: Every character is transmitted as 8 data bits, even parity, 1 stop bit. Even parity provides character-level detection in addition to the frame FCS.

**Framing**: The FDL layer defines SD1, SD2, SD3, SD4, and SC frame types. SD2 is the workhorse for variable-length data exchange. The FCS is an 8-bit arithmetic sum (modulo 256) of the DA, SA, FC, and data bytes.

**Direction control**: Because RS-485 is half-duplex, software (or a hardware RS-485 mode) must toggle DE/RE̅ around each transmission. Timing must guarantee the SYN gap (≥33 bit times idle before a valid frame) and leave time for slave response within the tSDR window.

**Bus access**: Token-passing between masters with master-slave polling within each token hold. Deterministic cycle times are achieved through the Target Rotation Time parameter and careful bus cycle time calculation.

**Implementation**: In both C/C++ and Rust, the key components are: serial port configuration (8E1), TX enable management via RTS or TIOCSRS485 ioctl, FCS computation (simple modulo-256 sum), frame serialisation/deserialisation, and a polling loop managing timing between request and response. On Linux, the TIOCSRS485 kernel feature provides hardware-accurate direction switching at high baud rates, eliminating timing jitter from software-controlled GPIO.

---

*References: IEC 61158-2, IEC 61784-1, Profibus International — Technology and Application (PI-165), EIA/TIA-485-A*