# DMA Integration for I2C Communications

## Overview

Direct Memory Access (DMA) integration with I2C allows data transfers to occur between I2C peripherals and memory without continuous CPU intervention. This significantly reduces CPU overhead, especially for large data transfers, freeing the processor to handle other tasks while the DMA controller manages the I2C transaction.

## Why Use DMA with I2C?

**Benefits:**
- **Reduced CPU Load**: CPU can perform other operations during data transfer
- **Increased Throughput**: More efficient for bulk transfers (sensor data arrays, display buffers)
- **Lower Power Consumption**: CPU can enter low-power states during transfers
- **Deterministic Timing**: Hardware-controlled transfers provide consistent timing
- **Reduced Interrupt Overhead**: Single interrupt at transfer completion vs. per-byte interrupts

**Typical Use Cases:**
- Reading large sensor data arrays
- Transferring display framebuffers to OLED/LCD controllers
- Bulk EEPROM/Flash memory operations
- High-speed continuous sensor sampling

## How DMA Works with I2C

The DMA controller acts as a bus master that can read from or write to memory independently. When configured for I2C:

1. **Configuration Phase**: CPU sets up DMA channel with source/destination addresses, transfer size, and I2C peripheral association
2. **Transfer Phase**: DMA controller moves data between memory and I2C data register as I2C hardware requests it
3. **Completion Phase**: DMA generates interrupt when transfer completes

## C/C++ Implementation Examples

### Example 1: STM32 HAL - DMA I2C Read

```c
#include "stm32f4xx_hal.h"

// Global handles
I2C_HandleTypeDef hi2c1;
DMA_HandleTypeDef hdma_i2c1_rx;
DMA_HandleTypeDef hdma_i2c1_tx;

// Buffer for received data
uint8_t rx_buffer[256];
volatile uint8_t transfer_complete = 0;

// Initialize I2C with DMA
void I2C_DMA_Init(void) {
    // Enable clocks
    __HAL_RCC_I2C1_CLK_ENABLE();
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    
    // Configure I2C pins (PB8=SCL, PB9=SDA)
    GPIO_InitTypeDef GPIO_InitStruct = {0};
    GPIO_InitStruct.Pin = GPIO_PIN_8 | GPIO_PIN_9;
    GPIO_InitStruct.Mode = GPIO_MODE_AF_OD;
    GPIO_InitStruct.Pull = GPIO_PULLUP;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    GPIO_InitStruct.Alternate = GPIO_AF4_I2C1;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);
    
    // Configure I2C
    hi2c1.Instance = I2C1;
    hi2c1.Init.ClockSpeed = 400000;  // 400kHz
    hi2c1.Init.DutyCycle = I2C_DUTYCYCLE_2;
    hi2c1.Init.OwnAddress1 = 0;
    hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
    hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
    hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
    hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
    HAL_I2C_Init(&hi2c1);
    
    // Configure DMA for RX
    hdma_i2c1_rx.Instance = DMA1_Stream0;
    hdma_i2c1_rx.Init.Channel = DMA_CHANNEL_1;
    hdma_i2c1_rx.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_i2c1_rx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_i2c1_rx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_i2c1_rx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_i2c1_rx.Init.Mode = DMA_NORMAL;
    hdma_i2c1_rx.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_i2c1_rx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    HAL_DMA_Init(&hdma_i2c1_rx);
    
    // Link DMA to I2C
    __HAL_LINKDMA(&hi2c1, hdmarx, hdma_i2c1_rx);
    
    // Enable DMA interrupts
    HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
}

// Read data from I2C device using DMA
HAL_StatusTypeDef I2C_DMA_Read(uint16_t dev_addr, uint8_t reg_addr, 
                                uint8_t *data, uint16_t size) {
    HAL_StatusTypeDef status;
    
    // First, write register address
    status = HAL_I2C_Master_Transmit(&hi2c1, dev_addr, &reg_addr, 1, 100);
    if (status != HAL_OK) return status;
    
    // Then read data using DMA
    transfer_complete = 0;
    status = HAL_I2C_Master_Receive_DMA(&hi2c1, dev_addr, data, size);
    
    return status;
}

// DMA transfer complete callback
void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        transfer_complete = 1;
    }
}

// DMA error callback
void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c->Instance == I2C1) {
        // Handle error
        Error_Handler();
    }
}

// DMA interrupt handler
void DMA1_Stream0_IRQHandler(void) {
    HAL_DMA_IRQHandler(&hdma_i2c1_rx);
}
```

### Example 2: ESP32 (ESP-IDF) - DMA I2C Transfer

```c
#include "driver/i2c.h"
#include "esp_log.h"

#define I2C_MASTER_SCL_IO    22
#define I2C_MASTER_SDA_IO    21
#define I2C_MASTER_FREQ_HZ   400000
#define I2C_MASTER_NUM       I2C_NUM_0

static const char *TAG = "I2C_DMA";

// Initialize I2C with DMA support
esp_err_t i2c_dma_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_MASTER_SDA_IO,
        .scl_io_num = I2C_MASTER_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t err = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (err != ESP_OK) return err;
    
    // Install driver with DMA enabled
    // rx_buf_len > 0 enables DMA for receive
    // tx_buf_len > 0 enables DMA for transmit
    return i2c_driver_install(I2C_MASTER_NUM, conf.mode, 
                              512,  // rx_buf_len
                              512,  // tx_buf_len
                              0);   // intr_alloc_flags
}

// Read sensor data using DMA
esp_err_t read_sensor_dma(uint8_t sensor_addr, uint8_t reg_addr, 
                          uint8_t *data, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    // Write register address
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg_addr, true);
    
    // Repeated start for read
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (sensor_addr << 1) | I2C_MASTER_READ, true);
    
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    
    // Execute command with DMA
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 
                                          pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    return ret;
}

// Write large buffer using DMA
esp_err_t write_display_buffer_dma(uint8_t display_addr, 
                                    const uint8_t *buffer, size_t len) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (display_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write(cmd, buffer, len, true);
    i2c_master_stop(cmd);
    
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, 
                                          pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "DMA transfer failed: %s", esp_err_to_name(ret));
    }
    
    return ret;
}
```

### Example 3: Nordic nRF52 - TWIM with EasyDMA

```c
#include "nrf_drv_twi.h"
#include "app_error.h"

// TWI instance with DMA (TWIM = TWI Master with EasyDMA)
static const nrf_drv_twi_t m_twi = NRF_DRV_TWI_INSTANCE(0);

static volatile bool m_xfer_done = false;
static uint8_t m_sample_buffer[128];

// Event handler
void twi_handler(nrf_drv_twi_evt_t const *p_event, void *p_context) {
    switch (p_event->type) {
        case NRF_DRV_TWI_EVT_DONE:
            m_xfer_done = true;
            if (p_event->xfer_desc.type == NRF_DRV_TWI_XFER_RX) {
                // RX transfer complete - process data
            }
            break;
        
        case NRF_DRV_TWI_EVT_ADDRESS_NACK:
            // Address not acknowledged
            break;
        
        case NRF_DRV_TWI_EVT_DATA_NACK:
            // Data not acknowledged
            break;
        
        default:
            break;
    }
}

// Initialize TWI with DMA
void twi_dma_init(void) {
    ret_code_t err_code;
    
    const nrf_drv_twi_config_t twi_config = {
        .scl = 27,
        .sda = 26,
        .frequency = NRF_DRV_TWI_FREQ_400K,
        .interrupt_priority = APP_IRQ_PRIORITY_HIGH,
        .clear_bus_init = false
    };
    
    err_code = nrf_drv_twi_init(&m_twi, &twi_config, twi_handler, NULL);
    APP_ERROR_CHECK(err_code);
    
    nrf_drv_twi_enable(&m_twi);
}

// Read using DMA
ret_code_t read_sensor_data_dma(uint8_t addr, uint8_t reg, 
                                 uint8_t *buffer, uint8_t len) {
    ret_code_t err_code;
    
    m_xfer_done = false;
    
    // Write register address, then read data
    err_code = nrf_drv_twi_tx(&m_twi, addr, &reg, 1, true);
    if (err_code != NRF_SUCCESS) return err_code;
    
    while (!m_xfer_done) { /* Wait */ }
    
    m_xfer_done = false;
    err_code = nrf_drv_twi_rx(&m_twi, addr, buffer, len);
    
    return err_code;
}
```

## Rust Implementation Examples

### Example 1: Embassy (Async Rust Embedded)

```rust
use embassy_stm32::i2c::{I2c, Config};
use embassy_stm32::time::Hertz;
use embassy_stm32::bind_interrupts;
use embassy_stm32::peripherals;

bind_interrupts!(struct Irqs {
    I2C1_EV => embassy_stm32::i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => embassy_stm32::i2c::ErrorInterruptHandler<peripherals::I2C1>;
});

#[embassy_executor::task]
async fn i2c_dma_task(
    i2c: peripherals::I2C1,
    scl: peripherals::PB8,
    sda: peripherals::PB9,
    tx_dma: peripherals::DMA1_CH6,
    rx_dma: peripherals::DMA1_CH7,
) {
    let mut config = Config::default();
    config.timeout = embassy_time::Duration::from_millis(1000);
    
    // Create I2C with DMA support
    let mut i2c = I2c::new(
        i2c,
        scl,
        sda,
        Irqs,
        tx_dma,
        rx_dma,
        Hertz(400_000),
        config,
    );
    
    const SENSOR_ADDR: u8 = 0x68;
    let mut buffer = [0u8; 256];
    
    loop {
        // Async DMA read - CPU is free during transfer
        match i2c.write_read(SENSOR_ADDR, &[0x3B], &mut buffer[..14]).await {
            Ok(_) => {
                // Process accelerometer/gyro data
                defmt::info!("Data received via DMA: {:?}", &buffer[..14]);
            }
            Err(e) => {
                defmt::error!("I2C DMA error: {:?}", e);
            }
        }
        
        embassy_time::Timer::after_millis(100).await;
    }
}

// Example: Reading large EEPROM block with DMA
async fn read_eeprom_page_dma(
    i2c: &mut I2c<'_, peripherals::I2C1, peripherals::DMA1_CH6, peripherals::DMA1_CH7>,
    eeprom_addr: u8,
    mem_addr: u16,
    buffer: &mut [u8],
) -> Result<(), embassy_stm32::i2c::Error> {
    // For 16-bit memory address
    let addr_bytes = [
        (mem_addr >> 8) as u8,
        (mem_addr & 0xFF) as u8,
    ];
    
    // DMA transfer happens automatically
    i2c.write_read(eeprom_addr, &addr_bytes, buffer).await
}

// Example: Writing display framebuffer with DMA
async fn write_display_buffer_dma(
    i2c: &mut I2c<'_, peripherals::I2C1, peripherals::DMA1_CH6, peripherals::DMA1_CH7>,
    display_addr: u8,
    buffer: &[u8],
) -> Result<(), embassy_stm32::i2c::Error> {
    // Prepare command + data
    let mut tx_buffer = [0u8; 1025];
    tx_buffer[0] = 0x40; // Data mode command
    tx_buffer[1..=1024].copy_from_slice(&buffer[..1024]);
    
    // DMA handles the entire transfer
    i2c.write(display_addr, &tx_buffer).await
}
```

### Example 2: ESP-IDF Rust Bindings

```rust
use esp_idf_hal::i2c::*;
use esp_idf_hal::prelude::*;
use esp_idf_hal::peripherals::Peripherals;
use esp_idf_sys::EspError;

struct I2cDma {
    driver: I2cDriver<'static>,
}

impl I2cDma {
    fn new(peripherals: Peripherals) -> Result<Self, EspError> {
        let i2c = peripherals.i2c0;
        let sda = peripherals.pins.gpio21;
        let scl = peripherals.pins.gpio22;
        
        let config = I2cConfig::new()
            .baudrate(400.kHz().into())
            .sda_enable_pullup(true)
            .scl_enable_pullup(true);
        
        // Create driver with DMA buffers
        let driver = I2cDriver::new(
            i2c,
            sda,
            scl,
            &config,
        )?;
        
        Ok(Self { driver })
    }
    
    // Read sensor data with DMA
    fn read_sensor(
        &mut self,
        sensor_addr: u8,
        reg_addr: u8,
        buffer: &mut [u8],
    ) -> Result<(), EspError> {
        // Write register address then read - DMA handles transfer
        self.driver.write_read(
            sensor_addr,
            &[reg_addr],
            buffer,
            1000, // timeout ms
        )
    }
    
    // Write large buffer with DMA
    fn write_buffer(
        &mut self,
        device_addr: u8,
        data: &[u8],
    ) -> Result<(), EspError> {
        self.driver.write(device_addr, data, 1000)
    }
    
    // Read accelerometer with DMA
    fn read_accelerometer(&mut self) -> Result<[i16; 3], EspError> {
        let mut buffer = [0u8; 6];
        self.read_sensor(0x68, 0x3B, &mut buffer)?;
        
        let x = i16::from_be_bytes([buffer[0], buffer[1]]);
        let y = i16::from_be_bytes([buffer[2], buffer[3]]);
        let z = i16::from_be_bytes([buffer[4], buffer[5]]);
        
        Ok([x, y, z])
    }
}

// Example usage
fn main() -> Result<(), EspError> {
    let peripherals = Peripherals::take()?;
    let mut i2c = I2cDma::new(peripherals)?;
    
    loop {
        match i2c.read_accelerometer() {
            Ok([x, y, z]) => {
                println!("Accel: X={}, Y={}, Z={}", x, y, z);
            }
            Err(e) => {
                eprintln!("I2C error: {:?}", e);
            }
        }
        
        std::thread::sleep(std::time::Duration::from_millis(100));
    }
}
```

### Example 3: embedded-hal with DMA trait

```rust
use embedded_hal::i2c::I2c;
use embedded_dma::{ReadBuffer, WriteBuffer};

// Generic DMA-capable I2C transfer function
async fn i2c_dma_transfer<I2C, E>(
    i2c: &mut I2C,
    address: u8,
    tx_buffer: &[u8],
    rx_buffer: &mut [u8],
) -> Result<(), E>
where
    I2C: I2c<Error = E>,
{
    // Write then read with DMA
    i2c.write_read(address, tx_buffer, rx_buffer)?;
    Ok(())
}

// Example: IMU driver with DMA support
pub struct ImuDriver<I2C> {
    i2c: I2C,
    address: u8,
}

impl<I2C, E> ImuDriver<I2C>
where
    I2C: I2c<Error = E>,
{
    pub fn new(i2c: I2C, address: u8) -> Self {
        Self { i2c, address }
    }
    
    // Read multiple registers using DMA
    pub fn read_registers(
        &mut self,
        start_reg: u8,
        buffer: &mut [u8],
    ) -> Result<(), E> {
        self.i2c.write_read(self.address, &[start_reg], buffer)
    }
    
    // Read full sensor data in one DMA transaction
    pub fn read_all_sensors(&mut self) -> Result<SensorData, E> {
        let mut buffer = [0u8; 14];
        
        // Single DMA transaction for all sensor data
        self.read_registers(0x3B, &mut buffer)?;
        
        Ok(SensorData {
            accel_x: i16::from_be_bytes([buffer[0], buffer[1]]),
            accel_y: i16::from_be_bytes([buffer[2], buffer[3]]),
            accel_z: i16::from_be_bytes([buffer[4], buffer[5]]),
            temp: i16::from_be_bytes([buffer[6], buffer[7]]),
            gyro_x: i16::from_be_bytes([buffer[8], buffer[9]]),
            gyro_y: i16::from_be_bytes([buffer[10], buffer[11]]),
            gyro_z: i16::from_be_bytes([buffer[12], buffer[13]]),
        })
    }
}

#[derive(Debug)]
pub struct SensorData {
    pub accel_x: i16,
    pub accel_y: i16,
    pub accel_z: i16,
    pub temp: i16,
    pub gyro_x: i16,
    pub gyro_y: i16,
    pub gyro_z: i16,
}
```

## Best Practices

**Configuration:**
- Use circular/normal mode appropriately (circular for continuous streaming, normal for one-shot)
- Configure DMA priority based on application needs
- Enable half-transfer interrupts for double-buffering scenarios
- Set appropriate FIFO thresholds to avoid underruns/overruns

**Memory Management:**
- Ensure buffers are in DMA-accessible memory regions
- Use proper alignment for DMA buffers (platform-specific)
- Avoid stack-allocated buffers for DMA in some architectures
- Consider cache coherency on systems with data cache

**Error Handling:**
- Always implement DMA error callbacks
- Handle I2C bus errors separately from DMA errors
- Implement timeouts for DMA transfers
- Clean up DMA state on errors before retrying

**Performance:**
- Batch multiple small transfers into larger ones when possible
- Use DMA for transfers larger than ~16 bytes (threshold varies by platform)
- For very small transfers, polling may be more efficient than DMA overhead

DMA integration transforms I2C from an interrupt-intensive peripheral into an efficient, CPU-friendly interface perfect for high-throughput applications.