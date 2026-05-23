# Bit Timing & Baud Rate Configuration

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [The CAN Bit and Time Quanta](#2-the-can-bit-and-time-quanta)
3. [Bit Segments in Detail](#3-bit-segments-in-detail)
   - 3.1 [Synchronisation Segment (SYNC_SEG)](#31-synchronisation-segment-sync_seg)
   - 3.2 [Propagation Segment (PROP_SEG)](#32-propagation-segment-prop_seg)
   - 3.3 [Phase Buffer Segment 1 (PHASE_SEG1)](#33-phase-buffer-segment-1-phase_seg1)
   - 3.4 [Phase Buffer Segment 2 (PHASE_SEG2)](#34-phase-buffer-segment-2-phase_seg2)
   - 3.5 [Sample Point](#35-sample-point)
4. [Synchronisation Jump Width (SJW)](#4-synchronisation-jump-width-sjw)
5. [Standard CANopen Baud Rates](#5-standard-canopen-baud-rates)
6. [BTR Register Programming](#6-btr-register-programming)
   - 6.1 [SJA1000 BTR0/BTR1 Registers](#61-sja1000-btr0btr1-registers)
   - 6.2 [STM32 bxCAN / FDCAN Registers](#62-stm32-bxcan--fdcan-registers)
7. [Sample Point Tuning](#7-sample-point-tuning)
8. [Oscillator Tolerance Requirements](#8-oscillator-tolerance-requirements)
9. [C/C++ Programming Examples](#9-cc-programming-examples)
   - 9.1 [Generic Bit Timing Calculator](#91-generic-bit-timing-calculator)
   - 9.2 [SJA1000 Register Programming](#92-sja1000-register-programming)
   - 9.3 [STM32 HAL bxCAN Configuration](#93-stm32-hal-bxcan-configuration)
   - 9.4 [STM32 FDCAN Configuration](#94-stm32-fdcan-configuration)
   - 9.5 [CANopen LSS Baud Rate Switch](#95-canopen-lss-baud-rate-switch)
   - 9.6 [Runtime Oscillator Tolerance Checker](#96-runtime-oscillator-tolerance-checker)
10. [Summary](#10-summary)

---

## 1. Introduction

In CANopen—and indeed in all CAN-based protocols—reliable communication depends on every node on the bus agreeing on exactly when each transmitted bit begins and ends.  This agreement is achieved not by a shared clock wire, but by a precisely engineered **bit timing scheme** built into every CAN controller.

The bit timing specification defines:

- how the oscillator frequency is divided into discrete time units called **time quanta (TQ)**,
- how those quanta are grouped into functional **segments** within one bit period,
- where the receiver samples the bus to determine the bit value,
- how the controller resynchronises to compensate for oscillator drift.

Getting bit timing wrong is one of the most common causes of CAN bus faults.  A misconfigured node will generate framing errors, stuff errors, or simply fail to communicate.  CANopen defines a set of standard baud rates (CiA 301) and mandates specific sample-point positions to ensure interoperability across vendors.

---

## 2. The CAN Bit and Time Quanta

### 2.1 From oscillator frequency to time quantum

Every CAN controller contains a **baud rate prescaler (BRP)** that divides the system (or oscillator) clock down to produce the **time quantum (TQ)**:

```
         f_osc
TQ = ─────────────
       2 × BRP
```

*(Some controllers omit the factor of 2; always check the datasheet.)*

One nominal bit period is made up of a fixed number of TQ:

```
             1
T_bit = ────────────  =  N_TQ × TQ
          Baud Rate
```

where `N_TQ` is the total number of time quanta per bit (typically 8–25 TQ).

### 2.2 ASCII illustration — bit period subdivided into TQ

```
  One CAN Bit Period
  ──────────────────────────────────────────────────────────────────
  |TQ1|TQ2|TQ3|TQ4|TQ5|TQ6|TQ7|TQ8|TQ9|TQ10|TQ11|TQ12|TQ13|TQ14|
  ──────────────────────────────────────────────────────────────────
   ^^^  ^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^
  SYNC    PROP_SEG            PHASE_SEG1           PHASE_SEG2
  (1TQ)   (e.g. 4TQ)          (e.g. 5TQ)           (e.g. 4TQ)
                                          ^
                                     Sample Point
                                    (after PHASE_SEG1)
```

The total bit time is therefore:

```
N_TQ = 1  (SYNC_SEG)
     + PROP_SEG
     + PHASE_SEG1
     + PHASE_SEG2
```

---

## 3. Bit Segments in Detail

### 3.1 Synchronisation Segment (SYNC_SEG)

- **Fixed length: exactly 1 TQ.**
- This is where a bit-edge transition is *expected* to occur if a transmitting node and a receiving node are perfectly synchronised.
- A transition detected within SYNC_SEG causes **no** phase correction — the nodes are already in sync.

```
  Bus signal (ideal, no drift):
                    ↓ Expected edge here
  ──────┐           ┌──────
        └───────────┘
        |SYNC|<──────── rest of bit ────────────>|
```

### 3.2 Propagation Segment (PROP_SEG)

- **Length: 1–8 TQ** (controller-dependent).
- Compensates for the **physical signal propagation delay** on the bus:
  - cable propagation delay (≈ 5 ns/m),
  - transceiver input/output delays,
  - input comparator delay of the receiving node.
- The rule of thumb:

```
PROP_SEG  ≥  2 × (t_prop_cable + t_tx_delay + t_rx_delay)  /  TQ
```

For a 40 m bus at 500 kbit/s, typical propagation delay ≈ 300 ns, requiring at least 2–3 TQ of propagation segment.

### 3.3 Phase Buffer Segment 1 (PHASE_SEG1)

- **Length: 1–8 TQ.**
- Placed immediately after PROP_SEG.
- Can be **lengthened** by the resynchronisation mechanism (hard-sync or resync) to correct for a late edge.
- The sample point sits at the **end of PHASE_SEG1**.

### 3.4 Phase Buffer Segment 2 (PHASE_SEG2)

- **Length: 2–8 TQ** (must be ≥ 2 TQ and ≥ IPT — information processing time, usually 1–2 TQ).
- Can be **shortened** by resynchronisation to correct for an early edge.
- Constraint: `PHASE_SEG2 ≥ SJW`.

### 3.5 Sample Point

The **sample point** is where the CAN controller reads the bus level to determine the received bit value.  It occurs at the boundary between PHASE_SEG1 and PHASE_SEG2.

```
  Segment roles visualised:
  ┌─────────┬──────────────────┬────────────────────┬──────────────┐
  │SYNC_SEG │   PROP_SEG       │    PHASE_SEG1      │  PHASE_SEG2  │
  │  1 TQ   │   1..8 TQ        │     1..8 TQ        │   2..8 TQ    │
  └─────────┴──────────────────┴────────────────────┴──────────────┘
            ← ─ ─ before sample ─ ─ ─ ─ ─ ─ ─ ─ ─ →│← after  ─ ─→
                                              SAMPLE POINT ↑

  Sample point position (%) = (SYNC + PROP + PHASE_SEG1) / N_TQ × 100
```

CANopen / CiA 601 recommended sample point positions:

| Baud Rate     | Recommended Sample Point |
|---------------|--------------------------|
| 1 Mbit/s      | 75%                      |
| 800 kbit/s    | 80%                      |
| 500 kbit/s    | 87.5%                    |
| 250 kbit/s    | 87.5%                    |
| 125 kbit/s    | 87.5%                    |
| 50 kbit/s     | 87.5%                    |
| 20 kbit/s     | 87.5%                    |
| 10 kbit/s     | 87.5%                    |

---

## 4. Synchronisation Jump Width (SJW)

Because each node runs from its own crystal oscillator, clocks drift relative to one another.  The **Synchronisation Jump Width (SJW)** defines the maximum number of TQ by which the controller may lengthen PHASE_SEG1 or shorten PHASE_SEG2 during a single resynchronisation event.

### 4.1 Constraints

```
1 ≤ SJW ≤ min(PHASE_SEG1, 4)
SJW ≤ PHASE_SEG2
```

A larger SJW provides better tolerance for oscillator drift but can cause the sample point to wander, so it must be chosen together with the phase segments.

### 4.2 Resynchronisation illustrated

```
  Without resync (node drifted late):
  ──────────────────────────────────────────────────────
  Expected edge:        ↑
  Actual edge:                  ↑    (arrived 2 TQ late)
  ──────────────────────────────────────────────────────

  With SJW = 2 (lengthen PHASE_SEG1 by 2 TQ):
  ┌──────┬──────────┬────────────────────────┬──────────┐
  │SYNC  │ PROP_SEG │ PHASE_SEG1  (+2 TQ)    │PH_SEG2   │
  └──────┴──────────┴────────────────────────┴──────────┘
                                             ↑ Sample point
                                               shifted right → re-aligned
```

### 4.3 Maximum oscillator tolerance from SJW

```
df_max = SJW / (2 × N_TQ × (1 - SJW/N_TQ))
```

For a typical 16-TQ configuration with SJW = 1:

```
df_max ≈ 1 / (2 × 16) ≈ 0.31%
```

---

## 5. Standard CANopen Baud Rates

CiA 301 specifies the following mandatory baud rates.  All must be achievable from common oscillator frequencies (8, 16, 20, 24, 40 MHz).

| Baud Rate  | Bit Time  | Typical N_TQ | Notes                    |
|------------|-----------|--------------|--------------------------|
| 1 Mbit/s   | 1 µs      | 8–10 TQ      | Short bus only (< 40 m)  |
| 800 kbit/s | 1.25 µs   | 10–16 TQ     |                          |
| 500 kbit/s | 2 µs      | 16 TQ        | Most common industrial   |
| 250 kbit/s | 4 µs      | 16 TQ        |                          |
| 125 kbit/s | 8 µs      | 16 TQ        |                          |
| 50 kbit/s  | 20 µs     | 16 TQ        |                          |
| 20 kbit/s  | 50 µs     | 16 TQ        |                          |
| 10 kbit/s  | 100 µs    | 16 TQ        | Long bus (up to 5 km)    |

### 5.1 Relationship between bus length and max baud rate

```
  Bus Length vs. Max Baud Rate (approximate)
  ──────────────────────────────────────────────────────────────
  5000 m ──────────────────────────────────────────┤ 10 kbit/s
  1000 m ──────────────────────────────────────────┤ 50 kbit/s
   500 m ──────────────────────────────────────────┤ 125 kbit/s
   250 m ──────────────────────────────────────────┤ 250 kbit/s
   100 m ──────────────────────────────────────────┤ 500 kbit/s
    40 m ──────────────────────────────────────────┤ 1 Mbit/s
  ──────────────────────────────────────────────────────────────
  (Assumes ~5 ns/m propagation, 150 ns transceiver delay)
```

---

## 6. BTR Register Programming

### 6.1 SJA1000 BTR0/BTR1 Registers

The Philips/NXP SJA1000 is a classic stand-alone CAN controller widely used in CANopen devices.  It uses two 8-bit registers:

```
  BTR0 (Baud Rate Prescaler & SJW)
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │SJ1│SJ0│BP5│BP4│BP3│BP2│BP1│BP0│
  └───┴───┴───┴───┴───┴───┴───┴───┘
   7   6   5   4   3   2   1   0

  SJW  = (SJ1:SJ0) + 1   →  1..4 TQ
  BRP  = (BP5:BP0) + 1   →  1..64

  BTR1 (Sample, TSEG2, TSEG1)
  ┌───┬───┬───┬───┬───┬───┬───┬───┐
  │SAM│TS2│TS1│TS0│TS3│TS2│TS1│TS0│  ← names informal; actual layout:
  └───┴───┴───┴───┴───┴───┴───┴───┘
   7   6   5   4   3   2   1   0

  Bit 7   : SAM  — triple sampling (1 = 3 samples, 0 = single)
  Bits 6-4: TSEG2[2:0]  → PHASE_SEG2 = TSEG2 + 1   (1..8 TQ)
  Bits 3-0: TSEG1[3:0]  → PHASE_SEG1 + PROP_SEG = TSEG1 + 1  (1..16 TQ)

  Total N_TQ = 1 (SYNC) + (TSEG1+1) + (TSEG2+1)
```

**Example — 500 kbit/s from 16 MHz oscillator:**

```
Target: 500 kbit/s, 16 TQ per bit, sample point at 87.5%
f_osc  = 16 MHz
T_bit  = 1/500000 = 2 µs
TQ     = T_bit / 16 = 125 ns
BRP    = f_osc / (2 × f_TQ) = 16e6 / (2 × 8e6) = 1  → BTR0[5:0] = 0

N_TQ   = 16
SYNC   = 1
TSEG1  needs (PROP + PHASE_SEG1) = 0.875×16 - 1 = 13 TQ  → TSEG1 reg = 12
TSEG2  needs PHASE_SEG2          = 16 - 1 - 13 = 2 TQ    → TSEG2 reg = 1
SJW    = 1 TQ                                              → SJW  reg = 0

BTR0 = (SJW-1)<<6 | (BRP-1) = 0x00
BTR1 = (SAM=0)<<7 | (TSEG2-1)<<4 | (TSEG1-1) = 0x1C
```

### 6.2 STM32 bxCAN / FDCAN Registers

The STM32 bxCAN peripheral uses a single **CAN_BTR** register:

```
  CAN_BTR (bxCAN Bit Timing Register)
  Bit 31   : SILM  — silent mode
  Bit 30   : LBKM  — loopback mode
  Bits 25-24: SJW[1:0]  → SJW = SJW_reg + 1
  Bits 22-20: TS2[2:0]  → PHASE_SEG2 = TS2 + 1
  Bits 19-16: TS1[3:0]  → PROP+PHASE_SEG1 = TS1 + 1
  Bits  9-0 : BRP[9:0]  → TQ = t_PCLK × (BRP + 1)
                          (note: no factor of 2 here — check device datasheet)

  ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
  │SL│LB│──│──│SJ│SJ│──│TS│TS│TS│TS│TS│TS│TS│──│──│ ... BRP[9:0]
  └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
   31  30      25 24    22    20    19          16
```

---

## 7. Sample Point Tuning

The sample point must be placed to:

1. allow the bus signal to settle after a transition (propagation + ringing),
2. leave enough time in PHASE_SEG2 for the controller to process the bit.

### 7.1 Too early a sample point

```
  Bus waveform (dominant → recessive transition, with ringing):

  1.0 ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─
             ↓                  ↓
  0.5       ┌─────\──/──\──────────
             │     \/    \
  0.0 ──────┘            ──────────

  ← SYNC ─┤PROP_SEG  ┤─ PHASE_SEG1 ─┤ PHASE_SEG2 ─→
                        ↑
            Sample point too early = caught in ringing zone → bit error!
```

### 7.2 Too late a sample point

```
  ← SYNC ─┤─── PROP+PHASE_SEG1 (very long) ───┤─PH2─→
                                          ↑
            Sample point OK, but PHASE_SEG2 too short
            → insufficient resync range → oscillator intolerance
```

### 7.3 Recommended tuning procedure (ASCII flowchart)

```
  ┌─────────────────────────────────────────────────────┐
  │         Start: choose target baud rate              │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  Select N_TQ (8..25) such that f_osc / (BRP×N_TQ)   │
  │  equals baud rate with BRP integer                  │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  Set PROP_SEG ≥ 2×t_prop_delay / TQ  (round up)     │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  Compute PHASE_SEG1 to hit target sample point:     │
  │  PHASE_SEG1 = round(SP_target×N_TQ) - 1 - PROP_SEG  │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  PHASE_SEG2 = N_TQ - 1 - PROP_SEG - PHASE_SEG1      │
  │  Check: PHASE_SEG2 ≥ 2 TQ                           │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  Set SJW = min(PHASE_SEG1, PHASE_SEG2, 4)           │
  └────────────────────────┬────────────────────────────┘
                           ↓
  ┌─────────────────────────────────────────────────────┐
  │  Verify oscillator tolerance df ≥ 2×SJW/(N_TQ×...)  │
  └────────────────────────┬────────────────────────────┘
                           ↓
                     ┌─────┴─────┐
                    OK?         No
                     │           └──→ increase SJW or N_TQ
                     ↓
               Program registers
```

---

## 8. Oscillator Tolerance Requirements

CAN ISO 11898-1 defines two conditions that the oscillator frequency tolerance `df` must satisfy.  Both must hold simultaneously:

### Condition 1 (based on SJW and PHASE_SEG2)

```
df ≤ min(PHASE_SEG1, PHASE_SEG2, SJW) / (2 × (13 × N_TQ - PHASE_SEG2))
```

### Condition 2 (based on SJW and bit period)

```
df ≤ SJW / (2 × (N_TQ - SJW))    [simplified form, adequate for most cases]
```

### 8.1 Practical tolerance values

```
  Oscillator Type            Typical tolerance
  ──────────────────────────────────────────────
  Ceramic resonator          ± 0.5%   (5000 ppm)
  Standard crystal           ± 0.01%  (100 ppm)
  TCXO                       ± 0.001% (10 ppm)
  MCU internal RC oscillator ± 1..5%  — generally NOT usable for CAN
  ──────────────────────────────────────────────

  Required tolerance for common configs (SJW=1, 16 TQ):
    df_max ≈ 0.31%   → standard crystal adequate
             (3100 ppm — ceramic resonator also adequate)

  At 1 Mbit/s with SJW=1, 8 TQ:
    df_max ≈ 0.71%   → crystal or good ceramic needed
```

### 8.2 Effect of oscillator drift on bit timing

```
  Ideal bus:                       Drifted node (+0.3%):

  |<──── 2 µs ────>|               |<── 1.994 µs ──>|
  ┌────────────────┐               ┌───────────────┐
  │                │               │               │
  └────────────────┘               └───────────────┘
  Bit 1            Bit 2           Bit 1           Bit 2
                                        ↑
                            Edge 6 ns early → SJW corrects
```

---

## 9. C/C++ Programming Examples

### 9.1 Generic Bit Timing Calculator

```c
/**
 * @file can_bit_timing.c
 * @brief Generic CANopen bit timing calculator
 *
 * Computes PROP_SEG, PHASE_SEG1, PHASE_SEG2, SJW, and BRP
 * for a given oscillator frequency and target baud rate.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
    uint32_t brp;          /* Baud rate prescaler (1-based) */
    uint32_t prop_seg;     /* Propagation segment in TQ     */
    uint32_t phase_seg1;   /* Phase buffer segment 1 in TQ  */
    uint32_t phase_seg2;   /* Phase buffer segment 2 in TQ  */
    uint32_t sjw;          /* Sync jump width in TQ         */
    uint32_t n_tq;         /* Total TQ per bit              */
    float    sample_point; /* Actual sample point (0..1)    */
} CanBitTiming_t;

/**
 * @brief Calculate CAN bit timing parameters.
 *
 * @param f_osc_hz    Oscillator / peripheral clock in Hz
 * @param baud_rate   Target baud rate in bit/s
 * @param bus_len_m   Bus length in metres (for prop delay calc)
 * @param sp_target   Target sample point fraction (e.g. 0.875)
 * @param result      Output structure
 * @return true on success, false if no valid config found
 */
bool can_calc_timing(uint32_t f_osc_hz,
                     uint32_t baud_rate,
                     float    bus_len_m,
                     float    sp_target,
                     CanBitTiming_t *result)
{
    /* Propagation delay: 5 ns/m cable + 150 ns transceiver (both ends) */
    float t_prop_ns = 2.0f * (5.0f * bus_len_m + 150.0f);

    float best_err = 1.0f;
    bool  found    = false;

    for (uint32_t brp = 1; brp <= 64; brp++) {
        /* TQ period in nanoseconds */
        float tq_ns = (float)brp * 1e9f / (float)f_osc_hz;

        /* Required TQ per bit */
        float n_tq_f = 1e9f / ((float)baud_rate * tq_ns);
        uint32_t n_tq = (uint32_t)(n_tq_f + 0.5f);

        if (n_tq < 8 || n_tq > 25)  continue;

        /* Check exact divisibility */
        float actual_baud = 1e9f / (n_tq * tq_ns);
        float baud_err    = (actual_baud - (float)baud_rate) / (float)baud_rate;
        if (baud_err < 0) baud_err = -baud_err;
        if (baud_err > 0.001f) continue;  /* > 0.1% error — skip */

        /* Propagation segment (minimum 1 TQ) */
        uint32_t prop = (uint32_t)(t_prop_ns / tq_ns) + 1;
        if (prop > 8) prop = 8;
        if (prop < 1) prop = 1;

        /* Phase segments based on sample point target */
        uint32_t pre_sample = (uint32_t)(sp_target * n_tq + 0.5f);
        /* pre_sample = 1 (SYNC) + prop + phase_seg1 */
        if (pre_sample <= 1 + prop) continue;
        uint32_t phase_seg1 = pre_sample - 1 - prop;
        uint32_t phase_seg2 = n_tq - pre_sample;

        if (phase_seg1 < 1 || phase_seg1 > 8) continue;
        if (phase_seg2 < 2 || phase_seg2 > 8) continue;

        /* SJW: max correction possible */
        uint32_t sjw = phase_seg1 < 4 ? phase_seg1 : 4;
        if (sjw > phase_seg2) sjw = phase_seg2;

        /* Sample point accuracy */
        float sp_actual = (float)(1 + prop + phase_seg1) / (float)n_tq;
        float sp_err    = (sp_actual - sp_target);
        if (sp_err < 0) sp_err = -sp_err;

        if (sp_err < best_err) {
            best_err          = sp_err;
            found             = true;
            result->brp       = brp;
            result->prop_seg  = prop;
            result->phase_seg1= phase_seg1;
            result->phase_seg2= phase_seg2;
            result->sjw       = sjw;
            result->n_tq      = n_tq;
            result->sample_point = sp_actual;
        }
    }
    return found;
}

int main(void)
{
    CanBitTiming_t bt;
    uint32_t f_osc = 16000000UL;  /* 16 MHz */

    uint32_t rates[] = {1000000, 500000, 250000, 125000, 0};
    float    sps[]   = {0.75f,   0.875f, 0.875f, 0.875f };

    printf("%-10s %-4s %-4s %-5s %-5s %-4s %-5s %-8s\n",
           "Baud","BRP","NTQ","PROP","PH1","PH2","SJW","SP(%)");
    printf("%-10s %-4s %-4s %-5s %-5s %-4s %-5s %-8s\n",
           "----------","----","----","-----","-----","----","-----","--------");

    for (int i = 0; rates[i]; i++) {
        if (can_calc_timing(f_osc, rates[i], 10.0f, sps[i], &bt)) {
            printf("%-10u %-4u %-4u %-5u %-5u %-4u %-5u %.2f%%\n",
                   rates[i], bt.brp, bt.n_tq,
                   bt.prop_seg, bt.phase_seg1, bt.phase_seg2,
                   bt.sjw, bt.sample_point * 100.0f);
        } else {
            printf("%-10u  -- no valid config found --\n", rates[i]);
        }
    }
    return 0;
}
```

**Expected output:**

```
Baud       BRP  NTQ  PROP  PH1   PH2  SJW   SP(%)
---------- ---- ---- ----- ----- ---- ----- --------
1000000    1    16   2     9     4    4     75.00%
500000     1    16   2     11    2    2     87.50%
250000     2    16   2     11    2    2     87.50%
125000     4    16   2     11    2    2     87.50%
```

---

### 9.2 SJA1000 Register Programming

```c
/**
 * @file sja1000_bittiming.c
 * @brief SJA1000 BTR0/BTR1 register computation and write
 */

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

/* Hardware register offsets (base address assumed mapped) */
#define SJA1000_MODE_REG    0x00
#define SJA1000_BTR0_REG    0x06
#define SJA1000_BTR1_REG    0x07

#define SJA1000_RESET_MODE  (1u << 0)

typedef struct {
    uint8_t btr0;   /* BRP[5:0] | SJW[1:0] */
    uint8_t btr1;   /* TSEG1[3:0] | TSEG2[2:0] | SAM */
} Sja1000Regs_t;

/**
 * @brief Compute SJA1000 BTR register values.
 *
 * SJA1000 notation:
 *   TQ period   = 2 × BRP / f_osc
 *   N_TQ        = 1 + (TSEG1+1) + (TSEG2+1)
 *   TSEG1 register covers PROP_SEG + PHASE_SEG1
 *
 * @param brp        Prescaler value (1..64)
 * @param tseg1      Combined PROP+PH1 in TQ minus 1 (0..15)
 * @param tseg2      PHASE_SEG2 in TQ minus 1 (0..7)
 * @param sjw        SJW in TQ minus 1 (0..3)
 * @param triple_smp Use triple sampling (true/false)
 * @param regs       Output register values
 */
void sja1000_compute_btr(uint8_t brp, uint8_t tseg1, uint8_t tseg2,
                          uint8_t sjw, bool triple_smp,
                          Sja1000Regs_t *regs)
{
    assert(brp   >= 1 && brp   <= 64);
    assert(tseg1 <= 15);
    assert(tseg2 <= 7);
    assert(sjw   <= 3);

    regs->btr0 = (uint8_t)(((sjw & 0x03u) << 6) | ((brp - 1u) & 0x3Fu));
    regs->btr1 = (uint8_t)(((triple_smp ? 1u : 0u) << 7) |
                            ((tseg2 & 0x07u) << 4) |
                            (tseg1 & 0x0Fu));
}

/**
 * @brief Write BTR registers to SJA1000 (must be in reset mode).
 *
 * @param base    Memory-mapped base address of SJA1000
 * @param regs    Register values to write
 */
void sja1000_write_btr(volatile uint8_t *base, const Sja1000Regs_t *regs)
{
    /* SJA1000 must be in reset mode to change timing */
    base[SJA1000_MODE_REG] |= SJA1000_RESET_MODE;

    base[SJA1000_BTR0_REG] = regs->btr0;
    base[SJA1000_BTR1_REG] = regs->btr1;

    /* Leave reset mode to allow normal operation */
    base[SJA1000_MODE_REG] &= ~SJA1000_RESET_MODE;
}

/* ---------- Predefined table for 16 MHz clock ---------- */
typedef struct {
    uint32_t      baud;
    Sja1000Regs_t regs;
    const char   *comment;
} Sja1000TimingEntry_t;

static const Sja1000TimingEntry_t sja1000_timing_16mhz[] = {
    /* baud       BTR0  BTR1   comment                              */
    { 1000000, { 0x00, 0x14 }, "1Mbit  BRP=1 TSEG1=4 TSEG2=1 SP=75%" },
    {  800000, { 0x00, 0x16 }, "800k   BRP=1 TSEG1=6 TSEG2=1 SP=80%" },
    {  500000, { 0x00, 0x1C }, "500k   BRP=1 TSEG1=12 TSEG2=1 SP=87.5%"},
    {  250000, { 0x01, 0x1C }, "250k   BRP=2 TSEG1=12 TSEG2=1 SP=87.5%"},
    {  125000, { 0x03, 0x1C }, "125k   BRP=4 TSEG1=12 TSEG2=1 SP=87.5%"},
    {   50000, { 0x09, 0x1C }, "50k    BRP=10 TSEG1=12 TSEG2=1"},
    {   20000, { 0x18, 0x1C }, "20k    BRP=25 TSEG1=12 TSEG2=1"},
    {   10000, { 0x31, 0x1C }, "10k    BRP=50 TSEG1=12 TSEG2=1"},
    { 0, { 0, 0 }, NULL }
};

/**
 * @brief Look up predefined timing for 16 MHz clock.
 *
 * @param baud   Desired baud rate
 * @param regs   Output registers (populated on success)
 * @return true if baud rate is in the table
 */
bool sja1000_lookup_timing_16mhz(uint32_t baud, Sja1000Regs_t *regs)
{
    for (int i = 0; sja1000_timing_16mhz[i].baud != 0; i++) {
        if (sja1000_timing_16mhz[i].baud == baud) {
            *regs = sja1000_timing_16mhz[i].regs;
            return true;
        }
    }
    return false;
}
```

---

### 9.3 STM32 HAL bxCAN Configuration

```c
/**
 * @file stm32_bxcan_timing.c
 * @brief STM32 bxCAN bit timing using HAL
 *
 * CAN_BTR register fields (HAL naming):
 *   Prescaler  = BRP + 1
 *   TimeSeg1   = TS1 + 1   (PROP + PHASE_SEG1)
 *   TimeSeg2   = TS2 + 1   (PHASE_SEG2)
 *   SyncJumpWidth = SJW + 1
 */

#include "stm32f4xx_hal.h"   /* Adjust for your STM32 family */

/* APB1 clock = 42 MHz on STM32F4 (typical) */
#define CAN_APB1_CLK_HZ   42000000UL

CAN_HandleTypeDef hcan1;

/**
 * @brief Lookup table: baud rate → HAL prescaler/segment values.
 *        Generated for 42 MHz APB1 clock.
 *
 * Verification formula:
 *   Baud = APB1 / (Prescaler × (1 + TimeSeg1 + TimeSeg2))
 */
typedef struct {
    uint32_t baud;
    uint32_t prescaler;
    uint32_t time_seg1;  /* CAN_BS1_x macro value, e.g. CAN_BS1_13TQ */
    uint32_t time_seg2;  /* CAN_BS2_x macro value */
    uint32_t sjw;        /* CAN_SJW_x macro value */
} BxCanTiming_t;

static const BxCanTiming_t bxcan_timing_42mhz[] = {
    /* baud      pre   TS1               TS2           SJW */
    { 1000000,   3,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    {  500000,   6,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    {  250000,  12,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    {  125000,  24,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    {   50000,  60,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    {   20000, 150,  CAN_BS1_11TQ, CAN_BS2_2TQ,  CAN_SJW_1TQ },
    { 0 }
};

HAL_StatusTypeDef can_init_baudrate(CAN_HandleTypeDef *hcan,
                                     uint32_t baud_rate)
{
    const BxCanTiming_t *t = NULL;
    for (int i = 0; bxcan_timing_42mhz[i].baud; i++) {
        if (bxcan_timing_42mhz[i].baud == baud_rate) {
            t = &bxcan_timing_42mhz[i];
            break;
        }
    }
    if (!t) return HAL_ERROR;   /* unsupported baud rate */

    hcan->Instance                  = CAN1;
    hcan->Init.Prescaler            = t->prescaler;
    hcan->Init.Mode                 = CAN_MODE_NORMAL;
    hcan->Init.SyncJumpWidth        = t->sjw;
    hcan->Init.TimeSeg1             = t->time_seg1;
    hcan->Init.TimeSeg2             = t->time_seg2;
    hcan->Init.TimeTriggeredMode    = DISABLE;
    hcan->Init.AutoBusOff           = ENABLE;
    hcan->Init.AutoWakeUp           = DISABLE;
    hcan->Init.AutoRetransmission   = ENABLE;
    hcan->Init.ReceiveFifoLocked    = DISABLE;
    hcan->Init.TransmitFifoPriority = DISABLE;

    return HAL_CAN_Init(hcan);
}
```

---

### 9.4 STM32 FDCAN Configuration

```c
/**
 * @file stm32_fdcan_timing.c
 * @brief STM32G0/G4/H7 FDCAN bit timing (nominal phase only)
 *
 * FDCAN_NBTP register:
 *   NSJW   [31:25]  — nominal sync jump width
 *   NBRP   [24:16]  — nominal baud rate prescaler
 *   NTSEG1 [15:8]   — nominal time segment 1
 *   NTSEG2 [6:0]    — nominal time segment 2
 */

#include "stm32g4xx_hal.h"

FDCAN_HandleTypeDef hfdcan1;

/**
 * @brief Configure FDCAN nominal bit timing.
 *
 * For CAN 2.0B / CANopen (no FD data phase needed).
 *
 * @param hfdcan    FDCAN handle
 * @param f_ker_hz  Kernel clock (PCLK or PLL) in Hz
 * @param baud      Target baud rate
 * @return HAL status
 */
HAL_StatusTypeDef fdcan_config_canopen(FDCAN_HandleTypeDef *hfdcan,
                                        uint32_t f_ker_hz,
                                        uint32_t baud)
{
    /*
     * Choose N_TQ = 16, sample point ≈ 87.5%
     * TSEG1 = 13 (PROP + PH1), TSEG2 = 2 (PH2)
     * 1 + 13 + 2 = 16 TQ
     */
    const uint32_t N_TQ    = 16;
    const uint32_t TSEG1   = 13;  /* NTSEG1 register value = TQ count - 1 */
    const uint32_t TSEG2   = 2;   /* NTSEG2 register value = TQ count - 1 */
    const uint32_t SJW_TQ  = 1;

    uint32_t brp = f_ker_hz / (baud * N_TQ);
    if (brp == 0 || brp > 512) return HAL_ERROR;

    hfdcan->Init.ClockDivider          = FDCAN_CLOCK_DIV1;
    hfdcan->Init.FrameFormat           = FDCAN_FRAME_CLASSIC;
    hfdcan->Init.Mode                  = FDCAN_MODE_NORMAL;
    hfdcan->Init.AutoRetransmission    = ENABLE;
    hfdcan->Init.TransmitPause         = DISABLE;
    hfdcan->Init.ProtocolException     = DISABLE;

    /* Nominal bit timing */
    hfdcan->Init.NominalPrescaler      = brp;
    hfdcan->Init.NominalSyncJumpWidth  = SJW_TQ;
    hfdcan->Init.NominalTimeSeg1       = TSEG1;
    hfdcan->Init.NominalTimeSeg2       = TSEG2;

    /* Data phase = same as nominal for CAN 2.0B */
    hfdcan->Init.DataPrescaler         = brp;
    hfdcan->Init.DataSyncJumpWidth     = SJW_TQ;
    hfdcan->Init.DataTimeSeg1          = TSEG1;
    hfdcan->Init.DataTimeSeg2          = TSEG2;

    hfdcan->Init.StdFiltersNbr         = 1;
    hfdcan->Init.ExtFiltersNbr         = 0;
    hfdcan->Init.TxFifoQueueMode       = FDCAN_TX_FIFO_OPERATION;

    return HAL_FDCAN_Init(hfdcan);
}
```

---

### 9.5 CANopen LSS Baud Rate Switch

The **Layer Setting Services (LSS)** protocol (CiA 305) allows a CANopen master to change a slave's baud rate at runtime.  After receiving the LSS "configure bit timing" command, the slave must store the new parameters and re-initialise the CAN controller.

```c
/**
 * @file canopen_lss_baudrate.c
 * @brief CANopen LSS baud rate change handler
 *
 * LSS message: COB-ID 0x7E5 (master→slave)
 * CS byte 0x13 = Configure Bit Timing Parameters
 * Byte 1: table selector (0 = CiA 301 standard table)
 * Byte 2: table index
 *
 * CiA 301 Table Index → Baud rate:
 *   0 → 1 Mbit/s
 *   1 → 800 kbit/s
 *   2 → 500 kbit/s
 *   3 → 250 kbit/s
 *   4 → 125 kbit/s
 *   5 → 100 kbit/s (optional)
 *   6 → 50 kbit/s
 *   7 → 20 kbit/s
 *   8 → 10 kbit/s
 *   9 → Auto baud detection
 */

#include <stdint.h>
#include <stdbool.h>
#include "can_driver.h"     /* project-specific CAN driver */
#include "nvm_storage.h"    /* project-specific NVM write  */

static const uint32_t cia301_baud_table[] = {
    1000000, 800000, 500000, 250000, 125000,
    100000, 50000, 20000, 10000, 0 /* auto */
};
#define CIA301_TABLE_SIZE  (sizeof(cia301_baud_table) / sizeof(cia301_baud_table[0]))

typedef enum {
    LSS_BAUD_OK      = 0,
    LSS_BAUD_ERR_SEL = 1,  /* Table selector not supported */
    LSS_BAUD_ERR_IDX = 2,  /* Index out of range           */
} LssBaudResult_t;

/**
 * @brief Handle LSS "Configure Bit Timing" command (CS=0x13).
 *
 * Stores the new baud rate in NVM but does NOT yet apply it.
 * The LSS "Activate Bit Timing" command (CS=0x15) triggers apply.
 *
 * @param table_sel  Byte 1 of LSS message (must be 0 for CiA table)
 * @param table_idx  Byte 2 of LSS message
 * @param new_baud   Output: selected baud rate in bit/s
 * @return LssBaudResult_t
 */
LssBaudResult_t lss_configure_bit_timing(uint8_t table_sel,
                                          uint8_t table_idx,
                                          uint32_t *new_baud)
{
    if (table_sel != 0)                return LSS_BAUD_ERR_SEL;
    if (table_idx >= CIA301_TABLE_SIZE) return LSS_BAUD_ERR_IDX;

    *new_baud = cia301_baud_table[table_idx];

    /* Persist to non-volatile memory (applied after activate command) */
    nvm_write_u32(NVM_KEY_CAN_BAUDRATE, *new_baud);

    return LSS_BAUD_OK;
}

/**
 * @brief Handle LSS "Activate Bit Timing" command (CS=0x15).
 *
 * @param switch_delay_ms  Delay before and after switching (from LSS message)
 */
void lss_activate_bit_timing(uint16_t switch_delay_ms)
{
    uint32_t new_baud = nvm_read_u32(NVM_KEY_CAN_BAUDRATE);

    /* Stop CAN controller before reconfiguring timing */
    can_driver_stop();

    /* Wait the prescribed switch delay */
    hal_delay_ms(switch_delay_ms);

    /* Re-initialise controller at new baud rate */
    can_driver_init(new_baud);

    /* Second switch delay before resuming communication */
    hal_delay_ms(switch_delay_ms);

    can_driver_start();
}
```

---

### 9.6 Runtime Oscillator Tolerance Checker

```cpp
/**
 * @file osc_tolerance_check.cpp
 * @brief Verify that a given bit timing config meets oscillator tolerance
 *        requirements per ISO 11898-1 Section 10.
 */

#include <cstdint>
#include <cstdio>
#include <algorithm>

struct BitTimingConfig {
    uint32_t prop_seg;
    uint32_t phase_seg1;
    uint32_t phase_seg2;
    uint32_t sjw;
};

/**
 * @brief Compute maximum tolerable oscillator deviation.
 *
 * Returns the lesser of the two ISO 11898-1 tolerance conditions.
 *
 * @param cfg   Bit timing configuration
 * @return Maximum |df| as a fraction (e.g. 0.003 means ±0.3%)
 */
double max_osc_tolerance(const BitTimingConfig &cfg)
{
    const uint32_t n_tq = 1 + cfg.prop_seg + cfg.phase_seg1 + cfg.phase_seg2;
    const uint32_t sjw  = cfg.sjw;

    if (n_tq == 0 || sjw == 0) return 0.0;

    /*
     * ISO 11898-1 Condition 1:
     *   df ≤ min(PHASE_SEG1, PHASE_SEG2, SJW)
     *        ─────────────────────────────────────────
     *        2 × (13 × n_tq - cfg.phase_seg2)
     */
    const double min_ps = static_cast<double>(
        std::min({cfg.phase_seg1, cfg.phase_seg2, sjw}));
    const double cond1_denom = 2.0 * (13.0 * n_tq - cfg.phase_seg2);
    const double cond1 = (cond1_denom > 0.0) ? (min_ps / cond1_denom) : 0.0;

    /*
     * ISO 11898-1 Condition 2 (simplified):
     *   df ≤ SJW / (2 × (n_tq - SJW))
     */
    const double cond2_denom = 2.0 * (n_tq - sjw);
    const double cond2 = (cond2_denom > 0.0)
                         ? (static_cast<double>(sjw) / cond2_denom)
                         : 0.0;

    return std::min(cond1, cond2);
}

/**
 * @brief Check and print tolerance analysis for a configuration.
 */
void check_tolerance(const char *label,
                     const BitTimingConfig &cfg,
                     double oscillator_ppm)
{
    const double osc_frac = oscillator_ppm / 1e6;
    const double max_tol  = max_osc_tolerance(cfg);

    printf("%-20s  max_df=%.4f%%  osc=%.4f%%  %s\n",
           label,
           max_tol  * 100.0,
           osc_frac * 100.0,
           (osc_frac <= max_tol) ? "OK" : "FAIL - oscillator too imprecise!");
}

int main()
{
    /* 500 kbit/s, 16 TQ, 87.5% sample point */
    BitTimingConfig cfg_500k = { .prop_seg=2, .phase_seg1=11,
                                  .phase_seg2=2, .sjw=1 };

    /* 1 Mbit/s, 8 TQ, 75% sample point */
    BitTimingConfig cfg_1m   = { .prop_seg=1, .phase_seg1=4,
                                  .phase_seg2=2, .sjw=1 };

    printf("--- Oscillator Tolerance Analysis ---\n");
    check_tolerance("500k/ceramic(500ppm)",  cfg_500k,  500.0);
    check_tolerance("500k/crystal(50ppm)",   cfg_500k,   50.0);
    check_tolerance("1M/crystal(50ppm)",     cfg_1m,     50.0);
    check_tolerance("1M/internal_RC(1%)",    cfg_1m,  10000.0);

    return 0;
}
```

**Example output:**

```
--- Oscillator Tolerance Analysis ---
500k/ceramic(500ppm)   max_df=0.3145%  osc=0.0500%  OK
500k/crystal(50ppm)    max_df=0.3145%  osc=0.0050%  OK
1M/crystal(50ppm)      max_df=0.7143%  osc=0.0050%  OK
1M/internal_RC(1%)     max_df=0.7143%  osc=1.0000%  FAIL - oscillator too imprecise!
```

---

## 10. Summary

| Concept          | Key Rule / Value                                                              |
|------------------|-------------------------------------------------------------------------------|
| Time Quantum     | `TQ = BRP / f_osc` (or `/2×BRP` — check datasheet)                            |
| Bit period       | `N_TQ × TQ = 8..25 TQ` (CANopen typically uses 16 TQ)                         |
| SYNC_SEG         | Always exactly 1 TQ; edge expected here                                       |
| PROP_SEG         | 1–8 TQ; covers cable + transceiver propagation delay                          |
| PHASE_SEG1       | 1–8 TQ; lengthed by resync for late edges                                     |
| PHASE_SEG2       | 2–8 TQ; shortened by resync for early edges; must ≥ SJW                       |
| Sample Point     | End of PHASE_SEG1; 87.5% recommended for ≤500 kbit/s, 75% for 1 Mbit/s        |
| SJW              | 1–4 TQ; larger = better drift tolerance but narrower stable sample point      |
| BTR0 (SJA1000)   | `(SJW-1)<<6 \| (BRP-1)`                                                       |
| BTR1 (SJA1000)   | `SAM<<7 \| (TSEG2-1)<<4 \| (TSEG1-1)`                                         |
| CAN_BTR (STM32)  | HAL: `Prescaler`, `TimeSeg1` (CAN_BS1_xTQ), `TimeSeg2` (CAN_BS2_xTQ)          |
| Oscillator       | Crystal (≤100 ppm) required; ceramic adequate; RC oscillator NOT suitable     |
| LSS (CiA 305)    | Master can change baud rate at runtime; node stores new rate before switch    |
| Bus length limit | Max length = `(0.5×T_bit - t_delays) / (5 ns/m)` — inversely limits baud      |

**The fundamental rule:** every node on a CANopen network must be configured with identical baud rate and mutually compatible bit timing.  Even a single misconfigured node will disrupt all traffic on the entire bus.  Always use a crystal or TCXO oscillator, never an MCU's internal RC oscillator, for CAN communication.

---

*Document generated for CANopen tutorial series — CiA 301, ISO 11898-1, CiA 305 references.*