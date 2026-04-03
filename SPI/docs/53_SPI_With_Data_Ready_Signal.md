# 53. SPI with Data Ready Signal

**Architecture & Theory**
- ASCII diagrams of minimal DRDY wiring, dual DRDY+BUSY wiring, and wired-OR multi-slave configurations
- Signal timing diagrams showing the exact handshake sequence
- Table of real-world devices that use DRDY (ADS1256, ICM-42688, MS5611, nRF24L01, etc.)

**Four Flow Control Strategies** with trade-offs: interrupt-driven, polling with timeout, DMA+interrupt, and RTOS event queue.

**C/C++ Examples (3 implementations)**
1. **STM32 bare-metal** — Full ADS1256 driver with EXTI ISR, `wait_for_drdy()` with timeout fallback, sign-extended 24-bit reads, and precise µs delays via DWT
2. **Linux userspace** — `spidev` + `libgpiod` using `gpiod_line_event_wait()` (epoll-backed, no busy polling), multi-transfer `SPI_IOC_MESSAGE` with `delay_usecs`
3. **FreeRTOS C++ class** — Task with semaphore-based ISR notification, overrun detection, and queue-backed sample buffering

**Rust Examples (3 implementations)**
1. **`embedded-hal` 1.0** — `no_std` generic driver over any SPI+GPIO, portable across all MCU HALs
2. **`rppal` (Raspberry Pi)** — Both polling mode and interrupt-driven mode using `Condvar`/`Mutex` for thread-safe signalling
3. **Embassy async** — Fully cooperative `async/await` using `ExtiInput::wait_for_falling_edge()` with `with_timeout()`

**Edge Cases** section covers the race condition between DRDY check and CS assertion, glitch filtering, missed edges, polarity inversion, and wired-OR multi-slave pitfalls.

## Using Additional GPIO for Slave-Initiated Communication and Flow Control

---

## Table of Contents

1. [Introduction](#introduction)
2. [Motivation and Use Cases](#motivation-and-use-cases)
3. [Hardware Architecture](#hardware-architecture)
4. [Signal Timing and Protocol Design](#signal-timing-and-protocol-design)
5. [Flow Control Strategies](#flow-control-strategies)
6. [Implementation in C/C++](#implementation-in-cc)
7. [Implementation in Rust](#implementation-in-rust)
8. [Edge Cases and Pitfalls](#edge-cases-and-pitfalls)
9. [Summary](#summary)

---

## Introduction

Standard SPI (Serial Peripheral Interface) is a **master-driven protocol**: the master controls the clock (SCLK), chip select (CS/SS), and all data transfers. The slave has no mechanism to signal the master that it has data ready to send or that it cannot accept data at a given moment.

The **Data Ready (DRDY)** pattern — sometimes also called **Data Available (DAVA)**, **Interrupt Request (IRQ)**, or **Flow Control (FC)** — solves this by adding one or more additional GPIO lines that allow the slave to participate actively in initiating and gating communication.

This is not a change to the SPI electrical standard but rather a **protocol convention** layered on top of the four standard SPI lines:

| Signal | Direction | Function |
|--------|-----------|----------|
| SCLK   | Master → Slave | Clock |
| MOSI   | Master → Slave | Data out |
| MISO   | Slave → Master | Data in |
| CS/SS  | Master → Slave | Chip Select |
| **DRDY** | **Slave → Master** | **Data ready / flow control** |
| **BUSY** (optional) | **Slave → Master** | **Slave processing, hold off** |

---

## Motivation and Use Cases

### Why Standard SPI Is Insufficient

In a basic SPI exchange, the master must either:
- **Poll continuously** — waste CPU cycles constantly querying the slave for new data.
- **Poll periodically** — risk missing time-critical data between poll intervals.
- **Use a fixed schedule** — only works if the slave's data production rate is perfectly predictable.

None of these are satisfactory for devices that produce data **asynchronously** or in **bursts**.

### Common Devices Using DRDY

| Device Type | Example Parts | Why DRDY is Needed |
|---|---|---|
| ADC / DAC | ADS1256, ADS8688, MCP3914 | Conversion takes variable time |
| IMU / Accelerometer | ADIS16xxx, ICM-42688 | Sample ready after integration period |
| Barometric Sensor | MS5611, LPS22HB | Measurement conversion latency |
| GNSS modules | uBlox NEO series | Packet ready notification |
| Display controllers | ILI9341 (TE pin) | Tearing effect prevention sync |
| Wireless modules | nRF24L01, WINC1500 | Incoming packet notification |
| Fingerprint sensors | FPC1020 | Scan complete indication |

---

## Hardware Architecture

### Minimal DRDY Configuration

```
  MASTER                         SLAVE
  ┌─────────────────┐            ┌────────────────────┐
  │          SCLK ──┼────────────┼─► SCLK             │
  │          MOSI ──┼────────────┼─► MOSI             │
  │          MISO ◄─┼────────────┼── MISO             │
  │            CS ──┼────────────┼─► CS               │
  │                 │            │                    │
  │  GPIO_IN  DRDY ◄┼────────────┼── DRDY  GPIO_OUT   │
  │  (with IRQ)     │            │                    │
  └─────────────────┘            └────────────────────┘
```

The DRDY line is typically:
- **Active LOW** — the slave pulls the line LOW when data is available (open-drain with pull-up resistor on the line, typically 4.7 kΩ–10 kΩ to VCC).
- Wired to a GPIO with **interrupt capability** on the master so it can respond asynchronously without polling.

### Extended Configuration with BUSY

```
  MASTER                         SLAVE
  ┌─────────────────┐            ┌────────────────────┐
  │          SCLK ──┼────────────┼─► SCLK             │
  │          MOSI ──┼────────────┼─► MOSI             │
  │          MISO ◄─┼────────────┼── MISO             │
  │            CS ──┼────────────┼─► CS               │
  │                 │            │                    │
  │  GPIO_IN  DRDY ◄┼────────────┼── DRDY  GPIO_OUT   │
  │  GPIO_IN  BUSY ◄┼────────────┼── BUSY  GPIO_OUT   │
  └─────────────────┘            └────────────────────┘
```

- **DRDY** — slave signals "I have data for you to read."
- **BUSY** — slave signals "I am processing your last command, do not start a new transaction."

### Multi-Slave Wired-OR DRDY

When multiple slaves share a DRDY line (open-drain), any slave asserting DRDY wakes the master, which then polls each slave's status register to identify the source:

```
  MASTER                          SLAVE A
  ┌──────────────┐    4.7kΩ      ┌─────────────┐
  │              │    ┌──VCC     │             │
  │  DRDY GPIO ◄─┼────┤          │ DRDY_A ─────┤
  │              │    └──────────┼─────────────┘
  │              │               │
  │              │               SLAVE B
  │              │              ┌─────────────┐
  │              │              │             │
  │              └──────────────┼── DRDY_B    │
  │                             └─────────────┘
  └──────────────┘
```

---

## Signal Timing and Protocol Design

### Basic DRDY Handshake Sequence

```
CS   ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___________________╱‾‾‾‾‾‾‾‾
DRDY ‾‾‾‾‾‾‾‾╲___________╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
             ↑           ↑
             Slave sets  Master reads on
             DRDY LOW    falling edge of DRDY

SCLK ____________________╱‾╲╱‾╲╱‾╲╱‾╲╱‾╲╱‾╲╱‾╲╱‾╲___
MISO ____________________╱════════════DATA═══════════
```

**Key Timing Rules:**
1. Master must **not** assert CS until DRDY is asserted (active LOW).
2. Master asserts CS, then begins clocking.
3. Slave de-asserts DRDY after the first clock edge (or after CS assertion, device-specific).
4. Master de-asserts CS after transfer completes.
5. Slave prepares next data and reasserts DRDY if more data is ready.

### DRDY-with-BUSY Timing

```
BUSY  ‾‾‾‾‾‾╲___________╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾
CMD   ────────[CMD_BYTES]──────────────────────────────
DRDY  ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___________╱‾‾‾‾‾‾‾‾‾
                                ↑           ↑
                                Data ready  Master reads
```

**Sequence:**
1. Master sends command → slave asserts BUSY immediately.
2. Slave processes command (ADC conversion, computation, etc.).
3. Slave de-asserts BUSY and asserts DRDY.
4. Master detects DRDY interrupt, begins read transaction.

---

## Flow Control Strategies

### Strategy 1: Interrupt-Driven (Recommended)

The master registers an ISR (Interrupt Service Routine) on the DRDY GPIO falling edge. The ISR signals a semaphore or sets a flag. A task or main loop checks the flag and initiates the SPI read.

**Advantages:** Zero polling overhead, immediate response.  
**Disadvantages:** ISR complexity, need for thread-safe signaling.

### Strategy 2: Polling with Timeout

The master polls the DRDY GPIO in a loop with a maximum timeout. Simpler but wastes CPU cycles.

**Advantages:** Simple, no ISR required.  
**Disadvantages:** Burns CPU, latency depends on poll rate.

### Strategy 3: DMA + Interrupt

The DRDY interrupt triggers DMA-based SPI transfer directly without CPU involvement for the data movement. Only a completion interrupt wakes the CPU.

**Advantages:** Maximum efficiency.  
**Disadvantages:** Complex setup, hardware-dependent.

### Strategy 4: Event Queue / Message Passing (RTOS)

The DRDY ISR posts an event to an OS queue. A dedicated SPI task blocks on the queue and handles all transfers. Clean separation of interrupt context from application logic.

---

## Implementation in C/C++

### Platform Context

The examples below target a **bare-metal ARM Cortex-M** (STM32-style) environment and a **Linux userspace** (using sysfs/gpiod + spidev). The patterns apply universally.

---

### 1. STM32 Bare-Metal C — Interrupt-Driven DRDY with ADS1256 ADC

```c
/**
 * spi_drdy.h
 * SPI + DRDY pattern for ADS1256 24-bit ADC
 * DRDY: active LOW, triggers EXTI interrupt on falling edge
 */
#ifndef SPI_DRDY_H
#define SPI_DRDY_H

#include <stdint.h>
#include <stdbool.h>

/* Pin configuration (adapt to your board) */
#define DRDY_GPIO_PORT      GPIOA
#define DRDY_GPIO_PIN       GPIO_PIN_0
#define DRDY_IRQn           EXTI0_IRQn

#define CS_GPIO_PORT        GPIOA
#define CS_GPIO_PIN         GPIO_PIN_4

/* Timeout for polling fallback (ms) */
#define DRDY_TIMEOUT_MS     500U

/* ADS1256 commands */
#define ADS1256_CMD_RDATA   0x01
#define ADS1256_CMD_RDATAC  0x03
#define ADS1256_CMD_SDATAC  0x0F
#define ADS1256_CMD_RREG    0x10
#define ADS1256_CMD_WREG    0x50
#define ADS1256_CMD_RESET   0xFE

typedef struct {
    volatile bool drdy_flag;    /* Set by ISR, cleared by consumer */
    int32_t       last_sample;  /* Latest ADC reading */
    uint32_t      overrun_count;/* ISR fired before last read complete */
} SpiDrdy_State_t;

extern SpiDrdy_State_t g_drdy_state;

/* Public API */
void    spi_drdy_init(void);
bool    spi_drdy_wait(uint32_t timeout_ms);
int32_t ads1256_read_sample(void);
void    ads1256_set_channel(uint8_t mux);
void    ads1256_start_continuous(void);
void    ads1256_stop_continuous(void);

#endif /* SPI_DRDY_H */
```

```c
/**
 * spi_drdy.c
 * Implementation — STM32 HAL-based, adaptable to other MCUs
 */
#include "spi_drdy.h"
#include "stm32f4xx_hal.h"
#include <string.h>

/* Assume SPI handle is defined elsewhere */
extern SPI_HandleTypeDef hspi1;

SpiDrdy_State_t g_drdy_state = {0};

/* ─────────────────────────────────────────────
 * Internal helpers
 * ───────────────────────────────────────────── */

static inline void cs_assert(void) {
    HAL_GPIO_WritePin(CS_GPIO_PORT, CS_GPIO_PIN, GPIO_PIN_RESET);
}

static inline void cs_deassert(void) {
    HAL_GPIO_WritePin(CS_GPIO_PORT, CS_GPIO_PIN, GPIO_PIN_SET);
}

static inline bool drdy_is_low(void) {
    return HAL_GPIO_ReadPin(DRDY_GPIO_PORT, DRDY_GPIO_PIN) == GPIO_PIN_RESET;
}

/**
 * Blocking delay in microseconds.
 * ADS1256 requires t6 >= 6.5 * CLKIN period (typically ~1.6µs at 7.68MHz).
 * Using DWT cycle counter for precision.
 */
static void delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000U);
    while ((DWT->CYCCNT - start) < cycles);
}

static HAL_StatusTypeDef spi_txrx_byte(uint8_t tx, uint8_t *rx) {
    return HAL_SPI_TransmitReceive(&hspi1, &tx, rx, 1, HAL_MAX_DELAY);
}

/* ─────────────────────────────────────────────
 * GPIO / EXTI Initialization
 * ───────────────────────────────────────────── */

void spi_drdy_init(void) {
    /* Enable DWT for µs delays */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL  |= DWT_CTRL_CYCCNTENA_Msk;

    /* Configure DRDY as input with pull-up (active LOW) */
    GPIO_InitTypeDef gpio = {
        .Pin  = DRDY_GPIO_PIN,
        .Mode = GPIO_MODE_IT_FALLING,   /* Interrupt on falling edge */
        .Pull = GPIO_PULLUP,
    };
    HAL_GPIO_Init(DRDY_GPIO_PORT, &gpio);

    /* Configure CS as push-pull output, deassert */
    gpio.Pin  = CS_GPIO_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_PP;
    gpio.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(CS_GPIO_PORT, &gpio);
    cs_deassert();

    /* Enable EXTI interrupt */
    HAL_NVIC_SetPriority(DRDY_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(DRDY_IRQn);

    memset(&g_drdy_state, 0, sizeof(g_drdy_state));
}

/* ─────────────────────────────────────────────
 * ISR — Called on DRDY falling edge
 * ───────────────────────────────────────────── */

/**
 * EXTI0_IRQHandler must call HAL_GPIO_EXTI_IRQHandler(DRDY_GPIO_PIN),
 * which eventually calls HAL_GPIO_EXTI_Callback.
 */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (GPIO_Pin == DRDY_GPIO_PIN) {
        if (g_drdy_state.drdy_flag) {
            /* Previous sample not yet consumed — overrun */
            g_drdy_state.overrun_count++;
        }
        g_drdy_state.drdy_flag = true;

        /* Optional: signal RTOS semaphore here instead of flag */
        /* osSemaphoreRelease(drdy_sem); */
    }
}

/* ─────────────────────────────────────────────
 * Wait for DRDY assertion
 * ───────────────────────────────────────────── */

/**
 * spi_drdy_wait() — Block until DRDY is asserted (flag set by ISR)
 * or timeout expires.
 *
 * @param timeout_ms  Maximum wait in milliseconds (0 = poll only DRDY pin)
 * @return            true if DRDY asserted, false on timeout
 */
bool spi_drdy_wait(uint32_t timeout_ms) {
    uint32_t start = HAL_GetTick();

    /* Check ISR flag first (may have been set before we called) */
    if (g_drdy_state.drdy_flag) {
        return true;
    }

    /* Poll the flag until set or timeout */
    while (!g_drdy_state.drdy_flag) {
        if ((HAL_GetTick() - start) >= timeout_ms) {
            /* Fallback: direct pin read (handles case where edge was missed) */
            return drdy_is_low();
        }
    }
    return true;
}

/* ─────────────────────────────────────────────
 * ADS1256 — Read single sample (single-shot mode)
 * ───────────────────────────────────────────── */

/**
 * ads1256_read_sample() — Wait for DRDY then read 24-bit ADC result.
 *
 * ADS1256 datasheet timing:
 *   - t6: Wait >= 6.5 * tCLKIN after CS assertion before RDATA command
 *   - t11: Wait 50 * tCLKIN after RDATA before clocking data (at 7.68MHz ≈ 6.5µs)
 *
 * Returns the signed 24-bit sample sign-extended to int32_t, or INT32_MIN on error.
 */
int32_t ads1256_read_sample(void) {
    if (!spi_drdy_wait(DRDY_TIMEOUT_MS)) {
        return INT32_MIN;  /* Timeout — slave did not respond */
    }

    /* Clear the flag atomically BEFORE asserting CS */
    __disable_irq();
    g_drdy_state.drdy_flag = false;
    __enable_irq();

    uint8_t rx[3];
    uint8_t dummy = 0xFF;

    cs_assert();
    delay_us(2);  /* t6 */

    /* Send RDATA command */
    spi_txrx_byte(ADS1256_CMD_RDATA, &dummy);

    delay_us(7);  /* t11 — wait before clocking data */

    /* Clock out 3 bytes (24 bits) MSB first */
    HAL_SPI_Receive(&hspi1, rx, 3, HAL_MAX_DELAY);

    cs_deassert();

    /* Reconstruct signed 24-bit value */
    int32_t sample = ((int32_t)rx[0] << 16) |
                     ((int32_t)rx[1] <<  8) |
                      (int32_t)rx[2];

    /* Sign-extend from 24-bit to 32-bit */
    if (sample & 0x800000) {
        sample |= (int32_t)0xFF000000;
    }

    g_drdy_state.last_sample = sample;
    return sample;
}

/* ─────────────────────────────────────────────
 * ADS1256 — Set input channel multiplexer
 * ───────────────────────────────────────────── */

void ads1256_set_channel(uint8_t mux) {
    /* Wait for DRDY before issuing register write */
    spi_drdy_wait(DRDY_TIMEOUT_MS);

    __disable_irq();
    g_drdy_state.drdy_flag = false;
    __enable_irq();

    uint8_t dummy;
    cs_assert();
    delay_us(2);

    /* WREG command: write 1 byte to register 0x01 (MUX) */
    spi_txrx_byte(ADS1256_CMD_WREG | 0x01, &dummy);  /* WREG, start at reg 1 */
    spi_txrx_byte(0x00, &dummy);                      /* Write 1 register */
    spi_txrx_byte(mux,  &dummy);                      /* MUX value */

    cs_deassert();
    delay_us(5);
}

/* ─────────────────────────────────────────────
 * ADS1256 — Continuous conversion mode
 * ───────────────────────────────────────────── */

void ads1256_start_continuous(void) {
    uint8_t dummy;
    spi_drdy_wait(DRDY_TIMEOUT_MS);
    cs_assert();
    delay_us(2);
    spi_txrx_byte(ADS1256_CMD_RDATAC, &dummy);
    /* Do NOT deassert CS in continuous mode — keep it low */
    /* DRDY will pulse for each new conversion */
}

void ads1256_stop_continuous(void) {
    uint8_t dummy;
    spi_txrx_byte(ADS1256_CMD_SDATAC, &dummy);
    cs_deassert();
}
```

---

### 2. Linux Userspace C — Generic DRDY with spidev + libgpiod

```c
/**
 * spi_drdy_linux.c
 * SPI with DRDY on Linux using spidev and libgpiod
 *
 * Compile: gcc -o spi_drdy_linux spi_drdy_linux.c -lgpiod
 * Run as root or add user to 'spi' and 'gpio' groups
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

/* ─── Configuration ─────────────────────────────────── */
#define SPI_DEVICE          "/dev/spidev0.0"
#define SPI_MODE            SPI_MODE_1       /* CPOL=0, CPHA=1 — ADS1256 */
#define SPI_BITS_PER_WORD   8
#define SPI_MAX_SPEED_HZ    1920000          /* 1.92 MHz */

#define GPIO_CHIP           "/dev/gpiochip0"
#define DRDY_LINE_OFFSET    17               /* BCM17 on Raspberry Pi */

#define DRDY_TIMEOUT_MS     500
#define READ_BUFFER_SIZE    3

/* ─── Context Structure ──────────────────────────────── */
typedef struct {
    int                  spi_fd;
    struct gpiod_chip   *gpio_chip;
    struct gpiod_line   *drdy_line;
} SpiDrdy_Context_t;

/* ─── Helpers ────────────────────────────────────────── */

static int64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/**
 * spi_transfer() — Full-duplex SPI transfer
 *
 * @param ctx   Initialized context
 * @param tx    TX buffer (NULL for read-only)
 * @param rx    RX buffer (NULL for write-only)
 * @param len   Number of bytes
 * @return      0 on success, -errno on failure
 */
static int spi_transfer(SpiDrdy_Context_t *ctx,
                        const uint8_t *tx, uint8_t *rx, size_t len) {
    static uint8_t zero_buf[256];

    struct spi_ioc_transfer xfer = {
        .tx_buf        = (unsigned long)(tx ? tx : zero_buf),
        .rx_buf        = (unsigned long)(rx ? rx : zero_buf),
        .len           = (uint32_t)len,
        .speed_hz      = SPI_MAX_SPEED_HZ,
        .bits_per_word = SPI_BITS_PER_WORD,
        .cs_change     = 0,
    };

    if (ioctl(ctx->spi_fd, SPI_IOC_MESSAGE(1), &xfer) < 0) {
        return -errno;
    }
    return 0;
}

/* ─── Initialization ─────────────────────────────────── */

/**
 * spi_drdy_init() — Open SPI device and configure DRDY GPIO line.
 *
 * The DRDY line is configured as an input with edge detection.
 * libgpiod's gpiod_line_event_wait() provides blocking edge detection
 * with a timeout — ideal for DRDY monitoring without busy-polling.
 *
 * @param ctx   Context to initialize
 * @return      0 on success, -1 on failure
 */
int spi_drdy_init(SpiDrdy_Context_t *ctx) {
    memset(ctx, 0, sizeof(*ctx));

    /* Open SPI device */
    ctx->spi_fd = open(SPI_DEVICE, O_RDWR);
    if (ctx->spi_fd < 0) {
        perror("open SPI device");
        return -1;
    }

    uint8_t mode = SPI_MODE;
    uint8_t bits = SPI_BITS_PER_WORD;
    uint32_t speed = SPI_MAX_SPEED_HZ;

    if (ioctl(ctx->spi_fd, SPI_IOC_WR_MODE, &mode) < 0 ||
        ioctl(ctx->spi_fd, SPI_IOC_WR_BITS_PER_WORD, &bits) < 0 ||
        ioctl(ctx->spi_fd, SPI_IOC_WR_MAX_SPEED_HZ, &speed) < 0) {
        perror("SPI ioctl config");
        close(ctx->spi_fd);
        return -1;
    }

    /* Open GPIO chip and request DRDY line as input with falling-edge events */
    ctx->gpio_chip = gpiod_chip_open(GPIO_CHIP);
    if (!ctx->gpio_chip) {
        perror("gpiod_chip_open");
        close(ctx->spi_fd);
        return -1;
    }

    ctx->drdy_line = gpiod_chip_get_line(ctx->gpio_chip, DRDY_LINE_OFFSET);
    if (!ctx->drdy_line) {
        perror("gpiod_chip_get_line");
        gpiod_chip_close(ctx->gpio_chip);
        close(ctx->spi_fd);
        return -1;
    }

    /* Request falling-edge event monitoring (DRDY is active LOW) */
    if (gpiod_line_request_falling_edge_events(ctx->drdy_line, "spi_drdy") < 0) {
        perror("gpiod_line_request_falling_edge_events");
        gpiod_chip_close(ctx->gpio_chip);
        close(ctx->spi_fd);
        return -1;
    }

    return 0;
}

/* ─── Wait for DRDY ──────────────────────────────────── */

/**
 * spi_drdy_wait() — Block until DRDY falls or timeout.
 *
 * Uses gpiod_line_event_wait() which is implemented using epoll internally,
 * so this is efficient — the thread sleeps until the edge event occurs.
 *
 * @param ctx         Initialized context
 * @param timeout_ms  Maximum wait time
 * @return            true if DRDY asserted, false on timeout or error
 */
bool spi_drdy_wait(SpiDrdy_Context_t *ctx, int timeout_ms) {
    /* Check if already asserted */
    int current = gpiod_line_get_value(ctx->drdy_line);
    if (current == 0) {
        return true;  /* Already LOW — data ready */
    }

    struct timespec timeout = {
        .tv_sec  = timeout_ms / 1000,
        .tv_nsec = (timeout_ms % 1000) * 1000000L,
    };

    int ret = gpiod_line_event_wait(ctx->drdy_line, &timeout);
    if (ret < 0) {
        perror("gpiod_line_event_wait");
        return false;
    }
    if (ret == 0) {
        fprintf(stderr, "DRDY timeout after %dms\n", timeout_ms);
        return false;
    }

    /* Consume the event */
    struct gpiod_line_event event;
    gpiod_line_event_read(ctx->drdy_line, &event);

    return (event.event_type == GPIOD_LINE_EVENT_FALLING_EDGE);
}

/* ─── ADS1256 Read Sample (Linux) ────────────────────── */

/**
 * ads1256_read_sample_linux() — Read 24-bit ADC result via spidev.
 *
 * Note: spidev handles CS automatically via the SPI_IOC_MESSAGE ioctl.
 * The kernel asserts CS before and deasserts after the transfer.
 * For devices needing manual CS timing (e.g., t6 delay), use a
 * GPIO-controlled CS instead (spidev cs_change=1 + separate CS GPIO).
 */
int32_t ads1256_read_sample_linux(SpiDrdy_Context_t *ctx) {
    if (!spi_drdy_wait(ctx, DRDY_TIMEOUT_MS)) {
        return INT32_MIN;
    }

    /* RDATA command + 3 dummy bytes to clock out 24-bit result */
    uint8_t tx[4] = { 0x01, 0xFF, 0xFF, 0xFF };
    uint8_t rx[4] = { 0 };

    /*
     * ADS1256 requires a delay between RDATA command and data bytes.
     * With spidev single ioctl, we split into two transfers using
     * cs_change=0 to keep CS asserted between them.
     */
    struct spi_ioc_transfer xfers[2] = {
        {
            /* Transfer 1: Send RDATA command */
            .tx_buf        = (unsigned long)&tx[0],
            .rx_buf        = (unsigned long)&rx[0],
            .len           = 1,
            .speed_hz      = SPI_MAX_SPEED_HZ,
            .bits_per_word = 8,
            .cs_change     = 0,
            .delay_usecs   = 7,  /* t11 delay in µs — CS stays asserted */
        },
        {
            /* Transfer 2: Read 3 data bytes */
            .tx_buf        = (unsigned long)&tx[1],
            .rx_buf        = (unsigned long)&rx[1],
            .len           = 3,
            .speed_hz      = SPI_MAX_SPEED_HZ,
            .bits_per_word = 8,
            .cs_change     = 0,
            .delay_usecs   = 0,
        },
    };

    if (ioctl(ctx->spi_fd, SPI_IOC_MESSAGE(2), xfers) < 0) {
        perror("SPI_IOC_MESSAGE");
        return INT32_MIN;
    }

    /* Reconstruct signed 24-bit sample */
    int32_t sample = ((int32_t)rx[1] << 16) |
                     ((int32_t)rx[2] <<  8) |
                      (int32_t)rx[3];

    /* Sign-extend 24-bit → 32-bit */
    if (sample & 0x800000) {
        sample |= (int32_t)0xFF000000;
    }

    return sample;
}

/* ─── Cleanup ────────────────────────────────────────── */

void spi_drdy_cleanup(SpiDrdy_Context_t *ctx) {
    if (ctx->drdy_line)  gpiod_line_release(ctx->drdy_line);
    if (ctx->gpio_chip)  gpiod_chip_close(ctx->gpio_chip);
    if (ctx->spi_fd > 0) close(ctx->spi_fd);
    memset(ctx, 0, sizeof(*ctx));
}

/* ─── Main example ───────────────────────────────────── */

int main(void) {
    SpiDrdy_Context_t ctx;

    if (spi_drdy_init(&ctx) < 0) {
        fprintf(stderr, "Initialization failed\n");
        return EXIT_FAILURE;
    }

    printf("Reading 10 samples from ADS1256...\n");

    for (int i = 0; i < 10; i++) {
        int32_t sample = ads1256_read_sample_linux(&ctx);

        if (sample == INT32_MIN) {
            fprintf(stderr, "Sample %d: ERROR (timeout or transfer failure)\n", i);
        } else {
            /* Convert to voltage: Vref=2.5V, gain=1, FSR=2^23 */
            double voltage = (double)sample * (2.5 / (1 << 23));
            printf("Sample %2d: 0x%06X  (%+.6f V)\n",
                   i, sample & 0xFFFFFF, voltage);
        }
    }

    spi_drdy_cleanup(&ctx);
    return EXIT_SUCCESS;
}
```

---

### 3. C++ — RTOS-Style Task with Event Queue (FreeRTOS)

```cpp
/**
 * SpiDrdyTask.hpp
 * C++ class encapsulating SPI+DRDY with FreeRTOS task and queue
 */
#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "semphr.h"
#include <cstdint>
#include <functional>

struct AdcSample {
    int32_t  value;
    uint32_t timestamp_ms;
    bool     valid;
};

class SpiDrdyTask {
public:
    using SampleCallback = std::function<void(const AdcSample&)>;

    SpiDrdyTask(SampleCallback cb, UBaseType_t priority = 3)
        : m_callback(cb)
    {
        m_sem  = xSemaphoreCreateBinary();
        m_queue = xQueueCreate(16, sizeof(AdcSample));

        xTaskCreate(task_entry, "SPI_DRDY", 512, this, priority, &m_task);
    }

    /**
     * Called from ISR (EXTI handler) when DRDY falls.
     * Must be called with FromISR suffix variants.
     */
    void notify_from_isr() {
        BaseType_t higher_priority_woken = pdFALSE;
        xSemaphoreGiveFromISR(m_sem, &higher_priority_woken);
        portYIELD_FROM_ISR(higher_priority_woken);
    }

private:
    SampleCallback   m_callback;
    TaskHandle_t     m_task   = nullptr;
    SemaphoreHandle_t m_sem   = nullptr;
    QueueHandle_t    m_queue  = nullptr;

    static void task_entry(void *arg) {
        static_cast<SpiDrdyTask*>(arg)->run();
    }

    void run() {
        for (;;) {
            /* Block indefinitely until ISR gives semaphore */
            if (xSemaphoreTake(m_sem, portMAX_DELAY) == pdTRUE) {
                AdcSample s;
                s.timestamp_ms = xTaskGetTickCount();
                s.value = read_sample_from_device();
                s.valid = (s.value != INT32_MIN);

                /* Post to queue; if full, overwrite oldest */
                if (xQueueSend(m_queue, &s, 0) != pdTRUE) {
                    AdcSample discard;
                    xQueueReceive(m_queue, &discard, 0);
                    xQueueSend(m_queue, &s, 0);
                }

                if (m_callback) {
                    m_callback(s);
                }
            }
        }
    }

    int32_t read_sample_from_device() {
        /* Call the low-level SPI read from spi_drdy.c */
        extern int32_t ads1256_read_sample(void);
        return ads1256_read_sample();
    }
};

/* ─── ISR wiring ──────────────────────────────────────── */
/*
 * In your EXTI IRQ handler (stm32f4xx_it.c):
 *
 *   extern SpiDrdyTask *g_adc_task;
 *
 *   void EXTI0_IRQHandler(void) {
 *       HAL_GPIO_EXTI_IRQHandler(DRDY_GPIO_PIN);
 *   }
 *
 *   void HAL_GPIO_EXTI_Callback(uint16_t pin) {
 *       if (pin == DRDY_GPIO_PIN && g_adc_task) {
 *           g_adc_task->notify_from_isr();
 *       }
 *   }
 */
```

---

## Implementation in Rust

### Platform Context

The Rust examples use:
- **`embedded-hal`** traits for portability across MCU HALs.
- **`linux-embedded-hal`** for running on Linux (Raspberry Pi, BeagleBone, etc.).
- **`rppal`** for Raspberry Pi GPIO access (alternative to linux-embedded-hal for Pi).
- **`nb`** (non-blocking) crate for async-compatible polling.

---

### 1. Rust — Embedded HAL Abstraction (no_std compatible)

```rust
// Cargo.toml dependencies:
// [dependencies]
// embedded-hal = "1.0"
// nb = "1.1"

//! spi_drdy.rs — SPI + DRDY abstraction using embedded-hal 1.0 traits.
//!
//! Generic over any SPI bus and GPIO pin, works on any embedded target.

#![no_std]

use core::fmt;
use embedded_hal::{
    digital::InputPin,
    spi::SpiDevice,
};
use nb::block;

/// Errors that can occur during SPI+DRDY operations.
#[derive(Debug)]
pub enum DrdyError<SpiErr, GpioErr> {
    /// SPI transfer failed.
    Spi(SpiErr),
    /// GPIO read failed.
    Gpio(GpioErr),
    /// DRDY did not assert within the timeout.
    Timeout,
}

impl<S: fmt::Debug, G: fmt::Debug> fmt::Display for DrdyError<S, G> {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DrdyError::Spi(e)   => write!(f, "SPI error: {:?}", e),
            DrdyError::Gpio(e)  => write!(f, "GPIO error: {:?}", e),
            DrdyError::Timeout  => write!(f, "DRDY timeout"),
        }
    }
}

/// Maximum number of polling iterations before timeout.
const DRDY_POLL_MAX: u32 = 100_000;

/// ADS1256 driver struct, generic over SPI device and DRDY pin.
///
/// `SPI` must implement `embedded_hal::spi::SpiDevice` (manages CS internally).
/// `DRDY` must implement `embedded_hal::digital::InputPin`.
pub struct Ads1256<SPI, DRDY> {
    spi:  SPI,
    drdy: DRDY,
}

impl<SPI, DRDY, SpiErr, GpioErr> Ads1256<SPI, DRDY>
where
    SPI:  SpiDevice<Error = SpiErr>,
    DRDY: InputPin<Error = GpioErr>,
    SpiErr:  fmt::Debug,
    GpioErr: fmt::Debug,
{
    /// Create a new ADS1256 driver.
    ///
    /// # Arguments
    /// * `spi`  — SPI device with automatic CS management.
    /// * `drdy` — DRDY input GPIO pin (active LOW).
    pub fn new(spi: SPI, drdy: DRDY) -> Self {
        Self { spi, drdy }
    }

    /// Wait for DRDY to go LOW (data ready), with a polling limit.
    ///
    /// Returns `Ok(())` when DRDY is asserted, `Err(DrdyError::Timeout)`
    /// if the pin does not assert within `DRDY_POLL_MAX` iterations.
    ///
    /// For interrupt-driven use, replace this with a future/signal mechanism.
    pub fn wait_for_drdy(&mut self) -> Result<(), DrdyError<SpiErr, GpioErr>> {
        for _ in 0..DRDY_POLL_MAX {
            let low = self.drdy.is_low()
                .map_err(DrdyError::Gpio)?;
            if low {
                return Ok(());
            }
            // Insert a small busy-wait or yield here for RTOS use:
            // cortex_m::asm::nop();
        }
        Err(DrdyError::Timeout)
    }

    /// Read a 24-bit sample from the ADS1256.
    ///
    /// Waits for DRDY, issues RDATA command, then clocks out 3 bytes.
    /// Returns the sign-extended 32-bit value.
    ///
    /// ADS1256 SPI timing note:
    ///   The t11 delay (>= 50 * tCLKIN) between RDATA command and data
    ///   bytes must be handled at the HAL level (e.g., via SPI clock speed
    ///   limiting or inserting dummy bytes in the transfer buffer).
    pub fn read_sample(&mut self) -> Result<i32, DrdyError<SpiErr, GpioErr>> {
        self.wait_for_drdy()?;

        // Perform two separate SpiDevice transactions:
        // Transaction 1: Send RDATA command (0x01)
        // Transaction 2: Read 3 data bytes
        //
        // SpiDevice::transaction() keeps CS asserted for the duration.
        let mut data = [0u8; 4];

        self.spi.transaction(&mut [
            // Write RDATA command byte, then read 3 data bytes in one CS assertion
            embedded_hal::spi::Operation::Write(&[0x01_u8]),
            // Some HALs support delay within transaction; if not, rely on SPI speed.
            embedded_hal::spi::Operation::Read(&mut data[0..3]),
        ]).map_err(DrdyError::Spi)?;

        // Reconstruct signed 24-bit value
        let raw = ((data[0] as i32) << 16)
                | ((data[1] as i32) <<  8)
                |  (data[2] as i32);

        // Sign-extend from 24 bits to 32 bits
        let sample = if raw & 0x800000 != 0 {
            raw | (0xFF_u32 as i32) << 24
        } else {
            raw
        };

        Ok(sample)
    }

    /// Write to an ADS1256 register.
    ///
    /// `reg`  — Register address (0x00–0x0A).
    /// `data` — Byte to write.
    pub fn write_register(
        &mut self,
        reg: u8,
        data: u8,
    ) -> Result<(), DrdyError<SpiErr, GpioErr>> {
        self.wait_for_drdy()?;

        // WREG command: 0x50 | reg_addr, then count-1=0x00, then data
        let cmd = [0x50 | (reg & 0x0F), 0x00, data];
        self.spi.write(&cmd).map_err(DrdyError::Spi)?;

        Ok(())
    }

    /// Set the analog input multiplexer (channel selection).
    ///
    /// ADS1256 MUX register encoding:
    ///   Bits[7:4] = positive input (AIN0..AIN7 = 0x0..0x7, AINCOM = 0x8)
    ///   Bits[3:0] = negative input
    pub fn set_channel(&mut self, pos: u8, neg: u8) -> Result<(), DrdyError<SpiErr, GpioErr>> {
        let mux = ((pos & 0x0F) << 4) | (neg & 0x0F);
        self.write_register(0x01, mux)?;  // Register 0x01 = MUX
        Ok(())
    }

    /// Consume self, returning the inner SPI bus and DRDY pin.
    pub fn release(self) -> (SPI, DRDY) {
        (self.spi, self.drdy)
    }
}
```

---

### 2. Rust — Linux Userspace with `rppal` (Raspberry Pi)

```rust
// Cargo.toml:
// [dependencies]
// rppal = "0.18"
// anyhow = "1.0"

//! spi_drdy_linux.rs — SPI + DRDY on Raspberry Pi using rppal.
//!
//! Demonstrates both polling and interrupt-driven (thread-based) DRDY handling.

use std::sync::{Arc, Mutex, Condvar};
use std::thread;
use std::time::Duration;

use rppal::gpio::{Gpio, InputPin, Level, Trigger};
use rppal::spi::{Bus, Mode, SlaveSelect, Spi};
use anyhow::{Context, Result};

// ─── Configuration ────────────────────────────────────────────
const DRDY_BCM_PIN: u8   = 17;       // BCM17 = physical pin 11
const SPI_BUS:      Bus  = Bus::Spi0;
const SPI_SS:       SlaveSelect = SlaveSelect::Ss0;
const SPI_CLK_HZ:   u32  = 1_920_000;
const SPI_MODE:     Mode = Mode::Mode1; // CPOL=0, CPHA=1

// ─── Polling-Based DRDY ───────────────────────────────────────

/// `SpiDrdyPoller` — Blocking DRDY poll with timeout.
/// Simple, no threads, minimal dependencies.
pub struct SpiDrdyPoller {
    spi:  Spi,
    drdy: InputPin,
}

impl SpiDrdyPoller {
    pub fn new() -> Result<Self> {
        let spi = Spi::new(SPI_BUS, SPI_SS, SPI_CLK_HZ, SPI_MODE)
            .context("Failed to open SPI device")?;

        let gpio = Gpio::new().context("Failed to open GPIO")?;
        let drdy = gpio.get(DRDY_BCM_PIN)
            .context("Failed to get DRDY pin")?
            .into_input_pullup();  // DRDY active LOW — use pull-up

        Ok(Self { spi, drdy })
    }

    /// Wait for DRDY to go LOW.
    /// Polls every 10µs; times out after `timeout`.
    pub fn wait_drdy(&self, timeout: Duration) -> Result<bool> {
        let deadline = std::time::Instant::now() + timeout;
        loop {
            if self.drdy.read() == Level::Low {
                return Ok(true);
            }
            if std::time::Instant::now() >= deadline {
                return Ok(false);  // Timeout — not an error, caller decides
            }
            thread::sleep(Duration::from_micros(10));
        }
    }

    /// Read 3 bytes from the slave after DRDY asserts.
    ///
    /// Performs a two-transfer sequence with delay inbetween:
    ///   1. Write command byte (RDATA = 0x01)
    ///   2. Sleep 7µs (t11 for ADS1256 at 7.68MHz CLKIN)
    ///   3. Read 3 data bytes
    ///
    /// rppal's Spi::write() and Spi::read() each assert/deassert CS
    /// independently. For devices needing CS held across a delay,
    /// use Spi::transfer() with padding bytes instead.
    pub fn read_sample(&self) -> Result<i32> {
        if !self.wait_drdy(Duration::from_millis(500))? {
            anyhow::bail!("DRDY timeout");
        }

        // Single transfer: [RDATA_CMD, dummy, dummy, dummy]
        // The delay between cmd and data is approximated by SPI clock rate.
        // For strict t11 compliance, split into two transfers + sleep.
        let mut buffer = [0x01_u8, 0x00, 0x00, 0x00];
        self.spi.transfer_in_place(&mut buffer)
            .context("SPI transfer failed")?;

        let raw = ((buffer[1] as i32) << 16)
                | ((buffer[2] as i32) <<  8)
                |  (buffer[3] as i32);

        // Sign-extend 24-bit → 32-bit
        Ok(if raw & 0x80_0000 != 0 { raw | -0x100_0000 } else { raw })
    }
}

// ─── Interrupt-Driven DRDY ────────────────────────────────────

/// `SpiDrdyDriver` — Interrupt-driven DRDY using rppal's async interrupt API.
///
/// rppal allows registering a callback on GPIO edge events, triggered from
/// a background thread managed by the library. We use a Condvar to
/// communicate between the interrupt callback and the main thread.
pub struct SpiDrdyDriver {
    spi:         Spi,
    drdy_signal: Arc<(Mutex<bool>, Condvar)>,
    // We keep the pin alive to keep the interrupt registered.
    _drdy_pin:   InputPin,
}

impl SpiDrdyDriver {
    pub fn new() -> Result<Self> {
        let spi = Spi::new(SPI_BUS, SPI_SS, SPI_CLK_HZ, SPI_MODE)
            .context("Failed to open SPI device")?;

        let gpio = Gpio::new().context("Failed to open GPIO")?;
        let mut drdy = gpio.get(DRDY_BCM_PIN)
            .context("Failed to get DRDY pin")?
            .into_input_pullup();

        let signal: Arc<(Mutex<bool>, Condvar)> = Arc::new((Mutex::new(false), Condvar::new()));
        let signal_clone = Arc::clone(&signal);

        // Register interrupt: fires on falling edge (DRDY goes LOW).
        drdy.set_async_interrupt(Trigger::FallingEdge, move |_level| {
            let (lock, cvar) = &*signal_clone;
            let mut ready = lock.lock().unwrap();
            *ready = true;
            cvar.notify_one();  // Wake waiting thread
        }).context("Failed to set async interrupt")?;

        Ok(Self {
            spi,
            drdy_signal: signal,
            _drdy_pin: drdy,
        })
    }

    /// Block until the DRDY interrupt fires, or timeout.
    pub fn wait_drdy(&self, timeout: Duration) -> Result<bool> {
        let (lock, cvar) = &*self.drdy_signal;
        let mut ready = lock.lock().unwrap();

        if *ready {
            *ready = false;
            return Ok(true);
        }

        // wait_timeout blocks the current thread until notified or timeout.
        let (mut ready, timed_out) = cvar.wait_timeout(ready, timeout).unwrap();

        if timed_out.timed_out() {
            return Ok(false);
        }

        *ready = false;  // Clear the flag
        Ok(true)
    }

    /// Read 24-bit ADC sample after DRDY assert.
    pub fn read_sample(&self) -> Result<i32> {
        if !self.wait_drdy(Duration::from_millis(500))? {
            anyhow::bail!("DRDY timeout");
        }

        let mut buf = [0x01_u8, 0x00, 0x00, 0x00];
        self.spi.transfer_in_place(&mut buf)
            .context("SPI transfer failed")?;

        let raw = ((buf[1] as i32) << 16)
                | ((buf[2] as i32) <<  8)
                |  (buf[3] as i32);

        Ok(if raw & 0x80_0000 != 0 { raw | -0x100_0000 } else { raw })
    }
}

// ─── Main ────────────────────────────────────────────────────

fn main() -> Result<()> {
    println!("=== SPI + DRDY Demo (Raspberry Pi) ===\n");

    // Demo 1: Polling mode
    println!("--- Polling mode ---");
    let poller = SpiDrdyPoller::new()?;
    for i in 0..5 {
        match poller.read_sample() {
            Ok(v) => {
                let voltage = v as f64 * (2.5 / (1 << 23) as f64);
                println!("  Sample {:2}: {:+.6} V  (raw: 0x{:06X})", i, voltage, v & 0xFF_FFFF);
            }
            Err(e) => eprintln!("  Sample {:2}: ERROR — {}", i, e),
        }
    }

    // Demo 2: Interrupt-driven mode
    println!("\n--- Interrupt-driven mode ---");
    let driver = SpiDrdyDriver::new()?;
    for i in 0..5 {
        match driver.read_sample() {
            Ok(v) => {
                let voltage = v as f64 * (2.5 / (1 << 23) as f64);
                println!("  Sample {:2}: {:+.6} V  (raw: 0x{:06X})", i, voltage, v & 0xFF_FFFF);
            }
            Err(e) => eprintln!("  Sample {:2}: ERROR — {}", i, e),
        }
    }

    Ok(())
}
```

---

### 3. Rust — Async/Await with `embassy` (no_std, STM32)

```rust
// Demonstrates DRDY using Embassy's async GPIO ExtiInput,
// which is natively interrupt-driven and cooperative with the executor.

#![no_std]
#![no_main]

// Cargo.toml (embassy-stm32 target):
// embassy-stm32 = { version = "0.1", features = ["stm32f411re", "time-driver-any"] }
// embassy-executor = { version = "0.5", features = ["arch-cortex-m"] }
// embassy-time = "0.3"

use embassy_executor::Spawner;
use embassy_stm32::{
    exti::ExtiInput,
    gpio::{Input, Pull},
    spi::{Config as SpiConfig, Spi},
    time::Hertz,
};
use embassy_time::{Duration, Timer, with_timeout};

/// Embassy task: continuously reads ADC samples, sleeping between reads.
/// The async wait for DRDY never blocks the executor — other tasks run freely.
#[embassy_executor::task]
async fn adc_reader_task(
    mut spi: Spi<'static, embassy_stm32::peripherals::SPI1,
                  embassy_stm32::peripherals::DMA2_CH3,
                  embassy_stm32::peripherals::DMA2_CH0>,
    mut drdy: ExtiInput<'static, embassy_stm32::peripherals::PA0>,
) {
    loop {
        // Await DRDY falling edge asynchronously.
        // This suspends only this task — the executor runs other tasks.
        let drdy_result = with_timeout(
            Duration::from_millis(500),
            drdy.wait_for_falling_edge(),  // Async, interrupt-driven
        ).await;

        match drdy_result {
            Err(_elapsed) => {
                defmt::warn!("DRDY timeout — slave unresponsive");
                Timer::after(Duration::from_millis(10)).await;
                continue;
            }
            Ok(_) => { /* DRDY asserted — proceed */ }
        }

        // Send RDATA command then read 3 data bytes.
        let cmd = [0x01_u8];
        if let Err(e) = spi.write(&cmd).await {
            defmt::error!("SPI write failed: {:?}", e);
            continue;
        }

        // t11 delay — yield to executor (not precise but avoids busy-wait)
        Timer::after(Duration::from_micros(7)).await;

        let mut data = [0u8; 3];
        if let Err(e) = spi.read(&mut data).await {
            defmt::error!("SPI read failed: {:?}", e);
            continue;
        }

        let raw = ((data[0] as i32) << 16)
                | ((data[1] as i32) <<  8)
                |  (data[2] as i32);

        let sample = if raw & 0x800000 != 0 { raw | -0x100_0000 } else { raw };

        // Convert to microvolt (Vref=2.5V, gain=1, 24-bit FSR)
        let uv = (sample as i64 * 2_500_000) / (1 << 23);
        defmt::info!("ADC: {} µV  (raw: 0x{:06X})", uv, sample & 0xFF_FFFF);
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    // Configure SPI1 at 1.92 MHz, Mode 1
    let mut spi_config = SpiConfig::default();
    spi_config.frequency = Hertz(1_920_000);

    let spi = Spi::new(
        p.SPI1,
        p.PB3,   // SCLK
        p.PB5,   // MOSI
        p.PB4,   // MISO
        p.DMA2_CH3,
        p.DMA2_CH0,
        spi_config,
    );

    // Configure DRDY as ExtiInput — combines GPIO input + EXTI interrupt.
    let drdy_pin = Input::new(p.PA0, Pull::Up);  // Active LOW → pull-up
    let drdy = ExtiInput::new(drdy_pin, p.EXTI0);

    spawner.spawn(adc_reader_task(spi, drdy)).unwrap();

    // Main task can do other work; ADC reading runs concurrently
    loop {
        Timer::after(Duration::from_secs(1)).await;
        defmt::info!("Main task heartbeat");
    }
}
```

---

## Edge Cases and Pitfalls

### 1. Race Condition: DRDY Between Check and CS Assert

If DRDY is checked by the master, then DRDY pulses (goes HIGH again) before CS is asserted, the master may assert CS when no valid data is present.

**Mitigation:**
- Clear the DRDY flag *inside* a critical section immediately before asserting CS.
- Use the DRDY signal level rather than an edge-triggered flag wherever possible.
- Some devices (like ADS1256) hold DRDY LOW until CS goes LOW, providing a natural lock.

```c
/* CORRECT — atomic flag clear and CS assert */
__disable_irq();
g_drdy_state.drdy_flag = false;
cs_assert();                   /* CS low before re-enabling IRQ */
__enable_irq();
```

### 2. DRDY Glitches and Debounce

DRDY can glitch due to PCB noise, especially when long wires are used. A glitch can cause spurious SPI reads.

**Mitigation:**
- Add a small RC filter (100Ω + 100nF) on the DRDY line.
- In firmware, after detecting DRDY falling edge, re-read the pin level after 1–2µs to confirm.
- Use active-LOW with pull-up to make the line noise-resilient (failsafe HIGH = not ready).

### 3. Missed DRDY Edges

If the DRDY pulse is very short and the CPU is in a critical section (IRQ disabled), the falling edge can be missed entirely.

**Mitigation:**
- Always supplement edge detection with a direct pin read in the wait function (fallback poll).
- Keep critical sections as short as possible.
- Choose SPI devices where DRDY is a latched signal (remains LOW until read), not a pulse.

### 4. SPI Clock Polarity / Phase Mismatch with DRDY Timing

Some devices change DRDY timing based on SPI mode. Always verify:
- Does DRDY de-assert on CS falling edge, first clock edge, or after last clock edge?
- This affects whether to check DRDY before or after CS in sequential reads.

### 5. Multiple Slaves Sharing DRDY (Wired-OR)

When DRDY is shared, the master cannot distinguish which slave asserted. After waking, it must query a status register on each slave. There is a risk of one slave re-asserting DRDY during the status polling of another slave, causing indefinite servicing of a single slave.

**Mitigation:** Round-robin status polling with a maximum service count per wake cycle.

### 6. DRDY Polarity Inversion

Some sensors assert DRDY HIGH when data is ready (active HIGH). Always check the datasheet.
Active HIGH DRDY connects to a GPIO configured for **rising edge** interrupt detection.

---

## Summary

The **SPI Data Ready (DRDY)** pattern is a simple but powerful extension to the standard SPI protocol that transforms SPI from a purely master-driven bus into one that supports **slave-initiated communication and flow control**.

| Aspect | Detail |
|---|---|
| **Core concept** | An additional GPIO line (DRDY) is driven by the slave to notify the master that data is ready to be read or that the slave is busy. |
| **Electrical convention** | Typically active LOW with an open-drain driver and pull-up resistor; wired to a GPIO with interrupt capability on the master. |
| **Why it matters** | Eliminates the need for polling loops, reduces latency, prevents reading stale or incomplete data, and enables the master CPU to sleep between transactions — critical for low-power designs. |
| **DRDY vs BUSY** | DRDY signals "data available for master to read"; BUSY signals "slave is processing, do not start a new transaction". Both can be used together for complete flow control. |
| **Implementation strategies** | Interrupt-driven (best), polling with timeout (simplest), DMA+interrupt (most efficient), RTOS event queue (cleanest for complex systems). |
| **C/C++ key patterns** | ISR sets a flag/semaphore; wait function checks flag with timeout fallback; atomic CS assertion to avoid race conditions; `SPI_IOC_MESSAGE` with `delay_usecs` on Linux for inter-transfer delays. |
| **Rust key patterns** | Generic `embedded-hal` traits for portability; `rppal` async interrupt callback with `Condvar` for Linux; Embassy `ExtiInput::wait_for_falling_edge()` for fully async, cooperative embedded operation. |
| **Critical pitfalls** | Race condition between DRDY check and CS assertion; missed edges due to ISR latency; DRDY glitches from PCB noise; polarity mismatch; shared DRDY with multiple slaves. |
| **Typical devices** | High-precision ADCs (ADS1256), IMUs (ADIS16xxx), barometric sensors (MS5611), GNSS modules, wireless SoCs (nRF24L01). |

The pattern scales gracefully from simple bare-metal polling to fully asynchronous, cooperative multitasking with Embassy, always using the same fundamental handshake: **wait for DRDY → assert CS → transfer → deassert CS → repeat**.