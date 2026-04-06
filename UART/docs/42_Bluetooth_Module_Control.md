# 42. Bluetooth Module Control via UART

- **Introduction** — AT command protocol concept, Classic vs BLE, SPP/GATT overview
- **Hardware Overview** — wiring diagram, voltage level table for HC-05/HC-06/HM-10/JDY-08/ESP32
- **AT Command Protocol** — command format, timing rules, command mode vs data mode
- **Common Modules** — full AT command tables for HC-05, HC-06, and HM-10
- **UART Configuration** — default settings comparison table

**C/C++ Code Examples:**
1. `uart_bt.h/.c` — STM32 HAL driver with ring buffer + IRQ, `send_cmd()`, GPIO control
2. `HC05Configurator` C++ class — typed API with lambda-injected UART, `std::optional` returns
3. HM-10 Arduino/AVR example — temperature BLE beacon

**Rust Code Examples:**
1. `AtEngine<S, D>` — `no_std`, `embedded-hal`-generic AT engine with fixed buffers
2. `Hc05<S, D>` — typed HC-05 driver using `core::fmt::Write` for command building
3. Async Rust with `tokio-serial` — for Linux/Raspberry Pi
4. `Hm10<S, D>` — BLE peripheral/central driver

**Advanced Topics:** Master mode scanning, baud auto-detection, non-blocking state machine, GATT characteristic discovery

**Troubleshooting table + Summary** with all key takeaways.

## AT Command Communication with BLE/Classic Modules

---

## Table of Contents

1. [Introduction](#introduction)
2. [Hardware Overview](#hardware-overview)
3. [AT Command Protocol](#at-command-protocol)
4. [Common Bluetooth Modules](#common-bluetooth-modules)
5. [UART Configuration](#uart-configuration)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Topics](#advanced-topics)
9. [Troubleshooting](#troubleshooting)
10. [Summary](#summary)

---

## Introduction

Bluetooth modules controlled via UART AT commands represent one of the most
widespread approaches to adding wireless connectivity to embedded systems.
Rather than implementing Bluetooth functionality in firmware directly, these
modules encapsulate the Bluetooth stack in dedicated hardware and expose a
simple serial (UART) interface using the well-established **AT command** (Hayes
command set) protocol.

This approach allows microcontrollers — even simple 8-bit AVRs — to leverage
Bluetooth Classic (BR/EDR) or Bluetooth Low Energy (BLE) without dealing with
RF stack complexity. The host MCU communicates at TTL or RS-232 voltage levels,
sending ASCII text commands and parsing responses.

### Key Concepts

- **AT Commands**: Text-based commands starting with `AT` (from "attention"),
  terminated with `\r\n`
- **UART**: Universal Asynchronous Receiver/Transmitter — the physical serial
  link between MCU and module
- **Classic Bluetooth (BR/EDR)**: Higher throughput (up to ~3 Mbps), suited for
  audio, file transfer
- **BLE (Bluetooth Low Energy)**: Lower power, lower throughput, suited for
  sensors and beacons
- **SPP (Serial Port Profile)**: Emulates a serial cable over Bluetooth Classic
- **GATT (Generic Attribute Profile)**: BLE data exchange model using
  characteristics

---

## Hardware Overview

### Typical Connection Diagram

```
  MCU                        BT Module (e.g. HC-05)
  ┌──────────┐               ┌──────────────────┐
  │  TX (PA9)├──────────────►│ RX               │
  │  RX (PA10│◄──────────────┤ TX               │
  │  GPIO_OUT├──────────────►│ EN/KEY (CMD mode) │
  │  GPIO_IN ◄───────────────┤ STATE (connected) │
  │  3.3V/5V ├──────────────►│ VCC              │
  │  GND     ├──────────────►│ GND              │
  └──────────┘               └──────────────────┘
```

### Voltage Level Considerations

Most Bluetooth modules (HC-05, HC-06) operate at **3.3 V logic** internally but
have 5 V-tolerant RX pins. However, the TX output is 3.3 V and must not be
applied directly to a 5 V-only MCU input without a level shifter. Always consult
the module datasheet:

| Module | VCC     | Logic Level | Notes                          |
|--------|---------|-------------|--------------------------------|
| HC-05  | 3.6–6 V | 3.3 V       | 5 V tolerant RX on most boards |
| HC-06  | 3.6–6 V | 3.3 V       | Slave-only, simpler AT set     |
| HM-10  | 3.3 V   | 3.3 V       | BLE only, rich AT set          |
| JDY-08 | 3.3 V   | 3.3 V       | BLE, iBeacon support           |
| ESP32  | 3.3 V   | 3.3 V       | Full BT + BLE via AT firmware  |

---

## AT Command Protocol

### Command Structure

All AT commands follow this format:

```
AT[+<command>[=<parameter>]]\r\n
```

Responses are typically:

```
OK\r\n
```
or
```
+COMMAND:value\r\n
OK\r\n
```
or on error:
```
ERROR\r\n
```

### Timing Rules

- Commands must be sent as complete lines ending with `\r\n`
- A minimum inter-command delay of **100 ms** is recommended
- Some commands (reset, pairing) require longer timeouts (up to 5 s)
- During data mode (after connection), AT commands are not interpreted — the
  module must be switched back to command mode first

### Command Mode vs. Data Mode

Most Classic Bluetooth modules (e.g., HC-05) have two modes:

- **Command Mode**: AT commands are processed; no data is forwarded. Activated by
  holding the KEY/EN pin HIGH before power-on (or pressing the small button).
- **Data Mode**: Transparent UART bridge; all bytes go through Bluetooth. AT
  commands are ignored.

BLE modules (e.g., HM-10) often use a special escape sequence (`+++`) to toggle
between modes, or switch automatically when a BLE connection drops.

---

## Common Bluetooth Modules

### HC-05 (Classic Bluetooth, Master/Slave)

The HC-05 is the most common hobbyist Bluetooth module. It supports both master
and slave roles and uses SPP for transparent serial communication.

**Key AT Commands (HC-05):**

| Command               | Description                          | Example Response     |
|-----------------------|--------------------------------------|----------------------|
| `AT`                  | Test communication                   | `OK`                 |
| `AT+VERSION?`         | Firmware version                     | `+VERSION:2.0-20100601` |
| `AT+NAME?`            | Get device name                      | `+NAME:HC-05`        |
| `AT+NAME=MyDevice`    | Set device name                      | `OK`                 |
| `AT+PSWD?`            | Get pairing PIN                      | `+PSWD:1234`         |
| `AT+PSWD=0000`        | Set pairing PIN                      | `OK`                 |
| `AT+UART?`            | Get baud,stop,parity                 | `+UART:9600,0,0`     |
| `AT+UART=115200,0,0`  | Set baud rate                        | `OK`                 |
| `AT+ROLE?`            | Get role (0=slave,1=master)          | `+ROLE:0`            |
| `AT+ROLE=1`           | Set as master                        | `OK`                 |
| `AT+RESET`            | Soft reset                           | `OK`                 |
| `AT+ORGL`             | Restore factory defaults             | `OK`                 |
| `AT+STATE?`           | Connection state                     | `+STATE:CONNECTED`   |
| `AT+ADDR?`            | Get BT MAC address                   | `+ADDR:1234:56:ABCDEF` |
| `AT+INQ`              | Inquiry scan (master mode)           | `+INQ:...`           |
| `AT+LINK=addr`        | Connect to address (master mode)     | `OK`                 |

### HC-06 (Classic Bluetooth, Slave Only)

HC-06 is a simplified slave-only variant. It **does not** require a special mode
pin — AT commands are always active when no connection is established. The
command set is smaller.

**Key AT Commands (HC-06):**

| Command              | Description                 |
|----------------------|-----------------------------|
| `AT`                 | Test                        |
| `AT+NAMEmyname`      | Set name (no `=` sign!)     |
| `AT+PIN1234`         | Set PIN (no `=` sign!)      |
| `AT+BAUD8`           | Set baud (1–8 codes)        |
| `AT+VERSION`         | Firmware version            |

> **Note:** HC-06 commands do **not** use `=` for parameter assignment.

### HM-10 (BLE 4.0)

The HM-10 (and clones) is the most popular BLE module for hobby use. It exposes
a GATT UART service and is compatible with iOS and Android BLE apps.

**Key AT Commands (HM-10):**

| Command               | Description                          |
|-----------------------|--------------------------------------|
| `AT`                  | Test (responds `OK`)                 |
| `AT+NAME?`            | Get name                             |
| `AT+NAMEmyname`       | Set name                             |
| `AT+ADDR?`            | Get BLE MAC                          |
| `AT+BAUD?`            | Get baud rate index                  |
| `AT+BAUD0`            | Set 9600 baud                        |
| `AT+ROLE0`            | Slave (peripheral) mode              |
| `AT+ROLE1`            | Master (central) mode                |
| `AT+RESET`            | Soft reset                           |
| `AT+RENEW`            | Factory reset                        |
| `AT+CONN1`            | Connect to last device               |
| `AT+DISC?`            | Discover BLE peripherals             |
| `AT+IMME1`            | Don't auto-connect on power-up       |
| `AT+NOTI1`            | Enable connection notifications      |
| `AT+PIO11`            | Set PIO1 pin high (LED control)      |

---

## UART Configuration

### Typical Settings

| Parameter  | HC-05 Default | HC-06 Default | HM-10 Default |
|------------|---------------|---------------|---------------|
| Baud Rate  | 38400 (AT mode) / 9600 (data mode) | 9600 | 9600 |
| Data Bits  | 8             | 8             | 8             |
| Parity     | None          | None          | None          |
| Stop Bits  | 1             | 1             | 1             |
| Flow Ctrl  | None          | None          | None          |

> **Important:** The HC-05 uses **38400 baud** in AT command mode and **9600
> baud** in data mode by default. The HM-10 uses **9600 baud** for AT commands
> but this can be changed.

---

## C/C++ Implementation

### 1. Basic UART Setup (STM32 HAL / bare-metal)

```c
// uart_bt.h
#ifndef UART_BT_H
#define UART_BT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define BT_UART         USART1
#define BT_BAUD_AT      38400
#define BT_BAUD_DATA    9600
#define BT_RX_BUF_SIZE  256
#define BT_TX_BUF_SIZE  256
#define BT_CMD_TIMEOUT_MS 2000
#define BT_LONG_TIMEOUT_MS 5000

typedef enum {
    BT_OK = 0,
    BT_ERR_TIMEOUT,
    BT_ERR_NACK,
    BT_ERR_OVERFLOW,
} bt_status_t;

bt_status_t bt_init(uint32_t baud);
bt_status_t bt_send_cmd(const char *cmd, char *resp, size_t resp_len, uint32_t timeout_ms);
bt_status_t bt_set_name(const char *name);
bt_status_t bt_set_pin(const char *pin);
bt_status_t bt_set_baud(uint32_t baud);
bool        bt_is_connected(void);
bt_status_t bt_send_data(const uint8_t *data, size_t len);
size_t      bt_receive_data(uint8_t *buf, size_t max_len);

#endif /* UART_BT_H */
```

```c
// uart_bt.c  — STM32 HAL-based implementation
#include "uart_bt.h"
#include "stm32f4xx_hal.h"
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------
 * Internal state
 * ----------------------------------------------------------------- */
static UART_HandleTypeDef huart1;
static uint8_t rx_buf[BT_RX_BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

/* GPIO for KEY (command mode) and STATE (connected indicator) */
#define BT_KEY_PORT     GPIOB
#define BT_KEY_PIN      GPIO_PIN_0
#define BT_STATE_PORT   GPIOB
#define BT_STATE_PIN    GPIO_PIN_1

/* -------------------------------------------------------------------
 * Low-level helpers
 * ----------------------------------------------------------------- */
static void uart_gpio_init(void)
{
    __HAL_RCC_USART1_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {0};

    /* TX: PA9 */
    gpio.Pin       = GPIO_PIN_9;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_NOPULL;
    gpio.Speed     = GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = GPIO_AF7_USART1;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* RX: PA10 */
    gpio.Pin  = GPIO_PIN_10;
    gpio.Mode = GPIO_MODE_AF_PP;
    HAL_GPIO_Init(GPIOA, &gpio);

    /* KEY pin (output) */
    gpio.Pin   = BT_KEY_PIN;
    gpio.Mode  = GPIO_MODE_OUTPUT_PP;
    gpio.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(BT_KEY_PORT, &gpio);

    /* STATE pin (input) */
    gpio.Pin  = BT_STATE_PIN;
    gpio.Mode = GPIO_MODE_INPUT;
    gpio.Pull = GPIO_PULLDOWN;
    HAL_GPIO_Init(BT_STATE_PORT, &gpio);
}

static bt_status_t uart_reconfigure(uint32_t baud)
{
    huart1.Instance          = BT_UART;
    huart1.Init.BaudRate     = baud;
    huart1.Init.WordLength   = UART_WORDLENGTH_8B;
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_TX_RX;
    huart1.Init.HwFlowCtl    = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;

    if (HAL_UART_Init(&huart1) != HAL_OK) return BT_ERR_TIMEOUT;

    /* Enable RXNE interrupt */
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);

    return BT_OK;
}

/* USART1 ISR — store received bytes in circular buffer */
void USART1_IRQHandler(void)
{
    if (__HAL_UART_GET_FLAG(&huart1, UART_FLAG_RXNE)) {
        uint8_t byte = (uint8_t)(huart1.Instance->DR & 0xFF);
        uint16_t next = (rx_head + 1) % BT_RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = byte;
            rx_head = next;
        }
        /* overflow: oldest byte silently dropped */
    }
}

/* -------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------- */

bt_status_t bt_init(uint32_t baud)
{
    uart_gpio_init();
    return uart_reconfigure(baud);
}

/**
 * @brief  Send an AT command and wait for a response line containing
 *         "OK" or "ERROR".
 *
 * @param  cmd        Null-terminated command string (without \r\n)
 * @param  resp       Buffer to store the full response
 * @param  resp_len   Size of resp buffer
 * @param  timeout_ms Maximum wait time in milliseconds
 */
bt_status_t bt_send_cmd(const char *cmd, char *resp,
                         size_t resp_len, uint32_t timeout_ms)
{
    /* Flush RX ring buffer */
    rx_head = rx_tail = 0;

    /* Transmit command + CRLF */
    char tx[128];
    snprintf(tx, sizeof(tx), "%s\r\n", cmd);
    HAL_UART_Transmit(&huart1, (uint8_t *)tx, strlen(tx), 1000);

    /* Collect response until "OK" or "ERROR" found, or timeout */
    uint32_t start = HAL_GetTick();
    size_t   pos   = 0;

    if (resp && resp_len > 0) resp[0] = '\0';

    while ((HAL_GetTick() - start) < timeout_ms) {
        while (rx_tail != rx_head) {
            uint8_t c = rx_buf[rx_tail];
            rx_tail = (rx_tail + 1) % BT_RX_BUF_SIZE;

            if (resp && pos < resp_len - 1) {
                resp[pos++] = (char)c;
                resp[pos]   = '\0';
            }

            /* Check for terminal markers */
            if (pos >= 2) {
                if (strstr(resp, "OK"))    return BT_OK;
                if (strstr(resp, "ERROR")) return BT_ERR_NACK;
            }
        }
    }
    return BT_ERR_TIMEOUT;
}

bt_status_t bt_set_name(const char *name)
{
    char cmd[64];
    char resp[64];
    snprintf(cmd, sizeof(cmd), "AT+NAME=%s", name);
    return bt_send_cmd(cmd, resp, sizeof(resp), BT_CMD_TIMEOUT_MS);
}

bt_status_t bt_set_pin(const char *pin)
{
    char cmd[32];
    char resp[32];
    snprintf(cmd, sizeof(cmd), "AT+PSWD=%s", pin);
    return bt_send_cmd(cmd, resp, sizeof(resp), BT_CMD_TIMEOUT_MS);
}

bt_status_t bt_set_baud(uint32_t baud)
{
    char cmd[48];
    char resp[32];
    /* Format: AT+UART=<baud>,<stop>,<parity> */
    snprintf(cmd, sizeof(cmd), "AT+UART=%lu,0,0", (unsigned long)baud);
    bt_status_t st = bt_send_cmd(cmd, resp, sizeof(resp), BT_CMD_TIMEOUT_MS);
    if (st == BT_OK) {
        /* Reset then re-init at new baud after module restarts */
        bt_send_cmd("AT+RESET", resp, sizeof(resp), BT_LONG_TIMEOUT_MS);
        HAL_Delay(1500);
        uart_reconfigure(baud);
    }
    return st;
}

bool bt_is_connected(void)
{
    return HAL_GPIO_ReadPin(BT_STATE_PORT, BT_STATE_PIN) == GPIO_PIN_SET;
}

bt_status_t bt_send_data(const uint8_t *data, size_t len)
{
    if (HAL_UART_Transmit(&huart1, (uint8_t *)data, len, 1000) == HAL_OK)
        return BT_OK;
    return BT_ERR_TIMEOUT;
}

size_t bt_receive_data(uint8_t *buf, size_t max_len)
{
    size_t count = 0;
    while (rx_tail != rx_head && count < max_len) {
        buf[count++] = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % BT_RX_BUF_SIZE;
    }
    return count;
}
```

### 2. High-Level HC-05 Configuration Utility (C++)

```cpp
// hc05_configurator.hpp
#pragma once
#include <string>
#include <functional>
#include <optional>

/**
 * @brief HC-05 Bluetooth module configurator.
 *
 * Wraps the low-level UART driver and provides typed accessors
 * for common HC-05 settings. Intended for use during a one-time
 * configuration routine (module in AT command mode).
 */
class HC05Configurator {
public:
    using UartSend    = std::function<void(const std::string&)>;
    using UartReceive = std::function<std::string(uint32_t timeout_ms)>;

    struct Config {
        std::string name    = "HC-05";
        std::string pin     = "1234";
        uint32_t    baud    = 9600;
        uint8_t     stop    = 0;   // 0=1 stop bit, 1=2 stop bits
        uint8_t     parity  = 0;   // 0=none, 1=odd, 2=even
        uint8_t     role    = 0;   // 0=slave, 1=master
    };

    HC05Configurator(UartSend send_fn, UartReceive recv_fn)
        : send_(send_fn), recv_(recv_fn) {}

    bool test() {
        return send_command("AT").find("OK") != std::string::npos;
    }

    std::optional<std::string> get_version() {
        auto r = send_command("AT+VERSION?");
        auto p = r.find("+VERSION:");
        if (p == std::string::npos) return std::nullopt;
        return r.substr(p + 9, r.find('\r', p) - p - 9);
    }

    std::optional<std::string> get_address() {
        auto r = send_command("AT+ADDR?");
        auto p = r.find("+ADDR:");
        if (p == std::string::npos) return std::nullopt;
        return r.substr(p + 6, r.find('\r', p) - p - 6);
    }

    bool configure(const Config& cfg) {
        if (!set_name(cfg.name))   return false;
        if (!set_pin(cfg.pin))     return false;
        if (!set_baud(cfg.baud, cfg.stop, cfg.parity)) return false;
        if (!set_role(cfg.role))   return false;
        reset();
        return true;
    }

    bool set_name(const std::string& name) {
        return ok(send_command("AT+NAME=" + name));
    }

    bool set_pin(const std::string& pin) {
        return ok(send_command("AT+PSWD=" + pin));
    }

    bool set_baud(uint32_t baud, uint8_t stop = 0, uint8_t parity = 0) {
        return ok(send_command(
            "AT+UART=" + std::to_string(baud) + "," +
            std::to_string(stop) + "," + std::to_string(parity)));
    }

    bool set_role(uint8_t role /* 0=slave, 1=master */) {
        return ok(send_command("AT+ROLE=" + std::to_string(role)));
    }

    bool restore_defaults() {
        return ok(send_command("AT+ORGL"));
    }

    void reset() {
        send_command("AT+RESET");
    }

private:
    UartSend    send_;
    UartReceive recv_;

    std::string send_command(const std::string& cmd,
                              uint32_t timeout_ms = 2000) {
        send_(cmd + "\r\n");
        return recv_(timeout_ms);
    }

    static bool ok(const std::string& resp) {
        return resp.find("OK") != std::string::npos;
    }
};
```

```cpp
// main_configure.cpp — example usage
#include "hc05_configurator.hpp"
#include "uart_bt.h"   // low-level driver from above
#include <cstdio>

// Adapt the C driver to C++ lambdas
static std::string uart_receive_string(uint32_t timeout_ms) {
    uint8_t buf[256];
    // Poll for up to timeout_ms
    uint32_t start = HAL_GetTick();
    size_t pos = 0;
    while ((HAL_GetTick() - start) < timeout_ms) {
        size_t n = bt_receive_data(buf + pos, sizeof(buf) - pos - 1);
        pos += n;
        buf[pos] = '\0';
        if (strstr((char*)buf, "OK") || strstr((char*)buf, "ERROR"))
            break;
    }
    return std::string((char*)buf, pos);
}

int main(void) {
    HAL_Init();
    SystemClock_Config();

    /* Enter AT command mode: hold KEY high, then power module */
    HAL_GPIO_WritePin(BT_KEY_PORT, BT_KEY_PIN, GPIO_PIN_SET);
    bt_init(38400);
    HAL_Delay(500);

    HC05Configurator cfg(
        [](const std::string& s) {
            bt_send_data((const uint8_t*)s.c_str(), s.size());
        },
        uart_receive_string
    );

    if (!cfg.test()) {
        printf("Module not responding!\n");
        return 1;
    }

    if (auto ver = cfg.get_version(); ver.has_value())
        printf("Firmware: %s\n", ver->c_str());

    HC05Configurator::Config my_config {
        .name   = "MyRobot",
        .pin    = "4321",
        .baud   = 115200,
        .role   = 0,   // slave
    };

    if (cfg.configure(my_config))
        printf("Configuration successful!\n");
    else
        printf("Configuration failed.\n");

    /* Release KEY — module will restart in data mode */
    HAL_GPIO_WritePin(BT_KEY_PORT, BT_KEY_PIN, GPIO_PIN_RESET);
    bt_init(115200);  // now use data-mode baud

    /* Main loop: transparent bridge */
    while (true) {
        uint8_t buf[64];
        size_t n = bt_receive_data(buf, sizeof(buf));
        if (n > 0) {
            /* Echo back */
            bt_send_data(buf, n);
        }
    }
}
```

### 3. HM-10 BLE Module (C, Arduino-compatible)

```c
/*
 * hm10_ble.c — HM-10 BLE module driver for Arduino/AVR
 *
 * Uses SoftwareSerial on AVR or Serial1 on boards with multiple UARTs.
 * For bare-metal AVR, substitute with direct USART register access.
 */

#include <Arduino.h>
#include <SoftwareSerial.h>

SoftwareSerial btSerial(10, 11); // RX, TX

#define BT_BAUD        9600
#define CMD_TIMEOUT_MS 1500

/* ------------------------------------------------------------------ */
/* Primitive: send command, return response string                     */
/* ------------------------------------------------------------------ */
String hm10_cmd(const String& cmd, uint32_t timeout_ms = CMD_TIMEOUT_MS) {
    btSerial.print(cmd);     /* HM-10 does NOT need \r\n for most cmds */
    String resp = "";
    unsigned long t = millis();
    while (millis() - t < timeout_ms) {
        while (btSerial.available())
            resp += (char)btSerial.read();
        if (resp.indexOf("OK") >= 0 || resp.indexOf("ERROR") >= 0)
            break;
    }
    return resp;
}

/* ------------------------------------------------------------------ */
/* Setup and configuration                                             */
/* ------------------------------------------------------------------ */
void hm10_begin() {
    btSerial.begin(BT_BAUD);
    delay(200);
}

bool hm10_test() {
    return hm10_cmd("AT").indexOf("OK") >= 0;
}

bool hm10_set_name(const String& name) {
    return hm10_cmd("AT+NAME" + name).indexOf("OK") >= 0;
}

bool hm10_set_role(uint8_t role) {
    /* 0 = peripheral (slave), 1 = central (master) */
    return hm10_cmd("AT+ROLE" + String(role)).indexOf("OK") >= 0;
}

bool hm10_enable_notifications() {
    /* AT+NOTI1: module sends "OK+CONN" / "OK+LOST" on connect/disconnect */
    return hm10_cmd("AT+NOTI1").indexOf("OK") >= 0;
}

String hm10_get_addr() {
    String r = hm10_cmd("AT+ADDR?");
    int p = r.indexOf("OK+ADDR:");
    if (p < 0) return "";
    return r.substring(p + 8, p + 20);
}

bool hm10_reset() {
    return hm10_cmd("AT+RESET", 3000).indexOf("OK") >= 0;
}

/* ------------------------------------------------------------------ */
/* Data mode: send / receive transparent BLE data                      */
/* ------------------------------------------------------------------ */
void hm10_send(const uint8_t* data, size_t len) {
    btSerial.write(data, len);
}

size_t hm10_receive(uint8_t* buf, size_t max_len) {
    size_t n = 0;
    while (btSerial.available() && n < max_len)
        buf[n++] = btSerial.read();
    return n;
}

/* ------------------------------------------------------------------ */
/* Example: temperature sensor BLE beacon                             */
/* ------------------------------------------------------------------ */
void setup() {
    Serial.begin(115200);
    hm10_begin();

    if (!hm10_test()) {
        Serial.println("HM-10 not found!");
        return;
    }

    hm10_set_name("TempSensor1");
    hm10_enable_notifications();
    hm10_reset();
    delay(1500);  // wait for reboot

    Serial.println("HM-10 ready. Advertising as 'TempSensor1'");
}

float read_temperature_c() {
    /* Placeholder: read from ADC/I2C sensor */
    return 23.5f;
}

void loop() {
    /* Send temperature reading over BLE every 2 s */
    char payload[32];
    float temp = read_temperature_c();
    snprintf(payload, sizeof(payload), "TEMP:%.1f\n", temp);
    hm10_send((uint8_t*)payload, strlen(payload));

    /* Echo any received commands back to Serial monitor */
    uint8_t buf[64];
    size_t n = hm10_receive(buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        Serial.print("BLE RX: ");
        Serial.println((char*)buf);
    }

    delay(2000);
}
```

---

## Rust Implementation

### 1. Platform-Agnostic AT Command Engine (no_std)

```rust
// at_engine.rs — no_std, no_alloc AT command engine using embedded-hal
#![no_std]

use embedded_hal::serial::{Read, Write};
use nb::block;

/// Maximum length of a single AT response line
pub const MAX_RESP_LEN: usize = 128;
/// Maximum AT command string length
pub const MAX_CMD_LEN: usize = 64;

#[derive(Debug, PartialEq)]
pub enum AtError {
    Timeout,
    Nack,
    Overflow,
    SerialError,
}

/// Result of an AT command send
pub type AtResult<T> = Result<T, AtError>;

/// Minimal time-provider trait (implement with your HAL's tick counter)
pub trait DelayMs {
    fn delay_ms(&mut self, ms: u32);
    fn elapsed_ms(&self) -> u32;
}

/// Core AT command engine parameterised over an embedded-hal serial port
pub struct AtEngine<S, D> {
    serial: S,
    delay:  D,
}

impl<S, D> AtEngine<S, D>
where
    S: Read<u8> + Write<u8>,
    D: DelayMs,
{
    pub fn new(serial: S, delay: D) -> Self {
        Self { serial, delay }
    }

    /// Send an AT command and collect the response until "OK"/"ERROR"
    /// or timeout_ms milliseconds elapse.
    ///
    /// Returns a fixed-size byte array and actual length on success.
    pub fn send_cmd(
        &mut self,
        cmd: &[u8],           // command bytes WITHOUT \r\n
        timeout_ms: u32,
    ) -> AtResult<([u8; MAX_RESP_LEN], usize)> {
        // Flush pending RX by discarding bytes for 5 ms
        let flush_end = self.delay.elapsed_ms() + 5;
        while self.delay.elapsed_ms() < flush_end {
            let _ = self.serial.read(); // ignore result
        }

        // Transmit command + CRLF
        for &b in cmd.iter().chain(b"\r\n".iter()) {
            block!(self.serial.write(b)).map_err(|_| AtError::SerialError)?;
        }

        // Collect response
        let mut buf = [0u8; MAX_RESP_LEN];
        let mut pos = 0usize;
        let start = self.delay.elapsed_ms();

        loop {
            if self.delay.elapsed_ms().wrapping_sub(start) >= timeout_ms {
                return Err(AtError::Timeout);
            }

            match self.serial.read() {
                Ok(byte) => {
                    if pos >= MAX_RESP_LEN {
                        return Err(AtError::Overflow);
                    }
                    buf[pos] = byte;
                    pos += 1;

                    // Check for "OK\r\n" or "ERROR\r\n"
                    if contains_subsequence(&buf[..pos], b"OK") {
                        return Ok((buf, pos));
                    }
                    if contains_subsequence(&buf[..pos], b"ERROR") {
                        return Err(AtError::Nack);
                    }
                }
                Err(nb::Error::WouldBlock) => {
                    // No byte yet — continue spinning
                }
                Err(_) => return Err(AtError::SerialError),
            }
        }
    }

    /// Convenience: send command and only check for OK
    pub fn cmd_ok(&mut self, cmd: &[u8], timeout_ms: u32) -> AtResult<()> {
        self.send_cmd(cmd, timeout_ms).map(|_| ())
    }

    /// Write raw bytes (data mode — not a command)
    pub fn write_raw(&mut self, data: &[u8]) -> AtResult<()> {
        for &b in data {
            block!(self.serial.write(b)).map_err(|_| AtError::SerialError)?;
        }
        Ok(())
    }

    /// Read up to `buf.len()` bytes (non-blocking, returns count)
    pub fn read_raw(&mut self, buf: &mut [u8]) -> usize {
        let mut n = 0;
        while n < buf.len() {
            match self.serial.read() {
                Ok(b) => { buf[n] = b; n += 1; }
                Err(_) => break,
            }
        }
        n
    }
}

fn contains_subsequence(haystack: &[u8], needle: &[u8]) -> bool {
    if needle.len() > haystack.len() { return false; }
    haystack.windows(needle.len()).any(|w| w == needle)
}
```

### 2. HC-05 Driver (Rust, no_std)

```rust
// hc05.rs — HC-05 driver built on top of AtEngine
#![no_std]

use crate::at_engine::{AtEngine, AtError, AtResult, DelayMs, MAX_RESP_LEN};
use embedded_hal::serial::{Read, Write};
use core::fmt::Write as FmtWrite;

/// Role of the HC-05 module
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum Hc05Role {
    Slave  = 0,
    Master = 1,
}

/// HC-05 baud rate configuration (matches AT+UART parameter)
pub struct Hc05UartCfg {
    pub baud:   u32,
    pub stop:   u8,   // 0=1bit, 1=2bit
    pub parity: u8,   // 0=none, 1=odd, 2=even
}

pub struct Hc05<S, D> {
    engine: AtEngine<S, D>,
}

impl<S, D> Hc05<S, D>
where
    S: Read<u8> + Write<u8>,
    D: DelayMs,
{
    pub fn new(serial: S, delay: D) -> Self {
        Self { engine: AtEngine::new(serial, delay) }
    }

    /// Verify communication — returns true if module responds to "AT"
    pub fn test(&mut self) -> bool {
        self.engine.cmd_ok(b"AT", 1000).is_ok()
    }

    /// Query the firmware version string
    pub fn version(&mut self) -> AtResult<[u8; MAX_RESP_LEN]> {
        let (buf, _) = self.engine.send_cmd(b"AT+VERSION?", 1500)?;
        Ok(buf)
    }

    /// Query the Bluetooth MAC address
    pub fn address(&mut self) -> AtResult<[u8; MAX_RESP_LEN]> {
        let (buf, _) = self.engine.send_cmd(b"AT+ADDR?", 1500)?;
        Ok(buf)
    }

    /// Set the device name (max 32 chars)
    pub fn set_name(&mut self, name: &str) -> AtResult<()> {
        let mut cmd = [0u8; 48];
        let mut writer = SliceWriter::new(&mut cmd);
        write!(writer, "AT+NAME={}", name).map_err(|_| AtError::Overflow)?;
        let len = writer.pos();
        self.engine.cmd_ok(&cmd[..len], 2000)
    }

    /// Set the pairing PIN (4 digit string)
    pub fn set_pin(&mut self, pin: &str) -> AtResult<()> {
        let mut cmd = [0u8; 24];
        let mut writer = SliceWriter::new(&mut cmd);
        write!(writer, "AT+PSWD={}", pin).map_err(|_| AtError::Overflow)?;
        let len = writer.pos();
        self.engine.cmd_ok(&cmd[..len], 2000)
    }

    /// Configure UART parameters
    pub fn set_uart(&mut self, cfg: &Hc05UartCfg) -> AtResult<()> {
        let mut cmd = [0u8; 32];
        let mut writer = SliceWriter::new(&mut cmd);
        write!(writer, "AT+UART={},{},{}", cfg.baud, cfg.stop, cfg.parity)
            .map_err(|_| AtError::Overflow)?;
        let len = writer.pos();
        self.engine.cmd_ok(&cmd[..len], 2000)
    }

    /// Set module role
    pub fn set_role(&mut self, role: Hc05Role) -> AtResult<()> {
        let cmd: &[u8] = match role {
            Hc05Role::Slave  => b"AT+ROLE=0",
            Hc05Role::Master => b"AT+ROLE=1",
        };
        self.engine.cmd_ok(cmd, 2000)
    }

    /// Soft reset
    pub fn reset(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+RESET", 3000)
    }

    /// Restore factory defaults
    pub fn restore_defaults(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+ORGL", 2000)
    }

    /// Full configuration helper
    pub fn configure(
        &mut self,
        name:  &str,
        pin:   &str,
        baud:  u32,
        role:  Hc05Role,
    ) -> AtResult<()> {
        self.set_name(name)?;
        self.set_pin(pin)?;
        self.set_uart(&Hc05UartCfg { baud, stop: 0, parity: 0 })?;
        self.set_role(role)?;
        self.reset()
    }

    /// Send raw data (data mode)
    pub fn send(&mut self, data: &[u8]) -> AtResult<()> {
        self.engine.write_raw(data)
    }

    /// Receive raw data (non-blocking)
    pub fn recv(&mut self, buf: &mut [u8]) -> usize {
        self.engine.read_raw(buf)
    }
}

// -----------------------------------------------------------------------
// Helper: write! into a &mut [u8] slice
// -----------------------------------------------------------------------
struct SliceWriter<'a> {
    buf: &'a mut [u8],
    pos: usize,
}
impl<'a> SliceWriter<'a> {
    fn new(buf: &'a mut [u8]) -> Self { Self { buf, pos: 0 } }
    fn pos(&self) -> usize { self.pos }
}
impl<'a> core::fmt::Write for SliceWriter<'a> {
    fn write_str(&mut self, s: &str) -> core::fmt::Result {
        let bytes = s.as_bytes();
        let remaining = self.buf.len() - self.pos;
        if bytes.len() > remaining { return Err(core::fmt::Error); }
        self.buf[self.pos..self.pos + bytes.len()].copy_from_slice(bytes);
        self.pos += bytes.len();
        Ok(())
    }
}
```

### 3. Async Rust with tokio-serial (Linux / Host)

```rust
// bt_async.rs — async AT command handler using tokio + tokio-serial
// Suitable for Raspberry Pi, Linux single-board computers, or host testing
//
// Cargo.toml dependencies:
//   tokio         = { version = "1", features = ["full"] }
//   tokio-serial  = "5"
//   bytes         = "1"

use tokio::io::{AsyncBufReadExt, AsyncWriteExt, BufReader};
use tokio::time::{timeout, Duration};
use tokio_serial::SerialPortBuilderExt;
use std::io;

const PORT: &str = "/dev/ttyUSB0";
const BAUD: u32  = 38400;

pub struct BluetoothAt {
    writer: tokio_serial::SerialStream,
    reader: BufReader<tokio_serial::SerialStream>,
}

impl BluetoothAt {
    pub fn open(port: &str, baud: u32) -> io::Result<Self> {
        let builder = tokio_serial::new(port, baud);
        let stream = builder.open_native_async()?;

        // Split into two halves — both wrap the same fd
        // tokio-serial streams are Clone-able for this pattern
        let (rx, tx) = tokio::io::split(stream);

        // We need to restructure slightly because tokio::io::split
        // returns ReadHalf/WriteHalf, not owned SerialStream.
        // Practical approach: use two separate file descriptors.
        // For simplicity here, keep it as a single struct with
        // manual read/write methods using a Mutex.
        todo!("Use Arc<Mutex<SerialStream>> in real code")
    }

    /// Send AT command, return full response as String
    pub async fn send_cmd(
        &mut self,
        cmd: &str,
        timeout_ms: u64,
    ) -> Result<String, String> {
        let full_cmd = format!("{}\r\n", cmd);
        self.writer
            .write_all(full_cmd.as_bytes())
            .await
            .map_err(|e| e.to_string())?;

        let mut response = String::new();
        let dur = Duration::from_millis(timeout_ms);

        match timeout(dur, self.read_until_ok_or_error(&mut response)).await {
            Ok(Ok(())) => Ok(response),
            Ok(Err(e)) => Err(e),
            Err(_) => Err(format!("Timeout after {} ms. Got: {}", timeout_ms, response)),
        }
    }

    async fn read_until_ok_or_error(
        &mut self,
        out: &mut String,
    ) -> Result<(), String> {
        let mut line = String::new();
        loop {
            line.clear();
            self.reader
                .read_line(&mut line)
                .await
                .map_err(|e| e.to_string())?;
            out.push_str(&line);
            if line.contains("OK") || line.contains("ERROR") {
                if line.contains("ERROR") {
                    return Err(format!("Module returned ERROR. Full response: {}", out));
                }
                return Ok(());
            }
        }
    }
}

/// Standalone async configuration example (tokio runtime)
#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port = tokio_serial::new(PORT, BAUD)
        .timeout(std::time::Duration::from_millis(100))
        .open_native_async()?;

    let (rx_half, mut tx_half) = tokio::io::split(port);
    let mut reader = BufReader::new(rx_half);

    // Helper closure: write command + read until OK/ERROR
    let send = |cmd: &str| {
        let cmd = format!("{}\r\n", cmd);
        async move { cmd }
    };

    // Test
    tx_half.write_all(b"AT\r\n").await?;
    let mut resp = String::new();
    let mut line = String::new();
    loop {
        reader.read_line(&mut line).await?;
        resp.push_str(&line);
        if line.contains("OK") || line.contains("ERROR") { break; }
        line.clear();
    }
    println!("AT response: {}", resp.trim());

    // Set name
    tx_half.write_all(b"AT+NAME=RustBot\r\n").await?;
    resp.clear();
    loop {
        line.clear();
        reader.read_line(&mut line).await?;
        resp.push_str(&line);
        if line.contains("OK") || line.contains("ERROR") { break; }
    }
    println!("Set name: {}", resp.trim());

    Ok(())
}
```

### 4. HM-10 BLE Driver in Rust (no_std)

```rust
// hm10.rs — HM-10 BLE module driver
#![no_std]

use crate::at_engine::{AtEngine, AtError, AtResult, DelayMs};
use embedded_hal::serial::{Read, Write};
use core::fmt::Write as FmtWrite;
use crate::hc05::SliceWriter;  // reuse from hc05.rs

/// HM-10 baud rate codes (AT+BAUD<n>)
#[repr(u8)]
pub enum Hm10Baud {
    Baud9600   = 0,
    Baud19200  = 1,
    Baud38400  = 2,
    Baud57600  = 3,
    Baud115200 = 4,
}

pub struct Hm10<S, D> {
    engine: AtEngine<S, D>,
}

impl<S, D> Hm10<S, D>
where
    S: Read<u8> + Write<u8>,
    D: DelayMs,
{
    pub fn new(serial: S, delay: D) -> Self {
        Self { engine: AtEngine::new(serial, delay) }
    }

    pub fn test(&mut self) -> bool {
        self.engine.cmd_ok(b"AT", 1000).is_ok()
    }

    pub fn set_name(&mut self, name: &str) -> AtResult<()> {
        let mut cmd = [0u8; 32];
        let mut w = SliceWriter::new(&mut cmd);
        write!(w, "AT+NAME{}", name).map_err(|_| AtError::Overflow)?;
        self.engine.cmd_ok(&cmd[..w.pos()], 1500)
    }

    pub fn set_role_peripheral(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+ROLE0", 1500)
    }

    pub fn set_role_central(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+ROLE1", 1500)
    }

    pub fn set_baud(&mut self, baud: Hm10Baud) -> AtResult<()> {
        let mut cmd = [0u8; 10];
        let mut w = SliceWriter::new(&mut cmd);
        write!(w, "AT+BAUD{}", baud as u8).map_err(|_| AtError::Overflow)?;
        self.engine.cmd_ok(&cmd[..w.pos()], 1500)
    }

    /// Enable connect/disconnect notifications ("OK+CONN" / "OK+LOST")
    pub fn enable_notifications(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+NOTI1", 1500)
    }

    /// Query the BLE MAC address
    pub fn get_address(&mut self) -> AtResult<[u8; 12]> {
        let (buf, _) = self.engine.send_cmd(b"AT+ADDR?", 1500)?;
        // Response: "OK+ADDR:XXXXXXXXXXXX\r\n"
        let mut addr = [0u8; 12];
        if let Some(pos) = find_subsequence(&buf, b"OK+ADDR:") {
            addr.copy_from_slice(&buf[pos + 8..pos + 20]);
        }
        Ok(addr)
    }

    pub fn reset(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+RESET", 2000)
    }

    pub fn factory_reset(&mut self) -> AtResult<()> {
        self.engine.cmd_ok(b"AT+RENEW", 3000)
    }

    pub fn send(&mut self, data: &[u8]) -> AtResult<()> {
        self.engine.write_raw(data)
    }

    pub fn recv(&mut self, buf: &mut [u8]) -> usize {
        self.engine.read_raw(buf)
    }
}

fn find_subsequence(haystack: &[u8], needle: &[u8]) -> Option<usize> {
    haystack.windows(needle.len()).position(|w| w == needle)
}
```

---

## Advanced Topics

### Master Mode: Scanning and Connecting (HC-05)

When the HC-05 is in master role, it can scan for other Bluetooth devices and
initiate connections programmatically.

```c
/*
 * HC-05 master mode: scan for devices, connect to a specific one.
 * The module must be configured as master (AT+ROLE=1) first.
 */

#define INQUIRY_TIMEOUT_MS 10000

bt_status_t hc05_scan_and_connect(const char *target_addr)
{
    char resp[512];
    bt_status_t st;

    /* Start inquiry */
    st = bt_send_cmd("AT+INQ", resp, sizeof(resp), INQUIRY_TIMEOUT_MS);
    if (st != BT_OK) return st;
    /* resp now contains lines like: "+INQ:1234:56:ABCDEF,0,RSSI" */

    /* Check if target is in results */
    if (strstr(resp, target_addr) == NULL)
        return BT_ERR_NACK;

    /* Connect */
    char cmd[48];
    snprintf(cmd, sizeof(cmd), "AT+LINK=%s", target_addr);
    return bt_send_cmd(cmd, resp, sizeof(resp), BT_LONG_TIMEOUT_MS);
}
```

### Baud Rate Auto-Detection

When you don't know the current baud rate of a module, try common rates in
sequence:

```c
static const uint32_t COMMON_BAUDS[] = {
    9600, 38400, 57600, 115200, 4800, 19200, 0
};

uint32_t hc05_detect_baud(void) {
    for (int i = 0; COMMON_BAUDS[i] != 0; i++) {
        uart_reconfigure(COMMON_BAUDS[i]);
        HAL_Delay(100);
        char resp[16];
        if (bt_send_cmd("AT", resp, sizeof(resp), 500) == BT_OK)
            return COMMON_BAUDS[i];
    }
    return 0; /* not found */
}
```

### State Machine for Robust AT Handling (C)

For production code, a non-blocking state machine is preferable to blocking
`bt_send_cmd`:

```c
typedef enum {
    AT_STATE_IDLE,
    AT_STATE_SENDING,
    AT_STATE_WAITING,
    AT_STATE_DONE,
    AT_STATE_ERROR,
} at_state_t;

typedef struct {
    at_state_t  state;
    char        cmd[MAX_CMD_LEN];
    char        resp[MAX_RESP_LEN];
    size_t      resp_pos;
    uint32_t    start_tick;
    uint32_t    timeout_ms;
    bt_status_t result;
} at_ctx_t;

void at_ctx_start(at_ctx_t *ctx, const char *cmd, uint32_t timeout_ms)
{
    strncpy(ctx->cmd, cmd, MAX_CMD_LEN - 1);
    ctx->resp[0]    = '\0';
    ctx->resp_pos   = 0;
    ctx->start_tick = HAL_GetTick();
    ctx->timeout_ms = timeout_ms;
    ctx->state      = AT_STATE_SENDING;
    ctx->result     = BT_OK;
}

/**
 * Call this from the main loop (not from an ISR).
 * Returns true when the operation is complete (check ctx->result).
 */
bool at_ctx_poll(at_ctx_t *ctx)
{
    switch (ctx->state) {
    case AT_STATE_SENDING: {
        char tx[MAX_CMD_LEN + 2];
        snprintf(tx, sizeof(tx), "%s\r\n", ctx->cmd);
        bt_send_data((uint8_t *)tx, strlen(tx));
        ctx->state = AT_STATE_WAITING;
        break;
    }
    case AT_STATE_WAITING: {
        uint8_t byte;
        while (bt_receive_data(&byte, 1) == 1) {
            if (ctx->resp_pos < MAX_RESP_LEN - 1) {
                ctx->resp[ctx->resp_pos++] = (char)byte;
                ctx->resp[ctx->resp_pos]   = '\0';
            }
            if (strstr(ctx->resp, "OK")) {
                ctx->state  = AT_STATE_DONE;
                ctx->result = BT_OK;
                return true;
            }
            if (strstr(ctx->resp, "ERROR")) {
                ctx->state  = AT_STATE_ERROR;
                ctx->result = BT_ERR_NACK;
                return true;
            }
        }
        if (HAL_GetTick() - ctx->start_tick >= ctx->timeout_ms) {
            ctx->state  = AT_STATE_ERROR;
            ctx->result = BT_ERR_TIMEOUT;
            return true;
        }
        break;
    }
    case AT_STATE_DONE:
    case AT_STATE_ERROR:
        return true;
    default:
        break;
    }
    return false;
}
```

### BLE GATT Service Discovery (HM-10 Central Mode)

When operating as a BLE central, the HM-10 can discover and read GATT
characteristics from remote peripherals:

```c
/*
 * Example: HM-10 central mode — connect to a peripheral and read
 * a characteristic value.
 *
 * Flow:
 *  1. Set ROLE=1 (central)
 *  2. AT+DISC? — discover nearby BLE devices
 *  3. AT+CON<MAC> — connect
 *  4. AT+FIND? — list services and characteristics
 *  5. AT+CHAR? — read characteristic value
 */

void hm10_central_demo(void)
{
    char resp[512];

    /* Switch to central */
    bt_send_cmd("AT+ROLE1", resp, sizeof(resp), 1500);
    bt_send_cmd("AT+RESET", resp, sizeof(resp), 3000);
    HAL_Delay(1500);

    /* Discover */
    bt_send_cmd("AT+DISC?", resp, sizeof(resp), 8000);
    printf("Discovered: %s\n", resp);
    /* Parse the first MAC address from resp ... */

    /* Connect (replace MAC with actual address) */
    bt_send_cmd("AT+CON112233AABBCC", resp, sizeof(resp), 5000);

    /* Read characteristic */
    bt_send_cmd("AT+CHAR?", resp, sizeof(resp), 3000);
    printf("Characteristics: %s\n", resp);
}
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| No response to `AT` | Wrong baud rate | Try all common rates with auto-detect |
| `AT` works, `AT+NAME?` fails | Module not in AT mode | Hold KEY/EN high before power-on |
| Commands work but data doesn't transfer | Module in AT mode during data phase | Release KEY pin, reconnect |
| Garbled output | Baud rate mismatch | Match MCU and module baud rates |
| Module pairs but disconnects | Power supply noise | Add 100 µF cap on VCC |
| HM-10 returns `OK+LOST` | BLE connection dropped | Check range; re-enable `AT+NOTI1` |
| HC-06 ignores AT commands | Connected state | Disconnect first; HC-06 ignores AT while connected |
| `AT+NAME=` rejected on HC-06 | HC-06 uses no `=` | Use `AT+NAMEname` format |
| Timeout on master `AT+INQ` | Inquiry period too short | Allow 10+ seconds for inquiry |

### Common Pitfalls

- **Case sensitivity**: AT commands are case-sensitive on most modules (`AT+NAME` works, `at+name` does not).
- **Line endings**: Always terminate with `\r\n`. Some modules accept `\r` alone but `\r\n` is safest.
- **Data mode bleed-through**: If random bytes appear in command responses, the remote side is in data mode and sending data. Ensure both ends coordinate mode switches.
- **Reset timing**: After `AT+RESET` or a baud change, always wait at least 1.5 seconds before sending further commands.
- **Clone modules**: Many cheap HM-10 modules are counterfeit and have reduced or incompatible AT command sets. Test basic commands first and compare with genuine HM-10 documentation.

---

## Summary

Bluetooth module control over UART using AT commands provides a practical and
well-supported approach to adding Bluetooth Classic or BLE capability to
embedded systems without implementing a full RF stack. The key principles are:

**Protocol:** Commands are ASCII strings starting with `AT`, terminated with
`\r\n`, and the module replies with result lines ending in `OK` or `ERROR`. A
two-mode architecture (command mode vs. data mode) governs when AT commands are
processed versus when raw data is forwarded over Bluetooth.

**Hardware:** A simple 4-wire connection (TX, RX, GND, VCC) is sufficient for
basic operation. An optional KEY/EN pin controls command mode entry on modules
like the HC-05. Voltage level matching (3.3 V vs. 5 V) must be observed.

**Module Variants:** HC-05 supports master/slave Bluetooth Classic and has the
richest AT set. HC-06 is slave-only with a simplified (no `=`) command syntax.
HM-10 supports BLE 4.0/4.2, exposes a GATT UART service, and is compatible with
smartphones. All require tailored AT command handling due to syntactic
differences.

**C/C++ Implementation:** A robust implementation uses a circular ring buffer
fed by a UART RX interrupt, a blocking (or non-blocking state-machine)
`send_cmd()` function that searches for `OK`/`ERROR` terminal strings, and
typed wrapper functions for each module setting. C++ adds value through RAII
wrappers and lambda-based dependency injection for portability.

**Rust Implementation:** The `embedded-hal` trait bounds allow a single
`AtEngine<S, D>` to work with any HAL-compatible serial port and delay source —
making the driver fully portable across MCU families. The `no_std` constraint
forces use of fixed-size stack buffers and avoids heap allocation, which is
appropriate for bare-metal targets. Async Rust with `tokio-serial` suits
Linux-based systems such as the Raspberry Pi.

**Key Takeaways:**

- Always match baud rates between MCU and module (including mode-dependent defaults)
- Use a timeout and terminal-string check rather than fixed-length reads
- Implement a non-blocking state machine for production/RTOS environments
- Account for module-specific AT syntax differences (especially HC-06 vs HC-05)
- Allow adequate reset and stabilisation delays after configuration changes
- Validate communication with a simple `AT` → `OK` handshake before proceeding

---

*Document: UART Topic 42 — Bluetooth Module Control*
*AT command communication with BLE/Classic modules*