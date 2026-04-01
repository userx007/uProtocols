# 51. Ultra Fast-mode (UFm) — 5 MHz I2C with Unidirectional Push-Pull

**Concept & Electrical** — UFm achieves 5 MHz by completely eliminating the open-drain topology and replacing it with push-pull drivers and a series resistor. The RC charging bottleneck that limits all other I2C modes simply does not exist here. The trade-off is that slaves can never drive the bus — making it permanently write-only.

**Protocol changes from standard I2C:**
- No ACK phase (no 9th clock pulse per byte)
- No clock stretching
- No multi-master
- No reads — ever

**C/C++ examples provided:**
1. **Register-level hardware driver** — conceptual NXP LPC-style peripheral with `CFG_PUSHPULL`, `CFG_NOACK`, `CFG_NOSTRETCH` bits
2. **Portable GPIO bit-bang** — pure push-pull bit-bang with correct `t_SU;DAT` / `t_HD;STA` timing
3. **C++17 class** — RAII `Transaction` guard, `std::span`, and a `GpioHal` concept for testability

**Rust examples provided:**
1. **`embedded-hal` bit-bang driver** — idiomatic `OutputPin` + `DelayNs` traits, write and `write_iter`
2. **Typestate builder** — `Idle → Started → Addressed → Committed` enforced at compile time; invalid sequences are compile errors, not runtime panics
3. **CRC-8/MAXIM wrapper** — application-layer integrity since UFm provides none at the protocol level

---

## Table of Contents

1. [Overview](#1-overview)
2. [Historical Context and Motivation](#2-historical-context-and-motivation)
3. [Electrical Characteristics](#3-electrical-characteristics)
4. [Protocol Differences from Standard I2C](#4-protocol-differences-from-standard-i2c)
5. [Timing Parameters](#5-timing-parameters)
6. [Addressing and Frame Structure](#6-addressing-and-frame-structure)
7. [Limitations and Constraints](#7-limitations-and-constraints)
8. [Use Cases](#8-use-cases)
9. [Programming in C/C++](#9-programming-in-cc)
10. [Programming in Rust](#10-programming-in-rust)
11. [Mixed-Mode Bus Considerations](#11-mixed-mode-bus-considerations)
12. [Summary](#12-summary)

---

## 1. Overview

**Ultra Fast-mode (UFm)** is a unidirectional variant of the I2C protocol standardized in the
NXP *UM10204 I2C-bus specification*, revision 6 (2014) and later. It operates at a nominal
clock frequency of **5 MHz** — ten times faster than Fast-mode Plus (FM+) and fifty times
faster than Standard-mode — making it the highest-speed member of the I2C family.

The defining characteristics of UFm are:

| Property | Value |
|---|---|
| Maximum clock frequency | **5 MHz** |
| Data direction | **Master → Slave only (unidirectional)** |
| Driver type | **Push-pull** (no open-drain, no pull-up resistors) |
| Acknowledgement (ACK) | **Not supported** |
| Clock stretching | **Not supported** |
| Multi-master | **Not supported** |
| Bus capacitance limit | 10 nF (vs 400 pF for FM+) |

Because slaves never drive the bus, there is no need for open-drain outputs or pull-up
resistors. The absence of the RC time constant imposed by pull-up resistors and bus
capacitance is precisely what enables the 5 MHz clock rate.

---

## 2. Historical Context and Motivation

Standard-mode (100 kHz), Fast-mode (400 kHz), Fast-mode Plus (1 MHz), and High-speed mode
(3.4 MHz) all share the fundamental open-drain bus topology. Open-drain means the rising
edge of SCL and SDA is determined by the RC charging time of the pull-up resistor and bus
capacitance. As frequency increases, this RC constraint becomes the dominant bottleneck.

High-speed mode (Hs-mode) works around this by requiring current-source pull-ups during
Hs transfers, but the bus must still start with a lower-speed handshake, and slaves can
still drive the bus.

UFm takes a different philosophy entirely: **eliminate the slave-to-master data path
completely**. If slaves never need to respond, the bus can be pure push-pull, rising edges
become near-instantaneous, and 5 MHz becomes feasible on reasonable PCB traces.

This trade-off — speed for bidirectionality — is well suited to a class of devices (LED
drivers, DACs, display controllers, actuator drivers) where configuration write operations
far outnumber read-back operations, or where read-back is simply not required at all.

---

## 3. Electrical Characteristics

### 3.1 Push-Pull Drivers

In UFm, both the master and all slaves use **push-pull output stages** on SCL and SDA.
The master actively drives both lines HIGH and LOW. Slaves drive SDA LOW during data
reception but — critically — the protocol does not include an ACK phase, so slaves never
need to pull SDA low independently. In practice, the slave SDA pin is input-only in UFm.

```
VDD
 |
 ├──[Series resistor Rs]──┐
 |                        |
 PMOS                   SDA/SCL
 |                        |
 NMOS                     |
 |                        |
GND                      GND
```

The series resistor `Rs` (typically 10–50 Ω) is placed on the master output to limit
current spikes and control signal edge rates. This is mandatory per the UFm specification.

### 3.2 No Pull-up Resistors

UFm buses have **no pull-up resistors**. This is a fundamental departure from all other
I2C modes. The absence of pull-ups means:

- No static current consumption from the pull-up network.
- Ringing/overshoot must be managed by the series resistor and careful PCB layout.
- A disconnected or unpowered slave leaves the bus in an indeterminate state (the master
  always drives, so this is usually acceptable).

### 3.3 Voltage Levels

UFm uses the same voltage thresholds as FM+ (1.8 V, 3.3 V, 5 V supply compatible) with
the standard I2C V_IL / V_IH definitions. The push-pull driver ensures full rail-to-rail
swings, so noise margins are excellent.

### 3.4 Bus Capacitance

The specification permits up to **10 nF** total bus capacitance, but this is a maximum
constrained by the need to charge/discharge within the very short bit period (~200 ns at
5 MHz). In practice, keeping capacitance below 1–2 nF is recommended for clean 5 MHz
operation.

---

## 4. Protocol Differences from Standard I2C

This table compares UFm against the mainstream I2C modes:

| Feature | SM (100k) | FM (400k) | FM+ (1M) | Hs (3.4M) | **UFm (5M)** |
|---|---|---|---|---|---|
| Open-drain | ✓ | ✓ | ✓ | ✓ | **✗** |
| Pull-up resistors | ✓ | ✓ | ✓ | ✓ | **✗** |
| Bidirectional SDA | ✓ | ✓ | ✓ | ✓ | **✗** |
| ACK/NACK | ✓ | ✓ | ✓ | ✓ | **✗** |
| Clock stretching | ✓ | ✓ | ✓ | ✓ | **✗** |
| Multi-master | ✓ | ✓ | ✓ | ✓ | **✗** |
| Read transactions | ✓ | ✓ | ✓ | ✓ | **✗** |
| Push-pull driver | ✗ | ✗ | ✗ | Partial | **✓** |

### 4.1 START and STOP Conditions

UFm retains the I2C START and STOP signaling conventions:

- **START**: SDA falls while SCL is HIGH.
- **STOP**: SDA rises while SCL is HIGH.
- **Repeated START**: A new START without a preceding STOP.

These are generated by the master using push-pull transitions.

### 4.2 No ACK Phase

In standard I2C, after every 8 data bits the transmitter releases SDA and the receiver
pulls SDA LOW (ACK) or leaves it HIGH (NACK). In UFm this ACK phase **does not exist**.
The master keeps driving SDA throughout the entire transaction. There is no 9th clock
pulse for acknowledgement.

This means:

- The master cannot detect if a slave is absent or has failed.
- Higher-layer protocols must handle error detection (e.g., CRC, watchdog timers).
- Transactions are shorter by one clock cycle per byte, slightly improving throughput.

### 4.3 Frame Format

```
START | ADDR[7:1] | R/W=0 | DATA[7:0] | DATA[7:0] | ... | STOP
       ←— 7 bits —→  1 bit   ←— 8 bits—→
```

- `R/W` bit is always **0** (Write). The specification mandates write-only operation.
- No ACK clock pulses between bytes.
- The slave address byte and all data bytes are 8 bits, driven entirely by the master.

---

## 5. Timing Parameters

At 5 MHz, one clock period is 200 ns. Key UFm timing parameters from the specification:

| Parameter | Symbol | Min | Max | Unit |
|---|---|---|---|---|
| SCL clock frequency | f_SCL | — | 5000 | kHz |
| LOW period of SCL | t_LOW | 50 | — | ns |
| HIGH period of SCL | t_HIGH | 50 | — | ns |
| Rise time SCL/SDA | t_r | — | 10 | ns |
| Fall time SCL/SDA | t_f | — | 10 | ns |
| Setup time for START | t_SU;STA | 20 | — | ns |
| Hold time for START | t_HD;STA | 20 | — | ns |
| Setup time for STOP | t_SU;STO | 20 | — | ns |
| Data setup time | t_SU;DAT | 5 | — | ns |
| Data hold time | t_HD;DAT | 0 | — | ns |
| Bus free time | t_BUF | 20 | — | ns |

The 10 ns rise/fall time requirement is only achievable with push-pull drivers and
carefully designed series resistors. Open-drain with pull-ups cannot meet this.

---

## 6. Addressing and Frame Structure

UFm uses the same 7-bit (and optionally 10-bit) address space as standard I2C. The
general call address (0x00) and reserved addresses apply equally.

### 6.1 7-bit Address Write Transaction

```
Bit:  S  A6 A5 A4 A3 A2 A1 A0  W   D7 D6 D5 D4 D3 D2 D1 D0  ... P
      ↑  ←————— Address ————→  0   ←————— Data byte ————→       ↑
    START                    R/W=0                             STOP
```

There is no ACK bit between the address byte and the first data byte, and no ACK between
subsequent data bytes.

### 6.2 10-bit Addressing

For 10-bit addresses, the first byte is `11110XX0` where `XX` are the two MSBs of the
address, followed by the 8 LSBs in the second byte. No ACK pulses follow either byte.

---

## 7. Limitations and Constraints

Understanding what UFm **cannot** do is as important as knowing what it can:

1. **No reads**: The master can never read data from a slave. Sensor data, status
   registers, and EEPROM contents are inaccessible over UFm.

2. **No error detection at the protocol level**: Lost bytes, address collisions, and
   slave failures are all silent. Application-layer CRC or checksums are essential.

3. **No clock stretching**: Slaves cannot pause the master by holding SCL low. Slaves
   must process data at full 5 MHz rate or buffer incoming bytes internally.

4. **Single master only**: Bus arbitration requires slaves to be able to drive the bus,
   which UFm slaves cannot do. Only one master is permitted.

5. **PCB layout discipline**: At 5 MHz, trace inductance, capacitance, and impedance
   mismatches cause reflections. Keep UFm traces short (<5 cm), matched, and avoid vias.

6. **Limited device support**: As of 2024, UFm is supported by a modest number of
   controller and peripheral ICs compared to SM/FM/FM+. Check datasheets carefully.

---

## 8. Use Cases

UFm is the right choice when:

- You need to stream configuration or command data to one or more actuators/drivers
  at the highest possible I2C speed.
- Read-back is not required, or is done via a separate interface (e.g., SPI, UART).
- The device count is small and all are on the same board (short traces).

**Typical applications:**

- LED matrix/driver configuration (e.g., IS31FL series with UFm support)
- LCD/OLED display command streaming
- DAC value streaming (audio, lighting, motor control set-points)
- Programmable power supply voltage control
- High-speed relay/switch matrix control
- FPGA configuration register streaming during operation

---

## 9. Programming in C/C++

Most microcontrollers do not have hardware UFm peripherals as of 2024; UFm is more
commonly found on dedicated I2C bridge ICs and specialized SoCs. The examples below
cover:

1. **Hardware UFm peripheral** (register-level, e.g., NXP LPC family concept).
2. **Bit-bang UFm** using GPIO with push-pull outputs — the most portable approach.
3. **C++ UFm master class** with RAII and transaction abstraction.

---

### 9.1 Hardware UFm — Register-Level C (Conceptual LPC-style)

```c
/**
 * ufm_hw.c
 * Register-level UFm master driver (conceptual NXP LPC-style peripheral).
 *
 * UFm differences from standard I2C peripheral configuration:
 *   - Enable push-pull output mode (no open-drain)
 *   - Set clock divider for 5 MHz
 *   - Disable ACK generation
 *   - Disable clock-stretch support
 */

#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------------------------
 * Peripheral register map (illustrative — adapt to your SoC)
 * -------------------------------------------------------------------------- */
typedef volatile struct {
    uint32_t CFG;       /* Configuration register                  */
    uint32_t STAT;      /* Status register                         */
    uint32_t INTENSET;  /* Interrupt enable set                    */
    uint32_t INTENCLR;  /* Interrupt enable clear                  */
    uint32_t TIMEOUT;   /* Timeout value                           */
    uint32_t CLKDIV;    /* Clock pre-divider                       */
    uint32_t INTSTAT;   /* Interrupt status (read-only)            */
    uint32_t _reserved;
    uint32_t MSTCTL;    /* Master control                          */
    uint32_t MSTTIME;   /* Master timing configuration             */
    uint32_t MSTDAT;    /* Master data register                    */
} I2C_Type;

/* Register bit definitions */
#define I2C_CFG_MSTEN       (1u << 0)   /* Master enable                  */
#define I2C_CFG_PUSHPULL    (1u << 4)   /* Push-pull mode (UFm)           */
#define I2C_CFG_NOACK       (1u << 5)   /* Disable ACK generation (UFm)   */
#define I2C_CFG_NOSTRETCH   (1u << 6)   /* Disable clock stretch (UFm)    */

#define I2C_STAT_MSTPEND    (1u << 0)   /* Master pending                 */
#define I2C_STAT_MSTSTATE   (7u << 1)   /* Master state field             */
#define I2C_MSTST_IDLE      (0u << 1)
#define I2C_MSTST_TXRDY     (2u << 1)   /* Transmit ready                 */

#define I2C_MSTCTL_START    (1u << 1)   /* Generate START                 */
#define I2C_MSTCTL_STOP     (1u << 2)   /* Generate STOP                  */
#define I2C_MSTCTL_CONTINUE (1u << 0)   /* Continue (next byte)           */

/* Peripheral base address (example) */
#define I2C0_BASE           ((I2C_Type *)0x40086000UL)

/* --------------------------------------------------------------------------
 * UFm initialisation
 *
 * @param i2c       Pointer to I2C peripheral registers
 * @param pclk_hz   Peripheral clock frequency in Hz (e.g. 150000000)
 * -------------------------------------------------------------------------- */
void ufm_init(I2C_Type *i2c, uint32_t pclk_hz)
{
    /* Step 1: Compute clock divider for 5 MHz SCL.
     *
     * SCL_freq = pclk / ((CLKDIV + 1) * (MSTSCLLOW + 2 + MSTSCLHIGH + 2))
     *
     * For equal HIGH/LOW: MSTSCLLOW = MSTSCLHIGH = N
     * Target: 5 000 000 Hz → period 200 ns, each half 100 ns.
     * With pclk = 150 MHz → 150e6 / 5e6 = 30 pclk cycles per SCL period.
     * Set CLKDIV=0 (divide by 1), MSTSCLLOW=MSTSCLHIGH = (30/2) - 2 = 13.
     */
    uint32_t scl_half = (pclk_hz / (2u * 5000000u)) - 2u;
    if (scl_half < 1u) scl_half = 1u;

    i2c->CLKDIV  = 0u;                         /* Pre-divider = 1          */
    i2c->MSTTIME = (scl_half << 4) | scl_half; /* HIGH[7:4] | LOW[3:0]    */

    /* Step 2: Configure UFm-specific options and enable master.            */
    i2c->CFG = I2C_CFG_MSTEN     /* Master mode                            */
             | I2C_CFG_PUSHPULL  /* Push-pull output drivers               */
             | I2C_CFG_NOACK     /* No ACK phase between bytes             */
             | I2C_CFG_NOSTRETCH;/* Slaves may not stretch SCL             */
}

/* --------------------------------------------------------------------------
 * Wait for the master state machine to reach a ready/idle state.
 * Returns false on timeout.
 * -------------------------------------------------------------------------- */
static bool ufm_wait_pending(I2C_Type *i2c)
{
    uint32_t timeout = 100000u;
    while (!(i2c->STAT & I2C_STAT_MSTPEND)) {
        if (--timeout == 0u) return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Transmit a UFm write transaction.
 *
 * @param i2c       Peripheral registers
 * @param addr7     7-bit slave address
 * @param data      Pointer to data bytes to transmit
 * @param len       Number of bytes
 * @return          true on success, false on timeout
 * -------------------------------------------------------------------------- */
bool ufm_write(I2C_Type *i2c, uint8_t addr7, const uint8_t *data, uint32_t len)
{
    /* Load address byte (R/W = 0 → write) and issue START */
    i2c->MSTDAT = (uint32_t)(addr7 << 1) | 0u;
    i2c->MSTCTL = I2C_MSTCTL_START;

    if (!ufm_wait_pending(i2c)) return false;

    /* Verify master entered TX-ready state (address was sent) */
    if ((i2c->STAT & I2C_STAT_MSTSTATE) != I2C_MSTST_TXRDY) return false;

    /* Transmit data bytes */
    for (uint32_t i = 0u; i < len; i++) {
        i2c->MSTDAT = data[i];
        i2c->MSTCTL = I2C_MSTCTL_CONTINUE;

        if (!ufm_wait_pending(i2c)) return false;
        if ((i2c->STAT & I2C_STAT_MSTSTATE) != I2C_MSTST_TXRDY) return false;
    }

    /* Generate STOP */
    i2c->MSTCTL = I2C_MSTCTL_STOP;

    /* Wait for bus to return to idle */
    uint32_t timeout = 100000u;
    while ((i2c->STAT & I2C_STAT_MSTSTATE) != I2C_MSTST_IDLE) {
        if (--timeout == 0u) return false;
    }
    return true;
}

/* --------------------------------------------------------------------------
 * Example usage
 * -------------------------------------------------------------------------- */
int main(void)
{
    I2C_Type *i2c = I2C0_BASE;

    /* Assume clock tree and GPIO push-pull configuration already done */
    ufm_init(i2c, 150000000u); /* 150 MHz peripheral clock */

    /* Send 3 bytes to LED driver at address 0x38 */
    const uint8_t cmd[] = { 0x01, 0xFF, 0x80 }; /* reg, brightness, mode */
    ufm_write(i2c, 0x38u, cmd, sizeof(cmd));

    for (;;) { /* spin */ }
}
```

---

### 9.2 Bit-Bang UFm in C (Portable GPIO Implementation)

Because hardware UFm peripherals are rare, bit-banging is the most practical approach
on general-purpose microcontrollers. Key difference from I2C bit-bang: **both SCL and
SDA are push-pull outputs**; no ACK sampling step.

```c
/**
 * ufm_bitbang.c
 *
 * Portable UFm bit-bang master using push-pull GPIO.
 * At 5 MHz each half-period is ~100 ns; adjust delay_ns() for your MCU.
 *
 * GPIO HAL requirements (implement for your platform):
 *   void gpio_set(int pin, int val);   -- drive pin HIGH (1) or LOW (0)
 *   void gpio_output(int pin);         -- configure pin as push-pull output
 *   void delay_ns(uint32_t ns);        -- busy-wait nanoseconds
 */

#include <stdint.h>
#include <stdbool.h>

/* Pin assignments (adapt to your board) */
#define UFM_SCL_PIN   10
#define UFM_SDA_PIN   11

/* Half-period for 5 MHz: 100 ns.
 * At 168 MHz Cortex-M4: ~17 cycles per 100 ns → tune empirically.         */
#define UFM_HALF_PERIOD_NS  100u

/* --------------------------------------------------------------------------
 * Low-level bus primitives
 * -------------------------------------------------------------------------- */
static inline void scl_high(void) { gpio_set(UFM_SCL_PIN, 1); delay_ns(UFM_HALF_PERIOD_NS); }
static inline void scl_low(void)  { gpio_set(UFM_SCL_PIN, 0); delay_ns(UFM_HALF_PERIOD_NS); }
static inline void sda_high(void) { gpio_set(UFM_SDA_PIN, 1); }
static inline void sda_low(void)  { gpio_set(UFM_SDA_PIN, 0); }

/* --------------------------------------------------------------------------
 * UFm GPIO initialisation: both pins as push-pull outputs, idle HIGH.
 * -------------------------------------------------------------------------- */
void ufm_bb_init(void)
{
    gpio_output(UFM_SCL_PIN);
    gpio_output(UFM_SDA_PIN);
    gpio_set(UFM_SCL_PIN, 1);
    gpio_set(UFM_SDA_PIN, 1);
    delay_ns(UFM_HALF_PERIOD_NS * 2u); /* Bus-free time t_BUF (≥20 ns)     */
}

/* START: SDA falls while SCL is HIGH                                        */
static void ufm_start(void)
{
    sda_high(); scl_high();
    delay_ns(UFM_HALF_PERIOD_NS); /* t_SU;STA ≥ 20 ns                      */
    sda_low();
    delay_ns(UFM_HALF_PERIOD_NS); /* t_HD;STA ≥ 20 ns                      */
    scl_low();
}

/* STOP: SDA rises while SCL is HIGH                                         */
static void ufm_stop(void)
{
    sda_low();
    scl_high();
    delay_ns(UFM_HALF_PERIOD_NS); /* t_SU;STO ≥ 20 ns                      */
    sda_high();
    delay_ns(UFM_HALF_PERIOD_NS * 2u); /* t_BUF ≥ 20 ns before next START  */
}

/* --------------------------------------------------------------------------
 * Transmit one byte (MSB first). No ACK phase.
 * -------------------------------------------------------------------------- */
static void ufm_send_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        /* Setup SDA before SCL rises (t_SU;DAT ≥ 5 ns)                    */
        if (byte & (1u << bit)) {
            sda_high();
        } else {
            sda_low();
        }
        delay_ns(5u);   /* Meet t_SU;DAT                                    */
        scl_high();     /* Slave samples SDA on rising SCL edge             */
        scl_low();
    }
    /* No 9th clock: UFm has no ACK phase                                   */
}

/* --------------------------------------------------------------------------
 * UFm write transaction.
 *
 * @param addr7   7-bit slave address
 * @param data    Data bytes to transmit
 * @param len     Number of bytes
 * -------------------------------------------------------------------------- */
void ufm_bb_write(uint8_t addr7, const uint8_t *data, uint32_t len)
{
    ufm_start();
    ufm_send_byte((uint8_t)(addr7 << 1) | 0u);  /* Address + W bit = 0    */
    for (uint32_t i = 0u; i < len; i++) {
        ufm_send_byte(data[i]);
    }
    ufm_stop();
}

/* --------------------------------------------------------------------------
 * Example: stream 8 brightness values to an LED driver (address 0x60)
 * -------------------------------------------------------------------------- */
int main(void)
{
    ufm_bb_init();

    uint8_t brightness[8] = { 255, 128, 64, 32, 16, 8, 4, 2 };

    /* First byte is the register address within the slave (0x00 = CH0)     */
    uint8_t frame[9];
    frame[0] = 0x00u;  /* Auto-increment register start                     */
    for (int i = 0; i < 8; i++) frame[i + 1] = brightness[i];

    ufm_bb_write(0x60u, frame, sizeof(frame));

    for (;;) {}
}
```

---

### 9.3 C++ UFm Master Class

A higher-level abstraction using C++17 with RAII transaction guard and compile-time
address validation:

```cpp
/**
 * UfmMaster.hpp
 *
 * C++17 UFm master abstraction.
 * Template parameter selects the GPIO HAL type for testability (dependency injection).
 */

#pragma once
#include <cstdint>
#include <cstddef>
#include <array>
#include <span>
#include <cassert>

/* --------------------------------------------------------------------------
 * Result type — avoids exceptions on bare-metal targets
 * -------------------------------------------------------------------------- */
enum class UfmResult : uint8_t {
    Ok = 0,
    Timeout,
    InvalidAddress,
    BusBusy,
};

/* --------------------------------------------------------------------------
 * Concept: GpioHal must provide:
 *   void set_output_pushpull(int pin)
 *   void write(int pin, bool value)
 *   void delay_ns(uint32_t ns)
 * -------------------------------------------------------------------------- */
template<typename T>
concept GpioHal = requires(T hal, int pin, bool val, uint32_t ns) {
    hal.set_output_pushpull(pin);
    hal.write(pin, val);
    hal.delay_ns(ns);
};

/* --------------------------------------------------------------------------
 * UfmMaster<Hal>
 * -------------------------------------------------------------------------- */
template<GpioHal Hal>
class UfmMaster {
public:
    static constexpr uint32_t kHalfPeriodNs = 100u;  /* 5 MHz              */

    UfmMaster(Hal& hal, int scl_pin, int sda_pin)
        : hal_(hal), scl_(scl_pin), sda_(sda_pin)
    {
        hal_.set_output_pushpull(scl_);
        hal_.set_output_pushpull(sda_);
        idle();
        hal_.delay_ns(kHalfPeriodNs * 2u);
    }

    /* Validate 7-bit address (reserved ranges 0x00, 0x78-0x7F excluded)   */
    static constexpr bool valid_address(uint8_t addr) noexcept {
        return addr >= 0x08u && addr <= 0x77u;
    }

    /* ------------------------------------------------------------------
     * write() — transmit bytes to slave address.
     * Returns UfmResult::InvalidAddress for reserved addresses.
     * ------------------------------------------------------------------ */
    UfmResult write(uint8_t addr7, std::span<const uint8_t> data) noexcept {
        if (!valid_address(addr7)) return UfmResult::InvalidAddress;

        start();
        send_byte(static_cast<uint8_t>((addr7 << 1u) | 0u));
        for (auto byte : data) {
            send_byte(byte);
        }
        stop();
        return UfmResult::Ok;
    }

    /* Convenience overload for fixed-size arrays                           */
    template<std::size_t N>
    UfmResult write(uint8_t addr7, const std::array<uint8_t, N>& arr) noexcept {
        return write(addr7, std::span<const uint8_t>{arr});
    }

    /* ------------------------------------------------------------------
     * RAII Transaction guard.
     * Usage:
     *   {
     *       auto tx = master.begin_transaction(0x38);
     *       tx.send(reg_addr);
     *       tx.send(value);
     *   }  // STOP generated on destruction
     * ------------------------------------------------------------------ */
    class Transaction {
    public:
        Transaction(UfmMaster& m, uint8_t addr7) : master_(m) {
            master_.start();
            master_.send_byte(static_cast<uint8_t>((addr7 << 1u) | 0u));
        }
        ~Transaction() { master_.stop(); }

        Transaction& send(uint8_t byte) noexcept {
            master_.send_byte(byte);
            return *this;
        }

        /* Chain multiple bytes */
        template<typename... Bytes>
        Transaction& send_all(Bytes... bytes) noexcept {
            (send(static_cast<uint8_t>(bytes)), ...);
            return *this;
        }

        /* Non-copyable, non-movable */
        Transaction(const Transaction&) = delete;
        Transaction& operator=(const Transaction&) = delete;

    private:
        UfmMaster& master_;
    };

    [[nodiscard]] Transaction begin_transaction(uint8_t addr7) noexcept {
        assert(valid_address(addr7));
        return Transaction{*this, addr7};
    }

private:
    Hal&  hal_;
    int   scl_;
    int   sda_;

    void idle() noexcept {
        hal_.write(scl_, true);
        hal_.write(sda_, true);
    }

    void start() noexcept {
        hal_.write(sda_, true);
        hal_.write(scl_, true);
        hal_.delay_ns(kHalfPeriodNs);
        hal_.write(sda_, false);
        hal_.delay_ns(kHalfPeriodNs);
        hal_.write(scl_, false);
    }

    void stop() noexcept {
        hal_.write(sda_, false);
        hal_.write(scl_, true);
        hal_.delay_ns(kHalfPeriodNs);
        hal_.write(sda_, true);
        hal_.delay_ns(kHalfPeriodNs * 2u);
    }

    void send_byte(uint8_t byte) noexcept {
        for (int bit = 7; bit >= 0; --bit) {
            hal_.write(sda_, (byte >> bit) & 1u);
            hal_.delay_ns(5u);              /* t_SU;DAT                    */
            hal_.write(scl_, true);
            hal_.delay_ns(kHalfPeriodNs);
            hal_.write(scl_, false);
            hal_.delay_ns(kHalfPeriodNs - 5u);
        }
        /* No ACK clock pulse */
    }
};

/* --------------------------------------------------------------------------
 * Example main using UfmMaster<MyGpioHal>
 * -------------------------------------------------------------------------- */
#ifdef UFM_EXAMPLE_MAIN

struct MyGpioHal {
    void set_output_pushpull(int /*pin*/) { /* configure GPIO */ }
    void write(int /*pin*/, bool /*val*/)  { /* drive GPIO    */ }
    void delay_ns(uint32_t /*ns*/)         { /* busy wait     */ }
};

int main()
{
    MyGpioHal hal;
    UfmMaster<MyGpioHal> ufm{hal, /*scl=*/10, /*sda=*/11};

    /* Simple write */
    const std::array<uint8_t, 3> cfg = { 0x01u, 0xFFu, 0x00u };
    ufm.write(0x38u, cfg);

    /* RAII transaction — STOP on scope exit */
    {
        ufm.begin_transaction(0x38u)
           .send_all(0x02u, 0x80u, 0x40u, 0x20u);
    }

    return 0;
}
#endif /* UFM_EXAMPLE_MAIN */
```

---

## 10. Programming in Rust

Rust's ownership and type system make it well-suited for embedded I2C/UFm drivers.
The examples use the [`embedded-hal`](https://docs.rs/embedded-hal) trait ecosystem,
which is the standard abstraction layer for embedded Rust.

---

### 10.1 UFm Bit-Bang Driver using `embedded-hal` Traits

```rust
//! ufm_bitbang.rs
//!
//! UFm (5 MHz) bit-bang master using embedded-hal OutputPin and DelayNs.
//! No ACK phase. Push-pull GPIO assumed by the platform HAL.
//!
//! Dependencies (Cargo.toml):
//!   embedded-hal = "1.0"

#![no_std]

use embedded_hal::digital::OutputPin;
use embedded_hal::delay::DelayNs;

/// Error type for UFm operations.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UfmError<PinErr> {
    /// GPIO driver returned an error.
    Pin(PinErr),
    /// Slave address is in a reserved range.
    InvalidAddress,
}

impl<E> From<E> for UfmError<E> {
    fn from(e: E) -> Self { UfmError::Pin(e) }
}

/// Validates a 7-bit I2C/UFm slave address.
/// Reserved: 0x00–0x07 and 0x78–0x7F.
const fn is_valid_address(addr: u8) -> bool {
    addr >= 0x08 && addr <= 0x77
}

/// UFm bit-bang master.
///
/// `SCL` and `SDA` must be configured as push-pull outputs by the platform
/// before constructing this type. No pull-up resistors should be present.
pub struct UfmMaster<SCL, SDA, DELAY>
where
    SCL: OutputPin,
    SDA: OutputPin,
    DELAY: DelayNs,
{
    scl: SCL,
    sda: SDA,
    delay: DELAY,
}

impl<SCL, SDA, DELAY, PinErr> UfmMaster<SCL, SDA, DELAY>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    /// Half-period for 5 MHz SCL: 100 ns.
    const HALF_PERIOD_NS: u32 = 100;
    /// Data setup time before SCL rise: ≥ 5 ns.
    const T_SU_DAT_NS: u32 = 5;

    /// Create a new UFm master. Both pins are driven HIGH (idle state).
    pub fn new(mut scl: SCL, mut sda: SDA, mut delay: DELAY)
        -> Result<Self, PinErr>
    {
        scl.set_high()?;
        sda.set_high()?;
        delay.delay_ns(Self::HALF_PERIOD_NS * 2); // t_BUF
        Ok(Self { scl, sda, delay })
    }

    /// Transmit `data` bytes to the 7-bit slave address `addr`.
    ///
    /// Returns `UfmError::InvalidAddress` for reserved addresses.
    /// No acknowledgement is expected or checked.
    pub fn write(&mut self, addr: u8, data: &[u8])
        -> Result<(), UfmError<PinErr>>
    {
        if !is_valid_address(addr) {
            return Err(UfmError::InvalidAddress);
        }
        self.start()?;
        self.send_byte(addr << 1)?; // R/W = 0 (write)
        for &byte in data {
            self.send_byte(byte)?;
        }
        self.stop()?;
        Ok(())
    }

    /// Write using an iterator, avoiding intermediate buffer allocation.
    pub fn write_iter<I>(&mut self, addr: u8, bytes: I)
        -> Result<(), UfmError<PinErr>>
    where
        I: IntoIterator<Item = u8>,
    {
        if !is_valid_address(addr) {
            return Err(UfmError::InvalidAddress);
        }
        self.start()?;
        self.send_byte(addr << 1)?;
        for byte in bytes {
            self.send_byte(byte)?;
        }
        self.stop()?;
        Ok(())
    }

    // -----------------------------------------------------------------------
    // Private bus primitives
    // -----------------------------------------------------------------------

    fn start(&mut self) -> Result<(), PinErr> {
        self.sda.set_high()?;
        self.scl.set_high()?;
        self.delay.delay_ns(Self::HALF_PERIOD_NS); // t_SU;STA ≥ 20 ns
        self.sda.set_low()?;
        self.delay.delay_ns(Self::HALF_PERIOD_NS); // t_HD;STA ≥ 20 ns
        self.scl.set_low()?;
        Ok(())
    }

    fn stop(&mut self) -> Result<(), PinErr> {
        self.sda.set_low()?;
        self.scl.set_high()?;
        self.delay.delay_ns(Self::HALF_PERIOD_NS); // t_SU;STO ≥ 20 ns
        self.sda.set_high()?;
        self.delay.delay_ns(Self::HALF_PERIOD_NS * 2); // t_BUF
        Ok(())
    }

    /// Transmit one byte MSB-first. No ACK phase.
    fn send_byte(&mut self, byte: u8) -> Result<(), PinErr> {
        for bit in (0..8).rev() {
            if (byte >> bit) & 1 == 1 {
                self.sda.set_high()?;
            } else {
                self.sda.set_low()?;
            }
            // Data must be stable t_SU;DAT (≥5 ns) before SCL rises
            self.delay.delay_ns(Self::T_SU_DAT_NS);
            self.scl.set_high()?;
            self.delay.delay_ns(Self::HALF_PERIOD_NS);
            self.scl.set_low()?;
            self.delay.delay_ns(Self::HALF_PERIOD_NS - Self::T_SU_DAT_NS);
        }
        // No 9th clock: UFm has no ACK
        Ok(())
    }
}
```

---

### 10.2 UFm Transaction Builder with Typestate Pattern

This example uses Rust's typestate pattern to enforce correct transaction sequencing
at **compile time**: you cannot call `send_data()` before `send_address()`, and you
cannot call `commit()` (STOP) without going through both prior stages.

```rust
//! ufm_typestate.rs
//!
//! UFm transaction builder using Rust typestate pattern.
//! Enforces: Idle → Started → Addressed → Committed at compile time.

#![no_std]

use embedded_hal::digital::OutputPin;
use embedded_hal::delay::DelayNs;

// ── Typestate marker types ───────────────────────────────────────────────────

/// Bus is idle; no transaction in progress.
pub struct Idle;
/// START has been issued.
pub struct Started;
/// Address byte has been sent; data bytes may follow.
pub struct Addressed;

// ── Transaction builder ──────────────────────────────────────────────────────

pub struct UfmTransaction<'bus, SCL, SDA, DELAY, State>
where
    SCL: OutputPin,
    SDA: OutputPin,
    DELAY: DelayNs,
{
    bus: &'bus mut UfmBus<SCL, SDA, DELAY>,
    _state: core::marker::PhantomData<State>,
}

pub struct UfmBus<SCL, SDA, DELAY> {
    scl: SCL,
    sda: SDA,
    delay: DELAY,
}

impl<SCL, SDA, DELAY, PinErr> UfmBus<SCL, SDA, DELAY>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    const HALF_NS: u32 = 100;

    pub fn new(mut scl: SCL, mut sda: SDA, delay: DELAY) -> Result<Self, PinErr> {
        scl.set_high()?;
        sda.set_high()?;
        Ok(Self { scl, sda, delay })
    }

    /// Begin a new transaction, returning a typestate-guarded builder.
    pub fn transaction(&mut self) -> UfmTransaction<'_, SCL, SDA, DELAY, Idle> {
        UfmTransaction { bus: self, _state: core::marker::PhantomData }
    }
}

// ── Idle → Started ───────────────────────────────────────────────────────────

impl<'bus, SCL, SDA, DELAY, PinErr>
    UfmTransaction<'bus, SCL, SDA, DELAY, Idle>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    /// Issue START condition. Consumes self, returning a Started transaction.
    pub fn start(self) -> Result<UfmTransaction<'bus, SCL, SDA, DELAY, Started>, PinErr> {
        let bus = self.bus;
        bus.sda.set_high()?;
        bus.scl.set_high()?;
        bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS);
        bus.sda.set_low()?;
        bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS);
        bus.scl.set_low()?;
        Ok(UfmTransaction { bus, _state: core::marker::PhantomData })
    }
}

// ── Started → Addressed ──────────────────────────────────────────────────────

impl<'bus, SCL, SDA, DELAY, PinErr>
    UfmTransaction<'bus, SCL, SDA, DELAY, Started>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    /// Send the 7-bit address (R/W forced to 0).
    pub fn address(self, addr7: u8)
        -> Result<UfmTransaction<'bus, SCL, SDA, DELAY, Addressed>, PinErr>
    {
        let bus = self.bus;
        Self::send_byte_raw(bus, addr7 << 1)?;
        Ok(UfmTransaction { bus, _state: core::marker::PhantomData })
    }

    fn send_byte_raw(bus: &mut UfmBus<SCL, SDA, DELAY>, byte: u8) -> Result<(), PinErr> {
        for bit in (0..8u8).rev() {
            if (byte >> bit) & 1 == 1 { bus.sda.set_high()? } else { bus.sda.set_low()? }
            bus.delay.delay_ns(5);
            bus.scl.set_high()?;
            bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS);
            bus.scl.set_low()?;
            bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS - 5);
        }
        Ok(())
    }
}

// ── Addressed → Committed ────────────────────────────────────────────────────

impl<'bus, SCL, SDA, DELAY, PinErr>
    UfmTransaction<'bus, SCL, SDA, DELAY, Addressed>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    /// Send one data byte.
    pub fn send(self, byte: u8) -> Result<Self, PinErr> {
        Self::send_byte_raw(self.bus, byte)?;
        Ok(self)
    }

    /// Send a slice of data bytes.
    pub fn send_all(mut self, data: &[u8]) -> Result<Self, PinErr> {
        for &b in data {
            Self::send_byte_raw(self.bus, b)?;
        }
        Ok(self)
    }

    /// Issue STOP and return the bus to idle. Consumes the transaction.
    pub fn commit(self) -> Result<(), PinErr> {
        let bus = self.bus;
        bus.sda.set_low()?;
        bus.scl.set_high()?;
        bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS);
        bus.sda.set_high()?;
        bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS * 2);
        Ok(())
    }

    fn send_byte_raw(bus: &mut UfmBus<SCL, SDA, DELAY>, byte: u8) -> Result<(), PinErr> {
        for bit in (0..8u8).rev() {
            if (byte >> bit) & 1 == 1 { bus.sda.set_high()? } else { bus.sda.set_low()? }
            bus.delay.delay_ns(5);
            bus.scl.set_high()?;
            bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS);
            bus.scl.set_low()?;
            bus.delay.delay_ns(UfmBus::<SCL, SDA, DELAY>::HALF_NS - 5);
        }
        Ok(())
    }
}

// ── Usage example ────────────────────────────────────────────────────────────

/// Example: compile-time-correct UFm transactions.
///
/// ```rust
/// // This compiles and runs correctly:
/// bus.transaction()
///    .start()?
///    .address(0x38)?
///    .send_all(&[0x01, 0xFF, 0x80])?
///    .commit()?;
///
/// // This would NOT compile — cannot call .send() without .address() first:
/// // bus.transaction().start()?.send(0xFF); // ← compile error
/// ```
#[allow(dead_code)]
fn usage_example<SCL, SDA, DELAY, PinErr>(bus: &mut UfmBus<SCL, SDA, DELAY>)
    -> Result<(), PinErr>
where
    SCL: OutputPin<Error = PinErr>,
    SDA: OutputPin<Error = PinErr>,
    DELAY: DelayNs,
{
    // Method-chained, typestate-enforced transaction
    bus.transaction()
       .start()?
       .address(0x38)?
       .send_all(&[0x01u8, 0xFFu8, 0x80u8])?
       .commit()?;

    // Multiple separate sends (result is still Addressed state)
    bus.transaction()
       .start()?
       .address(0x60)?
       .send(0x00u8)?   // Register address
       .send(0xAAu8)?   // Value
       .send(0x55u8)?   // Value
       .commit()?;

    Ok(())
}
```

---

### 10.3 CRC-8 Application Layer Error Detection (Rust)

Since UFm provides no protocol-level ACK, adding an application-layer CRC is strongly
recommended for reliability. This example wraps the UFm driver with CRC-8/MAXIM:

```rust
//! ufm_crc.rs
//!
//! UFm write with CRC-8 appended (Polynomial 0x31, init 0x00).
//! The slave is expected to verify the CRC internally.

#![no_std]

/// CRC-8 (Dallas/Maxim, polynomial 0x31) computation.
pub fn crc8_maxim(data: &[u8]) -> u8 {
    let mut crc: u8 = 0x00;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ 0x31;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

/// Write `addr_and_data` to a UFm slave with a CRC-8 byte appended.
///
/// The frame sent on the bus is:
///   START | ADDR | addr_and_data[0] | ... | addr_and_data[N-1] | CRC8 | STOP
///
/// `addr_and_data` should begin with the slave register address, followed
/// by payload bytes. The slave address byte is included in the CRC calculation.
pub fn ufm_write_crc<SCL, SDA, DELAY, PinErr>(
    master: &mut crate::UfmMaster<SCL, SDA, DELAY>,
    addr7: u8,
    payload: &[u8],
) -> Result<(), crate::UfmError<PinErr>>
where
    SCL: embedded_hal::digital::OutputPin<Error = PinErr>,
    SDA: embedded_hal::digital::OutputPin<Error = PinErr>,
    DELAY: embedded_hal::delay::DelayNs,
{
    // Build CRC over address byte + payload
    let addr_byte = addr7 << 1; // W=0
    let mut crc = crc8_maxim(&[addr_byte]);
    crc = {
        let mut c = crc;
        for &b in payload { c = crc8_step(c, b); }
        c
    };

    // Transmit payload followed by CRC
    master.write_iter(
        addr7,
        payload.iter().copied().chain(core::iter::once(crc)),
    )
}

fn crc8_step(mut crc: u8, byte: u8) -> u8 {
    crc ^= byte;
    for _ in 0..8 {
        crc = if crc & 0x80 != 0 { (crc << 1) ^ 0x31 } else { crc << 1 };
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn crc8_known_vector() {
        // CRC-8/MAXIM of [0x02, 0x1C] should be 0xA2
        assert_eq!(crc8_maxim(&[0x02, 0x1C]), 0xA2);
    }
}
```

---

## 11. Mixed-Mode Bus Considerations

UFm devices must **never be placed on the same bus segment** as SM/FM/FM+/Hs devices.
The reasons are fundamental:

1. **Push-pull vs open-drain conflict**: If a UFm master drives SDA HIGH while an
   open-drain slave tries to pull it LOW for an ACK, both devices fight the line —
   potentially causing latch-up or permanent damage.

2. **No wired-AND**: Standard I2C relies on multiple devices being able to pull the bus
   low simultaneously (wired-AND). Push-pull drivers break this assumption.

3. **Speed incompatibility**: Even if electrical damage is avoided, SM/FM devices cannot
   operate at 5 MHz.

**If you need both UFm and standard I2C**, use separate bus segments with separate GPIO
pins, or a bus multiplexer/switch IC that completely isolates the segments.

---

## 12. Summary

| Topic | Key Points |
|---|---|
| **Speed** | 5 MHz SCL — highest in the I2C family; 5× faster than FM+ |
| **Direction** | Write-only; master→slave exclusively; no reads possible |
| **Drivers** | Push-pull mandatory; no pull-up resistors; series Rs for edge control |
| **ACK** | Eliminated entirely; slaves cannot pull SDA; no error feedback |
| **Clock stretch** | Not permitted; slaves must keep up or buffer internally |
| **Multi-master** | Not supported; bus arbitration requires slave drive capability |
| **Addressing** | Standard 7-bit / 10-bit I2C address space; same reserved ranges |
| **Error detection** | Must be done at application layer (CRC, watchdog, redundancy) |
| **PCB constraints** | Short traces (<5 cm), minimal vias, controlled impedance recommended |
| **Isolation** | Never mix UFm and open-drain I2C devices on the same bus segment |
| **Best use cases** | LED drivers, DACs, display command streaming, actuator control |
| **C/C++ approach** | Register-level HAL or GPIO bit-bang; C++ RAII Transaction guard |
| **Rust approach** | `embedded-hal` traits; typestate builder enforces order at compile time |
| **CRC** | Append CRC-8 to every frame for application-level integrity checking |

UFm is a specialised protocol that deliberately sacrifices the flexibility of
bidirectional I2C in exchange for raw write throughput. When your application needs to
stream commands or configuration data to a peripheral at the highest possible I2C speed,
and read-back is not required, UFm is the optimal choice. Its push-pull bus, eliminated
ACK phase, and 5 MHz clock combine to deliver a lean, high-speed write channel, provided
the surrounding system is designed with its strict electrical and layout constraints in
mind.