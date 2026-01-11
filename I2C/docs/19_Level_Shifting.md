# I²C Level Shifting: Interfacing Devices with Different Voltage Levels

## Overview

Level shifting is a critical aspect of I²C communication when interfacing devices that operate at different voltage levels. Modern embedded systems often combine components with varying logic levels (e.g., 3.3V microcontrollers with 5V sensors), making level shifting essential for reliable communication.

## Why Level Shifting is Needed

I²C buses use open-drain/open-collector outputs with pull-up resistors. The voltage level is determined by the pull-up voltage (VDD). When devices operate at different voltages:

- A 5V device pulling up to 5V can damage a 3.3V device's inputs
- A 3.3V device may not reach the logic high threshold (VIH) of a 5V device
- Direct connection without level shifting can cause communication failures or permanent damage

## Common Voltage Level Scenarios

1. **3.3V ↔ 5V**: Most common (e.g., ESP32, STM32 at 3.3V with legacy 5V sensors)
2. **1.8V ↔ 3.3V**: Modern low-power MCUs with standard peripherals
3. **3.3V ↔ 2.5V**: Industrial and automotive applications
4. **Multi-voltage systems**: Systems with 3+ different voltage domains

## Level Shifting Techniques

### 1. **MOSFET-Based Bidirectional Level Shifter**

This is the most popular solution for I²C, using two N-channel MOSFETs.

**How it works:**
- When either side pulls low, the MOSFET conducts, pulling the other side low
- When released, pull-up resistors on both sides pull to their respective voltages
- Bidirectional operation with no direction control needed

**Circuit:**
```
VDD_LOW (3.3V)          VDD_HIGH (5V)
    |                        |
   [R1] 4.7kΩ              [R2] 4.7kΩ
    |                        |
    ├─────────┬──────────────┤
    |         │              |
  SDA_LOW   [G]──[S]       SDA_HIGH
              │  MOSFET      
            [D]──────────────┘
              │
             GND
```

### 2. **Dedicated I²C Level Shifter ICs**

ICs like PCA9306, TXS0102, or FXMA2102 provide integrated solutions.

**Advantages:**
- Simplified design
- Guaranteed performance
- Built-in ESD protection
- Higher speed capability

### 3. **Resistor Divider (Unidirectional, High-to-Low Only)**

Simple but only works for one-way communication from high voltage to low voltage.

**Warning:** Not recommended for I²C due to bidirectional nature and timing constraints.

## Code Examples

### C/C++ Example: ESP32 (3.3V) with 5V I²C Device

```c
#include <Wire.h>

// ESP32 I²C pins (3.3V logic)
#define SDA_PIN 21
#define SCL_PIN 22

// 5V I²C device address (e.g., PCF8574 I/O expander)
#define DEVICE_ADDR 0x20

void setup() {
    Serial.begin(115200);
    
    // Initialize I²C with custom pins
    // Hardware level shifter (e.g., PCA9306) between ESP32 and device
    Wire.begin(SDA_PIN, SCL_PIN);
    Wire.setClock(100000); // 100kHz for safer operation with level shifters
    
    Serial.println("I²C with Level Shifter Initialized");
}

void loop() {
    // Write to 5V device through level shifter
    Wire.beginTransmission(DEVICE_ADDR);
    Wire.write(0xFF); // Set all pins high
    uint8_t error = Wire.endTransmission();
    
    if (error == 0) {
        Serial.println("Write successful");
    } else {
        Serial.printf("Error: %d\n", error);
    }
    
    // Read from 5V device through level shifter
    Wire.requestFrom(DEVICE_ADDR, 1);
    if (Wire.available()) {
        uint8_t data = Wire.read();
        Serial.printf("Read data: 0x%02X\n", data);
    }
    
    delay(1000);
}
```

### C Example: STM32 with Level Shifter Configuration

```c
#include "stm32f4xx_hal.h"

I2C_HandleTypeDef hi2c1;

// STM32F4 running at 3.3V communicating with 5V devices
void I2C_LevelShifter_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable clocks
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    
    // Configure I2C GPIO pins (PB8 = SCL, PB9 = SDA)
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD; // Open-drain
    GPIO_InitStruct.Pull = GPIO_NOPULL;     // External pull-ups on both sides of level shifter
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // Configure I2C peripheral
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000; // 100kHz for level shifter compatibility
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    HAL_I2C_Init(&hi2c1);
}

void I2C_Write_5V_Device(uint8_t dev_addr, uint8_t reg, uint8_t data) {
    uint8_t buf[2] = {reg, data};
    
    // HAL handles the level-shifted communication transparently
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(
        &hi2c1, 
        dev_addr << 1, 
        buf, 
        2, 
        HAL_MAX_DELAY
    );
    
    if (status != HAL_OK) {
        // Handle error
        Error_Handler();
    }
}

uint8_t I2C_Read_5V_Device(uint8_t dev_addr, uint8_t reg) {
    uint8_t data;
    
    // Write register address
    HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1, &reg, 1, HAL_MAX_DELAY);
    
    // Read data
    HAL_I2C_Master_Receive(&hi2c1, dev_addr << 1, &data, 1, HAL_MAX_DELAY);
    
    return data;
}
```

### Rust Example: Embedded HAL with Level Shifting

```rust
#![no_std]
#![no_main]

use embedded_hal::blocking::i2c::{Write, WriteRead, Read};
use panic_halt as _;

// Generic I2C device driver that works with level shifters
pub struct LevelShiftedI2CDevice<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> LevelShiftedI2CDevice<I2C>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    /// Write a single register through the level shifter
    pub fn write_register(&mut self, register: u8, value: u8) -> Result<(), E> {
        let buffer = [register, value];
        self.i2c.write(self.address, &buffer)
    }
    
    /// Read a single register through the level shifter
    pub fn read_register(&mut self, register: u8) -> Result<u8, E> {
        let mut buffer = [0u8; 1];
        self.i2c.write_read(self.address, &[register], &mut buffer)?;
        Ok(buffer[0])
    }
    
    /// Read multiple bytes through the level shifter
    pub fn read_registers(&mut self, start_register: u8, buffer: &mut [u8]) -> Result<(), E> {
        self.i2c.write_read(self.address, &[start_register], buffer)
    }
}

// Example for ESP32-C3 (RISC-V) with level shifter
#[cfg(feature = "esp32c3")]
use esp32c3_hal::{
    clock::ClockControl,
    i2c::I2C,
    gpio::IO,
    pac::Peripherals,
    prelude::*,
    Rtc,
};

#[cfg(feature = "esp32c3")]
#[entry]
fn main() -> ! {
    let peripherals = Peripherals::take().unwrap();
    let system = peripherals.SYSTEM.split();
    let clocks = ClockControl::boot_defaults(system.clock_control).freeze();
    
    let io = IO::new(peripherals.GPIO, peripherals.IO_MUX);
    
    // Initialize I2C at 100kHz for level shifter compatibility
    // GPIO4 = SDA, GPIO5 = SCL (3.3V side of level shifter)
    let i2c = I2C::new(
        peripherals.I2C0,
        io.pins.gpio4,
        io.pins.gpio5,
        100u32.kHz(),
        &clocks,
    );
    
    // Create device instance (5V device on the other side of level shifter)
    let mut device = LevelShiftedI2CDevice::new(i2c, 0x20);
    
    loop {
        // Write to 5V device through level shifter
        if let Err(_) = device.write_register(0x00, 0xFF) {
            // Handle error
        }
        
        // Read from 5V device through level shifter
        match device.read_register(0x00) {
            Ok(value) => {
                // Process value
            },
            Err(_) => {
                // Handle error
            }
        }
        
        // Delay
        esp32c3_hal::delay::Delay::new(&clocks).delay_ms(1000u32);
    }
}
```

### Rust Example: STM32 with Level Shifter

```rust
#![no_std]
#![no_main]

use cortex_m_rt::entry;
use panic_halt as _;
use stm32f4xx_hal::{
    pac,
    prelude::*,
    i2c::{I2c, Mode},
};

const DEVICE_5V_ADDR: u8 = 0x48; // Example: TMP102 temperature sensor at 5V

#[entry]
fn main() -> ! {
    // Get peripherals
    let dp = pac::Peripherals::take().unwrap();
    
    // Configure clocks
    let rcc = dp.RCC.constrain();
    let clocks = rcc.cfgr.freeze();
    
    // Configure GPIO for I2C
    let gpiob = dp.GPIOB.split();
    let scl = gpiob.pb8.into_alternate_open_drain();
    let sda = gpiob.pb9.into_alternate_open_drain();
    
    // Initialize I2C at 100kHz
    // External level shifter (e.g., PCA9306) between STM32 (3.3V) and sensor (5V)
    let mut i2c = I2c::new(
        dp.I2C1,
        (scl, sda),
        Mode::Standard { frequency: 100.kHz() },
        &clocks,
    );
    
    loop {
        // Read temperature from 5V sensor through level shifter
        let mut buffer = [0u8; 2];
        
        // Point to temperature register
        if i2c.write(DEVICE_5V_ADDR, &[0x00]).is_ok() {
            // Read temperature data
            if i2c.read(DEVICE_5V_ADDR, &mut buffer).is_ok() {
                let temp_raw = ((buffer[0] as u16) << 4) | ((buffer[1] as u16) >> 4);
                let temperature = (temp_raw as f32) * 0.0625;
                // Process temperature...
            }
        }
        
        cortex_m::asm::delay(8_000_000); // Delay
    }
}
```

## Hardware Considerations

### Pull-up Resistor Selection

When using level shifters, you need pull-ups on **both sides**:

**Low voltage side (3.3V):**
```
R_pullup = (VDD - VOL) / IOL
For 3.3V: ~4.7kΩ typical
```

**High voltage side (5V):**
```
For 5V: ~4.7kΩ to 10kΩ typical
```

**Total bus capacitance affects maximum frequency:**
```
f_max ≈ 1 / (2π × R_pullup × C_bus)
```

### Common Level Shifter ICs

| IC | Channels | Speed | Features |
|---|---|---|---|
| PCA9306 | 2 | 400kHz | Auto-direction, 1.0-3.6V ↔ 1.8-5.5V |
| TXS0102 | 2 | 24Mbps | Very fast, 1.65-3.6V ↔ 2.3-5.5V |
| TXB0104 | 4 | 100Mbps | Auto-direction, 1.2-3.6V ↔ 1.65-5.5V |
| PCA9517 | 2 | 400kHz | Bus buffer with level shift |

## Best Practices

1. **Always use level shifters** when voltage domains differ
2. **Don't rely on internal pull-ups** through a level shifter - use external pull-ups on both sides
3. **Keep traces short** between MCU/sensor and level shifter
4. **Lower clock speeds** (100kHz) are more reliable with level shifters than 400kHz
5. **Add bypass capacitors** (0.1µF) near VDD pins of level shifter
6. **Ground must be common** between all devices
7. **Test thoroughly** at temperature extremes if applicable

## Troubleshooting

**Symptoms of improper level shifting:**
- Intermittent communication failures
- No ACK from devices
- Data corruption
- Bus lockup (SDA or SCL stuck low)

**Debug steps:**
1. Verify voltages on both sides with oscilloscope/multimeter
2. Check rise times - should be < 1µs for 100kHz
3. Ensure pull-ups are present on both sides
4. Verify common ground connection
5. Reduce clock speed to 10kHz for testing

## Summary

Level shifting is non-negotiable when interfacing I²C devices at different voltages. MOSFET-based or IC-based bidirectional level shifters are the standard solutions. The code examples above work identically whether using discrete MOSFETs or integrated level shifter ICs - the microcontroller is unaware of the level shifting happening in the hardware layer.