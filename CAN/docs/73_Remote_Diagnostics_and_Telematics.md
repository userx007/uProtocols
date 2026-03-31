# 73. Remote Diagnostics and Telematics

**C/C++ examples:**
1. **CAN frame capture** via Linux SocketCAN with J1939 filter setup and PGN 61444 engine speed decoding.
2. **UDS ReadDataByIdentifier** — ISO-TP single-frame request/response for live ECU data (RPM, coolant temperature).
3. **MQTT publisher** using Eclipse Paho with mTLS certificate authentication and JSON payload serialization.
4. **Predictive maintenance** rolling statistics using Welford's online algorithm with Z-score anomaly detection.

**Rust examples:**
5. **Async CAN capture** using the `socketcan` crate and Tokio, with J1939 decode helpers.
6. **MQTT with mTLS** using `rumqttc` and `serde_json` for typed signal serialization.
7. **Anomaly detector** — idiomatic Rust port of the rolling stats engine with a `PredictiveMaintenance` struct.
8. **Store-and-forward buffer** using SQLite for offline persistence and atomic batch flush when connectivity resumes.

The summary ties together the full data flow: CAN acquisition → gateway enrichment → secured MQTT transport → cloud time-series analytics and predictive maintenance alerting.


## Bridging CAN Networks to Cloud Services for Fleet Monitoring and Predictive Maintenance

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [CAN Data Acquisition Layer](#can-data-acquisition-layer)
4. [Telematics Gateway Design](#telematics-gateway-design)
5. [Data Serialization and Protocol Adaptation](#data-serialization-and-protocol-adaptation)
6. [Cloud Communication Protocols](#cloud-communication-protocols)
7. [Fleet Monitoring](#fleet-monitoring)
8. [Predictive Maintenance](#predictive-maintenance)
9. [Security Considerations](#security-considerations)
10. [C/C++ Implementation Examples](#cc-implementation-examples)
11. [Rust Implementation Examples](#rust-implementation-examples)
12. [Summary](#summary)

---

## Introduction

Remote diagnostics and telematics represent the convergence of automotive/industrial CAN bus technology with modern cloud infrastructure. A **telematics system** collects data from vehicle or machine CAN networks, aggregates it in an embedded gateway, and transmits it over cellular, Wi-Fi, or satellite links to cloud backends for analysis, monitoring, and decision-making.

Key use cases include:

- **Fleet management**: Real-time GPS tracking, driver behavior scoring, fuel consumption monitoring.
- **Remote diagnostics**: Reading and clearing Diagnostic Trouble Codes (DTCs) over the air, reducing costly workshop visits.
- **Predictive maintenance**: Using historical and streaming CAN signals to forecast component failure before it occurs.
- **OTA firmware updates**: Distributing ECU software updates triggered by cloud orchestration.
- **Regulatory compliance**: Tachograph data retrieval, emissions reporting (OBD-II / Euro 6).

---

## Architecture Overview

```
┌──────────────────────────────────────────────────────────────────────┐
│                        Vehicle / Machine                             │
│                                                                      │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────────────┐   │
│  │ Engine   │   │ Gearbox  │   │ ABS/ESC  │   │ Body Control     │   │
│  │  ECU     │   │  TCU     │   │  Module  │   │  Module (BCM)    │   │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬─────────────┘   │
│       │              │              │               │                │
│  ─────┴──────────────┴──────────────┴───────────────┴──── CAN Bus    │
│                                     │                                │
│                          ┌──────────┴──────────┐                     │
│                          │  Telematics Gateway  │                    │
│                          │  (OBD-II / J1939 /   │                    │
│                          │   CANopen adapter)   │                    │
│                          └──────────┬───────────┘                    │
└─────────────────────────────────────┼────────────────────────────────┘
                                      │ LTE / 5G / Wi-Fi / Satellite
                          ┌───────────▼───────────┐
                          │   Cloud Backend       │
                          │  (MQTT Broker /       │
                          │   REST API /          │
                          │   AMQP / gRPC)        │
                          └───────────┬───────────┘
                                      │
              ┌───────────────────────┼──────────────────────┐
              │                       │                      │
   ┌──────────▼───────┐   ┌───────────▼─────────┐  ┌─────────▼───────┐
   │  Time-Series DB  │   │  Analytics Engine   │  │  Fleet Dashboard│
   │  (InfluxDB /     │   │  (ML / Rules Engine)│  │  (Web / Mobile) │
   │   TimescaleDB)   │   │                     │  │                 │
   └──────────────────┘   └─────────────────────┘  └─────────────────┘
```

The gateway acts as a **protocol bridge**: it speaks CAN on the vehicle side and IP-based protocols on the cloud side.

---

## CAN Data Acquisition Layer

The gateway subscribes to relevant CAN frames or actively polls ECUs using diagnostic protocols:

### Passive Monitoring (Sniffing)

The gateway listens to all CAN traffic matching a filter list and records signal values decoded from a **DBC (database CAN)** file — a standard format describing message IDs, signal bit positions, scaling factors, and units.

### Active Polling via UDS (ISO 14229)

Unified Diagnostic Services (UDS) over CAN allows the gateway to request:

- **ReadDataByIdentifier (0x22)**: Live sensor values (coolant temperature, RPM, etc.)
- **ReadDTCInformation (0x19)**: Stored and pending fault codes.
- **RequestDownload / TransferData**: Triggering ECU software updates.

### SAE J1939 (Heavy Vehicles)

J1939 uses 29-bit extended CAN IDs. Parameter Group Numbers (PGNs) encode data such as engine speed (PGN 61444), vehicle speed (PGN 65265), and fuel rate (PGN 65266).

---

## Telematics Gateway Design

A practical embedded gateway typically combines:

| Component | Role |
|---|---|
| CAN controller (MCP2515 / SJA1000 / built-in SoC) | Physical CAN access |
| Microcontroller / SoC (STM32, i.MX8, Raspberry Pi CM4) | Local processing |
| Cellular modem (u-blox SARA-R4, Quectel EC21) | WAN connectivity |
| Secure element / HSM | Key storage, mutual TLS |
| GNSS module | Location data |
| Local flash / eMMC | Buffering during connectivity loss |

The gateway must handle **store-and-forward**: when the cellular link is unavailable (tunnels, rural areas), data is persisted locally and uploaded when connectivity resumes.

---

## Data Serialization and Protocol Adaptation

Raw CAN frames (8 bytes) must be enriched and encoded before cloud transmission:

- **Timestamp** (UTC, microsecond precision from GPS PPS or NTP).
- **Vehicle Identification Number (VIN)** as a device identity.
- **Signal decoding**: Raw integer values scaled to physical units using DBC factors.
- **Serialization formats**: JSON (human-readable), Protocol Buffers / FlatBuffers (compact binary), Apache Avro (schema-on-read for analytics pipelines).
- **Compression**: zlib/LZ4 for burst uploads of buffered data.

---

## Cloud Communication Protocols

### MQTT (Most Common for Telematics)

MQTT is lightweight, supports QoS levels 0/1/2, and maps naturally to CAN signal topics:

```
vehicles/{VIN}/can/signals          <- live signal stream
vehicles/{VIN}/dtcs                 <- fault codes
vehicles/{VIN}/commands/response    <- OTA command responses
```

### HTTPS REST

Used for configuration downloads, DTC uploads, and periodic reports where real-time latency is not critical.

### gRPC / AMQP

Used in high-throughput fleet backends where millions of vehicles generate continuous streams.

---

## Fleet Monitoring

Fleet monitoring aggregates signals across all vehicles in a time-series database and presents them on a dashboard. Key metrics:

- **Utilization**: Engine-on hours, idle time, distance driven.
- **Driver behavior**: Harsh braking events (deceleration > threshold on CAN), speeding, excessive idling.
- **Fuel economy**: Calculated from J1939 fuel rate PGN and odometer.
- **Geofencing**: Alerts when a vehicle leaves/enters a geographic zone, correlated with GPS coordinates transmitted alongside CAN data.

---

## Predictive Maintenance

Predictive maintenance uses machine learning or rule-based engines applied to historical CAN signal trends:

| Signal | Failure Predicted |
|---|---|
| Coolant temperature trends upward over weeks | Thermostat / water pump degradation |
| Battery voltage drops during cranking | Battery or alternator end-of-life |
| Transmission oil temperature spikes | Clutch pack wear |
| Injector duty cycle increase with flat power output | Injector fouling |
| Brake pressure asymmetry | Caliper seizure |

The cloud backend ingests streams, computes rolling statistics (mean, variance, rate of change), and triggers alerts or work orders when anomaly thresholds are exceeded.

---

## Security Considerations

- **Transport security**: Mutual TLS (mTLS) between gateway and broker; certificates provisioned via PKI at manufacturing time.
- **Device identity**: X.509 certificates stored in secure element; never exposed to application software.
- **Message signing**: HMAC-SHA256 on MQTT payloads to prevent spoofing.
- **CAN firewall**: The gateway should enforce a strict allowlist of CAN message IDs it will forward to the cloud, preventing raw CAN injection from a compromised cloud back into safety-critical ECUs.
- **OTA security**: Signed firmware images (Ed25519); rollback prevention via secure boot counter in fuses.

---

## C/C++ Implementation Examples

### Example 1: CAN Frame Capture Using SocketCAN (Linux)

```c
// can_capture.c
// Capture raw CAN frames using Linux SocketCAN and buffer them for upload.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <time.h>

#define BUFFER_SIZE 1024

typedef struct {
    uint32_t can_id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint64_t timestamp_us;
} TelematicsFrame;

static TelematicsFrame frame_buffer[BUFFER_SIZE];
static int buffer_head = 0;

// Returns monotonic microsecond timestamp
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)(ts.tv_nsec / 1000);
}

int open_can_socket(const char *ifname) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); close(sock); return -1; }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return -1;
    }

    // Example: filter – only accept J1939 engine speed PGN 0x0CF004  
    // and coolant temperature PGN 0x0FEEE
    struct can_filter rfilter[] = {
        { .can_id = 0x0CF00400, .can_mask = CAN_EFF_MASK | CAN_EFF_FLAG },
        { .can_id = 0x0FEEE000, .can_mask = 0x1FFF0000 | CAN_EFF_FLAG  },
    };
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER, rfilter, sizeof(rfilter));

    return sock;
}

int capture_frames(int sock, int max_frames) {
    struct can_frame frame;
    int count = 0;

    while (count < max_frames) {
        ssize_t nbytes = read(sock, &frame, sizeof(struct can_frame));
        if (nbytes < (ssize_t)sizeof(struct can_frame)) {
            fprintf(stderr, "Incomplete CAN frame\n");
            continue;
        }

        if (buffer_head >= BUFFER_SIZE) {
            fprintf(stderr, "Buffer full – dropping frame\n");
            continue;
        }

        TelematicsFrame *tf = &frame_buffer[buffer_head++];
        tf->can_id       = frame.can_id & CAN_EFF_MASK;
        tf->dlc          = frame.can_dlc;
        tf->timestamp_us = now_us();
        memcpy(tf->data, frame.data, frame.can_dlc);
        count++;
    }
    return count;
}

// Decode J1939 Engine Speed from PGN 61444 (0xF004), SPN 190
// Bytes 4-5, resolution 0.125 rpm/bit, offset 0
double decode_engine_speed(const uint8_t *data) {
    uint16_t raw = (uint16_t)data[3] | ((uint16_t)data[4] << 8);
    return raw * 0.125;  // RPM
}

int main(void) {
    int sock = open_can_socket("can0");
    if (sock < 0) return EXIT_FAILURE;

    printf("Capturing 100 CAN frames...\n");
    int n = capture_frames(sock, 100);
    printf("Captured %d frames\n", n);

    for (int i = 0; i < n; i++) {
        TelematicsFrame *tf = &frame_buffer[i];
        printf("[%llu us] ID=0x%08X DLC=%d Data=",
               (unsigned long long)tf->timestamp_us, tf->can_id, tf->dlc);
        for (int b = 0; b < tf->dlc; b++)
            printf("%02X ", tf->data[b]);
        printf("\n");
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### Example 2: UDS ReadDataByIdentifier over CAN (C++)

```cpp
// uds_reader.cpp
// Send UDS ReadDataByIdentifier (0x22) request over CAN and parse response.
// Targets an ECU at CAN ID 0x7E0 (response on 0x7E8).

#include <cstdint>
#include <cstring>
#include <cstdio>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <stdexcept>
#include <vector>
#include <array>

constexpr uint32_t UDS_REQUEST_ID  = 0x7E0;
constexpr uint32_t UDS_RESPONSE_ID = 0x7E8;

// ISO-TP single-frame: PCI byte 0x0N where N = length
// For short UDS frames (≤7 bytes), single-frame ISO-TP suffices.

class UdsClient {
public:
    explicit UdsClient(const char *ifname) {
        sock_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_ < 0) throw std::runtime_error("socket failed");

        struct ifreq ifr{};
        strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
        ioctl(sock_, SIOCGIFINDEX, &ifr);

        struct sockaddr_can addr{ .can_family = AF_CAN,
                                  .can_ifindex = ifr.ifr_ifindex };
        if (bind(sock_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            throw std::runtime_error("bind failed");

        // Accept only UDS response ID
        struct can_filter filt{ .can_id   = UDS_RESPONSE_ID,
                                .can_mask = CAN_SFF_MASK };
        setsockopt(sock_, SOL_CAN_RAW, CAN_RAW_FILTER, &filt, sizeof(filt));
    }

    ~UdsClient() { if (sock_ >= 0) close(sock_); }

    // Send ReadDataByIdentifier for a given DID (2-byte Data Identifier)
    // Returns the response data payload or empty vector on error.
    std::vector<uint8_t> readDataByIdentifier(uint16_t did) {
        // Build ISO-TP single frame: [PCI] [SID=0x22] [DID_HIGH] [DID_LOW] [padding]
        struct can_frame req{};
        req.can_id  = UDS_REQUEST_ID;
        req.can_dlc = 8;
        req.data[0] = 0x03;               // SF PCI: 3 bytes of payload
        req.data[1] = 0x22;               // SID: ReadDataByIdentifier
        req.data[2] = (did >> 8) & 0xFF;  // DID high byte
        req.data[3] = did & 0xFF;         // DID low byte
        std::fill(req.data + 4, req.data + 8, 0xCC); // padding

        if (write(sock_, &req, sizeof(req)) != sizeof(req)) {
            perror("write");
            return {};
        }

        // Wait for response (simple blocking read, no timeout for brevity)
        struct can_frame resp{};
        ssize_t nbytes = read(sock_, &resp, sizeof(resp));
        if (nbytes < (ssize_t)sizeof(struct can_frame)) return {};

        // Validate positive response: SF PCI + SID 0x62 + echoed DID
        uint8_t pci       = resp.data[0];
        uint8_t resp_sid  = resp.data[1];
        uint16_t resp_did = ((uint16_t)resp.data[2] << 8) | resp.data[3];

        if ((pci & 0xF0) != 0x00) {
            fprintf(stderr, "Not a single-frame ISO-TP response\n");
            return {};
        }
        if (resp_sid != 0x62) {
            fprintf(stderr, "Negative response: NRC=0x%02X\n", resp.data[3]);
            return {};
        }
        if (resp_did != did) {
            fprintf(stderr, "DID mismatch in response\n");
            return {};
        }

        uint8_t payload_len = (pci & 0x0F) - 3; // subtract SID + DID (2 bytes)
        std::vector<uint8_t> result(resp.data + 4, resp.data + 4 + payload_len);
        return result;
    }

private:
    int sock_ = -1;
};

int main() {
    try {
        UdsClient client("can0");

        // DID 0xF40C: Engine RPM (OBD-II PID mapped to UDS DID in many ECUs)
        auto rpm_data = client.readDataByIdentifier(0xF40C);
        if (rpm_data.size() >= 2) {
            // Formula: ((A*256)+B)/4 rpm
            uint16_t raw = ((uint16_t)rpm_data[0] << 8) | rpm_data[1];
            printf("Engine RPM: %.2f\n", raw / 4.0);
        }

        // DID 0xF405: Engine Coolant Temperature
        auto temp_data = client.readDataByIdentifier(0xF405);
        if (!temp_data.empty()) {
            int temp_c = (int)temp_data[0] - 40; // offset -40 per OBD-II spec
            printf("Coolant Temp: %d °C\n", temp_c);
        }

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
```

---

### Example 3: MQTT Telemetry Publisher (C with Paho MQTT)

```c
// mqtt_publisher.c
// Serialize CAN signals to JSON and publish to an MQTT broker.
// Depends on: Eclipse Paho MQTT C client (libpaho-mqtt3c)
// Build: gcc mqtt_publisher.c -lpaho-mqtt3c -o mqtt_pub

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "MQTTClient.h"  // Paho MQTT C client

#define BROKER_URL   "ssl://fleet.example.com:8883"
#define CLIENT_ID    "GW-VIN-1HGBH41JXMN109186"
#define TOPIC_PREFIX "vehicles/1HGBH41JXMN109186/can/signals"
#define QOS          1
#define TIMEOUT_MS   10000L

typedef struct {
    double engine_rpm;
    double coolant_temp_c;
    double vehicle_speed_kmh;
    double fuel_level_pct;
    uint64_t timestamp_ms;
} VehicleSignals;

// Serialize signals to a compact JSON string.
// Returns bytes written, or -1 on error.
int signals_to_json(const VehicleSignals *sig, char *buf, size_t buflen) {
    return snprintf(buf, buflen,
        "{"
        "\"ts\":%llu,"
        "\"rpm\":%.1f,"
        "\"coolant\":%.1f,"
        "\"speed\":%.1f,"
        "\"fuel\":%.1f"
        "}",
        (unsigned long long)sig->timestamp_ms,
        sig->engine_rpm,
        sig->coolant_temp_c,
        sig->vehicle_speed_kmh,
        sig->fuel_level_pct
    );
}

int publish_signals(MQTTClient client, const VehicleSignals *sig) {
    char payload[256];
    int len = signals_to_json(sig, payload, sizeof(payload));
    if (len < 0) return MQTTCLIENT_FAILURE;

    MQTTClient_message msg = MQTTClient_message_initializer;
    msg.payload    = payload;
    msg.payloadlen = len;
    msg.qos        = QOS;
    msg.retained   = 0;

    MQTTClient_deliveryToken token;
    int rc = MQTTClient_publishMessage(client, TOPIC_PREFIX, &msg, &token);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Publish failed: %d\n", rc);
        return rc;
    }

    return MQTTClient_waitForCompletion(client, token, TIMEOUT_MS);
}

int main(void) {
    MQTTClient client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    MQTTClient_SSLOptions ssl_opts      = MQTTClient_SSLOptions_initializer;

    MQTTClient_create(&client, BROKER_URL, CLIENT_ID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);

    // mTLS: device certificate + private key stored in secure element
    ssl_opts.trustStore      = "/etc/telematics/ca-chain.pem";
    ssl_opts.keyStore        = "/etc/telematics/device-cert.pem";
    ssl_opts.privateKey      = "/etc/telematics/device-key.pem";
    ssl_opts.enableServerCertAuth = 1;

    conn_opts.ssl          = &ssl_opts;
    conn_opts.keepAliveInterval = 30;
    conn_opts.cleansession = 1;

    int rc = MQTTClient_connect(client, &conn_opts);
    if (rc != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "MQTT connect failed: %d\n", rc);
        MQTTClient_destroy(&client);
        return EXIT_FAILURE;
    }

    // Simulate a decoded CAN signal snapshot
    VehicleSignals sig = {
        .engine_rpm       = 2450.0,
        .coolant_temp_c   = 87.5,
        .vehicle_speed_kmh = 95.0,
        .fuel_level_pct   = 62.3,
        .timestamp_ms     = 1700000000000ULL
    };

    rc = publish_signals(client, &sig);
    printf("Publish %s (rc=%d)\n", rc == MQTTCLIENT_SUCCESS ? "OK" : "FAILED", rc);

    MQTTClient_disconnect(client, 2000);
    MQTTClient_destroy(&client);
    return EXIT_SUCCESS;
}
```

---

### Example 4: Predictive Maintenance – Rolling Statistics (C++)

```cpp
// rolling_stats.cpp
// Compute rolling mean and variance for a CAN signal to detect anomalies.
// Used in the gateway or cloud analytics service.

#include <cstdint>
#include <cmath>
#include <cstdio>
#include <deque>
#include <stdexcept>

class RollingStats {
public:
    explicit RollingStats(size_t window_size)
        : window_(window_size), n_(0), mean_(0.0), M2_(0.0) {
        if (window_size == 0) throw std::invalid_argument("window must be > 0");
    }

    // Add a new sample using Welford's online algorithm.
    void add(double x) {
        samples_.push_back(x);
        n_++;

        // Welford update
        double delta  = x - mean_;
        mean_        += delta / n_;
        double delta2 = x - mean_;
        M2_          += delta * delta2;

        // Evict oldest sample when window is exceeded
        if (samples_.size() > window_) {
            double old_x = samples_.front();
            samples_.pop_front();
            n_--;

            // Reverse Welford update for the removed sample
            double old_delta  = old_x - mean_;
            mean_            -= old_delta / n_;
            double old_delta2 = old_x - mean_;
            M2_              -= old_delta * old_delta2;
            if (M2_ < 0.0) M2_ = 0.0; // numerical guard
        }
    }

    double mean()     const { return mean_; }
    double variance() const { return n_ > 1 ? M2_ / (n_ - 1) : 0.0; }
    double stddev()   const { return std::sqrt(variance()); }
    size_t count()    const { return n_; }

    // Returns true if the sample deviates more than z_threshold standard deviations.
    bool isAnomaly(double x, double z_threshold = 3.0) const {
        if (n_ < 5) return false; // not enough data yet
        double sd = stddev();
        if (sd < 1e-9) return false;
        return std::abs(x - mean_) > z_threshold * sd;
    }

private:
    size_t        window_;
    std::deque<double> samples_;
    size_t        n_;
    double        mean_;
    double        M2_;
};

// Simulate gateway-side anomaly detection on coolant temperature signal
int main() {
    RollingStats coolant_stats(60); // 60-sample rolling window (e.g., 60 seconds)

    // Simulate normal operating data
    double normal_temps[] = {
        85.0, 86.0, 85.5, 86.2, 85.8, 86.1, 85.9, 86.3,
        85.7, 86.0, 85.5, 85.8, 86.0, 85.6, 86.2, 85.9
    };
    for (double t : normal_temps) coolant_stats.add(t);

    printf("Baseline: mean=%.2f°C  stddev=%.4f°C\n",
           coolant_stats.mean(), coolant_stats.stddev());

    // Simulate a degrading thermostat causing temperature drift
    double anomaly_temps[] = { 89.0, 92.0, 96.5, 101.3, 108.7 };
    for (double t : anomaly_temps) {
        bool alert = coolant_stats.isAnomaly(t, 3.0);
        printf("Coolant=%.1f°C  z=%.2f  %s\n",
               t,
               (t - coolant_stats.mean()) /
                   (coolant_stats.stddev() + 1e-9),
               alert ? "⚠ ANOMALY – schedule maintenance" : "OK");
        coolant_stats.add(t);
    }

    return 0;
}
```

---

## Rust Implementation Examples

### Example 5: CAN Frame Capture with `socketcan` Crate

```toml
# Cargo.toml dependencies
[dependencies]
socketcan = "3"
tokio = { version = "1", features = ["full"] }
chrono = "0.4"
```

```rust
// src/can_capture.rs
// Asynchronous CAN frame capture using the socketcan crate on Linux.

use socketcan::{tokio::CanSocket, CanFrame, Frame, EmbeddedFrame};
use tokio::time::{timeout, Duration};
use chrono::Utc;

#[derive(Debug, Clone)]
pub struct TelematicsFrame {
    pub can_id:       u32,
    pub dlc:          usize,
    pub data:         Vec<u8>,
    pub timestamp_us: i64,
}

impl From<CanFrame> for TelematicsFrame {
    fn from(f: CanFrame) -> Self {
        TelematicsFrame {
            can_id:       f.raw_id(),
            dlc:          f.data().len(),
            data:         f.data().to_vec(),
            timestamp_us: Utc::now().timestamp_micros(),
        }
    }
}

/// Capture up to `max_frames` CAN frames with a per-frame read timeout.
pub async fn capture_frames(
    iface: &str,
    max_frames: usize,
    read_timeout: Duration,
) -> anyhow::Result<Vec<TelematicsFrame>> {
    let sock = CanSocket::open(iface)?;
    let mut frames = Vec::with_capacity(max_frames);

    while frames.len() < max_frames {
        match timeout(read_timeout, sock.read_frame()).await {
            Ok(Ok(frame)) => {
                frames.push(TelematicsFrame::from(frame));
            }
            Ok(Err(e)) => {
                eprintln!("CAN read error: {e}");
            }
            Err(_) => {
                eprintln!("Read timeout – connectivity issue?");
                break;
            }
        }
    }

    Ok(frames)
}

/// Decode J1939 Engine Speed (PGN 61444, SPN 190) from bytes 3-4.
pub fn decode_j1939_engine_speed(data: &[u8]) -> Option<f64> {
    if data.len() < 5 { return None; }
    let raw = u16::from_le_bytes([data[3], data[4]]);
    Some(raw as f64 * 0.125) // 0.125 RPM per bit
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let frames = capture_frames("can0", 50, Duration::from_secs(5)).await?;

    println!("Captured {} frames:", frames.len());
    for f in &frames {
        print!(
            "[{} µs] ID=0x{:08X} DLC={} Data=",
            f.timestamp_us, f.can_id, f.dlc
        );
        for b in &f.data { print!("{b:02X} "); }
        println!();

        // Detect J1939 PGN 61444 (0x0CF00400 with priority 3 → raw ID varies)
        if (f.can_id & 0x00FFFF00) == 0x00F00400 {
            if let Some(rpm) = decode_j1939_engine_speed(&f.data) {
                println!("  → Engine Speed: {rpm:.2} RPM");
            }
        }
    }

    Ok(())
}
```

---

### Example 6: MQTT Telemetry Client with TLS (Rust / `rumqttc`)

```toml
# Cargo.toml
[dependencies]
rumqttc    = "0.24"
tokio      = { version = "1", features = ["full"] }
serde      = { version = "1", features = ["derive"] }
serde_json = "1"
rustls     = "0.23"
rustls-pemfile = "2"
```

```rust
// src/mqtt_publisher.rs
// Publish serialized CAN signal data to an MQTT broker using mTLS.

use rumqttc::{
    AsyncClient, MqttOptions, QoS, TlsConfiguration, Transport,
};
use serde::Serialize;
use std::{fs, time::Duration};
use tokio::time::sleep;

#[derive(Debug, Serialize)]
pub struct VehicleSignals {
    pub ts:      u64,    // Unix milliseconds
    pub rpm:     f64,
    pub coolant: f64,    // °C
    pub speed:   f64,    // km/h
    pub fuel:    f64,    // %
}

const VIN:          &str = "1HGBH41JXMN109186";
const BROKER_HOST:  &str = "fleet.example.com";
const BROKER_PORT:  u16  = 8883;

fn build_tls_config() -> anyhow::Result<TlsConfiguration> {
    let ca_cert   = fs::read("/etc/telematics/ca-chain.pem")?;
    let dev_cert  = fs::read("/etc/telematics/device-cert.pem")?;
    let dev_key   = fs::read("/etc/telematics/device-key.pem")?;

    Ok(TlsConfiguration::Simple {
        ca:          ca_cert,
        alpn:        None,
        client_auth: Some((dev_cert, dev_key)),
    })
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    let mut mqtt_opts = MqttOptions::new(
        format!("GW-{VIN}"),
        BROKER_HOST,
        BROKER_PORT,
    );
    mqtt_opts.set_keep_alive(Duration::from_secs(30));
    mqtt_opts.set_transport(Transport::Tls(build_tls_config()?));

    let (client, mut eventloop) = AsyncClient::new(mqtt_opts, 32);

    // Drive the event loop in a background task
    tokio::spawn(async move {
        loop {
            match eventloop.poll().await {
                Ok(event) => {
                    // Handle incoming ACKs, CONNACK, etc.
                    tracing::debug!("MQTT event: {event:?}");
                }
                Err(e) => {
                    eprintln!("MQTT event loop error: {e}");
                    sleep(Duration::from_secs(5)).await;
                }
            }
        }
    });

    let topic = format!("vehicles/{VIN}/can/signals");

    // Simulate a publishing loop (in production: driven by CAN capture task)
    let signals = VehicleSignals {
        ts:      1700000000000,
        rpm:     2450.0,
        coolant: 87.5,
        speed:   95.0,
        fuel:    62.3,
    };

    let payload = serde_json::to_vec(&signals)?;
    client
        .publish(&topic, QoS::AtLeastOnce, false, payload)
        .await?;
    println!("Published signals to topic: {topic}");

    // Allow time for ACK before shutdown
    sleep(Duration::from_secs(2)).await;
    Ok(())
}
```

---

### Example 7: Predictive Maintenance – Rolling Z-Score Anomaly Detector (Rust)

```rust
// src/anomaly_detector.rs
// Online rolling statistics with Welford's algorithm for CAN signal anomaly detection.

use std::collections::VecDeque;

#[derive(Debug)]
pub struct RollingStats {
    window:   usize,
    samples:  VecDeque<f64>,
    mean:     f64,
    m2:       f64,
}

impl RollingStats {
    pub fn new(window: usize) -> Self {
        assert!(window > 0, "window must be > 0");
        Self {
            window,
            samples: VecDeque::with_capacity(window + 1),
            mean:    0.0,
            m2:      0.0,
        }
    }

    /// Add a new sample, evicting the oldest if the window is full.
    pub fn add(&mut self, x: f64) {
        let n_before = self.samples.len() as f64;

        // Online Welford update (add)
        self.samples.push_back(x);
        let n_after = self.samples.len() as f64;
        let delta  = x - self.mean;
        self.mean += delta / n_after;
        self.m2   += delta * (x - self.mean);

        // Evict oldest if over window
        if self.samples.len() > self.window {
            let old_x = self.samples.pop_front().unwrap();
            let n_new = n_before; // length after eviction
            let old_delta  = old_x - self.mean;
            self.mean     -= old_delta / n_new;
            let old_delta2 = old_x - self.mean;
            self.m2       -= old_delta * old_delta2;
            self.m2        = self.m2.max(0.0); // numerical guard
        }
    }

    pub fn mean(&self)     -> f64 { self.mean }
    pub fn count(&self)    -> usize { self.samples.len() }

    pub fn variance(&self) -> f64 {
        let n = self.samples.len();
        if n > 1 { self.m2 / (n - 1) as f64 } else { 0.0 }
    }

    pub fn stddev(&self) -> f64 { self.variance().sqrt() }

    /// Returns `Some(z_score)` if the value is anomalous, else `None`.
    pub fn check_anomaly(&self, x: f64, z_threshold: f64) -> Option<f64> {
        if self.count() < 5 { return None; }
        let sd = self.stddev();
        if sd < 1e-9 { return None; }
        let z = (x - self.mean).abs() / sd;
        if z > z_threshold { Some(z) } else { None }
    }
}

#[derive(Debug, Clone)]
pub struct MaintenanceAlert {
    pub signal_name: String,
    pub value:       f64,
    pub z_score:     f64,
    pub mean:        f64,
    pub stddev:      f64,
}

pub struct PredictiveMaintenance {
    signal_name: String,
    stats:       RollingStats,
    z_threshold: f64,
}

impl PredictiveMaintenance {
    pub fn new(signal_name: &str, window: usize, z_threshold: f64) -> Self {
        Self {
            signal_name: signal_name.to_string(),
            stats:       RollingStats::new(window),
            z_threshold,
        }
    }

    /// Feed a new signal value. Returns an alert if anomalous.
    pub fn feed(&mut self, value: f64) -> Option<MaintenanceAlert> {
        let alert = self.stats.check_anomaly(value, self.z_threshold)
            .map(|z| MaintenanceAlert {
                signal_name: self.signal_name.clone(),
                value,
                z_score:     z,
                mean:        self.stats.mean(),
                stddev:      self.stats.stddev(),
            });
        self.stats.add(value);
        alert
    }
}

fn main() {
    let mut detector = PredictiveMaintenance::new("coolant_temp_c", 60, 3.0);

    // Feed normal baseline
    for temp in [85.0_f64, 86.0, 85.5, 86.2, 85.8, 86.1, 85.9, 86.3,
                 85.7, 86.0, 85.5, 85.8, 86.0, 85.6, 86.2, 85.9]
    {
        detector.feed(temp);
    }

    println!(
        "Baseline established: mean={:.2}°C  σ={:.4}°C",
        detector.stats.mean(),
        detector.stats.stddev()
    );

    // Simulate thermostat degradation
    for temp in [89.0_f64, 92.0, 96.5, 101.3, 108.7] {
        if let Some(alert) = detector.feed(temp) {
            println!(
                "⚠  ALERT: {} = {:.1}°C  z={:.2}  (mean={:.2}°C  σ={:.4}°C) → Schedule service",
                alert.signal_name, alert.value, alert.z_score,
                alert.mean, alert.stddev
            );
        } else {
            println!("  {:.1}°C – OK", temp);
        }
    }
}
```

---

### Example 8: Store-and-Forward Buffer (Rust)

```rust
// src/store_forward.rs
// Persist CAN telemetry to a local SQLite database when offline,
// and flush to the cloud when connectivity is restored.

use rusqlite::{Connection, params};
use serde_json::Value;

pub struct StoreForwardBuffer {
    conn: Connection,
}

impl StoreForwardBuffer {
    pub fn new(db_path: &str) -> anyhow::Result<Self> {
        let conn = Connection::open(db_path)?;
        conn.execute_batch(
            "CREATE TABLE IF NOT EXISTS pending_messages (
                id        INTEGER PRIMARY KEY AUTOINCREMENT,
                topic     TEXT    NOT NULL,
                payload   TEXT    NOT NULL,
                created   INTEGER NOT NULL DEFAULT (strftime('%s','now')),
                attempts  INTEGER NOT NULL DEFAULT 0
            );"
        )?;
        Ok(Self { conn })
    }

    /// Persist a message that could not be sent immediately.
    pub fn enqueue(&self, topic: &str, payload: &Value) -> anyhow::Result<()> {
        self.conn.execute(
            "INSERT INTO pending_messages (topic, payload) VALUES (?1, ?2)",
            params![topic, serde_json::to_string(payload)?],
        )?;
        Ok(())
    }

    /// Retrieve up to `limit` pending messages for retry.
    pub fn dequeue_batch(&self, limit: usize)
        -> anyhow::Result<Vec<(i64, String, String)>>
    {
        let mut stmt = self.conn.prepare(
            "SELECT id, topic, payload FROM pending_messages
             ORDER BY id ASC LIMIT ?1"
        )?;
        let rows = stmt.query_map(params![limit as i64], |row| {
            Ok((row.get::<_, i64>(0)?, row.get::<_, String>(1)?, row.get::<_, String>(2)?))
        })?
        .collect::<Result<Vec<_>, _>>()?;
        Ok(rows)
    }

    /// Remove successfully uploaded messages by their database IDs.
    pub fn acknowledge(&self, ids: &[i64]) -> anyhow::Result<()> {
        for id in ids {
            self.conn.execute(
                "DELETE FROM pending_messages WHERE id = ?1",
                params![id],
            )?;
        }
        Ok(())
    }

    pub fn pending_count(&self) -> anyhow::Result<i64> {
        Ok(self.conn.query_row(
            "SELECT COUNT(*) FROM pending_messages",
            [],
            |row| row.get(0),
        )?)
    }
}

fn main() -> anyhow::Result<()> {
    let buf = StoreForwardBuffer::new("/var/lib/telematics/buffer.db")?;

    // Simulate offline buffering of three signal snapshots
    for i in 0..3 {
        let msg = serde_json::json!({
            "ts":      1700000000000_u64 + i * 1000,
            "rpm":     2400.0 + i as f64 * 50.0,
            "speed":   90.0,
        });
        buf.enqueue("vehicles/VIN123/can/signals", &msg)?;
    }
    println!("Buffered {} messages", buf.pending_count()?);

    // Simulate connectivity restored: flush batch
    let batch = buf.dequeue_batch(10)?;
    println!("Flushing {} messages to cloud...", batch.len());
    let ids: Vec<i64> = batch.iter().map(|(id, _, _)| *id).collect();

    // --- In production: publish each message via MQTT/HTTPS here ---

    buf.acknowledge(&ids)?;
    println!("Acknowledged. Pending: {}", buf.pending_count()?);
    Ok(())
}
```

---

## Summary

Remote diagnostics and telematics form the critical bridge between isolated CAN networks and the broader connected ecosystem. The key architectural concerns and takeaways are:

**Data acquisition** is the foundation. Passive SocketCAN sniffing is ideal for continuous high-frequency signals; active UDS polling is needed for on-demand diagnostics and fault code retrieval. J1939 PGN decoding handles heavy commercial vehicles with its standardized parameter group structure.

**The telematics gateway** serves as a hardened edge device that normalises raw CAN bytes into enriched, timestamped, and DBC-decoded signal values, compresses them, and forwards them to the cloud over secured cellular or Wi-Fi links.

**MQTT with mTLS** is the de facto standard for cloud transport: it is lightweight enough for embedded devices, supports QoS acknowledgement semantics needed for reliable delivery, and maps naturally to hierarchical vehicle/signal topic trees.

**Store-and-forward buffering** (demonstrated with SQLite in the Rust examples) is non-negotiable in real deployments because vehicles routinely lose connectivity. Messages must be persisted locally and flushed atomically when the link recovers.

**Predictive maintenance** adds the most business value by moving fleets from costly reactive repairs to scheduled, data-driven servicing. Simple online algorithms such as Welford's rolling mean and Z-score anomaly detection can run on the gateway itself, generating alerts without requiring full telemetry upload — reducing bandwidth and latency.

**Security** must be designed in from the start: mutual TLS, hardware-backed key storage, signed OTA firmware, and strict CAN firewall rules to prevent the cloud path from becoming an attack vector into safety-critical vehicle systems.

The C/C++ examples target embedded Linux gateways using the SocketCAN kernel subsystem and Paho MQTT, while the Rust examples demonstrate the same patterns with strong type safety, async-first design via Tokio, and fearless concurrency — qualities that make Rust increasingly attractive for safety-critical telematics gateway software.

---

*Document: 73 – Remote Diagnostics and Telematics | CAN Bus Series*