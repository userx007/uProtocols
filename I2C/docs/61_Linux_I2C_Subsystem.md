# 61. Linux I2C Subsystem

**What's included:**

- **Protocol fundamentals** — signal lines, addressing, transaction types (write, read, combined/repeated-START), and SMBus
- **Subsystem architecture** — layered diagram showing the path from userspace `/dev/i2c-N` through i2c-core down to the hardware controller
- **Key data structures** — annotated `i2c_adapter`, `i2c_algorithm`, `i2c_msg`, `i2c_client`, and `i2c_driver`
- **Adapter & algorithm implementation** — how to write a bus controller driver with `master_xfer` and `functionality` callbacks
- **Kernel device driver (C)** — a complete, realistic BME280 sensor driver using IIO, `devm_*` APIs, SMBus helpers, and raw `i2c_transfer()` with combined messages
- **Userspace C/C++** — raw `ioctl(I2C_RDWR)`, SMBus ioctl API, and a full RAII C++ wrapper class
- **Rust** — four approaches: `i2cdev` crate, raw `libc::ioctl`, `embedded-hal` traits for portable/no_std drivers, and async with `tokio-i2cdev`
- **Device Tree bindings** — DTS snippets for adapter nodes and slave devices with key property explanations
- **Debugging tools** — `i2cdetect`, `i2cget/set/dump`, `i2ctransfer`, ftrace, and sysfs
- **Summary table** mapping context → language → API


> Understanding the Linux kernel I2C framework, adapters, and algorithms

---

## Table of Contents

1. [Introduction to I2C](#1-introduction-to-i2c)
2. [I2C Protocol Fundamentals](#2-i2c-protocol-fundamentals)
3. [Linux I2C Subsystem Architecture](#3-linux-i2c-subsystem-architecture)
4. [Key Kernel Data Structures](#4-key-kernel-data-structures)
5. [I2C Adapters and Algorithms](#5-i2c-adapters-and-algorithms)
6. [Writing an I2C Device Driver in C/C++](#6-writing-an-i2c-device-driver-in-cc)
7. [Userspace I2C Access in C/C++](#7-userspace-i2c-access-in-cc)
8. [I2C Programming in Rust](#8-i2c-programming-in-rust)
9. [Device Tree Bindings](#9-device-tree-bindings)
10. [Debugging and Tools](#10-debugging-and-tools)
11. [Summary](#11-summary)

---

## 1. Introduction to I2C

**I2C** (Inter-Integrated Circuit, pronounced "I-squared-C") is a synchronous, multi-master, multi-slave, packet-switched, single-ended, serial communication bus invented by Philips Semiconductor (now NXP) in 1982. It uses only **two bidirectional open-drain lines**:

| Signal | Description |
|--------|-------------|
| **SCL** | Serial Clock Line — driven by the master |
| **SDA** | Serial Data Line — bidirectional data |

Both lines are pulled high via resistors. Typical operating speeds are:

| Mode | Speed |
|------|-------|
| Standard Mode | 100 kbit/s |
| Fast Mode | 400 kbit/s |
| Fast-Mode Plus | 1 Mbit/s |
| High-Speed Mode | 3.4 Mbit/s |
| Ultra Fast-mode | 5 Mbit/s |

I2C is ubiquitous in embedded Linux systems: sensors, EEPROMs, RTCs, power management ICs, display controllers, and more all commonly use I2C.

---

## 2. I2C Protocol Fundamentals

### 2.1 Addressing

Each I2C device has a **7-bit address** (some devices use 10-bit extended addressing). The master sends the address followed by a read/write bit:

```
START | ADDR[6:0] | R/W | ACK | DATA... | STOP
```

### 2.2 Transaction Types

- **Write**: Master sends address + W bit, then one or more data bytes.
- **Read**: Master sends address + R bit, slave sends data bytes.
- **Combined (Repeated START)**: Write (register address), repeated START, then read — the most common pattern for register-based devices.

### 2.3 SMBus

**SMBus** (System Management Bus) is a subset of I2C with stricter timing and voltage rules. Linux treats SMBus as a subset and provides translation layers. Most I2C adapters in PC hardware expose an SMBus interface.

---

## 3. Linux I2C Subsystem Architecture

The Linux I2C subsystem lives under `drivers/i2c/` in the kernel source and is organized into well-defined layers:

```
┌─────────────────────────────────────────────────────────┐
│                   User Space                            │
│         /dev/i2c-N  (character device)                  │
│         sysfs: /sys/bus/i2c/                            │
└────────────────────┬────────────────────────────────────┘
                     │  ioctl / read / write
┌────────────────────▼────────────────────────────────────┐
│              I2C Character Device Driver                │
│              (drivers/i2c/i2c-dev.c)                    │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              I2C Core (drivers/i2c/i2c-core-*.c)        │
│  - Bus management        - Device/Driver matching       │
│  - i2c_transfer()        - SMBus emulation              │
│  - i2c_master_send/recv  - Algorithm selection          │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              I2C Bus Driver (Adapter)                   │
│  - i2c_adapter + i2c_algorithm                          │
│  Examples: i2c-bcm2835, i2c-imx, i2c-designware         │
└────────────────────┬────────────────────────────────────┘
                     │
┌────────────────────▼────────────────────────────────────┐
│              Physical I2C Hardware                      │
│              (SoC I2C Controller)                       │
└─────────────────────────────────────────────────────────┘
```

### Key Components

| Component | Role |
|-----------|------|
| **i2c-core** | Central registration, matching, transfer dispatch |
| **i2c_adapter** | Represents a physical I2C bus controller |
| **i2c_algorithm** | Callback table: how to drive the hardware |
| **i2c_client** | Represents a device on the bus |
| **i2c_driver** | Kernel driver for a specific device class |
| **i2c-dev** | Exposes adapters as `/dev/i2c-N` character devices |

---

## 4. Key Kernel Data Structures

### 4.1 `struct i2c_adapter`

Represents a physical I2C controller (bus master).

```c
struct i2c_adapter {
    struct module         *owner;
    unsigned int           class;
    const struct i2c_algorithm *algo;   /* the algorithm to access the bus */
    void                  *algo_data;

    const struct i2c_lock_operations *lock_ops;
    struct rt_mutex        bus_lock;
    struct rt_mutex        mux_lock;

    int                    timeout;     /* in jiffies */
    int                    retries;
    struct device          dev;         /* the adapter device */
    unsigned long          locked_flags;

    int                    nr;          /* bus number */
    char                   name[48];
    struct completion      dev_released;

    struct mutex           userspace_clients_lock;
    struct list_head       userspace_clients;

    struct i2c_bus_recovery_info *bus_recovery_info;
    const struct i2c_adapter_quirks *quirks;

    struct irq_domain     *host_notify_domain;
    struct regulator      *bus_regulator;
};
```

### 4.2 `struct i2c_algorithm`

Defines how the adapter communicates with the hardware.

```c
struct i2c_algorithm {
    /*
     * master_xfer: issue a set of i2c transactions to the given i2c adapter.
     * Returns the number of executed messages or a negative errno.
     */
    int (*master_xfer)(struct i2c_adapter *adap,
                       struct i2c_msg *msgs, int num);

    int (*master_xfer_atomic)(struct i2c_adapter *adap,
                              struct i2c_msg *msgs, int num);

    /* SMBus transfer: returns 0 or a negative errno */
    int (*smbus_xfer)(struct i2c_adapter *adap, u16 addr,
                      unsigned short flags, char read_write,
                      u8 command, int size, union i2c_smbus_data *data);

    int (*smbus_xfer_atomic)(struct i2c_adapter *adap, u16 addr,
                             unsigned short flags, char read_write,
                             u8 command, int size,
                             union i2c_smbus_data *data);

    /* Determine what the adapter supports */
    u32 (*functionality)(struct i2c_adapter *adap);

#if IS_ENABLED(CONFIG_I2C_SLAVE)
    int (*reg_slave)(struct i2c_client *client);
    int (*unreg_slave)(struct i2c_client *client);
#endif
};
```

### 4.3 `struct i2c_msg`

A single I2C message (read or write segment).

```c
struct i2c_msg {
    __u16 addr;     /* slave address */
    __u16 flags;    /* I2C_M_RD, I2C_M_TEN, I2C_M_NOSTART, etc. */
    __u16 len;      /* msg length in bytes */
    __u8 *buf;      /* pointer to msg data */
};

/* Common flags */
#define I2C_M_RD            0x0001  /* read data, from slave to master */
#define I2C_M_TEN           0x0010  /* 10-bit chip address */
#define I2C_M_DMA_SAFE      0x0200  /* buffer is DMA safe */
#define I2C_M_RECV_LEN      0x0400  /* length will be first received byte */
#define I2C_M_NO_RD_ACK     0x0800  /* do not generate ACK on read */
#define I2C_M_IGNORE_NAK    0x1000  /* ignore NACK */
#define I2C_M_REV_DIR_ADDR  0x2000  /* toggles the Rd/Wr bit */
#define I2C_M_NOSTART       0x4000  /* no re-START before next msg */
#define I2C_M_STOP          0x8000  /* force STOP after this msg */
```

### 4.4 `struct i2c_client`

Represents a specific I2C slave device on a bus.

```c
struct i2c_client {
    unsigned short flags;       /* I2C_CLIENT_TEN, I2C_CLIENT_PEC, etc. */
    unsigned short addr;        /* chip address - 7 bits, stored in lower 7 bits */
    char name[I2C_NAME_SIZE];
    struct i2c_adapter *adapter;/* the adapter we sit on */
    struct device dev;          /* the device structure */
    int init_irq;               /* irq set at initialization */
    int irq;                    /* irq issued by device */
    struct list_head detected;
#if IS_ENABLED(CONFIG_I2C_SLAVE)
    i2c_slave_cb_t slave_cb;    /* callback for slave mode */
#endif
    void *devres_group_id;      /* ID of probe devres group */
};
```

### 4.5 `struct i2c_driver`

The kernel driver structure for a class of I2C devices.

```c
struct i2c_driver {
    unsigned int class;

    int (*probe)(struct i2c_client *client);
    void (*remove)(struct i2c_client *client);
    void (*shutdown)(struct i2c_client *client);

    /* Alert callback (e.g., SMBus alert) */
    void (*alert)(struct i2c_client *client,
                  enum i2c_alert_protocol protocol,
                  unsigned int data);

    int (*command)(struct i2c_client *client,
                   unsigned int cmd, void *arg);

    struct device_driver driver;
    const struct i2c_device_id *id_table;

    /* Device auto-detection (legacy) */
    int (*detect)(struct i2c_client *client,
                  struct i2c_board_info *info);
    const unsigned short *address_list;
    struct list_head clients;
};
```

---

## 5. I2C Adapters and Algorithms

### 5.1 Adapter Registration

A bus driver (adapter) registers itself with I2C core:

```c
/* Typical flow in a platform/SoC I2C controller driver */
static int my_i2c_probe(struct platform_device *pdev)
{
    struct my_i2c_dev *dev;
    struct i2c_adapter *adap;
    int ret;

    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev)
        return -ENOMEM;

    /* ... map registers, configure clocks, request IRQ ... */

    adap = &dev->adapter;
    i2c_set_adapdata(adap, dev);
    adap->owner       = THIS_MODULE;
    adap->class       = I2C_CLASS_DEPRECATED;
    adap->algo        = &my_i2c_algorithm;
    adap->dev.parent  = &pdev->dev;
    adap->dev.of_node = pdev->dev.of_node;
    adap->nr          = pdev->id;  /* or -1 for dynamic */
    snprintf(adap->name, sizeof(adap->name),
             "my-i2c-adapter.%d", pdev->id);

    ret = i2c_add_numbered_adapter(adap); /* or i2c_add_adapter() */
    if (ret)
        return ret;

    platform_set_drvdata(pdev, dev);
    return 0;
}

static int my_i2c_remove(struct platform_device *pdev)
{
    struct my_i2c_dev *dev = platform_get_drvdata(pdev);
    i2c_del_adapter(&dev->adapter);
    return 0;
}
```

### 5.2 Algorithm Implementation

The algorithm's `master_xfer` function is the core of bus communication:

```c
static int my_i2c_xfer(struct i2c_adapter *adap,
                        struct i2c_msg *msgs, int num)
{
    struct my_i2c_dev *dev = i2c_get_adapdata(adap);
    int i, ret;

    for (i = 0; i < num; i++) {
        struct i2c_msg *msg = &msgs[i];

        /* Generate START (or repeated START) */
        ret = my_i2c_start(dev, msg->addr,
                           (msg->flags & I2C_M_RD) ? 1 : 0);
        if (ret)
            goto out_stop;

        if (msg->flags & I2C_M_RD) {
            /* Read path */
            ret = my_i2c_read_bytes(dev, msg->buf, msg->len);
        } else {
            /* Write path */
            ret = my_i2c_write_bytes(dev, msg->buf, msg->len);
        }

        if (ret)
            goto out_stop;
    }

    /* Generate STOP on last message */
    my_i2c_stop(dev);
    return num;

out_stop:
    my_i2c_stop(dev);
    return ret;
}

static u32 my_i2c_func(struct i2c_adapter *adap)
{
    return I2C_FUNC_I2C
         | I2C_FUNC_SMBUS_EMUL
         | I2C_FUNC_10BIT_ADDR;
}

static const struct i2c_algorithm my_i2c_algorithm = {
    .master_xfer  = my_i2c_xfer,
    .functionality = my_i2c_func,
};
```

---

## 6. Writing an I2C Device Driver in C/C++

### 6.1 Complete Kernel Driver Example: BME280 Sensor

This example shows a minimal but realistic kernel I2C driver for a temperature/pressure sensor.

```c
// File: drivers/misc/bme280_example.c
// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/delay.h>

#define BME280_CHIP_ID_REG      0xD0
#define BME280_CHIP_ID_VAL      0x60
#define BME280_RESET_REG        0xE0
#define BME280_RESET_VAL        0xB6
#define BME280_CTRL_MEAS_REG    0xF4
#define BME280_STATUS_REG       0xF3
#define BME280_TEMP_MSB_REG     0xFA
#define BME280_CALIB_T1_REG     0x88

/* Calibration data trimming */
struct bme280_calib {
    u16 T1;
    s16 T2, T3;
};

struct bme280_data {
    struct i2c_client   *client;
    struct mutex         lock;
    struct bme280_calib  calib;
};

/* Read a single byte register */
static int bme280_read_byte(struct i2c_client *client, u8 reg, u8 *val)
{
    int ret;

    ret = i2c_smbus_read_byte_data(client, reg);
    if (ret < 0) {
        dev_err(&client->dev, "Failed to read reg 0x%02x: %d\n", reg, ret);
        return ret;
    }
    *val = (u8)ret;
    return 0;
}

/* Write a single byte register */
static int bme280_write_byte(struct i2c_client *client, u8 reg, u8 val)
{
    int ret = i2c_smbus_write_byte_data(client, reg, val);
    if (ret)
        dev_err(&client->dev, "Failed to write reg 0x%02x: %d\n", reg, ret);
    return ret;
}

/* Read a block of registers */
static int bme280_read_block(struct i2c_client *client, u8 reg,
                              u8 *buf, int len)
{
    /*
     * Use a combined write+read transaction:
     *   START | ADDR+W | REG | REPEATED-START | ADDR+R | DATA... | STOP
     */
    struct i2c_msg msgs[2] = {
        {
            .addr  = client->addr,
            .flags = 0,            /* write */
            .len   = 1,
            .buf   = &reg,
        },
        {
            .addr  = client->addr,
            .flags = I2C_M_RD,    /* read */
            .len   = len,
            .buf   = buf,
        },
    };

    int ret = i2c_transfer(client->adapter, msgs, ARRAY_SIZE(msgs));
    if (ret != ARRAY_SIZE(msgs)) {
        dev_err(&client->dev, "i2c_transfer failed: %d\n", ret);
        return (ret < 0) ? ret : -EIO;
    }
    return 0;
}

/* Read calibration data from NVM */
static int bme280_read_calibration(struct bme280_data *data)
{
    struct i2c_client *client = data->client;
    u8 buf[6];
    int ret;

    ret = bme280_read_block(client, BME280_CALIB_T1_REG, buf, sizeof(buf));
    if (ret)
        return ret;

    data->calib.T1 = le16_to_cpup((__le16 *)&buf[0]);
    data->calib.T2 = le16_to_cpup((__le16 *)&buf[2]);
    data->calib.T3 = le16_to_cpup((__le16 *)&buf[4]);
    return 0;
}

/* Compensate raw temperature reading using trimming parameters */
static s32 bme280_compensate_temp(struct bme280_data *data, s32 adc_T)
{
    struct bme280_calib *c = &data->calib;
    s32 var1, var2;

    var1 = ((((adc_T >> 3) - ((s32)c->T1 << 1))) * ((s32)c->T2)) >> 11;
    var2 = (((((adc_T >> 4) - ((s32)c->T1)) *
              ((adc_T >> 4) - ((s32)c->T1))) >> 12) *
            ((s32)c->T3)) >> 14;

    return (var1 + var2) * 5 + 128) >> 8;  /* in 0.01 degC */
}

/* Trigger a forced measurement and read temperature */
static int bme280_read_temp(struct bme280_data *data, int *temp_mc)
{
    struct i2c_client *client = data->client;
    u8 buf[3];
    s32 adc_T;
    int ret;

    /* Forced mode: osrs_t=1 (1x oversampling), mode=01 */
    ret = bme280_write_byte(client, BME280_CTRL_MEAS_REG, 0x25);
    if (ret)
        return ret;

    /* Wait for measurement to complete (~2ms) */
    usleep_range(2000, 3000);

    /* Read raw 20-bit temperature */
    ret = bme280_read_block(client, BME280_TEMP_MSB_REG, buf, sizeof(buf));
    if (ret)
        return ret;

    adc_T = ((s32)buf[0] << 12) | ((s32)buf[1] << 4) | (buf[2] >> 4);
    *temp_mc = bme280_compensate_temp(data, adc_T) * 10; /* millidegrees C */
    return 0;
}

/* IIO channel descriptor */
static const struct iio_chan_spec bme280_channels[] = {
    {
        .type       = IIO_TEMP,
        .info_mask_separate = BIT(IIO_CHAN_INFO_PROCESSED),
    },
};

static int bme280_read_raw(struct iio_dev *indio_dev,
                            struct iio_chan_spec const *chan,
                            int *val, int *val2, long mask)
{
    struct bme280_data *data = iio_priv(indio_dev);
    int ret;

    if (mask != IIO_CHAN_INFO_PROCESSED || chan->type != IIO_TEMP)
        return -EINVAL;

    mutex_lock(&data->lock);
    ret = bme280_read_temp(data, val);
    mutex_unlock(&data->lock);

    if (ret)
        return ret;

    *val2 = 0;
    return IIO_VAL_INT;
}

static const struct iio_info bme280_info = {
    .read_raw = bme280_read_raw,
};

/* Driver probe */
static int bme280_probe(struct i2c_client *client)
{
    struct iio_dev *indio_dev;
    struct bme280_data *data;
    u8 chip_id;
    int ret;

    if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
        dev_err(&client->dev, "I2C adapter lacks SMBUS byte support\n");
        return -EOPNOTSUPP;
    }

    /* Verify chip identity */
    ret = bme280_read_byte(client, BME280_CHIP_ID_REG, &chip_id);
    if (ret)
        return ret;
    if (chip_id != BME280_CHIP_ID_VAL) {
        dev_err(&client->dev,
                "Unexpected chip ID: 0x%02x (expected 0x%02x)\n",
                chip_id, BME280_CHIP_ID_VAL);
        return -ENODEV;
    }

    /* Allocate IIO device with private data */
    indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
    if (!indio_dev)
        return -ENOMEM;

    data = iio_priv(indio_dev);
    data->client = client;
    mutex_init(&data->lock);
    i2c_set_clientdata(client, indio_dev);

    indio_dev->name     = "bme280";
    indio_dev->info     = &bme280_info;
    indio_dev->channels = bme280_channels;
    indio_dev->num_channels = ARRAY_SIZE(bme280_channels);
    indio_dev->modes    = INDIO_DIRECT_MODE;

    /* Read factory calibration from device NVM */
    ret = bme280_read_calibration(data);
    if (ret)
        return ret;

    return devm_iio_device_register(&client->dev, indio_dev);
}

/* Device match table */
static const struct i2c_device_id bme280_id[] = {
    { "bme280", 0 },
    { }
};
MODULE_DEVICE_TABLE(i2c, bme280_id);

static const struct of_device_id bme280_of_match[] = {
    { .compatible = "bosch,bme280" },
    { }
};
MODULE_DEVICE_TABLE(of, bme280_of_match);

static struct i2c_driver bme280_driver = {
    .driver = {
        .name          = "bme280",
        .of_match_table = bme280_of_match,
    },
    .probe    = bme280_probe,
    .id_table = bme280_id,
};

module_i2c_driver(bme280_driver);

MODULE_AUTHOR("Example Author");
MODULE_DESCRIPTION("BME280 Temperature Sensor Driver");
MODULE_LICENSE("GPL v2");
```

### 6.2 SMBus API Reference (C)

Linux provides a higher-level SMBus API that many device drivers prefer:

```c
#include <linux/i2c.h>

/* Read/write single byte (no register) */
s32 i2c_smbus_read_byte(const struct i2c_client *client);
s32 i2c_smbus_write_byte(const struct i2c_client *client, u8 value);

/* Read/write register byte */
s32 i2c_smbus_read_byte_data(const struct i2c_client *client, u8 command);
s32 i2c_smbus_write_byte_data(const struct i2c_client *client,
                               u8 command, u8 value);

/* Read/write register word (16-bit, little-endian) */
s32 i2c_smbus_read_word_data(const struct i2c_client *client, u8 command);
s32 i2c_smbus_write_word_data(const struct i2c_client *client,
                               u8 command, u16 value);

/* Read block of up to 32 bytes */
s32 i2c_smbus_read_i2c_block_data(const struct i2c_client *client,
                                   u8 command, u8 length, u8 *values);
s32 i2c_smbus_write_i2c_block_data(const struct i2c_client *client,
                                    u8 command, u8 length, const u8 *values);

/* Low-level raw transfer (array of messages) */
int i2c_transfer(struct i2c_adapter *adap,
                 struct i2c_msg *msgs, int num);

/* Convenience wrappers */
int i2c_master_send(const struct i2c_client *client,
                    const char *buf, int count);
int i2c_master_recv(const struct i2c_client *client,
                    char *buf, int count);
```

---

## 7. Userspace I2C Access in C/C++

Linux exposes I2C adapters via `/dev/i2c-N` character devices. The `i2c-dev` module must be loaded (or built-in).

### 7.1 Basic Read/Write via ioctl

```c
// File: userspace_i2c_example.c
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

#define I2C_BUS     "/dev/i2c-1"
#define DEVICE_ADDR 0x76          /* BME280 default address */

/* Write a register then read back N bytes (combined transaction) */
static int i2c_read_reg(int fd, uint8_t reg, uint8_t *buf, int len)
{
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg msgs[2];

    /* First message: write register address */
    msgs[0].addr  = DEVICE_ADDR;
    msgs[0].flags = 0;           /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    /* Second message: read data (repeated START) */
    msgs[1].addr  = DEVICE_ADDR;
    msgs[1].flags = I2C_M_RD;   /* read */
    msgs[1].len   = len;
    msgs[1].buf   = buf;

    packets.msgs  = msgs;
    packets.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &packets) < 0) {
        perror("ioctl I2C_RDWR");
        return -1;
    }
    return 0;
}

/* Write a register + data byte */
static int i2c_write_reg(int fd, uint8_t reg, uint8_t val)
{
    uint8_t buf[2] = { reg, val };
    struct i2c_rdwr_ioctl_data packets;
    struct i2c_msg msg = {
        .addr  = DEVICE_ADDR,
        .flags = 0,
        .len   = sizeof(buf),
        .buf   = buf,
    };

    packets.msgs  = &msg;
    packets.nmsgs = 1;

    if (ioctl(fd, I2C_RDWR, &packets) < 0) {
        perror("ioctl I2C_RDWR write");
        return -1;
    }
    return 0;
}

int main(void)
{
    int fd;
    uint8_t chip_id;

    /* Open the I2C bus */
    fd = open(I2C_BUS, O_RDWR);
    if (fd < 0) {
        perror("open " I2C_BUS);
        return EXIT_FAILURE;
    }

    /* Query adapter functionality */
    unsigned long funcs;
    if (ioctl(fd, I2C_FUNCS, &funcs) < 0) {
        perror("I2C_FUNCS");
        close(fd);
        return EXIT_FAILURE;
    }
    printf("I2C adapter functionality: 0x%08lx\n", funcs);
    printf("  I2C_FUNC_I2C:            %s\n",
           (funcs & I2C_FUNC_I2C) ? "yes" : "no");
    printf("  I2C_FUNC_SMBUS_BYTE:     %s\n",
           (funcs & I2C_FUNC_SMBUS_BYTE) ? "yes" : "no");

    /* Read chip ID register (0xD0) */
    if (i2c_read_reg(fd, 0xD0, &chip_id, 1) < 0) {
        close(fd);
        return EXIT_FAILURE;
    }
    printf("Chip ID: 0x%02X (expect 0x60 for BME280)\n", chip_id);

    close(fd);
    return EXIT_SUCCESS;
}
```

### 7.2 SMBus ioctl API (Userspace)

```c
#include <linux/i2c-dev.h>

/* Set the slave address for subsequent read/write calls */
ioctl(fd, I2C_SLAVE, DEVICE_ADDR);       /* returns EBUSY if already used by kernel driver */
ioctl(fd, I2C_SLAVE_FORCE, DEVICE_ADDR); /* force even if driver is bound */

/* SMBus call */
struct i2c_smbus_ioctl_data {
    __u8  read_write;  /* I2C_SMBUS_READ or I2C_SMBUS_WRITE */
    __u8  command;     /* register address */
    __u32 size;        /* I2C_SMBUS_BYTE_DATA, I2C_SMBUS_WORD_DATA, etc. */
    union i2c_smbus_data __user *data;
};

/* Example: read byte data via SMBus ioctl */
static int smbus_read_byte(int fd, uint8_t reg, uint8_t *val)
{
    union i2c_smbus_data data;
    struct i2c_smbus_ioctl_data args = {
        .read_write = I2C_SMBUS_READ,
        .command    = reg,
        .size       = I2C_SMBUS_BYTE_DATA,
        .data       = &data,
    };

    if (ioctl(fd, I2C_SMBUS, &args) < 0)
        return -errno;

    *val = data.byte & 0xFF;
    return 0;
}
```

### 7.3 C++ RAII Wrapper

```cpp
// File: I2CDevice.hpp
#pragma once
#include <string>
#include <stdexcept>
#include <cstdint>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

class I2CDevice {
public:
    I2CDevice(const std::string& bus, uint16_t address)
        : address_(address)
    {
        fd_ = ::open(bus.c_str(), O_RDWR);
        if (fd_ < 0)
            throw std::runtime_error("Failed to open I2C bus: " + bus);

        if (::ioctl(fd_, I2C_SLAVE, address_) < 0) {
            ::close(fd_);
            throw std::runtime_error("Failed to set I2C slave address");
        }
    }

    ~I2CDevice() {
        if (fd_ >= 0)
            ::close(fd_);
    }

    /* Disable copy */
    I2CDevice(const I2CDevice&) = delete;
    I2CDevice& operator=(const I2CDevice&) = delete;

    /* Allow move */
    I2CDevice(I2CDevice&& o) noexcept : fd_(o.fd_), address_(o.address_) {
        o.fd_ = -1;
    }

    uint8_t readByte(uint8_t reg) {
        if (::write(fd_, &reg, 1) != 1)
            throw std::runtime_error("I2C write register failed");

        uint8_t val;
        if (::read(fd_, &val, 1) != 1)
            throw std::runtime_error("I2C read byte failed");
        return val;
    }

    void writeByte(uint8_t reg, uint8_t val) {
        uint8_t buf[2] = { reg, val };
        if (::write(fd_, buf, 2) != 2)
            throw std::runtime_error("I2C write byte failed");
    }

    std::vector<uint8_t> readBlock(uint8_t reg, size_t len) {
        if (::write(fd_, &reg, 1) != 1)
            throw std::runtime_error("I2C write register failed");

        std::vector<uint8_t> data(len);
        ssize_t n = ::read(fd_, data.data(), len);
        if (n < 0 || static_cast<size_t>(n) != len)
            throw std::runtime_error("I2C read block failed");
        return data;
    }

    /* Low-level: issue arbitrary message array */
    void transfer(struct i2c_msg* msgs, int num) {
        struct i2c_rdwr_ioctl_data packets{ msgs, static_cast<uint32_t>(num) };
        if (::ioctl(fd_, I2C_RDWR, &packets) < 0)
            throw std::runtime_error("I2C transfer failed");
    }

private:
    int      fd_;
    uint16_t address_;
};

// Usage example
/*
int main() {
    try {
        I2CDevice bme280("/dev/i2c-1", 0x76);
        uint8_t id = bme280.readByte(0xD0);
        std::cout << "Chip ID: 0x" << std::hex << (int)id << "\n";

        bme280.writeByte(0xF4, 0x25); // forced mode
        auto raw = bme280.readBlock(0xFA, 3);
        // process raw temperature...
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }
}
*/
```

---

## 8. I2C Programming in Rust

Rust has an active embedded ecosystem. For Linux userspace, the `i2cdev` crate is the standard interface. For `no_std` embedded environments, `embedded-hal` is the canonical approach.

### 8.1 Userspace I2C with `i2cdev` crate

Add to `Cargo.toml`:

```toml
[dependencies]
i2cdev = "0.6"
```

```rust
// File: src/main.rs
use i2cdev::core::I2CDevice;
use i2cdev::linux::{LinuxI2CDevice, LinuxI2CError};

const BME280_ADDR: u16 = 0x76;
const REG_CHIP_ID: u8  = 0xD0;
const REG_CTRL:    u8  = 0xF4;
const REG_TEMP:    u8  = 0xFA;

fn read_register(dev: &mut LinuxI2CDevice, reg: u8) -> Result<u8, LinuxI2CError> {
    dev.smbus_read_byte_data(reg)
}

fn write_register(dev: &mut LinuxI2CDevice, reg: u8, val: u8) -> Result<(), LinuxI2CError> {
    dev.smbus_write_byte_data(reg, val)
}

fn read_raw_temperature(dev: &mut LinuxI2CDevice) -> Result<i32, LinuxI2CError> {
    // Trigger forced measurement: osrs_t=1, mode=forced (0b00100101)
    write_register(dev, REG_CTRL, 0x25)?;

    // Give sensor time to measure
    std::thread::sleep(std::time::Duration::from_millis(3));

    // Read 3 bytes of raw temperature (20-bit value)
    let msb  = read_register(dev, REG_TEMP)?     as i32;
    let lsb  = read_register(dev, REG_TEMP + 1)? as i32;
    let xlsb = read_register(dev, REG_TEMP + 2)? as i32;

    Ok((msb << 12) | (lsb << 4) | (xlsb >> 4))
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut dev = LinuxI2CDevice::new("/dev/i2c-1", BME280_ADDR)?;

    let chip_id = read_register(&mut dev, REG_CHIP_ID)?;
    println!("Chip ID: {:#04x} (expect 0x60 for BME280)", chip_id);

    if chip_id != 0x60 {
        eprintln!("Unexpected chip ID!");
        return Ok(());
    }

    let raw_temp = read_raw_temperature(&mut dev)?;
    println!("Raw temperature ADC: {}", raw_temp);

    // In a real driver, apply calibration compensation here
    Ok(())
}
```

### 8.2 Raw ioctl-based I2C in Rust (no external crate)

```rust
// File: src/i2c_raw.rs
// Demonstrates direct ioctl usage, mirroring the C userspace approach.

use std::fs::{File, OpenOptions};
use std::os::unix::io::AsRawFd;

// Linux I2C ioctl constants
const I2C_SLAVE: u64 = 0x0703;
const I2C_RDWR:  u64 = 0x0707;
const I2C_M_RD:  u16 = 0x0001;

#[repr(C)]
struct I2cMsg {
    addr:  u16,
    flags: u16,
    len:   u16,
    buf:   *mut u8,
}

#[repr(C)]
struct I2cRdwrIoctlData {
    msgs:  *mut I2cMsg,
    nmsgs: u32,
}

pub struct I2cBus {
    file: File,
}

impl I2cBus {
    pub fn open(bus: &str) -> std::io::Result<Self> {
        let file = OpenOptions::new().read(true).write(true).open(bus)?;
        Ok(Self { file })
    }

    /// Set the slave address for this device
    pub fn set_slave(&self, addr: u16) -> std::io::Result<()> {
        let ret = unsafe {
            libc::ioctl(self.file.as_raw_fd(), I2C_SLAVE, addr as libc::c_ulong)
        };
        if ret < 0 {
            Err(std::io::Error::last_os_error())
        } else {
            Ok(())
        }
    }

    /// Combined write-then-read transaction (repeated START)
    pub fn write_read(
        &self,
        addr: u16,
        write_buf: &[u8],
        read_buf: &mut [u8],
    ) -> std::io::Result<()> {
        let mut write_buf_copy = write_buf.to_vec();
        let mut msgs = [
            I2cMsg {
                addr,
                flags: 0,
                len:   write_buf_copy.len() as u16,
                buf:   write_buf_copy.as_mut_ptr(),
            },
            I2cMsg {
                addr,
                flags: I2C_M_RD,
                len:   read_buf.len() as u16,
                buf:   read_buf.as_mut_ptr(),
            },
        ];

        let mut data = I2cRdwrIoctlData {
            msgs:  msgs.as_mut_ptr(),
            nmsgs: msgs.len() as u32,
        };

        let ret = unsafe {
            libc::ioctl(
                self.file.as_raw_fd(),
                I2C_RDWR,
                &mut data as *mut I2cRdwrIoctlData,
            )
        };

        if ret < 0 {
            Err(std::io::Error::last_os_error())
        } else {
            Ok(())
        }
    }
}

// Add to Cargo.toml: libc = "0.2"
```

### 8.3 Embedded Rust with `embedded-hal`

For bare-metal or RTOS environments, `embedded-hal` abstracts the I2C interface:

```toml
# Cargo.toml
[dependencies]
embedded-hal = "1.0"
# For RPi: rppal = "0.18"
# For STM32: stm32f4xx-hal = { version = "0.21", features = ["stm32f401"] }
```

```rust
// File: src/bme280_hal.rs
// Platform-independent driver using embedded-hal traits.

use embedded_hal::i2c::{I2c, ErrorType};

const BME280_ADDR: u8  = 0x76;
const REG_CHIP_ID: u8  = 0xD0;
const REG_RESET:   u8  = 0xE0;
const REG_CTRL:    u8  = 0xF4;
const REG_TEMP_MSB: u8 = 0xFA;

pub struct Bme280<I2C> {
    i2c:  I2C,
    addr: u8,
}

impl<I2C, E> Bme280<I2C>
where
    I2C: I2c<Error = E>,
    E: core::fmt::Debug,
{
    pub fn new(i2c: I2C, addr: u8) -> Self {
        Self { i2c, addr }
    }

    /// Read a single register
    pub fn read_reg(&mut self, reg: u8) -> Result<u8, E> {
        let mut buf = [0u8; 1];
        self.i2c.write_read(self.addr, &[reg], &mut buf)?;
        Ok(buf[0])
    }

    /// Write a single register
    pub fn write_reg(&mut self, reg: u8, val: u8) -> Result<(), E> {
        self.i2c.write(self.addr, &[reg, val])
    }

    /// Read multiple registers
    pub fn read_regs(&mut self, start_reg: u8, buf: &mut [u8]) -> Result<(), E> {
        self.i2c.write_read(self.addr, &[start_reg], buf)
    }

    /// Verify device identity
    pub fn verify_id(&mut self) -> Result<bool, E> {
        let id = self.read_reg(REG_CHIP_ID)?;
        Ok(id == 0x60)
    }

    /// Reset the device
    pub fn reset(&mut self) -> Result<(), E> {
        self.write_reg(REG_RESET, 0xB6)
    }

    /// Trigger a forced measurement and return raw ADC value
    pub fn read_raw_temp(&mut self) -> Result<i32, E> {
        // Forced mode, 1x temperature oversampling
        self.write_reg(REG_CTRL, 0x25)?;

        // Note: In real embedded code, use a HAL delay, not std::thread::sleep
        // delay.delay_ms(3u32);

        let mut raw = [0u8; 3];
        self.read_regs(REG_TEMP_MSB, &mut raw)?;

        let adc = ((raw[0] as i32) << 12)
                | ((raw[1] as i32) << 4)
                | ((raw[2] as i32) >> 4);
        Ok(adc)
    }
}

// Example: instantiate on an actual platform (e.g. Linux + rppal)
/*
use rppal::i2c::I2c;

fn main() {
    let i2c = I2c::new().expect("Failed to open I2C");
    let mut sensor = Bme280::new(i2c, BME280_ADDR);

    if sensor.verify_id().unwrap() {
        let raw = sensor.read_raw_temp().unwrap();
        println!("Raw ADC: {}", raw);
    }
}
*/
```

### 8.4 Async I2C in Rust with `tokio-i2cdev`

For applications that must multiplex I2C with other async I/O:

```toml
[dependencies]
tokio-i2cdev = "0.5"
tokio = { version = "1", features = ["full"] }
```

```rust
use tokio_i2cdev::{AsyncI2CDevice, I2CDevice, I2CTransfer, I2CMessage};

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut dev = AsyncI2CDevice::new("/dev/i2c-1")?;

    let reg = [0xD0u8]; // chip ID register
    let mut id = [0u8; 1];

    let mut msgs = [
        I2CMessage::write(&reg),
        I2CMessage::read(&mut id),
    ];

    dev.transfer(&mut msgs).await?;
    println!("Chip ID (async): {:#04x}", id[0]);

    Ok(())
}
```

---

## 9. Device Tree Bindings

On modern ARM/RISC-V Linux systems, I2C devices are described in the Device Tree:

```dts
/* SoC-level I2C controller definition (in SoC .dtsi) */
i2c1: i2c@40005400 {
    compatible   = "st,stm32f7-i2c";
    reg          = <0x40005400 0x400>;
    interrupts   = <GIC_SPI 31 IRQ_TYPE_LEVEL_HIGH>,
                   <GIC_SPI 32 IRQ_TYPE_LEVEL_HIGH>;
    clocks       = <&rcc 1 CLK_I2C1>;
    #address-cells = <1>;
    #size-cells    = <0>;
    status         = "disabled";
};

/* Board-level overlay (in board .dts) */
&i2c1 {
    pinctrl-names = "default";
    pinctrl-0     = <&i2c1_pins>;
    clock-frequency = <400000>;  /* Fast Mode: 400 kHz */
    status          = "okay";

    /* BME280 sensor at address 0x76 */
    bme280@76 {
        compatible = "bosch,bme280";
        reg        = <0x76>;
        interrupt-parent = <&gpio>;
        interrupts       = <5 IRQ_TYPE_LEVEL_LOW>;
    };

    /* AT24C256 EEPROM at address 0x50 */
    eeprom@50 {
        compatible    = "atmel,24c256";
        reg           = <0x50>;
        pagesize      = <64>;
        read-only;
    };
};
```

Key properties for I2C devices:

| Property | Meaning |
|----------|---------|
| `reg` | 7-bit I2C address of the slave |
| `compatible` | Driver match string (`vendor,device`) |
| `clock-frequency` | Bus speed in Hz (on adapter node) |
| `#address-cells = <1>` | Required on I2C adapter nodes |
| `#size-cells = <0>` | Required on I2C adapter nodes |

---

## 10. Debugging and Tools

### 10.1 Command-Line Tools

```bash
# List all I2C adapters
i2cdetect -l

# Scan bus 1 for devices (shows address map)
i2cdetect -y 1

# Read a register (e.g., reg 0xD0 from device 0x76 on bus 1)
i2cget -y 1 0x76 0xD0

# Write a register (e.g., write 0x25 to reg 0xF4)
i2cset -y 1 0x76 0xF4 0x25

# Dump all 256 registers of a device
i2cdump -y 1 0x76

# Interactive I2C transfer (combined messages)
i2ctransfer -y 1 w1@0x76 0xD0 r1@0x76
```

### 10.2 Kernel Debug Interfaces

```bash
# Enable I2C core debug logging
echo 7 > /proc/sys/kernel/printk
modprobe i2c-dev dyndbg=+p

# View registered adapters
ls /sys/bus/i2c/devices/

# View devices on adapter 1
ls /sys/bus/i2c/devices/i2c-1/

# Check i2c_algorithm functionality bitmask
cat /sys/bus/i2c/devices/i2c-1/i2c-dev/i2c-1/name

# Trace I2C transfers with ftrace
echo 1 > /sys/kernel/debug/tracing/events/i2c/enable
cat /sys/kernel/debug/tracing/trace
```

### 10.3 Checking Adapter Functionality (C)

```c
#include <linux/i2c.h>

void print_i2c_funcs(unsigned long funcs) {
    struct { unsigned long flag; const char *name; } f[] = {
        { I2C_FUNC_I2C,               "I2C_FUNC_I2C" },
        { I2C_FUNC_10BIT_ADDR,        "I2C_FUNC_10BIT_ADDR" },
        { I2C_FUNC_SMBUS_QUICK,       "SMBUS_QUICK" },
        { I2C_FUNC_SMBUS_READ_BYTE,   "SMBUS_READ_BYTE" },
        { I2C_FUNC_SMBUS_WRITE_BYTE,  "SMBUS_WRITE_BYTE" },
        { I2C_FUNC_SMBUS_READ_BYTE_DATA,  "SMBUS_BYTE_DATA_R" },
        { I2C_FUNC_SMBUS_WRITE_BYTE_DATA, "SMBUS_BYTE_DATA_W" },
        { I2C_FUNC_SMBUS_READ_WORD_DATA,  "SMBUS_WORD_DATA_R" },
        { I2C_FUNC_SMBUS_WRITE_WORD_DATA, "SMBUS_WORD_DATA_W" },
        { I2C_FUNC_SMBUS_READ_BLOCK_DATA, "SMBUS_BLOCK_R" },
        { I2C_FUNC_SMBUS_I2C_BLOCK,       "SMBUS_I2C_BLOCK" },
        { 0, NULL }
    };

    for (int i = 0; f[i].name; i++) {
        if (funcs & f[i].flag)
            printf("  [x] %s\n", f[i].name);
    }
}
```

---

## 11. Summary

The Linux I2C subsystem provides a robust, layered framework for communicating with I2C/SMBus devices across the full spectrum from bare-metal drivers to userspace applications.

### Architecture Recap

The subsystem cleanly separates concerns into three layers: the **adapter/algorithm layer** (hardware bus driver), the **I2C core** (registration, matching, transfer dispatch), and the **device driver layer** (client-facing API). This separation means a sensor driver like `bme280` can run unmodified on a Raspberry Pi, an STM32, or any Linux platform with a supported I2C controller.

### Key API Surfaces

| Context | Language | API |
|---------|----------|-----|
| Kernel driver | C | `i2c_transfer()`, `i2c_smbus_*()`, `i2c_add_adapter()` |
| Userspace | C/C++ | `/dev/i2c-N` via `ioctl(I2C_RDWR)` or `ioctl(I2C_SMBUS)` |
| Userspace | Rust | `i2cdev` crate, `tokio-i2cdev` for async |
| Embedded (no_std) | Rust | `embedded-hal` I2C traits |

### Design Best Practices

- **Always check `i2c_check_functionality()`** before using SMBus-specific calls — not all adapters support all operations.
- **Use `devm_*` allocation** in kernel drivers to avoid resource leaks on error paths.
- **Prefer `i2c_smbus_*` helpers** over raw `i2c_transfer()` when the device supports it — they handle protocol details automatically.
- **Combined write+read** (with repeated START) is the correct way to read device registers; avoid issuing separate STOP/START between the register address write and the data read.
- **Describe devices in Device Tree** for modern ARM/RISC-V platforms so the kernel can match and instantiate drivers automatically at boot.
- **Use `i2cdetect` and ftrace** for hardware-level debugging before diving into driver code.

### Rust Ecosystem Summary

Rust offers first-class I2C support at every level. The `embedded-hal` trait system enables writing drivers that are **portable across platforms** without modification, a significant advantage for library authors. For Linux userspace, `i2cdev` provides an idiomatic Rust wrapper. The Rust kernel I2C driver API is still maturing (as of kernel 6.x), but in-tree Rust I2C drivers are actively being developed.

---

*Reference: Linux kernel source `drivers/i2c/`, `Documentation/i2c/`, `include/linux/i2c.h`*