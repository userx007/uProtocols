# 19. UART Break Detection

**Topic Coverage** — The document explains what a UART break condition is (TX line held LOW longer than one full character frame), how it differs from ordinary framing errors, and its timing characteristics across protocols like LIN Bus, DMX512, and RS-232.

**C/C++ Examples** include:
- **Linux** — `termios` raw mode with `PARMRK` to capture breaks as the `0xFF 0x00 0x00` sentinel sequence, plus `ioctl`-based `TIOCSBRK`/`TIOCCBRK` for sending breaks
- **Windows** — Win32 `WaitCommEvent` with `EV_BREAK` and overlapped I/O, plus `SetCommBreak`/`ClearCommBreak`
- **STM32** — Register-level USART2 with LIN break detection interrupt (`LBDIE`/`LBD` flag)
- **AVR ATmega** — ISR-based framing error + null byte detection

**Rust Examples** include:
- `serialport` crate — cross-platform `set_break()`/`clear_break()` and PARMRK-based receive detection
- `nix` crate — direct `termios` access for Linux with full `InputFlags::PARMRK` configuration
- `embedded-hal` / `stm32f4xx-hal` — `no_std` interrupt handler with atomic flag for LIN break detection

The summary consolidates the key rules for distinguishing breaks from framing errors, handling the `0xFF` escape ambiguity in PARMRK mode, and managing interrupt latency on embedded targets.

## Identifying and Handling Extended Low-Level Break Conditions

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is a UART Break Condition?](#what-is-a-uart-break-condition)
3. [Break Condition Timing and Signaling](#break-condition-timing-and-signaling)
4. [Hardware Detection Mechanisms](#hardware-detection-mechanisms)
5. [Software Detection Approaches](#software-detection-approaches)
6. [Break Detection in C/C++](#break-detection-in-cc)
   - [Linux (termios API)](#linux-termios-api)
   - [Windows (Win32 API)](#windows-win32-api)
   - [Bare-Metal / Embedded (Register-Level)](#bare-metal--embedded-register-level)
7. [Break Detection in Rust](#break-detection-in-rust)
   - [Using the `serialport` Crate](#using-the-serialport-crate)
   - [Low-Level Linux via `nix`](#low-level-linux-via-nix)
   - [Embedded Rust (UART HAL)](#embedded-rust-uart-hal)
8. [Sending a Break Signal](#sending-a-break-signal)
9. [Common Use Cases](#common-use-cases)
10. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
11. [Summary](#summary)

---

## Introduction

Break detection is a critical feature of UART (Universal Asynchronous Receiver/Transmitter) communication that allows devices to identify abnormal line conditions — specifically, when the transmit line is held **low** for longer than the duration of a full character frame. This mechanism is widely used for protocol synchronization, attention signaling, line fault detection, and baud-rate auto-negotiation.

Understanding break detection requires familiarity with the UART framing protocol, how hardware UART peripherals report line errors, and how to handle those events in both operating-system-level and bare-metal programming environments.

---

## What Is a UART Break Condition?

In standard UART communication, the idle line state is **logic HIGH** (mark state). A normal character transmission begins with a **start bit** (logic LOW), followed by data bits, an optional parity bit, and one or more **stop bits** (logic HIGH).

A **break condition** occurs when the transmitter holds the line **continuously LOW** for a duration **longer than one complete character frame** (start bit + data bits + parity + stop bits). To the receiver, this looks like a start bit that never ends — the stop bit never goes HIGH.

```
Normal frame (8N1):
  ___    _________    _________
     |__|         |__|         |___  (idle = HIGH)
  [START][D0..D7][STOP]

Break condition:
  _________________________________________
 |                                         |  (line held LOW)
 |_________________________________________|
  <- longer than one full frame duration ->
```

Key characteristics of a break condition:

- The line remains LOW for **at least one full character time** (commonly defined as 10–13 bit periods for 8N1)
- It is **not** valid data — the UART hardware flags it as a special error/event
- The duration may be fixed (e.g., exactly 2 character times) or variable depending on protocol
- After the break ends, the line returns to the idle HIGH state before normal transmission resumes

---

## Break Condition Timing and Signaling

The exact timing of a break condition varies by protocol and application:

| Protocol / Context       | Typical Break Duration      | Purpose                          |
|--------------------------|-----------------------------|----------------------------------|
| RS-232 general use       | > 1 character frame         | Attention / reset signal         |
| LIN Bus (automotive)     | 13+ bit times               | Frame synchronization header     |
| DMX512 (stage lighting)  | 92 µs minimum               | New packet start signal          |
| MIDI (rare use)          | System reset signaling       | Reset all devices                |
| Serial console (Linux)   | ~250 ms (varies)            | Generate kernel SysRq             |
| Modem AT commands        | ~1.5 seconds                | Escape to command mode           |

For an 8N1 frame at 9600 baud:

- 1 bit period = 1/9600 ≈ 104 µs
- 1 full frame = 10 bits = ~1.04 ms
- A break must be **> 1.04 ms** to be detected as such at this baud rate

---

## Hardware Detection Mechanisms

Most UART peripherals detect breaks automatically in hardware and report them via status registers or interrupt flags.

### Line Status Register (LSR) — 16550 UART

The classic 16550 UART (and most compatible UARTs) has a **Line Status Register** at offset +5 from the base address:

```
Bit 4: BI — Break Interrupt
       Set when a break condition is detected on the RX line.
       Cleared when the LSR is read.

Bit 3: FE — Framing Error
       Often set simultaneously with BI.

Bit 2: PE — Parity Error
       May also be set during a break.

Bit 1: OE — Overrun Error

Bit 0: DR — Data Ready
```

When a break is received, the UART typically:
1. Sets the `BI` bit in the LSR
2. Places a **null byte (0x00)** in the receive FIFO, flagged with an error
3. Triggers a **receiver line status interrupt** (IIR = 0x06) if interrupts are enabled

### ARM Cortex-M UART (e.g., STM32 USART)

On STM32 devices, the USART Status Register (USART_SR / ISR) includes:

```
Bit 9 (USART_SR_LBD): LIN Break Detection Flag
                       Set when a break of >= 10 or 11 bits is detected
                       (configured via LBDL bit in CR2)
Bit 3 (USART_SR_FE):  Framing Error — also set during break
```

LIN break detection can be enabled with a dedicated interrupt (`USART_CR2_LBDIE`).

---

## Software Detection Approaches

There are two primary software approaches to detecting breaks:

### 1. Hardware-Assisted Detection (Preferred)

- Read the UART status register or use interrupt callbacks
- The hardware flags the break; software just needs to check the flag
- Most accurate — zero latency, no guesswork

### 2. Timing-Based Software Detection

- Measure the duration of a received LOW pulse
- If the duration exceeds `(bits_per_frame / baud_rate) * 1.0`, treat as break
- Used when hardware support is unavailable or unreliable
- Requires a high-resolution timer and careful ISR implementation

---

## Break Detection in C/C++

### Linux (termios API)

On Linux, a break received on a serial port is reported as a **null byte with no error flags** when certain termios options are set, or as a special signal (`SIGINT`) if `BRKINT` is set in `c_iflag`.

#### Method 1: Detect Break as Null Byte (Raw Mode)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>

#define SERIAL_PORT "/dev/ttyS0"

int configure_serial_raw(int fd) {
    struct termios tty;

    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        return -1;
    }

    // Raw mode: disable all processing
    cfmakeraw(&tty);

    // Set baud rate
    cfsetispeed(&tty, B9600);
    cfsetospeed(&tty, B9600);

    // 8N1
    tty.c_cflag &= ~PARENB;   // No parity
    tty.c_cflag &= ~CSTOPB;   // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;        // 8 data bits

    // IMPORTANT for break detection:
    // Clear IGNBRK so breaks are NOT ignored
    tty.c_iflag &= ~IGNBRK;
    // Clear BRKINT so break does NOT raise SIGINT
    tty.c_iflag &= ~BRKINT;
    // Set PARMRK to mark parity/framing errors with \377 \0 prefix
    // A break is reported as: \377 \0 \0  (three-byte sequence)
    tty.c_iflag |= PARMRK;

    // Non-blocking read
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;  // 100ms timeout

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        return -1;
    }
    return 0;
}

int main(void) {
    int fd = open(SERIAL_PORT, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open");
        return EXIT_FAILURE;
    }

    if (configure_serial_raw(fd) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }

    printf("Listening for break conditions on %s...\n", SERIAL_PORT);

    unsigned char buf[256];
    int i = 0;
    unsigned char ring[3] = {0};  // Sliding window for \377 \0 \0

    while (1) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                usleep(10000);  // 10ms
                continue;
            }
            perror("read");
            break;
        }

        for (ssize_t j = 0; j < n; j++) {
            // Shift ring buffer
            ring[0] = ring[1];
            ring[1] = ring[2];
            ring[2] = buf[j];

            // Break sequence: 0xFF 0x00 0x00
            if (ring[0] == 0xFF && ring[1] == 0x00 && ring[2] == 0x00) {
                printf("[BREAK DETECTED] Break condition received!\n");
                // Reset ring to avoid re-triggering
                memset(ring, 0xFF, sizeof(ring));
            } else if (ring[2] != 0x00) {
                printf("Data: 0x%02X\n", ring[2]);
            }
        }
    }

    close(fd);
    return EXIT_SUCCESS;
}
```

#### Method 2: Using `TIOCMGET` / `ioctl` for Line State

```c
#include <sys/ioctl.h>

// Check if RX line is currently in break state
int check_break_state(int fd) {
    int status;
    if (ioctl(fd, TIOCMGET, &status) < 0) {
        perror("ioctl TIOCMGET");
        return -1;
    }
    // TIOCM_LE = Line Enable — reflects carrier/break on some drivers
    // This is driver-dependent; use PARMRK method for reliability
    return (status & TIOCM_LE) ? 1 : 0;
}

// Send a break condition (hold TX low for ~250ms)
int send_break(int fd, int duration_ms) {
    // tcsendbreak: if duration == 0, send ~0.25s break
    // Otherwise, duration is implementation-defined
    if (tcsendbreak(fd, duration_ms) != 0) {
        perror("tcsendbreak");
        return -1;
    }
    return 0;
}
```

---

### Windows (Win32 API)

On Windows, UART break detection uses `GetCommModemStatus`, `SetCommBreak`/`ClearCommBreak`, and the `EV_BREAK` event via `WaitCommEvent`.

```c
#include <windows.h>
#include <stdio.h>

HANDLE open_serial_port(const char *port_name) {
    HANDLE hSerial = CreateFileA(
        port_name,
        GENERIC_READ | GENERIC_WRITE,
        0,                // No sharing
        NULL,             // Default security
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED,
        NULL
    );

    if (hSerial == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s: error %lu\n", port_name, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Configure port parameters
    DCB dcbSerialParams = {0};
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hSerial, &dcbSerialParams)) {
        fprintf(stderr, "GetCommState failed: %lu\n", GetLastError());
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    dcbSerialParams.BaudRate = CBR_9600;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity   = NOPARITY;

    if (!SetCommState(hSerial, &dcbSerialParams)) {
        fprintf(stderr, "SetCommState failed: %lu\n", GetLastError());
        CloseHandle(hSerial);
        return INVALID_HANDLE_VALUE;
    }

    // Set timeouts
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = 50;
    timeouts.ReadTotalTimeoutConstant    = 50;
    timeouts.ReadTotalTimeoutMultiplier  = 10;
    SetCommTimeouts(hSerial, &timeouts);

    return hSerial;
}

void wait_for_break(HANDLE hSerial) {
    // Mask for break detection event
    if (!SetCommMask(hSerial, EV_BREAK | EV_ERR | EV_RXCHAR)) {
        fprintf(stderr, "SetCommMask failed: %lu\n", GetLastError());
        return;
    }

    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (ov.hEvent == NULL) {
        fprintf(stderr, "CreateEvent failed: %lu\n", GetLastError());
        return;
    }

    printf("Waiting for break condition...\n");

    while (1) {
        DWORD dwEvtMask = 0;

        if (!WaitCommEvent(hSerial, &dwEvtMask, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) {
                fprintf(stderr, "WaitCommEvent failed: %lu\n", GetLastError());
                break;
            }
            // Wait for overlapped operation
            DWORD dwWait = WaitForSingleObject(ov.hEvent, INFINITE);
            if (dwWait != WAIT_OBJECT_0) {
                fprintf(stderr, "WaitForSingleObject failed: %lu\n", GetLastError());
                break;
            }
            DWORD dwBytes;
            GetOverlappedResult(hSerial, &ov, &dwBytes, FALSE);
        }

        if (dwEvtMask & EV_BREAK) {
            printf("[BREAK DETECTED] Break condition received on serial port!\n");

            // Clear the break condition
            ClearCommError(hSerial, NULL, NULL);
        }

        if (dwEvtMask & EV_ERR) {
            DWORD dwErrors;
            COMSTAT comStat;
            ClearCommError(hSerial, &dwErrors, &comStat);

            if (dwErrors & CE_BREAK) {
                printf("[BREAK via CE_BREAK] Break error flagged in ClearCommError!\n");
            }
            if (dwErrors & CE_FRAME) {
                printf("[FRAME ERROR] Framing error detected.\n");
            }
        }

        ResetEvent(ov.hEvent);
    }

    CloseHandle(ov.hEvent);
}

// Send a break signal on Windows
void send_break_windows(HANDLE hSerial, DWORD duration_ms) {
    SetCommBreak(hSerial);     // Assert break (hold TX low)
    Sleep(duration_ms);        // Hold for specified duration
    ClearCommBreak(hSerial);   // Release break
    printf("Break sent for %lu ms\n", duration_ms);
}

int main(void) {
    HANDLE hSerial = open_serial_port("COM3");
    if (hSerial == INVALID_HANDLE_VALUE) return 1;

    wait_for_break(hSerial);

    CloseHandle(hSerial);
    return 0;
}
```

---

### Bare-Metal / Embedded (Register-Level)

#### STM32 (USART with LIN Break Detection)

```c
#include "stm32f4xx.h"

// Global flag set by interrupt
volatile uint8_t break_detected = 0;
volatile uint8_t received_byte  = 0;

void USART2_Init(void) {
    // Enable clocks
    RCC->AHB1ENR  |= RCC_AHB1ENR_GPIOAEN;
    RCC->APB1ENR  |= RCC_APB1ENR_USART2EN;

    // Configure PA2 (TX) and PA3 (RX) as AF7
    GPIOA->MODER   |= (2U << 4) | (2U << 6);  // Alternate function
    GPIOA->AFR[0]  |= (7U << 8) | (7U << 12); // AF7 = USART2

    // Set baud rate: 9600 at 42 MHz APB1
    // USARTDIV = 42000000 / (16 * 9600) = 273.4375
    USART2->BRR = (273 << 4) | 7;   // Mantissa=273, Fraction=7

    // Enable USART, TX, RX
    USART2->CR1 = USART_CR1_UE | USART_CR1_TE | USART_CR1_RE;

    // Enable LIN mode and LIN break detection interrupt
    // LBDL = 0: 10-bit break detection
    // LBDIE = 1: LIN break detection interrupt enable
    USART2->CR2 |= USART_CR2_LINEN | USART_CR2_LBDIE;

    // Also enable receive interrupt for normal data
    USART2->CR1 |= USART_CR1_RXNEIE;

    // Enable NVIC interrupt
    NVIC_SetPriority(USART2_IRQn, 1);
    NVIC_EnableIRQ(USART2_IRQn);
}

void USART2_IRQHandler(void) {
    uint32_t sr = USART2->SR;

    // Check for LIN break detection
    if (sr & USART_SR_LBD) {
        break_detected = 1;
        USART2->SR &= ~USART_SR_LBD;  // Clear the LBD flag
        // Optionally: discard next byte (sync byte in LIN)
    }

    // Check for framing error (also set during break)
    if (sr & USART_SR_FE) {
        // A framing error during reception may indicate a break
        // Must read DR to clear FE flag
        (void)USART2->DR;
    }

    // Normal data reception
    if (sr & USART_SR_RXNE) {
        received_byte = (uint8_t)(USART2->DR & 0xFF);
        // Process received_byte as needed
    }
}

void process_uart(void) {
    if (break_detected) {
        break_detected = 0;
        // Handle break — e.g., start new LIN frame
        printf("LIN Break detected! Starting new frame...\n");
        // ... handle LIN sync and ID bytes
    }

    if (received_byte) {
        printf("Received: 0x%02X\n", received_byte);
        received_byte = 0;
    }
}

// Send a break on STM32
void USART2_SendBreak(void) {
    // Set SBK bit: USART will send a break at next opportunity
    USART2->CR1 |= USART_CR1_SBK;
    // Hardware clears SBK automatically after break is sent
}

int main(void) {
    USART2_Init();

    while (1) {
        process_uart();
        // Other application logic...
    }
}
```

#### AVR ATmega (Classic 8-bit)

```c
#include <avr/io.h>
#include <avr/interrupt.h>
#include <stdint.h>

#define BAUD 9600UL
#define UBRR_VALUE (F_CPU / (16UL * BAUD) - 1)

volatile uint8_t break_flag = 0;

void uart_init(void) {
    // Set baud rate
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE);

    // Enable RX, TX, and RX Complete + USART Data Register Empty interrupts
    UCSR0B = (1 << RXEN0) | (1 << TXEN0) | (1 << RXCIE0);

    // 8N1 frame format
    UCSR0C = (1 << UCSZ01) | (1 << UCSZ00);

    sei();  // Enable global interrupts
}

ISR(USART_RX_vect) {
    uint8_t status = UCSR0A;
    uint8_t data   = UDR0;  // Always read UDR0 to clear the interrupt

    // Check Framing Error bit (FE0)
    // On AVR, a break condition causes a framing error AND data = 0x00
    if ((status & (1 << FE0)) && (data == 0x00)) {
        break_flag = 1;
        return;
    }

    // Normal data
    // Process data here...
    (void)data;
}

int main(void) {
    uart_init();

    while (1) {
        if (break_flag) {
            break_flag = 0;
            // Handle break condition
            // e.g., reset protocol state machine
        }
    }
}
```

---

## Break Detection in Rust

### Using the `serialport` Crate

The `serialport` crate is the standard cross-platform serial library for Rust. As of recent versions, it provides break support on both Linux and Windows.

Add to `Cargo.toml`:

```toml
[dependencies]
serialport = "4"
```

#### Receiving and Detecting Break

```rust
use serialport::{SerialPort, SerialPortBuilder};
use std::io::{self, Read};
use std::time::Duration;

fn open_port(path: &str, baud: u32) -> Box<dyn SerialPort> {
    serialport::new(path, baud)
        .timeout(Duration::from_millis(100))
        .data_bits(serialport::DataBits::Eight)
        .parity(serialport::Parity::None)
        .stop_bits(serialport::StopBits::One)
        .flow_control(serialport::FlowControl::None)
        .open()
        .expect("Failed to open serial port")
}

fn main() {
    let mut port = open_port("/dev/ttyUSB0", 9600);

    println!("Listening on serial port. Waiting for data or break...");

    let mut buf = [0u8; 256];
    // Ring buffer to detect PARMRK break sequence: 0xFF 0x00 0x00
    let mut ring: [u8; 3] = [0xFF, 0xFF, 0xFF];

    loop {
        match port.read(&mut buf) {
            Ok(0) => {
                // Timeout — no data
                continue;
            }
            Ok(n) => {
                for &byte in &buf[..n] {
                    ring[0] = ring[1];
                    ring[1] = ring[2];
                    ring[2] = byte;

                    // PARMRK break sequence: 0xFF 0x00 0x00
                    if ring == [0xFF, 0x00, 0x00] {
                        println!("[BREAK DETECTED] Break condition received!");
                        ring = [0xFF, 0xFF, 0xFF]; // Reset to avoid re-trigger
                    } else if ring[2] != 0x00 {
                        println!("Data byte: 0x{:02X}", ring[2]);
                    }
                }
            }
            Err(ref e) if e.kind() == io::ErrorKind::TimedOut => {
                continue;
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
}
```

#### Sending a Break Signal

```rust
use serialport::SerialPort;
use std::time::Duration;

fn send_break(port: &mut dyn SerialPort, duration: Duration) -> Result<(), serialport::Error> {
    port.set_break()?;           // Assert break (TX line goes LOW)
    std::thread::sleep(duration);
    port.clear_break()?;         // Release break (TX line returns HIGH)
    println!("Break sent for {:?}", duration);
    Ok(())
}

fn main() {
    let mut port = serialport::new("/dev/ttyUSB0", 9600)
        .timeout(Duration::from_millis(100))
        .open()
        .expect("Failed to open port");

    // Send a 250ms break (typical for LIN or DMX sync)
    send_break(port.as_mut(), Duration::from_millis(250))
        .expect("Failed to send break");
}
```

---

### Low-Level Linux via `nix`

For fine-grained control on Linux, use the `nix` crate to access `termios` and `ioctl` directly.

```toml
[dependencies]
nix = { version = "0.27", features = ["termios", "ioctl", "fs"] }
```

```rust
use nix::fcntl::{open, OFlag};
use nix::sys::stat::Mode;
use nix::sys::termios::{
    self, BaudRate, ControlFlags, InputFlags, LocalFlags, OutputFlags,
    SetArg, SpecialCharacterIndices, Termios,
};
use nix::unistd::{read, close};
use std::os::unix::io::RawFd;

fn configure_raw_with_parmrk(fd: RawFd) -> nix::Result<()> {
    let mut tty: Termios = termios::tcgetattr(fd)?;

    // Raw mode
    termios::cfmakeraw(&mut tty);

    // Set baud rate
    termios::cfsetispeed(&mut tty, BaudRate::B9600)?;
    termios::cfsetospeed(&mut tty, BaudRate::B9600)?;

    // Break detection configuration:
    // Remove IGNBRK: do not ignore break
    tty.input_flags.remove(InputFlags::IGNBRK);
    // Remove BRKINT: break does not generate SIGINT
    tty.input_flags.remove(InputFlags::BRKINT);
    // Add PARMRK: mark parity/framing errors — break arrives as 0xFF 0x00 0x00
    tty.input_flags.insert(InputFlags::PARMRK);

    // Non-blocking read
    tty.control_chars[SpecialCharacterIndices::VMIN as usize] = 0;
    tty.control_chars[SpecialCharacterIndices::VTIME as usize] = 1; // 100ms

    termios::tcsetattr(fd, SetArg::TCSANOW, &tty)?;
    Ok(())
}

fn listen_for_break(fd: RawFd) {
    let mut buf = [0u8; 256];
    let mut ring: [u8; 3] = [0xFF; 3];

    println!("Listening for break (PARMRK mode)...");
    loop {
        match read(fd, &mut buf) {
            Ok(0) => continue,
            Ok(n) => {
                for &byte in &buf[..n] {
                    ring[0] = ring[1];
                    ring[1] = ring[2];
                    ring[2] = byte;
                    if ring == [0xFF, 0x00, 0x00] {
                        println!("[BREAK DETECTED]");
                        ring = [0xFF; 3];
                    }
                }
            }
            Err(nix::errno::Errno::EAGAIN) => {
                std::thread::sleep(std::time::Duration::from_millis(10));
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }
}

fn main() {
    let fd = open(
        "/dev/ttyS0",
        OFlag::O_RDWR | OFlag::O_NOCTTY | OFlag::O_NONBLOCK,
        Mode::empty(),
    )
    .expect("Cannot open serial port");

    configure_raw_with_parmrk(fd).expect("Cannot configure port");
    listen_for_break(fd);
    close(fd).ok();
}
```

---

### Embedded Rust (UART HAL)

Using `embedded-hal` with the `stm32f4xx-hal` crate:

```toml
[dependencies]
cortex-m = "0.7"
cortex-m-rt = "0.7"
stm32f4xx-hal = { version = "0.21", features = ["stm32f411"] }
panic-halt = "0.2"
```

```rust
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    serial::{config::Config, Serial},
};
use core::sync::atomic::{AtomicBool, Ordering};

// Atomic flag for break detection (set in interrupt, polled in main)
static BREAK_DETECTED: AtomicBool = AtomicBool::new(false);

#[entry]
fn main() -> ! {
    let dp = pac::Peripherals::take().unwrap();
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.sysclk(84.MHz()).freeze();

    let gpioa = dp.GPIOA.split();
    let tx_pin = gpioa.pa2.into_alternate::<7>();
    let rx_pin = gpioa.pa3.into_alternate::<7>();

    // Configure USART2 at 9600 baud
    let serial = Serial::new(
        dp.USART2,
        (tx_pin, rx_pin),
        Config::default().baudrate(9600.bps()),
        &clocks,
    )
    .unwrap();

    let (mut tx, mut rx) = serial.split();

    // Enable LIN mode and break detection interrupt in hardware registers
    unsafe {
        let usart = &*pac::USART2::ptr();
        // Enable LIN mode
        usart.cr2.modify(|_, w| w.linen().set_bit());
        // Enable LIN break detection interrupt (LBDIE)
        usart.cr2.modify(|_, w| w.lbdie().set_bit());
    }

    // Unmask USART2 interrupt in NVIC
    unsafe {
        cortex_m::peripheral::NVIC::unmask(pac::Interrupt::USART2);
    }

    loop {
        if BREAK_DETECTED.swap(false, Ordering::SeqCst) {
            // Handle break: e.g., reset LIN state machine, log event
            // Note: In no_std, use defmt or semihosting for debug output
        }

        // Normal UART receive
        if let Ok(byte) = rx.read() {
            // Process received byte
            let _ = byte;
        }
    }
}

// USART2 interrupt handler
#[cortex_m_rt::interrupt]
fn USART2() {
    unsafe {
        let usart = &*pac::USART2::ptr();
        let sr = usart.sr.read();

        // LIN break detected
        if sr.lbd().bit_is_set() {
            BREAK_DETECTED.store(true, Ordering::SeqCst);
            // Clear the LBD flag
            usart.sr.modify(|_, w| w.lbd().clear_bit());
        }

        // Framing error — also triggered during break
        if sr.fe().bit_is_set() {
            // Must read DR to clear FE
            let _ = usart.dr.read().dr().bits();
        }

        // Normal receive
        if sr.rxne().bit_is_set() {
            let _data = usart.dr.read().dr().bits() as u8;
            // Queue data for processing...
        }
    }
}
```

---

## Sending a Break Signal

Sending a break deliberately is as important as receiving one. Here are the platform methods:

| Platform       | Method                                      |
|----------------|---------------------------------------------|
| Linux          | `tcsendbreak(fd, 0)` — sends ~0.25 s break  |
| Linux (manual) | `ioctl(fd, TIOCSBRK)` / `ioctl(fd, TIOCCBRK)` |
| Windows        | `SetCommBreak()` / `ClearCommBreak()`        |
| Rust (serialport) | `port.set_break()` / `port.clear_break()` |
| STM32          | Set `USART_CR1_SBK` bit                     |
| AVR            | Set `TXB80`/`TXEN` with framing violation   |

**Example — Linux manual break (C):**

```c
#include <sys/ioctl.h>
#include <unistd.h>

void send_manual_break(int fd, unsigned int duration_us) {
    ioctl(fd, TIOCSBRK);     // Assert break: pull TX LOW
    usleep(duration_us);     // Hold for desired duration
    ioctl(fd, TIOCCBRK);     // Release break: return TX HIGH
}
```

---

## Common Use Cases

### 1. LIN Bus (Local Interconnect Network)

LIN is a single-wire automotive bus that uses a mandatory break field at the start of every frame:

```
LIN Frame:
 [BREAK: 13+ bits LOW] [SYNC: 0x55] [PID] [DATA 0..7] [CHECKSUM]
```

The master sends the break; all slaves synchronize their baud rate to the SYNC byte that follows.

### 2. DMX512 (Stage Lighting / Theatrical Control)

DMX512 uses a break to signal the start of a new 512-channel packet:

```
DMX Packet:
 [BREAK: ≥92 µs] [MAB: ≥12 µs HIGH] [START CODE: 0x00] [CH1..CH512]
```

Every transmit cycle begins with a break + mark-after-break (MAB) sequence.

### 3. Serial Console SysRq (Linux Kernel)

Linux interprets a long break on a serial console as a **SysRq** trigger, enabling emergency kernel commands (sync, unmount, reboot) when the system is otherwise unresponsive.

### 4. RS-232 Attention Signal

Many RS-232 protocols use a break to interrupt the remote end and request attention — similar in spirit to a TCP `RST` packet. Older dial-up modems used breaks to switch from data mode to command mode.

### 5. Baud Rate Auto-Detection

Some protocols send a break followed by a known byte (like `0x55` = alternating bits) and measure the timing to auto-detect the baud rate — the receiver counts high/low transitions to calculate bit period.

---

## Error Handling and Edge Cases

### Distinguishing Break from Framing Error

A framing error (`FE`) occurs whenever a stop bit is received as LOW instead of HIGH. A break is a special case of a framing error lasting longer than one frame. Key distinctions:

| Condition       | Data Byte | FE bit | BI/LBD bit | Duration          |
|-----------------|-----------|--------|------------|-------------------|
| Framing Error   | Any       | Set    | Clear      | Single stop bit   |
| Break Condition | 0x00      | Set    | Set        | > 1 full frame    |

Always check **both** the BI flag and the received data byte: a break arrives as `0x00` with both FE and BI set.

### Noise and False Breaks

On noisy lines, transient LOW pulses can mimic short break conditions. Mitigation strategies:

- Require the break to exceed a **minimum duration threshold** (e.g., 2× character frame)
- Use hardware noise filters (`ONEBIT` sampling on STM32 reduces susceptibility)
- Verify the break is followed by the expected protocol byte (e.g., `0x55` SYNC in LIN)
- Use RS-485 differential signaling to improve noise immunity

### Interrupt Latency in Embedded Systems

When relying on interrupts for break detection, be aware that:

- **High interrupt latency** can cause the LBD flag to be processed late — the next SYNC byte may arrive before the ISR runs
- Use a **dedicated high-priority interrupt** for break detection in time-sensitive protocols
- On bare-metal systems, consider using DMA for data reception so the CPU is only interrupted for break events

### Null Bytes and PARMRK Confusion

In PARMRK mode on Linux, the byte sequence `0xFF 0x00 0x00` signals a break. However, a legitimate `0xFF` data byte is escaped as `0xFF 0xFF`. Parsers must handle both cases:

```c
// PARMRK byte interpretation:
// 0xFF 0xFF       → literal data byte 0xFF
// 0xFF 0x00 0x00  → break condition
// 0xFF 0x00 0xNN  → framing/parity error on byte NN
```

---

## Summary

UART break detection is the mechanism by which a receiver identifies an extended LOW condition on the serial line — one that spans more than a complete character frame. This capability is fundamental to many serial protocols and system-level functions.

**Key takeaways:**

- A **break condition** is a continuous LOW state lasting longer than one UART character frame, distinguishable from normal data by its duration and the coincident framing error flag.

- **Hardware UART peripherals** (16550, STM32 USART, AVR ATmega) report breaks via dedicated status bits (`BI`, `LBD`, `FE`) and can generate interrupts — this is the most reliable detection method.

- On **Linux**, configure `termios` with `PARMRK` and without `IGNBRK`/`BRKINT` to receive breaks as the three-byte sentinel sequence `0xFF 0x00 0x00` in the data stream.

- On **Windows**, use `WaitCommEvent` with the `EV_BREAK` mask, then confirm via `ClearCommError` / `CE_BREAK`.

- In **Rust**, the `serialport` crate provides `set_break()` and `clear_break()` for sending breaks cross-platform; detection uses the same PARMRK technique on Linux, or can be handled via `nix` for direct `termios` access.

- On **bare-metal embedded systems**, enable the dedicated LIN break detection interrupt (or poll the framing error + zero-byte condition on AVR) and handle the event in the ISR with minimal latency.

- **Sending a break** is equally important — use `tcsendbreak()`, `TIOCSBRK/TIOCCBRK`, `SetCommBreak/ClearCommBreak`, or the HAL-specific `SBK` bit depending on your platform.

- **Protocol-specific requirements** vary significantly: LIN requires breaks of ≥13 bit times, DMX512 requires ≥92 µs, and Linux SysRq interprets long breaks on console ports.

Correct break handling, combined with robust noise filtering and careful interrupt priority configuration, ensures reliable operation across a wide range of serial communication protocols.

---

*Document covers: UART Break Detection — Platform implementations in C/C++ (Linux termios, Windows Win32, STM32, AVR) and Rust (serialport crate, nix, embedded-hal).*