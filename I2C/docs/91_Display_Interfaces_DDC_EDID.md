# 91. Display Interfaces (DDC/EDID) — I2C for Monitor Communication

**Protocol Architecture** — How the I2C bus is embedded in VGA/DVI/HDMI/DP connectors, the two I2C addresses (`0x50` for EDID, `0x37` for DDC/CI), and how host and monitor exchange messages.

**EDID Structure** — Full byte-level layout of the 128-byte EDID block, manufacturer ID decoding (packed 5-bit ASCII), descriptor blocks (monitor name, serial, range limits), and CEA-861 extension block parsing for HDMI.

**DDC/CI Protocol** — Message framing with length/source/dest/checksum fields, the mandatory 40 ms inter-message delay, and a VCP feature code reference table.

**C/C++ Code Examples:**
- Raw EDID reader via `I2C_RDWR` ioctl with full struct parsing
- `DdcCiDevice` class in C++ for Get/Set VCP with brightness scaling
- CEA-861 extension parser covering audio, video, vendor-specific (HDMI OUI) and speaker blocks

**Rust Code Examples:**
- EDID reader using unsafe FFI ioctl with idiomatic struct ownership
- `DdcCi<B: I2cBus>` generic abstraction with a `LinuxI2cBus` backend — fully trait-separated for testability
- Standalone EDID file verifier for checking binary EDID dumps from `/sys/class/drm/`

**Advanced Topics** — EDID override/spoofing, DisplayPort MST DDC tunneling, MCCS capability string format, and thread safety.

**Troubleshooting Table** — Common failure modes with root causes and fixes.


---

## Table of Contents

1. [Introduction](#introduction)
2. [Protocol Architecture](#protocol-architecture)
3. [EDID Structure Deep Dive](#edid-structure-deep-dive)
4. [DDC/CI Command Reference](#ddci-command-reference)
5. [Hardware Setup & Addressing](#hardware-setup--addressing)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Topics](#advanced-topics)
9. [Troubleshooting](#troubleshooting)
10. [Summary](#summary)

---

## Introduction

Modern monitors communicate with host systems through a dedicated I2C bus embedded in the display connector. This channel supports two complementary standards:

- **EDID (Extended Display Identification Data)** — a read-only EEPROM inside the monitor that describes its capabilities (resolution, refresh rates, color gamut, manufacturer, serial number, etc.)
- **DDC/CI (Display Data Channel / Command Interface)** — a bidirectional protocol layered on top of DDC that allows software to read and *control* monitor settings at runtime (brightness, contrast, input source, color temperature, etc.)

Both protocols share the same two-wire I2C bus running at 100 kHz (standard mode), routed through the display cable (VGA, DVI, HDMI, or DisplayPort all carry it).

### Why This Matters

| Use Case | Protocol | Direction |
|---|---|---|
| Auto-configure resolution on boot | EDID | Host ← Monitor |
| Read monitor make/model in software | EDID | Host ← Monitor |
| Adjust brightness via OS power management | DDC/CI | Host → Monitor |
| Query current input source | DDC/CI | Host ↔ Monitor |
| Build KVM or display management tools | DDC/CI | Host ↔ Monitor |
| Firmware update of monitor MCU | Proprietary DDC/CI extension | Host → Monitor |

---

## Protocol Architecture

```
Host System                        Monitor
─────────────────────────────────────────────────────────
┌──────────────┐   I2C @ 100 kHz  ┌──────────────────────┐
│  GPU / iGPU  │◄─────────────────►│   Monitor Controller │
│              │   SCL / SDA       │                      │
│  DDC Host    │                   │  ┌────────────────┐  │
└──────────────┘                   │  │  EDID EEPROM   │  │
                                   │  │  (256 bytes)   │  │
  Connector Pin Mapping:           │  └────────────────┘  │
  VGA  → Pin 9 (SCL), Pin 5 (SDA) │  ┌────────────────┐  │
  DVI  → Pin 6 (SCL), Pin 7 (SDA) │  │  DDC/CI MCU    │  │
  HDMI → Pin 15(SCL), Pin 16(SDA) │  │  (VCP controls)│  │
  DP   → AUX channel (DPCD)       │  └────────────────┘  │
                                   └──────────────────────┘
```

### I2C Addresses Used

| Address (7-bit) | Purpose |
|---|---|
| `0x50` | EDID EEPROM (read-only, standard) |
| `0x37` | DDC/CI command interface (read/write) |
| `0x51` | DDC/CI response address (used in protocol framing) |
| `0x6E` | MCCS (Monitor Control Command Set) segment pointer |

> **Note:** For DisplayPort, EDID is accessed through DPCD (DisplayPort Configuration Data) registers over the AUX channel, not raw I2C, though the EDID data format is identical.

---

## EDID Structure Deep Dive

EDID is a 128-byte (or 256-byte for EDID 1.4+) block with a well-defined binary layout. Extension blocks can add another 128 bytes each (CEA-861 for HDMI audio/video info, DisplayID, etc.).

```
Offset  Size   Field
──────────────────────────────────────────────────────────
0x00    8      Fixed header: 00 FF FF FF FF FF FF 00
0x08    2      Manufacturer ID (3-letter packed ASCII)
0x0A    2      Product code
0x0C    4      Serial number (binary)
0x10    1      Week of manufacture
0x11    1      Year of manufacture (+ 1990)
0x12    1      EDID version (usually 1)
0x13    1      EDID revision (usually 3 or 4)
0x14    1      Video input definition
0x15    1      Horizontal size (cm)
0x16    1      Vertical size (cm)
0x17    1      Display gamma (value = (gamma × 100) − 100)
0x18    1      Feature support bitmask
0x19    10     Chromaticity coordinates (CIE 1931 xy)
0x23    3      Established timing bitmaps
0x26    16     Standard timing information (8 × 2 bytes)
0x36    72     Four 18-byte descriptor blocks:
               - Detailed Timing Descriptor (preferred mode)
               - Monitor name (tag 0xFC)
               - Monitor serial string (tag 0xFF)
               - Range limits (tag 0xFD)
0x7E    1      Number of extension blocks
0x7F    1      Checksum (sum of all 128 bytes = 0 mod 256)
```

### Manufacturer ID Decoding

The 2-byte manufacturer ID encodes a 3-letter PNP code. Each letter is stored as (letter − 'A' + 1) in 5-bit fields:

```
Bits [14:10] → Letter 1
Bits  [9:5]  → Letter 2
Bits  [4:0]  → Letter 3
```

Examples: `DEL` = Dell, `SAM` = Samsung, `LEN` = Lenovo, `AUO` = AU Optronics.

---

## DDC/CI Command Reference

DDC/CI messages follow the MCCS (Monitor Control Command Set) specification. Each message has a specific framing:

### Message Framing

```
Write (Host → Monitor, I2C address 0x37):
  [0x6E] [length | 0x80] [0x51] [opcode] [param_high] [param_low] [checksum]
   ↑ dest   ↑ byte count   ↑ src

Read (Host reads from 0x37, monitor replies):
  [0x6F] [length | 0x80] [0x6E] [result_high] [result_low] [checksum]
```

The checksum is XOR of all bytes in the message including the address byte.

### VCP (Virtual Control Panel) Feature Codes

| Code | Feature | Access |
|---|---|---|
| `0x10` | Brightness | R/W |
| `0x12` | Contrast | R/W |
| `0x16` | Red video gain | R/W |
| `0x18` | Green video gain | R/W |
| `0x1A` | Blue video gain | R/W |
| `0x60` | Input source select | R/W |
| `0xD6` | Power mode | R/W |
| `0xDF` | VCP version | R |
| `0xC8` | Display controller type | R |
| `0xC9` | Display firmware level | R |

### VCP Get/Set Protocol

**Get VCP Feature (opcode 0x01):**
```
Write: 6E 82 01 <vcp_code> <checksum>
Read:  6F 88 02 <vcp_code> <result_code> <vh> <vl> <mh> <ml> <checksum>
         │  │  │           │             │    │              └── max value
         │  │  │           │             └────┴── current value
         │  │  └── reply opcode           0=no error, 1=unsupported
         │  └── length
         └── source 0x6F
```

**Set VCP Feature (opcode 0x03):**
```
Write: 6E 84 03 <vcp_code> <value_high> <value_low> <checksum>
```

---

## Hardware Setup & Addressing

### Linux: Accessing DDC/EDID via `/dev/i2c-*`

On Linux, the GPU driver exposes the DDC bus as an I2C adapter. You can discover them:

```bash
# List all I2C buses
i2cdetect -l

# Scan a specific bus (usually bus 3, 5, or 7 for display DDC)
i2cdetect -y 5

# Read raw EDID (128 bytes from address 0x50)
i2cget -y 5 0x50 0x00 i 128

# Or use dedicated tool
edid-decode /sys/class/drm/card0-HDMI-A-1/edid
```

### Windows: Using SetupAPI / Monitor EDID Registry

On Windows, EDID is cached in the registry at:
```
HKLM\SYSTEM\CurrentControlSet\Enum\DISPLAY\<MonitorID>\<Instance>\Device Parameters\EDID
```

DDC/CI access requires either WinDDK monitor driver APIs or a user-mode I2C library like `ddcutil`.

---

## C/C++ Implementation

### 1. Reading EDID via Linux I2C (`/dev/i2c-N`)

```c
// edid_reader.c
// gcc -o edid_reader edid_reader.c

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

#define EDID_I2C_ADDR    0x50
#define EDID_LENGTH      128
#define EDID_HEADER      "\x00\xFF\xFF\xFF\xFF\xFF\xFF\x00"

typedef struct {
    uint8_t  data[EDID_LENGTH];
    char     manufacturer[4];
    uint16_t product_code;
    uint32_t serial_number;
    uint8_t  week;
    uint16_t year;
    uint8_t  version;
    uint8_t  revision;
    uint8_t  h_size_cm;
    uint8_t  v_size_cm;
    float    gamma;
    char     monitor_name[14];
    char     monitor_serial[14];
} edid_info_t;

static int i2c_read_edid(int fd, uint8_t i2c_addr, uint8_t reg_offset,
                          uint8_t *buf, size_t len)
{
    struct i2c_rdwr_ioctl_data msgset;
    struct i2c_msg msgs[2];

    /* Write register offset (segment pointer) */
    msgs[0].addr  = i2c_addr;
    msgs[0].flags = 0;          /* write */
    msgs[0].len   = 1;
    msgs[0].buf   = &reg_offset;

    /* Read data */
    msgs[1].addr  = i2c_addr;
    msgs[1].flags = I2C_M_RD;  /* read */
    msgs[1].len   = (uint16_t)len;
    msgs[1].buf   = buf;

    msgset.msgs  = msgs;
    msgset.nmsgs = 2;

    if (ioctl(fd, I2C_RDWR, &msgset) < 0) {
        perror("I2C_RDWR ioctl failed");
        return -1;
    }
    return 0;
}

static int validate_edid_checksum(const uint8_t *data)
{
    uint8_t sum = 0;
    for (int i = 0; i < EDID_LENGTH; i++)
        sum += data[i];
    return (sum == 0) ? 0 : -1;
}

static void decode_manufacturer_id(const uint8_t *data, char *out)
{
    /* Packed in bytes 8-9: bits[14:10] letter1, [9:5] letter2, [4:0] letter3 */
    uint16_t packed = ((uint16_t)data[8] << 8) | data[9];
    out[0] = 'A' + ((packed >> 10) & 0x1F) - 1;
    out[1] = 'A' + ((packed >>  5) & 0x1F) - 1;
    out[2] = 'A' + ((packed >>  0) & 0x1F) - 1;
    out[3] = '\0';
}

static void decode_descriptor_block(const uint8_t *block, char *name_buf,
                                     char *serial_buf)
{
    /* 18-byte descriptor: if bytes 0-1 are 0, it's a monitor descriptor */
    if (block[0] == 0x00 && block[1] == 0x00 && block[2] == 0x00) {
        uint8_t tag = block[3];
        if ((tag == 0xFC || tag == 0xFF) && name_buf != NULL) {
            /* bytes 5..17 = 13 bytes of ASCII, padded with 0x0A then spaces */
            char *dst = (tag == 0xFC) ? name_buf : serial_buf;
            if (dst) {
                strncpy(dst, (const char *)&block[5], 13);
                dst[13] = '\0';
                /* strip trailing newline/spaces */
                for (int i = 12; i >= 0 && (dst[i] == '\n' || dst[i] == ' '); i--)
                    dst[i] = '\0';
            }
        }
    }
}

int edid_parse(const uint8_t *raw, edid_info_t *info)
{
    if (memcmp(raw, EDID_HEADER, 8) != 0) {
        fprintf(stderr, "Invalid EDID header\n");
        return -1;
    }
    if (validate_edid_checksum(raw) != 0) {
        fprintf(stderr, "EDID checksum failed\n");
        return -1;
    }

    memcpy(info->data, raw, EDID_LENGTH);

    decode_manufacturer_id(raw, info->manufacturer);
    info->product_code   = ((uint16_t)raw[0x09] << 8) | raw[0x08];
    info->serial_number  = ((uint32_t)raw[0x0F] << 24) | ((uint32_t)raw[0x0E] << 16)
                         | ((uint32_t)raw[0x0D] <<  8) |  (uint32_t)raw[0x0C];
    info->week           = raw[0x10];
    info->year           = (uint16_t)raw[0x11] + 1990;
    info->version        = raw[0x12];
    info->revision       = raw[0x13];
    info->h_size_cm      = raw[0x15];
    info->v_size_cm      = raw[0x16];
    info->gamma          = (raw[0x17] + 100) / 100.0f;

    memset(info->monitor_name,   0, sizeof(info->monitor_name));
    memset(info->monitor_serial, 0, sizeof(info->monitor_serial));

    /* Descriptor blocks at offsets 0x36, 0x48, 0x5A, 0x6C */
    for (int d = 0; d < 4; d++) {
        decode_descriptor_block(&raw[0x36 + d * 18],
                                info->monitor_name,
                                info->monitor_serial);
    }
    return 0;
}

void edid_print(const edid_info_t *info)
{
    printf("Manufacturer:   %s\n",    info->manufacturer);
    printf("Product Code:   0x%04X\n", info->product_code);
    printf("Serial (bin):   %u\n",    info->serial_number);
    printf("Manufacture:    Week %u, Year %u\n", info->week, info->year);
    printf("EDID Version:   %u.%u\n", info->version, info->revision);
    printf("Physical Size:  %u cm x %u cm\n", info->h_size_cm, info->v_size_cm);
    printf("Gamma:          %.2f\n",  info->gamma);
    printf("Monitor Name:   %s\n",    info->monitor_name[0]   ? info->monitor_name   : "(none)");
    printf("Monitor Serial: %s\n",    info->monitor_serial[0] ? info->monitor_serial : "(none)");
}

int main(int argc, char *argv[])
{
    const char *i2c_dev = (argc > 1) ? argv[1] : "/dev/i2c-5";
    int fd;
    uint8_t raw[EDID_LENGTH];
    edid_info_t info;

    fd = open(i2c_dev, O_RDWR);
    if (fd < 0) {
        fprintf(stderr, "Cannot open %s: %s\n", i2c_dev, strerror(errno));
        return 1;
    }

    printf("Reading EDID from %s (I2C addr 0x%02X)...\n", i2c_dev, EDID_I2C_ADDR);

    if (i2c_read_edid(fd, EDID_I2C_ADDR, 0x00, raw, EDID_LENGTH) != 0) {
        close(fd);
        return 1;
    }

    printf("\nRaw EDID bytes:\n");
    for (int i = 0; i < EDID_LENGTH; i++) {
        printf("%02X ", raw[i]);
        if ((i + 1) % 16 == 0) printf("\n");
    }
    printf("\n");

    if (edid_parse(raw, &info) == 0) {
        printf("\nDecoded EDID:\n");
        edid_print(&info);
    }

    close(fd);
    return 0;
}
```

### 2. DDC/CI Brightness Control in C++

```cpp
// ddc_control.cpp
// g++ -o ddc_control ddc_control.cpp

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <linux/i2c.h>
#include <stdexcept>
#include <optional>

// DDC/CI I2C addresses
static constexpr uint8_t DDC_CI_ADDR     = 0x37;
static constexpr uint8_t DDC_CI_HOST_SRC = 0x51;
static constexpr uint8_t DDC_CI_MON_DST  = 0x6E;
static constexpr uint8_t DDC_CI_MON_SRC  = 0x6F;

// VCP feature codes
static constexpr uint8_t VCP_BRIGHTNESS   = 0x10;
static constexpr uint8_t VCP_CONTRAST     = 0x12;
static constexpr uint8_t VCP_INPUT_SOURCE = 0x60;
static constexpr uint8_t VCP_POWER_MODE   = 0xD6;

// DDC/CI opcodes
static constexpr uint8_t OPCODE_GET_VCP = 0x01;
static constexpr uint8_t OPCODE_SET_VCP = 0x03;
static constexpr uint8_t OPCODE_GET_VCP_REPLY = 0x02;

struct VcpReply {
    uint8_t  result_code;   // 0 = success, 1 = unsupported
    uint8_t  vcp_code;
    uint16_t current_value;
    uint16_t max_value;
};

class DdcCiDevice {
public:
    explicit DdcCiDevice(const char *i2c_path) {
        fd_ = open(i2c_path, O_RDWR);
        if (fd_ < 0)
            throw std::runtime_error(std::string("Cannot open ") + i2c_path
                                     + ": " + strerror(errno));
    }

    ~DdcCiDevice() {
        if (fd_ >= 0) close(fd_);
    }

    // Non-copyable
    DdcCiDevice(const DdcCiDevice &) = delete;
    DdcCiDevice &operator=(const DdcCiDevice &) = delete;

    std::optional<VcpReply> get_vcp(uint8_t vcp_code) {
        // Build Get VCP request: dest, len, src, opcode, vcp_code, checksum
        uint8_t msg[5];
        msg[0] = DDC_CI_MON_DST;
        msg[1] = 0x80 | 0x02;      // length = 2 bytes of payload
        msg[2] = DDC_CI_HOST_SRC;
        msg[3] = OPCODE_GET_VCP;
        msg[4] = vcp_code;

        uint8_t chk = 0;
        for (int i = 0; i < 5; i++) chk ^= msg[i];

        uint8_t packet[6];
        memcpy(packet, msg, 5);
        packet[5] = chk;

        if (i2c_write(DDC_CI_ADDR, packet, 6) < 0)
            return std::nullopt;

        // DDC/CI requires a minimum 40ms delay before reading reply
        usleep(50000);

        uint8_t reply[12] = {};
        if (i2c_read(DDC_CI_ADDR, reply, 12) < 0)
            return std::nullopt;

        // Validate reply framing
        // reply[0] = source (0x6F), reply[1] = length|0x80, reply[2] = dest (0x6E)
        // reply[3] = opcode (0x02), reply[4] = result, reply[5] = vcp_code
        // reply[6..7] = max_high/max_low, reply[8..9] = cur_high/cur_low, reply[10] = chk
        if (reply[3] != OPCODE_GET_VCP_REPLY) {
            fprintf(stderr, "Unexpected reply opcode: 0x%02X\n", reply[3]);
            return std::nullopt;
        }

        VcpReply r{};
        r.result_code   = reply[4];
        r.vcp_code      = reply[5];
        r.max_value     = (static_cast<uint16_t>(reply[6]) << 8) | reply[7];
        r.current_value = (static_cast<uint16_t>(reply[8]) << 8) | reply[9];
        return r;
    }

    bool set_vcp(uint8_t vcp_code, uint16_t value) {
        // Build Set VCP request: dest, len, src, opcode, vcp_code, val_h, val_l, checksum
        uint8_t msg[7];
        msg[0] = DDC_CI_MON_DST;
        msg[1] = 0x80 | 0x04;      // length = 4 bytes of payload
        msg[2] = DDC_CI_HOST_SRC;
        msg[3] = OPCODE_SET_VCP;
        msg[4] = vcp_code;
        msg[5] = static_cast<uint8_t>(value >> 8);
        msg[6] = static_cast<uint8_t>(value & 0xFF);

        uint8_t chk = 0;
        for (int i = 0; i < 7; i++) chk ^= msg[i];

        uint8_t packet[8];
        memcpy(packet, msg, 7);
        packet[7] = chk;

        return i2c_write(DDC_CI_ADDR, packet, 8) == 0;
    }

    bool set_brightness(uint8_t percent) {
        // Get max brightness first to scale properly
        auto reply = get_vcp(VCP_BRIGHTNESS);
        if (!reply) return false;

        uint16_t target = static_cast<uint16_t>(
            (static_cast<uint32_t>(percent) * reply->max_value) / 100);
        printf("Setting brightness: %u%% → value %u (max %u)\n",
               percent, target, reply->max_value);
        return set_vcp(VCP_BRIGHTNESS, target);
    }

private:
    int fd_;

    int i2c_write(uint8_t addr, const uint8_t *buf, size_t len) {
        struct i2c_msg msg;
        struct i2c_rdwr_ioctl_data msgset;

        msg.addr  = addr;
        msg.flags = 0;
        msg.len   = static_cast<uint16_t>(len);
        msg.buf   = const_cast<uint8_t *>(buf);

        msgset.msgs  = &msg;
        msgset.nmsgs = 1;

        if (ioctl(fd_, I2C_RDWR, &msgset) < 0) {
            perror("i2c_write failed");
            return -1;
        }
        return 0;
    }

    int i2c_read(uint8_t addr, uint8_t *buf, size_t len) {
        struct i2c_msg msg;
        struct i2c_rdwr_ioctl_data msgset;

        msg.addr  = addr;
        msg.flags = I2C_M_RD;
        msg.len   = static_cast<uint16_t>(len);
        msg.buf   = buf;

        msgset.msgs  = &msg;
        msgset.nmsgs = 1;

        if (ioctl(fd_, I2C_RDWR, &msgset) < 0) {
            perror("i2c_read failed");
            return -1;
        }
        return 0;
    }
};

int main(int argc, char *argv[]) {
    const char *dev = (argc > 1) ? argv[1] : "/dev/i2c-5";

    try {
        DdcCiDevice ddc(dev);

        printf("=== DDC/CI Monitor Control ===\n\n");

        // Query brightness
        auto br = ddc.get_vcp(VCP_BRIGHTNESS);
        if (br) {
            printf("Brightness: %u / %u (%.0f%%)\n",
                   br->current_value, br->max_value,
                   br->max_value > 0
                   ? (100.0 * br->current_value / br->max_value)
                   : 0.0);
        }

        // Query contrast
        auto ct = ddc.get_vcp(VCP_CONTRAST);
        if (ct) {
            printf("Contrast:   %u / %u\n", ct->current_value, ct->max_value);
        }

        // Set brightness to 70%
        if (argc > 2) {
            int pct = atoi(argv[2]);
            if (pct >= 0 && pct <= 100) {
                if (ddc.set_brightness(static_cast<uint8_t>(pct)))
                    printf("Brightness set to %d%%\n", pct);
                else
                    fprintf(stderr, "Failed to set brightness\n");
            }
        }

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }

    return 0;
}
```

### 3. EDID Extension Block Parsing (CEA-861 / HDMI)

```c
// cea_extension.c — Parse CEA-861 HDMI extension block (byte 128-255)

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define CEA_EXT_TAG     0x02
#define HDMI_OUI        0x000C03   // HDMI Licensing LLC OUI

typedef struct {
    uint8_t  tag;        // 0x02 = CEA-861
    uint8_t  revision;
    uint8_t  dtd_offset; // offset to first DTD from start of block
    uint8_t  native_dtds_flags;
    uint8_t  data[124];  // data block collection
} cea_extension_t;

typedef struct {
    uint8_t  tag_code;   // bits [7:5]: tag, bits [4:0]: length
    uint8_t  payload[31];
} data_block_t;

static const char *get_audio_format_name(uint8_t format)
{
    switch (format) {
    case 1:  return "LPCM";
    case 2:  return "AC-3";
    case 3:  return "MPEG-1";
    case 4:  return "MP3";
    case 5:  return "MPEG-2";
    case 6:  return "AAC LC";
    case 7:  return "DTS";
    case 8:  return "ATRAC";
    case 9:  return "DSD (SACD)";
    case 10: return "E-AC-3";
    case 11: return "DTS-HD";
    case 12: return "MAT (TrueHD)";
    default: return "Unknown";
    }
}

void parse_cea_extension(const uint8_t *ext_block)
{
    if (ext_block[0] != CEA_EXT_TAG) {
        printf("Not a CEA-861 extension block (tag=0x%02X)\n", ext_block[0]);
        return;
    }

    uint8_t revision   = ext_block[1];
    uint8_t dtd_offset = ext_block[2];
    uint8_t flags      = ext_block[3];

    printf("CEA-861 Extension, Revision %u\n", revision);
    printf("  Native DTDs: %u\n", flags & 0x0F);
    printf("  Underscan:   %s\n", (flags & 0x80) ? "yes" : "no");
    printf("  Audio:       %s\n", (flags & 0x40) ? "yes" : "no");
    printf("  YCbCr 4:4:4: %s\n", (flags & 0x20) ? "yes" : "no");
    printf("  YCbCr 4:2:2: %s\n", (flags & 0x10) ? "yes" : "no");

    /* Walk the Data Block Collection (bytes 4 to dtd_offset-1) */
    const uint8_t *p   = ext_block + 4;
    const uint8_t *end = ext_block + dtd_offset;

    while (p < end) {
        uint8_t tag    = (*p >> 5) & 0x07;
        uint8_t length = *p & 0x1F;
        p++;

        switch (tag) {
        case 1: {  /* Audio Data Block */
            printf("\n  Audio Data Block (%u bytes):\n", length);
            for (uint8_t i = 0; i + 2 < length; i += 3) {
                uint8_t fmt  = (p[i] >> 3) & 0x0F;
                uint8_t ch   = (p[i] & 0x07) + 1;
                uint8_t freq = p[i + 1];
                printf("    Format: %-16s  Channels: %u  Rates: 0x%02X\n",
                       get_audio_format_name(fmt), ch, freq);
            }
            break;
        }
        case 2: {  /* Video Data Block (SVDs) */
            printf("\n  Video Data Block (%u VIC codes):\n", length);
            for (uint8_t i = 0; i < length; i++) {
                uint8_t vic    = p[i] & 0x7F;
                uint8_t native = (p[i] >> 7) & 1;
                printf("    VIC %3u%s\n", vic, native ? " (native)" : "");
            }
            break;
        }
        case 3: {  /* Vendor Specific Data Block */
            if (length >= 3) {
                uint32_t oui = ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
                printf("\n  Vendor Specific Block, OUI=0x%06X", oui);
                if (oui == HDMI_OUI) {
                    uint8_t a = p[3] & 0x0F;
                    uint8_t b = (p[3] >> 4) & 0x0F;
                    printf(" (HDMI LLC)\n");
                    printf("    HDMI Source Phy Addr: %u.%u.%u.%u\n",
                           (p[4] >> 4), (p[4] & 0x0F),
                           (p[5] >> 4), (p[5] & 0x0F));
                    if (length >= 7) {
                        printf("    Deep Color 48-bit: %s\n", (p[6] & 0x40) ? "yes" : "no");
                        printf("    Deep Color 36-bit: %s\n", (p[6] & 0x20) ? "yes" : "no");
                        printf("    Deep Color 30-bit: %s\n", (p[6] & 0x10) ? "yes" : "no");
                        printf("    Deep Color Y444:   %s\n", (p[6] & 0x08) ? "yes" : "no");
                        printf("    DVI Dual:          %s\n", (p[6] & 0x01) ? "yes" : "no");
                    }
                } else {
                    printf("\n");
                }
            }
            break;
        }
        case 4: {  /* Speaker Allocation Block */
            if (length >= 1) {
                printf("\n  Speaker Allocation: 0x%02X\n", p[0]);
                if (p[0] & 0x01) printf("    Front Left + Right\n");
                if (p[0] & 0x02) printf("    LFE\n");
                if (p[0] & 0x04) printf("    Front Center\n");
                if (p[0] & 0x08) printf("    Rear Left + Right\n");
                if (p[0] & 0x10) printf("    Rear Center\n");
            }
            break;
        }
        default:
            printf("\n  Unknown Block, tag=%u, length=%u\n", tag, length);
            break;
        }
        p += length;
    }
}
```

---

## Rust Implementation

### 1. EDID Reader in Rust (Linux `/dev/i2c-*`)

```rust
// edid_reader/src/main.rs
// Cargo.toml: [dependencies]  (no external deps needed for basic I2C)

use std::fs::OpenOptions;
use std::os::unix::io::AsRawFd;
use std::path::PathBuf;

// I2C ioctl constants (from linux/i2c-dev.h)
const I2C_RDWR: u64 = 0x0707;
const I2C_M_RD: u16 = 0x0001;

const EDID_I2C_ADDR: u16 = 0x50;
const EDID_LENGTH: usize  = 128;
const EDID_HEADER: [u8; 8] = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];

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

#[derive(Debug)]
pub struct EdidInfo {
    pub manufacturer:   String,
    pub product_code:   u16,
    pub serial_number:  u32,
    pub week:           u8,
    pub year:           u16,
    pub version:        u8,
    pub revision:       u8,
    pub h_size_cm:      u8,
    pub v_size_cm:      u8,
    pub gamma:          f32,
    pub monitor_name:   String,
    pub monitor_serial: String,
}

fn i2c_read_edid(fd: i32, addr: u16, reg: u8, len: usize)
    -> Result<Vec<u8>, String>
{
    let mut reg_byte = [reg];
    let mut data = vec![0u8; len];

    let mut msgs = [
        I2cMsg {
            addr,
            flags: 0,   // write
            len:   1,
            buf:   reg_byte.as_mut_ptr(),
        },
        I2cMsg {
            addr,
            flags: I2C_M_RD,
            len:   len as u16,
            buf:   data.as_mut_ptr(),
        },
    ];

    let mut ioctl_data = I2cRdwrIoctlData {
        msgs:  msgs.as_mut_ptr(),
        nmsgs: 2,
    };

    let ret = unsafe {
        libc_ioctl(fd, I2C_RDWR, &mut ioctl_data as *mut _ as u64)
    };

    if ret < 0 {
        return Err(format!("I2C_RDWR ioctl failed: errno {}", get_errno()));
    }
    Ok(data)
}

// Minimal ioctl wrapper (avoid pulling in libc crate)
extern "C" {
    fn ioctl(fd: i32, request: u64, ...) -> i32;
    fn __errno_location() -> *mut i32;
}

fn libc_ioctl(fd: i32, request: u64, arg: u64) -> i32 {
    unsafe { ioctl(fd, request, arg) }
}

fn get_errno() -> i32 {
    unsafe { *__errno_location() }
}

fn validate_checksum(data: &[u8]) -> bool {
    data.iter().fold(0u8, |acc, &b| acc.wrapping_add(b)) == 0
}

fn decode_manufacturer_id(data: &[u8]) -> String {
    let packed = ((data[8] as u16) << 8) | (data[9] as u16);
    let c1 = b'A' + ((packed >> 10) & 0x1F) as u8 - 1;
    let c2 = b'A' + ((packed >>  5) & 0x1F) as u8 - 1;
    let c3 = b'A' + ((packed >>  0) & 0x1F) as u8 - 1;
    String::from_utf8_lossy(&[c1, c2, c3]).into_owned()
}

fn decode_descriptor_string(block: &[u8]) -> Option<String> {
    if block[0] == 0x00 && block[1] == 0x00 && block[2] == 0x00
        && (block[3] == 0xFC || block[3] == 0xFF)
    {
        let raw = &block[5..18];
        let s: String = raw
            .iter()
            .take_while(|&&b| b != 0x0A && b != 0x00)
            .map(|&b| b as char)
            .collect::<String>()
            .trim_end()
            .to_owned();
        return Some(s);
    }
    None
}

fn parse_edid(raw: &[u8]) -> Result<EdidInfo, String> {
    if &raw[..8] != EDID_HEADER {
        return Err("Invalid EDID header".to_string());
    }
    if !validate_checksum(raw) {
        return Err("EDID checksum mismatch".to_string());
    }

    let manufacturer  = decode_manufacturer_id(raw);
    let product_code  = (raw[0x09] as u16) << 8 | raw[0x08] as u16;
    let serial_number = (raw[0x0F] as u32) << 24 | (raw[0x0E] as u32) << 16
                      | (raw[0x0D] as u32) <<  8 | raw[0x0C] as u32;

    let mut monitor_name   = String::new();
    let mut monitor_serial = String::new();

    for d in 0..4usize {
        let blk = &raw[0x36 + d * 18..0x36 + d * 18 + 18];
        if let Some(s) = decode_descriptor_string(blk) {
            match blk[3] {
                0xFC => monitor_name   = s,
                0xFF => monitor_serial = s,
                _ => {}
            }
        }
    }

    Ok(EdidInfo {
        manufacturer,
        product_code,
        serial_number,
        week:           raw[0x10],
        year:           raw[0x11] as u16 + 1990,
        version:        raw[0x12],
        revision:       raw[0x13],
        h_size_cm:      raw[0x15],
        v_size_cm:      raw[0x16],
        gamma:          (raw[0x17] as f32 + 100.0) / 100.0,
        monitor_name,
        monitor_serial,
    })
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    let dev_path = args.get(1).map(String::as_str).unwrap_or("/dev/i2c-5");

    let file = OpenOptions::new()
        .read(true)
        .write(true)
        .open(dev_path)
        .expect(&format!("Cannot open {}", dev_path));

    let fd = file.as_raw_fd();

    println!("Reading EDID from {} ...", dev_path);

    let raw = i2c_read_edid(fd, EDID_I2C_ADDR, 0x00, EDID_LENGTH)
        .expect("Failed to read EDID");

    print!("\nRaw bytes:\n");
    for (i, b) in raw.iter().enumerate() {
        print!("{:02X} ", b);
        if (i + 1) % 16 == 0 { println!(); }
    }
    println!();

    match parse_edid(&raw) {
        Ok(info) => {
            println!("\nDecoded EDID:");
            println!("  Manufacturer:   {}", info.manufacturer);
            println!("  Product Code:   0x{:04X}", info.product_code);
            println!("  Serial (bin):   {}", info.serial_number);
            println!("  Manufacture:    Week {}, Year {}", info.week, info.year);
            println!("  EDID Version:   {}.{}", info.version, info.revision);
            println!("  Physical Size:  {} cm × {} cm", info.h_size_cm, info.v_size_cm);
            println!("  Gamma:          {:.2}", info.gamma);
            println!("  Monitor Name:   {}", if info.monitor_name.is_empty() { "(none)" } else { &info.monitor_name });
            println!("  Monitor Serial: {}", if info.monitor_serial.is_empty() { "(none)" } else { &info.monitor_serial });
        }
        Err(e) => eprintln!("Parse error: {}", e),
    }
}
```

### 2. DDC/CI Controller in Rust (with Trait Abstraction)

```rust
// ddc_ci/src/lib.rs — DDC/CI abstraction layer

use std::time::Duration;
use std::thread;

pub const VCP_BRIGHTNESS:   u8 = 0x10;
pub const VCP_CONTRAST:     u8 = 0x12;
pub const VCP_COLOR_TEMP:   u8 = 0x0B;
pub const VCP_INPUT_SOURCE: u8 = 0x60;
pub const VCP_POWER_MODE:   u8 = 0xD6;

const DDC_CI_DEST:  u8 = 0x6E;
const DDC_CI_SRC:   u8 = 0x51;
const OPCODE_GET:   u8 = 0x01;
const OPCODE_SET:   u8 = 0x03;
const OPCODE_REPLY: u8 = 0x02;

#[derive(Debug, Clone, Copy)]
pub struct VcpValue {
    pub current: u16,
    pub maximum: u16,
}

impl VcpValue {
    pub fn percent(&self) -> f32 {
        if self.maximum == 0 { return 0.0; }
        (self.current as f32 / self.maximum as f32) * 100.0
    }
}

#[derive(Debug)]
pub enum DdcError {
    I2cWriteError(String),
    I2cReadError(String),
    InvalidReply,
    UnsupportedFeature(u8),
}

impl std::fmt::Display for DdcError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            DdcError::I2cWriteError(s)       => write!(f, "I2C write error: {}", s),
            DdcError::I2cReadError(s)        => write!(f, "I2C read error: {}", s),
            DdcError::InvalidReply           => write!(f, "Invalid DDC/CI reply"),
            DdcError::UnsupportedFeature(c)  => write!(f, "Unsupported VCP code: 0x{:02X}", c),
        }
    }
}

pub trait I2cBus {
    fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), String>;
    fn read(&mut self, addr: u8, buf: &mut [u8]) -> Result<(), String>;
}

pub struct DdcCi<B: I2cBus> {
    bus: B,
}

impl<B: I2cBus> DdcCi<B> {
    pub fn new(bus: B) -> Self {
        Self { bus }
    }

    fn ddc_checksum(bytes: &[u8]) -> u8 {
        bytes.iter().fold(0u8, |acc, &b| acc ^ b)
    }

    pub fn get_vcp(&mut self, code: u8) -> Result<VcpValue, DdcError> {
        // Packet: [DDC_CI_DEST, 0x82, DDC_CI_SRC, OPCODE_GET, code, checksum]
        let payload = [DDC_CI_DEST, 0x82, DDC_CI_SRC, OPCODE_GET, code];
        let chk     = Self::ddc_checksum(&payload);
        let packet  = [payload[0], payload[1], payload[2],
                       payload[3], payload[4], chk];

        self.bus.write(0x37, &packet)
            .map_err(DdcError::I2cWriteError)?;

        // Mandatory delay per MCCS spec (≥ 40 ms)
        thread::sleep(Duration::from_millis(50));

        let mut reply = [0u8; 12];
        self.bus.read(0x37, &mut reply)
            .map_err(DdcError::I2cReadError)?;

        if reply[3] != OPCODE_REPLY {
            return Err(DdcError::InvalidReply);
        }
        if reply[4] != 0 {
            return Err(DdcError::UnsupportedFeature(code));
        }

        Ok(VcpValue {
            maximum: (reply[6] as u16) << 8 | reply[7] as u16,
            current: (reply[8] as u16) << 8 | reply[9] as u16,
        })
    }

    pub fn set_vcp(&mut self, code: u8, value: u16) -> Result<(), DdcError> {
        // Packet: [DDC_CI_DEST, 0x84, DDC_CI_SRC, OPCODE_SET, code, vh, vl, checksum]
        let payload = [
            DDC_CI_DEST, 0x84, DDC_CI_SRC, OPCODE_SET,
            code,
            (value >> 8) as u8,
            (value & 0xFF) as u8,
        ];
        let chk = Self::ddc_checksum(&payload);

        let mut packet = [0u8; 8];
        packet[..7].copy_from_slice(&payload);
        packet[7] = chk;

        self.bus.write(0x37, &packet)
            .map_err(DdcError::I2cWriteError)?;

        thread::sleep(Duration::from_millis(50));
        Ok(())
    }

    /// Set brightness as a percentage (0–100)
    pub fn set_brightness_percent(&mut self, pct: u8) -> Result<(), DdcError> {
        let vcp = self.get_vcp(VCP_BRIGHTNESS)?;
        let target = ((pct as u32 * vcp.maximum as u32) / 100) as u16;
        self.set_vcp(VCP_BRIGHTNESS, target)
    }

    /// Convenience: print all common VCP values
    pub fn print_status(&mut self) {
        for (name, code) in &[
            ("Brightness",   VCP_BRIGHTNESS),
            ("Contrast",     VCP_CONTRAST),
            ("Input Source", VCP_INPUT_SOURCE),
        ] {
            match self.get_vcp(*code) {
                Ok(v) => println!("  {:14} : {:4} / {:4}  ({:.0}%)",
                                  name, v.current, v.maximum, v.percent()),
                Err(e) => println!("  {:14} : Error — {}", name, e),
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────
// Linux /dev/i2c-N backend
// ──────────────────────────────────────────────────────────────

#[cfg(target_os = "linux")]
pub mod linux {
    use super::I2cBus;
    use std::fs::OpenOptions;
    use std::os::unix::io::AsRawFd;

    const I2C_RDWR: u64 = 0x0707;
    const I2C_M_RD: u16 = 0x0001;

    #[repr(C)]
    struct I2cMsg { addr: u16, flags: u16, len: u16, buf: *mut u8 }
    #[repr(C)]
    struct I2cRdwrIoctlData { msgs: *mut I2cMsg, nmsgs: u32 }

    extern "C" {
        fn ioctl(fd: i32, req: u64, ...) -> i32;
    }

    pub struct LinuxI2cBus {
        fd: i32,
        _file: std::fs::File,
    }

    impl LinuxI2cBus {
        pub fn open(path: &str) -> Result<Self, String> {
            let file = OpenOptions::new().read(true).write(true).open(path)
                .map_err(|e| format!("open {}: {}", path, e))?;
            let fd = file.as_raw_fd();
            Ok(Self { fd, _file: file })
        }
    }

    impl I2cBus for LinuxI2cBus {
        fn write(&mut self, addr: u8, data: &[u8]) -> Result<(), String> {
            let mut buf = data.to_vec();
            let mut msg = I2cMsg {
                addr: addr as u16, flags: 0,
                len: buf.len() as u16, buf: buf.as_mut_ptr(),
            };
            let mut ioctl_data = I2cRdwrIoctlData { msgs: &mut msg, nmsgs: 1 };
            let ret = unsafe { ioctl(self.fd, I2C_RDWR, &mut ioctl_data as *mut _ as u64) };
            if ret < 0 { return Err(format!("write ioctl error")); }
            Ok(())
        }

        fn read(&mut self, addr: u8, buf: &mut [u8]) -> Result<(), String> {
            let mut msg = I2cMsg {
                addr: addr as u16, flags: I2C_M_RD,
                len: buf.len() as u16, buf: buf.as_mut_ptr(),
            };
            let mut ioctl_data = I2cRdwrIoctlData { msgs: &mut msg, nmsgs: 1 };
            let ret = unsafe { ioctl(self.fd, I2C_RDWR, &mut ioctl_data as *mut _ as u64) };
            if ret < 0 { return Err(format!("read ioctl error")); }
            Ok(())
        }
    }
}

// ──────────────────────────────────────────────────────────────
// main.rs
// ──────────────────────────────────────────────────────────────
// use ddc_ci::{DdcCi, VCP_BRIGHTNESS};
// use ddc_ci::linux::LinuxI2cBus;
//
// fn main() {
//     let bus = LinuxI2cBus::open("/dev/i2c-5").unwrap();
//     let mut ddc = DdcCi::new(bus);
//
//     println!("Monitor Status:");
//     ddc.print_status();
//
//     ddc.set_brightness_percent(60).unwrap();
//     println!("Brightness set to 60%");
// }
```

### 3. EDID Checksum & CRC Verification Utility in Rust

```rust
// edid_verify/src/main.rs — Standalone EDID file verifier

use std::path::Path;

fn verify_edid_file(path: &Path) -> Result<(), String> {
    let raw = std::fs::read(path)
        .map_err(|e| format!("Cannot read {}: {}", path.display(), e))?;

    if raw.len() < 128 {
        return Err(format!("File too short: {} bytes", raw.len()));
    }

    let header: [u8; 8] = [0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00];
    if raw[..8] != header {
        return Err("Invalid EDID header".to_string());
    }

    // Verify each 128-byte block
    let num_blocks = raw.len() / 128;
    for blk in 0..num_blocks {
        let block = &raw[blk * 128..(blk + 1) * 128];
        let checksum = block.iter().fold(0u8, |acc, &b| acc.wrapping_add(b));
        if checksum != 0 {
            return Err(format!("Block {} checksum fail (sum=0x{:02X})", blk, checksum));
        }
        println!("Block {}: checksum OK", blk);

        if blk > 0 {
            match block[0] {
                0x02 => println!("  → CEA-861 Extension (HDMI/AV)"),
                0x40 => println!("  → DisplayID Extension"),
                0x70 => println!("  → DisplayID 2.0 Extension"),
                0xF0 => println!("  → Block Map"),
                tag  => println!("  → Unknown extension tag 0x{:02X}", tag),
            }
        }
    }

    println!("\nAll {} block(s) valid.", num_blocks);
    Ok(())
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.len() < 2 {
        eprintln!("Usage: edid_verify <edid_binary_file>");
        eprintln!("  Example: edid_verify /sys/class/drm/card0-HDMI-A-1/edid");
        std::process::exit(1);
    }
    match verify_edid_file(Path::new(&args[1])) {
        Ok(())   => {},
        Err(e)   => { eprintln!("Error: {}", e); std::process::exit(1); }
    }
}
```

---

## Advanced Topics

### EDID Override / Spoofing

Linux allows injecting custom EDID via firmware or sysfs when a monitor reports incorrect capabilities. This is commonly used with damaged EEPROMs, KVM switches, or HDMI dummies.

```bash
# Load custom EDID from file at boot (add to /etc/modprobe.d/drm.conf):
options drm_kms_helper edid_firmware=HDMI-A-1:/lib/firmware/custom_edid.bin

# Or at runtime via kernel debugfs (requires root):
echo /lib/firmware/custom_edid.bin > \
  /sys/kernel/debug/dri/0/HDMI-A-1/edid_override
```

### Multi-Monitor DDC via DisplayPort MST

With DisplayPort Multi-Stream Transport (MST), each monitor in a daisy chain has its own I2C segment. The DP AUX channel handles segmenting, and the DPCD `MSTM_CTRL` register must be configured before DDC access.

```c
// DPCD register addresses for MST DDC tunneling
#define DPCD_MSTM_CTRL           0x00111
#define DPCD_REMOTE_I2C_READ     0x00E0  // SIDEBAND_MSG
```

### MCCS Capability String

Beyond individual VCP gets/sets, monitors expose a capability string via DDC/CI opcode `0xF3` (Get Capabilities Reply). This ASCII string follows a LISP-like syntax:

```
(prot(Monitor)type(LCD)model(DELL U2720Q)
 cmds(01 02 03 07 0C E3 F3)
 vcp(10 12 60(01 03 04) D6(01 04 05) DF)
 mswhql(1) asset_eep(40) mccs_ver(2.1))
```

Parsing this allows discovering exactly which VCP codes and input sources a monitor supports without trial-and-error.

### Thread Safety & Locking

DDC/CI is not thread-safe. Multiple simultaneous transactions corrupt the state machine. On Linux, always serialize DDC access using a mutex or open `/dev/i2c-N` with O_RDWR and hold it exclusively per session.

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---|---|---|
| `I2C_RDWR` returns `-ENODEV` | Wrong bus number | List with `i2cdetect -l`; try buses 3–9 |
| EDID reads all zeros | GPU driver not exposing DDC | Load `i2c-dev` module: `modprobe i2c-dev` |
| Checksum fails on valid monitor | KVM/cable I2C distortion | Reduce clock speed; add pull-ups (4.7 kΩ) |
| DDC/CI get returns `result=1` | Feature unsupported by monitor | Check capability string first |
| Reply bytes are garbage | Missing 40 ms delay | Add `usleep(50000)` between write and read |
| HDMI dummy plug no EDID | No EEPROM present | Load firmware EDID or use edid_override |
| Intermittent read errors | Noisy long cable | Use shorter cable; add 100 nF decoupling |

---

## Summary

Display interfaces via DDC/EDID represent one of the most widely deployed uses of I2C in consumer electronics. Every monitor with a digital or analog video connector carries an I2C bus used for two distinct purposes:

**EDID** provides a static, read-only description of the display's capabilities — its supported resolutions, timing parameters, color gamut, and physical attributes — stored in a 128-to-256-byte EEPROM at I2C address `0x50`. The host reads this on hotplug detection to configure the graphics pipeline automatically. Parsing EDID requires understanding its binary layout: the fixed 8-byte header, packed manufacturer ID encoding, four 18-byte descriptor blocks, and optional CEA-861 extension blocks for HDMI audio/video metadata.

**DDC/CI** extends the same bus (address `0x37`) into a bidirectional control channel, allowing software to read and write VCP (Virtual Control Panel) feature codes — brightness, contrast, input source, power state, and more. Each transaction follows a specific framing with length fields, source/destination addresses, and an XOR checksum, with a mandatory 40 ms minimum delay between write and read.

In **C/C++**, access is straightforward via Linux's `/dev/i2c-N` device nodes and the `I2C_RDWR` ioctl, which supports combined write+read transactions in a single system call — essential for proper I2C register-addressed reads. In **Rust**, the same ioctl can be called through unsafe FFI, but the I2C bus can be abstracted behind a trait (`I2cBus`) to allow clean testing with mock backends and easy porting across platforms. Both languages benefit from proper checksum validation, structured descriptor parsing, and careful timing compliance to ensure reliable operation across the wide variety of monitor implementations in the field.

Key practical considerations include: using `modprobe i2c-dev` to expose the DDC bus on Linux, applying the mandatory inter-message delay for DDC/CI, serializing access to prevent corruption from concurrent transactions, and always querying the monitor's capability string before assuming VCP feature support.

---

*Part of the I2C Programming Reference Series — Topic 91*