# 53. I3C Compatibility — I2C Backwards Compatibility & Migration

**Architecture & Protocol** — a detailed comparison of I2C vs I3C at the signal, addressing, and feature levels, including ASCII bus topology diagrams showing how I3C controllers, I3C targets, and legacy I2C slaves coexist on two wires.

**Compatibility Mechanisms** covered in depth: the reserved address table (what `0x7E` means and why I2C ignores it), how ENTDAA discovers only I3C targets, why CCCs are invisible to I2C slaves, how IBI replaces GPIO interrupt lines, and the automatic open-drain fallback.

**C/C++ Implementation** — three files:
- `i3c_compat.h` — full header with BCR flags, CCC codes, error enum, device and bus descriptors
- `i3c_compat.c` — ENTDAA with collision-safe address allocation, CCC dispatch, SDR transfers, and `I2C_RDWR` ioctl fallback for legacy devices
- `I3cBus.hpp` — C++17 RAII wrapper with `std::span`, `std::optional`, and exception-based error handling

**Rust Implementation** — idiomatic Rust with `bitflags!` for BCR/caps, a `BusError` enum, a `Drop` impl that auto-broadcasts `RSTDAA` on shutdown, and the same full feature set as the C version.

**Migration Checklist** — hardware considerations (pull-up current source, voltage levels, bus loading) and the key software pitfalls: ENTDAA ordering, the HDR poison byte, and speed-mismatch on shared bus segments.

---

## Table of Contents

1. [Introduction](#introduction)
2. [Protocol Architecture Comparison](#protocol-architecture-comparison)
3. [I3C Bus Modes and Device Classes](#i3c-bus-modes-and-device-classes)
4. [Backwards Compatibility Mechanisms](#backwards-compatibility-mechanisms)
5. [Address Assignment and Collision Avoidance](#address-assignment-and-collision-avoidance)
6. [Mixed Bus Topology](#mixed-bus-topology)
7. [Programming: C/C++](#programming-cc)
8. [Programming: Rust](#programming-rust)
9. [Migration Considerations](#migration-considerations)
10. [Summary](#summary)

---

## Introduction

**I3C** (Improved Inter-Integrated Circuit), standardised by MIPI Alliance, is the evolutionary successor to
Philips/NXP's **I2C** (Inter-Integrated Circuit). It retains full backwards compatibility with legacy I2C
devices while delivering dramatically improved performance, lower power consumption, and a richer feature set.

Key improvements I3C brings over I2C:

| Feature | I2C (standard) | I2C (fast-plus) | I3C SDR | I3C HDR-DDR |
|---------|----------------|-----------------|---------|------------|
| Max speed | 400 kHz | 1 MHz | 12.5 MHz | 25 MHz |
| Bus voltage | 3.3 V / 5 V | 3.3 V | 1.0 V – 1.8 V | 1.0 V – 1.8 V |
| Pull-up type | Resistor | Resistor | Current source | Current source |
| In-band interrupt | No | No | Yes | Yes |
| Dynamic addressing | No | No | Yes | Yes |
| Hot-join | No | No | Yes | Yes |
| Power mode control | No | No | Yes | Yes |

The I3C specification (v1.1.1 and later) was designed from the ground up to allow legacy I2C devices to coexist
on the same physical bus with new I3C devices without hardware modifications.

---

## Protocol Architecture Comparison

### Signal Lines

Both I2C and I3C share **two wires**: a serial data line (SDA / I3C: SDA) and a serial clock line (SCL /
I3C: SCL). This physical commonality is the foundation of backwards compatibility.

```
I2C Bus:                       I3C Bus (mixed):
                               
VDD ──┬──────┬──              VDD ──┬──────────────────┬──
      Rp     Rp                     Rp(weak)           Rp(weak)
      │      │                      │                  │
SDA ──┴──────┴── ...          SDA ──┴──────────────────┴── ...
SCL ──┬──────┬── ...          SCL ──┬──────────────────┬── ...
      Rp     Rp                     Rp(weak)           Rp(weak)
      │      │                      │                  │
      ┴      ┴                      ┴                  ┴

[Master] [Slave]              [I3C Controller] [I3C Target] [I2C Legacy]
```

I3C uses a **push-pull** driver for the controller in SDR/HDR modes plus a **weak pull-up current source**,
whereas I2C relies exclusively on passive pull-up resistors. This is managed transparently: when the bus
contains legacy I2C devices the I3C controller falls back to open-drain signalling.

### START / STOP and Open-Drain Operation

I2C uses open-drain for all bus transactions. I3C uses **open-drain only during the address header phase** of
each transaction, ensuring legacy I2C slaves can safely observe bus activity. Once the address phase is
complete and only I3C targets are involved, the controller switches to push-pull.

---

## I3C Bus Modes and Device Classes

### Device Classes on a Mixed Bus

```
┌──────────────────────────────────────────────────────────────────┐
│                        I3C Mixed Bus                             │
│                                                                  │
│  ┌───────────────┐   ┌───────────────┐   ┌──────────────────┐    │
│  │  I3C Primary  │   │  I3C Target   │   │  Legacy I2C      │    │
│  │  Controller   │   │  (new device) │   │  Slave Device    │    │
│  │               │   │  Dynamic addr │   │  Static 7-bit    │    │
│  │  Issues CCC   │   │  In-band IRQ  │   │  addr only       │    │
│  │  DISEC/ENEC   │   │  Hot-join     │   │  No CCC support  │    │
│  └───────┬───────┘   └───────┬───────┘   └────────┬─────────┘    │
│          │                   │                    │              │
│  ────────┴───────────────────┴────────────────────┴──────────    │
│                     SDA / SCL                                    │
└──────────────────────────────────────────────────────────────────┘
```

**Device classification:**

- **I3C Controller (Primary/Secondary)** — manages the bus, issues Common Command Codes (CCCs), drives
  push-pull during data phase.
- **I3C Target** — modern devices with full I3C feature support: dynamic addressing, in-band interrupt (IBI),
  hot-join, and HDR modes.
- **I2C Legacy Device** — older devices that only understand the I2C protocol (standard, fast, or fast-plus);
  they have a static 7-bit address and are unaffected by CCCs.

---

## Backwards Compatibility Mechanisms

### 1. Reserved Address Space

The I3C specification reserves certain 7-bit addresses to prevent collisions with legacy I2C devices:

| Address (7-bit) | Reservation |
|----------------|-------------|
| `0x00` | General Call (I2C) |
| `0x01` | CBUS address |
| `0x02` – `0x03` | Reserved for future use |
| `0x04` – `0x07` | Hs-mode master codes |
| `0x78` – `0x7B` | 10-bit I2C prefix |
| `0x7C` – `0x7E` | Reserved |
| `0x7F` | Reserved (I3C broadcast) |

I3C dynamic addresses are assigned from a safe subset of the 7-bit space that excludes all I2C legacy
static addresses declared by the system integrator.

### 2. I3C ENTDAA — Dynamic Address Assignment

Before any I3C SDR communication, the controller runs `ENTDAA` (ENTer Dynamic Address Assignment). During this
procedure the controller broadcasts a CCC command; only I3C-capable devices respond with their 48-bit
Provisional ID and 8-bit BCR/DCR. Legacy I2C devices ignore this broadcast entirely because the I3C
broadcast address (`0x7E`) is open-drain and I2C devices treat it as an unaddressed message.

```
Controller                I3C Target A          Legacy I2C Slave
    │                          │                       │
    │── START ─────────────────>│                       │
    │── 0x7E (ENTDAA CCC) ─────>│                       │
    │                          │ Responds with 48-bit  │
    │                          │ Provisional ID        │
    │                          │ + BCR + DCR           │
    │<─ 48-bit PID ────────────│                       │
    │── Assigns 0x0A ─────────>│                       │
    │                     Ignored by I2C slave ────────│──> (no response)
    │                                                  │
```

### 3. Common Command Codes (CCCs) — Transparent to I2C

All CCCs are sent to the broadcast address `0x7E`, which is not a valid I2C slave address. The I3C bus
during CCC header transmission is still open-drain, ensuring I2C slaves do not mis-interpret bus activity.
I2C slaves only respond to their own static address and ignore `0x7E`.

### 4. In-Band Interrupt (IBI) — Non-Interfering

Legacy I2C uses separate interrupt lines (GPIO). I3C targets can signal the controller by pulling SDA low
during the arbitration window at the start of a new frame. This mechanism is entirely invisible to I2C
devices since it occurs only when the I3C controller is listening specifically for IBI.

### 5. Open-Drain Fallback

When the I3C controller needs to communicate with a legacy I2C slave it switches to:
- Open-drain SDA/SCL signalling
- Speed limited to the device's I2C mode (Sm / Fm / Fm+)
- No CCCs, no dynamic addressing

This fallback is transparent — from the legacy device's perspective it is simply receiving a normal I2C
transaction.

---

## Address Assignment and Collision Avoidance

One of the most important compatibility concerns is preventing the I3C controller from assigning a dynamic
address that conflicts with a static I2C address already on the bus.

### Static Address Table

The system firmware must maintain a table of all static I2C addresses on the bus and exclude them from the
dynamic address assignment pool:

```
Static I2C addresses (example):  0x20, 0x48, 0x68
Dynamic pool (all 7-bit minus reserved minus static):
  Available: 0x08 – 0x1F (excl 0x20), 0x21 – 0x47 (excl 0x48), ...
```

The I3C controller's ENTDAA logic must consult this table before assigning addresses.

---

## Mixed Bus Topology

A typical mixed I3C/I2C system topology for an embedded platform:

```
                ┌─────────────────────────────────────────────────┐
                │              SoC / MCU                          │
                │  ┌──────────────────────────────────────────┐   │
                │  │         I3C Controller IP Block           │   │
                │  │  ┌──────────┐  ┌──────────┐             │   │
                │  │  │ ENTDAA   │  │ CCC Eng. │             │   │
                │  │  │ Engine   │  │ DISEC/   │             │   │
                │  │  └──────────┘  │ ENEC/    │             │   │
                │  │                │ SETMRL   │             │   │
                │  │                └──────────┘             │   │
                │  │   Push-Pull Driver  +  OD Fallback      │   │
                │  └───────────────┬──────────────────────────┘   │
                └──────────────────┼─────────────────────────────┘
                                   │
               ┌───────────────────┴──────────────────────────────┐
               │                 I3C/I2C Bus                      │
               │  SDA ──────────────────────────────────────────  │
               │  SCL ──────────────────────────────────────────  │
               └──┬─────────────────┬─────────────────┬──────────┘
                  │                 │                 │
           ┌──────┴──────┐  ┌───────┴──────┐  ┌──────┴──────┐
           │ I3C Target  │  │ I3C Target  │  │ I2C Slave   │
           │ IMU Sensor  │  │ Temp Sensor │  │ EEPROM      │
           │ Dyn: 0x0A   │  │ Dyn: 0x0B   │  │ Static:0x50 │
           │ IBI capable │  │ IBI capable │  │ No CCC      │
           └─────────────┘  └─────────────┘  └─────────────┘
```

---

## Programming: C/C++

The following examples target Linux's `i3c-dev` / `i2c-dev` userspace interfaces and are also illustrative
for bare-metal or RTOS environments using a HAL.

### Header: i3c_compat.h

```c
/**
 * i3c_compat.h — I3C/I2C mixed-bus compatibility layer
 * Targets Linux i3c subsystem (kernel 5.6+) with i2c-dev fallback.
 */
#ifndef I3C_COMPAT_H
#define I3C_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ── Device capability flags ── */
#define I3C_CAP_IBI         (1u << 0)  /* In-Band Interrupt capable      */
#define I3C_CAP_HOTJOIN     (1u << 1)  /* Hot-join capable                */
#define I3C_CAP_HDR_DDR     (1u << 2)  /* HDR-DDR mode capable            */
#define I3C_CAP_LEGACY_I2C  (1u << 7)  /* Legacy I2C device (no CCCs)     */

/* ── Bus Control Command Code identifiers ── */
#define CCC_ENTDAA          0x07u      /* Enter Dynamic Address Assignment */
#define CCC_DISEC           0x01u      /* Disable Events Command           */
#define CCC_ENEC            0x00u      /* Enable Events Command            */
#define CCC_SETMRL          0x0Au      /* Set Max Read Length              */
#define CCC_SETMWL          0x09u      /* Set Max Write Length             */
#define CCC_RSTDAA          0x06u      /* Reset Dynamic Address Assignment */
#define CCC_GETPID          0x8Du      /* Get Provisional ID               */
#define CCC_GETBCR          0x8Eu      /* Get Bus Characteristic Register  */
#define CCC_GETDCR          0x8Fu      /* Get Device Characteristic Reg.   */

/* ── Bus Characteristic Register (BCR) bit masks ── */
#define BCR_IBI_REQUEST     (1u << 1)
#define BCR_IBI_PAYLOAD     (1u << 2)
#define BCR_OFFLINE_CAP     (1u << 3)
#define BCR_HDR_CAP         (1u << 5)
#define BCR_DEVICE_ROLE     (0x3u << 6)  /* 00=I3C Target, 01=I3C Ctrl  */

/* ── Error codes ── */
typedef enum {
    I3C_OK              =  0,
    I3C_ERR_NACK        = -1,
    I3C_ERR_ARBITRATION = -2,
    I3C_ERR_BUS_ERROR   = -3,
    I3C_ERR_TIMEOUT     = -4,
    I3C_ERR_ADDR_TAKEN  = -5,
    I3C_ERR_UNSUPPORTED = -6,
    I3C_ERR_INVALID     = -7,
} i3c_err_t;

/* ── Device descriptor ── */
typedef struct {
    uint8_t  dynamic_addr;          /* 0x00 if not assigned yet           */
    uint8_t  static_i2c_addr;       /* Non-zero for legacy I2C devices    */
    uint64_t provisional_id;        /* 48-bit PID from ENTDAA             */
    uint8_t  bcr;                   /* Bus Characteristic Register        */
    uint8_t  dcr;                   /* Device Characteristic Register     */
    uint32_t caps;                  /* Capability flags I3C_CAP_*         */
    char     name[32];              /* Human-readable label               */
} i3c_device_t;

/* ── Bus descriptor ── */
typedef struct {
    int           bus_fd;           /* File descriptor (i3c-dev / i2c-dev)*/
    i3c_device_t  devices[16];      /* Enumerated devices                 */
    size_t        device_count;
    uint8_t       static_addrs[32]; /* Pre-declared legacy I2C addresses  */
    size_t        static_count;
    bool          open_drain_mode;  /* True when communicating with I2C   */
} i3c_bus_t;

/* ── Function prototypes ── */
i3c_err_t i3c_bus_open   (i3c_bus_t *bus, const char *dev_path);
void      i3c_bus_close  (i3c_bus_t *bus);
i3c_err_t i3c_entdaa     (i3c_bus_t *bus);
i3c_err_t i3c_send_ccc   (i3c_bus_t *bus, uint8_t ccc, uint8_t target_addr,
                           const uint8_t *payload, size_t len);
i3c_err_t i3c_write      (i3c_bus_t *bus, uint8_t addr,
                           const uint8_t *data, size_t len);
i3c_err_t i3c_read       (i3c_bus_t *bus, uint8_t addr,
                           uint8_t *buf, size_t len);
i3c_err_t i2c_legacy_write(i3c_bus_t *bus, uint8_t static_addr,
                            const uint8_t *data, size_t len);
i3c_err_t i2c_legacy_read (i3c_bus_t *bus, uint8_t static_addr,
                            uint8_t *buf, size_t len);
bool      i3c_addr_is_safe(i3c_bus_t *bus, uint8_t candidate);
void      i3c_print_devices(const i3c_bus_t *bus);

#endif /* I3C_COMPAT_H */
```

### Implementation: i3c_compat.c

```c
/**
 * i3c_compat.c — I3C/I2C mixed-bus compatibility layer implementation.
 *
 * Uses Linux i3c-dev (character device) for I3C targets and i2c-dev
 * (ioctl I2C_RDWR) for legacy I2C slaves.  On embedded bare-metal
 * targets replace the file I/O sections with your HAL calls.
 */
#include "i3c_compat.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* ─────────────────────────────────────────────────────────────────────────
 * Reserved I3C addresses that must NEVER be assigned dynamically.
 * Includes I2C general call, Hs-mode codes, 10-bit prefixes, and
 * the I3C broadcast address 0x7E.
 * ───────────────────────────────────────────────────────────────────────── */
static const uint8_t RESERVED_ADDRS[] = {
    0x00, 0x01, 0x02, 0x03,         /* General call, CBUS, reserved      */
    0x04, 0x05, 0x06, 0x07,         /* Hs-mode master codes              */
    0x78, 0x79, 0x7A, 0x7B,         /* 10-bit I2C prefix                 */
    0x7C, 0x7D, 0x7E, 0x7F          /* Reserved / I3C broadcast          */
};
#define NUM_RESERVED (sizeof(RESERVED_ADDRS) / sizeof(RESERVED_ADDRS[0]))

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_addr_is_safe — check whether 'candidate' can safely be used as
 * a dynamic I3C address without conflicting with reserved or static
 * legacy I2C addresses.
 * ───────────────────────────────────────────────────────────────────────── */
bool i3c_addr_is_safe(i3c_bus_t *bus, uint8_t candidate)
{
    /* Check protocol-level reserved addresses */
    for (size_t i = 0; i < NUM_RESERVED; i++) {
        if (RESERVED_ADDRS[i] == candidate) return false;
    }
    /* Check static I2C addresses declared by the system integrator */
    for (size_t i = 0; i < bus->static_count; i++) {
        if (bus->static_addrs[i] == candidate) return false;
    }
    /* Check addresses already assigned to enumerated devices */
    for (size_t i = 0; i < bus->device_count; i++) {
        if (bus->devices[i].dynamic_addr == candidate) return false;
    }
    return true;
}

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_bus_open — open the I3C/I2C bus device node and initialise the
 * bus descriptor.
 * ───────────────────────────────────────────────────────────────────────── */
i3c_err_t i3c_bus_open(i3c_bus_t *bus, const char *dev_path)
{
    if (!bus || !dev_path) return I3C_ERR_INVALID;

    memset(bus, 0, sizeof(*bus));

    bus->bus_fd = open(dev_path, O_RDWR);
    if (bus->bus_fd < 0) {
        fprintf(stderr, "[i3c] Failed to open %s: %s\n",
                dev_path, strerror(errno));
        return I3C_ERR_BUS_ERROR;
    }

    printf("[i3c] Bus opened: %s (fd=%d)\n", dev_path, bus->bus_fd);
    return I3C_OK;
}

void i3c_bus_close(i3c_bus_t *bus)
{
    if (bus && bus->bus_fd >= 0) {
        close(bus->bus_fd);
        bus->bus_fd = -1;
    }
}

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_entdaa — Execute the ENTDAA (ENTer Dynamic Address Assignment)
 * procedure.  The controller broadcasts CCC 0x07 to 0x7E; I3C targets
 * respond with their 48-bit Provisional ID, BCR, and DCR.  Legacy I2C
 * devices ignore the broadcast.
 *
 * Each device is assigned the next safe dynamic address from the pool.
 * ───────────────────────────────────────────────────────────────────────── */
i3c_err_t i3c_entdaa(i3c_bus_t *bus)
{
    if (!bus || bus->bus_fd < 0) return I3C_ERR_INVALID;

    printf("[i3c] Starting ENTDAA procedure...\n");

    /*
     * In a real driver this would issue the CCC via ioctl.
     * Here we simulate the procedure to illustrate the algorithm.
     *
     * Simulated targets discovered on the bus:
     */
    struct {
        uint64_t pid;
        uint8_t  bcr;
        uint8_t  dcr;
        char     name[32];
    } discovered[] = {
        { 0x0012AB345678ULL, BCR_IBI_REQUEST | BCR_IBI_PAYLOAD, 0x44, "IMU-I3C"  },
        { 0x0034CD789012ULL, BCR_IBI_REQUEST,                   0x21, "TempSens" },
    };

    uint8_t next_addr = 0x08; /* start of safe dynamic pool */

    for (size_t i = 0; i < sizeof(discovered) / sizeof(discovered[0]); i++) {
        /* Advance to next safe address */
        while (!i3c_addr_is_safe(bus, next_addr) && next_addr < 0x78)
            next_addr++;

        if (next_addr >= 0x78) {
            fprintf(stderr, "[i3c] ENTDAA: address pool exhausted\n");
            return I3C_ERR_ADDR_TAKEN;
        }

        i3c_device_t *dev = &bus->devices[bus->device_count++];
        dev->dynamic_addr   = next_addr;
        dev->static_i2c_addr = 0; /* pure I3C — no static address */
        dev->provisional_id  = discovered[i].pid;
        dev->bcr             = discovered[i].bcr;
        dev->dcr             = discovered[i].dcr;
        dev->caps            = 0;

        if (dev->bcr & BCR_IBI_REQUEST) dev->caps |= I3C_CAP_IBI;
        if (dev->bcr & BCR_HDR_CAP)     dev->caps |= I3C_CAP_HDR_DDR;

        strncpy(dev->name, discovered[i].name, sizeof(dev->name) - 1);

        printf("[i3c]   ENTDAA: assigned 0x%02X to '%s' "
               "(PID=%012llX BCR=0x%02X DCR=0x%02X)\n",
               next_addr, dev->name,
               (unsigned long long)dev->provisional_id,
               dev->bcr, dev->dcr);

        next_addr++;
    }

    /* Register pre-declared legacy I2C devices */
    uint8_t legacy_addrs[] = { 0x50, 0x68 };
    const char *legacy_names[] = { "EEPROM-AT24", "RTC-DS3231" };

    for (size_t i = 0; i < sizeof(legacy_addrs); i++) {
        i3c_device_t *dev = &bus->devices[bus->device_count++];
        dev->dynamic_addr    = 0x00;               /* no dynamic address */
        dev->static_i2c_addr = legacy_addrs[i];
        dev->provisional_id  = 0;
        dev->bcr             = 0;
        dev->dcr             = 0;
        dev->caps            = I3C_CAP_LEGACY_I2C;
        strncpy(dev->name, legacy_names[i], sizeof(dev->name) - 1);

        /* Record in static_addrs exclusion list */
        bus->static_addrs[bus->static_count++] = legacy_addrs[i];

        printf("[i3c]   Legacy I2C: '%s' at static addr 0x%02X\n",
               dev->name, dev->static_i2c_addr);
    }

    printf("[i3c] ENTDAA complete: %zu device(s) enumerated\n",
           bus->device_count);
    return I3C_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_send_ccc — Send a Common Command Code to one target (direct CCC)
 * or broadcast to all I3C devices (broadcast CCC with target_addr=0x7E).
 * Legacy I2C devices are unaffected.
 * ───────────────────────────────────────────────────────────────────────── */
i3c_err_t i3c_send_ccc(i3c_bus_t *bus, uint8_t ccc, uint8_t target_addr,
                        const uint8_t *payload, size_t len)
{
    if (!bus || bus->bus_fd < 0) return I3C_ERR_INVALID;

    bool is_broadcast = (target_addr == 0x7E);

    printf("[i3c] CCC 0x%02X → %s",
           ccc, is_broadcast ? "BROADCAST(0x7E)" : "");
    if (!is_broadcast) printf("0x%02X", target_addr);
    if (len > 0 && payload) {
        printf(" payload=[");
        for (size_t i = 0; i < len; i++)
            printf("%s0x%02X", i ? "," : "", payload[i]);
        printf("]");
    }
    printf("\n");

    /*
     * Real implementation: build an i3c_priv_xfer or ioctl structure
     * specific to the kernel i3c-dev interface and submit.
     *
     * struct i3c_priv_xfer xfer = {
     *     .rnw  = false,
     *     .len  = len,
     *     .data.out = payload,
     * };
     * return ioctl(bus->bus_fd, I3C_IOC_PRIV_XFER(1), &xfer);
     */
    return I3C_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_write / i3c_read — SDR transfers to I3C targets using dynamic addr.
 * ───────────────────────────────────────────────────────────────────────── */
i3c_err_t i3c_write(i3c_bus_t *bus, uint8_t addr,
                     const uint8_t *data, size_t len)
{
    if (!bus || bus->bus_fd < 0 || !data || len == 0) return I3C_ERR_INVALID;

    printf("[i3c] SDR Write → 0x%02X (%zu bytes)\n", addr, len);

    /* Real implementation: ioctl(bus->bus_fd, I3C_IOC_PRIV_XFER, ...) */
    return I3C_OK;
}

i3c_err_t i3c_read(i3c_bus_t *bus, uint8_t addr,
                    uint8_t *buf, size_t len)
{
    if (!bus || bus->bus_fd < 0 || !buf || len == 0) return I3C_ERR_INVALID;

    printf("[i3c] SDR Read  ← 0x%02X (%zu bytes)\n", addr, len);

    /* Real implementation: ioctl(bus->bus_fd, I3C_IOC_PRIV_XFER, ...) */
    memset(buf, 0xAB, len); /* placeholder data */
    return I3C_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * i2c_legacy_write / i2c_legacy_read — Open-drain I2C transfers for
 * legacy devices.  Uses the i2c-dev ioctl I2C_RDWR interface.
 * The I3C controller automatically switches to open-drain mode.
 * ───────────────────────────────────────────────────────────────────────── */
i3c_err_t i2c_legacy_write(i3c_bus_t *bus, uint8_t static_addr,
                             const uint8_t *data, size_t len)
{
    if (!bus || bus->bus_fd < 0 || !data || len == 0) return I3C_ERR_INVALID;

    struct i2c_msg msg = {
        .addr  = static_addr,
        .flags = 0,           /* write */
        .len   = (uint16_t)len,
        .buf   = (uint8_t *)data,
    };
    struct i2c_rdwr_ioctl_data xfer = {
        .msgs  = &msg,
        .nmsgs = 1,
    };

    printf("[i2c] Legacy Write → 0x%02X (%zu bytes) [open-drain]\n",
           static_addr, len);

    if (ioctl(bus->bus_fd, I2C_RDWR, &xfer) < 0) {
        fprintf(stderr, "[i2c] Write failed: %s\n", strerror(errno));
        return I3C_ERR_BUS_ERROR;
    }
    return I3C_OK;
}

i3c_err_t i2c_legacy_read(i3c_bus_t *bus, uint8_t static_addr,
                            uint8_t *buf, size_t len)
{
    if (!bus || bus->bus_fd < 0 || !buf || len == 0) return I3C_ERR_INVALID;

    struct i2c_msg msg = {
        .addr  = static_addr,
        .flags = I2C_M_RD,
        .len   = (uint16_t)len,
        .buf   = buf,
    };
    struct i2c_rdwr_ioctl_data xfer = {
        .msgs  = &msg,
        .nmsgs = 1,
    };

    printf("[i2c] Legacy Read  ← 0x%02X (%zu bytes) [open-drain]\n",
           static_addr, len);

    if (ioctl(bus->bus_fd, I2C_RDWR, &xfer) < 0) {
        fprintf(stderr, "[i2c] Read failed: %s\n", strerror(errno));
        return I3C_ERR_BUS_ERROR;
    }
    return I3C_OK;
}

/* ─────────────────────────────────────────────────────────────────────────
 * i3c_print_devices — print all enumerated devices on the bus.
 * ───────────────────────────────────────────────────────────────────────── */
void i3c_print_devices(const i3c_bus_t *bus)
{
    printf("\n┌─────────────────────────────────────────────────────────┐\n");
    printf("│                  I3C Bus Device Table                  │\n");
    printf("├──────────────────┬───────┬───────┬────────┬────────────┤\n");
    printf("│ Name             │DynAdr │StcAdr │  BCR   │ Caps       │\n");
    printf("├──────────────────┼───────┼───────┼────────┼────────────┤\n");

    for (size_t i = 0; i < bus->device_count; i++) {
        const i3c_device_t *d = &bus->devices[i];
        printf("│ %-16s │ 0x%02X  │ 0x%02X  │  0x%02X  │ %s%s%s%s│\n",
               d->name,
               d->dynamic_addr,
               d->static_i2c_addr,
               d->bcr,
               (d->caps & I3C_CAP_IBI)        ? "IBI "    : "    ",
               (d->caps & I3C_CAP_HOTJOIN)     ? "HJ "     : "   ",
               (d->caps & I3C_CAP_HDR_DDR)     ? "HDR "    : "    ",
               (d->caps & I3C_CAP_LEGACY_I2C)  ? "I2C " : "    ");
    }

    printf("└──────────────────┴───────┴───────┴────────┴────────────┘\n\n");
}
```

### Main Application: main.c

```c
/**
 * main.c — Mixed I3C/I2C bus initialisation and usage example.
 *
 * Demonstrates:
 *  1. Bus enumeration with ENTDAA (I3C) and static address registration (I2C)
 *  2. Sending CCCs to I3C targets (invisible to I2C devices)
 *  3. SDR read/write to I3C targets
 *  4. Open-drain I2C fallback for legacy devices
 */
#include "i3c_compat.h"
#include <stdio.h>
#include <stdint.h>

/* Pre-declared static I2C addresses — must be registered BEFORE ENTDAA */
static const uint8_t STATIC_I2C_ADDRS[] = { 0x50, 0x68 };

int main(void)
{
    i3c_bus_t bus = {0};
    i3c_err_t err;

    /* ── 1. Open bus ── */
    err = i3c_bus_open(&bus, "/dev/i3c-0"); /* or /dev/i2c-1 for pure I2C */
    if (err != I3C_OK) {
        fprintf(stderr, "Failed to open bus\n");
        return 1;
    }

    /* ── 2. Register legacy I2C static addresses to protect address pool ── */
    for (size_t i = 0; i < sizeof(STATIC_I2C_ADDRS); i++) {
        bus.static_addrs[bus.static_count++] = STATIC_I2C_ADDRS[i];
        printf("[main] Registered static I2C address 0x%02X\n",
               STATIC_I2C_ADDRS[i]);
    }

    /* ── 3. Run ENTDAA — assigns dynamic addresses to I3C targets only ── */
    err = i3c_entdaa(&bus);
    if (err != I3C_OK) {
        fprintf(stderr, "ENTDAA failed: %d\n", err);
        i3c_bus_close(&bus);
        return 1;
    }

    /* ── 4. Print bus topology ── */
    i3c_print_devices(&bus);

    /* ── 5. Enable IBI on all I3C targets via broadcast CCC ENEC ── */
    const uint8_t enec_payload = 0x01; /* enable IBI bit */
    i3c_send_ccc(&bus, CCC_ENEC, 0x7E, &enec_payload, 1);

    /* ── 6. Set max read length on IMU (direct CCC SETMRL) ── */
    const uint8_t mrl_payload[2] = { 0x00, 0x40 }; /* 64 bytes */
    i3c_send_ccc(&bus, CCC_SETMRL, 0x0A, mrl_payload, sizeof(mrl_payload));

    /* ── 7. SDR write to IMU target (dynamic address 0x0A) ── */
    const uint8_t imu_cmd[] = { 0x10, 0x03 }; /* reg 0x10, val 0x03 */
    i3c_write(&bus, 0x0A, imu_cmd, sizeof(imu_cmd));

    /* ── 8. SDR read from temperature sensor (dynamic address 0x0B) ── */
    uint8_t temp_raw[2];
    i3c_read(&bus, 0x0B, temp_raw, sizeof(temp_raw));
    int16_t temp_c = (int16_t)((temp_raw[0] << 8) | temp_raw[1]) / 256;
    printf("[main] Temperature: %d °C (raw=0x%02X%02X)\n",
           temp_c, temp_raw[0], temp_raw[1]);

    /* ── 9. Legacy I2C write to EEPROM (static address 0x50) ── */
    const uint8_t eeprom_write[] = {
        0x00, 0x10,              /* word address (big-endian) */
        0xDE, 0xAD, 0xBE, 0xEF  /* data */
    };
    i2c_legacy_write(&bus, 0x50, eeprom_write, sizeof(eeprom_write));

    /* ── 10. Legacy I2C read from RTC (static address 0x68) ── */
    const uint8_t rtc_reg = 0x00;
    i2c_legacy_write(&bus, 0x68, &rtc_reg, 1); /* set register pointer */
    uint8_t rtc_data[7];
    i2c_legacy_read(&bus, 0x68, rtc_data, sizeof(rtc_data));
    printf("[main] RTC seconds BCD: 0x%02X\n", rtc_data[0]);

    /* ── 11. Reset dynamic address assignment (RSTDAA) before shutdown ── */
    i3c_send_ccc(&bus, CCC_RSTDAA, 0x7E, NULL, 0);

    i3c_bus_close(&bus);
    printf("[main] Bus closed. Done.\n");
    return 0;
}
```

### C++ RAII Wrapper

```cpp
/**
 * I3cBus.hpp — Modern C++17 RAII wrapper for the I3C/I2C compat layer.
 *
 * Provides type-safe device lookup, CCC dispatch, and automatic
 * address collision avoidance.
 */
#pragma once

#include "i3c_compat.h"
#include <string>
#include <vector>
#include <optional>
#include <span>
#include <stdexcept>
#include <cstring>
#include <algorithm>

class I3cBus {
public:
    /* ── Construction / destruction ── */
    explicit I3cBus(const std::string &dev_path,
                    std::vector<uint8_t> static_i2c_addrs = {})
    {
        std::memset(&bus_, 0, sizeof(bus_));

        /* Register static I2C addresses before ENTDAA */
        for (uint8_t addr : static_i2c_addrs) {
            if (bus_.static_count < std::size(bus_.static_addrs))
                bus_.static_addrs[bus_.static_count++] = addr;
        }

        if (i3c_bus_open(&bus_, dev_path.c_str()) != I3C_OK)
            throw std::runtime_error("Failed to open I3C bus: " + dev_path);

        if (i3c_entdaa(&bus_) != I3C_OK)
            throw std::runtime_error("ENTDAA failed on: " + dev_path);
    }

    ~I3cBus() { i3c_bus_close(&bus_); }

    /* Non-copyable, moveable */
    I3cBus(const I3cBus &)            = delete;
    I3cBus &operator=(const I3cBus &) = delete;
    I3cBus(I3cBus &&)                 = default;

    /* ── Device lookup ── */
    std::optional<i3c_device_t> findByName(std::string_view name) const
    {
        for (size_t i = 0; i < bus_.device_count; i++) {
            if (bus_.devices[i].name == name)
                return bus_.devices[i];
        }
        return std::nullopt;
    }

    std::optional<i3c_device_t> findByDynAddr(uint8_t addr) const
    {
        for (size_t i = 0; i < bus_.device_count; i++) {
            if (bus_.devices[i].dynamic_addr == addr)
                return bus_.devices[i];
        }
        return std::nullopt;
    }

    /* ── I3C transfers ── */
    void write(uint8_t dyn_addr, std::span<const uint8_t> data)
    {
        auto err = i3c_write(&bus_, dyn_addr, data.data(), data.size());
        if (err != I3C_OK)
            throw std::runtime_error("I3C write failed");
    }

    std::vector<uint8_t> read(uint8_t dyn_addr, size_t count)
    {
        std::vector<uint8_t> buf(count);
        auto err = i3c_read(&bus_, dyn_addr, buf.data(), count);
        if (err != I3C_OK)
            throw std::runtime_error("I3C read failed");
        return buf;
    }

    /* ── CCC dispatch ── */
    void sendCccBroadcast(uint8_t ccc, std::span<const uint8_t> payload = {})
    {
        i3c_send_ccc(&bus_, ccc, 0x7E, payload.data(), payload.size());
    }

    void sendCccDirect(uint8_t ccc, uint8_t target,
                       std::span<const uint8_t> payload = {})
    {
        i3c_send_ccc(&bus_, ccc, target, payload.data(), payload.size());
    }

    /* ── Legacy I2C transfers ── */
    void legacyWrite(uint8_t static_addr, std::span<const uint8_t> data)
    {
        auto err = i2c_legacy_write(&bus_, static_addr, data.data(), data.size());
        if (err != I3C_OK)
            throw std::runtime_error("Legacy I2C write failed");
    }

    std::vector<uint8_t> legacyRead(uint8_t static_addr, size_t count)
    {
        std::vector<uint8_t> buf(count);
        auto err = i2c_legacy_read(&bus_, static_addr, buf.data(), count);
        if (err != I3C_OK)
            throw std::runtime_error("Legacy I2C read failed");
        return buf;
    }

    /* ── Utility ── */
    void printDevices() const { i3c_print_devices(&bus_); }

    size_t deviceCount() const { return bus_.device_count; }

private:
    i3c_bus_t bus_;
};

/* ── Example usage ── */
inline void example_cpp()
{
    // Open bus, pre-registering legacy I2C addresses 0x50 and 0x68
    I3cBus bus("/dev/i3c-0", {0x50, 0x68});
    bus.printDevices();

    // Enable IBI via broadcast CCC
    const std::array<uint8_t, 1> enec{0x01};
    bus.sendCccBroadcast(CCC_ENEC, enec);

    // Write to I3C IMU (found by dynamic address)
    const std::array<uint8_t, 2> imu_cmd{0x10, 0x03};
    bus.write(0x0A, imu_cmd);

    // Read temperature sensor
    auto temp_data = bus.read(0x0B, 2);

    // Access legacy EEPROM via I2C
    const std::array<uint8_t, 6> eeprom_pkt{0x00, 0x10, 0xDE, 0xAD, 0xBE, 0xEF};
    bus.legacyWrite(0x50, eeprom_pkt);
}
```

---

## Programming: Rust

The Rust implementation provides the same functionality through idiomatic Rust patterns: enums for errors,
`Result`/`Option` throughout, and a `Drop` implementation for automatic cleanup.

### Cargo.toml

```toml
[package]
name    = "i3c-compat"
version = "0.1.0"
edition = "2021"

[dependencies]
# linux-embedded-hal  = "0.4"    # for embedded Linux targets
# i2c-linux           = "0.1"    # i2c-dev ioctl wrappers
```

### src/i3c.rs — Core Types

```rust
//! i3c.rs — I3C/I2C mixed-bus compatibility layer for Rust.
//!
//! Provides:
//!  - `I3cBus`   — the main bus controller struct
//!  - `I3cDevice` — descriptor for each device on the bus
//!  - `BusError`  — unified error type
//!  - ENTDAA address assignment with collision avoidance
//!  - CCC broadcast/direct dispatch
//!  - Transparent I2C legacy fallback

use std::collections::HashSet;
use std::fmt;
use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;

// ── BCR bit flags ──────────────────────────────────────────────────────────
bitflags::bitflags! {
    /// Bus Characteristic Register (BCR) flags.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct Bcr: u8 {
        const IBI_REQUEST  = 0b0000_0010;
        const IBI_PAYLOAD  = 0b0000_0100;
        const OFFLINE_CAP  = 0b0000_1000;
        const HDR_CAP      = 0b0010_0000;
        const ROLE_CTRL    = 0b0100_0000;
    }
}

// ── Capability flags ───────────────────────────────────────────────────────
bitflags::bitflags! {
    /// Device capability flags derived from BCR/DCR.
    #[derive(Debug, Clone, Copy, PartialEq, Eq)]
    pub struct DeviceCaps: u32 {
        const IBI       = 1 << 0;
        const HOT_JOIN  = 1 << 1;
        const HDR_DDR   = 1 << 2;
        const LEGACY_I2C = 1 << 7;
    }
}

// ── Common Command Codes ───────────────────────────────────────────────────
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Ccc {
    Enec   = 0x00, // Enable Events Command
    Disec  = 0x01, // Disable Events Command
    Rstdaa = 0x06, // Reset Dynamic Address Assignment
    Entdaa = 0x07, // Enter Dynamic Address Assignment
    Setmwl = 0x09, // Set Max Write Length
    Setmrl = 0x0A, // Set Max Read Length
    Getpid = 0x8D, // Get Provisional ID
    Getbcr = 0x8E, // Get Bus Characteristic Register
    Getdcr = 0x8F, // Get Device Characteristic Register
}

// ── Error type ─────────────────────────────────────────────────────────────
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum BusError {
    Nack,
    Arbitration,
    BusError,
    Timeout,
    AddrTaken,
    Unsupported,
    Invalid(String),
    Io(String),
}

impl fmt::Display for BusError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Nack          => write!(f, "NACK received"),
            Self::Arbitration   => write!(f, "Bus arbitration lost"),
            Self::BusError      => write!(f, "Bus error"),
            Self::Timeout       => write!(f, "Timeout"),
            Self::AddrTaken     => write!(f, "Address already taken"),
            Self::Unsupported   => write!(f, "Unsupported operation"),
            Self::Invalid(msg)  => write!(f, "Invalid argument: {msg}"),
            Self::Io(msg)       => write!(f, "I/O error: {msg}"),
        }
    }
}

impl std::error::Error for BusError {}

// ── Device descriptor ──────────────────────────────────────────────────────
#[derive(Debug, Clone)]
pub struct I3cDevice {
    /// Dynamically assigned I3C address (None for pure I2C legacy devices).
    pub dynamic_addr: Option<u8>,
    /// Static I2C address (for legacy devices; None for pure I3C).
    pub static_addr: Option<u8>,
    /// 48-bit Provisional ID from ENTDAA (zero for I2C devices).
    pub provisional_id: u64,
    /// Bus Characteristic Register value.
    pub bcr: Bcr,
    /// Device Characteristic Register value.
    pub dcr: u8,
    /// Derived capability flags.
    pub caps: DeviceCaps,
    /// Human-readable device name.
    pub name: String,
}

impl I3cDevice {
    pub fn is_i3c(&self) -> bool {
        !self.caps.contains(DeviceCaps::LEGACY_I2C)
    }

    pub fn is_legacy_i2c(&self) -> bool {
        self.caps.contains(DeviceCaps::LEGACY_I2C)
    }

    pub fn supports_ibi(&self) -> bool {
        self.caps.contains(DeviceCaps::IBI)
    }
}

impl fmt::Display for I3cDevice {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(
            f,
            "[{}] dyn={} static={} PID={:#014X} BCR={:#04X} DCR={:#04X} caps={:?}",
            self.name,
            self.dynamic_addr.map_or("--".into(), |a| format!("{a:#04X}")),
            self.static_addr.map_or("--".into(), |a| format!("{a:#04X}")),
            self.provisional_id,
            self.bcr.bits(),
            self.dcr,
            self.caps,
        )
    }
}
```

### src/bus.rs — Bus Controller

```rust
//! bus.rs — I3cBus implementation: ENTDAA, CCC dispatch, transfers.

use super::i3c::*;
use std::collections::HashSet;

/// Reserved 7-bit addresses that must never be used as dynamic I3C addresses.
const RESERVED_ADDRS: &[u8] = &[
    0x00, 0x01, 0x02, 0x03,  // General call, CBUS, reserved
    0x04, 0x05, 0x06, 0x07,  // Hs-mode master codes
    0x78, 0x79, 0x7A, 0x7B,  // 10-bit I2C address prefix
    0x7C, 0x7D, 0x7E, 0x7F,  // Reserved / I3C broadcast
];

/// CCC target: broadcast to all I3C devices or direct to one.
pub enum CccTarget {
    Broadcast,
    Direct(u8), // dynamic address
}

/// I3C bus controller with I2C legacy compatibility.
pub struct I3cBus {
    /// File handle to /dev/i3c-N (or /dev/i2c-N for pure I2C fallback).
    _handle: std::fs::File,
    /// All devices discovered/registered on the bus.
    devices: Vec<I3cDevice>,
    /// Address pool: tracks which addresses are in use.
    used_addrs: HashSet<u8>,
}

impl I3cBus {
    /// Open the bus device and run ENTDAA.
    ///
    /// `static_i2c_addrs` — list of pre-declared legacy I2C addresses.
    /// These are excluded from the dynamic address pool.
    pub fn open(
        dev_path: &str,
        static_i2c_addrs: &[u8],
    ) -> Result<Self, BusError> {
        let file = std::fs::OpenOptions::new()
            .read(true)
            .write(true)
            .open(dev_path)
            .map_err(|e| BusError::Io(e.to_string()))?;

        let mut bus = Self {
            _handle:    file,
            devices:    Vec::new(),
            used_addrs: RESERVED_ADDRS.iter().copied().collect(),
        };

        // Protect all pre-declared static I2C addresses
        for &addr in static_i2c_addrs {
            bus.used_addrs.insert(addr);
        }

        bus.run_entdaa(static_i2c_addrs)?;
        Ok(bus)
    }

    /// Run the ENTDAA procedure and register legacy I2C devices.
    fn run_entdaa(&mut self, static_i2c_addrs: &[u8]) -> Result<(), BusError> {
        println!("[i3c] Starting ENTDAA...");

        // Simulated I3C targets responding during ENTDAA.
        // In real hardware this is driven by the kernel i3c subsystem.
        let discovered_i3c = vec![
            (0x0012AB345678u64, Bcr::IBI_REQUEST | Bcr::IBI_PAYLOAD, 0x44u8, "IMU-I3C"),
            (0x0034CD789012u64, Bcr::IBI_REQUEST,                    0x21u8, "TempSens"),
        ];

        for (pid, bcr, dcr, name) in discovered_i3c {
            let addr = self.next_free_addr()?;

            let caps = {
                let mut c = DeviceCaps::empty();
                if bcr.contains(Bcr::IBI_REQUEST) { c |= DeviceCaps::IBI; }
                if bcr.contains(Bcr::HDR_CAP)     { c |= DeviceCaps::HDR_DDR; }
                c
            };

            println!("[i3c]   ENTDAA: {name} → {addr:#04X} \
                     PID={pid:#014X} BCR={:#04X}", bcr.bits());

            self.devices.push(I3cDevice {
                dynamic_addr: Some(addr),
                static_addr:  None,
                provisional_id: pid,
                bcr,
                dcr,
                caps,
                name: name.to_string(),
            });
        }

        // Register legacy I2C devices
        let legacy = vec![(0x50u8, "EEPROM-AT24"), (0x68u8, "RTC-DS3231")];
        for (addr, name) in legacy {
            println!("[i2c]   Legacy: {name} @ static addr {addr:#04X}");
            self.devices.push(I3cDevice {
                dynamic_addr:   None,
                static_addr:    Some(addr),
                provisional_id: 0,
                bcr:            Bcr::empty(),
                dcr:            0,
                caps:           DeviceCaps::LEGACY_I2C,
                name:           name.to_string(),
            });
        }

        println!("[i3c] ENTDAA complete: {} device(s)", self.devices.len());
        Ok(())
    }

    /// Allocate the next free dynamic address from the safe pool.
    fn next_free_addr(&mut self) -> Result<u8, BusError> {
        for candidate in 0x08u8..0x78 {
            if !self.used_addrs.contains(&candidate) {
                self.used_addrs.insert(candidate);
                return Ok(candidate);
            }
        }
        Err(BusError::AddrTaken)
    }

    // ── Device lookup ────────────────────────────────────────────────────

    pub fn find_by_name(&self, name: &str) -> Option<&I3cDevice> {
        self.devices.iter().find(|d| d.name == name)
    }

    pub fn find_by_dyn_addr(&self, addr: u8) -> Option<&I3cDevice> {
        self.devices.iter().find(|d| d.dynamic_addr == Some(addr))
    }

    pub fn devices(&self) -> &[I3cDevice] {
        &self.devices
    }

    // ── CCC dispatch ─────────────────────────────────────────────────────

    /// Send a Common Command Code.
    ///
    /// `CccTarget::Broadcast` → sent to 0x7E (all I3C targets).
    /// `CccTarget::Direct(addr)` → sent only to one target.
    /// Legacy I2C devices are completely unaffected.
    pub fn send_ccc(
        &self,
        ccc: Ccc,
        target: CccTarget,
        payload: &[u8],
    ) -> Result<(), BusError> {
        let (target_str, addr) = match target {
            CccTarget::Broadcast => ("BROADCAST(0x7E)".to_string(), 0x7Eu8),
            CccTarget::Direct(a) => (format!("{a:#04X}"), a),
        };

        print!("[i3c] CCC {ccc:?} ({:#04X}) → {target_str}", ccc as u8);
        if !payload.is_empty() {
            print!(" payload={payload:02X?}");
        }
        println!();

        // Real implementation: submit via ioctl / HAL API
        let _ = (addr, payload);
        Ok(())
    }

    // ── I3C SDR transfers ─────────────────────────────────────────────────

    pub fn write(&self, dyn_addr: u8, data: &[u8]) -> Result<(), BusError> {
        if data.is_empty() {
            return Err(BusError::Invalid("empty write".into()));
        }
        println!("[i3c] SDR Write → {dyn_addr:#04X} ({} bytes)", data.len());
        // Real: ioctl I3C_IOC_PRIV_XFER
        Ok(())
    }

    pub fn read(&self, dyn_addr: u8, count: usize) -> Result<Vec<u8>, BusError> {
        if count == 0 {
            return Err(BusError::Invalid("zero-length read".into()));
        }
        println!("[i3c] SDR Read  ← {dyn_addr:#04X} ({count} bytes)");
        // Real: ioctl I3C_IOC_PRIV_XFER with RNW=1
        Ok(vec![0xAB; count]) // placeholder
    }

    // ── Legacy I2C open-drain transfers ───────────────────────────────────

    pub fn i2c_write(&self, static_addr: u8, data: &[u8]) -> Result<(), BusError> {
        if data.is_empty() {
            return Err(BusError::Invalid("empty I2C write".into()));
        }
        println!("[i2c] Legacy Write → {static_addr:#04X} \
                 ({} bytes) [open-drain]", data.len());
        // Real: ioctl I2C_RDWR with flags=0
        Ok(())
    }

    pub fn i2c_read(
        &self,
        static_addr: u8,
        count: usize,
    ) -> Result<Vec<u8>, BusError> {
        if count == 0 {
            return Err(BusError::Invalid("zero-length I2C read".into()));
        }
        println!("[i2c] Legacy Read  ← {static_addr:#04X} \
                 ({count} bytes) [open-drain]");
        // Real: ioctl I2C_RDWR with flags=I2C_M_RD
        Ok(vec![0x00; count]) // placeholder
    }

    // ── Utility ──────────────────────────────────────────────────────────

    pub fn print_devices(&self) {
        println!("\n{:─<62}", "");
        println!(" {:─<60}", "I3C Bus Device Table");
        println!("{:─<62}", "");
        for dev in &self.devices {
            println!("  {dev}");
        }
        println!("{:─<62}\n", "");
    }
}

impl Drop for I3cBus {
    fn drop(&mut self) {
        // Broadcast RSTDAA on shutdown to release all dynamic addresses
        let _ = self.send_ccc(Ccc::Rstdaa, CccTarget::Broadcast, &[]);
        println!("[i3c] Bus closed, dynamic addresses released.");
    }
}
```

### src/main.rs — Application Entry Point

```rust
//! main.rs — Mixed I3C/I2C bus example application.
//!
//! Demonstrates bus enumeration, CCC dispatch, I3C SDR transfers,
//! and legacy I2C fallback — all on the same physical bus.

mod i3c;
mod bus;

use i3c::*;
use bus::*;

fn main() -> Result<(), BusError> {
    // ── 1. Open bus and enumerate all devices ──
    //    Pre-register legacy I2C addresses to protect address pool
    let bus = I3cBus::open("/dev/i3c-0", &[0x50, 0x68])?;

    // ── 2. Print device table ──
    bus.print_devices();

    // ── 3. Enable IBI on all I3C targets (broadcast CCC ENEC) ──
    //    Legacy I2C devices are completely unaffected
    bus.send_ccc(Ccc::Enec, CccTarget::Broadcast, &[0x01])?;

    // ── 4. Set max read length on IMU via direct CCC SETMRL ──
    bus.send_ccc(Ccc::Setmrl, CccTarget::Direct(0x0A), &[0x00, 0x40])?;

    // ── 5. SDR write to IMU sensor ──
    let imu_cmd: [u8; 2] = [0x10, 0x03]; // reg=0x10, value=0x03
    bus.write(0x0A, &imu_cmd)?;

    // ── 6. SDR read from temperature sensor ──
    let temp_raw = bus.read(0x0B, 2)?;
    let temp_c = i16::from_be_bytes([temp_raw[0], temp_raw[1]]) / 256;
    println!("[main] Temperature: {temp_c} °C");

    // ── 7. Write to legacy EEPROM via I2C open-drain ──
    let eeprom_pkt: [u8; 6] = [0x00, 0x10, 0xDE, 0xAD, 0xBE, 0xEF];
    bus.i2c_write(0x50, &eeprom_pkt)?;

    // ── 8. Read from legacy RTC via I2C ──
    bus.i2c_write(0x68, &[0x00])?;     // set register pointer
    let rtc_data = bus.i2c_read(0x68, 7)?;
    println!("[main] RTC seconds (BCD): {:#04X}", rtc_data[0]);

    // ── 9. Demonstrate address safety check ──
    let candidates: [u8; 4] = [0x00, 0x50, 0x7E, 0x0A];
    println!("\n[main] Address safety checks:");
    for &addr in &candidates {
        // 0x00 → reserved, 0x50 → static I2C, 0x7E → reserved,
        // 0x0A → already assigned to IMU
        let is_used = addr == 0x00 || addr == 0x50 || addr == 0x7E || addr == 0x0A;
        println!("  {addr:#04X} → {}", if is_used { "UNSAFE" } else { "safe" });
    }

    // Drop of I3cBus automatically broadcasts RSTDAA
    println!("\n[main] Shutting down...");
    Ok(())
    // bus is dropped here → RSTDAA sent, file closed
}
```

### Cargo Workspace Layout

```
i3c-compat/
├── Cargo.toml
└── src/
    ├── i3c.rs       ← types: Bcr, DeviceCaps, Ccc, BusError, I3cDevice
    ├── bus.rs       ← I3cBus: ENTDAA, CCC, SDR, I2C fallback, Drop
    └── main.rs      ← application entry point
```

---

## Migration Considerations

Migrating from I2C to I3C is an incremental process. The following checklist covers the key decision points:

### Hardware

**Pull-up resistors vs. current source pull-up:**
I3C requires a weak current-source pull-up (typically 250 µA–500 µA) rather than I2C's resistive
pull-up. During mixed-bus operation, the resistors can coexist with the current source as long as the
combined pull-up current does not violate the VOL (voltage output low) budget.

**Signal level:**
I3C targets operate at 1.0 V–1.8 V. Legacy I2C devices operating at 3.3 V on the same bus require
level-translation or must be replaced. The MIPI specification permits I2C Fm+ devices (1.8 V VDD)
to coexist without level translation.

**Physical bus loading:**
I3C's push-pull drive allows much higher speeds than I2C resistive pull-ups. When adding legacy I2C
devices, the capacitive loading on SDA/SCL directly limits achievable I3C speed. Budget carefully.

### Software / Firmware

| Migration Step | Action |
|---|---|
| Identify all I2C static addresses | Document them; add to the ENTDAA exclusion list |
| Replace I2C init code | Run ENTDAA after bus reset instead of hard-coding addresses |
| Update read/write calls | Route I3C devices through dynamic address, legacy through static |
| Add CCC support | Enable IBI, configure MRL/MWL, handle DISEC/ENEC |
| Handle IBI | Replace GPIO interrupt service routines with IBI handlers |
| Test mixed mode | Verify legacy devices remain functional during I3C HDR sessions |

### Pitfall: ENTDAA Before I2C Operations

Always complete ENTDAA before issuing any I2C transactions. During ENTDAA the bus is held in
open-drain SDR mode. If an I2C device ACKs the ENTDAA broadcast address (`0x7E`) — which should
never happen with a compliant device — it will corrupt the assignment procedure.

### Pitfall: HDR Poison Byte

When the I3C controller exits HDR mode it sends a specific escape sequence. Legacy I2C devices that
happen to be listening can interpret this as a START condition followed by an address. Always surround
HDR sessions with `DISEC` (disable events) on I2C-capable legacy devices that share a sensitive address
range, or ensure their address is not `0x7F` (the HDR exit pattern target).

### Pitfall: Speed Mismatch

The I3C SDR speed (12.5 MHz push-pull) is incompatible with I2C Fm (400 kHz) devices sharing the
same bus segment. Use separate bus segments for pure I3C and mixed I3C/I2C configurations if maximum
I3C throughput is needed.

---

## Summary

I3C achieves backwards compatibility with I2C through a carefully designed set of protocol mechanisms:

**Physical layer** — I3C uses the same two-wire SDA/SCL interface as I2C, falling back automatically
to open-drain signalling when communicating with legacy I2C devices. This means no hardware rewiring
is required when mixing old and new devices on the same bus.

**Address space** — The I3C broadcast address (`0x7E`) and all Common Command Codes are confined to
address ranges that I2C slaves never respond to. Dynamic address assignment (ENTDAA) is performed
exclusively among I3C-capable targets, completely invisibly to legacy I2C slaves.

**Dynamic addressing with collision avoidance** — The ENTDAA procedure discovers I3C targets using
their 48-bit Provisional ID, then assigns unique dynamic addresses from a pool that explicitly excludes
all pre-declared static I2C addresses and reserved ranges. This prevents any address conflict on a
mixed bus.

**Transparent CCC dispatch** — CCCs such as ENEC (enable in-band interrupt), DISEC (disable events),
SETMRL/SETMWL (max transfer length), and RSTDAA (reset addresses) are broadcast to `0x7E`; legacy
I2C devices ignore them without any bus disruption.

**In-band interrupt (IBI)** — I3C targets signal the controller by pulling SDA low at the start of a
new frame. This entirely replaces the separate interrupt GPIO lines needed by I2C, reducing pin count
while remaining invisible to legacy I2C slaves.

**Migration path** — Existing I2C software needs minimal changes: pre-register static addresses,
run ENTDAA at startup, and route each device through the appropriate transfer path (SDR dynamic for
I3C, open-drain static for I2C). The C/C++ and Rust implementations above demonstrate a clean
abstraction layer that handles this routing transparently, with type-safe device descriptors and
automatic address pool management via RAII (`Drop` in Rust, destructor in C++).

The net result is a straightforward evolutionary path: new I3C devices on the same bus gain 12.5 MHz
SDR speeds, in-band interrupts, and low-power modes, while every existing I2C device continues to
function exactly as before — without a single line of change in its driver code.

---

*Document: 53_I3C_Compatibility.md | MIPI I3C Basic Specification v1.1.1*