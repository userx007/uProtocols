# CAN Database Files (DBC/KCD)

## Overview

CAN database files are standardized formats for defining the structure and meaning of CAN network communications. They serve as a "data dictionary" that maps raw CAN messages to human-readable signals, defines encoding/decoding rules, and documents network configurations.

## What Are Database Files?

**DBC (CAN Database)** and **KCD (KCD Format)** files store:
- **Message definitions**: CAN IDs, names, sizes, transmitters
- **Signal definitions**: Bit positions, lengths, scaling, offsets, units
- **Network nodes**: ECUs and their transmitted/received messages
- **Enumerations**: Named values for signal states
- **Comments and metadata**: Documentation for maintenance

These files enable:
- Automatic code generation for encoding/decoding
- Consistent interpretation across teams and tools
- Version control of network specifications
- Interoperability between different analysis tools

## DBC File Structure

DBC files use a text-based format with specific keywords:

```
VERSION ""

NS_ : 
    NS_DESC_
    CM_
    BA_DEF_
    BA_
    VAL_
    CAT_DEF_
    CAT_
    FILTER
    BA_DEF_DEF_
    EV_DATA_
    ENVVAR_DATA_
    SGTYPE_
    SGTYPE_VAL_
    BA_DEF_SGTYPE_
    BA_SGTYPE_
    SIG_TYPE_REF_
    VAL_TABLE_
    SIG_GROUP_
    SIG_VALTYPE_
    SIGTYPE_VALTYPE_
    BO_TX_BU_
    CAT_
    FILTER

BS_:

BU_: ECU1 ECU2 ECU3

BO_ 100 EngineData: 8 ECU1
 SG_ EngineSpeed : 0|16@1+ (0.25,0) [0|16383.75] "rpm" ECU2
 SG_ EngineTemp : 16|8@1+ (1,-40) [-40|215] "degC" ECU2
```

### Key Sections

1. **BU_ (Network Nodes)**: Lists all ECUs
2. **BO_ (Message Objects)**: Defines CAN messages
3. **SG_ (Signals)**: Defines signals within messages
4. **CM_ (Comments)**: Documentation strings
5. **BA_ (Attributes)**: Custom properties
6. **VAL_ (Value Tables)**: Enumeration mappings

## Signal Definition Format

```
SG_ SignalName : StartBit|Length@ByteOrder+ (Factor,Offset) [Min|Max] "Unit" Receiver
```

- **StartBit**: LSB position (0-63 for 8-byte message)
- **Length**: Number of bits
- **ByteOrder**: 1=little-endian, 0=big-endian
- **+/-**: Signed/unsigned
- **Factor/Offset**: Physical value = (raw_value * Factor) + Offset
- **Min/Max**: Valid range
- **Unit**: Engineering units
- **Receiver**: ECU that consumes this signal

## C/C++ Implementation

### Parsing DBC Files

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MAX_SIGNALS 64
#define MAX_MESSAGES 256

typedef struct {
    char name[64];
    uint16_t start_bit;
    uint16_t length;
    uint8_t byte_order;  // 0=big-endian, 1=little-endian
    uint8_t is_signed;
    double factor;
    double offset;
    double min_value;
    double max_value;
    char unit[32];
} CANSignal;

typedef struct {
    uint32_t can_id;
    char name[64];
    uint8_t dlc;
    char transmitter[64];
    CANSignal signals[MAX_SIGNALS];
    uint8_t signal_count;
} CANMessage;

typedef struct {
    CANMessage messages[MAX_MESSAGES];
    uint16_t message_count;
    char nodes[32][64];
    uint8_t node_count;
} CANDatabase;

// Simple DBC parser (partial implementation)
int parse_dbc_file(const char* filename, CANDatabase* db) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        perror("Failed to open DBC file");
        return -1;
    }
    
    char line[512];
    CANMessage* current_msg = NULL;
    
    while (fgets(line, sizeof(line), file)) {
        // Parse network nodes
        if (strncmp(line, "BU_:", 3) == 0) {
            char* token = strtok(line + 3, " \t\n");
            while (token && db->node_count < 32) {
                strncpy(db->nodes[db->node_count++], token, 63);
                token = strtok(NULL, " \t\n");
            }
        }
        
        // Parse message definition
        else if (strncmp(line, "BO_ ", 4) == 0) {
            if (db->message_count >= MAX_MESSAGES) continue;
            
            current_msg = &db->messages[db->message_count++];
            sscanf(line, "BO_ %u %63s %hhu %63s", 
                   &current_msg->can_id, 
                   current_msg->name,
                   &current_msg->dlc,
                   current_msg->transmitter);
            
            // Remove trailing colon from name
            char* colon = strchr(current_msg->name, ':');
            if (colon) *colon = '\0';
            
            current_msg->signal_count = 0;
        }
        
        // Parse signal definition
        else if (strncmp(line, " SG_ ", 5) == 0 && current_msg) {
            if (current_msg->signal_count >= MAX_SIGNALS) continue;
            
            CANSignal* sig = &current_msg->signals[current_msg->signal_count++];
            char byte_order_char, sign_char;
            
            sscanf(line, " SG_ %63s : %hu|%hu@%c%c (%lf,%lf) [%lf|%lf] \"%31[^\"]",
                   sig->name,
                   &sig->start_bit,
                   &sig->length,
                   &byte_order_char,
                   &sign_char,
                   &sig->factor,
                   &sig->offset,
                   &sig->min_value,
                   &sig->max_value,
                   sig->unit);
            
            sig->byte_order = (byte_order_char == '1') ? 1 : 0;
            sig->is_signed = (sign_char == '-') ? 1 : 0;
        }
    }
    
    fclose(file);
    return 0;
}

// Decode signal from CAN data
double decode_signal(const CANSignal* sig, const uint8_t* data) {
    uint64_t raw_value = 0;
    
    if (sig->byte_order == 1) {  // Little-endian (Intel)
        uint16_t bit_pos = sig->start_bit;
        for (uint16_t i = 0; i < sig->length; i++) {
            uint8_t byte_idx = bit_pos / 8;
            uint8_t bit_idx = bit_pos % 8;
            if (data[byte_idx] & (1 << bit_idx)) {
                raw_value |= (1ULL << i);
            }
            bit_pos++;
        }
    } else {  // Big-endian (Motorola)
        // More complex bit extraction for big-endian
        // Implementation varies by convention
    }
    
    // Handle signed values
    if (sig->is_signed && (raw_value & (1ULL << (sig->length - 1)))) {
        raw_value |= (~0ULL << sig->length);  // Sign extend
    }
    
    // Apply scaling
    return (double)raw_value * sig->factor + sig->offset;
}

// Encode signal into CAN data
void encode_signal(const CANSignal* sig, double value, uint8_t* data) {
    // Convert physical value to raw value
    int64_t raw_value = (int64_t)((value - sig->offset) / sig->factor);
    
    // Clamp to valid range
    int64_t max_unsigned = (1LL << sig->length) - 1;
    if (!sig->is_signed && raw_value > max_unsigned) {
        raw_value = max_unsigned;
    }
    
    if (sig->byte_order == 1) {  // Little-endian
        uint16_t bit_pos = sig->start_bit;
        for (uint16_t i = 0; i < sig->length; i++) {
            uint8_t byte_idx = bit_pos / 8;
            uint8_t bit_idx = bit_pos % 8;
            if (raw_value & (1LL << i)) {
                data[byte_idx] |= (1 << bit_idx);
            } else {
                data[byte_idx] &= ~(1 << bit_idx);
            }
            bit_pos++;
        }
    }
}

int main() {
    CANDatabase db = {0};
    
    if (parse_dbc_file("vehicle.dbc", &db) == 0) {
        printf("Loaded %d messages from DBC\n", db.message_count);
        
        // Example: decode a message
        uint8_t can_data[8] = {0x10, 0x27, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};
        
        for (int i = 0; i < db.message_count; i++) {
            if (db.messages[i].can_id == 0x100) {
                printf("Message: %s\n", db.messages[i].name);
                for (int j = 0; j < db.messages[i].signal_count; j++) {
                    CANSignal* sig = &db.messages[i].signals[j];
                    double value = decode_signal(sig, can_data);
                    printf("  %s: %.2f %s\n", sig->name, value, sig->unit);
                }
            }
        }
    }
    
    return 0;
}
```

### C++ Object-Oriented Approach

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <map>
#include <string>
#include <cstdint>

class CANSignal {
public:
    std::string name;
    uint16_t start_bit;
    uint16_t length;
    bool is_little_endian;
    bool is_signed;
    double factor;
    double offset;
    double min_value;
    double max_value;
    std::string unit;
    
    double decode(const std::vector<uint8_t>& data) const {
        uint64_t raw = extract_bits(data);
        
        if (is_signed && (raw & (1ULL << (length - 1)))) {
            raw |= (~0ULL << length);
        }
        
        return static_cast<double>(static_cast<int64_t>(raw)) * factor + offset;
    }
    
    void encode(double value, std::vector<uint8_t>& data) const {
        int64_t raw = static_cast<int64_t>((value - offset) / factor);
        insert_bits(raw, data);
    }
    
private:
    uint64_t extract_bits(const std::vector<uint8_t>& data) const {
        uint64_t raw = 0;
        
        if (is_little_endian) {
            for (uint16_t i = 0; i < length; i++) {
                uint16_t bit_pos = start_bit + i;
                uint8_t byte_idx = bit_pos / 8;
                uint8_t bit_idx = bit_pos % 8;
                
                if (byte_idx < data.size() && (data[byte_idx] & (1 << bit_idx))) {
                    raw |= (1ULL << i);
                }
            }
        }
        
        return raw;
    }
    
    void insert_bits(uint64_t raw, std::vector<uint8_t>& data) const {
        if (is_little_endian) {
            for (uint16_t i = 0; i < length; i++) {
                uint16_t bit_pos = start_bit + i;
                uint8_t byte_idx = bit_pos / 8;
                uint8_t bit_idx = bit_pos % 8;
                
                if (byte_idx < data.size()) {
                    if (raw & (1ULL << i)) {
                        data[byte_idx] |= (1 << bit_idx);
                    } else {
                        data[byte_idx] &= ~(1 << bit_idx);
                    }
                }
            }
        }
    }
};

class CANMessage {
public:
    uint32_t can_id;
    std::string name;
    uint8_t dlc;
    std::string transmitter;
    std::vector<CANSignal> signals;
    
    std::map<std::string, double> decode(const std::vector<uint8_t>& data) const {
        std::map<std::string, double> values;
        for (const auto& sig : signals) {
            values[sig.name] = sig.decode(data);
        }
        return values;
    }
};

class CANDatabase {
public:
    std::map<uint32_t, CANMessage> messages;
    std::vector<std::string> nodes;
    
    bool load_dbc(const std::string& filename) {
        std::ifstream file(filename);
        if (!file) return false;
        
        std::string line;
        CANMessage* current_msg = nullptr;
        
        while (std::getline(file, line)) {
            if (line.rfind("BO_ ", 0) == 0) {
                uint32_t id;
                char name[64], tx[64];
                uint8_t dlc;
                
                if (sscanf(line.c_str(), "BO_ %u %63s %hhu %63s", 
                          &id, name, &dlc, tx) == 4) {
                    CANMessage msg;
                    msg.can_id = id;
                    msg.name = std::string(name).substr(0, strlen(name) - 1); // Remove ':'
                    msg.dlc = dlc;
                    msg.transmitter = tx;
                    
                    messages[id] = msg;
                    current_msg = &messages[id];
                }
            }
            else if (line.rfind(" SG_ ", 0) == 0 && current_msg) {
                CANSignal sig;
                char name[64], unit[32];
                char order, sign;
                
                if (sscanf(line.c_str(), " SG_ %63s : %hu|%hu@%c%c (%lf,%lf) [%lf|%lf] \"%31[^\"]",
                          name, &sig.start_bit, &sig.length, &order, &sign,
                          &sig.factor, &sig.offset, &sig.min_value, 
                          &sig.max_value, unit) >= 9) {
                    sig.name = name;
                    sig.unit = unit;
                    sig.is_little_endian = (order == '1');
                    sig.is_signed = (sign == '-');
                    
                    current_msg->signals.push_back(sig);
                }
            }
        }
        
        return true;
    }
    
    const CANMessage* get_message(uint32_t can_id) const {
        auto it = messages.find(can_id);
        return (it != messages.end()) ? &it->second : nullptr;
    }
};

int main() {
    CANDatabase db;
    
    if (db.load_dbc("vehicle.dbc")) {
        std::cout << "Loaded " << db.messages.size() << " messages\n";
        
        // Decode example
        std::vector<uint8_t> data = {0x10, 0x27, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00};
        
        const CANMessage* msg = db.get_message(0x100);
        if (msg) {
            auto values = msg->decode(data);
            std::cout << "Message: " << msg->name << "\n";
            for (const auto& [signal, value] : values) {
                std::cout << "  " << signal << ": " << value << "\n";
            }
        }
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader};

#[derive(Debug, Clone)]
pub struct CANSignal {
    pub name: String,
    pub start_bit: u16,
    pub length: u16,
    pub is_little_endian: bool,
    pub is_signed: bool,
    pub factor: f64,
    pub offset: f64,
    pub min_value: f64,
    pub max_value: f64,
    pub unit: String,
}

impl CANSignal {
    pub fn decode(&self, data: &[u8]) -> f64 {
        let raw = self.extract_bits(data);
        
        let raw_signed = if self.is_signed {
            let sign_bit = 1u64 << (self.length - 1);
            if raw & sign_bit != 0 {
                // Sign extend
                (raw | (!0u64 << self.length)) as i64
            } else {
                raw as i64
            }
        } else {
            raw as i64
        };
        
        (raw_signed as f64) * self.factor + self.offset
    }
    
    pub fn encode(&self, value: f64, data: &mut [u8]) {
        let raw = ((value - self.offset) / self.factor) as i64;
        self.insert_bits(raw as u64, data);
    }
    
    fn extract_bits(&self, data: &[u8]) -> u64 {
        let mut raw = 0u64;
        
        if self.is_little_endian {
            for i in 0..self.length {
                let bit_pos = self.start_bit + i;
                let byte_idx = (bit_pos / 8) as usize;
                let bit_idx = bit_pos % 8;
                
                if byte_idx < data.len() && (data[byte_idx] & (1 << bit_idx)) != 0 {
                    raw |= 1u64 << i;
                }
            }
        }
        
        raw
    }
    
    fn insert_bits(&self, raw: u64, data: &mut [u8]) {
        if self.is_little_endian {
            for i in 0..self.length {
                let bit_pos = self.start_bit + i;
                let byte_idx = (bit_pos / 8) as usize;
                let bit_idx = bit_pos % 8;
                
                if byte_idx < data.len() {
                    if (raw & (1u64 << i)) != 0 {
                        data[byte_idx] |= 1 << bit_idx;
                    } else {
                        data[byte_idx] &= !(1 << bit_idx);
                    }
                }
            }
        }
    }
}

#[derive(Debug, Clone)]
pub struct CANMessage {
    pub can_id: u32,
    pub name: String,
    pub dlc: u8,
    pub transmitter: String,
    pub signals: Vec<CANSignal>,
}

impl CANMessage {
    pub fn decode(&self, data: &[u8]) -> HashMap<String, f64> {
        self.signals
            .iter()
            .map(|sig| (sig.name.clone(), sig.decode(data)))
            .collect()
    }
}

#[derive(Debug, Default)]
pub struct CANDatabase {
    pub messages: HashMap<u32, CANMessage>,
    pub nodes: Vec<String>,
}

impl CANDatabase {
    pub fn load_dbc(&mut self, filename: &str) -> Result<(), Box<dyn std::error::Error>> {
        let file = File::open(filename)?;
        let reader = BufReader::new(file);
        
        let mut current_msg: Option<CANMessage> = None;
        
        for line in reader.lines() {
            let line = line?;
            
            if line.starts_with("BU_:") {
                self.nodes = line[3..]
                    .split_whitespace()
                    .map(String::from)
                    .collect();
            }
            else if line.starts_with("BO_ ") {
                if let Some(msg) = current_msg.take() {
                    self.messages.insert(msg.can_id, msg);
                }
                
                let parts: Vec<&str> = line.split_whitespace().collect();
                if parts.len() >= 4 {
                    if let Ok(id) = parts[1].parse::<u32>() {
                        if let Ok(dlc) = parts[3].parse::<u8>() {
                            let name = parts[2].trim_end_matches(':').to_string();
                            let transmitter = parts.get(4).unwrap_or(&"").to_string();
                            
                            current_msg = Some(CANMessage {
                                can_id: id,
                                name,
                                dlc,
                                transmitter,
                                signals: Vec::new(),
                            });
                        }
                    }
                }
            }
            else if line.trim().starts_with("SG_ ") {
                if let Some(ref mut msg) = current_msg {
                    if let Some(sig) = parse_signal(&line) {
                        msg.signals.push(sig);
                    }
                }
            }
        }
        
        if let Some(msg) = current_msg {
            self.messages.insert(msg.can_id, msg);
        }
        
        Ok(())
    }
    
    pub fn get_message(&self, can_id: u32) -> Option<&CANMessage> {
        self.messages.get(&can_id)
    }
}

fn parse_signal(line: &str) -> Option<CANSignal> {
    // Simplified parser - production code needs robust parsing
    let parts: Vec<&str> = line.trim().split_whitespace().collect();
    if parts.len() < 2 { return None; }
    
    let name = parts[1].to_string();
    
    // Parse bit position: "0|16@1+"
    let bit_info = parts.get(3)?;
    let bit_parts: Vec<&str> = bit_info.split(&['|', '@'][..]).collect();
    if bit_parts.len() < 3 { return None; }
    
    let start_bit = bit_parts[0].parse().ok()?;
    let length = bit_parts[1].parse().ok()?;
    let order_sign = bit_parts[2];
    
    let is_little_endian = order_sign.starts_with('1');
    let is_signed = order_sign.ends_with('-');
    
    // Parse scaling: "(0.25,0)"
    let scale_info = parts.get(4)?;
    let scale_parts: Vec<&str> = scale_info
        .trim_matches(|c| c == '(' || c == ')')
        .split(',')
        .collect();
    
    let factor = scale_parts.get(0)?.parse().ok()?;
    let offset = scale_parts.get(1)?.parse().ok()?;
    
    Some(CANSignal {
        name,
        start_bit,
        length,
        is_little_endian,
        is_signed,
        factor,
        offset,
        min_value: 0.0,
        max_value: 0.0,
        unit: String::new(),
    })
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut db = CANDatabase::default();
    db.load_dbc("vehicle.dbc")?;
    
    println!("Loaded {} messages", db.messages.len());
    
    // Decode example
    let data = [0x10, 0x27, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00];
    
    if let Some(msg) = db.get_message(0x100) {
        let values = msg.decode(&data);
        println!("Message: {}", msg.name);
        for (signal, value) in values {
            println!("  {}: {:.2}", signal, value);
        }
    }
    
    Ok(())
}
```

## Summary

**CAN Database Files (DBC/KCD)** provide a standardized way to document CAN network structures, enabling:

- **Automatic encoding/decoding** of CAN messages based on defined signal layouts
- **Consistent interpretation** across development teams and tools
- **Version-controlled specifications** that evolve with the system
- **Tool interoperability** for analysis, simulation, and code generation

**Key concepts**:
- Messages contain multiple signals with defined bit positions
- Signals use scaling (factor/offset) to convert between raw and physical values
- Byte ordering (little/big-endian) affects bit extraction algorithms
- Database files serve as the single source of truth for network definition

**Implementation patterns**:
- **C**: Manual parsing with structures, explicit bit manipulation
- **C++**: Object-oriented design with encapsulated encode/decode logic
- **Rust**: Safe abstractions with strong type checking and error handling

These database files are critical for modern automotive and industrial CAN networks, where hundreds of signals require precise documentation and automated processing.