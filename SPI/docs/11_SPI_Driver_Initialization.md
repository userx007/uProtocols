# SPI Driver Initialization

## Detailed Description

SPI (Serial Peripheral Interface) driver initialization is a fundamental process in embedded systems programming that establishes communication between a microcontroller and peripheral devices. This initialization involves configuring hardware registers, setting up GPIO pins for SPI functionality, configuring clock sources and dividers, and establishing the communication parameters that define how data will be transferred.

The initialization process typically involves several key steps:

1. **Clock Configuration**: Enabling the peripheral clock for the SPI module and configuring the SPI clock frequency by setting appropriate prescalers and dividers
2. **GPIO Pin Configuration**: Setting up pins for MOSI (Master Out Slave In), MISO (Master In Slave Out), SCK (Serial Clock), and CS/SS (Chip Select/Slave Select) with proper alternate functions
3. **SPI Mode Selection**: Configuring clock polarity (CPOL) and clock phase (CPHA) to match the peripheral device requirements
4. **Data Format**: Setting data frame size (typically 8 or 16 bits), bit order (MSB or LSB first), and frame format
5. **Master/Slave Configuration**: Determining whether the device operates as a master or slave
6. **DMA and Interrupt Setup**: Optionally configuring direct memory access or interrupt-driven communication

## Code Examples

### C/C++ Implementation (STM32-style)

```c
#include <stdint.h>

// SPI register structure (simplified)
typedef struct {
    volatile uint32_t CR1;      // Control Register 1
    volatile uint32_t CR2;      // Control Register 2
    volatile uint32_t SR;       // Status Register
    volatile uint32_t DR;       // Data Register
    volatile uint32_t CRCPR;    // CRC Polynomial Register
    volatile uint32_t RXCRCR;   // RX CRC Register
    volatile uint32_t TXCRCR;   // TX CRC Register
} SPI_TypeDef;

// GPIO register structure (simplified)
typedef struct {
    volatile uint32_t MODER;    // Mode Register
    volatile uint32_t OTYPER;   // Output Type Register
    volatile uint32_t OSPEEDR;  // Output Speed Register
    volatile uint32_t PUPDR;    // Pull-up/Pull-down Register
    volatile uint32_t AFR[2];   // Alternate Function Registers
} GPIO_TypeDef;

// SPI Configuration Structure
typedef struct {
    uint32_t mode;              // Master or Slave
    uint32_t direction;         // Full duplex, half duplex, or simplex
    uint32_t dataSize;          // 8-bit or 16-bit
    uint32_t clockPolarity;     // CPOL: 0 or 1
    uint32_t clockPhase;        // CPHA: 0 or 1
    uint32_t nss;               // NSS management: hardware or software
    uint32_t baudRatePrescaler; // Clock divider
    uint32_t firstBit;          // MSB or LSB first
} SPI_Config;

// SPI Control Register 1 bit definitions
#define SPI_CR1_CPHA        (1 << 0)
#define SPI_CR1_CPOL        (1 << 1)
#define SPI_CR1_MSTR        (1 << 2)
#define SPI_CR1_BR_SHIFT    3
#define SPI_CR1_SPE         (1 << 6)
#define SPI_CR1_LSBFIRST    (1 << 7)
#define SPI_CR1_SSI         (1 << 8)
#define SPI_CR1_SSM         (1 << 9)
#define SPI_CR1_DFF         (1 << 11)
#define SPI_CR1_BIDIMODE    (1 << 15)

// Base addresses (example for STM32)
#define SPI1_BASE           0x40013000
#define GPIOA_BASE          0x40020000
#define RCC_BASE            0x40023800

#define SPI1                ((SPI_TypeDef*)SPI1_BASE)
#define GPIOA               ((GPIO_TypeDef*)GPIOA_BASE)

// RCC register for enabling clocks
#define RCC_APB2ENR         (*(volatile uint32_t*)(RCC_BASE + 0x44))
#define RCC_AHB1ENR         (*(volatile uint32_t*)(RCC_BASE + 0x30))
#define RCC_APB2ENR_SPI1EN  (1 << 12)
#define RCC_AHB1ENR_GPIOAEN (1 << 0)

/**
 * Initialize GPIO pins for SPI1
 * PA5 - SCK, PA6 - MISO, PA7 - MOSI
 */
void SPI_GPIO_Init(void) {
    // Enable GPIOA clock
    RCC_AHB1ENR |= RCC_AHB1ENR_GPIOAEN;
    
    // Configure PA5, PA6, PA7 as alternate function
    GPIOA->MODER &= ~((3 << (5*2)) | (3 << (6*2)) | (3 << (7*2)));
    GPIOA->MODER |= (2 << (5*2)) | (2 << (6*2)) | (2 << (7*2));
    
    // Set alternate function AF5 for SPI1
    GPIOA->AFR[0] &= ~((0xF << (5*4)) | (0xF << (6*4)) | (0xF << (7*4)));
    GPIOA->AFR[0] |= (5 << (5*4)) | (5 << (6*4)) | (5 << (7*4));
    
    // Configure output speed to high
    GPIOA->OSPEEDR |= (3 << (5*2)) | (3 << (6*2)) | (3 << (7*2));
    
    // Configure output type as push-pull (default, bit = 0)
    GPIOA->OTYPER &= ~((1 << 5) | (1 << 6) | (1 << 7));
    
    // No pull-up, no pull-down
    GPIOA->PUPDR &= ~((3 << (5*2)) | (3 << (6*2)) | (3 << (7*2)));
}

/**
 * Initialize SPI1 peripheral
 */
void SPI_Init(SPI_TypeDef* spi, SPI_Config* config) {
    // Enable SPI1 clock
    RCC_APB2ENR |= RCC_APB2ENR_SPI1EN;
    
    // Disable SPI before configuration
    spi->CR1 &= ~SPI_CR1_SPE;
    
    // Configure SPI CR1 register
    uint32_t cr1 = 0;
    
    // Set clock phase and polarity
    if (config->clockPhase) {
        cr1 |= SPI_CR1_CPHA;
    }
    if (config->clockPolarity) {
        cr1 |= SPI_CR1_CPOL;
    }
    
    // Set as master
    if (config->mode == 1) {  // Master mode
        cr1 |= SPI_CR1_MSTR;
    }
    
    // Set baud rate prescaler
    cr1 |= (config->baudRatePrescaler << SPI_CR1_BR_SHIFT);
    
    // Set data frame format (8-bit or 16-bit)
    if (config->dataSize == 16) {
        cr1 |= SPI_CR1_DFF;
    }
    
    // Set bit order
    if (config->firstBit == 1) {  // LSB first
        cr1 |= SPI_CR1_LSBFIRST;
    }
    
    // Software slave management
    if (config->nss == 1) {
        cr1 |= SPI_CR1_SSM | SPI_CR1_SSI;
    }
    
    // Write to CR1
    spi->CR1 = cr1;
    
    // Enable SPI
    spi->CR1 |= SPI_CR1_SPE;
}

/**
 * Example usage
 */
int main(void) {
    SPI_Config spi_config = {
        .mode = 1,                      // Master mode
        .direction = 0,                 // Full duplex
        .dataSize = 8,                  // 8-bit data
        .clockPolarity = 0,             // CPOL = 0
        .clockPhase = 0,                // CPHA = 0
        .nss = 1,                       // Software NSS management
        .baudRatePrescaler = 3,         // fPCLK/16
        .firstBit = 0                   // MSB first
    };
    
    // Initialize GPIO pins
    SPI_GPIO_Init();
    
    // Initialize SPI1
    SPI_Init(SPI1, &spi_config);
    
    // Now ready to transmit/receive data
    while (1) {
        // Application code
    }
    
    return 0;
}
```

### Rust Implementation (embedded-hal approach)

```rust
// Using embedded-hal traits and a hypothetical PAC (Peripheral Access Crate)
use core::ptr;

// Register block structure
#[repr(C)]
pub struct SpiRegisters {
    cr1: u32,      // Control Register 1
    cr2: u32,      // Control Register 2
    sr: u32,       // Status Register
    dr: u32,       // Data Register
    crcpr: u32,    // CRC Polynomial Register
    rxcrcr: u32,   // RX CRC Register
    txcrcr: u32,   // TX CRC Register
}

// SPI Configuration
pub struct SpiConfig {
    pub mode: Mode,
    pub clock_polarity: Polarity,
    pub clock_phase: Phase,
    pub data_size: DataSize,
    pub baud_rate: BaudRate,
    pub bit_order: BitOrder,
}

#[derive(Copy, Clone)]
pub enum Mode {
    Master,
    Slave,
}

#[derive(Copy, Clone)]
pub enum Polarity {
    IdleLow,   // CPOL = 0
    IdleHigh,  // CPOL = 1
}

#[derive(Copy, Clone)]
pub enum Phase {
    CaptureOnFirstTransition,  // CPHA = 0
    CaptureOnSecondTransition, // CPHA = 1
}

#[derive(Copy, Clone)]
pub enum DataSize {
    EightBit,
    SixteenBit,
}

#[derive(Copy, Clone)]
pub enum BitOrder {
    MsbFirst,
    LsbFirst,
}

#[derive(Copy, Clone)]
pub enum BaudRate {
    Div2 = 0,
    Div4 = 1,
    Div8 = 2,
    Div16 = 3,
    Div32 = 4,
    Div64 = 5,
    Div128 = 6,
    Div256 = 7,
}

// Control Register 1 bit definitions
mod cr1 {
    pub const CPHA: u32 = 1 << 0;
    pub const CPOL: u32 = 1 << 1;
    pub const MSTR: u32 = 1 << 2;
    pub const BR_SHIFT: u32 = 3;
    pub const SPE: u32 = 1 << 6;
    pub const LSBFIRST: u32 = 1 << 7;
    pub const SSI: u32 = 1 << 8;
    pub const SSM: u32 = 1 << 9;
    pub const DFF: u32 = 1 << 11;
}

// SPI driver structure
pub struct Spi {
    registers: *mut SpiRegisters,
}

impl Spi {
    /// Create a new SPI instance from a base address
    pub unsafe fn new(base_address: usize) -> Self {
        Spi {
            registers: base_address as *mut SpiRegisters,
        }
    }
    
    /// Initialize the SPI peripheral with the given configuration
    pub fn init(&mut self, config: SpiConfig) {
        unsafe {
            let regs = &mut *self.registers;
            
            // Disable SPI before configuration
            regs.cr1 &= !cr1::SPE;
            
            let mut cr1_value = 0u32;
            
            // Configure clock polarity
            match config.clock_polarity {
                Polarity::IdleHigh => cr1_value |= cr1::CPOL,
                Polarity::IdleLow => {},
            }
            
            // Configure clock phase
            match config.clock_phase {
                Phase::CaptureOnSecondTransition => cr1_value |= cr1::CPHA,
                Phase::CaptureOnFirstTransition => {},
            }
            
            // Configure mode (master/slave)
            match config.mode {
                Mode::Master => cr1_value |= cr1::MSTR,
                Mode::Slave => {},
            }
            
            // Configure baud rate
            cr1_value |= (config.baud_rate as u32) << cr1::BR_SHIFT;
            
            // Configure data size
            match config.data_size {
                DataSize::SixteenBit => cr1_value |= cr1::DFF,
                DataSize::EightBit => {},
            }
            
            // Configure bit order
            match config.bit_order {
                BitOrder::LsbFirst => cr1_value |= cr1::LSBFIRST,
                BitOrder::MsbFirst => {},
            }
            
            // Software slave management
            cr1_value |= cr1::SSM | cr1::SSI;
            
            // Write configuration to CR1
            ptr::write_volatile(&mut regs.cr1, cr1_value);
            
            // Enable SPI
            regs.cr1 |= cr1::SPE;
        }
    }
    
    /// Transmit a single byte
    pub fn transmit(&mut self, data: u8) -> Result<(), SpiError> {
        unsafe {
            let regs = &mut *self.registers;
            
            // Wait until TX buffer is empty
            while (regs.sr & (1 << 1)) == 0 {}
            
            // Write data to data register
            ptr::write_volatile(&mut regs.dr, data as u32);
            
            Ok(())
        }
    }
    
    /// Receive a single byte
    pub fn receive(&mut self) -> Result<u8, SpiError> {
        unsafe {
            let regs = &mut *self.registers;
            
            // Wait until RX buffer is not empty
            while (regs.sr & (1 << 0)) == 0 {}
            
            // Read data from data register
            let data = ptr::read_volatile(&regs.dr) as u8;
            
            Ok(data)
        }
    }
    
    /// Transfer data (transmit and receive simultaneously)
    pub fn transfer(&mut self, data: u8) -> Result<u8, SpiError> {
        self.transmit(data)?;
        self.receive()
    }
}

#[derive(Debug)]
pub enum SpiError {
    Timeout,
    Overrun,
    ModeFault,
}

// GPIO configuration for SPI pins
pub mod gpio {
    use core::ptr;
    
    #[repr(C)]
    pub struct GpioRegisters {
        moder: u32,
        otyper: u32,
        ospeedr: u32,
        pupdr: u32,
        idr: u32,
        odr: u32,
        bsrr: u32,
        lckr: u32,
        afr: [u32; 2],
    }
    
    pub struct GpioPin {
        port: *mut GpioRegisters,
        pin: u8,
    }
    
    impl GpioPin {
        pub unsafe fn new(port_address: usize, pin: u8) -> Self {
            GpioPin {
                port: port_address as *mut GpioRegisters,
                pin,
            }
        }
        
        /// Configure pin as alternate function
        pub fn configure_alternate_function(&mut self, af: u8) {
            unsafe {
                let port = &mut *self.port;
                let pin = self.pin as usize;
                
                // Set mode to alternate function (10)
                port.moder = (port.moder & !(3 << (pin * 2))) | (2 << (pin * 2));
                
                // Set alternate function
                let afr_index = if pin < 8 { 0 } else { 1 };
                let afr_pin = if pin < 8 { pin } else { pin - 8 };
                port.afr[afr_index] = (port.afr[afr_index] & !(0xF << (afr_pin * 4)))
                    | ((af as u32) << (afr_pin * 4));
                
                // Set output speed to high
                port.ospeedr |= 3 << (pin * 2);
                
                // No pull-up, no pull-down
                port.pupdr &= !(3 << (pin * 2));
            }
        }
    }
}

// Example usage
#[no_mangle]
pub extern "C" fn main() -> ! {
    // Base addresses (example)
    const SPI1_BASE: usize = 0x4001_3000;
    const GPIOA_BASE: usize = 0x4002_0000;
    
    unsafe {
        // Configure GPIO pins for SPI
        let mut sck_pin = gpio::GpioPin::new(GPIOA_BASE, 5);
        let mut miso_pin = gpio::GpioPin::new(GPIOA_BASE, 6);
        let mut mosi_pin = gpio::GpioPin::new(GPIOA_BASE, 7);
        
        // Set alternate function 5 for SPI1
        sck_pin.configure_alternate_function(5);
        miso_pin.configure_alternate_function(5);
        mosi_pin.configure_alternate_function(5);
        
        // Create and initialize SPI
        let mut spi = Spi::new(SPI1_BASE);
        
        let config = SpiConfig {
            mode: Mode::Master,
            clock_polarity: Polarity::IdleLow,
            clock_phase: Phase::CaptureOnFirstTransition,
            data_size: DataSize::EightBit,
            baud_rate: BaudRate::Div16,
            bit_order: BitOrder::MsbFirst,
        };
        
        spi.init(config);
        
        // Transfer data
        loop {
            match spi.transfer(0xAA) {
                Ok(received) => {
                    // Process received data
                },
                Err(_) => {
                    // Handle error
                }
            }
        }
    }
}
```

## Summary

SPI driver initialization is a critical embedded systems task that requires precise configuration of hardware peripherals. The process involves enabling peripheral clocks, configuring GPIO pins with appropriate alternate functions, and setting SPI communication parameters including clock polarity, phase, baud rate, and data format. The C/C++ implementation demonstrates direct register manipulation typical in bare-metal programming, while the Rust implementation showcases memory-safe abstractions with volatile operations and type-safe configuration enums. Both approaches achieve the same goal: establishing reliable SPI communication between a microcontroller and peripheral devices. Proper initialization ensures data integrity and timing compliance with connected devices, making it foundational knowledge for embedded developers working with sensors, displays, memory chips, and other SPI-compatible peripherals.