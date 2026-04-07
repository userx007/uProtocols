# 56. Linux Serial Programming

**Conceptual coverage** — UART frame anatomy, Linux serial subsystem layers (line discipline → UART driver → hardware), and the termios data structure with all four flag groups explained.

**C/C++ examples:**
- Full `configure_serial_port()` function with all flags annotated
- Non-standard baud rate via `termios2`/`ioctl`
- RS-485 half-duplex setup
- `select()` and `poll()` for non-blocking I/O
- A complete serial echo server
- A NMEA GPS sentence parser (C++)
- A Modbus RTU binary protocol implementation with CRC-16

**Rust examples:**
- `serialport` crate: basic open/read/write, GPS NMEA reader, and a framed binary protocol with CRC-8
- `nix` crate: low-level `termios` configuration mirroring the C approach for maximum control

**Summary** highlights the critical pitfalls — forgotten `O_NOCTTY`, missing `tcflush()`, `IXON`/`IXOFF` corrupting binary data, and `VTIME` precision limitations — which are the most common sources of bugs in real embedded Linux projects.

## Using the termios API for UART Communication in Linux

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART and Serial Communication Fundamentals](#uart-and-serial-communication-fundamentals)
3. [Linux Serial Subsystem Overview](#linux-serial-subsystem-overview)
4. [The termios API](#the-termios-api)
5. [Opening a Serial Port](#opening-a-serial-port)
6. [Configuring Serial Port Parameters](#configuring-serial-port-parameters)
7. [Reading and Writing Data](#reading-and-writing-data)
8. [Canonical vs. Raw Mode](#canonical-vs-raw-mode)
9. [Timeouts and Blocking Behavior](#timeouts-and-blocking-behavior)
10. [Hardware and Software Flow Control](#hardware-and-software-flow-control)
11. [Error Handling](#error-handling)
12. [Advanced Topics](#advanced-topics)
13. [Complete C/C++ Examples](#complete-cc-examples)
14. [Rust Serial Programming](#rust-serial-programming)
15. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems and Linux-based platforms. It enables asynchronous, full-duplex communication between devices using just two data lines (TX and RX), making it indispensable for microcontroller interfacing, GPS modules, Bluetooth adapters, industrial sensors, modems, and debug consoles.

On Linux, serial ports are exposed as character device files under `/dev/`:

- `/dev/ttyS0`, `/dev/ttyS1`, ... — native hardware UART ports
- `/dev/ttyUSB0`, `/dev/ttyUSB1`, ... — USB-to-serial adapters (via `cp210x`, `ftdi_sio`, etc.)
- `/dev/ttyACM0`, `/dev/ttyACM1`, ... — USB CDC ACM devices (e.g., Arduino)
- `/dev/ttyAMA0` — ARM UART (common on Raspberry Pi)

The primary API for configuring and using these devices is **termios** (terminal I/O), defined in `<termios.h>`. It provides a POSIX-standardized interface for setting baud rates, character framing, parity, flow control, and I/O modes.

---

## UART and Serial Communication Fundamentals

UART transmits data one bit at a time over a single wire per direction. A typical UART frame consists of:

```
IDLE  START  D0  D1  D2  D3  D4  D5  D6  D7  [PARITY]  STOP(S)  IDLE
 1     0     b   b   b   b   b   b   b   b     (opt)    1 or 2     1
```

Key parameters that must match on both ends:

| Parameter     | Common Values                         |
|---------------|---------------------------------------|
| Baud rate     | 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600 |
| Data bits     | 5, 6, 7, 8 (almost always 8)          |
| Parity        | None, Even, Odd                       |
| Stop bits     | 1, 2                                  |
| Flow control  | None, RTS/CTS (hardware), XON/XOFF (software) |

The shorthand notation **8N1** (8 data bits, No parity, 1 stop bit) is by far the most common configuration.

---

## Linux Serial Subsystem Overview

```
User Space Application
        │
        │  open() / read() / write() / ioctl()
        ▼
  /dev/ttyS0  (character device file)
        │
        ▼
   Line Discipline (N_TTY by default)
   - Canonical/raw mode buffering
   - Echo, signal generation
   - Software flow control (XON/XOFF)
        │
        ▼
   UART Driver (e.g., 8250/16550 driver)
   - Baud rate generator
   - FIFO management
   - Hardware flow control (RTS/CTS)
        │
        ▼
   Physical UART Hardware (TX/RX pins)
```

The **line discipline** is the software layer between the driver and user space. For raw binary communication (the most common case in embedded work), it must be set to **raw mode** to disable all character processing.

---

## The termios API

The `termios` structure (defined in `<termios.h>`) holds all serial port configuration:

```c
struct termios {
    tcflag_t c_iflag;   // Input modes
    tcflag_t c_oflag;   // Output modes
    tcflag_t c_cflag;   // Control modes (baud rate, framing)
    tcflag_t c_lflag;   // Local modes (canonical, echo, signals)
    cc_t     c_cc[NCCS]; // Special characters (VMIN, VTIME, etc.)
};
```

### Key Functions

| Function                             | Description                                      |
|--------------------------------------|--------------------------------------------------|
| `tcgetattr(fd, &tty)`                | Read current settings into `tty`                 |
| `tcsetattr(fd, TCSANOW, &tty)`       | Apply settings immediately                       |
| `tcsetattr(fd, TCSADRAIN, &tty)`     | Apply after all output has been transmitted      |
| `tcsetattr(fd, TCSAFLUSH, &tty)`     | Apply after output transmitted, discard input    |
| `cfsetispeed(&tty, B115200)`         | Set input baud rate                              |
| `cfsetospeed(&tty, B115200)`         | Set output baud rate                             |
| `cfgetispeed(&tty)`                  | Get current input baud rate                      |
| `cfgetospeed(&tty)`                  | Get current output baud rate                     |
| `tcdrain(fd)`                        | Wait until all output has been sent              |
| `tcflush(fd, TCIOFLUSH)`             | Discard buffered input and/or output             |
| `tcsendbreak(fd, 0)`                 | Send a BREAK signal                              |

---

## Opening a Serial Port

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

int open_serial_port(const char *device) {
    int fd = open(device, O_RDWR    // Read and write
                        | O_NOCTTY  // Don't make this the controlling terminal
                        | O_NDELAY  // Open in non-blocking mode initially
                        | O_CLOEXEC); // Close on exec

    if (fd < 0) {
        fprintf(stderr, "Error opening %s: %s\n", device, strerror(errno));
        return -1;
    }

    // Switch back to blocking mode for reads
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    return fd;
}
```

**Important flags:**

- `O_NOCTTY` — Prevents the terminal from becoming the process's controlling terminal, which would allow signals like `SIGINT` to interrupt your program from serial data.
- `O_NDELAY` — Opens without waiting for a carrier detect (DCD) signal; important when no modem is attached.

---

## Configuring Serial Port Parameters

### Full Configuration Function (C)

```c
#include <termios.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

typedef enum {
    PARITY_NONE,
    PARITY_EVEN,
    PARITY_ODD
} parity_t;

int configure_serial_port(int fd,
                          int baud_rate,
                          int data_bits,     // 5, 6, 7, 8
                          parity_t parity,
                          int stop_bits,     // 1 or 2
                          int hw_flow_ctrl)  // 0 = none, 1 = RTS/CTS
{
    struct termios tty;

    // Read existing settings
    if (tcgetattr(fd, &tty) != 0) {
        fprintf(stderr, "tcgetattr error: %s\n", strerror(errno));
        return -1;
    }

    // ---- Baud Rate ----
    speed_t speed;
    switch (baud_rate) {
        case 50:      speed = B50;      break;
        case 75:      speed = B75;      break;
        case 110:     speed = B110;     break;
        case 300:     speed = B300;     break;
        case 600:     speed = B600;     break;
        case 1200:    speed = B1200;    break;
        case 2400:    speed = B2400;    break;
        case 4800:    speed = B4800;    break;
        case 9600:    speed = B9600;    break;
        case 19200:   speed = B19200;   break;
        case 38400:   speed = B38400;   break;
        case 57600:   speed = B57600;   break;
        case 115200:  speed = B115200;  break;
        case 230400:  speed = B230400;  break;
        case 460800:  speed = B460800;  break;
        case 921600:  speed = B921600;  break;
        default:
            fprintf(stderr, "Unsupported baud rate: %d\n", baud_rate);
            return -1;
    }
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // ---- Control Modes (c_cflag) ----
    tty.c_cflag |= CREAD;   // Enable receiver
    tty.c_cflag |= CLOCAL;  // Ignore modem control lines

    // Data bits
    tty.c_cflag &= ~CSIZE;  // Clear data size bits
    switch (data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        case 8: tty.c_cflag |= CS8; break;
        default:
            fprintf(stderr, "Invalid data bits: %d\n", data_bits);
            return -1;
    }

    // Parity
    switch (parity) {
        case PARITY_NONE:
            tty.c_cflag &= ~PARENB;  // No parity
            tty.c_cflag &= ~PARODD;
            break;
        case PARITY_EVEN:
            tty.c_cflag |= PARENB;   // Enable parity
            tty.c_cflag &= ~PARODD;  // Even
            break;
        case PARITY_ODD:
            tty.c_cflag |= PARENB;   // Enable parity
            tty.c_cflag |= PARODD;   // Odd
            break;
    }

    // Stop bits
    if (stop_bits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    // Hardware flow control
    if (hw_flow_ctrl)
        tty.c_cflag |= CRTSCTS;
    else
        tty.c_cflag &= ~CRTSCTS;

    // ---- Input Modes (c_iflag) ----
    // Disable software flow control
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    // Disable special byte processing
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    // ---- Output Modes (c_oflag) ----
    tty.c_oflag &= ~OPOST;   // Raw output (no special processing)
    tty.c_oflag &= ~ONLCR;   // Don't convert \n to \r\n

    // ---- Local Modes (c_lflag) ----
    tty.c_lflag &= ~ICANON;  // Disable canonical (line) mode → raw mode
    tty.c_lflag &= ~ECHO;    // Disable echo
    tty.c_lflag &= ~ECHOE;   // Disable erase echo
    tty.c_lflag &= ~ECHONL;  // Disable newline echo
    tty.c_lflag &= ~ISIG;    // Disable signal generation (SIGINT, SIGQUIT)

    // ---- Special Characters ----
    // VMIN = 0, VTIME = 0: Return immediately with whatever data is available
    // VMIN = 1, VTIME = 0: Block until at least 1 byte is received
    // VMIN = 0, VTIME = N: Timeout after N * 100ms
    // VMIN = N, VTIME = N: Block until N bytes, with inter-byte timeout
    tty.c_cc[VMIN]  = 1;    // Wait for at least 1 byte
    tty.c_cc[VTIME] = 10;   // 1 second timeout (10 * 100ms)

    // Apply settings immediately
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        fprintf(stderr, "tcsetattr error: %s\n", strerror(errno));
        return -1;
    }

    // Flush any stale data
    tcflush(fd, TCIOFLUSH);

    return 0;
}
```

### Setting Non-Standard Baud Rates (Linux-specific)

For baud rates not in the POSIX standard (e.g., 250000, 1000000), use the Linux `termios2` extension:

```c
#include <asm/termios.h>   // struct termios2, BOTHER
#include <sys/ioctl.h>

int set_custom_baud(int fd, unsigned int baud) {
    struct termios2 tio;

    if (ioctl(fd, TCGETS2, &tio) != 0)
        return -1;

    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = baud;
    tio.c_ospeed = baud;

    return ioctl(fd, TCSETS2, &tio);
}
```

---

## Reading and Writing Data

### Writing

```c
#include <unistd.h>
#include <string.h>

ssize_t serial_write(int fd, const void *buf, size_t len) {
    ssize_t bytes_written = 0;
    const uint8_t *ptr = (const uint8_t *)buf;

    while (bytes_written < (ssize_t)len) {
        ssize_t n = write(fd, ptr + bytes_written, len - bytes_written);
        if (n < 0) {
            if (errno == EINTR) continue;  // Interrupted by signal, retry
            perror("serial write error");
            return -1;
        }
        bytes_written += n;
    }

    // Ensure all bytes are physically transmitted
    tcdrain(fd);
    return bytes_written;
}

// Example: Send an AT command
void send_at_command(int fd) {
    const char *cmd = "AT+CGMI\r\n";
    serial_write(fd, cmd, strlen(cmd));
}
```

### Reading

```c
#include <unistd.h>
#include <errno.h>

// Blocking read with exact byte count
ssize_t serial_read_exact(int fd, void *buf, size_t len, int timeout_ms) {
    ssize_t total = 0;
    uint8_t *ptr = (uint8_t *)buf;
    struct timeval tv_start, tv_now;
    gettimeofday(&tv_start, NULL);

    while (total < (ssize_t)len) {
        // Check elapsed time
        gettimeofday(&tv_now, NULL);
        long elapsed = (tv_now.tv_sec - tv_start.tv_sec) * 1000
                     + (tv_now.tv_usec - tv_start.tv_usec) / 1000;
        if (elapsed >= timeout_ms) {
            fprintf(stderr, "Read timeout after %d ms\n", timeout_ms);
            break;
        }

        ssize_t n = read(fd, ptr + total, len - total);
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            perror("serial read error");
            return -1;
        }
        if (n == 0) break;  // EOF
        total += n;
    }

    return total;
}

// Reading with select() for multiplexing or tighter timeouts
ssize_t serial_read_select(int fd, void *buf, size_t len, int timeout_ms) {
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(fd, &readfds);

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &readfds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("select error");
        return -1;
    }
    if (ret == 0) {
        fprintf(stderr, "Timeout waiting for data\n");
        return 0;
    }

    return read(fd, buf, len);
}
```

---

## Canonical vs. Raw Mode

The line discipline can operate in two fundamental modes:

### Canonical Mode (default for terminals)

Input is processed line by line. A `read()` call returns only when a complete line (terminated by `\n`, `\r`, or `EOF`) has been received. Special characters like backspace are processed to edit the line buffer. This is appropriate for interactive terminal sessions but **not** for binary protocol communication.

```c
// Enable canonical mode
tty.c_lflag |= ICANON;
tty.c_lflag |= ECHO | ECHOE;  // Echo input and erase
```

### Raw Mode (required for binary UART communication)

Every byte received is passed directly to user space without any processing. This is mandatory for binary protocols, custom framing, embedded device communication, etc.

```c
// Enable raw mode — all of these must be cleared
tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ECHONL | ISIG | IEXTEN);
tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
tty.c_oflag &= ~(OPOST | ONLCR);

// cfmakeraw() is a convenience function that sets all of the above:
cfmakeraw(&tty);
```

---

## Timeouts and Blocking Behavior

The `VMIN` and `VTIME` values in `c_cc[]` control when `read()` returns:

| VMIN | VTIME | Behavior                                                        |
|------|-------|-----------------------------------------------------------------|
| 0    | 0     | Non-blocking: return immediately with available data (0 if none) |
| 1    | 0     | Block indefinitely until at least 1 byte arrives               |
| 0    | N     | Return after N×100ms timeout, or when data arrives             |
| M    | N     | Block until M bytes received; N×100ms inter-byte timeout       |

```c
// Non-blocking read
tty.c_cc[VMIN]  = 0;
tty.c_cc[VTIME] = 0;

// Block until 1 byte (most common for event-driven)
tty.c_cc[VMIN]  = 1;
tty.c_cc[VTIME] = 0;

// Timeout-based: return after 500ms even if no data
tty.c_cc[VMIN]  = 0;
tty.c_cc[VTIME] = 5;  // 5 * 100ms = 500ms

// Wait for 64 bytes, with 200ms inter-byte timeout
tty.c_cc[VMIN]  = 64;
tty.c_cc[VTIME] = 2;
```

---

## Hardware and Software Flow Control

### Software Flow Control (XON/XOFF)

Uses ASCII control characters `DC1` (0x11, XON) and `DC3` (0x13, XOFF) embedded in the data stream. Unreliable for binary data (since these bytes may appear in payloads).

```c
tty.c_iflag |= (IXON | IXOFF);  // Enable XON/XOFF
tty.c_iflag |= IXANY;           // Any character restarts output
```

### Hardware Flow Control (RTS/CTS)

Uses dedicated signal lines: RTS (Request to Send) and CTS (Clear to Send). The receiver asserts RTS to signal it is ready; the transmitter checks CTS before sending. Reliable for binary protocols.

```c
tty.c_cflag |= CRTSCTS;   // Enable RTS/CTS hardware flow control
```

### Manual RTS/DTR Control (via ioctl)

```c
#include <sys/ioctl.h>

// Assert RTS
int flags;
ioctl(fd, TIOCMGET, &flags);
flags |= TIOCM_RTS;
ioctl(fd, TIOCMSET, &flags);

// De-assert DTR
ioctl(fd, TIOCMGET, &flags);
flags &= ~TIOCM_DTR;
ioctl(fd, TIOCMSET, &flags);

// Read modem status bits
int status;
ioctl(fd, TIOCMGET, &status);
if (status & TIOCM_CTS) printf("CTS is asserted\n");
if (status & TIOCM_DSR) printf("DSR is asserted\n");
if (status & TIOCM_CAR) printf("DCD is asserted\n");
```

---

## Error Handling

```c
#include <termios.h>
#include <errno.h>
#include <string.h>

// Check for framing, parity, and overrun errors
// Enable error marking first:
// tty.c_iflag |= PARMRK;  — marks erroneous bytes as \377 \0 <byte>

// Detect overrun via UART statistics (Linux-specific)
#include <linux/serial.h>

void check_serial_errors(int fd) {
    struct serial_icounter_struct counters;
    if (ioctl(fd, TIOCGICOUNT, &counters) == 0) {
        printf("RX: %d, TX: %d\n", counters.rx, counters.tx);
        printf("Frame errors: %d\n", counters.frame);
        printf("Parity errors: %d\n", counters.parity);
        printf("Overrun errors: %d\n", counters.overrun);
        printf("Break events: %d\n", counters.brk);
    }
}
```

---

## Advanced Topics

### Using `poll()` / `epoll()` for Non-Blocking I/O

```c
#include <poll.h>

int serial_poll_read(int fd, void *buf, size_t len, int timeout_ms) {
    struct pollfd pfd = {
        .fd = fd,
        .events = POLLIN
    };

    int ret = poll(&pfd, 1, timeout_ms);
    if (ret < 0) {
        perror("poll");
        return -1;
    }
    if (ret == 0) return 0;  // Timeout

    if (pfd.revents & POLLIN)
        return read(fd, buf, len);
    if (pfd.revents & POLLERR)
        fprintf(stderr, "Serial port error\n");

    return -1;
}
```

### RS-485 Half-Duplex Mode

RS-485 uses a single differential pair for both TX and RX. The driver must automatically control the direction via RTS assertion, or the hardware supports auto-direction control.

```c
#include <linux/serial.h>

void enable_rs485(int fd) {
    struct serial_rs485 rs485conf = {0};
    rs485conf.flags |= SER_RS485_ENABLED;
    rs485conf.flags |= SER_RS485_RTS_ON_SEND;    // RTS high when sending
    rs485conf.flags &= ~SER_RS485_RTS_AFTER_SEND; // RTS low after sending
    rs485conf.delay_rts_before_send = 0;  // Microseconds
    rs485conf.delay_rts_after_send  = 0;

    if (ioctl(fd, TIOCSRS485, &rs485conf) < 0)
        perror("RS-485 ioctl failed");
}
```

---

## Complete C/C++ Examples

### Example 1: Complete Serial Echo Server (C)

```c
// serial_echo.c — Opens a serial port and echoes received bytes back
// Compile: gcc -o serial_echo serial_echo.c
// Usage:   ./serial_echo /dev/ttyUSB0 115200

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <termios.h>

static volatile int running = 1;

void sigint_handler(int sig) {
    (void)sig;
    running = 0;
}

int open_and_configure(const char *device, int baud) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    // Restore blocking mode
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    // Set baud rate
    speed_t speed = (baud == 115200) ? B115200 :
                    (baud == 9600)   ? B9600   :
                    (baud == 57600)  ? B57600  : B115200;
    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    // Raw mode
    cfmakeraw(&tty);

    // 8N1
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= CREAD | CLOCAL;

    // Block until at least 1 byte
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <device> <baud>\n", argv[0]);
        return 1;
    }

    signal(SIGINT, sigint_handler);

    int fd = open_and_configure(argv[1], atoi(argv[2]));
    if (fd < 0) return 1;

    printf("Opened %s at %s baud. Echoing... (Ctrl-C to quit)\n",
           argv[1], argv[2]);

    uint8_t buf[256];
    while (running) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            break;
        }
        if (n > 0) {
            write(fd, buf, n);  // Echo back
            printf("Echoed %zd bytes\n", n);
        }
    }

    close(fd);
    printf("\nClosed serial port.\n");
    return 0;
}
```

### Example 2: NMEA GPS Reader (C++)

```cpp
// gps_reader.cpp — Reads NMEA sentences from a GPS module
// Compile: g++ -std=c++17 -o gps_reader gps_reader.cpp
// Usage:   ./gps_reader /dev/ttyAMA0 9600

#include <iostream>
#include <string>
#include <vector>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>

class SerialPort {
public:
    SerialPort() : fd_(-1) {}

    ~SerialPort() {
        if (fd_ >= 0) close(fd_);
    }

    bool open(const std::string &device, int baud) {
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_NDELAY | O_CLOEXEC);
        if (fd_ < 0) {
            std::cerr << "Cannot open " << device << ": " << strerror(errno) << "\n";
            return false;
        }

        // Restore blocking mode
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags & ~O_NONBLOCK);

        struct termios tty{};
        if (tcgetattr(fd_, &tty) != 0) {
            std::cerr << "tcgetattr: " << strerror(errno) << "\n";
            return false;
        }

        speed_t speed;
        switch (baud) {
            case 4800:   speed = B4800;   break;
            case 9600:   speed = B9600;   break;
            case 38400:  speed = B38400;  break;
            case 115200: speed = B115200; break;
            default:     speed = B9600;
        }

        cfsetispeed(&tty, speed);
        cfsetospeed(&tty, speed);
        cfmakeraw(&tty);

        tty.c_cflag &= ~CSIZE;
        tty.c_cflag |= CS8 | CREAD | CLOCAL;
        tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);

        // Block on each byte for line-reading
        tty.c_cc[VMIN]  = 1;
        tty.c_cc[VTIME] = 0;

        if (tcsetattr(fd_, TCSANOW, &tty) != 0) {
            std::cerr << "tcsetattr: " << strerror(errno) << "\n";
            return false;
        }

        tcflush(fd_, TCIOFLUSH);
        return true;
    }

    // Read a single line terminated by '\n'
    std::string readLine() {
        std::string line;
        char c;
        while (true) {
            ssize_t n = read(fd_, &c, 1);
            if (n < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (n == 0) break;
            if (c == '\n') break;
            if (c != '\r') line += c;
        }
        return line;
    }

private:
    int fd_;
};

// Parse $GPGGA sentence: time, lat, lon, fix quality, satellite count
struct GGA {
    std::string time;
    double lat = 0, lon = 0;
    int fix_quality = 0;
    int num_satellites = 0;
    double altitude = 0;
};

bool parse_gga(const std::string &sentence, GGA &gga) {
    if (sentence.substr(0, 6) != "$GPGGA" &&
        sentence.substr(0, 6) != "$GNGGA") return false;

    std::vector<std::string> fields;
    std::stringstream ss(sentence);
    std::string token;
    while (std::getline(ss, token, ','))
        fields.push_back(token);

    if (fields.size() < 10) return false;

    gga.time = fields[1];
    if (!fields[2].empty() && !fields[4].empty()) {
        // Latitude: DDMM.MMMMM → decimal degrees
        double raw_lat = std::stod(fields[2]);
        int deg = (int)(raw_lat / 100);
        double min = raw_lat - deg * 100;
        gga.lat = deg + min / 60.0;
        if (fields[3] == "S") gga.lat = -gga.lat;

        // Longitude: DDDMM.MMMMM → decimal degrees
        double raw_lon = std::stod(fields[4]);
        deg = (int)(raw_lon / 100);
        min = raw_lon - deg * 100;
        gga.lon = deg + min / 60.0;
        if (fields[5] == "W") gga.lon = -gga.lon;
    }

    if (!fields[6].empty()) gga.fix_quality    = std::stoi(fields[6]);
    if (!fields[7].empty()) gga.num_satellites = std::stoi(fields[7]);
    if (!fields[9].empty()) gga.altitude       = std::stod(fields[9]);

    return true;
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <device> <baud>\n";
        return 1;
    }

    SerialPort port;
    if (!port.open(argv[1], std::stoi(argv[2]))) return 1;

    std::cout << "Reading GPS data from " << argv[1] << "...\n";

    while (true) {
        std::string line = port.readLine();
        if (line.empty()) continue;

        GGA gga;
        if (parse_gga(line, gga) && gga.fix_quality > 0) {
            printf("Time: %s | Lat: %.6f | Lon: %.6f | "
                   "Fix: %d | Sats: %d | Alt: %.1fm\n",
                   gga.time.c_str(), gga.lat, gga.lon,
                   gga.fix_quality, gga.num_satellites, gga.altitude);
        }
    }
    return 0;
}
```

### Example 3: Modbus RTU Frame Send/Receive (C)

```c
// modbus_rtu.c — Send a Modbus RTU Read Holding Registers request
// This demonstrates binary protocol communication over UART

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/time.h>

// CRC-16/MODBUS
uint16_t modbus_crc(const uint8_t *data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) crc = (crc >> 1) ^ 0xA001;
            else               crc >>= 1;
        }
    }
    return crc;
}

// Build and send Modbus FC03 (Read Holding Registers)
int modbus_read_registers(int fd, uint8_t slave_id,
                          uint16_t start_addr, uint16_t count,
                          uint16_t *out_regs)
{
    // Build request frame: [addr][FC][addr_hi][addr_lo][count_hi][count_lo][CRC_lo][CRC_hi]
    uint8_t request[8];
    request[0] = slave_id;
    request[1] = 0x03;                      // FC03: Read Holding Registers
    request[2] = (start_addr >> 8) & 0xFF;
    request[3] =  start_addr       & 0xFF;
    request[4] = (count      >> 8) & 0xFF;
    request[5] =  count            & 0xFF;

    uint16_t crc = modbus_crc(request, 6);
    request[6] = crc & 0xFF;
    request[7] = (crc >> 8) & 0xFF;

    // Flush any old data
    tcflush(fd, TCIOFLUSH);

    // Send request
    if (write(fd, request, sizeof(request)) != sizeof(request)) {
        perror("write modbus request");
        return -1;
    }
    tcdrain(fd);

    // Expected response: [addr][FC][byte_count][data...][CRC_lo][CRC_hi]
    size_t expected_len = 5 + count * 2;
    uint8_t response[256];
    ssize_t total = 0;

    struct timeval start, now;
    gettimeofday(&start, NULL);

    while (total < (ssize_t)expected_len) {
        gettimeofday(&now, NULL);
        long elapsed_ms = (now.tv_sec - start.tv_sec) * 1000
                        + (now.tv_usec - start.tv_usec) / 1000;
        if (elapsed_ms > 500) {
            fprintf(stderr, "Modbus timeout\n");
            return -1;
        }

        ssize_t n = read(fd, response + total, expected_len - total);
        if (n < 0 && errno != EINTR) {
            perror("read modbus response");
            return -1;
        }
        if (n > 0) total += n;
    }

    // Verify CRC
    uint16_t recv_crc = response[total-2] | (response[total-1] << 8);
    uint16_t calc_crc = modbus_crc(response, total - 2);
    if (recv_crc != calc_crc) {
        fprintf(stderr, "CRC mismatch: got 0x%04X expected 0x%04X\n",
                recv_crc, calc_crc);
        return -1;
    }

    // Extract register values
    uint8_t byte_count = response[2];
    for (int i = 0; i < byte_count / 2; i++) {
        out_regs[i] = (response[3 + i*2] << 8) | response[4 + i*2];
    }

    return byte_count / 2;
}

int open_serial_485(const char *dev) {
    int fd = open(dev, O_RDWR | O_NOCTTY | O_NDELAY | O_CLOEXEC);
    if (fd < 0) { perror("open"); return -1; }

    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) & ~O_NONBLOCK);

    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 5;  // 500ms inter-byte timeout
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(void) {
    int fd = open_serial_485("/dev/ttyUSB0");
    if (fd < 0) return 1;

    uint16_t registers[10];
    int count = modbus_read_registers(fd, 1,  // slave ID 1
                                      0,      // starting address
                                      10,     // read 10 registers
                                      registers);
    if (count > 0) {
        printf("Read %d registers:\n", count);
        for (int i = 0; i < count; i++)
            printf("  [%d] = %u (0x%04X)\n", i, registers[i], registers[i]);
    }

    close(fd);
    return 0;
}
```

---

## Rust Serial Programming

Rust provides safe, ergonomic serial port access primarily through two crates:

- **`serialport`** — high-level, cross-platform, wraps termios on Linux
- **`nix`** — low-level, safe Rust wrappers over POSIX/Linux system calls (including termios)

### Using the `serialport` Crate

Add to `Cargo.toml`:

```toml
[dependencies]
serialport = "4"
```

#### Example 1: Basic Open and Read/Write

```rust
// src/main.rs — Basic serial port echo using the `serialport` crate
use serialport::{SerialPort, DataBits, FlowControl, Parity, StopBits};
use std::io::{self, Read, Write};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // List available ports
    let ports = serialport::available_ports()?;
    println!("Available serial ports:");
    for p in &ports {
        println!("  {}", p.port_name);
    }

    // Open port
    let mut port = serialport::new("/dev/ttyUSB0", 115_200)
        .data_bits(DataBits::Eight)
        .flow_control(FlowControl::None)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .timeout(Duration::from_millis(1000))
        .open()?;

    println!("Opened {} at 115200 baud", port.name().unwrap_or_default());

    // Write a string
    let msg = b"Hello, UART!\r\n";
    port.write_all(msg)?;
    port.flush()?;
    println!("Sent: {:?}", std::str::from_utf8(msg)?);

    // Read response (up to 256 bytes, with 1s timeout)
    let mut buf = [0u8; 256];
    match port.read(&mut buf) {
        Ok(n) => println!("Received {} bytes: {:?}", n, &buf[..n]),
        Err(ref e) if e.kind() == io::ErrorKind::TimedOut =>
            println!("Read timed out"),
        Err(e) => eprintln!("Read error: {}", e),
    }

    Ok(())
}
```

#### Example 2: NMEA GPS Reader in Rust

```rust
// src/gps.rs — Read and parse NMEA $GPGGA sentences
use serialport::SerialPort;
use std::io::{self, BufRead, BufReader};
use std::time::Duration;

#[derive(Debug, Default)]
struct GgaFix {
    time: String,
    lat: f64,
    lon: f64,
    fix_quality: u8,
    num_satellites: u8,
    altitude_m: f64,
}

fn parse_gga(line: &str) -> Option<GgaFix> {
    if !line.starts_with("$GPGGA") && !line.starts_with("$GNGGA") {
        return None;
    }
    // Strip checksum (*XX) if present
    let data = line.split('*').next()?;
    let fields: Vec<&str> = data.split(',').collect();
    if fields.len() < 10 { return None; }

    let fix_quality: u8 = fields[6].parse().unwrap_or(0);
    if fix_quality == 0 { return None; }  // No fix

    // Parse latitude: DDMM.MMMMM
    let raw_lat: f64 = fields[2].parse().ok()?;
    let lat_deg = (raw_lat / 100.0).floor();
    let lat_min = raw_lat - lat_deg * 100.0;
    let mut lat = lat_deg + lat_min / 60.0;
    if fields[3] == "S" { lat = -lat; }

    // Parse longitude: DDDMM.MMMMM
    let raw_lon: f64 = fields[4].parse().ok()?;
    let lon_deg = (raw_lon / 100.0).floor();
    let lon_min = raw_lon - lon_deg * 100.0;
    let mut lon = lon_deg + lon_min / 60.0;
    if fields[5] == "W" { lon = -lon; }

    Some(GgaFix {
        time: fields[1].to_string(),
        lat,
        lon,
        fix_quality,
        num_satellites: fields[7].parse().unwrap_or(0),
        altitude_m: fields[9].parse().unwrap_or(0.0),
    })
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port = serialport::new("/dev/ttyAMA0", 9600)
        .timeout(Duration::from_secs(5))
        .open()?;

    println!("Reading GPS data...");

    let reader = BufReader::new(port);
    for line in reader.lines() {
        match line {
            Ok(sentence) => {
                if let Some(fix) = parse_gga(&sentence) {
                    println!(
                        "UTC: {} | Lat: {:.6}° | Lon: {:.6}° | \
                         Fix: {} | Sats: {} | Alt: {:.1}m",
                        fix.time, fix.lat, fix.lon,
                        fix.fix_quality, fix.num_satellites, fix.altitude_m
                    );
                }
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                eprintln!("Timeout waiting for GPS data");
                break;
            }
            Err(e) => eprintln!("Error: {}", e),
        }
    }
    Ok(())
}
```

#### Example 3: Binary Protocol with Framing

```rust
// src/binary_protocol.rs — Send/receive framed binary messages
// Frame format: [SOF:0xAA][LEN:u8][DATA...][CRC8]

use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::Duration;

const SOF: u8 = 0xAA;

fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

fn build_frame(payload: &[u8]) -> Vec<u8> {
    assert!(payload.len() <= 255, "Payload too long");
    let mut frame = Vec::with_capacity(3 + payload.len());
    frame.push(SOF);
    frame.push(payload.len() as u8);
    frame.extend_from_slice(payload);
    let checksum = crc8(&frame[1..]);  // CRC over LEN + DATA
    frame.push(checksum);
    frame
}

fn read_frame(port: &mut dyn SerialPort) -> Result<Vec<u8>, String> {
    let mut header = [0u8; 2];

    // Synchronize to SOF byte
    loop {
        port.read_exact(&mut header[..1])
            .map_err(|e| format!("read SOF: {}", e))?;
        if header[0] == SOF { break; }
        eprintln!("Skipping framing byte: 0x{:02X}", header[0]);
    }

    // Read LEN
    port.read_exact(&mut header[1..2])
        .map_err(|e| format!("read LEN: {}", e))?;
    let len = header[1] as usize;

    // Read DATA + CRC
    let mut buf = vec![0u8; len + 1];
    port.read_exact(&mut buf)
        .map_err(|e| format!("read DATA: {}", e))?;

    let payload = &buf[..len];
    let recv_crc = buf[len];
    let calc_crc = crc8(&[&[header[1]], payload].concat());

    if recv_crc != calc_crc {
        return Err(format!(
            "CRC mismatch: got 0x{:02X}, expected 0x{:02X}",
            recv_crc, calc_crc
        ));
    }

    Ok(payload.to_vec())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut port = serialport::new("/dev/ttyUSB0", 115_200)
        .timeout(Duration::from_millis(500))
        .open()?;

    // Send a "GET_STATUS" command (opcode 0x01)
    let request = build_frame(&[0x01]);
    port.write_all(&request)?;
    port.flush()?;
    println!("Sent frame: {:02X?}", request);

    // Read response
    match read_frame(port.as_mut()) {
        Ok(payload) => println!("Received {} bytes: {:02X?}", payload.len(), payload),
        Err(e)      => eprintln!("Frame error: {}", e),
    }

    Ok(())
}
```

### Using `nix` for Low-Level termios Access

For cases where you need direct access to `termios` flags (e.g., RS-485, custom baud rates):

```toml
[dependencies]
nix = { version = "0.27", features = ["term", "fs", "ioctl"] }
```

```rust
use nix::fcntl::{open, OFlag};
use nix::sys::stat::Mode;
use nix::sys::termios::{
    self, BaudRate, ControlFlags, InputFlags, LocalFlags, OutputFlags,
    SetArg, SpecialCharacterIndices as SCI, Termios,
};
use std::os::unix::io::RawFd;

fn configure_raw_uart(fd: RawFd, baud: BaudRate) -> nix::Result<()> {
    let mut tty: Termios = termios::tcgetattr(fd)?;

    // Set baud rate
    termios::cfsetispeed(&mut tty, baud)?;
    termios::cfsetospeed(&mut tty, baud)?;

    // Control flags: 8N1, no flow control
    tty.control_flags &= !ControlFlags::CSIZE;
    tty.control_flags |= ControlFlags::CS8
        | ControlFlags::CREAD
        | ControlFlags::CLOCAL;
    tty.control_flags &= !(
        ControlFlags::PARENB |
        ControlFlags::CSTOPB |
        ControlFlags::CRTSCTS
    );

    // Input flags: disable all processing
    tty.input_flags &= !(
        InputFlags::IGNBRK | InputFlags::BRKINT  |
        InputFlags::PARMRK | InputFlags::ISTRIP  |
        InputFlags::INLCR  | InputFlags::IGNCR   |
        InputFlags::ICRNL  | InputFlags::IXON    |
        InputFlags::IXOFF  | InputFlags::IXANY
    );

    // Output flags: raw output
    tty.output_flags &= !(OutputFlags::OPOST | OutputFlags::ONLCR);

    // Local flags: raw mode
    tty.local_flags &= !(
        LocalFlags::ICANON | LocalFlags::ECHO   |
        LocalFlags::ECHOE  | LocalFlags::ECHONL |
        LocalFlags::ISIG
    );

    // Blocking read: wait for at least 1 byte
    tty.control_chars[SCI::VMIN  as usize] = 1;
    tty.control_chars[SCI::VTIME as usize] = 0;

    termios::tcsetattr(fd, SetArg::TCSANOW, &tty)?;
    termios::tcflush(fd, termios::FlushArg::TCIOFLUSH)?;

    Ok(())
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let fd = open(
        "/dev/ttyUSB0",
        OFlag::O_RDWR | OFlag::O_NOCTTY | OFlag::O_CLOEXEC,
        Mode::empty(),
    )?;

    configure_raw_uart(fd, BaudRate::B115200)?;
    println!("Serial port configured successfully");

    // Use nix::unistd::read / write for I/O
    use nix::unistd::{read, write};
    let msg = b"Hello from Rust/nix!\r\n";
    write(fd, msg)?;

    let mut buf = [0u8; 64];
    let n = read(fd, &mut buf)?;
    println!("Received {} bytes: {:?}", n, &buf[..n]);

    nix::unistd::close(fd)?;
    Ok(())
}
```

---

## Summary

Linux serial programming via the **termios API** provides complete, POSIX-standard control over UART communication. The key concepts and takeaways are:

**Fundamentals.** UART communication is characterized by its baud rate, data bits, parity, and stop bits (e.g., 8N1 at 115200). On Linux, serial ports appear as character device files (`/dev/ttyS*`, `/dev/ttyUSB*`, `/dev/ttyAMA*`), and all configuration flows through the `termios` structure.

**Core workflow.** Every serial port application follows the same pattern: open the device with `O_RDWR | O_NOCTTY | O_NDELAY`, read the current settings with `tcgetattr()`, modify only the necessary flags, and apply with `tcsetattr()`. Always call `cfmakeraw()` or manually clear all processing flags before configuring — the default terminal settings will corrupt binary data.

**Critical flags.** `c_lflag` must disable `ICANON`, `ECHO`, and `ISIG` for raw mode. `c_iflag` must disable `ICRNL` and software flow control (`IXON`/`IXOFF`). `c_oflag` must disable `OPOST`. Failure to clear any of these is the most common source of mysterious data corruption bugs.

**VMIN and VTIME** control the blocking behavior of `read()`. Use `VMIN=1, VTIME=0` for event-driven byte reception; `VMIN=0, VTIME=N` for timeout-based polling; `VMIN=0, VTIME=0` for non-blocking reads. For complex I/O multiplexing, `select()`, `poll()`, or `epoll()` can be used instead.

**Flow control.** Prefer hardware RTS/CTS (`CRTSCTS`) for reliable binary protocols. Avoid software XON/XOFF (`IXON`/`IXOFF`) when the payload may contain `0x11` or `0x13` bytes.

**Non-standard baud rates** (250000, 1000000, etc.) require the Linux-specific `termios2` extension via `ioctl(TCSETS2)`, since POSIX only defines rates up to B921600 and support varies.

**RS-485 half-duplex** requires either automatic RTS direction control via `ioctl(TIOCSRS485)` with the `SER_RS485_ENABLED` flag, or an RS-485 transceiver with auto-direction capability.

**In Rust,** the `serialport` crate provides a safe, ergonomic, cross-platform abstraction ideal for most use cases. The `nix` crate exposes safe Rust wrappers around the full termios and ioctl surface for low-level control scenarios. Both approaches are production-ready and well-maintained.

**Key pitfalls to avoid:**
- Forgetting `O_NOCTTY` (your process can receive spurious signals from the serial line)
- Not calling `tcflush()` after configuration (stale bytes in the UART FIFO corrupt the first read)
- Neglecting `tcdrain()` after writes in timing-sensitive protocols
- Using `IXON`/`IXOFF` with binary data
- Relying on `VTIME` alone for protocol timeouts (use `select()`/`poll()` for millisecond precision)
- Ignoring error counters — always check framing, parity, and overrun errors in production embedded applications

The termios API, while verbose, gives you full control over every aspect of serial communication, making it suitable for everything from simple debug consoles to high-speed industrial fieldbus protocols running on Linux-based embedded systems.

---

*References: POSIX.1-2017, `man 3 termios`, `man 4 tty`, Linux kernel serial driver documentation, The Linux Programming Interface (Kerrisk, Chapter 62)*