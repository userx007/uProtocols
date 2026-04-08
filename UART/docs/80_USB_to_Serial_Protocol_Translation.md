# 80. USB-to-Serial Protocol Translation

**Theory & Architecture**
- Full stack diagram from application layer down to physical UART pins
- USB transfer types (Bulk, Interrupt, Control) and their roles in serial bridging
- UART frame structure, line coding parameters, and modem signal semantics
- CDC-ACM standard (SET_LINE_CODING, GET_LINE_CODING, SET_CONTROL_LINE_STATE) with packed structs
- Vendor-specific protocols for FTDI, CP210x, CH340, PL2303 — including FTDI's 2-byte status header and baud divisor calculation

**Key Concepts**
- The flush timeout problem — why USB-serial has an irreducible 1–16 ms latency floor
- Modem signal translation table (DTR/RTS/CTS/DSR via USB control requests)
- Flow control bridging (hardware RTS/CTS, XON/XOFF, USB back-pressure)

**C/C++ Code Examples**
- Linux `termios` full configuration (custom baud rates, modem lines, select-based timeout)
- Windows Win32 COM API with overlapped (async) I/O
- Direct `libusb` access to an FTDI chip — including divisor calculation and status header stripping
- CDC-ACM bridge firmware in C (for bare-metal MCUs) with the complete buffering state machine

**Rust Code Examples**
- `serialport` crate: port enumeration by VID/PID, full configuration, DTR/RTS control
- `rusb` crate: direct CDC-ACM control requests (SET/GET_LINE_CODING, SEND_BREAK)
- Async bidirectional bridge with `tokio-serial` and `mpsc` channels

**Practical Sections**
- Error handling table (overrun, baud mismatch, DTR reset, CH340 latency limitations)
- Linux debugging toolkit (`lsusb`, `usbmon`, Wireshark CDC-ACM dissection, `latency_timer`)
- RP2040/TinyUSB embedded firmware example

## Bridging Between USB and UART at the Protocol Level

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [USB Protocol Fundamentals Relevant to Serial Bridging](#usb-protocol-fundamentals)
4. [UART Protocol Fundamentals](#uart-protocol-fundamentals)
5. [Bridge Chip Architecture and Driver Model](#bridge-chip-architecture)
6. [CDC-ACM: USB Communications Device Class](#cdc-acm)
7. [Vendor-Specific Protocols (FTDI, CP210x, CH34x, PL2303)](#vendor-specific-protocols)
8. [Protocol Translation Mechanics](#protocol-translation-mechanics)
9. [Latency and Buffering Considerations](#latency-and-buffering)
10. [Flow Control Bridging](#flow-control-bridging)
11. [Line Coding and Baud Rate Negotiation](#line-coding-and-baud-rate)
12. [Programming in C/C++](#programming-in-c-cpp)
    - [Linux: Using termios and the USB-Serial Device](#linux-termios)
    - [Windows: Using Win32 COM API](#windows-com-api)
    - [Direct libusb Access (Bypassing the Driver)](#libusb-direct)
    - [Implementing a Software CDC-ACM Bridge in C](#software-cdc-bridge-c)
13. [Programming in Rust](#programming-in-rust)
    - [Using the serialport Crate](#rust-serialport)
    - [Using rusb for Direct USB Communication](#rust-rusb)
    - [Async USB-Serial Bridge with Tokio](#rust-async-bridge)
14. [Embedded Firmware: Implementing USB-to-UART on a Microcontroller](#embedded-firmware)
15. [Error Handling and Edge Cases](#error-handling)
16. [Debugging and Troubleshooting](#debugging)
17. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

USB-to-Serial protocol translation is one of the most ubiquitous hardware/software bridging challenges in embedded systems, development tooling, and industrial automation. Every time a developer flashes firmware via a USB cable, connects to a serial console on a Raspberry Pi, or communicates with a microcontroller from a PC, some form of USB-to-UART translation is happening.

The challenge is not trivial: USB and UART are architecturally very different protocols. USB is a packet-oriented, host-initiated, enumeration-based bus with strict timing and class abstractions. UART is a simple, asynchronous, byte-stream protocol with no master/slave distinction at the electrical level. Bridging them requires translating not just data bytes, but also signalling, flow control, error states, and timing semantics.

This document covers the protocol translation problem in depth — from the USB and UART fundamentals, through bridge chip architectures and software drivers, to practical C/C++ and Rust code examples for both host-side and embedded (firmware) implementations.

---

## 2. Architecture Overview <a name="architecture-overview"></a>

A USB-to-Serial bridge system has the following layers:

```
┌──────────────────────────────────────────────────┐
│              Host Application                    │
│    (opens /dev/ttyUSB0 or COM3, reads/writes)    │
├──────────────────────────────────────────────────┤
│              OS Serial API Layer                 │
│    (termios, Win32 COMMAPI, pyserial, etc.)      │
├──────────────────────────────────────────────────┤
│              USB-Serial Kernel Driver            │
│    (cdc_acm, ftdi_sio, cp210x, ch341, pl2303)    │
├──────────────────────────────────────────────────┤
│              USB Host Controller Driver          │
│    (XHCI, EHCI, OHCI)                            │
├──────────────────────────────────────────────────┤
│              USB Physical Bus (D+/D-)            │
└──────────────────────────────────────────────────┘
                        │
              ┌─────────▼──────────┐
              │  USB-Serial Bridge  │
              │  Chip (FTDI, CP2102 │
              │  CH340, CDC-ACM MCU)│
              └─────────┬──────────┘
                        │
┌───────────────────────▼──────────────────────────┐
│         UART TX/RX/RTS/CTS/DTR/DSR               │
├──────────────────────────────────────────────────┤
│         Target Device (MCU, SoC, Modem)          │
└──────────────────────────────────────────────────┘
```

The bridge chip is the physical translator. It presents a USB device interface to the host and a UART interface to the target device. The kernel driver on the host exposes this as a virtual COM port (VCP) or a character device, making it transparent to applications.

There are two broad approaches:

- **Dedicated bridge chips**: FTDI FT232, Silicon Labs CP2102/CP2104, WCH CH340/CH341, Prolific PL2303. These handle the USB-to-UART translation entirely in silicon.
- **Microcontroller-based bridges**: A microcontroller (STM32, RP2040, ATmega32U4) runs firmware that implements a USB device stack and a UART peripheral, acting as the bridge in software.

---

## 3. USB Protocol Fundamentals Relevant to Serial Bridging <a name="usb-protocol-fundamentals"></a>

### Transfer Types

USB defines four transfer types. For serial bridging, two are relevant:

| Transfer Type | Usage in USB-Serial |
|---|---|
| **Bulk** | Main data path (TX and RX bytes). Guaranteed delivery, no timing guarantee. |
| **Interrupt** | Status/control notifications (line state, modem signals). Small, periodic packets. |
| **Control** | Device setup, line coding configuration (baud rate, data bits, etc.) |
| **Isochronous** | Not used in serial bridges |

### Endpoints

A typical USB-serial device exposes:

- **EP0** (Control IN/OUT): Device enumeration, CDC requests (Set_Line_Coding, Set_Control_Line_State)
- **EP1 BULK IN**: Device → Host data (UART RX data sent to PC)
- **EP2 BULK OUT**: Host → Device data (PC TX data sent to UART TX)
- **EP3 INTERRUPT IN**: Device → Host notifications (line state changes, break, error flags)

### USB Latency and Packet Granularity

USB Full Speed operates at 1 ms frame intervals. A USB-serial bridge must buffer incoming UART bytes and flush them to the USB host either when the buffer is full or on a periodic timeout (typically 1–16 ms). This introduces irreducible latency that does not exist on a native UART.

---

## 4. UART Protocol Fundamentals <a name="uart-protocol-fundamentals"></a>

UART (Universal Asynchronous Receiver/Transmitter) transmits data as a serial bitstream with the following frame structure:

```
Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  Parity  Stop
 HIGH   LOW  ...data bits (LSB first)...     opt.    HIGH
```

Key parameters (the "line coding"):
- **Baud rate**: Bits per second (e.g., 9600, 115200, 921600)
- **Data bits**: 5, 6, 7, or 8 (almost universally 8)
- **Parity**: None, Even, Odd, Mark, Space
- **Stop bits**: 1, 1.5, or 2

UART also defines modem control lines:
- **RTS** (Request to Send), **CTS** (Clear to Send) — hardware flow control
- **DTR** (Data Terminal Ready), **DSR** (Data Set Ready) — device presence/ready
- **DCD** (Data Carrier Detect), **RI** (Ring Indicator) — modem-specific

These signals must be mapped into the USB protocol space, which is one of the key translation challenges.

---

## 5. Bridge Chip Architecture and Driver Model <a name="bridge-chip-architecture"></a>

### Common Bridge Chips

| Chip | Vendor | USB Class | Max Baud | Notes |
|---|---|---|---|---|
| FT232RL/FT232H | FTDI | Vendor-specific | 3 Mbaud | Very common, well-supported |
| CP2102/CP2104 | Silicon Labs | Vendor-specific | 1 Mbaud | Low power, popular in ESP32 boards |
| CH340/CH341 | WCH | Vendor-specific | 2 Mbaud | Very cheap, used in clone boards |
| PL2303 | Prolific | Vendor-specific | 1.2 Mbaud | Older, many counterfeit chips exist |
| STM32 (CDC) | ST + user firmware | CDC-ACM | ~2 Mbaud | Flexible, software-defined |
| RP2040 (TinyUSB) | Raspberry Pi | CDC-ACM | ~1 Mbaud | Used in Pico as debug bridge |

### Driver-to-Device Communication

The kernel driver communicates with the bridge chip over USB using the chip's specific protocol. For CDC-ACM devices this is standardized; for vendor chips it is proprietary. In both cases the driver exposes a `/dev/ttyUSBx` (Linux) or `COMx` (Windows) device to user space.

---

## 6. CDC-ACM: USB Communications Device Class <a name="cdc-acm"></a>

CDC-ACM (Abstract Control Model) is the USB standard for serial communication. It is defined in the USB CDC specification (USB Class Definitions for Communications Devices, v1.2).

### Descriptors

A CDC-ACM device uses two interfaces:
1. **CDC Control Interface** (class 0x02, subclass 0x02): Carries the interrupt endpoint for status notifications and handles control requests.
2. **CDC Data Interface** (class 0x0A): Carries the bulk IN/OUT endpoints for data.

### Key CDC-ACM Requests (sent over EP0)

| Request | Code | Direction | Description |
|---|---|---|---|
| `SET_LINE_CODING` | 0x20 | Host→Device | Set baud rate, data bits, parity, stop bits |
| `GET_LINE_CODING` | 0x21 | Device→Host | Query current line coding |
| `SET_CONTROL_LINE_STATE` | 0x22 | Host→Device | Set RTS and DTR signals |
| `SEND_BREAK` | 0x23 | Host→Device | Send a UART break condition |

### Line Coding Structure (7 bytes)

```c
typedef struct {
    uint32_t dwDTERate;   // Baud rate in bits/second (little-endian)
    uint8_t  bCharFormat; // Stop bits: 0=1, 1=1.5, 2=2
    uint8_t  bParityType; // 0=None, 1=Odd, 2=Even, 3=Mark, 4=Space
    uint8_t  bDataBits;   // Data bits: 5, 6, 7, 8, or 16
} __attribute__((packed)) cdc_line_coding_t;
```

### Serial State Notification (10 bytes, sent via Interrupt IN)

```c
// CDC Serial State notification
// bmRequestType = 0xA1, bRequest = 0x20 (SERIAL_STATE)
typedef struct {
    uint8_t  bmRequestType; // 0xA1
    uint8_t  bNotification; // 0x20
    uint16_t wValue;        // 0
    uint16_t wIndex;        // Interface number
    uint16_t wLength;       // 2
    uint16_t data;          // Bitmask of serial state flags
} __attribute__((packed)) cdc_serial_state_t;

// data bitmask:
// Bit 0: DCD   Bit 1: DSR   Bit 2: Break  Bit 3: Ring
// Bit 4: FramingError  Bit 5: ParityError  Bit 6: Overrun
```

---

## 7. Vendor-Specific Protocols <a name="vendor-specific-protocols"></a>

### FTDI Protocol

FTDI chips use vendor-specific USB requests (bRequest values in the range 0x00–0x10). The kernel driver `ftdi_sio` handles these. Key requests:

| Request | bRequest | Description |
|---|---|---|
| Reset | 0x00 | Reset the device |
| Set Modem Ctrl | 0x01 | Set DTR/RTS |
| Set Flow Ctrl | 0x02 | Set flow control mode |
| Set Baud Rate | 0x03 | Set baud rate divisor |
| Set Data | 0x04 | Set data bits, parity, stop bits |
| Get Modem Status | 0x05 | Read CTS, DSR, RI, DCD |

FTDI uses a custom baud rate divisor system: the base clock (3 MHz or 6 MHz) is divided by a fractional divisor encoded in a 14-bit field. This allows very flexible baud rate generation but requires non-trivial divisor calculation.

Every FTDI bulk IN packet is prepended with a **2-byte status header** that the driver strips before presenting data to the application:

```
Byte 0: Modem status (CTS, DSR, RI, DCD)
Byte 1: Line status (Receive error flags)
Byte 2–N: Actual data bytes
```

This means minimum USB packet overhead is 2 bytes even for empty polls — important when understanding latency.

### CP210x Protocol

Silicon Labs CP210x uses vendor-specific control requests on the control endpoint. Baud rate is set directly as a 32-bit integer (no divisor calculation needed), making it simpler to program directly.

### CH340/CH341

WCH chips use vendor-specific control requests. The CH341 also supports I2C and SPI modes. The baud rate encoding uses a two-register prescaler/divisor scheme that requires specific calculation.

---

## 8. Protocol Translation Mechanics <a name="protocol-translation-mechanics"></a>

The core translation loop in a bridge chip or firmware is:

### USB → UART Direction (Host TX → Target RX)

1. Host sends bulk OUT packet to the bridge
2. Bridge firmware receives bytes into a TX FIFO
3. Bridge UART peripheral serializes bytes at the configured baud rate
4. Bytes appear on the UART TX pin

### UART → USB Direction (Target TX → Host RX)

1. Target sends UART bytes at the configured baud rate
2. Bridge UART peripheral receives bytes into an RX FIFO
3. Bridge firmware accumulates bytes until either:
   - The RX FIFO reaches a threshold (e.g., 64 bytes), or
   - A timeout fires (e.g., 1 ms), whichever comes first
4. Bridge sends a bulk IN packet to the host

The timeout-based flush is critical: without it, a single byte response from a device (e.g., a prompt `>`) would wait indefinitely in the buffer, never reaching the host. This is the source of the characteristic ~1–16 ms latency floor on USB-serial bridges.

### Modem Signal Translation

| UART Signal | USB Mechanism |
|---|---|
| RTS (output from host) | `SET_CONTROL_LINE_STATE` bit 1 |
| DTR (output from host) | `SET_CONTROL_LINE_STATE` bit 0 |
| CTS (input to host) | Serial State notification bit (or modem status byte for FTDI) |
| DSR (input to host) | Serial State notification |
| DCD (input to host) | Serial State notification |
| Break (output from host) | `SEND_BREAK` request |

---

## 9. Latency and Buffering Considerations <a name="latency-and-buffering"></a>

USB-serial latency has two components:

1. **USB polling latency**: For Full Speed USB (12 Mbps), the host polls every 1 ms. For High Speed (480 Mbps) with a microframe interval, this can be as low as 125 µs.
2. **Bridge flush timeout**: The firmware-configurable timeout before a partial buffer is flushed upstream. FTDI allows this to be set via `SET_LATENCY_TIMER` (default: 16 ms, minimum: 1 ms via `ftdi_sio` `latency_timer` sysfs attribute on Linux).

For latency-sensitive applications (e.g., bitbang protocols, precise timing):

```bash
# Linux: reduce FTDI latency timer to 1ms
echo 1 | sudo tee /sys/bus/usb-serial/devices/ttyUSB0/latency_timer

# Or set via udev rule
SUBSYSTEM=="usb-serial", DRIVER=="ftdi_sio", \
  ATTR{latency_timer}="1"
```

---

## 10. Flow Control Bridging <a name="flow-control-bridging"></a>

### Hardware Flow Control (RTS/CTS)

In hardware flow control mode, the bridge monitors the CTS input pin. If CTS is deasserted (high), the bridge pauses UART TX. Similarly, it asserts/deasserts RTS to tell the target to pause. This is entirely handled in the bridge chip hardware and is transparent to the USB side.

### Software Flow Control (XON/XOFF)

XON (0x11) and XOFF (0x13) characters in the data stream request the sender to pause/resume. The bridge can be configured to intercept these in hardware or pass them through for software handling. CDC-ACM does not standardize XON/XOFF interception; FTDI provides configurable XON/XOFF handling via the `SET_FLOW_CTRL` request.

### USB Back-Pressure

When the host cannot consume data fast enough (e.g., application buffer full), the USB driver stops issuing bulk IN transfers. The bridge chip's RX FIFO fills up, and eventually RTS is deasserted to apply back-pressure to the UART target. This closes the flow control loop across the USB-UART boundary.

---

## 11. Line Coding and Baud Rate Negotiation <a name="line-coding-and-baud-rate"></a>

For CDC-ACM devices, the host sends `SET_LINE_CODING` with the desired parameters. The firmware must configure its UART peripheral to match. The bridge should respond gracefully if an unsupported baud rate is requested — either by rounding to the nearest supported rate or by NACKing the request (though in practice most firmware silently clamps).

For FTDI devices, the driver calculates the baud divisor on the host side and sends it as part of `Set_Baud_Rate`. The chip simply programs its internal divider.

---

## 12. Programming in C/C++ <a name="programming-in-c-cpp"></a>

### 12.1 Linux: Using termios and the USB-Serial Device <a name="linux-termios"></a>

The standard POSIX interface for serial ports on Linux. The kernel driver (e.g., `ftdi_sio`, `cp210x`, `cdc_acm`) exposes the device as `/dev/ttyUSBx` or `/dev/ttyACMx`. Application code sees a standard TTY.

```c
// usb_serial_posix.c
// Demonstrates opening, configuring, and using a USB-serial port on Linux
// Compile: gcc -o usb_serial_posix usb_serial_posix.c

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <linux/serial.h>  // For struct serial_struct (custom baud rates)

// ------------------------------------------------------------------
// Open and configure a serial port
// device: e.g. "/dev/ttyUSB0" or "/dev/ttyACM0"
// baud:   e.g. B115200 (use termios Bxxx constants)
// Returns fd on success, -1 on error
// ------------------------------------------------------------------
int serial_open(const char *device, speed_t baud) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // Require the device to be a tty
    if (!isatty(fd)) {
        fprintf(stderr, "%s is not a TTY\n", device);
        close(fd);
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    // Set baud rate (input and output)
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    // 8N1: 8 data bits, No parity, 1 stop bit
    tty.c_cflag &= ~PARENB;          // No parity
    tty.c_cflag &= ~CSTOPB;          // 1 stop bit (not 2)
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;              // 8 data bits

    // Hardware flow control: disable
    tty.c_cflag &= ~CRTSCTS;

    // Enable receiver, ignore modem status lines
    tty.c_cflag |= CREAD | CLOCAL;

    // Raw mode: no line processing
    cfmakeraw(&tty);

    // Non-blocking read: return immediately with whatever is available
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);

    // Switch to blocking mode for cleaner read() semantics
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    return fd;
}

// ------------------------------------------------------------------
// Set a custom (non-standard) baud rate using Linux serial_struct
// Useful for rates like 250000, 500000, 1000000
// ------------------------------------------------------------------
int serial_set_custom_baud(int fd, int baud) {
    struct serial_struct ss;
    if (ioctl(fd, TIOCGSERIAL, &ss) < 0) {
        perror("TIOCGSERIAL");
        return -1;
    }

    ss.flags = (ss.flags & ~ASYNC_SPD_MASK) | ASYNC_SPD_CUST;
    ss.custom_divisor = (ss.baud_base + (baud / 2)) / baud;

    if (ioctl(fd, TIOCSSERIAL, &ss) < 0) {
        perror("TIOCSSERIAL");
        return -1;
    }

    // Must still set termios speed to B38400 as the "trigger" for custom baud
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetispeed(&tty, B38400);
    cfsetospeed(&tty, B38400);
    tcsetattr(fd, TCSANOW, &tty);

    return 0;
}

// ------------------------------------------------------------------
// Manipulate DTR and RTS modem control lines
// ------------------------------------------------------------------
int serial_set_dtr(int fd, int state) {
    int status;
    if (ioctl(fd, TIOCMGET, &status) < 0) return -1;
    if (state)
        status |= TIOCM_DTR;
    else
        status &= ~TIOCM_DTR;
    return ioctl(fd, TIOCMSET, &status);
}

int serial_set_rts(int fd, int state) {
    int status;
    if (ioctl(fd, TIOCMGET, &status) < 0) return -1;
    if (state)
        status |= TIOCM_RTS;
    else
        status &= ~TIOCM_RTS;
    return ioctl(fd, TIOCMSET, &status);
}

// ------------------------------------------------------------------
// Read with timeout using select()
// Returns bytes read, 0 on timeout, -1 on error
// ------------------------------------------------------------------
int serial_read_timeout(int fd, uint8_t *buf, size_t len, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) { perror("select"); return -1; }
    if (ret == 0) return 0;  // Timeout

    return read(fd, buf, len);
}

// ------------------------------------------------------------------
// Write with retry on EAGAIN
// ------------------------------------------------------------------
int serial_write_all(int fd, const uint8_t *buf, size_t len) {
    size_t written = 0;
    while (written < len) {
        ssize_t n = write(fd, buf + written, len - written);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(1000);
                continue;
            }
            perror("write");
            return -1;
        }
        written += n;
    }
    return written;
}

// ------------------------------------------------------------------
// Example: send AT command and wait for response
// ------------------------------------------------------------------
int main(void) {
    int fd = serial_open("/dev/ttyUSB0", B115200);
    if (fd < 0) return EXIT_FAILURE;

    // Assert DTR to signal device readiness (some devices reset on DTR toggle)
    serial_set_dtr(fd, 1);
    usleep(100000);  // 100ms settle time

    // Send a command
    const uint8_t cmd[] = "AT\r\n";
    printf("TX: %s", cmd);
    serial_write_all(fd, cmd, sizeof(cmd) - 1);

    // Read response with 500ms timeout
    uint8_t response[256];
    int n = serial_read_timeout(fd, response, sizeof(response) - 1, 500);
    if (n > 0) {
        response[n] = '\0';
        printf("RX: %s\n", response);
    } else if (n == 0) {
        printf("Timeout: no response\n");
    }

    // Clean up
    tcdrain(fd);   // Wait for TX to complete
    serial_set_dtr(fd, 0);
    close(fd);
    return EXIT_SUCCESS;
}
```

### 12.2 Windows: Using Win32 COM API <a name="windows-com-api"></a>

On Windows, USB-serial devices appear as `COMx` ports. The Win32 `CreateFile`/`ReadFile`/`WriteFile` API with `DCB` (Device Control Block) is used for configuration.

```c
// usb_serial_win32.c
// Windows COM port access for USB-serial devices
// Compile: cl usb_serial_win32.c

#include <windows.h>
#include <stdio.h>
#include <stdint.h>

// ------------------------------------------------------------------
// Open and configure a COM port
// portName: e.g. "COM3" or "\\\\.\\COM10" for ports > COM9
// baud: e.g. CBR_115200
// ------------------------------------------------------------------
HANDLE com_open(const char *portName, DWORD baud) {
    // For COM ports > 9, must use the \\.\COMx form
    char fullName[32];
    snprintf(fullName, sizeof(fullName), "\\\\.\\%s", portName);

    HANDLE hPort = CreateFileA(
        fullName,
        GENERIC_READ | GENERIC_WRITE,
        0,              // No sharing
        NULL,           // No security attributes
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,  // Use overlapped (async) I/O
        NULL
    );

    if (hPort == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "CreateFile failed: %lu\n", GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Configure DCB (Device Control Block)
    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(hPort, &dcb)) {
        fprintf(stderr, "GetCommState failed: %lu\n", GetLastError());
        CloseHandle(hPort);
        return INVALID_HANDLE_VALUE;
    }

    dcb.BaudRate    = baud;
    dcb.ByteSize    = 8;
    dcb.Parity      = NOPARITY;
    dcb.StopBits    = ONESTOPBIT;
    dcb.fBinary     = TRUE;
    dcb.fParity     = FALSE;
    dcb.fOutxCtsFlow = FALSE;  // No hardware flow control
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX        = FALSE;  // No XON/XOFF
    dcb.fInX         = FALSE;
    dcb.fNull        = FALSE;  // Don't discard null bytes

    if (!SetCommState(hPort, &dcb)) {
        fprintf(stderr, "SetCommState failed: %lu\n", GetLastError());
        CloseHandle(hPort);
        return INVALID_HANDLE_VALUE;
    }

    // Set timeouts: 500ms total read timeout
    COMMTIMEOUTS timeouts;
    timeouts.ReadIntervalTimeout         = 50;    // Max ms between bytes
    timeouts.ReadTotalTimeoutMultiplier  = 10;    // ms per byte
    timeouts.ReadTotalTimeoutConstant    = 500;   // ms constant
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant   = 1000;

    if (!SetCommTimeouts(hPort, &timeouts)) {
        fprintf(stderr, "SetCommTimeouts failed: %lu\n", GetLastError());
        CloseHandle(hPort);
        return INVALID_HANDLE_VALUE;
    }

    // Purge any stale data
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return hPort;
}

// ------------------------------------------------------------------
// Write bytes using overlapped I/O
// ------------------------------------------------------------------
BOOL com_write(HANDLE hPort, const uint8_t *data, DWORD len) {
    OVERLAPPED ov;
    SecureZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD written = 0;
    BOOL ok = WriteFile(hPort, data, len, &written, &ov);

    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        WaitForSingleObject(ov.hEvent, INFINITE);
        GetOverlappedResult(hPort, &ov, &written, FALSE);
    }

    CloseHandle(ov.hEvent);
    return (written == len);
}

// ------------------------------------------------------------------
// Read bytes using overlapped I/O with timeout
// Returns bytes read
// ------------------------------------------------------------------
DWORD com_read(HANDLE hPort, uint8_t *buf, DWORD maxLen, DWORD timeoutMs) {
    OVERLAPPED ov;
    SecureZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD bytesRead = 0;
    BOOL ok = ReadFile(hPort, buf, maxLen, &bytesRead, &ov);

    if (!ok && GetLastError() == ERROR_IO_PENDING) {
        DWORD result = WaitForSingleObject(ov.hEvent, timeoutMs);
        if (result == WAIT_OBJECT_0) {
            GetOverlappedResult(hPort, &ov, &bytesRead, FALSE);
        } else {
            // Timeout: cancel the pending read
            CancelIo(hPort);
            bytesRead = 0;
        }
    }

    CloseHandle(ov.hEvent);
    return bytesRead;
}

// ------------------------------------------------------------------
// Control DTR and RTS lines
// ------------------------------------------------------------------
void com_set_dtr(HANDLE hPort, BOOL state) {
    EscapeCommFunction(hPort, state ? SETDTR : CLRDTR);
}

void com_set_rts(HANDLE hPort, BOOL state) {
    EscapeCommFunction(hPort, state ? SETRTS : CLRRTS);
}

// ------------------------------------------------------------------
// Read modem status lines
// ------------------------------------------------------------------
void com_get_modem_status(HANDLE hPort) {
    DWORD status;
    if (GetCommModemStatus(hPort, &status)) {
        printf("CTS: %s  DSR: %s  RING: %s  DCD: %s\n",
            (status & MS_CTS_ON)  ? "ON" : "OFF",
            (status & MS_DSR_ON)  ? "ON" : "OFF",
            (status & MS_RING_ON) ? "ON" : "OFF",
            (status & MS_RLSD_ON) ? "ON" : "OFF");
    }
}

int main(void) {
    HANDLE hPort = com_open("COM3", CBR_115200);
    if (hPort == INVALID_HANDLE_VALUE) return 1;

    com_set_dtr(hPort, TRUE);
    Sleep(100);

    uint8_t cmd[] = "AT\r\n";
    printf("TX: %s", cmd);
    com_write(hPort, cmd, sizeof(cmd) - 1);

    uint8_t response[256];
    DWORD n = com_read(hPort, response, sizeof(response) - 1, 500);
    if (n > 0) {
        response[n] = '\0';
        printf("RX: %s\n", response);
    } else {
        printf("Timeout\n");
    }

    com_set_dtr(hPort, FALSE);
    CloseHandle(hPort);
    return 0;
}
```

### 12.3 Direct libusb Access (Bypassing the Driver) <a name="libusb-direct"></a>

Sometimes it is necessary to communicate with a USB-serial bridge chip directly, bypassing the OS kernel driver — for example, to use FTDI's extended features, implement a custom protocol, or work in an environment without kernel driver support.

```c
// direct_ftdi_libusb.c
// Direct FTDI FT232 communication via libusb, bypassing ftdi_sio driver
// Compile: gcc -o direct_ftdi_libusb direct_ftdi_libusb.c -lusb-1.0
// Requires: libusb-1.0-dev, and ftdi_sio kernel module unloaded (or detached)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <libusb-1.0/libusb.h>

// FTDI vendor/product IDs
#define FTDI_VID          0x0403
#define FTDI_FT232_PID    0x6001

// FTDI request types
#define FTDI_REQTYPE_OUT  (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_OUT)
#define FTDI_REQTYPE_IN   (LIBUSB_REQUEST_TYPE_VENDOR | LIBUSB_RECIPIENT_DEVICE | LIBUSB_ENDPOINT_IN)

// FTDI bRequest values
#define FTDI_REQ_RESET       0x00
#define FTDI_REQ_MODEM_CTRL  0x01
#define FTDI_REQ_SET_FLOW    0x02
#define FTDI_REQ_SET_BAUD    0x03
#define FTDI_REQ_SET_DATA    0x04
#define FTDI_REQ_GET_MODEM   0x05
#define FTDI_REQ_SET_LATENCY 0x09
#define FTDI_REQ_GET_LATENCY 0x0A

// FTDI endpoints (interface 0)
#define FTDI_EP_BULK_IN   0x81
#define FTDI_EP_BULK_OUT  0x02

typedef struct {
    libusb_context       *ctx;
    libusb_device_handle *dev;
    int                   interface;
} ftdi_ctx_t;

// ------------------------------------------------------------------
// Calculate FTDI baud rate divisor
// FTDI base clock is 3000000 Hz for FT232BM/FT232R
// Returns encoded divisor value
// ------------------------------------------------------------------
static uint16_t ftdi_baud_to_divisor(uint32_t baud) {
    static const uint8_t frac_code[8] = {0, 3, 2, 4, 1, 5, 6, 7};
    uint32_t divisor = (3000000 * 8 + baud / 2) / baud;
    uint16_t encoded = (divisor >> 3) | (frac_code[divisor & 0x7] << 14);
    return encoded;
}

// ------------------------------------------------------------------
// Open FTDI device and configure for serial use
// ------------------------------------------------------------------
int ftdi_open(ftdi_ctx_t *ftdi, uint32_t baud) {
    int ret;
    libusb_init(&ftdi->ctx);
    libusb_set_option(ftdi->ctx, LIBUSB_OPTION_LOG_LEVEL, LIBUSB_LOG_LEVEL_WARNING);

    ftdi->dev = libusb_open_device_with_vid_pid(ftdi->ctx, FTDI_VID, FTDI_FT232_PID);
    if (!ftdi->dev) {
        fprintf(stderr, "Device not found\n");
        return -1;
    }

    // Detach kernel driver if active
    ftdi->interface = 0;
    if (libusb_kernel_driver_active(ftdi->dev, ftdi->interface) == 1) {
        ret = libusb_detach_kernel_driver(ftdi->dev, ftdi->interface);
        if (ret < 0) {
            fprintf(stderr, "Cannot detach kernel driver: %s\n", libusb_error_name(ret));
            return -1;
        }
    }

    ret = libusb_claim_interface(ftdi->dev, ftdi->interface);
    if (ret < 0) {
        fprintf(stderr, "Cannot claim interface: %s\n", libusb_error_name(ret));
        return -1;
    }

    // Reset the device
    libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_RESET, 0, ftdi->interface + 1, NULL, 0, 1000);

    // Set baud rate (wValue = divisor low, wIndex encodes high bits and interface)
    uint16_t divisor = ftdi_baud_to_divisor(baud);
    ret = libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_SET_BAUD,
        divisor & 0xFFFF,
        ((divisor >> 8) & 0xFF00) | (ftdi->interface + 1),
        NULL, 0, 1000);
    if (ret < 0) {
        fprintf(stderr, "Set baud failed: %s\n", libusb_error_name(ret));
        return -1;
    }

    // Set 8N1: wValue = 0x0800 (8 data bits, no parity, 1 stop bit)
    libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_SET_DATA, 0x0800, ftdi->interface + 1, NULL, 0, 1000);

    // Disable flow control
    libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_SET_FLOW, 0, ftdi->interface + 1, NULL, 0, 1000);

    // Set DTR and RTS high
    libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_MODEM_CTRL, 0x0303, ftdi->interface + 1, NULL, 0, 1000);

    // Reduce latency timer to 1ms for low-latency operation
    libusb_control_transfer(ftdi->dev, FTDI_REQTYPE_OUT,
        FTDI_REQ_SET_LATENCY, 1, ftdi->interface + 1, NULL, 0, 1000);

    printf("FTDI opened at %u baud\n", baud);
    return 0;
}

// ------------------------------------------------------------------
// Write data to FTDI
// ------------------------------------------------------------------
int ftdi_write(ftdi_ctx_t *ftdi, const uint8_t *data, int len) {
    int transferred = 0;
    int ret = libusb_bulk_transfer(ftdi->dev, FTDI_EP_BULK_OUT,
                                   (uint8_t *)data, len, &transferred, 1000);
    if (ret < 0) {
        fprintf(stderr, "Write error: %s\n", libusb_error_name(ret));
        return -1;
    }
    return transferred;
}

// ------------------------------------------------------------------
// Read data from FTDI (strips the 2-byte status header per packet)
// ------------------------------------------------------------------
int ftdi_read(ftdi_ctx_t *ftdi, uint8_t *buf, int max_len, int timeout_ms) {
    // FTDI prepends 2 status bytes to each bulk IN packet
    // Allocate extra space for them
    uint8_t raw[max_len + 2];
    int transferred = 0;

    int ret = libusb_bulk_transfer(ftdi->dev, FTDI_EP_BULK_IN,
                                   raw, sizeof(raw), &transferred, timeout_ms);

    if (ret == LIBUSB_ERROR_TIMEOUT && transferred == 0) return 0;
    if (ret < 0) {
        fprintf(stderr, "Read error: %s\n", libusb_error_name(ret));
        return -1;
    }

    // Strip the 2-byte FTDI status header from each 64-byte USB packet
    int out_len = 0;
    int offset = 0;
    while (offset < transferred) {
        int packet_size = transferred - offset;
        if (packet_size > 64) packet_size = 64;

        // Bytes 0-1 are status; bytes 2+ are data
        if (packet_size > 2) {
            int data_len = packet_size - 2;
            if (out_len + data_len > max_len) data_len = max_len - out_len;
            memcpy(buf + out_len, raw + offset + 2, data_len);
            out_len += data_len;
        }
        offset += packet_size;
    }

    return out_len;
}

void ftdi_close(ftdi_ctx_t *ftdi) {
    libusb_release_interface(ftdi->dev, ftdi->interface);
    libusb_attach_kernel_driver(ftdi->dev, ftdi->interface);
    libusb_close(ftdi->dev);
    libusb_exit(ftdi->ctx);
}

int main(void) {
    ftdi_ctx_t ftdi;
    if (ftdi_open(&ftdi, 115200) < 0) return EXIT_FAILURE;

    uint8_t cmd[] = "Hello, FTDI!\r\n";
    ftdi_write(&ftdi, cmd, sizeof(cmd) - 1);

    uint8_t buf[256];
    int n = ftdi_read(&ftdi, buf, sizeof(buf) - 1, 500);
    if (n > 0) {
        buf[n] = '\0';
        printf("Received: %s\n", buf);
    }

    ftdi_close(&ftdi);
    return EXIT_SUCCESS;
}
```

### 12.4 Implementing a Software CDC-ACM Bridge in C <a name="software-cdc-bridge-c"></a>

The following example shows a simplified CDC-ACM device implementation on a bare-metal microcontroller (pseudocode with hardware abstraction — intended to show the bridge logic rather than a specific MCU):

```c
// cdc_acm_bridge.c
// Conceptual CDC-ACM USB-to-UART bridge firmware
// Demonstrates the core state machine and data flow

#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "usb_device.h"   // HAL-specific USB device driver
#include "uart_hal.h"     // HAL-specific UART driver

// CDC Line Coding structure (matches USB CDC spec)
typedef struct __attribute__((packed)) {
    uint32_t dwDTERate;   // Baud rate
    uint8_t  bCharFormat; // Stop bits
    uint8_t  bParityType; // Parity
    uint8_t  bDataBits;   // Data bits
} cdc_line_coding_t;

// Bridge state
static cdc_line_coding_t line_coding = {
    .dwDTERate   = 115200,
    .bCharFormat = 0,  // 1 stop bit
    .bParityType = 0,  // No parity
    .bDataBits   = 8
};

static bool dtr_state = false;
static bool rts_state = false;

// Circular buffers for bidirectional data
#define BUF_SIZE 512
static uint8_t usb_to_uart_buf[BUF_SIZE];
static uint8_t uart_to_usb_buf[BUF_SIZE];
static volatile uint16_t u2u_head = 0, u2u_tail = 0;  // USB→UART
static volatile uint16_t uu_head  = 0, uu_tail  = 0;  // UART→USB

// Flush timer: we must send partial USB packets after a timeout
static volatile uint32_t flush_timer_ms = 0;
#define FLUSH_TIMEOUT_MS 1

// ------------------------------------------------------------------
// USB CDC control request handler (called from USB interrupt/task)
// ------------------------------------------------------------------
void cdc_handle_control_request(usb_setup_packet_t *setup, uint8_t *data) {
    switch (setup->bRequest) {

    case CDC_SET_LINE_CODING:   // 0x20
        // Host is setting baud rate, parity, stop bits
        memcpy(&line_coding, data, sizeof(cdc_line_coding_t));

        // Reconfigure UART peripheral
        uart_configure(
            line_coding.dwDTERate,
            line_coding.bDataBits,
            line_coding.bParityType,
            line_coding.bCharFormat
        );
        printf("[CDC] Line coding: %lu baud, %u bits, parity=%u, stop=%u\n",
               (unsigned long)line_coding.dwDTERate,
               line_coding.bDataBits,
               line_coding.bParityType,
               line_coding.bCharFormat);
        break;

    case CDC_GET_LINE_CODING:   // 0x21
        // Host is querying current line coding
        usb_send_control_response((uint8_t *)&line_coding, sizeof(line_coding));
        break;

    case CDC_SET_CONTROL_LINE_STATE:  // 0x22
        // wValue bit 0 = DTR, bit 1 = RTS
        dtr_state = (setup->wValue & 0x01) != 0;
        rts_state = (setup->wValue & 0x02) != 0;

        // Drive physical modem control lines
        uart_set_dtr(dtr_state);
        uart_set_rts(rts_state);

        printf("[CDC] Control line state: DTR=%d RTS=%d\n",
               (int)dtr_state, (int)rts_state);

        // Many embedded targets (Arduino, ESP32) use DTR to trigger reset
        // If DTR transitions low→high, assert reset for 100ms
        if (dtr_state) {
            // target_assert_reset();  // Platform-specific
        }
        break;

    case CDC_SEND_BREAK:        // 0x23
        // wValue = break duration in ms (0xFFFF = indefinite)
        uart_send_break(setup->wValue);
        break;

    default:
        usb_stall_control_endpoint();
        break;
    }
}

// ------------------------------------------------------------------
// Called when USB host sends data (Bulk OUT)
// ------------------------------------------------------------------
void cdc_on_bulk_out(const uint8_t *data, uint16_t len) {
    // Buffer data for UART transmission
    for (uint16_t i = 0; i < len; i++) {
        uint16_t next = (u2u_head + 1) % BUF_SIZE;
        if (next != u2u_tail) {
            usb_to_uart_buf[u2u_head] = data[i];
            u2u_head = next;
        }
        // If buffer full: NACK next Bulk OUT (USB back-pressure)
        // This is handled automatically by not re-arming the endpoint
    }
}

// ------------------------------------------------------------------
// UART RX interrupt handler
// ------------------------------------------------------------------
void uart_rx_irq_handler(uint8_t byte) {
    // Buffer received byte for USB transmission
    uint16_t next = (uu_head + 1) % BUF_SIZE;
    if (next != uu_tail) {
        uart_to_usb_buf[uu_head] = byte;
        uu_head = next;
    }
    // Reset flush timer on every received byte
    flush_timer_ms = 0;
}

// ------------------------------------------------------------------
// Main bridge task (called from main loop or RTOS task)
// ------------------------------------------------------------------
void cdc_bridge_task(void) {
    // === USB → UART path ===
    // Drain the USB receive buffer into the UART transmit FIFO
    while (u2u_tail != u2u_head) {
        uint8_t byte = usb_to_uart_buf[u2u_tail];
        u2u_tail = (u2u_tail + 1) % BUF_SIZE;
        uart_tx_byte(byte);
    }

    // === UART → USB path ===
    // Check if we have data to send and flush conditions are met
    uint16_t avail;
    if (uu_tail <= uu_head)
        avail = uu_head - uu_tail;
    else
        avail = BUF_SIZE - uu_tail + uu_head;

    bool should_flush = false;

    // Condition 1: Buffer has reached the USB packet size (64 bytes)
    if (avail >= 64) {
        should_flush = true;
    }

    // Condition 2: Flush timeout — send partial packet after quiet period
    // This ensures a single-byte response doesn't sit in the buffer
    if (avail > 0 && flush_timer_ms >= FLUSH_TIMEOUT_MS) {
        should_flush = true;
    }

    if (should_flush && usb_bulk_in_ready()) {
        uint8_t pkt[64];
        uint16_t pkt_len = 0;

        while (uu_tail != uu_head && pkt_len < 64) {
            pkt[pkt_len++] = uart_to_usb_buf[uu_tail];
            uu_tail = (uu_tail + 1) % BUF_SIZE;
        }

        usb_bulk_in_send(pkt, pkt_len);
        flush_timer_ms = 0;
    }

    // === Modem line state monitoring ===
    // Check if UART input modem lines changed and notify host
    static uint8_t last_modem_state = 0;
    uint8_t modem_state = uart_get_modem_lines();  // CTS, DSR, DCD, RI

    if (modem_state != last_modem_state) {
        last_modem_state = modem_state;
        // Send CDC Serial State notification via Interrupt IN endpoint
        cdc_send_serial_state_notification(modem_state);
    }
}

// ------------------------------------------------------------------
// 1ms SysTick or timer interrupt increments flush timer
// ------------------------------------------------------------------
void systick_handler(void) {
    flush_timer_ms++;
}
```

---

## 13. Programming in Rust <a name="programming-in-rust"></a>

### 13.1 Using the `serialport` Crate <a name="rust-serialport"></a>

The `serialport` crate provides a cross-platform, idiomatic Rust API for serial ports. It abstracts the underlying OS differences (termios on Linux/macOS, Win32 on Windows).

```toml
# Cargo.toml
[dependencies]
serialport = "4.3"
```

```rust
// src/main.rs — Basic USB-serial communication using the serialport crate
use serialport::{SerialPort, SerialPortType, UsbPortInfo};
use std::io::{self, Read, Write};
use std::time::Duration;
use std::error::Error;

// ------------------------------------------------------------------
// List all available serial ports, showing USB device info
// ------------------------------------------------------------------
fn list_usb_serial_ports() {
    let ports = serialport::available_ports().expect("Failed to enumerate ports");
    println!("Available serial ports:");
    for p in &ports {
        match &p.port_type {
            SerialPortType::UsbPort(info) => {
                println!(
                    "  {} [USB] VID={:04X} PID={:04X} SN={} Manufacturer={} Product={}",
                    p.port_name,
                    info.vid,
                    info.pid,
                    info.serial_number.as_deref().unwrap_or("?"),
                    info.manufacturer.as_deref().unwrap_or("?"),
                    info.product.as_deref().unwrap_or("?"),
                );
            }
            SerialPortType::PciPort => println!("  {} [PCI]", p.port_name),
            SerialPortType::BluetoothPort => println!("  {} [BT]", p.port_name),
            SerialPortType::Unknown => println!("  {} [Unknown]", p.port_name),
        }
    }
}

// ------------------------------------------------------------------
// Find the first USB-serial port matching a VID/PID
// ------------------------------------------------------------------
fn find_usb_serial_by_vid_pid(vid: u16, pid: u16) -> Option<String> {
    serialport::available_ports().ok()?.into_iter().find_map(|p| {
        if let SerialPortType::UsbPort(UsbPortInfo { vid: v, pid: pi, .. }) = p.port_type {
            if v == vid && pi == pid {
                return Some(p.port_name);
            }
        }
        None
    })
}

// ------------------------------------------------------------------
// Open a serial port with full configuration
// ------------------------------------------------------------------
fn open_serial(
    port_name: &str,
    baud_rate: u32,
    timeout: Duration,
) -> Result<Box<dyn SerialPort>, Box<dyn Error>> {
    let port = serialport::new(port_name, baud_rate)
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .timeout(timeout)
        .open()?;

    Ok(port)
}

// ------------------------------------------------------------------
// Write bytes and wait for a response ending with a delimiter
// ------------------------------------------------------------------
fn send_and_receive(
    port: &mut Box<dyn SerialPort>,
    command: &[u8],
    terminator: u8,
    max_bytes: usize,
) -> Result<Vec<u8>, Box<dyn Error>> {
    port.write_all(command)?;
    port.flush()?;

    let mut response = Vec::with_capacity(max_bytes);
    let mut byte = [0u8; 1];

    loop {
        match port.read(&mut byte) {
            Ok(1) => {
                response.push(byte[0]);
                if byte[0] == terminator || response.len() >= max_bytes {
                    break;
                }
            }
            Ok(0) => break, // EOF
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => break,
            Err(e) => return Err(e.into()),
        }
    }

    Ok(response)
}

// ------------------------------------------------------------------
// Control DTR/RTS modem lines
// ------------------------------------------------------------------
fn reset_via_dtr(port: &mut Box<dyn SerialPort>) -> Result<(), Box<dyn Error>> {
    // Assert DTR low (resets many Arduino/ESP boards)
    port.write_data_terminal_ready(false)?;
    std::thread::sleep(Duration::from_millis(100));
    // De-assert DTR
    port.write_data_terminal_ready(true)?;
    std::thread::sleep(Duration::from_millis(100));
    Ok(())
}

fn main() -> Result<(), Box<dyn Error>> {
    list_usb_serial_ports();

    // Try to find a CP2102-based device (Silicon Labs VID)
    let port_name = find_usb_serial_by_vid_pid(0x10C4, 0xEA60)
        .or_else(|| {
            // Fall back to first available USB port
            serialport::available_ports().ok()?.into_iter().find_map(|p| {
                if matches!(p.port_type, SerialPortType::UsbPort(_)) {
                    Some(p.port_name)
                } else {
                    None
                }
            })
        })
        .unwrap_or_else(|| "/dev/ttyUSB0".to_string());

    println!("\nOpening: {}", port_name);

    let mut port = open_serial(&port_name, 115200, Duration::from_millis(500))?;

    // Check initial modem state
    println!("CTS: {}", port.read_clear_to_send()?);
    println!("DSR: {}", port.read_data_set_ready()?);

    // Toggle DTR to reset target device
    reset_via_dtr(&mut port)?;
    println!("Device reset via DTR");

    // Send an AT command and wait for a newline-terminated response
    let cmd = b"AT\r\n";
    println!("TX: {}", String::from_utf8_lossy(cmd).trim());

    let response = send_and_receive(&mut port, cmd, b'\n', 256)?;
    println!("RX: {}", String::from_utf8_lossy(&response).trim());

    Ok(())
}
```

### 13.2 Using `rusb` for Direct USB Communication <a name="rust-rusb"></a>

For direct USB access (bypassing the kernel driver), the `rusb` crate provides safe Rust bindings to libusb.

```toml
# Cargo.toml
[dependencies]
rusb = "0.9"
```

```rust
// src/direct_usb.rs — Direct USB communication with a CDC-ACM device
// This allows CDC control requests without a kernel driver
use rusb::{Context, Device, DeviceHandle, UsbContext};
use std::time::Duration;
use std::error::Error;

// CDC-ACM constants
const CDC_REQTYPE_OUT: u8 = 0x21; // bmRequestType: class, interface, host-to-device
const CDC_REQTYPE_IN:  u8 = 0xA1; // bmRequestType: class, interface, device-to-host
const CDC_SET_LINE_CODING: u8 = 0x20;
const CDC_GET_LINE_CODING: u8 = 0x21;
const CDC_SET_CONTROL_LINE_STATE: u8 = 0x22;
const CDC_SEND_BREAK: u8 = 0x23;

// Typical CDC-ACM endpoints (check actual descriptor for your device)
const EP_BULK_IN:  u8 = 0x81;
const EP_BULK_OUT: u8 = 0x02;

struct CdcAcmDevice {
    handle: DeviceHandle<Context>,
    data_interface: u8,
    ctrl_interface: u8,
}

impl CdcAcmDevice {
    /// Open a CDC-ACM device by VID:PID and detach kernel driver if needed
    fn open(ctx: &Context, vid: u16, pid: u16) -> Result<Self, Box<dyn Error>> {
        let handle = ctx.open_device_with_vid_pid(vid, pid)
            .ok_or("Device not found")?;

        // Typically CDC uses interface 0 (control) and 1 (data)
        let ctrl_interface = 0u8;
        let data_interface = 1u8;

        // Detach kernel driver from both interfaces
        for iface in [ctrl_interface, data_interface] {
            if handle.kernel_driver_active(iface)? {
                handle.detach_kernel_driver(iface)?;
            }
            handle.claim_interface(iface)?;
        }

        Ok(CdcAcmDevice { handle, data_interface, ctrl_interface })
    }

    /// Send SET_LINE_CODING request
    fn set_line_coding(
        &self,
        baud: u32,
        data_bits: u8,
        parity: u8,
        stop_bits: u8,
    ) -> Result<(), Box<dyn Error>> {
        let mut coding = [0u8; 7];
        // dwDTERate: little-endian u32
        coding[0] = (baud & 0xFF) as u8;
        coding[1] = ((baud >> 8) & 0xFF) as u8;
        coding[2] = ((baud >> 16) & 0xFF) as u8;
        coding[3] = ((baud >> 24) & 0xFF) as u8;
        coding[4] = stop_bits;  // 0=1, 1=1.5, 2=2
        coding[5] = parity;     // 0=None, 1=Odd, 2=Even
        coding[6] = data_bits;  // 8

        self.handle.write_control(
            CDC_REQTYPE_OUT,
            CDC_SET_LINE_CODING,
            0,
            self.ctrl_interface as u16,
            &coding,
            Duration::from_millis(1000),
        )?;

        println!("Set line coding: {} baud, {}N{}", baud, data_bits, stop_bits + 1);
        Ok(())
    }

    /// Send GET_LINE_CODING request and parse response
    fn get_line_coding(&self) -> Result<(u32, u8, u8, u8), Box<dyn Error>> {
        let mut coding = [0u8; 7];
        self.handle.read_control(
            CDC_REQTYPE_IN,
            CDC_GET_LINE_CODING,
            0,
            self.ctrl_interface as u16,
            &mut coding,
            Duration::from_millis(1000),
        )?;

        let baud = u32::from_le_bytes([coding[0], coding[1], coding[2], coding[3]]);
        let stop_bits = coding[4];
        let parity    = coding[5];
        let data_bits = coding[6];

        Ok((baud, data_bits, parity, stop_bits))
    }

    /// Set control line state (DTR/RTS)
    fn set_control_line_state(&self, dtr: bool, rts: bool) -> Result<(), Box<dyn Error>> {
        let value = (dtr as u16) | ((rts as u16) << 1);
        self.handle.write_control(
            CDC_REQTYPE_OUT,
            CDC_SET_CONTROL_LINE_STATE,
            value,
            self.ctrl_interface as u16,
            &[],
            Duration::from_millis(1000),
        )?;
        Ok(())
    }

    /// Send a BREAK signal
    fn send_break(&self, duration_ms: u16) -> Result<(), Box<dyn Error>> {
        self.handle.write_control(
            CDC_REQTYPE_OUT,
            CDC_SEND_BREAK,
            duration_ms,
            self.ctrl_interface as u16,
            &[],
            Duration::from_millis(1000),
        )?;
        Ok(())
    }

    /// Write data bytes to the device
    fn write(&self, data: &[u8]) -> Result<usize, Box<dyn Error>> {
        let transferred = self.handle.write_bulk(
            EP_BULK_OUT,
            data,
            Duration::from_millis(1000),
        )?;
        Ok(transferred)
    }

    /// Read data bytes from the device
    fn read(&self, buf: &mut [u8]) -> Result<usize, Box<dyn Error>> {
        let transferred = self.handle.read_bulk(
            EP_BULK_IN,
            buf,
            Duration::from_millis(500),
        )?;
        Ok(transferred)
    }
}

impl Drop for CdcAcmDevice {
    fn drop(&mut self) {
        let _ = self.handle.release_interface(self.data_interface);
        let _ = self.handle.release_interface(self.ctrl_interface);
        // Optionally re-attach kernel driver
        let _ = self.handle.attach_kernel_driver(self.ctrl_interface);
        let _ = self.handle.attach_kernel_driver(self.data_interface);
    }
}

fn main() -> Result<(), Box<dyn Error>> {
    let ctx = Context::new()?;

    // Example: open a CH340 device (WCH VID=0x1A86, PID=0x7523)
    // Adjust VID:PID for your device
    let dev = CdcAcmDevice::open(&ctx, 0x1A86, 0x7523)?;

    // Query current line coding
    let (baud, data, parity, stop) = dev.get_line_coding()?;
    println!("Current: {} baud, {} data, parity={}, stop={}", baud, data, parity, stop);

    // Reconfigure to 9600 8N1
    dev.set_line_coding(9600, 8, 0, 0)?;

    // Assert DTR
    dev.set_control_line_state(true, false)?;
    std::thread::sleep(Duration::from_millis(100));

    // Send data
    let cmd = b"AT\r\n";
    let written = dev.write(cmd)?;
    println!("Wrote {} bytes", written);

    // Read response
    let mut buf = [0u8; 256];
    match dev.read(&mut buf) {
        Ok(n) if n > 0 => println!("Read: {}", String::from_utf8_lossy(&buf[..n])),
        Ok(_) => println!("No data"),
        Err(e) => println!("Read error: {}", e),
    }

    Ok(())
}
```

### 13.3 Async USB-Serial Bridge with Tokio <a name="rust-async-bridge"></a>

A production-grade bridge often needs to bridge bidirectionally and concurrently. This example uses Tokio for async I/O:

```toml
# Cargo.toml
[dependencies]
serialport = "4.3"
tokio = { version = "1", features = ["full"] }
tokio-serial = "5.4"
futures = "0.3"
```

```rust
// src/async_bridge.rs — Async bidirectional USB-serial bridge using Tokio
// Useful for: bridging two serial ports, logging, protocol injection

use tokio_serial::SerialPortBuilderExt;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::mpsc;
use std::time::Duration;
use std::error::Error;

const BAUD_RATE: u32 = 115200;
const BUF_SIZE: usize = 1024;

/// Bridge data between two serial ports asynchronously
/// port_a: e.g. /dev/ttyUSB0 (the USB-serial device)
/// port_b: e.g. /dev/ttyACM0 (another device or loopback)
async fn run_bridge(port_a_name: &str, port_b_name: &str) -> Result<(), Box<dyn Error>> {
    // Open both ports
    let port_a = tokio_serial::new(port_a_name, BAUD_RATE)
        .timeout(Duration::from_millis(100))
        .open_native_async()?;

    let port_b = tokio_serial::new(port_b_name, BAUD_RATE)
        .timeout(Duration::from_millis(100))
        .open_native_async()?;

    let (mut a_reader, mut a_writer) = tokio::io::split(port_a);
    let (mut b_reader, mut b_writer) = tokio::io::split(port_b);

    // Channels for bridged data (with optional inspection/logging)
    let (a_to_b_tx, mut a_to_b_rx) = mpsc::channel::<Vec<u8>>(32);
    let (b_to_a_tx, mut b_to_a_rx) = mpsc::channel::<Vec<u8>>(32);

    // Task: Read from A, send to channel (A → B direction)
    let a_to_b_reader = tokio::spawn(async move {
        let mut buf = vec![0u8; BUF_SIZE];
        loop {
            match a_reader.read(&mut buf).await {
                Ok(0) => break, // EOF
                Ok(n) => {
                    let chunk = buf[..n].to_vec();
                    println!("[A→B] {} bytes: {:?}",
                        n, String::from_utf8_lossy(&chunk));
                    if a_to_b_tx.send(chunk).await.is_err() { break; }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
                Err(e) => { eprintln!("Read A error: {}", e); break; }
            }
        }
    });

    // Task: Write channel data to B (A → B direction)
    let a_to_b_writer = tokio::spawn(async move {
        while let Some(data) = a_to_b_rx.recv().await {
            if let Err(e) = b_writer.write_all(&data).await {
                eprintln!("Write B error: {}", e);
                break;
            }
        }
    });

    // Task: Read from B, send to channel (B → A direction)
    let b_to_a_reader = tokio::spawn(async move {
        let mut buf = vec![0u8; BUF_SIZE];
        loop {
            match b_reader.read(&mut buf).await {
                Ok(0) => break,
                Ok(n) => {
                    let chunk = buf[..n].to_vec();
                    println!("[B→A] {} bytes: {:?}",
                        n, String::from_utf8_lossy(&chunk));
                    if b_to_a_tx.send(chunk).await.is_err() { break; }
                }
                Err(e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
                Err(e) => { eprintln!("Read B error: {}", e); break; }
            }
        }
    });

    // Task: Write channel data to A (B → A direction)
    let b_to_a_writer = tokio::spawn(async move {
        while let Some(data) = b_to_a_rx.recv().await {
            if let Err(e) = a_writer.write_all(&data).await {
                eprintln!("Write A error: {}", e);
                break;
            }
        }
    });

    // Wait for any task to complete (error or EOF)
    tokio::select! {
        _ = a_to_b_reader => println!("A reader done"),
        _ = a_to_b_writer => println!("B writer done"),
        _ = b_to_a_reader => println!("B reader done"),
        _ = b_to_a_writer => println!("A writer done"),
    }

    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    run_bridge("/dev/ttyUSB0", "/dev/ttyACM0").await
}
```

---

## 14. Embedded Firmware: Implementing USB-to-UART on a Microcontroller <a name="embedded-firmware"></a>

When using a microcontroller as a USB-to-UART bridge (e.g., an RP2040 or STM32), the firmware must implement both a USB device stack and a UART driver, and bridge between them.

The TinyUSB library is the most widely used embedded USB stack for this purpose:

```c
// tusb_config.h (excerpt for CDC-ACM bridge on RP2040)
#define CFG_TUD_CDC       1   // Enable CDC class
#define CFG_TUD_CDC_RX_BUFSIZE  512
#define CFG_TUD_CDC_TX_BUFSIZE  512

// main.c — RP2040 USB-to-UART bridge using TinyUSB
#include "pico/stdlib.h"
#include "tusb.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#define UART_ID   uart0
#define UART_TX   0
#define UART_RX   1
#define UART_BAUD 115200

// Receive buffer for UART → USB direction
static uint8_t uart_rx_buf[512];
static volatile uint16_t rx_head = 0, rx_tail = 0;

void uart_rx_irq(void) {
    while (uart_is_readable(UART_ID)) {
        uint8_t byte = uart_getc(UART_ID);
        uint16_t next = (rx_head + 1) % sizeof(uart_rx_buf);
        if (next != rx_tail) {
            uart_rx_buf[rx_head] = byte;
            rx_head = next;
        }
    }
}

int main(void) {
    stdio_init_all();
    tusb_init();

    uart_init(UART_ID, UART_BAUD);
    gpio_set_function(UART_TX, GPIO_FUNC_UART);
    gpio_set_function(UART_RX, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    // Enable UART RX interrupt
    irq_set_exclusive_handler(UART0_IRQ, uart_rx_irq);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    while (true) {
        tud_task();  // TinyUSB device task

        // USB → UART: send any CDC data down the UART
        if (tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));
            uart_write_blocking(UART_ID, buf, count);
        }

        // UART → USB: send any UART data up to the host
        if (rx_head != rx_tail && tud_cdc_write_available() > 0) {
            uint8_t buf[64];
            uint16_t count = 0;
            while (rx_tail != rx_head && count < 64) {
                buf[count++] = uart_rx_buf[rx_tail];
                rx_tail = (rx_tail + 1) % sizeof(uart_rx_buf);
            }
            tud_cdc_write(buf, count);
            tud_cdc_write_flush();
        }
    }
}

// TinyUSB callback: host changed line coding (baud rate etc.)
void tud_cdc_line_coding_cb(uint8_t itf, cdc_line_coding_t const *p_line_coding) {
    (void)itf;
    uart_set_baudrate(UART_ID, p_line_coding->bit_rate);

    uart_parity_t parity;
    switch (p_line_coding->parity) {
        case 1:  parity = UART_PARITY_ODD;  break;
        case 2:  parity = UART_PARITY_EVEN; break;
        default: parity = UART_PARITY_NONE; break;
    }
    uart_set_format(UART_ID,
        p_line_coding->data_bits,
        p_line_coding->stop_bits == 2 ? 2 : 1,
        parity);
}

// TinyUSB callback: host changed control lines (DTR, RTS)
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    // Drive GPIO for DTR/RTS if connected to target device
    // gpio_put(PIN_DTR, dtr);
    // gpio_put(PIN_RTS, rts);
}
```

---

## 15. Error Handling and Edge Cases <a name="error-handling"></a>

### Common Error Conditions and Mitigations

| Error Condition | Cause | Mitigation |
|---|---|---|
| USB disconnect during transfer | Cable pull, power loss | Handle `ENODEV`/`ERROR_FILE_NOT_FOUND`; implement reconnect loop |
| Baud rate mismatch | Incorrect `SET_LINE_CODING` | Always verify with `GET_LINE_CODING`; check UART framing errors |
| RX overrun | UART faster than USB can drain | Implement RTS flow control; increase bridge RX buffer |
| Stale data on open | Leftover bytes in device FIFO | Flush buffers on open: `tcflush()`, `PurgeComm()`, or USB Reset |
| DTR-triggered unintentional reset | Arduino/ESP reset on DTR | Disable DTR on open; add hardware capacitor filter on RST line |
| CH340 latency | Fixed 8ms flush interval | Cannot reduce (no `latency_timer` sysfs node); use FTDI for timing-sensitive apps |
| Baud rate quantization error | Divisor rounding | Accept up to ±3% error; most UART receivers tolerate this |
| Break condition misdetection | Noise on RX line | Check framing error flags alongside break detection |

### Detecting and Handling Overrun

```c
// Check for UART errors using termios on Linux
int serial_check_errors(int fd) {
    int errors = 0;
    ioctl(fd, TIOCGICOUNT, &errors);

    struct serial_icounter_struct icount;
    if (ioctl(fd, TIOCGICOUNT, &icount) == 0) {
        if (icount.overrun) printf("Overrun errors: %d\n", icount.overrun);
        if (icount.frame)   printf("Framing errors: %d\n", icount.frame);
        if (icount.parity)  printf("Parity errors:  %d\n", icount.parity);
        if (icount.brk)     printf("Break signals:  %d\n", icount.brk);
    }
    return 0;
}
```

---

## 16. Debugging and Troubleshooting <a name="debugging"></a>

### Linux Tools

```bash
# Identify bridge chip from USB descriptors
lsusb -v -d 0403:6001       # FTDI FT232
lsusb -v -d 10c4:ea60       # CP2102
lsusb -v -d 1a86:7523       # CH340
lsusb -v -d 067b:2303       # PL2303

# Check which driver is bound
ls -la /sys/bus/usb-serial/devices/ttyUSB0/driver

# Monitor kernel messages during plug/unplug
sudo dmesg -w | grep -E 'ttyUSB|ftdi|cp210x|ch341'

# Capture all serial traffic with strace
strace -e read,write -p $(pgrep minicom) 2>&1 | cat

# Use minicom or picocom for interactive testing
picocom -b 115200 /dev/ttyUSB0

# Dump raw USB traffic (requires usbmon kernel module)
sudo modprobe usbmon
sudo tcpdump -i usbmon1 -w usb_capture.pcapng
# Then open in Wireshark: Analyze > Decode As > USB Serial

# Check current FTDI latency timer
cat /sys/bus/usb-serial/devices/ttyUSB0/latency_timer
```

### Wireshark USB Dissection

Wireshark can dissect CDC-ACM traffic. Filter: `usb.transfer_type == 0x03` (bulk transfers). Look for:
- `SET_LINE_CODING` in control transfers
- Bulk OUT packets (host → device data)
- Bulk IN packets (device → host data, check size patterns for buffer flush behavior)

---

## 17. Summary <a name="summary"></a>

USB-to-Serial protocol translation bridges two fundamentally different communication architectures. The key insights from this document are:

**Protocol Architecture**: USB is packet-oriented, host-initiated, and class-abstracted. UART is stream-oriented, asynchronous, and signalling-based. The bridge must translate between these paradigms for data, flow control, modem signalling, and error states.

**The Flush Timeout Problem**: The single most important characteristic of USB-serial bridges is the mandatory buffering timeout. UART bytes accumulate in a bridge FIFO and are flushed to the USB host either when the buffer fills (typically 64 bytes) or on a timeout (1–16 ms). This creates an irreducible latency floor and is the primary reason USB-serial bridges cannot be used as drop-in replacements for native UARTs in hard real-time applications.

**CDC-ACM vs Vendor Protocols**: CDC-ACM is the portable, standards-based approach. Vendor chips (FTDI, CP210x, CH340) offer better performance and additional features but require vendor-specific drivers. For new designs where the MCU has a USB peripheral, implementing CDC-ACM in firmware is the most flexible and driver-free option.

**Modem Signal Bridging**: DTR, RTS, CTS, DSR, DCD and RI must be explicitly translated via USB control requests and interrupt notifications. Many systems rely on DTR for device reset; this must be handled carefully to avoid unintentional resets.

**C/C++ on the host**: POSIX `termios` on Linux and Win32 `DCB`/`COMMAPI` on Windows provide the standard abstraction. `libusb` enables direct vendor protocol access when kernel driver bypass is needed.

**Rust on the host**: The `serialport` crate provides cross-platform, idiomatic serial access. The `rusb` crate enables direct USB control for CDC-ACM requests. `tokio-serial` enables async bridging for production applications.

**Embedded firmware**: TinyUSB on RP2040, STM32, or similar MCUs enables a pure software USB-to-UART bridge. The core bridge logic is a two-direction buffering loop with flush timeout management and CDC callback handlers for line coding and control line state changes.

Understanding these translation mechanics is essential for anyone building robust communication infrastructure between a host computer and embedded targets.

---

*Document: 80 — USB-to-Serial Protocol Translation | UART Topic Series*