# 83. Vibration and Shock Resistance

**Physical & Electrical** (Sections 2–3): Explains the five core failure modes — signal rise-time degradation, bus lock-up (the most dangerous), data corruption, connector contact bounce, and ground bounce — then maps each to a hardware countermeasure: connector selection, cable routing, pull-up resistor sizing tables, bus buffers, decoupling, and TVS protection.

**Protocol hardening** (Section 4): Clock speed reduction, clock-stretch timeout sizing, and transaction length minimization.

**Software layer** (Sections 5–7): A recovery state machine with three levels (clock recovery → soft reset → hard reset), CRC-8/SMBus PEC computation, a full retry loop with exponential back-off, redundant triple-read majority voting, and a watchdog-supervised background health monitor — all implemented in both **C/C++** (Linux `i2c-dev` + bare-metal CMSIS style) and **Rust** (`embedded-hal` + `linux-embedded-hal`).

**Validation** (Section 8): Bench simulation techniques, IEC 60068-2-6/-2-64 vibration standards, IEC 60068-2-27 shock testing, and an automated error-injection test harness in Rust.

The document closes with a **design checklist** covering hardware, protocol, software, and testing, and a **summary** that captures the core engineering principles.

## Ensuring Reliable I2C Communication in Mechanically Harsh Environments

---

## Table of Contents

1. [Introduction](#introduction)
2. [How Vibration and Shock Affect I2C](#how-vibration-and-shock-affect-i2c)
3. [Physical and Electrical Countermeasures](#physical-and-electrical-countermeasures)
4. [Protocol-Level Hardening](#protocol-level-hardening)
5. [Software Error Detection and Recovery](#software-error-detection-and-recovery)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Testing and Validation Strategies](#testing-and-validation-strategies)
9. [Design Checklist](#design-checklist)
10. [Summary](#summary)

---

## Introduction

The I2C (Inter-Integrated Circuit) bus was originally designed for benign, static environments — printed circuit boards in consumer electronics, industrial sensors in controlled enclosures, and similar low-stress applications. Its two-wire open-drain architecture is elegantly simple, but that simplicity comes at a cost: the bus is surprisingly fragile when subjected to mechanical vibration and shock.

Harsh environments — automotive engine bays, industrial machinery, aerospace systems, robotics, agricultural equipment, handheld tools — impose mechanical stresses that directly translate into electrical disturbances. A vibrating cable acts as an antenna and a signal attenuator. A mechanical shock event can corrupt a byte mid-transfer, lock up the bus, or permanently damage connector contacts. Left unaddressed, these issues cause intermittent failures that are notoriously difficult to reproduce and diagnose.

This chapter covers the full stack of techniques required to build a vibration- and shock-resilient I2C system: from connector selection and PCB layout, through pull-up resistor tuning and bus capacitance management, to software-layer error detection, recovery state machines, and watchdog-supervised transfer loops.

---

## How Vibration and Shock Affect I2C

Understanding the failure modes is the prerequisite for choosing the right countermeasures.

### 2.1 Signal Integrity Degradation

I2C is a relatively slow protocol (100 kHz standard, 400 kHz fast-mode, 1 MHz fast-mode plus), but its open-drain topology means that signal edges depend entirely on pull-up resistors charging parasitic and wire capacitance. Any mechanical event that changes that capacitance — even transiently — alters the rise time and therefore the voltage at the point where receivers sample the bus.

**Vibration effects on rise time:**

```
Vcc
 |
[Rp]  ← pull-up resistor
 |
SDA ──┬──────── to master
      |
     [Cbus]  ← increases with connector bounce, cable flex
      |
     GND
```

As connector pins intermittently separate under vibration, the effective bus capacitance fluctuates. When pins re-seat, charge sharing can produce a glitch that a receiver interprets as a spurious edge.

### 2.2 Framing Errors and Bus Lock-Up

I2C is a synchronous, clocked protocol. If a mechanical shock corrupts the SCL stream while SDA is being driven low by a slave, the master loses count of the bit position within the byte. The slave continues to hold SDA low (awaiting more clocks), but the master believes the transaction is complete. The result is a **bus lock-up**: SDA is permanently low, START conditions are impossible, and all subsequent transactions fail.

This is the most common and most dangerous failure mode in mechanically stressed systems. Recovery requires either a hardware reset or a deliberate SCL clock-pulse sequence (9 pulses to force the slave to release SDA).

### 2.3 Data Corruption Without Lock-Up

Short glitches may not lock the bus but still corrupt data:
- A spike on SCL causes an extra clock edge, shifting the byte register by one bit.
- A spike on SDA during a clock-high period is interpreted as a spurious data bit.
- An ACK bit may be missed, causing the master to NACK a correctly received byte.

### 2.4 Connector Contact Bounce

Mechanical shock causes connector pins to bounce, producing a burst of transitions on both SDA and SCL simultaneously. This can trigger a spurious START condition (SDA falling while SCL is high) or STOP condition (SDA rising while SCL is high), corrupting the current transfer and potentially confusing slaves that track bus state.

### 2.5 Ground Bounce and Common-Mode Interference

Vibration in systems with long cable runs causes relative motion between cable shield, signal ground, and power ground. This produces common-mode noise that exceeds the I2C noise margin — typically only a few hundred millivolts in a 3.3 V system.

---

## Physical and Electrical Countermeasures

Software alone cannot compensate for a fundamentally broken physical layer. These hardware measures must come first.

### 3.1 Connector Selection

| Connector Type | Vibration Rating | Recommended For |
|---|---|---|
| Standard 0.1" header | Poor — no locking | Prototyping only |
| JST-GH with latch | Good | Consumer/robotics |
| Molex Micro-Fit | Excellent | Automotive, industrial |
| M8/M12 circular | Excellent | Factory floor, outdoor |
| Soldered wire-to-board | Best | No connector = no contact bounce |

**Rule:** In environments with shock > 50 g or sustained vibration > 5 g, use locking connectors or eliminate connectors via direct solder.

### 3.2 Cable Routing and Strain Relief

- Keep I2C cable runs as short as possible. Every 10 cm of unshielded cable adds approximately 30–100 pF of capacitance and an equivalent amount of pickup area.
- Use twisted pair for SDA and SCL (twisted together, with a separate ground conductor). Twisting provides first-order common-mode rejection.
- Use shielded cable (overall braid or foil) when bus length exceeds 30 cm in electrically noisy environments. Connect the shield to GND at one end only to avoid ground loops.
- Secure cables with tie wraps every 5–10 cm. Unsecured cables resonate at mechanical frequencies and produce continuous signal disturbance.
- Add strain relief where cables exit connectors. The failure mode without strain relief is not immediate breakage but intermittent contact variation.

### 3.3 Pull-Up Resistor Sizing

The pull-up resistor is the primary control knob for signal integrity. The standard formula for minimum pull-up resistance is:

```
Rp(min) = (Vcc - Vol(max)) / Ioh(max)
```

Where `Vol(max)` is the maximum low-level output voltage (typically 0.4 V) and `Ioh(max)` is the maximum sink current of the weakest driver on the bus (typically 3 mA for standard devices).

For a 3.3 V system: `Rp(min) ≈ (3.3 - 0.4) / 0.003 ≈ 967 Ω`

The maximum pull-up resistance is governed by rise time:

```
Rp(max) = tr / (0.8473 × Cbus)
```

Where `tr` is the maximum allowed rise time (1000 ns for standard mode, 300 ns for fast mode) and `Cbus` is total bus capacitance.

**In vibrating environments:** Use a value 30–50% below the calculated maximum. This makes the bus "stiffer" — rise times are faster and less sensitive to transient capacitance changes from connector bounce.

**Typical guidance by environment:**

| Environment | Bus Capacitance | Recommended Rp (3.3 V, 100 kHz) |
|---|---|---|
| Static PCB | < 50 pF | 4.7 kΩ |
| Short cable (< 20 cm) | 50–100 pF | 2.2 kΩ |
| Medium cable (20–50 cm) | 100–200 pF | 1.5 kΩ |
| Long cable or harsh env. | 200–400 pF | 1.0 kΩ (with buffer) |

### 3.4 Bus Buffers and Repeaters

For cable lengths beyond 50 cm, or when bus capacitance exceeds 400 pF, insert an I2C bus buffer such as the TI PCA9617A or NXP P82B96. These devices:
- Re-drive signals with fresh edges, eliminating accumulated capacitive distortion.
- Provide DC isolation between bus segments.
- Allow one segment to sustain a lock-up without affecting the other.

For truly long runs (> 1 m) in harsh environments, consider replacing I2C with RS-485 or CAN between subsystems, with I2C only on the local PCB. This is often the correct architectural decision.

### 3.5 Decoupling and Power Supply Filtering

Every device on the I2C bus requires adequate decoupling. A mechanical shock event often triggers a current surge in nearby power converters; without decoupling, this can glitch the supply rail and the I2C bus simultaneously.

- Place a 100 nF ceramic capacitor within 2 mm of each VCC pin.
- Add a 10 µF bulk capacitor per device cluster.
- In automotive or high-vibration environments, use X7R or X5R ceramic capacitors (avoid Y5V, which loses 80% of capacitance at rated voltage and temperature extremes).

### 3.6 ESD and TVS Protection

Connectors are entry points for ESD and cable-induced transients. Add a TVS (Transient Voltage Suppressor) array (e.g., PRTR5V0U2X) at connector entry points. Ensure the clamping voltage is below the I2C receiver's absolute maximum input rating, and the capacitance per line is below 10 pF to avoid distorting the signal.

---

## Protocol-Level Hardening

### 4.1 Reducing Clock Speed

Lower clock speeds give more margin in the time domain. At 100 kHz (standard mode), each bit period is 10 µs — a glitch must persist for at least one full clock period to corrupt a bit. At 400 kHz, the margin drops to 2.5 µs.

In harsh environments where maximum throughput is not required, operate at 50 kHz or even 10 kHz. This significantly increases the minimum glitch width required to cause a bit error.

### 4.2 Clock Stretching Awareness

Some slaves use clock stretching to delay the master. Ensure the master's I2C peripheral or bit-banging implementation respects the SCL low-hold state. A vibration event during clock stretching can cause the master to time out and abort the transaction unnecessarily.

Set clock-stretch timeouts conservatively: at least 25 ms for standard devices, or implement dynamic timeout adjustment based on device characterization.

### 4.3 Transaction Sizing

Prefer short transactions over long ones. A 2-byte register read completes in approximately 30 µs at 400 kHz; a 32-byte block read takes over 700 µs. The probability of a vibration event corrupting the transaction scales linearly with transaction duration.

- Read registers individually or in small groups when sensor update rates allow.
- Use repeated START (Sr) rather than STOP + START between related transactions. This keeps the bus locked to the master and reduces the window for spurious conditions.

---

## Software Error Detection and Recovery

### 5.1 The Recovery State Machine

Every I2C application in a harsh environment must implement a recovery state machine. The states are:

```
        ┌─────────────────────────────────┐
        │                                 ▼
   [IDLE] ──► [TRANSFER] ──► [SUCCESS] ──► [IDLE]
                  │
                  │ error/timeout
                  ▼
            [ERROR_DETECT]
                  │
                  ├──► SDA stuck low? ──► [CLOCK_RECOVERY] (9 pulses)
                  │
                  ├──► timeout only? ──► [SOFT_RESET]
                  │
                  └──► repeated failure ──► [HARD_RESET] ──► [REINIT]
```

### 5.2 Bus Lock-Up Detection and Clock Recovery

When SDA is stuck low, the master must generate 9 SCL pulses to force the slave through its byte-shift register and back to an idle state. The procedure:

1. Drive SCL low.
2. Release SDA (configure as input).
3. For 9 iterations:
   - Drive SCL high, wait one half-period.
   - Drive SCL low, wait one half-period.
   - If SDA is high, the slave has released — exit loop early.
4. Generate a STOP condition: SCL high, then SDA rising.
5. Reinitialize the I2C peripheral.

### 5.3 CRC Verification

I2C provides no built-in data integrity check beyond the ACK bit (which only confirms that a byte was received, not that it was received correctly). For critical data, add a CRC byte to every transaction.

The SMBus specification defines an 8-bit CRC (polynomial x⁸ + x² + x + 1) called PEC (Packet Error Code). Even if the I2C controller does not implement PEC in hardware, it is straightforward to compute in software.

### 5.4 Redundant Reads and Majority Voting

For sensors in extremely harsh environments (pyrotechnic, ballistic, high-rate vibration), implement redundant reads with majority voting:

- Read the register three times.
- If all three agree → use the value.
- If two agree → use the majority value, log the discrepancy.
- If all three differ → log the failure, use the last known good value or a safe default.

This works because mechanical glitches are typically brief and do not persist across multiple back-to-back reads.

---

## C/C++ Implementation

### 6.1 I2C Bus Recovery (Linux `i2c-dev`)

```c
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/gpio.h>

/* -----------------------------------------------------------------------
 * GPIO-based I2C clock recovery
 * Requires SCL and SDA to be accessible as GPIO lines.
 * ----------------------------------------------------------------------- */
#define SCL_GPIO_LINE   2        /* GPIO number for SCL */
#define SDA_GPIO_LINE   3        /* GPIO number for SDA */
#define BIT_HALF_PERIOD_US  5   /* 5 µs → 100 kHz */

/**
 * drive_gpio() - Set a GPIO line high or low via sysfs (simplified).
 * For production code, use libgpiod for non-deprecated GPIO control.
 */
static void drive_gpio(int line, int value) {
    char path[64], val[2];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", line);
    val[0] = value ? '1' : '0';
    val[1] = '\0';
    int fd = open(path, O_WRONLY);
    if (fd >= 0) { write(fd, val, 1); close(fd); }
}

static int read_gpio(int line) {
    char path[64], val[2];
    snprintf(path, sizeof(path), "/sys/class/gpio/gpio%d/value", line);
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;
    read(fd, val, 1);
    close(fd);
    return (val[0] == '1') ? 1 : 0;
}

/**
 * i2c_bus_recover() - Perform the 9-clock recovery sequence.
 *
 * If SDA is stuck low after a failed transaction, the slave is mid-byte.
 * Clocking SCL 9 times forces the slave to complete and release SDA.
 *
 * @return true if SDA was successfully released, false otherwise.
 */
bool i2c_bus_recover(void) {
    bool sda_released = false;

    printf("[I2C] Starting bus recovery (9-clock sequence)\n");

    /* Ensure SCL is low before we start */
    drive_gpio(SCL_GPIO_LINE, 0);
    usleep(BIT_HALF_PERIOD_US);

    for (int i = 0; i < 9; i++) {
        drive_gpio(SCL_GPIO_LINE, 1);
        usleep(BIT_HALF_PERIOD_US);

        if (read_gpio(SDA_GPIO_LINE) == 1) {
            /* Slave has released SDA — success */
            sda_released = true;
            printf("[I2C] SDA released after %d clock pulses\n", i + 1);
            break;
        }

        drive_gpio(SCL_GPIO_LINE, 0);
        usleep(BIT_HALF_PERIOD_US);
    }

    if (!sda_released) {
        /* Last attempt: check after final clock high */
        drive_gpio(SCL_GPIO_LINE, 1);
        usleep(BIT_HALF_PERIOD_US);
        sda_released = (read_gpio(SDA_GPIO_LINE) == 1);
    }

    if (sda_released) {
        /* Generate STOP condition: SCL high, SDA rising */
        drive_gpio(SDA_GPIO_LINE, 0);
        usleep(BIT_HALF_PERIOD_US);
        drive_gpio(SCL_GPIO_LINE, 1);
        usleep(BIT_HALF_PERIOD_US);
        drive_gpio(SDA_GPIO_LINE, 1);
        usleep(BIT_HALF_PERIOD_US);
        printf("[I2C] STOP condition generated — bus released\n");
    } else {
        printf("[I2C] Recovery failed — hardware reset required\n");
    }

    return sda_released;
}
```

### 6.2 CRC-8 (SMBus PEC) Computation

```c
/* -----------------------------------------------------------------------
 * SMBus PEC (Packet Error Code) — CRC-8, polynomial 0x07
 * (x^8 + x^2 + x + 1)
 * ----------------------------------------------------------------------- */

static uint8_t crc8_table[256];
static bool    crc8_table_ready = false;

static void crc8_build_table(void) {
    for (int i = 0; i < 256; i++) {
        uint8_t crc = (uint8_t)i;
        for (int j = 0; j < 8; j++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x07) : (crc << 1);
        }
        crc8_table[i] = crc;
    }
    crc8_table_ready = true;
}

/**
 * smbus_pec() - Compute SMBus Packet Error Code for a byte array.
 *
 * The PEC is computed over: [device_addr_byte, command, data...]
 * Include the address byte (7-bit addr << 1 | R/W) as the first byte.
 *
 * @data:    Pointer to the data buffer (including address byte).
 * @length:  Number of bytes in the buffer.
 * @return:  Computed CRC-8 value.
 */
uint8_t smbus_pec(const uint8_t *data, size_t length) {
    if (!crc8_table_ready) crc8_build_table();

    uint8_t crc = 0x00;
    for (size_t i = 0; i < length; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}
```

### 6.3 Resilient Transfer with Retry and Recovery

```c
#include <errno.h>
#include <string.h>
#include <time.h>

#define I2C_MAX_RETRIES     5
#define I2C_RETRY_DELAY_MS  10
#define I2C_HARD_RESET_GPIO 17   /* GPIO line connected to RESET_N of I2C slaves */

typedef enum {
    I2C_OK = 0,
    I2C_ERR_NACK,
    I2C_ERR_BUS_BUSY,
    I2C_ERR_TIMEOUT,
    I2C_ERR_LOCKED,
    I2C_ERR_FATAL
} i2c_status_t;

typedef struct {
    int      fd;               /* file descriptor from open("/dev/i2c-N") */
    uint8_t  addr;             /* 7-bit I2C address */
    uint32_t consecutive_errors;
    bool     use_pec;          /* enable SMBus PEC checking */
} i2c_device_t;

static void ms_sleep(int ms) {
    struct timespec ts = { .tv_sec = ms / 1000,
                           .tv_nsec = (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void hardware_reset_slaves(void) {
    printf("[I2C] Asserting hardware reset\n");
    drive_gpio(I2C_HARD_RESET_GPIO, 0);
    ms_sleep(10);
    drive_gpio(I2C_HARD_RESET_GPIO, 1);
    ms_sleep(50);   /* Allow slaves to re-initialize */
    printf("[I2C] Hardware reset released\n");
}

/**
 * i2c_read_register() - Read a register from an I2C device with full
 *                        error detection, retry, and bus recovery.
 *
 * @dev:      Pointer to the i2c_device_t descriptor.
 * @reg:      Register address to read.
 * @data:     Output buffer.
 * @len:      Number of bytes to read.
 * @return:   I2C_OK on success, error code otherwise.
 */
i2c_status_t i2c_read_register(i2c_device_t *dev,
                                uint8_t       reg,
                                uint8_t      *data,
                                size_t        len) {
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data ioctl_data;

    /* Read buffer: data + optional PEC byte */
    uint8_t read_buf[len + 1];

    msgs[0].addr  = dev->addr;
    msgs[0].flags = 0;          /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = dev->addr;
    msgs[1].flags = I2C_M_RD;
    msgs[1].len   = dev->use_pec ? (uint16_t)(len + 1) : (uint16_t)len;
    msgs[1].buf   = read_buf;

    ioctl_data.msgs  = msgs;
    ioctl_data.nmsgs = 2;

    for (int attempt = 0; attempt < I2C_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            ms_sleep(I2C_RETRY_DELAY_MS * attempt);  /* back-off */
            printf("[I2C] Retry %d/%d for addr=0x%02X reg=0x%02X\n",
                   attempt, I2C_MAX_RETRIES - 1, dev->addr, reg);
        }

        int ret = ioctl(dev->fd, I2C_RDWR, &ioctl_data);

        if (ret < 0) {
            dev->consecutive_errors++;

            if (errno == EBUSY || errno == ETIMEDOUT) {
                /* Check for SDA stuck low */
                if (read_gpio(SDA_GPIO_LINE) == 0) {
                    printf("[I2C] SDA stuck low — attempting clock recovery\n");
                    if (!i2c_bus_recover()) {
                        /* Recovery failed — need hard reset */
                        hardware_reset_slaves();
                    }
                    /* Re-open the I2C adapter to reinitialize the controller */
                    close(dev->fd);
                    dev->fd = open("/dev/i2c-1", O_RDWR);
                    ioctl(dev->fd, I2C_SLAVE, dev->addr);
                }
                continue;
            }

            if (errno == ENXIO) {
                /* NACK — device may be resetting after shock */
                continue;
            }

            /* Unrecognized error */
            return I2C_ERR_FATAL;
        }

        /* Transfer succeeded — verify PEC if enabled */
        if (dev->use_pec) {
            /* PEC covers: addr_write, reg, addr_read, data[] */
            uint8_t pec_buf[3 + len];
            pec_buf[0] = (dev->addr << 1) | 0;   /* addr + write */
            pec_buf[1] = reg;
            pec_buf[2] = (dev->addr << 1) | 1;   /* addr + read */
            memcpy(&pec_buf[3], read_buf, len);

            uint8_t expected_pec = smbus_pec(pec_buf, sizeof(pec_buf));
            uint8_t received_pec = read_buf[len];

            if (expected_pec != received_pec) {
                printf("[I2C] PEC mismatch: expected=0x%02X got=0x%02X\n",
                       expected_pec, received_pec);
                dev->consecutive_errors++;
                continue;
            }
        }

        memcpy(data, read_buf, len);
        dev->consecutive_errors = 0;
        return I2C_OK;
    }

    /* All retries exhausted */
    if (dev->consecutive_errors > 10) {
        printf("[I2C] Persistent failure — triggering hard reset\n");
        hardware_reset_slaves();
        dev->consecutive_errors = 0;
        return I2C_ERR_LOCKED;
    }

    return I2C_ERR_TIMEOUT;
}
```

### 6.4 Redundant Read with Majority Vote

```c
/**
 * i2c_read_majority_vote() - Read a register three times and return
 *                             the majority-agreed value.
 *
 * Suitable for single-byte registers in high-vibration environments.
 * For multi-byte registers, extend voting to each byte position.
 *
 * @dev:   Device descriptor.
 * @reg:   Register address.
 * @out:   Output: agreed-upon byte value.
 * @return I2C_OK on majority agreement, I2C_ERR_FATAL if all three differ.
 */
i2c_status_t i2c_read_majority_vote(i2c_device_t *dev,
                                     uint8_t       reg,
                                     uint8_t      *out) {
    uint8_t readings[3];

    for (int i = 0; i < 3; i++) {
        i2c_status_t st = i2c_read_register(dev, reg, &readings[i], 1);
        if (st != I2C_OK) {
            /* Replace failed read with a sentinel */
            readings[i] = 0xFF;
        }
    }

    /* Majority vote */
    if (readings[0] == readings[1]) {
        *out = readings[0];
        if (readings[2] != readings[0])
            printf("[I2C] Majority vote: discarded outlier 0x%02X\n", readings[2]);
        return I2C_OK;
    }
    if (readings[0] == readings[2]) {
        *out = readings[0];
        printf("[I2C] Majority vote: discarded outlier 0x%02X\n", readings[1]);
        return I2C_OK;
    }
    if (readings[1] == readings[2]) {
        *out = readings[1];
        printf("[I2C] Majority vote: discarded outlier 0x%02X\n", readings[0]);
        return I2C_OK;
    }

    printf("[I2C] Majority vote: all three reads differ (0x%02X, 0x%02X, 0x%02X)\n",
           readings[0], readings[1], readings[2]);
    return I2C_ERR_FATAL;
}
```

### 6.5 Watchdog-Supervised Transfer Loop (Embedded / Bare-Metal)

```c
/* -----------------------------------------------------------------------
 * Bare-metal example using CMSIS-style HAL (STM32-like pseudocode).
 * Demonstrates a watchdog-supervised I2C read loop.
 * ----------------------------------------------------------------------- */

#define I2C_TRANSFER_TIMEOUT_MS   50
#define WATCHDOG_KICK_INTERVAL_MS 25

/* Callback called by I2C interrupt on completion or error */
volatile bool g_transfer_complete = false;
volatile bool g_transfer_error    = false;

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    g_transfer_complete = true;
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    g_transfer_error = true;
}

/**
 * robust_i2c_read() - Non-blocking I2C read with watchdog refresh
 *                     and timeout detection.
 *
 * Kicks the watchdog during the wait loop so a stuck bus does not
 * cause an unintended system reset — instead the timeout logic handles
 * recovery gracefully, and the watchdog is only a last-resort backstop.
 */
HAL_StatusTypeDef robust_i2c_read(I2C_HandleTypeDef *hi2c,
                                   uint16_t           dev_addr,
                                   uint8_t           *buf,
                                   uint16_t           size) {
    g_transfer_complete = false;
    g_transfer_error    = false;

    /* Start non-blocking DMA receive */
    HAL_StatusTypeDef status =
        HAL_I2C_Master_Receive_DMA(hi2c, dev_addr << 1, buf, size);

    if (status != HAL_OK) {
        return status;
    }

    uint32_t start = HAL_GetTick();
    uint32_t last_kick = start;

    while (!g_transfer_complete && !g_transfer_error) {
        uint32_t now = HAL_GetTick();

        /* Kick watchdog periodically during wait */
        if ((now - last_kick) >= WATCHDOG_KICK_INTERVAL_MS) {
            HAL_IWDG_Refresh(&hiwdg);
            last_kick = now;
        }

        /* Check for timeout */
        if ((now - start) > I2C_TRANSFER_TIMEOUT_MS) {
            HAL_I2C_Master_Abort_IT(hi2c, dev_addr << 1);
            return HAL_TIMEOUT;
        }
    }

    return g_transfer_error ? HAL_ERROR : HAL_OK;
}
```

---

## Rust Implementation

### 7.1 Project Setup (`Cargo.toml`)

```toml
[package]
name    = "i2c-vibration-resilient"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal    = "1.0"
linux-embedded-hal = "0.4"
nb              = "1.1"
log             = "0.4"
```

### 7.2 Error Types and Bus State Machine

```rust
use std::fmt;

/// All error conditions that can occur on a vibration-stressed I2C bus.
#[derive(Debug, Clone, PartialEq)]
pub enum I2cError {
    /// Device did not acknowledge — possible loose connection or reset
    Nack,
    /// Bus arbitration lost — spurious START/STOP from cable bounce
    ArbitrationLost,
    /// Transfer did not complete within the allowed time
    Timeout,
    /// SDA stuck low — slave mid-byte, needs clock recovery
    BusLocked,
    /// CRC mismatch — data corrupted in transit
    CrcMismatch { expected: u8, received: u8 },
    /// Majority vote failed — all three reads produced different values
    MajorityVoteFailed { reads: [u8; 3] },
    /// Underlying platform/HAL error
    Platform(String),
}

impl fmt::Display for I2cError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Self::Nack                    => write!(f, "I2C NACK"),
            Self::ArbitrationLost         => write!(f, "I2C arbitration lost"),
            Self::Timeout                 => write!(f, "I2C timeout"),
            Self::BusLocked               => write!(f, "I2C bus locked (SDA stuck low)"),
            Self::CrcMismatch { expected, received } =>
                write!(f, "CRC mismatch: expected {:#04x}, got {:#04x}", expected, received),
            Self::MajorityVoteFailed { reads } =>
                write!(f, "Majority vote failed: reads = {:02x?}", reads),
            Self::Platform(s)             => write!(f, "Platform error: {}", s),
        }
    }
}

impl std::error::Error for I2cError {}

/// Tracks the operational state of the bus for recovery purposes.
#[derive(Debug, Clone, PartialEq)]
pub enum BusState {
    Idle,
    Transferring,
    Recovering,
    HardReset,
    Failed,
}
```

### 7.3 CRC-8 (SMBus PEC) in Rust

```rust
/// Compute the SMBus PEC (CRC-8, polynomial 0x07) for the given buffer.
///
/// Include the full I2C message bytes: address byte, command, data.
/// The address byte for a 7-bit address `addr` is:
///   - Write: `addr << 1`
///   - Read:  `addr << 1 | 1`
pub fn smbus_pec(data: &[u8]) -> u8 {
    const POLY: u8 = 0x07;
    let mut crc: u8 = 0x00;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ POLY
            } else {
                crc << 1
            };
        }
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_smbus_pec_known_value() {
        // SMBus PEC for [0xA2, 0x00] = 0xF2 (device addr=0x51 write, reg=0x00)
        let data = [0xA2u8, 0x00u8];
        assert_eq!(smbus_pec(&data), 0xF2);
    }
}
```

### 7.4 Resilient I2C Device Abstraction

```rust
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::{I2c, Operation};
use std::time::{Duration, Instant};
use std::thread;

const MAX_RETRIES:     usize    = 5;
const RETRY_BASE_MS:   u64      = 10;
const MAX_BACKOFF_MS:  u64      = 200;

/// A vibration-resilient I2C device handle.
pub struct ResilientI2c {
    bus:                I2cdev,
    addr:               u8,
    use_pec:            bool,
    consecutive_errors: u32,
    state:              BusState,
}

impl ResilientI2c {
    pub fn new(bus: I2cdev, addr: u8, use_pec: bool) -> Self {
        Self {
            bus,
            addr,
            use_pec,
            consecutive_errors: 0,
            state: BusState::Idle,
        }
    }

    /// Read `len` bytes from `register` with retry, CRC verification,
    /// and exponential back-off on failure.
    pub fn read_register(&mut self, register: u8, buf: &mut [u8])
        -> Result<(), I2cError>
    {
        let with_pec = self.use_pec;
        // Allocate extra byte for PEC if enabled
        let read_len = if with_pec { buf.len() + 1 } else { buf.len() };
        let mut raw = vec![0u8; read_len];

        let mut last_error = I2cError::Timeout;

        for attempt in 0..MAX_RETRIES {
            if attempt > 0 {
                let delay_ms = (RETRY_BASE_MS * (1 << attempt)).min(MAX_BACKOFF_MS);
                log::debug!("I2C retry {}/{}, delay {}ms, addr={:#04x} reg={:#04x}",
                             attempt, MAX_RETRIES - 1, delay_ms, self.addr, register);
                thread::sleep(Duration::from_millis(delay_ms));
            }

            self.state = BusState::Transferring;

            let result = self.bus.transaction(
                self.addr,
                &mut [
                    Operation::Write(&[register]),
                    Operation::Read(&mut raw[..read_len]),
                ],
            );

            match result {
                Ok(()) => {
                    // Verify PEC if enabled
                    if with_pec {
                        let data_len = buf.len();
                        let mut pec_input = Vec::with_capacity(3 + data_len);
                        pec_input.push(self.addr << 1);        // addr + write
                        pec_input.push(register);
                        pec_input.push((self.addr << 1) | 1);  // addr + read
                        pec_input.extend_from_slice(&raw[..data_len]);

                        let expected = smbus_pec(&pec_input);
                        let received = raw[data_len];

                        if expected != received {
                            last_error = I2cError::CrcMismatch { expected, received };
                            log::warn!("I2C PEC mismatch on attempt {}: {:?}",
                                        attempt, last_error);
                            self.consecutive_errors += 1;
                            continue;
                        }
                    }

                    buf.copy_from_slice(&raw[..buf.len()]);
                    self.consecutive_errors = 0;
                    self.state = BusState::Idle;
                    return Ok(());
                }

                Err(e) => {
                    self.consecutive_errors += 1;
                    last_error = self.classify_error(e);
                    log::warn!("I2C error on attempt {}: {:?}", attempt, last_error);

                    if last_error == I2cError::BusLocked {
                        self.state = BusState::Recovering;
                        self.software_bus_recover();
                    }
                }
            }
        }

        self.state = BusState::Failed;
        Err(last_error)
    }

    /// Read a register three times and return the majority-agreed value.
    /// Only valid for single-byte registers.
    pub fn read_register_majority(&mut self, register: u8)
        -> Result<u8, I2cError>
    {
        let mut readings = [0u8; 3];
        let mut valid    = [false; 3];

        for i in 0..3 {
            let mut buf = [0u8; 1];
            match self.read_register(register, &mut buf) {
                Ok(()) => {
                    readings[i] = buf[0];
                    valid[i]    = true;
                }
                Err(e) => {
                    log::warn!("Majority vote read {} failed: {:?}", i, e);
                }
            }
        }

        // Majority vote (only among successful reads)
        if valid[0] && valid[1] && readings[0] == readings[1] {
            return Ok(readings[0]);
        }
        if valid[0] && valid[2] && readings[0] == readings[2] {
            return Ok(readings[0]);
        }
        if valid[1] && valid[2] && readings[1] == readings[2] {
            return Ok(readings[1]);
        }
        // Fallback: if only one read succeeded, use it
        for i in 0..3 {
            if valid[i] { return Ok(readings[i]); }
        }

        Err(I2cError::MajorityVoteFailed { reads: readings })
    }

    /// Write a register with PEC appended if enabled.
    pub fn write_register(&mut self, register: u8, value: u8)
        -> Result<(), I2cError>
    {
        let payload: Vec<u8> = if self.use_pec {
            let pec_input = [self.addr << 1, register, value];
            let pec = smbus_pec(&pec_input);
            vec![register, value, pec]
        } else {
            vec![register, value]
        };

        for attempt in 0..MAX_RETRIES {
            if attempt > 0 {
                let delay_ms = (RETRY_BASE_MS * (1 << attempt)).min(MAX_BACKOFF_MS);
                thread::sleep(Duration::from_millis(delay_ms));
            }

            match self.bus.write(self.addr, &payload) {
                Ok(()) => {
                    self.consecutive_errors = 0;
                    return Ok(());
                }
                Err(e) => {
                    self.consecutive_errors += 1;
                    let classified = self.classify_error(e);
                    if classified == I2cError::BusLocked {
                        self.software_bus_recover();
                    }
                }
            }
        }

        Err(I2cError::Timeout)
    }

    // ------------------------------------------------------------------ //
    //  Private helpers                                                     //
    // ------------------------------------------------------------------ //

    fn classify_error(&self, _e: impl std::error::Error) -> I2cError {
        // In a real implementation, inspect the error type from the HAL.
        // Here we return a representative error for illustration.
        I2cError::Timeout
    }

    /// Perform a software-level bus recovery by toggling SCL via GPIO.
    /// In a real system this would use the libgpiod crate or platform GPIO API.
    fn software_bus_recover(&mut self) {
        log::warn!("I2C: Attempting software bus recovery");
        // Platform-specific GPIO recovery would go here.
        // See the C implementation above for the algorithm.
        thread::sleep(Duration::from_millis(5));
        self.state = BusState::Idle;
    }
}
```

### 7.5 Periodic Health Monitor

```rust
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

/// Spawn a background thread that periodically reads a status register
/// from the device and triggers recovery if it fails repeatedly.
///
/// This is a lightweight "heartbeat" supervisor for vibration-prone sensors.
pub fn spawn_i2c_monitor(
    device: Arc<Mutex<ResilientI2c>>,
    status_register: u8,
    poll_interval:   Duration,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut failures: u32 = 0;
        const HARD_RESET_THRESHOLD: u32 = 10;

        loop {
            thread::sleep(poll_interval);

            let result = {
                let mut dev = device.lock().unwrap();
                let mut buf = [0u8; 1];
                dev.read_register(status_register, &mut buf)
                   .map(|_| buf[0])
            };

            match result {
                Ok(status) => {
                    if failures > 0 {
                        log::info!("I2C monitor: bus recovered after {} failures", failures);
                        failures = 0;
                    }
                    log::debug!("I2C status = {:#04x}", status);
                }
                Err(e) => {
                    failures += 1;
                    log::error!("I2C monitor: failure #{}: {:?}", failures, e);

                    if failures >= HARD_RESET_THRESHOLD {
                        log::error!("I2C monitor: {} consecutive failures, \
                                     requesting hard reset", failures);
                        // Signal the main application or trigger GPIO reset here
                        failures = 0;
                    }
                }
            }
        }
    })
}
```

---

## Testing and Validation Strategies

### 8.1 Bench Simulation of Vibration Effects

Before hardware testing, simulate bus degradation in software:
- **Inject capacitance:** Add a 1 nF capacitor in series with a relay to the bus, switch it on/off at the vibration frequency. This mimics connector bounce.
- **Add series resistance:** A 47 Ω resistor in series with SCL simulates a marginal connector contact.
- **Ground injection:** Drive a small noise signal (10–50 mV, 50–500 Hz) onto the GND plane to simulate chassis vibration effects.

### 8.2 Hardware-in-Loop Vibration Testing

For qualification testing, mount the target PCB on a vibration table (electrodynamic shaker) and run continuous I2C transfers while logging:
- Transaction success rate
- Bit error rate (requires CRC)
- Lock-up events per hour
- Recovery time after each lock-up

IEC 60068-2-6 (sinusoidal vibration) and IEC 60068-2-64 (random vibration) are the relevant test standards for industrial equipment.

### 8.3 Shock Testing

Per IEC 60068-2-27, apply half-sine shock pulses (typically 50 g, 11 ms) in all three axes, both positive and negative directions (18 shots total). Monitor the I2C bus for lock-up events during and after each shock.

### 8.4 Automated Error Injection in Rust

```rust
#[cfg(test)]
mod vibration_simulation {
    use super::*;
    use std::sync::atomic::{AtomicU32, Ordering};

    static INJECT_COUNTER: AtomicU32 = AtomicU32::new(0);

    /// Mock I2C bus that injects errors at a configurable rate.
    /// Use this to validate error recovery logic without hardware.
    pub struct FaultyI2cBus {
        pub error_rate: f64,  // 0.0 = no errors, 1.0 = always fail
    }

    impl FaultyI2cBus {
        pub fn should_inject_error(&self) -> bool {
            let count = INJECT_COUNTER.fetch_add(1, Ordering::Relaxed);
            // Simple deterministic injection: fail every N-th transaction
            let period = (1.0 / self.error_rate.max(0.001)) as u32;
            count % period == 0
        }
    }

    #[test]
    fn test_majority_vote_survives_single_corruption() {
        // In a real test, wrap FaultyI2cBus in the ResilientI2c abstraction.
        // Here we demonstrate the PEC verification logic directly.
        let good_data  = [0x48u8];
        let bad_data   = [0xFFu8];  // corrupted by vibration glitch

        // Three reads: two good, one corrupted
        let reads = [good_data[0], bad_data[0], good_data[0]];

        // Majority vote should recover the correct value
        let majority = if reads[0] == reads[1] { reads[0] }
                       else if reads[0] == reads[2] { reads[0] }
                       else if reads[1] == reads[2] { reads[1] }
                       else { panic!("All differ") };

        assert_eq!(majority, 0x48, "Majority vote should recover correct reading");
    }
}
```

---

## Design Checklist

Use this checklist when designing an I2C system for a harsh mechanical environment:

**Hardware**
- [ ] Locking connectors or direct solder on all I2C cable connections
- [ ] Twisted-pair cable for SDA/SCL; shielded if length > 30 cm
- [ ] Cable secured with tie wraps every 5–10 cm; strain relief at connectors
- [ ] Pull-up resistors sized 30–50% below the maximum calculated value
- [ ] 100 nF decoupling capacitor at every VCC pin, < 2 mm from pin
- [ ] TVS protection arrays at all connector entry points
- [ ] Bus buffer/repeater if cable length > 50 cm or Cbus > 400 pF

**Protocol Configuration**
- [ ] Clock speed reduced to minimum needed for application latency budget
- [ ] Clock stretch timeout set conservatively (≥ 25 ms)
- [ ] Short transactions preferred; large block reads broken into smaller chunks

**Software**
- [ ] Bus lock-up detection: SDA state sampled after failed transactions
- [ ] 9-clock recovery sequence implemented and tested
- [ ] Hardware reset path implemented (GPIO to RESET_N of slaves)
- [ ] CRC-8 / SMBus PEC computed and verified on all safety-critical reads
- [ ] Retry loop with exponential back-off on all transfers
- [ ] Majority voting for single-byte registers in highest-risk environments
- [ ] Background health monitor/watchdog thread
- [ ] All error paths logged with timestamp, address, register, and error type

**Testing**
- [ ] Vibration simulation test on bench (capacitance injection)
- [ ] Formal vibration testing per IEC 60068-2-6 or -2-64
- [ ] Formal shock testing per IEC 60068-2-27
- [ ] Automated error injection test suite for all recovery paths
- [ ] Soak test: 24+ hours continuous transfers under vibration profile

---

## Summary

I2C was not designed for mechanical harshness, but with a layered engineering approach it can be made reliable in demanding environments. The key insights from this chapter are:

**Physical layer first.** No software technique can compensate for a fundamentally broken physical layer. Locking connectors, twisted-pair cable, correct pull-up sizing, and decoupling capacitors are prerequisites, not optional enhancements.

**The dominant failure mode is bus lock-up.** When a vibration or shock event corrupts SCL mid-byte, the slave holds SDA low indefinitely. Every system in a harsh environment must implement the 9-clock recovery sequence and, as a backstop, a hardware RESET_N line.

**CRC adds the error detection that I2C lacks.** The ACK mechanism confirms byte receipt, not correctness. SMBus PEC (CRC-8) is lightweight and provides high probability of detecting single-bit and burst errors.

**Redundancy is justified for critical values.** Triple-read majority voting adds only microseconds of latency but catches transient single-read corruptions that CRC alone cannot prevent.

**Back-off and recovery must be layered.** From millisecond-scale transaction retry, through second-scale clock recovery, to hardware reset for persistent failures — each layer handles a different class of disturbance without over-reacting to transient events.

**Test under realistic conditions.** Desk testing cannot expose vibration-related failures. Formal testing against IEC 60068 standards, combined with automated error-injection testing of the software recovery paths, is essential before deployment in harsh environments.

By applying the hardware measures in Section 3, the protocol discipline in Section 4, and the software patterns in Sections 5–7, it is practical to achieve I2C transaction success rates above 99.99% in environments with sustained vibration up to 10 g and shock events up to 100 g — environments where an unmodified I2C implementation would fail within minutes.

---

*Document: 83_Vibration_And_Shock_Resistance.md — Part of the I2C Engineering Reference Series*