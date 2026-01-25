# Fuzzing CAN Protocols

## Overview

Fuzzing is a dynamic testing technique that involves sending malformed, unexpected, or random data to a system to discover vulnerabilities, crashes, or unexpected behaviors. In the context of Controller Area Network (CAN) protocols, fuzzing is critical for identifying security weaknesses in automotive and industrial control systems.

## Why Fuzz CAN Protocols?

CAN buses were originally designed without security in mind. Modern vehicles and industrial systems rely heavily on CAN for critical communications, making them attractive targets for attackers. Fuzzing helps discover:

- **Buffer overflows** in CAN message handlers
- **Denial-of-Service vulnerabilities** through malformed frames
- **State machine errors** in protocol implementations
- **Input validation failures**
- **Race conditions** in message processing
- **Protocol implementation bugs**

## CAN Frame Structure Recap

Understanding the CAN frame structure is essential for effective fuzzing:

- **Identifier**: 11-bit (standard) or 29-bit (extended)
- **Data Length Code (DLC)**: 0-8 bytes
- **Data Field**: 0-8 bytes of payload
- **RTR bit**: Remote Transmission Request
- **IDE bit**: Identifier Extension bit

## Fuzzing Strategies for CAN

### 1. **Random Fuzzing**
Generate completely random CAN frames with random IDs, DLC values, and data.

### 2. **Mutation-Based Fuzzing**
Capture legitimate CAN traffic and mutate specific fields.

### 3. **Generation-Based Fuzzing**
Create frames based on protocol specifications (UDS, J1939, CANopen).

### 4. **Stateful Fuzzing**
Maintain state to test protocol sequences (e.g., diagnostic sessions).

## C/C++ Implementation

Here's a comprehensive CAN fuzzer implementation in C++:

```cpp
#include <iostream>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <vector>
#include <random>

class CANFuzzer {
private:
    int sock;
    struct sockaddr_can addr;
    struct ifreq ifr;
    std::mt19937 rng;
    
    // Statistics
    uint64_t frames_sent;
    uint64_t errors;
    
public:
    CANFuzzer(const char* interface) : frames_sent(0), errors(0) {
        // Initialize random number generator
        rng.seed(std::random_device{}());
        
        // Create socket
        sock = socket(PF_CAN, SOCK_RAW, CAN_RAW);
        if (sock < 0) {
            perror("Socket creation failed");
            exit(1);
        }
        
        // Specify CAN interface
        strcpy(ifr.ifr_name, interface);
        ioctl(sock, SIOCGIFINDEX, &ifr);
        
        // Bind socket to CAN interface
        addr.can_family = AF_CAN;
        addr.can_ifindex = ifr.ifr_ifindex;
        
        if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
            perror("Bind failed");
            exit(1);
        }
        
        std::cout << "CAN Fuzzer initialized on " << interface << std::endl;
    }
    
    ~CANFuzzer() {
        close(sock);
        std::cout << "\nFuzzing Statistics:" << std::endl;
        std::cout << "  Frames sent: " << frames_sent << std::endl;
        std::cout << "  Errors: " << errors << std::endl;
    }
    
    // Generate random CAN ID (standard or extended)
    uint32_t randomCANID(bool extended = false) {
        std::uniform_int_distribution<uint32_t> dist_std(0, 0x7FF);
        std::uniform_int_distribution<uint32_t> dist_ext(0, 0x1FFFFFFF);
        
        if (extended) {
            return dist_ext(rng) | CAN_EFF_FLAG;
        }
        return dist_std(rng);
    }
    
    // Generate random data length
    uint8_t randomDLC() {
        std::uniform_int_distribution<uint8_t> dist(0, 8);
        return dist(rng);
    }
    
    // Fill buffer with random data
    void randomData(uint8_t* data, size_t len) {
        std::uniform_int_distribution<uint8_t> dist(0, 255);
        for (size_t i = 0; i < len; i++) {
            data[i] = dist(rng);
        }
    }
    
    // Random fuzzing: completely random frames
    void fuzzRandom(uint64_t count, uint32_t delay_us = 1000) {
        std::cout << "Starting random fuzzing (" << count << " frames)..." << std::endl;
        
        for (uint64_t i = 0; i < count; i++) {
            struct can_frame frame;
            memset(&frame, 0, sizeof(frame));
            
            // Random ID (mix of standard and extended)
            bool use_extended = (rng() % 2 == 0);
            frame.can_id = randomCANID(use_extended);
            
            // Random DLC
            frame.can_dlc = randomDLC();
            
            // Random data
            randomData(frame.data, frame.can_dlc);
            
            // Send frame
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                errors++;
            } else {
                frames_sent++;
            }
            
            if (delay_us > 0) {
                usleep(delay_us);
            }
            
            // Progress indicator
            if (i % 1000 == 0) {
                std::cout << "Sent: " << i << " frames\r" << std::flush;
            }
        }
        std::cout << std::endl;
    }
    
    // Mutation-based fuzzing
    void fuzzMutate(const struct can_frame& base_frame, uint64_t count, 
                    uint32_t delay_us = 1000) {
        std::cout << "Starting mutation fuzzing..." << std::endl;
        
        std::uniform_int_distribution<int> mutation_type(0, 4);
        
        for (uint64_t i = 0; i < count; i++) {
            struct can_frame frame = base_frame;
            
            // Apply random mutation
            switch (mutation_type(rng)) {
                case 0: // Mutate ID
                    frame.can_id ^= (1 << (rng() % 11));
                    break;
                    
                case 1: // Mutate DLC
                    frame.can_dlc = randomDLC();
                    break;
                    
                case 2: // Bit flip in data
                    if (frame.can_dlc > 0) {
                        int byte_idx = rng() % frame.can_dlc;
                        int bit_idx = rng() % 8;
                        frame.data[byte_idx] ^= (1 << bit_idx);
                    }
                    break;
                    
                case 3: // Byte substitution
                    if (frame.can_dlc > 0) {
                        int byte_idx = rng() % frame.can_dlc;
                        frame.data[byte_idx] = rng() % 256;
                    }
                    break;
                    
                case 4: // Randomize all data
                    randomData(frame.data, frame.can_dlc);
                    break;
            }
            
            // Send mutated frame
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                errors++;
            } else {
                frames_sent++;
            }
            
            if (delay_us > 0) {
                usleep(delay_us);
            }
            
            if (i % 1000 == 0) {
                std::cout << "Sent: " << i << " frames\r" << std::flush;
            }
        }
        std::cout << std::endl;
    }
    
    // UDS-specific fuzzing (diagnostic services)
    void fuzzUDS(uint32_t target_id, uint64_t count, uint32_t delay_us = 10000) {
        std::cout << "Starting UDS protocol fuzzing..." << std::endl;
        
        // Common UDS service IDs
        std::vector<uint8_t> services = {
            0x10, // DiagnosticSessionControl
            0x11, // ECUReset
            0x14, // ClearDiagnosticInformation
            0x19, // ReadDTCInformation
            0x22, // ReadDataByIdentifier
            0x27, // SecurityAccess
            0x2E, // WriteDataByIdentifier
            0x31, // RoutineControl
            0x34, // RequestDownload
            0x36, // TransferData
            0x3E, // TesterPresent
        };
        
        std::uniform_int_distribution<size_t> service_dist(0, services.size() - 1);
        
        for (uint64_t i = 0; i < count; i++) {
            struct can_frame frame;
            memset(&frame, 0, sizeof(frame));
            
            frame.can_id = target_id;
            frame.can_dlc = randomDLC();
            
            // Use valid service ID sometimes, random otherwise
            if (rng() % 3 == 0) {
                frame.data[0] = services[service_dist(rng)];
            } else {
                frame.data[0] = rng() % 256;
            }
            
            // Fill rest with random data
            randomData(frame.data + 1, frame.can_dlc - 1);
            
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                errors++;
            } else {
                frames_sent++;
            }
            
            if (delay_us > 0) {
                usleep(delay_us);
            }
            
            if (i % 100 == 0) {
                std::cout << "Sent: " << i << " UDS frames\r" << std::flush;
            }
        }
        std::cout << std::endl;
    }
    
    // Invalid DLC fuzzing (DLC > 8)
    void fuzzInvalidDLC(uint64_t count, uint32_t delay_us = 1000) {
        std::cout << "Starting invalid DLC fuzzing..." << std::endl;
        
        for (uint64_t i = 0; i < count; i++) {
            struct can_frame frame;
            memset(&frame, 0, sizeof(frame));
            
            frame.can_id = randomCANID(false);
            // Intentionally invalid DLC values
            frame.can_dlc = 9 + (rng() % 7); // 9-15
            
            randomData(frame.data, 8);
            
            if (write(sock, &frame, sizeof(frame)) != sizeof(frame)) {
                errors++;
            } else {
                frames_sent++;
            }
            
            if (delay_us > 0) {
                usleep(delay_us);
            }
            
            if (i % 1000 == 0) {
                std::cout << "Sent: " << i << " frames\r" << std::flush;
            }
        }
        std::cout << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <can_interface>" << std::endl;
        std::cerr << "Example: " << argv[0] << " can0" << std::endl;
        return 1;
    }
    
    CANFuzzer fuzzer(argv[1]);
    
    std::cout << "\nCAN Protocol Fuzzer" << std::endl;
    std::cout << "===================" << std::endl;
    
    // Run different fuzzing strategies
    fuzzer.fuzzRandom(5000, 500);
    
    // Example base frame for mutation
    struct can_frame base_frame;
    base_frame.can_id = 0x7DF; // OBD-II broadcast
    base_frame.can_dlc = 8;
    memset(base_frame.data, 0, 8);
    base_frame.data[0] = 0x02;
    base_frame.data[1] = 0x01; // Show current data
    base_frame.data[2] = 0x0D; // Vehicle speed
    
    fuzzer.fuzzMutate(base_frame, 2000, 1000);
    
    // UDS fuzzing on ECU
    fuzzer.fuzzUDS(0x7E0, 1000, 5000);
    
    // Invalid DLC fuzzing
    fuzzer.fuzzInvalidDLC(1000, 500);
    
    return 0;
}
```

## Rust Implementation

Here's a Rust implementation using the `socketcan` crate:

```rust
use socketcan::{CANSocket, CANFrame, CANError};
use rand::Rng;
use std::time::Duration;
use std::thread;

pub struct CANFuzzer {
    socket: CANSocket,
    frames_sent: u64,
    errors: u64,
}

impl CANFuzzer {
    pub fn new(interface: &str) -> Result<Self, CANError> {
        let socket = CANSocket::open(interface)?;
        
        println!("CAN Fuzzer initialized on {}", interface);
        
        Ok(CANFuzzer {
            socket,
            frames_sent: 0,
            errors: 0,
        })
    }
    
    // Generate random CAN ID
    fn random_can_id(&self, extended: bool) -> u32 {
        let mut rng = rand::thread_rng();
        if extended {
            rng.gen_range(0..0x1FFFFFFF) | 0x80000000 // Set EFF flag
        } else {
            rng.gen_range(0..0x7FF)
        }
    }
    
    // Generate random data
    fn random_data(&self, len: usize) -> Vec<u8> {
        let mut rng = rand::thread_rng();
        (0..len).map(|_| rng.gen()).collect()
    }
    
    // Random fuzzing
    pub fn fuzz_random(&mut self, count: u64, delay_ms: u64) {
        println!("Starting random fuzzing ({} frames)...", count);
        let mut rng = rand::thread_rng();
        
        for i in 0..count {
            let extended = rng.gen_bool(0.5);
            let can_id = self.random_can_id(extended);
            let dlc = rng.gen_range(0..=8);
            let data = self.random_data(dlc);
            
            match CANFrame::new(can_id, &data, false, false) {
                Ok(frame) => {
                    match self.socket.write_frame(&frame) {
                        Ok(_) => self.frames_sent += 1,
                        Err(_) => self.errors += 1,
                    }
                }
                Err(_) => self.errors += 1,
            }
            
            if delay_ms > 0 {
                thread::sleep(Duration::from_millis(delay_ms));
            }
            
            if i % 1000 == 0 {
                print!("\rSent: {} frames", i);
            }
        }
        println!();
    }
    
    // Mutation-based fuzzing
    pub fn fuzz_mutate(&mut self, base_frame: &CANFrame, count: u64, delay_ms: u64) {
        println!("Starting mutation fuzzing...");
        let mut rng = rand::thread_rng();
        
        for i in 0..count {
            let mut data = base_frame.data().to_vec();
            let mutation_type = rng.gen_range(0..5);
            
            match mutation_type {
                0 => {
                    // Bit flip in data
                    if !data.is_empty() {
                        let byte_idx = rng.gen_range(0..data.len());
                        let bit_idx = rng.gen_range(0..8);
                        data[byte_idx] ^= 1 << bit_idx;
                    }
                }
                1 => {
                    // Byte substitution
                    if !data.is_empty() {
                        let byte_idx = rng.gen_range(0..data.len());
                        data[byte_idx] = rng.gen();
                    }
                }
                2 => {
                    // Change length
                    let new_len = rng.gen_range(0..=8);
                    data.resize(new_len, 0);
                }
                3 => {
                    // Randomize all data
                    data = self.random_data(data.len());
                }
                4 => {
                    // Add random bytes
                    let extra_bytes = rng.gen_range(0..=(8 - data.len()));
                    data.extend(self.random_data(extra_bytes));
                }
                _ => {}
            }
            
            match CANFrame::new(base_frame.id(), &data, false, false) {
                Ok(frame) => {
                    match self.socket.write_frame(&frame) {
                        Ok(_) => self.frames_sent += 1,
                        Err(_) => self.errors += 1,
                    }
                }
                Err(_) => self.errors += 1,
            }
            
            if delay_ms > 0 {
                thread::sleep(Duration::from_millis(delay_ms));
            }
            
            if i % 1000 == 0 {
                print!("\rSent: {} frames", i);
            }
        }
        println!();
    }
    
    // UDS-specific fuzzing
    pub fn fuzz_uds(&mut self, target_id: u32, count: u64, delay_ms: u64) {
        println!("Starting UDS protocol fuzzing...");
        let mut rng = rand::thread_rng();
        
        let services = vec![
            0x10, // DiagnosticSessionControl
            0x11, // ECUReset
            0x14, // ClearDiagnosticInformation
            0x19, // ReadDTCInformation
            0x22, // ReadDataByIdentifier
            0x27, // SecurityAccess
            0x2E, // WriteDataByIdentifier
            0x31, // RoutineControl
            0x3E, // TesterPresent
        ];
        
        for i in 0..count {
            let dlc = rng.gen_range(1..=8);
            let mut data = Vec::with_capacity(dlc);
            
            // Use valid service ID sometimes, random otherwise
            if rng.gen_bool(0.33) {
                data.push(services[rng.gen_range(0..services.len())]);
            } else {
                data.push(rng.gen());
            }
            
            // Fill rest with random data
            data.extend(self.random_data(dlc - 1));
            
            match CANFrame::new(target_id, &data, false, false) {
                Ok(frame) => {
                    match self.socket.write_frame(&frame) {
                        Ok(_) => self.frames_sent += 1,
                        Err(_) => self.errors += 1,
                    }
                }
                Err(_) => self.errors += 1,
            }
            
            if delay_ms > 0 {
                thread::sleep(Duration::from_millis(delay_ms));
            }
            
            if i % 100 == 0 {
                print!("\rSent: {} UDS frames", i);
            }
        }
        println!();
    }
    
    pub fn print_stats(&self) {
        println!("\nFuzzing Statistics:");
        println!("  Frames sent: {}", self.frames_sent);
        println!("  Errors: {}", self.errors);
    }
}

fn main() -> Result<(), CANError> {
    let args: Vec<String> = std::env::args().collect();
    
    if args.len() < 2 {
        eprintln!("Usage: {} <can_interface>", args[0]);
        eprintln!("Example: {} can0", args[0]);
        std::process::exit(1);
    }
    
    let mut fuzzer = CANFuzzer::new(&args[1])?;
    
    println!("\nCAN Protocol Fuzzer");
    println!("===================");
    
    // Run different fuzzing strategies
    fuzzer.fuzz_random(5000, 1);
    
    // Example base frame for mutation
    let base_frame = CANFrame::new(
        0x7DF, // OBD-II broadcast
        &[0x02, 0x01, 0x0D, 0x00, 0x00, 0x00, 0x00, 0x00],
        false,
        false,
    )?;
    
    fuzzer.fuzz_mutate(&base_frame, 2000, 2);
    
    // UDS fuzzing
    fuzzer.fuzz_uds(0x7E0, 1000, 10);
    
    fuzzer.print_stats();
    
    Ok(())
}
```

## Advanced Fuzzing Techniques

### Coverage-Guided Fuzzing

For deeper testing, use coverage-guided fuzzing with tools like AFL or libFuzzer:

```cpp
// libFuzzer harness example
extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    if (size < sizeof(struct can_frame)) {
        return 0;
    }
    
    struct can_frame *frame = (struct can_frame *)data;
    
    // Call your CAN message handler
    process_can_frame(frame);
    
    return 0;
}
```

### Stateful Protocol Fuzzing

For protocols like UDS that require session management:

```rust
pub struct StatefulUDSFuzzer {
    current_session: u8,
    security_level: u8,
    // Track protocol state
}

impl StatefulUDSFuzzer {
    pub fn fuzz_diagnostic_session(&mut self) {
        // Enter diagnostic session first
        self.send_uds(0x10, &[0x03]); // Extended session
        
        // Then fuzz within that session context
        // ...
    }
}
```

## Safety Considerations

⚠️ **WARNING**: Fuzzing CAN buses can cause:
- **Physical damage** to vehicles or machinery
- **Loss of control** of safety-critical systems
- **Bricked ECUs** requiring reflashing
- **Voided warranties**

### Safe Fuzzing Practices:

1. **Use isolated test benches** - Never fuzz production vehicles
2. **Implement rate limiting** - Don't flood the bus
3. **Monitor for anomalies** - Watch for crashes or reboots
4. **Have kill switches** - Be able to stop fuzzing immediately
5. **Test in simulation** - Use virtual CAN environments when possible
6. **Document everything** - Record all crashes and interesting findings

## Summary

Fuzzing CAN protocols is essential for discovering vulnerabilities in automotive and industrial systems. Key takeaways:

- **Multiple fuzzing strategies** are needed: random, mutation-based, generation-based, and stateful fuzzing
- **Protocol-aware fuzzing** (UDS, J1939, etc.) finds more vulnerabilities than purely random approaches
- **Safety is paramount** - always fuzz on isolated test systems, never on production vehicles
- **C/C++ and Rust** both provide excellent tools for building CAN fuzzers with low-level control
- **Coverage-guided fuzzing** provides deeper testing by tracking code paths
- **Monitoring and logging** are critical for identifying and reproducing issues

Effective CAN fuzzing requires understanding both the protocol specifications and the underlying implementation details. Combining automated fuzzing with manual analysis produces the best results for securing CAN-based systems.