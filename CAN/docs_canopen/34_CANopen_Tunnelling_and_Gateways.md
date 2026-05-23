# 34. CANopen over Serial / CANopen Tunnelling

> **Series:** CANopen In-Depth Technical Reference  
> **Topic:** CAN-to-Ethernet Gateways (CiA 309), REST/JSON and Binary Tunnel Protocols,
> Remote SDO Access Patterns, Latency Impact on Real-Time PDOs,
> and Secure Remote Commissioning Architectures

---

## Table of Contents

1. [Introduction and Motivation](#1-introduction-and-motivation)
2. [CiA 309 – The Gateway Standard](#2-cia-309--the-gateway-standard)
3. [Physical and Logical Topology](#3-physical-and-logical-topology)
4. [Binary Tunnel Protocol (CiA 309-2 / SocketCAN)](#4-binary-tunnel-protocol-cia-309-2--socketcan)
5. [REST / JSON Tunnel Protocol (CiA 309-5)](#5-rest--json-tunnel-protocol-cia-309-5)
6. [Remote SDO Access Patterns](#6-remote-sdo-access-patterns)
7. [Latency Impact on Real-Time PDOs](#7-latency-impact-on-real-time-pdos)
8. [Secure Remote Commissioning Architectures](#8-secure-remote-commissioning-architectures)
9. [C/C++ Programming Examples](#9-cc-programming-examples)
10. [Summary](#10-summary)

---

## 1. Introduction and Motivation

CANopen was designed as a fieldbus protocol operating over a CAN physical layer, typically
limited to a single bus segment with a maximum of 127 nodes and cable lengths dictated by
bit-rate (e.g., 40 m at 1 Mbit/s, 1000 m at 50 kbit/s). These constraints become obstacles
in modern industrial automation scenarios:

- **Remote factory floors** must be accessible from a central engineering station or cloud.
- **Distributed machines** spread across buildings or sites share a single logical CANopen
  network managed from one location.
- **Commissioning and diagnostics** are performed by service engineers who are not
  physically present at the machine.
- **IT/OT convergence** demands CANopen devices to participate in Ethernet-based
  architectures alongside OPC UA, MQTT, or REST services.

**CANopen Tunnelling** (also called *CANopen over IP* or a *CAN-to-Ethernet gateway*)
bridges CANopen's serial CAN bus to TCP/IP networks. The CAN frames are either encapsulated
transparently (binary tunnel) or translated into a higher-level protocol (REST/JSON, Modbus
TCP, OPC UA). The CiA (CAN in Automation) standardised these interfaces in the **CiA 309**
specification family.

```
+-------------------+          Serial CAN Bus (ISO 11898)
|  CANopen Device A |----+
+-------------------+    |    +-----------+     TCP/IP Network
                          +---| CAN-ETH   |===================== Engineering PC
+-------------------+    |    | Gateway   |                       Cloud Service
|  CANopen Device B |----+    +-----------+                       Remote HMI
+-------------------+
```

---

## 2. CiA 309 – The Gateway Standard

CiA 309 is a multi-part specification defining interfaces between CANopen networks and
non-CAN communication systems. The relevant parts are:

| Part        | Title                                          | Transport        |
|-------------|------------------------------------------------|------------------|
| CiA 309-1   | General principles and services                | (framework)      |
| CiA 309-2   | CANopen ASCII command interpreter              | Serial / Telnet  |
| CiA 309-3   | CANopen gateway with SDO gateway protocol      | TCP/UDP          |
| CiA 309-4   | CANopen to Modbus TCP gateway                  | Modbus TCP       |
| CiA 309-5   | CANopen REST API (HTTP/JSON)                   | HTTP/HTTPS       |
| CiA 309-6   | CANopen to OPC UA gateway                      | OPC UA           |

### 2.1 Core Concepts in CiA 309

**Network Number:** Each CAN bus segment behind a gateway is assigned a logical network
number (1–127). A single gateway can serve multiple CAN buses simultaneously.

**Node ID:** The CANopen node address (1–127) within one network.

**Composite Addressing:** Remote SDO requests use the combined address `[net, node]`.

**SDO Gateway Service:** The gateway exposes a virtual SDO channel that forwards
expedited/segmented SDO requests to the real node on the CAN bus. The gateway handles
the CAN SDO state machine; the IP client just sends a single request and awaits a reply.

---

## 3. Physical and Logical Topology

### 3.1 Single-Network Gateway

The simplest deployment: one CAN bus, one gateway, one IP client.

```
                    CAN Bus (500 kbit/s)
  ┌──────────────────────────────────────────────────┐
  │                                                  │
[Node 1]   [Node 2]   [Node 3]   [Node 4]  [Gateway Node 127]
Drive      I/O        Sensor     Sensor    │
                                           │
                                      CAN-ETH
                                      Adapter
                                           │
                                      Ethernet (100BASE-TX)
                                           │
                                   ┌───────────────┐
                                   │  Engineering  │
                                   │  Workstation  │
                                   └───────────────┘
```

### 3.2 Multi-Network Gateway

A multi-port gateway bridges several isolated CAN buses. Each port gets its own network
number.

```
                         ┌─────────────────────┐
  Net 1: CAN Bus A ──────┤  Port 1             │
  (Drives, 500 kbit/s)   │                     │
                         │   Multi-Net         ├────── Gigabit Ethernet ──── SCADA
  Net 2: CAN Bus B ──────┤  CAN-ETH            │                             OPC UA
  (I/O, 250 kbit/s)      │  Gateway            │
                         │                     │
  Net 3: CAN Bus C ──────┤  Port 3             │
  (Sensors, 125 kbit/s)  │                     │
                         └─────────────────────┘
```

### 3.3 Layered Protocol Stack

```
Application Layer:  CANopen (DS301, DS401, DS402 ...)
                    ─────────────────────────────────
Gateway Layer:      CiA 309-3 SDO Protocol  (TCP port 5000)
                    CiA 309-5 REST/JSON      (TCP port 80/443)
                    ─────────────────────────────────
Transport Layer:    TCP / UDP
Network Layer:      IPv4 / IPv6
Data Link Layer:    Ethernet (IEEE 802.3)
Physical Layer:     100BASE-TX / 1000BASE-T / Wi-Fi
```

---

## 4. Binary Tunnel Protocol (CiA 309-2 / SocketCAN)

### 4.1 ASCII Command Interface (CiA 309-2)

CiA 309-2 defines a human-readable ASCII interface accessible via a serial port, Telnet, or
SSH session. Commands use a simple text syntax, making it useful for manual diagnostics
and scripting.

**Command Format:**

```
[<sequence>] <verb> [<net>] [<node>] [<index>]s<subindex> [<datatype>] [<value>]
```

**Common Verbs:**

| Verb    | Direction  | Description                          |
|---------|------------|--------------------------------------|
| `r`     | SDO Read   | Upload object from node              |
| `w`     | SDO Write  | Download object to node              |
| `start` | NMT        | Send NMT Start command               |
| `stop`  | NMT        | Send NMT Stop command                |
| `preop` | NMT        | Enter Pre-Operational state          |
| `reset` | NMT        | Reset node                           |
| `status`| NMT        | Query NMT state                      |
| `lss`   | LSS        | Layer Setting Services               |

**Example Session (Telnet to Gateway Port 23):**

```
> r 1 2 0x6040s0 u16
[1] r 1 2 6040s00 u16 0x0006

> w 1 2 0x6040s0 u16 0x000F
[2] w 1 2 6040s00 u16 0x000F ok

> status 1 2
[3] status 1 2 OPERATIONAL
```

### 4.2 Raw CAN Frame Tunnelling

Some gateways support raw CAN frame forwarding over TCP/UDP (SocketCAN-over-IP,
CANtunnel, or proprietary protocols). Each CAN frame is encapsulated in a small header
carrying the network number, and the IP payload contains the raw CAN frame bytes.

**CiA 309-3 SDO Tunnel Frame (simplified):**

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Sequence (16)                | Command (8)   | Net Num (8)   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  Node ID (8)  |  Index (16)                   | Subindex (8)  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|  DataType(8)  |  DataLen (8)  |  Data (0–4 bytes) ...         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

The gateway performs the CAN SDO state machine on behalf of the IP client. Only one
logical "virtual SDO client" channel exists per TCP connection; multiplexing is achieved
via the sequence number.

---

## 5. REST / JSON Tunnel Protocol (CiA 309-5)

CiA 309-5 defines a RESTful HTTP/HTTPS API for accessing CANopen resources. Resources are
addressed by URL, and payloads are JSON objects.

### 5.1 URL Structure

```
https://<gateway>/<network>/<node>/od/<index>/<subindex>
```

**Examples:**

| URL                                      | Meaning                                  |
|------------------------------------------|------------------------------------------|
| `GET /1/2/od/6040/0`                     | Read Control Word (0x6040s0) on Net1 N2  |
| `PUT /1/2/od/6040/0`                     | Write Control Word                       |
| `GET /1/2/od/1000/0`                     | Read Device Type (0x1000s0) on Net1 N2   |
| `GET /1/2/nmt`                           | Read NMT state of node 2                 |
| `POST /1/2/nmt`                          | Send NMT command to node 2               |
| `GET /1/nodes`                           | List all active nodes on network 1       |
| `GET /networks`                          | List all gateway networks                |

### 5.2 JSON Payload Examples

**SDO Read Response (`GET /1/2/od/6040/0`):**

```json
{
  "network": 1,
  "node":    2,
  "index":   "0x6040",
  "subindex": 0,
  "datatype": "UNSIGNED16",
  "value":    6,
  "access":   "rw",
  "name":     "Controlword"
}
```

**SDO Write Request (`PUT /1/2/od/6040/0`):**

```json
{
  "value": 15
}
```

**NMT State Response (`GET /1/2/nmt`):**

```json
{
  "network": 1,
  "node":    2,
  "state":   "OPERATIONAL",
  "stateCode": 5
}
```

**NMT Command (`POST /1/2/nmt`):**

```json
{
  "command": "start"
}
```

### 5.3 Bulk Object Dictionary Access

CiA 309-5 allows reading/writing entire object sub-trees, which is useful for uploading
device configurations:

**`GET /1/2/od/1400` (RPDO Communication Parameter):**

```json
{
  "index": "0x1400",
  "name":  "Receive PDO Communication Parameter 1",
  "subobjects": [
    { "subindex": 0, "value": 3,          "datatype": "UNSIGNED8",  "name": "HighestSubindex" },
    { "subindex": 1, "value": 517,        "datatype": "UNSIGNED32", "name": "COB-ID" },
    { "subindex": 2, "value": 255,        "datatype": "UNSIGNED8",  "name": "TransmissionType" }
  ]
}
```

### 5.4 REST Error Handling

HTTP status codes map to CANopen abort codes:

| HTTP Status | Meaning                           | CANopen Abort Code  |
|-------------|-----------------------------------|---------------------|
| 200 OK      | SDO transfer successful           | 0x00000000          |
| 400 Bad Request | Invalid value / datatype      | 0x06070010          |
| 403 Forbidden   | Read-only object              | 0x06010002          |
| 404 Not Found   | Object/subindex not present   | 0x06020000          |
| 408 Timeout     | SDO response timeout          | 0x05040000          |
| 503 Service Unavailable | CAN bus off           | (gateway error)     |

---

## 6. Remote SDO Access Patterns

### 6.1 Synchronous (Blocking) SDO Pattern

The simplest pattern: the IP client sends one request and blocks until the gateway returns
the response. Gateway serialises the CAN SDO exchange internally.

```
  IP Client                Gateway             CANopen Node
      │                       │                     │
      │── HTTP GET / TCP ──►  │                     │
      │   (SDO upload req)    │── SDO Upload Req ──►│
      │                       │                     │ (process)
      │                       │◄── SDO Upload Rsp ──│
      │◄── HTTP 200 / TCP ──  │                     │
      │   (value in JSON)     │                     │
```

**Latency breakdown (typical values):**

```
  ┌─────────────────────────────────────────────────────────────┐
  │ TCP RTT (LAN):          ~0.5 ms                             │
  │ Gateway processing:     ~0.1–0.5 ms                         │
  │ CAN SDO exchange:       ~1–2 ms (@ 500 kbit/s)              │
  │ ─────────────────────────────────────────────────────────── │
  │ Total SDO via Gateway:  ~2–4 ms (LAN)                       │
  │ Total SDO via WAN/VPN:  ~20–200 ms (depends on WAN)         │
  └─────────────────────────────────────────────────────────────┘
```

### 6.2 Asynchronous SDO Pattern

For non-blocking implementations, the IP client sends a request with a sequence number.
Multiple pending requests can be outstanding. The gateway matches replies to requests
using the sequence number.

```
  IP Client              Gateway             CANopen Bus
      │                      │                     │
      │── Req [seq=1] ──────►│── SDO to N1 ───────►│
      │── Req [seq=2] ──────►│── SDO to N2 ───────►│
      │── Req [seq=3] ──────►│── SDO to N3 ───────►│
      │                      │◄── Reply from N3 ───│
      │◄── Rsp [seq=3] ────  │◄── Reply from N1 ───│
      │◄── Rsp [seq=1] ────  │◄── Reply from N2 ───│
      │◄── Rsp [seq=2] ────  │                     │
```

**Note:** The gateway must enforce one outstanding SDO per CAN node (SDO is not
multiplexable per node). Requests to the same node are queued internally.

### 6.3 Block SDO for Large Objects

When reading device firmware (e.g., index 0x1F50) or large configuration files, SDO Block
Transfer (CiA 301 §7.2.4.3) dramatically improves throughput over the gateway:

```
  Expedited SDO (4 bytes)  : 1 CAN frame request + 1 CAN frame response
  Segmented SDO (7 b/seg)  : N segment frames, N+1 total round-trips
  Block SDO (127 b/block)  : N segment frames, but only 2 round-trips per block
                              → dramatically fewer IP-side delays on WAN
```

### 6.4 PDO Monitoring via Gateway

For applications that only need to *observe* PDO data (not transmit in real-time), the
gateway can cache the last received PDO values and serve them via REST:

```
  Gateway internal buffer (updated on each received PDO):

  Net 1, Node 2, TPDO1 (COB-ID 0x182):
    Last data:  [0xAB, 0xCD, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]
    Timestamp:  1716457823.412 (Unix epoch)
    Age:        12 ms

  GET /1/2/tpdo/1 →
  {
    "cobId":     "0x182",
    "data":      "ABCD000000000000",
    "timestamp": 1716457823.412,
    "ageMs":     12
  }
```

This pattern is suitable for SCADA dashboards, historian logging, or alarm monitoring,
but **not** for real-time closed-loop control (see Section 7).

---

## 7. Latency Impact on Real-Time PDOs

### 7.1 PDO Real-Time Constraints

CANopen PDOs are designed for cyclic real-time data exchange with deterministic timing.
A typical motion control PDO cycle is:

```
 Cycle time:  1 ms (1 kHz)
 CAN bit rate: 1 Mbit/s
 PDO frame:   8 data bytes → 111 bit → 111 µs CAN transmission time
 Jitter budget: ±20–50 µs (hardware-timestamped nodes)
```

### 7.2 Effect of IP Tunnelling on PDO Timing

When PDOs are forwarded across an IP network (e.g., the gateway subscribes to TPDOs and
re-publishes them on IP), additional latency and jitter are introduced:

```
  CAN Bus Side                        IP Network Side
  ──────────────────────────────      ─────────────────────────────────
  PDO @  0 ms  ──►[Gateway]──►        IP packet @  0.5 ms (LAN)
  PDO @  1 ms  ──►[Gateway]──►        IP packet @  1.6 ms
  PDO @  2 ms  ──►[Gateway]──►        IP packet @  2.4 ms
  PDO @  3 ms  ──►[Gateway]──►        IP packet @  4.1 ms  ← jitter spike
  PDO @  4 ms  ──►[Gateway]──►        IP packet @  4.5 ms
```

**Observed latency figures (typical LAN, no QoS):**

```
  ┌─────────────────────────────────────────────────────────┐
  │  Metric              │  LAN (1G)   │  WAN/VPN  │ 4G LTE │
  ├──────────────────────┼─────────────┼───────────┼────────┤
  │  Mean latency        │  0.3–1 ms   │  20–80 ms │ 20–50ms│
  │  Jitter (P99)        │  1–3 ms     │  5–50 ms  │ 10–80ms│
  │  Packet loss (P99)   │  0%         │  0.01–0.1%│  0.1%  │
  │  PDO real-time use?  │  Marginal   │  No       │  No    │
  └─────────────────────────────────────────────────────────┘
```

### 7.3 Compensating Mechanisms

**7.3.1 Gateway-side PDO Timestamping**

The gateway timestamps each PDO when it arrives on the CAN bus (hardware or software
timestamp). The IP receiver can reconstruct the original timeline and detect missing PDOs.

**7.3.2 PDO Buffering and Interpolation**

For motion control over WAN, the remote system maintains a buffer of PDO setpoints. The
gateway sends PDOs ahead of time; the remote interpolates between received setpoints,
masking individual lost packets.

```
  Remote Controller Buffer (200 ms depth @ 1 ms cycle = 200 PDO setpoints)

  ─── Time ──────────────────────────────────────────────────────────►
  Received:  [SP0][SP1][SP2][  ][SP4][SP5][  ][  ][SP8][SP9] ...
                               ↑              ↑↑
                          lost packet     2 lost packets
  Interpolated:          [SP3*]         [SP6*][SP7*]  (linear interpolation)
  Applied:     [SP0][SP1][SP2][SP3*][SP4][SP5][SP6*][SP7*][SP8][SP9]
```

**7.3.3 Hybrid Architecture: Local CAN, Remote SDO Only**

The recommended architecture for real-time systems is to keep the PDO loop **local** on
the CAN bus and only route **non-real-time SDO access** through the gateway:

```
  ┌─────────────────────────────────────────────────┐
  │         Machine Controller (local)              │
  │                                                 │
  │  ┌──────────┐   PDO (1 ms, real-time CAN)       │
  │  │ PLC/DSP  │══════════════════════[ Drive ]    │
  │  └──────────┘                                   │
  │       │  SDO (non-real-time)                    │
  │  ┌────▼─────┐                                   │
  │  │ Gateway  │──── Ethernet ──── Cloud / SCADA   │
  │  └──────────┘   (SDO config, diagnostics only)  │
  └─────────────────────────────────────────────────┘
```

---

## 8. Secure Remote Commissioning Architectures

### 8.1 Threat Model

Remote access to industrial CANopen networks exposes critical assets:

```
  THREATS:
  ┌───────────────────────────────────────────────────┐
  │ T1: Unauthorised SDO write → machine misconfigured│
  │ T2: NMT command injection → unexpected stop/reset │
  │ T3: PDO injection → uncontrolled motion           │
  │ T4: Eavesdropping → parameter/IP theft            │
  │ T5: Replay attack → re-execute old commands       │
  │ T6: Denial of Service → gateway overload          │
  └───────────────────────────────────────────────────┘
```

### 8.2 Layered Security Architecture

```
  ┌───────────────────────────────────────────────────────────────┐
  │  Layer 5: Audit & Monitoring                                  │
  │    - All SDO read/writes logged with user, timestamp, value   │
  │    - Anomaly detection: unexpected NMT commands, mass reads   │
  ├───────────────────────────────────────────────────────────────┤
  │  Layer 4: Authorisation (RBAC)                                │
  │    - Role: "Monitor"   → GET only (read SDO, read PDO cache)  │
  │    - Role: "Operator"  → NMT start/stop, limited SDO writes   │
  │    - Role: "Engineer"  → full SDO, firmware download          │
  │    - Role: "Admin"     → gateway config, user management      │
  ├───────────────────────────────────────────────────────────────┤
  │  Layer 3: Authentication                                      │
  │    - Client certificates (mTLS) for machine-to-machine        │
  │    - JWT / OAuth 2.0 for human users via HTTPS                │
  │    - TOTP (2FA) for privileged roles                          │
  ├───────────────────────────────────────────────────────────────┤
  │  Layer 2: Transport Security                                  │
  │    - TLS 1.3 for all REST/JSON (CiA 309-5)                    │
  │    - IPSec / WireGuard VPN for binary tunnel (CiA 309-3)      │
  │    - Certificate pinning for gateway identity                 │
  ├───────────────────────────────────────────────────────────────┤
  │  Layer 1: Network Segmentation                                │
  │    - Gateway in DMZ, not directly in OT network               │
  │    - Firewall: whitelist IP ranges, allowed ports only        │
  │    - Optional: one-way data diode for monitoring-only feeds   │
  └───────────────────────────────────────────────────────────────┘
```

### 8.3 Secure Commissioning Workflow

```
  Service Engineer (remote)             Gateway                  Machine
        │                                  │                        │
        │── HTTPS + mTLS handshake ───────►│                        │
        │   (engineer certificate)         │ verify cert chain      │
        │◄── 200 OK + JWT token ────────── │                        │
        │                                  │                        │
        │── GET /networks ────────────────►│                        │
        │◄── [{net:1, nodes:[2,3,5]}] ───  │                        │
        │                                  │                        │
        │── GET /1/2/od/1000/0 ──────────► │── SDO Read Node 2 ───► │
        │   (read Device Type)             │◄── SDO Response ─────  │
        │◄── {value: 0x00020192} ───────   │                        │
        │                                  │                        │
        │── PUT /1/2/od/6040/0 ──────────► │ RBAC check:            │
        │   {value: 15}  [requires         │ role = "Engineer" ✓    │
        │    "Engineer" role]              │── SDO Write Node 2 ──► │
        │                                  │◄── SDO OK ───────────  │
        │◄── 200 OK ─────────────────────  │                        │
        │                                  │                        │
        │  [all steps logged]              │── Audit log written    │
```

### 8.4 Firmware Update via Gateway (CiA 302-3)

Remote firmware download uses the CANopen firmware update protocol (SDO block transfer
to index 0x1F50/0x1F51/0x1F57) routed through the gateway:

```
  Step 1: Engineer uploads firmware binary to gateway staging area
          POST /1/2/firmware  (multipart/form-data, TLS encrypted)

  Step 2: Gateway validates firmware signature (ECDSA-SHA256)

  Step 3: Gateway sends NMT Pre-Operational to target node

  Step 4: Gateway opens SDO block transfer to node:
          Write 0x1F50s01 (ProgramData) via block SDO
          (128 bytes per block × N blocks)

  Step 5: Gateway writes 0x1F51s01 = 1 (start program update)

  Step 6: Gateway polls 0x1F57s01 (program status) until complete

  Step 7: Gateway sends NMT Reset to node

  Step 8: Engineer verifies new firmware via 0x1018s02 (SW version)
```

---

## 9. C/C++ Programming Examples

### 9.1 CiA 309-2 ASCII Client (TCP/Telnet)

This example opens a TCP connection to a CiA 309-2 gateway and sends ASCII SDO
read/write commands.

```cpp
// cia309_ascii_client.cpp
// Connects to a CiA 309-2 ASCII gateway via TCP and
// performs SDO read/write operations.

#include <iostream>
#include <string>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

class Cia309AsciiClient {
public:
    Cia309AsciiClient(const std::string& host, uint16_t port)
        : host_(host), port_(port), sock_(-1), seq_(0) {}

    ~Cia309AsciiClient() {
        if (sock_ >= 0) close(sock_);
    }

    // Connect to the gateway
    void connect() {
        sock_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (sock_ < 0)
            throw std::runtime_error("socket(): " + std::string(strerror(errno)));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port   = htons(port_);
        inet_pton(AF_INET, host_.c_str(), &addr.sin_addr);

        if (::connect(sock_, (sockaddr*)&addr, sizeof(addr)) < 0)
            throw std::runtime_error("connect(): " + std::string(strerror(errno)));

        // Consume gateway banner if present
        char buf[256];
        struct timeval tv{0, 100000};  // 100 ms
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        (void)recv(sock_, buf, sizeof(buf), 0);  // may time out
    }

    // SDO expedited read: returns value as uint32
    uint32_t sdoRead(uint8_t net, uint8_t node,
                     uint16_t index, uint8_t sub)
    {
        std::ostringstream cmd;
        cmd << "[" << ++seq_ << "] r "
            << (int)net  << " "
            << (int)node << " 0x"
            << std::hex << index
            << "s" << std::dec << (int)sub
            << "\r\n";

        sendCommand(cmd.str());
        std::string resp = readResponse(seq_);
        return parseValue(resp);
    }

    // SDO expedited write
    void sdoWrite(uint8_t net, uint8_t node,
                  uint16_t index, uint8_t sub,
                  const std::string& datatype, uint32_t value)
    {
        std::ostringstream cmd;
        cmd << "[" << ++seq_ << "] w "
            << (int)net  << " "
            << (int)node << " 0x"
            << std::hex << index
            << "s" << std::dec << (int)sub
            << " " << datatype
            << " 0x" << std::hex << value
            << "\r\n";

        sendCommand(cmd.str());
        std::string resp = readResponse(seq_);

        if (resp.find("ok") == std::string::npos &&
            resp.find("OK") == std::string::npos)
            throw std::runtime_error("SDO write failed: " + resp);
    }

    // Send NMT command
    void nmtCommand(uint8_t net, uint8_t node,
                    const std::string& cmd_str)
    {
        std::ostringstream cmd;
        cmd << "[" << ++seq_ << "] " << cmd_str << " "
            << (int)net  << " "
            << (int)node << "\r\n";

        sendCommand(cmd.str());
        readResponse(seq_);
    }

private:
    std::string host_;
    uint16_t    port_;
    int         sock_;
    int         seq_;

    void sendCommand(const std::string& cmd) {
        if (send(sock_, cmd.c_str(), cmd.size(), 0) < 0)
            throw std::runtime_error("send(): " + std::string(strerror(errno)));
    }

    std::string readResponse(int expected_seq) {
        // Set 2-second receive timeout
        struct timeval tv{2, 0};
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        std::string line;
        char c;
        while (true) {
            ssize_t n = recv(sock_, &c, 1, 0);
            if (n <= 0)
                throw std::runtime_error("Gateway timeout or disconnected");
            if (c == '\n') {
                // Check if this is our sequence number
                std::string prefix = "[" + std::to_string(expected_seq) + "]";
                if (line.find(prefix) != std::string::npos)
                    return line;
                line.clear();
            } else if (c != '\r') {
                line += c;
            }
        }
    }

    uint32_t parseValue(const std::string& resp) {
        // Response format: "[seq] r net node indexsSub dtype 0xVALUE"
        auto pos = resp.rfind("0x");
        if (pos == std::string::npos)
            throw std::runtime_error("Cannot parse value from: " + resp);
        return (uint32_t)std::stoul(resp.substr(pos + 2), nullptr, 16);
    }
};

// ─── Demo Usage ───────────────────────────────────────────────
int main() {
    try {
        Cia309AsciiClient gw("192.168.1.100", 23);  // Telnet port
        gw.connect();
        std::cout << "Connected to gateway\n";

        // Read Device Type of node 2 on network 1
        uint32_t devType = gw.sdoRead(1, 2, 0x1000, 0);
        std::cout << "Device type: 0x" << std::hex << devType << "\n";

        // Read vendor ID
        uint32_t vendorId = gw.sdoRead(1, 2, 0x1018, 1);
        std::cout << "Vendor ID: 0x" << std::hex << vendorId << "\n";

        // Start the node (NMT Operational)
        gw.nmtCommand(1, 2, "start");
        std::cout << "Node 2 started\n";

        // Write control word: enable operation (DS402 drive)
        gw.sdoWrite(1, 2, 0x6040, 0, "u16", 0x000F);
        std::cout << "Control word written\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

### 9.2 CiA 309-5 REST/JSON Client (libcurl)

```cpp
// cia309_rest_client.cpp
// HTTP/JSON client for CiA 309-5 REST gateway.
// Requires: libcurl, nlohmann/json

#include <iostream>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// ─── cURL write callback ───────────────────────────────────────
static size_t curlWriteCb(void* data, size_t sz, size_t nmemb,
                           std::string* out) {
    out->append((char*)data, sz * nmemb);
    return sz * nmemb;
}

class Cia309RestClient {
public:
    Cia309RestClient(const std::string& baseUrl,
                     const std::string& certFile = "",
                     const std::string& keyFile  = "",
                     const std::string& caFile   = "")
        : baseUrl_(baseUrl),
          certFile_(certFile),
          keyFile_(keyFile),
          caFile_(caFile)
    {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~Cia309RestClient() {
        curl_global_cleanup();
    }

    // Read an SDO value from the gateway
    json sdoRead(int net, int node, uint16_t index, uint8_t sub) {
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/od/%x/%d",
                 baseUrl_.c_str(), net, node, index, sub);
        return httpGet(url);
    }

    // Write an SDO value to the gateway
    json sdoWrite(int net, int node, uint16_t index, uint8_t sub,
                  const json& payload) {
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/od/%x/%d",
                 baseUrl_.c_str(), net, node, index, sub);
        return httpPut(url, payload);
    }

    // Read NMT state of a node
    json nmtGet(int net, int node) {
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/nmt",
                 baseUrl_.c_str(), net, node);
        return httpGet(url);
    }

    // Send NMT command to a node
    json nmtPost(int net, int node, const std::string& command) {
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/nmt",
                 baseUrl_.c_str(), net, node);
        json payload = { {"command", command} };
        return httpPost(url, payload);
    }

    // List all nodes on a network
    json listNodes(int net) {
        char url[256];
        snprintf(url, sizeof(url), "%s/%d/nodes", baseUrl_.c_str(), net);
        return httpGet(url);
    }

    // Read last cached PDO data
    json pdoGet(int net, int node, int pdoNum) {
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/tpdo/%d",
                 baseUrl_.c_str(), net, node, pdoNum);
        return httpGet(url);
    }

private:
    std::string baseUrl_, certFile_, keyFile_, caFile_;

    CURL* makeCurl(const std::string& url, std::string& respBody,
                   long& httpCode) {
        CURL* c = curl_easy_init();
        if (!c) throw std::runtime_error("curl_easy_init failed");

        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, curlWriteCb);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &respBody);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 5000L);

        // TLS client certificate (mTLS) for CiA 309-5
        if (!certFile_.empty()) {
            curl_easy_setopt(c, CURLOPT_SSLCERT,    certFile_.c_str());
            curl_easy_setopt(c, CURLOPT_SSLKEY,     keyFile_.c_str());
            curl_easy_setopt(c, CURLOPT_CAINFO,     caFile_.c_str());
            curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
            curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        }
        return c;
    }

    json httpGet(const std::string& url) {
        std::string body;
        long code = 0;
        CURL* c = makeCurl(url, body, code);

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Accept: application/json");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

        CURLcode res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);

        if (res != CURLE_OK)
            throw std::runtime_error(curl_easy_strerror(res));
        if (code >= 400)
            throw std::runtime_error("HTTP " + std::to_string(code) +
                                     ": " + body);
        return json::parse(body);
    }

    json httpPut(const std::string& url, const json& payload) {
        return httpSendBody(url, payload, "PUT");
    }

    json httpPost(const std::string& url, const json& payload) {
        return httpSendBody(url, payload, "POST");
    }

    json httpSendBody(const std::string& url, const json& payload,
                      const std::string& method) {
        std::string body;
        long code = 0;
        CURL* c = makeCurl(url, body, code);
        std::string data = payload.dump();

        struct curl_slist* hdrs = nullptr;
        hdrs = curl_slist_append(hdrs, "Content-Type: application/json");
        hdrs = curl_slist_append(hdrs, "Accept: application/json");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, method.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)data.size());

        CURLcode res = curl_easy_perform(c);
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);

        if (res != CURLE_OK)
            throw std::runtime_error(curl_easy_strerror(res));
        if (code >= 400)
            throw std::runtime_error("HTTP " + std::to_string(code) +
                                     ": " + body);
        return body.empty() ? json{} : json::parse(body);
    }
};

// ─── Demo Usage ───────────────────────────────────────────────
int main() {
    // Plain HTTP (no TLS) for lab use
    Cia309RestClient gw("http://192.168.1.100");

    // Secure variant with mTLS certificates
    // Cia309RestClient gw("https://192.168.1.100",
    //     "/etc/canopen/client.crt",
    //     "/etc/canopen/client.key",
    //     "/etc/canopen/ca.crt");

    try {
        // List nodes on network 1
        auto nodes = gw.listNodes(1);
        std::cout << "Nodes on net 1: " << nodes.dump(2) << "\n";

        // Read device type of node 2
        auto dt = gw.sdoRead(1, 2, 0x1000, 0);
        std::cout << "Device Type: " << dt["value"] << "\n";

        // Start node 2
        gw.nmtPost(1, 2, "start");
        std::cout << "Node started\n";

        // Write target velocity to DS402 drive
        gw.sdoWrite(1, 2, 0x60FF, 0, {{"value", 1500}});
        std::cout << "Target velocity set to 1500 rpm\n";

        // Check NMT state
        auto nmt = gw.nmtGet(1, 2);
        std::cout << "NMT state: " << nmt["state"] << "\n";

        // Read cached TPDO1 (status word + actual position)
        auto pdo = gw.pdoGet(1, 2, 1);
        std::cout << "PDO data: " << pdo["data"]
                  << " age: "     << pdo["ageMs"] << " ms\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
    return 0;
}
```

---

### 9.3 Gateway-Side SDO Forwarder (Embedded C)

This example shows the gateway firmware side: it receives a CiA 309-3 TCP request,
performs the CAN SDO exchange, and sends the TCP response.

```c
/* gw_sdo_forwarder.c
 * Gateway firmware: forwards IP SDO requests to CAN SDO.
 * Simplified for clarity – production code adds mutex, timeout,
 * and per-node queuing. */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── CiA 309-3 packet header (16 bytes) ─────────────────── */
#pragma pack(push, 1)
typedef struct {
    uint16_t  sequence;    /* sequence number (big-endian) */
    uint8_t   command;     /* 0x40 = SDO read, 0x22 = SDO write */
    uint8_t   network;     /* CAN network number */
    uint8_t   node;        /* CANopen node ID */
    uint16_t  index;       /* Object Dictionary index */
    uint8_t   subindex;    /* Object Dictionary subindex */
    uint8_t   datatype;    /* CiA 309 data type code */
    uint8_t   datalen;     /* Number of data bytes (0–4) */
    uint8_t   data[4];     /* SDO data (expedited, ≤4 bytes) */
    uint8_t   reserved[2]; /* alignment padding */
} Cia309Packet;
#pragma pack(pop)

/* ── CAN frame structure ─────────────────────────────────── */
typedef struct {
    uint32_t id;        /* 11-bit COB-ID */
    uint8_t  dlc;
    uint8_t  data[8];
} CanFrame;

/* ── Platform HAL (to be implemented per target) ─────────── */
extern int  can_send(uint8_t net, const CanFrame* frame);
extern int  can_recv(uint8_t net, CanFrame* frame, uint32_t timeout_ms);
extern int  tcp_recv(int sock, void* buf, size_t len);
extern int  tcp_send(int sock, const void* buf, size_t len);

/* ── SDO command specifiers ──────────────────────────────── */
#define SDO_CMD_UL_REQ    0x40   /* Upload (read) request     */
#define SDO_CMD_UL_RSP    0x43   /* Upload response (4 bytes) */
#define SDO_CMD_DL_REQ1   0x23   /* Download 4-byte request   */
#define SDO_CMD_DL_REQ2   0x27   /* Download 3-byte request   */
#define SDO_CMD_DL_RSP    0x60   /* Download response (OK)    */
#define SDO_CMD_ABORT     0x80   /* Abort transfer            */

#define SDO_TX_BASE  0x600       /* COB-ID base: 0x600 + NodeID */
#define SDO_RX_BASE  0x580       /* COB-ID base: 0x580 + NodeID */

/* ── Forward one SDO read request to the CAN bus ─────────── */
static int gw_sdo_read(uint8_t net, uint8_t node,
                        uint16_t idx, uint8_t sub,
                        uint8_t* out_data, uint8_t* out_len)
{
    CanFrame tx = {0}, rx = {0};

    /* Build SDO Upload Request */
    tx.id       = SDO_TX_BASE + node;
    tx.dlc      = 8;
    tx.data[0]  = SDO_CMD_UL_REQ;
    tx.data[1]  = (uint8_t)(idx & 0xFF);
    tx.data[2]  = (uint8_t)(idx >> 8);
    tx.data[3]  = sub;
    /* bytes 4-7 are reserved (0x00) */

    if (can_send(net, &tx) < 0)
        return -1;

    /* Wait for SDO Upload Response (timeout 500 ms) */
    for (int tries = 0; tries < 10; ++tries) {
        if (can_recv(net, &rx, 50) < 0)
            continue;

        if (rx.id != (uint32_t)(SDO_RX_BASE + node))
            continue;  /* not our response */

        if (rx.data[0] == SDO_CMD_ABORT)
            return -2;  /* node aborted */

        if ((rx.data[0] & 0xE0) == 0x40) {
            /* Expedited upload response: bytes[4..7] = value */
            uint8_t n = (rx.data[0] >> 2) & 0x03;  /* unused bytes */
            *out_len = 4 - n;
            memcpy(out_data, &rx.data[4], *out_len);
            return 0;
        }
    }
    return -3;  /* timeout */
}

/* ── Forward one SDO write request to the CAN bus ─────────── */
static int gw_sdo_write(uint8_t net, uint8_t node,
                         uint16_t idx, uint8_t sub,
                         const uint8_t* data, uint8_t len)
{
    CanFrame tx = {0}, rx = {0};
    uint8_t cmd;

    /* Choose command specifier based on data length */
    switch (len) {
        case 1: cmd = 0x2F; break;
        case 2: cmd = 0x2B; break;
        case 3: cmd = 0x27; break;
        case 4: cmd = 0x23; break;
        default: return -1;
    }

    tx.id      = SDO_TX_BASE + node;
    tx.dlc     = 8;
    tx.data[0] = cmd;
    tx.data[1] = (uint8_t)(idx & 0xFF);
    tx.data[2] = (uint8_t)(idx >> 8);
    tx.data[3] = sub;
    memcpy(&tx.data[4], data, len);

    if (can_send(net, &tx) < 0)
        return -1;

    /* Wait for SDO Download Response */
    for (int tries = 0; tries < 10; ++tries) {
        if (can_recv(net, &rx, 50) < 0)
            continue;
        if (rx.id != (uint32_t)(SDO_RX_BASE + node))
            continue;
        if (rx.data[0] == SDO_CMD_ABORT)
            return -2;
        if (rx.data[0] == SDO_CMD_DL_RSP)
            return 0;  /* success */
    }
    return -3;  /* timeout */
}

/* ── Main gateway dispatch loop ───────────────────────────── */
void gw_dispatch_loop(int client_sock)
{
    Cia309Packet req, rsp;

    while (true) {
        /* Receive one CiA 309-3 request from IP client */
        if (tcp_recv(client_sock, &req, sizeof(req)) <= 0)
            break;

        memset(&rsp, 0, sizeof(rsp));
        rsp.sequence = req.sequence;
        rsp.command  = req.command | 0x80;  /* response flag */
        rsp.network  = req.network;
        rsp.node     = req.node;
        rsp.index    = req.index;
        rsp.subindex = req.subindex;

        if (req.command == SDO_CMD_UL_REQ) {
            /* SDO Read */
            int rc = gw_sdo_read(req.network, req.node,
                                  req.index, req.subindex,
                                  rsp.data, &rsp.datalen);
            if (rc == -2) {
                /* Abort: fill abort code in data */
                rsp.command  = SDO_CMD_ABORT;
                rsp.datalen  = 4;
                uint32_t ac  = 0x08000000;
                memcpy(rsp.data, &ac, 4);
            } else if (rc < 0) {
                rsp.command  = SDO_CMD_ABORT;
                rsp.datalen  = 4;
                uint32_t ac  = 0x05040000; /* SDO timeout */
                memcpy(rsp.data, &ac, 4);
            }
        } else if ((req.command & 0xE0) == 0x20) {
            /* SDO Write */
            int rc = gw_sdo_write(req.network, req.node,
                                   req.index, req.subindex,
                                   req.data, req.datalen);
            if (rc < 0) {
                rsp.command  = SDO_CMD_ABORT;
                rsp.datalen  = 4;
                uint32_t ac  = (rc == -2) ? 0x08000000 : 0x05040000;
                memcpy(rsp.data, &ac, 4);
            }
        }

        tcp_send(client_sock, &rsp, sizeof(rsp));
    }
}
```

---

### 9.4 Latency Measurement Utility

This utility measures round-trip SDO latency through the gateway at different network
conditions.

```cpp
// gw_latency_bench.cpp
// Measures SDO round-trip latency through a CiA 309-5 REST gateway
// and reports statistics (min, max, mean, P95, P99).

#include <iostream>
#include <vector>
#include <algorithm>
#include <numeric>
#include <chrono>
#include <thread>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;
using json  = nlohmann::json;

static size_t discardCb(void*, size_t sz, size_t nm, void*) {
    return sz * nm;
}

// Perform one SDO GET and return RTT in milliseconds
static double measureSdoRtt(const std::string& url) {
    CURL* c = curl_easy_init();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, discardCb);
    curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 2000L);

    auto t0 = Clock::now();
    CURLcode res = curl_easy_perform(c);
    auto t1 = Clock::now();
    curl_easy_cleanup(c);

    if (res != CURLE_OK) return -1.0;
    return Ms(t1 - t0).count();
}

void printStats(const std::vector<double>& samples) {
    if (samples.empty()) { std::cout << "  No data\n"; return; }

    std::vector<double> sorted = samples;
    std::sort(sorted.begin(), sorted.end());

    double sum  = std::accumulate(sorted.begin(), sorted.end(), 0.0);
    double mean = sum / sorted.size();
    double p95  = sorted[(size_t)(sorted.size() * 0.95)];
    double p99  = sorted[(size_t)(sorted.size() * 0.99)];

    // ASCII histogram (10 buckets)
    double lo   = sorted.front(), hi = sorted.back();
    double bw   = (hi - lo) / 10.0 + 0.001;
    int    hist[10] = {};
    for (double v : sorted)
        hist[(int)((v - lo) / bw)]++;
    int maxH = *std::max_element(hist, hist + 10);

    std::cout << "\n  Latency Distribution (ms):\n";
    for (int row = 4; row >= 0; --row) {
        std::cout << "  |";
        for (int b = 0; b < 10; ++b) {
            int h = (hist[b] * 5 + maxH/2) / (maxH ? maxH : 1);
            std::cout << (h > row ? "###" : "   ");
        }
        std::cout << "|\n";
    }
    std::cout << "  +" << std::string(30, '-') << "+\n";
    std::cout << "   " << lo << " ms" << std::string(20, ' ')
              << hi << " ms\n\n";

    std::cout << "  Samples : " << sorted.size()       << "\n"
              << "  Min     : " << sorted.front()       << " ms\n"
              << "  Max     : " << sorted.back()        << " ms\n"
              << "  Mean    : " << mean                  << " ms\n"
              << "  P95     : " << p95                   << " ms\n"
              << "  P99     : " << p99                   << " ms\n";
}

int main(int argc, char* argv[]) {
    std::string gwHost = (argc > 1) ? argv[1] : "192.168.1.100";
    int samples = (argc > 2) ? std::stoi(argv[2]) : 100;

    // SDO read of 0x1000s0 on net=1 node=2 (device type – always readable)
    std::string url = "http://" + gwHost + "/1/2/od/1000/0";

    std::cout << "CANopen Gateway Latency Benchmark\n"
              << "  Gateway: " << gwHost  << "\n"
              << "  URL:     " << url     << "\n"
              << "  Samples: " << samples << "\n\n";

    curl_global_init(CURL_GLOBAL_ALL);
    std::vector<double> results;
    results.reserve(samples);

    for (int i = 0; i < samples; ++i) {
        double rtt = measureSdoRtt(url);
        if (rtt >= 0) results.push_back(rtt);
        std::cout << "  [" << i+1 << "/" << samples << "] RTT = "
                  << rtt << " ms\r" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::cout << "\n";

    printStats(results);
    curl_global_cleanup();
    return 0;
}
```

---

### 9.5 Secure TLS Gateway Client with JWT Authentication

```cpp
// secure_gw_client.cpp
// Demonstrates JWT bearer token authentication + TLS for CiA 309-5.
// Handles token refresh automatically.

#include <iostream>
#include <string>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

static size_t writeBody(void* data, size_t sz, size_t nm, std::string* out) {
    out->append((char*)data, sz * nm);
    return sz * nm;
}

class SecureGatewayClient {
public:
    SecureGatewayClient(const std::string& host,
                        const std::string& username,
                        const std::string& password,
                        const std::string& caFile)
        : host_(host), user_(username), pass_(password), caFile_(caFile)
    {
        curl_global_init(CURL_GLOBAL_ALL);
    }

    ~SecureGatewayClient() { curl_global_cleanup(); }

    // Authenticate and obtain JWT token
    void authenticate() {
        std::string url  = host_ + "/auth/token";
        json payload = { {"username", user_}, {"password", pass_} };
        std::string body = httpPost(url, payload, "");

        auto resp = json::parse(body);
        token_      = resp["access_token"].get<std::string>();
        expiresAt_  = std::chrono::steady_clock::now() +
                      std::chrono::seconds(
                          resp.value("expires_in", 3600));

        std::cout << "[Auth] Token obtained, expires in "
                  << resp.value("expires_in", 3600) << "s\n";
    }

    // Read an SDO value (auto-refreshes token if expired)
    json sdoRead(int net, int node, uint16_t idx, uint8_t sub) {
        ensureToken();
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/od/%x/%d",
                 host_.c_str(), net, node, idx, sub);
        return json::parse(httpGet(url, token_));
    }

    // Write an SDO value
    json sdoWrite(int net, int node, uint16_t idx, uint8_t sub,
                  const json& payload) {
        ensureToken();
        char url[256];
        snprintf(url, sizeof(url),
                 "%s/%d/%d/od/%x/%d",
                 host_.c_str(), net, node, idx, sub);
        return json::parse(httpPut(url, payload, token_));
    }

private:
    std::string host_, user_, pass_, caFile_, token_;
    std::chrono::steady_clock::time_point expiresAt_;

    void ensureToken() {
        auto now = std::chrono::steady_clock::now();
        // Refresh 30 seconds before expiry
        if (token_.empty() ||
            now >= expiresAt_ - std::chrono::seconds(30))
            authenticate();
    }

    CURL* baseCurl(const std::string& url, std::string& body) {
        CURL* c = curl_easy_init();
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeBody);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &body);
        curl_easy_setopt(c, CURLOPT_TIMEOUT_MS, 5000L);
        // TLS: verify gateway certificate
        curl_easy_setopt(c, CURLOPT_CAINFO, caFile_.c_str());
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
        return c;
    }

    curl_slist* makeHeaders(const std::string& token,
                             bool withBody = false) {
        curl_slist* h = nullptr;
        h = curl_slist_append(h, "Accept: application/json");
        if (withBody)
            h = curl_slist_append(h, "Content-Type: application/json");
        if (!token.empty()) {
            std::string auth = "Authorization: Bearer " + token;
            h = curl_slist_append(h, auth.c_str());
        }
        return h;
    }

    std::string httpGet(const std::string& url,
                        const std::string& token) {
        std::string body;
        CURL* c = baseCurl(url, body);
        auto* h = makeHeaders(token);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(h);
        long code;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK || code >= 400)
            throw std::runtime_error("GET failed: " + body);
        return body;
    }

    std::string httpPost(const std::string& url, const json& payload,
                         const std::string& token) {
        std::string body, data = payload.dump();
        CURL* c = baseCurl(url, body);
        auto* h = makeHeaders(token, true);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)data.size());
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(h);
        long code;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK || code >= 400)
            throw std::runtime_error("POST failed: " + body);
        return body;
    }

    std::string httpPut(const std::string& url, const json& payload,
                        const std::string& token) {
        std::string body, data = payload.dump();
        CURL* c = baseCurl(url, body);
        auto* h = makeHeaders(token, true);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, h);
        curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "PUT");
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, data.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)data.size());
        CURLcode rc = curl_easy_perform(c);
        curl_slist_free_all(h);
        long code;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK || code >= 400)
            throw std::runtime_error("PUT failed: " + body);
        return body;
    }
};

// ─── Demo ─────────────────────────────────────────────────────
int main() {
    SecureGatewayClient gw(
        "https://gateway.factory.example.com",
        "engineer1",
        "s3cr3t!",
        "/etc/ssl/certs/factory-ca.crt"
    );

    gw.authenticate();

    // Read actual velocity from DS402 drive (net=1, node=2)
    auto vel = gw.sdoRead(1, 2, 0x606C, 0);
    std::cout << "Actual velocity: " << vel["value"] << " rpm\n";

    // Set target velocity
    gw.sdoWrite(1, 2, 0x60FF, 0, {{"value", 750}});
    std::cout << "Target velocity set\n";

    return 0;
}
```

---

## 10. Summary

CANopen Tunnelling bridges the gap between traditional CAN-bus fieldbus networks and
modern IP-based automation architectures. The key points from this chapter are:

**Standards and Protocols:**
CiA 309 is the governing specification family. CiA 309-2 provides an ASCII text interface
suitable for scripting and manual diagnostics. CiA 309-3 defines a binary TCP protocol with
sequenced requests. CiA 309-5 specifies a RESTful HTTP/JSON API that integrates naturally
with web technologies, cloud platforms, and microservices. Higher-level translations to
Modbus TCP (309-4) and OPC UA (309-6) are also defined.

**Topology:**
A CAN-to-Ethernet gateway sits at the edge of the CAN bus segment and presents a virtual
SDO client to IP-side software. Multiple CAN networks can be served by a single gateway,
each identified by a logical network number. The gateway handles the CAN SDO state machine
internally; IP clients interact with a simple request/response model.

**SDO Access:**
Remote SDO reads and writes are the primary use case for tunnelling. For expedited objects
the total round-trip over a LAN is typically 2–5 ms; over WAN it rises to 20–200+ ms.
Asynchronous request patterns with sequence numbers allow multiple outstanding requests
across different nodes. SDO Block Transfer should be used for large object uploads over
high-latency links to minimise the number of IP-level round-trips.

**Real-Time PDOs:**
PDOs are inherently real-time and should remain on the local CAN bus. Tunnelling PDOs over
IP introduces jitter that is incompatible with closed-loop motion control over WAN. The
recommended pattern is to tunnel only SDO (configuration, diagnostics) remotely, while
PDO-based control loops execute locally. Gateway-side PDO caching allows SCADA and HMI
systems to observe process values at low polling rates without impacting the CAN bus.

**Security:**
Remote CANopen access expands the attack surface dramatically. A layered security model is
essential: TLS 1.3 or IPSec for transport encryption, certificate-based mutual
authentication or JWT for identity, role-based access control (RBAC) for authorisation,
and comprehensive audit logging for compliance. Firmware updates over the gateway must be
signed and validated before execution on the device.

**Programming:**
The C/C++ examples in this chapter demonstrate a complete toolchain: an ASCII client for
interactive scripting, a REST/JSON client using libcurl for application integration, a
gateway-side SDO forwarder showing the CAN state machine, a latency benchmark utility, and
a secure TLS client with JWT token management and auto-refresh. These form a solid
foundation for building production-grade remote CANopen tooling.

---

*References:*
- *CiA 309: CANopen interfaces for networks and systems — Parts 1–6*
- *CiA 301: CANopen application layer and communication profile*
- *CiA 302: Additional application layer functions for CANopen*
- *ISO 11898: Road vehicles — Controller area network (CAN)*
- *IEC 62443: Industrial network and system security*