# 08 · EDS & DCF Device Description Files

> **CANopen Series — Chapter 8**
> Covers: EDS INI-file syntax · mandatory/optional sections · data-type encoding ·
> generating EDS from code · parsing EDS at runtime · DCF differences ·
> toolchain integration

---

## Table of Contents

1. [Overview and Purpose](#1-overview-and-purpose)
2. [File Format — INI Syntax Foundations](#2-file-format--ini-syntax-foundations)
3. [Mandatory EDS Sections](#3-mandatory-eds-sections)
4. [Object Dictionary Sections](#4-object-dictionary-sections)
5. [Data Type Encoding](#5-data-type-encoding)
6. [Optional Sections](#6-optional-sections)
7. [DCF Files — Node-ID-Parameterised Variant](#7-dcf-files--node-id-parameterised-variant)
8. [Generating EDS from C/C++ Code](#8-generating-eds-from-cc-code)
9. [Parsing EDS at Runtime in C/C++](#9-parsing-eds-at-runtime-in-cc)
10. [Toolchain Integration](#10-toolchain-integration)
11. [Common Pitfalls and Best Practices](#11-common-pitfalls-and-best-practices)
12. [Summary](#12-summary)

---

## 1. Overview and Purpose

An **Electronic Data Sheet (EDS)** is a standardised, human-readable text file that
completely describes a CANopen device's Object Dictionary — its data objects, their
types, access rights, default values, and communication capabilities — without any
knowledge of the physical device or network topology.

A **Device Configuration File (DCF)** is a superset of EDS that additionally stores the
*actual* network-specific parameter values (node-ID, baud rate, PDO mappings …) for one
concrete device instance on a specific bus.

```
 ┌────────────────────────────────────────────────────────────────────────────┐
 │                      CANopen Device Description Landscape                  │
 │                                                                            │
 │  ┌──────────────┐   describes    ┌──────────────────────────────────────┐  │
 │  │  Real Device │ ─────────────► │         Object Dictionary            │  │
 │  │  (firmware)  │                │  Index 0x1000 … 0x9FFF               │  │
 │  └──────────────┘                └──────────────────────────────────────┘  │
 │          │                                        │                        │
 │          │  captured by                           │  encoded in            │
 │          ▼                                        ▼                        │
 │  ┌───────────────────────┐           ┌────────────────────────────────┐    │
 │  │       EDS File        │           │          DCF File              │    │
 │  │  (device-generic)     │           │  (network / node instance)     │    │
 │  │                       │           │                                │    │
 │  │  • OD structure only  │  extends  │  • Same OD structure           │    │
 │  │  • Default values     │ ────────► │  • ParameterValue entries      │    │
 │  │  • No node-ID         │           │  • Concrete node-ID baked in   │    │
 │  │  • *.eds extension    │           │  • *.dcf extension             │    │
 │  └───────────────────────┘           └────────────────────────────────┘    │
 │                                                                            │
 │  Used by: CANopen Magic, Peak PCAN-Explorer, EDS Editor, NMT master,       │
 │           configuration tools, automated test frameworks                   │
 └────────────────────────────────────────────────────────────────────────────┘
```

Both formats are defined in **CiA 306** (CANopen Electronic Data Sheet specification).

---

## 2. File Format — INI Syntax Foundations

EDS / DCF files follow classic **Windows INI** syntax. The parser must handle:

```
 ┌─────────────────────────────────────────────────────────────────┐
 │                     INI Syntax Quick Reference                  │
 │                                                                 │
 │  [SectionName]          ← section header (case-insensitive)     │
 │  Key=Value              ← key-value pair, no spaces around =    │
 │  ; This is a comment    ← semicolons introduce line comments    │
 │  $NODEID+0x600          ← DCF macro: node-ID placeholder        │
 │                                                                 │
 │  Rules:                                                         │
 │  • Keys are case-insensitive                                    │
 │  • Values are strings; numeric bases: 0x… hex, 0… octal,        │
 │    plain decimal                                                │
 │  • Boolean: 0 = false, 1 = true                                 │
 │  • Strings may be quoted: "hello world"                         │
 │  • Trailing whitespace is significant (trim it!)                │
 └─────────────────────────────────────────────────────────────────┘
```

Minimal complete EDS skeleton (comments stripped):

```ini
[FileInfo]
FileName=MyDevice.eds
FileVersion=1
FileRevision=1
EDSVersion=4.0
Description=Minimal example
CreationTime=10:00AM
CreationDate=05-20-2026
CreatedBy=Engineering
ModificationTime=10:00AM
ModificationDate=05-20-2026
ModifiedBy=Engineering

[DeviceInfo]
VendorName=ACME Drives
VendorNumber=0x00000042
ProductName=AxisController
ProductNumber=0x00000001
RevisionNumber=0x00010000
OrderCode=AC-100
BaudRate_10=1
BaudRate_20=1
BaudRate_50=1
BaudRate_125=1
BaudRate_250=1
BaudRate_500=1
BaudRate_800=1
BaudRate_1000=1
SimpleBootUpMaster=0
SimpleBootUpSlave=1
Granularity=0
DynamicChannelsSupported=0
GroupMessaging=0
NrOfRXPDO=1
NrOfTXPDO=1
LSS_Supported=0

[DummyUsage]
Dummy0001=0
Dummy0002=0
Dummy0003=0
Dummy0004=0
Dummy0005=0
Dummy0006=0
Dummy0007=0

[Comments]
Lines=0

[Objects]
SupportedObjects=3
1=0x1000
2=0x1001
3=0x1018

; ... object sections follow ...
```

---

## 3. Mandatory EDS Sections

### 3.1 [FileInfo]

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  [FileInfo] — Mandatory Keys                                            │
 │                                                                         │
 │  Key               Type    Description                                  │
 │  ──────────────── ─────── ───────────────────────────────────────────   │
 │  FileName          string  Basename of the file (no path)               │
 │  FileVersion       uint    Integer file version                         │
 │  FileRevision      uint    Integer file revision                        │
 │  EDSVersion        string  Spec revision: "4.0" is current              │
 │  Description       string  Free-text (may be empty)                     │
 │  CreationTime      string  "HH:MMxM" format, e.g. "10:00AM"             │
 │  CreationDate      string  "MM-DD-YYYY"                                 │
 │  CreatedBy         string  Author name                                  │
 │  ModificationTime  string  Same format as CreationTime                  │
 │  ModificationDate  string  Same format as CreationDate                  │
 │  ModifiedBy        string  Last editor                                  │
 └─────────────────────────────────────────────────────────────────────────┘
```

### 3.2 [DeviceInfo]

Contains capability flags. The baud-rate keys use a 0/1 present/absent convention.
`NrOfRXPDO` and `NrOfTXPDO` must exactly match the number of PDO objects present in the
OD sections.

### 3.3 [DummyUsage]

Declares which CiA 301 "dummy objects" (0x0001–0x0007) the device uses as PDO mapping
placeholders. All seven keys are mandatory; set to 0 (unused) or 1 (used).

### 3.4 [Objects] / [Comments]

`[Objects]` lists every supported index in ascending order.
`[Comments]` carries free-text lines for tool display (optional but section must exist).

---

## 4. Object Dictionary Sections

Each object index listed in `[Objects]` must have a matching section. The naming
convention depends on object type:

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │                    Object Section Naming Convention                          │
 │                                                                              │
 │   Object type        Section name          Sub-object section                │
 │   ─────────────────  ──────────────────    ─────────────────────────────     │
 │   VAR  (single)      [1000]                (none — top-level is the var)     │
 │   ARRAY              [1600]                [1600sub0], [1600sub1], …         │
 │   RECORD             [1018]                [1018sub0], [1018sub1], …         │
 │                                                                              │
 │  Indices use uppercase HEX without 0x prefix.                                │
 └──────────────────────────────────────────────────────────────────────────────┘
```

### 4.1 VAR Object — Example: Device Type (0x1000)

```ini
[1000]
ParameterName=Device type
ObjectType=0x7          ; VAR
DataType=0x0007         ; UNSIGNED32
AccessType=ro
DefaultValue=0x00000000
PDOMapping=0
```

### 4.2 ARRAY Object — Example: RXPDO Mapping (0x1600)

An ARRAY first has a sub0 (count), then sub1…subN for each element:

```ini
[1600]
ParameterName=Receive PDO Mapping Parameter 1
ObjectType=0x8          ; ARRAY
SubNumber=9             ; sub0 (count) + sub1..sub8

[1600sub0]
ParameterName=Highest sub-index supported
ObjectType=0x7
DataType=0x0005         ; UNSIGNED8
AccessType=rw
DefaultValue=0x02
PDOMapping=0

[1600sub1]
ParameterName=1st mapped object
ObjectType=0x7
DataType=0x0007
AccessType=rw
DefaultValue=0x60400010 ; Index=0x6040, Sub=0x00, Bits=16
PDOMapping=0

[1600sub2]
ParameterName=2nd mapped object
ObjectType=0x7
DataType=0x0007
AccessType=rw
DefaultValue=0x607A0020 ; Index=0x607A, Sub=0x00, Bits=32
PDOMapping=0
```

### 4.3 RECORD Object — Example: Identity Object (0x1018)

```ini
[1018]
ParameterName=Identity Object
ObjectType=0x9          ; RECORD
SubNumber=5

[1018sub0]
ParameterName=Highest sub-index supported
ObjectType=0x7
DataType=0x0005
AccessType=ro
DefaultValue=0x04
PDOMapping=0

[1018sub1]
ParameterName=Vendor-ID
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000042
PDOMapping=0

[1018sub2]
ParameterName=Product code
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000001
PDOMapping=0

[1018sub3]
ParameterName=Revision number
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00010000
PDOMapping=0

[1018sub4]
ParameterName=Serial number
ObjectType=0x7
DataType=0x0007
AccessType=ro
DefaultValue=0x00000000
PDOMapping=0
```

### 4.4 Object Type Codes

```
 ┌──────────────────────────────────────────────────────────┐
 │  ObjectType values (CiA 301)                             │
 │                                                          │
 │  Code   Symbol   Meaning                                 │
 │  ─────  ───────  ──────────────────────────────────────  │
 │  0x05   DEFTYPE  Type definition (built-in)              │
 │  0x06   DEFSTRUCT  Structure type (built-in)             │
 │  0x07   VAR      Single variable                         │
 │  0x08   ARRAY    Array of uniform type                   │
 │  0x09   RECORD   Structure of mixed types                │
 └──────────────────────────────────────────────────────────┘
```

### 4.5 AccessType Values

```
 ┌──────────────────────────────────────────────────────────────┐
 │  AccessType   Read   Write   Notes                           │
 │  ──────────  ─────  ──────  ───────────────────────────────  │
 │  ro           yes    no      Read-only                       │
 │  wo           no     yes     Write-only                      │
 │  rw           yes    yes     Read/write                      │
 │  rwr          yes    yes     Read/write on PDO receive       │
 │  rww          yes    yes     Read/write on PDO transmit      │
 │  const        yes    no      Constant (no NVM storage)       │
 └──────────────────────────────────────────────────────────────┘
```

---

## 5. Data Type Encoding

CiA 301 defines standard data types with fixed index/code assignments:

```
 ┌──────────────────────────────────────────────────────────────────────────┐
 │              Standard CANopen Data Types in EDS                          │
 │                                                                          │
 │  DataType  Name           Bytes   Notes                                  │
 │  ────────  ─────────────  ─────   ─────────────────────────────────────  │
 │  0x0001    BOOLEAN          1     0x00=FALSE, 0x01=TRUE                  │
 │  0x0002    INTEGER8         1     signed                                 │
 │  0x0003    INTEGER16        2     signed, little-endian                  │
 │  0x0004    INTEGER32        4     signed, little-endian                  │
 │  0x0005    UNSIGNED8        1     unsigned                               │
 │  0x0006    UNSIGNED16       2     unsigned, little-endian                │
 │  0x0007    UNSIGNED32       4     unsigned, little-endian                │
 │  0x0008    REAL32           4     IEEE 754 single                        │
 │  0x0009    VISIBLE_STRING   var   ASCII, no NUL terminator               │
 │  0x000A    OCTET_STRING     var   raw bytes                              │
 │  0x000B    UNICODE_STRING   var   UTF-16LE                               │
 │  0x000F    DOMAIN           var   arbitrary binary blob                  │
 │  0x0010    INTEGER24        3     signed                                 │
 │  0x0011    REAL64           8     IEEE 754 double                        │
 │  0x0015    INTEGER64        8     signed                                 │
 │  0x0016    UNSIGNED24       3                                            │
 │  0x001B    UNSIGNED64       8                                            │
 │  0x0020    PDO_COMM_PAR     –     RECORD (predefined, no separate entry) │
 │  0x0021    PDO_MAPPING      –     RECORD (predefined)                    │
 └──────────────────────────────────────────────────────────────────────────┘
```

Variable-length objects (VISIBLE_STRING, OCTET_STRING, DOMAIN) encode their size in
the `LowLimit` / `HighLimit` fields or leave them absent to indicate unlimited.

### 5.1 DefaultValue Encoding Rules

```
 ┌─────────────────────────────────────────────────────────────────────────┐
 │  DefaultValue Encoding per DataType                                     │
 │                                                                         │
 │  DataType          Format         Example                               │
 │  ────────────────  ─────────────  ─────────────────────────────────     │
 │  INTEGERn           decimal/hex    -128  or  0xFF80                     │
 │  UNSIGNEDn          decimal/hex    255   or  0x00FF                     │
 │  BOOLEAN            0 or 1         1                                    │
 │  REAL32/REAL64      decimal float  3.14159                              │
 │  VISIBLE_STRING     bare string    Hello World  (no quotes in EDS)      │
 │  OCTET_STRING       space-hex      41 43 4D 45  (ASCII "ACME")          │
 │  DOMAIN             (no default)   (omit key)                           │
 └─────────────────────────────────────────────────────────────────────────┘
```

---

## 6. Optional Sections

### 6.1 [DeviceComissioning] — DCF only

See Section 7.

### 6.2 [TypeDefinitions]

Device-specific compound types (rarely used in practice):

```ini
[TypeDefinitions]
SupportedObjects=1
1=0x0023

[0023]
ParameterName=StatusWord
ObjectType=0x05         ; DEFTYPE
DataType=0x0006         ; UNSIGNED16
```

### 6.3 [UserDefinedAreas]

Vendor-specific extension block: manufacturer name + free keys. Tools must ignore
sections they do not recognise.

### 6.4 PDO Communication Parameter Section (0x1400–0x15FF / 0x1800–0x19FF)

```ini
[1400]
ParameterName=Receive PDO Communication Parameter 1
ObjectType=0x9
SubNumber=3

[1400sub0]
ParameterName=Highest sub-index supported
ObjectType=0x7
DataType=0x0005
AccessType=ro
DefaultValue=0x02
PDOMapping=0

[1400sub1]
ParameterName=COB-ID use by RPDO1
ObjectType=0x7
DataType=0x0007
AccessType=rw
DefaultValue=$NODEID+0x200   ; DCF macro; in pure EDS use 0x00000200
PDOMapping=0

[1400sub2]
ParameterName=Transmission type
ObjectType=0x7
DataType=0x0005
AccessType=rw
DefaultValue=0xFE            ; 0xFE = event-driven asynchronous
PDOMapping=0
```

---

## 7. DCF Files — Node-ID-Parameterised Variant

A DCF extends EDS with two additions:

**A) [DeviceComissioning] section** — stores the assigned node-ID and baud rate:

```ini
[DeviceComissioning]
NodeID=0x05
Baudrate=250
NetNumber=0
NetworkName=Plant Bus A
CANopenManager=0
LSS_SerialNumber=0x00000000
```

**B) `ParameterValue` key** — added alongside `DefaultValue` in any object sub-section:

```ini
[1400sub1]
ParameterName=COB-ID use by RPDO1
ObjectType=0x7
DataType=0x0007
AccessType=rw
DefaultValue=0x00000200
ParameterValue=0x00000205   ; 0x200 + NodeID(5) — resolved at commissioning time
PDOMapping=0
```

The `$NODEID` macro in a DCF gets substituted with the concrete node-ID when the
configuration tool writes the file. A master NMT stack can then read the DCF and
programme the device via SDO download without any manual recalculation.

### 7.1 EDS vs. DCF Comparison

```
 ┌─────────────────────────────────────────────────────────────────────────────┐
 │                       EDS  vs.  DCF  at a Glance                            │
 │                                                                             │
 │  Aspect                  EDS                      DCF                       │
 │  ──────────────────────  ─────────────────────    ───────────────────────   │
 │  File extension          .eds                     .dcf                      │
 │  Node-ID                 absent / $NODEID macro   concrete value in         │
 │                                                   [DeviceComissioning]      │
 │  DefaultValue            device default           device default            │
 │  ParameterValue          absent                   network-specific value    │
 │  Reusable across nodes?  yes                      one node / one network    │
 │  Created by              device manufacturer      configuration tool        │
 │  Used by                 tools, catalogues        NMT master / auto-cfg     │
 │  CiA spec                CiA 306                  CiA 306                   │
 └─────────────────────────────────────────────────────────────────────────────┘
```

### 7.2 $NODEID Macro Resolution

The `$NODEID` placeholder is an arithmetic expression evaluated at commissioning time:

```
 DefaultValue = $NODEID + 0x200
 NodeID       = 5
 Resolved     = 5 + 512 = 517 = 0x00000205
```

Allowed arithmetic: `+`, `-`, `*`, `/` (integer), parentheses.

---

## 8. Generating EDS from C/C++ Code

Generating EDS programmatically ensures the file stays in sync with the firmware's
Object Dictionary. The pattern is straightforward: build an in-memory OD description,
then serialise to INI format.

### 8.1 Data Structures

```cpp
// eds_types.hpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <optional>

enum class ObjType   : uint8_t { VAR = 0x07, ARRAY = 0x08, RECORD = 0x09 };
enum class AccessType { RO, WO, RW, CONST };

struct SubEntry {
    uint8_t     subIndex;
    std::string paramName;
    uint16_t    dataType;       // CiA 301 type code (e.g. 0x0007 = UINT32)
    AccessType  access;
    std::string defaultValue;   // string representation
    bool        pdoMapping;
};

struct OdEntry {
    uint16_t            index;
    std::string         paramName;
    ObjType             objectType;
    std::vector<SubEntry> subs;  // empty for VAR (use subs[0] instead)
};

struct EdsDeviceInfo {
    std::string vendorName;
    uint32_t    vendorNumber;
    std::string productName;
    uint32_t    productNumber;
    uint32_t    revisionNumber;
    uint8_t     nrOfRxPdo;
    uint8_t     nrOfTxPdo;
};
```

### 8.2 EDS Writer

```cpp
// eds_writer.cpp
#include "eds_types.hpp"
#include <cstdio>
#include <ctime>
#include <algorithm>

static const char* accessStr(AccessType a) {
    switch (a) {
        case AccessType::RO:    return "ro";
        case AccessType::WO:    return "wo";
        case AccessType::RW:    return "rw";
        case AccessType::CONST: return "const";
    }
    return "rw";
}

static void writeFileInfo(FILE* f, const std::string& filename) {
    time_t now = time(nullptr);
    struct tm* t = localtime(&now);
    char date[16], time_s[12];
    strftime(date,   sizeof(date),   "%m-%d-%Y", t);
    strftime(time_s, sizeof(time_s), "%I:%M%p",  t);

    fprintf(f, "[FileInfo]\n");
    fprintf(f, "FileName=%s\n", filename.c_str());
    fprintf(f, "FileVersion=1\n");
    fprintf(f, "FileRevision=1\n");
    fprintf(f, "EDSVersion=4.0\n");
    fprintf(f, "Description=\n");
    fprintf(f, "CreationTime=%s\n", time_s);
    fprintf(f, "CreationDate=%s\n", date);
    fprintf(f, "CreatedBy=EdsGenerator\n");
    fprintf(f, "ModificationTime=%s\n", time_s);
    fprintf(f, "ModificationDate=%s\n", date);
    fprintf(f, "ModifiedBy=EdsGenerator\n\n");
}

static void writeDeviceInfo(FILE* f, const EdsDeviceInfo& di) {
    fprintf(f, "[DeviceInfo]\n");
    fprintf(f, "VendorName=%s\n",    di.vendorName.c_str());
    fprintf(f, "VendorNumber=0x%08X\n",  di.vendorNumber);
    fprintf(f, "ProductName=%s\n",   di.productName.c_str());
    fprintf(f, "ProductNumber=0x%08X\n", di.productNumber);
    fprintf(f, "RevisionNumber=0x%08X\n",di.revisionNumber);
    fprintf(f, "OrderCode=\n");
    // advertise all common baud rates as supported
    for (int b : {10,20,50,125,250,500,800,1000})
        fprintf(f, "BaudRate_%d=1\n", b);
    fprintf(f, "SimpleBootUpMaster=0\n");
    fprintf(f, "SimpleBootUpSlave=1\n");
    fprintf(f, "Granularity=0\n");
    fprintf(f, "DynamicChannelsSupported=0\n");
    fprintf(f, "GroupMessaging=0\n");
    fprintf(f, "NrOfRXPDO=%u\n", di.nrOfRxPdo);
    fprintf(f, "NrOfTXPDO=%u\n", di.nrOfTxPdo);
    fprintf(f, "LSS_Supported=0\n\n");
}

static void writeObjectSection(FILE* f, const OdEntry& obj) {
    char idxStr[8];
    snprintf(idxStr, sizeof(idxStr), "%04X", obj.index);

    if (obj.objectType == ObjType::VAR) {
        // VAR: everything in the top-level section
        const SubEntry& s = obj.subs[0];
        fprintf(f, "[%s]\n", idxStr);
        fprintf(f, "ParameterName=%s\n",  obj.paramName.c_str());
        fprintf(f, "ObjectType=0x7\n");
        fprintf(f, "DataType=0x%04X\n",   s.dataType);
        fprintf(f, "AccessType=%s\n",     accessStr(s.access));
        if (!s.defaultValue.empty())
            fprintf(f, "DefaultValue=%s\n", s.defaultValue.c_str());
        fprintf(f, "PDOMapping=%d\n\n",   s.pdoMapping ? 1 : 0);
        return;
    }

    // ARRAY or RECORD: header section + sub-sections
    uint8_t objCode = (obj.objectType == ObjType::ARRAY) ? 0x08 : 0x09;
    fprintf(f, "[%s]\n", idxStr);
    fprintf(f, "ParameterName=%s\n",  obj.paramName.c_str());
    fprintf(f, "ObjectType=0x%X\n",   objCode);
    fprintf(f, "SubNumber=%zu\n\n",   obj.subs.size());

    for (const auto& s : obj.subs) {
        fprintf(f, "[%ssub%X]\n", idxStr, s.subIndex);
        fprintf(f, "ParameterName=%s\n",  s.paramName.c_str());
        fprintf(f, "ObjectType=0x7\n");
        fprintf(f, "DataType=0x%04X\n",   s.dataType);
        fprintf(f, "AccessType=%s\n",     accessStr(s.access));
        if (!s.defaultValue.empty())
            fprintf(f, "DefaultValue=%s\n", s.defaultValue.c_str());
        fprintf(f, "PDOMapping=%d\n\n",   s.pdoMapping ? 1 : 0);
    }
}

bool writeEds(const char* path,
              const EdsDeviceInfo& di,
              const std::vector<OdEntry>& od)
{
    FILE* f = fopen(path, "w");
    if (!f) return false;

    std::string filename = path;
    auto pos = filename.rfind('/');
    if (pos != std::string::npos) filename = filename.substr(pos + 1);

    writeFileInfo(f, filename);
    writeDeviceInfo(f, di);

    // Dummy usage (none used)
    fprintf(f, "[DummyUsage]\n");
    for (int i = 1; i <= 7; i++) fprintf(f, "Dummy%04d=0\n", i);
    fprintf(f, "\n[Comments]\nLines=0\n\n");

    // Object index list
    fprintf(f, "[Objects]\nSupportedObjects=%zu\n", od.size());
    for (size_t i = 0; i < od.size(); i++)
        fprintf(f, "%zu=0x%04X\n", i+1, od[i].index);
    fprintf(f, "\n");

    // Individual object sections
    for (const auto& obj : od)
        writeObjectSection(f, obj);

    fclose(f);
    return true;
}
```

### 8.3 Usage Example — Building a Drive OD

```cpp
// main_generate.cpp
#include "eds_types.hpp"
#include <vector>

int main() {
    EdsDeviceInfo di{
        "ACME Drives", 0x00000042,
        "AxisController", 0x00000001,
        0x00010000, 1, 1
    };

    std::vector<OdEntry> od;

    // 0x1000 — Device Type (VAR)
    od.push_back({0x1000, "Device type", ObjType::VAR, {
        {0, "", 0x0007, AccessType::RO, "0x00020192", false}
        //                                 ^^^^^^^^ CiA 402 drive profile
    }});

    // 0x1001 — Error Register (VAR)
    od.push_back({0x1001, "Error register", ObjType::VAR, {
        {0, "", 0x0005, AccessType::RO, "0x00", false}
    }});

    // 0x1018 — Identity Object (RECORD)
    od.push_back({0x1018, "Identity Object", ObjType::RECORD, {
        {0, "Highest sub-index supported", 0x0005, AccessType::RO, "0x04", false},
        {1, "Vendor-ID",        0x0007, AccessType::RO, "0x00000042", false},
        {2, "Product code",     0x0007, AccessType::RO, "0x00000001", false},
        {3, "Revision number",  0x0007, AccessType::RO, "0x00010000", false},
        {4, "Serial number",    0x0007, AccessType::RO, "0x00000000", false},
    }});

    // 0x1400 — RPDO1 Communication Parameter (RECORD)
    od.push_back({0x1400, "Receive PDO Communication Parameter 1",
        ObjType::RECORD, {
        {0, "Highest sub-index supported", 0x0005, AccessType::RO, "0x02", false},
        {1, "COB-ID use by RPDO1",         0x0007, AccessType::RW, "0x00000200", false},
        {2, "Transmission type",           0x0005, AccessType::RW, "0xFE",       false},
    }});

    // 0x1600 — RPDO1 Mapping (ARRAY)
    od.push_back({0x1600, "Receive PDO Mapping Parameter 1", ObjType::ARRAY, {
        {0, "Highest sub-index supported", 0x0005, AccessType::RW, "0x02", false},
        {1, "1st mapped object",  0x0007, AccessType::RW, "0x60400010", false},
        {2, "2nd mapped object",  0x0007, AccessType::RW, "0x607A0020", false},
    }});

    // 0x6040 — Controlword (CiA 402)
    od.push_back({0x6040, "Controlword", ObjType::VAR, {
        {0, "", 0x0006, AccessType::RW, "0x0000", true}  // PDO-mappable
    }});

    // 0x607A — Target Position (CiA 402)
    od.push_back({0x607A, "Target position", ObjType::VAR, {
        {0, "", 0x0004, AccessType::RW, "0x00000000", true}
    }});

    writeEds("AxisController.eds", di, od);
    return 0;
}
```

### 8.4 Generating a DCF from EDS at Commissioning

```cpp
// dcf_writer.cpp  — extends the EDS writer concept

struct DcfCommissioning {
    uint8_t  nodeId;
    uint16_t baudrate;   // kbit/s
    std::string networkName;
};

// Resolve $NODEID macro in a DefaultValue string
static std::string resolveNodeId(const std::string& val, uint8_t nodeId) {
    const std::string token = "$NODEID";
    auto p = val.find(token);
    if (p == std::string::npos) return val;

    // Parse everything after the macro as an arithmetic offset
    std::string rest = val.substr(p + token.size());
    long offset = 0;
    if (!rest.empty()) {
        char* end;
        offset = strtol(rest.c_str(), &end, 0);
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "0x%08X",
             (unsigned)(nodeId + offset));
    return buf;
}

bool writeDcf(const char* path,
              const EdsDeviceInfo& di,
              const std::vector<OdEntry>& od,
              const DcfCommissioning& dc)
{
    // Re-use the EDS writer but also:
    //  1. Append [DeviceComissioning] section
    //  2. Add ParameterValue = resolved DefaultValue for COB-ID entries

    FILE* f = fopen(path, "w");
    if (!f) return false;

    // Write all standard EDS content first
    // (call the individual helper functions here)

    // ... (same as writeEds) ...

    // [DeviceComissioning]
    fprintf(f, "[DeviceComissioning]\n");
    fprintf(f, "NodeID=0x%02X\n",    dc.nodeId);
    fprintf(f, "Baudrate=%u\n",      dc.baudrate);
    fprintf(f, "NetNumber=0\n");
    fprintf(f, "NetworkName=%s\n",   dc.networkName.c_str());
    fprintf(f, "CANopenManager=0\n");
    fprintf(f, "LSS_SerialNumber=0x00000000\n\n");

    // For each sub-entry that has a $NODEID macro, emit ParameterValue
    for (const auto& obj : od) {
        for (const auto& s : obj.subs) {
            if (s.defaultValue.find("$NODEID") != std::string::npos) {
                // The actual section was already written above;
                // in a real implementation rewrite or append the section.
                std::string pv = resolveNodeId(s.defaultValue, dc.nodeId);
                // (append ParameterValue=pv to the already-written section)
                fprintf(f, "; Resolved: %s -> %s (node %u)\n",
                        s.defaultValue.c_str(), pv.c_str(), dc.nodeId);
            }
        }
    }

    fclose(f);
    return true;
}
```

---

## 9. Parsing EDS at Runtime in C/C++

An NMT master or configuration tool needs to parse EDS/DCF files at runtime. The
following is a lean, allocation-minimising parser suitable for embedded Linux or
desktop commissioning software.

### 9.1 Architecture

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │                        EDS Parser Architecture                               │
 │                                                                              │
 │  ┌────────────┐   raw bytes   ┌─────────────────┐  tokens   ┌────────────┐   │
 │  │  EDS File  │ ────────────► │   Line Scanner  │ ────────► │  Section   │   │
 │  │  (disk)    │               │  (fgets loop)   │           │  Detector  │   │
 │  └────────────┘               └─────────────────┘           └────────────┘   │
 │                                                                    │         │
 │                                                                    ▼         │
 │                                                         ┌────────────────┐   │
 │                                                         │  Key=Value     │   │
 │                                                         │  Accumulator   │   │
 │                                                         └────────────────┘   │
 │                                                                    │         │
 │                                   ┌────────────────────────────────┤         │
 │                                   ▼                                ▼         │
 │                          ┌──────────────┐               ┌──────────────────┐ │
 │                          │  OdEntry     │               │ DeviceInfo /     │ │
 │                          │  Builder     │               │ FileInfo store   │ │
 │                          └──────────────┘               └──────────────────┘ │
 │                                   │                                          │
 │                                   ▼                                          │
 │                          ┌──────────────────────────────────────────────┐    │
 │                          │         std::map<uint32_t, SubEntry>         │    │
 │                          │  key = (index << 8) | subIndex               │    │
 │                          └──────────────────────────────────────────────┘    │
 └──────────────────────────────────────────────────────────────────────────────┘
```

### 9.2 INI Parser Core

```cpp
// eds_parser.hpp
#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <functional>

// Callback: section name, key, value
using IniCallback = std::function<void(
    const std::string& section,
    const std::string& key,
    const std::string& value)>;

/**
 * Minimal INI parser.
 * - Strips ; comments and leading/trailing whitespace
 * - Section names are lowercased for comparison
 * - Returns false on file-open error
 */
bool parseIni(const char* path, IniCallback cb);
```

```cpp
// eds_parser.cpp
#include "eds_parser.hpp"
#include <cstdio>
#include <cctype>
#include <algorithm>

static std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return s;
}

bool parseIni(const char* path, IniCallback cb) {
    FILE* f = fopen(path, "r");
    if (!f) return false;

    char line[512];
    std::string section;

    while (fgets(line, sizeof(line), f)) {
        std::string s = trim(line);

        // Skip empty lines and comments
        if (s.empty() || s[0] == ';') continue;

        if (s[0] == '[') {
            // Section header
            auto end = s.find(']');
            if (end != std::string::npos)
                section = toLower(s.substr(1, end - 1));
            continue;
        }

        // Key=Value
        auto eq = s.find('=');
        if (eq == std::string::npos) continue;

        std::string key = trim(s.substr(0, eq));
        std::string val = trim(s.substr(eq + 1));

        // Strip inline comment
        auto sc = val.find(';');
        if (sc != std::string::npos)
            val = trim(val.substr(0, sc));

        cb(section, toLower(key), val);
    }

    fclose(f);
    return true;
}
```

### 9.3 EDS Object Dictionary Builder

```cpp
// eds_od_builder.hpp
#pragma once
#include "eds_parser.hpp"
#include <map>
#include <string>
#include <cstdint>

struct ParsedSubEntry {
    std::string paramName;
    uint16_t    dataType    = 0;
    uint8_t     objectType  = 0;
    std::string accessType;
    std::string defaultValue;
    std::string paramValue;  // DCF only
    bool        pdoMapping  = false;
};

// Key: packed (index << 8) | subIndex
using ParsedOD = std::map<uint32_t, ParsedSubEntry>;

struct ParsedDeviceInfo {
    std::string vendorName;
    uint32_t    vendorNumber  = 0;
    std::string productName;
    uint32_t    productNumber = 0;
    uint32_t    revisionNumber= 0;
    uint8_t     nodeId        = 0xFF; // DCF only
    uint16_t    baudrate      = 0;    // DCF only
};

struct ParsedEds {
    ParsedDeviceInfo  device;
    ParsedOD          od;
};

ParsedEds loadEds(const char* path);
```

```cpp
// eds_od_builder.cpp
#include "eds_od_builder.hpp"
#include <cstdlib>
#include <cstring>
#include <cctype>

static uint32_t parseHexDec(const std::string& s) {
    if (s.empty()) return 0;
    return (uint32_t)strtoul(s.c_str(), nullptr, 0);
}

// Decode section name like "1600sub2" -> (index=0x1600, sub=2, valid=true)
static bool decodeSectionName(const std::string& sec,
                               uint16_t& index, uint8_t& sub)
{
    // Must be 4 hex digits, optionally followed by "sub" + hex digits
    if (sec.size() < 4) return false;
    char* end;
    index = (uint16_t)strtoul(sec.c_str(), &end, 16);
    if (end == sec.c_str()) return false;

    if (*end == '\0') {
        sub = 0xFF; // no sub specified — top-level VAR or header
        return true;
    }
    if (strncmp(end, "sub", 3) == 0) {
        sub = (uint8_t)strtoul(end + 3, nullptr, 16);
        return true;
    }
    return false;
}

ParsedEds loadEds(const char* path) {
    ParsedEds result;

    // Temporary per-section accumulator
    std::string  curSection;
    uint16_t     curIndex  = 0;
    uint8_t      curSub    = 0xFF;
    ParsedSubEntry curEntry;
    bool         inOdSection = false;

    auto flushEntry = [&]() {
        if (!inOdSection) return;
        uint32_t key = ((uint32_t)curIndex << 8) | (curSub == 0xFF ? 0 : curSub);
        // For VAR (no sub in section name), sub=0
        result.od[key] = curEntry;
        curEntry = {};
        inOdSection = false;
    };

    parseIni(path, [&](const std::string& sec,
                        const std::string& key,
                        const std::string& val)
    {
        if (sec != curSection) {
            flushEntry();
            curSection = sec;

            uint16_t idx; uint8_t sub;
            if (decodeSectionName(sec, idx, sub)) {
                curIndex = idx;
                curSub   = sub;
                inOdSection = true;
            } else {
                inOdSection = false;
            }
        }

        // [DeviceInfo]
        if (sec == "deviceinfo") {
            if      (key == "vendorname")     result.device.vendorName   = val;
            else if (key == "vendornumber")   result.device.vendorNumber = parseHexDec(val);
            else if (key == "productname")    result.device.productName  = val;
            else if (key == "productnumber")  result.device.productNumber= parseHexDec(val);
            else if (key == "revisionnumber") result.device.revisionNumber=parseHexDec(val);
            return;
        }
        // [DeviceComissioning] (DCF)
        if (sec == "devicecomissioning") {
            if      (key == "nodeid")   result.device.nodeId   = (uint8_t)parseHexDec(val);
            else if (key == "baudrate") result.device.baudrate = (uint16_t)parseHexDec(val);
            return;
        }

        // Object sub-section fields
        if (inOdSection) {
            if      (key == "parametername") curEntry.paramName    = val;
            else if (key == "datatype")      curEntry.dataType     = (uint16_t)parseHexDec(val);
            else if (key == "objecttype")    curEntry.objectType   = (uint8_t) parseHexDec(val);
            else if (key == "accesstype")    curEntry.accessType   = val;
            else if (key == "defaultvalue")  curEntry.defaultValue = val;
            else if (key == "parametervalue")curEntry.paramValue   = val;
            else if (key == "pdomapping")    curEntry.pdoMapping   = (val != "0");
        }
    });

    flushEntry(); // flush last section
    return result;
}
```

### 9.4 Using the Parser — SDO Auto-Configuration

A typical NMT master workflow reads a DCF and downloads `ParameterValue` entries to the
device via SDO before starting the network:

```cpp
// sdo_autocfg.cpp  (pseudo-code — SDO transport abstracted)
#include "eds_od_builder.hpp"
#include "sdo_client.hpp"   // hypothetical SDO client abstraction

void autoConfigureFromDcf(SdoClient& sdo,
                          const char* dcfPath)
{
    ParsedEds eds = loadEds(dcfPath);

    printf("Configuring node 0x%02X (%s %s)\n",
           eds.device.nodeId,
           eds.device.vendorName.c_str(),
           eds.device.productName.c_str());

    for (auto& [packedKey, entry] : eds.od) {
        // Only write entries that have an explicit ParameterValue
        if (entry.paramValue.empty()) continue;

        uint16_t index  = (uint16_t)(packedKey >> 8);
        uint8_t  subIdx = (uint8_t) (packedKey & 0xFF);

        uint32_t value = (uint32_t)strtoul(entry.paramValue.c_str(), nullptr, 0);

        printf("  SDO download: [%04X sub%02X] = 0x%08X  (%s)\n",
               index, subIdx, value, entry.paramName.c_str());

        int rc = sdo.download(eds.device.nodeId, index, subIdx,
                              &value, sizeof(uint32_t));
        if (rc != 0) {
            fprintf(stderr, "  ERROR: SDO abort 0x%08X\n", sdo.lastAbortCode());
        }
    }
}
```

### 9.5 EDS Validation Checks

```cpp
// eds_validator.cpp
#include "eds_od_builder.hpp"
#include <cstdio>

struct ValidationResult {
    bool ok = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;

    void err(const std::string& msg) { ok = false; errors.push_back(msg); }
    void warn(const std::string& msg){ warnings.push_back(msg); }
};

ValidationResult validateEds(const ParsedEds& eds) {
    ValidationResult r;

    // Check: Device Identity Object (0x1018) must exist
    uint32_t idk = (0x1018u << 8) | 0;
    if (eds.od.find(idk) == eds.od.end())
        r.err("Missing mandatory object 0x1018 (Identity Object)");

    // Check: Device Type (0x1000) must exist
    if (eds.od.find((0x1000u << 8)) == eds.od.end())
        r.err("Missing mandatory object 0x1000 (Device Type)");

    // Check: Error Register (0x1001) must exist
    if (eds.od.find((0x1001u << 8)) == eds.od.end())
        r.err("Missing mandatory object 0x1001 (Error Register)");

    // Warn: Vendor ID of 0 is suspicious
    uint32_t vidKey = (0x1018u << 8) | 0x01;
    auto it = eds.od.find(vidKey);
    if (it != eds.od.end()) {
        uint32_t vid = (uint32_t)strtoul(it->second.defaultValue.c_str(), nullptr, 0);
        if (vid == 0) r.warn("Vendor-ID is 0x00000000 (unregistered)");
    }

    // Warn: PDO-mapped objects should have appropriate access
    for (auto& [k, e] : eds.od) {
        if (e.pdoMapping && e.accessType == "ro") {
            uint16_t idx = (uint16_t)(k >> 8);
            // TPDO source objects being ro is fine; warn for RPDO targets
            if (idx < 0x6000)
                r.warn("PDO-mappable object at 0x" +
                       std::to_string(idx) + " is read-only");
        }
    }

    return r;
}
```

---

## 10. Toolchain Integration

```
 ┌────────────────────────────────────────────────────────────────────────────────┐
 │                    CANopen EDS/DCF Toolchain Ecosystem                         │
 │                                                                                │
 │  ┌──────────────────────────────────────────────────────────────────────────┐  │
 │  │                       DESIGN / EDIT PHASE                                │  │
 │  │                                                                          │  │
 │  │  ┌───────────────────┐   ┌───────────────────┐   ┌──────────────────┐    │  │
 │  │  │   CiA EDS Editor  │   │  CANopen Magic    │   │   ixxat Plugin   │    │  │
 │  │  │  (free reference  │   │  (SYS TEC /       │   │   or similar     │    │  │
 │  │  │   implementation) │   │   Emotas based)   │   │   OEM tools      │    │  │
 │  │  └────────┬──────────┘   └────────┬──────────┘   └─────────┬────────┘    │  │
 │  │           │                       │                        │             │  │
 │  └───────────┼───────────────────────┼────────────────────────┼─────────────┘  │
 │              │      EDS / DCF files  │                        │                │
 │  ┌───────────▼───────────────────────▼────────────────────────▼─────────────┐  │
 │  │                       CONFIGURATION PHASE                                │  │
 │  │                                                                          │  │
 │  │  ┌───────────────────────────────────────────────────────────────────┐   │  │
 │  │  │  Peak PCAN-Explorer 6              CANopen Magic Network Manager  │   │  │
 │  │  │  • Import .eds → auto-decode PDOs  • Drag-drop EDS onto bus       │   │  │
 │  │  │  • Read/write OD via SDO           • Auto-generate DCF per node   │   │  │
 │  │  │  • Record/replay CAN frames        • Download config via SDO      │   │  │
 │  │  └───────────────────────────────────────────────────────────────────┘   │  │
 │  └──────────────────────────────────────────────────────────────────────────┘  │
 │              │                                                                 │
 │              │  CAN bus (ISO 11898)                                            │
 │  ┌───────────▼──────────────────────────────────────────────────────────────┐  │
 │  │  Device under test:  Node 0x05                                           │  │
 │  │  Responds to SDO, NMT, EMCY, PDOs  ◄── firmware uses OD from EDS         │  │
 │  └──────────────────────────────────────────────────────────────────────────┘  │
 └────────────────────────────────────────────────────────────────────────────────┘
```

### 10.1 CANopen Magic (SYS TEC / Emotas-based tools)

CANopen Magic is a popular Windows-based CANopen configuration and monitoring suite.

**EDS workflow:**

1. File → Import EDS → select `*.eds` → device appears in Device Tree.
2. Double-click device → Object Dictionary view shows all parameters.
3. Right-click any object → "Download value" sends an SDO write.
4. Network → "Store all parameters" (0x1010 sub 1 = 0x65766173) persists to NVM.
5. File → Export DCF → creates a node-specific `*.dcf` with all current values.

**EDS generation from CANopen Magic:**

CANopen Magic can import CAN trace logs and attempt to build an EDS heuristically —
useful for reverse-engineering undocumented devices.

### 10.2 Peak PCAN-Explorer 6

PCAN-Explorer is primarily a CAN protocol analyser with CANopen awareness.

**Key EDS features:**

```
 PCAN-Explorer EDS Integration Flow
 ───────────────────────────────────
  1.  Open "CANopen" plugin tab
  2.  Assign .eds file per node-ID  (Node → Properties → EDS)
  3.  Incoming CAN frames are now decoded symbolically:
      [0x285]  TPDO1 from Node 5:
               StatusWord  = 0x0237  (Operation Enabled)
               ActualPos   = 0x0000A400  (41984 counts)
  4.  SDO panel: browse OD, read/write values
  5.  DCF export: save configured parameters to .dcf
```

C API integration (for automated test frameworks using the PCAN SDK):

```cpp
// pcan_eds_autotest.cpp
// Uses PCAN Basic + a separate EDS parser; PCAN SDK does not expose EDS API

#include <PCANBasic.h>
#include "eds_od_builder.hpp"
#include "sdo_codec.hpp"  // proprietary SDO frame builder

// Verify that a live device matches its EDS defaults
bool verifyDeviceVsEds(TPCANHandle channel, uint8_t nodeId,
                       const char* edsPath)
{
    ParsedEds eds = loadEds(edsPath);
    bool pass = true;

    for (auto& [pk, entry] : eds.od) {
        if (entry.accessType != "ro" && entry.accessType != "const") continue;
        if (entry.defaultValue.empty()) continue;

        uint16_t idx = (uint16_t)(pk >> 8);
        uint8_t  sub = (uint8_t)(pk & 0xFF);

        // Build SDO upload request
        TPCANMsg req = buildSdoUploadRequest(nodeId, idx, sub);
        CAN_Write(channel, &req);

        TPCANMsg resp{};
        if (CAN_Read(channel, &resp, nullptr) != PCAN_ERROR_OK) {
            fprintf(stderr, "FAIL [%04X.%02X]: no response\n", idx, sub);
            pass = false;
            continue;
        }

        uint32_t live  = decodeSdoResponse(resp);
        uint32_t expected = (uint32_t)strtoul(
            entry.defaultValue.c_str(), nullptr, 0);

        if (live != expected) {
            fprintf(stderr, "MISMATCH [%04X.%02X]: live=0x%08X expected=0x%08X\n",
                    idx, sub, live, expected);
            pass = false;
        }
    }
    return pass;
}
```

### 10.3 CiA EDS Editor

The CiA EDS Editor (available from the CAN in Automation association) is the reference
tool for authoring valid EDS files. It enforces CiA 306 compliance including:

- Correct object type / data type combinations.
- Mandatory object presence (0x1000, 0x1001, 0x1018).
- PDO mapping validity checks.
- Sub-index count consistency.

**Typical CI/CD pipeline:**

```
 ┌───────────────────────────────────────────────────────────────────────────┐
 │                    CI / CD EDS Validation Pipeline                        │
 │                                                                           │
 │  firmware/od.h  ──►  generate_eds.cpp  ──►  device.eds                    │
 │  (source of truth)   (build step)           (generated artifact)          │
 │                                                    │                      │
 │                                                    ▼                      │
 │                                        ┌───────────────────────┐          │
 │                                        │  eds_validator (own)  │          │
 │                                        │  + canopen_ed CLI     │          │
 │                                        │  (CiA EDS Editor cmd) │          │
 │                                        └───────────────────────┘          │
 │                                                    │                      │
 │                              pass ◄────────────────┴──────────► fail      │
 │                               │                                   │       │
 │                         merge / tag                          block PR     │
 └───────────────────────────────────────────────────────────────────────────┘
```

---

## 11. Common Pitfalls and Best Practices

### 11.1 Pitfalls

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │  Common EDS/DCF Mistakes                                                     │
 │                                                                              │
 │  Mistake                          Consequence                                │
 │  ────────────────────────────────  ────────────────────────────────────────  │
 │  NrOfRXPDO/NrOfTXPDO mismatch     Tools refuse to load the EDS               │
 │  SubNumber wrong                   Sub-entries silently ignored              │
 │  Trailing space on DefaultValue    parseHexDec returns 0 silently            │
 │  $NODEID left in plain EDS        Some tools crash or misparse               │
 │  ObjectType=0x9 but accessing     Sub0 typically returns count only;         │
 │  sub0 directly via SDO             don't PDO-map sub0                        │
 │  Mixing decimal and hex without    0200 is 200 decimal, not 0x200!           │
 │  0x prefix                                                                   │
 │  Upper/lower case section names    Strict parsers reject "deviceInfo"        │
 │  Missing [Comments] section        Some tools segfault on absent section     │
 │  Encoding VISIBLE_STRING length    Use LowLimit=0/HighLimit=N, not DataType  │
 │  in DataType                                                                 │
 └──────────────────────────────────────────────────────────────────────────────┘
```

### 11.2 Best Practices

**Generate, don't hand-edit.** Maintain the Object Dictionary in a single C/C++ header
(`od_defs.h`) and generate the EDS file as a build step. This guarantees the EDS is
always consistent with the firmware.

```cpp
// od_defs.h  — single source of truth
#define OD_VENDOR_ID        0x00000042u
#define OD_PRODUCT_CODE     0x00000001u
#define OD_REVISION         0x00010000u
#define OD_NR_RPDO          1u
#define OD_NR_TPDO          1u

#define OD_IDX_DEVICE_TYPE  0x1000u
#define OD_IDX_ERR_REG      0x1001u
#define OD_IDX_IDENTITY     0x1018u
#define OD_IDX_RPDO1_COMM   0x1400u
#define OD_IDX_RPDO1_MAP    0x1600u
#define OD_IDX_CTRL_WORD    0x6040u
#define OD_IDX_TGT_POS      0x607Au
```

**Version-stamp the EDS.** Encode the firmware git hash or semantic version in the
`FileRevision` or `Description` field and in `RevisionNumber` of 0x1018sub3.

**Validate in CI.** Parse the generated EDS with your own validator and optionally with
the CiA EDS Editor CLI to catch regressions before release.

**Keep DCFs out of version control** (or only commit them tagged to a specific hardware
serial / factory configuration). They contain network-specific data that changes per
installation.

---

## 12. Summary

```
 ┌──────────────────────────────────────────────────────────────────────────────┐
 │                          Chapter 8 — Summary                                 │
 └──────────────────────────────────────────────────────────────────────────────┘

  EDS and DCF files are the lingua franca of CANopen device integration.
  Understanding their structure and tooling unlocks automated commissioning,
  interoperability testing, and robust multi-vendor system integration.

 ┌──────────────────────────────────────────────────────────────────────────────┐
 │  Topic                     Key Takeaway                                      │
 │  ────────────────────────  ────────────────────────────────────────────────  │
 │  File format               Windows INI; case-insensitive; ; comments         │
 │  EDS purpose               Describes OD structure & defaults; device-        │
 │                            generic; no node-ID                               │
 │  DCF purpose               Extends EDS with ParameterValue per entry and     │
 │                            concrete node-ID in [DeviceComissioning]          │
 │  $NODEID macro             Arithmetic expression resolved at commissioning   │
 │  Mandatory sections        [FileInfo], [DeviceInfo], [DummyUsage],           │
 │                            [Comments], [Objects]                             │
 │  Object types              VAR (0x07), ARRAY (0x08), RECORD (0x09)           │
 │  Sub-section naming        [1600sub1] = index 0x1600, sub-index 1            │
 │  Data types                0x0005=UINT8, 0x0007=UINT32, 0x0004=INT32 …       │
 │  Code generation           Write EDS as a build artefact from od_defs.h      │
 │  Runtime parsing           Lean INI scanner → map<uint32_t, SubEntry>        │
 │  SDO auto-config           Read DCF ParameterValue → SDO download loop       │
 │  CANopen Magic             GUI config, OD browse, DCF export                 │
 │  PCAN-Explorer             Protocol analysis + symbolic PDO decode           │
 │  CiA EDS Editor            Reference validator; use in CI pipelines          │
 │  Best practice             Generate EDS from code; validate in CI;           │
 │                            never hand-edit large EDS files                   │
 └──────────────────────────────────────────────────────────────────────────────┘

  Subsequent chapter:  09 · LSS Layer Setting Services
```

---

*Document generated 2026-05-20 · CANopen Series · Based on CiA 301 rev 4.3 and CiA 306 rev 1.3*# EDS & DCF Device Description Files

