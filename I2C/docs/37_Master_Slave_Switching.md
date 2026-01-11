# I2C Master-Slave Switching

## Overview

Master-Slave Switching refers to I2C devices that can dynamically change their role between master (initiator of communication) and slave (responder) on the same bus. This capability is useful in peer-to-peer communication scenarios, multi-master systems, or devices that need to both initiate transactions and respond to requests from other masters.

## Key Concepts

**Traditional I2C Roles:**
- **Master**: Generates clock (SCL), initiates communication, controls bus
- **Slave**: Responds to master, driven by master's clock

**Multi-Role Devices:**
- Can function as master to communicate with sensors/peripherals
- Can function as slave to accept commands from other controllers
- Must handle bus arbitration and role transitions properly

## Use Cases

1. **Bridging/Gateway Devices**: A device that acts as slave to a main controller but master to downstream sensors
2. **Peer-to-Peer Communication**: Two microcontrollers that need bidirectional communication
3. **Smart Peripherals**: Devices that can both report data (as slave) and control other peripherals (as master)
4. **Distributed Systems**: Multiple processors sharing control of a sensor network

## Implementation Considerations

### Hardware Requirements
- Separate master and slave hardware peripherals (on some MCUs)
- OR time-multiplexed single I2C peripheral with mode switching
- Proper pull-up resistors (typically 4.7kΩ for standard mode)
- Bus arbitration support

### Software Challenges
- **Bus Arbitration**: Handling collisions when multiple masters try to communicate
- **Address Conflicts**: Master mode doesn't have an address; must ensure no conflicts
- **State Management**: Tracking current role and transitioning cleanly
- **Priority Management**: Deciding when to act as master vs remaining as slave

## C/C++ Code Examples

### Example 1: Basic Master-Slave Switching (STM32 HAL)

```c
#include "stm32f4xx_hal.h"

I2C_HandleTypeDef hi2c1;
#define DEVICE_SLAVE_ADDR 0x42
#define REMOTE_DEVICE_ADDR 0x30

typedef enum {
    MODE_SLAVE,
    MODE_MASTER
} I2C_Mode_t;

volatile I2C_Mode_t current_mode = MODE_SLAVE;
uint8_t slave_rx_buffer[32];
uint8_t slave_tx_buffer[32];

// Initialize I2C in slave mode
void I2C_Init_Slave(void) {
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 100000;
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = DEVICE_SLAVE_ADDR << 1;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    
    if (HAL_I2C_Init(&hi2c1) != HAL_OK) {
        Error_Handler();
    }
    
    // Start listening as slave
    HAL_I2C_EnableListen_IT(&hi2c1);
    current_mode = MODE_SLAVE;
}

// Switch to master mode and perform transaction
HAL_StatusTypeDef I2C_Switch_To_Master_And_Send(uint8_t *data, uint16_t size) {
    HAL_StatusTypeDef status;
    
    // Wait for bus to be free
    while (HAL_I2C_GetState(&hi2c1) != HAL_I2C_STATE_READY) {
        HAL_Delay(1);
    }
    
    // Disable slave listening
    if (current_mode == MODE_SLAVE) {
        HAL_I2C_DisableListen_IT(&hi2c1);
    }
    
    current_mode = MODE_MASTER;
    
    // Perform master transaction
    status = HAL_I2C_Master_Transmit(&hi2c1, REMOTE_DEVICE_ADDR << 1, 
                                      data, size, 1000);
    
    // Return to slave mode
    current_mode = MODE_SLAVE;
    HAL_I2C_EnableListen_IT(&hi2c1);
    
    return status;
}

// Slave receive callback
void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        // Process received data
        // Re-enable listening
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, slave_rx_buffer, 
                                       sizeof(slave_rx_buffer), 
                                       I2C_FIRST_AND_LAST_FRAME);
    }
}

// Slave transmit callback
void HAL_I2C_SlaveTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        // Prepare next transmission data if needed
    }
}

// Address match callback - master is addressing us as slave
void HAL_I2C_AddrCallback(I2C_HandleTypeDef *hi2c, 
                          uint8_t TransferDirection, 
                          uint16_t AddrMatchCode) {
    if (TransferDirection == I2C_DIRECTION_TRANSMIT) {
        // Master wants to send data to us
        HAL_I2C_Slave_Seq_Receive_IT(hi2c, slave_rx_buffer, 
                                       sizeof(slave_rx_buffer), 
                                       I2C_FIRST_AND_LAST_FRAME);
    } else {
        // Master wants to read data from us
        HAL_I2C_Slave_Seq_Transmit_IT(hi2c, slave_tx_buffer, 
                                        sizeof(slave_tx_buffer), 
                                        I2C_FIRST_AND_LAST_FRAME);
    }
}
```

### Example 2: ESP32 Master-Slave Switching

```cpp
#include <driver/i2c.h>

#define I2C_MASTER_NUM I2C_NUM_0
#define I2C_SLAVE_NUM I2C_NUM_0  // Same peripheral, different modes
#define I2C_MASTER_FREQ_HZ 100000
#define SLAVE_ADDR 0x42
#define REMOTE_DEVICE_ADDR 0x30

class I2C_MultiRole {
private:
    bool is_master_mode;
    SemaphoreHandle_t mode_mutex;
    
public:
    I2C_MultiRole() : is_master_mode(false) {
        mode_mutex = xSemaphoreCreateMutex();
    }
    
    // Initialize as slave
    esp_err_t init_slave() {
        i2c_config_t conf_slave = {
            .mode = I2C_MODE_SLAVE,
            .sda_io_num = GPIO_NUM_21,
            .scl_io_num = GPIO_NUM_22,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .slave = {
                .addr_10bit_en = 0,
                .slave_addr = SLAVE_ADDR,
            }
        };
        
        esp_err_t err = i2c_param_config(I2C_SLAVE_NUM, &conf_slave);
        if (err != ESP_OK) return err;
        
        return i2c_driver_install(I2C_SLAVE_NUM, conf_slave.mode, 
                                   256, 256, 0);
    }
    
    // Switch to master mode and perform transaction
    esp_err_t master_write(uint8_t *data, size_t len) {
        xSemaphoreTake(mode_mutex, portMAX_DELAY);
        
        // Delete slave driver
        i2c_driver_delete(I2C_SLAVE_NUM);
        
        // Configure as master
        i2c_config_t conf_master = {
            .mode = I2C_MODE_MASTER,
            .sda_io_num = GPIO_NUM_21,
            .scl_io_num = GPIO_NUM_22,
            .sda_pullup_en = GPIO_PULLUP_ENABLE,
            .scl_pullup_en = GPIO_PULLUP_ENABLE,
            .master = {
                .clk_speed = I2C_MASTER_FREQ_HZ,
            }
        };
        
        esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf_master);
        if (err != ESP_OK) {
            xSemaphoreGive(mode_mutex);
            return err;
        }
        
        err = i2c_driver_install(I2C_MASTER_NUM, conf_master.mode, 0, 0, 0);
        if (err != ESP_OK) {
            xSemaphoreGive(mode_mutex);
            return err;
        }
        
        is_master_mode = true;
        
        // Perform master write
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (REMOTE_DEVICE_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write(cmd, data, len, true);
        i2c_master_stop(cmd);
        err = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 1000 / portTICK_PERIOD_MS);
        i2c_cmd_link_delete(cmd);
        
        // Switch back to slave
        i2c_driver_delete(I2C_MASTER_NUM);
        init_slave();
        is_master_mode = false;
        
        xSemaphoreGive(mode_mutex);
        return err;
    }
    
    // Read data as slave
    int slave_read(uint8_t *data, size_t max_len, TickType_t timeout) {
        if (is_master_mode) return -1;
        
        return i2c_slave_read_buffer(I2C_SLAVE_NUM, data, max_len, timeout);
    }
    
    // Write data as slave (response to master read)
    int slave_write(uint8_t *data, size_t len, TickType_t timeout) {
        if (is_master_mode) return -1;
        
        return i2c_slave_write_buffer(I2C_SLAVE_NUM, data, len, timeout);
    }
};

// Usage example
void app_main() {
    I2C_MultiRole i2c;
    
    // Start as slave
    i2c.init_slave();
    
    // Slave task
    xTaskCreate([](void *param) {
        I2C_MultiRole *i2c = (I2C_MultiRole*)param;
        uint8_t buffer[64];
        
        while (1) {
            int len = i2c->slave_read(buffer, sizeof(buffer), 
                                       pdMS_TO_TICKS(100));
            if (len > 0) {
                // Process received data
                printf("Slave received %d bytes\n", len);
            }
        }
    }, "slave_task", 4096, &i2c, 5, NULL);
    
    // Periodically send data as master
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        
        uint8_t data[] = {0x01, 0x02, 0x03};
        esp_err_t err = i2c.master_write(data, sizeof(data));
        
        if (err == ESP_OK) {
            printf("Master write successful\n");
        } else {
            printf("Master write failed: %d\n", err);
        }
    }
}
```

## Rust Code Examples

### Example 1: Rust Embedded HAL Pattern

```rust
use embedded_hal::blocking::i2c::{Write, Read, WriteRead};
use core::cell::RefCell;

#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CRole {
    Master,
    Slave,
}

pub trait I2CMasterSlave {
    type Error;
    
    /// Switch to master mode
    fn switch_to_master(&mut self) -> Result<(), Self::Error>;
    
    /// Switch to slave mode with given address
    fn switch_to_slave(&mut self, address: u8) -> Result<(), Self::Error>;
    
    /// Get current role
    fn current_role(&self) -> I2CRole;
    
    /// Master write operation
    fn master_write(&mut self, addr: u8, bytes: &[u8]) -> Result<(), Self::Error>;
    
    /// Master read operation
    fn master_read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), Self::Error>;
    
    /// Slave receive (blocking or with timeout)
    fn slave_receive(&mut self, buffer: &mut [u8]) -> Result<usize, Self::Error>;
    
    /// Slave transmit (response to master read)
    fn slave_transmit(&mut self, bytes: &[u8]) -> Result<(), Self::Error>;
}

// Example implementation for a hypothetical MCU
pub struct MultiRoleI2C<I2C> {
    peripheral: RefCell<I2C>,
    current_role: RefCell<I2CRole>,
    slave_address: RefCell<u8>,
}

impl<I2C> MultiRoleI2C<I2C> {
    pub fn new(peripheral: I2C, initial_slave_addr: u8) -> Self {
        Self {
            peripheral: RefCell::new(peripheral),
            current_role: RefCell::new(I2CRole::Slave),
            slave_address: RefCell::new(initial_slave_addr),
        }
    }
}

// Concrete implementation example (pseudo-code for STM32-like HAL)
#[cfg(feature = "stm32")]
use stm32f4xx_hal::i2c::I2c;
use stm32f4xx_hal::pac::I2C1;

impl I2CMasterSlave for MultiRoleI2C<I2c<I2C1>> {
    type Error = stm32f4xx_hal::i2c::Error;
    
    fn switch_to_master(&mut self) -> Result<(), Self::Error> {
        let mut role = self.current_role.borrow_mut();
        
        if *role == I2CRole::Master {
            return Ok(());
        }
        
        // Disable slave mode
        let mut periph = self.peripheral.borrow_mut();
        // periph.disable_slave_mode(); // Hypothetical method
        
        // Enable master mode
        // periph.enable_master_mode(); // Hypothetical method
        
        *role = I2CRole::Master;
        Ok(())
    }
    
    fn switch_to_slave(&mut self, address: u8) -> Result<(), Self::Error> {
        let mut role = self.current_role.borrow_mut();
        
        if *role == I2CRole::Slave {
            return Ok(());
        }
        
        // Wait for any master transactions to complete
        let mut periph = self.peripheral.borrow_mut();
        // periph.wait_idle(); // Hypothetical method
        
        // Configure slave address
        *self.slave_address.borrow_mut() = address;
        
        // Switch to slave mode
        // periph.set_slave_address(address);
        // periph.enable_slave_mode();
        
        *role = I2CRole::Slave;
        Ok(())
    }
    
    fn current_role(&self) -> I2CRole {
        *self.current_role.borrow()
    }
    
    fn master_write(&mut self, addr: u8, bytes: &[u8]) -> Result<(), Self::Error> {
        self.switch_to_master()?;
        let mut periph = self.peripheral.borrow_mut();
        periph.write(addr, bytes)
    }
    
    fn master_read(&mut self, addr: u8, buffer: &mut [u8]) -> Result<(), Self::Error> {
        self.switch_to_master()?;
        let mut periph = self.peripheral.borrow_mut();
        periph.read(addr, buffer)
    }
    
    fn slave_receive(&mut self, buffer: &mut [u8]) -> Result<usize, Self::Error> {
        if self.current_role() != I2CRole::Slave {
            self.switch_to_slave(*self.slave_address.borrow())?;
        }
        
        // Blocking receive implementation
        let periph = self.peripheral.borrow_mut();
        // periph.slave_receive_blocking(buffer)
        Ok(0) // Placeholder
    }
    
    fn slave_transmit(&mut self, bytes: &[u8]) -> Result<(), Self::Error> {
        if self.current_role() != I2CRole::Slave {
            return Err(stm32f4xx_hal::i2c::Error::Overrun); // Wrong mode error
        }
        
        let periph = self.peripheral.borrow_mut();
        // periph.slave_transmit_blocking(bytes)
        Ok(()) // Placeholder
    }
}
```

### Example 2: Async Rust with Embassy

```rust
use embassy_stm32::i2c::{I2c, Config as I2cConfig};
use embassy_stm32::mode::Async;
use embassy_time::{Duration, Timer};
use embassy_sync::blocking_mutex::raw::ThreadModeRawMutex;
use embassy_sync::mutex::Mutex;

pub struct AsyncMultiRoleI2C<'d> {
    master_i2c: Mutex<ThreadModeRawMutex, Option<I2c<'d, Async>>>,
    slave_address: u8,
    current_mode: Mutex<ThreadModeRawMutex, Mode>,
}

#[derive(Clone, Copy, PartialEq)]
enum Mode {
    Master,
    Slave,
}

impl<'d> AsyncMultiRoleI2C<'d> {
    pub fn new(slave_addr: u8) -> Self {
        Self {
            master_i2c: Mutex::new(None),
            slave_address: slave_addr,
            current_mode: Mutex::new(Mode::Slave),
        }
    }
    
    /// Initialize in slave mode
    pub async fn init_slave(&self) {
        // Set up slave listening
        let mut mode = self.current_mode.lock().await;
        *mode = Mode::Slave;
        
        // Start slave reception task
        // This would use interrupt-driven slave mode
    }
    
    /// Perform a master transaction, then return to slave mode
    pub async fn master_transaction<F, R>(&self, f: F) -> R
    where
        F: FnOnce(&mut I2c<'d, Async>) -> R,
    {
        // Lock the mode
        let mut mode = self.current_mode.lock().await;
        
        // If we were in slave mode, disable it
        if *mode == Mode::Slave {
            // Disable slave interrupts/listening
        }
        
        // Get or create master I2C instance
        let mut i2c_lock = self.master_i2c.lock().await;
        
        // Perform the transaction
        let result = if let Some(ref mut i2c) = *i2c_lock {
            f(i2c)
        } else {
            panic!("Master I2C not initialized");
        };
        
        // Return to slave mode
        drop(i2c_lock);
        *mode = Mode::Slave;
        
        result
    }
    
    /// Write to a device as master
    pub async fn write_as_master(&self, addr: u8, data: &[u8]) -> Result<(), embassy_stm32::i2c::Error> {
        self.master_transaction(|i2c| {
            // In real async code, this would be:
            // i2c.write(addr, data).await
            Ok(())
        }).await
    }
    
    /// Read from a device as master
    pub async fn read_as_master(&self, addr: u8, buffer: &mut [u8]) -> Result<(), embassy_stm32::i2c::Error> {
        self.master_transaction(|i2c| {
            // i2c.read(addr, buffer).await
            Ok(())
        }).await
    }
}

// Example usage in an Embassy application
#[embassy_executor::task]
async fn i2c_slave_handler(device: &'static AsyncMultiRoleI2C<'static>) {
    let mut buffer = [0u8; 64];
    
    loop {
        // Wait for data as slave (would use interrupts in real implementation)
        Timer::after(Duration::from_millis(100)).await;
        
        // Process received slave data
        // In reality, this would be interrupt-driven
    }
}

#[embassy_executor::task]
async fn i2c_master_sender(device: &'static AsyncMultiRoleI2C<'static>) {
    loop {
        Timer::after(Duration::from_secs(5)).await;
        
        // Periodically send data as master
        let data = [0x01, 0x02, 0x03];
        match device.write_as_master(0x30, &data).await {
            Ok(_) => {
                // Success
            }
            Err(e) => {
                // Handle error
            }
        }
    }
}

#[embassy_executor::main]
async fn main(spawner: embassy_executor::Spawner) {
    // Initialize device
    static DEVICE: AsyncMultiRoleI2C<'static> = AsyncMultiRoleI2C::new(0x42);
    
    DEVICE.init_slave().await;
    
    // Spawn tasks
    spawner.spawn(i2c_slave_handler(&DEVICE)).unwrap();
    spawner.spawn(i2c_master_sender(&DEVICE)).unwrap();
}
```

## Best Practices

1. **Minimize Mode Switching**: Mode transitions take time and can disrupt communication; batch operations when possible

2. **Bus Arbitration**: Always check bus state before switching to master mode to avoid collisions

3. **Priority Management**: Establish clear priorities - typically slave mode should have priority to respond to external masters

4. **Timeout Handling**: Always use timeouts in master mode to prevent deadlocks

5. **State Synchronization**: Use proper locking mechanisms (mutexes, critical sections) to manage role transitions

6. **Separate Hardware**: If available, use separate I2C peripherals for master and slave roles to avoid complexity

7. **Testing**: Thoroughly test role transitions under various conditions, especially with multiple masters on the bus

8. **Clock Stretching**: Ensure slave mode supports clock stretching if transitions might take time

## Common Pitfalls

- **Address Conflicts**: Ensure your device's slave address doesn't conflict with devices you communicate with as master
- **Incomplete Transitions**: Always complete current transaction before switching modes
- **Lost Data**: Buffer slave data appropriately during master operations
- **Bus Lockup**: Improper switching can leave the bus in an inconsistent state
- **Race Conditions**: Multiple masters attempting to control the bus simultaneously

Master-Slave switching adds significant complexity but enables flexible, distributed I2C architectures where devices can have peer-to-peer relationships rather than strict hierarchies.