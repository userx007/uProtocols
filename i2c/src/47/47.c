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