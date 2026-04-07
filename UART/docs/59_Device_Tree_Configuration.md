# 59. Device Tree Configuration — Declaring UART Devices in Device Tree for Embedded Linux

1. **Introduction** — why DT nodes are required before any UART driver can probe
2. **What Is a Device Tree?** — DTS → DTB → kernel pipeline
3. **DT Syntax Fundamentals** — nodes, labels, properties, `compatible`, `status`
4. **UART Node Structure** — minimal and annotated complete node examples
5. **Standard Properties Table** — all UART-relevant properties with types and descriptions
6. **Platform-Specific Examples** — Raspberry Pi 4 (BCM2711 / PL011 + mini-UART), STM32MP1, NXP i.MX8M Mini, and SiFive RISC-V
7. **Pinmux & GPIO** — `pinctrl` states, sleep pins, RS-485 direction GPIO
8. **Clocks & Power Domains** — dual-clock nodes, power domain references
9. **DMA Integration** — TX/RX DMA channel specifiers
10. **C/C++ runtime** — `termios` configuration, RS-485 `ioctl`, a RAII C++ class, and live DT property reading via `/proc/device-tree`
11. **Rust runtime** — `serialport` crate, raw RS-485 `ioctl` via `nix`/`libc`, and DT property parsing
12. **Overlays** — dynamic patching with `.dtbo` and `configfs`
13. **Debugging** — `dtc`, `/proc/device-tree`, `dmesg`, and a problem/cause/fix table
14. **Summary** — consolidated key takeaways

---

## Table of Contents

1. [Introduction](#introduction)
2. [What Is a Device Tree?](#what-is-a-device-tree)
3. [Device Tree Syntax Fundamentals](#device-tree-syntax-fundamentals)
4. [UART Node Structure](#uart-node-structure)
5. [Standard UART Device Tree Properties](#standard-uart-device-tree-properties)
6. [Platform-Specific Examples](#platform-specific-examples)
7. [Pinmux and GPIO Configuration](#pinmux-and-gpio-configuration)
8. [Clock and Power Domain Configuration](#clock-and-power-domain-configuration)
9. [DMA Integration](#dma-integration)
10. [Runtime Interaction from C/C++](#runtime-interaction-from-cc)
11. [Runtime Interaction from Rust](#runtime-interaction-from-rust)
12. [Overlays — Dynamic Device Tree Patching](#overlays--dynamic-device-tree-patching)
13. [Debugging Device Tree Issues](#debugging-device-tree-issues)
14. [Summary](#summary)

---

## Introduction

When bringing up a UART peripheral on embedded Linux, two complementary layers must be in place: a **device driver** in the kernel and a **device tree (DT) node** that describes the hardware to the kernel. Without a device tree entry, the kernel cannot discover the peripheral, allocate its resources, or create the `/dev/ttyXXX` node that userspace programs rely on.

This topic covers the full lifecycle of a UART device tree declaration: the syntax and properties used in the `.dts` / `.dtsi` source files, how they are compiled into a `.dtb` binary blob, how the kernel bindings interpret them, and how C/C++ and Rust programs interact with the resulting device file at runtime.

---

## What Is a Device Tree?

A **Device Tree** is a data structure (a tree of named nodes and key-value properties) that describes non-discoverable hardware to an operating system. On ARM, RISC-V, and many other embedded architectures there is no equivalent of a PCI bus that can enumerate peripherals automatically. The device tree fills that gap.

The source format is **DTS** (Device Tree Source). It is compiled by the **Device Tree Compiler** (`dtc`) into a binary **DTB** (Device Tree Blob). The bootloader (U-Boot, barebox, etc.) passes the DTB to the kernel at boot time via a CPU register (e.g., `x1` on AArch64).

```
board.dts  ──dtc──►  board.dtb  ──U-Boot──►  Linux kernel
                                               │
                                               ▼
                                        /proc/device-tree
                                        /dev/ttyS0, /dev/ttyAMA0 …
```

Kernel bindings documentation lives under `Documentation/devicetree/bindings/serial/` in the kernel source tree and is the authoritative reference for any given driver.

---

## Device Tree Syntax Fundamentals

```dts
/ {                                 /* root node */
    #address-cells = <1>;           /* cells used to encode addresses */
    #size-cells    = <1>;           /* cells used to encode sizes     */

    node-label: node-name@base-addr {
        compatible = "vendor,chip-uart", "ns16550a";
        reg        = <0x4000_6000 0x1000>;  /* base address, size */
        interrupts = <0 38 4>;
        status     = "okay";
    };
};
```

Key syntax rules:

- **Nodes** are `name@unit-address { … };`
- **Labels** (`node-label:`) allow cross-referencing with `&node-label { … }`
- **Properties** are `key = value;` — values may be integers (`<…>`), byte arrays (`[…]`), or strings (`"…"`)
- **`compatible`** is the most important property — the kernel matches it against registered drivers
- **`status`** defaults to `"okay"`; set to `"disabled"` to turn a peripheral off without removing its node

---

## UART Node Structure

A minimal but complete UART node looks like this:

```dts
/* Minimal UART node */
uart1: serial@40006000 {
    compatible    = "st,stm32h7-uart";          /* matches driver */
    reg           = <0x40006000 0x400>;         /* MMIO base + size */
    interrupts    = <GIC_SPI 38 IRQ_TYPE_LEVEL_HIGH>;
    clocks        = <&rcc STM32H7_APB1_CLOCK(USART2)>;
    status        = "okay";
};
```

For a 16550-compatible UART (the most portable binding):

```dts
uart0: serial@9000000 {
    compatible    = "ns16550a";
    reg           = <0x9000000 0x100>;
    interrupts    = <0 1 4>;          /* SPI 1, active-high level */
    clock-frequency = <1843200>;      /* external crystal, Hz */
    reg-shift     = <0>;              /* registers are byte-wide */
    reg-io-width  = <4>;             /* but mapped in 32-bit slots */
    fifo-size     = <16>;
    status        = "okay";
};
```

---

## Standard UART Device Tree Properties

| Property | Type | Description |
|---|---|---|
| `compatible` | string-list | Driver match string(s); most specific first |
| `reg` | prop-encoded-array | MMIO base address and size |
| `interrupts` | prop-encoded-array | IRQ specifier (format depends on interrupt controller) |
| `clocks` | phandle + args | Reference to clock provider and clock ID |
| `clock-names` | string-list | Symbolic names matched to `clocks` entries |
| `clock-frequency` | u32 | Fixed input clock in Hz (when no clock framework is used) |
| `baud` | u32 | Initial baud rate at boot (optional, driver default otherwise) |
| `reg-shift` | u32 | Register stride (0=byte, 1=16-bit, 2=32-bit) |
| `reg-io-width` | u32 | Access width for MMIO reads/writes |
| `fifo-size` | u32 | Hardware FIFO depth in bytes |
| `dmas` | phandle + args | DMA channel specifiers (TX, RX) |
| `dma-names` | string-list | Symbolic names (`"tx"`, `"rx"`) |
| `pinctrl-0` | phandle-list | Pin state for `"default"` |
| `pinctrl-names` | string-list | State names matching `pinctrl-N` |
| `rs485-rts-delay` | u32 pair | RTS assert/de-assert delays in ms |
| `linux,rs485-enabled-at-boot-time` | boolean | Enable RS-485 mode at boot |
| `status` | string | `"okay"` or `"disabled"` |

---

## Platform-Specific Examples

### Raspberry Pi 4 (BCM2711) — `mini-uart` and `pl011`

```dts
/* arch/arm/boot/dts/bcm2711.dtsi excerpt */

/* PL011 full UART */
uart0: serial@7e201000 {
    compatible    = "arm,pl011", "arm,primecell";
    reg           = <0x7e201000 0x200>;
    interrupts    = <GIC_SPI 121 IRQ_TYPE_LEVEL_HIGH>;
    clocks        = <&clocks BCM2835_CLOCK_UART>,
                    <&clocks BCM2835_CLOCK_VPU>;
    clock-names   = "uartclk", "apb_pclk";
    arm,primecell-periphid = <0x00241011>;
    cts-event-workaround;
    status        = "disabled";   /* enabled in board overlay */
};

/* AUX mini-UART */
uart1: serial@7e215040 {
    compatible    = "brcm,bcm2835-aux-uart";
    reg           = <0x7e215040 0x40>;
    interrupts    = <GIC_SPI 93 IRQ_TYPE_LEVEL_HIGH>;
    clocks        = <&aux_clk>;
    status        = "disabled";
};
```

Enabling UART0 in a board `.dts` overlay:

```dts
/* raspberrypi-4-uart0.dts overlay */
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,bcm2711";

    fragment@0 {
        target = <&uart0>;
        __overlay__ {
            pinctrl-names = "default";
            pinctrl-0     = <&uart0_pins>;
            status        = "okay";
        };
    };

    fragment@1 {
        target = <&gpio>;
        __overlay__ {
            uart0_pins: uart0_pins {
                brcm,pins     = <14 15>;
                brcm,function = <BCM2835_FSEL_ALT0>;
                brcm,pull     = <BCM2835_PUD_OFF BCM2835_PUD_UP>;
            };
        };
    };
};
```

---

### STM32MP1 (ARM Cortex-A7 + M4)

```dts
/* arch/arm/boot/dts/stm32mp151.dtsi */
usart2: serial@4000e000 {
    compatible    = "st,stm32h7-uart";
    reg           = <0x4000e000 0x400>;
    interrupts-extended = <&exti 27 IRQ_TYPE_LEVEL_HIGH>;
    clocks        = <&rcc USART2_K>;
    resets        = <&rcc USART2_R>;
    wakeup-source;
    dmas          = <&dmamux1 43 0x400 0x5>,
                    <&dmamux1 44 0x400 0x5>;
    dma-names     = "rx", "tx";
    status        = "disabled";
};

/* Board file enables it with RS-485 */
&usart2 {
    pinctrl-names = "default", "sleep", "idle";
    pinctrl-0     = <&usart2_pins_a>;
    pinctrl-1     = <&usart2_sleep_pins_a>;
    pinctrl-2     = <&usart2_idle_pins_a>;
    uart-has-rtscts;
    status        = "okay";
};
```

---

### NXP i.MX8M Mini

```dts
/* arch/arm64/boot/dts/freescale/imx8mm.dtsi */
uart1: serial@30860000 {
    compatible    = "fsl,imx8mm-uart",
                    "fsl,imx6q-uart";
    reg           = <0x30860000 0x10000>;
    interrupts    = <GIC_SPI 26 IRQ_TYPE_LEVEL_HIGH>;
    clocks        = <&clk IMX8MM_CLK_UART1_ROOT>,
                    <&clk IMX8MM_CLK_UART1_ROOT>;
    clock-names   = "ipg", "per";
    dmas          = <&sdma1 22 4 0>, <&sdma1 23 4 0>;
    dma-names     = "rx", "tx";
    status        = "disabled";
};

/* Board overlay — console UART */
&uart1 {
    pinctrl-names = "default";
    pinctrl-0     = <&pinctrl_uart1>;
    status        = "okay";
};
```

---

### RISC-V SiFive (SiFive UART)

```dts
/* arch/riscv/boot/dts/sifive/fu540-c000.dtsi */
uart0: serial@10010000 {
    compatible    = "sifive,fu540-c000-uart0",
                    "sifive,uart0";
    reg           = <0x0 0x10010000 0x0 0x1000>;
    interrupt-parent = <&plic>;
    interrupts    = <4>;
    clocks        = <&prci FU540_PRCI_CLK_TLCLK>;
    status        = "okay";
};
```

---

## Pinmux and GPIO Configuration

Modern SoCs multiplex physical pads between several functions. The `pinctrl` subsystem manages this. A UART node references one or more pin states.

```dts
/* Pin controller node (SoC .dtsi) */
pinctrl: pinmux@fe000000 {
    compatible = "vendor,soc-pinctrl";
    reg        = <0xfe000000 0x10000>;

    uart2_default: uart2-default-pins {
        /* TX on PA2, RX on PA3 — function 7 = UART2 */
        pins = "PA2", "PA3";
        function = <7>;
        bias-disable;
        drive-strength = <4>;
    };

    uart2_sleep: uart2-sleep-pins {
        pins = "PA2", "PA3";
        function = <0>;             /* GPIO, hi-Z during suspend */
        bias-pull-down;
    };
};

/* UART node references the states */
uart2: serial@40004400 {
    compatible    = "vendor,uart";
    reg           = <0x40004400 0x400>;
    interrupts    = <0 39 4>;
    clocks        = <&apb1_clk>;
    pinctrl-names = "default", "sleep";
    pinctrl-0     = <&uart2_default>;
    pinctrl-1     = <&uart2_sleep>;
    status        = "okay";
};
```

For RS-485 with a direction-control GPIO:

```dts
uart3: serial@40004800 {
    compatible    = "vendor,uart";
    reg           = <0x40004800 0x400>;
    interrupts    = <0 40 4>;
    clocks        = <&apb1_clk>;
    pinctrl-names = "default";
    pinctrl-0     = <&uart3_pins>;

    /* RS-485 direction GPIO: GPIO bank 2, pin 6, active-high */
    rts-gpios     = <&gpio2 6 GPIO_ACTIVE_HIGH>;
    rs485-rts-active-high;
    linux,rs485-enabled-at-boot-time;
    rs485-rts-delay = <1 1>;        /* assert/de-assert: 1 ms each */

    status        = "okay";
};
```

---

## Clock and Power Domain Configuration

```dts
/* Clock controller */
clks: clock-controller@50000000 {
    compatible = "vendor,clock-ctrl";
    reg        = <0x50000000 0x1000>;
    #clock-cells = <1>;
};

/* Power domain controller */
pd: power-domain-controller@50010000 {
    compatible = "vendor,power-domain";
    reg        = <0x50010000 0x100>;
    #power-domain-cells = <1>;
};

uart4: serial@40005000 {
    compatible    = "vendor,uart";
    reg           = <0x40005000 0x400>;
    interrupts    = <0 41 4>;

    /* Two clocks: bus clock and functional clock */
    clocks        = <&clks UART4_BUS_CLK>, <&clks UART4_KERN_CLK>;
    clock-names   = "bus", "kern";

    /* Power domain — must be on for UART to function */
    power-domains = <&pd UART4_PD>;

    status        = "okay";
};
```

---

## DMA Integration

DMA avoids CPU involvement in byte-by-byte transfers, critical for high baud rates.

```dts
dma: dma-controller@48000000 {
    compatible     = "vendor,dma-ctrl";
    reg            = <0x48000000 0x1000>;
    interrupts     = <0 10 4>;
    #dma-cells     = <3>;           /* channel, request, flags */
};

uart5: serial@40005400 {
    compatible    = "vendor,uart";
    reg           = <0x40005400 0x400>;
    interrupts    = <0 42 4>;
    clocks        = <&clks UART5_CLK>;

    /*
     * Two DMA channels:
     *   Channel 5, request line 7, flags 0x0  → TX
     *   Channel 6, request line 8, flags 0x0  → RX
     */
    dmas          = <&dma 5 7 0x0>, <&dma 6 8 0x0>;
    dma-names     = "tx", "rx";

    status        = "okay";
};
```

---

## Runtime Interaction from C/C++

Once the device tree node is in place and the kernel boots, the UART appears as `/dev/ttyS0`, `/dev/ttyAMA0`, `/dev/ttySTM0`, etc. Standard POSIX `termios` APIs apply universally.

### Opening and Configuring a Serial Port (C)

```c
/**
 * uart_open.c — Open and configure a UART device on Linux
 * Compile: gcc -O2 -o uart_open uart_open.c
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    const char *device;
    speed_t     baud;
    uint8_t     data_bits; /* 5, 6, 7, or 8 */
    uint8_t     stop_bits; /* 1 or 2 */
    char        parity;    /* 'N', 'E', 'O' */
} uart_config_t;

int uart_open(const uart_config_t *cfg)
{
    int fd = open(cfg->device, O_RDWR | O_NOCTTY | O_SYNC);
    if (fd < 0) {
        fprintf(stderr, "open %s: %s\n", cfg->device, strerror(errno));
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    /* Baud rate */
    cfsetispeed(&tty, cfg->baud);
    cfsetospeed(&tty, cfg->baud);

    /* Raw mode — disable all processing */
    cfmakeraw(&tty);

    /* Data bits */
    tty.c_cflag &= ~CSIZE;
    switch (cfg->data_bits) {
        case 5: tty.c_cflag |= CS5; break;
        case 6: tty.c_cflag |= CS6; break;
        case 7: tty.c_cflag |= CS7; break;
        default: tty.c_cflag |= CS8; break;
    }

    /* Stop bits */
    if (cfg->stop_bits == 2)
        tty.c_cflag |= CSTOPB;
    else
        tty.c_cflag &= ~CSTOPB;

    /* Parity */
    switch (cfg->parity) {
        case 'E':
            tty.c_cflag |=  PARENB;
            tty.c_cflag &= ~PARODD;
            break;
        case 'O':
            tty.c_cflag |=  PARENB;
            tty.c_cflag |=  PARODD;
            break;
        default: /* 'N' */
            tty.c_cflag &= ~PARENB;
            break;
    }

    /* Enable receiver, ignore modem control lines */
    tty.c_cflag |= (CLOCAL | CREAD);

    /* Non-blocking read with timeout: return after 1 char or 1/10 s */
    tty.c_cc[VMIN]  = 1;
    tty.c_cc[VTIME] = 1;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }
    tcflush(fd, TCIOFLUSH);
    return fd;
}

int main(void)
{
    const uart_config_t cfg = {
        .device    = "/dev/ttyS1",
        .baud      = B115200,
        .data_bits = 8,
        .stop_bits = 1,
        .parity    = 'N',
    };

    int fd = uart_open(&cfg);
    if (fd < 0) return 1;

    const char *msg = "Hello from UART\r\n";
    ssize_t n = write(fd, msg, strlen(msg));
    printf("Wrote %zd bytes\n", n);

    char buf[64];
    n = read(fd, buf, sizeof(buf) - 1);
    if (n > 0) {
        buf[n] = '\0';
        printf("Read: %s\n", buf);
    }

    close(fd);
    return 0;
}
```

---

### RS-485 Half-Duplex Control (C)

```c
/**
 * rs485.c — Enable RS-485 mode via ioctl after device tree configures RTS GPIO
 * Requires: Linux ≥ 3.2, kernel built with CONFIG_SERIAL_RS485
 */
#include <fcntl.h>
#include <linux/serial.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int uart_enable_rs485(int fd)
{
    struct serial_rs485 rs485conf = {
        .flags         = SER_RS485_ENABLED
                       | SER_RS485_RTS_ON_SEND,   /* RTS high during TX */
        .delay_rts_before_send = 1,               /* ms */
        .delay_rts_after_send  = 1,               /* ms */
    };

    if (ioctl(fd, TIOCSRS485, &rs485conf) < 0) {
        perror("TIOCSRS485");
        return -1;
    }
    return 0;
}

int main(void)
{
    int fd = open("/dev/ttyS3", O_RDWR | O_NOCTTY);
    if (fd < 0) { perror("open"); return 1; }

    struct termios tty;
    tcgetattr(fd, &tty);
    cfmakeraw(&tty);
    cfsetspeed(&tty, B9600);
    tty.c_cflag |= CS8 | CLOCAL | CREAD;
    tcsetattr(fd, TCSANOW, &tty);

    if (uart_enable_rs485(fd) == 0)
        printf("RS-485 mode active\n");

    const char cmd[] = { 0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B };
    write(fd, cmd, sizeof(cmd));

    usleep(50000); /* wait for slave response */
    uint8_t resp[16];
    ssize_t n = read(fd, resp, sizeof(resp));
    printf("Response: %zd bytes\n", n);

    close(fd);
    return 0;
}
```

---

### C++ Class Wrapper

```cpp
/**
 * SerialPort.hpp — RAII C++ wrapper around a POSIX serial port
 */
#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

class SerialPort {
public:
    SerialPort(const std::string& device, speed_t baud)
        : fd_(-1)
    {
        fd_ = open(device.c_str(), O_RDWR | O_NOCTTY | O_SYNC);
        if (fd_ < 0)
            throw std::runtime_error("Cannot open " + device);

        struct termios tty{};
        tcgetattr(fd_, &tty);
        cfmakeraw(&tty);
        cfsetspeed(&tty, baud);
        tty.c_cflag |= CS8 | CLOCAL | CREAD;
        tty.c_cc[VMIN]  = 0;
        tty.c_cc[VTIME] = 10;  /* 1 s timeout */

        if (tcsetattr(fd_, TCSANOW, &tty) != 0)
            throw std::runtime_error("tcsetattr failed");
    }

    ~SerialPort() {
        if (fd_ >= 0) close(fd_);
    }

    /* Non-copyable */
    SerialPort(const SerialPort&)            = delete;
    SerialPort& operator=(const SerialPort&) = delete;

    /* Movable */
    SerialPort(SerialPort&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }

    ssize_t write(const std::vector<uint8_t>& data) {
        return ::write(fd_, data.data(), data.size());
    }

    std::vector<uint8_t> read(size_t max_bytes) {
        std::vector<uint8_t> buf(max_bytes);
        ssize_t n = ::read(fd_, buf.data(), max_bytes);
        if (n < 0) n = 0;
        buf.resize(static_cast<size_t>(n));
        return buf;
    }

    void flush() { tcflush(fd_, TCIOFLUSH); }

private:
    int fd_;
};

/* Usage example */
/*
int main() {
    SerialPort sp("/dev/ttyAMA0", B115200);
    sp.write({ 'H','i','\r','\n' });
    auto resp = sp.read(64);
    // process resp …
}
*/
```

---

### Reading Device Tree Properties Programmatically (C)

```c
/**
 * dt_read.c — Read UART DT properties from /proc/device-tree at runtime.
 * Useful for self-configuring daemons.
 */
#include <dirent.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>   /* ntohl */

/* Read a u32 property from a DT node directory */
static int dt_read_u32(const char *node_path, const char *prop,
                        uint32_t *out)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", node_path, prop);

    int fd = open(path, O_RDONLY);
    if (fd < 0) return -1;

    uint32_t be;
    ssize_t n = read(fd, &be, sizeof(be));
    close(fd);
    if (n != sizeof(be)) return -1;

    *out = ntohl(be);   /* DT stores multi-byte values big-endian */
    return 0;
}

int main(void)
{
    /* Walk /proc/device-tree looking for serial nodes */
    const char *dt_root = "/proc/device-tree";
    DIR *dir = opendir(dt_root);
    if (!dir) { perror("opendir"); return 1; }

    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL) {
        if (strncmp(ent->d_name, "serial@", 7) != 0 &&
            strncmp(ent->d_name, "uart@",   5) != 0)
            continue;

        char node[512];
        snprintf(node, sizeof(node), "%s/%s", dt_root, ent->d_name);
        printf("Node: %s\n", node);

        uint32_t freq = 0;
        if (dt_read_u32(node, "clock-frequency", &freq) == 0)
            printf("  clock-frequency: %u Hz\n", freq);

        uint32_t fifo = 0;
        if (dt_read_u32(node, "fifo-size", &fifo) == 0)
            printf("  fifo-size: %u\n", fifo);
    }
    closedir(dir);
    return 0;
}
```

---

## Runtime Interaction from Rust

### Dependencies (`Cargo.toml`)

```toml
[package]
name    = "uart-dt-demo"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4"          # cross-platform serial port abstraction
nix        = { version = "0.27", features = ["term", "ioctl"] }
anyhow     = "1"
```

---

### Opening and Configuring a UART (Rust)

```rust
//! uart_config.rs — Configure and use a UART on Linux
//! Matches what the DT node declares: 115200 8N1

use anyhow::{Context, Result};
use serialport::{DataBits, FlowControl, Parity, SerialPort, StopBits};
use std::io::{Read, Write};
use std::time::Duration;

fn open_uart(device: &str, baud: u32) -> Result<Box<dyn SerialPort>> {
    let port = serialport::new(device, baud)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(1000))
        .open()
        .with_context(|| format!("Failed to open {device}"))?;

    Ok(port)
}

fn main() -> Result<()> {
    let mut port = open_uart("/dev/ttyAMA0", 115_200)?;

    let msg = b"Hello from Rust\r\n";
    port.write_all(msg)
        .context("Write failed")?;
    port.flush()?;

    let mut buf = vec![0u8; 64];
    match port.read(&mut buf) {
        Ok(n) if n > 0 => {
            let s = String::from_utf8_lossy(&buf[..n]);
            println!("Received: {s}");
        }
        Ok(_)  => println!("No data received (timeout)"),
        Err(e) => eprintln!("Read error: {e}"),
    }
    Ok(())
}
```

---

### RS-485 via Raw `ioctl` (Rust + `nix`)

```rust
//! rs485_ioctl.rs — Enable RS-485 half-duplex mode using the Linux serial_rs485 ioctl
use anyhow::{bail, Result};
use std::os::fd::AsRawFd;
use std::fs::OpenOptions;

/// Mirror of Linux's `serial_rs485` struct (linux/serial.h)
#[repr(C)]
struct SerialRs485 {
    flags:                  u32,
    delay_rts_before_send:  u32,
    delay_rts_after_send:   u32,
    _padding:               [u32; 5],
}

const SER_RS485_ENABLED:      u32 = 1 << 0;
const SER_RS485_RTS_ON_SEND:  u32 = 1 << 1;

// ioctl number for TIOCSRS485 = _IOWR('T', 0x2F, struct serial_rs485)
// On ARM/ARM64: IOC_WRITE=1, IOC_READ=2, NRBITS=8, TYPEBITS=8, SIZEBITS=14
const TIOCSRS485: u64 = 0x542F;

fn enable_rs485(fd: std::os::unix::io::RawFd) -> Result<()> {
    let mut rs485 = SerialRs485 {
        flags: SER_RS485_ENABLED | SER_RS485_RTS_ON_SEND,
        delay_rts_before_send: 1,
        delay_rts_after_send:  1,
        _padding: [0u32; 5],
    };

    let ret = unsafe {
        libc::ioctl(fd, TIOCSRS485, &mut rs485 as *mut SerialRs485)
    };
    if ret < 0 {
        bail!("TIOCSRS485 ioctl failed: {}", std::io::Error::last_os_error());
    }
    Ok(())
}

fn main() -> Result<()> {
    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .open("/dev/ttyS3")?;

    enable_rs485(file.as_raw_fd())?;
    println!("RS-485 enabled");

    // Configure baud rate with termios …
    Ok(())
}
```

---

### Reading Device Tree Properties (Rust)

```rust
//! dt_reader.rs — Parse UART DT properties from /proc/device-tree
use anyhow::Result;
use std::fs;
use std::path::Path;

/// Read a big-endian u32 property from a DT node
fn dt_read_u32(node: &Path, prop: &str) -> Option<u32> {
    let bytes = fs::read(node.join(prop)).ok()?;
    if bytes.len() < 4 { return None; }
    Some(u32::from_be_bytes(bytes[..4].try_into().ok()?))
}

/// Read a null-terminated string property
fn dt_read_string(node: &Path, prop: &str) -> Option<String> {
    let bytes = fs::read(node.join(prop)).ok()?;
    let s = bytes.iter().take_while(|&&b| b != 0).cloned().collect::<Vec<_>>();
    String::from_utf8(s).ok()
}

fn main() -> Result<()> {
    let dt = Path::new("/proc/device-tree");

    for entry in fs::read_dir(dt)? {
        let entry = entry?;
        let name  = entry.file_name().into_string().unwrap_or_default();

        if !name.starts_with("serial@") && !name.starts_with("uart@") {
            continue;
        }

        let node = entry.path();
        println!("Node: {name}");

        if let Some(compat) = dt_read_string(&node, "compatible") {
            println!("  compatible:       {compat}");
        }
        if let Some(freq) = dt_read_u32(&node, "clock-frequency") {
            println!("  clock-frequency:  {freq} Hz");
        }
        if let Some(fifo) = dt_read_u32(&node, "fifo-size") {
            println!("  fifo-size:        {fifo} bytes");
        }
        if let Some(status) = dt_read_string(&node, "status") {
            println!("  status:           {status}");
        }
    }
    Ok(())
}
```

---

## Overlays — Dynamic Device Tree Patching

Overlays (`.dtbo`) allow adding or modifying device tree nodes without rebuilding the full DTB. They are essential for SBCs like Raspberry Pi and BeagleBone where HATs/Capes add peripherals at runtime.

```dts
/* uart2-overlay.dts — Adds UART2 to an existing DTB */
/dts-v1/;
/plugin/;

&uart2 {
    pinctrl-names = "default";
    pinctrl-0     = <&uart2_pins>;
    status        = "okay";
};

&pinctrl {
    uart2_pins: uart2-pins {
        pins  = "PB8", "PB9";
        function = <6>;   /* UART2 ALT */
        bias-pull-up;
    };
};
```

Compile and load at runtime:

```bash
# Compile the overlay
dtc -@ -I dts -O dtb -o uart2-overlay.dtbo uart2-overlay.dts

# Load via configfs (Linux ≥ 4.4)
mkdir /sys/kernel/config/device-tree/overlays/uart2
cp uart2-overlay.dtbo /sys/kernel/config/device-tree/overlays/uart2/dtbo
echo 1 > /sys/kernel/config/device-tree/overlays/uart2/status
```

---

## Debugging Device Tree Issues

### Inspect Compiled DT at Runtime

```bash
# Dump the live device tree back to DTS
dtc -I fs -O dts /proc/device-tree 2>/dev/null | grep -A30 "serial@"

# Check if your UART node is present
ls /proc/device-tree/serial@40006000/

# Verify the kernel matched a driver
cat /proc/device-tree/serial@40006000/compatible

# Check the device node was created
ls -l /dev/ttyS*
ls -l /dev/ttyAMA*
```

### Check Driver Binding

```bash
# List registered platform drivers
ls /sys/bus/platform/drivers/ | grep uart
ls /sys/bus/platform/drivers/ | grep serial

# Check if device is bound to a driver
cat /sys/bus/platform/devices/40006000.serial/driver

# Kernel log at boot (DT probe messages)
dmesg | grep -i "serial\|uart\|tty"
```

### Common Problems and Fixes

| Symptom | Likely Cause | Fix |
|---|---|---|
| No `/dev/ttyXX` node | Driver not probed | Check `compatible` string against kernel driver |
| `ENODEV` on open | `status = "disabled"` | Set `status = "okay"` in board `.dts` |
| Garbage characters | Wrong baud / clock | Verify `clock-frequency` matches hardware |
| Kernel panic at boot | Bad `reg` or `interrupts` | Cross-check datasheet and errata |
| RS-485 stuck TX | Missing `rts-gpios` | Add GPIO specifier and check DT binding |
| UART works, DMA doesn't | Wrong DMA request lines | Verify `dmas` against SoC TRM |

---

## Summary

Device tree configuration is the foundational step for any UART peripheral in embedded Linux. Rather than hardcoding addresses, interrupts, and clocks in the kernel source, the device tree externalises this information into a portable, human-readable description.

**Key takeaways:**

- A UART DT node must have at minimum `compatible`, `reg`, `interrupts`, and `status = "okay"`. Everything else (clocks, pinctrl, DMA, RS-485) is added incrementally.
- The `compatible` string is the binding between hardware description and kernel driver. It must exactly match what the driver registers in `of_device_id`.
- SoC vendors provide base `.dtsi` files with peripherals set to `"disabled"`. Board-level `.dts` files enable them with `&uart_label { status = "okay"; }` overlays.
- Pin multiplexing (`pinctrl`) and clock configuration are almost always required on modern SoCs and must be declared alongside the UART node.
- DT overlays allow runtime modification, enabling plug-and-play peripherals on development boards without rebuilding the full DTB.
- Once the kernel probes the device, userspace accesses it via a standard POSIX tty interface. Both **C/C++** (`termios`, `ioctl`) and **Rust** (`serialport` crate + `nix` for low-level ioctls) work identically against the resulting `/dev/ttyXXX` character device.
- The `/proc/device-tree` virtual filesystem exposes the live DT, enabling runtime introspection from both shell scripts and application code.

Mastering device tree UART declarations enables confident board bring-up, clean driver separation, and portable, maintainable embedded Linux BSPs.