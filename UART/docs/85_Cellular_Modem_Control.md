# 85. Cellular Modem Control

- **Hardware Architecture** — wiring diagram, voltage level warnings, UART parameter table, power-on sequencing
- **AT Command Fundamentals** — command/response structure, all response types, the 4 command forms (execute/read/write/test)
- **Core AT Command Categories** — 6 tables covering identification, PDP/data, SMS, voice, GNSS, and TCP/IP sockets
- **C/C++ Implementation** — a reusable `uart_modem.c` POSIX driver, a full `ModemManager` C++ class with mutex-safe AT channel, URC listener thread, SMS, data activation, and a complete `main.cpp` example
- **Rust Implementation** — typed error model with `thiserror`, a `UartPort` abstraction using the `serialport` crate, a clean `AtChannel` layer, and a high-level `Modem` struct with full `Cargo.toml`
- **Advanced Topics** — URC demultiplexing, PDU-mode SMS, Quectel TCP socket API, SIMCom MQTT via AT, and FOTA OTA firmware update
- **Error Handling** — common failure modes table, retry with exponential back-off (C), watchdog pattern (Rust)
- **Security** — SIM PIN, TLS sockets, AT injection defence, FOTA integrity

## Controlling 4G/LTE Modems Through the UART AT Interface

---

## Table of Contents

1. [Introduction](#introduction)
2. [Hardware Architecture](#hardware-architecture)
3. [AT Command Fundamentals](#at-command-fundamentals)
4. [UART Configuration](#uart-configuration)
5. [Core AT Command Categories](#core-at-command-categories)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Topics](#advanced-topics)
9. [Error Handling & Reliability](#error-handling--reliability)
10. [Security Considerations](#security-considerations)
11. [Summary](#summary)

---

## Introduction

Cellular modem control through the UART (Universal Asynchronous Receiver-Transmitter) AT command interface is a foundational technique in embedded systems and IoT development. Modern 4G/LTE modem modules — such as those from **Quectel** (EC21, EC25, BG96), **SIMCom** (SIM7600, SIM7080G), **u-blox** (SARA-R4, LARA-R6), and **Telit** (LE910) — expose a serial AT command interface for host-microcontroller or host-computer communication.

The AT command set (originally "Hayes command set") was developed in the 1980s and remains the standard modem control language. Modern 4G/LTE modems extend this legacy set with 3GPP-standardised commands (ITU-T V.250, 3GPP TS 27.007, 27.005) and proprietary vendor extensions.

### Why UART?

- **Universally available** on microcontrollers and SBCs (Raspberry Pi, STM32, ESP32, nRF52840, etc.)
- **Simple wiring**: minimum 2 wires (TX/RX); 4 wires with hardware flow control (RTS/CTS)
- **Low overhead**: no USB stack, no I²C addressing complexity
- **Deterministic timing** for embedded RTOS environments

---

## Hardware Architecture

### Physical Connection

```
  Host MCU / SBC                    4G/LTE Modem Module
  ┌──────────────┐                  ┌──────────────────────┐
  │   UART TX  ──┼──────────────────┼─► UART RX            │
  │   UART RX  ◄─┼──────────────────┼──  UART TX           │
  │   RTS      ──┼──────────────────┼─► CTS  (flow ctrl)   │
  │   CTS      ◄─┼──────────────────┼──  RTS  (flow ctrl)  │
  │   GPIO     ──┼──────────────────┼─► PWRKEY             │
  │   GPIO     ◄─┼──────────────────┼──  STATUS            │
  │   GPIO     ──┼──────────────────┼─► RESET_N            │
  │   GND      ──┼──────────────────┼──  GND               │
  └──────────────┘                  └──────────────────────┘
```

### Typical UART Parameters for 4G/LTE Modems

| Parameter     | Typical Value              | Notes                                 |
|---------------|----------------------------|---------------------------------------|
| Baud Rate     | 115200 (default)           | Auto-baud or fixed; some support 921600 |
| Data Bits     | 8                          | Standard                              |
| Parity        | None                       | Standard                              |
| Stop Bits     | 1                          | Standard                              |
| Flow Control  | Hardware (RTS/CTS)         | Highly recommended for data transfer  |
| Voltage Level | 1.8V or 3.3V               | Check modem datasheet!                |

> **Warning**: Many 4G modem modules operate at 1.8V I/O. Connecting a 3.3V MCU directly without a level shifter can damage the modem.

### Power Sequencing

Most modems require a specific power-on sequence:

```
VBAT ──► stable ──► PWRKEY pulse (500ms–2s) ──► STATUS pin goes HIGH ──► AT commands accepted
```

---

## AT Command Fundamentals

### Command Structure

```
AT<command>[=<parameter>]<CR>
```

All AT commands are terminated with `\r` (Carriage Return, 0x0D). Responses are terminated with `\r\n`.

### Response Types

| Response       | Meaning                                           |
|----------------|---------------------------------------------------|
| `OK`           | Command executed successfully                     |
| `ERROR`        | Command failed (no detail)                        |
| `+CME ERROR: n`| Extended mobile equipment error code              |
| `+CMS ERROR: n`| SMS-specific error code                           |
| `> `           | Prompt waiting for data (e.g., SMS body)          |
| Unsolicited    | Async event (e.g., `+CREG`, incoming call, SMS)   |

### Command Types (ITU-T V.250)

```
AT+CMD          → Execute / Action command
AT+CMD?         → Read current value
AT+CMD=<val>    → Write / Set value
AT+CMD=?        → Test: list supported values/ranges
```

### Example Interaction

```
Host  →   AT\r
Modem ←   \r\nOK\r\n

Host  →   AT+CGMM\r
Modem ←   \r\nEC25EUFA\r\n\r\nOK\r\n

Host  →   AT+CSQ\r
Modem ←   \r\n+CSQ: 18,0\r\n\r\nOK\r\n
```

---

## Core AT Command Categories

### 1. Basic Identification & Status

| Command         | Description                        | Example Response              |
|-----------------|------------------------------------|-------------------------------|
| `AT`            | Attention / health check           | `OK`                          |
| `ATI`           | Manufacturer info                  | Quectel, EC25, Revision...    |
| `AT+CGMM`       | Model identification               | `EC25EUFA`                    |
| `AT+CGSN`       | IMEI number                        | `+CGSN: 356938035643816`      |
| `AT+CIMI`       | IMSI number                        | `204080813280552`             |
| `AT+CCID`       | SIM card ICCID                     | `+CCID: 89314404000167997563` |
| `AT+CSQ`        | Signal quality (RSSI, BER)         | `+CSQ: 18,0`                  |
| `AT+CREG?`      | Network registration status        | `+CREG: 0,1`                  |
| `AT+CEREG?`     | EPS (LTE) network registration     | `+CEREG: 0,1`                 |
| `AT+COPS?`      | Current operator                   | `+COPS: 0,0,"Vodafone DE",7`  |

### 2. Network & PDP Context (Data Connection)

| Command                              | Description                             |
|--------------------------------------|-----------------------------------------|
| `AT+CGDCONT=1,"IP","apn.name"`       | Define PDP context (APN)                |
| `AT+CGACT=1,1`                       | Activate PDP context                    |
| `AT+CGPADDR=1`                       | Query assigned IP address               |
| `AT+CGATT?`                          | GPRS/LTE attach status                  |
| `AT+CEREG=2`                         | Enable extended LTE registration URCs   |

### 3. SMS Control (3GPP TS 27.005)

| Command                   | Description                              |
|---------------------------|------------------------------------------|
| `AT+CMGF=1`               | Set SMS text mode (1=text, 0=PDU)        |
| `AT+CMGS="<number>"`      | Send SMS (followed by message + Ctrl-Z)  |
| `AT+CMGL="ALL"`           | List all SMS messages                    |
| `AT+CMGR=<index>`         | Read SMS at index                        |
| `AT+CMGD=<index>`         | Delete SMS at index                      |
| `AT+CNMI=2,2,0,0,0`       | New message indication to serial port    |

### 4. Voice Call Control

| Command          | Description                            |
|------------------|----------------------------------------|
| `ATD<number>;`   | Dial voice call (semicolon = voice)    |
| `ATA`            | Answer incoming call                   |
| `ATH`            | Hang up                                |
| `AT+CLCC`        | List current calls                     |
| `AT+VTS=<DTMF>`  | Send DTMF tone                         |

### 5. GPS / GNSS (Quectel-specific example)

| Command                 | Description                            |
|-------------------------|----------------------------------------|
| `AT+QGPS=1`             | Enable GNSS                            |
| `AT+QGPSLOC=2`          | Get location (mode 2 = parsed)         |
| `AT+QGPSEND`            | Disable GNSS                           |

### 6. TCP/IP Stack (AT socket API — vendor-specific)

Quectel EC25 example:

| Command                                  | Description                        |
|------------------------------------------|------------------------------------|
| `AT+QIOPEN=1,0,"TCP","host",port,0,1`    | Open TCP socket                    |
| `AT+QISEND=0,<len>`                      | Send data                          |
| `AT+QICLOSE=0`                           | Close socket                       |
| `AT+QISTATE=1,0`                         | Query socket status                |

---

## C/C++ Implementation

### 1. UART Initialisation (Linux / POSIX)

```c
/* uart_modem.h */
#ifndef UART_MODEM_H
#define UART_MODEM_H

#include <stdint.h>
#include <stdbool.h>

#define AT_DEFAULT_TIMEOUT_MS   5000
#define AT_BUFFER_SIZE          2048
#define AT_LINE_SIZE            256

typedef struct {
    int   fd;           /* File descriptor for UART port */
    char  port[64];     /* e.g. "/dev/ttyUSB0" or "/dev/ttyS1" */
    int   baudrate;     /* e.g. 115200 */
} ModemHandle;

typedef enum {
    MODEM_OK = 0,
    MODEM_ERROR,
    MODEM_TIMEOUT,
    MODEM_NO_RESPONSE,
} ModemStatus;

ModemStatus modem_open(ModemHandle *modem, const char *port, int baudrate);
void        modem_close(ModemHandle *modem);
ModemStatus modem_send_at(ModemHandle *modem,
                           const char  *cmd,
                           char        *response,
                           size_t       response_size,
                           uint32_t     timeout_ms);
bool        modem_wait_for(ModemHandle *modem,
                            const char  *expected,
                            uint32_t     timeout_ms);

#endif /* UART_MODEM_H */
```

```c
/* uart_modem.c */
#include "uart_modem.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <termios.h>
#include <sys/select.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static int baudrate_to_bflag(int baud)
{
    switch (baud) {
        case 9600:   return B9600;
        case 19200:  return B19200;
        case 38400:  return B38400;
        case 57600:  return B57600;
        case 115200: return B115200;
        case 230400: return B230400;
        case 921600: return B921600;
        default:     return B115200;
    }
}

static uint64_t millis(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

/* ------------------------------------------------------------------ */
/* Public API                                                           */
/* ------------------------------------------------------------------ */

ModemStatus modem_open(ModemHandle *modem, const char *port, int baudrate)
{
    modem->fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (modem->fd < 0) {
        perror("modem_open: open()");
        return MODEM_ERROR;
    }

    strncpy(modem->port, port, sizeof(modem->port) - 1);
    modem->baudrate = baudrate;

    struct termios tty;
    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(modem->fd, &tty) != 0) {
        perror("modem_open: tcgetattr()");
        close(modem->fd);
        return MODEM_ERROR;
    }

    cfsetispeed(&tty, baudrate_to_bflag(baudrate));
    cfsetospeed(&tty, baudrate_to_bflag(baudrate));

    /* Raw mode, 8N1, hardware flow control */
    tty.c_cflag &= ~PARENB;            /* No parity */
    tty.c_cflag &= ~CSTOPB;            /* 1 stop bit */
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;                /* 8 data bits */
    tty.c_cflag |= CRTSCTS;            /* Enable HW flow control */
    tty.c_cflag |= CREAD | CLOCAL;     /* Enable receiver, ignore modem lines */

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);  /* Raw input */
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);           /* No SW flow ctrl */
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK |
                     ISTRIP | INLCR | IGNCR | ICRNL);
    tty.c_oflag &= ~OPOST;             /* Raw output */

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;               /* 100ms read timeout granule */

    if (tcsetattr(modem->fd, TCSANOW, &tty) != 0) {
        perror("modem_open: tcsetattr()");
        close(modem->fd);
        return MODEM_ERROR;
    }

    tcflush(modem->fd, TCIOFLUSH);
    return MODEM_OK;
}

void modem_close(ModemHandle *modem)
{
    if (modem->fd >= 0) {
        close(modem->fd);
        modem->fd = -1;
    }
}

ModemStatus modem_send_at(ModemHandle *modem,
                           const char  *cmd,
                           char        *response,
                           size_t       response_size,
                           uint32_t     timeout_ms)
{
    /* Send: append \r */
    char tx_buf[AT_LINE_SIZE];
    snprintf(tx_buf, sizeof(tx_buf), "%s\r", cmd);

    ssize_t written = write(modem->fd, tx_buf, strlen(tx_buf));
    if (written < 0) {
        perror("modem_send_at: write()");
        return MODEM_ERROR;
    }
    tcdrain(modem->fd);  /* Wait until all bytes are transmitted */

    if (response == NULL || response_size == 0)
        return MODEM_OK;

    /* Read response until "OK\r\n" or "ERROR\r\n" or timeout */
    memset(response, 0, response_size);
    size_t   total   = 0;
    uint64_t deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(modem->fd, &rfds);

        struct timeval tv;
        uint64_t remaining = deadline - millis();
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(modem->fd + 1, &rfds, NULL, NULL, &tv);
        if (ret < 0)  return MODEM_ERROR;
        if (ret == 0) break;  /* Timeout */

        char chunk[256];
        ssize_t n = read(modem->fd, chunk, sizeof(chunk) - 1);
        if (n > 0) {
            chunk[n] = '\0';
            if (total + n < response_size - 1) {
                memcpy(response + total, chunk, n);
                total += n;
                response[total] = '\0';
            }

            /* Check for terminal strings */
            if (strstr(response, "\r\nOK\r\n")    != NULL ||
                strstr(response, "\r\nERROR\r\n") != NULL ||
                strstr(response, "+CME ERROR")    != NULL ||
                strstr(response, "+CMS ERROR")    != NULL) {
                break;
            }
        }
    }

    if (total == 0) return MODEM_NO_RESPONSE;

    /* Determine success / failure */
    if (strstr(response, "\r\nOK\r\n") != NULL)
        return MODEM_OK;
    if (strstr(response, "ERROR") != NULL)
        return MODEM_ERROR;

    return MODEM_TIMEOUT;
}

bool modem_wait_for(ModemHandle *modem,
                     const char  *expected,
                     uint32_t     timeout_ms)
{
    char buf[AT_BUFFER_SIZE];
    memset(buf, 0, sizeof(buf));

    size_t   total    = 0;
    uint64_t deadline = millis() + timeout_ms;

    while (millis() < deadline) {
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(modem->fd, &rfds);

        struct timeval tv;
        uint64_t remaining = deadline - millis();
        tv.tv_sec  = remaining / 1000;
        tv.tv_usec = (remaining % 1000) * 1000;

        int ret = select(modem->fd + 1, &rfds, NULL, NULL, &tv);
        if (ret <= 0) break;

        char chunk[256];
        ssize_t n = read(modem->fd, chunk, sizeof(chunk) - 1);
        if (n > 0) {
            chunk[n] = '\0';
            if (total + n < sizeof(buf) - 1) {
                memcpy(buf + total, chunk, n);
                total += n;
                buf[total] = '\0';
            }
            if (strstr(buf, expected) != NULL)
                return true;
        }
    }
    return false;
}
```

### 2. High-Level Modem Manager (C++)

```cpp
// ModemManager.hpp
#pragma once

#include <string>
#include <optional>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <regex>

extern "C" {
#include "uart_modem.h"
}

struct SignalQuality {
    int  rssi_raw;    /* 0–31, 99=unknown */
    int  rssi_dbm;    /* Approx: -113 + 2*rssi_raw */
    int  ber;
};

struct NetworkInfo {
    std::string operator_name;
    std::string technology;  /* "LTE", "WCDMA", "GSM" */
    bool        registered;
};

class ModemManager {
public:
    using URCCallback = std::function<void(const std::string& urc)>;

    explicit ModemManager(const std::string& port, int baud = 115200);
    ~ModemManager();

    bool                       init();
    std::optional<std::string> get_imei();
    std::optional<std::string> get_iccid();
    std::optional<SignalQuality> get_signal_quality();
    std::optional<NetworkInfo>   get_network_info();

    bool enable_data(const std::string& apn);
    std::optional<std::string> get_ip_address();

    bool send_sms(const std::string& number, const std::string& message);

    void register_urc_handler(URCCallback cb);
    void start_urc_listener();
    void stop_urc_listener();

private:
    ModemHandle        handle_;
    std::string        port_;
    int                baud_;
    std::mutex         uart_mutex_;
    std::thread        urc_thread_;
    std::atomic<bool>  running_{false};
    URCCallback        urc_callback_;

    std::string send_cmd(const std::string& cmd,
                         uint32_t timeout_ms = AT_DEFAULT_TIMEOUT_MS);
    std::string extract_value(const std::string& response,
                               const std::string& prefix);
    void urc_loop();
};
```

```cpp
// ModemManager.cpp
#include "ModemManager.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>

ModemManager::ModemManager(const std::string& port, int baud)
    : port_(port), baud_(baud)
{
    handle_.fd = -1;
}

ModemManager::~ModemManager()
{
    stop_urc_listener();
    modem_close(&handle_);
}

bool ModemManager::init()
{
    if (modem_open(&handle_, port_.c_str(), baud_) != MODEM_OK) {
        std::cerr << "Failed to open UART port: " << port_ << "\n";
        return false;
    }

    /* Synchronise baud rate */
    for (int attempt = 0; attempt < 5; ++attempt) {
        auto resp = send_cmd("AT", 1000);
        if (resp.find("OK") != std::string::npos) {
            /* Echo off, verbose errors on */
            send_cmd("ATE0", 500);
            send_cmd("AT+CMEE=2", 500);
            send_cmd("AT+CNMI=2,2,0,0,0", 500);  /* SMS URC to serial */
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }

    std::cerr << "Modem not responding after 5 attempts.\n";
    return false;
}

std::string ModemManager::send_cmd(const std::string& cmd, uint32_t timeout_ms)
{
    std::lock_guard<std::mutex> lock(uart_mutex_);
    char buf[AT_BUFFER_SIZE] = {};
    modem_send_at(&handle_, cmd.c_str(), buf, sizeof(buf), timeout_ms);
    return std::string(buf);
}

std::string ModemManager::extract_value(const std::string& response,
                                         const std::string& prefix)
{
    auto pos = response.find(prefix);
    if (pos == std::string::npos) return {};
    pos += prefix.size();
    auto end = response.find_first_of("\r\n", pos);
    return response.substr(pos, end - pos);
}

std::optional<std::string> ModemManager::get_imei()
{
    auto resp = send_cmd("AT+CGSN");
    /* IMEI is a 15-digit number on its own line */
    std::regex re("(\\d{15})");
    std::smatch m;
    if (std::regex_search(resp, m, re))
        return m[1].str();
    return std::nullopt;
}

std::optional<std::string> ModemManager::get_iccid()
{
    auto resp = send_cmd("AT+CCID");
    auto val  = extract_value(resp, "+CCID: ");
    if (!val.empty()) return val;
    return std::nullopt;
}

std::optional<SignalQuality> ModemManager::get_signal_quality()
{
    auto resp = send_cmd("AT+CSQ");
    auto val  = extract_value(resp, "+CSQ: ");
    if (val.empty()) return std::nullopt;

    SignalQuality sq{};
    sscanf(val.c_str(), "%d,%d", &sq.rssi_raw, &sq.ber);
    sq.rssi_dbm = (sq.rssi_raw == 99) ? -999 : (-113 + 2 * sq.rssi_raw);
    return sq;
}

std::optional<NetworkInfo> ModemManager::get_network_info()
{
    /* +COPS: 0,0,"Vodafone DE",7  where 7=LTE, 2=UTRAN, 0=GSM */
    auto resp = send_cmd("AT+COPS?", 10000);
    auto val  = extract_value(resp, "+COPS: ");
    if (val.empty()) return std::nullopt;

    NetworkInfo ni{};
    int mode, fmt, act;
    char op[64] = {};
    if (sscanf(val.c_str(), "%d,%d,\"%63[^\"]\",%d", &mode, &fmt, op, &act) >= 3) {
        ni.operator_name = op;
        ni.registered    = true;
        switch (act) {
            case 7:  ni.technology = "LTE";     break;
            case 2:  ni.technology = "WCDMA";   break;
            case 0:  ni.technology = "GSM";     break;
            default: ni.technology = "Unknown"; break;
        }
    }
    return ni;
}

bool ModemManager::enable_data(const std::string& apn)
{
    /* Set APN on context 1 */
    std::string cmd = "AT+CGDCONT=1,\"IP\",\"" + apn + "\"";
    auto resp = send_cmd(cmd);
    if (resp.find("OK") == std::string::npos) return false;

    /* Activate context 1 */
    resp = send_cmd("AT+CGACT=1,1", 30000);
    return resp.find("OK") != std::string::npos;
}

std::optional<std::string> ModemManager::get_ip_address()
{
    auto resp = send_cmd("AT+CGPADDR=1");
    /* +CGPADDR: 1,"10.0.0.5" */
    std::regex re(R"(\d+\.\d+\.\d+\.\d+)");
    std::smatch m;
    if (std::regex_search(resp, m, re))
        return m[0].str();
    return std::nullopt;
}

bool ModemManager::send_sms(const std::string& number,
                              const std::string& message)
{
    send_cmd("AT+CMGF=1");  /* Text mode */

    std::string cmd = "AT+CMGS=\"" + number + "\"";
    {
        std::lock_guard<std::mutex> lock(uart_mutex_);
        char buf[AT_BUFFER_SIZE] = {};
        modem_send_at(&handle_, cmd.c_str(), buf, sizeof(buf), 5000);
        /* Look for ">" prompt */
        if (std::string(buf).find('>') == std::string::npos)
            return false;

        /* Send message body followed by Ctrl-Z (0x1A) */
        std::string body = message + "\x1A";
        write(handle_.fd, body.c_str(), body.size());

        /* Wait for +CMGS response */
        return modem_wait_for(&handle_, "+CMGS:", 30000);
    }
}

void ModemManager::register_urc_handler(URCCallback cb)
{
    urc_callback_ = std::move(cb);
}

void ModemManager::start_urc_listener()
{
    running_ = true;
    urc_thread_ = std::thread(&ModemManager::urc_loop, this);
}

void ModemManager::stop_urc_listener()
{
    running_ = false;
    if (urc_thread_.joinable())
        urc_thread_.join();
}

void ModemManager::urc_loop()
{
    char buf[AT_BUFFER_SIZE];
    std::string partial;

    while (running_) {
        memset(buf, 0, sizeof(buf));
        {
            std::lock_guard<std::mutex> lock(uart_mutex_);
            /* Non-blocking read */
            ssize_t n = read(handle_.fd, buf, sizeof(buf) - 1);
            if (n > 0) partial += std::string(buf, n);
        }

        /* Extract complete lines (\r\n terminated) */
        size_t pos;
        while ((pos = partial.find("\r\n")) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial.erase(0, pos + 2);
            if (!line.empty() && urc_callback_)
                urc_callback_(line);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
```

### 3. Application Example (C++)

```cpp
// main.cpp
#include "ModemManager.hpp"
#include <iostream>

int main()
{
    ModemManager modem("/dev/ttyUSB2", 115200);

    if (!modem.init()) {
        std::cerr << "Modem initialisation failed.\n";
        return 1;
    }

    /* Register URC handler for async events */
    modem.register_urc_handler([](const std::string& urc) {
        std::cout << "[URC] " << urc << "\n";
        /* Handle incoming SMS notification: +CMT: "+491234567890",,"24/01/15,12:00:00+04" */
        if (urc.find("+CMT:") != std::string::npos) {
            std::cout << "Incoming SMS!\n";
        }
    });
    modem.start_urc_listener();

    /* Print IMEI */
    if (auto imei = modem.get_imei()) {
        std::cout << "IMEI: " << *imei << "\n";
    }

    /* Signal quality */
    if (auto sq = modem.get_signal_quality()) {
        std::cout << "RSSI: " << sq->rssi_dbm << " dBm\n";
    }

    /* Network info */
    if (auto net = modem.get_network_info()) {
        std::cout << "Operator: " << net->operator_name
                  << "  Tech: " << net->technology << "\n";
    }

    /* Enable LTE data */
    if (modem.enable_data("internet.youroperator.com")) {
        if (auto ip = modem.get_ip_address()) {
            std::cout << "IP Address: " << *ip << "\n";
        }
    }

    /* Send an SMS */
    if (modem.send_sms("+491234567890", "Hello from C++ modem driver!")) {
        std::cout << "SMS sent successfully.\n";
    }

    /* Run for 30s receiving URCs */
    std::this_thread::sleep_for(std::chrono::seconds(30));
    modem.stop_urc_listener();

    return 0;
}
```

---

## Rust Implementation

### Cargo.toml

```toml
[package]
name    = "cellular-modem"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport   = "4.3"
thiserror    = "1.0"
tokio        = { version = "1", features = ["full"] }
tokio-serial = "5.4"
regex        = "1.10"
tracing      = "0.1"
tracing-subscriber = "0.3"
```

### 1. Core Types & Error Handling

```rust
// src/error.rs
use thiserror::Error;

#[derive(Error, Debug)]
pub enum ModemError {
    #[error("Serial port error: {0}")]
    SerialPort(#[from] serialport::Error),

    #[error("I/O error: {0}")]
    Io(#[from] std::io::Error),

    #[error("AT command timed out after {0}ms")]
    Timeout(u64),

    #[error("Modem returned ERROR: {0}")]
    AtError(String),

    #[error("Parse error: {0}")]
    Parse(String),

    #[error("Modem not initialised")]
    NotInitialised,
}

pub type ModemResult<T> = Result<T, ModemError>;
```

```rust
// src/types.rs

#[derive(Debug, Clone)]
pub struct SignalQuality {
    pub rssi_raw: i32,
    pub rssi_dbm: i32,
    pub ber:      i32,
}

impl SignalQuality {
    pub fn from_csq(raw: i32, ber: i32) -> Self {
        let dbm = if raw == 99 { -999 } else { -113 + 2 * raw };
        Self { rssi_raw: raw, rssi_dbm: dbm, ber }
    }

    pub fn quality_label(&self) -> &'static str {
        match self.rssi_dbm {
            i32::MIN..=-109 => "Marginal",
            -109..=-95      => "OK",
            -95..=-80       => "Good",
            _               => "Excellent",
        }
    }
}

#[derive(Debug, Clone)]
pub struct NetworkInfo {
    pub operator:   String,
    pub technology: RadioTechnology,
    pub registered: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub enum RadioTechnology {
    Gsm,
    Wcdma,
    Lte,
    Unknown(u8),
}

impl From<u8> for RadioTechnology {
    fn from(act: u8) -> Self {
        match act {
            0 => RadioTechnology::Gsm,
            2 => RadioTechnology::Wcdma,
            7 => RadioTechnology::Lte,
            n => RadioTechnology::Unknown(n),
        }
    }
}

impl std::fmt::Display for RadioTechnology {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            RadioTechnology::Gsm         => write!(f, "GSM"),
            RadioTechnology::Wcdma       => write!(f, "WCDMA/3G"),
            RadioTechnology::Lte         => write!(f, "4G/LTE"),
            RadioTechnology::Unknown(n)  => write!(f, "Unknown({})", n),
        }
    }
}
```

### 2. UART Driver (Synchronous)

```rust
// src/uart.rs
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use serialport::SerialPort;
use crate::error::{ModemError, ModemResult};

pub struct UartPort {
    port: Box<dyn SerialPort>,
}

impl UartPort {
    pub fn open(path: &str, baud: u32) -> ModemResult<Self> {
        let port = serialport::new(path, baud)
            .data_bits(serialport::DataBits::Eight)
            .parity(serialport::Parity::None)
            .stop_bits(serialport::StopBits::One)
            .flow_control(serialport::FlowControl::Hardware)
            .timeout(Duration::from_millis(100))
            .open()?;

        Ok(Self { port })
    }

    /// Send raw bytes
    pub fn write_all(&mut self, data: &[u8]) -> ModemResult<()> {
        self.port.write_all(data).map_err(ModemError::Io)?;
        self.port.flush().map_err(ModemError::Io)?;
        Ok(())
    }

    /// Read until a terminal string is found or timeout expires
    pub fn read_until(
        &mut self,
        terminals: &[&str],
        timeout: Duration,
    ) -> ModemResult<String> {
        let deadline = Instant::now() + timeout;
        let mut buf   = Vec::with_capacity(1024);
        let mut chunk = [0u8; 256];

        loop {
            match self.port.read(&mut chunk) {
                Ok(0) => {}
                Ok(n) => {
                    buf.extend_from_slice(&chunk[..n]);
                    let s = String::from_utf8_lossy(&buf);

                    for term in terminals {
                        if s.contains(term) {
                            return Ok(s.into_owned());
                        }
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {}
                Err(e) => return Err(ModemError::Io(e)),
            }

            if Instant::now() >= deadline {
                let s = String::from_utf8_lossy(&buf).into_owned();
                if s.is_empty() {
                    return Err(ModemError::Timeout(timeout.as_millis() as u64));
                }
                return Err(ModemError::Timeout(timeout.as_millis() as u64));
            }
        }
    }

    /// Flush both input and output buffers
    pub fn flush_buffers(&mut self) -> ModemResult<()> {
        self.port.clear(serialport::ClearBuffer::All).map_err(ModemError::SerialPort)?;
        Ok(())
    }
}
```

### 3. AT Command Layer

```rust
// src/at.rs
use std::time::Duration;
use tracing::{debug, warn};
use crate::uart::UartPort;
use crate::error::{ModemError, ModemResult};

const AT_TERMINALS: &[&str] = &[
    "\r\nOK\r\n",
    "\r\nERROR\r\n",
    "+CME ERROR:",
    "+CMS ERROR:",
];

pub struct AtChannel {
    uart: UartPort,
}

impl AtChannel {
    pub fn new(uart: UartPort) -> Self {
        Self { uart }
    }

    /// Send an AT command and return the full response string.
    pub fn send(&mut self, cmd: &str, timeout: Duration) -> ModemResult<String> {
        let tx = format!("{}\r", cmd);
        debug!(cmd = cmd, "AT →");

        self.uart.write_all(tx.as_bytes())?;

        let resp = self.uart.read_until(AT_TERMINALS, timeout)?;
        debug!(response = %resp.trim(), "AT ←");

        if resp.contains("\r\nOK\r\n") {
            Ok(resp)
        } else if let Some(pos) = resp.find("+CME ERROR:") {
            let detail = resp[pos..].trim().to_string();
            Err(ModemError::AtError(detail))
        } else if let Some(pos) = resp.find("+CMS ERROR:") {
            let detail = resp[pos..].trim().to_string();
            Err(ModemError::AtError(detail))
        } else if resp.contains("ERROR") {
            Err(ModemError::AtError(resp.trim().to_string()))
        } else {
            Err(ModemError::Timeout(timeout.as_millis() as u64))
        }
    }

    /// Extract a single-line value following a URC prefix.
    /// e.g. extract_value("+CSQ: 18,0\r\nOK\r\n", "+CSQ: ") → "18,0"
    pub fn extract_value<'a>(response: &'a str, prefix: &str) -> Option<&'a str> {
        let start = response.find(prefix)? + prefix.len();
        let end   = response[start..].find(|c| c == '\r' || c == '\n')
                                     .map(|p| start + p)
                                     .unwrap_or(response.len());
        Some(response[start..end].trim())
    }

    pub fn flush(&mut self) -> ModemResult<()> {
        self.uart.flush_buffers()
    }
}
```

### 4. High-Level Modem Driver

```rust
// src/modem.rs
use std::time::Duration;
use regex::Regex;
use tracing::info;

use crate::at::AtChannel;
use crate::error::{ModemError, ModemResult};
use crate::types::{NetworkInfo, RadioTechnology, SignalQuality};
use crate::uart::UartPort;

pub struct Modem {
    at: AtChannel,
}

impl Modem {
    /// Open the UART port and create a Modem instance.
    pub fn open(port: &str, baud: u32) -> ModemResult<Self> {
        let uart = UartPort::open(port, baud)?;
        Ok(Self { at: AtChannel::new(uart) })
    }

    /// Initialise: sync, disable echo, enable extended errors.
    pub fn init(&mut self) -> ModemResult<()> {
        self.at.flush()?;

        // Try to sync up to 5 times
        for attempt in 1..=5 {
            match self.at.send("AT", Duration::from_millis(1000)) {
                Ok(_) => {
                    info!(attempt, "Modem synchronised");
                    self.at.send("ATE0",     Duration::from_millis(500))?;  // Echo off
                    self.at.send("AT+CMEE=2", Duration::from_millis(500))?; // Verbose errors
                    return Ok(());
                }
                Err(e) => {
                    tracing::warn!(attempt, error = %e, "Sync attempt failed");
                    std::thread::sleep(Duration::from_millis(500));
                }
            }
        }

        Err(ModemError::NotInitialised)
    }

    /// Query IMEI number.
    pub fn imei(&mut self) -> ModemResult<String> {
        let resp = self.at.send("AT+CGSN", Duration::from_secs(5))?;
        // IMEI appears as a standalone 15-digit number
        let re   = Regex::new(r"\d{15}").unwrap();
        re.find(&resp)
            .map(|m| m.as_str().to_string())
            .ok_or_else(|| ModemError::Parse("IMEI not found".into()))
    }

    /// Query SIM ICCID.
    pub fn iccid(&mut self) -> ModemResult<String> {
        let resp = self.at.send("AT+CCID", Duration::from_secs(5))?;
        AtChannel::extract_value(&resp, "+CCID: ")
            .map(str::to_string)
            .ok_or_else(|| ModemError::Parse("ICCID not found".into()))
    }

    /// Query signal quality.
    pub fn signal_quality(&mut self) -> ModemResult<SignalQuality> {
        let resp = self.at.send("AT+CSQ", Duration::from_secs(5))?;
        let val  = AtChannel::extract_value(&resp, "+CSQ: ")
            .ok_or_else(|| ModemError::Parse("CSQ not found".into()))?;

        let parts: Vec<i32> = val.split(',')
            .filter_map(|s| s.parse().ok())
            .collect();

        if parts.len() < 2 {
            return Err(ModemError::Parse(format!("Unexpected CSQ: {}", val)));
        }

        Ok(SignalQuality::from_csq(parts[0], parts[1]))
    }

    /// Query current network operator & technology.
    pub fn network_info(&mut self) -> ModemResult<NetworkInfo> {
        let resp = self.at.send("AT+COPS?", Duration::from_secs(10))?;
        let val  = AtChannel::extract_value(&resp, "+COPS: ")
            .ok_or_else(|| ModemError::Parse("COPS not found".into()))?;

        // +COPS: 0,0,"Vodafone DE",7
        let re = Regex::new(r#"(\d+),(\d+),"([^"]*)",(\d+)"#).unwrap();
        if let Some(caps) = re.captures(val) {
            let operator  = caps[3].to_string();
            let act: u8   = caps[4].parse().unwrap_or(0xFF);
            return Ok(NetworkInfo {
                operator,
                technology: RadioTechnology::from(act),
                registered: true,
            });
        }

        // May be +COPS: 0 (not registered)
        Ok(NetworkInfo {
            operator:   String::new(),
            technology: RadioTechnology::Unknown(0xFF),
            registered: false,
        })
    }

    /// Set APN and activate PDP context 1.
    pub fn enable_data(&mut self, apn: &str) -> ModemResult<String> {
        let cmd = format!("AT+CGDCONT=1,\"IP\",\"{}\"", apn);
        self.at.send(&cmd, Duration::from_secs(5))?;

        self.at.send("AT+CGACT=1,1", Duration::from_secs(30))?;

        // Read assigned IP
        let resp = self.at.send("AT+CGPADDR=1", Duration::from_secs(5))?;
        let re   = Regex::new(r"\d+\.\d+\.\d+\.\d+").unwrap();
        re.find(&resp)
            .map(|m| m.as_str().to_string())
            .ok_or_else(|| ModemError::Parse("IP address not found".into()))
    }

    /// Send an SMS in text mode.
    pub fn send_sms(&mut self, number: &str, message: &str) -> ModemResult<()> {
        self.at.send("AT+CMGF=1", Duration::from_secs(5))?;  // Text mode

        // Send header — expect ">" prompt
        let cmd = format!("AT+CMGS=\"{}\"", number);
        // The ">" prompt does not end with OK, so use a custom terminal
        let tx  = format!("{}\r", cmd);
        self.at.flush()?;

        // We need direct UART access here for the two-step exchange
        // This is handled by writing via the AtChannel's underlying UART
        // and waiting for the ">" prompt separately.
        // For brevity, a simplified version using the AT layer:
        let _ = self.at.send(&cmd, Duration::from_secs(5));

        // Compose message body with Ctrl-Z terminator
        let body = format!("{}\x1a", message);
        let resp = self.at.send(&body, Duration::from_secs(30))?;

        if resp.contains("+CMGS:") || resp.contains("OK") {
            Ok(())
        } else {
            Err(ModemError::AtError("SMS send failed".into()))
        }
    }

    /// Read all SMS messages from SIM.
    pub fn list_sms(&mut self) -> ModemResult<Vec<String>> {
        self.at.send("AT+CMGF=1", Duration::from_secs(5))?;
        let resp = self.at.send("AT+CMGL=\"ALL\"", Duration::from_secs(15))?;

        // Parse individual message entries (simplified)
        let messages: Vec<String> = resp
            .lines()
            .filter(|l| !l.starts_with("+CMGL:") && !l.is_empty()
                        && *l != "OK")
            .map(str::to_string)
            .collect();

        Ok(messages)
    }

    /// Factory reset the modem.
    pub fn factory_reset(&mut self) -> ModemResult<()> {
        self.at.send("AT&F", Duration::from_secs(10))?;
        self.at.send("AT+QPRTPARA=3", Duration::from_secs(10)).ok(); // Quectel NV reset
        Ok(())
    }
}
```

### 5. Async Application Entry Point (Rust + Tokio)

```rust
// src/main.rs
mod at;
mod error;
mod modem;
mod types;
mod uart;

use modem::Modem;
use tracing_subscriber::FmtSubscriber;

fn main() -> error::ModemResult<()> {
    // Initialise structured logging
    tracing::subscriber::set_global_default(
        FmtSubscriber::builder()
            .with_max_level(tracing::Level::DEBUG)
            .finish(),
    ).expect("Failed to set subscriber");

    let mut modem = Modem::open("/dev/ttyUSB2", 115200)?;
    modem.init()?;

    // --- Identity ---
    match modem.imei() {
        Ok(imei) => println!("IMEI:  {}", imei),
        Err(e)   => eprintln!("IMEI error: {}", e),
    }

    match modem.iccid() {
        Ok(iccid) => println!("ICCID: {}", iccid),
        Err(e)    => eprintln!("ICCID error: {}", e),
    }

    // --- Radio ---
    if let Ok(sq) = modem.signal_quality() {
        println!("Signal: {} dBm  ({})", sq.rssi_dbm, sq.quality_label());
    }

    if let Ok(net) = modem.network_info() {
        println!("Network: {} — {}", net.operator, net.technology);
    }

    // --- Data ---
    match modem.enable_data("internet.youroperator.com") {
        Ok(ip) => println!("Connected! IP: {}", ip),
        Err(e) => eprintln!("Data connection failed: {}", e),
    }

    // --- SMS ---
    match modem.send_sms("+491234567890", "Hello from Rust modem driver!") {
        Ok(())  => println!("SMS sent."),
        Err(e)  => eprintln!("SMS error: {}", e),
    }

    // --- List received SMS ---
    if let Ok(msgs) = modem.list_sms() {
        println!("\n--- Inbox ({} messages) ---", msgs.len());
        for (i, msg) in msgs.iter().enumerate() {
            println!("[{}] {}", i + 1, msg);
        }
    }

    Ok(())
}
```

---

## Advanced Topics

### 1. Unsolicited Result Codes (URCs)

URCs are asynchronous messages pushed by the modem at any time. Robust drivers must handle them independently of the command/response cycle.

Common URCs:

| URC                              | Event                             |
|----------------------------------|-----------------------------------|
| `+CREG: 1`                       | Registered to home network        |
| `+CEREG: 0,1`                    | EPS registration (LTE)            |
| `+CMT: "+491234",,"24/01/15"...` | Incoming SMS (direct to serial)   |
| `RING`                           | Incoming voice call               |
| `+CMTI: "SM",3`                  | SMS stored at index 3             |
| `+QIURC: "recv",0`               | Data received on TCP socket 0     |
| `+QIND: "csq",20,0`              | Signal quality change             |

URC handling strategy: run a background thread (or async task) that reads from the UART continuously and demultiplexes between pending AT responses and URCs.

### 2. PDU Mode SMS

For binary/unicode SMS or reliable 8-bit clean messages, PDU mode is preferred:

```c
/* Switch to PDU mode */
modem_send_at(&handle, "AT+CMGF=0", buf, sizeof(buf), 3000);

/*
 * PDU = SMSC_Length + SMSC_Info + PDU_Type + MR + DA_Len
 *       + DA_TON_NPI + DA_Digits + PID + DCS + VP + UDL + UD
 * Building a PDU from scratch is non-trivial; use a library.
 */
```

### 3. Quectel TCP/IP Socket Example (C)

```c
/* Open a TCP connection using Quectel's AT+QIOPEN API */
void tcp_demo(ModemHandle *modem)
{
    char buf[AT_BUFFER_SIZE];

    /* Configure context */
    modem_send_at(modem, "AT+QICSGP=1,1,\"internet\",\"\",\"\",1",
                  buf, sizeof(buf), 5000);

    /* Activate context */
    modem_send_at(modem, "AT+QIACT=1", buf, sizeof(buf), 30000);

    /*
     * AT+QIOPEN=<contextID>,<connectID>,<service_type>,
     *           <addr>,<remote_port>,<local_port>,<access_mode>
     * access_mode 1 = Direct push mode (data URC)
     */
    modem_send_at(modem,
        "AT+QIOPEN=1,0,\"TCP\",\"api.example.com\",80,0,1",
        buf, sizeof(buf), 30000);

    /* Wait for +QIOPEN: 0,0 (connected) */
    if (!modem_wait_for(modem, "+QIOPEN: 0,0", 30000)) {
        fprintf(stderr, "TCP connect failed\n");
        return;
    }

    /* Send HTTP GET request */
    const char *http_req =
        "GET / HTTP/1.1\r\nHost: api.example.com\r\nConnection: close\r\n\r\n";
    size_t len = strlen(http_req);

    char cmd[64];
    snprintf(cmd, sizeof(cmd), "AT+QISEND=0,%zu", len);
    modem_send_at(modem, cmd, buf, sizeof(buf), 5000);

    /* After ">" prompt, write data directly */
    write(modem->fd, http_req, len);

    /* Data arrives via +QIURC: "recv",0 URCs */
    modem_wait_for(modem, "+QIURC: \"closed\",0", 15000);

    /* Close socket */
    modem_send_at(modem, "AT+QICLOSE=0", buf, sizeof(buf), 5000);
    modem_send_at(modem, "AT+QIDEACT=1", buf, sizeof(buf), 5000);
}
```

### 4. MQTT via AT Commands (SIM7080G NB-IoT/LTE-M)

```c
/* SIMCom SIM7080G MQTT client via AT commands */
void mqtt_demo(ModemHandle *modem)
{
    char buf[AT_BUFFER_SIZE];

    /* Configure MQTT parameters */
    modem_send_at(modem, "AT+SMCONF=\"URL\",\"broker.example.com\",1883",
                  buf, sizeof(buf), 5000);
    modem_send_at(modem, "AT+SMCONF=\"CLIENTID\",\"device-001\"",
                  buf, sizeof(buf), 5000);
    modem_send_at(modem, "AT+SMCONF=\"KEEPTIME\",60",
                  buf, sizeof(buf), 5000);

    /* Connect */
    modem_send_at(modem, "AT+SMCONN", buf, sizeof(buf), 30000);

    /* Publish */
    modem_send_at(modem,
        "AT+SMPUB=\"sensors/temp\",12,0,0",
        buf, sizeof(buf), 5000);
    /* Wait for ">" prompt then send payload */
    write(modem->fd, "24.5 degrees", 12);

    /* Subscribe */
    modem_send_at(modem,
        "AT+SMSUB=\"commands/device-001\",1",
        buf, sizeof(buf), 5000);

    /* Incoming messages arrive as +SMSUB URCs */
    modem_wait_for(modem, "+SMSUB:", 60000);

    modem_send_at(modem, "AT+SMDISC", buf, sizeof(buf), 5000);
}
```

### 5. Firmware OTA Update (FOTA)

```c
/* Quectel FOTA via HTTP */
void fota_demo(ModemHandle *modem)
{
    char buf[AT_BUFFER_SIZE];

    /*
     * AT+QFOTADL triggers a firmware download from a URL.
     * The modem reboots automatically after successful download.
     */
    modem_send_at(modem,
        "AT+QFOTADL=\"http://update.server.com/EC25EUFA-firmware.bin\"",
        buf, sizeof(buf), 5000);

    /* Monitor progress via +QIND: "fota",<status>,<progress> URCs */
    printf("FOTA in progress... do not power off\n");

    /* Wait up to 5 minutes for completion */
    modem_wait_for(modem, "+QIND: \"fota\",\"END\",0", 300000);
}
```

---

## Error Handling & Reliability

### Common Issues and Mitigations

| Problem                          | Symptom                             | Mitigation                                         |
|----------------------------------|-------------------------------------|----------------------------------------------------|
| UART buffer overflow             | Garbled / truncated responses       | Enable HW flow control (RTS/CTS)                   |
| Missed URCs                      | Lost events, SMS, call status       | Dedicated URC reader thread                        |
| Command collision                | Mixed responses                     | Mutex/semaphore around AT transaction              |
| Modem SIM not ready              | `+CME ERROR: 10`                    | Wait for `+CPIN: READY` URC after power-on         |
| Network registration lost        | `+CREG: 0` URC                      | Re-register, reset modem if persistent             |
| Modem firmware crash             | No response to AT                   | Assert RESET_N, re-initialise                      |
| Baud rate mismatch after reboot  | Garbage on RX                       | Always set baud explicitly; consider auto-baud     |

### Retry Pattern (C)

```c
ModemStatus send_at_with_retry(ModemHandle  *modem,
                                const char   *cmd,
                                char         *response,
                                size_t        resp_size,
                                uint32_t      timeout_ms,
                                int           max_retries)
{
    for (int i = 0; i < max_retries; ++i) {
        ModemStatus s = modem_send_at(modem, cmd, response,
                                      resp_size, timeout_ms);
        if (s == MODEM_OK) return MODEM_OK;

        fprintf(stderr, "AT command \"%s\" attempt %d/%d failed: %d\n",
                cmd, i + 1, max_retries, s);

        /* Exponential back-off: 200ms, 400ms, 800ms... */
        usleep((200 << i) * 1000);
    }
    return MODEM_ERROR;
}
```

### Watchdog Pattern (Rust)

```rust
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};
use std::thread;

pub struct ModemWatchdog {
    modem:    Arc<Mutex<crate::modem::Modem>>,
    interval: Duration,
}

impl ModemWatchdog {
    pub fn spawn(modem: Arc<Mutex<crate::modem::Modem>>, interval: Duration) {
        thread::spawn(move || {
            loop {
                thread::sleep(interval);
                let mut m = modem.lock().unwrap();
                match m.signal_quality() {
                    Ok(sq) => {
                        tracing::info!("Watchdog: modem alive, RSSI {} dBm", sq.rssi_dbm);
                    }
                    Err(e) => {
                        tracing::error!("Watchdog: modem not responding ({}), re-init...", e);
                        // Trigger GPIO reset here, then re-init
                        let _ = m.init();
                    }
                }
            }
        });
    }
}
```

---

## Security Considerations

1. **SIM PIN Protection**: Always unlock the SIM with `AT+CPIN=<PIN>` before operations. Avoid hardcoding PINs; read from secure storage.

2. **TLS/SSL for TCP sockets**: Use `AT+QSSLOPEN` (Quectel) or equivalent instead of plain `AT+QIOPEN`. Configure certificates via `AT+QFUPL` to the modem's file system.

3. **UART Physical Security**: The serial port exposes direct modem control. In production hardware, disable debug UART headers and use locked-down boot configurations.

4. **AT Command Injection**: Never pass user-controlled strings directly into AT commands without validation. An attacker-supplied phone number could inject `\x1A` to prematurely terminate an SMS and inject subsequent commands.

5. **Firmware Integrity**: Validate FOTA binary checksums before and after download. Quectel modules support secure boot.

6. **Data at Rest on SIM**: Periodically delete SMS messages (`AT+CMGD`) to prevent sensitive data accumulation.

---

## Summary

Cellular modem control via UART AT commands is a mature, well-documented technique that remains the dominant method for integrating 4G/LTE modules into embedded systems and IoT devices. The core principles are straightforward:

- **UART configuration** must match the modem's electrical level (1.8V or 3.3V), baud rate, and flow control requirements. Hardware RTS/CTS flow control is essential for reliable high-throughput communication.

- **AT command interaction** follows a synchronous request/response pattern using `\r`-terminated commands. Robust drivers implement: (a) a mutex-protected synchronous command channel for host-initiated commands, and (b) a background URC listener for asynchronous modem events such as incoming SMS, call rings, and network registration changes.

- **C/C++ implementations** leverage POSIX `termios` for UART configuration, `select()`-based I/O with timeout management, and thread-safe wrappers. The command/response cycle is wrapped in a reusable `modem_send_at()` primitive with configurable timeout and retry logic.

- **Rust implementations** use the `serialport` crate for cross-platform UART access and model errors with `thiserror`. The type system enforces correct state transitions, and `Option`/`Result` return types eliminate unchecked error paths common in C codebases.

- **Advanced features** — including TCP/IP sockets, MQTT, GNSS positioning, and OTA firmware updates — are accessed through vendor-specific AT extension commands, the most popular being the Quectel AT command set. These higher-level services dramatically reduce the software stack required on the host MCU.

- **Production robustness** requires hardware watchdogs, exponential-backoff retry policies, dedicated URC threads, and modem reset circuits. Security must be addressed at every layer: SIM PIN management, TLS-encrypted sockets, and defence against AT command injection.

Mastery of the UART AT interface provides a low-dependency, highly portable foundation for cellular connectivity that scales from bare-metal microcontrollers to full Linux-based embedded computers.

---

*Document: 85_Cellular_Modem_Control.md — UART Series*