# 18. Parity Errors — Detecting Transmission Errors Through Parity Bit Validation

**Theory sections** — What a parity bit is, how UART framing works with it, all four parity modes (Even, Odd, Mark, Space), the detection mechanism on both TX and RX sides, and a clear table of the fundamental limitation (only odd numbers of bit flips are caught).

**Hardware register reference** — Bit-level details for three common UART families: the 16550 (PC/Linux), STM32 (ARM Cortex-M), and AVR (ATmega).

**C/C++ code examples:**
- Portable software parity computation and validation
- Linux `termios` setup with `INPCK | PARMRK` and a full decoder for the `\xFF\x00\xByte` error-marking escape sequence
- STM32 HAL with `HAL_UART_ErrorCallback` and PE interrupt handling
- Bare-metal 16550 register-level polling with a structured `uart_rx_result_t`

**Rust code examples:**
- `no_std` software parity with `ParityMode` enum and `Result<u8, ParityError>` return types
- Linux `serialport` crate usage with error statistics tracking
- STM32 `stm32f4xx-hal` with typed `UartRxError` enum
- Lock-free interrupt-safe ring buffer with per-slot parity/framing error flags

**Practical guidance** — ISR flow diagram, recovery strategies table (discard, NAK, resync, baud-rate check, alarm escalation), CRC layering advice, error-rate monitoring, and a configuration checklist.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [What Is a Parity Bit?](#2-what-is-a-parity-bit)
3. [How Parity Works in UART Framing](#3-how-parity-works-in-uart-framing)
4. [Types of Parity](#4-types-of-parity)
5. [Parity Error Detection: Mechanism](#5-parity-error-detection-mechanism)
6. [Limitations of Parity](#6-limitations-of-parity)
7. [Hardware: UART Parity Configuration Registers](#7-hardware-uart-parity-configuration-registers)
8. [C/C++ Implementation](#8-cc-implementation)
9. [Rust Implementation](#9-rust-implementation)
10. [Interrupt-Driven Parity Error Handling](#10-interrupt-driven-parity-error-handling)
11. [Practical Strategies for Robust Systems](#11-practical-strategies-for-robust-systems)
12. [Summary](#12-summary)

---

## 1. Introduction

In any serial communication system, data transmitted over a physical medium is susceptible to corruption. Electromagnetic interference, impedance mismatches, voltage fluctuations, and timing skew can all flip individual bits within a transmitted byte. Without any error-detection mechanism, corrupted data would be silently accepted as valid, potentially causing system misbehaviour or data loss.

**Parity checking** is the oldest and most lightweight error-detection scheme used in UART (Universal Asynchronous Receiver/Transmitter) communication. It works by appending a single extra bit — the *parity bit* — to every transmitted character, allowing the receiver to determine whether the data arrived intact.

Although modern systems often rely on higher-layer protocols (CRC, checksums, ARQ) for robust error correction, parity remains relevant because:

- It is implemented entirely **in hardware** with zero CPU overhead during normal operation.
- It provides **immediate, per-frame detection** rather than end-of-message detection.
- It is mandated or expected by many legacy and industrial protocols (RS-232, Modbus RTU, some MIDI extensions).
- It is easy to configure and reason about, making it a useful first line of defence.

---

## 2. What Is a Parity Bit?

A **parity bit** is a single binary digit appended to a group of data bits. Its value is chosen so that the total number of `1` bits in the combined group (data bits + parity bit) satisfies a chosen rule — either **even** or **odd**.

### Simple Example

Data byte: `0b01001011` (decimal 75, `'K'` in ASCII)

Count the `1` bits: positions 0, 1, 3, 6 → **4 ones** (an even count).

| Parity Mode | Rule                           | Parity Bit Value |
|-------------|--------------------------------|-----------------|
| Even parity | Total ones must be even        | `0` (already even) |
| Odd parity  | Total ones must be odd         | `1` (make it odd) |
| Mark parity | Parity bit always `1`          | `1`              |
| Space parity| Parity bit always `0`          | `0`              |

The parity bit is **transmitted as the 9th bit** in the UART frame (after the 8 data bits, before the stop bit).

---

## 3. How Parity Works in UART Framing

A UART frame with parity enabled has the following structure:

```
 _______________________________________________
|     |      |      |      |      |     |      |
| ... | DATA  DATA  DATA  DATA  ... | PAR | STOP |
|_____|______|______|______|______|_____|______|
  Start  D0    D1    D2    D3  ...D7   Parity  Stop
  bit                                   bit    bit(s)
```

- **Start bit**: Always logic LOW (1 bit).
- **Data bits**: 5–9 bits (typically 8).
- **Parity bit**: Optional; inserted between last data bit and stop bit.
- **Stop bit(s)**: Always logic HIGH (1 or 2 bits).

The UART hardware **automatically computes** the parity bit on the transmit side and **automatically validates** it on the receive side. When the received parity bit does not match the expected value, the hardware sets a **Parity Error (PE)** flag in its status register and (if configured) fires an interrupt.

---

## 4. Types of Parity

### 4.1 Even Parity

The parity bit is set so that the **total number of `1` bits** (data + parity) is **even**.

```
Data: 1 0 1 1 0 0 1 0  → four 1s (even) → parity bit = 0
Data: 1 0 1 1 0 0 1 1  → five  1s (odd)  → parity bit = 1
```

Even parity is the most widely used mode in industrial and telecom protocols.

### 4.2 Odd Parity

The parity bit is set so that the **total number of `1` bits** is **odd**.

```
Data: 1 0 1 1 0 0 1 0  → four 1s (even) → parity bit = 1
Data: 1 0 1 1 0 0 1 1  → five  1s (odd)  → parity bit = 0
```

Odd parity has a small advantage: an all-zeros byte always results in a parity bit of `1`, ensuring the line is never idle-like during data transmission.

### 4.3 Mark Parity

The parity bit is **always `1`**, regardless of data content. This effectively dedicates the 9th bit as a constant marker. It provides no error detection but is sometimes used in address-byte detection (e.g., Multidrop/9-bit UART modes).

### 4.4 Space Parity

The parity bit is **always `0`**. Similar to mark parity — no error detection, but can be used to signal a break condition or in legacy protocol compatibility.

### 4.5 No Parity

The parity bit is omitted entirely, giving the standard 8N1 (8 data, No parity, 1 stop) configuration. The frame is one bit shorter, increasing throughput by ~10% at the cost of any hardware error detection.

---

## 5. Parity Error Detection: Mechanism

### 5.1 Transmitter Side

The transmitting UART hardware:
1. Counts the number of `1` bits in the data byte.
2. Determines the required parity bit value based on the selected mode.
3. Appends the parity bit to the outgoing frame automatically.

No software intervention is needed.

### 5.2 Receiver Side

The receiving UART hardware:
1. Receives all data bits.
2. Counts the number of `1` bits received (including the parity bit).
3. Checks whether the parity matches the configured mode.
4. If the parity is **incorrect**, sets the **Parity Error (PE)** flag in the Line Status Register (LSR) or equivalent status register.
5. Optionally generates a CPU interrupt or DMA event.

The corrupted byte is still placed into the receive FIFO/buffer — software must decide whether to discard it, request a retransmission, or log an error.

### 5.3 What Triggers a Parity Error?

- A single bit flip during transmission (noise spike, EMI).
- A baud rate mismatch causing sample-point drift.
- Mismatched parity configuration between transmitter and receiver.
- Electrical line faults (short circuit, broken ground reference).

---

## 6. Limitations of Parity

Parity checking detects **odd numbers of bit errors** within a single frame:

| Number of Bits Flipped | Detected? |
|------------------------|-----------|
| 1                      | ✅ Yes    |
| 2                      | ❌ No (errors cancel out) |
| 3                      | ✅ Yes    |
| 4                      | ❌ No     |
| Any even number        | ❌ No     |

This is the fundamental weakness: parity only catches **single-bit errors** reliably. In noisy environments where burst errors affect multiple bits simultaneously (e.g., near VFDs, switching power supplies), parity alone is insufficient and must be supplemented with CRC or higher-layer checksums.

Additionally, parity provides **detection only** — not correction. It cannot identify *which* bit is wrong, only that something is wrong.

---

## 7. Hardware: UART Parity Configuration Registers

### 7.1 Generic 16550-Compatible UART (PC/Linux)

The 16550 UART (used in PCs and many SoCs) uses the **Line Control Register (LCR)** at offset `+3` from the UART base address:

```
LCR Register (offset 0x03):
 Bit 7 : DLAB  – Divisor Latch Access Bit
 Bit 6 : Break Control
 Bit 5 : Stick Parity (force mark/space)
 Bit 4 : Even Parity Select (0=odd, 1=even)
 Bit 3 : Parity Enable (0=no parity, 1=parity enabled)
 Bit 2 : Stop Bits (0=1 stop bit, 1=2 stop bits)
 Bits 1-0 : Word Length (00=5, 01=6, 10=7, 11=8 bits)
```

The **Line Status Register (LSR)** at offset `+5` reports errors:

```
LSR Register (offset 0x05):
 Bit 0 : Data Ready
 Bit 1 : Overrun Error
 Bit 2 : Parity Error   ← relevant bit
 Bit 3 : Framing Error
 Bit 4 : Break Interrupt
 Bit 5 : THRE (Transmit Holding Register Empty)
 Bit 6 : TEMT (Transmitter Empty)
 Bit 7 : Error in RCVR FIFO
```

### 7.2 STM32 UART (ARM Cortex-M, HAL)

On STM32 microcontrollers, parity is configured via the `USART_CR1` register:

```
USART_CR1:
 Bit 10 : PCE  – Parity Control Enable
 Bit 9  : PS   – Parity Selection (0=even, 1=odd)
 Bit 8  : PEIE – Parity Error Interrupt Enable
```

The status register `USART_SR` (or `USART_ISR` on STM32F7+):

```
 Bit 0 : PE – Parity Error flag
```

### 7.3 AVR USART (8-bit Microcontroller)

On AVR (e.g., ATmega328P), the `UCSR0C` register controls parity:

```
UCSR0C:
 Bits 5-4 : UPM01:UPM00
   00 = Disabled
   01 = Reserved
   10 = Even parity
   11 = Odd parity
```

Status is in `UCSR0A`:

```
 Bit 2 : UPE0 – USART Parity Error
```

---

## 8. C/C++ Implementation

### 8.1 Software Parity Calculation (Platform-Independent)

Before diving into hardware examples, here is a portable software implementation of parity calculation — useful for verifying hardware behaviour or for implementing parity on bit-banged interfaces:

```c
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Compute even parity for an 8-bit value.
 *        Returns 0 if the number of set bits is already even,
 *        returns 1 if odd (parity bit needed to make it even).
 */
uint8_t compute_even_parity(uint8_t data) {
    data ^= data >> 4;
    data ^= data >> 2;
    data ^= data >> 1;
    return data & 0x01;
}

/**
 * @brief Compute odd parity — invert of even parity.
 */
uint8_t compute_odd_parity(uint8_t data) {
    return compute_even_parity(data) ^ 0x01;
}

/**
 * @brief Validate received byte against its parity bit.
 *
 * @param data      The 8 data bits received.
 * @param parity_bit The parity bit received (0 or 1).
 * @param even_mode  true = even parity, false = odd parity.
 * @return true if parity is correct (no error), false if error detected.
 */
bool validate_parity(uint8_t data, uint8_t parity_bit, bool even_mode) {
    uint8_t expected = even_mode ? compute_even_parity(data)
                                 : compute_odd_parity(data);
    return (expected == (parity_bit & 0x01));
}

/* --- Example usage ---------------------------------------------------- */
#include <stdio.h>

int main(void) {
    uint8_t byte = 0x4B;  /* 'K', binary 0100 1011 — four 1s */

    uint8_t ep = compute_even_parity(byte);
    uint8_t op = compute_odd_parity(byte);

    printf("Data: 0x%02X  Even parity bit: %u  Odd parity bit: %u\n",
           byte, ep, op);

    /* Simulate a received byte with a 1-bit error */
    uint8_t corrupted = byte ^ 0x04;  /* flip bit 2 → five 1s */
    bool ok = validate_parity(corrupted, ep, /*even=*/true);
    printf("Validation after bit-flip: %s\n", ok ? "OK" : "PARITY ERROR");

    return 0;
}
```

**Expected output:**
```
Data: 0x4B  Even parity bit: 0  Odd parity bit: 1
Validation after bit-flip: PARITY ERROR
```

---

### 8.2 Linux / POSIX — Configuring Hardware Parity via `termios`

On Linux (and any POSIX system with a serial port), parity is configured through the `termios` interface:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#define UART_DEVICE "/dev/ttyS0"
#define BAUD_RATE   B9600
#define READ_TIMEOUT_DS 10   /* deciseconds (10 = 1 second) */

typedef enum {
    PARITY_NONE,
    PARITY_EVEN,
    PARITY_ODD
} parity_mode_t;

/**
 * @brief Open and configure a UART port with specified parity.
 *
 * @param device   Path to the serial device (e.g. "/dev/ttyS0").
 * @param baud     Baud rate constant (e.g. B9600, B115200).
 * @param parity   Parity mode selection.
 * @return File descriptor on success, -1 on failure.
 */
int uart_open(const char *device, speed_t baud, parity_mode_t parity) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("uart_open: open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) != 0) {
        perror("uart_open: tcgetattr");
        close(fd);
        return -1;
    }

    /* Set baud rate */
    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    /* 8-bit characters */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;

    /* Disable modem controls, enable receiver */
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Configure parity */
    tty.c_cflag &= ~(PARENB | PARODD);   /* clear parity bits first */
    switch (parity) {
        case PARITY_EVEN:
            tty.c_cflag |= PARENB;           /* enable parity */
            /* PARODD cleared = even parity */
            break;
        case PARITY_ODD:
            tty.c_cflag |= (PARENB | PARODD);  /* enable parity + odd */
            break;
        case PARITY_NONE:
        default:
            /* already cleared */
            break;
    }

    /* 1 stop bit */
    tty.c_cflag &= ~CSTOPB;

    /* Disable hardware flow control */
    tty.c_cflag &= ~CRTSCTS;

    /* Raw input — no canonical processing */
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_iflag = 0;

    /* Enable parity error marking in input stream.
     * INPCK: enable input parity checking
     * PARMRK: mark parity errors with \377 \0 <byte> sequence */
    if (parity != PARITY_NONE) {
        tty.c_iflag |= (INPCK | PARMRK);
    }

    /* Blocking read with timeout */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = READ_TIMEOUT_DS;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * @brief Read bytes and detect parity errors.
 *
 * When PARMRK is set, a parity-error byte X is delivered as:
 *   0xFF 0x00 X
 * A genuine 0xFF byte is delivered as:
 *   0xFF 0xFF
 * This function decodes that marking and reports parity errors.
 *
 * @param fd        File descriptor from uart_open().
 * @param buf       Output buffer for decoded bytes.
 * @param buf_len   Size of output buffer.
 * @param err_mask  Output bitmask; bit N set if buf[N] had a parity error.
 * @return Number of decoded bytes, or -1 on read error.
 */
int uart_read_with_parity_check(int fd, uint8_t *buf, size_t buf_len,
                                 uint64_t *err_mask) {
    /* Raw buffer: up to 3x larger because of PARMRK escape sequences */
    uint8_t raw[buf_len * 3];
    *err_mask = 0;

    ssize_t n = read(fd, raw, sizeof(raw));
    if (n < 0) {
        perror("uart_read_with_parity_check: read");
        return -1;
    }

    size_t ri = 0;   /* raw index */
    size_t di = 0;   /* decoded index */

    while (ri < (size_t)n && di < buf_len) {
        if (raw[ri] == 0xFF) {
            if (ri + 1 < (size_t)n && raw[ri + 1] == 0x00) {
                /* Parity error sequence: 0xFF 0x00 <byte> */
                if (ri + 2 < (size_t)n) {
                    buf[di] = raw[ri + 2];
                    *err_mask |= (1ULL << di);  /* flag this byte */
                    printf("[PARITY ERROR] Corrupted byte at index %zu: 0x%02X\n",
                           di, buf[di]);
                    di++;
                    ri += 3;
                } else {
                    break;  /* incomplete sequence */
                }
            } else if (ri + 1 < (size_t)n && raw[ri + 1] == 0xFF) {
                /* Escaped genuine 0xFF */
                buf[di++] = 0xFF;
                ri += 2;
            } else {
                buf[di++] = raw[ri++];
            }
        } else {
            buf[di++] = raw[ri++];
        }
    }

    return (int)di;
}

/* --- Demo main -------------------------------------------------------- */

int main(void) {
    int fd = uart_open(UART_DEVICE, BAUD_RATE, PARITY_EVEN);
    if (fd < 0) {
        fprintf(stderr, "Failed to open UART.\n");
        return EXIT_FAILURE;
    }

    printf("UART opened with even parity. Waiting for data...\n");

    uint8_t  buf[64];
    uint64_t err_mask = 0;

    int n = uart_read_with_parity_check(fd, buf, sizeof(buf), &err_mask);
    if (n > 0) {
        printf("Received %d bytes.\n", n);
        for (int i = 0; i < n; i++) {
            char flag = (err_mask >> i) & 1 ? '!' : ' ';
            printf("  [%02d] 0x%02X '%c' %c\n", i, buf[i],
                   (buf[i] >= 0x20 && buf[i] < 0x7F) ? buf[i] : '.', flag);
        }
        if (err_mask)
            printf("Note: '!' marks bytes with detected parity errors.\n");
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### 8.3 STM32 HAL — Parity Error Interrupt Handling

```c
/* stm32_uart_parity.c — STM32 HAL example for parity error detection */

#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* ---- Globals --------------------------------------------------------- */

UART_HandleTypeDef huart2;

#define RX_BUFFER_SIZE  64
static uint8_t  rx_buffer[RX_BUFFER_SIZE];
static uint32_t parity_error_count = 0;
static uint32_t bytes_received     = 0;

/* ---- UART2 Initialization -------------------------------------------- */

/**
 * @brief Initialise USART2 with even parity at 9600 baud.
 *        Pin mapping: PA2=TX, PA3=RX (STM32F4 default).
 */
void MX_USART2_UART_Init(void) {
    huart2.Instance          = USART2;
    huart2.Init.BaudRate     = 9600;
    huart2.Init.WordLength   = UART_WORDLENGTH_9B;  /* 9-bit frame: 8 data + parity */
    huart2.Init.StopBits     = UART_STOPBITS_1;
    huart2.Init.Parity       = UART_PARITY_EVEN;    /* even parity */
    huart2.Init.Mode         = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart2) != HAL_OK) {
        /* Initialisation failed — signal via LED or log */
        Error_Handler();
    }

    /* Enable the parity error interrupt (PEIE bit in CR1) */
    __HAL_UART_ENABLE_IT(&huart2, UART_IT_PE);

    /* Start interrupt-driven receive */
    HAL_UART_Receive_IT(&huart2, rx_buffer, 1);
}

/* ---- HAL Callbacks ---------------------------------------------------- */

/**
 * @brief Called by HAL when one byte is received successfully.
 *        Restart the interrupt receive for the next byte.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {
        bytes_received++;

        /* Process rx_buffer[0] here (e.g., push to ring buffer) */
        /* For demo: echo it back */
        HAL_UART_Transmit(&huart2, rx_buffer, 1, HAL_MAX_DELAY);

        /* Re-arm the receiver */
        HAL_UART_Receive_IT(&huart2, rx_buffer, 1);
    }
}

/**
 * @brief Called by HAL when a UART error is detected.
 *        Inspect huart->ErrorCode to identify parity vs other errors.
 */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART2) {

        /* ---- Parity Error ---- */
        if (huart->ErrorCode & HAL_UART_ERROR_PE) {
            parity_error_count++;

            /* Read the corrupted byte from DR to clear the PE flag.
             * On STM32F4 the PE flag is cleared by reading SR then DR. */
            volatile uint32_t sr  = huart->Instance->SR;
            volatile uint32_t dr  = huart->Instance->DR;
            (void)sr; (void)dr;   /* suppress unused-variable warnings */

            /* Log / signal the error */
            printf("[PE] Parity error #%lu detected\r\n",
                   (unsigned long)parity_error_count);
        }

        /* ---- Framing Error (also common in noisy environments) ---- */
        if (huart->ErrorCode & HAL_UART_ERROR_FE) {
            printf("[FE] Framing error detected\r\n");
        }

        /* ---- Overrun Error ---- */
        if (huart->ErrorCode & HAL_UART_ERROR_ORE) {
            printf("[ORE] Overrun error detected\r\n");
        }

        /* Re-arm the receiver regardless of error type */
        HAL_UART_Receive_IT(&huart2, rx_buffer, 1);
    }
}

/* ---- Transmit with parity -------------------------------------------- */

/**
 * @brief Transmit a buffer over UART2 with even parity enabled.
 *        The hardware handles parity bit insertion automatically.
 */
HAL_StatusTypeDef uart_transmit_with_parity(const uint8_t *data, uint16_t len) {
    return HAL_UART_Transmit(&huart2, (uint8_t *)data, len, HAL_MAX_DELAY);
}

/* ---- Statistics ------------------------------------------------------- */

void uart_print_statistics(void) {
    printf("=== UART Statistics ===\r\n");
    printf("Bytes received   : %lu\r\n", (unsigned long)bytes_received);
    printf("Parity errors    : %lu\r\n", (unsigned long)parity_error_count);
    if (bytes_received > 0) {
        printf("Error rate       : %.3f%%\r\n",
               100.0f * parity_error_count / bytes_received);
    }
}
```

---

### 8.4 Low-Level Register Access (16550 UART / Bare-Metal x86)

```c
/* 16550_parity.c — Direct register access for PC 16550 UART */

#include <stdint.h>
#include <stdbool.h>

/* ---- 16550 register offsets from UART base (I/O port) --------------- */
#define UART_RBR   0  /* Receive Buffer Register  (read)  */
#define UART_THR   0  /* Transmit Holding Register (write) */
#define UART_IER   1  /* Interrupt Enable Register         */
#define UART_IIR   2  /* Interrupt Ident. Register (read)  */
#define UART_FCR   2  /* FIFO Control Register    (write)  */
#define UART_LCR   3  /* Line Control Register             */
#define UART_MCR   4  /* Modem Control Register            */
#define UART_LSR   5  /* Line Status Register              */
#define UART_MSR   6  /* Modem Status Register             */
#define UART_DLL   0  /* Divisor Latch Low  (when DLAB=1)  */
#define UART_DLH   1  /* Divisor Latch High (when DLAB=1)  */

/* LCR bits */
#define LCR_WLS8    0x03   /* 8-bit word length              */
#define LCR_PARENB  0x08   /* Parity enable                  */
#define LCR_PAREVN  0x10   /* Even parity (0 = odd)          */
#define LCR_STKPAR  0x20   /* Stick parity (mark/space)      */
#define LCR_DLAB    0x80   /* Divisor Latch Access Bit       */

/* LSR bits */
#define LSR_DR      0x01   /* Data Ready                     */
#define LSR_OE      0x02   /* Overrun Error                  */
#define LSR_PE      0x04   /* Parity Error ← key flag        */
#define LSR_FE      0x08   /* Framing Error                  */
#define LSR_BI      0x10   /* Break Interrupt                */
#define LSR_THRE    0x20   /* THR Empty                      */

/* IER bits */
#define IER_ERBFI   0x01   /* Enable Received Data Available IRQ */
#define IER_ELSI    0x04   /* Enable Receiver Line Status   IRQ  */

/* Platform I/O port access — replace with your BSP's equivalent */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile ("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ---- UART initialisation --------------------------------------------- */

#define COM1_BASE  0x3F8U

/**
 * @brief Configure COM1 for 9600 8E1 (8 data, even parity, 1 stop bit).
 */
void uart16550_init_even_parity(void) {
    uint16_t base = COM1_BASE;

    /* Disable interrupts during configuration */
    outb(base + UART_IER, 0x00);

    /* Set DLAB to access divisor latches */
    outb(base + UART_LCR, LCR_DLAB);

    /* Baud rate divisor for 9600 bps from 1.8432 MHz clock:
     *   divisor = 1843200 / (16 * 9600) = 12 */
    outb(base + UART_DLL, 0x0C);   /* divisor low  */
    outb(base + UART_DLH, 0x00);   /* divisor high */

    /* 8 data bits, even parity, 1 stop bit — clear DLAB */
    outb(base + UART_LCR, LCR_WLS8 | LCR_PARENB | LCR_PAREVN);

    /* Enable and reset FIFOs (FCR) — 14-byte trigger level */
    outb(base + UART_FCR, 0xC7);

    /* Enable interrupts: Data Available + Line Status (parity errors) */
    outb(base + UART_IER, IER_ERBFI | IER_ELSI);
}

/* ---- Receive byte with parity check ---------------------------------- */

typedef struct {
    uint8_t  data;          /* The received byte          */
    bool     parity_error;  /* true if PE flag was set    */
    bool     framing_error; /* true if FE flag was set    */
    bool     overrun_error; /* true if OE flag was set    */
    bool     data_ready;    /* true if valid data present */
} uart_rx_result_t;

/**
 * @brief Poll for a received byte and capture any error flags.
 *        Clears LSR error flags by reading the register.
 */
uart_rx_result_t uart16550_receive(void) {
    uart_rx_result_t result = {0};
    uint16_t base = COM1_BASE;

    uint8_t lsr = inb(base + UART_LSR);

    result.parity_error  = (lsr & LSR_PE)  != 0;
    result.framing_error = (lsr & LSR_FE)  != 0;
    result.overrun_error = (lsr & LSR_OE)  != 0;
    result.data_ready    = (lsr & LSR_DR)  != 0;

    if (result.data_ready) {
        result.data = inb(base + UART_RBR);  /* reading clears DR */
    }

    return result;
}

/**
 * @brief Transmit a single byte (polling, waits for THR empty).
 */
void uart16550_transmit(uint8_t byte) {
    uint16_t base = COM1_BASE;
    /* Wait until Transmit Holding Register is empty */
    while (!(inb(base + UART_LSR) & LSR_THRE))
        ;
    outb(base + UART_THR, byte);
}

/* ---- Usage example ---------------------------------------------------- */

void uart_poll_loop(void) {
    uart16550_init_even_parity();

    while (1) {
        uart_rx_result_t rx = uart16550_receive();

        if (!rx.data_ready)
            continue;

        if (rx.parity_error) {
            /* Handle parity error: discard byte, request retransmit, etc. */
            /* For RS-485/Modbus: send NACK (0x15) */
            uart16550_transmit(0x15);  /* NAK character */
            continue;
        }

        if (rx.framing_error || rx.overrun_error) {
            /* Other line errors — typically flush the buffer and resync */
            continue;
        }

        /* Valid data — process or echo */
        uart16550_transmit(rx.data);
    }
}
```

---

## 9. Rust Implementation

Rust's strong type system and ownership model are well suited for UART parity error handling — errors are modelled explicitly as types, preventing silent data corruption.

### 9.1 Software Parity Computation (no-std)

```rust
//! uart_parity.rs — no_std software parity for embedded Rust

#![no_std]

/// Parity mode selection.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParityMode {
    None,
    Even,
    Odd,
    Mark,   // always 1
    Space,  // always 0
}

/// Error type returned when a parity check fails.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ParityError {
    /// The received parity bit did not match the expected value.
    Mismatch { data: u8, received: u8, expected: u8 },
}

impl core::fmt::Display for ParityError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            ParityError::Mismatch { data, received, expected } => write!(
                f,
                "Parity error on byte 0x{:02X}: received bit={}, expected bit={}",
                data, received, expected
            ),
        }
    }
}

/// Compute the parity bit value for a given byte and mode.
///
/// # Arguments
/// * `data` — The 8-bit data byte to compute parity for.
/// * `mode` — The parity mode to use.
///
/// # Returns
/// The parity bit value (0 or 1), or `None` for `ParityMode::None`.
pub fn compute_parity_bit(data: u8, mode: ParityMode) -> Option<u8> {
    let ones = data.count_ones() as u8;
    match mode {
        ParityMode::None  => None,
        ParityMode::Even  => Some(ones % 2),        // 0 if even, 1 if odd
        ParityMode::Odd   => Some(1 - (ones % 2)),  // flip of even
        ParityMode::Mark  => Some(1),
        ParityMode::Space => Some(0),
    }
}

/// Validate a received byte against its received parity bit.
///
/// # Returns
/// * `Ok(data)` — parity matches; data is considered valid.
/// * `Err(ParityError::Mismatch)` — parity mismatch detected.
pub fn validate_parity(
    data: u8,
    received_parity: u8,
    mode: ParityMode,
) -> Result<u8, ParityError> {
    match compute_parity_bit(data, mode) {
        None => Ok(data),  // parity disabled, always accept
        Some(expected) => {
            if received_parity & 0x01 == expected {
                Ok(data)
            } else {
                Err(ParityError::Mismatch {
                    data,
                    received: received_parity & 0x01,
                    expected,
                })
            }
        }
    }
}

/// Encode a byte as a 9-bit value (8 data bits + parity bit).
///
/// Returns `(data_byte, parity_bit)`.
pub fn encode_with_parity(data: u8, mode: ParityMode) -> (u8, u8) {
    let parity = compute_parity_bit(data, mode).unwrap_or(0);
    (data, parity)
}

// ---- Tests (run with: cargo test) -------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_even_parity_even_count() {
        // 0x4B = 0100_1011 → four 1s → parity bit = 0 (already even)
        assert_eq!(compute_parity_bit(0x4B, ParityMode::Even), Some(0));
    }

    #[test]
    fn test_even_parity_odd_count() {
        // 0x47 = 0100_0111 → four 1s... wait: 0100_0111 = 3 ones
        // Actually: 0x47 = 0100 0111 → bits 0,1,2,6 → four 1s... 
        // Let me use a clear example: 0x01 = 0000_0001 → one 1 → parity bit = 1
        assert_eq!(compute_parity_bit(0x01, ParityMode::Even), Some(1));
    }

    #[test]
    fn test_odd_parity() {
        // 0x4B has four 1s → even → odd parity bit = 1
        assert_eq!(compute_parity_bit(0x4B, ParityMode::Odd), Some(1));
    }

    #[test]
    fn test_validate_ok() {
        let data = 0x4B_u8;
        let parity_bit = compute_parity_bit(data, ParityMode::Even).unwrap();
        assert_eq!(validate_parity(data, parity_bit, ParityMode::Even), Ok(data));
    }

    #[test]
    fn test_validate_error() {
        let data = 0x4B_u8;
        // Corrupt one bit: 0x4B ^ 0x04 = 0x4F
        let corrupted = data ^ 0x04;
        let original_parity = compute_parity_bit(data, ParityMode::Even).unwrap();
        // Now corrupted has different parity → should fail
        let result = validate_parity(corrupted, original_parity, ParityMode::Even);
        assert!(result.is_err());
    }

    #[test]
    fn test_mark_parity_always_one() {
        for b in 0..=255_u8 {
            assert_eq!(compute_parity_bit(b, ParityMode::Mark), Some(1));
        }
    }

    #[test]
    fn test_space_parity_always_zero() {
        for b in 0..=255_u8 {
            assert_eq!(compute_parity_bit(b, ParityMode::Space), Some(0));
        }
    }
}
```

---

### 9.2 Linux Serial Port with `serialport` Crate

```toml
# Cargo.toml
[dependencies]
serialport = "4"
```

```rust
//! uart_parity_linux.rs — UART parity error handling on Linux using the
//! `serialport` crate.

use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};
use std::io::{self, Read};
use std::time::Duration;

/// Wrap an open serial port with parity-error statistics.
pub struct ParityCheckedUart {
    port:          Box<dyn SerialPort>,
    bytes_read:    u64,
    parity_errors: u64,
}

impl ParityCheckedUart {
    /// Open a serial port with the specified parity mode.
    ///
    /// # Example
    /// ```no_run
    /// let uart = ParityCheckedUart::open("/dev/ttyS0", 9600, Parity::Even)?;
    /// ```
    pub fn open(
        device: &str,
        baud_rate: u32,
        parity: Parity,
    ) -> Result<Self, serialport::Error> {
        let port = serialport::new(device, baud_rate)
            .data_bits(DataBits::Eight)
            .stop_bits(StopBits::One)
            .parity(parity)
            .flow_control(FlowControl::None)
            .timeout(Duration::from_secs(1))
            .open()?;

        Ok(ParityCheckedUart {
            port,
            bytes_read: 0,
            parity_errors: 0,
        })
    }

    /// Read bytes into `buf`.
    ///
    /// The `serialport` crate surfaces parity errors through I/O errors
    /// with kind `InvalidData`. We catch these and update statistics.
    pub fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match self.port.read(buf) {
            Ok(n) => {
                self.bytes_read += n as u64;
                Ok(n)
            }
            Err(e) if e.kind() == io::ErrorKind::InvalidData => {
                // Parity or framing error reported by the driver
                self.parity_errors += 1;
                eprintln!(
                    "[PE #{:4}] Parity error detected: {}",
                    self.parity_errors, e
                );
                // Return 0 bytes — caller can decide to retry or abort
                Ok(0)
            }
            Err(e) => Err(e),
        }
    }

    /// Write bytes to the serial port.
    pub fn write(&mut self, data: &[u8]) -> io::Result<usize> {
        use std::io::Write;
        self.port.write(data)
    }

    /// Print cumulative error statistics.
    pub fn print_stats(&self) {
        println!("=== UART Parity Statistics ===");
        println!("  Bytes read     : {}", self.bytes_read);
        println!("  Parity errors  : {}", self.parity_errors);
        if self.bytes_read > 0 {
            let rate = 100.0 * self.parity_errors as f64 / self.bytes_read as f64;
            println!("  Error rate     : {:.4}%", rate);
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut uart = ParityCheckedUart::open("/dev/ttyS0", 9600, Parity::Even)?;

    println!("Listening on /dev/ttyS0 @ 9600 baud, even parity...");

    let mut buf = [0u8; 64];
    let mut total = 0usize;

    loop {
        let n = uart.read(&mut buf)?;
        total += n;

        for i in 0..n {
            let c = buf[i];
            let printable = if c.is_ascii_graphic() { c as char } else { '.' };
            print!("0x{:02X}({}) ", c, printable);
        }
        if n > 0 {
            println!();
        }

        if total >= 256 {
            break;
        }
    }

    uart.print_stats();
    Ok(())
}
```

---

### 9.3 Embedded Rust — STM32 with `stm32f4xx-hal`

```toml
# Cargo.toml
[dependencies]
cortex-m        = "0.7"
cortex-m-rt     = "0.7"
stm32f4xx-hal   = { version = "0.21", features = ["stm32f411"] }
nb              = "1.0"
```

```rust
//! stm32_uart_parity.rs — Parity error handling on STM32F4 in embedded Rust.
//!
//! USART2 configured: 9600 baud, 8E1 (8 data, even parity, 1 stop).
//! PA2 = TX, PA3 = RX.

#![no_std]
#![no_main]

use cortex_m_rt::entry;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::{Config, Parity, StopBits, WordLength}, Event, Serial},
};
use nb::block;

/// Application-level error types for UART receive operations.
#[derive(Debug)]
pub enum UartRxError {
    /// Hardware-reported parity error on the received byte.
    ParityError(u8),
    /// Hardware-reported framing error.
    FramingError,
    /// Receiver overrun — data was lost.
    Overrun,
    /// No data available yet (non-blocking read).
    WouldBlock,
}

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();

    // Clock setup
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    // GPIO
    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate::<7>();
    let rx_pin = gpioa.pa3.into_alternate::<7>();

    // Configure USART2 with even parity
    // Note: when parity is enabled on STM32, set WordLength to 9 bits
    // so that the parity bit fits without consuming a data bit.
    let config = Config::default()
        .baudrate(9600.bps())
        .wordlength_9()          // 9-bit frame to accommodate parity bit
        .parity_even()           // even parity
        .stopbits(StopBits::STOP1);

    let serial = Serial::new(dp.USART2, (tx_pin, rx_pin), config, &clocks)
        .unwrap();

    let (mut tx, mut rx) = serial.split();

    // Enable parity error interrupt
    // (In a real application this would use NVIC and a proper ISR)
    // Here we use polling with manual error-flag inspection.

    let mut parity_error_count: u32 = 0;
    let mut bytes_received:     u32 = 0;
    let mut response_buf = [0u8; 1];

    loop {
        // Read one byte, blocking until data arrives
        match receive_byte_checked(&mut rx) {
            Ok(byte) => {
                bytes_received += 1;
                // Echo valid data back
                response_buf[0] = byte;
                block!(tx.write(byte)).ok();
            }
            Err(UartRxError::ParityError(corrupted_byte)) => {
                parity_error_count += 1;
                // Send NAK to indicate error to sender
                block!(tx.write(0x15_u8)).ok(); // NAK
                // Optionally log: parity_error_count, corrupted_byte
                let _ = corrupted_byte; // suppress warning in this demo
            }
            Err(UartRxError::Overrun) => {
                // Flush and resync
                block!(tx.write(0x15_u8)).ok();
            }
            Err(UartRxError::WouldBlock) => {
                // Non-blocking: no data yet, continue loop
                cortex_m::asm::nop();
            }
            Err(_) => {}
        }
    }
}

/// Read one byte from the UART receiver, checking all error flags.
///
/// On STM32F4 HAL, receiver errors surface via the `nb::Error` type.
/// We decompose the result to provide specific error variants.
fn receive_byte_checked(
    rx: &mut impl embedded_hal::serial::Read<u8, Error = stm32f4xx_hal::serial::Error>,
) -> Result<u8, UartRxError> {
    match rx.read() {
        Ok(byte) => Ok(byte),
        Err(nb::Error::Other(stm32f4xx_hal::serial::Error::Parity)) => {
            // The corrupted byte is lost here at the HAL abstraction level.
            // For the raw byte, access the DR register directly.
            Err(UartRxError::ParityError(0x00))
        }
        Err(nb::Error::Other(stm32f4xx_hal::serial::Error::Framing)) => {
            Err(UartRxError::FramingError)
        }
        Err(nb::Error::Other(stm32f4xx_hal::serial::Error::Overrun)) => {
            Err(UartRxError::Overrun)
        }
        Err(nb::Error::WouldBlock) => Err(UartRxError::WouldBlock),
        Err(_) => Err(UartRxError::FramingError),
    }
}
```

---

### 9.4 Interrupt-Driven Ring Buffer with Parity Tracking (Rust + `bare-metal`)

```rust
//! ring_buffer_uart.rs — Lock-free ring buffer for UART RX with parity
//! error flags, suitable for bare-metal embedded Rust (no_std, no heap).

#![no_std]

use core::sync::atomic::{AtomicBool, AtomicU8, AtomicUsize, Ordering};

const BUFFER_SIZE: usize = 128;

/// A single slot in the ring buffer, holding a byte and its error status.
#[derive(Copy, Clone, Default)]
struct RxSlot {
    data:          u8,
    parity_error:  bool,
    framing_error: bool,
}

/// Ring buffer for UART receive data with per-byte error tracking.
pub struct UartRingBuffer {
    slots:    [RxSlot; BUFFER_SIZE],
    head:     AtomicUsize,  // written by ISR
    tail:     AtomicUsize,  // read  by application
    overflow: AtomicBool,
}

impl UartRingBuffer {
    pub const fn new() -> Self {
        UartRingBuffer {
            slots:    [RxSlot { data: 0, parity_error: false, framing_error: false };
                       BUFFER_SIZE],
            head:     AtomicUsize::new(0),
            tail:     AtomicUsize::new(0),
            overflow: AtomicBool::new(false),
        }
    }

    /// Called from ISR: push a received byte with its error flags.
    ///
    /// # Safety
    /// Must only be called from a single ISR context.
    pub unsafe fn push_from_isr(
        &self,
        data: u8,
        parity_error: bool,
        framing_error: bool,
    ) {
        let head = self.head.load(Ordering::Relaxed);
        let next_head = (head + 1) % BUFFER_SIZE;
        let tail = self.tail.load(Ordering::Acquire);

        if next_head == tail {
            // Buffer full — mark overflow, discard byte
            self.overflow.store(true, Ordering::Relaxed);
            return;
        }

        // SAFETY: only the ISR writes to slots[head], only main reads after
        // head is advanced.
        let slot_ptr = self.slots.as_ptr().add(head) as *mut RxSlot;
        slot_ptr.write(RxSlot { data, parity_error, framing_error });

        self.head.store(next_head, Ordering::Release);
    }

    /// Pop one byte from the ring buffer.
    ///
    /// Returns `None` if the buffer is empty.
    pub fn pop(&self) -> Option<RxSlot> {
        let tail = self.tail.load(Ordering::Relaxed);
        let head = self.head.load(Ordering::Acquire);

        if tail == head {
            return None;  // empty
        }

        // SAFETY: we own the tail slot; ISR only advances head.
        let slot = unsafe { self.slots.as_ptr().add(tail).read() };
        self.tail.store((tail + 1) % BUFFER_SIZE, Ordering::Release);
        Some(slot)
    }

    /// Check and clear the overflow flag.
    pub fn take_overflow(&self) -> bool {
        self.overflow.swap(false, Ordering::AcqRel)
    }

    pub fn is_empty(&self) -> bool {
        self.tail.load(Ordering::Relaxed) == self.head.load(Ordering::Acquire)
    }
}

// Global instance — accessible from both ISR and main
static RX_BUFFER: UartRingBuffer = UartRingBuffer::new();

/// Application main loop processing bytes from the ring buffer.
pub fn process_received_bytes() -> (u32, u32) {
    let mut total: u32 = 0;
    let mut errors: u32 = 0;

    while let Some(slot) = RX_BUFFER.pop() {
        total += 1;

        if slot.parity_error {
            errors += 1;
            // Discard corrupted byte; do NOT pass to protocol layer
            continue;
        }

        if slot.framing_error {
            // Framing error often indicates baud rate mismatch
            // Could trigger a baud-rate auto-detection routine
            continue;
        }

        // Valid byte — pass to higher-level handler
        handle_valid_byte(slot.data);
    }

    if RX_BUFFER.take_overflow() {
        // Ring buffer overflowed — higher-layer resync needed
        handle_overflow();
    }

    (total, errors)
}

fn handle_valid_byte(_byte: u8) {
    // Application-specific processing
}

fn handle_overflow() {
    // Application-specific overflow recovery
}
```

---

## 10. Interrupt-Driven Parity Error Handling

In production systems, parity errors should be handled via hardware interrupts rather than polling, to minimise latency and avoid missing errors in high-throughput scenarios.

### Typical ISR Flow (Any MCU)

```
UART RX Interrupt fires
       │
       ▼
Read Line Status Register (LSR / SR / UCSRA)
       │
       ├── PE flag set?  YES ──► Increment parity_error_count
       │                         Read data register (clears flags)
       │                         Push corrupted_byte with error=true to ring buffer
       │                         If NACK needed: queue NACK in TX buffer
       │
       ├── FE flag set?  YES ──► Increment framing_error_count
       │                         Possibly trigger baud-rate resync
       │
       └── No error?          ► Read data byte normally
                                Push to ring buffer with error=false
```

### Error Recovery Strategies

| Strategy | When to Use | Mechanism |
|---|---|---|
| **Discard & continue** | Best-effort streams (sensor telemetry) | Drop byte, increment counter |
| **Request retransmit (NAK)** | Acknowledged protocols (Modbus, custom) | Send NAK character, expect repeat |
| **Frame resync** | Fixed-length frame protocols | Discard until valid start sequence |
| **Baud-rate check** | Parity AND framing errors together | Auto-detect baud from break character |
| **Escalate alarm** | Safety-critical channels | Raise fault flag, halt or switch to backup |

---

## 11. Practical Strategies for Robust Systems

### 11.1 Combine Parity with Higher-Layer CRC

Parity protects individual frames; CRC protects messages:

```
[START][DATA x N bytes][CRC16] ← message level
  each byte has a parity bit   ← frame level (hardware)
```

A parity error on any byte invalidates the CRC, providing two independent detection layers.

### 11.2 Monitor Error Rate Over Time

```c
/* Maintain a rolling error rate. Trigger alarm if > threshold. */
#define ERROR_RATE_THRESHOLD_PPM  1000  /* 0.1% */

void check_error_rate(uint32_t bytes, uint32_t errors) {
    if (bytes < 100) return;   /* need statistical significance */
    uint32_t ppm = (errors * 1000000UL) / bytes;
    if (ppm > ERROR_RATE_THRESHOLD_PPM) {
        trigger_line_quality_alarm(ppm);
    }
}
```

### 11.3 Parity Configuration Checklist

Before deploying a UART link with parity, verify:

- Both ends use the **same parity mode** (even/odd/none).
- Both ends use the **same word length** (8-bit data + parity = 9-bit frame on many MCUs).
- The baud rates are **matched within ±2%** — drift causes sample-point errors that manifest as parity errors.
- Cable length and **termination** are appropriate for the baud rate.
- Ground reference is **shared** between transmitter and receiver.

### 11.4 Distinguishing Parity Errors from Configuration Errors

If you receive **continuous parity errors** from the first byte:

1. Check parity mode configuration on both ends (even vs odd mismatch).
2. Check word length (8-bit vs 9-bit frame mismatch is common on STM32).
3. Verify baud rate.
4. Probe with a logic analyser to observe the raw bit stream.

Intermittent parity errors (occasional) suggest a noise or electrical issue. Constant errors suggest a configuration mismatch.

---

## 12. Summary

Parity error detection is the most basic yet universally supported error-detection mechanism in UART communication. A single extra bit appended to each transmitted byte allows the receiver's hardware to flag corrupted frames in real time.

**Key takeaways:**

- **Even parity** (most common): total `1` bits must be even; parity bit adjusted accordingly.
- **Odd parity**: total `1` bits must be odd.
- **Mark/Space**: fixed parity bit used for protocol signalling, not error detection.
- Parity detects **single-bit errors** reliably, but misses **2-bit (even-count)** errors.
- Hardware handles parity insertion (TX) and validation (RX) automatically. Software only needs to configure the mode and check the PE flag.
- On Linux/POSIX, use `termios` with `INPCK | PARMRK` to receive parity-error-marked bytes in the data stream.
- On embedded MCUs (STM32, AVR, etc.), enable the PE interrupt (`PEIE`) and clear the error by reading the status and data registers.
- In Rust, model parity errors as explicit `Result<u8, ParityError>` types to ensure the compiler forces error handling at every call site.
- In production systems, supplement hardware parity with **CRC at the message level** and **error-rate monitoring** to detect degrading line quality before it causes failures.

Parity is a first line of defence — lightweight, zero-overhead in normal operation, and universally available. Used correctly, it catches the majority of real-world single-bit noise events and gives the system a reliable indication that data integrity has been compromised.

---

*Document: 18 — Parity Errors | UART Programming Reference Series*