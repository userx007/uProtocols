# 06. UART Voltage Levels and Logic


**Standards covered:** TTL (5 V), CMOS (3.3 V / 1.8 V), RS-232, and RS-485 — with exact voltage thresholds, noise margins, polarity conventions, max cable lengths, and typical use cases.

**Level shifting section** covers resistor dividers, BSS138 MOSFET bidirectional shifters, MAX232/MAX3232 for RS-232, and MAX485-family transceivers for RS-485 with direction control.

**Code examples included:**

| # | Language | Topic |
|---|---|---|
| 1 | C (POSIX) | RS-232 port open/configure/read/write via `termios` |
| 2 | C (Linux) | RS-485 half-duplex with GPIO DE control via `libgpiod` |
| 3 | C (STM32 HAL) | RS-485 direction control with TC flag for safe DE de-assertion |
| 4 | C | Voltage standard compatibility checker utility |
| 5 | Rust | RS-232 communication with the `serialport` crate |
| 6 | Rust | RS-485 half-duplex with GPIO direction control |
| 7 | Rust | Type-safe voltage standard model with baud/length calculations |

The summary synthesizes the hardware decision criteria and the key software concern (RS-485 DE timing) in a concise paragraph.

> TTL, CMOS, RS-232, RS-485 voltage standards and level shifting requirements

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Voltage Levels Matter in UART](#why-voltage-levels-matter-in-uart)
3. [TTL Logic Levels](#1-ttl-transistor-transistor-logic)
4. [CMOS Logic Levels](#2-cmos-complementary-metal-oxide-semiconductor)
5. [RS-232 Standard](#3-rs-232-standard)
6. [RS-485 Standard](#4-rs-485-standard)
7. [Voltage Standard Comparison Table](#voltage-standard-comparison-table)
8. [Level Shifting](#level-shifting)
9. [C/C++ Programming Examples](#cc-programming-examples)
10. [Rust Programming Examples](#rust-programming-examples)
11. [Common Pitfalls and Best Practices](#common-pitfalls-and-best-practices)
12. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is a protocol, not a physical layer standard. It defines *how* bits are framed and timed, but says nothing about *what voltages* represent a logical 0 or 1. The physical voltage levels used on the wire depend entirely on the electrical standard chosen for the interface. Understanding these voltage standards is critical for:

- Correctly interconnecting microcontrollers, peripherals, and host systems
- Protecting hardware from overvoltage damage
- Ensuring reliable communication over varying cable lengths
- Diagnosing intermittent or completely failed UART links

The four most common electrical standards encountered in embedded and industrial UART work are **TTL**, **CMOS**, **RS-232**, and **RS-485**. Each has distinct voltage thresholds, signal polarity conventions, and intended use cases.

---

## Why Voltage Levels Matter in UART

UART is inherently a digital protocol: every bit is either a Mark (logical 1, also called *idle* or *stop*) or a Space (logical 0, also called *start*). What varies dramatically between standards is:

- **The absolute voltages** used for Mark and Space
- **Signal polarity** — some standards invert the sense of 0 and 1
- **Reference** — single-ended (one wire referenced to ground) versus differential (two wires whose *difference* carries the signal)
- **Drive strength and noise immunity** — determining maximum cable length and noise tolerance

Connecting two devices using different voltage standards without level shifting is a common source of hardware failure. For example, connecting an RS-232 line (which can swing to ±12 V) directly to a 3.3 V microcontroller GPIO will permanently damage the microcontroller.

---

## 1. TTL (Transistor-Transistor Logic)

### Overview

TTL originated with bipolar junction transistor logic families (7400-series ICs) and operates from a **5 V supply**. It is the "classic" logic level found in older microcontrollers, Arduino Uno boards, and many 5 V peripherals.

### Logic Thresholds

| Level | Output | Input |
|---|---|---|
| Logic HIGH (Mark / Idle) | ≥ 2.4 V | ≥ 2.0 V |
| Logic LOW (Space / Start) | ≤ 0.4 V | ≤ 0.8 V |
| Supply Voltage | 5 V | 5 V |

The **noise margin** is 0.4 V on both rails (the gap between guaranteed output levels and the required input thresholds).

### UART Signal Convention

In TTL UART, the line is **idle HIGH** (Mark = 5 V, logical 1). A transmission begins with a start bit pulling the line LOW (Space = 0 V, logical 0). Data bits follow LSB-first, then optional parity, then stop bit(s) that return the line HIGH.

### Characteristics

- Single-ended, referenced to a common ground
- Short-distance only (typically < 1 meter)
- Susceptible to ground noise and capacitive loading
- No inherent noise immunity for long runs

---

## 2. CMOS (Complementary Metal-Oxide-Semiconductor)

### Overview

Modern microcontrollers — ARM Cortex-M devices, ESP32, STM32, RP2040, etc. — use CMOS logic operating at **3.3 V** (and sometimes 1.8 V or 5 V). CMOS logic defines thresholds as fractions of VDD rather than as fixed absolute voltages, making the family inherently voltage-scalable.

### Logic Thresholds (3.3 V CMOS)

| Level | Output | Input |
|---|---|---|
| Logic HIGH | ≥ 2.4 V (often ≥ VDD − 0.1 V) | ≥ 0.7 × VDD = 2.31 V |
| Logic LOW | ≤ 0.4 V (often ≤ 0.1 V) | ≤ 0.3 × VDD = 0.99 V |
| Supply Voltage | 3.3 V | 3.3 V |

### Logic Thresholds (1.8 V CMOS)

| Level | Output | Input |
|---|---|---|
| Logic HIGH | ≥ 1.7 V | ≥ 1.26 V |
| Logic LOW | ≤ 0.1 V | ≤ 0.54 V |

### Characteristics

- Single-ended, referenced to common ground
- Lower power consumption than TTL (no static current path)
- Narrower supply voltages mean stricter level shifting requirements
- 5 V TTL signals will damage 3.3 V CMOS inputs (overvoltage)
- Most modern MCU UART peripherals are 3.3 V CMOS

### 5 V Tolerant Pins

Some CMOS microcontrollers mark certain GPIO pins as *5 V tolerant*. These pins have input clamp protection and can accept 5 V TTL signals while the device runs at 3.3 V. **Always verify the datasheet** — not all pins are 5 V tolerant, and even tolerant pins usually cannot source 5 V.

---

## 3. RS-232 Standard

### Overview

RS-232 (now formally EIA/TIA-232) was defined in 1969 for interfacing Data Terminal Equipment (DTE, e.g., a computer) to Data Circuit-terminating Equipment (DCE, e.g., a modem). Despite its age, RS-232 remains widespread in industrial equipment, test instruments, PLCs, and legacy PC interfaces.

### Signal Convention — Inverted Logic

RS-232 uses **inverted (negative) logic** compared to TTL/CMOS:

| State | Voltage Range | TTL/CMOS Equivalent |
|---|---|---|
| Mark (Logical 1 / Idle / Stop) | −3 V to −15 V | HIGH |
| Space (Logical 0 / Start) | +3 V to +15 V | LOW |
| Undefined / Transition Zone | −3 V to +3 V | — |

The **guaranteed driver output** is ±5 V minimum; ±12 V is typical. The **receiver threshold** is ±3 V (anything between −3 V and +3 V is undefined).

### Key Points

- **Single-ended** (one signal wire + ground per signal)
- **Maximum cable length:** 15 m (50 ft) at 20 kbaud, or until capacitance exceeds 2500 pF
- **Maximum data rate:** 20 kbps per the original standard (practically much higher over short cables)
- Supports a rich set of handshake lines: RTS, CTS, DTR, DSR, DCD, RI
- DB-9 or DB-25 connectors traditionally used
- Requires dedicated level-shifting IC (e.g., MAX232, SP3232, MAX3232) to interface with TTL/CMOS MCUs

### Common DB-9 Pin Assignment (DTE Perspective)

| Pin | Signal | Direction |
|---|---|---|
| 1 | DCD | Input |
| 2 | RxD | Input |
| 3 | TxD | Output |
| 4 | DTR | Output |
| 5 | GND | — |
| 6 | DSR | Input |
| 7 | RTS | Output |
| 8 | CTS | Input |
| 9 | RI | Input |

### RS-232 Limitations

- Only point-to-point (one driver, one receiver)
- Cannot drive long cables at high baud rates
- No differential noise rejection
- High voltage levels require dedicated driver/receiver ICs

---

## 4. RS-485 Standard

### Overview

RS-485 (EIA/TIA-485) is a **differential**, **multi-drop** serial standard designed for long-distance industrial communication. It is the physical layer underlying protocols such as Modbus RTU, DMX512, BACnet MS/TP, and PROFIBUS.

### Differential Signaling

RS-485 uses two wires, conventionally labeled **A (−)** and **B (+)**, or sometimes D− and D+. The **voltage difference** (V_B − V_A) determines the logic level:

| State | V(B−A) | Logical Meaning |
|---|---|---|
| Mark (logical 1) | ≥ +200 mV | Idle / Stop bit |
| Space (logical 0) | ≤ −200 mV | Start bit / Data 0 |
| Common-mode range | −7 V to +12 V | Acceptable range for V_A and V_B individually |

Drivers must produce at least ±1.5 V differential. The ±200 mV receiver threshold provides a generous noise margin.

### Multi-Drop Bus Topology

RS-485 supports up to **32 unit loads** (standard) on a single two-wire bus. Extended-load transceivers (1/4 or 1/8 unit load) allow up to 128 or 256 nodes. One node talks at a time; all others must tri-state their drivers.

### Key Specifications

| Parameter | Value |
|---|---|
| Maximum nodes (standard) | 32 (up to 256 with 1/8 UL transceivers) |
| Maximum cable length | 1200 m (4000 ft) at 100 kbps |
| Maximum data rate | 10 Mbps (over short cables) |
| Common-mode rejection | ±7 V to ±12 V depending on transceiver |
| Termination | 120 Ω at each end of the bus |
| Topology | Linear bus (daisy-chain); star topologies degrade signal integrity |

### Half-Duplex vs. Full-Duplex

Most RS-485 deployments use **half-duplex** (two wires, one pair for both transmit and receive, direction controlled by a driver-enable pin). Full-duplex RS-485 (four wires, separate TX and RX pairs) is also possible.

### Bus Termination

A **120 Ω termination resistor** must be placed at each end of the cable to prevent reflections. Fail-safe biasing resistors (typically 560 Ω pull-up on B, pull-down on A) ensure the bus defaults to a known state when no driver is active.

---

## Voltage Standard Comparison Table

| Property | TTL (5V) | CMOS (3.3V) | RS-232 | RS-485 |
|---|---|---|---|---|
| Supply | 5 V | 3.3 V | ±12 V (typical) | 5 V (transceiver) |
| Logic HIGH voltage | ≥ 2.4 V | ≥ 2.31 V | −3 V to −15 V | V_B − V_A ≥ +0.2 V |
| Logic LOW voltage | ≤ 0.8 V | ≤ 0.99 V | +3 V to +15 V | V_B − V_A ≤ −0.2 V |
| Signal type | Single-ended | Single-ended | Single-ended | Differential |
| Max nodes | 1:1 | 1:1 | 1:1 | 32–256 |
| Max cable length | < 1 m | < 1 m | 15 m @ 20 kbps | 1200 m @ 100 kbps |
| Max baud rate | Depends on MCU | Depends on MCU | ~1 Mbps practical | 10 Mbps |
| Noise immunity | Low | Low | Medium | High |
| Common use | MCU-to-peripheral | MCU GPIO | PC/instrument ports | Industrial bus |

---

## Level Shifting

### When Level Shifting Is Required

Level shifting is required whenever two UART endpoints operate at different voltage standards. Common scenarios:

- **3.3 V MCU ↔ 5 V TTL device:** Bidirectional voltage translation
- **Any MCU ↔ RS-232 port:** Requires a dedicated RS-232 transceiver IC
- **Any MCU ↔ RS-485 bus:** Requires an RS-485 transceiver IC with direction control

### 3.3 V CMOS ↔ 5 V TTL Level Shifting

#### Option A: Resistor Voltage Divider (RX only, one direction)

A simple resistor divider can bring a 5 V signal down to 3.3 V for the MCU's RX pin. This is unidirectional — it only works for signals coming *into* the 3.3 V device.

```
5V_TxD ──┬── 10 kΩ ──┬── 3.3V_RxD
          │           │
         GND        20 kΩ
                      │
                     GND
```

Output voltage ≈ 5 V × (20k / 30k) = 3.33 V ✓

The 3.3 V MCU TxD can drive the 5 V device's RxD directly if the 5 V device's input threshold is ≤ 2.4 V (TTL-compatible inputs accept 3.3 V HIGH). If the device has CMOS inputs requiring VDD × 0.7 = 3.5 V, a level shifter is needed for TxD too.

#### Option B: BSS138 MOSFET Bidirectional Shifter

The classic open-drain bidirectional level shifter uses an N-channel MOSFET (BSS138 or similar):

```
3.3V ──────────────────── 5V
       │              │
      10kΩ           10kΩ
       │              │
3.3V_Signal ──[BSS138 Gate to 3.3V]── 5V_Signal
```

Modules such as the SparkFun BOB-12009 or Adafruit 757 implement four channels of this circuit. They are inexpensive and easy to use for low-to-medium baud rates (works well up to ~500 kbps).

#### Option C: Dedicated Level Shifter ICs

For higher speeds or cleaner signal integrity, dedicated ICs are preferred:

- **TXS0101 / TXS0108E** (Texas Instruments) — auto-direction sensing, up to 24 Mbps
- **74LVC1T45** — single-bit, direction pin, fast
- **SN74AVCH2T245** — two-bit, 1.2 V to 5.5 V translation

### MCU ↔ RS-232: MAX232 / MAX3232 Family

The most common RS-232 interface IC is the **MAX232** (5 V) or **MAX3232** (3.3 V). These ICs contain a charge pump to generate ±12 V from a single supply, plus two drivers and two receivers.

```
MCU (3.3V CMOS)                MAX3232              RS-232 Connector
─────────────────               ──────────────        ────────────────
TxD (3.3V) ─────── T1IN ──── T1OUT ──────────────── TxD (±12V)
RxD (3.3V) ─────── R1OUT ─── R1IN  ──────────────── RxD (±12V)
3.3V ──────────────── VCC
GND ────────────────── GND
                      C1+, C1−, C2+, C2−: 100 nF charge pump capacitors
```

Key wiring note: **C1, C2, C3, C4** are the charge-pump capacitors (100 nF ceramic, placed close to the IC). Omitting or incorrectly sizing these prevents the IC from generating the RS-232 voltages.

### MCU ↔ RS-485: MAX485 / SN75176 / SP485 Family

RS-485 transceivers bridge 3.3 V / 5 V CMOS logic to the differential A/B bus. Half-duplex operation requires a **Driver Enable (DE)** and **Receiver Enable (RE, active LOW)** pin, usually tied together and driven by a GPIO.

```
MCU                  MAX485               RS-485 Bus
───────────         ──────────            ──────────
TxD ──────── DI                 A ─────── Wire A
DE_GPIO ───── DE/RE             B ─────── Wire B
RxD ──────── RO
3.3V/5V ───── VCC
GND ─────────── GND
```

**DE/RE control:** DE HIGH = driver enabled (transmit mode); RE LOW = receiver enabled (receive mode). These are often the same GPIO pin (DE and /RE tied together). Set DE=HIGH before writing, DE=LOW after the last byte has fully shifted out.

**Propagation delay caution:** When switching from TX to RX, the last byte must completely leave the UART shift register before DE is de-asserted. Calculate the time for the last byte: `t = (bits_per_frame) / baud_rate`. For 8N1 at 9600 baud: 10 bits / 9600 = 1.04 ms.

---

## C/C++ Programming Examples

### Example 1: Configuring UART for RS-232 (Linux, POSIX termios)

This example opens a serial port connected via an RS-232 transceiver, configures it for 9600-8-N-1, and sends/receives data.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

/* Open and configure a serial port for RS-232 communication.
 * device: e.g. "/dev/ttyUSB0" (USB-to-RS232 adapter) or "/dev/ttyS0"
 * baud:   e.g. B9600, B115200 (termios constants)
 * Returns file descriptor on success, -1 on failure. */
int uart_open(const char *device, speed_t baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) {
        perror("uart_open: open");
        return -1;
    }

    /* Configure the port */
    struct termios tty;
    memset(&tty, 0, sizeof tty);

    if (tcgetattr(fd, &tty) != 0) {
        perror("uart_open: tcgetattr");
        close(fd);
        return -1;
    }

    /* Set baud rate (both input and output) */
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* 8 data bits, no parity, 1 stop bit (8N1) */
    tty.c_cflag &= ~PARENB;          /* No parity */
    tty.c_cflag &= ~CSTOPB;          /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;             /* 8 data bits */

    /* Disable hardware (RTS/CTS) flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Enable receiver, ignore modem status lines */
    tty.c_cflag |= CREAD | CLOCAL;

    /* Raw input — no special handling of received bytes */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  /* Disable XON/XOFF software flow ctrl */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    /* Raw output */
    tty.c_oflag &= ~OPOST;
    tty.c_oflag &= ~ONLCR;

    /* Non-canonical (raw) mode: read returns after VMIN bytes or VTIME×100ms */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_cc[VMIN]  = 0;   /* Non-blocking: return immediately */
    tty.c_cc[VTIME] = 10;  /* 1 second timeout (10 × 100ms) */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

int main(void)
{
    /* Open RS-232 port (connected via MAX232/MAX3232 transceiver) */
    int fd = uart_open("/dev/ttyUSB0", B9600);
    if (fd < 0) return EXIT_FAILURE;

    /* Transmit a command */
    const char *cmd = "HELLO\r\n";
    ssize_t written = write(fd, cmd, strlen(cmd));
    if (written < 0) {
        perror("write");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Sent %zd bytes via RS-232\n", written);

    /* Receive a response */
    char buf[256];
    memset(buf, 0, sizeof buf);
    ssize_t n = read(fd, buf, sizeof buf - 1);
    if (n > 0) {
        printf("Received %zd bytes: %s\n", n, buf);
    } else if (n == 0) {
        printf("Timeout — no data received\n");
    } else {
        perror("read");
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

**Build:** `gcc -o rs232_demo rs232_demo.c`

---

### Example 2: RS-485 Half-Duplex Direction Control (Linux + GPIO via libgpiod)

In RS-485, software must assert DE (Driver Enable) before transmitting and de-assert it after the last byte has cleared the UART shift register.

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <gpiod.h>   /* sudo apt install libgpiod-dev */

#define GPIO_CHIP   "gpiochip0"
#define DE_GPIO_PIN  17          /* BCM pin driving RS-485 DE/!RE */

/* Compute the time in microseconds for one UART frame at the given baud rate.
 * For 8N1: 1 start + 8 data + 1 stop = 10 bit-times. */
static unsigned long frame_time_us(int baud_rate)
{
    /* 10 bits per frame for 8N1 */
    return (unsigned long)(10 * 1000000UL / baud_rate);
}

int main(void)
{
    /* --- Open GPIO for DE control --- */
    struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) { perror("gpiod_chip_open"); return EXIT_FAILURE; }

    struct gpiod_line *de_line = gpiod_chip_get_line(chip, DE_GPIO_PIN);
    if (!de_line) { perror("gpiod_chip_get_line"); return EXIT_FAILURE; }

    if (gpiod_line_request_output(de_line, "rs485_de", 0) < 0) {
        perror("gpiod_line_request_output");
        return EXIT_FAILURE;
    }

    /* --- Open UART (connected to RS-485 transceiver DI/RO pins) --- */
    int fd = open("/dev/ttyAMA0", O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); return EXIT_FAILURE; }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);
    tty.c_cflag = CS8 | CREAD | CLOCAL;
    tty.c_iflag = 0;
    tty.c_oflag = 0;
    tty.c_lflag = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 10;
    tcsetattr(fd, TCSANOW, &tty);

    const char *msg   = "SENSOR_REQUEST\r\n";
    size_t      msglen = strlen(msg);
    int         baud   = 9600;

    /* --- Transmit sequence --- */
    /* 1. Assert DE (enable driver, disable receiver) */
    gpiod_line_set_value(de_line, 1);
    usleep(10); /* Short settling time for transceiver */

    /* 2. Write data */
    write(fd, msg, msglen);

    /* 3. Wait for all bytes to leave the shift register:
     *    time = (number of frames) × (bits per frame / baud)            */
    unsigned long tx_time_us = msglen * frame_time_us(baud);
    tcdrain(fd);            /* Block until kernel TX buffer is empty */
    usleep(tx_time_us);     /* Extra guard time for the last bit to finish */

    /* 4. De-assert DE (disable driver, enable receiver) */
    gpiod_line_set_value(de_line, 0);

    /* --- Receive sequence --- */
    char    rxbuf[256];
    memset(rxbuf, 0, sizeof rxbuf);
    ssize_t n = read(fd, rxbuf, sizeof rxbuf - 1);
    if (n > 0) {
        printf("RS-485 response (%zd bytes): %s\n", n, rxbuf);
    } else {
        printf("No response (timeout)\n");
    }

    close(fd);
    gpiod_line_release(de_line);
    gpiod_chip_close(chip);
    return EXIT_SUCCESS;
}
```

**Build:** `gcc -o rs485_demo rs485_demo.c -lgpiod`

---

### Example 3: Level Shifting Awareness — STM32 HAL UART (C)

When using STM32 HAL, the UART peripheral itself is voltage-agnostic — but pin assignment matters. This example shows configuring a UART with explicit notes on voltage level concerns.

```c
/* STM32 HAL UART — 3.3 V CMOS UART with RS-485 direction control
 * Target: STM32F4xx, using USART2 on PA2 (TX) / PA3 (RX)
 * RS-485 DE pin: PA4 (active HIGH)
 *
 * VOLTAGE NOTE:
 *   All GPIO on STM32F4 are 3.3V CMOS.
 *   If the far end is 5V TTL, check if the STM32 pin is 5V-tolerant
 *   (marked FT in the datasheet pinout table).
 *   PA2, PA3 on STM32F411 are FT — safe to receive 5V TTL signals.
 *   PA4 driving a MAX485 DE pin is fine: MAX485 accepts 3.3V HIGH as logic HIGH.
 */

#include "stm32f4xx_hal.h"

UART_HandleTypeDef huart2;

#define RS485_DE_PIN   GPIO_PIN_4
#define RS485_DE_PORT  GPIOA

static void RS485_Transmit_Enable(void) {
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);
}

static void RS485_Receive_Enable(void) {
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);
}

void UART2_Init(void)
{
    huart2.Instance        = USART2;
    huart2.Init.BaudRate   = 9600;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits   = UART_STOPBITS_1;
    huart2.Init.Parity     = UART_PARITY_NONE;
    huart2.Init.Mode       = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl  = UART_HWCONTROL_NONE;
    HAL_UART_Init(&huart2);

    /* DE pin: output, initially LOW (receive mode) */
    GPIO_InitTypeDef gpio = {0};
    gpio.Pin   = RS485_DE_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Pull  = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(RS485_DE_PORT, &gpio);
    RS485_Receive_Enable();
}

HAL_StatusTypeDef RS485_Send(const uint8_t *data, uint16_t len)
{
    RS485_Transmit_Enable();
    HAL_StatusTypeDef status = HAL_UART_Transmit(&huart2, data, len, 100);

    /* Wait for TC (Transmission Complete) flag — last bit is on the wire */
    while (__HAL_UART_GET_FLAG(&huart2, UART_FLAG_TC) == RESET) {}

    RS485_Receive_Enable();
    return status;
}

HAL_StatusTypeDef RS485_Receive(uint8_t *buf, uint16_t len, uint32_t timeout_ms)
{
    return HAL_UART_Receive(&huart2, buf, len, timeout_ms);
}
```

---

### Example 4: Checking Voltage Compatibility at Runtime (Microcontroller C)

A utility pattern for embedded code that reads a configuration register to determine which voltage standard the board uses and applies the corresponding baud-rate prescaler limit:

```c
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    VOLTAGE_STD_TTL_5V   = 0,
    VOLTAGE_STD_CMOS_3V3 = 1,
    VOLTAGE_STD_CMOS_1V8 = 2,
    VOLTAGE_STD_RS232    = 3,
    VOLTAGE_STD_RS485    = 4,
} VoltageStandard;

/* Returns the maximum safe baud rate for the given voltage standard
 * given the board's cable length or topology constraints. */
uint32_t max_safe_baud(VoltageStandard std, uint32_t cable_length_m)
{
    switch (std) {
    case VOLTAGE_STD_TTL_5V:
    case VOLTAGE_STD_CMOS_3V3:
    case VOLTAGE_STD_CMOS_1V8:
        /* Short-range single-ended: limit by MCU peripheral, not cable */
        if (cable_length_m > 1) return 0; /* Not recommended > 1m */
        return 10000000UL; /* Up to 10 Mbps possible over < 1m */

    case VOLTAGE_STD_RS232:
        /* RS-232: 1 bit = 100 pF/m × cable_length must stay < 2500 pF */
        if (cable_length_m > 15) return 0;
        /* Rough approximation: max baud decreases linearly with length */
        return (uint32_t)(20000UL * 15UL / (cable_length_m ? cable_length_m : 1));

    case VOLTAGE_STD_RS485:
        /* RS-485: baud × length product is approximately constant.
         * 100 Mbps·m product rule: 100kbps @ 1000m, 1Mbps @ 100m, etc. */
        if (cable_length_m == 0) cable_length_m = 1;
        uint32_t baud = (uint32_t)(100000000UL / cable_length_m);
        return (baud > 10000000UL) ? 10000000UL : baud;
    }
    return 0;
}

/* Check whether two endpoints are directly connectable without level shifting */
bool requires_level_shift(VoltageStandard a, VoltageStandard b)
{
    /* Same standard: always compatible */
    if (a == b) return false;

    /* TTL 5V and CMOS 3.3V: technically need shifting;
     * 5V TTL OUT → 3.3V CMOS IN is overvoltage unless pin is 5V tolerant */
    if ((a == VOLTAGE_STD_TTL_5V  && b == VOLTAGE_STD_CMOS_3V3) ||
        (a == VOLTAGE_STD_CMOS_3V3 && b == VOLTAGE_STD_TTL_5V)) {
        return true; /* Verify 5V tolerance in datasheet before skipping shift */
    }

    /* RS-232 and RS-485 always need a transceiver IC */
    if (a == VOLTAGE_STD_RS232 || b == VOLTAGE_STD_RS232) return true;
    if (a == VOLTAGE_STD_RS485 || b == VOLTAGE_STD_RS485) return true;

    /* 1.8V CMOS with anything else needs shifting */
    if (a == VOLTAGE_STD_CMOS_1V8 || b == VOLTAGE_STD_CMOS_1V8) return true;

    return false;
}
```

---

## Rust Programming Examples

### Example 5: RS-232 UART Communication (Rust, serialport crate)

```toml
# Cargo.toml
[dependencies]
serialport = "4"
```

```rust
use std::io::{self, Read, Write};
use std::time::Duration;
use serialport::{self, DataBits, FlowControl, Parity, StopBits};

/// Open an RS-232 serial port and configure it for 8N1 at the specified baud rate.
/// The physical RS-232 transceiver (MAX232/MAX3232) is transparent to this code.
fn open_rs232(device: &str, baud_rate: u32) -> Result<Box<dyn serialport::SerialPort>, serialport::Error> {
    serialport::new(device, baud_rate)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(1000))
        .open()
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // On Linux: /dev/ttyUSB0 for USB-RS232 adapters, /dev/ttyS0 for native ports
    // On Windows: COM3, COM4, etc.
    let port_name = "/dev/ttyUSB0";
    let baud_rate  = 9600;

    println!("Opening RS-232 port '{}' at {} baud (8N1)", port_name, baud_rate);
    let mut port = open_rs232(port_name, baud_rate)?;

    // ---Transmit---
    let command = b"GET_STATUS\r\n";
    port.write_all(command)?;
    port.flush()?;
    println!("Sent {} bytes", command.len());

    // ---Receive (up to 256 bytes, 1 second timeout)---
    let mut response = vec![0u8; 256];
    match port.read(&mut response) {
        Ok(n) if n > 0 => {
            let text = String::from_utf8_lossy(&response[..n]);
            println!("Received {} bytes: {}", n, text.trim());
        }
        Ok(_) => println!("Timeout — no data received"),
        Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
            println!("Timeout — no data received");
        }
        Err(e) => return Err(e.into()),
    }

    Ok(())
}
```

---

### Example 6: RS-485 Half-Duplex with GPIO Direction Control (Rust, Linux)

```toml
# Cargo.toml
[dependencies]
serialport = "4"
gpiod = "0.4"      # Or use the `gpio-cdev` crate
```

```rust
use std::io::{Read, Write};
use std::thread;
use std::time::Duration;
use serialport::{DataBits, FlowControl, Parity, StopBits};
use gpiod::{Chip, Options, EdgeDetect};

/// RS-485 transceiver direction
#[derive(Debug, Clone, Copy, PartialEq)]
enum Rs485Direction {
    Transmit,
    Receive,
}

struct Rs485Port {
    serial:    Box<dyn serialport::SerialPort>,
    de_chip:   Chip,
    de_line:   u32,
    baud_rate: u32,
}

impl Rs485Port {
    fn new(serial_dev: &str, baud_rate: u32, gpio_chip: &str, de_pin: u32) -> Result<Self, Box<dyn std::error::Error>> {
        let serial = serialport::new(serial_dev, baud_rate)
            .data_bits(DataBits::Eight)
            .parity(Parity::None)
            .stop_bits(StopBits::One)
            .flow_control(FlowControl::None)
            .timeout(Duration::from_millis(500))
            .open()?;

        let de_chip = Chip::new(gpio_chip)?;

        Ok(Rs485Port { serial, de_chip, de_line: de_pin, baud_rate })
    }

    /// Set the DE (Driver Enable) GPIO to control bus direction.
    fn set_direction(&mut self, dir: Rs485Direction) -> Result<(), Box<dyn std::error::Error>> {
        let opts = Options::output([self.de_line]).consumer("rs485_de");
        let lines = self.de_chip.request_lines(opts)?;
        let value = match dir {
            Rs485Direction::Transmit => 1u8,
            Rs485Direction::Receive  => 0u8,
        };
        lines.set_values([value])?;
        Ok(())
    }

    /// Calculate how long (microseconds) it takes to transmit `n_bytes` at the configured baud rate.
    /// Assumes 8N1 framing (10 bits per frame).
    fn tx_duration_us(&self, n_bytes: usize) -> u64 {
        (n_bytes as u64 * 10 * 1_000_000) / self.baud_rate as u64
    }

    /// Send a message on the RS-485 bus, handling direction control automatically.
    fn send(&mut self, data: &[u8]) -> Result<(), Box<dyn std::error::Error>> {
        // 1. Enable driver
        self.set_direction(Rs485Direction::Transmit)?;
        thread::sleep(Duration::from_micros(10)); // Transceiver settling time

        // 2. Transmit
        self.serial.write_all(data)?;
        self.serial.flush()?;

        // 3. Wait for last byte to fully leave the shift register
        let guard_us = self.tx_duration_us(data.len()) + 100; // 100 µs extra guard
        thread::sleep(Duration::from_micros(guard_us));

        // 4. Switch back to receive mode
        self.set_direction(Rs485Direction::Receive)?;
        Ok(())
    }

    /// Receive up to `max_len` bytes with a given timeout.
    fn receive(&mut self, max_len: usize) -> Result<Vec<u8>, Box<dyn std::error::Error>> {
        let mut buf = vec![0u8; max_len];
        match self.serial.read(&mut buf) {
            Ok(n) => {
                buf.truncate(n);
                Ok(buf)
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => Ok(vec![]),
            Err(e) => Err(e.into()),
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut port = Rs485Port::new(
        "/dev/ttyAMA0",   // UART device connected to RS-485 transceiver
        9600,
        "gpiochip0",      // GPIO chip
        17,               // BCM pin 17 → DE/!RE on MAX485
    )?;

    // Send a Modbus-RTU style request (simplified, no real CRC here)
    let request = b"\x01\x03\x00\x00\x00\x02\xC4\x0B";
    println!("Sending RS-485 request ({} bytes)...", request.len());
    port.send(request)?;

    // Receive response
    let response = port.receive(64)?;
    if response.is_empty() {
        println!("No response received (timeout)");
    } else {
        println!("Response ({} bytes): {:02X?}", response.len(), response);
    }

    Ok(())
}
```

---

### Example 7: Voltage Standard Selection and Validation (Rust)

A compile-time-safe Rust abstraction that encodes voltage standard constraints:

```rust
use std::fmt;

/// Represents a UART voltage standard with its key electrical properties.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum VoltageStandard {
    Ttl5V,
    Cmos3V3,
    Cmos1V8,
    Rs232,
    Rs485,
}

impl VoltageStandard {
    /// Maximum recommended cable length in meters for this standard.
    pub fn max_cable_length_m(self) -> u32 {
        match self {
            VoltageStandard::Ttl5V   => 1,
            VoltageStandard::Cmos3V3 => 1,
            VoltageStandard::Cmos1V8 => 1,
            VoltageStandard::Rs232   => 15,
            VoltageStandard::Rs485   => 1200,
        }
    }

    /// Maximum data rate in bits per second (at minimum cable length).
    pub fn max_baud_rate(self) -> u32 {
        match self {
            VoltageStandard::Ttl5V   => 10_000_000,
            VoltageStandard::Cmos3V3 => 10_000_000,
            VoltageStandard::Cmos1V8 =>  5_000_000,
            VoltageStandard::Rs232   =>  1_000_000,
            VoltageStandard::Rs485   => 10_000_000,
        }
    }

    /// Whether this standard uses differential signaling.
    pub fn is_differential(self) -> bool {
        matches!(self, VoltageStandard::Rs485)
    }

    /// Whether level shifting hardware is needed to interface with `other`.
    pub fn needs_level_shift_to(self, other: VoltageStandard) -> bool {
        if self == other {
            return false;
        }
        // RS-232 and RS-485 always need a transceiver
        if matches!(self, VoltageStandard::Rs232 | VoltageStandard::Rs485)
            || matches!(other, VoltageStandard::Rs232 | VoltageStandard::Rs485)
        {
            return true;
        }
        // 1.8 V CMOS paired with 3.3 V or 5 V needs shifting
        if matches!(self, VoltageStandard::Cmos1V8)
            || matches!(other, VoltageStandard::Cmos1V8)
        {
            return true;
        }
        // 5V TTL driving 3.3V CMOS input is an overvoltage risk
        matches!(
            (self, other),
            (VoltageStandard::Ttl5V, VoltageStandard::Cmos3V3)
            | (VoltageStandard::Cmos3V3, VoltageStandard::Ttl5V)
        )
    }

    /// Maximum safe baud rate given cable length, accounting for RS-485 baud×length product.
    pub fn max_baud_at_length(self, cable_m: u32) -> u32 {
        if cable_m == 0 {
            return self.max_baud_rate();
        }
        match self {
            VoltageStandard::Rs485 => {
                // ~100 Mbps·m product rule
                let baud = 100_000_000u64 / cable_m as u64;
                baud.min(self.max_baud_rate() as u64) as u32
            }
            VoltageStandard::Rs232 => {
                if cable_m > 15 { 0 } else { 20_000 * 15 / cable_m }
            }
            _ => {
                if cable_m > 1 { 0 } else { self.max_baud_rate() }
            }
        }
    }
}

impl fmt::Display for VoltageStandard {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            VoltageStandard::Ttl5V   => write!(f, "TTL 5V"),
            VoltageStandard::Cmos3V3 => write!(f, "CMOS 3.3V"),
            VoltageStandard::Cmos1V8 => write!(f, "CMOS 1.8V"),
            VoltageStandard::Rs232   => write!(f, "RS-232"),
            VoltageStandard::Rs485   => write!(f, "RS-485"),
        }
    }
}

fn main() {
    let standards = [
        VoltageStandard::Ttl5V,
        VoltageStandard::Cmos3V3,
        VoltageStandard::Cmos1V8,
        VoltageStandard::Rs232,
        VoltageStandard::Rs485,
    ];

    println!("=== Voltage Standard Properties ===\n");
    for std in &standards {
        println!(
            "{:<12} | Max cable: {:>5}m | Max baud: {:>10} bps | Differential: {}",
            std.to_string(),
            std.max_cable_length_m(),
            std.max_baud_rate(),
            if std.is_differential() { "Yes" } else { "No " }
        );
    }

    println!("\n=== Level Shift Requirements ===\n");
    let pairs = [
        (VoltageStandard::Ttl5V,   VoltageStandard::Cmos3V3),
        (VoltageStandard::Cmos3V3, VoltageStandard::Cmos1V8),
        (VoltageStandard::Cmos3V3, VoltageStandard::Rs232),
        (VoltageStandard::Cmos3V3, VoltageStandard::Rs485),
        (VoltageStandard::Rs232,   VoltageStandard::Rs485),
    ];
    for (a, b) in &pairs {
        println!(
            "{} ↔ {}: level shift {}",
            a, b,
            if a.needs_level_shift_to(*b) { "REQUIRED" } else { "not needed" }
        );
    }

    println!("\n=== RS-485 Baud Rate vs. Cable Length ===\n");
    let lengths = [10u32, 100, 300, 600, 1000, 1200];
    for &len in &lengths {
        println!(
            "{:>5} m → max {:>9} bps",
            len,
            VoltageStandard::Rs485.max_baud_at_length(len)
        );
    }
}
```

**Expected output:**
```
=== Voltage Standard Properties ===

TTL 5V       | Max cable:     1m | Max baud:   10000000 bps | Differential: No 
CMOS 3.3V    | Max cable:     1m | Max baud:   10000000 bps | Differential: No 
CMOS 1.8V    | Max cable:     1m | Max baud:    5000000 bps | Differential: No 
RS-232       | Max cable:    15m | Max baud:    1000000 bps | Differential: No 
RS-485       | Max cable:  1200m | Max baud:   10000000 bps | Differential: Yes

=== Level Shift Requirements ===

TTL 5V ↔ CMOS 3.3V: level shift REQUIRED
CMOS 3.3V ↔ CMOS 1.8V: level shift REQUIRED
CMOS 3.3V ↔ RS-232: level shift REQUIRED
CMOS 3.3V ↔ RS-485: level shift REQUIRED
RS-232 ↔ RS-485: level shift REQUIRED

=== RS-485 Baud Rate vs. Cable Length ===

   10 m → max  10000000 bps
  100 m → max   1000000 bps
  300 m → max    333333 bps
  600 m → max    166666 bps
 1000 m → max    100000 bps
 1200 m → max     83333 bps
```

---

## Common Pitfalls and Best Practices

### Pitfall 1: Connecting RS-232 to a Bare MCU GPIO

RS-232 signals can reach ±15 V. Connecting them directly to a 3.3 V MCU GPIO will immediately destroy the GPIO or the entire chip. **Always use a MAX232/MAX3232 or equivalent transceiver IC**.

### Pitfall 2: Forgetting DE De-Assertion Timing (RS-485)

Calling `DE = 0` immediately after `write()` returns will cut off the last stop bit. The kernel write buffer may be empty, but the UART shift register is still clocking out bits. Use `tcdrain()` (Linux) or wait for the TC (Transmission Complete) flag (STM32 HAL) *before* de-asserting DE.

### Pitfall 3: Assuming All MCU Pins Are 5 V Tolerant

Many STM32, nRF52, and other ARM MCUs have a mix of 5 V-tolerant and non-tolerant pins. Connecting a 5 V TTL TxD to a non-tolerant pin will cause latch-up or permanent damage. **Always check the specific pin's tolerance in the device datasheet** — never assume.

### Pitfall 4: Missing RS-485 Termination

Without 120 Ω termination at both ends of the cable, high-frequency signals reflect and cause data corruption at baud rates above ~19200 on cables longer than a few meters. Add termination resistors, especially during initial bring-up.

### Pitfall 5: Floating RS-485 Bus

When no device is driving the bus, the A and B wires float, and receivers may interpret noise as data. Add fail-safe biasing: 560 Ω pull-up on B (to VCC), 560 Ω pull-down on A (to GND). Many RS-485 transceivers (SN65HVD3082, MAX3485E) have built-in fail-safe receivers.

### Pitfall 6: Ground Reference in RS-232

RS-232 is single-ended: signal voltages are measured relative to signal ground. If two RS-232 devices have different ground potentials (ground loops), communication fails or noise is induced. In industrial environments with long RS-232 runs, this is a common cause of intermittent errors — another reason RS-485 (differential) is preferred.

### Best Practice: Scope Before You Debug

When a UART link doesn't work, the first step is to probe the signal with an oscilloscope:

1. Verify the idle state is correct (HIGH for TTL/CMOS, negative voltage for RS-232, differential for RS-485)
2. Measure the actual high and low voltages
3. Check for the start bit, data bits, and stop bit
4. Verify baud rate by measuring one bit period

If you don't have a scope, a logic analyzer with protocol decoding is the next best option.

---

## Summary

UART as a protocol is independent of the physical voltage layer. Choosing the right voltage standard is a system-level decision that affects hardware cost, reliability, cable length, and the number of nodes on a bus.

**TTL (5 V)** is the legacy standard for short-range MCU-to-peripheral communication on 5 V systems. **CMOS (3.3 V and 1.8 V)** is the modern standard for virtually all contemporary microcontrollers and SoCs — it is lower power but stricter about overvoltage on inputs. **RS-232** extends UART to longer cables (up to 15 m) and noisy environments through higher voltage swings and negative-logic signaling, at the cost of requiring a dedicated level-shifting IC. **RS-485** is the choice for industrial, multi-drop, long-distance communication — its differential signaling provides excellent noise immunity, supports buses of up to 256 nodes, and can run reliably over 1200 m of twisted-pair cable.

Level shifting is mandatory whenever endpoints use different standards. Resistor dividers handle simple unidirectional CMOS translation; dedicated ICs (TXS0108, BSS138 modules) cover bidirectional translation; MAX232/MAX3232 handles RS-232; and MAX485/SN65HVD transceivers handle RS-485 with direction control via a DE GPIO pin.

In software, the key standard-specific concern is **RS-485 direction control**: the driver enable GPIO must be asserted before writing and de-asserted only after the hardware shift register has fully clocked out the last stop bit, using `tcdrain()` in Linux or polling the TC flag in STM32 HAL. All other software-layer UART code (baud rate, framing, stop bits) is identical regardless of the physical voltage standard in use.

---

*End of Document — UART Topic 06: Voltage Levels and Logic*