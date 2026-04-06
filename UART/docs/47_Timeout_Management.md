# 47. UART Timeout Management

**Timeout Categories** — five distinct layers are defined: byte-level (inter-character), response (round-trip), frame completion, session/inactivity, and heartbeat/keep-alive. Each has a different purpose and typical value range.

**Strategies** — fixed, adaptive (exponential backoff), sliding window, dead-reckoning (baud-rate-aware), and retry/backoff policy tables.

**C/C++ Examples:**
- `termios` VMIN/VTIME hardware timeout configuration
- Software response timeout using `select()` with millisecond precision
- `uart_read_exact()` enforcing separate response and inter-character timeouts
- `DeviceWatchdog` C++ class with background thread and `feed()` pattern
- Request-retry loop with exponential backoff
- Bare-metal embedded `uart_readline()` for microcontrollers with no OS

**Rust Examples:**
- `read_exact_with_timeout()` using the `serialport` crate
- `RetryPolicy` struct with exponential backoff
- Async variant using `tokio::time::timeout` and `tokio-serial`
- `UartWatchdog` with `Arc<Mutex<WatchdogState>>` and a state machine (Alive → Suspect → Dead → Recovering)
- Variable-length frame reader with per-byte inter-character enforcement

**Advanced Patterns** — deadline propagation (shrinking budget across sub-operations) and an empirical calibration helper (mean + 3σ recommendation).

**Summary** — a comparison table of C vs. Rust approaches for each concern.

## Handling Communication Timeouts and Dead Device Detection

---

## Table of Contents

1. [Introduction](#introduction)
2. [Timeout Categories in UART Communication](#timeout-categories)
3. [Timeout Strategies and Algorithms](#timeout-strategies)
4. [C/C++ Implementation](#c-cpp-implementation)
5. [Rust Implementation](#rust-implementation)
6. [Dead Device Detection](#dead-device-detection)
7. [Advanced Patterns](#advanced-patterns)
8. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

Timeout management is one of the most critical aspects of robust UART communication. Without
proper timeout handling, a program can hang indefinitely waiting for data that will never arrive —
caused by a crashed device, a disconnected cable, electrical noise corrupting a packet, or a
firmware bug that silences a remote device.

UART is inherently asynchronous and has no built-in handshake for "no data to send" conditions.
Unlike TCP/IP, there is no protocol-level keep-alive. This means every layer of the UART
software stack must be designed with the assumption that silence can mean either "I am thinking"
or "I am dead."

Effective timeout management requires answers to three questions:

- **How long do we wait** before concluding that a response is not coming?
- **How do we recover** from a timeout condition without corrupting subsequent communication?
- **How do we distinguish** a slow-but-alive device from a dead one?

---

## 2. Timeout Categories in UART Communication <a name="timeout-categories"></a>

### 2.1 Byte-Level (Inter-Character) Timeout

The time allowed between consecutive bytes within a single frame or packet. If a packet's
first byte arrives but subsequent bytes do not arrive within this window, the packet is
considered incomplete and discarded.

- **Typical range:** 1.5 to 3.5 character times (used in Modbus RTU), or a fixed millisecond
  window.
- **Use case:** Detecting truncated frames caused by line noise or a sender that crashed
  mid-transmission.

### 2.2 Response Timeout (Round-Trip Timeout)

The time allowed between sending a request and receiving the first byte of a response. This
is the most commonly managed timeout.

- **Typical range:** 10 ms to several seconds depending on the device and baud rate.
- **Use case:** Request-response protocols (AT commands, Modbus, custom command protocols).

### 2.3 Frame / Message Completion Timeout

The maximum time allowed to receive a complete, valid message once the first byte has arrived.
Combines inter-character and total-frame concerns.

- **Use case:** Variable-length protocols where the length field appears mid-packet.

### 2.4 Session / Inactivity Timeout

The maximum time of total silence allowed in an ongoing session before the connection is
considered dead.

- **Typical range:** Seconds to minutes.
- **Use case:** Long-running connections to modems, GPS receivers, and sensors that send
  periodic data.

### 2.5 Heartbeat / Keep-Alive Timeout

A proactive timeout: if no data has been exchanged for a defined period, a "ping" message
is sent. If no "pong" is received within the response timeout, the device is declared dead.

---

## 3. Timeout Strategies and Algorithms <a name="timeout-strategies"></a>

### 3.1 Fixed Timeout

The simplest strategy. A fixed deadline is set from the moment a request is sent or the
first byte is received.

**Pros:** Simple. Predictable.  
**Cons:** Must be tuned per device. Too short causes false positives; too long wastes time.

### 3.2 Adaptive Timeout (Exponential Backoff)

The timeout doubles (or scales by a factor) with each retry. Common in network stacks and
useful for devices that may be temporarily overloaded.

```
timeout_n = min(base_timeout * 2^n, max_timeout)
```

### 3.3 Sliding Window Timeout

A timeout window that resets each time a byte is received. Effective for streaming protocols
where data trickles in.

### 3.4 Dead-Reckoning Timeout

Calculate the expected transmission time from the baud rate and expected response length,
then add a fixed guard margin. This produces a tight, protocol-aware timeout.

```
expected_ms = (response_bytes * 10 * 1000) / baud_rate
timeout_ms  = expected_ms + guard_margin_ms
```

### 3.5 Retry and Backoff Policy

Most robust implementations combine timeouts with a retry policy:

| Attempt | Timeout | Action on Failure |
|---------|---------|-------------------|
| 1       | 100 ms  | Retry             |
| 2       | 200 ms  | Retry             |
| 3       | 400 ms  | Log error         |
| 4       | 800 ms  | Mark device dead  |

---

## 4. C/C++ Implementation <a name="c-cpp-implementation"></a>

### 4.1 POSIX: Configuring Hardware Timeouts with `termios`

On Linux/POSIX systems, the `termios` VTIME field provides kernel-level inter-character
timeout support, reducing polling overhead.

```c
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

/**
 * Open and configure a serial port with termios hardware timeout.
 *
 * VMIN=0, VTIME=N: read() returns when N*100ms elapses with no data,
 *                  or immediately if data arrives.
 * VMIN=1, VTIME=N: read() blocks until at least 1 byte arrives, then
 *                  the inter-character timer starts.
 *
 * @param device  e.g. "/dev/ttyUSB0"
 * @param baud    e.g. B115200
 * @param vtime   timeout in units of 100ms (1 = 100ms, 10 = 1000ms)
 * @return file descriptor or -1 on error
 */
int uart_open(const char *device, speed_t baud, uint8_t vtime) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        perror("uart_open: open");
        return -1;
    }

    struct termios tty;
    memset(&tty, 0, sizeof tty);

    if (tcgetattr(fd, &tty) != 0) {
        perror("uart_open: tcgetattr");
        close(fd);
        return -1;
    }

    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    /* 8N1 */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | PARODD | CSTOPB | CRTSCTS);
    tty.c_cflag |= CLOCAL | CREAD;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY | IGNBRK | BRKINT |
                     PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;

    /* Non-canonical mode with timeout */
    tty.c_cc[VMIN]  = 0;     /* return immediately if no data */
    tty.c_cc[VTIME] = vtime; /* inter-character timeout in 100ms units */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}
```

### 4.2 Software Response Timeout with `select()`

`select()` provides millisecond-precision waiting without spinning the CPU.

```c
#include <sys/select.h>
#include <sys/time.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef enum {
    UART_OK      = 0,
    UART_TIMEOUT = -1,
    UART_ERROR   = -2,
} uart_status_t;

/**
 * Wait for data to be available on fd, with a millisecond timeout.
 *
 * @param fd         Serial file descriptor
 * @param timeout_ms Maximum wait time in milliseconds
 * @return UART_OK if data available, UART_TIMEOUT, or UART_ERROR
 */
uart_status_t uart_wait_readable(int fd, uint32_t timeout_ms) {
    fd_set read_fds;
    struct timeval tv;

    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(fd + 1, &read_fds, NULL, NULL, &tv);
    if (ret < 0) {
        perror("select");
        return UART_ERROR;
    }
    if (ret == 0) {
        return UART_TIMEOUT;
    }
    return UART_OK;
}

/**
 * Read exactly `len` bytes within `timeout_ms`, enforcing both
 * a response timeout (first byte) and an inter-character timeout.
 *
 * @param fd              Serial file descriptor
 * @param buf             Output buffer
 * @param len             Number of bytes to read
 * @param response_ms     Timeout for the first byte (ms)
 * @param interchar_ms    Timeout between subsequent bytes (ms)
 * @param bytes_read      Output: actual bytes received
 * @return UART_OK, UART_TIMEOUT, or UART_ERROR
 */
uart_status_t uart_read_exact(int fd, uint8_t *buf, size_t len,
                               uint32_t response_ms, uint32_t interchar_ms,
                               size_t *bytes_read)
{
    *bytes_read = 0;

    /* Wait for the first byte with the (longer) response timeout */
    uart_status_t st = uart_wait_readable(fd, response_ms);
    if (st != UART_OK) return st;

    while (*bytes_read < len) {
        ssize_t n = read(fd, buf + *bytes_read, len - *bytes_read);
        if (n < 0) { perror("read"); return UART_ERROR; }
        if (n == 0) break;
        *bytes_read += (size_t)n;

        if (*bytes_read < len) {
            /* Use the shorter inter-character timeout for subsequent bytes */
            st = uart_wait_readable(fd, interchar_ms);
            if (st != UART_OK) {
                fprintf(stderr, "Inter-character timeout after %zu bytes\n",
                        *bytes_read);
                return UART_TIMEOUT;
            }
        }
    }

    return (*bytes_read == len) ? UART_OK : UART_TIMEOUT;
}
```

### 4.3 Dead Device Detection with Heartbeat (C++)

```cpp
#include <chrono>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <iostream>

using Clock     = std::chrono::steady_clock;
using TimePoint = Clock::time_point;
using Ms        = std::chrono::milliseconds;

/**
 * DeviceWatchdog
 *
 * Monitors a UART device by sending periodic heartbeats and tracking
 * the last successful response. If no response is received within
 * `dead_threshold`, the on_dead callback fires.
 *
 * Thread-safe. Uses an internal worker thread.
 */
class DeviceWatchdog {
public:
    using HeartbeatFn = std::function<bool()>; // returns true if device responded
    using DeadFn      = std::function<void()>;

    DeviceWatchdog(HeartbeatFn heartbeat,
                   DeadFn      on_dead,
                   Ms          interval       = Ms(1000),
                   Ms          dead_threshold = Ms(5000))
        : heartbeat_(std::move(heartbeat))
        , on_dead_(std::move(on_dead))
        , interval_(interval)
        , dead_threshold_(dead_threshold)
        , running_(false)
        , device_dead_(false)
    {
        last_response_ = Clock::now();
    }

    ~DeviceWatchdog() { stop(); }

    /** Record a successful response from the device (call from any thread). */
    void feed() {
        std::lock_guard<std::mutex> lk(mutex_);
        last_response_ = Clock::now();
        if (device_dead_) {
            device_dead_ = false;
            std::cout << "[Watchdog] Device recovered.\n";
        }
    }

    bool is_dead() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return device_dead_;
    }

    void start() {
        running_ = true;
        worker_  = std::thread(&DeviceWatchdog::run, this);
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

private:
    void run() {
        while (running_) {
            std::this_thread::sleep_for(interval_);

            bool responded = heartbeat_();
            if (responded) {
                feed();
                continue;
            }

            /* Check elapsed time since last confirmed response */
            Ms elapsed;
            {
                std::lock_guard<std::mutex> lk(mutex_);
                elapsed = std::chrono::duration_cast<Ms>(
                    Clock::now() - last_response_);
            }

            if (elapsed >= dead_threshold_) {
                bool was_dead;
                {
                    std::lock_guard<std::mutex> lk(mutex_);
                    was_dead    = device_dead_;
                    device_dead_ = true;
                }
                if (!was_dead) {
                    std::cerr << "[Watchdog] Device declared dead after "
                              << elapsed.count() << " ms of silence.\n";
                    on_dead_();
                }
            }
        }
    }

    HeartbeatFn        heartbeat_;
    DeadFn             on_dead_;
    Ms                 interval_;
    Ms                 dead_threshold_;
    std::atomic<bool>  running_;
    mutable std::mutex mutex_;
    TimePoint          last_response_;
    bool               device_dead_;
    std::thread        worker_;
};
```

### 4.4 Request-Response with Retry and Exponential Backoff (C++)

```cpp
#include <cstdint>
#include <vector>
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>

struct RetryPolicy {
    uint32_t base_timeout_ms = 100;
    uint32_t max_timeout_ms  = 2000;
    uint8_t  max_attempts    = 4;
    float    backoff_factor  = 2.0f;
};

/**
 * Send a request over UART and collect a response, retrying on timeout.
 *
 * On each attempt, the timeout doubles (capped at max_timeout_ms).
 * Returns the response bytes, or throws std::runtime_error on failure.
 */
std::vector<uint8_t> uart_request_with_retry(
    int fd,
    const std::vector<uint8_t>& request,
    size_t expected_response_len,
    const RetryPolicy& policy = {})
{
    uint32_t timeout_ms = policy.base_timeout_ms;

    for (uint8_t attempt = 1; attempt <= policy.max_attempts; ++attempt) {
        /* Flush stale data before each attempt */
        tcflush(fd, TCIFLUSH);

        /* Send request */
        ssize_t written = write(fd, request.data(), request.size());
        if (written < 0 || static_cast<size_t>(written) != request.size()) {
            throw std::runtime_error("uart_request_with_retry: write failed");
        }

        /* Read response */
        std::vector<uint8_t> response(expected_response_len);
        size_t bytes_read = 0;
        uart_status_t status = uart_read_exact(
            fd, response.data(), expected_response_len,
            timeout_ms,          /* response timeout */
            timeout_ms / 4,      /* inter-character timeout */
            &bytes_read);

        if (status == UART_OK) {
            std::cout << "[UART] Success on attempt " << (int)attempt << "\n";
            return response;
        }

        std::cerr << "[UART] Attempt " << (int)attempt
                  << " timed out (" << timeout_ms << " ms). ";

        /* Exponential backoff */
        timeout_ms = static_cast<uint32_t>(
            std::min((float)timeout_ms * policy.backoff_factor,
                     (float)policy.max_timeout_ms));

        if (attempt < policy.max_attempts) {
            std::cerr << "Retrying with " << timeout_ms << " ms timeout.\n";
        } else {
            std::cerr << "All attempts exhausted.\n";
        }
    }

    throw std::runtime_error("uart_request_with_retry: device unresponsive");
}
```

### 4.5 Embedded Bare-Metal Timeout (No OS)

On microcontrollers without an OS, software timeout loops using hardware timers are standard practice.

```c
#include <stdint.h>
#include <stdbool.h>

/* Platform-specific: returns milliseconds since boot */
extern uint32_t hal_get_tick_ms(void);

/* Platform-specific: returns one byte from UART RX buffer, or -1 if empty */
extern int  hal_uart_getchar(void);
extern void hal_uart_putchar(uint8_t c);
extern void hal_uart_flush_rx(void);

typedef enum {
    UART_RES_OK      = 0,
    UART_RES_TIMEOUT = 1,
    UART_RES_OVERFLOW= 2,
} uart_result_t;

/**
 * Receive bytes into buf until `terminator` is found, `max_len-1` bytes
 * are received, or `timeout_ms` elapses with no new data.
 *
 * The returned buffer is always null-terminated.
 * `interchar_ms` resets on every received byte.
 */
uart_result_t uart_readline(uint8_t *buf, size_t max_len,
                             uint8_t terminator,
                             uint32_t response_ms,
                             uint32_t interchar_ms)
{
    size_t   idx         = 0;
    uint32_t deadline    = hal_get_tick_ms() + response_ms;
    bool     first_byte  = true;

    hal_uart_flush_rx();

    while (true) {
        int ch = hal_uart_getchar();

        if (ch < 0) {
            /* No byte available — check timeout */
            uint32_t now = hal_get_tick_ms();
            if (now >= deadline) {
                buf[idx] = '\0';
                return (idx == 0) ? UART_RES_TIMEOUT : UART_RES_TIMEOUT;
            }
            continue; /* spin-wait (or replace with __WFI() on ARM) */
        }

        buf[idx++] = (uint8_t)ch;

        /* After first byte, switch to inter-character timeout */
        if (first_byte) {
            first_byte = false;
            deadline   = hal_get_tick_ms() + interchar_ms;
        } else {
            deadline = hal_get_tick_ms() + interchar_ms; /* reset on each byte */
        }

        if ((uint8_t)ch == terminator) {
            buf[idx] = '\0';
            return UART_RES_OK;
        }

        if (idx >= max_len - 1) {
            buf[idx] = '\0';
            return UART_RES_OVERFLOW;
        }
    }
}

/**
 * Example usage: send AT command and await "OK\r\n"
 */
bool send_at_command(const char *cmd) {
    /* Transmit command */
    for (const char *p = cmd; *p; p++) hal_uart_putchar((uint8_t)*p);
    hal_uart_putchar('\r');
    hal_uart_putchar('\n');

    uint8_t response[64];
    uart_result_t result = uart_readline(
        response, sizeof(response),
        '\n',   /* line terminator */
        500,    /* response timeout: 500 ms */
        50      /* inter-character timeout: 50 ms */
    );

    if (result == UART_RES_TIMEOUT) {
        /* Log or trigger fault handler */
        return false;
    }

    /* Simple check: response contains "OK" */
    for (size_t i = 0; response[i] && i < sizeof(response) - 1; i++) {
        if (response[i] == 'O' && response[i+1] == 'K') return true;
    }
    return false;
}
```

---

## 5. Rust Implementation <a name="rust-implementation"></a>

### 5.1 Core Timeout Read with `serialport` crate

```toml
# Cargo.toml
[dependencies]
serialport = "4"
thiserror  = "1"
```

```rust
use serialport::SerialPort;
use std::io::{self, Read};
use std::time::{Duration, Instant};
use thiserror::Error;

#[derive(Debug, Error)]
pub enum UartError {
    #[error("Response timeout: no data within {timeout_ms}ms")]
    ResponseTimeout { timeout_ms: u64 },

    #[error("Inter-character timeout after {bytes_received} bytes")]
    InterCharTimeout { bytes_received: usize },

    #[error("Incomplete frame: got {got}, expected {expected} bytes")]
    IncompleteFrame { got: usize, expected: usize },

    #[error("I/O error: {0}")]
    Io(#[from] io::Error),

    #[error("Serial port error: {0}")]
    SerialPort(#[from] serialport::Error),
}

/// Read exactly `expected` bytes with separate response and inter-character timeouts.
///
/// The `port` should be opened with a short `timeout` (e.g. 10ms) for polling.
/// This function implements the actual timeout logic in software.
pub fn read_exact_with_timeout(
    port: &mut dyn SerialPort,
    expected: usize,
    response_timeout: Duration,
    interchar_timeout: Duration,
) -> Result<Vec<u8>, UartError> {
    let mut buf = vec![0u8; expected];
    let mut received = 0usize;
    let mut deadline = Instant::now() + response_timeout;

    while received < expected {
        let mut single = [0u8; 1];
        match port.read(&mut single) {
            Ok(1) => {
                buf[received] = single[0];
                received += 1;
                // After the first byte, switch to the inter-character deadline
                deadline = Instant::now() + interchar_timeout;
            }
            Ok(0) | Err(_) => {
                // No byte available
                if Instant::now() >= deadline {
                    return if received == 0 {
                        Err(UartError::ResponseTimeout {
                            timeout_ms: response_timeout.as_millis() as u64,
                        })
                    } else {
                        Err(UartError::InterCharTimeout {
                            bytes_received: received,
                        })
                    };
                }
                std::thread::sleep(Duration::from_millis(1));
            }
            Ok(n) => unreachable!("read returned {n} for 1-byte buffer"),
        }
    }

    buf.truncate(received);
    Ok(buf)
}
```

### 5.2 Request-Response with Retry and Exponential Backoff

```rust
use std::time::Duration;

#[derive(Debug, Clone)]
pub struct RetryPolicy {
    pub base_timeout:   Duration,
    pub max_timeout:    Duration,
    pub max_attempts:   u32,
    pub backoff_factor: f64,
}

impl Default for RetryPolicy {
    fn default() -> Self {
        Self {
            base_timeout:   Duration::from_millis(100),
            max_timeout:    Duration::from_millis(2000),
            max_attempts:   4,
            backoff_factor: 2.0,
        }
    }
}

/// Send `request` bytes and receive `expected_len` response bytes,
/// retrying with exponential backoff on timeout.
pub fn request_with_retry(
    port:         &mut dyn SerialPort,
    request:      &[u8],
    expected_len: usize,
    policy:       &RetryPolicy,
) -> Result<Vec<u8>, UartError> {
    let mut timeout = policy.base_timeout;

    for attempt in 1..=policy.max_attempts {
        // Flush RX buffer before each attempt
        port.clear(serialport::ClearBuffer::Input)?;

        // Send request
        use std::io::Write;
        port.write_all(request).map_err(UartError::Io)?;
        port.flush().map_err(UartError::Io)?;

        // Read response
        match read_exact_with_timeout(port, expected_len, timeout, timeout / 4) {
            Ok(response) => {
                log::debug!("UART success on attempt {attempt}");
                return Ok(response);
            }
            Err(UartError::ResponseTimeout { .. })
            | Err(UartError::InterCharTimeout { .. }) => {
                log::warn!(
                    "UART attempt {attempt}/{}: timeout ({timeout:?})",
                    policy.max_attempts
                );
            }
            Err(e) => return Err(e),
        }

        // Exponential backoff, capped at max_timeout
        let next_ms = (timeout.as_millis() as f64 * policy.backoff_factor) as u64;
        timeout = Duration::from_millis(next_ms).min(policy.max_timeout);
    }

    Err(UartError::ResponseTimeout {
        timeout_ms: policy.max_timeout.as_millis() as u64,
    })
}
```

### 5.3 Async Timeout with Tokio

```toml
# Cargo.toml (async variant)
[dependencies]
tokio        = { version = "1", features = ["full"] }
serialport   = "4"
tokio-serial = "5"
thiserror    = "1"
```

```rust
use tokio::time::{timeout, Duration};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_serial::SerialPortBuilderExt;

/// Async UART read with deadline enforcement using tokio::time::timeout.
/// This is ideal for async runtimes where blocking calls are unacceptable.
pub async fn async_uart_read(
    port:       &mut tokio_serial::SerialStream,
    buf:        &mut [u8],
    deadline:   Duration,
) -> Result<usize, UartError> {
    match timeout(deadline, port.read(buf)).await {
        Ok(Ok(n))  => Ok(n),
        Ok(Err(e)) => Err(UartError::Io(e)),
        Err(_elapsed) => Err(UartError::ResponseTimeout {
            timeout_ms: deadline.as_millis() as u64,
        }),
    }
}

/// Async request-response with a single timeout budget.
pub async fn async_request_response(
    port:        &mut tokio_serial::SerialStream,
    request:     &[u8],
    expected:    usize,
    total_budget: Duration,
) -> Result<Vec<u8>, UartError> {
    // Write request
    timeout(Duration::from_millis(100), port.write_all(request))
        .await
        .map_err(|_| UartError::ResponseTimeout { timeout_ms: 100 })?
        .map_err(UartError::Io)?;

    // Read response within remaining budget
    let mut buf = vec![0u8; expected];
    let n = async_uart_read(port, &mut buf, total_budget).await?;

    if n != expected {
        return Err(UartError::IncompleteFrame {
            got:      n,
            expected,
        });
    }

    Ok(buf)
}
```

### 5.4 Dead Device Detection with Watchdog in Rust

```rust
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;

#[derive(Debug, Clone, PartialEq)]
pub enum DeviceState {
    Alive,
    Dead,
    Recovering,
}

#[derive(Debug)]
pub struct WatchdogState {
    pub last_response: Instant,
    pub state:         DeviceState,
    pub miss_count:    u32,
}

/// Watchdog that monitors a UART device via a periodic heartbeat function.
///
/// `heartbeat_fn` should send a probe and return `true` if a valid
/// response was received within its own timeout.
pub struct UartWatchdog {
    state:        Arc<Mutex<WatchdogState>>,
    interval:     Duration,
    dead_after:   Duration,
}

impl UartWatchdog {
    pub fn new(interval: Duration, dead_after: Duration) -> Self {
        Self {
            state: Arc::new(Mutex::new(WatchdogState {
                last_response: Instant::now(),
                state:         DeviceState::Alive,
                miss_count:    0,
            })),
            interval,
            dead_after,
        }
    }

    /// Call this whenever the application receives a valid response.
    pub fn feed(&self) {
        let mut s = self.state.lock().unwrap();
        s.last_response = Instant::now();
        s.miss_count    = 0;
        if s.state == DeviceState::Dead {
            s.state = DeviceState::Recovering;
            eprintln!("[Watchdog] Device is recovering.");
        } else {
            s.state = DeviceState::Alive;
        }
    }

    /// Returns true if the device is currently considered dead.
    pub fn is_dead(&self) -> bool {
        self.state.lock().unwrap().state == DeviceState::Dead
    }

    /// Spawn the watchdog background thread.
    ///
    /// `heartbeat_fn`: closure that sends a probe; returns `true` on response.
    /// `on_dead`: closure called once when the device transitions to Dead.
    pub fn spawn<H, D>(self, heartbeat_fn: H, on_dead: D)
    where
        H: Fn() -> bool + Send + 'static,
        D: Fn()          + Send + 'static,
    {
        let state_ref  = Arc::clone(&self.state);
        let interval   = self.interval;
        let dead_after = self.dead_after;

        thread::spawn(move || {
            loop {
                thread::sleep(interval);

                let responded = heartbeat_fn();

                let mut s = state_ref.lock().unwrap();

                if responded {
                    s.last_response = Instant::now();
                    s.miss_count    = 0;
                    if s.state != DeviceState::Alive {
                        s.state = DeviceState::Alive;
                        eprintln!("[Watchdog] Device confirmed alive.");
                    }
                } else {
                    s.miss_count += 1;
                    let elapsed = s.last_response.elapsed();

                    if elapsed >= dead_after && s.state != DeviceState::Dead {
                        s.state = DeviceState::Dead;
                        eprintln!(
                            "[Watchdog] Device declared DEAD after {}ms ({} misses).",
                            elapsed.as_millis(),
                            s.miss_count
                        );
                        drop(s);      // release lock before callback
                        on_dead();
                        continue;
                    }
                }
            }
        });
    }
}
```

### 5.5 Frame Completion Timeout with Variable-Length Packets

```rust
use std::time::{Duration, Instant};

/// A framing context for variable-length UART packets.
///
/// Protocol: [0xAA] [LENGTH:u8] [PAYLOAD: LENGTH bytes] [CRC:u8]
pub struct FrameReader {
    interchar_timeout: Duration,
}

#[derive(Debug)]
pub struct Frame {
    pub payload: Vec<u8>,
}

impl FrameReader {
    pub fn new(interchar_timeout: Duration) -> Self {
        Self { interchar_timeout }
    }

    /// Read one complete frame from the port.
    ///
    /// The inter-character timeout is enforced between every byte.
    /// An outer response timeout should be applied by the caller
    /// (e.g. wrapping this call in `tokio::time::timeout`).
    pub fn read_frame(
        &self,
        port: &mut dyn SerialPort,
    ) -> Result<Frame, UartError> {
        // Read start-of-frame marker
        let sof = self.read_byte(port, self.interchar_timeout * 5)?; // generous first-byte wait
        if sof != 0xAA {
            return Err(UartError::Io(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("Bad SOF: 0x{sof:02X}"),
            )));
        }

        // Read length
        let length = self.read_byte(port, self.interchar_timeout)? as usize;
        if length == 0 || length > 255 {
            return Err(UartError::Io(io::Error::new(
                io::ErrorKind::InvalidData,
                "Invalid frame length",
            )));
        }

        // Read payload
        let mut payload = Vec::with_capacity(length);
        for i in 0..length {
            let b = self.read_byte(port, self.interchar_timeout)
                .map_err(|_| UartError::InterCharTimeout { bytes_received: i })?;
            payload.push(b);
        }

        // Read and verify CRC
        let crc_received = self.read_byte(port, self.interchar_timeout)?;
        let crc_expected = payload.iter().fold(0u8, |acc, &b| acc ^ b);
        if crc_received != crc_expected {
            return Err(UartError::Io(io::Error::new(
                io::ErrorKind::InvalidData,
                format!("CRC mismatch: 0x{crc_received:02X} != 0x{crc_expected:02X}"),
            )));
        }

        Ok(Frame { payload })
    }

    fn read_byte(
        &self,
        port: &mut dyn SerialPort,
        timeout: Duration,
    ) -> Result<u8, UartError> {
        let deadline = Instant::now() + timeout;
        let mut buf = [0u8; 1];
        loop {
            match port.read(&mut buf) {
                Ok(1) => return Ok(buf[0]),
                Ok(_) | Err(_) => {
                    if Instant::now() >= deadline {
                        return Err(UartError::ResponseTimeout {
                            timeout_ms: timeout.as_millis() as u64,
                        });
                    }
                    std::thread::sleep(Duration::from_millis(1));
                }
            }
        }
    }
}
```

---

## 6. Dead Device Detection <a name="dead-device-detection"></a>

### 6.1 Detection Strategies

Dead device detection goes beyond simple timeouts. A device may be dead in different ways:

| Symptom | Likely Cause | Detection Method |
|---------|-------------|-----------------|
| No response to request | Device crashed, disconnected | Response timeout + retry exhaustion |
| Partial response, then silence | Device crashed mid-transmission | Inter-character timeout |
| Garbled data, no valid frames | Line noise, wrong baud rate | CRC/checksum failure rate |
| Response latency increasing | Device overloaded or FIFO full | Sliding average latency monitoring |
| Silence exceeds keep-alive interval | Device entered low-power mode or crashed | Heartbeat timeout |

### 6.2 State Machine for Device Liveness

```
         ┌─────────────────────────────┐
         │                             │
         ▼                             │ valid response received
      [ALIVE] ──── timeout ──────► [SUSPECT]
         ▲                             │
         │                             │ timeout × N
         │ valid response              ▼
         └──────────────────────── [DEAD]
                                       │
                                       │ reconnect / reopen port
                                       ▼
                                  [RECOVERING]
```

### 6.3 Recovery Actions on Dead Device

When a device is declared dead, recovery should follow a defined procedure:

1. **Log the event** with timestamp and last known good time.
2. **Close and reopen** the serial port (clears OS buffers and re-negotiates line state).
3. **Assert DTR/RTS** reset line if available (can reset embedded targets).
4. **Send a reset or wake-up command** if the protocol supports it.
5. **Exponentially back off** reconnection attempts to avoid flooding logs.
6. **Alert the operator** or trigger a fault handler after N failed reconnections.

---

## 7. Advanced Patterns <a name="advanced-patterns"></a>

### 7.1 Deadline Propagation

In complex systems, a top-level operation (e.g. "read sensor data within 200 ms") should
propagate a shrinking deadline budget through each sub-operation, rather than each sub-operation
having its own independent timeout.

```rust
use std::time::{Duration, Instant};

pub struct Deadline(Instant);

impl Deadline {
    pub fn from_now(d: Duration) -> Self { Self(Instant::now() + d) }
    pub fn remaining(&self) -> Option<Duration> {
        self.0.checked_duration_since(Instant::now())
    }
    pub fn is_expired(&self) -> bool { Instant::now() >= self.0 }
}

pub fn composite_operation(
    port:     &mut dyn SerialPort,
    deadline: &Deadline,
) -> Result<Vec<u8>, UartError> {
    // Phase 1: wake up device
    let phase1_budget = deadline
        .remaining()
        .ok_or(UartError::ResponseTimeout { timeout_ms: 0 })?
        .min(Duration::from_millis(50));

    read_exact_with_timeout(port, 1, phase1_budget, phase1_budget)?;

    // Phase 2: read payload, using whatever budget remains
    let phase2_budget = deadline
        .remaining()
        .ok_or(UartError::ResponseTimeout { timeout_ms: 0 })?;

    read_exact_with_timeout(port, 64, phase2_budget, phase2_budget / 8)
}
```

### 7.2 Timeout Calibration Helper

Automatically measure actual device response time and recommend timeout values.

```c
#include <stdint.h>
#include <stdio.h>

#define CALIBRATION_SAMPLES 20

/**
 * Calibrate response timeout for a device.
 *
 * Sends `probe` N times and measures round-trip time.
 * Recommends timeout = mean + 3*stddev (99.7% coverage for normal distribution).
 */
void calibrate_timeout(int fd, const uint8_t *probe, size_t probe_len,
                        size_t response_len)
{
    uint32_t samples[CALIBRATION_SAMPLES];
    uint32_t sum = 0, min_t = UINT32_MAX, max_t = 0;
    uint32_t success = 0;

    for (int i = 0; i < CALIBRATION_SAMPLES; i++) {
        tcflush(fd, TCIFLUSH);
        write(fd, probe, probe_len);

        struct timespec t0, t1;
        clock_gettime(CLOCK_MONOTONIC, &t0);

        uint8_t buf[256];
        size_t n = 0;
        uart_read_exact(fd, buf, response_len, 2000, 100, &n);

        clock_gettime(CLOCK_MONOTONIC, &t1);

        if (n == response_len) {
            uint32_t ms = (uint32_t)(
                (t1.tv_sec - t0.tv_sec) * 1000 +
                (t1.tv_nsec - t0.tv_nsec) / 1000000);
            samples[success++] = ms;
            sum += ms;
            if (ms < min_t) min_t = ms;
            if (ms > max_t) max_t = ms;
        }
    }

    if (success == 0) {
        printf("Calibration FAILED: no responses received.\n");
        return;
    }

    uint32_t mean = sum / success;

    /* Variance and standard deviation */
    uint64_t var_sum = 0;
    for (uint32_t i = 0; i < success; i++) {
        int64_t diff = (int64_t)samples[i] - mean;
        var_sum += (uint64_t)(diff * diff);
    }
    uint32_t stddev = (uint32_t)sqrt((double)var_sum / success);

    printf("Calibration results (%u/%d responses):\n", success, CALIBRATION_SAMPLES);
    printf("  Min:    %u ms\n", min_t);
    printf("  Max:    %u ms\n", max_t);
    printf("  Mean:   %u ms\n", mean);
    printf("  StdDev: %u ms\n", stddev);
    printf("  Recommended timeout (mean + 3*sigma): %u ms\n", mean + 3 * stddev);
}
```

---

## 8. Summary <a name="summary"></a>

Timeout management is a non-trivial but essential discipline in UART programming. The key
principles are:

**Separate timeout layers.** Response timeout (waiting for the first byte), inter-character
timeout (detecting truncated frames), and session/heartbeat timeout (detecting dead devices)
serve different purposes and require different values.

**Never spin-wait without a deadline.** Every `read()` call — whether in bare-metal C or
async Rust — must be bounded. Unbounded reads are the source of hung processes and
unresponsive systems.

**Retry with backoff.** A single timeout is rarely enough. Devices can experience transient
hiccups. Exponential backoff avoids overwhelming a struggling device while still detecting
a truly dead one.

**Distinguish slow from dead.** Use a state machine (Alive → Suspect → Dead → Recovering)
rather than a binary alive/dead flag. This allows graceful handling of temporarily
unresponsive devices.

**Propagate deadlines, not timeouts.** In layered protocols, pass a shrinking deadline
budget through the call stack rather than assigning independent timeouts to each layer.
This prevents a sequence of individually-valid timeouts from blowing past a top-level SLA.

**Calibrate empirically.** Use actual device measurements (mean response time plus
3× standard deviation) to set timeouts, rather than guessing. Hard-coded "round number"
timeouts (e.g. exactly 1000 ms) are rarely optimal and often too conservative or too tight.

**Close and reopen the port on dead-device recovery.** The OS serial port state (line
discipline, FIFO, modem-control lines) can become inconsistent when a device disconnects.
Always reopen the port as part of the recovery sequence.

| Concern | C/POSIX Approach | Rust Approach |
|---------|-----------------|---------------|
| Response timeout | `select()` with `struct timeval` | `timeout()` from `tokio::time` or polling with `Instant` |
| Inter-character timeout | Nested `select()` calls | Deadline reset on each byte in loop |
| Hardware timeout | `termios` VMIN/VTIME | `serialport` crate `set_timeout()` |
| Retry policy | Manual loop with `timeout_ms *= factor` | Struct-driven `RetryPolicy` with iterator pattern |
| Dead device | Watchdog thread with `pthread_mutex_t` | `Arc<Mutex<WatchdogState>>` + `thread::spawn` |
| Async | `poll()` / `epoll()` | `tokio-serial` + `tokio::time::timeout` |

---

*Document: 47 — Timeout Management | UART Programming Series*