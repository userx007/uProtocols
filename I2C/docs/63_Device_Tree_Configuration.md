# 63. Device Tree Configuration

**Structure & Concepts**
- DTS/DTSI/DTB/DTBO file types and compilation commands
- I2C bus node anatomy with all essential properties (`compatible`, `reg`, `clock-frequency`, `#address-cells`, `#size-cells`)
- Full property reference tables for both bus-level and device-level nodes

**7 Real-World Device Examples**
LM75 temperature sensor, DS3231 RTC, AT24 EEPROM, PCA9555 GPIO expander, MPU-6050 IMU, SSD1306 OLED display, and a multi-device bus example

**Advanced Topics**
- Device Tree Overlays (DTBO) for runtime device injection
- I2C multiplexer (PCA9548) with virtual child buses
- Pinmux/pinctrl integration and regulator power dependencies

**C/C++ Code Examples**
1. Raw userspace I2C via `/dev/i2c-N` with `I2C_RDWR` ioctl
2. Reading DT properties from `/sys/firmware/devicetree/` sysfs
3. Full in-kernel I2C driver with `of_match_table`, GPIO, regulator, and IRQ from DT
4. DTB parsing with `libfdt`

**Rust Code Examples**
1. Userspace I2C via `linux-embedded-hal` and the `embedded-hal` trait
2. Pure-Rust DTB parsing with the `fdt` crate
3. Rust kernel driver using the in-tree Linux Rust API (6.1+)
4. Sysfs DT property reading in safe Rust

**Debugging section** covers `i2cdetect`, `i2cget`, `i2cdump`, `dmesg` patterns, and a troubleshooting table for common errors.

## Describing I2C Buses and Devices in Device Tree for Linux-Based Systems

---

## Table of Contents

1. [Introduction](#introduction)
2. [Device Tree Fundamentals](#device-tree-fundamentals)
3. [I2C Bus Node Structure](#i2c-bus-node-structure)
4. [I2C Device Node Properties](#i2c-device-node-properties)
5. [Common I2C Device Examples](#common-i2c-device-examples)
6. [Advanced Topics](#advanced-topics)
7. [Programming: Reading and Using Device Tree from C/C++](#programming-readingusing-device-tree-from-cc)
8. [Programming: Reading and Using Device Tree from Rust](#programming-readingusing-device-tree-from-rust)
9. [Kernel Driver Integration](#kernel-driver-integration)
10. [Debugging and Verification](#debugging-and-verification)
11. [Summary](#summary)

---

## Introduction

The **Device Tree (DT)** is a data structure that describes hardware to the Linux kernel in a platform-independent way. Instead of hard-coding hardware details inside kernel source code, Device Tree Source (DTS) files declare the physical topology of a board — including I2C controllers, their bus frequencies, and the peripheral devices hanging off each bus.

This separation of hardware description from driver logic is critical for embedded Linux systems (such as those built on ARM SoCs, RISC-V, or PowerPC), where the same Linux kernel image must boot on many different hardware variants.

**Why Device Tree for I2C?**

- I2C devices are not self-describing — unlike USB or PCI, an I2C slave cannot announce itself to the host
- The kernel needs to know device addresses, interrupt lines, voltage rails, and device-specific properties at boot time
- Device Tree provides a structured, maintainable, and upstream-compatible way to supply this information

---

## Device Tree Fundamentals

### File Types

| Extension | Purpose |
|-----------|---------|
| `.dts`    | Device Tree Source — human-readable source for a complete board |
| `.dtsi`   | Device Tree Source Include — reusable fragments (SoC, common peripherals) |
| `.dtb`    | Device Tree Blob — compiled binary passed to the kernel by the bootloader |
| `.dtbo`   | Device Tree Blob Overlay — dynamically loadable overlay for add-on hardware |

### Compilation

```bash
# Compile DTS to DTB
dtc -I dts -O dtb -o my_board.dtb my_board.dts

# Decompile DTB back to DTS (for inspection)
dtc -I dtb -O dts -o my_board.dts my_board.dtb

# Using the kernel's build system
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- dtbs
```

### Node Syntax

```dts
node-name@unit-address {
    compatible = "vendor,device";
    reg = <address size>;
    property-name = <value>;
    string-property = "value";
    boolean-property;              /* presence alone means true */
    child-node@0 {
        /* nested node */
    };
};
```

---

## I2C Bus Node Structure

An I2C controller on an SoC is represented as a node in the Device Tree. The node lives under the SoC or platform bus and carries properties that configure the controller hardware.

### Minimal I2C Bus Node

```dts
i2c0: i2c@40003000 {
    compatible = "vendor,soc-i2c";          /* matches a kernel driver */
    reg = <0x40003000 0x1000>;              /* MMIO base address, size */
    interrupts = <0 53 4>;                  /* GIC SPI 53, level-high */
    clocks = <&apb_clk>;                    /* clock source */
    clock-frequency = <100000>;             /* standard mode: 100 kHz */
    #address-cells = <1>;                   /* child I2C address width */
    #size-cells = <0>;                      /* I2C has no size concept */
    status = "okay";
};
```

### Key Bus-Level Properties

| Property | Type | Description |
|----------|------|-------------|
| `compatible` | string-list | Matches the kernel driver via `of_match_table` |
| `reg` | cells | MMIO register range of the I2C controller |
| `interrupts` | cells | Interrupt specifier (controller, number, flags) |
| `clocks` | phandle | Clock supplying the I2C peripheral |
| `clock-frequency` | u32 | SCL frequency in Hz (100000 / 400000 / 1000000 / 3400000) |
| `#address-cells` | u32 | **Must be `<1>`** for I2C buses |
| `#size-cells` | u32 | **Must be `<0>`** for I2C buses |
| `status` | string | `"okay"` to enable, `"disabled"` to disable |

### Clock Frequency Values

```dts
clock-frequency = <100000>;    /* Standard Mode (Sm)       100 kHz  */
clock-frequency = <400000>;    /* Fast Mode (Fm)           400 kHz  */
clock-frequency = <1000000>;   /* Fast-Mode Plus (Fm+)      1 MHz   */
clock-frequency = <3400000>;   /* High-Speed Mode (Hs)    3.4 MHz   */
```

### Multi-Bus Example (Raspberry Pi Style)

```dts
/ {
    soc {
        i2c0: i2c@7e205000 {
            compatible = "brcm,bcm2835-i2c";
            reg = <0x7e205000 0x200>;
            interrupts = <2 21>;
            clocks = <&clk_core>;
            clock-frequency = <100000>;
            #address-cells = <1>;
            #size-cells = <0>;
            status = "disabled";            /* disabled by default in SoC .dtsi */
        };

        i2c1: i2c@7e804000 {
            compatible = "brcm,bcm2835-i2c";
            reg = <0x7e804000 0x200>;
            interrupts = <2 21>;
            clocks = <&clk_core>;
            clock-frequency = <400000>;     /* fast mode on bus 1 */
            #address-cells = <1>;
            #size-cells = <0>;
            status = "disabled";
        };
    };
};
```

In the board-level `.dts` or overlay, the bus is enabled and devices are populated:

```dts
&i2c1 {
    status = "okay";
    /* I2C devices go here */
};
```

---

## I2C Device Node Properties

Every I2C peripheral is a child node of its bus node. The minimum required properties are `compatible` and `reg` (the 7-bit I2C address).

### Standard Device Properties

| Property | Type | Description |
|----------|------|-------------|
| `compatible` | string-list | Driver binding string(s); most-specific first |
| `reg` | u32 | 7-bit I2C address (0x00–0x77) |
| `interrupts` | cells | Interrupt line from the device (if any) |
| `interrupt-parent` | phandle | Interrupt controller the IRQ is wired to |
| `vcc-supply` | phandle | Power regulator reference |
| `reset-gpios` | phandle+specifier | GPIO used to reset the device |
| `wakeup-source` | boolean | Device can wake the system from sleep |
| `clock-frequency` | u32 | Override bus frequency for this device |

### Generic Device Node Template

```dts
&i2c1 {
    status = "okay";

    my_device: my-device@48 {
        compatible = "vendor,device-name";
        reg = <0x48>;                               /* 7-bit I2C address */
        interrupt-parent = <&gpio>;
        interrupts = <17 IRQ_TYPE_EDGE_FALLING>;
        vcc-supply = <&reg_3v3>;
        reset-gpios = <&gpio 12 GPIO_ACTIVE_LOW>;
    };
};
```

### 10-Bit Addressing

For devices using 10-bit I2C addresses, use the I2C 10-bit address notation:

```dts
&i2c0 {
    status = "okay";

    device_10bit@200 {
        compatible = "vendor,device";
        reg = <0x200>;    /* 10-bit address; value > 0x77 signals 10-bit mode */
    };
};
```

---

## Common I2C Device Examples

### 1. Temperature Sensor (LM75 / TMP102)

```dts
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    temperature_sensor: lm75@48 {
        compatible = "national,lm75", "lm75";
        reg = <0x48>;                        /* A0=A1=A2=GND → address 0x48 */
        interrupt-parent = <&gpio>;
        interrupts = <4 IRQ_TYPE_LEVEL_LOW>; /* OS/INT pin */
        #thermal-sensor-cells = <0>;
    };
};
```

### 2. Real-Time Clock (DS3231)

```dts
&i2c0 {
    status = "okay";

    rtc: ds3231@68 {
        compatible = "maxim,ds3231";
        reg = <0x68>;
        interrupt-parent = <&gpio>;
        interrupts = <11 IRQ_TYPE_EDGE_FALLING>; /* INT/SQW pin */
        wakeup-source;
    };
};
```

### 3. EEPROM (AT24C256)

```dts
&i2c1 {
    status = "okay";

    eeprom: at24@50 {
        compatible = "atmel,24c256";
        reg = <0x50>;
        pagesize = <64>;                     /* vendor-specific: page write size */
        read-only;                           /* optional: mount as read-only */
    };
};
```

### 4. GPIO Expander (PCA9555)

```dts
&i2c0 {
    status = "okay";

    gpio_expander: pca9555@20 {
        compatible = "nxp,pca9555";
        reg = <0x20>;
        interrupt-parent = <&gpio>;
        interrupts = <8 IRQ_TYPE_LEVEL_LOW>;
        gpio-controller;
        #gpio-cells = <2>;
        interrupt-controller;
        #interrupt-cells = <2>;
    };
};
```

The `gpio-controller` and `#gpio-cells = <2>` bindings expose the expander's pins to the rest of the Device Tree:

```dts
/* Referencing an expander GPIO in another node */
led@0 {
    gpios = <&gpio_expander 3 GPIO_ACTIVE_HIGH>;
};
```

### 5. Accelerometer (MPU-6050)

```dts
&i2c1 {
    status = "okay";

    imu: mpu6050@68 {
        compatible = "invensense,mpu6050";
        reg = <0x68>;
        interrupt-parent = <&gpio>;
        interrupts = <25 IRQ_TYPE_EDGE_RISING>;
        mount-matrix =                       /* rotation matrix for sensor orientation */
            "1",  "0", "0",
            "0", "-1", "0",
            "0",  "0", "1";
    };
};
```

### 6. Display (SSD1306 OLED via I2C)

```dts
&i2c1 {
    status = "okay";

    oled_display: ssd1306@3c {
        compatible = "solomon,ssd1306fb-i2c";
        reg = <0x3c>;
        solomon,height = <64>;
        solomon,width  = <128>;
        solomon,page-offset = <0>;
        reset-gpios = <&gpio 24 GPIO_ACTIVE_LOW>;
        vbat-supply  = <&reg_5v>;
    };
};
```

### 7. Multiple Devices on One Bus

```dts
&i2c1 {
    status = "okay";
    clock-frequency = <400000>;

    /* Temperature sensor at 0x48 */
    temp: lm75@48 {
        compatible = "national,lm75";
        reg = <0x48>;
    };

    /* RTC at 0x68 */
    rtc: ds1307@68 {
        compatible = "dallas,ds1307";
        reg = <0x68>;
    };

    /* 256Kbit EEPROM at 0x50 */
    eeprom: at24@50 {
        compatible = "atmel,24c256";
        reg = <0x50>;
        pagesize = <64>;
    };
};
```

---

## Advanced Topics

### Device Tree Overlays (DTBO)

Overlays allow adding I2C devices at runtime without recompiling the full Device Tree — especially useful for add-on hardware like Raspberry Pi HATs.

**Writing an overlay:**

```dts
/dts-v1/;
/plugin/;

#include <dt-bindings/gpio/gpio.h>
#include <dt-bindings/interrupt-controller/irq.h>

/ {
    compatible = "raspberrypi,4-model-b", "brcm,bcm2711";

    fragment@0 {
        target = <&i2c1>;
        __overlay__ {
            status = "okay";
            #address-cells = <1>;
            #size-cells = <0>;

            bme280: bme280@76 {
                compatible = "bosch,bme280";
                reg = <0x76>;
            };
        };
    };
};
```

**Loading an overlay at runtime:**

```bash
# Compile overlay
dtc -@ -I dts -O dtb -o bme280.dtbo bme280-overlay.dts

# Apply overlay (Linux 4.4+)
mkdir /sys/kernel/config/device-tree/overlays/bme280
cp bme280.dtbo /sys/kernel/config/device-tree/overlays/bme280/dtbo

# Remove overlay
rmdir /sys/kernel/config/device-tree/overlays/bme280
```

### I2C Multiplexer (PCA9548)

An I2C mux creates child buses; each segment is a sub-node with its own `#address-cells` and `#size-cells`:

```dts
&i2c0 {
    status = "okay";

    i2c_mux: pca9548@70 {
        compatible = "nxp,pca9548";
        reg = <0x70>;
        #address-cells = <1>;
        #size-cells = <0>;
        reset-gpios = <&gpio 5 GPIO_ACTIVE_LOW>;

        i2c@0 {
            #address-cells = <1>;
            #size-cells = <0>;
            reg = <0>;                      /* mux channel 0 */

            sensor_ch0: lm75@48 {
                compatible = "national,lm75";
                reg = <0x48>;
            };
        };

        i2c@1 {
            #address-cells = <1>;
            #size-cells = <0>;
            reg = <1>;                      /* mux channel 1 */

            sensor_ch1: lm75@48 {
                compatible = "national,lm75";
                reg = <0x48>;               /* same address, different channel */
            };
        };
    };
};
```

### Pinmux/Pinctrl Integration

Many SoCs require declaring pin multiplexing alongside the I2C node:

```dts
/* In the pinctrl node (SoC .dtsi) */
pinctrl_i2c1: i2c1grp {
    fsl,pins = <
        MX6QDL_PAD_KEY_COL3__I2C2_SCL  0x4001b8b1
        MX6QDL_PAD_KEY_ROW3__I2C2_SDA  0x4001b8b1
    >;
};

/* In the board .dts */
&i2c1 {
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_i2c1>;
    clock-frequency = <400000>;
    status = "okay";
};
```

### Regulator and Power Dependencies

```dts
regulators {
    reg_3v3: regulator-3v3 {
        compatible = "regulator-fixed";
        regulator-name = "3v3";
        regulator-min-microvolt = <3300000>;
        regulator-max-microvolt = <3300000>;
        regulator-always-on;
    };
};

&i2c1 {
    status = "okay";

    sensor@48 {
        compatible = "ti,tmp117";
        reg = <0x48>;
        vcc-supply = <&reg_3v3>;            /* sensor powered from 3.3V rail */
    };
};
```

---

## Programming: Reading/Using Device Tree from C/C++

While Device Tree is consumed by the Linux kernel automatically, userspace programs often need to interact with DT-configured I2C devices via the kernel's sysfs/i2cdev interfaces or write kernel drivers that parse DT properties.

### 1. Userspace I2C Access via `/dev/i2c-N`

The kernel's `i2c-dev` module exposes each I2C bus as `/dev/i2c-N`. Userspace programs open this device and perform raw transfers.

```c
/* i2c_userspace.c — raw I2C read/write via /dev/i2c-N */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <errno.h>
#include <string.h>

#define I2C_BUS     "/dev/i2c-1"
#define LM75_ADDR   0x48

/* Read a 16-bit register from LM75 temperature sensor */
int lm75_read_temp(int fd, float *celsius) {
    uint8_t reg  = 0x00;    /* temperature register */
    uint8_t buf[2];

    /* Combined write-then-read using I2C_RDWR ioctl */
    struct i2c_msg msgs[2] = {
        {
            .addr  = LM75_ADDR,
            .flags = 0,             /* write */
            .len   = 1,
            .buf   = &reg,
        },
        {
            .addr  = LM75_ADDR,
            .flags = I2C_M_RD,      /* read */
            .len   = 2,
            .buf   = buf,
        },
    };

    struct i2c_rdwr_ioctl_data data = {
        .msgs  = msgs,
        .nmsgs = 2,
    };

    if (ioctl(fd, I2C_RDWR, &data) < 0) {
        perror("I2C_RDWR");
        return -1;
    }

    /* LM75 temperature: 9-bit two's complement, MSB first, 0.5°C LSB */
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    raw >>= 7;                     /* right-align 9-bit value */
    *celsius = raw * 0.5f;
    return 0;
}

int main(void) {
    int fd = open(I2C_BUS, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", I2C_BUS, strerror(errno));
        return EXIT_FAILURE;
    }

    float temp;
    if (lm75_read_temp(fd, &temp) == 0)
        printf("Temperature: %.1f °C\n", temp);

    close(fd);
    return EXIT_SUCCESS;
}
```

### 2. Querying Device Tree from Userspace (`/sys/firmware/devicetree/`)

The kernel exposes the live Device Tree under `/sys/firmware/devicetree/base/`. Each node is a directory; properties are files.

```c
/* dt_query.c — read DT properties from sysfs */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>  /* ntohl */

#define DT_BASE "/sys/firmware/devicetree/base"

/* Read a big-endian u32 DT property */
static int dt_read_u32(const char *path, uint32_t *out) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;

    uint32_t val;
    if (fread(&val, sizeof(val), 1, f) != 1) {
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = ntohl(val);   /* DT stores integers in big-endian */
    return 0;
}

/* Read a DT string property */
static int dt_read_string(const char *path, char *buf, size_t len) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    size_t n = fread(buf, 1, len - 1, f);
    buf[n] = '\0';
    fclose(f);
    return 0;
}

int main(void) {
    /* Check the compatible string of I2C bus 1 */
    char compat[128];
    if (dt_read_string(DT_BASE "/soc/i2c@7e804000/compatible",
                       compat, sizeof(compat)) == 0) {
        printf("I2C bus compatible: %s\n", compat);
    }

    /* Read clock-frequency */
    uint32_t freq;
    if (dt_read_u32(DT_BASE "/soc/i2c@7e804000/clock-frequency",
                    &freq) == 0) {
        printf("I2C clock-frequency: %u Hz\n", freq);
    }

    /* List I2C child devices */
    printf("\nDT I2C bus children:\n");
    system("ls " DT_BASE "/soc/i2c@7e804000/");

    return 0;
}
```

### 3. Writing a Kernel Driver with DT Support (C)

This is the canonical pattern for an in-kernel I2C driver that uses Device Tree for configuration.

```c
/* my_sensor_driver.c — kernel I2C driver with Device Tree binding */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>

struct my_sensor {
    struct i2c_client   *client;
    struct gpio_desc    *reset_gpio;
    struct regulator    *vcc;
    int                  irq;
};

/* ----- sysfs: expose temperature reading ----- */
static ssize_t temperature_show(struct device *dev,
                                 struct device_attribute *attr, char *buf)
{
    struct my_sensor *sensor = dev_get_drvdata(dev);
    int raw;

    /* Read 16-bit register 0x00 */
    raw = i2c_smbus_read_word_swapped(sensor->client, 0x00);
    if (raw < 0)
        return raw;

    /* Convert and format (example: 1/256 °C per LSB) */
    return sysfs_emit(buf, "%d.%02d\n", raw >> 8,
                      (raw & 0xFF) * 100 / 256);
}
static DEVICE_ATTR_RO(temperature);

static struct attribute *my_sensor_attrs[] = {
    &dev_attr_temperature.attr,
    NULL,
};
ATTRIBUTE_GROUPS(my_sensor);

/* ----- IRQ handler ----- */
static irqreturn_t my_sensor_irq(int irq, void *dev_id)
{
    struct my_sensor *sensor = dev_id;
    dev_info(&sensor->client->dev, "Alert interrupt received\n");
    return IRQ_HANDLED;
}

/* ----- Probe: called when kernel matches driver to DT node ----- */
static int my_sensor_probe(struct i2c_client *client)
{
    struct device *dev = &client->dev;
    struct my_sensor *sensor;
    int ret;

    sensor = devm_kzalloc(dev, sizeof(*sensor), GFP_KERNEL);
    if (!sensor)
        return -ENOMEM;

    sensor->client = client;
    i2c_set_clientdata(client, sensor);

    /* --- Parse DT properties --- */

    /* 1. GPIO reset line (optional) */
    sensor->reset_gpio = devm_gpiod_get_optional(dev, "reset",
                                                  GPIOD_OUT_HIGH);
    if (IS_ERR(sensor->reset_gpio))
        return dev_err_probe(dev, PTR_ERR(sensor->reset_gpio),
                             "Failed to get reset GPIO\n");

    /* 2. Voltage regulator */
    sensor->vcc = devm_regulator_get(dev, "vcc");
    if (IS_ERR(sensor->vcc))
        return dev_err_probe(dev, PTR_ERR(sensor->vcc),
                             "Failed to get vcc regulator\n");

    ret = regulator_enable(sensor->vcc);
    if (ret)
        return ret;

    /* 3. Read a custom DT property */
    u32 sample_rate;
    if (!of_property_read_u32(dev->of_node, "my,sample-rate-hz",
                               &sample_rate)) {
        dev_info(dev, "Sample rate from DT: %u Hz\n", sample_rate);
    }

    /* 4. Interrupt */
    if (client->irq > 0) {
        ret = devm_request_threaded_irq(dev, client->irq, NULL,
                                        my_sensor_irq,
                                        IRQF_TRIGGER_FALLING | IRQF_ONESHOT,
                                        dev_name(dev), sensor);
        if (ret)
            return dev_err_probe(dev, ret, "Failed to request IRQ\n");
    }

    /* 5. Apply hardware reset */
    if (sensor->reset_gpio) {
        gpiod_set_value_cansleep(sensor->reset_gpio, 1);  /* assert */
        msleep(10);
        gpiod_set_value_cansleep(sensor->reset_gpio, 0);  /* release */
        msleep(10);
    }

    dev_info(dev, "my_sensor probed at 0x%02x\n", client->addr);
    return 0;
}

static void my_sensor_remove(struct i2c_client *client)
{
    struct my_sensor *sensor = i2c_get_clientdata(client);
    regulator_disable(sensor->vcc);
}

/* ----- Device Tree match table ----- */
static const struct of_device_id my_sensor_of_match[] = {
    { .compatible = "myvendor,my-sensor-v1" },
    { .compatible = "myvendor,my-sensor-v2" },
    { /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, my_sensor_of_match);

/* ----- I2C driver structure ----- */
static struct i2c_driver my_sensor_driver = {
    .driver = {
        .name           = "my-sensor",
        .of_match_table = my_sensor_of_match,
        .dev_groups     = my_sensor_groups,
    },
    .probe  = my_sensor_probe,
    .remove = my_sensor_remove,
};
module_i2c_driver(my_sensor_driver);

MODULE_AUTHOR("Your Name <your@email.com>");
MODULE_DESCRIPTION("Example I2C sensor driver with Device Tree support");
MODULE_LICENSE("GPL");
```

**Corresponding Device Tree binding:**

```dts
&i2c1 {
    status = "okay";

    my_sensor: my-sensor@48 {
        compatible = "myvendor,my-sensor-v1";
        reg = <0x48>;
        interrupt-parent = <&gpio>;
        interrupts = <17 IRQ_TYPE_EDGE_FALLING>;
        vcc-supply = <&reg_3v3>;
        reset-gpios = <&gpio 12 GPIO_ACTIVE_LOW>;
        my,sample-rate-hz = <10>;
    };
};
```

### 4. Using `libfdt` to Parse DTB in C

`libfdt` is the reference library for navigating compiled Device Tree blobs from C userspace or bootloaders.

```c
/* parse_dtb.c — navigate a DTB using libfdt */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <libfdt.h>       /* apt install libfdt-dev */

static void *load_dtb(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    rewind(f);
    void *buf = malloc(*size);
    if (buf) fread(buf, 1, *size, f);
    fclose(f);
    return buf;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <file.dtb>\n", argv[0]);
        return 1;
    }

    size_t dtb_size;
    void *dtb = load_dtb(argv[1], &dtb_size);
    if (!dtb || fdt_check_header(dtb) != 0) {
        fprintf(stderr, "Invalid DTB\n");
        free(dtb);
        return 1;
    }

    /* Walk all nodes to find I2C child devices */
    int node;
    fdt_for_each_node_by_compatible(node, dtb, -1, "national,lm75") {
        printf("Found LM75 node: %s\n", fdt_get_name(dtb, node, NULL));

        /* Read 'reg' property (I2C address) */
        int len;
        const fdt32_t *reg = fdt_getprop(dtb, node, "reg", &len);
        if (reg && len >= (int)sizeof(fdt32_t))
            printf("  I2C address: 0x%02x\n", fdt32_to_cpu(*reg));

        /* Read parent bus clock-frequency */
        int parent = fdt_parent_offset(dtb, node);
        const fdt32_t *freq = fdt_getprop(dtb, parent,
                                           "clock-frequency", &len);
        if (freq && len >= (int)sizeof(fdt32_t))
            printf("  Bus clock: %u Hz\n", fdt32_to_cpu(*freq));
    }

    free(dtb);
    return 0;
}
```

```bash
# Compile
gcc -o parse_dtb parse_dtb.c -lfdt

# Run
./parse_dtb /boot/my_board.dtb
```

---

## Programming: Reading/Using Device Tree from Rust

### 1. Userspace I2C via `linux-i2c` Crate

```toml
# Cargo.toml
[dependencies]
linux-embedded-hal = "0.4"
embedded-hal = "1.0"
```

```rust
// src/main.rs — I2C access from Rust userspace
use linux_embedded_hal::I2cdev;
use embedded_hal::i2c::I2c;
use std::path::Path;

const LM75_ADDR: u8 = 0x48;
const TEMP_REG:  u8 = 0x00;

fn read_lm75_temperature(i2c: &mut I2cdev) -> anyhow::Result<f32> {
    let mut buf = [0u8; 2];
    // Write register address, then read 2 bytes (combined transfer)
    i2c.write_read(LM75_ADDR, &[TEMP_REG], &mut buf)?;

    // LM75: 9-bit two's complement, MSB first, 0.5 °C per LSB
    let raw = i16::from_be_bytes([buf[0], buf[1]]) >> 7;
    Ok(raw as f32 * 0.5)
}

fn main() -> anyhow::Result<()> {
    let mut i2c = I2cdev::new(Path::new("/dev/i2c-1"))?;

    let temp = read_lm75_temperature(&mut i2c)?;
    println!("Temperature: {:.1} °C", temp);
    Ok(())
}
```

### 2. Parsing Device Tree Properties from Userspace in Rust

```toml
# Cargo.toml
[dependencies]
fdt = "0.1"           # pure-Rust FDT/DTB parser
anyhow = "1"
```

```rust
// src/parse_dt.rs — parse a DTB using the `fdt` crate
use fdt::Fdt;
use std::fs;

fn main() -> anyhow::Result<()> {
    // Load the live device tree from the kernel's sysfs
    let dtb_bytes = fs::read("/sys/firmware/fdt")?;
    let fdt = Fdt::new(&dtb_bytes)?;

    println!("DT model: {}", fdt.root().model());

    // Iterate all I2C devices by compatible string
    for node in fdt.all_nodes() {
        let Some(compat) = node.compatible() else { continue };

        // Match any LM75-family temperature sensor
        if compat.all().any(|c| c.contains("lm75")) {
            let name = node.name;
            let addr = node.property("reg")
                .and_then(|p| p.as_usize())
                .unwrap_or(0);

            println!("Found LM75: {} at I2C address 0x{:02x}", name, addr);

            // Read clock-frequency from parent bus node
            if let Some(parent) = node.parent() {
                if let Some(freq) = parent.property("clock-frequency")
                    .and_then(|p| p.as_usize()) {
                    println!("  Bus frequency: {} Hz", freq);
                }
            }
        }
    }
    Ok(())
}
```

### 3. Rust Kernel Driver with Device Tree Bindings

The Linux kernel Rust API (`rust/kernel/`) provides safe abstractions for DT interaction. This example uses the in-tree Rust API (Linux 6.1+):

```rust
// drivers/i2c/my_sensor.rs — Rust kernel I2C driver with OF bindings

use kernel::prelude::*;
use kernel::i2c;
use kernel::of;
use kernel::gpio::GpioDesc;
use kernel::regulator::Regulator;

module! {
    type: MySensorDriver,
    name: "my_sensor",
    author: "Your Name",
    description: "Example I2C sensor driver in Rust",
    license: "GPL",
}

struct MySensorData {
    reset_gpio: Option<GpioDesc>,
    vcc: Regulator,
}

struct MySensorDriver;

#[vtable]
impl i2c::Driver for MySensorDriver {
    type Data = Box<MySensorData>;

    const OF_DEVICE_ID_TABLE: Option<&'static [of::DeviceId]> = Some(&[
        of::DeviceId::new("myvendor,my-sensor-v1"),
        of::DeviceId::new("myvendor,my-sensor-v2"),
    ]);

    fn probe(client: &i2c::Client, _id: Option<&i2c::DeviceId>) -> Result<Self::Data> {
        let dev = client.as_ref();

        pr_info!("my_sensor: probing device at 0x{:02x}\n", client.address());

        // Retrieve GPIO reset line from DT ("reset-gpios" property)
        let reset_gpio = dev.gpiod_get_optional("reset", GpioFlags::GPIOD_OUT_HIGH)
            .map_err(|e| {
                pr_err!("my_sensor: failed to get reset GPIO: {:?}\n", e);
                e
            })?;

        // Retrieve voltage regulator from DT ("vcc-supply" property)
        let vcc = dev.regulator_get("vcc")
            .map_err(|e| {
                pr_err!("my_sensor: failed to get vcc regulator: {:?}\n", e);
                e
            })?;
        vcc.enable()?;

        // Read custom DT property
        if let Ok(sample_rate) = dev.property_read_u32("my,sample-rate-hz") {
            pr_info!("my_sensor: sample rate from DT: {} Hz\n", sample_rate);
        }

        // Apply reset sequence
        if let Some(ref gpio) = reset_gpio {
            gpio.set_value(true);
            kernel::delay::coarse_sleep(core::time::Duration::from_millis(10));
            gpio.set_value(false);
            kernel::delay::coarse_sleep(core::time::Duration::from_millis(10));
        }

        // Simple register read: temperature register 0x00
        let mut buf = [0u8; 2];
        client.read_regs(0x00, &mut buf)?;
        let raw = i16::from_be_bytes(buf) >> 7;
        let celsius = raw as f32 * 0.5;
        pr_info!("my_sensor: initial temperature: {:.1} °C\n", celsius);

        Ok(Box::try_new(MySensorData {
            reset_gpio,
            vcc,
        })?)
    }

    fn remove(client: &i2c::Client, data: &Self::Data) {
        let _ = data.vcc.disable();
        pr_info!("my_sensor: removed device at 0x{:02x}\n", client.address());
    }
}

impl kernel::Module for MySensorDriver {
    fn init(_module: &'static ThisModule) -> Result<Self> {
        let driver = i2c::Registration::<MySensorDriver>::new_pinned(
            c_str!("my-sensor"),
            &THIS_MODULE,
        )?;
        let _ = driver;    // Kept alive by kernel registration
        Ok(MySensorDriver)
    }
}
```

### 4. Reading DT Properties Directly from the Sysfs in Rust

```rust
// src/sysfs_dt.rs — read DT properties from /sys/firmware/devicetree/
use std::{fs, path::Path};

const DT_BASE: &str = "/sys/firmware/devicetree/base";

/// Read a big-endian u32 from a DT property file
fn read_dt_u32(path: &Path) -> anyhow::Result<u32> {
    let bytes = fs::read(path)?;
    if bytes.len() < 4 {
        anyhow::bail!("Property too short");
    }
    Ok(u32::from_be_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]))
}

/// Read a null-terminated string DT property
fn read_dt_string(path: &Path) -> anyhow::Result<String> {
    let bytes = fs::read(path)?;
    let s = String::from_utf8_lossy(bytes.trim_ascii_end());
    Ok(s.trim_end_matches('\0').to_string())
}

fn main() -> anyhow::Result<()> {
    let i2c_node = Path::new(DT_BASE).join("soc/i2c@7e804000");

    // Read compatible string
    let compat = read_dt_string(&i2c_node.join("compatible"))?;
    println!("I2C bus compatible: {}", compat);

    // Read clock-frequency
    let freq = read_dt_u32(&i2c_node.join("clock-frequency"))?;
    println!("I2C clock-frequency: {} Hz ({} kHz)", freq, freq / 1000);

    // List all child devices (I2C peripherals)
    println!("\nI2C child devices:");
    for entry in fs::read_dir(&i2c_node)? {
        let entry = entry?;
        if entry.file_type()?.is_dir() {
            let name = entry.file_name();
            let child = i2c_node.join(&name).join("compatible");
            if let Ok(child_compat) = read_dt_string(&child) {
                println!("  {} ({})", name.to_string_lossy(), child_compat);
            }
        }
    }

    Ok(())
}
```

---

## Kernel Driver Integration

### Binding Documentation

Every upstream DT binding must have documentation in `Documentation/devicetree/bindings/`. Bindings are written in YAML schema format (DT Schema):

```yaml
# Documentation/devicetree/bindings/i2c/myvendor,my-sensor.yaml
%YAML 1.2
---
$id: http://devicetree.org/schemas/i2c/myvendor,my-sensor.yaml#
$schema: http://devicetree.org/meta-schemas/core.yaml#

title: MyVendor Temperature Sensor

maintainers:
  - Your Name <your@email.com>

allOf:
  - $ref: /schemas/i2c/i2c-peripheral-props.yaml#

properties:
  compatible:
    enum:
      - myvendor,my-sensor-v1
      - myvendor,my-sensor-v2

  reg:
    maxItems: 1

  interrupts:
    maxItems: 1

  vcc-supply: true

  reset-gpios:
    maxItems: 1

  my,sample-rate-hz:
    $ref: /schemas/types.yaml#/definitions/uint32
    minimum: 1
    maximum: 100
    description: Sample rate in Hz (default 10)

required:
  - compatible
  - reg

unevaluatedProperties: false

examples:
  - |
    #include <dt-bindings/gpio/gpio.h>
    #include <dt-bindings/interrupt-controller/irq.h>
    i2c {
        #address-cells = <1>;
        #size-cells = <0>;
        temperature-sensor@48 {
            compatible = "myvendor,my-sensor-v1";
            reg = <0x48>;
            interrupts = <17 IRQ_TYPE_EDGE_FALLING>;
            vcc-supply = <&reg_3v3>;
            reset-gpios = <&gpio 12 GPIO_ACTIVE_LOW>;
            my,sample-rate-hz = <10>;
        };
    };
```

**Validate binding against schema:**

```bash
make dt_binding_check \
    DT_SCHEMA_FILES=Documentation/devicetree/bindings/i2c/myvendor,my-sensor.yaml
```

### SMBus vs I2C Kernel APIs

```c
/* SMBus convenience functions (simpler, widely supported) */
s32 i2c_smbus_read_byte(client);
s32 i2c_smbus_read_byte_data(client, reg);
s32 i2c_smbus_read_word_data(client, reg);
s32 i2c_smbus_write_byte_data(client, reg, value);

/* Raw I2C transfers (full flexibility) */
int i2c_transfer(adapter, msgs, num);
int i2c_master_send(client, buf, count);
int i2c_master_recv(client, buf, count);
```

---

## Debugging and Verification

### Command-Line Tools

```bash
# List all I2C buses registered with the kernel
ls /sys/bus/i2c/devices/

# Detect devices on bus 1 (scans all 7-bit addresses)
i2cdetect -y 1

# Read a register (device 0x48, register 0x00, 2 bytes)
i2cget -y 1 0x48 0x00 w

# Write a register
i2cset -y 1 0x48 0x01 0x60 w

# Dump all registers
i2cdump -y 1 0x48

# Inspect loaded Device Tree
dtc -I fs -O dts /sys/firmware/devicetree/base 2>/dev/null | grep -A10 "i2c@"

# Check driver binding
cat /sys/bus/i2c/devices/1-0048/driver/module/name

# Kernel log for I2C activity
dmesg | grep i2c
dmesg | grep "my.sensor"
```

### Common Issues and Solutions

| Problem | Likely Cause | Solution |
|---------|-------------|----------|
| Device not in `i2cdetect` | Wrong bus, device not powered | Check bus number and power rails in DT |
| Driver not bound | `compatible` mismatch | Compare DT string vs `of_match_table` exactly |
| `EREMOTEIO` (-121) | Device NAK | Wrong I2C address, device not ready |
| `ENODEV` | Device absent | Check DT `status = "okay"` |
| `EINVAL` on `I2C_RDWR` | Wrong message flags | Verify `I2C_M_RD` flag usage |
| Clock stretching issues | Too fast SCL | Reduce `clock-frequency` in DT |

### Verifying DT at Runtime

```bash
# Full DT dump from live kernel
dtc -I fs -O dts -o /tmp/live.dts /sys/firmware/devicetree/base

# Find your I2C device in the live DT
grep -r "0x48\|lm75\|my-sensor" /sys/firmware/devicetree/base/ 2>/dev/null

# Check if overlay was applied
ls /sys/kernel/config/device-tree/overlays/

# Verify I2C device was created by kernel
ls /sys/bus/i2c/devices/
# Expected: 1-0048  (bus 1, address 0x48)
```

---

## Summary

Device Tree Configuration for I2C in Linux-based systems is the standard mechanism for describing I2C controller hardware and peripheral devices to the kernel without hard-coding platform details into driver source code.

**Key concepts covered:**

**Device Tree Structure:** I2C buses are declared as child nodes of the SoC/platform bus with `#address-cells = <1>` and `#size-cells = <0>`. I2C peripheral devices are child nodes of their bus, identified by a `compatible` string and a `reg` property holding the 7-bit I2C address.

**Essential bus properties** include `compatible` (driver binding), `reg` (MMIO base), `clock-frequency` (SCL speed), and `status` (enabled/disabled). Devices declare their I2C address via `reg`, and optionally reference interrupt lines, GPIO pins, and voltage regulators.

**Device Tree Overlays (DTBO)** enable runtime addition of I2C devices — critical for plug-in hardware — using the `configfs` overlay interface.

**Programming in C/C++:**
- Userspace access via `/dev/i2c-N` using `I2C_RDWR` ioctl with `i2c_msg` structures
- Kernel drivers bind to DT nodes via `of_match_table` and use `of_property_read_*()` functions to extract DT properties
- `libfdt` enables userspace DTB parsing with the reference C API

**Programming in Rust:**
- `linux-embedded-hal` and the `embedded-hal` I2C trait provide userspace I2C access
- The `fdt` crate parses DTB blobs from userspace in safe Rust
- The Linux kernel's in-tree Rust API (6.1+) provides safe abstractions for DT property reading, GPIO, and regulator access in kernel drivers

**Upstream compatibility** requires YAML-format binding documentation validated with `dt_binding_check`, ensuring interoperability and maintainability.

The combination of a well-written Device Tree binding, a matching kernel driver, and appropriate sysfs/ioctl interfaces provides a robust, portable, and maintainable I2C integration on any Linux-based embedded platform.

---

*Document: 63 — Device Tree Configuration | I2C Programming Series*