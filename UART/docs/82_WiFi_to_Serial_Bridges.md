# 82. WiFi-to-Serial Bridges — ESP8266/ESP32 as Wireless UART Bridge

**Architecture** — ASCII diagrams for both the data-flow topology and the TCP connection state machine, plus a comparison table of all five operating modes (TCP Server/Client, UDP, WebSocket, MQTT).

**C/C++ Firmware (ESP side)**
- Single-client TCP server loop (Arduino, works on both ESP8266 and ESP32 with `#ifdef`)
- Multi-client FreeRTOS task-per-connection bridge (ESP32)
- UDP bridge for low-latency telemetry

**C/C++ Host Client**
- POSIX `select()`-based bidirectional client with `TCP_NODELAY`
- A reusable `FramedSerialClient` class using 2-byte length-prefixed framing

**Rust Host Client**
- Synchronous `std::net` client with two threads (RX/TX) and `Arc<Mutex<TcpStream>>`
- Async `tokio` client with a custom `tokio-util::Decoder/Encoder` for length-prefixed framing and automatic reconnection
- Virtual serial port via PTY + `serialport` crate (for apps expecting `/dev/ttyXXX`)

**Advanced Topics** — autobaud detection via GPIO interrupt timing, lock-free SPSC ring buffer for ISR-to-task transfer, and mDNS/Bonjour advertisement.

**Reliability & Security** — watchdog timer, TCP keep-alive tuning, constant-time token authentication, and a threat mitigation table.

**Diagnostics** — `nc`/`socat` probing, Python RTT latency measurement script, and an HTTP JSON status endpoint on the ESP.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Architecture Overview](#2-architecture-overview)
3. [Hardware Setup](#3-hardware-setup)
4. [Protocol Stack](#4-protocol-stack)
5. [Firmware: ESP8266/ESP32 Bridge in C/C++](#5-firmware-esp8266esp32-bridge-in-cc)
6. [Host-Side Client in C/C++](#6-host-side-client-in-cc)
7. [Host-Side Client in Rust](#7-host-side-client-in-rust)
8. [Advanced Topics](#8-advanced-topics)
9. [Error Handling and Reliability](#9-error-handling-and-reliability)
10. [Security Considerations](#10-security-considerations)
11. [Debugging and Diagnostics](#11-debugging-and-diagnostics)
12. [Summary](#12-summary)

---

## 1. Introduction

A **WiFi-to-Serial bridge** (also called a wireless UART bridge or serial-over-WiFi adapter) is a device that transparently relays data between a physical UART interface and a TCP/IP network over WiFi.  It allows legacy serial devices — sensors, PLCs, GPS modules, microcontrollers, industrial instruments — to be accessed, monitored, and controlled over a LAN or the internet without any modification to their firmware or hardware.

The **ESP8266** and **ESP32** are ideal candidates for this role:

| Feature | ESP8266 | ESP32 |
|---|---|---|
| WiFi | 802.11 b/g/n | 802.11 b/g/n |
| CPU | 80/160 MHz single-core | 240 MHz dual-core |
| UART ports | 2 (1 usable) | 3 |
| RAM | ~80 KB heap | ~320 KB heap |
| Bluetooth | No | Yes (BLE + Classic) |
| Cost | ~$1–2 | ~$3–5 |

### Primary Use Cases

- Remote access to serial debug consoles
- Industrial sensor data aggregation over WiFi
- Wireless firmware flashing of remote targets
- SCADA system integration for legacy RS-232/RS-485 devices
- Peer-to-peer wireless communication between embedded nodes

---

## 2. Architecture Overview

```
  ┌─────────────────────────────────────────────────────────┐
  │                   WiFi Network (LAN/WAN)                │
  │                                                         │
  │   ┌──────────────┐  TCP/UDP  ┌──────────────────────┐  │
  │   │  Host (PC,   │◄─────────►│   ESP8266/ESP32      │  │
  │   │  Server, MCU)│  port     │   WiFi-Serial Bridge │  │
  │   └──────────────┘  e.g.4096 └──────────┬───────────┘  │
  │                                         │               │
  └─────────────────────────────────────────┼───────────────┘
                                            │ UART (TX/RX/GND)
                                            ▼
                                  ┌─────────────────┐
                                  │  Serial Device  │
                                  │  (GPS, PLC,     │
                                  │   sensor, MCU)  │
                                  └─────────────────┘
```

### Data Flow (Bidirectional)

```
WiFi RX  →  TCP socket buffer  →  UART TX  →  Serial device
Serial device  →  UART RX  →  TCP socket buffer  →  WiFi TX
```

### Operating Modes

| Mode | Description |
|---|---|
| **TCP Server** | ESP listens; hosts connect on demand. Best for polling. |
| **TCP Client** | ESP connects to a fixed host server. Best for streaming. |
| **UDP** | Stateless, low-latency; useful for time-critical telemetry. |
| **WebSocket** | Browser-accessible; useful for web dashboards. |
| **MQTT-over-WiFi** | Serial data published as MQTT topics. |

---

## 3. Hardware Setup

### Wiring: ESP8266 (NodeMCU) ↔ Serial Device

```
NodeMCU          Serial Device
─────────────────────────────────
GPIO1 (TX0)  →   RX
GPIO3 (RX0)  ←   TX
GND          ─── GND
3.3V/5V      →   VCC  (check device voltage!)
```

> ⚠️ **Voltage Warning**: ESP8266/ESP32 GPIO pins are **3.3V logic**. RS-232 signals use ±12V.
> Always use a **MAX3232** (3.3V) or **MAX232** (5V) level shifter for RS-232 devices.

### Wiring: ESP32 with Second UART (UART2)

```
ESP32           Serial Device
─────────────────────────────────
GPIO17 (TX2) →   RX
GPIO16 (RX2) ←   TX
GND          ─── GND
```

Using UART2 leaves UART0 free for debug output — a critical advantage of the ESP32.

---

## 4. Protocol Stack

### Framing Considerations

Raw TCP is a **stream protocol** — it provides no message boundaries. For a transparent bridge this is fine; the application layer on each side handles its own framing. However, for structured protocols consider:

- **Fixed-length packets**: simplest; wastes bandwidth on short messages
- **Length-prefixed frames**: 2-byte header carrying payload length
- **Delimiter-based**: use `\n` or a custom sentinel byte
- **SLIP (RFC 1055)**: lightweight byte-stuffing framing used in embedded systems

For most transparent bridge applications, **raw byte forwarding** with TCP is sufficient.

---

## 5. Firmware: ESP8266/ESP32 Bridge in C/C++

The ESP-IDF (for ESP32) and Arduino SDK (for both) are the dominant frameworks.

### 5.1 Arduino / ESP-Arduino: TCP Server Bridge

This is the most common pattern: the ESP acts as a TCP server. Any client that connects receives and can send serial data.

```cpp
// WiFi_Serial_Bridge.ino
// Platform: ESP8266 (NodeMCU) or ESP32 (minor #ifdef changes)
// Framework: Arduino

#include <Arduino.h>

#ifdef ESP32
  #include <WiFi.h>
#else
  #include <ESP8266WiFi.h>
#endif

// ─── Configuration ───────────────────────────────────────────────────────────
static const char*    SSID          = "MyNetwork";
static const char*    PASSWORD      = "MyPassword";
static const uint16_t TCP_PORT      = 4096;
static const uint32_t UART_BAUD     = 115200;
static const size_t   BUFFER_SIZE   = 256;

// ─── Globals ─────────────────────────────────────────────────────────────────
WiFiServer  server(TCP_PORT);
WiFiClient  client;           // single active client slot
uint8_t     buf[BUFFER_SIZE];

// ─── Helper: non-blocking reconnect ──────────────────────────────────────────
static void wifi_connect() {
  Serial.println("\n[WiFi] Connecting...");
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    if (millis() - t0 > 15000) {
      Serial.println("[WiFi] Timeout — restarting");
      ESP.restart();
    }
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\n[WiFi] Connected. IP: %s\n",
                WiFi.localIP().toString().c_str());
}

// ─── Setup ───────────────────────────────────────────────────────────────────
void setup() {
  // Debug port: UART0 on ESP8266, USB-CDC on ESP32
  Serial.begin(115200);

#ifdef ESP32
  // Use UART2 for the bridge so UART0 (Serial) stays free for debug
  Serial2.begin(UART_BAUD, SERIAL_8N1, 16 /*RX*/, 17 /*TX*/);
#else
  // ESP8266 has only one usable UART — sacrifice debug output
  Serial.begin(UART_BAUD);
#endif

  wifi_connect();
  server.begin();
  server.setNoDelay(true);   // Disable Nagle algorithm for low latency

  Serial.printf("[Bridge] TCP server listening on port %d\n", TCP_PORT);
}

// ─── Main Loop ───────────────────────────────────────────────────────────────
void loop() {
  // ── Reconnect WiFi if dropped ──────────────────────────────────────────────
  if (WiFi.status() != WL_CONNECTED) {
    wifi_connect();
  }

  // ── Accept new TCP connection (single-client model) ───────────────────────
  if (server.hasClient()) {
    if (client && client.connected()) {
      // Reject additional clients
      WiFiClient rejected = server.available();
      rejected.stop();
      Serial.println("[Bridge] Rejected second client");
    } else {
      client = server.available();
      Serial.printf("[Bridge] Client connected: %s\n",
                    client.remoteIP().toString().c_str());
    }
  }

  // ── Forward: TCP → UART ───────────────────────────────────────────────────
  if (client && client.connected() && client.available()) {
    size_t n = client.read(buf, sizeof(buf));
    if (n > 0) {
#ifdef ESP32
      Serial2.write(buf, n);
#else
      Serial.write(buf, n);
#endif
    }
  }

  // ── Forward: UART → TCP ───────────────────────────────────────────────────
#ifdef ESP32
  if (Serial2.available() && client && client.connected()) {
    size_t n = Serial2.readBytes(buf, sizeof(buf));
#else
  if (Serial.available() && client && client.connected()) {
    size_t n = Serial.readBytes(buf, sizeof(buf));
#endif
    if (n > 0) {
      client.write(buf, n);
    }
  }

  // ── Clean up stale client ─────────────────────────────────────────────────
  if (client && !client.connected()) {
    Serial.println("[Bridge] Client disconnected");
    client.stop();
  }
}
```

### 5.2 Multi-Client TCP Bridge (ESP32, FreeRTOS Tasks)

The ESP32's dual-core FreeRTOS allows a cleaner, interrupt-driven design with
multiple simultaneous clients via separate tasks per connection.

```cpp
// multi_client_bridge.cpp  (ESP-Arduino framework on ESP32)

#include <Arduino.h>
#include <WiFi.h>

static const char*    SSID        = "MyNetwork";
static const char*    PASSWORD    = "MyPassword";
static const uint16_t TCP_PORT    = 4096;
static const uint32_t UART_BAUD   = 115200;
static const int      MAX_CLIENTS = 4;
static const size_t   BUF_SIZE    = 512;

WiFiServer server(TCP_PORT);

// Shared UART mutex — multiple client tasks write to the same UART
SemaphoreHandle_t uart_mutex;

// ─── Per-client task ──────────────────────────────────────────────────────────
struct ClientTask {
  WiFiClient client;
  bool       active = false;
};

static ClientTask clients[MAX_CLIENTS];
static SemaphoreHandle_t clients_mutex;

// Forward declarations
void client_task(void* arg);
void uart_to_all_task(void* arg);

void setup() {
  Serial.begin(115200);
  Serial2.begin(UART_BAUD, SERIAL_8N1, 16, 17);

  uart_mutex    = xSemaphoreCreateMutex();
  clients_mutex = xSemaphoreCreateMutex();

  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  Serial.printf("[WiFi] IP: %s\n", WiFi.localIP().toString().c_str());

  server.begin();
  server.setNoDelay(true);

  // Task: broadcast UART RX to all TCP clients
  xTaskCreatePinnedToCore(uart_to_all_task, "uart_bcast", 4096, nullptr, 2, nullptr, 0);

  Serial.printf("[Bridge] Listening on port %d (max %d clients)\n",
                TCP_PORT, MAX_CLIENTS);
}

// ─── Broadcast UART → all clients ─────────────────────────────────────────────
void uart_to_all_task(void* /*arg*/) {
  uint8_t buf[BUF_SIZE];
  for (;;) {
    size_t avail = Serial2.available();
    if (avail > 0) {
      size_t n = Serial2.readBytes(buf, min(avail, BUF_SIZE));
      xSemaphoreTake(clients_mutex, portMAX_DELAY);
      for (auto& ct : clients) {
        if (ct.active && ct.client.connected()) {
          ct.client.write(buf, n);
        }
      }
      xSemaphoreGive(clients_mutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

// ─── Per-client task: TCP → UART ──────────────────────────────────────────────
void client_task(void* arg) {
  ClientTask* ct = static_cast<ClientTask*>(arg);
  uint8_t buf[BUF_SIZE];

  Serial.printf("[Bridge] Client task started for %s\n",
                ct->client.remoteIP().toString().c_str());

  while (ct->client.connected()) {
    if (ct->client.available()) {
      size_t n = ct->client.read(buf, sizeof(buf));
      if (n > 0) {
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
        Serial2.write(buf, n);
        xSemaphoreGive(uart_mutex);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  Serial.println("[Bridge] Client disconnected");
  xSemaphoreTake(clients_mutex, portMAX_DELAY);
  ct->client.stop();
  ct->active = false;
  xSemaphoreGive(clients_mutex);

  vTaskDelete(nullptr); // Self-delete
}

void loop() {
  if (server.hasClient()) {
    WiFiClient incoming = server.available();

    xSemaphoreTake(clients_mutex, portMAX_DELAY);
    bool accepted = false;
    for (auto& ct : clients) {
      if (!ct.active) {
        ct.client = incoming;
        ct.active = true;
        xSemaphoreGive(clients_mutex);

        xTaskCreatePinnedToCore(client_task, "client", 4096, &ct, 1, nullptr, 1);
        accepted = true;
        break;
      }
    }
    if (!accepted) {
      xSemaphoreGive(clients_mutex);
      incoming.stop(); // No slot available
      Serial.println("[Bridge] Max clients reached — connection rejected");
    }
  }
  delay(10);
}
```

### 5.3 UDP Bridge (Low-Latency Telemetry)

For sensor data where occasional packet loss is acceptable and latency matters:

```cpp
// udp_bridge.ino  (ESP32, Arduino)
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>

static const char*    SSID      = "MyNetwork";
static const char*    PASSWORD  = "MyPassword";
static const uint16_t LOCAL_PORT  = 4097;
// Target host to send UART data to
static const char*    HOST_IP   = "192.168.1.100";
static const uint16_t HOST_PORT = 4098;
static const uint32_t UART_BAUD = 9600;
static const size_t   BUF_SIZE  = 256;

WiFiUdp udp;
uint8_t buf[BUF_SIZE];

void setup() {
  Serial2.begin(UART_BAUD, SERIAL_8N1, 16, 17);
  WiFi.begin(SSID, PASSWORD);
  while (WiFi.status() != WL_CONNECTED) delay(200);
  udp.begin(LOCAL_PORT);
}

void loop() {
  // UART → UDP
  if (Serial2.available()) {
    size_t n = Serial2.readBytes(buf, BUF_SIZE);
    udp.beginPacket(HOST_IP, HOST_PORT);
    udp.write(buf, n);
    udp.endPacket();
  }

  // UDP → UART
  int pkt = udp.parsePacket();
  if (pkt > 0) {
    size_t n = udp.read(buf, BUF_SIZE);
    Serial2.write(buf, n);
  }
}
```

---

## 6. Host-Side Client in C/C++

The host application opens a TCP socket to the ESP's IP and port, then reads/writes
as if it were a local serial port.

### 6.1 POSIX TCP Serial Client (Linux / macOS / WSL)

```c
/* wifi_serial_client.c
 * Compile: gcc -o wifi_serial_client wifi_serial_client.c
 * Usage:   ./wifi_serial_client 192.168.1.42 4096
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

#define BUF_SIZE 1024

/* Create and connect a non-blocking TCP socket */
static int tcp_connect(const char *host, const char *port) {
    struct addrinfo hints = {
        .ai_family   = AF_UNSPEC,
        .ai_socktype = SOCK_STREAM,
    };
    struct addrinfo *res;

    int err = getaddrinfo(host, port, &hints, &res);
    if (err) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(err));
        return -1;
    }

    int fd = -1;
    for (struct addrinfo *p = res; p; p = p->ai_next) {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;

        /* Disable Nagle — critical for interactive/low-latency use */
        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;

        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);

    if (fd < 0) {
        perror("connect");
    }
    return fd;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]);
        return 1;
    }

    int sock = tcp_connect(argv[1], argv[2]);
    if (sock < 0) return 1;

    printf("[Client] Connected to %s:%s\n", argv[1], argv[2]);

    /* Set stdin to raw, non-blocking */
    fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK);

    uint8_t buf[BUF_SIZE];
    fd_set  rfds;
    int     maxfd = (sock > STDIN_FILENO ? sock : STDIN_FILENO) + 1;

    for (;;) {
        FD_ZERO(&rfds);
        FD_SET(sock,         &rfds);
        FD_SET(STDIN_FILENO, &rfds);

        struct timeval tv = { .tv_sec = 1, .tv_usec = 0 };
        int nready = select(maxfd, &rfds, NULL, NULL, &tv);

        if (nready < 0) {
            if (errno == EINTR) continue;
            perror("select");
            break;
        }

        /* Socket → stdout (data arriving from the serial device) */
        if (FD_ISSET(sock, &rfds)) {
            ssize_t n = recv(sock, buf, sizeof(buf), 0);
            if (n <= 0) {
                printf("\n[Client] Server closed connection.\n");
                break;
            }
            fwrite(buf, 1, n, stdout);
            fflush(stdout);
        }

        /* stdin → socket (user sends data to the serial device) */
        if (FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n > 0) {
                if (send(sock, buf, n, 0) != n) {
                    perror("send");
                    break;
                }
            }
        }
    }

    close(sock);
    return 0;
}
```

### 6.2 Length-Prefixed Framing Protocol (C++)

For structured communication where you need reliable message boundaries over TCP:

```cpp
// framed_client.cpp
// Compile: g++ -std=c++17 -o framed_client framed_client.cpp
// Implements a simple 2-byte-length-prefixed protocol:
//   [LEN_HI][LEN_LO][...payload...]

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

// ─── Low-level helpers ────────────────────────────────────────────────────────

static int tcp_connect(const char* host, uint16_t port) {
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%u", port);

    addrinfo hints{}, *res;
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int rc = getaddrinfo(host, port_str, &hints, &res);
    if (rc) throw std::runtime_error(gai_strerror(rc));

    int fd = -1;
    for (auto* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        int flag = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd); fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) throw std::runtime_error("connect failed");
    return fd;
}

// Receive exactly `n` bytes (blocking)
static void recv_exact(int fd, void* buf, size_t n) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        ssize_t r = recv(fd, p, n, MSG_WAITALL);
        if (r <= 0) throw std::runtime_error("recv_exact: connection lost");
        p += r; n -= r;
    }
}

// Send all bytes
static void send_all(int fd, const void* buf, size_t n) {
    auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        ssize_t s = send(fd, p, n, MSG_NOSIGNAL);
        if (s <= 0) throw std::runtime_error("send_all: connection lost");
        p += s; n -= s;
    }
}

// ─── Framed protocol ──────────────────────────────────────────────────────────

class FramedSerialClient {
public:
    explicit FramedSerialClient(const char* host, uint16_t port)
        : fd_(tcp_connect(host, port)) {
        printf("[FramedClient] Connected to %s:%u\n", host, port);
    }

    ~FramedSerialClient() { if (fd_ >= 0) ::close(fd_); }

    // Send a framed message: [2-byte big-endian length][payload]
    void send_frame(const uint8_t* data, uint16_t len) {
        uint8_t hdr[2] = {
            static_cast<uint8_t>(len >> 8),
            static_cast<uint8_t>(len & 0xFF)
        };
        send_all(fd_, hdr,  2);
        send_all(fd_, data, len);
    }

    // Receive a framed message; returns payload bytes
    std::vector<uint8_t> recv_frame() {
        uint8_t hdr[2];
        recv_exact(fd_, hdr, 2);
        uint16_t len = (static_cast<uint16_t>(hdr[0]) << 8) | hdr[1];
        std::vector<uint8_t> payload(len);
        recv_exact(fd_, payload.data(), len);
        return payload;
    }

private:
    int fd_;
};

// ─── Example usage ────────────────────────────────────────────────────────────
int main() {
    try {
        FramedSerialClient client("192.168.1.42", 4096);

        // Send a command to the serial device
        const char* cmd = "AT\r\n";
        client.send_frame(reinterpret_cast<const uint8_t*>(cmd),
                          static_cast<uint16_t>(strlen(cmd)));
        printf("[Client] Sent: %s", cmd);

        // Wait for response
        auto response = client.recv_frame();
        printf("[Client] Received %zu bytes: %.*s\n",
               response.size(),
               static_cast<int>(response.size()),
               response.data());

    } catch (const std::exception& e) {
        fprintf(stderr, "[Error] %s\n", e.what());
        return 1;
    }
    return 0;
}
```

---

## 7. Host-Side Client in Rust

Rust's ownership model, `std::net` socket API, and rich ecosystem (`tokio` for async)
make it an excellent choice for robust, production-grade bridge clients.

### 7.1 Synchronous TCP Client (std::net)

```rust
// src/main.rs — synchronous WiFi-serial bridge client
// Cargo.toml: no external deps needed for this version
//
// [package]
// name    = "wifi_serial_client"
// version = "0.1.0"
// edition = "2021"

use std::io::{self, BufReader, Read, Write};
use std::net::TcpStream;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

const RECONNECT_DELAY_MS: u64 = 2000;

fn connect_with_retry(addr: &str) -> TcpStream {
    loop {
        match TcpStream::connect(addr) {
            Ok(stream) => {
                // Disable Nagle's algorithm for low-latency forwarding
                stream.set_nodelay(true).expect("set_nodelay failed");
                stream
                    .set_read_timeout(Some(Duration::from_millis(100)))
                    .expect("set_read_timeout failed");
                println!("[Client] Connected to {addr}");
                return stream;
            }
            Err(e) => {
                eprintln!("[Client] Connection failed: {e}. Retrying in {RECONNECT_DELAY_MS}ms…");
                thread::sleep(Duration::from_millis(RECONNECT_DELAY_MS));
            }
        }
    }
}

fn main() {
    let addr = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "192.168.1.42:4096".to_string());

    let stream = connect_with_retry(&addr);
    let stream = Arc::new(Mutex::new(stream));

    // ── Thread 1: Network → stdout ────────────────────────────────────────────
    let stream_rx = Arc::clone(&stream);
    let rx_thread = thread::spawn(move || {
        let mut buf = [0u8; 1024];
        loop {
            let result = {
                let mut s = stream_rx.lock().unwrap();
                s.read(&mut buf)
            };
            match result {
                Ok(0) => {
                    eprintln!("[Client] Server closed connection.");
                    break;
                }
                Ok(n) => {
                    io::stdout().write_all(&buf[..n]).ok();
                    io::stdout().flush().ok();
                }
                Err(ref e)
                    if e.kind() == io::ErrorKind::WouldBlock
                        || e.kind() == io::ErrorKind::TimedOut =>
                {
                    // No data yet — spin
                }
                Err(e) => {
                    eprintln!("[Client] Read error: {e}");
                    break;
                }
            }
        }
    });

    // ── Thread 2: stdin → Network ─────────────────────────────────────────────
    let stream_tx = Arc::clone(&stream);
    let tx_thread = thread::spawn(move || {
        let stdin = io::stdin();
        let mut reader = BufReader::new(stdin);
        let mut buf = [0u8; 1024];
        loop {
            match reader.read(&mut buf) {
                Ok(0) | Err(_) => break, // EOF or error
                Ok(n) => {
                    let mut s = stream_tx.lock().unwrap();
                    if s.write_all(&buf[..n]).is_err() {
                        eprintln!("[Client] Write error.");
                        break;
                    }
                }
            }
        }
    });

    rx_thread.join().ok();
    tx_thread.join().ok();
}
```

### 7.2 Async Tokio Client with Framing

For production use with async I/O, reconnection logic, and length-prefixed framing:

```toml
# Cargo.toml
[package]
name    = "async_serial_bridge"
version = "0.1.0"
edition = "2021"

[dependencies]
tokio        = { version = "1", features = ["full"] }
tokio-util   = { version = "0.7", features = ["codec"] }
bytes        = "1"
tracing      = "0.1"
tracing-subscriber = "0.3"
```

```rust
// src/main.rs — async WiFi-serial bridge client with length-prefixed framing
//
// Frame format: [u16 big-endian length][payload bytes]

use bytes::{Buf, BufMut, Bytes, BytesMut};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;
use tokio::sync::mpsc;
use tokio::time::{sleep, Duration};
use tokio_util::codec::{Decoder, Encoder, Framed};
use tracing::{error, info, warn};

// ─── Length-prefixed codec ────────────────────────────────────────────────────

struct LengthPrefixedCodec;

impl Decoder for LengthPrefixedCodec {
    type Item  = Bytes;
    type Error = std::io::Error;

    fn decode(&mut self, src: &mut BytesMut) -> Result<Option<Self::Item>, Self::Error> {
        if src.len() < 2 {
            return Ok(None); // Need at least 2 bytes for the length header
        }
        let len = u16::from_be_bytes([src[0], src[1]]) as usize;
        if src.len() < 2 + len {
            src.reserve(2 + len - src.len());
            return Ok(None); // Incomplete frame
        }
        src.advance(2);
        Ok(Some(src.split_to(len).freeze()))
    }
}

impl Encoder<Bytes> for LengthPrefixedCodec {
    type Error = std::io::Error;

    fn encode(&mut self, item: Bytes, dst: &mut BytesMut) -> Result<(), Self::Error> {
        let len = item.len();
        if len > u16::MAX as usize {
            return Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "Frame too large",
            ));
        }
        dst.reserve(2 + len);
        dst.put_u16(len as u16);
        dst.extend_from_slice(&item);
        Ok(())
    }
}

// ─── Bridge client ────────────────────────────────────────────────────────────

use futures::{SinkExt, StreamExt};

async fn run_session(
    addr: &str,
    mut outgoing_rx: mpsc::Receiver<Bytes>,
    incoming_tx:     mpsc::Sender<Bytes>,
) -> std::io::Result<()> {
    let stream = TcpStream::connect(addr).await?;
    stream.set_nodelay(true)?;
    info!("[Client] Connected to {addr}");

    let mut framed = Framed::new(stream, LengthPrefixedCodec);

    loop {
        tokio::select! {
            // Receive from network, forward upstream
            maybe_frame = framed.next() => {
                match maybe_frame {
                    Some(Ok(frame)) => {
                        if incoming_tx.send(frame).await.is_err() {
                            break; // Receiver dropped
                        }
                    }
                    Some(Err(e)) => { error!("Decode error: {e}"); break; }
                    None         => { info!("[Client] Server closed."); break; }
                }
            }
            // Receive from app, send to network
            maybe_out = outgoing_rx.recv() => {
                match maybe_out {
                    Some(data) => { framed.send(data).await?; }
                    None       => break, // Sender dropped
                }
            }
        }
    }
    Ok(())
}

async fn run_with_reconnect(addr: String) {
    let (out_tx, out_rx)  = mpsc::channel::<Bytes>(64);
    let (in_tx,  mut in_rx) = mpsc::channel::<Bytes>(64);

    // Spawn the reconnecting network loop
    let addr_clone = addr.clone();
    tokio::spawn(async move {
        loop {
            match run_session(&addr_clone, out_rx, in_tx.clone()).await {
                Ok(_)  => warn!("[Client] Session ended cleanly"),
                Err(e) => error!("[Client] Session error: {e}"),
            }
            sleep(Duration::from_secs(2)).await;
            info!("[Client] Reconnecting…");
        }
    });

    // Application logic: send a command, print responses
    let cmd = Bytes::from_static(b"AT\r\n");
    out_tx.send(cmd).await.expect("send failed");
    info!("[App] Sent AT command");

    while let Some(frame) = in_rx.recv().await {
        info!(
            "[App] Received {} bytes: {:?}",
            frame.len(),
            std::str::from_utf8(&frame).unwrap_or("<binary>")
        );
    }
}

#[tokio::main]
async fn main() {
    tracing_subscriber::fmt::init();

    let addr = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "192.168.1.42:4096".to_string());

    run_with_reconnect(addr).await;
}
```

### 7.3 Rust: Virtual Serial Port via `serialport` crate

For applications expecting a local `/dev/ttyXXX` interface, use `socat` to create a
PTY pair and pipe one end to the TCP bridge:

```bash
# Create a virtual serial port at /dev/ttyV0 backed by the WiFi bridge
socat PTY,link=/dev/ttyV0,raw,echo=0 TCP:192.168.1.42:4096
```

Then access it in Rust as any other serial port:

```toml
[dependencies]
serialport = "4"
```

```rust
// src/virtual_port.rs — access WiFi bridge through a PTY (via socat)
use serialport::SerialPort;
use std::io::{Read, Write};
use std::time::Duration;

fn open_bridge_as_serial(port_path: &str, baud: u32) -> Box<dyn SerialPort> {
    serialport::new(port_path, baud)
        .timeout(Duration::from_millis(500))
        .open()
        .expect("Failed to open virtual serial port")
}

fn main() {
    // /dev/ttyV0 was created by socat pointing at the ESP's TCP server
    let mut port = open_bridge_as_serial("/dev/ttyV0", 115200);
    println!("[VirtualPort] Opened bridge as serial port");

    // Send AT command
    port.write_all(b"AT\r\n").expect("write failed");

    // Read response
    let mut buf = vec![0u8; 256];
    let n = port.read(&mut buf).expect("read failed");
    println!(
        "[VirtualPort] Response: {}",
        String::from_utf8_lossy(&buf[..n])
    );
}
```

---

## 8. Advanced Topics

### 8.1 Baud Rate Auto-Detection

For bridges that must accommodate devices with unknown baud rates, implement an
autobaud detection routine on the ESP32:

```cpp
// autobaud.cpp — measure pulse width on RX to estimate baud rate
// Works by timing the shortest high→low transition (= 1 bit period)

static const uint32_t CANDIDATE_BAUDS[] = {
    1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200, 230400, 921600
};

uint32_t nearest_baud(uint32_t measured_bps) {
    uint32_t best = CANDIDATE_BAUDS[0];
    uint32_t best_err = abs((int32_t)measured_bps - (int32_t)best);
    for (auto b : CANDIDATE_BAUDS) {
        uint32_t err = abs((int32_t)measured_bps - (int32_t)b);
        if (err < best_err) { best = b; best_err = err; }
    }
    return best;
}

// Attach to RX pin interrupt and measure pulse widths
volatile uint32_t last_edge_us = 0;
volatile uint32_t min_pulse_us = UINT32_MAX;

void IRAM_ATTR on_rx_edge() {
    uint32_t now = micros();
    uint32_t pulse = now - last_edge_us;
    last_edge_us = now;
    if (pulse > 0 && pulse < min_pulse_us) min_pulse_us = pulse;
}

uint32_t detect_baud(uint8_t rx_pin, uint32_t sample_ms = 1000) {
    min_pulse_us = UINT32_MAX;
    last_edge_us = micros();
    attachInterrupt(digitalPinToInterrupt(rx_pin), on_rx_edge, CHANGE);
    delay(sample_ms);
    detachInterrupt(digitalPinToInterrupt(rx_pin));

    if (min_pulse_us == UINT32_MAX) return 9600; // No signal — use default
    uint32_t measured_bps = 1000000UL / min_pulse_us;
    return nearest_baud(measured_bps);
}
```

### 8.2 Circular Ring Buffer (Lock-Free, ESP32)

For high-throughput bridges, avoid blocking between the UART ISR and the TCP task
using a lock-free single-producer single-consumer ring buffer:

```cpp
// ring_buffer.h — SPSC ring buffer for ISR-to-task data transfer

template<size_t N>
class RingBuffer {
    static_assert((N & (N - 1)) == 0, "N must be a power of 2");
public:
    bool push(uint8_t byte) {
        size_t next = (head_ + 1) & (N - 1);
        if (next == tail_) return false; // Full
        buf_[head_] = byte;
        head_ = next;
        return true;
    }

    bool pop(uint8_t& out) {
        if (head_ == tail_) return false; // Empty
        out  = buf_[tail_];
        tail_ = (tail_ + 1) & (N - 1);
        return true;
    }

    size_t size() const {
        return (head_ - tail_) & (N - 1);
    }

private:
    volatile uint8_t buf_[N]{};
    volatile size_t  head_ = 0;
    volatile size_t  tail_ = 0;
};

// Instantiate a 4096-byte ring buffer for UART→TCP path
RingBuffer<4096> uart_rx_ring;
```

### 8.3 mDNS / Bonjour Discovery

Hard-coding IP addresses is fragile. Use mDNS to advertise the bridge so clients can
discover it by name:

```cpp
// ESP32 mDNS advertisement
#include <ESPmDNS.h>

void setup_mdns(const char* hostname) {
    if (!MDNS.begin(hostname)) {
        Serial.println("[mDNS] Start failed");
        return;
    }
    // Advertise a raw TCP serial service
    MDNS.addService("uart-bridge", "tcp", TCP_PORT);
    // Clients can discover via: dns-sd -B _uart-bridge._tcp
    Serial.printf("[mDNS] Advertised as %s.local\n", hostname);
}
```

---

## 9. Error Handling and Reliability

### Connection State Machine

```
         ┌──────────┐
    ┌────►│  IDLE    │◄──────────────────────────────────┐
    │    └────┬─────┘                                    │
    │         │ WiFi up                                  │
    │    ┌────▼─────┐                                    │
    │    │CONNECTING│                                    │
    │    └────┬─────┘                                    │
    │         │ TCP connected                            │
    │    ┌────▼─────┐     Client disconnects    ┌────────┴───┐
    │    │  ACTIVE  ├──────────────────────────►│ CLOSING    │
    │    └────┬─────┘                           └────────────┘
    │         │ WiFi dropped
    │    ┌────▼─────┐
    └────┤  ERROR   │
         └──────────┘
```

### Watchdog Timer (ESP32)

```cpp
#include <esp_task_wdt.h>

void setup() {
    // Restart if main loop blocks for more than 5 seconds
    esp_task_wdt_init(5, true);
    esp_task_wdt_add(NULL);
}

void loop() {
    esp_task_wdt_reset(); // Must be called regularly
    // ... bridge logic ...
}
```

### TCP Keep-Alive Detection

```cpp
// Detect silently dropped connections (e.g., NAT timeout, cable unplug)
WiFiClient client;

void enable_keepalive(WiFiClient& c) {
    int fd = c.fd();
    int yes = 1;
    setsockopt(fd, SOL_SOCKET,  SO_KEEPALIVE,  &yes,  sizeof(yes));
    int idle  = 10; // Seconds idle before first keepalive probe
    int intvl = 5;  // Seconds between probes
    int cnt   = 3;  // Drop connection after this many unanswered probes
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE,  &idle,  sizeof(idle));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &intvl, sizeof(intvl));
    setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT,   &cnt,   sizeof(cnt));
}
```

---

## 10. Security Considerations

### Why Security Matters

An unsecured WiFi-serial bridge exposes every byte of UART traffic — including
industrial control commands, credentials, firmware updates — to anyone on the LAN
or internet.

### Threat Mitigation Table

| Threat | Mitigation |
|---|---|
| Eavesdropping | TLS/SSL socket (mbedTLS on ESP-IDF) |
| Unauthorized access | Password authentication on connect |
| Replay attacks | Nonce-based or sequence-numbered frames |
| SSID exposure | WPA2-Enterprise or provisioning via BLE |
| OTA firmware injection | Signed OTA updates (ESP-IDF secure boot) |

### Simple Token Authentication (C++)

```cpp
// On the ESP: require client to send a shared secret before forwarding data

static const char* AUTH_TOKEN   = "secret-token-abc123";
static const size_t TOKEN_LEN   = 20; // Fixed length for timing safety

bool authenticate_client(WiFiClient& c) {
    uint8_t buf[TOKEN_LEN] = {};
    uint32_t t0 = millis();

    // Wait for token with 5-second timeout
    while (c.available() < (int)TOKEN_LEN) {
        if (!c.connected() || millis() - t0 > 5000) return false;
        delay(10);
    }
    c.readBytes(buf, TOKEN_LEN);

    // Constant-time comparison to prevent timing attacks
    uint8_t diff = 0;
    for (size_t i = 0; i < TOKEN_LEN; i++) {
        diff |= buf[i] ^ (uint8_t)AUTH_TOKEN[i];
    }
    return diff == 0;
}
```

---

## 11. Debugging and Diagnostics

### AT-Command Probe (before wiring the real device)

Use `nc` (netcat) or `socat` to test the bridge without a real serial device:

```bash
# Open an interactive TCP session to the bridge
nc 192.168.1.42 4096

# Or send a single command and capture response
echo -ne "AT\r\n" | nc -q1 192.168.1.42 4096

# Hex dump of raw bridge traffic
nc 192.168.1.42 4096 | xxd
```

### Latency Measurement (Python)

```python
# measure_latency.py — round-trip time through the WiFi bridge
import socket, time, statistics

HOST, PORT = "192.168.1.42", 4096
ITERATIONS = 100
PAYLOAD    = b"PING\r\n"

latencies = []
with socket.create_connection((HOST, PORT), timeout=5) as s:
    s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    for _ in range(ITERATIONS):
        t0 = time.perf_counter()
        s.sendall(PAYLOAD)
        s.recv(1024)               # discard echo
        latencies.append((time.perf_counter() - t0) * 1000)

print(f"RTT min={min(latencies):.2f}ms  "
      f"avg={statistics.mean(latencies):.2f}ms  "
      f"max={max(latencies):.2f}ms  "
      f"p99={sorted(latencies)[98]:.2f}ms")
```

### ESP Diagnostic Endpoint (HTTP)

```cpp
// Add a simple HTTP diagnostic page alongside the TCP bridge (ESP32)
#include <WebServer.h>
WebServer http(80);

void setup_diagnostics() {
    http.on("/", HTTP_GET, []() {
        String json = "{";
        json += "\"ip\":\"" + WiFi.localIP().toString() + "\",";
        json += "\"rssi\":"  + String(WiFi.RSSI())      + ",";
        json += "\"heap\":"  + String(ESP.getFreeHeap()) + ",";
        json += "\"uptime\":" + String(millis() / 1000)  + "}";
        http.send(200, "application/json", json);
    });
    http.begin();
}

void loop() {
    http.handleClient();
    // ... bridge logic ...
}
```

---

## 12. Summary

| Aspect | Key Points |
|---|---|
| **Hardware** | ESP32 preferred (3 UARTs, dual-core, more RAM). Use level shifters for RS-232. |
| **Operating Mode** | TCP Server for on-demand polling; TCP Client for persistent streaming; UDP for low-latency telemetry. |
| **Firmware Pattern** | Single-client loop (simple), multi-client FreeRTOS tasks (scalable), ring buffer (ISR-safe high throughput). |
| **Framing** | Raw bytes for transparent bridging; length-prefix or SLIP for structured protocols. |
| **C/C++ Client** | POSIX `select()` for bidirectional I/O; `TCP_NODELAY` essential for low latency. |
| **Rust Client** | `std::net` for simple sync use; `tokio` + `tokio-util::codec` for async with custom framing. |
| **Reliability** | Watchdog timer, TCP keep-alive, reconnect loops, connection state machine. |
| **Security** | Token authentication at minimum; TLS for production; signed OTA for firmware. |
| **Discovery** | mDNS/Bonjour (`_uart-bridge._tcp`) eliminates hard-coded IPs. |
| **Latency** | Typical WiFi bridge RTT: 2–15 ms on a local LAN; use `TCP_NODELAY` + ring buffers to minimize jitter. |

### When to Choose What

- **ESP8266** — cost-sensitive, single device, moderate throughput (< 115200 baud)
- **ESP32** — multiple UART ports needed, multi-client, high baud rates, TLS, or BLE provisioning required
- **TCP Server on ESP** — host connects on demand; simple polling use cases
- **TCP Client on ESP** — persistent cloud connection; data logging; always-on streaming
- **UDP** — real-time telemetry where occasional loss is acceptable and latency is critical
- **Rust async client** — production server handling many bridges simultaneously; requires robust reconnect and structured framing
- **C POSIX client** — embedded Linux targets, minimal dependencies, tight resource budgets