# 14. CANopen TIME Object & Distributed Timestamps

---

## Table of Contents

1. [Overview](#1-overview)
2. [TIME COB-ID (0x100)](#2-time-cob-id-0x100)
3. [48-bit Timestamp Encoding](#3-48-bit-timestamp-encoding)
4. [TIME Producer Implementation](#4-time-producer-implementation)
5. [TIME Consumer Implementation](#5-time-consumer-implementation)
6. [Object Dictionary Entries](#6-object-dictionary-entries)
7. [Accuracy Considerations](#7-accuracy-considerations)
8. [Correlation with External RTC](#8-correlation-with-external-rtc)
9. [Correlation with PTP / IEEE-1588](#9-correlation-with-ptp--ieee-1588)
10. [Advanced Topics & Edge Cases](#10-advanced-topics--edge-cases)
11. [Summary](#11-summary)

---

## 1. Overview

The CANopen **TIME object** provides a mechanism for broadcasting a network-wide timestamp
so that all nodes on the CAN bus can synchronize to a common time base. It is one of the
predefined communication objects in the CANopen standard (CiA 301) and operates alongside
the SYNC object to support time-coherent, coordinated behaviour across distributed devices
such as motion controllers, I/O modules, sensors, and HMI panels.

```
 CANopen Network – TIME Distribution
 =====================================

  +-----------+         CAN Bus             +-----------+
  |  TIME     |  ---[TIME 0x100]--------->  | Consumer  |
  | Producer  |  ---[TIME 0x100]--------->  | Node B    |
  | (Master)  |  ---[TIME 0x100]--------->  | Node C    |
  +-----------+                             | Node D    |
       |                                    +-----------+
  [External]
   RTC / PTP
```

### Key Characteristics

- **Unidirectional**: TIME messages flow from exactly **one producer** to N consumers.
- **Non-confirmed**: Like SYNC, TIME is a broadcast; no handshake or acknowledgment occurs.
- **Fixed CAN frame**: Always exactly **6 data bytes** (48 bits).
- **Default COB-ID**: `0x100` (highest-priority non-NMT message class).
- **Epoch**: Days are counted from **1 January 1984**, milliseconds within the current day.

---

## 2. TIME COB-ID (0x100)

### CAN Frame Structure

```
 CAN Frame – TIME Object
 ========================
 +--------+---------+-----+---------------------------------------+
 | Field  | Bits    | Hex | Description                           |
 +--------+---------+-----+---------------------------------------+
 | SOF    |  1      |  —  | Start of Frame                        |
 | ID     | 11      | 100 | COB-ID = 0x100  (TIME)                |
 | RTR    |  1      |  0  | Remote Transmission Request = 0       |
 | IDE    |  1      |  0  | Standard 11-bit identifier            |
 | DLC    |  4      |  6  | Data Length Code = 6 bytes            |
 | DATA   | 48      |  —  | 6-byte timestamp payload (see below)  |
 | CRC    | 15+1    |  —  | CRC sequence + delimiter              |
 | ACK    |  2      |  —  | Acknowledge slot + delimiter          |
 | EOF    |  7      |  —  | End of Frame                          |
 +--------+---------+-----+---------------------------------------+

 Arbitration ID (11 bits):
  Bit 10 9 8 7 6 5 4 3 2 1 0
       0 0 1 0 0 0 0 0 0 0 0   =  0x100
```

### COB-ID in the Object Dictionary

The TIME COB-ID is stored at **Object 0x1012** (sub-index 0x00) in the Object Dictionary.

```
 Object 0x1012 – Bit Layout
 ===========================

  31   30   29   28 ... 11  10 ... 0
  +----+----+----+---------+---------+
  | V  | P  | R  | unused  |  COB-ID |
  +----+----+----+---------+---------+

  Bit 31 (V)  = 1 → This node CONSUMES TIME objects
  Bit 30 (P)  = 1 → This node PRODUCES TIME objects
  Bit 29 (R)  = 0 → Standard 11-bit CAN ID (1 = 29-bit extended)
  Bits 10..0  = CAN identifier (default: 0x100)
```

> **Rule**: Only **one** node on the network must have bit 30 set. Multiple consumers
> (bit 31 set) are allowed and expected.

---

## 3. 48-bit Timestamp Encoding

The 6-byte TIME payload is split into two fields:

```
 TIME Object Payload (6 bytes, little-endian)
 =============================================

  Byte:  [0]   [1]   [2]   [3]     [4]   [5]
         +-----+-----+-----+-----+ +-----+-----+
         |          ms (32 bit)  | | days(16b) |
         +-----+-----+-----+-----+ +-----+-----+
          LSB                 MSB   LSB    MSB

  ms   (bytes 0-3) : Milliseconds since midnight (00:00:00.000)
                     Range: 0 … 86,399,999  (fits in 32 bits)
  days (bytes 4-5) : Days since 1 January 1984
                     Range: 0 … 65,535  (last valid: ~year 2163)
```

### Epoch Reference

```
 CANopen Epoch: 1 January 1984 = Day 0
 ======================================

  Unix Epoch:     1 Jan 1970
  CANopen Epoch:  1 Jan 1984
                  |<--- 5114 days offset ---|
  1970-01-01      1984-01-01                 today
  |               |
  +---------------+------- ... -------+------>  time
       5114 days          ~14975 days
                          (as of mid-2025)

  Conversion:
    CANopen days = (Unix_timestamp_seconds / 86400) - 5114
    Unix seconds = (CANopen_days + 5114) * 86400 + (CANopen_ms / 1000)
```

### Encoding Example

Let us encode `2025-06-15 14:30:45.500 UTC`:

```
 Step 1: Days since 1984-01-01
   Days from 1970 to 2025-06-15 = 20255  (Unix day number)
   CANopen days = 20255 - 5114 = 15141 = 0x3B25

 Step 2: ms since midnight
   14h * 3600000 = 50,400,000
   30m *   60000 =  1,800,000
   45s *    1000 =     45,000
   500ms           =        500
   Total ms = 52,245,500 = 0x031D_B57C

 Payload bytes (little-endian):
   Byte 0: 0x7C   (ms bits  7.. 0)
   Byte 1: 0xB5   (ms bits 15.. 8)
   Byte 2: 0x1D   (ms bits 23..16)
   Byte 3: 0x03   (ms bits 31..24)
   Byte 4: 0x25   (days bits  7..0)
   Byte 5: 0x3B   (days bits 15..8)

 CAN Frame:  ID=0x100  DLC=6  Data: 7C B5 1D 03 25 3B
```

### C Structure Definition

```c
#include <stdint.h>

/* CANopen TIME object payload – packed, little-endian */
#pragma pack(push, 1)
typedef struct {
    uint32_t ms_since_midnight;   /* Milliseconds since 00:00:00 UTC, 32-bit LE */
    uint16_t days_since_1984;     /* Days since 1984-01-01,           16-bit LE */
} canopen_time_t;
#pragma pack(pop)

/* Compile-time size guard */
_Static_assert(sizeof(canopen_time_t) == 6, "TIME object must be 6 bytes");
```

---

## 4. TIME Producer Implementation

The TIME producer node periodically reads a reference clock and transmits the TIME object.

### 4.1 Encoding Function

```c
/*
 * canopen_time_encode.c
 *
 * Encodes a UTC broken-down time into a 6-byte CANopen TIME payload.
 * Assumes the input is in UTC (no timezone or DST adjustments are applied
 * by CANopen itself — it simply carries wall-clock UTC time).
 */

#include <stdint.h>
#include <string.h>
#include <time.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t ms_since_midnight;
    uint16_t days_since_1984;
} canopen_time_t;
#pragma pack(pop)

/* CANopen epoch: 1984-01-01 as Unix timestamp (seconds) */
#define CANOPEN_EPOCH_UNIX_SEC   441763200UL   /* = 5114 * 86400 */
#define CANOPEN_EPOCH_DAYS_UNIX  5114U

/**
 * encode_canopen_time() – convert a Unix timestamp + milliseconds into
 * a CANopen TIME payload.
 *
 * @param unix_sec   Seconds since Unix epoch (1970-01-01 00:00:00 UTC)
 * @param sub_sec_ms Sub-second milliseconds [0..999]
 * @param out        Output TIME payload (6 bytes)
 * @return           0 on success, -1 if time precedes CANopen epoch
 */
int encode_canopen_time(uint64_t unix_sec, uint16_t sub_sec_ms,
                        canopen_time_t *out)
{
    if (unix_sec < CANOPEN_EPOCH_UNIX_SEC)
        return -1;  /* Before 1984-01-01 — invalid */

    /* Total seconds since CANopen epoch */
    uint64_t since_epoch_sec = unix_sec - CANOPEN_EPOCH_UNIX_SEC;

    /* Integer day number since 1984-01-01 */
    uint32_t day   = (uint32_t)(since_epoch_sec / 86400UL);
    uint32_t tod_s = (uint32_t)(since_epoch_sec % 86400UL); /* time-of-day */

    /* Milliseconds since midnight */
    uint32_t ms = tod_s * 1000U + sub_sec_ms;

    out->ms_since_midnight = ms;           /* compiler writes LE on LE host */
    out->days_since_1984   = (uint16_t)day;

    return 0;
}

/* ---------------------------------------------------------------------------
 * Example: produce a TIME frame using POSIX clock_gettime()
 * --------------------------------------------------------------------------- */
#include <stdio.h>

void time_producer_send_example(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    uint16_t sub_ms = (uint16_t)(ts.tv_nsec / 1000000UL); /* ns → ms */

    canopen_time_t payload;
    if (encode_canopen_time((uint64_t)ts.tv_sec, sub_ms, &payload) != 0) {
        fprintf(stderr, "TIME encode: system clock before CANopen epoch!\n");
        return;
    }

    /*
     * Transmit on CAN:
     *   COB-ID = 0x100,  DLC = 6,  Data = &payload
     *
     * (Replace with your CAN driver call, e.g. SocketCAN or hardware FIFO)
     */
    uint8_t raw[6];
    memcpy(raw, &payload, 6);

    printf("TIME frame  COB-ID=0x100  DLC=6  Data: "
           "%02X %02X %02X %02X %02X %02X\n",
           raw[0], raw[1], raw[2], raw[3], raw[4], raw[5]);
}
```

### 4.2 Producer Class (C++)

```cpp
/*
 * TimeProducer.hpp – CANopen TIME object producer (C++17)
 *
 * Responsibilities:
 *   - Periodically sample an injected clock source
 *   - Encode into 6-byte CANopen TIME payload
 *   - Transmit via an injected CAN send callback
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <chrono>

namespace canopen {

#pragma pack(push, 1)
struct TimePayload {
    uint32_t ms_since_midnight{0};
    uint16_t days_since_1984{0};
};
#pragma pack(pop)
static_assert(sizeof(TimePayload) == 6, "TimePayload must be 6 bytes");

/* Callback type: (cob_id, data_ptr, dlc) → void */
using CanSendFn = std::function<void(uint32_t, const uint8_t*, uint8_t)>;

class TimeProducer {
public:
    static constexpr uint32_t TIME_COB_ID         = 0x100U;
    static constexpr uint64_t CANOPEN_EPOCH_UNIX  = 441763200ULL; /* 1984-01-01 UTC */
    static constexpr uint32_t MS_PER_DAY          = 86400000UL;

    explicit TimeProducer(CanSendFn send_fn)
        : send_(std::move(send_fn)) {}

    /**
     * Call periodically (e.g. every 1 second or on SYNC) to broadcast TIME.
     * Uses std::chrono::system_clock — replace with hardware RTC if needed.
     */
    void produce()
    {
        using namespace std::chrono;

        /* Current time as duration since Unix epoch */
        auto now      = system_clock::now().time_since_epoch();
        auto now_ms   = duration_cast<milliseconds>(now).count();

        encode_and_send(static_cast<uint64_t>(now_ms));
    }

    /** Inject an explicit Unix timestamp in milliseconds (e.g. from PTP). */
    void produce_from_unix_ms(uint64_t unix_ms)
    {
        encode_and_send(unix_ms);
    }

private:
    void encode_and_send(uint64_t unix_ms)
    {
        /* Convert to ms since CANopen epoch */
        constexpr uint64_t epoch_ms = CANOPEN_EPOCH_UNIX * 1000ULL;
        if (unix_ms < epoch_ms) return;  /* Sanity check */

        uint64_t since_epoch_ms = unix_ms - epoch_ms;

        TimePayload p;
        p.days_since_1984   = static_cast<uint16_t>(since_epoch_ms / MS_PER_DAY);
        p.ms_since_midnight = static_cast<uint32_t>(since_epoch_ms % MS_PER_DAY);

        uint8_t buf[6];
        std::memcpy(buf, &p, 6);
        send_(TIME_COB_ID, buf, 6);
    }

    CanSendFn send_;
};

} // namespace canopen
```

---

## 5. TIME Consumer Implementation

A consumer receives a TIME frame, decodes the payload, and applies it to its local time base.

### 5.1 Decoding Function (C)

```c
/*
 * canopen_time_decode.c
 *
 * Decodes a 6-byte CANopen TIME payload into a Unix timestamp.
 */

#include <stdint.h>
#include <string.h>

#pragma pack(push, 1)
typedef struct {
    uint32_t ms_since_midnight;
    uint16_t days_since_1984;
} canopen_time_t;
#pragma pack(pop)

#define CANOPEN_EPOCH_UNIX_SEC  441763200UL

/**
 * decode_canopen_time() – extract Unix seconds + sub-second ms.
 *
 * @param payload     Pointer to 6-byte TIME frame data
 * @param unix_sec    OUT: seconds since 1970-01-01
 * @param sub_sec_ms  OUT: sub-second portion [0..999]
 */
void decode_canopen_time(const uint8_t *payload,
                         uint64_t *unix_sec,
                         uint16_t *sub_sec_ms)
{
    canopen_time_t t;
    memcpy(&t, payload, 6);  /* Safe byte-copy, handles alignment */

    uint64_t since_epoch_ms =
        (uint64_t)t.days_since_1984 * 86400000ULL +
        (uint64_t)t.ms_since_midnight;

    uint64_t unix_ms = since_epoch_ms + (uint64_t)CANOPEN_EPOCH_UNIX_SEC * 1000ULL;

    *unix_sec    = unix_ms / 1000ULL;
    *sub_sec_ms  = (uint16_t)(unix_ms % 1000ULL);
}

/* ---------------------------------------------------------------------------
 * CAN receive callback – called by the driver on every received frame
 * --------------------------------------------------------------------------- */
#include <stdio.h>
#include <time.h>

static volatile uint64_t g_last_time_unix_ms = 0;  /* last received TIME */

void can_receive_callback(uint32_t cob_id, const uint8_t *data, uint8_t dlc)
{
    if (cob_id != 0x100U || dlc != 6)
        return;  /* Not a TIME object */

    uint64_t unix_sec;
    uint16_t sub_ms;
    decode_canopen_time(data, &unix_sec, &sub_ms);

    g_last_time_unix_ms = unix_sec * 1000ULL + sub_ms;

    /* Optional: apply to system clock (requires root / CAP_SYS_TIME on Linux) */
    struct timespec ts = {
        .tv_sec  = (time_t)unix_sec,
        .tv_nsec = (long)sub_ms * 1000000L
    };
    clock_settime(CLOCK_REALTIME, &ts);  /* Best-effort; may fail */

    printf("CANopen TIME received: Unix=%llu.%03u\n",
           (unsigned long long)unix_sec, sub_ms);
}
```

### 5.2 Consumer Class (C++) with Drift Tracking

```cpp
/*
 * TimeConsumer.hpp – CANopen TIME consumer with local drift tracking (C++17)
 */

#pragma once
#include <cstdint>
#include <cstring>
#include <chrono>
#include <optional>
#include <functional>

namespace canopen {

class TimeConsumer {
public:
    using ClockNow   = std::function<int64_t()>;  /* returns monotonic ms */
    using ApplyFn    = std::function<void(int64_t unix_ms)>;

    static constexpr uint32_t TIME_COB_ID        = 0x100U;
    static constexpr uint64_t CANOPEN_EPOCH_MS   = 441763200ULL * 1000ULL;
    static constexpr uint32_t MS_PER_DAY         = 86400000UL;

    TimeConsumer(ClockNow clock, ApplyFn apply)
        : clock_(std::move(clock)), apply_(std::move(apply)) {}

    /**
     * Call from CAN receive handler.
     * @return Decoded Unix timestamp in ms, or -1 if frame rejected.
     */
    int64_t on_time_frame(uint32_t cob_id, const uint8_t *data, uint8_t dlc)
    {
        if (cob_id != TIME_COB_ID || dlc != 6)
            return -1;

        /* --- Decode --- */
        uint32_t ms_day;
        uint16_t days;
        std::memcpy(&ms_day, data + 0, 4);
        std::memcpy(&days,   data + 4, 2);

        int64_t network_unix_ms =
            static_cast<int64_t>(CANOPEN_EPOCH_MS)
          + static_cast<int64_t>(days)   * 86400000LL
          + static_cast<int64_t>(ms_day);

        /* --- Drift measurement --- */
        int64_t local_mono = clock_();
        if (last_mono_.has_value() && last_network_ms_.has_value()) {
            int64_t delta_mono    = local_mono            - *last_mono_;
            int64_t delta_network = network_unix_ms       - *last_network_ms_;
            drift_ppm_ = drift_filter(
                static_cast<double>(delta_network - delta_mono)
                / static_cast<double>(delta_mono) * 1e6);
        }
        last_mono_       = local_mono;
        last_network_ms_ = network_unix_ms;

        /* --- Apply --- */
        apply_(network_unix_ms);
        return network_unix_ms;
    }

    /** Estimated drift of local oscillator relative to network time (ppm). */
    double drift_ppm() const { return drift_ppm_; }

private:
    /* Simple single-pole IIR low-pass filter for drift */
    double drift_filter(double new_sample)
    {
        constexpr double alpha = 0.1;
        drift_ppm_ = (1.0 - alpha) * drift_ppm_ + alpha * new_sample;
        return drift_ppm_;
    }

    ClockNow                  clock_;
    ApplyFn                   apply_;
    std::optional<int64_t>    last_mono_;
    std::optional<int64_t>    last_network_ms_;
    double                    drift_ppm_{0.0};
};

} // namespace canopen
```

---

## 6. Object Dictionary Entries

The TIME object uses two mandatory OD entries:

```
 Object Dictionary – TIME-Related Entries
 =========================================

  Index  Sub  Name                     Type     Access  Description
  -----  ---  -----------------------  -------  ------  -------------------------
  1012h  00   COB-ID TIME Message      UINT32   RW      Bit 31: consumer enable
                                                        Bit 30: producer enable
                                                        Bit 29: frame type (0=11b)
                                                        Bits 10-0: CAN ID
  1013h  00   High Resolution Timestamp UINT32  RW      Sub-microsecond extension
                                                        (optional, vendor-specific)
```

### Example: Configuring via SDO (C)

```c
/*
 * Configure a node as TIME consumer via SDO write to Object 0x1012.
 *
 * Bit layout for consumer-only:
 *   Bit 31 = 1  (consume)
 *   Bit 30 = 0  (do not produce)
 *   Bit 29 = 0  (standard 11-bit frame)
 *   Bits 10..0 = 0x100
 *
 *   Value = 0x80000100
 */

#include <stdint.h>

/* Bitmask constants for OD 0x1012 */
#define OD1012_CONSUMER_FLAG   (1UL << 31)
#define OD1012_PRODUCER_FLAG   (1UL << 30)
#define OD1012_EXTENDED_FLAG   (1UL << 29)
#define OD1012_COBID_MASK      (0x7FFUL)

typedef struct {
    uint32_t value;
} od_1012_t;

od_1012_t od_1012_build_consumer(uint16_t cob_id)
{
    od_1012_t r;
    r.value = OD1012_CONSUMER_FLAG | (cob_id & OD1012_COBID_MASK);
    return r;
}

od_1012_t od_1012_build_producer(uint16_t cob_id)
{
    od_1012_t r;
    r.value = OD1012_PRODUCER_FLAG | (cob_id & OD1012_COBID_MASK);
    return r;
}

/* Decode helpers */
int      od_1012_is_consumer(od_1012_t v) { return (v.value >> 31) & 1; }
int      od_1012_is_producer(od_1012_t v) { return (v.value >> 30) & 1; }
int      od_1012_is_extended(od_1012_t v) { return (v.value >> 29) & 1; }
uint16_t od_1012_cob_id(od_1012_t v)     { return (uint16_t)(v.value & OD1012_COBID_MASK); }
```

---

## 7. Accuracy Considerations

The achievable synchronization accuracy over CANopen is constrained by several factors:

```
 Error Budget – CANopen TIME Accuracy
 ======================================

  Source of error               Typical value   Notes
  ----------------------------  --------------  ---------------------------
  CAN bit-time jitter           ± 0..2 bit times  At 1 Mbit/s = ±2 µs
  CAN bus propagation delay     1..5 µs/20m     Negligible on short buses
  Software ISR latency          10..500 µs      OS scheduler, IRQ priority
  Application-layer encoding    0..1 ms         Quantization to 1 ms
  Reference clock accuracy      ±1..50 ppm      Crystal oscillator quality
  CAN arbitration delay         0..130 µs       Frame wait at 1 Mbit/s
  ---------------------------------+------------------------------------
  TOTAL (worst-case, no tuning)   ~1..2 ms      Dominated by ISR latency
  TOTAL (hardware timestamp)      ~10..50 µs    With CAN controller HW TS

  Improvement Strategies:
  +-----------------------+------------------------------------------+
  | Strategy              | Effect                                   |
  +-----------------------+------------------------------------------+
  | Hardware timestamping | Eliminates ISR latency (best approach)   |
  | High-priority IRQ     | Reduces software latency                 |
  | RTOS real-time task   | Bounded latency for timestamp capture    |
  | PTP as reference      | Improves source accuracy to sub-µs       |
  | Frequent TIME msgs    | Reduces drift accumulation between msgs  |
  +-----------------------+------------------------------------------+
```

### Hardware Timestamp Approach (C, SocketCAN)

```c
/*
 * Receive TIME with hardware CAN timestamp using SocketCAN SO_TIMESTAMPING.
 * The kernel attaches a hardware-captured timestamp to each received frame,
 * bypassing ISR latency in the measurement.
 */

#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/net_tstamp.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>

int open_can_socket_with_hwts(const char *iface)
{
    int sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (sock < 0) return -1;

    struct ifreq ifr;
    strncpy(ifr.ifr_name, iface, IFNAMSIZ - 1);
    ioctl(sock, SIOCGIFINDEX, &ifr);

    struct sockaddr_can addr = {
        .can_family  = AF_CAN,
        .can_ifindex = ifr.ifr_ifindex
    };
    bind(sock, (struct sockaddr *)&addr, sizeof(addr));

    /* Request hardware receive timestamps */
    int ts_flags = SOF_TIMESTAMPING_RX_HARDWARE
                 | SOF_TIMESTAMPING_RAW_HARDWARE;
    setsockopt(sock, SOL_SOCKET, SO_TIMESTAMPING, &ts_flags, sizeof(ts_flags));

    return sock;
}

void receive_time_with_hwts(int sock)
{
    struct can_frame frame;
    struct msghdr   msg    = {0};
    struct iovec    iov    = { .iov_base = &frame, .iov_len = sizeof(frame) };
    char            ctrl[256];

    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = ctrl;
    msg.msg_controllen = sizeof(ctrl);

    ssize_t n = recvmsg(sock, &msg, 0);
    if (n < 0) return;

    if (frame.can_id != 0x100 || frame.can_dlc != 6)
        return;  /* Not a TIME object */

    /* Extract hardware timestamp from ancillary data */
    struct timespec hw_ts = {0};
    for (struct cmsghdr *cm = CMSG_FIRSTHDR(&msg); cm;
         cm = CMSG_NXTHDR(&msg, cm))
    {
        if (cm->cmsg_level == SOL_SOCKET &&
            cm->cmsg_type  == SO_TIMESTAMPING)
        {
            struct timespec *ts = (struct timespec *)CMSG_DATA(cm);
            hw_ts = ts[2];  /* Index 2 = hardware timestamp */
        }
    }

    printf("HW RX timestamp: %ld.%09ld\n", hw_ts.tv_sec, hw_ts.tv_nsec);

    /* Decode CANopen TIME payload */
    uint64_t unix_sec;
    uint16_t sub_ms;
    decode_canopen_time(frame.data, &unix_sec, &sub_ms);
    printf("CANopen TIME: %llu.%03u\n", (unsigned long long)unix_sec, sub_ms);
}
```

---

## 8. Correlation with External RTC

An embedded node must bridge between an on-chip Real-Time Clock (RTC) and the CANopen TIME format.

```
 RTC ↔ CANopen TIME Correlation
 =================================

  +---------------+      read()      +-------------+
  |  External RTC |  ------------->  |  CANopen    |
  |  (e.g. DS3231)|  <-- set_time()  |  TIME       |
  |  I2C / SPI    |                  |  Producer   |
  +---------------+                  +-------------+
         |                                 |
    ±2 ppm accuracy                  CAN Bus 0x100
    battery-backed                         |
                                    +------+------+
                                    | Consumer A  |
                                    | Consumer B  |
                                    +-------------+

  Time Authority Chain:
    GPS / NTP  →  RTC (battery-backed)  →  CANopen TIME  →  Node local clock
      best            good                    ±1 ms             adjusted
```

### RTC Synchronisation Example (C)

```c
/*
 * rtc_to_canopen.c
 *
 * Reads time from a DS3231 RTC over I2C (simplified), encodes it
 * as a CANopen TIME payload, and transmits on the CAN bus.
 *
 * DS3231 BCD register layout (relevant subset):
 *   0x00 = seconds (BCD)
 *   0x01 = minutes (BCD)
 *   0x02 = hours   (BCD, 24-hour mode)
 *   0x03 = day of week (1=Sun)
 *   0x04 = day of month (BCD)
 *   0x05 = month (BCD)
 *   0x06 = year  (BCD, 00-99, offset from 2000)
 */

#include <stdint.h>
#include <string.h>

/* BCD helpers */
static inline uint8_t bcd2bin(uint8_t bcd)
{
    return (bcd >> 4) * 10 + (bcd & 0x0F);
}

/* Simplified days-in-month (ignoring century leap-year correction) */
static const uint8_t days_in_month[12] =
    { 31,28,31,30,31,30,31,31,30,31,30,31 };

static int is_leap(int y)
{
    return (y % 4 == 0 && (y % 100 != 0 || y % 400 == 0));
}

/**
 * ds3231_regs_to_canopen() – convert DS3231 register snapshot to TIME payload.
 *
 * @param regs  7 bytes from DS3231 registers 0x00..0x06
 * @param out   CANopen TIME payload
 */
void ds3231_regs_to_canopen(const uint8_t regs[7], canopen_time_t *out)
{
    uint8_t sec  = bcd2bin(regs[0] & 0x7F);
    uint8_t min  = bcd2bin(regs[1]);
    uint8_t hour = bcd2bin(regs[2] & 0x3F);
    uint8_t dom  = bcd2bin(regs[4]);          /* day of month, 1-based */
    uint8_t mon  = bcd2bin(regs[5] & 0x1F);   /* month, 1-based        */
    int     year = 2000 + bcd2bin(regs[6]);    /* full year             */

    /* --- ms since midnight --- */
    out->ms_since_midnight = ((uint32_t)hour * 3600U
                            + (uint32_t)min  *   60U
                            + (uint32_t)sec) * 1000U;
    /* sub-second = 0 (RTC resolution is 1 second) */

    /* --- days since 1984-01-01 --- */
    /* Count days from 1984 to (year-1) */
    uint32_t days = 0;
    for (int y = 1984; y < year; y++)
        days += is_leap(y) ? 366 : 365;

    /* Add days of elapsed months in current year */
    for (int m = 1; m < mon; m++) {
        days += days_in_month[m - 1];
        if (m == 2 && is_leap(year)) days++;
    }

    /* Add elapsed days in current month (dom is 1-based) */
    days += dom - 1;

    out->days_since_1984 = (uint16_t)days;
}
```

---

## 9. Correlation with PTP / IEEE-1588

PTP (Precision Time Protocol, IEEE-1588) provides sub-microsecond synchronization and
is increasingly used as the grandmaster clock for CANopen networks.

```
 PTP → CANopen TIME Bridge
 ==========================

  [Grandmaster Clock]
   GPS-disciplined PTP
   Accuracy: ±100 ns
        |
        |  Ethernet (PTP frames)
        v
  [PTP Boundary Clock]           [Other PTP nodes]
  or Transparent Clock
        |
        | CLOCK_TAI / CLOCK_REALTIME
        v
  +--------------------+
  |  PTP-to-CANopen   |   Every 1 second (or on SYNC)
  |  Bridge Node      |  ----[TIME 0x100]----------->  CAN Bus
  |  (Embedded MCU    |  ----[TIME 0x100]----------->  Nodes
  |   or Linux host)  |
  +--------------------+
        |
  PTP offset: ±1 µs
  CAN encode: ±1 ms (quantization)
  ISR latency: ±10-100 µs
  ---------------------------
  Consumer accuracy: ±1-2 ms

  Note: TAI vs UTC
  ----------------
  PTP uses TAI (International Atomic Time), not UTC.
  CANopen uses UTC.  The bridge MUST apply the leap-second offset:
    UTC = TAI - leap_seconds   (37 seconds as of 2025)
```

### PTP-to-CANopen Bridge (C, Linux PTP via clock_gettime)

```c
/*
 * ptp_to_canopen_bridge.c
 *
 * Reads time from a PTP-synchronized Linux clock (CLOCK_REALTIME or
 * a PHC device), converts to CANopen TIME, and sends on CAN.
 *
 * Compile:  gcc -O2 -o bridge ptp_to_canopen_bridge.c -lrt
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define CANOPEN_EPOCH_UNIX_SEC  441763200ULL
#define TAI_UTC_OFFSET_SECONDS  37   /* Update when a leap second is announced */

#pragma pack(push, 1)
typedef struct {
    uint32_t ms_since_midnight;
    uint16_t days_since_1984;
} canopen_time_t;
#pragma pack(pop)

/**
 * get_utc_from_tai_clock() – read TAI clock and convert to UTC.
 *
 * Linux CLOCK_TAI gives TAI time; subtract known leap-second offset.
 * If CLOCK_TAI is unavailable, fall back to CLOCK_REALTIME (which is UTC).
 */
static int get_utc_ms(uint64_t *out_unix_ms)
{
    struct timespec ts;

#ifdef CLOCK_TAI
    if (clock_gettime(CLOCK_TAI, &ts) == 0) {
        /* Convert TAI → UTC */
        ts.tv_sec -= TAI_UTC_OFFSET_SECONDS;
    } else
#endif
    {
        if (clock_gettime(CLOCK_REALTIME, &ts) != 0)
            return -1;
    }

    *out_unix_ms = (uint64_t)ts.tv_sec * 1000ULL
                 + (uint64_t)(ts.tv_nsec / 1000000UL);
    return 0;
}

static void encode_canopen_time(uint64_t unix_ms, uint8_t out[6])
{
    canopen_time_t t;
    uint64_t since_epoch_ms = unix_ms - CANOPEN_EPOCH_UNIX_SEC * 1000ULL;

    t.days_since_1984   = (uint16_t)(since_epoch_ms / 86400000ULL);
    t.ms_since_midnight = (uint32_t)(since_epoch_ms % 86400000ULL);
    memcpy(out, &t, 6);
}

/* Stub: replace with your CAN driver */
static void can_send(uint32_t cob_id, const uint8_t *data, uint8_t dlc)
{
    printf("CAN TX  ID=%03X  DLC=%u  Data:", cob_id, dlc);
    for (int i = 0; i < dlc; i++) printf(" %02X", data[i]);
    printf("\n");
}

int main(void)
{
    printf("PTP→CANopen TIME bridge started (TAI offset = %d s)\n",
           TAI_UTC_OFFSET_SECONDS);

    for (;;) {
        uint64_t unix_ms = 0;
        if (get_utc_ms(&unix_ms) == 0) {
            uint8_t payload[6];
            encode_canopen_time(unix_ms, payload);
            can_send(0x100, payload, 6);
        }

        /* Broadcast every 1 second */
        struct timespec delay = { .tv_sec = 1, .tv_nsec = 0 };
        nanosleep(&delay, NULL);
    }

    return 0;
}
```

### PTP Clock Quality Assessment (C++)

```cpp
/*
 * PtpQualityMonitor.hpp
 *
 * Monitors PTP clock quality and suppresses TIME broadcast if the
 * PTP grandmaster is lost or clock is not yet synchronized.
 */

#pragma once
#include <cstdint>
#include <ctime>
#include <cmath>
#include <fstream>
#include <string>

namespace canopen {

class PtpQualityMonitor {
public:
    /**
     * Reads the ptp4l offset from /var/run/ptp4l (or similar sysfs/shm source).
     * Returns true if PTP is synchronized within the given threshold (nanoseconds).
     */
    static bool is_synchronized(int64_t threshold_ns = 1000000LL /* 1 ms */)
    {
        int64_t offset_ns = read_ptp_offset_ns();
        return std::abs(offset_ns) < threshold_ns;
    }

    /** Read current UTC ms using CLOCK_REALTIME (post-PTP adjustment). */
    static uint64_t utc_ms_now()
    {
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return static_cast<uint64_t>(ts.tv_sec)  * 1000ULL
             + static_cast<uint64_t>(ts.tv_nsec) / 1000000ULL;
    }

private:
    /**
     * Read PTP servo offset from /run/timemaster/ptp4l.offset
     * (as written by timemaster / linuxptp tools).
     * Returns 0 if file not found or unparseable.
     */
    static int64_t read_ptp_offset_ns()
    {
        std::ifstream f("/run/timemaster/ptp4l.offset");
        if (!f.is_open()) return 0;
        int64_t offset = 0;
        f >> offset;
        return offset;
    }
};

} // namespace canopen
```

---

## 10. Advanced Topics & Edge Cases

### 10.1 Midnight Rollover

The `ms_since_midnight` counter resets to zero at each UTC midnight. A consumer must
detect this by tracking the `days_since_1984` increment.

```c
/*
 * Detect midnight rollover and validate TIME coherency.
 * Returns 1 if the new timestamp is monotonically consistent with the previous.
 */
int time_is_coherent(const canopen_time_t *prev, const canopen_time_t *curr)
{
    if (curr->days_since_1984 == prev->days_since_1984) {
        /* Same day: ms must be >= previous */
        return curr->ms_since_midnight >= prev->ms_since_midnight;
    }
    if (curr->days_since_1984 == prev->days_since_1984 + 1U) {
        /* Day incremented: normal midnight rollover */
        return 1;
    }
    /* Jump of more than one day: suspicious */
    return 0;
}
```

### 10.2 Relation to the SYNC Object

```
 SYNC + TIME Interaction Timeline
 ==================================

  TIME   |---[T]----------[T]----------[T]----------[T]-->
         |    |            |            |            |
  SYNC   |    |--[S][S][S]-+--[S][S][S]-+--[S][S][S]-+-->
         |    |            |            |            |
  PDO    |    +->[P][P][P] +->[P][P][P] +->[P][P][P]

  [T] = TIME object (0x100) — typically every 1 second
  [S] = SYNC object (0x80)  — typically every 1–10 ms
  [P] = TPDO data sampled/transmitted on SYNC reception

  Best practice:
    - Send TIME just before (or coincident with) a SYNC period boundary
    - This allows timestamped PDO data to be precisely correlated with
      the TIME base, enabling accurate event reconstruction.
```

### 10.3 TIME Object and NMT States

```
 NMT State Machine – TIME Object Behaviour
 ==========================================

                 [INITIALISATION]
                       |
                  reset / boot
                       |
                       v
               [PRE-OPERATIONAL]
                       |         TIME messages NOT processed
                       |         (consumer ignores 0x100 frames)
                    NMT Start
                       |
                       v
                 [OPERATIONAL]  <--------+
                       |                 |
             TIME msgs processed         |
             (consumer accepts 0x100)    |
                       |                 |
                    NMT Stop             | NMT Start
                       |                 |
                       v                 |
                  [STOPPED]  -----------+
                       |
             TIME msgs NOT processed

  Note: TIME PRODUCER behaviour is implementation-defined in pre-operational.
  CiA 301 recommends the producer transmit TIME regardless of its own NMT state,
  so that consumers entering OPERATIONAL immediately receive a valid timestamp.
```

### 10.4 Handling 32-bit ms Overflow

The `ms_since_midnight` field spans [0 … 86,399,999]. This fits comfortably in 27 bits,
so a 32-bit field will never overflow within a single day. However, the consumer should
validate the field:

```c
static int validate_time_payload(const canopen_time_t *t)
{
    /* ms value must be < 86,400,000 (24 * 3600 * 1000) */
    if (t->ms_since_midnight >= 86400000UL) {
        /* Malformed frame: reject */
        return -1;
    }
    return 0;
}
```

---

## 11. Summary

```
 CANopen TIME Object – Quick Reference Card
 ===========================================

  COB-ID:          0x100  (fixed default, configurable via OD 0x1012)
  DLC:             6 bytes  (always)
  Frame type:      Broadcast, unconfirmed, no response expected

  Payload layout:
  +---+---+---+---+---+---+
  | 0 | 1 | 2 | 3 | 4 | 5 |
  +---+---+---+---+---+---+
  |<--- ms (32-bit LE) --->|<-days->|
   ms since midnight (UTC)  days since 1984-01-01

  Epoch:           1 January 1984 = Day 0
  Unix offset:     5114 days = 441,763,200 seconds

  OD entries:
    0x1012[00]  COB-ID TIME  (Bit31=consumer, Bit30=producer)
    0x1013[00]  High-resolution timestamp extension (optional)

  Accuracy summary:
  +--------------------------+------------------+
  | Setup                    | Typical Accuracy |
  +--------------------------+------------------+
  | Basic software           | ±1-2 ms          |
  | High-priority ISR        | ±100-500 µs      |
  | CAN hardware timestamp   | ±10-50 µs        |
  | PTP grandmaster + bridge | ±50-200 µs       |
  +--------------------------+------------------+

  Key implementation rules:
    1. Exactly ONE producer per network (OD 0x1012 bit 30).
    2. Encode as UTC; handle TAI→UTC conversion when using PTP/IEEE-1588.
    3. Validate ms < 86,400,000 and detect midnight rollover via day counter.
    4. CANopen TIME resolution is 1 ms — sub-ms precision requires OD 0x1013
       or vendor-specific extensions.
    5. Use hardware CAN timestamps for precise correlation with SYNC/PDO data.
    6. In OPERATIONAL state, consumers apply received TIME immediately.
    7. Bridge nodes must maintain TAI–UTC leap-second offset (37 s as of 2025).
```

### Further Reading

- **CiA 301** – CANopen Application Layer and Communication Profile (normative specification)
- **CiA 302** – Additional Application Layer Functions (TIME consumer/producer profiles)
- **IEEE 1588-2019** – Precision Clock Synchronization Protocol for Networked Measurement and Control Systems
- **IETF RFC 5905** – Network Time Protocol Version 4 (NTPv4) — for reference clock comparison
- **Linux PTP Project** – `linuxptp.sourceforge.net` — open-source IEEE 1588 implementation for Linux

---

*Document: 14 – TIME Object & Distributed Timestamps | CANopen Programming Guide*
*Revision: 1.0 | Encoding: UTF-8*