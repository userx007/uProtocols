# 31. Conformance & Interoperability Testing

> **CANopen Series — Chapter 31**
> Covers: CiA conformance test plans, mandatory/optional object verification,
> SDO/PDO timing tests, NMT transition stress tests, vendor interoperability
> workshops, and preparing a Certificate of Conformance.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [CiA Conformance Framework](#2-cia-conformance-framework)
3. [The Electronic Data Sheet (EDS) as Test Anchor](#3-the-electronic-data-sheet-eds-as-test-anchor)
4. [Mandatory vs Optional Object Verification](#4-mandatory-vs-optional-object-verification)
5. [SDO Conformance and Timing Tests](#5-sdo-conformance-and-timing-tests)
6. [PDO Conformance and Timing Tests](#6-pdo-conformance-and-timing-tests)
7. [NMT Transition Stress Tests](#7-nmt-transition-stress-tests)
8. [EMCY and Error Register Validation](#8-emcy-and-error-register-validation)
9. [Vendor Interoperability Workshops](#9-vendor-interoperability-workshops)
10. [Automated Test Harness in C/C++](#10-automated-test-harness-in-cc)
11. [Preparing a Certificate of Conformance](#11-preparing-a-certificate-of-conformance)
12. [Common Failure Modes and Remediation](#12-common-failure-modes-and-remediation)
13. [Summary](#13-summary)

---

## 1. Introduction

CANopen conformance testing is the formal process by which a device or system
implementation is validated against the specifications published by **CAN in
Automation (CiA)**, primarily:

- **CiA 301** — CANopen Application Layer and Communication Profile
- **CiA 302** — Additional Application Layer Functions
- **CiA 303** — Recommendation for CANopen Indicators and Connectors
- **CiA 4xx/5xx/6xx** — Device profiles (e.g., CiA 401 for I/O modules)

Conformance testing is distinct from functional testing. It answers the
question: *"Does this device behave exactly as the standard mandates?"*, not
merely *"Does this device work in my system?"*

Interoperability testing goes one step further: devices from different vendors
must exchange data, respond to NMT commands, and recover from errors in a
predictable, standard-compliant manner — even when integrated for the first time.

```
  TESTING PYRAMID
  ===============

           /\
          /  \
         / CT \      <- Conformance Testing (CiA / lab)
        /------\
       /        \
      /    IT    \   <- Interoperability Testing (multi-vendor)
     /------------\
    /              \
   /  Functional    \  <- Functional / Integration Testing (system)
  /------------------\
 /   Unit / Module    \  <- Developer unit tests
/______________________\
```

Conformance testing typically occurs **before** interoperability workshops and
both precede system integration.

---

## 2. CiA Conformance Framework

### 2.1 Roles and Bodies

| Body | Role |
|------|------|
| **CiA e.V.** | Publishes specifications; administers conformance programmes |
| **Accredited Test Labs** | Execute formal test plans; issue Certificates of Conformance |
| **Vendor** | Prepares DUT (Device Under Test), EDS, and documentation |
| **System Integrator** | May request interoperability evidence from vendors |

### 2.2 Test Plan Structure

The CiA conformance test plan is organised into **test groups**, each targeting
a protocol layer:

```
  CiA CONFORMANCE TEST PLAN STRUCTURE
  ====================================

  +--------------------------------------------------+
  |                 Test Campaign                    |
  +--------------------------------------------------+
       |           |           |           |
       v           v           v           v
  +--------+  +--------+  +--------+  +--------+
  |  OBJ   |  |  SDO   |  |  PDO   |  |  NMT   |
  | Verify |  | Tests  |  | Tests  |  | Tests  |
  +--------+  +--------+  +--------+  +--------+
       |           |           |           |
       v           v           v           v
  +--------+  +--------+  +--------+  +--------+
  | EMCY   |  | SYNC   |  | TIME   |  | SRDO   |
  | Tests  |  | Tests  |  | Tests  |  | Tests  |
  +--------+  +--------+  +--------+  +--------+
```

Each test group contains:
- **Static checks** — Object Dictionary structure, EDS consistency
- **Behavioural checks** — Protocol reactions, timing bounds
- **Stress checks** — Repeated operations, boundary conditions, error injection

### 2.3 Device Under Test (DUT) Setup

```
  LAB TEST TOPOLOGY
  =================

  +----------------+       CAN Bus (125/250/500/1000 kbit/s)
  |   Test System  |===========================================+
  |                |                                           |
  | +-----------+  |      +---------------------------+        |
  | |Conformance|  |      |     Device Under Test     |        |
  | |  Master   |<========>  (DUT)                    |        |
  | +-----------+  |      |  Node-ID: configurable    |        |
  |                |      |  Baud: all rates tested   |        |
  | +-----------+  |      +---------------------------+        |
  | | CAN       |  |                                           |
  | | Analyser  |<===========================================> |
  | +-----------+  |      (passive monitor — all frames)       |
  |                |                                           |
  | +-----------+  |                                           |
  | | EDS       |  |                                           |
  | | Parser    |  |                                           |
  | +-----------+  |                                           |
  +----------------+
```

The test system acts as a **conformance master** that:
1. Reads the device's EDS to know which objects are supported.
2. Generates test vectors from the EDS.
3. Records all bus traffic for off-line analysis.
4. Compares actual behaviour against expected behaviour per CiA 301.

---

## 3. The Electronic Data Sheet (EDS) as Test Anchor

Every conformance test campaign begins with EDS validation. An invalid EDS
means the test cannot start.

### 3.1 EDS Integrity Checks

```c
/* ---------------------------------------------------------------
 * eds_validator.c
 * Minimal EDS structural validator.
 * Checks section presence and mandatory parameter keys.
 * --------------------------------------------------------------- */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_LINE     256
#define MAX_SECTION   64

typedef struct {
    int file_info_ok;
    int device_info_ok;
    int dummy_usage_ok;
    int comments_ok;
    int objects_ok;
    int error_count;
} EDSValidationResult;

/* Mandatory top-level sections per CiA 306 */
static const char *MANDATORY_SECTIONS[] = {
    "FileInfo",
    "DeviceInfo",
    "DummyUsage",
    "Comments",
    "ObjectLinks",    /* may be absent in older EDS */
    NULL
};

/* Mandatory keys within [FileInfo] */
static const char *FILEINFO_KEYS[] = {
    "FileName",
    "FileVersion",
    "FileRevision",
    "EDSVersion",
    "Description",
    "CreationTime",
    "CreationDate",
    "CreatedBy",
    "ModificationTime",
    "ModificationDate",
    "ModifiedBy",
    NULL
};

static int section_present(FILE *fp, const char *section_name)
{
    char line[MAX_LINE];
    char target[MAX_SECTION + 3];
    snprintf(target, sizeof(target), "[%s]", section_name);

    rewind(fp);
    while (fgets(line, sizeof(line), fp)) {
        /* Strip trailing whitespace */
        char *end = line + strlen(line) - 1;
        while (end > line && isspace((unsigned char)*end))
            *end-- = '\0';
        if (strcmp(line, target) == 0)
            return 1;
    }
    return 0;
}

EDSValidationResult eds_validate(const char *filepath)
{
    EDSValidationResult result = {0};
    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        fprintf(stderr, "[EDS-VAL] Cannot open: %s\n", filepath);
        result.error_count = 1;
        return result;
    }

    /* Check mandatory sections */
    if (section_present(fp, "FileInfo"))  result.file_info_ok   = 1;
    if (section_present(fp, "DeviceInfo")) result.device_info_ok = 1;
    if (section_present(fp, "DummyUsage")) result.dummy_usage_ok = 1;
    if (section_present(fp, "Comments"))   result.comments_ok    = 1;

    if (!result.file_info_ok) {
        fprintf(stderr, "[EDS-VAL] FAIL: Missing [FileInfo] section\n");
        result.error_count++;
    }
    if (!result.device_info_ok) {
        fprintf(stderr, "[EDS-VAL] FAIL: Missing [DeviceInfo] section\n");
        result.error_count++;
    }

    /* Check object count consistency */
    char line[MAX_LINE];
    int obj_count_declared = -1;
    rewind(fp);
    int in_objects_section = 0;
    while (fgets(line, sizeof(line), fp)) {
        if (strncmp(line, "[Objects]", 9) == 0) {
            in_objects_section = 1;
            continue;
        }
        if (in_objects_section && strncmp(line, "SupportedObjects=", 17) == 0) {
            obj_count_declared = atoi(line + 17);
            break;
        }
        if (in_objects_section && line[0] == '[')
            break;
    }

    if (obj_count_declared < 0) {
        fprintf(stderr, "[EDS-VAL] FAIL: SupportedObjects key missing\n");
        result.error_count++;
    } else {
        printf("[EDS-VAL] SupportedObjects declared: %d\n", obj_count_declared);
        result.objects_ok = 1;
    }

    fclose(fp);
    return result;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <device.eds>\n", argv[0]);
        return 1;
    }
    EDSValidationResult r = eds_validate(argv[1]);
    printf("\n=== EDS Validation Summary ===\n");
    printf("  FileInfo    : %s\n", r.file_info_ok   ? "OK" : "MISSING");
    printf("  DeviceInfo  : %s\n", r.device_info_ok ? "OK" : "MISSING");
    printf("  DummyUsage  : %s\n", r.dummy_usage_ok ? "OK" : "MISSING");
    printf("  Objects     : %s\n", r.objects_ok     ? "OK" : "MISSING");
    printf("  Total Errors: %d\n", r.error_count);
    return (r.error_count == 0) ? 0 : 1;
}
```

---

## 4. Mandatory vs Optional Object Verification

### 4.1 Object Classification

CANopen objects fall into three conformance categories:

```
  OBJECT CONFORMANCE CATEGORIES (CiA 301)
  =========================================

  Index Range     | Mandatory (M) / Conditional (C) / Optional (O)
  ----------------+-------------------------------------------------
  0x1000          | M  — Device Type
  0x1001          | M  — Error Register
  0x1002          | O  — Manufacturer Status Register
  0x1003          | C  — Pre-defined Error Field (if EMCY supported)
  0x1005          | M  — COB-ID SYNC
  0x1006          | C  — Communication Cycle Period (if SYNC producer)
  0x1008          | O  — Manufacturer Device Name
  0x1009          | O  — Manufacturer HW Version
  0x100A          | O  — Manufacturer SW Version
  0x100C-100D     | C  — Guard Time / Life Time Factor (if NMT slave)
  0x1010          | C  — Store Parameters (if supported)
  0x1011          | C  — Restore Default Parameters (if supported)
  0x1014          | M  — COB-ID EMCY
  0x1015          | O  — Inhibit Time EMCY
  0x1016          | C  — Consumer Heartbeat Time (if HB consumer)
  0x1017          | C  — Producer Heartbeat Time (if HB producer)
  0x1018          | M  — Identity Object
  0x1019          | O  — Synchronous Counter Overflow Value
  0x1020          | O  — Verify Configuration
  0x1021          | O  — Store EDS
  0x1022          | O  — Store Format
  0x1023-1024     | O  — OS Command / Prompt
  0x1025          | O  — OS Debugger Interface
  0x1026          | O  — OS Prompt
  0x1028          | O  — Emergency Consumer Object
  0x1029          | C  — Error Behaviour (if error handling)
  0x1200-127F     | C  — SDO Server Parameters
  0x1280-12FF     | C  — SDO Client Parameters
  0x1300-13FF     | C  — GFC Parameter
  0x1400-15FF     | C  — Receive PDO Communication Parameters
  0x1600-17FF     | C  — Receive PDO Mapping Parameters
  0x1800-19FF     | C  — Transmit PDO Communication Parameters
  0x1A00-1BFF     | C  — Transmit PDO Mapping Parameters
```

### 4.2 Object Verification Test in C++

```cpp
// ---------------------------------------------------------------
// object_verifier.cpp
// Walks a device's object dictionary over SDO and checks that
// mandatory objects respond correctly.
// ---------------------------------------------------------------
#include <cstdint>
#include <vector>
#include <string>
#include <functional>
#include <cstdio>

// ---- Minimal SDO abstraction (replace with your CAN driver) ----
enum class SDOResult { OK, ABORT, TIMEOUT };

struct SDOResponse {
    SDOResult   result;
    uint32_t    abort_code;
    uint8_t     data[8];
    uint8_t     data_len;
};

// Forward declaration — implement with your CAN stack
SDOResponse sdo_upload(uint8_t node_id, uint16_t index, uint8_t subindex,
                       uint32_t timeout_ms = 500);

// ---- Object under test descriptor ----
struct ObjSpec {
    uint16_t    index;
    uint8_t     subindex;
    const char *name;
    bool        mandatory;          // true = MUST be present
    bool        must_be_writable;   // true = write access required
    uint32_t    expected_abort;     // 0 = expect success
};

// Mandatory objects as per CiA 301 Rev 4.2.0
static const std::vector<ObjSpec> MANDATORY_OBJS = {
    { 0x1000, 0x00, "Device Type",           true,  false, 0 },
    { 0x1001, 0x00, "Error Register",         true,  false, 0 },
    { 0x1005, 0x00, "COB-ID SYNC",            true,  false, 0 },
    { 0x1014, 0x00, "COB-ID EMCY",            true,  false, 0 },
    { 0x1017, 0x00, "Producer HB Time",       true,  false, 0 },
    { 0x1018, 0x00, "Identity: highest sub",  true,  false, 0 },
    { 0x1018, 0x01, "Identity: Vendor ID",    true,  false, 0 },
    { 0x1018, 0x02, "Identity: Product Code", true,  false, 0 },
    { 0x1018, 0x03, "Identity: Revision",     true,  false, 0 },
    { 0x1018, 0x04, "Identity: Serial",       false, false, 0 },
};

struct TestResult {
    const char *name;
    uint16_t    index;
    uint8_t     subindex;
    bool        passed;
    std::string detail;
};

class ObjectVerifier {
public:
    explicit ObjectVerifier(uint8_t node_id) : m_node(node_id) {}

    std::vector<TestResult> run(const std::vector<ObjSpec>& specs) {
        std::vector<TestResult> results;

        for (const auto& s : specs) {
            TestResult tr;
            tr.name     = s.name;
            tr.index    = s.index;
            tr.subindex = s.subindex;

            SDOResponse rsp = sdo_upload(m_node, s.index, s.subindex);

            if (s.expected_abort != 0) {
                // We EXPECT an abort code
                tr.passed = (rsp.result == SDOResult::ABORT &&
                             rsp.abort_code == s.expected_abort);
                if (!tr.passed) {
                    char buf[64];
                    snprintf(buf, sizeof(buf),
                             "Expected abort 0x%08X, got result=%d abort=0x%08X",
                             s.expected_abort,
                             (int)rsp.result,
                             rsp.abort_code);
                    tr.detail = buf;
                } else {
                    tr.detail = "Abort received as expected";
                }
            } else if (rsp.result == SDOResult::OK) {
                tr.passed = true;
                char buf[64];
                snprintf(buf, sizeof(buf), "OK (%u bytes)", rsp.data_len);
                tr.detail = buf;
            } else if (rsp.result == SDOResult::TIMEOUT) {
                tr.passed = !s.mandatory;   // optional missing = warn, not fail
                tr.detail = s.mandatory ? "TIMEOUT (mandatory object missing!)"
                                        : "TIMEOUT (optional, acceptable)";
            } else {
                tr.passed = false;
                char buf[64];
                snprintf(buf, sizeof(buf), "ABORT 0x%08X (unexpected)", rsp.abort_code);
                tr.detail = buf;
            }
            results.push_back(tr);
        }
        return results;
    }

    void print_report(const std::vector<TestResult>& results) const {
        printf("\n");
        printf("  Object Verification Report — Node 0x%02X\n", m_node);
        printf("  %-40s %-8s %-8s %s\n", "Object Name", "Index", "Sub", "Result");
        printf("  %s\n", std::string(80, '-').c_str());
        int passed = 0, failed = 0;
        for (const auto& r : results) {
            printf("  %-40s 0x%04X   0x%02X     %s — %s\n",
                   r.name, r.index, r.subindex,
                   r.passed ? "PASS" : "FAIL",
                   r.detail.c_str());
            r.passed ? ++passed : ++failed;
        }
        printf("  %s\n", std::string(80, '-').c_str());
        printf("  TOTAL: %d passed, %d failed\n\n", passed, failed);
    }

private:
    uint8_t m_node;
};

// ---- Stub implementation (replace with real CAN) ----
SDOResponse sdo_upload(uint8_t node_id, uint16_t index, uint8_t subindex,
                       uint32_t timeout_ms)
{
    (void)node_id; (void)timeout_ms;
    // Simulated: return OK for all mandatory objects
    SDOResponse r{};
    r.result   = SDOResult::OK;
    r.data_len = 4;
    // Fill plausible values
    uint32_t val = ((uint32_t)index << 8) | subindex; // synthetic
    memcpy(r.data, &val, 4);
    return r;
}

int main()
{
    ObjectVerifier verifier(0x01);
    auto results = verifier.run(MANDATORY_OBJS);
    verifier.print_report(results);
    return 0;
}
```

---

## 5. SDO Conformance and Timing Tests

### 5.1 SDO Protocol State Machine

```
  SDO UPLOAD (Expedited) — CONFORMANCE VIEW
  ==========================================

  Initiator (Master)             DUT (Slave)
  ==================             ===========
        |                              |
        |--- Initiate Upload Req ----->|   ccs=2, index, subindex
        |    CAN ID: 0x600 + node_id   |
        |                              |
        |<-- Initiate Upload Resp -----|   scs=2, e=1, s=1, n, data
        |    CAN ID: 0x580 + node_id   |
        |                              |
        |          [DONE]              |

  SDO UPLOAD (Segmented) — CONFORMANCE VIEW
  ==========================================

  Initiator (Master)             DUT (Slave)
  ==================             ===========
        |                              |
        |--- Initiate Upload Req ----->|  ccs=2
        |                              |
        |<-- Initiate Upload Resp -----|  scs=2, e=0, size indicated
        |                              |
        |--- Upload Segment Req ------>|  ccs=3, toggle=0
        |                              |
        |<-- Upload Segment Resp ------|  scs=0, toggle=0, seg-data, c=0
        |                              |
        |--- Upload Segment Req ------>|  ccs=3, toggle=1
        |                              |
        |<-- Upload Segment Resp ------|  scs=0, toggle=1, seg-data, c=1
        |                              |
        |          [DONE]              |

  SDO ABORT SEQUENCE
  ==================

  Initiator (Master)             DUT (Slave)
  ==================             ===========
        |                              |
        |--- Initiate Upload Req ----->|  index/sub = non-existent
        |                              |
        |<-- Abort Transfer -----------|  abort code = 0x06020000
        |    (Object does not exist)   |
        |                              |
```

### 5.2 SDO Timing Test Implementation

CiA 301 mandates that a DUT must complete an SDO response within a **configurable
timeout** (default typically 500 ms in implementations; the conformance test
uses 200 ms for the fast-path). This test measures actual response latency.

```c
/* ---------------------------------------------------------------
 * sdo_timing_test.c
 * Measures SDO round-trip latency for expedited and segmented
 * transfers and verifies they meet CiA timing requirements.
 * --------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Platform monotonic clock in microseconds */
static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + (uint64_t)ts.tv_nsec / 1000ULL;
}

/* ---- Simulated CAN frame (replace with real driver) ---- */
typedef struct {
    uint32_t cob_id;
    uint8_t  dlc;
    uint8_t  data[8];
} CANFrame;

/* Platform-specific: send CAN frame */
static int can_send(const CANFrame *f)
{
    (void)f;
    return 0; /* 0 = success */
}

/* Platform-specific: receive CAN frame with timeout_us */
static int can_recv(CANFrame *f, uint64_t timeout_us)
{
    (void)timeout_us;
    /* Simulate fast response: fill a dummy expedited upload response */
    f->cob_id  = 0x581;           /* node 1 response */
    f->dlc     = 8;
    f->data[0] = 0x43;            /* scs=2, e=1, s=1, n=0 (4 bytes) */
    f->data[1] = 0x18;            /* index low: 0x1018 */
    f->data[2] = 0x10;            /* index high */
    f->data[3] = 0x01;            /* subindex */
    f->data[4] = 0x00;            /* data byte 0 */
    f->data[5] = 0x00;            /* data byte 1 */
    f->data[6] = 0x00;            /* data byte 2 */
    f->data[7] = 0x00;            /* data byte 3 */
    return 0; /* 0 = success */
}

/* SDO timing measurement result */
typedef struct {
    uint16_t index;
    uint8_t  subindex;
    uint64_t latency_us;
    int      timed_out;
    int      abort_received;
    uint32_t abort_code;
} SDOTimingResult;

#define SDO_TIMEOUT_US   500000ULL   /* 500 ms conformance limit */
#define EXPEDITED_LIMIT  200000ULL   /* 200 ms expected fast path */

static SDOTimingResult measure_sdo_upload(uint8_t node_id,
                                          uint16_t index,
                                          uint8_t  subindex)
{
    SDOTimingResult result = {0};
    result.index    = index;
    result.subindex = subindex;

    CANFrame req = {0};
    req.cob_id  = 0x600 + node_id;
    req.dlc     = 8;
    req.data[0] = 0x40;              /* ccs=2: initiate upload request */
    req.data[1] = (uint8_t)(index & 0xFF);
    req.data[2] = (uint8_t)(index >> 8);
    req.data[3] = subindex;

    uint64_t t_send = now_us();
    can_send(&req);

    CANFrame rsp;
    int rc = can_recv(&rsp, SDO_TIMEOUT_US);
    uint64_t t_recv = now_us();

    if (rc != 0) {
        result.timed_out  = 1;
        result.latency_us = SDO_TIMEOUT_US;
        return result;
    }

    result.latency_us = t_recv - t_send;

    /* Check for abort */
    if ((rsp.data[0] & 0xE0) == 0x80) {
        result.abort_received = 1;
        memcpy(&result.abort_code, &rsp.data[4], 4);
    }

    return result;
}

/* Run a batch of SDO timing tests and print a report */
typedef struct { uint16_t index; uint8_t subindex; const char *name; } ObjEntry;

static const ObjEntry TEST_OBJECTS[] = {
    { 0x1000, 0x00, "Device Type"      },
    { 0x1001, 0x00, "Error Register"   },
    { 0x1005, 0x00, "COB-ID SYNC"      },
    { 0x1014, 0x00, "COB-ID EMCY"      },
    { 0x1018, 0x01, "Vendor ID"        },
    { 0x1018, 0x02, "Product Code"     },
    { 0x1018, 0x03, "Revision Number"  },
    { 0x1017, 0x00, "HB Producer Time" },
};
#define N_OBJECTS (sizeof(TEST_OBJECTS)/sizeof(TEST_OBJECTS[0]))

int main(void)
{
    uint8_t node_id = 0x01;
    int repeat = 10;   /* measure each object N times */

    printf("SDO Upload Timing Conformance Test\n");
    printf("====================================\n");
    printf("Node: 0x%02X | Repeats per object: %d\n\n", node_id, repeat);
    printf("%-24s  %8s  %8s  %8s  %s\n",
           "Object", "Min(us)", "Max(us)", "Avg(us)", "Conformance");
    printf("%s\n", "------------------------------------------------------------------------");

    int total_pass = 0, total_fail = 0;

    for (size_t i = 0; i < N_OBJECTS; i++) {
        uint64_t sum = 0, min_lat = UINT64_MAX, max_lat = 0;
        int timeouts = 0, aborts = 0;

        for (int r = 0; r < repeat; r++) {
            SDOTimingResult res = measure_sdo_upload(
                node_id,
                TEST_OBJECTS[i].index,
                TEST_OBJECTS[i].subindex);

            if (res.timed_out)       { timeouts++; continue; }
            if (res.abort_received)  { aborts++;   continue; }

            sum += res.latency_us;
            if (res.latency_us < min_lat) min_lat = res.latency_us;
            if (res.latency_us > max_lat) max_lat = res.latency_us;
        }

        int valid = repeat - timeouts - aborts;
        if (valid == 0) {
            printf("%-24s  %8s  %8s  %8s  FAIL (timeouts=%d aborts=%d)\n",
                   TEST_OBJECTS[i].name, "N/A", "N/A", "N/A", timeouts, aborts);
            total_fail++;
            continue;
        }

        uint64_t avg = sum / (uint64_t)valid;
        int conf_ok  = (max_lat <= SDO_TIMEOUT_US) && (timeouts == 0);

        printf("%-24s  %8llu  %8llu  %8llu  %s\n",
               TEST_OBJECTS[i].name,
               (unsigned long long)min_lat,
               (unsigned long long)max_lat,
               (unsigned long long)avg,
               conf_ok ? "PASS" : "FAIL");

        conf_ok ? total_pass++ : total_fail++;
    }

    printf("%s\n", "------------------------------------------------------------------------");
    printf("RESULT: %d PASS, %d FAIL\n", total_pass, total_fail);
    return (total_fail == 0) ? 0 : 1;
}
```

### 5.3 SDO Abort Code Conformance

Each SDO abort code must be correctly generated by the DUT. Key codes tested:

```
  SDO ABORT CODE CONFORMANCE TABLE
  ==================================

  Abort Code    | Condition Tested
  --------------|--------------------------------------------------
  0x05030000    | Toggle bit mismatch (segmented transfer)
  0x05040000    | SDO command specifier not valid
  0x05040001    | Client/server command specifier invalid
  0x05040002    | Invalid block size (block mode only)
  0x05040003    | Invalid sequence number (block mode only)
  0x05040004    | CRC error (block mode only)
  0x05040005    | Out of memory
  0x06010000    | Unsupported access
  0x06010001    | Attempt to read a write-only object
  0x06010002    | Attempt to write a read-only object
  0x06020000    | Object does not exist
  0x06040041    | Object cannot be mapped to PDO
  0x06040042    | PDO length exceeded
  0x06040043    | Parameter incompatible
  0x06040047    | Internal device incompatibility
  0x06060000    | Access failed due to hardware error
  0x06070010    | Data type/length mismatch
  0x06070012    | Data type mismatch, length too high
  0x06070013    | Data type mismatch, length too low
  0x06090011    | Sub-index does not exist
  0x06090030    | Value range of parameter exceeded
  0x06090031    | Value too high
  0x06090032    | Value too low
  0x06090036    | Max less than min
  0x060A0023    | Resource not available
  0x08000000    | General error
  0x08000020    | Data cannot be transferred or stored
  0x08000021    | Local control prevents transfer
  0x08000022    | Device state prevents transfer
  0x08000023    | OD dynamic generation failure
  0x08000024    | No data available
```

---

## 6. PDO Conformance and Timing Tests

### 6.1 PDO Timing Requirements

```
  PDO TRANSMISSION TIMING (CiA 301)
  ====================================

  Synchronous PDO (transmission type 1–240):
  -------------------------------------------
  SYNC #N                            SYNC #(N+1)
    |                                     |
    +--[ PDO sent after Nth SYNC ]--------+
    |<-------- Sync Cycle Period -------->|
    |                                     |
    PDO MUST be sent:                     |
    - After SYNC reception                |
    - Before next SYNC (or within inhibit)|

  Event-driven PDO (transmission type 254/255):
  ----------------------------------------------
  Event                 Inhibit                Next transmission
    |                   Time                   allowed
    +---[ PDO sent ]----+----------------------+----[ PDO sent ]--
                        |<----- >=InhibitTime->|

  RTR PDO (transmission type 252/253):
  -------------------------------------
  RTR received          Response Window
    |                       |
    +---[ RTR Frame ]-------+---[ PDO Response ]
                            |<--- <= 500 ms ---->
```

### 6.2 PDO Configuration Verification

```cpp
// ---------------------------------------------------------------
// pdo_conformance.cpp
// Verifies PDO mapping and communication parameter objects
// and checks that PDO messages are transmitted within spec.
// ---------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <functional>

// Simulated CAN + SDO layer — replace with your platform driver
struct CANMsg {
    uint32_t cob_id;
    uint8_t  dlc;
    uint8_t  data[8];
    uint64_t timestamp_us;  // monotonic
};

// ---- PDO descriptor read from OD via SDO ----
struct PDOCommParam {
    uint32_t cob_id;          // 0x1400+n subindex 1
    uint8_t  transmission_type; // subindex 2
    uint16_t inhibit_time;    // subindex 3 (x100 us)
    uint16_t event_timer;     // subindex 5 (ms), TPDO only
};

struct PDOMapEntry {
    uint16_t index;
    uint8_t  subindex;
    uint8_t  length_bits;
};

struct PDOMapping {
    uint8_t            n_mapped;   // 0x1A00+n subindex 0
    PDOMapEntry        entries[8]; // subindex 1..8
};

// ---- Conformance test result ----
struct PDOTestResult {
    uint8_t  pdo_num;      // 0-based
    bool     cob_id_ok;
    bool     trans_type_ok;
    bool     mapping_len_ok;  // total bits <= 64
    bool     timing_ok;
    uint64_t observed_interval_us;
    uint32_t expected_interval_us;
    bool     inhibit_respected;
};

// Simulated SDO reads — returns canned data
static uint32_t sim_read_u32(uint8_t /*node*/, uint16_t index, uint8_t sub)
{
    // 0x1800 TPDO0 communication params
    if (index == 0x1800 && sub == 0x01) return 0x181;   // COB-ID = 0x181
    if (index == 0x1800 && sub == 0x02) return 255;     // event-driven
    if (index == 0x1800 && sub == 0x03) return 100;     // inhibit = 10 ms
    if (index == 0x1800 && sub == 0x05) return 100;     // event timer = 100 ms
    if (index == 0x1A00 && sub == 0x00) return 2;       // 2 mapped objects
    if (index == 0x1A00 && sub == 0x01) return 0x60000108; // 0x6000:1 8-bit
    if (index == 0x1A00 && sub == 0x02) return 0x60000210; // 0x6000:2 16-bit
    return 0;
}

static PDOCommParam read_tpdo_comm(uint8_t node, uint8_t pdo_num)
{
    uint16_t base = 0x1800 + pdo_num;
    PDOCommParam p;
    p.cob_id           = sim_read_u32(node, base, 1);
    p.transmission_type= (uint8_t)sim_read_u32(node, base, 2);
    p.inhibit_time     = (uint16_t)sim_read_u32(node, base, 3);
    p.event_timer      = (uint16_t)sim_read_u32(node, base, 5);
    return p;
}

static PDOMapping read_tpdo_mapping(uint8_t node, uint8_t pdo_num)
{
    uint16_t base = 0x1A00 + pdo_num;
    PDOMapping m;
    m.n_mapped = (uint8_t)sim_read_u32(node, base, 0);
    uint8_t total_bits = 0;
    for (uint8_t i = 0; i < m.n_mapped && i < 8; i++) {
        uint32_t entry = sim_read_u32(node, base, i + 1);
        m.entries[i].index      = (uint16_t)(entry >> 16);
        m.entries[i].subindex   = (uint8_t)((entry >> 8) & 0xFF);
        m.entries[i].length_bits= (uint8_t)(entry & 0xFF);
        total_bits += m.entries[i].length_bits;
    }
    return m;
}

// Simulate receiving N PDO messages from bus, return timestamps
static std::vector<uint64_t> capture_pdo_timestamps(uint32_t cob_id,
                                                     int count,
                                                     uint64_t window_us)
{
    // Simulate: event-driven PDO at ~100ms intervals
    std::vector<uint64_t> ts;
    uint64_t base = 1000000ULL; // start at t=1s
    for (int i = 0; i < count; i++) {
        ts.push_back(base + (uint64_t)i * 100000ULL); // 100 ms
    }
    (void)cob_id; (void)window_us;
    return ts;
}

PDOTestResult verify_tpdo(uint8_t node, uint8_t pdo_num)
{
    PDOTestResult r{};
    r.pdo_num = pdo_num;

    PDOCommParam comm   = read_tpdo_comm(node, pdo_num);
    PDOMapping   mapping = read_tpdo_mapping(node, pdo_num);

    // 1. COB-ID check: bit 31 must be 0 (PDO active), CAN ID valid
    uint32_t cob = comm.cob_id & 0x1FFFFFFF;
    r.cob_id_ok = !(comm.cob_id & (1UL << 31))  // not disabled
               && (cob >= 0x181 && cob <= 0x57F); // default CAN ID range

    // 2. Transmission type valid: 0-240 (sync), 252-255 (async)
    uint8_t tt = comm.transmission_type;
    r.trans_type_ok = (tt <= 240) || (tt >= 252);

    // 3. Mapping total length <= 64 bits
    uint8_t total_bits = 0;
    for (uint8_t i = 0; i < mapping.n_mapped; i++)
        total_bits += mapping.entries[i].length_bits;
    r.mapping_len_ok = (total_bits <= 64);

    // 4. Timing: capture PDO frames and measure intervals
    if (tt == 254 || tt == 255) {
        // Event driven: expect interval ~event_timer ms
        auto ts = capture_pdo_timestamps(cob, 5, 2000000ULL);
        if (ts.size() >= 2) {
            uint64_t sum_interval = 0;
            for (size_t i = 1; i < ts.size(); i++)
                sum_interval += ts[i] - ts[i-1];
            uint64_t avg_interval = sum_interval / (ts.size() - 1);
            r.observed_interval_us  = avg_interval;
            r.expected_interval_us  = (uint32_t)comm.event_timer * 1000;
            // Allow 20% tolerance
            uint64_t tolerance = r.expected_interval_us / 5;
            r.timing_ok = (avg_interval >= r.expected_interval_us - tolerance) &&
                          (avg_interval <= r.expected_interval_us + tolerance);

            // Inhibit time check: no two consecutive PDOs closer than inhibit
            uint64_t inhibit_us = (uint64_t)comm.inhibit_time * 100;
            r.inhibit_respected = true;
            for (size_t i = 1; i < ts.size(); i++) {
                if ((ts[i] - ts[i-1]) < inhibit_us) {
                    r.inhibit_respected = false;
                    break;
                }
            }
        } else {
            r.timing_ok = false;
        }
    } else {
        // Synchronous: would need SYNC injection — mark as N/A for this example
        r.timing_ok        = true;
        r.inhibit_respected= true;
    }

    return r;
}

int main(void)
{
    uint8_t node = 0x01;
    printf("PDO Conformance Test — Node 0x%02X\n", node);
    printf("====================================\n\n");

    for (uint8_t pdo = 0; pdo < 4; pdo++) {
        PDOTestResult r = verify_tpdo(node, pdo);
        printf("TPDO%u:\n", pdo);
        printf("  COB-ID valid    : %s\n", r.cob_id_ok       ? "PASS" : "FAIL");
        printf("  Trans type      : %s\n", r.trans_type_ok   ? "PASS" : "FAIL");
        printf("  Mapping length  : %s\n", r.mapping_len_ok  ? "PASS" : "FAIL");
        printf("  Timing          : %s (observed %llu us, expected %u us)\n",
               r.timing_ok ? "PASS" : "FAIL",
               (unsigned long long)r.observed_interval_us,
               r.expected_interval_us);
        printf("  Inhibit time    : %s\n\n", r.inhibit_respected ? "PASS" : "FAIL");
    }
    return 0;
}
```

---

## 7. NMT Transition Stress Tests

### 7.1 NMT State Machine (CiA 301)

```
  NMT STATE MACHINE
  ==================

              Power-On / Reset
                    |
                    v
             +-------------+
             | INITIALISING|  (Boot-up sequence)
             +------+------+
                    |
                    | (auto-transition after boot)
                    v
             +-------------+
        +--->| PRE-OPERAT. |<---+
        |    +------+------+    |
        |           |           |
        |   [Start  |           | [Enter Pre-Op]
        |    Node]  |           | (NMT cmd 0x80)
        |           v           |
        |    +-------------+    |
        |    | OPERATIONAL |----+
        |    +------+------+
        |           |
        |           | [Enter Pre-Op / Stop Node]
        |           |
        |    +------v------+
        |    |   STOPPED   |
        |    +------+------+
        |           |
        +------[Enter Pre-Op]
               (NMT cmd 0x80)

  NMT Command Codes:
    0x01 = Start Node        (-> OPERATIONAL)
    0x02 = Stop Node         (-> STOPPED)
    0x80 = Enter Pre-Op      (-> PRE-OPERATIONAL)
    0x81 = Reset Node        (-> INITIALISING -> PRE-OP)
    0x82 = Reset Comm        (-> INITIALISING comm -> PRE-OP)
```

### 7.2 NMT Transition Stress Test

```c
/* ---------------------------------------------------------------
 * nmt_stress_test.c
 * Repeatedly cycles through NMT states and verifies:
 *   1. Boot-up message appears after Reset Node/Reset Comm
 *   2. Heartbeat state matches expected NMT state
 *   3. Device recovers within specified time window
 *   4. No unexpected EMCY frames occur during transitions
 * --------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>   /* usleep */
#include <time.h>

/* ---- NMT state codes (as reported in Heartbeat/Boot-up) ---- */
#define NMT_STATE_INITIALISING   0x00
#define NMT_STATE_STOPPED        0x04
#define NMT_STATE_OPERATIONAL    0x05
#define NMT_STATE_PRE_OP         0x7F

/* ---- NMT command codes ---- */
#define NMT_CMD_START_NODE       0x01
#define NMT_CMD_STOP_NODE        0x02
#define NMT_CMD_ENTER_PREOP      0x80
#define NMT_CMD_RESET_NODE       0x81
#define NMT_CMD_RESET_COMM       0x82

/* ---- Timeouts (ms) ---- */
#define BOOTUP_TIMEOUT_MS        1000
#define TRANSITION_TIMEOUT_MS     500
#define HB_TOLERANCE_MS           200

typedef struct {
    int      pass;
    int      fail;
    int      bootup_timeout;
    int      state_mismatch;
    int      unexpected_emcy;
    uint64_t total_iterations;
} NMTStressStats;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Platform stubs — replace with real CAN driver calls */
static int send_nmt_command(uint8_t node_id, uint8_t cmd)
{
    printf("  [NMT] -> 0x%02X cmd=0x%02X\n", node_id, cmd);
    usleep(1000); /* simulate bus latency */
    return 0;
}

/* Returns observed NMT state from heartbeat, -1 on timeout */
static int wait_for_nmt_state(uint8_t node_id, uint8_t expected_state,
                               uint32_t timeout_ms)
{
    uint64_t deadline = now_ms() + timeout_ms;
    /* Simulate: always return expected state immediately */
    (void)node_id;
    usleep(5000);   /* simulate ~5ms response */
    if (now_ms() > deadline) return -1;
    return expected_state;  /* stub: returns expected */
}

/* Returns 1 if a boot-up message is received, 0 on timeout */
static int wait_for_bootup(uint8_t node_id, uint32_t timeout_ms)
{
    /* Simulate: boot-up always arrives in ~50ms */
    (void)node_id;
    usleep(50000);
    return (50 < timeout_ms) ? 1 : 0;
}

/* Returns 1 if unexpected EMCY was received on bus */
static int check_for_emcy(uint8_t node_id)
{
    (void)node_id;
    return 0; /* simulate: no EMCY */
}

/* One full NMT transition cycle test */
typedef struct {
    uint8_t  cmd;
    uint8_t  expected_state;
    uint32_t timeout_ms;
    int      expect_bootup;
    const char *label;
} NMTStep;

static const NMTStep CYCLE[] = {
    { NMT_CMD_ENTER_PREOP,  NMT_STATE_PRE_OP,      TRANSITION_TIMEOUT_MS, 0, "Enter Pre-Op"  },
    { NMT_CMD_START_NODE,   NMT_STATE_OPERATIONAL,  TRANSITION_TIMEOUT_MS, 0, "Start Node"    },
    { NMT_CMD_STOP_NODE,    NMT_STATE_STOPPED,      TRANSITION_TIMEOUT_MS, 0, "Stop Node"     },
    { NMT_CMD_ENTER_PREOP,  NMT_STATE_PRE_OP,      TRANSITION_TIMEOUT_MS, 0, "Pre-Op again"  },
    { NMT_CMD_RESET_COMM,   NMT_STATE_PRE_OP,      BOOTUP_TIMEOUT_MS,     1, "Reset Comm"    },
    { NMT_CMD_START_NODE,   NMT_STATE_OPERATIONAL,  TRANSITION_TIMEOUT_MS, 0, "Start Node"    },
    { NMT_CMD_RESET_NODE,   NMT_STATE_PRE_OP,      BOOTUP_TIMEOUT_MS,     1, "Reset Node"    },
};
#define N_STEPS (sizeof(CYCLE)/sizeof(CYCLE[0]))

static int run_nmt_cycle(uint8_t node_id, NMTStressStats *stats,
                         int verbose)
{
    int cycle_ok = 1;

    for (size_t i = 0; i < N_STEPS; i++) {
        const NMTStep *s = &CYCLE[i];
        if (verbose) printf("  Step %zu: %s\n", i, s->label);

        send_nmt_command(node_id, s->cmd);

        if (s->expect_bootup) {
            if (!wait_for_bootup(node_id, s->timeout_ms)) {
                fprintf(stderr, "  FAIL: Boot-up timeout after %s\n", s->label);
                stats->bootup_timeout++;
                cycle_ok = 0;
            }
        }

        int observed = wait_for_nmt_state(node_id, s->expected_state,
                                           s->timeout_ms);
        if (observed < 0) {
            fprintf(stderr, "  FAIL: Timeout waiting for state 0x%02X after %s\n",
                    s->expected_state, s->label);
            stats->state_mismatch++;
            cycle_ok = 0;
        } else if ((uint8_t)observed != s->expected_state) {
            fprintf(stderr, "  FAIL: State 0x%02X expected, got 0x%02X after %s\n",
                    s->expected_state, (uint8_t)observed, s->label);
            stats->state_mismatch++;
            cycle_ok = 0;
        }

        if (check_for_emcy(node_id)) {
            fprintf(stderr, "  WARN: Unexpected EMCY during %s\n", s->label);
            stats->unexpected_emcy++;
        }
    }

    stats->total_iterations++;
    if (cycle_ok) stats->pass++;
    else          stats->fail++;
    return cycle_ok;
}

int main(void)
{
    uint8_t  node_id   = 0x01;
    int      n_cycles  = 100;
    int      verbose   = 0;
    NMTStressStats stats = {0};

    printf("NMT Transition Stress Test\n");
    printf("===========================\n");
    printf("Node: 0x%02X | Cycles: %d\n\n", node_id, n_cycles);

    for (int c = 0; c < n_cycles; c++) {
        if (c % 10 == 0)
            printf("Running cycle %d/%d ...\n", c+1, n_cycles);
        run_nmt_cycle(node_id, &stats, verbose);
    }

    printf("\n=== NMT Stress Test Report ===\n");
    printf("  Total cycles       : %llu\n", (unsigned long long)stats.total_iterations);
    printf("  Pass               : %d\n",   stats.pass);
    printf("  Fail               : %d\n",   stats.fail);
    printf("  Boot-up timeouts   : %d\n",   stats.bootup_timeout);
    printf("  State mismatches   : %d\n",   stats.state_mismatch);
    printf("  Unexpected EMCYs   : %d\n",   stats.unexpected_emcy);
    printf("  Pass rate          : %.1f%%\n",
           (double)stats.pass / (double)stats.total_iterations * 100.0);
    printf("  Conformance        : %s\n",
           (stats.fail == 0) ? "PASS" : "FAIL");

    return (stats.fail == 0) ? 0 : 1;
}
```

---

## 8. EMCY and Error Register Validation

### 8.1 EMCY Frame Structure

```
  EMCY FRAME FORMAT (CiA 301)
  ============================

  CAN ID: 0x80 + Node-ID
  DLC: 8 bytes

  Byte  7   6   5   4   3   2   1   0
       +---+---+---+---+---+---+---+---+
  0-1  |    Emergency Error Code       |   (LSB first, 16-bit)
       +---+---+---+---+---+---+---+---+
  2    |     Error Register (0x1001)   |   (8-bit mirror)
       +---+---+---+---+---+---+---+---+
  3-7  | Manufacturer-specific Error   |   (5 bytes)
       +---+---+---+---+---+---+---+---+

  Error Register bit flags (0x1001):
    Bit 0: Generic error
    Bit 1: Current error
    Bit 2: Voltage error
    Bit 3: Temperature error
    Bit 4: Communication error
    Bit 5: Device profile specific
    Bit 6: Reserved (must be 0)
    Bit 7: Manufacturer-specific

  EMCY Recovery: device MUST send EMCY with code 0x0000 when error clears
```

### 8.2 EMCY Conformance Checker

```cpp
// ---------------------------------------------------------------
// emcy_validator.cpp
// Validates EMCY frame content, error register consistency, and
// that error-cleared EMCYs (code 0x0000) are sent on recovery.
// ---------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <map>
#include <string>

struct EMCYFrame {
    uint8_t  node_id;
    uint16_t emergency_error_code;
    uint8_t  error_register;
    uint8_t  manufacturer_data[5];
    uint64_t timestamp_us;
};

// EMCY error code categories (CiA 301 Table 8)
static const struct { uint16_t mask; uint16_t val; const char *name; } EMCY_CLASSES[] = {
    { 0xFF00, 0x0000, "Error Reset / No Error" },
    { 0xFF00, 0x1000, "Generic Error" },
    { 0xFF00, 0x2000, "Current" },
    { 0xFF00, 0x3000, "Voltage" },
    { 0xFF00, 0x4000, "Temperature" },
    { 0xFF00, 0x5000, "Device Hardware" },
    { 0xFF00, 0x6000, "Device Software" },
    { 0xFF00, 0x7000, "Additional Modules" },
    { 0xFF00, 0x8000, "Monitoring" },
    { 0xFF00, 0x9000, "External Error" },
    { 0xFF00, 0xF000, "Additional Functions" },
    { 0xFF00, 0xFF00, "Manufacturer-specific" },
    { 0x0000, 0x0000, nullptr }
};

static const char* emcy_class_name(uint16_t code)
{
    for (int i = 0; EMCY_CLASSES[i].name; i++) {
        if ((code & EMCY_CLASSES[i].mask) == EMCY_CLASSES[i].val)
            return EMCY_CLASSES[i].name;
    }
    return "Unknown";
}

struct EMCYValidationResult {
    bool frame_length_ok;       // DLC == 8
    bool error_code_known;      // in a valid class
    bool error_register_ok;     // bit 6 not set; matches error code class
    bool recovery_emcy_ok;      // 0x0000 sent after error cleared (if applicable)
    int  warnings;
    std::vector<std::string> issues;
};

class EMCYValidator {
public:
    EMCYValidationResult validate(const EMCYFrame& f) {
        EMCYValidationResult r{};
        r.frame_length_ok  = true; // we assume DLC=8 from CAN driver
        r.error_code_known = false;
        r.error_register_ok= true;
        r.recovery_emcy_ok = true;

        // 1. Error code class check
        for (int i = 0; EMCY_CLASSES[i].name; i++) {
            if ((f.emergency_error_code & EMCY_CLASSES[i].mask) == EMCY_CLASSES[i].val) {
                r.error_code_known = true;
                break;
            }
        }
        if (!r.error_code_known)
            r.issues.push_back("Unknown EMCY error code class");

        // 2. Error register bit 6 must always be 0
        if (f.error_register & 0x40) {
            r.error_register_ok = false;
            r.issues.push_back("Error Register bit 6 is set (reserved, must be 0)");
        }

        // 3. Generic error (bit 0) must be set for any non-zero code
        if (f.emergency_error_code != 0x0000 && !(f.error_register & 0x01)) {
            r.error_register_ok = false;
            r.issues.push_back("Generic error bit (0x1001 bit0) not set for non-zero EMCY");
        }

        // 4. Track error code in history
        if (f.emergency_error_code == 0x0000) {
            // Recovery: check that a prior error existed
            if (m_active_errors.find(f.node_id) == m_active_errors.end() ||
                m_active_errors[f.node_id].empty()) {
                r.warnings++;
                r.issues.push_back("Received EMCY 0x0000 but no prior active error tracked");
            } else {
                m_active_errors[f.node_id].pop_back();
            }
        } else {
            m_active_errors[f.node_id].push_back(f.emergency_error_code);
        }

        return r;
    }

    void print_report(const EMCYFrame& f, const EMCYValidationResult& r) const {
        printf("  EMCY Node=0x%02X Code=0x%04X (%s) Reg=0x%02X\n",
               f.node_id, f.emergency_error_code,
               emcy_class_name(f.emergency_error_code),
               f.error_register);
        printf("    Error code known  : %s\n", r.error_code_known  ? "OK" : "FAIL");
        printf("    Error register    : %s\n", r.error_register_ok ? "OK" : "FAIL");
        for (const auto& issue : r.issues)
            printf("    Issue: %s\n", issue.c_str());
        printf("\n");
    }

private:
    std::map<uint8_t, std::vector<uint16_t>> m_active_errors;
};

int main(void)
{
    EMCYValidator validator;

    // Simulate incoming EMCY frames
    std::vector<EMCYFrame> frames = {
        { 0x01, 0x8110, 0x11, {0,0,0,0,0}, 1000000 }, // CAN overrun
        { 0x01, 0x8120, 0x11, {0,0,0,0,0}, 1200000 }, // CAN passive
        { 0x01, 0x0000, 0x00, {0,0,0,0,0}, 2000000 }, // recovered
        { 0x02, 0x4210, 0x09, {0,0,0,0,0}, 1500000 }, // temperature
        { 0x02, 0x4210, 0x49, {0,0,0,0,0}, 1500000 }, // bit6 set — ERROR
    };

    printf("EMCY Conformance Validation\n");
    printf("============================\n\n");

    int total_pass = 0, total_fail = 0;
    for (const auto& f : frames) {
        auto r = validator.validate(f);
        validator.print_report(f, r);

        bool ok = r.error_code_known && r.error_register_ok && r.recovery_emcy_ok;
        ok ? total_pass++ : total_fail++;
    }

    printf("Result: %d PASS, %d FAIL\n", total_pass, total_fail);
    return (total_fail == 0) ? 0 : 1;
}
```

---

## 9. Vendor Interoperability Workshops

### 9.1 Workshop Structure

Interoperability workshops are multi-day events where devices from different
vendors are connected on a shared CAN bus and validated against each other —
not against a reference implementation, but against the *standard's behaviour
definition*.

```
  INTEROPERABILITY WORKSHOP — BUS TOPOLOGY
  ==========================================

  CAN Bus (125 kbit/s default during discovery, then 1 Mbit/s)
  |
  +---[ Bus Analyser / Logger ]  (passive, all frames captured)
  |
  +---[ NMT Master / Test Coordinator ] (node 0x01, vendor A)
  |
  +---[ Drive / Servo ]                 (node 0x02, vendor B)
  |
  +---[ I/O Module ]                    (node 0x03, vendor C)
  |
  +---[ Safety Module ]                 (node 0x04, vendor D)
  |
  +---[ HMI / Display ]                 (node 0x05, vendor E)
  |
  +---[ 120 Ohm termination ]

  Phases:
  1. EDS exchange and off-line compatibility check
  2. Physical layer verification (bit timing, termination)
  3. NMT boot sequence with all nodes
  4. SDO parameter configuration cross-vendor
  5. PDO data exchange under load
  6. Error injection and recovery
  7. Mixed-vendor heartbeat monitoring
```

### 9.2 PDO Cross-Vendor Compatibility Matrix

During a workshop, the following compatibility matrix is filled for each PDO
pairing:

```
  TPDO <-> RPDO COMPATIBILITY MATRIX
  =====================================

  Producer \ Consumer | Node B  | Node C  | Node D  | Node E
  --------------------|---------|---------|---------|--------
  Node A (TPDO1)      |   OK    |   OK    |  WARN*  |   N/A
  Node B (TPDO1)      |   ---   |   OK    |   OK    |   OK
  Node C (TPDO1)      |   OK    |   ---   |   OK    |  FAIL**
  Node D (TPDO1)      |   N/A   |   OK    |   ---   |   OK

  * WARN: COB-ID conflict (both default, required manual config)
  ** FAIL: Byte order (endianness) mismatch in mapping
```

### 9.3 Boot Sequence Interoperability Check (C)

```c
/* ---------------------------------------------------------------
 * interop_boot_check.c
 * Monitors all nodes on the bus during boot and verifies:
 *   - All nodes send boot-up messages
 *   - Boot-up arrives within device profile deadline
 *   - Heartbeat COB-IDs do not collide
 *   - No duplicate Node-IDs
 * --------------------------------------------------------------- */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define MAX_NODES 127

typedef struct {
    uint8_t  node_id;
    uint8_t  bootup_received;
    uint64_t bootup_time_ms;
    uint8_t  nmt_state;
    uint16_t hb_producer_time_ms;  /* from 0x1017 SDO read */
} NodeInfo;

static NodeInfo g_nodes[MAX_NODES];
static int      g_node_count = 0;

void register_node(uint8_t node_id, uint16_t hb_time_ms)
{
    if (g_node_count >= MAX_NODES) return;
    NodeInfo *n        = &g_nodes[g_node_count++];
    n->node_id         = node_id;
    n->bootup_received = 0;
    n->bootup_time_ms  = 0;
    n->nmt_state       = 0xFF; /* unknown */
    n->hb_producer_time_ms = hb_time_ms;
}

void on_bootup_received(uint8_t node_id, uint64_t time_ms)
{
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].node_id == node_id) {
            g_nodes[i].bootup_received = 1;
            g_nodes[i].bootup_time_ms  = time_ms;
            g_nodes[i].nmt_state       = NMT_STATE_PRE_OP;
            break;
        }
    }
}

int check_duplicate_node_ids(void)
{
    int duplicates = 0;
    for (int i = 0; i < g_node_count; i++) {
        for (int j = i+1; j < g_node_count; j++) {
            if (g_nodes[i].node_id == g_nodes[j].node_id) {
                printf("  FAIL: Duplicate Node-ID 0x%02X detected!\n",
                       g_nodes[i].node_id);
                duplicates++;
            }
        }
    }
    return duplicates;
}

int check_hb_cob_id_collision(void)
{
    /* Heartbeat COB-ID = 0x700 + node_id; must be unique */
    /* Since node_id must be unique, HB COB-IDs are automatically unique */
    /* This check ensures no node uses 0x700 (broadcast, invalid) */
    int violations = 0;
    for (int i = 0; i < g_node_count; i++) {
        if (g_nodes[i].node_id == 0x00) {
            printf("  FAIL: Node 0x00 is not a valid node_id\n");
            violations++;
        }
    }
    return violations;
}

void print_boot_report(uint64_t deadline_ms)
{
    printf("\n=== Interoperability Boot Report ===\n");
    printf("  %-8s %-10s %-12s %-10s %s\n",
           "Node-ID", "Boot-up", "Time(ms)", "HB Period", "Conformance");
    printf("  %s\n", "-----------------------------------------------");

    int pass = 0, fail = 0;
    for (int i = 0; i < g_node_count; i++) {
        NodeInfo *n = &g_nodes[i];
        const char *conf;
        if (!n->bootup_received) {
            conf = "FAIL (no boot-up)";
            fail++;
        } else if (n->bootup_time_ms > deadline_ms) {
            conf = "FAIL (too slow)";
            fail++;
        } else {
            conf = "PASS";
            pass++;
        }
        printf("  0x%02X     %-10s %-12llu %-10u %s\n",
               n->node_id,
               n->bootup_received ? "YES" : "NO",
               (unsigned long long)n->bootup_time_ms,
               n->hb_producer_time_ms,
               conf);
    }
    printf("  %s\n", "-----------------------------------------------");
    printf("  Duplicates   : %d\n", check_duplicate_node_ids());
    printf("  COB conflicts: %d\n", check_hb_cob_id_collision());
    printf("  Pass/Fail    : %d / %d\n\n", pass, fail);
}

/* Simulated workshop scenario */
#define NMT_STATE_PRE_OP 0x7F
int main(void)
{
    /* Register expected nodes from EDS inventory */
    register_node(0x01, 1000);
    register_node(0x02, 1000);
    register_node(0x03, 500);
    register_node(0x04, 500);
    register_node(0x05, 2000);

    /* Simulate boot-up messages arriving (from CAN bus monitor) */
    on_bootup_received(0x01, 120);
    on_bootup_received(0x02, 250);
    on_bootup_received(0x03, 300);
    /* Node 0x04 — late boot-up */
    on_bootup_received(0x04, 2500);
    /* Node 0x05 — never boots (simulate failure) */

    print_boot_report(1000);
    return 0;
}
```

---

## 10. Automated Test Harness in C/C++

A practical conformance test campaign integrates all individual tests into a
unified harness with structured reporting.

```cpp
// ---------------------------------------------------------------
// conformance_harness.cpp
// Top-level test runner integrating all conformance test suites.
// Produces a structured pass/fail report per CiA test group.
// ---------------------------------------------------------------
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>
#include <functional>
#include <string>
#include <chrono>

// ---- Test group identifiers ----
enum class TestGroup {
    EDS_VALIDATION,
    OBJ_MANDATORY,
    OBJ_OPTIONAL,
    SDO_EXPEDITED,
    SDO_SEGMENTED,
    SDO_BLOCK,
    SDO_ABORT_CODES,
    PDO_CONFIG,
    PDO_TIMING,
    NMT_TRANSITIONS,
    NMT_STRESS,
    EMCY_FORMAT,
    EMCY_RECOVERY,
    HB_TIMING,
    SYNC_TIMING,
};

static const char* group_name(TestGroup g) {
    switch (g) {
    case TestGroup::EDS_VALIDATION:  return "EDS Validation";
    case TestGroup::OBJ_MANDATORY:   return "Mandatory Objects";
    case TestGroup::OBJ_OPTIONAL:    return "Optional Objects";
    case TestGroup::SDO_EXPEDITED:   return "SDO Expedited";
    case TestGroup::SDO_SEGMENTED:   return "SDO Segmented";
    case TestGroup::SDO_BLOCK:       return "SDO Block Mode";
    case TestGroup::SDO_ABORT_CODES: return "SDO Abort Codes";
    case TestGroup::PDO_CONFIG:      return "PDO Configuration";
    case TestGroup::PDO_TIMING:      return "PDO Timing";
    case TestGroup::NMT_TRANSITIONS: return "NMT Transitions";
    case TestGroup::NMT_STRESS:      return "NMT Stress";
    case TestGroup::EMCY_FORMAT:     return "EMCY Format";
    case TestGroup::EMCY_RECOVERY:   return "EMCY Recovery";
    case TestGroup::HB_TIMING:       return "Heartbeat Timing";
    case TestGroup::SYNC_TIMING:     return "SYNC Timing";
    default:                          return "Unknown";
    }
}

// ---- Single test case ----
struct TestCase {
    TestGroup   group;
    std::string name;
    bool        mandatory;   // mandatory for CiA certificate
    std::function<bool()> run;
};

// ---- Test result ----
struct TestRecord {
    TestCase  tc;
    bool      passed;
    double    duration_ms;
    std::string failure_reason;
};

// ---- Test harness ----
class ConformanceHarness {
public:
    void add(TestGroup group, const char *name, bool mandatory,
             std::function<bool()> fn)
    {
        m_tests.push_back({ group, name, mandatory, std::move(fn) });
    }

    void run_all(uint8_t node_id) {
        printf("\n");
        printf("  ╔══════════════════════════════════════════════════════════╗\n");
        printf("  ║      CANopen Conformance Test Campaign                   ║\n");
        printf("  ║      Node-ID: 0x%02X                                       ║\n",
               node_id);
        printf("  ╚══════════════════════════════════════════════════════════╝\n\n");

        for (auto& tc : m_tests) {
            auto t0 = std::chrono::steady_clock::now();
            bool ok = false;
            std::string reason;
            try {
                ok = tc.run();
            } catch (const std::exception& e) {
                ok     = false;
                reason = e.what();
            }
            auto t1 = std::chrono::steady_clock::now();
            double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

            TestRecord rec;
            rec.tc             = tc;
            rec.passed         = ok;
            rec.duration_ms    = ms;
            rec.failure_reason = reason;
            m_records.push_back(rec);

            printf("  [%s] %-36s %s  (%.1f ms)%s\n",
                   tc.mandatory ? "M" : "O",
                   tc.name.c_str(),
                   ok ? "PASS" : "FAIL",
                   ms,
                   (ok || reason.empty()) ? "" : (" — " + reason).c_str());
        }

        print_summary();
    }

private:
    void print_summary() const {
        printf("\n");
        printf("  ┌─────────────────────────────────────────────────────────┐\n");
        printf("  │  CONFORMANCE SUMMARY                                    │\n");
        printf("  ├──────────────────────┬──────┬──────┬────────────────────┤\n");
        printf("  │ Group                │ Pass │ Fail │ Mandatory Fails    │\n");
        printf("  ├──────────────────────┼──────┼──────┼────────────────────┤\n");

        // Group results
        struct GroupStat { int pass, fail, mand_fail; };
        std::map<TestGroup, GroupStat> stats;
        for (const auto& r : m_records) {
            auto& s = stats[r.tc.group];
            if (r.passed) s.pass++;
            else {
                s.fail++;
                if (r.tc.mandatory) s.mand_fail++;
            }
        }

        int total_pass = 0, total_fail = 0, total_mfail = 0;
        for (const auto& kv : stats) {
            printf("  │ %-20s │ %4d │ %4d │ %18d │\n",
                   group_name(kv.first),
                   kv.second.pass, kv.second.fail, kv.second.mand_fail);
            total_pass  += kv.second.pass;
            total_fail  += kv.second.fail;
            total_mfail += kv.second.mand_fail;
        }

        printf("  ├──────────────────────┼──────┼──────┼────────────────────┤\n");
        printf("  │ TOTAL                │ %4d  │ %4d  │ %18d               │\n",
               total_pass, total_fail, total_mfail);
        printf("  └──────────────────────┴──────┴──────┴────────────────────┘\n");

        printf("\n  Certificate eligibility: %s\n",
               (total_mfail == 0 && total_fail == 0)
               ? "ELIGIBLE — all tests passed"
               : (total_mfail == 0)
                 ? "CONDITIONAL — optional failures only"
                 : "NOT ELIGIBLE — mandatory test failures");
    }

    std::vector<TestCase>   m_tests;
    std::vector<TestRecord> m_records;
};

// ---- Test implementations (stubs — wire up to your CAN stack) ----
static bool test_eds_sections()   { return true; /* eds_validate(path).error_count == 0 */ }
static bool test_obj_device_type(){ return true; /* sdo_upload(0x1000,0).result == OK */ }
static bool test_obj_identity()   { return true; /* check 0x1018 subindices */ }
static bool test_sdo_expedited()  { return true; /* round-trip < 200ms */ }
static bool test_sdo_segmented()  { return true; /* toggle bit check */ }
static bool test_sdo_abort_0602() { return true; /* non-existent object -> 0x06020000 */ }
static bool test_pdo_config()     { return true; /* total mapped bits <= 64 */ }
static bool test_pdo_timing()     { return true; /* event timer +-20% */ }
static bool test_nmt_start()      { return true; /* 0x05 within 500ms */ }
static bool test_nmt_preop()      { return true; /* 0x7F within 500ms */ }
static bool test_nmt_reset()      { return true; /* boot-up within 1s */ }
static bool test_nmt_stress()     { return true; /* 100 cycles, 0 fail */ }
static bool test_emcy_format()    { return true; /* bit 6 check */ }
static bool test_emcy_recovery()  { return true; /* 0x0000 clears active */ }
static bool test_hb_timing()      { return true; /* within +/- HB/4 */ }

int main()
{
    ConformanceHarness harness;

    harness.add(TestGroup::EDS_VALIDATION, "EDS Section Presence",     true,  test_eds_sections);
    harness.add(TestGroup::OBJ_MANDATORY,  "Device Type (0x1000)",      true,  test_obj_device_type);
    harness.add(TestGroup::OBJ_MANDATORY,  "Identity Object (0x1018)",  true,  test_obj_identity);
    harness.add(TestGroup::SDO_EXPEDITED,  "SDO Expedited Upload",      true,  test_sdo_expedited);
    harness.add(TestGroup::SDO_SEGMENTED,  "SDO Segmented Toggle Bit",  true,  test_sdo_segmented);
    harness.add(TestGroup::SDO_ABORT_CODES,"SDO Abort 0x06020000",      true,  test_sdo_abort_0602);
    harness.add(TestGroup::PDO_CONFIG,     "PDO Mapping Length <= 64b", true,  test_pdo_config);
    harness.add(TestGroup::PDO_TIMING,     "PDO Event Timer Tolerance", false, test_pdo_timing);
    harness.add(TestGroup::NMT_TRANSITIONS,"NMT Start Node",            true,  test_nmt_start);
    harness.add(TestGroup::NMT_TRANSITIONS,"NMT Enter Pre-Op",          true,  test_nmt_preop);
    harness.add(TestGroup::NMT_TRANSITIONS,"NMT Reset Node + Boot-up",  true,  test_nmt_reset);
    harness.add(TestGroup::NMT_STRESS,     "NMT 100-Cycle Stress",      false, test_nmt_stress);
    harness.add(TestGroup::EMCY_FORMAT,    "EMCY Frame Format",         true,  test_emcy_format);
    harness.add(TestGroup::EMCY_RECOVERY,  "EMCY Recovery (0x0000)",    true,  test_emcy_recovery);
    harness.add(TestGroup::HB_TIMING,      "Heartbeat Period Accuracy",  true,  test_hb_timing);

    harness.run_all(0x01);
    return 0;
}
```

---

## 11. Preparing a Certificate of Conformance

### 11.1 Certificate of Conformance Process

```
  CiA CERTIFICATE OF CONFORMANCE — PROCESS FLOW
  ================================================

  Vendor                    Accredited Lab              CiA e.V.
  ======                    ==============              =========
     |                            |                         |
     |--- Submit Application ---->|                         |
     |    (EDS, DUT, docs)        |                         |
     |                            |                         |
     |<-- Acknowledge + Schedule -|                         |
     |                            |                         |
     |--- Ship DUT + EDS -------->|                         |
     |                            |                         |
     |                     [Run Test Campaign]              |
     |                       (CiA 301 suite)                |
     |                            |                         |
     |<-- Preliminary Report -----|                         |
     |    (pass/fail details)     |                         |
     |                            |                         |
  [Fix]                           |                         |
     |--- Corrected DUT --------->|                         |
     |                            |                         |
     |                     [Re-test failed items]           |
     |                            |                         |
     |<-- Final Report -----------|                         |
     |                            |                         |
     |                            |--- Submit to CiA ------>|
     |                            |    (signed report)      |
     |                            |                         |
     |<----- Certificate Issued ----------------------------|
     |       (valid 3 years)      |                         |
```

### 11.2 Certificate Documentation Checklist

The following artefacts must be prepared **before** submitting to a test lab:

```
  PRE-SUBMISSION CHECKLIST
  =========================

  Device Documentation:
  [ ] Electronic Data Sheet (EDS) — validated, version stamped
  [ ] Object Dictionary table (index, sub, type, access, range, default)
  [ ] Device profile compliance declaration (CiA 4xx/5xx as applicable)
  [ ] Hardware revision (PCB version, BOM)
  [ ] Firmware version and build hash

  Protocol Documentation:
  [ ] Supported SDO services (expedited / segmented / block)
  [ ] PDO count and default mapping for each TPDO/RPDO
  [ ] NMT slave/master capability declaration
  [ ] Heartbeat producer / consumer configuration
  [ ] Supported baud rates
  [ ] LSS support declaration (if applicable)
  [ ] SRDO support (if CiA 304 claimed)

  Test Preparation:
  [ ] DUT configured to factory defaults
  [ ] Two DUT samples provided (primary + spare)
  [ ] Node-ID configurable to test lab's chosen value
  [ ] Baud rate configurable (via hardware switches or LSS)
  [ ] Test interface cable and connector types documented
  [ ] Power supply requirements documented

  Quality Documents:
  [ ] Declaration of Conformance (self-assessment)
  [ ] Previous test reports (if re-certification)
  [ ] Change log vs prior certificate (if applicable)
```

### 11.3 Certificate Content (CiA format)

A conformance certificate issued by an accredited lab contains:

```
  CERTIFICATE OF CONFORMANCE
  ===========================

  Certificate No. : CIA-CONF-2025-XXXXX
  Issue Date      : YYYY-MM-DD
  Valid Until     : YYYY+3-MM-DD

  Device Under Test
  -----------------
  Manufacturer    : Acme Drives GmbH
  Product Name    : CANopen Servo Drive Type X
  Product Code    : 0xABCD1234
  Revision        : 0x00010001
  Serial          : (batch)
  EDS Version     : 2.3

  Standard Tested
  ---------------
  CiA 301 Rev 4.2.0  — Application Layer and Communication Profile
  CiA 402 Rev 3.0    — Device Profile for Drives and Motion Control
  CiA 306 Rev 1.3    — Electronic Data Sheet Specification

  Test Groups
  -----------
  OBJ   : PASS (all mandatory objects present)
  SDO   : PASS (expedited, segmented; block not supported)
  PDO   : PASS (4 TPDO, 4 RPDO)
  NMT   : PASS (slave; producer HB)
  EMCY  : PASS
  SYNC  : PASS (consumer)
  LSS   : NOT TESTED (not supported by device)
  SRDO  : NOT TESTED (not supported by device)

  Remarks
  -------
  SDO Block Mode: not implemented — device correctly returns
  abort code 0x05040001 for block transfer initiation.

  Signed: [Test Engineer]          [Lab Director]
          Accredited CANopen Test Laboratory
```

---

## 12. Common Failure Modes and Remediation

```
  COMMON CONFORMANCE FAILURES
  ============================

  CATEGORY         FAILURE                       ROOT CAUSE               FIX
  ---------------  --------------------------    ----------------------   -------------------
  EDS              SupportedObjects mismatch     OD added without EDS     Regenerate EDS from OD
  EDS              EDSVersion < 4.0              Legacy EDS file          Update EDS header
  OBJ              0x1018 Vendor ID = 0          Not assigned by CiA      Register with CiA e.V.
  OBJ              0x1001 not updated            Error register static    Update on error events
  SDO              Toggle bit not flipped        Bug in segmented xfer    Fix state machine
  SDO              Wrong abort code (0x08000000) Generic fallback used    Map specific abort codes
  SDO              Timeout < 200 ms              Too aggressive timeout   Per CiA 301 recommendation
  PDO              Mapped bits > 64              Mapping error in EDS     Correct mapping entries
  PDO              COB-ID collision at boot      Default ID conflict      Use LSS or commissioning
  PDO              TPDO not sent after SYNC      Interrupt latency        Check SYNC ISR priority
  NMT              No boot-up after Reset Node   Boot sequence bug        Check init code path
  NMT              Stays in STOPPED on bus err   Missing error recovery   Implement NMT slave FSM
  EMCY             Bit 6 set in Error Register   Register not masked      AND result with 0xBF
  EMCY             No recovery EMCY (0x0000)     Send-on-clear missing    Send 0x0000 when clearing
  HB               Heartbeat interval > +25%     Timer drift              Use hardware timer
  HB               Heartbeat after Stop Node     NMT state not checked    Suppress HB in STOPPED
```

---

## 13. Summary

CANopen conformance and interoperability testing is a structured, multi-layer
validation discipline. The key points are:

**Conformance testing** (against CiA 301 and device profiles) verifies that:
- The EDS accurately describes the implemented Object Dictionary.
- All mandatory objects (0x1000, 0x1001, 0x1005, 0x1014, 0x1017, 0x1018) are
  present and readable.
- SDO services handle expedited and segmented transfers correctly, with proper
  toggle-bit handling and standard-compliant abort codes.
- PDO mappings do not exceed 64 bits, COB-IDs are valid, and transmission timing
  (synchronous or event-driven with inhibit time) meets the specification.
- NMT state transitions (Pre-Operational, Operational, Stopped, Reset) occur
  reliably and a boot-up message is generated after every reset.
- EMCY frames are correctly formatted (bit 6 of Error Register = 0; code 0x0000
  sent on recovery).

**Interoperability testing** additionally verifies that:
- Devices from multiple vendors boot without COB-ID conflicts.
- PDO producers and consumers from different vendors exchange data
  without endianness, length, or timing mismatches.
- NMT masters from vendor A can manage NMT slaves from vendors B, C, and D.

**Certificate of Conformance** requires:
- A complete, validated EDS (CiA 306).
- Passing all mandatory test groups at an accredited lab.
- A registered Vendor ID from CiA e.V.
- A valid Product Code and Revision in object 0x1018.

**In C/C++ implementations** the most critical areas to get right are:
- The SDO segmented transfer toggle-bit state machine.
- The NMT finite-state machine, particularly boot-up and reset paths.
- Accurate, hardware-timer-driven heartbeat production.
- Correct EMCY generation and error-register management tied to real
  hardware/software error detection.

Investing in an automated test harness (as shown in Section 10) dramatically
reduces the cost of lab testing by catching the majority of conformance issues
before the DUT ever leaves the development facility.

---

*CANopen Series — Chapter 31 of 32*
*References: CiA 301 Rev 4.2.0, CiA 306 Rev 1.3, CiA 303, CiA 402*