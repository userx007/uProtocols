# CAN Fault Injection Testing

Fault Injection Testing (FIT) is a disciplined methodology for validating the robustness and fault-tolerance of CAN bus networks by deliberately introducing controlled failures. Rather than waiting for hardware defects or field failures to reveal software weaknesses, FIT engineers systematically *force* the system into abnormal states and observe whether it recovers correctly — or fails gracefully.

CAN was designed with fault confinement in mind (TEC/REC counters, Bus-Off state, error frames), but those mechanisms only work if the software layer is implemented correctly. FIT is the discipline that verifies this.---

## Core Fault Categories

### 1. Physical Layer Faults

These simulate real hardware failures that field engineers encounter:

- **Short circuit CANH–CANL**: Collapses the differential voltage to ~0 V; all nodes see a permanent dominant bit — the bus is stuck and cannot recover without hardware intervention.
- **Short to GND / VCC**: Drives one wire to a fixed rail, again destroying the differential signal.
- **Open circuit / missing termination**: Reflections cause corrupt frames and form-error flags; symptoms are intermittent and notoriously hard to reproduce.
- **Damaged transceiver**: A stuck transmitter can masquerade as a babbling node.

### 2. Protocol-Level Faults

Injected at the bit level, typically requiring a capable CAN controller or FPGA:

- **Bit-stuffing violations**: After 5 consecutive identical bits, CAN requires a complementary stuff bit. Injecting a 6th same-polarity bit causes every active receiver to emit an error frame.
- **CRC errors**: Corrupting the 15-bit CRC field causes all receivers to flag a CRC Error; the transmitting node raises its TEC by 8.
- **Form errors**: Violations in the fixed-format fields (EOF, ACK delimiter, etc.) trigger form-error frames.
- **ACK errors**: A transmitter sees no dominant ACK slot — every receiver failed to acknowledge.

### 3. Babbling Node Fault

A node continuously transmitting without regard for bus access rules. This is one of the most destructive scenarios in safety-critical systems. Because CAN is CSMA/CD with priority arbitration, a high-priority babbling frame (low arbitration ID) can completely starve legitimate traffic.

ISO 26262 and SAE J1939 both require that the system handle babbling nodes gracefully, typically through a Bus Guardian or hardware watchdog that silences the transmitter when TEC reaches 255 (Bus-Off).

### 4. Timing and Overload Faults

- **Overload frames**: Deliberately triggered between data frames to stress receiver buffer handling.
- **Burst injection**: Rapid-fire frames at maximum bus utilisation (theoretical max ~85% for 8-byte frames) to verify that the DUT's receive FIFO does not overflow and drop frames silently.
- **Late/missing heartbeat**: Tests whether the application layer correctly detects and handles missing periodic messages (PDU supervision in J1939, AUTOSAR PDU routers, etc.).

### 5. Semantic Faults

The message arrives intact at the protocol level but carries wrong data:

- **Counter rollover errors**: Many safety-critical systems embed a rolling counter in each PDU. Injecting a frame with a skipped counter value should trigger a sequence error.
- **Signal range violations**: Values outside the physical range (e.g., an engine RPM signal of 0xFFFF when max is 8000 RPM) should trigger application-layer plausibility checks.
- **Wrong CAN-ID on known payload**: Tests whether the application confuses messages from different senders.

---

## C/C++ Implementation

The C implementation covers the core simulation framework — a fault injector that operates over the SocketCAN interface (Linux) and can also drive a hardware CAN controller register interface directly.

```c
/* can_fault_injector.h — CAN Fault Injection Framework (C11)
 *
 * Targets: Linux SocketCAN (vcan0/can0) or bare-metal via
 * a thin HAL abstraction. Requires libsocketcan on Linux.
 *
 * Compile:
 *   gcc -std=c11 -Wall -Wextra -o fit can_fault_injector.c -lpthread
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* ── Fault types ─────────────────────────────────────────── */
typedef enum {
    FAULT_NONE             = 0x00,
    FAULT_BIT_FLIP         = 0x01,  /* Flip N bits in payload          */
    FAULT_CRC_CORRUPT      = 0x02,  /* Replace CRC with wrong value     */
    FAULT_BABBLING_NODE    = 0x04,  /* Flood bus with high-prio frames  */
    FAULT_MISSING_FRAME    = 0x08,  /* Suppress periodic message        */
    FAULT_WRONG_ID         = 0x10,  /* Send known payload on wrong ID   */
    FAULT_OVERLOAD_BURST   = 0x20,  /* Rapid-fire frames                */
    FAULT_COUNTER_SKIP     = 0x40,  /* Skip rolling counter value       */
    FAULT_DELAYED_FRAME    = 0x80,  /* Inject artificial latency        */
} fault_type_t;

/* ── Fault descriptor ────────────────────────────────────── */
typedef struct {
    fault_type_t   type;
    uint32_t       target_id;       /* CAN ID to perturb                */
    uint32_t       injected_id;     /* Used for FAULT_WRONG_ID          */
    uint8_t        bit_mask[8];     /* Which payload bits to flip       */
    uint32_t       burst_count;     /* Frames per burst (OVERLOAD)      */
    uint32_t       burst_interval_us; /* Microseconds between frames    */
    uint32_t       delay_us;        /* Delay for FAULT_DELAYED_FRAME    */
    uint8_t        counter_skip;    /* How many counter values to skip  */
    bool           extended;        /* Extended (29-bit) CAN ID         */
    unsigned int   duration_ms;     /* 0 = single shot, >0 = sustained  */
} fault_descriptor_t;

/* ── Error counter snapshot ──────────────────────────────── */
typedef struct {
    uint8_t  tec;           /* Transmit Error Counter               */
    uint8_t  rec;           /* Receive Error Counter                */
    bool     bus_off;       /* True when TEC >= 256                 */
    uint32_t error_frames;  /* Total error frames seen              */
    uint32_t lost_frames;   /* Frames dropped (overrun)             */
} can_error_state_t;

/* ── Test result ─────────────────────────────────────────── */
typedef enum {
    RESULT_PASS,
    RESULT_FAIL_NO_RECOVERY,
    RESULT_FAIL_WRONG_DATA,
    RESULT_FAIL_TIMEOUT,
    RESULT_FAIL_BUS_OFF,
} test_result_t;

typedef struct {
    test_result_t      outcome;
    can_error_state_t  final_state;
    unsigned long      frames_injected;
    unsigned long      frames_received_by_dut;
    double             recovery_time_ms;
    char               description[256];
} test_report_t;

/* ── HAL interface (implement per platform) ──────────────── */
typedef struct {
    int  (*open)(const char *iface);
    void (*close)(int fd);
    int  (*send)(int fd, uint32_t id, bool ext,
                 const uint8_t *data, uint8_t dlc);
    int  (*recv)(int fd, uint32_t *id, uint8_t *data,
                 uint8_t *dlc, uint32_t timeout_ms);
    bool (*get_error_state)(int fd, can_error_state_t *out);
} can_hal_t;

int  fit_run(const can_hal_t      *hal,
             int                   fd,
             const fault_descriptor_t *fault,
             test_report_t        *report_out);
```

```c
/* can_fault_injector.c — implementation */
#include "can_fault_injector.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>

/* ── Internal helpers ────────────────────────────────────── */
static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

static void sleep_us(uint32_t us) {
    struct timespec req = {
        .tv_sec  = us / 1000000,
        .tv_nsec = (us % 1000000) * 1000,
    };
    nanosleep(&req, NULL);
}

/* ── Payload helpers ─────────────────────────────────────── */
static void apply_bit_flip(uint8_t *payload, uint8_t dlc,
                            const uint8_t *mask) {
    for (uint8_t i = 0; i < dlc && i < 8; i++)
        payload[i] ^= mask[i];
}

/* Rolling counter helper: byte 0 holds the counter. */
static void advance_counter_with_skip(uint8_t *payload, uint8_t skip) {
    payload[0] = (uint8_t)(payload[0] + 1 + skip);
}

/* ── Babbling node thread ─────────────────────────────────── */
typedef struct {
    const can_hal_t       *hal;
    int                    fd;
    const fault_descriptor_t *fault;
    volatile bool          stop;
    unsigned long          count;
} babble_ctx_t;

static void *babble_thread(void *arg) {
    babble_ctx_t *ctx = arg;
    uint8_t  payload[8];
    memset(payload, 0xBB, 8);           /* Marker pattern            */
    while (!ctx->stop) {
        ctx->hal->send(ctx->fd,
                       ctx->fault->target_id,
                       ctx->fault->extended,
                       payload, 8);
        ctx->count++;
        if (ctx->fault->burst_interval_us)
            sleep_us(ctx->fault->burst_interval_us);
    }
    return NULL;
}

/* ── Core runner ─────────────────────────────────────────── */
int fit_run(const can_hal_t          *hal,
            int                       fd,
            const fault_descriptor_t *fault,
            test_report_t            *report) {
    memset(report, 0, sizeof(*report));
    report->outcome = RESULT_PASS;

    uint64_t t_start = now_us();
    uint64_t t_end   = t_start
                     + (uint64_t)fault->duration_ms * 1000ULL;

    /* ── BABBLING NODE ────────────────────────────────────── */
    if (fault->type & FAULT_BABBLING_NODE) {
        babble_ctx_t ctx = {
            .hal   = hal,
            .fd    = fd,
            .fault = fault,
            .stop  = false,
            .count = 0,
        };
        pthread_t tid;
        pthread_create(&tid, NULL, babble_thread, &ctx);

        /* Monitor DUT error state during babble phase */
        can_error_state_t es;
        while (now_us() < t_end) {
            hal->get_error_state(fd, &es);
            if (es.bus_off) {
                report->outcome = RESULT_FAIL_BUS_OFF;
                snprintf(report->description,
                         sizeof(report->description),
                         "DUT entered Bus-Off after %lu babble frames",
                         ctx.count);
            }
            sleep_us(10000);  /* poll every 10 ms */
        }

        ctx.stop = true;
        pthread_join(tid, NULL);
        report->frames_injected = ctx.count;

        /* Allow DUT time to recover from Bus-Off */
        if (es.bus_off) {
            uint64_t t_recovery = now_us();
            do {
                hal->get_error_state(fd, &es);
                sleep_us(5000);
            } while (es.bus_off &&
                     (now_us() - t_recovery) < 2000000ULL); /* 2 s */

            if (!es.bus_off) {
                report->recovery_time_ms =
                    (now_us() - t_recovery) / 1000.0;
                /* Outcome upgrades to PASS only if it was Bus-Off */
                if (report->outcome == RESULT_FAIL_BUS_OFF)
                    report->outcome = RESULT_PASS;
            } else {
                report->outcome = RESULT_FAIL_NO_RECOVERY;
            }
        }
        hal->get_error_state(fd, &report->final_state);
        return 0;
    }

    /* ── BIT FLIP + CRC + MISSING + WRONG-ID ─────────────── */
    uint8_t  payload[8] = {0x01, 0x02, 0x03, 0x04,
                           0x05, 0x06, 0x07, 0x08};
    uint8_t  dlc        = 8;
    bool     single     = (fault->duration_ms == 0);

    do {
        uint8_t  tx[8];
        memcpy(tx, payload, dlc);
        uint32_t tx_id = fault->target_id;

        if (fault->type & FAULT_BIT_FLIP)
            apply_bit_flip(tx, dlc, fault->bit_mask);

        if (fault->type & FAULT_COUNTER_SKIP)
            advance_counter_with_skip(tx, fault->counter_skip);

        if (fault->type & FAULT_WRONG_ID)
            tx_id = fault->injected_id;

        if (fault->type & FAULT_MISSING_FRAME) {
            /* Suppress: do nothing, just wait cycle */
            sleep_us(fault->burst_interval_us ?: 10000);
        } else {
            if (fault->type & FAULT_DELAYED_FRAME)
                sleep_us(fault->delay_us);

            int rc = hal->send(fd, tx_id, fault->extended, tx, dlc);
            if (rc < 0) {
                report->outcome = RESULT_FAIL_NO_RECOVERY;
                break;
            }
            report->frames_injected++;
        }

        /* Overload burst: fire N extra frames immediately */
        if (fault->type & FAULT_OVERLOAD_BURST) {
            for (uint32_t b = 0; b < fault->burst_count; b++) {
                hal->send(fd, tx_id, fault->extended, tx, dlc);
                report->frames_injected++;
                if (fault->burst_interval_us)
                    sleep_us(fault->burst_interval_us);
            }
        }

        /* Increment rolling counter for next iteration */
        payload[0]++;

    } while (!single && now_us() < t_end);

    hal->get_error_state(fd, &report->final_state);
    if (report->final_state.bus_off)
        report->outcome = RESULT_FAIL_BUS_OFF;

    return 0;
}
```

```cpp
/* fit_test_suite.cpp — C++17 test orchestrator
 *
 * Wraps the C injector, provides RAII resource management,
 * scenario builder, and HTML report generation.
 *
 * Compile:
 *   g++ -std=c++17 -Wall -Wextra -o fit_suite fit_test_suite.cpp \
 *       can_fault_injector.c -lpthread
 */
#include "can_fault_injector.h"
#include <algorithm>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <memory>
#include <cstring>

/* ── RAII CAN handle ─────────────────────────────────────── */
class CanDevice {
    const can_hal_t *hal_;
    int              fd_;

public:
    explicit CanDevice(const can_hal_t *hal, const std::string &iface)
        : hal_(hal), fd_(hal->open(iface.c_str()))
    {
        if (fd_ < 0)
            throw std::runtime_error("Failed to open CAN interface: " + iface);
    }
    ~CanDevice() { hal_->close(fd_); }

    int fd() const { return fd_; }
    const can_hal_t *hal() const { return hal_; }

    CanDevice(const CanDevice &)             = delete;
    CanDevice &operator=(const CanDevice &)  = delete;
};

/* ── Scenario ────────────────────────────────────────────── */
struct Scenario {
    std::string        name;
    fault_descriptor_t fault;
    /* Optional DUT validation callback — receives the report */
    std::function<bool(const test_report_t &)> validate;
};

/* ── Test runner ─────────────────────────────────────────── */
class FitTestSuite {
    CanDevice              device_;
    std::vector<Scenario>  scenarios_;

public:
    FitTestSuite(const can_hal_t *hal, const std::string &iface)
        : device_(hal, iface) {}

    void add(Scenario s) { scenarios_.push_back(std::move(s)); }

    struct Summary {
        int total{};
        int passed{};
        int failed{};
        double total_time_ms{};
        std::vector<std::pair<std::string, test_report_t>> reports;
    };

    Summary run_all() {
        Summary sum;
        for (const auto &sc : scenarios_) {
            auto t0 = std::chrono::steady_clock::now();

            test_report_t report{};
            fit_run(device_.hal(), device_.fd(), &sc.fault, &report);

            auto t1 = std::chrono::steady_clock::now();
            double elapsed =
                std::chrono::duration<double, std::milli>(t1 - t0).count();

            /* Apply optional application-level validation */
            bool ok = (report.outcome == RESULT_PASS);
            if (ok && sc.validate)
                ok = sc.validate(report);
            if (!ok && report.outcome == RESULT_PASS)
                report.outcome = RESULT_FAIL_WRONG_DATA;

            sum.total++;
            ok ? sum.passed++ : sum.failed++;
            sum.total_time_ms += elapsed;
            sum.reports.emplace_back(sc.name, report);

            std::cout << (ok ? "[PASS]" : "[FAIL]")
                      << "  " << sc.name
                      << "  (" << elapsed << " ms)\n";
        }
        return sum;
    }

    static void write_html_report(const Summary &sum,
                                  const std::string &path) {
        std::ofstream f(path);
        if (!f) return;

        f << "<!DOCTYPE html><html><head>\n"
          << "<meta charset='utf-8'>\n"
          << "<title>CAN FIT Report</title>\n"
          << "<style>body{font-family:sans-serif;padding:2rem}"
          << "table{border-collapse:collapse;width:100%}"
          << "th,td{border:1px solid #ccc;padding:.5rem .75rem;text-align:left}"
          << "th{background:#f4f4f4}"
          << ".pass{color:green;font-weight:bold}"
          << ".fail{color:red;font-weight:bold}</style>\n"
          << "</head><body>\n"
          << "<h1>CAN Fault Injection Test Report</h1>\n"
          << "<p>Total: " << sum.total
          << " | Passed: " << sum.passed
          << " | Failed: " << sum.failed
          << " | Time: " << sum.total_time_ms << " ms</p>\n"
          << "<table><tr><th>Scenario</th><th>Outcome</th>"
          << "<th>Frames injected</th><th>Recovery ms</th>"
          << "<th>TEC</th><th>Bus-Off</th><th>Notes</th></tr>\n";

        static const char *outcome_str[] = {
            "PASS", "FAIL_NO_RECOVERY", "FAIL_WRONG_DATA",
            "FAIL_TIMEOUT", "FAIL_BUS_OFF"
        };

        for (const auto &[name, r] : sum.reports) {
            const char *cls = (r.outcome == RESULT_PASS) ? "pass" : "fail";
            f << "<tr>"
              << "<td>" << name << "</td>"
              << "<td class='" << cls << "'>"
                 << outcome_str[r.outcome] << "</td>"
              << "<td>" << r.frames_injected << "</td>"
              << "<td>" << r.recovery_time_ms << "</td>"
              << "<td>" << static_cast<int>(r.final_state.tec) << "</td>"
              << "<td>" << (r.final_state.bus_off ? "YES" : "no") << "</td>"
              << "<td>" << r.description << "</td>"
              << "</tr>\n";
        }

        f << "</table></body></html>\n";
        std::cout << "Report written to " << path << "\n";
    }
};

/* ── Demo (substitute real HAL for your platform) ────────── */
int main() {
    extern const can_hal_t socketcan_hal; /* Defined separately */

    FitTestSuite suite(&socketcan_hal, "vcan0");

    /* ── Scenario 1: single-byte bit flip on ID 0x123 ──── */
    {
        Scenario sc;
        sc.name = "Bit-flip on payload byte 0 (ID 0x123)";
        sc.fault = {
            .type            = FAULT_BIT_FLIP,
            .target_id       = 0x123,
            .bit_mask        = {0x01, 0, 0, 0, 0, 0, 0, 0},
            .burst_count     = 0,
            .burst_interval_us = 0,
            .delay_us        = 0,
            .counter_skip    = 0,
            .extended        = false,
            .duration_ms     = 500,
        };
        sc.validate = [](const test_report_t &r) {
            /* DUT must not enter Bus-Off from a payload fault */
            return !r.final_state.bus_off;
        };
        suite.add(std::move(sc));
    }

    /* ── Scenario 2: babbling node on high-priority ID ──── */
    {
        Scenario sc;
        sc.name  = "Babbling node (ID 0x001, 2 seconds)";
        sc.fault = {
            .type              = FAULT_BABBLING_NODE,
            .target_id         = 0x001,
            .burst_interval_us = 100,   /* ~10 000 fps */
            .extended          = false,
            .duration_ms       = 2000,
        };
        sc.validate = [](const test_report_t &r) {
            /* System must recover in under 1 second */
            return r.recovery_time_ms < 1000.0;
        };
        suite.add(std::move(sc));
    }

    /* ── Scenario 3: counter skip ──────────────────────── */
    {
        Scenario sc;
        sc.name  = "Counter skip by 2 on ID 0x200";
        sc.fault = {
            .type              = FAULT_COUNTER_SKIP,
            .target_id         = 0x200,
            .burst_interval_us = 10000, /* 100 fps */
            .counter_skip      = 2,
            .extended          = false,
            .duration_ms       = 1000,
        };
        suite.add(std::move(sc));
    }

    /* ── Scenario 4: missing frame (heartbeat timeout) ─── */
    {
        Scenario sc;
        sc.name  = "Missing heartbeat on ID 0x300 (500 ms)";
        sc.fault = {
            .type              = FAULT_MISSING_FRAME,
            .target_id         = 0x300,
            .burst_interval_us = 10000,
            .extended          = false,
            .duration_ms       = 500,
        };
        suite.add(std::move(sc));
    }

    auto summary = suite.run_all();
    FitTestSuite::write_html_report(summary, "fit_report.html");

    return (summary.failed == 0) ? 0 : 1;
}
```

---

## Rust Implementation

The Rust version provides stronger type-safety guarantees, uses `async` for concurrent monitoring without unsafe threads, and leverages the type system to prevent invalid fault configurations at compile time.

```rust
// can_fit.rs — CAN Fault Injection Framework (Rust, no_std-friendly core)
//
// Dependencies (Cargo.toml):
//   [dependencies]
//   socketcan = "3"
//   tokio     = { version = "1", features = ["full"] }
//   bitflags  = "2"
//   thiserror = "1"

use std::time::{Duration, Instant};
use bitflags::bitflags;
use thiserror::Error;

// ── Error type ─────────────────────────────────────────────
#[derive(Debug, Error)]
pub enum FitError {
    #[error("CAN I/O error: {0}")]
    Io(#[from] std::io::Error),
    #[error("DUT did not recover within {0:?}")]
    NoRecovery(Duration),
    #[error("Bus-Off state entered")]
    BusOff,
    #[error("Validation callback rejected result")]
    ValidationFailed(String),
}

// ── Fault flags ────────────────────────────────────────────
bitflags! {
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub struct FaultFlags: u32 {
        const BIT_FLIP        = 0b00000001;
        const BABBLING_NODE   = 0b00000010;
        const MISSING_FRAME   = 0b00000100;
        const WRONG_ID        = 0b00001000;
        const OVERLOAD_BURST  = 0b00010000;
        const COUNTER_SKIP    = 0b00100000;
        const DELAYED_FRAME   = 0b01000000;
    }
}

// ── Strongly-typed fault descriptor ───────────────────────
#[derive(Debug, Clone)]
pub struct FaultDescriptor {
    pub flags:            FaultFlags,
    pub target_id:        u32,
    pub injected_id:      Option<u32>,      // For WRONG_ID
    pub bit_mask:         [u8; 8],          // For BIT_FLIP
    pub burst_count:      u32,              // For OVERLOAD_BURST
    pub frame_interval:   Duration,         // Between consecutive frames
    pub delay:            Option<Duration>, // For DELAYED_FRAME
    pub counter_skip:     u8,              // For COUNTER_SKIP
    pub extended:         bool,
    pub duration:         Option<Duration>, // None = single shot
}

// ── Builder ────────────────────────────────────────────────
#[derive(Default)]
pub struct FaultBuilder {
    descriptor: FaultDescriptor,
}

impl Default for FaultDescriptor {
    fn default() -> Self {
        Self {
            flags:          FaultFlags::empty(),
            target_id:      0,
            injected_id:    None,
            bit_mask:       [0; 8],
            burst_count:    0,
            frame_interval: Duration::from_millis(10),
            delay:          None,
            counter_skip:   0,
            extended:       false,
            duration:       None,
        }
    }
}

impl FaultBuilder {
    pub fn new(target_id: u32) -> Self {
        let mut fb = Self::default();
        fb.descriptor.target_id = target_id;
        fb
    }
    pub fn bit_flip(mut self, mask: [u8; 8]) -> Self {
        self.descriptor.flags |= FaultFlags::BIT_FLIP;
        self.descriptor.bit_mask = mask;
        self
    }
    pub fn babbling_node(mut self) -> Self {
        self.descriptor.flags |= FaultFlags::BABBLING_NODE;
        self
    }
    pub fn missing_frame(mut self) -> Self {
        self.descriptor.flags |= FaultFlags::MISSING_FRAME;
        self
    }
    pub fn wrong_id(mut self, injected: u32) -> Self {
        self.descriptor.flags |= FaultFlags::WRONG_ID;
        self.descriptor.injected_id = Some(injected);
        self
    }
    pub fn overload_burst(mut self, count: u32) -> Self {
        self.descriptor.flags |= FaultFlags::OVERLOAD_BURST;
        self.descriptor.burst_count = count;
        self
    }
    pub fn counter_skip(mut self, skip: u8) -> Self {
        self.descriptor.flags |= FaultFlags::COUNTER_SKIP;
        self.descriptor.counter_skip = skip;
        self
    }
    pub fn delayed(mut self, delay: Duration) -> Self {
        self.descriptor.flags |= FaultFlags::DELAYED_FRAME;
        self.descriptor.delay = Some(delay);
        self
    }
    pub fn interval(mut self, interval: Duration) -> Self {
        self.descriptor.frame_interval = interval;
        self
    }
    pub fn duration(mut self, d: Duration) -> Self {
        self.descriptor.duration = Some(d);
        self
    }
    pub fn extended(mut self) -> Self {
        self.descriptor.extended = true;
        self
    }
    pub fn build(self) -> FaultDescriptor {
        self.descriptor
    }
}

// ── Error state snapshot ───────────────────────────────────
#[derive(Debug, Clone, Default)]
pub struct ErrorState {
    pub tec:          u8,
    pub rec:          u8,
    pub bus_off:      bool,
    pub error_frames: u64,
}

// ── Test result ────────────────────────────────────────────
#[derive(Debug)]
pub enum TestOutcome {
    Pass,
    FailNoRecovery,
    FailBusOff { recovery_ms: Option<f64> },
    FailValidation(String),
    FailTimeout,
}

#[derive(Debug)]
pub struct TestReport {
    pub scenario_name:    String,
    pub outcome:          TestOutcome,
    pub frames_injected:  u64,
    pub final_error_state: ErrorState,
    pub recovery_time:    Option<Duration>,
    pub elapsed:          Duration,
}

impl TestReport {
    pub fn passed(&self) -> bool {
        matches!(self.outcome, TestOutcome::Pass)
    }
}

// ── Abstract CAN driver trait ──────────────────────────────
/// Implement this for SocketCAN, PCAN, Kvaser, or sim.
#[async_trait::async_trait]
pub trait CanDriver: Send + Sync {
    async fn send_frame(
        &self,
        id:  u32,
        ext: bool,
        data: &[u8],
    ) -> std::io::Result<()>;

    async fn error_state(&self) -> ErrorState;
}

// ── Fault injector ─────────────────────────────────────────
pub struct FaultInjector<D: CanDriver> {
    driver:  std::sync::Arc<D>,
}

impl<D: CanDriver + 'static> FaultInjector<D> {
    pub fn new(driver: D) -> Self {
        Self { driver: std::sync::Arc::new(driver) }
    }

    pub async fn run(
        &self,
        name: &str,
        fault: &FaultDescriptor,
        validate: Option<&dyn Fn(&TestReport) -> Result<(), String>>,
    ) -> TestReport {
        let t_start = Instant::now();

        let outcome = self.execute(fault).await;

        let final_state = self.driver.error_state().await;
        let elapsed     = t_start.elapsed();

        let mut report = TestReport {
            scenario_name:    name.to_owned(),
            outcome:          outcome.unwrap_or(TestOutcome::Pass),
            frames_injected:  0,  // filled in execute()
            final_error_state: final_state,
            recovery_time:    None,
            elapsed,
        };

        // Apply optional validation
        if matches!(report.outcome, TestOutcome::Pass) {
            if let Some(v) = validate {
                if let Err(msg) = v(&report) {
                    report.outcome = TestOutcome::FailValidation(msg);
                }
            }
        }

        report
    }

    async fn execute(
        &self,
        fault: &FaultDescriptor,
    ) -> Option<TestOutcome> {
        // ── Babbling node ───────────────────────────────────
        if fault.flags.contains(FaultFlags::BABBLING_NODE) {
            return self.run_babbling(fault).await;
        }

        // ── Frame-level injections ──────────────────────────
        let deadline = fault.duration.map(|d| Instant::now() + d);
        let mut counter: u8 = 0;
        let mut injected: u64 = 0;

        loop {
            let mut payload = [counter, 0x02, 0x03, 0x04,
                               0x05, 0x06, 0x07, 0x08u8];

            // Bit flip
            if fault.flags.contains(FaultFlags::BIT_FLIP) {
                for (b, m) in payload.iter_mut()
                                     .zip(fault.bit_mask.iter()) {
                    *b ^= m;
                }
            }

            // Counter skip
            if fault.flags.contains(FaultFlags::COUNTER_SKIP) {
                payload[0] = payload[0].wrapping_add(fault.counter_skip);
            }

            let tx_id = if fault.flags.contains(FaultFlags::WRONG_ID) {
                fault.injected_id.unwrap_or(fault.target_id)
            } else {
                fault.target_id
            };

            if !fault.flags.contains(FaultFlags::MISSING_FRAME) {
                if let Some(delay) = fault.delay {
                    tokio::time::sleep(delay).await;
                }
                let _ = self.driver
                    .send_frame(tx_id, fault.extended, &payload)
                    .await;
                injected += 1;

                // Overload burst
                if fault.flags.contains(FaultFlags::OVERLOAD_BURST) {
                    for _ in 0..fault.burst_count {
                        let _ = self.driver
                            .send_frame(tx_id, fault.extended, &payload)
                            .await;
                        injected += 1;
                    }
                }
            }

            counter = counter.wrapping_add(1);

            // Check deadline
            match deadline {
                None => break,                     // single shot
                Some(dl) if Instant::now() >= dl => break,
                _ => {}
            }

            tokio::time::sleep(fault.frame_interval).await;
        }

        let state = self.driver.error_state().await;
        if state.bus_off {
            // Attempt to observe recovery
            let t_rec = Instant::now();
            let timeout = Duration::from_secs(2);
            loop {
                tokio::time::sleep(Duration::from_millis(5)).await;
                let s = self.driver.error_state().await;
                if !s.bus_off {
                    return None; // Recovered → Pass
                }
                if t_rec.elapsed() >= timeout {
                    return Some(TestOutcome::FailNoRecovery);
                }
            }
        }

        None // Pass
    }

    async fn run_babbling(
        &self,
        fault: &FaultDescriptor,
    ) -> Option<TestOutcome> {
        let duration = fault.duration.unwrap_or(Duration::from_secs(1));
        let driver   = self.driver.clone();
        let id       = fault.target_id;
        let extended = fault.extended;
        let interval = fault.frame_interval;

        let (stop_tx, mut stop_rx) = tokio::sync::watch::channel(false);

        // Babble in a separate task
        let babble = tokio::spawn(async move {
            let payload = [0xBBu8; 8];
            let mut count: u64 = 0;
            while !*stop_rx.borrow() {
                let _ = driver.send_frame(id, extended, &payload).await;
                count += 1;
                tokio::time::sleep(interval).await;
            }
            count
        });

        // Monitor DUT concurrently
        let monitor = {
            let driver  = self.driver.clone();
            let timeout = duration;
            tokio::spawn(async move {
                let deadline = Instant::now() + timeout;
                let mut bus_off_seen = false;
                while Instant::now() < deadline {
                    let s = driver.error_state().await;
                    if s.bus_off { bus_off_seen = true; }
                    tokio::time::sleep(Duration::from_millis(10)).await;
                }
                bus_off_seen
            })
        };

        let bus_off_during = monitor.await.unwrap_or(false);
        let _ = stop_tx.send(true);
        let _ = babble.await;

        if bus_off_during {
            // Give DUT time to recover
            let t_rec = Instant::now();
            let recovery_timeout = Duration::from_secs(2);
            loop {
                tokio::time::sleep(Duration::from_millis(5)).await;
                let s = self.driver.error_state().await;
                if !s.bus_off {
                    let rec_ms = t_rec.elapsed().as_secs_f64() * 1000.0;
                    return Some(TestOutcome::FailBusOff {
                        recovery_ms: Some(rec_ms)
                    });
                    // Note: caller may upgrade to Pass if recovery is expected
                }
                if t_rec.elapsed() >= recovery_timeout {
                    return Some(TestOutcome::FailNoRecovery);
                }
            }
        }

        None // No Bus-Off seen → Pass
    }
}

// ── Test suite orchestrator ────────────────────────────────
pub struct FitSuite<D: CanDriver + 'static> {
    injector:  FaultInjector<D>,
    scenarios: Vec<(
        String,
        FaultDescriptor,
        Option<Box<dyn Fn(&TestReport) -> Result<(), String> + Send + Sync>>,
    )>,
}

impl<D: CanDriver + 'static> FitSuite<D> {
    pub fn new(driver: D) -> Self {
        Self {
            injector:  FaultInjector::new(driver),
            scenarios: Vec::new(),
        }
    }

    pub fn add_scenario(
        &mut self,
        name: impl Into<String>,
        fault: FaultDescriptor,
        validate: Option<Box<dyn Fn(&TestReport) -> Result<(), String>
                               + Send + Sync>>,
    ) {
        self.scenarios.push((name.into(), fault, validate));
    }

    pub async fn run_all(&self) -> Vec<TestReport> {
        let mut reports = Vec::with_capacity(self.scenarios.len());
        for (name, fault, validate) in &self.scenarios {
            let v_ref = validate.as_deref();
            let report = self.injector
                .run(name, fault, v_ref)
                .await;

            let status = if report.passed() { "PASS" } else { "FAIL" };
            println!("[{status}]  {name}  ({:.1} ms)",
                     report.elapsed.as_secs_f64() * 1000.0);

            reports.push(report);
        }
        reports
    }

    pub fn print_summary(reports: &[TestReport]) {
        let passed = reports.iter().filter(|r| r.passed()).count();
        let failed = reports.len() - passed;
        println!("\n── Summary ───────────────────────────────────────");
        println!("  Total : {}", reports.len());
        println!("  Passed: {passed}");
        println!("  Failed: {failed}");
        println!("──────────────────────────────────────────────────");
    }
}

// ── Usage example ──────────────────────────────────────────
#[tokio::main]
async fn main() {
    // In production, substitute SimDriver with your SocketCanDriver
    // or KvaserDriver implementation.
    let driver = SimDriver::default();
    let mut suite = FitSuite::new(driver);

    // Scenario 1: bit-flip on byte 0 of ID 0x123
    suite.add_scenario(
        "Bit-flip byte 0, ID 0x123",
        FaultBuilder::new(0x123)
            .bit_flip([0x01, 0, 0, 0, 0, 0, 0, 0])
            .interval(Duration::from_millis(10))
            .duration(Duration::from_millis(500))
            .build(),
        Some(Box::new(|r| {
            if r.final_error_state.bus_off {
                Err("DUT entered Bus-Off on payload fault".to_owned())
            } else {
                Ok(())
            }
        })),
    );

    // Scenario 2: babbling node, 2 seconds
    suite.add_scenario(
        "Babbling node ID 0x001, 2 s",
        FaultBuilder::new(0x001)
            .babbling_node()
            .interval(Duration::from_micros(100))
            .duration(Duration::from_secs(2))
            .build(),
        Some(Box::new(|r| {
            match &r.outcome {
                TestOutcome::FailBusOff { recovery_ms: Some(ms) }
                    if *ms < 1000.0 => Ok(()),
                TestOutcome::Pass => Ok(()),
                _ => Err(format!("Unexpected outcome: {:?}", r.outcome)),
            }
        })),
    );

    // Scenario 3: counter skip by 3 on ID 0x200
    suite.add_scenario(
        "Counter skip +3, ID 0x200",
        FaultBuilder::new(0x200)
            .counter_skip(3)
            .interval(Duration::from_millis(10))
            .duration(Duration::from_secs(1))
            .build(),
        None,
    );

    // Scenario 4: missing heartbeat for 500 ms
    suite.add_scenario(
        "Missing heartbeat ID 0x300, 500 ms",
        FaultBuilder::new(0x300)
            .missing_frame()
            .interval(Duration::from_millis(10))
            .duration(Duration::from_millis(500))
            .build(),
        None,
    );

    let reports = suite.run_all().await;
    FitSuite::<SimDriver>::print_summary(&reports);
}

// ── Simulation driver (for unit-testing without hardware) ──
#[derive(Default)]
struct SimDriver {
    frame_count: std::sync::atomic::AtomicU64,
}

#[async_trait::async_trait]
impl CanDriver for SimDriver {
    async fn send_frame(&self, _id: u32, _ext: bool, _data: &[u8])
        -> std::io::Result<()>
    {
        self.frame_count
            .fetch_add(1, std::sync::atomic::Ordering::Relaxed);
        Ok(())
    }

    async fn error_state(&self) -> ErrorState {
        // Simulated: never enters Bus-Off
        ErrorState { tec: 0, rec: 0, bus_off: false, error_frames: 0 }
    }
}
```

---

## TEC/REC State Machine — How the Controller Responds

Understanding when faults escalate to Bus-Off is critical for interpreting test results:---


## Key Implementation Details

### Thread/Task Safety

The C implementation uses a `pthread` to run the babbling node flood independently from the monitoring loop. The Rust implementation replaces this with a `tokio::spawn` task pair — the babble task and the monitor task run concurrently, and a `watch::channel` provides cooperative cancellation. This eliminates the raw mutex and stop-flag polling that C requires.

### HAL Abstraction

Both implementations isolate hardware access behind an interface (`can_hal_t` struct of function pointers in C; the `CanDriver` async trait in Rust). This means the same test code runs against:

- Linux `vcan0` (virtual CAN — unit tests in CI)
- Real `can0/can1` hardware (integration tests on bench)
- PCAN, Kvaser, or Vector CANoe/CANalyzer (HIL rigs)
- A simulation backend that returns scripted error state sequences

### Measuring Recovery Time

Bus-Off recovery time is safety-critical. ISO 11898-1 mandates that after Bus-Off, a node waits for 128 occurrences of 11 consecutive recessive bits (~1.28 ms at 1 Mbit/s) before re-entering Error Active. If the application software also applies a software delay (which is common to avoid rapid Bus-Off cycling), the total recovery window may be several hundred milliseconds to seconds. FIT must measure this and verify it falls within the system's fault-tolerant time interval (FTTI as defined in ISO 26262).

### Babbling Node Containment

A compliant node automatically enters Bus-Off when TEC reaches 256 (after approximately 32 unacknowledged frames). The test validates two properties:

1. **Self-containment**: Does the babbling node itself enter Bus-Off (correct — it should)?
2. **Peer resilience**: Do legitimate nodes on the bus continue operating normally once the babbler is silenced?

Both require simultaneous monitoring of the injector's error counters and the DUT's application-layer behaviour.

---

## Summary

| Aspect | Detail |
|---|---|
| **Purpose** | Validate CAN fault tolerance before field deployment |
| **Standards** | ISO 11898-1 (protocol), ISO 16845 (conformance), ISO 26262 (functional safety) |
| **Physical faults** | Short circuits, open circuits, missing termination — require hardware relay or FIT box |
| **Protocol faults** | Bit-stuffing violations, CRC errors, form errors — require bit-accurate injection (FPGA or capable CAN controller) |
| **Babbling node** | Highest-priority ID floods the bus; DUT must survive via Bus-Off self-isolation |
| **Recovery test** | TEC ≥ 256 → Bus-Off → 128 × 11 recessive bits → Error Active; measure total time |
| **Semantic faults** | Bit-flips, counter skips, range violations — require application-level validation callbacks |
| **C implementation** | `pthread`-based injector + HAL struct of function pointers; `fit_run()` + C++ RAII suite |
| **Rust implementation** | `async/await` with `tokio`, `bitflags!` for type-safe fault masks, `CanDriver` trait for HAL abstraction |
| **Key metric** | Recovery time < system FTTI (Fault-Tolerant Time Interval); no silent data corruption |
| **Infrastructure** | `vcan0` (virtual) for CI, real or HIL hardware for integration; HTML report for traceability |

Fault injection is not just a test technique — in ISO 26262 ASIL-C/D systems it is a **mandatory** verification activity. The framework above provides the scaffolding; the hardest part is writing the validation callbacks that understand what the application is *supposed* to do when things go wrong.