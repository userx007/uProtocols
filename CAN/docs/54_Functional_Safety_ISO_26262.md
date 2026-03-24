# 54. Functional Safety (ISO 26262) in CAN Communication Systems

## Table of Contents

1. [Introduction](#introduction)
2. [ISO 26262 Overview](#iso-26262-overview)
3. [ASIL Classification](#asil-classification)
4. [Safety Mechanisms for CAN](#safety-mechanisms-for-can)
5. [Fault Metrics](#fault-metrics)
6. [CAN Safety Architecture Patterns](#can-safety-architecture-patterns)
7. [Implementation in C/C++](#implementation-in-cc)
8. [Implementation in Rust](#implementation-in-rust)
9. [Diagnostic Coverage and FMEA](#diagnostic-coverage-and-fmea)
10. [Summary](#summary)

---

## Introduction

Functional Safety in automotive systems, governed by **ISO 26262**, defines the requirements for the entire development lifecycle of safety-critical electrical and electronic systems. CAN (Controller Area Network) is the dominant communication backbone in vehicles, and any safety-critical function that relies on CAN messaging — such as brake control, steering, airbag deployment, or powertrain management — must be designed to meet the applicable **Automotive Safety Integrity Level (ASIL)**.

This document covers how to architect CAN communication systems with the safety mechanisms, fault detection strategies, and metrics required by ISO 26262, with concrete code examples in C/C++ and Rust.

---

## ISO 26262 Overview

ISO 26262 (*Road vehicles — Functional safety*) is a risk-based standard derived from IEC 61508. It applies to safety-related systems in passenger vehicles up to 3,500 kg.

Key concepts:

- **Item**: A system or combination of systems that implements a function at the vehicle level.
- **Hazard Analysis and Risk Assessment (HARA)**: Identifies hazardous events and assigns ASIL levels.
- **Safety Goal**: A top-level safety requirement derived from the HARA.
- **Functional Safety Concept**: High-level safety measures to achieve safety goals.
- **Technical Safety Concept**: Implementation-level safety requirements.
- **Safety Mechanism**: Hardware or software feature that detects or controls faults.

The standard is structured into parts:

| Part | Topic |
|------|-------|
| Part 1 | Vocabulary |
| Part 2 | Management of functional safety |
| Part 3 | Concept phase |
| Part 4 | Product development at system level |
| Part 5 | Product development at hardware level |
| Part 6 | Product development at software level |
| Part 7 | Production, operation, service, decommissioning |
| Part 8 | Supporting processes |
| Part 9 | ASIL-oriented and safety-oriented analyses |
| Part 10 | Guidelines |
| Part 11 | Semiconductors |
| Part 12 | Motorcycles (adaptation) |

---

## ASIL Classification

ASIL is derived from three parameters evaluated during HARA:

- **Severity (S0–S3)**: Potential harm to persons.
- **Exposure (E0–E4)**: Frequency of the hazardous situation.
- **Controllability (C0–C3)**: Probability that the driver can avoid harm.

The resulting ASIL ranges from **QM** (Quality Management, no specific safety requirements) to **ASIL D** (most stringent):

```
ASIL A < ASIL B < ASIL C < ASIL D
```

### ASIL Decomposition

A high ASIL requirement can be split into two lower requirements assigned to redundant channels, e.g.:

```
ASIL D → ASIL B(D) + ASIL B(D)
ASIL C → ASIL A(C) + ASIL B(C)
```

This is commonly applied to CAN ECUs implementing dual-channel redundancy.

---

## Safety Mechanisms for CAN

### CAN Hardware Safety Features

The CAN protocol itself provides some built-in error detection:

| Mechanism | Description | Coverage |
|-----------|-------------|----------|
| CRC (15-bit) | Detects burst errors up to 15 bits | High |
| Bit Stuffing | Detects violations of the stuffing rule | Medium |
| Frame Check | Validates fixed-format fields | Medium |
| ACK Check | Confirms at least one receiver acknowledged | Low |
| Error Counters (TEC/REC) | Bus-off after repeated errors | Medium |

However, CAN's built-in mechanisms are **insufficient alone** for ASIL B and above. Additional software-level safety mechanisms are mandatory.

### E2E (End-to-End) Protection

E2E protection (defined in **AUTOSAR E2E Library**, based on ISO 26262 Part 7) adds a protective wrapper to CAN data:

- **Counter**: Detects missing, repeated, or out-of-order messages.
- **CRC**: Detects data corruption beyond CAN's built-in CRC.
- **Data ID**: Ensures the message came from the correct sender (routing protection).

AUTOSAR defines standardized E2E profiles:

| Profile | CRC Width | Counter | Data ID | Typical ASIL |
|---------|-----------|---------|---------|--------------|
| E2E P01 | 8-bit CRC | 4-bit | 4-bit | ASIL A/B |
| E2E P02 | 8-bit CRC | 4-bit | 8-bit | ASIL A/B |
| E2E P04 | 32-bit CRC | 8-bit | 32-bit | ASIL D |
| E2E P05 | 16-bit CRC | 8-bit | 16-bit | ASIL B/C |
| E2E P06 | 16-bit CRC | 8-bit | 16-bit | ASIL B/C |
| E2E P07 | 64-bit CRC | 8-bit | 32-bit | ASIL D (CAN FD) |

### Watchdog Supervision

A software watchdog monitors the aliveness of CAN communication tasks. Missing heartbeat messages or overdue frames trigger a safe state.

### Timeout Supervision

Every safety-relevant CAN message must have a reception timeout. If a message is not received within its defined period (typically 1.5x–3x the transmission cycle), the receiving ECU must enter a safe state.

### Message Authentication (AUTOSAR SecOC)

For ASIL D systems, Secure Onboard Communication (SecOC) provides cryptographic authentication of CAN messages to detect injection and spoofing attacks.

---

## Fault Metrics

ISO 26262 Part 5 defines quantitative metrics for hardware elements:

### Single-Point Fault Metric (SPFM)

Measures the fraction of failures that are **detected or controlled** before reaching single-point faults:

```
SPFM = 1 - (λ_SPF / (λ_S + λ_MPF_latent))
```

Requirements by ASIL:

| ASIL | SPFM Target |
|------|-------------|
| ASIL B | ≥ 90% |
| ASIL C | ≥ 97% |
| ASIL D | ≥ 99% |

### Latent Fault Metric (LFM)

Measures the fraction of latent (undetected) multiple-point faults that are covered:

```
LFM = 1 - (λ_MPF_latent / λ_MPF_total)
```

| ASIL | LFM Target |
|------|------------|
| ASIL B | ≥ 60% |
| ASIL C | ≥ 80% |
| ASIL D | ≥ 90% |

### Probabilistic Metric for random Hardware Failures (PMHF)

The overall residual risk from random hardware failures:

```
PMHF = Σ (λ_SPF_i + λ_RF_i + λ_MPF_latent_i)
```

| ASIL | PMHF Target (per hour) |
|------|------------------------|
| ASIL B | < 10⁻⁷ |
| ASIL C | < 10⁻⁷ |
| ASIL D | < 10⁻⁸ |

### Diagnostic Coverage (DC)

The fraction of a component's failure rate that is covered by a safety mechanism:

```
DC = λ_detected / λ_total
```

| DC Class | Range |
|----------|-------|
| Low | 60% – 90% |
| Medium | 90% – 99% |
| High | ≥ 99% |

---

## CAN Safety Architecture Patterns

### Pattern 1: Single Channel with E2E (ASIL A/B)

```
[Sender ECU]  ---CAN bus---  [Receiver ECU]
   E2E Protect()               E2E Check()
   - CRC8                      - CRC8
   - Counter                   - Counter
   - DataID                    - DataID
   - Timeout detect            - Timeout detect
```

### Pattern 2: Dual Channel Redundant CAN (ASIL C/D)

```
[Primary ECU]   ---CAN bus 1---  [Safety Monitor]
[Secondary ECU] ---CAN bus 2---  [Safety Monitor]
                                  Cross-compare results
                                  Voter logic → Safe State or Output
```

### Pattern 3: Heterogeneous Redundancy (ASIL D)

Two independent channels use different hardware and software to avoid common-cause failures (CCFs):

```
Channel A: MCU-A + CAN Controller A + SW stack A
Channel B: MCU-B + CAN Controller B + SW stack B
           |
           +--- Independent voter / comparator
```

---

## Implementation in C/C++

### E2E Profile P01 Protection (CRC8 + Counter)

```c
/**
 * E2E Profile P01 Protection Layer
 * Implements CRC8 and rolling counter for ASIL A/B CAN messages
 * Based on AUTOSAR E2E Profile 1
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* CRC8 lookup table (SAE J1850 polynomial 0x1D) */
static const uint8_t crc8_table[256] = {
    0x00, 0x1D, 0x3A, 0x27, 0x74, 0x69, 0x4E, 0x53,
    0xE8, 0xF5, 0xD2, 0xCF, 0x9C, 0x81, 0xA6, 0xBB,
    /* ... full table omitted for brevity ... */
};

/* E2E P01 configuration */
typedef struct {
    uint8_t  data_id_nibble;   /* 4-bit Data ID, identifies the message */
    uint8_t  max_delta_counter;/* Maximum allowed counter jump */
    uint8_t  max_error_state_init; /* Errors to enter ERROR state */
} E2E_P01_ConfigType;

/* E2E P01 state (receiver side) */
typedef struct {
    uint8_t  counter;          /* Last received counter value */
    uint8_t  status;           /* E2E check status */
    uint8_t  no_new_or_repeated_data_counter;
    bool     new_data_available;
} E2E_P01_StateType;

/* E2E check status codes */
#define E2E_P01_STATUS_OK            0x00
#define E2E_P01_STATUS_WRONGCRC      0x01
#define E2E_P01_STATUS_SYNC          0x02
#define E2E_P01_STATUS_INITIAL       0x04
#define E2E_P01_STATUS_REPEATED      0x08
#define E2E_P01_STATUS_OKSOMELOST    0x20
#define E2E_P01_STATUS_WRONGSEQUENCE 0x40

/**
 * Calculate CRC8 over data buffer
 */
static uint8_t crc8_calculate(const uint8_t *data, uint16_t length, uint8_t start_value) {
    uint8_t crc = start_value;
    for (uint16_t i = 0; i < length; i++) {
        crc = crc8_table[crc ^ data[i]];
    }
    return crc;
}

/**
 * E2E P01 Protect: Called by sender before transmitting
 * @param config    E2E configuration
 * @param counter   Pointer to rolling counter (incremented on each call)
 * @param data      CAN payload buffer (first byte: CRC+counter nibble)
 * @param length    Payload length in bytes
 */
void E2E_P01_Protect(const E2E_P01_ConfigType *config,
                     uint8_t *counter,
                     uint8_t *data,
                     uint8_t length) {
    /* Write counter nibble into byte 0 [7:4] = DataID, [3:0] = counter */
    data[0] = ((config->data_id_nibble & 0x0F) << 4) | (*counter & 0x0F);

    /* Calculate CRC over entire payload, excluding the CRC byte itself */
    /* In P01, CRC is placed in byte 1 (after DataID/Counter byte) */
    uint8_t crc = crc8_calculate(&data[1], (uint16_t)(length - 1), 0xFF);
    /* Include the DataID nibble in CRC calculation */
    crc = crc8_table[crc ^ data[0]];
    data[1] = crc; /* Store CRC in second byte per P01 layout */

    /* Increment counter (wraps 0–14, skip 15 per AUTOSAR spec) */
    *counter = (*counter + 1) & 0x0F;
    if (*counter == 0x0F) {
        *counter = 0;
    }
}

/**
 * E2E P01 Check: Called by receiver after reception
 */
void E2E_P01_Check(const E2E_P01_ConfigType *config,
                   E2E_P01_StateType *state,
                   const uint8_t *data,
                   uint8_t length) {
    /* Extract received counter and Data ID nibble */
    uint8_t recv_counter  = data[0] & 0x0F;
    uint8_t recv_data_id  = (data[0] >> 4) & 0x0F;
    uint8_t recv_crc      = data[1];

    /* Verify Data ID */
    if (recv_data_id != config->data_id_nibble) {
        state->status = E2E_P01_STATUS_WRONGCRC; /* Treat as CRC error */
        return;
    }

    /* Recalculate CRC */
    uint8_t calc_crc = crc8_calculate(&data[1], (uint16_t)(length - 1), 0xFF);
    calc_crc = crc8_table[calc_crc ^ data[0]];

    if (calc_crc != recv_crc) {
        state->status = E2E_P01_STATUS_WRONGCRC;
        return;
    }

    /* Check counter progression */
    if (!state->new_data_available) {
        /* First message after init */
        state->counter = recv_counter;
        state->status  = E2E_P01_STATUS_INITIAL;
        state->new_data_available = true;
        return;
    }

    uint8_t delta = (recv_counter - state->counter) & 0x0F;

    if (delta == 0) {
        state->status = E2E_P01_STATUS_REPEATED;
    } else if (delta == 1) {
        state->status = E2E_P01_STATUS_OK;
    } else if (delta <= config->max_delta_counter) {
        state->status = E2E_P01_STATUS_OKSOMELOST;
    } else {
        state->status = E2E_P01_STATUS_WRONGSEQUENCE;
    }

    state->counter = recv_counter;
}
```

### Timeout Supervision for CAN Messages

```c
/**
 * CAN Message Timeout Supervisor
 * Detects missing or overdue messages, triggers safe state
 */

#include <stdint.h>
#include <stdbool.h>

#define MAX_MONITORED_MESSAGES  32

/* Supervision result codes */
typedef enum {
    SUPERVISION_OK          = 0,
    SUPERVISION_TIMEOUT     = 1,
    SUPERVISION_INITIALIZING = 2
} SupervisionResult;

/* Per-message supervision context */
typedef struct {
    uint32_t  timeout_ms;         /* Maximum allowed reception interval */
    uint32_t  last_received_tick; /* Timestamp of last valid reception */
    bool      initialized;        /* First message received? */
    bool      enabled;            /* Supervision active? */
    uint32_t  missed_count;       /* Consecutive missed cycles */
    uint32_t  max_missed;         /* Maximum allowed missed cycles */
} MsgSupervisionContext;

static MsgSupervisionContext supervision_table[MAX_MONITORED_MESSAGES];

/**
 * Register a message for supervision
 * @param msg_id       Index into supervision table (maps to CAN ID)
 * @param timeout_ms   Maximum reception interval in milliseconds
 * @param max_missed   How many missed cycles before triggering fault
 */
void supervision_register(uint8_t msg_id, uint32_t timeout_ms, uint32_t max_missed) {
    supervision_table[msg_id].timeout_ms   = timeout_ms;
    supervision_table[msg_id].max_missed   = max_missed;
    supervision_table[msg_id].initialized  = false;
    supervision_table[msg_id].enabled      = true;
    supervision_table[msg_id].missed_count = 0;
}

/**
 * Call this whenever a monitored message is successfully received
 */
void supervision_signal_received(uint8_t msg_id, uint32_t current_tick_ms) {
    supervision_table[msg_id].last_received_tick = current_tick_ms;
    supervision_table[msg_id].initialized        = true;
    supervision_table[msg_id].missed_count       = 0;
}

/**
 * Call periodically (e.g., every 1 ms) to check all supervised messages
 * Returns SUPERVISION_TIMEOUT and triggers safe state if any message is overdue
 */
SupervisionResult supervision_check_all(uint32_t current_tick_ms,
                                         void (*safe_state_callback)(uint8_t msg_id)) {
    SupervisionResult result = SUPERVISION_OK;

    for (uint8_t i = 0; i < MAX_MONITORED_MESSAGES; i++) {
        MsgSupervisionContext *ctx = &supervision_table[i];
        if (!ctx->enabled) continue;

        if (!ctx->initialized) {
            /* Grace period: allow first message to arrive */
            result = SUPERVISION_INITIALIZING;
            continue;
        }

        uint32_t elapsed = current_tick_ms - ctx->last_received_tick;
        if (elapsed > ctx->timeout_ms) {
            ctx->missed_count++;
            if (ctx->missed_count >= ctx->max_missed) {
                /* Trigger safe state */
                if (safe_state_callback) {
                    safe_state_callback(i);
                }
                result = SUPERVISION_TIMEOUT;
            }
        }
    }
    return result;
}
```

### FMEA-Based CAN Fault Handler

```cpp
/**
 * CAN Safety Fault Manager (C++)
 * Implements a safety state machine for ASIL B/C CAN nodes
 */

#include <cstdint>
#include <functional>
#include <array>

/* Safety states per ISO 26262 */
enum class SafetyState : uint8_t {
    NORMAL      = 0,  /* All checks passing */
    DEGRADED    = 1,  /* Non-critical fault, reduced functionality */
    SAFE        = 2,  /* Safety mechanism activated, outputs frozen/disabled */
    NO_DEMAND   = 3,  /* System in safe state due to no demand */
    FAULT       = 4   /* Unrecoverable fault */
};

/* Fault codes for diagnostics (DTC) */
enum class FaultCode : uint16_t {
    NONE                = 0x0000,
    E2E_CRC_ERROR       = 0x0100,
    E2E_COUNTER_ERROR   = 0x0101,
    E2E_TIMEOUT         = 0x0102,
    BUS_OFF             = 0x0200,
    PASSIVE_ERROR       = 0x0201,
    CHECKSUM_MISMATCH   = 0x0300,
    RAM_CORRUPTION      = 0x0400,
    STACK_OVERFLOW      = 0x0401
};

/* Fault event with ISO 26262 context */
struct FaultEvent {
    FaultCode   code;
    uint8_t     asil_level;     /* 0=QM, 1=A, 2=B, 3=C, 4=D */
    bool        safety_relevant;
    uint32_t    timestamp_ms;
    uint8_t     occurrence_count;
};

/* Configurable safe state actions */
using SafeStateAction = std::function<void(FaultCode)>;

class CanSafetyManager {
public:
    static constexpr uint8_t MAX_FAULTS = 16;

    explicit CanSafetyManager(SafeStateAction action)
        : current_state_(SafetyState::NORMAL)
        , fault_count_(0)
        , safe_state_action_(action)
    {}

    /**
     * Report a fault from any part of the stack
     * Transitions state machine based on ASIL level
     */
    void report_fault(FaultCode code, uint8_t asil_level, uint32_t timestamp_ms) {
        /* Store fault in event log */
        if (fault_count_ < MAX_FAULTS) {
            fault_log_[fault_count_++] = FaultEvent{
                code, asil_level, true, timestamp_ms, 1
            };
        }

        /* State machine transition */
        if (asil_level >= 3) { /* ASIL C or D */
            transition_to(SafetyState::SAFE, code);
        } else if (asil_level >= 2) { /* ASIL B */
            if (current_state_ == SafetyState::DEGRADED) {
                transition_to(SafetyState::SAFE, code);
            } else {
                transition_to(SafetyState::DEGRADED, code);
            }
        } else { /* ASIL A / QM */
            /* Log only, no state change unless repeated */
            check_repeated_faults(code);
        }
    }

    SafetyState current_state() const { return current_state_; }
    uint8_t fault_count() const { return fault_count_; }

    /* Reset to NORMAL after fault recovery (requires explicit operator action) */
    bool recover() {
        if (current_state_ == SafetyState::DEGRADED) {
            fault_count_ = 0;
            current_state_ = SafetyState::NORMAL;
            return true;
        }
        return false; /* SAFE/FAULT states require hardware reset */
    }

private:
    SafetyState current_state_;
    uint8_t     fault_count_;
    SafeStateAction safe_state_action_;
    std::array<FaultEvent, MAX_FAULTS> fault_log_{};

    void transition_to(SafetyState new_state, FaultCode code) {
        if (new_state > current_state_) { /* Only escalate, never downgrade */
            current_state_ = new_state;
            if (new_state == SafetyState::SAFE || new_state == SafetyState::FAULT) {
                if (safe_state_action_) {
                    safe_state_action_(code);
                }
            }
        }
    }

    void check_repeated_faults(FaultCode code) {
        for (uint8_t i = 0; i < fault_count_; i++) {
            if (fault_log_[i].code == code) {
                fault_log_[i].occurrence_count++;
                if (fault_log_[i].occurrence_count >= 3) {
                    transition_to(SafetyState::DEGRADED, code);
                }
                return;
            }
        }
    }
};
```

### Dual-Channel CAN Voter (ASIL D)

```cpp
/**
 * Dual-Channel CAN Signal Voter for ASIL D
 * Compares signals from redundant CAN channels and outputs the agreed value
 */

#include <cstdint>
#include <cmath>
#include <optional>

template <typename T>
class DualChannelVoter {
public:
    struct VoterResult {
        T       value;          /* Agreed output value */
        bool    agreement;      /* True if channels agreed */
        bool    channel_a_valid;
        bool    channel_b_valid;
    };

    /**
     * @param tolerance  Maximum allowed difference between channels
     */
    explicit DualChannelVoter(T tolerance)
        : tolerance_(tolerance)
        , divergence_count_(0)
    {}

    /**
     * Vote between two redundant channel values
     * Returns the agreed value or triggers safe state on persistent divergence
     */
    VoterResult vote(const T& channel_a, bool a_valid,
                     const T& channel_b, bool b_valid) {
        VoterResult result{};
        result.channel_a_valid = a_valid;
        result.channel_b_valid = b_valid;

        if (a_valid && b_valid) {
            T diff = (channel_a > channel_b) ? (channel_a - channel_b)
                                              : (channel_b - channel_a);
            if (diff <= tolerance_) {
                /* Agreement: use average */
                result.value     = (channel_a + channel_b) / T(2);
                result.agreement = true;
                divergence_count_ = 0;
            } else {
                /* Divergence: count occurrences */
                divergence_count_++;
                result.agreement = false;
                /* Use the last agreed value (or channel A as fallback) */
                result.value = last_agreed_value_.value_or(channel_a);
            }
        } else if (a_valid) {
            result.value     = channel_a;
            result.agreement = false; /* Only one channel available */
        } else if (b_valid) {
            result.value     = channel_b;
            result.agreement = false;
        } else {
            /* Both channels invalid — safe state required */
            result.agreement = false;
            divergence_count_ = max_divergence_; /* Force safe state */
        }

        if (result.agreement) {
            last_agreed_value_ = result.value;
        }

        return result;
    }

    bool needs_safe_state() const { return divergence_count_ >= max_divergence_; }

    static constexpr uint8_t max_divergence_ = 3;

private:
    T           tolerance_;
    uint8_t     divergence_count_;
    std::optional<T> last_agreed_value_;
};

/* Usage example: Redundant wheel speed CAN signals */
void example_wheel_speed_voter() {
    DualChannelVoter<float> voter(5.0f); /* 5 km/h tolerance */

    float speed_a = 60.2f;   /* km/h from CAN channel A */
    float speed_b = 60.5f;   /* km/h from CAN channel B */

    auto result = voter.vote(speed_a, true, speed_b, true);

    if (voter.needs_safe_state()) {
        /* Persistent divergence → apply brakes, halt vehicle */
    } else if (result.agreement) {
        /* Use result.value for control algorithm */
    }
}
```

---

## Implementation in Rust

Rust's ownership model and type system are well suited for safety-critical CAN implementations. The compiler enforces memory safety statically, reducing the risk of undefined behavior in ASIL-rated software.

### E2E P01 Protection in Rust

```rust
/// E2E Profile P01 protection layer
/// Implements CRC8 + 4-bit rolling counter for ASIL A/B CAN messages

// CRC8 polynomial 0x1D (SAE J1850)
const CRC8_POLYNOMIAL: u8 = 0x1D;

fn build_crc8_table() -> [u8; 256] {
    let mut table = [0u8; 256];
    for (i, entry) in table.iter_mut().enumerate() {
        let mut crc = i as u8;
        for _ in 0..8 {
            if crc & 0x80 != 0 {
                crc = (crc << 1) ^ CRC8_POLYNOMIAL;
            } else {
                crc <<= 1;
            }
        }
        *entry = crc;
    }
    table
}

/// E2E P01 configuration (immutable, shared across sender/receiver)
#[derive(Debug, Clone, Copy)]
pub struct E2eP01Config {
    pub data_id_nibble: u8,      // 4-bit unique message identifier
    pub max_delta_counter: u8,   // Max allowed counter jump per cycle
}

/// Check status returned by E2E check
#[derive(Debug, PartialEq, Clone, Copy)]
pub enum E2eStatus {
    Ok,
    OkSomeLost,
    Repeated,
    WrongSequence,
    WrongCrc,
    Initial,
}

/// Receiver state (mutable across calls)
#[derive(Debug, Default)]
pub struct E2eP01ReceiverState {
    pub last_counter: u8,
    pub initialized: bool,
}

/// Sender state
#[derive(Debug, Default)]
pub struct E2eP01SenderState {
    pub counter: u8,
}

/// E2E P01 Protect: called by sender before transmitting.
/// Writes DataID nibble + counter into byte[0], CRC into byte[1].
///
/// # Panics
/// Panics if payload is less than 2 bytes (required minimum).
pub fn e2e_p01_protect(
    config: &E2eP01Config,
    state: &mut E2eP01SenderState,
    payload: &mut [u8],
) {
    assert!(payload.len() >= 2, "E2E P01 payload must be at least 2 bytes");

    let table = build_crc8_table();

    // Byte 0: [7:4] = DataID nibble, [3:0] = counter
    payload[0] = ((config.data_id_nibble & 0x0F) << 4) | (state.counter & 0x0F);

    // CRC over byte[0] and byte[2..] (excluding the CRC byte itself at index 1)
    let crc = payload[2..]
        .iter()
        .fold(table[0xFF ^ payload[0] as usize], |acc, &byte| {
            table[acc as usize ^ byte as usize]
        });
    payload[1] = crc;

    // Increment counter: 0..=14, skip 15
    state.counter = (state.counter + 1) % 15;
}

/// E2E P01 Check: called by receiver after reception.
pub fn e2e_p01_check(
    config: &E2eP01Config,
    state: &mut E2eP01ReceiverState,
    payload: &[u8],
) -> E2eStatus {
    assert!(payload.len() >= 2, "E2E P01 payload must be at least 2 bytes");

    let table = build_crc8_table();

    let recv_data_id = (payload[0] >> 4) & 0x0F;
    let recv_counter = payload[0] & 0x0F;
    let recv_crc     = payload[1];

    // Verify DataID
    if recv_data_id != config.data_id_nibble {
        return E2eStatus::WrongCrc;
    }

    // Recalculate CRC
    let calc_crc = payload[2..]
        .iter()
        .fold(table[0xFF ^ payload[0] as usize], |acc, &byte| {
            table[acc as usize ^ byte as usize]
        });

    if calc_crc != recv_crc {
        return E2eStatus::WrongCrc;
    }

    // First message
    if !state.initialized {
        state.last_counter = recv_counter;
        state.initialized  = true;
        return E2eStatus::Initial;
    }

    // Counter delta (modulo 15)
    let delta = recv_counter.wrapping_sub(state.last_counter) % 15;
    state.last_counter = recv_counter;

    match delta {
        0 => E2eStatus::Repeated,
        1 => E2eStatus::Ok,
        d if d <= config.max_delta_counter => E2eStatus::OkSomeLost,
        _ => E2eStatus::WrongSequence,
    }
}

#[cfg(test)]
mod e2e_tests {
    use super::*;

    #[test]
    fn test_protect_and_check_ok() {
        let config = E2eP01Config { data_id_nibble: 0x5, max_delta_counter: 3 };
        let mut sender = E2eP01SenderState::default();
        let mut receiver = E2eP01ReceiverState::default();

        let mut payload = [0u8; 8];
        payload[2] = 0xDE;
        payload[3] = 0xAD;

        // First message (Initial)
        e2e_p01_protect(&config, &mut sender, &mut payload);
        let status = e2e_p01_check(&config, &mut receiver, &payload);
        assert_eq!(status, E2eStatus::Initial);

        // Second message (Ok)
        e2e_p01_protect(&config, &mut sender, &mut payload);
        let status = e2e_p01_check(&config, &mut receiver, &payload);
        assert_eq!(status, E2eStatus::Ok);
    }

    #[test]
    fn test_crc_corruption_detected() {
        let config = E2eP01Config { data_id_nibble: 0x3, max_delta_counter: 3 };
        let mut sender = E2eP01SenderState::default();
        let mut receiver = E2eP01ReceiverState::default();

        let mut payload = [0u8; 8];
        e2e_p01_protect(&config, &mut sender, &mut payload);

        // Corrupt a data byte
        payload[4] ^= 0xFF;

        let status = e2e_p01_check(&config, &mut receiver, &payload);
        assert_eq!(status, E2eStatus::WrongCrc);
    }
}
```

### CAN Timeout Supervisor in Rust

```rust
/// CAN message timeout supervisor
/// Monitors reception freshness for safety-critical messages

use std::time::{Duration, Instant};

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SupervisionStatus {
    Ok,
    Timeout,
    Initializing,
}

/// Per-message supervision slot
#[derive(Debug)]
pub struct MessageSupervision {
    pub timeout:        Duration,
    pub max_missed:     u32,
    last_received:      Option<Instant>,
    missed_count:       u32,
    enabled:            bool,
}

impl MessageSupervision {
    pub fn new(timeout: Duration, max_missed: u32) -> Self {
        Self {
            timeout,
            max_missed,
            last_received: None,
            missed_count: 0,
            enabled: true,
        }
    }

    /// Signal that a valid message was received
    pub fn signal_received(&mut self) {
        self.last_received = Some(Instant::now());
        self.missed_count  = 0;
    }

    /// Check current supervision status
    pub fn check(&mut self) -> SupervisionStatus {
        if !self.enabled {
            return SupervisionStatus::Ok;
        }

        match self.last_received {
            None => SupervisionStatus::Initializing,
            Some(t) => {
                if t.elapsed() > self.timeout {
                    self.missed_count += 1;
                    if self.missed_count >= self.max_missed {
                        SupervisionStatus::Timeout
                    } else {
                        SupervisionStatus::Ok
                    }
                } else {
                    self.missed_count = 0;
                    SupervisionStatus::Ok
                }
            }
        }
    }
}

/// Aggregated supervisor for a set of CAN messages
pub struct CanSupervisionManager {
    slots: Vec<(String, MessageSupervision)>,
}

impl CanSupervisionManager {
    pub fn new() -> Self {
        Self { slots: Vec::new() }
    }

    /// Register a message for supervision
    pub fn register(&mut self, name: &str, timeout: Duration, max_missed: u32) -> usize {
        let idx = self.slots.len();
        self.slots.push((
            name.to_owned(),
            MessageSupervision::new(timeout, max_missed),
        ));
        idx
    }

    /// Signal valid reception by index
    pub fn on_received(&mut self, idx: usize) {
        if let Some((_, sup)) = self.slots.get_mut(idx) {
            sup.signal_received();
        }
    }

    /// Run all checks. Returns list of timed-out message names.
    pub fn check_all(&mut self) -> Vec<&str> {
        self.slots
            .iter_mut()
            .filter_map(|(name, sup)| {
                if sup.check() == SupervisionStatus::Timeout {
                    Some(name.as_str())
                } else {
                    None
                }
            })
            .collect()
    }
}

#[cfg(test)]
mod supervision_tests {
    use super::*;
    use std::thread;

    #[test]
    fn test_timeout_detection() {
        let mut sup = MessageSupervision::new(
            Duration::from_millis(10),
            2,
        );
        sup.signal_received();
        assert_eq!(sup.check(), SupervisionStatus::Ok);

        // Simulate timeout
        thread::sleep(Duration::from_millis(15));
        assert_eq!(sup.check(), SupervisionStatus::Ok);   // missed_count = 1
        assert_eq!(sup.check(), SupervisionStatus::Timeout); // missed_count = 2
    }
}
```

### Dual-Channel Voter in Rust

```rust
/// Generic dual-channel voter for ASIL D redundant CAN signals

#[derive(Debug, PartialEq)]
pub enum VoterDecision {
    Agreed,       // Both channels valid and within tolerance
    Degraded,     // Only one channel valid
    Diverged,     // Both valid but outside tolerance
    NoValidInput, // Both channels invalid
}

#[derive(Debug)]
pub struct VoterResult<T> {
    pub value:    Option<T>,
    pub decision: VoterDecision,
}

#[derive(Debug)]
pub struct DualChannelVoter<T> {
    tolerance:         T,
    divergence_count:  u32,
    max_divergence:    u32,
    last_agreed:       Option<T>,
}

impl<T> DualChannelVoter<T>
where
    T: Copy
     + PartialOrd
     + std::ops::Add<Output = T>
     + std::ops::Sub<Output = T>
     + std::ops::Div<Output = T>
     + From<u8>,
{
    pub fn new(tolerance: T, max_divergence: u32) -> Self {
        Self {
            tolerance,
            divergence_count: 0,
            max_divergence,
            last_agreed: None,
        }
    }

    /// Compare two redundant channel values
    pub fn vote(
        &mut self,
        ch_a: Option<T>,
        ch_b: Option<T>,
    ) -> VoterResult<T> {
        match (ch_a, ch_b) {
            (Some(a), Some(b)) => {
                let diff = if a > b { a - b } else { b - a };
                if diff <= self.tolerance {
                    let avg = (a + b) / T::from(2u8);
                    self.divergence_count = 0;
                    self.last_agreed = Some(avg);
                    VoterResult { value: Some(avg), decision: VoterDecision::Agreed }
                } else {
                    self.divergence_count += 1;
                    VoterResult {
                        value: self.last_agreed,
                        decision: VoterDecision::Diverged,
                    }
                }
            }
            (Some(a), None) => VoterResult {
                value: Some(a),
                decision: VoterDecision::Degraded,
            },
            (None, Some(b)) => VoterResult {
                value: Some(b),
                decision: VoterDecision::Degraded,
            },
            (None, None) => {
                self.divergence_count = self.max_divergence; // force safe state
                VoterResult { value: None, decision: VoterDecision::NoValidInput }
            }
        }
    }

    /// True if persistent divergence requires safe-state transition
    pub fn safe_state_required(&self) -> bool {
        self.divergence_count >= self.max_divergence
    }
}

#[cfg(test)]
mod voter_tests {
    use super::*;

    #[test]
    fn test_agreement() {
        let mut voter = DualChannelVoter::<f32>::new(5.0, 3);
        let r = voter.vote(Some(60.0), Some(61.0));
        assert_eq!(r.decision, VoterDecision::Agreed);
        assert!((r.value.unwrap() - 60.5).abs() < 0.01);
    }

    #[test]
    fn test_divergence_triggers_safe_state() {
        let mut voter = DualChannelVoter::<f32>::new(5.0, 3);
        for _ in 0..3 {
            voter.vote(Some(0.0), Some(100.0));
        }
        assert!(voter.safe_state_required());
    }
}
```

---

## Diagnostic Coverage and FMEA

### Failure Mode and Effects Analysis for CAN

| Failure Mode | Effect | Detection Mechanism | DC Class |
|---|---|---|---|
| Bit corruption on bus | Wrong signal value received | CAN CRC + E2E CRC8/CRC32 | High |
| Message not sent (sender freeze) | Receiver acts on stale data | Timeout supervision | High |
| Message sent with wrong ID | Wrong receiver processes frame | E2E DataID | High |
| Counter not incrementing (sender freeze) | Repeated message not detected | E2E Counter | High |
| Wrong data length | Truncated payload | DLC check + CAN frame check | High |
| Bus-off condition | No messages on bus | CAN TEC/REC counters + bus-off IRQ | High |
| Spurious message injection | Incorrect control action | E2E DataID + SecOC (ASIL D) | Medium |
| Delayed message (jitter) | Timing-sensitive control degraded | Cycle time monitoring | Medium |
| CAN transceiver failure | Partial or no communication | Hardware self-test, loop-back | High |
| ECU RAM corruption | Wrong payload built | Memory ECC + software CRC | High |

### Recommended Safety Mechanisms by ASIL

| Safety Mechanism | ASIL A | ASIL B | ASIL C | ASIL D |
|---|:---:|:---:|:---:|:---:|
| CAN built-in CRC | ✅ | ✅ | ✅ | ✅ |
| E2E Profile P01/P02 | ✅ | ✅ | — | — |
| E2E Profile P04/P07 | — | — | ✅ | ✅ |
| Reception timeout | ✅ | ✅ | ✅ | ✅ |
| Counter supervision | — | ✅ | ✅ | ✅ |
| Dual-channel redundancy | — | — | ✅ | ✅ |
| Cross-ECU comparison voter | — | — | ✅ | ✅ |
| SecOC message authentication | — | — | — | ✅ |
| Hardware ECC on CAN buffers | — | ✅ | ✅ | ✅ |
| Periodic RAM/ROM test | — | ✅ | ✅ | ✅ |
| Watchdog (internal + external) | ✅ | ✅ | ✅ | ✅ |

---

## Summary

Designing CAN communication systems to meet ISO 26262 ASIL requirements involves a layered approach spanning hardware, protocol, and software:

**ASIL Classification** drives all safety decisions. The HARA assigns an ASIL to each safety function, and this ASIL propagates to the CAN messages that carry signals for that function. ASIL D functions (e.g., steering, braking) require dual-channel redundancy and the strongest E2E profiles.

**E2E Protection** (AUTOSAR profiles P01–P07) is the primary software safety mechanism for CAN messages. It adds a CRC, rolling counter, and Data ID to each message, detecting corruption, repetition, sequence errors, and routing faults. Selecting the right profile depends on the required ASIL and payload size.

**Timeout Supervision** is mandatory for all safety-relevant messages. If a message is not received within its defined cycle time (typically monitored at 1.5–3× the nominal period), the receiver must transition to a safe state or use a safe default value.

**Fault Metrics** (SPFM, LFM, PMHF) are calculated during hardware FMEA to demonstrate that the combination of CAN's built-in error detection and software safety mechanisms achieves sufficient diagnostic coverage. ASIL D targets ≥99% SPFM and a PMHF below 10⁻⁸/h.

**Redundancy and Voting** are required for ASIL C and D. Dual physical CAN channels running independent hardware and software stacks feed a voter that detects persistent disagreement and triggers a safe state. ASIL D additionally requires heterogeneous redundancy to prevent common-cause failures.

**Rust** offers particular advantages for safety-critical CAN code: the ownership model eliminates data races in interrupt/task contexts, the type system prevents integer overflow at compile time (checked arithmetic by default in debug mode), and `#[no_std]` compatibility enables bare-metal deployment on automotive MCUs without an OS.

The overall ISO 26262 development process requires that all safety mechanisms be documented in the Technical Safety Concept, verified through requirements-based testing, and validated by a functional safety audit — the code patterns above represent the implementation layer of that larger process.