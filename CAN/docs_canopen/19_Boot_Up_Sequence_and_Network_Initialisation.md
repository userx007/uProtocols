# 19. CANopen Boot-Up Sequence & Network Initialisation

**What's inside:**

- **NMT State Machine** — Full ASCII state diagram with the four states (Initialisation, Pre-Operational, Operational, Stopped) and the SDO/PDO/NMT capability table per state.
- **Boot-Up Message** — CAN frame format, COB-ID formula, and a C detection function.
- **Master Boot Sequence** — ASCII sequence diagram with master↔slave interaction, NMT command frame format, and `co_nmt_send_command()` implementation.
- **Auto-Start vs. NMT-Commanded Start** — Timeline comparisons for both modes, OD 0x1F80 bit explanations, and a feature comparison table.
- **Configuration Manager Pattern** — ASCII architecture diagram showing the Boot-Up Monitor and Config Manager relationship, plus a full state machine diagram and C implementation of the manager.
- **Checking Mandatory Objects** — OD table of mandatory entries (0x1000, 0x1001, 0x1018) with a complete `co_check_mandatory_objects()` function.
- **SDO Config Before NMT Start** — Step-by-step PDO configuration sequence (disable → params → mapping → enable), heartbeat setup, and an SDO-with-retry wrapper.
- **Robustness Against Missing Nodes** — Node classification table (mandatory/optional/no-check), boot timeout logic, and heartbeat loss/re-appearance handling.
- **Complete Example** — A full master boot state machine tying all phases together, plus a comprehensive ASCII flow diagram showing the entire sequence end-to-end.
- **Summary** — Concise recap of all key principles.


---

## Table of Contents

1. [Introduction](#1-introduction)
2. [NMT State Machine Overview](#2-nmt-state-machine-overview)
3. [Boot-Up Message](#3-boot-up-message)
4. [Master Boot Sequence](#4-master-boot-sequence)
5. [Slave Auto-Start vs. NMT-Commanded Start](#5-slave-auto-start-vs-nmt-commanded-start)
6. [Configuration Manager Pattern](#6-configuration-manager-pattern)
7. [Checking Mandatory Objects](#7-checking-mandatory-objects)
8. [Configuration via SDO Before NMT Start](#8-configuration-via-sdo-before-nmt-start)
9. [Robustness Against Missing Nodes](#9-robustness-against-missing-nodes)
10. [Complete Example: Master Boot & Network Init](#10-complete-example-master-boot--network-init)
11. [Summary](#11-summary)

---

## 1. Introduction

CANopen defines a strict life-cycle management protocol for all nodes on a network. At power-on,
every node — master and slaves alike — must progress through a defined set of states before
exchanging application data. The **Boot-Up Sequence** governs this orderly start-up, ensuring
that:

- All mandatory objects exist and are accessible.
- Nodes are configured (via SDO) before they begin transmitting process data (PDOs).
- The NMT master controls when each slave enters the **Operational** state.
- The system behaves predictably even when some nodes are absent or slow to start.

The governing standard is **CiA 302** (CANopen manager and programmable devices) together with
**CiA 301** (CANopen application layer and communication profile).

---

## 2. NMT State Machine Overview

Every CANopen node implements the following NMT state machine:

```
                        Power-On / Reset
                              |
                              v
                    +-------------------+
                    |   INITIALISATION  |
                    |   (internal only) |
                    +-------------------+
                              |
                    (sends Boot-Up message)
                              |
                              v
                    +-------------------+
               +--->|   PRE-OPERATIONAL |<---+
               |    +-------------------+    |
               |      |             |        |
               |   NMT Start    NMT Stop     |
               |      |             |        |
               |      v             v        |
               |  +-----------+  +--------+  |
               |  |OPERATIONAL|  | STOPPED|  |
               |  +-----------+  +--------+  |
               |      |                      |
               +----------- NMT Pre-Op ------+
               |
               | NMT Reset Node / Reset Comm
               +---> back to INITIALISATION
```

**State descriptions:**

| State            | SDO | PDO | NMT | EMCY | SYNC |
|------------------|-----|-----|-----|------|------|
| INITIALISATION   | No  | No  | No  | No   | No   |
| PRE-OPERATIONAL  | Yes | No  | Yes | Yes  | Yes  |
| OPERATIONAL      | Yes | Yes | Yes | Yes  | Yes  |
| STOPPED          | No  | No  | Yes | No   | No   |

---

## 3. Boot-Up Message

When a node completes its internal initialisation it autonomously transitions to
**PRE-OPERATIONAL** and broadcasts a single **Boot-Up message** on the CAN bus.

```
  CAN ID  : 0x700 + Node-ID
  DLC     : 1
  Data[0] : 0x00

  Example for Node-ID = 0x05:
  +--------+-----+---------+
  | COB-ID | DLC |  Data   |
  +--------+-----+---------+
  | 0x705  |  1  |  0x00   |
  +--------+-----+---------+
```

The master listens for these Boot-Up messages as part of its boot procedure to know which
slaves are present and ready to be configured.

```c
/* Boot-Up message detection (receive side) */
#define CANOPEN_BOOTUP_BASE_COB_ID  0x700U

bool co_is_bootup_message(uint32_t cob_id, uint8_t dlc,
                          const uint8_t *data, uint8_t *node_id_out)
{
    if ((cob_id >= (CANOPEN_BOOTUP_BASE_COB_ID + 1U)) &&
        (cob_id <= (CANOPEN_BOOTUP_BASE_COB_ID + 127U)) &&
        dlc == 1U &&
        data[0] == 0x00U)
    {
        *node_id_out = (uint8_t)(cob_id - CANOPEN_BOOTUP_BASE_COB_ID);
        return true;
    }
    return false;
}
```

---

## 4. Master Boot Sequence

The NMT master follows a defined sequence after its own power-on. CiA 302 defines this as the
**"Boot process of a CANopen manager"**.

### 4.1 Sequence Diagram (ASCII)

```
  MASTER                              SLAVE(s)
    |                                    |
    |-- Power-On, Hardware Init -------->|  (slaves also power on)
    |                                    |
    |  [Internal Initialisation]         |  [Internal Initialisation]
    |                                    |
    |<------- Boot-Up (0x705, 0x00) -----|  Slave 5 ready
    |<------- Boot-Up (0x706, 0x00) -----|  Slave 6 ready
    |                                    |
    |  [Check mandatory objects via SDO] |
    |-- SDO Read 0x1000 (Device Type) -->|
    |<-- SDO Response: 0x00000000 -------|
    |-- SDO Read 0x1018 (Identity) ----->|
    |<-- SDO Response: Vendor/Product ---|
    |                                    |
    |  [Configure via SDO if needed]     |
    |-- SDO Write 0x1400 (RPDO Comm) --->|
    |<-- SDO Ack ------------------------|
    |-- SDO Write 0x1600 (RPDO Map)  --->|
    |<-- SDO Ack ------------------------|
    |                                    |
    |  [NMT Start Node]                  |
    |-- NMT 0x01, Node 5 (Start) ------->|  -> OPERATIONAL
    |-- NMT 0x01, Node 6 (Start) ------->|  -> OPERATIONAL
    |                                    |
    |  [Exchange PDO data]               |
    |<======= TPDO (process data) =======|
    |=======> RPDO (process data) ======>|
```

### 4.2 NMT Command Frame Format

```
  CAN ID  : 0x000  (broadcast, no node-ID offset)
  DLC     : 2
  Data[0] : Command Specifier (CS)
  Data[1] : Node-ID (0x00 = all nodes)

  Command Specifiers:
  +------+-----------------------------+
  |  CS  |  Command                    |
  +------+-----------------------------+
  | 0x01 |  Start Remote Node          |
  | 0x02 |  Stop Remote Node           |
  | 0x80 |  Enter Pre-Operational      |
  | 0x81 |  Reset Node                 |
  | 0x82 |  Reset Communication        |
  +------+-----------------------------+
```

```c
/* NMT command transmission */
typedef enum {
    NMT_CMD_START_REMOTE_NODE    = 0x01,
    NMT_CMD_STOP_REMOTE_NODE     = 0x02,
    NMT_CMD_ENTER_PREOP          = 0x80,
    NMT_CMD_RESET_NODE           = 0x81,
    NMT_CMD_RESET_COMMUNICATION  = 0x82
} co_nmt_command_t;

int co_nmt_send_command(co_handle_t *co, co_nmt_command_t cmd, uint8_t node_id)
{
    uint8_t data[2];
    data[0] = (uint8_t)cmd;
    data[1] = node_id;  /* 0x00 = all nodes */
    return can_transmit(co->can_if, 0x000U, 2U, data);
}

/* Start a specific slave */
co_nmt_send_command(&co_master, NMT_CMD_START_REMOTE_NODE, 0x05);

/* Start ALL slaves simultaneously */
co_nmt_send_command(&co_master, NMT_CMD_START_REMOTE_NODE, 0x00);
```

---

## 5. Slave Auto-Start vs. NMT-Commanded Start

There are two fundamental strategies for getting slave nodes into the **Operational** state.

### 5.1 NMT-Commanded Start (Recommended for Most Applications)

The slave remains in PRE-OPERATIONAL after its Boot-Up message. The master explicitly sends
`NMT Start Remote Node` when it is ready. This gives the master full control over the startup
sequence.

```
  Object 0x1F80 (NMT Startup) bit 2 = 0  --> slave waits for NMT Start command
```

```
  Timeline:

  t0   Slave powers on
       |
  t1   Slave enters PRE-OPERATIONAL, sends Boot-Up
       |  [SDO config window open]
  t2   Master configures PDOs, maps, heartbeat via SDO
       |
  t3   Master sends NMT Start (0x01, node-id)
       |
  t4   Slave enters OPERATIONAL, PDOs active
```

### 5.2 Slave Auto-Start

The slave autonomously transitions from PRE-OPERATIONAL to OPERATIONAL without waiting for
an NMT Start command. This is controlled by Object 0x1F80, bit 2.

```
  Object 0x1F80 (NMT Startup) bit 2 = 1  --> slave auto-starts to Operational
```

```
  Timeline:

  t0   Slave powers on
       |
  t1   Slave enters PRE-OPERATIONAL, sends Boot-Up
       |
  t2   Slave auto-transitions to OPERATIONAL (no master command needed)
       |
       NOTE: Master may miss the window to configure the slave!
             Use only for simple devices with fixed configuration.
```

**Object 0x1F80 — NMT Startup behaviour:**

```
  Bit 0: Master/Slave selection (1 = NMT master, 0 = NMT slave)
  Bit 1: Start slave nodes after boot (master sends NMT Start to all)
  Bit 2: Self-start (slave auto-starts to Operational)
  Bit 3: Start master auto (master enters Operational after all slaves configured)
```

```c
/* Slave: Configure auto-start via OD entry 0x1F80 */
#define OD_INDEX_NMT_STARTUP    0x1F80U
#define NMT_STARTUP_SELF_START  (1U << 2)

/* Disable auto-start — wait for master (recommended) */
uint32_t nmt_startup = 0x00000000U;
od_write_u32(OD_INDEX_NMT_STARTUP, 0x00, nmt_startup);

/* Enable auto-start */
uint32_t nmt_startup_auto = NMT_STARTUP_SELF_START;
od_write_u32(OD_INDEX_NMT_STARTUP, 0x00, nmt_startup_auto);
```

### 5.3 Comparison Table

```
  +----------------------------+------------------+--------------------+
  | Property                   | NMT-Commanded    | Auto-Start         |
  +----------------------------+------------------+--------------------+
  | Master control             | Full             | None               |
  | Config window (SDO)        | Guaranteed       | Not guaranteed     |
  | Safe for dynamic config    | Yes              | No                 |
  | Suitable for fixed config  | Yes              | Yes                |
  | Required CiA 302 master    | Yes              | No                 |
  | Recovery after errors      | Better           | Risk of stale cfg  |
  +----------------------------+------------------+--------------------+
```

---

## 6. Configuration Manager Pattern

For larger systems, CiA 302 defines a **Configuration Manager** — a software component
(typically running on the master or a dedicated node) that automates the SDO-based
configuration of slave nodes during boot.

### 6.1 Architecture

```
  +------------------------------------------------------------+
  |                  NMT MASTER NODE                           |
  |                                                            |
  |  +--------------+     +---------------------------+        |
  |  | Boot-Up      |     | Configuration Manager     |        |
  |  | Monitor      |---->| - Reads DCF / EDS         |        |
  |  |              |     | - Verifies device identity|        |
  |  | Tracks which |     | - Downloads SDO config    |        |
  |  | slaves have  |     | - Maintains config state  |        |
  |  | sent Boot-Up |     | - Reports pass/fail       |        |
  |  +--------------+     +---------------------------+        |
  |          ^                        |                        |
  +----------|------------------------|------------------------+
             |                        |  SDO Requests
     Boot-Up |                        v
    Messages |         +-----------------------------+
             |         |        CAN Bus              |
             |         +-----------------------------+
             |                   |         |
    +--------+--------+   +------+--+  +---+------+
    |    SLAVE 0x05   |   | SLAVE   |  | SLAVE    |
    |  PRE-OPERATIONAL|   |  0x06   |  |  0x07    |
    +-----------------+   +---------+  +----------+
```

### 6.2 Configuration Manager State Machine

```
  [IDLE]
     |
     | Network start triggered
     v
  [WAITING_FOR_BOOTUP]
     |
     | Boot-Up received from expected nodes
     | (or timeout for optional nodes)
     v
  [CHECKING_IDENTITY]   <-- SDO read 0x1000, 0x1018
     |
     | Device type and identity match DCF
     v
  [CONFIGURING]         <-- SDO writes from DCF/config table
     |
     | All SDOs acknowledged
     v
  [STARTING_NMT]        <-- NMT Start Remote Node
     |
     | Slave confirmed Operational (via heartbeat)
     v
  [OPERATIONAL]
```

### 6.3 Configuration Manager Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* Configuration entry: one SDO write operation */
typedef struct {
    uint16_t index;
    uint8_t  sub_index;
    uint32_t value;
    uint8_t  size;   /* 1, 2, or 4 bytes */
} co_config_entry_t;

/* Per-slave configuration descriptor */
typedef struct {
    uint8_t               node_id;
    uint32_t              expected_device_type;   /* OD 0x1000 */
    uint32_t              expected_vendor_id;     /* OD 0x1018.01 */
    uint32_t              expected_product_code;  /* OD 0x1018.02 */
    const co_config_entry_t *config_table;
    size_t                config_count;
} co_slave_descriptor_t;

/* Configuration manager state */
typedef enum {
    CFG_STATE_IDLE,
    CFG_STATE_WAIT_BOOTUP,
    CFG_STATE_CHECK_IDENTITY,
    CFG_STATE_CONFIGURE,
    CFG_STATE_START_NMT,
    CFG_STATE_OPERATIONAL,
    CFG_STATE_ERROR
} co_cfg_state_t;

typedef struct {
    co_cfg_state_t         state;
    const co_slave_descriptor_t *slave;
    size_t                 config_index;   /* current SDO write index */
    uint32_t               timeout_ms;
    uint32_t               elapsed_ms;
    bool                   bootup_received;
} co_cfg_manager_t;

/* Initialise the configuration manager for a slave */
void co_cfg_manager_init(co_cfg_manager_t        *mgr,
                         const co_slave_descriptor_t *slave,
                         uint32_t                 bootup_timeout_ms)
{
    memset(mgr, 0, sizeof(*mgr));
    mgr->slave      = slave;
    mgr->state      = CFG_STATE_WAIT_BOOTUP;
    mgr->timeout_ms = bootup_timeout_ms;
}

/* Call on every Boot-Up message received */
void co_cfg_manager_on_bootup(co_cfg_manager_t *mgr, uint8_t node_id)
{
    if (mgr->state == CFG_STATE_WAIT_BOOTUP &&
        mgr->slave->node_id == node_id)
    {
        mgr->bootup_received = true;
        mgr->state           = CFG_STATE_CHECK_IDENTITY;
        mgr->elapsed_ms      = 0U;
    }
}
```

---

## 7. Checking Mandatory Objects

Before configuring a slave, the configuration manager must verify that mandatory objects
exist and that the device is what it claims to be. CiA 301 mandates certain OD entries.

### 7.1 Mandatory Objects (CiA 301)

```
  +--------+-----------+-----------------------------------------------+
  | Index  | Sub-Index | Description                                   |
  +--------+-----------+-----------------------------------------------+
  | 0x1000 | 0x00      | Device Type (UNSIGNED32)                      |
  | 0x1001 | 0x00      | Error Register (UNSIGNED8)                    |
  | 0x1018 | 0x00      | Identity Object (highest sub-index: 1..4)     |
  | 0x1018 | 0x01      | Vendor ID                                     |
  | 0x1018 | 0x02      | Product Code                                  |
  | 0x1018 | 0x03      | Revision Number (optional in some profiles)   |
  | 0x1018 | 0x04      | Serial Number   (optional in some profiles)   |
  +--------+-----------+-----------------------------------------------+
```

### 7.2 Reading Mandatory Objects via SDO

```c
/* SDO expedited read helper (simplified) */
typedef struct {
    bool     success;
    uint32_t value;
    uint8_t  size;
} co_sdo_result_t;

co_sdo_result_t co_sdo_read(co_handle_t *co, uint8_t node_id,
                             uint16_t index, uint8_t sub_index);

/* Check mandatory identity objects */
typedef struct {
    bool     mandatory_ok;
    bool     identity_match;
    uint32_t device_type;
    uint32_t vendor_id;
    uint32_t product_code;
} co_identity_check_t;

co_identity_check_t co_check_mandatory_objects(
    co_handle_t                 *co,
    const co_slave_descriptor_t *slave)
{
    co_identity_check_t result = {0};
    co_sdo_result_t     sdo;

    /* 1. Read Device Type (0x1000.00) — MANDATORY */
    sdo = co_sdo_read(co, slave->node_id, 0x1000U, 0x00U);
    if (!sdo.success) {
        /* Fatal: 0x1000 must exist */
        return result;
    }
    result.device_type  = sdo.value;
    result.mandatory_ok = true;

    /* 2. Read Error Register (0x1001.00) — MANDATORY */
    sdo = co_sdo_read(co, slave->node_id, 0x1001U, 0x00U);
    if (!sdo.success) {
        result.mandatory_ok = false;
        return result;
    }
    if (sdo.value != 0U) {
        /* Slave is reporting an error — log but continue */
        co_log_warning("Node %u: Error Register = 0x%02X",
                       slave->node_id, sdo.value);
    }

    /* 3. Read Identity: Vendor ID (0x1018.01) */
    sdo = co_sdo_read(co, slave->node_id, 0x1018U, 0x01U);
    if (!sdo.success) {
        return result;
    }
    result.vendor_id = sdo.value;

    /* 4. Read Identity: Product Code (0x1018.02) */
    sdo = co_sdo_read(co, slave->node_id, 0x1018U, 0x02U);
    if (!sdo.success) {
        return result;
    }
    result.product_code = sdo.value;

    /* 5. Cross-check against expected values from DCF/descriptor */
    result.identity_match =
        (result.device_type  == slave->expected_device_type) &&
        (result.vendor_id    == slave->expected_vendor_id)   &&
        (result.product_code == slave->expected_product_code);

    return result;
}
```

---

## 8. Configuration via SDO Before NMT Start

The most important rule: **all SDO configuration must complete before the NMT Start Remote
Node command is sent**. PDOs are inactive in PRE-OPERATIONAL, so the configuration window is
safe and deterministic.

### 8.1 PDO Configuration Sequence

PDO configuration always follows the pattern:
1. Disable the PDO (set bit 31 of COB-ID to 1).
2. Write communication parameters (0x1400/0x1800 series).
3. Write mapping parameters (0x1600/0x1A00 series).
4. Re-enable the PDO (clear bit 31 of COB-ID).

```
  Configure RPDO 1 on Slave Node 5  (Master writes to slave's OD):

  Step  Index    Sub  Value        Description
  ----  -------  ---  -----------  --------------------------------------
   1    0x1400   0x01 0x8000_0205  Disable COB-ID (bit31=1), ID=0x205
   2    0x1400   0x02 0x00000001   Transmission type = synchronous
   3    0x1600   0x00 0x00000000   Disable mapping (count = 0)
   4    0x1600   0x01 0x60400110   Map: OD[0x6040].01, 16 bits (CiA 402)
   5    0x1600   0x02 0x60600108   Map: OD[0x6060].01, 8 bits
   6    0x1600   0x00 0x00000002   Enable mapping (count = 2)
   7    0x1400   0x01 0x0000_0205  Enable COB-ID (bit31=0), ID=0x205
```

```c
/* Generic SDO expedited write */
int co_sdo_write_u32(co_handle_t *co, uint8_t node_id,
                     uint16_t index, uint8_t sub_index, uint32_t value);
int co_sdo_write_u8 (co_handle_t *co, uint8_t node_id,
                     uint16_t index, uint8_t sub_index, uint8_t  value);

/* Configure a single RPDO on a slave */
int co_configure_rpdo(co_handle_t *co,
                      uint8_t      node_id,
                      uint8_t      pdo_num,       /* 0-based: RPDO1=0 */
                      uint32_t     cob_id,
                      uint8_t      trans_type,
                      const uint32_t *mappings,   /* OD mapping entries */
                      uint8_t      mapping_count)
{
    uint16_t comm_idx = (uint16_t)(0x1400U + pdo_num);
    uint16_t map_idx  = (uint16_t)(0x1600U + pdo_num);
    int      rc;

    /* Step 1: Disable PDO (set invalid bit) */
    rc = co_sdo_write_u32(co, node_id, comm_idx, 0x01U,
                          cob_id | 0x80000000UL);
    if (rc != 0) return rc;

    /* Step 2: Set transmission type */
    rc = co_sdo_write_u8(co, node_id, comm_idx, 0x02U, trans_type);
    if (rc != 0) return rc;

    /* Step 3: Clear mapping count */
    rc = co_sdo_write_u8(co, node_id, map_idx, 0x00U, 0U);
    if (rc != 0) return rc;

    /* Step 4: Write each mapping entry */
    for (uint8_t i = 0U; i < mapping_count; i++) {
        rc = co_sdo_write_u32(co, node_id, map_idx,
                              (uint8_t)(i + 1U), mappings[i]);
        if (rc != 0) return rc;
    }

    /* Step 5: Set mapping count (enables mapping) */
    rc = co_sdo_write_u8(co, node_id, map_idx, 0x00U, mapping_count);
    if (rc != 0) return rc;

    /* Step 6: Enable PDO (clear invalid bit) */
    rc = co_sdo_write_u32(co, node_id, comm_idx, 0x01U,
                          cob_id & ~0x80000000UL);
    return rc;
}
```

### 8.2 Heartbeat Producer Configuration

```c
/* Configure heartbeat on slave (OD 0x1017 = producer time in ms) */
int co_configure_heartbeat_producer(co_handle_t *co,
                                    uint8_t      node_id,
                                    uint16_t     period_ms)
{
    /* 0 = heartbeat disabled, >0 = period in milliseconds */
    return co_sdo_write_u32(co, node_id, 0x1017U, 0x00U,
                            (uint32_t)period_ms);
}

/* Configure heartbeat consumer on master (OD 0x1016) */
int co_configure_heartbeat_consumer(co_handle_t *co_master,
                                    uint8_t      consumer_idx, /* 1-based */
                                    uint8_t      monitored_node_id,
                                    uint16_t     consumer_time_ms)
{
    /* Format: [0x00][NodeID][TimeHigh][TimeLow]                       */
    /* consumer_time_ms should be > producer_time_ms (e.g., 1.5x)      */
    uint32_t value = ((uint32_t)monitored_node_id << 16U) |
                     (uint32_t)consumer_time_ms;
    return co_sdo_write_u32(co_master->od, co_master->node_id,
                            0x1016U, consumer_idx, value);
}
```

### 8.3 Full SDO Configuration Sequence with Retry

```c
#define SDO_MAX_RETRIES  3U
#define SDO_RETRY_DELAY_MS 100U

int co_sdo_write_with_retry(co_handle_t *co, uint8_t node_id,
                             uint16_t index, uint8_t sub, uint32_t value)
{
    for (uint8_t attempt = 0U; attempt < SDO_MAX_RETRIES; attempt++) {
        int rc = co_sdo_write_u32(co, node_id, index, sub, value);
        if (rc == 0) {
            return 0;   /* success */
        }
        co_log_warning("SDO write 0x%04X.%02X on node %u failed "
                       "(attempt %u/%u): %d",
                       index, sub, node_id, attempt + 1U,
                       SDO_MAX_RETRIES, rc);
        co_delay_ms(SDO_RETRY_DELAY_MS);
    }
    return -1;  /* all retries exhausted */
}

/* Apply an entire configuration table to a slave */
int co_apply_config_table(co_handle_t                 *co,
                          uint8_t                      node_id,
                          const co_config_entry_t     *table,
                          size_t                       count)
{
    for (size_t i = 0U; i < count; i++) {
        int rc = co_sdo_write_with_retry(co, node_id,
                                         table[i].index,
                                         table[i].sub_index,
                                         table[i].value);
        if (rc != 0) {
            co_log_error("Config failed at entry %zu "
                         "(0x%04X.%02X) for node %u",
                         i, table[i].index, table[i].sub_index, node_id);
            return rc;
        }
    }
    return 0;
}
```

---

## 9. Robustness Against Missing Nodes

A production-quality master must handle the case where slaves do not appear within the
expected time window, or disappear after initial start-up.

### 9.1 Node Classification

CiA 302 defines how the master classifies nodes:

```
  +------------------+-----------------------------------------------------+
  | Class            | Behaviour if absent at boot                         |
  +------------------+-----------------------------------------------------+
  | MANDATORY        | Master MUST NOT start network — halt with error     |
  | OPTIONAL         | Master may start without it — log warning           |
  | NO_CHECK         | Master ignores presence/absence completely          |
  +------------------+-----------------------------------------------------+
```

Object 0x1F81 (Node Assignment) per slave controls this classification.

### 9.2 Boot-Up Timeout and Node Tracking

```c
#define MAX_NODES           127U
#define BOOTUP_TIMEOUT_MS   5000U   /* 5 s default per CiA 302 */

typedef enum {
    NODE_CLASS_MANDATORY,
    NODE_CLASS_OPTIONAL,
    NODE_CLASS_NO_CHECK
} co_node_class_t;

typedef struct {
    uint8_t          node_id;
    co_node_class_t  node_class;
    bool             bootup_received;
    bool             configured;
    bool             operational;
    uint32_t         bootup_time_ms;  /* timestamp when Boot-Up received */
} co_node_status_t;

typedef struct {
    co_node_status_t nodes[MAX_NODES];
    uint8_t          node_count;
    uint32_t         boot_start_ms;
} co_boot_monitor_t;

/* Check whether boot may proceed */
bool co_boot_may_proceed(const co_boot_monitor_t *mon, uint32_t now_ms)
{
    bool all_mandatory_ready = true;
    uint32_t elapsed = now_ms - mon->boot_start_ms;

    if (elapsed < BOOTUP_TIMEOUT_MS) {
        /* Timeout not yet elapsed — wait for more nodes */
        return false;
    }

    /* Timeout elapsed: check mandatory nodes */
    for (uint8_t i = 0U; i < mon->node_count; i++) {
        const co_node_status_t *n = &mon->nodes[i];
        if (n->node_class == NODE_CLASS_MANDATORY &&
            !n->bootup_received)
        {
            co_log_error("Mandatory node %u missing — cannot start network",
                         n->node_id);
            all_mandatory_ready = false;
        }
        else if (n->node_class == NODE_CLASS_OPTIONAL &&
                 !n->bootup_received)
        {
            co_log_warning("Optional node %u absent — continuing without it",
                           n->node_id);
        }
    }

    return all_mandatory_ready;
}
```

### 9.3 Heartbeat-Based Node Loss Detection at Runtime

```
  Heartbeat monitoring timeline:

  SLAVE                          MASTER
    |                               |
    |-- HB (t=0ms) ---------------->| consumer timer reset
    |                               |
    |-- HB (t=100ms) -------------->| consumer timer reset
    |                               |
    |   [SLAVE CRASHES / BUS ERROR] |
    |                               | consumer_time (e.g. 150ms) expires
    |                               |-> HEARTBEAT_EVENT (error)
    |                               |-> Emergency EMCY to application
    |                               |-> NMT state of slave = UNKNOWN
    |                               |
    |-- HB (t=500ms, after reset) ->| Boot-Up or Heartbeat received
    |                               |-> slave detected as re-appeared
    |                               |-> optional: re-configure and restart
```

```c
/* Heartbeat event handler — called by CANopen stack */
void co_heartbeat_event_cb(uint8_t node_id,
                           co_nmt_state_t previous_state,
                           co_nmt_state_t new_state,
                           void          *user_data)
{
    co_boot_monitor_t *mon = (co_boot_monitor_t *)user_data;

    if (new_state == CO_NMT_STATE_UNKNOWN) {
        /* Node lost — heartbeat consumer timed out */
        co_log_error("Node %u heartbeat lost!", node_id);

        /* Find the node descriptor */
        for (uint8_t i = 0U; i < mon->node_count; i++) {
            if (mon->nodes[i].node_id == node_id) {
                mon->nodes[i].operational = false;
                if (mon->nodes[i].node_class == NODE_CLASS_MANDATORY) {
                    /* Trigger application-level safety response */
                    app_handle_mandatory_node_loss(node_id);
                }
                break;
            }
        }
    }
    else if (new_state == CO_NMT_STATE_PREOP) {
        /* Node reappeared after reset — re-configure and restart */
        co_log_info("Node %u re-appeared in PRE-OPERATIONAL", node_id);
        /* Schedule re-configuration (not from ISR context) */
        co_schedule_node_reconfig(node_id);
    }
}
```

---

## 10. Complete Example: Master Boot & Network Init

This section brings all the pieces together into a complete boot sequence implementation
suitable for an embedded NMT master.

```c
/*
 * co_network_init.c
 * CANopen Master Boot Sequence — complete example
 */

#include "canopen_master.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Network configuration (would typically come from a DCF/NVS)        */
/* ------------------------------------------------------------------ */

/* RPDO1 of drive node: ControlWord + ModeOfOperation (CiA 402) */
static const uint32_t drive_rpdo1_mappings[] = {
    0x60400110UL,   /* 0x6040.01, 16-bit */
    0x60600108UL    /* 0x6060.01,  8-bit */
};

/* Drive node 0x05 configuration table */
static const co_config_entry_t drive_node5_config[] = {
    /* Heartbeat producer: 100 ms */
    { 0x1017U, 0x00U, 100U,    4U },
    /* SYNC interval: 10 ms (0x80002710 = 10000 us) */
    { 0x1006U, 0x00U, 10000U,  4U },
    /* Emergency COB-ID: standard (0x80 + node-id) */
    { 0x1014U, 0x00U, 0x85U,   4U },
};

/* Slave descriptor */
static const co_slave_descriptor_t drive_slave = {
    .node_id              = 0x05U,
    .expected_device_type = 0x00000192UL,  /* CiA 402 drive profile */
    .expected_vendor_id   = 0x0000015AUL,  /* example vendor */
    .expected_product_code= 0x00001234UL,
    .config_table         = drive_node5_config,
    .config_count         = sizeof(drive_node5_config) /
                            sizeof(drive_node5_config[0])
};


/* ------------------------------------------------------------------ */
/* Master boot procedure                                               */
/* ------------------------------------------------------------------ */

typedef enum {
    BOOT_PHASE_INIT,
    BOOT_PHASE_WAIT_BOOTUP,
    BOOT_PHASE_IDENTITY_CHECK,
    BOOT_PHASE_SDO_CONFIG,
    BOOT_PHASE_PDO_CONFIG,
    BOOT_PHASE_NMT_START,
    BOOT_PHASE_RUNNING,
    BOOT_PHASE_FATAL_ERROR
} boot_phase_t;

typedef struct {
    boot_phase_t           phase;
    co_handle_t           *co;
    co_boot_monitor_t      monitor;
    co_cfg_manager_t       cfg_mgr;
    uint32_t               phase_start_ms;
} master_boot_ctx_t;

static master_boot_ctx_t s_boot;

/* Called once from main() or RTOS task on startup */
void co_master_boot_init(co_handle_t *co)
{
    memset(&s_boot, 0, sizeof(s_boot));
    s_boot.co             = co;
    s_boot.phase          = BOOT_PHASE_INIT;
    s_boot.phase_start_ms = co_get_ms();

    /* Register one slave to monitor */
    s_boot.monitor.nodes[0].node_id    = drive_slave.node_id;
    s_boot.monitor.nodes[0].node_class = NODE_CLASS_MANDATORY;
    s_boot.monitor.node_count          = 1U;
    s_boot.monitor.boot_start_ms       = co_get_ms();

    /* Configure heartbeat consumer on master for slave 5 */
    /* Consumer time = 150 ms (1.5x producer of 100 ms) */
    co_configure_heartbeat_consumer(co, 1U, 0x05U, 150U);

    co_log_info("CANopen master boot initiated");
}

/* Periodic task — call every ~10 ms */
void co_master_boot_tick(void)
{
    uint32_t now_ms  = co_get_ms();
    int      rc;

    switch (s_boot.phase) {

    case BOOT_PHASE_INIT:
        /* Master sends NMT Reset Communication to all nodes to ensure
         * a clean start. Slaves will re-initialise and send Boot-Up. */
        co_nmt_send_command(s_boot.co, NMT_CMD_RESET_COMMUNICATION, 0x00U);
        s_boot.monitor.boot_start_ms = now_ms;
        s_boot.phase                 = BOOT_PHASE_WAIT_BOOTUP;
        co_log_info("NMT Reset Communication sent to all nodes");
        break;

    case BOOT_PHASE_WAIT_BOOTUP:
        if (co_boot_may_proceed(&s_boot.monitor, now_ms)) {
            co_log_info("All mandatory nodes present — proceeding");
            s_boot.phase          = BOOT_PHASE_IDENTITY_CHECK;
            s_boot.phase_start_ms = now_ms;
        }
        /* co_master_on_bootup() sets bootup_received via callback */
        break;

    case BOOT_PHASE_IDENTITY_CHECK: {
        co_identity_check_t id_check =
            co_check_mandatory_objects(s_boot.co, &drive_slave);

        if (!id_check.mandatory_ok) {
            co_log_error("Node %u: mandatory OD objects missing",
                         drive_slave.node_id);
            s_boot.phase = BOOT_PHASE_FATAL_ERROR;
            break;
        }
        if (!id_check.identity_match) {
            co_log_error("Node %u: identity mismatch "
                         "(vendor=0x%08X product=0x%08X)",
                         drive_slave.node_id,
                         id_check.vendor_id, id_check.product_code);
            s_boot.phase = BOOT_PHASE_FATAL_ERROR;
            break;
        }
        co_log_info("Node %u identity verified", drive_slave.node_id);
        s_boot.phase = BOOT_PHASE_SDO_CONFIG;
        break;
    }

    case BOOT_PHASE_SDO_CONFIG:
        rc = co_apply_config_table(s_boot.co,
                                   drive_slave.node_id,
                                   drive_slave.config_table,
                                   drive_slave.config_count);
        if (rc != 0) {
            co_log_error("Node %u: SDO config failed", drive_slave.node_id);
            s_boot.phase = BOOT_PHASE_FATAL_ERROR;
            break;
        }
        co_log_info("Node %u: SDO configuration complete",
                    drive_slave.node_id);
        s_boot.phase = BOOT_PHASE_PDO_CONFIG;
        break;

    case BOOT_PHASE_PDO_CONFIG:
        /* Configure RPDO1 on drive node */
        rc = co_configure_rpdo(s_boot.co,
                               drive_slave.node_id,
                               0U,               /* RPDO number 1 (0-based) */
                               0x00000205UL,     /* COB-ID 0x205 */
                               0x01U,            /* synchronous */
                               drive_rpdo1_mappings,
                               2U);
        if (rc != 0) {
            co_log_error("Node %u: PDO config failed", drive_slave.node_id);
            s_boot.phase = BOOT_PHASE_FATAL_ERROR;
            break;
        }
        co_log_info("Node %u: PDO configuration complete",
                    drive_slave.node_id);
        s_boot.phase = BOOT_PHASE_NMT_START;
        break;

    case BOOT_PHASE_NMT_START:
        /* All config done — start the slave */
        co_nmt_send_command(s_boot.co, NMT_CMD_START_REMOTE_NODE,
                            drive_slave.node_id);
        co_log_info("NMT Start sent to node %u — entering OPERATIONAL",
                    drive_slave.node_id);
        s_boot.monitor.nodes[0].operational = true;
        s_boot.phase                        = BOOT_PHASE_RUNNING;
        break;

    case BOOT_PHASE_RUNNING:
        /* Normal operation — heartbeat and PDO watchdog handled
         * by the CANopen stack callbacks */
        break;

    case BOOT_PHASE_FATAL_ERROR:
        /* Application-specific error handling:
         * - set safe outputs
         * - light fault LED
         * - log to NVS
         * - optionally retry after delay */
        app_enter_safe_state();
        break;
    }
}

/* Called from CAN receive ISR / task when Boot-Up message arrives */
void co_master_on_bootup(uint8_t node_id)
{
    for (uint8_t i = 0U; i < s_boot.monitor.node_count; i++) {
        if (s_boot.monitor.nodes[i].node_id == node_id) {
            s_boot.monitor.nodes[i].bootup_received = true;
            s_boot.monitor.nodes[i].bootup_time_ms  = co_get_ms();
            co_log_info("Boot-Up received from node %u", node_id);

            /* If we are in RUNNING phase, node has re-appeared after reset */
            if (s_boot.phase == BOOT_PHASE_RUNNING) {
                co_log_warning("Node %u re-appeared unexpectedly "
                               "— re-configuring", node_id);
                s_boot.phase = BOOT_PHASE_IDENTITY_CHECK;
            }
            break;
        }
    }
}
```

### 10.1 Full Boot Sequence Flow (ASCII)

```
  POWER ON
     |
     v
  [MASTER: INIT phase]
     |-- NMT Reset Communication (0x82, 0x00) --> ALL NODES
     |
     v
  [MASTER: WAIT_BOOTUP phase]
     |
     |<-- Boot-Up (0x705, 0x00) -- SLAVE 5
     |
     | (timeout 5000 ms elapses)
     | All mandatory nodes present?  YES --> continue
     |                               NO  --> FATAL ERROR
     v
  [MASTER: IDENTITY_CHECK phase]
     |-- SDO Read 0x1000.00 ------> SLAVE 5 : Device Type
     |<-- 0x00000192 (CiA 402)
     |-- SDO Read 0x1018.01 ------> SLAVE 5 : Vendor ID
     |<-- 0x0000015A
     |-- SDO Read 0x1018.02 ------> SLAVE 5 : Product Code
     |<-- 0x00001234
     | Match expected? YES --> continue
     v
  [MASTER: SDO_CONFIG phase]
     |-- SDO Write 0x1017.00=100 -> SLAVE 5 : HB period 100 ms
     |-- SDO Write 0x1006.00=10000 SLAVE 5 : SYNC period 10 ms
     |-- SDO Write 0x1014.00=0x85 > SLAVE 5 : EMCY COB-ID
     v
  [MASTER: PDO_CONFIG phase]
     |-- SDO Write 0x1400.01 -----> SLAVE 5 : Disable RPDO1
     |-- SDO Write 0x1400.02 -----> SLAVE 5 : Trans type = 1
     |-- SDO Write 0x1600.00=0 ---> SLAVE 5 : Clear map count
     |-- SDO Write 0x1600.01 -----> SLAVE 5 : Map ControlWord
     |-- SDO Write 0x1600.02 -----> SLAVE 5 : Map ModeOfOp
     |-- SDO Write 0x1600.00=2 ---> SLAVE 5 : Enable 2 entries
     |-- SDO Write 0x1400.01 -----> SLAVE 5 : Enable RPDO1
     v
  [MASTER: NMT_START phase]
     |-- NMT Start (0x01, 0x05) --> SLAVE 5 --> OPERATIONAL
     v
  [MASTER: RUNNING phase]
     |
     |<====== TPDO1 every SYNC ====== SLAVE 5 : Status/Position
     |======> RPDO1 every SYNC =====> SLAVE 5 : ControlWord/Mode
     |
     |<------ HB (every 100 ms) ----- SLAVE 5
     | (master consumer 150 ms — resets on each HB)
     |
     +-- [Node loss] --> co_heartbeat_event_cb() -> safe state / reconfig
```

---

## 11. Summary

CANopen's boot-up and network initialisation sequence provides a structured, deterministic
mechanism for bringing a multi-node network online safely. The key principles are:

**NMT state machine** — Every node starts in INITIALISATION, automatically transitions to
PRE-OPERATIONAL (broadcasting its Boot-Up message), and then waits for master commands.
PDOs are only active in OPERATIONAL.

**Boot-Up message (0x700 + NodeID, data=0x00)** — The single-byte signal that announces a
node's readiness for configuration. The master tracks these to know which slaves are online.

**NMT-commanded start vs. auto-start** — Controlled via OD 0x1F80. NMT-commanded start
(bit 2 = 0) is strongly preferred for any system requiring SDO configuration, as it guarantees
the configuration window between Boot-Up and entering OPERATIONAL.

**Configuration Manager pattern** — The master checks mandatory objects (0x1000, 0x1001,
0x1018), verifies device identity against expected values (from a DCF or internal table), and
applies SDO writes to configure PDOs, heartbeats, SYNC, and application parameters — all
before sending the NMT Start command.

**PDO configuration order** — Disable → configure communication parameters → configure
mapping (count to 0, write entries, count to N) → enable. Never write to mapping entries
while the PDO is enabled and the count is non-zero.

**Robustness** — Mandatory vs. optional node classification (OD 0x1F81) determines whether a
missing slave blocks the entire network start. Heartbeat monitoring detects runtime node loss
and triggers re-configuration on re-appearance. SDO retries handle transient bus errors.

**Timing discipline** — The master must complete all SDO transactions before issuing NMT
Start. In PRE-OPERATIONAL there is no risk of PDO collisions. After NMT Start the only
safe per-node configuration mechanism is through synchronous SDO exchanges, which must not
conflict with active PDO cycles.

These principles together ensure a repeatable, safe network start-up that is resilient to
hardware variance, missing nodes, and power-cycling of individual devices.

---

*References: CiA 301 v4.2 — CANopen application layer and communication profile;
CiA 302 v4.1 — CANopen manager and programmable devices.*