# 90. Remote Serial Console — Accessing UART over Network using `ser2net`

**Architecture & Setup** — How `ser2net` sits between a physical UART (`/dev/ttyUSB0`) and a TCP port, with a clear diagram of the full signal path from target device to developer workstation.

**ser2net Configuration** — Both the modern YAML format (v4+) and the legacy colon-delimited format, with annotated parameter explanations.

**C/C++ Code Examples:**
- `uart_open()` — full `termios` configuration for raw 8N1 mode at any standard baud rate
- `uart_tcp_server.c` — a minimal `ser2net` clone using `select()` for bidirectional bridging
- `uart_tcp_client.c` — an interactive terminal client with raw stdin mode

**Rust Code Examples:**
- `open_uart()` — using the `serialport` crate with proper builder pattern
- Async TCP-to-UART bridge using Tokio with `spawn_blocking` for the serial side
- Async TCP client using `tokio::select!` for concurrent stdin/stdout handling

**Security** — SSH tunnelling, firewall rules, ser2net TLS/certauth, and practical hardening tips.

**Troubleshooting table** — covering the most common failure modes (permissions, baud mismatch, device busy, etc.).

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Architecture Overview](#2-architecture-overview)
3. [Hardware Setup](#3-hardware-setup)
4. [ser2net: The Bridge Daemon](#4-ser2net-the-bridge-daemon)
   - 4.1 [Installation](#41-installation)
   - 4.2 [Configuration File](#42-configuration-file)
   - 4.3 [Running ser2net](#43-running-ser2net)
5. [Client-Side Access](#5-client-side-access)
   - 5.1 [telnet](#51-telnet)
   - 5.2 [netcat (nc)](#52-netcat-nc)
   - 5.3 [minicom / screen](#53-minicom--screen)
6. [Programming: C/C++ Implementation](#6-programming-cc-implementation)
   - 6.1 [UART Configuration on Linux (termios)](#61-uart-configuration-on-linux-termios)
   - 6.2 [TCP Server Forwarding UART](#62-tcp-server-forwarding-uart)
   - 6.3 [TCP Client Reading Remote UART](#63-tcp-client-reading-remote-uart)
7. [Programming: Rust Implementation](#7-programming-rust-implementation)
   - 7.1 [UART Configuration with `serialport` crate](#71-uart-configuration-with-serialport-crate)
   - 7.2 [Async TCP-to-UART Bridge (Tokio)](#72-async-tcp-to-uart-bridge-tokio)
   - 7.3 [Rust TCP Client for Remote Console](#73-rust-tcp-client-for-remote-console)
8. [Security Considerations](#8-security-considerations)
9. [Troubleshooting](#9-troubleshooting)
10. [Summary](#10-summary)

---

## 1. Introduction

A **Remote Serial Console** (also called a *serial console server* or *terminal server*) allows engineers and system administrators to access a device's UART (Universal Asynchronous Receiver/Transmitter) debug or management console over a TCP/IP network, without requiring physical access to the serial port.

This is invaluable in embedded systems development and production deployments where:

- The target device is in a remote location (data centre, field deployment, another building).
- Multiple developers need simultaneous or shared access to a device's console.
- Automated CI/CD pipelines need to read boot logs or inject commands.
- A headless server or embedded Linux device must be administered when SSH is unavailable (e.g. during boot, kernel panic, or network misconfiguration).

The standard open-source tool for this on Linux is **`ser2net`**, a daemon that maps one or more serial ports to TCP or telnet ports, making them accessible over a network.

---

## 2. Architecture Overview

```
┌─────────────────────────────────┐          Network (TCP/IP)
│  Embedded / Remote Host         │◄────────────────────────────────────►
│                                 │                                       │
│  /dev/ttyS0  or  /dev/ttyUSB0   │                               ┌───────────────┐
│        │                        │                               │  Developer    │
│     ser2net daemon              │                               │  Workstation  │
│     (listens on TCP :2001)      │                               │               │
│        │                        │                               │  telnet / nc  │
│  ──────┴──────── TCP:2001 ──────┼───────────────────────────────┤  minicom      │
│                                 │                               │  custom app   │
└─────────────────────────────────┘                               └───────────────┘

Physical UART:
  Target Device ──── TX/RX/GND ──── USB-to-UART adapter ──── Remote Host /dev/ttyUSB0
```

The remote host (e.g. a Raspberry Pi, a Linux server, or any system with a USB-to-UART adapter) runs `ser2net`. Any TCP client connecting to the advertised port gets a bidirectional byte stream that is transparently forwarded to/from the physical UART.

---

## 3. Hardware Setup

For a typical embedded Linux target:

| Component | Example |
|---|---|
| Remote host | Raspberry Pi 4 / PC with Linux |
| USB-to-UART adapter | CP2102, CH340, FTDI FT232 |
| Target device | Any embedded board with a debug UART |
| Connection | TX→RX, RX→TX, GND→GND (3.3 V logic, **never 5 V to 3.3 V**) |

After plugging in the USB adapter, verify the device node:

```bash
dmesg | grep tty
ls -l /dev/ttyUSB*
# Typical output: /dev/ttyUSB0
```

Set proper permissions (or add your user to the `dialout` group):

```bash
sudo usermod -aG dialout $USER
# Re-login for the change to take effect
```

---

## 4. ser2net: The Bridge Daemon

### 4.1 Installation

```bash
# Debian / Ubuntu / Raspberry Pi OS
sudo apt update && sudo apt install ser2net

# Fedora / RHEL / Rocky Linux
sudo dnf install ser2net

# From source (latest version with YAML config support)
git clone https://github.com/cminyard/ser2net.git
cd ser2net
./configure && make && sudo make install
```

Check the installed version — versions ≥ 4.0 use a YAML-based configuration, while older versions use a legacy colon-delimited format.

```bash
ser2net -v
```

### 4.2 Configuration File

#### Modern YAML format (`/etc/ser2net.yaml`) — ser2net ≥ 4.0

```yaml
# /etc/ser2net.yaml

# Define a connection named "uart0"
connection: &uart0
  accepter: tcp,2001          # Listen on TCP port 2001
  connector: serialdev,/dev/ttyUSB0,115200n81,local
  options:
    kickolduser: true          # Disconnect old client when new one connects
    telnet-brk-on-sync: true

# Second UART on a different port
connection: &uart1
  accepter: tcp,2002
  connector: serialdev,/dev/ttyS0,9600n81,local
```

**Key connector parameters:**

| Field | Meaning |
|---|---|
| `/dev/ttyUSB0` | Serial device node |
| `115200` | Baud rate |
| `n` | No parity |
| `8` | 8 data bits |
| `1` | 1 stop bit |
| `local` | Ignore modem control lines (DCD/DTR) |

#### Legacy format (`/etc/ser2net.conf`) — ser2net < 4.0

```
# port:state:timeout:device:options
2001:telnet:0:/dev/ttyUSB0:115200 8DATABITS NONE 1STOPBIT banner
2002:raw:0:/dev/ttyS0:9600 8DATABITS NONE 1STOPBIT
```

- `telnet` — uses Telnet protocol (negotiates terminal options).
- `raw` — plain TCP byte stream; preferred for programmatic access.
- `0` — no timeout (connection stays open indefinitely).

### 4.3 Running ser2net

```bash
# Start manually for testing
sudo ser2net -c /etc/ser2net.yaml -n   # -n = foreground, no daemonize

# Enable as a systemd service
sudo systemctl enable --now ser2net
sudo systemctl status ser2net

# View live logs
journalctl -u ser2net -f
```

---

## 5. Client-Side Access

### 5.1 telnet

```bash
telnet 192.168.1.50 2001
```

Press `Ctrl+]` then type `quit` to exit the telnet session cleanly.

### 5.2 netcat (nc)

```bash
nc 192.168.1.50 2001
```

Useful for scripted access. Pipe commands in or capture output to a file:

```bash
# Log boot output to file for 30 seconds
nc -w 30 192.168.1.50 2001 > boot_log.txt
```

### 5.3 minicom / screen

```bash
# Using screen as a remote serial terminal
# (ser2net configured in raw mode)
screen /dev/tcp/192.168.1.50/2001   # Not directly supported in all shells

# Better: use socat to create a virtual tty backed by TCP, then attach minicom
socat PTY,link=/tmp/vserial,raw,echo=0 TCP:192.168.1.50:2001 &
minicom -D /tmp/vserial -b 115200
```

---

## 6. Programming: C/C++ Implementation

### 6.1 UART Configuration on Linux (termios)

The `termios` API is the POSIX standard for configuring serial ports on Linux/Unix.

```c
// uart_config.c
// Demonstrates opening and configuring a UART port in raw mode at 115200 baud.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

// Returns an open, configured file descriptor, or -1 on error.
int uart_open(const char *device, int baud_rate)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("uart_open: open");
        return -1;
    }

    // Restore blocking I/O
    fcntl(fd, F_SETFL, 0);

    struct termios options;
    if (tcgetattr(fd, &options) != 0) {
        perror("uart_open: tcgetattr");
        close(fd);
        return -1;
    }

    // Map the integer baud rate to a termios constant
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

    // 8N1, raw mode
    options.c_cflag &= ~PARENB;          // No parity
    options.c_cflag &= ~CSTOPB;          // 1 stop bit
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;              // 8 data bits
    options.c_cflag &= ~CRTSCTS;         // No hardware flow control
    options.c_cflag |= CREAD | CLOCAL;   // Enable receiver, ignore modem lines

    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  // Raw input
    options.c_iflag &= ~(IXON | IXOFF | IXANY);          // No software flow ctrl
    options.c_iflag &= ~(ICRNL | INLCR);                 // Do not translate CR/NL
    options.c_oflag &= ~OPOST;           // Raw output

    // Read returns after at least 1 byte, with no timeout
    options.c_cc[VMIN]  = 1;
    options.c_cc[VTIME] = 0;

    tcflush(fd, TCIFLUSH);
    if (tcsetattr(fd, TCSANOW, &options) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

int main(void)
{
    int fd = uart_open("/dev/ttyUSB0", 115200);
    if (fd < 0) return EXIT_FAILURE;

    printf("UART opened successfully. Reading...\n");

    char buf[256];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        fputs(buf, stdout);
        fflush(stdout);
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

### 6.2 TCP Server Forwarding UART

This example implements a minimal serial-to-network bridge in C — essentially a simplified `ser2net` — using `select()` for bidirectional forwarding.

```c
// uart_tcp_server.c
// Opens /dev/ttyUSB0 and listens on TCP:2001.
// Any connected TCP client gets a bidirectional pipe to the UART.
//
// Compile: gcc -o uart_tcp_server uart_tcp_server.c
// Usage:   sudo ./uart_tcp_server /dev/ttyUSB0 115200 2001

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUF_SIZE 4096

// (uart_open() as shown in section 6.1 — omitted for brevity)
extern int uart_open(const char *device, int baud_rate);

// Create a TCP server socket listening on 0.0.0.0:port
static int tcp_listen(int port)
{
    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { perror("socket"); return -1; }

    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons((uint16_t)port),
    };

    if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(srv); return -1;
    }
    if (listen(srv, 1) < 0) {
        perror("listen"); close(srv); return -1;
    }
    return srv;
}

// Bridge data between uart_fd and client_fd using select()
static void bridge(int uart_fd, int client_fd)
{
    char buf[BUF_SIZE];
    fd_set rfds;
    int maxfd = (uart_fd > client_fd ? uart_fd : client_fd) + 1;

    printf("Bridging started (uart=%d, client=%d)\n", uart_fd, client_fd);

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(uart_fd, &rfds);
        FD_SET(client_fd, &rfds);

        int ret = select(maxfd, &rfds, NULL, NULL, NULL);
        if (ret < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        // Data from UART → send to TCP client
        if (FD_ISSET(uart_fd, &rfds)) {
            ssize_t n = read(uart_fd, buf, sizeof(buf));
            if (n <= 0) { fprintf(stderr, "UART read error\n"); break; }
            if (write(client_fd, buf, (size_t)n) != n) {
                fprintf(stderr, "TCP write error\n"); break;
            }
        }

        // Data from TCP client → write to UART
        if (FD_ISSET(client_fd, &rfds)) {
            ssize_t n = read(client_fd, buf, sizeof(buf));
            if (n <= 0) { printf("Client disconnected\n"); break; }
            if (write(uart_fd, buf, (size_t)n) != n) {
                fprintf(stderr, "UART write error\n"); break;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <device> <baud> <tcp_port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *device = argv[1];
    int baud      = atoi(argv[2]);
    int tcp_port  = atoi(argv[3]);

    int uart_fd = uart_open(device, baud);
    if (uart_fd < 0) return EXIT_FAILURE;

    int srv_fd = tcp_listen(tcp_port);
    if (srv_fd < 0) { close(uart_fd); return EXIT_FAILURE; }

    printf("Listening on TCP port %d, forwarding to %s @ %d baud\n",
           tcp_port, device, baud);

    // Accept one client at a time (extend with fork/threads for multiple)
    for (;;) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
        int client_fd = accept(srv_fd, (struct sockaddr *)&client_addr, &addr_len);
        if (client_fd < 0) { perror("accept"); continue; }

        printf("Client connected: %s\n", inet_ntoa(client_addr.sin_addr));
        bridge(uart_fd, client_fd);
        close(client_fd);
    }

    close(srv_fd);
    close(uart_fd);
    return EXIT_SUCCESS;
}
```

### 6.3 TCP Client Reading Remote UART

```c
// uart_tcp_client.c
// Connects to a remote ser2net (or custom bridge) and prints received data.
// Also reads stdin and forwards it to the remote UART.
//
// Compile: gcc -o uart_tcp_client uart_tcp_client.c
// Usage:   ./uart_tcp_client 192.168.1.50 2001

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h>

#define BUF_SIZE 4096

static int tcp_connect(const char *host, int port)
{
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return -1; }

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port   = htons((uint16_t)port),
    };
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid host address: %s\n", host);
        close(sock);
        return -1;
    }
    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    return sock;
}

// Put stdin into raw mode so individual keypresses are forwarded immediately
static struct termios saved_termios;

static void set_raw_stdin(void)
{
    struct termios raw;
    tcgetattr(STDIN_FILENO, &saved_termios);
    raw = saved_termios;
    cfmakeraw(&raw);
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
}

static void restore_stdin(void)
{
    tcsetattr(STDIN_FILENO, TCSANOW, &saved_termios);
}

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sock = tcp_connect(argv[1], atoi(argv[2]));
    if (sock < 0) return EXIT_FAILURE;

    printf("Connected to %s:%s — press Ctrl+C to exit\n\n", argv[1], argv[2]);
    set_raw_stdin();
    atexit(restore_stdin);

    char buf[BUF_SIZE];
    fd_set rfds;

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        FD_SET(sock, &rfds);

        int ret = select(sock + 1, &rfds, NULL, NULL, NULL);
        if (ret < 0) { perror("select"); break; }

        // Remote UART data → local stdout
        if (FD_ISSET(sock, &rfds)) {
            ssize_t n = read(sock, buf, sizeof(buf));
            if (n <= 0) { printf("\nConnection closed by remote.\n"); break; }
            write(STDOUT_FILENO, buf, (size_t)n);
        }

        // Local keystroke → remote UART
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            write(sock, buf, (size_t)n);
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

## 7. Programming: Rust Implementation

### 7.1 UART Configuration with `serialport` crate

Add to `Cargo.toml`:

```toml
[dependencies]
serialport = "4"
tokio     = { version = "1", features = ["full"] }
```

```rust
// src/uart_config.rs
// Demonstrates basic UART open and read loop using the `serialport` crate.

use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};
use std::time::Duration;
use std::io::{self, Read};

pub fn open_uart(
    device: &str,
    baud_rate: u32,
) -> Result<Box<dyn SerialPort>, serialport::Error> {
    serialport::new(device, baud_rate)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(100))
        .open()
}

fn main() -> anyhow::Result<()> {
    let mut port = open_uart("/dev/ttyUSB0", 115_200)?;
    println!("UART opened. Reading...");

    let mut buf = [0u8; 256];
    loop {
        match port.read(&mut buf) {
            Ok(0) => {}
            Ok(n) => {
                let s = String::from_utf8_lossy(&buf[..n]);
                print!("{}", s);
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {}
            Err(e) => return Err(e.into()),
        }
    }
}
```

### 7.2 Async TCP-to-UART Bridge (Tokio)

This is a production-quality async bridge using Tokio — equivalent to `ser2net` in a single Rust binary.

```rust
// src/bridge_server.rs
// Async TCP server that bridges /dev/ttyUSB0 to TCP port 2001.
//
// Cargo.toml dependencies:
//   tokio    = { version = "1", features = ["full"] }
//   serialport = "4"
//   anyhow   = "1"

use anyhow::Result;
use serialport::SerialPort;
use std::io::{Read, Write};
use std::net::SocketAddr;
use std::sync::{Arc, Mutex};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};

const UART_DEVICE: &str = "/dev/ttyUSB0";
const BAUD_RATE: u32 = 115_200;
const TCP_ADDR: &str = "0.0.0.0:2001";
const BUF_SIZE: usize = 4096;

/// Shared, mutex-guarded UART handle (serialport is not Send)
type SharedUart = Arc<Mutex<Box<dyn SerialPort>>>;

async fn handle_client(stream: TcpStream, uart: SharedUart) -> Result<()> {
    let peer = stream.peer_addr()?;
    println!("Client connected: {}", peer);

    let (mut tcp_reader, mut tcp_writer) = stream.into_split();

    // Spawn a task: UART → TCP
    let uart_to_tcp = {
        let uart = Arc::clone(&uart);
        tokio::task::spawn_blocking(move || -> Result<()> {
            let mut buf = [0u8; BUF_SIZE];
            // We create a channel to pass data to the async side
            // For simplicity this example uses a blocking read loop
            // with a short timeout and writes to a shared Vec.
            // A production implementation would use a tokio::sync::mpsc channel.
            loop {
                let n = {
                    let mut port = uart.lock().unwrap();
                    match port.read(&mut buf) {
                        Ok(n) => n,
                        Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => 0,
                        Err(e) => return Err(e.into()),
                    }
                };
                if n > 0 {
                    // In a real impl, send buf[..n] over an mpsc channel to the async writer.
                    // Simplified: just print for demonstration purposes.
                    println!("[UART→TCP] {} bytes", n);
                }
            }
        })
    };

    // TCP → UART (async read, blocking write)
    let mut tcp_buf = [0u8; BUF_SIZE];
    loop {
        let n = tcp_reader.read(&mut tcp_buf).await?;
        if n == 0 {
            println!("Client {} disconnected", peer);
            break;
        }
        let chunk = tcp_buf[..n].to_vec();
        let uart = Arc::clone(&uart);
        tokio::task::spawn_blocking(move || {
            let mut port = uart.lock().unwrap();
            port.write_all(&chunk).ok();
        })
        .await?;
    }

    uart_to_tcp.abort();
    Ok(())
}

#[tokio::main]
async fn main() -> Result<()> {
    // Open UART
    let port = serialport::new(UART_DEVICE, BAUD_RATE)
        .timeout(std::time::Duration::from_millis(50))
        .open()?;
    let uart: SharedUart = Arc::new(Mutex::new(port));

    // Start TCP listener
    let listener = TcpListener::bind(TCP_ADDR).await?;
    println!("Listening on {} — bridging {}", TCP_ADDR, UART_DEVICE);

    loop {
        let (stream, _addr) = listener.accept().await?;
        let uart = Arc::clone(&uart);
        tokio::spawn(async move {
            if let Err(e) = handle_client(stream, uart).await {
                eprintln!("Client error: {}", e);
            }
        });
    }
}
```

> **Note:** For a fully production-ready bridge, use `tokio::sync::mpsc` channels to decouple the blocking UART read thread from the async TCP write task. The pattern above demonstrates the architectural scaffolding.

### 7.3 Rust TCP Client for Remote Console

```rust
// src/tcp_client.rs
// Connects to a remote ser2net instance and provides an interactive terminal.
// Cargo.toml: tokio = { version = "1", features = ["full"] }

use std::io::{self, Write};
use tokio::io::{AsyncReadExt, AsyncWriteExt, stdin, stdout};
use tokio::net::TcpStream;

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let args: Vec<String> = std::env::args().collect();
    if args.len() != 3 {
        eprintln!("Usage: {} <host> <port>", args[0]);
        std::process::exit(1);
    }

    let addr = format!("{}:{}", args[1], args[2]);
    let stream = TcpStream::connect(&addr).await?;
    println!("Connected to {} — Ctrl+C to exit\n", addr);

    let (mut tcp_rx, mut tcp_tx) = stream.into_split();

    // Task 1: Remote UART → local stdout
    let recv_task = tokio::spawn(async move {
        let mut buf = [0u8; 4096];
        let mut out = stdout();
        loop {
            match tcp_rx.read(&mut buf).await {
                Ok(0) => {
                    eprintln!("\nConnection closed by remote.");
                    break;
                }
                Ok(n) => {
                    out.write_all(&buf[..n]).await.ok();
                    out.flush().await.ok();
                }
                Err(e) => {
                    eprintln!("Read error: {}", e);
                    break;
                }
            }
        }
    });

    // Task 2: Local stdin → remote UART
    let send_task = tokio::spawn(async move {
        let mut buf = [0u8; 256];
        let mut inp = stdin();
        loop {
            match inp.read(&mut buf).await {
                Ok(0) | Err(_) => break,
                Ok(n) => {
                    if tcp_tx.write_all(&buf[..n]).await.is_err() {
                        break;
                    }
                }
            }
        }
    });

    // Wait for either task to finish
    tokio::select! {
        _ = recv_task => {},
        _ = send_task => {},
    }

    Ok(())
}
```

---

## 8. Security Considerations

Exposing a UART console over a network carries significant security implications. Apply the following measures in any non-isolated network:

### 8.1 Restrict Access with a Firewall

```bash
# Allow only a specific management subnet to reach ser2net ports
sudo ufw allow from 192.168.1.0/24 to any port 2001
sudo ufw deny 2001
```

### 8.2 Wrap with SSH Port Forwarding

Never expose raw `ser2net` ports on a public network. Tunnel through SSH instead:

```bash
# On the developer workstation:
# Forward local port 2001 to the remote host's localhost:2001 over SSH
ssh -L 2001:localhost:2001 user@remote-host

# Then, in another terminal, connect to the forwarded local port
telnet localhost 2001
```

### 8.3 ser2net Authentication / TLS (v4+)

ser2net 4.x supports `certauth` and SSL/TLS via `gensio`:

```yaml
# Wrap the TCP accepter with SSL
connection: &uart0_tls
  accepter: ssl(key=/etc/ser2net/server.key,cert=/etc/ser2net/server.crt),tcp,2001
  connector: serialdev,/dev/ttyUSB0,115200n81,local
```

### 8.4 Additional Hardening

- Run `ser2net` as a non-root user with only group `dialout` access.
- Use `kickolduser: true` to prevent stale sessions from blocking legitimate access.
- Enable logging to audit who accessed the console and when.
- Consider VPN (WireGuard, OpenVPN) as the outer security perimeter.

---

## 9. Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `open: Permission denied` on `/dev/ttyUSB*` | User not in `dialout` group | `sudo usermod -aG dialout $USER` |
| No data received from UART | Crossed TX/RX wires | Swap TX↔RX on the cable |
| Garbled characters | Baud rate mismatch | Match baud rate on both ends |
| Connection refused on TCP port | ser2net not running or firewall blocking | `systemctl status ser2net`, check `ufw` |
| Client connects but no output | Wrong device node configured | `ls /dev/ttyUSB*`, verify in `ser2net.yaml` |
| `Device busy` error | Another process (e.g. ModemManager) holds the port | `sudo systemctl stop ModemManager` |
| Rust `serialport::open` fails | Device permissions or `serialport` crate version | Check udev rules, update crate |

---

## 10. Summary

A **Remote Serial Console** solves the fundamental challenge of accessing a device's UART debug port without physical presence. The workflow is:

1. **Hardware:** Connect the target's UART (TX/RX/GND) to a Linux host via a USB-to-UART adapter. The adapter appears as `/dev/ttyUSB0` (or similar).

2. **ser2net daemon:** Configured via `/etc/ser2net.yaml`, it listens on a TCP port and transparently forwards all bytes between the TCP socket and the physical serial port. It supports raw TCP and Telnet modes, baud rate configuration, connection management, and optionally TLS.

3. **Client access:** Any tool that speaks TCP can access the console — `telnet`, `nc`, `socat` + `minicom`, or a custom application.

4. **C/C++ programming:** Use the POSIX `termios` API (`tcgetattr`/`tcsetattr`, `cfsetispeed`, raw mode flags) to configure the UART, and standard BSD socket APIs with `select()` for bidirectional non-blocking forwarding. This is exactly what `ser2net` does internally.

5. **Rust programming:** The `serialport` crate provides a safe, cross-platform abstraction over serial ports. Combined with Tokio's async runtime, a fully asynchronous TCP-to-UART bridge can be written in clean, memory-safe Rust code, with `spawn_blocking` handling the inherently synchronous serial I/O.

6. **Security:** Raw TCP exposure is acceptable only on isolated lab networks. For any production or internet-facing use, apply SSH tunnelling, firewall rules, or TLS with certificate authentication.

The Remote Serial Console pattern is indispensable in embedded development, production system administration, CI/CD pipeline automation, and any scenario where physical access to a device's debug port is impractical.

---

*Document generated for UART programming series — Topic 90.*