# 60. Serial Port Virtualization

**Architecture** — How PTYs work on Linux (master/slave fd pair), and how the OS presents a virtual port indistinguishable from a physical UART.

**C/C++ examples** include:
- Raw PTY creation with `posix_openpt` / `grantpt` / `unlockpt` / `ptsname`
- `termios` configuration for 8N1 raw mode
- A threaded echo simulator acting as a virtual device
- A C++ RAII `VirtualSerialPort` class
- A null-modem bridge connecting two PTY pairs

**Rust examples** include:
- A `VirtualSerialPort` struct using the `nix` crate
- An echo simulator thread
- Client-side access using the `serialport` crate
- A null-modem bridge in pure Rust
- A `#[test]` loopback integration test

**Advanced patterns** cover protocol injection, traffic logging with timestamps, baud-rate throttling via `nanosleep`, and a `pytest` fixture that launches a C/Rust simulator and runs Python-level serial tests against it.

**Windows** is addressed via `com0com` and the Win32 `CreateFile`/`DCB` API, with a note on cross-platform portability using the `serialport` crate.

> **Creating virtual serial ports for testing and development**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Concepts and Architecture](#concepts-and-architecture)
3. [Linux: PTY-Based Virtual Serial Ports](#linux-pty-based-virtual-serial-ports)
4. [Using socat for Virtual Port Pairs](#using-socat-for-virtual-port-pairs)
5. [Programming Virtual Ports in C/C++](#programming-virtual-ports-in-cc)
6. [Programming Virtual Ports in Rust](#programming-virtual-ports-in-rust)
7. [Loopback Testing](#loopback-testing)
8. [Windows: Virtual COM Ports](#windows-virtual-com-ports)
9. [Cross-Platform Considerations](#cross-platform-considerations)
10. [Advanced Patterns](#advanced-patterns)
11. [Summary](#summary)

---

## Introduction

Serial Port Virtualization refers to the technique of creating software-emulated serial (UART) interfaces that behave like physical hardware ports — without requiring any physical hardware. Virtual serial ports are indispensable for:

- **Unit and integration testing** of serial protocol stacks without hardware
- **Simulation environments** where real devices are replaced by software models
- **CI/CD pipelines** that must run on machines with no serial hardware
- **Protocol development** where traffic can be injected, inspected, and replayed
- **Multi-application communication** where two processes communicate over a virtual serial link

On Linux, virtual serial ports are built on the **PTY (pseudo-terminal)** subsystem. On Windows, tools such as `com0com` or the Windows driver model provide virtual COM port pairs.

---

## Concepts and Architecture

### Physical vs. Virtual Serial Ports

| Aspect | Physical Port | Virtual Port |
|---|---|---|
| Hardware required | Yes (UART chip) | No |
| OS driver | Vendor UART driver | PTY or virtual COM driver |
| Typical name (Linux) | `/dev/ttyS0`, `/dev/ttyUSB0` | `/dev/pts/N` |
| Typical name (Windows) | `COM1` – `COM8` | `COM10+` (virtual) |
| Loopback possible | With hardware jumper | Natively in software |
| Speed limits | Hardware baud rate | Software-defined |

### PTY Architecture (Linux)

A PTY consists of two file descriptors forming a bidirectional pipe that the OS presents as a terminal device:

```
Application A            Kernel PTY Subsystem           Application B
   (master fd)  <----->  [pts/N / ttyN pair]  <----->   (slave /dev/pts/N)
  writes data               routes I/O                    reads data
```

- The **master** side is opened by the controlling process (e.g., a UART simulator).
- The **slave** side (`/dev/pts/N`) is what other applications open as if it were a real serial port.
- The two sides are connected by the kernel; `termios` settings (baud, parity, stop bits) are applied on the slave side.

---

## Linux: PTY-Based Virtual Serial Ports

### Creating a PTY Manually

The POSIX API for creating PTYs involves `posix_openpt`, `grantpt`, `unlockpt`, and `ptsname`.

```c
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

// Returns the master fd; slave_name is filled with the slave device path.
int create_virtual_serial_port(char *slave_name, size_t name_len) {
    // Open the next available PTY master
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) {
        perror("posix_openpt");
        return -1;
    }

    // Grant access to the slave PTY device
    if (grantpt(master_fd) < 0) {
        perror("grantpt");
        close(master_fd);
        return -1;
    }

    // Unlock the slave PTY
    if (unlockpt(master_fd) < 0) {
        perror("unlockpt");
        close(master_fd);
        return -1;
    }

    // Retrieve the slave device path (e.g., /dev/pts/5)
    const char *pts_name = ptsname(master_fd);
    if (!pts_name) {
        perror("ptsname");
        close(master_fd);
        return -1;
    }

    snprintf(slave_name, name_len, "%s", pts_name);
    printf("Virtual serial port created: %s\n", slave_name);
    return master_fd;
}
```

### Configuring termios on the Slave

To make the virtual port behave like a real UART (raw mode, specific baud rate):

```c
#include <termios.h>

void configure_serial(int fd, speed_t baud) {
    struct termios tty;
    tcgetattr(fd, &tty);

    // Raw mode: no canonical processing, no signals
    cfmakeraw(&tty);

    // Set input and output baud rate
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    // 8N1: 8 data bits, no parity, 1 stop bit
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        // 8 data bits

    // Disable hardware flow control
    tty.c_cflag &= ~CRTSCTS;

    // Enable receiver, ignore modem status lines
    tty.c_cflag |= CREAD | CLOCAL;

    // Non-blocking read
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    tcsetattr(fd, TCSANOW, &tty);
}
```

---

## Using socat for Virtual Port Pairs

`socat` is the quickest way to create a linked virtual serial port pair for testing from the shell:

```bash
# Create a linked pair: /tmp/vserial0 <-> /tmp/vserial1
socat -d -d \
    pty,raw,echo=0,link=/tmp/vserial0 \
    pty,raw,echo=0,link=/tmp/vserial1
```

- Application A opens `/tmp/vserial0`; Application B opens `/tmp/vserial1`.
- Data written to one side appears on the other — exactly like a null-modem cable.

To add baud rate emulation (throttle throughput):

```bash
socat -d -d \
    pty,raw,echo=0,link=/tmp/vserial0,b115200 \
    pty,raw,echo=0,link=/tmp/vserial1,b115200
```

---

## Programming Virtual Ports in C/C++

### Complete C Example: Virtual Serial Port Simulator

The following example creates a virtual port, configures it for 115200 8N1, and runs an echo server on the master side — simulating a UART device. A real application can connect to the slave path.

```c
// virtual_serial_simulator.c
// Compile: gcc -o vsim virtual_serial_simulator.c -lpthread

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <errno.h>

#define BUF_SIZE 256

typedef struct {
    int master_fd;
} SimulatorContext;

// Simulator thread: reads from master, echoes back with prefix
void *simulator_thread(void *arg) {
    SimulatorContext *ctx = (SimulatorContext *)arg;
    uint8_t buf[BUF_SIZE];
    ssize_t n;

    printf("[SIM] Simulator running. Waiting for data...\n");

    while (1) {
        n = read(ctx->master_fd, buf, sizeof(buf));
        if (n <= 0) {
            if (errno == EINTR) continue;
            perror("[SIM] read");
            break;
        }

        printf("[SIM] Received %zd bytes: ", n);
        for (ssize_t i = 0; i < n; i++) printf("%02X ", buf[i]);
        printf("\n");

        // Echo back: prepend 0xAA as a simulated device response header
        uint8_t response[BUF_SIZE + 1];
        response[0] = 0xAA;
        memcpy(&response[1], buf, n);

        ssize_t written = write(ctx->master_fd, response, n + 1);
        if (written < 0) {
            perror("[SIM] write");
            break;
        }
        printf("[SIM] Sent %zd bytes (echo with header)\n", written);
    }
    return NULL;
}

void configure_raw(int fd) {
    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetispeed(&tty, B115200);
    cfsetospeed(&tty, B115200);
    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8 | CREAD | CLOCAL;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  // 100ms read timeout
    tcsetattr(fd, TCSANOW, &tty);
}

int main(void) {
    char slave_name[64];
    int master_fd = posix_openpt(O_RDWR | O_NOCTTY);
    if (master_fd < 0) { perror("posix_openpt"); return 1; }

    grantpt(master_fd);
    unlockpt(master_fd);
    snprintf(slave_name, sizeof(slave_name), "%s", ptsname(master_fd));

    configure_raw(master_fd);

    printf("[MAIN] Virtual serial port: %s\n", slave_name);
    printf("[MAIN] Connect your application to %s\n", slave_name);

    // Start simulator thread
    SimulatorContext ctx = { .master_fd = master_fd };
    pthread_t tid;
    pthread_create(&tid, NULL, simulator_thread, &ctx);

    // Client side: open the slave and send test data
    sleep(1);  // Give the thread time to start
    int client_fd = open(slave_name, O_RDWR | O_NOCTTY);
    if (client_fd < 0) { perror("open slave"); return 1; }
    configure_raw(client_fd);

    // Send test frame
    uint8_t frame[] = {0x01, 0x02, 0x03, 0x04};
    printf("[CLIENT] Sending frame: 01 02 03 04\n");
    write(client_fd, frame, sizeof(frame));

    // Read response
    uint8_t response[BUF_SIZE];
    ssize_t n = read(client_fd, response, sizeof(response));
    if (n > 0) {
        printf("[CLIENT] Response (%zd bytes): ", n);
        for (ssize_t i = 0; i < n; i++) printf("%02X ", response[i]);
        printf("\n");
    }

    sleep(1);
    close(client_fd);
    close(master_fd);
    pthread_cancel(tid);
    pthread_join(tid, NULL);
    return 0;
}
```

### C++ Example: RAII Virtual Port Manager

```cpp
// VirtualSerialPort.hpp
#pragma once
#include <string>
#include <stdexcept>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

class VirtualSerialPort {
public:
    explicit VirtualSerialPort(speed_t baud = B115200) {
        master_fd_ = posix_openpt(O_RDWR | O_NOCTTY);
        if (master_fd_ < 0)
            throw std::runtime_error("posix_openpt failed");

        if (grantpt(master_fd_) < 0 || unlockpt(master_fd_) < 0)
            throw std::runtime_error("PTY setup failed");

        slave_path_ = ptsname(master_fd_);
        configure(master_fd_, baud);
    }

    ~VirtualSerialPort() {
        if (master_fd_ >= 0) close(master_fd_);
    }

    // Non-copyable, movable
    VirtualSerialPort(const VirtualSerialPort&) = delete;
    VirtualSerialPort& operator=(const VirtualSerialPort&) = delete;

    VirtualSerialPort(VirtualSerialPort&& other) noexcept
        : master_fd_(other.master_fd_), slave_path_(std::move(other.slave_path_)) {
        other.master_fd_ = -1;
    }

    [[nodiscard]] int masterFd() const { return master_fd_; }
    [[nodiscard]] const std::string& slavePath() const { return slave_path_; }

    ssize_t write(const void* data, size_t len) {
        return ::write(master_fd_, data, len);
    }

    ssize_t read(void* buf, size_t len) {
        return ::read(master_fd_, buf, len);
    }

private:
    int         master_fd_ = -1;
    std::string slave_path_;

    static void configure(int fd, speed_t baud) {
        struct termios tty{};
        cfmakeraw(&tty);
        cfsetispeed(&tty, baud);
        cfsetospeed(&tty, baud);
        tty.c_cflag &= ~static_cast<tcflag_t>(PARENB | CSTOPB | CSIZE | CRTSCTS);
        tty.c_cflag |= CS8 | CREAD | CLOCAL;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 1;
        tcsetattr(fd, TCSANOW, &tty);
    }
};
```

```cpp
// main.cpp — using the RAII wrapper
#include "VirtualSerialPort.hpp"
#include <iostream>
#include <vector>
#include <cstring>

int main() {
    try {
        VirtualSerialPort vsp(B115200);
        std::cout << "Slave device: " << vsp.slavePath() << "\n";

        // Spawn a thread that acts as the device simulator
        std::thread sim([&vsp]() {
            std::vector<uint8_t> buf(256);
            while (true) {
                ssize_t n = vsp.read(buf.data(), buf.size());
                if (n > 0) {
                    // Reverse the bytes as a dummy response
                    std::reverse(buf.begin(), buf.begin() + n);
                    vsp.write(buf.data(), n);
                }
            }
        });

        // Connect a client to the slave
        int client = open(vsp.slavePath().c_str(), O_RDWR | O_NOCTTY);
        uint8_t tx[] = {0xDE, 0xAD, 0xBE, 0xEF};
        ::write(client, tx, sizeof(tx));

        uint8_t rx[8]{};
        ssize_t n = ::read(client, rx, sizeof(rx));
        std::cout << "Response bytes: ";
        for (ssize_t i = 0; i < n; ++i)
            std::cout << std::hex << static_cast<int>(rx[i]) << " ";
        std::cout << "\n";

        close(client);
        sim.detach();
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
```

### C Example: Pair of Virtual Ports (Null-Modem Emulation)

For testing two applications that each need their own port:

```c
// null_modem.c — creates two linked virtual ports and bridges them
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

typedef struct { int src; int dst; const char *name; } Bridge;

void *bridge_thread(void *arg) {
    Bridge *b = (Bridge *)arg;
    uint8_t buf[512];
    ssize_t n;
    while ((n = read(b->src, buf, sizeof(buf))) > 0) {
        write(b->dst, buf, n);
    }
    return NULL;
}

int make_pty(char *name, size_t len) {
    int fd = posix_openpt(O_RDWR | O_NOCTTY);
    grantpt(fd); unlockpt(fd);
    snprintf(name, len, "%s", ptsname(fd));

    struct termios t;
    tcgetattr(fd, &t);
    cfmakeraw(&t);
    cfsetispeed(&t, B115200);
    cfsetospeed(&t, B115200);
    t.c_cflag |= CREAD | CLOCAL;
    tcsetattr(fd, TCSANOW, &t);
    return fd;
}

int main(void) {
    char name_a[64], name_b[64];
    int fd_a = make_pty(name_a, sizeof(name_a));
    int fd_b = make_pty(name_b, sizeof(name_b));

    printf("Port A slave: %s\n", name_a);
    printf("Port B slave: %s\n", name_b);
    printf("These ports are now null-modem linked.\n");

    // Bridge A->B and B->A
    Bridge ab = { fd_a, fd_b, "A->B" };
    Bridge ba = { fd_b, fd_a, "B->A" };

    pthread_t t1, t2;
    pthread_create(&t1, NULL, bridge_thread, &ab);
    pthread_create(&t2, NULL, bridge_thread, &ba);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    close(fd_a);
    close(fd_b);
    return 0;
}
```

---

## Programming Virtual Ports in Rust

Rust provides safe, ergonomic access to PTYs via the `nix` crate (POSIX bindings) and the `serialport` crate for cross-platform serial I/O.

### Cargo.toml

```toml
[package]
name = "virtual_serial"
version = "0.1.0"
edition = "2021"

[dependencies]
nix     = { version = "0.29", features = ["term", "ioctl", "fs"] }
serialport = "4"
```

### Rust Example: Creating a PTY Pair

```rust
// src/main.rs
use nix::fcntl::{open, OFlag};
use nix::pty::{grantpt, posix_openpt, ptsname, unlockpt};
use nix::sys::stat::Mode;
use nix::sys::termios::{
    cfsetispeed, cfsetospeed, tcgetattr, tcsetattr, BaudRate,
    LocalFlags, OutputFlags, InputFlags, ControlFlags,
    SetArg, SpecialCharacterIndices as SCI,
};
use std::io::{Read, Write};
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;

pub struct VirtualSerialPort {
    master: std::fs::File,
    slave_path: PathBuf,
}

impl VirtualSerialPort {
    /// Create a new virtual serial port at 115200 baud, 8N1.
    pub fn new() -> Result<Self, Box<dyn std::error::Error>> {
        // Open a PTY master
        let master = posix_openpt(OFlag::O_RDWR | OFlag::O_NOCTTY)?;
        grantpt(&master)?;
        unlockpt(&master)?;

        let slave_name = unsafe { ptsname(&master)? };
        let slave_path = PathBuf::from(&slave_name);

        // Configure master in raw mode, 115200 baud
        let fd = master.as_raw_fd();
        let mut tty = tcgetattr(fd)?;

        // Raw mode: clear all processing flags
        tty.input_flags &= !(InputFlags::IGNBRK
            | InputFlags::BRKINT
            | InputFlags::PARMRK
            | InputFlags::ISTRIP
            | InputFlags::INLCR
            | InputFlags::IGNCR
            | InputFlags::ICRNL
            | InputFlags::IXON);
        tty.output_flags &= !OutputFlags::OPOST;
        tty.local_flags &= !(LocalFlags::ECHO
            | LocalFlags::ECHONL
            | LocalFlags::ICANON
            | LocalFlags::ISIG
            | LocalFlags::IEXTEN);
        tty.control_flags &= !(ControlFlags::CSIZE | ControlFlags::PARENB);
        tty.control_flags |= ControlFlags::CS8 | ControlFlags::CREAD | ControlFlags::CLOCAL;

        // Non-blocking reads
        tty.control_chars[SCI::VMIN as usize] = 0;
        tty.control_chars[SCI::VTIME as usize] = 1; // 100ms timeout

        cfsetispeed(&mut tty, BaudRate::B115200)?;
        cfsetospeed(&mut tty, BaudRate::B115200)?;
        tcsetattr(fd, SetArg::TCSANOW, &tty)?;

        // Convert OwnedFd to File for convenient I/O
        let master_file = unsafe {
            use std::os::unix::io::FromRawFd;
            std::fs::File::from_raw_fd(master.into_raw_fd())
        };

        println!("Virtual serial port ready: {}", slave_name);

        Ok(VirtualSerialPort {
            master: master_file,
            slave_path,
        })
    }

    pub fn slave_path(&self) -> &std::path::Path {
        &self.slave_path
    }

    pub fn write_bytes(&mut self, data: &[u8]) -> std::io::Result<usize> {
        self.master.write(data)
    }

    pub fn read_bytes(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        self.master.read(buf)
    }
}
```

### Rust Example: Echo Simulator

```rust
// src/simulator.rs — runs a device simulator on the master side
use std::thread;
use std::io::{Read, Write};

pub fn run_echo_simulator(mut port: super::VirtualSerialPort) {
    println!("[SIM] Slave path: {}", port.slave_path().display());
    println!("[SIM] Connect your application to the path above.");

    thread::spawn(move || {
        let mut buf = vec![0u8; 256];
        loop {
            match port.read_bytes(&mut buf) {
                Ok(0) => continue,
                Ok(n) => {
                    let received = &buf[..n];
                    println!(
                        "[SIM] Received {} bytes: {:02X?}",
                        n, received
                    );
                    // Prepend 0xAA as response header
                    let mut response = Vec::with_capacity(n + 1);
                    response.push(0xAA_u8);
                    response.extend_from_slice(received);
                    if let Err(e) = port.write_bytes(&response) {
                        eprintln!("[SIM] Write error: {}", e);
                        break;
                    }
                }
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                Err(e) => {
                    eprintln!("[SIM] Read error: {}", e);
                    break;
                }
            }
        }
    });
}
```

### Rust Example: Client Using the `serialport` Crate

```rust
// src/client.rs — opens the slave as a standard serial port
use serialport::SerialPort;
use std::time::Duration;

pub fn run_client(slave_path: &str) -> Result<(), Box<dyn std::error::Error>> {
    let mut port = serialport::new(slave_path, 115_200)
        .timeout(Duration::from_millis(500))
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .open()?;

    println!("[CLIENT] Opened port: {}", slave_path);

    // Send a test frame
    let frame: &[u8] = &[0x01, 0x02, 0x03, 0x04];
    port.write_all(frame)?;
    println!("[CLIENT] Sent: {:02X?}", frame);

    // Read response (up to 32 bytes)
    let mut buf = vec![0u8; 32];
    let n = port.read(&mut buf)?;
    println!("[CLIENT] Response ({} bytes): {:02X?}", n, &buf[..n]);

    Ok(())
}
```

### Rust Example: Full Integration — Null-Modem Bridge

```rust
// src/bridge.rs — bridges two PTYs (null-modem in pure Rust)
use std::io::{Read, Write};
use std::thread;
use std::os::unix::io::{AsRawFd, FromRawFd};

fn make_pty() -> (std::fs::File, String) {
    use nix::pty::{grantpt, posix_openpt, ptsname, unlockpt};
    use nix::fcntl::OFlag;

    let master = posix_openpt(OFlag::O_RDWR | OFlag::O_NOCTTY).unwrap();
    grantpt(&master).unwrap();
    unlockpt(&master).unwrap();
    let slave = unsafe { ptsname(&master).unwrap() };
    let file = unsafe { std::fs::File::from_raw_fd(master.into_raw_fd()) };
    (file, slave)
}

pub fn run_bridge() {
    let (mut fd_a, name_a) = make_pty();
    let (mut fd_b, name_b) = make_pty();

    println!("Port A: {}", name_a);
    println!("Port B: {}", name_b);

    // Clone file descriptors for bidirectional bridging
    let mut fd_a2 = fd_a.try_clone().unwrap();
    let mut fd_b2 = fd_b.try_clone().unwrap();

    // A -> B
    thread::spawn(move || {
        let mut buf = [0u8; 512];
        loop {
            if let Ok(n) = fd_a.read(&mut buf) {
                if n > 0 { let _ = fd_b.write_all(&buf[..n]); }
            }
        }
    });

    // B -> A
    thread::spawn(move || {
        let mut buf = [0u8; 512];
        loop {
            if let Ok(n) = fd_b2.read(&mut buf) {
                if n > 0 { let _ = fd_a2.write_all(&buf[..n]); }
            }
        }
    });

    println!("Bridge running. Press Ctrl+C to stop.");
    loop { std::thread::sleep(std::time::Duration::from_secs(1)); }
}
```

---

## Loopback Testing

Loopback testing validates that a serial stack can send and receive data correctly. With a virtual port, the master fd is the "device" and the slave fd is the "DUT" (device under test):

### C Loopback Test

```c
// loopback_test.c — send data to slave, read it back from master
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>

int main(void) {
    char slave_name[64];
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    grantpt(master); unlockpt(master);
    snprintf(slave_name, sizeof(slave_name), "%s", ptsname(master));

    int slave = open(slave_name, O_RDWR | O_NOCTTY);
    assert(slave >= 0);

    // Write from slave (application side), read on master (device side)
    const char *msg = "LOOPBACK_TEST_123";
    write(slave, msg, strlen(msg));

    char buf[64] = {0};
    ssize_t n = read(master, buf, sizeof(buf) - 1);
    printf("Master received: '%.*s'\n", (int)n, buf);
    assert(strncmp(buf, msg, strlen(msg)) == 0);

    // Write response from master (device side), read on slave
    write(master, "ACK", 3);
    char ack[8] = {0};
    read(slave, ack, sizeof(ack));
    printf("Slave received: '%s'\n", ack);
    assert(strncmp(ack, "ACK", 3) == 0);

    printf("Loopback test PASSED.\n");
    close(slave);
    close(master);
    return 0;
}
```

### Rust Loopback Test

```rust
#[cfg(test)]
mod tests {
    use super::*;
    use std::io::{Read, Write};

    #[test]
    fn test_virtual_port_loopback() {
        let mut vsp = VirtualSerialPort::new().expect("create virtual port");
        let slave_path = vsp.slave_path().to_owned();

        // Open slave side
        let mut slave = serialport::new(slave_path.to_str().unwrap(), 115_200)
            .timeout(std::time::Duration::from_millis(200))
            .open()
            .expect("open slave");

        // Client writes, master reads
        let msg = b"RUST_LOOPBACK_TEST";
        slave.write_all(msg).unwrap();

        let mut buf = vec![0u8; 64];
        let n = vsp.read_bytes(&mut buf).unwrap();
        assert_eq!(&buf[..n], msg);

        // Master writes response, client reads
        vsp.write_bytes(b"OK").unwrap();
        let n = slave.read(&mut buf).unwrap();
        assert_eq!(&buf[..n], b"OK");

        println!("Loopback test passed.");
    }
}
```

---

## Windows: Virtual COM Ports

On Windows, software-based virtual COM port pairs are provided by third-party drivers. The most widely used open-source option is **com0com**. Commercial alternatives include **HHD Virtual Serial Port**, **ELTIMA Virtual Serial Port Driver**, and **VSPE**.

### com0com Setup

1. Download and install `com0com` from SourceForge.
2. Use the configuration tool to create a port pair, e.g. `COM10` ↔ `COM11`.
3. Both ports appear in Device Manager as standard COM ports.

### Windows C/C++ Access

On Windows, serial ports are accessed through Win32 file handles:

```c
// windows_serial.c
#include <windows.h>
#include <stdio.h>

HANDLE open_serial(const char *port_name, DWORD baud) {
    // On Windows, ports above COM9 need prefix \\.\
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", port_name);

    HANDLE hPort = CreateFileA(
        path,
        GENERIC_READ | GENERIC_WRITE,
        0,             // Exclusive access
        NULL,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        NULL
    );

    if (hPort == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s: error %lu\n", port_name, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Configure DCB (Device Control Block)
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);
    GetCommState(hPort, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.StopBits = ONESTOPBIT;
    dcb.Parity   = NOPARITY;
    SetCommState(hPort, &dcb);

    // Set timeouts
    COMMTIMEOUTS timeouts = {
        .ReadIntervalTimeout         = 50,
        .ReadTotalTimeoutConstant    = 100,
        .ReadTotalTimeoutMultiplier  = 10,
        .WriteTotalTimeoutConstant   = 100,
        .WriteTotalTimeoutMultiplier = 10,
    };
    SetCommTimeouts(hPort, &timeouts);

    return hPort;
}

int main(void) {
    HANDLE hA = open_serial("COM10", CBR_115200);
    HANDLE hB = open_serial("COM11", CBR_115200);

    if (hA == INVALID_HANDLE_VALUE || hB == INVALID_HANDLE_VALUE) return 1;

    const char *msg = "Hello from COM10";
    DWORD written;
    WriteFile(hA, msg, (DWORD)strlen(msg), &written, NULL);

    char buf[64] = {0};
    DWORD read_count;
    ReadFile(hB, buf, sizeof(buf) - 1, &read_count, NULL);
    printf("COM11 received: '%s'\n", buf);

    CloseHandle(hA);
    CloseHandle(hB);
    return 0;
}
```

### Rust on Windows: `serialport` Crate

The `serialport` crate abstracts over both POSIX PTYs and Windows COM ports:

```rust
// Works on both Linux (/dev/pts/N) and Windows (COM10)
fn open_port(path: &str) -> Box<dyn serialport::SerialPort> {
    serialport::new(path, 115_200)
        .timeout(std::time::Duration::from_millis(200))
        .open()
        .expect("Failed to open serial port")
}
```

---

## Cross-Platform Considerations

| Feature | Linux | macOS | Windows |
|---|---|---|---|
| PTY API | `posix_openpt` (glibc) | `posix_openpt` (libc) | Not available |
| Virtual COM pairs | `socat` / manual PTY | `socat` / `tty` util | `com0com`, VSPE |
| Rust crate | `nix` + `serialport` | `nix` + `serialport` | `serialport` |
| Device path | `/dev/pts/N` | `/dev/ttys000` | `\\.\COMN` |
| Path > COM9 prefix | N/A | N/A | `\\\\.\\` required |
| Baud rate emulation | `termios` | `termios` | DCB settings |

### Portable Rust Pattern

```rust
fn virtual_port_path() -> String {
    #[cfg(unix)]
    {
        // Create PTY and return slave path (Linux/macOS)
        let vsp = VirtualSerialPort::new().unwrap();
        vsp.slave_path().to_string_lossy().into_owned()
    }
    #[cfg(windows)]
    {
        // Assume com0com is installed; return one of the pair
        String::from("COM10")
    }
}
```

---

## Advanced Patterns

### Pattern 1: Protocol Injector

Intercept and modify traffic between a real application and a virtual device:

```c
// injector.c — sits between two PTYs, modifying bytes in transit
void inject_loop(int from_fd, int to_fd, uint8_t (*transform)(uint8_t)) {
    uint8_t buf[512];
    ssize_t n;
    while ((n = read(from_fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; i++)
            buf[i] = transform(buf[i]);
        write(to_fd, buf, n);
    }
}

// Example: invert all bits to simulate a noisy channel
uint8_t invert(uint8_t b) { return ~b; }
```

### Pattern 2: Traffic Logger in Rust

```rust
use std::io::{Read, Write};
use std::fs::File;

pub struct LoggingBridge {
    log: File,
}

impl LoggingBridge {
    pub fn relay(&mut self, src: &mut dyn Read, dst: &mut dyn Write) {
        let mut buf = vec![0u8; 512];
        while let Ok(n) = src.read(&mut buf) {
            if n == 0 { break; }
            let _ = dst.write_all(&buf[..n]);
            // Log with timestamp
            let ts = std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_millis();
            let line = format!("[{}] TX {:02X?}\n", ts, &buf[..n]);
            let _ = self.log.write_all(line.as_bytes());
        }
    }
}
```

### Pattern 3: Baud Rate Throttling

Simulate a real serial port's throughput limit in software:

```c
#include <time.h>

void throttled_write(int fd, const uint8_t *data, size_t len, int baud) {
    // bits per byte = 10 (start + 8 data + stop)
    double bytes_per_sec = baud / 10.0;
    double delay_ns_per_byte = 1e9 / bytes_per_sec;

    struct timespec ts = {
        .tv_sec  = 0,
        .tv_nsec = (long)delay_ns_per_byte,
    };

    for (size_t i = 0; i < len; i++) {
        write(fd, &data[i], 1);
        nanosleep(&ts, NULL);
    }
}
```

### Pattern 4: pytest-based Serial Testing in Python (calling C/Rust backend)

Virtual serial ports pair especially well with automated test harnesses:

```python
# test_serial_protocol.py
import subprocess, serial, time, pytest

@pytest.fixture(scope="session")
def virtual_port():
    """Start the Rust/C simulator binary, capture its slave path."""
    proc = subprocess.Popen(
        ["./vsim"],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
    )
    # Parse "Virtual serial port: /dev/pts/5" from stdout
    for line in proc.stdout:
        if "Virtual serial port:" in line:
            path = line.strip().split()[-1]
            yield path
            proc.terminate()
            return

def test_echo_response(virtual_port):
    port = serial.Serial(virtual_port, 115200, timeout=0.5)
    port.write(bytes([0x01, 0x02, 0x03, 0x04]))
    response = port.read(5)
    assert response[0] == 0xAA          # Header
    assert response[1:] == bytes([0x01, 0x02, 0x03, 0x04])  # Echo
    port.close()
```

---

## Summary

Serial Port Virtualization is an essential technique for developing and testing UART-based systems without physical hardware. The key concepts and takeaways are:

**Core Technology (Linux)**
- The **PTY subsystem** (`posix_openpt` / `grantpt` / `unlockpt` / `ptsname`) creates a master/slave fd pair that the OS presents as a real TTY device.
- `termios` is used to configure raw mode, baud rate, parity, and stop bits exactly as with physical ports.
- The slave device path (`/dev/pts/N`) can be opened by any application expecting a real serial port.

**Tools**
- `socat` is the fastest path to a null-modem virtual port pair for shell-level testing.
- `com0com` (Windows) provides the equivalent virtual COM port pair at the driver level.

**C/C++ Programming**
- Use `posix_openpt` + `cfmakeraw` + `cfsetispeed` for raw PTY creation and configuration.
- RAII wrappers (C++ classes) encapsulate the fd lifecycle cleanly.
- Two PTY masters bridged with threads emulate a full null-modem cable.

**Rust Programming**
- The `nix` crate exposes all POSIX PTY APIs with safe wrappers.
- The `serialport` crate provides a unified cross-platform API (`serialport::new(path, baud).open()`), abstracting over PTYs on Linux/macOS and COM ports on Windows.
- Rust's ownership model ensures master fds are closed properly with `Drop`.

**Testing Patterns**
- Loopback tests (write to slave, read from master) validate codec and framing logic without hardware.
- Protocol injectors (sitting between two PTYs) enable fault injection and noise simulation.
- Traffic loggers capture timestamped hex dumps for debugging.
- pytest fixtures can launch a Rust/C simulator process and extract the slave path for Python-level integration tests.

**Cross-Platform Note**
- The PTY API is POSIX; no direct equivalent exists on Windows. Use `serialport` with `com0com` or a similar virtual COM driver to maintain portable test code.

Virtual serial ports eliminate hardware dependencies from the development cycle, enabling robust, automated, and repeatable testing of UART protocols from day one.

---

*Document: 60_Serial_Port_Virtualization.md | UART Series*