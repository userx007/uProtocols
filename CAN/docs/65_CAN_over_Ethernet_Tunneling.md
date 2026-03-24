# 65. CAN over Ethernet Tunneling

> **Encapsulating CAN frames in Ethernet packets for remote access and distributed development.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Tunnel CAN over Ethernet?](#why-tunnel-can-over-ethernet)
3. [Protocol Architecture](#protocol-architecture)
4. [Tunneling Protocols and Standards](#tunneling-protocols-and-standards)
   - [SocketCAN / ISOTP (Linux)](#socketcan--isotp-linux)
   - [CANalyzer Remote / Vector XL-API over TCP](#canalyzer-remote--vector-xl-api-over-tcp)
   - [CAN over UDP (Custom)](#can-over-udp-custom)
   - [CANFD Support](#canfd-support)
5. [Frame Encapsulation Format](#frame-encapsulation-format)
6. [Implementation in C/C++](#implementation-in-cc)
   - [UDP Sender (CAN → Ethernet)](#udp-sender-can--ethernet)
   - [UDP Receiver (Ethernet → CAN)](#udp-receiver-ethernet--can)
   - [TCP Tunneling Server](#tcp-tunneling-server)
   - [TCP Tunneling Client](#tcp-tunneling-client)
   - [SocketCAN vcan Bridge](#socketcan-vcan-bridge)
7. [Implementation in Rust](#implementation-in-rust)
   - [Rust UDP Sender](#rust-udp-sender)
   - [Rust UDP Receiver](#rust-udp-receiver)
   - [Rust Async TCP Tunnel (Tokio)](#rust-async-tcp-tunnel-tokio)
   - [Rust CAN Frame Codec with Tokio Codec](#rust-can-frame-codec-with-tokio-codec)
8. [Error Handling and Reliability](#error-handling-and-reliability)
9. [Security Considerations](#security-considerations)
10. [Performance and Latency](#performance-and-latency)
11. [Practical Deployment Scenarios](#practical-deployment-scenarios)
12. [Summary](#summary)

---

## Introduction

The Controller Area Network (CAN) bus is a robust serial communication protocol widely used in automotive, industrial automation, medical devices, and aerospace systems. CAN operates over a shared differential bus (typically up to 1 Mbit/s for classic CAN, up to 8 Mbit/s for CAN FD) with a maximum physical cable length of a few tens of metres.

**CAN over Ethernet Tunneling** is the technique of encapsulating raw CAN frames — or entire CAN bus traffic streams — inside standard IP/UDP or IP/TCP packets and transmitting them across an Ethernet network (LAN, WAN, or even the Internet). At the remote end, the encapsulated CAN data is unwrapped and injected back onto a physical or virtual CAN bus.

This extends the reach of CAN communication from a few metres on a physical bus to essentially any network distance.

---

## Why Tunnel CAN over Ethernet?

| Motivation | Description |
|---|---|
| **Remote ECU access** | Developers and testers can interact with vehicle ECUs from a different building, city, or country. |
| **Distributed simulation** | HIL (Hardware-in-the-Loop) test systems can share CAN data across multiple simulation nodes. |
| **CI/CD pipelines** | Automated test infrastructure can run CAN-based tests on real hardware without physical co-location. |
| **CAN bus logging** | A gateway device logs live CAN traffic and streams it over the network to a central server. |
| **Bus bridging** | Two geographically separated CAN segments are transparently bridged via an IP tunnel. |
| **Virtual CAN development** | ECU software is developed and tested using virtual CAN (vcan) interfaces backed by a remote tunnel. |
| **Monitoring & diagnostics** | Real-time dashboards monitor CAN traffic from production machines or fleet vehicles. |

---

## Protocol Architecture

The overall architecture of a CAN over Ethernet tunnel looks like this:

```
  [CAN Device A]                               [CAN Device B]
       |                                              |
  [CAN Bus]                                     [CAN Bus]
       |                                              |
  [CAN Interface]                           [CAN Interface]
  (e.g. socketcan)                          (e.g. socketcan)
       |                                              |
  [Tunnel Sender]  ──── IP Network ────  [Tunnel Receiver]
  (Encapsulate)       (UDP or TCP)        (Decapsulate)
       |                                              |
  [UDP/TCP Socket]                        [UDP/TCP Socket]
```

Key layers:

- **Physical layer**: CAN bus (ISO 11898), CAN FD, or virtual CAN (Linux `vcan`)
- **CAN driver layer**: `SocketCAN` on Linux, peak PCAN API, Vector XL API, etc.
- **Encapsulation layer**: Custom UDP/TCP framing, or standard protocols (see below)
- **Transport layer**: UDP (low-latency, lossy) or TCP (reliable, ordered)
- **Network layer**: IPv4 or IPv6 Ethernet

---

## Tunneling Protocols and Standards

### SocketCAN / ISOTP (Linux)

Linux `SocketCAN` provides a native `AF_CAN` socket family. The `can-utils` package provides tools like `cangw` (CAN gateway) and `cannelloni` (open-source CAN over UDP tunneling daemon). `cannelloni` groups multiple CAN frames into a single UDP packet for efficiency.

**cannelloni frame format (simplified):**

```
UDP Payload:
  [version: 1 byte]
  [op_code: 1 byte]
  [seq_no: 2 bytes]
  [count: 2 bytes]
  [CAN frame 0] [CAN frame 1] ... [CAN frame N]
```

### CANalyzer Remote / Vector XL-API over TCP

Vector's CANalyzer and CANdb++ tools support remote bus access via proprietary TCP-based protocols. The XL-API can be used over networked virtual channels.

### CAN over UDP (Custom)

Many embedded and automotive projects implement their own lightweight protocol:

- Each CAN frame is encapsulated in a fixed-size or variable-size UDP datagram.
- A sequence number allows detection of lost or reordered packets.
- Timestamps allow offline replay of bus traffic.

### CANFD Support

CAN FD (Flexible Data-rate) frames carry up to 64 bytes of payload (vs. 8 bytes for classic CAN) and require an extended encapsulation format. Any tunnel must distinguish classic CAN and CAN FD frames, typically via a flags field.

---

## Frame Encapsulation Format

A practical custom tunnel packet format:

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|    Magic (0xCA 0xFE)          |  Version      |  Flags        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Sequence Number (32-bit)                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   Timestamp (64-bit microseconds)             |
|                                                               |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Frame Count  |
+-+-+-+-+-+-+-+-+
```

Per CAN frame:

```
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                   CAN ID (29-bit + flags)                     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|   DLC (Data Length Code)      |  Frame Flags (EFF/RTR/ERR/FD) |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|         Data (0–8 bytes classic / 0–64 bytes CAN FD)          |
|                         (padded to 4-byte boundary)           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

**Flags byte:**

| Bit | Meaning |
|-----|---------|
| 0 | Extended Frame Format (EFF) — 29-bit CAN ID |
| 1 | Remote Transmission Request (RTR) |
| 2 | Error Frame |
| 3 | CAN FD frame |
| 4 | CAN FD Bit Rate Switch (BRS) |
| 5 | CAN FD Error State Indicator (ESI) |
| 6–7 | Reserved |

---

## Implementation in C/C++

### UDP Sender (CAN → Ethernet)

This example reads CAN frames from a SocketCAN interface and sends them over UDP.

```c
/* can_tunnel_sender.c
 * Reads CAN frames from a SocketCAN interface and forwards them via UDP.
 * Build: gcc -o can_sender can_tunnel_sender.c
 * Usage: ./can_sender <can_iface> <remote_ip> <remote_port>
 *        e.g.: ./can_sender can0 192.168.1.100 5555
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>

#define TUNNEL_MAGIC    0xCAFE
#define TUNNEL_VERSION  1
#define MAX_FRAMES_PER_PKT 8

/* ── Tunnel packet header ─────────────────────────────────────── */
typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint32_t seq_no;
    uint64_t timestamp_us;
    uint8_t  frame_count;
} tunnel_header_t;

/* ── Per-frame header inside the tunnel packet ────────────────── */
typedef struct __attribute__((packed)) {
    uint32_t can_id;        /* CAN ID + EFF/RTR/ERR bits */
    uint8_t  dlc;
    uint8_t  frame_flags;
    uint8_t  pad[2];
    uint8_t  data[8];       /* max classic CAN payload */
} tunnel_frame_t;

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <can_iface> <remote_ip> <port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *can_iface  = argv[1];
    const char *remote_ip  = argv[2];
    uint16_t    remote_port = (uint16_t)atoi(argv[3]);

    /* ── Open SocketCAN socket ──────────────────────────────────── */
    int can_sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (can_sock < 0) { perror("socket(CAN)"); return EXIT_FAILURE; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, can_iface, IFNAMSIZ - 1);
    if (ioctl(can_sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl(SIOCGIFINDEX)"); return EXIT_FAILURE;
    }

    struct sockaddr_can addr_can = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(can_sock, (struct sockaddr *)&addr_can, sizeof(addr_can)) < 0) {
        perror("bind(CAN)"); return EXIT_FAILURE;
    }

    /* ── Open UDP socket ─────────────────────────────────────────── */
    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) { perror("socket(UDP)"); return EXIT_FAILURE; }

    struct sockaddr_in dest = {
        .sin_family      = AF_INET,
        .sin_port        = htons(remote_port),
    };
    if (inet_pton(AF_INET, remote_ip, &dest.sin_addr) != 1) {
        fprintf(stderr, "Invalid remote IP: %s\n", remote_ip); return EXIT_FAILURE;
    }

    /* ── Main forwarding loop ───────────────────────────────────── */
    uint32_t seq_no = 0;
    struct can_frame cf;

    printf("[CAN Sender] Forwarding %s → udp://%s:%u\n",
           can_iface, remote_ip, remote_port);

    while (1) {
        ssize_t nbytes = read(can_sock, &cf, sizeof(cf));
        if (nbytes < (ssize_t)sizeof(struct can_frame)) {
            perror("read(CAN)"); continue;
        }

        /* Build tunnel packet (single frame per UDP datagram for simplicity) */
        uint8_t pkt[sizeof(tunnel_header_t) + sizeof(tunnel_frame_t)];
        tunnel_header_t *hdr = (tunnel_header_t *)pkt;
        tunnel_frame_t  *tf  = (tunnel_frame_t *)(pkt + sizeof(tunnel_header_t));

        hdr->magic        = htons(TUNNEL_MAGIC);
        hdr->version      = TUNNEL_VERSION;
        hdr->flags        = 0;
        hdr->seq_no       = htonl(seq_no++);
        hdr->timestamp_us = htobe64(now_us());
        hdr->frame_count  = 1;

        tf->can_id      = htonl(cf.can_id);
        tf->dlc         = cf.can_dlc;
        tf->frame_flags = (cf.can_id & CAN_EFF_FLAG) ? 0x01 : 0x00;
        tf->frame_flags |= (cf.can_id & CAN_RTR_FLAG) ? 0x02 : 0x00;
        tf->frame_flags |= (cf.can_id & CAN_ERR_FLAG) ? 0x04 : 0x00;
        memcpy(tf->data, cf.data, cf.can_dlc);

        ssize_t sent = sendto(udp_sock, pkt, sizeof(pkt), 0,
                              (struct sockaddr *)&dest, sizeof(dest));
        if (sent < 0) { perror("sendto(UDP)"); }
    }

    close(can_sock);
    close(udp_sock);
    return EXIT_SUCCESS;
}
```

---

### UDP Receiver (Ethernet → CAN)

```c
/* can_tunnel_receiver.c
 * Receives UDP tunnel packets and injects CAN frames into a SocketCAN interface.
 * Build: gcc -o can_receiver can_tunnel_receiver.c
 * Usage: ./can_receiver <listen_port> <can_iface>
 *        e.g.: ./can_receiver 5555 vcan0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>

#define TUNNEL_MAGIC   0xCAFE
#define TUNNEL_VERSION 1
#define MAX_PKT_SIZE   2048

typedef struct __attribute__((packed)) {
    uint16_t magic;
    uint8_t  version;
    uint8_t  flags;
    uint32_t seq_no;
    uint64_t timestamp_us;
    uint8_t  frame_count;
} tunnel_header_t;

typedef struct __attribute__((packed)) {
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  frame_flags;
    uint8_t  pad[2];
    uint8_t  data[8];
} tunnel_frame_t;

static int open_can_socket(const char *iface) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket(CAN)"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl"); close(sock); return -1;
    }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind(CAN)"); close(sock); return -1;
    }
    return sock;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <listen_port> <can_iface>\n", argv[0]);
        return EXIT_FAILURE;
    }
    uint16_t    port     = (uint16_t)atoi(argv[1]);
    const char *can_iface = argv[2];

    /* ── UDP listen socket ──────────────────────────────────────── */
    int udp_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_sock < 0) { perror("socket(UDP)"); return EXIT_FAILURE; }

    struct sockaddr_in local = {
        .sin_family      = AF_INET,
        .sin_port        = htons(port),
        .sin_addr.s_addr = INADDR_ANY,
    };
    if (bind(udp_sock, (struct sockaddr *)&local, sizeof(local)) < 0) {
        perror("bind(UDP)"); return EXIT_FAILURE;
    }

    int can_sock = open_can_socket(can_iface);
    if (can_sock < 0) return EXIT_FAILURE;

    printf("[CAN Receiver] Listening on UDP :%u → %s\n", port, can_iface);

    uint8_t buf[MAX_PKT_SIZE];
    uint32_t last_seq = 0;

    while (1) {
        ssize_t len = recv(udp_sock, buf, sizeof(buf), 0);
        if (len < (ssize_t)sizeof(tunnel_header_t)) continue;

        tunnel_header_t *hdr = (tunnel_header_t *)buf;

        /* Validate magic and version */
        if (ntohs(hdr->magic) != TUNNEL_MAGIC || hdr->version != TUNNEL_VERSION) {
            fprintf(stderr, "Invalid tunnel packet — discarding\n"); continue;
        }

        uint32_t seq = ntohl(hdr->seq_no);
        if (seq != last_seq + 1 && last_seq != 0) {
            fprintf(stderr, "Sequence gap: expected %u, got %u (lost %u packets)\n",
                    last_seq + 1, seq, seq - last_seq - 1);
        }
        last_seq = seq;

        /* Iterate frames */
        uint8_t *cursor = buf + sizeof(tunnel_header_t);
        uint8_t *end    = buf + len;

        for (int i = 0; i < hdr->frame_count && cursor + sizeof(tunnel_frame_t) <= end; i++) {
            tunnel_frame_t *tf = (tunnel_frame_t *)cursor;

            struct can_frame cf = {0};
            cf.can_id  = ntohl(tf->can_id);
            cf.can_dlc = tf->dlc;
            if (tf->frame_flags & 0x01) cf.can_id |= CAN_EFF_FLAG;
            if (tf->frame_flags & 0x02) cf.can_id |= CAN_RTR_FLAG;
            if (tf->frame_flags & 0x04) cf.can_id |= CAN_ERR_FLAG;
            memcpy(cf.data, tf->data, tf->dlc);

            if (write(can_sock, &cf, sizeof(cf)) < 0) {
                perror("write(CAN)");
            }

            cursor += sizeof(tunnel_frame_t);
        }
    }

    close(udp_sock);
    close(can_sock);
    return EXIT_SUCCESS;
}
```

---

### TCP Tunneling Server

TCP tunneling provides reliable, ordered delivery — essential when CAN frames must not be lost (e.g., diagnostic sessions, UDS).

```cpp
// can_tcp_server.cpp  — Bidirectional CAN ↔ TCP tunnel server
// Build: g++ -std=c++17 -pthread -o can_tcp_server can_tcp_server.cpp
// Usage: ./can_tcp_server <can_iface> <tcp_port>

#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>

static constexpr uint16_t FRAME_MAGIC = 0xCAFE;

/* Wire format for a single CAN frame over TCP (length-prefixed) */
struct __attribute__((packed)) TcpCanFrame {
    uint16_t magic;           /* 0xCAFE */
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  flags;
    uint8_t  data[8];
};

static bool send_all(int fd, const void *buf, size_t len) {
    const auto *ptr = static_cast<const uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = send(fd, ptr, len, MSG_NOSIGNAL);
        if (n <= 0) return false;
        ptr += n; len -= n;
    }
    return true;
}

static bool recv_all(int fd, void *buf, size_t len) {
    auto *ptr = static_cast<uint8_t *>(buf);
    while (len > 0) {
        ssize_t n = recv(fd, ptr, len, MSG_WAITALL);
        if (n <= 0) return false;
        ptr += n; len -= n;
    }
    return true;
}

static int open_can(const char *iface) {
    int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr{};
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(s, SIOCGIFINDEX, &ifr);
    sockaddr_can addr{ .can_family = AF_CAN, .can_ifindex = ifr.ifr_ifindex };
    bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    return s;
}

/* Thread: CAN → TCP */
void can_to_tcp(int can_fd, int tcp_fd, std::atomic<bool> &running) {
    can_frame cf{};
    while (running) {
        ssize_t n = read(can_fd, &cf, sizeof(cf));
        if (n < static_cast<ssize_t>(sizeof(can_frame))) break;

        TcpCanFrame pkt{};
        pkt.magic  = htons(FRAME_MAGIC);
        pkt.can_id = htonl(cf.can_id);
        pkt.dlc    = cf.can_dlc;
        pkt.flags  = ((cf.can_id & CAN_EFF_FLAG) ? 0x01 : 0) |
                     ((cf.can_id & CAN_RTR_FLAG) ? 0x02 : 0);
        memcpy(pkt.data, cf.data, cf.can_dlc);

        if (!send_all(tcp_fd, &pkt, sizeof(pkt))) {
            std::cerr << "TCP send failed — closing connection\n"; break;
        }
    }
    running = false;
}

/* Thread: TCP → CAN */
void tcp_to_can(int tcp_fd, int can_fd, std::atomic<bool> &running) {
    TcpCanFrame pkt{};
    while (running) {
        if (!recv_all(tcp_fd, &pkt, sizeof(pkt))) {
            std::cerr << "TCP recv failed — closing connection\n"; break;
        }
        if (ntohs(pkt.magic) != FRAME_MAGIC) {
            std::cerr << "Bad magic — skipping\n"; continue;
        }

        can_frame cf{};
        cf.can_id  = ntohl(pkt.can_id);
        cf.can_dlc = pkt.dlc;
        if (pkt.flags & 0x01) cf.can_id |= CAN_EFF_FLAG;
        if (pkt.flags & 0x02) cf.can_id |= CAN_RTR_FLAG;
        memcpy(cf.data, pkt.data, pkt.dlc);
        write(can_fd, &cf, sizeof(cf));
    }
    running = false;
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <can_iface> <tcp_port>\n";
        return 1;
    }
    const char *can_iface = argv[1];
    uint16_t tcp_port     = static_cast<uint16_t>(std::atoi(argv[2]));

    int can_fd = open_can(can_iface);

    int srv = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    int opt = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{ .sin_family = AF_INET, .sin_port = htons(tcp_port),
                      .sin_addr = { INADDR_ANY } };
    bind(srv, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    listen(srv, 1);

    std::cout << "[CAN TCP Server] Listening on :" << tcp_port
              << " bridging to " << can_iface << "\n";

    while (true) {
        sockaddr_in client{}; socklen_t clen = sizeof(client);
        int tcp_fd = accept(srv, reinterpret_cast<sockaddr*>(&client), &clen);
        if (tcp_fd < 0) continue;

        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client.sin_addr, ip, sizeof(ip));
        std::cout << "Client connected: " << ip << ":" << ntohs(client.sin_port) << "\n";

        std::atomic<bool> running{true};
        std::thread t1(can_to_tcp, can_fd, tcp_fd, std::ref(running));
        std::thread t2(tcp_to_can, tcp_fd, can_fd, std::ref(running));
        t1.join(); t2.join();
        close(tcp_fd);
        std::cout << "Client disconnected.\n";
    }
    close(can_fd); close(srv);
}
```

---

### TCP Tunneling Client

```cpp
// can_tcp_client.cpp — Connects to the TCP tunnel server
// Build: g++ -std=c++17 -pthread -o can_tcp_client can_tcp_client.cpp
// Usage: ./can_tcp_client <can_iface> <server_ip> <server_port>

#include <iostream>
#include <thread>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/ioctl.h>

// Reuse the same TcpCanFrame, send_all, recv_all, open_can,
// can_to_tcp, tcp_to_can functions from the server example above.
// (In a real project these would be in a shared header.)

// [Include shared definitions here — same as server above]

int main(int argc, char *argv[]) {
    if (argc != 4) {
        std::cerr << "Usage: " << argv[0] << " <can_iface> <server_ip> <port>\n";
        return 1;
    }
    const char *can_iface   = argv[1];
    const char *server_ip   = argv[2];
    uint16_t    server_port = static_cast<uint16_t>(std::atoi(argv[3]));

    int can_fd = open_can(can_iface);

    int tcp_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in srv_addr{};
    srv_addr.sin_family = AF_INET;
    srv_addr.sin_port   = htons(server_port);
    inet_pton(AF_INET, server_ip, &srv_addr.sin_addr);

    if (connect(tcp_fd, reinterpret_cast<sockaddr*>(&srv_addr), sizeof(srv_addr)) < 0) {
        perror("connect"); return 1;
    }

    std::cout << "[CAN TCP Client] Connected to " << server_ip << ":" << server_port
              << " bridging " << can_iface << "\n";

    std::atomic<bool> running{true};
    std::thread t1(can_to_tcp, can_fd, tcp_fd, std::ref(running));
    std::thread t2(tcp_to_can, tcp_fd, can_fd, std::ref(running));
    t1.join(); t2.join();

    close(can_fd); close(tcp_fd);
    return 0;
}
```

---

### SocketCAN vcan Bridge

Set up a virtual CAN interface on both sides for development without physical hardware:

```bash
# On both sides — create a virtual CAN interface
sudo modprobe vcan
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Verify
ip link show vcan0

# Test locally (two terminals):
# Terminal 1 — listen:
candump vcan0

# Terminal 2 — send:
cansend vcan0 123#DEADBEEF
```

---

## Implementation in Rust

Rust's memory safety and async ecosystem make it an excellent choice for reliable CAN tunnel implementations. We use the `socketcan` crate for CAN access and `tokio` for async networking.

Add to `Cargo.toml`:

```toml
[dependencies]
socketcan   = "3"
tokio       = { version = "1", features = ["full"] }
bytes       = "1"
tokio-util  = { version = "0.7", features = ["codec"] }
anyhow      = "1"
```

---

### Rust UDP Sender

```rust
// src/bin/can_udp_sender.rs
// Forwards CAN frames from a SocketCAN interface to a remote UDP endpoint.
// Usage: cargo run --bin can_udp_sender -- can0 192.168.1.100:5555

use anyhow::{Context, Result};
use socketcan::{CanSocket, Socket, Frame, CanFrame};
use std::net::UdpSocket;
use std::time::{SystemTime, UNIX_EPOCH};

const TUNNEL_MAGIC: u16   = 0xCAFE;
const TUNNEL_VERSION: u8  = 1;

/// Compact tunnel header (little-endian on the wire for simplicity here)
#[repr(C, packed)]
struct TunnelHeader {
    magic:        u16,
    version:      u8,
    flags:        u8,
    seq_no:       u32,
    timestamp_us: u64,
    frame_count:  u8,
}

/// Per-frame payload
#[repr(C, packed)]
struct TunnelFrame {
    can_id:      u32,
    dlc:         u8,
    frame_flags: u8,
    _pad:        [u8; 2],
    data:        [u8; 8],
}

fn now_us() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_micros() as u64)
        .unwrap_or(0)
}

fn build_packet(frame: &CanFrame, seq_no: u32) -> Vec<u8> {
    let hdr = TunnelHeader {
        magic:        TUNNEL_MAGIC.to_be(),
        version:      TUNNEL_VERSION,
        flags:        0,
        seq_no:       seq_no.to_be(),
        timestamp_us: now_us().to_be(),
        frame_count:  1,
    };

    let raw_id   = frame.raw_id();
    let dlc      = frame.dlc() as u8;
    let mut flags = 0u8;
    if frame.is_extended() { flags |= 0x01; }
    if frame.is_remote_frame() { flags |= 0x02; }
    if frame.is_error_frame() { flags |= 0x04; }

    let mut data_bytes = [0u8; 8];
    let d = frame.data();
    data_bytes[..d.len()].copy_from_slice(d);

    let tf = TunnelFrame {
        can_id:      raw_id.to_be(),
        dlc,
        frame_flags: flags,
        _pad:        [0; 2],
        data:        data_bytes,
    };

    let mut pkt = Vec::with_capacity(
        std::mem::size_of::<TunnelHeader>() + std::mem::size_of::<TunnelFrame>(),
    );

    // Safety: packed repr, no padding issues with byte copy
    let hdr_bytes = unsafe {
        std::slice::from_raw_parts(
            &hdr as *const TunnelHeader as *const u8,
            std::mem::size_of::<TunnelHeader>(),
        )
    };
    let tf_bytes = unsafe {
        std::slice::from_raw_parts(
            &tf as *const TunnelFrame as *const u8,
            std::mem::size_of::<TunnelFrame>(),
        )
    };
    pkt.extend_from_slice(hdr_bytes);
    pkt.extend_from_slice(tf_bytes);
    pkt
}

fn main() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let iface   = args.next().unwrap_or_else(|| "can0".into());
    let remote  = args.next().unwrap_or_else(|| "127.0.0.1:5555".into());

    let can_sock = CanSocket::open(&iface)
        .with_context(|| format!("Failed to open SocketCAN interface '{iface}'"))?;
    let udp_sock = UdpSocket::bind("0.0.0.0:0")
        .context("Failed to bind UDP socket")?;

    println!("[Rust CAN Sender] {iface} → udp://{remote}");

    let mut seq_no: u32 = 0;

    loop {
        let frame = can_sock.read_frame()
            .context("Failed to read CAN frame")?;

        if let CanFrame::Data(data_frame) = frame {
            let pkt = build_packet(&data_frame, seq_no);
            udp_sock.send_to(&pkt, &remote)
                .context("Failed to send UDP packet")?;
            seq_no = seq_no.wrapping_add(1);
        }
    }
}
```

---

### Rust UDP Receiver

```rust
// src/bin/can_udp_receiver.rs
// Receives UDP tunnel packets and injects CAN frames into a SocketCAN interface.
// Usage: cargo run --bin can_udp_receiver -- 5555 vcan0

use anyhow::{bail, Context, Result};
use socketcan::{CanSocket, CanDataFrame, EmbeddedFrame, Frame, Socket};
use std::net::UdpSocket;

const TUNNEL_MAGIC: u16   = 0xCAFE;
const TUNNEL_VERSION: u8  = 1;
const HDR_SIZE: usize     = 15;  // size_of::<TunnelHeader>()
const FRAME_SIZE: usize   = 16;  // size_of::<TunnelFrame>()

fn parse_header(buf: &[u8]) -> Option<(u16, u8, u8, u32, u8)> {
    if buf.len() < HDR_SIZE { return None; }
    let magic   = u16::from_be_bytes([buf[0], buf[1]]);
    let version = buf[2];
    let flags   = buf[3];
    let seq_no  = u32::from_be_bytes([buf[4], buf[5], buf[6], buf[7]]);
    // bytes 8..15 are timestamp (skip for now)
    let count   = buf[14];
    Some((magic, version, flags, seq_no, count))
}

fn parse_can_frame(buf: &[u8]) -> Option<(u32, u8, u8, [u8; 8])> {
    if buf.len() < FRAME_SIZE { return None; }
    let can_id      = u32::from_be_bytes([buf[0], buf[1], buf[2], buf[3]]);
    let dlc         = buf[4];
    let frame_flags = buf[5];
    let mut data    = [0u8; 8];
    data.copy_from_slice(&buf[8..16]);
    Some((can_id, dlc, frame_flags, data))
}

fn main() -> Result<()> {
    let mut args  = std::env::args().skip(1);
    let port  = args.next().unwrap_or_else(|| "5555".into());
    let iface = args.next().unwrap_or_else(|| "vcan0".into());

    let udp_sock = UdpSocket::bind(format!("0.0.0.0:{port}"))
        .context("Failed to bind UDP socket")?;
    let can_sock = CanSocket::open(&iface)
        .with_context(|| format!("Failed to open SocketCAN '{iface}'"))?;

    println!("[Rust CAN Receiver] UDP :{port} → {iface}");

    let mut buf   = [0u8; 2048];
    let mut last_seq: Option<u32> = None;

    loop {
        let (len, _src) = udp_sock.recv_from(&mut buf)
            .context("UDP recv_from failed")?;

        let (magic, version, _flags, seq_no, frame_count) =
            match parse_header(&buf[..len]) {
                Some(v) => v,
                None    => { eprintln!("Short packet"); continue; }
            };

        if magic != TUNNEL_MAGIC || version != TUNNEL_VERSION {
            eprintln!("Invalid magic/version — discarding"); continue;
        }

        // Sequence gap detection
        if let Some(last) = last_seq {
            let expected = last.wrapping_add(1);
            if seq_no != expected {
                eprintln!(
                    "Sequence gap: expected {expected}, got {seq_no} ({} lost)",
                    seq_no.wrapping_sub(expected)
                );
            }
        }
        last_seq = Some(seq_no);

        let mut cursor = HDR_SIZE;
        for _ in 0..frame_count {
            if cursor + FRAME_SIZE > len { break; }

            if let Some((raw_id, dlc, frame_flags, data)) =
                parse_can_frame(&buf[cursor..cursor + FRAME_SIZE])
            {
                let dlc = dlc.min(8) as usize;

                // Reconstruct the SocketCAN id with flag bits
                let mut socket_id = raw_id & 0x1FFF_FFFF;
                if frame_flags & 0x01 != 0 { socket_id |= socketcan::EFF_FLAG; }
                if frame_flags & 0x02 != 0 { socket_id |= socketcan::RTR_FLAG; }
                if frame_flags & 0x04 != 0 { socket_id |= socketcan::ERR_FLAG; }

                if let Ok(id) = socketcan::Id::try_from(socket_id) {
                    if let Ok(frame) = CanDataFrame::new(id, &data[..dlc]) {
                        let _ = can_sock.write_frame(&frame);
                    }
                }
            }
            cursor += FRAME_SIZE;
        }
    }
}
```

---

### Rust Async TCP Tunnel (Tokio)

```rust
// src/bin/can_tcp_tunnel.rs
// Async bidirectional CAN ↔ TCP tunnel using Tokio.
// Usage:
//   Server mode: cargo run --bin can_tcp_tunnel -- server can0 4444
//   Client mode: cargo run --bin can_tcp_tunnel -- client vcan0 192.168.1.100:4444

use anyhow::{Context, Result};
use socketcan::{CanSocket, CanDataFrame, EmbeddedFrame, Frame, Socket};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::{TcpListener, TcpStream};
use std::sync::Arc;

const FRAME_SIZE: usize = 16; // magic(2) + can_id(4) + dlc(1) + flags(1) + data(8)

fn encode_frame(frame: &CanDataFrame) -> [u8; FRAME_SIZE] {
    let mut buf = [0u8; FRAME_SIZE];
    buf[0..2].copy_from_slice(&0xCAFEu16.to_be_bytes());

    let raw_id = frame.raw_id();
    buf[2..6].copy_from_slice(&raw_id.to_be_bytes());

    let dlc = frame.dlc() as u8;
    buf[6] = dlc;

    let mut flags = 0u8;
    if frame.is_extended()     { flags |= 0x01; }
    if frame.is_remote_frame() { flags |= 0x02; }
    buf[7] = flags;

    let d = frame.data();
    buf[8..8 + d.len()].copy_from_slice(d);
    buf
}

fn decode_frame(buf: &[u8; FRAME_SIZE]) -> Option<CanDataFrame> {
    let magic = u16::from_be_bytes([buf[0], buf[1]]);
    if magic != 0xCAFE { return None; }

    let raw_id   = u32::from_be_bytes([buf[2], buf[3], buf[4], buf[5]]);
    let dlc      = buf[6].min(8) as usize;
    let flags    = buf[7];

    let mut socket_id = raw_id & 0x1FFF_FFFF;
    if flags & 0x01 != 0 { socket_id |= socketcan::EFF_FLAG; }
    if flags & 0x02 != 0 { socket_id |= socketcan::RTR_FLAG; }

    let id = socketcan::Id::try_from(socket_id).ok()?;
    CanDataFrame::new(id, &buf[8..8 + dlc]).ok()
}

/// Forward CAN → TCP in a blocking thread
async fn can_to_tcp_task(
    can_iface: Arc<String>,
    mut tcp_write: tokio::io::WriteHalf<TcpStream>,
) -> Result<()> {
    let iface = can_iface.clone();
    // SocketCAN is blocking — run in a thread pool
    tokio::task::spawn_blocking(move || -> Result<Vec<[u8; FRAME_SIZE]>> {
        let sock = CanSocket::open(&*iface)?;
        let mut frames = Vec::new();
        // Read one frame at a time (in production, batch reads)
        if let Ok(socketcan::CanFrame::Data(f)) = sock.read_frame() {
            frames.push(encode_frame(&f));
        }
        Ok(frames)
    });

    // For a production system, use a channel to bridge the blocking
    // SocketCAN thread with the async TCP writer:
    let (tx, mut rx) = tokio::sync::mpsc::channel::<[u8; FRAME_SIZE]>(256);

    let iface2 = can_iface.clone();
    std::thread::spawn(move || {
        let sock = match CanSocket::open(&*iface2) {
            Ok(s) => s,
            Err(e) => { eprintln!("CAN open error: {e}"); return; }
        };
        loop {
            if let Ok(socketcan::CanFrame::Data(f)) = sock.read_frame() {
                let _ = tx.blocking_send(encode_frame(&f));
            }
        }
    });

    while let Some(encoded) = rx.recv().await {
        tcp_write.write_all(&encoded).await
            .context("TCP write failed")?;
    }
    Ok(())
}

/// Forward TCP → CAN in an async task
async fn tcp_to_can_task(
    can_iface: Arc<String>,
    mut tcp_read: tokio::io::ReadHalf<TcpStream>,
) -> Result<()> {
    let sock = Arc::new(CanSocket::open(&*can_iface)
        .with_context(|| format!("open {can_iface}"))?);

    let mut buf = [0u8; FRAME_SIZE];
    loop {
        tcp_read.read_exact(&mut buf).await
            .context("TCP read failed")?;

        if let Some(frame) = decode_frame(&buf) {
            let sock2 = Arc::clone(&sock);
            tokio::task::spawn_blocking(move || {
                let _ = sock2.write_frame(&frame);
            });
        }
    }
}

async fn handle_connection(stream: TcpStream, can_iface: Arc<String>) {
    let (read_half, write_half) = tokio::io::split(stream);
    let iface1 = Arc::clone(&can_iface);
    let iface2 = Arc::clone(&can_iface);

    let t1 = tokio::spawn(can_to_tcp_task(iface1, write_half));
    let t2 = tokio::spawn(tcp_to_can_task(iface2, read_half));

    tokio::select! {
        _ = t1 => eprintln!("CAN→TCP task ended"),
        _ = t2 => eprintln!("TCP→CAN task ended"),
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let mut args = std::env::args().skip(1);
    let mode     = args.next().unwrap_or_else(|| "server".into());
    let iface    = Arc::new(args.next().unwrap_or_else(|| "vcan0".into()));
    let addr     = args.next().unwrap_or_else(|| "0.0.0.0:4444".into());

    match mode.as_str() {
        "server" => {
            let listener = TcpListener::bind(&addr).await
                .with_context(|| format!("Listen on {addr}"))?;
            println!("[Rust Async Server] Listening {addr} ↔ {}", iface);
            loop {
                let (stream, peer) = listener.accept().await?;
                println!("Connection from {peer}");
                let iface = Arc::clone(&iface);
                tokio::spawn(handle_connection(stream, iface));
            }
        }
        "client" => {
            let stream = TcpStream::connect(&addr).await
                .with_context(|| format!("Connect to {addr}"))?;
            println!("[Rust Async Client] {} ↔ {addr}", iface);
            handle_connection(stream, iface).await;
        }
        _ => eprintln!("Unknown mode '{mode}'. Use 'server' or 'client'."),
    }
    Ok(())
}
```

---

### Rust CAN Frame Codec with Tokio Codec

For production-grade TCP tunneling, using `tokio_util::codec` provides clean framing:

```rust
// src/codec.rs — Tokio LengthDelimitedCodec-style CAN frame codec

use bytes::{Buf, BufMut, BytesMut};
use tokio_util::codec::{Decoder, Encoder};
use std::io;

const FRAME_SIZE: usize = 16;

#[derive(Debug, Clone)]
pub struct CanTunnelFrame {
    pub can_id:      u32,
    pub dlc:         u8,
    pub flags:       u8,
    pub data:        [u8; 8],
}

pub struct CanTunnelCodec;

impl Encoder<CanTunnelFrame> for CanTunnelCodec {
    type Error = io::Error;

    fn encode(&mut self, item: CanTunnelFrame, dst: &mut BytesMut) -> io::Result<()> {
        dst.reserve(FRAME_SIZE);
        dst.put_u16(0xCAFE);
        dst.put_u32(item.can_id);
        dst.put_u8(item.dlc);
        dst.put_u8(item.flags);
        dst.put_slice(&item.data);
        Ok(())
    }
}

impl Decoder for CanTunnelCodec {
    type Item  = CanTunnelFrame;
    type Error = io::Error;

    fn decode(&mut self, src: &mut BytesMut) -> io::Result<Option<CanTunnelFrame>> {
        if src.len() < FRAME_SIZE {
            // Not enough data yet — wait for more bytes
            src.reserve(FRAME_SIZE - src.len());
            return Ok(None);
        }

        let magic = u16::from_be_bytes([src[0], src[1]]);
        if magic != 0xCAFE {
            return Err(io::Error::new(io::ErrorKind::InvalidData, "Bad magic"));
        }

        src.advance(2); // consume magic
        let can_id = src.get_u32();
        let dlc    = src.get_u8();
        let flags  = src.get_u8();
        let mut data = [0u8; 8];
        src.copy_to_slice(&mut data);

        Ok(Some(CanTunnelFrame { can_id, dlc, flags, data }))
    }
}
```

---

## Error Handling and Reliability

### UDP Considerations

UDP is connectionless and does not guarantee delivery or ordering. A robust UDP tunnel must handle:

- **Packet loss**: Sequence numbers detect gaps. The receiver can log or trigger a recovery action but cannot retransmit what has been lost.
- **Packet reordering**: A short reorder buffer (e.g., 16–32 packet jitter buffer) can restore order.
- **Duplicate packets**: Sequence numbers allow suppression of duplicates.
- **MTU fragmentation**: A standard Ethernet MTU of 1500 bytes fits approximately 80 classic CAN frames (or ~23 CAN FD frames) per UDP datagram after headers.

```c
/* Sequence gap handler in C */
static void check_sequence(uint32_t last, uint32_t current,
                            uint64_t *lost_count) {
    uint32_t expected = last + 1;
    if (current == expected) return;

    uint32_t gap = current - expected;
    *lost_count += gap;
    fprintf(stderr, "[WARN] Lost %u CAN tunnel packet(s). Total lost: %llu\n",
            gap, (unsigned long long)*lost_count);
}
```

### TCP Considerations

TCP guarantees delivery and ordering, but introduces head-of-line blocking and potential latency jitter under congestion. Techniques to mitigate this:

- Disable Nagle's algorithm (`TCP_NODELAY`) for CAN's latency-sensitive traffic.
- Use `SO_KEEPALIVE` to detect silently dropped connections.

```c
/* Disable Nagle's algorithm for low-latency CAN tunneling */
int flag = 1;
setsockopt(tcp_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));

/* Enable TCP keepalive */
int keepalive = 1;
setsockopt(tcp_fd, SOL_SOCKET, SO_KEEPALIVE, &keepalive, sizeof(keepalive));
int keepidle  = 5;   /* seconds before first probe */
int keepintvl = 1;   /* interval between probes */
int keepcnt   = 3;   /* number of probes before giving up */
setsockopt(tcp_fd, IPPROTO_TCP, TCP_KEEPIDLE,  &keepidle,  sizeof(keepidle));
setsockopt(tcp_fd, IPPROTO_TCP, TCP_KEEPINTVL, &keepintvl, sizeof(keepintvl));
setsockopt(tcp_fd, IPPROTO_TCP, TCP_KEEPCNT,   &keepcnt,   sizeof(keepcnt));
```

---

## Security Considerations

A CAN tunnel that exposes a vehicle or industrial bus over a network introduces significant security risks:

| Threat | Mitigation |
|---|---|
| Unauthorized access to CAN bus | Authenticate clients before allowing tunnel setup (e.g., TLS mutual auth) |
| Man-in-the-middle injection | Encrypt the tunnel (TLS/DTLS) |
| Replay attacks | Use per-session nonces or timestamps with a short validity window |
| Denial-of-service flooding | Rate-limit incoming UDP datagrams; implement allowlists by source IP |
| CAN ID spoofing via tunnel | Validate allowed CAN ID ranges on the gateway side |

**Minimal TLS wrapper using OpenSSL (conceptual C++ sketch):**

```cpp
// Using OpenSSL to wrap the TCP tunnel in TLS
SSL_CTX *ctx = SSL_CTX_new(TLS_server_method());
SSL_CTX_use_certificate_file(ctx, "server.crt", SSL_FILETYPE_PEM);
SSL_CTX_use_PrivateKey_file (ctx, "server.key", SSL_FILETYPE_PEM);
// Require client certificate
SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
SSL_CTX_load_verify_locations(ctx, "ca.crt", nullptr);

SSL *ssl = SSL_new(ctx);
SSL_set_fd(ssl, tcp_fd);
SSL_accept(ssl);

// Replace send/recv with SSL_write/SSL_read
SSL_write(ssl, data, len);
SSL_read(ssl, buf, sizeof(buf));
```

---

## Performance and Latency

Typical end-to-end latency figures for a local Gigabit Ethernet tunnel:

| Configuration | Added Latency |
|---|---|
| Local vcan loopback | < 1 µs |
| UDP LAN (same switch) | ~100–200 µs |
| TCP LAN (Nagle off) | ~200–500 µs |
| TCP over WAN / VPN | 1–50 ms (depends on route) |

CAN itself operates at 1 Mbit/s, meaning a worst-case 8-byte frame takes ~130 µs to transmit on the bus. Tunnel latency on a LAN is therefore comparable to or faster than the physical CAN bit time.

**Throughput:** A single UDP socket on Gigabit Ethernet can easily handle the full bandwidth of multiple CAN buses simultaneously (even 100 buses at 1 Mbit/s each only totals 100 Mbit/s).

---

## Practical Deployment Scenarios

### 1. Remote HIL Test Bench

```
[Test PC — Berlin]           [HIL Rack — Munich]
  Simulation SW               Physical CAN bus
       |                            |
  can_tcp_client            can_tcp_server
       |__________ VPN _____________|
```

### 2. OBD-II Diagnostic Gateway

```
[Vehicle CAN bus]
      |
  [Raspberry Pi]  (socketcan + UDP sender)
      |
  [4G/LTE modem]
      |
[Cloud server]  (UDP receiver → database + dashboard)
```

### 3. Multi-ECU Distributed Simulation

```
[Node A — Engine ECU sim]    [Node B — ABS ECU sim]    [Node C — Body CAN sim]
         |                            |                          |
     vcan0 ←──────────── UDP multicast 239.1.2.3:5555 ─────────→ vcan0
```

For multicast-based CAN tunneling, use `IP_ADD_MEMBERSHIP` to subscribe all nodes to the same multicast group, so every node on the virtual bus sees every CAN frame.

```c
struct ip_mreq mreq;
inet_pton(AF_INET, "239.1.2.3", &mreq.imr_multiaddr);
mreq.imr_interface.s_addr = INADDR_ANY;
setsockopt(udp_sock, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq));
```

---

## Summary

CAN over Ethernet Tunneling is a mature and practical technique for extending the reach of CAN bus communication beyond its physical limits.

**Key takeaways:**

- CAN frames are encapsulated in UDP or TCP packets with a lightweight header carrying the CAN ID, DLC, flags, sequence number, and optional timestamp.
- **UDP** offers the lowest latency and is preferred when occasional frame loss is tolerable (monitoring, logging, development).
- **TCP** provides reliability and ordering and is required for diagnostic sessions (UDS, OBD) where no frame can be lost.
- **Linux SocketCAN** is the de-facto standard CAN API on embedded Linux; virtual `vcan` interfaces allow full tunnel development without hardware.
- **C/C++** implementations are straightforward with raw POSIX sockets and the `<linux/can.h>` API; threading handles the bidirectional data flow.
- **Rust** with `socketcan` + `tokio` provides memory-safe, async tunnel implementations ideal for production gateway software, with clean abstractions via `tokio_util::codec`.
- Security — encryption (TLS/DTLS), authentication, and CAN ID filtering — is essential whenever a tunnel crosses a trust boundary.
- Practical latency on a LAN is 100–500 µs, making tunneling transparent to most higher-level protocols running over CAN.

CAN over Ethernet tunneling is a foundational building block for modern automotive and industrial development infrastructure — enabling distributed teams, cloud-based test benches, and remote diagnostics without modifying the ECUs or protocols on the CAN bus itself.