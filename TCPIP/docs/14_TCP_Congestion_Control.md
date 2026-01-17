# TCP Congestion Control

## Overview

TCP Congestion Control is a set of algorithms designed to prevent network congestion by dynamically adjusting the rate at which data is transmitted. Unlike flow control (which prevents overwhelming the receiver), congestion control prevents overwhelming the network infrastructure between sender and receiver.

The core mechanism uses a **congestion window (cwnd)** that limits the amount of unacknowledged data in flight. The sender can transmit min(cwnd, receiver's advertised window) bytes without waiting for acknowledgments.

## Key Algorithms

### 1. **Slow Start**

Slow Start is the initial phase when a connection begins or after a timeout. Despite its name, it grows the congestion window exponentially:

- **cwnd** starts at a small value (typically 1-10 MSS - Maximum Segment Size)
- For each ACK received, cwnd increases by 1 MSS
- This doubles cwnd every RTT (Round Trip Time)
- Continues until cwnd reaches the **slow start threshold (ssthresh)** or packet loss occurs

**Purpose**: Quickly probe the network's capacity while starting conservatively.

### 2. **Congestion Avoidance**

Once cwnd reaches ssthresh, TCP enters Congestion Avoidance mode:

- cwnd grows linearly instead of exponentially
- Increases by approximately 1 MSS per RTT
- Formula: cwnd += MSS * (MSS / cwnd) for each ACK
- Continues until packet loss is detected

**Purpose**: Cautiously increase throughput while avoiding network saturation.

### 3. **Fast Retransmit**

Detects packet loss without waiting for a timeout:

- When sender receives 3 duplicate ACKs, it assumes packet loss
- Immediately retransmits the missing segment
- Doesn't wait for the retransmission timeout (RTO)

**Purpose**: Recover from loss quickly, reducing latency.

### 4. **Fast Recovery**

Works in conjunction with Fast Retransmit:

- After Fast Retransmit, instead of going back to Slow Start:
  - Set ssthresh = cwnd / 2
  - Set cwnd = ssthresh + 3 * MSS (accounting for the 3 dup ACKs)
- Inflate cwnd by 1 MSS for each additional duplicate ACK
- When new ACK arrives, set cwnd = ssthresh and enter Congestion Avoidance

**Purpose**: Maintain higher throughput during recovery since some data is still flowing.

## State Transitions

```
Connection Start → Slow Start
Slow Start (cwnd < ssthresh) → Congestion Avoidance (cwnd >= ssthresh)
Congestion Avoidance (3 dup ACKs) → Fast Retransmit → Fast Recovery
Fast Recovery (new ACK) → Congestion Avoidance
Any state (timeout) → Slow Start (ssthresh = cwnd/2, cwnd = 1 MSS)
```

## Code Examples

### C/C++ Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define INITIAL_CWND 10      // Initial congestion window (in MSS units)
#define MSS 1460             // Maximum Segment Size in bytes
#define INITIAL_SSTHRESH 65535

typedef enum {
    SLOW_START,
    CONGESTION_AVOIDANCE,
    FAST_RECOVERY
} TCPState;

typedef struct {
    uint32_t cwnd;           // Congestion window (in bytes)
    uint32_t ssthresh;       // Slow start threshold (in bytes)
    uint32_t dupacks;        // Duplicate ACK count
    TCPState state;
    uint32_t mss;            // Maximum segment size
    uint32_t last_ack;       // Last acknowledged sequence number
    uint32_t recover;        // Highest sequence sent when entering recovery
} TCPCongestionControl;

// Initialize congestion control state
void tcp_cc_init(TCPCongestionControl *cc) {
    cc->cwnd = INITIAL_CWND * MSS;
    cc->ssthresh = INITIAL_SSTHRESH;
    cc->dupacks = 0;
    cc->state = SLOW_START;
    cc->mss = MSS;
    cc->last_ack = 0;
    cc->recover = 0;
}

// Handle ACK reception
void tcp_cc_on_ack(TCPCongestionControl *cc, uint32_t ack_seq, bool is_dup) {
    if (is_dup) {
        cc->dupacks++;
        
        // Fast Retransmit threshold
        if (cc->dupacks == 3) {
            printf("Fast Retransmit triggered at cwnd=%u\n", cc->cwnd);
            
            // Enter Fast Recovery
            cc->ssthresh = cc->cwnd / 2;
            if (cc->ssthresh < 2 * cc->mss) {
                cc->ssthresh = 2 * cc->mss;
            }
            
            cc->cwnd = cc->ssthresh + 3 * cc->mss;
            cc->state = FAST_RECOVERY;
            cc->recover = ack_seq; // Mark recovery point
            
            printf("Entering Fast Recovery: ssthresh=%u, cwnd=%u\n", 
                   cc->ssthresh, cc->cwnd);
            
            // Retransmit the lost segment (application would do this)
        } else if (cc->state == FAST_RECOVERY && cc->dupacks > 3) {
            // Inflate cwnd for each additional duplicate ACK
            cc->cwnd += cc->mss;
            printf("Inflating cwnd in Fast Recovery: cwnd=%u\n", cc->cwnd);
        }
    } else {
        // New ACK received
        if (cc->state == FAST_RECOVERY) {
            // Exit Fast Recovery
            cc->cwnd = cc->ssthresh;
            cc->state = CONGESTION_AVOIDANCE;
            printf("Exiting Fast Recovery: cwnd=%u\n", cc->cwnd);
        } else if (cc->state == SLOW_START) {
            // Slow Start: increase cwnd by 1 MSS for each ACK
            cc->cwnd += cc->mss;
            
            // Transition to Congestion Avoidance when reaching ssthresh
            if (cc->cwnd >= cc->ssthresh) {
                cc->state = CONGESTION_AVOIDANCE;
                printf("Entering Congestion Avoidance at cwnd=%u\n", cc->cwnd);
            } else {
                printf("Slow Start: cwnd=%u\n", cc->cwnd);
            }
        } else if (cc->state == CONGESTION_AVOIDANCE) {
            // Congestion Avoidance: increase cwnd by ~1 MSS per RTT
            // Increment is MSS * MSS / cwnd per ACK
            cc->cwnd += (cc->mss * cc->mss) / cc->cwnd;
            printf("Congestion Avoidance: cwnd=%u\n", cc->cwnd);
        }
        
        cc->dupacks = 0;
        cc->last_ack = ack_seq;
    }
}

// Handle timeout event
void tcp_cc_on_timeout(TCPCongestionControl *cc) {
    printf("Timeout detected at cwnd=%u\n", cc->cwnd);
    
    // Timeout is more serious than duplicate ACKs
    cc->ssthresh = cc->cwnd / 2;
    if (cc->ssthresh < 2 * cc->mss) {
        cc->ssthresh = 2 * cc->mss;
    }
    
    cc->cwnd = cc->mss;  // Reset to 1 MSS
    cc->state = SLOW_START;
    cc->dupacks = 0;
    
    printf("Reset to Slow Start: ssthresh=%u, cwnd=%u\n", 
           cc->ssthresh, cc->cwnd);
}

// Get current sending window
uint32_t tcp_cc_get_cwnd(TCPCongestionControl *cc) {
    return cc->cwnd;
}

// Example usage
int main() {
    TCPCongestionControl cc;
    tcp_cc_init(&cc);
    
    printf("=== TCP Congestion Control Simulation ===\n");
    printf("Initial state: cwnd=%u, ssthresh=%u\n\n", cc.cwnd, cc.ssthresh);
    
    // Simulate Slow Start
    printf("--- Slow Start Phase ---\n");
    for (int i = 0; i < 5; i++) {
        tcp_cc_on_ack(&cc, 1000 * (i + 1), false);
    }
    
    // Simulate Congestion Avoidance
    printf("\n--- Congestion Avoidance Phase ---\n");
    for (int i = 0; i < 5; i++) {
        tcp_cc_on_ack(&cc, 6000 + 1000 * i, false);
    }
    
    // Simulate packet loss (3 duplicate ACKs)
    printf("\n--- Packet Loss Detection ---\n");
    tcp_cc_on_ack(&cc, 11000, true);
    tcp_cc_on_ack(&cc, 11000, true);
    tcp_cc_on_ack(&cc, 11000, true);
    
    // More duplicate ACKs during recovery
    printf("\n--- Fast Recovery Phase ---\n");
    tcp_cc_on_ack(&cc, 11000, true);
    tcp_cc_on_ack(&cc, 11000, true);
    
    // New ACK arrives
    printf("\n--- Recovery Complete ---\n");
    tcp_cc_on_ack(&cc, 12000, false);
    
    // Simulate timeout
    printf("\n--- Timeout Event ---\n");
    tcp_cc_on_timeout(&cc);
    
    return 0;
}
```

### C++ Implementation (Object-Oriented)

```cpp
#include <iostream>
#include <algorithm>
#include <cstdint>

class TCPCongestionControl {
public:
    enum class State {
        SLOW_START,
        CONGESTION_AVOIDANCE,
        FAST_RECOVERY
    };

private:
    static constexpr uint32_t INITIAL_CWND = 10;
    static constexpr uint32_t MSS = 1460;
    static constexpr uint32_t INITIAL_SSTHRESH = 65535;
    
    uint32_t cwnd_;          // Congestion window (bytes)
    uint32_t ssthresh_;      // Slow start threshold (bytes)
    uint32_t dupacks_;       // Duplicate ACK counter
    uint32_t mss_;           // Maximum segment size
    State state_;
    uint32_t recover_;       // Recovery sequence number

public:
    TCPCongestionControl() 
        : cwnd_(INITIAL_CWND * MSS),
          ssthresh_(INITIAL_SSTHRESH),
          dupacks_(0),
          mss_(MSS),
          state_(State::SLOW_START),
          recover_(0) {}

    void onAck(uint32_t ack_seq, bool is_duplicate) {
        if (is_duplicate) {
            handleDuplicateAck(ack_seq);
        } else {
            handleNewAck(ack_seq);
        }
    }

    void onTimeout() {
        std::cout << "Timeout: cwnd=" << cwnd_ << " -> ";
        
        ssthresh_ = std::max(cwnd_ / 2, 2 * mss_);
        cwnd_ = mss_;
        state_ = State::SLOW_START;
        dupacks_ = 0;
        
        std::cout << "cwnd=" << cwnd_ << ", ssthresh=" << ssthresh_ << std::endl;
    }

    uint32_t getCwnd() const { return cwnd_; }
    uint32_t getSsthresh() const { return ssthresh_; }
    State getState() const { return state_; }

private:
    void handleDuplicateAck(uint32_t ack_seq) {
        dupacks_++;
        
        if (dupacks_ == 3) {
            // Fast Retransmit
            std::cout << "Fast Retransmit at cwnd=" << cwnd_ << std::endl;
            
            ssthresh_ = std::max(cwnd_ / 2, 2 * mss_);
            cwnd_ = ssthresh_ + 3 * mss_;
            state_ = State::FAST_RECOVERY;
            recover_ = ack_seq;
            
            std::cout << "Fast Recovery: ssthresh=" << ssthresh_ 
                      << ", cwnd=" << cwnd_ << std::endl;
        } else if (state_ == State::FAST_RECOVERY && dupacks_ > 3) {
            // Inflate cwnd
            cwnd_ += mss_;
            std::cout << "Inflate cwnd: " << cwnd_ << std::endl;
        }
    }

    void handleNewAck(uint32_t ack_seq) {
        if (state_ == State::FAST_RECOVERY) {
            // Exit Fast Recovery
            cwnd_ = ssthresh_;
            state_ = State::CONGESTION_AVOIDANCE;
            std::cout << "Exit Fast Recovery: cwnd=" << cwnd_ << std::endl;
        } else if (state_ == State::SLOW_START) {
            // Exponential growth
            cwnd_ += mss_;
            
            if (cwnd_ >= ssthresh_) {
                state_ = State::CONGESTION_AVOIDANCE;
                std::cout << "-> Congestion Avoidance at cwnd=" << cwnd_ << std::endl;
            }
        } else if (state_ == State::CONGESTION_AVOIDANCE) {
            // Linear growth: ~1 MSS per RTT
            cwnd_ += (mss_ * mss_) / cwnd_;
        }
        
        dupacks_ = 0;
    }
};

int main() {
    TCPCongestionControl cc;
    
    std::cout << "=== TCP Congestion Control (C++) ===\n" << std::endl;
    
    // Simulate connection
    std::cout << "Slow Start:\n";
    for (int i = 0; i < 6; i++) {
        cc.onAck(1000 * (i + 1), false);
        std::cout << "  cwnd=" << cc.getCwnd() << std::endl;
    }
    
    std::cout << "\nCongestion Avoidance:\n";
    for (int i = 0; i < 4; i++) {
        cc.onAck(7000 + 1000 * i, false);
        std::cout << "  cwnd=" << cc.getCwnd() << std::endl;
    }
    
    std::cout << "\nPacket Loss (3 dup ACKs):\n";
    cc.onAck(11000, true);
    cc.onAck(11000, true);
    cc.onAck(11000, true);
    
    std::cout << "\nNew ACK:\n";
    cc.onAck(12000, false);
    
    std::cout << "\nTimeout:\n";
    cc.onTimeout();
    
    return 0;
}
```

### Rust Implementation

```rust
use std::cmp;

#[derive(Debug, Clone, Copy, PartialEq)]
enum CongestionState {
    SlowStart,
    CongestionAvoidance,
    FastRecovery,
}

struct TCPCongestionControl {
    cwnd: u32,           // Congestion window (bytes)
    ssthresh: u32,       // Slow start threshold (bytes)
    dupacks: u32,        // Duplicate ACK count
    mss: u32,            // Maximum segment size
    state: CongestionState,
    recover: u32,        // Recovery point
}

impl TCPCongestionControl {
    const INITIAL_CWND: u32 = 10;
    const MSS: u32 = 1460;
    const INITIAL_SSTHRESH: u32 = 65535;

    fn new() -> Self {
        Self {
            cwnd: Self::INITIAL_CWND * Self::MSS,
            ssthresh: Self::INITIAL_SSTHRESH,
            dupacks: 0,
            mss: Self::MSS,
            state: CongestionState::SlowStart,
            recover: 0,
        }
    }

    fn on_ack(&mut self, ack_seq: u32, is_duplicate: bool) {
        if is_duplicate {
            self.handle_duplicate_ack(ack_seq);
        } else {
            self.handle_new_ack(ack_seq);
        }
    }

    fn handle_duplicate_ack(&mut self, ack_seq: u32) {
        self.dupacks += 1;

        if self.dupacks == 3 {
            // Fast Retransmit triggered
            println!("Fast Retransmit at cwnd={}", self.cwnd);

            self.ssthresh = cmp::max(self.cwnd / 2, 2 * self.mss);
            self.cwnd = self.ssthresh + 3 * self.mss;
            self.state = CongestionState::FastRecovery;
            self.recover = ack_seq;

            println!(
                "Entering Fast Recovery: ssthresh={}, cwnd={}",
                self.ssthresh, self.cwnd
            );
        } else if self.state == CongestionState::FastRecovery && self.dupacks > 3 {
            // Inflate cwnd for additional duplicate ACKs
            self.cwnd += self.mss;
            println!("Inflating cwnd: {}", self.cwnd);
        }
    }

    fn handle_new_ack(&mut self, _ack_seq: u32) {
        match self.state {
            CongestionState::FastRecovery => {
                // Exit Fast Recovery
                self.cwnd = self.ssthresh;
                self.state = CongestionState::CongestionAvoidance;
                println!("Exiting Fast Recovery: cwnd={}", self.cwnd);
            }
            CongestionState::SlowStart => {
                // Exponential growth
                self.cwnd += self.mss;

                if self.cwnd >= self.ssthresh {
                    self.state = CongestionState::CongestionAvoidance;
                    println!("Entering Congestion Avoidance at cwnd={}", self.cwnd);
                } else {
                    println!("Slow Start: cwnd={}", self.cwnd);
                }
            }
            CongestionState::CongestionAvoidance => {
                // Linear growth: approximately 1 MSS per RTT
                let increment = (self.mss * self.mss) / self.cwnd;
                self.cwnd += increment;
                println!("Congestion Avoidance: cwnd={}", self.cwnd);
            }
        }

        self.dupacks = 0;
    }

    fn on_timeout(&mut self) {
        println!("Timeout at cwnd={}", self.cwnd);

        self.ssthresh = cmp::max(self.cwnd / 2, 2 * self.mss);
        self.cwnd = self.mss;
        self.state = CongestionState::SlowStart;
        self.dupacks = 0;

        println!(
            "Reset to Slow Start: ssthresh={}, cwnd={}",
            self.ssthresh, self.cwnd
        );
    }

    fn get_cwnd(&self) -> u32 {
        self.cwnd
    }

    fn get_state(&self) -> CongestionState {
        self.state
    }
}

fn main() {
    let mut cc = TCPCongestionControl::new();

    println!("=== TCP Congestion Control (Rust) ===\n");
    println!("Initial: cwnd={}, ssthresh={}\n", cc.get_cwnd(), cc.ssthresh);

    // Simulate Slow Start
    println!("--- Slow Start Phase ---");
    for i in 0..5 {
        cc.on_ack(1000 * (i + 1), false);
    }

    // Simulate Congestion Avoidance
    println!("\n--- Congestion Avoidance Phase ---");
    for i in 0..5 {
        cc.on_ack(6000 + 1000 * i, false);
    }

    // Simulate packet loss
    println!("\n--- Packet Loss (3 duplicate ACKs) ---");
    cc.on_ack(11000, true);
    cc.on_ack(11000, true);
    cc.on_ack(11000, true);

    // Additional duplicate ACKs
    println!("\n--- Fast Recovery ---");
    cc.on_ack(11000, true);
    cc.on_ack(11000, true);

    // New ACK
    println!("\n--- New ACK (Recovery Complete) ---");
    cc.on_ack(12000, false);

    // Timeout
    println!("\n--- Timeout ---");
    cc.on_timeout();

    println!("\nFinal state: {:?}, cwnd={}", cc.get_state(), cc.get_cwnd());
}
```

## Summary

**TCP Congestion Control** is essential for preventing network congestion and ensuring efficient, fair network utilization. The four main components work together:

1. **Slow Start** - Exponentially increases the congestion window to quickly discover available bandwidth while starting conservatively
2. **Congestion Avoidance** - Linearly increases the window to carefully probe for additional capacity without causing congestion
3. **Fast Retransmit** - Detects packet loss early (via 3 duplicate ACKs) and retransmits immediately without waiting for timeout
4. **Fast Recovery** - Maintains higher throughput during loss recovery by avoiding a complete return to Slow Start

These algorithms dynamically adapt the transmission rate based on network conditions, balancing the competing goals of high throughput, low latency, and fair resource sharing. Modern variants like TCP Reno, TCP NewReno, TCP CUBIC, and TCP BBR build upon these foundational concepts to optimize performance for different network conditions.

The key insight is that TCP treats packet loss as a congestion signal and responds by reducing its sending rate, then gradually increasing again until it finds the optimal operating point. This creates a sawtooth pattern in the congestion window over time, constantly probing for available bandwidth while backing off when congestion is detected.