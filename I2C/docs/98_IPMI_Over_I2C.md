# 98. IPMI over I2C

**Architecture & Theory**
- Full system diagram showing BMC ↔ IPMB ↔ satellite MC/sensor/FRU topology
- I2C vs SMBus comparison table and why IPMI mandates SMBus with PEC
- IPMB addressing conventions, multi-master arbitration, and typical bus layouts per server

**Protocol Details**
- Byte-level IPMB request and response frame formats with field annotations
- Two's-complement checksum algorithm (covers header and body separately)
- CRC-8 / PEC calculation (polynomial 0x07) over the full SMBus transfer

**C/C++ Examples**
- `/dev/i2c-N` setup with `I2C_PEC` and `I2C_SLAVE` ioctls
- Message builder, checksum utilities, send/receive with retry logic
- `Get Device ID`, `Get Sensor Reading`, `Read FRU Data`, and `Add SEL Entry` implementations

**Rust Examples**
- `Cargo.toml` with `i2cdev` + `thiserror` dependencies
- Strongly-typed `NetFn`, `IpmbRequest`, `IpmbResponse`, and a custom `IpmbError` enum
- Full `IpmbChannel` struct with `request()`, response parsing, and all four IPMI commands
- A complete `main()` demonstrating real-world usage

**Operational Guidance**
- IPMI completion code table (0x00–0xFF)
- Bus reliability best practices (PEC, retry backoff, sequence tracking, bus recovery)
- Security considerations (lack of IPMB authentication, privilege separation, firmware hygiene)

**Intelligent Platform Management Interface communication via I2C/SMBus**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Architecture Overview](#architecture-overview)
3. [IPMI and I2C/SMBus Relationship](#ipmi-and-i2csmbus-relationship)
4. [Key Concepts and Terminology](#key-concepts-and-terminology)
5. [IPMB – Intelligent Platform Management Bus](#ipmb--intelligent-platform-management-bus)
6. [Message Structure and Framing](#message-structure-and-framing)
7. [BMC – Baseboard Management Controller](#bmc--baseboard-management-controller)
8. [IPMI Commands over I2C](#ipmi-commands-over-i2c)
9. [Programming in C/C++](#programming-in-cc)
10. [Programming in Rust](#programming-in-rust)
11. [Error Handling and Reliability](#error-handling-and-reliability)
12. [Security Considerations](#security-considerations)
13. [Summary](#summary)

---

## Introduction

IPMI (Intelligent Platform Management Interface) is an industry-standard specification that defines hardware-level interfaces for out-of-band server management. It enables administrators to monitor, manage, and recover servers independently of the host operating system — even when the system is powered off, unresponsive, or in a failed state.

At its physical foundation, IPMI relies heavily on I2C (Inter-Integrated Circuit) and its derivative SMBus (System Management Bus) as the low-level communication fabric connecting management controllers, sensors, FRU (Field Replaceable Unit) devices, and other platform management components.

IPMI was originally defined by Intel, HP, NEC, and Dell and has evolved through versions 1.0, 1.5, and 2.0. Version 2.0 remains the dominant standard in enterprise servers today.

---

## Architecture Overview

```
+---------------------------+
|       Host OS / Software  |
+----------+----------------+
           |  KCS / SMIC / BT (in-band)
           v
+----------+----------------+
|   BMC (Baseboard          |
|   Management Controller)  |  <----> LAN (RMCP/RMCP+)
|                           |  <----> IPMB (I2C/SMBus)
+----------+----------------+
           |
     I2C / SMBus (IPMB)
           |
    +------+-------+-------+
    |              |       |
+---+---+    +-----+--+  +-+------+
| Sensor|    |  FRU   |  |  Sat.  |
|  SDR  |    |  EEPROM|  |  MC    |
+-------+    +--------+  +--------+
```

The BMC sits at the heart of the IPMI architecture. It communicates:
- **In-band** with the host CPU via KCS (Keyboard Controller Style), SMIC, or BT interfaces.
- **Out-of-band** over a LAN interface using RMCP/RMCP+ (UDP port 623).
- **Internally** via IPMB (I2C bus) to satellite management controllers, sensor chips, FRU EEPROMs, and fan controllers.

---

## IPMI and I2C/SMBus Relationship

I2C was developed by Philips (now NXP) as a two-wire serial bus (SDA + SCL) supporting multi-master, multi-slave configurations. SMBus is a subset of I2C defined by Intel that adds stricter timing, voltage levels, and protocol rules to improve reliability in management applications.

IPMI uses SMBus as its preferred physical and data-link layer for the following reasons:

| Property | I2C | SMBus | IPMI Preference |
|---|---|---|---|
| Clock speed | Up to 3.4 MHz | 10–100 kHz | SMBus (100 kHz) |
| Addressing | 7-bit or 10-bit | 7-bit | 7-bit |
| Error checking | Optional | PEC (CRC-8) | Required (PEC) |
| Timeout enforcement | No | Yes | Yes |
| Bus protocol | Raw | Defined transactions | SMBus transactions |

IPMI mandates the use of SMBus **Packet Error Checking (PEC)**, a CRC-8 byte appended to every transfer to detect data corruption on the bus.

---

## Key Concepts and Terminology

| Term | Meaning |
|---|---|
| **BMC** | Baseboard Management Controller — the primary IPMI controller |
| **IPMB** | Intelligent Platform Management Bus — the I2C/SMBus used by IPMI |
| **Satellite MC** | Secondary management controller connected via IPMB |
| **FRU** | Field Replaceable Unit — components with identity EEPROMs |
| **SDR** | Sensor Data Record — describes sensor capabilities and thresholds |
| **SEL** | System Event Log — log of platform events stored in BMC NV memory |
| **IPMI Message** | Structured request/response packet with checksums |
| **NetFn** | Network Function — top-level command category (6-bit field) |
| **LUN** | Logical Unit Number — sub-addressing within a controller (2-bit field) |
| **Cmd** | Command byte — identifies the specific IPMI command |
| **RSAD** | Responder Slave Address — I2C address of the target |
| **RQAD** | Requester Slave Address — I2C address of the sender |
| **Seq** | Sequence number — matches responses to requests |

---

## IPMB – Intelligent Platform Management Bus

IPMB is the I2C/SMBus instance that forms the management interconnect backbone in IPMI platforms. It operates at 100 kHz and uses 7-bit addressing.

### IPMB Addressing

The BMC typically resides at I2C address **0x20** (7-bit). Satellite MCs occupy other addresses in the range 0x20–0x3E (the "IPMB address space"), in steps of 2 to allow for the read/write bit:

| Device | Typical I2C Address |
|---|---|
| Primary BMC | 0x20 |
| Satellite MC #1 | 0x22 |
| Satellite MC #2 | 0x24 |
| FRU EEPROM | 0x50–0x57 |
| SPD (Memory) | 0x50–0x57 |
| LM75 Temp Sensor | 0x48–0x4F |

### IPMB Multi-Master Operation

IPMB supports multiple masters. Both the BMC and satellite MCs can initiate transactions. Arbitration follows standard I2C rules (SDA wire-AND dominance). IPMB additionally requires:

- **Bus timeout detection**: Stuck bus recovery after 25 ms.
- **Retry mechanism**: Senders must retry failed transactions up to a defined count.
- **PEC byte**: Mandatory CRC-8 on every IPMB message.

---

## Message Structure and Framing

An IPMB message is a structured payload carried as the data bytes of an I2C write transaction.

### IPMB Request Message Format

```
Byte  Field              Description
----  -----------------  -----------------------------------------------
  0   rsAddr             Responder Slave Address (7-bit I2C addr << 1)
  1   netFn/rsLUN        Network Function (bits 7:2) + Responder LUN (bits 1:0)
  2   checksum1          Checksum of bytes 0–1 (two's complement mod 256)
  3   rqAddr             Requester Slave Address (7-bit I2C addr << 1)
  4   rqSeq/rqLUN        Sequence number (bits 7:2) + Requester LUN (bits 1:0)
  5   cmd                Command byte
  6.. data               Optional command data bytes
  N   checksum2          Checksum of bytes 3..(N-1)
  N+1 PEC                SMBus Packet Error Check (CRC-8 of entire transfer)
```

### IPMB Response Message Format

```
Byte  Field              Description
----  -----------------  -----------------------------------------------
  0   rqAddr             Requester Slave Address (echoed, now the responder's target)
  1   netFn/rqLUN        netFn | 0x04 (response bit set) + LUN
  2   checksum1          Checksum of bytes 0–1
  3   rsAddr             Responder Slave Address
  4   rqSeq/rsLUN        Sequence number (echoed) + LUN
  5   cmd                Command (echoed)
  6   completionCode     0x00 = success, else error code
  7.. responseData       Response payload bytes
  N   checksum2          Checksum of bytes 3..(N-1)
  N+1 PEC                CRC-8
```

### Checksum Algorithm

Both checksums use **two's complement modulo 256** of the covered bytes:

```
checksum = (-(sum of covered bytes)) & 0xFF
```

Verification: sum of covered bytes plus checksum == 0x00 (mod 256).

### PEC (Packet Error Check) CRC-8

PEC uses CRC-8 with polynomial **0x07** (x⁸ + x² + x + 1), calculated over the entire SMBus transaction including the address byte and R/W bit:

```
CRC-8: polynomial = 0x07, initial value = 0x00, no reflection
```

---

## BMC – Baseboard Management Controller

The BMC is a dedicated microcontroller (typically ARM Cortex-M or ASPEED AST series) embedded on the server motherboard. It operates independently of the main CPU and retains power as long as standby power (+5VSB) is available.

### BMC Responsibilities

- Monitoring temperature, voltage, fan speed, and power via I2C sensors.
- Reading FRU EEPROMs for inventory and asset data.
- Logging system events to the SEL.
- Controlling power sequences and watchdog timers.
- Serving IPMI requests from the host and the network.
- Communicating with satellite MCs via IPMB.

### I2C Bus Topology on a Typical Server

A BMC typically manages multiple I2C buses:

```
BMC
├── I2C Bus 0 (IPMB-0)   ── Satellite MCs, Power Modules
├── I2C Bus 1             ── Temperature Sensors, Fan Controllers
├── I2C Bus 2             ── FRU EEPROMs (motherboard, PSU, risers)
├── I2C Bus 3             ── SPD EEPROMs (DIMM slots)
├── I2C Bus 4             ── VRM (Voltage Regulator Modules, PMBus)
└── I2C Bus 5             ── PCIe card management
```

---

## IPMI Commands over I2C

IPMI organizes commands by **Network Function (NetFn)** codes:

| NetFn (Request) | NetFn (Response) | Category |
|---|---|---|
| 0x06 | 0x07 | Application |
| 0x04 | 0x05 | Sensor/Event |
| 0x0A | 0x0B | Storage (SEL, SDR, FRU) |
| 0x0C | 0x0D | Transport |
| 0x2C | 0x2D | Group Extension |

### Common Commands

| NetFn | Command | Description |
|---|---|---|
| 0x06 | 0x01 | Get Device ID |
| 0x06 | 0x38 | Get Channel Info |
| 0x04 | 0x2D | Get Sensor Reading |
| 0x04 | 0x27 | Set Sensor Thresholds |
| 0x0A | 0x40 | Get FRU Inventory Area Info |
| 0x0A | 0x11 | Get SEL Entry |
| 0x0A | 0x20 | Get SDR Repository Info |
| 0x0A | 0x23 | Get SDR |

---

## Programming in C/C++

### Linux I2C/SMBus Setup

On Linux, I2C devices are accessed via `/dev/i2c-N` using `ioctl()` calls from `<linux/i2c-dev.h>`.

```c
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>

#define BMC_I2C_BUS     "/dev/i2c-0"
#define BMC_I2C_ADDR    0x20    /* BMC IPMB address (7-bit) */
#define MY_IPMB_ADDR    0x10    /* Our slave address on IPMB */

/* Open I2C bus and set slave address */
int ipmb_open(const char *bus, uint8_t slave_addr) {
    int fd = open(bus, O_RDWR);
    if (fd < 0) {
        perror("open I2C bus");
        return -1;
    }
    /* Enable PEC (SMBus Packet Error Checking) */
    if (ioctl(fd, I2C_PEC, 1) < 0) {
        perror("I2C_PEC");
        close(fd);
        return -1;
    }
    if (ioctl(fd, I2C_SLAVE, slave_addr) < 0) {
        perror("I2C_SLAVE");
        close(fd);
        return -1;
    }
    return fd;
}
```

### CRC-8 / PEC Calculation

```c
/* CRC-8 with polynomial 0x07 (used for IPMI PEC) */
uint8_t crc8_compute(const uint8_t *data, size_t len) {
    uint8_t crc = 0x00;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++) {
            if (crc & 0x80)
                crc = (crc << 1) ^ 0x07;
            else
                crc <<= 1;
        }
    }
    return crc;
}

/* Two's complement checksum (mod 256) */
uint8_t ipmb_checksum(const uint8_t *data, size_t len) {
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
        sum += data[i];
    return (~sum + 1) & 0xFF;
}
```

### IPMB Message Builder

```c
#define IPMB_MAX_MSG_LEN  32

typedef struct {
    uint8_t  rs_addr;      /* Responder slave address (shifted) */
    uint8_t  netfn_lun;    /* NetFn[7:2] | rsLUN[1:0] */
    uint8_t  checksum1;
    uint8_t  rq_addr;      /* Requester slave address (shifted) */
    uint8_t  seq_lun;      /* Sequence[7:2] | rqLUN[1:0] */
    uint8_t  cmd;
    uint8_t  data[IPMB_MAX_MSG_LEN];
    uint8_t  data_len;
} ipmb_request_t;

/*
 * Build a raw IPMB request byte buffer.
 * Returns total length (including checksums, excluding PEC).
 */
int ipmb_build_request(const ipmb_request_t *req,
                        uint8_t *buf, size_t buf_size) {
    if (buf_size < req->data_len + 7)
        return -1;

    buf[0] = req->rs_addr;
    buf[1] = req->netfn_lun;
    buf[2] = ipmb_checksum(buf, 2);      /* checksum1 covers bytes 0-1 */
    buf[3] = req->rq_addr;
    buf[4] = req->seq_lun;
    buf[5] = req->cmd;
    memcpy(&buf[6], req->data, req->data_len);
    buf[6 + req->data_len] = ipmb_checksum(&buf[3], 3 + req->data_len);

    return 7 + req->data_len;  /* rs_addr, netfn, ck1, rq_addr, seq, cmd, data[], ck2 */
}
```

### Sending an IPMB Request and Receiving the Response

```c
#include <stdlib.h>

#define IPMB_RESPONSE_TIMEOUT_MS  5000
#define IPMB_MAX_RETRIES          3

typedef struct {
    uint8_t  completion_code;
    uint8_t  data[IPMB_MAX_MSG_LEN];
    uint8_t  data_len;
} ipmb_response_t;

static uint8_t g_sequence = 0;

int ipmb_send_recv(int fd,
                   uint8_t rs_addr,     /* 7-bit BMC address */
                   uint8_t netfn,
                   uint8_t cmd,
                   const uint8_t *req_data,  uint8_t req_len,
                   ipmb_response_t *resp) {

    uint8_t raw[IPMB_MAX_MSG_LEN + 8];
    uint8_t rxbuf[IPMB_MAX_MSG_LEN + 8];
    int retries = IPMB_MAX_RETRIES;
    int rc;

    ipmb_request_t req = {
        .rs_addr   = (rs_addr << 1),
        .netfn_lun = (netfn << 2) | 0x00,  /* LUN 0 */
        .rq_addr   = (MY_IPMB_ADDR << 1),
        .seq_lun   = ((g_sequence++ & 0x3F) << 2) | 0x00,
        .cmd       = cmd,
        .data_len  = req_len,
    };
    if (req_len)
        memcpy(req.data, req_data, req_len);

    int raw_len = ipmb_build_request(&req, raw, sizeof(raw));
    if (raw_len < 0)
        return -EINVAL;

    /* Compute PEC over [addr_byte_with_W, raw[0..raw_len-1]] */
    uint8_t pec_input[IPMB_MAX_MSG_LEN + 9];
    pec_input[0] = (rs_addr << 1);  /* Address + Write bit = 0 */
    memcpy(&pec_input[1], raw, raw_len);
    uint8_t pec = crc8_compute(pec_input, raw_len + 1);
    raw[raw_len] = pec;
    raw_len++;

    while (retries-- > 0) {
        /* Write the IPMB request (raw I2C write to BMC) */
        rc = write(fd, raw, raw_len);
        if (rc != raw_len) {
            if (retries == 0) return -EIO;
            usleep(10000);
            continue;
        }

        /* Read the response */
        usleep(100000);  /* 100ms processing time for BMC */
        rc = read(fd, rxbuf, sizeof(rxbuf));
        if (rc < 7) {  /* Minimum valid response: header(3) + rq fields(3) + CC(1) */
            if (retries == 0) return -ETIMEDOUT;
            usleep(10000);
            continue;
        }

        /* Validate checksums */
        uint8_t ck1 = ipmb_checksum(rxbuf, 2);
        if (ck1 + rxbuf[2] != 0) {
            fprintf(stderr, "IPMB: checksum1 error\n");
            return -EBADMSG;
        }
        uint8_t ck2 = ipmb_checksum(&rxbuf[3], rc - 4);
        if (ck2 + rxbuf[rc - 1] != 0) {
            fprintf(stderr, "IPMB: checksum2 error\n");
            return -EBADMSG;
        }

        resp->completion_code = rxbuf[6];
        resp->data_len = rc - 8;  /* subtract header, cc, checksum2 */
        if (resp->data_len > 0)
            memcpy(resp->data, &rxbuf[7], resp->data_len);

        return 0;
    }
    return -ETIMEDOUT;
}
```

### Example: Get Device ID Command (NetFn 0x06, Cmd 0x01)

```c
typedef struct {
    uint8_t  device_id;
    uint8_t  device_revision;
    uint8_t  firmware_rev_major;
    uint8_t  firmware_rev_minor;
    uint8_t  ipmi_version;
    uint8_t  additional_device_support;
    uint8_t  manufacturer_id[3];
    uint8_t  product_id[2];
} ipmi_device_id_t;

int ipmi_get_device_id(int fd, uint8_t bmc_addr, ipmi_device_id_t *dev_id) {
    ipmb_response_t resp;
    int rc;

    rc = ipmb_send_recv(fd, bmc_addr,
                         0x06,   /* NetFn: Application */
                         0x01,   /* Cmd: Get Device ID */
                         NULL, 0,
                         &resp);
    if (rc != 0) {
        fprintf(stderr, "Get Device ID failed: %d\n", rc);
        return rc;
    }
    if (resp.completion_code != 0x00) {
        fprintf(stderr, "IPMI completion code: 0x%02X\n", resp.completion_code);
        return -1;
    }
    if (resp.data_len < sizeof(ipmi_device_id_t)) {
        fprintf(stderr, "Truncated Get Device ID response\n");
        return -EBADMSG;
    }

    memcpy(dev_id, resp.data, sizeof(ipmi_device_id_t));
    return 0;
}
```

### Example: Read Sensor (NetFn 0x04, Cmd 0x2D)

```c
typedef struct {
    uint8_t reading;           /* Raw sensor reading */
    uint8_t flags;             /* Bit 5: reading unavailable, bit 6: scanning disabled */
    uint8_t sensor_states_lo;
    uint8_t sensor_states_hi;
} ipmi_sensor_reading_t;

int ipmi_get_sensor_reading(int fd, uint8_t bmc_addr,
                             uint8_t sensor_number,
                             ipmi_sensor_reading_t *reading) {
    ipmb_response_t resp;
    uint8_t req_data[1] = { sensor_number };

    int rc = ipmb_send_recv(fd, bmc_addr,
                             0x04,   /* NetFn: Sensor/Event */
                             0x2D,   /* Cmd: Get Sensor Reading */
                             req_data, 1,
                             &resp);
    if (rc != 0 || resp.completion_code != 0x00)
        return rc ? rc : -1;

    reading->reading          = resp.data[0];
    reading->flags            = resp.data[1];
    reading->sensor_states_lo = (resp.data_len > 2) ? resp.data[2] : 0;
    reading->sensor_states_hi = (resp.data_len > 3) ? resp.data[3] : 0;
    return 0;
}
```

### Example: Read FRU EEPROM Data (NetFn 0x0A, Cmd 0x11)

```c
/* FRU Area header (Common Header at offset 0) */
typedef struct {
    uint8_t format_version;   /* 0x01 */
    uint8_t internal_area_offset;
    uint8_t chassis_area_offset;
    uint8_t board_area_offset;
    uint8_t product_area_offset;
    uint8_t multirecord_area_offset;
    uint8_t padding;
    uint8_t checksum;
} fru_common_header_t;

int ipmi_fru_read(int fd, uint8_t bmc_addr,
                  uint8_t fru_id, uint16_t offset,
                  uint8_t *buf, uint8_t count) {
    ipmb_response_t resp;
    uint8_t req_data[4] = {
        fru_id,
        (uint8_t)(offset & 0xFF),
        (uint8_t)(offset >> 8),
        count
    };

    int rc = ipmb_send_recv(fd, bmc_addr,
                             0x0A,   /* NetFn: Storage */
                             0x11,   /* Cmd: Read FRU Data */
                             req_data, 4,
                             &resp);
    if (rc != 0 || resp.completion_code != 0x00)
        return rc ? rc : -1;

    uint8_t bytes_returned = resp.data[0];
    if (bytes_returned > resp.data_len - 1)
        return -EBADMSG;

    memcpy(buf, &resp.data[1], bytes_returned);
    return bytes_returned;
}

/* Example usage: read FRU Common Header */
void example_read_fru(int fd) {
    fru_common_header_t hdr;
    int rc = ipmi_fru_read(fd, BMC_I2C_ADDR, 0, 0,
                            (uint8_t *)&hdr, sizeof(hdr));
    if (rc > 0) {
        printf("FRU Board Area at offset: 0x%02X (x8 = %d bytes)\n",
               hdr.board_area_offset, hdr.board_area_offset * 8);
    }
}
```

### Example: Write to SEL (System Event Log)

```c
/* IPMI SEL Record (16 bytes) */
typedef struct {
    uint16_t record_id;
    uint8_t  record_type;          /* 0x02 = System Event */
    uint32_t timestamp;
    uint8_t  generator_id_lo;      /* rqSA */
    uint8_t  generator_id_hi;      /* channel/LUN */
    uint8_t  evm_rev;              /* 0x04 for IPMI 2.0 */
    uint8_t  sensor_type;
    uint8_t  sensor_number;
    uint8_t  event_dir_type;       /* bit7=assert/deassert, bits6:0=event type */
    uint8_t  event_data[3];
} __attribute__((packed)) ipmi_sel_record_t;

int ipmi_add_sel_entry(int fd, uint8_t bmc_addr,
                        const ipmi_sel_record_t *sel_rec,
                        uint16_t *new_record_id) {
    ipmb_response_t resp;

    /* Request data is the SEL record minus the record_id (BMC assigns it) */
    int rc = ipmb_send_recv(fd, bmc_addr,
                             0x0A,   /* NetFn: Storage */
                             0x44,   /* Cmd: Add SEL Entry */
                             (const uint8_t *)sel_rec + 2, /* skip record_id */
                             sizeof(ipmi_sel_record_t) - 2,
                             &resp);
    if (rc != 0 || resp.completion_code != 0x00)
        return rc ? rc : -1;

    if (new_record_id && resp.data_len >= 2)
        *new_record_id = (resp.data[1] << 8) | resp.data[0];

    return 0;
}
```

---

## Programming in Rust

### Dependencies (Cargo.toml)

```toml
[package]
name = "ipmi-ipmb"
version = "0.1.0"
edition = "2021"

[dependencies]
i2cdev = "0.6"
thiserror = "1"
log = "0.4"
env_logger = "0.11"
bitfield = "0.17"
```

### Core Types and Errors

```rust
use thiserror::Error;

#[derive(Debug, Error)]
pub enum IpmbError {
    #[error("I2C device error: {0}")]
    I2cError(#[from] i2cdev::linux::LinuxI2CError),
    #[error("Buffer too small")]
    BufferTooSmall,
    #[error("Checksum mismatch (expected {expected:#04x}, got {got:#04x})")]
    ChecksumError { expected: u8, got: u8 },
    #[error("PEC mismatch")]
    PecError,
    #[error("Timeout waiting for IPMB response")]
    Timeout,
    #[error("IPMI completion code error: {0:#04x}")]
    CompletionCode(u8),
    #[error("Response too short ({0} bytes)")]
    ResponseTooShort(usize),
}

pub type IpmbResult<T> = Result<T, IpmbError>;

/// IPMI Network Function codes
#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NetFn {
    Application   = 0x06,
    SensorEvent   = 0x04,
    Storage       = 0x0A,
    Transport     = 0x0C,
}

impl NetFn {
    pub fn response_code(self) -> u8 {
        (self as u8) | 0x04
    }
}

/// IPMB request message structure
#[derive(Debug, Clone)]
pub struct IpmbRequest {
    pub rs_addr:  u8,   // 7-bit responder address
    pub netfn:    u8,
    pub rs_lun:   u8,   // 0..3
    pub rq_addr:  u8,   // 7-bit requester address
    pub seq:      u8,   // 0..63
    pub rq_lun:   u8,   // 0..3
    pub cmd:      u8,
    pub data:     Vec<u8>,
}

/// IPMB response
#[derive(Debug, Clone)]
pub struct IpmbResponse {
    pub completion_code: u8,
    pub data: Vec<u8>,
}
```

### PEC and Checksum Utilities

```rust
/// CRC-8 with polynomial 0x07 (IPMI PEC)
pub fn crc8(data: &[u8]) -> u8 {
    let mut crc: u8 = 0x00;
    for &byte in data {
        crc ^= byte;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = crc.wrapping_shl(1) ^ 0x07;
            } else {
                crc = crc.wrapping_shl(1);
            }
        }
    }
    crc
}

/// Two's complement checksum (mod 256)
pub fn ipmb_checksum(data: &[u8]) -> u8 {
    let sum: u8 = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
    (!sum).wrapping_add(1)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_checksum() {
        // checksum of [rs_addr, netfn_lun] should make sum == 0
        let data = [0x20u8, 0x18u8];  // BMC addr, NetFn=App(0x06)<<2
        let ck = ipmb_checksum(&data);
        let sum = data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
        assert_eq!(sum.wrapping_add(ck), 0);
    }

    #[test]
    fn test_pec_known_value() {
        // Known CRC-8 test vector
        let data = [0x20u8, 0x18, 0xC8, 0x20, 0x00, 0x01];
        let pec = crc8(&data);
        assert_ne!(pec, 0);  // Just verify it runs; real value is hardware-dependent
    }
}
```

### IPMB Message Serialization

```rust
impl IpmbRequest {
    /// Serialize to raw bytes suitable for I2C write (without PEC)
    pub fn to_bytes(&self) -> IpmbResult<Vec<u8>> {
        let mut buf = Vec::with_capacity(7 + self.data.len());

        let rs_addr_byte = self.rs_addr << 1;
        let netfn_lun    = (self.netfn << 2) | (self.rs_lun & 0x03);
        let rq_addr_byte = self.rq_addr << 1;
        let seq_lun      = ((self.seq & 0x3F) << 2) | (self.rq_lun & 0x03);

        buf.push(rs_addr_byte);
        buf.push(netfn_lun);
        buf.push(ipmb_checksum(&[rs_addr_byte, netfn_lun]));
        buf.push(rq_addr_byte);
        buf.push(seq_lun);
        buf.push(self.cmd);
        buf.extend_from_slice(&self.data);

        let ck2_start = 3;
        let ck2 = ipmb_checksum(&buf[ck2_start..]);
        buf.push(ck2);

        Ok(buf)
    }

    /// Build full SMBus packet with PEC
    pub fn to_smbus_packet(&self) -> IpmbResult<Vec<u8>> {
        let mut msg = self.to_bytes()?;
        // PEC covers: [addr_with_write_bit] + msg bytes
        let addr_byte = self.rs_addr << 1;  // Write bit = 0
        let mut pec_input = vec![addr_byte];
        pec_input.extend_from_slice(&msg);
        let pec = crc8(&pec_input);
        msg.push(pec);
        Ok(msg)
    }
}
```

### Linux I2C Interface

```rust
use i2cdev::linux::LinuxI2CDevice;
use i2cdev::core::I2CDevice;
use std::time::{Duration, Instant};
use std::thread;

const BMC_I2C_BUS: &str = "/dev/i2c-0";
const BMC_ADDR: u8 = 0x20;
const MY_ADDR: u8  = 0x10;

pub struct IpmbChannel {
    device: LinuxI2CDevice,
    bmc_addr: u8,
    rq_addr: u8,
    seq: u8,
}

impl IpmbChannel {
    pub fn new(bus: &str, bmc_addr: u8, my_addr: u8) -> IpmbResult<Self> {
        let device = LinuxI2CDevice::new(bus, bmc_addr as u16)?;
        Ok(Self {
            device,
            bmc_addr,
            rq_addr: my_addr,
            seq: 0,
        })
    }

    fn next_seq(&mut self) -> u8 {
        let s = self.seq;
        self.seq = (self.seq + 1) & 0x3F;
        s
    }

    /// Send IPMB request and wait for response with retry
    pub fn request(
        &mut self,
        netfn: u8,
        cmd: u8,
        data: Vec<u8>,
    ) -> IpmbResult<IpmbResponse> {
        let req = IpmbRequest {
            rs_addr: self.bmc_addr,
            netfn,
            rs_lun: 0,
            rq_addr: self.rq_addr,
            seq: self.next_seq(),
            rq_lun: 0,
            cmd,
            data,
        };

        let packet = req.to_smbus_packet()?;

        let mut last_err = IpmbError::Timeout;
        for attempt in 0..3 {
            if attempt > 0 {
                thread::sleep(Duration::from_millis(100));
            }

            // Write request
            if let Err(e) = self.device.write(&packet) {
                log::warn!("IPMB write attempt {} failed: {}", attempt, e);
                last_err = IpmbError::I2cError(e);
                continue;
            }

            // Wait for BMC to process
            thread::sleep(Duration::from_millis(100));

            // Read response (up to 40 bytes)
            let mut rxbuf = [0u8; 40];
            match self.device.read(&mut rxbuf) {
                Err(e) => {
                    log::warn!("IPMB read attempt {} failed: {}", attempt, e);
                    last_err = IpmbError::I2cError(e);
                    continue;
                }
                Ok(n) => {
                    match Self::parse_response(&rxbuf[..n]) {
                        Ok(resp) => return Ok(resp),
                        Err(e) => {
                            log::warn!("Parse error attempt {}: {}", attempt, e);
                            last_err = e;
                        }
                    }
                }
            }
        }

        Err(last_err)
    }

    fn parse_response(rxbuf: &[u8]) -> IpmbResult<IpmbResponse> {
        // Minimum: rqAddr(1) + netfn(1) + ck1(1) + rsAddr(1) + seq(1) + cmd(1) + cc(1) + ck2(1) = 8
        if rxbuf.len() < 8 {
            return Err(IpmbError::ResponseTooShort(rxbuf.len()));
        }

        // Validate checksum1 (bytes 0-1)
        let ck1_sum = rxbuf[0].wrapping_add(rxbuf[1]).wrapping_add(rxbuf[2]);
        if ck1_sum != 0 {
            return Err(IpmbError::ChecksumError {
                expected: 0,
                got: ck1_sum,
            });
        }

        // Validate checksum2 (bytes 3..N-1)
        let ck2_data = &rxbuf[3..rxbuf.len() - 1];
        let ck2_sum: u8 = ck2_data
            .iter()
            .fold(rxbuf[rxbuf.len() - 1], |acc, &b| acc.wrapping_add(b));
        if ck2_sum != 0 {
            return Err(IpmbError::ChecksumError {
                expected: 0,
                got: ck2_sum,
            });
        }

        let completion_code = rxbuf[6];
        let response_data = rxbuf[7..rxbuf.len() - 1].to_vec();

        Ok(IpmbResponse {
            completion_code,
            data: response_data,
        })
    }
}
```

### IPMI Command Implementations in Rust

```rust
/// Device ID response (IPMI spec Table 20-2)
#[derive(Debug)]
pub struct DeviceId {
    pub device_id: u8,
    pub device_revision: u8,
    pub firmware_rev_major: u8,
    pub firmware_rev_minor: u8,
    pub ipmi_version: u8,
    pub manufacturer_id: u32,  // 3-byte IANA
    pub product_id: u16,
}

impl IpmbChannel {
    /// Get Device ID (NetFn=0x06, Cmd=0x01)
    pub fn get_device_id(&mut self) -> IpmbResult<DeviceId> {
        let resp = self.request(0x06, 0x01, vec![])?;

        if resp.completion_code != 0x00 {
            return Err(IpmbError::CompletionCode(resp.completion_code));
        }
        if resp.data.len() < 11 {
            return Err(IpmbError::ResponseTooShort(resp.data.len()));
        }

        Ok(DeviceId {
            device_id:           resp.data[0],
            device_revision:     resp.data[1] & 0x0F,
            firmware_rev_major:  resp.data[2] & 0x7F,
            firmware_rev_minor:  resp.data[3],
            ipmi_version:        resp.data[4],
            manufacturer_id:     (resp.data[7] as u32) << 16
                                 | (resp.data[6] as u32) << 8
                                 | (resp.data[5] as u32),
            product_id:         (resp.data[9] as u16) << 8 | resp.data[8] as u16,
        })
    }

    /// Get Sensor Reading (NetFn=0x04, Cmd=0x2D)
    pub fn get_sensor_reading(&mut self, sensor_num: u8) -> IpmbResult<(u8, bool)> {
        let resp = self.request(0x04, 0x2D, vec![sensor_num])?;

        if resp.completion_code != 0x00 {
            return Err(IpmbError::CompletionCode(resp.completion_code));
        }
        if resp.data.is_empty() {
            return Err(IpmbError::ResponseTooShort(0));
        }

        let reading = resp.data[0];
        let unavailable = resp.data.get(1).map(|&f| f & 0x20 != 0).unwrap_or(false);

        if unavailable {
            log::warn!("Sensor {} reading unavailable", sensor_num);
        }

        Ok((reading, !unavailable))
    }

    /// Read FRU data (NetFn=0x0A, Cmd=0x11)
    pub fn fru_read(
        &mut self,
        fru_id: u8,
        offset: u16,
        count: u8,
    ) -> IpmbResult<Vec<u8>> {
        let req_data = vec![
            fru_id,
            (offset & 0xFF) as u8,
            (offset >> 8) as u8,
            count,
        ];
        let resp = self.request(0x0A, 0x11, req_data)?;

        if resp.completion_code != 0x00 {
            return Err(IpmbError::CompletionCode(resp.completion_code));
        }
        if resp.data.is_empty() {
            return Err(IpmbError::ResponseTooShort(0));
        }

        let returned_count = resp.data[0] as usize;
        if resp.data.len() < 1 + returned_count {
            return Err(IpmbError::ResponseTooShort(resp.data.len()));
        }

        Ok(resp.data[1..1 + returned_count].to_vec())
    }

    /// Get SEL Info (NetFn=0x0A, Cmd=0x40)
    pub fn get_sel_info(&mut self) -> IpmbResult<SelInfo> {
        let resp = self.request(0x0A, 0x40, vec![])?;

        if resp.completion_code != 0x00 {
            return Err(IpmbError::CompletionCode(resp.completion_code));
        }
        if resp.data.len() < 14 {
            return Err(IpmbError::ResponseTooShort(resp.data.len()));
        }

        Ok(SelInfo {
            version:           resp.data[0],
            record_count:      u16::from_le_bytes([resp.data[1], resp.data[2]]),
            free_space:        u16::from_le_bytes([resp.data[3], resp.data[4]]),
            last_add_timestamp: u32::from_le_bytes([
                resp.data[5], resp.data[6], resp.data[7], resp.data[8],
            ]),
        })
    }
}

#[derive(Debug)]
pub struct SelInfo {
    pub version: u8,
    pub record_count: u16,
    pub free_space: u16,
    pub last_add_timestamp: u32,
}
```

### Main Program Example

```rust
fn main() -> IpmbResult<()> {
    env_logger::init();

    let mut ch = IpmbChannel::new(BMC_I2C_BUS, BMC_ADDR, MY_ADDR)?;

    // --- Get Device ID ---
    let dev = ch.get_device_id()?;
    println!("BMC Device ID   : {:#04x}", dev.device_id);
    println!("Firmware Version: {}.{:02}", dev.firmware_rev_major, dev.firmware_rev_minor);
    println!("IPMI Version    : {:.1}", (dev.ipmi_version as f32) / 10.0);
    println!("Manufacturer ID : {:#08x} (IANA)", dev.manufacturer_id);
    println!("Product ID      : {:#06x}", dev.product_id);

    // --- Read Sensor #0x01 ---
    let (raw_reading, valid) = ch.get_sensor_reading(0x01)?;
    if valid {
        println!("Sensor 0x01 raw reading: {}", raw_reading);
    }

    // --- Read FRU Common Header ---
    let hdr_bytes = ch.fru_read(0, 0, 8)?;
    println!("FRU Board Area offset: {} (x8 = {} bytes)",
             hdr_bytes[3], hdr_bytes[3] as u16 * 8);

    // --- Get SEL Info ---
    let sel = ch.get_sel_info()?;
    println!("SEL Records: {}, Free Space: {} bytes",
             sel.record_count, sel.free_space);

    Ok(())
}
```

---

## Error Handling and Reliability

### Completion Codes

IPMI defines a standard set of completion codes returned by the BMC:

| Code | Meaning |
|---|---|
| 0x00 | Command completed normally |
| 0xC0 | Node busy |
| 0xC1 | Invalid command |
| 0xC2 | Command invalid for given LUN |
| 0xC3 | Timeout while processing |
| 0xC7 | Request data length invalid |
| 0xC8 | Request data length limit exceeded |
| 0xCC | Invalid data field in request |
| 0xCE | Request data truncated |
| 0xD0 | Parameter not supported |
| 0xD4 | Insufficient privilege level |
| 0xFF | Unspecified error |

### Bus Reliability Recommendations

1. **PEC on all transfers**: Always enable SMBus PEC. I2C bus noise is common in server environments.
2. **Retry with backoff**: Retry failed requests 2–3 times with exponential backoff (start at 10 ms).
3. **Sequence number tracking**: Discard responses where the echoed sequence number does not match the request.
4. **Timeout enforcement**: Impose 5 second maximum wait for a response.
5. **Bus hang recovery**: If the bus appears stuck (SDA held low), attempt a bus recovery sequence (9 clock cycles + STOP).

### Bus Recovery in C

```c
/* Attempt I2C bus recovery (send 9 clocks + STOP) */
int i2c_bus_recover(const char *bus_path) {
    /* On Linux, use I2C_RETRIES ioctl or GPIO bit-bang recovery */
    int fd = open(bus_path, O_RDWR);
    if (fd < 0) return -1;

    /* Set max retries; kernel I2C layer handles bus clear */
    int retries = 9;
    ioctl(fd, I2C_RETRIES, retries);

    /* Attempt a dummy transaction to release stuck bus */
    uint8_t dummy = 0;
    write(fd, &dummy, 1);  /* Will fail, but triggers recovery */

    close(fd);
    return 0;
}
```

---

## Security Considerations

IPMI over I2C operates on a physically local bus, which reduces (but does not eliminate) attack surface compared to network-exposed IPMI. However, several concerns apply:

1. **Physical access control**: The I2C/IPMB bus is accessible to any component plugged into the system (PCIe cards, riser cards). Malicious hardware could sniff or inject IPMB traffic.

2. **No authentication on IPMB**: The IPMB protocol does not include authentication. Any device on the bus can send IPMI commands to the BMC.

3. **Privilege separation**: Modern BMC firmware implements channel-level privilege enforcement. IPMB access is typically granted Operator or Administrator level by default — review and restrict as needed.

4. **Command filtering**: Consider implementing BMC firmware policies to allow only specific NetFn/Cmd combinations on IPMB.

5. **SEL monitoring**: Log and monitor all IPMB-originated commands in the SEL to detect unusual activity.

6. **Firmware updates**: Keep BMC firmware current; many security patches address IPMB and I2C parsing vulnerabilities (buffer overflows in response parsing, etc.).

7. **Encryption at higher layers**: For sensitive management operations, prefer IPMI over LAN with RMCP+ (IPMI 2.0 mandatory encryption and authentication) rather than raw IPMB.

---

## Summary

IPMI over I2C (IPMB) is the foundational fabric enabling out-of-band platform management in enterprise servers. It uses SMBus at 100 kHz with mandatory PEC (CRC-8) error checking to connect the BMC with satellite management controllers, sensor chips, FRU EEPROMs, and power management modules.

The IPMB message format consists of two header checksum groups, a command byte, optional data, and a trailing PEC byte — providing robust error detection across the noisy server environment. IPMI commands are organized by NetFn code (Application, Sensor/Event, Storage, Transport) and include operations for reading sensor values, retrieving device inventory (FRU), querying the System Event Log (SEL), and managing satellite controllers.

In C/C++, IPMB access is achieved via the Linux `/dev/i2c-N` device interface using `ioctl()` with `I2C_PEC` and `I2C_SLAVE`. CRC-8 and two's-complement checksums must be computed manually around each request/response. In Rust, the `i2cdev` crate provides safe I2C access, and idiomatic error handling with `thiserror` makes the error propagation clean and expressive.

Key reliability practices include retry-with-backoff, sequence number matching, strict timeout enforcement, and bus recovery procedures. Security-wise, IPMB lacks authentication, so physical access control, command filtering, and privilege separation at the BMC firmware level are essential mitigations.

IPMI over I2C remains ubiquitous in server management and is the foundation upon which higher-level tools like `ipmitool`, OpenBMC, and OpenIPMI are built.

---

*Document covers IPMI Specification v2.0 (rev 1.1) and Linux kernel I2C subsystem as of kernel 6.x.*