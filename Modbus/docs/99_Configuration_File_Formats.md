# Modbus Configuration File Formats

## Detailed Description

Configuration file formats are essential for managing Modbus device parameters, communication settings, and system configurations in a structured, human-readable, and machine-parsable way. Rather than hardcoding device addresses, register maps, and communication parameters directly into applications, modern Modbus implementations use external configuration files to store this information.

### Why Configuration Files Matter

In industrial automation systems, Modbus networks often contain dozens or hundreds of devices, each with unique:
- **Communication parameters**: Baud rate, parity, stop bits, timeout values
- **Device addressing**: Slave IDs, IP addresses, port numbers
- **Register mappings**: Data point addresses, data types, scaling factors
- **Application logic**: Polling intervals, alarm thresholds, units of measurement

Configuration files allow operators and engineers to modify these parameters without recompiling code, making systems more flexible, maintainable, and deployable across different installations.

### Common Configuration File Formats

**JSON (JavaScript Object Notation)**
- Lightweight, human-readable format
- Native support in modern programming languages
- Hierarchical structure ideal for complex configurations
- Supports nested objects and arrays
- Popular in web-based SCADA systems and modern applications

**XML (Extensible Markup Language)**
- Highly structured with schema validation capabilities
- Industry-standard in many legacy industrial systems
- Self-documenting with attributes and elements
- Good for complex, deeply nested configurations
- Supports comments and metadata

**CSV (Comma-Separated Values)**
- Simple tabular format
- Excellent for register maps and device lists
- Easy to edit in spreadsheet applications
- Limited to flat data structures
- Fast parsing performance

### Typical Configuration Elements

A comprehensive Modbus configuration typically includes:

1. **Connection Settings**: Protocol type (RTU/TCP), COM port, baud rate, IP address, port number
2. **Device Information**: Slave ID, device name, manufacturer, model
3. **Register Map**: Register addresses, function codes, data types, access levels
4. **Timing Parameters**: Read/write timeouts, polling intervals, retry counts
5. **Data Processing**: Scaling factors, offset values, units, limits
6. **Application Settings**: Logging levels, alarm configurations, UI preferences

---

## C/C++ Implementation

### JSON Configuration with nlohmann/json

```cpp
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

// Structure to hold Modbus device configuration
struct ModbusRegister {
    uint16_t address;
    std::string name;
    std::string dataType;
    double scaleFactor;
    std::string unit;
};

struct ModbusDevice {
    uint8_t slaveId;
    std::string name;
    std::string ipAddress;
    uint16_t port;
    std::vector<ModbusRegister> registers;
};

struct ModbusConfig {
    std::string protocol;  // "RTU" or "TCP"
    int timeout;
    int retries;
    std::vector<ModbusDevice> devices;
};

// Parse JSON configuration
ModbusConfig loadJsonConfig(const std::string& filename) {
    ModbusConfig config;
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open configuration file: " + filename);
    }
    
    json j;
    file >> j;
    
    config.protocol = j["modbus"]["protocol"];
    config.timeout = j["modbus"]["timeout"];
    config.retries = j["modbus"]["retries"];
    
    for (const auto& device : j["modbus"]["devices"]) {
        ModbusDevice dev;
        dev.slaveId = device["slaveId"];
        dev.name = device["name"];
        
        if (device.contains("ipAddress")) {
            dev.ipAddress = device["ipAddress"];
            dev.port = device["port"];
        }
        
        for (const auto& reg : device["registers"]) {
            ModbusRegister r;
            r.address = reg["address"];
            r.name = reg["name"];
            r.dataType = reg["dataType"];
            r.scaleFactor = reg.value("scaleFactor", 1.0);
            r.unit = reg.value("unit", "");
            dev.registers.push_back(r);
        }
        
        config.devices.push_back(dev);
    }
    
    return config;
}

// Example: Create a sample JSON configuration
void createSampleJsonConfig(const std::string& filename) {
    json config = {
        {"modbus", {
            {"protocol", "TCP"},
            {"timeout", 1000},
            {"retries", 3},
            {"devices", json::array({
                {
                    {"slaveId", 1},
                    {"name", "Temperature Sensor"},
                    {"ipAddress", "192.168.1.100"},
                    {"port", 502},
                    {"registers", json::array({
                        {
                            {"address", 0},
                            {"name", "temperature"},
                            {"dataType", "float"},
                            {"scaleFactor", 0.1},
                            {"unit", "°C"}
                        },
                        {
                            {"address", 2},
                            {"name", "humidity"},
                            {"dataType", "uint16"},
                            {"scaleFactor", 0.01},
                            {"unit", "%"}
                        }
                    })}
                },
                {
                    {"slaveId", 2},
                    {"name", "Pressure Transmitter"},
                    {"ipAddress", "192.168.1.101"},
                    {"port", 502},
                    {"registers", json::array({
                        {
                            {"address", 0},
                            {"name", "pressure"},
                            {"dataType", "float"},
                            {"scaleFactor", 1.0},
                            {"unit", "bar"}
                        }
                    })}
                }
            })}
        }}
    };
    
    std::ofstream file(filename);
    file << config.dump(4);  // Pretty print with 4-space indent
}

int main() {
    try {
        // Create sample configuration
        createSampleJsonConfig("modbus_config.json");
        
        // Load and parse configuration
        ModbusConfig config = loadJsonConfig("modbus_config.json");
        
        // Display loaded configuration
        std::cout << "Modbus Protocol: " << config.protocol << std::endl;
        std::cout << "Timeout: " << config.timeout << " ms" << std::endl;
        std::cout << "Devices: " << config.devices.size() << std::endl;
        
        for (const auto& device : config.devices) {
            std::cout << "\nDevice: " << device.name << std::endl;
            std::cout << "  Slave ID: " << (int)device.slaveId << std::endl;
            std::cout << "  IP: " << device.ipAddress << ":" << device.port << std::endl;
            std::cout << "  Registers:" << std::endl;
            
            for (const auto& reg : device.registers) {
                std::cout << "    - " << reg.name << " @ " << reg.address
                         << " (" << reg.dataType << ") scale=" << reg.scaleFactor
                         << " " << reg.unit << std::endl;
            }
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

### XML Configuration with TinyXML-2

```cpp
#include <iostream>
#include <vector>
#include "tinyxml2.h"

using namespace tinyxml2;

// Create sample XML configuration
void createSampleXmlConfig(const std::string& filename) {
    XMLDocument doc;
    
    XMLElement* root = doc.NewElement("ModbusConfiguration");
    doc.InsertFirstChild(root);
    
    XMLElement* settings = doc.NewElement("Settings");
    settings->SetAttribute("protocol", "TCP");
    settings->SetAttribute("timeout", 1000);
    settings->SetAttribute("retries", 3);
    root->InsertEndChild(settings);
    
    XMLElement* devices = doc.NewElement("Devices");
    root->InsertEndChild(devices);
    
    // Device 1
    XMLElement* device1 = doc.NewElement("Device");
    device1->SetAttribute("slaveId", 1);
    device1->SetAttribute("name", "Temperature Sensor");
    device1->SetAttribute("ipAddress", "192.168.1.100");
    device1->SetAttribute("port", 502);
    
    XMLElement* registers1 = doc.NewElement("Registers");
    
    XMLElement* reg1 = doc.NewElement("Register");
    reg1->SetAttribute("address", 0);
    reg1->SetAttribute("name", "temperature");
    reg1->SetAttribute("dataType", "float");
    reg1->SetAttribute("scaleFactor", 0.1);
    reg1->SetAttribute("unit", "°C");
    registers1->InsertEndChild(reg1);
    
    device1->InsertEndChild(registers1);
    devices->InsertEndChild(device1);
    
    doc.SaveFile(filename.c_str());
}

// Parse XML configuration
void loadXmlConfig(const std::string& filename) {
    XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != XML_SUCCESS) {
        throw std::runtime_error("Cannot load XML file");
    }
    
    XMLElement* root = doc.FirstChildElement("ModbusConfiguration");
    XMLElement* settings = root->FirstChildElement("Settings");
    
    std::cout << "Protocol: " << settings->Attribute("protocol") << std::endl;
    std::cout << "Timeout: " << settings->IntAttribute("timeout") << " ms" << std::endl;
    
    XMLElement* devices = root->FirstChildElement("Devices");
    for (XMLElement* device = devices->FirstChildElement("Device");
         device != nullptr;
         device = device->NextSiblingElement("Device")) {
        
        std::cout << "\nDevice: " << device->Attribute("name") << std::endl;
        std::cout << "  Slave ID: " << device->IntAttribute("slaveId") << std::endl;
        std::cout << "  IP: " << device->Attribute("ipAddress") 
                  << ":" << device->IntAttribute("port") << std::endl;
        
        XMLElement* registers = device->FirstChildElement("Registers");
        for (XMLElement* reg = registers->FirstChildElement("Register");
             reg != nullptr;
             reg = reg->NextSiblingElement("Register")) {
            
            std::cout << "    - " << reg->Attribute("name")
                     << " @ " << reg->IntAttribute("address")
                     << " (" << reg->Attribute("dataType") << ")" << std::endl;
        }
    }
}
```

### CSV Configuration Parser

```cpp
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>

struct RegisterMapEntry {
    uint16_t address;
    std::string name;
    std::string dataType;
    std::string accessMode;  // R, W, RW
    double scaleFactor;
    std::string unit;
};

// Parse CSV register map
std::vector<RegisterMapEntry> loadCsvRegisterMap(const std::string& filename) {
    std::vector<RegisterMapEntry> registerMap;
    std::ifstream file(filename);
    
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open CSV file: " + filename);
    }
    
    std::string line;
    std::getline(file, line);  // Skip header
    
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string token;
        RegisterMapEntry entry;
        
        std::getline(ss, token, ',');
        entry.address = std::stoi(token);
        
        std::getline(ss, entry.name, ',');
        std::getline(ss, entry.dataType, ',');
        std::getline(ss, entry.accessMode, ',');
        
        std::getline(ss, token, ',');
        entry.scaleFactor = std::stod(token);
        
        std::getline(ss, entry.unit, ',');
        
        registerMap.push_back(entry);
    }
    
    return registerMap;
}

// Create sample CSV file
void createSampleCsv(const std::string& filename) {
    std::ofstream file(filename);
    
    file << "Address,Name,DataType,Access,ScaleFactor,Unit\n";
    file << "0,Temperature,float,R,0.1,°C\n";
    file << "2,Humidity,uint16,R,0.01,%\n";
    file << "4,Setpoint,float,RW,1.0,°C\n";
    file << "6,ControlMode,uint16,RW,1.0,\n";
    
    file.close();
}
```

---

## Rust Implementation

```rust
use serde::{Deserialize, Serialize};
use std::fs;
use std::error::Error;

// JSON Configuration Structures
#[derive(Debug, Serialize, Deserialize)]
struct ModbusRegister {
    address: u16,
    name: String,
    #[serde(rename = "dataType")]
    data_type: String,
    #[serde(default = "default_scale")]
    scale_factor: f64,
    #[serde(default)]
    unit: String,
}

fn default_scale() -> f64 {
    1.0
}

#[derive(Debug, Serialize, Deserialize)]
struct ModbusDevice {
    #[serde(rename = "slaveId")]
    slave_id: u8,
    name: String,
    #[serde(rename = "ipAddress")]
    ip_address: Option<String>,
    port: Option<u16>,
    registers: Vec<ModbusRegister>,
}

#[derive(Debug, Serialize, Deserialize)]
struct ModbusSettings {
    protocol: String,
    timeout: u32,
    retries: u32,
    devices: Vec<ModbusDevice>,
}

#[derive(Debug, Serialize, Deserialize)]
struct ModbusConfig {
    modbus: ModbusSettings,
}

// Load JSON configuration
fn load_json_config(filename: &str) -> Result<ModbusConfig, Box<dyn Error>> {
    let contents = fs::read_to_string(filename)?;
    let config: ModbusConfig = serde_json::from_str(&contents)?;
    Ok(config)
}

// Create sample JSON configuration
fn create_sample_json_config(filename: &str) -> Result<(), Box<dyn Error>> {
    let config = ModbusConfig {
        modbus: ModbusSettings {
            protocol: "TCP".to_string(),
            timeout: 1000,
            retries: 3,
            devices: vec![
                ModbusDevice {
                    slave_id: 1,
                    name: "Temperature Sensor".to_string(),
                    ip_address: Some("192.168.1.100".to_string()),
                    port: Some(502),
                    registers: vec![
                        ModbusRegister {
                            address: 0,
                            name: "temperature".to_string(),
                            data_type: "float".to_string(),
                            scale_factor: 0.1,
                            unit: "°C".to_string(),
                        },
                        ModbusRegister {
                            address: 2,
                            name: "humidity".to_string(),
                            data_type: "uint16".to_string(),
                            scale_factor: 0.01,
                            unit: "%".to_string(),
                        },
                    ],
                },
                ModbusDevice {
                    slave_id: 2,
                    name: "Pressure Transmitter".to_string(),
                    ip_address: Some("192.168.1.101".to_string()),
                    port: Some(502),
                    registers: vec![
                        ModbusRegister {
                            address: 0,
                            name: "pressure".to_string(),
                            data_type: "float".to_string(),
                            scale_factor: 1.0,
                            unit: "bar".to_string(),
                        },
                    ],
                },
            ],
        },
    };
    
    let json = serde_json::to_string_pretty(&config)?;
    fs::write(filename, json)?;
    Ok(())
}

// Display configuration
fn display_config(config: &ModbusConfig) {
    println!("Modbus Protocol: {}", config.modbus.protocol);
    println!("Timeout: {} ms", config.modbus.timeout);
    println!("Devices: {}", config.modbus.devices.len());
    
    for device in &config.modbus.devices {
        println!("\nDevice: {}", device.name);
        println!("  Slave ID: {}", device.slave_id);
        if let (Some(ip), Some(port)) = (&device.ip_address, device.port) {
            println!("  IP: {}:{}", ip, port);
        }
        println!("  Registers:");
        
        for reg in &device.registers {
            println!(
                "    - {} @ {} ({}) scale={} {}",
                reg.name, reg.address, reg.data_type, reg.scale_factor, reg.unit
            );
        }
    }
}

// XML Configuration using quick-xml
use quick_xml::events::{Event, BytesStart, BytesText};
use quick_xml::{Reader, Writer};
use std::io::Cursor;

fn create_sample_xml_config(filename: &str) -> Result<(), Box<dyn Error>> {
    let mut writer = Writer::new(Cursor::new(Vec::new()));
    
    // Root element
    let mut root = BytesStart::new("ModbusConfiguration");
    writer.write_event(Event::Start(root.to_borrowed()))?;
    
    // Settings
    let mut settings = BytesStart::new("Settings");
    settings.push_attribute(("protocol", "TCP"));
    settings.push_attribute(("timeout", "1000"));
    settings.push_attribute(("retries", "3"));
    writer.write_event(Event::Empty(settings))?;
    
    // Devices
    let devices = BytesStart::new("Devices");
    writer.write_event(Event::Start(devices.to_borrowed()))?;
    
    // Device 1
    let mut device = BytesStart::new("Device");
    device.push_attribute(("slaveId", "1"));
    device.push_attribute(("name", "Temperature Sensor"));
    device.push_attribute(("ipAddress", "192.168.1.100"));
    device.push_attribute(("port", "502"));
    writer.write_event(Event::Start(device.to_borrowed()))?;
    
    // Registers
    let registers = BytesStart::new("Registers");
    writer.write_event(Event::Start(registers.to_borrowed()))?;
    
    let mut reg = BytesStart::new("Register");
    reg.push_attribute(("address", "0"));
    reg.push_attribute(("name", "temperature"));
    reg.push_attribute(("dataType", "float"));
    reg.push_attribute(("scaleFactor", "0.1"));
    reg.push_attribute(("unit", "°C"));
    writer.write_event(Event::Empty(reg))?;
    
    writer.write_event(Event::End(registers.to_end()))?;
    writer.write_event(Event::End(device.to_end()))?;
    writer.write_event(Event::End(devices.to_end()))?;
    writer.write_event(Event::End(root.to_end()))?;
    
    let result = writer.into_inner().into_inner();
    fs::write(filename, result)?;
    Ok(())
}

// CSV Configuration using csv crate
use csv::{Reader as CsvReader, Writer as CsvWriter};

#[derive(Debug, Deserialize, Serialize)]
struct RegisterMapEntry {
    address: u16,
    name: String,
    data_type: String,
    access: String,
    scale_factor: f64,
    unit: String,
}

fn load_csv_register_map(filename: &str) -> Result<Vec<RegisterMapEntry>, Box<dyn Error>> {
    let mut rdr = CsvReader::from_path(filename)?;
    let mut entries = Vec::new();
    
    for result in rdr.deserialize() {
        let entry: RegisterMapEntry = result?;
        entries.push(entry);
    }
    
    Ok(entries)
}

fn create_sample_csv(filename: &str) -> Result<(), Box<dyn Error>> {
    let mut wtr = CsvWriter::from_path(filename)?;
    
    wtr.write_record(&["address", "name", "data_type", "access", "scale_factor", "unit"])?;
    wtr.write_record(&["0", "Temperature", "float", "R", "0.1", "°C"])?;
    wtr.write_record(&["2", "Humidity", "uint16", "R", "0.01", "%"])?;
    wtr.write_record(&["4", "Setpoint", "float", "RW", "1.0", "°C"])?;
    wtr.write_record(&["6", "ControlMode", "uint16", "RW", "1.0", ""])?;
    
    wtr.flush()?;
    Ok(())
}

// Main function demonstrating usage
fn main() -> Result<(), Box<dyn Error>> {
    // JSON Configuration
    println!("=== JSON Configuration ===");
    create_sample_json_config("modbus_config.json")?;
    let config = load_json_config("modbus_config.json")?;
    display_config(&config);
    
    // XML Configuration
    println!("\n=== XML Configuration ===");
    create_sample_xml_config("modbus_config.xml")?;
    println!("XML configuration created successfully");
    
    // CSV Configuration
    println!("\n=== CSV Register Map ===");
    create_sample_csv("register_map.csv")?;
    let register_map = load_csv_register_map("register_map.csv")?;
    
    for entry in register_map {
        println!(
            "{} @ {} ({}) - {} scale={} {}",
            entry.name, entry.address, entry.data_type,
            entry.access, entry.scale_factor, entry.unit
        );
    }
    
    Ok(())
}

// Cargo.toml dependencies:
// [dependencies]
// serde = { version = "1.0", features = ["derive"] }
// serde_json = "1.0"
// quick-xml = "0.31"
// csv = "1.3"
```

---

## Summary

**Configuration file formats** are fundamental to professional Modbus implementations, enabling flexible, maintainable, and scalable industrial automation systems. The three primary formats each serve specific use cases:

- **JSON** excels in modern applications with its lightweight syntax, native language support, and hierarchical structure ideal for complex device configurations
- **XML** provides robust schema validation and self-documentation, making it suitable for enterprise systems requiring strict data integrity
- **CSV** offers simplicity and spreadsheet compatibility, perfect for register maps and tabular device data

Proper configuration management separates device parameters from application logic, allowing operators to modify communication settings, register mappings, and device information without code changes. This approach reduces deployment time, minimizes errors, and improves system maintainability across multiple installations. Both C/C++ and Rust ecosystems provide excellent libraries (nlohmann/json, TinyXML-2, serde, quick-xml, csv) that make parsing and generating these configuration formats straightforward and type-safe.