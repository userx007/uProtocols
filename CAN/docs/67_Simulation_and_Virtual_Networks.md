# CAN Simulation and Virtual Networks

Protocol development without physical hardware is a cornerstone of modern embedded systems engineering. Virtual CAN environments let you build, test, and validate CAN stacks, diagnostic routines, and ECU simulations entirely in software — drastically cutting costs and iteration time. Three main tiers exist: professional simulation suites (CANoe, CANalyzer), open-source network tools (Busmaster), and the Linux kernel's `vcan` / `socketcan` subsystem used directly in C/C++ or Rust.

Let me start with an architectural overview of how these layers relate, then dive into the programming details.---

## 1. The Linux vcan Subsystem — The Foundation

Linux's `vcan` (virtual CAN) driver is the workhorse of software-only CAN development. It creates a loopback network interface that behaves identically to a physical CAN bus from the application's perspective. Every process that opens a socket on `vcan0` sees every frame sent by every other process on that same interface — exactly as on a real bus.

**Setting up the virtual interface** (shell, must be done once per boot or in a udev rule):

```bash
# Load the kernel modules
sudo modprobe vcan
sudo modprobe can-dev

# Create and bring up a virtual interface
sudo ip link add dev vcan0 type vcan
sudo ip link set up vcan0

# Optional: add a second bus for gateway testing
sudo ip link add dev vcan1 type vcan
sudo ip link set up vcan1

# Verify
ip link show type vcan

# Monitor all frames in real-time (separate terminal)
candump vcan0

# Inject a test frame
cansend vcan0 123#DEADBEEF01020304
```

---

## 2. C — Raw SocketCAN ECU Simulator

This example simulates a complete ECU node: it periodically broadcasts engine RPM and coolant temperature, and simultaneously listens for incoming requests (e.g., a diagnostic tester asking for data via a request/response pattern).

```c
/* ecu_simulator.c
 * Simulates an Engine Control Unit on vcan0
 * Broadcasts RPM (0x100) and coolant temp (0x101) at fixed intervals,
 * responds to diagnostic requests on 0x7DF with UDS-style replies on 0x7E8.
 *
 * Build: gcc -O2 -Wall -o ecu_sim ecu_simulator.c
 * Run:   ./ecu_sim vcan0
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <time.h>
#include <pthread.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <linux/can.h>
#include <linux/can/raw.h>

/* ── ECU state (shared between threads) ─────────────────────────── */
typedef struct {
    volatile uint16_t rpm;          /* 0–8000 rpm                     */
    volatile int8_t   coolant_c;    /* -40 to +215 °C (offset +40)    */
    volatile uint8_t  ignition_on;  /* 0 = off, 1 = on                */
    pthread_mutex_t   lock;
} EcuState;

static EcuState g_ecu = { .rpm = 800, .coolant_c = 20, .ignition_on = 1 };
static volatile int g_running = 1;

/* ── Helpers ─────────────────────────────────────────────────────── */
static int open_can_socket(const char *iface, canid_t recv_id)
{
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) { perror("socket"); return -1; }

    /* Optionally filter to only receive a specific CAN-ID */
    if (recv_id != 0) {
        struct can_filter filter = {
            .can_id   = recv_id,
            .can_mask = CAN_SFF_MASK   /* exact 11-bit match          */
        };
        setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FILTER,
                   &filter, sizeof(filter));
    }

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind"); close(sock); return -1;
    }
    return sock;
}

/* ── Broadcast thread: sends PDUs at 10 Hz ───────────────────────── */
static void *tx_thread(void *arg)
{
    int sock = *(int *)arg;
    struct can_frame frame;
    struct timespec ts = { .tv_sec = 0, .tv_nsec = 100000000L }; /* 100 ms */

    while (g_running) {
        pthread_mutex_lock(&g_ecu.lock);
        uint16_t rpm      = g_ecu.rpm;
        uint8_t  temp_raw = (uint8_t)(g_ecu.coolant_c + 40); /* SAE J1979 */
        pthread_mutex_unlock(&g_ecu.lock);

        /* Frame 0x100: Engine RPM (2 bytes, big-endian, 0.25 rpm/bit) */
        uint16_t rpm_scaled = rpm * 4; /* 0.25 rpm per LSB            */
        memset(&frame, 0, sizeof(frame));
        frame.can_id  = 0x100;
        frame.can_dlc = 4;
        frame.data[0] = 0x00;                     /* counter / mode byte */
        frame.data[1] = 0x00;
        frame.data[2] = (rpm_scaled >> 8) & 0xFF;
        frame.data[3] =  rpm_scaled       & 0xFF;
        write(sock, &frame, sizeof(frame));

        /* Frame 0x101: Coolant temperature (OBD-II PID 0x05 style)   */
        frame.can_id  = 0x101;
        frame.can_dlc = 3;
        frame.data[0] = 0x02;   /* PCI byte: single frame, 2 bytes    */
        frame.data[1] = 0x41;   /* Mode 0x41 = response to mode 0x01  */
        frame.data[2] = temp_raw;
        write(sock, &frame, sizeof(frame));

        nanosleep(&ts, NULL);

        /* Simulate engine warm-up */
        pthread_mutex_lock(&g_ecu.lock);
        if (g_ecu.coolant_c < 90) g_ecu.coolant_c++;
        if (g_ecu.rpm < 850)      g_ecu.rpm += 5; /* idle stabilise   */
        pthread_mutex_unlock(&g_ecu.lock);
    }
    return NULL;
}

/* ── Diagnostic RX thread: UDS-like request/response ─────────────── */
static void *diag_thread(void *arg)
{
    const char *iface = (const char *)arg;
    int sock = open_can_socket(iface, 0x7DF); /* SAE J1979 functional ID */
    if (sock < 0) return NULL;

    /* We reply on a separate socket without the receive filter */
    int tx_sock = open_can_socket(iface, 0);

    struct can_frame req, resp;
    while (g_running) {
        ssize_t nbytes = read(sock, &req, sizeof(req));
        if (nbytes <= 0) continue;
        if (req.can_dlc < 3) continue;

        uint8_t service = req.data[1]; /* e.g. 0x01 = OBD show current data */
        uint8_t pid     = req.data[2];

        memset(&resp, 0, sizeof(resp));
        resp.can_id  = 0x7E8; /* physical response from ECU #1 */
        resp.can_dlc = 8;
        resp.data[0] = 0x04;  /* PCI: single frame, 4 data bytes    */
        resp.data[1] = service + 0x40; /* positive response          */
        resp.data[2] = pid;

        pthread_mutex_lock(&g_ecu.lock);
        switch (pid) {
            case 0x05: /* Coolant temp */
                resp.data[3] = (uint8_t)(g_ecu.coolant_c + 40);
                break;
            case 0x0C: /* Engine RPM — 2 bytes, A/4 */
                { uint16_t r = g_ecu.rpm * 4;
                  resp.data[3] = (r >> 8) & 0xFF;
                  resp.data[4] =  r       & 0xFF; }
                break;
            default:
                resp.data[1] = 0x7F;  /* negative response           */
                resp.data[2] = service;
                resp.data[3] = 0x31;  /* request out of range        */
        }
        pthread_mutex_unlock(&g_ecu.lock);

        write(tx_sock, &resp, sizeof(resp));
        printf("[diag] svc=0x%02X pid=0x%02X → replied 0x7E8\n",
               service, pid);
    }
    close(tx_sock);
    close(sock);
    return NULL;
}

static void sig_handler(int s) { (void)s; g_running = 0; }

int main(int argc, char *argv[])
{
    const char *iface = (argc > 1) ? argv[1] : "vcan0";
    printf("[ecu_sim] Starting on %s\n", iface);

    pthread_mutex_init(&g_ecu.lock, NULL);
    signal(SIGINT, sig_handler);

    int tx_sock = open_can_socket(iface, 0);
    if (tx_sock < 0) return 1;

    pthread_t tx_tid, diag_tid;
    pthread_create(&tx_tid,   NULL, tx_thread,   &tx_sock);
    pthread_create(&diag_tid, NULL, diag_thread, (void *)iface);

    pthread_join(tx_tid,   NULL);
    pthread_join(diag_tid, NULL);
    close(tx_sock);
    pthread_mutex_destroy(&g_ecu.lock);
    printf("[ecu_sim] Stopped.\n");
    return 0;
}
```

---

## 3. C++ — Diagnostic Tester with Signal Decoding

This tester connects to the same `vcan0`, sends OBD-II requests, decodes responses, and decodes raw PDUs using a simple DBC-style signal map — a minimal version of what CANoe does with a `.dbc` file.

```cpp
// diag_tester.cpp
// OBD-II tester + signal decoder against the C ECU simulator above.
// Build: g++ -std=c++17 -O2 -Wall -o diag_tester diag_tester.cpp -lpthread
// Run:   ./diag_tester vcan0

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <format>       // C++20; use fmt::format if on C++17
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace std::chrono_literals;

/* ── Signal descriptor (DBC-style, very simplified) ─────────────── */
struct CanSignal {
    std::string name;
    uint8_t     start_byte;  /* 0-based byte position in data[]      */
    uint8_t     length;      /* bit length (8 or 16 here)            */
    double      factor;
    double      offset;
    std::string unit;

    double decode(const uint8_t *data) const {
        uint32_t raw = 0;
        for (uint8_t i = 0; i < (length / 8); ++i)
            raw = (raw << 8) | data[start_byte + i];
        return raw * factor + offset;
    }
};

/* ── Minimal signal database (mirrors what a .dbc file provides) ─── */
const std::map<canid_t, std::vector<CanSignal>> kSignalDb = {
    { 0x100, {
        { "EngineRPM",      2, 16, 0.25,   0.0, "rpm" },
    }},
    { 0x101, {
        { "CoolantTemp",    2,  8, 1.0,  -40.0, "°C" },
    }},
};

/* ── SocketCAN wrapper ───────────────────────────────────────────── */
class CanSocket {
public:
    explicit CanSocket(const std::string &iface) {
        fd_ = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (fd_ < 0) throw std::runtime_error("socket() failed");

        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, iface.c_str(), IFNAMSIZ - 1);
        ::ioctl(fd_, SIOCGIFINDEX, &ifr);

        struct sockaddr_can addr{};
        addr.can_family  = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("bind() failed");

        // Set 200 ms receive timeout so the RX loop can check for shutdown
        struct timeval tv{ .tv_sec = 0, .tv_usec = 200'000 };
        setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    }

    ~CanSocket() { if (fd_ >= 0) ::close(fd_); }

    bool send(const can_frame &f) const {
        return ::write(fd_, &f, sizeof(f)) == sizeof(f);
    }

    std::optional<can_frame> receive() const {
        can_frame f{};
        ssize_t n = ::read(fd_, &f, sizeof(f));
        if (n == sizeof(f)) return f;
        return std::nullopt;
    }

private:
    int fd_{-1};
};

/* ── OBD-II request builder ──────────────────────────────────────── */
can_frame build_obd_request(uint8_t pid)
{
    can_frame f{};
    f.can_id  = 0x7DF;   /* SAE J1979 functional broadcast addr    */
    f.can_dlc = 8;
    f.data[0] = 0x02;    /* PCI: single frame, 2 data bytes        */
    f.data[1] = 0x01;    /* Mode 01: show current data             */
    f.data[2] = pid;
    /* bytes 3-7 padding (0xAA is conventional) */
    std::fill(f.data + 3, f.data + 8, 0xAA);
    return f;
}

/* ── Decode an OBD-II positive response ─────────────────────────── */
void decode_obd_response(const can_frame &f)
{
    if (f.can_dlc < 4) return;
    if (f.data[1] != 0x41) return;   /* not mode 01 positive response */
    uint8_t pid = f.data[2];

    switch (pid) {
        case 0x05:
            std::cout << std::format("  [OBD] Coolant temp = {} °C\n",
                                     static_cast<int>(f.data[3]) - 40);
            break;
        case 0x0C:
            {
                uint16_t raw = (static_cast<uint16_t>(f.data[3]) << 8) | f.data[4];
                std::cout << std::format("  [OBD] Engine RPM  = {:.1f} rpm\n",
                                         raw / 4.0);
            }
            break;
        default:
            std::cout << std::format("  [OBD] PID 0x{:02X} = 0x{:02X}\n",
                                     pid, f.data[3]);
    }
}

/* ── Passive sniffer: applies the signal database ────────────────── */
void decode_with_signaldb(const can_frame &f)
{
    auto it = kSignalDb.find(f.can_id & CAN_SFF_MASK);
    if (it == kSignalDb.end()) return;
    for (const auto &sig : it->second) {
        double val = sig.decode(f.data);
        std::cout << std::format("  [DBC] {:15s} = {:8.2f} {}\n",
                                 sig.name, val, sig.unit);
    }
}

int main(int argc, char *argv[])
{
    const std::string iface = (argc > 1) ? argv[1] : "vcan0";
    std::cout << "[diag_tester] Connecting to " << iface << "\n";

    CanSocket sock(iface);
    std::atomic<bool> running{true};

    /* RX thread: sniff all frames and decode via signal DB */
    std::thread rx_thread([&]() {
        while (running) {
            auto frame = sock.receive();
            if (!frame) continue;

            /* Filter out frames we sent ourselves (loopback) */
            if ((frame->can_id & CAN_SFF_MASK) == 0x7DF) continue;

            /* OBD-II response? */
            if ((frame->can_id & CAN_SFF_MASK) == 0x7E8) {
                decode_obd_response(*frame);
            } else {
                decode_with_signaldb(*frame);
            }
        }
    });

    /* TX loop: poll PID 0x05 and 0x0C every 500 ms */
    const std::vector<uint8_t> pids_to_poll = { 0x05, 0x0C };
    int cycle = 0;
    while (running) {
        uint8_t pid = pids_to_poll[cycle % pids_to_poll.size()];
        std::cout << std::format("\n[diag_tester] Requesting PID 0x{:02X}\n", pid);
        sock.send(build_obd_request(pid));
        std::this_thread::sleep_for(500ms);
        ++cycle;
        if (cycle >= 10) running = false;   /* run 5 full cycles then exit */
    }

    rx_thread.join();
    std::cout << "[diag_tester] Done.\n";
    return 0;
}
```

---

## 4. C++ — Virtual CAN Gateway (vcan0 ↔ vcan1)

A gateway bridges two buses, selectively forwarding or transforming frames — essential for testing distributed architectures where, say, a body control module (BCM) on `vcan0` must exchange messages with a powertrain ECU on `vcan1`.

```cpp
// gateway.cpp
// Bridges vcan0 <-> vcan1 with a configurable routing table and
// optional signal-level transformation (e.g., unit conversion).
// Build: g++ -std=c++20 -O2 -Wall -o gateway gateway.cpp -lpthread

#include <array>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <thread>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Route entry: an optional transform applied before forwarding ── */
struct Route {
    canid_t dst_id;
    /* Transform: modify the frame before it is forwarded.
     * Return false to drop (filter) the frame.                       */
    std::function<bool(can_frame &)> transform = nullptr;
};

/* ── Routing table: src_id → Route on destination bus ───────────── */
const std::map<canid_t, Route> kRoutes = {
    /* Forward RPM unchanged */
    { 0x100, { 0x200, nullptr } },

    /* Forward coolant temp but convert from raw ECU format to a    */
    /* different byte layout expected by the powertrain BCM          */
    { 0x101, { 0x201, [](can_frame &f) -> bool {
        /* Destination expects: [temp_raw][0x00][0x00]..., no header */
        uint8_t raw = f.data[2];
        std::fill(f.data, f.data + 8, 0x00);
        f.data[0] = raw;
        f.can_dlc = 2;
        return true;   /* forward after transform */
    }}},

    /* Drop all diagnostic frames — do not route 7DF across buses   */
    { 0x7DF, { 0x000, [](can_frame &) { return false; } } },
};

/* ── Thin SocketCAN handle (no timeout — blocking read) ──────────── */
struct Bus {
    int fd{-1};

    explicit Bus(const char *iface) {
        fd = ::socket(PF_CAN, SOCK_RAW, CAN_RAW);
        struct ifreq ifr{};
        std::strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
        ::ioctl(fd, SIOCGIFINDEX, &ifr);
        struct sockaddr_can addr{ .can_family  = AF_CAN,
                                  .can_ifindex = ifr.ifr_ifindex };
        ::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    }

    ~Bus() { if (fd >= 0) ::close(fd); }

    bool send(const can_frame &f) const {
        return ::write(fd, &f, sizeof(f)) == sizeof(f);
    }

    bool recv(can_frame &f) const {
        return ::read(fd, &f, sizeof(f)) == sizeof(f);
    }
};

/* ── Bridge one direction ─────────────────────────────────────────── */
void bridge(const Bus &rx_bus, const Bus &tx_bus,
            const char *label, std::atomic<bool> &running)
{
    can_frame f;
    while (running) {
        if (!rx_bus.recv(f)) continue;
        canid_t id = f.can_id & CAN_SFF_MASK;

        auto it = kRoutes.find(id);
        if (it == kRoutes.end()) continue;   /* not in routing table */

        const Route &route = it->second;
        if (route.transform && !route.transform(f)) {
            std::cout << std::format("[{}] Dropped  0x{:03X}\n", label, id);
            continue;
        }

        f.can_id = route.dst_id;
        if (tx_bus.send(f))
            std::cout << std::format("[{}] Forwarded 0x{:03X} → 0x{:03X}\n",
                                     label, id, route.dst_id);
    }
}

int main()
{
    Bus bus0("vcan0"), bus1("vcan1");
    std::atomic<bool> running{true};

    std::cout << "[gateway] Bridging vcan0 <-> vcan1\n";

    std::thread t0([&]{ bridge(bus0, bus1, "0→1", running); });
    std::thread t1([&]{ bridge(bus1, bus0, "1→0", running); });

    std::this_thread::sleep_for(std::chrono::seconds(30));
    running = false;
    t0.join(); t1.join();
    return 0;
}
```

---

## 5. Rust — Async ECU Node with `socketcan` crate

The Rust ecosystem has first-class SocketCAN support via the `socketcan` crate, with `tokio` for async I/O. This example implements an ECU node with periodic broadcast, request/response handling, and graceful shutdown — production-grade patterns for embedded systems simulation.

```toml
# Cargo.toml
[package]
name    = "can_ecu_sim"
version = "0.1.0"
edition = "2021"

[dependencies]
socketcan = "3"
tokio     = { version = "1", features = ["full"] }
anyhow    = "1"
```

```rust
// src/main.rs
// Async ECU simulator using socketcan + tokio.
// Run: cargo run -- vcan0
//
// Sends:
//   0x100 — Engine RPM broadcast (10 Hz)
//   0x101 — Coolant temp broadcast (10 Hz)
// Responds to:
//   0x7DF — OBD-II functional requests (Mode 01, PID 05/0C)

use anyhow::Result;
use socketcan::{CanFrame, CanSocket, Frame, Socket};
use std::{
    sync::{
        atomic::{AtomicBool, AtomicI32, AtomicU16, Ordering},
        Arc,
    },
    time::Duration,
};
use tokio::{signal, task, time};

/* ── Shared ECU state (lock-free atomics for this simple case) ───── */
#[derive(Default)]
struct EcuState {
    rpm:        AtomicU16,   // 0–8000
    coolant_c:  AtomicI32,   // -40 to +215 °C
    running:    AtomicBool,
}

impl EcuState {
    fn new(rpm: u16, coolant_c: i32) -> Arc<Self> {
        Arc::new(Self {
            rpm:       AtomicU16::new(rpm),
            coolant_c: AtomicI32::new(coolant_c),
            running:   AtomicBool::new(true),
        })
    }
}

/* ── Frame builders ──────────────────────────────────────────────── */
fn build_rpm_frame(rpm: u16) -> CanFrame {
    let scaled: u16 = rpm.saturating_mul(4);   // 0.25 rpm per LSB
    let data = [
        0x00,
        0x00,
        (scaled >> 8) as u8,
        scaled as u8,
        0, 0, 0, 0,
    ];
    CanFrame::new(0x100.into(), &data[..4]).expect("valid frame")
}

fn build_temp_frame(coolant_c: i32) -> CanFrame {
    let raw = (coolant_c + 40).clamp(0, 255) as u8;   // OBD offset +40
    let data = [0x02, 0x41, raw];
    CanFrame::new(0x101.into(), &data).expect("valid frame")
}

fn build_obd_response(pid: u8, state: &EcuState) -> Option<CanFrame> {
    let rpm = state.rpm.load(Ordering::Relaxed);
    let temp = state.coolant_c.load(Ordering::Relaxed);

    let data: Vec<u8> = match pid {
        0x05 => {
            // Coolant temperature
            let raw = (temp + 40).clamp(0, 255) as u8;
            vec![0x03, 0x41, 0x05, raw]
        }
        0x0C => {
            // Engine RPM (A * 256 + B) / 4
            let scaled = (rpm as u32) * 4;
            vec![0x04, 0x41, 0x0C,
                 ((scaled >> 8) & 0xFF) as u8,
                 (scaled & 0xFF) as u8]
        }
        _ => {
            // Negative response: service not supported
            vec![0x03, 0x7F, 0x01, 0x31]
        }
    };
    CanFrame::new(0x7E8.into(), &data).ok()
}

/* ── Broadcast task: 10 Hz PDU transmission ─────────────────────── */
async fn broadcast_task(iface: String, state: Arc<EcuState>) -> Result<()> {
    // CanSocket is blocking; run in a dedicated OS thread via spawn_blocking
    task::spawn_blocking(move || -> Result<()> {
        let sock = CanSocket::open(&iface)?;
        let mut interval = std::time::Instant::now();
        let period = Duration::from_millis(100);

        while state.running.load(Ordering::Relaxed) {
            let rpm     = state.rpm.load(Ordering::Relaxed);
            let coolant = state.coolant_c.load(Ordering::Relaxed);

            sock.write_frame(&build_rpm_frame(rpm))?;
            sock.write_frame(&build_temp_frame(coolant))?;

            println!("[broadcast] RPM={rpm:5}  Coolant={coolant:3}°C");

            // Simulate engine warm-up
            let new_temp = (coolant + 1).min(90);
            state.coolant_c.store(new_temp, Ordering::Relaxed);

            // Spin-wait to maintain precise period
            let elapsed = interval.elapsed();
            if elapsed < period {
                std::thread::sleep(period - elapsed);
            }
            interval = std::time::Instant::now();
        }
        Ok(())
    })
    .await?
}

/* ── Diagnostic RX task: handles OBD-II requests ────────────────── */
async fn diagnostic_task(iface: String, state: Arc<EcuState>) -> Result<()> {
    task::spawn_blocking(move || -> Result<()> {
        let rx_sock = CanSocket::open(&iface)?;
        let tx_sock = CanSocket::open(&iface)?;

        // Set 200 ms read timeout so we can poll `running`
        rx_sock.set_read_timeout(Duration::from_millis(200))?;

        // Set a hardware-level filter: only receive 0x7DF
        use socketcan::CanFilter;
        rx_sock.set_filters(&[CanFilter::new(0x7DF, 0x7FF)])?;

        while state.running.load(Ordering::Relaxed) {
            let frame = match rx_sock.read_frame() {
                Ok(f) => f,
                Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => continue,
                Err(e) => return Err(e.into()),
            };

            // Expect: [PCI=0x02][service=0x01][pid]...
            let data = frame.data();
            if data.len() < 3 || data[1] != 0x01 { continue; }

            let pid = data[2];
            println!("[diag] Request PID=0x{pid:02X}");

            if let Some(resp) = build_obd_response(pid, &state) {
                tx_sock.write_frame(&resp)?;
                println!("[diag] Responded on 0x7E8");
            }
        }
        Ok(())
    })
    .await?
}

/* ── Entry point ─────────────────────────────────────────────────── */
#[tokio::main]
async fn main() -> Result<()> {
    let iface = std::env::args()
        .nth(1)
        .unwrap_or_else(|| "vcan0".into());

    println!("[ecu_sim] Starting on {iface}");

    let state = EcuState::new(800, 20);

    let bc_state  = Arc::clone(&state);
    let bc_iface  = iface.clone();
    let diag_state = Arc::clone(&state);
    let diag_iface = iface.clone();

    tokio::select! {
        res = broadcast_task(bc_iface, bc_state)   => res?,
        res = diagnostic_task(diag_iface, diag_state) => res?,
        _ = signal::ctrl_c() => {
            println!("\n[ecu_sim] Ctrl-C — shutting down.");
            state.running.store(false, Ordering::Relaxed);
        }
    }

    println!("[ecu_sim] Stopped.");
    Ok(())
}
```

---

## 6. Rust — CAN Frame Recorder and Replayer

Replay tools are indispensable for regression testing: record a real bus session once, then replay it deterministically against new firmware. This implements both sides with accurate inter-frame timing.

```rust
// src/bin/recorder.rs  (cargo run --bin recorder -- vcan0 session.log)
// src/bin/replayer.rs  (cargo run --bin replayer -- vcan0 session.log)

// ── recorder ─────────────────────────────────────────────────────
use anyhow::Result;
use socketcan::{CanSocket, Frame, Socket};
use std::{
    fs::File,
    io::{BufWriter, Write},
    time::{Instant, SystemTime, UNIX_EPOCH},
};

fn record(iface: &str, path: &str) -> Result<()> {
    let sock = CanSocket::open(iface)?;
    sock.set_read_timeout(std::time::Duration::from_millis(500))?;

    let mut writer = BufWriter::new(File::create(path)?);
    let start = Instant::now();

    println!("[recorder] Writing to {path} — Ctrl-C to stop");
    loop {
        match sock.read_frame() {
            Ok(frame) => {
                let ts_us = start.elapsed().as_micros() as u64;
                let id    = frame.raw_id();
                let data  = frame.data();
                // Format: <timestamp_us> <can_id_hex> <data_hex...>
                write!(&mut writer, "{ts_us} {id:08X}")?;
                for b in data { write!(&mut writer, " {b:02X}")?; }
                writeln!(&mut writer)?;
            }
            Err(e) if e.kind() == std::io::ErrorKind::WouldBlock => {}
            Err(e) => eprintln!("[recorder] {e}"),
        }
    }
}

// ── replayer ─────────────────────────────────────────────────────
use std::{
    io::{BufRead, BufReader},
    thread,
};

fn replay(iface: &str, path: &str) -> Result<()> {
    let sock   = CanSocket::open(iface)?;
    let reader = BufReader::new(File::open(path)?);
    let mut lines = reader.lines().peekable();

    let mut prev_ts_us: u64 = 0;
    let start = Instant::now();

    println!("[replayer] Replaying {path} on {iface}");

    for line in lines {
        let line = line?;
        let mut parts = line.split_whitespace();

        let ts_us: u64 = parts.next().unwrap_or("0").parse()?;
        let id: u32    = u32::from_str_radix(parts.next().unwrap_or("0"), 16)?;
        let data: Vec<u8> = parts
            .map(|h| u8::from_str_radix(h, 16).unwrap_or(0))
            .collect();

        // Wait until it's time to send this frame
        let target = std::time::Duration::from_micros(ts_us);
        let elapsed = start.elapsed();
        if target > elapsed {
            thread::sleep(target - elapsed);
        }

        let frame = socketcan::CanFrame::new(
            socketcan::Id::from(socketcan::StandardId::new(id as u16)
                .unwrap_or(socketcan::StandardId::MAX)),
            &data,
        )?;
        sock.write_frame(&frame)?;
        println!("[replayer] {ts_us:>10}µs  ID=0x{id:03X}  {:?}", data);
    }

    println!("[replayer] Done.");
    Ok(())
}
```

---

## 7. CANoe and Busmaster — Tool-Based Simulation

These tools add a graphical layer on top of the same underlying concepts.

**CANoe (Vector Informatik)** uses CAPL (CAN Access Programming Language — a C-like scripting language) to define node behaviour. A CAPL network node is effectively the same as the C ECU simulator above, but it runs inside CANoe's simulation environment with built-in timing, logging, and `.dbc` signal decoding:

```capl
// CAPL script: ecu_node.can (runs inside CANoe's simulation)
// Broadcasts engine RPM on message 0x100 every 100 ms

variables {
    message 0x100 msg_rpm;     // defined in the attached .dbc file
    msTimer t_broadcast;
    int rpm = 800;
}

on start {
    setTimer(t_broadcast, 100);   // fire every 100 ms
}

on timer t_broadcast {
    msg_rpm.EngineRPM = rpm;      // signal name from .dbc
    output(msg_rpm);
    if (rpm < 3000) rpm += 10;    // ramp up
    setTimer(t_broadcast, 100);
}

on message 0x7DF {               // OBD-II request received
    if (this.byte(2) == 0x0C) {  // PID: Engine RPM
        message 0x7E8 resp;
        int scaled = rpm * 4;
        resp.byte(0) = 0x04;
        resp.byte(1) = 0x41;
        resp.byte(2) = 0x0C;
        resp.byte(3) = (scaled >> 8) & 0xFF;
        resp.byte(4) =  scaled       & 0xFF;
        output(resp);
    }
}
```

**Busmaster** (open-source, Windows) takes a different approach: you write signal handlers in C++ DLLs that it hot-loads, or you use its built-in message scheduler with a `.dbf` signal database. It also supports virtual SocketCAN interfaces on Windows via a PEAK or Kvaser driver in loopback mode.

---

## 8. CAN FD on Virtual Interfaces

Classical CAN allows 8 bytes of payload. CAN FD raises that to 64 bytes. Linux `vcan` supports FD natively:

```bash
# Create a vcan interface with FD enabled
sudo ip link add dev vcanfd0 type vcan
sudo ip link set vcanfd0 mtu 72    # 72 = canfd_frame size
sudo ip link set up vcanfd0
```

```c
// canfd_send.c — sending a CAN FD frame from C
#include <linux/can.h>
#include <linux/can/raw.h>
// ... (socket setup identical to classical CAN) ...

// Enable FD on the socket
int enable_fd = 1;
setsockopt(sock, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd, sizeof(enable_fd));

struct canfd_frame fdf = {0};
fdf.can_id = 0x123;
fdf.len    = 64;             /* up to 64 bytes                       */
fdf.flags  = CANFD_BRS;     /* bit rate switching — faster data phase */
memset(fdf.data, 0xAB, 64);

write(sock, &fdf, sizeof(fdf));
```

```rust
// Rust: CAN FD with socketcan crate
use socketcan::{CanFdSocket, CanFdFrame, FdFrame, Socket};

let sock = CanFdSocket::open("vcanfd0")?;
let mut data = [0u8; 64];
data.fill(0xAB);
let frame = CanFdFrame::new(0x123.into(), &data)?;
sock.write_frame(&frame)?;
```

---

## Summary

| Concern | Tool / API | Key Detail |
|---|---|---|
| Virtual bus setup | `ip link add type vcan` | Kernel loopback, zero hardware needed |
| Low-level C/C++ I/O | `socket(PF_CAN)` + `read/write` | Identical API to physical interfaces |
| Signal decoding | DBC map in code / CANoe | `factor × raw + offset` per signal |
| Async Rust | `socketcan` + `tokio::spawn_blocking` | CanSocket is blocking; offload to thread pool |
| Gateway testing | Two vcan interfaces + routing table | Frame transform functions per CAN-ID |
| Regression testing | Record/replay with `µs` timestamps | Deterministic timing from saved logs |
| Professional simulation | CANoe (CAPL) / Busmaster (DLL) | GUI, `.dbc` integration, AUTOSAR support |
| CAN FD | `canfd_frame`, `mtu 72`, `CANFD_BRS` | Same vcan driver, just wider frame struct |

The Linux `vcan` + SocketCAN stack is the most versatile foundation because the **exact same application code** runs unmodified against a real `can0` hardware interface simply by changing the interface name string — making virtual-to-physical migration a one-line change.