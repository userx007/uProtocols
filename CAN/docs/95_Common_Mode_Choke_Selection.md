# 95. Common Mode Choke Selection for CAN Bus

**Theory & Hardware** — explains common mode vs. differential mode noise, how a CMC's opposing windings cancel differential signal flux while blocking common mode currents, and provides a complete parameter selection guide covering L_cm, leakage inductance, SRF, winding balance, DCR, and rated current for both classic CAN and CAN FD.

**Code Examples (C/C++):**
- `can_emi_monitor.c` — Linux SocketCAN error frame parser that categorises bit, form, stuff, and ACK errors and provides an EMI diagnostic assessment
- `cmc_selector.cpp` — Engineering calculator that scores CMC candidates against a given CAN configuration, checking all key parameters with pass/warn/fail grading
- `can_adaptive_bitrate.c` — Embedded state machine that reduces CAN bit rate when EMI-driven error counters exceed thresholds, with automatic restoration after a clean period

**Code Examples (Rust):**
- `cmc_evaluator.rs` — Type-safe CMC evaluator using idiomatic Rust structs and enums, mirroring the C++ selector with ranked output
- `can_emi_stats.rs` — Async CAN error monitor using the `socketcan` crate, parsing Linux error frames and producing a diagnostic report

**Practical guidance** on PCB layout, Y-capacitor placement, part number examples from Würth, TDK, Murata, and Bourns, and validation methods (eye diagram, BCI testing per ISO 11452-4, CISPR 25).

## Choosing Appropriate Common Mode Filters to Reduce EMI and Improve Signal Integrity

---

## Table of Contents

1. [Introduction](#introduction)
2. [Fundamentals of CAN Bus Signal Integrity](#fundamentals)
3. [Common Mode vs. Differential Mode Noise](#common-vs-differential)
4. [How Common Mode Chokes Work](#how-cmcs-work)
5. [Key Selection Parameters](#key-selection-parameters)
6. [Application-Specific Considerations](#application-specific)
7. [Circuit Placement and Layout Guidelines](#circuit-placement)
8. [C/C++ Implementation Examples](#c-cpp-examples)
9. [Rust Implementation Examples](#rust-examples)
10. [Testing and Validation](#testing-validation)
11. [Summary](#summary)

---

## 1. Introduction 

Controller Area Network (CAN) bus is a robust, differential serial communication protocol widely deployed in automotive, industrial automation, medical devices, and aerospace systems. While the differential signaling of CAN inherently rejects common mode noise, real-world installations face challenging electromagnetic environments that can corrupt messages, increase error rates, and cause EMC compliance failures.

A **common mode choke (CMC)** — also called a common mode filter or common mode inductor — is a passive two-winding component placed on the CAN bus lines (CANH and CANL) to suppress common mode interference while allowing the differential CAN signal to pass with minimal attenuation. Proper CMC selection is critical to achieving both functional reliability and regulatory EMC compliance (e.g., CISPR 25, ISO 11452, FCC Part 15).

---

## 2. Fundamentals of CAN Bus Signal Integrity 

### CAN Electrical Characteristics

| Parameter | ISO 11898-2 (High-Speed CAN) | Value |
|---|---|---|
| Nominal bit rates | Up to 1 Mbit/s (CAN) / 8 Mbit/s (CAN FD) | — |
| Differential voltage (dominant) | CANH − CANL | 1.5 V – 3.0 V |
| Differential voltage (recessive) | CANH − CANL | −0.5 V – +0.05 V |
| Common mode range | Both lines together | −2 V to +7 V |
| Bus termination | 120 Ω at each end | — |

### Why EMI is a Problem

CAN transceivers switch rapidly. At 1 Mbit/s, signal edges can have rise times of 50–200 ns, generating significant energy in the 5 MHz–100 MHz range. In automotive and industrial environments, this energy:

- Radiates from unshielded cables (acting as antennas)
- Couples into adjacent signal lines via crosstalk
- Can be amplified by cable resonances
- Must comply with strict automotive OEM and regulatory emission limits

---

## 3. Common Mode vs. Differential Mode Noise 

Understanding the distinction between noise types is essential for correct filter selection:

```
         CANH ─────────────────────────────────►
                        \     /
                   Common Mode   Differential Mode
                        /     \
         CANL ─────────────────────────────────►

  Common Mode:      Both lines carry same noise in same direction
  Differential Mode: Noise appears as a voltage difference between lines
```

**Common mode noise** is the dominant EMI source in CAN systems because:
- Ground potential differences between nodes create common mode currents
- Capacitive coupling from power supplies induces common mode voltage
- Long cable runs act as loop antennas for external fields

**Differential mode noise** is generally lower in CAN but can arise from impedance imbalances in the cable or PCB layout.

A CMC addresses **common mode** noise; a differential-mode ferrite bead or LC filter addresses **differential** noise. Most CAN EMI problems benefit primarily from a CMC.

---

## 4. How Common Mode Chokes Work 

A common mode choke consists of two windings wound on a single toroidal or drum core, with the windings wound in **opposing directions** relative to the core flux:

```
              CANH ──┤ L1 ├── CANH'
                        |
                     Shared
                      Core
                        |
              CANL ──┤ L2 ├── CANL'

  Differential signal (CANH vs CANL opposite polarity):
    → Flux in L1 and L2 cancel → Low impedance → Signal passes through

  Common mode noise (same polarity on both lines):
    → Flux in L1 and L2 add → High impedance → Noise is blocked/reflected
```

### Equivalent Circuit

For differential signals:
```
  Z_differential ≈ 2 × Lleakage   (typically very small, 0.5–5 µH)
```

For common mode noise:
```
  Z_common = 2 × L_common = 2 × µ × N² × Ae / le   (typically 100 µH – 100 mH)
```

This ratio — high common mode impedance, low differential impedance — is the fundamental operating principle.

---

## 5. Key Selection Parameters 

### 5.1 Common Mode Inductance (L_cm)

The inductance seen by common mode signals. Higher values provide more attenuation at lower frequencies.

**Guideline for CAN:**
- Classic CAN (≤1 Mbit/s): 100 µH – 10 mH at 100 kHz
- CAN FD (up to 8 Mbit/s): 100 µH – 1 mH (lower L to reduce differential insertion loss)

### 5.2 Differential Mode Impedance / Insertion Loss

The choke must not attenuate the CAN differential signal. Evaluate:
- Differential insertion loss (should be < 1 dB in the signal band)
- Differential leakage inductance (keep < 10% of L_cm)

For CAN FD at 8 Mbit/s, bandwidth extends to ~40 MHz; choose a CMC with low differential capacitance to avoid resonances.

### 5.3 Rated Current

CAN transceivers draw modest currents (typically < 100 mA), but DC bias reduces effective inductance in ferrite cores. Select a CMC rated for at least 2× the maximum expected current.

**Common ratings for CAN:** 100 mA – 500 mA

### 5.4 DC Resistance (DCR)

DCR causes voltage drop and power dissipation. For CAN:
- Target DCR < 10 Ω per winding (< 1 Ω preferred for high-current nodes)

### 5.5 Self-Resonant Frequency (SRF)

Above the SRF, the choke becomes capacitive and loses common mode rejection. Ensure:
```
  SRF > 3 × f_max_noise
```

For noise up to 200 MHz, choose a CMC with SRF > 600 MHz.

### 5.6 Impedance vs. Frequency Profile

Request or plot the `|Z_cm| vs. frequency` curve from the manufacturer's datasheet. For CAN:

```
  Frequency Range    |Z_cm| Target
  ─────────────────────────────────
  100 kHz            > 100 Ω
  1 MHz              > 500 Ω
  10 MHz             > 1 kΩ
  100 MHz            > 500 Ω  (declining is acceptable)
```

### 5.7 Balancing (Symmetry)

Imbalance between the two windings converts differential-mode signals into common mode — the opposite of what you want. Look for:
- Inductance matching between windings: ΔL/L < 2%
- Specified longitudinal conversion loss (LCL) > 40 dB

### 5.8 Temperature Range

Automotive: −40°C to +125°C (AEC-Q200 qualified components recommended)
Industrial: −40°C to +85°C

---

## 6. Application-Specific Considerations

### 6.1 Automotive CAN (ISO 11898-2)

- Compliance targets: CISPR 25 Class 5, ISO 11452-2/4
- Recommended CMC: Würth 744232 series, TDK ACM series, Murata DLW series
- Typical value: 100 µH at 100 kHz, 200 mA rated
- Place CMC at the connector/harness entry point
- Add 100 pF X2Y capacitors or common mode capacitors (4.7 nF to chassis) after CMC

### 6.2 CAN FD (ISO 11898-2:2016)

CAN FD's higher data-phase bit rates (2–8 Mbit/s) demand lower leakage inductance:
- Differential leakage: < 200 nH
- CMC with SRF > 500 MHz
- Evaluate differential signal eye diagram with CMC in-circuit

### 6.3 Industrial CAN (DeviceNet, CANopen)

- Longer cable runs (up to 500 m) increase susceptibility
- Higher common mode inductance acceptable (lower bit rates)
- 1 mH – 10 mH CMC at 100 kHz
- Consider toroidal CMC with shielded winding

### 6.4 Common CMC Part Examples

| Part Number | Manufacturer | L_cm (typ) | DCR | I_rated | SRF | Notes |
|---|---|---|---|---|---|---|
| ACM2012-900-2P-T | TDK | 900 µH | 1.0 Ω | 100 mA | >100 MHz | SMD 0805 |
| DLW5BTM102SQ2L | Murata | 1000 µH | 0.5 Ω | 300 mA | 200 MHz | SMD |
| 744232090 | Würth | 90 µH | 0.35 Ω | 600 mA | 500 MHz | AEC-Q200 |
| MESC4000 | EPCOS | 4 mH | 1.2 Ω | 150 mA | 50 MHz | Through-hole |
| SRF2012-102Y | Bourns | 1000 µH | 0.8 Ω | 200 mA | 150 MHz | SMD |

---

## 7. Circuit Placement and Layout Guidelines 

### Recommended CAN Bus EMI Filter Circuit

```
 Connector                                            Transceiver
    │                                                     │
  CANH ──┬── CMC_L1 ──┬────────────────────────── CANH
         │            │
        Cy1          Cx     ← Optional: 4.7 nF X-cap (diff mode)
         │            │
  CANL ──┴── CMC_L2 ──┴────────────────────────── CANL
         │
        GND/Chassis
         │
        Cy2 (4.7 nF Y-cap to chassis on each line for CM filtering)
```

### PCB Layout Rules

1. **Place CMC as close to the connector as possible** — filter noise at the entry point before it enters the PCB
2. **Keep CMC traces short and direct** — avoid long traces between the connector and CMC
3. **Separate filtered and unfiltered sections** — use PCB ground pour or copper pours as barriers
4. **Avoid routing other signals near the CMC** — magnetic field from the choke can couple into adjacent traces
5. **Use a solid ground plane** — essential for the Y-capacitors to provide a low-impedance return path
6. **Place Y-capacitors at the CMC output** — between each CAN line and chassis ground (4.7 nF – 10 nF)
7. **Keep CMC away from power inductors** — mutual inductance degrades common mode performance

---

## 8. C/C++ Implementation Examples 

While common mode chokes are passive hardware components selected at design time, software plays a critical role in monitoring CAN bus health, detecting EMI-related errors, and adapting transmission parameters. The following examples demonstrate these capabilities.

### 8.1 CAN Bus Error Monitoring (Linux SocketCAN)

```c
/**
 * can_emi_monitor.c
 *
 * Monitors CAN bus error frames to detect EMI-induced communication
 * problems that might indicate inadequate common mode filtering.
 *
 * Compile: gcc -o can_emi_monitor can_emi_monitor.c
 * Usage:   ./can_emi_monitor can0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>

/* Error statistics structure */
typedef struct {
    uint64_t total_frames;
    uint64_t error_frames;
    uint64_t bit_errors;
    uint64_t form_errors;
    uint64_t stuff_errors;
    uint64_t ack_errors;
    uint64_t crc_errors;
    uint64_t bus_off_events;
    uint64_t error_passive_events;
    time_t   start_time;
} can_error_stats_t;

/**
 * Parse a CAN error frame and update statistics.
 * High CRC/bit-error rates often indicate common mode EMI problems.
 */
static void parse_error_frame(const struct can_frame *frame,
                               can_error_stats_t *stats)
{
    stats->error_frames++;

    /* Bus state transitions */
    if (frame->can_id & CAN_ERR_BUSOFF) {
        stats->bus_off_events++;
        fprintf(stderr, "[ERROR] Bus-off event detected! "
                        "Check common mode choke and termination.\n");
    }

    if (frame->can_id & CAN_ERR_CRTL) {
        if (frame->data[1] & CAN_ERR_CRTL_RX_PASSIVE ||
            frame->data[1] & CAN_ERR_CRTL_TX_PASSIVE) {
            stats->error_passive_events++;
        }
    }

    /* Protocol error types — crucial for EMI diagnosis */
    if (frame->can_id & CAN_ERR_PROT) {
        uint8_t prot_type  = frame->data[2];
        uint8_t prot_loc   = frame->data[3];

        if (prot_type & CAN_ERR_PROT_BIT)   stats->bit_errors++;
        if (prot_type & CAN_ERR_PROT_FORM)  stats->form_errors++;
        if (prot_type & CAN_ERR_PROT_STUFF) stats->stuff_errors++;

        (void)prot_loc; /* Can be used for advanced diagnosis */
    }

    if (frame->can_id & CAN_ERR_ACK) {
        stats->ack_errors++;
    }

    /* Note: CRC errors are inferred from error-passive transitions
     * combined with high bit-error rates in many controller implementations */
}

/**
 * Print diagnostic assessment based on error statistics.
 *
 * Pattern interpretation for EMI/CMC diagnosis:
 *   High bit errors + high stuff errors → External EMI, poor common mode rejection
 *   High CRC errors                     → Signal integrity degradation
 *   High form errors                    → Impedance mismatch or ringing
 *   Bus-off events                      → Severe EMI or wiring fault
 */
static void print_diagnosis(const can_error_stats_t *stats)
{
    time_t elapsed = time(NULL) - stats->start_time;
    if (elapsed == 0) elapsed = 1;

    double error_rate = (double)stats->error_frames /
                        (double)(stats->total_frames + 1) * 100.0;

    printf("\n=== CAN Bus EMI Diagnostic Report ===\n");
    printf("Runtime:           %ld seconds\n", (long)elapsed);
    printf("Total frames:      %llu\n", (unsigned long long)stats->total_frames);
    printf("Error frames:      %llu (%.2f%%)\n",
           (unsigned long long)stats->error_frames, error_rate);
    printf("Bit errors:        %llu\n", (unsigned long long)stats->bit_errors);
    printf("Form errors:       %llu\n", (unsigned long long)stats->form_errors);
    printf("Stuff errors:      %llu\n", (unsigned long long)stats->stuff_errors);
    printf("ACK errors:        %llu\n", (unsigned long long)stats->ack_errors);
    printf("Bus-off events:    %llu\n", (unsigned long long)stats->bus_off_events);
    printf("Error-passive evs: %llu\n", (unsigned long long)stats->error_passive_events);

    /* Diagnostic assessment */
    printf("\n--- Diagnosis ---\n");
    if (error_rate > 1.0) {
        printf("[WARN] High error rate (>1%%): Investigate EMI sources.\n");
        if (stats->bit_errors > stats->form_errors * 2) {
            printf("  → Pattern suggests external common mode interference.\n");
            printf("  → Verify CMC is installed; check L_cm and SRF values.\n");
            printf("  → Consider higher common mode inductance (e.g., 1 mH → 4.7 mH).\n");
        }
        if (stats->form_errors > stats->bit_errors) {
            printf("  → Form errors dominant: Check cable impedance/termination.\n");
            printf("  → Review PCB layout around CMC.\n");
        }
    } else if (error_rate < 0.01) {
        printf("[OK] Error rate nominal (<0.01%%): CMC selection appears adequate.\n");
    } else {
        printf("[INFO] Marginal error rate: Monitor under full load conditions.\n");
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <can_interface>\n", argv[0]);
        return EXIT_FAILURE;
    }

    /* Create raw CAN socket */
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) {
        perror("socket");
        return EXIT_FAILURE;
    }

    /* Bind to specified interface */
    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(sock);
        return EXIT_FAILURE;
    }

    /* Enable error frame reception */
    can_err_mask_t err_mask = CAN_ERR_MASK;
    setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
               &err_mask, sizeof(err_mask));

    printf("Monitoring %s for EMI-related CAN errors...\n", argv[1]);
    printf("Press Ctrl+C to stop and see report.\n\n");

    can_error_stats_t stats = { .start_time = time(NULL) };
    struct can_frame frame;

    while (1) {
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) {
            if (errno == EINTR) break;  /* Signal received, print report */
            perror("read");
            break;
        }

        if (frame.can_id & CAN_ERR_FLAG) {
            parse_error_frame(&frame, &stats);
        } else {
            stats.total_frames++;
        }
    }

    print_diagnosis(&stats);
    close(sock);
    return EXIT_SUCCESS;
}
```

### 8.2 CMC Parameter Calculator (C++)

```cpp
/**
 * cmc_selector.cpp
 *
 * Engineering tool to calculate and evaluate common mode choke
 * parameters for a given CAN bus configuration.
 *
 * Compile: g++ -std=c++17 -o cmc_selector cmc_selector.cpp -lm
 */

#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>

/* Physical constants */
static constexpr double PI = 3.14159265358979323846;
static constexpr double MU_0 = 4.0 * PI * 1e-7;  // Permeability of free space (H/m)

/* CAN bus configuration */
struct CANConfig {
    double bit_rate_bps;          // Classic CAN or CAN FD arbitration rate
    double data_rate_bps;         // CAN FD data phase rate (= bit_rate for classic)
    double cable_length_m;        // Maximum cable length
    double max_current_A;         // Maximum transcever current
    double temp_min_C;            // Minimum operating temperature
    double temp_max_C;            // Maximum operating temperature
    bool   is_automotive;         // Requires AEC-Q200
};

/* CMC candidate specification */
struct CMCSpec {
    std::string part_number;
    std::string manufacturer;
    double L_cm_uH;               // Common mode inductance at 100 kHz (µH)
    double L_diff_nH;             // Differential leakage inductance (nH)
    double DCR_ohm;               // DC resistance per winding (Ω)
    double I_rated_A;             // Rated current (A)
    double SRF_MHz;               // Self-resonant frequency (MHz)
    double winding_balance_pct;   // Winding inductance mismatch (%)
    bool   aec_q200;              // AEC-Q200 qualified
};

/**
 * Calculate the -3 dB cutoff frequency for a CMC in a CAN system.
 * The CMC + source/load impedance forms a high-pass filter for common mode.
 *
 * @param L_cm_uH  Common mode inductance in µH
 * @param Z_s_ohm  Source impedance for common mode (typically 50–150 Ω)
 * @param Z_l_ohm  Load impedance for common mode (typically 50–150 Ω)
 * @return Cutoff frequency in Hz
 */
double calc_cm_cutoff_hz(double L_cm_uH, double Z_s_ohm, double Z_l_ohm)
{
    double L = L_cm_uH * 1e-6;
    double Z_total = Z_s_ohm + Z_l_ohm;
    return Z_total / (2.0 * PI * 2.0 * L);  // Factor 2 for series combination
}

/**
 * Calculate differential insertion loss caused by leakage inductance.
 * Leakage inductance forms a series impedance that attenuates differential signal.
 *
 * @param L_diff_nH    Differential leakage inductance in nH
 * @param freq_hz      Frequency in Hz
 * @param Z_line_ohm   Differential impedance of the CAN line (typically 120 Ω)
 * @return Insertion loss in dB (negative means attenuation)
 */
double calc_diff_insertion_loss_dB(double L_diff_nH, double freq_hz, double Z_line_ohm)
{
    double X_L = 2.0 * PI * freq_hz * L_diff_nH * 1e-9;
    double loss = 20.0 * std::log10(Z_line_ohm / std::sqrt(Z_line_ohm * Z_line_ohm + X_L * X_L));
    return loss;
}

/**
 * Evaluate a CMC candidate against a CAN configuration.
 * Returns a score (0–100) and prints a detailed report.
 */
struct EvaluationResult {
    double score;
    bool   pass;
    std::vector<std::string> warnings;
    std::vector<std::string> failures;
};

EvaluationResult evaluate_cmc(const CANConfig &cfg, const CMCSpec &cmc)
{
    EvaluationResult result;
    result.score = 100.0;
    result.pass  = true;

    /* --- Check 1: Current rating --- */
    double required_current = cfg.max_current_A * 2.0;  // 2× safety factor
    if (cmc.I_rated_A < required_current) {
        result.failures.push_back(
            "Current rating " + std::to_string(cmc.I_rated_A * 1000) + " mA < "
            + "required " + std::to_string(required_current * 1000) + " mA");
        result.score -= 25.0;
        result.pass = false;
    }

    /* --- Check 2: Self-resonant frequency --- */
    double max_noise_freq_MHz = 200.0;  // Target noise suppression up to 200 MHz
    double required_SRF_MHz = max_noise_freq_MHz * 3.0;
    if (cmc.SRF_MHz < required_SRF_MHz) {
        result.warnings.push_back(
            "SRF " + std::to_string(cmc.SRF_MHz) + " MHz may be marginal; "
            "recommend >" + std::to_string(required_SRF_MHz) + " MHz");
        result.score -= 10.0;
    }

    /* --- Check 3: Differential insertion loss at signal frequency --- */
    double signal_bw_hz = cfg.data_rate_bps * 5.0;  // ~5× bit rate for bandwidth estimate
    double diff_loss_dB = calc_diff_insertion_loss_dB(cmc.L_diff_nH, signal_bw_hz, 120.0);
    if (diff_loss_dB < -3.0) {
        result.failures.push_back(
            "Differential insertion loss " + std::to_string(diff_loss_dB)
            + " dB at signal bandwidth (limit: -3 dB)");
        result.score -= 30.0;
        result.pass = false;
    } else if (diff_loss_dB < -1.0) {
        result.warnings.push_back(
            "Differential insertion loss " + std::to_string(diff_loss_dB)
            + " dB: marginal. Verify eye diagram.");
        result.score -= 10.0;
    }

    /* --- Check 4: Common mode cutoff frequency --- */
    double cm_cutoff = calc_cm_cutoff_hz(cmc.L_cm_uH, 50.0, 50.0);
    double target_cm_cutoff = cfg.bit_rate_bps * 0.01;  // CMC must attenuate below 1% of bit rate
    if (cm_cutoff > target_cm_cutoff) {
        result.warnings.push_back(
            "CM cutoff " + std::to_string(cm_cutoff / 1e3) + " kHz > "
            + std::to_string(target_cm_cutoff / 1e3) + " kHz target; "
            "consider higher L_cm");
        result.score -= 15.0;
    }

    /* --- Check 5: Winding balance --- */
    if (cmc.winding_balance_pct > 5.0) {
        result.warnings.push_back(
            "Winding imbalance " + std::to_string(cmc.winding_balance_pct)
            + "% > 5%: may convert differential signal to common mode");
        result.score -= 10.0;
    }

    /* --- Check 6: Automotive qualification --- */
    if (cfg.is_automotive && !cmc.aec_q200) {
        result.failures.push_back("AEC-Q200 required for automotive; part not qualified");
        result.score -= 20.0;
        result.pass = false;
    }

    /* --- Check 7: DCR voltage drop --- */
    double v_drop = cmc.DCR_ohm * cfg.max_current_A;
    if (v_drop > 0.1) {
        result.warnings.push_back(
            "DCR voltage drop " + std::to_string(v_drop * 1000) + " mV may affect signal levels");
        result.score -= 5.0;
    }

    result.score = std::max(result.score, 0.0);
    return result;
}

void print_evaluation(const CMCSpec &cmc, const EvaluationResult &res)
{
    std::cout << "\n──────────────────────────────────────────────────\n";
    std::cout << "  " << cmc.manufacturer << " " << cmc.part_number << "\n";
    std::cout << "──────────────────────────────────────────────────\n";
    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Score:  " << res.score << "/100  ["
              << (res.pass ? "PASS" : "FAIL") << "]\n";
    std::cout << "  L_cm:   " << cmc.L_cm_uH << " µH  |  "
              << "L_diff: " << cmc.L_diff_nH << " nH  |  "
              << "DCR: " << cmc.DCR_ohm << " Ω\n";
    std::cout << "  I_rate: " << cmc.I_rated_A * 1000 << " mA  |  "
              << "SRF: " << cmc.SRF_MHz << " MHz  |  "
              << "AEC-Q200: " << (cmc.aec_q200 ? "Yes" : "No") << "\n";

    for (const auto &w : res.warnings) {
        std::cout << "  [WARN] " << w << "\n";
    }
    for (const auto &f : res.failures) {
        std::cout << "  [FAIL] " << f << "\n";
    }
}

int main()
{
    /* Define target CAN configuration */
    CANConfig can_cfg = {
        .bit_rate_bps   = 500e3,   // 500 kbit/s CAN FD arbitration
        .data_rate_bps  = 2e6,     // 2 Mbit/s CAN FD data phase
        .cable_length_m = 10.0,
        .max_current_A  = 0.08,    // 80 mA maximum
        .temp_min_C     = -40.0,
        .temp_max_C     = 125.0,
        .is_automotive  = true,
    };

    /* CMC candidates to evaluate */
    std::vector<CMCSpec> candidates = {
        {
            .part_number       = "744232090",
            .manufacturer      = "Würth Elektronik",
            .L_cm_uH           = 90.0,
            .L_diff_nH         = 150.0,
            .DCR_ohm           = 0.35,
            .I_rated_A         = 0.600,
            .SRF_MHz           = 500.0,
            .winding_balance_pct = 1.5,
            .aec_q200          = true,
        },
        {
            .part_number       = "ACM2012-900-2P-T",
            .manufacturer      = "TDK",
            .L_cm_uH           = 900.0,
            .L_diff_nH         = 2000.0,  // Higher leakage
            .DCR_ohm           = 1.0,
            .I_rated_A         = 0.100,
            .SRF_MHz           = 100.0,
            .winding_balance_pct = 3.0,
            .aec_q200          = false,
        },
        {
            .part_number       = "DLW5BTM102SQ2L",
            .manufacturer      = "Murata",
            .L_cm_uH           = 1000.0,
            .L_diff_nH         = 300.0,
            .DCR_ohm           = 0.50,
            .I_rated_A         = 0.300,
            .SRF_MHz           = 200.0,
            .winding_balance_pct = 2.0,
            .aec_q200          = true,
        },
    };

    std::cout << "=== CAN Bus Common Mode Choke Selector ===\n";
    std::cout << "Configuration: CAN FD, "
              << can_cfg.bit_rate_bps / 1e3 << " kbit/s arb / "
              << can_cfg.data_rate_bps / 1e6 << " Mbit/s data, "
              << (can_cfg.is_automotive ? "Automotive" : "Industrial") << "\n";

    /* Evaluate and sort by score */
    std::vector<std::pair<CMCSpec, EvaluationResult>> results;
    for (const auto &cmc : candidates) {
        results.emplace_back(cmc, evaluate_cmc(can_cfg, cmc));
    }
    std::sort(results.begin(), results.end(),
              [](const auto &a, const auto &b) {
                  return a.second.score > b.second.score;
              });

    for (const auto &[cmc, res] : results) {
        print_evaluation(cmc, res);
    }

    std::cout << "\n=== Recommendation: "
              << results[0].first.manufacturer << " "
              << results[0].first.part_number
              << " (Score: " << results[0].second.score << ") ===\n";

    return 0;
}
```

### 8.3 Adaptive Bit Rate Fallback on EMI Detection (Embedded C)

```c
/**
 * can_adaptive_bitrate.c
 *
 * Embedded controller that monitors CAN error counters and
 * reduces bit rate when EMI causes excessive errors — a
 * defensive strategy when CMC filtering is insufficient.
 *
 * Targets: STM32 / NXP S32K / Generic AUTOSAR-style HAL
 */

#include <stdint.h>
#include <stdbool.h>

/* --- Platform HAL stubs (replace with actual HAL calls) --- */
typedef struct {
    uint32_t TEC;  /* Transmit Error Counter */
    uint32_t REC;  /* Receive Error Counter  */
    uint32_t LEC;  /* Last Error Code (ISO 11898-2 Table 20) */
    bool     bus_off;
    bool     error_passive;
} CAN_ErrorState_t;

typedef enum {
    CAN_BITRATE_1M   = 1000000UL,
    CAN_BITRATE_500K =  500000UL,
    CAN_BITRATE_250K =  250000UL,
    CAN_BITRATE_125K =  125000UL,
} CAN_BitRate_t;

extern void     HAL_CAN_GetErrorState(CAN_ErrorState_t *state);
extern bool     HAL_CAN_SetBitRate(CAN_BitRate_t rate);
extern void     HAL_CAN_RecoverFromBusOff(void);
extern uint32_t HAL_GetTick_ms(void);
extern void     LOG_Warning(const char *fmt, ...);

/* --- Adaptive EMI handler --- */

#define EMI_CHECK_INTERVAL_MS     100U   /* Check errors every 100 ms */
#define EMI_TEC_THRESHOLD          96U   /* TEC approaching error-passive */
#define EMI_REC_THRESHOLD          96U   /* REC approaching error-passive */
#define EMI_RECOVERY_DELAY_MS   10000U  /* Wait 10 s before rate increase */
#define EMI_RATE_HOLD_MS        30000U  /* Hold lower rate for 30 s */

typedef struct {
    CAN_BitRate_t current_rate;
    CAN_BitRate_t preferred_rate;
    uint32_t      last_check_ms;
    uint32_t      degraded_since_ms;
    uint32_t      consecutive_clean_intervals;
    bool          is_degraded;
} CAN_AdaptiveState_t;

static CAN_AdaptiveState_t g_adaptive = {
    .current_rate   = CAN_BITRATE_500K,
    .preferred_rate = CAN_BITRATE_500K,
};

/**
 * Reduce the CAN bit rate by one step.
 * Lower bit rates are more tolerant of signal degradation from EMI.
 */
static bool step_down_bitrate(CAN_AdaptiveState_t *state)
{
    CAN_BitRate_t next;
    switch (state->current_rate) {
        case CAN_BITRATE_1M:   next = CAN_BITRATE_500K; break;
        case CAN_BITRATE_500K: next = CAN_BITRATE_250K; break;
        case CAN_BITRATE_250K: next = CAN_BITRATE_125K; break;
        default: return false;  /* Already at minimum */
    }

    LOG_Warning("EMI detected: reducing CAN bit rate from %lu to %lu bps",
                (unsigned long)state->current_rate,
                (unsigned long)next);

    if (HAL_CAN_SetBitRate(next)) {
        state->current_rate = next;
        state->is_degraded  = true;
        state->degraded_since_ms = HAL_GetTick_ms();
        state->consecutive_clean_intervals = 0;
        return true;
    }
    return false;
}

/**
 * Attempt to restore preferred bit rate after a clean period.
 */
static void try_restore_bitrate(CAN_AdaptiveState_t *state)
{
    if (!state->is_degraded) return;

    uint32_t now = HAL_GetTick_ms();
    uint32_t held_ms = now - state->degraded_since_ms;

    if (held_ms < EMI_RATE_HOLD_MS) return;

    state->consecutive_clean_intervals++;

    /* Require 10 consecutive clean intervals before restoring */
    if (state->consecutive_clean_intervals < 10) return;

    if (state->current_rate < state->preferred_rate) {
        /* Step up one level */
        CAN_BitRate_t next;
        switch (state->current_rate) {
            case CAN_BITRATE_125K: next = CAN_BITRATE_250K; break;
            case CAN_BITRATE_250K: next = CAN_BITRATE_500K; break;
            case CAN_BITRATE_500K: next = CAN_BITRATE_1M;   break;
            default: next = state->preferred_rate; break;
        }
        if (next > state->preferred_rate) next = state->preferred_rate;

        if (HAL_CAN_SetBitRate(next)) {
            state->current_rate = next;
            state->degraded_since_ms = now;
            state->consecutive_clean_intervals = 0;

            if (state->current_rate == state->preferred_rate) {
                state->is_degraded = false;
                LOG_Warning("CAN bit rate restored to preferred %lu bps",
                            (unsigned long)state->preferred_rate);
            }
        }
    }
}

/**
 * Main EMI monitoring task. Call from a periodic task or timer ISR context.
 * Implements a state machine for adaptive bit rate control.
 */
void CAN_EMI_MonitorTask(void)
{
    uint32_t now = HAL_GetTick_ms();
    if ((now - g_adaptive.last_check_ms) < EMI_CHECK_INTERVAL_MS) return;
    g_adaptive.last_check_ms = now;

    CAN_ErrorState_t err;
    HAL_CAN_GetErrorState(&err);

    /* Handle bus-off: recover and reduce rate */
    if (err.bus_off) {
        LOG_Warning("CAN bus-off: recovering and reducing bit rate");
        HAL_CAN_RecoverFromBusOff();
        step_down_bitrate(&g_adaptive);
        return;
    }

    /* Check error counters */
    bool high_errors = (err.TEC > EMI_TEC_THRESHOLD) ||
                       (err.REC > EMI_REC_THRESHOLD) ||
                       err.error_passive;

    if (high_errors) {
        step_down_bitrate(&g_adaptive);
    } else {
        try_restore_bitrate(&g_adaptive);
    }
}
```

---

## 9. Rust Implementation Examples 

### 9.1 CMC Parameter Evaluator in Rust

```rust
//! cmc_evaluator.rs
//!
//! Rust library for evaluating common mode choke suitability for CAN bus.
//! Provides type-safe parameter checking and reporting.
//!
//! Run: cargo run

use std::fmt;

const PI: f64 = std::f64::consts::PI;

/// CAN bus operating configuration.
#[derive(Debug, Clone)]
pub struct CanConfig {
    /// Arbitration-phase bit rate in bits/second
    pub arb_bit_rate: f64,
    /// Data-phase bit rate in bits/second (same as arb_bit_rate for classic CAN)
    pub data_bit_rate: f64,
    /// Maximum cable length in metres
    pub cable_length_m: f64,
    /// Maximum transceiver current in amperes
    pub max_current_a: f64,
    /// Requires AEC-Q200 automotive qualification
    pub automotive: bool,
}

impl CanConfig {
    /// Return the estimated maximum signal bandwidth in Hz.
    /// Uses 5× the data-phase bit rate as a conservative estimate.
    pub fn signal_bandwidth_hz(&self) -> f64 {
        self.data_bit_rate * 5.0
    }
}

/// Common mode choke specification.
#[derive(Debug, Clone)]
pub struct CmcSpec {
    pub part_number: String,
    pub manufacturer: String,
    /// Common mode inductance at 100 kHz in µH
    pub l_cm_uh: f64,
    /// Differential leakage inductance in nH
    pub l_diff_nh: f64,
    /// DC resistance per winding in Ω
    pub dcr_ohm: f64,
    /// Rated current in amperes
    pub i_rated_a: f64,
    /// Self-resonant frequency in MHz
    pub srf_mhz: f64,
    /// Winding inductance mismatch in percent
    pub winding_balance_pct: f64,
    /// AEC-Q200 automotive qualification
    pub aec_q200: bool,
}

/// A single evaluation finding.
#[derive(Debug, Clone)]
pub enum Finding {
    Pass(String),
    Warning(String),
    Failure(String),
}

impl fmt::Display for Finding {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Finding::Pass(msg)    => write!(f, "  [PASS] {}", msg),
            Finding::Warning(msg) => write!(f, "  [WARN] {}", msg),
            Finding::Failure(msg) => write!(f, "  [FAIL] {}", msg),
        }
    }
}

/// Result of evaluating a CMC against a CAN configuration.
#[derive(Debug)]
pub struct EvalResult {
    pub score: f64,
    pub overall_pass: bool,
    pub findings: Vec<Finding>,
}

impl EvalResult {
    fn new() -> Self {
        EvalResult {
            score: 100.0,
            overall_pass: true,
            findings: Vec::new(),
        }
    }

    fn add_pass(&mut self, msg: impl Into<String>) {
        self.findings.push(Finding::Pass(msg.into()));
    }

    fn add_warning(&mut self, msg: impl Into<String>, penalty: f64) {
        self.score -= penalty;
        self.findings.push(Finding::Warning(msg.into()));
    }

    fn add_failure(&mut self, msg: impl Into<String>, penalty: f64) {
        self.score -= penalty;
        self.overall_pass = false;
        self.findings.push(Finding::Failure(msg.into()));
    }
}

/// Calculate the -3 dB common mode cutoff frequency.
///
/// # Arguments
/// * `l_cm_uh`   - Common mode inductance in µH
/// * `z_src_ohm` - Common mode source impedance
/// * `z_load_ohm`- Common mode load impedance
pub fn cm_cutoff_hz(l_cm_uh: f64, z_src_ohm: f64, z_load_ohm: f64) -> f64 {
    let l = l_cm_uh * 1e-6;
    let z_total = z_src_ohm + z_load_ohm;
    z_total / (2.0 * PI * 2.0 * l)
}

/// Calculate differential insertion loss at a given frequency.
///
/// # Arguments
/// * `l_diff_nh`  - Differential leakage inductance in nH
/// * `freq_hz`    - Frequency in Hz
/// * `z_diff_ohm` - Differential impedance of CAN line (typically 120 Ω)
///
/// # Returns
/// Insertion loss in dB (negative → attenuation)
pub fn diff_insertion_loss_db(l_diff_nh: f64, freq_hz: f64, z_diff_ohm: f64) -> f64 {
    let x_l = 2.0 * PI * freq_hz * l_diff_nh * 1e-9;
    20.0 * (z_diff_ohm / (z_diff_ohm.hypot(x_l))).log10()
}

/// Evaluate a CMC candidate against a CAN bus configuration.
pub fn evaluate(config: &CanConfig, cmc: &CmcSpec) -> EvalResult {
    let mut result = EvalResult::new();

    // --- Current rating (2× safety factor) ---
    let required_a = config.max_current_a * 2.0;
    if cmc.i_rated_a >= required_a {
        result.add_pass(format!(
            "Current: {:.0} mA rated ≥ {:.0} mA required",
            cmc.i_rated_a * 1000.0,
            required_a * 1000.0
        ));
    } else {
        result.add_failure(
            format!(
                "Current: {:.0} mA rated < {:.0} mA required",
                cmc.i_rated_a * 1000.0,
                required_a * 1000.0
            ),
            25.0,
        );
    }

    // --- Self-resonant frequency ---
    let max_noise_mhz = 200.0_f64;
    let min_srf_mhz = max_noise_mhz * 3.0;
    if cmc.srf_mhz >= min_srf_mhz {
        result.add_pass(format!(
            "SRF: {:.0} MHz ≥ {:.0} MHz minimum",
            cmc.srf_mhz, min_srf_mhz
        ));
    } else {
        result.add_warning(
            format!(
                "SRF: {:.0} MHz < {:.0} MHz recommended",
                cmc.srf_mhz, min_srf_mhz
            ),
            10.0,
        );
    }

    // --- Differential insertion loss ---
    let bw = config.signal_bandwidth_hz();
    let loss_db = diff_insertion_loss_db(cmc.l_diff_nh, bw, 120.0);
    if loss_db >= -1.0 {
        result.add_pass(format!(
            "Differential loss: {:.2} dB at {:.1} MHz signal BW",
            loss_db,
            bw / 1e6
        ));
    } else if loss_db >= -3.0 {
        result.add_warning(
            format!(
                "Differential loss: {:.2} dB — marginal, verify eye diagram",
                loss_db
            ),
            10.0,
        );
    } else {
        result.add_failure(
            format!(
                "Differential loss: {:.2} dB exceeds -3 dB limit",
                loss_db
            ),
            30.0,
        );
    }

    // --- Common mode filter effectiveness ---
    let cutoff = cm_cutoff_hz(cmc.l_cm_uh, 50.0, 50.0);
    let target = config.arb_bit_rate * 0.01;
    if cutoff <= target {
        result.add_pass(format!(
            "CM cutoff: {:.1} kHz ≤ {:.1} kHz target",
            cutoff / 1e3,
            target / 1e3
        ));
    } else {
        result.add_warning(
            format!(
                "CM cutoff: {:.1} kHz > {:.1} kHz — consider higher L_cm",
                cutoff / 1e3,
                target / 1e3
            ),
            15.0,
        );
    }

    // --- Winding balance ---
    if cmc.winding_balance_pct <= 2.0 {
        result.add_pass(format!(
            "Balance: {:.1}% mismatch — excellent",
            cmc.winding_balance_pct
        ));
    } else if cmc.winding_balance_pct <= 5.0 {
        result.add_warning(
            format!(
                "Balance: {:.1}% mismatch — acceptable but verify LCL spec",
                cmc.winding_balance_pct
            ),
            5.0,
        );
    } else {
        result.add_warning(
            format!(
                "Balance: {:.1}% mismatch — may create common mode from differential",
                cmc.winding_balance_pct
            ),
            15.0,
        );
    }

    // --- Automotive qualification ---
    if config.automotive && !cmc.aec_q200 {
        result.add_failure("AEC-Q200 required for automotive — part not qualified", 20.0);
    } else if config.automotive && cmc.aec_q200 {
        result.add_pass("AEC-Q200 qualified — automotive approved".to_string());
    }

    // Clamp score to [0, 100]
    result.score = result.score.clamp(0.0, 100.0);
    result
}

fn print_report(cmc: &CmcSpec, result: &EvalResult) {
    println!("\n──────────────────────────────────────────────────────");
    println!("  {} {} — Score: {:.1}/100 [{}]",
        cmc.manufacturer,
        cmc.part_number,
        result.score,
        if result.overall_pass { "PASS" } else { "FAIL" }
    );
    println!("  L_cm: {} µH | L_diff: {} nH | DCR: {} Ω | SRF: {} MHz",
        cmc.l_cm_uh, cmc.l_diff_nh, cmc.dcr_ohm, cmc.srf_mhz);
    println!("──────────────────────────────────────────────────────");
    for f in &result.findings {
        println!("{}", f);
    }
}

fn main() {
    let config = CanConfig {
        arb_bit_rate:  500_000.0,
        data_bit_rate: 2_000_000.0,
        cable_length_m: 10.0,
        max_current_a: 0.08,
        automotive: true,
    };

    let candidates = vec![
        CmcSpec {
            part_number: "744232090".into(),
            manufacturer: "Würth Elektronik".into(),
            l_cm_uh: 90.0,
            l_diff_nh: 150.0,
            dcr_ohm: 0.35,
            i_rated_a: 0.600,
            srf_mhz: 500.0,
            winding_balance_pct: 1.5,
            aec_q200: true,
        },
        CmcSpec {
            part_number: "DLW5BTM102SQ2L".into(),
            manufacturer: "Murata".into(),
            l_cm_uh: 1000.0,
            l_diff_nh: 300.0,
            dcr_ohm: 0.50,
            i_rated_a: 0.300,
            srf_mhz: 200.0,
            winding_balance_pct: 2.0,
            aec_q200: true,
        },
        CmcSpec {
            part_number: "ACM2012-900-2P-T".into(),
            manufacturer: "TDK".into(),
            l_cm_uh: 900.0,
            l_diff_nh: 2000.0,
            dcr_ohm: 1.0,
            i_rated_a: 0.100,
            srf_mhz: 100.0,
            winding_balance_pct: 3.0,
            aec_q200: false,
        },
    ];

    println!("=== CAN Bus CMC Evaluator (Rust) ===");
    println!("Config: CAN FD {:.0} kbit/s arb / {:.1} Mbit/s data | Automotive: {}",
        config.arb_bit_rate / 1e3,
        config.data_bit_rate / 1e6,
        config.automotive
    );

    let mut scored: Vec<(&CmcSpec, EvalResult)> = candidates
        .iter()
        .map(|c| (c, evaluate(&config, c)))
        .collect();

    scored.sort_by(|a, b| b.1.score.partial_cmp(&a.1.score).unwrap());

    for (cmc, result) in &scored {
        print_report(cmc, result);
    }

    let (best_cmc, best_result) = &scored[0];
    println!("\n=== Top Recommendation ===");
    println!("  {} {} — Score {:.1}",
        best_cmc.manufacturer,
        best_cmc.part_number,
        best_result.score
    );
}
```

### 9.2 CAN Error Statistics with `socketcan` Crate (Rust/Linux)

```rust
//! can_emi_stats.rs
//!
//! Async CAN bus EMI error monitoring using the `socketcan` crate.
//!
//! Cargo.toml dependencies:
//!   socketcan = "3"
//!   tokio = { version = "1", features = ["full"] }
//!   anyhow = "1"

use socketcan::{CANFrame, CANSocket, CANFilter, ShouldRetry};
use std::time::{Duration, Instant};
use std::io;

/// Accumulated error statistics
#[derive(Default, Debug)]
struct ErrorStats {
    total_frames:    u64,
    error_frames:    u64,
    bit_errors:      u64,
    form_errors:     u64,
    stuff_errors:    u64,
    bus_off_events:  u64,
    error_passive:   u64,
    start:           Option<Instant>,
}

impl ErrorStats {
    fn record_start(&mut self) {
        self.start = Some(Instant::now());
    }

    fn elapsed_secs(&self) -> f64 {
        self.start
            .map(|s| s.elapsed().as_secs_f64())
            .unwrap_or(1.0)
    }

    fn error_rate_pct(&self) -> f64 {
        let total = self.total_frames + self.error_frames;
        if total == 0 {
            return 0.0;
        }
        self.error_frames as f64 / total as f64 * 100.0
    }

    fn print_report(&self) {
        println!("\n=== CAN EMI Report ===");
        println!("Runtime:        {:.1} s", self.elapsed_secs());
        println!("Total frames:   {}", self.total_frames);
        println!("Error frames:   {} ({:.3}%)", self.error_frames, self.error_rate_pct());
        println!("Bit errors:     {}", self.bit_errors);
        println!("Form errors:    {}", self.form_errors);
        println!("Stuff errors:   {}", self.stuff_errors);
        println!("Bus-off events: {}", self.bus_off_events);
        println!("Err-passive:    {}", self.error_passive);

        println!("\n--- Diagnosis ---");
        let er = self.error_rate_pct();
        if er > 1.0 {
            println!("[HIGH] Error rate {:.2}% — EMI likely present", er);
            if self.bit_errors > self.form_errors * 2 {
                println!("  → Bit errors dominant: external common mode interference");
                println!("  → Review CMC selection: increase L_cm or improve PCB layout");
            }
            if self.bus_off_events > 0 {
                println!("  → Bus-off events: severe EMI or wiring fault — check CMC");
            }
        } else if er < 0.01 {
            println!("[OK] Error rate nominal — CMC appears adequate");
        } else {
            println!("[MARGINAL] Monitor under full load / worst-case EMI conditions");
        }
    }
}

// CAN error frame bit masks (from Linux kernel linux/can/error.h)
const CAN_ERR_FLAG:     u32 = 0x20000000;
const CAN_ERR_BUSOFF:   u32 = 0x00000040;
const CAN_ERR_CRTL:     u32 = 0x00000004;
const CAN_ERR_PROT:     u32 = 0x00000008;
const CAN_ERR_PROT_BIT:   u8 = 0x01;
const CAN_ERR_PROT_FORM:  u8 = 0x02;
const CAN_ERR_PROT_STUFF: u8 = 0x04;
const CAN_ERR_CRTL_RX_PASSIVE: u8 = 0x04;
const CAN_ERR_CRTL_TX_PASSIVE: u8 = 0x08;

fn process_frame(frame: &CANFrame, stats: &mut ErrorStats) {
    let id = frame.id();

    if id & CAN_ERR_FLAG != 0 {
        stats.error_frames += 1;
        let data = frame.data();

        if id & CAN_ERR_BUSOFF != 0 {
            stats.bus_off_events += 1;
            eprintln!("[BUS-OFF] Bus-off event! Verify CMC and termination.");
        }

        if id & CAN_ERR_CRTL != 0 {
            if data.len() > 1 {
                let ctrl = data[1];
                if ctrl & (CAN_ERR_CRTL_RX_PASSIVE | CAN_ERR_CRTL_TX_PASSIVE) != 0 {
                    stats.error_passive += 1;
                }
            }
        }

        if id & CAN_ERR_PROT != 0 && data.len() > 2 {
            let prot = data[2];
            if prot & CAN_ERR_PROT_BIT   != 0 { stats.bit_errors   += 1; }
            if prot & CAN_ERR_PROT_FORM  != 0 { stats.form_errors  += 1; }
            if prot & CAN_ERR_PROT_STUFF != 0 { stats.stuff_errors += 1; }
        }
    } else {
        stats.total_frames += 1;
    }
}

fn main() -> anyhow::Result<()> {
    let iface = std::env::args().nth(1)
        .unwrap_or_else(|| "can0".to_string());

    let sock = CANSocket::open(&iface)?;

    // Enable error frame reception (ERR mask = all errors)
    sock.set_error_filter_accept_all()?;
    sock.set_read_timeout(Duration::from_secs(1))?;

    println!("Monitoring {} — press Ctrl+C to stop.", iface);

    let mut stats = ErrorStats::default();
    stats.record_start();

    loop {
        match sock.read_frame() {
            Ok(frame) => process_frame(&frame, &mut stats),
            Err(e) if e.kind() == io::ErrorKind::WouldBlock => continue,
            Err(e) if e.kind() == io::ErrorKind::Interrupted => break,
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }

    stats.print_report();
    Ok(())
}
```

---

## 10. Testing and Validation 

After installing a CMC, validate its effectiveness with these methods:

### 10.1 Common Mode Rejection Measurement

Connect a spectrum analyser or EMI receiver to a line impedance stabilisation network (LISN) on the CAN cable. Measure radiated and conducted emissions with and without the CMC:

- Target: ≥ 20 dB reduction at frequencies between 1 MHz and 100 MHz
- Compare against CISPR 25 Class 5 or applicable limit

### 10.2 CAN Bus Eye Diagram

Use an oscilloscope with CAN protocol decode at maximum bit rate:
- Differential eye must be fully open
- Rise/fall times should be within ISO 11898-2 spec
- No ringing or pre-shoot exceeding 10% of signal amplitude

### 10.3 Error Frame Rate Logging

Use the C or Rust monitoring code above to log error rates during:
- Normal operation (error rate should be < 0.01%)
- Worst-case EMI injection (per ISO 11452-4)
- Temperature extremes (−40°C and +125°C for automotive)

### 10.4 Immunity Testing

Perform bulk current injection (BCI) per ISO 11452-4:
- Apply 200 mA injection current across 1–400 MHz
- CAN bus must remain operational throughout
- No bus-off events permitted

---

## 11. Summary 

Common mode chokes are a critical but often under-specified component in CAN bus designs. The key principles for correct selection are:

**Electrical Parameters to Specify:**
- Common mode inductance (L_cm): 90 µH – 10 mH depending on bit rate and cable length
- Differential leakage inductance (L_diff): < 200 nH for CAN FD; < 2 µH for classic CAN
- Self-resonant frequency (SRF): at least 3× the highest noise frequency of concern
- Winding balance: < 2% mismatch for lowest differential-to-common mode conversion
- Rated current: ≥ 2× maximum expected current

**Application Rules:**
- Classic CAN (≤ 1 Mbit/s): prioritise high L_cm (1–10 mH)
- CAN FD (≥ 2 Mbit/s): balance L_cm against low L_diff to preserve signal bandwidth
- Automotive: always require AEC-Q200 qualification
- Place CMC at the connector, not near the transceiver IC

**Software's Role:**
While CMC selection is a hardware activity, software monitoring of error counters (TEC, REC, LEC) provides direct observability into whether the installed CMC provides adequate filtering in the deployed environment. Adaptive bit rate fallback can improve robustness in worst-case EMI conditions.

**A well-selected CMC, combined with proper PCB layout and Y-capacitors to chassis ground, can provide 20–40 dB of common mode noise suppression — the difference between a system that passes CISPR 25 Class 5 and one that fails radiated emissions testing by a wide margin.**

---

*Document: CAN Bus Topic 95 — Common Mode Choke Selection*
*Covers: EMI theory, choke physics, parameter selection, C/C++ and Rust code examples, testing methodology*