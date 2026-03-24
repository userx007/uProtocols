# 59. NMEA 2000 Marine Protocol

> **Maritime CAN network standard for vessel instrumentation and sensor integration.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Protocol Architecture](#protocol-architecture)
3. [Physical Layer](#physical-layer)
4. [Data Model — PGN Structure](#data-model--pgn-structure)
5. [Address Claiming](#address-claiming)
6. [Key PGNs and Their Formats](#key-pgns-and-their-formats)
7. [Programming in C/C++](#programming-in-cc)
8. [Programming in Rust](#programming-in-rust)
9. [Gateway and Bridging Patterns](#gateway-and-bridging-patterns)
10. [Testing and Simulation](#testing-and-simulation)
11. [Summary](#summary)

---

## Introduction

NMEA 2000 (also written N2K) is an open standard published by the **National Marine Electronics Association** for connecting marine instrumentation, sensors, and control devices aboard vessels. It was introduced in 2001 as the successor to the older NMEA 0183 serial protocol.

Where NMEA 0183 was a simple, slow, single-talker/multi-listener RS-422 serial bus, NMEA 2000 is a full CAN-based network that supports:

- **Multiple talkers and listeners** on the same physical bus
- **Plug-and-play** device address claiming
- **High data rates** (250 kbit/s)
- **Standardised message formats** (PGNs — Parameter Group Numbers)
- **Backbone-and-drop** wiring topology

NMEA 2000 is electrically and physically identical to **SAE J1939**, and its data-link and transport layers are derived from J1939. At the application layer, NMEA 2000 defines its own message dictionary (PGNs) tailored to marine use: GPS, depth sounder, wind, engine, rudder, AIS, and much more.

Common usage scenarios include:

- Chartplotters reading GPS position, speed, heading, and depth
- Engine monitors displaying RPM, temperature, oil pressure, and fuel flow
- Autopilots receiving wind and heading data
- AIS transponders broadcasting and receiving vessel identity and position
- Radar overlays receiving course-over-ground from GPS

---

## Protocol Architecture

NMEA 2000 maps cleanly onto the OSI model:

```
┌──────────────────────────────────┐
│  Application Layer               │  PGNs (Parameter Group Numbers)
│  (NMEA 2000 specific)            │  Device class / function codes
├──────────────────────────────────┤
│  Transport / Session Layer       │  ISO 11783-3 fast-packet & multi-packet
│  (derived from J1939 / ISO TP)   │  transport protocol (TP.DT / TP.CM)
├──────────────────────────────────┤
│  Network / Data-Link Layer       │  SAE J1939 / CAN 2.0B 29-bit IDs
│  Address claiming, NAME          │  Priority / PGN / Source address
├──────────────────────────────────┤
│  Physical Layer                  │  ISO 11898 CAN @ 250 kbit/s
│  NMEA 2000 cable spec            │  Backbone + drop cables, terminators
└──────────────────────────────────┘
```

### 29-bit CAN Identifier Layout

Every NMEA 2000 frame uses a 29-bit extended CAN identifier structured as follows:

```
Bits 28–26  Priority (3 bits)   — 0 (highest) to 7 (lowest)
Bit  25     Reserved            — always 0
Bit  24     Data Page           — 0 or 1 (extends PGN space)
Bits 23–16  PGN high byte       — together with bits 24 and the
Bits 15–8   PGN low byte          PDU format, forms the full PGN
Bits  7–0   Source Address      — 0x00–0xFE (0xFF = global)
```

For **peer-to-peer (PDU1)** PGNs (PGN low byte < 0xF0), bits 15–8 carry the destination address and are not part of the PGN. For **broadcast (PDU2)** PGNs (PGN low byte ≥ 0xF0), all 8 bits are part of the PGN and the message is broadcast to all nodes.

---

## Physical Layer

### Cable and Connector

NMEA 2000 defines a proprietary 5-pin connector and cable (compatible with DeviceNet Micro-C):

| Pin | Signal        | Color  |
|-----|---------------|--------|
| 1   | Shield/GND    | Bare   |
| 2   | NET-S (+12 V) | Red    |
| 3   | NET-C (GND)   | Black  |
| 4   | CAN-H         | White  |
| 5   | CAN-L         | Blue   |

### Topology

```
[Term 120Ω] ──── Backbone ──── [Term 120Ω]
                   │   │   │
                 Drop Drop Drop
                  │    │    │
               GPS  Depth  Engine
```

- **Backbone**: up to 100 m of heavy cable (LEN budget)
- **Drops**: maximum 6 m each
- **LEN (Load Equivalency Number)**: each device consumes LENs; the bus supplies up to 50 LEN at 1 A per LEN
- **Terminators**: 120 Ω at each end of the backbone (measured: ~60 Ω across CAN-H/CAN-L when powered off)

### Bit Timing at 250 kbit/s

```
Bit time = 4 µs
Sync segment  = 1 time quantum
Propagation   = configurable
Phase Seg 1   = configurable
Phase Seg 2   = configurable
SJW           = 1–4 time quanta
```

---

## Data Model — PGN Structure

### Parameter Group Number (PGN)

A PGN is a 17-bit or 18-bit number identifying a logical group of parameters. Common PGNs:

| PGN    | Decimal | Description                          |
|--------|---------|--------------------------------------|
| 0x01F0 | 496     | ISO Address Claim                    |
| 0x01FE | 510     | ISO Request                          |
| 0x1F801| 129025  | GNSS Position Rapid Update           |
| 0x1F802| 129026  | COG & SOG Rapid Update               |
| 0x1F808| 129038  | AIS Class A Position Report          |
| 0x1FA02| 130306  | Wind Data                            |
| 0x1F10D| 127245  | Rudder                               |
| 0x1F201| 127489  | Engine Parameters (Rapid)            |
| 0x1F202| 127488  | Engine Parameters (Dynamic)          |
| 0x1FA0C| 130312  | Temperature                          |
| 0x1F10E| 127250  | Vessel Heading                       |
| 0x1F501| 128267  | Water Depth                          |

### Fast-Packet Protocol

PGNs with payloads larger than 8 bytes (up to 223 bytes) use NMEA 2000's **fast-packet** framing:

```
Frame 0 (first):
  Byte 0: [Seq counter (3 bits) | Frame counter (5 bits)] = 0xX0
  Byte 1: Total bytes in message
  Bytes 2–7: First 6 bytes of payload

Frames 1–N (subsequent):
  Byte 0: [Seq counter (3 bits) | Frame counter (5 bits)] = 0xX1, 0xX2, …
  Bytes 1–7: Next 7 bytes of payload
```

Up to 32 frames can be sent per message (frame counter 0–31).

### Multi-Packet Transport Protocol (ISO TP)

For PGNs that use J1939 TP instead of fast-packet (rare in NMEA 2000 but valid):

- **TP.CM** (PGN 0xEC00): Connection Management — broadcast or peer-to-peer
- **TP.DT** (PGN 0xEB00): Data Transfer — up to 1785 bytes

---

## Address Claiming

Every NMEA 2000 node must claim a unique source address (0x00–0xFE) using a J1939-derived procedure based on its **NAME** field — an 8-byte value encoding:

```
Bits 63    Self-configurable address (1=yes)
Bits 62–60 Industry group (4 = Marine)
Bits 59–56 Reserved
Bits 55–42 Device class (e.g., 60 = Navigation)
Bits 41–35 Device function (e.g., 145 = GPS)
Bits 34–32 Device instance upper
Bits 31–24 Device instance lower + Manufacturer code (11 bits)
Bits 21–0  Identity number (21 bits, unique per manufacturer)
```

### Address Claim Procedure

```
1. Node selects a preferred address (often from EEPROM or default).
2. Node sends "Address Claim" PGN 0xEE00 with its NAME, source = preferred address.
3. All nodes receiving this check if they own that address with a lower NAME value.
4. If a conflict exists, the node with the higher NAME must select a new address and re-claim.
5. If no conflict within 250 ms, the address is successfully claimed.
6. Nodes must also respond to ISO Request (PGN 0xEA00) for their address claim.
```

---

## Key PGNs and Their Formats

### PGN 129025 — GNSS Position Rapid Update

Transmitted up to 10 Hz. 8 bytes, single CAN frame.

```
Byte 0–3: Latitude   (int32, 1e-7 degrees, signed)
Byte 4–7: Longitude  (int32, 1e-7 degrees, signed)
```

### PGN 129026 — COG & SOG Rapid Update

```
Byte 0:   SID (Sequence ID, for data correlation)
Byte 1:   COG Reference (bits 1–0: 0=True, 1=Magnetic)
Byte 2–3: COG (uint16, 1e-4 radians, range 0–2π)
Byte 4–5: SOG (uint16, 1e-2 m/s)
Byte 6–7: Reserved
```

### PGN 128267 — Water Depth

```
Byte 0:   SID
Byte 1–4: Depth (uint32, 1e-2 m)
Byte 5–6: Offset (int16, 1e-3 m, transducer offset)
Byte 7:   Reserved
```

### PGN 130306 — Wind Data

```
Byte 0:   SID
Byte 1–2: Wind Speed    (uint16, 1e-2 m/s)
Byte 3–4: Wind Angle    (uint16, 1e-4 rad)
Byte 5:   Reference     (bits 2–0: 0=True,1=Magnetic,2=Apparent,3=True vessel)
Byte 6–7: Reserved
```

---

## Programming in C/C++

### 1. CAN Frame Basics and PGN Extraction

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── 29-bit extended CAN ID layout for NMEA 2000 / J1939 ── */

typedef struct {
    uint8_t  priority;    /* 0 (highest) – 7 (lowest) */
    uint8_t  data_page;   /* 0 or 1 */
    uint32_t pgn;         /* full PGN (17 or 18 bits) */
    uint8_t  dest_addr;   /* 0xFF = broadcast / PDU2 */
    uint8_t  src_addr;
} N2K_Header;

/** Decode a 29-bit extended CAN ID into an NMEA 2000 header. */
N2K_Header n2k_decode_id(uint32_t can_id) {
    N2K_Header h;
    h.src_addr  = (uint8_t)(can_id & 0xFF);
    h.priority  = (uint8_t)((can_id >> 26) & 0x07);
    h.data_page = (uint8_t)((can_id >> 24) & 0x01);

    uint8_t pdu_format = (uint8_t)((can_id >> 16) & 0xFF);  /* PS high byte */

    if (pdu_format >= 0xF0) {
        /* PDU2 — broadcast PGN; destination bits are part of PGN */
        h.dest_addr = 0xFF;
        h.pgn = (uint32_t)((can_id >> 8) & 0x01FFFF);
        /* include data page bit */
        h.pgn |= ((uint32_t)h.data_page << 17);
    } else {
        /* PDU1 — peer-to-peer; bits 15-8 are destination address */
        h.dest_addr = (uint8_t)((can_id >> 8) & 0xFF);
        h.pgn = ((uint32_t)h.data_page << 17) |
                ((uint32_t)pdu_format   << 8);
    }
    return h;
}

/** Encode an NMEA 2000 header back into a 29-bit CAN ID. */
uint32_t n2k_encode_id(const N2K_Header *h) {
    uint32_t id = 0;
    id |= ((uint32_t)(h->priority  & 0x07) << 26);
    id |= ((uint32_t)(h->data_page & 0x01) << 24);
    if (h->dest_addr == 0xFF) {
        /* PDU2 — broadcast */
        id |= ((h->pgn & 0x1FF00) << 8);          /* PGN bits 16–8 */
        id |= ((h->pgn & 0x000FF) << 8);           /* PGN bits 7–0 as PS */
    } else {
        /* PDU1 — addressed */
        id |= ((uint32_t)(h->pgn & 0xFF00));       /* PF in bits 23–16 */
        id |= ((uint32_t)(h->dest_addr) << 8);
    }
    id |= h->src_addr;
    return id;
}
```

---

### 2. Fast-Packet Reassembly

```c
#define N2K_MAX_FAST_PACKET_PAYLOAD  223
#define N2K_MAX_FAST_PACKET_SESSIONS  8   /* simultaneous reassemblies */

typedef struct {
    uint32_t pgn;
    uint8_t  src;
    uint8_t  seq;                    /* 3-bit sequence counter */
    uint8_t  total_len;
    uint8_t  received_frames;
    uint8_t  buf[N2K_MAX_FAST_PACKET_PAYLOAD];
    bool     active;
} FPSession;

static FPSession fp_sessions[N2K_MAX_FAST_PACKET_SESSIONS];

/** Find or allocate a fast-packet session. Returns NULL if no slot. */
static FPSession *fp_find_session(uint32_t pgn, uint8_t src, uint8_t seq) {
    for (int i = 0; i < N2K_MAX_FAST_PACKET_SESSIONS; i++) {
        if (fp_sessions[i].active &&
            fp_sessions[i].pgn == pgn &&
            fp_sessions[i].src == src &&
            fp_sessions[i].seq == seq)
            return &fp_sessions[i];
    }
    return NULL;
}

static FPSession *fp_alloc_session(void) {
    for (int i = 0; i < N2K_MAX_FAST_PACKET_SESSIONS; i++) {
        if (!fp_sessions[i].active) {
            memset(&fp_sessions[i], 0, sizeof(FPSession));
            fp_sessions[i].active = true;
            return &fp_sessions[i];
        }
    }
    return NULL;  /* all sessions busy */
}

typedef void (*N2KMessageCallback)(uint32_t pgn, uint8_t src,
                                   const uint8_t *payload, uint8_t len);

/**
 * Feed a raw CAN frame into the fast-packet reassembler.
 * Returns true if a complete message was assembled and cb was called.
 *
 * @param pgn      PGN decoded from the CAN ID
 * @param src      Source address
 * @param data     CAN frame data bytes (up to 8)
 * @param data_len Number of data bytes (usually 8)
 * @param cb       Callback invoked with the completed message
 */
bool n2k_fast_packet_feed(uint32_t pgn, uint8_t src,
                          const uint8_t *data, uint8_t data_len,
                          N2KMessageCallback cb) {
    if (data_len < 2) return false;

    uint8_t frame_ctr = data[0] & 0x1F;   /* bits 4–0 */
    uint8_t seq_ctr   = (data[0] >> 5) & 0x07; /* bits 7–5 */

    if (frame_ctr == 0) {
        /* First frame of a fast-packet message */
        uint8_t total = data[1];
        FPSession *s = fp_alloc_session();
        if (!s) return false;
        s->pgn       = pgn;
        s->src       = src;
        s->seq       = seq_ctr;
        s->total_len = total;
        s->received_frames = 1;
        /* First frame carries 6 bytes of payload (bytes 2–7) */
        uint8_t copy = (total < 6) ? total : 6;
        memcpy(s->buf, data + 2, copy);
    } else {
        /* Subsequent frame */
        FPSession *s = fp_find_session(pgn, src, seq_ctr);
        if (!s) return false;
        uint8_t offset = 6 + (frame_ctr - 1) * 7;
        uint8_t remaining = s->total_len - offset;
        uint8_t copy = (remaining < 7) ? remaining : 7;
        if (offset + copy > N2K_MAX_FAST_PACKET_PAYLOAD) {
            s->active = false;
            return false;
        }
        memcpy(s->buf + offset, data + 1, copy);
        s->received_frames++;

        uint8_t expected = (s->total_len - 6 + 6) / 7 + 1; /* total frames */
        if (s->received_frames >= expected) {
            /* Message complete */
            cb(s->pgn, s->src, s->buf, s->total_len);
            s->active = false;
            return true;
        }
    }
    return false;
}
```

---

### 3. Decoding Common PGNs

```c
#include <math.h>

/* ── PGN 129025: GNSS Position Rapid Update ── */
typedef struct {
    double latitude;   /* degrees, + = North */
    double longitude;  /* degrees, + = East  */
} PGN129025;

bool decode_pgn_129025(const uint8_t *data, uint8_t len, PGN129025 *out) {
    if (len < 8) return false;
    int32_t raw_lat, raw_lon;
    memcpy(&raw_lat, data + 0, 4);
    memcpy(&raw_lon, data + 4, 4);
    out->latitude  = raw_lat  * 1e-7;
    out->longitude = raw_lon  * 1e-7;
    return true;
}

/* ── PGN 129026: COG & SOG Rapid Update ── */
typedef struct {
    uint8_t  sid;
    uint8_t  cog_ref;   /* 0=True, 1=Magnetic */
    double   cog_deg;   /* course over ground, degrees */
    double   sog_ms;    /* speed over ground, m/s */
} PGN129026;

bool decode_pgn_129026(const uint8_t *data, uint8_t len, PGN129026 *out) {
    if (len < 8) return false;
    out->sid     = data[0];
    out->cog_ref = data[1] & 0x03;
    uint16_t raw_cog, raw_sog;
    memcpy(&raw_cog, data + 2, 2);
    memcpy(&raw_sog, data + 4, 2);
    /* COG: 1e-4 radians → degrees */
    out->cog_deg = (raw_cog == 0xFFFF) ? NAN : raw_cog * 1e-4 * (180.0 / M_PI);
    /* SOG: 1e-2 m/s */
    out->sog_ms  = (raw_sog == 0xFFFF) ? NAN : raw_sog * 0.01;
    return true;
}

/* ── PGN 128267: Water Depth ── */
typedef struct {
    uint8_t sid;
    double  depth_m;   /* depth from transducer, metres */
    double  offset_m;  /* transducer offset (+ = below waterline) */
} PGN128267;

bool decode_pgn_128267(const uint8_t *data, uint8_t len, PGN128267 *out) {
    if (len < 7) return false;
    out->sid = data[0];
    uint32_t raw_depth;
    int16_t  raw_offset;
    memcpy(&raw_depth,  data + 1, 4);
    memcpy(&raw_offset, data + 5, 2);
    out->depth_m  = (raw_depth  == 0xFFFFFFFF) ? NAN : raw_depth  * 0.01;
    out->offset_m = (raw_offset == (int16_t)0x7FFF) ? NAN : raw_offset * 0.001;
    return true;
}

/* ── PGN 130306: Wind Data ── */
typedef struct {
    uint8_t sid;
    double  speed_ms;
    double  angle_deg;
    uint8_t reference;  /* 0=True,1=Magnetic,2=Apparent,3=True vessel */
} PGN130306;

bool decode_pgn_130306(const uint8_t *data, uint8_t len, PGN130306 *out) {
    if (len < 6) return false;
    out->sid = data[0];
    uint16_t raw_speed, raw_angle;
    memcpy(&raw_speed, data + 1, 2);
    memcpy(&raw_angle, data + 3, 2);
    out->speed_ms  = (raw_speed == 0xFFFF) ? NAN : raw_speed * 0.01;
    out->angle_deg = (raw_angle == 0xFFFF) ? NAN : raw_angle * 1e-4 * (180.0 / M_PI);
    out->reference = data[5] & 0x07;
    return true;
}
```

---

### 4. Address Claiming

```c
#include <stdint.h>
#include <stdbool.h>

#define PGN_ISO_ADDRESS_CLAIM  0x0EE00UL
#define N2K_GLOBAL_ADDR        0xFF

/* NAME field structure — 8 bytes, little-endian on the bus */
typedef struct {
    uint32_t identity_number    : 21; /* unique per manufacturer */
    uint32_t manufacturer_code  : 11; /* NMEA assigned */
    uint8_t  device_instance_lo :  4;
    uint8_t  device_instance_hi :  3;
    uint8_t  device_function    ;     /* 8 bits */
    uint8_t  device_class       :  7;
    uint8_t  reserved           :  1;
    uint8_t  industry_group     :  4; /* 4 = Marine */
    uint8_t  self_config_addr   :  1;
    uint8_t  _pad               :  3;
} N2K_NAME;

/* Serialise NAME to 8 bus bytes (little-endian) */
void n2k_name_to_bytes(const N2K_NAME *n, uint8_t out[8]) {
    uint64_t v = 0;
    v |= ((uint64_t)(n->identity_number   & 0x1FFFFF));
    v |= ((uint64_t)(n->manufacturer_code & 0x7FF)   << 21);
    v |= ((uint64_t)(n->device_instance_lo & 0xF)    << 32);
    v |= ((uint64_t)(n->device_instance_hi & 0x7)    << 36);
    v |= ((uint64_t)(n->device_function)              << 40);
    v |= ((uint64_t)(n->device_class & 0x7F)          << 49);
    v |= ((uint64_t)(n->industry_group & 0xF)         << 57);
    v |= ((uint64_t)(n->self_config_addr & 0x01)      << 63);
    memcpy(out, &v, 8);
}

typedef struct {
    uint8_t  claimed_addr;
    N2K_NAME name;
    bool     address_claimed;
    /* platform-specific: send_can_frame(id, data, len) */
} N2K_Node;

/** Broadcast an Address Claim frame. */
void n2k_send_address_claim(N2K_Node *node,
    void (*send_frame)(uint32_t id, const uint8_t *data, uint8_t len))
{
    N2K_Header h = {
        .priority  = 6,
        .data_page = 0,
        .pgn       = PGN_ISO_ADDRESS_CLAIM,
        .dest_addr = 0xFF,
        .src_addr  = node->claimed_addr
    };
    uint8_t data[8];
    n2k_name_to_bytes(&node->name, data);
    uint32_t id = n2k_encode_id(&h);
    send_frame(id, data, 8);
}

/** Handle an incoming Address Claim frame. */
void n2k_handle_address_claim(N2K_Node *node, uint8_t src_addr,
    const uint8_t *data,
    void (*send_frame)(uint32_t id, const uint8_t *data, uint8_t len))
{
    if (src_addr != node->claimed_addr) return; /* not our address */

    uint64_t their_name, our_name;
    memcpy(&their_name, data, 8);

    uint8_t our_bytes[8];
    n2k_name_to_bytes(&node->name, our_bytes);
    memcpy(&our_name, our_bytes, 8);

    if (our_name < their_name) {
        /* We win — re-assert our claim */
        n2k_send_address_claim(node, send_frame);
    } else {
        /* We lose — try next address */
        if (node->claimed_addr < 0xFE) {
            node->claimed_addr++;
        } else {
            node->claimed_addr = 0x00; /* wrap (self-configurable) */
        }
        node->address_claimed = false;
        n2k_send_address_claim(node, send_frame);
    }
}
```

---

### 5. Transmitting a PGN (Single-Frame)

```cpp
// C++ example using SocketCAN on Linux
#include <cstdio>
#include <cstring>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <unistd.h>

class N2KSocketCAN {
public:
    int sock = -1;

    bool open(const char *iface) {
        sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock < 0) return false;
        struct ifreq ifr{};
        strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        ioctl(sock, SIOCGIFINDEX, &ifr);
        struct sockaddr_can addr{};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        return bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == 0;
    }

    /** Send a single-frame NMEA 2000 message (≤ 8 bytes). */
    bool send_pgn(uint8_t priority, uint32_t pgn, uint8_t src,
                  uint8_t dest, const uint8_t *payload, uint8_t len) {
        if (len > 8) return false;
        struct can_frame frame{};
        frame.can_dlc = len;
        memcpy(frame.data, payload, len);

        /* Build 29-bit ID */
        uint32_t id = ((uint32_t)(priority & 0x07) << 26);
        uint8_t pf  = (pgn >> 8) & 0xFF;
        if (pf >= 0xF0) {
            /* PDU2 broadcast */
            id |= ((pgn & 0x1FFFF) << 8);
        } else {
            /* PDU1 addressed */
            id |= ((uint32_t)(pf)   << 16);
            id |= ((uint32_t)(dest) <<  8);
        }
        id |= src;
        frame.can_id = id | CAN_EFF_FLAG;
        return write(sock, &frame, sizeof(frame)) == sizeof(frame);
    }

    /** Transmit PGN 129026 — COG & SOG. */
    void send_cog_sog(uint8_t src, uint8_t sid,
                      double cog_deg, double sog_ms) {
        uint8_t data[8] = {};
        data[0] = sid;
        data[1] = 0x00; /* True reference */
        auto raw_cog = (uint16_t)(cog_deg * M_PI / 180.0 / 1e-4);
        auto raw_sog = (uint16_t)(sog_ms / 0.01);
        memcpy(data + 2, &raw_cog, 2);
        memcpy(data + 4, &raw_sog, 2);
        data[6] = 0xFF; data[7] = 0xFF; /* reserved */
        send_pgn(2, 129026, src, 0xFF, data, 8);
    }

    ~N2KSocketCAN() { if (sock >= 0) close(sock); }
};
```

---

### 6. Receiving and Dispatching in C++

```cpp
#include <functional>
#include <unordered_map>
#include <vector>
#include <cstdint>

/** Dispatcher that routes decoded PGN payloads to handlers. */
class N2KDispatcher {
public:
    using Handler = std::function<void(uint8_t src,
                                       const uint8_t *data, uint8_t len)>;

    void register_pgn(uint32_t pgn, Handler h) {
        handlers_[pgn].push_back(std::move(h));
    }

    void dispatch(uint32_t pgn, uint8_t src,
                  const uint8_t *data, uint8_t len) {
        auto it = handlers_.find(pgn);
        if (it != handlers_.end())
            for (auto &h : it->second) h(src, data, len);
    }

private:
    std::unordered_map<uint32_t, std::vector<Handler>> handlers_;
};

/* --- Usage example --- */
int main() {
    N2KSocketCAN can;
    if (!can.open("can0")) { perror("open"); return 1; }

    N2KDispatcher disp;

    /* Register handler for Water Depth */
    disp.register_pgn(128267, [](uint8_t src, const uint8_t *d, uint8_t len) {
        PGN128267 depth{};
        if (decode_pgn_128267(d, len, &depth))
            printf("Depth from 0x%02X: %.2f m (offset %.3f m)\n",
                   src, depth.depth_m, depth.offset_m);
    });

    /* Register handler for Wind */
    disp.register_pgn(130306, [](uint8_t src, const uint8_t *d, uint8_t len) {
        PGN130306 wind{};
        if (decode_pgn_130306(d, len, &wind))
            printf("Wind from 0x%02X: %.1f m/s @ %.1f°\n",
                   src, wind.speed_ms, wind.angle_deg);
    });

    /* RX loop */
    FPSession sessions[N2K_MAX_FAST_PACKET_SESSIONS] = {};
    struct can_frame frame{};
    while (read(can.sock, &frame, sizeof(frame)) > 0) {
        uint32_t raw_id = frame.can_id & CAN_EFF_MASK;
        N2K_Header h = n2k_decode_id(raw_id);

        /* Determine if this PGN uses fast-packet */
        bool is_fast_packet = (h.pgn == 128267 || h.pgn == 130306
                               || h.pgn == 129029 /* GNSS data */);

        if (is_fast_packet) {
            n2k_fast_packet_feed(h.pgn, h.src_addr,
                                 frame.data, frame.can_dlc,
                [&](uint32_t pgn, uint8_t src, const uint8_t *pl, uint8_t l) {
                    disp.dispatch(pgn, src, pl, l);
                });
        } else {
            disp.dispatch(h.pgn, h.src_addr, frame.data, frame.can_dlc);
        }
    }
}
```

---

## Programming in Rust

### 1. Dependencies (Cargo.toml)

```toml
[package]
name = "nmea2000"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan   = "3"          # SocketCAN bindings for Linux
byteorder   = "1"          # Little-endian integer parsing
thiserror   = "1"          # Error derive macro
log         = "0.4"
env_logger  = "0.11"
```

---

### 2. Core Types and CAN ID Parsing

```rust
//! nmea2000/src/header.rs

use std::fmt;

/// Decoded NMEA 2000 / J1939 29-bit CAN identifier.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct N2kHeader {
    pub priority:  u8,
    pub data_page: u8,
    pub pgn:       u32,
    pub dest_addr: u8,   // 0xFF = broadcast
    pub src_addr:  u8,
}

impl N2kHeader {
    /// Decode a raw 29-bit extended CAN ID.
    pub fn from_can_id(id: u32) -> Self {
        let src_addr  = (id & 0xFF) as u8;
        let priority  = ((id >> 26) & 0x07) as u8;
        let data_page = ((id >> 24) & 0x01) as u8;
        let pdu_fmt   = ((id >> 16) & 0xFF) as u8;   // PDU Format byte

        if pdu_fmt >= 0xF0 {
            // PDU2 — broadcast
            let pgn = ((id >> 8) & 0x01FFFF) | ((data_page as u32) << 17);
            N2kHeader { priority, data_page, pgn, dest_addr: 0xFF, src_addr }
        } else {
            // PDU1 — addressed
            let dest_addr = ((id >> 8) & 0xFF) as u8;
            let pgn = ((data_page as u32) << 17) | ((pdu_fmt as u32) << 8);
            N2kHeader { priority, data_page, pgn, dest_addr, src_addr }
        }
    }

    /// Encode back to a 29-bit CAN ID.
    pub fn to_can_id(self) -> u32 {
        let mut id: u32 = 0;
        id |= (self.priority as u32 & 0x07) << 26;
        id |= (self.data_page as u32 & 0x01) << 24;
        let pf = ((self.pgn >> 8) & 0xFF) as u8;
        if self.dest_addr == 0xFF {
            id |= (self.pgn & 0x1FFFF) << 8;
        } else {
            id |= (pf as u32) << 16;
            id |= (self.dest_addr as u32) << 8;
        }
        id |= self.src_addr as u32;
        id
    }
}

impl fmt::Display for N2kHeader {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "PGN={} src=0x{:02X} dst=0x{:02X} prio={}",
               self.pgn, self.src_addr, self.dest_addr, self.priority)
    }
}
```

---

### 3. PGN Decoders

```rust
//! nmea2000/src/pgn.rs

use byteorder::{ByteOrder, LittleEndian};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum N2kError {
    #[error("payload too short: need {need} bytes, got {got}")]
    TooShort { need: usize, got: usize },
    #[error("reserved / unavailable value")]
    Unavailable,
}

// ── PGN 129025: GNSS Position Rapid Update ────────────────────────────────

#[derive(Debug, Clone, Copy)]
pub struct GnssPosition {
    pub latitude:  f64,   // degrees (+N / -S)
    pub longitude: f64,   // degrees (+E / -W)
}

impl GnssPosition {
    pub const PGN: u32 = 129_025;

    pub fn decode(data: &[u8]) -> Result<Self, N2kError> {
        if data.len() < 8 { return Err(N2kError::TooShort { need: 8, got: data.len() }); }
        let raw_lat = LittleEndian::read_i32(&data[0..4]);
        let raw_lon = LittleEndian::read_i32(&data[4..8]);
        Ok(Self {
            latitude:  raw_lat as f64 * 1e-7,
            longitude: raw_lon as f64 * 1e-7,
        })
    }
}

// ── PGN 129026: COG & SOG Rapid Update ───────────────────────────────────

#[derive(Debug, Clone, Copy)]
pub struct CogSog {
    pub sid:       u8,
    pub cog_ref:   u8,      // 0=True, 1=Magnetic
    pub cog_deg:   Option<f64>,
    pub sog_ms:    Option<f64>,
}

impl CogSog {
    pub const PGN: u32 = 129_026;

    pub fn decode(data: &[u8]) -> Result<Self, N2kError> {
        if data.len() < 8 { return Err(N2kError::TooShort { need: 8, got: data.len() }); }
        let sid     = data[0];
        let cog_ref = data[1] & 0x03;
        let raw_cog = LittleEndian::read_u16(&data[2..4]);
        let raw_sog = LittleEndian::read_u16(&data[4..6]);
        Ok(Self {
            sid,
            cog_ref,
            cog_deg: if raw_cog == 0xFFFF { None }
                     else { Some(raw_cog as f64 * 1e-4 * 180.0 / std::f64::consts::PI) },
            sog_ms:  if raw_sog == 0xFFFF { None }
                     else { Some(raw_sog as f64 * 0.01) },
        })
    }
}

// ── PGN 128267: Water Depth ───────────────────────────────────────────────

#[derive(Debug, Clone, Copy)]
pub struct WaterDepth {
    pub sid:      u8,
    pub depth_m:  Option<f64>,
    pub offset_m: Option<f64>,
}

impl WaterDepth {
    pub const PGN: u32 = 128_267;

    pub fn decode(data: &[u8]) -> Result<Self, N2kError> {
        if data.len() < 7 { return Err(N2kError::TooShort { need: 7, got: data.len() }); }
        let raw_depth  = LittleEndian::read_u32(&data[1..5]);
        let raw_offset = LittleEndian::read_i16(&data[5..7]);
        Ok(Self {
            sid:      data[0],
            depth_m:  if raw_depth == 0xFFFF_FFFF { None }
                      else { Some(raw_depth as f64 * 0.01) },
            offset_m: if raw_offset == 0x7FFF { None }
                      else { Some(raw_offset as f64 * 0.001) },
        })
    }
}

// ── PGN 130306: Wind Data ─────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WindReference { True, Magnetic, Apparent, TrueVessel, Unknown(u8) }

impl From<u8> for WindReference {
    fn from(v: u8) -> Self {
        match v & 0x07 {
            0 => Self::True, 1 => Self::Magnetic,
            2 => Self::Apparent, 3 => Self::TrueVessel,
            n => Self::Unknown(n),
        }
    }
}

#[derive(Debug, Clone, Copy)]
pub struct WindData {
    pub sid:       u8,
    pub speed_ms:  Option<f64>,
    pub angle_deg: Option<f64>,
    pub reference: WindReference,
}

impl WindData {
    pub const PGN: u32 = 130_306;

    pub fn decode(data: &[u8]) -> Result<Self, N2kError> {
        if data.len() < 6 { return Err(N2kError::TooShort { need: 6, got: data.len() }); }
        let raw_speed = LittleEndian::read_u16(&data[1..3]);
        let raw_angle = LittleEndian::read_u16(&data[3..5]);
        Ok(Self {
            sid:       data[0],
            speed_ms:  if raw_speed == 0xFFFF { None }
                       else { Some(raw_speed as f64 * 0.01) },
            angle_deg: if raw_angle == 0xFFFF { None }
                       else { Some(raw_angle as f64 * 1e-4 * 180.0 / std::f64::consts::PI) },
            reference: WindReference::from(data[5]),
        })
    }
}
```

---

### 4. Fast-Packet Reassembler

```rust
//! nmea2000/src/fast_packet.rs

use std::collections::HashMap;

const MAX_PAYLOAD: usize = 223;

#[derive(Default)]
struct Session {
    total_len: u8,
    buf: [u8; MAX_PAYLOAD],
    next_frame: u8,
}

/// Fast-packet reassembler keyed by (PGN, source address, sequence counter).
pub struct FastPacketReassembler {
    sessions: HashMap<(u32, u8, u8), Session>,
}

impl FastPacketReassembler {
    pub fn new() -> Self {
        Self { sessions: HashMap::new() }
    }

    /// Feed a CAN frame.  Returns the completed payload when all frames arrive.
    pub fn feed(&mut self, pgn: u32, src: u8, data: &[u8])
        -> Option<(u32, u8, Vec<u8>)>
    {
        if data.is_empty() { return None; }
        let frame_ctr = data[0] & 0x1F;
        let seq_ctr   = (data[0] >> 5) & 0x07;
        let key = (pgn, src, seq_ctr);

        if frame_ctr == 0 {
            // First frame
            if data.len() < 2 { return None; }
            let total = data[1] as usize;
            let mut s = Session { total_len: data[1], ..Default::default() };
            let copy = total.min(6).min(data.len().saturating_sub(2));
            s.buf[..copy].copy_from_slice(&data[2..2 + copy]);
            s.next_frame = 1;
            self.sessions.insert(key, s);
            None
        } else {
            let s = self.sessions.get_mut(&key)?;
            if frame_ctr != s.next_frame { return None; }
            let offset = 6 + (frame_ctr as usize - 1) * 7;
            let remaining = s.total_len as usize - offset;
            let copy = remaining.min(7).min(data.len().saturating_sub(1));
            if offset + copy > MAX_PAYLOAD { return None; }
            s.buf[offset..offset + copy].copy_from_slice(&data[1..1 + copy]);
            s.next_frame += 1;

            // Check completion
            let total = s.total_len as usize;
            let frames_needed = 1 + (total.saturating_sub(6) + 6) / 7;
            if s.next_frame as usize >= frames_needed {
                let payload = s.buf[..total].to_vec();
                self.sessions.remove(&key);
                Some((pgn, src, payload))
            } else {
                None
            }
        }
    }
}
```

---

### 5. SocketCAN Integration and Dispatcher

```rust
//! nmea2000/src/main.rs

use socketcan::{CanSocket, Socket, ExtendedId};
use std::collections::HashMap;

mod header;  mod pgn;  mod fast_packet;

use header::N2kHeader;
use pgn::{GnssPosition, CogSog, WaterDepth, WindData};
use fast_packet::FastPacketReassembler;

/// PGNs that use NMEA 2000 fast-packet framing (≤ 223 bytes, multi-frame).
const FAST_PACKET_PGNS: &[u32] = &[
    WaterDepth::PGN, WindData::PGN,
    129_029,   // GNSS Position Data (extended, multi-frame)
    127_489,   // Engine Parameters (rapid)
];

fn is_fast_packet(pgn: u32) -> bool {
    FAST_PACKET_PGNS.contains(&pgn)
}

fn dispatch(pgn: u32, src: u8, data: &[u8]) {
    match pgn {
        GnssPosition::PGN => {
            if let Ok(pos) = GnssPosition::decode(data) {
                println!("[GPS  0x{src:02X}] lat={:.6}° lon={:.6}°",
                         pos.latitude, pos.longitude);
            }
        }
        CogSog::PGN => {
            if let Ok(cs) = CogSog::decode(data) {
                println!("[COG  0x{src:02X}] cog={:.1}° sog={:.2}m/s",
                         cs.cog_deg.unwrap_or(f64::NAN),
                         cs.sog_ms.unwrap_or(f64::NAN));
            }
        }
        WaterDepth::PGN => {
            if let Ok(d) = WaterDepth::decode(data) {
                println!("[DPTH 0x{src:02X}] depth={:.2}m offset={:.3}m",
                         d.depth_m.unwrap_or(f64::NAN),
                         d.offset_m.unwrap_or(f64::NAN));
            }
        }
        WindData::PGN => {
            if let Ok(w) = WindData::decode(data) {
                println!("[WIND 0x{src:02X}] {:.1}m/s @ {:.1}° ({:?})",
                         w.speed_ms.unwrap_or(f64::NAN),
                         w.angle_deg.unwrap_or(f64::NAN),
                         w.reference);
            }
        }
        other => {
            println!("[PGN  0x{src:02X}] PGN={other} ({} bytes)", data.len());
        }
    }
}

fn main() -> anyhow::Result<()> {
    env_logger::init();
    let sock = CanSocket::open("can0")?;
    let mut fp = FastPacketReassembler::new();

    loop {
        let frame = sock.read_frame()?;
        let raw_id = match frame.id() {
            socketcan::Id::Extended(eid) => eid.as_raw(),
            _ => continue,   // ignore standard frames
        };

        let hdr = N2kHeader::from_can_id(raw_id);
        let data = frame.data();

        if is_fast_packet(hdr.pgn) {
            if let Some((pgn, src, payload)) = fp.feed(hdr.pgn, hdr.src_addr, data) {
                dispatch(pgn, src, &payload);
            }
        } else {
            dispatch(hdr.pgn, hdr.src_addr, data);
        }
    }
}
```

---

### 6. Encoding and Transmitting in Rust

```rust
//! Encoding and sending PGN 129026 (COG & SOG) in Rust.

use byteorder::{ByteOrder, LittleEndian};
use socketcan::{CanSocket, CanFrame, Socket, ExtendedId, Id};

fn encode_cog_sog(sid: u8, cog_deg: f64, sog_ms: f64) -> [u8; 8] {
    let mut data = [0u8; 8];
    data[0] = sid;
    data[1] = 0x00;  // True reference
    let raw_cog = (cog_deg * std::f64::consts::PI / 180.0 / 1e-4) as u16;
    let raw_sog = (sog_ms / 0.01) as u16;
    LittleEndian::write_u16(&mut data[2..4], raw_cog);
    LittleEndian::write_u16(&mut data[4..6], raw_sog);
    data[6] = 0xFF; data[7] = 0xFF;
    data
}

fn send_cog_sog(sock: &CanSocket, src: u8, sid: u8,
                cog_deg: f64, sog_ms: f64) -> anyhow::Result<()> {
    let hdr = N2kHeader {
        priority: 2, data_page: 0, pgn: CogSog::PGN,
        dest_addr: 0xFF, src_addr: src,
    };
    let raw_id = hdr.to_can_id();
    let data = encode_cog_sog(sid, cog_deg, sog_ms);
    let eid = ExtendedId::new(raw_id).ok_or(anyhow::anyhow!("bad ID"))?;
    let frame = CanFrame::new(Id::Extended(eid), &data)?;
    sock.write_frame(&frame)?;
    Ok(())
}
```

---

## Gateway and Bridging Patterns

### NMEA 0183 → NMEA 2000 Bridge

Many legacy instruments still speak NMEA 0183. A common embedded pattern reads NMEA 0183 sentences over UART and re-publishes them as N2K PGNs:

```c
/* Pseudocode gateway skeleton (C) */
void uart_line_received(const char *sentence) {
    if (strncmp(sentence, "$GPGLL", 6) == 0 ||
        strncmp(sentence, "$GNGLL", 6) == 0) {
        double lat, lon;
        if (parse_gll(sentence, &lat, &lon)) {
            uint8_t data[8];
            int32_t raw_lat = (int32_t)(lat * 1e7);
            int32_t raw_lon = (int32_t)(lon * 1e7);
            memcpy(data + 0, &raw_lat, 4);
            memcpy(data + 4, &raw_lon, 4);
            can_send_pgn(2, 129025, MY_ADDR, 0xFF, data, 8);
        }
    }
    /* ... handle $WIMWV for wind, $SDDBT for depth, etc. */
}
```

### Signal K Integration

Signal K is a modern open-source marine data layer that bridges NMEA 2000 and web/cloud services. The **signalk-server** Node.js application connects to a SocketCAN interface, parses N2K frames via the `@canboat/analyzer` library, and exposes all vessel data over WebSocket and REST APIs.

```bash
# Install Signal K server
npm install -g @signalk/server

# Start with CAN interface
signalk-server --interfaces can0
```

---

## Testing and Simulation

### Virtual CAN Interface (Linux)

```bash
# Load the vcan kernel module
sudo modprobe vcan

# Create a virtual interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Send a raw test frame (PGN 129026 example)
# CAN ID: priority=2 (bits 28-26=010), PGN=129026 (0x1F802), src=0x22
# 29-bit ID = 0x09F80222
cansend vcan0 09F80222#00002A1A641800FF

# Monitor all frames
candump vcan0 -L
```

### Using canboat for Decoding

The open-source **canboat** suite provides `analyzer` — a tool that decodes raw NMEA 2000 CAN frames into human-readable JSON:

```bash
# Install canboat
git clone https://github.com/canboat/canboat
cd canboat && make

# Pipe candump output through the analyzer
candump vcan0 | ./analyzer -json
```

Example output:
```json
{
  "timestamp": "2024-01-15T10:32:45.001Z",
  "prio": 2,
  "src": 34,
  "dst": 255,
  "pgn": 129026,
  "description": "COG & SOG Rapid Update",
  "fields": {
    "SID": 0,
    "COG Reference": "True",
    "COG": 185.2,
    "SOG": 3.45
  }
}
```

### Unit-Testing PGN Decoders in Rust

```rust
#[cfg(test)]
mod tests {
    use super::pgn::*;

    #[test]
    fn test_cog_sog_decode() {
        // SID=0, ref=True, COG=180° (π rad = 31416 × 1e-4), SOG=3.0m/s
        let cog_raw = (std::f64::consts::PI / 1e-4) as u16;  // 31416
        let sog_raw: u16 = 300;  // 3.00 m/s
        let mut data = [0u8; 8];
        data[0] = 0; data[1] = 0;
        data[2..4].copy_from_slice(&cog_raw.to_le_bytes());
        data[4..6].copy_from_slice(&sog_raw.to_le_bytes());
        data[6] = 0xFF; data[7] = 0xFF;

        let cs = CogSog::decode(&data).unwrap();
        assert!((cs.cog_deg.unwrap() - 180.0).abs() < 0.01);
        assert!((cs.sog_ms.unwrap() - 3.00).abs() < 0.01);
    }

    #[test]
    fn test_water_depth_decode() {
        let depth_raw: u32 = 1234;   // 12.34 m
        let offset_raw: i16 = -500;  // -0.500 m
        let mut data = [0u8; 7];
        data[0] = 1;
        data[1..5].copy_from_slice(&depth_raw.to_le_bytes());
        data[5..7].copy_from_slice(&offset_raw.to_le_bytes());

        let d = WaterDepth::decode(&data).unwrap();
        assert!((d.depth_m.unwrap() - 12.34).abs() < 0.001);
        assert!((d.offset_m.unwrap() - (-0.5)).abs() < 0.001);
    }

    #[test]
    fn test_unavailable_values() {
        let data = [0u8, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF];
        let d = WaterDepth::decode(&data).unwrap();
        assert!(d.depth_m.is_none());
        assert!(d.offset_m.is_none());
    }
}
```

---

## Summary

NMEA 2000 is the dominant standard for modern marine instrumentation networking. Built on the well-proven CAN 2.0B physical layer at 250 kbit/s, it combines the J1939 address-claiming and identifier structure with a marine-specific application layer organised around **Parameter Group Numbers (PGNs)**.

Key architectural points to remember:

- The 29-bit CAN ID encodes **priority**, **data page**, **PGN** (or destination address for PDU1 messages), and **source address** in every frame.
- Single-frame PGNs carry up to 8 bytes; multi-byte PGNs (up to 223 bytes) use the **fast-packet** protocol with an 8-byte sequential counter and length byte in the first frame.
- Devices must participate in **address claiming** using their 8-byte NAME before transmitting on the bus, ensuring collision-free multi-master operation.
- The bus carries **power** (12 V) alongside CAN-H/CAN-L, simplifying device wiring.
- NMEA 2000 integrates cleanly with **Signal K**, **canboat**, and open-source chart-plotter software such as OpenCPN.

In **C/C++**, implementation typically involves raw bit manipulation of the 29-bit ID, a fast-packet state machine keyed by (PGN, source, sequence), and either SocketCAN on Linux or a vendor HAL on embedded targets. In **Rust**, the `socketcan` crate provides an ergonomic async-capable interface, and the language's strong type system and `Result`-based error handling make PGN decoders robust against malformed or truncated data. Both ecosystems benefit from the wealth of open-source reference implementations — particularly **canboat/analyzer** — which serve as authoritative ground truth for PGN definitions and encoding details.

---

*References: NMEA 2000 Standard (NMEA.org), SAE J1939 Standard, canboat open-source project (github.com/canboat/canboat), OpenCPN, Signal K.*