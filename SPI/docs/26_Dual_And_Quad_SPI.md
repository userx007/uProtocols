# Dual and Quad SPI: Enhanced Throughput Modes

## Overview

Standard SPI (Serial Peripheral Interface) uses a single data line for transmission (MOSI) and a single line for reception (MISO), achieving half-duplex or full-duplex communication. **Dual SPI** and **Quad SPI** are enhanced modes that utilize multiple data lines simultaneously to increase throughput, commonly used with flash memory devices, displays, and other high-speed peripherals.

## Key Concepts

### Standard SPI vs. Dual/Quad SPI

**Standard SPI (Single SPI):**
- MOSI: Master Out Slave In (1 data line)
- MISO: Master In Slave Out (1 data line)
- Maximum throughput: 1 bit per clock cycle

**Dual SPI:**
- Uses 2 data lines (IO0, IO1) bidirectionally
- Throughput: 2 bits per clock cycle
- 2x faster than standard SPI

**Quad SPI (QSPI):**
- Uses 4 data lines (IO0, IO1, IO2, IO3) bidirectionally
- Throughput: 4 bits per clock cycle
- 4x faster than standard SPI

### Operating Modes

1. **Extended SPI Mode**: Command and address phases use single line, data phase uses dual/quad
2. **Dual/Quad I/O Mode**: Address and data phases use multiple lines
3. **Dual/Quad Output Mode**: Only data output uses multiple lines
4. **QPI Mode**: All phases (command, address, data) use quad lines

## Pin Configuration

### Dual SPI Pins
- **CLK**: Serial clock
- **CS#**: Chip select (active low)
- **IO0**: Bidirectional data line 0 (was MOSI)
- **IO1**: Bidirectional data line 1 (was MISO)

### Quad SPI Pins
- **CLK**: Serial clock
- **CS#**: Chip select (active low)
- **IO0-IO3**: Four bidirectional data lines
- Sometimes includes **HOLD#** and **WP#** (Write Protect) pins

## C/C++ Implementation Examples

### Example 1: Basic Quad SPI Flash Interface (Embedded C)

```c
#include <stdint.h>
#include <stdbool.h>

// QSPI register definitions (example for STM32)
#define QSPI_BASE           0x40000000
#define QSPI_CR             (*(volatile uint32_t*)(QSPI_BASE + 0x00))
#define QSPI_DCR            (*(volatile uint32_t*)(QSPI_BASE + 0x04))
#define QSPI_SR             (*(volatile uint32_t*)(QSPI_BASE + 0x08))
#define QSPI_CCR            (*(volatile uint32_t*)(QSPI_BASE + 0x14))
#define QSPI_AR             (*(volatile uint32_t*)(QSPI_BASE + 0x18))
#define QSPI_DR             (*(volatile uint32_t*)(QSPI_BASE + 0x20))

// Flash commands
#define QUAD_READ_CMD       0x6B
#define QUAD_WRITE_CMD      0x32
#define READ_STATUS_CMD     0x05
#define WRITE_ENABLE_CMD    0x06

// QSPI modes
typedef enum {
    QSPI_MODE_SINGLE = 0,
    QSPI_MODE_DUAL   = 1,
    QSPI_MODE_QUAD   = 3
} qspi_mode_t;

// QSPI configuration structure
typedef struct {
    uint8_t  cmd;
    uint32_t address;
    uint8_t  addr_size;
    uint8_t  dummy_cycles;
    qspi_mode_t cmd_mode;
    qspi_mode_t addr_mode;
    qspi_mode_t data_mode;
    bool     use_address;
    bool     use_data;
} qspi_command_t;

// Initialize QSPI peripheral
void qspi_init(void) {
    // Enable QSPI clock
    // Configure GPIO pins for QSPI function
    
    // Configure QSPI: 
    // - Flash size (e.g., 16MB = 24 bits address)
    // - Clock prescaler
    QSPI_DCR = (23 << 16) | (1 << 8);  // 16MB flash, prescaler = 2
    
    // Enable QSPI
    QSPI_CR = (1 << 0);
}

// Send command to QSPI flash
bool qspi_send_command(const qspi_command_t* cmd) {
    uint32_t ccr = 0;
    
    // Configure instruction mode and instruction
    ccr |= (cmd->cmd_mode << 8) | cmd->cmd;
    
    // Configure address mode and size
    if (cmd->use_address) {
        ccr |= (cmd->addr_mode << 10);
        ccr |= ((cmd->addr_size - 1) << 12);
    }
    
    // Configure data mode
    if (cmd->use_data) {
        ccr |= (cmd->data_mode << 24);
    }
    
    // Set dummy cycles
    ccr |= (cmd->dummy_cycles << 18);
    
    // Write to CCR register
    QSPI_CCR = ccr;
    
    // Write address if needed
    if (cmd->use_address) {
        QSPI_AR = cmd->address;
    }
    
    return true;
}

// Read data from QSPI flash using Quad mode
bool qspi_read_quad(uint32_t address, uint8_t* buffer, uint32_t size) {
    qspi_command_t cmd = {
        .cmd = QUAD_READ_CMD,
        .address = address,
        .addr_size = 3,          // 24-bit address
        .dummy_cycles = 8,       // Device-specific
        .cmd_mode = QSPI_MODE_SINGLE,
        .addr_mode = QSPI_MODE_SINGLE,
        .data_mode = QSPI_MODE_QUAD,
        .use_address = true,
        .use_data = true
    };
    
    // Configure for read operation
    QSPI_CR &= ~(1 << 28);  // Clear DMA mode
    
    if (!qspi_send_command(&cmd)) {
        return false;
    }
    
    // Read data
    for (uint32_t i = 0; i < size; i++) {
        // Wait for data available
        while (!(QSPI_SR & (1 << 2)));
        buffer[i] = QSPI_DR;
    }
    
    return true;
}

// Write data to QSPI flash using Quad mode
bool qspi_write_quad(uint32_t address, const uint8_t* data, uint32_t size) {
    // Enable write
    qspi_command_t we_cmd = {
        .cmd = WRITE_ENABLE_CMD,
        .cmd_mode = QSPI_MODE_SINGLE,
        .use_address = false,
        .use_data = false
    };
    qspi_send_command(&we_cmd);
    
    // Quad page program
    qspi_command_t cmd = {
        .cmd = QUAD_WRITE_CMD,
        .address = address,
        .addr_size = 3,
        .dummy_cycles = 0,
        .cmd_mode = QSPI_MODE_SINGLE,
        .addr_mode = QSPI_MODE_SINGLE,
        .data_mode = QSPI_MODE_QUAD,
        .use_address = true,
        .use_data = true
    };
    
    if (!qspi_send_command(&cmd)) {
        return false;
    }
    
    // Write data
    for (uint32_t i = 0; i < size; i++) {
        // Wait for FIFO ready
        while (!(QSPI_SR & (1 << 3)));
        QSPI_DR = data[i];
    }
    
    // Wait for completion
    while (QSPI_SR & (1 << 5));
    
    return true;
}

// Example usage
void example_qspi_usage(void) {
    uint8_t write_buffer[256];
    uint8_t read_buffer[256];
    
    // Initialize QSPI
    qspi_init();
    
    // Prepare test data
    for (int i = 0; i < 256; i++) {
        write_buffer[i] = i;
    }
    
    // Write data to flash
    qspi_write_quad(0x1000, write_buffer, 256);
    
    // Read back data
    qspi_read_quad(0x1000, read_buffer, 256);
    
    // Verify data
    bool success = true;
    for (int i = 0; i < 256; i++) {
        if (read_buffer[i] != write_buffer[i]) {
            success = false;
            break;
        }
    }
}
```

### Example 2: Dual SPI with DMA (C++)

```cpp
#include <cstdint>
#include <array>
#include <functional>

class DualSPI {
private:
    struct Registers {
        volatile uint32_t CR;     // Control register
        volatile uint32_t SR;     // Status register
        volatile uint32_t DR;     // Data register
        volatile uint32_t CCR;    // Command/Configuration register
        volatile uint32_t AR;     // Address register
    };
    
    Registers* regs;
    bool dma_enabled;
    std::function<void()> transfer_complete_callback;
    
public:
    enum class DataMode {
        Single = 0,
        Dual = 1,
        Quad = 3
    };
    
    enum class TransferType {
        Read,
        Write
    };
    
    DualSPI(uintptr_t base_address) 
        : regs(reinterpret_cast<Registers*>(base_address)),
          dma_enabled(false) {
    }
    
    void init(uint32_t clock_prescaler) {
        // Configure clock and enable peripheral
        regs->CR = (1 << 0) | (clock_prescaler << 24);
    }
    
    void enable_dma(bool enable) {
        dma_enabled = enable;
        if (enable) {
            regs->CR |= (1 << 2);  // DMA enable bit
        } else {
            regs->CR &= ~(1 << 2);
        }
    }
    
    template<size_t N>
    bool dual_transfer(uint8_t command, 
                       uint32_t address,
                       std::array<uint8_t, N>& buffer,
                       TransferType type) {
        // Configure command
        uint32_t ccr = command;
        ccr |= (static_cast<uint32_t>(DataMode::Single) << 8);  // Command in single mode
        ccr |= (static_cast<uint32_t>(DataMode::Dual) << 10);   // Address in dual mode
        ccr |= (static_cast<uint32_t>(DataMode::Dual) << 24);   // Data in dual mode
        ccr |= (2 << 12);  // 3-byte address
        
        if (type == TransferType::Read) {
            ccr |= (1 << 26);  // Read mode
        }
        
        regs->CCR = ccr;
        regs->AR = address;
        
        if (dma_enabled) {
            return dma_transfer(buffer.data(), N, type);
        } else {
            return polling_transfer(buffer.data(), N, type);
        }
    }
    
    void set_transfer_complete_callback(std::function<void()> callback) {
        transfer_complete_callback = callback;
    }
    
private:
    bool polling_transfer(uint8_t* buffer, size_t size, TransferType type) {
        if (type == TransferType::Read) {
            for (size_t i = 0; i < size; i++) {
                while (!(regs->SR & (1 << 2)));  // Wait for data
                buffer[i] = regs->DR;
            }
        } else {
            for (size_t i = 0; i < size; i++) {
                while (!(regs->SR & (1 << 3)));  // Wait for FIFO
                regs->DR = buffer[i];
            }
            while (regs->SR & (1 << 5));  // Wait for busy
        }
        return true;
    }
    
    bool dma_transfer(uint8_t* buffer, size_t size, TransferType type) {
        // Configure DMA for transfer
        // This is hardware-specific
        // Typically involves setting memory address, size, and direction
        
        // Start DMA transfer
        // DMA controller will handle the data transfer
        
        return true;
    }
    
public:
    // Interrupt handler
    void handle_interrupt() {
        if (regs->SR & (1 << 1)) {  // Transfer complete
            if (transfer_complete_callback) {
                transfer_complete_callback();
            }
        }
    }
};

// Usage example
void dual_spi_example() {
    DualSPI spi(0x40000000);
    spi.init(2);  // Prescaler = 2
    
    std::array<uint8_t, 512> data;
    
    // Read 512 bytes using dual mode
    spi.dual_transfer(0x3B,  // Dual read command
                      0x1000, // Address
                      data,
                      DualSPI::TransferType::Read);
    
    // Process data...
}
```

## Rust Implementation Examples

### Example 1: Safe Quad SPI Abstraction

```rust
use core::ptr::{read_volatile, write_volatile};

// QSPI register structure
#[repr(C)]
struct QspiRegisters {
    cr: u32,      // Control register
    dcr: u32,     // Device configuration
    sr: u32,      // Status register
    fcr: u32,     // Flag clear register
    dlr: u32,     // Data length register
    ccr: u32,     // Communication configuration
    ar: u32,      // Address register
    abr: u32,     // Alternate bytes register
    dr: u32,      // Data register
    psmkr: u32,   // Polling status mask
    psmar: u32,   // Polling status match
    pir: u32,     // Polling interval
}

#[derive(Clone, Copy)]
pub enum SpiMode {
    Single = 0b00,
    Dual = 0b01,
    Quad = 0b11,
}

#[derive(Clone, Copy)]
pub enum AddressSize {
    Bits8 = 0,
    Bits16 = 1,
    Bits24 = 2,
    Bits32 = 3,
}

pub struct QspiCommand {
    instruction: u8,
    address: Option<u32>,
    address_size: AddressSize,
    alternate_bytes: Option<u32>,
    dummy_cycles: u8,
    instruction_mode: SpiMode,
    address_mode: SpiMode,
    data_mode: SpiMode,
}

impl QspiCommand {
    pub fn new(instruction: u8) -> Self {
        Self {
            instruction,
            address: None,
            address_size: AddressSize::Bits24,
            alternate_bytes: None,
            dummy_cycles: 0,
            instruction_mode: SpiMode::Single,
            address_mode: SpiMode::Single,
            data_mode: SpiMode::Single,
        }
    }
    
    pub fn with_address(mut self, addr: u32, size: AddressSize) -> Self {
        self.address = Some(addr);
        self.address_size = size;
        self
    }
    
    pub fn with_dummy_cycles(mut self, cycles: u8) -> Self {
        self.dummy_cycles = cycles;
        self
    }
    
    pub fn with_modes(mut self, inst: SpiMode, addr: SpiMode, data: SpiMode) -> Self {
        self.instruction_mode = inst;
        self.address_mode = addr;
        self.data_mode = data;
        self
    }
}

pub struct QuadSpi {
    regs: *mut QspiRegisters,
}

impl QuadSpi {
    pub fn new(base_address: usize) -> Self {
        Self {
            regs: base_address as *mut QspiRegisters,
        }
    }
    
    pub fn init(&mut self, flash_size_bytes: u32, prescaler: u8) {
        unsafe {
            // Calculate flash size in powers of 2
            let fsize = (32 - flash_size_bytes.leading_zeros() - 1) as u32;
            
            // Configure device size and clock prescaler
            let dcr = (fsize << 16) | ((prescaler as u32) << 8);
            write_volatile(&mut (*self.regs).dcr, dcr);
            
            // Enable QSPI
            write_volatile(&mut (*self.regs).cr, 1);
        }
    }
    
    fn wait_flag(&self, flag_mask: u32) {
        unsafe {
            while (read_volatile(&(*self.regs).sr) & flag_mask) == 0 {}
        }
    }
    
    fn configure_command(&mut self, cmd: &QspiCommand, data_len: Option<u32>) {
        unsafe {
            let mut ccr = cmd.instruction as u32;
            
            // Instruction mode
            ccr |= (cmd.instruction_mode as u32) << 8;
            
            // Address configuration
            if cmd.address.is_some() {
                ccr |= (cmd.address_mode as u32) << 10;
                ccr |= (cmd.address_size as u32) << 12;
            }
            
            // Data mode
            if data_len.is_some() {
                ccr |= (cmd.data_mode as u32) << 24;
            }
            
            // Dummy cycles
            ccr |= (cmd.dummy_cycles as u32) << 18;
            
            // Set data length if provided
            if let Some(len) = data_len {
                write_volatile(&mut (*self.regs).dlr, len - 1);
            }
            
            // Write CCR (starts operation)
            write_volatile(&mut (*self.regs).ccr, ccr);
            
            // Write address if needed
            if let Some(addr) = cmd.address {
                write_volatile(&mut (*self.regs).ar, addr);
            }
        }
    }
    
    pub fn read(&mut self, cmd: QspiCommand, buffer: &mut [u8]) -> Result<(), &'static str> {
        if buffer.is_empty() {
            return Err("Buffer cannot be empty");
        }
        
        self.configure_command(&cmd, Some(buffer.len() as u32));
        
        unsafe {
            for byte in buffer.iter_mut() {
                self.wait_flag(1 << 2);  // Wait for FTF (FIFO threshold)
                *byte = read_volatile(&(*self.regs).dr) as u8;
            }
            
            // Wait for completion
            self.wait_flag(1 << 1);  // Wait for TCF (transfer complete)
            write_volatile(&mut (*self.regs).fcr, 1 << 1);  // Clear TCF
        }
        
        Ok(())
    }
    
    pub fn write(&mut self, cmd: QspiCommand, data: &[u8]) -> Result<(), &'static str> {
        if data.is_empty() {
            return Err("Data cannot be empty");
        }
        
        self.configure_command(&cmd, Some(data.len() as u32));
        
        unsafe {
            for &byte in data.iter() {
                self.wait_flag(1 << 3);  // Wait for FTF (FIFO threshold)
                write_volatile(&mut (*self.regs).dr, byte as u32);
            }
            
            // Wait for completion
            self.wait_flag(1 << 1);  // Wait for TCF
            write_volatile(&mut (*self.regs).fcr, 1 << 1);  // Clear TCF
        }
        
        Ok(())
    }
    
    pub fn send_command_only(&mut self, cmd: QspiCommand) -> Result<(), &'static str> {
        self.configure_command(&cmd, None);
        
        unsafe {
            self.wait_flag(1 << 1);  // Wait for TCF
            write_volatile(&mut (*self.regs).fcr, 1 << 1);  // Clear TCF
        }
        
        Ok(())
    }
}

// Usage example
pub fn qspi_example() {
    const QSPI_BASE: usize = 0x4000_0000;
    const FLASH_SIZE: u32 = 16 * 1024 * 1024;  // 16 MB
    
    let mut qspi = QuadSpi::new(QSPI_BASE);
    qspi.init(FLASH_SIZE, 1);
    
    // Quad read command
    let read_cmd = QspiCommand::new(0x6B)
        .with_address(0x1000, AddressSize::Bits24)
        .with_dummy_cycles(8)
        .with_modes(SpiMode::Single, SpiMode::Single, SpiMode::Quad);
    
    let mut buffer = [0u8; 256];
    qspi.read(read_cmd, &mut buffer).unwrap();
    
    // Quad write (page program)
    let write_cmd = QspiCommand::new(0x32)
        .with_address(0x2000, AddressSize::Bits24)
        .with_modes(SpiMode::Single, SpiMode::Single, SpiMode::Quad);
    
    let data: [u8; 256] = core::array::from_fn(|i| i as u8);
    qspi.write(write_cmd, &data).unwrap();
}
```

### Example 2: Memory-Mapped Quad SPI

```rust
use core::slice;

pub struct MemoryMappedQspi {
    qspi: QuadSpi,
    mapped_address: usize,
}

impl MemoryMappedQspi {
    pub fn new(qspi_base: usize, mapped_base: usize) -> Self {
        Self {
            qspi: QuadSpi::new(qspi_base),
            mapped_address: mapped_base,
        }
    }
    
    pub fn enable_memory_mapped_mode(&mut self, read_command: u8, dummy_cycles: u8) {
        unsafe {
            // Configure for continuous read in quad mode
            let mut ccr = read_command as u32;
            ccr |= (SpiMode::Single as u32) << 8;   // Instruction mode
            ccr |= (SpiMode::Quad as u32) << 10;    // Address mode
            ccr |= (SpiMode::Quad as u32) << 24;    // Data mode
            ccr |= (AddressSize::Bits24 as u32) << 12;
            ccr |= (dummy_cycles as u32) << 18;
            ccr |= 3 << 26;  // Memory-mapped mode
            
            write_volatile(&mut (*self.qspi.regs).ccr, ccr);
        }
    }
    
    pub fn read_slice(&self, offset: usize, length: usize) -> &[u8] {
        unsafe {
            let ptr = (self.mapped_address + offset) as *const u8;
            slice::from_raw_parts(ptr, length)
        }
    }
    
    pub fn read_u32(&self, offset: usize) -> u32 {
        unsafe {
            let ptr = (self.mapped_address + offset) as *const u32;
            core::ptr::read_volatile(ptr)
        }
    }
}

// Example: Execute code from QSPI flash
pub fn execute_from_qspi() {
    const QSPI_BASE: usize = 0x4000_0000;
    const MAPPED_BASE: usize = 0x9000_0000;
    
    let mut qspi_mem = MemoryMappedQspi::new(QSPI_BASE, MAPPED_BASE);
    qspi_mem.enable_memory_mapped_mode(0xEB, 6);  // Quad I/O fast read
    
    // Now can read from flash as if it were regular memory
    let data = qspi_mem.read_slice(0, 1024);
    
    // Or execute code directly from flash (XIP - Execute In Place)
    type FlashFunction = fn() -> u32;
    let func: FlashFunction = unsafe {
        core::mem::transmute(MAPPED_BASE as *const ())
    };
    
    // Call function stored in flash
    // let result = func();
}
```

## Summary

**Dual and Quad SPI** technologies significantly enhance data throughput by utilizing multiple data lines simultaneously. Dual SPI doubles the data rate using two bidirectional lines, while Quad SPI quadruples it using four lines. These enhanced modes are particularly valuable for:

- **Flash memory interfaces**: Fast boot times and efficient program storage
- **High-resolution displays**: Rapid screen updates
- **Data logging**: Quick storage of sensor data
- **Firmware updates**: Reduced update times

The implementations shown demonstrate both low-level register manipulation and higher-level abstractions. C/C++ examples illustrate direct hardware control with DMA support, while Rust examples emphasize memory safety through type systems and ownership rules. Memory-mapped modes enable execute-in-place (XIP) functionality, allowing processors to run code directly from external flash memory.

Key considerations include proper command sequencing, dummy cycle configuration (device-specific), and understanding the various operating modes. Modern microcontrollers often include dedicated QSPI peripherals with hardware support for these protocols, making high-speed external memory access both practical and efficient.