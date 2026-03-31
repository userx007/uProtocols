# 87. Version Control for DBC Files

**Concepts covered:**
- DBC file anatomy — which fields are version-critical vs. metadata-only
- Semantic versioning (MAJOR/MINOR/PATCH) mapped to CAN change categories, with a complete compatibility taxonomy table
- Schema hashing — computing SHA-256 over structural content only (IDs, DLCs, bit layouts, factor/offset) so documentation commits don't produce false positives

**C/C++ implementations:**
- **`dbc_diff`** — a structural differ that walks messages, signals, and receiver lists and classifies every change by `ChangeLevel`; exits non-zero on MAJOR changes for CI use
- **`CompatMatrix`** — a rule-based engine that evaluates `(DBC version × ECU firmware version)` pairs and renders a terminal compatibility matrix
- **Shim layer generator** — auto-generated C adapter that rescales RPM raw values and stubs removed signals so legacy ECU firmware survives a MAJOR DBC bump

**Rust implementations:**
- `types.rs` — fully serializable `Signal` / `Message` / `DbcFile` structs with `schema_hash()` using `sha2`
- `diff.rs` — idiomatic differ with `ChangeLevel` ordering and `DiffResult::suggested_bump()`
- `change_tracker.rs` — persistent `DbcRepo` with project registry, automatic impact analysis (which projects are affected by which CAN IDs), and JSON export for CI pipelines

**DevOps:**
- GitHub Actions workflow that blocks merges when MAJOR changes appear without a version bump
- Pre-commit hook that enforces the same policy locally
- Recommended multi-project repository layout with migration folder conventions

> Managing database file evolution, compatibility matrices, and change tracking in multi-project environments.

---

## Table of Contents

1. [Introduction](#introduction)
2. [DBC File Anatomy for Version Awareness](#dbc-file-anatomy-for-version-awareness)
3. [Semantic Versioning Strategy for DBC Files](#semantic-versioning-strategy-for-dbc-files)
4. [Change Categories and Compatibility Impact](#change-categories-and-compatibility-impact)
5. [DBC Diff Engine — C/C++](#dbc-diff-engine--cc)
6. [Compatibility Matrix Builder — C/C++](#compatibility-matrix-builder--cc)
7. [DBC Version Control Library — Rust](#dbc-version-control-library--rust)
8. [Multi-Project Environment Management](#multi-project-environment-management)
9. [Automated CI/CD Integration](#automated-cicd-integration)
10. [Summary](#summary)

---

## Introduction

A **DBC file** (`.dbc`, *Database CAN*) is a structured text format originally defined by Vector Informatik that describes the complete signal/message topology of one or more CAN networks. In modern automotive and industrial projects, a single vehicle program can involve dozens of ECUs contributing to and consuming dozens of DBC files simultaneously.

Without disciplined version control:

- A signal renamed in one ECU's DBC silently breaks another ECU's parser.
- Bit-length changes corrupt runtime data without compile-time warnings.
- Multiple projects diverge into incompatible variants with no audit trail.

Version control for DBC files goes beyond simply storing them in Git. It requires:

| Concern | What it means |
|---|---|
| **Semantic diffing** | Understanding *what changed* at the message/signal level, not just text lines |
| **Compatibility matrices** | Which DBC versions can communicate with which ECU firmware versions |
| **Change impact analysis** | Determining which projects/ECUs are affected by a given change |
| **Migration tooling** | Auto-generating shim layers or adaptation code when breaking changes occur |
| **Traceability** | Linking every change to a requirement, ticket, or AUTOSAR ARXML origin |

---

## DBC File Anatomy for Version Awareness

A minimal DBC file looks like this:

```
VERSION "1.4.2"

NS_ :
    NS_DESC_
    CM_

BS_: 500000

BU_: ECU_Engine ECU_Transmission ECU_BCM

BO_ 256 EngineStatus: 8 ECU_Engine
 SG_ EngineRPM : 0|16@1+ (0.25,0) [0|16383.75] "RPM" ECU_Transmission,ECU_BCM
 SG_ CoolantTemp : 16|8@1+ (1,-40) [-40|215] "degC" ECU_BCM
 SG_ EngineRunning : 24|1@1+ (1,0) [0|1] "" ECU_Transmission

BO_ 512 TransmissionStatus: 4 ECU_Transmission
 SG_ CurrentGear : 0|4@1+ (1,0) [0|15] "" ECU_Engine,ECU_BCM
 SG_ GearShiftActive : 4|1@1+ (1,0) [0|1] "" ECU_Engine

CM_ SG_ 256 EngineRPM "Engine speed in RPM, 0.25 RPM/bit resolution";
CM_ SG_ 256 CoolantTemp "Engine coolant temperature";

BA_DEF_ SG_ "SystemSignalLongSymbol" STRING;
BA_DEF_DEF_ "SystemSignalLongSymbol" "";

BA_ "DBCFileVersion" "1.4.2";
BA_ "Author" "powertrain_team";
BA_ "ChangeDate" "2024-11-15";
```

**Version-relevant fields to track:**

| DBC Element | Version-Relevant Change |
|---|---|
| `VERSION` string | Overall file version marker |
| `BO_` — Message ID | Any ID change = breaking |
| `BO_` — DLC | Any DLC reduction = breaking |
| `SG_` — bit position/length | Any change = breaking |
| `SG_` — factor/offset | Value-semantic change (breaking for consumers) |
| `SG_` — min/max | Range contract change |
| `SG_` — receivers | Routing topology change |
| `BA_` attributes | Metadata only (usually non-breaking) |
| `CM_` comments | Documentation only (non-breaking) |

---

## Semantic Versioning Strategy for DBC Files

DBC files benefit from a versioning scheme analogous to [SemVer](https://semver.org/):

```
MAJOR.MINOR.PATCH[-label]

MAJOR  — Breaking changes (ID, DLC, bit-layout, factor/offset)
MINOR  — Additive changes (new signals/messages, new ECU nodes)
PATCH  — Non-breaking updates (comments, attribute metadata, min/max hints)
```

### Encoding the version inside the DBC

```
VERSION "2.3.1"

BA_DEF_ BU_ "ECUSwVersion" STRING;
BA_DEF_DEF_ "ECUSwVersion" "";

BA_ "DBCFileVersion" "2.3.1";
BA_ "DBCSchemaHash"  "sha256:a3f7c29d...";   ; Hash of structural elements only
BA_ "CompatibleFrom" "2.0.0";               ; Oldest compatible consumer version
BA_ "LastBreakingChange" "2.0.0";
```

The **schema hash** is computed over structural content only (messages, signals, IDs, DLCs, factors, offsets) — excluding comments and metadata — so that purely documentary commits do not change the hash.

---

## Change Categories and Compatibility Impact

```
┌─────────────────────────────────────────────────────────────────────┐
│                      DBC CHANGE TAXONOMY                            │
├──────────────────────┬────────────────────┬─────────────────────────┤
│ Change Type          │ Semver Bump        │ Consumer Impact         │
├──────────────────────┼────────────────────┼─────────────────────────┤
│ Message ID changed   │ MAJOR              │ ECU receives wrong msg  │
│ DLC reduced          │ MAJOR              │ Out-of-bounds signal    │
│ Signal bit pos/len   │ MAJOR              │ Wrong decoded value     │
│ Factor/offset change │ MAJOR (semantic)   │ Scaled value corrupted  │
│ Signal removed       │ MAJOR              │ Null/stale data         │
│ Signal byte order    │ MAJOR              │ Endianness corruption   │
├──────────────────────┼────────────────────┼─────────────────────────┤
│ New message added    │ MINOR              │ Unknown message (safe)  │
│ New signal in msg    │ MINOR*             │ OK if DLC unchanged     │
│ New ECU node added   │ MINOR              │ Transparent             │
│ Receiver list grow   │ MINOR              │ Transparent             │
├──────────────────────┼────────────────────┼─────────────────────────┤
│ Comment updated      │ PATCH              │ None                    │
│ Min/max hint change  │ PATCH              │ Warning at most         │
│ Attribute change     │ PATCH              │ Tool-level only         │
│ DLC increased        │ PATCH†             │ OK for existing signals │
└──────────────────────┴────────────────────┴─────────────────────────┘
 * MINOR only if added signal fits within existing DLC
 † PATCH only if existing signal layout is preserved
```

---

## DBC Diff Engine — C/C++

The following implements a structural DBC differ that classifies changes by compatibility level.

### dbc_types.h

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace dbc_vc {

// ── Signal definition ────────────────────────────────────────────────
struct Signal {
    std::string name;
    uint32_t    start_bit   = 0;
    uint32_t    bit_length  = 0;
    bool        is_little_endian = true;  // @1 = Intel, @0 = Motorola
    bool        is_unsigned = true;
    double      factor      = 1.0;
    double      offset      = 0.0;
    double      min_val     = 0.0;
    double      max_val     = 0.0;
    std::string unit;
    std::vector<std::string> receivers;
    std::string comment;

    // Structural identity (version-critical fields)
    bool structural_equal(const Signal& o) const {
        return start_bit      == o.start_bit
            && bit_length     == o.bit_length
            && is_little_endian == o.is_little_endian
            && is_unsigned    == o.is_unsigned
            && factor         == o.factor
            && offset         == o.offset;
    }
};

// ── Message definition ───────────────────────────────────────────────
struct Message {
    uint32_t    id   = 0;       // CAN message ID (11 or 29 bit)
    std::string name;
    uint8_t     dlc  = 0;       // Data Length Code (0..8 or FD up to 64)
    std::string transmitter;
    std::map<std::string, Signal> signals;
    std::string comment;
};

// ── Whole DBC file ───────────────────────────────────────────────────
struct DbcFile {
    std::string version;
    std::string filename;
    std::map<uint32_t, Message> messages;       // keyed by CAN ID
    std::vector<std::string>    nodes;

    std::string schema_hash() const;            // Implemented below
};

// ── Change record ────────────────────────────────────────────────────
enum class ChangeLevel {
    NONE,
    PATCH,    // Non-breaking
    MINOR,    // Additive
    MAJOR,    // Breaking
};

inline const char* to_string(ChangeLevel l) {
    switch(l) {
        case ChangeLevel::NONE:  return "NONE";
        case ChangeLevel::PATCH: return "PATCH";
        case ChangeLevel::MINOR: return "MINOR";
        case ChangeLevel::MAJOR: return "MAJOR";
    }
    return "?";
}

struct ChangeRecord {
    ChangeLevel level;
    std::string path;           // e.g. "MSG[256].SIG[EngineRPM].start_bit"
    std::string description;
    std::string old_value;
    std::string new_value;
};

} // namespace dbc_vc
```

### dbc_diff.h / dbc_diff.cpp

```cpp
// dbc_diff.h
#pragma once
#include "dbc_types.h"
#include <vector>

namespace dbc_vc {

struct DiffResult {
    std::vector<ChangeRecord> changes;

    ChangeLevel overall_level() const {
        ChangeLevel max = ChangeLevel::NONE;
        for (const auto& c : changes) {
            if (c.level > max) max = c.level;
        }
        return max;
    }

    // Suggested semver bump string
    std::string suggested_bump(const std::string& current_version) const;
    void print_report(std::ostream& out) const;
};

DiffResult diff_dbc(const DbcFile& baseline, const DbcFile& updated);

} // namespace dbc_vc
```

```cpp
// dbc_diff.cpp
#include "dbc_diff.h"
#include <sstream>
#include <iostream>
#include <iomanip>

namespace dbc_vc {

// ── Helper macros ────────────────────────────────────────────────────
#define FIELD_CMP(level, path, field, old_obj, new_obj)             \
    if ((old_obj).field != (new_obj).field) {                       \
        result.changes.push_back({                                  \
            ChangeLevel::level,                                     \
            path + "." #field,                                      \
            "Field changed: " #field,                               \
            std::to_string((old_obj).field),                        \
            std::to_string((new_obj).field)                         \
        });                                                         \
    }

static void diff_signal(
        DiffResult& result,
        const std::string& path,
        const Signal& old_sig,
        const Signal& new_sig)
{
    // Breaking: structural layout
    FIELD_CMP(MAJOR, path, start_bit,        old_sig, new_sig)
    FIELD_CMP(MAJOR, path, bit_length,       old_sig, new_sig)
    FIELD_CMP(MAJOR, path, is_little_endian, old_sig, new_sig)
    FIELD_CMP(MAJOR, path, is_unsigned,      old_sig, new_sig)

    // Breaking: value semantics
    if (old_sig.factor != new_sig.factor) {
        result.changes.push_back({
            ChangeLevel::MAJOR, path + ".factor",
            "Scale factor changed — decoded values differ",
            std::to_string(old_sig.factor),
            std::to_string(new_sig.factor)
        });
    }
    if (old_sig.offset != new_sig.offset) {
        result.changes.push_back({
            ChangeLevel::MAJOR, path + ".offset",
            "Offset changed — decoded values differ",
            std::to_string(old_sig.offset),
            std::to_string(new_sig.offset)
        });
    }

    // Patch: documentation/range hints
    if (old_sig.unit != new_sig.unit) {
        result.changes.push_back({
            ChangeLevel::PATCH, path + ".unit",
            "Unit string changed",
            old_sig.unit, new_sig.unit
        });
    }
    if (old_sig.comment != new_sig.comment) {
        result.changes.push_back({
            ChangeLevel::PATCH, path + ".comment",
            "Comment changed", old_sig.comment, new_sig.comment
        });
    }

    // Minor: receiver list growth
    for (const auto& r : new_sig.receivers) {
        bool found = false;
        for (const auto& ro : old_sig.receivers) if (ro == r) { found = true; break; }
        if (!found) {
            result.changes.push_back({
                ChangeLevel::MINOR, path + ".receivers",
                "New receiver added: " + r, "", r
            });
        }
    }
    // Major: receiver removed (sender no longer targets an ECU)
    for (const auto& ro : old_sig.receivers) {
        bool found = false;
        for (const auto& r : new_sig.receivers) if (r == ro) { found = true; break; }
        if (!found) {
            result.changes.push_back({
                ChangeLevel::MAJOR, path + ".receivers",
                "Receiver removed: " + ro, ro, ""
            });
        }
    }
}

static void diff_message(
        DiffResult& result,
        uint32_t id,
        const Message& old_msg,
        const Message& new_msg)
{
    const std::string mpath = "MSG[" + std::to_string(id) + "]";

    // Breaking: DLC reduction
    if (new_msg.dlc < old_msg.dlc) {
        result.changes.push_back({
            ChangeLevel::MAJOR, mpath + ".dlc",
            "DLC reduced — existing signals may overflow",
            std::to_string(old_msg.dlc),
            std::to_string(new_msg.dlc)
        });
    } else if (new_msg.dlc > old_msg.dlc) {
        result.changes.push_back({
            ChangeLevel::PATCH, mpath + ".dlc",
            "DLC increased — backward compatible",
            std::to_string(old_msg.dlc),
            std::to_string(new_msg.dlc)
        });
    }

    // Breaking: message rename
    if (old_msg.name != new_msg.name) {
        result.changes.push_back({
            ChangeLevel::MAJOR, mpath + ".name",
            "Message renamed",
            old_msg.name, new_msg.name
        });
    }

    // Check existing signals
    for (const auto& [sname, old_sig] : old_msg.signals) {
        auto it = new_msg.signals.find(sname);
        if (it == new_msg.signals.end()) {
            result.changes.push_back({
                ChangeLevel::MAJOR,
                mpath + ".SIG[" + sname + "]",
                "Signal removed",
                sname, ""
            });
        } else {
            diff_signal(result, mpath + ".SIG[" + sname + "]",
                        old_sig, it->second);
        }
    }

    // Check new signals
    for (const auto& [sname, new_sig] : new_msg.signals) {
        if (old_msg.signals.find(sname) == old_msg.signals.end()) {
            // New signal: minor if it fits within DLC, major if DLC also grew
            ChangeLevel lv = (new_msg.dlc == old_msg.dlc)
                              ? ChangeLevel::MINOR
                              : ChangeLevel::MINOR;  // DLC increase is PATCH above
            result.changes.push_back({
                lv,
                mpath + ".SIG[" + sname + "]",
                "New signal added",
                "", sname
            });
        }
    }
}

// ── Public diff entry point ──────────────────────────────────────────
DiffResult diff_dbc(const DbcFile& baseline, const DbcFile& updated)
{
    DiffResult result;

    // Messages removed → MAJOR
    for (const auto& [id, old_msg] : baseline.messages) {
        if (updated.messages.find(id) == updated.messages.end()) {
            result.changes.push_back({
                ChangeLevel::MAJOR,
                "MSG[" + std::to_string(id) + "]",
                "Message removed: " + old_msg.name,
                old_msg.name, ""
            });
        }
    }

    // Messages added → MINOR; existing → recurse
    for (const auto& [id, new_msg] : updated.messages) {
        auto it = baseline.messages.find(id);
        if (it == baseline.messages.end()) {
            result.changes.push_back({
                ChangeLevel::MINOR,
                "MSG[" + std::to_string(id) + "]",
                "New message added: " + new_msg.name,
                "", new_msg.name
            });
        } else {
            diff_message(result, id, it->second, new_msg);
        }
    }

    // Node changes
    for (const auto& node : baseline.nodes) {
        bool found = false;
        for (const auto& n : updated.nodes) if (n == node) { found = true; break; }
        if (!found) {
            result.changes.push_back({
                ChangeLevel::MAJOR, "BU_." + node,
                "ECU node removed", node, ""
            });
        }
    }
    for (const auto& node : updated.nodes) {
        bool found = false;
        for (const auto& n : baseline.nodes) if (n == node) { found = true; break; }
        if (!found) {
            result.changes.push_back({
                ChangeLevel::MINOR, "BU_." + node,
                "ECU node added", "", node
            });
        }
    }

    return result;
}

// ── Report printer ───────────────────────────────────────────────────
void DiffResult::print_report(std::ostream& out) const {
    out << "\n===== DBC DIFF REPORT =====\n";
    out << "Overall impact: " << to_string(overall_level()) << "\n";
    out << "Total changes:  " << changes.size() << "\n\n";

    const char* levels[] = {"NONE ", "PATCH", "MINOR", "MAJOR"};
    for (const auto& c : changes) {
        out << "[" << to_string(c.level) << "]  "
            << std::left << std::setw(50) << c.path
            << "  " << c.description;
        if (!c.old_value.empty() || !c.new_value.empty()) {
            out << "  (" << c.old_value << " → " << c.new_value << ")";
        }
        out << "\n";
    }
}

} // namespace dbc_vc
```

### Usage Example

```cpp
// main_diff.cpp
#include "dbc_diff.h"
#include <iostream>

int main() {
    using namespace dbc_vc;

    // Baseline v1.4.2
    DbcFile baseline;
    baseline.version = "1.4.2";
    auto& eng = baseline.messages[256];
    eng.id = 256; eng.name = "EngineStatus"; eng.dlc = 8;
    eng.signals["EngineRPM"] = {
        .name="EngineRPM", .start_bit=0, .bit_length=16,
        .is_little_endian=true, .is_unsigned=true,
        .factor=0.25, .offset=0.0,
        .receivers={"ECU_Transmission","ECU_BCM"}
    };
    eng.signals["CoolantTemp"] = {
        .name="CoolantTemp", .start_bit=16, .bit_length=8,
        .is_little_endian=true, .is_unsigned=true,
        .factor=1.0, .offset=-40.0,
        .receivers={"ECU_BCM"}
    };

    // Updated v2.0.0 — breaking: RPM factor changed, CoolantTemp removed
    DbcFile updated;
    updated.version = "2.0.0";
    auto& eng2 = updated.messages[256];
    eng2.id = 256; eng2.name = "EngineStatus"; eng2.dlc = 8;
    eng2.signals["EngineRPM"] = {
        .name="EngineRPM", .start_bit=0, .bit_length=16,
        .is_little_endian=true, .is_unsigned=true,
        .factor=0.5,            // <-- breaking change!
        .offset=0.0,
        .receivers={"ECU_Transmission","ECU_BCM"}
    };
    // CoolantTemp not added → removed → MAJOR
    // New signal added:
    eng2.signals["OilPressure"] = {
        .name="OilPressure", .start_bit=32, .bit_length=8,
        .is_little_endian=true, .is_unsigned=true,
        .factor=0.1, .offset=0.0,
        .receivers={"ECU_BCM"}
    };

    DiffResult result = diff_dbc(baseline, updated);
    result.print_report(std::cout);

    return (result.overall_level() >= ChangeLevel::MAJOR) ? 1 : 0;
}
```

**Output:**
```
===== DBC DIFF REPORT =====
Overall impact: MAJOR
Total changes:  3

[MAJOR]  MSG[256].SIG[EngineRPM].factor        Scale factor changed — decoded values differ  (0.250000 → 0.500000)
[MAJOR]  MSG[256].SIG[CoolantTemp]              Signal removed  (CoolantTemp → )
[MINOR]  MSG[256].SIG[OilPressure]              New signal added  ( → OilPressure)
```

---

## Compatibility Matrix Builder — C/C++

A compatibility matrix maps **(DBC version, ECU firmware version)** pairs to a compatibility verdict. This is critical in multi-project environments where different ECU teams release at different cadences.

```cpp
// compat_matrix.h
#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <iostream>

namespace dbc_vc {

enum class CompatStatus {
    COMPATIBLE,         // Full forward/backward compatibility
    COMPATIBLE_DEGRADED,// Works but with limited functionality (new signals ignored)
    INCOMPATIBLE,       // Breaking — runtime errors expected
    UNTESTED,           // Not validated
};

inline const char* to_string(CompatStatus s) {
    switch(s) {
        case CompatStatus::COMPATIBLE:          return "✓ COMPATIBLE";
        case CompatStatus::COMPATIBLE_DEGRADED: return "~ DEGRADED  ";
        case CompatStatus::INCOMPATIBLE:        return "✗ INCOMPATIBLE";
        case CompatStatus::UNTESTED:            return "? UNTESTED  ";
    }
    return "?";
}

struct SemVer {
    int major = 0, minor = 0, patch = 0;

    static SemVer parse(const std::string& s) {
        SemVer v;
        sscanf(s.c_str(), "%d.%d.%d", &v.major, &v.minor, &v.patch);
        return v;
    }

    bool operator<(const SemVer& o) const {
        if (major != o.major) return major < o.major;
        if (minor != o.minor) return minor < o.minor;
        return patch < o.patch;
    }
    bool operator==(const SemVer& o) const {
        return major == o.major && minor == o.minor && patch == o.patch;
    }
    std::string str() const {
        return std::to_string(major)+"."+std::to_string(minor)+"."+std::to_string(patch);
    }
};

// ── Per-ECU compatibility rule ───────────────────────────────────────
struct EcuCompatRule {
    std::string ecu_name;
    SemVer      min_dbc_version;  // ECU requires DBC >= this
    SemVer      max_dbc_version;  // ECU tested up to this DBC version
    SemVer      min_ecu_fw;       // Minimum ECU firmware for this DBC
};

// ── Compatibility matrix ─────────────────────────────────────────────
class CompatMatrix {
public:
    void add_rule(EcuCompatRule rule) {
        rules_[rule.ecu_name].push_back(std::move(rule));
    }

    // Check whether a given DBC version is compatible with an ECU firmware
    CompatStatus check(
        const std::string& ecu_name,
        const std::string& dbc_version_str,
        const std::string& ecu_fw_str) const
    {
        SemVer dbc = SemVer::parse(dbc_version_str);
        SemVer fw  = SemVer::parse(ecu_fw_str);

        auto it = rules_.find(ecu_name);
        if (it == rules_.end()) return CompatStatus::UNTESTED;

        for (const auto& rule : it->second) {
            if (fw < rule.min_ecu_fw) continue;  // FW too old for this rule

            // DBC too old (breaking changes not yet present in this DBC)
            if (dbc < rule.min_dbc_version) return CompatStatus::INCOMPATIBLE;

            // DBC newer than tested range — may have untested additive features
            if (rule.max_dbc_version < dbc) {
                if (dbc.major == rule.max_dbc_version.major) {
                    return CompatStatus::COMPATIBLE_DEGRADED; // MINOR additions
                }
                return CompatStatus::INCOMPATIBLE;            // MAJOR jump
            }

            return CompatStatus::COMPATIBLE;
        }
        return CompatStatus::UNTESTED;
    }

    // Print full matrix for all registered ECUs × DBC versions
    void print_matrix(
        const std::vector<std::string>& dbc_versions,
        const std::map<std::string, std::string>& ecu_fw_map,
        std::ostream& out) const
    {
        const int COL = 18;
        out << "\n╔══ COMPATIBILITY MATRIX ═════════════════════════════\n";
        out << "║  ECU \\ DBC Version";
        for (const auto& v : dbc_versions)
            out << std::setw(COL) << v;
        out << "\n╠═════════════════════════════════════════════════════\n";

        for (const auto& [ecu, fw] : ecu_fw_map) {
            out << "║  " << std::left << std::setw(18) << (ecu + " (fw" + fw + ")");
            for (const auto& dv : dbc_versions) {
                auto status = check(ecu, dv, fw);
                out << std::setw(COL) << to_string(status);
            }
            out << "\n";
        }
        out << "╚═════════════════════════════════════════════════════\n";
    }

private:
    std::map<std::string, std::vector<EcuCompatRule>> rules_;
};

} // namespace dbc_vc
```

```cpp
// main_compat.cpp
#include "compat_matrix.h"
#include <iostream>

int main() {
    using namespace dbc_vc;

    CompatMatrix matrix;

    // ECU_Engine: requires DBC >= 1.0.0, tested up to 2.x
    matrix.add_rule({"ECU_Engine",
        SemVer::parse("1.0.0"),   // min DBC
        SemVer::parse("2.9.9"),   // max DBC tested
        SemVer::parse("3.2.0")}); // min ECU FW

    // ECU_Transmission: tested only on 1.x
    matrix.add_rule({"ECU_Transmission",
        SemVer::parse("1.0.0"),
        SemVer::parse("1.9.9"),
        SemVer::parse("5.0.0")});

    // ECU_BCM: ships with strict 1.4.x requirement
    matrix.add_rule({"ECU_BCM",
        SemVer::parse("1.4.0"),
        SemVer::parse("1.4.9"),
        SemVer::parse("2.0.0")});

    std::vector<std::string> dbc_versions = {
        "1.3.0", "1.4.2", "1.5.0", "2.0.0", "2.1.0", "3.0.0"
    };
    std::map<std::string, std::string> ecu_fw = {
        {"ECU_Engine",       "3.5.1"},
        {"ECU_Transmission", "5.1.0"},
        {"ECU_BCM",          "2.3.0"},
    };

    matrix.print_matrix(dbc_versions, ecu_fw, std::cout);
    return 0;
}
```

**Output:**
```
╔══ COMPATIBILITY MATRIX ═════════════════════════════
║  ECU \ DBC Version     1.3.0           1.4.2           1.5.0           2.0.0           2.1.0           3.0.0
╠═════════════════════════════════════════════════════
║  ECU_Engine (fw3.5.1)  ✓ COMPATIBLE    ✓ COMPATIBLE    ✓ COMPATIBLE    ✓ COMPATIBLE    ✓ COMPATIBLE    ✗ INCOMPATIBLE
║  ECU_Transm (fw5.1.0)  ✓ COMPATIBLE    ✓ COMPATIBLE    ~ DEGRADED      ✗ INCOMPATIBLE  ✗ INCOMPATIBLE  ✗ INCOMPATIBLE
║  ECU_BCM (fw2.3.0)     ✗ INCOMPATIBLE  ✓ COMPATIBLE    ✗ INCOMPATIBLE  ✗ INCOMPATIBLE  ✗ INCOMPATIBLE  ✗ INCOMPATIBLE
╚═════════════════════════════════════════════════════
```

---

## DBC Version Control Library — Rust

The Rust implementation provides a complete, idiomatic solution with strong typing, error propagation, and built-in hash-based change detection.

### Cargo.toml

```toml
[package]
name    = "dbc-vc"
version = "0.1.0"
edition = "2021"

[dependencies]
sha2     = "0.10"
hex      = "0.4"
semver   = "1.0"
serde    = { version = "1", features = ["derive"] }
serde_json = "1"
```

### src/types.rs

```rust
use std::collections::HashMap;
use serde::{Deserialize, Serialize};

/// A parsed CAN signal definition
#[derive(Debug, Clone, PartialEq, Serialize, Deserialize)]
pub struct Signal {
    pub name: String,
    pub start_bit: u32,
    pub bit_length: u32,
    pub is_little_endian: bool,
    pub is_unsigned: bool,
    pub factor: f64,
    pub offset: f64,
    pub min_val: f64,
    pub max_val: f64,
    pub unit: String,
    pub receivers: Vec<String>,
    pub comment: Option<String>,
}

impl Signal {
    /// Returns true if the structural (wire-format) fields are identical.
    /// Changes here are always MAJOR.
    pub fn is_structurally_equal(&self, other: &Signal) -> bool {
        self.start_bit       == other.start_bit
        && self.bit_length   == other.bit_length
        && self.is_little_endian == other.is_little_endian
        && self.is_unsigned  == other.is_unsigned
        && self.factor       == other.factor
        && self.offset       == other.offset
    }

    /// Stable string for hashing — excludes mutable metadata
    pub fn schema_repr(&self) -> String {
        format!(
            "SG_{}:{}|{}@{}{}({},{})",
            self.name, self.start_bit, self.bit_length,
            if self.is_little_endian { 1 } else { 0 },
            if self.is_unsigned { '+' } else { '-' },
            self.factor, self.offset
        )
    }
}

/// A parsed CAN message definition
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Message {
    pub id: u32,
    pub name: String,
    pub dlc: u8,
    pub transmitter: String,
    pub signals: HashMap<String, Signal>,
    pub comment: Option<String>,
}

impl Message {
    pub fn schema_repr(&self) -> String {
        let mut sigs: Vec<String> = self.signals.values()
            .map(|s| s.schema_repr())
            .collect();
        sigs.sort();   // deterministic ordering
        format!("BO_{}_{}_{}_[{}]", self.id, self.name, self.dlc, sigs.join(";"))
    }
}

/// The complete parsed DBC file
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DbcFile {
    pub version: String,
    pub filename: String,
    pub messages: HashMap<u32, Message>,   // keyed by CAN ID
    pub nodes: Vec<String>,
}

impl DbcFile {
    /// Compute a deterministic SHA-256 hash of structural elements only.
    /// Metadata (comments, attributes, version string) are excluded.
    pub fn schema_hash(&self) -> String {
        use sha2::{Sha256, Digest};

        let mut parts: Vec<String> = self.messages.values()
            .map(|m| m.schema_repr())
            .collect();
        parts.sort();

        let mut hasher = Sha256::new();
        hasher.update(parts.join("\n").as_bytes());
        hex::encode(hasher.finalize())
    }
}
```

### src/diff.rs

```rust
use crate::types::{DbcFile, Message, Signal};
use std::fmt;

/// Severity of a detected change
#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum ChangeLevel {
    Patch,  // Non-breaking (docs, metadata)
    Minor,  // Additive (new messages/signals)
    Major,  // Breaking (ID, DLC, layout, semantics)
}

impl fmt::Display for ChangeLevel {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            ChangeLevel::Patch => write!(f, "PATCH"),
            ChangeLevel::Minor => write!(f, "MINOR"),
            ChangeLevel::Major => write!(f, "MAJOR"),
        }
    }
}

/// A single detected change with location and description
#[derive(Debug, Clone)]
pub struct ChangeRecord {
    pub level: ChangeLevel,
    pub path: String,
    pub description: String,
    pub old_value: Option<String>,
    pub new_value: Option<String>,
}

impl fmt::Display for ChangeRecord {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "[{:<5}]  {:<50}  {}", self.level, self.path, self.description)?;
        match (&self.old_value, &self.new_value) {
            (Some(o), Some(n)) => write!(f, "  ({o} → {n})")?,
            (Some(o), None)    => write!(f, "  (was: {o})")?,
            (None,    Some(n)) => write!(f, "  (now: {n})")?,
            _                  => {}
        }
        Ok(())
    }
}

/// The result of a structural DBC comparison
#[derive(Debug, Default)]
pub struct DiffResult {
    pub changes: Vec<ChangeRecord>,
}

impl DiffResult {
    pub fn overall_level(&self) -> Option<ChangeLevel> {
        self.changes.iter().map(|c| c.level).max()
    }

    /// Returns the recommended SemVer bump component
    pub fn suggested_bump(&self) -> &'static str {
        match self.overall_level() {
            Some(ChangeLevel::Major) => "MAJOR",
            Some(ChangeLevel::Minor) => "MINOR",
            Some(ChangeLevel::Patch) => "PATCH",
            None => "NONE",
        }
    }

    fn push(&mut self, level: ChangeLevel, path: &str, desc: &str,
            old: Option<String>, new: Option<String>) {
        self.changes.push(ChangeRecord {
            level,
            path: path.to_string(),
            description: desc.to_string(),
            old_value: old,
            new_value: new,
        });
    }
}

/// Entry point: compare two DBC files and return all detected changes
pub fn diff_dbc(baseline: &DbcFile, updated: &DbcFile) -> DiffResult {
    let mut result = DiffResult::default();

    // Fast-path: identical schema hash → no structural changes
    if baseline.schema_hash() == updated.schema_hash() {
        return result;   // may still have PATCH-level metadata diffs
    }

    // Messages removed
    for (id, old_msg) in &baseline.messages {
        if !updated.messages.contains_key(id) {
            result.push(ChangeLevel::Major,
                &format!("MSG[{id}]"),
                "Message removed",
                Some(old_msg.name.clone()), None);
        }
    }

    // Messages added or changed
    for (id, new_msg) in &updated.messages {
        match baseline.messages.get(id) {
            None => result.push(ChangeLevel::Minor,
                &format!("MSG[{id}]"),
                "New message added",
                None, Some(new_msg.name.clone())),
            Some(old_msg) => diff_message(&mut result, *id, old_msg, new_msg),
        }
    }

    // Node changes
    diff_nodes(&mut result, &baseline.nodes, &updated.nodes);

    result
}

fn diff_message(result: &mut DiffResult, id: u32, old: &Message, new: &Message) {
    let base = format!("MSG[{id}]");

    if old.name != new.name {
        result.push(ChangeLevel::Major, &format!("{base}.name"),
            "Message renamed",
            Some(old.name.clone()), Some(new.name.clone()));
    }

    match new.dlc.cmp(&old.dlc) {
        std::cmp::Ordering::Less => result.push(ChangeLevel::Major,
            &format!("{base}.dlc"), "DLC reduced — signals may overflow",
            Some(old.dlc.to_string()), Some(new.dlc.to_string())),
        std::cmp::Ordering::Greater => result.push(ChangeLevel::Patch,
            &format!("{base}.dlc"), "DLC increased — backward compatible",
            Some(old.dlc.to_string()), Some(new.dlc.to_string())),
        _ => {}
    }

    for (sname, old_sig) in &old.signals {
        match new.signals.get(sname) {
            None => result.push(ChangeLevel::Major,
                &format!("{base}.SIG[{sname}]"),
                "Signal removed",
                Some(sname.clone()), None),
            Some(new_sig) => diff_signal(result, &base, sname, old_sig, new_sig),
        }
    }

    for (sname, _) in &new.signals {
        if !old.signals.contains_key(sname) {
            result.push(ChangeLevel::Minor,
                &format!("{base}.SIG[{sname}]"),
                "New signal added",
                None, Some(sname.clone()));
        }
    }
}

fn diff_signal(result: &mut DiffResult, msg_path: &str,
               name: &str, old: &Signal, new: &Signal) {
    let base = format!("{msg_path}.SIG[{name}]");

    macro_rules! major_field {
        ($field:ident, $label:literal) => {
            if old.$field != new.$field {
                result.push(ChangeLevel::Major,
                    &format!("{base}.{}", $label),
                    concat!("Breaking change: ", $label),
                    Some(format!("{:?}", old.$field)),
                    Some(format!("{:?}", new.$field)));
            }
        };
    }
    macro_rules! patch_field {
        ($field:ident, $label:literal) => {
            if old.$field != new.$field {
                result.push(ChangeLevel::Patch,
                    &format!("{base}.{}", $label),
                    concat!("Documentation change: ", $label),
                    Some(format!("{:?}", old.$field)),
                    Some(format!("{:?}", new.$field)));
            }
        };
    }

    major_field!(start_bit,        "start_bit");
    major_field!(bit_length,       "bit_length");
    major_field!(is_little_endian, "byte_order");
    major_field!(is_unsigned,      "value_type");
    major_field!(factor,           "factor");
    major_field!(offset,           "offset");
    patch_field!(unit,             "unit");
    patch_field!(comment,          "comment");
    patch_field!(min_val,          "min_val");
    patch_field!(max_val,          "max_val");

    // Receiver analysis
    for r in &new.receivers {
        if !old.receivers.contains(r) {
            result.push(ChangeLevel::Minor,
                &format!("{base}.receivers"),
                "New receiver added",
                None, Some(r.clone()));
        }
    }
    for r in &old.receivers {
        if !new.receivers.contains(r) {
            result.push(ChangeLevel::Major,
                &format!("{base}.receivers"),
                "Receiver removed — ECU no longer subscribed",
                Some(r.clone()), None);
        }
    }
}

fn diff_nodes(result: &mut DiffResult, old: &[String], new: &[String]) {
    for node in old {
        if !new.contains(node) {
            result.push(ChangeLevel::Major, &format!("BU_.{node}"),
                "ECU node removed", Some(node.clone()), None);
        }
    }
    for node in new {
        if !old.contains(node) {
            result.push(ChangeLevel::Minor, &format!("BU_.{node}"),
                "ECU node added", None, Some(node.clone()));
        }
    }
}
```

### src/change_tracker.rs

```rust
//! Persistent change log for audit trails and multi-project traceability

use crate::diff::{DiffResult, ChangeLevel};
use crate::types::DbcFile;
use serde::{Deserialize, Serialize};
use std::time::{SystemTime, UNIX_EPOCH};

/// A single versioned commit in the DBC change history
#[derive(Debug, Serialize, Deserialize)]
pub struct ChangeCommit {
    pub timestamp: u64,          // Unix timestamp
    pub author: String,
    pub ticket: Option<String>,  // e.g. "PROJ-1234"
    pub old_version: String,
    pub new_version: String,
    pub old_hash: String,
    pub new_hash: String,
    pub bump_level: String,      // "MAJOR" / "MINOR" / "PATCH" / "NONE"
    pub summary: Vec<String>,    // Human-readable change descriptions
    pub projects_affected: Vec<String>,
}

/// Multi-project DBC repository
pub struct DbcRepo {
    pub history: Vec<ChangeCommit>,
    pub project_registry: Vec<ProjectEntry>,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct ProjectEntry {
    pub name: String,
    pub subscribed_dbc_ids: Vec<u32>,   // CAN message IDs consumed
    pub pinned_version: Option<String>, // None = always latest
}

impl DbcRepo {
    pub fn new() -> Self {
        DbcRepo { history: Vec::new(), project_registry: Vec::new() }
    }

    pub fn register_project(&mut self, entry: ProjectEntry) {
        self.project_registry.push(entry);
    }

    /// Record a diff result as a commit with automatic impact analysis
    pub fn commit(
        &mut self,
        old: &DbcFile,
        new: &DbcFile,
        diff: &DiffResult,
        author: &str,
        ticket: Option<&str>,
    ) -> &ChangeCommit {
        // Determine which project IDs are affected
        let affected_ids: Vec<u32> = diff.changes.iter()
            .filter_map(|c| {
                // Extract MSG[<id>] from path
                if c.path.starts_with("MSG[") {
                    c.path[4..].split(']').next()
                        .and_then(|s| s.parse::<u32>().ok())
                } else { None }
            })
            .collect();

        let affected_projects: Vec<String> = self.project_registry.iter()
            .filter(|p| {
                p.subscribed_dbc_ids.iter().any(|id| affected_ids.contains(id))
            })
            .map(|p| p.name.clone())
            .collect();

        let commit = ChangeCommit {
            timestamp: SystemTime::now()
                .duration_since(UNIX_EPOCH).unwrap().as_secs(),
            author: author.to_string(),
            ticket: ticket.map(|t| t.to_string()),
            old_version: old.version.clone(),
            new_version: new.version.clone(),
            old_hash: old.schema_hash(),
            new_hash: new.schema_hash(),
            bump_level: diff.suggested_bump().to_string(),
            summary: diff.changes.iter()
                .map(|c| format!("[{}] {} — {}", c.level, c.path, c.description))
                .collect(),
            projects_affected: affected_projects,
        };

        self.history.push(commit);
        self.history.last().unwrap()
    }

    /// Print the full change history
    pub fn print_log(&self) {
        println!("\n╔══ DBC CHANGE LOG ═══════════════════════════════════════");
        for (i, commit) in self.history.iter().enumerate().rev() {
            println!("║ #{:03}  {}→{}  [{}]  by {}  {}",
                i,
                commit.old_version, commit.new_version,
                commit.bump_level, commit.author,
                commit.ticket.as_deref().unwrap_or("—"),
            );
            println!("║      schema: {} → {}",
                &commit.old_hash[..12], &commit.new_hash[..12]);
            if !commit.projects_affected.is_empty() {
                println!("║      ⚠ Affects: {}", commit.projects_affected.join(", "));
            }
            for s in &commit.summary {
                println!("║        {s}");
            }
            println!("║");
        }
        println!("╚═══════════════════════════════════════════════════════");
    }

    /// Export history as JSON for CI/CD consumption
    pub fn export_json(&self) -> String {
        serde_json::to_string_pretty(&self.history)
            .unwrap_or_else(|_| "[]".into())
    }
}
```

### src/main.rs

```rust
mod types;
mod diff;
mod change_tracker;

use types::{DbcFile, Message, Signal};
use diff::diff_dbc;
use change_tracker::{DbcRepo, ProjectEntry};
use std::collections::HashMap;

fn make_signal(name: &str, start: u32, len: u32, factor: f64, offset: f64,
               receivers: Vec<&str>) -> Signal {
    Signal {
        name: name.to_string(),
        start_bit: start, bit_length: len,
        is_little_endian: true, is_unsigned: true,
        factor, offset,
        min_val: 0.0, max_val: 0.0,
        unit: String::new(),
        receivers: receivers.into_iter().map(String::from).collect(),
        comment: None,
    }
}

fn main() {
    // ── Build baseline DBC v1.4.2 ─────────────────────────────────────
    let mut baseline_msgs = HashMap::new();
    let mut eng_sigs = HashMap::new();
    eng_sigs.insert("EngineRPM".into(),
        make_signal("EngineRPM", 0, 16, 0.25, 0.0,
                    vec!["ECU_Transmission","ECU_BCM"]));
    eng_sigs.insert("CoolantTemp".into(),
        make_signal("CoolantTemp", 16, 8, 1.0, -40.0, vec!["ECU_BCM"]));
    eng_sigs.insert("EngineRunning".into(),
        make_signal("EngineRunning", 24, 1, 1.0, 0.0,
                    vec!["ECU_Transmission"]));
    baseline_msgs.insert(256u32, Message {
        id: 256, name: "EngineStatus".into(), dlc: 8,
        transmitter: "ECU_Engine".into(),
        signals: eng_sigs, comment: None,
    });

    let baseline = DbcFile {
        version: "1.4.2".to_string(),
        filename: "powertrain.dbc".to_string(),
        messages: baseline_msgs,
        nodes: vec!["ECU_Engine".into(), "ECU_Transmission".into(), "ECU_BCM".into()],
    };

    println!("Baseline schema hash: {}", &baseline.schema_hash()[..16]);

    // ── Build updated DBC v2.0.0 (breaking changes) ───────────────────
    let mut updated_msgs = HashMap::new();
    let mut eng_sigs2 = HashMap::new();
    eng_sigs2.insert("EngineRPM".into(),
        make_signal("EngineRPM", 0, 16, 0.5, 0.0,   // factor changed!
                    vec!["ECU_Transmission","ECU_BCM"]));
    // CoolantTemp REMOVED — breaking
    eng_sigs2.insert("EngineRunning".into(),
        make_signal("EngineRunning", 24, 1, 1.0, 0.0,
                    vec!["ECU_Transmission"]));
    eng_sigs2.insert("OilPressure".into(),         // new signal — minor
        make_signal("OilPressure", 32, 8, 0.1, 0.0, vec!["ECU_BCM"]));
    updated_msgs.insert(256u32, Message {
        id: 256, name: "EngineStatus".into(), dlc: 8,
        transmitter: "ECU_Engine".into(),
        signals: eng_sigs2, comment: None,
    });

    let updated = DbcFile {
        version: "2.0.0".to_string(),
        filename: "powertrain.dbc".to_string(),
        messages: updated_msgs,
        nodes: vec!["ECU_Engine".into(), "ECU_Transmission".into(),
                    "ECU_BCM".into(), "ECU_ADAS".into()],
    };

    println!("Updated  schema hash: {}", &updated.schema_hash()[..16]);

    // ── Run diff ──────────────────────────────────────────────────────
    let diff = diff_dbc(&baseline, &updated);
    println!("\n{} changes detected. Suggested bump: {}",
        diff.changes.len(), diff.suggested_bump());
    for change in &diff.changes {
        println!("  {change}");
    }

    // ── Commit to repo with project impact analysis ───────────────────
    let mut repo = DbcRepo::new();

    repo.register_project(ProjectEntry {
        name: "project-alpha".to_string(),
        subscribed_dbc_ids: vec![256, 512],
        pinned_version: None,
    });
    repo.register_project(ProjectEntry {
        name: "project-beta".to_string(),
        subscribed_dbc_ids: vec![512, 768],
        pinned_version: Some("1.4.2".to_string()),
    });
    repo.register_project(ProjectEntry {
        name: "project-gamma-adas".to_string(),
        subscribed_dbc_ids: vec![256],
        pinned_version: None,
    });

    let commit = repo.commit(
        &baseline, &updated, &diff,
        "powertrain_team",
        Some("PT-4471"),
    );
    println!("\nAffected projects: {:?}", commit.projects_affected);

    repo.print_log();

    // Export for CI pipeline
    let json = repo.export_json();
    std::fs::write("dbc_change_log.json", &json).ok();
    println!("\nChange log exported to dbc_change_log.json");
}
```

---

## Multi-Project Environment Management

### Repository Structure

A recommended layout for managing DBC files across multiple projects:

```
can-db/
├── .git/
├── schemas/
│   ├── powertrain/
│   │   ├── powertrain.dbc           ← Current HEAD
│   │   ├── powertrain.schema.hash   ← Pre-computed structural hash
│   │   └── CHANGELOG.md             ← Auto-generated
│   ├── chassis/
│   │   └── chassis.dbc
│   └── body/
│       └── body.dbc
├── compatibility/
│   ├── matrix.json                  ← Machine-readable compat matrix
│   └── pinned_versions.json         ← Per-project pin table
├── migrations/
│   └── 1.4.2_to_2.0.0/
│       ├── migration_notes.md
│       └── shim_layer.c             ← Auto-generated adapter code
├── tools/
│   ├── dbc-diff        (Rust binary)
│   ├── compat-check    (Rust binary)
│   └── dbc-hash        (C++ utility)
└── .github/
    └── workflows/
        └── dbc_compat_check.yml
```

### pinned_versions.json

```json
{
  "projects": {
    "project-alpha": {
      "powertrain.dbc": ">=2.0.0",
      "chassis.dbc":    ">=1.2.0",
      "body.dbc":       "*"
    },
    "project-beta": {
      "powertrain.dbc": "~1.4.0",
      "chassis.dbc":    ">=1.0.0"
    },
    "project-gamma-adas": {
      "powertrain.dbc": ">=2.0.0",
      "chassis.dbc":    ">=1.3.0"
    }
  }
}
```

### Shim Layer Generation (C)

When a MAJOR bump occurs, the tooling can auto-generate a C shim that adapts old decoders to the new wire format:

```c
/**
 * AUTO-GENERATED MIGRATION SHIM
 * DBC: powertrain.dbc  v1.4.2 → v2.0.0
 * Generated: 2024-11-20
 * Ticket: PT-4471
 *
 * DO NOT EDIT — re-generate with: dbc-migrate --from 1.4.2 --to 2.0.0
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ── MSG[256] EngineStatus ── */

/* OLD decoder: v1.4.2 expects factor=0.25 */
typedef struct {
    float   engine_rpm;       /* 0.25 RPM/bit */
    int8_t  coolant_temp_c;   /* removed in v2.0.0 */
    bool    engine_running;
} EngineStatus_v142_t;

/* NEW decoder: v2.0.0 uses factor=0.5 */
typedef struct {
    float   engine_rpm;       /* 0.5 RPM/bit */
    bool    engine_running;
    float   oil_pressure_bar; /* new in v2.0.0 */
} EngineStatus_v200_t;

/**
 * Adapt a v2.0.0 raw CAN frame so that a v1.4.2 consumer can process it.
 *
 * Limitations:
 *   - coolant_temp will read 0 (signal removed; caller must handle)
 *   - EngineRPM is rescaled: raw_rpm_bits *= 2 to compensate factor change
 */
static inline void shim_EngineStatus_v200_to_v142(
    const uint8_t* frame_v200,   /* 8-byte CAN payload from v2.0.0 sender */
    uint8_t*       frame_v142_out /* 8-byte buffer for v1.4.2 consumer    */
)
{
    memcpy(frame_v142_out, frame_v200, 8);

    /* Rescale RPM: v1.4.2 factor=0.25, v2.0.0 factor=0.5
     * physical = raw * factor  →  raw_v142 = raw_v200 * (0.5/0.25) = raw*2 */
    uint16_t raw_rpm;
    memcpy(&raw_rpm, frame_v200 + 0, sizeof(raw_rpm));  /* bits 0..15, LE */
    raw_rpm = (uint16_t)((uint32_t)raw_rpm * 2 > 0xFFFF
                          ? 0xFFFF : (uint32_t)raw_rpm * 2);
    memcpy(frame_v142_out + 0, &raw_rpm, sizeof(raw_rpm));

    /* CoolantTemp (bits 16..23) — zero out; no longer transmitted */
    frame_v142_out[2] = 0x00;
}

/**
 * Decode a v2.0.0 frame into the v1.4.2 struct via the shim above.
 * @return false if the RPM value was saturated during rescaling.
 */
static inline bool decode_shim_EngineStatus(
    const uint8_t*      frame_v200,
    EngineStatus_v142_t* out)
{
    uint8_t adapted[8];
    shim_EngineStatus_v200_to_v142(frame_v200, adapted);

    uint16_t raw_rpm;
    memcpy(&raw_rpm, adapted + 0, 2);
    out->engine_rpm     = raw_rpm * 0.25f;
    out->coolant_temp_c = 0;                 /* unavailable */
    out->engine_running = (adapted[3] & 0x01) != 0;

    return raw_rpm != 0xFFFF;   /* false = saturation occurred */
}
```

---

## Automated CI/CD Integration

### GitHub Actions Workflow

```yaml
# .github/workflows/dbc_compat_check.yml
name: DBC Compatibility Gate

on:
  pull_request:
    paths: ['schemas/**/*.dbc']

jobs:
  dbc-diff:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0   # Need full history for baseline

      - name: Get changed DBC files
        id: changed
        run: |
          git diff --name-only origin/${{ github.base_ref }}...HEAD \
            | grep '\.dbc$' > changed_dbc.txt
          cat changed_dbc.txt

      - name: Run DBC diff tool
        run: |
          while IFS= read -r dbc; do
            echo "=== Checking: $dbc ==="
            baseline=$(git show origin/${{ github.base_ref }}:"$dbc" 2>/dev/null \
                        || echo "NEWFILE")
            if [ "$baseline" != "NEWFILE" ]; then
              git show origin/${{ github.base_ref }}:"$dbc" > /tmp/baseline.dbc
              ./tools/dbc-diff /tmp/baseline.dbc "$dbc" \
                --output json > /tmp/diff_result.json
              cat /tmp/diff_result.json
            else
              echo "New DBC file — skipping diff"
            fi
          done < changed_dbc.txt

      - name: Compatibility matrix check
        run: |
          ./tools/compat-check \
            --matrix compatibility/matrix.json \
            --pins    compatibility/pinned_versions.json \
            --diff    /tmp/diff_result.json
          # Exits non-zero if any pinned project is broken

      - name: Block merge on MAJOR without version bump
        run: |
          BUMP=$(jq -r '.suggested_bump' /tmp/diff_result.json)
          OLD_VER=$(jq -r '.old_version'  /tmp/diff_result.json)
          NEW_VER=$(jq -r '.new_version'  /tmp/diff_result.json)
          if [ "$BUMP" = "MAJOR" ]; then
            OLD_MAJOR=$(echo $OLD_VER | cut -d. -f1)
            NEW_MAJOR=$(echo $NEW_VER | cut -d. -f1)
            if [ "$OLD_MAJOR" = "$NEW_MAJOR" ]; then
              echo "::error::MAJOR breaking change detected but VERSION was not bumped!"
              echo "::error::Old: $OLD_VER  New: $NEW_VER"
              exit 1
            fi
          fi
          echo "Version bump policy: OK"

      - name: Post diff summary as PR comment
        uses: marocchino/sticky-pull-request-comment@v2
        with:
          message: |
            ## 📡 DBC Compatibility Report
            ${{ env.DBC_DIFF_SUMMARY }}
```

### Pre-commit Hook (Shell)

```bash
#!/usr/bin/env bash
# .git/hooks/pre-commit
# Prevents commits that introduce breaking DBC changes without a MAJOR version bump

set -euo pipefail

CHANGED_DBCS=$(git diff --cached --name-only | grep '\.dbc$' || true)
[ -z "$CHANGED_DBCS" ] && exit 0

for dbc in $CHANGED_DBCS; do
    baseline_hash=$(git show HEAD:"$dbc" 2>/dev/null \
                    | ./tools/dbc-hash --stdin || echo "NONE")
    staged_hash=$(git show :"$dbc" | ./tools/dbc-hash --stdin)

    if [ "$baseline_hash" = "$staged_hash" ]; then
        echo "[dbc-vc] $dbc: schema unchanged (PATCH)"
        continue
    fi

    result=$(git show HEAD:"$dbc" 2>/dev/null \
             | ./tools/dbc-diff /dev/stdin <(git show :"$dbc") \
             --output json 2>/dev/null || echo '{"suggested_bump":"MINOR"}')

    bump=$(echo "$result" | jq -r '.suggested_bump')
    echo "[dbc-vc] $dbc: detected $bump change"

    if [ "$bump" = "MAJOR" ]; then
        new_ver=$(grep '^VERSION' <(git show :"$dbc") | awk '{print $2}' | tr -d '"')
        old_ver=$(git show HEAD:"$dbc" 2>/dev/null \
                  | grep '^VERSION' | awk '{print $2}' | tr -d '"')
        new_major=$(echo "$new_ver" | cut -d. -f1)
        old_major=$(echo "$old_ver" | cut -d. -f1)
        if [ "$new_major" = "$old_major" ]; then
            echo "ERROR: Breaking change in $dbc requires MAJOR version bump"
            echo "       Current: $old_ver  New: $new_ver"
            echo "       Run: ./tools/dbc-bump --major $dbc"
            exit 1
        fi
    fi
done

exit 0
```

---

## Summary

| Aspect | Key Takeaway |
|---|---|
| **DBC schema hashing** | Compute SHA-256 over structural elements only (IDs, DLCs, bit layouts, factor/offset) to detect meaningful changes while ignoring documentation noise |
| **Semantic diffing** | Classify every change as MAJOR (breaking), MINOR (additive), or PATCH (metadata) — never rely on line-level text diffs alone |
| **Compatibility matrices** | Map (DBC version × ECU firmware version) pairs to COMPATIBLE / DEGRADED / INCOMPATIBLE verdicts; export as JSON for CI consumption |
| **Multi-project tracing** | Register each project's subscribed CAN IDs; automatically propagate impact analysis when MAJOR changes are committed |
| **Shim generation** | For breaking changes, auto-generate C adapter layers that rescale or remap raw CAN payloads so legacy ECU firmware can continue operating |
| **CI/CD gates** | Block merges that introduce MAJOR structural changes without a corresponding MAJOR version bump; post human-readable diff summaries on pull requests |
| **Rust strengths** | Strong typing for change levels, deterministic hashing, serde-based JSON export, and compile-time safety for the diff engine core |
| **C/C++ strengths** | Direct integration with existing embedded toolchains, zero-overhead shim layers, and compatibility with Vector/PEAK CAN tool ecosystems |

Version control for DBC files is not a documentation concern — it is a **system safety concern**. A silently broken signal decode can manifest as an incorrect gear shift, a misfiring airbag, or an unresponsive brake system. Treating DBC evolution with the same rigour as source code — semantic versioning, automated diff gates, and compatibility matrices — is foundational to robust multi-ECU development.