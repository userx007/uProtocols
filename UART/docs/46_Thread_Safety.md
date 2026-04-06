# 46. UART Thread Safety

**C/C++ Coverage:**
1. **Mutex-protected UART driver** — POSIX `pthread_mutex_t` wrapping TX and RX with a full header/source split
2. **Atomic ring buffer** — Lock-free SPSC queue using `stdatomic.h` for ISR/thread communication
3. **C++ RAII wrapper** — `std::mutex` + `std::lock_guard` with a transactional send-receive method
4. **FreeRTOS DMA pattern** — Binary semaphores signaled from ISR callbacks + mutex for multi-task protection
5. **Producer-consumer logger** — Condition variable queue with a single dedicated UART writer thread

**Rust Coverage:**
1. **Generic `UartSafe<T>` wrapper** — Mutex over any `Read+Write` type
2. **`Arc<Mutex<T>>` multi-thread pattern** — Idiomatic shared ownership with statistics tracking
3. **`no_std` bare-metal** — `cortex_m::interrupt::Mutex` + `critical-section` crate + `heapless` SPSC queue
4. **Channel-based logging** — `mpsc::sync_channel` for backpressure-aware multi-producer UART writes
5. **Atomic SPSC ring buffer** — Lock-free implementation using `core::sync::atomic` for ISR ↔ thread communication

The document also includes a comparison table of platforms vs. recommended primitives, RS-485 half-duplex special handling, and a patterns/anti-patterns section covering deadlock scenarios and non-atomic check-then-act bugs.

## Synchronization Primitives for Concurrent UART Access

---

## Table of Contents

1. [Introduction](#introduction)
2. [Why Thread Safety Matters for UART](#why-thread-safety-matters-for-uart)
3. [Core Synchronization Primitives](#core-synchronization-primitives)
4. [C/C++ Implementations](#cc-implementations)
   - [Mutex-Protected UART Driver](#mutex-protected-uart-driver)
   - [Ring Buffer with Spinlock](#ring-buffer-with-spinlock)
   - [POSIX Thread-Safe UART Wrapper](#posix-thread-safe-uart-wrapper)
   - [RTOS-Style Semaphore-Based Access](#rtos-style-semaphore-based-access)
   - [Producer-Consumer with Condition Variables](#producer-consumer-with-condition-variables)
5. [Rust Implementations](#rust-implementations)
   - [Mutex-Wrapped UART in std Environment](#mutex-wrapped-uart-in-std-environment)
   - [Arc and Mutex for Shared UART Access](#arc-and-mutex-for-shared-uart-access)
   - [no_std Embedded: Critical Sections](#no_std-embedded-critical-sections)
   - [Channel-Based UART Communication](#channel-based-uart-communication)
   - [Atomic Ring Buffer for ISR/Thread Communication](#atomic-ring-buffer-for-isrthread-communication)
6. [Common Patterns and Anti-Patterns](#common-patterns-and-anti-patterns)
7. [Platform-Specific Considerations](#platform-specific-considerations)
8. [Summary](#summary)

---

## Introduction

UART (Universal Asynchronous Receiver-Transmitter) is a serial communication protocol widely used in embedded systems, microcontrollers, and host-to-device communication. In modern systems—whether running a full OS, an RTOS, or even bare-metal with interrupts—multiple execution contexts (threads, tasks, interrupt service routines) frequently need to access the same UART peripheral simultaneously.

Without proper synchronization, concurrent UART access leads to:

- **Data corruption**: Interleaved writes from multiple threads produce garbled output
- **Lost data**: Simultaneous reads consume bytes meant for a specific consumer
- **Undefined behavior**: Hardware register conflicts on embedded targets
- **Race conditions**: Non-atomic sequences (check buffer → write) broken by preemption

Thread safety for UART requires carefully chosen synchronization primitives matched to the execution environment.

---

## Why Thread Safety Matters for UART

Consider two threads both calling a naive `uart_write()` simultaneously:

```
Thread A: "Hello, "      →  H e l l o ,   [preempted]
Thread B: "World!\n"     →  W o r l d ! \n
Thread A: (resumes)      →  W o r l d
```

The actual output on the wire becomes: `Hello, World!\nWorld` — a corrupted, interleaved mess. This is a classic race condition.

UART access has additional complications:

- **ISR interaction**: Receive data arrives via interrupt; the ISR and application thread both touch the RX buffer
- **Half-duplex buses**: RS-485 requires bus direction control; two threads trying to transmit simultaneously may leave the bus in an invalid state
- **Framing requirements**: A complete protocol frame must be sent atomically — preemption mid-frame breaks the protocol
- **DMA transfers**: DMA completion callbacks may race with thread-initiated transfers

---

## Core Synchronization Primitives

| Primitive | Use Case | Overhead | Blocks? |
|-----------|----------|----------|---------|
| **Mutex** | Exclusive access to shared resource | Low–Medium | Yes |
| **Semaphore** | Signaling between ISR and thread | Very Low | Yes (counting) |
| **Spinlock** | Short critical sections, bare metal | Very Low | Busy-waits |
| **Critical Section** (disable IRQ) | ISR-safe buffer access | Minimal | No (disables IRQs) |
| **Atomic Operations** | Lock-free index updates in ring buffers | Minimal | No |
| **Condition Variable** | Producer-consumer synchronization | Medium | Yes |
| **Message Queue / Channel** | Decoupled thread communication | Medium | Yes |

---

## C/C++ Implementations

### Mutex-Protected UART Driver

The simplest and most portable approach: wrap every UART operation in a mutex lock/unlock pair. This ensures only one thread accesses the UART at a time.

```c
// uart_safe.h
#ifndef UART_SAFE_H
#define UART_SAFE_H

#include <stdint.h>
#include <stddef.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct {
    int       fd;           // File descriptor (Linux) or peripheral handle
    pthread_mutex_t tx_mutex;
    pthread_mutex_t rx_mutex;
    bool      initialized;
} UartHandle;

int  uart_init(UartHandle *uart, const char *device, int baud);
int  uart_write_safe(UartHandle *uart, const uint8_t *data, size_t len);
int  uart_read_safe(UartHandle *uart, uint8_t *buf, size_t len, int timeout_ms);
void uart_deinit(UartHandle *uart);

#endif
```

```c
// uart_safe.c
#include "uart_safe.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <time.h>

int uart_init(UartHandle *uart, const char *device, int baud) {
    uart->fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (uart->fd < 0) {
        perror("uart_init: open");
        return -1;
    }

    // Configure termios (omitted for brevity — set baud, 8N1, raw mode)

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    // ERRORCHECK helps catch double-lock bugs during development
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK);

    pthread_mutex_init(&uart->tx_mutex, &attr);
    pthread_mutex_init(&uart->rx_mutex, &attr);
    pthread_mutexattr_destroy(&attr);

    uart->initialized = true;
    return 0;
}

// Thread-safe write: acquires TX mutex, writes full buffer, releases mutex.
// Returns bytes written or -1 on error.
int uart_write_safe(UartHandle *uart, const uint8_t *data, size_t len) {
    if (!uart->initialized) return -1;

    int ret = pthread_mutex_lock(&uart->tx_mutex);
    if (ret != 0) {
        errno = ret;
        return -1;
    }

    size_t total_written = 0;
    while (total_written < len) {
        ssize_t n = write(uart->fd, data + total_written, len - total_written);
        if (n < 0) {
            if (errno == EINTR) continue;   // Retry on signal interruption
            pthread_mutex_unlock(&uart->tx_mutex);
            return -1;
        }
        total_written += (size_t)n;
    }

    pthread_mutex_unlock(&uart->tx_mutex);
    return (int)total_written;
}

// Thread-safe read with timeout in milliseconds.
int uart_read_safe(UartHandle *uart, uint8_t *buf, size_t len, int timeout_ms) {
    if (!uart->initialized) return -1;

    pthread_mutex_lock(&uart->rx_mutex);

    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_nsec += (long)timeout_ms * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    size_t total_read = 0;
    while (total_read < len) {
        ssize_t n = read(uart->fd, buf + total_read, len - total_read);
        if (n > 0) {
            total_read += (size_t)n;
        } else if (n == 0 || (n < 0 && errno == EAGAIN)) {
            // Check deadline
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            if (now.tv_sec > deadline.tv_sec ||
                (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
                break; // Timeout
            }
            usleep(500); // Brief sleep before retry
        } else if (errno != EINTR) {
            pthread_mutex_unlock(&uart->rx_mutex);
            return -1;
        }
    }

    pthread_mutex_unlock(&uart->rx_mutex);
    return (int)total_read;
}

void uart_deinit(UartHandle *uart) {
    if (!uart->initialized) return;
    pthread_mutex_destroy(&uart->tx_mutex);
    pthread_mutex_destroy(&uart->rx_mutex);
    close(uart->fd);
    uart->initialized = false;
}
```

**Usage:**

```c
#include "uart_safe.h"
#include <pthread.h>
#include <stdio.h>

UartHandle g_uart;

void *thread_sensor(void *arg) {
    const uint8_t cmd[] = {0xAA, 0x01, 0xFF};
    while (1) {
        uart_write_safe(&g_uart, cmd, sizeof(cmd));
        uint8_t resp[8];
        int n = uart_read_safe(&g_uart, resp, sizeof(resp), 100);
        if (n > 0) printf("Sensor: got %d bytes\n", n);
        usleep(50000);
    }
    return NULL;
}

void *thread_logger(void *arg) {
    while (1) {
        const char *msg = "[LOG] status=OK\r\n";
        uart_write_safe(&g_uart, (const uint8_t*)msg, strlen(msg));
        sleep(1);
    }
    return NULL;
}

int main(void) {
    uart_init(&g_uart, "/dev/ttyUSB0", 115200);

    pthread_t t1, t2;
    pthread_create(&t1, NULL, thread_sensor, NULL);
    pthread_create(&t2, NULL, thread_logger, NULL);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    uart_deinit(&g_uart);
    return 0;
}
```

---

### Ring Buffer with Spinlock

For embedded systems or high-frequency ISR/thread communication, a lock-free-style ring buffer with atomic index updates avoids blocking in the ISR. A spinlock protects the buffer metadata in multi-core environments.

```c
// ring_buffer.h
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdbool.h>

#define RING_BUF_SIZE 256   // Must be power of 2 for efficient masking

typedef struct {
    uint8_t  buf[RING_BUF_SIZE];
    atomic_size_t head;     // Written by producer (ISR or TX thread)
    atomic_size_t tail;     // Written by consumer (application thread)
} RingBuffer;

static inline void ring_init(RingBuffer *rb) {
    atomic_store(&rb->head, 0);
    atomic_store(&rb->tail, 0);
}

// Called from ISR or producer context — must be non-blocking
static inline bool ring_push(RingBuffer *rb, uint8_t byte) {
    size_t head = atomic_load_explicit(&rb->head, memory_order_relaxed);
    size_t next_head = (head + 1) & (RING_BUF_SIZE - 1);

    if (next_head == atomic_load_explicit(&rb->tail, memory_order_acquire)) {
        return false; // Buffer full
    }

    rb->buf[head] = byte;
    atomic_store_explicit(&rb->head, next_head, memory_order_release);
    return true;
}

// Called from consumer/thread context
static inline bool ring_pop(RingBuffer *rb, uint8_t *byte) {
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_relaxed);

    if (tail == atomic_load_explicit(&rb->head, memory_order_acquire)) {
        return false; // Buffer empty
    }

    *byte = rb->buf[tail];
    atomic_store_explicit(&rb->tail, (tail + 1) & (RING_BUF_SIZE - 1),
                          memory_order_release);
    return true;
}

static inline size_t ring_available(RingBuffer *rb) {
    size_t head = atomic_load_explicit(&rb->head, memory_order_acquire);
    size_t tail = atomic_load_explicit(&rb->tail, memory_order_acquire);
    return (head - tail) & (RING_BUF_SIZE - 1);
}

#endif
```

```c
// uart_isr_safe.c — ISR integration example (bare-metal / CMSIS style)
#include "ring_buffer.h"

static RingBuffer g_rx_buf;
static RingBuffer g_tx_buf;

// Called by hardware UART ISR — no RTOS primitives allowed here
void UART1_IRQHandler(void) {
    uint32_t status = UART1->SR;

    if (status & UART_SR_RXNE) {
        // Data received — push to RX ring buffer
        uint8_t byte = (uint8_t)(UART1->DR & 0xFF);
        if (!ring_push(&g_rx_buf, byte)) {
            // Buffer overflow — track error
            g_rx_overflow_count++;
        }
    }

    if (status & UART_SR_TXE) {
        uint8_t byte;
        if (ring_pop(&g_tx_buf, &byte)) {
            UART1->DR = byte;
        } else {
            // TX buffer empty — disable TXE interrupt
            UART1->CR1 &= ~UART_CR1_TXEIE;
        }
    }
}

// Thread-safe send: pushes data to TX ring buffer, enables TXE interrupt
int uart_send(const uint8_t *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        // Spin-wait if TX buffer full (could also use semaphore to block)
        while (!ring_push(&g_tx_buf, data[i])) {
            __asm volatile("nop"); // Or yield() in RTOS context
        }
    }
    // Enable TXE interrupt to start transmission
    UART1->CR1 |= UART_CR1_TXEIE;
    return (int)len;
}

// Thread-safe receive
int uart_recv(uint8_t *buf, size_t len, uint32_t timeout_ticks) {
    size_t received = 0;
    uint32_t start = get_tick();

    while (received < len) {
        if (ring_pop(&g_rx_buf, &buf[received])) {
            received++;
        } else if ((get_tick() - start) >= timeout_ticks) {
            break; // Timeout
        }
    }
    return (int)received;
}
```

---

### POSIX Thread-Safe UART Wrapper

A C++ RAII class that automatically manages lock lifetime, preventing forgotten unlocks even when exceptions occur.

```cpp
// UartGuard.hpp
#pragma once

#include <mutex>
#include <vector>
#include <string>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

class Uart {
public:
    explicit Uart(const std::string &device, speed_t baud = B115200)
        : fd_(-1) {
        fd_ = ::open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open UART: " + device);
        configure(baud);
    }

    ~Uart() {
        if (fd_ >= 0) ::close(fd_);
    }

    // Non-copyable, non-movable (mutex is non-copyable)
    Uart(const Uart &) = delete;
    Uart &operator=(const Uart &) = delete;

    // Thread-safe write — RAII lock ensures unlock on any exit path
    ssize_t write(const uint8_t *data, size_t len) {
        std::lock_guard<std::mutex> lock(tx_mutex_);
        return writeAll(data, len);
    }

    ssize_t write(const std::string &s) {
        return write(reinterpret_cast<const uint8_t*>(s.data()), s.size());
    }

    // Thread-safe read
    ssize_t read(uint8_t *buf, size_t len) {
        std::lock_guard<std::mutex> lock(rx_mutex_);
        return ::read(fd_, buf, len);
    }

    // Transactional send-receive: holds BOTH locks for the duration.
    // Use for request-response protocols where no other thread should
    // interleave between sending a command and reading its reply.
    std::vector<uint8_t> transaction(const std::vector<uint8_t> &cmd,
                                     size_t expected_reply_len,
                                     int timeout_ms = 500) {
        // Lock TX first, then RX — always in the same order to avoid deadlock
        std::lock_guard<std::mutex> tx_lock(tx_mutex_);
        std::lock_guard<std::mutex> rx_lock(rx_mutex_);

        writeAll(cmd.data(), cmd.size());

        std::vector<uint8_t> reply(expected_reply_len);
        size_t total = 0;
        auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::milliseconds(timeout_ms);

        while (total < expected_reply_len &&
               std::chrono::steady_clock::now() < deadline) {
            ssize_t n = ::read(fd_, reply.data() + total,
                               expected_reply_len - total);
            if (n > 0) total += static_cast<size_t>(n);
        }
        reply.resize(total);
        return reply;
    }

private:
    int fd_;
    std::mutex tx_mutex_;
    std::mutex rx_mutex_;

    void configure(speed_t baud) {
        struct termios tty{};
        tcgetattr(fd_, &tty);
        cfsetospeed(&tty, baud);
        cfsetispeed(&tty, baud);
        cfmakeraw(&tty);
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 0;
        tcsetattr(fd_, TCSANOW, &tty);
    }

    ssize_t writeAll(const uint8_t *data, size_t len) {
        size_t sent = 0;
        while (sent < len) {
            ssize_t n = ::write(fd_, data + sent, len - sent);
            if (n < 0 && errno != EINTR) return -1;
            if (n > 0) sent += static_cast<size_t>(n);
        }
        return static_cast<ssize_t>(sent);
    }
};
```

**Usage:**

```cpp
#include "UartGuard.hpp"
#include <thread>
#include <iostream>

int main() {
    auto uart = std::make_shared<Uart>("/dev/ttyUSB0", B115200);

    // Thread 1: periodic status poll
    std::thread t1([&uart] {
        const std::vector<uint8_t> poll_cmd = {0x02, 0x01, 0x00, 0x03};
        while (true) {
            auto reply = uart->transaction(poll_cmd, 8, 200);
            std::cout << "Status bytes: " << reply.size() << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    // Thread 2: log messages
    std::thread t2([&uart] {
        while (true) {
            uart->write("PING\r\n");
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

    t1.join();
    t2.join();
}
```

---

### RTOS-Style Semaphore-Based Access

In FreeRTOS or similar RTOSes, binary semaphores and mutexes are the standard primitives. Using a mutex (which includes priority inheritance) rather than a binary semaphore avoids priority inversion.

```c
// freertos_uart.c
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

#define UART_TX_TIMEOUT_MS  100
#define UART_RX_TIMEOUT_MS  500

typedef struct {
    UART_HandleTypeDef *huart;      // HAL handle (STM32 example)
    SemaphoreHandle_t   tx_mutex;   // Mutex for TX (priority inheritance)
    SemaphoreHandle_t   rx_mutex;   // Mutex for RX
    SemaphoreHandle_t   tx_done;    // Binary semaphore: TX DMA complete
    SemaphoreHandle_t   rx_done;    // Binary semaphore: RX DMA complete
} RtosUart;

RtosUart g_uart1;

void uart_rtos_init(RtosUart *u, UART_HandleTypeDef *huart) {
    u->huart    = huart;
    u->tx_mutex = xSemaphoreCreateMutex();
    u->rx_mutex = xSemaphoreCreateMutex();
    u->tx_done  = xSemaphoreCreateBinary();
    u->rx_done  = xSemaphoreCreateBinary();

    configASSERT(u->tx_mutex);
    configASSERT(u->rx_mutex);
    configASSERT(u->tx_done);
    configASSERT(u->rx_done);
}

// HAL DMA TX complete callback — called from ISR
void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == g_uart1.huart) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(g_uart1.tx_done, &woken);
        portYIELD_FROM_ISR(woken);  // Yield if higher-priority task unblocked
    }
}

// HAL DMA RX complete callback
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart == g_uart1.huart) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(g_uart1.rx_done, &woken);
        portYIELD_FROM_ISR(woken);
    }
}

// Thread-safe DMA TX. Blocks until DMA completes or timeout.
HAL_StatusTypeDef uart_send_dma(RtosUart *u, const uint8_t *data, uint16_t len) {
    if (xSemaphoreTake(u->tx_mutex, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) != pdTRUE)
        return HAL_TIMEOUT;

    HAL_StatusTypeDef status =
        HAL_UART_Transmit_DMA(u->huart, (uint8_t *)data, len);

    if (status == HAL_OK) {
        // Wait for DMA completion semaphore (given by ISR callback)
        if (xSemaphoreTake(u->tx_done, pdMS_TO_TICKS(UART_TX_TIMEOUT_MS)) != pdTRUE) {
            HAL_UART_AbortTransmit(u->huart);
            status = HAL_TIMEOUT;
        }
    }

    xSemaphoreGive(u->tx_mutex);
    return status;
}

// Thread-safe DMA RX
HAL_StatusTypeDef uart_recv_dma(RtosUart *u, uint8_t *buf, uint16_t len) {
    if (xSemaphoreTake(u->rx_mutex, pdMS_TO_TICKS(UART_RX_TIMEOUT_MS)) != pdTRUE)
        return HAL_TIMEOUT;

    HAL_StatusTypeDef status =
        HAL_UART_Receive_DMA(u->huart, buf, len);

    if (status == HAL_OK) {
        if (xSemaphoreTake(u->rx_done, pdMS_TO_TICKS(UART_RX_TIMEOUT_MS)) != pdTRUE) {
            HAL_UART_AbortReceive(u->huart);
            status = HAL_TIMEOUT;
        }
    }

    xSemaphoreGive(u->rx_mutex);
    return status;
}
```

---

### Producer-Consumer with Condition Variables

For a logging system where many threads produce log messages over UART, a single dedicated UART writer thread processes a shared queue — eliminating per-thread locking contention.

```c
// uart_logger.c — single-writer pattern with condition variable
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define LOG_QUEUE_DEPTH  64
#define LOG_MSG_MAX_LEN  128

typedef struct {
    char data[LOG_MSG_MAX_LEN];
    size_t len;
} LogMessage;

typedef struct {
    LogMessage       queue[LOG_QUEUE_DEPTH];
    size_t           head, tail, count;
    pthread_mutex_t  lock;
    pthread_cond_t   not_empty;
    pthread_cond_t   not_full;
    bool             running;
} LogQueue;

static LogQueue g_log_queue;

void log_queue_init(LogQueue *q) {
    q->head = q->tail = q->count = 0;
    q->running = true;
    pthread_mutex_init(&q->lock, NULL);
    pthread_cond_init(&q->not_empty, NULL);
    pthread_cond_init(&q->not_full, NULL);
}

// Called by any producer thread — blocks if queue is full
void log_enqueue(LogQueue *q, const char *msg) {
    size_t len = strnlen(msg, LOG_MSG_MAX_LEN - 1);

    pthread_mutex_lock(&q->lock);

    // Wait while full
    while (q->count == LOG_QUEUE_DEPTH && q->running) {
        pthread_cond_wait(&q->not_full, &q->lock);
    }

    if (q->running) {
        memcpy(q->queue[q->head].data, msg, len);
        q->queue[q->head].data[len] = '\0';
        q->queue[q->head].len = len;
        q->head = (q->head + 1) % LOG_QUEUE_DEPTH;
        q->count++;
        pthread_cond_signal(&q->not_empty);
    }

    pthread_mutex_unlock(&q->lock);
}

// UART writer thread — sole consumer of the log queue
void *uart_writer_thread(void *arg) {
    LogQueue *q = (LogQueue *)arg;
    int fd = *(int *)((uint8_t *)arg + offsetof(LogQueue, running) + 8); // retrieve fd
    // (In practice, pass fd separately via a struct wrapper)

    while (1) {
        pthread_mutex_lock(&q->lock);

        while (q->count == 0 && q->running) {
            pthread_cond_wait(&q->not_empty, &q->lock);
        }

        if (!q->running && q->count == 0) {
            pthread_mutex_unlock(&q->lock);
            break;
        }

        LogMessage msg = q->queue[q->tail];
        q->tail = (q->tail + 1) % LOG_QUEUE_DEPTH;
        q->count--;
        pthread_cond_signal(&q->not_full);

        pthread_mutex_unlock(&q->lock);

        // Write to UART outside the lock — no contention during slow I/O
        write(fd, msg.data, msg.len);
    }

    return NULL;
}

void log_queue_stop(LogQueue *q) {
    pthread_mutex_lock(&q->lock);
    q->running = false;
    pthread_cond_broadcast(&q->not_empty);
    pthread_cond_broadcast(&q->not_full);
    pthread_mutex_unlock(&q->lock);
}
```

---

## Rust Implementations

Rust's ownership model, borrow checker, and type system make many concurrency bugs impossible to compile. The type system enforces "fearless concurrency" — if it compiles, it is (usually) free of data races.

### Mutex-Wrapped UART in std Environment

```rust
// uart_safe.rs — std environment (Linux, embedded-linux)
use std::io::{self, Read, Write};
use std::sync::{Arc, Mutex};
use std::time::Duration;

// Wraps any Read+Write type (serial port, TCP stream, file descriptor)
// in a thread-safe Mutex guard.
pub struct UartSafe<T> {
    inner: Mutex<T>,
}

impl<T: Read + Write + Send + 'static> UartSafe<T> {
    pub fn new(port: T) -> Self {
        UartSafe {
            inner: Mutex::new(port),
        }
    }

    /// Acquires the lock and writes all bytes. Returns error if lock is poisoned.
    pub fn write_all(&self, data: &[u8]) -> io::Result<()> {
        let mut port = self
            .inner
            .lock()
            .map_err(|_| io::Error::new(io::ErrorKind::Other, "Mutex poisoned"))?;
        port.write_all(data)
    }

    /// Acquires the lock and reads up to `buf.len()` bytes.
    pub fn read(&self, buf: &mut [u8]) -> io::Result<usize> {
        let mut port = self
            .inner
            .lock()
            .map_err(|_| io::Error::new(io::ErrorKind::Other, "Mutex poisoned"))?;
        port.read(buf)
    }

    /// Atomic send-then-receive (holds lock for entire round trip).
    pub fn transaction(&self, cmd: &[u8], reply_buf: &mut [u8]) -> io::Result<usize> {
        let mut port = self
            .inner
            .lock()
            .map_err(|_| io::Error::new(io::ErrorKind::Other, "Mutex poisoned"))?;
        port.write_all(cmd)?;
        port.read(reply_buf)
    }
}

// UartSafe is Send+Sync if T is Send — the Mutex guarantees exclusive access.
// The compiler verifies this automatically.
unsafe impl<T: Send> Sync for UartSafe<T> {}
```

**Usage with `serialport` crate:**

```rust
use serialport::SerialPort;
use std::sync::Arc;
use std::thread;
use std::time::Duration;

fn main() {
    let port = serialport::new("/dev/ttyUSB0", 115_200)
        .timeout(Duration::from_millis(200))
        .open()
        .expect("Failed to open port");

    // Arc allows multiple threads to share ownership
    let uart = Arc::new(UartSafe::new(port));

    let uart_clone = Arc::clone(&uart);
    let t1 = thread::spawn(move || {
        for _ in 0..10 {
            uart_clone.write_all(b"PING\r\n").expect("write failed");
            thread::sleep(Duration::from_millis(100));
        }
    });

    let uart_clone2 = Arc::clone(&uart);
    let t2 = thread::spawn(move || {
        let mut buf = [0u8; 64];
        for _ in 0..10 {
            match uart_clone2.read(&mut buf) {
                Ok(n) => println!("Received {} bytes", n),
                Err(e) => eprintln!("Read error: {}", e),
            }
            thread::sleep(Duration::from_millis(150));
        }
    });

    t1.join().unwrap();
    t2.join().unwrap();
}
```

---

### Arc and Mutex for Shared UART Access

Rust's `Arc<Mutex<T>>` is the idiomatic pattern for shared mutable state across threads:

```rust
// multi_thread_uart.rs
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

struct UartDriver {
    tx_buffer: Vec<u8>,
    stats: UartStats,
}

#[derive(Default)]
struct UartStats {
    bytes_sent: u64,
    bytes_received: u64,
    errors: u32,
}

impl UartDriver {
    fn new() -> Self {
        UartDriver {
            tx_buffer: Vec::with_capacity(256),
            stats: UartStats::default(),
        }
    }

    fn send(&mut self, data: &[u8]) -> Result<(), &'static str> {
        // Simulate hardware write
        self.tx_buffer.extend_from_slice(data);
        self.stats.bytes_sent += data.len() as u64;
        println!("[UART TX] {} bytes", data.len());
        self.tx_buffer.clear();
        Ok(())
    }

    fn stats(&self) -> &UartStats {
        &self.stats
    }
}

fn spawn_producer(uart: Arc<Mutex<UartDriver>>, id: u8) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        for seq in 0u8..5 {
            let msg = format!("Thread-{} Seq-{}\r\n", id, seq);

            // Lock is released when `guard` goes out of scope (end of block)
            {
                let mut driver = uart.lock().unwrap();
                driver.send(msg.as_bytes()).expect("send failed");
            } // <-- MutexGuard dropped here, lock released

            thread::sleep(Duration::from_millis(50 + (id as u64 * 13)));
        }
    })
}

fn main() {
    let uart = Arc::new(Mutex::new(UartDriver::new()));

    // Spawn 4 producer threads
    let handles: Vec<_> = (1u8..=4)
        .map(|id| spawn_producer(Arc::clone(&uart), id))
        .collect();

    for h in handles {
        h.join().unwrap();
    }

    let driver = uart.lock().unwrap();
    println!(
        "Done. Total bytes sent: {}",
        driver.stats().bytes_sent
    );
}
```

---

### no_std Embedded: Critical Sections

For bare-metal `no_std` Rust (e.g., on STM32, RP2040), the `critical-section` crate provides a portable IRQ-disable mechanism, and `cortex-m` provides hardware-specific primitives.

```rust
// embedded_uart.rs — no_std, cortex-m bare-metal
#![no_std]
#![no_main]

use core::cell::RefCell;
use cortex_m::interrupt::Mutex;
use cortex_m_rt::entry;

// Global UART peripheral protected by a critical-section Mutex.
// RefCell provides interior mutability; Mutex ensures ISR safety.
static UART: Mutex<RefCell<Option<stm32f4xx_hal::serial::Serial<
    stm32f4xx_hal::pac::USART1,
    (stm32f4xx_hal::gpio::Pin<'A', 9>, stm32f4xx_hal::gpio::Pin<'A', 10>),
>>>> = Mutex::new(RefCell::new(None));

// Ring buffer for ISR → thread communication
use heapless::spsc::{Consumer, Producer, Queue};

// Static SPSC (single-producer, single-consumer) queue — lock-free
static mut RX_QUEUE: Queue<u8, 256> = Queue::new();
static mut RX_PRODUCER: Option<Producer<'static, u8, 256>> = None;
static mut RX_CONSUMER: Option<Consumer<'static, u8, 256>> = None;

#[entry]
fn main() -> ! {
    // Initialize hardware (peripheral setup omitted for brevity)

    // Split the queue into producer (for ISR) and consumer (for main thread)
    let (producer, consumer) = unsafe { RX_QUEUE.split() };
    unsafe {
        RX_PRODUCER = Some(producer);
        RX_CONSUMER = Some(consumer);
    }

    loop {
        // Access UART within a critical section (IRQs disabled)
        cortex_m::interrupt::free(|cs| {
            if let Some(ref mut serial) = UART.borrow(cs).borrow_mut().as_mut() {
                use embedded_hal::serial::Write;
                let _ = serial.write(b'A');
            }
        });

        // Read from RX queue (safe without critical section — single consumer)
        if let Some(consumer) = unsafe { RX_CONSUMER.as_mut() } {
            while let Some(byte) = consumer.dequeue() {
                // Process received byte
                process_byte(byte);
            }
        }
    }
}

// USART1 interrupt handler
#[cortex_m_rt::interrupt]
fn USART1() {
    // Safe: producer is only used here (ISR context)
    if let Some(producer) = unsafe { RX_PRODUCER.as_mut() } {
        cortex_m::interrupt::free(|cs| {
            if let Some(ref mut serial) = UART.borrow(cs).borrow_mut().as_mut() {
                use embedded_hal::serial::Read;
                if let Ok(byte) = serial.read() {
                    let _ = producer.enqueue(byte); // Non-blocking
                }
            }
        });
    }
}

fn process_byte(_byte: u8) {
    // Application logic
}
```

**Using the `critical-section` crate (portable across architectures):**

```rust
use critical_section::Mutex;
use core::cell::Cell;

static BYTE_COUNT: Mutex<Cell<u32>> = Mutex::new(Cell::new(0));

fn increment_from_isr_or_thread() {
    critical_section::with(|cs| {
        let count = BYTE_COUNT.borrow(cs).get();
        BYTE_COUNT.borrow(cs).set(count + 1);
    });
}
```

---

### Channel-Based UART Communication

Rust's `std::sync::mpsc` (multi-producer, single-consumer) channels provide an elegant producer-consumer model for UART logging — no manual locking required.

```rust
// channel_uart.rs
use std::sync::mpsc;
use std::thread;
use std::time::Duration;
use std::io::Write;

#[derive(Debug)]
enum UartCommand {
    Send(Vec<u8>),
    Shutdown,
}

fn start_uart_writer_thread(
    mut port: Box<dyn Write + Send>,
) -> mpsc::SyncSender<UartCommand> {
    // Bounded channel with backpressure — producers block if queue is full
    let (tx, rx) = mpsc::sync_channel::<UartCommand>(64);

    thread::spawn(move || {
        while let Ok(cmd) = rx.recv() {
            match cmd {
                UartCommand::Send(data) => {
                    if let Err(e) = port.write_all(&data) {
                        eprintln!("UART write error: {}", e);
                    }
                }
                UartCommand::Shutdown => {
                    println!("UART writer thread shutting down");
                    break;
                }
            }
        }
    });

    tx
}

fn main() {
    // Create a simulated port (stdout for demo)
    let port: Box<dyn Write + Send> = Box::new(std::io::stdout());
    let uart_tx = start_uart_writer_thread(port);

    // Multiple producer threads — all share the sender by cloning
    let handles: Vec<_> = (0..5)
        .map(|id| {
            let tx = uart_tx.clone();
            thread::spawn(move || {
                for seq in 0..3 {
                    let msg = format!("[Thread {}] Message {}\r\n", id, seq);
                    // send() blocks if channel is full (backpressure)
                    tx.send(UartCommand::Send(msg.into_bytes())).ok();
                    thread::sleep(Duration::from_millis(30));
                }
            })
        })
        .collect();

    for h in handles {
        h.join().unwrap();
    }

    // Graceful shutdown
    uart_tx.send(UartCommand::Shutdown).ok();
    thread::sleep(Duration::from_millis(100)); // Allow writer to flush
}
```

---

### Atomic Ring Buffer for ISR/Thread Communication

For `no_std` or performance-critical paths, an atomic SPSC ring buffer avoids locks entirely using `core::sync::atomic`:

```rust
// atomic_ring.rs — lock-free SPSC ring buffer
use core::sync::atomic::{AtomicUsize, Ordering};
use core::cell::UnsafeCell;

const CAPACITY: usize = 256; // Must be power of 2

pub struct SpscRingBuffer {
    buf:  UnsafeCell<[u8; CAPACITY]>,
    head: AtomicUsize,  // Written by producer only
    tail: AtomicUsize,  // Written by consumer only
}

// SAFETY: head and tail are each written by exactly one side.
// The buffer cells accessed are disjoint between producer and consumer
// because head != tail ensures no overlap.
unsafe impl Sync for SpscRingBuffer {}
unsafe impl Send for SpscRingBuffer {}

impl SpscRingBuffer {
    pub const fn new() -> Self {
        SpscRingBuffer {
            buf:  UnsafeCell::new([0u8; CAPACITY]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
        }
    }

    /// Push a byte (producer context — ISR or single producer thread).
    /// Returns `Err(byte)` if the buffer is full.
    pub fn push(&self, byte: u8) -> Result<(), u8> {
        let head = self.head.load(Ordering::Relaxed);
        let next = (head + 1) & (CAPACITY - 1);

        if next == self.tail.load(Ordering::Acquire) {
            return Err(byte); // Full
        }

        // SAFETY: head is exclusively owned by producer; no concurrent write
        unsafe { (*self.buf.get())[head] = byte; }

        // Release: ensure byte is visible before head update
        self.head.store(next, Ordering::Release);
        Ok(())
    }

    /// Pop a byte (consumer context — single consumer thread).
    pub fn pop(&self) -> Option<u8> {
        let tail = self.tail.load(Ordering::Relaxed);

        if tail == self.head.load(Ordering::Acquire) {
            return None; // Empty
        }

        // SAFETY: tail is exclusively owned by consumer
        let byte = unsafe { (*self.buf.get())[tail] };

        // Release: ensure read is complete before tail update
        self.tail.store((tail + 1) & (CAPACITY - 1), Ordering::Release);
        Some(byte)
    }

    pub fn len(&self) -> usize {
        let head = self.head.load(Ordering::Acquire);
        let tail = self.tail.load(Ordering::Acquire);
        (head.wrapping_sub(tail)) & (CAPACITY - 1)
    }

    pub fn is_empty(&self) -> bool {
        self.head.load(Ordering::Acquire) == self.tail.load(Ordering::Acquire)
    }
}

// Global buffer — producer in ISR, consumer in main thread
static RX_BUFFER: SpscRingBuffer = SpscRingBuffer::new();

// Example ISR (pseudo-code)
fn uart_rx_isr() {
    let byte = read_uart_data_register();
    if RX_BUFFER.push(byte).is_err() {
        // Handle overflow (e.g., increment error counter)
    }
}

// Main thread consumer
fn process_uart_data() {
    while let Some(byte) = RX_BUFFER.pop() {
        handle_byte(byte);
    }
}

fn read_uart_data_register() -> u8 { 0 } // Placeholder
fn handle_byte(_b: u8) {}
```

---

## Common Patterns and Anti-Patterns

### ✅ Good Patterns

**Minimize lock hold time** — do not hold a UART mutex while processing received data:

```c
// Good: acquire, copy, release, then process
uint8_t local_buf[64];
int n;
pthread_mutex_lock(&uart.rx_mutex);
n = read(uart.fd, local_buf, sizeof(local_buf));
pthread_mutex_unlock(&uart.rx_mutex);
process(local_buf, n);  // Processing outside the lock

// Bad: processing inside the lock blocks other threads
pthread_mutex_lock(&uart.rx_mutex);
n = read(uart.fd, buf, sizeof(buf));
slow_process(buf, n);   // Other threads starved during processing
pthread_mutex_unlock(&uart.rx_mutex);
```

**Consistent lock ordering** — always acquire multiple mutexes in the same order to prevent deadlock:

```c
// Both TX and RX needed: ALWAYS lock tx_mutex first, then rx_mutex
pthread_mutex_lock(&uart.tx_mutex);   // Order: TX first
pthread_mutex_lock(&uart.rx_mutex);   // then RX
// ... use UART ...
pthread_mutex_unlock(&uart.rx_mutex); // Release in reverse
pthread_mutex_unlock(&uart.tx_mutex);
```

**Single writer pattern** — dedicate one thread to UART writes; other threads enqueue messages:

```
Thread A ──┐
Thread B ──┼──► Message Queue ──► UART Writer Thread ──► UART Hardware
Thread C ──┘
```

### ❌ Anti-Patterns

```c
// DEADLOCK: Thread 1 holds TX, waits for RX
//           Thread 2 holds RX, waits for TX
// Thread 1:
pthread_mutex_lock(&uart.tx_mutex);
pthread_mutex_lock(&uart.rx_mutex);  // Deadlock if Thread 2 has rx_mutex

// Thread 2:
pthread_mutex_lock(&uart.rx_mutex);
pthread_mutex_lock(&uart.tx_mutex);  // Deadlock if Thread 1 has tx_mutex
```

```rust
// WRONG in Rust: trying to share &mut across threads without Mutex
// This doesn't compile — the borrow checker prevents the data race
let mut port = open_port();
thread::spawn(|| port.write_all(b"hello")); // Error: cannot move `port` into closure
thread::spawn(|| port.write_all(b"world")); // Already moved
```

```c
// WRONG: Non-atomic check-then-act
if (uart_tx_buffer_space() > 0) {    // Thread A checks
    // Thread B preempts and also checks — both see space
    uart_write(data, len);           // Buffer overflow!
}
// Fix: hold the mutex across the check AND the write
```

---

## Platform-Specific Considerations

| Platform | Recommended Approach | Notes |
|----------|---------------------|-------|
| **Linux (pthreads)** | `pthread_mutex_t` or C++ `std::mutex` | Use `PTHREAD_MUTEX_RECURSIVE` if re-entrant calls needed |
| **Windows** | `CRITICAL_SECTION` or `std::mutex` | `CRITICAL_SECTION` is faster for same-process use |
| **FreeRTOS** | `xSemaphoreCreateMutex()` | Provides priority inheritance; prefer over binary semaphore |
| **Zephyr RTOS** | `k_mutex` | Built-in priority inheritance |
| **Bare-metal single-core** | Disable/enable interrupts | `__disable_irq()` / `__enable_irq()` on ARM |
| **Bare-metal multi-core** | Spinlock + disable interrupts | Both needed on SMP systems |
| **Rust std** | `Arc<Mutex<T>>` or channels | Compiler-enforced; cannot forget to unlock |
| **Rust no_std** | `critical-section` crate + SPSC queue | Portable across architectures |

**RS-485 Half-Duplex Special Case:**

```c
// Must hold bus direction lock across the full TX+turnaround sequence
pthread_mutex_lock(&rs485_bus_mutex);
set_rs485_direction(DIR_TX);
uart_write(data, len);
tcdrain(uart.fd);           // Wait for hardware TX complete
set_rs485_direction(DIR_RX);
pthread_mutex_unlock(&rs485_bus_mutex);
// No other thread may transmit until direction is back to RX
```

---

## Summary

UART thread safety is fundamentally about preventing concurrent access to a shared serial peripheral from causing data corruption, lost bytes, or hardware conflicts. The right approach depends on the execution environment:

**In C/C++**, POSIX mutexes (`pthread_mutex_t`) and C++ `std::mutex` guard UART access on Linux/Windows. For embedded targets using an RTOS (FreeRTOS, Zephyr), use OS-provided mutexes with priority inheritance to prevent priority inversion. On bare-metal single-core systems, simply disabling interrupts around register access is often sufficient. On multi-core embedded targets, a spinlock combined with interrupt disable is required.

**In Rust**, the type system and ownership model make thread-safety violations a compile-time error. `Arc<Mutex<T>>` is the idiomatic pattern for shared UART access in `std` environments. For `no_std` embedded targets, the `critical-section` crate provides portable ISR-safe critical regions, and lock-free SPSC ring buffers using `core::sync::atomic` enable efficient ISR-to-thread data passing without any blocking.

**Key design principles** that apply across all environments:

- Hold locks for the **shortest time possible** — copy data out, then process outside the lock
- Acquire multiple locks in a **consistent global order** to prevent deadlock
- Use the **single-writer pattern** (dedicated UART thread + message queue) for high-contention scenarios
- On embedded systems, match the synchronization primitive to the execution context: RTOS primitives for task context, interrupt-disable for ISR context, and atomics for lock-free ISR/task communication
- In Rust, prefer channels and `Arc<Mutex<T>>` over raw primitives — the type system enforces correct usage at compile time

Correctly synchronized UART drivers are deterministic, deadlock-free, and safe under any scheduling scenario — the hallmark of production-quality embedded and systems software.

---

*Document: 46_Thread_Safety.md | UART Programming Series*