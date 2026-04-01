# 54. CBUS Mode — Two-Wire CBUS Protocol Compatibility and Differences from Standard I²C

**Protocol fundamentals** — the exact frame structure (`[START] [4-bit dev addr] [4-bit sub-addr] [8-bit data] [STOP]`), timing constraints (≤ 31.25 kHz), and the absence of any ACK phase or read direction.

**Differences from I²C** — a full comparison table covering address width, device count, data direction, ACK, clock stretching, multi-master, and frequency.

**Three C/C++ examples:**
- Linux `I2C_RDWR` ioctl approach for host systems
- Portable GPIO bit-bang implementation for any MCU
- STM32 HAL mixed-bus example (hardware I²C + software CBUS on the same system)

**Two Rust examples:**
- `embedded-hal` v1.0 generic bit-bang driver (portable across RP2040, STM32, ESP32, etc.)
- Linux raw `ioctl` implementation for SBCs like Raspberry Pi

**Practical sections** on pull-up resistor sizing, bus isolation with I²C multiplexers, arbitration, logic analyzer setup, and a debugging table of common failure modes.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [Historical Background](#2-historical-background)
3. [CBUS Protocol Fundamentals](#3-cbus-protocol-fundamentals)
   - 3.1 [Physical Layer](#31-physical-layer)
   - 3.2 [Signal Timing and Levels](#32-signal-timing-and-levels)
   - 3.3 [CBUS Address and Data Frame](#33-cbus-address-and-data-frame)
4. [Key Differences from Standard I²C](#4-key-differences-from-standard-i2c)
5. [CBUS Compatibility in I²C Controllers](#5-cbus-compatibility-in-i2c-controllers)
6. [CBUS Mode Configuration](#6-cbus-mode-configuration)
7. [Programming in C/C++](#7-programming-in-cc)
   - 7.1 [Linux I²C-dev with CBUS Device Simulation](#71-linux-i2c-dev-with-cbus-device-simulation)
   - 7.2 [Bit-Banging CBUS Protocol in C](#72-bit-banging-cbus-protocol-in-c)
   - 7.3 [STM32 HAL — Mixed Bus with CBUS Slave](#73-stm32-hal--mixed-bus-with-cbus-slave)
8. [Programming in Rust](#8-programming-in-rust)
   - 8.1 [Embedded-hal Bit-Bang CBUS Driver](#81-embedded-hal-bit-bang-cbus-driver)
   - 8.2 [Linux /dev/i2c CBUS Raw Transfer in Rust](#82-linux-devi2c-cbus-raw-transfer-in-rust)
9. [CBUS on Mixed I²C Buses — Practical Considerations](#9-cbus-on-mixed-i2c-buses--practical-considerations)
10. [Debugging CBUS Issues](#10-debugging-cbus-issues)
11. [Summary](#11-summary)

---

## 1. Introduction

**CBUS** (Control Bus) is a two-wire serial protocol originally defined by Philips (now NXP) as part of the broader I²C specification family. It was designed primarily for **consumer electronics control applications** — specifically for the remote control and power management of audio/video equipment — and predates several features of modern I²C.

CBUS operates on the same physical wires as I²C (`SCL` and `SDA`, or their CBUS equivalents `CCLK` and `CDAT`) but uses a fundamentally **different electrical and logical protocol**. Modern I²C controllers that are "CBUS compatible" can acknowledge CBUS messages on the bus without misinterpreting them as I²C traffic, but they cannot natively decode the CBUS payload.

Understanding CBUS mode is essential when:

- Designing systems that bridge legacy consumer electronics to modern microcontrollers.
- Using I²C controllers that advertise CBUS compatibility (e.g., PCA9544, PCF8584, UM10204-compliant hardware).
- Reverse-engineering or maintaining older embedded systems with mixed bus populations.

---

## 2. Historical Background

CBUS was introduced by Philips in the **early 1990s** to provide a low-pin-count control interface for consumer appliances. Its primary use case was the **inter-IC Sound (IIS)** and **inter-IC control** domain: connecting a microcontroller in a television or VCR to peripheral ICs such as tuners, audio processors, and infrared decoders.

Key milestones:

| Year | Event |
|------|-------|
| ~1992 | CBUS first specified by Philips alongside I²C Rev. 1 |
| 1995 | I²C specification Rev. 2.0 formally acknowledges CBUS as a compatible subset |
| 2000 | I²C Rev. 2.1 defines CBUS compatibility explicitly in section 4.3 |
| 2012 | NXP UM10204 (I²C-bus specification Rev. 6) retains CBUS compatibility clauses |
| 2021 | UM10204 Rev. 7.0 maintains backward CBUS compatibility requirements |

CBUS devices are now rare in new designs but remain present in legacy industrial and consumer equipment that engineers must interface with.

---

## 3. CBUS Protocol Fundamentals

### 3.1 Physical Layer

CBUS uses the same open-drain / open-collector two-wire topology as I²C:

```
VCC
 │
 ├──[ Rp ]──┬── CCLK (Clock)
 │          │
 │   ┌──────┤
 │   │  IC1 │
 │   └──────┘
 │
 ├──[ Rp ]──┬── CDAT (Data)
 │          │
 │   ┌──────┤
 │   │  IC1 │
 │   └──────┘
GND
```

- **CCLK**: Clock line, equivalent to I²C SCL.
- **CDAT**: Data line, equivalent to I²C SDA.
- Pull-up resistors are required on both lines (typically 4.7 kΩ at 3.3 V or 10 kΩ at 5 V).
- Multiple devices share both lines in a multi-drop configuration.

### 3.2 Signal Timing and Levels

CBUS is **significantly slower** than I²C Standard Mode:

| Parameter | CBUS | I²C Standard | I²C Fast |
|-----------|------|--------------|----------|
| Max clock frequency | **31.25 kHz** | 100 kHz | 400 kHz |
| Logic HIGH threshold | ≥ 0.7 × VCC | ≥ 0.7 × VCC | ≥ 0.7 × VCC |
| Logic LOW threshold | ≤ 0.3 × VCC | ≤ 0.3 × VCC | ≤ 0.3 × VCC |
| Clock stretch support | No | Yes | Yes |
| Acknowledge mechanism | **Different** (see §3.3) | Standard ACK | Standard ACK |

The most critical timing constraint: **CCLK must not exceed 31.25 kHz**. Driving a CBUS device faster will cause malfunction or missed packets.

### 3.3 CBUS Address and Data Frame

This is where CBUS diverges most sharply from I²C.

#### I²C Frame Structure (recap)

```
S | ADDR[6:0] | R/W | ACK | DATA[7:0] | ACK | ... | P
```

- 7-bit address + R/W bit
- Byte-oriented, each byte acknowledged by receiver pulling SDA low

#### CBUS Frame Structure

```
S | ADDR[3:0] | SUBADDR[3:0] | DATA[7:0] | P
  └──4-bit──┘  └────4-bit───┘  └──8-bit──┘
```

Key structural differences:

1. **4-bit device address** (not 7-bit): CBUS supports only **16 unique device addresses**.
2. **No separate R/W bit**: CBUS is **write-only by design** — the protocol has no read transaction.
3. **No ACK/NACK handshake**: CBUS devices do **not** pull CDAT low after receiving bytes. There is no acknowledgment phase.
4. **Sub-address nibble**: A second 4-bit field selects a register or function within the device.
5. **START and STOP conditions** are electrically identical to I²C (SDA falls while SCL high = START; SDA rises while SCL high = STOP).

#### Bit-Level Diagram

```
Clock (CCLK):
 _   _   _   _   _   _   _   _   _   _   _   _   _   _   _   _
| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_| |_|

Data  (CDAT):
‾\_A3 A2 A1 A0 S3 S2 S1 S0 D7 D6 D5 D4 D3 D2 D1 D0_/‾
 ST                                                    SP
```

Where:
- `ST` = START condition
- `A3..A0` = 4-bit device address (MSB first)
- `S3..S0` = 4-bit sub-address (MSB first)
- `D7..D0` = 8-bit data
- `SP` = STOP condition
- No ACK clock cycles exist between fields

---

## 4. Key Differences from Standard I²C

| Feature | Standard I²C | CBUS |
|---------|-------------|------|
| Address width | 7-bit (or 10-bit) | **4-bit** |
| Max devices on bus | 112 (7-bit) | **16** |
| Data direction | Read and Write | **Write only** |
| Acknowledge | Required per byte | **None** |
| Clock stretching | Supported | **Not supported** |
| Max frequency | 100 kHz (Std) / 400 kHz (Fast) | **31.25 kHz** |
| Frame length | Variable | **Fixed: 16 bits address/cmd + 8 bits data** |
| Sub-addressing | Via data bytes | **Built into frame (4 bits)** |
| Multi-master | Supported with arbitration | **Not specified** |
| Bus collision detection | Yes (SDA monitoring) | **Not defined** |
| Repeated START | Supported | **Not supported** |

### Coexistence on the Same Bus

I²C Rev. 2.1 and later define **CBUS compatibility** to mean:

> An I²C device that is CBUS compatible will **not** interpret a CBUS START+4-bit-address frame as a valid I²C transaction and will **not** attempt to respond or interfere.

This is guaranteed by the address space separation:
- CBUS uses only the low nibble (0x0–0xF) of address space in its 4-bit scheme.
- The I²C 7-bit address `0bXXXXYYYY` will never alias to a CBUS address because I²C always processes 7 bits before the R/W bit, whereas CBUS emits only 4 address bits then transitions directly to sub-address bits — the bit count difference causes I²C slaves to desynchronize and ignore the packet.

However, **a CBUS master must still respect I²C electrical rules** (open-drain, pull-up timing) to avoid corrupting ongoing I²C transactions from other masters.

---

## 5. CBUS Compatibility in I²C Controllers

Many modern I²C peripheral controllers expose a **CBUS compatible mode** flag. When enabled, the controller:

1. Formats outgoing transactions with 4-bit address + 4-bit sub-address + 8-bit data (16 total bits before STOP).
2. Drops the ACK wait phase entirely.
3. Limits clock to ≤ 31.25 kHz automatically.
4. Uses standard I²C START/STOP conditions.

Examples of controllers with CBUS mode support:

- **PCF8584** (NXP) — classic I²C bus controller IC with explicit CBUS mode register bit.
- **PCA9544** (NXP) — I²C multiplexer that passes CBUS frames transparently.
- **DesignWare I²C IP** (Synopsys) — used in many SoCs (including Linux-driven systems); supports `IC_CON.SPEED = 0b11` for CBUS.
- **STM32 I²C peripheral** — does not natively support CBUS mode; requires software emulation.
- **Microchip MCP2221A** — USB-to-I²C bridge; CBUS framing must be emulated in software.

---

## 6. CBUS Mode Configuration

When using a controller with native CBUS support, configuration typically involves:

1. **Set bus speed** to ≤ 31.25 kHz (period ≥ 32 µs).
2. **Enable CBUS mode** via a dedicated control register bit.
3. **Disable ACK generation** and ACK polling.
4. **Format the address field** as 4 bits, not 7 bits.

For controllers without native support (e.g., STM32, AVR, ESP32), the protocol must be **bit-banged** using GPIO, or wrapped in an I²C transaction that matches CBUS electrical behavior.

### Register Configuration Example (PCF8584)

```
Control Register S1:
Bit 7: ENI  — Enable I²C (1 = enabled)
Bit 6: STA  — Generate START
Bit 5: STO  — Generate STOP
Bit 4: ACK  — Acknowledge bit (set to 0 for CBUS — no ACK)
Bit 3: CB   — CBUS mode select (1 = CBUS, 0 = I²C)
Bit 2-0: ...
```

Setting `CB=1` and `ACK=0` places the PCF8584 into CBUS master mode.

---

## 7. Programming in C/C++

### 7.1 Linux I²C-dev with CBUS Device Simulation

On Linux, the `i2c-dev` interface does not natively support CBUS mode. However, you can use `I2C_RDWR` with raw message structures to send exactly 2 bytes (address nibble + sub-address nibble packed, then data byte) at 31.25 kHz — mimicking what a CBUS master would send.

The following example demonstrates sending a CBUS-formatted packet over a Linux I²C adapter by constructing the raw byte stream manually:

```c
/*
 * cbus_linux.c
 * Sends a CBUS-compatible frame over Linux /dev/i2c-X
 *
 * Compile: gcc -o cbus_linux cbus_linux.c
 * Usage:   ./cbus_linux /dev/i2c-1 <dev_addr_4bit> <sub_addr_4bit> <data_byte>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

/* CBUS clock must not exceed 31.25 kHz.
 * Linux I2C adapters are typically set via:
 *   echo 31250 > /sys/bus/i2c/devices/i2c-1/of_node/clock-frequency
 * or adapter-specific sysfs/dtb parameters.
 * This code assumes the adapter is already at the correct speed.
 */

#define CBUS_MAX_FREQ_HZ  31250

typedef struct {
    int      fd;        /* open file descriptor to /dev/i2c-X */
    uint8_t  dev_addr;  /* 4-bit CBUS device address (0x0 – 0xF) */
} cbus_dev_t;

/*
 * cbus_open - Open the I2C adapter for CBUS use.
 *
 * Note: We use a dummy 7-bit I2C address derived from the 4-bit CBUS
 * address because the Linux i2c-dev kernel layer requires a 7-bit address.
 * The actual CBUS framing is achieved via I2C_RDWR with no-ACK flags where
 * supported, or by packing addr+sub into the first data byte when not.
 */
int cbus_open(cbus_dev_t *dev, const char *bus_path, uint8_t cbus_addr_4bit)
{
    if (cbus_addr_4bit > 0x0F) {
        fprintf(stderr, "CBUS address must be 4-bit (0x0–0xF)\n");
        return -1;
    }

    dev->fd = open(bus_path, O_RDWR);
    if (dev->fd < 0) {
        perror("open");
        return -1;
    }
    dev->dev_addr = cbus_addr_4bit;
    return 0;
}

void cbus_close(cbus_dev_t *dev)
{
    if (dev->fd >= 0) close(dev->fd);
    dev->fd = -1;
}

/*
 * cbus_write - Send one CBUS frame:
 *   [START] [addr_nibble | sub_nibble] [data_byte] [STOP]
 *
 * Packed into two bytes for the I2C write payload:
 *   byte[0] = (dev_addr << 4) | sub_addr   (the "command" byte)
 *   byte[1] = data
 *
 * The I2C 7-bit slave address is set to (dev_addr & 0x0F) so the
 * I2C START condition carries the correct lower nibble; the upper nibble
 * of the actual CBUS address field is encoded in byte[0].
 *
 * WARNING: This emulation is approximate. A true CBUS frame has:
 *   4-bit addr + 4-bit sub + 8-bit data = 16 data bits, no ACK.
 * The Linux I2C subsystem will still clock an ACK cycle. Use a dedicated
 * CBUS controller IC (e.g. PCF8584) for strict protocol compliance.
 */
int cbus_write(cbus_dev_t *dev, uint8_t sub_addr, uint8_t data)
{
    if (sub_addr > 0x0F) {
        fprintf(stderr, "CBUS sub-address must be 4-bit (0x0–0xF)\n");
        return -1;
    }

    /* Pack CBUS "address byte": upper nibble = device addr, lower = sub-addr */
    uint8_t payload[2];
    payload[0] = (uint8_t)((dev->dev_addr << 4) | (sub_addr & 0x0F));
    payload[1] = data;

    struct i2c_msg msg = {
        .addr  = (uint16_t)(dev->dev_addr & 0x0F), /* 7-bit I2C addr (approx) */
        .flags = 0,                                  /* write */
        .len   = sizeof(payload),
        .buf   = payload,
    };

    struct i2c_rdwr_ioctl_data xfer = {
        .msgs  = &msg,
        .nmsgs = 1,
    };

    if (ioctl(dev->fd, I2C_RDWR, &xfer) < 0) {
        perror("I2C_RDWR (CBUS write)");
        return -1;
    }
    return 0;
}

/* --------------- main --------------- */
int main(int argc, char *argv[])
{
    if (argc != 5) {
        fprintf(stderr,
            "Usage: %s <i2c_bus> <dev_addr_hex4> <sub_addr_hex4> <data_hex>\n"
            "  Example: %s /dev/i2c-1 0x5 0x3 0xA0\n", argv[0], argv[0]);
        return EXIT_FAILURE;
    }

    const char *bus   = argv[1];
    uint8_t dev_addr  = (uint8_t)strtol(argv[2], NULL, 16);
    uint8_t sub_addr  = (uint8_t)strtol(argv[3], NULL, 16);
    uint8_t data      = (uint8_t)strtol(argv[4], NULL, 16);

    cbus_dev_t dev;
    if (cbus_open(&dev, bus, dev_addr) < 0) return EXIT_FAILURE;

    printf("CBUS frame → dev=0x%X sub=0x%X data=0x%02X\n",
           dev_addr, sub_addr, data);

    int rc = cbus_write(&dev, sub_addr, data);
    if (rc == 0) printf("Frame sent successfully.\n");

    cbus_close(&dev);
    return (rc == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

---

### 7.2 Bit-Banging CBUS Protocol in C

For microcontrollers without native CBUS support, implement the protocol directly on GPIO pins. This example is portable C99 targeting any platform with GPIO toggle functions.

```c
/*
 * cbus_bitbang.c
 * Pure software CBUS master implementation via GPIO bit-banging.
 *
 * Platform-specific stubs (gpio_set_high, gpio_set_low, delay_us)
 * must be implemented for the target MCU.
 */

#include <stdint.h>
#include <stdbool.h>

/* ---------------------------------------------------------------
 * Platform Abstraction — implement for your target MCU
 * --------------------------------------------------------------- */
extern void gpio_set_high(int pin);
extern void gpio_set_low(int pin);
extern void delay_us(uint32_t us);

/* Pin assignments — change to match your hardware */
#define CBUS_CCLK_PIN   4
#define CBUS_CDAT_PIN   5

/*
 * CBUS clock period: must be >= 32 µs (31.25 kHz max).
 * Half-period = 16 µs → quarter-period = 8 µs for setup/hold margins.
 */
#define CBUS_HALF_PERIOD_US   16u
#define CBUS_QUARTER_PERIOD_US 8u

/* ---------------------------------------------------------------
 * Low-level signal helpers
 * --------------------------------------------------------------- */

static inline void cclk_high(void) { gpio_set_high(CBUS_CCLK_PIN); }
static inline void cclk_low(void)  { gpio_set_low(CBUS_CCLK_PIN);  }
static inline void cdat_high(void) { gpio_set_high(CBUS_CDAT_PIN); }
static inline void cdat_low(void)  { gpio_set_low(CBUS_CDAT_PIN);  }
static inline void half_period(void) { delay_us(CBUS_HALF_PERIOD_US); }
static inline void quarter_period(void) { delay_us(CBUS_QUARTER_PERIOD_US); }

/* ---------------------------------------------------------------
 * CBUS START condition
 * SDA falls while SCL is HIGH (same as I²C)
 * --------------------------------------------------------------- */
static void cbus_start(void)
{
    cdat_high();
    cclk_high();
    quarter_period();
    cdat_low();   /* SDA falls while SCL high → START */
    quarter_period();
    cclk_low();
    quarter_period();
}

/* ---------------------------------------------------------------
 * CBUS STOP condition
 * SDA rises while SCL is HIGH (same as I²C)
 * --------------------------------------------------------------- */
static void cbus_stop(void)
{
    cdat_low();
    quarter_period();
    cclk_high();
    quarter_period();
    cdat_high();  /* SDA rises while SCL high → STOP */
    half_period();
}

/* ---------------------------------------------------------------
 * Transmit one bit on CDAT.
 * Data is sampled on the RISING edge of CCLK.
 * No ACK — caller manages all bit counts.
 * --------------------------------------------------------------- */
static void cbus_send_bit(bool bit)
{
    if (bit) cdat_high(); else cdat_low();
    quarter_period();
    cclk_high();
    half_period();
    cclk_low();
    quarter_period();
}

/* ---------------------------------------------------------------
 * Transmit a nibble (4 bits), MSB first.
 * --------------------------------------------------------------- */
static void cbus_send_nibble(uint8_t nibble)
{
    for (int i = 3; i >= 0; i--) {
        cbus_send_bit((nibble >> i) & 0x01);
    }
}

/* ---------------------------------------------------------------
 * Transmit one byte (8 bits), MSB first.
 * --------------------------------------------------------------- */
static void cbus_send_byte(uint8_t byte)
{
    for (int i = 7; i >= 0; i--) {
        cbus_send_bit((byte >> i) & 0x01);
    }
}

/* ---------------------------------------------------------------
 * cbus_send_frame - Transmit a complete CBUS frame.
 *
 * Frame structure:
 *   [START] [dev_addr: 4 bits] [sub_addr: 4 bits] [data: 8 bits] [STOP]
 *   Total: 16 data bits, no ACK phase.
 *
 * @param dev_addr   4-bit device address (0x0 – 0xF)
 * @param sub_addr   4-bit sub-address / command (0x0 – 0xF)
 * @param data       8-bit data payload
 *
 * @return  0 on success, -1 if parameters are out of range
 * --------------------------------------------------------------- */
int cbus_send_frame(uint8_t dev_addr, uint8_t sub_addr, uint8_t data)
{
    if (dev_addr > 0x0F || sub_addr > 0x0F) {
        return -1;  /* Address or sub-address exceeds 4-bit range */
    }

    cbus_start();
    cbus_send_nibble(dev_addr);   /* 4-bit device address, MSB first */
    cbus_send_nibble(sub_addr);   /* 4-bit sub-address,   MSB first */
    cbus_send_byte(data);         /* 8-bit data,          MSB first */
    cbus_stop();

    return 0;
}

/* ---------------------------------------------------------------
 * Example: Send multiple CBUS frames (e.g. to configure a TV tuner)
 * --------------------------------------------------------------- */
void cbus_example_sequence(void)
{
    /* Device at address 0x5, sub-command 0x2, data = 0x80 (e.g. power on) */
    cbus_send_frame(0x5, 0x2, 0x80);

    /* Device at address 0x5, sub-command 0x3, data = 0x44 (e.g. set channel) */
    cbus_send_frame(0x5, 0x3, 0x44);

    /* Device at address 0xA, sub-command 0x0, data = 0xFF (broadcast reset) */
    cbus_send_frame(0xA, 0x0, 0xFF);
}
```

---

### 7.3 STM32 HAL — Mixed Bus with CBUS Slave

In a system where STM32 is the I²C master and must coexist with a CBUS slave on the same bus, the STM32 bit-bangs CBUS frames while I²C traffic to other devices uses the hardware peripheral. This example uses STM32 HAL GPIO for the CBUS lines and the hardware I²C for other slaves.

```c
/*
 * cbus_stm32.c
 * STM32 HAL implementation: mixed I²C + CBUS master on shared bus.
 *
 * Hardware I²C peripheral handles normal I²C slaves.
 * Bit-banged CBUS handles legacy CBUS-only devices.
 *
 * Assumptions:
 *   - CBUS device wired to PA4 (CCLK) and PA5 (CDAT)
 *   - Hardware I²C1 wired to PB6 (SCL) and PB7 (SDA)
 *   - Both buses share the same VCC and ground reference
 *   - STM32CubeMX-generated HAL init already called
 */

#include "main.h"       /* STM32 HAL includes, GPIO defines */
#include <stdint.h>
#include <stdbool.h>

/* CBUS GPIO — must be configured as Output Open-Drain in CubeMX */
#define CBUS_CCLK_PORT   GPIOA
#define CBUS_CCLK_PIN    GPIO_PIN_4
#define CBUS_CDAT_PORT   GPIOA
#define CBUS_CDAT_PIN    GPIO_PIN_5

/* External I²C handle from CubeMX */
extern I2C_HandleTypeDef hi2c1;

/* ---------------------------------------------------------------
 * Timing: 31.25 kHz → period = 32 µs → half = 16 µs
 * Using DWT cycle counter for precision; falls back to HAL_Delay.
 * --------------------------------------------------------------- */
static inline void cbus_delay_us(uint32_t us)
{
    /* Simple DWT-based microsecond delay (requires DWT_Init() at startup) */
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000u);
    while ((DWT->CYCCNT - start) < cycles) { __NOP(); }
}

#define CBUS_HALF_US   16u
#define CBUS_QTR_US     8u

static void cclk_h(void) { HAL_GPIO_WritePin(CBUS_CCLK_PORT, CBUS_CCLK_PIN, GPIO_PIN_SET); }
static void cclk_l(void) { HAL_GPIO_WritePin(CBUS_CCLK_PORT, CBUS_CCLK_PIN, GPIO_PIN_RESET); }
static void cdat_h(void) { HAL_GPIO_WritePin(CBUS_CDAT_PORT, CBUS_CDAT_PIN, GPIO_PIN_SET); }
static void cdat_l(void) { HAL_GPIO_WritePin(CBUS_CDAT_PORT, CBUS_CDAT_PIN, GPIO_PIN_RESET); }

static void cbus_start_condition(void)
{
    cdat_h(); cclk_h(); cbus_delay_us(CBUS_QTR_US);
    cdat_l(); cbus_delay_us(CBUS_QTR_US);  /* SDA low while SCL high */
    cclk_l(); cbus_delay_us(CBUS_QTR_US);
}

static void cbus_stop_condition(void)
{
    cdat_l(); cbus_delay_us(CBUS_QTR_US);
    cclk_h(); cbus_delay_us(CBUS_QTR_US);
    cdat_h(); cbus_delay_us(CBUS_HALF_US); /* SDA high while SCL high */
}

static void cbus_clock_bit(bool level)
{
    if (level) cdat_h(); else cdat_l();
    cbus_delay_us(CBUS_QTR_US);
    cclk_h();
    cbus_delay_us(CBUS_HALF_US);
    cclk_l();
    cbus_delay_us(CBUS_QTR_US);
}

/*
 * cbus_transmit - Full CBUS frame transmission.
 * Returns HAL_OK (0) on success.
 */
HAL_StatusTypeDef cbus_transmit(uint8_t dev_addr4,
                                uint8_t sub_addr4,
                                uint8_t data)
{
    if (dev_addr4 > 0x0F || sub_addr4 > 0x0F) return HAL_ERROR;

    cbus_start_condition();

    /* 4-bit device address, MSB first */
    for (int8_t i = 3; i >= 0; i--)
        cbus_clock_bit((dev_addr4 >> i) & 1);

    /* 4-bit sub-address, MSB first */
    for (int8_t i = 3; i >= 0; i--)
        cbus_clock_bit((sub_addr4 >> i) & 1);

    /* 8-bit data, MSB first */
    for (int8_t i = 7; i >= 0; i--)
        cbus_clock_bit((data >> i) & 1);

    cbus_stop_condition();
    return HAL_OK;
}

/*
 * Example: Mixed bus usage — talk to an I²C sensor AND a CBUS IR blaster.
 */
void mixed_bus_example(void)
{
    uint8_t sensor_reg = 0x00;
    uint8_t sensor_val[2];

    /* Normal I²C read from a temperature sensor at address 0x48 */
    HAL_I2C_Mem_Read(&hi2c1, 0x48 << 1, sensor_reg,
                     I2C_MEMADD_SIZE_8BIT, sensor_val, 2, HAL_MAX_DELAY);

    /* CBUS write to a legacy A/V controller at CBUS address 0x3 */
    /* sub=0x1 → "volume", data=0x70 → volume level 112 */
    cbus_transmit(0x3, 0x1, 0x70);

    /* CBUS write: sub=0x0 → "power state", data=0x01 → power on */
    cbus_transmit(0x3, 0x0, 0x01);
}
```

---

## 8. Programming in Rust

### 8.1 Embedded-hal Bit-Bang CBUS Driver

This implementation uses the `embedded-hal` v1.0 traits for GPIO and delay, making it portable across any MCU with a compatible HAL (e.g., `rp2040-hal`, `stm32f4xx-hal`, `esp-idf-hal`).

```rust
//! cbus_driver.rs
//! A portable CBUS master driver using embedded-hal v1.0 traits.
//!
//! Add to Cargo.toml:
//!   [dependencies]
//!   embedded-hal = "1.0"

use embedded_hal::digital::OutputPin;
use embedded_hal::delay::DelayNs;

/// CBUS timing constants (31.25 kHz max → 32 µs period)
const HALF_PERIOD_NS: u32 = 16_000; // 16 µs
const QUARTER_PERIOD_NS: u32 = 8_000; // 8 µs

/// Error type for CBUS operations
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum CbusError {
    /// Device or sub-address exceeds 4-bit range
    AddressOutOfRange,
    /// GPIO pin operation failed
    GpioError,
}

/// CBUS master driver
///
/// `CCLK`: clock pin (open-drain output)
/// `CDAT`: data pin (open-drain output)
/// `DELAY`: delay provider implementing `DelayNs`
pub struct CbusMaster<CCLK, CDAT, DELAY>
where
    CCLK: OutputPin,
    CDAT: OutputPin,
    DELAY: DelayNs,
{
    cclk: CCLK,
    cdat: CDAT,
    delay: DELAY,
}

impl<CCLK, CDAT, DELAY> CbusMaster<CCLK, CDAT, DELAY>
where
    CCLK: OutputPin,
    CDAT: OutputPin,
    DELAY: DelayNs,
{
    /// Create a new CBUS master.
    /// Pins should be configured as open-drain outputs with external pull-ups.
    pub fn new(cclk: CCLK, cdat: CDAT, delay: DELAY) -> Self {
        Self { cclk, cdat, delay }
    }

    /// Release the underlying pins and delay back to the caller.
    pub fn free(self) -> (CCLK, CDAT, DELAY) {
        (self.cclk, self.cdat, self.delay)
    }

    // ------- Private signal helpers -------

    fn cclk_high(&mut self) {
        let _ = self.cclk.set_high();
    }
    fn cclk_low(&mut self) {
        let _ = self.cclk.set_low();
    }
    fn cdat_high(&mut self) {
        let _ = self.cdat.set_high();
    }
    fn cdat_low(&mut self) {
        let _ = self.cdat.set_low();
    }
    fn half_period(&mut self) {
        self.delay.delay_ns(HALF_PERIOD_NS);
    }
    fn quarter_period(&mut self) {
        self.delay.delay_ns(QUARTER_PERIOD_NS);
    }

    // ------- CBUS bus conditions -------

    /// Generate CBUS START: CDAT falls while CCLK is HIGH.
    fn start(&mut self) {
        self.cdat_high();
        self.cclk_high();
        self.quarter_period();
        self.cdat_low(); // SDA falling edge while CLK high = START
        self.quarter_period();
        self.cclk_low();
        self.quarter_period();
    }

    /// Generate CBUS STOP: CDAT rises while CCLK is HIGH.
    fn stop(&mut self) {
        self.cdat_low();
        self.quarter_period();
        self.cclk_high();
        self.quarter_period();
        self.cdat_high(); // SDA rising edge while CLK high = STOP
        self.half_period();
    }

    /// Clock one bit onto CDAT. Data sampled on CCLK rising edge.
    fn clock_bit(&mut self, bit: bool) {
        if bit {
            self.cdat_high();
        } else {
            self.cdat_low();
        }
        self.quarter_period();
        self.cclk_high();
        self.half_period();
        self.cclk_low();
        self.quarter_period();
    }

    /// Transmit a nibble (4 bits), MSB first.
    fn send_nibble(&mut self, nibble: u8) {
        for i in (0..4).rev() {
            self.clock_bit((nibble >> i) & 1 != 0);
        }
    }

    /// Transmit one byte (8 bits), MSB first.
    fn send_byte(&mut self, byte: u8) {
        for i in (0..8).rev() {
            self.clock_bit((byte >> i) & 1 != 0);
        }
    }

    // ------- Public API -------

    /// Send a single CBUS frame.
    ///
    /// # Arguments
    /// * `dev_addr`  — 4-bit device address (0x0–0xF)
    /// * `sub_addr`  — 4-bit sub-address / register (0x0–0xF)
    /// * `data`      — 8-bit data payload
    ///
    /// # Frame layout (no ACK phase)
    /// ```text
    /// [START] [dev_addr: 4b] [sub_addr: 4b] [data: 8b] [STOP]
    /// ```
    pub fn send_frame(
        &mut self,
        dev_addr: u8,
        sub_addr: u8,
        data: u8,
    ) -> Result<(), CbusError> {
        if dev_addr > 0x0F || sub_addr > 0x0F {
            return Err(CbusError::AddressOutOfRange);
        }

        self.start();
        self.send_nibble(dev_addr); // 4-bit device address
        self.send_nibble(sub_addr); // 4-bit sub-address
        self.send_byte(data);       // 8-bit data
        self.stop();

        Ok(())
    }

    /// Send a sequence of CBUS frames (command table).
    ///
    /// Each tuple is `(dev_addr, sub_addr, data)`.
    pub fn send_sequence(
        &mut self,
        frames: &[(u8, u8, u8)],
    ) -> Result<(), CbusError> {
        for &(dev, sub, data) in frames {
            self.send_frame(dev, sub, data)?;
            // Optional inter-frame gap — many CBUS devices need ≥ 10 µs
            self.delay.delay_ns(10_000);
        }
        Ok(())
    }
}

// ------- Example usage (no_std entry point) -------

// Shown here as a doc-test / usage example block:
//
// fn main_example() {
//     // Platform-specific pin and delay setup (e.g. rp2040-hal):
//     let mut pac = rp2040_pac::Peripherals::take().unwrap();
//     let sio = Sio::new(pac.SIO);
//     let pins = Pins::new(pac.IO_BANK0, pac.PADS_BANK0, sio.gpio_bank0, &mut pac.RESETS);
//
//     let cclk_pin = pins.gpio4.into_push_pull_output(); // use open-drain if available
//     let cdat_pin = pins.gpio5.into_push_pull_output();
//     let delay = cortex_m::delay::Delay::new(core.SYST, clocks.system_clock.freq().to_Hz());
//
//     let mut cbus = CbusMaster::new(cclk_pin, cdat_pin, delay);
//
//     // Send a sequence to a legacy CBUS A/V device (addr=0x5)
//     let commands: [(u8, u8, u8); 3] = [
//         (0x5, 0x0, 0x01),  // Power on
//         (0x5, 0x1, 0x70),  // Volume = 112
//         (0x5, 0x3, 0x0C),  // Select input channel 12
//     ];
//     cbus.send_sequence(&commands).expect("CBUS error");
// }
```

---

### 8.2 Linux /dev/i2c CBUS Raw Transfer in Rust

For Linux targets (e.g., Raspberry Pi, BeagleBone, industrial SBCs), use the `i2cdev` crate or raw `ioctl` calls.

```rust
//! cbus_linux.rs
//! CBUS-frame emulation over Linux /dev/i2c-X using raw ioctl.
//!
//! Add to Cargo.toml:
//!   [dependencies]
//!   nix = { version = "0.27", features = ["ioctl"] }
//!
//! Run: cargo run -- /dev/i2c-1 0x5 0x3 0xA0

use std::env;
use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;
use std::process;

// Linux I2C ioctl constants
const I2C_RDWR: u64 = 0x0707;
const I2C_M_WR: u16 = 0x0000;

/// Corresponds to struct i2c_msg in <linux/i2c.h>
#[repr(C)]
struct I2cMsg {
    addr: u16,
    flags: u16,
    len: u16,
    buf: *mut u8,
}

/// Corresponds to struct i2c_rdwr_ioctl_data in <linux/i2c-dev.h>
#[repr(C)]
struct I2cRdwrIoctlData {
    msgs: *mut I2cMsg,
    nmsgs: u32,
}

/// CbusDevice wraps a Linux I2C adapter file descriptor.
struct CbusDevice {
    file: File,
    dev_addr: u8, // 4-bit CBUS device address
}

impl CbusDevice {
    /// Open the I2C bus adapter for CBUS use.
    fn open(bus_path: &str, dev_addr: u8) -> Result<Self, Box<dyn std::error::Error>> {
        if dev_addr > 0x0F {
            return Err("CBUS device address must fit in 4 bits (0x0–0xF)".into());
        }
        let file = OpenOptions::new().read(true).write(true).open(bus_path)?;
        Ok(Self { file, dev_addr })
    }

    /// Send one CBUS frame via I2C_RDWR ioctl.
    ///
    /// The CBUS frame [dev_addr:4 | sub_addr:4 | data:8] is mapped into
    /// an I2C write transaction as two bytes, with the I2C 7-bit slave
    /// address approximated from the lower nibble of the CBUS address.
    ///
    /// See the C example for caveats regarding ACK emulation.
    fn send_frame(&self, sub_addr: u8, data: u8) -> Result<(), Box<dyn std::error::Error>> {
        if sub_addr > 0x0F {
            return Err("CBUS sub-address must fit in 4 bits (0x0–0xF)".into());
        }

        // Pack: upper nibble = CBUS device addr, lower = sub-addr
        let mut payload = [
            (self.dev_addr << 4) | (sub_addr & 0x0F),
            data,
        ];

        let mut msg = I2cMsg {
            addr: self.dev_addr as u16,
            flags: I2C_M_WR,
            len: payload.len() as u16,
            buf: payload.as_mut_ptr(),
        };

        let mut xfer = I2cRdwrIoctlData {
            msgs: &mut msg as *mut I2cMsg,
            nmsgs: 1,
        };

        let fd = self.file.as_raw_fd();

        // Safety: we pass valid pointers to correctly-sized structs.
        let ret = unsafe {
            libc_ioctl(fd, I2C_RDWR, &mut xfer as *mut I2cRdwrIoctlData)
        };

        if ret < 0 {
            return Err(format!(
                "ioctl I2C_RDWR failed: {}",
                std::io::Error::last_os_error()
            ).into());
        }

        Ok(())
    }
}

/// Thin wrapper around the ioctl syscall (avoids pulling in full libc dependency).
unsafe fn libc_ioctl(fd: i32, request: u64, arg: *mut I2cRdwrIoctlData) -> i32 {
    let result: i64;
    core::arch::asm!(
        "syscall",
        in("rax") 16u64,      // SYS_ioctl
        in("rdi") fd as u64,
        in("rsi") request,
        in("rdx") arg as u64,
        out("rcx") _,
        out("r11") _,
        lateout("rax") result,
        options(nostack, preserves_flags),
    );
    result as i32
}

fn main() {
    let args: Vec<String> = env::args().collect();

    if args.len() != 5 {
        eprintln!(
            "Usage: {} <i2c_bus> <dev_addr_hex4> <sub_addr_hex4> <data_hex>",
            args[0]
        );
        eprintln!("  Example: {} /dev/i2c-1 0x5 0x3 0xA0", args[0]);
        process::exit(1);
    }

    let bus = &args[1];
    let dev_addr = u8::from_str_radix(args[2].trim_start_matches("0x"), 16)
        .expect("Invalid dev_addr");
    let sub_addr = u8::from_str_radix(args[3].trim_start_matches("0x"), 16)
        .expect("Invalid sub_addr");
    let data = u8::from_str_radix(args[4].trim_start_matches("0x"), 16)
        .expect("Invalid data");

    let device = CbusDevice::open(bus, dev_addr).unwrap_or_else(|e| {
        eprintln!("Failed to open CBUS device: {e}");
        process::exit(1);
    });

    println!(
        "Sending CBUS frame → dev=0x{dev_addr:X} sub=0x{sub_addr:X} data=0x{data:02X}"
    );

    device.send_frame(sub_addr, data).unwrap_or_else(|e| {
        eprintln!("CBUS send failed: {e}");
        process::exit(1);
    });

    println!("Frame sent successfully.");
}
```

---

## 9. CBUS on Mixed I²C Buses — Practical Considerations

### Bus Speed Hierarchy

When CBUS and I²C devices share the same two wires, the **entire bus must operate at 31.25 kHz** or lower. This impacts all I²C devices, including fast-mode slaves. Verify that all I²C devices on the shared bus are compatible with the reduced clock rate.

If speed degradation is unacceptable, use an **I²C multiplexer** (e.g., PCA9544) to isolate the CBUS segment:

```
MCU I²C Master (up to 400 kHz)
        │
     PCA9544 (multiplexer)
    ┌────┴────┐
  Ch0        Ch1 (31.25 kHz segment)
(fast I²C)   │
            [CBUS Device]
            [I²C Slave at 31.25 kHz]
```

### Pull-up Resistor Selection

For a mixed bus at 31.25 kHz with 5 V logic and 3 CBUS devices + 2 I²C devices:

```
Rp = (VCC - VOL) / (N × IOL_max)
   = (5.0 - 0.4) / (5 × 3 mA)
   ≈ 307 Ω minimum

Recommended range for 31.25 kHz: 4.7 kΩ – 10 kΩ
```

At 31.25 kHz the capacitive load constraint is relaxed compared to Fast Mode, so 10 kΩ pull-ups are viable for most board layouts.

### Bus Arbitration

CBUS has no defined arbitration mechanism. If multiple CBUS masters might transmit simultaneously on a shared bus:

- Implement software arbitration (monitor CDAT for unexpected level changes during transmission).
- Use a semaphore/mutex in RTOS environments.
- Consider a dedicated CBUS segment per master.

---

## 10. Debugging CBUS Issues

### Logic Analyzer Setup

Capture CBUS frames with a logic analyzer using **I²C decode mode** at ≤ 31.25 kHz. The analyzer will decode the first byte as an I²C address (reporting false NACK because CBUS has no ACK), but the bit sequence will be visible and correct for manual inspection.

Alternatively, set the analyzer to **asynchronous/raw serial** mode and manually parse:

```
Expected capture for cbus_send_frame(0x5, 0x3, 0xA0):
  START
  Bits[15:12] = 0101  → dev_addr = 0x5
  Bits[11:8]  = 0011  → sub_addr = 0x3
  Bits[7:0]   = 10100000 → data = 0xA0
  STOP
```

### Common Issues and Fixes

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| Device ignores all frames | Clock too fast (> 31.25 kHz) | Reduce SCL frequency |
| Intermittent missed frames | Bus capacitance too high | Reduce pull-up Rp or shorten PCB traces |
| I²C slaves misbehave during CBUS | CBUS bits aliasing into I²C address space | Verify CBUS device address ≠ any I²C slave address in lower nibble |
| Data corruption on last bit | STOP condition timing too tight | Add ≥ 4 µs hold time before STOP |
| False I²C NACK on logic analyzer | Expected — CBUS has no ACK | Ignore analyzer ACK errors; decode bits manually |

---

## 11. Summary

**CBUS** is a two-wire serial control protocol in the I²C family, designed for simple write-only control of consumer electronics ICs at up to **31.25 kHz**. It shares the same open-drain physical layer as I²C but differs structurally in every other way: a **4-bit device address** (16 devices max), a **built-in 4-bit sub-address** field, an **8-bit data payload**, and **absolutely no acknowledgment phase**. There is also no read direction — CBUS is inherently unidirectional (master-to-slave only).

Key takeaways:

- **Coexistence is possible** on the same two wires as I²C, because the frame structure is distinct enough that I²C-compliant slaves will desynchronize and ignore CBUS traffic, and CBUS slaves operate below any I²C speed tier.
- **The entire shared bus must run at 31.25 kHz or below.** If this is unacceptable, use an I²C multiplexer to isolate the CBUS segment.
- **Native CBUS mode** is rare in modern controllers. Most implementations require software bit-banging, which is straightforward given the simple frame structure (16 bits of addressing + 8 bits of data, no ACK).
- **In C/C++**, CBUS is best implemented via GPIO bit-banging with careful timing (16 µs half-period) or via controllers like the PCF8584 that have a dedicated CBUS mode register bit.
- **In Rust**, the `embedded-hal` v1.0 trait system provides a clean, portable abstraction for bit-banged CBUS using generic `OutputPin` and `DelayNs` bounds, enabling code reuse across all MCU HAL implementations.
- **On Linux**, CBUS framing can be approximated through `I2C_RDWR` ioctl with carefully packed payloads, but strict protocol compliance requires either a hardware CBUS controller or a userspace GPIO bit-bang driver.

CBUS remains important for engineers working with legacy A/V ICs, older set-top box chipsets, and any embedded system that must interface with hardware designed before modern I²C Fast Mode became ubiquitous.

---

*Document: 54_CBUS_Mode.md — Part of the I²C Protocol Reference Series*