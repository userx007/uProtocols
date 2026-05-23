# 26. Configuration Manager & Automatic Node Configuration

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals & Architecture](#fundamentals--architecture)
3. [The Identity Object (0x1018)](#the-identity-object-0x1018)
4. [Device Configuration Files (DCF)](#device-configuration-files-dcf)
5. [DCF-Based Configuration Download](#dcf-based-configuration-download)
6. [Verify-Before-Write Strategy](#verify-before-write-strategy)
7. [Version Checking via Identity Object](#version-checking-via-identity-object)
8. [Incremental Configuration](#incremental-configuration)
9. [Error Recovery During Network Startup](#error-recovery-during-network-startup)
10. [Complete Implementation Example](#complete-implementation-example)
11. [Summary](#summary)

---

## Introduction

In a CANopen network, nodes must be properly configured before they can participate
in productive communication. This configuration encompasses PDO mappings, communication
parameters, heartbeat timers, and application-specific settings. Doing this manually
for each node — especially in large networks — is error-prone and time-consuming.

The **Configuration Manager** (CM) is a standardized CANopen master-side mechanism
that automates node configuration during network startup. It uses **Device Configuration
Files (DCF)** to describe the desired configuration of each node and downloads those
settings automatically using SDO transfers.

Key capabilities:

- **DCF-based configuration download** — reads node config from `.dcf` files and writes
  all required object dictionary entries via SDO
- **Verify-before-write strategy** — reads current values and skips writes if already
  correct, reducing bus load and wear on non-volatile memory
- **Version checking via identity object** — compares firmware/hardware revision in
  object 0x1018 to detect mismatches before applying config
- **Incremental configuration** — only updates parameters that changed since the last
  known-good configuration
- **Error recovery** — handles SDO errors, timeouts, and unreachable nodes gracefully
  during startup

---

## Fundamentals & Architecture

### Network Startup Sequence

```
  Power-On / Reset
       |
       v
  +--------------------+
  |  NMT Master sends  |
  |  Reset_All or      |
  |  Boot command      |
  +--------------------+
       |
       v
  Nodes enter INITIALISATION
  then automatically go to
  PRE-OPERATIONAL
       |
       v
  +-----------------------------+
  |  Configuration Manager      |
  |  detects nodes via          |
  |  Boot-Up message (0x700+ID) |
  +-----------------------------+
       |
       v
  For each detected node:
  +-----------------------------+
  |  1. Read Identity (0x1018)  |
  |  2. Check version           |
  |  3. Load DCF for node       |
  |  4. Verify / Download       |
  |  5. Send NMT Start          |
  +-----------------------------+
       |
       v
  All nodes in OPERATIONAL
```

### Configuration Manager State Machine

```
  +-------------+
  |   IDLE      |<---------------------------------+
  +-------------+                                  |
       |                                           |
       | Boot-Up received from node N              |
       v                                           |
  +------------------+                             |
  |  READ_IDENTITY   | Read 0x1018 sub1..sub4      |
  +------------------+                             |
       |                                           |
       | Identity OK / DCF found                   |
       v                                           |
  +------------------+                             |
  |  VERIFY_PARAMS   | Read current OD values      |
  +------------------+                             |
       |                                           |
       | Differences found                         |
       v                                           |
  +------------------+                             |
  |  DOWNLOAD_PARAMS | Write via SDO               |
  +------------------+                             |
       |                                           |
       | All params written                        |
       v                                           |
  +------------------+                             |
  |  START_NODE      | NMT Start_Remote_Node       |
  +------------------+                             |
       |                                           |
       | Done ----------------------------------->-+
       |
       | Error at any stage
       v
  +------------------+
  |  ERROR_RECOVERY  |
  +------------------+
       |
       | Retry / Skip / Abort
       v
  Back to IDLE or FATAL_ERROR
```

---

## The Identity Object (0x1018)

Every CANopen device must implement the **Identity Object** at index `0x1018`. It
provides a standardized way to identify device type, vendor, product, revision, and
serial number.

### Object 0x1018 Structure

```
  Index 0x1018 — Identity Object
  ================================
  Sub 0x00  Highest Sub-Index         (UINT8,  RO)
  Sub 0x01  Vendor ID                 (UINT32, RO)  — assigned by CiA
  Sub 0x02  Product Code              (UINT32, RO)  — manufacturer-specific
  Sub 0x03  Revision Number           (UINT32, RO)  — hi:major / lo:minor
  Sub 0x04  Serial Number             (UINT32, RO)  — device instance ID

  Revision Number bit layout:
  +--------------------------------+--------------------------------+
  | Bits 31..16  Major Revision    | Bits 15..0   Minor Revision   |
  +--------------------------------+--------------------------------+

  Major: incremented on incompatible firmware changes (config must be re-verified)
  Minor: incremented on backward-compatible changes (config may remain valid)
```

### Reading Identity via SDO in C

```c
#include <stdint.h>
#include <string.h>

/* CANopen SDO expedited upload (simplified interface) */
int sdo_read(uint8_t node_id, uint16_t index, uint8_t subindex,
             void *data, uint8_t *size);

typedef struct {
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision;      /* hi16 = major, lo16 = minor */
    uint32_t serial_number;
} co_identity_t;

int co_read_identity(uint8_t node_id, co_identity_t *id)
{
    uint8_t sz;
    int ret;

    ret = sdo_read(node_id, 0x1018, 0x01, &id->vendor_id,     &sz); if (ret) return ret;
    ret = sdo_read(node_id, 0x1018, 0x02, &id->product_code,  &sz); if (ret) return ret;
    ret = sdo_read(node_id, 0x1018, 0x03, &id->revision,      &sz); if (ret) return ret;
    ret = sdo_read(node_id, 0x1018, 0x04, &id->serial_number, &sz); if (ret) return ret;

    return 0;
}

/* Decode revision helpers */
static inline uint16_t co_revision_major(uint32_t rev) { return (uint16_t)(rev >> 16); }
static inline uint16_t co_revision_minor(uint32_t rev) { return (uint16_t)(rev & 0xFFFF); }
```

---

## Device Configuration Files (DCF)

A **DCF** (Device Configuration File) is an INI-format text file derived from the
EDS (Electronic Data Sheet) format. It describes the complete desired configuration
of a specific node, including all object dictionary entries that need to be set.

### DCF File Structure

```
  [DeviceInfo]
  NodeId=3
  BaudRate=250

  [Comments]
  Lines=1
  Line1=Auto-generated by CANopen Configurator v2.1

  [DummyUsage]
  Dummy0001=0
  ...

  [IndexList]         <-- Which objects are configured
  Entries=4
  1=1400              <-- Index in hex (PDO comm param)
  2=1600              <-- PDO mapping
  3=6040              <-- Controlword
  4=6041              <-- Statusword

  [1400]              <-- One section per index
  ParameterName=Receive PDO Communication Parameter 1
  ObjectType=0x9
  SubNumber=3

  [1400sub0]
  ParameterName=Highest Sub-Index
  ObjectType=0x7
  DataType=0x0005
  AccessType=ro
  DefaultValue=2
  PDOMapping=0

  [1400sub1]
  ParameterName=COB-ID use by RPDO1
  ObjectType=0x7
  DataType=0x0007
  AccessType=rw
  DefaultValue=0x00000203    <-- COB-ID = 0x200 + Node-ID (3)
  PDOMapping=0

  [1400sub2]
  ParameterName=Transmission Type
  ObjectType=0x7
  DataType=0x0005
  AccessType=rw
  DefaultValue=254           <-- Event-driven (asynchronous)
  PDOMapping=0
```

### DCF Parser in C++

```cpp
#include <map>
#include <string>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cstdint>

struct DcfEntry {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  data_type;   /* 0x04=INT8, 0x05=UINT8, 0x06=INT16, 0x07=UINT32, ... */
    uint32_t value;
    bool     read_only;
};

class DcfParser {
public:
    std::vector<DcfEntry> entries;
    uint8_t  node_id   = 0;
    uint32_t baud_rate = 250;

    bool load(const std::string &path) {
        std::ifstream f(path);
        if (!f.is_open()) return false;

        std::map<std::string, std::map<std::string, std::string>> ini;
        std::string section, line;

        while (std::getline(f, line)) {
            trim(line);
            if (line.empty() || line[0] == ';') continue;
            if (line[0] == '[') {
                section = line.substr(1, line.find(']') - 1);
            } else {
                auto eq = line.find('=');
                if (eq != std::string::npos)
                    ini[section][to_lower(line.substr(0, eq))] =
                                                line.substr(eq + 1);
            }
        }

        /* Parse device info */
        node_id   = (uint8_t)std::stoi(ini["DeviceInfo"]["nodeid"]);
        baud_rate = (uint32_t)std::stoi(ini["DeviceInfo"]["baudrate"]);

        /* Parse index list */
        int count = std::stoi(ini["IndexList"]["entries"]);
        for (int i = 1; i <= count; ++i) {
            uint16_t idx = (uint16_t)std::stoul(
                ini["IndexList"][std::to_string(i)], nullptr, 16);

            auto &sec = ini[to_hex(idx)];
            int subs = std::stoi(sec.count("subnumber") ?
                                 sec["subnumber"] : "1");

            for (int s = 0; s < subs; ++s) {
                std::string subkey = to_hex(idx) + "sub" + std::to_string(s);
                auto &ss = ini[subkey];
                if (ss.empty()) continue;
                if (ss["accesstype"] == "ro") continue;

                DcfEntry e;
                e.index    = idx;
                e.subindex = (uint8_t)s;
                e.data_type = (uint8_t)std::stoul(ss["datatype"], nullptr, 16);
                e.value    = (uint32_t)std::stoul(ss["defaultvalue"], nullptr, 0);
                e.read_only = false;
                entries.push_back(e);
            }
        }
        return true;
    }

private:
    static void trim(std::string &s) {
        s.erase(0, s.find_first_not_of(" \t\r\n"));
        s.erase(s.find_last_not_of(" \t\r\n") + 1);
    }
    static std::string to_lower(std::string s) {
        for (auto &c : s) c = tolower(c);
        return s;
    }
    static std::string to_hex(uint16_t v) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%04X", v);
        return std::string(buf);
    }
};
```

---

## DCF-Based Configuration Download

The core operation of the Configuration Manager is writing all DCF entries to the
target node using **SDO transfers**. Each entry is addressed by `(index, subindex)`
and the value is written using expedited SDO download for entries ≤ 4 bytes, or
segmented/block SDO for larger data.

### SDO Download State Machine (Single Entry)

```
  START
    |
    v
  Send SDO Download Request
  (CAN frame: 0x600 + node_id)
    |
    v
  Wait for response
  (CAN frame: 0x580 + node_id)     Timeout?
    |                                  |
    | Response received                v
    v                           RETRY or ABORT
  Check response byte:
    |
    +--- 0x60 = Success -------> DONE (OK)
    |
    +--- 0x80 = Abort ---------> Read abort code
    |                             |
    |                             +-- 0x06090011 = subindex not found
    |                             +-- 0x06010002 = read-only object
    |                             +-- 0x08000022 = device in wrong state
    |                             v
    |                           Handle per policy
    v
  ERROR
```

### SDO Download Implementation in C

```c
#include <stdint.h>
#include <string.h>

#define SDO_TIMEOUT_MS      500
#define SDO_MAX_RETRIES     3

/* CANopen abort codes */
#define CO_SDO_ABORT_READONLY     0x06010002UL
#define CO_SDO_ABORT_NO_OBJECT    0x06020000UL
#define CO_SDO_ABORT_NO_SUBINDEX  0x06090011UL
#define CO_SDO_ABORT_HW_ERROR     0x06060000UL

typedef struct {
    uint32_t abort_code;
    int      retries_left;
    bool     skip_on_readonly;
    bool     abort_on_hw_error;
} sdo_policy_t;

/* Platform-specific CAN send/receive — implement per BSP */
int  can_send(uint32_t cob_id, uint8_t *data, uint8_t len);
int  can_receive(uint32_t cob_id, uint8_t *data, uint8_t *len, uint32_t timeout_ms);

int co_sdo_write(uint8_t node_id, uint16_t index, uint8_t subindex,
                 uint32_t value, uint8_t size_bytes, const sdo_policy_t *pol)
{
    uint8_t tx[8] = {0};
    uint8_t rx[8];
    uint8_t rx_len;
    int retries = pol ? pol->retries_left : SDO_MAX_RETRIES;

    /* Build expedited download request (size ≤ 4 bytes) */
    /* Command specifier: 0x23 | ((4 - size) << 2) */
    tx[0] = 0x23U | (uint8_t)((4U - size_bytes) << 2U);
    tx[1] = (uint8_t)(index & 0xFF);
    tx[2] = (uint8_t)(index >> 8);
    tx[3] = subindex;
    memcpy(&tx[4], &value, size_bytes);  /* little-endian */

    while (retries-- > 0) {
        if (can_send(0x600U + node_id, tx, 8) < 0)
            continue;

        if (can_receive(0x580U + node_id, rx, &rx_len, SDO_TIMEOUT_MS) < 0)
            continue; /* timeout — retry */

        if (rx[0] == 0x60U) {
            return 0; /* Success */
        }

        if (rx[0] == 0x80U) {
            uint32_t abort = (uint32_t)rx[4]
                           | ((uint32_t)rx[5] << 8)
                           | ((uint32_t)rx[6] << 16)
                           | ((uint32_t)rx[7] << 24);

            if (pol && pol->skip_on_readonly &&
                abort == CO_SDO_ABORT_READONLY) {
                return 0; /* silently skip read-only objects */
            }
            return (int)(-abort); /* propagate abort code */
        }
    }
    return -1; /* exhausted retries */
}

/* Perform full DCF download to a node */
int co_download_dcf(uint8_t node_id, const DcfEntry *entries, int count)
{
    sdo_policy_t pol = {
        .retries_left    = SDO_MAX_RETRIES,
        .skip_on_readonly = true,
        .abort_on_hw_error = false,
    };

    for (int i = 0; i < count; ++i) {
        const DcfEntry *e = &entries[i];
        uint8_t sz;

        switch (e->data_type) {
            case 0x01: case 0x04: case 0x05: sz = 1; break;  /* BOOL,INT8,UINT8 */
            case 0x06: case 0x07: sz = 2; break;              /* INT16,UINT16 */
            default:               sz = 4; break;              /* INT32,UINT32,... */
        }

        int ret = co_sdo_write(node_id, e->index, e->subindex,
                               e->value, sz, &pol);
        if (ret < 0) {
            /* Log error, decide: skip entry or abort node config? */
            printf("[CM] Node %d: SDO write 0x%04X/0x%02X failed: %d\n",
                   node_id, e->index, e->subindex, ret);
            /* policy: continue for now */
        }
    }
    return 0;
}
```

---

## Verify-Before-Write Strategy

Writing every DCF entry unconditionally every startup causes unnecessary bus traffic
and wears out EEPROM/Flash in devices with non-volatile parameter storage. The
**verify-before-write** strategy first reads the current value and only writes if
it differs from the desired DCF value.

### Verify-Before-Write Decision Flow

```
  For each DCF entry (index, subindex, desired_value):

  +----------------------------+
  |  SDO Read current value    |
  +----------------------------+
         |
         | Read success?
     YES |                   NO (error / not implemented)
         |                       |
         v                       v
  current == desired?      Log warning, WRITE anyway
     YES |         NO           |
         |          |           |
         v          v           |
     SKIP        WRITE ---------+
     (no bus       |
      traffic)     v
                 Verify write?
                 (optional re-read)
                   |
                   v
                 Confirm or flag mismatch
```

### Verify-Before-Write in C++

```cpp
#include <cstdio>
#include <cstdint>

class ConfigManager {
public:
    struct WriteStats {
        int total     = 0;
        int skipped   = 0;
        int written   = 0;
        int errors    = 0;
    };

    WriteStats verify_and_download(uint8_t node_id,
                                   const std::vector<DcfEntry> &entries,
                                   bool post_verify = false)
    {
        WriteStats stats;
        sdo_policy_t pol{SDO_MAX_RETRIES, true, false, 0};

        for (const auto &e : entries) {
            ++stats.total;
            uint32_t current = 0;
            uint8_t  sz      = data_type_size(e.data_type);

            /* Step 1: Read current value */
            if (sdo_read(node_id, e.index, e.subindex, &current, sz) == 0) {
                if (current == e.value) {
                    ++stats.skipped;
                    continue;  /* Already correct — skip write */
                }
            }
            /* else: read failed — proceed with write anyway */

            /* Step 2: Write desired value */
            int ret = co_sdo_write(node_id, e.index, e.subindex,
                                   e.value, sz, &pol);
            if (ret != 0) {
                ++stats.errors;
                printf("[CM] Write failed 0x%04X/0x%02X node=%d ret=%d\n",
                       e.index, e.subindex, node_id, ret);
                continue;
            }
            ++stats.written;

            /* Step 3 (optional): Post-write verification */
            if (post_verify) {
                uint32_t readback = 0;
                if (sdo_read(node_id, e.index, e.subindex, &readback, sz) == 0) {
                    if (readback != e.value) {
                        printf("[CM] VERIFY FAIL 0x%04X/0x%02X: "
                               "wrote 0x%08X, read back 0x%08X\n",
                               e.index, e.subindex, e.value, readback);
                        ++stats.errors;
                    }
                }
            }
        }
        return stats;
    }

private:
    static uint8_t data_type_size(uint8_t dt) {
        switch (dt) {
            case 0x01: case 0x04: case 0x05: return 1;
            case 0x06: case 0x07:            return 2;
            default:                         return 4;
        }
    }

    /* Declare SDO helpers — implemented elsewhere */
    static int sdo_read(uint8_t node_id, uint16_t index, uint8_t sub,
                        uint32_t *val, uint8_t sz);
};
```

---

## Version Checking via Identity Object

Before applying any configuration, the CM reads `0x1018` and compares the device's
**major revision** against the version stored in the DCF or a local version database.
A major revision mismatch means the firmware layout may have changed — applying an
old DCF blindly could corrupt the device or produce incorrect behavior.

### Version Check Strategy

```
  DCF specifies:
    vendor_id    = 0x00000315   (CiA member ID)
    product_code = 0x00000042
    revision     = 0x00010003   (major=1, minor=3)

  Device reports (via 0x1018):
    vendor_id    = 0x00000315
    product_code = 0x00000042
    revision     = 0x00010005   (major=1, minor=5)

  Decision Matrix:
  +----------------+----------------+---------+---------------------------+
  | Vendor match?  | Product match? | Major   | Action                    |
  +----------------+----------------+---------+---------------------------+
  | NO             | —              | —       | ABORT: wrong device       |
  | YES            | NO             | —       | ABORT: wrong product      |
  | YES            | YES            | DIFFERS | ABORT or WARN: FW changed |
  | YES            | YES            | SAME    | MINOR diff: OK, continue  |
  +----------------+----------------+---------+---------------------------+
```

### Version Checking in C

```c
typedef struct {
    uint32_t vendor_id;
    uint32_t product_code;
    uint16_t required_major;  /* 0 = don't check major */
    uint16_t min_minor;       /* 0 = don't check minor */
} version_requirement_t;

typedef enum {
    VER_OK            = 0,
    VER_WRONG_VENDOR  = -1,
    VER_WRONG_PRODUCT = -2,
    VER_MAJOR_DIFFERS = -3,
    VER_MINOR_TOO_OLD = -4,
    VER_READ_ERROR    = -5,
} ver_check_result_t;

ver_check_result_t co_check_version(uint8_t node_id,
                                    const version_requirement_t *req)
{
    co_identity_t id;
    if (co_read_identity(node_id, &id) != 0)
        return VER_READ_ERROR;

    if (req->vendor_id && id.vendor_id != req->vendor_id) {
        printf("[CM] Node %d: vendor mismatch: got 0x%08X, expected 0x%08X\n",
               node_id, id.vendor_id, req->vendor_id);
        return VER_WRONG_VENDOR;
    }

    if (req->product_code && id.product_code != req->product_code) {
        printf("[CM] Node %d: product mismatch: got 0x%08X, expected 0x%08X\n",
               node_id, id.product_code, req->product_code);
        return VER_WRONG_PRODUCT;
    }

    uint16_t actual_major = co_revision_major(id.revision);
    uint16_t actual_minor = co_revision_minor(id.revision);

    if (req->required_major && actual_major != req->required_major) {
        printf("[CM] Node %d: major revision mismatch: "
               "got %u, expected %u — config may be incompatible\n",
               node_id, actual_major, req->required_major);
        return VER_MAJOR_DIFFERS;
    }

    if (req->min_minor && actual_minor < req->min_minor) {
        printf("[CM] Node %d: firmware too old: minor=%u, need>=%u\n",
               node_id, actual_minor, req->min_minor);
        return VER_MINOR_TOO_OLD;
    }

    return VER_OK;
}
```

---

## Incremental Configuration

In a running system, it is wasteful and risky to download an entire DCF every time a
node reboots. **Incremental configuration** tracks which entries have changed since
the last successful configuration and only downloads the delta.

### Incremental Config Tracking

```
  Configuration Database (stored in CM flash / file):
  +--------+--------+----------+--------------------+-----------+
  | NodeID | Index  | Subindex | Last Written Value  | CRC/Hash  |
  +--------+--------+----------+--------------------+-----------+
  |   3    | 0x1400 |  0x01    | 0x00000203          | 0xA1B2    |
  |   3    | 0x1400 |  0x02    | 0x000000FE          | 0xC3D4    |
  |   3    | 0x1600 |  0x01    | 0x60400108          | 0xE5F6    |
  +--------+--------+----------+--------------------+-----------+

  On next startup:
    Load new DCF  -->  Compare with DB  -->  Only download CHANGED entries
```

### Incremental Configuration in C++

```cpp
#include <unordered_map>
#include <cstdint>
#include <vector>
#include <fstream>

struct CfgKey {
    uint8_t  node_id;
    uint16_t index;
    uint8_t  subindex;

    bool operator==(const CfgKey &o) const {
        return node_id == o.node_id &&
               index   == o.index   &&
               subindex == o.subindex;
    }
};

struct CfgKeyHash {
    std::size_t operator()(const CfgKey &k) const {
        return std::hash<uint32_t>()(
            ((uint32_t)k.node_id << 24) |
            ((uint32_t)k.index   <<  8) |
             (uint32_t)k.subindex);
    }
};

class IncrementalConfigManager {
    using ConfigDB = std::unordered_map<CfgKey, uint32_t, CfgKeyHash>;

    ConfigDB last_written_;   /* persisted between resets */
    std::string db_path_;

public:
    explicit IncrementalConfigManager(const std::string &db_path)
        : db_path_(db_path) { load_db(); }

    /* Returns only entries that differ from last-written state */
    std::vector<DcfEntry> compute_delta(uint8_t node_id,
                                        const std::vector<DcfEntry> &dcf)
    {
        std::vector<DcfEntry> delta;
        for (const auto &e : dcf) {
            CfgKey key{node_id, e.index, e.subindex};
            auto it = last_written_.find(key);
            if (it == last_written_.end() || it->second != e.value) {
                delta.push_back(e);
            }
        }
        return delta;
    }

    /* Call after successful write of an entry */
    void mark_written(uint8_t node_id, const DcfEntry &e) {
        CfgKey key{node_id, e.index, e.subindex};
        last_written_[key] = e.value;
    }

    /* Persist DB to file (call after successful node config) */
    void save_db() {
        std::ofstream f(db_path_, std::ios::binary | std::ios::trunc);
        for (const auto &kv : last_written_) {
            f.write((const char*)&kv.first,  sizeof(CfgKey));
            f.write((const char*)&kv.second, sizeof(uint32_t));
        }
    }

    /* Invalidate all entries for a node (e.g. after firmware update) */
    void invalidate_node(uint8_t node_id) {
        for (auto it = last_written_.begin(); it != last_written_.end(); ) {
            if (it->first.node_id == node_id)
                it = last_written_.erase(it);
            else
                ++it;
        }
    }

private:
    void load_db() {
        std::ifstream f(db_path_, std::ios::binary);
        if (!f.is_open()) return;
        CfgKey k; uint32_t v;
        while (f.read((char*)&k, sizeof(CfgKey)) &&
               f.read((char*)&v, sizeof(uint32_t))) {
            last_written_[k] = v;
        }
    }
};
```

---

## Error Recovery During Network Startup

Network startup is the most fragile phase: nodes may not answer SDO requests,
firmware may be in a bad state, or a node may be missing entirely. A robust CM must
handle all these scenarios without blocking the rest of the network.

### Error Categories and Responses

```
  Error Type              | Detection                | Response
  ========================+==========================+=======================
  Node missing / silent   | No Boot-Up within T_boot | Skip, mark absent
  SDO timeout             | No response within T_sdo  | Retry N times, skip
  SDO abort               | 0x80 response + code      | Per-code policy
  Wrong device identity   | Vendor/Product mismatch   | Skip, alert operator
  Incompatible firmware   | Major revision mismatch   | Skip or use fallback
  EEPROM write-protected  | Abort 0x06010002          | Skip (read-only OK)
  EEPROM full / HW error  | Abort 0x06060000          | Retry, then abort node
  Node in wrong NMT state | Abort 0x08000022          | Send Pre-Op, retry
```

### Error Recovery State Machine

```
  Startup: wait for Boot-Up from node N
       |
       | T_boot expires, no Boot-Up
       v
  +---------------------------+
  |  MISSING: log & continue  |
  |  Mark node as absent      |
  |  Do NOT start NMT         |
  +---------------------------+

  --- OR ---

  Boot-Up received, SDO write fails
       |
       v
  +---------------------------+
  |  Retry (up to MAX_RETRY)  |
  +---------------------------+
       |
       | Still failing
       v
  +-------------------------------+
  |  Try reset node via NMT:      |
  |  NMT Reset_Communication(N)   |
  +-------------------------------+
       |
       | Wait for Boot-Up again
       v
  +-------------------------------+
  |  Retry configuration once     |
  +-------------------------------+
       |
       | Fails again
       v
  +-------------------------------+
  |  GIVE UP: mark node as        |
  |  ERROR_CFG, start rest of     |
  |  network without this node    |
  +-------------------------------+
```

### Error Recovery Implementation in C

```c
#include <stdint.h>
#include <stdbool.h>

#define MAX_RETRIES        3
#define T_BOOT_MS       2000
#define T_SDO_MS         500
#define T_RESET_WAIT_MS 1500

typedef enum {
    NODE_STATE_UNKNOWN,
    NODE_STATE_OK,
    NODE_STATE_ABSENT,
    NODE_STATE_CFG_ERROR,
    NODE_STATE_WRONG_FW,
} node_cfg_state_t;

typedef struct {
    uint8_t          node_id;
    node_cfg_state_t state;
    uint32_t         error_code;
} node_status_t;

/* NMT command codes */
#define NMT_START_REMOTE     0x01
#define NMT_STOP_REMOTE      0x02
#define NMT_ENTER_PREOP      0x80
#define NMT_RESET_NODE       0x81
#define NMT_RESET_COMM       0x82

int  nmt_send(uint8_t cmd, uint8_t node_id);
bool wait_for_bootup(uint8_t node_id, uint32_t timeout_ms);

node_cfg_state_t co_configure_node_with_recovery(
    uint8_t node_id,
    const version_requirement_t *ver_req,
    const DcfEntry *entries,
    int entry_count)
{
    int attempt;

    /* Step 1: Verify identity */
    ver_check_result_t vr = co_check_version(node_id, ver_req);
    if (vr == VER_WRONG_VENDOR || vr == VER_WRONG_PRODUCT) {
        printf("[CM] Node %d identity mismatch — skipping\n", node_id);
        return NODE_STATE_WRONG_FW;
    }
    if (vr == VER_MAJOR_DIFFERS) {
        printf("[CM] Node %d major revision mismatch — skipping config\n", node_id);
        return NODE_STATE_WRONG_FW;
    }

    /* Step 2: Attempt configuration with retries + reset */
    for (attempt = 0; attempt < MAX_RETRIES; ++attempt) {
        bool all_ok = true;
        sdo_policy_t pol = {
            .retries_left    = 2,
            .skip_on_readonly = true,
        };

        for (int i = 0; i < entry_count; ++i) {
            const DcfEntry *e = &entries[i];
            uint8_t sz = (e->data_type <= 0x05) ? 1 :
                         (e->data_type <= 0x07) ? 2 : 4;

            int ret = co_sdo_write(node_id, e->index, e->subindex,
                                   e->value, sz, &pol);
            if (ret != 0) {
                uint32_t abort = (uint32_t)(-ret);
                if (abort == CO_SDO_ABORT_READONLY) continue; /* OK */
                if (abort == CO_SDO_ABORT_NO_OBJECT)  continue; /* OK */

                /* Device in wrong state — try to put back in Pre-Op */
                if (abort == 0x08000022UL) {
                    nmt_send(NMT_ENTER_PREOP, node_id);
                    os_sleep_ms(50);
                    /* retry this entry */
                    ret = co_sdo_write(node_id, e->index, e->subindex,
                                       e->value, sz, &pol);
                    if (ret == 0) continue;
                }

                all_ok = false;
                printf("[CM] Node %d: cfg entry 0x%04X/0x%02X failed, "
                       "abort=0x%08X\n",
                       node_id, e->index, e->subindex, abort);
            }
        }

        if (all_ok) {
            nmt_send(NMT_START_REMOTE, node_id);
            return NODE_STATE_OK;
        }

        /* Reset communication and retry */
        printf("[CM] Node %d: attempt %d failed, resetting communication\n",
               node_id, attempt + 1);
        nmt_send(NMT_RESET_COMM, node_id);
        if (!wait_for_bootup(node_id, T_RESET_WAIT_MS)) {
            printf("[CM] Node %d: did not re-boot after reset\n", node_id);
            return NODE_STATE_ABSENT;
        }
    }

    printf("[CM] Node %d: configuration permanently failed\n", node_id);
    return NODE_STATE_CFG_ERROR;
}
```

---

## Complete Implementation Example

The following brings all the pieces together into a realistic Configuration Manager
that initialises a CANopen network of multiple nodes.

### Full Network Startup Flow

```
  Main startup sequence (simplified):

  cm_init()
  |
  +-- Load all DCF files from /config/*.dcf
  +-- Build node_config_table[MAX_NODES]
  |
  co_network_start()
  |
  +-- NMT Reset_All
  +-- Wait T_boot for Boot-Up messages
  |
  For each Boot-Up received:
  |
  +-- co_configure_node_with_recovery()
      |
      +-- co_check_version()           (abort if wrong HW)
      |
      +-- compute_delta()              (incremental: only changed)
      |
      +-- verify_and_download()        (verify-before-write)
      |   +-- sdo_read() each entry
      |   +-- sdo_write() if differs
      |
      +-- nmt_send(START_REMOTE)
      |
      +-- mark_written() + save_db()   (update incremental DB)
  |
  All nodes processed:
  +-- Log summary: OK / CFG_ERROR / ABSENT / WRONG_FW
  +-- Report to application layer
```

### Complete C++ Configuration Manager Class

```cpp
#include <vector>
#include <map>
#include <string>
#include <chrono>
#include <thread>

/* Assume DcfParser, IncrementalConfigManager, co_check_version,
   verify_and_download, nmt_send, wait_for_bootup are defined above */

struct NodeConfig {
    uint8_t              node_id;
    version_requirement_t ver_req;
    std::vector<DcfEntry> dcf_entries;
};

class NetworkConfigManager {
    std::map<uint8_t, NodeConfig>  node_configs_;
    IncrementalConfigManager       incr_mgr_;
    std::map<uint8_t, node_cfg_state_t> node_states_;
    int total_ok_      = 0;
    int total_errors_  = 0;
    int total_absent_  = 0;

public:
    explicit NetworkConfigManager(const std::string &db_path)
        : incr_mgr_(db_path) {}

    /* Register a node: load its DCF and version requirements */
    bool add_node(uint8_t node_id, const std::string &dcf_path,
                  const version_requirement_t &ver_req)
    {
        DcfParser parser;
        if (!parser.load(dcf_path)) {
            printf("[CM] Failed to load DCF: %s\n", dcf_path.c_str());
            return false;
        }
        NodeConfig cfg;
        cfg.node_id  = node_id;
        cfg.ver_req  = ver_req;
        cfg.dcf_entries = parser.entries;
        node_configs_[node_id] = cfg;
        return true;
    }

    /* Called on Boot-Up from a node */
    void on_bootup(uint8_t node_id) {
        auto it = node_configs_.find(node_id);
        if (it == node_configs_.end()) {
            printf("[CM] Node %d: no config registered — starting unconfigured\n",
                   node_id);
            nmt_send(NMT_START_REMOTE, node_id);
            return;
        }

        const NodeConfig &cfg = it->second;

        /* Version check */
        ver_check_result_t vr = co_check_version(node_id, &cfg.ver_req);
        if (vr == VER_MAJOR_DIFFERS || vr == VER_WRONG_VENDOR ||
            vr == VER_WRONG_PRODUCT) {
            node_states_[node_id] = NODE_STATE_WRONG_FW;
            ++total_errors_;
            return;
        }
        if (vr == VER_MINOR_TOO_OLD) {
            printf("[CM] Node %d: minor revision too old, continuing anyway\n",
                   node_id);
        }

        /* Incremental delta */
        auto delta = incr_mgr_.compute_delta(node_id, cfg.dcf_entries);
        printf("[CM] Node %d: %zu / %zu entries need update\n",
               node_id, delta.size(), cfg.dcf_entries.size());

        /* Verify-before-write download */
        ConfigManager cm;
        auto stats = cm.verify_and_download(node_id, delta, /*post_verify=*/true);

        printf("[CM] Node %d: total=%d skip=%d write=%d err=%d\n",
               node_id, stats.total, stats.skipped, stats.written, stats.errors);

        if (stats.errors == 0) {
            /* Mark all written entries in incremental DB */
            for (const auto &e : delta)
                incr_mgr_.mark_written(node_id, e);
            incr_mgr_.save_db();

            nmt_send(NMT_START_REMOTE, node_id);
            node_states_[node_id] = NODE_STATE_OK;
            ++total_ok_;
        } else {
            node_states_[node_id] = NODE_STATE_CFG_ERROR;
            ++total_errors_;
        }
    }

    /* Called when a node fails to boot within T_boot */
    void on_bootup_timeout(uint8_t node_id) {
        printf("[CM] Node %d: absent (no Boot-Up received)\n", node_id);
        node_states_[node_id] = NODE_STATE_ABSENT;
        ++total_absent_;
    }

    void print_summary() const {
        printf("\n=== Configuration Manager Summary ===\n");
        printf("  Configured OK  : %d\n", total_ok_);
        printf("  Config errors  : %d\n", total_errors_);
        printf("  Absent nodes   : %d\n", total_absent_);
        for (const auto &kv : node_states_) {
            const char *s = "?";
            switch (kv.second) {
                case NODE_STATE_OK:        s = "OK";       break;
                case NODE_STATE_ABSENT:    s = "ABSENT";   break;
                case NODE_STATE_CFG_ERROR: s = "CFG_ERR";  break;
                case NODE_STATE_WRONG_FW:  s = "WRONG_FW"; break;
                default: break;
            }
            printf("  Node %3d : %s\n", kv.first, s);
        }
        printf("=====================================\n\n");
    }
};
```

### Main Application Entry Point

```c
int main(void)
{
    NetworkConfigManager mgr("/var/canopen/cfgdb.bin");

    version_requirement_t pump_req = {
        .vendor_id       = 0x00000315,
        .product_code    = 0x00000042,
        .required_major  = 1,
        .min_minor       = 2,
    };

    mgr.add_node(3, "/config/node3_pump.dcf",  pump_req);
    mgr.add_node(5, "/config/node5_valve.dcf", pump_req);
    mgr.add_node(7, "/config/node7_sensor.dcf", pump_req);

    /* Reset entire network, wait for Boot-Up events */
    nmt_send(NMT_RESET_COMM, 0 /* broadcast */);
    os_sleep_ms(100);

    uint32_t deadline = os_now_ms() + 5000; /* 5s boot window */
    while (os_now_ms() < deadline) {
        uint8_t booted_id;
        if (poll_bootup_event(&booted_id)) {
            mgr.on_bootup(booted_id);
        }
    }

    /* Any node that never sent Boot-Up is absent */
    for (uint8_t id : {3, 5, 7}) {
        if (mgr.node_states_[id] == NODE_STATE_UNKNOWN)
            mgr.on_bootup_timeout(id);
    }

    mgr.print_summary();
    return 0;
}
```

---

## Summary

The CANopen **Configuration Manager** provides a fully automated, robust mechanism
for network startup and node configuration. The five core techniques work together:

```
  +--------------------------------------------------------------+
  |              Configuration Manager Core Techniques           |
  +------+-------------------+----------------------------------+
  | #    | Technique         | Purpose                          |
  +------+-------------------+----------------------------------+
  | 1    | DCF Download      | Single source of truth for all   |
  |      |                   | node parameters; avoids manual   |
  |      |                   | per-node wiring in code          |
  +------+-------------------+----------------------------------+
  | 2    | Verify-Before-    | Reduces SDO bus traffic and      |
  |      | Write             | EEPROM wear; safe to run on      |
  |      |                   | every power cycle                |
  +------+-------------------+----------------------------------+
  | 3    | Identity / Version| Prevents applying wrong config   |
  |      | Checking          | to mismatched firmware; catches  |
  |      |                   | hardware substitution errors     |
  +------+-------------------+----------------------------------+
  | 4    | Incremental       | Only writes changed params;      |
  |      | Configuration     | dramatically cuts startup time   |
  |      |                   | in production systems            |
  +------+-------------------+----------------------------------+
  | 5    | Error Recovery    | Handles absent / misbehaving     |
  |      |                   | nodes without blocking rest of   |
  |      |                   | the network from starting        |
  +------+-------------------+----------------------------------+
```

**Key design principles:**

- **Non-blocking**: A failed node does not stop the rest of the network from entering
  OPERATIONAL state.
- **Idempotent**: Running the CM multiple times on a correctly configured node writes
  nothing (all entries already match).
- **Defensive**: Every version mismatch, SDO error, and timeout is explicitly handled
  with a defined policy.
- **Persistent**: The incremental configuration database survives power cycles, so
  subsequent startups are fast for unchanged nodes.
- **Standardised**: The DCF/EDS format, identity object (0x1018), and SDO protocol
  are all defined in CiA 301 and CiA 302, ensuring interoperability with third-party
  tools and nodes.

A properly implemented Configuration Manager transforms a complex manual commissioning
task into a zero-touch automatic process that is reproducible, auditable, and safe
to deploy in safety-critical industrial environments.

---

*References: CiA 301 CANopen Application Layer, CiA 302 CANopen NMT and Device Monitoring,
CiA 306 Electronic Data Sheet Specification.*# Configuration Manager & Automatic Node Configuration

> _TODO: add content_
