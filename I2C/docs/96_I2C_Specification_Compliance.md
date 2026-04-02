# 96. I2C Specification Compliance

**What's included:**

- **Electrical characteristics** — VOL/VIH/VIL levels, pull-up resistor sizing formulas with a worked example for 3.3V / 100pF / Fast-mode
- **Timing parameter table** — all critical UM10204 Table 10 values (t_HD;STA, t_SU;DAT, t_BUF, t_LOW, t_HIGH, t_r, t_f, C_b) across Sm / Fm / Fm+
- **Protocol requirements** — START/STOP waveforms, data validity rule, ACK/NACK, Repeated START
- **Multi-master arbitration** and clock synchronisation
- **Clock stretching** — compliance trap with hardware timeouts
- **7-bit and 10-bit addressing** — reserved address table, correct two-byte sequence for 10-bit reads (only first byte repeated after Sr)
- **Special modes** — HS-mode master code preamble, UFm push-pull unidirectionality

**Code examples:**

- **C** — `i2c_timing_check.c`: full compliance checker with spec structs + `CHECK_MIN`/`CHECK_MAX` macros; pull-up sizing formula; Linux `i2c-dev` compliant repeated-START register read; bus scanner
- **C** — `i2c_bitbang.c`: complete UM10204 Fast-mode bit-banged master with clock-stretch support, arbitration detection, `i2c_write_reg` and `i2c_read_reg` with Repeated START
- **C** — `i2c_10bit_addr.c`: 10-bit addressing write and read (correct Sr behaviour)
- **Rust** — `i2c_compliance.rs`: `no_std`-compatible compliance checker with violation collection and unit tests
- **Rust** — `i2c_bitbang_master.rs`: `embedded-hal` v1.0 trait-implementing bit-banged master with `transaction()`, arbitration, and stretch support
- **Rust** — `i2c_reserved_addr.rs`: reserved address guard with unit tests


**Understanding and Implementing NXP UM10204 I2C-Bus Specification Requirements**

---

## Table of Contents

1. [Introduction](#introduction)
2. [The NXP UM10204 Specification Overview](#the-nxp-um10204-specification-overview)
3. [Electrical Characteristics](#electrical-characteristics)
4. [Timing Parameters](#timing-parameters)
5. [Protocol Requirements](#protocol-requirements)
6. [Multi-Master and Arbitration](#multi-master-and-arbitration)
7. [Clock Stretching](#clock-stretching)
8. [Address Modes: 7-bit and 10-bit](#address-modes-7-bit-and-10-bit)
9. [Special Modes: HS-mode, Ultra Fast-mode](#special-modes-hs-mode-ultra-fast-mode)
10. [Programming: C/C++ Examples](#programming-cc-examples)
11. [Programming: Rust Examples](#programming-rust-examples)
12. [Summary](#summary)

---

## Introduction

The I2C (Inter-Integrated Circuit) bus was originally developed by Philips Semiconductor (now NXP) in the early 1980s as a simple, two-wire serial communication interface. Its defining document — **NXP UM10204**, currently at Rev. 7 (2021) — is the normative specification that all compliant I2C implementations must follow.

I2C compliance is not merely a design nicety; it is a prerequisite for reliable interoperability between devices from different manufacturers. Violations of the specification can produce subtle, intermittent failures that are very difficult to debug, especially on boards with many I2C slaves operating at different speeds.

This chapter covers the full scope of the UM10204 requirements: electrical constraints, timing parameters, protocol state machines, arbitration, clock stretching, addressing modes, and special speed modes. C/C++ and Rust code examples demonstrate how to implement and verify compliance programmatically.

---

## The NXP UM10204 Specification Overview

The UM10204 specification defines:

- **Physical layer**: voltage levels, current sourcing, pull-up resistor sizing, bus capacitance limits.
- **Data link layer**: START/STOP conditions, clock generation, data validity, ACK/NACK, arbitration.
- **Protocol layer**: address frames (7-bit / 10-bit), data frames, repeated START, reserved addresses.
- **Speed modes**:

| Mode | Maximum Bit Rate | Notes |
|---|---|---|
| Standard-mode (Sm) | 100 kbit/s | Original mode, universal support |
| Fast-mode (Fm) | 400 kbit/s | Backward compatible with Sm |
| Fast-mode Plus (Fm+) | 1 Mbit/s | Requires stronger pull-ups / current drivers |
| High-speed mode (Hs) | 3.4 Mbit/s | Requires Hs-capable master/slaves, CBUS compat. |
| Ultra Fast-mode (UFm) | 5 Mbit/s | Unidirectional, push-pull, no ACK |

> **Key principle**: Every device operating on an I2C bus must be capable of handling — at minimum — the electrical and timing requirements of the *slowest* operating mode that it advertises support for.

---

## Electrical Characteristics

### Logic Levels

UM10204 defines logic levels relative to VDD (supply voltage):

| Parameter | Symbol | Condition | Min | Max |
|---|---|---|---|---|
| Low-level input voltage | VIL | — | −0.5 V | 0.3 × VDD |
| High-level input voltage | VIH | — | 0.7 × VDD | VDD + 0.5 V |
| Low-level output voltage | VOL | 3 mA sink | — | 0.4 V |
| Hysteresis (Fm+) | Vhys | — | 0.1 × VDD | — |

### Pull-up Resistors

The I2C bus is open-drain (open-collector). Pull-up resistors (Rp) must be sized to satisfy **both**:

1. Rise-time constraint: `t_r = 0.8473 × Rp × Cb` where `Cb` is the total bus capacitance.
2. Current sink limit: VOL ≤ 0.4 V when the lowest-voltage device pulls the line low through a 3 mA (maximum) sink.

**Minimum Rp** (ensures VIL is not exceeded by leakage):

```
Rp_min = (VDD - VOL_max) / I_OL_max
```

**Maximum Rp** (ensures the line can rise within t_r):

```
Rp_max = t_r / (0.8473 × Cb)
```

For 400 kbit/s Fast-mode with 100 pF bus capacitance:

```
t_r_max = 300 ns
Rp_max = 300 ns / (0.8473 × 100 pF) ≈ 3.54 kΩ
```

---

## Timing Parameters

The UM10204 defines many timing parameters. The most critical for software and FPGA implementations are:

| Symbol | Parameter | Sm (100k) | Fm (400k) | Fm+ (1M) |
|---|---|---|---|---|
| f_SCL | SCL clock frequency | 0–100 kHz | 0–400 kHz | 0–1 MHz |
| t_HD;STA | Hold time START | ≥ 4.0 µs | ≥ 0.6 µs | ≥ 0.26 µs |
| t_SU;STA | Setup time repeated START | ≥ 4.7 µs | ≥ 0.6 µs | ≥ 0.26 µs |
| t_HD;DAT | Data hold time | ≥ 0 µs | ≥ 0 µs | ≥ 0 µs |
| t_SU;DAT | Data setup time | ≥ 250 ns | ≥ 100 ns | ≥ 50 ns |
| t_SU;STO | Setup time STOP | ≥ 4.0 µs | ≥ 0.6 µs | ≥ 0.26 µs |
| t_BUF | Bus free time between STOP and START | ≥ 4.7 µs | ≥ 1.3 µs | ≥ 0.5 µs |
| t_LOW | SCL low period | ≥ 4.7 µs | ≥ 1.3 µs | ≥ 0.5 µs |
| t_HIGH | SCL high period | ≥ 4.0 µs | ≥ 0.6 µs | ≥ 0.26 µs |
| t_r | Rise time SDA/SCL | ≤ 1000 ns | ≤ 300 ns | ≤ 120 ns |
| t_f | Fall time SDA/SCL | ≤ 300 ns | ≤ 300 ns | ≤ 120 ns |
| C_b | Capacitive load per bus line | ≤ 400 pF | ≤ 400 pF | ≤ 550 pF |

> **Important**: All timing parameters are measured at the **bus pins**, not at the microcontroller GPIO. PCB parasitics and cable capacitance must be subtracted from the device's internal timing budget.

---

## Protocol Requirements

### START and STOP Conditions

A **START** condition is a HIGH-to-LOW transition on SDA while SCL remains HIGH.  
A **STOP** condition is a LOW-to-HIGH transition on SDA while SCL remains HIGH.

```
START:   SCL ______|‾‾‾‾‾‾‾‾‾‾‾‾
         SDA ‾‾‾‾‾‾‾|__________

STOP:    SCL ______|‾‾‾‾‾‾‾‾‾‾‾‾
         SDA ___________|‾‾‾‾‾‾
```

Only a **master** generates START and STOP. The first byte after START is always an address byte.

### Data Validity

The SDA line **must be stable** whenever SCL is HIGH. Data may only change while SCL is LOW. The single exception is START and STOP conditions.

### ACK and NACK

After every byte transmission, the **receiver** must pull SDA LOW during the 9th clock pulse (ACK). If SDA remains HIGH during the 9th clock, this is NACK. The master generates the ACK/NACK after a read, the slave generates it after a write.

### Repeated START (Sr)

A master may issue a new START condition without releasing the bus (no STOP). This is mandatory when changing the transfer direction (write→read) in a single atomic transaction, commonly used for register read sequences.

---

## Multi-Master and Arbitration

Multiple masters can share an I2C bus. Arbitration is performed **bitwise** on SDA:

- Each master monitors SDA while driving.
- If a master drives SDA HIGH but reads SDA LOW, another master is driving it LOW — the master with the LOW wins (open-drain wired-AND).
- The losing master must immediately stop and wait for the bus to become free.
- No data is lost: the winning master is unaware a collision occurred.

Arbitration only happens during the **address phase**. If two masters address different slaves, arbitration resolves before any slave responds. If they address the same slave, arbitration continues through the data bytes.

**Clock synchronization**: In multi-master scenarios, the SCL line is also open-drain. All masters use the wired-AND SCL, so the master with the longest LOW period dominates clock stretching.

---

## Clock Stretching

A **slave** may hold SCL LOW after the master releases it, signalling it needs more time to prepare data. The master must detect this and wait.

Requirements per UM10204:
- After the master releases SCL (stops driving LOW), it must sample SCL and wait until it reads HIGH before proceeding.
- The delay between the master releasing SCL and SCL actually going HIGH is: `t_stretch = slave_hold_time`.
- The master must **not** timeout prematurely; many microcontroller I2C peripherals have configurable stretch timeouts. The spec does not define a maximum stretch time — it is device-specific.

> **Compliance trap**: Many I2C master implementations have a fixed hardware timeout for clock stretching that is shorter than what a compliant (but slow) slave requires. Always verify the maximum stretch time of all slaves and configure the master accordingly.

---

## Address Modes: 7-bit and 10-bit

### 7-bit Addressing

The first byte is `[A6:A0, R/W̄]`. The 7-bit address occupies bits 7:1; bit 0 is the R/W̄ bit.

Reserved 7-bit addresses (must not be used as slave addresses):

| Address | Usage |
|---|---|
| 0000 000 | General call address |
| 0000 001 | CBUS address |
| 0000 010 | Reserved (future) |
| 0000 011 | Reserved (future) |
| 0000 1XX | Hs-mode master code |
| 1111 1XX | Reserved |
| 1111 0XX | 10-bit address prefix |

### 10-bit Addressing

10-bit addresses use a two-byte prefix sequence:

```
First byte:  1 1 1 1 0 A9 A8 R/W̄
Second byte: A7 A6 A5 A4 A3 A2 A1 A0
```

For a write transaction: master sends first byte (with R/W̄=0), slave ACKs, master sends second byte, slave ACKs, master sends data.

For a read transaction after a write: master sends a **Repeated START**, then first byte with R/W̄=1 (second byte is not repeated), slave ACKs and begins transmitting.

---

## Special Modes: HS-mode, Ultra Fast-mode

### High-Speed Mode (Hs, 3.4 Mbit/s)

Hs-mode requires a special **master code** (`0000 1XXX`) sent at F/S-mode speed before switching to Hs. The master code never receives an ACK — it merely signals slaves to switch to Hs timing.

Key Hs-mode differences:
- Current-source pull-up (3 mA) for fast rise times.
- SDAH and SCLH lines may be separated from the F/S-mode bus.
- Spike suppression filter (50 ns) is mandatory.
- Arbitration and clock synchronization are disabled during Hs transfers.

### Ultra Fast-mode (UFm, 5 Mbit/s)

UFm is fundamentally different:
- **Push-pull** drivers — no open-drain, no pull-up resistors.
- **Unidirectional**: master to slave only.
- **No ACK**: slaves cannot respond.
- Not backward compatible with other modes.
- Use case: write-only peripherals (e.g., LED drivers, DACs) where simplicity and speed matter.

---

## Programming: C/C++ Examples

### 1. Timing Verification Utility

```c
/**
 * i2c_timing_check.c
 *
 * Compile-time and runtime verification of I2C timing compliance
 * per NXP UM10204 Rev.7
 *
 * Target: Linux userspace (i2c-dev) + bare-metal (portable timing struct)
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

/* UM10204 Table 10: Timing parameters (all values in nanoseconds) */
typedef struct {
    uint32_t f_scl_max_hz;   /* Maximum SCL frequency */
    uint32_t t_hd_sta_min;   /* Hold time START/repeated START */
    uint32_t t_su_sta_min;   /* Setup time repeated START */
    uint32_t t_su_dat_min;   /* Data setup time */
    uint32_t t_hd_dat_min;   /* Data hold time */
    uint32_t t_su_sto_min;   /* Setup time STOP condition */
    uint32_t t_buf_min;      /* Bus free time between STOP and START */
    uint32_t t_low_min;      /* SCL low period */
    uint32_t t_high_min;     /* SCL high period */
    uint32_t t_r_max;        /* Rise time */
    uint32_t t_f_max;        /* Fall time */
    uint32_t cb_max_pf;      /* Max bus capacitance (picofarads) */
} i2c_timing_spec_t;

/* Standard-mode (100 kbit/s) timing requirements */
static const i2c_timing_spec_t I2C_SM_SPEC = {
    .f_scl_max_hz  = 100000,
    .t_hd_sta_min  = 4000,   /* 4.0 µs */
    .t_su_sta_min  = 4700,   /* 4.7 µs */
    .t_su_dat_min  = 250,    /* 250 ns */
    .t_hd_dat_min  = 0,
    .t_su_sto_min  = 4000,
    .t_buf_min     = 4700,
    .t_low_min     = 4700,
    .t_high_min    = 4000,
    .t_r_max       = 1000,
    .t_f_max       = 300,
    .cb_max_pf     = 400,
};

/* Fast-mode (400 kbit/s) timing requirements */
static const i2c_timing_spec_t I2C_FM_SPEC = {
    .f_scl_max_hz  = 400000,
    .t_hd_sta_min  = 600,    /* 0.6 µs */
    .t_su_sta_min  = 600,
    .t_su_dat_min  = 100,    /* 100 ns */
    .t_hd_dat_min  = 0,
    .t_su_sto_min  = 600,
    .t_buf_min     = 1300,   /* 1.3 µs */
    .t_low_min     = 1300,
    .t_high_min    = 600,
    .t_r_max       = 300,
    .t_f_max       = 300,
    .cb_max_pf     = 400,
};

/* Fast-mode Plus (1 Mbit/s) timing requirements */
static const i2c_timing_spec_t I2C_FMP_SPEC = {
    .f_scl_max_hz  = 1000000,
    .t_hd_sta_min  = 260,
    .t_su_sta_min  = 260,
    .t_su_dat_min  = 50,
    .t_hd_dat_min  = 0,
    .t_su_sto_min  = 260,
    .t_buf_min     = 500,
    .t_low_min     = 500,
    .t_high_min    = 260,
    .t_r_max       = 120,
    .t_f_max       = 120,
    .cb_max_pf     = 550,
};

/**
 * Measured timing from oscilloscope or logic analyser capture.
 */
typedef struct {
    uint32_t t_hd_sta_ns;
    uint32_t t_su_sta_ns;
    uint32_t t_su_dat_ns;
    uint32_t t_hd_dat_ns;
    uint32_t t_su_sto_ns;
    uint32_t t_buf_ns;
    uint32_t t_low_ns;
    uint32_t t_high_ns;
    uint32_t t_r_ns;
    uint32_t t_f_ns;
    uint32_t scl_freq_hz;
    uint32_t bus_cap_pf;
} i2c_measured_t;

typedef struct {
    bool pass;
    char violated_param[64];
    uint32_t required;
    uint32_t measured;
} i2c_check_result_t;

#define CHECK_MIN(field, param_name)                                      \
    do {                                                                  \
        if (measured->field < spec->field) {                             \
            res.pass = false;                                             \
            snprintf(res.violated_param, sizeof(res.violated_param),     \
                     "%s: need >= %u ns, got %u ns",                     \
                     param_name, spec->field, measured->field);           \
            res.required = spec->field;                                   \
            res.measured = measured->field;                               \
            return res;                                                   \
        }                                                                 \
    } while (0)

#define CHECK_MAX(mfield, sfield, param_name)                             \
    do {                                                                  \
        if (measured->mfield > spec->sfield) {                           \
            res.pass = false;                                             \
            snprintf(res.violated_param, sizeof(res.violated_param),     \
                     "%s: need <= %u ns, got %u ns",                     \
                     param_name, spec->sfield, measured->mfield);         \
            res.required = spec->sfield;                                  \
            res.measured = measured->mfield;                              \
            return res;                                                   \
        }                                                                 \
    } while (0)

i2c_check_result_t i2c_check_compliance(
    const i2c_timing_spec_t *spec,
    const i2c_measured_t    *measured)
{
    i2c_check_result_t res = { .pass = true };

    CHECK_MIN(t_hd_sta_ns,  "t_HD;STA");
    CHECK_MIN(t_su_sta_ns,  "t_SU;STA");
    CHECK_MIN(t_su_dat_ns,  "t_SU;DAT");
    CHECK_MIN(t_hd_dat_ns,  "t_HD;DAT");
    CHECK_MIN(t_su_sto_ns,  "t_SU;STO");
    CHECK_MIN(t_buf_ns,     "t_BUF");
    CHECK_MIN(t_low_ns,     "t_LOW");
    CHECK_MIN(t_high_ns,    "t_HIGH");

    CHECK_MAX(t_r_ns,       t_r_max,   "t_r");
    CHECK_MAX(t_f_ns,       t_f_max,   "t_f");

    if (measured->scl_freq_hz > spec->f_scl_max_hz) {
        res.pass = false;
        snprintf(res.violated_param, sizeof(res.violated_param),
                 "f_SCL: need <= %u Hz, got %u Hz",
                 spec->f_scl_max_hz, measured->scl_freq_hz);
        res.required = spec->f_scl_max_hz;
        res.measured = measured->scl_freq_hz;
        return res;
    }

    if (measured->bus_cap_pf > spec->cb_max_pf) {
        res.pass = false;
        snprintf(res.violated_param, sizeof(res.violated_param),
                 "C_b: need <= %u pF, got %u pF",
                 spec->cb_max_pf, measured->bus_cap_pf);
        res.required = spec->cb_max_pf;
        res.measured = measured->bus_cap_pf;
        return res;
    }

    return res;
}

/* ----------------------------------------------------------------
 * Pull-up resistor sizing helper
 * ---------------------------------------------------------------- */
typedef struct {
    float rp_min_ohm;  /* Minimum Rp to keep VOL ≤ 0.4 V */
    float rp_max_ohm;  /* Maximum Rp to meet rise-time requirement */
    bool  feasible;    /* True if rp_min < rp_max */
} rp_result_t;

/**
 * @param vdd_v       Supply voltage in volts
 * @param vol_max_v   Maximum VOL (typically 0.4 V)
 * @param iol_max_a   Maximum sink current per device (typically 0.003 A for Sm/Fm)
 * @param tr_max_ns   Maximum rise time from spec
 * @param cb_pf       Total bus capacitance in picofarads
 */
rp_result_t i2c_pullup_size(
    float vdd_v,
    float vol_max_v,
    float iol_max_a,
    float tr_max_ns,
    float cb_pf)
{
    rp_result_t r;
    r.rp_min_ohm = (vdd_v - vol_max_v) / iol_max_a;
    /* t_r = 0.8473 * Rp * Cb  =>  Rp = t_r / (0.8473 * Cb) */
    float cb_f   = cb_pf * 1e-12f;
    float tr_s   = tr_max_ns * 1e-9f;
    r.rp_max_ohm = tr_s / (0.8473f * cb_f);
    r.feasible   = r.rp_min_ohm < r.rp_max_ohm;
    return r;
}

/* ----------------------------------------------------------------
 * Linux i2c-dev: compliant multi-byte register write with timing
 * ---------------------------------------------------------------- */
#ifdef __linux__
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <time.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>

/**
 * Compliant I2C register read using repeated START (no STOP between
 * write of register address and read of data).
 *
 * Uses I2C_RDWR ioctl which generates Sr (repeated START) natively.
 */
int i2c_reg_read_compliant(
    int     bus_fd,
    uint8_t slave_addr,
    uint8_t reg_addr,
    uint8_t *buf,
    uint16_t len)
{
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data data;

    /* Message 0: write register address */
    msgs[0].addr  = slave_addr;
    msgs[0].flags = 0;          /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg_addr;

    /* Message 1: read data (kernel generates repeated START between) */
    msgs[1].addr  = slave_addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    data.msgs  = msgs;
    data.nmsgs = 2;

    if (ioctl(bus_fd, I2C_RDWR, &data) < 0) {
        perror("I2C_RDWR ioctl");
        return -errno;
    }
    return 0;
}

/**
 * Open I2C bus and verify the adapter supports the I2C_RDWR ioctl
 * (required for repeated START compliance).
 */
int i2c_open_bus(const char *dev_path)
{
    int fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    unsigned long funcs;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        perror("I2C_FUNCS");
        close(fd);
        return -1;
    }

    if (!(funcs & I2C_FUNC_I2C)) {
        fprintf(stderr, "Adapter does not support I2C_RDWR (repeated START).\n");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * Bus scan: probe all 7-bit addresses and report present devices.
 * Skips reserved addresses per UM10204.
 */
void i2c_scan_bus(int fd)
{
    printf("I2C Bus Scan\n");
    printf("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\n");

    for (int row = 0; row < 8; row++) {
        printf("%02x: ", row * 16);
        for (int col = 0; col < 16; col++) {
            int addr = row * 16 + col;

            /* Skip reserved addresses: 0x00-0x07 and 0x78-0x7F */
            if (addr < 0x08 || addr > 0x77) {
                printf("   ");
                continue;
            }

            if (ioctl(fd, I2C_SLAVE, addr) < 0) {
                printf("   ");
                continue;
            }

            /* Quick read probe — 1 byte */
            uint8_t dummy;
            int ret = read(fd, &dummy, 1);
            printf(ret >= 0 ? "%02x " : "-- ", addr);
        }
        printf("\n");
    }
}
#endif /* __linux__ */

/* ----------------------------------------------------------------
 * Example main: check a Fast-mode measurement for compliance
 * ---------------------------------------------------------------- */
int main(void)
{
    /* Simulated oscilloscope measurements for a 400 kHz bus */
    i2c_measured_t measured = {
        .t_hd_sta_ns  = 650,
        .t_su_sta_ns  = 650,
        .t_su_dat_ns  = 110,
        .t_hd_dat_ns  = 10,
        .t_su_sto_ns  = 650,
        .t_buf_ns     = 1400,
        .t_low_ns     = 1350,
        .t_high_ns    = 650,
        .t_r_ns       = 280,
        .t_f_ns       = 150,
        .scl_freq_hz  = 395000,
        .bus_cap_pf   = 80,
    };

    i2c_check_result_t res = i2c_check_compliance(&I2C_FM_SPEC, &measured);

    if (res.pass) {
        printf("Timing compliance: PASS (Fast-mode 400 kbit/s)\n");
    } else {
        printf("Timing compliance: FAIL\n");
        printf("  Violation: %s\n", res.violated_param);
    }

    /* Pull-up sizing for 3.3 V, 100 pF bus, Fast-mode */
    rp_result_t rp = i2c_pullup_size(
        3.3f,   /* VDD */
        0.4f,   /* VOL max */
        0.003f, /* IOL max (3 mA) */
        300.0f, /* t_r max ns */
        100.0f  /* Cb pF */
    );

    printf("\nPull-up resistor range for 3.3V, 100pF, Fm:\n");
    printf("  Rp_min = %.0f Ω\n", rp.rp_min_ohm);
    printf("  Rp_max = %.0f Ω\n", rp.rp_max_ohm);
    printf("  Feasible: %s\n", rp.feasible ? "YES" : "NO (bus overloaded)");

    return 0;
}
```

---

### 2. Bit-Banged I2C Master (UM10204-Compliant)

```c
/**
 * i2c_bitbang.c
 *
 * Portable bit-banged I2C master compliant with NXP UM10204 Fast-mode.
 * Implements exact timing delays and clock-stretch detection.
 *
 * The HAL layer (i2c_hal_*) must be ported to the target platform.
 */

#include <stdint.h>
#include <stdbool.h>

/* ── Platform HAL (implement for your MCU/OS) ──────────────────────── */

/* Drive SCL low */
static inline void hal_scl_low(void);
/* Release SCL (let pull-up bring it high) */
static inline void hal_scl_release(void);
/* Read SCL — returns 0 or 1 */
static inline int  hal_scl_read(void);
/* Drive SDA low */
static inline void hal_sda_low(void);
/* Release SDA */
static inline void hal_sda_release(void);
/* Read SDA — returns 0 or 1 */
static inline int  hal_sda_read(void);
/* Delay at least `ns` nanoseconds */
static inline void hal_delay_ns(uint32_t ns);

/* ── UM10204 Fast-mode timing constants (nanoseconds) ─────────────── */
#define T_HD_STA_NS   600U   /* Hold time after START */
#define T_SU_STA_NS   600U   /* Setup time before repeated START */
#define T_LOW_NS     1300U   /* SCL low period */
#define T_HIGH_NS     600U   /* SCL high period */
#define T_SU_DAT_NS   100U   /* Data setup time (before SCL rising) */
#define T_HD_DAT_NS     0U   /* Data hold time (after SCL falling) */
#define T_SU_STO_NS   600U   /* Setup time before STOP */
#define T_BUF_NS     1300U   /* Bus free time after STOP */
#define T_STRETCH_TIMEOUT_US 25000U  /* Clock stretch timeout (device-specific) */

/* ── Internal helpers ──────────────────────────────────────────────── */

/**
 * Wait for SCL to go high (clock stretching support).
 * Returns false on timeout (slave stuck — bus error).
 */
static bool scl_wait_high(void)
{
    uint32_t deadline = T_STRETCH_TIMEOUT_US * 4; /* rough loop count */
    hal_scl_release();
    while (!hal_scl_read()) {
        if (--deadline == 0) return false; /* timeout */
        hal_delay_ns(250);
    }
    return true;
}

/* ── Public API ────────────────────────────────────────────────────── */

typedef enum {
    I2C_OK       = 0,
    I2C_ACK      = 0,
    I2C_NACK     = 1,
    I2C_BUS_BUSY = -1,
    I2C_TIMEOUT  = -2,
    I2C_ARB_LOST = -3,
} i2c_status_t;

/**
 * Generate START condition.
 * Precondition: SCL and SDA are both HIGH (idle bus).
 */
i2c_status_t i2c_start(void)
{
    /* Verify bus is free: SDA and SCL must both be HIGH */
    if (!hal_sda_read() || !hal_scl_read())
        return I2C_BUS_BUSY;

    /* SDA falls while SCL is HIGH → START condition */
    hal_sda_low();
    hal_delay_ns(T_HD_STA_NS);  /* t_HD;STA: hold SDA low before SCL falls */
    hal_scl_low();
    hal_delay_ns(T_HD_DAT_NS);  /* t_HD;DAT */
    return I2C_OK;
}

/**
 * Generate Repeated START (Sr).
 * Precondition: SCL is currently LOW.
 */
i2c_status_t i2c_repeated_start(void)
{
    /* Release SDA first, then let SCL rise */
    hal_sda_release();
    hal_delay_ns(T_SU_STA_NS);  /* t_SU;STA: SDA must be stable before SCL rises */

    if (!scl_wait_high()) return I2C_TIMEOUT;
    hal_delay_ns(T_HIGH_NS);    /* SCL high period */

    /* Now pull SDA low while SCL is high → Repeated START */
    hal_sda_low();
    hal_delay_ns(T_HD_STA_NS);
    hal_scl_low();
    hal_delay_ns(T_HD_DAT_NS);
    return I2C_OK;
}

/**
 * Generate STOP condition.
 * Precondition: SCL is currently LOW.
 */
void i2c_stop(void)
{
    hal_sda_low();
    hal_delay_ns(T_SU_STO_NS);   /* t_SU;STO: hold SDA low before SCL rises */

    if (!scl_wait_high()) {
        /* If SCL never rises, force-release anyway to avoid bus lockup */
    }
    hal_delay_ns(T_HIGH_NS);

    /* SDA rises while SCL is HIGH → STOP condition */
    hal_sda_release();
    hal_delay_ns(T_BUF_NS);      /* t_BUF: bus free time */
}

/**
 * Transmit one byte, MSB first. Returns ACK/NACK from slave.
 * Precondition: SCL is LOW.
 */
i2c_status_t i2c_write_byte(uint8_t byte)
{
    for (int bit = 7; bit >= 0; bit--) {
        /* Set SDA before SCL rises (t_SU;DAT) */
        if (byte & (1u << bit))
            hal_sda_release();
        else
            hal_sda_low();

        hal_delay_ns(T_SU_DAT_NS);

        /* Raise SCL — wait for stretch */
        if (!scl_wait_high()) return I2C_TIMEOUT;
        hal_delay_ns(T_HIGH_NS);

        /* Arbitration check: if we sent HIGH but SDA reads LOW, we lost */
        if ((byte & (1u << bit)) && !hal_sda_read())
            return I2C_ARB_LOST;

        hal_scl_low();
        hal_delay_ns(T_HD_DAT_NS);
    }

    /* 9th clock: release SDA for slave ACK */
    hal_sda_release();
    hal_delay_ns(T_SU_DAT_NS);

    if (!scl_wait_high()) return I2C_TIMEOUT;
    hal_delay_ns(T_HIGH_NS);

    int ack = hal_sda_read(); /* LOW = ACK, HIGH = NACK */
    hal_scl_low();
    hal_delay_ns(T_HD_DAT_NS);

    return ack ? I2C_NACK : I2C_ACK;
}

/**
 * Receive one byte from slave. Master sends ACK if `ack` is true,
 * NACK on the last byte of a read.
 * Precondition: SCL is LOW.
 */
uint8_t i2c_read_byte(bool send_ack)
{
    uint8_t byte = 0;
    hal_sda_release(); /* Release SDA for slave to drive */

    for (int bit = 7; bit >= 0; bit--) {
        hal_delay_ns(T_SU_DAT_NS);

        if (!scl_wait_high()) return 0xFF; /* timeout — return 0xFF */
        hal_delay_ns(T_HIGH_NS);

        if (hal_sda_read()) byte |= (1u << bit);
        hal_scl_low();
        hal_delay_ns(T_HD_DAT_NS);
    }

    /* Master drives ACK or NACK on 9th clock */
    if (send_ack)
        hal_sda_low();     /* ACK  */
    else
        hal_sda_release(); /* NACK */

    hal_delay_ns(T_SU_DAT_NS);
    if (!scl_wait_high()) return byte;
    hal_delay_ns(T_HIGH_NS);
    hal_scl_low();
    hal_delay_ns(T_HD_DAT_NS);

    hal_sda_release();
    return byte;
}

/**
 * High-level: write N bytes to a 7-bit addressed slave register.
 * Generates START, address+W, reg_addr, data[0..len-1], STOP.
 */
i2c_status_t i2c_write_reg(
    uint8_t        slave_addr,
    uint8_t        reg_addr,
    const uint8_t *data,
    uint8_t        len)
{
    i2c_status_t s;

    s = i2c_start();
    if (s != I2C_OK) return s;

    s = i2c_write_byte((slave_addr << 1) | 0x00); /* W bit = 0 */
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(reg_addr);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    for (uint8_t i = 0; i < len; i++) {
        s = i2c_write_byte(data[i]);
        if (s != I2C_ACK) { i2c_stop(); return s; }
    }

    i2c_stop();
    return I2C_OK;
}

/**
 * High-level: read N bytes from a 7-bit addressed slave register.
 * Generates START, address+W, reg_addr, Sr, address+R, data[], STOP.
 * Uses Repeated START (no bus release between write and read).
 */
i2c_status_t i2c_read_reg(
    uint8_t  slave_addr,
    uint8_t  reg_addr,
    uint8_t *buf,
    uint8_t  len)
{
    i2c_status_t s;

    s = i2c_start();
    if (s != I2C_OK) return s;

    s = i2c_write_byte((slave_addr << 1) | 0x00);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(reg_addr);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    /* Repeated START — direction change without releasing bus */
    s = i2c_repeated_start();
    if (s != I2C_OK) { i2c_stop(); return s; }

    s = i2c_write_byte((slave_addr << 1) | 0x01); /* R bit = 1 */
    if (s != I2C_ACK) { i2c_stop(); return s; }

    for (uint8_t i = 0; i < len; i++) {
        bool ack = (i < len - 1); /* ACK all except last byte */
        buf[i] = i2c_read_byte(ack);
    }

    i2c_stop();
    return I2C_OK;
}
```

---

### 3. 10-bit Address Transaction (C)

```c
/**
 * i2c_10bit_addr.c
 *
 * Demonstrates UM10204-compliant 10-bit addressing.
 * First byte: 0b11110 A9 A8 R/W̄
 * Second byte: A7..A0
 */

#include <stdint.h>
#include "i2c_bitbang.h"  /* assumes i2c_start, i2c_write_byte, etc. */

#define I2C_10BIT_PREFIX  0xF0u  /* 1111 0xxx */

/**
 * Write to a 10-bit addressed slave.
 */
i2c_status_t i2c_write_10bit(
    uint16_t       addr10,       /* 10-bit slave address */
    uint8_t        reg_addr,
    const uint8_t *data,
    uint8_t        len)
{
    i2c_status_t s;
    uint8_t first_byte = (uint8_t)(I2C_10BIT_PREFIX
                          | ((addr10 >> 7) & 0x06u)  /* A9:A8 in bits 2:1 */
                          | 0x00u);                   /* W = 0 */
    uint8_t second_byte = (uint8_t)(addr10 & 0xFFu);

    s = i2c_start();
    if (s != I2C_OK) return s;

    s = i2c_write_byte(first_byte);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(second_byte);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(reg_addr);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    for (uint8_t i = 0; i < len; i++) {
        s = i2c_write_byte(data[i]);
        if (s != I2C_ACK) { i2c_stop(); return s; }
    }

    i2c_stop();
    return I2C_OK;
}

/**
 * Read from a 10-bit addressed slave using Repeated START.
 * Per UM10204: after Sr, only the first byte (with R=1) is repeated —
 * the second address byte is NOT sent again.
 */
i2c_status_t i2c_read_10bit(
    uint16_t  addr10,
    uint8_t   reg_addr,
    uint8_t  *buf,
    uint8_t   len)
{
    i2c_status_t s;
    uint8_t first_byte_w = (uint8_t)(I2C_10BIT_PREFIX
                            | ((addr10 >> 7) & 0x06u)
                            | 0x00u);
    uint8_t second_byte  = (uint8_t)(addr10 & 0xFFu);
    uint8_t first_byte_r = (uint8_t)(I2C_10BIT_PREFIX
                            | ((addr10 >> 7) & 0x06u)
                            | 0x01u); /* R = 1 */

    s = i2c_start();
    if (s != I2C_OK) return s;

    s = i2c_write_byte(first_byte_w);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(second_byte);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    s = i2c_write_byte(reg_addr);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    /* Repeated START */
    s = i2c_repeated_start();
    if (s != I2C_OK) { i2c_stop(); return s; }

    /* Only first byte again (with R bit) — no second address byte */
    s = i2c_write_byte(first_byte_r);
    if (s != I2C_ACK) { i2c_stop(); return s; }

    for (uint8_t i = 0; i < len; i++) {
        buf[i] = i2c_read_byte(i < len - 1);
    }

    i2c_stop();
    return I2C_OK;
}
```

---

## Programming: Rust Examples

### 1. Compliance Checker in Rust

```rust
//! i2c_compliance.rs
//!
//! Compile-time and runtime I2C timing compliance checker
//! implementing NXP UM10204 Rev.7 requirements.
//!
//! No std required — supports both `std` and `no_std` environments.

#![cfg_attr(not(feature = "std"), no_std)]

/// I2C timing specification (all times in nanoseconds).
#[derive(Debug, Clone, Copy)]
pub struct I2cTimingSpec {
    pub f_scl_max_hz:  u32,
    pub t_hd_sta_min:  u32,
    pub t_su_sta_min:  u32,
    pub t_su_dat_min:  u32,
    pub t_hd_dat_min:  u32,
    pub t_su_sto_min:  u32,
    pub t_buf_min:     u32,
    pub t_low_min:     u32,
    pub t_high_min:    u32,
    pub t_r_max:       u32,
    pub t_f_max:       u32,
    pub cb_max_pf:     u32,
}

/// UM10204 Standard-mode (100 kbit/s) specification.
pub const SM_SPEC: I2cTimingSpec = I2cTimingSpec {
    f_scl_max_hz:  100_000,
    t_hd_sta_min:  4_000,
    t_su_sta_min:  4_700,
    t_su_dat_min:  250,
    t_hd_dat_min:  0,
    t_su_sto_min:  4_000,
    t_buf_min:     4_700,
    t_low_min:     4_700,
    t_high_min:    4_000,
    t_r_max:       1_000,
    t_f_max:       300,
    cb_max_pf:     400,
};

/// UM10204 Fast-mode (400 kbit/s) specification.
pub const FM_SPEC: I2cTimingSpec = I2cTimingSpec {
    f_scl_max_hz:  400_000,
    t_hd_sta_min:  600,
    t_su_sta_min:  600,
    t_su_dat_min:  100,
    t_hd_dat_min:  0,
    t_su_sto_min:  600,
    t_buf_min:     1_300,
    t_low_min:     1_300,
    t_high_min:    600,
    t_r_max:       300,
    t_f_max:       300,
    cb_max_pf:     400,
};

/// UM10204 Fast-mode Plus (1 Mbit/s) specification.
pub const FMP_SPEC: I2cTimingSpec = I2cTimingSpec {
    f_scl_max_hz:  1_000_000,
    t_hd_sta_min:  260,
    t_su_sta_min:  260,
    t_su_dat_min:  50,
    t_hd_dat_min:  0,
    t_su_sto_min:  260,
    t_buf_min:     500,
    t_low_min:     500,
    t_high_min:    260,
    t_r_max:       120,
    t_f_max:       120,
    cb_max_pf:     550,
};

/// Measured timing values from a logic analyser or oscilloscope.
#[derive(Debug, Clone, Copy)]
pub struct I2cMeasured {
    pub t_hd_sta_ns:  u32,
    pub t_su_sta_ns:  u32,
    pub t_su_dat_ns:  u32,
    pub t_hd_dat_ns:  u32,
    pub t_su_sto_ns:  u32,
    pub t_buf_ns:     u32,
    pub t_low_ns:     u32,
    pub t_high_ns:    u32,
    pub t_r_ns:       u32,
    pub t_f_ns:       u32,
    pub scl_freq_hz:  u32,
    pub bus_cap_pf:   u32,
}

/// A single compliance violation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Violation {
    pub param:    &'static str,
    pub kind:     ViolationKind,
    pub required: u32,
    pub measured: u32,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ViolationKind {
    TooLow,   /* measured < minimum */
    TooHigh,  /* measured > maximum */
}

/// Compliance check result — collects all violations.
#[derive(Debug)]
pub struct ComplianceReport {
    pub violations: heapless::Vec<Violation, 16>,
}

impl ComplianceReport {
    pub fn is_compliant(&self) -> bool {
        self.violations.is_empty()
    }
}

/// Check measured I2C timing against a specification.
/// Returns a report containing all violations (empty = compliant).
pub fn check_compliance(
    spec:     &I2cTimingSpec,
    measured: &I2cMeasured,
) -> ComplianceReport {
    let mut report = ComplianceReport {
        violations: heapless::Vec::new(),
    };

    macro_rules! check_min {
        ($measured_field:expr, $spec_field:expr, $param:literal) => {
            if $measured_field < $spec_field {
                let _ = report.violations.push(Violation {
                    param:    $param,
                    kind:     ViolationKind::TooLow,
                    required: $spec_field,
                    measured: $measured_field,
                });
            }
        };
    }

    macro_rules! check_max {
        ($measured_field:expr, $spec_field:expr, $param:literal) => {
            if $measured_field > $spec_field {
                let _ = report.violations.push(Violation {
                    param:    $param,
                    kind:     ViolationKind::TooHigh,
                    required: $spec_field,
                    measured: $measured_field,
                });
            }
        };
    }

    check_min!(measured.t_hd_sta_ns, spec.t_hd_sta_min, "t_HD;STA");
    check_min!(measured.t_su_sta_ns, spec.t_su_sta_min, "t_SU;STA");
    check_min!(measured.t_su_dat_ns, spec.t_su_dat_min, "t_SU;DAT");
    check_min!(measured.t_hd_dat_ns, spec.t_hd_dat_min, "t_HD;DAT");
    check_min!(measured.t_su_sto_ns, spec.t_su_sto_min, "t_SU;STO");
    check_min!(measured.t_buf_ns,    spec.t_buf_min,    "t_BUF");
    check_min!(measured.t_low_ns,    spec.t_low_min,    "t_LOW");
    check_min!(measured.t_high_ns,   spec.t_high_min,   "t_HIGH");

    check_max!(measured.t_r_ns,      spec.t_r_max,      "t_r");
    check_max!(measured.t_f_ns,      spec.t_f_max,      "t_f");
    check_max!(measured.scl_freq_hz, spec.f_scl_max_hz, "f_SCL");
    check_max!(measured.bus_cap_pf,  spec.cb_max_pf,    "C_b");

    report
}

/// Pull-up resistor sizing per UM10204 Section 7.1.
pub struct PullupResult {
    pub rp_min_ohm: f32,
    pub rp_max_ohm: f32,
    pub feasible: bool,
}

pub fn pullup_size(
    vdd_v: f32,
    vol_max_v: f32,
    iol_max_a: f32,
    tr_max_ns: f32,
    cb_pf: f32,
) -> PullupResult {
    let rp_min = (vdd_v - vol_max_v) / iol_max_a;
    let cb_f   = cb_pf * 1e-12;
    let tr_s   = tr_max_ns * 1e-9;
    let rp_max = tr_s / (0.8473 * cb_f);
    PullupResult {
        rp_min_ohm: rp_min,
        rp_max_ohm: rp_max,
        feasible: rp_min < rp_max,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_fm_compliant_passes() {
        let measured = I2cMeasured {
            t_hd_sta_ns: 700,
            t_su_sta_ns: 700,
            t_su_dat_ns: 120,
            t_hd_dat_ns: 10,
            t_su_sto_ns: 700,
            t_buf_ns:    1400,
            t_low_ns:    1400,
            t_high_ns:   700,
            t_r_ns:      250,
            t_f_ns:      150,
            scl_freq_hz: 390_000,
            bus_cap_pf:  80,
        };
        let report = check_compliance(&FM_SPEC, &measured);
        assert!(report.is_compliant(), "Expected compliant, got {:?}", report.violations);
    }

    #[test]
    fn test_fm_violation_detected() {
        let mut measured = I2cMeasured {
            t_hd_sta_ns: 700,
            t_su_sta_ns: 700,
            t_su_dat_ns: 80, /* VIOLATION: < 100 ns minimum */
            t_hd_dat_ns: 0,
            t_su_sto_ns: 700,
            t_buf_ns:    1400,
            t_low_ns:    1400,
            t_high_ns:   700,
            t_r_ns:      250,
            t_f_ns:      150,
            scl_freq_hz: 390_000,
            bus_cap_pf:  80,
        };
        let report = check_compliance(&FM_SPEC, &measured);
        assert!(!report.is_compliant());
        assert_eq!(report.violations[0].param, "t_SU;DAT");
        assert_eq!(report.violations[0].kind, ViolationKind::TooLow);
    }

    #[test]
    fn test_pullup_3v3_100pf_fm() {
        let r = pullup_size(3.3, 0.4, 0.003, 300.0, 100.0);
        assert!(r.feasible, "3.3V 100pF FM should be feasible");
        assert!(r.rp_min_ohm < r.rp_max_ohm);
        /* Typical 4.7k or 2.2k is within range */
    }
}
```

---

### 2. Bit-Banged I2C Master in Rust (embedded-hal)

```rust
//! i2c_bitbang_master.rs
//!
//! UM10204-compliant bit-banged I2C master for embedded Rust.
//! Uses embedded-hal v1.0 traits for GPIO and delay abstraction.
//!
//! [dependencies]
//! embedded-hal = "1.0"

use embedded_hal::delay::DelayNs;
use embedded_hal::digital::{InputPin, OutputPin};

/// UM10204 Fast-mode timing constants (nanoseconds).
const T_HD_STA: u32 = 600;
const T_SU_STA: u32 = 600;
const T_LOW:    u32 = 1_300;
const T_HIGH:   u32 = 600;
const T_SU_DAT: u32 = 100;
const T_SU_STO: u32 = 600;
const T_BUF:    u32 = 1_300;
const STRETCH_LOOPS: u32 = 100_000;

/// I2C error type.
#[derive(Debug, PartialEq, Eq)]
pub enum I2cError {
    Nack,
    BusBusy,
    Timeout,
    ArbitrationLost,
    GpioError,
}

/// Bit-banged I2C master.
pub struct BitBangI2c<SCL, SDA, DELAY>
where
    SCL: OutputPin + InputPin,
    SDA: OutputPin + InputPin,
    DELAY: DelayNs,
{
    scl:   SCL,
    sda:   SDA,
    delay: DELAY,
}

impl<SCL, SDA, DELAY> BitBangI2c<SCL, SDA, DELAY>
where
    SCL: OutputPin + InputPin,
    SDA: OutputPin + InputPin,
    DELAY: DelayNs,
{
    pub fn new(mut scl: SCL, mut sda: SDA, delay: DELAY) -> Self {
        /* Ensure both lines start high (released) */
        let _ = scl.set_high();
        let _ = sda.set_high();
        Self { scl, sda, delay }
    }

    fn scl_low(&mut self) {
        let _ = self.scl.set_low();
    }

    fn scl_release(&mut self) {
        let _ = self.scl.set_high();
    }

    fn scl_read(&mut self) -> bool {
        self.scl.is_high().unwrap_or(false)
    }

    fn sda_low(&mut self) {
        let _ = self.sda.set_low();
    }

    fn sda_release(&mut self) {
        let _ = self.sda.set_high();
    }

    fn sda_read(&mut self) -> bool {
        self.sda.is_high().unwrap_or(false)
    }

    fn delay_ns(&mut self, ns: u32) {
        self.delay.delay_ns(ns);
    }

    /// Wait for SCL to rise (clock stretch support).
    fn scl_wait_high(&mut self) -> Result<(), I2cError> {
        self.scl_release();
        for _ in 0..STRETCH_LOOPS {
            if self.scl_read() {
                return Ok(());
            }
            self.delay_ns(100);
        }
        Err(I2cError::Timeout)
    }

    /// Generate START condition.
    pub fn start(&mut self) -> Result<(), I2cError> {
        if !self.sda_read() || !self.scl_read() {
            return Err(I2cError::BusBusy);
        }
        self.sda_low();
        self.delay_ns(T_HD_STA);
        self.scl_low();
        Ok(())
    }

    /// Generate Repeated START.
    pub fn repeated_start(&mut self) -> Result<(), I2cError> {
        self.sda_release();
        self.delay_ns(T_SU_STA);
        self.scl_wait_high()?;
        self.delay_ns(T_HIGH);
        self.sda_low();
        self.delay_ns(T_HD_STA);
        self.scl_low();
        Ok(())
    }

    /// Generate STOP condition.
    pub fn stop(&mut self) {
        self.sda_low();
        self.delay_ns(T_SU_STO);
        let _ = self.scl_wait_high();
        self.delay_ns(T_HIGH);
        self.sda_release();
        self.delay_ns(T_BUF);
    }

    /// Write one byte; return Ok(()) on ACK, Err(Nack) on NACK.
    pub fn write_byte(&mut self, byte: u8) -> Result<(), I2cError> {
        for bit in (0..8).rev() {
            if byte & (1 << bit) != 0 {
                self.sda_release();
            } else {
                self.sda_low();
            }
            self.delay_ns(T_SU_DAT);
            self.scl_wait_high()?;
            self.delay_ns(T_HIGH);

            /* Arbitration check */
            if (byte & (1 << bit) != 0) && !self.sda_read() {
                self.scl_low();
                return Err(I2cError::ArbitrationLost);
            }

            self.scl_low();
        }

        /* 9th clock — read ACK */
        self.sda_release();
        self.delay_ns(T_SU_DAT);
        self.scl_wait_high()?;
        self.delay_ns(T_HIGH);
        let nack = self.sda_read();
        self.scl_low();

        if nack { Err(I2cError::Nack) } else { Ok(()) }
    }

    /// Read one byte; send ACK if `ack` is true (last byte should send NACK).
    pub fn read_byte(&mut self, ack: bool) -> Result<u8, I2cError> {
        let mut byte = 0u8;
        self.sda_release();

        for bit in (0..8).rev() {
            self.delay_ns(T_SU_DAT);
            self.scl_wait_high()?;
            self.delay_ns(T_HIGH);
            if self.sda_read() {
                byte |= 1 << bit;
            }
            self.scl_low();
        }

        /* Master drives ACK or NACK */
        if ack { self.sda_low(); } else { self.sda_release(); }
        self.delay_ns(T_SU_DAT);
        let _ = self.scl_wait_high();
        self.delay_ns(T_HIGH);
        self.scl_low();
        self.sda_release();

        Ok(byte)
    }

    /// Write a register: START → addr+W → reg → data… → STOP
    pub fn write_reg(
        &mut self,
        addr: u8,
        reg: u8,
        data: &[u8],
    ) -> Result<(), I2cError> {
        self.start()?;
        self.write_byte(addr << 1)?;      /* R/W̄ = 0 */
        self.write_byte(reg)?;
        for &b in data {
            self.write_byte(b)?;
        }
        self.stop();
        Ok(())
    }

    /// Read a register: START → addr+W → reg → Sr → addr+R → data… → STOP
    pub fn read_reg(
        &mut self,
        addr: u8,
        reg: u8,
        buf: &mut [u8],
    ) -> Result<(), I2cError> {
        self.start()?;
        self.write_byte(addr << 1)?;       /* write phase */
        self.write_byte(reg)?;
        self.repeated_start()?;            /* Repeated START — per UM10204 */
        self.write_byte((addr << 1) | 1)?; /* R/W̄ = 1 */

        let len = buf.len();
        for (i, slot) in buf.iter_mut().enumerate() {
            let ack = i < len - 1;
            *slot = self.read_byte(ack)?;
        }
        self.stop();
        Ok(())
    }
}

/// Implement embedded-hal v1.0 I2C trait for full HAL compatibility.
impl<SCL, SDA, DELAY> embedded_hal::i2c::I2c for BitBangI2c<SCL, SDA, DELAY>
where
    SCL: OutputPin + InputPin,
    SDA: OutputPin + InputPin,
    DELAY: DelayNs,
{
    fn transaction(
        &mut self,
        address: u8,
        operations: &mut [embedded_hal::i2c::Operation<'_>],
    ) -> Result<(), Self::Error> {
        use embedded_hal::i2c::Operation;
        let mut first = true;

        for op in operations.iter_mut() {
            if first {
                self.start()?;
                first = false;
            } else {
                self.repeated_start()?;
            }

            match op {
                Operation::Write(data) => {
                    self.write_byte(address << 1)?;
                    for &b in data.iter() {
                        self.write_byte(b)?;
                    }
                }
                Operation::Read(buf) => {
                    self.write_byte((address << 1) | 1)?;
                    let len = buf.len();
                    for (i, slot) in buf.iter_mut().enumerate() {
                        *slot = self.read_byte(i < len - 1)?;
                    }
                }
            }
        }

        self.stop();
        Ok(())
    }
}

impl<SCL, SDA, DELAY> embedded_hal::i2c::ErrorType for BitBangI2c<SCL, SDA, DELAY>
where
    SCL: OutputPin + InputPin,
    SDA: OutputPin + InputPin,
    DELAY: DelayNs,
{
    type Error = I2cError;
}

impl embedded_hal::i2c::Error for I2cError {
    fn kind(&self) -> embedded_hal::i2c::ErrorKind {
        use embedded_hal::i2c::ErrorKind;
        match self {
            I2cError::Nack            => ErrorKind::NoAcknowledge(
                embedded_hal::i2c::NoAcknowledgeSource::Unknown,
            ),
            I2cError::BusBusy         => ErrorKind::Bus,
            I2cError::Timeout         => ErrorKind::Bus,
            I2cError::ArbitrationLost => ErrorKind::ArbitrationLoss,
            I2cError::GpioError       => ErrorKind::Other,
        }
    }
}
```

---

### 3. Reserved Address Detection (Rust)

```rust
//! i2c_reserved_addr.rs
//!
//! UM10204 Table 3: Reserved I2C addresses.
//! Use this before attempting to assign or scan an address.

/// Returns true if the 7-bit address is reserved per UM10204 Table 3.
pub fn is_reserved_address(addr7: u8) -> bool {
    matches!(addr7,
        0x00        /* General Call */
      | 0x01        /* CBUS address */
      | 0x02        /* Reserved — future */
      | 0x03        /* Reserved — future */
      | 0x04..=0x07 /* Hs-mode master codes */
      | 0x78..=0x7B /* 10-bit address prefix */
      | 0x7C..=0x7F /* Reserved */
    )
}

/// Returns the meaning of a reserved address, or None if not reserved.
pub fn reserved_address_description(addr7: u8) -> Option<&'static str> {
    match addr7 {
        0x00        => Some("General Call Address"),
        0x01        => Some("CBUS Address"),
        0x02        => Some("Reserved (future)"),
        0x03        => Some("Reserved (future)"),
        0x04..=0x07 => Some("Hs-mode Master Code"),
        0x78..=0x7B => Some("10-bit Slave Address Prefix (1111 0xx)"),
        0x7C..=0x7F => Some("Reserved"),
        _           => None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn general_call_is_reserved() {
        assert!(is_reserved_address(0x00));
        assert_eq!(reserved_address_description(0x00), Some("General Call Address"));
    }

    #[test]
    fn normal_address_not_reserved() {
        for addr in 0x08..=0x77u8 {
            assert!(!is_reserved_address(addr), "0x{:02X} should not be reserved", addr);
        }
    }

    #[test]
    fn ten_bit_prefix_is_reserved() {
        for addr in 0x78..=0x7Bu8 {
            assert!(is_reserved_address(addr));
        }
    }
}
```

---

## Summary

The NXP UM10204 I2C specification is the normative reference for all I2C implementations. Compliance requires attention at three layers:

**Electrical layer**: Pull-up resistors must be sized to satisfy both the VOL requirement (minimum Rp) and the rise-time requirement (maximum Rp) for the target bus capacitance. For Fast-mode on a 100 pF bus at 3.3 V, the window is approximately 967 Ω to 3.54 kΩ.

**Timing layer**: Every timing parameter in UM10204 Table 10 must be met at the bus pins, not the MCU GPIO. The most commonly violated parameters are `t_SU;DAT` (data setup before SCL rises) and `t_BUF` (bus free time between STOP and START). A logic analyser with automated I2C protocol decoding should be used to verify all parameters against the appropriate speed-mode spec.

**Protocol layer**: The Repeated START condition (Sr) is mandatory for combined write-then-read transactions and must be used instead of issuing STOP followed by a new START. For 10-bit addressing, only the first address byte is re-sent after Sr — the second byte is not repeated. Reserved addresses (0x00–0x07, 0x78–0x7F) must never be used as slave addresses.

**Multi-master compliance**: Arbitration is handled automatically by the open-drain bus topology, but software must detect arbitration loss (driving HIGH while reading LOW on SDA) and back off immediately. Clock synchronisation is also automatic on open-drain SCL, but the master must support clock stretching and must not time out before the slowest expected slave releases SCL.

**Speed modes**: Standard-mode, Fast-mode, and Fast-mode Plus are fully backward-compatible. High-speed mode requires a master code preamble and CBUS-compatible hardware. Ultra Fast-mode is push-pull and unidirectional — incompatible with all other modes.

In C/C++, compliance checking, pull-up sizing, and bit-banged masters can be implemented portably using timing parameter tables and HAL abstractions. In Rust, the `embedded-hal` v1.0 `I2c` trait provides a standardised interface that bit-banged or peripheral-backed drivers can implement, enabling driver code to be fully HAL-independent and tested offline.

---

*Reference: NXP UM10204 — I2C-bus specification and user manual, Rev. 7.0, 2021.*