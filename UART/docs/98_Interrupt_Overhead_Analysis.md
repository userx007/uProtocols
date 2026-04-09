# 98. UART Interrupt Overhead Analysis

**Theory** — the five phases of interrupt overhead (latency → entry → body → exit → pipeline recovery), with a cycle-cost table for ARM Cortex-M, and all major sources of overhead (per-byte mode, FIFO mode, DMA, compiler-induced cost, NVIC priority effects).

**C/C++ examples (4):**
1. GPIO toggle method using STM32 HAL for oscilloscope measurement
2. DWT cycle counter with min/avg/max statistics accumulation
3. CPU load percentage calculator with atomic snapshot/reset
4. 16550-compatible FIFO drain loop with amortised overhead tracking

**Rust examples (4):**
1. RTIC-style ISR with `AtomicU32` statistics (no mutex needed)
2. CPU load monitor with window-based sampling and reset
3. FIFO drain loop with per-ISR and per-byte overhead tracking
4. `OverheadProfile` struct for comparing configurations (`no_std` compatible)

**Additional sections** cover benchmarking strategies (loopback test, baud rate sweep, worst-case analysis, ITM/SWO tracing) and optimization techniques (FIFO thresholds, DMA, minimal ISR bodies, register tuning, NVIC priority).

## Measuring CPU Time Spent in UART Interrupt Handlers

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is Interrupt Overhead?](#what-is-interrupt-overhead)
3. [Sources of UART Interrupt Overhead](#sources-of-uart-interrupt-overhead)
4. [Measurement Techniques](#measurement-techniques)
5. [C/C++ Implementation Examples](#cc-implementation-examples)
6. [Rust Implementation Examples](#rust-implementation-examples)
7. [Benchmarking and Profiling Strategies](#benchmarking-and-profiling-strategies)
8. [Optimization Techniques](#optimization-techniques)
9. [Summary](#summary)

---

## Introduction

In embedded and real-time systems, UART (Universal Asynchronous Receiver-Transmitter) communication is commonly driven by interrupt service routines (ISRs). While interrupts provide responsive, low-latency handling compared to polling, they are not "free" — each interrupt event consumes CPU cycles for context save/restore, handler execution, and pipeline recovery. Understanding and measuring this **interrupt overhead** is essential for systems where CPU budget is constrained or where deterministic timing must be guaranteed.

This document covers the theory behind UART interrupt overhead, how to measure it accurately, and how to minimize it, with concrete code examples in both **C/C++** and **Rust**.

---

## What Is Interrupt Overhead?

Interrupt overhead is the total CPU time consumed as a direct result of handling an interrupt that would not have been spent in normal foreground execution. It consists of several phases:

```
Interrupt Event
      │
      ▼
[1] Interrupt Latency      ← CPU finishes current instruction, saves context
      │
      ▼
[2] ISR Entry              ← Jump to vector, prologue (register save)
      │
      ▼
[3] ISR Body Execution     ← Actual handler logic (read UART, buffer write, flags)
      │
      ▼
[4] ISR Exit               ← Epilogue (register restore), return-from-interrupt
      │
      ▼
[5] Pipeline Recovery      ← CPU restores prefetch/pipeline state
      │
      ▼
Normal Execution Resumes
```

| Phase | Typical ARM Cortex-M Cost | Notes |
|---|---|---|
| Context save (HW) | 12 cycles | Stacks xPSR, PC, LR, R12, R0–R3 |
| Jump to vector | 2–4 cycles | Vector table fetch |
| ISR prologue (SW) | 0–16 cycles | Compiler-saved regs |
| ISR body | variable | Your code |
| ISR epilogue (SW) | 0–16 cycles | Compiler-restored regs |
| Context restore (HW) | 10 cycles | Unstacks saved frame |
| Pipeline refill | 3–5 cycles | Prefetch recovery |

At 115200 baud, one byte arrives approximately every 87 µs. On a 72 MHz Cortex-M3, each ISR invocation costs at minimum ~30 cycles (~0.4 µs), even for a trivial handler. At 1 Mbaud this becomes significant.

---

## Sources of UART Interrupt Overhead

### 1. Per-Byte Interrupt Mode (RX/TX Individual)

The most common but expensive mode: one interrupt per received or transmitted byte.

- **Maximum interrupt rate** = baud\_rate / 10 (for 8N1 framing)
- At 921600 baud → ~92,160 interrupts/second
- CPU overhead = 92,160 × (ISR cycles) / CPU\_frequency

### 2. FIFO-Based Interrupt Mode

Hardware FIFOs (available on many UARTs) trigger an interrupt only when N bytes accumulate. This reduces interrupt frequency by a factor of N at the cost of added latency.

### 3. DMA-Assisted UART

DMA transfers entire buffers without CPU involvement per byte. The CPU is only interrupted at buffer-full or half-full events, drastically cutting overhead for bulk data.

### 4. Compiler-Induced Overhead

ISR functions marked with `__attribute__((interrupt))` or equivalent may cause the compiler to save/restore more registers than necessary, increasing prologue/epilogue cost.

### 5. Nested Interrupts and Priority

Higher-priority interrupts preempting UART ISRs add tail-chaining overhead. NVIC tail-chaining on Cortex-M reduces but does not eliminate this cost.

---

## Measurement Techniques

### Technique 1: GPIO Toggle (Logic Analyzer / Oscilloscope)

Set a GPIO pin HIGH on ISR entry and LOW on ISR exit. Measure the pulse width with an oscilloscope or logic analyzer.

**Advantages:** Cycle-accurate, no code intrusion on timing paths  
**Disadvantages:** Requires free GPIO pin and external hardware

### Technique 2: Hardware Cycle Counter (DWT on ARM Cortex-M)

The ARM Cortex-M DWT (Data Watchpoint and Trace) unit provides a free-running 32-bit cycle counter. Sample it at ISR entry and exit.

**Advantages:** No external hardware, high precision  
**Disadvantages:** DWT must be enabled; 32-bit counter wraps at ~59 seconds on 72 MHz

### Technique 3: Timestamp Timer (TIM/SysTick)

Use a hardware timer running at full CPU speed to record entry/exit timestamps.

### Technique 4: Statistical Accumulation

Accumulate total cycles spent in the ISR over a fixed window, then compute average and maximum overhead.

---

## C/C++ Implementation Examples

### Example 1: GPIO Toggle Method (STM32 HAL)

```c
#include "stm32f4xx_hal.h"

/* Measurement GPIO: PA5 (LED pin or dedicated debug pin) */
#define MEAS_GPIO_PORT  GPIOA
#define MEAS_GPIO_PIN   GPIO_PIN_5

/* UART RX circular buffer */
#define RX_BUF_SIZE 256
static volatile uint8_t rx_buffer[RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/**
 * @brief UART1 interrupt handler with GPIO overhead measurement.
 *
 * Connect a logic analyzer or oscilloscope to PA5 to observe
 * the pulse width, which equals the ISR execution time.
 */
void USART1_IRQHandler(void)
{
    /* --- ISR ENTRY: set pin HIGH --- */
    MEAS_GPIO_PORT->BSRR = MEAS_GPIO_PIN;   /* atomic set, 1 cycle */

    uint32_t sr = USART1->SR;

    if (sr & USART_SR_RXNE)  /* Receive buffer not empty */
    {
        uint8_t data = (uint8_t)(USART1->DR & 0xFF);
        uint16_t next_head = (rx_head + 1) % RX_BUF_SIZE;

        if (next_head != rx_tail)  /* not full */
        {
            rx_buffer[rx_head] = data;
            rx_head = next_head;
        }
        /* else: buffer overflow — byte dropped */
    }

    if (sr & USART_SR_ORE)   /* Overrun error: clear by reading DR */
    {
        (void)USART1->DR;
    }

    /* --- ISR EXIT: set pin LOW --- */
    MEAS_GPIO_PORT->BSRR = (uint32_t)MEAS_GPIO_PIN << 16U;  /* atomic reset */
}
```

### Example 2: DWT Cycle Counter Method (ARM Cortex-M)

```c
#include <stdint.h>
#include <stdbool.h>

/* ── DWT cycle counter enable ─────────────────────────────────────── */
static inline void dwt_enable(void)
{
    /* Enable trace & debug block */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    /* Reset and enable the cycle counter */
    DWT->CYCCNT  = 0U;
    DWT->CTRL   |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t dwt_cycles(void)
{
    return DWT->CYCCNT;
}

/* ── Overhead statistics ──────────────────────────────────────────── */
typedef struct {
    uint32_t total_cycles;   /* sum of all ISR durations          */
    uint32_t call_count;     /* number of ISR invocations         */
    uint32_t max_cycles;     /* worst-case single ISR duration    */
    uint32_t min_cycles;     /* best-case single ISR duration     */
} isr_stats_t;

static volatile isr_stats_t uart_isr_stats = {
    .min_cycles = UINT32_MAX
};

/* ── UART RX buffer ───────────────────────────────────────────────── */
#define RX_BUF_SIZE 512
static volatile uint8_t  rx_buf[RX_BUF_SIZE];
static volatile uint32_t rx_head = 0;
static volatile uint32_t rx_tail = 0;

/* ── ISR with DWT measurement ─────────────────────────────────────── */
void USART1_IRQHandler(void)
{
    uint32_t t_start = dwt_cycles();    /* capture entry timestamp */

    uint32_t sr = USART1->SR;

    if (sr & USART_SR_RXNE)
    {
        uint8_t byte = (uint8_t)USART1->DR;
        uint32_t next = (rx_head + 1U) % RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            rx_head = next;
        }
    }

    if (sr & USART_SR_ORE) {
        (void)USART1->DR;   /* clear overrun */
    }

    /* ── Record duration ─────────────────────────────────── */
    uint32_t elapsed = dwt_cycles() - t_start;

    uart_isr_stats.total_cycles += elapsed;
    uart_isr_stats.call_count++;

    if (elapsed > uart_isr_stats.max_cycles)
        uart_isr_stats.max_cycles = elapsed;
    if (elapsed < uart_isr_stats.min_cycles)
        uart_isr_stats.min_cycles = elapsed;
}

/* ── Reporting helper ─────────────────────────────────────────────── */
#include <stdio.h>

void uart_isr_report(uint32_t cpu_hz)
{
    if (uart_isr_stats.call_count == 0) {
        printf("No ISR invocations recorded.\n");
        return;
    }

    uint32_t avg = uart_isr_stats.total_cycles / uart_isr_stats.call_count;

    printf("=== UART ISR Overhead Report ===\n");
    printf("  Invocations : %lu\n",   uart_isr_stats.call_count);
    printf("  Avg cycles  : %lu  (%lu ns)\n", avg,
           (uint32_t)((uint64_t)avg * 1000000000ULL / cpu_hz));
    printf("  Max cycles  : %lu  (%lu ns)\n", uart_isr_stats.max_cycles,
           (uint32_t)((uint64_t)uart_isr_stats.max_cycles * 1000000000ULL / cpu_hz));
    printf("  Min cycles  : %lu  (%lu ns)\n", uart_isr_stats.min_cycles,
           (uint32_t)((uint64_t)uart_isr_stats.min_cycles * 1000000000ULL / cpu_hz));
    printf("  Total cycles: %lu\n",   uart_isr_stats.total_cycles);
    printf("================================\n");
}
```

### Example 3: CPU Load Percentage Calculation

```c
#include <stdint.h>

/**
 * @brief Compute the percentage of CPU time consumed by UART ISRs
 *        over a measurement window.
 *
 * @param window_ms    Measurement window in milliseconds
 * @param cpu_hz       CPU clock frequency in Hz
 * @return             CPU load in hundredths of a percent (e.g. 125 = 1.25%)
 */
uint32_t uart_cpu_load_pct_x100(uint32_t window_ms, uint32_t cpu_hz)
{
    /* Snapshot and reset counters atomically */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    uint32_t cycles_in_isr = uart_isr_stats.total_cycles;
    uart_isr_stats.total_cycles = 0;
    uart_isr_stats.call_count   = 0;
    __set_PRIMASK(primask);

    /* Total cycles available in the window */
    uint64_t window_cycles = (uint64_t)cpu_hz * window_ms / 1000ULL;

    /* Return as integer hundredths of percent */
    return (uint32_t)((uint64_t)cycles_in_isr * 10000ULL / window_cycles);
}

/* Usage example in a 1 Hz monitor task */
void monitor_task(void)
{
    HAL_Delay(1000);   /* measure over 1 second */
    uint32_t load = uart_cpu_load_pct_x100(1000, 72000000);
    printf("UART ISR CPU load: %lu.%02lu%%\n", load / 100, load % 100);
}
```

### Example 4: FIFO Threshold Tuning (16550-Compatible UART)

```c
/* ── 16550 UART register definitions ─────────────────────────────── */
#define UART_BASE       0x40013800UL
#define UART_RBR        (*(volatile uint32_t *)(UART_BASE + 0x00))  /* Receive buffer  */
#define UART_FCR        (*(volatile uint32_t *)(UART_BASE + 0x08))  /* FIFO control    */
#define UART_IIR        (*(volatile uint32_t *)(UART_BASE + 0x08))  /* Interrupt ident */
#define UART_LSR        (*(volatile uint32_t *)(UART_BASE + 0x14))  /* Line status     */

/* FCR trigger levels */
#define FCR_FIFO_ENABLE     (1U << 0)
#define FCR_RX_RESET        (1U << 1)
#define FCR_TX_RESET        (1U << 2)
#define FCR_TRIG_1BYTE      (0U << 6)   /* interrupt every 1 byte  */
#define FCR_TRIG_4BYTES     (1U << 6)   /* interrupt every 4 bytes */
#define FCR_TRIG_8BYTES     (2U << 6)   /* interrupt every 8 bytes */
#define FCR_TRIG_14BYTES    (3U << 6)   /* interrupt every 14 bytes (max latency) */

/**
 * @brief Configure UART FIFO trigger level to reduce interrupt rate.
 *
 * Reducing interrupt frequency lowers overhead at the cost of higher
 * per-byte latency. At 115200 baud, 14-byte trigger ≈ 1.2 ms latency.
 */
void uart_fifo_configure(uint8_t trigger_level_bits)
{
    UART_FCR = FCR_FIFO_ENABLE |
               FCR_RX_RESET    |
               FCR_TX_RESET    |
               (trigger_level_bits & (3U << 6));
}

/* ISR drains the FIFO in a loop — one interrupt, multiple bytes */
void UART_IRQHandler(void)
{
    uint32_t t_start = dwt_cycles();

    while (UART_LSR & (1U << 0))  /* data ready */
    {
        uint8_t byte = (uint8_t)UART_RBR;
        uint32_t next = (rx_head + 1U) % RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            rx_head = next;
        }
    }

    uint32_t elapsed = dwt_cycles() - t_start;
    uart_isr_stats.total_cycles += elapsed;
    uart_isr_stats.call_count++;
    if (elapsed > uart_isr_stats.max_cycles)
        uart_isr_stats.max_cycles = elapsed;
}
```

---

## Rust Implementation Examples

### Example 1: DWT Cycle Counter with Atomic Statistics (RTIC Framework)

```rust
//! UART interrupt overhead analysis using RTIC on STM32F4.
//!
//! Dependencies (Cargo.toml):
//!   cortex-m       = "0.7"
//!   cortex-m-rt    = "0.7"
//!   cortex-m-rtic  = "1.1"
//!   stm32f4xx-hal  = { version = "0.20", features = ["stm32f411"] }
//!   heapless       = "0.8"

use core::sync::atomic::{AtomicU32, Ordering};
use cortex_m::peripheral::DWT;
use heapless::spsc::{Consumer, Producer, Queue};
use stm32f4xx_hal::{
    pac,
    serial::{config::Config, Serial},
    prelude::*,
};

// ── ISR statistics (atomic for lock-free access) ─────────────────────
static ISR_CALL_COUNT:   AtomicU32 = AtomicU32::new(0);
static ISR_TOTAL_CYCLES: AtomicU32 = AtomicU32::new(0);
static ISR_MAX_CYCLES:   AtomicU32 = AtomicU32::new(0);

/// Read the ARM DWT cycle counter.
#[inline(always)]
fn dwt_cycles() -> u32 {
    // Safety: DWT is always accessible on Cortex-M3/M4/M7 after TRCENA is set
    unsafe { (*DWT::PTR).cyccnt.read() }
}

/// Enable the DWT cycle counter.
pub fn dwt_enable(core: &mut cortex_m::Peripherals) {
    core.DCB.enable_trace();
    core.DWT.enable_cycle_counter();
}

// ── Circular ring buffer via heapless SPSC queue ──────────────────────
static mut RX_QUEUE: Queue<u8, 256> = Queue::new();

pub struct UartIsrResources {
    pub serial: pac::USART1,
    pub rx_producer: Producer<'static, u8, 256>,
}

// ── RTIC UART ISR ─────────────────────────────────────────────────────
/// This function would be bound to the USART1 interrupt in an RTIC app.
/// Shown here as a standalone function for clarity.
pub fn usart1_isr_handler(res: &mut UartIsrResources) {
    let t_start = dwt_cycles();

    // Read status register
    let sr = res.serial.sr.read();

    if sr.rxne().bit_is_set() {
        // Reading DR clears RXNE flag
        let byte = res.serial.dr.read().dr().bits() as u8;

        // Attempt to enqueue; silently drop on overflow
        let _ = res.rx_producer.enqueue(byte);
    }

    if sr.ore().bit_is_set() {
        // Clear overrun by reading DR
        let _ = res.serial.dr.read();
    }

    // ── Record overhead ───────────────────────────────────────────
    let elapsed = dwt_cycles().wrapping_sub(t_start);

    ISR_TOTAL_CYCLES.fetch_add(elapsed, Ordering::Relaxed);
    ISR_CALL_COUNT.fetch_add(1, Ordering::Relaxed);

    // Update max without a mutex (slightly racy but acceptable for stats)
    let prev_max = ISR_MAX_CYCLES.load(Ordering::Relaxed);
    if elapsed > prev_max {
        ISR_MAX_CYCLES.store(elapsed, Ordering::Relaxed);
    }
}

// ── Statistics snapshot and report ───────────────────────────────────
#[derive(Debug, Clone, Copy)]
pub struct IsrStats {
    pub call_count:   u32,
    pub total_cycles: u32,
    pub avg_cycles:   u32,
    pub max_cycles:   u32,
    pub avg_ns:       u32,
    pub max_ns:       u32,
}

/// Snapshot current ISR statistics (non-resetting).
pub fn uart_isr_stats_snapshot(cpu_hz: u32) -> IsrStats {
    let calls  = ISR_CALL_COUNT.load(Ordering::Relaxed);
    let total  = ISR_TOTAL_CYCLES.load(Ordering::Relaxed);
    let max    = ISR_MAX_CYCLES.load(Ordering::Relaxed);

    let avg = if calls > 0 { total / calls } else { 0 };

    IsrStats {
        call_count:   calls,
        total_cycles: total,
        avg_cycles:   avg,
        max_cycles:   max,
        avg_ns: cycles_to_ns(avg, cpu_hz),
        max_ns: cycles_to_ns(max, cpu_hz),
    }
}

#[inline]
fn cycles_to_ns(cycles: u32, cpu_hz: u32) -> u32 {
    ((cycles as u64 * 1_000_000_000u64) / cpu_hz as u64) as u32
}
```

### Example 2: CPU Load Monitor in Rust

```rust
use core::sync::atomic::{AtomicU32, Ordering};

static WINDOW_CYCLE_BUDGET: AtomicU32 = AtomicU32::new(0);

/// Compute UART ISR CPU load over a sampling window.
///
/// Call this from a periodic monitoring task (e.g. every 1000 ms).
///
/// Returns load as integer hundredths of a percent:
///   150 => 1.50%
pub fn uart_cpu_load_pct_x100(window_ms: u32, cpu_hz: u32) -> u32 {
    // Atomically snapshot and reset counters
    let cycles_in_isr = ISR_TOTAL_CYCLES.swap(0, Ordering::SeqCst);
    let _calls        = ISR_CALL_COUNT.swap(0, Ordering::SeqCst);

    // Total CPU cycles available in the measurement window
    let window_cycles: u64 = (cpu_hz as u64 * window_ms as u64) / 1000;

    if window_cycles == 0 {
        return 0;
    }

    ((cycles_in_isr as u64 * 10_000) / window_cycles) as u32
}

/// Format and print a load report via semihosting or RTT.
pub fn print_load_report(cpu_hz: u32) {
    let stats = uart_isr_stats_snapshot(cpu_hz);
    let load  = uart_cpu_load_pct_x100(1000, cpu_hz);

    // In a real embedded project, replace with rtt_target::rprintln! or similar
    #[cfg(feature = "semihosting")]
    {
        use cortex_m_semihosting::hprintln;
        hprintln!("=== UART ISR Overhead ===").ok();
        hprintln!("  Calls    : {}", stats.call_count).ok();
        hprintln!("  Avg      : {} cycles ({} ns)", stats.avg_cycles, stats.avg_ns).ok();
        hprintln!("  Max      : {} cycles ({} ns)", stats.max_cycles, stats.max_ns).ok();
        hprintln!("  CPU Load : {}.{:02}%", load / 100, load % 100).ok();
        hprintln!("========================").ok();
    }
}
```

### Example 3: FIFO Drain Loop with Overhead Tracking (Rust, PAC Level)

```rust
use stm32f4xx_hal::pac;

/// UART ISR that drains the hardware FIFO in one invocation.
///
/// On devices with a FIFO, this pattern reduces interrupt frequency
/// by processing multiple bytes per interrupt, amortising the
/// context-save/restore cost across N bytes instead of 1.
pub fn uart_fifo_drain_isr(usart: &pac::USART1, producer: &mut Producer<'static, u8, 512>) {
    let t_start = dwt_cycles();
    let mut bytes_processed: u32 = 0;

    loop {
        let sr = usart.sr.read();

        if sr.rxne().bit_is_clear() {
            break;  // FIFO empty
        }

        let byte = usart.dr.read().dr().bits() as u8;

        if producer.enqueue(byte).is_err() {
            // Buffer full — count overflow events separately if needed
        }

        bytes_processed += 1;

        // Safety guard against infinite loop if hardware misbehaves
        if bytes_processed >= 16 {
            break;
        }
    }

    let elapsed = dwt_cycles().wrapping_sub(t_start);

    // Accumulate stats
    ISR_TOTAL_CYCLES.fetch_add(elapsed, Ordering::Relaxed);
    ISR_CALL_COUNT.fetch_add(1, Ordering::Relaxed);

    let prev_max = ISR_MAX_CYCLES.load(Ordering::Relaxed);
    if elapsed > prev_max {
        ISR_MAX_CYCLES.store(elapsed, Ordering::Relaxed);
    }

    // Amortised cost per byte (useful for comparative analysis)
    let _ = bytes_processed; // log or store as needed
}
```

### Example 4: Overhead Comparison Table Builder (Rust, `no_std`)

```rust
/// Accumulates overhead data across different UART configurations
/// (per-byte ISR vs FIFO-4 vs FIFO-8) for comparison.
#[derive(Default, Debug)]
pub struct OverheadProfile {
    pub label:          &'static str,
    pub total_calls:    u32,
    pub total_cycles:   u64,
    pub max_cycles:     u32,
}

impl OverheadProfile {
    pub const fn new(label: &'static str) -> Self {
        Self {
            label,
            total_calls:  0,
            total_cycles: 0,
            max_cycles:   0,
        }
    }

    /// Record a single ISR invocation.
    #[inline]
    pub fn record(&mut self, cycles: u32) {
        self.total_cycles += cycles as u64;
        self.total_calls  += 1;
        if cycles > self.max_cycles {
            self.max_cycles = cycles;
        }
    }

    /// Average cycles per ISR invocation.
    pub fn avg_cycles(&self) -> u32 {
        if self.total_calls == 0 {
            0
        } else {
            (self.total_cycles / self.total_calls as u64) as u32
        }
    }

    /// Overhead per byte received (bytes_per_isr = FIFO depth or 1).
    pub fn cycles_per_byte(&self, bytes_per_isr: u32) -> u32 {
        if bytes_per_isr == 0 { return 0; }
        self.avg_cycles() / bytes_per_isr
    }
}

// Usage:
//
//   static mut PROFILE_PER_BYTE: OverheadProfile = OverheadProfile::new("per-byte");
//   static mut PROFILE_FIFO4:    OverheadProfile = OverheadProfile::new("fifo-4");
//
//   // In ISR:
//   unsafe { PROFILE_PER_BYTE.record(elapsed); }
//
//   // In monitor:
//   let p = unsafe { &PROFILE_PER_BYTE };
//   // avg_cycles per byte: p.cycles_per_byte(1)
//   // avg_cycles per byte with FIFO-4: p.cycles_per_byte(4)
```

---

## Benchmarking and Profiling Strategies

### Strategy 1: Controlled Loopback Test

Connect TX → RX on the same device. Transmit a known byte stream from a background task and measure ISR statistics over a fixed duration.

```
┌─────────┐     TX ─────────► RX     ┌─────────┐
│ CPU/MCU │◄─────────────────────────│ CPU/MCU │
│  (test) │     known byte stream    │  (same) │
└─────────┘                          └─────────┘
             Loopback cable or wire
```

### Strategy 2: Baud Rate Sweep

Measure overhead at multiple baud rates to characterise how ISR frequency affects total CPU load:

| Baud Rate | ISR/sec (8N1) | Measured Avg Cycles | CPU Load @ 72 MHz |
|---|---|---|---|
| 9,600    | ~960     | ~35 cycles | ~0.05%  |
| 115,200  | ~11,520  | ~35 cycles | ~0.56%  |
| 921,600  | ~92,160  | ~35 cycles | ~4.5%   |
| 4,000,000 | ~400,000 | ~35 cycles | ~19.4%  |

*Approximate values; actual figures depend on ISR body complexity and CPU.*

### Strategy 3: Worst-Case Analysis

Introduce deliberate jitter in ISR scheduling (e.g., by enabling competing high-priority interrupts) and record maximum ISR latency to verify real-time guarantees.

### Strategy 4: Profiling with ITM/SWO (Arm CoreSight)

On Cortex-M3/M4/M7, the Instrumentation Trace Macrocell (ITM) allows timestamped software trace events to be streamed to a debugger over the SWO pin without GPIO toggling.

```c
/* Write a 32-bit timestamp to ITM stimulus port 0 */
static inline void itm_trace(uint32_t value) {
    while (ITM->PORT[0].u32 == 0);   /* wait for port ready */
    ITM->PORT[0].u32 = value;
}

void USART1_IRQHandler(void) {
    itm_trace(dwt_cycles() | 0x80000000U);  /* entry marker */
    /* ... handler body ... */
    itm_trace(dwt_cycles() & 0x7FFFFFFFU);  /* exit marker */
}
```

Capture with OpenOCD, J-Link, or pyOCD, then post-process timestamps offline.

---

## Optimization Techniques

### 1. Increase FIFO Threshold

Trade latency for lower interrupt rate. Use FIFO thresholds of 4, 8, or 14 bytes to reduce interrupt frequency by up to 14×.

### 2. DMA Transfer Mode

Configure UART RX/TX via DMA. The CPU is only interrupted at buffer-full or half-full points, reducing interrupts by orders of magnitude for bulk data.

```c
/* Pseudo-code: DMA-based UART RX on STM32 */
/* DMA transfers data to rx_buf[] autonomously.           */
/* CPU interrupt fires only when half-buffer is full:     */
/*   FULL 512-byte buffer → 2 interrupts instead of 512  */
HAL_UART_Receive_DMA(&huart1, rx_buf, RX_BUF_SIZE);
```

### 3. Minimize ISR Body

Move all non-critical processing (parsing, protocol handling, logging) out of the ISR into a deferred task or thread:

- **ISR:** raw byte → ring buffer (3–5 instructions)
- **Task:** ring buffer → parse → act (unlimited complexity)

### 4. Register Usage

Use `__attribute__((interrupt, optimize("O3")))` in C or `#[naked]` in Rust to take precise control of register save/restore, preventing the compiler from pushing unnecessary callee-saved registers.

### 5. NVIC Priority Tuning

Assign UART a lower priority than hard real-time interrupts to avoid blocking critical ISRs, while keeping UART high enough that its own latency budget is met.

---

## Summary

UART interrupt overhead arises from the cumulative cost of CPU context saves, ISR execution, and pipeline recovery for every interrupt event. At high baud rates this overhead can consume a significant fraction of available CPU cycles.

**Key measurement approaches:**
- **GPIO toggle + oscilloscope** — simplest, cycle-accurate, requires external hardware
- **DWT cycle counter** — software-only, no external hardware, ARM Cortex-M specific
- **ITM/SWO trace** — non-intrusive, requires CoreSight-capable debug probe

**Key metrics to track:**
- Average cycles per ISR invocation
- Maximum (worst-case) ISR duration
- Total CPU load percentage over a measurement window

**Key optimization levers:**
- Increase FIFO trigger threshold to reduce interrupt frequency
- Use DMA for bulk transfers, reserving interrupts for buffer boundaries
- Keep ISR bodies minimal — enqueue bytes, defer all processing
- Use hardware cycle counters and atomic statistics accumulation for low-overhead profiling in both C and Rust

Both C/C++ and Rust provide the primitives needed for accurate, low-intrusion overhead analysis. Rust's type system and atomic types offer additional safety guarantees when sharing statistics between ISR and foreground contexts without locks.

---

*Document: 98 — UART Interrupt Overhead Analysis*  
*Covers: ARM Cortex-M, STM32, 16550-compatible UARTs*  
*Languages: C/C++, Rust (cortex-m, RTIC, heapless)*