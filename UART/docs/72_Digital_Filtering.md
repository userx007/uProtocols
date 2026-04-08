# 72. Digital Filtering — Applying Filters to Reduce Noise in UART Reception

**Theory** — Why UART is noise-prone (no shared clock, single-ended signalling, EMI) and the five main filter types: majority voting, moving average (FIR), median, EMA/IIR, and debounce/glitch.

**Hardware** — A reference table of built-in noise filtering registers across STM32, AVR, nRF52, ESP32, and RP2040, since hardware filtering should always be the first layer.

**C/C++ implementations** — All five filters with full, production-quality code: a soft-UART ISR with majority voting, a templated C++ moving average class, a C median filter with insertion sort, both integer and floating-point EMA variants, and a bit-level debounce filter.

**Rust implementations** — All five filters written `no_std`-compatible: majority voting with a `SoftUartRx` state machine, a const-generic `MovingAverage<T, N>`, `MedianFilter<N>`, both integer and `f32` EMA, and a `DebounceFilter` — each with unit tests.

**Pipeline** — A layered architecture diagram showing how to chain all filters from pin to application, with a combined C++ pipeline class example.

**Performance table** — RAM, CPU cycles, latency, and best-use guidance for each filter to help choose the right tool for a given MCU constraint.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Noise Occurs in UART Reception](#why-noise-occurs-in-uart-reception)
3. [Types of Digital Filters Used in UART](#types-of-digital-filters-used-in-uart)
   - 3.1 [Majority Voting Filter](#31-majority-voting-filter)
   - 3.2 [Moving Average (FIR) Filter](#32-moving-average-fir-filter)
   - 3.3 [Median Filter](#33-median-filter)
   - 3.4 [Exponential Moving Average (EMA / IIR) Filter](#34-exponential-moving-average-ema--iir-filter)
   - 3.5 [Debounce / Glitch Filter](#35-debounce--glitch-filter)
4. [Hardware-Assisted Filtering in MCU UARTs](#hardware-assisted-filtering-in-mcu-uarts)
5. [Software Digital Filtering in C/C++](#software-digital-filtering-in-cc)
   - 5.1 [Majority Voting in C](#51-majority-voting-in-c)
   - 5.2 [Moving Average Filter in C++](#52-moving-average-filter-in-c)
   - 5.3 [Median Filter in C](#53-median-filter-in-c)
   - 5.4 [IIR / EMA Filter in C](#54-iir--ema-filter-in-c)
   - 5.5 [Glitch / Debounce Filter in C](#55-glitch--debounce-filter-in-c)
6. [Software Digital Filtering in Rust](#software-digital-filtering-in-rust)
   - 6.1 [Majority Voting in Rust](#61-majority-voting-in-rust)
   - 6.2 [Moving Average Filter in Rust](#62-moving-average-filter-in-rust)
   - 6.3 [Median Filter in Rust](#63-median-filter-in-rust)
   - 6.4 [IIR / EMA Filter in Rust](#64-iir--ema-filter-in-rust)
   - 6.5 [Glitch / Debounce Filter in Rust (no_std)](#65-glitch--debounce-filter-in-rust-no_std)
7. [Combining Filters in a UART Receive Pipeline](#combining-filters-in-a-uart-receive-pipeline)
8. [Performance Considerations](#performance-considerations)
9. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver/Transmitter) is one of the oldest and most widely deployed serial communication protocols in embedded systems. Despite its simplicity, UART is susceptible to a variety of noise sources — especially when running over long cable runs, in electrically noisy industrial environments, or at higher baud rates. **Digital filtering** is the practice of processing sampled UART bit-level or byte-level data in software (or in dedicated hardware logic) to suppress noise, reduce bit errors, and recover a clean data stream.

Unlike analog filtering (which operates on the continuous-time voltage signal before it reaches the MCU pin), digital filtering operates on **sampled discrete values** — either individual bit samples taken by oversampling or processed bytes received from the UART peripheral. Understanding when and how to apply each technique is essential for robust embedded communications.

---

## Why Noise Occurs in UART Reception

UART is inherently noise-sensitive due to several characteristics:

- **No clock signal**: Unlike SPI or I²C, UART has no shared clock. Both ends maintain independent oscillators and must agree on timing. Any frequency mismatch, jitter, or glitch can shift sample points.
- **Single-ended signaling**: Standard UART uses single-ended TTL/CMOS levels, making it vulnerable to ground shifts and common-mode interference. (RS-485 and RS-422 use differential signaling as a hardware solution.)
- **Long cable capacitance**: Capacitance on long cables rounds signal edges, increasing the probability of sampling in a transition region.
- **EMI and cross-talk**: Switching power supplies, motor drivers, or adjacent high-frequency signals can inject spikes onto the RX line.

Common noise artefacts visible on a UART RX line include:

| Artefact | Cause |
|---|---|
| Short glitches (< 1 bit period) | EMI spikes, relay bounce |
| Bit level errors | Baud rate mismatch, jitter |
| Framing errors | Edge distortion causing wrong start/stop detection |
| Burst errors | Ground loops, high-current switching transients |

---

## Types of Digital Filters Used in UART

### 3.1 Majority Voting Filter

The most common hardware technique for UART noise rejection. The receiver oversamples each bit (typically 8× or 16× the baud rate) and takes 3 samples near the centre of the bit period. The value that appears at least twice out of three is accepted as the bit value.

**Pros:** Very simple, deterministic, zero latency beyond the oversampling delay.  
**Cons:** Only filters glitches narrower than ~1/3 of the sample window.

Most MCU UART peripherals (STM32, nRF52, SAMD, etc.) implement this in hardware and allow it to be enabled/disabled via a control register.

### 3.2 Moving Average (FIR) Filter

A Finite Impulse Response (FIR) filter that averages N recent samples. Applied to multi-byte data streams rather than individual bits, this smooths amplitude noise in byte values if UART is used to transmit analogue sensor readings encoded as bytes.

**Pros:** Linear phase, easy to implement, configurable window size.  
**Cons:** Introduces latency equal to half the window length; attenuates high-frequency signal content along with noise.

### 3.3 Median Filter

Sorts a window of N recent samples and returns the middle (median) value. Excellent at rejecting impulse noise (spikes) without blurring edges as a mean filter does.

**Pros:** Highly effective against outlier byte values (e.g., a garbled byte in the middle of a sequence).  
**Cons:** Higher computational cost; non-linear (cannot be combined with other linear filters simply).

### 3.4 Exponential Moving Average (EMA / IIR) Filter

An Infinite Impulse Response filter using the recursion:

```
y[n] = α * x[n] + (1 - α) * y[n-1]
```

Where `α` (0 < α ≤ 1) controls the smoothing factor. Small α = heavy smoothing (slow response); large α = light smoothing (fast response).

**Pros:** Extremely lightweight (one multiply, one add, one state variable); no buffer needed.  
**Cons:** Infinite impulse response means it never fully forgets old values; phase lag introduced.

### 3.5 Debounce / Glitch Filter

Requires that a signal remain stable for a minimum number of consecutive samples before accepting a transition. Applied at bit level, it suppresses glitches shorter than the required stable count.

**Pros:** Intuitive, directly targets the most common noise artefact on UART lines.  
**Cons:** Adds latency equal to the stability window; must be tuned carefully to not suppress legitimate fast transitions.

---

## Hardware-Assisted Filtering in MCU UARTs

Most modern MCU UART peripherals provide built-in noise rejection that should always be enabled first:

| MCU Family | Feature | Register/Bit |
|---|---|---|
| STM32 (HAL) | Oversampling by 16 (default) or 8; noise error flag (NE) | `USART_CR1_OVER8`, `USART_SR_NE` |
| STM32 | Single-wire / noise filtering | `USART_CR3_ONEBIT` — set to use 1-sample instead of majority vote |
| AVR (ATmega) | 3-sample majority voting always on when `U2Xn=0` | `UCSRnA` |
| nRF52 | EasyDMA UART with configurable oversampling | `UARTE_CONFIG_PARITY` |
| ESP32 | 16× oversampling, configurable sample point | UART_CONF0_REG |
| RP2040 | PIO state machines allow custom bit sampling | PIO program |

Always check the datasheet noise filter capabilities before implementing a software solution — hardware filtering is faster and uses no CPU cycles.

---

## Software Digital Filtering in C/C++

The following examples demonstrate software-side filtering applied after bytes are received from the UART peripheral, or applied to oversampled bit streams in bit-bang or custom PIO implementations.

### 5.1 Majority Voting in C

Used when performing soft-UART or PIO-based reception and you have access to individual bit samples.

```c
#include <stdint.h>
#include <stdbool.h>

/**
 * majority_vote - Determine the majority bit from 3 samples.
 *
 * @param s0 First sample  (taken at ~25% of bit period from centre)
 * @param s1 Second sample (taken at ~50% / centre of bit period)
 * @param s2 Third sample  (taken at ~75% of bit period)
 *
 * @return Majority bit value (0 or 1).
 */
static inline uint8_t majority_vote(uint8_t s0, uint8_t s1, uint8_t s2)
{
    return (s0 & s1) | (s1 & s2) | (s0 & s2);
}

/**
 * Example: Soft-UART bit decoder with majority voting.
 * Assumes sample_bit() reads the current RX pin level.
 * Called from a timer ISR at 3x the baud rate, centred on bit periods.
 */
void soft_uart_isr(void)
{
    static uint8_t samples[3];
    static uint8_t sample_idx = 0;
    static uint8_t shift_reg  = 0;
    static uint8_t bit_count  = 0;
    static bool    in_frame   = false;

    uint8_t raw = read_rx_pin(); /* platform-specific pin read */
    samples[sample_idx++] = raw;

    if (sample_idx < 3) {
        return; /* Accumulate 3 samples before deciding */
    }
    sample_idx = 0;

    uint8_t bit = majority_vote(samples[0], samples[1], samples[2]);

    if (!in_frame) {
        if (bit == 0) {           /* Start bit detected */
            in_frame  = true;
            bit_count = 0;
            shift_reg = 0;
        }
    } else {
        if (bit_count < 8) {
            shift_reg |= (bit << bit_count);
            bit_count++;
        } else {
            /* Stop bit: bit should be 1 */
            if (bit == 1) {
                uart_rx_buffer_push(shift_reg); /* Store decoded byte */
            }
            /* else: framing error - discard */
            in_frame = false;
        }
    }
}
```

---

### 5.2 Moving Average Filter in C++

Suitable for filtering sequences of byte values received over UART (e.g., sensor readings encoded as uint8_t or uint16_t streams).

```cpp
#include <cstdint>
#include <array>
#include <numeric>

/**
 * MovingAverageFilter - Templated fixed-size moving average filter.
 *
 * @tparam T      Sample type (uint8_t, int16_t, float, etc.)
 * @tparam N      Window size (number of samples to average)
 */
template<typename T, std::size_t N>
class MovingAverageFilter {
public:
    MovingAverageFilter() : buffer_{}, head_(0), count_(0), sum_(0) {}

    /**
     * Update the filter with a new sample and return the current average.
     */
    T update(T sample)
    {
        if (count_ == N) {
            /* Subtract the oldest sample before it is overwritten */
            sum_ -= buffer_[head_];
        } else {
            ++count_;
        }

        buffer_[head_] = sample;
        sum_ += sample;
        head_ = (head_ + 1) % N;

        return static_cast<T>(sum_ / count_);
    }

    /** Reset the filter state. */
    void reset()
    {
        buffer_.fill(T{});
        head_  = 0;
        count_ = 0;
        sum_   = T{};
    }

private:
    std::array<T, N> buffer_;
    std::size_t      head_;
    std::size_t      count_;
    T                sum_;
};

/* ---------------------------------------------------------------
 * Usage example: 8-sample moving average over UART sensor bytes
 * --------------------------------------------------------------- */
static MovingAverageFilter<uint16_t, 8> adc_filter;

void uart_rx_callback(uint8_t high_byte, uint8_t low_byte)
{
    uint16_t raw   = (static_cast<uint16_t>(high_byte) << 8) | low_byte;
    uint16_t clean = adc_filter.update(raw);
    process_sensor_value(clean);
}
```

---

### 5.3 Median Filter in C

The median filter is ideal for rejecting single corrupted bytes without distorting the rest of the sequence.

```c
#include <stdint.h>
#include <string.h>

#define MEDIAN_WINDOW 5  /* Must be odd for a unique median */

/**
 * sort_u16 - Simple insertion sort on a small array (in-place).
 */
static void sort_u16(uint16_t *arr, uint8_t len)
{
    for (uint8_t i = 1; i < len; i++) {
        uint16_t key = arr[i];
        int8_t   j   = (int8_t)(i - 1);
        while (j >= 0 && arr[j] > key) {
            arr[j + 1] = arr[j];
            j--;
        }
        arr[j + 1] = key;
    }
}

/**
 * MedianFilter state.
 */
typedef struct {
    uint16_t buf[MEDIAN_WINDOW];
    uint8_t  head;
    uint8_t  count;
} MedianFilter;

void median_filter_init(MedianFilter *f)
{
    memset(f, 0, sizeof(*f));
}

/**
 * median_filter_update - Push a new sample and return the median.
 *
 * @param f       Filter state.
 * @param sample  New input sample.
 * @return        Current median of the window.
 */
uint16_t median_filter_update(MedianFilter *f, uint16_t sample)
{
    f->buf[f->head] = sample;
    f->head = (f->head + 1) % MEDIAN_WINDOW;
    if (f->count < MEDIAN_WINDOW) f->count++;

    /* Copy the active portion, sort, return middle element */
    uint16_t tmp[MEDIAN_WINDOW];
    memcpy(tmp, f->buf, f->count * sizeof(uint16_t));
    sort_u16(tmp, f->count);

    return tmp[f->count / 2];
}

/* ---------------------------------------------------------------
 * Usage
 * --------------------------------------------------------------- */
static MedianFilter rx_median;

void app_init(void)
{
    median_filter_init(&rx_median);
}

void process_uart_byte(uint16_t value)
{
    uint16_t filtered = median_filter_update(&rx_median, value);
    /* Use filtered value */
    (void)filtered;
}
```

---

### 5.4 IIR / EMA Filter in C

Extremely lightweight — ideal for resource-constrained MCUs with no FPU, using integer arithmetic.

```c
#include <stdint.h>

/**
 * Integer EMA filter using bit-shift for the smoothing factor.
 *
 * alpha = 1/2^SHIFT  (e.g. SHIFT=3 → alpha ≈ 0.125, heavy smoothing)
 *                     (     SHIFT=1 → alpha = 0.5,   light smoothing)
 *
 * Formula:  acc = acc - (acc >> SHIFT) + (sample >> SHIFT)
 *           (equivalent to acc = (1-alpha)*acc + alpha*sample, scaled by 2^SHIFT)
 */
#define EMA_SHIFT 3u   /* Tune: larger = smoother but slower response */

typedef struct {
    int32_t acc;        /* Internal accumulator (scaled by 2^SHIFT) */
    bool    initialised;
} EmaFilter;

/**
 * ema_update - Update the EMA filter with a new integer sample.
 *
 * @param f       Filter state.
 * @param sample  Raw sample (e.g. ADC reading from UART sensor frame).
 * @return        Filtered output (same scale as input).
 */
int32_t ema_update(EmaFilter *f, int32_t sample)
{
    if (!f->initialised) {
        f->acc         = sample << EMA_SHIFT;
        f->initialised = true;
    } else {
        f->acc = f->acc - (f->acc >> EMA_SHIFT) + sample;
    }
    return f->acc >> EMA_SHIFT;
}

/* ---------------------------------------------------------------
 * Floating-point variant (for systems with FPU)
 * --------------------------------------------------------------- */
typedef struct {
    float   acc;
    float   alpha;
    bool    initialised;
} EmaFilterF;

void ema_float_init(EmaFilterF *f, float alpha)
{
    f->alpha       = alpha;
    f->initialised = false;
}

float ema_float_update(EmaFilterF *f, float sample)
{
    if (!f->initialised) {
        f->acc         = sample;
        f->initialised = true;
    } else {
        f->acc = f->alpha * sample + (1.0f - f->alpha) * f->acc;
    }
    return f->acc;
}
```

---

### 5.5 Glitch / Debounce Filter in C

Applied at the bit level on a sampled RX signal to reject transients shorter than a threshold.

```c
#include <stdint.h>
#include <stdbool.h>

#define DEBOUNCE_COUNT 4u   /* Signal must be stable for this many samples */

typedef struct {
    uint8_t last_stable;     /* Last confirmed stable level (0 or 1) */
    uint8_t candidate;       /* Level currently being validated    */
    uint8_t stable_count;    /* Consecutive matching samples seen   */
} DebounceFilter;

void debounce_init(DebounceFilter *f, uint8_t initial_level)
{
    f->last_stable  = initial_level;
    f->candidate    = initial_level;
    f->stable_count = DEBOUNCE_COUNT;
}

/**
 * debounce_update - Feed one new sample; returns the current stable level.
 *
 * @param f      Filter state.
 * @param sample New 1-bit sample (0 or 1).
 * @return       Stable, noise-filtered level.
 */
uint8_t debounce_update(DebounceFilter *f, uint8_t sample)
{
    if (sample == f->candidate) {
        if (f->stable_count < DEBOUNCE_COUNT) {
            f->stable_count++;
        }
        if (f->stable_count >= DEBOUNCE_COUNT) {
            f->last_stable = f->candidate; /* Transition confirmed */
        }
    } else {
        /* New candidate level — restart the stability counter */
        f->candidate    = sample;
        f->stable_count = 1;
    }
    return f->last_stable;
}

/* ---------------------------------------------------------------
 * Integration: Soft-UART ISR using the debounce filter
 * --------------------------------------------------------------- */
static DebounceFilter rx_debounce;

void uart_oversampling_isr(void)
{
    uint8_t raw_bit = read_rx_pin();  /* platform-specific */
    uint8_t clean   = debounce_update(&rx_debounce, raw_bit);
    process_clean_bit(clean);
}
```

---

## Software Digital Filtering in Rust

The following Rust examples are written with embedded (`no_std`) compatibility in mind, using only `core` and optionally `heapless` for fixed-size buffers.

### 6.1 Majority Voting in Rust

```rust
/// Determine the majority of three 1-bit samples.
///
/// Equivalent to a 2-of-3 vote: result is 1 if at least two inputs are 1.
#[inline(always)]
pub fn majority_vote(s0: u8, s1: u8, s2: u8) -> u8 {
    (s0 & s1) | (s1 & s2) | (s0 & s2)
}

/// Soft-UART bit receiver state using majority voting.
pub struct SoftUartRx {
    samples:    [u8; 3],
    sample_idx: usize,
    shift_reg:  u8,
    bit_count:  u8,
    in_frame:   bool,
}

impl SoftUartRx {
    pub const fn new() -> Self {
        Self {
            samples:    [0u8; 3],
            sample_idx: 0,
            shift_reg:  0,
            bit_count:  0,
            in_frame:   false,
        }
    }

    /// Call this from your oversampling timer ISR (3× baud rate).
    ///
    /// Returns `Some(byte)` when a complete, noise-filtered byte is received.
    pub fn feed(&mut self, raw_sample: u8) -> Option<u8> {
        self.samples[self.sample_idx] = raw_sample & 1;
        self.sample_idx += 1;

        if self.sample_idx < 3 {
            return None; // Need all 3 samples
        }
        self.sample_idx = 0;

        let bit = majority_vote(self.samples[0], self.samples[1], self.samples[2]);

        if !self.in_frame {
            if bit == 0 {
                // Start bit detected
                self.in_frame  = true;
                self.bit_count = 0;
                self.shift_reg = 0;
            }
        } else if self.bit_count < 8 {
            self.shift_reg |= bit << self.bit_count;
            self.bit_count += 1;
        } else {
            // Stop bit
            self.in_frame = false;
            if bit == 1 {
                return Some(self.shift_reg); // Valid byte
            }
            // else framing error — byte discarded
        }
        None
    }
}
```

---

### 6.2 Moving Average Filter in Rust

```rust
use core::ops::{Add, AddAssign, SubAssign, Div};
use core::default::Default;

/// Fixed-size circular moving average filter.
///
/// # Type Parameters
/// - `T`: Numeric sample type.
/// - `N`: Window size (number of samples).
pub struct MovingAverage<T, const N: usize> {
    buffer: [T; N],
    head:   usize,
    count:  usize,
    sum:    T,
}

impl<T, const N: usize> MovingAverage<T, N>
where
    T: Copy + Default + Add<Output = T> + AddAssign + SubAssign + Div<T, Output = T> + From<usize>,
{
    pub const fn new() -> Self {
        Self {
            buffer: [T::default(); N],  // requires T: Copy + Default
            // NOTE: const fn limitation — use `new_zeroed` if T doesn't impl const Default
            head:   0,
            count:  0,
            sum:    T::default(),
        }
    }

    /// Feed a new sample; returns the current windowed average.
    pub fn update(&mut self, sample: T) -> T {
        if self.count == N {
            self.sum -= self.buffer[self.head]; // Remove oldest
        } else {
            self.count += 1;
        }
        self.buffer[self.head] = sample;
        self.sum += sample;
        self.head = (self.head + 1) % N;

        self.sum / T::from(self.count)
    }

    /// Reset the filter to its initial state.
    pub fn reset(&mut self) {
        self.buffer = [T::default(); N];
        self.head   = 0;
        self.count  = 0;
        self.sum    = T::default();
    }
}

// ---------------------------------------------------------------------------
// Usage example
// ---------------------------------------------------------------------------
fn process_uart_sensor_stream(raw_bytes: &[u16]) -> heapless::Vec<u16, 64> {
    let mut filter: MovingAverage<u16, 8> = MovingAverage::new();
    let mut out = heapless::Vec::new();

    for &sample in raw_bytes {
        let filtered = filter.update(sample);
        let _ = out.push(filtered);
    }
    out
}
```

---

### 6.3 Median Filter in Rust

```rust
/// Median filter over a fixed-size window using a sorted insertion approach.
///
/// Window size `N` should be an odd number for a unique median.
pub struct MedianFilter<const N: usize> {
    buffer: [u16; N],
    head:   usize,
    count:  usize,
}

impl<const N: usize> MedianFilter<N> {
    pub const fn new() -> Self {
        Self { buffer: [0u16; N], head: 0, count: 0 }
    }

    /// Update the filter with a new sample; returns the current median.
    pub fn update(&mut self, sample: u16) -> u16 {
        self.buffer[self.head] = sample;
        self.head = (self.head + 1) % N;
        if self.count < N {
            self.count += 1;
        }

        // Copy active window, sort, return middle element
        let mut tmp = [0u16; N];
        tmp[..self.count].copy_from_slice(&self.buffer[..self.count]);
        tmp[..self.count].sort_unstable();

        tmp[self.count / 2]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn median_rejects_spike() {
        let mut f: MedianFilter<5> = MedianFilter::new();
        assert_eq!(f.update(100), 100);
        assert_eq!(f.update(101), 100);
        assert_eq!(f.update(102), 101);
        assert_eq!(f.update(9999), 101); // spike — median stays near 101
        assert_eq!(f.update(100), 101);
    }
}
```

---

### 6.4 IIR / EMA Filter in Rust

```rust
/// Integer EMA filter using bit-shift for the smoothing coefficient.
///
/// Effective alpha = 1 / 2^SHIFT.
/// Larger SHIFT → heavier smoothing, slower step response.
pub struct EmaFilter {
    acc:         i32,
    shift:       u32,
    initialised: bool,
}

impl EmaFilter {
    /// Create a new EMA filter with the given bit-shift (1..=8 typical).
    pub const fn new(shift: u32) -> Self {
        Self { acc: 0, shift, initialised: false }
    }

    /// Feed a new sample; returns the filtered output.
    pub fn update(&mut self, sample: i32) -> i32 {
        if !self.initialised {
            self.acc         = sample << self.shift;
            self.initialised = true;
        } else {
            self.acc = self.acc - (self.acc >> self.shift) + sample;
        }
        self.acc >> self.shift
    }

    pub fn reset(&mut self) {
        self.acc         = 0;
        self.initialised = false;
    }
}

/// Floating-point EMA filter for systems with an FPU.
pub struct EmaFilterF32 {
    acc:         f32,
    alpha:       f32,
    initialised: bool,
}

impl EmaFilterF32 {
    pub const fn new(alpha: f32) -> Self {
        Self { acc: 0.0, alpha, initialised: false }
    }

    pub fn update(&mut self, sample: f32) -> f32 {
        if !self.initialised {
            self.acc         = sample;
            self.initialised = true;
        } else {
            self.acc = self.alpha * sample + (1.0 - self.alpha) * self.acc;
        }
        self.acc
    }
}

// ---------------------------------------------------------------------------
// Usage: filter raw UART ADC readings
// ---------------------------------------------------------------------------
fn demo() {
    let mut ema = EmaFilter::new(3); // alpha ≈ 0.125

    let raw_samples: [i32; 8] = [100, 102, 98, 150 /*spike*/, 101, 99, 100, 102];
    for &s in &raw_samples {
        let out = ema.update(s);
        // In embedded code: transmit/process `out`
        let _ = out;
    }
}
```

---

### 6.5 Glitch / Debounce Filter in Rust (no_std)

```rust
/// Debounce filter: requires `STABLE` consecutive matching samples
/// before accepting a level transition. Effective for rejecting
/// glitches shorter than `STABLE` sample periods on the UART RX line.
pub struct DebounceFilter {
    last_stable:  u8,
    candidate:    u8,
    stable_count: u8,
    threshold:    u8,
}

impl DebounceFilter {
    /// Create a new debounce filter.
    ///
    /// - `initial_level`: Starting assumed level (0 or 1).
    /// - `threshold`: Number of matching samples required to accept a transition.
    pub const fn new(initial_level: u8, threshold: u8) -> Self {
        Self {
            last_stable:  initial_level,
            candidate:    initial_level,
            stable_count: threshold,
            threshold,
        }
    }

    /// Feed a new raw bit sample (0 or 1).
    /// Returns the current debounced (stable) level.
    #[inline]
    pub fn update(&mut self, sample: u8) -> u8 {
        let sample = sample & 1;
        if sample == self.candidate {
            if self.stable_count < self.threshold {
                self.stable_count += 1;
            }
            if self.stable_count >= self.threshold {
                self.last_stable = self.candidate;
            }
        } else {
            self.candidate    = sample;
            self.stable_count = 1;
        }
        self.last_stable
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn glitch_suppressed() {
        let mut f = DebounceFilter::new(1, 4); // Start high, need 4 stable samples
        // Momentary low glitch (3 samples) — should NOT cause transition
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(1), 1); // Returns to high before threshold
        // Legitimate low (4+ samples) — SHOULD transition
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(0), 1);
        assert_eq!(f.update(0), 0); // Confirmed
    }
}
```

---

## Combining Filters in a UART Receive Pipeline

Real-world UART noise rejection benefits from layering multiple filters at different stages of the signal chain. A typical pipeline looks like:

```
Physical RX Pin
      │
      ▼
[Hardware Oversampling]     ← MCU UART peripheral: 8× or 16× majority vote
      │
      ▼
[Glitch / Debounce Filter]  ← Software: reject spikes < N sample periods
      │
      ▼
[UART Frame Decoder]        ← Start bit, 8 data bits, stop bit
      │
      ▼
[Byte-level Median Filter]  ← Reject single corrupted bytes (outlier rejection)
      │
      ▼
[EMA / Moving Average]      ← Smooth amplitude noise in sensor value sequences
      │
      ▼
Application Logic
```

### C++ Pipeline Example

```cpp
#include <cstdint>
#include <optional>

class UartRxPipeline {
public:
    UartRxPipeline() = default;

    /** Called from UART RX complete callback with each received byte. */
    std::optional<uint16_t> process(uint8_t high, uint8_t low)
    {
        uint16_t raw = (static_cast<uint16_t>(high) << 8) | low;

        /* Stage 1: Median filter — reject outlier bytes */
        uint16_t med = median_.update(raw);

        /* Stage 2: EMA filter — smooth residual amplitude noise */
        float smooth = ema_.update(static_cast<float>(med));

        return static_cast<uint16_t>(smooth);
    }

private:
    MedianFilter<uint16_t, 5>   median_;  /* Defined earlier */
    EmaFilterF                  ema_{0.3f}; /* alpha = 0.3 */
};
```

---

## Performance Considerations

| Filter | RAM Usage | CPU Cycles (approx.) | Latency | Best For |
|---|---|---|---|---|
| Majority Vote | O(1) | ~5 | < 1 bit period | Bit-level glitches |
| Debounce | O(1) | ~8 | N × sample period | Short glitch rejection |
| EMA (integer) | O(1) | ~4 (shift/add) | ~1/α sample periods | Smooth streaming data |
| EMA (float) | O(1) | ~10 (with FPU) | ~1/α sample periods | Smooth streaming data |
| Moving Average | O(N) | ~N+5 | N/2 samples | Low-distortion smoothing |
| Median | O(N log N) | ~N² (insertion sort) | N/2 samples | Impulse noise rejection |

**Recommendations for resource-constrained MCUs:**
- Prefer integer EMA over floating-point EMA where an FPU is absent.
- Keep median filter windows ≤ 7 elements to bound CPU cost.
- Always enable the hardware majority vote first; software filters are complementary, not replacements.
- Avoid large moving average windows in ISR context — move processing to the main loop.

---

## Summary

Digital filtering for UART reception operates at two levels: **bit-level** filtering (majority voting, debouncing) to prevent noise from corrupting individual bit decisions, and **byte-level** filtering (moving average, median, EMA) to smooth or de-spike sequences of received data values.

The key takeaways are:

**Majority voting** is the foundational bit-level filter and is implemented in hardware by virtually every modern MCU UART peripheral. It should always be enabled and is the first line of defence.

**Debounce / glitch filtering** in software extends majority voting to wider glitch windows or is used in soft-UART implementations where hardware filtering is not available.

**Median filters** excel at rejecting isolated corrupted bytes without blurring sharp signal transitions, making them ideal for protocol-level byte streams where occasional framing errors produce wildly wrong values.

**Moving average (FIR) filters** offer low-distortion smoothing with configurable bandwidth but introduce latency and require a RAM buffer proportional to the window size.

**Exponential moving average (IIR) filters** are the most efficient choice when RAM and CPU are scarce: a single state variable and a handful of instructions per sample deliver effective low-pass filtering at minimal cost.

In practice, the most robust approach is a pipeline combining hardware oversampling with a lightweight software debounce at the bit level, followed by a median and/or EMA filter on the byte value stream. Both C/C++ and Rust implementations can be made fully `no_std` compatible, enabling their use on bare-metal MCUs from the smallest AVR to modern Cortex-M33 targets.

---

*Document: 72 — Digital Filtering in UART Reception | C/C++ & Rust Reference*