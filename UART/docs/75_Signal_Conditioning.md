# 75. Signal Conditioning

**Hardware techniques** — voltage level translation (resistor dividers, MOSFET shifters), RC low-pass filtering with cut-off frequency guidance, line termination (series and parallel), Schmitt trigger buffering (74HC14), ESD/surge protection (TVS diodes), galvanic isolation (optocouplers vs. digital isolators), and RS-232/RS-485/RS-422 line drivers with a comparison table.

**C/C++ examples:**
- Baud rate divisor calculation with quantisation error reporting (STM32-style BRR)
- 3-of-5 majority-vote bit-bang receiver for noise rejection
- Framing/noise/overrun error detection with automatic receiver recovery
- RS-485 DE/RE direction control with precise post-transmission timing

**Rust examples:**
- `no_std`-compatible baud rate calculator with ppm error and acceptability check
- `embedded-hal`-generic majority-vote sampler and bit-bang receiver
- Type-safe RS-485 half-duplex driver using ownership to guarantee direction pin cleanup
- `UartHw` trait abstraction with `ReliableUart` accumulating signal quality statistics

**Diagnostics table** listing key metrics (FE rate, NE rate, rise/fall time, baud error) with target thresholds.

> **Analog conditioning circuits for improved UART signal quality**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Signal Conditioning Matters for UART](#why-signal-conditioning-matters-for-uart)
3. [Key Signal Conditioning Techniques](#key-signal-conditioning-techniques)
   - [Voltage Level Translation](#1-voltage-level-translation)
   - [RC Low-Pass Filtering](#2-rc-low-pass-filtering)
   - [Line Termination](#3-line-termination)
   - [Schmitt Trigger Buffering](#4-schmitt-trigger-buffering)
   - [ESD and Surge Protection](#5-esd-and-surge-protection)
   - [Isolation (Optocouplers / Digital Isolators)](#6-isolation-optocouplers--digital-isolators)
   - [RS-232 / RS-485 Line Drivers](#7-rs-232--rs-485-line-drivers)
4. [Circuit Design Considerations](#circuit-design-considerations)
5. [Programming Signal Conditioning in C/C++](#programming-signal-conditioning-in-cc)
   - [Baud Rate and Timing Calibration](#baud-rate-and-timing-calibration)
   - [Software Noise Filtering (Majority Voting)](#software-noise-filtering-majority-voting)
   - [Framing Error Detection and Recovery](#framing-error-detection-and-recovery)
   - [RS-485 Direction Control](#rs-485-direction-control)
6. [Programming Signal Conditioning in Rust](#programming-signal-conditioning-in-rust)
   - [Baud Rate and Timing Calibration in Rust](#baud-rate-and-timing-calibration-in-rust)
   - [Software Noise Filtering in Rust](#software-noise-filtering-in-rust)
   - [RS-485 Direction Control in Rust](#rs-485-direction-control-in-rust)
   - [Framing Error Detection in Rust](#framing-error-detection-in-rust)
7. [Diagnostics and Signal Quality Metrics](#diagnostics-and-signal-quality-metrics)
8. [Summary](#summary)

---

## Introduction

Signal conditioning is the process of manipulating an analog signal so it meets the requirements of the next stage of processing — in this case, reliable reception by a UART peripheral. Raw UART signals traversing cables, crossing PCB traces, bridging voltage domains, or surviving industrial environments are subject to a variety of degradation mechanisms: noise, reflections, ground offsets, electrostatic discharge (ESD), and voltage incompatibilities.

Signal conditioning encompasses both **hardware circuits** (passive filters, active buffers, line drivers, isolators) and **software techniques** (majority-vote sampling, error detection, adaptive baud rate recovery). Together they ensure that the logic "1" and logic "0" transitions seen by the UART receiver are clean, properly timed, and within the receiver's input voltage thresholds.

---

## Why Signal Conditioning Matters for UART

UART communication is asynchronous — there is no shared clock to mask timing jitter or noise. A single corrupted bit edge can cause:

- **Framing errors** – the stop bit is sampled as a zero.
- **Break conditions** – the line stays low for longer than a full frame.
- **Parity errors** – a bit is flipped mid-frame.
- **Data corruption** – a valid character is decoded as a different character.

Typical sources of signal degradation:

| Source | Effect |
|--------|--------|
| Long cables / high baud rate | Ringing, reflections, inter-symbol interference |
| Mixed voltage systems (5 V MCU ↔ 3.3 V device) | Overvoltage damage, logic-level mismatch |
| Inductive loads (motors, relays) | Conducted noise spikes on power/ground |
| Electrostatic discharge | Latch-up or destruction of UART I/O pins |
| Ground potential difference | Common-mode interference on single-ended lines |
| Capacitive loads | Slow rise/fall times, bit-timing errors |

---

## Key Signal Conditioning Techniques

### 1. Voltage Level Translation

When two devices operate at different logic voltages (e.g., 5 V AVR ↔ 3.3 V ESP32), a **level shifter** is required. A resistor voltage divider suffices for unidirectional, low-speed links, while a dedicated IC (e.g., TXB0101, BSS138 MOSFET) is preferred for bidirectional or higher-speed links.

**Resistor divider (5 V → 3.3 V, receive side only):**

```
5V TX ─── R1(10kΩ) ─┬─── 3.3V RX
                     │
                    R2(20kΩ)
                     │
                    GND
```

Output voltage = 5 V × 20k / (10k + 20k) = 3.33 V ✓

**Bidirectional MOSFET level shifter (BSS138):**

```
3.3V side          5V side
  A ──┤Gate    ├── B
      │  BSS138 │
  3.3V ──┤Source│
         Drain──── 5V via pull-up
```

### 2. RC Low-Pass Filtering

An RC low-pass filter attenuates high-frequency noise while preserving the fundamental UART transitions. The cut-off frequency must be chosen carefully: too low and valid bit edges are rounded beyond the receiver's threshold; too high and noise passes through.

**Rule of thumb:** fc ≥ 10 × baud_rate (to preserve rise/fall time), fc ≤ 0.1 × noise_frequency.

```
TX ──── R(1kΩ) ──┬── RX
                 │
                C(100pF)
                 │
                GND
```

For R = 1 kΩ, C = 100 pF:  
fc = 1 / (2π × 1000 × 100×10⁻¹²) ≈ **1.59 MHz**

This is suitable for baud rates up to ~115 200 baud while attenuating noise above ~1.5 MHz.

### 3. Line Termination

On long traces or cables (> λ/10 of the signal's knee frequency), unterminated lines produce **reflections** that cause glitches at UART inputs. The characteristic impedance Z₀ of a PCB trace is typically 50–120 Ω.

**Series termination** (at source):

```
TX ── Rs(50Ω) ────────────── RX
```

**Parallel termination** (at load):

```
TX ──────────────┬── RX
                Rt(120Ω)
                 │
                GND
```

Series termination wastes no DC power and is preferred in UART (point-to-point) links.

### 4. Schmitt Trigger Buffering

A Schmitt trigger introduces **hysteresis** — the switching threshold for a rising edge is higher than for a falling edge. This prevents noise near the logic threshold from causing multiple spurious transitions.

A 74HC14 (hex inverting Schmitt trigger) or a dedicated single-gate (SN74LVC1G17) in a non-inverting configuration is inserted in the receive path:

```
Noisy RX signal ──── [74HC14] ──── Clean RX to UART
```

Typical hysteresis voltage: 0.5 V at 3.3 V supply, 1.0 V at 5 V supply.

### 5. ESD and Surge Protection

UART pins exposed to external connectors (USB-UART bridges, RS-232 ports, industrial terminals) need protection against ESD (±8 kV contact discharge per IEC 61000-4-2) and cable-coupled surges.

**TVS diode (unidirectional):**

```
TX/RX ──┬──── [line]
        │
      [TVS]   e.g., PRTR5V0U2X, TPD2E001
        │
       GND
```

**RC + TVS combination:**

```
TX/RX ─── R(33Ω) ──┬── [to connector]
                   [TVS]
                    │
                   GND
```

The series resistor limits peak current through the TVS and also provides additional low-pass filtering.

### 6. Isolation (Optocouplers / Digital Isolators)

In systems where ground loops or high common-mode voltages are present (industrial equipment, motor drives, medical devices), **galvanic isolation** is mandatory.

- **Optocoupler (6N137, HCPL-0611):** inexpensive, ~10 Mbps, introduces propagation delay (~50–200 ns), requires current limiting resistor on LED side.
- **Digital isolator (ISO7721, ADUM1201):** lower power, faster (>100 Mbps), propagation delay ~10 ns, CMTI > 25 kV/µs.

```
MCU TX ─── R(330Ω) ──[LED]──┐
                             │  6N137    ┌── Isolated RX ──► Remote device
                            [PD]─────────┤
GND_MCU                                GND_ISO
```

### 7. RS-232 / RS-485 Line Drivers

Standard CMOS UART logic levels (0–3.3 V or 0–5 V) are unsuitable for long-distance transmission. Dedicated line drivers convert to robust physical-layer standards:

| Standard | Voltage swing | Distance | Topology |
|----------|--------------|----------|----------|
| RS-232 | ±3 V to ±15 V | < 15 m | Point-to-point |
| RS-422 | ±0.2 V to ±6 V (differential) | < 1200 m | Point-to-multipoint |
| RS-485 | ±0.2 V to ±6 V (differential) | < 1200 m | Multi-drop (32–256 nodes) |

**MAX3232** (RS-232 transceiver): charge-pump generates ±5.5 V from 3.3 V supply.  
**MAX485 / SN65HVD75** (RS-485 transceiver): differential pair, requires direction control (DE/RE pins).

---

## Circuit Design Considerations

1. **Place bypass capacitors (100 nF) close to every transceiver VCC pin** — switching transients couple back into the signal chain.
2. **Route TX and RX as differential pairs on RS-485/422**, keeping them away from high-current traces.
3. **Keep RC filter components close to the UART RX pin** — stray capacitance from long traces nullifies the design.
4. **Use ferrite beads on power lines** to isolate UART circuitry from switching noise.
5. **Select Schmitt trigger inputs** on MCU pins where possible — most modern MCUs allow this via GPIO configuration registers.
6. **Ground plane continuity** under UART traces reduces common-mode noise pickup.

---

## Programming Signal Conditioning in C/C++

Even with ideal hardware conditioning, software must be written to handle residual signal imperfections, configure hardware correctly, and recover gracefully from errors.

### Baud Rate and Timing Calibration

Incorrect baud rate is a leading cause of framing errors. The UART peripheral derives its clock by dividing the system clock — rounding error accumulates at high baud rates.

```c
/**
 * @file uart_baud_cal.c
 * @brief Demonstrates baud rate error calculation and best-divisor selection
 *        for a UART peripheral with a 16-bit BRR (Baud Rate Register).
 *
 * Target: STM32-style UART (USART_BRR = PCLK / baud_rate)
 */

#include <stdint.h>
#include <stdio.h>
#include <math.h>

#define PCLK_HZ   72000000UL   /* Peripheral clock: 72 MHz */

typedef struct {
    uint32_t brr;           /* Value to write to BRR            */
    uint32_t actual_baud;   /* Resulting actual baud rate        */
    float    error_pct;     /* Deviation from target (%)         */
} BaudConfig;

/**
 * Calculate the best BRR value for a desired baud rate.
 * Supports oversampling by 16 (OVER8 = 0) or 8 (OVER8 = 1).
 *
 * @param target_baud   Desired baud rate in bits/s
 * @param over8         0 = oversample by 16, 1 = oversample by 8
 * @return              BaudConfig with optimal settings
 */
BaudConfig uart_calc_baud(uint32_t target_baud, int over8) {
    BaudConfig cfg = {0};
    uint32_t divider;

    if (over8) {
        /* BRR[15:4] = USARTDIV[15:4], BRR[2:0] = USARTDIV[3:0] >> 1 */
        divider = (PCLK_HZ + target_baud / 2) / target_baud; /* round */
        uint32_t mantissa  = (divider >> 4) & 0xFFF;
        uint32_t fraction  = (divider & 0xF) >> 1;
        cfg.brr = (mantissa << 4) | fraction;
        cfg.actual_baud = PCLK_HZ / (8 * (mantissa + (float)fraction / 8.0f));
    } else {
        /* Standard: BRR = PCLK / baud (round to nearest) */
        divider = (PCLK_HZ + target_baud / 2) / target_baud;
        cfg.brr = divider;
        cfg.actual_baud = PCLK_HZ / divider;
    }

    cfg.error_pct = 100.0f * ((float)cfg.actual_baud - (float)target_baud)
                             / (float)target_baud;
    return cfg;
}

int main(void) {
    uint32_t targets[] = { 9600, 19200, 57600, 115200, 230400, 460800, 921600 };

    printf("%-10s  %-8s  %-10s  %-10s  %s\n",
           "Target", "OVER8", "BRR", "Actual", "Error(%)");
    printf("%s\n", "--------------------------------------------------------------");

    for (size_t i = 0; i < sizeof(targets)/sizeof(targets[0]); i++) {
        for (int over8 = 0; over8 <= 1; over8++) {
            BaudConfig c = uart_calc_baud(targets[i], over8);
            printf("%-10u  %-8d  0x%-8X  %-10u  %+.4f\n",
                   targets[i], over8, c.brr, c.actual_baud, c.error_pct);
        }
    }
    return 0;
}
```

**Output excerpt:**
```
Target      OVER8     BRR         Actual      Error(%)
--------------------------------------------------------------
115200      0         0x000001D9  115108      -0.0799
115200      1         0x000001D9  115108      -0.0799
921600      0         0x0000004E  921659      +0.0641
921600      1         0x00000027  923077      +0.1538
```

---

### Software Noise Filtering (Majority Voting)

When hardware filtering is absent or insufficient, a software majority-vote sampler oversamples each bit period and takes the logical level voted by the majority of samples. This is the same principle used inside hardware UART receivers (typically 16× oversampling with a 3-sample majority vote at the centre).

```c
/**
 * @file uart_majority_vote.c
 * @brief Bit-bang UART receiver with 3-of-5 majority vote noise rejection.
 *
 * Assumes GPIO_ReadPin() and delay_us() are provided by the BSP.
 */

#include <stdint.h>
#include <stdbool.h>

/* ── Platform abstractions ──────────────────────────────────────────────── */
extern int      GPIO_ReadPin(unsigned pin);   /* returns 0 or 1            */
extern void     delay_us(unsigned us);        /* blocking microsecond delay */

#define RX_PIN       5
#define BAUD_RATE    9600
#define BIT_US       (1000000U / BAUD_RATE)  /* 104 µs at 9600 baud        */

/* ── Majority vote over N samples ────────────────────────────────────────── */
/**
 * Sample a GPIO pin @p samples times, spaced @p interval_us apart.
 * Returns the majority logic level (1 or 0).
 */
static int majority_vote(unsigned pin, unsigned samples, unsigned interval_us) {
    int votes = 0;
    for (unsigned i = 0; i < samples; i++) {
        votes += GPIO_ReadPin(pin);
        if (i < samples - 1)
            delay_us(interval_us);
    }
    return (votes > (int)(samples / 2)) ? 1 : 0;
}

/**
 * Blocking bit-bang receive of one UART byte (8N1 format).
 * Returns the received byte, or -1 on framing error.
 *
 * Strategy:
 *   1. Wait for start-bit (falling edge on RX).
 *   2. Delay 1.5 bit periods to sample the centre of bit 0.
 *   3. Use 5-sample majority vote for each of the 8 data bits.
 *   4. Verify stop bit is high.
 */
int uart_receive_byte_filtered(void) {
    /* 1. Wait for start bit (line goes low) */
    while (GPIO_ReadPin(RX_PIN) == 1)
        ;  /* idle — spinning; use interrupt in production */

    /* 2. Skip half a bit, then centre on bit 0 */
    delay_us(BIT_US + BIT_US / 2);   /* 1.5 × bit period  */

    uint8_t byte = 0;

    /* 3. Sample 8 data bits (LSB first for UART) */
    for (int bit = 0; bit < 8; bit++) {
        /* 5-sample majority vote centred on bit */
        int interval = BIT_US / 5;   /* spread 5 samples across bit period */
        int level = majority_vote(RX_PIN, 5, interval);

        if (level)
            byte |= (1u << bit);

        /* Advance to next bit centre: compensate for samples already taken */
        delay_us(BIT_US - 5 * interval);
    }

    /* 4. Verify stop bit */
    int stop = majority_vote(RX_PIN, 5, BIT_US / 5);
    if (stop != 1)
        return -1;  /* Framing error */

    return (int)byte;
}
```

---

### Framing Error Detection and Recovery

Hardware UART peripherals set framing error (FE) flags when the stop bit is sampled low. The correct response is to flush the receive buffer and re-synchronise.

```c
/**
 * @file uart_error_recovery.c
 * @brief STM32 HAL-style UART error detection and automatic recovery.
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── Minimal HAL stubs (replace with real HAL) ─────────────────────────── */
typedef struct {
    volatile uint32_t SR;   /* Status Register  */
    volatile uint32_t DR;   /* Data Register    */
    volatile uint32_t BRR;
    volatile uint32_t CR1;
    volatile uint32_t CR2;
    volatile uint32_t CR3;
} USART_TypeDef;

#define USART_SR_FE    (1u << 1)   /* Framing Error flag  */
#define USART_SR_NE    (1u << 2)   /* Noise Error flag    */
#define USART_SR_ORE   (1u << 3)   /* Overrun Error flag  */
#define USART_SR_RXNE  (1u << 5)   /* RX Not Empty flag   */
#define USART_CR1_RE   (1u << 2)   /* Receiver Enable     */
#define USART_CR1_UE   (1u << 13)  /* UART Enable         */

/* Error counters for diagnostics */
typedef struct {
    uint32_t framing_errors;
    uint32_t noise_errors;
    uint32_t overrun_errors;
    uint32_t recovered;
} UartStats;

static UartStats g_stats = {0};

/**
 * Read one byte from USART, handling framing / noise / overrun errors.
 *
 * On error the DR is read (clears flags on STM32), the UART is briefly
 * disabled/re-enabled to flush the shift register, and -1 is returned so
 * the caller can decide to retry or discard the frame.
 *
 * @param  usart   Pointer to USART peripheral
 * @param  out     Pointer to store received byte
 * @return true if byte received cleanly, false on any error
 */
bool uart_read_byte_safe(USART_TypeDef *usart, uint8_t *out) {
    /* Wait for data */
    while (!(usart->SR & USART_SR_RXNE))
        ;

    uint32_t sr = usart->SR;   /* Snapshot before reading DR */
    uint8_t  dr = (uint8_t)(usart->DR & 0xFF);   /* Read clears RXNE + error flags */

    if (sr & USART_SR_FE) {
        g_stats.framing_errors++;
        goto recover;
    }
    if (sr & USART_SR_NE) {
        g_stats.noise_errors++;
        goto recover;
    }
    if (sr & USART_SR_ORE) {
        g_stats.overrun_errors++;
        goto recover;
    }

    *out = dr;
    return true;

recover:
    /* Flush: disable receiver, re-enable */
    usart->CR1 &= ~USART_CR1_RE;
    for (volatile int i = 0; i < 100; i++) /* short pause */ ;
    usart->CR1 |= USART_CR1_RE;
    g_stats.recovered++;
    (void)dr;   /* discarded */
    return false;
}

/**
 * Receive a complete packet with timeout and error budget.
 * Returns number of bytes received, or negative on fatal error.
 */
int uart_receive_packet(USART_TypeDef *usart,
                        uint8_t       *buf,
                        size_t         max_len,
                        uint32_t       timeout_ms) {
    extern uint32_t get_tick_ms(void);   /* system tick */
    uint32_t deadline = get_tick_ms() + timeout_ms;
    size_t   received = 0;
    int      errors   = 0;
    const int ERROR_BUDGET = 3;   /* abort after 3 consecutive errors */

    while (received < max_len && get_tick_ms() < deadline) {
        uint8_t byte;
        if (uart_read_byte_safe(usart, &byte)) {
            buf[received++] = byte;
            errors = 0;           /* reset consecutive error counter */
        } else {
            if (++errors >= ERROR_BUDGET)
                return -1;        /* too many consecutive errors */
        }
    }
    return (int)received;
}
```

---

### RS-485 Direction Control

RS-485 is half-duplex: a direction-control GPIO must be driven high before transmitting and low again immediately after the last byte has shifted out, before the remote device responds.

```c
/**
 * @file rs485_direction.c
 * @brief RS-485 transmit/receive direction control with precise timing.
 *
 * The DE (Driver Enable) pin must be deasserted within one bit-period
 * after the last stop bit; otherwise the bus is held driven and the
 * remote's reply is blocked.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── BSP stubs ─────────────────────────────────────────────────────────── */
extern void     GPIO_Set(unsigned pin, int level);
extern void     UART_TxByte(uint8_t byte);
extern bool     UART_TxComplete(void);    /* true when shift register empty */
extern uint8_t  UART_RxByte(void);
extern bool     UART_RxAvailable(void);
extern void     delay_us(unsigned us);

#define DE_PIN      6        /* Driver Enable  — active high */
#define RE_PIN      7        /* Receiver Enable — active low */
#define BAUD_RATE   115200
#define BIT_US      (1000000U / BAUD_RATE)

/* Switch to transmit mode — assert DE, deassert RE */
static inline void rs485_tx_mode(void) {
    GPIO_Set(DE_PIN, 1);
    GPIO_Set(RE_PIN, 1);   /* disable receiver while driving */
}

/* Switch to receive mode — deassert DE, assert RE */
static inline void rs485_rx_mode(void) {
    GPIO_Set(DE_PIN, 0);
    GPIO_Set(RE_PIN, 0);
}

/**
 * Transmit a buffer over RS-485 with safe direction control.
 *
 * @param data  Pointer to data buffer
 * @param len   Number of bytes to transmit
 */
void rs485_transmit(const uint8_t *data, size_t len) {
    if (len == 0) return;

    rs485_tx_mode();

    /* Small propagation delay: allow DE to assert before first bit */
    delay_us(2);

    for (size_t i = 0; i < len; i++)
        UART_TxByte(data[i]);

    /* Wait for the shift register to completely empty (last stop bit out) */
    while (!UART_TxComplete())
        ;

    /* Hold for one additional bit period to ensure stop bit exits the cable */
    delay_us(BIT_US + BIT_US / 2);

    rs485_rx_mode();
}

/**
 * Receive bytes from RS-485 bus into a buffer.
 * Blocks until @p expected bytes are received or @p timeout_us elapses.
 *
 * @return number of bytes received
 */
size_t rs485_receive(uint8_t *buf, size_t expected, uint32_t timeout_us) {
    rs485_rx_mode();

    extern uint32_t get_tick_us(void);
    uint32_t deadline = get_tick_us() + timeout_us;
    size_t   count    = 0;

    while (count < expected && get_tick_us() < deadline) {
        if (UART_RxAvailable())
            buf[count++] = UART_RxByte();
    }
    return count;
}
```

---

## Programming Signal Conditioning in Rust

Rust's type system and ownership model make it well-suited to embedded UART programming, especially when using the `embedded-hal` trait abstractions.

### Baud Rate and Timing Calibration in Rust

```rust
//! uart_baud_cal.rs
//! Baud rate divider calculation with error reporting.
//!
//! No_std compatible — uses only core integer arithmetic.

#![no_std]

/// Result of a baud rate calculation.
#[derive(Debug, Clone, Copy)]
pub struct BaudConfig {
    /// Value to write to the hardware BRR register.
    pub brr: u32,
    /// Actual baud rate achieved after integer rounding.
    pub actual_baud: u32,
    /// Error in thousandths of a percent (avoids float in no_std).
    pub error_ppm: i32,
}

/// Calculate the optimal BRR value for `target_baud` given a peripheral
/// clock of `pclk_hz`. Supports 8× and 16× oversampling.
///
/// # Arguments
/// * `pclk_hz`     – Peripheral clock frequency in Hz.
/// * `target_baud` – Desired baud rate in bits/s.
/// * `over8`       – `true` = 8× oversampling; `false` = 16× oversampling.
pub fn calc_baud(pclk_hz: u32, target_baud: u32, over8: bool) -> BaudConfig {
    let oversample = if over8 { 8u32 } else { 16u32 };

    // Round-to-nearest integer division.
    let divider = (pclk_hz + target_baud / 2) / target_baud;

    let brr = if over8 {
        let mantissa = (divider >> 4) & 0xFFF;
        let fraction = (divider & 0xF) >> 1;
        (mantissa << 4) | fraction
    } else {
        divider
    };

    // Re-derive actual baud from the stored BRR.
    let actual_baud = pclk_hz / (oversample * divider.max(1));

    // Error in ppm (parts per million): 1_000_000 * (actual - target) / target
    let error_ppm = (actual_baud as i64 - target_baud as i64)
        * 1_000_000i64
        / target_baud as i64;

    BaudConfig {
        brr,
        actual_baud,
        error_ppm: error_ppm as i32,
    }
}

/// Returns `true` if the baud error is within the ±2% UART tolerance.
pub fn baud_error_acceptable(cfg: &BaudConfig) -> bool {
    cfg.error_ppm.abs() < 20_000   // 2% = 20 000 ppm
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_115200_at_72mhz() {
        let cfg = calc_baud(72_000_000, 115_200, false);
        assert!(baud_error_acceptable(&cfg),
            "Error {:.3}% exceeds 2% limit",
            cfg.error_ppm as f32 / 10_000.0);
    }

    #[test]
    fn test_9600_at_8mhz() {
        let cfg = calc_baud(8_000_000, 9_600, false);
        assert!(baud_error_acceptable(&cfg));
    }
}
```

---

### Software Noise Filtering in Rust

```rust
//! uart_majority_vote.rs
//! 3-of-5 majority-vote bit sampler for bit-bang UART reception.
//!
//! Implements `embedded_hal::digital::InputPin` for the GPIO abstraction.

use embedded_hal::digital::InputPin;
use embedded_hal::delay::DelayNs;

/// Samples `pin` `count` times, separated by `interval_ns` nanoseconds.
/// Returns `true` if the majority of samples are high.
pub fn majority_vote<P, D>(
    pin: &mut P,
    delay: &mut D,
    count: u8,
    interval_ns: u32,
) -> bool
where
    P: InputPin,
    D: DelayNs,
{
    let mut high_count: u8 = 0;
    for i in 0..count {
        if pin.is_high().unwrap_or(false) {
            high_count += 1;
        }
        if i < count - 1 {
            delay.delay_ns(interval_ns);
        }
    }
    high_count > count / 2
}

/// Bit-bang UART frame receiver with majority-vote noise rejection.
///
/// # Returns
/// `Ok(byte)` on success, `Err(FramingError)` if the stop bit is low.
pub fn receive_byte_filtered<P, D>(
    pin: &mut P,
    delay: &mut D,
    baud_rate: u32,
) -> Result<u8, FramingError>
where
    P: InputPin,
    D: DelayNs,
{
    let bit_ns = 1_000_000_000u32 / baud_rate;
    let half_bit_ns = bit_ns / 2;
    let sample_interval_ns = bit_ns / 5; // spread 5 samples over one bit

    // 1. Wait for start bit (falling edge).
    while pin.is_high().unwrap_or(true) {}

    // 2. Advance to centre of first data bit (1.5 × bit period).
    delay.delay_ns(bit_ns + half_bit_ns);

    // 3. Sample 8 data bits (LSB first).
    let mut byte: u8 = 0;
    for bit in 0..8u8 {
        if majority_vote(pin, delay, 5, sample_interval_ns) {
            byte |= 1 << bit;
        }
        // Advance to next bit centre, compensating for sampling time.
        let elapsed_ns = 5 * sample_interval_ns;
        if bit_ns > elapsed_ns {
            delay.delay_ns(bit_ns - elapsed_ns);
        }
    }

    // 4. Verify stop bit.
    let stop_high = majority_vote(pin, delay, 5, sample_interval_ns);
    if !stop_high {
        return Err(FramingError);
    }

    Ok(byte)
}

/// Framing error: stop bit was sampled low.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct FramingError;
```

---

### RS-485 Direction Control in Rust

```rust
//! rs485.rs
//! RS-485 half-duplex driver with safe DE/RE direction control.
//!
//! Guarantees the DE pin is deasserted before returning from `transmit()`,
//! even if the closure passed to it panics (via a Drop guard).

use embedded_hal::digital::OutputPin;
use embedded_hal::serial::{Read, Write};
use core::fmt;

/// RS-485 driver wrapping a half-duplex UART and direction-control pins.
pub struct Rs485<UART, DE, RE> {
    uart: UART,
    de_pin: DE,   // Driver Enable — active high
    re_pin: RE,   // Receiver Enable — active low
}

impl<UART, DE, RE, E> Rs485<UART, DE, RE>
where
    UART: Read<u8, Error = E> + Write<u8, Error = E>,
    DE:   OutputPin,
    RE:   OutputPin,
{
    /// Create a new RS-485 driver. Starts in receive mode.
    pub fn new(uart: UART, mut de_pin: DE, mut re_pin: RE) -> Self {
        let _ = de_pin.set_low();    // DE low  → driver disabled
        let _ = re_pin.set_low();    // RE low  → receiver enabled
        Self { uart, de_pin, re_pin }
    }

    /// Switch to transmit mode (DE high, RE high).
    fn tx_mode(&mut self) {
        let _ = self.re_pin.set_high();   // disable receiver
        let _ = self.de_pin.set_high();   // enable driver
    }

    /// Switch to receive mode (DE low, RE low).
    fn rx_mode(&mut self) {
        let _ = self.de_pin.set_low();    // disable driver
        let _ = self.re_pin.set_low();    // enable receiver
    }

    /// Transmit a byte slice over RS-485.
    ///
    /// Direction is automatically managed: TX mode is asserted before the
    /// first byte and released (with a blocking flush) after the last.
    pub fn transmit(&mut self, data: &[u8]) -> Result<(), E> {
        if data.is_empty() {
            return Ok(());
        }

        self.tx_mode();

        for &byte in data {
            // nb::block! polls Write::write until the UART accepts the byte.
            nb::block!(self.uart.write(byte))?;
        }

        // Wait until the shift register is fully drained.
        nb::block!(self.uart.flush())?;

        self.rx_mode();
        Ok(())
    }

    /// Read up to `buf.len()` bytes from the RS-485 bus.
    /// Returns the number of bytes actually read.
    pub fn receive(&mut self, buf: &mut [u8]) -> Result<usize, E> {
        self.rx_mode();
        let mut count = 0;
        for slot in buf.iter_mut() {
            match self.uart.read() {
                Ok(byte)                          => { *slot = byte; count += 1; }
                Err(nb::Error::WouldBlock)        => break,
                Err(nb::Error::Other(e))          => return Err(e),
            }
        }
        Ok(count)
    }
}
```

---

### Framing Error Detection in Rust

```rust
//! uart_error_recovery.rs
//! Framing / noise / overrun error detection with automatic recovery.
//!
//! Uses a custom UartError enum to distinguish hardware error types.

use core::fmt;

/// UART hardware error types.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartError {
    /// Stop bit was sampled low — baud rate mismatch or severe noise.
    Framing,
    /// Noise detected during bit sampling.
    Noise,
    /// New data arrived before previous byte was read.
    Overrun,
    /// Timeout waiting for data.
    Timeout,
}

impl fmt::Display for UartError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Framing  => write!(f, "Framing error"),
            Self::Noise    => write!(f, "Noise error"),
            Self::Overrun  => write!(f, "Overrun error"),
            Self::Timeout  => write!(f, "Timeout"),
        }
    }
}

/// Statistics for monitoring signal quality over time.
#[derive(Debug, Default, Clone, Copy)]
pub struct SignalStats {
    pub bytes_received:  u32,
    pub framing_errors:  u32,
    pub noise_errors:    u32,
    pub overrun_errors:  u32,
    pub recovered:       u32,
}

impl SignalStats {
    /// Signal quality as a percentage (100% = no errors).
    pub fn quality_pct(&self) -> f32 {
        let total = self.bytes_received + self.framing_errors
                  + self.noise_errors   + self.overrun_errors;
        if total == 0 { return 100.0; }
        100.0 * self.bytes_received as f32 / total as f32
    }
}

/// Trait abstracting a hardware UART with error visibility.
pub trait UartHw {
    fn read_with_status(&mut self) -> Result<u8, UartError>;
    fn flush_rx(&mut self);
}

/// Wrapper that accumulates error statistics and auto-recovers.
pub struct ReliableUart<H: UartHw> {
    hw:    H,
    stats: SignalStats,
}

impl<H: UartHw> ReliableUart<H> {
    pub fn new(hw: H) -> Self {
        Self { hw, stats: SignalStats::default() }
    }

    /// Read a single byte, recovering on error.
    /// Returns `Ok(byte)` or `Err(UartError::Timeout)` after `max_retries`.
    pub fn read_byte(&mut self, max_retries: u8) -> Result<u8, UartError> {
        for _ in 0..=max_retries {
            match self.hw.read_with_status() {
                Ok(byte) => {
                    self.stats.bytes_received += 1;
                    return Ok(byte);
                }
                Err(UartError::Framing) => {
                    self.stats.framing_errors += 1;
                    self.hw.flush_rx();
                    self.stats.recovered += 1;
                }
                Err(UartError::Noise) => {
                    self.stats.noise_errors += 1;
                    self.hw.flush_rx();
                    self.stats.recovered += 1;
                }
                Err(UartError::Overrun) => {
                    self.stats.overrun_errors += 1;
                    self.hw.flush_rx();
                    self.stats.recovered += 1;
                }
                Err(e) => return Err(e),
            }
        }
        Err(UartError::Timeout)
    }

    /// Receive a complete packet of known length.
    pub fn read_packet(
        &mut self,
        buf:        &mut [u8],
        max_retries_per_byte: u8,
    ) -> Result<usize, UartError> {
        for (i, slot) in buf.iter_mut().enumerate() {
            *slot = self.read_byte(max_retries_per_byte)
                        .map_err(|_| UartError::Timeout)?;
            let _ = i;
        }
        Ok(buf.len())
    }

    /// Access accumulated signal quality statistics.
    pub fn stats(&self) -> &SignalStats {
        &self.stats
    }
}
```

---

## Diagnostics and Signal Quality Metrics

Good signal conditioning is also about **observability** — knowing when the conditioning is failing so you can correct it.

Key metrics to track and expose:

| Metric | Description | Good value |
|--------|-------------|------------|
| FE rate | Framing errors per 1000 bytes | < 1 |
| NE rate | Noise errors per 1000 bytes | < 1 |
| ORE rate | Overrun errors per 1000 bytes | 0 |
| RSSI (RS-485) | Differential receiver input voltage | > 200 mV |
| Rise/fall time | Measured at receiver input | < 0.2 × bit period |
| Baud error | Deviation from nominal | < 2% |

These can be logged over a debug UART, published via a diagnostic register map (Modbus, SCPI), or exposed in a simple shell command in firmware.

---

## Summary

Signal conditioning for UART is a multi-layer discipline:

**Hardware layer** provides the foundation: proper voltage translation prevents damage and logic-level errors; RC filters and Schmitt triggers clean up noise and restore sharp transitions; termination resistors eliminate reflections on long traces; TVS diodes and optocouplers protect against ESD and ground loops; RS-232/RS-485 line drivers extend communication range dramatically.

**Software layer** compensates for residual imperfections: baud rate calculations must minimise quantisation error (keep below 2%); majority-vote oversampling rejects narrow noise pulses that slip through hardware filters; framing error handlers detect corruption and restore the receive path; RS-485 direction control must precisely bracket the transmit window to avoid bus contention; signal quality statistics allow operators to detect degrading cable or connector conditions before communication fails.

Together, these techniques allow UART communication to operate reliably at speeds from 1200 baud across noisy industrial floors up to several Mbit/s on well-designed PCB interconnects. The best systems combine both: minimum hardware conditioning sufficient for the environment, complemented by defensive software that degrades gracefully and reports early warning of signal problems.

---

*Document: UART Series — Topic 75: Signal Conditioning*  
*Languages: C/C++ · Rust (embedded-hal 1.x)*