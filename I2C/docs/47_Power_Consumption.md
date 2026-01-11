# I2C Power Consumption: Minimizing Power Usage in Battery-Powered Applications

## Overview

Power consumption is critical in battery-powered I2C applications like wearables, IoT sensors, and remote monitoring devices. I2C itself is relatively power-efficient, but careful design of both hardware and software is essential to maximize battery life.

## Key Power Consumption Factors

### 1. **Pull-up Resistor Power**
The I2C bus requires pull-up resistors that constantly draw current when the bus lines are pulled low. Power dissipation occurs during both communication and idle states.

**Power calculation:**
```
P = V² / R
```
For 3.3V with 4.7kΩ pull-ups: P ≈ 2.3mW per resistor (when line is low)

### 2. **Active Communication Power**
- Master and slave devices consume power during transactions
- Clock frequency affects power (higher speed = more power)
- Bus capacitance charging/discharging

### 3. **Idle/Sleep Mode Power**
- Static current draw when devices are powered but inactive
- Pull-up resistor leakage current

## Power Reduction Strategies

### 1. **Optimize Pull-up Resistors**
Use higher resistance values (10kΩ-47kΩ) for lower power, but ensure they meet bus timing requirements:
- Lower capacitance buses can use higher resistance
- Trade-off: higher resistance = slower rise times

### 2. **Reduce Clock Frequency**
Run I2C at the minimum speed needed (100 kHz standard mode vs 400 kHz fast mode).

### 3. **Use Sleep Modes**
Put both master and slave devices into low-power modes between transactions.

### 4. **Minimize Transaction Length**
- Batch data transfers
- Use burst reads/writes
- Avoid polling; use interrupts instead

### 5. **Power Down Pull-ups**
Actively control pull-up power when bus is idle (requires additional circuitry).

## Code Examples

### C/C++ Implementation (STM32)

```c
/**
 * I2C Low Power Implementation for STM32
 * Demonstrates power-saving techniques for battery-powered applications
 */

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

// Device addresses
#define SENSOR_ADDR 0x48
#define I2C_TIMEOUT 100

// Power management structure
typedef struct {
    uint32_t sleep_duration_ms;
    uint32_t wake_interval_ms;
    bool i2c_active;
} PowerManager_t;

I2C_HandleTypeDef hi2c1;
PowerManager_t power_mgr = {0};

/**
 * Initialize I2C in low-power configuration
 * - Uses 100kHz (standard mode) instead of 400kHz
 * - Configures GPIO for low power when I2C is disabled
 */
void I2C_LowPower_Init(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Enable clocks
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C1_CLK_ENABLE();
    
    // Configure I2C pins (PB8=SCL, PB9=SDA)
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL; // External pull-ups used
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW; // Low speed for power savings
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // Configure I2C for low power
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;  // 100kHz - lower power than 400kHz
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
    
    power_mgr.i2c_active = true;
}

/**
 * Disable I2C peripheral and configure pins for minimal power
 */
void I2C_LowPower_Disable(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    // Deinitialize I2C
    HAL_I2C_DeInit(&hi2c1);
    
    // Configure pins as analog input (lowest power consumption)
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // Disable I2C clock
    __HAL_RCC_I2C1_CLK_DISABLE();
    
    power_mgr.i2c_active = false;
}

/**
 * Re-enable I2C after low-power mode
 */
void I2C_LowPower_Enable(void) {
    if (!power_mgr.i2c_active) {
        I2C_LowPower_Init();
    }
}

/**
 * Burst read - minimize transaction time
 * Reads multiple bytes in a single transaction to reduce overhead
 */
HAL_StatusTypeDef I2C_BurstRead(uint8_t dev_addr, uint8_t reg_addr, 
                                 uint8_t *data, uint16_t len) {
    HAL_StatusTypeDef status;
    
    // Write register address
    status = HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1, 
                                     &reg_addr, 1, I2C_TIMEOUT);
    if (status != HAL_OK) return status;
    
    // Burst read data
    status = HAL_I2C_Master_Receive(&hi2c1, dev_addr << 1, 
                                    data, len, I2C_TIMEOUT);
    return status;
}

/**
 * Read sensor with full power management
 * - Wake up peripherals
 * - Perform I2C transaction
 * - Return to sleep mode
 */
bool ReadSensorWithPowerSave(uint8_t reg, uint8_t *data, uint8_t len) {
    bool success = false;
    
    // 1. Wake up I2C if disabled
    I2C_LowPower_Enable();
    
    // 2. Small delay for sensor wake-up (if needed)
    HAL_Delay(1);
    
    // 3. Perform burst read to minimize transaction time
    if (I2C_BurstRead(SENSOR_ADDR, reg, data, len) == HAL_OK) {
        success = true;
    }
    
    // 4. Disable I2C to save power
    I2C_LowPower_Disable();
    
    return success;
}

/**
 * Enter low-power sleep mode
 * Configures system for minimal power consumption
 */
void EnterLowPowerSleep(uint32_t sleep_ms) {
    // Disable I2C
    I2C_LowPower_Disable();
    
    // Configure wake-up timer (RTC)
    // This is platform-specific - example uses HAL delay
    // In production, use RTC alarm or LPTIM
    
    // Enter Stop Mode (deepest sleep with RAM retention)
    HAL_PWR_EnterSTOPMode(PWR_LOWPOWERREGULATOR_ON, PWR_STOPENTRY_WFI);
    
    // System wakes here
    // Reconfigure clocks after wake-up
    SystemClock_Config();
}

/**
 * Optimized periodic sensor reading pattern
 * Typical battery-powered application loop
 */
void PeriodicSensorRead_Example(void) {
    uint8_t sensor_data[6];
    uint32_t wake_interval = 60000; // Wake every 60 seconds
    
    while (1) {
        // Read sensor data with power management
        if (ReadSensorWithPowerSave(0x00, sensor_data, sizeof(sensor_data))) {
            // Process data (send via radio, log, etc.)
            ProcessSensorData(sensor_data);
        }
        
        // Enter deep sleep until next reading
        EnterLowPowerSleep(wake_interval);
    }
}

/**
 * DMA-based I2C transfer (lowest CPU power)
 * Uses DMA to transfer data without CPU intervention
 */
HAL_StatusTypeDef I2C_BurstRead_DMA(uint8_t dev_addr, uint8_t reg_addr,
                                     uint8_t *data, uint16_t len) {
    HAL_StatusTypeDef status;
    
    // Write register address
    status = HAL_I2C_Master_Transmit(&hi2c1, dev_addr << 1,
                                     &reg_addr, 1, I2C_TIMEOUT);
    if (status != HAL_OK) return status;
    
    // Burst read with DMA (CPU can sleep during transfer)
    status = HAL_I2C_Master_Receive_DMA(&hi2c1, dev_addr << 1, data, len);
    
    return status;
}

/**
 * Configure pull-up resistor power control (optional)
 * Uses a GPIO to enable/disable pull-up resistor power
 * Requires external MOSFET circuit
 */
void ConfigurePullupPowerControl(void) {
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    
    __HAL_RCC_GPIOC_CLK_ENABLE();
    
    // PC0 controls pull-up power via MOSFET
    GPIO_InitStruct.Pin = GPIO_PIN_0;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);
    
    // Initially disable pull-ups
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
}

void EnablePullups(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_SET);
    HAL_Delay(1); // Allow pull-ups to stabilize
}

void DisablePullups(void) {
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_0, GPIO_PIN_RESET);
}

// Placeholder functions
void ProcessSensorData(uint8_t *data) { /* Implementation */ }
void SystemClock_Config(void) { /* Implementation */ }
void Error_Handler(void) { while(1); }
```

### Rust Implementation (embedded-hal)

```rust
/*!
 * I2C Low Power Implementation in Rust
 * Demonstrates power-saving techniques using embedded-hal traits
 */

#![no_std]

use embedded_hal::blocking::i2c::{Write, WriteRead, Read};
use embedded_hal::blocking::delay::DelayMs;
use core::fmt;

// Error types for power management
#[derive(Debug, Clone, Copy)]
pub enum PowerError {
    I2cError,
    InvalidState,
    Timeout,
}

impl fmt::Display for PowerError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        match self {
            PowerError::I2cError => write!(f, "I2C communication error"),
            PowerError::InvalidState => write!(f, "Invalid power state"),
            PowerError::Timeout => write!(f, "Operation timeout"),
        }
    }
}

/// Power state management
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum PowerState {
    Active,
    LowPower,
    Sleep,
    DeepSleep,
}

/// Power manager configuration
pub struct PowerConfig {
    pub wake_interval_ms: u32,
    pub i2c_clock_khz: u32,
    pub use_dma: bool,
    pub pullup_control: bool,
}

impl Default for PowerConfig {
    fn default() -> Self {
        PowerConfig {
            wake_interval_ms: 60_000, // 60 seconds
            i2c_clock_khz: 100,        // 100kHz standard mode
            use_dma: false,
            pullup_control: false,
        }
    }
}

/// Low-power I2C device manager
pub struct LowPowerI2C<I2C, DELAY> {
    i2c: Option<I2C>,
    delay: DELAY,
    config: PowerConfig,
    state: PowerState,
    transaction_count: u32,
}

impl<I2C, DELAY, E> LowPowerI2C<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    /// Create a new low-power I2C manager
    pub fn new(i2c: I2C, delay: DELAY, config: PowerConfig) -> Self {
        LowPowerI2C {
            i2c: Some(i2c),
            delay,
            config,
            state: PowerState::Active,
            transaction_count: 0,
        }
    }

    /// Enable I2C peripheral (wake from sleep)
    pub fn enable(&mut self) -> Result<(), PowerError> {
        match self.state {
            PowerState::Active => Ok(()),
            _ => {
                // In a real implementation, reconfigure I2C peripheral
                self.state = PowerState::Active;
                self.delay.delay_ms(1); // Stabilization time
                Ok(())
            }
        }
    }

    /// Disable I2C peripheral for power savings
    pub fn disable(&mut self) -> Result<(), PowerError> {
        if self.state == PowerState::Active {
            // In a real implementation, deinitialize I2C and set pins to analog
            self.state = PowerState::LowPower;
            Ok(())
        } else {
            Err(PowerError::InvalidState)
        }
    }

    /// Burst read - multiple bytes in single transaction
    pub fn burst_read(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) 
        -> Result<(), PowerError> {
        
        self.enable()?;
        
        let i2c = self.i2c.as_mut().ok_or(PowerError::InvalidState)?;
        
        // Perform write-read transaction
        i2c.write_read(addr, &[reg], buffer)
            .map_err(|_| PowerError::I2cError)?;
        
        self.transaction_count += 1;
        Ok(())
    }

    /// Burst write - multiple bytes in single transaction
    pub fn burst_write(&mut self, addr: u8, data: &[u8]) -> Result<(), PowerError> {
        self.enable()?;
        
        let i2c = self.i2c.as_mut().ok_or(PowerError::InvalidState)?;
        
        i2c.write(addr, data)
            .map_err(|_| PowerError::I2cError)?;
        
        self.transaction_count += 1;
        Ok(())
    }

    /// Optimized sensor read with automatic power management
    pub fn read_sensor_low_power(&mut self, addr: u8, reg: u8, buffer: &mut [u8]) 
        -> Result<(), PowerError> {
        
        // 1. Wake up I2C
        self.enable()?;
        
        // 2. Perform burst read
        self.burst_read(addr, reg, buffer)?;
        
        // 3. Return to low power immediately
        self.disable()?;
        
        Ok(())
    }

    /// Get current power state
    pub fn power_state(&self) -> PowerState {
        self.state
    }

    /// Get transaction statistics
    pub fn transaction_count(&self) -> u32 {
        self.transaction_count
    }

    /// Enter sleep mode (releases I2C peripheral)
    pub fn enter_sleep(&mut self) -> Result<I2C, PowerError> {
        self.state = PowerState::Sleep;
        self.i2c.take().ok_or(PowerError::InvalidState)
    }

    /// Wake from sleep mode (reclaims I2C peripheral)
    pub fn wake_from_sleep(&mut self, i2c: I2C) {
        self.i2c = Some(i2c);
        self.state = PowerState::Active;
    }
}

/// Example sensor driver with power management
pub struct LowPowerSensor<I2C, DELAY> {
    i2c_mgr: LowPowerI2C<I2C, DELAY>,
    device_addr: u8,
}

impl<I2C, DELAY, E> LowPowerSensor<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    pub fn new(i2c: I2C, delay: DELAY, device_addr: u8) -> Self {
        let config = PowerConfig::default();
        let i2c_mgr = LowPowerI2C::new(i2c, delay, config);
        
        LowPowerSensor {
            i2c_mgr,
            device_addr,
        }
    }

    /// Read temperature (example)
    pub fn read_temperature(&mut self) -> Result<f32, PowerError> {
        let mut buffer = [0u8; 2];
        
        // Read 2 bytes starting from register 0x00
        self.i2c_mgr.read_sensor_low_power(
            self.device_addr,
            0x00,
            &mut buffer
        )?;
        
        // Convert to temperature (example conversion)
        let raw = u16::from_be_bytes(buffer);
        let temp = (raw as f32) * 0.0625;
        
        Ok(temp)
    }

    /// Read multiple sensor values in batch
    pub fn read_all_sensors(&mut self) -> Result<SensorData, PowerError> {
        let mut buffer = [0u8; 12];
        
        // Single burst read of all sensor registers
        self.i2c_mgr.read_sensor_low_power(
            self.device_addr,
            0x00,
            &mut buffer
        )?;
        
        Ok(SensorData::from_bytes(&buffer))
    }

    /// Configure sensor for low-power operation
    pub fn configure_low_power_mode(&mut self) -> Result<(), PowerError> {
        // Example: Set sensor to low-power mode with reduced sampling rate
        let config_data = [
            0x01,  // Config register
            0x20,  // Low power mode, 1Hz sample rate
        ];
        
        self.i2c_mgr.burst_write(self.device_addr, &config_data)?;
        
        Ok(())
    }

    /// Disable I2C between readings
    pub fn sleep(&mut self) -> Result<(), PowerError> {
        self.i2c_mgr.disable()
    }

    /// Wake up for next reading
    pub fn wake(&mut self) -> Result<(), PowerError> {
        self.i2c_mgr.enable()
    }
}

/// Sensor data structure
#[derive(Debug, Clone, Copy)]
pub struct SensorData {
    pub temperature: f32,
    pub humidity: f32,
    pub pressure: f32,
}

impl SensorData {
    fn from_bytes(bytes: &[u8]) -> Self {
        // Example parsing
        let temp_raw = u16::from_be_bytes([bytes[0], bytes[1]]);
        let hum_raw = u16::from_be_bytes([bytes[4], bytes[5]]);
        let pres_raw = u32::from_be_bytes([0, bytes[8], bytes[9], bytes[10]]);
        
        SensorData {
            temperature: (temp_raw as f32) * 0.0625,
            humidity: (hum_raw as f32) / 655.36,
            pressure: (pres_raw as f32) / 100.0,
        }
    }
}

/// Power-optimized periodic reading pattern
pub struct PeriodicReader<I2C, DELAY> {
    sensor: LowPowerSensor<I2C, DELAY>,
    interval_ms: u32,
    reading_count: u32,
}

impl<I2C, DELAY, E> PeriodicReader<I2C, DELAY>
where
    I2C: Write<Error = E> + WriteRead<Error = E> + Read<Error = E>,
    DELAY: DelayMs<u32>,
{
    pub fn new(sensor: LowPowerSensor<I2C, DELAY>, interval_ms: u32) -> Self {
        PeriodicReader {
            sensor,
            interval_ms,
            reading_count: 0,
        }
    }

    /// Perform one reading cycle
    pub fn read_cycle(&mut self) -> Result<SensorData, PowerError> {
        // Wake sensor and I2C
        self.sensor.wake()?;
        
        // Read all sensor data in one burst
        let data = self.sensor.read_all_sensors()?;
        
        // Return to sleep
        self.sensor.sleep()?;
        
        self.reading_count += 1;
        
        Ok(data)
    }

    /// Main loop for battery-powered operation
    pub fn run_loop<F>(&mut self, mut process_fn: F) -> !
    where
        F: FnMut(SensorData),
    {
        loop {
            match self.read_cycle() {
                Ok(data) => {
                    process_fn(data);
                }
                Err(e) => {
                    // Handle error (could log, retry, etc.)
                }
            }
            
            // Sleep until next reading
            // In real implementation, use RTC alarm or low-power timer
            // This is a placeholder
            cortex_m::asm::wfi(); // Wait for interrupt
        }
    }

    pub fn reading_count(&self) -> u32 {
        self.reading_count
    }
}

/// Calculate estimated power consumption
pub struct PowerEstimator {
    i2c_freq_khz: u32,
    pullup_resistance_kohm: u32,
    supply_voltage_mv: u32,
}

impl PowerEstimator {
    pub fn new(i2c_freq_khz: u32, pullup_resistance_kohm: u32, 
               supply_voltage_mv: u32) -> Self {
        PowerEstimator {
            i2c_freq_khz,
            pullup_resistance_kohm,
            supply_voltage_mv,
        }
    }

    /// Calculate pull-up resistor power (when line is low)
    pub fn pullup_power_uw(&self) -> u32 {
        // P = V² / R (in microwatts)
        let v_sq = self.supply_voltage_mv * self.supply_voltage_mv;
        v_sq / self.pullup_resistance_kohm
    }

    /// Estimate average power during I2C transaction
    pub fn transaction_power_estimate_uw(&self, bytes: u32, duty_cycle: f32) -> u32 {
        // Simplified estimation
        let pullup_power = self.pullup_power_uw();
        let active_power = pullup_power * 2; // Both SDA and SCL
        
        (active_power as f32 * duty_cycle) as u32
    }
}

// Example usage function
#[cfg(test)]
mod example {
    use super::*;

    pub fn usage_example() {
        // This demonstrates the API, actual types would come from HAL
        
        // 1. Create power estimator
        let estimator = PowerEstimator::new(100, 10, 3300);
        let pullup_power = estimator.pullup_power_uw();
        // pullup_power = 1089 µW per resistor
        
        // 2. Configure sensor for low power
        // let mut sensor = LowPowerSensor::new(i2c, delay, 0x48);
        // sensor.configure_low_power_mode().unwrap();
        
        // 3. Set up periodic reader
        // let mut reader = PeriodicReader::new(sensor, 60_000);
        
        // 4. Run main loop
        // reader.run_loop(|data| {
        //     // Process sensor data
        //     // Send via radio, log to flash, etc.
        // });
    }
}
```

### Arduino/ESP32 Example (Practical Implementation)

```c
/**
 * I2C Low Power Example for ESP32/Arduino
 * Demonstrates practical power-saving techniques for battery operation
 */

#include <Wire.h>

// ESP32 specific includes for deep sleep
#ifdef ESP32
  #include "esp_sleep.h"
  #include "driver/rtc_io.h"
#endif

// Sensor configuration
#define SENSOR_ADDR 0x48
#define TEMP_REG 0x00

// I2C pins (ESP32)
#define SDA_PIN 21
#define SCL_PIN 22

// Power control pin for pull-up resistors (optional)
#define PULLUP_POWER_PIN 15

// Configuration
#define SLEEP_INTERVAL_SEC 60      // Sleep 60 seconds between readings
#define I2C_CLOCK_SPEED 100000     // 100kHz for lower power
#define USE_FAST_I2C false         // Use standard mode

// Power statistics
struct PowerStats {
  uint32_t wake_count;
  uint32_t successful_reads;
  uint32_t failed_reads;
  float last_battery_voltage;
};

PowerStats stats = {0};

/**
 * Initialize I2C with low-power settings
 */
void initI2C() {
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(I2C_CLOCK_SPEED);
  
  Serial.println("I2C initialized in low-power mode");
  Serial.printf("Clock speed: %d Hz\n", I2C_CLOCK_SPEED);
}

/**
 * Deinitialize I2C to save power
 * Sets pins to input mode with pull-ups disabled
 */
void deinitI2C() {
  Wire.end();
  
  // Configure pins for lowest power consumption
  pinMode(SDA_PIN, INPUT);
  pinMode(SCL_PIN, INPUT);
  
  #ifdef ESP32
    // On ESP32, can set to RTC GPIO mode for even lower power
    rtc_gpio_init((gpio_num_t)SDA_PIN);
    rtc_gpio_init((gpio_num_t)SCL_PIN);
    rtc_gpio_set_direction((gpio_num_t)SDA_PIN, RTC_GPIO_MODE_DISABLED);
    rtc_gpio_set_direction((gpio_num_t)SCL_PIN, RTC_GPIO_MODE_DISABLED);
  #endif
}

/**
 * Control external pull-up resistor power (optional)
 * Requires MOSFET circuit to switch pull-up power
 */
void enablePullups() {
  digitalWrite(PULLUP_POWER_PIN, HIGH);
  delay(1); // Stabilization time
}

void disablePullups() {
  digitalWrite(PULLUP_POWER_PIN, LOW);
}

/**
 * Burst read function - reads multiple bytes in one transaction
 */
bool i2cBurstRead(uint8_t devAddr, uint8_t regAddr, uint8_t* data, uint8_t len) {
  Wire.beginTransmission(devAddr);
  Wire.write(regAddr);
  
  if (Wire.endTransmission(false) != 0) { // Repeated start
    return false;
  }
  
  Wire.requestFrom(devAddr, len);
  
  uint8_t i = 0;
  while (Wire.available() && i < len) {
    data[i++] = Wire.read();
  }
  
  return (i == len);
}

/**
 * Read temperature sensor with full power management
 */
bool readTemperatureOptimized(float* temperature) {
  uint8_t data[2];
  bool success = false;
  
  // 1. Enable pull-ups if controlled externally
  #ifdef PULLUP_POWER_PIN
    enablePullups();
  #endif
  
  // 2. Initialize I2C
  initI2C();
  
  // 3. Read sensor data (2 bytes burst read)
  if (i2cBurstRead(SENSOR_ADDR, TEMP_REG, data, 2)) {
    // 4. Convert to temperature
    int16_t raw = (data[0] << 8) | data[1];
    *temperature = raw * 0.0625; // Example conversion
    success = true;
    stats.successful_reads++;
  } else {
    stats.failed_reads++;
  }
  
  // 5. Deinitialize I2C
  deinitI2C();
  
  // 6. Disable pull-ups if controlled externally
  #ifdef PULLUP_POWER_PIN
    disablePullups();
  #endif
  
  return success;
}

/**
 * Configure sensor for low-power operation
 */
bool configureSensorLowPower() {
  initI2C();
  
  Wire.beginTransmission(SENSOR_ADDR);
  Wire.write(0x01); // Config register
  Wire.write(0x60); // Low power mode: shutdown between conversions
  Wire.write(0xA0); // Config: 8 samples average, low power
  
  bool success = (Wire.endTransmission() == 0);
  
  deinitI2C();
  return success;
}

/**
 * Read battery voltage (ESP32 ADC)
 */
float readBatteryVoltage() {
  #ifdef ESP32
    // Assuming voltage divider on ADC pin
    int raw = analogRead(36); // GPIO36 (VP)
    // Convert to voltage (depends on divider ratio)
    return (raw / 4095.0) * 3.3 * 2.0; // Example for 1:1 divider
  #else
    return 3.3; // Dummy value for non-ESP32
  #endif
}

/**
 * Enter deep sleep mode
 */
void enterDeepSleep(uint32_t seconds) {
  Serial.printf("Entering deep sleep for %d seconds\n", seconds);
  Serial.flush(); // Ensure all serial data is sent
  
  #ifdef ESP32
    // Configure wake-up
    esp_sleep_enable_timer_wakeup(seconds * 1000000ULL);
    
    // Enter deep sleep
    esp_deep_sleep_start();
  #else
    // For non-ESP32, just use delay (not true low power)
    delay(seconds * 1000);
  #endif
}

/**
 * Calculate power consumption estimate
 */
void printPowerEstimate() {
  // Pull-up resistor power (assuming 10kΩ, 3.3V)
  float pullup_power_mw = (3.3 * 3.3) / 10.0; // ~1.09mW per resistor
  float total_pullup_mw = pullup_power_mw * 2; // SDA and SCL
  
  // Active time per reading (~10ms for I2C transaction)
  float active_time_sec = 0.010;
  
  // Sleep current (ESP32 deep sleep: ~10µA)
  float sleep_current_ua = 10.0;
  
  // Active current (ESP32 + sensor: ~50mA)
  float active_current_ma = 50.0;
  
  // Average current calculation
  float duty_cycle = active_time_sec / SLEEP_INTERVAL_SEC;
  float avg_current_ua = (active_current_ma * 1000 * duty_cycle) + sleep_current_ua;
  
  Serial.println("\n=== Power Estimate ===");
  Serial.printf("Pull-up power: %.2f mW (when active)\n", total_pullup_mw);
  Serial.printf("Active current: %.2f mA\n", active_current_ma);
  Serial.printf("Sleep current: %.2f µA\n", sleep_current_ua);
  Serial.printf("Duty cycle: %.4f%%\n", duty_cycle * 100);
  Serial.printf("Average current: %.2f µA\n", avg_current_ua);
  Serial.printf("\nBattery life (2000mAh):\n");
  Serial.printf("  Estimated: %.1f days\n", (2000.0 * 1000.0) / (avg_current_ua * 24.0));
}

/**
 * Optimized reading cycle
 */
void performReadingCycle() {
  float temperature;
  
  Serial.println("\n--- Wake Up ---");
  stats.wake_count++;
  
  // Read battery voltage
  stats.last_battery_voltage = readBatteryVoltage();
  Serial.printf("Battery: %.2fV\n", stats.last_battery_voltage);
  
  // Read sensor
  if (readTemperatureOptimized(&temperature)) {
    Serial.printf("Temperature: %.2f°C\n", temperature);
    
    // Process data (e.g., send via LoRa, store in flash, etc.)
    // For demonstration, just print
  } else {
    Serial.println("Failed to read sensor!");
  }
  
  // Print statistics
  Serial.printf("Stats: Wake=%d, Success=%d, Failed=%d\n",
                stats.wake_count, stats.successful_reads, stats.failed_reads);
}

void setup() {
  Serial.begin(115200);
  delay(100);
  
  Serial.println("\n=== I2C Low Power Demo ===");
  
  #ifdef ESP32
    // Print wake-up reason
    esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
    if (wakeup_reason == ESP_SLEEP_WAKEUP_TIMER) {
      Serial.println("Wake from timer");
    } else {
      Serial.println("Initial boot");
    }
  #endif
  
  // Configure pull-up power control if used
  #ifdef PULLUP_POWER_PIN
    pinMode(PULLUP_POWER_PIN, OUTPUT);
    disablePullups();
  #endif
  
  // Configure sensor on first boot
  if (stats.wake_count == 0) {
    Serial.println("Configuring sensor...");
    if (configureSensorLowPower()) {
      Serial.println("Sensor configured for low power");
    } else {
      Serial.println("Sensor configuration failed!");
    }
    
    printPowerEstimate();
  }
  
  // Perform reading
  performReadingCycle();
  
  // Enter sleep
  enterDeepSleep(SLEEP_INTERVAL_SEC);
}

void loop() {
  // Never reached in deep sleep mode
  // For non-ESP32 or light sleep:
  delay(SLEEP_INTERVAL_SEC * 1000);
  performReadingCycle();
}

/**
 * Alternative: Light sleep (maintains WiFi connection)
 */
#ifdef ESP32
void enterLightSleep(uint32_t milliseconds) {
  // Light sleep maintains RAM and WiFi calibration data
  esp_sleep_enable_timer_wakeup(milliseconds * 1000ULL);
  esp_light_sleep_start();
}
#endif

/**
 * Advanced: Adaptive sleep based on battery level
 */
uint32_t getAdaptiveSleepInterval() {
  float battery = readBatteryVoltage();
  
  if (battery > 3.7) {
    return 60;   // 60 seconds - normal operation
  } else if (battery > 3.5) {
    return 120;  // 2 minutes - extend battery
  } else if (battery > 3.3) {
    return 300;  // 5 minutes - critical battery
  } else {
    return 600;  // 10 minutes - near empty
  }
}
```

## Power Optimization Best Practices

### Hardware Design
1. **Choose appropriate pull-up resistors**: 10kΩ-47kΩ for battery-powered devices
2. **Add pull-up power control**: Use MOSFET to switch pull-up power
3. **Use low-power devices**: Select sensors with shutdown/sleep modes
4. **Short trace lengths**: Reduce bus capacitance

### Software Optimization
1. **Batch transactions**: Read multiple registers in single burst
2. **Minimize wake time**: Initialize, read, sleep quickly
3. **Use interrupts**: Avoid polling sensors
4. **DMA transfers**: Free CPU during I2C operations
5. **Adaptive intervals**: Adjust sleep time based on battery level

### Power Consumption Examples

**Typical I2C Power Budget:**
- Pull-up resistors (10kΩ @ 3.3V): ~1.1mW per resistor when active
- Active MCU: 20-50mA depending on chip
- Deep sleep MCU: 5-50µA
- Sensor active: 100µA-10mA
- Sensor sleep: 0.1-10µA

**Battery Life Calculation:**
```
Average Current = (Active_Current × Active_Time + Sleep_Current × Sleep_Time) / Total_Time

Example:
- Active: 50mA for 10ms every 60s
- Sleep: 10µA
- Average = (50mA × 0.01s + 0.01mA × 59.99s) / 60s ≈ 18.3µA

Battery Life (2000mAh) = 2000mAh / 0.0183mA ≈ 109,000 hours (12.5 years)
```

## Key Takeaways

The examples demonstrate crucial power-saving techniques: using burst reads to minimize transaction time, disabling I2C peripherals between readings, configuring GPIO pins for minimal leakage, implementing deep sleep modes, and optionally controlling pull-up resistor power. The combination of these techniques can reduce average power consumption from tens of milliamps to just a few microamps, enabling battery life measured in months or years rather than days.