# 94. Cellular Network Programming

## Detailed Description

Cellular Network Programming involves developing software to interact with LTE (Long-Term Evolution) and 5G cellular networks for data connectivity, network selection, signal monitoring, and mobile broadband applications. Unlike traditional TCP/IP programming that assumes an established network connection, cellular programming requires managing the physical and link layers of mobile networks, handling network attachment procedures, monitoring signal quality, and managing network selection between different carriers and technologies (2G/3G/4G/5G).

### Key Concepts

**Network Attachment and Registration:**
- **PLMN Selection**: Public Land Mobile Network identification and selection (MCC+MNC codes)
- **Network Registration**: Attaching to cellular networks and managing registration states
- **APN Configuration**: Access Point Name settings for data connections
- **Bearer Management**: Managing data bearers and QoS (Quality of Service) parameters

**Signal and Network Monitoring:**
- **Signal Strength**: RSSI (Received Signal Strength Indicator), RSRP (Reference Signal Received Power)
- **Signal Quality**: RSRQ (Reference Signal Received Quality), SINR (Signal-to-Interference-plus-Noise Ratio)
- **Cell Information**: Cell ID, tracking area code, neighboring cells
- **Network Technology**: Detection of 2G/3G/4G/5G capabilities and active technology

**AT Commands Interface:**
Most cellular modems expose control through AT commands (originally from Hayes modems). Common command sets include:
- Standard AT commands (3GPP TS 27.007, 27.005)
- Vendor-specific extensions (Qualcomm, Sierra Wireless, Telit, etc.)
- Commands for network operations, SMS, GPS, and more

**Network Selection Strategies:**
- **Automatic Selection**: Modem automatically selects best available network
- **Manual Selection**: Application specifies preferred PLMN
- **Priority Lists**: Configuring preferred network operators
- **Technology Preference**: Selecting between 2G/3G/4G/5G based on requirements

### Common Use Cases

1. **IoT Devices**: Remote sensors, asset tracking, smart meters
2. **Mobile Routers**: 4G/5G hotspots and failover connections
3. **Vehicle Connectivity**: Telematics and connected car systems
4. **Industrial Automation**: Remote monitoring and control systems
5. **Emergency Services**: Backup connectivity for critical infrastructure

## Programming Implementation

### C/C++ Implementation

```c
// cellular_modem.h
#ifndef CELLULAR_MODEM_H
#define CELLULAR_MODEM_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>
#include <stdbool.h>

// Network registration states
typedef enum {
    REG_NOT_REGISTERED = 0,
    REG_HOME_NETWORK = 1,
    REG_SEARCHING = 2,
    REG_DENIED = 3,
    REG_UNKNOWN = 4,
    REG_ROAMING = 5
} RegistrationState;

// Network access technology
typedef enum {
    ACT_GSM = 0,
    ACT_GSM_COMPACT = 1,
    ACT_UTRAN = 2,
    ACT_GSM_EGPRS = 3,
    ACT_UTRAN_HSDPA = 4,
    ACT_UTRAN_HSUPA = 5,
    ACT_UTRAN_HSDPA_HSUPA = 6,
    ACT_E_UTRAN = 7,  // LTE
    ACT_NR = 10       // 5G NR
} AccessTechnology;

// Signal quality information
typedef struct {
    int rssi;           // Signal strength in dBm
    int rsrp;           // Reference Signal Received Power (LTE/5G)
    int rsrq;           // Reference Signal Received Quality (LTE/5G)
    int sinr;           // Signal to Interference plus Noise Ratio
    int ber;            // Bit Error Rate
} SignalQuality;

// Network information
typedef struct {
    char plmn[8];       // Mobile Country Code + Mobile Network Code
    char operator_name[32];
    AccessTechnology act;
    RegistrationState reg_state;
    int cell_id;
    int lac;            // Location Area Code
} NetworkInfo;

// Cellular modem structure
typedef struct {
    int fd;                     // File descriptor for serial port
    char device_path[256];      // Device path (e.g., /dev/ttyUSB2)
    bool is_connected;
    NetworkInfo network_info;
    SignalQuality signal_quality;
    char response_buffer[4096];
} CellularModem;

// Function prototypes
int cellular_modem_init(CellularModem *modem, const char *device_path);
int cellular_modem_close(CellularModem *modem);
int cellular_send_at_command(CellularModem *modem, const char *command, 
                             char *response, size_t response_size, int timeout_ms);
int cellular_get_signal_quality(CellularModem *modem, SignalQuality *quality);
int cellular_get_network_info(CellularModem *modem, NetworkInfo *info);
int cellular_set_network_mode(CellularModem *modem, const char *mode);
int cellular_scan_networks(CellularModem *modem);
int cellular_connect_network(CellularModem *modem, const char *apn, 
                             const char *username, const char *password);
int cellular_disconnect_network(CellularModem *modem);
bool cellular_is_registered(CellularModem *modem);

#endif // CELLULAR_MODEM_H
```

```c
// cellular_modem.c
#include "cellular_modem.h"

// Initialize cellular modem
int cellular_modem_init(CellularModem *modem, const char *device_path) {
    struct termios tty;
    
    memset(modem, 0, sizeof(CellularModem));
    strncpy(modem->device_path, device_path, sizeof(modem->device_path) - 1);
    
    // Open serial port
    modem->fd = open(device_path, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (modem->fd < 0) {
        perror("Error opening serial port");
        return -1;
    }
    
    // Configure serial port
    if (tcgetattr(modem->fd, &tty) != 0) {
        perror("Error getting terminal attributes");
        close(modem->fd);
        return -1;
    }
    
    // Set baud rate to 115200
    cfsetospeed(&tty, B115200);
    cfsetispeed(&tty, B115200);
    
    // 8N1 mode
    tty.c_cflag &= ~PARENB;        // No parity
    tty.c_cflag &= ~CSTOPB;        // 1 stop bit
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |= CS8;            // 8 data bits
    tty.c_cflag &= ~CRTSCTS;       // No hardware flow control
    tty.c_cflag |= CREAD | CLOCAL; // Enable receiver, ignore modem control lines
    
    // Raw mode
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_oflag &= ~OPOST;
    
    // Set timeouts
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 10;  // 1 second timeout
    
    if (tcsetattr(modem->fd, TCSANOW, &tty) != 0) {
        perror("Error setting terminal attributes");
        close(modem->fd);
        return -1;
    }
    
    // Initialize modem with basic AT commands
    usleep(500000); // Wait for modem to be ready
    
    cellular_send_at_command(modem, "ATZ\r", NULL, 0, 2000);      // Reset
    usleep(500000);
    cellular_send_at_command(modem, "ATE0\r", NULL, 0, 2000);     // Echo off
    cellular_send_at_command(modem, "AT+CMEE=2\r", NULL, 0, 2000); // Verbose errors
    
    printf("Cellular modem initialized on %s\n", device_path);
    return 0;
}

// Send AT command and receive response
int cellular_send_at_command(CellularModem *modem, const char *command,
                             char *response, size_t response_size, int timeout_ms) {
    fd_set read_fds;
    struct timeval timeout;
    char buffer[256];
    int total_bytes = 0;
    
    if (modem->fd < 0) {
        return -1;
    }
    
    // Clear any pending data
    tcflush(modem->fd, TCIOFLUSH);
    
    // Send command
    ssize_t written = write(modem->fd, command, strlen(command));
    if (written < 0) {
        perror("Error writing to serial port");
        return -1;
    }
    
    if (response == NULL) {
        usleep(100000); // Give modem time to process
        return 0;
    }
    
    // Read response
    memset(response, 0, response_size);
    timeout.tv_sec = timeout_ms / 1000;
    timeout.tv_usec = (timeout_ms % 1000) * 1000;
    
    while (total_bytes < response_size - 1) {
        FD_ZERO(&read_fds);
        FD_SET(modem->fd, &read_fds);
        
        int ret = select(modem->fd + 1, &read_fds, NULL, NULL, &timeout);
        if (ret < 0) {
            perror("Select error");
            return -1;
        } else if (ret == 0) {
            break; // Timeout
        }
        
        ssize_t bytes_read = read(modem->fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            buffer[bytes_read] = '\0';
            strncat(response, buffer, response_size - total_bytes - 1);
            total_bytes += bytes_read;
            
            // Check for completion
            if (strstr(response, "OK\r\n") || strstr(response, "ERROR\r\n")) {
                break;
            }
        }
    }
    
    return total_bytes;
}

// Get signal quality
int cellular_get_signal_quality(CellularModem *modem, SignalQuality *quality) {
    char response[512];
    int rssi_raw, ber;
    
    memset(quality, 0, sizeof(SignalQuality));
    
    // Get basic signal quality (GSM/UMTS)
    if (cellular_send_at_command(modem, "AT+CSQ\r", response, 
                                 sizeof(response), 2000) < 0) {
        return -1;
    }
    
    // Parse: +CSQ: <rssi>,<ber>
    if (sscanf(response, "+CSQ: %d,%d", &rssi_raw, &ber) == 2) {
        // Convert to dBm (approximate)
        if (rssi_raw == 99) {
            quality->rssi = -999; // Unknown
        } else {
            quality->rssi = -113 + (rssi_raw * 2);
        }
        quality->ber = ber;
    }
    
    // Get LTE signal quality (if available)
    if (cellular_send_at_command(modem, "AT+CESQ\r", response, 
                                 sizeof(response), 2000) > 0) {
        int rxlev, ber2, rscp, ecno, rsrq, rsrp;
        // Parse: +CESQ: <rxlev>,<ber>,<rscp>,<ecno>,<rsrq>,<rsrp>
        if (sscanf(response, "+CESQ: %d,%d,%d,%d,%d,%d", 
                   &rxlev, &ber2, &rscp, &ecno, &rsrq, &rsrp) == 6) {
            if (rsrq != 255) {
                quality->rsrq = -20 + (rsrq * 0.5);  // Convert to dB
            }
            if (rsrp != 255) {
                quality->rsrp = -140 + rsrp;  // Convert to dBm
            }
        }
    }
    
    return 0;
}

// Get network registration information
int cellular_get_network_info(CellularModem *modem, NetworkInfo *info) {
    char response[512];
    int n, stat, act;
    char lac[8], ci[16];
    
    memset(info, 0, sizeof(NetworkInfo));
    
    // Get registration status
    if (cellular_send_at_command(modem, "AT+CEREG?\r", response, 
                                 sizeof(response), 2000) < 0) {
        // Try older command for 2G/3G
        cellular_send_at_command(modem, "AT+CREG?\r", response, 
                                sizeof(response), 2000);
    }
    
    // Parse: +CEREG: <n>,<stat>[,<lac>,<ci>[,<AcT>]]
    if (sscanf(response, "+CEREG: %d,%d,\"%[^\"]\",\"%[^\"]\",%d", 
               &n, &stat, lac, ci, &act) >= 2) {
        info->reg_state = (RegistrationState)stat;
        info->act = (AccessTechnology)act;
        info->lac = (int)strtol(lac, NULL, 16);
        info->cell_id = (int)strtol(ci, NULL, 16);
    }
    
    // Get operator name
    if (cellular_send_at_command(modem, "AT+COPS?\r", response, 
                                 sizeof(response), 2000) > 0) {
        char mode[4], format[4], oper[32];
        // Parse: +COPS: <mode>,<format>,"<oper>"[,<AcT>]
        if (sscanf(response, "+COPS: %[^,],%[^,],\"%[^\"]\"", 
                   mode, format, oper) >= 3) {
            strncpy(info->operator_name, oper, sizeof(info->operator_name) - 1);
        }
    }
    
    modem->network_info = *info;
    return 0;
}

// Set network selection mode
int cellular_set_network_mode(CellularModem *modem, const char *mode) {
    char command[64];
    char response[256];
    
    // mode can be: "AUTO", "LTE_ONLY", "GSM_ONLY", etc.
    // Vendor-specific, example for Qualcomm-based modems:
    if (strcmp(mode, "AUTO") == 0) {
        snprintf(command, sizeof(command), "AT+QCFG=\"nwscanmode\",0\r");
    } else if (strcmp(mode, "LTE_ONLY") == 0) {
        snprintf(command, sizeof(command), "AT+QCFG=\"nwscanmode\",3\r");
    } else if (strcmp(mode, "5G_ONLY") == 0) {
        snprintf(command, sizeof(command), "AT+QCFG=\"nwscanmode\",11\r");
    } else {
        return -1;
    }
    
    return cellular_send_at_command(modem, command, response, 
                                   sizeof(response), 5000);
}

// Scan for available networks
int cellular_scan_networks(CellularModem *modem) {
    char response[2048];
    
    printf("Scanning for available networks (this may take 60+ seconds)...\n");
    
    // AT+COPS=? triggers network scan
    if (cellular_send_at_command(modem, "AT+COPS=?\r", response, 
                                 sizeof(response), 120000) < 0) {
        return -1;
    }
    
    printf("Available networks:\n%s\n", response);
    return 0;
}

// Connect to cellular network with APN
int cellular_connect_network(CellularModem *modem, const char *apn,
                             const char *username, const char *password) {
    char command[256];
    char response[512];
    
    printf("Connecting to network with APN: %s\n", apn);
    
    // Define PDP context (example for context ID 1)
    snprintf(command, sizeof(command), 
             "AT+CGDCONT=1,\"IP\",\"%s\"\r", apn);
    if (cellular_send_at_command(modem, command, response, 
                                 sizeof(response), 2000) < 0) {
        return -1;
    }
    
    // Set authentication (if needed)
    if (username && password && strlen(username) > 0) {
        snprintf(command, sizeof(command),
                 "AT+CGAUTH=1,1,\"%s\",\"%s\"\r", username, password);
        cellular_send_at_command(modem, command, response, 
                                sizeof(response), 2000);
    }
    
    // Activate PDP context
    if (cellular_send_at_command(modem, "AT+CGACT=1,1\r", response,
                                 sizeof(response), 30000) < 0) {
        return -1;
    }
    
    // Check if connection established
    if (strstr(response, "OK")) {
        modem->is_connected = true;
        printf("Network connection established\n");
        return 0;
    }
    
    return -1;
}

// Check if registered to network
bool cellular_is_registered(CellularModem *modem) {
    NetworkInfo info;
    cellular_get_network_info(modem, &info);
    return (info.reg_state == REG_HOME_NETWORK || 
            info.reg_state == REG_ROAMING);
}

// Close modem connection
int cellular_modem_close(CellularModem *modem) {
    if (modem->fd >= 0) {
        if (modem->is_connected) {
            cellular_disconnect_network(modem);
        }
        close(modem->fd);
        modem->fd = -1;
    }
    return 0;
}

// Disconnect from network
int cellular_disconnect_network(CellularModem *modem) {
    char response[256];
    
    cellular_send_at_command(modem, "AT+CGACT=0,1\r", response,
                            sizeof(response), 5000);
    modem->is_connected = false;
    printf("Network disconnected\n");
    return 0;
}
```

```c
// main.c - Example usage
#include "cellular_modem.h"

int main() {
    CellularModem modem;
    SignalQuality quality;
    NetworkInfo info;
    
    // Initialize modem (adjust device path for your system)
    // Common paths: /dev/ttyUSB2, /dev/ttyACM0, /dev/modem
    if (cellular_modem_init(&modem, "/dev/ttyUSB2") < 0) {
        fprintf(stderr, "Failed to initialize modem\n");
        return 1;
    }
    
    // Wait for network registration
    printf("Waiting for network registration...\n");
    int retries = 30;
    while (retries-- > 0) {
        if (cellular_is_registered(&modem)) {
            printf("Registered to network!\n");
            break;
        }
        sleep(2);
    }
    
    if (!cellular_is_registered(&modem)) {
        fprintf(stderr, "Failed to register to network\n");
        cellular_modem_close(&modem);
        return 1;
    }
    
    // Get network information
    if (cellular_get_network_info(&modem, &info) == 0) {
        printf("\n=== Network Information ===\n");
        printf("Operator: %s\n", info.operator_name);
        printf("Registration: %d\n", info.reg_state);
        printf("Technology: %d\n", info.act);
        printf("Cell ID: 0x%X\n", info.cell_id);
        printf("LAC: 0x%X\n", info.lac);
    }
    
    // Get signal quality
    if (cellular_get_signal_quality(&modem, &quality) == 0) {
        printf("\n=== Signal Quality ===\n");
        printf("RSSI: %d dBm\n", quality.rssi);
        if (quality.rsrp != 0) {
            printf("RSRP: %d dBm (LTE/5G)\n", quality.rsrp);
            printf("RSRQ: %.1f dB (LTE/5G)\n", quality.rsrq);
        }
    }
    
    // Connect to network (replace with your APN)
    const char *apn = "internet";  // Example APN
    if (cellular_connect_network(&modem, apn, NULL, NULL) == 0) {
        printf("\nData connection established!\n");
        
        // Now you can use standard socket programming for data transfer
        // The modem typically creates a network interface (e.g., wwan0, ppp0)
        
        sleep(5);
        cellular_disconnect_network(&modem);
    }
    
    // Cleanup
    cellular_modem_close(&modem);
    return 0;
}
```

### Rust Implementation

```rust
// Cargo.toml
/*
[package]
name = "cellular_network"
version = "0.1.0"
edition = "2021"

[dependencies]
serialport = "4.2"
regex = "1.10"
thiserror = "1.0"
tokio = { version = "1.35", features = ["full"] }
*/

// src/lib.rs
use serialport::{SerialPort, SerialPortType};
use std::io::{Read, Write};
use std::time::Duration;
use thiserror::Error;

#[derive(Error, Debug)]
pub enum CellularError {
    #[error("Serial port error: {0}")]
    SerialPort(#[from] serialport::Error),
    
    #[error("IO error: {0}")]
    Io(#[from] std::io::Error),
    
    #[error("AT command timeout")]
    Timeout,
    
    #[error("AT command failed: {0}")]
    CommandFailed(String),
    
    #[error("Parse error: {0}")]
    ParseError(String),
    
    #[error("Not registered to network")]
    NotRegistered,
}

pub type Result<T> = std::result::Result<T, CellularError>;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum RegistrationState {
    NotRegistered = 0,
    HomeNetwork = 1,
    Searching = 2,
    Denied = 3,
    Unknown = 4,
    Roaming = 5,
}

impl From<u8> for RegistrationState {
    fn from(value: u8) -> Self {
        match value {
            0 => RegistrationState::NotRegistered,
            1 => RegistrationState::HomeNetwork,
            2 => RegistrationState::Searching,
            3 => RegistrationState::Denied,
            4 => RegistrationState::Unknown,
            5 => RegistrationState::Roaming,
            _ => RegistrationState::Unknown,
        }
    }
}

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum AccessTechnology {
    GSM = 0,
    UTRAN = 2,          // 3G
    EUTRAN = 7,         // LTE/4G
    NR = 10,            // 5G
    Unknown = 255,
}

impl From<u8> for AccessTechnology {
    fn from(value: u8) -> Self {
        match value {
            0 => AccessTechnology::GSM,
            2 => AccessTechnology::UTRAN,
            7 => AccessTechnology::EUTRAN,
            10 => AccessTechnology::NR,
            _ => AccessTechnology::Unknown,
        }
    }
}

#[derive(Debug, Clone)]
pub struct SignalQuality {
    pub rssi: i32,      // dBm
    pub rsrp: i32,      // dBm (LTE/5G)
    pub rsrq: f32,      // dB (LTE/5G)
    pub sinr: i32,      // dB (LTE/5G)
    pub ber: u8,        // Bit Error Rate
}

impl Default for SignalQuality {
    fn default() -> Self {
        Self {
            rssi: -999,
            rsrp: -999,
            rsrq: -999.0,
            sinr: -999,
            ber: 99,
        }
    }
}

#[derive(Debug, Clone)]
pub struct NetworkInfo {
    pub plmn: String,
    pub operator_name: String,
    pub access_technology: AccessTechnology,
    pub registration_state: RegistrationState,
    pub cell_id: u32,
    pub lac: u32,
}

impl Default for NetworkInfo {
    fn default() -> Self {
        Self {
            plmn: String::new(),
            operator_name: String::new(),
            access_technology: AccessTechnology::Unknown,
            registration_state: RegistrationState::NotRegistered,
            cell_id: 0,
            lac: 0,
        }
    }
}

pub struct CellularModem {
    port: Box<dyn SerialPort>,
    device_path: String,
    is_connected: bool,
    network_info: NetworkInfo,
    signal_quality: SignalQuality,
}

impl CellularModem {
    pub fn new(device_path: &str, baud_rate: u32) -> Result<Self> {
        let port = serialport::new(device_path, baud_rate)
            .timeout(Duration::from_millis(1000))
            .open()?;
        
        let mut modem = CellularModem {
            port,
            device_path: device_path.to_string(),
            is_connected: false,
            network_info: NetworkInfo::default(),
            signal_quality: SignalQuality::default(),
        };
        
        // Initialize modem
        std::thread::sleep(Duration::from_millis(500));
        modem.send_at_command("ATZ", Duration::from_secs(2))?;      // Reset
        std::thread::sleep(Duration::from_millis(500));
        modem.send_at_command("ATE0", Duration::from_secs(2))?;     // Echo off
        modem.send_at_command("AT+CMEE=2", Duration::from_secs(2))?; // Verbose errors
        
        println!("Cellular modem initialized on {}", device_path);
        Ok(modem)
    }
    
    pub fn send_at_command(&mut self, command: &str, timeout: Duration) -> Result<String> {
        // Clear buffers
        let _ = self.port.clear(serialport::ClearBuffer::All);
        
        // Send command
        let cmd = format!("{}\r", command);
        self.port.write_all(cmd.as_bytes())?;
        self.port.flush()?;
        
        // Read response
        let mut response = String::new();
        let mut buffer = [0u8; 256];
        let start = std::time::Instant::now();
        
        loop {
            if start.elapsed() > timeout {
                return Err(CellularError::Timeout);
            }
            
            match self.port.read(&mut buffer) {
                Ok(n) if n > 0 => {
                    let chunk = String::from_utf8_lossy(&buffer[..n]);
                    response.push_str(&chunk);
                    
                    // Check for completion
                    if response.contains("OK\r\n") {
                        return Ok(response);
                    } else if response.contains("ERROR") {
                        return Err(CellularError::CommandFailed(response));
                    }
                }
                Ok(_) => std::thread::sleep(Duration::from_millis(10)),
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => {
                    std::thread::sleep(Duration::from_millis(10));
                }
                Err(e) => return Err(CellularError::Io(e)),
            }
        }
    }
    
    pub fn get_signal_quality(&mut self) -> Result<SignalQuality> {
        let mut quality = SignalQuality::default();
        
        // Get basic signal quality
        let response = self.send_at_command("AT+CSQ", Duration::from_secs(2))?;
        
        // Parse: +CSQ: <rssi>,<ber>
        let re = regex::Regex::new(r"\+CSQ: (\d+),(\d+)").unwrap();
        if let Some(caps) = re.captures(&response) {
            let rssi_raw: i32 = caps[1].parse().unwrap_or(99);
            quality.ber = caps[2].parse().unwrap_or(99);
            
            quality.rssi = if rssi_raw == 99 {
                -999
            } else {
                -113 + (rssi_raw * 2)
            };
        }
        
        // Try to get LTE signal quality
        if let Ok(response) = self.send_at_command("AT+CESQ", Duration::from_secs(2)) {
            let re = regex::Regex::new(r"\+CESQ: (\d+),(\d+),(\d+),(\d+),(\d+),(\d+)").unwrap();
            if let Some(caps) = re.captures(&response) {
                let rsrq: u8 = caps[5].parse().unwrap_or(255);
                let rsrp: u8 = caps[6].parse().unwrap_or(255);
                
                if rsrq != 255 {
                    quality.rsrq = -20.0 + (rsrq as f32 * 0.5);
                }
                if rsrp != 255 {
                    quality.rsrp = -140 + rsrp as i32;
                }
            }
        }
        
        self.signal_quality = quality.clone();
        Ok(quality)
    }
    
    pub fn get_network_info(&mut self) -> Result<NetworkInfo> {
        let mut info = NetworkInfo::default();
        
        // Get registration status (try LTE first, then fallback to 2G/3G)
        let response = self.send_at_command("AT+CEREG?", Duration::from_secs(2))
            .or_else(|_| self.send_at_command("AT+CREG?", Duration::from_secs(2)))?;
        
        // Parse: +CEREG: <n>,<stat>[,"<lac>","<ci>"[,<AcT>]]
        let re = regex::Regex::new(
            r#"\+C[E]?REG: \d+,(\d+)(?:,"([0-9A-F]+)","([0-9A-F]+)",(\d+))?"#
        ).unwrap();
        
        if let Some(caps) = re.captures(&response) {
            info.registration_state = caps[1].parse::<u8>().unwrap_or(0).into();
            
            if caps.get(2).is_some() {
                info.lac = u32::from_str_radix(&caps[2], 16).unwrap_or(0);
                info.cell_id = u32::from_str_radix(&caps[3], 16).unwrap_or(0);
                info.access_technology = caps[4].parse::<u8>().unwrap_or(255).into();
            }
        }
        
        // Get operator name
        if let Ok(response) = self.send_at_command("AT+COPS?", Duration::from_secs(2)) {
            let re = regex::Regex::new(r#"\+COPS: \d+,\d+,"([^"]+)"#).unwrap();
            if let Some(caps) = re.captures(&response) {
                info.operator_name = caps[1].to_string();
            }
        }
        
        self.network_info = info.clone();
        Ok(info)
    }
    
    pub fn set_network_mode(&mut self, mode: &str) -> Result<()> {
        let command = match mode {
            "AUTO" => "AT+QCFG=\"nwscanmode\",0",
            "LTE_ONLY" => "AT+QCFG=\"nwscanmode\",3",
            "5G_ONLY" => "AT+QCFG=\"nwscanmode\",11",
            _ => return Err(CellularError::ParseError("Invalid mode".to_string())),
        };
        
        self.send_at_command(command, Duration::from_secs(5))?;
        Ok(())
    }
    
    pub fn scan_networks(&mut self) -> Result<String> {
        println!("Scanning for networks (may take 60+ seconds)...");
        self.send_at_command("AT+COPS=?", Duration::from_secs(120))
    }
    
    pub fn connect_network(&mut self, apn: &str, username: Option<&str>, 
                          password: Option<&str>) -> Result<()> {
        println!("Connecting to network with APN: {}", apn);
        
        // Define PDP context
        let command = format!("AT+CGDCONT=1,\"IP\",\"{}\"", apn);
        self.send_at_command(&command, Duration::from_secs(2))?;
        
        // Set authentication if provided
        if let (Some(user), Some(pass)) = (username, password) {
            let auth_cmd = format!("AT+CGAUTH=1,1,\"{}\",\"{}\"", user, pass);
            let _ = self.send_at_command(&auth_cmd, Duration::from_secs(2));
        }
        
        // Activate PDP context
        self.send_at_command("AT+CGACT=1,1", Duration::from_secs(30))?;
        
        self.is_connected = true;
        println!("Network connection established");
        Ok(())
    }
    
    pub fn disconnect_network(&mut self) -> Result<()> {
        self.send_at_command("AT+CGACT=0,1", Duration::from_secs(5))?;
        self.is_connected = false;
        println!("Network disconnected");
        Ok(())
    }
    
    pub fn is_registered(&mut self) -> bool {
        if let Ok(info) = self.get_network_info() {
            matches!(info.registration_state, 
                    RegistrationState::HomeNetwork | RegistrationState::Roaming)
        } else {
            false
        }
    }
    
    pub fn list_serial_ports() -> Vec<String> {
        serialport::available_ports()
            .unwrap_or_default()
            .into_iter()
            .filter(|p| {
                matches!(p.port_type, SerialPortType::UsbPort(_))
            })
            .map(|p| p.port_name)
            .collect()
    }
}

impl Drop for CellularModem {
    fn drop(&mut self) {
        if self.is_connected {
            let _ = self.disconnect_network();
        }
    }
}
```

```rust
// src/main.rs
use cellular_network::{CellularModem, Result};
use std::thread;
use std::time::Duration;

fn main() -> Result<()> {
    println!("=== Cellular Network Programming Demo ===\n");
    
    // List available serial ports
    println!("Available serial ports:");
    for port in CellularModem::list_serial_ports() {
        println!("  - {}", port);
    }
    println!();
    
    // Initialize modem (adjust device path for your system)
    let device_path = "/dev/ttyUSB2"; // Common: /dev/ttyUSB2, /dev/ttyACM0
    let mut modem = CellularModem::new(device_path, 115200)?;
    
    // Wait for network registration
    println!("Waiting for network registration...");
    let mut retries = 30;
    while retries > 0 {
        if modem.is_registered() {
            println!("✓ Registered to network!\n");
            break;
        }
        thread::sleep(Duration::from_secs(2));
        retries -= 1;
    }
    
    if !modem.is_registered() {
        eprintln!("✗ Failed to register to network");
        return Ok(());
    }
    
    // Get network information
    match modem.get_network_info() {
        Ok(info) => {
            println!("=== Network Information ===");
            println!("Operator: {}", info.operator_name);
            println!("Registration: {:?}", info.registration_state);
            println!("Technology: {:?}", info.access_technology);
            println!("Cell ID: 0x{:X}", info.cell_id);
            println!("LAC: 0x{:X}", info.lac);
            println!();
        }
        Err(e) => eprintln!("Error getting network info: {}", e),
    }
    
    // Get signal quality
    match modem.get_signal_quality() {
        Ok(quality) => {
            println!("=== Signal Quality ===");
            println!("RSSI: {} dBm", quality.rssi);
            if quality.rsrp != -999 {
                println!("RSRP: {} dBm (LTE/5G)", quality.rsrp);
                println!("RSRQ: {:.1} dB (LTE/5G)", quality.rsrq);
            }
            println!();
        }
        Err(e) => eprintln!("Error getting signal quality: {}", e),
    }
    
    // Connect to network (replace with your carrier's APN)
    let apn = "internet"; // Example APN
    match modem.connect_network(apn, None, None) {
        Ok(_) => {
            println!("✓ Data connection established!\n");
            println!("Network interface created (e.g., wwan0, ppp0)");
            println!("You can now use standard socket programming\n");
            
            // Keep connection active for a few seconds
            thread::sleep(Duration::from_secs(5));
            
            modem.disconnect_network()?;
        }
        Err(e) => eprintln!("✗ Failed to connect: {}", e),
    }
    
    Ok(())
}
```

## Summary

**Cellular Network Programming** enables applications to interact with LTE/5G networks for mobile data connectivity and network management. Key aspects include:

**Core Capabilities:**
- Network registration and PLMN selection
- Signal quality monitoring (RSSI, RSRP, RSRQ, SINR)
- APN configuration and data bearer management
- Network technology selection (2G/3G/4G/5G)
- AT command interface for modem control

**Technical Highlights:**
- Serial port communication for modem control
- Parsing AT command responses for network status
- Managing network attachment and PDP contexts
- Signal strength monitoring for connection quality
- Automatic and manual network selection strategies

**Common Applications:**
- IoT devices with cellular connectivity
- Mobile routers and failover systems
- Vehicle telematics and fleet management
- Remote industrial monitoring
- Emergency backup connectivity

**Best Practices:**
- Handle network registration timeouts gracefully
- Monitor signal quality to optimize data transfer
- Configure appropriate APNs for carrier networks
- Implement retry logic for transient failures
- Use vendor-specific AT commands when needed
- Respect power management for battery-operated devices

The implementations demonstrate low-level modem control through AT commands, network status monitoring, and data connection establishment - essential skills for embedded systems, IoT devices, and applications requiring reliable cellular connectivity independent of operating system network management.