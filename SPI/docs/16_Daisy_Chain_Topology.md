# SPI Daisy Chain Topology

## Detailed Description

**Daisy Chain Topology** is an SPI configuration where multiple SPI devices are connected in series rather than in parallel. In this arrangement, the data output (MISO/SDO) of one device connects to the data input (MOSI/SDI) of the next device, creating a chain. This topology significantly reduces the number of chip select (CS) pins required on the microcontroller, as all devices in the chain can share a single CS line.

### How It Works

In a daisy chain configuration:

1. **Serial Connection**: The master's MOSI connects to the first slave's data input. The first slave's data output connects to the second slave's data input, and so on. The last slave's data output connects back to the master's MISO.

2. **Shared Control Lines**: All devices share the same clock (SCK) and chip select (CS) signals.

3. **Data Propagation**: When the master sends data, it shifts through each device in the chain. After N clock cycles (where N = total bits for all devices), each device has received its designated data, and the master has received data back from all devices.

4. **Bit Shifting**: Each device typically has an internal shift register. On each clock cycle, data shifts in from the previous device and shifts out to the next device.

### Advantages

- **Reduced Pin Count**: Only one CS pin needed regardless of the number of devices
- **Simplified Wiring**: Fewer connections between master and slaves
- **Scalability**: Easy to add more devices to the chain

### Disadvantages

- **Slower Communication**: Must clock through all devices to update any single device
- **Device Dependency**: All devices must support daisy chaining (not all SPI devices do)
- **Complexity**: More complex software to manage data distribution
- **Propagation Delay**: Longer chains increase communication latency

### Common Use Cases

- **LED Drivers**: Cascaded shift registers (74HC595) or LED controllers (MAX7219)
- **ADC/DAC Chains**: Multiple analog converters in series
- **Display Modules**: Segmented displays or dot matrix arrays
- **IO Expanders**: Extending digital I/O across multiple chips

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <string.h>

// Hardware abstraction (platform-specific)
void spi_init(void);
void spi_select(void);
void spi_deselect(void);
uint8_t spi_transfer(uint8_t data);
void delay_us(uint32_t us);

// Configuration for daisy chain
#define NUM_DEVICES 3
#define BYTES_PER_DEVICE 2

// Daisy chain structure
typedef struct {
    uint8_t num_devices;
    uint8_t bytes_per_device;
    uint8_t total_bytes;
    uint8_t tx_buffer[NUM_DEVICES * BYTES_PER_DEVICE];
    uint8_t rx_buffer[NUM_DEVICES * BYTES_PER_DEVICE];
} daisy_chain_t;

// Initialize daisy chain
void daisy_chain_init(daisy_chain_t *chain, uint8_t num_devices, 
                      uint8_t bytes_per_device) {
    chain->num_devices = num_devices;
    chain->bytes_per_device = bytes_per_device;
    chain->total_bytes = num_devices * bytes_per_device;
    memset(chain->tx_buffer, 0, chain->total_bytes);
    memset(chain->rx_buffer, 0, chain->total_bytes);
    
    spi_init();
}

// Update specific device in the chain
void daisy_chain_set_device_data(daisy_chain_t *chain, uint8_t device_index,
                                 const uint8_t *data, uint8_t length) {
    if (device_index >= chain->num_devices || 
        length > chain->bytes_per_device) {
        return;
    }
    
    // Data for first device goes to the end of the buffer
    // (it will be shifted out first)
    uint8_t buffer_offset = (chain->num_devices - 1 - device_index) * 
                            chain->bytes_per_device;
    memcpy(&chain->tx_buffer[buffer_offset], data, length);
}

// Transfer data through entire daisy chain
void daisy_chain_transfer(daisy_chain_t *chain) {
    spi_select();
    delay_us(1);
    
    // Clock out data through entire chain
    for (int i = 0; i < chain->total_bytes; i++) {
        chain->rx_buffer[i] = spi_transfer(chain->tx_buffer[i]);
    }
    
    delay_us(1);
    spi_deselect();
}

// Get data from specific device in chain
void daisy_chain_get_device_data(daisy_chain_t *chain, uint8_t device_index,
                                 uint8_t *data, uint8_t length) {
    if (device_index >= chain->num_devices || 
        length > chain->bytes_per_device) {
        return;
    }
    
    // Data from first device is at the end of rx buffer
    uint8_t buffer_offset = (chain->num_devices - 1 - device_index) * 
                            chain->bytes_per_device;
    memcpy(data, &chain->rx_buffer[buffer_offset], length);
}

// Example: Controlling cascaded 74HC595 shift registers
void example_led_control(void) {
    daisy_chain_t chain;
    daisy_chain_init(&chain, 3, 1); // 3 devices, 1 byte each
    
    // Set LED patterns for each shift register
    uint8_t pattern1 = 0b10101010;
    uint8_t pattern2 = 0b11001100;
    uint8_t pattern3 = 0b11110000;
    
    daisy_chain_set_device_data(&chain, 0, &pattern1, 1);
    daisy_chain_set_device_data(&chain, 1, &pattern2, 1);
    daisy_chain_set_device_data(&chain, 2, &pattern3, 1);
    
    // Send all data at once
    daisy_chain_transfer(&chain);
}

// Example: Reading from cascaded ADCs
typedef struct {
    uint16_t channel0;
    uint16_t channel1;
} adc_reading_t;

void example_adc_chain(void) {
    daisy_chain_t chain;
    daisy_chain_init(&chain, 2, 2); // 2 ADCs, 2 bytes each
    
    // Command to read from ADCs (device-specific)
    uint8_t read_cmd[2] = {0x01, 0x80};
    
    daisy_chain_set_device_data(&chain, 0, read_cmd, 2);
    daisy_chain_set_device_data(&chain, 1, read_cmd, 2);
    
    daisy_chain_transfer(&chain);
    
    // Extract readings
    adc_reading_t adc0_data, adc1_data;
    daisy_chain_get_device_data(&chain, 0, (uint8_t*)&adc0_data, 2);
    daisy_chain_get_device_data(&chain, 1, (uint8_t*)&adc1_data, 2);
}
```

### Rust Implementation

```rust
use embedded_hal::spi::{SpiDevice, SpiBus};
use embedded_hal::digital::OutputPin;

/// Daisy chain configuration and state
pub struct DaisyChain<SPI> {
    spi: SPI,
    num_devices: usize,
    bytes_per_device: usize,
    tx_buffer: Vec<u8>,
    rx_buffer: Vec<u8>,
}

impl<SPI, E> DaisyChain<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    /// Create a new daisy chain controller
    pub fn new(spi: SPI, num_devices: usize, bytes_per_device: usize) -> Self {
        let total_bytes = num_devices * bytes_per_device;
        Self {
            spi,
            num_devices,
            bytes_per_device,
            tx_buffer: vec![0u8; total_bytes],
            rx_buffer: vec![0u8; total_bytes],
        }
    }

    /// Set data for a specific device in the chain
    /// device_index: 0 = first device in chain (closest to master)
    pub fn set_device_data(&mut self, device_index: usize, data: &[u8]) -> Result<(), &'static str> {
        if device_index >= self.num_devices {
            return Err("Device index out of range");
        }
        
        if data.len() > self.bytes_per_device {
            return Err("Data length exceeds bytes per device");
        }

        // First device data goes at the end (shifted out first)
        let offset = (self.num_devices - 1 - device_index) * self.bytes_per_device;
        self.tx_buffer[offset..offset + data.len()].copy_from_slice(data);
        
        Ok(())
    }

    /// Transfer data through the entire daisy chain
    pub fn transfer(&mut self) -> Result<(), E> {
        self.spi.transfer(&mut self.rx_buffer, &self.tx_buffer)?;
        Ok(())
    }

    /// Get data received from a specific device
    pub fn get_device_data(&self, device_index: usize, data: &mut [u8]) -> Result<(), &'static str> {
        if device_index >= self.num_devices {
            return Err("Device index out of range");
        }
        
        if data.len() > self.bytes_per_device {
            return Err("Data buffer too large");
        }

        let offset = (self.num_devices - 1 - device_index) * self.bytes_per_device;
        data.copy_from_slice(&self.rx_buffer[offset..offset + data.len()]);
        
        Ok(())
    }

    /// Clear all buffers
    pub fn clear(&mut self) {
        self.tx_buffer.fill(0);
        self.rx_buffer.fill(0);
    }

    /// Get total number of bytes in chain
    pub fn total_bytes(&self) -> usize {
        self.num_devices * self.bytes_per_device
    }
}

// Example: MAX7219 LED matrix driver chain
pub struct Max7219Chain<SPI> {
    chain: DaisyChain<SPI>,
}

impl<SPI, E> Max7219Chain<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    const BYTES_PER_MAX7219: usize = 2; // Register address + data

    pub fn new(spi: SPI, num_displays: usize) -> Self {
        Self {
            chain: DaisyChain::new(spi, num_displays, Self::BYTES_PER_MAX7219),
        }
    }

    /// Send command to specific MAX7219 in chain
    pub fn write_register(&mut self, display: usize, register: u8, value: u8) -> Result<(), E> {
        let data = [register, value];
        self.chain.set_device_data(display, &data)
            .map_err(|_| panic!("Invalid display index"));
        self.chain.transfer()?;
        Ok(())
    }

    /// Send same command to all MAX7219 devices
    pub fn write_all(&mut self, register: u8, value: u8) -> Result<(), E> {
        let data = [register, value];
        for i in 0..self.chain.num_devices {
            self.chain.set_device_data(i, &data)
                .map_err(|_| panic!("Invalid display index"));
        }
        self.chain.transfer()?;
        Ok(())
    }

    /// Initialize all MAX7219 displays
    pub fn init(&mut self) -> Result<(), E> {
        self.write_all(0x09, 0x00)?; // Decode mode: no decode
        self.write_all(0x0A, 0x08)?; // Intensity: medium
        self.write_all(0x0B, 0x07)?; // Scan limit: all digits
        self.write_all(0x0C, 0x01)?; // Shutdown: normal operation
        self.write_all(0x0F, 0x00)?; // Display test: off
        Ok(())
    }
}

// Example: Cascaded shift registers (74HC595)
pub struct ShiftRegisterChain<SPI> {
    chain: DaisyChain<SPI>,
}

impl<SPI, E> ShiftRegisterChain<SPI>
where
    SPI: SpiDevice<Error = E>,
{
    pub fn new(spi: SPI, num_registers: usize) -> Self {
        Self {
            chain: DaisyChain::new(spi, num_registers, 1),
        }
    }

    /// Set output state for a specific shift register
    pub fn set_outputs(&mut self, register: usize, value: u8) -> Result<(), E> {
        self.chain.set_device_data(register, &[value])
            .map_err(|_| panic!("Invalid register index"));
        self.chain.transfer()?;
        Ok(())
    }

    /// Set all outputs in the chain at once
    pub fn set_all_outputs(&mut self, values: &[u8]) -> Result<(), E> {
        if values.len() != self.chain.num_devices {
            panic!("Values array must match number of devices");
        }

        for (i, &value) in values.iter().enumerate() {
            self.chain.set_device_data(i, &[value])
                .map_err(|_| panic!("Invalid register index"));
        }
        
        self.chain.transfer()?;
        Ok(())
    }

    /// Clear all outputs (set to 0)
    pub fn clear_all(&mut self) -> Result<(), E> {
        self.chain.clear();
        self.chain.transfer()?;
        Ok(())
    }
}

// Usage example with embedded-hal traits
#[cfg(feature = "example")]
mod example {
    use super::*;

    pub fn led_pattern_demo<SPI, E>(spi: SPI) -> Result<(), E>
    where
        SPI: SpiDevice<Error = E>,
    {
        // Create chain of 4 shift registers
        let mut sr_chain = ShiftRegisterChain::new(spi, 4);

        // Set different patterns on each register
        let patterns = [
            0b10101010,
            0b11001100,
            0b11110000,
            0b11111111,
        ];

        sr_chain.set_all_outputs(&patterns)?;

        Ok(())
    }

    pub fn max7219_demo<SPI, E>(spi: SPI) -> Result<(), E>
    where
        SPI: SpiDevice<Error = E>,
    {
        // Create chain of 3 MAX7219 LED displays
        let mut displays = Max7219Chain::new(spi, 3);

        // Initialize all displays
        displays.init()?;

        // Display different patterns on each
        for display in 0..3 {
            for row in 1..=8 {
                displays.write_register(display, row, 0xFF)?;
            }
        }

        Ok(())
    }
}
```

## Summary

**SPI Daisy Chain Topology** connects multiple SPI devices in series, where each device's output feeds into the next device's input, forming a chain. This configuration uses only one chip select line for all devices, significantly reducing pin count requirements on the microcontroller. The master clocks data through the entire chain, with each device capturing its portion of the data stream as it passes through.

**Key advantages** include reduced wiring complexity and minimal GPIO usage, making it ideal for applications like LED matrices, cascaded shift registers, and multiple identical sensors. **The main tradeoff** is slower communication speed, as the master must clock through all devices even when updating just one, and all devices must support daisy-chaining functionality. This topology is most effective when you need to control many similar devices and communication speed is not the primary concern.