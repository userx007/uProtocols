// diag_tester.cpp
// OBD-II tester + signal decoder against the C ECU simulator above.
// Build: g++ -std=c++20 -O2 -Wall -o diag_tester diag_tester.cpp -lpthread
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