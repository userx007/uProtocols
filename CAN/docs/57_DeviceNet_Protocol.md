# 57. DeviceNet Protocol

## Overview

DeviceNet is an industrial communication protocol built on top of the **Controller Area Network (CAN)** physical and data-link layers. Developed by Allen-Bradley (now Rockwell Automation) in 1994 and later handed over to the **Open DeviceNet Vendor Association (ODVA)**, it has become a widely adopted open standard for factory automation and process control environments.

DeviceNet defines higher-layer services — addressing, messaging, object models, and device profiles — on top of raw CAN, bridging the gap between the physical bus and actual industrial application semantics.

---

## 1. Protocol Architecture

DeviceNet is structured around three fundamental concepts:

### 1.1 CAN Foundation

DeviceNet inherits directly from CAN 2.0A (11-bit identifier). The 11-bit CAN ID is partitioned into a **Message ID (4 bits)** and a **MAC ID / Node Address (6 bits)**, yielding a maximum of **64 nodes** per network segment (addresses 0–63).

```
CAN 11-bit Identifier Layout:
Bits [10:6] = Message Group / Connection ID
Bits [5:0]  = MAC ID (Node Address, 0–63)
```

### 1.2 Message Groups

DeviceNet defines four message groups that organize traffic by priority and purpose:

| Group | CAN ID Range | Purpose |
|-------|-------------|---------|
| Group 1 | 0x000–0x1FF | Slave I/O (high priority) |
| Group 2 | 0x200–0x3FF | Master/Slave I/O |
| Group 3 | 0x400–0x5FF | Explicit messaging |
| Group 4 | 0x600–0x7FF | Network management, Duplicate MAC ID check |

### 1.3 Object Model

Every DeviceNet device exposes a set of **objects**, each with **attributes** and **services**. The mandatory objects are:

- **Identity Object** (Class 0x01) — vendor ID, device type, product code, revision, serial number
- **Message Router Object** (Class 0x02) — routes explicit messages to the correct class
- **DeviceNet Object** (Class 0x03) — node MAC ID, baud rate, bus-off counter
- **Connection Object** (Class 0x05) — manages I/O and explicit message connections

### 1.4 Connection Types

DeviceNet uses two primary connection paradigms:

#### Explicit Messaging
- Point-to-point, request/response
- Used for configuration, diagnostics, parameter access
- Relatively low frequency

#### Implicit (I/O) Messaging
- Cyclic, change-of-state, or strobed
- Carries real-time process data
- Predefined connection sets for standard devices (e.g., digital I/O blocks)

---

## 2. Physical Layer

DeviceNet uses the CAN physical layer with specific cabling conventions:

- **Baud Rates**: 125 kbps, 250 kbps, 500 kbps
- **Maximum network length**: 500 m at 125 kbps, 100 m at 500 kbps
- **Topology**: Linear trunk with drop lines
- **Cable**: Thick (up to 500 m trunk) or Thin (up to 100 m trunk)
- **Power**: 24 VDC supplied on the cable (separate conductors), up to 8 A

**Wire color convention (5-wire DeviceNet cable):**

| Color | Signal |
|-------|--------|
| Red | V+ (24 VDC) |
| Black | V– (0 VDC) |
| White | CAN_H |
| Blue | CAN_L |
| Bare/Shield | Shield/Drain |

---

## 3. Network Management and Duplicate MAC ID Check

When a node powers up, it must perform a **Duplicate MAC ID Check** using Group 4 messages before participating on the network. This prevents address conflicts.

The check consists of:
1. Node sends a challenge message containing its MAC ID
2. If another node with the same MAC ID responds, a conflict is detected
3. If no response within the timeout period, the node proceeds online

---

## 4. Predefined Connection Sets (PCS)

One of DeviceNet's most distinctive features is the **Predefined Master/Slave Connection Set**, which simplifies implementation for simple I/O devices by hardcoding connection parameters.

The PCS includes:
- **Explicit Message Connection** (Connection ID 0)
- **Polled I/O Connection** (Connection ID 1)
- **Bit-Strobed Connection** (Connection ID 2)
- **Change-of-State / Cyclic Connection** (Connection IDs 3 & 4)

This allows plug-and-play interoperability between masters and slaves from different vendors.

---

## 5. Explicit Messaging — Frame Format

An explicit message request carries a **Service Code** and a **Class/Instance/Attribute** triplet to address the exact object attribute being read or written.

```
Explicit Message Data Field (up to 8 bytes per CAN frame):
Byte 0: Message Body Fragment (MBF) header / fragmentation info
Byte 1: Service Code (e.g., 0x0E = Get_Attribute_Single)
Byte 2: Class ID (low byte)
Byte 3: Instance ID (low byte)
Byte 4: Attribute ID
Bytes 5–7: Data (for Set requests)
```

Common service codes:

| Code | Service |
|------|---------|
| 0x0E | Get_Attribute_Single |
| 0x10 | Set_Attribute_Single |
| 0x01 | Get_Attributes_All |
| 0x4E | Get_Attribute_Single (response) |
| 0x8E | Error response |

---

## 6. Programming in C/C++

The following examples demonstrate DeviceNet concepts using standard Linux SocketCAN as the CAN interface layer.

### 6.1 Setup and CAN Socket Initialization

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define DEVICENET_MAX_NODES     64
#define DEVICENET_MAX_DATA      8

/* DeviceNet Message Group offsets */
#define GROUP1_BASE   0x000
#define GROUP2_BASE   0x200
#define GROUP3_BASE   0x400
#define GROUP4_BASE   0x600

/* DeviceNet Explicit Message Service Codes */
#define SVC_GET_ATTR_SINGLE     0x0E
#define SVC_SET_ATTR_SINGLE     0x10
#define SVC_GET_ATTRS_ALL       0x01
#define SVC_RESET               0x05

/* DeviceNet Class IDs */
#define CLASS_IDENTITY          0x01
#define CLASS_MSG_ROUTER        0x02
#define CLASS_DEVICENET         0x03
#define CLASS_CONNECTION        0x05

/* DeviceNet Object Attributes - Identity */
#define ATTR_VENDOR_ID          0x01
#define ATTR_DEVICE_TYPE        0x02
#define ATTR_PRODUCT_CODE       0x03
#define ATTR_REVISION           0x04
#define ATTR_STATUS             0x05
#define ATTR_SERIAL_NUMBER      0x06
#define ATTR_PRODUCT_NAME       0x07

typedef struct {
    int sock;
    uint8_t mac_id;   /* Local node MAC ID (0-63) */
} devicenet_node_t;

/**
 * Build a Group 3 explicit message CAN ID for a given source MAC ID.
 * Group 3 master explicit request: 0x400 | (dest_mac_id << 0) ... 
 * Per DeviceNet spec: CAN ID = 0b100_MMMMMM (Group3, MAC ID in lower 6 bits)
 */
static uint32_t devicenet_explicit_can_id(uint8_t dest_mac_id) {
    return (GROUP3_BASE | (dest_mac_id & 0x3F));
}

/**
 * Initialize a DeviceNet node (opens a SocketCAN socket).
 */
int devicenet_init(devicenet_node_t *node, const char *ifname, uint8_t mac_id) {
    struct sockaddr_can addr;
    struct ifreq ifr;

    node->mac_id = mac_id & 0x3F;

    node->sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (node->sock < 0) {
        perror("socket");
        return -1;
    }

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ - 1);
    if (ioctl(node->sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX");
        close(node->sock);
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;

    if (bind(node->sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(node->sock);
        return -1;
    }

    printf("[DeviceNet] Node MAC ID %d initialized on %s\n", mac_id, ifname);
    return 0;
}
```

### 6.2 Duplicate MAC ID Check (Group 4)

```c
/**
 * Perform Duplicate MAC ID Check as per DeviceNet spec.
 * Sends a Group 4 challenge and waits for a conflicting response.
 * Returns 0 if MAC ID is unique, -1 if conflict detected.
 */
int devicenet_dup_mac_check(devicenet_node_t *node) {
    struct can_frame tx_frame, rx_frame;
    struct timeval tv;
    fd_set fds;

    /* Group 4 Duplicate MAC ID Check Request:
       CAN ID = 0x600 | mac_id  (our own MAC ID in the ID field) */
    memset(&tx_frame, 0, sizeof(tx_frame));
    tx_frame.can_id  = GROUP4_BASE | (node->mac_id & 0x3F);
    tx_frame.can_dlc = 2;
    tx_frame.data[0] = 0x00;      /* Service: Dup MAC check request */
    tx_frame.data[1] = node->mac_id;

    if (write(node->sock, &tx_frame, sizeof(tx_frame)) < 0) {
        perror("write dup_mac_check");
        return -1;
    }

    /* Wait up to 500ms for a response from another node with same MAC ID */
    FD_ZERO(&fds);
    FD_SET(node->sock, &fds);
    tv.tv_sec  = 0;
    tv.tv_usec = 500000;  /* 500 ms */

    int ret = select(node->sock + 1, &fds, NULL, NULL, &tv);
    if (ret > 0) {
        ssize_t n = read(node->sock, &rx_frame, sizeof(rx_frame));
        if (n > 0) {
            uint8_t src_mac = rx_frame.can_id & 0x3F;
            if (src_mac == node->mac_id) {
                fprintf(stderr, "[DeviceNet] Duplicate MAC ID %d detected!\n",
                        node->mac_id);
                return -1;  /* Conflict: another node has same MAC ID */
            }
        }
    }

    printf("[DeviceNet] Duplicate MAC check passed for MAC ID %d\n", node->mac_id);
    return 0;
}
```

### 6.3 Explicit Message — Get Attribute Single (Master Side)

```c
/**
 * Send a DeviceNet explicit message: Get_Attribute_Single
 * Reads a single attribute from a remote node's object.
 */
int devicenet_get_attribute(devicenet_node_t *master,
                             uint8_t  dest_mac_id,
                             uint8_t  class_id,
                             uint8_t  instance_id,
                             uint8_t  attr_id,
                             uint8_t *out_data,
                             uint8_t *out_len)
{
    struct can_frame tx_frame, rx_frame;
    struct timeval tv;
    fd_set fds;

    memset(&tx_frame, 0, sizeof(tx_frame));

    /*
     * DeviceNet Explicit Request frame layout (simplified, no fragmentation):
     * Byte 0: Frag/XID byte  (0x00 = no fragmentation, XID=0)
     * Byte 1: Service Code   (0x0E = Get_Attribute_Single)
     * Byte 2: Class ID
     * Byte 3: Instance ID
     * Byte 4: Attribute ID
     */
    tx_frame.can_id  = devicenet_explicit_can_id(dest_mac_id);
    tx_frame.can_dlc = 5;
    tx_frame.data[0] = 0x00;              /* no fragmentation */
    tx_frame.data[1] = SVC_GET_ATTR_SINGLE;
    tx_frame.data[2] = class_id;
    tx_frame.data[3] = instance_id;
    tx_frame.data[4] = attr_id;

    if (write(master->sock, &tx_frame, sizeof(tx_frame)) != sizeof(tx_frame)) {
        perror("write explicit msg");
        return -1;
    }

    /* Wait for response (Group 3 reply from dest_mac_id) */
    FD_ZERO(&fds);
    FD_SET(master->sock, &fds);
    tv.tv_sec  = 1;
    tv.tv_usec = 0;

    if (select(master->sock + 1, &fds, NULL, NULL, &tv) <= 0) {
        fprintf(stderr, "[DeviceNet] Timeout waiting for GetAttr response\n");
        return -1;
    }

    ssize_t n = read(master->sock, &rx_frame, sizeof(rx_frame));
    if (n <= 0) return -1;

    /* Check for error response (service code bit 7 set on error = 0x8E) */
    if (rx_frame.data[1] == 0x8E) {
        fprintf(stderr, "[DeviceNet] Error response: general error 0x%02X\n",
                rx_frame.data[2]);
        return -1;
    }

    /* Success: data starts at byte 2 */
    *out_len = (uint8_t)(rx_frame.can_dlc - 2);
    memcpy(out_data, &rx_frame.data[2], *out_len);

    return 0;
}
```

### 6.4 Explicit Message — Set Attribute Single

```c
/**
 * Send a DeviceNet explicit message: Set_Attribute_Single
 * Writes data to a single attribute on a remote node's object.
 */
int devicenet_set_attribute(devicenet_node_t *master,
                             uint8_t  dest_mac_id,
                             uint8_t  class_id,
                             uint8_t  instance_id,
                             uint8_t  attr_id,
                             const uint8_t *data,
                             uint8_t  data_len)
{
    struct can_frame tx_frame;

    if (data_len > 3) {
        fprintf(stderr, "[DeviceNet] Set_Attribute_Single data too long for single frame\n");
        return -1;
    }

    memset(&tx_frame, 0, sizeof(tx_frame));
    tx_frame.can_id  = devicenet_explicit_can_id(dest_mac_id);
    tx_frame.can_dlc = (uint8_t)(5 + data_len);
    tx_frame.data[0] = 0x00;              /* no fragmentation */
    tx_frame.data[1] = SVC_SET_ATTR_SINGLE;
    tx_frame.data[2] = class_id;
    tx_frame.data[3] = instance_id;
    tx_frame.data[4] = attr_id;
    memcpy(&tx_frame.data[5], data, data_len);

    if (write(master->sock, &tx_frame, sizeof(tx_frame)) != sizeof(tx_frame)) {
        perror("write set_attribute");
        return -1;
    }

    printf("[DeviceNet] Set_Attribute_Single sent to MAC ID %d, class=0x%02X "
           "inst=0x%02X attr=0x%02X\n",
           dest_mac_id, class_id, instance_id, attr_id);
    return 0;
}
```

### 6.5 I/O Polled Connection — Master Poll (Group 2)

```c
/**
 * DeviceNet polled I/O: master sends poll command to a slave,
 * slave responds with its current input data.
 * Group 2, Connection ID 1 (Polled I/O).
 */
int devicenet_polled_io(devicenet_node_t *master,
                         uint8_t  slave_mac_id,
                         const uint8_t *output_data, uint8_t out_len,
                         uint8_t *input_data,  uint8_t *in_len)
{
    struct can_frame tx_frame, rx_frame;
    struct timeval tv;
    fd_set fds;

    /* Group 2 Master -> Slave I/O:
       CAN ID = 0x200 | (slave_mac_id & 0x3F)
       DLC 0 = pure poll (request input only), or send output bytes */
    memset(&tx_frame, 0, sizeof(tx_frame));
    tx_frame.can_id  = GROUP2_BASE | (slave_mac_id & 0x3F);
    tx_frame.can_dlc = out_len;
    if (out_len > 0)
        memcpy(tx_frame.data, output_data, out_len);

    if (write(master->sock, &tx_frame, sizeof(tx_frame)) < 0) {
        perror("write polled I/O");
        return -1;
    }

    /* Wait for slave response (Group 1: 0x000 | slave_mac_id) */
    FD_ZERO(&fds);
    FD_SET(master->sock, &fds);
    tv.tv_sec  = 0;
    tv.tv_usec = 100000;  /* 100 ms */

    if (select(master->sock + 1, &fds, NULL, NULL, &tv) <= 0) {
        fprintf(stderr, "[DeviceNet] Polled I/O timeout for slave %d\n", slave_mac_id);
        return -1;
    }

    ssize_t n = read(master->sock, &rx_frame, sizeof(rx_frame));
    if (n <= 0) return -1;

    *in_len = rx_frame.can_dlc;
    memcpy(input_data, rx_frame.data, *in_len);

    return 0;
}
```

### 6.6 Reading Identity Object (Practical Example)

```c
/**
 * Query the Identity Object (Class 0x01, Instance 0x01) of a device.
 */
typedef struct {
    uint16_t vendor_id;
    uint16_t device_type;
    uint16_t product_code;
    struct { uint8_t major; uint8_t minor; } revision;
    uint32_t serial_number;
} devicenet_identity_t;

int devicenet_read_identity(devicenet_node_t *master,
                             uint8_t dest_mac_id,
                             devicenet_identity_t *id)
{
    uint8_t buf[8];
    uint8_t len;

    /* Read Vendor ID */
    if (devicenet_get_attribute(master, dest_mac_id,
                                 CLASS_IDENTITY, 0x01,
                                 ATTR_VENDOR_ID, buf, &len) < 0)
        return -1;
    id->vendor_id = (uint16_t)(buf[0] | (buf[1] << 8));

    /* Read Device Type */
    if (devicenet_get_attribute(master, dest_mac_id,
                                 CLASS_IDENTITY, 0x01,
                                 ATTR_DEVICE_TYPE, buf, &len) < 0)
        return -1;
    id->device_type = (uint16_t)(buf[0] | (buf[1] << 8));

    /* Read Serial Number */
    if (devicenet_get_attribute(master, dest_mac_id,
                                 CLASS_IDENTITY, 0x01,
                                 ATTR_SERIAL_NUMBER, buf, &len) < 0)
        return -1;
    id->serial_number = (uint32_t)(buf[0]        |
                                    (buf[1] << 8)  |
                                    (buf[2] << 16) |
                                    (buf[3] << 24));

    printf("[DeviceNet] Node %d -> VendorID=0x%04X DeviceType=0x%04X "
           "SerialNo=0x%08X\n",
           dest_mac_id, id->vendor_id, id->device_type, id->serial_number);
    return 0;
}

int main(void) {
    devicenet_node_t master;
    devicenet_identity_t identity;
    uint8_t in_data[8];
    uint8_t in_len;

    /* Initialize master node with MAC ID 0 on can0 */
    if (devicenet_init(&master, "can0", 0) < 0) return 1;

    /* Perform Duplicate MAC ID check */
    if (devicenet_dup_mac_check(&master) < 0) return 1;

    /* Query identity of slave at MAC ID 5 */
    if (devicenet_read_identity(&master, 5, &identity) < 0) return 1;

    /* Poll I/O from slave at MAC ID 5, send 1 output byte, receive input */
    uint8_t output = 0xFF;
    if (devicenet_polled_io(&master, 5, &output, 1, in_data, &in_len) == 0) {
        printf("[DeviceNet] Poll response %d bytes: ", in_len);
        for (int i = 0; i < in_len; i++) printf("0x%02X ", in_data[i]);
        printf("\n");
    }

    close(master.sock);
    return 0;
}
```

---

## 7. Programming in Rust

Rust provides memory safety and strong typing — excellent for industrial protocol implementations. The examples use the `socketcan` crate.

### 7.1 Cargo.toml Dependencies

```toml
[package]
name = "devicenet_rust"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan = "3"
anyhow   = "1"
```

### 7.2 DeviceNet Types and Constants

```rust
use socketcan::{CanSocket, CanFrame, Socket, StandardId, EmbeddedFrame};
use std::time::Duration;
use anyhow::{Result, bail, Context};

/// DeviceNet CAN ID group base addresses
const GROUP1_BASE: u16 = 0x000;
const GROUP2_BASE: u16 = 0x200;
const GROUP3_BASE: u16 = 0x400;
const GROUP4_BASE: u16 = 0x600;

/// DeviceNet Explicit Message Service Codes
const SVC_GET_ATTR_SINGLE: u8 = 0x0E;
const SVC_SET_ATTR_SINGLE: u8 = 0x10;
const SVC_GET_ATTRS_ALL:   u8 = 0x01;
const SVC_RESET:           u8 = 0x05;
const SVC_ERROR_RESPONSE:  u8 = 0x8E;

/// DeviceNet Object Class IDs
#[repr(u8)]
#[derive(Debug, Clone, Copy)]
pub enum DnClass {
    Identity   = 0x01,
    MsgRouter  = 0x02,
    DeviceNet  = 0x03,
    Connection = 0x05,
}

/// Identity Object Attributes
#[repr(u8)]
#[derive(Debug, Clone, Copy)]
pub enum IdentityAttr {
    VendorId    = 0x01,
    DeviceType  = 0x02,
    ProductCode = 0x03,
    Revision    = 0x04,
    Status      = 0x05,
    SerialNo    = 0x06,
    ProductName = 0x07,
}

/// DeviceNet node identity information
#[derive(Debug, Default)]
pub struct DeviceNetIdentity {
    pub vendor_id:    u16,
    pub device_type:  u16,
    pub product_code: u16,
    pub revision:     (u8, u8),  /* (major, minor) */
    pub serial_no:    u32,
    pub product_name: String,
}

/// A DeviceNet master node backed by a SocketCAN socket
pub struct DeviceNetMaster {
    socket: CanSocket,
    mac_id: u8,
}
```

### 7.3 Master Initialization and Duplicate MAC Check

```rust
impl DeviceNetMaster {
    /// Create and initialize a DeviceNet master node.
    pub fn new(ifname: &str, mac_id: u8) -> Result<Self> {
        let mac_id = mac_id & 0x3F;
        let socket = CanSocket::open(ifname)
            .with_context(|| format!("Failed to open CAN interface '{}'", ifname))?;

        socket.set_read_timeout(Duration::from_millis(500))
            .context("Failed to set read timeout")?;

        println!("[DeviceNet] Master MAC ID {} initialized on {}", mac_id, ifname);
        Ok(Self { socket, mac_id })
    }

    /// Perform Duplicate MAC ID Check (Group 4).
    /// Returns Ok(()) if MAC ID is unique, Err if conflict detected.
    pub fn duplicate_mac_check(&self) -> Result<()> {
        // Build Group 4 challenge frame
        let can_id = GROUP4_BASE | (self.mac_id as u16);
        let data   = [0x00u8, self.mac_id];
        let id     = StandardId::new(can_id)
            .ok_or_else(|| anyhow::anyhow!("Invalid CAN ID {:#05X}", can_id))?;
        let frame  = CanFrame::new(id, &data)
            .ok_or_else(|| anyhow::anyhow!("Failed to create CAN frame"))?;

        self.socket.write_frame(&frame)
            .context("Failed to send Duplicate MAC ID check")?;

        // Listen for conflicting response (500 ms timeout already set)
        match self.socket.read_frame() {
            Ok(rx) => {
                let src_mac = (rx.raw_id() & 0x3F) as u8;
                if src_mac == self.mac_id {
                    bail!("Duplicate MAC ID {} detected on network!", self.mac_id);
                }
                // Unrelated frame arrived — treat as no conflict
                println!("[DeviceNet] Duplicate MAC check passed for MAC ID {}", self.mac_id);
                Ok(())
            }
            Err(_) => {
                // Timeout: no conflict found
                println!("[DeviceNet] Duplicate MAC check passed for MAC ID {} (timeout)",
                         self.mac_id);
                Ok(())
            }
        }
    }
```

### 7.4 Explicit Messaging — Get and Set Attribute

```rust
    /// Send Get_Attribute_Single and return the response data bytes.
    pub fn get_attribute(
        &self,
        dest_mac_id: u8,
        class:       DnClass,
        instance:    u8,
        attribute:   u8,
    ) -> Result<Vec<u8>> {
        let can_id = GROUP3_BASE | (dest_mac_id & 0x3F) as u16;
        let id = StandardId::new(can_id)
            .ok_or_else(|| anyhow::anyhow!("Invalid CAN ID {:#05X}", can_id))?;

        // Explicit message: [frag_byte, svc_code, class, instance, attr]
        let data = [
            0x00,
            SVC_GET_ATTR_SINGLE,
            class as u8,
            instance,
            attribute,
        ];

        let frame = CanFrame::new(id, &data)
            .ok_or_else(|| anyhow::anyhow!("Failed to create CAN frame"))?;

        self.socket.write_frame(&frame)
            .context("Failed to send Get_Attribute_Single")?;

        let rx = self.socket.read_frame()
            .context("Timeout waiting for Get_Attribute_Single response")?;

        let rx_data = rx.data();
        if rx_data.len() < 2 {
            bail!("Response too short: {} bytes", rx_data.len());
        }
        if rx_data[1] == SVC_ERROR_RESPONSE {
            let general_err = rx_data.get(2).copied().unwrap_or(0xFF);
            bail!("Get_Attribute_Single error: general_status=0x{:02X}", general_err);
        }

        // Response data begins at byte 2
        Ok(rx_data[2..].to_vec())
    }

    /// Send Set_Attribute_Single with provided data bytes.
    pub fn set_attribute(
        &self,
        dest_mac_id: u8,
        class:       DnClass,
        instance:    u8,
        attribute:   u8,
        value:       &[u8],
    ) -> Result<()> {
        if value.len() > 3 {
            bail!("Set_Attribute_Single data too long for single frame ({} bytes)", value.len());
        }

        let can_id = GROUP3_BASE | (dest_mac_id & 0x3F) as u16;
        let id = StandardId::new(can_id)
            .ok_or_else(|| anyhow::anyhow!("Invalid CAN ID {:#05X}", can_id))?;

        let mut data = vec![0x00u8, SVC_SET_ATTR_SINGLE, class as u8, instance, attribute];
        data.extend_from_slice(value);

        let frame = CanFrame::new(id, &data)
            .ok_or_else(|| anyhow::anyhow!("Failed to create CAN frame"))?;

        self.socket.write_frame(&frame)
            .context("Failed to send Set_Attribute_Single")?;

        println!("[DeviceNet] Set_Attribute_Single sent to MAC ID {}, \
                  class={:#04X} inst={} attr={}",
                 dest_mac_id, class as u8, instance, attribute);
        Ok(())
    }
```

### 7.5 Polled I/O and Identity Query

```rust
    /// Perform a polled I/O exchange with a slave node (Group 2).
    /// Sends output_data to slave, returns the slave's input data.
    pub fn polled_io(
        &self,
        slave_mac_id: u8,
        output_data:  &[u8],
    ) -> Result<Vec<u8>> {
        let can_id = GROUP2_BASE | (slave_mac_id & 0x3F) as u16;
        let id = StandardId::new(can_id)
            .ok_or_else(|| anyhow::anyhow!("Invalid CAN ID {:#05X}", can_id))?;

        let frame = CanFrame::new(id, output_data)
            .ok_or_else(|| anyhow::anyhow!("Failed to create poll frame"))?;

        self.socket.write_frame(&frame)
            .context("Failed to send polled I/O request")?;

        let rx = self.socket.read_frame()
            .context("Timeout waiting for polled I/O response")?;

        Ok(rx.data().to_vec())
    }

    /// Read the full Identity Object from a remote node.
    pub fn read_identity(&self, dest_mac_id: u8) -> Result<DeviceNetIdentity> {
        let mut identity = DeviceNetIdentity::default();

        // Vendor ID (2 bytes, little-endian)
        let raw = self.get_attribute(dest_mac_id, DnClass::Identity, 1,
                                     IdentityAttr::VendorId as u8)?;
        identity.vendor_id = u16::from_le_bytes([raw[0], raw.get(1).copied().unwrap_or(0)]);

        // Device Type (2 bytes)
        let raw = self.get_attribute(dest_mac_id, DnClass::Identity, 1,
                                     IdentityAttr::DeviceType as u8)?;
        identity.device_type = u16::from_le_bytes([raw[0], raw.get(1).copied().unwrap_or(0)]);

        // Product Code (2 bytes)
        let raw = self.get_attribute(dest_mac_id, DnClass::Identity, 1,
                                     IdentityAttr::ProductCode as u8)?;
        identity.product_code = u16::from_le_bytes([raw[0], raw.get(1).copied().unwrap_or(0)]);

        // Revision: 2 bytes [major, minor]
        let raw = self.get_attribute(dest_mac_id, DnClass::Identity, 1,
                                     IdentityAttr::Revision as u8)?;
        identity.revision = (raw[0], raw.get(1).copied().unwrap_or(0));

        // Serial Number (4 bytes, little-endian)
        let raw = self.get_attribute(dest_mac_id, DnClass::Identity, 1,
                                     IdentityAttr::SerialNo as u8)?;
        identity.serial_no = u32::from_le_bytes([
            raw[0],
            raw.get(1).copied().unwrap_or(0),
            raw.get(2).copied().unwrap_or(0),
            raw.get(3).copied().unwrap_or(0),
        ]);

        println!("[DeviceNet] Node {} Identity: VendorID=0x{:04X} DeviceType=0x{:04X} \
                  Rev={}.{} SerialNo=0x{:08X}",
                 dest_mac_id,
                 identity.vendor_id, identity.device_type,
                 identity.revision.0, identity.revision.1,
                 identity.serial_no);

        Ok(identity)
    }
} // impl DeviceNetMaster
```

### 7.6 Main Entry Point

```rust
fn main() -> Result<()> {
    // Create master node with MAC ID 0 on can0
    let master = DeviceNetMaster::new("can0", 0)?;

    // Duplicate MAC ID check
    master.duplicate_mac_check()?;

    // Read identity from slave at MAC ID 5
    let identity = master.read_identity(5)?;
    println!("Queried device: {:?}", identity);

    // Set an attribute: write 0x01 to DeviceNet Object (0x03), instance 1, baud rate attr
    master.set_attribute(5, DnClass::DeviceNet, 1, 0x04, &[0x01])?;

    // Polled I/O: send 0xFF as output, read inputs from slave 5
    let output = [0xFFu8];
    let inputs = master.polled_io(5, &output)?;
    print!("[DeviceNet] Polled I/O inputs from slave 5: ");
    for byte in &inputs {
        print!("0x{:02X} ", byte);
    }
    println!();

    Ok(())
}
```

---

## 8. Error Handling and Diagnostics

DeviceNet defines a structured error response format for explicit messages:

| Field | Description |
|-------|-------------|
| Service Code | 0x8E (error indicator) |
| General Status | Root cause (e.g., 0x08 = Service not supported) |
| Additional Status | Optional additional detail |

Common general status codes:

| Code | Meaning |
|------|---------|
| 0x00 | Success |
| 0x08 | Service not supported |
| 0x09 | Invalid attribute value |
| 0x0C | Object does not exist |
| 0x0F | Attribute not settable |
| 0x14 | Already in requested state |

---

## 9. Key Differences vs. Other CAN Protocols

| Feature | DeviceNet | CANopen | SAE J1939 |
|---------|-----------|---------|-----------|
| Node limit | 64 | 127 | 253 (multi-segment) |
| Address space | MAC ID (6-bit) | Node ID (7-bit) | Source Address (8-bit) |
| Object model | CIP (flat class/instance/attr) | EDS / Object Dictionary | SPN / PGN |
| I/O model | Predefined Connection Sets | PDO | PGN broadcast |
| Target domain | Factory automation | Machine control | Trucks / vehicles |
| Power on cable | Yes (24 VDC) | Optional | No |
| Managed by | ODVA | CiA | SAE |

---

## Summary

DeviceNet is a mature, well-specified industrial CAN protocol that layers a rich object model and connection management on top of CAN 2.0A. Its key characteristics are:

- **64-node limit** determined by the 6-bit MAC ID embedded in the 11-bit CAN identifier.
- **Two messaging paradigms**: explicit (request/response for configuration) and implicit (high-speed I/O for real-time data).
- **Predefined Connection Sets** that allow standard I/O devices to interoperate without device-specific configuration.
- **CIP Object Model** (Common Industrial Protocol) that provides a uniform Class/Instance/Attribute addressing scheme across all DeviceNet (and EtherNet/IP) devices.
- **Duplicate MAC ID Check** ensures address uniqueness on startup.
- **24 VDC power on the cable** simplifies field wiring for simple devices.

In C/C++, implementation typically uses the Linux SocketCAN API directly, constructing CAN frames that encode the DeviceNet group, MAC ID, and explicit message payloads. In Rust, the `socketcan` crate provides a safe, idiomatic abstraction over the same kernel API, enabling compile-time safety without sacrificing low-level control. Both languages require careful frame construction to respect DeviceNet's group-based CAN ID partitioning and the structured explicit message byte layout.

DeviceNet remains widely deployed in legacy factory automation systems, and understanding it is essential for integrating PLCs, drives, sensors, and I/O blocks in environments standardized on ODVA technology.

---

*References: ODVA DeviceNet Specification (Edition 3.x), IEC 62026-3, Allen-Bradley DeviceNet Media Design Installation Guide.*