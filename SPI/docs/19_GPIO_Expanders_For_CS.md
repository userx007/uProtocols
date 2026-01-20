# GPIO Expanders for CS (Chip Select) in SPI Communication

## Overview

When working with SPI (Serial Peripheral Interface) communication, each slave device requires a dedicated Chip Select (CS) line. Microcontrollers have a limited number of GPIO pins, which can become a constraint when interfacing with multiple SPI devices. GPIO expanders provide an elegant solution by multiplying the available CS lines through I2C or SPI-controlled I/O expansion chips.

## The Problem: Limited CS Lines

In a standard SPI configuration, the master controls multiple slaves using individual CS lines. While MISO, MOSI, and SCK can be shared among all devices, each slave needs its own CS pin. A typical microcontroller might have 20-30 GPIO pins, but after allocating pins for power, communication buses, sensors, and other peripherals, you may find yourself with insufficient pins for all required CS lines.

## Solution: GPIO Expanders

GPIO expanders are integrated circuits that communicate via I2C or SPI and provide additional digital I/O pins. Popular GPIO expanders include:

- **MCP23017/MCP23S17**: 16-bit I/O expander (I2C/SPI versions)
- **PCF8574/PCF8575**: 8-bit/16-bit I/O expander (I2C)
- **TCA9555**: 16-bit I/O expander (I2C)
- **74HC595**: 8-bit shift register (SPI-like serial interface)

## Architecture

The typical architecture involves:

1. **Primary Communication Bus**: I2C or SPI bus connecting the microcontroller to the GPIO expander
2. **GPIO Expander**: Provides multiple output pins for CS control
3. **SPI Bus**: Separate SPI bus (MISO, MOSI, SCK) connected to all slave devices
4. **CS Lines**: Routed from GPIO expander outputs to individual SPI slaves

## Code Examples

### C/C++ Example: Using MCP23017 (I2C) for SPI CS Control

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

// MCP23017 Register Addresses
#define MCP23017_IODIRA   0x00  // I/O Direction Register A
#define MCP23017_IODIRB   0x01  // I/O Direction Register B
#define MCP23017_GPIOA    0x12  // GPIO Register A
#define MCP23017_GPIOB    0x13  // GPIO Register B

// I2C address of MCP23017 (A0-A2 grounded)
#define MCP23017_ADDR     0x20

// SPI slave device identifiers
typedef enum {
    SPI_DEVICE_0 = 0,
    SPI_DEVICE_1 = 1,
    SPI_DEVICE_2 = 2,
    SPI_DEVICE_3 = 3,
    SPI_DEVICE_4 = 4,
    SPI_DEVICE_5 = 5,
    SPI_DEVICE_6 = 6,
    SPI_DEVICE_7 = 7
} spi_device_t;

// Platform-specific I2C functions (to be implemented)
extern void i2c_write_byte(uint8_t addr, uint8_t reg, uint8_t data);
extern void i2c_read_byte(uint8_t addr, uint8_t reg, uint8_t *data);
extern void spi_transfer(uint8_t *tx_data, uint8_t *rx_data, size_t len);

// Current state of CS lines
static uint8_t cs_state_porta = 0xFF;  // All high (inactive)
static uint8_t cs_state_portb = 0xFF;  // All high (inactive)

/**
 * Initialize the MCP23017 GPIO expander
 * Configure all pins as outputs and set them high (CS inactive)
 */
void gpio_expander_init(void) {
    // Configure all pins on Port A as outputs
    i2c_write_byte(MCP23017_ADDR, MCP23017_IODIRA, 0x00);
    
    // Configure all pins on Port B as outputs
    i2c_write_byte(MCP23017_ADDR, MCP23017_IODIRB, 0x00);
    
    // Set all CS lines high (inactive)
    i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOA, 0xFF);
    i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOB, 0xFF);
    
    cs_state_porta = 0xFF;
    cs_state_portb = 0xFF;
    
    printf("GPIO Expander initialized: 16 CS lines available\n");
}

/**
 * Select a specific SPI device by driving its CS line low
 */
void spi_select_device(spi_device_t device) {
    if (device < 8) {
        // Device on Port A
        cs_state_porta &= ~(1 << device);  // Clear bit (drive low)
        i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOA, cs_state_porta);
    } else if (device < 16) {
        // Device on Port B
        cs_state_portb &= ~(1 << (device - 8));
        i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOB, cs_state_portb);
    }
}

/**
 * Deselect a specific SPI device by driving its CS line high
 */
void spi_deselect_device(spi_device_t device) {
    if (device < 8) {
        // Device on Port A
        cs_state_porta |= (1 << device);  // Set bit (drive high)
        i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOA, cs_state_porta);
    } else if (device < 16) {
        // Device on Port B
        cs_state_portb |= (1 << (device - 8));
        i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOB, cs_state_portb);
    }
}

/**
 * Deselect all SPI devices
 */
void spi_deselect_all(void) {
    cs_state_porta = 0xFF;
    cs_state_portb = 0xFF;
    i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOA, 0xFF);
    i2c_write_byte(MCP23017_ADDR, MCP23017_GPIOB, 0xFF);
}

/**
 * Perform SPI transaction with a specific device
 */
bool spi_transaction(spi_device_t device, uint8_t *tx_data, 
                     uint8_t *rx_data, size_t len) {
    if (device >= 16) {
        return false;
    }
    
    // Select the device
    spi_select_device(device);
    
    // Small delay for CS setup time (device-dependent)
    // delay_us(1);
    
    // Perform SPI transfer
    spi_transfer(tx_data, rx_data, len);
    
    // Small delay for CS hold time
    // delay_us(1);
    
    // Deselect the device
    spi_deselect_device(device);
    
    return true;
}

/**
 * Example: Read from multiple sensors
 */
void read_multiple_sensors(void) {
    uint8_t tx_buf[3] = {0x00, 0x00, 0x00};  // Read command
    uint8_t rx_buf[3] = {0};
    
    // Read from device 0
    spi_transaction(SPI_DEVICE_0, tx_buf, rx_buf, 3);
    printf("Device 0 data: 0x%02X 0x%02X 0x%02X\n", 
           rx_buf[0], rx_buf[1], rx_buf[2]);
    
    // Read from device 3
    spi_transaction(SPI_DEVICE_3, tx_buf, rx_buf, 3);
    printf("Device 3 data: 0x%02X 0x%02X 0x%02X\n", 
           rx_buf[0], rx_buf[1], rx_buf[2]);
}

int main(void) {
    // Initialize hardware
    gpio_expander_init();
    
    // Example usage
    read_multiple_sensors();
    
    return 0;
}
```

### C++ Example: Object-Oriented Approach with 74HC595 Shift Register

```cpp
#include <cstdint>
#include <array>
#include <memory>

class ShiftRegister74HC595 {
private:
    uint8_t data_pin;
    uint8_t clock_pin;
    uint8_t latch_pin;
    uint8_t current_state;
    
    // Platform-specific GPIO functions
    void digitalWrite(uint8_t pin, bool value);
    void delayMicroseconds(uint32_t us);
    
public:
    ShiftRegister74HC595(uint8_t data, uint8_t clock, uint8_t latch)
        : data_pin(data), clock_pin(clock), latch_pin(latch), 
          current_state(0xFF) {}
    
    void begin() {
        // Configure pins as outputs
        // pinMode(data_pin, OUTPUT);
        // pinMode(clock_pin, OUTPUT);
        // pinMode(latch_pin, OUTPUT);
        
        // Initialize all CS lines high
        shiftOut(0xFF);
    }
    
    void shiftOut(uint8_t value) {
        // Latch low to prepare for data
        digitalWrite(latch_pin, false);
        
        // Shift out 8 bits, MSB first
        for (int i = 7; i >= 0; i--) {
            digitalWrite(clock_pin, false);
            digitalWrite(data_pin, (value >> i) & 0x01);
            digitalWrite(clock_pin, true);
        }
        
        // Latch high to update outputs
        digitalWrite(latch_pin, true);
        
        current_state = value;
    }
    
    void setCS(uint8_t device, bool active) {
        if (device >= 8) return;
        
        if (active) {
            current_state &= ~(1 << device);  // Active low
        } else {
            current_state |= (1 << device);   // Inactive high
        }
        
        shiftOut(current_state);
    }
    
    void selectDevice(uint8_t device) {
        setCS(device, true);
    }
    
    void deselectDevice(uint8_t device) {
        setCS(device, false);
    }
    
    void deselectAll() {
        shiftOut(0xFF);
    }
};

class SPIManager {
private:
    std::unique_ptr<ShiftRegister74HC595> cs_expander;
    
    void spiTransfer(uint8_t* tx_data, uint8_t* rx_data, size_t len) {
        // Platform-specific SPI transfer
    }
    
public:
    SPIManager(uint8_t data_pin, uint8_t clock_pin, uint8_t latch_pin) {
        cs_expander = std::make_unique<ShiftRegister74HC595>(
            data_pin, clock_pin, latch_pin
        );
    }
    
    void begin() {
        cs_expander->begin();
        // Initialize SPI hardware
    }
    
    bool transaction(uint8_t device, uint8_t* tx_data, 
                    uint8_t* rx_data, size_t len) {
        if (device >= 8) return false;
        
        cs_expander->selectDevice(device);
        spiTransfer(tx_data, rx_data, len);
        cs_expander->deselectDevice(device);
        
        return true;
    }
    
    // Convenience method for read operations
    std::array<uint8_t, 4> readRegister(uint8_t device, uint8_t reg) {
        std::array<uint8_t, 4> tx_data = {reg, 0x00, 0x00, 0x00};
        std::array<uint8_t, 4> rx_data = {0};
        
        transaction(device, tx_data.data(), rx_data.data(), 4);
        
        return rx_data;
    }
};

int main() {
    // Create SPI manager with shift register on pins 2, 3, 4
    SPIManager spi(2, 3, 4);
    spi.begin();
    
    // Read from device 0, register 0x10
    auto data = spi.readRegister(0, 0x10);
    
    return 0;
}
```

### Rust Example: Safe Abstraction with embedded-hal

```rust
use embedded_hal::blocking::i2c::{Write, WriteRead};
use embedded_hal::blocking::spi::Transfer;
use embedded_hal::digital::v2::OutputPin;

// MCP23017 register definitions
const MCP23017_IODIRA: u8 = 0x00;
const MCP23017_IODIRB: u8 = 0x01;
const MCP23017_GPIOA: u8 = 0x12;
const MCP23017_GPIOB: u8 = 0x13;

#[derive(Debug)]
pub enum GpioExpanderError<E> {
    I2cError(E),
    InvalidDevice,
}

pub struct Mcp23017<I2C> {
    i2c: I2C,
    address: u8,
    port_a_state: u8,
    port_b_state: u8,
}

impl<I2C, E> Mcp23017<I2C>
where
    I2C: Write<Error = E> + WriteRead<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self {
            i2c,
            address,
            port_a_state: 0xFF,
            port_b_state: 0xFF,
        }
    }

    pub fn init(&mut self) -> Result<(), GpioExpanderError<E>> {
        // Configure all pins as outputs
        self.write_register(MCP23017_IODIRA, 0x00)?;
        self.write_register(MCP23017_IODIRB, 0x00)?;

        // Set all CS lines high (inactive)
        self.write_register(MCP23017_GPIOA, 0xFF)?;
        self.write_register(MCP23017_GPIOB, 0xFF)?;

        Ok(())
    }

    fn write_register(&mut self, register: u8, value: u8) -> Result<(), GpioExpanderError<E>> {
        self.i2c
            .write(self.address, &[register, value])
            .map_err(GpioExpanderError::I2cError)
    }

    pub fn set_pin(&mut self, pin: u8, state: bool) -> Result<(), GpioExpanderError<E>> {
        if pin >= 16 {
            return Err(GpioExpanderError::InvalidDevice);
        }

        if pin < 8 {
            // Port A
            if state {
                self.port_a_state |= 1 << pin;
            } else {
                self.port_a_state &= !(1 << pin);
            }
            self.write_register(MCP23017_GPIOA, self.port_a_state)?;
        } else {
            // Port B
            let pin_b = pin - 8;
            if state {
                self.port_b_state |= 1 << pin_b;
            } else {
                self.port_b_state &= !(1 << pin_b);
            }
            self.write_register(MCP23017_GPIOB, self.port_b_state)?;
        }

        Ok(())
    }

    pub fn select_cs(&mut self, device: u8) -> Result<(), GpioExpanderError<E>> {
        self.set_pin(device, false) // CS is active low
    }

    pub fn deselect_cs(&mut self, device: u8) -> Result<(), GpioExpanderError<E>> {
        self.set_pin(device, true) // CS is active high when deselected
    }

    pub fn deselect_all(&mut self) -> Result<(), GpioExpanderError<E>> {
        self.port_a_state = 0xFF;
        self.port_b_state = 0xFF;
        self.write_register(MCP23017_GPIOA, 0xFF)?;
        self.write_register(MCP23017_GPIOB, 0xFF)?;
        Ok(())
    }
}

pub struct SpiWithExpander<SPI, I2C> {
    spi: SPI,
    expander: Mcp23017<I2C>,
}

impl<SPI, I2C, SE, IE> SpiWithExpander<SPI, I2C>
where
    SPI: Transfer<u8, Error = SE>,
    I2C: Write<Error = IE> + WriteRead<Error = IE>,
{
    pub fn new(spi: SPI, expander: Mcp23017<I2C>) -> Self {
        Self { spi, expander }
    }

    pub fn transaction(
        &mut self,
        device: u8,
        data: &mut [u8],
    ) -> Result<(), GpioExpanderError<IE>> {
        // Select the device
        self.expander.select_cs(device)?;

        // Perform SPI transfer
        // Note: In real implementation, handle SPI errors appropriately
        let _ = self.spi.transfer(data);

        // Deselect the device
        self.expander.deselect_cs(device)?;

        Ok(())
    }

    pub fn read_register(
        &mut self,
        device: u8,
        register: u8,
        buffer: &mut [u8],
    ) -> Result<(), GpioExpanderError<IE>> {
        let mut tx_data = [register, 0x00, 0x00, 0x00];
        
        self.expander.select_cs(device)?;
        let _ = self.spi.transfer(&mut tx_data);
        self.expander.deselect_cs(device)?;

        buffer.copy_from_slice(&tx_data[1..]);
        Ok(())
    }
}

// Example usage with a hypothetical embedded system
#[cfg(feature = "example")]
fn main() -> ! {
    // Initialize I2C and SPI peripherals (platform-specific)
    let i2c = initialize_i2c();
    let spi = initialize_spi();

    // Create and initialize GPIO expander
    let mut expander = Mcp23017::new(i2c, 0x20);
    expander.init().unwrap();

    // Create SPI manager with expander
    let mut spi_manager = SpiWithExpander::new(spi, expander);

    loop {
        let mut data = [0x00, 0x00, 0x00];
        
        // Read from device 0
        spi_manager.transaction(0, &mut data).unwrap();
        
        // Process data...
        
        delay_ms(100);
    }
}
```

## Key Considerations

### Performance Impact

Using GPIO expanders introduces additional latency compared to direct GPIO control. The I2C communication for changing CS lines adds overhead, typically ranging from 50-200 microseconds depending on I2C clock speed. For high-speed SPI applications, this may be a bottleneck.

### Timing Requirements

Ensure that CS setup and hold times specified in the SPI slave datasheets are met, accounting for the additional I2C communication time. Some devices may require faster CS switching than GPIO expanders can provide.

### Power Consumption

GPIO expanders consume additional power, particularly I2C-based ones that maintain active communication. Consider low-power modes if battery operation is required.

### Error Handling

Implement robust error handling for I2C communication failures, as these can leave CS lines in undefined states, potentially corrupting SPI transactions.

## Summary

GPIO expanders provide a practical solution for managing multiple SPI devices when microcontroller GPIO pins are limited. By using I2C or shift register-based expanders like the MCP23017 or 74HC595, you can control 8-16 or more CS lines using just 2-3 microcontroller pins. The trade-off involves added latency from the expander communication, which must be considered against the SPI device timing requirements. The code examples demonstrate implementation approaches in C, C++, and Rust, showcasing proper initialization, device selection, and transaction management. This technique is widely used in industrial applications, multi-sensor systems, and complex embedded designs where pin count optimization is essential.