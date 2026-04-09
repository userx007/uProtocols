# 100. Future of Asynchronous Serial

**Evolution of UART** — from RS-232 with ±12 V and DB-9 connectors through TTL/3.3 V/1.8 V variants, up to modern MCU enhancements like DMA, FIFO buffers, IDLE-line detection, LPUART, RS-485 mode, and auto-baud detection.

**USB-C and UART bridging** — USB-UART bridge chips (FTDI, CP2102N, CH340), USB CDC-ACM firmware on MCUs with USB peripherals, and the niche use of USB-C SBU pins for debug UART.

**Modern alternatives** — RS-485/Modbus, LIN, CAN/CAN FD, SPI, I²C, USB CDC, LoRa, BLE NUS — with a comparison table and a decision flowchart.

**C/C++ code examples:**
- Linux `termios2` with custom baud rates (921600+)
- STM32 HAL UART with DMA + IDLE-line interrupt
- RP2040 PIO-based UART for arbitrary baud/pin
- RS-485 direction control via Linux `TIOCSRS485` ioctl
- STM32 USB CDC-ACM middleware
- RP2040 TinyUSB CDC echo

**Rust code examples:**
- `tokio-serial` async UART on Linux
- Embassy async UART with DMA on STM32
- RTIC interrupt-driven UART on nRF52840
- Embassy USB CDC-ACM on RP2040
- `serialport-rs` port enumeration/filtering


## Evolution of UART, USB-C UART, and Modern Alternatives

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [The Evolution of UART](#2-the-evolution-of-uart)
3. [Modern UART Enhancements](#3-modern-uart-enhancements)
4. [USB-C and UART Bridging](#4-usb-c-and-uart-bridging)
5. [Modern Alternatives to Classic UART](#5-modern-alternatives-to-classic-uart)
6. [Wireless Serial Alternatives](#6-wireless-serial-alternatives)
7. [Programming Modern UART in C/C++](#7-programming-modern-uart-in-cc)
8. [Programming Modern UART in Rust](#8-programming-modern-uart-in-rust)
9. [USB-CDC Virtual COM Port in C/C++](#9-usb-cdc-virtual-com-port-in-cc)
10. [USB-CDC Virtual COM Port in Rust](#10-usb-cdc-virtual-com-port-in-rust)
11. [Choosing the Right Protocol](#11-choosing-the-right-protocol)
12. [Summary](#12-summary)

---

## 1. Introduction

UART (Universal Asynchronous Receiver/Transmitter) has been a cornerstone of embedded communication since the early days of computing. Originally designed for RS-232 serial terminals in the 1960s and 1970s, UART's fundamental principle — asynchronous, frame-based transmission of bytes over two wires — has proven remarkably durable.

Yet the landscape of serial communication is undergoing profound change. USB-C has displaced legacy connectors. High-speed buses like USB 3.x, PCIe, and MIPI achieve gigabit throughputs. Wireless protocols such as BLE, Wi-Fi, and Zigbee eliminate wires entirely. And modern microcontrollers offer UART peripherals with DMA, FIFO buffers, auto-baud detection, and hardware flow control far beyond the original 8250 UART chip.

This document examines:

- How UART itself has evolved.
- How USB-C integrates with and extends serial communication.
- What modern alternatives exist and when to prefer them.
- Practical C/C++ and Rust programming patterns for these technologies.

---

## 2. The Evolution of UART

### 2.1 From RS-232 to TTL UART

The original RS-232 standard used ±12 V signaling, DB-9 or DB-25 connectors, and a host of modem control signals (RTS, CTS, DTR, DSR, DCD, RI). Over decades, embedded systems stripped this down to the essentials:

| Era | Voltage | Connector | Key Feature |
|-----|---------|-----------|-------------|
| RS-232 (1969) | ±12 V | DB-9/DB-25 | Full modem signals |
| TTL UART (1990s) | 5 V | Header pins | Simplified, low cost |
| LVTTL/3.3 V UART (2000s) | 3.3 V | Header pins | Low-power MCUs |
| 1.8 V UART (2010s) | 1.8 V | FPC/pads | Ultra-low-power IoT |

### 2.2 Enhanced UART Peripheral Features

Modern UART peripherals in microcontrollers (STM32, RP2040, nRF52, ESP32, etc.) include:

- **FIFO buffers** — Reduce interrupt overhead; typically 16–64 byte FIFOs.
- **DMA support** — Transfer entire blocks without CPU involvement.
- **Hardware flow control** — RTS/CTS lines managed by hardware.
- **Auto-baud detection** — Measure incoming baud rate automatically.
- **LIN mode** — Local Interconnect Network bus for automotive.
- **IrDA mode** — Infrared serial communication.
- **Smartcard (ISO 7816) mode** — For SIM cards and contact-based cards.
- **RS-485 half-duplex mode** — DE/RE pin management built into hardware.
- **Wakeup from sleep** — Match byte or address wakeup patterns.

### 2.3 UART in the IoT Era

Even as higher-speed alternatives proliferate, UART endures in IoT because:

- It requires only two wires (TX, RX) with no clock line.
- Debug consoles on nearly every SoC use UART.
- AT-command modems (cellular, Wi-Fi, BLE) still present a UART interface.
- Boot ROM and bootloaders universally support UART.
- Power consumption at low baud rates is negligible.

---

## 3. Modern UART Enhancements

### 3.1 High-Speed UART

Classic UART maxes out around 115,200 bps in practice. Modern MCUs support much higher rates:

| Speed | Common Use |
|-------|------------|
| 115,200 bps | Debug console, legacy devices |
| 921,600 bps | Fast sensor streams, GNSS |
| 3 Mbps | High-speed MCU-to-MCU links |
| 12 Mbps | Some USB-UART bridge chips (CP2102N) |

### 3.2 RS-485: Industrial Serial

RS-485 extends UART for multi-drop, long-distance, noisy industrial environments:

- Differential signaling (±7 V common mode range).
- Up to 32–256 devices on one bus.
- Up to 1,200 m at low speeds; 10 Mbps at short distances.
- Half-duplex: a direction-enable (DE) pin controls transmit/receive.
- Used in Modbus RTU, DMX512, BACnet MS/TP.

### 3.3 LIN Bus

LIN (Local Interconnect Network) is a single-wire, master/slave protocol built on UART:

- 1-wire + ground, up to 20 kbps.
- Used in automotive body electronics (window motors, seat controls, lighting).
- The UART hardware generates the LIN break field and checksum.

---

## 4. USB-C and UART Bridging

### 4.1 USB-C as a Universal Serial Connector

USB-C (released 2014, now dominant) is fundamentally a connector, not a protocol. The same physical USB-C port can carry:

- USB 2.0 / 3.2 / 4 (Thunderbolt 4)
- DisplayPort / HDMI (Alternate Mode)
- Power Delivery (up to 240 W)
- **UART via USB-CDC or USB-UART bridge chips**

### 4.2 USB-UART Bridge Chips

The most practical way to expose UART over USB-C is a bridge chip. These devices appear as a virtual COM port (VCP) on the host OS with no drivers needed on modern systems (via USB CDC-ACM class):

| Chip | Max Speed | Notes |
|------|-----------|-------|
| FTDI FT232RN | 3 Mbps | Industry standard, broad OS support |
| Silicon Labs CP2102N | 3 Mbps | Very small QFN package |
| WCH CH340G | 2 Mbps | Low cost, widely used in Arduino clones |
| Prolific PL2303 | 12 Mbps | Common in cables |
| Microchip MCP2221A | 115,200 | Also has I2C/GPIO |

These chips connect to a USB-C receptacle externally, then present TTL UART (TX/RX/RTS/CTS) to the target MCU.

### 4.3 USB Alternate Mode: UART via SBU Pins

USB-C defines SBU1/SBU2 (Sideband Use) pins, which some designs repurpose for low-speed UART debug channels independent of the main USB data path. This is used in:

- **Google's UART-over-USB-C (SuzyQ cable / CCD)** for Chromebook debugging.
- Various proprietary debug accessories.

### 4.4 USB CDC-ACM: UART without a Bridge Chip

When the target MCU has a USB peripheral (e.g., STM32, RP2040, nRF52840), it can implement USB CDC-ACM (Communications Device Class – Abstract Control Model) in firmware, appearing as a virtual serial port directly. No bridge chip is needed.

---

## 5. Modern Alternatives to Classic UART

### 5.1 Comparison Table

| Protocol | Wires | Speed | Distance | Multi-drop | Use Case |
|----------|-------|-------|----------|------------|----------|
| UART/RS-232 | 2–9 | Up to 115.2 kbps typical | ~15 m | No | Debug, modems |
| RS-485 | 2 (diff) | Up to 10 Mbps | 1,200 m | Yes (32+) | Industrial, Modbus |
| SPI | 3–4 | 10s of Mbps | Short (PCB) | Yes (CS per device) | Sensors, flash |
| I²C | 2 | 100k–3.4 Mbps | Short (PCB) | Yes (addr) | Sensors, EEPROMs |
| USB CDC | 1 (cable) | 12–480 Mbps | 5 m | No (USB hub) | PC–device comms |
| CAN bus | 2 (diff) | 1 Mbps (CAN FD: 8 Mbps) | 1,000 m | Yes (110 nodes) | Automotive, robotics |
| LIN | 1 | 20 kbps | 40 m | Yes (16 slaves) | Automotive body |
| Ethernet/IP | 2–4 pairs | 10 Mbps–10 Gbps | 100 m | Yes | Industrial IoT |
| LPUART | 2 | Low baud | Short | No | Low-power IoT wakeup |

### 5.2 Single-Wire UART

Some MCUs support single-wire half-duplex UART where TX and RX share one pin (e.g., STM32 USART in half-duplex mode). Used for compact wiring in space-constrained designs.

### 5.3 LPUART (Low-Power UART)

Found in STM32L, nRF9160, and similar ultra-low-power MCUs. LPUART can:

- Operate from a 32.768 kHz LSE clock.
- Receive data while the CPU is in deep sleep.
- Wake the CPU on address match or specific byte.
- Achieve ~10 µA active current vs 1+ mA for standard UART.

---

## 6. Wireless Serial Alternatives

Modern designs frequently replace wired UART with a wireless link:

| Technology | Typical Range | Throughput | Protocol |
|------------|--------------|-----------|----------|
| BLE (Bluetooth LE) | 10–100 m | ~1–2 Mbps PHY, ~100 kbps app | BLE Nordic UART Service (NUS) |
| Wi-Fi (TCP/UDP) | 50–200 m | 10s of Mbps | Raw TCP, MQTT, HTTP |
| Zigbee | 10–100 m | 250 kbps | ZigBee serial profiles |
| Thread/Matter | 10–100 m | 250 kbps | IPv6-based |
| LoRa / LoRaWAN | km range | 0.3–50 kbps | Long-range IoT |
| 900 MHz FSK | 100–1,000 m | 10–500 kbps | Point-to-point modules |

Modules like ESP32, nRF52840, and SARA-R4 expose these wireless stacks via — you guessed it — UART AT commands, so UART remains as the application interface even when the air interface is wireless.

---

## 7. Programming Modern UART in C/C++

### 7.1 Linux / POSIX: High-Speed UART with Termios

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <asm/termbits.h>  // For custom baud rates via BOTHER

/**
 * Open and configure a UART port with a custom baud rate using termios2.
 * Supports non-standard rates like 921600, 1500000, 3000000 on Linux.
 */
int uart_open_custom_baud(const char *device, int baud_rate) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return -1;
    }

    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) != 0) {
        perror("TCGETS2");
        close(fd);
        return -1;
    }

    // Raw mode: no echo, no canonical, no signals
    tio.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON | IXOFF | IXANY);
    tio.c_oflag &= ~OPOST;
    tio.c_cflag &= ~(CSIZE | PARENB | CSTOPB | CRTSCTS);
    tio.c_cflag |= CS8 | CREAD | CLOCAL;
    tio.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON | ISIG | IEXTEN);

    // Custom baud rate via BOTHER
    tio.c_cflag &= ~CBAUD;
    tio.c_cflag |= BOTHER;
    tio.c_ispeed = baud_rate;
    tio.c_ospeed = baud_rate;

    // Blocking read: return after 1 byte, 100 ms timeout
    tio.c_cc[VMIN]  = 1;
    tio.c_cc[VTIME] = 1;

    if (ioctl(fd, TCSETS2, &tio) != 0) {
        perror("TCSETS2");
        close(fd);
        return -1;
    }

    // Flush pending data
    ioctl(fd, TCFLSH, TCIOFLUSH);

    // Switch back to blocking mode
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);

    return fd;
}

/**
 * Send a null-terminated string over UART with retry on partial writes.
 */
ssize_t uart_send_string(int fd, const char *msg) {
    size_t total = strlen(msg);
    size_t sent  = 0;
    while (sent < total) {
        ssize_t n = write(fd, msg + sent, total - sent);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("write");
            return -1;
        }
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

/**
 * Receive bytes until a newline or buffer full. Returns bytes read.
 */
ssize_t uart_recv_line(int fd, char *buf, size_t maxlen) {
    size_t idx = 0;
    while (idx < maxlen - 1) {
        ssize_t n = read(fd, &buf[idx], 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            perror("read");
            return -1;
        }
        if (n == 0) break;  // Timeout / EOF
        if (buf[idx] == '\n') { idx++; break; }
        idx++;
    }
    buf[idx] = '\0';
    return (ssize_t)idx;
}

int main(void) {
    const char *port = "/dev/ttyUSB0";
    int baud = 921600;

    int fd = uart_open_custom_baud(port, baud);
    if (fd < 0) return EXIT_FAILURE;

    printf("Opened %s at %d baud\n", port, baud);

    uart_send_string(fd, "AT\r\n");

    char response[256];
    ssize_t n = uart_recv_line(fd, response, sizeof(response));
    if (n > 0)
        printf("Response: %s\n", response);

    close(fd);
    return EXIT_SUCCESS;
}
```

---

### 7.2 STM32 HAL: UART with DMA and Idle-Line Detection

A modern pattern for variable-length frames uses DMA + UART IDLE interrupt, which fires when the line goes idle after receiving bytes (no need to know frame length in advance).

```c
// uart_dma.h
#pragma once
#include "stm32f4xx_hal.h"

#define UART_RX_DMA_BUF_SIZE  256

typedef struct {
    UART_HandleTypeDef *huart;
    DMA_HandleTypeDef  *hdma_rx;
    uint8_t             dma_buf[UART_RX_DMA_BUF_SIZE];
    uint8_t             app_buf[UART_RX_DMA_BUF_SIZE];
    volatile uint16_t   app_len;
    volatile uint8_t    data_ready;
} UartDmaCtx;

void uart_dma_init(UartDmaCtx *ctx, UART_HandleTypeDef *huart, DMA_HandleTypeDef *hdma);
void uart_dma_idle_isr(UartDmaCtx *ctx);
```

```c
// uart_dma.c
#include "uart_dma.h"
#include <string.h>

void uart_dma_init(UartDmaCtx *ctx,
                   UART_HandleTypeDef *huart,
                   DMA_HandleTypeDef  *hdma) {
    ctx->huart    = huart;
    ctx->hdma_rx  = hdma;
    ctx->data_ready = 0;
    ctx->app_len  = 0;

    // Enable IDLE line interrupt in USART_CR1
    __HAL_UART_ENABLE_IT(huart, UART_IT_IDLE);

    // Start circular DMA receive into ring buffer
    HAL_UART_Receive_DMA(huart, ctx->dma_buf, UART_RX_DMA_BUF_SIZE);
}

/**
 * Call this from the USARTx_IRQHandler when IDLE flag is set.
 * Calculates how many bytes arrived since last call and copies them.
 */
void uart_dma_idle_isr(UartDmaCtx *ctx) {
    if (__HAL_UART_GET_FLAG(ctx->huart, UART_FLAG_IDLE)) {
        __HAL_UART_CLEAR_IDLEFLAG(ctx->huart);

        uint16_t dma_remaining = (uint16_t)__HAL_DMA_GET_COUNTER(ctx->hdma_rx);
        uint16_t received      = UART_RX_DMA_BUF_SIZE - dma_remaining;

        if (received > 0) {
            memcpy(ctx->app_buf, ctx->dma_buf, received);
            ctx->app_len   = received;
            ctx->data_ready = 1;
        }
    }
}

// --- Example main loop usage ---
//
// extern UartDmaCtx g_uart_ctx;
//
// void USART1_IRQHandler(void) {
//     HAL_UART_IRQHandler(&huart1);
//     uart_dma_idle_isr(&g_uart_ctx);
// }
//
// void process_uart(void) {
//     if (g_uart_ctx.data_ready) {
//         g_uart_ctx.data_ready = 0;
//         handle_frame(g_uart_ctx.app_buf, g_uart_ctx.app_len);
//     }
// }
```

---

### 7.3 RP2040: PIO-Based UART for Flexible Baud Rates

The RP2040 Programmable I/O (PIO) state machines can implement UART in software, enabling arbitrary baud rates and unconventional configurations:

```c
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "uart_tx.pio.h"   // Generated from .pio file by pioasm

// PIO UART TX at any baud rate, any GPIO pin
void pio_uart_tx_init(PIO pio, uint sm, uint gpio, uint baud_rate) {
    uint offset = pio_add_program(pio, &uart_tx_program);
    pio_sm_config c = uart_tx_program_get_default_config(offset);

    // Map the state machine TX FIFO to the GPIO
    sm_config_set_sideset_pins(&c, gpio);
    pio_gpio_init(pio, gpio);
    pio_sm_set_consecutive_pindirs(pio, sm, gpio, 1, true);

    // Set baud: PIO clock divisor
    float div = (float)clock_get_hz(clk_sys) / (8.0f * baud_rate);
    sm_config_set_clkdiv(&c, div);

    // OSR shifts right, autopull at 8 bits
    sm_config_set_out_shift(&c, true, false, 32);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

void pio_uart_putc(PIO pio, uint sm, char c) {
    // Block until TX FIFO has space
    pio_sm_put_blocking(pio, sm, (uint32_t)c);
}

void pio_uart_puts(PIO pio, uint sm, const char *str) {
    while (*str) pio_uart_putc(pio, sm, *str++);
}

int main(void) {
    stdio_init_all();

    PIO pio = pio0;
    uint sm  = 0;
    uint tx_pin = 4;

    pio_uart_tx_init(pio, sm, tx_pin, 115200);
    pio_uart_puts(pio, sm, "Hello from PIO UART!\r\n");

    while (true) tight_loop_contents();
}
```

---

### 7.4 RS-485 Direction Control in C

```c
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <linux/serial.h>

/**
 * Enable RS485 mode via Linux kernel RS485 ioctl.
 * The kernel toggles RTS automatically before/after transmission.
 */
int uart_enable_rs485(int fd, int rts_on_send, int rts_after_send) {
    struct serial_rs485 rs485;
    memset(&rs485, 0, sizeof(rs485));

    rs485.flags = SER_RS485_ENABLED;
    if (rts_on_send)    rs485.flags |= SER_RS485_RTS_ON_SEND;
    if (!rts_after_send) rs485.flags |= SER_RS485_RTS_AFTER_SEND;

    rs485.delay_rts_before_send = 0;  // microseconds
    rs485.delay_rts_after_send  = 0;

    if (ioctl(fd, TIOCSRS485, &rs485) < 0) {
        perror("TIOCSRS485");
        return -1;
    }
    return 0;
}

// Usage: after uart_open_custom_baud(), call:
//   uart_enable_rs485(fd, 1, 0);
// Then normal write() calls work; kernel handles DE/RE pin via RTS.
```

---

## 8. Programming Modern UART in Rust

### 8.1 Rust on Linux: tokio + serialport-rs for Async UART

```rust
// Cargo.toml dependencies:
// tokio = { version = "1", features = ["full"] }
// serialport = "4"
// tokio-serial = "5"
// bytes = "1"

use std::time::Duration;
use tokio_serial::{SerialPortBuilderExt, SerialStream};
use tokio::io::{AsyncReadExt, AsyncWriteExt};

/// Open a serial port asynchronously using tokio-serial.
async fn open_uart(port: &str, baud: u32) -> tokio_serial::Result<SerialStream> {
    tokio_serial::new(port, baud)
        .timeout(Duration::from_millis(200))
        .data_bits(tokio_serial::DataBits::Eight)
        .parity(tokio_serial::Parity::None)
        .stop_bits(tokio_serial::StopBits::One)
        .flow_control(tokio_serial::FlowControl::None)
        .open_native_async()
}

/// Send a command and receive the response line.
async fn send_at_command(
    port: &mut SerialStream,
    cmd: &str,
) -> tokio_serial::Result<String> {
    // Send
    port.write_all(cmd.as_bytes()).await?;
    port.flush().await?;

    // Receive up to 256 bytes with a timeout baked into the port
    let mut buf = vec![0u8; 256];
    let n = port.read(&mut buf).await?;

    let response = String::from_utf8_lossy(&buf[..n]).trim().to_owned();
    Ok(response)
}

#[tokio::main]
async fn main() -> tokio_serial::Result<()> {
    let port_name = "/dev/ttyUSB0";
    let baud_rate = 115_200;

    let mut port = open_uart(port_name, baud_rate).await?;
    println!("Opened {} at {} baud", port_name, baud_rate);

    let reply = send_at_command(&mut port, "AT\r\n").await?;
    println!("Modem reply: {:?}", reply);

    Ok(())
}
```

---

### 8.2 Rust Embedded: Embassy Async UART on STM32

Embassy is a modern async embedded framework for Rust. It brings `async/await` to bare-metal MCUs, with UART using DMA transparently.

```rust
// Cargo.toml:
// embassy-stm32 = { version = "*", features = ["stm32f401re", "time-driver-any"] }
// embassy-executor = { version = "*", features = ["arch-cortex-m"] }
// embassy-time = "*"

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_stm32::usart::{Config, Uart};
use embassy_stm32::dma::NoDma;
use embassy_stm32::{bind_interrupts, peripherals, usart};
use embassy_time::{Duration, Timer};

bind_interrupts!(struct Irqs {
    USART2 => usart::InterruptHandler<peripherals::USART2>;
});

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());

    let config = Config::default();
    // PA2 = TX, PA3 = RX on Nucleo F401RE
    let mut uart = Uart::new(
        p.USART2,
        p.PA3,   // RX
        p.PA2,   // TX
        Irqs,
        p.DMA1_CH6,  // TX DMA
        p.DMA1_CH5,  // RX DMA
        config,
    ).unwrap();

    loop {
        // Async write — DMA in background, await completion
        uart.write(b"Hello from Embassy async UART!\r\n").await.unwrap();

        // Async read — fills exactly the buffer
        let mut buf = [0u8; 64];
        match embassy_time::with_timeout(
            Duration::from_millis(500),
            uart.read(&mut buf),
        ).await {
            Ok(Ok(_)) => {
                // Echo received bytes back
                uart.write(&buf).await.unwrap();
            }
            Ok(Err(e)) => defmt::error!("UART error: {:?}", e),
            Err(_)     => defmt::warn!("Read timeout"),
        }

        Timer::after(Duration::from_millis(100)).await;
    }
}
```

---

### 8.3 Rust Embedded: RTIC + UART Interrupt-Driven on nRF52840

RTIC (Real-Time Interrupt-driven Concurrency) is an alternative to Embassy for hard real-time requirements:

```rust
#![no_std]
#![no_main]

use nrf52840_hal as hal;
use hal::uarte::{Baudrate, Parity, Uarte, UarteRx, UarteTx};
use hal::pac::UARTE0;
use heapless::spsc::{Producer, Consumer, Queue};
use heapless::String;

static mut RX_QUEUE: Queue<u8, 256> = Queue::new();

#[rtic::app(device = nrf52840_hal::pac, peripherals = true)]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        uart_tx: UarteTx<UARTE0>,
        uart_rx: UarteRx<UARTE0>,
        rx_prod: Producer<'static, u8, 256>,
        rx_cons: Consumer<'static, u8, 256>,
        rx_byte: u8,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local, init::Monotonics) {
        let (rx_prod, rx_cons) = unsafe { RX_QUEUE.split() };

        let p0 = hal::gpio::p0::Parts::new(cx.device.P0);
        let txd = p0.p0_06.into_push_pull_output(hal::gpio::Level::High).degrade();
        let rxd = p0.p0_08.into_floating_input().degrade();

        let uarte = Uarte::new(
            cx.device.UARTE0,
            hal::uarte::Pins { txd, rxd, cts: None, rts: None },
            Parity::EXCLUDED,
            Baudrate::BAUD115200,
        );
        let (uart_tx, uart_rx) = uarte.split();

        let rx_byte: u8 = 0;

        (
            Shared {},
            Local { uart_tx, uart_rx, rx_prod, rx_cons, rx_byte },
            init::Monotonics(),
        )
    }

    /// UARTE0 RX interrupt — fires on each received byte
    #[task(binds = UARTE0_UART0, local = [uart_rx, rx_prod, rx_byte])]
    fn uart_rx_isr(cx: uart_rx_isr::Context) {
        // Read one byte (non-blocking in ISR context)
        if let Ok(b) = cx.local.uart_rx.read() {
            let _ = cx.local.rx_prod.enqueue(b);
        }
    }

    #[idle(local = [uart_tx, rx_cons])]
    fn idle(cx: idle::Context) -> ! {
        loop {
            // Drain the receive queue and echo back
            while let Some(byte) = cx.local.rx_cons.dequeue() {
                // Echo with newline on carriage return
                let _ = cx.local.uart_tx.write(byte);
                if byte == b'\r' {
                    let _ = cx.local.uart_tx.write(b'\n');
                }
            }
            cortex_m::asm::wfi();  // Sleep until next interrupt
        }
    }
}
```

---

## 9. USB-CDC Virtual COM Port in C/C++

### 9.1 STM32 USB CDC with HAL

STM32CubeMX generates USB CDC middleware. The key application interface:

```c
// usbd_cdc_if.c — Application-level CDC callbacks

#include "usbd_cdc_if.h"
#include <string.h>

// Ring buffer for received data
#define CDC_RX_BUF_SIZE 1024
static uint8_t  cdc_rx_buf[CDC_RX_BUF_SIZE];
static uint16_t cdc_rx_head = 0;
static uint16_t cdc_rx_tail = 0;
static volatile uint8_t cdc_connected = 0;

// Called by USB stack when host opens/closes the port
// (DTR signal via SET_CONTROL_LINE_STATE)
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t *pbuf, uint16_t length) {
    switch (cmd) {
        case CDC_SET_CONTROL_LINE_STATE: {
            // pbuf[0] bit 0 = DTR, bit 1 = RTS
            cdc_connected = (pbuf[0] & 0x01) ? 1 : 0;
            break;
        }
        case CDC_SET_LINE_CODING: {
            // Host sets baud rate — can mirror to a physical UART if bridging
            // USBD_CDC_LineCodingTypeDef *lc = (USBD_CDC_LineCodingTypeDef*)pbuf;
            break;
        }
    }
    return USBD_OK;
}

// Called when USB host sends data to device
static int8_t CDC_Receive_FS(uint8_t *buf, uint32_t *len) {
    uint32_t n = *len;
    for (uint32_t i = 0; i < n; i++) {
        cdc_rx_buf[cdc_rx_head] = buf[i];
        cdc_rx_head = (cdc_rx_head + 1) % CDC_RX_BUF_SIZE;
    }
    // Re-arm USB receive endpoint
    USBD_CDC_SetRxBuffer(&hUsbDeviceFS, buf);
    USBD_CDC_ReceivePacket(&hUsbDeviceFS);
    return USBD_OK;
}

// Application: send data over USB CDC
HAL_StatusTypeDef CDC_Send(const uint8_t *data, uint16_t len) {
    if (!cdc_connected) return HAL_ERROR;

    uint32_t timeout = HAL_GetTick() + 100;
    while (CDC_Transmit_FS((uint8_t *)data, len) == USBD_BUSY) {
        if (HAL_GetTick() > timeout) return HAL_TIMEOUT;
        HAL_Delay(1);
    }
    return HAL_OK;
}

// Application: read one byte from ring buffer
int CDC_ReadByte(void) {
    if (cdc_rx_head == cdc_rx_tail) return -1;  // Empty
    uint8_t b  = cdc_rx_buf[cdc_rx_tail];
    cdc_rx_tail = (cdc_rx_tail + 1) % CDC_RX_BUF_SIZE;
    return b;
}
```

---

### 9.2 RP2040 TinyUSB CDC

The Pico SDK integrates TinyUSB. stdio can be routed over USB CDC automatically:

```c
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "tusb.h"

int main(void) {
    // Route stdio (printf) over USB CDC
    stdio_usb_init();

    // Wait up to 2 s for a host to open the CDC port
    uint32_t start = board_millis();
    while (!tud_cdc_connected()) {
        tud_task();  // Must call periodically to process USB
        if (board_millis() - start > 2000) break;
    }

    printf("RP2040 USB CDC ready\r\n");

    while (true) {
        tud_task();  // Process USB events

        if (tud_cdc_available()) {
            uint8_t buf[64];
            uint32_t count = tud_cdc_read(buf, sizeof(buf));

            // Echo back with modification
            for (uint32_t i = 0; i < count; i++) {
                // Convert lowercase to uppercase
                if (buf[i] >= 'a' && buf[i] <= 'z')
                    buf[i] -= 32;
            }
            tud_cdc_write(buf, count);
            tud_cdc_write_flush();
        }
    }
}
```

---

## 10. USB-CDC Virtual COM Port in Rust

### 10.1 RP2040 USB CDC with Embassy and usbd-serial

```rust
#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_rp::bind_interrupts;
use embassy_rp::usb::{Driver, InterruptHandler};
use embassy_rp::peripherals::USB;
use embassy_usb::class::cdc_acm::{CdcAcmClass, State};
use embassy_usb::{Builder, Config};
use embassy_time::Timer;
use static_cell::StaticCell;

bind_interrupts!(struct Irqs {
    USBCTRL_IRQ => InterruptHandler<USB>;
});

static STATE: StaticCell<State> = StaticCell::new();

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_rp::init(Default::default());

    let driver = Driver::new(p.USB, Irqs);

    let mut config = Config::new(0xc0de, 0xcafe);
    config.manufacturer = Some("Embedded Rust");
    config.product      = Some("USB CDC Example");
    config.serial_number = Some("12345678");

    let mut builder = {
        static DEVICE_DESC: StaticCell<[u8; 256]> = StaticCell::new();
        static CONFIG_DESC: StaticCell<[u8; 256]> = StaticCell::new();
        static BOS_DESC:    StaticCell<[u8; 256]> = StaticCell::new();
        static CONTROL_BUF:StaticCell<[u8; 64]>  = StaticCell::new();
        Builder::new(
            driver,
            config,
            DEVICE_DESC.init([0; 256]),
            CONFIG_DESC.init([0; 256]),
            BOS_DESC.init([0; 256]),
            &mut [],
            CONTROL_BUF.init([0; 64]),
        )
    };

    let state = STATE.init(State::new());
    let mut class = CdcAcmClass::new(&mut builder, state, 64);
    let mut usb   = builder.build();

    // Run USB task in background
    spawner.spawn(usb_task(usb)).unwrap();

    loop {
        // Wait for host to open the CDC port
        class.wait_connection().await;
        defmt::info!("CDC connected");

        loop {
            let mut buf = [0u8; 64];
            match class.read_packet(&mut buf).await {
                Ok(n) if n > 0 => {
                    // Echo upper-cased response
                    let upper: heapless::Vec<u8, 64> = buf[..n]
                        .iter()
                        .map(|&b| if b.is_ascii_lowercase() { b - 32 } else { b })
                        .collect();
                    let _ = class.write_packet(&upper).await;
                }
                Ok(_)  => Timer::after_millis(1).await,
                Err(_) => break,  // Disconnected
            }
        }
        defmt::info!("CDC disconnected");
    }
}

#[embassy_executor::task]
async fn usb_task(mut usb: embassy_usb::UsbDevice<'static, Driver<'static, USB>>) {
    usb.run().await;
}
```

---

### 10.2 Rust on Linux: Enumerating and Selecting Serial Ports

```rust
use serialport;

/// List all available serial ports and filter USB-UART bridges.
fn list_usb_serial_ports() {
    let ports = serialport::available_ports().expect("No ports found");

    for port in &ports {
        match &port.port_type {
            serialport::SerialPortType::UsbPort(info) => {
                println!(
                    "USB Serial: {}  VID:{:04X} PID:{:04X}  Manufacturer: {}  Product: {}",
                    port.port_name,
                    info.vid,
                    info.pid,
                    info.manufacturer.as_deref().unwrap_or("?"),
                    info.product.as_deref().unwrap_or("?"),
                );
            }
            serialport::SerialPortType::BluetoothPort => {
                println!("Bluetooth Serial: {}", port.port_name);
            }
            serialport::SerialPortType::PciPort => {
                println!("PCI Serial: {}", port.port_name);
            }
            serialport::SerialPortType::Unknown => {
                println!("Unknown: {}", port.port_name);
            }
        }
    }
}

fn main() {
    list_usb_serial_ports();
}
```

---

## 11. Choosing the Right Protocol

Use this decision guide when designing a new system:

```
Is the link primarily for debug / firmware update?
  └─ Yes → UART (simple, universal bootloader support)
            OR USB-CDC (no extra chip if MCU has USB)

Is PCB-level sensor integration needed?
  ├─ Short distance, single device → SPI (fastest)
  ├─ Multi-device, few pins → I²C
  └─ Industrial noise, multi-drop → RS-485 / Modbus

Is a host PC connection needed?
  ├─ MCU has USB peripheral → USB CDC-ACM (no extra chip)
  └─ MCU has no USB → USB-UART bridge (FT232, CP2102)

Is long cable distance required?
  ├─ < 100 m, moderate speed → RS-485
  ├─ > 100 m → Ethernet / Industrial Ethernet (EtherCAT)
  └─ Very long, low-speed IoT → RS-485 at low baud, or LoRa/LoRaWAN

Is ultra-low power a priority?
  └─ LPUART with wakeup on address byte

Is the application automotive?
  ├─ Body electronics, few nodes → LIN
  └─ Powertrain, safety systems → CAN / CAN FD

Is wireless preferred?
  ├─ < 100 m, high throughput → Wi-Fi TCP/UDP
  ├─ < 100 m, low power → BLE NUS (Nordic UART Service)
  └─ km range, very low throughput → LoRa / LoRaWAN
```

---

## 12. Summary

### Key Takeaways

**UART's resilience** comes from its simplicity. Two wires, no clock, minimal protocol overhead — these properties keep UART relevant for debug consoles, bootloaders, AT-command modems, and low-speed sensor links regardless of what higher-speed alternatives emerge.

**USB-C integration** is primarily achieved through USB-UART bridge chips (FTDI, CP2102N, CH340) or, better still, USB CDC-ACM implemented directly in MCU firmware. The SBU pins in USB-C also allow proprietary UART debug channels independent of the main USB path.

**Modern UART enhancements** — DMA, IDLE-line interrupts, auto-baud, RS-485 mode, LPUART wakeup — dramatically improve UART's usability and efficiency on modern MCUs without changing the fundamental protocol.

**Embedded Rust** has reached production readiness for UART and USB-CDC:
- **Embassy** provides async UART and USB-CDC with DMA, clean `async/await` syntax, and broad MCU support.
- **RTIC** provides hard real-time interrupt-driven UART for deterministic systems.
- **serialport-rs + tokio-serial** provide ergonomic async UART on Linux hosts.

**For new designs**, the practical hierarchy is:
1. If the MCU has USB → USB CDC-ACM (no extra BOM cost).
2. If no USB → USB-UART bridge for host connectivity; raw UART for embedded-to-embedded.
3. For industrial multi-drop → RS-485 with Modbus RTU.
4. For wireless → BLE NUS or Wi-Fi TCP using an AT-command module (which itself speaks UART).
5. For ultra-low power → LPUART with wakeup-from-sleep.

UART will not disappear. It is baked into every SoC's ROM, debug infrastructure, and peripheral set. What continues to evolve is the transport layer above it — USB bridging, wireless tunneling, and DMA-driven async firmware architectures that make UART faster, lower-power, and easier to program than ever before.

---

*Document: 100 – Future of Asynchronous Serial | Topic Series: UART and Serial Communication*