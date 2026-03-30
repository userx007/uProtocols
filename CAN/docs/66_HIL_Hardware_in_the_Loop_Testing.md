# 66. HIL (Hardware-in-the-Loop) Testing

## Setting up hardware-in-the-loop test benches for automated CAN ECU validation and regression testing

---

## 1. What Is HIL Testing?

Hardware-in-the-Loop (HIL) testing is an embedded-systems validation technique where a **real ECU** (the device under test, DUT) is exercised by a surrounding test environment that *simulates* the physical world and the rest of the vehicle network. Instead of deploying the ECU into a real vehicle and manually driving test scenarios, a HIL bench:

- Injects realistic CAN frames that the ECU would normally receive from sensors and peer ECUs
- Reads and validates the CAN frames the ECU transmits in response
- Simulates hard-wired signals (ignition line, analogue sensor voltages, PWM outputs) through I/O boards
- Injects deliberate faults (bus-off, bit errors, missing heartbeats) to exercise defensive code
- Produces a machine-readable pass/fail verdict that CI pipelines can consume

The CAN bus is the nervous system of nearly every automotive and industrial HIL bench. All stimulus, response capture, timing analysis, and fault injection travel over CAN or a derivative (CAN-FD, ISO 15765 UDS diagnostics).

---

## 2. HIL Bench Anatomy

### 2.1 Components

| Layer | Role | Typical hardware |
|---|---|---|
| Host PC | Runs test scripts, evaluates verdicts, stores traces | Linux workstation / Raspberry Pi |
| CAN adapter | Bridges host USB/PCIe to the CAN bus | Peak PCAN-USB, Vector VN1610, Kvaser Leaf |
| Signal conditioning | Supplies regulated power, routes analogue/digital I/O | National Instruments DAQ, relay boards |
| ECU DUT | Real production or prototype hardware | Target microcontroller board |
| Simulated nodes | Software stubs that mimic absent peer ECUs | Same host PC via second CAN channel |
| Fault injector | Corrupts frames, forces bus-off, drops ACKs | Dedicated hardware or software bit-banging |
| Result store | Persists logs, CAN traces (`.asc`, `.blf`, `.csv`) | File system, InfluxDB, Elasticsearch |

### 2.2 Test execution flow

```
[Test script]
     │  load test case (stimulus frames + expected responses)
     ▼
[CAN adapter TX] ──── CAN-H/CAN-L ──── [ECU DUT]
                                             │
                                        processes frame
                                             │
[CAN adapter RX] ◄─── CAN-H/CAN-L ◄───────┘
     │
  compare received frames against expected set
     │
  ┌──┴────────┐
  │ PASS/FAIL │ → CI pipeline exit code
  └───────────┘
```

---

## 3. Tooling Landscape

| Task | Open-source | Commercial |
|---|---|---|
| CAN frame I/O | SocketCAN (Linux kernel), python-can | Peak CANalyzer, Vector CANoe |
| Test scripting | pytest + python-can, Robot Framework | CANalyzer test modules |
| Signal databases | DBC files (open format) | CANdb++, FIBEX |
| Fault injection | `cangen`, custom SocketCAN tools | Goepel, Softing fault injectors |
| Reporting | JUnit XML, Allure | CAPL report generator |

---

## 4. C / C++ Implementation

### 4.1 Minimal HIL frame sender (SocketCAN, Linux)

```c
/* hil_sender.c — Injects a stimulus CAN frame, reads the DUT response,
   and evaluates a pass/fail verdict.
   Build: gcc -O2 -Wall -o hil_sender hil_sender.c
   Run:   sudo ./hil_sender vcan0
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ── test parameters ─────────────────────────────────────── */
#define STIMULUS_ID     0x201u   /* frame sent to DUT          */
#define EXPECTED_ID     0x301u   /* frame DUT should emit back  */
#define RESPONSE_MS     100u     /* max wait time in ms         */

/* ── helpers ─────────────────────────────────────────────── */
static int open_can(const char *iface)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(fd, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); close(fd); return -1; }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror("bind"); close(fd); return -1;
    }
    return fd;
}

static int send_frame(int fd, canid_t id, const uint8_t *data, uint8_t dlc)
{
    struct can_frame f = { .can_id = id, .can_dlc = dlc };
    memcpy(f.data, data, dlc);
    ssize_t n = write(fd, &f, sizeof f);
    return (n == sizeof f) ? 0 : -1;
}

/* Wait up to timeout_ms for a frame matching expected_id.
   Returns 1 on match, 0 on timeout, -1 on error. */
static int await_frame(int fd, canid_t expected_id,
                        struct can_frame *out, int timeout_ms)
{
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    int rc = poll(&pfd, 1, timeout_ms);
    if (rc < 0)  { perror("poll"); return -1; }
    if (rc == 0) { fprintf(stderr, "[HIL] timeout waiting for 0x%03X\n",
                            expected_id); return 0; }

    struct can_frame f;
    ssize_t n = read(fd, &f, sizeof f);
    if (n != sizeof f) return -1;

    if ((f.can_id & CAN_EFF_MASK) == expected_id) {
        if (out) *out = f;
        return 1;
    }
    return 0; /* different ID arrived first — caller should retry */
}

/* ── test cases ──────────────────────────────────────────── */
typedef struct {
    const char *name;
    uint8_t     stimulus[8];
    uint8_t     stim_dlc;
    uint8_t     expected_data[8];
    uint8_t     exp_dlc;
} TestCase;

static const TestCase tests[] = {
    {
        .name          = "TC001_engine_speed_request",
        .stimulus      = { 0x01, 0x00 },
        .stim_dlc      = 2,
        .expected_data = { 0x01, 0x0B, 0xB8 }, /* 3000 RPM big-endian */
        .exp_dlc       = 3,
    },
    {
        .name          = "TC002_vehicle_speed_request",
        .stimulus      = { 0x02, 0x00 },
        .stim_dlc      = 2,
        .expected_data = { 0x02, 0x00, 0x64 }, /* 100 km/h            */
        .exp_dlc       = 3,
    },
};

/* ── main ────────────────────────────────────────────────── */
int main(int argc, char *argv[])
{
    const char *iface = (argc > 1) ? argv[1] : "vcan0";
    int fd = open_can(iface);
    if (fd < 0) return EXIT_FAILURE;

    int passed = 0, failed = 0;
    size_t n_tests = sizeof tests / sizeof tests[0];

    for (size_t i = 0; i < n_tests; i++) {
        const TestCase *tc = &tests[i];
        printf("[HIL] %-40s  ... ", tc->name);
        fflush(stdout);

        /* send stimulus */
        if (send_frame(fd, STIMULUS_ID, tc->stimulus, tc->stim_dlc) < 0) {
            printf("SEND ERROR\n"); failed++; continue;
        }

        /* collect response */
        struct can_frame resp = {0};
        int rc = await_frame(fd, EXPECTED_ID, &resp, RESPONSE_MS);
        if (rc <= 0) {
            printf("FAIL (no response)\n"); failed++; continue;
        }

        /* compare payload */
        if (resp.can_dlc != tc->exp_dlc ||
            memcmp(resp.data, tc->expected_data, tc->exp_dlc) != 0) {
            printf("FAIL (payload mismatch)\n"); failed++; continue;
        }

        printf("PASS\n");
        passed++;
        usleep(5000); /* inter-frame gap */
    }

    printf("\n[HIL] Results: %d passed, %d failed / %zu total\n",
           passed, failed, n_tests);
    close(fd);
    return (failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

### 4.2 C++ HIL test bench class with DBC signal decoding

```cpp
/* hil_bench.hpp — Reusable C++17 HIL test bench abstraction.
   Wraps SocketCAN, decodes DBC-style signals, and accumulates
   JUnit-compatible test results.
*/
#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <functional>
#include <chrono>
#include <stdexcept>
#include <iostream>
#include <format>

#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <poll.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ─── signal descriptor (simplified DBC entry) ─────────────── */
struct Signal {
    std::string  name;
    uint8_t      start_bit;   /* LSB position in CAN frame (Intel byte order) */
    uint8_t      length;      /* bit length (1–32)                             */
    double       factor;
    double       offset;

    double decode(const uint8_t *data) const {
        uint32_t raw = 0;
        for (uint8_t i = 0; i < length; i++) {
            uint8_t bit = start_bit + i;
            if ((data[bit / 8] >> (bit % 8)) & 1u)
                raw |= (1u << i);
        }
        return raw * factor + offset;
    }
};

/* ─── single test-case definition ──────────────────────────── */
struct HilTestCase {
    std::string              id;
    std::string              description;
    canid_t                  stim_id;
    std::vector<uint8_t>     stim_payload;
    canid_t                  expect_id;
    std::function<bool(const struct can_frame &)> predicate;
    std::chrono::milliseconds timeout { 100 };
};

/* ─── result record ─────────────────────────────────────────── */
struct HilResult {
    std::string id;
    bool        passed;
    std::string detail;
    double      elapsed_ms;
};

/* ─── HIL bench ─────────────────────────────────────────────── */
class HilBench {
public:
    explicit HilBench(const std::string &iface) : iface_(iface) {
        fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0) throw std::runtime_error("socket: " + std::string(strerror(errno)));

        struct ifreq ifr {};
        iface_.copy(ifr.ifr_name, IFNAMSIZ - 1);
        if (::ioctl(fd_, SIOCGIFINDEX, &ifr) < 0)
            throw std::runtime_error("ioctl: " + std::string(strerror(errno)));

        struct sockaddr_can addr {};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (::bind(fd_, (struct sockaddr *)&addr, sizeof addr) < 0)
            throw std::runtime_error("bind: " + std::string(strerror(errno)));
    }

    ~HilBench() { if (fd_ >= 0) ::close(fd_); }

    /* non-copyable */
    HilBench(const HilBench &) = delete;
    HilBench &operator=(const HilBench &) = delete;

    void add_test(HilTestCase tc) { cases_.push_back(std::move(tc)); }

    /* Run all registered tests and return results */
    std::vector<HilResult> run_all() {
        std::vector<HilResult> results;
        results.reserve(cases_.size());
        for (const auto &tc : cases_)
            results.push_back(run_one(tc));
        return results;
    }

    /* Emit JUnit XML to stdout */
    static void print_junit(const std::vector<HilResult> &results,
                            const std::string &suite_name = "HIL_CAN")
    {
        int failures = 0;
        for (const auto &r : results) if (!r.passed) failures++;

        std::cout << R"(<?xml version="1.0" encoding="UTF-8"?>)" "\n";
        std::cout << std::format(
            R"(<testsuite name="{}" tests="{}" failures="{}" errors="0">)"
            "\n", suite_name, results.size(), failures);

        for (const auto &r : results) {
            std::cout << std::format(
                R"(  <testcase name="{}" time="{:.3f}">)" "\n",
                r.id, r.elapsed_ms / 1000.0);
            if (!r.passed)
                std::cout << std::format(
                    R"(    <failure message="{}" />)" "\n", r.detail);
            std::cout << "  </testcase>\n";
        }
        std::cout << "</testsuite>\n";
    }

private:
    std::string              iface_;
    int                      fd_ { -1 };
    std::vector<HilTestCase> cases_;

    void send_frame(canid_t id, const std::vector<uint8_t> &payload) {
        struct can_frame f {};
        f.can_id  = id;
        f.can_dlc = static_cast<uint8_t>(std::min(payload.size(), size_t{8}));
        std::copy_n(payload.begin(), f.can_dlc, f.data);
        if (::write(fd_, &f, sizeof f) != sizeof f)
            throw std::runtime_error("write: " + std::string(strerror(errno)));
    }

    HilResult run_one(const HilTestCase &tc) {
        auto t0 = std::chrono::steady_clock::now();

        try {
            send_frame(tc.stim_id, tc.stim_payload);
        } catch (const std::exception &e) {
            return { tc.id, false, std::string("TX error: ") + e.what(), 0.0 };
        }

        /* poll for expected response */
        struct pollfd pfd { fd_, POLLIN, 0 };
        int ready = ::poll(&pfd, 1,
            static_cast<int>(tc.timeout.count()));

        auto t1 = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double, std::milli>(t1 - t0).count();

        if (ready <= 0)
            return { tc.id, false, "timeout waiting for response", elapsed };

        struct can_frame resp {};
        if (::read(fd_, &resp, sizeof resp) != sizeof resp)
            return { tc.id, false, "read error", elapsed };

        if ((resp.can_id & CAN_EFF_MASK) != tc.expect_id)
            return { tc.id, false,
                std::format("unexpected ID 0x{:03X} (want 0x{:03X})",
                            resp.can_id & CAN_EFF_MASK, tc.expect_id),
                elapsed };

        bool ok = tc.predicate(resp);
        return { tc.id, ok,
                 ok ? "" : "payload predicate failed",
                 elapsed };
    }
};

/* ─── usage example ─────────────────────────────────────────── */
/*
int main()
{
    HilBench bench("vcan0");

    Signal rpm_sig { "EngineSpeed", 0, 16, 0.25, 0.0 };

    bench.add_test({
        .id          = "TC001_rpm_nominal",
        .description = "ECU reports 3000 RPM when requested",
        .stim_id     = 0x201,
        .stim_payload= { 0x01, 0x00 },
        .expect_id   = 0x301,
        .predicate   = [&rpm_sig](const struct can_frame &f) {
            double rpm = rpm_sig.decode(f.data);
            return rpm >= 2950.0 && rpm <= 3050.0;  // ±50 RPM tolerance
        },
        .timeout     = std::chrono::milliseconds(100),
    });

    auto results = bench.run_all();
    HilBench::print_junit(results);
    return std::ranges::all_of(results,
        [](const HilResult &r){ return r.passed; }) ? 0 : 1;
}
*/
```

### 4.3 Fault injection — forcing bus-off

```c
/* hil_fault_inject.c — Forces a bus-off condition by driving illegal
   dominant bits, then verifies the DUT recovers within a deadline.
   Requires a real CAN transceiver (not vcan) with error frame support.
   Build: gcc -O2 -Wall -o hil_fault hil_fault_inject.c
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

#define RECOVERY_DEADLINE_MS 1000   /* DUT must recover within 1 s     */
#define HEARTBEAT_ID         0x100  /* DUT sends this when healthy      */

static int open_can_errors(const char *iface)
{
    int fd = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) { perror("socket"); return -1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd, SIOCGIFINDEX, &ifr);

    /* subscribe to error frames from the kernel                        */
    can_err_mask_t err_mask = CAN_ERR_BUSOFF | CAN_ERR_CRTL | CAN_ERR_TX_TIMEOUT;
    setsockopt(fd, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &err_mask, sizeof err_mask);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    bind(fd, (struct sockaddr *)&addr, sizeof addr);
    return fd;
}

static void inject_bus_off(const char *iface)
{
    /* ip link set vcan0 type can restart-ms 0 — set via netlink in
       production; for brevity we use a system(3) call here.           */
    char cmd[128];
    snprintf(cmd, sizeof cmd,
             "ip link set %s type can bitrate 500000 restart-ms 0 2>/dev/null"
             " || true", iface);
    system(cmd);

    /* Generate 16 consecutive transmit errors to push TEC > 255.
       On a real bus with a dominant-bit injector this is hardware-driven;
       here we overflow TX by writing without ACK (loopback disabled).  */
    int fd_tx = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr2;
    strncpy(ifr2.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(fd_tx, SIOCGIFINDEX, &ifr2);
    int loopback = 0;
    setsockopt(fd_tx, SOL_CAN_RAW, CAN_RAW_LOOPBACK, &loopback, sizeof loopback);
    struct sockaddr_can a2 = {AF_CAN, .can_ifindex = ifr2.ifr_ifindex};
    bind(fd_tx, (struct sockaddr *)&a2, sizeof a2);

    struct can_frame f = { .can_id = 0x7FF, .can_dlc = 8 };
    memset(f.data, 0xDE, 8);
    for (int i = 0; i < 32; i++) write(fd_tx, &f, sizeof f);
    close(fd_tx);
}

int main(int argc, char *argv[])
{
    const char *iface = (argc > 1) ? argv[1] : "can0";
    int fd = open_can_errors(iface);
    if (fd < 0) return EXIT_FAILURE;

    printf("[HIL] Injecting bus-off on %s ...\n", iface);
    inject_bus_off(iface);

    /* Wait for heartbeat — proves DUT recovered */
    printf("[HIL] Waiting for DUT heartbeat (0x%03X) within %d ms ...\n",
           HEARTBEAT_ID, RECOVERY_DEADLINE_MS);

    struct pollfd pfd = { fd, POLLIN, 0 };
    int rc = poll(&pfd, 1, RECOVERY_DEADLINE_MS);
    if (rc <= 0) {
        printf("[HIL] FAIL — DUT did not recover within deadline\n");
        close(fd); return EXIT_FAILURE;
    }

    struct can_frame resp;
    read(fd, &resp, sizeof resp);

    if (resp.can_id & CAN_ERR_FLAG) {
        /* still receiving error frames, not data */
        printf("[HIL] FAIL — received error frame, not heartbeat\n");
        close(fd); return EXIT_FAILURE;
    }

    if ((resp.can_id & CAN_EFF_MASK) == HEARTBEAT_ID) {
        printf("[HIL] PASS — DUT recovered and sent heartbeat 0x%03X\n",
               HEARTBEAT_ID);
        close(fd); return EXIT_SUCCESS;
    }

    printf("[HIL] FAIL — unexpected frame 0x%03X during recovery\n",
           resp.can_id & CAN_EFF_MASK);
    close(fd);
    return EXIT_FAILURE;
}
```

### 4.4 Timing analysis — cycle-time validation

```cpp
/* hil_timing.cpp — Validates that a periodic ECU message arrives within
   its specified cycle time (e.g., 10 ms ± 2 ms for a 100 Hz signal).
   Build: g++ -std=c++17 -O2 -Wall -o hil_timing hil_timing.cpp
*/
#include <cstdio>
#include <cstring>
#include <vector>
#include <algorithm>
#include <chrono>
#include <stdexcept>

#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

using Clock = std::chrono::steady_clock;
using Ms    = std::chrono::duration<double, std::milli>;

static int open_can(const char *iface) {
    int fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr {}; strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ::ioctl(fd, SIOCGIFINDEX, &ifr);
    struct sockaddr_can a { AF_CAN, .can_ifindex = ifr.ifr_ifindex };
    ::bind(fd, (struct sockaddr *)&a, sizeof a);
    return fd;
}

struct TimingStats {
    double min_ms, max_ms, mean_ms, jitter_ms;
};

TimingStats measure_cycle(int fd, canid_t id, int n_samples, int timeout_ms)
{
    std::vector<double> deltas;
    deltas.reserve(n_samples);

    auto prev = Clock::now();
    int  collected = 0;

    while (collected < n_samples) {
        struct timeval tv { timeout_ms / 1000, (timeout_ms % 1000) * 1000 };
        fd_set fds; FD_ZERO(&fds); FD_SET(fd, &fds);
        if (::select(fd + 1, &fds, nullptr, nullptr, &tv) <= 0)
            throw std::runtime_error("timeout waiting for frame");

        struct can_frame f {};
        ::read(fd, &f, sizeof f);
        if ((f.can_id & CAN_EFF_MASK) != id) continue;

        auto now = Clock::now();
        if (collected > 0)
            deltas.push_back(Ms(now - prev).count());
        prev = now;
        collected++;
    }

    double sum = 0.0;
    for (double d : deltas) sum += d;
    double mean = sum / deltas.size();

    double jitter = 0.0;
    for (double d : deltas) jitter = std::max(jitter, std::abs(d - mean));

    return {
        *std::min_element(deltas.begin(), deltas.end()),
        *std::max_element(deltas.begin(), deltas.end()),
        mean,
        jitter,
    };
}

int main(int argc, char *argv[])
{
    const char *iface      = (argc > 1) ? argv[1] : "vcan0";
    const canid_t TARGET   = 0x100;    /* periodic ECU heartbeat  */
    const double  NOM_MS   = 10.0;     /* 100 Hz nominal          */
    const double  TOL_MS   = 2.0;      /* ±2 ms tolerance         */

    int fd = open_can(iface);
    printf("[HIL] Measuring cycle time of 0x%03X on %s ...\n", TARGET, iface);

    TimingStats s = measure_cycle(fd, TARGET, 50, 500);
    ::close(fd);

    printf("[HIL]  min=%.2f ms  max=%.2f ms  mean=%.2f ms  jitter=%.2f ms\n",
           s.min_ms, s.max_ms, s.mean_ms, s.jitter_ms);

    bool pass = (s.min_ms >= NOM_MS - TOL_MS) &&
                (s.max_ms <= NOM_MS + TOL_MS) &&
                (s.jitter_ms <= TOL_MS);

    printf("[HIL] TC_TIMING_%03X %s\n", TARGET, pass ? "PASS" : "FAIL");
    return pass ? 0 : 1;
}
```

---

## 5. Rust Implementation

### 5.1 Cargo dependencies

```toml
# Cargo.toml
[package]
name    = "hil_can"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan  = "3"          # SocketCAN bindings for Linux
anyhow     = "1"
thiserror  = "1"
tokio      = { version = "1", features = ["full"] }
quick-xml  = "0.36"       # JUnit XML report generation
```

### 5.2 Core HIL bench — async Rust with Tokio

```rust
// src/bench.rs — Async HIL test bench with SocketCAN
//
// The bench sends a stimulus frame, awaits a response within a deadline,
// and evaluates a user-supplied predicate over the received frame.
//
// Compile: cargo build --release
// Run:     sudo ./target/release/hil_can vcan0

use anyhow::{bail, Context, Result};
use socketcan::{CanFrame, CanSocket, EmbeddedFrame, Frame, Socket};
use std::{
    fmt,
    time::{Duration, Instant},
};

// ── error types ────────────────────────────────────────────────
#[derive(Debug, thiserror::Error)]
pub enum HilError {
    #[error("TX failed: {0}")]
    Tx(String),
    #[error("response timeout after {0:?}")]
    Timeout(Duration),
    #[error("unexpected frame ID 0x{received:03X} (expected 0x{expected:03X})")]
    WrongId { received: u32, expected: u32 },
    #[error("predicate failed: {0}")]
    Predicate(String),
}

// ── test verdict ───────────────────────────────────────────────
#[derive(Debug, Clone)]
pub enum Verdict {
    Pass { elapsed: Duration },
    Fail { elapsed: Duration, reason: HilError },
}

impl Verdict {
    pub fn is_pass(&self) -> bool { matches!(self, Verdict::Pass { .. }) }
}

impl fmt::Display for Verdict {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Verdict::Pass { elapsed } =>
                write!(f, "PASS  ({:.1} ms)", elapsed.as_secs_f64() * 1000.0),
            Verdict::Fail { elapsed, reason } =>
                write!(f, "FAIL  ({:.1} ms)  — {reason}", elapsed.as_secs_f64() * 1000.0),
        }
    }
}

// ── test case ──────────────────────────────────────────────────
pub struct HilTestCase {
    pub id:          String,
    pub stim_id:     u32,
    pub stim_data:   Vec<u8>,
    pub expect_id:   u32,
    pub predicate:   Box<dyn Fn(&CanFrame) -> Result<(), String> + Send + Sync>,
    pub timeout:     Duration,
}

// ── bench ──────────────────────────────────────────────────────
pub struct HilBench {
    socket: CanSocket,
    cases:  Vec<HilTestCase>,
}

impl HilBench {
    pub fn open(iface: &str) -> Result<Self> {
        let socket = CanSocket::open(iface)
            .with_context(|| format!("opening SocketCAN interface '{iface}'"))?;
        socket.set_read_timeout(Duration::from_millis(500))?;
        Ok(Self { socket, cases: Vec::new() })
    }

    pub fn add(&mut self, tc: HilTestCase) { self.cases.push(tc); }

    /// Run all test cases sequentially, returning one Verdict per case.
    pub fn run_all(&self) -> Vec<(&str, Verdict)> {
        self.cases.iter()
            .map(|tc| (tc.id.as_str(), self.run_one(tc)))
            .collect()
    }

    fn run_one(&self, tc: &HilTestCase) -> Verdict {
        let t0 = Instant::now();

        // --- transmit stimulus -------------------------------------------
        let dlc = tc.stim_data.len().min(8);
        let mut data = [0u8; 8];
        data[..dlc].copy_from_slice(&tc.stim_data[..dlc]);

        let frame = match CanFrame::new(
            socketcan::StandardId::new(tc.stim_id as u16)
                .expect("stimulus ID fits 11-bit standard CAN"),
            &data[..dlc],
        ) {
            Some(f) => f,
            None => return Verdict::Fail {
                elapsed: t0.elapsed(),
                reason:  HilError::Tx("could not build CAN frame".into()),
            },
        };

        if let Err(e) = self.socket.write_frame(&frame) {
            return Verdict::Fail {
                elapsed: t0.elapsed(),
                reason:  HilError::Tx(e.to_string()),
            };
        }

        // --- receive response --------------------------------------------
        loop {
            if t0.elapsed() >= tc.timeout {
                return Verdict::Fail {
                    elapsed: t0.elapsed(),
                    reason:  HilError::Timeout(tc.timeout),
                };
            }

            let resp = match self.socket.read_frame() {
                Ok(f)  => f,
                Err(_) => return Verdict::Fail {
                    elapsed: t0.elapsed(),
                    reason:  HilError::Timeout(tc.timeout),
                },
            };

            let resp_id = resp.raw_id() & 0x1FFF_FFFF;
            if resp_id != tc.expect_id { continue; } // wait for right ID

            // --- evaluate predicate --------------------------------------
            match (tc.predicate)(&resp) {
                Ok(()) => return Verdict::Pass { elapsed: t0.elapsed() },
                Err(msg) => return Verdict::Fail {
                    elapsed: t0.elapsed(),
                    reason:  HilError::Predicate(msg),
                },
            }
        }
    }
}

// ── JUnit XML report ───────────────────────────────────────────
pub fn emit_junit(results: &[(&str, Verdict)], suite: &str) {
    let failures = results.iter().filter(|(_, v)| !v.is_pass()).count();
    println!(r#"<?xml version="1.0" encoding="UTF-8"?>"#);
    println!(r#"<testsuite name="{suite}" tests="{}" failures="{failures}">"#,
             results.len());
    for (id, verdict) in results {
        let ms = match verdict {
            Verdict::Pass { elapsed } | Verdict::Fail { elapsed, .. } =>
                elapsed.as_secs_f64(),
        };
        println!(r#"  <testcase name="{id}" time="{ms:.3}">"#);
        if let Verdict::Fail { reason, .. } = verdict {
            println!(r#"    <failure message="{reason}" />"#);
        }
        println!("  </testcase>");
    }
    println!("</testsuite>");
}
```

### 5.3 Main entry point with registered test cases

```rust
// src/main.rs — Wires up the HilBench, registers test cases, prints results

mod bench;
use bench::{HilBench, HilTestCase, emit_junit};
use socketcan::{EmbeddedFrame, Frame};
use std::{env, process, time::Duration};

fn main() {
    let iface = env::args().nth(1).unwrap_or_else(|| "vcan0".into());

    let mut b = HilBench::open(&iface).unwrap_or_else(|e| {
        eprintln!("[HIL] Fatal: {e}");
        process::exit(1);
    });

    // ── TC001: engine speed ──────────────────────────────────────────────
    b.add(HilTestCase {
        id:        "TC001_engine_speed".into(),
        stim_id:   0x201,
        stim_data: vec![0x01, 0x00],
        expect_id: 0x301,
        timeout:   Duration::from_millis(100),
        predicate: Box::new(|frame| {
            let d = frame.data();
            if d.len() < 3 { return Err("DLC too short".into()); }
            // Bytes 1–2 hold RPM as big-endian u16, factor 1.0
            let rpm = u16::from_be_bytes([d[1], d[2]]) as f64;
            if (2950.0..=3050.0).contains(&rpm) {
                Ok(())
            } else {
                Err(format!("RPM {rpm:.0} outside [2950, 3050]"))
            }
        }),
    });

    // ── TC002: fault — DLC mismatch ──────────────────────────────────────
    b.add(HilTestCase {
        id:        "TC002_dlc_validation".into(),
        stim_id:   0x201,
        stim_data: vec![0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF],
        expect_id: 0x301,
        timeout:   Duration::from_millis(100),
        predicate: Box::new(|frame| {
            // Expect ECU to respond with an error code byte at offset 0
            let d = frame.data();
            if d.first() == Some(&0xFE) {
                Ok(())
            } else {
                Err(format!("expected error code 0xFE, got 0x{:02X}",
                            d.first().copied().unwrap_or(0xFF)))
            }
        }),
    });

    // ── TC003: cycle time (indirect — single round-trip latency) ─────────
    b.add(HilTestCase {
        id:        "TC003_latency_under_10ms".into(),
        stim_id:   0x201,
        stim_data: vec![0x01, 0x00],
        expect_id: 0x301,
        timeout:   Duration::from_millis(10),   // tight deadline = latency check
        predicate: Box::new(|_| Ok(())),        // any response is acceptable
    });

    // ── execute ───────────────────────────────────────────────────────────
    println!("[HIL] Running {} test cases on {iface}\n", b.run_all().len());
    // run again to capture — run_all is non-destructive
    let results = b.run_all();

    let mut pass = 0usize;
    let mut fail = 0usize;
    for (id, verdict) in &results {
        let ok = verdict.is_pass();
        println!("  {:<40}  {verdict}", id);
        if ok { pass += 1; } else { fail += 1; }
    }

    println!("\n[HIL] {pass} passed, {fail} failed / {} total\n",
             results.len());

    // Emit JUnit XML for CI integration
    emit_junit(&results, "HIL_CAN_Suite");

    process::exit(if fail == 0 { 0 } else { 1 });
}
```

### 5.4 Timing statistics utility (Rust)

```rust
// src/timing.rs — Collects N timestamps of a periodic CAN message and
// computes min / max / mean cycle time and jitter.

use anyhow::Result;
use socketcan::{CanSocket, EmbeddedFrame, Frame, Socket};
use std::time::{Duration, Instant};

pub struct CycleStats {
    pub min_ms:    f64,
    pub max_ms:    f64,
    pub mean_ms:   f64,
    pub jitter_ms: f64,
    pub samples:   usize,
}

pub fn measure_cycle(iface: &str, can_id: u32, n: usize, timeout: Duration)
    -> Result<CycleStats>
{
    let sock = CanSocket::open(iface)?;
    sock.set_read_timeout(timeout)?;

    let mut deltas: Vec<f64> = Vec::with_capacity(n);
    let mut prev   = Instant::now();
    let mut count  = 0usize;

    while count < n {
        let frame = sock.read_frame()?;
        if frame.raw_id() & 0x1FFF_FFFF != can_id { continue; }

        let now = Instant::now();
        if count > 0 {
            deltas.push((now - prev).as_secs_f64() * 1000.0);
        }
        prev  = now;
        count += 1;
    }

    let min = deltas.iter().cloned().fold(f64::INFINITY, f64::min);
    let max = deltas.iter().cloned().fold(f64::NEG_INFINITY, f64::max);
    let mean = deltas.iter().sum::<f64>() / deltas.len() as f64;
    let jitter = deltas.iter().map(|d| (d - mean).abs())
                        .fold(0.0_f64, f64::max);

    Ok(CycleStats { min_ms: min, max_ms: max, mean_ms: mean,
                    jitter_ms: jitter, samples: deltas.len() })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn stats_nominal_10hz() {
        // Synthetic deltas simulating a 10 ms periodic message
        let deltas = vec![9.8, 10.1, 10.0, 9.9, 10.2];
        let mean: f64 = deltas.iter().sum::<f64>() / deltas.len() as f64;
        let jitter = deltas.iter().map(|d| (d - mean).abs())
                            .fold(0.0_f64, f64::max);
        assert!((mean - 10.0).abs() < 0.2);
        assert!(jitter < 0.3);
    }
}
```

---

## 6. CI Integration

### 6.1 Virtual CAN setup script (Linux)

```bash
#!/usr/bin/env bash
# setup_vcan.sh — Creates a virtual CAN interface for CI pipelines
set -euo pipefail

IFACE="${1:-vcan0}"
modprobe vcan 2>/dev/null || true
ip link show "$IFACE" &>/dev/null && ip link delete "$IFACE"
ip link add dev "$IFACE" type vcan
ip link set up "$IFACE"
echo "[setup] $IFACE is up"
```

### 6.2 GitHub Actions workflow

```yaml
# .github/workflows/hil_can.yml
name: HIL CAN regression

on: [push, pull_request]

jobs:
  hil:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Setup virtual CAN
        run: sudo bash setup_vcan.sh vcan0

      - name: Build HIL bench (C)
        run: gcc -O2 -Wall -o hil_sender src/hil_sender.c

      - name: Build HIL bench (Rust)
        run: cargo build --release

      - name: Run C HIL tests
        run: sudo ./hil_sender vcan0
        timeout-minutes: 2

      - name: Run Rust HIL tests (JUnit output)
        run: sudo ./target/release/hil_can vcan0 2>&1 | tee results.xml
        timeout-minutes: 2

      - name: Publish test report
        uses: mikepenz/action-junit-report@v4
        if: always()
        with:
          report_paths: results.xml
```

---

## 7. HIL Test Design Patterns

| Pattern | Description | When to use |
|---|---|---|
| **Stimulus / response** | Send one CAN frame; assert response matches spec | Function tests, API coverage |
| **Timing validation** | Measure N cycle-time deltas; check min/max/jitter | Periodic heartbeats, sensor signals |
| **Negative / fault** | Inject malformed frames or bus-off; assert ECU recovers | Robustness, ISO 11898 compliance |
| **State-machine walk** | Drive ECU through a state sequence via CAN commands | Boot, diagnostic session, mode changes |
| **Load / stress** | Flood bus near 100 % utilisation; assert no message loss | Bus-load compliance, latency budgets |
| **Regression baseline** | Record golden CAN trace; diff every CI run against it | Prevent regressions across SW releases |

---

## 8. Real Hardware vs Virtual CAN

| Aspect | `vcan` (virtual) | Real CAN adapter |
|---|---|---|
| Cost | Free, always-on in CI | Peak/Vector hardware required |
| Error frame support | None | Full (bus-off, bit errors) |
| Timing accuracy | OS scheduler, ± ms | Sub-µs with HW timestamps |
| Regression suitability | Excellent | Required for timing / EMC tests |
| Best for | Logic / protocol tests | Timing, EMC, fault injection |

---

## 9. Summary

Hardware-in-the-Loop testing positions a **real ECU under automated control** of a software test harness that speaks native CAN. The key engineering concerns are:

**Test bench architecture** — A Linux host running SocketCAN drives one or more CAN channels. One channel connects to the physical ECU (DUT); a second channel may simulate absent peer nodes. Signal conditioning boards supply power and hard-wired signals that the ECU expects.

**Stimulus / response pattern** — Every test case defines a CAN frame stimulus (ID + payload), an expected response (ID + predicate), and a response deadline. The bench transmits the stimulus, polls for the matching response, evaluates a typed predicate over the payload bytes, and records pass / fail with timing metadata.

**Signal decoding** — DBC files describe how physical values (RPM, voltage, temperature) map to raw bit fields. The bench decodes signals before applying predicates, giving tests that read `assert!(rpm >= 2950.0)` rather than raw byte comparisons.

**Fault injection** — Robustness tests force the ECU into adverse conditions: bus-off (transmit error counter overflow), frame corruption (dominant bit injection), missing acknowledgements, and late or absent periodic messages. The bench verifies that the ECU recovers within its specified deadline.

**Timing analysis** — Automotive CAN signals are periodic. The bench records arrival timestamps over N consecutive frames and derives min/max/mean cycle time and worst-case jitter, asserting these fall within the signal's specified tolerances.

**CI integration** — Virtual CAN (`vcan`) replaces real hardware in headless pipelines. Tests produce JUnit XML consumed by GitHub Actions, GitLab CI, or Jenkins. `vcan` covers logic and protocol correctness; real hardware CAN channels cover timing, EMC, and electrical fault scenarios.

**C / C++ strengths** — Direct SocketCAN `write(2)` / `read(2)` calls give the lowest latency and exact byte-level control. The `HilBench` C++ class wraps this in a type-safe API with `std::function` predicates, DLC validation, and JUnit emission.

**Rust strengths** — The `socketcan` crate provides safe, idiomatic bindings with strong type guarantees over frame IDs and DLC. Ownership semantics prevent double-free and data races in multi-threaded bench runners. `thiserror` produces structured, match-able error variants (`HilError::Timeout`, `HilError::Predicate`) that drive clear CI failure messages without panics.

Both languages compile to a single native binary with no runtime dependency, suitable for deployment on embedded Linux targets (Yocto, Buildroot) co-located with the ECU under test.