# 61. Power Management and Sleep Modes in CAN Networks

> **Topic:** Implementing selective wake-up, partial networking, and low-power CAN transceiver modes for energy efficiency in embedded and automotive systems.

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Transceiver Power States](#can-transceiver-power-states)
3. [Partial Networking (ISO 11898-6)](#partial-networking-iso-11898-6)
4. [Selective Wake-Up Mechanisms](#selective-wake-up-mechanisms)
5. [C/C++ Implementation](#cc-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Hardware Abstraction Considerations](#hardware-abstraction-considerations)
8. [Common Pitfalls](#common-pitfalls)
9. [Summary](#summary)

---

## Introduction

Modern automotive and industrial CAN networks face a fundamental tension: ECUs must remain reachable at all times (to receive diagnostic commands, remote wakeups, or safety-critical messages), yet keeping every node fully powered is prohibitively expensive in terms of quiescent current — especially in battery-backed systems.

Power management on CAN networks encompasses three interrelated strategies:

- **Sleep Modes** — transitioning the CAN transceiver (and optionally the MCU) to a minimal-current state while the CAN bus is idle.
- **Selective Wake-Up (Partial Networking)** — waking only the subset of ECUs addressed by a specific CAN frame, leaving all others asleep.
- **Low-Power Transceiver Modes** — using hardware-defined states (Normal, Standby, Sleep, Listen-Only) exposed by transceivers such as the TJA1145, MCP2562, or SN65HVD23x.

Standards involved include ISO 11898-2 (physical layer), ISO 11898-6 (partial networking), and OEM-specific wake-up frame definitions (e.g., AUTOSAR NM, OSEK NM).

---

## CAN Transceiver Power States

Most modern CAN transceivers implement at least four operating modes, controlled via SPI or dedicated mode pins:

| Mode         | TXD | RXD | Bus Bias | Current (typical) | Description |
|--------------|-----|-----|----------|--------------------|-------------|
| Normal       | Active | Active | Yes | ~5–10 mA | Full duplex operation |
| Standby      | Ignored | Wake detect | Partial | ~50–200 µA | Bus monitored for dominant edge |
| Sleep        | Ignored | Off | No | ~5–20 µA | Lowest power; wake via INH or local |
| Listen-Only  | Ignored | Active | No | ~3–8 mA | Receive without driving the bus |

The **INH (Inhibit)** pin on transceivers like the TJA1145 can be used to cut power to the MCU supply rail when the transceiver enters sleep, enabling sub-microamp system current when used correctly.

### State Transition Diagram

```
         CAN Bus Idle              Wake Frame Received
  Normal ──────────────► Standby ──────────────────────► Normal
    ▲                      │                               ▲
    │                      │ Timeout / No Activity         │
    │                      ▼                               │
    │                    Sleep ─── Local Wake (INT pin) ───┘
    │
    └───────────────── Application Request
```

---

## Partial Networking (ISO 11898-6)

ISO 11898-6 standardizes a **CAN Partial Networking** (PN) protocol, where transceivers with PN capability (e.g., TJA1145, NCV7431) evaluate incoming CAN frames *in hardware* while the MCU is powered down. Only frames matching a pre-configured **wake-up filter** (by CAN ID and optionally payload data mask) trigger an MCU wake-up.

### Wake-Up Frame Configuration

A PN-capable transceiver is configured with:

- **Identifier filter**: 11-bit or 29-bit CAN ID to match.
- **Data mask + data**: Optional byte-level matching of the CAN payload.
- **Frame type**: Classic CAN or CAN FD.
- **Bus error detection**: Enables wake on CAN error frames as a fallback.

```
Wake-Up Frame Example (ISO 11898-6):
┌──────┬──────┬──────────────────────────────────┐
│ SOF  │ ID   │ DLC │ D0 │ D1 │ D2 │ ... │ CRC  │
└──────┴──────┴──────────────────────────────────┘
         ▲                 ▲
         │                 │
   Must match         Optionally matched
   ID filter          against data mask
```

### NM (Network Management) Wake-Up

In AUTOSAR-compliant networks, Network Management (NM) messages serve as keep-alive frames. Each ECU periodically transmits an NM PDU; absence of NM traffic triggers a coordinated shutdown sequence:

1. ECU stops sending NM messages → enters Network Mode: Prepare Bus Sleep.
2. After timeout (typically 1–5 s) → enters Bus Sleep Mode.
3. Transceiver is configured for PN wake-up and MCU enters deep sleep.
4. Reception of a matching NM frame → transceiver asserts INT → MCU wakes.

---

## Selective Wake-Up Mechanisms

### Edge-Triggered Wake (Classic Standby)

The simplest mechanism: the transceiver wakes on any dominant-to-recessive transition on the bus. This is non-selective — *any* CAN frame wakes *all* nodes in standby. Useful for low-node-count networks where full wake is acceptable.

### Frame-Level Selective Wake (PN)

Requires PN-capable transceivers. The MCU programs the wake filter via SPI before sleeping. The transceiver autonomously receives and filters frames; if a match is detected, it asserts the INT line to the MCU.

### Application-Level Selective Wake

Software-based: all ECUs wake on any frame, then the application software decides whether to remain active or immediately return to sleep based on message content. Simpler to implement but incurs a full MCU wake penalty per unwanted frame.

---

## C/C++ Implementation

### 1. TJA1145 Transceiver Driver (SPI-based PN Configuration)

The TJA1145 is a widely used PN-capable CAN transceiver configured over SPI.

```c
/* tja1145.h — TJA1145 register definitions */
#pragma once
#include <stdint.h>
#include <stdbool.h>

/* TJA1145 Register Map */
#define TJA1145_REG_MODE_CTRL        0x01
#define TJA1145_REG_MAIN_STATUS      0x03
#define TJA1145_REG_SYS_EVENT_EN     0x04
#define TJA1145_REG_MEM_0            0x06  /* General-purpose memory */
#define TJA1145_REG_WU_PIN_STATUS    0x64
#define TJA1145_REG_CAN_CTRL         0x20
#define TJA1145_REG_TRANSCEIVER_STAT 0x22
#define TJA1145_REG_TRANSCEIVER_EVT  0x23
#define TJA1145_REG_DATA_RATE        0x26
#define TJA1145_REG_CAN_ID_0         0x27  /* PN: CAN wake-up ID bytes */
#define TJA1145_REG_CAN_ID_1         0x28
#define TJA1145_REG_CAN_ID_2         0x29
#define TJA1145_REG_CAN_ID_3         0x2A
#define TJA1145_REG_CAN_MASK_0       0x2B  /* PN: ID mask */
#define TJA1145_REG_CAN_MASK_1       0x2C
#define TJA1145_REG_CAN_MASK_2       0x2D
#define TJA1145_REG_CAN_MASK_3       0x2E
#define TJA1145_REG_FRAME_CTRL       0x2F  /* PN: frame control */
#define TJA1145_REG_DATA_MASK_0      0x68  /* PN: data byte masks */
#define TJA1145_REG_DATA_MASK_1      0x69

/* Mode Control values */
#define TJA1145_MODE_SLEEP           0x01
#define TJA1145_MODE_STANDBY         0x04
#define TJA1145_MODE_NORMAL          0x07

/* CAN Control bits */
#define TJA1145_CAN_CTRL_PNCOK      (1 << 5)  /* PN config OK */
#define TJA1145_CAN_CTRL_CPNC       (1 << 4)  /* CAN PN config enable */
#define TJA1145_CAN_CTRL_CMC_CAN    0x01       /* CAN active mode */

typedef struct {
    void (*spi_write)(uint8_t reg, uint8_t val);
    uint8_t (*spi_read)(uint8_t reg);
    void (*delay_us)(uint32_t us);
} Tja1145Driver;

/**
 * @brief Write a register via SPI.
 *        SPI frame: [addr(7) | R/W(1)][data(8)]
 *        Write: addr_byte = (reg << 1) | 0x00
 */
static inline void tja1145_write(const Tja1145Driver *drv, uint8_t reg, uint8_t val) {
    drv->spi_write((reg << 1) & 0xFE, val);
}

static inline uint8_t tja1145_read(const Tja1145Driver *drv, uint8_t reg) {
    return drv->spi_read((reg << 1) | 0x01);
}

/**
 * @brief Configure partial networking wake-up filter.
 *
 * @param drv       Driver handle with SPI callbacks
 * @param wake_id   11-bit CAN ID that should trigger wake-up
 * @param id_mask   Bits set to 1 are "don't care" in the ID match
 * @param pn_data   8-byte data pattern to match (NULL = disable data matching)
 * @param data_mask 2-byte data mask (1 bit per data byte, 0=match required)
 */
void tja1145_configure_pn(const Tja1145Driver *drv,
                           uint16_t wake_id,
                           uint16_t id_mask,
                           const uint8_t *pn_data,
                           uint16_t data_mask)
{
    /* Switch to standby to allow register configuration */
    tja1145_write(drv, TJA1145_REG_MODE_CTRL, TJA1145_MODE_STANDBY);
    drv->delay_us(100);

    /* Program 11-bit CAN ID into ID registers [CAN_ID_0..3]
     * TJA1145 stores the ID left-aligned in a 29-bit field.
     * For 11-bit IDs: ID[10:3] → CAN_ID_0[7:0], ID[2:0] → CAN_ID_1[7:5] */
    tja1145_write(drv, TJA1145_REG_CAN_ID_0, (uint8_t)((wake_id >> 3) & 0xFF));
    tja1145_write(drv, TJA1145_REG_CAN_ID_1, (uint8_t)((wake_id & 0x07) << 5));
    tja1145_write(drv, TJA1145_REG_CAN_ID_2, 0x00);
    tja1145_write(drv, TJA1145_REG_CAN_ID_3, 0x00);

    /* Program ID mask (same bit layout) */
    tja1145_write(drv, TJA1145_REG_CAN_MASK_0, (uint8_t)((id_mask >> 3) & 0xFF));
    tja1145_write(drv, TJA1145_REG_CAN_MASK_1, (uint8_t)((id_mask & 0x07) << 5));
    tja1145_write(drv, TJA1145_REG_CAN_MASK_2, 0x00);
    tja1145_write(drv, TJA1145_REG_CAN_MASK_3, 0x00);

    /* Frame control: 11-bit ID, DLC=8, data match enabled if pn_data != NULL */
    uint8_t frame_ctrl = 0x08; /* DLC = 8 */
    if (pn_data != NULL) {
        frame_ctrl |= (1 << 4); /* Enable data match (PNDM bit) */
    }
    tja1145_write(drv, TJA1145_REG_FRAME_CTRL, frame_ctrl);

    /* Data mask registers (2 bytes cover 8 data bytes, 1 bit each) */
    if (pn_data != NULL) {
        tja1145_write(drv, TJA1145_REG_DATA_MASK_0, (uint8_t)(data_mask & 0xFF));
        tja1145_write(drv, TJA1145_REG_DATA_MASK_1, (uint8_t)(data_mask >> 8));
    }

    /* Enable CAN PN configuration */
    uint8_t can_ctrl = TJA1145_CAN_CTRL_CPNC | TJA1145_CAN_CTRL_CMC_CAN;
    tja1145_write(drv, TJA1145_REG_CAN_CTRL, can_ctrl);

    /* Verify PN configuration was accepted (PNCOK bit) */
    uint8_t status = tja1145_read(drv, TJA1145_REG_CAN_CTRL);
    if (!(status & TJA1145_CAN_CTRL_PNCOK)) {
        /* Configuration rejected — check register values and SPI integrity */
        /* In production: set an error flag or invoke error handler */
    }
}
```

### 2. MCU Sleep Entry and Wake-Up Handler (ARM Cortex-M, STM32 example)

```c
/* power_management.c */
#include <stdint.h>
#include "stm32g0xx_hal.h"  /* Adjust to your MCU HAL */
#include "tja1145.h"

/* ------------------------------------------------------------------ */
/* Application-level NM timeout tracking                              */
/* ------------------------------------------------------------------ */

static volatile uint32_t nm_last_rx_tick = 0;  /* Tick of last NM frame received */
static const uint32_t NM_TIMEOUT_MS     = 3000; /* 3 s bus idle → prepare sleep */
static const uint32_t BUS_SLEEP_DELAY_MS = 1000; /* 1 s prepare-sleep → sleep */

typedef enum {
    PM_STATE_NORMAL,
    PM_STATE_PREPARE_SLEEP,
    PM_STATE_SLEEP,
} PmState;

static PmState g_pm_state = PM_STATE_NORMAL;

/* Called from CAN RX ISR when an NM frame is received */
void pm_on_nm_frame_received(void) {
    nm_last_rx_tick = HAL_GetTick();
    if (g_pm_state != PM_STATE_NORMAL) {
        g_pm_state = PM_STATE_NORMAL;
        /* Restart NM timers, re-enable CAN activity */
    }
}

/* ------------------------------------------------------------------ */
/* Sleep entry sequence                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief  Transition the system to low-power sleep with PN wake-up enabled.
 *
 *  Sequence:
 *   1. Flush outgoing CAN messages.
 *   2. Configure TJA1145 PN wake filter.
 *   3. Set transceiver to Sleep mode (INH cuts MCU VCC on some HW layouts).
 *   4. Enter MCU Stop/Standby mode — woken by TJA1145 INT pin (EXTI).
 */
void pm_enter_sleep(const Tja1145Driver *drv) {
    /* 1. Disable CAN peripheral (flush + stop) */
    HAL_CAN_Stop(&hcan1);
    HAL_CAN_DeInit(&hcan1);

    /* 2. Configure PN filter:
     *    Wake on NM frame ID = 0x400, exact match (mask = 0x000)
     *    No data filtering */
    tja1145_configure_pn(drv,
                         /*wake_id=*/   0x400,
                         /*id_mask=*/   0x000,
                         /*pn_data=*/   NULL,
                         /*data_mask=*/ 0x0000);

    /* 3. Enable system event: wake-up on CAN bus activity */
    tja1145_write(drv, TJA1145_REG_SYS_EVENT_EN, 0x01); /* CBSE: CAN bus-silence event */

    /* 4. Set transceiver to Sleep mode */
    tja1145_write(drv, TJA1145_REG_MODE_CTRL, TJA1145_MODE_SLEEP);

    /* 5. Configure EXTI on TJA1145 INT pin (e.g., PA0) for rising edge */
    /* (Assumed pre-configured in GPIO/EXTI init) */
    __HAL_GPIO_EXTI_CLEAR_IT(GPIO_PIN_0);

    /* 6. Enter MCU Stop2 mode — lowest power while retaining SRAM */
    HAL_PWREx_EnterSTOP2Mode(PWR_STOPENTRY_WFI);

    /*
     * ---- MCU IS NOW STOPPED ----
     * Execution resumes here after the INT EXTI fires.
     */

    /* 7. Wake-up: restore clocks (Stop2 disables PLL) */
    SystemClock_Config();

    /* 8. Re-initialize CAN peripheral */
    MX_CAN1_Init();
    HAL_CAN_Start(&hcan1);

    /* 9. Read TJA1145 wake-up source */
    uint8_t wake_status = tja1145_read(drv, TJA1145_REG_MAIN_STATUS);
    (void)wake_status; /* Log or act on wake source if needed */

    /* 10. Clear transceiver event flags */
    tja1145_write(drv, TJA1145_REG_TRANSCEIVER_EVT, 0xFF);

    g_pm_state = PM_STATE_NORMAL;
}

/* ------------------------------------------------------------------ */
/* Power management task (call from main loop or RTOS task)           */
/* ------------------------------------------------------------------ */

void pm_task(const Tja1145Driver *drv) {
    uint32_t now = HAL_GetTick();

    switch (g_pm_state) {
        case PM_STATE_NORMAL:
            if ((now - nm_last_rx_tick) > NM_TIMEOUT_MS) {
                g_pm_state = PM_STATE_PREPARE_SLEEP;
                /* Broadcast NM "Prepare Bus Sleep" indication if applicable */
            }
            break;

        case PM_STATE_PREPARE_SLEEP:
            if ((now - nm_last_rx_tick) > (NM_TIMEOUT_MS + BUS_SLEEP_DELAY_MS)) {
                pm_enter_sleep(drv);
                /* Returns here after wake-up */
            }
            break;

        case PM_STATE_SLEEP:
            /* Should not be reached; pm_enter_sleep() blocks until wake */
            break;
    }
}
```

### 3. Linux SocketCAN: Bus-Off Recovery and Idle Detection

On Linux systems (e.g., automotive gateway ECUs), power management can be coordinated through SocketCAN's netlink interface and kernel wakeup sources.

```c
/* linux_can_pm.c — SocketCAN power management coordination */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <net/if.h>
#include <time.h>

#define CAN_IFACE       "can0"
#define IDLE_TIMEOUT_S  3      /* Consider bus idle after 3 s with no frames */

typedef struct {
    int sock;
    struct timespec last_frame_time;
    int bus_is_idle;
} CanPmContext;

/**
 * @brief Open a CAN socket with error frame reception enabled.
 *        Error frames are used to detect bus-off and passive error states.
 */
int can_pm_open(CanPmContext *ctx) {
    struct sockaddr_can addr;
    struct ifreq ifr;
    can_err_mask_t err_mask;

    ctx->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (ctx->sock < 0) return -1;

    /* Bind to interface */
    strncpy(ifr.ifr_name, CAN_IFACE, IFNAMSIZ - 1);
    ioctl(ctx->sock, SIOCGIFINDEX, &ifr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (bind(ctx->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) return -1;

    /* Subscribe to bus-off and error-passive error frames */
    err_mask = CAN_ERR_BUSOFF | CAN_ERR_CRTL;
    setsockopt(ctx->sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    /* Non-blocking reads for polling */
    fcntl(ctx->sock, F_SETFL, O_NONBLOCK);

    clock_gettime(CLOCK_MONOTONIC, &ctx->last_frame_time);
    ctx->bus_is_idle = 0;
    return 0;
}

/**
 * @brief Check for received frames and detect bus idle condition.
 * @return 1 if bus is considered idle (no frames for IDLE_TIMEOUT_S seconds)
 */
int can_pm_poll(CanPmContext *ctx) {
    struct can_frame frame;
    ssize_t nbytes;
    struct timespec now;

    /* Drain all available frames */
    while ((nbytes = read(ctx->sock, &frame, sizeof(frame))) > 0) {
        clock_gettime(CLOCK_MONOTONIC, &ctx->last_frame_time);
        ctx->bus_is_idle = 0;

        if (frame.can_id & CAN_ERR_FLAG) {
            /* Bus-off: kernel has automatically restarted the controller
             * (if auto-restart is configured via ip link set can0 restart-ms 100) */
            if (frame.can_id & CAN_ERR_BUSOFF) {
                fprintf(stderr, "[CAN PM] Bus-off detected on %s\n", CAN_IFACE);
            }
        }
    }

    if (nbytes < 0 && errno != EAGAIN) {
        perror("can read");
        return -1;
    }

    /* Check idle timeout */
    clock_gettime(CLOCK_MONOTONIC, &now);
    long elapsed_s = now.tv_sec - ctx->last_frame_time.tv_sec;
    if (elapsed_s >= IDLE_TIMEOUT_S && !ctx->bus_is_idle) {
        ctx->bus_is_idle = 1;
        printf("[CAN PM] Bus idle detected — initiating sleep sequence\n");
        return 1;
    }
    return 0;
}

/**
 * @brief Bring down CAN interface before system suspend.
 *        Call before writing to /sys/power/state.
 */
void can_pm_suspend(CanPmContext *ctx) {
    char cmd[64];
    close(ctx->sock);
    ctx->sock = -1;
    snprintf(cmd, sizeof(cmd), "ip link set %s down", CAN_IFACE);
    system(cmd); /* In production: use netlink directly */
    printf("[CAN PM] CAN interface down, system may suspend\n");
}

/**
 * @brief Resume CAN interface after system wake-up.
 */
int can_pm_resume(CanPmContext *ctx) {
    char cmd[128];
    snprintf(cmd, sizeof(cmd),
             "ip link set %s up type can bitrate 500000 restart-ms 100", CAN_IFACE);
    system(cmd);
    return can_pm_open(ctx);
}

int main(void) {
    CanPmContext ctx;
    if (can_pm_open(&ctx) < 0) {
        perror("can_pm_open");
        return 1;
    }

    while (1) {
        int idle = can_pm_poll(&ctx);
        if (idle == 1) {
            can_pm_suspend(&ctx);
            /*
             * Signal system power manager to suspend, e.g.:
             *   echo mem > /sys/power/state
             * The wake-up source (TJA1145 INT → GPIO wakeup) resumes the kernel.
             */
            sleep(1); /* Placeholder — real code would block on suspend */
            can_pm_resume(&ctx);
        }
        usleep(50000); /* 50 ms poll interval */
    }
}
```

---

## Rust Implementation

### 1. TJA1145 SPI Driver in Rust (using `embedded-hal`)

```rust
// tja1145.rs — Rust driver for TJA1145 CAN transceiver
//
// Dependencies (Cargo.toml):
//   embedded-hal = "1.0"
//   nb = "1.0"

use embedded_hal::spi::SpiDevice;

/// TJA1145 register addresses
#[allow(dead_code)]
pub mod regs {
    pub const MODE_CTRL:        u8 = 0x01;
    pub const MAIN_STATUS:      u8 = 0x03;
    pub const SYS_EVENT_EN:     u8 = 0x04;
    pub const CAN_CTRL:         u8 = 0x20;
    pub const TRANSCEIVER_STAT: u8 = 0x22;
    pub const TRANSCEIVER_EVT:  u8 = 0x23;
    pub const DATA_RATE:        u8 = 0x26;
    pub const CAN_ID_0:         u8 = 0x27;
    pub const CAN_ID_1:         u8 = 0x28;
    pub const CAN_ID_2:         u8 = 0x29;
    pub const CAN_ID_3:         u8 = 0x2A;
    pub const CAN_MASK_0:       u8 = 0x2B;
    pub const CAN_MASK_1:       u8 = 0x2C;
    pub const CAN_MASK_2:       u8 = 0x2D;
    pub const CAN_MASK_3:       u8 = 0x2E;
    pub const FRAME_CTRL:       u8 = 0x2F;
    pub const DATA_MASK_0:      u8 = 0x68;
    pub const DATA_MASK_1:      u8 = 0x69;
}

/// Transceiver operating modes
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum TransceiverMode {
    Sleep   = 0x01,
    Standby = 0x04,
    Normal  = 0x07,
}

/// Errors that can occur during transceiver operations
#[derive(Debug)]
pub enum Tja1145Error<SpiError> {
    Spi(SpiError),
    PnConfigRejected,
    InvalidCanId,
}

/// Partial networking wake-up filter configuration
#[derive(Debug, Clone)]
pub struct PnFilter {
    /// 11-bit CAN ID to match for wake-up
    pub wake_id: u16,
    /// Bitmask for wake_id: 1 = don't care, 0 = must match
    pub id_mask: u16,
    /// Optional 8-byte data pattern + 2-byte per-byte mask
    pub data_filter: Option<([u8; 8], u16)>,
}

impl PnFilter {
    /// Create a filter that wakes on a specific 11-bit ID, no data matching.
    pub fn id_only(wake_id: u16) -> Self {
        assert!(wake_id <= 0x7FF, "CAN 11-bit ID must be ≤ 0x7FF");
        Self { wake_id, id_mask: 0x000, data_filter: None }
    }

    /// Create a filter that wakes on a range of IDs (masked match).
    pub fn id_masked(wake_id: u16, id_mask: u16) -> Self {
        Self { wake_id, id_mask, data_filter: None }
    }
}

/// TJA1145 driver wrapper
pub struct Tja1145<SPI> {
    spi: SPI,
}

impl<SPI: SpiDevice> Tja1145<SPI> {
    pub fn new(spi: SPI) -> Self {
        Self { spi }
    }

    /// SPI write: [reg_addr << 1 | 0][data]
    fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), Tja1145Error<SPI::Error>> {
        let buf = [(reg << 1) & 0xFE, val];
        self.spi.write(&buf).map_err(Tja1145Error::Spi)
    }

    /// SPI read: [reg_addr << 1 | 1][dummy] → read second byte
    fn read_reg(&mut self, reg: u8) -> Result<u8, Tja1145Error<SPI::Error>> {
        let mut buf = [(reg << 1) | 0x01, 0x00];
        self.spi.transfer_in_place(&mut buf).map_err(Tja1145Error::Spi)?;
        Ok(buf[1])
    }

    /// Set the transceiver operating mode.
    pub fn set_mode(&mut self, mode: TransceiverMode) -> Result<(), Tja1145Error<SPI::Error>> {
        self.write_reg(regs::MODE_CTRL, mode as u8)
    }

    /// Configure partial networking wake-up filter.
    ///
    /// The transceiver must be in Standby mode before calling this.
    pub fn configure_pn(&mut self, filter: &PnFilter) -> Result<(), Tja1145Error<SPI::Error>> {
        if filter.wake_id > 0x7FF {
            return Err(Tja1145Error::InvalidCanId);
        }

        // Enter standby to allow PN register writes
        self.set_mode(TransceiverMode::Standby)?;

        // Program 11-bit ID (left-aligned in 29-bit field)
        // ID[10:3] → CAN_ID_0, ID[2:0] → CAN_ID_1[7:5]
        self.write_reg(regs::CAN_ID_0, ((filter.wake_id >> 3) & 0xFF) as u8)?;
        self.write_reg(regs::CAN_ID_1, ((filter.wake_id & 0x07) << 5) as u8)?;
        self.write_reg(regs::CAN_ID_2, 0x00)?;
        self.write_reg(regs::CAN_ID_3, 0x00)?;

        // Program ID mask
        self.write_reg(regs::CAN_MASK_0, ((filter.id_mask >> 3) & 0xFF) as u8)?;
        self.write_reg(regs::CAN_MASK_1, ((filter.id_mask & 0x07) << 5) as u8)?;
        self.write_reg(regs::CAN_MASK_2, 0x00)?;
        self.write_reg(regs::CAN_MASK_3, 0x00)?;

        // Frame control: DLC=8, enable data match if filter has data
        let pndm_bit: u8 = if filter.data_filter.is_some() { 0x10 } else { 0x00 };
        self.write_reg(regs::FRAME_CTRL, 0x08 | pndm_bit)?;

        // Data mask registers
        if let Some((_, data_mask)) = filter.data_filter {
            self.write_reg(regs::DATA_MASK_0, (data_mask & 0xFF) as u8)?;
            self.write_reg(regs::DATA_MASK_1, (data_mask >> 8) as u8)?;
        }

        // Enable CAN PN: CPNC=1, CMC=01 (CAN active)
        self.write_reg(regs::CAN_CTRL, 0x31)?;  // CPNC | CMC_CAN

        // Verify PNCOK bit
        let can_ctrl = self.read_reg(regs::CAN_CTRL)?;
        if (can_ctrl & 0x20) == 0 {  // PNCOK bit
            return Err(Tja1145Error::PnConfigRejected);
        }

        Ok(())
    }

    /// Clear all transceiver event flags (call after wake-up).
    pub fn clear_events(&mut self) -> Result<(), Tja1145Error<SPI::Error>> {
        self.write_reg(regs::TRANSCEIVER_EVT, 0xFF)
    }

    /// Read the main status register.
    pub fn main_status(&mut self) -> Result<u8, Tja1145Error<SPI::Error>> {
        self.read_reg(regs::MAIN_STATUS)
    }

    /// Enter sleep mode with PN wake-up enabled.
    /// Returns the driver back for use after MCU wake-up.
    pub fn enter_sleep(mut self, filter: &PnFilter) -> Result<Self, Tja1145Error<SPI::Error>> {
        self.configure_pn(filter)?;
        // Enable CAN bus-silence event to allow sleep entry
        self.write_reg(regs::SYS_EVENT_EN, 0x01)?;
        self.set_mode(TransceiverMode::Sleep)?;
        // Caller is responsible for entering MCU sleep (platform-specific)
        Ok(self)
    }
}
```

### 2. Power Management State Machine in Rust

```rust
// power_manager.rs — CAN network management power state machine

use core::sync::atomic::{AtomicU32, Ordering};

/// Atomic tick counter — updated by SysTick ISR or RTOS timer.
static TICK_MS: AtomicU32 = AtomicU32::new(0);
/// Tick of last received NM frame.
static NM_LAST_RX_TICK: AtomicU32 = AtomicU32::new(0);

pub fn tick_increment() {
    TICK_MS.fetch_add(1, Ordering::Relaxed);
}

pub fn on_nm_frame_received() {
    let now = TICK_MS.load(Ordering::Relaxed);
    NM_LAST_RX_TICK.store(now, Ordering::Relaxed);
}

/// NM timeout configuration
const NM_TIMEOUT_MS:      u32 = 3_000;
const BUS_SLEEP_DELAY_MS: u32 = 1_000;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PmState {
    Normal,
    PrepareSleep,
    Sleeping,
}

pub struct PowerManager {
    state: PmState,
}

impl PowerManager {
    pub const fn new() -> Self {
        Self { state: PmState::Normal }
    }

    pub fn state(&self) -> PmState {
        self.state
    }

    /// Poll the state machine. Returns `true` when sleep should be entered.
    pub fn poll(&mut self) -> bool {
        let now      = TICK_MS.load(Ordering::Relaxed);
        let last_nm  = NM_LAST_RX_TICK.load(Ordering::Relaxed);
        let elapsed  = now.wrapping_sub(last_nm);

        match self.state {
            PmState::Normal => {
                if elapsed > NM_TIMEOUT_MS {
                    defmt::info!("[PM] NM timeout — entering PrepSleep");
                    self.state = PmState::PrepareSleep;
                }
                false
            }
            PmState::PrepareSleep => {
                if elapsed <= NM_TIMEOUT_MS {
                    // NM traffic resumed — back to normal
                    self.state = PmState::Normal;
                    return false;
                }
                if elapsed > NM_TIMEOUT_MS + BUS_SLEEP_DELAY_MS {
                    self.state = PmState::Sleeping;
                    return true;  // Caller should invoke sleep sequence
                }
                false
            }
            PmState::Sleeping => false,
        }
    }

    /// Called after wake-up interrupt fires.
    pub fn on_wakeup(&mut self) {
        defmt::info!("[PM] Wake-up received");
        self.state = PmState::Normal;
        NM_LAST_RX_TICK.store(TICK_MS.load(Ordering::Relaxed), Ordering::Relaxed);
    }
}
```

### 3. Main Application Loop (RTOS-style, no_std)

```rust
// main.rs — Embassy/bare-metal integration sketch
//
// Assumes:
//   - Embassy executor on Cortex-M
//   - SPI peripheral via embassy-stm32
//   - GPIO INT pin as wakeup EXTI source

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::spi::{Config as SpiConfig, Spi};
use embassy_stm32::exti::ExtiInput;
use embassy_stm32::gpio::{Input, Pull};
use embassy_time::{Duration, Timer};

mod tja1145;
mod power_manager;

use tja1145::{PnFilter, Tja1145};
use power_manager::PowerManager;

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // SPI for TJA1145 (SPI1, 1 MHz, CPOL=0, CPHA=1 per TJA1145 spec)
    let mut spi_config = SpiConfig::default();
    spi_config.frequency = embassy_stm32::time::Hertz(1_000_000);
    let spi = Spi::new(p.SPI1, p.PA5, p.PA7, p.PA6, p.DMA1_CH3, p.DMA1_CH2, spi_config);

    let mut transceiver = Tja1145::new(spi);

    // TJA1145 INT pin — asserted on CAN wake-up frame detection
    let int_pin = ExtiInput::new(Input::new(p.PA0, Pull::None), p.EXTI0);

    // PN filter: wake on NM frame ID 0x400, no data matching
    let pn_filter = PnFilter::id_only(0x400);

    let mut pm = PowerManager::new();

    loop {
        // Simulate CAN NM frame polling (replace with real CAN RX task)
        Timer::after(Duration::from_millis(50)).await;
        power_manager::tick_increment(); // In real use: driven by SysTick

        if pm.poll() {
            // --- Enter sleep sequence ---
            defmt::info!("[Main] Entering CAN sleep with PN wake filter 0x{:03X}", 0x400u16);

            // Configure transceiver for PN wake and enter sleep
            // (SPI ownership transferred into enter_sleep, returned after wake)
            transceiver = transceiver
                .enter_sleep(&pn_filter)
                .expect("PN config failed");

            // Enter MCU stop mode — wake on EXTI (TJA1145 INT)
            // embassy-stm32: cortex_m::asm::wfi() or embassy PWR stop mode
            int_pin.wait_for_rising_edge().await;

            // --- Resumed after wake-up ---
            transceiver.clear_events().expect("Clear events failed");
            pm.on_wakeup();
            defmt::info!("[Main] Woke up from CAN sleep");
        }
    }
}
```

---

## Hardware Abstraction Considerations

When designing a portable CAN power management layer, several concerns must be addressed across MCU and transceiver families:

**SPI Protocol Variants** — The TJA1145 uses a 16-bit SPI frame (8-bit address + 8-bit data). Other transceivers such as the MCP2561/2 use a simple EN/STB pin pair instead of SPI. Abstract the control interface behind a trait (Rust) or function pointer struct (C).

**INH Pin Usage** — Some TJA1145 circuit designs route the INH pin to the MCU's main voltage regulator enable input. When the transceiver asserts INH low, the MCU loses power entirely. In these designs, wake-up state must be preserved in the transceiver's general-purpose memory registers (TJA1145_REG_MEM_0..3) or in a battery-backed SRAM region.

**Clock Recovery After Stop Mode** — ARM Cortex-M Stop modes disable the PLL and may require clock reconfiguration on wake. Always restore system clocks before reinitializing the CAN peripheral.

**CAN FD and PN** — ISO 11898-6 PN was originally defined for Classic CAN. CAN FD transceiver variants (e.g., TJA1145x with FD extension) support PN on CAN FD frames; the frame control register must reflect the correct FD format bit.

**Bus Error Handling During Wake-Up** — The transceiver may encounter bus errors (glitches, partial frames) while filtering frames in sleep. Configuring the `CAN_ERR_BUS` bit in the transceiver event enables causes a forced wake on bus error, which is a safety-conservative choice for diagnostic networks.

---

## Common Pitfalls

1. **Not flushing the TX queue before sleep** — Frames stuck in the controller's TX buffer may corrupt the bus on wake-up if the controller resumes mid-transmission context.

2. **PN filter bit-ordering confusion** — The TJA1145 stores CAN IDs left-aligned in a 29-bit field regardless of whether 11-bit or 29-bit frames are used. Incorrect shift values are the most common source of "node never wakes" bugs.

3. **Forgetting to clear event flags on wake** — Uncleaned event flags can cause an immediate re-sleep or prevent the transceiver from returning to Normal mode.

4. **Race condition on NM timeout** — If the NM timeout fires just as the last NM frame is being received, the MCU may enter sleep while a wake-up frame is on the bus. Use atomic tick operations and ensure ISR priority is configured to update `nm_last_rx_tick` before the PM task runs.

5. **Ignoring PNCOK** — The TJA1145 rejects PN configurations that contain invalid combinations (e.g., DLC=0 with data matching enabled). Always check PNCOK after programming; proceed to sleep only if the bit is set.

6. **SPI CS timing violations** — The TJA1145 requires CS to be held low for the full 16-bit frame. Some SPI HAL drivers deassert CS between bytes; verify the SPI transfer is truly atomic.

---

## Summary

| Aspect | Key Takeaway |
|---|---|
| **Transceiver Modes** | Use Sleep (µA) for deep idle, Standby (µA–mA) for bus monitoring, Normal for active communication |
| **Partial Networking** | ISO 11898-6 — configure wake ID filter in hardware (TJA1145 via SPI) so MCU stays off until addressed |
| **NM Integration** | Coordinate sleep/wake with AUTOSAR/OSEK Network Management timeouts for orderly bus shutdown |
| **C/C++ Driver** | SPI register abstraction via function-pointer struct; MCU sleep/wake orchestrated around HAL stop modes |
| **Rust Driver** | `embedded-hal` SPI trait + typestate ownership ensures transceiver config is valid before sleep entry |
| **Linux/SocketCAN** | Use `ip link set can0 down` before suspend; kernel wakeup source tied to GPIO connected to TJA1145 INT |
| **Critical Checks** | Always verify PNCOK, flush TX queue, restore clocks, clear event flags on every wake cycle |

> **Energy impact:** A well-configured PN system with TJA1145 can reduce an idle ECU's quiescent current from ~15 mA (CAN active) to **under 20 µA**, a reduction of over 750×, while remaining fully responsive to addressed CAN frames within ~100 µs of bus activity.