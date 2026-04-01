# 52. I²C High-Speed Mode Details

**Master Code Transmission** — the reserved address `0x08`–`0x0F` is transmitted at F/S speed, the NACK on bit 9 is by design (not an error), and a Repeated START immediately follows to switch the bus to 3.4 MHz. Multi-master arbitration uses unique master codes per master with standard wired-AND resolution.

**Current Source Pull-Ups** — explains why resistive pull-ups fail mathematically (RC time constant), the switchable current source topology (enabled only during LOW→HIGH transitions), and the comparator-threshold disable mechanism. Covers both discrete and integrated implementations.

**3.4 MHz Operation Specifics** — asymmetric timing (t_LOW ≥ 160 ns, t_HIGH ≥ 60 ns), the strict no-clock-stretching rule for slaves, STOP vs. Repeated START session management, and mixed-speed bus coexistence with legacy F/S devices.

The code examples include:
- **C (Linux userspace)** — `I2C_RDWR` with `I2C_M_IGNORE_NAK` for the master code NACK, combined write-read transactions
- **C (bare-metal STM32H7)** — `TIMINGR` register calculation from scratch, register-level master code + write + read sequences with timeout loops
- **Rust (Linux / `linux-embedded-hal`)** — type-safe session manager with `HsMasterCode` enum, `Drop`-based session cleanup
- **Rust (bare-metal)** — `unsafe` volatile register driver compatible with Embassy/RTIC, full CR2 builder function

> **Specification Reference:** I²C-bus specification and user manual (UM10204), §5.3 — HS-mode  
> **Maximum Bit Rate:** 3.4 Mbit/s  
> **Backward Compatible With:** Standard (100 kHz), Fast (400 kHz), Fast-Plus (1 MHz)

---

## Table of Contents

1. [Overview](#1-overview)
2. [Electrical Characteristics and Timing](#2-electrical-characteristics-and-timing)
3. [Master Code Transmission](#3-master-code-transmission)
4. [Current Source Pull-Ups](#4-current-source-pull-ups)
5. [HS-Mode Protocol Flow](#5-hs-mode-protocol-flow)
6. [Mixed-Speed Bus Architecture](#6-mixed-speed-bus-architecture)
7. [Code Examples in C/C++](#7-code-examples-in-cc)
8. [Code Examples in Rust](#8-code-examples-in-rust)
9. [Common Pitfalls](#9-common-pitfalls)
10. [Summary](#10-summary)

---

## 1. Overview

High-Speed (HS) mode extends the I²C bus to **3.4 Mbit/s**, enabling high-throughput communication
between processors, FPGAs, and peripherals such as ADCs, DACs, image sensors, and EEPROMs without
resorting to SPI or proprietary parallel buses.

HS-mode is **not** a separate protocol — it is a **negotiated upgrade** from an existing F/S-mode
(Fast/Standard) session. The bus always starts in F/S-mode, and a special **Master Code** is
transmitted to signal the switch to HS electrical and timing characteristics.

### Key Distinctions from Lower-Speed Modes

| Property                  | Standard (SM) | Fast (FM) | Fast-Plus (FM+) | High-Speed (HS) |
|---------------------------|:-------------:|:---------:|:---------------:|:---------------:|
| Max bit rate              | 100 kbit/s    | 400 kbit/s | 1 Mbit/s       | 3.4 Mbit/s      |
| Pull-up type              | Resistive     | Resistive | Resistive       | **Current source** |
| Clock stretching (slave)  | Yes           | Yes       | Yes             | **No**          |
| START before address      | Standard START| Standard  | Standard        | **Repeated START** |
| Master Code required      | No            | No        | No              | **Yes**         |
| CBUS compatibility        | Yes           | No        | No              | No              |
| Spike filter required     | Optional      | Yes       | Yes             | Yes             |

---

## 2. Electrical Characteristics and Timing

### 2.1 Timing Parameters (V_DD = 3.3 V typical)

| Symbol    | Parameter                        | Min     | Max     | Unit |
|-----------|----------------------------------|---------|---------|------|
| f_SCL     | SCL clock frequency              | 0       | 3400    | kHz  |
| t_LOW     | SCL low period                   | 160     | —       | ns   |
| t_HIGH    | SCL high period                  | 60      | —       | ns   |
| t_r       | Rise time (with current source)  | 10      | 40      | ns   |
| t_f       | Fall time                        | 10      | 80      | ns   |
| t_SU;DAT  | Data setup time                  | 10      | —       | ns   |
| t_HD;DAT  | Data hold time                   | 0       | 70      | ns   |
| C_b       | Capacitive load per bus line     | —       | 100     | pF   |

> **Note:** The asymmetric t_LOW/t_HIGH ratio (160 ns vs 60 ns) reflects the need for the current
> source to charge bus capacitance quickly during the high phase, while keeping the low phase long
> enough for all devices to drive SCL/SDA low reliably.

### 2.2 Signal Integrity at 3.4 MHz

At 3.4 Mbit/s the full bit period is ~294 ns. With a 100 pF bus capacitance and a 3 mA current
source, the rise time is approximately:

```
t_r = C_b × ΔV / I_source = 100e-12 × 3.3 / 3e-3 ≈ 110 ns  (too slow!)
```

This is why HS-mode **mandates current source pull-ups** and **limits C_b to 100 pF** — resistive
pull-ups simply cannot charge bus capacitance fast enough. With a 3 mA current source driving to
VDD through a series resistor (for overshoot damping), the effective rise time drops to the 10–40 ns
range required by the spec.

---

## 3. Master Code Transmission

### 3.1 What Is the Master Code?

The Master Code is an **8-bit reserved address byte** transmitted in F/S-mode at the beginning of
an HS-mode transaction. It serves two purposes:

1. **Announces** that an HS-capable master wants to switch to 3.4 Mbit/s operation.
2. **Identifies the master** on multi-master buses (each HS master has a unique code).

Master codes occupy the reserved address space `0000 1XXX` (0x08–0x0F):

| Code  | Binary       | Hex  |
|-------|--------------|------|
| MC_0  | 0000 1000    | 0x08 |
| MC_1  | 0000 1001    | 0x09 |
| MC_2  | 0000 1010    | 0x0A |
| MC_3  | 0000 1011    | 0x0B |
| MC_4  | 0000 1100    | 0x0C |
| MC_5  | 0000 1101    | 0x0D |
| MC_6  | 0000 1110    | 0x0E |
| MC_7  | 0000 1111    | 0x0F |

### 3.2 Master Code Transmission Sequence

```
F/S-mode:
  ┌─────────┐   ┌───────────────────────┐   ┌──────────────────────────────────────────┐
  │  START  │→  │  Master Code (0x0Y)   │→  │  No ACK expected — switch to HS timing  │
  └─────────┘   │  (8 bits, F/S speed)  │   └──────────────────────────────────────────┘
                └───────────────────────┘

HS-mode (immediately after):
  ┌──────────────────┐   ┌────────────────────┐   ┌───────┐   ┌──────┐   ┌─────┐
  │  Repeated START  │→  │  Slave Address + R/W│→  │  ACK  │→  │ Data │→  │ ... │
  └──────────────────┘   │  (HS speed 3.4MHz)  │   └───────┘   └──────┘   └─────┘
```

**Critical rules for Master Code transmission:**

- Transmitted at **F/S-mode speed** (not HS speed).
- The **ACK bit is not generated** by any slave — the 9th clock pulse is a NACK by design.
  HS-capable slaves recognize the master code and internally switch to HS receive mode.
- Non-HS slaves **ignore** the master code (reserved address space, no ACK).
- Immediately after the NACK 9th bit, the master issues a **Repeated START (Sr)** and transitions
  all timing to HS-mode.
- The master code byte is **not** a read or write to any device; it carries no data payload.

### 3.3 Multi-Master Arbitration with Master Codes

In multi-master configurations each HS master is assigned a **unique master code** (MC_0..MC_7).
During arbitration (which occurs in F/S-mode, before HS switchover):

- Masters transmit their master codes simultaneously.
- Standard I²C wired-AND arbitration applies: a master that transmits a `1` but reads back a `0`
  has lost arbitration and must release the bus.
- The winning master (whose code was transmitted without collision) proceeds to issue Sr and switches
  to HS-mode.
- Losing masters **do not generate a START** — they wait and monitor until the bus is free.

```
Master A transmits: 0000 1010  (0x0A)
Master B transmits: 0000 1001  (0x09)

Bit 1 collision at bit position 1 (value '1' vs '0'):
  Master A reads '0' → loses arbitration, releases bus
  Master B continues → wins, proceeds to HS-mode
```

---

## 4. Current Source Pull-Ups

### 4.1 Why Resistive Pull-Ups Fail at 3.4 MHz

A standard resistive pull-up forms an RC circuit with the bus capacitance. For a 10 kΩ pull-up and
100 pF bus capacitance:

```
τ = RC = 10,000 × 100e-12 = 1 µs  (63% rise in 1 µs — far too slow for 294 ns bit periods)
```

Even a 1 kΩ pull-up gives τ = 100 ns, which struggles to meet the t_HIGH = 60 ns minimum at full
load. Additionally, the static current draw through a 1 kΩ resistor to GND is 3.3 mA constantly —
wasting power and heating the resistor.

### 4.2 Current Source Pull-Up Topology

HS-mode mandates **switchable current source pull-ups** on SDA and SCL. The topology is:

```
VDD ──┬────────────────────────────────────────
      │
   [R_s]  ← Series damping resistor (10–50 Ω)
      │                        ┌─ Spike filter
      ├──── I_source (3 mA) ───┤
      │     (enabled during    └─ SCL/SDA line ──── to slave devices
      │      HIGH phase)
   [R_weak] ← Weak resistive pull-up (100 kΩ)
      │       (always on, for bus-idle state)
      │
     SCL/SDA open-drain drivers (masters + slaves)
```

**Operation:**

| Phase         | Current Source | Weak Pull-up | Net Effect                        |
|---------------|:--------------:|:------------:|-----------------------------------|
| LOW (driven)  | Disabled       | Active       | Line held low by driver           |
| Transition    | Enabled        | Active       | Fast charge of C_b                |
| HIGH (stable) | Disabled       | Active       | Weak hold, saves power            |
| Bus idle      | Disabled       | Active       | Bus floats high via weak resistor |

The current source is **only enabled during the SCL/SDA transition from LOW to HIGH**, then disabled
once the line reaches V_IH. This minimizes power dissipation and prevents overshoot at the receiver.

### 4.3 Current Source Enable Timing

The master controls the current source via an internal enable signal tied to the SCL output driver:

```
SCL driver:  _________|‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾|___________
I_src enable:_________|‾‾‾‾‾|_________________|‾‾‾‾‾|___
                      ↑     ↑                 ↑     ↑
                    LOW→HIGH I_src off      LOW→HIGH I_src off
                    (charge) (hold)         (charge)  (hold)
```

In practice, the current source enable is often implemented as a **comparator threshold**: once
SCL/SDA rises above ~0.7 × VDD, the comparator disables the current source and the weak pull-up
holds the line.

### 4.4 Practical Implementation: Discrete vs Integrated

**Discrete implementation** (for custom hardware):

- PMOS transistor driven by a level-shifted comparator output
- Gate-source voltage set to limit I_D to ~3 mA
- Series damping resistor (22 Ω typical) to prevent ringing

**Integrated implementation:**

- Most HS-capable I²C controllers (e.g., NXP PCA9615, TI TUSB3216, STM32H7 I²C peripheral) include
  the current source internally.
- The `I2C_TIMINGR` register (STM32) or equivalent configures the current source drive strength.

---

## 5. HS-Mode Protocol Flow

### 5.1 Complete Transaction Diagram

```
Idle ──→ START ──→ [F/S speed] Master Code ──→ NACK(9th) ──→ Sr ──→ [HS speed] ...

[HS speed]:
  Slave Addr (7-bit) + R/W ──→ ACK ──→ [optional: register/sub-addr] ──→ Data ──→ ... ──→ STOP
```

In detail:

```
SDA: ‾‾‾\__MC7__MC6__MC5__MC4__MC3__MC2__MC1__MC0__NACK_/‾‾‾\_ADDR_...
SCL: ‾‾‾‾\_1__2__3__4__5__6__7__8__9(NACK)_/‾‾‾\_10_11_12_...
          ←──────── F/S speed ──────────→ Sr  ←───── HS speed ──────→

         [Master Code = 0x0Y]                [Normal I²C from here]
```

### 5.2 Repeated START in HS-Mode

During an HS-mode session (after the master code), a **Repeated START** keeps the bus in HS-mode.
A **STOP followed by a new START** drops back to F/S-mode, requiring a new master code for the
next HS transaction.

```
HS session spanning multiple transfers:

  [START][MC][NACK][Sr][ADDR1+W][ACK][DATA...][Sr][ADDR2+R][ACK][DATA...][STOP]
  ←F/S→        ←─────────────── HS-mode ──────────────────────────────→ ←F/S→
```

### 5.3 Clock Stretching: NOT Allowed in HS-Mode

In SM/FM modes, slaves may hold SCL LOW to request more processing time. In HS-mode, **clock
stretching by slaves is forbidden** — it would violate timing and corrupt the high-speed waveform.
Only the **master** controls SCL in HS-mode. Slaves must be able to process data at 3.4 Mbit/s
without requesting additional time.

---

## 6. Mixed-Speed Bus Architecture

### 6.1 Topology

HS-capable masters and slaves can coexist with legacy F/S devices on the same physical bus, because:

- The master code uses a **reserved address** that F/S slaves ignore (no ACK, no response).
- HS-mode transactions use **Repeated START** (not STOP/START), so F/S slaves never see an address
  during an HS transaction — they remain idle.
- When the HS master issues a **STOP**, F/S devices resume normal operation.

```
             VDD
              │
         [Current source pull-ups]
              │
SCL ──────────┼────┬─────────┬──────────┬──────────
              │    │         │          │
SDA ──────────┼────┼────┬────┼─────┬────┼─────
              │    │    │    │     │    │
         [HS Master] [HS Slave] [FM Slave] [SM Slave]
```

### 6.2 Speed Negotiation at Boot

```
1. All devices on bus; master starts in F/S mode (≤400 kHz or ≤1 MHz FM+)
2. Master transmits Master Code → slaves recognize HS intent
3. Sr issued → HS timing takes effect
4. All HS-capable devices communicate at 3.4 Mbit/s
5. F/S-only devices are dormant during HS phase (ignored master code, no address match)
6. STOP issued → bus returns to F/S mode for any mixed-speed communication
```

---

## 7. Code Examples in C/C++

### 7.1 Linux Kernel / Userspace: Bit-Banged HS-Mode (Educational)

The following example demonstrates the **logical sequence** of HS-mode startup using Linux I²C
userspace APIs. Note: true 3.4 MHz operation requires hardware I²C controller support; pure
bit-banging in userspace cannot achieve the timing precision required.

```c
/**
 * i2c_hs_mode.c
 *
 * Demonstrates the logical sequence for entering I²C High-Speed mode.
 * Uses Linux i2c-dev interface for hardware-accelerated I²C.
 *
 * Hardware: Raspberry Pi / BeagleBone with HS-capable I²C controller
 * Compile:  gcc -o i2c_hs_mode i2c_hs_mode.c
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ── Master Code definitions ─────────────────────────────────────────── */

#define I2C_HS_MASTER_CODE_BASE   0x08u   /* 0000 1000 */
#define I2C_HS_MASTER_ID          0u      /* This master's ID: 0..7       */
#define I2C_HS_MASTER_CODE        (I2C_HS_MASTER_CODE_BASE | I2C_HS_MASTER_ID)

/* ── HS-mode flag for i2c_msg ────────────────────────────────────────── */
/* Not universally supported — check your kernel version */
#ifndef I2C_M_HS
#define I2C_M_HS  0x0400
#endif

/* ── Result type ─────────────────────────────────────────────────────── */

typedef enum {
    HS_OK                 =  0,
    HS_ERR_OPEN           = -1,
    HS_ERR_NO_HS_SUPPORT  = -2,
    HS_ERR_MASTER_CODE    = -3,
    HS_ERR_TRANSFER       = -4,
} hs_result_t;

/* ── Context ─────────────────────────────────────────────────────────── */

typedef struct {
    int      fd;
    uint8_t  master_code;
    bool     hs_active;
    uint32_t bus_frequency_hz;  /* HS = 3_400_000 */
} hs_i2c_ctx_t;

/* ─────────────────────────────────────────────────────────────────────── */
/* Open I²C bus and verify HS capability                                   */
/* ─────────────────────────────────────────────────────────────────────── */

hs_result_t hs_i2c_open(hs_i2c_ctx_t *ctx, const char *dev, uint8_t master_id)
{
    unsigned long funcs = 0;

    ctx->fd = open(dev, O_RDWR);
    if (ctx->fd < 0) {
        perror("open");
        return HS_ERR_OPEN;
    }

    /* Query adapter capabilities */
    if (ioctl(ctx->fd, I2C_FUNCS, &funcs) < 0) {
        perror("I2C_FUNCS");
        close(ctx->fd);
        return HS_ERR_OPEN;
    }

    printf("[HS-I2C] Adapter capabilities: 0x%08lX\n", funcs);

    /* Check for high-speed support */
    if (!(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr, "[HS-I2C] Adapter does not support raw I2C_RDWR\n");
        close(ctx->fd);
        return HS_ERR_NO_HS_SUPPORT;
    }

    ctx->master_code     = (uint8_t)(I2C_HS_MASTER_CODE_BASE | (master_id & 0x07u));
    ctx->hs_active       = false;
    ctx->bus_frequency_hz = 3400000u;

    printf("[HS-I2C] Master code: 0x%02X\n", ctx->master_code);
    return HS_OK;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Transmit master code in F/S-mode to initiate HS session                 */
/*                                                                          */
/* The master code is sent as a write with no data bytes (just the address  */
/* byte itself). The NACK from slaves is expected and handled as success.   */
/* ─────────────────────────────────────────────────────────────────────── */

hs_result_t hs_i2c_enter_hs_mode(hs_i2c_ctx_t *ctx)
{
    /*
     * Construct a minimal I²C message that sends only the master code byte.
     * Many Linux i2c drivers handle the HS master code via I2C_M_HS flag;
     * where unsupported we simulate with a zero-byte write to the reserved addr.
     */

    uint8_t dummy = 0x00;

    struct i2c_msg msgs[1] = {
        {
            .addr  = ctx->master_code,   /* 0x08..0x0F — reserved HS space */
            .flags = I2C_M_IGNORE_NAK,   /* Expect NACK — do not treat as error */
            .len   = 0,                  /* No data payload */
            .buf   = &dummy,
        },
    };

    struct i2c_rdwr_ioctl_data xfer = {
        .msgs  = msgs,
        .nmsgs = 1,
    };

    int ret = ioctl(ctx->fd, I2C_RDWR, &xfer);
    if (ret < 0 && errno != ENXIO) {
        /* ENXIO (no device) is expected — master code has no ACK */
        perror("[HS-I2C] Master code transmission failed");
        return HS_ERR_MASTER_CODE;
    }

    ctx->hs_active = true;
    printf("[HS-I2C] Master code 0x%02X sent. HS session active.\n", ctx->master_code);
    return HS_OK;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Perform a combined write-then-read in HS-mode                           */
/*                                                                          */
/* The sequence is:                                                          */
/*   [START][MasterCode][NACK][Sr][SlaveAddr+W][ACK][RegAddr][ACK]         */
/*   [Sr][SlaveAddr+R][ACK][Data...][NACK][STOP]                            */
/* ─────────────────────────────────────────────────────────────────────── */

hs_result_t hs_i2c_write_read(hs_i2c_ctx_t *ctx,
                               uint8_t       slave_addr,
                               const uint8_t *write_buf,
                               uint16_t      write_len,
                               uint8_t       *read_buf,
                               uint16_t      read_len)
{
    if (!ctx->hs_active) {
        hs_result_t r = hs_i2c_enter_hs_mode(ctx);
        if (r != HS_OK) return r;
    }

    /* 3 messages:
     *  [0] Master code (F/S-mode, NACK expected)
     *  [1] Write phase (HS-mode after Sr)
     *  [2] Read phase  (HS-mode after Sr)
     */
    uint8_t dummy = 0;

    struct i2c_msg msgs[3] = {
        /* Master code — F/S speed, NACK ignored */
        {
            .addr  = ctx->master_code,
            .flags = I2C_M_IGNORE_NAK,
            .len   = 0,
            .buf   = &dummy,
        },
        /* Write phase — slave address + register + data */
        {
            .addr  = slave_addr,
            .flags = 0,                  /* Write */
            .len   = write_len,
            .buf   = (uint8_t *)write_buf,
        },
        /* Read phase — Repeated START, slave address + read */
        {
            .addr  = slave_addr,
            .flags = I2C_M_RD,           /* Read */
            .len   = read_len,
            .buf   = read_buf,
        },
    };

    struct i2c_rdwr_ioctl_data xfer = {
        .msgs  = msgs,
        .nmsgs = (read_len > 0) ? 3 : 2,
    };

    int ret = ioctl(ctx->fd, I2C_RDWR, &xfer);
    if (ret < 0) {
        perror("[HS-I2C] HS transfer failed");
        ctx->hs_active = false;   /* Assume session ended on error */
        return HS_ERR_TRANSFER;
    }

    return HS_OK;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* End HS session (issue STOP → bus returns to F/S-mode)                   */
/* ─────────────────────────────────────────────────────────────────────── */

void hs_i2c_end_session(hs_i2c_ctx_t *ctx)
{
    /*
     * The STOP condition is generated automatically by the kernel driver
     * after the last i2c_msg in an I2C_RDWR transaction. So ending the
     * session simply means marking our context as inactive.
     */
    ctx->hs_active = false;
    printf("[HS-I2C] Session ended. Bus returned to F/S-mode.\n");
}

void hs_i2c_close(hs_i2c_ctx_t *ctx)
{
    hs_i2c_end_session(ctx);
    close(ctx->fd);
    ctx->fd = -1;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Example usage                                                            */
/* ─────────────────────────────────────────────────────────────────────── */

int main(void)
{
    hs_i2c_ctx_t ctx = {0};

    /* Open I²C-1 bus, master ID = 0 (uses master code 0x08) */
    if (hs_i2c_open(&ctx, "/dev/i2c-1", I2C_HS_MASTER_ID) != HS_OK) {
        return 1;
    }

    /* Example: read 4 bytes from register 0x00 of slave at address 0x50 */
    const uint8_t reg_addr  = 0x00;
    uint8_t       rx_buf[4] = {0};

    hs_result_t r = hs_i2c_write_read(&ctx,
                                       0x50,
                                       &reg_addr, 1,
                                       rx_buf,    sizeof(rx_buf));
    if (r == HS_OK) {
        printf("[HS-I2C] Read: %02X %02X %02X %02X\n",
               rx_buf[0], rx_buf[1], rx_buf[2], rx_buf[3]);
    }

    hs_i2c_close(&ctx);
    return (r == HS_OK) ? 0 : 1;
}
```

---

### 7.2 Bare-Metal C: STM32H7 HS-Mode Configuration

STM32H7 has a native HS-capable I²C peripheral (`I2C4`) with internal current source support.
Below is the register-level configuration using CMSIS-style direct register access.

```c
/**
 * stm32h7_i2c_hs.c
 *
 * Configures STM32H7 I2C4 peripheral for 3.4 Mbit/s High-Speed mode.
 * Uses direct CMSIS register access — no HAL dependency.
 *
 * Assumes:
 *   - HSE = 25 MHz, PLL configured to give I2C4 kernel clock = 48 MHz
 *   - SCL on PF14 (AF4), SDA on PF15 (AF4)
 */

#include <stdint.h>
#include <stdbool.h>

/* ── Minimal CMSIS-style register definitions ───────────────────────── */

typedef struct {
    volatile uint32_t CR1;       /* 0x00 Control register 1    */
    volatile uint32_t CR2;       /* 0x04 Control register 2    */
    volatile uint32_t OAR1;      /* 0x08 Own address 1         */
    volatile uint32_t OAR2;      /* 0x0C Own address 2         */
    volatile uint32_t TIMINGR;   /* 0x10 Timing register       */
    volatile uint32_t TIMEOUTR;  /* 0x14 Timeout register      */
    volatile uint32_t ISR;       /* 0x18 Interrupt status      */
    volatile uint32_t ICR;       /* 0x1C Interrupt clear       */
    volatile uint32_t PECR;      /* 0x20 PEC register          */
    volatile uint32_t RXDR;      /* 0x24 Receive data          */
    volatile uint32_t TXDR;      /* 0x28 Transmit data         */
} I2C_TypeDef;

#define I2C4_BASE   0x58001C00UL
#define I2C4        ((I2C_TypeDef *)I2C4_BASE)

/* RCC registers (simplified) */
#define RCC_BASE    0x58024400UL
typedef struct {
    volatile uint32_t _pad[0x98/4];
    volatile uint32_t APB4ENR;  /* 0x98 */
} RCC_TypeDef;
#define RCC         ((RCC_TypeDef *)RCC_BASE)
#define RCC_APB4ENR_I2C4EN  (1u << 7)

/* GPIOF registers (simplified) */
#define GPIOF_BASE  0x58021400UL
typedef struct {
    volatile uint32_t MODER;
    volatile uint32_t OTYPER;
    volatile uint32_t OSPEEDR;
    volatile uint32_t PUPDR;
    volatile uint32_t IDR;
    volatile uint32_t ODR;
    volatile uint32_t BSRR;
    volatile uint32_t LCKR;
    volatile uint32_t AFR[2];
} GPIO_TypeDef;
#define GPIOF       ((GPIO_TypeDef *)GPIOF_BASE)

/* ── I2C CR1 bit definitions ─────────────────────────────────────────── */
#define I2C_CR1_PE          (1u << 0)   /* Peripheral enable              */
#define I2C_CR1_ANFOFF      (1u << 12)  /* Analog noise filter OFF        */
#define I2C_CR1_DNF_SHIFT   8u          /* Digital noise filter bits[11:8]*/

/* ── I2C CR2 bit definitions ─────────────────────────────────────────── */
#define I2C_CR2_AUTOEND     (1u << 25)
#define I2C_CR2_RELOAD      (1u << 24)
#define I2C_CR2_NBYTES_SHIFT 16u
#define I2C_CR2_NACK        (1u << 15)
#define I2C_CR2_STOP        (1u << 14)
#define I2C_CR2_START       (1u << 13)
#define I2C_CR2_HEAD10R     (1u << 12)
#define I2C_CR2_ADD10       (1u << 11)
#define I2C_CR2_RD_WRN      (1u << 10)
#define I2C_CR2_SADD_MASK   0x3FFu

/* ── I2C ISR bit definitions ─────────────────────────────────────────── */
#define I2C_ISR_TXE         (1u << 0)
#define I2C_ISR_TXIS        (1u << 1)
#define I2C_ISR_RXNE        (1u << 2)
#define I2C_ISR_ADDR        (1u << 3)
#define I2C_ISR_NACKF       (1u << 4)
#define I2C_ISR_STOPF       (1u << 5)
#define I2C_ISR_TC          (1u << 6)
#define I2C_ISR_TCR         (1u << 7)
#define I2C_ISR_BERR        (1u << 8)
#define I2C_ISR_ARLO        (1u << 9)
#define I2C_ISR_BUSY        (1u << 15)

/* ── HS Master Code ──────────────────────────────────────────────────── */
#define HS_MASTER_CODE  0x08u   /* Master code for master ID 0 */

/* ─────────────────────────────────────────────────────────────────────── */
/*                                                                          */
/*  TIMINGR calculation for 3.4 Mbit/s with I2CCLK = 48 MHz               */
/*                                                                          */
/*  I2CCLK period = 1/48MHz ≈ 20.83 ns                                     */
/*                                                                          */
/*  HS-mode requirements:                                                   */
/*    t_HIGH  ≥ 60 ns  → SCLH ≥ 60/20.83 ≈ 3 → use SCLH = 3              */
/*    t_LOW   ≥ 160 ns → SCLL ≥ 160/20.83 ≈ 8 → use SCLL = 8             */
/*    t_SDADEL (data hold) = 0 (HS allows 0)                               */
/*    t_SCLDEL (data setup) = 10 ns → SCLDEL ≥ 1                          */
/*    PRESC = 0 (no prescaler for maximum resolution)                       */
/*                                                                          */
/*  TIMINGR[31:28] = PRESC   = 0                                            */
/*  TIMINGR[23:20] = SCLDEL  = 1                                            */
/*  TIMINGR[19:16] = SDADEL  = 0                                            */
/*  TIMINGR[15:8]  = SCLH    = 3                                            */
/*  TIMINGR[7:0]   = SCLL    = 8                                            */
/*                                                                          */
/*  Resulting bit rate = 48MHz / (0+1) / (3+8+2) ≈ 3.69 Mbit/s            */
/*  (The +2 accounts for sync logic — adjust SCLH/SCLL to taste)           */
/* ─────────────────────────────────────────────────────────────────────── */

#define I2C_TIMINGR_HS_48MHZ   \
    ((0u  << 28) |  /* PRESC  = 0 */  \
     (1u  << 20) |  /* SCLDEL = 1 */  \
     (0u  << 16) |  /* SDADEL = 0 */  \
     (3u  <<  8) |  /* SCLH   = 3 */  \
     (8u  <<  0))   /* SCLL   = 8 */

/* ─────────────────────────────────────────────────────────────────────── */
/* GPIO configuration for I2C4: PF14=SCL, PF15=SDA                        */
/* ─────────────────────────────────────────────────────────────────────── */

static void gpio_i2c4_init(void)
{
    /* Enable GPIOF clock (AHB4ENR bit 5) */
    *(volatile uint32_t *)(RCC_BASE + 0xE0) |= (1u << 5);

    /* PF14, PF15: Alternate function mode (MODER = 10) */
    GPIOF->MODER &= ~((3u << 28) | (3u << 30));
    GPIOF->MODER |=  ((2u << 28) | (2u << 30));

    /* Open-drain output type (required for I²C) */
    GPIOF->OTYPER |= (1u << 14) | (1u << 15);

    /* Very high speed (OSPEEDR = 11) */
    GPIOF->OSPEEDR |= (3u << 28) | (3u << 30);

    /* No pull-up/pull-down (current source provides pull-up) */
    GPIOF->PUPDR &= ~((3u << 28) | (3u << 30));

    /* AF4 for I2C4 on PF14/PF15 */
    GPIOF->AFR[1] &= ~((0xFu << 24) | (0xFu << 28));
    GPIOF->AFR[1] |=  ((4u   << 24) | (4u   << 28));
}

/* ─────────────────────────────────────────────────────────────────────── */
/* I2C4 peripheral initialization for HS-mode                              */
/* ─────────────────────────────────────────────────────────────────────── */

void i2c4_hs_init(void)
{
    /* Enable I2C4 peripheral clock */
    RCC->APB4ENR |= RCC_APB4ENR_I2C4EN;

    /* Configure GPIO */
    gpio_i2c4_init();

    /* Disable peripheral during configuration */
    I2C4->CR1 &= ~I2C_CR1_PE;

    /* Configure timing for 3.4 Mbit/s */
    I2C4->TIMINGR = I2C_TIMINGR_HS_48MHZ;

    /* CR1: Enable analog filter, no digital filter, no interrupts yet */
    I2C4->CR1 = 0u;  /* Analog filter on by default (ANFOFF=0) */

    /* Enable peripheral */
    I2C4->CR1 |= I2C_CR1_PE;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Wait helper with simple timeout                                          */
/* ─────────────────────────────────────────────────────────────────────── */

static bool wait_flag(volatile uint32_t *reg, uint32_t flag, uint32_t timeout_us)
{
    /* In bare-metal: replace with SysTick-based timeout */
    volatile uint32_t t = timeout_us * 10u;
    while (!(*reg & flag)) {
        if (--t == 0) return false;
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Transmit HS Master Code (F/S-mode, expect NACK)                         */
/* ─────────────────────────────────────────────────────────────────────── */

bool i2c4_send_master_code(void)
{
    /* CR2: address = HS_MASTER_CODE, 0 bytes, generate START */
    I2C4->CR2 = ((0u & 0xFFu) << I2C_CR2_NBYTES_SHIFT) |  /* NBYTES = 0     */
                ((HS_MASTER_CODE & 0x7Fu) << 1u)         |  /* SADD           */
                I2C_CR2_START;                               /* Generate START */

    /* Wait for NACK (expected — no slave acknowledges master code) */
    if (!wait_flag(&I2C4->ISR, I2C_ISR_NACKF, 1000u)) {
        return false;   /* Timeout */
    }

    /* Clear NACK flag */
    I2C4->ICR = I2C_ISR_NACKF;

    /* The hardware automatically issues a Repeated START for the next
     * transaction. The bus is now in HS-mode. */

    return true;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* HS-mode write: [MasterCode][Sr][ADDR+W][ACK][reg][data...]              */
/* ─────────────────────────────────────────────────────────────────────── */

bool i2c4_hs_write(uint8_t slave_addr, uint8_t reg, const uint8_t *data, uint8_t len)
{
    /* Step 1: Send master code */
    if (!i2c4_send_master_code()) {
        return false;
    }

    /* Step 2: Set up HS write — slave address, byte count = len+1 (reg + data) */
    I2C4->CR2 = (((uint32_t)(len + 1u) << I2C_CR2_NBYTES_SHIFT)) |
                ((uint32_t)(slave_addr & 0x7Fu) << 1u)            |
                I2C_CR2_AUTOEND                                    |
                I2C_CR2_START;

    /* Step 3: Send register address */
    if (!wait_flag(&I2C4->ISR, I2C_ISR_TXIS, 1000u)) return false;
    I2C4->TXDR = reg;

    /* Step 4: Send data bytes */
    for (uint8_t i = 0; i < len; i++) {
        if (!wait_flag(&I2C4->ISR, I2C_ISR_TXIS, 1000u)) return false;
        I2C4->TXDR = data[i];
    }

    /* Step 5: Wait for STOP (auto-generated by AUTOEND) */
    if (!wait_flag(&I2C4->ISR, I2C_ISR_STOPF, 5000u)) return false;
    I2C4->ICR = I2C_ISR_STOPF;

    return true;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* HS-mode read: [MasterCode][Sr][ADDR+W][ACK][reg][Sr][ADDR+R][ACK][data]*/
/* ─────────────────────────────────────────────────────────────────────── */

bool i2c4_hs_read(uint8_t slave_addr, uint8_t reg, uint8_t *data, uint8_t len)
{
    /* Step 1: Send master code */
    if (!i2c4_send_master_code()) {
        return false;
    }

    /* Step 2: Write register address (no AUTOEND — Repeated START follows) */
    I2C4->CR2 = (1u << I2C_CR2_NBYTES_SHIFT)                |  /* 1 byte */
                ((uint32_t)(slave_addr & 0x7Fu) << 1u)       |
                I2C_CR2_START;                                   /* No AUTOEND */

    if (!wait_flag(&I2C4->ISR, I2C_ISR_TXIS, 1000u)) return false;
    I2C4->TXDR = reg;

    /* Wait for TC (transfer complete, no STOP) */
    if (!wait_flag(&I2C4->ISR, I2C_ISR_TC, 1000u)) return false;

    /* Step 3: Repeated START, read phase */
    I2C4->CR2 = ((uint32_t)len << I2C_CR2_NBYTES_SHIFT)     |
                ((uint32_t)(slave_addr & 0x7Fu) << 1u)       |
                I2C_CR2_RD_WRN                                |  /* Read */
                I2C_CR2_AUTOEND                               |
                I2C_CR2_START;

    /* Step 4: Receive data */
    for (uint8_t i = 0; i < len; i++) {
        if (!wait_flag(&I2C4->ISR, I2C_ISR_RXNE, 1000u)) return false;
        data[i] = (uint8_t)I2C4->RXDR;
    }

    /* Step 5: Wait for STOP */
    if (!wait_flag(&I2C4->ISR, I2C_ISR_STOPF, 5000u)) return false;
    I2C4->ICR = I2C_ISR_STOPF;

    return true;
}

/* ─────────────────────────────────────────────────────────────────────── */
/* Example usage (called from main or RTOS task)                           */
/* ─────────────────────────────────────────────────────────────────────── */

void hs_mode_example(void)
{
    i2c4_hs_init();

    /* Write 2 bytes to register 0x10 of slave at 0x50 */
    const uint8_t tx[2] = {0xAB, 0xCD};
    i2c4_hs_write(0x50, 0x10, tx, sizeof(tx));

    /* Read 4 bytes from register 0x00 of same slave */
    uint8_t rx[4] = {0};
    i2c4_hs_read(0x50, 0x00, rx, sizeof(rx));
}
```

---

## 8. Code Examples in Rust

### 8.1 Rust: Linux I²C HS-Mode Session (using `linux-embedded-hal`)

```rust
//! i2c_hs_mode.rs
//!
//! High-Speed I²C session management in Rust for Linux targets.
//! Uses the `linux-embedded-hal` and `embedded-hal` crates.
//!
//! [dependencies]
//! linux-embedded-hal = "0.4"
//! embedded-hal = "1.0"

use std::io;
use linux_embedded_hal::I2cdev;

/// I²C HS master codes (address space 0x08–0x0F, reserved)
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum HsMasterCode {
    Mc0 = 0x08,
    Mc1 = 0x09,
    Mc2 = 0x0A,
    Mc3 = 0x0B,
    Mc4 = 0x0C,
    Mc5 = 0x0D,
    Mc6 = 0x0E,
    Mc7 = 0x0F,
}

impl HsMasterCode {
    /// Construct from a master ID (0–7)
    pub fn from_id(id: u8) -> Option<Self> {
        match id {
            0 => Some(Self::Mc0),
            1 => Some(Self::Mc1),
            2 => Some(Self::Mc2),
            3 => Some(Self::Mc3),
            4 => Some(Self::Mc4),
            5 => Some(Self::Mc5),
            6 => Some(Self::Mc6),
            7 => Some(Self::Mc7),
            _ => None,
        }
    }

    pub fn address(self) -> u8 {
        self as u8
    }
}

/// Errors that can occur during HS-mode operation
#[derive(Debug)]
pub enum HsI2cError {
    /// Underlying I²C bus error
    Bus(io::Error),
    /// Master code was not sent successfully
    MasterCodeFailed,
    /// HS-mode session not active
    SessionNotActive,
    /// Invalid master ID
    InvalidMasterId(u8),
    /// Transfer failed with details
    TransferFailed { addr: u8, reason: String },
}

impl std::fmt::Display for HsI2cError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Self::Bus(e)            => write!(f, "I²C bus error: {e}"),
            Self::MasterCodeFailed  => write!(f, "HS master code transmission failed"),
            Self::SessionNotActive  => write!(f, "HS session not active"),
            Self::InvalidMasterId(id) => write!(f, "Invalid master ID: {id} (must be 0–7)"),
            Self::TransferFailed { addr, reason } =>
                write!(f, "Transfer to 0x{addr:02X} failed: {reason}"),
        }
    }
}

impl std::error::Error for HsI2cError {}

/// Context for managing an I²C High-Speed mode session.
///
/// # Protocol Note
///
/// Every HS-mode transaction begins with a Master Code byte transmitted
/// at F/S-mode speed. Slaves recognize this and switch to HS receive mode.
/// The NACK on the 9th bit is expected and non-fatal. A Repeated START
/// (Sr) follows immediately, and subsequent communication occurs at
/// 3.4 Mbit/s.
pub struct HsI2cSession {
    dev:         I2cdev,
    master_code: HsMasterCode,
    active:      bool,
}

impl HsI2cSession {
    /// Open an I²C device and configure it for HS-mode sessions.
    ///
    /// # Arguments
    /// * `path`      – Path to I²C device, e.g. `"/dev/i2c-1"`
    /// * `master_id` – This master's HS identifier (0–7)
    pub fn open(path: &str, master_id: u8) -> Result<Self, HsI2cError> {
        let master_code = HsMasterCode::from_id(master_id)
            .ok_or(HsI2cError::InvalidMasterId(master_id))?;

        let dev = I2cdev::new(path).map_err(HsI2cError::Bus)?;

        println!("[HS-I2C] Opened {path}, master code = 0x{:02X}", master_code.address());

        Ok(Self {
            dev,
            master_code,
            active: false,
        })
    }

    /// Transmit the HS master code in F/S-mode to initiate an HS session.
    ///
    /// Sends a zero-length write to the reserved master code address.
    /// The resulting NACK is expected and handled gracefully.
    pub fn enter_hs_mode(&mut self) -> Result<(), HsI2cError> {
        use linux_embedded_hal::i2cdev::linux::LinuxI2CMessage;
        use linux_embedded_hal::i2cdev::core::I2CTransfer;

        // Send master code: address-only write, no data, NACK expected.
        // I2C_M_IGNORE_NAK (0x1000) tells the kernel driver not to fail on NACK.
        let mc_addr = self.master_code.address();
        let mut dummy = [0u8; 0];

        let mut msgs = [LinuxI2CMessage::write(&mut dummy)
            .with_address(mc_addr as u16)
            .ignore_nack()];

        // The kernel driver handles the STOP→Repeated-START transition.
        self.dev.transfer(&mut msgs).map_err(|e| {
            // ENXIO is expected (no device ack) — treat as success
            if e.kind() == io::ErrorKind::Other {
                // Could be ENXIO; continue
            }
            HsI2cError::MasterCodeFailed
        }).unwrap_or(());

        self.active = true;
        println!("[HS-I2C] HS session started (master code 0x{mc_addr:02X})");
        Ok(())
    }

    /// Write bytes to a slave device in HS-mode.
    ///
    /// # Sequence
    /// ```text
    /// [START][MC][NACK][Sr][SlaveAddr+W][ACK][reg][data...][STOP]
    /// ```
    pub fn write(&mut self, slave_addr: u8, register: u8, data: &[u8])
        -> Result<(), HsI2cError>
    {
        if !self.active {
            self.enter_hs_mode()?;
        }

        use linux_embedded_hal::i2cdev::linux::LinuxI2CMessage;
        use linux_embedded_hal::i2cdev::core::I2CTransfer;

        // Build payload: register byte followed by data
        let mut payload = Vec::with_capacity(1 + data.len());
        payload.push(register);
        payload.extend_from_slice(data);

        let mc_addr = self.master_code.address();
        let mut dummy = [0u8; 0];

        let mut msgs = [
            // Master code (F/S speed, NACK ignored)
            LinuxI2CMessage::write(&mut dummy)
                .with_address(mc_addr as u16)
                .ignore_nack(),
            // Write payload (HS speed via Repeated START)
            LinuxI2CMessage::write(&payload)
                .with_address(slave_addr as u16),
        ];

        self.dev.transfer(&mut msgs).map_err(|e| {
            HsI2cError::TransferFailed {
                addr:   slave_addr,
                reason: e.to_string(),
            }
        })?;

        Ok(())
    }

    /// Read bytes from a slave device in HS-mode.
    ///
    /// # Sequence
    /// ```text
    /// [START][MC][NACK][Sr][SlaveAddr+W][ACK][reg][Sr][SlaveAddr+R][ACK][data...][NACK][STOP]
    /// ```
    pub fn read(&mut self, slave_addr: u8, register: u8, buf: &mut [u8])
        -> Result<(), HsI2cError>
    {
        if !self.active {
            self.enter_hs_mode()?;
        }

        use linux_embedded_hal::i2cdev::linux::LinuxI2CMessage;
        use linux_embedded_hal::i2cdev::core::I2CTransfer;

        let mc_addr  = self.master_code.address();
        let reg_buf  = [register];
        let mut dummy = [0u8; 0];

        let mut msgs = [
            // Master code (F/S speed, NACK ignored)
            LinuxI2CMessage::write(&mut dummy)
                .with_address(mc_addr as u16)
                .ignore_nack(),
            // Write register address (HS speed)
            LinuxI2CMessage::write(&reg_buf)
                .with_address(slave_addr as u16),
            // Read data (HS speed, Repeated START)
            LinuxI2CMessage::read(buf)
                .with_address(slave_addr as u16),
        ];

        self.dev.transfer(&mut msgs).map_err(|e| {
            HsI2cError::TransferFailed {
                addr:   slave_addr,
                reason: e.to_string(),
            }
        })?;

        Ok(())
    }

    /// End the HS session. The next START will be in F/S-mode.
    pub fn end_session(&mut self) {
        self.active = false;
        println!("[HS-I2C] Session ended. Bus returned to F/S-mode.");
    }
}

impl Drop for HsI2cSession {
    fn drop(&mut self) {
        if self.active {
            self.end_session();
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Example usage
// ─────────────────────────────────────────────────────────────────────────

fn main() -> Result<(), HsI2cError> {
    let mut session = HsI2cSession::open("/dev/i2c-1", 0)?;

    // Write 2 bytes to register 0x10 of slave 0x50
    session.write(0x50, 0x10, &[0xAB, 0xCD])?;

    // Read 4 bytes from register 0x00 of slave 0x50
    let mut rx = [0u8; 4];
    session.read(0x50, 0x00, &mut rx)?;
    println!("Read: {:02X?}", rx);

    // Session ends automatically via Drop
    Ok(())
}
```

---

### 8.2 Rust: Bare-Metal HS-Mode Register Driver (Embassy / RTIC compatible)

```rust
//! hs_i2c_driver.rs
//!
//! Bare-metal HS-mode I²C driver for ARM Cortex-M targets.
//! Designed for use with Embassy or RTIC; uses volatile register access.
//!
//! [dependencies]
//! cortex-m = "0.7"
//! vcell = "0.1"

use core::ptr;

/// Base address of I2C4 (STM32H7)
const I2C4_BASE: usize = 0x5800_1C00;

/// I2C register offsets
const CR1_OFF:      usize = 0x00;
const CR2_OFF:      usize = 0x04;
const TIMINGR_OFF:  usize = 0x10;
const ISR_OFF:      usize = 0x18;
const ICR_OFF:      usize = 0x1C;
const RXDR_OFF:     usize = 0x24;
const TXDR_OFF:     usize = 0x28;

/// CR1 bits
const CR1_PE:       u32 = 1 << 0;

/// CR2 bits
const CR2_AUTOEND:  u32 = 1 << 25;
const CR2_START:    u32 = 1 << 13;
const CR2_STOP:     u32 = 1 << 14;
const CR2_RD_WRN:   u32 = 1 << 10;

/// ISR bits  
const ISR_TXIS:     u32 = 1 << 1;
const ISR_RXNE:     u32 = 1 << 2;
const ISR_NACKF:    u32 = 1 << 4;
const ISR_STOPF:    u32 = 1 << 5;
const ISR_TC:       u32 = 1 << 6;

/// ICR bits
const ICR_NACKCF:   u32 = 1 << 4;
const ICR_STOPCF:   u32 = 1 << 5;

/// HS Master Code for master ID 0
const HS_MASTER_CODE: u8 = 0x08;

/// TIMINGR value for 3.4 Mbit/s with 48 MHz I2CCLK
/// PRESC=0, SCLDEL=1, SDADEL=0, SCLH=3, SCLL=8
const TIMINGR_HS_48MHZ: u32 =
    (0 << 28) | (1 << 20) | (0 << 16) | (3 << 8) | 8;

// ─────────────────────────────────────────────────────────────────────────
// Safe register access helpers
// ─────────────────────────────────────────────────────────────────────────

#[inline(always)]
unsafe fn read_reg(offset: usize) -> u32 {
    ptr::read_volatile((I2C4_BASE + offset) as *const u32)
}

#[inline(always)]
unsafe fn write_reg(offset: usize, val: u32) {
    ptr::write_volatile((I2C4_BASE + offset) as *mut u32, val);
}

#[inline(always)]
unsafe fn modify_reg(offset: usize, clear: u32, set: u32) {
    let v = read_reg(offset);
    write_reg(offset, (v & !clear) | set);
}

// ─────────────────────────────────────────────────────────────────────────
// Error type
// ─────────────────────────────────────────────────────────────────────────

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum I2cHsError {
    Timeout,
    Nack,
    ArbitrationLost,
    BusError,
}

// ─────────────────────────────────────────────────────────────────────────
// HS-mode I²C driver
// ─────────────────────────────────────────────────────────────────────────

pub struct I2cHsDriver;

impl I2cHsDriver {
    /// Initialize I2C4 for HS-mode at 3.4 Mbit/s.
    ///
    /// # Safety
    /// Caller must ensure clocks and GPIO are configured before calling.
    pub unsafe fn init(&self) {
        // Disable peripheral
        modify_reg(CR1_OFF, CR1_PE, 0);

        // Set HS-mode timing
        write_reg(TIMINGR_OFF, TIMINGR_HS_48MHZ);

        // Clear CR1 (analog filter on, no digital filter)
        write_reg(CR1_OFF, 0);

        // Enable peripheral
        modify_reg(CR1_OFF, 0, CR1_PE);
    }

    /// Wait for a specific ISR flag with a spin-loop timeout.
    unsafe fn wait_flag(&self, flag: u32, timeout: u32) -> Result<(), I2cHsError> {
        let mut t = timeout;
        loop {
            let isr = read_reg(ISR_OFF);

            // Check error conditions first
            if isr & (1 << 9) != 0 { return Err(I2cHsError::ArbitrationLost); }
            if isr & (1 << 8) != 0 { return Err(I2cHsError::BusError); }
            if isr & ISR_NACKF != 0 && flag != ISR_NACKF {
                write_reg(ICR_OFF, ICR_NACKCF);
                return Err(I2cHsError::Nack);
            }

            if isr & flag != 0 { return Ok(()); }

            t = t.checked_sub(1).ok_or(I2cHsError::Timeout)?;
            // In Embassy: use `embassy_time::Timer::after()` instead of spin
        }
    }

    /// Build CR2 value for a transfer.
    fn cr2(slave_addr: u8, nbytes: u8, read: bool, autoend: bool, start: bool) -> u32 {
        let mut cr2 = ((slave_addr as u32 & 0x7F) << 1)
                    | ((nbytes as u32) << 16);
        if read    { cr2 |= CR2_RD_WRN; }
        if autoend { cr2 |= CR2_AUTOEND; }
        if start   { cr2 |= CR2_START; }
        cr2
    }

    /// Send the HS master code in F/S-mode.
    ///
    /// Transmits a zero-byte write to the reserved master code address.
    /// The resulting NACK is expected and cleared.
    pub unsafe fn send_master_code(&self) -> Result<(), I2cHsError> {
        // Zero-byte write to master code address; NACK expected
        write_reg(CR2_OFF,
            ((HS_MASTER_CODE as u32 & 0x7F) << 1) |
            (0u32 << 16) |   // NBYTES = 0
            CR2_START
        );

        // Wait for NACK (9th bit) — this is the expected response
        match self.wait_flag(ISR_NACKF, 10_000) {
            Ok(()) | Err(I2cHsError::Nack) => {
                write_reg(ICR_OFF, ICR_NACKCF);
                Ok(())
            }
            Err(e) => Err(e),
        }
    }

    /// Write `data` to `register` at `slave_addr` using HS-mode.
    ///
    /// # Transaction
    /// ```text
    /// [S][MC][NACK][Sr][slave+W][ACK][reg][data...][P]
    /// ```
    ///
    /// # Safety
    /// Peripheral must be initialized and bus must be idle.
    pub unsafe fn write(
        &self,
        slave_addr: u8,
        register:   u8,
        data:       &[u8],
    ) -> Result<(), I2cHsError> {
        // 1. Send master code
        self.send_master_code()?;

        // 2. Start write: nbytes = 1 (register) + data.len()
        let nbytes = (1 + data.len()) as u8;
        write_reg(CR2_OFF, Self::cr2(slave_addr, nbytes, false, true, true));

        // 3. Send register address
        self.wait_flag(ISR_TXIS, 50_000)?;
        write_reg(TXDR_OFF, register as u32);

        // 4. Send data bytes
        for &byte in data {
            self.wait_flag(ISR_TXIS, 50_000)?;
            write_reg(TXDR_OFF, byte as u32);
        }

        // 5. Wait for STOP (AUTOEND generates it automatically)
        self.wait_flag(ISR_STOPF, 50_000)?;
        write_reg(ICR_OFF, ICR_STOPCF);

        Ok(())
    }

    /// Read `buf.len()` bytes from `register` at `slave_addr` using HS-mode.
    ///
    /// # Transaction
    /// ```text
    /// [S][MC][NACK][Sr][slave+W][ACK][reg][Sr][slave+R][ACK][data...][NACK][P]
    /// ```
    ///
    /// # Safety
    /// Peripheral must be initialized and bus must be idle.
    pub unsafe fn read(
        &self,
        slave_addr: u8,
        register:   u8,
        buf:        &mut [u8],
    ) -> Result<(), I2cHsError> {
        // 1. Send master code
        self.send_master_code()?;

        // 2. Write phase: send register address (no AUTOEND)
        write_reg(CR2_OFF, Self::cr2(slave_addr, 1, false, false, true));
        self.wait_flag(ISR_TXIS, 50_000)?;
        write_reg(TXDR_OFF, register as u32);

        // Wait for TC (transfer complete — Repeated START follows)
        self.wait_flag(ISR_TC, 50_000)?;

        // 3. Read phase: Repeated START, read nbytes
        let nbytes = buf.len() as u8;
        write_reg(CR2_OFF, Self::cr2(slave_addr, nbytes, true, true, true));

        // 4. Receive bytes
        for byte in buf.iter_mut() {
            self.wait_flag(ISR_RXNE, 50_000)?;
            *byte = read_reg(RXDR_OFF) as u8;
        }

        // 5. Wait for STOP
        self.wait_flag(ISR_STOPF, 50_000)?;
        write_reg(ICR_OFF, ICR_STOPCF);

        Ok(())
    }
}

// ─────────────────────────────────────────────────────────────────────────
// Example bare-metal entry point
// ─────────────────────────────────────────────────────────────────────────

#[no_mangle]
pub unsafe extern "C" fn hs_mode_example() {
    let drv = I2cHsDriver;
    drv.init();

    // Write 2 bytes to register 0x10 at slave 0x50
    let _ = drv.write(0x50, 0x10, &[0xAB, 0xCD]);

    // Read 4 bytes from register 0x00 at slave 0x50
    let mut rx = [0u8; 4];
    let _ = drv.read(0x50, 0x00, &mut rx);
}
```

---

## 9. Common Pitfalls

### 9.1 Treating the Master Code NACK as an Error

The NACK on the 9th bit of the master code is **by design** — not a bus fault. Drivers that use
generic I²C error handling will abort the transaction. Always use `I2C_M_IGNORE_NAK` (Linux) or
equivalent when transmitting the master code.

### 9.2 Applying HS Timing Before the Master Code

The master code **must** be transmitted at F/S-mode speed (≤400 kHz or ≤1 MHz FM+). Switching the
clock to 3.4 MHz before the master code will cause slaves to misinterpret the reserved address.

### 9.3 Issuing STOP Instead of Repeated START

After the master code NACK, the next bus event must be a **Repeated START (Sr)**, not a STOP.
Issuing STOP returns to F/S-mode and requires the entire master code sequence again for the next
HS transfer.

### 9.4 Resistive Pull-Ups at 3.4 MHz

Even a 1 kΩ resistive pull-up cannot reliably drive a 100 pF bus line to VDD within the 40 ns
rise time limit. **Current source pull-ups are mandatory** — verify your hardware schematic includes
them or uses an HS-capable I²C buffer IC (e.g., NXP PCA9615).

### 9.5 Slave Clock Stretching

If an HS-mode slave stretches SCL (which it should not do, per spec), the timing constraints are
violated and data corruption or bus lockup may occur. Ensure all slaves on the HS segment are
certified as HS-capable with no clock stretching.

### 9.6 Bus Capacitance Exceeding 100 pF

HS-mode is limited to 100 pF bus capacitance. Long PCB traces, multiple connectors, or crowded
boards can exceed this. Use an I²C bus extender/buffer (e.g., TI PCA9517, NXP P82B96) to isolate
high-capacitance segments.

### 9.7 Multi-Master Arbitration Not Handled

In multi-master designs, each HS master must have a **unique master code** (MC_0..MC_7). Using the
same code on two masters causes arbitration failure during the F/S master code phase and neither
master will gain the bus.

---

## 10. Summary

I²C High-Speed mode extends the bus to **3.4 Mbit/s** through a carefully orchestrated protocol
upgrade mechanism:

**Master Code Transmission** kicks off every HS session. A special 8-bit reserved address
(`0x08`–`0x0F`) is sent at F/S speed, signalling intent to switch to HS operation. All slaves
recognize the code but do not ACK it — the resulting NACK on the 9th clock is expected and
harmless. In multi-master systems, each master holds a unique code; wired-AND arbitration resolves
contention during this phase before HS-mode begins.

**Current Source Pull-Ups** replace the traditional resistive pull-ups to meet the demanding rise
time requirements (10–40 ns) at 3.4 MHz. A 3 mA current source is switched on only during the
LOW-to-HIGH transition, then disabled once the line is stable, minimizing power dissipation while
ensuring signal integrity on bus capacitances up to 100 pF.

**3.4 MHz Operation** requires strict adherence to asymmetric timing (t_LOW ≥ 160 ns,
t_HIGH ≥ 60 ns), a no-clock-stretching policy for slaves, and the use of Repeated STARTs to stay
within the HS session. A plain STOP/START sequence exits HS-mode and demands a fresh master code
for the next high-speed burst.

**Mixed-Speed Compatibility** is preserved because the master code uses a reserved address space
ignored by F/S slaves, and HS transactions use Repeated STARTs that keep F/S devices from ever
seeing an address match during the high-speed phase.

In practice, software must ensure NACK tolerance during master code transmission, correct timing
register values for the chosen controller clock, and hardware must provide current source pull-up
circuits or HS-capable buffer ICs. Both the C/C++ and Rust examples above demonstrate these
principles applied to Linux userspace (`I2C_RDWR` / `linux-embedded-hal`) and bare-metal
STM32H7 register-level programming.

---

*© I²C High-Speed Mode Details — Document 52*  
*I²C is a trademark of NXP Semiconductors.*