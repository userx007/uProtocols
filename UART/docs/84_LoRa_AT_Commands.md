# 84. LoRa AT Commands — Interfacing with LoRa Modules via UART AT Commands

- **Sections 1–3** — LoRa/LoRaWAN concepts, hardware wiring diagram, and UART electrical parameters (8N1, 3.3 V TTL, baud rates).
- **Sections 4–6** — The AT command protocol in detail: line termination, timing constraints, command forms (test/read/write/execute), response codes, and a comprehensive AT command reference table for OTAA config, join, send, P2P, and radio parameters.
- **Section 7 (C/C++)** — A full platform-agnostic HAL abstraction, a core `lora_send_cmd()` engine with deadline-based line reading, high-level `lora_init/config_otaa/join/send` functions, a POSIX `main()` tying it all together, and a C++ RAII wrapper using `std::optional` for downlinks.
- **Section 8 (Rust)** — A generic `AtEngine<P: Read+Write>` over any transport, `thiserror`-based error types, a `LoRaWan<P>` high-level API, a `serialport`-backed `main()`, and a `no_std`/`embedded-hal` variant for bare-metal targets.
- **Sections 9–11** — Exponential back-off retry patterns, RX flush strategy, state machine guidance, P2P mode, FUOTA, duty cycle compliance, and a concise summary table.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [LoRa and LoRaWAN Overview](#2-lora-and-lorawan-overview)
3. [Hardware Architecture](#3-hardware-architecture)
4. [UART Communication Fundamentals for LoRa Modules](#4-uart-communication-fundamentals-for-lora-modules)
5. [AT Command Protocol](#5-at-command-protocol)
6. [Common AT Commands Reference](#6-common-at-commands-reference)
7. [C/C++ Implementation](#7-cc-implementation)
8. [Rust Implementation](#8-rust-implementation)
9. [Error Handling and Reliability](#9-error-handling-and-reliability)
10. [Advanced Topics](#10-advanced-topics)
11. [Summary](#11-summary)

---

## 1. Introduction

LoRa (Long Range) modules are low-power, long-distance wireless transceivers widely used in IoT applications — from smart agriculture and asset tracking to industrial monitoring and smart cities. Most commercial LoRa modules (such as the RAK811, Murata CMWX1ZZABZ, Seeed E5, RisingHF RHF76-052, and many others) expose their functionality through a **UART-based AT command interface**, allowing a host microcontroller to configure and operate the LoRa transceiver without needing to interact with the radio hardware directly.

This document covers:

- The UART electrical and protocol layer used to communicate with LoRa modules.
- The AT command dialect used by common LoRa modules.
- Complete, production-quality code for parsing AT responses and sending commands in **C/C++** and **Rust**.
- Robust error handling strategies, timeouts, and state-machine patterns.

---

## 2. LoRa and LoRaWAN Overview

**LoRa** is a physical layer (PHY) modulation technique based on Chirp Spread Spectrum (CSS). It enables:

- Communication ranges of 2–15 km (urban–rural).
- Extremely low power consumption (sleep currents in the µA range).
- Immunity to interference and multipath fading.
- Data rates from ~250 bps to ~50 kbps depending on spreading factor.

**LoRaWAN** is the MAC-layer protocol that runs on top of LoRa, defining how devices join a network, how data is routed through gateways to a network server, and how security (AES-128) is managed.

### Key LoRaWAN Parameters

| Parameter           | Description                                      | Typical Values         |
|---------------------|--------------------------------------------------|------------------------|
| Spreading Factor    | Controls range vs. data rate trade-off           | SF7–SF12               |
| Bandwidth           | RF channel bandwidth                             | 125 kHz, 250 kHz       |
| Coding Rate         | Forward error correction overhead                | 4/5, 4/6, 4/7, 4/8     |
| TX Power            | Transmit power                                   | 2–20 dBm               |
| Frequency           | Operating frequency band                         | 868 MHz (EU), 915 MHz (US) |
| Join Mode           | Over-the-Air Activation or Personalization       | OTAA, ABP              |

---

## 3. Hardware Architecture

A typical embedded system using a LoRa module via UART looks like this:

```
+--------------------+         UART TX/RX          +---------------------+
|  Host MCU          |<--------------------------->|  LoRa Module        |
|  (STM32, ESP32,    | (3.3V TTL, 9600–115200 baud)|  (RAK811, E5, etc.) |
|   RPi, etc.)       |                             |                     |
|                    |   Optional: RESET, BOOT pins|                     |
+--------------------+                             +---------------------+
                                                          |
                                                    LoRa RF Antenna
                                                          |
                                                   LoRaWAN Gateway
                                                          |
                                                    Network Server
                                                    (TTN, Chirpstack)
```

### Typical UART Settings

| Parameter    | Value                      |
|--------------|----------------------------|
| Baud Rate    | 9600 or 115200 bps         |
| Data Bits    | 8                          |
| Parity       | None                       |
| Stop Bits    | 1 (8N1)                    |
| Flow Control | None (usually)             |
| Logic Level  | 3.3 V TTL                  |

> **Important:** Many LoRa modules operate at 3.3 V logic. Do **not** connect directly to a 5 V UART without a level shifter.

---

## 4. UART Communication Fundamentals for LoRa Modules

### Line Termination

AT commands sent to LoRa modules are terminated with `\r\n` (CR+LF). Responses from the module are also terminated with `\r\n`. A response sequence typically looks like:

```
Host → Module:  "AT+VERSION\r\n"
Module → Host:  "+VERSION: 1.0.2\r\n"
                "OK\r\n"
```

Or on error:

```
Host → Module:  "AT+SEND=1,2,AABBCC\r\n"
Module → Host:  "ERROR(-1)\r\n"
```

### Timing Considerations

- **Command timeout:** Most simple queries respond within 100–500 ms.
- **Join timeout:** OTAA join can take 6–10 seconds (waiting for join-accept window).
- **TX timeout:** Transmission + receive window can take up to 6 seconds.
- **Always implement per-command timeouts** to prevent blocking the host indefinitely.

---

## 5. AT Command Protocol

### Command Structure

```
AT[+CMD][=<param1>,<param2>,...]\r\n
```

| Form             | Example                     | Purpose             |
|------------------|-----------------------------|---------------------|
| Test             | `AT\r\n`                    | Check module alive  |
| Read             | `AT+BAND?\r\n`              | Query parameter     |
| Write            | `AT+BAND=0\r\n`             | Set parameter       |
| Execute          | `AT+JOIN\r\n`               | Trigger action      |

### Response Codes

| Response    | Meaning                                  |
|-------------|------------------------------------------|
| `OK`        | Command accepted and executed            |
| `ERROR`     | Command failed (generic)                 |
| `ERROR(-1)` | Invalid parameter                        |
| `ERROR(-2)` | Unsupported command                      |
| `ERROR(-3)` | Radio busy                               |
| `+EVT:JOIN` | Asynchronous join result event           |
| `+EVT:RX`   | Asynchronous received data event         |

> **Note:** Different manufacturers use slightly different AT dialects. Always consult your specific module's datasheet. The examples here target the **RAK Wireless** and **Seeed LoRa-E5** command sets, which are among the most common.

---

## 6. Common AT Commands Reference

### Module Control

| Command           | Description                         | Example Response        |
|-------------------|-------------------------------------|-------------------------|
| `AT`              | Ping / alive check                  | `OK`                    |
| `AT+RESET`        | Software reset module               | `OK`                    |
| `AT+VERSION`      | Firmware version                    | `+VERSION: 1.0.3\r\nOK` |
| `AT+SLEEP`        | Enter sleep mode                    | `OK`                    |

### LoRaWAN Configuration

| Command                          | Description                             |
|----------------------------------|-----------------------------------------|
| `AT+DEVEUI=<16-hex-chars>`       | Set Device EUI (OTAA)                   |
| `AT+APPEUI=<16-hex-chars>`       | Set Application EUI (OTAA)              |
| `AT+APPKEY=<32-hex-chars>`       | Set Application Key (OTAA)              |
| `AT+DEVADDR=<8-hex-chars>`       | Set Device Address (ABP)                |
| `AT+APPSKEY=<32-hex-chars>`      | Set App Session Key (ABP)               |
| `AT+NWKSKEY=<32-hex-chars>`      | Set Network Session Key (ABP)           |
| `AT+NJM=1`                       | Set join mode: 1=OTAA, 0=ABP            |
| `AT+BAND=5`                      | Set frequency band (5=EU868)            |
| `AT+DR=3`                        | Set data rate (0–5 for EU868)           |
| `AT+ADR=1`                       | Enable Adaptive Data Rate               |
| `AT+PORT=1`                      | Set application port (1–223)            |

### Join and Send

| Command                          | Description                             |
|----------------------------------|-----------------------------------------|
| `AT+JOIN`                        | Start OTAA join procedure               |
| `AT+JOIN=1:0:10:8`               | Join with auto-retry (mode:retry:period:cnt) |
| `AT+SEND=<port>:<confirm>:<data>`| Send hex data (confirm: 0=unconf, 1=conf) |
| `AT+RECVB=?`                     | Query last received data                |

### Radio Parameters (P2P / Raw LoRa)

| Command                          | Description                             |
|----------------------------------|-----------------------------------------|
| `AT+NWM=0`                       | Switch to raw LoRa P2P mode             |
| `AT+PFREQ=868000000`             | Set P2P frequency (Hz)                  |
| `AT+PSF=7`                       | Set P2P spreading factor                |
| `AT+PBW=0`                       | Set P2P bandwidth (0=125kHz)            |
| `AT+PCR=0`                       | Set P2P coding rate (0=4/5)             |
| `AT+PPL=8`                       | Set P2P preamble length                 |
| `AT+PTP=14`                      | Set P2P TX power (dBm)                  |
| `AT+PSEND=<hex>`                 | Send raw P2P packet                     |
| `AT+PRECV=<timeout_ms>`          | Open P2P receive window                 |

---

## 7. C/C++ Implementation

### 7.1 Platform Abstraction Layer

```c
/* lora_uart.h — Platform-agnostic UART abstraction for LoRa AT interface */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* Maximum length of one AT response line */
#define LORA_LINE_BUF_LEN   256
/* Maximum number of lines in a multi-line response */
#define LORA_MAX_LINES      16
/* Default command timeout in milliseconds */
#define LORA_DEFAULT_TIMEOUT_MS  2000
/* JOIN timeout — OTAA can take several seconds */
#define LORA_JOIN_TIMEOUT_MS    12000
/* TX timeout — TX + RX windows */
#define LORA_TX_TIMEOUT_MS       6000

typedef enum {
    LORA_OK          =  0,
    LORA_ERR_TIMEOUT = -1,
    LORA_ERR_PARAM   = -2,
    LORA_ERR_BUSY    = -3,
    LORA_ERR_NOJOIN  = -4,
    LORA_ERR_UNKNOWN = -99,
} lora_err_t;

/* Response container — holds up to LORA_MAX_LINES response lines */
typedef struct {
    char    lines[LORA_MAX_LINES][LORA_LINE_BUF_LEN];
    int     count;          /* Number of lines received               */
    bool    ok;             /* True if final line was "OK"            */
    int     error_code;     /* Numeric error code if not ok           */
} lora_response_t;

/*
 * HAL callbacks — implement these for your platform (STM32, Linux, ESP-IDF, etc.)
 */
typedef struct {
    /* Write 'len' bytes to UART. Returns number of bytes written. */
    int  (*uart_write)(const uint8_t *buf, size_t len, void *ctx);
    /* Read one byte with timeout_ms. Returns 1 on success, 0 on timeout. */
    int  (*uart_read_byte)(uint8_t *byte, uint32_t timeout_ms, void *ctx);
    /* Get current time in milliseconds (for timeout tracking). */
    uint32_t (*get_ms)(void *ctx);
    /* Platform context pointer (e.g., UART handle) */
    void *ctx;
} lora_hal_t;
```

### 7.2 Core AT Command Engine

```c
/* lora_at.c — Core AT command send/receive engine */
#include "lora_uart.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

/* Internal: read one '\n'-terminated line into buf (strips \r\n).
 * Returns number of chars read, or -1 on timeout. */
static int read_line(const lora_hal_t *hal, char *buf, size_t buf_len,
                     uint32_t deadline_ms)
{
    size_t pos = 0;
    uint8_t ch;

    while (pos < buf_len - 1) {
        uint32_t now = hal->get_ms(hal->ctx);
        if (now >= deadline_ms) return -1;                 /* timeout      */

        uint32_t remaining = deadline_ms - now;
        if (!hal->uart_read_byte(&ch, remaining, hal->ctx))
            return -1;                                     /* read timeout */

        if (ch == '\r') continue;                          /* skip CR      */
        if (ch == '\n') { buf[pos] = '\0'; return (int)pos; }
        buf[pos++] = (char)ch;
    }
    buf[pos] = '\0';
    return (int)pos;
}

/* Parse error code from strings like "ERROR(-1)" or "ERROR" */
static int parse_error_code(const char *line)
{
    const char *p = strstr(line, "ERROR(");
    if (p) {
        p += 6;
        return atoi(p);                /* e.g., -1 */
    }
    return -99;                        /* generic unknown error */
}

/*
 * lora_send_cmd — Send an AT command and collect the response.
 *
 * @param hal         Platform HAL
 * @param cmd         Command string WITHOUT \r\n (e.g., "AT+VERSION")
 * @param timeout_ms  Total time to wait for "OK" or "ERROR"
 * @param resp        Output: filled response struct
 * @return LORA_OK or LORA_ERR_*
 */
lora_err_t lora_send_cmd(const lora_hal_t *hal,
                         const char       *cmd,
                         uint32_t          timeout_ms,
                         lora_response_t  *resp)
{
    /* Build command with CRLF */
    char full_cmd[LORA_LINE_BUF_LEN];
    int  cmd_len = snprintf(full_cmd, sizeof(full_cmd), "%s\r\n", cmd);
    if (cmd_len < 0 || cmd_len >= (int)sizeof(full_cmd))
        return LORA_ERR_PARAM;

    /* Clear response */
    memset(resp, 0, sizeof(*resp));

    /* Transmit */
    hal->uart_write((const uint8_t *)full_cmd, (size_t)cmd_len, hal->ctx);

    /* Calculate deadline */
    uint32_t deadline = hal->get_ms(hal->ctx) + timeout_ms;

    /* Read response lines until "OK" or "ERROR" or timeout */
    while (resp->count < LORA_MAX_LINES) {
        char line[LORA_LINE_BUF_LEN];
        int  n = read_line(hal, line, sizeof(line), deadline);

        if (n < 0) return LORA_ERR_TIMEOUT;
        if (n == 0) continue;                   /* blank line — skip       */

        /* Store the line */
        strncpy(resp->lines[resp->count], line, LORA_LINE_BUF_LEN - 1);
        resp->count++;

        /* Terminal conditions */
        if (strcmp(line, "OK") == 0) {
            resp->ok = true;
            return LORA_OK;
        }
        if (strncmp(line, "ERROR", 5) == 0) {
            resp->ok        = false;
            resp->error_code = parse_error_code(line);
            return (resp->error_code == -2) ? LORA_ERR_PARAM : LORA_ERR_UNKNOWN;
        }
    }
    return LORA_ERR_TIMEOUT;
}

/* Convenience: send command and check OK, ignore response body */
lora_err_t lora_cmd_ok(const lora_hal_t *hal, const char *cmd, uint32_t timeout_ms)
{
    lora_response_t resp;
    return lora_send_cmd(hal, cmd, timeout_ms, &resp);
}
```

### 7.3 LoRaWAN High-Level API

```c
/* lora_lorawan.h / lora_lorawan.c — High-level LoRaWAN operations */
#include "lora_uart.h"
#include <string.h>
#include <stdio.h>

/* ---- OTAA Configuration ---- */
typedef struct {
    char dev_eui [17];   /* 8 bytes = 16 hex chars + '\0' */
    char app_eui [17];
    char app_key [33];   /* 16 bytes = 32 hex chars + '\0' */
} lora_otaa_cfg_t;

/* ---- Received downlink data ---- */
typedef struct {
    uint8_t  port;
    uint8_t  data[242];
    size_t   data_len;
    bool     confirmed;
} lora_rx_t;

/*
 * lora_init — Reset module and verify it responds.
 */
lora_err_t lora_init(const lora_hal_t *hal)
{
    lora_err_t err;

    /* Software reset */
    err = lora_cmd_ok(hal, "AT+RESET", LORA_DEFAULT_TIMEOUT_MS);
    if (err != LORA_OK) return err;

    /* Allow module time to boot */
    /* (In real code, use a platform delay here — e.g., HAL_Delay(500)) */

    /* Verify alive */
    return lora_cmd_ok(hal, "AT", LORA_DEFAULT_TIMEOUT_MS);
}

/*
 * lora_config_otaa — Program OTAA credentials into the module.
 */
lora_err_t lora_config_otaa(const lora_hal_t *hal, const lora_otaa_cfg_t *cfg)
{
    char cmd[80];
    lora_err_t err;

    /* Set OTAA join mode */
    err = lora_cmd_ok(hal, "AT+NJM=1", LORA_DEFAULT_TIMEOUT_MS);
    if (err != LORA_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+DEVEUI=%s", cfg->dev_eui);
    err = lora_cmd_ok(hal, cmd, LORA_DEFAULT_TIMEOUT_MS);
    if (err != LORA_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+APPEUI=%s", cfg->app_eui);
    err = lora_cmd_ok(hal, cmd, LORA_DEFAULT_TIMEOUT_MS);
    if (err != LORA_OK) return err;

    snprintf(cmd, sizeof(cmd), "AT+APPKEY=%s", cfg->app_key);
    return lora_cmd_ok(hal, cmd, LORA_DEFAULT_TIMEOUT_MS);
}

/*
 * lora_join — Trigger OTAA join and wait for +EVT:JOIN_FAILED or +EVT:JOINED.
 *
 * Many modules emit an asynchronous event after the join attempt.
 * We read lines until we see the event or timeout.
 */
lora_err_t lora_join(const lora_hal_t *hal)
{
    lora_response_t resp;
    lora_err_t      err;

    /* Issue join command */
    err = lora_send_cmd(hal, "AT+JOIN", LORA_JOIN_TIMEOUT_MS, &resp);
    if (err != LORA_OK) return err;

    /* Check response lines for join result event */
    for (int i = 0; i < resp.count; i++) {
        if (strstr(resp.lines[i], "JOINED"))       return LORA_OK;
        if (strstr(resp.lines[i], "JOIN_FAILED"))  return LORA_ERR_NOJOIN;
    }

    /*
     * Some modules require polling or waiting for an async event
     * emitted separately from the OK response. In that case, read
     * additional lines after OK until the EVT appears.
     */
    uint32_t deadline = hal->get_ms(hal->ctx) + LORA_JOIN_TIMEOUT_MS;
    char line[LORA_LINE_BUF_LEN];
    while (hal->get_ms(hal->ctx) < deadline) {
        int n = read_line(hal, line, sizeof(line), deadline);
        if (n < 0) break;
        if (strstr(line, "JOINED"))       return LORA_OK;
        if (strstr(line, "JOIN_FAILED"))  return LORA_ERR_NOJOIN;
    }
    return LORA_ERR_TIMEOUT;
}

/*
 * lora_send — Send an uplink payload (hex-encoded) on the given port.
 *
 * @param port     LoRaWAN application port (1–223)
 * @param data     Raw bytes to send
 * @param data_len Number of bytes
 * @param confirm  true = confirmed uplink, false = unconfirmed
 * @param rx       If non-NULL, filled with any received downlink
 */
lora_err_t lora_send(const lora_hal_t *hal,
                     uint8_t           port,
                     const uint8_t    *data,
                     size_t            data_len,
                     bool              confirm,
                     lora_rx_t        *rx)
{
    if (data_len > 242) return LORA_ERR_PARAM;

    /* Build hex payload string */
    char hex_payload[485];         /* 242 bytes * 2 hex chars + '\0' */
    for (size_t i = 0; i < data_len; i++)
        snprintf(&hex_payload[i * 2], 3, "%02X", data[i]);
    hex_payload[data_len * 2] = '\0';

    /* Build AT+SEND command */
    char cmd[520];
    snprintf(cmd, sizeof(cmd), "AT+SEND=%u:%u:%s",
             port, confirm ? 1 : 0, hex_payload);

    lora_response_t resp;
    lora_err_t      err = lora_send_cmd(hal, cmd, LORA_TX_TIMEOUT_MS, &resp);

    /* Parse downlink if caller wants it */
    if (rx && err == LORA_OK) {
        for (int i = 0; i < resp.count; i++) {
            /* Example: "+EVT:RX_1:AABBCC" or "+RX:port=1,data=AABB" */
            const char *p = strstr(resp.lines[i], "+EVT:RX");
            if (!p) p = strstr(resp.lines[i], "+RX:");
            if (p) {
                /* Minimal parser — extend per your module's dialect */
                rx->port     = (uint8_t)port;    /* simplification */
                rx->data_len = 0;
                /* Locate hex data after last ':' */
                const char *hex = strrchr(resp.lines[i], ':');
                if (hex) {
                    hex++;
                    size_t hex_len = strlen(hex);
                    for (size_t j = 0; j + 1 < hex_len && rx->data_len < 242; j += 2) {
                        unsigned byte_val = 0;
                        sscanf(&hex[j], "%02X", &byte_val);
                        rx->data[rx->data_len++] = (uint8_t)byte_val;
                    }
                }
                break;
            }
        }
    }
    return err;
}
```

### 7.4 Usage Example (C)

```c
/* main.c — Example using the LoRa AT command library on a POSIX system */
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <time.h>
#include "lora_uart.h"

/* ---- POSIX UART HAL implementation ---- */

static int posix_uart_fd = -1;

static int posix_write(const uint8_t *buf, size_t len, void *ctx)
{
    (void)ctx;
    return (int)write(posix_uart_fd, buf, len);
}

static int posix_read_byte(uint8_t *byte, uint32_t timeout_ms, void *ctx)
{
    (void)ctx;
    fd_set  rfds;
    struct  timeval tv;
    FD_ZERO(&rfds);
    FD_SET(posix_uart_fd, &rfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    int ret = select(posix_uart_fd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return 0;
    return (read(posix_uart_fd, byte, 1) == 1) ? 1 : 0;
}

static uint32_t posix_get_ms(void *ctx)
{
    (void)ctx;
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static int open_uart(const char *device, int baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd < 0) return -1;
    struct termios tty;
    tcgetattr(fd, &tty);
    cfsetospeed(&tty, (speed_t)baud);   /* e.g., B9600 or B115200 */
    cfsetispeed(&tty, (speed_t)baud);
    tty.c_cflag  = CS8 | CREAD | CLOCAL;
    tty.c_iflag  = IGNPAR;
    tty.c_oflag  = 0;
    tty.c_lflag  = 0;
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &tty);
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(void)
{
    posix_uart_fd = open_uart("/dev/ttyUSB0", B9600);
    if (posix_uart_fd < 0) { perror("open_uart"); return 1; }

    lora_hal_t hal = {
        .uart_write    = posix_write,
        .uart_read_byte= posix_read_byte,
        .get_ms        = posix_get_ms,
        .ctx           = NULL,
    };

    /* Initialize module */
    if (lora_init(&hal) != LORA_OK) {
        fprintf(stderr, "Module not responding\n");
        return 1;
    }
    printf("Module alive\n");

    /* Configure OTAA credentials */
    lora_otaa_cfg_t otaa = {
        .dev_eui = "0102030405060708",
        .app_eui = "0807060504030201",
        .app_key = "01020304050607080102030405060708",
    };
    if (lora_config_otaa(&hal, &otaa) != LORA_OK) {
        fprintf(stderr, "OTAA config failed\n");
        return 1;
    }

    /* Join network */
    printf("Joining network...\n");
    if (lora_join(&hal) != LORA_OK) {
        fprintf(stderr, "Join failed\n");
        return 1;
    }
    printf("Joined successfully\n");

    /* Send a payload every 30 seconds */
    for (;;) {
        uint8_t payload[] = { 0xDE, 0xAD, 0xBE, 0xEF };
        lora_rx_t rx = {0};

        lora_err_t err = lora_send(&hal, 1, payload, sizeof(payload),
                                   false /* unconfirmed */, &rx);
        if (err == LORA_OK) {
            printf("TX success");
            if (rx.data_len > 0) {
                printf(", RX %zu bytes:", rx.data_len);
                for (size_t i = 0; i < rx.data_len; i++)
                    printf(" %02X", rx.data[i]);
            }
            printf("\n");
        } else {
            fprintf(stderr, "TX error: %d\n", err);
        }
        sleep(30);
    }
}
```

### 7.5 C++ Object-Oriented Wrapper

```cpp
// LoRaModule.hpp — C++ RAII wrapper around the C AT command engine
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <stdexcept>
#include "lora_uart.h"

class LoRaModule {
public:
    struct OtaaConfig {
        std::string dev_eui;   // 16 hex chars
        std::string app_eui;   // 16 hex chars
        std::string app_key;   // 32 hex chars
    };

    struct Downlink {
        uint8_t              port;
        std::vector<uint8_t> data;
    };

    explicit LoRaModule(lora_hal_t hal) : hal_(hal) {}

    void init() {
        check(lora_init(&hal_), "init");
    }

    std::string version() {
        lora_response_t resp;
        check(lora_send_cmd(&hal_, "AT+VERSION",
                            LORA_DEFAULT_TIMEOUT_MS, &resp), "version");
        // First line is "+VERSION: X.Y.Z"
        return resp.count > 0 ? resp.lines[0] : "";
    }

    void configOtaa(const OtaaConfig &cfg) {
        lora_otaa_cfg_t c{};
        cfg.dev_eui.copy(c.dev_eui, 16);
        cfg.app_eui.copy(c.app_eui, 16);
        cfg.app_key.copy(c.app_key, 32);
        check(lora_config_otaa(&hal_, &c), "config_otaa");
    }

    void join() {
        check(lora_join(&hal_), "join");
    }

    std::optional<Downlink> send(uint8_t port,
                                 const std::vector<uint8_t> &payload,
                                 bool confirm = false)
    {
        lora_rx_t rx{};
        lora_err_t err = lora_send(&hal_, port,
                                   payload.data(), payload.size(),
                                   confirm, &rx);
        check(err, "send");
        if (rx.data_len > 0) {
            Downlink dl;
            dl.port = rx.port;
            dl.data.assign(rx.data, rx.data + rx.data_len);
            return dl;
        }
        return std::nullopt;
    }

private:
    lora_hal_t hal_;

    static void check(lora_err_t err, const char *op) {
        if (err != LORA_OK) {
            throw std::runtime_error(
                std::string(op) + " failed: error=" + std::to_string(err));
        }
    }
};
```

---

## 8. Rust Implementation

### 8.1 Project Setup (`Cargo.toml`)

```toml
[package]
name    = "lora-at"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"      # Cross-platform serial port access
thiserror  = "1"      # Ergonomic error types
log        = "0.4"
env_logger = "0.11"

[features]
default = []
std     = []
```

### 8.2 Error Types

```rust
// src/error.rs
use thiserror::Error;

#[derive(Debug, Error)]
pub enum LoraError {
    #[error("UART I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("Command timed out")]
    Timeout,

    #[error("Module returned error code {0}")]
    ModuleError(i32),

    #[error("Join failed")]
    JoinFailed,

    #[error("Invalid parameter: {0}")]
    InvalidParam(String),

    #[error("Serial port error: {0}")]
    Serial(#[from] serialport::Error),
}

pub type LoraResult<T> = Result<T, LoraError>;
```

### 8.3 AT Command Engine

```rust
// src/at_engine.rs
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use crate::error::{LoraError, LoraResult};

/// One parsed AT response — collection of lines plus terminal status.
#[derive(Debug, Default)]
pub struct AtResponse {
    pub lines: Vec<String>,
    pub ok:    bool,
}

/// Low-level AT command engine that owns a serial port handle.
pub struct AtEngine<P> {
    port:    P,
    buf:     Vec<u8>,          // Byte accumulation buffer
}

impl<P> AtEngine<P>
where
    P: Read + Write,
{
    pub fn new(port: P) -> Self {
        Self { port, buf: Vec::with_capacity(256) }
    }

    /// Send raw bytes (a command with CRLF) over UART.
    fn write_cmd(&mut self, cmd: &str) -> LoraResult<()> {
        let full = format!("{}\r\n", cmd);
        log::debug!("TX: {:?}", full.trim());
        self.port.write_all(full.as_bytes())?;
        Ok(())
    }

    /// Read bytes until '\n', with deadline enforcement.
    /// Returns the line without trailing CR/LF, or Err(Timeout).
    fn read_line(&mut self, deadline: Instant) -> LoraResult<String> {
        self.buf.clear();
        let mut byte = [0u8; 1];

        loop {
            if Instant::now() >= deadline {
                return Err(LoraError::Timeout);
            }
            match self.port.read(&mut byte) {
                Ok(1) => {
                    let ch = byte[0];
                    if ch == b'\r' { continue; }
                    if ch == b'\n' {
                        let s = String::from_utf8_lossy(&self.buf).into_owned();
                        log::debug!("RX: {:?}", s);
                        return Ok(s);
                    }
                    self.buf.push(ch);
                    if self.buf.len() > 512 {
                        self.buf.clear();   // overflow guard
                    }
                }
                Ok(_) | Err(_) => {
                    // Non-blocking read returned no data; yield briefly.
                    std::thread::sleep(Duration::from_millis(1));
                }
            }
        }
    }

    /// Parse numeric error code from "ERROR(-1)" style strings.
    fn parse_error_code(line: &str) -> i32 {
        if let Some(start) = line.find("ERROR(") {
            let inner = &line[start + 6..];
            if let Some(end) = inner.find(')') {
                return inner[..end].parse().unwrap_or(-99);
            }
        }
        -99
    }

    /// Send an AT command and collect the full response.
    ///
    /// Blocks until "OK" or "ERROR" is received, or `timeout` elapses.
    pub fn send_cmd(&mut self, cmd: &str, timeout: Duration) -> LoraResult<AtResponse> {
        self.write_cmd(cmd)?;

        let deadline = Instant::now() + timeout;
        let mut resp = AtResponse::default();

        loop {
            let line = self.read_line(deadline)?;

            if line.is_empty() { continue; }

            if line == "OK" {
                resp.ok = true;
                resp.lines.push(line);
                return Ok(resp);
            }

            if line.starts_with("ERROR") {
                let code = Self::parse_error_code(&line);
                resp.lines.push(line);
                return Err(LoraError::ModuleError(code));
            }

            resp.lines.push(line);
        }
    }

    /// Convenience: send command and assert OK.
    pub fn cmd_ok(&mut self, cmd: &str, timeout: Duration) -> LoraResult<()> {
        self.send_cmd(cmd, timeout)?;
        Ok(())
    }

    /// Read lines asynchronously (e.g., waiting for an EVT after OK).
    pub fn read_event(&mut self, timeout: Duration) -> LoraResult<String> {
        let deadline = Instant::now() + timeout;
        loop {
            let line = self.read_line(deadline)?;
            if !line.is_empty() {
                return Ok(line);
            }
        }
    }
}
```

### 8.4 LoRaWAN High-Level API

```rust
// src/lorawan.rs
use std::time::Duration;
use crate::at_engine::AtEngine;
use crate::error::{LoraError, LoraResult};
use std::io::{Read, Write};

const DEFAULT_TIMEOUT: Duration = Duration::from_secs(2);
const JOIN_TIMEOUT:    Duration = Duration::from_secs(12);
const TX_TIMEOUT:      Duration = Duration::from_secs(6);

/// OTAA join credentials
pub struct OtaaConfig {
    pub dev_eui: String,   // 16 hex chars
    pub app_eui: String,   // 16 hex chars
    pub app_key: String,   // 32 hex chars
}

/// Received downlink data
#[derive(Debug)]
pub struct Downlink {
    pub port: u8,
    pub data: Vec<u8>,
}

/// High-level LoRaWAN driver
pub struct LoRaWan<P> {
    engine: AtEngine<P>,
}

impl<P: Read + Write> LoRaWan<P> {
    pub fn new(port: P) -> Self {
        Self { engine: AtEngine::new(port) }
    }

    /// Reset and verify the module responds.
    pub fn init(&mut self) -> LoraResult<()> {
        self.engine.cmd_ok("AT+RESET", DEFAULT_TIMEOUT)?;
        std::thread::sleep(Duration::from_millis(500));
        self.engine.cmd_ok("AT", DEFAULT_TIMEOUT)?;
        log::info!("LoRa module initialized");
        Ok(())
    }

    /// Query firmware version string.
    pub fn version(&mut self) -> LoraResult<String> {
        let resp = self.engine.send_cmd("AT+VERSION", DEFAULT_TIMEOUT)?;
        Ok(resp.lines.first().cloned().unwrap_or_default())
    }

    /// Program OTAA credentials.
    pub fn config_otaa(&mut self, cfg: &OtaaConfig) -> LoraResult<()> {
        self.engine.cmd_ok("AT+NJM=1", DEFAULT_TIMEOUT)?;
        self.engine.cmd_ok(&format!("AT+DEVEUI={}", cfg.dev_eui), DEFAULT_TIMEOUT)?;
        self.engine.cmd_ok(&format!("AT+APPEUI={}", cfg.app_eui), DEFAULT_TIMEOUT)?;
        self.engine.cmd_ok(&format!("AT+APPKEY={}", cfg.app_key), DEFAULT_TIMEOUT)?;
        log::info!("OTAA credentials configured");
        Ok(())
    }

    /// Trigger OTAA join. Waits for JOIN event or timeout.
    pub fn join(&mut self) -> LoraResult<()> {
        let resp = self.engine.send_cmd("AT+JOIN", JOIN_TIMEOUT)?;

        // Check for inline join result
        for line in &resp.lines {
            if line.contains("JOINED") {
                log::info!("Joined LoRaWAN network");
                return Ok(());
            }
            if line.contains("JOIN_FAILED") {
                return Err(LoraError::JoinFailed);
            }
        }

        // Some modules send an async EVT after the OK
        let deadline = std::time::Instant::now() + JOIN_TIMEOUT;
        while std::time::Instant::now() < deadline {
            match self.engine.read_event(Duration::from_secs(1)) {
                Ok(line) => {
                    if line.contains("JOINED") {
                        log::info!("Joined LoRaWAN network (async)");
                        return Ok(());
                    }
                    if line.contains("JOIN_FAILED") {
                        return Err(LoraError::JoinFailed);
                    }
                }
                Err(LoraError::Timeout) => break,
                Err(e) => return Err(e),
            }
        }
        Err(LoraError::Timeout)
    }

    /// Send an uplink payload (raw bytes) on the specified port.
    ///
    /// Returns any received downlink, or None if no downlink arrived.
    pub fn send(
        &mut self,
        port: u8,
        data: &[u8],
        confirm: bool,
    ) -> LoraResult<Option<Downlink>> {
        if data.len() > 242 {
            return Err(LoraError::InvalidParam("payload > 242 bytes".into()));
        }

        // Hex-encode payload
        let hex: String = data.iter().map(|b| format!("{:02X}", b)).collect();

        let cmd = format!("AT+SEND={}:{}:{}", port, confirm as u8, hex);
        let resp = self.engine.send_cmd(&cmd, TX_TIMEOUT)?;

        // Parse downlink from response lines
        for line in &resp.lines {
            if line.contains("+EVT:RX") || line.contains("+RX:") {
                if let Some(dl) = Self::parse_downlink(line, port) {
                    return Ok(Some(dl));
                }
            }
        }
        Ok(None)
    }

    /// Parse a downlink event line into a Downlink struct.
    fn parse_downlink(line: &str, default_port: u8) -> Option<Downlink> {
        // Format: "+EVT:RX_1:AABBCC" or similar — adapt per module dialect
        let hex_part = line.rsplit(':').next()?;
        let data = Self::hex_decode(hex_part)?;
        Some(Downlink { port: default_port, data })
    }

    /// Decode a hex string into bytes, returning None on malformed input.
    fn hex_decode(hex: &str) -> Option<Vec<u8>> {
        if hex.len() % 2 != 0 { return None; }
        (0..hex.len())
            .step_by(2)
            .map(|i| u8::from_str_radix(&hex[i..i + 2], 16).ok())
            .collect()
    }
}
```

### 8.5 Main Application (Rust)

```rust
// src/main.rs
mod at_engine;
mod error;
mod lorawan;

use lorawan::{LoRaWan, OtaaConfig};
use serialport::SerialPort;
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    env_logger::init();

    // Open serial port
    let port = serialport::new("/dev/ttyUSB0", 9600)
        .timeout(Duration::from_millis(10))   // non-blocking reads
        .open()?;

    let mut lora: LoRaWan<Box<dyn SerialPort>> = LoRaWan::new(port);

    // Initialize
    lora.init()?;
    println!("Version: {}", lora.version()?);

    // Configure and join
    let otaa = OtaaConfig {
        dev_eui: "0102030405060708".to_string(),
        app_eui: "0807060504030201".to_string(),
        app_key: "01020304050607080102030405060708".to_string(),
    };
    lora.config_otaa(&otaa)?;

    println!("Joining LoRaWAN network...");
    lora.join()?;
    println!("Joined!");

    // Periodic send loop
    let payload = vec![0xDE, 0xAD, 0xBE, 0xEF];
    loop {
        match lora.send(1, &payload, false)? {
            Some(dl) => {
                println!("Downlink on port {}: {:02X?}", dl.port, dl.data);
            }
            None => println!("Uplink sent, no downlink"),
        }
        std::thread::sleep(Duration::from_secs(30));
    }
}
```

### 8.6 Embedded Rust (no_std, RTIC or Embassy-compatible)

```rust
// For bare-metal embedded targets (STM32, nRF52, etc.) without std
// Uses embedded-hal traits instead of serialport.
#![no_std]
#![no_main]

use embedded_hal::serial::{Read, Write};
use nb::block;

pub struct AtEngineNb<UART> {
    uart: UART,
    buf:  heapless::Vec<u8, 256>,
}

impl<UART, E> AtEngineNb<UART>
where
    UART: Read<u8, Error = E> + Write<u8, Error = E>,
{
    pub fn new(uart: UART) -> Self {
        Self { uart, buf: heapless::Vec::new() }
    }

    pub fn write_cmd(&mut self, cmd: &str) -> Result<(), E> {
        for b in cmd.bytes().chain(b"\r\n".iter().copied()) {
            block!(self.uart.write(b))?;
        }
        Ok(())
    }

    /// Blocking read of one line. 'tick' is called in the wait loop.
    pub fn read_line<F>(&mut self, mut tick: F) -> Option<heapless::String<256>>
    where
        F: FnMut() -> bool,   // returns false to abort (timeout)
    {
        self.buf.clear();
        loop {
            if !tick() { return None; }
            if let Ok(b) = self.uart.read() {
                if b == b'\r' { continue; }
                if b == b'\n' {
                    let s = core::str::from_utf8(&self.buf).ok()?;
                    let mut out = heapless::String::new();
                    out.push_str(s).ok()?;
                    return Some(out);
                }
                let _ = self.buf.push(b);
            }
        }
    }
}
```

---

## 9. Error Handling and Reliability

### 9.1 Retry Strategy

```c
/* C: Retry with exponential back-off */
lora_err_t lora_join_with_retry(const lora_hal_t *hal, int max_attempts)
{
    for (int attempt = 0; attempt < max_attempts; attempt++) {
        lora_err_t err = lora_join(hal);
        if (err == LORA_OK) return LORA_OK;

        /* Exponential back-off: 5s, 10s, 20s, ... capped at 60s */
        uint32_t delay_s = (uint32_t)(5u << attempt);
        if (delay_s > 60u) delay_s = 60u;
        /* platform_delay_s(delay_s); */
    }
    return LORA_ERR_NOJOIN;
}
```

```rust
// Rust: Retry with exponential back-off
pub fn join_with_retry<P: Read + Write>(
    lora: &mut LoRaWan<P>,
    max_attempts: u32,
) -> LoraResult<()> {
    for attempt in 0..max_attempts {
        match lora.join() {
            Ok(()) => return Ok(()),
            Err(LoraError::JoinFailed) | Err(LoraError::Timeout) => {
                let delay_secs = (5u64 << attempt).min(60);
                log::warn!("Join failed (attempt {}), retrying in {}s", attempt + 1, delay_secs);
                std::thread::sleep(Duration::from_secs(delay_secs));
            }
            Err(e) => return Err(e),
        }
    }
    Err(LoraError::JoinFailed)
}
```

### 9.2 RX Buffer Flush Before Command

Always flush the UART RX buffer before sending a new command to discard any stale async events (e.g., late downlink notifications) that could confuse the response parser:

```c
/* C: flush pending RX bytes */
static void uart_flush_rx(const lora_hal_t *hal)
{
    uint8_t dummy;
    while (hal->uart_read_byte(&dummy, 5 /* ms */, hal->ctx))
        ; /* drain */
}
```

### 9.3 State Machine for Production Systems

In production firmware, use an explicit state machine rather than blocking calls:

```
IDLE → SENDING_CMD → WAITING_RESPONSE → PARSING → [OK | ERROR | TIMEOUT]
         ↑_______________________________________________|
```

This allows the MCU to continue doing other work while waiting for LoRa module responses, which is especially important given the multi-second join and TX latencies.

---

## 10. Advanced Topics

### 10.1 LoRa P2P (Point-to-Point) without LoRaWAN

```c
/* Switch to P2P mode and send a raw packet */
lora_err_t lora_p2p_send(const lora_hal_t *hal,
                          uint32_t freq_hz,
                          uint8_t  sf,
                          const uint8_t *data,
                          size_t data_len)
{
    char cmd[64];
    lora_cmd_ok(hal, "AT+NWM=0",   LORA_DEFAULT_TIMEOUT_MS); /* P2P mode  */
    snprintf(cmd, sizeof(cmd), "AT+PFREQ=%lu", (unsigned long)freq_hz);
    lora_cmd_ok(hal, cmd,          LORA_DEFAULT_TIMEOUT_MS);
    snprintf(cmd, sizeof(cmd), "AT+PSF=%u", sf);
    lora_cmd_ok(hal, cmd,          LORA_DEFAULT_TIMEOUT_MS);

    char hex[485] = {0};
    for (size_t i = 0; i < data_len; i++)
        snprintf(&hex[i * 2], 3, "%02X", data[i]);

    snprintf(cmd, sizeof(cmd), "AT+PSEND=%s", hex);
    return lora_cmd_ok(hal, cmd, LORA_TX_TIMEOUT_MS);
}
```

### 10.2 Reading Module ADC / GPIO via AT

Some modules expose internal sensors or GPIO through AT commands:

```bash
AT+BAT          # Read battery voltage (ADC)
AT+GPIO=0,0,0   # Configure GPIO pin 0 as input, read it
```

### 10.3 Over-the-Air Firmware Update (FUOTA)

LoRaWAN 1.0.4+ supports FUOTA via fragmented data block transport. Some modules handle this internally and expose a simple AT interface:

```bash
AT+FUOTA=1      # Enable FUOTA listening
```

### 10.4 Duty Cycle and Regulatory Compliance

In the EU868 band, the 1% duty cycle rule applies per sub-band. Most LoRaWAN modules enforce this in firmware and will return `ERROR(-3)` (radio busy) if you try to transmit too frequently. Your application must handle this by:

- Waiting for the duty cycle window to reset.
- Using adaptive data rate (ADR) to reduce time-on-air.
- Listening for the `ERROR(-3)` response and backing off accordingly.

---

## 11. Summary

| Topic | Key Points |
|-------|-----------|
| **UART Settings** | 8N1, typically 9600 or 115200 baud, 3.3V TTL, no flow control |
| **Command Format** | `AT+CMD[=params]\r\n` → response lines → `OK\r\n` or `ERROR\r\n` |
| **Timeouts** | Simple queries: ~500 ms; Join: ~12 s; TX+RX: ~6 s |
| **OTAA Flow** | Set DevEUI + AppEUI + AppKey → `AT+JOIN` → wait for JOINED event |
| **Send Flow** | `AT+SEND=port:confirm:hexdata` → wait for OK → parse optional downlink |
| **C Design** | HAL abstraction struct + `lora_send_cmd()` + high-level wrappers |
| **Rust Design** | `AtEngine<P: Read+Write>` generic over transport + `LoRaWan` API |
| **Reliability** | Flush RX before send, per-command timeouts, exponential retry on join failure |
| **Duty Cycle** | EU: 1% per sub-band; handle `ERROR(-3)` with back-off |
| **P2P Mode** | `AT+NWM=0` + frequency/SF/BW config + `AT+PSEND` for raw LoRa |

### Design Principles

The AT command interface is deceptively simple but requires careful engineering in production systems. The most important practices are:

1. **Always implement timeouts** — a missing `\r\n` or a module crash will otherwise block your system permanently.
2. **Use a generic transport abstraction** (HAL in C, trait bounds in Rust) so your LoRa driver is testable with mock UART and portable across platforms.
3. **Handle async events** — join results, downlinks, and error conditions can arrive outside the normal command-response window.
4. **Respect duty cycle limits** — especially in EU regions; throttle your TX rate and handle busy errors gracefully.
5. **Use a state machine** for production firmware to avoid blocking the CPU during multi-second LoRa operations.