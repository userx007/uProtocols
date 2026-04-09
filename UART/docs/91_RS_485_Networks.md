# 91. RS-485 Networks

**Theory & Hardware** — electrical characteristics (differential voltage thresholds, common-mode range, cable distance vs. baud tradeoffs), bus topology rules, half-duplex vs. full-duplex, termination resistors, and fail-safe biasing.

**Direction Control** — DE/RE pin behavior, the critical timing hazard when releasing the driver too early, and how to use Linux's `TIOCSRS485` ioctl to let the kernel handle it automatically.

**C/C++ Examples:**
- Linux serial port setup with `termios` and `TIOCSRS485` ioctl
- Manual GPIO-based direction control with nanosecond-level timing
- A `RS485Master` C++ class polling multiple slaves with Modbus RTU FC 0x03
- A standalone Modbus RTU frame encoder/decoder with CRC-16

**Rust Examples:**
- Serial port configuration via the `serialport` crate
- A full `RS485Master` struct with read/write register methods, CRC validation, and exception handling
- A Modbus RTU slave responder with unit tests verifying the CRC and register read logic

**Practical Guidance** — a troubleshooting table for the 7 most common RS-485 failure modes, a retry-with-backoff pattern in C, and a comparison table of industrial protocols built on RS-485 (Modbus, BACnet, PROFIBUS, DMX512, etc.).

## Building Industrial Multi-Drop Networks with RS-485

---

## Table of Contents

1. [Introduction](#introduction)
2. [RS-485 Physical Layer](#rs-485-physical-layer)
3. [Electrical Characteristics](#electrical-characteristics)
4. [Network Topology](#network-topology)
5. [Half-Duplex vs Full-Duplex](#half-duplex-vs-full-duplex)
6. [Termination and Biasing](#termination-and-biasing)
7. [Protocol Framing](#protocol-framing)
8. [Direction Control (DE/RE Pins)](#direction-control-dere-pins)
9. [Programming in C/C++](#programming-in-cc)
   - [Linux Serial Port Setup](#linux-serial-port-setup)
   - [RS-485 Direction Control in C](#rs-485-direction-control-in-c)
   - [Master/Slave Communication Example in C++](#masterslave-communication-example-in-c)
   - [Modbus RTU Frame Builder in C](#modbus-rtu-frame-builder-in-c)
10. [Programming in Rust](#programming-in-rust)
    - [Serial Port Configuration in Rust](#serial-port-configuration-in-rust)
    - [RS-485 Master in Rust](#rs-485-master-in-rust)
    - [Modbus RTU in Rust](#modbus-rtu-in-rust)
11. [Error Handling and Bus Contention](#error-handling-and-bus-contention)
12. [Common Industrial Protocols over RS-485](#common-industrial-protocols-over-rs-485)
13. [Summary](#summary)

---

## Introduction

**RS-485** (also known as TIA/EIA-485) is a standard defining the electrical characteristics of drivers and receivers for balanced digital multipoint systems. It is the backbone of countless industrial communication systems due to its robustness, long-distance capability, and support for multi-drop networks (multiple devices on a single bus).

RS-485 supersedes RS-422 (which is point-to-point) by allowing up to **32 unit loads** (and up to 256 with 1/8th unit-load transceivers) on a single differential pair. It is the physical layer underneath many industrial protocols including **Modbus RTU**, **DMX512**, **BACnet MS/TP**, **PROFIBUS**, and many proprietary systems.

Key reasons RS-485 is dominant in industrial automation:

- **Differential signaling** — immune to common-mode noise
- **Long cable runs** — up to 1200 meters at low baud rates
- **Multi-drop** — up to 32 (or 256) nodes on one bus
- **Half or full duplex** options
- **Low cost** — simple transceivers (e.g., MAX485, SN75176, SP3485)

---

## RS-485 Physical Layer

RS-485 uses a **balanced differential pair** of wires, conventionally labeled:

| Signal | Description |
|--------|-------------|
| **A (−)** | Non-inverting driver output / inverting receiver input |
| **B (+)** | Inverting driver output / non-inverting receiver input |
| **GND** | Signal reference ground (often a 3rd wire) |

> ⚠️ **Note on A/B labeling**: Different manufacturers swap A and B. Always check your transceiver datasheet. Some label them D+/D− or Y/Z.

The differential voltage **V(A) − V(B)** determines the logical state:

| Condition | Logic |
|-----------|-------|
| V(B) − V(A) > +200 mV | Logic 1 (Mark) |
| V(B) − V(A) < −200 mV | Logic 0 (Space) |
| −200 mV < V(B−A) < +200 mV | Undefined / fail-safe region |

The common-mode voltage range is typically **−7V to +12V**, giving excellent rejection of ground noise between nodes.

---

## Electrical Characteristics

| Parameter | Value |
|-----------|-------|
| Maximum cable length | 1200 m @ 100 kbps |
| Maximum data rate | 10 Mbps (short cables) |
| Driver output voltage | ±1.5 V min differential |
| Receiver sensitivity | ±200 mV |
| Common-mode range | −7 V to +12 V |
| Max nodes (standard) | 32 unit loads |
| Max nodes (1/8 UL chips) | 256 |
| Supply voltage | 3.3 V or 5 V |
| Cable impedance | Typically 120 Ω (twisted pair) |

The relationship between baud rate and cable length follows an approximate inverse: higher baud rates require shorter cables. A common rule of thumb is that the product of baud rate (bps) × cable length (m) ≈ 10^8.

---

## Network Topology

RS-485 is designed for a **daisy-chain (bus) topology**:

```
 ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
 │  Master  │    │ Slave 1  │    │ Slave 2  │    │ Slave N  │
 │  Node 0  │    │  Node 1  │    │  Node 2  │    │  Node N  │
 └────┬─────┘    └────┬─────┘    └────┬─────┘    └────┬─────┘
      │               │               │               │
 ─────┴───────────────┴───────────────┴───────────────┴─────
      A/B Bus (twisted pair)
 ═════╤═══════════════════════════════════════════════╤═════
 120Ω ╧ Termination                      Termination ╧ 120Ω
```

**Rules for RS-485 bus topology:**

1. **Linear bus only** — star or tree topologies cause reflections
2. **Termination resistors** at both ends of the cable
3. **One master transmits at a time** (half-duplex) — bus arbitration is application-layer
4. **Short stubs** — device tap lengths should be < 3 meters
5. **Twisted pair cable** is mandatory for noise immunity

---

## Half-Duplex vs Full-Duplex

### Half-Duplex (2-wire, most common)

All nodes share a single twisted pair. Only one node transmits at a time. Direction is controlled by enabling/disabling the driver (DE pin) and receiver (RE pin).

```
Node A ──── A/B ──── Node B ──── A/B ──── Node C
         (shared transmit/receive pair)
```

**Pros:** Only 2 wires, simpler cabling, lower cost  
**Cons:** Requires explicit direction control, lower throughput  

### Full-Duplex (4-wire)

Uses two separate twisted pairs — one for each direction. Typically used between a master and a single slave, or with a 4-wire RS-485 hub.

```
Master TX ──── A1/B1 ──── Slave RX
Master RX ──── A2/B2 ──── Slave TX
```

**Pros:** Simultaneous transmit and receive, no direction switching  
**Cons:** More wires, only practical for point-to-point or star (with hub)

---

## Termination and Biasing

### Termination Resistors

Termination prevents signal reflections at cable ends. Place a **120 Ω resistor** (matching cable characteristic impedance) between A and B at **both ends** of the bus only.

```c
// In software: no code needed — this is hardware
// Resistor value = cable characteristic impedance (typ. 120 Ω)
// Power: P = V² / R = (5V)² / 120 = ~200 mW (use 0.25W or 0.5W)
```

### Fail-Safe Biasing

When no node is transmitting (bus idle), the A/B lines float into the undefined region. Fail-safe bias resistors ensure a defined idle state (logic 1 = MARK = no transmission):

```
VCC
 │
[Rpull-up ~560Ω]  ← Pull B toward VCC (logic 1)
 │
 ├──── B line
 │
[Rpull-down ~560Ω] ← Pull A toward GND (logic 1)
 │
GND
```

Some transceivers (e.g., MAX3430) have built-in fail-safe biasing.

---

## Protocol Framing

RS-485 is a **physical layer only** — it carries UART frames (start bit, data bits, optional parity, stop bits). Upper-layer protocols add addressing and framing.

A typical UART frame on RS-485:

```
IDLE  [Start][D0][D1][D2][D3][D4][D5][D6][D7][Parity][Stop] IDLE
  1      0    .   .   .   .   .   .   .   .    0/1     1      1
```

For **Modbus RTU** (the most common RS-485 protocol):

```
[Address 1B][Function 1B][Data N bytes][CRC-16 2B]
```

- **Inter-frame silence**: ≥ 3.5 character times at the configured baud rate
- **Address 0**: Broadcast (no response expected)
- **Addresses 1–247**: Individual slave devices

---

## Direction Control (DE/RE Pins)

In half-duplex RS-485, the transceiver has:

| Pin | Name | Function |
|-----|------|----------|
| DE  | Driver Enable | HIGH = transmit; LOW = high-impedance (listen) |
| RE  | Receiver Enable | LOW = receive enabled; HIGH = disabled |

Typically DE and /RE are tied together and controlled by a single GPIO.

**Critical timing requirement:**

```
GPIO HIGH → [DE assert time ~150 ns] → First bit transmitted
Last bit transmitted → [DE release time ~150 ns] → GPIO LOW
```

Failure to hold DE high long enough results in the last bits being corrupted. On Linux with software-controlled GPIO, you must add small delays or use hardware auto-direction control (RS-485 mode in the kernel).

---

## Programming in C/C++

### Linux Serial Port Setup

```c
/**
 * rs485_init.c
 * Initialize a Linux serial port for RS-485 communication.
 * Requires: #include <linux/serial.h> for RS485 ioctls
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

#define RS485_PORT   "/dev/ttyS0"
#define BAUD_RATE    B9600

/**
 * Open and configure a serial port for RS-485.
 *
 * On modern Linux kernels (3.x+), the kernel can handle RS-485
 * direction control automatically via the SER_RS485_ENABLED flag.
 * This eliminates the need for manual GPIO toggling.
 *
 * @param port  Device path, e.g. "/dev/ttyS0" or "/dev/ttyUSB0"
 * @param baud  Baud rate constant, e.g. B9600
 * @return      File descriptor on success, -1 on error
 */
int rs485_open(const char *port, speed_t baud) {
    int fd;
    struct termios tty;
    struct serial_rs485 rs485conf;

    /* Open port: read/write, no controlling terminal, non-blocking */
    fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    /* Clear any existing termios settings */
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    /* Set baud rate */
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    /* 8N1 (8 data bits, no parity, 1 stop bit) */
    tty.c_cflag &= ~PARENB;          /* No parity */
    tty.c_cflag &= ~CSTOPB;          /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;              /* 8 data bits */
    tty.c_cflag &= ~CRTSCTS;         /* No hardware flow control */
    tty.c_cflag |= CREAD | CLOCAL;   /* Enable receiver, ignore modem lines */

    /* Raw mode: no echo, no signal handling, no canonical processing */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    /* Disable software flow control */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);

    /* Disable any special output processing */
    tty.c_oflag &= ~OPOST;

    /* Blocking read: return when at least 1 byte available, no timeout */
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    /* -------------------------------------------------------
     * Enable kernel RS-485 mode (hardware direction control).
     * The kernel will automatically toggle RTS/DE before/after
     * each write(), eliminating manual GPIO handling.
     * ------------------------------------------------------- */
    memset(&rs485conf, 0, sizeof(rs485conf));
    rs485conf.flags |= SER_RS485_ENABLED;
    rs485conf.flags |= SER_RS485_RTS_ON_SEND;     /* DE HIGH during transmit */
    rs485conf.flags &= ~SER_RS485_RTS_AFTER_SEND; /* DE LOW after transmit */
    rs485conf.delay_rts_before_send = 0;           /* Delay in ms before send */
    rs485conf.delay_rts_after_send  = 0;           /* Delay in ms after send */

    if (ioctl(fd, TIOCSRS485, &rs485conf) < 0) {
        /* Non-fatal: kernel RS-485 mode not supported, use manual GPIO */
        perror("ioctl TIOCSRS485 (will use manual direction control)");
    }

    return fd;
}

int main(void) {
    int fd = rs485_open(RS485_PORT, BAUD_RATE);
    if (fd < 0) {
        fprintf(stderr, "Failed to open RS-485 port\n");
        return EXIT_FAILURE;
    }

    printf("RS-485 port opened: %s\n", RS485_PORT);

    /* Example: transmit a simple frame */
    uint8_t frame[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };
    ssize_t written = write(fd, frame, sizeof(frame));
    if (written < 0) {
        perror("write");
    } else {
        printf("Sent %zd bytes\n", written);
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### RS-485 Direction Control in C

When kernel RS-485 mode is unavailable (e.g., USB-to-RS485 adapters, microcontrollers), direction must be controlled manually via GPIO:

```c
/**
 * rs485_gpio_direction.c
 * Manual DE/RE direction control using Linux sysfs GPIO.
 * For production use, consider libgpiod instead of sysfs.
 */
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define DE_RE_GPIO_NUM   "17"           /* GPIO pin controlling DE/RE */
#define GPIO_EXPORT      "/sys/class/gpio/export"
#define GPIO_DIR_PATH    "/sys/class/gpio/gpio" DE_RE_GPIO_NUM "/direction"
#define GPIO_VAL_PATH    "/sys/class/gpio/gpio" DE_RE_GPIO_NUM "/value"

/* Nanosecond delay for direction switching */
static void ns_delay(long ns) {
    struct timespec ts = { .tv_sec = 0, .tv_nsec = ns };
    nanosleep(&ts, NULL);
}

/* Set GPIO direction: "out" or "in" */
static int gpio_set_direction(const char *dir) {
    int fd = open(GPIO_DIR_PATH, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, dir, strlen(dir));
    close(fd);
    return 0;
}

/* Set GPIO value: 1 = drive, 0 = receive */
static int gpio_set_value(int value) {
    int fd = open(GPIO_VAL_PATH, O_WRONLY);
    if (fd < 0) return -1;
    write(fd, value ? "1" : "0", 1);
    close(fd);
    return 0;
}

/**
 * Transmit a frame on RS-485 with manual direction control.
 *
 * Sequence:
 *   1. Assert DE (GPIO HIGH) — enable driver
 *   2. Wait for transceiver propagation delay (~150 ns)
 *   3. Write data to UART
 *   4. tcdrain() — wait until all bytes shifted out of TX FIFO
 *   5. Wait for last bit to finish (bit_time = 1/baud * 10 bits)
 *   6. De-assert DE (GPIO LOW) — disable driver, enable receiver
 */
int rs485_transmit(int uart_fd, int baud, const uint8_t *data, size_t len) {
    /* Bit time in nanoseconds for full UART frame (10 bits per byte: start+8+stop) */
    long bit_time_ns = (1000000000L / baud) * 10;

    /* Step 1 & 2: Assert DE */
    gpio_set_value(1);
    ns_delay(200);  /* Allow driver to assert bus (~150ns typical) */

    /* Step 3: Write data */
    ssize_t written = write(uart_fd, data, len);

    /* Step 4: Wait for TX FIFO to drain */
    tcdrain(uart_fd);

    /* Step 5: Wait for the last bit to finish */
    ns_delay(bit_time_ns);

    /* Step 6: Release bus */
    gpio_set_value(0);

    return (int)written;
}
```

---

### Master/Slave Communication Example in C++

```cpp
/**
 * rs485_master.cpp
 * RS-485 multi-drop master: polls multiple slaves sequentially.
 * Uses POSIX serial I/O. Compile: g++ -std=c++17 -o master rs485_master.cpp
 */
#include <iostream>
#include <vector>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <chrono>
#include <thread>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/select.h>

/* ------------------------------------------------------------------ */
/* CRC-16/MODBUS calculation                                            */
/* ------------------------------------------------------------------ */
static uint16_t crc16_modbus(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

/* ------------------------------------------------------------------ */
/* RS485Master class                                                    */
/* ------------------------------------------------------------------ */
class RS485Master {
public:
    explicit RS485Master(const std::string &port, int baud = 9600)
        : fd_(-1), baud_(baud)
    {
        fd_ = openPort(port, baud);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open RS-485 port: " + port);
    }

    ~RS485Master() {
        if (fd_ >= 0) close(fd_);
    }

    /**
     * Send a Modbus RTU Read Holding Registers request.
     * @param slave_addr  Slave address (1–247)
     * @param start_reg   Starting register address (0-based)
     * @param num_regs    Number of registers to read (1–125)
     * @return            Vector of register values, empty on error
     */
    std::vector<uint16_t> readHoldingRegisters(
        uint8_t slave_addr, uint16_t start_reg, uint16_t num_regs)
    {
        /* Build request frame: [addr][0x03][reg_hi][reg_lo][count_hi][count_lo][crc_lo][crc_hi] */
        uint8_t request[8];
        request[0] = slave_addr;
        request[1] = 0x03;  /* Function code: Read Holding Registers */
        request[2] = (start_reg >> 8) & 0xFF;
        request[3] = start_reg & 0xFF;
        request[4] = (num_regs >> 8) & 0xFF;
        request[5] = num_regs & 0xFF;

        uint16_t crc = crc16_modbus(request, 6);
        request[6] = crc & 0xFF;        /* CRC low byte first in Modbus RTU */
        request[7] = (crc >> 8) & 0xFF;

        /* Transmit request */
        tcflush(fd_, TCIFLUSH);  /* Clear receive buffer before transmit */
        ssize_t sent = write(fd_, request, sizeof(request));
        if (sent != sizeof(request)) {
            std::cerr << "Write error\n";
            return {};
        }
        tcdrain(fd_);

        /* Expected response: [addr][0x03][byte_count][data...][crc_lo][crc_hi] */
        /* byte_count = num_regs * 2 */
        size_t response_len = 5 + num_regs * 2;
        std::vector<uint8_t> response(response_len);

        if (!readResponse(response.data(), response_len, 500)) {
            std::cerr << "Timeout waiting for slave " << (int)slave_addr << "\n";
            return {};
        }

        /* Validate CRC */
        uint16_t rx_crc = (response[response_len - 1] << 8) | response[response_len - 2];
        uint16_t calc_crc = crc16_modbus(response.data(), response_len - 2);
        if (rx_crc != calc_crc) {
            std::cerr << "CRC mismatch from slave " << (int)slave_addr << "\n";
            return {};
        }

        /* Parse register values (big-endian 16-bit) */
        std::vector<uint16_t> regs;
        regs.reserve(num_regs);
        for (uint16_t i = 0; i < num_regs; i++) {
            uint16_t val = ((uint16_t)response[3 + i * 2] << 8) | response[4 + i * 2];
            regs.push_back(val);
        }
        return regs;
    }

private:
    int fd_;
    int baud_;

    int openPort(const std::string &port, int baud) {
        int fd = open(port.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd < 0) return -1;
        fcntl(fd, F_SETFL, 0);  /* Blocking mode */

        struct termios tty{};
        tcgetattr(fd, &tty);
        speed_t speed = (baud == 19200) ? B19200 :
                        (baud == 38400) ? B38400 :
                        (baud == 115200) ? B115200 : B9600;
        cfsetspeed(&tty, speed);
        cfmakeraw(&tty);
        tty.c_cflag |= (CLOCAL | CREAD);
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10; /* 1 second timeout */
        tcsetattr(fd, TCSANOW, &tty);
        return fd;
    }

    /* Read exactly 'len' bytes within 'timeout_ms' milliseconds */
    bool readResponse(uint8_t *buf, size_t len, int timeout_ms) {
        size_t received = 0;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (received < len) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) return false;

            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd_, &fds);
            auto remaining = deadline - now;
            struct timeval tv;
            tv.tv_sec  = std::chrono::duration_cast<std::chrono::seconds>(remaining).count();
            tv.tv_usec = std::chrono::duration_cast<std::chrono::microseconds>(
                             remaining % std::chrono::seconds(1)).count();

            int ret = select(fd_ + 1, &fds, nullptr, nullptr, &tv);
            if (ret <= 0) return false;

            ssize_t n = read(fd_, buf + received, len - received);
            if (n > 0) received += n;
        }
        return true;
    }
};

/* ------------------------------------------------------------------ */
/* Main: poll 3 slaves                                                  */
/* ------------------------------------------------------------------ */
int main() {
    try {
        RS485Master master("/dev/ttyS0", 9600);

        for (uint8_t slave = 1; slave <= 3; slave++) {
            std::cout << "Polling slave " << (int)slave << "...\n";
            auto regs = master.readHoldingRegisters(slave, 0, 4);
            if (regs.empty()) {
                std::cout << "  No response\n";
            } else {
                for (size_t i = 0; i < regs.size(); i++)
                    std::cout << "  Reg[" << i << "] = " << regs[i] << "\n";
            }
            /* Inter-poll delay: allow bus to return to idle */
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

### Modbus RTU Frame Builder in C

```c
/**
 * modbus_rtu.c
 * Lightweight Modbus RTU frame encoder/decoder for RS-485.
 * Supports function codes 01, 02, 03, 04, 05, 06, 15, 16.
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define MODBUS_MAX_FRAME  256
#define MODBUS_FC_READ_COILS         0x01
#define MODBUS_FC_READ_DISCRETE      0x02
#define MODBUS_FC_READ_HOLDING_REGS  0x03
#define MODBUS_FC_READ_INPUT_REGS    0x04
#define MODBUS_FC_WRITE_SINGLE_COIL  0x05
#define MODBUS_FC_WRITE_SINGLE_REG   0x06
#define MODBUS_FC_WRITE_MULTI_REGS   0x10

typedef struct {
    uint8_t  addr;           /* Slave address */
    uint8_t  func;           /* Function code */
    uint8_t  data[253];      /* Payload (max PDU data = 253 bytes) */
    uint8_t  data_len;       /* Length of data[] */
} ModbusFrame;

/* CRC-16/MODBUS: polynomial 0xA001, init 0xFFFF */
uint16_t modbus_crc16(const uint8_t *buf, uint16_t len) {
    uint16_t crc = 0xFFFF;
    while (len--) {
        crc ^= *buf++;
        for (int i = 0; i < 8; i++)
            crc = (crc & 1) ? (crc >> 1) ^ 0xA001 : (crc >> 1);
    }
    return crc;
}

/**
 * Encode a ModbusFrame into a raw RTU byte stream.
 * @param frame   Input frame to encode
 * @param out     Output buffer (at least MODBUS_MAX_FRAME bytes)
 * @return        Number of bytes in output, -1 on error
 */
int modbus_encode(const ModbusFrame *frame, uint8_t *out) {
    if (!frame || !out) return -1;

    out[0] = frame->addr;
    out[1] = frame->func;
    memcpy(&out[2], frame->data, frame->data_len);

    uint16_t crc = modbus_crc16(out, 2 + frame->data_len);
    out[2 + frame->data_len]     = crc & 0xFF;       /* Low byte first */
    out[2 + frame->data_len + 1] = (crc >> 8) & 0xFF;

    return 2 + frame->data_len + 2;
}

/**
 * Decode a raw RTU byte stream into a ModbusFrame.
 * Validates CRC.
 * @return  0 on success, -1 on CRC error, -2 on invalid length
 */
int modbus_decode(const uint8_t *raw, uint16_t raw_len, ModbusFrame *frame) {
    if (raw_len < 4) return -2;  /* Minimum: addr + func + 2 CRC bytes */

    uint16_t calc_crc = modbus_crc16(raw, raw_len - 2);
    uint16_t rx_crc   = raw[raw_len - 2] | ((uint16_t)raw[raw_len - 1] << 8);
    if (calc_crc != rx_crc) return -1;

    frame->addr     = raw[0];
    frame->func     = raw[1];
    frame->data_len = (uint8_t)(raw_len - 4);
    memcpy(frame->data, &raw[2], frame->data_len);
    return 0;
}

/**
 * Build a "Read Holding Registers" request frame (FC 0x03).
 */
int modbus_build_read_regs(uint8_t addr, uint16_t start, uint16_t count,
                            uint8_t *out) {
    ModbusFrame frame;
    frame.addr     = addr;
    frame.func     = MODBUS_FC_READ_HOLDING_REGS;
    frame.data[0]  = (start >> 8) & 0xFF;
    frame.data[1]  =  start       & 0xFF;
    frame.data[2]  = (count >> 8) & 0xFF;
    frame.data[3]  =  count       & 0xFF;
    frame.data_len = 4;
    return modbus_encode(&frame, out);
}

/**
 * Build a "Write Single Register" request frame (FC 0x06).
 */
int modbus_build_write_reg(uint8_t addr, uint16_t reg, uint16_t value,
                            uint8_t *out) {
    ModbusFrame frame;
    frame.addr     = addr;
    frame.func     = MODBUS_FC_WRITE_SINGLE_REG;
    frame.data[0]  = (reg   >> 8) & 0xFF;
    frame.data[1]  =  reg         & 0xFF;
    frame.data[2]  = (value >> 8) & 0xFF;
    frame.data[3]  =  value       & 0xFF;
    frame.data_len = 4;
    return modbus_encode(&frame, out);
}

/* Demo */
int main(void) {
    uint8_t buf[MODBUS_MAX_FRAME];
    int len;

    /* Read 10 holding registers from slave 3, starting at register 100 */
    len = modbus_build_read_regs(3, 100, 10, buf);
    printf("Read Regs frame (%d bytes): ", len);
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");

    /* Write value 0x1234 to register 5 of slave 7 */
    len = modbus_build_write_reg(7, 5, 0x1234, buf);
    printf("Write Reg frame (%d bytes): ", len);
    for (int i = 0; i < len; i++) printf("%02X ", buf[i]);
    printf("\n");

    return 0;
}
```

---

## Programming in Rust

### Serial Port Configuration in Rust

```toml
# Cargo.toml
[package]
name = "rs485-example"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"
tokio = { version = "1", features = ["full"], optional = true }
```

```rust
// src/serial_setup.rs
//! RS-485 serial port configuration using the `serialport` crate.
//! The `serialport` crate abstracts POSIX termios and Windows COM port APIs.

use serialport::{SerialPort, DataBits, FlowControl, Parity, StopBits};
use std::time::Duration;
use std::io::{self, Read, Write};

/// Open and configure a serial port for RS-485 at the specified baud rate.
///
/// Returns a boxed `SerialPort` trait object ready for read/write.
/// Kernel RS-485 mode (TIOCSRS485) should be configured via udev rules
/// or a pre-configuration step when using the `serialport` crate.
pub fn open_rs485(port_name: &str, baud_rate: u32) -> io::Result<Box<dyn SerialPort>> {
    let port = serialport::new(port_name, baud_rate)
        .data_bits(DataBits::Eight)
        .flow_control(FlowControl::None)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .timeout(Duration::from_millis(500))
        .open()
        .map_err(|e| io::Error::new(io::ErrorKind::Other, e.to_string()))?;

    Ok(port)
}

/// Transmit a byte slice and flush the port buffer.
pub fn rs485_write(port: &mut Box<dyn SerialPort>, data: &[u8]) -> io::Result<usize> {
    let n = port.write(data)?;
    port.flush()?;
    Ok(n)
}

/// Read exactly `expected` bytes, respecting the port's configured timeout.
pub fn rs485_read_exact(
    port: &mut Box<dyn SerialPort>,
    expected: usize,
) -> io::Result<Vec<u8>> {
    let mut buf = vec![0u8; expected];
    let mut received = 0;

    while received < expected {
        match port.read(&mut buf[received..]) {
            Ok(0)  => break,
            Ok(n)  => received += n,
            Err(e) if e.kind() == io::ErrorKind::TimedOut => break,
            Err(e) => return Err(e),
        }
    }
    buf.truncate(received);
    Ok(buf)
}
```

---

### RS-485 Master in Rust

```rust
// src/main.rs
//! RS-485 multi-drop master implementing Modbus RTU read operations.
//! 
//! Usage:
//!   cargo run -- /dev/ttyS0 9600

mod serial_setup;

use std::env;
use std::io;

/* ------------------------------------------------------------------ */
/* Modbus RTU CRC-16                                                    */
/* ------------------------------------------------------------------ */

/// Compute CRC-16/MODBUS over a byte slice.
/// Polynomial: 0xA001 (reflected), initial value: 0xFFFF.
fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            crc = if crc & 0x0001 != 0 {
                (crc >> 1) ^ 0xA001
            } else {
                crc >> 1
            };
        }
    }
    crc
}

/// Append Modbus CRC-16 (little-endian) to a mutable frame buffer.
fn append_crc(frame: &mut Vec<u8>) {
    let crc = crc16_modbus(frame);
    frame.push((crc & 0xFF) as u8);          // Low byte first
    frame.push(((crc >> 8) & 0xFF) as u8);
}

/// Validate CRC of a received Modbus RTU frame.
/// Returns Ok(()) if valid, Err with description if not.
fn validate_crc(frame: &[u8]) -> Result<(), String> {
    if frame.len() < 4 {
        return Err(format!("Frame too short: {} bytes", frame.len()));
    }
    let payload  = &frame[..frame.len() - 2];
    let rx_crc   = (frame[frame.len() - 2] as u16)
                 | ((frame[frame.len() - 1] as u16) << 8);
    let calc_crc = crc16_modbus(payload);
    if rx_crc != calc_crc {
        Err(format!("CRC mismatch: expected {:#06X}, got {:#06X}", calc_crc, rx_crc))
    } else {
        Ok(())
    }
}

/* ------------------------------------------------------------------ */
/* Modbus RTU request builders                                          */
/* ------------------------------------------------------------------ */

/// Build a Read Holding Registers request (FC 0x03).
///
/// Frame: [addr][0x03][start_hi][start_lo][count_hi][count_lo][crc_lo][crc_hi]
fn build_read_holding_regs(slave: u8, start: u16, count: u16) -> Vec<u8> {
    let mut frame = vec![
        slave,
        0x03,
        (start >> 8) as u8,
        (start & 0xFF) as u8,
        (count >> 8) as u8,
        (count & 0xFF) as u8,
    ];
    append_crc(&mut frame);
    frame
}

/// Build a Write Single Register request (FC 0x06).
fn build_write_single_reg(slave: u8, register: u16, value: u16) -> Vec<u8> {
    let mut frame = vec![
        slave,
        0x06,
        (register >> 8) as u8,
        (register & 0xFF) as u8,
        (value >> 8) as u8,
        (value & 0xFF) as u8,
    ];
    append_crc(&mut frame);
    frame
}

/* ------------------------------------------------------------------ */
/* RS485Master struct                                                   */
/* ------------------------------------------------------------------ */

struct RS485Master {
    port: Box<dyn serialport::SerialPort>,
}

impl RS485Master {
    pub fn new(port_name: &str, baud: u32) -> io::Result<Self> {
        let port = serial_setup::open_rs485(port_name, baud)?;
        Ok(Self { port })
    }

    /// Read holding registers from a slave.
    /// Returns a Vec of register values (u16) or an error string.
    pub fn read_holding_registers(
        &mut self,
        slave: u8,
        start: u16,
        count: u16,
    ) -> Result<Vec<u16>, String> {
        /* Build and transmit request */
        let request = build_read_holding_regs(slave, start, count);
        serial_setup::rs485_write(&mut self.port, &request)
            .map_err(|e| format!("Write error: {e}"))?;

        /* Expected response length: addr(1) + fc(1) + byte_count(1) + data(n*2) + crc(2) */
        let expected_len = 5 + (count as usize) * 2;
        let response = serial_setup::rs485_read_exact(&mut self.port, expected_len)
            .map_err(|e| format!("Read error: {e}"))?;

        if response.len() < expected_len {
            return Err(format!(
                "Short response: got {} bytes, expected {}",
                response.len(), expected_len
            ));
        }

        /* Validate */
        validate_crc(&response)?;

        /* Check for exception response (FC with bit7 set) */
        if response[1] & 0x80 != 0 {
            return Err(format!("Modbus exception code: {:#04X}", response[2]));
        }

        /* Parse register values (big-endian) */
        let regs: Vec<u16> = (0..count as usize)
            .map(|i| {
                let hi = response[3 + i * 2] as u16;
                let lo = response[4 + i * 2] as u16;
                (hi << 8) | lo
            })
            .collect();

        Ok(regs)
    }

    /// Write a single register on a slave.
    pub fn write_single_register(
        &mut self,
        slave: u8,
        register: u16,
        value: u16,
    ) -> Result<(), String> {
        let request = build_write_single_reg(slave, register, value);
        serial_setup::rs485_write(&mut self.port, &request)
            .map_err(|e| format!("Write error: {e}"))?;

        /* Echo response is identical to request for FC 0x06 */
        let response = serial_setup::rs485_read_exact(&mut self.port, request.len())
            .map_err(|e| format!("Read error: {e}"))?;

        validate_crc(&response)?;
        Ok(())
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

fn main() {
    let args: Vec<String> = env::args().collect();
    let port_name = args.get(1).map(|s| s.as_str()).unwrap_or("/dev/ttyS0");
    let baud: u32  = args.get(2).and_then(|s| s.parse().ok()).unwrap_or(9600);

    println!("RS-485 Master starting on {} @ {} baud", port_name, baud);

    let mut master = match RS485Master::new(port_name, baud) {
        Ok(m)  => m,
        Err(e) => { eprintln!("Failed to open port: {e}"); return; }
    };

    /* Poll 3 slaves */
    for slave in 1u8..=3 {
        print!("Slave {slave}: ");
        match master.read_holding_registers(slave, 0, 4) {
            Ok(regs) => {
                println!("Regs = {:?}", regs);
            }
            Err(e) => {
                println!("Error — {e}");
            }
        }

        std::thread::sleep(std::time::Duration::from_millis(20));
    }

    /* Write example: set register 10 to value 42 on slave 1 */
    match master.write_single_register(1, 10, 42) {
        Ok(())  => println!("Write to slave 1 reg 10 succeeded"),
        Err(e)  => eprintln!("Write failed: {e}"),
    }
}
```

---

### Modbus RTU in Rust

```rust
// src/modbus_slave.rs
//! Minimal Modbus RTU slave responder.
//! Suitable for embedded targets using `no_std` with minor adaptation
//! (replace Vec with fixed-size arrays, remove std::io dependencies).

/// Maximum size of a Modbus RTU frame (256 bytes per spec)
const MAX_FRAME: usize = 256;

/// Simulated register bank for the slave
struct RegisterBank {
    holding: [u16; 128],
    input:   [u16; 128],
    coils:   [bool; 128],
}

impl RegisterBank {
    fn new() -> Self {
        Self {
            holding: [0u16; 128],
            input:   [0u16; 128],
            coils:   [false; 128],
        }
    }
}

/// Process a received Modbus RTU request and produce a response frame.
///
/// Returns `Some(Vec<u8>)` with the response frame to transmit,
/// or `None` if the request is a broadcast (address 0) or has a CRC error.
fn process_modbus_request(
    raw: &[u8],
    my_address: u8,
    regs: &mut RegisterBank,
) -> Option<Vec<u8>> {
    /* Validate minimum length and CRC */
    if raw.len() < 4 {
        return None;
    }
    let payload_crc = crc16_modbus(&raw[..raw.len() - 2]);
    let rx_crc = (raw[raw.len() - 2] as u16) | ((raw[raw.len() - 1] as u16) << 8);
    if payload_crc != rx_crc {
        eprintln!("CRC error: calc={payload_crc:#06X} rx={rx_crc:#06X}");
        return None;
    }

    let addr = raw[0];

    /* Ignore frames not addressed to us (and broadcast address 0 has no response) */
    if addr != my_address {
        return None;
    }

    let func = raw[1];
    let mut response = Vec::with_capacity(MAX_FRAME);
    response.push(addr);

    match func {
        0x03 => {
            /* Read Holding Registers */
            let start = ((raw[2] as u16) << 8) | raw[3] as u16;
            let count = ((raw[4] as u16) << 8) | raw[5] as u16;

            if start + count > regs.holding.len() as u16 {
                /* Exception: Illegal Data Address */
                response.push(func | 0x80);
                response.push(0x02);
            } else {
                response.push(func);
                response.push((count * 2) as u8); /* Byte count */
                for i in 0..count as usize {
                    let val = regs.holding[start as usize + i];
                    response.push((val >> 8) as u8);
                    response.push((val & 0xFF) as u8);
                }
            }
        }
        0x06 => {
            /* Write Single Register */
            let reg   = ((raw[2] as u16) << 8) | raw[3] as u16;
            let value = ((raw[4] as u16) << 8) | raw[5] as u16;

            if reg as usize >= regs.holding.len() {
                response.push(func | 0x80);
                response.push(0x02);
            } else {
                regs.holding[reg as usize] = value;
                /* Echo the request as response */
                response.push(func);
                response.push(raw[2]);
                response.push(raw[3]);
                response.push(raw[4]);
                response.push(raw[5]);
            }
        }
        _ => {
            /* Exception: Illegal Function */
            response.push(func | 0x80);
            response.push(0x01);
        }
    }

    append_crc_vec(&mut response);
    Some(response)
}

fn crc16_modbus(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &b in data {
        crc ^= b as u16;
        for _ in 0..8 {
            crc = if crc & 1 != 0 { (crc >> 1) ^ 0xA001 } else { crc >> 1 };
        }
    }
    crc
}

fn append_crc_vec(frame: &mut Vec<u8>) {
    let crc = crc16_modbus(frame);
    frame.push((crc & 0xFF) as u8);
    frame.push((crc >> 8) as u8);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_crc16() {
        /* Known Modbus RTU CRC test vector:
         * Request to read 1 register from slave 1 at address 0
         * [0x01][0x03][0x00][0x00][0x00][0x01] → CRC = 0x840A
         */
        let data = [0x01u8, 0x03, 0x00, 0x00, 0x00, 0x01];
        let crc = crc16_modbus(&data);
        assert_eq!(crc, 0x0A84, "CRC = {:#06X}", crc);
    }

    #[test]
    fn test_read_holding_registers() {
        let mut regs = RegisterBank::new();
        regs.holding[0] = 0x1234;
        regs.holding[1] = 0x5678;

        /* FC 03: read 2 regs from address 0, slave 1 */
        let mut request = vec![0x01u8, 0x03, 0x00, 0x00, 0x00, 0x02];
        append_crc_vec(&mut request);

        let response = process_modbus_request(&request, 1, &mut regs)
            .expect("Should produce a response");

        /* Response: [01][03][04][12][34][56][78][crc_lo][crc_hi] */
        assert_eq!(response[0], 0x01);  /* Address */
        assert_eq!(response[1], 0x03);  /* Function code */
        assert_eq!(response[2], 0x04);  /* Byte count = 4 */
        assert_eq!((response[3] as u16) << 8 | response[4] as u16, 0x1234);
        assert_eq!((response[5] as u16) << 8 | response[6] as u16, 0x5678);
    }
}
```

---

## Error Handling and Bus Contention

### Common RS-485 Problems and Solutions

| Problem | Symptom | Cause | Solution |
|---------|---------|-------|----------|
| Bus contention | Garbled data, high current | Two nodes transmitting simultaneously | Strict master/slave protocol; one transmitter at a time |
| Missing termination | Reflections, bit errors at high baud | No end resistors | Add 120 Ω at both cable ends |
| Floating bus | Spurious start bits on idle bus | No bias resistors | Add fail-safe pull-up/pull-down |
| Ground loops | Intermittent errors, transceiver damage | No signal ground wire | Add GND wire; use isolated transceivers |
| Direction timing | Last byte corrupted | DE released too early | Wait `tcdrain()` + 1 bit time before releasing DE |
| Long stubs | Errors at specific nodes | >3m tap cables | Shorten stubs or use active hubs |
| Address collision | Multiple slaves respond | Duplicate address | Audit all slave addresses during commissioning |

### Software Error Handling Pattern in C

```c
/**
 * Robust RS-485 transaction with retry logic.
 * Implements exponential backoff for failed requests.
 */
#define MAX_RETRIES    3
#define BASE_DELAY_MS  10

int rs485_transaction_with_retry(
    int fd,
    const uint8_t *request, size_t req_len,
    uint8_t *response, size_t resp_len,
    int timeout_ms)
{
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        /* Flush stale data from receive buffer */
        tcflush(fd, TCIFLUSH);

        /* Transmit request */
        if (write(fd, request, req_len) != (ssize_t)req_len) {
            fprintf(stderr, "TX error on attempt %d\n", attempt + 1);
            goto retry;
        }
        tcdrain(fd);

        /* Receive response */
        size_t received = 0;
        int elapsed = 0;
        while (received < resp_len && elapsed < timeout_ms) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(fd, &fds);
            struct timeval tv = { .tv_sec = 0, .tv_usec = 5000 }; /* 5ms poll */
            if (select(fd + 1, &fds, NULL, NULL, &tv) > 0) {
                ssize_t n = read(fd, response + received, resp_len - received);
                if (n > 0) received += n;
            }
            elapsed += 5;
        }

        if (received == resp_len) {
            /* Validate CRC */
            uint16_t calc = modbus_crc16(response, resp_len - 2);
            uint16_t rx   = response[resp_len - 2] | ((uint16_t)response[resp_len - 1] << 8);
            if (calc == rx) return 0;  /* Success */
            fprintf(stderr, "CRC error on attempt %d\n", attempt + 1);
        } else {
            fprintf(stderr, "Timeout: got %zu/%zu bytes on attempt %d\n",
                    received, resp_len, attempt + 1);
        }

    retry:
        /* Exponential backoff between retries */
        usleep((BASE_DELAY_MS << attempt) * 1000);
    }
    return -1; /* All attempts failed */
}
```

---

## Common Industrial Protocols over RS-485

| Protocol | Topology | Addressing | Frame Type | Typical Use |
|----------|----------|------------|------------|-------------|
| **Modbus RTU** | Master/Slave | 1–247 | Binary with CRC-16 | PLCs, sensors, drives |
| **BACnet MS/TP** | Master/Slave + Token | 0–254 | Binary with CRC-16 | Building automation |
| **PROFIBUS DP** | Master/Slave + Multi-master | 0–126 | Binary with Hamming | Factory automation |
| **DMX512** | Broadcast | 1–512 channels | Binary, no CRC | Stage lighting |
| **Modbus ASCII** | Master/Slave | 1–247 | ASCII with LRC | Legacy systems |
| **LIN bus** | Master/Slave | 1–63 | Binary with checksum | Automotive subsystems |

---

## Summary

RS-485 is the proven physical layer for industrial multi-drop serial networks. Its differential signaling gives it immunity to electrical noise, long-cable capability, and multi-node support that makes it indispensable in factories, buildings, and process control environments.

**Key engineering takeaways:**

- Always use **twisted pair cable** with a **characteristic impedance of 120 Ω** and terminate both ends with matching resistors to prevent reflections.
- Add **fail-safe bias resistors** to define the bus state during idle periods and prevent spurious start bits.
- RS-485 is **half-duplex by default** — implement strict bus discipline at the application layer (typically master/slave) so only one node drives the bus at a time.
- Direction control (DE/RE) is critical: always use `tcdrain()` plus a guard delay before releasing the driver, or configure the Linux kernel's native **TIOCSRS485** mode to automate this.
- **Modbus RTU** is the dominant application-layer protocol on RS-485 — a straightforward binary framing with CRC-16/MODBUS, supporting up to 247 individually addressed slave nodes.
- In **C/C++**, use POSIX `termios` for port configuration and the `TIOCSRS485` ioctl for hardware direction control. Implement CRC validation and retry logic for robust communication.
- In **Rust**, the `serialport` crate provides a clean cross-platform abstraction. Rust's ownership model naturally prevents the resource management bugs (double-close, use-after-free on file descriptors) common in C RS-485 drivers.
- Always implement **timeout and retry logic** with at least 3 attempts and inter-retry backoff, as transient noise bursts are common in industrial environments.

RS-485's longevity (standardized in 1983, still dominant today) is a testament to the elegance of differential signaling for noisy industrial environments — a technique that remains essential knowledge for embedded and industrial software engineers.

---

*Document: RS-485 Networks — Part of UART Series, Topic 91*