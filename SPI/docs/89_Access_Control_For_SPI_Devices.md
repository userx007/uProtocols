# 89. Access Control for SPI Devices

**Concepts & Architecture**
- A threat model diagram showing the SPI attack surface (T1–T6: key theft, command injection, DoS, replay, TOCTOU, write-protection bypass)
- Comparison table showing SPI's lack of built-in addressing, authentication, or arbitration vs. I²C and USB
- Five layered access control strategies explained with ASCII architecture diagrams

**C/C++ Code Examples**
- `spi_bus_manager` — mutex-protected bus with a per-(task, device) ACL, explicit acquire/release/transfer lifecycle
- `spi_rbac` — role table layered on top of the bus manager, mapping tasks → roles → per-device operation masks
- `spi_capability` — HMAC-SHA256 signed capability tokens with expiry, device bitmask, and a nonce revocation list
- `spi_integrity` — per-frame signing with monotonic sequence numbers for anti-replay protection

**Rust Code Examples**
- `SpiManager` with an RAII `SpiDeviceGuard` — holding the guard *is* the proof of authorization; the bus releases automatically on drop
- Type-state RBAC — `ReadOnly`, `ReadWrite`, `Admin` phantom types where calling `write()` on a `ReadOnly` handle is a **compile error**
- Capability token system and frame integrity manager, idiomatic to Rust's error handling with `thiserror`

**Operational Guidance**
- RTOS supervisor architecture with MPU-protected SPI register access via IPC queues
- Tamper-evident chained-HMAC audit log design
- A pitfall table covering 10 common mistakes (TOCTOU races, CS left asserted on crash, weak key management, etc.)

> **Implementing software access control for security-critical SPI peripherals**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Access Control Matters for SPI](#why-access-control-matters-for-spi)
3. [Threat Model](#threat-model)
4. [Core Access Control Concepts](#core-access-control-concepts)
5. [Access Control Strategies](#access-control-strategies)
   - 5.1 [Device Ownership & Mutex Locking](#51-device-ownership--mutex-locking)
   - 5.2 [Role-Based Access Control (RBAC)](#52-role-based-access-control-rbac)
   - 5.3 [Token / Capability-Based Access](#53-token--capability-based-access)
   - 5.4 [Hardware Chip-Select Gating](#54-hardware-chip-select-gating)
   - 5.5 [Transaction Signing & Integrity Checks](#55-transaction-signing--integrity-checks)
6. [C/C++ Implementation](#cc-implementation)
   - 6.1 [Mutex-Protected SPI Bus Manager](#61-mutex-protected-spi-bus-manager)
   - 6.2 [RBAC SPI Controller](#62-rbac-spi-controller)
   - 6.3 [Capability Token System](#63-capability-token-system)
   - 6.4 [HMAC Transaction Integrity](#64-hmac-transaction-integrity)
7. [Rust Implementation](#rust-implementation)
   - 7.1 [Ownership-Enforced SPI Device Manager](#71-ownership-enforced-spi-device-manager)
   - 7.2 [RBAC with Type-State Pattern](#72-rbac-with-type-state-pattern)
   - 7.3 [Capability Token System in Rust](#73-capability-token-system-in-rust)
   - 7.4 [HMAC Transaction Integrity in Rust](#74-hmac-transaction-integrity-in-rust)
8. [Secure SPI Bus Arbitration on RTOS](#secure-spi-bus-arbitration-on-rtos)
9. [Audit Logging](#audit-logging)
10. [Common Pitfalls and Mitigations](#common-pitfalls-and-mitigations)
11. [Summary](#summary)

---

## Introduction

The Serial Peripheral Interface (SPI) is a synchronous, full-duplex communication protocol widely used in embedded systems to interface with security-critical peripherals such as:

- **Secure elements** (e.g., ATECC608, SE050)
- **TPM chips** (Trusted Platform Modules)
- **Cryptographic co-processors**
- **Encrypted flash memories** (e.g., IS25LP, Winbond W25Q with security registers)
- **Hardware Security Modules (HSMs)**
- **Tamper-detection ICs**

Because SPI is a bare-metal bus with no built-in authentication, authorization, or arbitration, any software component with access to the SPI controller registers can potentially send arbitrary commands to these critical peripherals. Without explicit software access control, this creates a significant attack surface: a compromised driver, a buggy RTOS task, or a malicious firmware module could read cryptographic keys, reset security counters, or permanently lock a device.

This document describes a layered approach to implementing robust software access control for security-critical SPI peripherals.

---

## Why Access Control Matters for SPI

Unlike I²C (which has addressing) or USB (which has enumeration and descriptors), raw SPI has no concept of:

| Feature | SPI | I²C | USB |
|---|---|---|---|
| Device addressing on bus | ❌ (CS pin only) | ✅ 7-bit | ✅ endpoint |
| Authentication | ❌ | ❌ | Optional |
| Authorization | ❌ | ❌ | Optional |
| Bus arbitration | ❌ | ✅ (multi-master) | ✅ |
| Command integrity | ❌ | ❌ | CRC |

The Chip Select (CS) line is typically software-controlled via a GPIO and provides only **physical multiplexing**, not logical authorization. Any code that can toggle the CS GPIO and write to the SPI FIFO can communicate with the device. On systems with multiple software components (RTOS tasks, OS processes, virtual machines), this is inadequate for security-critical peripherals.

---

## Threat Model

Before designing access control, define the threat model:

```
┌─────────────────────────────────────────────────────────┐
│                   Attack Surfaces                       │
│                                                         │
│  ┌─────────────┐   ┌─────────────┐   ┌─────────────┐    │
│  │ Compromised │   │  Buggy /    │   │  Privilege  │    │
│  │  App Task   │   │  Untrusted  │   │  Escalation │    │
│  │             │   │   Driver    │   │  Exploit    │    │
│  └──────┬──────┘   └──────┬──────┘   └──────┬──────┘    │
│         │                 │                 │           │
│         └─────────────────┴─────────────────┘           │
│                           │                             │
│                    ┌──────▼──────┐                      │
│                    │  SPI Bus    │   ← No HW protection │
│                    │  Controller │                      │
│                    └──────┬──────┘                      │
│                           │                             │
│              ┌────────────┼──────────┐                  │
│        ┌─────▼──┐   ┌─────▼──┐  ┌────▼───┐              │
│        │Secure  │   │ TPM    │  │Crypto  │              │
│        │Element │   │ Chip   │  │Flash   │              │
│        └────────┘   └────────┘  └────────┘              │
└─────────────────────────────────────────────────────────┘

Threats:
  T1 - Unauthorized read of cryptographic keys or secrets
  T2 - Unauthorized write / command injection to security IC
  T3 - Denial of Service (bus monopolization)
  T4 - Replay attacks on recorded command sequences
  T5 - TOCTOU races on shared bus access
  T6 - Bypassing write-protection registers
```

---

## Core Access Control Concepts

### 1. Identification
Each software entity (task, thread, process) that wants to access an SPI device must have a verifiable identity — typically a task ID, process credential, or cryptographic token.

### 2. Authentication
The access control layer verifies that the caller is who it claims to be. In embedded systems this is often done by checking the caller's task ID against an allowlist, or validating a capability token.

### 3. Authorization
Once identity is established, check whether this identity is **permitted to perform the requested operation** on the target device. This is where RBAC or capability tables come in.

### 4. Mutual Exclusion
The SPI bus must be locked during a complete transaction to prevent interleaving of commands from different callers.

### 5. Integrity Verification
For critical commands, use MACs (Message Authentication Codes) or sequence numbers to detect replays or tampering with the command stream.

### 6. Audit Logging
All access attempts (successful or denied) should be logged for forensic analysis.

---

## Access Control Strategies

### 5.1 Device Ownership & Mutex Locking

The simplest layer: wrap all SPI bus operations in a mutex and track the current owner.

```
┌─────────────────────────────────┐
│        SPI Bus Manager          │
│                                 │
│  owner_id: TaskID               │
│  mutex: BusMutex                │
│  cs_gpio[N]: GPIO[]             │
│                                 │
│  acquire(task_id, device_id)    │
│  release(task_id)               │
│  transfer(buf, len)             │
└─────────────────────────────────┘
```

**Pros:** Simple, prevents concurrent access.  
**Cons:** Does not restrict *which* tasks may access *which* device; only serializes access.

---

### 5.2 Role-Based Access Control (RBAC)

Define roles (e.g., `CRYPTO_ADMIN`, `SENSOR_READER`, `BOOTLOADER`) and map each role to a set of permitted devices and permitted operations.

```
Roles:
  CRYPTO_ADMIN  → {SE050: READ|WRITE|ADMIN, TPM: READ|WRITE}
  APP_TASK      → {SE050: READ}
  BOOTLOADER    → {CRYPTO_FLASH: READ|WRITE, SE050: NONE}

Task → Role mapping (set at initialization time):
  task_crypto   → CRYPTO_ADMIN
  task_app      → APP_TASK
  task_boot     → BOOTLOADER
```

---

### 5.3 Token / Capability-Based Access

Each task is issued a capability token at initialization. The token encodes the permitted devices, operations, and an expiry time. The SPI manager verifies the token before granting access.

```
Capability Token (64 bytes):
  ┌─────────────────────────────────────────────┐
  │ issuer_id  : u16                            │
  │ holder_id  : u32  (task/process ID)         │
  │ device_mask: u32  (bitmask of allowed devs) │
  │ op_mask    : u16  (READ | WRITE | ADMIN)    │
  │ expires_at : u64  (monotonic timestamp)     │
  │ nonce      : u32  (replay protection)       │
  │ hmac       : [u8; 32]  (HMAC-SHA256)        │
  └─────────────────────────────────────────────┘
```

---

### 5.4 Hardware Chip-Select Gating

For maximum isolation, control CS GPIO lines from a **privileged** supervisor layer only. Application tasks request a transaction through a message queue; the supervisor validates and executes it in a controlled environment (MPU-protected region or Trusted Execution Environment).

```
 [App Task] ─── IPC Queue ──► [SPI Supervisor (privileged)]
                                      │
                              Validates capability token
                              Checks RBAC
                              Asserts CS GPIO
                              Executes SPI transfer
                              Deasserts CS GPIO
                              Returns result via IPC
```

---

### 5.5 Transaction Signing & Integrity Checks

For commands sent to cryptographic devices, sign each transaction with a session key shared between the supervisor and the target device. This prevents command injection even if the SPI bus is probed or replayed.

---

## C/C++ Implementation

### 6.1 Mutex-Protected SPI Bus Manager

```c
/* spi_access_control.h */
#ifndef SPI_ACCESS_CONTROL_H
#define SPI_ACCESS_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Maximum devices on the bus */
#define SPI_MAX_DEVICES     8
#define SPI_MAX_TASKS       16
#define SPI_INVALID_OWNER   UINT32_MAX

/* Operation permission flags */
typedef enum {
    SPI_OP_NONE    = 0x00,
    SPI_OP_READ    = 0x01,
    SPI_OP_WRITE   = 0x02,
    SPI_OP_ADMIN   = 0x04,
    SPI_OP_ALL     = 0x07
} spi_op_t;

/* SPI device descriptor */
typedef struct {
    uint8_t     device_id;
    uint8_t     cs_gpio_pin;
    uint8_t     cs_gpio_port;
    uint32_t    max_clock_hz;
    bool        is_security_critical;
    const char *name;
} spi_device_desc_t;

/* Access Control Entry: maps (task_id, device_id) → permissions */
typedef struct {
    uint32_t    task_id;
    uint8_t     device_id;
    spi_op_t    allowed_ops;
    bool        valid;
} spi_ace_t;

/* Access result codes */
typedef enum {
    SPI_ACCESS_OK            =  0,
    SPI_ACCESS_DENIED        = -1,
    SPI_ACCESS_BUSY          = -2,
    SPI_ACCESS_INVALID_DEV   = -3,
    SPI_ACCESS_TIMEOUT       = -4,
    SPI_ACCESS_INTEGRITY_ERR = -5,
} spi_access_result_t;

/* Opaque bus manager handle */
typedef struct spi_bus_manager spi_bus_manager_t;

/* Initialize the bus manager */
spi_bus_manager_t *spi_bus_manager_init(
    const spi_device_desc_t *devices,
    size_t                   num_devices
);

/* Add an Access Control Entry */
spi_access_result_t spi_acl_add(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    uint8_t            device_id,
    spi_op_t           ops
);

/* Acquire exclusive access to a device */
spi_access_result_t spi_acquire(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    uint8_t            device_id,
    spi_op_t           requested_op,
    uint32_t           timeout_ms
);

/* Release the bus */
spi_access_result_t spi_release(
    spi_bus_manager_t *mgr,
    uint32_t           task_id
);

/* Perform a transfer (must hold lock) */
spi_access_result_t spi_transfer(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    const uint8_t     *tx_buf,
    uint8_t           *rx_buf,
    size_t             len
);

#endif /* SPI_ACCESS_CONTROL_H */
```

```c
/* spi_access_control.c */
#include "spi_access_control.h"
#include <string.h>
#include <stdlib.h>

/* ── Platform HAL stubs (replace with your RTOS/BSP calls) ── */
typedef void* mutex_t;
static mutex_t  mutex_create(void)              { return (void*)1; }
static bool     mutex_lock(mutex_t m, uint32_t timeout_ms) {
    (void)m; (void)timeout_ms; return true;
}
static void     mutex_unlock(mutex_t m)         { (void)m; }
static void     gpio_write(uint8_t port, uint8_t pin, bool val) {
    (void)port; (void)pin; (void)val;
}
static uint32_t get_task_id(void)               { return 0; /* scheduler call */ }
static void     spi_hw_transfer(const uint8_t *tx, uint8_t *rx, size_t len) {
    (void)tx; (void)rx; (void)len; /* HAL SPI transfer */
}
static void     log_event(const char *msg, uint32_t task, uint8_t dev) {
    (void)msg; (void)task; (void)dev;
}
/* ──────────────────────────────────────────────────────────── */

struct spi_bus_manager {
    const spi_device_desc_t  *devices;
    size_t                    num_devices;
    spi_ace_t                 acl[SPI_MAX_TASKS * SPI_MAX_DEVICES];
    size_t                    acl_count;
    uint32_t                  current_owner;
    uint8_t                   current_device;
    spi_op_t                  current_op;
    mutex_t                   bus_mutex;
};

/* ── Internal helpers ── */

static const spi_device_desc_t *find_device(
    const spi_bus_manager_t *mgr,
    uint8_t device_id)
{
    for (size_t i = 0; i < mgr->num_devices; i++) {
        if (mgr->devices[i].device_id == device_id) {
            return &mgr->devices[i];
        }
    }
    return NULL;
}

static spi_op_t get_permissions(
    const spi_bus_manager_t *mgr,
    uint32_t                 task_id,
    uint8_t                  device_id)
{
    for (size_t i = 0; i < mgr->acl_count; i++) {
        const spi_ace_t *ace = &mgr->acl[i];
        if (ace->valid && ace->task_id == task_id
                       && ace->device_id == device_id) {
            return ace->allowed_ops;
        }
    }
    return SPI_OP_NONE;
}

/* ── Public API ── */

spi_bus_manager_t *spi_bus_manager_init(
    const spi_device_desc_t *devices,
    size_t                   num_devices)
{
    spi_bus_manager_t *mgr = calloc(1, sizeof(*mgr));
    if (!mgr) return NULL;

    mgr->devices      = devices;
    mgr->num_devices  = num_devices;
    mgr->current_owner  = SPI_INVALID_OWNER;
    mgr->current_device = 0xFF;
    mgr->bus_mutex    = mutex_create();
    return mgr;
}

spi_access_result_t spi_acl_add(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    uint8_t            device_id,
    spi_op_t           ops)
{
    if (mgr->acl_count >= (SPI_MAX_TASKS * SPI_MAX_DEVICES)) {
        return SPI_ACCESS_DENIED;
    }
    if (!find_device(mgr, device_id)) {
        return SPI_ACCESS_INVALID_DEV;
    }
    spi_ace_t *ace    = &mgr->acl[mgr->acl_count++];
    ace->task_id      = task_id;
    ace->device_id    = device_id;
    ace->allowed_ops  = ops;
    ace->valid        = true;
    return SPI_ACCESS_OK;
}

spi_access_result_t spi_acquire(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    uint8_t            device_id,
    spi_op_t           requested_op,
    uint32_t           timeout_ms)
{
    /* 1. Validate device exists */
    const spi_device_desc_t *dev = find_device(mgr, device_id);
    if (!dev) {
        log_event("DENY: unknown device", task_id, device_id);
        return SPI_ACCESS_INVALID_DEV;
    }

    /* 2. Check permissions before locking */
    spi_op_t perms = get_permissions(mgr, task_id, device_id);
    if ((perms & requested_op) != requested_op) {
        log_event("DENY: insufficient permissions", task_id, device_id);
        return SPI_ACCESS_DENIED;
    }

    /* 3. Acquire mutex */
    if (!mutex_lock(mgr->bus_mutex, timeout_ms)) {
        log_event("DENY: timeout acquiring bus", task_id, device_id);
        return SPI_ACCESS_TIMEOUT;
    }

    /* 4. Record owner and activate CS */
    mgr->current_owner  = task_id;
    mgr->current_device = device_id;
    mgr->current_op     = requested_op;

    gpio_write(dev->cs_gpio_port, dev->cs_gpio_pin, false); /* CS active low */
    log_event("GRANT: bus acquired", task_id, device_id);
    return SPI_ACCESS_OK;
}

spi_access_result_t spi_release(
    spi_bus_manager_t *mgr,
    uint32_t           task_id)
{
    if (mgr->current_owner != task_id) {
        log_event("DENY: release by non-owner", task_id, mgr->current_device);
        return SPI_ACCESS_DENIED;
    }

    const spi_device_desc_t *dev = find_device(mgr, mgr->current_device);
    if (dev) {
        gpio_write(dev->cs_gpio_port, dev->cs_gpio_pin, true); /* CS deassert */
    }

    log_event("RELEASE: bus released", task_id, mgr->current_device);
    mgr->current_owner  = SPI_INVALID_OWNER;
    mgr->current_device = 0xFF;
    mutex_unlock(mgr->bus_mutex);
    return SPI_ACCESS_OK;
}

spi_access_result_t spi_transfer(
    spi_bus_manager_t *mgr,
    uint32_t           task_id,
    const uint8_t     *tx_buf,
    uint8_t           *rx_buf,
    size_t             len)
{
    /* Verify caller still owns the bus */
    if (mgr->current_owner != task_id) {
        log_event("DENY: transfer without ownership", task_id, 0xFF);
        return SPI_ACCESS_DENIED;
    }

    /* Validate buffer pointers */
    if (!tx_buf && !rx_buf) {
        return SPI_ACCESS_DENIED;
    }

    /* Perform the hardware transfer */
    spi_hw_transfer(tx_buf, rx_buf, len);
    return SPI_ACCESS_OK;
}
```

**Usage Example:**

```c
/* Usage example — two tasks with different permissions */
#include "spi_access_control.h"

static const spi_device_desc_t devices[] = {
    { .device_id = 0, .cs_gpio_pin = 4, .cs_gpio_port = 0,
      .max_clock_hz = 1000000, .is_security_critical = true,
      .name = "ATECC608_SecureElement" },
    { .device_id = 1, .cs_gpio_pin = 5, .cs_gpio_port = 0,
      .max_clock_hz = 8000000, .is_security_critical = false,
      .name = "W25Q128_Flash" },
};

void security_system_init(void)
{
    spi_bus_manager_t *mgr = spi_bus_manager_init(devices, 2);

    /* Crypto task: full access to secure element, no flash */
    spi_acl_add(mgr, TASK_ID_CRYPTO, 0, SPI_OP_READ | SPI_OP_WRITE | SPI_OP_ADMIN);

    /* App task: read-only from secure element */
    spi_acl_add(mgr, TASK_ID_APP,    0, SPI_OP_READ);

    /* Flash task: full access to flash only */
    spi_acl_add(mgr, TASK_ID_FLASH,  1, SPI_OP_READ | SPI_OP_WRITE);

    /* App task tries to write to secure element → DENIED */
    spi_access_result_t result = spi_acquire(
        mgr, TASK_ID_APP, 0, SPI_OP_WRITE, 100);
    /* result == SPI_ACCESS_DENIED */
}
```

---

### 6.2 RBAC SPI Controller

```c
/* spi_rbac.h — Role-Based Access Control for SPI */
#ifndef SPI_RBAC_H
#define SPI_RBAC_H

#include <stdint.h>
#include "spi_access_control.h"

#define SPI_MAX_ROLES      8

typedef uint8_t spi_role_id_t;

/* Role definition: name + per-device permission bitmask */
typedef struct {
    const char    *name;
    spi_role_id_t  id;
    /* device_perms[device_id] = allowed spi_op_t flags */
    spi_op_t       device_perms[SPI_MAX_DEVICES];
} spi_role_t;

/* Role assignment: task_id → role_id */
typedef struct {
    uint32_t      task_id;
    spi_role_id_t role_id;
} spi_role_assignment_t;

typedef struct spi_rbac_manager spi_rbac_manager_t;

spi_rbac_manager_t *spi_rbac_init(
    const spi_device_desc_t *devices,
    size_t                   num_devices
);

int spi_rbac_define_role(
    spi_rbac_manager_t *rm,
    const spi_role_t   *role
);

int spi_rbac_assign_role(
    spi_rbac_manager_t *rm,
    uint32_t            task_id,
    spi_role_id_t       role_id
);

spi_access_result_t spi_rbac_acquire(
    spi_rbac_manager_t *rm,
    uint32_t            task_id,
    uint8_t             device_id,
    spi_op_t            op,
    uint32_t            timeout_ms
);

spi_access_result_t spi_rbac_release(
    spi_rbac_manager_t *rm,
    uint32_t            task_id
);

spi_access_result_t spi_rbac_transfer(
    spi_rbac_manager_t *rm,
    uint32_t            task_id,
    const uint8_t      *tx,
    uint8_t            *rx,
    size_t              len
);

#endif /* SPI_RBAC_H */
```

```c
/* spi_rbac.c */
#include "spi_rbac.h"
#include <stdlib.h>
#include <string.h>

struct spi_rbac_manager {
    spi_bus_manager_t     *bus;
    spi_role_t             roles[SPI_MAX_ROLES];
    size_t                 num_roles;
    spi_role_assignment_t  assignments[SPI_MAX_TASKS];
    size_t                 num_assignments;
};

spi_rbac_manager_t *spi_rbac_init(
    const spi_device_desc_t *devices,
    size_t                   num_devices)
{
    spi_rbac_manager_t *rm = calloc(1, sizeof(*rm));
    if (!rm) return NULL;
    rm->bus = spi_bus_manager_init(devices, num_devices);
    if (!rm->bus) { free(rm); return NULL; }
    return rm;
}

int spi_rbac_define_role(
    spi_rbac_manager_t *rm,
    const spi_role_t   *role)
{
    if (rm->num_roles >= SPI_MAX_ROLES) return -1;
    memcpy(&rm->roles[rm->num_roles++], role, sizeof(*role));
    return 0;
}

int spi_rbac_assign_role(
    spi_rbac_manager_t *rm,
    uint32_t            task_id,
    spi_role_id_t       role_id)
{
    if (rm->num_assignments >= SPI_MAX_TASKS) return -1;
    rm->assignments[rm->num_assignments].task_id = task_id;
    rm->assignments[rm->num_assignments].role_id = role_id;
    rm->num_assignments++;
    return 0;
}

static const spi_role_t *find_role_for_task(
    const spi_rbac_manager_t *rm,
    uint32_t                  task_id)
{
    for (size_t i = 0; i < rm->num_assignments; i++) {
        if (rm->assignments[i].task_id == task_id) {
            spi_role_id_t rid = rm->assignments[i].role_id;
            for (size_t j = 0; j < rm->num_roles; j++) {
                if (rm->roles[j].id == rid) return &rm->roles[j];
            }
        }
    }
    return NULL;
}

spi_access_result_t spi_rbac_acquire(
    spi_rbac_manager_t *rm,
    uint32_t            task_id,
    uint8_t             device_id,
    spi_op_t            op,
    uint32_t            timeout_ms)
{
    const spi_role_t *role = find_role_for_task(rm, task_id);
    if (!role) return SPI_ACCESS_DENIED;

    if (device_id >= SPI_MAX_DEVICES) return SPI_ACCESS_INVALID_DEV;

    spi_op_t allowed = role->device_perms[device_id];
    if ((allowed & op) != op) return SPI_ACCESS_DENIED;

    /* Register the resolved permission in the underlying ACL */
    spi_acl_add(rm->bus, task_id, device_id, allowed);
    return spi_acquire(rm->bus, task_id, device_id, op, timeout_ms);
}

spi_access_result_t spi_rbac_release(
    spi_rbac_manager_t *rm, uint32_t task_id)
{
    return spi_release(rm->bus, task_id);
}

spi_access_result_t spi_rbac_transfer(
    spi_rbac_manager_t *rm, uint32_t task_id,
    const uint8_t *tx, uint8_t *rx, size_t len)
{
    return spi_transfer(rm->bus, task_id, tx, rx, len);
}

/* ── RBAC setup example ── */
void rbac_setup_example(
    const spi_device_desc_t *devices,
    size_t                   num_devices)
{
    spi_rbac_manager_t *rm = spi_rbac_init(devices, num_devices);

    /* Define roles */
    static const spi_role_t crypto_admin = {
        .name = "CryptoAdmin",
        .id   = 1,
        .device_perms = {
            [0] = SPI_OP_READ | SPI_OP_WRITE | SPI_OP_ADMIN, /* Secure Element */
            [1] = SPI_OP_NONE,                                 /* Flash: no access */
        }
    };
    static const spi_role_t app_reader = {
        .name = "AppReader",
        .id   = 2,
        .device_perms = {
            [0] = SPI_OP_READ,  /* Secure Element: read only */
            [1] = SPI_OP_READ,  /* Flash: read only          */
        }
    };
    static const spi_role_t bootloader = {
        .name = "Bootloader",
        .id   = 3,
        .device_perms = {
            [0] = SPI_OP_NONE,                  /* No secure element */
            [1] = SPI_OP_READ | SPI_OP_WRITE,   /* Flash R/W         */
        }
    };

    spi_rbac_define_role(rm, &crypto_admin);
    spi_rbac_define_role(rm, &app_reader);
    spi_rbac_define_role(rm, &bootloader);

    /* Assign roles to tasks */
    spi_rbac_assign_role(rm, TASK_ID_CRYPTO, 1);
    spi_rbac_assign_role(rm, TASK_ID_APP,    2);
    spi_rbac_assign_role(rm, TASK_ID_BOOT,   3);
}
```

---

### 6.3 Capability Token System

```c
/* spi_capability.h — Capability token system for SPI access */
#ifndef SPI_CAPABILITY_H
#define SPI_CAPABILITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include "spi_access_control.h"

#define SPI_CAP_HMAC_LEN    32
#define SPI_CAP_KEY_LEN     32

typedef struct __attribute__((packed)) {
    uint16_t   issuer_id;       /* Trusted issuer identity   */
    uint32_t   holder_id;       /* Task / process ID         */
    uint32_t   device_mask;     /* Bitmask of allowed devices*/
    uint16_t   op_mask;         /* Allowed operations        */
    uint64_t   expires_at;      /* Monotonic timestamp (ms)  */
    uint32_t   nonce;           /* Anti-replay nonce         */
    uint8_t    hmac[SPI_CAP_HMAC_LEN]; /* HMAC-SHA256 over above fields */
} spi_capability_t;

/* Initialize the capability manager with a master signing key */
int spi_cap_manager_init(const uint8_t key[SPI_CAP_KEY_LEN]);

/* Issue a new capability token */
spi_capability_t spi_cap_issue(
    uint16_t issuer_id,
    uint32_t holder_id,
    uint32_t device_mask,
    uint16_t op_mask,
    uint64_t expires_at
);

/* Verify a token (returns true if valid, not expired, not replayed) */
bool spi_cap_verify(
    const spi_capability_t *cap,
    uint32_t                claimed_holder_id,
    uint8_t                 device_id,
    spi_op_t                requested_op,
    uint64_t                current_time_ms
);

/* Revoke a specific nonce (add to revocation list) */
int spi_cap_revoke(uint32_t nonce);

#endif /* SPI_CAPABILITY_H */
```

```c
/* spi_capability.c */
#include "spi_capability.h"
#include <string.h>
#include <stdlib.h>

/* Stub: replace with your HMAC-SHA256 implementation */
static void hmac_sha256(
    const uint8_t *key, size_t key_len,
    const uint8_t *data, size_t data_len,
    uint8_t out[32])
{
    /* Production: use mbedTLS, wolfSSL, or hardware crypto */
    (void)key; (void)key_len; (void)data; (void)data_len;
    memset(out, 0xAB, 32); /* STUB */
}

static uint64_t monotonic_time_ms(void)
{
    return 0; /* Replace with your RTC / tick counter */
}

#define MAX_REVOKED_NONCES 64

static uint8_t  g_signing_key[SPI_CAP_KEY_LEN];
static uint32_t g_revoked_nonces[MAX_REVOKED_NONCES];
static size_t   g_revoked_count = 0;
static uint32_t g_nonce_counter = 1;

int spi_cap_manager_init(const uint8_t key[SPI_CAP_KEY_LEN])
{
    memcpy(g_signing_key, key, SPI_CAP_KEY_LEN);
    g_revoked_count  = 0;
    g_nonce_counter  = 1;
    return 0;
}

spi_capability_t spi_cap_issue(
    uint16_t issuer_id,
    uint32_t holder_id,
    uint32_t device_mask,
    uint16_t op_mask,
    uint64_t expires_at)
{
    spi_capability_t cap = {
        .issuer_id   = issuer_id,
        .holder_id   = holder_id,
        .device_mask = device_mask,
        .op_mask     = op_mask,
        .expires_at  = expires_at,
        .nonce       = g_nonce_counter++,
    };

    /* HMAC covers all fields except the HMAC itself */
    size_t signed_len = offsetof(spi_capability_t, hmac);
    hmac_sha256(g_signing_key, SPI_CAP_KEY_LEN,
                (const uint8_t *)&cap, signed_len,
                cap.hmac);
    return cap;
}

bool spi_cap_verify(
    const spi_capability_t *cap,
    uint32_t                claimed_holder_id,
    uint8_t                 device_id,
    spi_op_t                requested_op,
    uint64_t                current_time_ms)
{
    /* 1. Verify HMAC integrity */
    uint8_t expected_hmac[SPI_CAP_HMAC_LEN];
    size_t  signed_len = offsetof(spi_capability_t, hmac);
    hmac_sha256(g_signing_key, SPI_CAP_KEY_LEN,
                (const uint8_t *)cap, signed_len,
                expected_hmac);
    if (memcmp(expected_hmac, cap->hmac, SPI_CAP_HMAC_LEN) != 0) {
        return false; /* Tampered token */
    }

    /* 2. Verify holder identity */
    if (cap->holder_id != claimed_holder_id) return false;

    /* 3. Check expiry */
    if (current_time_ms > cap->expires_at) return false;

    /* 4. Check device permission */
    if (!(cap->device_mask & (1u << device_id))) return false;

    /* 5. Check operation permission */
    if ((cap->op_mask & requested_op) != (uint16_t)requested_op) return false;

    /* 6. Check revocation list */
    for (size_t i = 0; i < g_revoked_count; i++) {
        if (g_revoked_nonces[i] == cap->nonce) return false;
    }

    return true;
}

int spi_cap_revoke(uint32_t nonce)
{
    if (g_revoked_count >= MAX_REVOKED_NONCES) return -1;
    g_revoked_nonces[g_revoked_count++] = nonce;
    return 0;
}
```

---

### 6.4 HMAC Transaction Integrity

```c
/* spi_integrity.h — Per-transaction HMAC signing */
#ifndef SPI_INTEGRITY_H
#define SPI_INTEGRITY_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define SPI_TX_TAG_LEN  8   /* Truncated HMAC tag (64-bit) */

/* Signed SPI command frame */
typedef struct __attribute__((packed)) {
    uint32_t seq_num;              /* Monotonic sequence number */
    uint8_t  device_id;
    uint8_t  op;
    uint16_t payload_len;
    uint8_t  payload[256];
    uint8_t  tag[SPI_TX_TAG_LEN]; /* Truncated HMAC-SHA256 */
} spi_signed_frame_t;

int  spi_integrity_init(const uint8_t *session_key, size_t key_len);
int  spi_frame_sign(spi_signed_frame_t *frame);
bool spi_frame_verify(const spi_signed_frame_t *frame);

#endif /* SPI_INTEGRITY_H */
```

```c
/* spi_integrity.c */
#include "spi_integrity.h"
#include <string.h>

static uint8_t  g_session_key[32];
static size_t   g_session_key_len;
static uint32_t g_tx_seq = 0;
static uint32_t g_rx_seq = 0;

/* Stub HMAC – replace with real implementation */
static void hmac_sha256_stub(
    const uint8_t *key, size_t kl,
    const uint8_t *data, size_t dl,
    uint8_t out[32])
{
    (void)key; (void)kl; (void)data; (void)dl;
    memset(out, 0xCD, 32);
}

int spi_integrity_init(const uint8_t *session_key, size_t key_len)
{
    if (key_len > sizeof(g_session_key)) return -1;
    memcpy(g_session_key, session_key, key_len);
    g_session_key_len = key_len;
    g_tx_seq = g_rx_seq = 0;
    return 0;
}

int spi_frame_sign(spi_signed_frame_t *frame)
{
    frame->seq_num = g_tx_seq++;

    uint8_t full_hmac[32];
    /* Sign everything except the tag field */
    size_t data_len = offsetof(spi_signed_frame_t, tag);
    hmac_sha256_stub(
        g_session_key, g_session_key_len,
        (const uint8_t *)frame, data_len,
        full_hmac);

    /* Truncate to SPI_TX_TAG_LEN bytes */
    memcpy(frame->tag, full_hmac, SPI_TX_TAG_LEN);
    return 0;
}

bool spi_frame_verify(const spi_signed_frame_t *frame)
{
    /* 1. Anti-replay: sequence number must advance */
    if (frame->seq_num != g_rx_seq) return false;

    /* 2. Recompute and compare tag */
    uint8_t full_hmac[32];
    uint8_t expected_tag[SPI_TX_TAG_LEN];
    size_t  data_len = offsetof(spi_signed_frame_t, tag);

    hmac_sha256_stub(
        g_session_key, g_session_key_len,
        (const uint8_t *)frame, data_len,
        full_hmac);
    memcpy(expected_tag, full_hmac, SPI_TX_TAG_LEN);

    if (memcmp(expected_tag, frame->tag, SPI_TX_TAG_LEN) != 0) return false;

    g_rx_seq++;
    return true;
}
```

---

## Rust Implementation

Rust's ownership type system provides **compile-time** enforcement of many access control properties that C requires runtime checks for. A task that does not own a `SpiDeviceGuard` literally cannot compile code that calls `transfer()`.

### 7.1 Ownership-Enforced SPI Device Manager

```rust
// spi_access_control.rs

use std::collections::HashMap;
use std::sync::{Arc, Mutex, MutexGuard};

/// Operation permission flags
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct SpiOps(u8);

impl SpiOps {
    pub const NONE:  SpiOps = SpiOps(0x00);
    pub const READ:  SpiOps = SpiOps(0x01);
    pub const WRITE: SpiOps = SpiOps(0x02);
    pub const ADMIN: SpiOps = SpiOps(0x04);
    pub const ALL:   SpiOps = SpiOps(0x07);

    pub fn contains(self, other: SpiOps) -> bool {
        (self.0 & other.0) == other.0
    }
}

impl std::ops::BitOr for SpiOps {
    type Output = Self;
    fn bitor(self, rhs: Self) -> Self { SpiOps(self.0 | rhs.0) }
}

/// Error types for access control
#[derive(Debug, thiserror::Error)]
pub enum SpiAccessError {
    #[error("Access denied for task {task_id} on device {device_id}")]
    Denied { task_id: u32, device_id: u8 },
    #[error("Unknown device {0}")]
    UnknownDevice(u8),
    #[error("Bus busy or mutex poisoned")]
    BusBusy,
    #[error("Not the current bus owner")]
    NotOwner,
}

/// SPI device descriptor
#[derive(Debug, Clone)]
pub struct SpiDeviceDesc {
    pub device_id:            u8,
    pub cs_gpio_pin:          u8,
    pub max_clock_hz:         u32,
    pub is_security_critical: bool,
    pub name:                 &'static str,
}

/// RAII guard – holding this proves you have authorized, exclusive bus access.
/// When dropped, the bus is automatically released.
pub struct SpiDeviceGuard<'mgr> {
    manager:   &'mgr SpiManager,
    task_id:   u32,
    device_id: u8,
    allowed:   SpiOps,
    // Holds the mutex for the duration of the guard's lifetime
    _lock:     MutexGuard<'mgr, BusState>,
}

impl<'mgr> SpiDeviceGuard<'mgr> {
    /// Transfer data. Only callable if you hold the guard.
    pub fn transfer(&self, tx: &[u8], rx: &mut [u8]) -> Result<(), SpiAccessError> {
        // HAL call — in production, call into your BSP here
        println!(
            "[SPI HAL] task={} dev={} tx={:02X?}",
            self.task_id, self.device_id, tx
        );
        rx.iter_mut().enumerate().for_each(|(i, b)| *b = tx[i % tx.len()]);
        Ok(())
    }

    /// Write-only transfer — only if WRITE permission is held
    pub fn write(&self, tx: &[u8]) -> Result<(), SpiAccessError> {
        if !self.allowed.contains(SpiOps::WRITE) {
            return Err(SpiAccessError::Denied {
                task_id:   self.task_id,
                device_id: self.device_id,
            });
        }
        self.transfer(tx, &mut vec![0u8; tx.len()])
    }

    /// Read-only transfer — only if READ permission is held
    pub fn read(&self, rx: &mut [u8]) -> Result<(), SpiAccessError> {
        if !self.allowed.contains(SpiOps::READ) {
            return Err(SpiAccessError::Denied {
                task_id:   self.task_id,
                device_id: self.device_id,
            });
        }
        let tx = vec![0xFFu8; rx.len()];
        self.transfer(&tx, rx)
    }
}

impl<'mgr> Drop for SpiDeviceGuard<'mgr> {
    fn drop(&mut self) {
        println!(
            "[SPI ACL] RELEASE task={} device={}",
            self.task_id, self.device_id
        );
        // cs_deassert would happen here via HAL
        // _lock is released automatically
    }
}

/// Internal bus state, protected by a Mutex
struct BusState {
    current_owner:  Option<u32>,
    current_device: Option<u8>,
}

/// Access Control Entry
#[derive(Debug, Clone)]
struct AclEntry {
    task_id:     u32,
    device_id:   u8,
    allowed_ops: SpiOps,
}

/// The bus manager
pub struct SpiManager {
    devices: HashMap<u8, SpiDeviceDesc>,
    acl:     Mutex<Vec<AclEntry>>,
    bus:     Mutex<BusState>,
}

impl SpiManager {
    pub fn new(devices: Vec<SpiDeviceDesc>) -> Arc<Self> {
        Arc::new(SpiManager {
            devices: devices.into_iter().map(|d| (d.device_id, d)).collect(),
            acl:     Mutex::new(Vec::new()),
            bus:     Mutex::new(BusState {
                current_owner:  None,
                current_device: None,
            }),
        })
    }

    /// Add an access control entry (called during secure init phase)
    pub fn acl_add(&self, task_id: u32, device_id: u8, ops: SpiOps) {
        let mut acl = self.acl.lock().expect("ACL mutex poisoned");
        acl.push(AclEntry { task_id, device_id, allowed_ops: ops });
        println!("[SPI ACL] Registered task={} dev={} ops={:#04X}", task_id, device_id, ops.0);
    }

    /// Acquire exclusive, authorized access to a device.
    /// Returns an RAII guard; holding the guard IS the proof of authorization.
    pub fn acquire(
        &self,
        task_id:   u32,
        device_id: u8,
        op:        SpiOps,
    ) -> Result<SpiDeviceGuard<'_>, SpiAccessError> {
        // 1. Validate device
        if !self.devices.contains_key(&device_id) {
            return Err(SpiAccessError::UnknownDevice(device_id));
        }

        // 2. Check ACL
        let allowed = {
            let acl = self.acl.lock().map_err(|_| SpiAccessError::BusBusy)?;
            acl.iter()
               .find(|e| e.task_id == task_id && e.device_id == device_id)
               .map(|e| e.allowed_ops)
               .unwrap_or(SpiOps::NONE)
        };

        if !allowed.contains(op) {
            eprintln!("[SPI ACL] DENIED task={} device={} op={:#04X}", task_id, device_id, op.0);
            return Err(SpiAccessError::Denied { task_id, device_id });
        }

        // 3. Acquire the bus mutex — this blocks until the bus is free
        let mut bus = self.bus.lock().map_err(|_| SpiAccessError::BusBusy)?;
        bus.current_owner  = Some(task_id);
        bus.current_device = Some(device_id);

        println!("[SPI ACL] GRANTED task={} device={}", task_id, device_id);
        // cs_assert would happen here via HAL

        Ok(SpiDeviceGuard {
            manager:   self,
            task_id,
            device_id,
            allowed,
            _lock:     bus,
        })
    }
}

// ── Example usage ──────────────────────────────────────────────────────────────

fn main() {
    let devices = vec![
        SpiDeviceDesc {
            device_id: 0, cs_gpio_pin: 4, max_clock_hz: 1_000_000,
            is_security_critical: true, name: "ATECC608",
        },
        SpiDeviceDesc {
            device_id: 1, cs_gpio_pin: 5, max_clock_hz: 8_000_000,
            is_security_critical: false, name: "W25Q128",
        },
    ];

    let mgr = SpiManager::new(devices);

    const CRYPTO_TASK: u32 = 10;
    const APP_TASK:    u32 = 20;

    // Setup ACL (done once in privileged init context)
    mgr.acl_add(CRYPTO_TASK, 0, SpiOps::READ | SpiOps::WRITE | SpiOps::ADMIN);
    mgr.acl_add(APP_TASK,    0, SpiOps::READ);
    mgr.acl_add(CRYPTO_TASK, 1, SpiOps::READ | SpiOps::WRITE);

    // Crypto task: successful write to secure element
    {
        let guard = mgr.acquire(CRYPTO_TASK, 0, SpiOps::WRITE).unwrap();
        guard.write(&[0x01, 0xAB, 0xCD]).unwrap();
    } // guard dropped here → bus released automatically

    // App task: read from secure element (permitted)
    {
        let guard = mgr.acquire(APP_TASK, 0, SpiOps::READ).unwrap();
        let mut buf = [0u8; 4];
        guard.read(&mut buf).unwrap();
        println!("App read: {:02X?}", buf);
    }

    // App task: write attempt → DENIED at compile-accessible runtime check
    match mgr.acquire(APP_TASK, 0, SpiOps::WRITE) {
        Err(SpiAccessError::Denied { task_id, device_id }) =>
            println!("Correctly denied: task={} dev={}", task_id, device_id),
        _ => panic!("Should have been denied!"),
    }
}
```

---

### 7.2 RBAC with Type-State Pattern

```rust
// spi_rbac.rs — Type-state RBAC: permissions encoded in types at compile time

use std::marker::PhantomData;

/// Permission marker traits
pub trait CanRead  {}
pub trait CanWrite {}
pub trait CanAdmin {}

/// Permission type-states
pub struct ReadOnly;
pub struct ReadWrite;
pub struct Admin;

impl CanRead  for ReadOnly  {}
impl CanRead  for ReadWrite {}
impl CanWrite for ReadWrite {}
impl CanRead  for Admin     {}
impl CanWrite for Admin     {}
impl CanAdmin for Admin     {}

/// A typed SPI handle — the phantom type P encodes what operations are allowed.
/// Attempting to call write() on a ReadOnly handle is a COMPILE ERROR.
pub struct TypedSpiHandle<P> {
    device_id: u8,
    _perms:    PhantomData<P>,
}

impl<P> TypedSpiHandle<P> {
    fn new(device_id: u8) -> Self {
        TypedSpiHandle { device_id, _perms: PhantomData }
    }
}

impl<P: CanRead> TypedSpiHandle<P> {
    pub fn read(&self, rx: &mut [u8]) {
        println!("[SPI] Read {} bytes from device {}", rx.len(), self.device_id);
        rx.fill(0xBE);
    }
}

impl<P: CanWrite> TypedSpiHandle<P> {
    pub fn write(&self, tx: &[u8]) {
        println!("[SPI] Write {:02X?} to device {}", tx, self.device_id);
    }
}

impl<P: CanAdmin> TypedSpiHandle<P> {
    pub fn admin_command(&self, cmd: u8) {
        println!("[SPI] Admin cmd={:#04X} on device {}", cmd, self.device_id);
    }
}

/// The factory that issues typed handles — it is the only entity that can
/// construct TypedSpiHandle values, enforcing the RBAC at the type level.
pub struct SpiRbacFactory;

impl SpiRbacFactory {
    /// Issue a read-only handle. Compiler will enforce no write calls are made.
    pub fn open_read_only(&self, device_id: u8) -> TypedSpiHandle<ReadOnly> {
        TypedSpiHandle::new(device_id)
    }

    /// Issue a read-write handle.
    pub fn open_read_write(&self, device_id: u8) -> TypedSpiHandle<ReadWrite> {
        TypedSpiHandle::new(device_id)
    }

    /// Issue an admin handle — only for privileged callers.
    pub fn open_admin(&self, device_id: u8) -> TypedSpiHandle<Admin> {
        TypedSpiHandle::new(device_id)
    }
}

fn rbac_type_state_example() {
    let factory = SpiRbacFactory;

    // App task gets a read-only handle
    let ro_handle = factory.open_read_only(0);
    let mut buf = [0u8; 8];
    ro_handle.read(&mut buf);
    // ro_handle.write(&buf);        // ← COMPILE ERROR: CanWrite not implemented for ReadOnly

    // Crypto task gets read-write handle
    let rw_handle = factory.open_read_write(0);
    rw_handle.read(&mut buf);
    rw_handle.write(&[0xDE, 0xAD]);
    // rw_handle.admin_command(0x01); // ← COMPILE ERROR: CanAdmin not implemented for ReadWrite

    // Admin task gets full access
    let admin_handle = factory.open_admin(0);
    admin_handle.read(&mut buf);
    admin_handle.write(&[0x01]);
    admin_handle.admin_command(0xFF);
}
```

---

### 7.3 Capability Token System in Rust

```rust
// spi_capability.rs — HMAC-signed capability tokens

use std::time::{SystemTime, UNIX_EPOCH};

pub const HMAC_LEN: usize = 32;
pub const KEY_LEN:  usize = 32;

/// Capability token — serializable, HMAC-protected
#[derive(Debug, Clone)]
pub struct SpiCapability {
    pub issuer_id:   u16,
    pub holder_id:   u32,
    pub device_mask: u32,
    pub op_mask:     u16,
    pub expires_at:  u64,
    pub nonce:       u32,
    pub hmac:        [u8; HMAC_LEN],
}

#[derive(Debug, thiserror::Error)]
pub enum CapabilityError {
    #[error("HMAC verification failed — token tampered")]
    IntegrityError,
    #[error("Token expired")]
    Expired,
    #[error("Holder ID mismatch")]
    HolderMismatch,
    #[error("Device not permitted")]
    DeviceNotPermitted,
    #[error("Operation not permitted")]
    OperationNotPermitted,
    #[error("Token revoked")]
    Revoked,
}

/// Capability manager — issues and verifies tokens
pub struct CapabilityManager {
    signing_key:     [u8; KEY_LEN],
    nonce_counter:   u32,
    revoked_nonces:  Vec<u32>,
}

impl CapabilityManager {
    pub fn new(signing_key: [u8; KEY_LEN]) -> Self {
        CapabilityManager {
            signing_key,
            nonce_counter: 1,
            revoked_nonces: Vec::new(),
        }
    }

    fn compute_hmac(&self, cap: &SpiCapability) -> [u8; HMAC_LEN] {
        // In production: use the `hmac` + `sha2` crates
        // Example: hmac::Hmac::<sha2::Sha256>::new_from_slice(&self.signing_key)
        // Here we use a stub for illustration
        let mut tag = [0u8; HMAC_LEN];
        let fields: &[u8] = &[
            &cap.issuer_id.to_le_bytes()[..],
            &cap.holder_id.to_le_bytes()[..],
            &cap.device_mask.to_le_bytes()[..],
            &cap.op_mask.to_le_bytes()[..],
            &cap.expires_at.to_le_bytes()[..],
            &cap.nonce.to_le_bytes()[..],
        ].concat();
        // Stub: XOR with key bytes (NOT cryptographically secure — replace!)
        for (i, b) in fields.iter().enumerate() {
            tag[i % HMAC_LEN] ^= b ^ self.signing_key[i % KEY_LEN];
        }
        tag
    }

    pub fn issue(
        &mut self,
        issuer_id:   u16,
        holder_id:   u32,
        device_mask: u32,
        op_mask:     u16,
        expires_at:  u64,
    ) -> SpiCapability {
        let nonce = self.nonce_counter;
        self.nonce_counter += 1;

        let mut cap = SpiCapability {
            issuer_id,
            holder_id,
            device_mask,
            op_mask,
            expires_at,
            nonce,
            hmac: [0u8; HMAC_LEN],
        };
        cap.hmac = self.compute_hmac(&cap);
        cap
    }

    pub fn verify(
        &self,
        cap:               &SpiCapability,
        claimed_holder_id: u32,
        device_id:         u8,
        requested_op:      u16,
        current_time_ms:   u64,
    ) -> Result<(), CapabilityError> {
        // 1. Integrity
        let expected = self.compute_hmac(cap);
        if expected != cap.hmac {
            return Err(CapabilityError::IntegrityError);
        }

        // 2. Holder identity
        if cap.holder_id != claimed_holder_id {
            return Err(CapabilityError::HolderMismatch);
        }

        // 3. Expiry
        if current_time_ms > cap.expires_at {
            return Err(CapabilityError::Expired);
        }

        // 4. Device permission
        if (cap.device_mask & (1u32 << device_id)) == 0 {
            return Err(CapabilityError::DeviceNotPermitted);
        }

        // 5. Operation permission
        if (cap.op_mask & requested_op) != requested_op {
            return Err(CapabilityError::OperationNotPermitted);
        }

        // 6. Revocation check
        if self.revoked_nonces.contains(&cap.nonce) {
            return Err(CapabilityError::Revoked);
        }

        Ok(())
    }

    pub fn revoke(&mut self, nonce: u32) {
        if !self.revoked_nonces.contains(&nonce) {
            self.revoked_nonces.push(nonce);
        }
    }
}

fn capability_example() {
    let signing_key = [0x42u8; KEY_LEN];
    let mut mgr = CapabilityManager::new(signing_key);

    // Issue capability to crypto task: device 0, read+write, 1 hour
    let cap = mgr.issue(
        /* issuer */ 1,
        /* holder */ 10,
        /* devs   */ 0b0000_0001,   // device 0 only
        /* ops    */ 0x03,           // READ | WRITE
        /* expiry */ 3_600_000,      // 1 hour in ms
    );
    println!("Issued capability: nonce={}", cap.nonce);

    // Verify — should succeed
    assert!(mgr.verify(&cap, 10, 0, 0x01, 0).is_ok());

    // Wrong holder — should fail
    assert!(mgr.verify(&cap, 99, 0, 0x01, 0).is_err());

    // Revoke and retry
    mgr.revoke(cap.nonce);
    assert!(matches!(
        mgr.verify(&cap, 10, 0, 0x01, 0),
        Err(CapabilityError::Revoked)
    ));

    println!("Capability token tests passed.");
}
```

---

### 7.4 HMAC Transaction Integrity in Rust

```rust
// spi_integrity.rs — Per-frame signing with anti-replay

pub const TAG_LEN: usize = 8; // Truncated HMAC-SHA256 (64-bit)
pub const KEY_LEN: usize = 32;

#[derive(Debug, Clone)]
pub struct SignedFrame {
    pub seq_num:     u32,
    pub device_id:   u8,
    pub op:          u8,
    pub payload:     Vec<u8>,
    pub tag:         [u8; TAG_LEN],
}

#[derive(Debug, thiserror::Error)]
pub enum IntegrityError {
    #[error("Replay detected: expected seq {expected}, got {got}")]
    ReplayDetected { expected: u32, got: u32 },
    #[error("MAC verification failed")]
    MacFailure,
}

pub struct SpiIntegrityManager {
    session_key: [u8; KEY_LEN],
    tx_seq:      u32,
    rx_seq:      u32,
}

impl SpiIntegrityManager {
    pub fn new(session_key: [u8; KEY_LEN]) -> Self {
        SpiIntegrityManager { session_key, tx_seq: 0, rx_seq: 0 }
    }

    fn compute_tag(
        &self,
        seq_num:   u32,
        device_id: u8,
        op:        u8,
        payload:   &[u8],
    ) -> [u8; TAG_LEN] {
        // Production: use hmac::Hmac::<sha2::Sha256>
        let mut data = Vec::new();
        data.extend_from_slice(&seq_num.to_le_bytes());
        data.push(device_id);
        data.push(op);
        data.extend_from_slice(payload);

        let mut tag = [0u8; TAG_LEN];
        for (i, b) in data.iter().enumerate() {
            tag[i % TAG_LEN] ^= b ^ self.session_key[i % KEY_LEN];
        }
        tag
    }

    /// Sign a frame before transmission
    pub fn sign_frame(
        &mut self,
        device_id: u8,
        op:        u8,
        payload:   Vec<u8>,
    ) -> SignedFrame {
        let seq = self.tx_seq;
        self.tx_seq += 1;
        let tag = self.compute_tag(seq, device_id, op, &payload);
        SignedFrame { seq_num: seq, device_id, op, payload, tag }
    }

    /// Verify a received frame (anti-replay + MAC check)
    pub fn verify_frame(&mut self, frame: &SignedFrame) -> Result<(), IntegrityError> {
        // Anti-replay: must be the next expected sequence number
        if frame.seq_num != self.rx_seq {
            return Err(IntegrityError::ReplayDetected {
                expected: self.rx_seq,
                got:      frame.seq_num,
            });
        }

        // MAC verification
        let expected_tag = self.compute_tag(
            frame.seq_num,
            frame.device_id,
            frame.op,
            &frame.payload,
        );
        if expected_tag != frame.tag {
            return Err(IntegrityError::MacFailure);
        }

        self.rx_seq += 1;
        Ok(())
    }
}

fn integrity_example() {
    let key = [0xA5u8; KEY_LEN];
    let mut tx_side = SpiIntegrityManager::new(key);
    let mut rx_side = SpiIntegrityManager::new(key);

    // Sign and verify a normal frame
    let frame = tx_side.sign_frame(0, 0x01, vec![0xDE, 0xAD, 0xBE, 0xEF]);
    assert!(rx_side.verify_frame(&frame).is_ok());

    // Replay the same frame — should fail
    let replay = frame.clone();
    assert!(matches!(
        rx_side.verify_frame(&replay),
        Err(IntegrityError::ReplayDetected { .. })
    ));

    // Tampered payload — MAC fails
    let mut tampered = tx_side.sign_frame(0, 0x01, vec![0x00, 0x01]);
    tampered.payload[0] = 0xFF; // tamper after signing
    assert!(matches!(rx_side.verify_frame(&tampered), Err(IntegrityError::MacFailure)));

    println!("Integrity tests passed.");
}
```

---

## Secure SPI Bus Arbitration on RTOS

On a multi-threaded RTOS (FreeRTOS, Zephyr, RIOT), the SPI access controller should run in a privileged task that owns the SPI peripheral and CS GPIO lines, with all other tasks communicating via an IPC message queue.

```
┌──────────────────────────────────────────────────────────────┐
│                     RTOS Task Architecture                   │
│                                                              │
│  ┌─────────────┐   ┌─────────────┐   ┌──────────────────┐    │
│  │  CryptoTask │   │   AppTask   │   │  BootloaderTask  │    │
│  │  (prio: 5)  │   │  (prio: 3)  │   │    (prio: 7)     │    │
│  └──────┬──────┘   └──────┬──────┘   └────────┬─────────┘    │
│         │    SPI_Request   │                   │             │
│         └──────────────────┴───────────────────┘             │
│                            │                                 │
│                     ┌──────▼──────┐                          │
│                     │  SPI Access │ ← Privileged             │
│                     │  Supervisor │   MPU-protected          │
│                     │  (prio: 10) │   context                │
│                     └──────┬──────┘                          │
│                            │                                 │
│              ┌─────────────┼───────────┐                     │
│        ┌─────▼──┐   ┌─────▼──┐  ┌─────▼──┐                   │
│        │  SE050 │   │  TPM   │  │ CFlash │                   │
│        └────────┘   └────────┘  └────────┘                   │
└──────────────────────────────────────────────────────────────┘

IPC Message Format:
  { task_id, capability_token, device_id, op, payload[], payload_len }

Response Format:
  { result_code, rx_data[], rx_len, error_detail }
```

The supervisor performs all capability validation and never exposes the raw SPI controller registers to unprivileged tasks. The physical CS GPIO lines are configured as outputs only in the supervisor's MPU region.

---

## Audit Logging

Every access control decision should be written to a tamper-evident audit log. For embedded systems, this can be a circular buffer in protected SRAM, or written to a dedicated write-once flash sector.

```c
/* C audit log entry */
typedef struct __attribute__((packed)) {
    uint64_t    timestamp_ms;
    uint32_t    task_id;
    uint8_t     device_id;
    uint8_t     op;
    uint8_t     result;        /* SPI_ACCESS_OK / DENIED / etc. */
    uint8_t     reserved;
    uint8_t     hmac[8];       /* Integrity tag over entry fields */
} spi_audit_entry_t;
```

```rust
// Rust audit log entry
#[repr(C, packed)]
pub struct AuditEntry {
    pub timestamp_ms: u64,
    pub task_id:      u32,
    pub device_id:    u8,
    pub op:           u8,
    pub result:       u8,
    pub reserved:     u8,
    pub tag:          [u8; 8],
}
```

Log integrity properties to enforce:
- **Sequential write-once** entries (no overwriting without cycling the log)
- Each entry is HMAC-tagged with a log signing key
- Chain HMACs: `tag[n] = HMAC(entry[n] || tag[n-1])` for tamper detection across entries
- Log storage in MPU-protected SRAM or write-protected flash sector

---

## Common Pitfalls and Mitigations

| Pitfall | Risk | Mitigation |
|---|---|---|
| CS GPIO accessible from unprivileged context | Any task can assert CS and inject commands | Use MPU/MMU to restrict GPIO registers to supervisor only |
| SPI FIFO accessible without ACL check | Buffer-stuffing attack | Gate all FIFO writes behind the ACL/mutex layer |
| TOCTOU between ACL check and transfer | Race condition allows unauthorized transfer | Hold the mutex across the entire acquire–transfer–release sequence |
| No sequence numbers on SPI frames | Replay attacks on recorded command sequences | Use monotonic sequence numbers + HMAC per transaction |
| Fixed capability tokens (no expiry) | Compromised token grants permanent access | Always set short expiry; implement revocation list |
| ACL table writeable at runtime by any task | Privilege escalation | Write-protect ACL after boot (MPU write-lock or ROM-based ACL) |
| Missing audit trail | No forensic capability after incident | Log all access decisions to tamper-evident storage |
| CS left asserted on exception/crash | Leaves device in unknown state | Use RAII guards (Rust) or cleanup handlers (C) that deassert CS |
| Clock/data lines shared with debug probes | Physical eavesdropping | Disable SWD/JTAG in production; add bus encryption layer |
| Weak or absent HMAC key management | Keys extractable from firmware | Store session keys in secure element or one-time-programmed efuses |

---

## Summary

Access control for SPI devices is a layered security concern with no single silver bullet. The key layers are:

**Layer 1 — Mutual Exclusion:** A mutex-protected bus manager ensures only one task accesses the bus at a time, preventing interleaving and race conditions. This is the minimum baseline for any multi-task SPI system.

**Layer 2 — Authorization (RBAC / Capability Tokens):** Define which tasks may perform which operations on which devices. RBAC uses a role table checked at runtime; capability tokens encode permissions in a cryptographically signed, time-limited, revocable token. Rust's type system can encode permissions at compile time via type-states, preventing entire classes of authorization bugs before the code runs.

**Layer 3 — Hardware Isolation:** On systems with an MPU/MMU, the SPI peripheral registers and CS GPIO lines should be accessible only from a privileged supervisor task. Unprivileged tasks communicate via IPC message queues, making unauthorized direct bus access architecturally impossible.

**Layer 4 — Transaction Integrity:** For commands sent to cryptographic devices, sign each frame with a session HMAC and include a monotonic sequence number. This prevents command injection and replay attacks even against an adversary who can observe or modify the SPI bus.

**Layer 5 — Audit Logging:** Record all access control decisions in a tamper-evident, chained-HMAC log for forensic analysis.

In Rust, the ownership model provides structural safety guarantees: an `SpiDeviceGuard` can only exist if authorization was granted, the bus is automatically released when the guard is dropped, and type-state patterns encode read/write/admin permissions directly into the type system — turning authorization violations into compile-time errors rather than runtime incidents. In C/C++, the same guarantees must be carefully enforced through coding discipline, RAII patterns using destructors (C++), and strict layering between the HAL and the access control manager.

Combining all five layers provides defense-in-depth: an attacker who compromises one mechanism still faces multiple independent barriers before reaching a security-critical SPI peripheral.

---

*Document: 89 — Access Control for SPI Devices*  
*Languages: C/C++, Rust*  
*Category: Embedded Security / SPI Peripheral Protection*