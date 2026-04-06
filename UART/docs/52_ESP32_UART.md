Now I'll create the comprehensive markdown document:Here's the complete document. It covers:

**Hardware** — The ESP32's three UART controllers, their default pins, FIFO sizes, baud range (up to 5 Mbps), and the GPIO matrix flexibility for pin remapping.

**C/C++ examples covering:**
- Basic initialization (`uart_param_config` → `uart_set_pin` → `uart_driver_install`)
- Blocking read/write and an echo task
- Full event-driven architecture with FreeRTOS queue and all event types (overflow, frame error, pattern detection, etc.)
- RS-485 half-duplex with automatic DE pin toggling via RTS
- Three simultaneous UART ports with dedicated per-port tasks

**Rust examples covering:**
- `UartDriver` setup via `esp-idf-hal` with the ownership model
- Polling and line-framing patterns
- Multi-threaded use with `std::thread`, showing how the type system prevents accidental double-use of a peripheral at compile time

**Additional sections** on pin mapping constraints, hardware/software flow control, light-sleep wakeup via UART edges, diagnostics, and a performance tuning table.

# 52. ESP32 UART — Using ESP-IDF UART Driver with Multiple Hardware Ports

---

## Table of Contents

1. [Introduction](#introduction)
2. [ESP32 UART Hardware Overview](#esp32-uart-hardware-overview)
3. [ESP-IDF UART Driver Architecture](#esp-idf-uart-driver-architecture)
4. [Key Configuration Parameters](#key-configuration-parameters)
5. [C/C++ Programming with ESP-IDF](#cc-programming-with-esp-idf)
   - 5.1 [Basic UART Initialization](#51-basic-uart-initialization)
   - 5.2 [Sending and Receiving Data](#52-sending-and-receiving-data)
   - 5.3 [Event-Driven UART with FreeRTOS](#53-event-driven-uart-with-freertos)
   - 5.4 [RS-485 Half-Duplex Mode](#54-rs-485-half-duplex-mode)
   - 5.5 [Multiple UART Ports in Parallel](#55-multiple-uart-ports-in-parallel)
6. [Rust Programming with ESP-IDF](#rust-programming-with-esp-idf)
   - 6.1 [Setup and Dependencies](#61-setup-and-dependencies)
   - 6.2 [Basic UART in Rust](#62-basic-uart-in-rust)
   - 6.3 [Non-Blocking and Async UART in Rust](#63-non-blocking-and-async-uart-in-rust)
   - 6.4 [Multiple UART Ports in Rust](#64-multiple-uart-ports-in-rust)
7. [Pin Mapping and GPIO Matrix](#pin-mapping-and-gpio-matrix)
8. [Flow Control](#flow-control)
9. [UART Wakeup from Light Sleep](#uart-wakeup-from-light-sleep)
10. [Error Handling and Diagnostics](#error-handling-and-diagnostics)
11. [Performance Considerations](#performance-considerations)
12. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is one of the oldest and most widely used serial communication protocols in embedded systems. It provides simple, two-wire, full-duplex asynchronous communication between devices. The ESP32 integrates three hardware UART controllers (UART0, UART1, UART2), each fully supported by Espressif's ESP-IDF framework via a rich driver API.

This document covers the full lifecycle of UART usage on ESP32: hardware capabilities, driver initialization, data transmission, event handling, RS-485 mode, multiple simultaneous ports, and equivalent implementations in both C/C++ and Rust using the `esp-idf-svc` / `esp-idf-hal` ecosystem.

---

## ESP32 UART Hardware Overview

The ESP32 has **three independent UART controllers**:

| Port   | Default TX | Default RX | Primary Use                         |
|--------|------------|------------|-------------------------------------|
| UART0  | GPIO1      | GPIO3      | Console / flashing (avoid reuse)    |
| UART1  | GPIO10     | GPIO9      | General purpose (often remapped)    |
| UART2  | GPIO17     | GPIO16     | General purpose                     |

Each UART controller supports:

- Baud rates from 300 bps up to 5 Mbps
- 5, 6, 7, or 8 data bits
- 1 or 2 stop bits
- None, even, or odd parity
- Hardware RTS/CTS flow control
- RS-485 half-duplex mode with automatic direction control
- IrDA mode (SIR)
- Hardware FIFO buffers: 128 bytes TX, 128 bytes RX
- DMA support for high-throughput transfers
- Wakeup from light sleep via UART activity

All UART pins are routed through the **GPIO matrix**, meaning any GPIO pin can be assigned to any UART signal (TX, RX, RTS, CTS), subject to GPIO limitations (input-only pins cannot be used as TX).

---

## ESP-IDF UART Driver Architecture

The ESP-IDF UART driver operates in two layers:

**Hardware layer** — directly controls the UART peripheral registers: baud rate divisor, parity, stop bits, FIFO thresholds, interrupts.

**Driver layer** — provides a thread-safe API on top of FreeRTOS. It manages:
- An internal **ring buffer** for RX (software buffer, configurable size)
- An optional **TX ring buffer** for non-blocking writes
- An **event queue** (FreeRTOS queue) for interrupt-driven event notifications
- Interrupt service routines (ISR) for FIFO overflow, break detection, frame errors, pattern detection, etc.

The recommended flow is:

```
uart_driver_install()  →  uart_param_config()  →  uart_set_pin()  →  uart_read_bytes() / uart_write_bytes()
```

---

## Key Configuration Parameters

The `uart_config_t` structure controls all hardware parameters:

```c
typedef struct {
    int baud_rate;                  // e.g. 115200
    uart_word_length_t data_bits;   // UART_DATA_8_BITS
    uart_parity_t parity;           // UART_PARITY_DISABLE
    uart_stop_bits_t stop_bits;     // UART_STOP_BITS_1
    uart_hw_flowcontrol_t flow_ctrl;// UART_HW_FLOWCTRL_DISABLE
    uint8_t rx_flow_ctrl_thresh;    // RTS threshold (0–127)
    union {
        uart_sclk_t source_clk;     // Clock source: APB or REF_TICK
    };
} uart_config_t;
```

---

## C/C++ Programming with ESP-IDF

### 5.1 Basic UART Initialization

```c
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

static const char *TAG = "uart_basic";

#define UART_PORT       UART_NUM_2
#define UART_TX_PIN     GPIO_NUM_17
#define UART_RX_PIN     GPIO_NUM_16
#define UART_BAUD       115200
#define BUF_SIZE        1024

void uart_init(void)
{
    // 1. Configure hardware parameters
    const uart_config_t uart_config = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // 2. Apply hardware config (before installing driver)
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_config));

    // 3. Assign GPIO pins
    ESP_ERROR_CHECK(uart_set_pin(
        UART_PORT,
        UART_TX_PIN,    // TX
        UART_RX_PIN,    // RX
        UART_PIN_NO_CHANGE, // RTS — not used
        UART_PIN_NO_CHANGE  // CTS — not used
    ));

    // 4. Install driver: RX buffer = BUF_SIZE*2, TX buffer = 0 (blocking),
    //    no event queue, no ISR flags
    ESP_ERROR_CHECK(uart_driver_install(
        UART_PORT,
        BUF_SIZE * 2,   // RX ring buffer size
        0,              // TX ring buffer size (0 = blocking)
        0,              // Event queue length (0 = no queue)
        NULL,           // Event queue handle
        0               // ISR flags
    ));

    ESP_LOGI(TAG, "UART%d initialized at %d baud", UART_PORT, UART_BAUD);
}
```

> **Note:** Always call `uart_param_config()` before `uart_driver_install()`. Installing the driver first and then changing parameters can cause undefined behavior.

---

### 5.2 Sending and Receiving Data

```c
#include "driver/uart.h"
#include "string.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define READ_TIMEOUT_MS  100   // Timeout in milliseconds
#define RX_BUF_SIZE      256

// --- Blocking write ---
void uart_send_string(const char *text)
{
    uart_write_bytes(UART_PORT, text, strlen(text));
}

// Send binary data with length
void uart_send_data(const uint8_t *data, size_t len)
{
    int written = uart_write_bytes(UART_PORT, (const char *)data, len);
    if (written < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
    }
}

// Wait for TX FIFO to drain (important before deep sleep or pin changes)
void uart_flush_tx(void)
{
    uart_wait_tx_done(UART_PORT, pdMS_TO_TICKS(1000));
}

// --- Blocking read with timeout ---
int uart_receive(uint8_t *buf, size_t max_len)
{
    int len = uart_read_bytes(
        UART_PORT,
        buf,
        max_len,
        pdMS_TO_TICKS(READ_TIMEOUT_MS)
    );
    return len; // Returns number of bytes read, -1 on error
}

// --- Echo task: reads bytes and echoes them back ---
void uart_echo_task(void *arg)
{
    uint8_t data[RX_BUF_SIZE];

    while (1) {
        int len = uart_read_bytes(UART_PORT, data, sizeof(data) - 1,
                                  pdMS_TO_TICKS(20));
        if (len > 0) {
            data[len] = '\0';
            ESP_LOGI(TAG, "Received %d bytes: %s", len, data);
            uart_write_bytes(UART_PORT, (const char *)data, len);
        }
    }
}
```

---

### 5.3 Event-Driven UART with FreeRTOS

For production code, the event queue mechanism is preferred over polling. The driver ISR pushes events into a FreeRTOS queue, and a dedicated task processes them.

```c
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

#define EV_QUEUE_SIZE   20
#define RX_BUF_SIZE     512
#define PATTERN_CHR     '\n'   // Detect newline as end-of-frame

static QueueHandle_t uart_event_queue;

void uart_event_init(void)
{
    const uart_config_t config = {
        .baud_rate  = 115200,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(UART_NUM_2, &config);
    uart_set_pin(UART_NUM_2, 17, 16, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    // Install with event queue
    uart_driver_install(UART_NUM_2, RX_BUF_SIZE * 2, RX_BUF_SIZE * 2,
                        EV_QUEUE_SIZE, &uart_event_queue, 0);

    // Enable pattern detection: detect 1x '\n' character
    uart_enable_pattern_det_baud_intr(UART_NUM_2, PATTERN_CHR, 1, 9, 0, 0);
    uart_pattern_queue_reset(UART_NUM_2, EV_QUEUE_SIZE);
}

void uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t      buf[RX_BUF_SIZE];

    while (1) {
        // Block until an event arrives
        if (!xQueueReceive(uart_event_queue, &event, portMAX_DELAY)) {
            continue;
        }

        switch (event.type) {
        case UART_DATA:
            // Normal data available in RX buffer
            {
                int len = uart_read_bytes(UART_NUM_2, buf,
                                          event.size, pdMS_TO_TICKS(100));
                buf[len] = '\0';
                ESP_LOGI(TAG, "[DATA] %d bytes: %s", len, buf);
            }
            break;

        case UART_PATTERN_DET:
            // Pattern character detected — read up to and including it
            {
                int pos = uart_pattern_pop_pos(UART_NUM_2);
                if (pos == -1) {
                    uart_flush_input(UART_NUM_2);
                } else {
                    int len = uart_read_bytes(UART_NUM_2, buf,
                                              pos + 1, pdMS_TO_TICKS(100));
                    buf[len] = '\0';
                    ESP_LOGI(TAG, "[PATTERN] Line: %s", buf);
                }
            }
            break;

        case UART_FIFO_OVF:
            ESP_LOGW(TAG, "RX FIFO overflow — flushing");
            uart_flush_input(UART_NUM_2);
            xQueueReset(uart_event_queue);
            break;

        case UART_BUFFER_FULL:
            ESP_LOGW(TAG, "Ring buffer full — flushing");
            uart_flush_input(UART_NUM_2);
            xQueueReset(uart_event_queue);
            break;

        case UART_BREAK:
            ESP_LOGW(TAG, "Break condition detected");
            break;

        case UART_FRAME_ERR:
            ESP_LOGE(TAG, "Frame error");
            break;

        case UART_PARITY_ERR:
            ESP_LOGE(TAG, "Parity error");
            break;

        default:
            ESP_LOGD(TAG, "Unknown event type: %d", event.type);
            break;
        }
    }
}

void app_main(void)
{
    uart_event_init();
    xTaskCreate(uart_event_task, "uart_events", 4096, NULL, 12, NULL);
}
```

---

### 5.4 RS-485 Half-Duplex Mode

RS-485 uses a differential bus where only one device transmits at a time. The ESP-IDF driver can automatically assert a direction-control GPIO (DE/RE pin) during transmission.

```c
#include "driver/uart.h"

#define RS485_PORT      UART_NUM_1
#define RS485_TX_PIN    GPIO_NUM_4
#define RS485_RX_PIN    GPIO_NUM_5
#define RS485_DE_PIN    GPIO_NUM_18   // Driver Enable (active high)
#define RS485_BAUD      9600

void rs485_init(void)
{
    const uart_config_t config = {
        .baud_rate        = RS485_BAUD,
        .data_bits        = UART_DATA_8_BITS,
        .parity           = UART_PARITY_DISABLE,
        .stop_bits        = UART_STOP_BITS_1,
        .flow_ctrl        = UART_HW_FLOWCTRL_DISABLE,
        .source_clk       = UART_SCLK_APB,
    };
    uart_param_config(RS485_PORT, &config);

    // Assign TX, RX, and RTS (used as DE signal)
    uart_set_pin(RS485_PORT, RS485_TX_PIN, RS485_RX_PIN,
                 RS485_DE_PIN, UART_PIN_NO_CHANGE);

    uart_driver_install(RS485_PORT, 256, 256, 0, NULL, 0);

    // Switch to RS-485 half-duplex mode
    // RTS (DE pin) is automatically toggled by the UART hardware
    uart_set_mode(RS485_PORT, UART_MODE_RS485_HALF_DUPLEX);
}

// Modbus RTU style: send request, wait for response
int rs485_transaction(const uint8_t *req, size_t req_len,
                      uint8_t *resp, size_t resp_max,
                      uint32_t timeout_ms)
{
    uart_flush(RS485_PORT);
    uart_write_bytes(RS485_PORT, (const char *)req, req_len);
    uart_wait_tx_done(RS485_PORT, pdMS_TO_TICKS(100));

    return uart_read_bytes(RS485_PORT, resp, resp_max,
                           pdMS_TO_TICKS(timeout_ms));
}
```

---

### 5.5 Multiple UART Ports in Parallel

```c
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Configuration for each port
typedef struct {
    uart_port_t  port;
    int          tx_pin;
    int          rx_pin;
    int          baud;
    const char  *name;
} uart_port_cfg_t;

static const uart_port_cfg_t ports[] = {
    { UART_NUM_0, 1,  3,  115200, "console"  },  // stdout/IDF monitor
    { UART_NUM_1, 4,  5,  9600,   "sensor"   },  // sensor module
    { UART_NUM_2, 17, 16, 57600,  "modem"    },  // GSM / BLE module
};

static void uart_setup(const uart_port_cfg_t *cfg)
{
    const uart_config_t c = {
        .baud_rate  = cfg->baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    uart_param_config(cfg->port, &c);
    uart_set_pin(cfg->port, cfg->tx_pin, cfg->rx_pin,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(cfg->port, 1024, 512, 0, NULL, 0);
    ESP_LOGI("UART", "Port %s (UART%d) ready at %d baud",
             cfg->name, cfg->port, cfg->baud);
}

// Per-port task: reads from sensor UART and logs
void sensor_task(void *arg)
{
    uint8_t buf[128];
    while (1) {
        int len = uart_read_bytes(UART_NUM_1, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(50));
        if (len > 0) {
            buf[len] = '\0';
            ESP_LOGI("sensor", "%s", buf);
        }
    }
}

// Per-port task: bridges modem UART to console
void modem_bridge_task(void *arg)
{
    uint8_t buf[256];
    while (1) {
        int len = uart_read_bytes(UART_NUM_2, buf, sizeof(buf) - 1,
                                  pdMS_TO_TICKS(50));
        if (len > 0) {
            uart_write_bytes(UART_NUM_0, (const char *)buf, len);
        }
    }
}

void app_main(void)
{
    for (int i = 0; i < 3; i++) {
        uart_setup(&ports[i]);
    }
    xTaskCreate(sensor_task,       "sensor_rx",  2048, NULL, 5, NULL);
    xTaskCreate(modem_bridge_task, "modem_rx",   2048, NULL, 5, NULL);
}
```

---

## Rust Programming with ESP-IDF

### 6.1 Setup and Dependencies

Add the following to `Cargo.toml`:

```toml
[dependencies]
esp-idf-hal  = { version = "0.43", features = ["std"] }
esp-idf-svc  = { version = "0.48", features = ["std"] }
esp-idf-sys  = { version = "0.34", features = ["binstart"] }
embedded-hal = "1.0"
```

The `esp-idf-hal` crate wraps the underlying C driver through bindings generated from `esp-idf-sys`. UART is exposed through the `uart` module.

---

### 6.2 Basic UART in Rust

```rust
use esp_idf_hal::prelude::*;
use esp_idf_hal::uart::{self, UartDriver, config::Config};
use esp_idf_hal::peripherals::Peripherals;
use std::time::Duration;

fn main() -> anyhow::Result<()> {
    // Required boilerplate for ESP-IDF in Rust
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    let peripherals = Peripherals::take()?;

    // Build UART configuration
    let config = Config::new()
        .baudrate(Hertz(115_200))
        .data_bits(uart::config::DataBits::DataBits8)
        .parity_none()
        .stop_bits(uart::config::StopBits::STOP1)
        .flow_control(uart::config::FlowControl::None);

    // Instantiate UART2 with TX=GPIO17, RX=GPIO16
    let uart = UartDriver::new(
        peripherals.uart2,
        peripherals.pins.gpio17,  // TX
        peripherals.pins.gpio16,  // RX
        Option::<esp_idf_hal::gpio::AnyIOPin>::None, // RTS
        Option::<esp_idf_hal::gpio::AnyIOPin>::None, // CTS
        &config,
    )?;

    log::info!("UART2 initialized");

    // Write a string
    uart.write("Hello from Rust on ESP32!\r\n".as_bytes())?;

    // Read with timeout
    let mut buf = [0u8; 64];
    let timeout = Duration::from_millis(100);

    loop {
        match uart.read(&mut buf, timeout.into()) {
            Ok(len) if len > 0 => {
                let text = core::str::from_utf8(&buf[..len])
                    .unwrap_or("<invalid utf8>");
                log::info!("RX {len} bytes: {text}");
                // Echo back
                uart.write(&buf[..len])?;
            }
            Ok(_) => {
                // Timeout — no data
            }
            Err(e) => {
                log::error!("UART read error: {e:?}");
            }
        }
    }
}
```

---

### 6.3 Non-Blocking and Async UART in Rust

For tasks that should not block on the read, use `read_nb` (non-blocking read from the RX FIFO directly):

```rust
use esp_idf_hal::uart::UartDriver;
use std::time::Duration;

fn poll_uart(uart: &UartDriver) -> anyhow::Result<Option<Vec<u8>>> {
    // Read available bytes without blocking (timeout = 0)
    let mut buf = [0u8; 256];
    match uart.read(&mut buf, esp_idf_hal::delay::NON_BLOCK) {
        Ok(0) => Ok(None),            // No data yet
        Ok(n) => Ok(Some(buf[..n].to_vec())),
        Err(e) => Err(e.into()),
    }
}

// Framing example: accumulate bytes until '\n'
fn read_line(uart: &UartDriver) -> anyhow::Result<String> {
    let mut line = Vec::new();
    let mut byte = [0u8; 1];

    loop {
        uart.read(&mut byte, Duration::from_millis(500).into())?;
        if byte[0] == b'\n' {
            break;
        }
        line.push(byte[0]);
    }

    // Strip trailing '\r' if present
    if line.last() == Some(&b'\r') {
        line.pop();
    }

    String::from_utf8(line).map_err(|e| anyhow::anyhow!("UTF-8 error: {e}"))
}
```

---

### 6.4 Multiple UART Ports in Rust

Each ESP-IDF UART peripheral is a distinct type in `esp-idf-hal`, allowing the compiler to enforce that each is only used once (ownership model).

```rust
use esp_idf_hal::prelude::*;
use esp_idf_hal::uart::{UartDriver, config::Config};
use esp_idf_hal::peripherals::Peripherals;
use std::thread;
use std::time::Duration;

fn make_config(baud: u32) -> Config {
    Config::new()
        .baudrate(Hertz(baud))
        .data_bits(esp_idf_hal::uart::config::DataBits::DataBits8)
        .parity_none()
        .stop_bits(esp_idf_hal::uart::config::StopBits::STOP1)
        .flow_control(esp_idf_hal::uart::config::FlowControl::None)
}

fn main() -> anyhow::Result<()> {
    esp_idf_svc::sys::link_patches();
    esp_idf_svc::log::EspLogger::initialize_default();

    let p = Peripherals::take()?;

    // UART1 — sensor (9600 baud, GPIO4/5)
    let uart1 = UartDriver::new(
        p.uart1,
        p.pins.gpio4,
        p.pins.gpio5,
        Option::<esp_idf_hal::gpio::AnyIOPin>::None,
        Option::<esp_idf_hal::gpio::AnyIOPin>::None,
        &make_config(9_600),
    )?;

    // UART2 — modem (57600 baud, GPIO17/16)
    let uart2 = UartDriver::new(
        p.uart2,
        p.pins.gpio17,
        p.pins.gpio16,
        Option::<esp_idf_hal::gpio::AnyIOPin>::None,
        Option::<esp_idf_hal::gpio::AnyIOPin>::None,
        &make_config(57_600),
    )?;

    // Sensor reader thread
    let sensor_handle = thread::Builder::new()
        .stack_size(4096)
        .spawn(move || {
            let mut buf = [0u8; 128];
            loop {
                if let Ok(n) = uart1.read(&mut buf, Duration::from_millis(50).into()) {
                    if n > 0 {
                        let s = core::str::from_utf8(&buf[..n]).unwrap_or("?");
                        log::info!("[sensor] {s}");
                    }
                }
            }
        })?;

    // Modem reader thread
    let modem_handle = thread::Builder::new()
        .stack_size(4096)
        .spawn(move || {
            let mut buf = [0u8; 256];
            loop {
                if let Ok(n) = uart2.read(&mut buf, Duration::from_millis(50).into()) {
                    if n > 0 {
                        log::info!("[modem] {} bytes", n);
                    }
                }
            }
        })?;

    sensor_handle.join().ok();
    modem_handle.join().ok();

    Ok(())
}
```

> **Ownership note:** The Rust type system prevents accidentally passing the same UART peripheral to two drivers — attempting to `take()` it twice results in a compile error, not a runtime panic.

---

## Pin Mapping and GPIO Matrix

The ESP32 GPIO matrix allows flexible pin assignment. Use `uart_set_pin()` in C or constructor arguments in Rust.

| Signal | Constraint                              |
|--------|-----------------------------------------|
| TX     | Any output-capable GPIO                 |
| RX     | Any input-capable GPIO (including input-only: GPIO34–39) |
| RTS    | Any output-capable GPIO                 |
| CTS    | Any input-capable GPIO                  |

**Common remapping scenario (UART1 flash conflict):**

On many ESP32 boards, GPIO9/10 are connected to the flash chip. UART1 must be remapped:

```c
// Remap UART1 away from flash pins
uart_set_pin(UART_NUM_1, GPIO_NUM_4, GPIO_NUM_5,
             UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
```

---

## Flow Control

Hardware flow control uses RTS (Request to Send) and CTS (Clear to Send):

```c
const uart_config_t config = {
    .baud_rate  = 115200,
    .data_bits  = UART_DATA_8_BITS,
    .parity     = UART_PARITY_DISABLE,
    .stop_bits  = UART_STOP_BITS_1,
    .flow_ctrl  = UART_HW_FLOWCTRL_CTS_RTS,  // Both directions
    .rx_flow_ctrl_thresh = 122,               // Assert RTS when RX FIFO >= 122 bytes
    .source_clk = UART_SCLK_APB,
};

uart_param_config(UART_NUM_2, &config);
uart_set_pin(UART_NUM_2, TX_PIN, RX_PIN, RTS_PIN, CTS_PIN);
```

Software flow control (XON/XOFF) can also be enabled:

```c
uart_set_sw_flow_ctrl(UART_NUM_2, true,
                      0x11,  // XON  character
                      0x13); // XOFF character
```

---

## UART Wakeup from Light Sleep

UART0 can wake the ESP32 from light sleep when a certain number of positive edges are detected on the RX line:

```c
#include "driver/uart.h"
#include "esp_sleep.h"

void configure_uart_wakeup(void)
{
    // Set threshold: 3 edges = 1.5 start bits + 1 data bit
    uart_set_wakeup_threshold(UART_NUM_0, 3);
    esp_sleep_enable_uart_wakeup(UART_NUM_0);
}
```

After waking, the first received byte may be corrupted due to the partial reception that triggered the wake; this is a known hardware limitation and should be handled by the application framing protocol.

---

## Error Handling and Diagnostics

```c
// Check UART status and clear errors
void uart_check_status(uart_port_t port)
{
    // Get number of bytes waiting in RX buffer
    size_t buffered;
    uart_get_buffered_data_len(port, &buffered);
    ESP_LOGI(TAG, "Buffered: %d bytes", buffered);

    // Flush stale RX data
    if (buffered > 0) {
        uart_flush_input(port);
    }

    // Read and clear UART error register
    uint32_t status = uart_get_collision_flag(port);
    if (status) {
        ESP_LOGW(TAG, "RS-485 collision detected");
    }
}

// In Rust, errors are returned as Result<_, EspError>
// EspError wraps the ESP-IDF esp_err_t error code
fn check_uart(uart: &UartDriver) {
    // Flush input
    if let Err(e) = unsafe {
        esp_idf_sys::uart_flush_input(uart.port() as i32)
            .into_result()
    } {
        log::warn!("Flush failed: {e:?}");
    }
}
```

---

## Performance Considerations

| Factor | Recommendation |
|---|---|
| RX buffer size | Set to at least 2× maximum expected burst size to avoid FIFO overflow |
| TX buffer | Use non-zero TX buffer for non-blocking writes from time-critical tasks |
| Event queue | Size = expected burst events; too small causes dropped events |
| Baud rate | Above ~1 Mbps, consider DMA mode (`uart_driver_install` with `ESP_INTR_FLAG_IRAM`) |
| Task priority | UART event task should be higher priority than data-processing tasks |
| Stack size | 2048 bytes minimum; 4096 recommended if logging is used inside the task |
| Multiple ports | Each port has independent buffers and ISR — no interference between ports |

---

## Summary

The ESP32 provides three independent hardware UART controllers that cover virtually all serial communication use cases — from simple debug consoles to multi-protocol industrial interfaces. The ESP-IDF UART driver abstracts the hardware into a clean, thread-safe API built on FreeRTOS primitives.

**Key takeaways:**

- **Three ports (UART0/1/2)**, all routable to any GPIO via the GPIO matrix. UART0 is reserved for the console by default.
- **Initialization sequence:** `uart_param_config` → `uart_set_pin` → `uart_driver_install`. Order matters.
- **Event-driven design** using `uart_driver_install` with an event queue is preferred for production firmware over polling loops.
- **Pattern detection** allows the hardware to interrupt when a delimiter character (e.g., `\n`) is received, enabling efficient line-oriented protocols.
- **RS-485 half-duplex** is natively supported — the driver automatically toggles the DE pin using the RTS signal.
- **Multiple ports** run independently; each needs its own task, buffer, and optionally event queue.
- **In Rust**, `esp-idf-hal::uart::UartDriver` provides a safe, ownership-enforced wrapper around the same underlying driver. The Rust type system prevents accidental double-use of a peripheral at compile time.
- **Flow control**, **wakeup from light sleep**, and **collision detection** are all supported by the driver.

For most applications, the pattern is: initialize once in `app_main`, spawn one FreeRTOS/thread task per UART port, and communicate between tasks using queues or channels rather than sharing UART handles directly.