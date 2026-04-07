# 54. Nordic nRF UART — EasyDMA-Backed Serial Communication on nRF52 and nRF53

**Hardware & Architecture**
- Full EasyDMA engine diagram and transfer lifecycle (TX and RX state machines)
- Peripheral instances across nRF52832, nRF52840, and nRF5340 (APP + NET cores)
- PSEL register format, baud rate constants table, interrupt event reference

**C/C++ Programming — three levels of abstraction**
- **Raw registers** — direct `NRF_UARTE0->TXD.PTR/MAXCNT/TASKS_*` with a full ISR
- **nrfx HAL** — `nrfx_uarte_init/tx/rx` with event handler and double-buffer swap
- **Zephyr async API** — `uart_callback_set`, `uart_rx_enable`, device tree overlay included

**Rust — two approaches**
- **nrf-hal + RTIC 2.x** — type-safe peripheral ownership, interrupt-driven echo task
- **Embassy async** — split TX/RX halves, `async fn write` / `read_until_idle`, concurrent task spawning

**Advanced Topics**
- Ping-pong RX double buffering with zero-gap re-arming (critical at high baud rates)
- PPI chain for autonomous `ENDTX → STOPTX` without CPU involvement
- nRF5340 shared RAM restriction (EasyDMA cannot access peripheral RAM)
- Debugging checklist covering HFCLK, PSEL, address range, and baud register verification

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Architecture](#hardware-architecture)
3. [Peripheral Variants](#peripheral-variants)
4. [EasyDMA Engine](#easydma-engine)
5. [Pin Mapping and GPIO Configuration](#pin-mapping-and-gpio-configuration)
6. [Clock and Baud Rate Generation](#clock-and-baud-rate-generation)
7. [Interrupts and Events](#interrupts-and-events)
8. [Flow Control (RTS/CTS)](#flow-control-rtscts)
9. [UARTE vs Bare UART](#uarte-vs-bare-uart)
10. [Programming with Nordic's nrfx HAL (C/C++)](#programming-with-nordics-nrfx-hal-cc)
11. [Low-Level Register Programming (C)](#low-level-register-programming-c)
12. [Zephyr RTOS Integration (C)](#zephyr-rtos-integration-c)
13. [Rust Implementation (nrf-hal + RTIC)](#rust-implementation-nrf-hal--rtic)
14. [Rust with Embassy (async/await)](#rust-with-embassy-asyncawait)
15. [Double Buffering and Circular Reception](#double-buffering-and-circular-reception)
16. [Low-Power Considerations](#low-power-considerations)
17. [Common Pitfalls and Debugging](#common-pitfalls-and-debugging)
18. [Summary](#summary)

---

## Overview

Nordic Semiconductor's nRF52 and nRF53 series microcontrollers implement UART through the **UARTE** (Universal Asynchronous Receiver/Transmitter with EasyDMA) peripheral. Unlike classic UART peripherals that interrupt the CPU for every byte, UARTE uses the **EasyDMA** engine to move data between RAM and the serial shift register autonomously, freeing the CPU for other tasks and enabling true zero-copy transfers.

Key attributes:

- Standard asynchronous serial protocol (start bit, 8 data bits, optional parity, 1 stop bit)
- Baud rates from **1200** to **1 Mbps** (hardware-generated, not software-bit-banged)
- Hardware **RTS/CTS** flow control
- EasyDMA for both TX and RX — transfers happen without per-byte CPU involvement
- **PPI** (Programmable Peripheral Interconnect) triggers allow fully autonomous operation
- Flexible **PSEL** (pin select) registers: any GPIO can be the UART pin
- nRF52840 and nRF5340 each expose **two** UARTE instances

---

## Hardware Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                          UARTE Peripheral                        │
│                                                                  │
│  ┌──────────────┐    ┌───────────────────────────────────────┐  │
│  │   PSEL Regs  │───▶│  GPIO Mux  ──▶ TXD / RXD / RTS / CTS │  │
│  └──────────────┘    └───────────────────────────────────────┘  │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐   │
│  │                     EasyDMA Engine                        │   │
│  │                                                           │   │
│  │  TX: RAM PTR ──▶ [MAXCNT bytes] ──▶ Shift Reg ──▶ TXD   │   │
│  │  RX: RXD ──▶ Shift Reg ──▶ [MAXCNT bytes] ──▶ RAM PTR   │   │
│  │                                                           │   │
│  │  Events: TXSTARTED  TXSTOPPED  ENDRX  RXTO  RXSTARTED   │   │
│  └──────────────────────────────────────────────────────────┘   │
│                                                                  │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌────────────┐  │
│  │ BAUDRATE │   │  CONFIG  │   │  ENABLE  │   │  INTEN     │  │
│  └──────────┘   └──────────┘   └──────────┘   └────────────┘  │
└─────────────────────────────────────────────────────────────────┘
         │                                         │
         ▼                                         ▼
    System Bus                              NVIC / IRQ
    (AHB / APB)
```

The peripheral lives on the APB bus. All configuration registers are memory-mapped. EasyDMA reads and writes the **AHB bus** directly, which means the target RAM buffers **must** reside in normal RAM (not flash, not stack of a task whose context may swap). On nRF5340, EasyDMA can only access the **Shared RAM** (`0x2000_0000` region) — peripheral RAM is not DMA-accessible.

---

## Peripheral Variants

| Series     | Instance  | Base Address   | Notes                                     |
|------------|-----------|----------------|-------------------------------------------|
| nRF52832   | UARTE0    | 0x40002000     | Single instance; legacy UART0 at same addr |
| nRF52840   | UARTE0    | 0x40002000     | Full-featured, hardware flow control       |
| nRF52840   | UARTE1    | 0x40028000     | Second instance                            |
| nRF5340 APP| UARTE0    | 0x40008000     | Application core                           |
| nRF5340 APP| UARTE1    | 0x40009000     | Application core                           |
| nRF5340 NET| UARTE0    | 0x41013000     | Network core                               |

> **nRF52832 Note:** The nRF52832 has a shared address space between legacy `UART0` and `UARTE0`. Using both simultaneously is not possible. Always prefer `UARTE0` for new code.

---

## EasyDMA Engine

EasyDMA operates with four registers per direction:

```
TX channel:
  TXD.PTR     — pointer to TX buffer in RAM (must be RAM, 32-bit aligned)
  TXD.MAXCNT  — number of bytes to transmit (0–65535)
  TXD.AMOUNT  — (read-only) bytes actually sent after ENDTX event

RX channel:
  RXD.PTR     — pointer to RX buffer in RAM
  RXD.MAXCNT  — maximum bytes to receive before ENDRX event fires
  RXD.AMOUNT  — (read-only) bytes received after ENDRX
```

### EasyDMA Transfer Lifecycle

```
TX:
  Set TXD.PTR, TXD.MAXCNT
       │
       ▼
  Trigger TASKS_STARTTX
       │
       ▼ (hardware sends bytes autonomously)
       │
  Event: EVENTS_TXSTARTED  ──▶ (optional PPI action)
       │
       ▼ (all bytes sent)
  Event: EVENTS_ENDTX
       │
       ▼
  Trigger TASKS_STOPTX  (or start next buffer)
       │
       ▼
  Event: EVENTS_TXSTOPPED


RX:
  Set RXD.PTR, RXD.MAXCNT
       │
       ▼
  Trigger TASKS_STARTRX
       │
       ▼ (hardware receives bytes autonomously)
       │
  Event: EVENTS_RXSTARTED
       │
  (either MAXCNT bytes received, or TASKS_STOPRX triggered)
       │
  Event: EVENTS_ENDRX  ──▶  RXD.AMOUNT holds count
       │
       ▼
  Event: EVENTS_RXTO  (after STOPRX: RX line idle timeout elapsed)
```

> **Critical:** EasyDMA requires that the buffer pointer points to **RAM** visible by the AHB master. Stack buffers in ISR context are safe, but buffers in `const` sections (flash) will silently fail or hard-fault.

---

## Pin Mapping and GPIO Configuration

UARTE uses `PSEL` registers to route to any GPIO pin. The peripheral controls the GPIO direction automatically when enabled.

```c
// PSEL register format (32-bit):
// Bit 31:    CONNECT  (0 = connected, 1 = disconnected)
// Bits 4:0:  PIN      (0–31)
// Bits 6:5:  PORT     (0–1 on nRF52840, 0 on nRF52832)

#define PSEL_PIN(port, pin)   (((port) << 5) | (pin))
#define PSEL_DISCONNECTED     (1u << 31)

// Example: TXD on P0.06, RXD on P0.08
NRF_UARTE0->PSEL.TXD = PSEL_PIN(0, 6);
NRF_UARTE0->PSEL.RXD = PSEL_PIN(0, 8);
NRF_UARTE0->PSEL.RTS = PSEL_DISCONNECTED;  // no flow control
NRF_UARTE0->PSEL.CTS = PSEL_DISCONNECTED;
```

After writing `PSEL`, enabling the peripheral (`ENABLE = 8`) asserts the GPIO configuration automatically.

---

## Clock and Baud Rate Generation

Baud rates are selected by writing a **pre-computed constant** to the `BAUDRATE` register (not a divider value):

| Baud Rate | BAUDRATE Register Value |
|-----------|------------------------|
| 1200      | `0x0004F000`           |
| 2400      | `0x0009D000`           |
| 4800      | `0x0013B000`           |
| 9600      | `0x00275000`           |
| 14400     | `0x003AF000`           |
| 19200     | `0x004EA000`           |
| 38400     | `0x009D5000`           |
| 57600     | `0x00EBF000`           |
| 115200    | `0x01D7E000`           |
| 230400    | `0x03AFB000`           |
| 460800    | `0x075F7000`           |
| 921600    | `0x0EBED000`           |
| 1000000   | `0x10000000`           |

These constants are defined in `<nrfx/mdk/nrf52840.h>` as `UARTE_BAUDRATE_BAUDRATE_Baud115200`, etc.

The internal baud clock is derived from the 16 MHz HFCLK. No fractional divider is used — all values are exact for the 16 MHz source.

---

## Interrupts and Events

UARTE events that can trigger interrupts (via `INTENSET`/`INTENCLR`):

| Event             | Bit | Description                                         |
|-------------------|-----|-----------------------------------------------------|
| `EVENTS_CTS`      | 0   | CTS line activated                                  |
| `EVENTS_NCTS`     | 1   | CTS line deactivated                                |
| `EVENTS_RXDRDY`   | 2   | Data received into RXD register (byte ready)        |
| `EVENTS_ENDRX`    | 4   | DMA RX buffer full (MAXCNT bytes received)          |
| `EVENTS_TXDRDY`   | 7   | Data sent from TXD register                         |
| `EVENTS_ENDTX`    | 8   | DMA TX buffer empty (all bytes sent)                |
| `EVENTS_ERROR`    | 9   | Error: framing, parity, break, overrun              |
| `EVENTS_RXTO`     | 17  | Receiver timeout (after STOPRX)                     |
| `EVENTS_RXSTARTED`| 19  | DMA RX transfer started                             |
| `EVENTS_TXSTARTED`| 20  | DMA TX transfer started                             |
| `EVENTS_TXSTOPPED`| 22  | Transmitter stopped                                 |

> **RXDRDY** is available in UARTE but is not DMA-backed. For efficient reception, use `ENDRX` (buffer filled) and `RXTO` (timeout after stop) events instead.

---

## Flow Control (RTS/CTS)

Hardware flow control prevents buffer overruns when the receiver cannot keep up.

```
Sender (nRF)              Receiver (peer)
    │                           │
    │─── RTS ───────────────────│  nRF asserts RTS low = "ready to receive"
    │                           │
    │─── TXD ──────────────────▶│
    │                           │
    │◀── CTS ───────────────────│  Peer asserts CTS low = "send to me"
    │                           │
    │ (TX stalled if CTS high)  │
```

Enable hardware flow control in the `CONFIG` register:

```c
NRF_UARTE0->CONFIG = (UARTE_CONFIG_HWFC_Enabled << UARTE_CONFIG_HWFC_Pos) |
                     (UARTE_CONFIG_PARITY_Excluded << UARTE_CONFIG_PARITY_Pos) |
                     (UARTE_CONFIG_STOP_One << UARTE_CONFIG_STOP_Pos);

NRF_UARTE0->PSEL.RTS = PSEL_PIN(0, 5);
NRF_UARTE0->PSEL.CTS = PSEL_PIN(0, 7);
```

---

## UARTE vs Bare UART

| Feature             | UART (legacy)       | UARTE (EasyDMA)         |
|---------------------|---------------------|-------------------------|
| CPU per byte        | Yes (interrupt/poll) | No (DMA burst)         |
| Max throughput      | ~500 kbps practical  | 1 Mbps sustained        |
| Buffer depth        | 1 byte (TXD/RXD reg) | Up to 65535 bytes      |
| RAM requirement     | None                 | User-allocated buffer   |
| nRF52832 address    | 0x40002000 (shared)  | 0x40002000 (shared)    |
| Available on nRF5340| No (removed)         | Yes (UARTE0, UARTE1)   |
| Simultaneous use    | Only one at a time   | Only one at a time      |

---

## Programming with Nordic's nrfx HAL (C/C++)

The **nrfx** library provides a thin, chip-agnostic driver layer above the raw registers. It is used both standalone and as the backend for Zephyr's UART driver.

### Initialization and Basic TX/RX (C)

```c
#include "nrfx_uarte.h"
#include <string.h>

// Allocate a driver instance
static nrfx_uarte_t m_uarte = NRFX_UARTE_INSTANCE(0);  // UARTE0

// RX double-buffer (swap on ENDRX)
static uint8_t rx_buf[2][64];
static volatile uint8_t active_rx = 0;

// ---------------------------------------------------------------
// Event handler — called from interrupt context
// ---------------------------------------------------------------
static void uarte_event_handler(nrfx_uarte_event_t const *p_event,
                                void                      *p_context)
{
    switch (p_event->type)
    {
        case NRFX_UARTE_EVT_RX_DONE:
        {
            size_t len = p_event->data.rxtx.bytes;
            uint8_t *buf = p_event->data.rxtx.p_data;
            // Process received data (len bytes in buf)
            process_received(buf, len);

            // Swap buffers and restart DMA immediately
            active_rx ^= 1;
            nrfx_uarte_rx(&m_uarte, rx_buf[active_rx],
                           sizeof(rx_buf[active_rx]));
            break;
        }

        case NRFX_UARTE_EVT_TX_DONE:
            // Transmit complete; signal a semaphore or set flag
            tx_complete_flag = true;
            break;

        case NRFX_UARTE_EVT_ERROR:
            // p_event->data.error.error_mask holds the error bits
            handle_uart_error(p_event->data.error.error_mask);
            break;

        default:
            break;
    }
}

// ---------------------------------------------------------------
// Driver initialisation
// ---------------------------------------------------------------
void uart_init(void)
{
    nrfx_uarte_config_t config = {
        .pseltxd   = NRF_GPIO_PIN_MAP(0, 6),   // P0.06 = TXD
        .pselrxd   = NRF_GPIO_PIN_MAP(0, 8),   // P0.08 = RXD
        .pselcts   = NRF_UARTE_PSEL_NOT_CONNECTED,
        .pselrts   = NRF_UARTE_PSEL_NOT_CONNECTED,
        .p_context = NULL,
        .baudrate  = NRF_UARTE_BAUDRATE_115200,
        .interrupt_priority = NRFX_UARTE_DEFAULT_CONFIG_IRQ_PRIORITY,
        .config    = {
            .hwfc   = NRF_UARTE_HWFC_DISABLED,
            .parity = NRF_UARTE_PARITY_EXCLUDED,
            .stop   = NRF_UARTE_STOPBITS_ONE,
        },
    };

    nrfx_err_t err = nrfx_uarte_init(&m_uarte, &config, uarte_event_handler);
    NRFX_ASSERT(err == NRFX_SUCCESS);

    // Kick off first RX DMA transfer
    nrfx_uarte_rx(&m_uarte, rx_buf[active_rx], sizeof(rx_buf[active_rx]));
}

// ---------------------------------------------------------------
// Blocking transmit helper
// ---------------------------------------------------------------
void uart_send(const uint8_t *data, size_t len)
{
    tx_complete_flag = false;
    nrfx_err_t err = nrfx_uarte_tx(&m_uarte, data, len);
    NRFX_ASSERT(err == NRFX_SUCCESS);

    // Spin-wait — in production use a semaphore
    while (!tx_complete_flag) { __WFE(); }
}
```

---

## Low-Level Register Programming (C)

Directly programming UARTE registers gives maximum control and is suitable for bootloaders or extremely resource-constrained firmware.

```c
#include <stdint.h>
#include <stdbool.h>
#include "nrf52840.h"    // CMSIS header, defines NRF_UARTE0

// ------------------------------------------------------------------
// 1. Configure
// ------------------------------------------------------------------
void uart_register_init(void)
{
    // Disable before configuring (required by spec)
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Disabled;

    // Pin select: P0.06 TX, P0.08 RX
    NRF_UARTE0->PSEL.TXD = (0 << 5) | 6;    // port 0, pin 6
    NRF_UARTE0->PSEL.RXD = (0 << 5) | 8;    // port 0, pin 8
    NRF_UARTE0->PSEL.RTS = (1UL << 31);      // disconnected
    NRF_UARTE0->PSEL.CTS = (1UL << 31);      // disconnected

    // 115200 baud, no parity, 1 stop bit, no flow control
    NRF_UARTE0->BAUDRATE = 0x01D7E000UL;
    NRF_UARTE0->CONFIG   = 0;

    // Enable interrupts: ENDTX, ENDRX, RXTO, ERROR
    NRF_UARTE0->INTENSET = UARTE_INTENSET_ENDTX_Msk  |
                           UARTE_INTENSET_ENDRX_Msk  |
                           UARTE_INTENSET_RXTO_Msk   |
                           UARTE_INTENSET_ERROR_Msk;

    NVIC_SetPriority(UARTE0_UART0_IRQn, 2);
    NVIC_EnableIRQ(UARTE0_UART0_IRQn);

    // Enable peripheral
    NRF_UARTE0->ENABLE = UARTE_ENABLE_ENABLE_Enabled;
}

// ------------------------------------------------------------------
// 2. Transmit a buffer (non-blocking, DMA)
// ------------------------------------------------------------------
static volatile bool g_tx_done = false;

void uart_tx_start(const uint8_t *buf, uint16_t len)
{
    // Buffer MUST be in RAM — check address if paranoid:
    // assert(((uint32_t)buf & 0xE0000000) == 0x20000000);

    g_tx_done = false;
    NRF_UARTE0->TXD.PTR    = (uint32_t)buf;
    NRF_UARTE0->TXD.MAXCNT = len;
    NRF_UARTE0->TASKS_STARTTX = 1;
}

void uart_tx_wait(void)
{
    while (!g_tx_done) { __WFE(); }
    // Stop TX so the TXSTOPPED event fires (releases pin)
    NRF_UARTE0->TASKS_STOPTX = 1;
}

// ------------------------------------------------------------------
// 3. Receive into a buffer (non-blocking, DMA)
// ------------------------------------------------------------------
static uint8_t  g_rx_buf[128];
static volatile uint16_t g_rx_len = 0;
static volatile bool     g_rx_done = false;

void uart_rx_start(void)
{
    g_rx_done = false;
    NRF_UARTE0->RXD.PTR    = (uint32_t)g_rx_buf;
    NRF_UARTE0->RXD.MAXCNT = sizeof(g_rx_buf);
    NRF_UARTE0->TASKS_STARTRX = 1;
}

void uart_rx_stop(void)
{
    // Trigger stop; RXTO fires after the last byte is committed
    NRF_UARTE0->TASKS_STOPRX = 1;
}

// ------------------------------------------------------------------
// 4. ISR
// ------------------------------------------------------------------
void UARTE0_UART0_IRQHandler(void)
{
    if (NRF_UARTE0->EVENTS_ENDTX) {
        NRF_UARTE0->EVENTS_ENDTX = 0;
        g_tx_done = true;
    }

    if (NRF_UARTE0->EVENTS_ENDRX) {
        NRF_UARTE0->EVENTS_ENDRX = 0;
        g_rx_len  = NRF_UARTE0->RXD.AMOUNT;
        g_rx_done = true;
    }

    if (NRF_UARTE0->EVENTS_RXTO) {
        NRF_UARTE0->EVENTS_RXTO = 0;
        // Bytes up to AMOUNT are valid even on timeout
        g_rx_len  = NRF_UARTE0->RXD.AMOUNT;
        g_rx_done = true;
    }

    if (NRF_UARTE0->EVENTS_ERROR) {
        NRF_UARTE0->EVENTS_ERROR = 0;
        uint32_t err = NRF_UARTE0->ERRORSRC;
        NRF_UARTE0->ERRORSRC = err;   // clear by writing 1s
        // Handle: OVERRUN(0), PARITY(1), FRAMING(2), BREAK(3)
    }
}

// ------------------------------------------------------------------
// 5. Usage example
// ------------------------------------------------------------------
int main(void)
{
    uart_register_init();

    static const uint8_t hello[] = "Hello nRF!\r\n";
    uart_tx_start(hello, sizeof(hello) - 1);
    uart_tx_wait();

    uart_rx_start();
    // ... application runs; call uart_rx_stop() to flush
}
```

---

## Zephyr RTOS Integration (C)

Zephyr wraps UARTE behind the `uart_*` API. Enable with `CONFIG_UART_ASYNC_API=y` for DMA-backed async operation.

### Device Tree Overlay (`boards/nrf52840dk_nrf52840.overlay`)

```dts
&uart0 {
    status = "okay";
    current-speed = <115200>;
    pinctrl-0 = <&uart0_default>;
    pinctrl-names = "default";

    /* RX buffer for async API */
    rx-double-buffering;
};

&pinctrl {
    uart0_default: uart0_default {
        group1 {
            psels = <NRF_PSEL(UART_TX, 0, 6)>,
                    <NRF_PSEL(UART_RX, 0, 8)>;
        };
    };
};
```

### Zephyr Async UART (C)

```c
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <string.h>

#define UART_NODE  DT_NODELABEL(uart0)
#define RX_BUF_SIZE 64

static const struct device *uart_dev;
static uint8_t rx_buf[2][RX_BUF_SIZE];

// Semaphore to signal TX completion
static K_SEM_DEFINE(tx_sem, 0, 1);

// -----------------------------------------------------------------
// Async callback
// -----------------------------------------------------------------
static void uart_async_cb(const struct device *dev,
                          struct uart_event   *evt,
                          void                *user_data)
{
    switch (evt->type) {

    case UART_TX_DONE:
        k_sem_give(&tx_sem);
        break;

    case UART_TX_ABORTED:
        k_sem_give(&tx_sem);   // unblock caller even on abort
        break;

    case UART_RX_RDY:
    {
        const uint8_t *data = evt->data.rx.buf + evt->data.rx.offset;
        size_t         len  = evt->data.rx.len;
        // Process len bytes in data[]
        for (size_t i = 0; i < len; i++) {
            process_byte(data[i]);
        }
        break;
    }

    case UART_RX_BUF_REQUEST:
        // Provide next buffer — double-buffering keeps DMA running
        {
            static uint8_t next = 0;
            next ^= 1;
            uart_rx_buf_rsp(dev, rx_buf[next], RX_BUF_SIZE);
        }
        break;

    case UART_RX_BUF_RELEASED:
        // Buffer returned; can be reused
        break;

    case UART_RX_STOPPED:
        // RX halted (error or explicit stop)
        break;

    default:
        break;
    }
}

// -----------------------------------------------------------------
// Init
// -----------------------------------------------------------------
int uart_async_init(void)
{
    uart_dev = DEVICE_DT_GET(UART_NODE);
    if (!device_is_ready(uart_dev)) {
        return -ENODEV;
    }

    int err = uart_callback_set(uart_dev, uart_async_cb, NULL);
    if (err) return err;

    // Start DMA receive with first buffer
    return uart_rx_enable(uart_dev, rx_buf[0], RX_BUF_SIZE,
                          SYS_FOREVER_US);  // no timeout
}

// -----------------------------------------------------------------
// Blocking transmit
// -----------------------------------------------------------------
int uart_send_sync(const uint8_t *data, size_t len)
{
    int err = uart_tx(uart_dev, data, len, SYS_FOREVER_US);
    if (err) return err;
    return k_sem_take(&tx_sem, K_FOREVER);
}

// -----------------------------------------------------------------
// Thread entry
// -----------------------------------------------------------------
void uart_thread(void *a, void *b, void *c)
{
    uart_async_init();

    while (1) {
        static const uint8_t msg[] = "Zephyr nRF UART\r\n";
        uart_send_sync(msg, sizeof(msg) - 1);
        k_sleep(K_MSEC(1000));
    }
}

K_THREAD_DEFINE(uart_tid, 1024, uart_thread, NULL, NULL, NULL, 5, 0, 0);
```

---

## Rust Implementation (nrf-hal + RTIC)

The [`nrf-hal`](https://github.com/nrf-rs/nrf-hal) crate provides type-safe wrappers around UARTE. Here we integrate it with RTIC for interrupt-driven operation.

### `Cargo.toml`

```toml
[dependencies]
nrf52840-hal = "0.16"
cortex-m     = "0.7"
cortex-m-rt  = "0.7"
rtic         = { version = "2", features = ["thumbv7m-backend"] }
heapless     = "0.8"
```

### RTIC Application (Rust)

```rust
//! UARTE0 with nrf52840-hal + RTIC 2.x
//! Echoes every received line back to sender.

#![no_main]
#![no_std]

use panic_halt as _;

#[rtic::app(device = nrf52840_hal::pac, dispatchers = [SWI0_EGU0])]
mod app {
    use nrf52840_hal::{
        pac::UARTE0,
        uarte::{Baudrate, Parity, Pins, Uarte},
        gpio::{p0, Level, Output, PushPull},
        prelude::*,
    };
    use heapless::Vec;
    use core::fmt::Write;

    // Shared resources
    #[shared]
    struct Shared {
        uarte: Uarte<UARTE0>,
    }

    // Local resources
    #[local]
    struct Local {
        rx_buf: Vec<u8, 128>,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local) {
        let p = cx.device;

        let port0 = p0::Parts::new(p.P0);
        let txd = port0.p0_06.into_push_pull_output(Level::High).degrade();
        let rxd = port0.p0_08.into_floating_input().degrade();

        let pins = Pins {
            txd,
            rxd,
            cts: None,
            rts: None,
        };

        let uarte = Uarte::new(p.UARTE0, pins, Parity::EXCLUDED, Baudrate::BAUD115200);

        // Enable ENDRX interrupt in NVIC (nrf-hal does this via the PAC)
        // Actual interrupt enabling done by RTIC via `#[task(binds = ...)]`

        (
            Shared { uarte },
            Local { rx_buf: Vec::new() },
        )
    }

    /// Idle task — enter WFE to save power
    #[idle]
    fn idle(_: idle::Context) -> ! {
        loop {
            cortex_m::asm::wfe();
        }
    }

    /// UARTE0 interrupt — fires on ENDRX / ENDTX / RXTO / ERROR
    #[task(binds = UARTE0_UART0, shared = [uarte], local = [rx_buf])]
    fn uarte_irq(cx: uarte_irq::Context) {
        let uarte   = cx.shared.uarte;
        let rx_buf  = cx.local.rx_buf;

        uarte.lock(|u| {
            // Collect received byte(s)
            let mut byte = [0u8; 1];
            if u.read(&mut byte).is_ok() {
                let b = byte[0];
                if b == b'\n' || rx_buf.len() >= rx_buf.capacity() - 1 {
                    // Echo the line back
                    let _ = u.write_str(core::str::from_utf8(rx_buf).unwrap_or("?"));
                    let _ = u.write_str("\r\n");
                    rx_buf.clear();
                } else if b != b'\r' {
                    let _ = rx_buf.push(b);
                }
            }
        });
    }
}
```

### Non-RTIC Blocking Example (Rust)

```rust
//! Minimal blocking UART echo — no RTOS, direct nrf-hal usage

#![no_main]
#![no_std]

use panic_halt as _;
use cortex_m_rt::entry;
use nrf52840_hal::{
    pac::Peripherals,
    gpio::{p0, Level},
    uarte::{Baudrate, Parity, Pins, Uarte},
    prelude::*,
};

#[entry]
fn main() -> ! {
    let p     = Peripherals::take().unwrap();
    let port0 = p0::Parts::new(p.P0);

    let pins = Pins {
        txd: port0.p0_06.into_push_pull_output(Level::High).degrade(),
        rxd: port0.p0_08.into_floating_input().degrade(),
        cts: None,
        rts: None,
    };

    let mut uart = Uarte::new(
        p.UARTE0,
        pins,
        Parity::EXCLUDED,
        Baudrate::BAUD115200,
    );

    // Send greeting
    let msg = b"nRF52840 UART ready\r\n";
    uart.write(msg).unwrap();

    // Echo loop
    loop {
        let mut buf = [0u8; 1];
        if uart.read(&mut buf).is_ok() {
            uart.write(&buf).unwrap();
        }
    }
}
```

---

## Rust with Embassy (async/await)

[Embassy](https://embassy.dev) provides an async executor tuned for embedded Cortex-M. Its nRF HAL wraps UARTE with `async fn`, enabling structured concurrency without RTOS threads.

### `Cargo.toml`

```toml
[dependencies]
embassy-nrf     = { version = "0.2", features = ["nrf52840", "time-driver-rtc1"] }
embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
embassy-time    = "0.3"
embassy-futures = "0.1"
static_cell     = "2"
```

### Embassy Async UART (Rust)

```rust
//! Embassy async UARTE — concurrent TX and RX tasks on nRF52840

#![no_main]
#![no_std]

use embassy_executor::Spawner;
use embassy_nrf::{
    bind_interrupts,
    gpio::{Level, Output, OutputDrive},
    peripherals::UARTE0,
    uarte::{self, InterruptHandler, Uarte},
};
use embassy_time::Timer;
use static_cell::StaticCell;

// Bind UARTE0 interrupt to Embassy's handler
bind_interrupts!(struct Irqs {
    UARTE0_UART0 => InterruptHandler<UARTE0>;
});

// -------------------------------------------------------------------
// TX task: sends a heartbeat message every second
// -------------------------------------------------------------------
#[embassy_executor::task]
async fn tx_task(mut uarte: uarte::UarteTx<'static, UARTE0>) {
    let msg = b"Heartbeat\r\n";
    loop {
        uarte.write(msg).await.unwrap();
        Timer::after_millis(1000).await;
    }
}

// -------------------------------------------------------------------
// RX task: receives bytes and echoes them back
// -------------------------------------------------------------------
#[embassy_executor::task]
async fn rx_task(mut uarte: uarte::UarteRx<'static, UARTE0>) {
    let mut buf = [0u8; 64];
    loop {
        // Read up to 64 bytes; returns when buffer is full or timeout
        match uarte.read_until_idle(&mut buf).await {
            Ok(n) if n > 0 => {
                // In a real app, send to a channel for TX task to echo
                // defmt::info!("Rx {} bytes", n);
            }
            _ => {}
        }
    }
}

// -------------------------------------------------------------------
// Main / init
// -------------------------------------------------------------------
#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_nrf::init(Default::default());

    let mut config = uarte::Config::default();
    config.baudrate = uarte::Baudrate::BAUD115200;
    config.parity   = uarte::Parity::EXCLUDED;

    // Create UARTE and split into independent TX / RX halves
    let uarte = Uarte::new(p.UARTE0, Irqs, p.P0_08, p.P0_06, config);
    let (tx, rx) = uarte.split();

    // Spawn independent tasks
    spawner.spawn(tx_task(tx)).unwrap();
    spawner.spawn(rx_task(rx)).unwrap();
}
```

### Embassy `read_until_idle` Internals

Embassy implements `read_until_idle` by arming a DMA RX transfer and monitoring the `RXTO` event. When the line goes idle (no byte received within ~1 character time after the last byte), the driver issues `TASKS_STOPRX`, waits for `RXTO`, and returns `RXD.AMOUNT`. This is the recommended pattern for line-oriented protocols.

---

## Double Buffering and Circular Reception

For continuous high-throughput reception, swap RX buffers on every `ENDRX` event so EasyDMA has a fresh buffer before the next byte arrives.

```c
// C — ISR-driven ping-pong RX with raw registers

#define BUF_SZ 128

static uint8_t rx_bufs[2][BUF_SZ];
static volatile uint8_t  prod = 0;  // buffer index EasyDMA is filling
static volatile uint16_t rx_amounts[2];

// Called by application to start reception
void dma_rx_start(void)
{
    NRF_UARTE0->RXD.PTR    = (uint32_t)rx_bufs[prod];
    NRF_UARTE0->RXD.MAXCNT = BUF_SZ;
    NRF_UARTE0->TASKS_STARTRX = 1;
}

void UARTE0_UART0_IRQHandler(void)
{
    if (NRF_UARTE0->EVENTS_ENDRX)
    {
        NRF_UARTE0->EVENTS_ENDRX = 0;

        // Capture filled buffer info
        uint8_t filled    = prod;
        rx_amounts[filled] = NRF_UARTE0->RXD.AMOUNT;

        // Swap and immediately re-arm before any gap
        prod ^= 1;
        NRF_UARTE0->RXD.PTR    = (uint32_t)rx_bufs[prod];
        NRF_UARTE0->RXD.MAXCNT = BUF_SZ;
        NRF_UARTE0->TASKS_STARTRX = 1;

        // Notify application (e.g. set event bits / post semaphore)
        notify_buffer_ready(filled, rx_amounts[filled]);
    }
}
```

```
Time ──────────────────────────────────────────────────────▶
       [ buf0 filling ][ buf0 full → swap ][ buf1 filling ]
                                           ↑
                                  ENDRX event fires here.
                                  New DMA starts IMMEDIATELY.
                                  Zero bytes lost.
```

---

## Low-Power Considerations

UARTE keeps the 16 MHz HFCLK running while active. To save power:

1. **Disable after burst transfers:** Call `TASKS_STOPTX`, wait for `EVENTS_TXSTOPPED`, then write `ENABLE = Disabled`. The 16 MHz oscillator can be released.
2. **Use PPI for auto-stop:** Chain `EVENTS_ENDTX → TASKS_STOPTX` through PPI without waking the CPU.
3. **UART wakeup from System OFF:** Only possible on nRF52840 and nRF5340 via the **UART RXD** pin as a GPIO sense interrupt — UARTE itself is powered off.
4. **Lazy HFCLK start:** On nRF52, HFCLK auto-starts when UARTE is enabled. Plan burst transfers rather than leaving UARTE enabled idle.

```c
// C — PPI chain to stop TX automatically
#include "nrfx_ppi.h"

nrf_ppi_channel_t ppi_ch;
nrfx_ppi_channel_alloc(&ppi_ch);
nrfx_ppi_channel_assign(ppi_ch,
    nrf_uarte_event_address_get(NRF_UARTE0, NRF_UARTE_EVENT_ENDTX),
    nrf_uarte_task_address_get(NRF_UARTE0, NRF_UARTE_TASK_STOPTX));
nrfx_ppi_channel_enable(ppi_ch);
// Now: ENDTX → STOPTX fires without CPU involvement
```

---

## Common Pitfalls and Debugging

| Symptom                         | Likely Cause                                                | Fix                                                    |
|---------------------------------|-------------------------------------------------------------|--------------------------------------------------------|
| No output on TXD                | Buffer in flash (const/literal)                             | Copy to RAM before passing pointer to EasyDMA          |
| First byte corrupted            | HFCLK not stable                                            | Ensure 16 MHz crystal is running; nrfx does this       |
| ENDRX never fires               | MAXCNT = 0, or STARTRX not triggered                        | Check register write order; STARTRX last               |
| Garbled data                    | Wrong baud rate constant                                    | Use MDK-defined constants; verify with oscilloscope    |
| Hard fault after STARTRX        | RXD.PTR points to peripheral RAM (nRF5340)                 | Use shared RAM region `0x2000_0000`                    |
| RX misses bytes at high baud    | No double buffering; gap between DMA restarts               | Swap buffer inside ENDRX ISR before any other code     |
| UARTE and UART conflict (52832) | Both instances enabled at same address                      | Disable UART0 before enabling UARTE0 (ENABLE = 0)      |
| TX/RX swapped silently          | PSEL.TXD wired to physical RX line                          | Check schematic; PSEL is from nRF's perspective        |
| No data after System ON wakeup  | UARTE was disabled; GPIO sense woke CPU but UARTE is off   | Re-enable and re-arm DMA in wakeup handler             |

### Quick Debug Checklist

```
1. Confirm 16 MHz HFCLK is running
   → Read NRF_CLOCK->HFCLKSTAT; bit 16 = STATE (1 = running)

2. Verify ENABLE register
   → NRF_UARTE0->ENABLE should read 0x08

3. Confirm PSEL values
   → Print TXD/RXD PSEL; bit 31 must be 0 (connected)

4. Check EasyDMA address
   → TXD.PTR should be in 0x2000_0000 – 0x2003_FFFF range (nRF52840)

5. Verify BAUDRATE register
   → 0x01D7E000 for 115200; not a raw divisor

6. Monitor events via RTT/J-Link
   → Log EVENTS_ENDTX, EVENTS_ENDRX, EVENTS_ERROR after each transfer
```

---

## Summary

The **nRF UARTE** peripheral with **EasyDMA** is the correct UART implementation for all nRF52 and nRF53 series production firmware. Its key advantages over classical interrupt-per-byte UART are:

- **Zero CPU overhead per byte** — DMA moves data autonomously between RAM and the shift register.
- **Sustained 1 Mbps** — limited only by the AHB bus and RAM access latency, not IRQ latency.
- **Flexible pin mapping** — any GPIO becomes TXD, RXD, RTS, or CTS via PSEL.
- **Low-power friendly** — disable the peripheral between bursts; PPI chains allow fully autonomous TX with no CPU involvement.

The programming model across the ecosystem is consistent:

| Layer          | Language | Key Abstraction                               |
|----------------|----------|-----------------------------------------------|
| Raw registers  | C        | `NRF_UARTE0->TXD.PTR`, `TASKS_*`, `EVENTS_*` |
| nrfx HAL       | C/C++    | `nrfx_uarte_init`, `nrfx_uarte_tx/rx`        |
| Zephyr async   | C        | `uart_callback_set`, `uart_tx`, `uart_rx_enable` |
| nrf-hal + RTIC | Rust     | `Uarte::new`, `write`, `read` + RTIC tasks    |
| Embassy        | Rust     | `async fn write/read_until_idle` + task spawning |

Regardless of the abstraction layer, the same EasyDMA principles apply: buffer in RAM, arm `PTR`/`MAXCNT`, trigger `STARTTX`/`STARTRX`, and react to `ENDTX`/`ENDRX`/`RXTO` events. Double-buffering the RX path eliminates gaps between DMA transfers and is essential above ~115200 baud for continuous reception.