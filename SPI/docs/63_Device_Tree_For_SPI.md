# 63. Device Tree for SPI

**Structure & Configuration** — How SPI controller nodes and slave device nodes are declared in DTS, including all standard properties (`spi-max-frequency`, `spi-cpol`/`spi-cpha`, `cs-gpios`, bus width, CS timing delays) with a quick-reference SPI mode table.

**Advanced Topics** — Multi-device buses with GPIO chip-selects, DMA bindings for high-throughput transfers, and Device Tree Overlays for runtime hardware changes.

**C/C++ examples** — Three levels: sysfs querying via `/proc/device-tree`, full DTB parsing with `libfdt`, a complete kernel `spi_driver` using `of_property_read_*` APIs, and a userspace `spidev` program.

**Rust examples** — Sysfs-based DT property reading (no unsafe), DTB parsing with the `fdt` crate, and userspace SPI transfers using the `spidev` crate — all with proper error handling.

**Debugging** — Shell commands to inspect the live DT, validate DTS files with `dtc`, verify driver binding, and a table of common pitfalls with their fixes.

# 63. Device Tree for SPI

## Declaring SPI Devices and Configuration in Device Tree Files

---

## Table of Contents

1. [Introduction](#introduction)
2. [Device Tree Fundamentals for SPI](#device-tree-fundamentals-for-spi)
3. [SPI Controller Node](#spi-controller-node)
4. [SPI Device Nodes](#spi-device-nodes)
5. [Common SPI Device Tree Properties](#common-spi-device-tree-properties)
6. [Multiple SPI Devices on a Bus](#multiple-spi-devices-on-a-bus)
7. [SPI with DMA Support](#spi-with-dma-support)
8. [Overlays and Runtime Configuration](#overlays-and-runtime-configuration)
9. [Reading Device Tree from C/C++](#reading-device-tree-from-cc)
10. [Reading Device Tree from Rust](#reading-device-tree-from-rust)
11. [Kernel Driver Integration (C)](#kernel-driver-integration-c)
12. [Userspace SPI via Device Tree (C/C++)](#userspace-spi-via-device-tree-cc)
13. [Userspace SPI via Device Tree (Rust)](#userspace-spi-via-device-tree-rust)
14. [Debugging and Validation](#debugging-and-validation)
15. [Summary](#summary)

---

## Introduction

The **Device Tree (DT)** is a data structure that describes the hardware topology of a system to the Linux kernel. For SPI (Serial Peripheral Interface) subsystems, the Device Tree serves as the authoritative source for:

- Which SPI controller(s) exist and where they are memory-mapped
- Which SPI slave devices are attached to each bus
- Bus parameters: clock speed, chip-select polarity, SPI mode (CPOL/CPHA)
- GPIO assignments for chip-select lines
- DMA channels for high-performance transfers
- IRQ lines for interrupt-driven SPI devices

Device Tree source files use the `.dts` (source) or `.dtsi` (include) extension and are compiled into binary `.dtb` (Device Tree Blob) files by the `dtc` compiler. The bootloader (U-Boot, GRUB, etc.) passes the DTB to the kernel at boot time.

---

## Device Tree Fundamentals for SPI

A Device Tree is organized as a hierarchical tree of **nodes**. Each node represents a hardware component and contains **properties** (key-value pairs). SPI controllers and their attached devices follow a well-defined schema.

### Basic structure

```dts
/dts-v1/;

/ {
    #address-cells = <1>;
    #size-cells = <1>;

    /* SoC-level includes or inline definitions */
    soc {
        #address-cells = <1>;
        #size-cells = <1>;

        spi0: spi@40013000 {
            /* SPI controller node — detailed below */
        };
    };
};
```

The `/dts-v1/;` magic header is mandatory. Every node that contains child nodes addressing must declare `#address-cells` and `#size-cells`.

---

## SPI Controller Node

The SPI controller node describes the hardware SPI master peripheral. Its name follows the convention `spi@<base-address>`.

```dts
spi0: spi@40013000 {
    compatible = "vendor,soc-spi";       /* matches the kernel driver */
    reg = <0x40013000 0x400>;            /* MMIO base address, size */
    interrupts = <GIC_SPI 35 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&rcc SPI1_CLK>;
    clock-names = "spi_clk";

    /* Bus properties */
    #address-cells = <1>;               /* child reg = chip-select index */
    #size-cells = <0>;                  /* no size for SPI devices */

    pinctrl-names = "default", "sleep";
    pinctrl-0 = <&spi1_pins_active>;
    pinctrl-1 = <&spi1_pins_sleep>;

    /* Optional DMA */
    dmas = <&dma1 3 3 0x400 0x0>,
           <&dma1 2 3 0x400 0x0>;
    dma-names = "rx", "tx";

    status = "okay";
};
```

### Key controller properties

| Property          | Description                                         |
|-------------------|-----------------------------------------------------|
| `compatible`      | String matching the kernel driver's `of_match_table` |
| `reg`             | MMIO base address and size                          |
| `interrupts`      | Interrupt specifier (GIC/PIC dependent)             |
| `clocks`          | Clock provider reference                            |
| `#address-cells`  | Always `<1>` for SPI (chip-select number)           |
| `#size-cells`     | Always `<0>` for SPI devices                        |
| `cs-gpios`        | GPIO-based chip-select lines (optional)             |
| `num-cs`          | Number of chip-select lines supported               |

---

## SPI Device Nodes

Child nodes of the SPI controller describe each attached device. The `reg` property specifies the **chip-select index** (0, 1, 2, …).

```dts
spi0: spi@40013000 {
    compatible = "vendor,soc-spi";
    reg = <0x40013000 0x400>;
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";

    /* SPI Flash — chip-select 0 */
    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;                          /* CS0 */
        spi-max-frequency = <50000000>;     /* 50 MHz */
        spi-tx-bus-width = <1>;
        spi-rx-bus-width = <4>;             /* QSPI read */

        partitions {
            compatible = "fixed-partitions";
            #address-cells = <1>;
            #size-cells = <1>;

            partition@0 {
                label = "bootloader";
                reg = <0x0 0x80000>;        /* 512 KiB */
                read-only;
            };

            partition@80000 {
                label = "rootfs";
                reg = <0x80000 0x780000>;   /* 7.5 MiB */
            };
        };
    };
};
```

---

## Common SPI Device Tree Properties

The following properties apply to SPI device nodes and are defined by the SPI core bindings:

```dts
device@0 {
    compatible = "manufacturer,part-number";
    reg = <0>;                          /* chip-select index */
    spi-max-frequency = <10000000>;     /* max SCK frequency in Hz */

    /* SPI mode: CPOL and CPHA */
    spi-cpol;                           /* clock polarity: idle high */
    spi-cpha;                           /* clock phase: capture on 2nd edge */

    /* Bus width for multi-line SPI */
    spi-tx-bus-width = <1>;             /* 1, 2, or 4 */
    spi-rx-bus-width = <1>;             /* 1, 2, or 4 */

    /* Chip-select active low (default) or active high */
    spi-cs-high;                        /* CS is active HIGH */

    /* Byte order */
    spi-lsb-first;                      /* LSB first (default: MSB) */

    /* 3-wire (half-duplex) mode */
    spi-3wire;

    /* GPIO-based chip-select override */
    cs-gpios = <&gpio1 5 GPIO_ACTIVE_LOW>;

    /* Delays in nanoseconds */
    spi-cs-setup-delay-ns = <100>;
    spi-cs-hold-delay-ns = <50>;
    spi-cs-inactive-delay-ns = <200>;
};
```

### SPI Mode Reference

| Mode | CPOL | CPHA | DT Property              |
|------|------|------|--------------------------|
| 0    | 0    | 0    | *(no property)*          |
| 1    | 0    | 1    | `spi-cpha`               |
| 2    | 1    | 0    | `spi-cpol`               |
| 3    | 1    | 1    | `spi-cpol` + `spi-cpha`  |

---

## Multiple SPI Devices on a Bus

Multiple slave devices can coexist on the same SPI bus by using different chip-select indices:

```dts
spi1: spi@40003000 {
    compatible = "vendor,soc-spi";
    reg = <0x40003000 0x400>;
    #address-cells = <1>;
    #size-cells = <0>;
    num-cs = <3>;

    /* GPIO chip-selects for devices not using hardware CS */
    cs-gpios = <0>,                        /* CS0: hardware */
               <&gpio0 12 GPIO_ACTIVE_LOW>, /* CS1: GPIO PA12 */
               <&gpio0 13 GPIO_ACTIVE_LOW>; /* CS2: GPIO PA13 */

    status = "okay";

    /* Device 0: SPI NOR Flash on CS0 */
    flash@0 {
        compatible = "jedec,spi-nor";
        reg = <0>;
        spi-max-frequency = <80000000>;
    };

    /* Device 1: ADC on CS1 */
    adc@1 {
        compatible = "microchip,mcp3204";
        reg = <1>;
        spi-max-frequency = <1800000>;
        vref-supply = <&vcc_3v3>;
    };

    /* Device 2: Touchscreen controller on CS2 */
    touchscreen@2 {
        compatible = "ti,ads7846";
        reg = <2>;
        spi-max-frequency = <2000000>;
        spi-cpol;
        spi-cpha;
        interrupts = <&gpio1 3 IRQ_TYPE_EDGE_FALLING>;
        pendown-gpio = <&gpio1 3 GPIO_ACTIVE_LOW>;
    };
};
```

---

## SPI with DMA Support

For high-throughput applications, DMA channels are configured in the controller node:

```dts
spi2: spi@40003800 {
    compatible = "st,stm32h7-spi";
    reg = <0x40003800 0x400>;
    interrupts = <GIC_SPI 36 IRQ_TYPE_LEVEL_HIGH>;
    clocks = <&rcc SPI2_CLK>;
    #address-cells = <1>;
    #size-cells = <0>;

    /* DMA bindings — two channels: RX and TX */
    dmas = <&dmamux1 0 38 0x400 0x01>,   /* RX: stream 0, request 38 */
           <&dmamux1 1 39 0x400 0x01>;   /* TX: stream 1, request 39 */
    dma-names = "rx", "tx";

    status = "okay";

    sensor@0 {
        compatible = "bosch,bmi088";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };
};
```

---

## Overlays and Runtime Configuration

Device Tree Overlays (`.dtbo`) allow modifying the Device Tree at runtime, which is especially useful for development boards like the Raspberry Pi.

### Writing an overlay

```dts
/* spi-mcp3208.dts — overlay for MCP3208 ADC on SPI0, CS1 */
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>

/ {
    compatible = "brcm,bcm2837";

    fragment@0 {
        target = <&spi0>;               /* patch the spi0 node */
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            mcp3208: adc@1 {
                compatible = "microchip,mcp3208";
                reg = <1>;              /* CS1 */
                spi-max-frequency = <1000000>;
                #io-channel-cells = <1>;
            };
        };
    };
};
```

### Compiling and loading the overlay

```bash
# Compile overlay
dtc -@ -I dts -O dtb -o spi-mcp3208.dtbo spi-mcp3208.dts

# Load overlay at runtime (Linux 4.4+)
mkdir /sys/kernel/config/device-tree/overlays/mcp3208
cp spi-mcp3208.dtbo /sys/kernel/config/device-tree/overlays/mcp3208/dtbo

# Verify the node appeared
ls /sys/bus/spi/devices/
```

---

## Reading Device Tree from C/C++

### Querying the live DT via sysfs

```c
/* dt_spi_query.c — Read SPI device properties from /proc/device-tree */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>   /* ntohl() */

#define DT_BASE "/proc/device-tree"

/* Read a u32 property from device tree sysfs node */
static int dt_read_u32(const char *path, uint32_t *out_val)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    uint32_t be_val;
    ssize_t n = read(fd, &be_val, sizeof(be_val));
    close(fd);

    if (n != sizeof(be_val)) {
        fprintf(stderr, "Short read from %s\n", path);
        return -1;
    }

    /* DT stores integers in big-endian format */
    *out_val = ntohl(be_val);
    return 0;
}

/* Read a string property */
static int dt_read_string(const char *path, char *buf, size_t buf_sz)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror(path);
        return -1;
    }

    ssize_t n = read(fd, buf, buf_sz - 1);
    close(fd);

    if (n < 0) return -1;

    buf[n] = '\0';
    return (int)n;
}

/* Check for a boolean (empty) property */
static int dt_has_property(const char *path)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    close(fd);
    return 1;
}

int main(void)
{
    /* Example: inspect SPI device at spi0/spidev0.0 */
    const char *spi_dev_path = DT_BASE "/soc/spi@7e204000/spidev@0";

    char compatible[128] = {0};
    uint32_t max_freq = 0;

    printf("=== SPI Device Tree Query ===\n\n");

    /* Compatible string */
    char compat_path[256];
    snprintf(compat_path, sizeof(compat_path), "%s/compatible", spi_dev_path);
    if (dt_read_string(compat_path, compatible, sizeof(compatible)) >= 0)
        printf("compatible: %s\n", compatible);

    /* spi-max-frequency */
    char freq_path[256];
    snprintf(freq_path, sizeof(freq_path), "%s/spi-max-frequency", spi_dev_path);
    if (dt_read_u32(freq_path, &max_freq) == 0)
        printf("spi-max-frequency: %u Hz (%.3f MHz)\n",
               max_freq, max_freq / 1e6);

    /* SPI mode flags */
    char prop_path[256];
    snprintf(prop_path, sizeof(prop_path), "%s/spi-cpol", spi_dev_path);
    printf("spi-cpol: %s\n", dt_has_property(prop_path) ? "yes" : "no");

    snprintf(prop_path, sizeof(prop_path), "%s/spi-cpha", spi_dev_path);
    printf("spi-cpha: %s\n", dt_has_property(prop_path) ? "yes" : "no");

    snprintf(prop_path, sizeof(prop_path), "%s/spi-cs-high", spi_dev_path);
    printf("spi-cs-high: %s\n", dt_has_property(prop_path) ? "yes" : "no");

    return 0;
}
```

### Using libfdt (C) for DTB parsing

```c
/* dtb_spi_parse.c — Parse a .dtb file with libfdt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <libfdt.h>     /* apt-get install libfdt-dev */

static void *load_dtb(const char *path, size_t *out_size)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror(path); return NULL; }

    struct stat st;
    fstat(fd, &st);

    void *buf = malloc(st.st_size);
    read(fd, buf, st.st_size);
    close(fd);

    *out_size = (size_t)st.st_size;
    return buf;
}

/* Enumerate all SPI devices in the DTB */
static void enumerate_spi_devices(const void *fdt)
{
    int root = fdt_path_offset(fdt, "/");
    if (root < 0) { fprintf(stderr, "No root node\n"); return; }

    /* Walk all nodes looking for SPI controller nodes */
    int node;
    fdt_for_each_subnode(node, fdt, root)
    {
        const char *name = fdt_get_name(fdt, node, NULL);
        if (strncmp(name, "spi", 3) != 0) continue;

        printf("SPI Controller: %s\n", name);

        /* Read reg property (MMIO base address) */
        int lenp = 0;
        const uint32_t *reg =
            fdt_getprop(fdt, node, "reg", &lenp);
        if (reg && lenp >= 4)
            printf("  base addr: 0x%08X\n",
                   (unsigned)fdt32_to_cpu(reg[0]));

        /* Iterate child (device) nodes */
        int child;
        fdt_for_each_subnode(child, fdt, node)
        {
            const char *dev_name = fdt_get_name(fdt, child, NULL);

            const char *compat =
                fdt_getprop(fdt, child, "compatible", NULL);

            const uint32_t *freq_prop =
                fdt_getprop(fdt, child, "spi-max-frequency", NULL);
            uint32_t freq = freq_prop ?
                fdt32_to_cpu(*freq_prop) : 0;

            printf("  Device: %-20s  compatible=%-30s  max=%u Hz\n",
                   dev_name,
                   compat ? compat : "(none)",
                   freq);
        }
    }
}

int main(int argc, char *argv[])
{
    const char *dtb_path = (argc > 1) ? argv[1] : "/boot/bcm2710-rpi-3-b.dtb";

    size_t dtb_size = 0;
    void *fdt = load_dtb(dtb_path, &dtb_size);
    if (!fdt) return 1;

    int ret = fdt_check_header(fdt);
    if (ret < 0) {
        fprintf(stderr, "Invalid DTB: %s\n", fdt_strerror(ret));
        free(fdt);
        return 1;
    }

    printf("DTB loaded: %zu bytes, version %d\n\n",
           dtb_size, fdt_version(fdt));

    enumerate_spi_devices(fdt);

    free(fdt);
    return 0;
}
```

**Build:**
```bash
gcc -o dtb_spi_parse dtb_spi_parse.c -lfdt
```

---

## Reading Device Tree from Rust

### Querying sysfs (pure Rust, no unsafe)

```rust
// dt_spi_query.rs — Read SPI DT properties from /proc/device-tree
use std::fs;
use std::io;
use std::path::{Path, PathBuf};

/// Read a big-endian u32 property from the Device Tree sysfs interface.
fn dt_read_u32(path: &Path) -> io::Result<u32> {
    let bytes = fs::read(path)?;
    if bytes.len() < 4 {
        return Err(io::Error::new(
            io::ErrorKind::UnexpectedEof,
            "DT u32 property is too short",
        ));
    }
    // Device Tree integers are always big-endian
    let value = u32::from_be_bytes(bytes[..4].try_into().unwrap());
    Ok(value)
}

/// Read a null-terminated string property from the Device Tree.
fn dt_read_string(path: &Path) -> io::Result<String> {
    let bytes = fs::read(path)?;
    // Strip trailing NUL bytes
    let trimmed = bytes.iter().take_while(|&&b| b != 0).cloned().collect::<Vec<_>>();
    String::from_utf8(trimmed)
        .map_err(|e| io::Error::new(io::ErrorKind::InvalidData, e))
}

/// Check whether a boolean (empty) property exists.
fn dt_has_property(path: &Path) -> bool {
    path.exists()
}

#[derive(Debug)]
struct SpiDeviceInfo {
    pub compatible: String,
    pub max_frequency_hz: Option<u32>,
    pub cpol: bool,
    pub cpha: bool,
    pub cs_high: bool,
    pub lsb_first: bool,
}

fn read_spi_device(dt_path: &Path) -> io::Result<SpiDeviceInfo> {
    let compatible = dt_read_string(&dt_path.join("compatible"))?;

    let max_frequency_hz = dt_read_u32(&dt_path.join("spi-max-frequency")).ok();

    let cpol      = dt_has_property(&dt_path.join("spi-cpol"));
    let cpha      = dt_has_property(&dt_path.join("spi-cpha"));
    let cs_high   = dt_has_property(&dt_path.join("spi-cs-high"));
    let lsb_first = dt_has_property(&dt_path.join("spi-lsb-first"));

    Ok(SpiDeviceInfo {
        compatible,
        max_frequency_hz,
        cpol,
        cpha,
        cs_high,
        lsb_first,
    })
}

fn spi_mode(info: &SpiDeviceInfo) -> u8 {
    match (info.cpol, info.cpha) {
        (false, false) => 0,
        (false, true)  => 1,
        (true,  false) => 2,
        (true,  true)  => 3,
    }
}

fn main() {
    let dt_base = PathBuf::from("/proc/device-tree/soc/spi@7e204000");

    // Enumerate child device nodes
    let entries = match fs::read_dir(&dt_base) {
        Ok(e) => e,
        Err(e) => {
            eprintln!("Cannot read DT path {}: {}", dt_base.display(), e);
            return;
        }
    };

    println!("=== SPI Devices from Device Tree ===\n");

    for entry in entries.flatten() {
        let path = entry.path();
        if !path.is_dir() { continue; }

        let node_name = path.file_name()
            .and_then(|n| n.to_str())
            .unwrap_or("?");

        // Only process nodes that look like SPI slave devices (have reg property)
        if !path.join("reg").exists() { continue; }

        match read_spi_device(&path) {
            Ok(info) => {
                println!("Node:        {}", node_name);
                println!("Compatible:  {}", info.compatible);

                if let Some(freq) = info.max_frequency_hz {
                    println!("Max freq:    {} Hz ({:.3} MHz)", freq, freq as f64 / 1e6);
                }

                println!("SPI mode:    {}", spi_mode(&info));
                println!("CS polarity: {}", if info.cs_high { "active-high" } else { "active-low" });
                println!("Bit order:   {}", if info.lsb_first { "LSB first" } else { "MSB first" });
                println!();
            }
            Err(e) => eprintln!("Error reading {}: {}", node_name, e),
        }
    }
}
```

### Using the `fdt` crate for DTB parsing

```rust
// Cargo.toml
// [dependencies]
// fdt = "0.1"

// dtb_spi_parse.rs — Parse a .dtb file using the fdt crate
use fdt::Fdt;
use std::fs;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let dtb_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/boot/bcm2710-rpi-3-b.dtb".to_string());

    let dtb_bytes = fs::read(&dtb_path)?;
    let fdt = Fdt::new(&dtb_bytes)?;

    println!("DTB loaded: {} bytes", dtb_bytes.len());
    println!("Total size: {} bytes\n", fdt.total_size());

    // Find all SPI controller nodes
    for node in fdt.all_nodes() {
        let name = node.name;
        if !name.starts_with("spi@") { continue; }

        println!("SPI Controller: {}", name);

        // Read compatible property
        if let Some(compat) = node.property("compatible") {
            if let Some(s) = compat.as_str() {
                println!("  compatible: {}", s);
            }
        }

        // Read interrupt number
        if let Some(irq_prop) = node.property("interrupts") {
            let cells: Vec<u32> = irq_prop.iter_cell_size(1).collect();
            if !cells.is_empty() {
                println!("  interrupt: {}", cells[0]);
            }
        }

        // Enumerate child (slave device) nodes
        for child in node.children() {
            let dev_name = child.name;

            let compat_str = child
                .property("compatible")
                .and_then(|p| p.as_str())
                .unwrap_or("(unknown)");

            let max_freq: Option<u32> = child
                .property("spi-max-frequency")
                .and_then(|p| p.as_usize())
                .map(|v| v as u32);

            let mode_str = match (
                child.property("spi-cpol").is_some(),
                child.property("spi-cpha").is_some(),
            ) {
                (false, false) => "Mode 0",
                (false, true)  => "Mode 1",
                (true,  false) => "Mode 2",
                (true,  true)  => "Mode 3",
            };

            print!("  Device: {:<20}  {:<30}  {}",
                   dev_name, compat_str, mode_str);

            if let Some(freq) = max_freq {
                print!("  @ {} Hz", freq);
            }
            println!();
        }
        println!();
    }

    Ok(())
}
```

**Build:**
```bash
cargo build --release
```

---

## Kernel Driver Integration (C)

Kernel drivers for SPI devices use the `of_*` APIs to extract Device Tree properties at probe time. This is how a typical `spi_driver` reads its DT configuration:

```c
/* spi_example_driver.c — Kernel-space SPI driver using Device Tree */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regmap.h>

#define DRIVER_NAME "example-spi-sensor"

struct example_priv {
    struct spi_device *spi;
    struct gpio_desc  *reset_gpio;
    u32                sample_rate;
    bool               high_res_mode;
};

static int example_spi_probe(struct spi_device *spi)
{
    struct device *dev = &spi->dev;
    struct device_node *np = dev->of_node;
    struct example_priv *priv;
    int ret;

    priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
    if (!priv)
        return -ENOMEM;

    priv->spi = spi;

    /* --- Read custom DT properties --- */

    /* Required: sample-rate-hz */
    ret = of_property_read_u32(np, "sample-rate-hz", &priv->sample_rate);
    if (ret) {
        dev_err(dev, "Missing 'sample-rate-hz' in DT\n");
        return ret;
    }

    /* Optional boolean: high-resolution-mode */
    priv->high_res_mode = of_property_read_bool(np, "high-resolution-mode");

    /* Optional GPIO: reset line */
    priv->reset_gpio = devm_gpiod_get_optional(dev, "reset",
                                                GPIOD_OUT_HIGH);
    if (IS_ERR(priv->reset_gpio))
        return PTR_ERR(priv->reset_gpio);

    /* --- Configure SPI settings from DT (already applied by core) --- */
    dev_info(dev, "SPI mode: %u, max_speed: %u Hz\n",
             spi->mode, spi->max_speed_hz);
    dev_info(dev, "sample-rate: %u Hz, high-res: %s\n",
             priv->sample_rate,
             priv->high_res_mode ? "yes" : "no");

    /* Hardware reset sequence */
    if (priv->reset_gpio) {
        gpiod_set_value_cansleep(priv->reset_gpio, 0);
        usleep_range(1000, 2000);
        gpiod_set_value_cansleep(priv->reset_gpio, 1);
        msleep(10);
    }

    spi_set_drvdata(spi, priv);

    dev_info(dev, "example-spi-sensor probed successfully\n");
    return 0;
}

static void example_spi_remove(struct spi_device *spi)
{
    /* devm_* resources freed automatically */
    dev_info(&spi->dev, "example-spi-sensor removed\n");
}

/* Device Tree match table */
static const struct of_device_id example_spi_of_match[] = {
    { .compatible = "example,spi-sensor-v1" },
    { .compatible = "example,spi-sensor-v2" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, example_spi_of_match);

/* SPI ID table for non-DT boards */
static const struct spi_device_id example_spi_id[] = {
    { "spi-sensor-v1", 0 },
    { "spi-sensor-v2", 1 },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, example_spi_id);

static struct spi_driver example_spi_driver = {
    .driver = {
        .name           = DRIVER_NAME,
        .of_match_table = example_spi_of_match,
    },
    .probe  = example_spi_probe,
    .remove = example_spi_remove,
    .id_table = example_spi_id,
};

module_spi_driver(example_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("Example SPI sensor driver with DT support");
```

**Corresponding DTS snippet for this driver:**

```dts
sensor@0 {
    compatible = "example,spi-sensor-v1";
    reg = <0>;
    spi-max-frequency = <5000000>;
    spi-cpol;
    spi-cpha;

    /* Custom properties consumed by the driver */
    sample-rate-hz = <1000>;
    high-resolution-mode;

    /* GPIO: active-low reset on GPIO bank 2, pin 7 */
    reset-gpios = <&gpio2 7 GPIO_ACTIVE_LOW>;
};
```

---

## Userspace SPI via Device Tree (C/C++)

When `spidev` is declared in the Device Tree, it exposes `/dev/spidevX.Y` to userspace. The DT configuration (mode, speed) sets the defaults, which can be overridden via `ioctl`.

### DTS for spidev

```dts
spi0: spi@40013000 {
    compatible = "vendor,soc-spi";
    #address-cells = <1>;
    #size-cells = <0>;
    status = "okay";

    /* Expose SPI bus to userspace via spidev */
    spidev@0 {
        compatible = "linux,spidev";
        reg = <0>;
        spi-max-frequency = <10000000>;
    };
};
```

### Userspace C code reading DT-configured SPI device

```c
/* spi_userspace.c — Talk to spidev; DT sets defaults */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>

/* Read back the parameters the kernel applied from Device Tree */
static void print_spi_config(int fd)
{
    uint8_t  mode    = 0;
    uint8_t  bits    = 0;
    uint32_t speed   = 0;

    ioctl(fd, SPI_IOC_RD_MODE,           &mode);
    ioctl(fd, SPI_IOC_RD_BITS_PER_WORD,  &bits);
    ioctl(fd, SPI_IOC_RD_MAX_SPEED_HZ,   &speed);

    printf("=== SPI Configuration (from Device Tree) ===\n");
    printf("Mode:           SPI_%d\n", mode);
    printf("Bits per word:  %u\n", bits);
    printf("Max speed:      %u Hz (%.3f MHz)\n", speed, speed / 1e6);
}

static int spi_transfer(int fd, const uint8_t *tx, uint8_t *rx, size_t len)
{
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)tx,
        .rx_buf        = (unsigned long)rx,
        .len           = len,
        .delay_usecs   = 0,
        .speed_hz      = 0,     /* 0 = use DT-configured default */
        .bits_per_word = 0,
    };

    int ret = ioctl(fd, SPI_IOC_MESSAGE(1), &tr);
    if (ret < 0) {
        perror("SPI_IOC_MESSAGE");
        return -1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    const char *dev = (argc > 1) ? argv[1] : "/dev/spidev0.0";

    int fd = open(dev, O_RDWR);
    if (fd < 0) {
        perror(dev);
        fprintf(stderr, "Ensure the DT has spidev@0 declared.\n");
        return 1;
    }

    /* Print configuration established by Device Tree */
    print_spi_config(fd);

    /* Example: read device ID register (common pattern) */
    uint8_t tx[2] = { 0x9F, 0x00 };   /* JEDEC READ ID command */
    uint8_t rx[2] = { 0x00, 0x00 };

    printf("\nSending JEDEC READ ID (0x9F)...\n");
    if (spi_transfer(fd, tx, rx, sizeof(tx)) == 0)
        printf("Response: 0x%02X 0x%02X\n", rx[0], rx[1]);

    close(fd);
    return 0;
}
```

---

## Userspace SPI via Device Tree (Rust)

```rust
// Cargo.toml
// [dependencies]
// spidev = "0.6"

// spi_userspace.rs — Userspace SPI using DT-configured spidev
use spidev::{Spidev, SpidevOptions, SpidevTransfer, SpiModeFlags};
use std::io::{self, Read, Write};

fn print_device_info(spi: &Spidev) -> io::Result<()> {
    // The kernel has already applied DT settings at device creation.
    // We can re-read them back:
    let options = SpidevOptions::new()
        .max_speed_hz(10_000_000)  // upper bound from DT; can be lowered
        .mode(SpiModeFlags::SPI_MODE_0)
        .bits_per_word(8)
        .build();

    spi.configure(&options)?;
    println!("SPI device configured (mode from Device Tree applied).");
    Ok(())
}

/// Send a command byte and receive N response bytes (full-duplex).
fn spi_command(spi: &mut Spidev, cmd: u8, rx_len: usize)
    -> io::Result<Vec<u8>>
{
    let mut tx = vec![0u8; 1 + rx_len];
    let mut rx = vec![0u8; 1 + rx_len];
    tx[0] = cmd;

    {
        let mut transfer = SpidevTransfer::read_write(&tx, &mut rx);
        spi.transfer(&mut transfer)?;
    }

    Ok(rx[1..].to_vec())
}

/// Demonstrate reading a JEDEC RDID (0x9F) response.
fn read_jedec_id(spi: &mut Spidev) -> io::Result<()> {
    // Command 0x9F: Manufacturer (1 byte) + Device ID (2 bytes)
    let id = spi_command(spi, 0x9F, 3)?;

    println!("JEDEC ID:");
    println!("  Manufacturer:  0x{:02X}", id[0]);
    println!("  Memory type:   0x{:02X}", id[1]);
    println!("  Memory density:0x{:02X}", id[2]);

    Ok(())
}

fn main() -> io::Result<()> {
    // Path set by Device Tree: spidev@0 on spi0 → /dev/spidev0.0
    let dev_path = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "/dev/spidev0.0".to_string());

    println!("Opening DT-declared SPI device: {}", dev_path);

    let mut spi = Spidev::open(&dev_path)?;
    print_device_info(&spi)?;

    read_jedec_id(&mut spi)?;

    // Demonstrate single-byte write (e.g., write-enable)
    spi.write_all(&[0x06])?;
    println!("\nWREN (0x06) command sent.");

    // Demonstrate read (e.g., read status register)
    let status_cmd = [0x05u8, 0x00];
    let mut status_resp = [0u8; 2];
    {
        let mut xfer = SpidevTransfer::read_write(&status_cmd, &mut status_resp);
        spi.transfer(&mut xfer)?;
    }
    println!("Status register: 0x{:02X}", status_resp[1]);

    Ok(())
}
```

---

## Debugging and Validation

### Inspect the live Device Tree

```bash
# List all SPI buses
ls /sys/bus/spi/

# List all SPI devices
ls /sys/bus/spi/devices/

# Show DT node properties for SPI controller
cat /proc/device-tree/soc/spi@7e204000/compatible
xxd /proc/device-tree/soc/spi@7e204000/clock-frequency

# Pretty-print entire subtree
dtc -I fs /proc/device-tree 2>/dev/null | grep -A 30 "spi@"
```

### Validate a DTS/DTB file

```bash
# Compile DTS to DTB
dtc -I dts -O dtb -o output.dtb input.dts

# Decompile DTB back to DTS for review
dtc -I dtb -O dts -o decompiled.dts my_board.dtb

# Check for warnings (strict mode)
dtc -W all -I dts -O dtb -o output.dtb input.dts

# Verify kernel loaded the expected DTB
cat /proc/device-tree/model
```

### Check SPI device binding

```bash
# Confirm driver bound to device
cat /sys/bus/spi/devices/spi0.0/modalias

# Verify max speed applied from DT
cat /sys/bus/spi/devices/spi0.0/of_node/spi-max-frequency | xxd

# Check driver association
ls -la /sys/bus/spi/devices/spi0.0/driver
```

### Common pitfalls

| Symptom | Likely Cause | Fix |
|---|---|---|
| Device not enumerated | `status = "disabled"` | Set `status = "okay"` |
| Wrong SPI mode | Missing `spi-cpol`/`spi-cpha` | Add appropriate properties |
| CS never asserts | Wrong `reg` value | Match CS index to hardware wiring |
| Clock too fast | `spi-max-frequency` too high | Reduce to device datasheet maximum |
| Probe fails with -ENODEV | `compatible` mismatch | Match driver's `of_match_table` exactly |
| Transfer corrupted | Missing `spi-cs-setup-delay-ns` | Add CS timing delays |

---

## Summary

The Device Tree provides a hardware description layer that completely separates Linux SPI driver code from board-specific wiring. Key takeaways:

**DTS authoring** — An SPI controller node (`spi@<addr>`) uses `#address-cells = <1>` and `#size-cells = <0>`. Each SPI slave is a child node with `reg = <cs-index>` and `spi-max-frequency`. SPI mode is encoded via boolean properties (`spi-cpol`, `spi-cpha`). GPIO-based chip-selects are declared with `cs-gpios`.

**Kernel drivers** — A `spi_driver` registers an `of_match_table` with `compatible` strings. In `.probe()`, the driver uses `of_property_read_u32()`, `of_property_read_bool()`, and `devm_gpiod_get_optional()` to extract DT properties. The SPI core automatically configures mode and speed from the DT before calling `.probe()`.

**Overlays** — Device Tree Overlays (`.dtbo`) allow adding or modifying SPI devices at runtime without recompiling the base DTB, which is the standard mechanism for add-on hardware on platforms like Raspberry Pi.

**Userspace** — The `spidev` compatible exposes an SPI bus via `/dev/spidevX.Y`. Userspace programs (in C via `linux/spi/spidev.h` or in Rust via the `spidev` crate) inherit the mode and speed configured in the Device Tree and use `ioctl(SPI_IOC_MESSAGE)` for full-duplex transfers.

**Debugging** — The live DT is inspectable at `/proc/device-tree/`. The `dtc` tool compiles, decompiles, and validates DTS files. SPI device binding and DT-applied parameters can be confirmed through `/sys/bus/spi/devices/`.

By centralizing all hardware description in the Device Tree, SPI drivers remain portable across boards, and hardware variations are handled entirely through DTS file differences rather than `#ifdef` guards or platform data tables.

---

*Document: 63 — Device Tree for SPI | Linux SPI Subsystem Series*