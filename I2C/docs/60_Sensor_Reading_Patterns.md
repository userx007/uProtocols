# 60. Sensor Reading Patterns

**Structure at a glance:**
- **I2C fundamentals** — transaction sequences, low-level POSIX primitives (`i2c_open`, `i2c_write_reg`, `i2c_read_regs`, atomic repeated-start via `I2C_RDWR`)
- **Sensor comparison table** — update rates, data widths, compensation requirements, and common devices for all four types
- **Polling strategies** — blocking, non-blocking timed, and interrupt/data-ready pin patterns
- **Temperature (TMP102)** — register decoding, 12-bit two's complement conversion, sliding-window averaging
- **Pressure (BMP280)** — 24-byte factory calibration parsing, Bosch's 64-bit integer compensation formulas, forced-mode one-shot, altitude calculation
- **Humidity (SHT31)** — command-word protocol, CRC-8 validation, Sensirion conversion formula, dew point calculation
- **Accelerometer (ADXL345)** — burst-read for axis-skew prevention, FIFO stream mode draining, FIR filter pattern, auto-sleep/link mode
- **Multi-sensor orchestration** — C++ `SensorManager` with per-sensor periods, priorities, failure back-off
- **Error handling** — retry with exponential back-off, staleness detection, physical plausibility bounds validation
- **Power management** — forced-mode cycling, ADXL345 auto-sleep configuration
- **Rust implementations** — `embedded-hal` 1.x versions of SHT31 (with CRC), ADXL345 (with FIFO), and a generic `RetryReader<T, E, F>` wrapper
- **Summary table** — all patterns with their use-case and key benefit

## Best Practices for Polling Temperature, Pressure, Humidity, and Accelerometer Sensors via I2C

---

## Table of Contents

1. [Introduction](#introduction)
2. [I2C Sensor Communication Fundamentals](#i2c-sensor-communication-fundamentals)
3. [Sensor Categories and Characteristics](#sensor-categories-and-characteristics)
4. [Polling Strategies](#polling-strategies)
5. [Temperature Sensor Patterns](#temperature-sensor-patterns)
6. [Pressure Sensor Patterns](#pressure-sensor-patterns)
7. [Humidity Sensor Patterns](#humidity-sensor-patterns)
8. [Accelerometer Sensor Patterns](#accelerometer-sensor-patterns)
9. [Multi-Sensor Orchestration](#multi-sensor-orchestration)
10. [Error Handling and Fault Tolerance](#error-handling-and-fault-tolerance)
11. [Power Management Patterns](#power-management-patterns)
12. [Rust Implementations](#rust-implementations)
13. [Summary](#summary)

---

## Introduction

Sensor reading via I2C is one of the most common tasks in embedded systems, IoT devices, and industrial automation. While the I2C protocol itself is straightforward, robust sensor reading requires careful attention to timing, data integrity, error handling, and power management.

This document covers best practices and concrete code patterns for reading the four most commonly encountered sensor types: temperature, pressure, humidity, and accelerometers. For each category, the patterns address:

- Correct register-level communication sequences
- Calibration and compensation mathematics
- Polling vs. interrupt-driven acquisition
- Data averaging and filtering
- Error detection and recovery
- Power-efficient operation

---

## I2C Sensor Communication Fundamentals

### The Read/Write Transaction

All I2C sensor interaction follows a small set of primitives. Understanding them is essential before exploring sensor-specific patterns.

**Single register read (most common):**

```
Master → START
Master → Slave address + WRITE bit
Master → Register address byte
Master → REPEATED START
Master → Slave address + READ bit
Slave  → Data byte(s)
Master → STOP (after ACK/NACK handshake)
```

**Burst register read (for multi-byte sensor data):**

```
Master → START
Master → Slave address + WRITE bit
Master → Starting register address
Master → REPEATED START
Master → Slave address + READ bit
Slave  → Byte N, Byte N+1, ..., Byte N+k
Master → NACK on final byte, then STOP
```

### Low-Level C/C++ I2C Primitives (Linux/POSIX)

```c
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

/**
 * @brief  Open an I2C bus and set the target device address.
 * @param  bus_path  e.g. "/dev/i2c-1"
 * @param  addr      7-bit I2C address of the device
 * @return file descriptor on success, -1 on error
 */
int i2c_open(const char *bus_path, uint8_t addr) {
    int fd = open(bus_path, O_RDWR);
    if (fd < 0) {
        perror("i2c_open: open");
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, addr) < 0) {
        perror("i2c_open: ioctl I2C_SLAVE");
        close(fd);
        return -1;
    }
    return fd;
}

/**
 * @brief  Write one byte to a register.
 */
int i2c_write_reg(int fd, uint8_t reg, uint8_t value) {
    uint8_t buf[2] = { reg, value };
    if (write(fd, buf, 2) != 2) {
        perror("i2c_write_reg");
        return -1;
    }
    return 0;
}

/**
 * @brief  Read one or more bytes starting from a register.
 */
int i2c_read_regs(int fd, uint8_t reg, uint8_t *data, size_t len) {
    // Write the register address
    if (write(fd, &reg, 1) != 1) {
        perror("i2c_read_regs: write reg");
        return -1;
    }
    // Read back the data
    ssize_t n = read(fd, data, len);
    if (n != (ssize_t)len) {
        perror("i2c_read_regs: read");
        return -1;
    }
    return 0;
}

/**
 * @brief  Atomic repeated-start read using the kernel's i2c_rdwr ioctl.
 *         Safer than separate write+read on multi-master buses.
 */
int i2c_read_regs_atomic(int fd, uint8_t addr,
                          uint8_t reg, uint8_t *data, size_t len) {
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data rdwr;

    msgs[0].addr  = addr;
    msgs[0].flags = 0;           /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg;

    msgs[1].addr  = addr;
    msgs[1].flags = I2C_M_RD;   /* read */
    msgs[1].len   = (uint16_t)len;
    msgs[1].buf   = data;

    rdwr.msgs  = msgs;
    rdwr.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &rdwr) < 0) {
        perror("i2c_read_regs_atomic");
        return -1;
    }
    return 0;
}
```

---

## Sensor Categories and Characteristics

| Sensor Type   | Typical Update Rate | Raw Data Width | Compensation Needed | Common Devices       |
|---------------|--------------------:|:--------------:|:-------------------:|----------------------|
| Temperature   | 1–10 Hz             | 12–16 bit      | Yes (offset/gain)   | LM75, TMP102, DS18B20 (1-wire), BME280 |
| Pressure      | 1–25 Hz             | 20–24 bit      | Yes (polynomial)    | BMP280, BMP388, LPS22HB |
| Humidity      | 1–8 Hz              | 14–16 bit      | Yes (linear/poly)   | SHT31, HTU21D, BME280 |
| Accelerometer | 10–3200 Hz          | 12–16 bit      | Partial (offset)    | ADXL345, MPU6050, LIS3DH, BMA400 |

Key design tension: **slow sensors** (temperature, humidity) benefit from averaging across many samples taken infrequently; **fast sensors** (accelerometers) need ring buffers and FIFO exploitation to avoid missing events.

---

## Polling Strategies

### 1. Blocking Poll (Simplest)

Blocks the calling thread for the measurement duration. Suitable only for single-sensor bare-metal or non-RTOS systems.

```c
/**
 * @brief  Simple blocking read – read one sample and return.
 *         Caller must introduce inter-sample delay.
 */
void sensor_blocking_poll_loop(int fd) {
    while (1) {
        float value = sensor_read(fd);   /* implementation-specific */
        process(value);
        usleep(100000);                  /* 100 ms = 10 Hz */
    }
}
```

### 2. Non-Blocking Timed Poll (Recommended for RTOS / Event Loops)

```c
#include <time.h>

typedef struct {
    int      fd;
    uint64_t period_us;       /* polling interval */
    uint64_t last_read_us;    /* timestamp of last read */
} sensor_ctx_t;

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

/**
 * @brief  Returns 1 and triggers a read if the polling period has elapsed,
 *         0 otherwise. Never blocks.
 */
int sensor_poll(sensor_ctx_t *ctx, float *out) {
    uint64_t now = now_us();
    if ((now - ctx->last_read_us) < ctx->period_us)
        return 0;
    ctx->last_read_us = now;
    *out = sensor_read(ctx->fd);
    return 1;
}
```

### 3. Interrupt-Driven (Data-Ready Pin)

Many sensors assert a GPIO when a new sample is ready. This is the most efficient approach for high-rate sensors.

```c
/* On a microcontroller (pseudo-code, HAL varies per platform) */
volatile uint8_t data_ready_flag = 0;

/* Called from GPIO interrupt handler */
void EXTI_IRQHandler(void) {
    data_ready_flag = 1;
}

void sensor_task(void) {
    while (1) {
        if (data_ready_flag) {
            data_ready_flag = 0;
            float val = sensor_read(i2c_fd);
            process(val);
        }
        /* yield / sleep until next interrupt */
    }
}
```

---

## Temperature Sensor Patterns

### TMP102 (Texas Instruments) — 12-bit, 3.3V/5V, ±0.5°C

The TMP102 stores temperature as a 12-bit two's complement value in registers 0x00 (MSB) and 0x01 (LSB, upper nibble only).

```c
#define TMP102_ADDR       0x48   /* ADD0 = GND */
#define TMP102_REG_TEMP   0x00
#define TMP102_REG_CFG    0x01

/* Resolution: 1 LSB = 0.0625°C */
#define TMP102_LSB_DEG    0.0625f

typedef struct {
    int     fd;
    float   last_celsius;
    int     error_count;
} tmp102_t;

/**
 * @brief  Read raw temperature register and convert to Celsius.
 * @return 0 on success, -1 on I2C error
 */
int tmp102_read(tmp102_t *dev) {
    uint8_t raw[2];

    if (i2c_read_regs(dev->fd, TMP102_REG_TEMP, raw, 2) < 0) {
        dev->error_count++;
        return -1;
    }

    /* Combine MSB and upper nibble of LSB; shift right by 4 for 12-bit */
    int16_t adc = (int16_t)((raw[0] << 8) | raw[1]) >> 4;

    /* Two's complement: if bit 11 set, value is negative */
    dev->last_celsius = adc * TMP102_LSB_DEG;
    dev->error_count  = 0;
    return 0;
}

/**
 * @brief  Configure one-shot vs. continuous mode, conversion rate, alert threshold.
 *         config_word: see TMP102 datasheet Table 7.
 */
int tmp102_configure(tmp102_t *dev, uint16_t config_word) {
    uint8_t buf[3];
    buf[0] = TMP102_REG_CFG;
    buf[1] = (config_word >> 8) & 0xFF;
    buf[2] =  config_word       & 0xFF;
    if (write(dev->fd, buf, 3) != 3) {
        perror("tmp102_configure");
        return -1;
    }
    return 0;
}
```

**Averaging pattern for stable temperature readings:**

```c
#define AVG_WINDOW 8

/**
 * @brief  Sliding window average to suppress ADC noise.
 */
typedef struct {
    float   buf[AVG_WINDOW];
    uint8_t idx;
    uint8_t count;
} running_avg_t;

void avg_push(running_avg_t *a, float v) {
    a->buf[a->idx] = v;
    a->idx = (a->idx + 1) % AVG_WINDOW;
    if (a->count < AVG_WINDOW) a->count++;
}

float avg_get(const running_avg_t *a) {
    if (a->count == 0) return 0.0f;
    float sum = 0.0f;
    for (uint8_t i = 0; i < a->count; i++) sum += a->buf[i];
    return sum / a->count;
}

/* Usage */
running_avg_t temp_avg = {0};
tmp102_t      tmp = { .fd = i2c_open("/dev/i2c-1", TMP102_ADDR) };

void temperature_task(void) {
    if (tmp102_read(&tmp) == 0) {
        avg_push(&temp_avg, tmp.last_celsius);
        float stable_temp = avg_get(&temp_avg);
        printf("Temperature: %.2f °C\n", stable_temp);
    }
}
```

---

## Pressure Sensor Patterns

### BMP280 (Bosch) — 20-bit, with factory calibration coefficients

The BMP280 requires reading 24 bytes of factory trim parameters from registers 0x88–0x9F and applying Bosch's published compensation formula to obtain physical units.

```c
#include <math.h>

#define BMP280_ADDR        0x76   /* SDO = GND */
#define BMP280_REG_CALIB   0x88
#define BMP280_REG_CTRL    0xF4
#define BMP280_REG_CONFIG  0xF5
#define BMP280_REG_DATA    0xF7   /* 6 bytes: pres[19:0] + temp[19:0] */
#define BMP280_REG_ID      0xD0
#define BMP280_CHIP_ID     0x60   /* expected value */

/* Bosch calibration coefficients (two's complement where applicable) */
typedef struct {
    uint16_t T1;
    int16_t  T2, T3;
    uint16_t P1;
    int16_t  P2, P3, P4, P5, P6, P7, P8, P9;
} bmp280_calib_t;

typedef struct {
    int           fd;
    bmp280_calib_t calib;
    int32_t       t_fine;   /* intermediate value shared between T and P compensation */
    double        temperature_c;
    double        pressure_pa;
} bmp280_t;

/**
 * @brief  Read and parse factory calibration data.
 */
int bmp280_read_calibration(bmp280_t *dev) {
    uint8_t buf[24];
    if (i2c_read_regs(dev->fd, BMP280_REG_CALIB, buf, 24) < 0)
        return -1;

    bmp280_calib_t *c = &dev->calib;
    c->T1 = (uint16_t)(buf[1]  << 8 | buf[0]);
    c->T2 = (int16_t) (buf[3]  << 8 | buf[2]);
    c->T3 = (int16_t) (buf[5]  << 8 | buf[4]);
    c->P1 = (uint16_t)(buf[7]  << 8 | buf[6]);
    c->P2 = (int16_t) (buf[9]  << 8 | buf[8]);
    c->P3 = (int16_t) (buf[11] << 8 | buf[10]);
    c->P4 = (int16_t) (buf[13] << 8 | buf[12]);
    c->P5 = (int16_t) (buf[15] << 8 | buf[14]);
    c->P6 = (int16_t) (buf[17] << 8 | buf[16]);
    c->P7 = (int16_t) (buf[19] << 8 | buf[18]);
    c->P8 = (int16_t) (buf[21] << 8 | buf[20]);
    c->P9 = (int16_t) (buf[23] << 8 | buf[22]);
    return 0;
}

/**
 * @brief  Bosch temperature compensation formula (integer arithmetic).
 *         Sets dev->t_fine for subsequent pressure calculation.
 * @return Temperature in 0.01°C units (e.g., 2345 = 23.45°C)
 */
static int32_t bmp280_compensate_temp(bmp280_t *dev, int32_t adc_T) {
    const bmp280_calib_t *c = &dev->calib;
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)c->T1 << 1)))
                    * (int32_t)c->T2) >> 11;
    int32_t var2 = (((((adc_T >> 4) - (int32_t)c->T1)
                    * ((adc_T >> 4) - (int32_t)c->T1)) >> 12)
                    * (int32_t)c->T3) >> 14;
    dev->t_fine = var1 + var2;
    return (dev->t_fine * 5 + 128) >> 8;
}

/**
 * @brief  Bosch pressure compensation formula (64-bit integer arithmetic).
 * @return Pressure in Q24.8 format: divide by 256 to get Pa
 */
static uint32_t bmp280_compensate_pres(bmp280_t *dev, int32_t adc_P) {
    const bmp280_calib_t *c = &dev->calib;
    int64_t var1 = (int64_t)dev->t_fine - 128000;
    int64_t var2 = var1 * var1 * (int64_t)c->P6;
    var2 = var2 + ((var1 * (int64_t)c->P5) << 17);
    var2 = var2 + (((int64_t)c->P4) << 35);
    var1 = ((var1 * var1 * (int64_t)c->P3) >> 8)
         + ((var1 * (int64_t)c->P2) << 12);
    var1 = ((((int64_t)1) << 47) + var1) * (int64_t)c->P1 >> 33;
    if (var1 == 0) return 0;   /* avoid division by zero */
    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = ((int64_t)c->P9 * (p >> 13) * (p >> 13)) >> 25;
    var2 = ((int64_t)c->P8 * p) >> 19;
    p = ((p + var1 + var2) >> 8) + ((int64_t)c->P7 << 4);
    return (uint32_t)p;
}

/**
 * @brief  Configure oversampling and mode, then trigger forced-mode measurement.
 *         Forced mode: sensor takes one measurement then returns to sleep.
 */
int bmp280_configure_forced(bmp280_t *dev) {
    /* osrs_t=x2 (011), osrs_p=x16 (101), mode=forced (01) */
    uint8_t ctrl = (0x03 << 5) | (0x05 << 2) | 0x01;
    if (i2c_write_reg(dev->fd, BMP280_REG_CTRL, ctrl) < 0)
        return -1;
    /* IIR filter coefficient = 16, standby 0.5ms */
    uint8_t cfg = (0x04 << 2) | (0x00 << 5);
    return i2c_write_reg(dev->fd, BMP280_REG_CONFIG, cfg);
}

/**
 * @brief  Poll status register and read data when ready.
 * @return 0 on success, 1 if still busy, -1 on error
 */
int bmp280_read(bmp280_t *dev) {
    uint8_t status;
    if (i2c_read_regs(dev->fd, 0xF3, &status, 1) < 0) return -1;
    if (status & 0x08) return 1;   /* measuring bit set: not done */

    uint8_t raw[6];
    if (i2c_read_regs(dev->fd, BMP280_REG_DATA, raw, 6) < 0) return -1;

    int32_t adc_P = (int32_t)((raw[0] << 12) | (raw[1] << 4) | (raw[2] >> 4));
    int32_t adc_T = (int32_t)((raw[3] << 12) | (raw[4] << 4) | (raw[5] >> 4));

    int32_t  temp_raw = bmp280_compensate_temp(dev, adc_T);
    uint32_t pres_raw = bmp280_compensate_pres(dev, adc_P);

    dev->temperature_c = temp_raw / 100.0;
    dev->pressure_pa   = pres_raw / 256.0;
    return 0;
}

/**
 * @brief  Convert Pa to altitude using the barometric formula.
 *         sea_level_pa: reference (typically 101325.0 Pa)
 */
double pressure_to_altitude(double pressure_pa, double sea_level_pa) {
    return 44330.0 * (1.0 - pow(pressure_pa / sea_level_pa, 1.0 / 5.255));
}
```

---

## Humidity Sensor Patterns

### SHT31 (Sensirion) — 16-bit, CRC-verified

The SHT31 uses command words rather than register addresses. Each measurement response includes inline CRC bytes for data integrity checking.

```c
#define SHT31_ADDR              0x44
#define SHT31_CMD_MEAS_HIGHREP  0x2400   /* Single-shot, high repeatability */
#define SHT31_CMD_SOFTRESET     0x30A2
#define SHT31_CMD_STATUS        0xF32D

typedef struct {
    int   fd;
    float temperature_c;
    float humidity_pct;
    int   crc_errors;
} sht31_t;

/**
 * @brief  CRC-8 polynomial 0x31, initial value 0xFF (per Sensirion spec).
 */
static uint8_t sht31_crc8(const uint8_t *data, size_t len) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80) ? ((crc << 1) ^ 0x31) : (crc << 1);
        }
    }
    return crc;
}

/**
 * @brief  Send a 2-byte command word to SHT31.
 */
static int sht31_send_cmd(int fd, uint16_t cmd) {
    uint8_t buf[2] = { (uint8_t)(cmd >> 8), (uint8_t)(cmd & 0xFF) };
    if (write(fd, buf, 2) != 2) {
        perror("sht31_send_cmd");
        return -1;
    }
    return 0;
}

/**
 * @brief  Trigger measurement and read 6-byte result (T_MSB, T_LSB, T_CRC,
 *         RH_MSB, RH_LSB, RH_CRC) with CRC validation.
 * @return 0 on success, -1 on I2C error, -2 on CRC mismatch
 */
int sht31_read(sht31_t *dev) {
    if (sht31_send_cmd(dev->fd, SHT31_CMD_MEAS_HIGHREP) < 0) return -1;

    /* High repeatability measurement takes up to 15 ms */
    usleep(15000);

    uint8_t raw[6];
    if (read(dev->fd, raw, 6) != 6) {
        perror("sht31_read");
        return -1;
    }

    /* Validate temperature CRC */
    if (sht31_crc8(raw, 2) != raw[2]) {
        dev->crc_errors++;
        fprintf(stderr, "SHT31: temperature CRC error\n");
        return -2;
    }
    /* Validate humidity CRC */
    if (sht31_crc8(raw + 3, 2) != raw[5]) {
        dev->crc_errors++;
        fprintf(stderr, "SHT31: humidity CRC error\n");
        return -2;
    }

    uint16_t raw_T  = (uint16_t)(raw[0] << 8 | raw[1]);
    uint16_t raw_RH = (uint16_t)(raw[3] << 8 | raw[4]);

    /* Sensirion conversion formulas */
    dev->temperature_c = -45.0f + 175.0f * raw_T  / 65535.0f;
    dev->humidity_pct  =           100.0f * raw_RH / 65535.0f;

    /* Clamp humidity to physical range */
    if (dev->humidity_pct < 0.0f)   dev->humidity_pct = 0.0f;
    if (dev->humidity_pct > 100.0f) dev->humidity_pct = 100.0f;

    return 0;
}

/**
 * @brief  Compute Dew Point using Magnus formula approximation.
 *         Valid range: 0–60°C, 1–100% RH.
 */
float compute_dew_point(float temp_c, float rh_pct) {
    const float a = 17.625f;
    const float b = 243.04f;
    float alpha = (a * temp_c / (b + temp_c)) + logf(rh_pct / 100.0f);
    return (b * alpha) / (a - alpha);
}
```

---

## Accelerometer Sensor Patterns

### ADXL345 (Analog Devices) — 13-bit, ±16g, FIFO-capable

Accelerometers differ from environmental sensors in two key ways: update rates can exceed 3 kHz, and raw data consists of three signed axes that must be read atomically (burst read) to avoid axis-skew.

```c
#define ADXL345_ADDR        0x53   /* ALT ADDRESS = 0x1D when SDO = VCC */
#define ADXL345_REG_DEVID   0x00   /* always 0xE5 */
#define ADXL345_REG_THRESH  0x1D
#define ADXL345_REG_BWRATE  0x2C
#define ADXL345_REG_POWER   0x2D
#define ADXL345_REG_FORMAT  0x31
#define ADXL345_REG_FIFOCTRL 0x38
#define ADXL345_REG_DATA    0x32   /* 6 bytes: X0 X1 Y0 Y1 Z0 Z1 */
#define ADXL345_REG_FIFO_STATUS 0x39

/* Full-scale options for REG_FORMAT (DATA_FORMAT) */
#define ADXL345_FS_2G   0x00
#define ADXL345_FS_4G   0x01
#define ADXL345_FS_8G   0x02
#define ADXL345_FS_16G  0x03

typedef struct {
    float x, y, z;      /* acceleration in m/s² */
} accel_sample_t;

typedef struct {
    int          fd;
    float        scale;          /* LSB to m/s² conversion factor */
    accel_sample_t last;
    int          error_count;
} adxl345_t;

/**
 * @brief  Initialize ADXL345: verify chip ID, set bandwidth and full scale.
 * @param  odr_code  Output data rate: 0x0A=100Hz, 0x0B=200Hz, 0x0C=400Hz
 * @param  fs_code   Full-scale range: ADXL345_FS_2G .. ADXL345_FS_16G
 */
int adxl345_init(adxl345_t *dev, uint8_t odr_code, uint8_t fs_code) {
    /* Check device identity */
    uint8_t devid;
    if (i2c_read_regs(dev->fd, ADXL345_REG_DEVID, &devid, 1) < 0) return -1;
    if (devid != 0xE5) {
        fprintf(stderr, "ADXL345: unexpected DEVID 0x%02X\n", devid);
        return -1;
    }

    /* Set output data rate */
    if (i2c_write_reg(dev->fd, ADXL345_REG_BWRATE, odr_code) < 0) return -1;

    /* Set full-scale and SPI mode (I2C: bit6=0), full resolution (bit3=1) */
    uint8_t fmt = (1 << 3) | (fs_code & 0x03);   /* FULL_RES | range */
    if (i2c_write_reg(dev->fd, ADXL345_REG_FORMAT, fmt) < 0) return -1;

    /*
     * In FULL_RES mode the scale is always 3.9 mg/LSB regardless of range.
     * 1g = 9.80665 m/s², so scale = 0.0039 * 9.80665
     */
    dev->scale = 0.0039f * 9.80665f;

    /* Wake sensor: measurement mode (D3=1), disable auto-sleep (D4=0) */
    if (i2c_write_reg(dev->fd, ADXL345_REG_POWER, 0x08) < 0) return -1;

    return 0;
}

/**
 * @brief  Burst-read all three axes in a single atomic I2C transaction.
 *         Reading X, Y, Z separately risks axis-skew if a new sample
 *         arrives between reads.
 */
int adxl345_read(adxl345_t *dev) {
    uint8_t raw[6];
    if (i2c_read_regs(dev->fd, ADXL345_REG_DATA, raw, 6) < 0) {
        dev->error_count++;
        return -1;
    }

    /* Each axis: two bytes, little-endian, 13-bit signed */
    int16_t rx = (int16_t)(raw[1] << 8 | raw[0]);
    int16_t ry = (int16_t)(raw[3] << 8 | raw[2]);
    int16_t rz = (int16_t)(raw[5] << 8 | raw[4]);

    dev->last.x = rx * dev->scale;
    dev->last.y = ry * dev->scale;
    dev->last.z = rz * dev->scale;
    dev->error_count = 0;
    return 0;
}

/**
 * @brief  Compute vector magnitude |a| = sqrt(x²+y²+z²).
 *         Useful for motion detection independent of orientation.
 */
float accel_magnitude(const accel_sample_t *s) {
    return sqrtf(s->x * s->x + s->y * s->y + s->z * s->z);
}

/* ─── FIFO-Backed High-Rate Reading ───────────────────────────────────────── */

#define FIFO_DEPTH 32

/**
 * @brief  Enable ADXL345's 32-sample FIFO in Stream mode.
 *         Stream: oldest sample discarded when FIFO full (never stalls).
 */
int adxl345_fifo_enable(adxl345_t *dev) {
    /* FIFO_CTL: mode=Stream (10), trigger=0, samples=31 (watermark) */
    return i2c_write_reg(dev->fd, ADXL345_REG_FIFOCTRL,
                         (0x02 << 6) | 31);
}

/**
 * @brief  Drain all pending FIFO entries into caller-supplied buffer.
 * @return Number of samples read, or -1 on error
 */
int adxl345_fifo_drain(adxl345_t *dev, accel_sample_t *buf, int buf_len) {
    uint8_t status;
    if (i2c_read_regs(dev->fd, ADXL345_REG_FIFO_STATUS, &status, 1) < 0)
        return -1;

    int count = status & 0x3F;        /* lower 6 bits = entries available */
    if (count > buf_len) count = buf_len;

    for (int i = 0; i < count; i++) {
        uint8_t raw[6];
        if (i2c_read_regs(dev->fd, ADXL345_REG_DATA, raw, 6) < 0) return -1;
        int16_t rx = (int16_t)(raw[1] << 8 | raw[0]);
        int16_t ry = (int16_t)(raw[3] << 8 | raw[2]);
        int16_t rz = (int16_t)(raw[5] << 8 | raw[4]);
        buf[i].x = rx * dev->scale;
        buf[i].y = ry * dev->scale;
        buf[i].z = rz * dev->scale;
    }
    return count;
}

/**
 * @brief  Simple FIR low-pass filter for accelerometer noise reduction.
 *         coeffs[] should sum to 1.0 for unity DC gain.
 */
float fir_filter(const float *buf, const float *coeffs, int len) {
    float out = 0.0f;
    for (int i = 0; i < len; i++) out += buf[i] * coeffs[i];
    return out;
}
```

---

## Multi-Sensor Orchestration

### C++ Sensor Manager with Priorities and Rate Limiting

```cpp
#include <iostream>
#include <vector>
#include <functional>
#include <chrono>
#include <algorithm>

using namespace std::chrono;

struct SensorEntry {
    std::string             name;
    std::function<int()>    read_fn;      /* returns 0 on success */
    milliseconds            period;
    time_point<steady_clock> next_read;
    int                     priority;     /* lower = higher priority */
    int                     failures;
    int                     max_failures;
};

class SensorManager {
public:
    void register_sensor(const std::string &name,
                         std::function<int()> read_fn,
                         milliseconds period,
                         int priority = 0,
                         int max_failures = 3) {
        sensors_.push_back({
            name, read_fn, period,
            steady_clock::now(),        /* read immediately */
            priority, 0, max_failures
        });
        // Keep sorted by priority
        std::sort(sensors_.begin(), sensors_.end(),
                  [](const SensorEntry &a, const SensorEntry &b) {
                      return a.priority < b.priority;
                  });
    }

    /**
     * @brief  Poll all sensors that have reached their deadline.
     *         Call this in your main loop or a dedicated thread.
     */
    void poll_all() {
        auto now = steady_clock::now();
        for (auto &s : sensors_) {
            if (now < s.next_read) continue;
            int rc = s.read_fn();
            if (rc != 0) {
                s.failures++;
                std::cerr << "[WARN] " << s.name
                          << " read failed (attempt " << s.failures << ")\n";
                if (s.failures >= s.max_failures) {
                    std::cerr << "[ERROR] " << s.name
                              << " exceeded failure limit – skipping\n";
                    /* Could: remove sensor, raise alarm, attempt reinit */
                }
                /* Back off: double the period on failure */
                s.next_read = now + s.period * 2;
            } else {
                s.failures  = 0;
                s.next_read = now + s.period;
            }
        }
    }

private:
    std::vector<SensorEntry> sensors_;
};

/* ─── Example wiring ──────────────────────────────────────────────────────── */
/*
int main() {
    adxl345_t accel = { .fd = i2c_open("/dev/i2c-1", ADXL345_ADDR) };
    sht31_t   rh    = { .fd = i2c_open("/dev/i2c-1", SHT31_ADDR)   };
    bmp280_t  baro  = { .fd = i2c_open("/dev/i2c-1", BMP280_ADDR)  };

    SensorManager mgr;
    mgr.register_sensor("accel",    [&]{ return adxl345_read(&accel); }, milliseconds(10),  0);
    mgr.register_sensor("pressure", [&]{ return bmp280_read(&baro);   }, milliseconds(100), 1);
    mgr.register_sensor("humidity", [&]{ return sht31_read(&rh);      }, milliseconds(500), 2);

    while (true) {
        mgr.poll_all();
        std::this_thread::sleep_for(milliseconds(1));
    }
}
*/
```

---

## Error Handling and Fault Tolerance

```c
typedef enum {
    SENSOR_OK        = 0,
    SENSOR_ERR_I2C   = -1,
    SENSOR_ERR_CRC   = -2,
    SENSOR_ERR_RANGE = -3,
    SENSOR_ERR_STALE = -4,
    SENSOR_ERR_INIT  = -5,
} sensor_err_t;

#define SENSOR_RETRY_MAX   3
#define SENSOR_STALE_MS  5000

typedef struct {
    sensor_err_t  last_error;
    int           consecutive_errors;
    uint64_t      last_success_ms;
    int           total_reads;
    int           total_errors;
} sensor_health_t;

/**
 * @brief  Wrap a sensor read with retry logic and health tracking.
 */
sensor_err_t sensor_read_with_retry(
        sensor_health_t *health,
        sensor_err_t (*read_fn)(void *ctx),
        void *ctx) {

    sensor_err_t rc = SENSOR_ERR_I2C;
    for (int attempt = 0; attempt < SENSOR_RETRY_MAX; attempt++) {
        rc = read_fn(ctx);
        health->total_reads++;
        if (rc == SENSOR_OK) {
            health->consecutive_errors = 0;
            health->last_success_ms    = now_us() / 1000;
            return SENSOR_OK;
        }
        health->total_errors++;
        health->consecutive_errors++;
        health->last_error = rc;
        usleep(5000 * (attempt + 1));   /* 5 ms, 10 ms, 15 ms back-off */
    }

    /* Check for stale data condition */
    uint64_t age_ms = now_us() / 1000 - health->last_success_ms;
    if (age_ms > SENSOR_STALE_MS && health->last_success_ms > 0)
        return SENSOR_ERR_STALE;

    return rc;
}

/**
 * @brief  Validate sensor output against physical plausibility bounds.
 *         Hard limits; sensor is faulty or grossly misconfigured if exceeded.
 */
sensor_err_t validate_temperature(float celsius) {
    if (celsius < -55.0f || celsius > 150.0f) return SENSOR_ERR_RANGE;
    return SENSOR_OK;
}

sensor_err_t validate_pressure(double pa) {
    /* 300 Pa = ~9000m altitude; 115000 Pa = well below sea level */
    if (pa < 300.0 || pa > 115000.0) return SENSOR_ERR_RANGE;
    return SENSOR_OK;
}

sensor_err_t validate_humidity(float pct) {
    if (pct < 0.0f || pct > 100.0f) return SENSOR_ERR_RANGE;
    return SENSOR_OK;
}
```

---

## Power Management Patterns

Many I2C sensors support low-power modes. The general strategy is: wake → configure → measure → sleep, cycling only as fast as the application demands.

```c
/* Example: BMP280 forced mode cycling (one-shot per trigger) */
void bmp280_low_power_cycle(bmp280_t *dev) {
    /*
     * Forced mode: sensor wakes, takes one measurement at the configured
     * oversampling rate, then automatically returns to sleep mode.
     * Power consumption during sleep: ~0.1 µA vs ~714 µA in normal mode.
     */
    bmp280_configure_forced(dev);     /* write CTRL_MEAS with mode=01 */

    /* Wait for conversion: ~2.3ms at 2x oversampling, ~40ms at max */
    int rc;
    do {
        usleep(3000);
        rc = bmp280_read(dev);
    } while (rc == 1);               /* rc==1: still measuring */
}

/* Example: ADXL345 auto-sleep / link mode */
int adxl345_enable_autosleep(adxl345_t *dev, float activity_threshold_g) {
    /*
     * THRESH_ACT (0x24): activity detection threshold, 62.5 mg/LSB
     * THRESH_INACT (0x25): inactivity threshold, 62.5 mg/LSB
     * TIME_INACT (0x26): inactivity time in seconds before auto-sleep
     * POWER_CTL (0x2D): enable AUTO_SLEEP (D4) + LINK (D5)
     */
    uint8_t thresh = (uint8_t)(activity_threshold_g / 0.0625f);
    if (i2c_write_reg(dev->fd, 0x24, thresh)  < 0) return -1;  /* ACT */
    if (i2c_write_reg(dev->fd, 0x25, thresh)  < 0) return -1;  /* INACT */
    if (i2c_write_reg(dev->fd, 0x26, 2)       < 0) return -1;  /* 2 seconds */
    if (i2c_write_reg(dev->fd, 0x27, 0xFF)    < 0) return -1;  /* all axes */
    /* Measurement + Auto-Sleep + Link */
    return i2c_write_reg(dev->fd, ADXL345_REG_POWER, 0x38);
}
```

---

## Rust Implementations

Rust's embedded-hal traits provide a hardware-agnostic, type-safe I2C interface. The patterns below use the `embedded-hal` 1.x API and the `linux-embedded-hal` crate for host-side testing.

### Cargo.toml dependencies

```toml
[dependencies]
embedded-hal   = "1.0"
linux-embedded-hal = "0.4"
```

### Rust I2C Helper Traits

```rust
use embedded_hal::i2c::{I2c, SevenBitAddress};

/// Read one byte from a register.
fn read_reg<I: I2c>(i2c: &mut I, addr: u8, reg: u8) -> Result<u8, I::Error> {
    let mut buf = [0u8; 1];
    i2c.write_read(addr, &[reg], &mut buf)?;
    Ok(buf[0])
}

/// Write one byte to a register.
fn write_reg<I: I2c>(i2c: &mut I, addr: u8, reg: u8, val: u8) -> Result<(), I::Error> {
    i2c.write(addr, &[reg, val])
}

/// Burst-read N bytes starting from a register.
fn burst_read<I: I2c, const N: usize>(
    i2c: &mut I,
    addr: u8,
    reg: u8,
) -> Result<[u8; N], I::Error> {
    let mut buf = [0u8; N];
    i2c.write_read(addr, &[reg], &mut buf)?;
    Ok(buf)
}
```

### Rust: SHT31 Humidity Sensor with CRC

```rust
use embedded_hal::i2c::I2c;

pub const SHT31_ADDR: u8 = 0x44;
const CMD_MEAS_HIGHREP: [u8; 2] = [0x24, 0x00];

#[derive(Debug, Default)]
pub struct Sht31Reading {
    pub temperature_c: f32,
    pub humidity_pct: f32,
}

fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0xFF;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            crc = if crc & 0x80 != 0 {
                (crc << 1) ^ 0x31
            } else {
                crc << 1
            };
        }
    }
    crc
}

#[derive(Debug)]
pub enum Sht31Error<E> {
    I2c(E),
    CrcMismatch,
    OutOfRange,
}

impl<E> From<E> for Sht31Error<E> {
    fn from(e: E) -> Self { Sht31Error::I2c(e) }
}

pub struct Sht31<I2C> {
    i2c:     I2C,
    address: u8,
}

impl<I2C: I2c> Sht31<I2C> {
    pub fn new(i2c: I2C) -> Self {
        Self { i2c, address: SHT31_ADDR }
    }

    pub fn soft_reset(&mut self) -> Result<(), Sht31Error<I2C::Error>> {
        self.i2c.write(self.address, &[0x30, 0xA2])?;
        Ok(())
    }

    pub fn read(&mut self) -> Result<Sht31Reading, Sht31Error<I2C::Error>> {
        // Send measurement command
        self.i2c.write(self.address, &CMD_MEAS_HIGHREP)?;

        // In a real system: use a timer; here we just busy-wait
        // (embedded-hal has no sleep, use HAL-specific delay)
        // delay.delay_ms(15u32);

        // Read 6 bytes: T_MSB, T_LSB, T_CRC, RH_MSB, RH_LSB, RH_CRC
        let mut raw = [0u8; 6];
        self.i2c.read(self.address, &mut raw)?;

        // CRC validation
        if crc8(&raw[0..2]) != raw[2] {
            return Err(Sht31Error::CrcMismatch);
        }
        if crc8(&raw[3..5]) != raw[5] {
            return Err(Sht31Error::CrcMismatch);
        }

        let raw_t  = u16::from_be_bytes([raw[0], raw[1]]);
        let raw_rh = u16::from_be_bytes([raw[3], raw[4]]);

        let temperature_c = -45.0_f32 + 175.0_f32 * raw_t  as f32 / 65535.0_f32;
        let humidity_pct  =              100.0_f32 * raw_rh as f32 / 65535.0_f32;

        // Plausibility check
        if !(-40.0_f32..=125.0_f32).contains(&temperature_c)
            || !(0.0_f32..=100.0_f32).contains(&humidity_pct) {
            return Err(Sht31Error::OutOfRange);
        }

        Ok(Sht31Reading { temperature_c, humidity_pct })
    }
}
```

### Rust: ADXL345 Accelerometer with FIFO

```rust
use embedded_hal::i2c::I2c;

pub const ADXL345_ADDR: u8 = 0x53;

const REG_DEVID:    u8 = 0x00;
const REG_BWRATE:   u8 = 0x2C;
const REG_POWER:    u8 = 0x2D;
const REG_FORMAT:   u8 = 0x31;
const REG_DATA:     u8 = 0x32;
const REG_FIFOCTL:  u8 = 0x38;
const REG_FIFOSTA:  u8 = 0x39;
const EXPECTED_ID:  u8 = 0xE5;

#[derive(Debug, Clone, Copy, Default)]
pub struct AccelSample {
    pub x: f32,
    pub y: f32,
    pub z: f32,
}

impl AccelSample {
    pub fn magnitude(&self) -> f32 {
        (self.x * self.x + self.y * self.y + self.z * self.z).sqrt()
    }
}

#[derive(Debug)]
pub enum Adxl345Error<E> {
    I2c(E),
    WrongId(u8),
}

pub struct Adxl345<I2C> {
    i2c:   I2C,
    addr:  u8,
    scale: f32,
}

impl<I2C: I2c> Adxl345<I2C> {
    /// ODR 100 Hz, ±16g full-resolution.
    pub fn new(mut i2c: I2C) -> Result<Self, Adxl345Error<I2C::Error>> {
        let mut id = [0u8; 1];
        i2c.write_read(ADXL345_ADDR, &[REG_DEVID], &mut id)
            .map_err(Adxl345Error::I2c)?;
        if id[0] != EXPECTED_ID {
            return Err(Adxl345Error::WrongId(id[0]));
        }

        // ODR = 100 Hz
        i2c.write(ADXL345_ADDR, &[REG_BWRATE, 0x0A])
            .map_err(Adxl345Error::I2c)?;
        // FULL_RES | ±16g
        i2c.write(ADXL345_ADDR, &[REG_FORMAT, 0x0B])
            .map_err(Adxl345Error::I2c)?;
        // Measurement mode
        i2c.write(ADXL345_ADDR, &[REG_POWER, 0x08])
            .map_err(Adxl345Error::I2c)?;

        Ok(Self {
            i2c,
            addr: ADXL345_ADDR,
            scale: 0.0039_f32 * 9.80665_f32,
        })
    }

    fn raw_to_sample(&self, raw: &[u8; 6]) -> AccelSample {
        let rx = i16::from_le_bytes([raw[0], raw[1]]);
        let ry = i16::from_le_bytes([raw[2], raw[3]]);
        let rz = i16::from_le_bytes([raw[4], raw[5]]);
        AccelSample {
            x: rx as f32 * self.scale,
            y: ry as f32 * self.scale,
            z: rz as f32 * self.scale,
        }
    }

    pub fn read_sample(&mut self) -> Result<AccelSample, I2C::Error> {
        let mut raw = [0u8; 6];
        self.i2c.write_read(self.addr, &[REG_DATA], &mut raw)?;
        Ok(self.raw_to_sample(&raw))
    }

    /// Enable FIFO stream mode with watermark at 16 samples.
    pub fn fifo_enable(&mut self) -> Result<(), I2C::Error> {
        // Mode=Stream (10b), watermark=16
        self.i2c.write(self.addr, &[REG_FIFOCTL, (0x02 << 6) | 16])
    }

    /// Drain all available FIFO samples into a Vec.
    pub fn fifo_drain(&mut self) -> Result<Vec<AccelSample>, I2C::Error> {
        let mut status = [0u8; 1];
        self.i2c.write_read(self.addr, &[REG_FIFOSTA], &mut status)?;
        let count = (status[0] & 0x3F) as usize;

        let mut samples = Vec::with_capacity(count);
        for _ in 0..count {
            samples.push(self.read_sample()?);
        }
        Ok(samples)
    }
}
```

### Rust: Generic Retry Wrapper

```rust
use std::time::{Duration, Instant};

#[derive(Debug)]
pub enum RetryError<E> {
    Sensor(E),
    Stale { age: Duration },
}

pub struct RetryReader<T, E, F>
where
    F: FnMut() -> Result<T, E>,
{
    read_fn:      F,
    max_attempts: usize,
    backoff:      Duration,
    last_success: Option<Instant>,
    stale_after:  Duration,
}

impl<T, E, F> RetryReader<T, E, F>
where
    F: FnMut() -> Result<T, E>,
{
    pub fn new(read_fn: F) -> Self {
        Self {
            read_fn,
            max_attempts: 3,
            backoff:      Duration::from_millis(5),
            last_success: None,
            stale_after:  Duration::from_secs(5),
        }
    }

    pub fn read(&mut self) -> Result<T, RetryError<E>> {
        let mut last_err = None;
        for attempt in 0..self.max_attempts {
            match (self.read_fn)() {
                Ok(v) => {
                    self.last_success = Some(Instant::now());
                    return Ok(v);
                }
                Err(e) => {
                    last_err = Some(e);
                    std::thread::sleep(self.backoff * (attempt as u32 + 1));
                }
            }
        }
        // Check staleness
        if let Some(last) = self.last_success {
            let age = last.elapsed();
            if age > self.stale_after {
                return Err(RetryError::Stale { age });
            }
        }
        Err(RetryError::Sensor(last_err.unwrap()))
    }
}
```

---

## Summary

| Pattern | When to Use | Key Benefit |
|---|---|---|
| **Blocking poll** | Single sensor, bare-metal, no RTOS | Simplest code |
| **Non-blocking timed poll** | Multiple sensors, event loop / RTOS task | No wasted CPU, easy to extend |
| **Interrupt / data-ready pin** | High-rate sensors (accel, gyro) | Zero latency, zero missed samples |
| **FIFO drain** | Very high rate (>200 Hz) or burst capture | Decouples MCU timing from sensor rate |
| **Burst register read** | Any multi-axis sensor (accel, gyro) | Atomic axis capture; prevents skew |
| **CRC verification** | SHT31, SCD41, any sensor with CRC fields | Detects wire noise and collisions |
| **Factory calibration** | BMP280, BME280, BME688 | Accurate physical units |
| **Running average** | Temperature, humidity (slow sensors) | Smooths ADC noise at near-zero cost |
| **FIR/IIR filter** | Accelerometers, high-noise environments | Noise reduction with configurable cutoff |
| **Forced mode / one-shot** | Battery-powered devices | Orders-of-magnitude power reduction |
| **Health & retry wrapper** | Any production system | Survives transient I2C glitches gracefully |

### Key Takeaways

**1. Always burst-read multi-byte registers.** Reading MSB and LSB in separate transactions risks reading across a sample boundary—especially critical for accelerometers at high ODRs.

**2. Validate CRC bytes when the sensor provides them.** CRC errors indicate noise, bus contention, or device faults; do not use corrupted samples.

**3. Apply factory calibration before any physical interpretation.** Raw ADC codes for BMP280, SHT31, and similar devices are meaningless without compensation.

**4. Match polling rate to sensor capability and application need.** Polling temperature at 1 kHz is wasteful; polling an accelerometer for fall detection at 1 Hz will miss events.

**5. Build in retries and staleness detection.** I2C buses occasionally glitch; graceful retry with exponential back-off makes sensors robust in production.

**6. Use forced/one-shot mode for battery-powered applications.** Environmental sensors in continuous mode consume 100–7000× more power than in forced mode.

**7. Exploit hardware FIFOs.** Sensors like the ADXL345 and MPU-6050 offer 32–1024 sample FIFOs; using them decouples your application timing from the sensor ODR and enables deep sleep between drains.

---

*Document version: 1.0 — covers TMP102, BMP280, SHT31, ADXL345 with C/C++ (POSIX/Linux) and Rust (embedded-hal 1.x) examples.*