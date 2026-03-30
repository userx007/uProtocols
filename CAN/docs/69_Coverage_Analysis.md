# 69. Coverage Analysis — CAN Driver & Protocol Stack Validation

Coverage analysis for CAN systems measures how thoroughly your test suite exercises the driver logic, protocol state machines, and message handling paths. Unlike simple unit testing, coverage analysis reveals *what you haven't tested* — silent gaps in error handling, uncovered baud-rate branches, unexercised DLC edge cases, and protocol state transitions that only fire under fault conditions. In safety-critical automotive systems (ISO 26262), achieving defined coverage targets is a certification requirement, not an optional quality metric.

There are two orthogonal dimensions of coverage to track:

**Code coverage** measures structural completeness — which lines, branches, and conditions in your C/C++/Rust source were actually executed during testing. Tools like `gcov`/`lcov` (C/C++) and `llvm-cov`/`cargo-llvm-cov` (Rust) instrument the binary and produce coverage maps.

**Scenario coverage** measures behavioural completeness — which meaningful combinations of inputs, states, and fault conditions your tests have exercised. This cannot be inferred from code coverage alone; a single test can hit 100% line coverage while never testing an error-recovery path.

Here is how the two dimensions layer together across a CAN stack:

![can_coverage_analysis_architecture](images/can_coverage_analysis_architecture.svg)<br>

---

## Code Coverage — C/C++ with `gcov` / `lcov`

The most common toolchain for embedded C/C++ coverage uses GCC's built-in instrumentation. Compile with `-fprofile-arcs -ftest-coverage`, run your tests, then collect `.gcda` data files and feed them to `lcov`.

### CAN driver under test

```c
/* can_driver.c — minimal CAN controller driver */
#include "can_driver.h"
#include <stdint.h>
#include <stdbool.h>

#define CAN_MAX_DLC       8U
#define EC_PASSIVE_LIMIT  128U
#define EC_BUSOFF_LIMIT   256U

typedef enum {
    CAN_STATE_INIT       = 0,
    CAN_STATE_ACTIVE,
    CAN_STATE_ERROR_PASSIVE,
    CAN_STATE_BUS_OFF,
} CanState;

static CanState  g_state        = CAN_STATE_INIT;
static uint16_t  g_tx_err_cnt   = 0U;
static uint16_t  g_rx_err_cnt   = 0U;
static CanRxFifo g_rx_fifo;

/* ── error-counter update ────────────────────────────────────────────── */
CanStatus can_update_error_counters(uint16_t tec, uint16_t rec)
{
    g_tx_err_cnt = tec;
    g_rx_err_cnt = rec;

    if (tec >= EC_BUSOFF_LIMIT || rec >= EC_BUSOFF_LIMIT) {   /* Branch A */
        g_state = CAN_STATE_BUS_OFF;
        can_hal_disable();
        return CAN_ERR_BUSOFF;
    }
    if (tec >= EC_PASSIVE_LIMIT || rec >= EC_PASSIVE_LIMIT) { /* Branch B */
        g_state = CAN_STATE_ERROR_PASSIVE;
        return CAN_ERR_PASSIVE;
    }
    g_state = CAN_STATE_ACTIVE;
    return CAN_OK;
}

/* ── receive handler (called from ISR) ───────────────────────────────── */
CanStatus can_rx_handler(const CanFrame *frame)
{
    if (frame == NULL) {                     /* null-guard — Branch C */
        return CAN_ERR_NULL;
    }
    if (frame->dlc > CAN_MAX_DLC) {         /* DLC validation — Branch D */
        return CAN_ERR_INVALID_DLC;
    }
    if (can_fifo_full(&g_rx_fifo)) {        /* overflow guard — Branch E */
        return CAN_ERR_OVERFLOW;
    }
    can_fifo_push(&g_rx_fifo, frame);
    return CAN_OK;
}

/* ── ID filter ───────────────────────────────────────────────────────── */
bool can_id_passes_filter(uint32_t id, uint32_t mask, uint32_t filter)
{
    /* MC/DC target: both (id & mask) and (== filter) must be independently
       shown to independently affect the outcome */
    return (id & mask) == filter;           /* Condition E1 AND E2 */
}
```

### Test harness with coverage tracking

```c
/* test_can_coverage.c — compile with:
   gcc -fprofile-arcs -ftest-coverage -O0 -g \
       can_driver.c test_can_coverage.c can_hal_stub.c -o test_can
   Then: ./test_can && gcov can_driver.c
         lcov -c -d . -o cov.info && genhtml cov.info -o html_report */

#include "can_driver.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ── helpers ─────────────────────────────────────────────────────────── */
static uint32_t g_pass = 0, g_fail = 0;

#define ASSERT_EQ(a, b, msg) \
    do { if ((a) == (b)) { g_pass++; } else { \
        fprintf(stderr, "FAIL [%s]: expected %d got %d\n", msg, (int)(b), (int)(a)); \
        g_fail++; } } while (0)

/* ── Branch A: bus-off via TEC ───────────────────────────────────────── */
static void test_busoff_via_tec(void)
{
    CanStatus s = can_update_error_counters(256, 0);
    ASSERT_EQ(s, CAN_ERR_BUSOFF, "busoff_via_tec");
}

/* ── Branch A: bus-off via REC ───────────────────────────────────────── */
static void test_busoff_via_rec(void)
{
    CanStatus s = can_update_error_counters(0, 256);
    ASSERT_EQ(s, CAN_ERR_BUSOFF, "busoff_via_rec");
}

/* ── Branch B: error-passive ─────────────────────────────────────────── */
static void test_error_passive(void)
{
    CanStatus s = can_update_error_counters(128, 0);
    ASSERT_EQ(s, CAN_ERR_PASSIVE, "error_passive_tec");

    s = can_update_error_counters(0, 128);
    ASSERT_EQ(s, CAN_ERR_PASSIVE, "error_passive_rec");
}

/* ── Branch D: DLC = 0 (boundary) ───────────────────────────────────── */
static void test_dlc_zero(void)
{
    CanFrame f = {.id = 0x123, .dlc = 0, .data = {0}};
    CanStatus s = can_rx_handler(&f);
    ASSERT_EQ(s, CAN_OK, "dlc_zero");
}

/* ── Branch D: DLC = 8 (max valid) ──────────────────────────────────── */
static void test_dlc_max_valid(void)
{
    CanFrame f = {.id = 0x123, .dlc = 8, .data = {0}};
    CanStatus s = can_rx_handler(&f);
    ASSERT_EQ(s, CAN_OK, "dlc_max_valid");
}

/* ── Branch D: DLC = 9 (invalid) ────────────────────────────────────── */
static void test_dlc_invalid(void)
{
    CanFrame f = {.id = 0x123, .dlc = 9, .data = {0}};
    CanStatus s = can_rx_handler(&f);
    ASSERT_EQ(s, CAN_ERR_INVALID_DLC, "dlc_invalid");
}

/* ── Branch C: null frame ────────────────────────────────────────────── */
static void test_null_frame(void)
{
    CanStatus s = can_rx_handler(NULL);
    ASSERT_EQ(s, CAN_ERR_NULL, "null_frame");
}

/* ── MC/DC for can_id_passes_filter ─────────────────────────────────── */
/*  To satisfy MC/DC we need each condition to independently change output:
    Condition E1: (id & mask) evaluated alone
    Condition E2: == filter evaluated alone               */
static void test_id_filter_mcdc(void)
{
    uint32_t mask   = 0x7FF;
    uint32_t filter = 0x200;

    /* E1 true, E2 true  → result true  */
    ASSERT_EQ(can_id_passes_filter(0x200, mask, filter), true,  "mcdc_tt");
    /* E1 true, E2 false → result false (E2 independently controls result) */
    ASSERT_EQ(can_id_passes_filter(0x201, mask, filter), false, "mcdc_tf");
    /* E1 false (masked != filter) → result false regardless of E2 */
    ASSERT_EQ(can_id_passes_filter(0x100, mask, filter), false, "mcdc_ff");
    /* boundary: ID 0x000 */
    ASSERT_EQ(can_id_passes_filter(0x000, mask, 0x000),  true,  "mcdc_id0");
    /* boundary: ID 0x7FF */
    ASSERT_EQ(can_id_passes_filter(0x7FF, mask, 0x7FF),  true,  "mcdc_id_max");
}

/* ── coverage report summary ─────────────────────────────────────────── */
static void print_scenario_matrix(void)
{
    /* Scenarios × pass/fail — extend with actual gcov data in CI */
    typedef struct { const char *name; const char *covered; } Row;
    Row rows[] = {
        {"Bus-off (TEC)",        "COVERED"},
        {"Bus-off (REC)",        "COVERED"},
        {"Error-passive (TEC)",  "COVERED"},
        {"Error-passive (REC)",  "COVERED"},
        {"DLC = 0",              "COVERED"},
        {"DLC = 8 (max)",        "COVERED"},
        {"DLC = 9 (invalid)",    "COVERED"},
        {"NULL frame",           "COVERED"},
        {"Filter MC/DC (tt)",    "COVERED"},
        {"Filter MC/DC (tf)",    "COVERED"},
        {"FIFO overflow",        "NOT COVERED"},  /* ← gap revealed */
        {"Bus-off recovery",     "NOT COVERED"},  /* ← gap revealed */
    };
    printf("\n%-30s %s\n", "Scenario", "Status");
    printf("%.48s\n", "------------------------------------------------");
    for (size_t i = 0; i < sizeof(rows)/sizeof(rows[0]); i++) {
        printf("%-30s %s\n", rows[i].name, rows[i].covered);
    }
}

int main(void)
{
    can_driver_init();

    test_busoff_via_tec();
    test_busoff_via_rec();
    test_error_passive();
    test_dlc_zero();
    test_dlc_max_valid();
    test_dlc_invalid();
    test_null_frame();
    test_id_filter_mcdc();

    print_scenario_matrix();
    printf("\nPassed: %u  Failed: %u\n", g_pass, g_fail);
    return (g_fail > 0) ? 1 : 0;
}
```

### Makefile — build, run, report

```makefile
# Requires: gcc, lcov, genhtml
CC      = gcc
CFLAGS  = -Wall -Wextra -O0 -g \
           -fprofile-arcs -ftest-coverage \
           --coverage

SRCS    = can_driver.c can_hal_stub.c test_can_coverage.c
TARGET  = test_can

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $^ -o $@

run: $(TARGET)
	./$(TARGET)

coverage: run
	gcov can_driver.c
	lcov --capture --directory . --output-file cov.info \
	     --rc lcov_branch_coverage=1
	genhtml cov.info --output-directory html_report \
	        --branch-coverage --legend
	@echo "Open html_report/index.html"

clean:
	rm -f $(TARGET) *.gcda *.gcno *.gcov cov.info
	rm -rf html_report
```

---

## Code Coverage — Rust with `cargo-llvm-cov`

Rust's coverage story uses LLVM's source-based instrumentation, which is considerably more accurate than gcov because it instruments at the MIR level before any optimisation.

```rust
// src/can_driver.rs — CAN driver with full coverage instrumentation support
// Build: cargo llvm-cov --html   (generates target/llvm-cov/html/index.html)

use std::collections::VecDeque;

// ── Error types ───────────────────────────────────────────────────────────
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanError {
    NullFrame,
    InvalidDlc,
    Overflow,
    BusOff,
    Passive,
}

// ── Frame ─────────────────────────────────────────────────────────────────
#[derive(Debug, Clone, Copy)]
pub struct CanFrame {
    pub id:  u32,
    pub dlc: u8,
    pub data: [u8; 8],
}

impl CanFrame {
    pub const MAX_DLC: u8 = 8;

    pub fn new(id: u32, data: &[u8]) -> Result<Self, CanError> {
        if data.len() > Self::MAX_DLC as usize {   // Branch: dlc > 8
            return Err(CanError::InvalidDlc);
        }
        let mut buf = [0u8; 8];
        buf[..data.len()].copy_from_slice(data);
        Ok(Self { id, dlc: data.len() as u8, data: buf })
    }
}

// ── State machine ─────────────────────────────────────────────────────────
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CanState {
    Init,
    Active,
    ErrorPassive,
    BusOff,
}

// ── Driver ───────────────────────────────────────────────────────────────
pub struct CanDriver {
    state:       CanState,
    tx_err_cnt:  u16,
    rx_err_cnt:  u16,
    rx_fifo:     VecDeque<CanFrame>,
    fifo_cap:    usize,
    pub hal_disabled: bool,   // observable for tests
}

const PASSIVE_THRESHOLD: u16 = 128;
const BUSOFF_THRESHOLD:  u16 = 256;

impl CanDriver {
    pub fn new(fifo_capacity: usize) -> Self {
        Self {
            state:        CanState::Init,
            tx_err_cnt:   0,
            rx_err_cnt:   0,
            rx_fifo:      VecDeque::with_capacity(fifo_capacity),
            fifo_cap:     fifo_capacity,
            hal_disabled: false,
        }
    }

    // ── error counter update ──────────────────────────────────────────────
    pub fn update_error_counters(&mut self, tec: u16, rec: u16)
        -> Result<(), CanError>
    {
        self.tx_err_cnt = tec;
        self.rx_err_cnt = rec;

        // MC/DC conditions: two independent predicates
        let tec_busoff = tec >= BUSOFF_THRESHOLD;   // C1
        let rec_busoff = rec >= BUSOFF_THRESHOLD;   // C2
        if tec_busoff || rec_busoff {               // Branch A
            self.state = CanState::BusOff;
            self.hal_disabled = true;
            return Err(CanError::BusOff);
        }

        let tec_passive = tec >= PASSIVE_THRESHOLD; // C3
        let rec_passive = rec >= PASSIVE_THRESHOLD; // C4
        if tec_passive || rec_passive {             // Branch B
            self.state = CanState::ErrorPassive;
            return Err(CanError::Passive);
        }

        self.state = CanState::Active;
        Ok(())
    }

    // ── receive handler ───────────────────────────────────────────────────
    pub fn receive(&mut self, frame: CanFrame) -> Result<(), CanError> {
        if frame.dlc > CanFrame::MAX_DLC {         // Branch C
            return Err(CanError::InvalidDlc);
        }
        if self.rx_fifo.len() >= self.fifo_cap {   // Branch D
            return Err(CanError::Overflow);
        }
        self.rx_fifo.push_back(frame);
        Ok(())
    }

    // ── ID filter — MC/DC target ─────────────────────────────────────────
    pub fn id_passes_filter(id: u32, mask: u32, filter: u32) -> bool {
        (id & mask) == filter   // two conditions for MC/DC
    }

    // ── accessors ─────────────────────────────────────────────────────────
    pub fn state(&self)    -> CanState { self.state }
    pub fn fifo_len(&self) -> usize    { self.rx_fifo.len() }
    pub fn pop_frame(&mut self) -> Option<CanFrame> { self.rx_fifo.pop_front() }
}
```

```rust
// src/lib.rs (or tests/coverage_tests.rs) — scenario + code coverage tests
// cargo llvm-cov --html --include-ffi

#[cfg(test)]
mod coverage_tests {
    use super::*;

    // ── helpers ───────────────────────────────────────────────────────────
    fn make_frame(id: u32, dlc: u8) -> CanFrame {
        let data = vec![0u8; dlc as usize];
        CanFrame::new(id, &data).unwrap_or(CanFrame { id, dlc, data: [0; 8] })
    }

    // ─────────────────────────────────────────────────────────────────────
    // BRANCH A — bus-off
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn test_busoff_via_tec() {
        let mut d = CanDriver::new(16);
        let r = d.update_error_counters(256, 0);
        assert_eq!(r, Err(CanError::BusOff));
        assert_eq!(d.state(), CanState::BusOff);
        assert!(d.hal_disabled);
    }

    #[test]
    fn test_busoff_via_rec() {
        let mut d = CanDriver::new(16);
        let r = d.update_error_counters(0, 256);
        assert_eq!(r, Err(CanError::BusOff));
        assert!(d.hal_disabled);
    }

    // MC/DC for Branch A: show C1 and C2 each independently control the outcome
    #[test]
    fn test_busoff_mcdc_c1_independent() {
        let mut d = CanDriver::new(16);
        // C1=true, C2=false → BusOff  (C1 alone drives result)
        assert_eq!(d.update_error_counters(256, 0),   Err(CanError::BusOff));
        let mut d = CanDriver::new(16);
        // C1=false, C2=false → NOT BusOff
        assert!(d.update_error_counters(127, 0).is_ok()
             || d.update_error_counters(127, 0) == Err(CanError::Passive));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BRANCH B — error-passive
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn test_error_passive_tec() {
        let mut d = CanDriver::new(16);
        assert_eq!(d.update_error_counters(128, 0), Err(CanError::Passive));
        assert_eq!(d.state(), CanState::ErrorPassive);
    }

    #[test]
    fn test_error_passive_rec() {
        let mut d = CanDriver::new(16);
        assert_eq!(d.update_error_counters(0, 128), Err(CanError::Passive));
        assert_eq!(d.state(), CanState::ErrorPassive);
    }

    #[test]
    fn test_active_below_thresholds() {
        let mut d = CanDriver::new(16);
        assert_eq!(d.update_error_counters(0, 0), Ok(()));
        assert_eq!(d.state(), CanState::Active);
    }

    // ─────────────────────────────────────────────────────────────────────
    // BRANCH C+D — receive
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn test_dlc_boundary_zero() {
        let mut d = CanDriver::new(16);
        let f = CanFrame { id: 0x100, dlc: 0, data: [0; 8] };
        assert_eq!(d.receive(f), Ok(()));
        assert_eq!(d.fifo_len(), 1);
    }

    #[test]
    fn test_dlc_boundary_max() {
        let mut d = CanDriver::new(16);
        let f = CanFrame { id: 0x100, dlc: 8, data: [0xAB; 8] };
        assert_eq!(d.receive(f), Ok(()));
    }

    #[test]
    fn test_dlc_invalid_nine() {
        let mut d = CanDriver::new(16);
        let f = CanFrame { id: 0x100, dlc: 9, data: [0; 8] };
        assert_eq!(d.receive(f), Err(CanError::InvalidDlc));
    }

    // ─────────────────────────────────────────────────────────────────────
    // BRANCH D — FIFO overflow (scenario gap identified earlier)
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn test_fifo_overflow() {
        let cap = 4;
        let mut d = CanDriver::new(cap);
        for i in 0..cap {
            let f = CanFrame { id: i as u32, dlc: 0, data: [0; 8] };
            assert_eq!(d.receive(f), Ok(()));
        }
        // Next push must overflow
        let overflow_frame = CanFrame { id: 0xFF, dlc: 0, data: [0; 8] };
        assert_eq!(d.receive(overflow_frame), Err(CanError::Overflow));
    }

    // ─────────────────────────────────────────────────────────────────────
    // ID filter — MC/DC (five test vectors per DO-178C guidance)
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn test_id_filter_mcdc() {
        let mask   = 0x7FF_u32;
        let filter = 0x200_u32;

        // tt → true
        assert!(CanDriver::id_passes_filter(0x200, mask, filter));
        // tf: masked matches but filter differs → false  (E2 controls)
        assert!(!CanDriver::id_passes_filter(0x201, mask, filter));
        // ft: masked differs from filter → false (E1 controls)
        assert!(!CanDriver::id_passes_filter(0x100, mask, filter));
        // boundary: ID 0x000
        assert!(CanDriver::id_passes_filter(0x000, mask, 0x000));
        // boundary: ID 0x7FF
        assert!(CanDriver::id_passes_filter(0x7FF, mask, 0x7FF));
    }

    // ─────────────────────────────────────────────────────────────────────
    // Scenario coverage matrix (printed at runtime)
    // ─────────────────────────────────────────────────────────────────────
    #[test]
    fn print_scenario_coverage_report() {
        let scenarios = [
            ("Bus-off via TEC",       true),
            ("Bus-off via REC",       true),
            ("Error-passive TEC",     true),
            ("Error-passive REC",     true),
            ("Active (below thresh)", true),
            ("DLC = 0",               true),
            ("DLC = 8 (max valid)",   true),
            ("DLC = 9 (invalid)",     true),
            ("FIFO overflow",         true),   // now covered
            ("Filter MC/DC tt",       true),
            ("Filter MC/DC tf",       true),
            ("Filter MC/DC ft",       true),
            ("Bus-off recovery",      false),  // still a gap
        ];

        println!("\n{:<32} {}", "Scenario", "Covered");
        println!("{}", "-".repeat(44));
        for (name, covered) in &scenarios {
            println!("{:<32} {}", name, if *covered { "YES" } else { "NO  ← gap" });
        }
    }
}
```

### `Cargo.toml` — coverage tooling

```toml
[package]
name    = "can_coverage"
version = "0.1.0"
edition = "2021"

[dev-dependencies]
# cargo-llvm-cov provides source-based LLVM coverage
# Install: cargo install cargo-llvm-cov
# Run:     cargo llvm-cov --html
#          cargo llvm-cov --lcov --output-path lcov.info
#          cargo llvm-cov nextest  (with cargo-nextest for parallel runs)

[profile.test]
opt-level = 0      # disable optimisations so coverage is source-faithful
debug     = true
```

### Running coverage — shell commands

```bash
# ── C/C++ (gcov + lcov) ───────────────────────────────────────────────
make coverage
# → html_report/index.html   (branch coverage enforced via --rc)

# generate XML for CI (e.g. SonarQube, GitLab, Jenkins Cobertura)
lcov --summary cov.info
gcovr -r . --xml coverage.xml --branch

# ── Rust (cargo-llvm-cov) ─────────────────────────────────────────────
cargo install cargo-llvm-cov        # one-time

# HTML report (opens target/llvm-cov/html/index.html)
cargo llvm-cov --html --open

# LCOV output for CI integration
cargo llvm-cov --lcov --output-path lcov.info

# Coverage summary to stdout with threshold enforcement
cargo llvm-cov --text | tee cov_summary.txt
# Fail the build if line coverage < 90%
cargo llvm-cov --fail-under-lines 90

# With nextest (parallel, faster)
cargo llvm-cov nextest --html

# Branch coverage (Rust nightly required for full branch data)
RUSTFLAGS="-Z instrument-coverage" \
    cargo +nightly test
```

---

## Scenario Coverage — Tracking the Test Matrix

Code coverage tools can only tell you what code ran. Scenario coverage must be tracked explicitly. The standard approach uses a coverage database — a simple CSV or JSON that maps each meaningful scenario to a test ID and pass/fail status.

```rust
// tools/scenario_tracker.rs — standalone coverage tracking utility
// cargo run --bin scenario_tracker

use std::fmt;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CoverageStatus {
    Covered,
    NotCovered,
    PartiallyTested,
}

impl fmt::Display for CoverageStatus {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Covered         => write!(f, "COVERED"),
            Self::NotCovered      => write!(f, "NOT COVERED"),
            Self::PartiallyTested => write!(f, "PARTIAL"),
        }
    }
}

#[derive(Debug, Clone)]
pub struct ScenarioEntry {
    pub id:          &'static str,
    pub description: &'static str,
    pub layer:       &'static str,
    pub status:      CoverageStatus,
    pub test_ids:    &'static [&'static str],
    pub notes:       &'static str,
}

// ── Master scenario list for a CAN driver ────────────────────────────────
pub fn build_can_scenario_matrix() -> Vec<ScenarioEntry> {
    vec![
        ScenarioEntry {
            id:          "CAN-SC-001",
            description: "Normal TX, single standard frame",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_tx_standard_frame"],
            notes:       "",
        },
        ScenarioEntry {
            id:          "CAN-SC-002",
            description: "Normal RX, DLC 0..8 boundary sweep",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_dlc_boundary_zero", "test_dlc_boundary_max"],
            notes:       "Both extremes exercised",
        },
        ScenarioEntry {
            id:          "CAN-SC-003",
            description: "Invalid DLC (> 8)",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_dlc_invalid_nine"],
            notes:       "",
        },
        ScenarioEntry {
            id:          "CAN-SC-004",
            description: "RX FIFO overflow",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_fifo_overflow"],
            notes:       "Gap closed in v2 of test suite",
        },
        ScenarioEntry {
            id:          "CAN-SC-005",
            description: "Error-passive state (TEC ≥ 128)",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_error_passive_tec", "test_error_passive_rec"],
            notes:       "",
        },
        ScenarioEntry {
            id:          "CAN-SC-006",
            description: "Bus-off state (TEC ≥ 256)",
            layer:       "Driver",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_busoff_via_tec", "test_busoff_via_rec"],
            notes:       "",
        },
        ScenarioEntry {
            id:          "CAN-SC-007",
            description: "Bus-off recovery (128 × 11 recessive bits)",
            layer:       "Driver",
            status:      CoverageStatus::NotCovered,
            test_ids:    &[],
            notes:       "Requires HW loopback or CANalyzer stub",
        },
        ScenarioEntry {
            id:          "CAN-SC-008",
            description: "ID filter boundary — 0x000 and 0x7FF",
            layer:       "Network",
            status:      CoverageStatus::Covered,
            test_ids:    &["test_id_filter_mcdc"],
            notes:       "MC/DC satisfied with 3 test vectors",
        },
        ScenarioEntry {
            id:          "CAN-SC-009",
            description: "Baud rate: 125 kbps",
            layer:       "HAL",
            status:      CoverageStatus::PartiallyTested,
            test_ids:    &["test_baud_125k"],
            notes:       "Only loopback; no physical bus test",
        },
        ScenarioEntry {
            id:          "CAN-SC-010",
            description: "Baud rate: 500 kbps",
            layer:       "HAL",
            status:      CoverageStatus::PartiallyTested,
            test_ids:    &["test_baud_500k"],
            notes:       "Only loopback; no physical bus test",
        },
        ScenarioEntry {
            id:          "CAN-SC-011",
            description: "ISO 15765-2 multi-frame TX (FF + CF)",
            layer:       "Transport",
            status:      CoverageStatus::NotCovered,
            test_ids:    &[],
            notes:       "Transport layer not yet unit-tested",
        },
        ScenarioEntry {
            id:          "CAN-SC-012",
            description: "Flow Control frame (FC) with FS=0 (CTS)",
            layer:       "Transport",
            status:      CoverageStatus::NotCovered,
            test_ids:    &[],
            notes:       "Transport layer not yet unit-tested",
        },
    ]
}

// ── Report generator ──────────────────────────────────────────────────────
pub fn print_coverage_report(matrix: &[ScenarioEntry]) {
    let total   = matrix.len();
    let covered = matrix.iter().filter(|e| e.status == CoverageStatus::Covered).count();
    let partial = matrix.iter().filter(|e| e.status == CoverageStatus::PartiallyTested).count();
    let missing = matrix.iter().filter(|e| e.status == CoverageStatus::NotCovered).count();

    println!("\n╔══ CAN Scenario Coverage Report ══════════════════════════════╗");
    println!("║  Total: {}  Covered: {}  Partial: {}  Missing: {}             ║",
             total, covered, partial, missing);
    println!("║  Coverage: {:.1}% (excl. partial)                             ║",
             (covered as f64 / total as f64) * 100.0);
    println!("╚═══════════════════════════════════════════════════════════════╝");

    println!("\n{:<12} {:<42} {:<12} {}",
             "ID", "Description", "Layer", "Status");
    println!("{}", "─".repeat(84));

    for e in matrix {
        let status_str = format!("{}", e.status);
        let gap_marker = if e.status == CoverageStatus::NotCovered { " ◄ GAP" } else { "" };
        println!("{:<12} {:<42} {:<12} {}{}",
                 e.id, e.description, e.layer, status_str, gap_marker);
        if !e.notes.is_empty() {
            println!("{:>12}   ↳ {}", "", e.notes);
        }
    }

    println!("\nUncovered scenarios:");
    for e in matrix.iter().filter(|e| e.status == CoverageStatus::NotCovered) {
        println!("  • [{}] {} — {}", e.id, e.description, e.notes);
    }
}

fn main() {
    let matrix = build_can_scenario_matrix();
    print_coverage_report(&matrix);
}
```

---

## Summary

| Aspect | C/C++ | Rust |
|---|---|---|
| **Tool** | `gcov` / `lcov` / `gcovr` | `cargo-llvm-cov` |
| **Compile flag** | `-fprofile-arcs -ftest-coverage` | Automatic (LLVM native) |
| **Branch coverage** | `--rc lcov_branch_coverage=1` | `--html` includes branch data |
| **MC/DC** | Manual test-vector design, verified via `gcov -b` | Same manual design; `cargo llvm-cov` reports condition hits |
| **CI integration** | `gcovr --xml` → Cobertura / SonarQube | `--lcov` → any LCOV-compatible tool |
| **Threshold gate** | `gcovr --fail-under-branch 85` | `cargo llvm-cov --fail-under-lines 90` |

**Key takeaways:**

Code coverage and scenario coverage are complementary, not interchangeable. 100% line coverage with a single happy-path test tells you almost nothing about fault robustness. The right process is: (1) define your scenario matrix first (bus-off, overflow, baud variants, filter boundaries, transport-layer multi-frame paths), (2) write tests that target those scenarios, (3) run structural coverage tools to find any code that your scenarios still didn't reach, then (4) ask whether that uncovered code represents a missing scenario or dead code that should be deleted.

For ISO 26262 ASIL-B/C/D, MC/DC (modified condition/decision coverage) is the mandated structural level for safety-relevant functions — each boolean condition inside a compound predicate must be independently shown to control the outcome, which requires deliberate test-vector design beyond what most test suites produce automatically.