# 35. Firmware Update over CANopen (LSS / SDO Block)

**Protocol Foundations** — Object dictionary entries (0x1F50–0x1F57), NMT states, and how Pre-Operational mode is used to isolate update traffic from normal PDO operations.
**LSS (Layer Setting Services)** — Full state machine diagram, Switch Mode Selective frame sequence, and a C example that selects a node by its 128-bit Vendor/Product/Revision/Serial identity and triggers bootloader entry.
**SDO Block Transfer** — Byte-level CAN frame layouts for all phases (Initiate, Segment, Block Ack, End), a complete C++ master-side downloader class, and a C slave-side server suitable for embedding in a bootloader.
**Bootloader Architecture** — Memory map ASCII diagrams for single-bank and dual-bank layouts, a boot-decision flowchart, and the ARM Cortex-M `jump_to_application()` vector-table relocation routine.
**Dual-Bank & Golden-Image** — Timeline diagrams of the update swap sequence, rollback flow, and `FwHeader` struct with a `bootloader_select_bank()` implementation.
**CRC32** — Both a software table-driven implementation and an STM32 hardware CRC unit example, with a warning about the polynomial difference.
**Flash-Verify-Execute** — Full master/slave sequence diagram showing every SDO frame from LSS selection through NMT boot-up of the new application.
**Throughput Calculations** — Segmented vs. block transfer comparison, block-size sensitivity table, baud-rate impact table, flash erase time breakdown, and multi-node bus-load budgeting.

> Bootloader design, program download via SDO block transfer, flash-verify-execute flow,
> dual-bank and golden-image strategies, transfer integrity (CRC32), and production
> programming throughput calculations.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [CANopen Protocol Foundations for Firmware Update](#2-canopen-protocol-foundations-for-firmware-update)
3. [LSS – Layer Setting Services](#3-lss--layer-setting-services)
4. [SDO Block Transfer](#4-sdo-block-transfer)
5. [Bootloader Architecture](#5-bootloader-architecture)
6. [Flash Memory Layouts](#6-flash-memory-layouts)
7. [Dual-Bank and Golden-Image Strategies](#7-dual-bank-and-golden-image-strategies)
8. [Transfer Integrity – CRC32](#8-transfer-integrity--crc32)
9. [Flash-Verify-Execute Flow](#9-flash-verify-execute-flow)
10. [C/C++ Implementation Examples](#10-cc-implementation-examples)
11. [Production Programming Throughput Calculations](#11-production-programming-throughput-calculations)
12. [Error Handling and Abort Codes](#12-error-handling-and-abort-codes)
13. [Summary](#13-summary)

---

## 1. Introduction

Firmware updates over a CAN bus are a practical necessity in industrial, automotive, and
embedded systems. CANopen provides two complementary mechanisms that, when combined, give
a clean, reliable, and standardised way to update firmware on any node in the network:

- **LSS (Layer Setting Services)** – used to identify, address, and switch individual nodes
  into a special configuration mode before programming begins.
- **SDO Block Transfer** – a high-throughput variant of the SDO (Service Data Object)
  protocol used to push large binary payloads (firmware images) efficiently across the bus.

Neither mechanism requires proprietary extensions. A conformant CANopen master running
standard stack software can update any conformant slave without vendor-specific tooling,
making the approach attractive for open production lines and field service.

---

## 2. CANopen Protocol Foundations for Firmware Update

### 2.1 Object Dictionary Entries Relevant to Firmware Update

The CANopen object dictionary (OD) contains standardised entries used during the update
process:

```
Index   Sub  Name                          Access  Type
------  ---  ----------------------------  ------  ------
0x1000   0   Device Type                   RO      UINT32
0x1018   0   Identity Object (count)       RO      UINT8
0x1018   1   Vendor ID                     RO      UINT32
0x1018   2   Product Code                  RO      UINT32
0x1018   3   Revision Number               RO      UINT32
0x1018   4   Serial Number                 RO      UINT32
0x1F50   1   Program Data (download area)  WO      DOMAIN
0x1F51   1   Program Control               RW      UINT8
0x1F56   1   Program Software Checksum     RO      UINT32
0x1F57   1   Flash Status Summary          RO      UINT32
```

Index **0x1F50** (Program Data) is the SDO block transfer target. The master writes the
raw firmware binary here. Index **0x1F51** (Program Control) drives the state machine:

```
Value  Meaning
-----  -------
  0    Stop program / prepare for download
  1    Start program  (jump to application)
  2    Reset application
  3    Clear program (erase flash)
```

### 2.2 Network Management States

```
            +----------+
            |  Initialising  |
            +-----+----+
                  | Boot-up message (0x700 + NodeID)
                  v
            +-----+----+       NMT Start_Remote_Node
            |  Pre-Operational +--------------------------> Operational
            +-----+----+
                  |  NMT Stop_Remote_Node
                  v
            +----------+
            |  Stopped  |
            +----------+
```

For firmware updates the node is placed in **Pre-Operational** (or kept there after
boot-up) so SDO communication is available but PDO traffic does not interfere.

---

## 3. LSS – Layer Setting Services

### 3.1 Overview

LSS (CiA 305) operates on two fixed CAN IDs:

```
Direction       CAN ID   Description
-----------     ------   ---------------------------
Master -> Slave  0x7E5   LSS command frames
Slave  -> Master 0x7E4   LSS response frames
```

LSS is used **before** SDO to:

1. Discover all nodes on the bus (LSS Identify Slave).
2. Select one specific node by its full 128-bit LSS address (Vendor ID + Product Code +
   Revision + Serial Number).
3. Switch that node into **LSS Waiting** state so its Node-ID and baud rate can be
   configured, or so it can be told to enter bootloader mode.

### 3.2 LSS State Machine

```
  +------------------+
  |   LSS Waiting    |<---------- Power-on default (all nodes start here)
  +--------+---------+
           |  Switch Mode Selective (or Switch Mode Global)
           v
  +------------------+
  |  LSS Configuration|   <-- Node-ID, baud rate, store, bootloader entry
  +--------+---------+
           |  Switch Mode Global (back to waiting)
           v
  +------------------+
  |   LSS Waiting    |
  +------------------+
```

### 3.3 Selecting a Node by LSS Address

The **Switch Mode Selective** sequence sends four consecutive frames, one for each 32-bit
word of the LSS address. Only the node whose full address matches all four words enters
Configuration state.

```
Frame 1:  CS=0x40  Vendor ID   (bytes 1-4, little-endian)
Frame 2:  CS=0x41  Product Code
Frame 3:  CS=0x42  Revision Number
Frame 4:  CS=0x43  Serial Number
```

The selected node responds with CS=0x44 (Identify Slave response).

### 3.4 C Example – LSS Switch Mode Selective

```c
#include <stdint.h>
#include <string.h>
#include "can_driver.h"   /* platform CAN send/receive */

#define LSS_TX_ID   0x7E5u
#define LSS_RX_ID   0x7E4u

typedef struct {
    uint32_t vendor_id;
    uint32_t product_code;
    uint32_t revision;
    uint32_t serial;
} LssAddress;

static void lss_send(uint8_t cs, uint32_t data)
{
    CanFrame f = {0};
    f.id  = LSS_TX_ID;
    f.dlc = 8;
    f.data[0] = cs;
    memcpy(&f.data[1], &data, 4);   /* little-endian */
    can_send(&f);
}

/**
 * Switch one node (identified by addr) into LSS Configuration state.
 * Returns 0 on success, -1 on timeout / no response.
 */
int lss_switch_mode_selective(const LssAddress *addr, uint32_t timeout_ms)
{
    lss_send(0x40, addr->vendor_id);
    lss_send(0x41, addr->product_code);
    lss_send(0x42, addr->revision);
    lss_send(0x43, addr->serial);

    CanFrame resp;
    if (can_receive_timeout(LSS_RX_ID, &resp, timeout_ms) < 0)
        return -1;  /* timeout */

    return (resp.data[0] == 0x44) ? 0 : -1;
}

/**
 * Switch all LSS-configured nodes back to LSS Waiting (mode=0).
 */
void lss_switch_mode_global_waiting(void)
{
    lss_send(0x04, 0);  /* CS 0x04, mode byte = 0 */
}
```

### 3.5 Triggering Bootloader Entry via LSS

After selecting a node, the master can send a vendor-specific LSS command (CS=0x4F,
commonly defined by the device manufacturer) or write to a proprietary OD entry to
instruct the node to reset into its bootloader personality before the SDO download begins.

```c
/* Vendor-specific: tell node to enter bootloader on next reset */
void lss_enter_bootloader(void)
{
    lss_send(0x4F, 0x424F4F54u);   /* 0x424F4F54 = "BOOT" */
}
```

---

## 4. SDO Block Transfer

### 4.1 Why Block Transfer?

Standard segmented SDO transfers carry **7 bytes of payload per CAN frame**. For a 256 KB
firmware image at 500 kbit/s this is acceptable but slow. Block transfer groups up to
**127 sub-blocks of 7 bytes** (889 bytes) into a single acknowledged window, dramatically
reducing round-trip overhead.

### 4.2 Block Download Protocol Overview

```
Master                                  Slave
  |                                       |
  |-- Initiate Block Download (0xC2) ---> |  (index, subindex, file size)
  |<--------- Block Download Response --- |  (ack, blksize)
  |                                       |
  |-- Block segment 1 (seqno=1) --------> |
  |-- Block segment 2 (seqno=2) --------> |
  |   ...                                 |
  |-- Block segment N (seqno=N, last) --> |
  |<--------- Block Ack (ackseq, blksize) |
  |                                       |
  |  (repeat blocks until all data sent)  |
  |                                       |
  |-- End Block Download (0xC1) ---------> |  (CRC, bytes in last segment)
  |<--------- End Block Ack (0xA1) ------- |
  |                                       |
```

Each block segment frame is 8 bytes:

```
Byte 0:  [c][seqno 7 bits]   c=1 → last segment in block
Bytes 1-7: 7 bytes of firmware data
```

### 4.3 SDO Block Download – CAN Frame Layout

```
Initiate Block Download (master -> slave):
+------+------+------+------+------+------+------+------+
|  C2  | idx_lo| idx_hi| sub |  size (4 bytes, LE)      |
+------+------+------+------+------+------+------+------+

Block Download Response (slave -> master):
+------+------+------+------+------+------+------+------+
|  A4  | idx_lo| idx_hi| sub | blksize | 0x00 | 0x00   |
+------+------+------+------+------+------+------+------+

Segment (master -> slave):
+------+------+------+------+------+------+------+------+
|c|seqno| d0  |  d1  |  d2  |  d3  |  d4  |  d5  |  d6 |
+------+------+------+------+------+------+------+------+

Block Ack (slave -> master):
+------+------+------+------+------+------+------+------+
|  A2  |ackseq|blksize| 0x00 | 0x00 | 0x00 | 0x00 | 0x00|
+------+------+------+------+------+------+------+------+

End Block Download (master -> slave):
+------+------+------+------+------+------+------+------+
|  C1  |n|crc_lo|crc_hi| 0x00| 0x00 | 0x00 | 0x00 | 0x00|
+------+------+------+------+------+------+------+------+
  n = number of bytes NOT used in last segment (0-6)

End Block Ack (slave -> master):
+------+------+------+------+------+------+------+------+
|  A1  | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 | 0x00 |
+------+------+------+------+------+------+------+------+
```

---

## 5. Bootloader Architecture

### 5.1 Memory Map Overview

```
 Flash Address Space (example: 512 KB total)
 +------------------------------------------+
 | 0x0800_0000  Bootloader (32 KB)          |
 |              - Minimal CAN driver        |
 |              - LSS slave handler         |
 |              - SDO block download server |
 |              - Flash erase/write         |
 |              - CRC32 verify              |
 |              - Jump-to-app logic         |
 +------------------------------------------+
 | 0x0800_8000  Application Bank A (240 KB) |
 |              (active firmware)           |
 +------------------------------------------+
 | 0x0804_4000  Application Bank B (240 KB) |
 |              (pending / golden image)    |
 +------------------------------------------+
```

### 5.2 Boot Decision Flow

```
                   RESET
                     |
                     v
             +-------+-------+
             |  Bootloader   |
             |  starts up    |
             +-------+-------+
                     |
          +----------+----------+
          |                     |
     Boot pin              No boot pin
     asserted?              asserted
          |                     |
          v                     v
  +-------+-------+    +--------+-------+
  | Stay in boot- |    | Check  magic   |
  | loader, wait  |    | word in SRAM   |
  | for LSS/SDO   |    +--------+-------+
  +---------------+             |
                       +--------+--------+
                       |                 |
                  Magic matches?     No magic
                       |                 |
                       v                 v
                +-----------+    +-------+-------+
                | Stay in   |    | Verify Bank A |
                | bootloader|    | CRC32         |
                +-----------+    +-------+-------+
                                         |
                              +----------+----------+
                              |                     |
                          CRC OK               CRC FAIL
                              |                     |
                              v                     v
                    +---------+-----+     +---------+-----+
                    | Jump to App A |     | Try Golden    |
                    +---------------+     | Image (Bank B)|
                                          +-------+-------+
                                                  |
                                       +----------+----------+
                                       |                     |
                                   CRC OK               CRC FAIL
                                       |                     |
                                       v                     v
                             +---------+-----+    +----------+------+
                             | Restore App A |    | Halt / Signal   |
                             | from Bank B   |    | Fatal Error     |
                             | Jump to App A |    +-----------------+
                             +---------------+
```

---

## 6. Flash Memory Layouts

### 6.1 Single-Bank Layout (simple, no fallback)

```
+------------------+  0x0800_0000
|   Bootloader     |  (protected sector)
+------------------+  0x0800_8000
|                  |
|   Application    |  (erased and reprogrammed)
|   (single copy)  |
|                  |
+------------------+  0x0808_0000
```

Pros: simple, maximum application space.
Cons: power loss during erase/write leaves the device unbootable ("bricked").

### 6.2 Dual-Bank Layout (reliable update)

```
+------------------+  0x0800_0000
|   Bootloader     |
+------------------+  0x0800_8000
|                  |
|   Bank A         |  (currently running application)
|   (active)       |
|                  |
+------------------+  0x0804_4000
|                  |
|   Bank B         |  (download target / golden image)
|   (inactive)     |
|                  |
+------------------+  0x0808_0000
```

Update procedure:
1. Download new firmware into Bank B.
2. Verify Bank B CRC.
3. Set "pending switch" flag in non-volatile config area.
4. Reset; bootloader selects Bank B as new active bank.
5. After successful first run, erase Bank A and copy Bank B → Bank A (or leave Bank B as golden).

### 6.3 Flash Sector / Page Header

Each bank begins with a small metadata header written by the bootloader after a successful
download:

```c
typedef struct __attribute__((packed)) {
    uint32_t magic;         /* 0xCAFEBABE – marks valid image */
    uint32_t version;       /* firmware version BCD */
    uint32_t image_size;    /* bytes of application code */
    uint32_t crc32;         /* CRC32 of image_size bytes following header */
    uint8_t  flags;         /* bit0=active, bit1=golden, bit2=verified */
    uint8_t  reserved[3];
} FwHeader;
```

---

## 7. Dual-Bank and Golden-Image Strategies

### 7.1 Dual-Bank Update Sequence

```
  Time -->

  Bank A [======= v1.0 RUNNING =======][=== erased ===][=== v1.1 copied ===]
  Bank B [   empty   ][==== v1.1 DOWNLOAD ====][=== v1.1 GOLDEN ===]
                         ^                ^
                    SDO Block         CRC verify
                    Transfer          passes
```

After confirming Bank B is good, the bootloader:
- Marks Bank B header `flags |= FLAG_GOLDEN`.
- Marks Bank A header as inactive.
- Swaps active-bank pointer in a small NV config page.

On the **next reset** the bootloader reads the config page, sees Bank B is active, and
jumps to the vector table at the start of Bank B.

### 7.2 Golden Image Rollback

A golden image is the last known-good firmware permanently kept in one bank as a
safety net. If the newly activated image fails its CRC (or if the application sets a
"boot failed" flag in SRAM), the bootloader falls back to the golden image.

```
                    Application sets
                    BOOT_FAIL flag
                    in SRAM before
                    watchdog expires
                         |
                         v
               +---------+---------+
               |   Bootloader on   |
               |   watchdog reset  |
               +---------+---------+
                         |
                  -------+-------
                  |             |
             BOOT_FAIL      Normal boot
              detected           |
                  |              v
                  v         Jump to active bank
         +--------+------+
         | Restore golden |
         | image to       |
         | active bank    |
         +--------+------+
                  |
                  v
            Jump to golden
            application
```

### 7.3 C Implementation – Bank Selection

```c
#define BANK_A_BASE    0x08008000UL
#define BANK_B_BASE    0x08044000UL
#define BANK_SIZE      0x0003C000UL   /* 240 KB */
#define FW_MAGIC       0xCAFEBABEUL
#define FLAG_ACTIVE    (1u << 0)
#define FLAG_GOLDEN    (1u << 1)
#define FLAG_VERIFIED  (1u << 2)

typedef struct __attribute__((packed)) {
    uint32_t magic;
    uint32_t version;
    uint32_t image_size;
    uint32_t crc32;
    uint8_t  flags;
    uint8_t  reserved[3];
} FwHeader;

static const FwHeader *header_at(uint32_t base)
{
    return (const FwHeader *)base;
}

static bool header_valid(const FwHeader *h)
{
    return (h->magic == FW_MAGIC) &&
           (h->image_size > 0) &&
           (h->image_size <= BANK_SIZE - sizeof(FwHeader));
}

/**
 * Select the bank to boot from.
 * Returns base address of chosen bank, or 0 if no bootable bank found.
 */
uint32_t bootloader_select_bank(void)
{
    const FwHeader *ha = header_at(BANK_A_BASE);
    const FwHeader *hb = header_at(BANK_B_BASE);

    bool a_valid = header_valid(ha) && (ha->flags & FLAG_VERIFIED);
    bool b_valid = header_valid(hb) && (hb->flags & FLAG_VERIFIED);

    /* Prefer whichever bank is flagged active */
    if (a_valid && (ha->flags & FLAG_ACTIVE))
        return BANK_A_BASE;
    if (b_valid && (hb->flags & FLAG_ACTIVE))
        return BANK_B_BASE;

    /* Fallback: prefer golden */
    if (b_valid && (hb->flags & FLAG_GOLDEN))
        return BANK_B_BASE;
    if (a_valid && (ha->flags & FLAG_GOLDEN))
        return BANK_A_BASE;

    return 0;   /* no bootable image */
}
```

---

## 8. Transfer Integrity – CRC32

### 8.1 Why CRC32?

CRC32 (the same polynomial used in Ethernet/ZIP) detects all single-bit errors, all
double-bit errors (for messages shorter than 2^32 bits), all odd numbers of errors, and
all burst errors shorter than 32 bits. It is a mandatory check before activating any
downloaded firmware.

Many ARM Cortex-M microcontrollers include a hardware CRC unit; the examples below show
both a software fallback and hardware acceleration.

### 8.2 Software CRC32 (table-driven, PKZIP polynomial)

```c
#include <stdint.h>
#include <stddef.h>

static const uint32_t crc32_table[256] = {
    /* Generated with polynomial 0xEDB88320 (reflected 0x04C11DB7) */
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA,
    0x076DC419, 0x706AF48F, 0xE963A535, 0x9E6495A3,
    /* ... (full 256-entry table omitted for brevity; generate at compile time) */
};

/**
 * Compute CRC32 over a buffer.
 * Call with crc=0xFFFFFFFF for first block; pass previous return value
 * for subsequent blocks. XOR result with 0xFFFFFFFF to finalise.
 */
uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    while (len--) {
        crc = (crc >> 8) ^ crc32_table[(crc ^ *buf++) & 0xFFu];
    }
    return crc;
}

uint32_t crc32_finalise(uint32_t crc)
{
    return crc ^ 0xFFFFFFFFUL;
}

/* Convenience: compute CRC32 of an entire image */
uint32_t crc32_image(const uint8_t *data, size_t size)
{
    return crc32_finalise(crc32_update(0xFFFFFFFFUL, data, size));
}
```

### 8.3 Hardware CRC32 (STM32 example)

```c
#include "stm32f4xx.h"

void hw_crc32_init(void)
{
    RCC->AHB1ENR |= RCC_AHB1ENR_CRCEN;
    CRC->CR = CRC_CR_RESET;   /* reset to 0xFFFFFFFF */
}

uint32_t hw_crc32_compute(const uint32_t *data, size_t word_count)
{
    CRC->CR = CRC_CR_RESET;
    while (word_count--) {
        CRC->DR = *data++;    /* feed 32-bit words */
    }
    return CRC->DR;           /* STM32 CRC unit uses ISO 3309 polynomial */
}
```

**Note:** The STM32 hardware CRC unit uses a non-reflected polynomial (0x04C11DB7)
without output XOR. Ensure your master-side CRC tool uses the same variant to avoid
mismatches. Many projects wrap the hardware unit to match the standard PKZIP convention.

### 8.4 CRC Verification Before Bank Activation

```c
bool verify_firmware_crc(uint32_t bank_base)
{
    const FwHeader *h = (const FwHeader *)bank_base;

    if (!header_valid(h))
        return false;

    const uint8_t *image_start = (const uint8_t *)(bank_base + sizeof(FwHeader));
    uint32_t computed = crc32_image(image_start, h->image_size);

    return (computed == h->crc32);
}
```

---

## 9. Flash-Verify-Execute Flow

### 9.1 Full Sequence Diagram

```
MASTER (PC tool / PLC)              SLAVE (bootloader node)
      |                                      |
      |--- NMT: Enter Pre-Operational -----> |
      |--- LSS: Switch Mode Selective -----> |  (select by Vendor+Product+Rev+Serial)
      |<-- LSS: Identify Slave Response ---- |
      |--- LSS: Enter Bootloader (vendor) -> |
      |--- NMT: Reset Node ----------------> |
      |                                      | (node resets into bootloader)
      |   (wait for node boot-up message)    |
      |<-- NMT Boot-up (0x700 + NodeID) ---- |
      |                                      |
      |--- SDO Write 0x1F51:01 = 0 --------> |  Stop program / prep download
      |<-- SDO Response OK --------------- - |
      |--- SDO Write 0x1F51:01 = 3 --------> |  Clear program (erase flash)
      |<-- SDO Response OK --------------- - |
      |                                      | (flash erase in progress, ~100ms/sector)
      |--- SDO Block Download 0x1F50:01 ---> |  Initiate, pass total image size
      |<-- Block Download Response --------- |  (slave sends blksize, e.g., 127)
      |                                      |
      |  [send blocks of 127 x 7 bytes]      |
      |--- Block 1 (seqno 1..127) ---------> |
      |<-- Block Ack (ackseq=127) ---------- |
      |--- Block 2 (seqno 1..127) ---------> |
      |<-- Block Ack (ackseq=127) ---------- |
      |   ... (repeat for all blocks)        |
      |--- Last partial block ------------> |
      |<-- Block Ack ---------------------- |
      |                                      |
      |--- End Block Download (CRC32) -----> |
      |<-- End Block Ack ------------------ -|
      |                                      | (node verifies CRC32 internally)
      |--- SDO Read  0x1F56:01 (checksum) -> |  Read back CRC stored by node
      |<-- SDO Response: CRC32 value ------- |
      |                                      |
      | (master verifies CRC matches)        |
      |                                      |
      |--- SDO Write 0x1F51:01 = 1 --------> |  Start program (jump to app)
      |<-- SDO Response OK ---------------- -|
      |                                      |
      |   (node resets / jumps to new app)   |
      |<-- NMT Boot-up (new app) ----------- |
      |                                      |
      |  UPDATE COMPLETE                     |
```

### 9.2 Jump-to-Application in Bootloader (ARM Cortex-M)

```c
typedef void (*AppEntry)(void);

/**
 * Validate and jump to application at given base address.
 * The vector table starts at base_addr; first word is initial MSP,
 * second word is Reset_Handler address.
 */
void jump_to_application(uint32_t base_addr)
{
    /* Sanity check: initial stack pointer should point into RAM */
    uint32_t sp = *(uint32_t *)(base_addr + 0x00);
    uint32_t pc = *(uint32_t *)(base_addr + 0x04);

    if ((sp < 0x20000000UL) || (sp > 0x20080000UL))
        return;   /* invalid stack pointer */
    if ((pc & 1u) == 0)
        return;   /* not Thumb address */

    /* Disable all interrupts, reset peripherals if needed */
    __disable_irq();

    /* Relocate vector table */
    SCB->VTOR = base_addr;

    /* Set stack pointer and jump */
    __set_MSP(sp);
    AppEntry entry = (AppEntry)(pc);
    entry();

    /* Should never reach here */
    while (1) {}
}
```

---

## 10. C/C++ Implementation Examples

### 10.1 SDO Block Download – Master Side (C++)

```cpp
#include <cstdint>
#include <cstring>
#include <vector>
#include <stdexcept>
#include "can_driver.h"

class SdoBlockDownloader {
public:
    static constexpr uint16_t SDO_RX_BASE = 0x600u; // master TX: 0x600 + nodeId
    static constexpr uint16_t SDO_TX_BASE = 0x580u; // master RX: 0x580 + nodeId

    SdoBlockDownloader(uint8_t node_id, uint32_t timeout_ms = 1000)
        : node_id_(node_id), timeout_ms_(timeout_ms) {}

    /**
     * Perform a full SDO block download to index:subindex.
     * data = firmware image bytes, size = byte count.
     * Returns true on success.
     */
    bool download(uint16_t index, uint8_t subindex,
                  const uint8_t *data, uint32_t size)
    {
        if (!initiate(index, subindex, size))
            return false;

        uint32_t offset = 0;
        while (offset < size) {
            uint8_t blksize = 0;
            if (!send_block(data, size, offset, blksize))
                return false;
            offset += blksize * 7u;
        }

        return end_transfer(data, size);
    }

private:
    uint8_t  node_id_;
    uint32_t timeout_ms_;
    uint8_t  blksize_ = 127;   /* negotiated with slave */

    bool initiate(uint16_t index, uint8_t subindex, uint32_t total_size)
    {
        uint8_t frame[8] = {};
        frame[0] = 0xC2u;                      /* initiate block download, size indicated, CRC supported */
        frame[1] = (uint8_t)(index & 0xFFu);
        frame[2] = (uint8_t)(index >> 8);
        frame[3] = subindex;
        memcpy(&frame[4], &total_size, 4);

        can_send(SDO_RX_BASE + node_id_, frame, 8);

        uint8_t resp[8];
        if (!sdo_receive(resp))
            return false;

        if (resp[0] != 0xA4u)
            return false;  /* not a block download response */

        blksize_ = resp[4];   /* slave-requested block size */
        return true;
    }

    bool send_block(const uint8_t *data, uint32_t total,
                    uint32_t offset, uint8_t &segs_sent)
    {
        segs_sent = 0;
        for (uint8_t seq = 1; seq <= blksize_; ++seq) {
            uint32_t pos = offset + (seq - 1u) * 7u;
            bool last_seg = (pos + 7u >= total);
            bool last_blk_seg = (seq == blksize_) || last_seg;

            uint8_t frame[8] = {};
            frame[0] = (uint8_t)(seq | (last_blk_seg ? 0x80u : 0x00u));
            uint32_t remain = (total > pos) ? (total - pos) : 0u;
            uint32_t chunk  = (remain > 7u) ? 7u : remain;
            memcpy(&frame[1], &data[pos], chunk);

            can_send(SDO_RX_BASE + node_id_, frame, 8);
            ++segs_sent;

            if (last_seg)
                break;
        }

        /* Wait for block ack */
        uint8_t resp[8];
        if (!sdo_receive(resp))
            return false;

        if (resp[0] != 0xA2u)
            return false;

        blksize_ = resp[2];   /* slave may adjust block size */
        return true;
    }

    bool end_transfer(const uint8_t *data, uint32_t size)
    {
        uint32_t crc = crc32_image(data, size);
        uint32_t last_bytes = size % 7u;
        uint8_t n = (last_bytes == 0) ? 0u : (uint8_t)(7u - last_bytes);

        uint8_t frame[8] = {};
        frame[0] = (uint8_t)(0xC1u | (n << 2));
        memcpy(&frame[1], &crc, 2);   /* low 16 bits of CRC in end frame */

        can_send(SDO_RX_BASE + node_id_, frame, 8);

        uint8_t resp[8];
        if (!sdo_receive(resp))
            return false;

        return (resp[0] == 0xA1u);
    }

    bool sdo_receive(uint8_t resp[8])
    {
        CanFrame f;
        if (can_receive_timeout(SDO_TX_BASE + node_id_, &f, timeout_ms_) < 0)
            return false;
        memcpy(resp, f.data, 8);
        return true;
    }
};
```

### 10.2 SDO Block Download – Slave Side (C, bootloader)

```c
/* ---- Bootloader SDO Block Transfer Server ---- */

#include <stdint.h>
#include <string.h>
#include "flash_driver.h"   /* flash_erase_sector(), flash_write_page() */
#include "can_driver.h"
#include "crc32.h"

#define SDO_RX_ID(nid)  (0x600u + (nid))
#define SDO_TX_ID(nid)  (0x580u + (nid))
#define BLKSIZE         127u

typedef enum {
    BLK_IDLE,
    BLK_INITIATED,
    BLK_RECEIVING,
    BLK_ENDED
} BlkState;

static struct {
    BlkState state;
    uint32_t total_size;
    uint32_t received;
    uint8_t  expected_seqno;
    uint8_t  last_ackseq;
    uint32_t write_addr;
    uint8_t  page_buf[512];
    uint32_t page_buf_fill;
    uint32_t crc_running;
} blk;

static uint8_t node_id = 1u;

static void sdo_send(const uint8_t *data)
{
    can_send(SDO_TX_ID(node_id), data, 8);
}

static void blk_send_response(void)
{
    uint8_t resp[8] = {0xA4u, 0, 0, 0, BLKSIZE, 0, 0, 0};
    /* echo index/subindex from initiate */
    sdo_send(resp);
}

static void blk_send_ack(void)
{
    uint8_t resp[8] = {0xA2u, blk.last_ackseq, BLKSIZE, 0, 0, 0, 0, 0};
    sdo_send(resp);
}

static void blk_send_end_ack(void)
{
    uint8_t resp[8] = {0xA1u, 0, 0, 0, 0, 0, 0, 0};
    sdo_send(resp);
}

static void flush_page_buf(void)
{
    if (blk.page_buf_fill > 0) {
        flash_write_page(blk.write_addr, blk.page_buf, blk.page_buf_fill);
        blk.write_addr   += blk.page_buf_fill;
        blk.page_buf_fill = 0;
    }
}

static void write_bytes(const uint8_t *data, uint32_t len)
{
    blk.crc_running = crc32_update(blk.crc_running, data, len);
    while (len > 0) {
        uint32_t space = sizeof(blk.page_buf) - blk.page_buf_fill;
        uint32_t chunk = (len < space) ? len : space;
        memcpy(blk.page_buf + blk.page_buf_fill, data, chunk);
        blk.page_buf_fill += chunk;
        data += chunk;
        len  -= chunk;
        if (blk.page_buf_fill == sizeof(blk.page_buf))
            flush_page_buf();
    }
}

/**
 * Call this from the CAN RX interrupt / task when a frame arrives
 * addressed to this node's SDO receive COB-ID.
 */
void sdo_block_process_frame(const uint8_t frame[8])
{
    uint8_t cs = frame[0];

    if (blk.state == BLK_IDLE) {
        if ((cs & 0xE2u) == 0xC2u) {  /* initiate block download */
            uint16_t index;
            memcpy(&index, &frame[1], 2);
            if (index == 0x1F50u && frame[3] == 1u) {
                memcpy(&blk.total_size, &frame[4], 4);
                blk.received      = 0;
                blk.expected_seqno = 1;
                blk.last_ackseq    = 0;
                blk.write_addr     = APPLICATION_BASE;
                blk.page_buf_fill  = 0;
                blk.crc_running    = 0xFFFFFFFFUL;
                blk.state          = BLK_INITIATED;
                blk_send_response();
            }
        }
        return;
    }

    if (blk.state == BLK_INITIATED || blk.state == BLK_RECEIVING) {
        blk.state = BLK_RECEIVING;
        uint8_t seqno  = cs & 0x7Fu;
        bool    is_last = (cs & 0x80u) != 0;

        if (seqno == blk.expected_seqno) {
            uint32_t remain  = blk.total_size - blk.received;
            uint32_t chunk   = (remain > 7u) ? 7u : remain;
            write_bytes(&frame[1], chunk);
            blk.received     += chunk;
            blk.last_ackseq   = seqno;
            ++blk.expected_seqno;
        }

        if (is_last) {
            flush_page_buf();
            blk_send_ack();
            blk.expected_seqno = 1;
        } else if (seqno == BLKSIZE) {
            blk_send_ack();
            blk.expected_seqno = 1;
        }
        return;
    }

    /* End block download */
    if (blk.state == BLK_RECEIVING && (cs & 0xE3u) == 0xC1u) {
        uint32_t received_crc;
        memcpy(&received_crc, &frame[1], 2);  /* low 16 bits only in CiA 301 */
        uint32_t computed = crc32_finalise(blk.crc_running);

        if ((computed & 0xFFFFu) == (received_crc & 0xFFFFu)) {
            /* Write header to flash marking image valid */
            write_firmware_header(APPLICATION_BASE, blk.total_size, computed);
            blk.state = BLK_ENDED;
            blk_send_end_ack();
        } else {
            /* Abort: CRC mismatch */
            uint8_t abort[8] = {0x80u, 0x50u, 0x1Fu, 0x01u,
                                 0x04u, 0x00u, 0x04u, 0x05u};
            sdo_send(abort);
            blk.state = BLK_IDLE;
        }
    }
}
```

### 10.3 Program Control (0x1F51) Handler

```c
void program_control_write(uint8_t value)
{
    switch (value) {
    case 0:   /* Stop / prepare */
        /* Nothing to do in bootloader */
        break;

    case 1:   /* Start program */
        if (verify_firmware_crc(BANK_A_BASE)) {
            jump_to_application(BANK_A_BASE + sizeof(FwHeader));
        }
        break;

    case 2:   /* Reset application */
        NVIC_SystemReset();
        break;

    case 3:   /* Clear program (erase flash) */
        flash_erase_range(APPLICATION_BASE, APPLICATION_SIZE);
        break;

    default:
        break;
    }
}
```

---

## 11. Production Programming Throughput Calculations

### 11.1 Variables and Assumptions

```
Symbol    Value     Description
------    -----     --------------------------
BR        500 kbit/s  CAN baud rate
BPF       8 bytes   Payload per CAN frame
IFG       0         Inter-frame gap (min)
SOF+ID+   47 bits   CAN 2.0B overhead per data frame (11-bit ID, worst case
 DLC+CRC            including bit stuffing allowance ~+25%)
         ~60 bits   Practical frame size at 500 kbit/s (with stuffing)
t_frame   120 µs    Time per 8-byte frame  (60 bits / 500 kbit)

Firmware  256 KB    = 262,144 bytes
```

### 11.2 SDO Segmented Transfer (baseline)

In segmented transfer, every 7-byte segment requires a request frame and a response:

```
Segments   = ceil(262144 / 7) = 37,449
Frames     = 2 × 37,449 = 74,898   (each segment: 1 master + 1 slave)
Time       = 74,898 × 120 µs = 8.99 seconds ≈ 9 s
```

### 11.3 SDO Block Transfer

Block transfer eliminates the per-segment round-trip. Only one ACK per block of 127
segments:

```
Segments   = 37,449
Blocks     = ceil(37,449 / 127) = 295
Slave ACK frames = 295
Master segment frames = 37,449
Total frames = 37,449 + 295 + 2 (initiate) + 2 (end) = 37,748
Time       = 37,748 × 120 µs = 4.53 seconds ≈ 4.5 s
```

**Block transfer is ~2× faster than segmented for this image size.**

### 11.4 Throughput vs. Block Size

```
Block Size   Overhead Frames  Total Frames  Time (256 KB)
(segments)   (ACKs)                         @ 500 kbit/s
----------   ---------------  ------------  -------------
      1       37,449           74,900        8.99 s   (= segmented)
     16        2,341           39,792        4.78 s
     63          595           38,046        4.57 s
    127          295           37,748        4.53 s   (max per CiA 301)
```

### 11.5 Baud Rate Impact

```
Baud Rate    Frame Time   256 KB Transfer (block, blksize=127)
---------    ----------   --------------------------------------
 125 kbit/s    480 µs      18.1 s
 250 kbit/s    240 µs       9.1 s
 500 kbit/s    120 µs       4.5 s
1000 kbit/s     60 µs       2.3 s
```

### 11.6 Flash Erase Time Must Be Accounted For

Flash erase is performed by the node internally and does not consume CAN bandwidth, but
it adds dead time the master must wait for (no SDO response until erase completes):

```
STM32F4 typical erase times:
  16 KB sector:   250 ms
  64 KB sector:   800 ms
 128 KB sector: 2,000 ms

For 256 KB application = 2 × 128 KB sectors:
  Erase time ≈ 4 s   (worst case: 2 × 2,000 ms)

Total programming time (500 kbit/s, blksize=127):
  Erase:    4.0 s
  Download: 4.5 s
  Verify:   0.1 s  (CRC, minimal bus traffic)
  -------- ------
  Total:   ~8.6 s per node
```

### 11.7 Multi-Node Production Line

For a production line updating N nodes in parallel (simultaneous SDO downloads to
different Node-IDs):

```
Each node occupies its own unique COB-IDs, so simultaneous block downloads are
possible on the same physical bus IF the bus load stays below ~70-80%.

Bus load per node = 37,748 frames × 8 bytes × 8 bits / (4.5 s × 500,000 bit/s)
                  = 37,748 × 64 / 2,250,000
                  ≈ 1.07 Mbit / 2.25 Mbit ≈ 47.6% per node

Two simultaneous nodes → ~95% bus load (marginal, risk of errors).
Recommended: update nodes sequentially for robustness, or
             batch no more than 1 node per 500 kbit/s segment.
```

---

## 12. Error Handling and Abort Codes

If a transfer error occurs the slave sends an SDO Abort with a 32-bit abort code:

```c
/* SDO Abort Transfer */
typedef struct {
    uint8_t  cs;         /* 0x80 */
    uint8_t  index_lo;
    uint8_t  index_hi;
    uint8_t  subindex;
    uint32_t abort_code; /* little-endian */
} SdoAbort;

/* Common abort codes during firmware download */
#define SDO_ABORT_CRC_ERROR          0x05040005UL  /* CRC error */
#define SDO_ABORT_HARDWARE_FAILURE   0x06060000UL  /* flash write error */
#define SDO_ABORT_OUT_OF_MEMORY      0x05040005UL  /* image too large */
#define SDO_ABORT_ACCESS_DENIED      0x06010000UL  /* write not permitted */
#define SDO_ABORT_GENERAL_ERROR      0x08000000UL  /* generic */
```

Recovery procedure:

```
1. Master receives SDO Abort (CS=0x80).
2. Master reads abort code (bytes 4-7).
3. Master retries from beginning:
   a. Send NMT Reset / LSS re-select.
   b. Re-issue Program Control = 3 (erase).
   c. Restart block download.
4. If three consecutive retries fail → flag node for manual inspection.
```

---

## 13. Summary

Firmware update over CANopen combines two complementary protocols into a robust,
standardised, and field-proven update path:

**LSS (CiA 305)** identifies and addresses nodes by their unique 128-bit hardware identity
without requiring prior Node-ID assignment. It allows a master to select one specific
device in a populated network and instruct it to enter bootloader mode before any data
transfer begins.

**SDO Block Transfer (CiA 301)** eliminates the per-segment round-trip overhead of
standard segmented transfers, achieving roughly twice the throughput. For a 256 KB image
at 500 kbit/s, block download completes in approximately 4.5 seconds, with flash erase
adding another 2–4 seconds.

**Bootloader design** must handle power-loss safety. The golden-image and dual-bank
strategies ensure there is always a known-good firmware to fall back to: new firmware is
written into an inactive bank, verified by CRC32, and only then activated by updating
a boot-selection flag. On failure the bootloader silently falls back to the golden image.

**CRC32** (polynomial 0xEDB88320) provides strong transfer integrity. It is computed
incrementally during the block download and checked twice: by the slave immediately after
the End Block Download frame, and optionally by the master by reading back object 0x1F56.

**Throughput calculations** must account for both bus time (proportional to image size and
block size) and flash erase time (independent of bus speed, can dominate for large flash
sectors). At 500 kbit/s updating a 256 KB node takes roughly 8–10 seconds end-to-end;
at 1 Mbit/s this halves to 4–5 seconds. Simultaneous multi-node updates are possible but
require careful bus-load budgeting; sequential updates are safer on dense networks.

---

*Reference: CiA 301 (CANopen application layer), CiA 302 (bootloader profile),
CiA 305 (LSS), CiA 302-3 (program download).*