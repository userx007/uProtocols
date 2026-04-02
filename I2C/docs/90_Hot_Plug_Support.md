# 90. I2C Hot-Plug Support

- **Background & Concepts** — why I2C hot-plug is non-trivial (no protocol-native insertion events, address collisions, bus capacitance, power sequencing)
- **Hardware Considerations** — hot-swap buffer ICs, card-present GPIO, pull-up placement, power sequencing
- **Detection Strategies** — polling/address scanning, GPIO interrupt, SMBus Alert, OS udev
- **C/C++ Code** — polling probe with `i2c-dev`, GPIO card-present with `libgpiod`, and a full C++ `HotPlugManager` class with callbacks and a background thread
- **Rust Code** — async polling with `tokio` + `i2cdev`, GPIO events with `tokio-gpiod`, and a generic trait-based `HotPlugRegistry` with per-device driver objects
- **Linux Integration** — sysfs `new_device`/`delete_device`, udev rules, and kernel bus notifier API
- **Edge Cases** — confirmation counts for insertion transients, removal thresholds, address conflicts, bus lockup/SDA recovery, and thread-safe Rust patterns
- **Summary** — concise synthesis of all key takeaways

> **Detecting and handling devices being added or removed from the bus at runtime**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Background and Concepts](#background-and-concepts)
3. [Hardware Considerations](#hardware-considerations)
4. [Detection Strategies](#detection-strategies)
5. [Implementation in C/C++](#implementation-in-cc)
6. [Implementation in Rust](#implementation-in-rust)
7. [Linux Kernel / sysfs Integration](#linux-kernel--sysfs-integration)
8. [Error Handling and Edge Cases](#error-handling-and-edge-cases)
9. [Summary](#summary)

---

## Introduction

I2C (Inter-Integrated Circuit) is a synchronous, multi-master, multi-slave, packet-switched serial communication bus protocol designed for short-distance communication between integrated circuits. While the standard I2C specification was designed for static, fixed-topology systems — where devices are wired on the bus at board design time — modern embedded systems and industrial applications increasingly require **hot-plug support**: the ability to detect, enumerate, and communicate with devices that are physically connected or disconnected from the bus while the system is running.

Hot-plug support introduces significant complexity because the I2C protocol itself has no native mechanism for announcing device presence or absence. There is no interrupt line per device, no plug/unplug event notification in the protocol, and no standardized "I am here" broadcast. All detection must be implemented by the host software through polling, auxiliary hardware signals, or OS-level subsystem hooks.

---

## Background and Concepts

### Why Hot-Plug is Non-Trivial on I2C

Unlike USB or PCIe — which have physical signaling mechanisms for plug events — I2C is a simple two-wire bus (SDA and SCL). Key challenges include:

- **No device insertion interrupt in the protocol itself.** The master must actively probe addresses to discover new slaves.
- **Address collisions.** If a hot-plugged device shares the same 7-bit address as another device already on the bus, communication becomes ambiguous or corrupted.
- **Bus capacitance.** Connecting a new device mid-operation adds capacitance to SDA/SCL, which can degrade signal quality or cause glitches on live transactions.
- **Power sequencing.** Devices plugged in may not be fully powered and initialized when the master first tries to communicate.
- **Driver state.** The OS or firmware must cleanly unbind drivers when a device disappears and re-bind them when one appears.

### Common Use Cases

- Modular sensor boards (temperature, humidity, pressure) on expansion connectors
- Hot-swappable I2C EEPROM modules for configuration
- Industrial field bus expanders with field-replaceable units
- Development/prototyping boards with plug-in peripheral modules (e.g., Raspberry Pi HATs, Arduino shields, STEMMA QT / Qwiic connector ecosystems)
- Medical or laboratory instruments with interchangeable probe heads

---

## Hardware Considerations

Before writing any software, the hardware must be designed (or understood) to support hot-plug safely.

### 1. Bus Isolation / Hot-Swap Buffers

Dedicated I2C hot-swap buffer ICs (e.g., NXP PCA9517, TI TCA4307) provide:

- Capacitance isolation between the main bus and plug-in branch
- Glitch filtering to prevent transients during insertion from corrupting live transactions
- Automatic enable/disable based on a power-good or card-present signal

### 2. Card-Present (CD) / Interrupt Pin

A dedicated GPIO pin connected to a pull-down (or pull-up) through the connector physically signals device presence. The host can:

- Poll this GPIO
- Use it as a hardware interrupt source (falling/rising edge)

This GPIO-based detection is far more reliable than pure I2C bus probing and is the recommended approach whenever hardware design allows it.

### 3. Power Sequencing

Ensure the device's power rail stabilizes before the host attempts I2C communication. A simple software delay (e.g., 10–50 ms after detecting the card-present signal) is often sufficient; some devices publish a specific power-on reset (POR) time in their datasheets.

### 4. Pull-Up Resistors

Hot-plug connectors should be designed so the bus pull-up resistors are on the main board side (not the module side). This avoids the bus floating during insertion/removal.

---

## Detection Strategies

### Strategy 1: Polling (Address Scanning)

The host periodically sends a START + slave address + STOP and checks for an ACK. No ACK means no device; ACK means device present.

**Pros:** Simple, no extra hardware.  
**Cons:** Can disturb some sensitive devices (e.g., triggering accidental writes); adds bus traffic; latency proportional to polling interval.

### Strategy 2: GPIO Card-Present Signal

A dedicated GPIO signals physical presence. On change, the host initiates device enumeration or tear-down.

**Pros:** Immediate, low CPU overhead, hardware-reliable.  
**Cons:** Requires hardware support; need debounce for mechanical connectors.

### Strategy 3: Interrupt from Existing Device (SMBus Alert)

The SMBus Alert Response Address (ARA, 0x0C) allows a device to signal the master via a shared `SMBALERT#` line. The master then reads the ARA to find out which device raised the alert.

**Pros:** Works within protocol; low-latency.  
**Cons:** Only for SMBus-capable devices; not a true plug/unplug detection mechanism.

### Strategy 4: OS udev / Kernel Notifier

On Linux, the I2C subsystem can be extended with kernel notifiers and userspace udev rules that respond to I2C adapter or device events.

---

## Implementation in C/C++

### Polling-Based Hot-Plug Detection (Linux i2c-dev)

This example periodically scans a list of expected device addresses and tracks their presence state.

```c
// hotplug_poll.c
// Compile: gcc -o hotplug_poll hotplug_poll.c
// Run:     sudo ./hotplug_poll /dev/i2c-1

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define POLL_INTERVAL_MS   500
#define MAX_TRACKED_ADDRS  16

typedef struct {
    uint8_t  address;
    bool     present;
    char     name[32];
} I2CDevice;

// Attempt a zero-length write to probe for device presence.
// Returns true if device ACKs.
static bool i2c_probe(int fd, uint8_t addr) {
    struct i2c_msg msg = {
        .addr  = addr,
        .flags = 0,       // write direction
        .len   = 0,
        .buf   = NULL
    };
    struct i2c_rdwr_ioctl_data data = {
        .msgs  = &msg,
        .nmsgs = 1
    };

    // Some kernels reject zero-length writes; fall back to a 1-byte read.
    if (ioctl(fd, I2C_RDWR, &data) < 0) {
        // Try a 1-byte read instead
        uint8_t dummy;
        msg.flags = I2C_M_RD;
        msg.len   = 1;
        msg.buf   = &dummy;
        return (ioctl(fd, I2C_RDWR, &data) >= 0);
    }
    return true;
}

static void on_device_connected(const I2CDevice *dev) {
    printf("[HOT-PLUG] Device CONNECTED  @ 0x%02X  (%s)\n",
           dev->address, dev->name);
    // TODO: Initialize device, load driver state, notify application, etc.
}

static void on_device_disconnected(const I2CDevice *dev) {
    printf("[HOT-PLUG] Device REMOVED    @ 0x%02X  (%s)\n",
           dev->address, dev->name);
    // TODO: Release resources, unbind driver, notify application, etc.
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s /dev/i2c-<N>\n", argv[0]);
        return 1;
    }

    int fd = open(argv[1], O_RDWR);
    if (fd < 0) {
        perror("open i2c bus");
        return 1;
    }

    // Devices we want to monitor (address + friendly name)
    I2CDevice tracked[MAX_TRACKED_ADDRS] = {
        { .address = 0x48, .present = false, .name = "TMP102 Temp Sensor" },
        { .address = 0x68, .present = false, .name = "MPU-6050 IMU"       },
        { .address = 0x76, .present = false, .name = "BME280 Env Sensor"  },
        { .address = 0x3C, .present = false, .name = "SSD1306 OLED"       },
    };
    int n_tracked = 4;

    printf("I2C Hot-Plug Monitor started on %s\n", argv[1]);
    printf("Polling every %d ms for %d addresses...\n\n",
           POLL_INTERVAL_MS, n_tracked);

    while (1) {
        for (int i = 0; i < n_tracked; i++) {
            bool now_present = i2c_probe(fd, tracked[i].address);

            if (now_present && !tracked[i].present) {
                tracked[i].present = true;
                on_device_connected(&tracked[i]);
            } else if (!now_present && tracked[i].present) {
                tracked[i].present = false;
                on_device_disconnected(&tracked[i]);
            }
        }
        usleep(POLL_INTERVAL_MS * 1000);
    }

    close(fd);
    return 0;
}
```

---

### GPIO Card-Present with Interrupt (Linux gpiod + i2c-dev)

Using `libgpiod` (v2 API) to detect physical insertion via a card-present GPIO, then communicating with the newly appeared device.

```c
// hotplug_gpio.c
// Compile: gcc -o hotplug_gpio hotplug_gpio.c -lgpiod
// Requires: libgpiod-dev (apt install libgpiod-dev)

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <gpiod.h>

#define GPIO_CHIP      "gpiochip0"
#define CARD_PRESENT_LINE  17          // BCM 17 on Raspberry Pi
#define I2C_BUS        "/dev/i2c-1"
#define DEVICE_ADDR    0x48
#define DEBOUNCE_MS    20

static int i2c_fd = -1;

static void device_connected(void) {
    printf("[EVENT] Device inserted — initializing 0x%02X\n", DEVICE_ADDR);

    // Power-on settling time
    usleep(50 * 1000);

    // Example: read 2 bytes from a TMP102 temperature register
    uint8_t reg = 0x00;
    if (write(i2c_fd, &reg, 1) != 1) {
        perror("write register pointer");
        return;
    }
    uint8_t buf[2];
    if (read(i2c_fd, buf, 2) != 2) {
        perror("read temperature");
        return;
    }
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]) >> 4;
    printf("  TMP102 temperature: %.4f °C\n", raw * 0.0625);
}

static void device_removed(void) {
    printf("[EVENT] Device removed — cleaning up\n");
    // Release any device-specific state here
}

int main(void) {
    // Open I2C bus
    i2c_fd = open(I2C_BUS, O_RDWR);
    if (i2c_fd < 0) { perror("open i2c"); return 1; }
    if (ioctl(i2c_fd, I2C_SLAVE, DEVICE_ADDR) < 0) {
        perror("I2C_SLAVE"); close(i2c_fd); return 1;
    }

    // Open GPIO chip
    struct gpiod_chip *chip = gpiod_chip_open_by_name(GPIO_CHIP);
    if (!chip) { perror("gpiod_chip_open"); return 1; }

    struct gpiod_line *line = gpiod_chip_get_line(chip, CARD_PRESENT_LINE);
    if (!line) { perror("gpiod_chip_get_line"); return 1; }

    // Configure for both-edge detection with debounce
    struct gpiod_line_request_config cfg = {
        .consumer     = "hotplug_monitor",
        .request_type = GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES,
        .flags        = 0,
    };
    if (gpiod_line_request(line, &cfg, 0) < 0) {
        perror("gpiod_line_request"); return 1;
    }

    printf("Waiting for GPIO %d (card-present) events...\n\n",
           CARD_PRESENT_LINE);

    // Track last known state
    bool last_present = (gpiod_line_get_value(line) == 1);
    if (last_present) device_connected();

    struct gpiod_line_event event;
    while (1) {
        // Block until an edge event occurs (or timeout)
        struct timespec timeout = { .tv_sec = 5, .tv_nsec = 0 };
        int ret = gpiod_line_event_wait(line, &timeout);
        if (ret < 0) { perror("event_wait"); break; }
        if (ret == 0) continue;  // timeout — no event

        if (gpiod_line_event_read(line, &event) < 0) continue;

        // Debounce: wait and re-read
        usleep(DEBOUNCE_MS * 1000);
        int val = gpiod_line_get_value(line);

        bool now_present = (val == 1);
        if (now_present == last_present) continue;  // Spurious edge

        last_present = now_present;
        if (now_present) device_connected();
        else             device_removed();
    }

    gpiod_line_release(line);
    gpiod_chip_close(chip);
    close(i2c_fd);
    return 0;
}
```

---

### C++ Object-Oriented Hot-Plug Manager

A more structured approach using a C++ class that manages a registry of devices and handles lifecycle callbacks.

```cpp
// HotPlugManager.hpp
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>

extern "C" {
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
}

class HotPlugManager {
public:
    using Callback = std::function<void(uint8_t address, const std::string& name)>;

    struct DeviceEntry {
        std::string name;
        bool        present{false};
        Callback    on_connect;
        Callback    on_disconnect;
    };

    explicit HotPlugManager(const std::string& bus_path,
                            std::chrono::milliseconds poll_interval =
                                std::chrono::milliseconds(500))
        : bus_path_(bus_path), interval_(poll_interval), running_(false)
    {
        fd_ = open(bus_path.c_str(), O_RDWR);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open I2C bus: " + bus_path);
    }

    ~HotPlugManager() {
        stop();
        if (fd_ >= 0) close(fd_);
    }

    void register_device(uint8_t addr, std::string name,
                         Callback on_connect, Callback on_disconnect)
    {
        devices_[addr] = DeviceEntry{
            std::move(name), false,
            std::move(on_connect), std::move(on_disconnect)
        };
    }

    void start() {
        running_ = true;
        worker_ = std::thread([this]{ poll_loop(); });
    }

    void stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }

private:
    bool probe(uint8_t addr) {
        uint8_t dummy;
        struct i2c_msg msg = {
            .addr  = addr,
            .flags = I2C_M_RD,
            .len   = 1,
            .buf   = &dummy
        };
        struct i2c_rdwr_ioctl_data data{ &msg, 1 };
        return ioctl(fd_, I2C_RDWR, &data) >= 0;
    }

    void poll_loop() {
        while (running_) {
            for (auto& [addr, entry] : devices_) {
                bool now_present = probe(addr);
                if (now_present && !entry.present) {
                    entry.present = true;
                    if (entry.on_connect)
                        entry.on_connect(addr, entry.name);
                } else if (!now_present && entry.present) {
                    entry.present = false;
                    if (entry.on_disconnect)
                        entry.on_disconnect(addr, entry.name);
                }
            }
            std::this_thread::sleep_for(interval_);
        }
    }

    std::string                     bus_path_;
    std::chrono::milliseconds       interval_;
    std::atomic<bool>               running_;
    int                             fd_{-1};
    std::thread                     worker_;
    std::map<uint8_t, DeviceEntry>  devices_;
};
```

```cpp
// main.cpp  — using HotPlugManager
#include "HotPlugManager.hpp"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    HotPlugManager mgr("/dev/i2c-1",
                       std::chrono::milliseconds(300));

    mgr.register_device(0x48, "TMP102",
        [](uint8_t addr, const std::string& name) {
            std::cout << "[+] Connected:    " << name
                      << " @ 0x" << std::hex << (int)addr << "\n";
        },
        [](uint8_t addr, const std::string& name) {
            std::cout << "[-] Disconnected: " << name
                      << " @ 0x" << std::hex << (int)addr << "\n";
        }
    );

    mgr.register_device(0x68, "MPU-6050",
        [](uint8_t addr, const std::string& name) {
            std::cout << "[+] Connected:    " << name
                      << " @ 0x" << std::hex << (int)addr << "\n";
        },
        [](uint8_t addr, const std::string& name) {
            std::cout << "[-] Disconnected: " << name
                      << " @ 0x" << std::hex << (int)addr << "\n";
        }
    );

    mgr.start();
    std::cout << "Hot-plug monitor running. Press Enter to stop.\n";
    std::cin.get();
    mgr.stop();
    return 0;
}
```

---

## Implementation in Rust

### Polling-Based Detection with `linux-embedded-hal` and `i2cdev`

```toml
# Cargo.toml
[dependencies]
i2cdev          = "0.6"
tokio           = { version = "1", features = ["full"] }
tokio-gpiod     = "0.3"
anyhow          = "1"
```

```rust
// src/hotplug_poll.rs
//
// Async polling hot-plug detector using tokio.
// Run: cargo run -- /dev/i2c-1

use anyhow::Result;
use i2cdev::core::I2CTransfer;
use i2cdev::linux::{LinuxI2CDevice, LinuxI2CMessage};
use std::collections::HashMap;
use std::time::Duration;
use tokio::time;

#[derive(Debug, Clone)]
struct DeviceInfo {
    address: u16,
    name:    &'static str,
    present: bool,
}

/// Attempt a 1-byte read to probe for an ACK from `address`.
fn probe_device(bus: &str, address: u16) -> bool {
    let Ok(mut dev) = LinuxI2CDevice::new(bus, address) else {
        return false;
    };
    let mut buf = [0u8; 1];
    let mut msgs = [LinuxI2CMessage::read(&mut buf)];
    dev.transfer(&mut msgs).is_ok()
}

async fn run_monitor(bus: &str, devices: &mut Vec<DeviceInfo>) {
    let mut interval = time::interval(Duration::from_millis(500));

    loop {
        interval.tick().await;

        for dev in devices.iter_mut() {
            let now_present = probe_device(bus, dev.address);

            match (dev.present, now_present) {
                (false, true) => {
                    dev.present = true;
                    println!(
                        "[+] CONNECTED:    {:<24} @ 0x{:02X}",
                        dev.name, dev.address
                    );
                    on_connect(dev.address, dev.name).await;
                }
                (true, false) => {
                    dev.present = false;
                    println!(
                        "[-] DISCONNECTED: {:<24} @ 0x{:02X}",
                        dev.name, dev.address
                    );
                    on_disconnect(dev.address, dev.name).await;
                }
                _ => {}
            }
        }
    }
}

async fn on_connect(address: u16, name: &str) {
    // In a real system: initialize device state, spawn a handler task, etc.
    println!("    -> Initializing {}...", name);
    time::sleep(Duration::from_millis(50)).await; // POR settling
}

async fn on_disconnect(address: u16, name: &str) {
    println!("    -> Cleaning up {}...", name);
}

#[tokio::main]
async fn main() -> Result<()> {
    let bus = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/i2c-1".to_string());

    println!("I2C Hot-Plug Monitor — bus: {bus}");
    println!("Polling every 500 ms...\n");

    let mut devices = vec![
        DeviceInfo { address: 0x48, name: "TMP102 Temp Sensor", present: false },
        DeviceInfo { address: 0x68, name: "MPU-6050 IMU",       present: false },
        DeviceInfo { address: 0x76, name: "BME280 Env Sensor",  present: false },
        DeviceInfo { address: 0x3C, name: "SSD1306 OLED",       present: false },
    ];

    run_monitor(&bus, &mut devices).await;
    Ok(())
}
```

---

### GPIO Card-Present Interrupt with Tokio (Rust)

```rust
// src/hotplug_gpio.rs
//
// Uses tokio-gpiod for async GPIO edge events to drive hot-plug detection.

use anyhow::Result;
use i2cdev::linux::LinuxI2CDevice;
use i2cdev::core::I2CDevice;
use std::time::Duration;
use tokio::time::sleep;
use tokio_gpiod::{Chip, EdgeDetect, Options};

const GPIO_CHIP:   &str = "gpiochip0";
const CD_LINE:     u32  = 17;      // Card-Present GPIO line
const I2C_BUS:     &str = "/dev/i2c-1";
const DEVICE_ADDR: u16  = 0x48;
const DEBOUNCE_MS: u64  = 20;

async fn on_device_inserted() -> Result<()> {
    println!("[HOT-PLUG] Device inserted at 0x{DEVICE_ADDR:02X}");
    sleep(Duration::from_millis(50)).await;  // POR settling time

    let mut dev = LinuxI2CDevice::new(I2C_BUS, DEVICE_ADDR)?;

    // Write to pointer register, then read 2 bytes (TMP102 temperature)
    dev.write(&[0x00])?;
    let mut buf = [0u8; 2];
    dev.read(&mut buf)?;

    let raw = ((buf[0] as i16) << 8 | buf[1] as i16) >> 4;
    let temp_c = raw as f32 * 0.0625;
    println!("  Temperature: {temp_c:.2} °C");
    Ok(())
}

async fn on_device_removed() {
    println!("[HOT-PLUG] Device removed — releasing resources");
}

#[tokio::main]
async fn main() -> Result<()> {
    let chip = Chip::new(GPIO_CHIP).await?;

    let opts = Options::input([CD_LINE])
        .edge(EdgeDetect::Both)
        .consumer("hotplug_monitor");

    let mut lines = chip.request_lines(opts).await?;

    println!("Monitoring GPIO line {CD_LINE} for card-present events...\n");

    // Read initial state
    let initial = lines.get_values([0u8]).await?;
    let mut last_present = initial[0] == 1;
    if last_present {
        let _ = on_device_inserted().await;
    }

    loop {
        let event = lines.read_event().await?;

        // Debounce
        sleep(Duration::from_millis(DEBOUNCE_MS)).await;
        let values = lines.get_values([0u8]).await?;
        let now_present = values[0] == 1;

        if now_present == last_present {
            continue; // Spurious edge
        }
        last_present = now_present;

        if now_present {
            if let Err(e) = on_device_inserted().await {
                eprintln!("Error initializing device: {e}");
            }
        } else {
            on_device_removed().await;
        }
    }
}
```

---

### Rust: Generic Hot-Plug Registry with Trait Objects

A flexible design using trait objects for per-device drivers:

```rust
// src/registry.rs

use std::collections::HashMap;
use std::sync::{Arc, Mutex};

/// Trait that every pluggable I2C device driver must implement.
pub trait I2CDriver: Send + Sync {
    fn name(&self) -> &str;
    fn address(&self) -> u16;
    /// Called once after the device is confirmed present.
    fn on_connect(&mut self) -> anyhow::Result<()>;
    /// Called when the device is no longer responding.
    fn on_disconnect(&mut self);
}

pub struct HotPlugRegistry {
    drivers:  Vec<Box<dyn I2CDriver>>,
    presence: HashMap<u16, bool>,
}

impl HotPlugRegistry {
    pub fn new() -> Self {
        Self {
            drivers:  Vec::new(),
            presence: HashMap::new(),
        }
    }

    pub fn register(&mut self, driver: Box<dyn I2CDriver>) {
        let addr = driver.address();
        self.presence.insert(addr, false);
        self.drivers.push(driver);
    }

    /// Call periodically. `probe_fn` returns true if addr is present.
    pub fn poll<F>(&mut self, probe_fn: F)
    where
        F: Fn(u16) -> bool,
    {
        for driver in self.drivers.iter_mut() {
            let addr = driver.address();
            let now  = probe_fn(addr);
            let prev = *self.presence.get(&addr).unwrap_or(&false);

            if now && !prev {
                *self.presence.get_mut(&addr).unwrap() = true;
                if let Err(e) = driver.on_connect() {
                    eprintln!("[ERR] {}: connect failed: {e}", driver.name());
                }
            } else if !now && prev {
                *self.presence.get_mut(&addr).unwrap() = false;
                driver.on_disconnect();
            }
        }
    }
}

// ---- Example driver implementation ----

pub struct Tmp102Driver {
    bus: String,
}

impl Tmp102Driver {
    pub fn new(bus: &str) -> Self {
        Self { bus: bus.to_owned() }
    }
}

impl I2CDriver for Tmp102Driver {
    fn name(&self)    -> &str { "TMP102" }
    fn address(&self) -> u16  { 0x48     }

    fn on_connect(&mut self) -> anyhow::Result<()> {
        use i2cdev::linux::LinuxI2CDevice;
        use i2cdev::core::I2CDevice;

        println!("[TMP102] Connected — reading initial temperature");
        let mut dev = LinuxI2CDevice::new(&self.bus, 0x48)?;
        dev.write(&[0x00])?;
        let mut buf = [0u8; 2];
        dev.read(&mut buf)?;
        let raw  = ((buf[0] as i16) << 8 | buf[1] as i16) >> 4;
        let temp = raw as f32 * 0.0625;
        println!("[TMP102] Temperature: {temp:.2} °C");
        Ok(())
    }

    fn on_disconnect(&mut self) {
        println!("[TMP102] Disconnected — driver state cleared");
    }
}
```

---

## Linux Kernel / sysfs Integration

On a Linux system, the kernel's I2C subsystem exposes mechanisms that can complement or replace userspace hot-plug polling.

### Instantiating Devices from Userspace

```bash
# Bind a driver for a newly detected device at runtime
echo "tmp102 0x48" | sudo tee /sys/bus/i2c/devices/i2c-1/new_device

# Remove a device that has been physically removed
echo "0x48" | sudo tee /sys/bus/i2c/devices/i2c-1/delete_device
```

### udev Rule for I2C Adapter Events

```udev
# /etc/udev/rules.d/99-i2c-hotplug.rules
# Trigger a script when an I2C adapter appears (e.g., USB-to-I2C dongle inserted)
SUBSYSTEM=="i2c-adapter", ACTION=="add", \
    RUN+="/usr/local/bin/i2c_adapter_added.sh %k"

SUBSYSTEM=="i2c-adapter", ACTION=="remove", \
    RUN+="/usr/local/bin/i2c_adapter_removed.sh %k"
```

```bash
#!/bin/bash
# /usr/local/bin/i2c_adapter_added.sh
# $1 = adapter name, e.g., i2c-3
ADAPTER=$1
logger "I2C adapter $ADAPTER appeared — scanning for devices"
i2cdetect -y "${ADAPTER#i2c-}" 2>/dev/null | logger
# Optionally instantiate known devices:
echo "tmp102 0x48" > /sys/bus/i2c/devices/$ADAPTER/new_device
```

### Kernel Notifier (Driver/Module level, C)

For kernel driver development:

```c
// In a kernel module
#include <linux/i2c.h>
#include <linux/notifier.h>

static int my_i2c_notifier(struct notifier_block *nb,
                            unsigned long action, void *data)
{
    struct device *dev = data;
    if (!dev_is_i2c(dev)) return NOTIFY_DONE;

    switch (action) {
    case BUS_NOTIFY_ADD_DEVICE:
        pr_info("I2C device added: %s\n", dev_name(dev));
        break;
    case BUS_NOTIFY_DEL_DEVICE:
        pr_info("I2C device removed: %s\n", dev_name(dev));
        break;
    }
    return NOTIFY_OK;
}

static struct notifier_block my_nb = { .notifier_call = my_i2c_notifier };

// In module init:
bus_register_notifier(&i2c_bus_type, &my_nb);

// In module exit:
bus_unregister_notifier(&i2c_bus_type, &my_nb);
```

---

## Error Handling and Edge Cases

### 1. Unstable ACK During Insertion

A device may partially ACK during the transient of being plugged in (power ramp-up). Use a **confirmation count**: require N successive successful probes before declaring a device present.

```c
// Confirm presence with N consecutive ACKs
static bool probe_with_confirmation(int fd, uint8_t addr, int n) {
    for (int i = 0; i < n; i++) {
        if (!i2c_probe(fd, addr)) return false;
        usleep(5000); // 5 ms between checks
    }
    return true;
}
```

### 2. Ghost Devices After Removal

When a device is physically removed but its capacitance still holds SDA/SCL high, the host may still get a false ACK for a few milliseconds. Use a **consecutive-failure threshold** before declaring a device absent.

### 3. Address Conflicts

If two modules share the same fixed address, only one can be on the bus at a time. Document this limitation clearly. Some devices support address-select pins; use them to assign distinct addresses.

### 4. Bus Lockup Recovery

A device removed mid-transaction can hold SDA low, locking the bus. Recovery procedure:

```c
// Toggle SCL up to 9 times to free a stuck SDA
static void i2c_bus_recovery(int scl_gpio, int sda_gpio) {
    for (int i = 0; i < 9; i++) {
        // Pulse SCL (pseudo-code — use actual GPIO API)
        gpio_set(scl_gpio, 0); usleep(5);
        gpio_set(scl_gpio, 1); usleep(5);
        if (gpio_get(sda_gpio) == 1) break; // SDA released
    }
    // Issue STOP condition
    gpio_set(sda_gpio, 0); usleep(5);
    gpio_set(scl_gpio, 1); usleep(5);
    gpio_set(sda_gpio, 1); usleep(5);
}
```

### 5. Thread Safety in Rust

Wrap the `HotPlugRegistry` in `Arc<Mutex<>>` when sharing between a polling task and application threads:

```rust
let registry = Arc::new(Mutex::new(HotPlugRegistry::new()));
let reg_clone = Arc::clone(&registry);

tokio::spawn(async move {
    let mut interval = tokio::time::interval(Duration::from_millis(500));
    loop {
        interval.tick().await;
        let mut reg = reg_clone.lock().unwrap();
        reg.poll(|addr| probe_device("/dev/i2c-1", addr));
    }
});
```

---

## Summary

I2C hot-plug support is not defined by the I2C specification itself — it must be engineered in both hardware and software. The key points are:

**Hardware foundation:** Use hot-swap buffer ICs (PCA9517, TCA4307) to isolate bus capacitance, dedicate a GPIO line for card-present signaling, and ensure clean power sequencing with adequate settling time before the first I2C transaction.

**Detection strategies:** GPIO card-present interrupts are the most reliable and lowest-overhead approach. Polling (address scanning) is simpler to implement but adds bus traffic and latency; it requires confirmation counts and removal thresholds to avoid false events from insertion transients.

**C/C++ implementation:** The Linux `i2c-dev` interface combined with `I2C_RDWR` ioctl provides a straightforward probe mechanism. `libgpiod` handles asynchronous card-present GPIO events. A C++ manager class encapsulates polling, state tracking, and lifecycle callbacks cleanly.

**Rust implementation:** The `i2cdev` crate provides idiomatic access to Linux I2C devices. Combined with `tokio-gpiod` for async GPIO events and Tokio for concurrency, Rust enables safe, efficient hot-plug management with trait-based driver abstractions and compile-time guarantees against data races.

**Linux integration:** The kernel sysfs interface (`new_device` / `delete_device`), udev rules, and kernel bus notifiers allow hot-plug events to be integrated into the full OS device lifecycle, enabling automatic driver binding and userspace notifications.

**Robustness:** Always implement bus recovery for SDA lockup, confirmation counts for insertion debounce, and consecutive-failure thresholds for removal detection. Address conflict management and thread-safe state sharing are essential for production deployments.

---

*Document covers I2C Hot-Plug Support (Topic 90) — Detecting and handling devices added or removed at runtime.*