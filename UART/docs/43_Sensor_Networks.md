# 43. Sensor Networks

- **Introduction** — why UART suits sensor networks
- **UART Fundamentals** — signal parameters, frame structure, and the three sensor communication patterns (streaming, poll-response, event-driven)
- **Architectures** — point-to-point, RS-485 multi-drop bus, mux-based, and USB hub topologies
- **Protocol Design** — ASCII vs. binary framing comparison, a recommended binary frame format, and Modbus RTU layout
- **C/C++ Code** — Linux `termios` init, CRC-16/CCITT, binary frame encoder/decoder state machine, C++ RAII sensor manager, RS-485 half-duplex direction control (STM32 HAL), interrupt-driven ring buffer, and DMA circular reception
- **Rust Code** — `crc.rs`, `frame.rs` with full `FrameDecoder` state machine, `sensor_reader.rs` using the `serialport` crate, async Tokio multi-sensor poller, and `no_std` embedded-hal driver
- **Error Handling** — error table with mitigations, retry logic, watchdog integration, and data staleness validation
- **Advanced Topics** — DMA-based reception, Modbus RTU in Rust, NMEA GPS parser in C, and a timestamped sensor aggregator in Rust
- **Summary** — concise recap of architecture decisions, implementation choices, and reliability patterns

## Building UART-Based Sensor Acquisition Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [UART Fundamentals for Sensor Networks](#uart-fundamentals)
3. [Sensor Network Architectures](#sensor-network-architectures)
4. [Protocol Design for Sensor Data](#protocol-design)
5. [C/C++ Implementation](#c-cpp-implementation)
6. [Rust Implementation](#rust-implementation)
7. [Error Handling and Reliability](#error-handling)
8. [Advanced Topics](#advanced-topics)
9. [Summary](#summary)

---

## 1. Introduction <a name="introduction"></a>

UART (Universal Asynchronous Receiver-Transmitter) remains one of the most widely used communication interfaces for connecting sensors to microcontrollers and host systems. Despite the proliferation of I²C, SPI, and wireless protocols, UART's simplicity, robustness, and ubiquity make it the backbone of countless sensor acquisition systems — from industrial automation to environmental monitoring.

A **UART-based sensor network** is a system where one or more sensors communicate with a host controller (MCU, SBC, or PC) using serial UART links. These networks can be point-to-point (one sensor, one controller) or bus-based (multiple sensors sharing a communication medium such as RS-485).

### Why UART for Sensor Networks?

- **Simplicity**: Only two wires (TX/RX) for point-to-point communication
- **Long-distance reach**: RS-485 variant supports up to 1200 m
- **Deterministic timing**: Asynchronous byte framing with predictable latency
- **Wide sensor support**: GPS modules, gas sensors, IMUs, barometers, and many others natively speak UART
- **Ease of debugging**: Human-readable ASCII protocols or simple binary frames are easy to inspect

---

## 2. UART Fundamentals for Sensor Networks <a name="uart-fundamentals"></a>

### 2.1 Signal Characteristics

| Parameter         | Typical Value             |
|-------------------|---------------------------|
| Baud rates        | 1200 – 921600 bps         |
| Data bits         | 8 (occasionally 7)        |
| Stop bits         | 1 or 2                    |
| Parity            | None, Even, or Odd        |
| Voltage levels    | 3.3 V or 5 V (TTL); ±12 V (RS-232) |
| Max cable length  | ~3 m (TTL); ~15 m (RS-232); ~1200 m (RS-485) |

### 2.2 Frame Structure

```
 Idle  Start  D0  D1  D2  D3  D4  D5  D6  D7  [P]  Stop  Idle
  1     0     .   .   .   .   .   .   .   .   opt   1      1
```

- **Start bit**: Always logic 0 (space), signals frame begin
- **Data bits**: LSB first by default
- **Parity bit**: Optional error-detection bit
- **Stop bit(s)**: Logic 1 (mark), return to idle

### 2.3 Sensor Communication Patterns

Sensors typically communicate over UART using one of three patterns:

| Pattern         | Description                                            | Example Sensors          |
|-----------------|--------------------------------------------------------|--------------------------|
| **Streaming**   | Sensor continuously transmits data at fixed intervals  | GPS (NMEA), IMUs         |
| **Poll-Response** | Host sends a request; sensor replies                 | Modbus RTU devices       |
| **Event-Driven**| Sensor transmits only when threshold/event occurs      | PIR motion, alarms       |

---

## 3. Sensor Network Architectures <a name="sensor-network-architectures"></a>

### 3.1 Point-to-Point (Single Sensor)

The simplest topology: one UART port per sensor.

```
 ┌──────────┐    TX ──────► RX   ┌────────────┐
 │   MCU    │                    │   Sensor   │
 │          │    RX ◄────── TX   │            │
 └──────────┘                    └────────────┘
```

**Use case**: GPS receiver, air quality sensor, single barometer.

### 3.2 Multi-Drop RS-485 Bus

RS-485 enables multiple sensors on a single twisted-pair bus using differential signaling.

```
 ┌──────────┐         ┌──────────┐   ┌──────────┐   ┌──────────┐
 │  Master  │ A+ ─────┤ Sensor 1 ├───┤ Sensor 2 ├───┤ Sensor N │
 │   MCU    │ B- ─────┤  (ID:1)  ├───┤  (ID:2)  ├───┤  (ID:N)  │
 └──────────┘         └──────────┘   └──────────┘   └──────────┘
      │                    120Ω termination at each end
```

**Key features**:
- Up to 32 unit loads (or 256 with high-impedance receivers)
- Half-duplex or full-duplex wiring options
- Requires explicit bus arbitration (master-slave or token-based)
- **Modbus RTU** is the dominant application-layer protocol

### 3.3 UART Multiplexed via Software

When hardware UART ports are scarce, a single UART can be time-multiplexed with a selector switch (e.g., 74HC4052 analog mux) or GPIO-toggled enable pins.

### 3.4 Hub-and-Spoke with USB-to-UART Adapters

In PC-connected systems, multiple CP2102/CH340/FTDI adapters allow a host computer to interface with many UART sensors simultaneously via virtual COM ports (VCP).

---

## 4. Protocol Design for Sensor Data <a name="protocol-design"></a>

### 4.1 ASCII vs Binary Framing

| Aspect        | ASCII Protocol            | Binary Protocol                |
|---------------|---------------------------|--------------------------------|
| Readability   | Human-readable            | Machine-readable only          |
| Overhead      | High (2 bytes/hex nibble) | Low (1 byte/value)             |
| Parsing       | Simple (sscanf, split)    | Requires parser/struct mapping |
| Error detect  | Checksum as ASCII hex     | CRC-8, CRC-16, CRC-32          |
| Example       | `$TEMP,+23.5,OK*4A\r\n`   | `AA 01 17 02 FA 55`            |

### 4.2 Recommended Binary Frame Format

```
┌──────┬──────┬────────┬──────────────────┬───────┐
│ SOF  │  ID  │  LEN   │     PAYLOAD      │  CRC  │
│ 0xAA │ 1 B  │  1 B   │   0 – 255 bytes  │  2 B  │
└──────┴──────┴────────┴──────────────────┴───────┘
```

- **SOF** (Start of Frame): Fixed sync byte `0xAA`
- **ID**: Sensor or command identifier
- **LEN**: Number of payload bytes
- **PAYLOAD**: Sensor data (little-endian floats, integers, etc.)
- **CRC**: CRC-16/CCITT over ID + LEN + PAYLOAD

### 4.3 Modbus RTU Frame (Industry Standard)

```
┌─────────┬──────────┬──────────┬─────────────────┬──────────┐
│ ADDR    │ FUNC     │  DATA    │     ...         │  CRC-16  │
│  1 B    │  1 B     │  N B     │                 │   2 B    │
└─────────┴──────────┴──────────┴─────────────────┴──────────┘
```

Modbus function codes relevant to sensors: `0x03` (Read Holding Registers), `0x04` (Read Input Registers).

---

## 5. C/C++ Implementation <a name="c-cpp-implementation"></a>

### 5.1 Low-Level UART Initialization (Linux termios)

```c
/* uart_init.c — Configure a UART port for sensor communication */
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

#define UART_DEVICE "/dev/ttyUSB0"
#define UART_BAUD   B9600

/**
 * Open and configure a serial port.
 * Returns file descriptor on success, -1 on error.
 */
int uart_open(const char *device, speed_t baud) {
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
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
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* 8N1: 8 data bits, no parity, 1 stop bit */
    tty.c_cflag &= ~PARENB;          /* No parity */
    tty.c_cflag &= ~CSTOPB;          /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;             /* 8-bit chars */
    tty.c_cflag &= ~CRTSCTS;         /* No hardware flow control */
    tty.c_cflag |=  CREAD | CLOCAL;  /* Enable receiver, ignore modem lines */

    /* Raw mode: no echo, no signals, no special characters */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);  /* No software flow control */
    tty.c_iflag &= ~(ICRNL | INLCR);         /* No CR/LF translation */
    tty.c_oflag &= ~OPOST;                   /* Raw output */

    /* Blocking read with 1-second timeout */
    tty.c_cc[VMIN]  = 0;   /* Return as soon as any data is available */
    tty.c_cc[VTIME] = 10;  /* 1.0 second timeout (units of 0.1s) */

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("uart_open: tcsetattr");
        close(fd);
        return -1;
    }

    tcflush(fd, TCIOFLUSH);  /* Flush any stale data */
    return fd;
}
```

---

### 5.2 CRC-16/CCITT Calculation

```c
/* crc16.h — CRC-16 for binary frame integrity */
#ifndef CRC16_H
#define CRC16_H

#include <stdint.h>
#include <stddef.h>

/**
 * Compute CRC-16/CCITT-FALSE over a byte buffer.
 * Polynomial: 0x1021, Initial value: 0xFFFF
 */
static inline uint16_t crc16_ccitt(const uint8_t *buf, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint16_t)buf[i] << 8;
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x8000)
                crc = (crc << 1) ^ 0x1021;
            else
                crc <<= 1;
        }
    }
    return crc;
}

#endif /* CRC16_H */
```

---

### 5.3 Binary Frame Encoder and Decoder

```c
/* sensor_frame.h */
#ifndef SENSOR_FRAME_H
#define SENSOR_FRAME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define FRAME_SOF       0xAA
#define FRAME_OVERHEAD  5     /* SOF(1) + ID(1) + LEN(1) + CRC(2) */
#define FRAME_MAX_LEN   260

/* Sensor IDs */
typedef enum {
    SENSOR_TEMPERATURE  = 0x01,
    SENSOR_HUMIDITY     = 0x02,
    SENSOR_PRESSURE     = 0x03,
    SENSOR_GAS          = 0x04,
    CMD_ACK             = 0x80,
    CMD_NACK            = 0x81,
} sensor_id_t;

/* Temperature/Humidity payload */
typedef struct __attribute__((packed)) {
    int16_t  temp_c10;    /* Temperature × 10 (e.g., 235 = 23.5 °C) */
    uint16_t humidity_x10; /* Relative humidity × 10 */
} temp_hum_payload_t;

/* Decoded frame */
typedef struct {
    sensor_id_t id;
    uint8_t     len;
    uint8_t     payload[255];
} sensor_frame_t;

/* Encode a frame into buf. Returns total bytes written, or -1 on error. */
int frame_encode(uint8_t *buf, size_t buf_size,
                 sensor_id_t id, const uint8_t *payload, uint8_t payload_len);

/* Feed one byte into the state machine. Returns true when a complete,
   valid frame has been received into *out. */
bool frame_receive_byte(uint8_t byte, sensor_frame_t *out);

#endif /* SENSOR_FRAME_H */
```

```c
/* sensor_frame.c */
#include "sensor_frame.h"
#include "crc16.h"
#include <string.h>

/* ── Encoder ─────────────────────────────────────────────────── */
int frame_encode(uint8_t *buf, size_t buf_size,
                 sensor_id_t id, const uint8_t *payload, uint8_t payload_len) {
    size_t total = (size_t)payload_len + FRAME_OVERHEAD;
    if (total > buf_size || total > FRAME_MAX_LEN)
        return -1;

    buf[0] = FRAME_SOF;
    buf[1] = (uint8_t)id;
    buf[2] = payload_len;
    memcpy(&buf[3], payload, payload_len);

    /* CRC over ID, LEN, and PAYLOAD */
    uint16_t crc = crc16_ccitt(&buf[1], payload_len + 2);
    buf[3 + payload_len]     = (uint8_t)(crc >> 8);
    buf[3 + payload_len + 1] = (uint8_t)(crc & 0xFF);

    return (int)total;
}

/* ── Decoder state machine ────────────────────────────────────── */
typedef enum {
    STATE_WAIT_SOF,
    STATE_READ_ID,
    STATE_READ_LEN,
    STATE_READ_PAYLOAD,
    STATE_READ_CRC_HI,
    STATE_READ_CRC_LO,
} rx_state_t;

bool frame_receive_byte(uint8_t byte, sensor_frame_t *out) {
    static rx_state_t state     = STATE_WAIT_SOF;
    static sensor_frame_t frame = {0};
    static uint8_t  rx_idx      = 0;
    static uint16_t rx_crc      = 0;

    switch (state) {
    case STATE_WAIT_SOF:
        if (byte == FRAME_SOF)
            state = STATE_READ_ID;
        break;

    case STATE_READ_ID:
        frame.id = (sensor_id_t)byte;
        state    = STATE_READ_LEN;
        break;

    case STATE_READ_LEN:
        frame.len = byte;
        rx_idx    = 0;
        state     = (byte > 0) ? STATE_READ_PAYLOAD : STATE_READ_CRC_HI;
        break;

    case STATE_READ_PAYLOAD:
        frame.payload[rx_idx++] = byte;
        if (rx_idx >= frame.len)
            state = STATE_READ_CRC_HI;
        break;

    case STATE_READ_CRC_HI:
        rx_crc = (uint16_t)byte << 8;
        state  = STATE_READ_CRC_LO;
        break;

    case STATE_READ_CRC_LO: {
        rx_crc |= byte;
        /* Validate: compute CRC over [id, len, payload] */
        uint8_t hdr[2] = { (uint8_t)frame.id, frame.len };
        uint16_t calc  = crc16_ccitt(hdr, 2);
        /* Continue CRC over payload — re-use same algorithm iteratively */
        for (uint8_t i = 0; i < frame.len; i++) {
            uint8_t b = frame.payload[i];
            calc ^= (uint16_t)b << 8;
            for (int bit = 0; bit < 8; bit++)
                calc = (calc & 0x8000) ? (calc << 1) ^ 0x1021 : calc << 1;
        }
        state = STATE_WAIT_SOF;
        if (calc == rx_crc) {
            *out = frame;
            return true;   /* Valid frame received */
        }
        /* CRC mismatch — discard frame silently */
        break;
    }
    }
    return false;
}
```

---

### 5.4 Polling Multiple Sensors (C++ with RAII)

```cpp
// sensor_manager.cpp — C++17 sensor polling manager
#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <functional>
#include <stdexcept>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>

struct SensorReading {
    int      sensor_id;
    float    temperature;   // °C
    float    humidity;      // %RH
    float    pressure;      // hPa
    uint64_t timestamp_ms;
};

class UartSensor {
public:
    explicit UartSensor(const std::string &device, speed_t baud = B9600)
        : device_(device) {
        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open " + device + ": " +
                                     std::string(strerror(errno)));
        configure(baud);
    }

    ~UartSensor() {
        if (fd_ >= 0) close(fd_);
    }

    /* Non-copyable, movable */
    UartSensor(const UartSensor &)            = delete;
    UartSensor &operator=(const UartSensor &) = delete;
    UartSensor(UartSensor &&o) : fd_(o.fd_), device_(std::move(o.device_)) {
        o.fd_ = -1;
    }

    /**
     * Send a poll command and receive response.
     * Returns true if a valid reading was parsed.
     */
    bool poll(uint8_t sensor_id, SensorReading &reading) {
        // Build request frame: [SOF][ID][LEN=0][CRC_HI][CRC_LO]
        uint8_t req[5];
        int len = frame_encode(req, sizeof(req), sensor_id, nullptr, 0);
        if (len < 0) return false;

        write(fd_, req, len);

        // Read response with timeout
        uint8_t buf[64];
        ssize_t n = read(fd_, buf, sizeof(buf));
        if (n <= 0) return false;

        sensor_frame_t frame;
        for (ssize_t i = 0; i < n; i++) {
            if (frame_receive_byte(buf[i], &frame)) {
                return parse_reading(frame, reading);
            }
        }
        return false;
    }

private:
    int         fd_;
    std::string device_;

    void configure(speed_t baud) {
        struct termios tty{};
        tcgetattr(fd_, &tty);
        cfsetspeed(&tty, baud);
        cfmakeraw(&tty);
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10;
        tcsetattr(fd_, TCSANOW, &tty);
        tcflush(fd_, TCIOFLUSH);
    }

    bool parse_reading(const sensor_frame_t &frame, SensorReading &r) {
        if (frame.id == SENSOR_TEMPERATURE && frame.len >= 4) {
            int16_t  t10, h10;
            memcpy(&t10, &frame.payload[0], 2);
            memcpy(&h10, &frame.payload[2], 2);
            r.temperature = t10 / 10.0f;
            r.humidity    = h10 / 10.0f;
            r.timestamp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            return true;
        }
        return false;
    }
};

/* Example: poll three sensors in a round-robin loop */
int main() {
    try {
        std::vector<std::unique_ptr<UartSensor>> sensors;
        sensors.emplace_back(std::make_unique<UartSensor>("/dev/ttyUSB0"));
        sensors.emplace_back(std::make_unique<UartSensor>("/dev/ttyUSB1"));
        sensors.emplace_back(std::make_unique<UartSensor>("/dev/ttyUSB2"));

        while (true) {
            for (size_t i = 0; i < sensors.size(); i++) {
                SensorReading r{};
                r.sensor_id = static_cast<int>(i + 1);
                if (sensors[i]->poll(SENSOR_TEMPERATURE, r)) {
                    printf("[Sensor %d] Temp: %.1f°C  RH: %.1f%%  t=%llu ms\n",
                           r.sensor_id, r.temperature, r.humidity,
                           (unsigned long long)r.timestamp_ms);
                } else {
                    fprintf(stderr, "[Sensor %d] No response\n", r.sensor_id);
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "Fatal: %s\n", e.what());
        return 1;
    }
    return 0;
}
```

---

### 5.5 RS-485 Half-Duplex Direction Control (Embedded / Bare Metal)

```c
/* rs485_stm32.c — RS-485 direction control on STM32 with HAL */
#include "stm32f4xx_hal.h"

#define RS485_DE_PIN    GPIO_PIN_12
#define RS485_DE_PORT   GPIOA

/* Enable transmit (assert DE/RE) */
static inline void rs485_tx_enable(void) {
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_SET);
}

/* Enable receive (deassert DE/RE) */
static inline void rs485_rx_enable(void) {
    /* Small delay to allow last byte to fully clock out */
    HAL_Delay(1);
    HAL_GPIO_WritePin(RS485_DE_PORT, RS485_DE_PIN, GPIO_PIN_RESET);
}

/**
 * Send a Modbus RTU request and collect response.
 * addr     : slave address (1–247)
 * func     : function code (e.g., 0x03)
 * reg_start: starting register address
 * reg_count: number of registers to read
 * Returns number of bytes in response, or -1 on timeout.
 */
int modbus_read_registers(UART_HandleTypeDef *huart,
                          uint8_t addr, uint8_t func,
                          uint16_t reg_start, uint16_t reg_count,
                          uint16_t *out_regs) {
    /* Build Modbus RTU request */
    uint8_t req[8];
    req[0] = addr;
    req[1] = func;
    req[2] = (reg_start >> 8) & 0xFF;
    req[3] = (reg_start)      & 0xFF;
    req[4] = (reg_count >> 8) & 0xFF;
    req[5] = (reg_count)      & 0xFF;

    uint16_t crc = modbus_crc16(req, 6);   /* Modbus uses CRC-16/IBM */
    req[6] = crc & 0xFF;                   /* Low byte first in Modbus */
    req[7] = (crc >> 8) & 0xFF;

    /* Transmit */
    rs485_tx_enable();
    HAL_UART_Transmit(huart, req, 8, 100);
    rs485_rx_enable();

    /* Receive: expected = 5 bytes overhead + 2*reg_count data bytes */
    uint8_t rsp[256];
    uint16_t expected = 5 + 2 * reg_count;
    HAL_StatusTypeDef status =
        HAL_UART_Receive(huart, rsp, expected, 200 /* ms */);

    if (status != HAL_OK)
        return -1;

    /* Verify CRC */
    uint16_t rx_crc = (uint16_t)rsp[expected - 1] << 8 | rsp[expected - 2];
    if (modbus_crc16(rsp, expected - 2) != rx_crc)
        return -1;

    /* Extract register values (big-endian) */
    for (uint16_t i = 0; i < reg_count; i++) {
        out_regs[i] = (uint16_t)rsp[3 + 2*i] << 8 | rsp[4 + 2*i];
    }
    return reg_count;
}
```

---

### 5.6 Interrupt-Driven Ring Buffer (Bare Metal C)

```c
/* ring_buffer.h — Lock-free single-producer/single-consumer ring buffer */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RING_BUF_SIZE 256   /* Must be power of 2 */

typedef struct {
    volatile uint8_t  buf[RING_BUF_SIZE];
    volatile uint16_t head;   /* Written by ISR */
    volatile uint16_t tail;   /* Read by main loop */
} ring_buf_t;

static inline bool ring_buf_push(ring_buf_t *rb, uint8_t byte) {
    uint16_t next = (rb->head + 1) & (RING_BUF_SIZE - 1);
    if (next == rb->tail) return false;   /* Buffer full */
    rb->buf[rb->head] = byte;
    rb->head = next;
    return true;
}

static inline bool ring_buf_pop(ring_buf_t *rb, uint8_t *out) {
    if (rb->tail == rb->head) return false;   /* Buffer empty */
    *out     = rb->buf[rb->tail];
    rb->tail = (rb->tail + 1) & (RING_BUF_SIZE - 1);
    return true;
}

static inline uint16_t ring_buf_available(const ring_buf_t *rb) {
    return (rb->head - rb->tail) & (RING_BUF_SIZE - 1);
}

#endif /* RING_BUFFER_H */
```

```c
/* uart_isr.c — UART receive interrupt fills ring buffer */
#include "ring_buffer.h"
#include "stm32f4xx_hal.h"

static ring_buf_t uart_rx_buf = {0};
extern UART_HandleTypeDef huart2;

/* Called by HAL_UART_IRQHandler on each received byte */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    static uint8_t rx_byte;
    if (huart->Instance == USART2) {
        ring_buf_push(&uart_rx_buf, rx_byte);
        /* Re-arm for next byte */
        HAL_UART_Receive_IT(huart, &rx_byte, 1);
    }
}

/* Main loop: drain ring buffer and decode frames */
void sensor_task(void) {
    static sensor_frame_t frame;
    uint8_t byte;
    while (ring_buf_pop(&uart_rx_buf, &byte)) {
        if (frame_receive_byte(byte, &frame)) {
            /* Process complete frame */
            process_sensor_frame(&frame);
        }
    }
}
```

---

## 6. Rust Implementation <a name="rust-implementation"></a>

Rust provides excellent memory safety and zero-cost abstractions for embedded and systems programming. The `serialport` crate handles cross-platform UART access; `embedded-hal` traits are used in no-std environments.

### 6.1 Cargo.toml Dependencies

```toml
[package]
name    = "uart_sensor_network"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport  = "4.3"
thiserror   = "1.0"
log         = "0.4"
env_logger  = "0.11"

# Optional: async support
tokio       = { version = "1", features = ["full"], optional = true }
tokio-serial = { version = "5", optional = true }

[features]
async = ["tokio", "tokio-serial"]
```

---

### 6.2 CRC-16 in Rust

```rust
// src/crc.rs — CRC-16/CCITT-FALSE implementation

/// Compute CRC-16/CCITT-FALSE over a byte slice.
/// Polynomial: 0x1021, Initial: 0xFFFF
pub fn crc16_ccitt(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= (byte as u16) << 8;
        for _ in 0..8 {
            crc = if crc & 0x8000 != 0 {
                (crc << 1) ^ 0x1021
            } else {
                crc << 1
            };
        }
    }
    crc
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn known_crc() {
        // CRC of "123456789" = 0x29B1 for CCITT-FALSE
        assert_eq!(crc16_ccitt(b"123456789"), 0x29B1);
    }
}
```

---

### 6.3 Frame Protocol

```rust
// src/frame.rs — Binary sensor frame encoder/decoder

use crate::crc::crc16_ccitt;
use std::fmt;

const FRAME_SOF: u8 = 0xAA;

/// Sensor and command identifiers
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u8)]
pub enum SensorId {
    Temperature = 0x01,
    Humidity    = 0x02,
    Pressure    = 0x03,
    Gas         = 0x04,
    Ack         = 0x80,
    Nack        = 0x81,
}

impl TryFrom<u8> for SensorId {
    type Error = FrameError;
    fn try_from(v: u8) -> Result<Self, FrameError> {
        match v {
            0x01 => Ok(Self::Temperature),
            0x02 => Ok(Self::Humidity),
            0x03 => Ok(Self::Pressure),
            0x04 => Ok(Self::Gas),
            0x80 => Ok(Self::Ack),
            0x81 => Ok(Self::Nack),
            _    => Err(FrameError::UnknownId(v)),
        }
    }
}

/// A decoded sensor frame
#[derive(Debug, Clone)]
pub struct Frame {
    pub id:      SensorId,
    pub payload: Vec<u8>,
}

/// Frame encoding/decoding errors
#[derive(Debug, thiserror::Error)]
pub enum FrameError {
    #[error("Unknown sensor ID: 0x{0:02X}")]
    UnknownId(u8),
    #[error("CRC mismatch: expected 0x{expected:04X}, got 0x{actual:04X}")]
    CrcMismatch { expected: u16, actual: u16 },
    #[error("Buffer too small")]
    BufferTooSmall,
    #[error("Payload too large ({0} bytes)")]
    PayloadTooLarge(usize),
}

impl fmt::Display for Frame {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "Frame {{ id: {:?}, payload: {:02X?} }}", self.id, self.payload)
    }
}

/// Encode a frame into a byte vector.
pub fn encode_frame(id: SensorId, payload: &[u8]) -> Result<Vec<u8>, FrameError> {
    if payload.len() > 255 {
        return Err(FrameError::PayloadTooLarge(payload.len()));
    }
    let mut buf = Vec::with_capacity(payload.len() + 5);
    buf.push(FRAME_SOF);
    buf.push(id as u8);
    buf.push(payload.len() as u8);
    buf.extend_from_slice(payload);

    let crc = crc16_ccitt(&buf[1..]);   // CRC over ID + LEN + PAYLOAD
    buf.push((crc >> 8) as u8);
    buf.push((crc & 0xFF) as u8);
    Ok(buf)
}

/// Stateful frame decoder — feed bytes one at a time.
#[derive(Default)]
pub struct FrameDecoder {
    state:   DecoderState,
    id:      u8,
    payload: Vec<u8>,
    expected_len: usize,
    crc_hi:  u8,
}

#[derive(Default, PartialEq)]
enum DecoderState {
    #[default]
    WaitSof,
    ReadId,
    ReadLen,
    ReadPayload,
    ReadCrcHi,
    ReadCrcLo,
}

impl FrameDecoder {
    /// Feed one byte. Returns `Some(Frame)` when a valid frame is complete.
    pub fn feed(&mut self, byte: u8) -> Option<Result<Frame, FrameError>> {
        use DecoderState::*;
        match self.state {
            WaitSof => {
                if byte == FRAME_SOF { self.state = ReadId; }
            }
            ReadId => {
                self.id    = byte;
                self.state = ReadLen;
            }
            ReadLen => {
                self.expected_len = byte as usize;
                self.payload.clear();
                self.state = if byte > 0 { ReadPayload } else { ReadCrcHi };
            }
            ReadPayload => {
                self.payload.push(byte);
                if self.payload.len() >= self.expected_len {
                    self.state = ReadCrcHi;
                }
            }
            ReadCrcHi => {
                self.crc_hi = byte;
                self.state  = ReadCrcLo;
            }
            ReadCrcLo => {
                let rx_crc = (self.crc_hi as u16) << 8 | byte as u16;
                self.state = WaitSof;

                // Recompute CRC over [id, len, payload]
                let mut crc_data = vec![self.id, self.expected_len as u8];
                crc_data.extend_from_slice(&self.payload);
                let calc_crc = crc16_ccitt(&crc_data);

                if calc_crc != rx_crc {
                    return Some(Err(FrameError::CrcMismatch {
                        expected: calc_crc,
                        actual:   rx_crc,
                    }));
                }

                return match SensorId::try_from(self.id) {
                    Ok(id) => Some(Ok(Frame { id, payload: self.payload.clone() })),
                    Err(e) => Some(Err(e)),
                };
            }
        }
        None
    }
}
```

---

### 6.4 UART Sensor Reader (serialport crate)

```rust
// src/sensor_reader.rs — Cross-platform UART sensor reader

use serialport::{SerialPort, SerialPortBuilder};
use std::time::Duration;
use std::io::{self, Read};

use crate::frame::{encode_frame, FrameDecoder, Frame, FrameError, SensorId};

/// A UART-connected sensor
pub struct UartSensorReader {
    port:    Box<dyn SerialPort>,
    decoder: FrameDecoder,
}

impl UartSensorReader {
    /// Open a serial port at the given baud rate.
    pub fn open(path: &str, baud: u32) -> Result<Self, serialport::Error> {
        let port = serialport::new(path, baud)
            .timeout(Duration::from_millis(1000))
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::None)
            .open()?;

        Ok(Self { port, decoder: FrameDecoder::default() })
    }

    /// Poll a sensor by sending a request and returning the response frame.
    pub fn poll(&mut self, sensor_id: SensorId) -> io::Result<Frame> {
        // Send request (empty payload)
        let req = encode_frame(sensor_id, &[])
            .map_err(|e| io::Error::new(io::ErrorKind::InvalidInput, e.to_string()))?;
        self.port.write_all(&req)?;

        // Read bytes and feed decoder until we get a frame or error
        let mut buf = [0u8; 64];
        loop {
            let n = self.port.read(&mut buf)?;
            for &byte in &buf[..n] {
                if let Some(result) = self.decoder.feed(byte) {
                    return result.map_err(|e| {
                        io::Error::new(io::ErrorKind::InvalidData, e.to_string())
                    });
                }
            }
        }
    }
}

/// Temperature/humidity sensor data
#[derive(Debug, Clone)]
pub struct TempHumReading {
    pub temperature_c: f32,
    pub humidity_pct:  f32,
}

impl TryFrom<Frame> for TempHumReading {
    type Error = FrameError;
    fn try_from(frame: Frame) -> Result<Self, FrameError> {
        if frame.payload.len() < 4 {
            return Err(FrameError::BufferTooSmall);
        }
        let t10 = i16::from_le_bytes([frame.payload[0], frame.payload[1]]);
        let h10 = i16::from_le_bytes([frame.payload[2], frame.payload[3]]);
        Ok(Self {
            temperature_c: t10 as f32 / 10.0,
            humidity_pct:  h10 as f32 / 10.0,
        })
    }
}
```

---

### 6.5 Multi-Sensor Polling Loop (Rust)

```rust
// src/main.rs — Poll multiple UART sensors

mod crc;
mod frame;
mod sensor_reader;

use sensor_reader::{UartSensorReader, TempHumReading};
use frame::SensorId;
use std::time::{Duration, Instant};
use log::{info, error};

const POLL_INTERVAL: Duration = Duration::from_millis(1000);

struct SensorNode {
    name:   &'static str,
    port:   &'static str,
    reader: UartSensorReader,
}

fn main() -> anyhow::Result<()> {
    env_logger::init();

    let port_configs = [
        ("Temperature Sensor A", "/dev/ttyUSB0"),
        ("Temperature Sensor B", "/dev/ttyUSB1"),
        ("Temperature Sensor C", "/dev/ttyUSB2"),
    ];

    let mut nodes: Vec<SensorNode> = port_configs
        .iter()
        .filter_map(|(name, port)| {
            match UartSensorReader::open(port, 9600) {
                Ok(reader) => {
                    info!("Opened {} on {}", name, port);
                    Some(SensorNode { name, port, reader })
                }
                Err(e) => {
                    error!("Cannot open {} ({}): {}", name, port, e);
                    None
                }
            }
        })
        .collect();

    if nodes.is_empty() {
        anyhow::bail!("No sensors available");
    }

    loop {
        let t0 = Instant::now();

        for node in &mut nodes {
            match node.reader.poll(SensorId::Temperature) {
                Ok(frame) => {
                    match TempHumReading::try_from(frame) {
                        Ok(reading) => {
                            info!("[{}] T={:.1}°C  RH={:.1}%",
                                  node.name,
                                  reading.temperature_c,
                                  reading.humidity_pct);
                        }
                        Err(e) => error!("[{}] Parse error: {}", node.name, e),
                    }
                }
                Err(e) => error!("[{}] Comm error: {}", node.name, e),
            }
        }

        // Sleep for the remainder of the polling interval
        if let Some(remaining) = POLL_INTERVAL.checked_sub(t0.elapsed()) {
            std::thread::sleep(remaining);
        }
    }
}
```

---

### 6.6 Async UART Sensor Network (Tokio)

```rust
// src/async_sensor.rs — Async multi-sensor polling with Tokio

use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio_serial::SerialPortBuilderExt;
use tokio::time::{interval, Duration};

use crate::frame::{encode_frame, FrameDecoder, SensorId};

pub async fn run_sensor_poller(ports: Vec<(&str, u32)>) {
    let handles: Vec<_> = ports
        .into_iter()
        .enumerate()
        .map(|(idx, (path, baud))| {
            let path = path.to_string();
            tokio::spawn(async move {
                let mut port = tokio_serial::new(&path, baud)
                    .open_native_async()
                    .expect("Failed to open port");

                let mut ticker   = interval(Duration::from_millis(500));
                let mut decoder  = FrameDecoder::default();
                let mut rx_buf   = [0u8; 128];

                loop {
                    ticker.tick().await;

                    // Send poll request
                    if let Ok(req) = encode_frame(SensorId::Temperature, &[]) {
                        let _ = port.write_all(&req).await;
                    }

                    // Receive response
                    match port.read(&mut rx_buf).await {
                        Ok(n) => {
                            for &byte in &rx_buf[..n] {
                                if let Some(Ok(frame)) = decoder.feed(byte) {
                                    println!("[Sensor {}] Frame: {}", idx, frame);
                                }
                            }
                        }
                        Err(e) => eprintln!("[Sensor {}] Read error: {}", idx, e),
                    }
                }
            })
        })
        .collect();

    for h in handles {
        let _ = h.await;
    }
}

#[tokio::main]
async fn main() {
    let sensors = vec![
        ("/dev/ttyUSB0", 9600u32),
        ("/dev/ttyUSB1", 9600u32),
    ];
    run_sensor_poller(sensors).await;
}
```

---

### 6.7 Embedded Rust (no_std with embedded-hal)

```rust
// src/embedded_uart.rs — no_std UART sensor on STM32 with embedded-hal

#![no_std]
#![no_main]

use embedded_hal::serial::{Read, Write};
use nb::block;

/// Generic UART sensor driver for embedded-hal targets.
pub struct EmbeddedSensor<UART> {
    uart: UART,
}

impl<UART> EmbeddedSensor<UART>
where
    UART: Read<u8> + Write<u8>,
    UART::Error: core::fmt::Debug,
{
    pub fn new(uart: UART) -> Self {
        Self { uart }
    }

    /// Send a byte and block until complete.
    fn send_byte(&mut self, byte: u8) {
        block!(self.uart.write(byte)).unwrap();
    }

    /// Send all bytes in a buffer.
    pub fn send_all(&mut self, data: &[u8]) {
        for &b in data {
            self.send_byte(b);
        }
        block!(self.uart.flush()).ok();
    }

    /// Read bytes into buffer, returns count received before timeout.
    /// `max_bytes`: maximum bytes to receive.
    pub fn receive(&mut self, buf: &mut [u8], max_bytes: usize) -> usize {
        let limit = max_bytes.min(buf.len());
        let mut count = 0;
        while count < limit {
            match self.uart.read() {
                Ok(byte) => {
                    buf[count] = byte;
                    count += 1;
                }
                Err(nb::Error::WouldBlock) => break,
                Err(_) => break,
            }
        }
        count
    }
}
```

---

## 7. Error Handling and Reliability <a name="error-handling"></a>

### 7.1 Common UART Errors and Mitigations

| Error             | Cause                                      | Mitigation                                |
|-------------------|--------------------------------------------|-------------------------------------------|
| **Framing error** | Baud rate mismatch, noise                  | Verify baud rates; use CRC; auto-baud     |
| **Overrun error** | MCU too slow to consume bytes              | Use DMA or ring buffer; increase FIFO     |
| **Parity error**  | Single-bit flip                            | Re-request frame; use CRC instead         |
| **CRC mismatch**  | Multi-bit corruption, collision on bus     | Retransmit with exponential backoff        |
| **Timeout**       | Sensor offline, cable fault, wrong address | Ping-check; heartbeat; watchdog           |
| **Stale data**    | Buffer not flushed between requests        | `tcflush()` / `port.clear()` before TX    |

### 7.2 Retry Logic (C)

```c
#define MAX_RETRIES     3
#define RETRY_DELAY_MS  50

int poll_with_retry(int fd, uint8_t sensor_id, sensor_frame_t *out) {
    for (int attempt = 0; attempt < MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            usleep(RETRY_DELAY_MS * 1000);
            tcflush(fd, TCIOFLUSH);   /* Flush stale bytes */
        }

        uint8_t req[5];
        int len = frame_encode(req, sizeof(req), sensor_id, NULL, 0);
        write(fd, req, len);

        uint8_t buf[64];
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            for (ssize_t i = 0; i < n; i++) {
                if (frame_receive_byte(buf[i], out))
                    return 0;   /* Success */
            }
        }
    }
    return -1;   /* All retries exhausted */
}
```

### 7.3 Watchdog Timer Integration

On bare-metal targets, reset the watchdog during every successful sensor read cycle to detect hung sensors:

```c
/* Inside sensor_task() after successfully reading all sensors */
HAL_IWDG_Refresh(&hiwdg);   /* STM32: kick the independent watchdog */
```

### 7.4 Timestamp and Data Validation

```c
typedef struct {
    sensor_frame_t frame;
    uint32_t       timestamp_ms;   /* Monotonic ms timestamp */
    bool           valid;          /* false = stale / error */
} sensor_record_t;

/* Mark data stale if not updated within 5 seconds */
void validate_sensor_records(sensor_record_t *records, int count,
                              uint32_t now_ms) {
    for (int i = 0; i < count; i++) {
        if ((now_ms - records[i].timestamp_ms) > 5000)
            records[i].valid = false;
    }
}
```

---

## 8. Advanced Topics <a name="advanced-topics"></a>

### 8.1 DMA-Based UART Reception (STM32 HAL)

DMA reception eliminates CPU overhead and is essential for high-baud-rate sensors (e.g., LiDAR at 230400 bps):

```c
/* Enable circular DMA on UART2 RX into a 256-byte ring buffer */
#define DMA_BUF_SIZE 256
static uint8_t dma_buf[DMA_BUF_SIZE];
static uint16_t last_pos = 0;

void start_uart_dma(void) {
    HAL_UART_Receive_DMA(&huart2, dma_buf, DMA_BUF_SIZE);
    /* DMA will continuously fill dma_buf in circular mode */
}

/* Call from main loop to process newly arrived bytes */
void process_dma_data(void) {
    /* Current write position = DMA_BUF_SIZE - DMA remaining count */
    uint16_t pos = DMA_BUF_SIZE -
                   __HAL_DMA_GET_COUNTER(huart2.hdmarx);

    while (last_pos != pos) {
        uint8_t byte = dma_buf[last_pos];
        last_pos = (last_pos + 1) % DMA_BUF_SIZE;

        sensor_frame_t frame;
        if (frame_receive_byte(byte, &frame))
            process_sensor_frame(&frame);
    }
}
```

### 8.2 Modbus RTU over RS-485 (Rust)

```rust
// src/modbus.rs — Minimal Modbus RTU master

/// CRC-16/IBM used by Modbus (polynomial 0xA001, reflected)
fn modbus_crc16(data: &[u8]) -> u16 {
    let mut crc: u16 = 0xFFFF;
    for &byte in data {
        crc ^= byte as u16;
        for _ in 0..8 {
            crc = if crc & 0x0001 != 0 {
                (crc >> 1) ^ 0xA001
            } else {
                crc >> 1
            };
        }
    }
    crc
}

/// Build a Modbus RTU Read Holding Registers request (FC 03).
pub fn build_read_regs_request(addr: u8, start: u16, count: u16) -> [u8; 8] {
    let mut frame = [0u8; 8];
    frame[0] = addr;
    frame[1] = 0x03;                       // Function code
    frame[2] = (start >> 8) as u8;
    frame[3] = (start & 0xFF) as u8;
    frame[4] = (count >> 8) as u8;
    frame[5] = (count & 0xFF) as u8;
    let crc   = modbus_crc16(&frame[..6]);
    frame[6]  = (crc & 0xFF) as u8;        // Low byte first
    frame[7]  = (crc >> 8) as u8;
    frame
}

/// Parse a Modbus RTU Read Holding Registers response.
/// Returns a Vec<u16> of register values on success.
pub fn parse_read_regs_response(data: &[u8]) -> Option<Vec<u16>> {
    if data.len() < 5 { return None; }
    let byte_count = data[2] as usize;
    if data.len() < 3 + byte_count + 2 { return None; }

    // Verify CRC
    let payload_len = 3 + byte_count;
    let rx_crc = u16::from_le_bytes([data[payload_len], data[payload_len + 1]]);
    if modbus_crc16(&data[..payload_len]) != rx_crc { return None; }

    // Extract register values (big-endian)
    let regs: Vec<u16> = data[3..3 + byte_count]
        .chunks_exact(2)
        .map(|c| u16::from_be_bytes([c[0], c[1]]))
        .collect();
    Some(regs)
}
```

### 8.3 NMEA Sentence Parser (GPS Sensor)

Many GPS modules stream NMEA 0183 sentences over UART at 9600 bps.

```c
/* nmea_parser.c — Parse $GPGGA sentences */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    double   latitude;   /* degrees, positive = N */
    double   longitude;  /* degrees, positive = E */
    float    altitude_m;
    uint8_t  fix_quality;
    uint8_t  num_satellites;
    bool     valid;
} gps_position_t;

/* Verify NMEA checksum: XOR of all bytes between $ and * */
static bool nmea_checksum_ok(const char *sentence) {
    const char *p = sentence + 1;   /* Skip '$' */
    uint8_t calc = 0;
    while (*p && *p != '*') calc ^= (uint8_t)*p++;
    if (*p != '*') return false;
    uint8_t expected = (uint8_t)strtol(p + 1, NULL, 16);
    return calc == expected;
}

/* Convert NMEA ddmm.mmmm format to decimal degrees */
static double nmea_to_degrees(const char *token, char direction) {
    if (!token || !*token) return 0.0;
    double raw    = atof(token);
    int    degrees = (int)(raw / 100);
    double minutes = raw - degrees * 100.0;
    double dd      = degrees + minutes / 60.0;
    return (direction == 'S' || direction == 'W') ? -dd : dd;
}

bool nmea_parse_gpgga(const char *sentence, gps_position_t *pos) {
    if (strncmp(sentence, "$GPGGA", 6) != 0 &&
        strncmp(sentence, "$GNGGA", 6) != 0)
        return false;

    if (!nmea_checksum_ok(sentence)) return false;

    char buf[128];
    strncpy(buf, sentence, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *fields[15] = {0};
    int   count = 0;
    char *tok = strtok(buf, ",");
    while (tok && count < 15) {
        fields[count++] = tok;
        tok = strtok(NULL, ",*");
    }
    if (count < 10) return false;

    pos->fix_quality    = (uint8_t)atoi(fields[6]);
    pos->num_satellites = (uint8_t)atoi(fields[7]);
    pos->altitude_m     = (float)atof(fields[9]);
    pos->latitude       = nmea_to_degrees(fields[2], fields[3] ? fields[3][0] : 'N');
    pos->longitude      = nmea_to_degrees(fields[4], fields[5] ? fields[5][0] : 'E');
    pos->valid          = (pos->fix_quality > 0);
    return true;
}
```

### 8.4 Sensor Data Aggregation and Timestamping

```rust
// src/aggregator.rs — Time-stamped sensor data store

use std::collections::VecDeque;
use std::time::{SystemTime, UNIX_EPOCH};

#[derive(Debug, Clone)]
pub struct TimestampedReading {
    pub unix_ms:     u64,
    pub sensor_id:   u8,
    pub temperature: f32,
    pub humidity:    f32,
}

pub struct SensorAggregator {
    buffer:   VecDeque<TimestampedReading>,
    capacity: usize,
}

impl SensorAggregator {
    pub fn new(capacity: usize) -> Self {
        Self {
            buffer: VecDeque::with_capacity(capacity),
            capacity,
        }
    }

    pub fn push(&mut self, sensor_id: u8, temp: f32, hum: f32) {
        let unix_ms = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .map(|d| d.as_millis() as u64)
            .unwrap_or(0);

        if self.buffer.len() >= self.capacity {
            self.buffer.pop_front();   // Drop oldest reading
        }
        self.buffer.push_back(TimestampedReading {
            unix_ms,
            sensor_id,
            temperature: temp,
            humidity:    hum,
        });
    }

    /// Compute rolling average temperature over recent N readings.
    pub fn avg_temperature(&self, sensor_id: u8, n: usize) -> Option<f32> {
        let readings: Vec<f32> = self.buffer
            .iter()
            .rev()
            .filter(|r| r.sensor_id == sensor_id)
            .take(n)
            .map(|r| r.temperature)
            .collect();

        if readings.is_empty() {
            None
        } else {
            Some(readings.iter().sum::<f32>() / readings.len() as f32)
        }
    }

    pub fn latest(&self, sensor_id: u8) -> Option<&TimestampedReading> {
        self.buffer
            .iter()
            .rev()
            .find(|r| r.sensor_id == sensor_id)
    }
}
```

---

## 9. Summary <a name="summary"></a>

UART-based sensor acquisition systems are a practical and pervasive approach to embedded sensing. This document covered the complete stack required to build reliable sensor networks:

**Architecture**: UART works well for point-to-point connections (single sensors) and scales to multi-node networks via RS-485 with Modbus RTU or custom binary protocols. The choice of topology depends on cable length, node count, and required throughput.

**Protocol Design**: Binary framing with CRC-16 integrity checking is recommended for reliability. A minimal frame with SOF, ID, length, payload, and CRC covers the vast majority of sensor use cases. For industrial environments, Modbus RTU is the industry-standard choice.

**C/C++ Implementation**: Linux `termios` provides full control over UART parameters. Interrupt-driven ring buffers and DMA reception are essential for high-throughput or CPU-constrained bare-metal applications. The C++ RAII `UartSensor` class encapsulates resource management cleanly.

**Rust Implementation**: The `serialport` crate offers cross-platform UART access with Rust's ownership guarantees preventing common resource-management bugs. The `FrameDecoder` state machine demonstrates how Rust's type system enforces protocol correctness. Async Tokio-based polling enables scalable multi-sensor systems without blocking threads.

**Reliability**: Retry logic with exponential backoff, CRC validation, watchdog timers, and data staleness checks are essential for production sensor networks. DMA circular buffers and OS-level interrupt-driven I/O prevent data loss at high baud rates.

**Advanced Sensors**: GPS NMEA parsing and Modbus RTU demonstrate how these foundations apply directly to real-world sensor families. The aggregator pattern with timestamping provides the data infrastructure needed for logging, dashboards, and alerting.

---

*This document is part of the UART Programming Reference Series. Related topics: [42. Modbus RTU](42_Modbus_RTU.md) · [44. DMA Transfer Techniques](44_DMA_Transfer_Techniques.md)*