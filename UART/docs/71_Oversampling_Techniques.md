# 71. UART Oversampling Techniques

- **Concept** — what oversampling is, why it exists, and how both 16x and 8x work including the sample-point timing diagrams
- **Majority voting** — the 2-of-3 vote logic, noise flag semantics, and the boolean expression behind it
- **Baud rate calculation** — the BRR register formulas for both modes with a concrete 84 MHz / 115200 baud worked example
- **C/C++ examples** — STM32 HAL config, bare-metal register access with correct BRR math for both modes, a full AVR software timer-ISR oversampled receiver, and a standalone majority vote utility
- **Rust examples** — `stm32f4xx-hal` builder pattern with error handling, a `no_std` software UART receiver struct driven by a timer tick, and generic majority vote functions with unit tests
- **Practical checklist** — clock accuracy requirements, baud rate selection pitfalls, and noise flag monitoring guidance
- **Summary table** — side-by-side comparison of 16x vs 8x across all key parameters

## 8x, 16x Oversampling for Improved Noise Immunity

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is Oversampling?](#what-is-oversampling)
3. [Why Oversample?](#why-oversample)
4. [How Oversampling Works](#how-oversampling-works)
   - [16x Oversampling](#16x-oversampling)
   - [8x Oversampling](#8x-oversampling)
5. [Baud Rate Generation with Oversampling](#baud-rate-generation-with-oversampling)
6. [Majority Voting / Noise Filtering](#majority-voting--noise-filtering)
7. [Trade-offs: 8x vs 16x](#trade-offs-8x-vs-16x)
8. [Programming Examples in C/C++](#programming-examples-in-cc)
   - [STM32 HAL – 16x vs 8x Configuration](#stm32-hal--16x-vs-8x-configuration)
   - [Direct Register Access (STM32)](#direct-register-access-stm32)
   - [AVR / Software Bit-Bang Oversampling](#avr--software-bit-bang-oversampling)
   - [Majority Vote Filter in C](#majority-vote-filter-in-c)
9. [Programming Examples in Rust](#programming-examples-in-rust)
   - [STM32 with `stm32f4xx-hal` – Oversampling8](#stm32-with-stm32f4xx-hal--oversampling8)
   - [Software Oversampling Receiver in Rust (no_std)](#software-oversampling-receiver-in-rust-no_std)
   - [Majority Vote in Rust](#majority-vote-in-rust)
10. [Practical Configuration Checklist](#practical-configuration-checklist)
11. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is a serial communication protocol that
operates without a shared clock between transmitter and receiver. Because there is no clock
line, the receiver must internally reconstruct the timing of each bit from the incoming data
stream alone. The mechanism it uses to do this reliably — especially in the presence of
electrical noise — is called **oversampling**.

Oversampling is built into virtually every modern UART hardware peripheral (STM32, NXP
Kinetis, Nordic nRF5x, ESP32, etc.) and is a fundamental concept for engineers working at
low baud rates on noisy lines, long cable runs, or in electromagnetically challenging
environments.

---

## What Is Oversampling?

In standard UART, both sides agree on a baud rate (e.g., 9600 baud = one bit every ~104 µs).
The receiver does **not** sample a bit just once — it samples it **multiple times per bit
period** and derives the logical value from those samples. The number of samples taken per
bit period is the **oversampling ratio**.

The two most common ratios are:

| Ratio | Samples per bit | Common name       |
|-------|-----------------|-------------------|
| 16x   | 16              | OVER8 = 0 (STM32) |
| 8x    | 8               | OVER8 = 1 (STM32) |

Some software-only or specialized hardware implementations also support 3x or 32x, but 8x
and 16x are the industry standard.

---

## Why Oversample?

UART lines are asynchronous — the receiver clock and the transmitter clock are never perfectly
aligned. They run at nominally the same frequency, but small manufacturing tolerances and
thermal drift cause them to diverge over time. Oversampling serves two purposes:

### 1. Bit-Centre Detection

At the start of each frame, the receiver detects the falling edge of the **start bit**. It
then counts a precise number of oversampling ticks to land at the **centre** of each
subsequent bit, where the signal is most stable and furthest from any transition edges.

```
Bit period (16x example):
Tick:  0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15
       |--start bit--|------data bit 0--------|...
                          ^ sample at tick 7/8/9
```

### 2. Noise Rejection via Majority Voting

Rather than trusting a single sample, the hardware takes **three consecutive samples** near
the bit centre and applies a majority vote. A single glitch (spike, EMI pulse, ground bounce)
that corrupts one of the three samples is outvoted by the other two, and the correct bit value
is recovered.

---

## How Oversampling Works

### 16x Oversampling

Each bit period is divided into 16 equal time slices. The UART clock runs at
`16 × baud_rate`. After synchronising to the start bit edge:

- Samples are taken at ticks **7, 8, and 9** (the middle third of the bit period).
- Majority vote: if at least 2 of 3 samples are HIGH, the bit is 1; otherwise 0.
- Error flags (`NF` — noise flag) are set if all three samples are not identical,
  alerting software that the line is noisy.

```
16x bit window:

 0  1  2  3  4  5  6 [7  8  9] 10 11 12 13 14 15
                      ^  ^  ^
                      Sample points (majority vote)
```

This gives the receiver ±4 ticks (25% of the bit period) of timing margin on either side of
the ideal sample window before an error occurs.

### 8x Oversampling

Each bit period is divided into 8 equal time slices. The UART clock runs at
`8 × baud_rate`. Samples are taken at ticks **3, 4, and 5**.

```
8x bit window:

 0  1  2 [3  4  5]  6  7
           ^  ^  ^
           Sample points
```

The timing margin is now ±2 ticks (25% of bit period) — numerically the same fraction, but
the absolute time window is half as wide, so the baud rate clock must be more accurate.
The benefit is that the UART clock only needs to be **8× the baud rate**, allowing higher
baud rates from a given peripheral clock.

---

## Baud Rate Generation with Oversampling

The baud rate divider (BRR register on STM32, for example) is calculated differently
depending on the oversampling ratio:

### 16x (default):

```
USARTDIV = f_CK / (16 × baud_rate)
BRR      = USARTDIV  (integer + 4-bit fraction)
```

### 8x:

```
USARTDIV = f_CK / (8 × baud_rate)
BRR[3:0] = (USARTDIV[3:0] << 1) & 0xF   // fractional part shifted
BRR[15:4] = USARTDIV[15:4]               // integer part unchanged
```

**Example — STM32, f_CK = 84 MHz, baud rate = 115200:**

| Oversampling | USARTDIV   | BRR value | Actual baud  | Error  |
|--------------|------------|-----------|--------------|--------|
| 16x          | 45.5729    | 0x2D9     | 115107       | 0.08%  |
| 8x           | 22.7865    | 0x16C     | 115107       | 0.08%  |

At lower clock speeds (e.g., 8 MHz), 8x allows reaching baud rates that would require
a fractional divider below 1.0 with 16x, which some peripherals cannot represent.

---

## Majority Voting / Noise Filtering

The majority vote logic for 3 samples `s7`, `s8`, `s9` (16x) can be expressed as:

```
bit_value = (s7 & s8) | (s8 & s9) | (s7 & s9)
noise     = !(s7 == s8 == s9)
```

This is a standard 2-of-3 majority gate. The noise flag does **not** discard the byte; it
simply marks it as potentially unreliable so the application can decide what to do
(log, retry, increment error counter, etc.).

In hardware, the NF (Noise Flag) bit in the UART status register is set whenever any of
the three sample points disagree, even if the majority vote produced the correct value.

---

## Programming Examples in C/C++

### STM32 HAL – 16x vs 8x Configuration

The STM32 HAL abstracts oversampling through the `OverSampling` field of
`UART_InitTypeDef`. This is the most portable approach across STM32 families.

```c
#include "stm32f4xx_hal.h"

UART_HandleTypeDef huart2;

/**
 * @brief  Configure USART2 with 16x oversampling (default, best noise immunity).
 *         PA2 = TX, PA3 = RX, 115200 baud, 84 MHz peripheral clock.
 */
void UART_Init_16x(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;   // <-- 16x

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief  Configure USART2 with 8x oversampling.
 *         Use when higher baud rates are needed from a low peripheral clock,
 *         or when the clock source has better-than-average accuracy.
 */
void UART_Init_8x(void)
{
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 115200;
    huart2.Init.WordLength   = UART_WORDLENGTH_8B;
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_NONE;
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_8;    // <-- 8x

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        Error_Handler();
    }
}

/**
 * @brief  Check if a noise error occurred on the last received byte.
 *         The NF (Noise Flag) is part of the status register.
 */
void UART_CheckNoiseFlag(UART_HandleTypeDef *huart)
{
    if (__HAL_UART_GET_FLAG(huart, UART_FLAG_NE)) {
        // Noise detected — majority vote may still have recovered the byte,
        // but the application should track this for diagnostics.
        __HAL_UART_CLEAR_NEFLAG(huart);
        // Increment noise error counter, log event, etc.
    }
}
```

---

### Direct Register Access (STM32)

For bare-metal code without HAL, the OVER8 bit is in `CR1`:

```c
#include "stm32f4xx.h"

#define PCLK1_HZ    42000000UL   // APB1 clock (USART2 is on APB1)
#define BAUD_RATE   115200UL

/**
 * @brief  Initialize USART2 at the register level with selectable oversampling.
 * @param  use_8x  Set 1 for 8x oversampling, 0 for 16x.
 */
void USART2_Init_Reg(uint8_t use_8x)
{
    /* Enable clocks */
    RCC->APB1ENR |= RCC_APB1ENR_USART2EN;
    RCC->AHB1ENR |= RCC_AHB1ENR_GPIOAEN;

    /* Configure PA2 (TX) and PA3 (RX) as AF7 */
    GPIOA->MODER   &= ~(0xF << 4);
    GPIOA->MODER   |=  (0xA << 4);   // Alternate function
    GPIOA->AFR[0]  |=  (7 << 8) | (7 << 12);  // AF7 = USART2

    /* Disable USART before configuring */
    USART2->CR1 = 0;

    if (use_8x) {
        /* 8x oversampling: set OVER8 bit */
        USART2->CR1 |= USART_CR1_OVER8;

        /*
         * BRR for 8x:
         *   USARTDIV = PCLK / (8 * BAUD) = 42000000 / 921600 = 45.57
         *   Integer part  = 45         → bits [15:4] = 45
         *   Fractional    = 0.57 * 8   ≈ 5 → bits [2:0] = 5
         *   BRR = (45 << 4) | 5 = 0x2D5
         *   NOTE: bit 3 of BRR must be 0 in OVER8 mode
         */
        uint32_t usartdiv = (PCLK1_HZ * 2) / BAUD_RATE;  // *2 for rounding
        uint32_t frac     = usartdiv & 0xF;
        uint32_t mantissa = usartdiv >> 4;
        frac = (frac + 1) >> 1;  // adjust fractional for 8x
        if (frac >= 8) { mantissa++; frac = 0; }
        USART2->BRR = (mantissa << 4) | (frac & 0x7);
    } else {
        /* 16x oversampling (default): OVER8 = 0 */
        USART2->CR1 &= ~USART_CR1_OVER8;

        /*
         * BRR for 16x:
         *   USARTDIV = PCLK / (16 * BAUD) = 42000000 / 1843200 = 22.786
         *   Integer part  = 22         → bits [15:4] = 22
         *   Fractional    = 0.786 * 16 ≈ 13 → bits [3:0] = 13
         *   BRR = (22 << 4) | 13 = 0x16D
         */
        uint32_t usartdiv = (PCLK1_HZ + BAUD_RATE / 2) / BAUD_RATE;
        USART2->BRR = usartdiv;
    }

    /* 8N1, TX+RX enable, UART enable */
    USART2->CR2 = 0;
    USART2->CR3 = 0;
    USART2->CR1 |= USART_CR1_TE | USART_CR1_RE | USART_CR1_UE;
}

/**
 * @brief  Read status register and check noise flag.
 * @return 1 if noise was detected on the last received byte, 0 otherwise.
 */
uint8_t USART2_NoiseFlag(void)
{
    uint32_t sr = USART2->SR;
    if (sr & USART_SR_NE) {
        /* Reading DR clears the NE flag */
        (void)USART2->DR;
        return 1;
    }
    return 0;
}
```

---

### AVR / Software Bit-Bang Oversampling

On microcontrollers without hardware UART oversampling (or when implementing a software
UART), you can implement 8x oversampling manually using a timer ISR. The timer fires at
`8 × baud_rate` and accumulates samples:

```c
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>
#include <stdbool.h>

#define F_CPU           16000000UL
#define BAUD            9600UL
#define OVERSAMPLE      8
#define TIMER_FREQ      (BAUD * OVERSAMPLE)   // 76800 Hz

/* Timer TOP value for CTC mode */
#define TIMER_TOP       ((F_CPU / TIMER_FREQ) - 1)   // = 207

typedef enum {
    RX_IDLE,
    RX_START,
    RX_DATA,
    RX_STOP
} rx_state_t;

static volatile rx_state_t rx_state = RX_IDLE;
static volatile uint8_t    rx_tick  = 0;   // sub-bit sample counter (0..7)
static volatile uint8_t    rx_bit   = 0;   // bit index within byte (0..7)
static volatile uint8_t    rx_shift = 0;   // shift register
static volatile uint8_t    sample_buf[3];  // three consecutive samples for majority vote
static volatile uint8_t    rx_byte  = 0;
static volatile bool       rx_ready = false;

/* Apply 2-of-3 majority vote to three single-bit samples */
static inline uint8_t majority_vote(uint8_t a, uint8_t b, uint8_t c)
{
    return (a & b) | (b & c) | (a & c);
}

/* Read the RX pin (adjust port/pin for your hardware) */
static inline uint8_t read_rx_pin(void)
{
    return (PIND >> PD0) & 0x01;
}

/**
 * @brief  Timer ISR fires at 8× baud rate.
 *         Implements the state machine for software oversampled UART RX.
 */
ISR(TIMER0_COMPA_vect)
{
    uint8_t pin = read_rx_pin();

    switch (rx_state) {

    case RX_IDLE:
        /* Wait for falling edge (start bit) */
        if (!pin) {
            rx_state = RX_START;
            rx_tick  = 0;
        }
        break;

    case RX_START:
        rx_tick++;
        if (rx_tick == 4) {
            /* Centre of start bit — verify it is still LOW */
            if (!pin) {
                rx_state = RX_DATA;
                rx_tick  = 0;
                rx_bit   = 0;
                rx_shift = 0;
            } else {
                /* False start — glitch on the line */
                rx_state = RX_IDLE;
            }
        }
        break;

    case RX_DATA:
        rx_tick++;
        /* Collect three samples around the bit centre (ticks 3, 4, 5 in 8x) */
        if (rx_tick == 3) sample_buf[0] = pin;
        if (rx_tick == 4) sample_buf[1] = pin;
        if (rx_tick == 5) sample_buf[2] = pin;

        if (rx_tick == 8) {
            /* End of bit period — apply majority vote */
            uint8_t bit_val = majority_vote(sample_buf[0],
                                            sample_buf[1],
                                            sample_buf[2]);
            rx_shift |= (bit_val << rx_bit);
            rx_bit++;
            rx_tick = 0;

            if (rx_bit == 8) {
                rx_state = RX_STOP;
            }
        }
        break;

    case RX_STOP:
        rx_tick++;
        if (rx_tick == 8) {
            /* Verify stop bit is HIGH */
            if (pin) {
                rx_byte  = rx_shift;
                rx_ready = true;
            }
            rx_state = RX_IDLE;
            rx_tick  = 0;
        }
        break;
    }
}

void SoftUART_Init(void)
{
    /* Configure Timer0 CTC mode at 8× baud rate */
    TCCR0A = (1 << WGM01);             // CTC mode
    TCCR0B = (1 << CS00);              // No prescaler
    OCR0A  = (uint8_t)TIMER_TOP;
    TIMSK0 = (1 << OCIE0A);            // Enable compare match interrupt

    /* RX pin as input with pull-up */
    DDRD  &= ~(1 << PD0);
    PORTD |=  (1 << PD0);

    sei();
}

bool SoftUART_Available(void)
{
    return rx_ready;
}

uint8_t SoftUART_Read(void)
{
    rx_ready = false;
    return rx_byte;
}
```

---

### Majority Vote Filter in C

A standalone, reusable majority vote filter useful for post-processing sampled bits in
software or for validating ADC readings:

```c
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  2-of-3 majority vote on three single-bit samples.
 * @param  s0,s1,s2  Single-bit samples (0 or 1).
 * @return  Majority value (0 or 1).
 */
uint8_t majority_vote_3(uint8_t s0, uint8_t s1, uint8_t s2)
{
    return (s0 & s1) | (s1 & s2) | (s0 & s2);
}

/**
 * @brief  Sliding window majority vote over N samples.
 *         Returns 1 if more than half the samples are HIGH.
 * @param  samples   Array of single-bit samples.
 * @param  count     Number of samples (should be odd for tie-breaking).
 * @return  1 if majority are 1, 0 otherwise.
 */
uint8_t majority_vote_n(const uint8_t *samples, uint8_t count)
{
    uint8_t ones = 0;
    for (uint8_t i = 0; i < count; i++) {
        ones += samples[i] & 0x01;
    }
    return (ones > count / 2) ? 1 : 0;
}

/**
 * @brief  Decode a full UART byte from a raw oversampled bit stream.
 *         Assumes the stream starts at the falling edge of the start bit.
 *
 * @param  stream       Bit array: 1 sample per entry (0 or 1).
 * @param  oversample   Oversampling ratio (8 or 16).
 * @param  out_byte     Decoded byte (output).
 * @return  true on success (valid start/stop bits), false on framing error.
 */
bool uart_decode_oversampled(const uint8_t *stream,
                              uint8_t        oversample,
                              uint8_t       *out_byte)
{
    uint8_t half = oversample / 2;
    uint8_t byte_val = 0;

    /* Verify start bit (should be 0 at the centre) */
    uint8_t start_samples[3];
    start_samples[0] = stream[half - 1];
    start_samples[1] = stream[half];
    start_samples[2] = stream[half + 1];
    if (majority_vote_3(start_samples[0], start_samples[1], start_samples[2]) != 0) {
        return false;   /* Framing error — start bit not LOW */
    }

    /* Decode 8 data bits */
    for (uint8_t bit = 0; bit < 8; bit++) {
        uint32_t centre = (uint32_t)oversample * (bit + 1) + half;
        uint8_t s[3] = {
            stream[centre - 1],
            stream[centre],
            stream[centre + 1]
        };
        uint8_t bit_val = majority_vote_3(s[0], s[1], s[2]);
        byte_val |= (bit_val << bit);
    }

    /* Verify stop bit (should be 1) */
    uint32_t stop_centre = (uint32_t)oversample * 9 + half;
    uint8_t stop_s[3] = {
        stream[stop_centre - 1],
        stream[stop_centre],
        stream[stop_centre + 1]
    };
    if (majority_vote_3(stop_s[0], stop_s[1], stop_s[2]) != 1) {
        return false;   /* Framing error — stop bit not HIGH */
    }

    *out_byte = byte_val;
    return true;
}
```

---

## Programming Examples in Rust

### STM32 with `stm32f4xx-hal` – Oversampling8

The `stm32f4xx-hal` crate exposes oversampling configuration through a builder pattern:

```toml
# Cargo.toml
[dependencies]
stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
cortex-m      = "0.7"
cortex-m-rt   = "0.7"
```

```rust
//! UART with 8x oversampling on STM32F4 using stm32f4xx-hal.
//!
//! Connects USART2 to PA2 (TX) / PA3 (RX) at 115_200 baud.

#![no_std]
#![no_main]

use cortex_m_rt::entry;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::{Config, Oversampling, StopBits, WordLength}, Serial},
};

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();

    // Configure system clocks
    let rcc   = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate();
    let rx_pin = gpioa.pa3.into_alternate();

    // Build UART config with 8x oversampling
    let config_8x = Config::default()
        .baudrate(115_200.bps())
        .wordlength(WordLength::DataBits8)
        .stopbits(StopBits::STOP1)
        .oversampling(Oversampling::Over8);   // <-- 8x oversampling

    let mut serial = Serial::new(
        dp.USART2,
        (tx_pin, rx_pin),
        config_8x,
        &clocks,
    )
    .unwrap();

    // Build UART config with 16x oversampling (better noise immunity)
    // Just change .oversampling(Oversampling::Over16)

    // Transmit a test byte
    serial.bwrite_all(b"Hello, oversampled UART!\r\n").ok();

    loop {
        // Check for noise flag via the status register
        if serial.is_rx_not_empty() {
            match serial.read() {
                Ok(byte) => {
                    // Echo received byte
                    serial.write(byte).ok();
                }
                Err(stm32f4xx_hal::serial::Error::Noise) => {
                    // Majority vote recovered the byte but noise was detected
                    serial.bwrite_all(b"[NOISE]\r\n").ok();
                }
                Err(stm32f4xx_hal::serial::Error::Framing) => {
                    serial.bwrite_all(b"[FRAMING_ERR]\r\n").ok();
                }
                Err(_) => {}
            }
        }
    }
}
```

---

### Software Oversampling Receiver in Rust (no_std)

A `no_std` software UART receiver implementing 8x oversampling using a timer interrupt,
suitable for microcontrollers without hardware UART or when bit-banging extra UART channels:

```rust
//! Software UART RX with 8x oversampling in Rust (no_std).
//!
//! The caller must drive `tick()` from a timer ISR at 8× the desired baud rate.

#![no_std]

use core::sync::atomic::{AtomicBool, AtomicU8, Ordering};

/// 2-of-3 majority vote on single-bit samples.
#[inline(always)]
fn majority3(a: u8, b: u8, c: u8) -> u8 {
    (a & b) | (b & c) | (a & c)
}

#[derive(Clone, Copy, PartialEq)]
enum RxState {
    Idle,
    Start,
    Data,
    Stop,
}

/// Software UART receiver state, suitable for use in interrupt context.
pub struct SoftUartRx {
    state:      RxState,
    tick:       u8,         // sub-bit tick counter (0..oversample-1)
    bit_index:  u8,         // current data bit index (0..7)
    shift_reg:  u8,         // accumulates decoded bits
    samples:    [u8; 3],    // three samples for majority vote
    rx_byte:    u8,
    rx_ready:   bool,
    noise_flag: bool,
}

impl SoftUartRx {
    pub const fn new() -> Self {
        Self {
            state:      RxState::Idle,
            tick:       0,
            bit_index:  0,
            shift_reg:  0,
            samples:    [0; 3],
            rx_byte:    0,
            rx_ready:   false,
            noise_flag: false,
        }
    }

    /// Call this from a timer ISR at exactly 8× baud rate.
    /// `pin_high` is the current logic level of the RX pin.
    pub fn tick(&mut self, pin_high: bool) {
        let pin: u8 = pin_high as u8;

        match self.state {
            RxState::Idle => {
                if !pin_high {
                    // Falling edge detected — potential start bit
                    self.state = RxState::Start;
                    self.tick  = 0;
                }
            }

            RxState::Start => {
                self.tick += 1;
                if self.tick == 4 {
                    // Centre of start bit
                    if !pin_high {
                        // Valid start bit confirmed
                        self.state     = RxState::Data;
                        self.tick      = 0;
                        self.bit_index = 0;
                        self.shift_reg = 0;
                        self.noise_flag = false;
                    } else {
                        // Glitch — false start
                        self.state = RxState::Idle;
                    }
                }
            }

            RxState::Data => {
                self.tick += 1;

                // Collect three samples at ticks 3, 4, 5 (centre ±1)
                if self.tick == 3 { self.samples[0] = pin; }
                if self.tick == 4 { self.samples[1] = pin; }
                if self.tick == 5 { self.samples[2] = pin; }

                if self.tick == 8 {
                    let voted = majority3(
                        self.samples[0],
                        self.samples[1],
                        self.samples[2],
                    );
                    // Check for noise (not all samples agree)
                    if self.samples[0] != self.samples[1]
                        || self.samples[1] != self.samples[2]
                    {
                        self.noise_flag = true;
                    }
                    self.shift_reg |= voted << self.bit_index;
                    self.bit_index += 1;
                    self.tick = 0;

                    if self.bit_index == 8 {
                        self.state = RxState::Stop;
                    }
                }
            }

            RxState::Stop => {
                self.tick += 1;
                if self.tick == 8 {
                    if pin_high {
                        // Valid stop bit
                        self.rx_byte  = self.shift_reg;
                        self.rx_ready = true;
                    }
                    // If stop bit is LOW: framing error — discard byte
                    self.state = RxState::Idle;
                    self.tick  = 0;
                }
            }
        }
    }

    /// Returns `Some(byte)` if a complete byte has been received.
    pub fn read(&mut self) -> Option<u8> {
        if self.rx_ready {
            self.rx_ready = false;
            Some(self.rx_byte)
        } else {
            None
        }
    }

    /// Returns true if noise was detected during the last received byte.
    pub fn noise_detected(&self) -> bool {
        self.noise_flag
    }
}
```

---

### Majority Vote in Rust

A generic, const-capable majority vote implementation useful in DSP pipelines or
soft-decision decoding:

```rust
/// 2-of-3 majority vote on `bool` samples.
#[inline(always)]
pub fn majority3_bool(a: bool, b: bool, c: bool) -> bool {
    (a && b) || (b && c) || (a && c)
}

/// N-of-M majority vote over a slice of bits (0 or 1 as u8).
/// Returns `true` if strictly more than half the samples are 1.
pub fn majority_n(samples: &[u8]) -> bool {
    let ones: usize = samples.iter().map(|&s| (s & 1) as usize).sum();
    ones * 2 > samples.len()
}

/// Decode a UART byte from a raw oversampled bit buffer.
///
/// # Arguments
/// * `stream`     – Slice of single-bit samples starting at the start-bit edge.
/// * `oversample` – Oversampling ratio (8 or 16).
///
/// # Returns
/// * `Ok(byte)`  on successful decode.
/// * `Err(msg)`  on framing error.
pub fn uart_decode(stream: &[u8], oversample: usize) -> Result<u8, &'static str> {
    let half = oversample / 2;

    // Validate start bit
    let start = majority3_bool(
        stream[half - 1] != 0,
        stream[half]     != 0,
        stream[half + 1] != 0,
    );
    if start {
        return Err("framing: start bit not LOW");
    }

    // Decode 8 data bits
    let mut byte_val: u8 = 0;
    for bit in 0..8usize {
        let centre = oversample * (bit + 1) + half;
        let voted = majority3_bool(
            stream[centre - 1] != 0,
            stream[centre]     != 0,
            stream[centre + 1] != 0,
        );
        if voted {
            byte_val |= 1 << bit;
        }
    }

    // Validate stop bit
    let stop_centre = oversample * 9 + half;
    let stop = majority3_bool(
        stream[stop_centre - 1] != 0,
        stream[stop_centre]     != 0,
        stream[stop_centre + 1] != 0,
    );
    if !stop {
        return Err("framing: stop bit not HIGH");
    }

    Ok(byte_val)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_majority_clean() {
        assert_eq!(majority3_bool(true,  true,  true),  true);
        assert_eq!(majority3_bool(false, false, false), false);
        assert_eq!(majority3_bool(true,  true,  false), true);   // noise on s2
        assert_eq!(majority3_bool(false, true,  false), false);  // noise on s1
    }

    #[test]
    fn test_majority_n_odd() {
        assert!(majority_n(&[1, 1, 0, 1, 0]));   // 3 of 5
        assert!(!majority_n(&[1, 0, 0, 0, 1]));  // 2 of 5
    }
}
```

---

## Practical Configuration Checklist

When configuring UART oversampling on a real project, work through this checklist:

**Clock accuracy:**
- Use an HSE (external crystal) rather than HSI (internal RC) when using 8x oversampling.
  Internal oscillators on STM32 have ±1–2% tolerance; 8x oversampling allows only ±3.5%
  total error budget between TX and RX clocks combined.
- With 16x oversampling the total budget relaxes to ±5%, so HSI is often acceptable below
  57600 baud.

**Baud rate selection:**
- Prefer baud rates that produce low BRR error (< 0.5%) at your peripheral clock.
- Common issue: at 8 MHz APB with 115200 baud and 16x oversampling, USARTDIV = 4.34 →
  actual baud = 119047 → 3.3% error. Switch to 8x (USARTDIV = 8.68 → lower error) or
  choose a lower baud rate.

**Noise flag monitoring:**
- Always read and count the NF bit in production firmware. A rising noise error rate signals
  a hardware problem (poor grounding, long unshielded cable, missing bypass capacitor) before
  it becomes a hard communication failure.

**High-baud / low-clock combinations:**
- If `f_CK / (16 × baud) < 1`, 16x oversampling is impossible. Switch to 8x or increase
  the peripheral clock. The STM32 HAL will return `HAL_ERROR` and set an error code if the
  divider is out of range.

---

## Summary

UART oversampling is the mechanism by which an asynchronous receiver achieves reliable
bit detection without a shared clock. Instead of sampling each bit exactly once,
the receiver samples it multiple times per bit period — 16 times at 16x oversampling,
or 8 times at 8x — and applies a 2-of-3 majority vote at the three central sample points
to recover the correct bit value even in the presence of short electrical noise bursts.

**16x oversampling** provides the widest timing margin (±4 ticks, equivalent to 25% of the
bit period) and the best noise rejection, making it the correct choice for noisy environments,
low-accuracy clocks, long cable runs, and any safety-critical application. It is the default
on virtually all modern UART peripherals.

**8x oversampling** halves the oversampling clock requirement, enabling higher baud rates
from a given peripheral clock. The timing margin remains proportionally the same (25%) but
the absolute window is narrower, demanding better clock accuracy. It is appropriate for
short, well-shielded PCB traces, high-quality oscillators, or high-baud-rate scenarios where
16x would require an impractically fast oversampling clock.

In software UARTs or in firmware post-processing pipelines, the same principle applies:
sample at a multiple of the baud rate (typically 8x), collect three samples near the bit
centre, and apply a majority vote. The `noise flag` — whether from hardware status registers
or software disagreement detection — is a valuable diagnostic that should always be monitored
in production systems to catch signal integrity problems early.

| Feature              | 16x Oversampling          | 8x Oversampling            |
|----------------------|---------------------------|----------------------------|
| Samples per bit      | 16                        | 8                          |
| Vote window          | Ticks 7, 8, 9             | Ticks 3, 4, 5              |
| Timing margin        | ±25% (wider absolute)     | ±25% (narrower absolute)   |
| Noise immunity       | Higher                    | Moderate                   |
| Max baud rate        | Lower for a given f_CK    | Higher for a given f_CK    |
| Clock accuracy req.  | Relaxed (~±5%)            | Tighter (~±3.5%)           |
| Typical use case     | Noisy lines, long cables  | High-speed, short PCB runs |
| STM32 register bit   | OVER8 = 0                 | OVER8 = 1                  |

The majority vote algorithm — `bit = (s0 & s1) | (s1 & s2) | (s0 & s2)` — is one of the
simplest and most effective noise-rejection techniques in digital communications, and
understanding it is key to writing robust UART drivers at any level of the stack.

---

*Document: 71_Oversampling_Techniques.md | Topic: UART Oversampling for Noise Immunity*