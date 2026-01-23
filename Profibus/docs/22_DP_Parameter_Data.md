# DP Parameter Data in Profibus

## Detailed Description

DP Parameter Data is a fundamental aspect of Profibus-DP (Decentralized Periphery) communication that enables the master station to configure and parameterize slave devices during the startup phase. This mechanism allows the master to send device-specific configuration data to slaves, ensuring they operate correctly according to the application requirements.

### Key Concepts

**Parameterization Process:**
- Occurs during the initialization phase before cyclic data exchange begins
- Allows the master to configure operating parameters of slave devices
- Device-specific parameters are sent in a structured telegram format
- Parameters can include operating modes, sensor ranges, filter settings, alarm limits, etc.

**Parameter Telegram Structure:**
The parameter data telegram typically consists of:
- **Header Information**: Device identification and parameter set version
- **Standard Parameters**: Common Profibus parameters (watchdog, diagnostic settings)
- **Device-Specific Parameters**: Manufacturer-specific configuration data organized according to the GSD (Device Database) file

**Parameter Types:**
1. **Standard DP Parameters**: Defined by the Profibus specification (station address, I/O configuration, watchdog time)
2. **Extended Parameters**: Device-specific parameters defined in the GSD file
3. **User Parameters**: Application-specific configuration values

### Programming Considerations

When implementing DP parameter handling, you need to:
- Parse GSD files to understand device-specific parameter structures
- Build properly formatted parameter telegrams
- Handle parameter acknowledgment and error responses
- Manage parameter persistence and restore functionality
- Validate parameter ranges and dependencies

## Code Examples

### C/C++ Implementation

```cpp
#include <stdint.h>
#include <string.h>
#include <vector>
#include <iostream>

// Profibus DP Parameter Data Structures and Functions

// Standard DP parameter header
struct DPParamHeader {
    uint8_t station_address;      // Slave station address
    uint8_t ident_number_high;    // Device identification (high byte)
    uint8_t ident_number_low;     // Device identification (low byte)
    uint8_t lock_unlock;          // Lock/unlock request
    uint8_t watchdog_fact1;       // Watchdog time factor 1
    uint8_t watchdog_fact2;       // Watchdog time factor 2
    uint8_t min_tsdr;             // Minimum station delay responder
    uint16_t ident_number;        // Reserved for compatibility
    uint8_t group_ident;          // Group identification
} __attribute__((packed));

// User parameter structure (device-specific)
struct UserParameter {
    uint8_t param_id;             // Parameter identifier
    uint8_t param_length;         // Length of parameter data
    std::vector<uint8_t> data;    // Parameter data
};

// Complete parameter telegram
class DPParameterTelegram {
private:
    DPParamHeader header;
    std::vector<UserParameter> user_params;
    std::vector<uint8_t> telegram_buffer;

public:
    DPParameterTelegram(uint8_t station_addr, uint16_t device_id) {
        memset(&header, 0, sizeof(header));
        header.station_address = station_addr;
        header.ident_number_high = (device_id >> 8) & 0xFF;
        header.ident_number_low = device_id & 0xFF;
        header.lock_unlock = 0x01;  // Unlock
        header.watchdog_fact1 = 10;  // 10ms base
        header.watchdog_fact2 = 10;  // Multiplier
        header.min_tsdr = 11;        // Minimum TSDR
        header.group_ident = 0x00;
    }

    // Set watchdog time (in milliseconds)
    void setWatchdogTime(uint16_t time_ms) {
        // Watchdog time = Fact1 * Fact2 * 10ms
        header.watchdog_fact1 = (time_ms / 100) & 0xFF;
        header.watchdog_fact2 = (time_ms / (header.watchdog_fact1 * 10)) & 0xFF;
    }

    // Add a user parameter
    bool addUserParameter(uint8_t param_id, const uint8_t* data, uint8_t length) {
        if (length == 0 || length > 244) {
            return false;  // Invalid length
        }

        UserParameter param;
        param.param_id = param_id;
        param.param_length = length;
        param.data.assign(data, data + length);
        user_params.push_back(param);
        return true;
    }

    // Build the complete parameter telegram
    std::vector<uint8_t> buildTelegram() {
        telegram_buffer.clear();

        // Add header
        uint8_t* header_ptr = reinterpret_cast<uint8_t*>(&header);
        telegram_buffer.insert(telegram_buffer.end(), 
                              header_ptr, 
                              header_ptr + sizeof(DPParamHeader));

        // Add user parameters
        for (const auto& param : user_params) {
            telegram_buffer.push_back(param.param_id);
            telegram_buffer.push_back(param.param_length);
            telegram_buffer.insert(telegram_buffer.end(), 
                                  param.data.begin(), 
                                  param.data.end());
        }

        return telegram_buffer;
    }

    // Parse received parameter telegram
    static bool parseTelegram(const uint8_t* data, size_t length, 
                             DPParameterTelegram& telegram) {
        if (length < sizeof(DPParamHeader)) {
            return false;
        }

        // Parse header
        memcpy(&telegram.header, data, sizeof(DPParamHeader));
        
        // Parse user parameters
        size_t offset = sizeof(DPParamHeader);
        telegram.user_params.clear();

        while (offset < length) {
            if (offset + 2 > length) break;

            UserParameter param;
            param.param_id = data[offset++];
            param.param_length = data[offset++];

            if (offset + param.param_length > length) {
                return false;  // Invalid parameter length
            }

            param.data.assign(data + offset, data + offset + param.param_length);
            offset += param.param_length;
            telegram.user_params.push_back(param);
        }

        return true;
    }

    // Display parameter information
    void displayParameters() const {
        std::cout << "DP Parameter Telegram:" << std::endl;
        std::cout << "  Station Address: " << (int)header.station_address << std::endl;
        std::cout << "  Device ID: 0x" << std::hex 
                  << ((header.ident_number_high << 8) | header.ident_number_low) 
                  << std::dec << std::endl;
        std::cout << "  Watchdog Time: " 
                  << (header.watchdog_fact1 * header.watchdog_fact2 * 10) 
                  << " ms" << std::endl;
        
        std::cout << "  User Parameters: " << user_params.size() << std::endl;
        for (size_t i = 0; i < user_params.size(); i++) {
            std::cout << "    Param " << i << " - ID: 0x" << std::hex 
                      << (int)user_params[i].param_id 
                      << ", Length: " << std::dec 
                      << (int)user_params[i].param_length << std::endl;
        }
    }
};

// Example: Device-specific parameter encoder
class TemperatureSensorParams {
public:
    enum class MeasurementRange {
        RANGE_0_100C = 0x01,
        RANGE_0_200C = 0x02,
        RANGE_NEG50_150C = 0x03
    };

    enum class FilterMode {
        NO_FILTER = 0x00,
        MEDIAN_FILTER = 0x01,
        AVERAGE_FILTER = 0x02
    };

    static std::vector<uint8_t> encodeRangeParameter(MeasurementRange range) {
        return std::vector<uint8_t>{static_cast<uint8_t>(range)};
    }

    static std::vector<uint8_t> encodeFilterParameter(FilterMode mode, 
                                                       uint8_t sample_count) {
        return std::vector<uint8_t>{static_cast<uint8_t>(mode), sample_count};
    }

    static std::vector<uint8_t> encodeAlarmLimits(float low_limit, 
                                                   float high_limit) {
        std::vector<uint8_t> data(8);
        // Encode as IEEE 754 float
        memcpy(data.data(), &low_limit, sizeof(float));
        memcpy(data.data() + 4, &high_limit, sizeof(float));
        return data;
    }
};

// Example usage
int main() {
    // Create parameter telegram for station 5, device ID 0x1234
    DPParameterTelegram telegram(5, 0x1234);
    
    // Set watchdog time to 500ms
    telegram.setWatchdogTime(500);

    // Add device-specific parameters for a temperature sensor
    // Parameter 0x10: Measurement range
    auto range_data = TemperatureSensorParams::encodeRangeParameter(
        TemperatureSensorParams::MeasurementRange::RANGE_0_200C
    );
    telegram.addUserParameter(0x10, range_data.data(), range_data.size());

    // Parameter 0x11: Filter settings
    auto filter_data = TemperatureSensorParams::encodeFilterParameter(
        TemperatureSensorParams::FilterMode::AVERAGE_FILTER, 8
    );
    telegram.addUserParameter(0x11, filter_data.data(), filter_data.size());

    // Parameter 0x12: Alarm limits
    auto alarm_data = TemperatureSensorParams::encodeAlarmLimits(10.0f, 180.0f);
    telegram.addUserParameter(0x12, alarm_data.data(), alarm_data.size());

    // Build and display telegram
    std::vector<uint8_t> telegram_bytes = telegram.buildTelegram();
    telegram.displayParameters();

    std::cout << "\nTelegram size: " << telegram_bytes.size() << " bytes" << std::endl;

    return 0;
}
```

### Rust Implementation

```rust
use std::mem;

/// Standard DP parameter header structure
#[repr(C, packed)]
#[derive(Debug, Clone, Copy)]
struct DpParamHeader {
    station_address: u8,
    ident_number_high: u8,
    ident_number_low: u8,
    lock_unlock: u8,
    watchdog_fact1: u8,
    watchdog_fact2: u8,
    min_tsdr: u8,
    ident_number: u16,
    group_ident: u8,
}

impl DpParamHeader {
    fn new(station_addr: u8, device_id: u16) -> Self {
        Self {
            station_address: station_addr,
            ident_number_high: ((device_id >> 8) & 0xFF) as u8,
            ident_number_low: (device_id & 0xFF) as u8,
            lock_unlock: 0x01, // Unlock
            watchdog_fact1: 10,
            watchdog_fact2: 10,
            min_tsdr: 11,
            ident_number: 0,
            group_ident: 0x00,
        }
    }

    fn set_watchdog_time(&mut self, time_ms: u16) {
        // Watchdog time = Fact1 * Fact2 * 10ms
        self.watchdog_fact1 = (time_ms / 100) as u8;
        if self.watchdog_fact1 > 0 {
            self.watchdog_fact2 = (time_ms / (self.watchdog_fact1 as u16 * 10)) as u8;
        }
    }

    fn as_bytes(&self) -> &[u8] {
        unsafe {
            std::slice::from_raw_parts(
                self as *const Self as *const u8,
                mem::size_of::<Self>(),
            )
        }
    }
}

/// User parameter structure
#[derive(Debug, Clone)]
struct UserParameter {
    param_id: u8,
    data: Vec<u8>,
}

impl UserParameter {
    fn new(param_id: u8, data: Vec<u8>) -> Result<Self, &'static str> {
        if data.is_empty() || data.len() > 244 {
            return Err("Invalid parameter length");
        }
        Ok(Self { param_id, data })
    }

    fn to_bytes(&self) -> Vec<u8> {
        let mut bytes = Vec::with_capacity(2 + self.data.len());
        bytes.push(self.param_id);
        bytes.push(self.data.len() as u8);
        bytes.extend_from_slice(&self.data);
        bytes
    }
}

/// Complete DP parameter telegram
#[derive(Debug)]
struct DpParameterTelegram {
    header: DpParamHeader,
    user_params: Vec<UserParameter>,
}

impl DpParameterTelegram {
    fn new(station_addr: u8, device_id: u16) -> Self {
        Self {
            header: DpParamHeader::new(station_addr, device_id),
            user_params: Vec::new(),
        }
    }

    fn set_watchdog_time(&mut self, time_ms: u16) {
        self.header.set_watchdog_time(time_ms);
    }

    fn add_user_parameter(&mut self, param_id: u8, data: Vec<u8>) -> Result<(), &'static str> {
        let param = UserParameter::new(param_id, data)?;
        self.user_params.push(param);
        Ok(())
    }

    fn build_telegram(&self) -> Vec<u8> {
        let mut telegram = Vec::new();

        // Add header
        telegram.extend_from_slice(self.header.as_bytes());

        // Add user parameters
        for param in &self.user_params {
            telegram.extend_from_slice(&param.to_bytes());
        }

        telegram
    }

    fn parse_telegram(data: &[u8]) -> Result<Self, &'static str> {
        if data.len() < mem::size_of::<DpParamHeader>() {
            return Err("Data too short for header");
        }

        // Parse header
        let header = unsafe {
            std::ptr::read_unaligned(data.as_ptr() as *const DpParamHeader)
        };

        let mut telegram = Self {
            header,
            user_params: Vec::new(),
        };

        // Parse user parameters
        let mut offset = mem::size_of::<DpParamHeader>();

        while offset < data.len() {
            if offset + 2 > data.len() {
                break;
            }

            let param_id = data[offset];
            let param_length = data[offset + 1] as usize;
            offset += 2;

            if offset + param_length > data.len() {
                return Err("Invalid parameter length");
            }

            let param_data = data[offset..offset + param_length].to_vec();
            telegram.user_params.push(UserParameter {
                param_id,
                data: param_data,
            });

            offset += param_length;
        }

        Ok(telegram)
    }

    fn display(&self) {
        println!("DP Parameter Telegram:");
        println!("  Station Address: {}", self.header.station_address);
        println!(
            "  Device ID: 0x{:04X}",
            (self.header.ident_number_high as u16) << 8 | self.header.ident_number_low as u16
        );
        println!(
            "  Watchdog Time: {} ms",
            self.header.watchdog_fact1 as u16 * self.header.watchdog_fact2 as u16 * 10
        );
        println!("  User Parameters: {}", self.user_params.len());
        
        for (i, param) in self.user_params.iter().enumerate() {
            println!(
                "    Param {} - ID: 0x{:02X}, Length: {}",
                i,
                param.param_id,
                param.data.len()
            );
        }
    }
}

/// Temperature sensor specific parameters
#[derive(Debug, Clone, Copy)]
enum MeasurementRange {
    Range0To100C = 0x01,
    Range0To200C = 0x02,
    RangeNeg50To150C = 0x03,
}

#[derive(Debug, Clone, Copy)]
enum FilterMode {
    NoFilter = 0x00,
    MedianFilter = 0x01,
    AverageFilter = 0x02,
}

struct TemperatureSensorParams;

impl TemperatureSensorParams {
    fn encode_range_parameter(range: MeasurementRange) -> Vec<u8> {
        vec![range as u8]
    }

    fn encode_filter_parameter(mode: FilterMode, sample_count: u8) -> Vec<u8> {
        vec![mode as u8, sample_count]
    }

    fn encode_alarm_limits(low_limit: f32, high_limit: f32) -> Vec<u8> {
        let mut data = Vec::with_capacity(8);
        data.extend_from_slice(&low_limit.to_le_bytes());
        data.extend_from_slice(&high_limit.to_le_bytes());
        data
    }
}

fn main() {
    // Create parameter telegram for station 5, device ID 0x1234
    let mut telegram = DpParameterTelegram::new(5, 0x1234);

    // Set watchdog time to 500ms
    telegram.set_watchdog_time(500);

    // Add device-specific parameters for a temperature sensor
    // Parameter 0x10: Measurement range
    let range_data = TemperatureSensorParams::encode_range_parameter(
        MeasurementRange::Range0To200C
    );
    telegram
        .add_user_parameter(0x10, range_data)
        .expect("Failed to add range parameter");

    // Parameter 0x11: Filter settings
    let filter_data = TemperatureSensorParams::encode_filter_parameter(
        FilterMode::AverageFilter,
        8
    );
    telegram
        .add_user_parameter(0x11, filter_data)
        .expect("Failed to add filter parameter");

    // Parameter 0x12: Alarm limits
    let alarm_data = TemperatureSensorParams::encode_alarm_limits(10.0, 180.0);
    telegram
        .add_user_parameter(0x12, alarm_data)
        .expect("Failed to add alarm limits");

    // Build and display telegram
    let telegram_bytes = telegram.build_telegram();
    telegram.display();

    println!("\nTelegram size: {} bytes", telegram_bytes.size());

    // Example: Parse telegram back
    match DpParameterTelegram::parse_telegram(&telegram_bytes) {
        Ok(parsed) => {
            println!("\n--- Parsed Telegram ---");
            parsed.display();
        }
        Err(e) => println!("Parse error: {}", e),
    }
}
```

## Summary

**DP Parameter Data** is the configuration mechanism in Profibus-DP that enables master stations to initialize and configure slave devices before operational data exchange. Key aspects include:

**Core Functions:**
- Transmits device-specific configuration during startup phase
- Defines operating parameters like measurement ranges, filter settings, alarm thresholds
- Establishes communication parameters (watchdog timers, response times)
- Ensures slaves operate according to application requirements

**Technical Structure:**
- Standard header containing station address, device ID, and watchdog settings
- User parameter section with device-specific configuration data
- Parameters organized according to GSD file specifications
- Typical telegram size ranges from 10 to 255 bytes

**Implementation Considerations:**
- Parameter validation against GSD-defined ranges
- Proper error handling for acknowledgment/rejection
- Support for both standard and extended parameter sets
- Persistence mechanisms for storing parameter configurations

**Best Practices:**
- Always validate parameter data before transmission
- Implement timeout handling for parameter acknowledgments
- Store parameter sets for device restoration after power cycles
- Use structured encoding/decoding for complex parameter types
- Follow manufacturer GSD specifications precisely

The code examples demonstrate building parameter telegrams with standard headers, adding device-specific user parameters (measurement ranges, filters, alarm limits), encoding/decoding parameter data, and parsing received telegrams. Both implementations show type-safe approaches to handling Profibus parameter data structures while maintaining compatibility with the protocol's binary format requirements.