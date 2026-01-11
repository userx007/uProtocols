/*
 * I2C Logic Analyzer Debugging Examples (Rust)
 * 
 * Demonstrates I2C patterns for logic analyzer debugging with
 * comprehensive logging and analysis capabilities
 */

use std::fs::File;
use std::io::{self, Write};
use std::thread;
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

// External crate dependencies:
// linux-embedded-hal = "0.4"
// embedded-hal = "1.0"

#[cfg(target_os = "linux")]
use linux_embedded_hal::{I2cdev, Delay};
use embedded_hal::i2c::{I2c, Operation};

// =============================================================================
// Transaction Logging Structures
// =============================================================================

#[derive(Debug, Clone)]
struct TransactionLog {
    timestamp_us: u64,
    address: u8,
    register: u8,
    data: Vec<u8>,
    operation: OperationType,
    success: bool,
    error_message: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
enum OperationType {
    Write,
    Read,
    WriteRead,
}

struct I2cDebugger {
    device: I2cdev,
    address: u8,
    transaction_log: Vec<TransactionLog>,
    start_time: Instant,
}

// =============================================================================
// Debug Implementation
// =============================================================================

impl I2cDebugger {
    fn new(bus_path: &str, address: u8) -> Result<Self, Box<dyn std::error::Error>> {
        let device = I2cdev::new(bus_path)?;
        
        Ok(Self {
            device,
            address,
            transaction_log: Vec::new(),
            start_time: Instant::now(),
        })
    }
    
    fn get_timestamp_us(&self) -> u64 {
        self.start_time.elapsed().as_micros() as u64
    }
    
    fn get_system_time_us() -> u64 {
        SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_micros() as u64
    }
    
    // Write to register with debug logging
    fn write_register_debug(
        &mut self,
        register: u8,
        data: &[u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let timestamp = self.get_timestamp_us();
        
        print!("[{} us] WRITE: Addr=0x{:02X} Reg=0x{:02X} Len={} Data=",
               timestamp, self.address, register, data.len());
        for byte in data {
            print!("0x{:02X} ", byte);
        }
        println!();
        
        // Prepare write buffer with register address
        let mut buffer = vec![register];
        buffer.extend_from_slice(data);
        
        let result = self.device.write(self.address, &buffer);
        
        let success = result.is_ok();
        let error_msg = result.err().map(|e| e.to_string());
        
        self.log_transaction(TransactionLog {
            timestamp_us: timestamp,
            address: self.address,
            register,
            data: data.to_vec(),
            operation: OperationType::Write,
            success,
            error_message: error_msg.clone(),
        });
        
        if let Some(err) = error_msg {
            println!("[ERROR] Write failed: {}", err);
            return Err(err.into());
        }
        
        Ok(())
    }
    
    // Read from register with debug logging
    fn read_register_debug(
        &mut self,
        register: u8,
        buffer: &mut [u8],
    ) -> Result<(), Box<dyn std::error::Error>> {
        let timestamp = self.get_timestamp_us();
        
        // Use write_read for combined operation
        let result = self.device.write_read(self.address, &[register], buffer);
        
        print!("[{} us] READ: Addr=0x{:02X} Reg=0x{:02X} Len={} Data=",
               timestamp, self.address, register, buffer.len());
        
        let success = result.is_ok();
        let error_msg = result.err().map(|e| e.to_string());
        
        if success {
            for byte in buffer.iter() {
                print!("0x{:02X} ", byte);
            }
            println!();
        } else {
            println!("[ERROR] Read failed: {}", error_msg.as_ref().unwrap());
        }
        
        self.log_transaction(TransactionLog {
            timestamp_us: timestamp,
            address: self.address,
            register,
            data: buffer.to_vec(),
            operation: OperationType::Read,
            success,
            error_message: error_msg.clone(),
        });
        
        if let Some(err) = error_msg {
            return Err(err.into());
        }
        
        Ok(())
    }
    
    fn log_transaction(&mut self, log: TransactionLog) {
        self.transaction_log.push(log);
    }
    
    // =============================================================================
    // Test Patterns
    // =============================================================================
    
    fn test_basic_transaction(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 1: Basic Transaction ===");
        
        let config_value = 0x60u8;
        self.write_register_debug(0x01, &[config_value])?;
        
        thread::sleep(Duration::from_millis(10));
        
        let mut read_back = [0u8];
        self.read_register_debug(0x01, &mut read_back)?;
        
        if read_back[0] == config_value {
            println!("✓ Read-back verification passed");
        } else {
            println!("✗ Read-back mismatch: wrote 0x{:02X}, read 0x{:02X}",
                     config_value, read_back[0]);
        }
        
        Ok(())
    }
    
    fn test_burst_write(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 2: Burst Write ===");
        
        let burst_data = [0x11, 0x22, 0x33, 0x44, 0x55];
        self.write_register_debug(0x00, &burst_data)?;
        
        Ok(())
    }
    
    fn test_rapid_transactions(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 3: Rapid Transactions ===");
        
        for i in 0..5 {
            let mut data = [0u8];
            match self.read_register_debug(0x00, &mut data) {
                Ok(_) => println!("Transaction {} completed", i),
                Err(e) => println!("Transaction {} failed: {}", i, e),
            }
            thread::sleep(Duration::from_micros(100));
        }
        
        Ok(())
    }
    
    fn test_error_conditions(&mut self) {
        println!("\n=== Test 4: Error Conditions ===");
        
        // Try invalid register
        println!("Attempting to read invalid register 0xFF:");
        let mut data = [0u8];
        let _ = self.read_register_debug(0xFF, &mut data);
        
        // Try wrong address (store original)
        let original_addr = self.address;
        self.address = 0x7F;
        
        println!("\nAttempting to access wrong address 0x7F:");
        let _ = self.read_register_debug(0x00, &mut data);
        
        // Restore original address
        self.address = original_addr;
    }
    
    fn test_clock_stretching(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 5: Clock Stretching Test ===");
        println!("Reading temperature (may cause clock stretching):");
        
        let mut temp_data = [0u8; 2];
        self.read_register_debug(0x00, &mut temp_data)?;
        
        // Convert to temperature (TMP102 format)
        let raw = ((temp_data[0] as i16) << 4) | ((temp_data[1] as i16) >> 4);
        let temperature = (raw as f32) * 0.0625;
        println!("Temperature: {:.2}°C", temperature);
        
        Ok(())
    }
    
    fn test_repeated_start(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        println!("\n=== Test 6: Repeated Start Condition ===");
        
        // This generates: START - ADDR+W - REG - RESTART - ADDR+R - DATA - STOP
        let mut buffer = [0u8; 2];
        self.read_register_debug(0x00, &mut buffer)?;
        
        println!("Repeated start transaction completed");
        Ok(())
    }
    
    // =============================================================================
    // Analysis and Reporting
    // =============================================================================
    
    fn print_transaction_summary(&self) {
        println!("\n=== Transaction Summary ===");
        println!("Total transactions: {}", self.transaction_log.len());
        
        let success_count = self.transaction_log.iter()
            .filter(|log| log.success)
            .count();
        let failure_count = self.transaction_log.len() - success_count;
        
        println!("Success: {}, Failures: {}", success_count, failure_count);
        
        // Print failed transactions
        for (i, log) in self.transaction_log.iter().enumerate() {
            if !log.success {
                println!("Failed transaction {}: Addr=0x{:02X} Reg=0x{:02X} Error={}",
                         i, log.address, log.register,
                         log.error_message.as_deref().unwrap_or("Unknown"));
            }
        }
        
        // Calculate timing statistics
        if self.transaction_log.len() > 1 {
            let mut intervals: Vec<u64> = Vec::new();
            for i in 1..self.transaction_log.len() {
                let interval = self.transaction_log[i].timestamp_us - 
                              self.transaction_log[i-1].timestamp_us;
                intervals.push(interval);
            }
            
            let avg_interval = intervals.iter().sum::<u64>() / intervals.len() as u64;
            let min_interval = intervals.iter().min().unwrap();
            let max_interval = intervals.iter().max().unwrap();
            
            println!("\nTiming Statistics:");
            println!("  Average interval: {} us", avg_interval);
            println!("  Min interval: {} us", min_interval);
            println!("  Max interval: {} us", max_interval);
        }
        
        // Operation type breakdown
        let write_count = self.transaction_log.iter()
            .filter(|log| log.operation == OperationType::Write)
            .count();
        let read_count = self.transaction_log.iter()
            .filter(|log| log.operation == OperationType::Read)
            .count();
        
        println!("\nOperation Breakdown:");
        println!("  Writes: {}", write_count);
        println!("  Reads: {}", read_count);
    }
    
    fn export_csv(&self, filename: &str) -> io::Result<()> {
        let mut file = File::create(filename)?;
        
        writeln!(file, "Timestamp_us,Address,Register,Length,Operation,Success,Data,Error")?;
        
        for log in &self.transaction_log {
            let data_str = log.data.iter()
                .map(|b| format!("{:02X}", b))
                .collect::<Vec<_>>()
                .join(" ");
            
            let op_str = match log.operation {
                OperationType::Write => "WRITE",
                OperationType::Read => "READ",
                OperationType::WriteRead => "WRITE_READ",
            };
            
            writeln!(
                file,
                "{},0x{:02X},0x{:02X},{},{},{},\"{}\",\"{}\"",
                log.timestamp_us,
                log.address,
                log.register,
                log.data.len(),
                op_str,
                log.success,
                data_str,
                log.error_message.as_deref().unwrap_or("")
            )?;
        }
        
        println!("Log exported to {}", filename);
        Ok(())
    }
    
    fn export_vcd(&self, filename: &str) -> io::Result<()> {
        let mut file = File::create(filename)?;
        
        // VCD header
        writeln!(file, "$version Generated by I2C Debugger $end")?;
        writeln!(file, "$timescale 1us $end")?;
        writeln!(file, "$scope module i2c $end")?;
        writeln!(file, "$var wire 1 ! sda $end")?;
        writeln!(file, "$var wire 1 @ scl $end")?;
        writeln!(file, "$upscope $end")?;
        writeln!(file, "$enddefinitions $end")?;
        
        // VCD body (simplified - just marks transaction times)
        for log in &self.transaction_log {
            writeln!(file, "#{}", log.timestamp_us)?;
            writeln!(file, "0!")?; // SDA activity marker
            writeln!(file, "#{}", log.timestamp_us + 1)?;
            writeln!(file, "1!")?;
        }
        
        println!("VCD waveform exported to {}", filename);
        Ok(())
    }
}

// =============================================================================
// Main Test Application
// =============================================================================

fn main() -> Result<(), Box<dyn std::error::Error>> {
    println!("I2C Logic Analyzer Debug Tool (Rust)");
    println!("====================================\n");
    println!("Connect your logic analyzer to:");
    println!("  - SDA (GPIO2 on Raspberry Pi)");
    println!("  - SCL (GPIO3 on Raspberry Pi)");
    println!("  - GND\n");
    println!("Press Enter to start tests...");
    
    let mut input = String::new();
    io::stdin().read_line(&mut input)?;
    
    // Initialize debugger
    let mut debugger = I2cDebugger::new("/dev/i2c-1", 0x48)?;
    
    println!("\n[{} us] Starting test sequence", debugger.get_timestamp_us());
    
    // Run test patterns
    let _ = debugger.test_basic_transaction();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_burst_write();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_rapid_transactions();
    thread::sleep(Duration::from_millis(100));
    
    debugger.test_error_conditions();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_clock_stretching();
    thread::sleep(Duration::from_millis(100));
    
    let _ = debugger.test_repeated_start();
    
    println!("\n[{} us] Test sequence complete", debugger.get_timestamp_us());
    
    // Analysis and export
    debugger.print_transaction_summary();
    debugger.export_csv("i2c_debug_log.csv")?;
    debugger.export_vcd("i2c_waveform.vcd")?;
    
    println!("\n=== Analysis Tips ===");
    println!("1. Import i2c_debug_log.csv into your logic analyzer software");
    println!("2. Use timestamps to correlate software events with captures");
    println!("3. Check i2c_waveform.vcd for timing visualization");
    println!("4. Look for ACK/NACK patterns on failed transactions");
    println!("5. Verify timing meets I2C spec:");
    println!("   - Standard mode: 100 kHz");
    println!("   - Fast mode: 400 kHz");
    println!("   - Fast mode plus: 1 MHz");
    println!("6. Check for clock stretching (SCL held low by slave)");
    println!("7. Verify proper START and STOP conditions");
    
    Ok(())
}

// =============================================================================
// Additional Utility Functions
// =============================================================================

#[allow(dead_code)]
fn analyze_bus_speed(timestamps: &[u64]) -> Option<f64> {
    if timestamps.len() < 2 {
        return None;
    }
    
    let total_time = timestamps.last().unwrap() - timestamps.first().unwrap();
    let num_transactions = timestamps.len() as f64;
    
    // Rough estimate: assume ~9 clocks per byte (8 data + 1 ACK)
    let estimated_clocks = num_transactions * 9.0;
    let clock_period_us = total_time as f64 / estimated_clocks;
    let frequency_khz = 1000.0 / clock_period_us;
    
    Some(frequency_khz)
}

#[allow(dead_code)]
fn detect_clock_stretching(log: &[TransactionLog]) -> Vec<usize> {
    // Detect transactions that took unusually long
    let mut stretching_indices = Vec::new();
    
    if log.len() < 2 {
        return stretching_indices;
    }
    
    let mut intervals: Vec<u64> = Vec::new();
    for i in 1..log.len() {
        intervals.push(log[i].timestamp_us - log[i-1].timestamp_us);
    }
    
    let avg = intervals.iter().sum::<u64>() / intervals.len() as u64;
    let threshold = avg * 3; // Transactions taking 3x average
    
    for (i, &interval) in intervals.iter().enumerate() {
        if interval > threshold {
            stretching_indices.push(i + 1);
        }
    }
    
    stretching_indices
}