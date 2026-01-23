# GSD Files (General Station Description) - Detailed Overview

## Introduction

GSD (General Station Description) files are standardized configuration files used in PROFIBUS networks to describe the characteristics, capabilities, and parameters of field devices. They serve as device profiles that enable engineering tools to properly configure and integrate PROFIBUS devices into automation systems without requiring manufacturer-specific software.

## What are GSD Files?

GSD files are ASCII text files with a defined structure that contain:

- **Device identification** (vendor ID, device type, hardware/software revision)
- **Supported PROFIBUS features** (baud rates, diagnostic capabilities)
- **Module configuration** (I/O modules, channels, data formats)
- **Parameter definitions** (configuration parameters, value ranges, defaults)
- **Diagnostic information** (alarm and diagnostic messages)

The GSD file format follows the EN 50170 standard and uses a specific syntax with keywords and values.

## Key Components of GSD Files

### 1. **General Section**
- Vendor information
- Device model and revision
- Supported PROFIBUS-DP versions
- Hardware/firmware compatibility

### 2. **Station Type Definition**
- Supported baud rates
- Slave-specific parameters
- Time monitoring values

### 3. **Module Definitions**
- Input/Output modules
- Data length and format
- Module consistency checks

### 4. **User Parameters**
- Configurable device parameters
- Value ranges and defaults
- Parameter dependencies

## Programming Examples

### C/C++ Example: GSD File Parser

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <vector>
#include <regex>

// Structure to hold GSD device information
struct GSDDeviceInfo {
    std::string vendorName;
    std::string modelName;
    uint16_t vendorId;
    uint16_t deviceId;
    std::string revision;
    std::vector<uint32_t> supportedBaudRates;
};

// Structure for module definition
struct GSDModule {
    std::string name;
    uint8_t moduleId;
    uint16_t inputLength;
    uint16_t outputLength;
    std::string dataFormat;
};

// Structure for user parameters
struct GSDParameter {
    std::string name;
    uint8_t paramId;
    std::string dataType;
    int minValue;
    int maxValue;
    int defaultValue;
    std::string unit;
};

class GSDParser {
private:
    std::map<std::string, std::string> generalParams;
    GSDDeviceInfo deviceInfo;
    std::vector<GSDModule> modules;
    std::vector<GSDParameter> parameters;

    // Trim whitespace from string
    std::string trim(const std::string& str) {
        size_t first = str.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) return "";
        size_t last = str.find_last_not_of(" \t\r\n");
        return str.substr(first, (last - first + 1));
    }

    // Parse a key-value line
    bool parseKeyValue(const std::string& line, std::string& key, std::string& value) {
        size_t pos = line.find('=');
        if (pos == std::string::npos) return false;
        
        key = trim(line.substr(0, pos));
        value = trim(line.substr(pos + 1));
        
        // Remove quotes if present
        if (value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.length() - 2);
        }
        
        return true;
    }

    // Parse baud rate list
    std::vector<uint32_t> parseBaudRates(const std::string& baudRateStr) {
        std::vector<uint32_t> rates;
        std::istringstream iss(baudRateStr);
        std::string token;
        
        while (std::getline(iss, token, ',')) {
            token = trim(token);
            if (!token.empty() && token != "0") {
                // Convert hex to decimal if needed
                uint32_t rate = std::stoul(token, nullptr, 0);
                rates.push_back(rate);
            }
        }
        return rates;
    }

    // Parse module definition
    void parseModule(const std::string& moduleStr) {
        GSDModule module;
        
        // Example format: "Module = "Digital Input" 0x10 0x04 0x00
        std::regex moduleRegex(R"("([^"]+)"\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+)\s+0x([0-9A-Fa-f]+))");
        std::smatch match;
        
        if (std::regex_search(moduleStr, match, moduleRegex)) {
            module.name = match[1];
            module.moduleId = std::stoi(match[2], nullptr, 16);
            module.inputLength = std::stoi(match[3], nullptr, 16);
            module.outputLength = std::stoi(match[4], nullptr, 16);
            modules.push_back(module);
        }
    }

public:
    bool parseFile(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            std::cerr << "Error: Cannot open GSD file: " << filename << std::endl;
            return false;
        }

        std::string line;
        std::string currentSection;

        while (std::getline(file, line)) {
            line = trim(line);
            
            // Skip comments and empty lines
            if (line.empty() || line[0] == ';' || line[0] == '#') {
                continue;
            }

            std::string key, value;
            if (!parseKeyValue(line, key, value)) {
                continue;
            }

            // Store general parameters
            generalParams[key] = value;

            // Parse specific important fields
            if (key == "Vendor_Name") {
                deviceInfo.vendorName = value;
            }
            else if (key == "Model_Name") {
                deviceInfo.modelName = value;
            }
            else if (key == "Ident_Number") {
                deviceInfo.deviceId = std::stoi(value, nullptr, 0);
            }
            else if (key == "Revision") {
                deviceInfo.revision = value;
            }
            else if (key == "9.6_supp" || key == "19.2_supp" || 
                     key == "45.45_supp" || key == "93.75_supp" ||
                     key == "187.5_supp" || key == "500_supp" ||
                     key == "1.5M_supp" || key == "3M_supp" ||
                     key == "6M_supp" || key == "12M_supp") {
                if (value == "1") {
                    std::string rate = key.substr(0, key.find("_"));
                    // Convert rate string to numeric value
                    if (rate.find("M") != std::string::npos) {
                        rate.erase(rate.find("M"), 1);
                        deviceInfo.supportedBaudRates.push_back(
                            static_cast<uint32_t>(std::stof(rate) * 1000000)
                        );
                    } else if (rate.find("k") != std::string::npos) {
                        rate.erase(rate.find("k"), 1);
                        deviceInfo.supportedBaudRates.push_back(
                            static_cast<uint32_t>(std::stof(rate) * 1000)
                        );
                    }
                }
            }
            else if (key.find("Module") == 0) {
                parseModule(value);
            }
        }

        file.close();
        return true;
    }

    // Display parsed information
    void displayDeviceInfo() const {
        std::cout << "\n===== GSD Device Information =====" << std::endl;
        std::cout << "Vendor: " << deviceInfo.vendorName << std::endl;
        std::cout << "Model: " << deviceInfo.modelName << std::endl;
        std::cout << "Device ID: 0x" << std::hex << deviceInfo.deviceId << std::dec << std::endl;
        std::cout << "Revision: " << deviceInfo.revision << std::endl;
        
        std::cout << "\nSupported Baud Rates:" << std::endl;
        for (const auto& rate : deviceInfo.supportedBaudRates) {
            if (rate >= 1000000) {
                std::cout << "  " << rate / 1000000 << " Mbps" << std::endl;
            } else {
                std::cout << "  " << rate / 1000 << " kbps" << std::endl;
            }
        }

        if (!modules.empty()) {
            std::cout << "\nAvailable Modules:" << std::endl;
            for (const auto& module : modules) {
                std::cout << "  - " << module.name 
                         << " (ID: 0x" << std::hex << (int)module.moduleId << std::dec
                         << ", In: " << module.inputLength 
                         << ", Out: " << module.outputLength << ")" << std::endl;
            }
        }
    }

    // Get device configuration for PROFIBUS master
    GSDDeviceInfo getDeviceInfo() const {
        return deviceInfo;
    }

    // Get specific parameter value
    std::string getParameter(const std::string& key) const {
        auto it = generalParams.find(key);
        if (it != generalParams.end()) {
            return it->second;
        }
        return "";
    }

    // Get all modules
    const std::vector<GSDModule>& getModules() const {
        return modules;
    }
};

// Example usage
int main() {
    GSDParser parser;
    
    // Parse GSD file
    if (parser.parseFile("example_device.gsd")) {
        parser.displayDeviceInfo();
        
        // Example: Configure device based on GSD information
        GSDDeviceInfo info = parser.getDeviceInfo();
        
        std::cout << "\n===== Device Configuration =====" << std::endl;
        std::cout << "Configuring device: " << info.modelName << std::endl;
        
        // Select highest supported baud rate
        if (!info.supportedBaudRates.empty()) {
            uint32_t maxBaud = *std::max_element(
                info.supportedBaudRates.begin(), 
                info.supportedBaudRates.end()
            );
            std::cout << "Selected baud rate: " << maxBaud << " bps" << std::endl;
        }
        
        // Display available modules for configuration
        const auto& modules = parser.getModules();
        if (!modules.empty()) {
            std::cout << "\nConfigurable modules: " << modules.size() << std::endl;
        }
    }
    
    return 0;
}
```

### Rust Example: GSD File Parser and Device Configuration

```rust
use std::collections::HashMap;
use std::fs::File;
use std::io::{BufRead, BufReader, Result};
use std::path::Path;

#[derive(Debug, Clone)]
pub struct GsdDeviceInfo {
    pub vendor_name: String,
    pub model_name: String,
    pub vendor_id: u16,
    pub device_id: u16,
    pub revision: String,
    pub supported_baud_rates: Vec<u32>,
}

#[derive(Debug, Clone)]
pub struct GsdModule {
    pub name: String,
    pub module_id: u8,
    pub input_length: u16,
    pub output_length: u16,
    pub data_format: String,
}

#[derive(Debug, Clone)]
pub struct GsdParameter {
    pub name: String,
    pub param_id: u8,
    pub data_type: String,
    pub min_value: i32,
    pub max_value: i32,
    pub default_value: i32,
    pub unit: String,
}

#[derive(Debug)]
pub struct GsdParser {
    general_params: HashMap<String, String>,
    device_info: GsdDeviceInfo,
    modules: Vec<GsdModule>,
    parameters: Vec<GsdParameter>,
}

impl GsdParser {
    pub fn new() -> Self {
        GsdParser {
            general_params: HashMap::new(),
            device_info: GsdDeviceInfo {
                vendor_name: String::new(),
                model_name: String::new(),
                vendor_id: 0,
                device_id: 0,
                revision: String::new(),
                supported_baud_rates: Vec::new(),
            },
            modules: Vec::new(),
            parameters: Vec::new(),
        }
    }

    /// Parse a GSD file from the given path
    pub fn parse_file<P: AsRef<Path>>(&mut self, path: P) -> Result<()> {
        let file = File::open(path)?;
        let reader = BufReader::new(file);

        for line in reader.lines() {
            let line = line?;
            let trimmed = line.trim();

            // Skip comments and empty lines
            if trimmed.is_empty() || trimmed.starts_with(';') || trimmed.starts_with('#') {
                continue;
            }

            if let Some((key, value)) = self.parse_key_value(trimmed) {
                self.general_params.insert(key.clone(), value.clone());
                self.process_parameter(&key, &value);
            }
        }

        Ok(())
    }

    /// Parse a key-value pair from a line
    fn parse_key_value(&self, line: &str) -> Option<(String, String)> {
        let parts: Vec<&str> = line.splitn(2, '=').collect();
        if parts.len() != 2 {
            return None;
        }

        let key = parts[0].trim().to_string();
        let mut value = parts[1].trim().to_string();

        // Remove quotes if present
        if value.starts_with('"') && value.ends_with('"') {
            value = value[1..value.len() - 1].to_string();
        }

        Some((key, value))
    }

    /// Process individual parameters and populate device info
    fn process_parameter(&mut self, key: &str, value: &str) {
        match key {
            "Vendor_Name" => {
                self.device_info.vendor_name = value.to_string();
            }
            "Model_Name" => {
                self.device_info.model_name = value.to_string();
            }
            "Ident_Number" => {
                if let Ok(id) = self.parse_number(value) {
                    self.device_info.device_id = id as u16;
                }
            }
            "Revision" => {
                self.device_info.revision = value.to_string();
            }
            _ if key.ends_with("_supp") => {
                if value == "1" {
                    self.parse_baud_rate_support(key);
                }
            }
            _ if key.starts_with("Module") => {
                if let Some(module) = self.parse_module(value) {
                    self.modules.push(module);
                }
            }
            _ if key.starts_with("PrmText") => {
                if let Some(param) = self.parse_parameter(key, value) {
                    self.parameters.push(param);
                }
            }
            _ => {}
        }
    }

    /// Parse numeric values (supporting hex and decimal)
    fn parse_number(&self, s: &str) -> Result<u32> {
        if s.starts_with("0x") || s.starts_with("0X") {
            u32::from_str_radix(&s[2..], 16)
                .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
        } else {
            s.parse::<u32>()
                .map_err(|e| std::io::Error::new(std::io::ErrorKind::InvalidData, e))
        }
    }

    /// Parse baud rate support from key name
    fn parse_baud_rate_support(&mut self, key: &str) {
        let baud_rate = match key {
            "9.6_supp" => Some(9600),
            "19.2_supp" => Some(19200),
            "45.45_supp" => Some(45450),
            "93.75_supp" => Some(93750),
            "187.5_supp" => Some(187500),
            "500_supp" => Some(500000),
            "1.5M_supp" => Some(1500000),
            "3M_supp" => Some(3000000),
            "6M_supp" => Some(6000000),
            "12M_supp" => Some(12000000),
            _ => None,
        };

        if let Some(rate) = baud_rate {
            self.device_info.supported_baud_rates.push(rate);
        }
    }

    /// Parse module definition
    fn parse_module(&self, value: &str) -> Option<GsdModule> {
        // Simplified parsing - in production, use regex or more robust parsing
        // Format: "Module Name" 0xID 0xInputLen 0xOutputLen
        let parts: Vec<&str> = value.split_whitespace().collect();
        if parts.len() < 4 {
            return None;
        }

        // Extract module name (quoted)
        let name_end = value.find('"')?.checked_add(1)?;
        let name_start = value[name_end..].find('"')? + name_end;
        let name = value[name_end..name_start].to_string();

        // Parse numeric values
        let module_id = self.parse_number(parts[parts.len() - 3]).ok()? as u8;
        let input_len = self.parse_number(parts[parts.len() - 2]).ok()? as u16;
        let output_len = self.parse_number(parts[parts.len() - 1]).ok()? as u16;

        Some(GsdModule {
            name,
            module_id,
            input_length: input_len,
            output_length: output_len,
            data_format: String::from("binary"),
        })
    }

    /// Parse parameter definition
    fn parse_parameter(&self, _key: &str, value: &str) -> Option<GsdParameter> {
        // Simplified - actual GSD parameter parsing is more complex
        Some(GsdParameter {
            name: value.to_string(),
            param_id: 0,
            data_type: String::from("uint8"),
            min_value: 0,
            max_value: 255,
            default_value: 0,
            unit: String::new(),
        })
    }

    /// Display parsed device information
    pub fn display_device_info(&self) {
        println!("\n===== GSD Device Information =====");
        println!("Vendor: {}", self.device_info.vendor_name);
        println!("Model: {}", self.device_info.model_name);
        println!("Device ID: 0x{:04X}", self.device_info.device_id);
        println!("Revision: {}", self.device_info.revision);

        println!("\nSupported Baud Rates:");
        for rate in &self.device_info.supported_baud_rates {
            if *rate >= 1_000_000 {
                println!("  {} Mbps", rate / 1_000_000);
            } else {
                println!("  {} kbps", rate / 1_000);
            }
        }

        if !self.modules.is_empty() {
            println!("\nAvailable Modules:");
            for module in &self.modules {
                println!(
                    "  - {} (ID: 0x{:02X}, In: {}, Out: {})",
                    module.name, module.module_id, module.input_length, module.output_length
                );
            }
        }

        if !self.parameters.is_empty() {
            println!("\nConfigurable Parameters: {}", self.parameters.len());
        }
    }

    /// Get device information
    pub fn get_device_info(&self) -> &GsdDeviceInfo {
        &self.device_info
    }

    /// Get modules
    pub fn get_modules(&self) -> &[GsdModule] {
        &self.modules
    }

    /// Get a specific parameter value
    pub fn get_parameter(&self, key: &str) -> Option<&String> {
        self.general_params.get(key)
    }
}

/// Device configuration builder using GSD information
pub struct DeviceConfigurator {
    device_info: GsdDeviceInfo,
    selected_baud_rate: u32,
    active_modules: Vec<GsdModule>,
}

impl DeviceConfigurator {
    pub fn new(device_info: GsdDeviceInfo) -> Self {
        // Select highest supported baud rate by default
        let selected_baud_rate = device_info
            .supported_baud_rates
            .iter()
            .max()
            .copied()
            .unwrap_or(9600);

        DeviceConfigurator {
            device_info,
            selected_baud_rate,
            active_modules: Vec::new(),
        }
    }

    pub fn set_baud_rate(&mut self, rate: u32) -> Result<()> {
        if self.device_info.supported_baud_rates.contains(&rate) {
            self.selected_baud_rate = rate;
            Ok(())
        } else {
            Err(std::io::Error::new(
                std::io::ErrorKind::InvalidInput,
                "Unsupported baud rate",
            ))
        }
    }

    pub fn add_module(&mut self, module: GsdModule) {
        self.active_modules.push(module);
    }

    pub fn generate_config(&self) -> Vec<u8> {
        let mut config = Vec::new();

        // Example configuration telegram generation
        config.push((self.device_info.device_id >> 8) as u8);
        config.push((self.device_info.device_id & 0xFF) as u8);

        // Add baud rate configuration
        let baud_code = self.baud_rate_to_code(self.selected_baud_rate);
        config.push(baud_code);

        // Add module configuration
        config.push(self.active_modules.len() as u8);
        for module in &self.active_modules {
            config.push(module.module_id);
            config.push((module.input_length >> 8) as u8);
            config.push((module.input_length & 0xFF) as u8);
            config.push((module.output_length >> 8) as u8);
            config.push((module.output_length & 0xFF) as u8);
        }

        config
    }

    fn baud_rate_to_code(&self, rate: u32) -> u8 {
        match rate {
            9600 => 0x00,
            19200 => 0x01,
            93750 => 0x02,
            187500 => 0x03,
            500000 => 0x04,
            1500000 => 0x06,
            3000000 => 0x07,
            6000000 => 0x08,
            12000000 => 0x09,
            _ => 0x00,
        }
    }

    pub fn display_config(&self) {
        println!("\n===== Device Configuration =====");
        println!("Device: {}", self.device_info.model_name);
        println!("Baud Rate: {} bps", self.selected_baud_rate);
        println!("Active Modules: {}", self.active_modules.len());
        for (idx, module) in self.active_modules.iter().enumerate() {
            println!("  Module {}: {}", idx + 1, module.name);
        }
    }
}

// Example usage
fn main() -> Result<()> {
    let mut parser = GsdParser::new();

    // Parse GSD file
    if let Err(e) = parser.parse_file("example_device.gsd") {
        eprintln!("Error parsing GSD file: {}", e);
        eprintln!("Creating example configuration with default values...");
        
        // Create example device info for demonstration
        parser.device_info = GsdDeviceInfo {
            vendor_name: "Example Vendor".to_string(),
            model_name: "PROFIBUS-DP Device".to_string(),
            vendor_id: 0x1234,
            device_id: 0x5678,
            revision: "V1.0".to_string(),
            supported_baud_rates: vec![9600, 19200, 187500, 500000, 1500000],
        };
    }

    parser.display_device_info();

    // Configure device based on GSD information
    let mut configurator = DeviceConfigurator::new(parser.get_device_info().clone());
    
    // Add modules if available
    for module in parser.get_modules() {
        configurator.add_module(module.clone());
    }

    configurator.display_config();

    // Generate configuration telegram
    let config_data = configurator.generate_config();
    println!("\nGenerated Configuration Telegram:");
    println!("  {:02X?}", config_data);

    Ok(())
}
```

## Summary

**GSD (General Station Description) files** are essential components in PROFIBUS networks that provide standardized device descriptions for automation tools. They enable plug-and-play integration of field devices without proprietary software.

### Key Takeaways:

1. **Purpose**: GSD files define device capabilities, parameters, and configuration options in a standardized ASCII format

2. **Content Structure**:
   - Device identification and vendor information
   - Supported PROFIBUS features (baud rates, protocols)
   - Module definitions (I/O configurations)
   - Parameter specifications (ranges, defaults, units)
   - Diagnostic capabilities

3. **Programming Applications**:
   - **Parsing**: Extract device metadata, supported features, and configuration options
   - **Configuration**: Generate proper parameterization telegrams for device setup
   - **Validation**: Verify compatibility between master and slave devices
   - **Tool Development**: Create engineering tools that auto-configure PROFIBUS networks

4. **Implementation Considerations**:
   - Handle various GSD format versions (GSD Revision 1-6)
   - Parse complex syntax including conditional parameters
   - Support module composition and slot configuration
   - Validate parameter ranges and dependencies
   - Generate correct byte sequences for device parameterization

5. **Best Practices**:
   - Always validate GSD files before use
   - Cache parsed data for performance
   - Implement error handling for malformed files
   - Support both legacy and modern GSD formats
   - Provide user-friendly representations of technical data

The code examples demonstrate complete GSD parsers in both C++ and Rust, showing how to extract device information, parse modules and parameters, and use this data to generate proper device configurations for PROFIBUS-DP networks.