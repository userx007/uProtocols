# 97. CAN Bus Repair and Maintenance

**Structure:**
- Fundamentals recap with voltage reference table
- All six common failure modes (open circuit, short circuit, bad termination, failed transceiver, ground loops, intermittent faults)
- Step-by-step troubleshooting methodology (physical → oscilloscope → node isolation → software)
- Maintenance schedule table and design-for-maintenance best practices

**Seven code examples across C/C++ and Rust:**

| # | Language | What it does |
|---|----------|-------------|
| 1 | C | Linux SocketCAN error frame receiver — decodes and prints TEC/REC and error types |
| 2 | C++ | Periodic health monitor polling sysfs stats and logging state transitions |
| 3 | C | Bare-metal STM32 bxCAN register reader for embedded targets (TEC, REC, LEC, bus state) |
| 4 | C++ | Thread-safe ring buffer fault logger for post-mortem intermittent fault analysis |
| 5 | Rust | SocketCAN error monitor with error rate statistics |
| 6 | Rust | `no_std`-compatible lock-free ring buffer for embedded CAN fault logging |
| 7 | Rust | Loopback self-test to isolate controller vs. transceiver faults |


## Troubleshooting Techniques for Identifying Broken Wires, Failed Transceivers, and Intermittent Faults

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Bus Fundamentals Recap](#can-bus-fundamentals-recap)
3. [Common Failure Modes](#common-failure-modes)
4. [Diagnostic Tools and Equipment](#diagnostic-tools-and-equipment)
5. [Step-by-Step Troubleshooting](#step-by-step-troubleshooting)
6. [Detecting Broken Wires](#detecting-broken-wires)
7. [Diagnosing Failed Transceivers](#diagnosing-failed-transceivers)
8. [Hunting Intermittent Faults](#hunting-intermittent-faults)
9. [Software-Based Diagnostics in C/C++](#software-based-diagnostics-in-cc)
10. [Software-Based Diagnostics in Rust](#software-based-diagnostics-in-rust)
11. [Preventive Maintenance Strategies](#preventive-maintenance-strategies)
12. [Summary](#summary)

---

## Introduction

Controller Area Network (CAN) bus is one of the most robust and widely deployed serial communication standards in embedded systems — used extensively in automotive electronics, industrial automation, medical devices, and aerospace systems. Its differential signaling and multi-master architecture make it inherently fault-tolerant. However, no physical medium is immune to degradation, and CAN bus failures — when they occur — can be subtle, intermittent, and expensive to diagnose without a systematic approach.

This chapter focuses on the practical techniques required to:

- Identify **broken or degraded wiring** in CAN harnesses
- Detect **failed or marginal transceivers**
- Capture and characterise **intermittent faults** that appear only under load, temperature, or vibration
- Use **software diagnostics** embedded in C/C++ and Rust to supplement hardware measurements

---

## CAN Bus Fundamentals Recap

A CAN bus consists of two wires: **CAN_H** (CAN High) and **CAN_L** (CAN Low), forming a twisted pair terminated at both ends with **120 Ω resistors**. The differential voltage between these lines encodes bits:

| State      | CAN_H Voltage | CAN_L Voltage | Differential (CAN_H - CAN_L) |
|------------|---------------|---------------|-------------------------------|
| Recessive  | ~2.5 V        | ~2.5 V        | ~0 V                         |
| Dominant   | ~3.5 V        | ~1.5 V        | ~2.0 V                       |

**Key electrical rules:**
- Total bus resistance (measured between CAN_H and CAN_L with all power off) should be approximately **60 Ω** (two 120 Ω termination resistors in parallel).
- Maximum stub length per node: typically **< 30 cm** at 500 kbit/s.
- Maximum bus length at 500 kbit/s: approximately **100 m**.

Understanding these values is the baseline for all electrical fault diagnosis.

---

## Common Failure Modes

### 1. Open Circuit (Broken Wire)
A break in CAN_H or CAN_L creates a segment of the bus that is electrically isolated. Nodes beyond the break can no longer communicate with nodes on the other side.

**Symptoms:**
- Specific nodes go offline while others remain functional
- Increased error counters on nodes near the break
- Bus may still function if only one wire is broken (single-wire fallback mode in some transceivers)

### 2. Short Circuit
- **CAN_H to CAN_L short**: Destroys the differential nature of the signal; the bus is permanently dominant or flooded with errors.
- **CAN_H or CAN_L to ground**: Typically causes a stuck-dominant condition.
- **CAN_H or CAN_L to supply (VCC)**: Can damage transceivers and microcontrollers.

**Symptoms:**
- All nodes go bus-off or report excessive error frames
- Bus resistance measurement is far below 60 Ω or near 0 Ω
- Oscilloscope shows flat, non-toggling waveform

### 3. Missing or Wrong Termination
- **No termination**: Signal reflections cause bit errors, especially at higher bit rates. Bus may work at short ranges or slow speeds but fail under load.
- **Single terminator**: One end unterminated; reflections occur from that end.
- **Incorrect resistor value**: Values above ~130 Ω or below ~100 Ω cause marginal operation.

**Symptoms:**
- High error rate, especially at higher baud rates or longer bus lengths
- Ringing visible on oscilloscope at end of dominant pulses

### 4. Failed Transceiver
Transceivers fail due to electrostatic discharge (ESD), reverse polarity, overvoltage, or overtemperature. A failed transceiver can:
- Hold the bus dominant (dominant stuck fault) — blocks all communication
- Become high impedance (open) — the node simply disappears from the bus
- Introduce resistive loading — degrades signal quality for all nodes

### 5. Ground Loops and Ground Offset
In distributed systems with long wire runs, nodes may have different ground potentials. If the ground offset between two nodes exceeds the common-mode range of the transceivers (typically ±12 V for ISO 11898-2 devices), transceivers can misread bits or sustain damage.

**Symptoms:**
- Errors that correlate with load switching or system state
- Higher error rate between specific pairs of nodes
- Transceiver damage occurring without obvious cause

### 6. Intermittent Faults
These are the most challenging. Causes include:
- Connector corrosion (increased contact resistance, often temperature-sensitive)
- Wire chafing under vibration (occasional open circuit)
- Thermal cycling causing cold solder joints to open and close
- Moisture ingress that clears upon drying

---

## Diagnostic Tools and Equipment

### Essential Hardware Tools

| Tool | Purpose |
|------|---------|
| **Digital Multimeter (DMM)** | Resistance, voltage, continuity checks |
| **Oscilloscope (2-channel)** | Signal waveform capture on CAN_H and CAN_L |
| **CAN Analyser / USB-to-CAN** | Frame-level traffic monitoring and injection |
| **Termination Resistor Test Set** | Verify termination without opening connectors |
| **Time Domain Reflectometer (TDR)** | Locate opens/shorts by measuring signal reflection time |
| **Protocol Analyser (e.g. Vector CANalyzer)** | In-depth frame decoding and bus load analysis |

### Software Tools

- **candump / cansend** (Linux SocketCAN) — logging and injection from the command line
- **Wireshark with CAN plugin** — frame-level capture and filtering
- **busmaster** — open-source CAN analyser for Windows
- Custom embedded diagnostics (discussed below)

---

## Step-by-Step Troubleshooting

### Step 1: Check Physical Layer First

Before any software diagnosis, verify the electrical health of the bus.

**With the entire system powered off:**

```
1. Disconnect all ECUs/nodes from the bus.
2. Measure resistance across CAN_H and CAN_L at one end of the harness.
   Expected: ~120 Ω per segment (one terminator present).
3. Connect both terminators (or reconnect the harness as a whole).
   Expected: ~60 Ω (120 Ω ∥ 120 Ω).
4. If significantly lower (< 40 Ω): suspect a short circuit.
5. If significantly higher (> 80 Ω): suspect a missing terminator, open wire, or poor connection.
6. Measure CAN_H-to-GND and CAN_L-to-GND resistance.
   Both should be high impedance (>> 10 kΩ) with power off.
```

**With the system powered up but idle:**

```
1. Measure DC voltages: CAN_H ≈ 2.5 V, CAN_L ≈ 2.5 V (recessive state).
2. If CAN_H is stuck at 3.5 V or CAN_L at 1.5 V: suspect a stuck-dominant transceiver.
3. If CAN_H or CAN_L reads 0 V or VCC: suspect a short circuit.
```

### Step 2: Oscilloscope Inspection

Connect oscilloscope probes differentially: Channel 1 on CAN_H, Channel 2 on CAN_L. Set trigger on the differential signal.

**What to look for:**

- **Correct waveform**: Clean dominant/recessive transitions, symmetrical rise/fall times (typically < 100 ns at 500 kbit/s), no excessive ringing.
- **Ringing after transitions**: Caused by missing or incorrect termination, or excessive stub lengths.
- **Asymmetric signals**: One rail not moving properly — suspect a partial short or failed transceiver.
- **Random spikes**: Electromagnetic interference (EMI), ground loops, or nearby switching power supplies.
- **Eye diagram**: Use persistence mode to build an eye diagram. A "closed" eye indicates marginal timing margins.

### Step 3: Node Isolation

Disconnect nodes one by one to identify whether a particular node is causing bus errors. After removing each node:
- Check bus resistance (should remain near 60 Ω if the node had no internal termination)
- Monitor error frames on a CAN analyser

A node that, when disconnected, eliminates bus errors is either:
- Holding the bus dominant (failed transceiver)
- Generating malformed frames (software or oscillator fault)

### Step 4: Software Error Counter Monitoring

All CAN controllers maintain **Transmit Error Counter (TEC)** and **Receive Error Counter (REC)**. Reading these periodically reveals which node is experiencing errors and whether it is failing to transmit or failing to receive.

Thresholds (per ISO 11898-1):

| State            | TEC / REC value |
|------------------|-----------------|
| Error Active     | TEC < 128 and REC < 128 |
| Error Passive    | TEC ≥ 128 or REC ≥ 128 |
| Bus Off          | TEC ≥ 256 |

---

## Detecting Broken Wires

### Segment Isolation Technique

If you suspect a broken wire in a segment of harness, the TDR is the most effective tool. However, in systems where TDR access is impractical, you can use the **node isolation** method:

1. Identify the last known-good node on one side and the first non-communicating node on the other.
2. Perform continuity checks at each connector between those nodes.
3. Measure resistance from a known-good point progressively along the harness.

An open circuit will show as infinite resistance from the point of the break onwards.

### Single-Wire Fault Detection

ISO 11898-3 defines a **low-speed fault-tolerant CAN** variant that can operate on a single wire. In standard (ISO 11898-2) high-speed systems, most modern transceivers can detect a single open wire via the TXD_DOMINANT_TIMEOUT mechanism and flag the condition. Check the transceiver's status/diagnostic registers (where available, such as on TJA1145, TJA1442, SN65HVD230 with diagnostics support).

---

## Diagnosing Failed Transceivers

### Visual Inspection

Burned transceivers often show visible discolouration. However, ESD damage is usually invisible. Always check:
- Correct supply voltage on VCC pin
- VIO/logic supply level matches the MCU
- Enable (EN) pin state
- TXD pin driven by the MCU (not floating)

### Dominant Stuck Fault

If a transceiver holds CAN_H high or CAN_L low, the bus is stuck dominant. The differential voltage will read approximately 2 V even with no node intentionally transmitting. To identify the culprit:

1. Disconnect nodes one at a time.
2. When the bus returns to recessive (CAN_H ≈ CAN_L ≈ 2.5 V), the last disconnected node contains the failed transceiver.

### Loopback Self-Test

Many CAN controllers support an **internal loopback mode** where TXD is connected internally to RXD without driving the physical bus. This allows you to verify that the controller itself is functional, isolating the fault to the transceiver.

---

## Hunting Intermittent Faults

Intermittent faults are the most time-consuming to diagnose. The key principle is: **you must capture the fault as it occurs**.

### Strategies

1. **Data Logging**: Use a CAN analyser or embedded logger to continuously record all frames, error frames, and bus-off events with high-resolution timestamps.

2. **Error Counter Polling**: Poll TEC and REC at high frequency. A rising TEC indicates a node struggling to transmit; a rising REC indicates reception problems (possibly affecting all nodes near a bad harness section).

3. **Stress Testing**: Apply mechanical stress (flex harnesses, wiggle connectors) while monitoring for fault-triggered error frames. Vibration tables can replicate vehicle or industrial vibration profiles.

4. **Thermal Cycling**: Heat-gun suspect connectors or cool them with freeze spray. Cold joints typically open when cold; corrosion contacts may clear when heated (as oxide layers crack and break).

5. **Error Frame Counting over Time**: Plot error frame rate vs. time. Faults that correlate with thermal patterns (hotter in afternoon, fewer errors at night) suggest thermal expansion issues.

6. **Trigger-Based Capture**: Configure the oscilloscope to trigger on error frames (look for the error flag: six consecutive dominant bits outside a frame). This freezes the display at the precise moment of failure.

---

## Software-Based Diagnostics in C/C++

The following examples use Linux **SocketCAN** (available on any Linux system with a CAN interface) and a generic register-level interface representative of STM32 bxCAN or similar controllers.

### Example 1: Reading Error Counters and Bus State (Linux SocketCAN / C)

```c
/* can_diagnostics.c
 * Reads CAN error counters and bus state using Linux SocketCAN netlink interface.
 * Compile: gcc -o can_diagnostics can_diagnostics.c
 * Usage: ./can_diagnostics can0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <linux/can/error.h>
#include <linux/can/netlink.h>

/* Enable error frames on the socket so we receive CAN error frames */
static int enable_error_reporting(int sock)
{
    can_err_mask_t err_mask = CAN_ERR_TX_TIMEOUT |
                               CAN_ERR_LOSTARB   |
                               CAN_ERR_CRTL      |
                               CAN_ERR_PROT      |
                               CAN_ERR_TRX       |
                               CAN_ERR_ACK       |
                               CAN_ERR_BUSOFF    |
                               CAN_ERR_BUSERROR  |
                               CAN_ERR_RESTARTED;
    return setsockopt(sock, SOL_CAN_RAW, CAN_RAW_ERR_FILTER,
                      &err_mask, sizeof(err_mask));
}

/* Decode and print an error frame received from the kernel */
static void decode_error_frame(const struct can_frame *frame)
{
    printf("\n[ERROR FRAME] can_id=0x%08X\n", frame->can_id);

    if (frame->can_id & CAN_ERR_TX_TIMEOUT)
        printf("  -> TX timeout (no ACK received)\n");

    if (frame->can_id & CAN_ERR_LOSTARB)
        printf("  -> Lost arbitration at bit %d\n", frame->data[0]);

    if (frame->can_id & CAN_ERR_CRTL) {
        printf("  -> Controller error: ");
        if (frame->data[1] & CAN_ERR_CRTL_RX_OVERFLOW)  printf("RX overflow ");
        if (frame->data[1] & CAN_ERR_CRTL_TX_OVERFLOW)  printf("TX overflow ");
        if (frame->data[1] & CAN_ERR_CRTL_RX_WARNING)   printf("RX warning ");
        if (frame->data[1] & CAN_ERR_CRTL_TX_WARNING)   printf("TX warning ");
        if (frame->data[1] & CAN_ERR_CRTL_RX_PASSIVE)   printf("RX passive ");
        if (frame->data[1] & CAN_ERR_CRTL_TX_PASSIVE)   printf("TX passive ");
        printf("\n");
        /* data[6] = TX error counter, data[7] = RX error counter */
        printf("     TEC=%u  REC=%u\n", frame->data[6], frame->data[7]);
    }

    if (frame->can_id & CAN_ERR_BUSOFF)
        printf("  -> BUS-OFF state entered!\n");

    if (frame->can_id & CAN_ERR_PROT) {
        printf("  -> Protocol violation: type=0x%02X location=0x%02X\n",
               frame->data[2], frame->data[3]);
    }
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <can_interface>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return EXIT_FAILURE; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
        perror("ioctl SIOCGIFINDEX"); close(sock); return EXIT_FAILURE;
    }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return EXIT_FAILURE;
    }

    if (enable_error_reporting(sock) < 0) {
        perror("setsockopt error filter"); close(sock); return EXIT_FAILURE;
    }

    printf("Monitoring CAN errors on %s (Ctrl+C to stop)...\n", argv[1]);

    unsigned long normal_frames = 0;
    unsigned long error_frames  = 0;

    struct can_frame frame;
    while (1) {
        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }
        if (nbytes < (ssize_t)sizeof(struct can_frame)) continue;

        if (frame.can_id & CAN_ERR_FLAG) {
            error_frames++;
            decode_error_frame(&frame);
            printf("  [Stats] Normal frames: %lu  Error frames: %lu  "
                   "Error rate: %.2f%%\n",
                   normal_frames, error_frames,
                   (double)error_frames / (double)(normal_frames + error_frames + 1) * 100.0);
        } else {
            normal_frames++;
        }
    }

    close(sock);
    return EXIT_SUCCESS;
}
```

### Example 2: Periodic Error Counter Poll and Bus State Logger (C++ / SocketCAN)

```cpp
/* can_health_monitor.cpp
 * Periodically reads CAN error counters via netlink and logs state transitions.
 * Compile: g++ -std=c++17 -o can_health_monitor can_health_monitor.cpp
 * Usage: ./can_health_monitor can0 500
 *        (polls every 500 ms)
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <chrono>
#include <thread>
#include <iomanip>
#include <ctime>
#include <stdexcept>

/* CAN error counters are exposed via /sys/class/net/<iface>/statistics/
 * and the netlink IFLA_CAN_BERR_COUNTER attribute.
 * For simplicity, this example reads the sysfs bus_error_count and
 * the /proc-style stats available via `ip -details -statistics link show`.
 * For embedded targets, replace with direct register reads.
 */

struct CanStats {
    unsigned long rx_errors;
    unsigned long tx_errors;
    unsigned long bus_errors;
    unsigned long restarts;
};

static CanStats read_stats(const std::string& iface)
{
    auto read_sysfs = [&](const std::string& file) -> unsigned long {
        std::ifstream f("/sys/class/net/" + iface + "/statistics/" + file);
        unsigned long val = 0;
        if (f) f >> val;
        return val;
    };

    return CanStats {
        .rx_errors  = read_sysfs("rx_errors"),
        .tx_errors  = read_sysfs("tx_errors"),
        .bus_errors = read_sysfs("rx_frame_errors"), // proxy for bus errors
        .restarts   = read_sysfs("tx_aborted_errors"),
    };
}

static std::string timestamp()
{
    auto now  = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <interface> [poll_ms]\n";
        return EXIT_FAILURE;
    }
    std::string iface = argv[1];
    int poll_ms = (argc >= 3) ? std::stoi(argv[2]) : 1000;

    std::cout << "CAN Health Monitor: " << iface
              << "  poll=" << poll_ms << " ms\n";
    std::cout << std::string(70, '-') << '\n';
    std::cout << std::left
              << std::setw(22) << "Timestamp"
              << std::setw(12) << "RX_Errors"
              << std::setw(12) << "TX_Errors"
              << std::setw(12) << "Bus_Errors"
              << std::setw(10) << "Restarts"
              << '\n';
    std::cout << std::string(70, '-') << '\n';

    CanStats prev = {};
    bool first = true;

    while (true) {
        try {
            CanStats s = read_stats(iface);

            if (!first) {
                /* Only print if something changed */
                if (s.rx_errors  != prev.rx_errors  ||
                    s.tx_errors  != prev.tx_errors  ||
                    s.bus_errors != prev.bus_errors ||
                    s.restarts   != prev.restarts) {

                    std::cout << std::left
                              << std::setw(22) << timestamp()
                              << std::setw(12) << s.rx_errors
                              << std::setw(12) << s.tx_errors
                              << std::setw(12) << s.bus_errors
                              << std::setw(10) << s.restarts
                              << '\n';

                    if (s.restarts > prev.restarts)
                        std::cout << "  *** BUS-OFF RECOVERY DETECTED ***\n";
                }
            } else {
                std::cout << std::left
                          << std::setw(22) << timestamp()
                          << std::setw(12) << s.rx_errors
                          << std::setw(12) << s.tx_errors
                          << std::setw(12) << s.bus_errors
                          << std::setw(10) << s.restarts
                          << "  [baseline]\n";
                first = false;
            }

            prev = s;
        } catch (const std::exception& ex) {
            std::cerr << "Error reading stats: " << ex.what() << '\n';
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(poll_ms));
    }

    return EXIT_SUCCESS;
}
```

### Example 3: Embedded Error Counter Reader (STM32 bxCAN — C)

```c
/* stm32_can_diagnostics.c
 * Direct register-level CAN diagnostic for STM32 bxCAN peripheral.
 * Intended for bare-metal or RTOS embedded targets.
 * Adapt base address for your specific MCU variant.
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>  /* or replace with your UART print */

/* STM32 bxCAN register base (CAN1 on most STM32F1/F4 devices) */
#define CAN1_BASE   0x40006400UL
#define CAN_ESR     (*(volatile uint32_t *)(CAN1_BASE + 0x18U))
#define CAN_MSR     (*(volatile uint32_t *)(CAN1_BASE + 0x04U))

/* ESR bit definitions */
#define CAN_ESR_EWGF    (1U << 0)   /* Error Warning Flag */
#define CAN_ESR_EPVF    (1U << 1)   /* Error Passive Flag */
#define CAN_ESR_BOFF    (1U << 2)   /* Bus-Off Flag */
#define CAN_ESR_LEC_MSK (0x7U << 4) /* Last Error Code mask */
#define CAN_ESR_TEC_POS (16U)       /* TX Error Counter position */
#define CAN_ESR_REC_POS (24U)       /* RX Error Counter position */

/* Last Error Code values */
static const char* lec_description(uint8_t lec)
{
    switch (lec) {
        case 0: return "No Error";
        case 1: return "Stuff Error";
        case 2: return "Form Error";
        case 3: return "Acknowledgment Error";
        case 4: return "Bit Recessive Error";
        case 5: return "Bit Dominant Error";
        case 6: return "CRC Error";
        case 7: return "Set by Software";
        default: return "Unknown";
    }
}

typedef struct {
    uint8_t  tec;          /* Transmit Error Counter (0-255+) */
    uint8_t  rec;          /* Receive Error Counter */
    uint8_t  lec;          /* Last Error Code */
    bool     error_warning;
    bool     error_passive;
    bool     bus_off;
} CanBusState;

CanBusState can_read_bus_state(void)
{
    uint32_t esr = CAN_ESR;

    return (CanBusState) {
        .tec           = (uint8_t)((esr >> CAN_ESR_TEC_POS) & 0xFF),
        .rec           = (uint8_t)((esr >> CAN_ESR_REC_POS) & 0xFF),
        .lec           = (uint8_t)((esr >> 4) & 0x07),
        .error_warning = (esr & CAN_ESR_EWGF) != 0,
        .error_passive = (esr & CAN_ESR_EPVF) != 0,
        .bus_off       = (esr & CAN_ESR_BOFF) != 0,
    };
}

void can_print_bus_state(const CanBusState *s)
{
    printf("CAN Bus State:\n");
    printf("  TEC           : %u\n", s->tec);
    printf("  REC           : %u\n", s->rec);
    printf("  Last Error    : %s (%u)\n", lec_description(s->lec), s->lec);
    printf("  Error Warning : %s\n", s->error_warning ? "YES (>=96 errors)" : "No");
    printf("  Error Passive : %s\n", s->error_passive ? "YES (>=128 errors)" : "No");
    printf("  Bus-Off       : %s\n", s->bus_off       ? "YES (>=256 errors)" : "No");

    if (s->bus_off) {
        printf("  ACTION: Node has entered bus-off. Recovery requires\n"
               "          128 occurrences of 11 recessive bits.\n");
    } else if (s->error_passive) {
        printf("  WARNING: Node is error-passive. Check wiring, termination,\n"
               "           and transceiver health.\n");
    }
}

/* Example usage in a periodic task or RTOS thread */
void can_diagnostics_task(void)
{
    CanBusState state = can_read_bus_state();
    can_print_bus_state(&state);

    /* Implement a simple fault log — store to NVM if bus-off is detected */
    if (state.bus_off) {
        /* save_fault_to_nvm(FAULT_CAN_BUSOFF, state.tec, state.rec); */
    }
}
```

### Example 4: Intermittent Fault Logger with Ring Buffer (C++)

```cpp
/* can_fault_log.cpp
 * Captures error events into a ring buffer with timestamps for post-mortem analysis.
 * Works with Linux SocketCAN; adapt for embedded use by replacing socket I/O.
 */

#include <iostream>
#include <array>
#include <atomic>
#include <cstdint>
#include <chrono>
#include <string>
#include <iomanip>
#include <mutex>

template<typename T, std::size_t N>
class RingBuffer {
public:
    void push(const T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        data_[head_] = item;
        head_        = (head_ + 1) % N;
        if (size_ < N) size_++;
    }

    void dump(std::ostream& out) const {
        std::lock_guard<std::mutex> lock(mutex_);
        std::size_t start = (size_ == N) ? head_ : 0;
        for (std::size_t i = 0; i < size_; ++i)
            out << data_[(start + i) % N];
    }

    std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return size_;
    }

private:
    mutable std::mutex mutex_;
    std::array<T, N>   data_ {};
    std::size_t        head_ {0};
    std::size_t        size_ {0};
};

struct FaultEvent {
    uint64_t    timestamp_us;
    uint32_t    can_id;
    uint8_t     tec;
    uint8_t     rec;
    std::string description;

    friend std::ostream& operator<<(std::ostream& os, const FaultEvent& e) {
        os << "[" << std::setw(14) << e.timestamp_us << " µs] "
           << "CAN_ID=0x" << std::hex << std::setw(8) << std::setfill('0')
           << e.can_id << std::dec << std::setfill(' ')
           << "  TEC=" << std::setw(3) << (int)e.tec
           << "  REC=" << std::setw(3) << (int)e.rec
           << "  " << e.description << '\n';
        return os;
    }
};

/* Global fault log holding the last 256 events */
static RingBuffer<FaultEvent, 256> g_fault_log;

static uint64_t now_us()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()
    ).count();
}

void log_can_error(uint32_t can_id, uint8_t tec, uint8_t rec,
                   const std::string& desc)
{
    g_fault_log.push(FaultEvent {
        .timestamp_us = now_us(),
        .can_id       = can_id,
        .tec          = tec,
        .rec          = rec,
        .description  = desc,
    });
}

void dump_fault_log()
{
    std::cout << "\n=== CAN FAULT LOG (" << g_fault_log.size()
              << " events) ===\n";
    g_fault_log.dump(std::cout);
    std::cout << "=== END OF LOG ===\n";
}

/* Integrate with the error frame receiver from Example 1:
 *
 *   if (frame.can_id & CAN_ERR_BUSOFF)
 *       log_can_error(frame.can_id, frame.data[6], frame.data[7], "BUS-OFF");
 *   if (frame.can_id & CAN_ERR_CRTL)
 *       log_can_error(frame.can_id, frame.data[6], frame.data[7], "CRTL error");
 *
 * On SIGINT or watchdog trigger, call dump_fault_log() to persist data.
 */
```

---

## Software-Based Diagnostics in Rust

### Example 5: CAN Error Monitor (Linux SocketCAN / Rust)

```toml
# Cargo.toml
[package]
name    = "can_diagnostics"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan = "3"
chrono    = "0.4"
```

```rust
// src/main.rs
// CAN bus error monitor using the `socketcan` crate on Linux.
// Usage: cargo run -- can0

use socketcan::{CanSocket, Socket, CanFrame, EmbeddedFrame};
use socketcan::frame::{CanErrorFrame, CanDataFrame};
use std::env;
use std::time::{Instant, Duration};

/// Classify the error and return a human-readable description.
fn describe_error(error_frame: &CanErrorFrame) -> String {
    use socketcan::frame::ErrorCondition;
    let mut parts = Vec::new();

    if error_frame.is_error_condition(ErrorCondition::BusOff) {
        parts.push("BUS-OFF".to_string());
    }
    if error_frame.is_error_condition(ErrorCondition::BusWarning) {
        parts.push("Bus-Warning".to_string());
    }
    if error_frame.is_error_condition(ErrorCondition::BusPassive) {
        parts.push("Error-Passive".to_string());
    }
    if error_frame.is_error_condition(ErrorCondition::TxTimeout) {
        parts.push("TX-Timeout (no ACK)".to_string());
    }
    if error_frame.is_error_condition(ErrorCondition::LostArbitration) {
        parts.push("Lost-Arbitration".to_string());
    }

    if parts.is_empty() {
        format!("Error frame: raw_id=0x{:08X}", error_frame.raw_id())
    } else {
        parts.join(" | ")
    }
}

fn main() -> anyhow::Result<()> {
    let iface = env::args().nth(1).unwrap_or_else(|| "can0".to_string());

    println!("CAN Diagnostics Monitor on '{}'", iface);
    println!("Press Ctrl+C to stop.\n");

    let sock = CanSocket::open(&iface)
        .map_err(|e| anyhow::anyhow!("Cannot open {}: {}", iface, e))?;

    sock.set_read_timeout(Duration::from_secs(5))?;

    let start       = Instant::now();
    let mut normal  = 0u64;
    let mut errors  = 0u64;

    loop {
        match sock.read_frame() {
            Ok(frame) => {
                let elapsed = start.elapsed().as_millis();

                match frame {
                    socketcan::CanAnyFrame::Error(err_frame) => {
                        errors += 1;
                        println!(
                            "[{:>8} ms] ERROR #{}: {}",
                            elapsed,
                            errors,
                            describe_error(&err_frame)
                        );
                        let error_rate = (errors as f64)
                            / (normal + errors + 1) as f64 * 100.0;
                        println!(
                            "            Stats: {} normal, {} errors, {:.2}% error rate",
                            normal, errors, error_rate
                        );
                    }
                    socketcan::CanAnyFrame::Normal(_) => {
                        normal += 1;
                    }
                    _ => {}
                }
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock
                   || e.kind() == std::io::ErrorKind::TimedOut => {
                // Timeout — bus is quiet, no messages received
                println!("[{:>8} ms] Bus silent (timeout) — check wiring/termination",
                         start.elapsed().as_millis());
            }
            Err(e) => {
                eprintln!("Read error: {}", e);
                break;
            }
        }
    }

    println!("\nFinal: {} normal frames, {} error frames", normal, errors);
    Ok(())
}
```

### Example 6: Ring Buffer Fault Logger in Rust (Embedded / no_std compatible)

```rust
// fault_log.rs
// A no_std-compatible ring buffer for CAN fault events.
// On std targets, use with Mutex<FaultLog>; on embedded, use from interrupt context
// with critical sections.

#![allow(dead_code)]

use core::sync::atomic::{AtomicUsize, Ordering};

/// A single captured fault event.
#[derive(Copy, Clone, Debug, Default)]
pub struct FaultEvent {
    /// Monotonic timestamp in milliseconds (or ticks).
    pub timestamp_ms: u32,
    /// CAN error code (platform-specific, e.g. ESR register value).
    pub error_code: u32,
    /// Transmit Error Counter at time of fault.
    pub tec: u8,
    /// Receive Error Counter at time of fault.
    pub rec: u8,
    /// Fault category.
    pub category: FaultCategory,
}

#[derive(Copy, Clone, Debug, Default, PartialEq)]
#[repr(u8)]
pub enum FaultCategory {
    #[default]
    None        = 0,
    StuffError  = 1,
    FormError   = 2,
    AckError    = 3,
    BitError    = 4,
    CrcError    = 5,
    ErrorPassive = 6,
    BusOff      = 7,
    BusSilent   = 8,  // No frames received for > timeout period
}

const LOG_CAPACITY: usize = 128;

/// Lock-free (single-producer, single-consumer) ring buffer for fault events.
pub struct FaultLog {
    entries:  [FaultEvent; LOG_CAPACITY],
    write:    AtomicUsize,
    read:     AtomicUsize,
}

impl FaultLog {
    pub const fn new() -> Self {
        Self {
            entries: [FaultEvent {
                timestamp_ms: 0,
                error_code: 0,
                tec: 0,
                rec: 0,
                category: FaultCategory::None,
            }; LOG_CAPACITY],
            write: AtomicUsize::new(0),
            read:  AtomicUsize::new(0),
        }
    }

    /// Record a new fault event.
    /// Safe to call from interrupt context if only one producer exists.
    pub fn push(&self, event: FaultEvent) {
        let w = self.write.load(Ordering::Relaxed);
        let next_w = (w + 1) % LOG_CAPACITY;

        // Overwrite oldest entry if full (we prefer recent data)
        // SAFETY: single producer, exclusive write access to entries[w]
        unsafe {
            let ptr = self.entries.as_ptr().add(w) as *mut FaultEvent;
            ptr.write(event);
        }

        self.write.store(next_w, Ordering::Release);
    }

    /// Drain all events and pass each to a callback (for logging to UART, NVM, etc.)
    pub fn drain<F: FnMut(&FaultEvent)>(&self, mut callback: F) {
        loop {
            let r = self.read.load(Ordering::Acquire);
            let w = self.write.load(Ordering::Acquire);
            if r == w { break; }

            // SAFETY: single consumer, exclusive read access to entries[r]
            let event = unsafe {
                &*self.entries.as_ptr().add(r)
            };
            callback(event);
            self.read.store((r + 1) % LOG_CAPACITY, Ordering::Release);
        }
    }

    /// Count pending (unread) entries.
    pub fn pending(&self) -> usize {
        let w = self.write.load(Ordering::Acquire);
        let r = self.read.load(Ordering::Acquire);
        (w + LOG_CAPACITY - r) % LOG_CAPACITY
    }
}

// ---- Usage example (std environment) ----

use std::sync::Mutex;

static FAULT_LOG: Mutex<FaultLog> = Mutex::new(FaultLog::new());

pub fn log_fault(timestamp_ms: u32, tec: u8, rec: u8, cat: FaultCategory) {
    let event = FaultEvent {
        timestamp_ms,
        error_code: 0,
        tec,
        rec,
        category: cat,
    };
    if let Ok(log) = FAULT_LOG.lock() {
        log.push(event);
    }
}

pub fn print_fault_log() {
    if let Ok(log) = FAULT_LOG.lock() {
        log.drain(|event| {
            println!(
                "[{:>8} ms] {:?}  TEC={:3}  REC={:3}",
                event.timestamp_ms,
                event.category,
                event.tec,
                event.rec,
            );
        });
    }
}
```

### Example 7: Rust CAN Bus Self-Test Routine

```rust
// can_self_test.rs
// Implements a loopback self-test using socketcan.
// Sets the interface to loopback mode, sends a known frame, and verifies receipt.

use socketcan::{CanSocket, Socket, CanDataFrame, EmbeddedFrame};
use socketcan::frame::CanId;
use std::time::Duration;

const TEST_FRAME_ID:  u32 = 0x7FF;
const TEST_FRAME_DATA: [u8; 4] = [0xDE, 0xAD, 0xBE, 0xEF];

pub enum SelfTestResult {
    Pass,
    NoEcho,
    DataMismatch { expected: [u8; 4], received: Vec<u8> },
    Error(String),
}

pub fn run_loopback_self_test(iface: &str) -> SelfTestResult {
    // On Linux, enable loopback: `ip link set can0 type can loopback on`
    // Or use SO_CAN_RAW_RECV_OWN_MSGS socket option
    let sock = match CanSocket::open(iface) {
        Ok(s)  => s,
        Err(e) => return SelfTestResult::Error(format!("Open failed: {}", e)),
    };

    // Receive own messages
    if let Err(e) = sock.set_recv_own_msgs(true) {
        return SelfTestResult::Error(format!("set_recv_own_msgs: {}", e));
    }
    if let Err(e) = sock.set_read_timeout(Duration::from_millis(200)) {
        return SelfTestResult::Error(format!("set timeout: {}", e));
    }

    // Build and send a test frame
    let id = socketcan::StandardId::new(TEST_FRAME_ID as u16)
        .expect("valid ID");
    let tx_frame = CanDataFrame::new(id, &TEST_FRAME_DATA)
        .expect("valid frame");

    if let Err(e) = sock.write_frame(&tx_frame) {
        return SelfTestResult::Error(format!("TX failed: {}", e));
    }

    // Attempt to receive the echo
    match sock.read_frame() {
        Ok(socketcan::CanAnyFrame::Normal(rx_frame)) => {
            let rx_data: Vec<u8> = rx_frame.data().to_vec();
            if rx_data == TEST_FRAME_DATA {
                SelfTestResult::Pass
            } else {
                SelfTestResult::DataMismatch {
                    expected: TEST_FRAME_DATA,
                    received: rx_data,
                }
            }
        }
        Ok(_) => SelfTestResult::NoEcho,
        Err(e) if e.kind() == std::io::ErrorKind::WouldBlock
               || e.kind() == std::io::ErrorKind::TimedOut => {
            SelfTestResult::NoEcho
        }
        Err(e) => SelfTestResult::Error(format!("RX failed: {}", e)),
    }
}

fn main() {
    let iface = std::env::args().nth(1).unwrap_or("vcan0".to_string());
    println!("Running CAN loopback self-test on '{}'...", iface);

    match run_loopback_self_test(&iface) {
        SelfTestResult::Pass => {
            println!("PASS: Loopback echo received with correct data.");
        }
        SelfTestResult::NoEcho => {
            println!("FAIL: No echo received. Transceiver or controller may be failed.");
            println!("      Check: TXD connected to MCU, VCC present, EN pin active.");
        }
        SelfTestResult::DataMismatch { expected, received } => {
            println!("FAIL: Data mismatch.");
            println!("      Expected: {:?}", expected);
            println!("      Received: {:?}", received);
            println!("      Possible: bit flip due to EMI, incorrect baud rate, or damaged medium.");
        }
        SelfTestResult::Error(e) => {
            eprintln!("ERROR: {}", e);
        }
    }
}
```

---

## Preventive Maintenance Strategies

### Scheduled Maintenance

| Interval | Task |
|----------|------|
| Commissioning | Verify bus resistance (60 Ω), voltage levels, and full communication check of all nodes |
| Annually | Inspect all connectors for corrosion; re-torque crimp connections; check terminator resistor values with DMM |
| After any wiring work | Re-verify resistance, re-run communication test |
| After environmental events (flooding, temperature excursion) | Full electrical inspection; check transceiver supply voltages |

### Design-for-Maintenance Best Practices

- **Label all connectors** with node name and wire direction (CAN_H / CAN_L / GND)
- **Use colour-coded wire**: yellow for CAN_H, green for CAN_L (common automotive convention)
- **Install test points** at key harness junctions so DMM and oscilloscope probes can be attached without disassembly
- **Log fault events to NVM** (non-volatile memory) with timestamps; retrieve them during service intervals
- **Implement bus-off auto-recovery** with exponential backoff; log each recovery event
- **Monitor error counters** via a background task and report to a vehicle/system management unit

### Firmware Watchdog Integration

A software watchdog that resets the CAN controller (and optionally the transceiver enable pin) after a configurable number of consecutive bus-off events can prevent a node from going permanently silent due to a transient electrical fault. Always log the reset event to NVM.

---

## Summary

CAN bus faults fall into four broad categories — **open circuits**, **short circuits**, **transceiver failures**, and **intermittent faults** — each requiring a distinct diagnostic approach.

The foundation of any diagnosis is the **physical layer check**: measure the 60 Ω bus resistance with power off, then measure the 2.5 V idle voltage levels with power on. An oscilloscope is indispensable for detecting signal quality degradation such as ringing (missing termination) or asymmetric waveforms (partial shorts or failed transceivers). **Node isolation** — systematically disconnecting nodes and observing whether bus health improves — quickly narrows the search space.

At the software level, **error counters (TEC and REC)** embedded in every CAN controller are the primary diagnostic instruments. The examples in this chapter demonstrate how to read these counters on both Linux (via SocketCAN error frames) and embedded targets (via direct register access), and how to record fault events into ring buffers for post-mortem analysis. The **loopback self-test** pattern in Rust isolates controller and transceiver faults with no dependency on external bus activity.

Intermittent faults demand **continuous logging**, triggered capture, and deliberate stress testing (thermal cycling, mechanical flexing). The key insight is that you must capture the fault in the act — post-hoc measurement of a fault that has cleared will yield normal readings. A well-designed system therefore maintains a persistent fault log in NVM so that even a single error event during a 10,000-hour service life is preserved for analysis.

Rigorous preventive maintenance — periodic resistance checks, connector inspection, and NVM fault log review — remains the most cost-effective strategy for minimising CAN bus downtime in production systems.

---

*End of Chapter 97: CAN Bus Repair and Maintenance*