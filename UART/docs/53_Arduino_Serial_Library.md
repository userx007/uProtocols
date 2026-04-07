# 53. Arduino Serial Library


**Hardware Architecture** — UART peripheral counts per board (Uno through ESP32/RP2040), the ATmega328P register map, baud rate calculation formulas, and the ISR-driven ring buffer data path.

**C/C++ Examples (6):**
1. Basic initialisation and echo server
2. Formatted output — `DEC`/`HEX`/`BIN`/float precision
3. Non-blocking line accumulator with a `<CMD>:<PARAM>` command parser
4. Raw register-level USART0 driver with ISR ring buffers (no Arduino library)
5. Multi-port USB ↔ UART1 gateway on the Mega
6. Binary framing protocol with CRC-8 and a receive state machine

**Rust Examples (4):**
7. AVR Uno via `arduino-hal` / `ufmt`
8. RP2040 Pico via `rp-hal` with blocking and non-blocking reads
9. Generic `embedded-hal` trait-based driver with software ring buffer (portable across any HAL)
10. RTIC interrupt-driven architecture with SPSC queue (ISR → idle task)

**Advanced Topics** — baud error tolerance, RS-485 direction control, `HardwareSerial` vs `SoftwareSerial` comparison table, TX back-pressure with `availableForWrite()`, custom baud rates (DMX512), and flow control options.

## Working with Arduino's Serial Class and Hardware Serial Ports

---

## Table of Contents

1. [Overview](#overview)
2. [Hardware Architecture](#hardware-architecture)
3. [The Serial Class — Under the Hood](#the-serial-class--under-the-hood)
4. [Core API Reference](#core-api-reference)
5. [C/C++ Programming Examples](#cc-programming-examples)
6. [Rust Programming Examples](#rust-programming-examples)
7. [Advanced Topics](#advanced-topics)
8. [Summary](#summary)

---

## Overview

The **Arduino Serial Library** provides a high-level, object-oriented interface to the UART (Universal Asynchronous Receiver/Transmitter) hardware peripherals embedded in AVR, ARM, RISC-V and other microcontrollers used on Arduino boards. It abstracts away direct register manipulation, offering a consistent API across different hardware targets while still giving access to the underlying hardware when needed.

Serial communication in the Arduino ecosystem follows the standard RS-232/UART protocol:
- **Asynchronous** — no shared clock line between devices.
- **Full-duplex** — simultaneous transmit (TX) and receive (RX).
- **Frame format** — configurable start bit, data bits (5–9), optional parity bit, and stop bit(s).
- **Baud rate** — must match between communicating devices; common values: 9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600.

---

## Hardware Architecture

### UART Peripherals by Board

| Board / MCU             | Hardware UARTs | Serial Objects                         | Notes                              |
|-------------------------|---------------|----------------------------------------|------------------------------------|
| Arduino Uno (ATmega328P)| 1             | `Serial`                               | USART0; shared with USB–Serial     |
| Arduino Mega (ATmega2560)| 4            | `Serial`, `Serial1`, `Serial2`, `Serial3` | USART0–USART3                   |
| Arduino Leonardo (ATmega32U4)| 1 UART + USB | `Serial` (USB CDC), `Serial1` (UART1) | `Serial` is virtual USB port     |
| Arduino Due (SAM3X8E)   | 4             | `Serial` (USB), `Serial1`–`Serial3`   | 32-bit ARM Cortex-M3              |
| Arduino Zero / MKR (SAMD21)| 6 SERCOM  | `Serial` (USB), `Serial1`             | Each SERCOM configurable as UART  |
| ESP32                   | 3             | `Serial`, `Serial1`, `Serial2`         | Flexible GPIO pin mapping         |
| RP2040 (Pico)           | 2 PL011 UARTs | `Serial` (USB CDC), `Serial1`, `Serial2`| TinyUSB stack                    |

### ATmega328P USART Register Map (Uno)

```
UBRR0H:UBRR0L  — Baud Rate Register (12-bit)
UCSR0A         — Control/Status Register A
  Bits: RXC0 TXC0 UDRE0 FE0 DOR0 UPE0 U2X0 MPCM0
UCSR0B         — Control/Status Register B
  Bits: RXCIE0 TXCIE0 UDRIE0 RXEN0 TXEN0 UCSZ02 RXB80 TXB80
UCSR0C         — Control/Status Register C
  Bits: UMSEL01 UMSEL00 UPM01 UPM00 USBS0 UCSZ01 UCSZ00 UCPOL0
UDR0           — UART Data Register (RX/TX buffer)
```

### Baud Rate Calculation

For a target baud rate *B* with system clock *f_osc*:

```
Normal mode (U2X = 0):  UBRR = (f_osc / (16 × B)) − 1
Double-speed (U2X = 1): UBRR = (f_osc / (8  × B)) − 1
```

**Example:** 115200 baud on 16 MHz Uno  
`UBRR = (16,000,000 / (16 × 115200)) − 1 = 7.68 → 8` (0.16% error — acceptable)

### Internal Buffering

The Arduino `HardwareSerial` implementation uses two circular ring buffers (default 64 bytes each, configurable via `SERIAL_TX_BUFFER_SIZE` / `SERIAL_RX_BUFFER_SIZE` macros):

```
TX Path:  write() → TX ring buffer → UDRIE ISR → UDR (hardware shift register) → TX pin
RX Path:  RX pin → hardware shift register → UDR → USART_RX ISR → RX ring buffer → read()
```

Interrupts (`USART_RX_vect`, `USART_UDRE_vect`) service the buffers asynchronously, so `Serial.print()` returns before all bytes have left the wire.

---

## The Serial Class — Under the Hood

`HardwareSerial` inherits from `Stream`, which inherits from `Print`:

```
Print
  └── Stream
        └── HardwareSerial   (Serial, Serial1, Serial2, Serial3)
```

**`Print`** provides formatted output: `print()`, `println()` with overloads for `int`, `float`, `String`, `char*`, etc., as well as numeric base formatting (`DEC`, `HEX`, `OCT`, `BIN`) and float precision.

**`Stream`** adds timed input methods: `readBytes()`, `readBytesUntil()`, `readString()`, `readStringUntil()`, `parseInt()`, `parseFloat()`, and `setTimeout()`.

**`HardwareSerial`** implements the actual UART hardware control and ISR-driven ring buffers.

---

## Core API Reference

### Initialisation

```cpp
Serial.begin(baud);
Serial.begin(baud, config);
```

`config` is built from symbolic constants:

| Symbol       | Data Bits | Parity | Stop Bits |
|--------------|-----------|--------|-----------|
| `SERIAL_8N1` | 8         | None   | 1         | ← default
| `SERIAL_8N2` | 8         | None   | 2         |
| `SERIAL_8E1` | 8         | Even   | 1         |
| `SERIAL_8O1` | 8         | Odd    | 1         |
| `SERIAL_7E1` | 7         | Even   | 1         |
| `SERIAL_7O1` | 7         | Odd    | 1         |
| `SERIAL_5N1` | 5         | None   | 1         |

```cpp
Serial.end();              // Disable UART, free pins for GPIO use
```

### Output Methods

```cpp
Serial.print(val);         // Print without newline
Serial.print(val, fmt);    // fmt: DEC, HEX, OCT, BIN, or decimal places for float
Serial.println(val);       // Print with \r\n
Serial.println(val, fmt);
size_t n = Serial.write(byte);          // Send single byte, returns 1 or 0
size_t n = Serial.write(buf, len);      // Send raw buffer
Serial.flush();            // Block until TX buffer is fully drained to wire
```

### Input Methods

```cpp
int  n  = Serial.available();          // Bytes waiting in RX buffer
int  c  = Serial.read();               // Read one byte (-1 if none)
int  c  = Serial.peek();               // Peek without consuming
size_t n = Serial.readBytes(buf, len); // Read up to len bytes (blocks up to timeout)
size_t n = Serial.readBytesUntil(term, buf, len); // Stop on terminator or timeout
String s = Serial.readString();        // Read until timeout
String s = Serial.readStringUntil(term);
long   v = Serial.parseInt();          // Parse next integer, skip non-digits
float  f = Serial.parseFloat();
void     Serial.setTimeout(ms);        // Default 1000 ms
```

### Status / Control

```cpp
if (Serial)           { /* port is open / ready */ }  // operator bool()
int tx_free = Serial.availableForWrite();              // Free bytes in TX buffer
```

---

## C/C++ Programming Examples

### Example 1 — Basic Initialisation and Echo

```cpp
// File: basic_echo.ino
// Demonstrates: begin(), available(), read(), write()

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }   // Wait for USB CDC on Leonardo/Zero/Due
    Serial.println(F("Echo server ready. Type something."));
}

void loop() {
    if (Serial.available() > 0) {
        uint8_t byte = Serial.read();
        Serial.write(byte);   // Raw echo — preserves exact byte value
    }
}
```

---

### Example 2 — Formatted Output

```cpp
// File: formatted_output.ino
// Demonstrates: print() overloads, HEX/BIN/float formatting

void printSensorReport(float voltage, uint16_t raw_adc, int8_t temperature) {
    Serial.println(F("=== Sensor Report ==="));

    Serial.print(F("Voltage   : "));
    Serial.print(voltage, 3);      // 3 decimal places
    Serial.println(F(" V"));

    Serial.print(F("ADC raw   : "));
    Serial.print(raw_adc, DEC);
    Serial.print(F("  (0x"));
    Serial.print(raw_adc, HEX);
    Serial.print(F(")  (0b"));
    Serial.print(raw_adc, BIN);
    Serial.println(F(")"));

    Serial.print(F("Temp      : "));
    Serial.print(temperature);
    Serial.println(F(" °C"));
    Serial.println();
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
}

void loop() {
    uint16_t adc = analogRead(A0);
    float    vcc = (adc / 1023.0f) * 5.0f;
    int8_t   tmp = 23;
    printSensorReport(vcc, adc, tmp);
    delay(2000);
}
```

---

### Example 3 — Robust Line-Based Command Parser

```cpp
// File: command_parser.ino
// Demonstrates: readBytesUntil(), custom protocol, multi-port use (Mega)
// Protocol: <CMD>:<PARAM>\n   e.g.  LED:1\n  SPEED:255\n

#define CMD_BUF_LEN 64

static char    g_buf[CMD_BUF_LEN];
static uint8_t g_buf_pos = 0;

// Non-blocking accumulator — avoids readBytesUntil() blocking behaviour
bool accumulateLine(char& cmd_out[CMD_BUF_LEN]) {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (g_buf_pos > 0) {
                g_buf[g_buf_pos] = '\0';
                memcpy(cmd_out, g_buf, g_buf_pos + 1);
                g_buf_pos = 0;
                return true;
            }
        } else if (g_buf_pos < CMD_BUF_LEN - 1) {
            g_buf[g_buf_pos++] = c;
        }
    }
    return false;
}

void processCommand(const char* line) {
    // Split on ':'
    const char* colon = strchr(line, ':');
    if (!colon) {
        Serial.println(F("ERR:malformed"));
        return;
    }

    char cmd[16]   = {0};
    char param[32] = {0};
    size_t cmd_len = colon - line;

    strncpy(cmd,   line,        min(cmd_len, sizeof(cmd) - 1));
    strncpy(param, colon + 1,   sizeof(param) - 1);

    if (strcmp(cmd, "LED") == 0) {
        int val = atoi(param);
        digitalWrite(LED_BUILTIN, val ? HIGH : LOW);
        Serial.print(F("OK:LED="));
        Serial.println(val);
    } else if (strcmp(cmd, "PING") == 0) {
        Serial.print(F("PONG:"));
        Serial.println(param);
    } else {
        Serial.print(F("ERR:unknown:"));
        Serial.println(cmd);
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }
    pinMode(LED_BUILTIN, OUTPUT);
    Serial.println(F("Ready. Commands: LED:<0|1>  PING:<text>"));
}

void loop() {
    char line[CMD_BUF_LEN];
    if (accumulateLine(line)) {
        processCommand(line);
    }
}
```

---

### Example 4 — Raw Register Access (ATmega328P / Uno)

Direct register manipulation — bypasses the Arduino library entirely for maximum performance and control:

```cpp
// File: raw_uart.cpp
// Target: ATmega328P @ 16 MHz
// Demonstrates: manual USART0 configuration, interrupt-driven TX/RX

#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/atomic.h>

// ── Ring buffer ────────────────────────────────────────────────────────────
#define RB_SIZE 128  // Must be power of 2
#define RB_MASK (RB_SIZE - 1)

typedef struct {
    volatile uint8_t buf[RB_SIZE];
    volatile uint8_t head;
    volatile uint8_t tail;
} RingBuf;

static RingBuf rx_buf = {{}, 0, 0};
static RingBuf tx_buf = {{}, 0, 0};

static inline bool rb_empty(const RingBuf* rb) { return rb->head == rb->tail; }
static inline bool rb_full (const RingBuf* rb) { return ((rb->head + 1) & RB_MASK) == rb->tail; }

static inline void rb_push(RingBuf* rb, uint8_t b) {
    uint8_t next = (rb->head + 1) & RB_MASK;
    if (next != rb->tail) {          // Drop silently if full
        rb->buf[rb->head] = b;
        rb->head = next;
    }
}

static inline int16_t rb_pop(RingBuf* rb) {
    if (rb->head == rb->tail) return -1;
    uint8_t b = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & RB_MASK;
    return b;
}

// ── UART initialisation ────────────────────────────────────────────────────
void uart0_init(uint32_t baud) {
    // Calculate UBRR with U2X for reduced error
    uint16_t ubrr = (F_CPU / (8UL * baud)) - 1;
    UBRR0H = (uint8_t)(ubrr >> 8);
    UBRR0L = (uint8_t)(ubrr);

    UCSR0A = _BV(U2X0);                         // Double speed
    UCSR0B = _BV(RXCIE0)                         // RX complete interrupt
           | _BV(RXEN0)                           // Enable receiver
           | _BV(TXEN0);                          // Enable transmitter
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);         // 8-N-1

    sei();
}

// ── Interrupt service routines ─────────────────────────────────────────────
ISR(USART_RX_vect) {
    uint8_t status = UCSR0A;
    uint8_t data   = UDR0;
    if (!(status & (_BV(FE0) | _BV(DOR0) | _BV(UPE0)))) {  // Check for errors
        rb_push(&rx_buf, data);
    }
}

ISR(USART_UDRE_vect) {
    int16_t b = rb_pop(&tx_buf);
    if (b >= 0) {
        UDR0 = (uint8_t)b;
    } else {
        UCSR0B &= ~_BV(UDRIE0);   // TX buffer empty — disable UDRE interrupt
    }
}

// ── Public API ─────────────────────────────────────────────────────────────
void uart0_write_byte(uint8_t b) {
    while (rb_full(&tx_buf)) { ; }     // Back-pressure if buffer full
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        rb_push(&tx_buf, b);
        UCSR0B |= _BV(UDRIE0);         // Enable UDRE interrupt to start transmission
    }
}

void uart0_write_str(const char* s) {
    while (*s) uart0_write_byte((uint8_t)*s++);
}

int16_t uart0_read_byte(void) {
    int16_t b;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        b = rb_pop(&rx_buf);
    }
    return b;
}

uint8_t uart0_available(void) {
    uint8_t h, t;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
        h = rx_buf.head;
        t = rx_buf.tail;
    }
    return (h - t) & RB_MASK;
}

// ── Usage ──────────────────────────────────────────────────────────────────
int main(void) {
    uart0_init(115200);
    uart0_write_str("Raw UART ready\r\n");

    for (;;) {
        int16_t c = uart0_read_byte();
        if (c >= 0) {
            uart0_write_byte((uint8_t)c);  // Echo
        }
    }
}
```

---

### Example 5 — Multi-Port Gateway (Arduino Mega)

```cpp
// File: serial_gateway.ino
// Target: Arduino Mega 2560
// Bridges Serial (USB host) ↔ Serial1 (external device @ 9600 8E2)

void setup() {
    Serial.begin(115200);               // USB–Serial to PC
    Serial1.begin(9600, SERIAL_8E2);    // External UART device, even parity, 2 stop bits
    while (!Serial) { ; }
    Serial.println(F("Gateway active: USB(115200,8N1) <--> UART1(9600,8E2)"));
}

void loop() {
    // USB → UART1
    while (Serial.available()) {
        Serial1.write(Serial.read());
    }

    // UART1 → USB
    while (Serial1.available()) {
        Serial.write(Serial1.read());
    }
}
```

---

### Example 6 — High-Speed Binary Protocol with Checksum

```cpp
// File: binary_protocol.ino
// Protocol frame:
//   [0xAA] [0x55] [LEN:1] [CMD:1] [PAYLOAD:LEN] [CRC8:1]

#define SYNC0  0xAA
#define SYNC1  0x55
#define MAX_PL 32

// CRC-8/MAXIM (Dallas 1-wire)
uint8_t crc8(const uint8_t* data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
        }
    }
    return crc;
}

bool sendFrame(uint8_t cmd, const uint8_t* payload, uint8_t len) {
    if (len > MAX_PL) return false;

    uint8_t buf[MAX_PL + 4];
    buf[0] = SYNC0;
    buf[1] = SYNC1;
    buf[2] = len;
    buf[3] = cmd;
    memcpy(&buf[4], payload, len);
    buf[4 + len] = crc8(&buf[2], 2 + len);  // CRC over LEN+CMD+PAYLOAD

    Serial.write(buf, 5 + len);
    return true;
}

// State-machine receiver
struct FrameRx {
    enum State : uint8_t { SYNC_A, SYNC_B, LEN, CMD, PAYLOAD, CRC } state = SYNC_A;
    uint8_t  len     = 0;
    uint8_t  cmd     = 0;
    uint8_t  payload[MAX_PL];
    uint8_t  idx     = 0;
};

static FrameRx rx;

bool pollFrame(uint8_t& cmd_out, uint8_t* pl_out, uint8_t& pl_len_out) {
    while (Serial.available()) {
        uint8_t b = Serial.read();
        switch (rx.state) {
            case FrameRx::SYNC_A:   if (b == SYNC0) rx.state = FrameRx::SYNC_B; break;
            case FrameRx::SYNC_B:   rx.state = (b == SYNC1) ? FrameRx::LEN : FrameRx::SYNC_A; break;
            case FrameRx::LEN:      rx.len = b; rx.idx = 0; rx.state = FrameRx::CMD; break;
            case FrameRx::CMD:      rx.cmd = b; rx.state = rx.len ? FrameRx::PAYLOAD : FrameRx::CRC; break;
            case FrameRx::PAYLOAD:
                rx.payload[rx.idx++] = b;
                if (rx.idx >= rx.len) rx.state = FrameRx::CRC;
                break;
            case FrameRx::CRC: {
                rx.state = FrameRx::SYNC_A;
                // Rebuild header for CRC verification
                uint8_t hdr[2] = { rx.len, rx.cmd };
                uint8_t expected = crc8(hdr, 2);
                expected = crc8_continue(rx.payload, rx.len, expected); // see below
                if (b == expected) {
                    cmd_out    = rx.cmd;
                    pl_len_out = rx.len;
                    memcpy(pl_out, rx.payload, rx.len);
                    return true;
                }
                break;
            }
        }
    }
    return false;
}

void setup() { Serial.begin(460800); }

void loop() {
    uint8_t cmd, pl[MAX_PL], plen;
    if (pollFrame(cmd, pl, plen)) {
        // Echo frame back with ACK command (0x01)
        sendFrame(0x01, pl, plen);
    }
}
```

---

## Rust Programming Examples

Rust is increasingly used for embedded development via the **`embedded-hal`** ecosystem. The following examples target AVR (Uno), RP2040 (Pico), and generic `embedded-hal` traits.

### Example 7 — AVR Uno with `avr-hal` (arduino-hal crate)

```rust
// Cargo.toml dependencies:
//   arduino-hal = { git = "https://github.com/Rahix/avr-hal", features = ["arduino-uno"] }
//   panic-halt = "0.2"

#![no_std]
#![no_main]

use arduino_hal::prelude::*;
use panic_halt as _;

#[arduino_hal::entry]
fn main() -> ! {
    let dp      = arduino_hal::Peripherals::take().unwrap();
    let pins    = arduino_hal::pins!(dp);

    // Initialise hardware USART0 at 115200 baud, 8-N-1
    let mut serial = arduino_hal::default_serial!(dp, pins, 115200);

    ufmt::uwriteln!(&mut serial, "Arduino-HAL Serial Demo\r").unwrap();

    let mut count: u32 = 0;
    loop {
        ufmt::uwriteln!(&mut serial, "Uptime: {} ticks\r", count).unwrap();
        count = count.wrapping_add(1);

        // Non-blocking read
        if let Ok(byte) = serial.read() {
            ufmt::uwriteln!(&mut serial, "Rx: 0x{:02X}\r", byte).unwrap();
        }

        arduino_hal::delay_ms(500);
    }
}
```

---

### Example 8 — RP2040 (Raspberry Pi Pico) with `rp-hal`

```rust
// Cargo.toml dependencies:
//   rp-pico  = "0.9"
//   embedded-hal = "0.2"
//   fugit    = "0.3"
//   panic-halt = "0.2"

#![no_std]
#![no_main]

use embedded_hal::serial::{Read, Write};
use panic_halt as _;
use rp_pico::entry;
use rp_pico::hal::{
    clocks::init_clocks_and_plls,
    pac,
    uart::{DataBits, StopBits, UartConfig, UartPeripheral},
    watchdog::Watchdog,
    Clock, Sio,
};
use fugit::RateExtU32;

#[entry]
fn main() -> ! {
    let mut pac  = pac::Peripherals::take().unwrap();
    let core     = pac::CorePeripherals::take().unwrap();
    let mut wdog = Watchdog::new(pac.WATCHDOG);
    let sio      = Sio::new(pac.SIO);

    let clocks = init_clocks_and_plls(
        rp_pico::XOSC_CRYSTAL_FREQ,
        pac.XOSC,
        pac.CLOCKS,
        pac.PLL_SYS,
        pac.PLL_USB,
        &mut pac.RESETS,
        &mut wdog,
    ).ok().unwrap();

    let pins = rp_pico::Pins::new(
        pac.IO_BANK0,
        pac.PADS_BANK0,
        sio.gpio_bank0,
        &mut pac.RESETS,
    );

    // UART0 on GPIO0 (TX) and GPIO1 (RX)
    let uart_pins = (pins.gpio0.into_function(), pins.gpio1.into_function());

    let mut uart = UartPeripheral::new(pac.UART0, uart_pins, &mut pac.RESETS)
        .enable(
            UartConfig::new(115_200.Hz(), DataBits::Eight, None, StopBits::One),
            clocks.peripheral_clock.freq(),
        )
        .unwrap();

    uart.write_full_blocking(b"RP2040 UART ready\r\n");

    let mut buf = [0u8; 64];
    let mut pos = 0usize;

    loop {
        match uart.read() {
            Ok(byte) => {
                buf[pos] = byte;
                // Echo
                let _ = uart.write(byte);

                if byte == b'\n' || pos >= buf.len() - 1 {
                    uart.write_full_blocking(b"\r\nReceived: ");
                    uart.write_full_blocking(&buf[..pos]);
                    uart.write_full_blocking(b"\r\n");
                    pos = 0;
                } else {
                    pos += 1;
                }
            }
            Err(_) => { /* No data available */ }
        }
    }
}
```

---

### Example 9 — Generic `embedded-hal` Trait-Based UART Driver

A reusable driver that works across any `embedded-hal`-compatible platform:

```rust
// File: src/uart_driver.rs
// Works with embedded-hal 0.2.x Read + Write traits

use embedded_hal::serial::{Read, Write};
use nb::block;

/// Errors that can occur in the serial driver
#[derive(Debug)]
pub enum SerialError<RE, WE> {
    ReadError(RE),
    WriteError(WE),
    Timeout,
    BufferOverflow,
}

/// Ring-buffer backed serial driver wrapper
pub struct SerialDriver<UART, const BUF: usize>
where
    UART: Read<u8> + Write<u8>,
{
    uart: UART,
    rx_buf: [u8; BUF],
    rx_head: usize,
    rx_tail: usize,
}

impl<UART, const BUF: usize> SerialDriver<UART, BUF>
where
    UART: Read<u8> + Write<u8>,
{
    pub fn new(uart: UART) -> Self {
        Self {
            uart,
            rx_buf: [0u8; BUF],
            rx_head: 0,
            rx_tail: 0,
        }
    }

    /// Non-blocking receive — poll and buffer available bytes
    pub fn poll_rx(&mut self)
    where
        <UART as Read<u8>>::Error: core::fmt::Debug,
    {
        loop {
            match self.uart.read() {
                Ok(byte) => {
                    let next = (self.rx_head + 1) % BUF;
                    if next != self.rx_tail {
                        self.rx_buf[self.rx_head] = byte;
                        self.rx_head = next;
                    }
                    // Silently drop on overflow — consider error signalling in production
                }
                Err(nb::Error::WouldBlock) => break,
                Err(nb::Error::Other(_))   => break,
            }
        }
    }

    /// Read one byte from the software buffer
    pub fn read_byte(&mut self) -> Option<u8> {
        if self.rx_head == self.rx_tail {
            return None;
        }
        let byte = self.rx_buf[self.rx_tail];
        self.rx_tail = (self.rx_tail + 1) % BUF;
        Some(byte)
    }

    /// Bytes available in receive buffer
    pub fn available(&self) -> usize {
        (self.rx_head + BUF - self.rx_tail) % BUF
    }

    /// Blocking write of a byte slice
    pub fn write_all(&mut self, data: &[u8])
    where
        <UART as Write<u8>>::Error: core::fmt::Debug,
    {
        for &byte in data {
            block!(self.uart.write(byte)).unwrap();
        }
        block!(self.uart.flush()).unwrap();
    }

    /// Read bytes until terminator character or buffer full
    pub fn read_until(&mut self, term: u8, out: &mut [u8]) -> usize {
        let mut n = 0;
        while n < out.len() {
            if let Some(b) = self.read_byte() {
                out[n] = b;
                n += 1;
                if b == term { break; }
            }
        }
        n
    }

    /// Send a formatted decimal number (no_std compatible)
    pub fn write_u32(&mut self, mut val: u32)
    where
        <UART as Write<u8>>::Error: core::fmt::Debug,
    {
        let mut tmp = [0u8; 10];
        let mut len = 0;
        if val == 0 {
            self.write_all(b"0");
            return;
        }
        while val > 0 {
            tmp[len] = b'0' + (val % 10) as u8;
            val /= 10;
            len += 1;
        }
        // Reverse digits
        let mut out = [0u8; 10];
        for i in 0..len { out[i] = tmp[len - 1 - i]; }
        self.write_all(&out[..len]);
    }
}

// ── Application ────────────────────────────────────────────────────────────
// Using SerialDriver with RP2040:
//
// let mut drv: SerialDriver<_, 128> = SerialDriver::new(uart);
// loop {
//     drv.poll_rx();
//     let mut line = [0u8; 64];
//     let n = drv.read_until(b'\n', &mut line);
//     if n > 0 {
//         drv.write_all(b"Echo: ");
//         drv.write_all(&line[..n]);
//     }
// }
```

---

### Example 10 — Interrupt-Driven UART with RTIC (RP2040)

Using the **RTIC** (Real-Time Interrupt-driven Concurrency) framework:

```rust
// Cargo.toml:
//   rtic = { version = "2", features = ["thumbv6-backend"] }
//   rp-pico = "0.9"
//   heapless = "0.8"

#![no_std]
#![no_main]

use heapless::spsc::{Consumer, Producer, Queue};
use rtic::app;
use rp_pico::hal::{pac, uart::UartPeripheral};

static mut RX_QUEUE: Queue<u8, 256> = Queue::new();

#[app(device = rp_pico::hal::pac, peripherals = true, dispatchers = [SW0_IRQ])]
mod app {
    use super::*;

    #[shared]
    struct Shared {}

    #[local]
    struct Local {
        uart:     UartPeripheral</* ... */>,
        rx_prod:  Producer<'static, u8, 256>,
        rx_cons:  Consumer<'static, u8, 256>,
    }

    #[init]
    fn init(cx: init::Context) -> (Shared, Local, init::Monotonics) {
        let (rx_prod, rx_cons) = unsafe { RX_QUEUE.split() };
        // ... uart setup omitted for brevity ...
        uart.enable_rx_interrupt();
        (Shared {}, Local { uart, rx_prod, rx_cons }, init::Monotonics())
    }

    /// UART RX interrupt — runs at hardware priority
    #[task(binds = UART0_IRQ, local = [uart, rx_prod])]
    fn uart_irq(cx: uart_irq::Context) {
        while let Ok(byte) = cx.local.uart.read() {
            let _ = cx.local.rx_prod.enqueue(byte);
        }
    }

    /// Idle task — processes received data
    #[idle(local = [rx_cons])]
    fn idle(cx: idle::Context) -> ! {
        loop {
            if let Some(byte) = cx.local.rx_cons.dequeue() {
                // Process byte without blocking interrupt context
                process_byte(byte);
            }
        }
    }
}

fn process_byte(_byte: u8) {
    // Application logic here
}
```

---

## Advanced Topics

### 1. Baud Rate Error and Tolerance

UART receivers tolerate a cumulative bit timing error of approximately **±2–4%** over a full frame. Sources of error:

- **Quantisation error** from integer UBRR rounding (calculated above).
- **Crystal accuracy** — typical ±20–50 ppm (negligible).
- **RC oscillator accuracy** — ATmega internal RC: ±10% un-calibrated, ±1% with OSCCAL tuning.

When using the internal RC oscillator, always tune `OSCCAL` via bootloader or factory calibration byte before relying on serial at high baud rates.

### 2. RS-485 Half-Duplex Direction Control

```cpp
// File: rs485.ino
// Manages a DE/RE pin for RS-485 transceiver direction

#define RS485_DE_PIN  2   // Driver Enable — HIGH = transmit

void rs485_send(const uint8_t* data, size_t len) {
    digitalWrite(RS485_DE_PIN, HIGH);   // Enable driver
    delayMicroseconds(10);              // Transceiver propagation delay

    Serial1.write(data, len);
    Serial1.flush();                    // Wait until last byte leaves shift register

    delayMicroseconds(10);
    digitalWrite(RS485_DE_PIN, LOW);    // Disable driver — enter receive mode
}

void setup() {
    Serial.begin(115200);               // Debug
    Serial1.begin(9600, SERIAL_8N1);    // RS-485 bus
    pinMode(RS485_DE_PIN, OUTPUT);
    digitalWrite(RS485_DE_PIN, LOW);    // Default: receive
}
```

### 3. SoftwareSerial vs HardwareSerial

| Feature               | HardwareSerial           | SoftwareSerial               |
|-----------------------|--------------------------|------------------------------|
| Implementation        | Hardware USART + ISR     | Bit-banged via timer/GPIO ISR |
| Max reliable baud     | 2 Mbps+ (target-dependent)| ~115200 on 16 MHz AVR        |
| Simultaneous ports    | Up to 4 (Mega)           | Multiple instances, 1 active at a time |
| CPU overhead          | Low — ISR-driven         | High — blocks CPU during RX  |
| Full-duplex           | Yes                       | Limited (not reliable)       |
| Interrupt conflict    | None                      | Conflicts with other ISRs    |

### 4. TX Buffer Pressure and `availableForWrite()`

```cpp
// File: back_pressure.ino
// Demonstrates non-blocking writes using availableForWrite()

void sendNonBlocking(const uint8_t* data, size_t len) {
    size_t sent = 0;
    while (sent < len) {
        int space = Serial.availableForWrite();
        if (space > 0) {
            size_t chunk = min((size_t)space, len - sent);
            Serial.write(data + sent, chunk);
            sent += chunk;
        }
        // Yield here in RTOS context, or perform other work
    }
}
```

### 5. Custom Baud Rates

```cpp
// Direct UBRR manipulation for non-standard baud rates (ATmega)
// Example: 250000 baud (used by DMX512 lighting protocol)

void Serial_begin_custom(uint32_t baud) {
    uint16_t ubrr = (F_CPU / (8UL * baud)) - 1;
    UBRR0H = ubrr >> 8;
    UBRR0L = ubrr;
    UCSR0A = _BV(U2X0);
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);  // 8-N-1
}
```

### 6. Flow Control

Arduino's `HardwareSerial` does **not** implement hardware RTS/CTS flow control. Options:

- **XON/XOFF (software):** Transmit ASCII `0x13` (XOFF) when RX buffer fills; `0x11` (XON) when drained. Must be implemented in application code.
- **Hardware RTS/CTS:** Requires GPIO management in user code on the Arduino side; the USART peripheral on most AVR devices does not automate this.
- **ESP32/SAMD/STM32:** Their UART peripherals support hardware flow control natively via HAL configuration.

---

## Summary

The **Arduino Serial Library** is a well-designed abstraction layer over hardware UART peripherals that delivers:

**Architecture:** A three-level class hierarchy (`Print` → `Stream` → `HardwareSerial`) with ISR-driven ring buffers for non-blocking asynchronous I/O. Direct register access remains available when the abstraction overhead is unacceptable.

**C/C++ Programming:** The API provides `begin(baud, config)`, `print()`/`println()` for formatted output (decimal, hex, octal, binary, float precision), `write()` for raw binary data, `available()`/`read()`/`peek()` for polling-based receive, and timed methods (`readBytesUntil()`, `parseInt()`) inherited from `Stream`. The `flush()` method drains the TX buffer to wire. On multi-UART boards (Mega, Due, ESP32) each port is an independent `HardwareSerial` instance with identical API.

**Rust Programming:** The `embedded-hal` ecosystem provides `Read<u8>` and `Write<u8>` traits as the standard UART abstraction. `arduino-hal` wraps AVR USART hardware; `rp-hal` wraps RP2040 PL011 UARTs. Generic trait-based drivers decouple application logic from hardware, and RTIC enables safe interrupt-driven concurrency with zero-cost abstractions. The `heapless` crate provides `no_std`-compatible ring buffers (SPSC queues) suitable for ISR-to-task data transfer.

**Key Design Considerations:**

- Match baud rates on both ends; check for quantisation error at high speeds.
- Size RX/TX ring buffers to your worst-case latency × throughput product.
- Use `availableForWrite()` to avoid blocking in latency-sensitive loops.
- Prefer `HardwareSerial` over `SoftwareSerial` for anything above ~57600 baud.
- Use `Serial.flush()` before power-down or mode-switch to avoid truncated frames.
- For RS-485, assert DE/RE with appropriate guard time around `flush()`.
- On Leonardo/Zero/Due, `while (!Serial)` is required because `Serial` is a USB CDC device — it becomes valid only when the host opens the COM port.

Together, the Arduino Serial Library and the Rust `embedded-hal` ecosystem cover the full spectrum from rapid prototyping with high-level convenience methods to production-grade, interrupt-driven, formally type-safe embedded UART implementations.