# 97. SMBus 2.0/3.0 Standards

**Technical depth:**
- Version-by-version evolution from SMBus 1.x through 3.0, with all major feature additions explained
- Complete protocol transaction type descriptions (Quick Command through Block Process Call) with wire-level diagrams
- Detailed electrical and timing tables for both 100 kHz and 1 MHz modes
- PEC (CRC-8/SMBUS) computation explained with the full polynomial and byte-sequencing rules
- ARP, SMBALERT#, and Host Notify protocols fully documented

**C/C++ examples:**
- Full `smbus.h` abstraction layer over Linux `i2c-dev` (all transaction types)
- Manual PEC computation using raw `I2C_RDWR` ioctl
- ARP discovery and address assignment (C++ class)
- SMBALERT# / ARA interrupt handler
- Host Notify receiver

**Rust examples:**
- `SMBusDevice` struct with PEC-aware read/write byte, word, block, and process call
- SMBALERT# watcher using `gpio-cdev`
- Complete Smart Battery (SBS) monitor reading temperature, voltage, current, SoC, and manufacturer name

## Detailed Comparison and Implementation of SMBus Specification Versions

---

## Table of Contents

1. [Introduction](#introduction)
2. [SMBus History and Motivation](#smbus-history-and-motivation)
3. [SMBus vs I2C: Core Differences](#smbus-vs-i2c-core-differences)
4. [SMBus 1.x Baseline](#smbus-1x-baseline)
5. [SMBus 2.0 — Key Features and Changes](#smbus-20--key-features-and-changes)
6. [SMBus 3.0 — Key Features and Changes](#smbus-30--key-features-and-changes)
7. [Protocol Details: Transaction Types](#protocol-details-transaction-types)
8. [Electrical and Timing Specifications](#electrical-and-timing-specifications)
9. [Packet Error Checking (PEC)](#packet-error-checking-pec)
10. [Alert Response Address (ARA)](#alert-response-address-ara)
11. [Host Notify Protocol](#host-notify-protocol)
12. [Address Resolution Protocol (ARP)](#address-resolution-protocol-arp)
13. [C/C++ Implementation Examples](#cc-implementation-examples)
14. [Rust Implementation Examples](#rust-implementation-examples)
15. [Comparison Table: SMBus Versions](#comparison-table-smbus-versions)
16. [Common SMBus Devices and Use Cases](#common-smbus-devices-and-use-cases)
17. [Summary](#summary)

---

## Introduction

The **System Management Bus (SMBus)** is a two-wire serial communication protocol derived from I2C, specifically designed for low-speed system management tasks in embedded and PC systems. While I2C is a general-purpose bus with broad flexibility, SMBus adds strict electrical, timing, and protocol constraints that ensure robust, interoperable communication among heterogeneous system components — particularly in power management, battery monitoring, thermal sensing, and hardware health monitoring.

SMBus has gone through several revisions. The two most significant modern versions are:

- **SMBus 2.0** (released 2000): Introduced the Host Notify protocol, Process Call, 32-byte block transfers, and improved ARP (Address Resolution Protocol).
- **SMBus 3.0** (released 2014): Raised the maximum bus speed to 1 MHz, introduced optional high-speed modes, and clarified many previously ambiguous timing rules.

This document provides an exhaustive technical reference covering both versions, their differences, and practical C/C++ and Rust implementations.

---

## SMBus History and Motivation

| Year | Version | Key Milestone |
|------|---------|---------------|
| 1995 | 1.0 | Initial release by Intel; derived from I2C |
| 1998 | 1.1 | Minor corrections and clarifications |
| 2000 | 2.0 | Host Notify, Process Call, ARP improvements |
| 2014 | 3.0 | 1 MHz speed, optional high-speed mode, improved timing |
| 2021 | 3.2 | Optional MCTP support, minor clarifications |

SMBus was created because raw I2C was too permissive for system management: bus speeds could vary wildly, devices could hold the clock indefinitely, and there was no standard error detection. SMBus solves all of these with mandatory timeouts, fixed voltage levels, and optional CRC (PEC).

---

## SMBus vs I2C: Core Differences

| Feature | I2C | SMBus 2.0 | SMBus 3.0 |
|---------|-----|-----------|-----------|
| Max clock speed | 3.4 MHz (HS) | 100 kHz | 1 MHz |
| Min clock speed | None defined | 10 kHz | 10 kHz |
| Timeout (slave clock stretching) | None | 25 ms max | 25 ms max |
| Bus idle timeout | None | 50 µs | 50 µs |
| Logic high voltage | VDD-based | 3.3V or 5V fixed | 3.3V typical |
| Logic low voltage | VDD-based | < 0.8V | < 0.4V |
| Packet Error Checking | Not defined | Optional (CRC-8) | Optional (CRC-8) |
| Address Resolution | Not defined | ARP (dynamic) | ARP (enhanced) |
| Alert mechanism | Not defined | SMBALERT# pin | SMBALERT# pin |
| Standard transaction types | Read/Write | 9 defined types | 9+ types |
| Multi-master support | Yes | Yes | Yes |

---

## SMBus 1.x Baseline

SMBus 1.x established the foundational protocol on top of I2C:

- **100 kHz maximum** bus clock.
- **7-bit addressing**, mirroring I2C standard mode.
- **Mandatory timeout**: devices must release SDA within 25 ms of any stretching.
- **Defined transaction types**: Quick Command, Send Byte, Receive Byte, Write Byte/Word, Read Byte/Word.
- **Reserved addresses**: `0x00` (General Call), `0x0C` (SMBus Alert Response), `0x28` (ARP), `0x7F–0x7B` (reserved).

---

## SMBus 2.0 — Key Features and Changes

### 1. Host Notify Protocol
Allows a slave device to initiate communication to the host by writing its own address and status data to the host's address (`0x08`). This replaces a dedicated interrupt line in many designs.

### 2. Process Call Transaction
A combined write-then-read in a single transaction, useful for commands that take a 16-bit input and return a 16-bit result (e.g., ADC conversion with gain parameter).

### 3. Block Write/Read Up to 32 Bytes
Extended the maximum block transfer from the earlier limit to exactly **32 bytes**, with the byte count always sent as the first data byte after the command code.

### 4. Block Process Call
Combines a block write and block read in a single transaction — the device processes input data and returns output without releasing the bus.

### 5. Improved ARP (Address Resolution Protocol)
Allows a host to dynamically assign addresses to devices using their **UDID (Unique Device Identifier)** — a 128-bit value burned into each device at manufacturing. This resolves address conflicts in boards where multiple identical ICs are used.

### 6. Packet Error Checking (PEC)
An optional CRC-8 byte appended to every transaction (computed over address, direction, command, and data bytes using the polynomial `x⁸ + x² + x + 1`).

---

## SMBus 3.0 — Key Features and Changes

### 1. 1 MHz Bus Speed
The most visible change: SMBus 3.0 allows bus operation up to **1 MHz**, matching I2C Fast-mode Plus (FM+). This dramatically increases throughput for block transfers while maintaining backward compatibility at 100 kHz.

### 2. High-speed Timing Clarifications
SMBus 3.0 formally defines rise and fall time requirements for 1 MHz operation and specifies that bus capacitance must remain below 400 pF (same as I2C FM+).

### 3. SMBSUS# and Power States
More precise definitions for how devices behave during platform suspend/resume cycles, with explicit requirements around SMBSUS# pin handling.

### 4. Timeout Requirements at 1 MHz
Even at 1 MHz, the 25 ms slave clock stretching timeout is preserved, but the minimum activity timeout is refined: the bus must see a START within **50 µs** of the previous transaction completing or be considered idle.

### 5. Device Default Address (DDA)
Devices may now respond to a defined **default address** during initialization before ARP assigns a permanent address.

### 6. Optional MCTP (SMBus 3.2)
The later 3.2 addendum allows SMBus to carry **Management Component Transport Protocol (MCTP)** packets, enabling firmware/management traffic alongside sensor data.

---

## Protocol Details: Transaction Types

All SMBus transactions follow the I2C framing convention: START → Address+R/W → ACK → Data → STOP.

### 1. Quick Command
```
S | Addr[6:0] | R/W | A | P
```
Sends only the R/W bit — no data. Used as a boolean trigger (e.g., turn device on/off).

### 2. Send Byte
```
S | Addr | Wr | A | CommandCode | A | P
```

### 3. Receive Byte
```
S | Addr | Rd | A | DataByte | NA | P
```

### 4. Write Byte
```
S | Addr | Wr | A | Cmd | A | DataByte | A | P
```

### 5. Write Word
```
S | Addr | Wr | A | Cmd | A | DataLow | A | DataHigh | A | P
```

### 6. Read Byte
```
S | Addr | Wr | A | Cmd | A | Sr | Addr | Rd | A | DataByte | NA | P
```

### 7. Read Word
```
S | Addr | Wr | A | Cmd | A | Sr | Addr | Rd | A | DataLow | A | DataHigh | NA | P
```

### 8. Process Call (SMBus 2.0+)
```
S | Addr | Wr | A | Cmd | A | DataLow | A | DataHigh | A |
Sr | Addr | Rd | A | RetLow | A | RetHigh | NA | P
```

### 9. Block Write (up to 32 bytes)
```
S | Addr | Wr | A | Cmd | A | ByteCount | A | Data[0] | A | ... | Data[N-1] | A | P
```

### 10. Block Read (up to 32 bytes)
```
S | Addr | Wr | A | Cmd | A | Sr | Addr | Rd | A | ByteCount | A | Data[0] | A | ... | NA | P
```

### 11. Block Process Call (SMBus 2.0+)
```
S | Addr | Wr | A | Cmd | A | WrCount | A | WrData... | A |
Sr | Addr | Rd | A | RdCount | A | RdData... | NA | P
```

### 12. Host Notify (SMBus 2.0+)
Slave writes TO host address (0x08):
```
S | 0x08 | Wr | A | DevAddr | A | DataLow | A | DataHigh | A | P
```

---

## Electrical and Timing Specifications

### Voltage Levels

| Parameter | SMBus 2.0 (100 kHz) | SMBus 3.0 (1 MHz) |
|-----------|--------------------|--------------------|
| V_HIGH min | 2.1V | 2.1V |
| V_LOW max | 0.8V | 0.4V |
| V_DD | 3.3V ± 0.3V | 3.3V ± 0.3V |
| Max current sink | 350 µA | 4 mA |
| Pull-up to | VDDS (not VDD) | VDDS |

### Timing Parameters (100 kHz)

| Parameter | Min | Max |
|-----------|-----|-----|
| SCL Clock period | 10 µs | 100 µs |
| SCL Low time | 4.7 µs | — |
| SCL High time | 4.0 µs | — |
| SDA setup time | 250 ns | — |
| SDA hold time | 300 ns | — |
| Rise time (SDA, SCL) | — | 1000 ns |
| Fall time (SDA, SCL) | — | 300 ns |
| Timeout (device) | — | 25 ms |
| Bus idle (before new START) | 50 µs | — |

### Timing Parameters (SMBus 3.0, 1 MHz)

| Parameter | Min | Max |
|-----------|-----|-----|
| SCL Clock period | 1 µs | — |
| SCL Low time | 500 ns | — |
| SCL High time | 260 ns | — |
| Rise time | — | 120 ns |
| Fall time | — | 120 ns |
| SDA setup time | 100 ns | — |
| SDA hold time | 100 ns | — |

---

## Packet Error Checking (PEC)

PEC is an optional CRC-8 byte appended to every transaction. It is computed over the **entire message including address bytes**.

**Polynomial:** `x⁸ + x² + x + 1` (CRC-8/SMBUS, also known as CRC-8 Dallas/Maxim)

**Seed:** 0x00

**Computation sequence for a Write Byte:**
```
PEC = CRC8(Addr_Wr, Cmd, Data)
```
Where `Addr_Wr` is `(slave_addr << 1) | 0` (write direction bit included).

**For a Read Byte (two phases):**
```
PEC = CRC8(Addr_Wr, Cmd, Addr_Rd, Data)
```

The master appends PEC as the last byte; the slave verifies it and NACKs if incorrect.

---

## Alert Response Address (ARA)

The **SMBALERT#** pin is an open-drain, active-low signal shared across all devices. When any device asserts it, the host performs an ARA transaction:

```
S | 0x0C | Rd | A | DevAddr | NA | P
```

The responding device releases the SMBALERT# line and its address is returned, allowing the host to query it. If multiple devices assert simultaneously, the lowest address wins arbitration (consistent with I2C arbitration rules).

---

## Host Notify Protocol

In SMBus 2.0, a slave can "call home" to the host by writing to the predefined **Host Address (0x08)**:

1. Slave detects condition (overcurrent, temperature alarm, etc.)
2. Slave becomes temporary master, generates START
3. Slave writes to `0x08` with its own address and a 16-bit status word
4. Host ACKs, reads the data, then serves the device

This eliminates dedicated interrupt pins in multi-device systems and is the foundation for modern battery management and power delivery notification systems.

---

## Address Resolution Protocol (ARP)

ARP allows the SMBus host to dynamically discover and assign 7-bit addresses to devices using their **128-bit UDID**. The ARP reserved address is `0x61` (write) / `0x61` (read).

### ARP Transaction Sequence

1. **Prepare to ARP** (`0x01`): Reset all devices to unresolved state.
2. **Reset Device** (`0x02`): Force specific device (by UDID) to re-enter ARP.
3. **Get UDID (General)** (`0x03`): All unresolved devices respond; arbitration selects one.
4. **Assign Address** (`0x04`): Host assigns a 7-bit address to the winning UDID.
5. **Get UDID (Directed)** (`0x07`): Query a specific already-assigned device.

UDID structure (16 bytes):
```
[Byte 0]     : Device Capabilities
[Byte 1-2]   : Version/Revision
[Byte 3-4]   : Vendor ID
[Byte 5-6]   : Device ID
[Byte 7-8]   : Interface (SMBus version)
[Byte 9-15]  : Vendor-specific unique ID
```

---

## C/C++ Implementation Examples

### 1. Core SMBus Abstraction Layer (Linux i2c-dev)

```c
/*
 * smbus.h - SMBus 2.0/3.0 abstraction over Linux i2c-dev
 * Requires: #include <linux/i2c-dev.h>, <linux/i2c.h>
 */

#ifndef SMBUS_H
#define SMBUS_H

#include <stdint.h>
#include <stdbool.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#define SMBUS_MAX_BLOCK  32
#define SMBUS_PEC_POLY   0x07  /* CRC-8/SMBUS polynomial */

typedef struct {
    int fd;
    uint8_t slave_addr;
    bool pec_enabled;
} smbus_ctx_t;

/* ── CRC-8/SMBUS ─────────────────────────────────────────── */
static uint8_t smbus_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ SMBUS_PEC_POLY;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* ── Bus Open/Close ──────────────────────────────────────── */
int smbus_open(smbus_ctx_t *ctx, const char *dev, uint8_t addr, bool pec) {
    ctx->fd = open(dev, O_RDWR);
    if (ctx->fd < 0) return -errno;

    ctx->slave_addr = addr;
    ctx->pec_enabled = pec;

    if (ioctl(ctx->fd, I2C_SLAVE, addr) < 0) return -errno;

    if (pec && ioctl(ctx->fd, I2C_PEC, 1) < 0) return -errno;

    return 0;
}

void smbus_close(smbus_ctx_t *ctx) {
    if (ctx->fd >= 0) {
        close(ctx->fd);
        ctx->fd = -1;
    }
}

/* ── Quick Command ──────────────────────────────────────── */
int smbus_quick_command(smbus_ctx_t *ctx, bool read) {
    return i2c_smbus_write_quick(ctx->fd, read ? I2C_SMBUS_READ : I2C_SMBUS_WRITE);
}

/* ── Send/Receive Byte ──────────────────────────────────── */
int smbus_send_byte(smbus_ctx_t *ctx, uint8_t value) {
    return i2c_smbus_write_byte(ctx->fd, value);
}

int smbus_receive_byte(smbus_ctx_t *ctx, uint8_t *value) {
    int ret = i2c_smbus_read_byte(ctx->fd);
    if (ret < 0) return ret;
    *value = (uint8_t)ret;
    return 0;
}

/* ── Write/Read Byte ─────────────────────────────────────── */
int smbus_write_byte(smbus_ctx_t *ctx, uint8_t cmd, uint8_t value) {
    return i2c_smbus_write_byte_data(ctx->fd, cmd, value);
}

int smbus_read_byte(smbus_ctx_t *ctx, uint8_t cmd, uint8_t *value) {
    int ret = i2c_smbus_read_byte_data(ctx->fd, cmd);
    if (ret < 0) return ret;
    *value = (uint8_t)ret;
    return 0;
}

/* ── Write/Read Word (little-endian) ─────────────────────── */
int smbus_write_word(smbus_ctx_t *ctx, uint8_t cmd, uint16_t value) {
    return i2c_smbus_write_word_data(ctx->fd, cmd, value);
}

int smbus_read_word(smbus_ctx_t *ctx, uint8_t cmd, uint16_t *value) {
    int ret = i2c_smbus_read_word_data(ctx->fd, cmd);
    if (ret < 0) return ret;
    *value = (uint16_t)ret;
    return 0;
}

/* ── Process Call (SMBus 2.0) ────────────────────────────── */
int smbus_process_call(smbus_ctx_t *ctx, uint8_t cmd,
                       uint16_t send, uint16_t *recv) {
    int ret = i2c_smbus_process_call(ctx->fd, cmd, send);
    if (ret < 0) return ret;
    *recv = (uint16_t)ret;
    return 0;
}

/* ── Block Write (SMBus 2.0, up to 32 bytes) ─────────────── */
int smbus_block_write(smbus_ctx_t *ctx, uint8_t cmd,
                      const uint8_t *data, uint8_t len) {
    if (len > SMBUS_MAX_BLOCK) return -EINVAL;
    return i2c_smbus_write_block_data(ctx->fd, cmd, len, data);
}

/* ── Block Read (SMBus 2.0, up to 32 bytes) ──────────────── */
int smbus_block_read(smbus_ctx_t *ctx, uint8_t cmd,
                     uint8_t *data, uint8_t *len) {
    uint8_t buf[SMBUS_MAX_BLOCK + 1];
    int ret = i2c_smbus_read_block_data(ctx->fd, cmd, buf);
    if (ret < 0) return ret;
    *len = (uint8_t)ret;
    memcpy(data, buf, *len);
    return 0;
}

#endif /* SMBUS_H */
```

---

### 2. PEC Manual Computation (for raw I2C transactions)

```c
/*
 * When the kernel driver does not handle PEC automatically, compute
 * and append/verify manually using raw I2C_RDWR ioctl.
 */
#include "smbus.h"

/* PEC for: Write Byte to register */
static int smbus_write_byte_pec(int fd, uint8_t addr,
                                uint8_t cmd, uint8_t value) {
    uint8_t pec_buf[3] = {
        (uint8_t)((addr << 1) | 0),  /* Addr + Write bit */
        cmd,
        value
    };
    uint8_t pec = smbus_crc8(pec_buf, 3);

    uint8_t buf[3] = { cmd, value, pec };

    struct i2c_msg msg = {
        .addr  = addr,
        .flags = 0,
        .len   = 3,
        .buf   = buf,
    };
    struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };

    return ioctl(fd, I2C_RDWR, &data) < 0 ? -errno : 0;
}

/* PEC for: Read Byte from register */
static int smbus_read_byte_pec(int fd, uint8_t addr,
                               uint8_t cmd, uint8_t *value) {
    uint8_t write_buf[1] = { cmd };
    uint8_t read_buf[2]  = { 0 };    /* data + PEC */

    struct i2c_msg msgs[2] = {
        { .addr = addr, .flags = 0,          .len = 1, .buf = write_buf },
        { .addr = addr, .flags = I2C_M_RD,   .len = 2, .buf = read_buf  },
    };
    struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = 2 };

    if (ioctl(fd, I2C_RDWR, &data) < 0) return -errno;

    /* Verify PEC over: AddrWr, Cmd, AddrRd, Data */
    uint8_t pec_buf[4] = {
        (uint8_t)((addr << 1) | 0),  /* Write address */
        cmd,
        (uint8_t)((addr << 1) | 1),  /* Read address */
        read_buf[0]
    };
    uint8_t expected_pec = smbus_crc8(pec_buf, 4);

    if (expected_pec != read_buf[1]) return -EIO;  /* PEC mismatch */

    *value = read_buf[0];
    return 0;
}
```

---

### 3. ARP — Dynamic Address Assignment (C++)

```cpp
/*
 * smbus_arp.cpp — SMBus 2.0 Address Resolution Protocol
 * Demonstrates discovering and assigning addresses using UDID.
 */

#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>

constexpr uint8_t  SMBUS_ARP_ADDR        = 0x61;
constexpr uint8_t  ARP_CMD_PREPARE       = 0x01;
constexpr uint8_t  ARP_CMD_RESET_DEV     = 0x02;
constexpr uint8_t  ARP_CMD_GET_UDID_GEN  = 0x03;
constexpr uint8_t  ARP_CMD_ASSIGN_ADDR   = 0x04;
constexpr size_t   UDID_SIZE             = 16;

struct UDIDEntry {
    uint8_t  udid[UDID_SIZE];
    uint8_t  assigned_addr;
};

class SMBusARP {
public:
    explicit SMBusARP(int fd) : fd_(fd) {}

    /* Step 1: Broadcast "Prepare to ARP" — all devices reset */
    void prepareToARP() {
        uint8_t cmd = ARP_CMD_PREPARE;
        sendToARP(&cmd, 1);
    }

    /* Step 2: Read one UDID (arbitration selects lowest address) */
    bool getUDIDGeneral(UDIDEntry &entry) {
        /* Write: ARP address + GET_UDID_GENERAL command */
        /* Read:  17 bytes = 1 byte count + 16 byte UDID */
        uint8_t write_buf[1] = { ARP_CMD_GET_UDID_GEN };
        uint8_t read_buf[17] = {};

        struct i2c_msg msgs[2] = {
            { .addr  = SMBUS_ARP_ADDR,
              .flags = 0,
              .len   = 1,
              .buf   = write_buf },
            { .addr  = SMBUS_ARP_ADDR,
              .flags = I2C_M_RD,
              .len   = 17,
              .buf   = read_buf },
        };
        struct i2c_rdwr_ioctl_data data = { .msgs = msgs, .nmsgs = 2 };

        if (ioctl(fd_, I2C_RDWR, &data) < 0) return false;

        uint8_t byte_count = read_buf[0];
        if (byte_count != UDID_SIZE) return false;

        memcpy(entry.udid, read_buf + 1, UDID_SIZE);
        return true;
    }

    /* Step 3: Assign a 7-bit address to the device with the given UDID */
    bool assignAddress(const uint8_t udid[UDID_SIZE], uint8_t new_addr) {
        /* Payload: CMD(1) + ByteCount(1) + UDID(16) + AddrByte(1) */
        uint8_t buf[19];
        buf[0] = ARP_CMD_ASSIGN_ADDR;
        buf[1] = 17;   /* byte count covers UDID + address byte */
        memcpy(buf + 2, udid, UDID_SIZE);
        buf[18] = (uint8_t)((new_addr << 1) | 1);  /* address + "address valid" flag */

        sendToARP(buf, sizeof(buf));
        return true;
    }

    /* Discover all ARP-capable devices and assign sequential addresses */
    std::vector<UDIDEntry> discoverAll(uint8_t start_addr = 0x10) {
        std::vector<UDIDEntry> found;
        prepareToARP();

        UDIDEntry entry{};
        uint8_t next_addr = start_addr;

        while (getUDIDGeneral(entry)) {
            entry.assigned_addr = next_addr;
            assignAddress(entry.udid, next_addr);
            found.push_back(entry);
            next_addr++;
        }
        return found;
    }

private:
    int fd_;

    void sendToARP(const uint8_t *buf, size_t len) {
        struct i2c_msg msg = {
            .addr  = SMBUS_ARP_ADDR,
            .flags = 0,
            .len   = (uint16_t)len,
            .buf   = const_cast<uint8_t *>(buf),
        };
        struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };

        if (ioctl(fd_, I2C_RDWR, &data) < 0)
            throw std::runtime_error("ARP send failed");
    }
};
```

---

### 4. Alert Response Address (ARA) Handling

```c
/*
 * smbus_ara.c — SMBALERT# + Alert Response Address handler
 *
 * Assumes SMBALERT# is connected to a GPIO interrupt.
 * On interrupt: poll ARA (0x0C) to find which device asserted.
 */

#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define ARA_ADDRESS  0x0C

/*
 * smbus_alert_response - Read the alert device's address via ARA.
 * Returns the 7-bit device address on success, -1 on error.
 */
int smbus_alert_response(int bus_fd) {
    uint8_t recv = 0;

    struct i2c_msg msg = {
        .addr  = ARA_ADDRESS,
        .flags = I2C_M_RD,
        .len   = 1,
        .buf   = &recv,
    };
    struct i2c_rdwr_ioctl_data data = { .msgs = &msg, .nmsgs = 1 };

    if (ioctl(bus_fd, I2C_RDWR, &data) < 0)
        return -1;

    /*
     * The device returns its own address shifted left by 1.
     * Bit 0 encodes whether it is the "most recent" alerter
     * when multiple devices simultaneously asserted SMBALERT#.
     */
    return (int)(recv >> 1);  /* extract 7-bit address */
}

/*
 * Example alert handler called from a GPIO IRQ callback:
 */
void on_smbalert_irq(int bus_fd, void (*handle_device)(int addr)) {
    int alerting_addr;

    /* Poll until no more devices are asserting SMBALERT# */
    while ((alerting_addr = smbus_alert_response(bus_fd)) >= 0) {
        printf("SMBALERT# from device 0x%02X\n", alerting_addr);
        handle_device(alerting_addr);
    }
}
```

---

### 5. Host Notify Receiver (SMBus 2.0)

```c
/*
 * smbus_host_notify.c
 *
 * The SMBus host must listen on address 0x08.
 * This example uses Linux's slave mode (i2c-slave backend).
 */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define HOST_NOTIFY_ADDR  0x08

typedef struct {
    uint8_t  device_addr;   /* Address of the notifying device */
    uint16_t status;        /* 16-bit status payload */
} host_notify_msg_t;

/*
 * Register this process as a slave at address 0x08.
 * Requires kernel i2c-slave-mqueue or similar backend.
 */
int smbus_host_notify_init(int bus_fd) {
    return ioctl(bus_fd, I2C_SLAVE, HOST_NOTIFY_ADDR);
}

/*
 * Parse a raw 3-byte Host Notify payload:
 *   Byte 0: device address (the notifying slave's address << 1)
 *   Byte 1: data_low
 *   Byte 2: data_high
 */
int parse_host_notify(const uint8_t raw[3], host_notify_msg_t *msg) {
    if (!raw || !msg) return -1;

    msg->device_addr = raw[0] >> 1;
    msg->status      = (uint16_t)(raw[1] | (raw[2] << 8));
    return 0;
}

void process_host_notify(int bus_fd) {
    uint8_t buf[3] = {0};

    /* Blocking read — waits for a slave to write 3 bytes to 0x08 */
    ssize_t n = read(bus_fd, buf, sizeof(buf));
    if (n == 3) {
        host_notify_msg_t msg;
        if (parse_host_notify(buf, &msg) == 0) {
            printf("Host Notify from 0x%02X, status=0x%04X\n",
                   msg.device_addr, msg.status);
        }
    }
}
```

---

## Rust Implementation Examples

### 1. SMBus Context with PEC Support

```rust
// smbus.rs — SMBus 2.0/3.0 implementation using linux-i2c crate
//
// Add to Cargo.toml:
//   i2cdev = "0.6"
//   thiserror = "1"

use i2cdev::core::I2CTransfer;
use i2cdev::linux::{LinuxI2CBus, LinuxI2CDevice, LinuxI2CMessage};
use std::path::Path;
use thiserror::Error;

const SMBUS_MAX_BLOCK: usize = 32;
const PEC_POLY: u8 = 0x07;

#[derive(Debug, Error)]
pub enum SMBusError {
    #[error("I2C error: {0}")]
    I2C(#[from] i2cdev::linux::LinuxI2CError),
    #[error("PEC mismatch (expected {expected:#04x}, got {got:#04x})")]
    PECMismatch { expected: u8, got: u8 },
    #[error("Block length {0} exceeds SMBus maximum of 32")]
    BlockTooLong(usize),
    #[error("Invalid response length")]
    InvalidLength,
}

pub type SMBusResult<T> = Result<T, SMBusError>;

/// Compute CRC-8/SMBUS over a byte slice.
pub fn crc8_smbus(data: &[u8]) -> u8 {
    let mut crc: u8 = 0x00;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ PEC_POLY;
            } else {
                crc <<= 1;
            }
        }
    }
    crc
}

pub struct SMBusDevice {
    dev: LinuxI2CDevice,
    addr: u8,
    pec: bool,
}

impl SMBusDevice {
    pub fn new<P: AsRef<Path>>(
        path: P,
        addr: u8,
        pec: bool,
    ) -> SMBusResult<Self> {
        let dev = LinuxI2CDevice::new(path, addr as u16)?;
        Ok(Self { dev, addr, pec })
    }

    // ── Write Byte ──────────────────────────────────────────
    pub fn write_byte(&mut self, cmd: u8, value: u8) -> SMBusResult<()> {
        if self.pec {
            // Compute PEC over: AddrWr, Cmd, Data
            let pec_input = [
                (self.addr << 1) | 0, // write direction
                cmd,
                value,
            ];
            let pec = crc8_smbus(&pec_input);
            let buf = [cmd, value, pec];
            self.dev.write(&buf).map_err(SMBusError::I2C)
        } else {
            self.dev.write(&[cmd, value]).map_err(SMBusError::I2C)
        }
    }

    // ── Read Byte ───────────────────────────────────────────
    pub fn read_byte(&mut self, cmd: u8) -> SMBusResult<u8> {
        let read_len: usize = if self.pec { 2 } else { 1 };
        let mut buf = vec![0u8; read_len];

        let mut messages = [
            LinuxI2CMessage::write(&[cmd]),
            LinuxI2CMessage::read(&mut buf),
        ];
        self.dev.transfer(&mut messages)?;

        if self.pec {
            let pec_input = [
                (self.addr << 1) | 0, // write phase
                cmd,
                (self.addr << 1) | 1, // read phase
                buf[0],
            ];
            let expected = crc8_smbus(&pec_input);
            if expected != buf[1] {
                return Err(SMBusError::PECMismatch {
                    expected,
                    got: buf[1],
                });
            }
        }
        Ok(buf[0])
    }

    // ── Write Word (little-endian) ───────────────────────────
    pub fn write_word(&mut self, cmd: u8, value: u16) -> SMBusResult<()> {
        let lo = (value & 0xFF) as u8;
        let hi = (value >> 8) as u8;
        if self.pec {
            let pec_input = [(self.addr << 1) | 0, cmd, lo, hi];
            let pec = crc8_smbus(&pec_input);
            self.dev.write(&[cmd, lo, hi, pec]).map_err(SMBusError::I2C)
        } else {
            self.dev.write(&[cmd, lo, hi]).map_err(SMBusError::I2C)
        }
    }

    // ── Read Word ───────────────────────────────────────────
    pub fn read_word(&mut self, cmd: u8) -> SMBusResult<u16> {
        let read_len = if self.pec { 3 } else { 2 };
        let mut buf = vec![0u8; read_len];

        let mut messages = [
            LinuxI2CMessage::write(&[cmd]),
            LinuxI2CMessage::read(&mut buf),
        ];
        self.dev.transfer(&mut messages)?;

        if self.pec {
            let pec_input = [
                (self.addr << 1) | 0,
                cmd,
                (self.addr << 1) | 1,
                buf[0],
                buf[1],
            ];
            let expected = crc8_smbus(&pec_input);
            if expected != buf[2] {
                return Err(SMBusError::PECMismatch {
                    expected,
                    got: buf[2],
                });
            }
        }
        Ok(u16::from_le_bytes([buf[0], buf[1]]))
    }

    // ── Block Write (SMBus 2.0) ──────────────────────────────
    pub fn block_write(&mut self, cmd: u8, data: &[u8]) -> SMBusResult<()> {
        if data.len() > SMBUS_MAX_BLOCK {
            return Err(SMBusError::BlockTooLong(data.len()));
        }
        let byte_count = data.len() as u8;
        let mut buf = Vec::with_capacity(2 + data.len() + 1);
        buf.push(cmd);
        buf.push(byte_count);
        buf.extend_from_slice(data);

        if self.pec {
            let mut pec_input = Vec::with_capacity(1 + buf.len());
            pec_input.push((self.addr << 1) | 0);
            pec_input.extend_from_slice(&buf);
            let pec = crc8_smbus(&pec_input);
            buf.push(pec);
        }

        self.dev.write(&buf).map_err(SMBusError::I2C)
    }

    // ── Block Read (SMBus 2.0) ───────────────────────────────
    pub fn block_read(&mut self, cmd: u8) -> SMBusResult<Vec<u8>> {
        // Read byte_count first, then up to 32 data bytes (+ optional PEC)
        let mut count_buf = [0u8; 1];
        {
            let mut msgs = [
                LinuxI2CMessage::write(&[cmd]),
                LinuxI2CMessage::read(&mut count_buf),
            ];
            self.dev.transfer(&mut msgs)?;
        }

        let byte_count = count_buf[0] as usize;
        if byte_count == 0 || byte_count > SMBUS_MAX_BLOCK {
            return Err(SMBusError::InvalidLength);
        }

        let read_len = byte_count + if self.pec { 1 } else { 0 };
        let mut data = vec![0u8; read_len];
        {
            let mut msgs = [
                LinuxI2CMessage::write(&[cmd]),
                LinuxI2CMessage::read(&mut data),
            ];
            self.dev.transfer(&mut msgs)?;
        }

        if self.pec {
            let received_pec = data[byte_count];
            let mut pec_input = Vec::with_capacity(4 + byte_count);
            pec_input.push((self.addr << 1) | 0);
            pec_input.push(cmd);
            pec_input.push((self.addr << 1) | 1);
            pec_input.push(byte_count as u8);
            pec_input.extend_from_slice(&data[..byte_count]);
            let expected = crc8_smbus(&pec_input);
            if expected != received_pec {
                return Err(SMBusError::PECMismatch {
                    expected,
                    got: received_pec,
                });
            }
            data.truncate(byte_count);
        }
        Ok(data)
    }

    // ── Process Call (SMBus 2.0) ─────────────────────────────
    pub fn process_call(&mut self, cmd: u8, send: u16) -> SMBusResult<u16> {
        let lo = (send & 0xFF) as u8;
        let hi = (send >> 8) as u8;
        let mut recv = [0u8; 2];

        let mut msgs = [
            LinuxI2CMessage::write(&[cmd, lo, hi]),
            LinuxI2CMessage::read(&mut recv),
        ];
        self.dev.transfer(&mut msgs)?;

        Ok(u16::from_le_bytes(recv))
    }
}
```

---

### 2. SMBALERT# Handler in Rust

```rust
// smbus_alert.rs — SMBALERT# / ARA handling using gpio-cdev + i2cdev

use gpio_cdev::{Chip, EventRequestFlags, LineRequestFlags};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

const ARA_ADDRESS: u8 = 0x0C;

/// Perform an ARA (Alert Response Address) read on the given bus fd.
/// Returns the 7-bit address of the alerting device, or None on failure.
pub fn smbus_alert_response(bus_fd: i32) -> Option<u8> {
    use libc::{c_int, ioctl};

    // i2c_msg and i2c_rdwr_ioctl_data mapped directly
    // (using nix or direct libc for raw ioctl)
    let mut recv: u8 = 0;

    // This is a simplified illustration; in practice use nix or i2cdev bindings.
    let result = unsafe {
        // ioctl I2C_RDWR to issue a 1-byte read to ARA_ADDRESS
        // Returns recv byte from the arbitration winner
        let _ = bus_fd; // suppress unused warning in example
        recv = 0x42;    // placeholder — real code calls ioctl
        0i32           // placeholder return
    };

    if result < 0 {
        None
    } else {
        Some(recv >> 1) // 7-bit device address
    }
}

/// Spawn a thread watching a GPIO line for SMBALERT# assertion.
pub fn watch_smbalert<F>(
    gpio_chip: &str,
    gpio_line: u32,
    bus_fd: i32,
    handler: F,
) -> std::thread::JoinHandle<()>
where
    F: Fn(u8) + Send + 'static,
{
    let chip_path = gpio_chip.to_string();
    let running = Arc::new(AtomicBool::new(true));
    let r = running.clone();

    std::thread::spawn(move || {
        let mut chip = Chip::new(&chip_path).expect("open GPIO chip");
        let line = chip.get_line(gpio_line).expect("get line");

        let events = line
            .events(
                LineRequestFlags::INPUT,
                EventRequestFlags::FALLING_EDGE,
                "smbalert",
            )
            .expect("request events");

        while r.load(Ordering::Relaxed) {
            if let Ok(_event) = events.get_event() {
                // SMBALERT# asserted — poll ARA until clear
                while let Some(addr) = smbus_alert_response(bus_fd) {
                    println!("SMBALERT# from device 0x{:02X}", addr);
                    handler(addr);
                }
            }
        }
    })
}
```

---

### 3. SMBus Battery Monitor Example (Rust)

```rust
// battery_monitor.rs
// Reads a Smart Battery (SBS-compliant) device using SMBus 2.0 commands.
// Typical Smart Battery sits at address 0x0B.

mod smbus;
use smbus::{SMBusDevice, SMBusResult};

const BATT_ADDR: u8 = 0x0B;

// Smart Battery System (SBS) command codes
const SBS_TEMP:             u8 = 0x08;
const SBS_VOLTAGE:          u8 = 0x09;
const SBS_CURRENT:          u8 = 0x0A;
const SBS_RELATIVE_SOC:     u8 = 0x0D;
const SBS_ABSOLUTE_SOC:     u8 = 0x0E;
const SBS_REMAINING_CAP:    u8 = 0x0F;
const SBS_FULL_CHARGE_CAP:  u8 = 0x10;
const SBS_CYCLE_COUNT:      u8 = 0x17;
const SBS_DESIGN_VOLTAGE:   u8 = 0x19;
const SBS_MANUFACTURER_NAME: u8 = 0x20;

#[derive(Debug)]
pub struct BatteryStatus {
    pub temperature_c: f32,
    pub voltage_mv: u16,
    pub current_ma: i16,
    pub relative_soc: u8,
    pub remaining_mah: u16,
    pub full_capacity_mah: u16,
    pub cycle_count: u16,
    pub manufacturer: String,
}

pub fn read_battery(dev: &mut SMBusDevice) -> SMBusResult<BatteryStatus> {
    // Temperature: in units of 0.1 K; convert to Celsius
    let temp_raw = dev.read_word(SBS_TEMP)?;
    let temperature_c = (temp_raw as f32 / 10.0) - 273.15;

    let voltage_mv       = dev.read_word(SBS_VOLTAGE)?;
    let current_raw      = dev.read_word(SBS_CURRENT)? as i16;
    let relative_soc     = dev.read_byte(SBS_RELATIVE_SOC)?;
    let remaining_mah    = dev.read_word(SBS_REMAINING_CAP)?;
    let full_capacity_mah = dev.read_word(SBS_FULL_CHARGE_CAP)?;
    let cycle_count      = dev.read_word(SBS_CYCLE_COUNT)?;

    // Block read for manufacturer name string
    let name_bytes = dev.block_read(SBS_MANUFACTURER_NAME)?;
    let manufacturer = String::from_utf8_lossy(&name_bytes).into_owned();

    Ok(BatteryStatus {
        temperature_c,
        voltage_mv,
        current_ma: current_raw,
        relative_soc,
        remaining_mah,
        full_capacity_mah,
        cycle_count,
        manufacturer,
    })
}

fn main() -> SMBusResult<()> {
    let mut dev = SMBusDevice::new("/dev/i2c-1", BATT_ADDR, true)?; // PEC enabled

    match read_battery(&mut dev) {
        Ok(status) => {
            println!("Manufacturer:    {}", status.manufacturer);
            println!("Temperature:     {:.1}°C", status.temperature_c);
            println!("Voltage:         {} mV", status.voltage_mv);
            println!("Current:         {} mA", status.current_ma);
            println!("State of Charge: {}%", status.relative_soc);
            println!("Remaining:       {} mAh / {} mAh",
                     status.remaining_mah, status.full_capacity_mah);
            println!("Cycle Count:     {}", status.cycle_count);
        }
        Err(e) => eprintln!("Battery read failed: {}", e),
    }

    Ok(())
}
```

---

## Comparison Table: SMBus Versions

| Feature | SMBus 1.0 | SMBus 1.1 | SMBus 2.0 | SMBus 3.0 |
|---------|-----------|-----------|-----------|-----------|
| Max speed | 100 kHz | 100 kHz | 100 kHz | **1 MHz** |
| Min speed | 10 kHz | 10 kHz | 10 kHz | 10 kHz |
| Timeout | 25 ms | 25 ms | 25 ms | 25 ms |
| Quick Command | ✓ | ✓ | ✓ | ✓ |
| Send/Receive Byte | ✓ | ✓ | ✓ | ✓ |
| Write/Read Byte | ✓ | ✓ | ✓ | ✓ |
| Write/Read Word | ✓ | ✓ | ✓ | ✓ |
| Block Write/Read (32B) | ✗ | Partial | ✓ | ✓ |
| Process Call | ✗ | ✗ | ✓ | ✓ |
| Block Process Call | ✗ | ✗ | ✓ | ✓ |
| PEC (CRC-8) | ✗ | ✗ | ✓ | ✓ |
| ARP (dynamic addressing) | ✗ | ✗ | ✓ | ✓ (enhanced) |
| Host Notify | ✗ | ✗ | ✓ | ✓ |
| SMBALERT# | ✓ | ✓ | ✓ | ✓ |
| SMBSUS# | ✗ | ✗ | ✓ | ✓ (refined) |
| MCTP transport | ✗ | ✗ | ✗ | Optional (3.2) |
| VIL max | 0.8V | 0.8V | 0.8V | **0.4V** |
| IOL (current sink) | 350 µA | 350 µA | 350 µA | 4 mA |

---

## Common SMBus Devices and Use Cases

| Device Type | Example ICs | SMBus Features Used |
|-------------|-------------|---------------------|
| Smart Battery | BQ40Z80, SBS-compliant cells | Block Read, Write Word, PEC |
| Power Management | LM75, INA3221 | Write/Read Byte/Word, SMBALERT# |
| DDR SPD EEPROM | AT24C02 | Read Byte, Block Read |
| Thermal Sensor | TMP102, ADT7461 | Read Word, SMBALERT#, ARA |
| Fan Controller | MAX6639, NCT7904 | Write Byte, SMBALERT# |
| Voltage Regulator | ISL68201, PMBUS | Block Write, Process Call, PEC |
| System MCU | EC (Embedded Controller) | Host Notify, all transaction types |
| DIMM SPD | SPD5118 (DDR5) | ARP, Block Read |

---

## Summary

SMBus is a tightly specified subset of I2C designed for reliable, interoperable system management. Its evolution across versions addresses key operational needs:

**SMBus 1.x** established the essential framework: a 100 kHz maximum speed, mandatory 25 ms clock-stretch timeout, defined voltage thresholds, and five basic transaction types (Quick Command through Read/Write Word). The SMBALERT# pin provided a shared interrupt mechanism.

**SMBus 2.0** added the features that define modern embedded management buses. The **Host Notify** protocol lets slave devices alert the host without dedicated interrupt pins. **Process Call** enables atomic command-response transactions. **32-byte block transfers** accommodate structured data like EEPROM or firmware payloads. **PEC (Packet Error Checking)** with CRC-8 provides end-to-end data integrity. The enhanced **ARP** allows hot-plug systems to resolve address conflicts using 128-bit UDIDs. Together these features make SMBus 2.0 the foundation of standards like Smart Battery System (SBS), PMBus, and IPMI.

**SMBus 3.0** modernized the electrical and speed profile. The headline change is **1 MHz maximum speed** (matching I2C Fast-mode Plus), dramatically improving throughput for block transfers. Tighter low-level voltage thresholds (VIL max dropped to 0.4V, current sink increased to 4 mA) enable operation in noisier environments. Timing definitions were clarified and harmonized with contemporary I2C standards. All SMBus 2.0 features are preserved, so 3.0 devices are fully backward compatible.

When implementing SMBus in software, the key considerations are: always enforce timeouts (a well-behaved SMBus master must never allow a slave to stretch the clock indefinitely), enable PEC where hardware supports it, and use ARP for any multi-device design where address collisions are possible. On Linux, the `i2c-dev` / `I2C_SMBUS` ioctl interface covers the common transaction types; raw `I2C_RDWR` is needed for manual PEC or ARP sequences. In Rust, the `i2cdev` crate provides a clean async-friendly wrapper while keeping close proximity to the kernel interface for full SMBus control.

---

*Document: 97_SMBus_2_0_3_0_Standards.md — Part of the I2C Protocol Reference Series*