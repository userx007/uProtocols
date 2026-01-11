/*
 * I2C Error Detection Implementation in C/C++
 * Demonstrates comprehensive error handling for I2C communication
 */

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Error status codes
typedef enum {
    I2C_OK = 0,
    I2C_ERROR_NACK,           // No acknowledge received
    I2C_ERROR_TIMEOUT,        // Communication timeout
    I2C_ERROR_ARBITRATION,    // Arbitration lost
    I2C_ERROR_BUS_ERROR,      // Bus error (invalid start/stop)
    I2C_ERROR_BUSY,           // Bus is busy
    I2C_ERROR_OVERFLOW,       // Buffer overflow
    I2C_ERROR_INVALID_PARAM   // Invalid parameter
} i2c_status_t;

// I2C transaction state
typedef enum {
    I2C_STATE_IDLE,
    I2C_STATE_TRANSMIT,
    I2C_STATE_RECEIVE,
    I2C_STATE_ERROR
} i2c_state_t;

// Hardware register structure (example for typical MCU)
typedef struct {
    volatile uint32_t CR1;      // Control register 1
    volatile uint32_t CR2;      // Control register 2
    volatile uint32_t SR1;      // Status register 1
    volatile uint32_t SR2;      // Status register 2
    volatile uint32_t DR;       // Data register
} I2C_TypeDef;

// Status register 1 bit definitions
#define I2C_SR1_SB          (1 << 0)   // Start bit
#define I2C_SR1_ADDR        (1 << 1)   // Address sent/matched
#define I2C_SR1_BTF         (1 << 2)   // Byte transfer finished
#define I2C_SR1_TXE         (1 << 7)   // Data register empty
#define I2C_SR1_RXNE        (1 << 6)   // Data register not empty
#define I2C_SR1_AF          (1 << 10)  // Acknowledge failure
#define I2C_SR1_ARLO        (1 << 9)   // Arbitration lost
#define I2C_SR1_BERR        (1 << 8)   // Bus error
#define I2C_SR1_TIMEOUT     (1 << 14)  // Timeout error

// Control register 1 bit definitions
#define I2C_CR1_PE          (1 << 0)   // Peripheral enable
#define I2C_CR1_START       (1 << 8)   // Start generation
#define I2C_CR1_STOP        (1 << 9)   // Stop generation
#define I2C_CR1_ACK         (1 << 10)  // Acknowledge enable

// Example I2C peripheral base address (platform specific)
#define I2C1_BASE           ((I2C_TypeDef*)0x40005400)

// Timeout value (iterations)
#define I2C_TIMEOUT_MS      100
#define I2C_TIMEOUT_TICKS   (I2C_TIMEOUT_MS * 1000) // Assuming 1MHz counter

/*
 * Check for acknowledge failure (NACK)
 */
static i2c_status_t i2c_check_ack_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_AF) {
        // Clear the ACK failure flag
        i2c->SR1 &= ~I2C_SR1_AF;
        
        // Generate STOP condition to release the bus
        i2c->CR1 |= I2C_CR1_STOP;
        
        printf("ERROR: NACK received - slave not responding\n");
        return I2C_ERROR_NACK;
    }
    return I2C_OK;
}

/*
 * Check for arbitration loss
 */
static i2c_status_t i2c_check_arbitration_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_ARLO) {
        // Clear the arbitration lost flag
        i2c->SR1 &= ~I2C_SR1_ARLO;
        
        printf("ERROR: Arbitration lost - another master took control\n");
        return I2C_ERROR_ARBITRATION;
    }
    return I2C_OK;
}

/*
 * Check for bus error
 */
static i2c_status_t i2c_check_bus_error(I2C_TypeDef *i2c) {
    if (i2c->SR1 & I2C_SR1_BERR) {
        // Clear the bus error flag
        i2c->SR1 &= ~I2C_SR1_BERR;
        
        printf("ERROR: Bus error - misplaced START or STOP condition\n");
        return I2C_ERROR_BUS_ERROR;
    }
    return I2C_OK;
}

/*
 * Wait for a specific flag with timeout protection
 */
static i2c_status_t i2c_wait_flag(I2C_TypeDef *i2c, uint32_t flag, 
                                   bool state, uint32_t timeout) {
    uint32_t tick_start = get_tick_count(); // Platform-specific timer
    
    while (timeout > 0) {
        // Check for errors first
        i2c_status_t status;
        
        if ((status = i2c_check_ack_error(i2c)) != I2C_OK) return status;
        if ((status = i2c_check_arbitration_error(i2c)) != I2C_OK) return status;
        if ((status = i2c_check_bus_error(i2c)) != I2C_OK) return status;
        
        // Check if flag reached desired state
        bool flag_status = (i2c->SR1 & flag) != 0;
        if (flag_status == state) {
            return I2C_OK;
        }
        
        // Check timeout
        if ((get_tick_count() - tick_start) >= timeout) {
            printf("ERROR: Timeout waiting for flag 0x%08X\n", flag);
            return I2C_ERROR_TIMEOUT;
        }
    }
    
    return I2C_ERROR_TIMEOUT;
}

/*
 * Write data to I2C slave with comprehensive error checking
 */
i2c_status_t i2c_write(I2C_TypeDef *i2c, uint8_t slave_addr, 
                       const uint8_t *data, uint16_t len) {
    i2c_status_t status;
    
    if (!data || len == 0) {
        return I2C_ERROR_INVALID_PARAM;
    }
    
    // Check if bus is busy
    if (i2c->SR2 & (1 << 1)) { // BUSY flag
        printf("ERROR: I2C bus is busy\n");
        return I2C_ERROR_BUSY;
    }
    
    // Generate START condition
    i2c->CR1 |= I2C_CR1_START;
    
    // Wait for START condition to be generated (SB flag)
    status = i2c_wait_flag(i2c, I2C_SR1_SB, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Failed to generate START condition\n");
        return status;
    }
    
    // Send slave address with write bit (LSB = 0)
    i2c->DR = (slave_addr << 1) | 0;
    
    // Wait for address to be sent (ADDR flag)
    status = i2c_wait_flag(i2c, I2C_SR1_ADDR, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Slave address 0x%02X not acknowledged\n", slave_addr);
        return status;
    }
    
    // Clear ADDR flag by reading SR1 and SR2
    (void)i2c->SR1;
    (void)i2c->SR2;
    
    // Transmit data bytes
    for (uint16_t i = 0; i < len; i++) {
        // Wait for TXE (transmit buffer empty)
        status = i2c_wait_flag(i2c, I2C_SR1_TXE, true, I2C_TIMEOUT_TICKS);
        if (status != I2C_OK) {
            printf("ERROR: Timeout waiting for TXE at byte %d\n", i);
            goto error_stop;
        }
        
        // Write data byte
        i2c->DR = data[i];
        
        // Check for NACK after each byte
        status = i2c_check_ack_error(i2c);
        if (status != I2C_OK) {
            printf("ERROR: NACK received at byte %d\n", i);
            goto error_stop;
        }
    }
    
    // Wait for BTF (byte transfer finished)
    status = i2c_wait_flag(i2c, I2C_SR1_BTF, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        goto error_stop;
    }
    
    // Generate STOP condition
    i2c->CR1 |= I2C_CR1_STOP;
    
    return I2C_OK;

error_stop:
    // Generate STOP to release the bus
    i2c->CR1 |= I2C_CR1_STOP;
    return status;
}

/*
 * Read data from I2C slave with error detection
 */
i2c_status_t i2c_read(I2C_TypeDef *i2c, uint8_t slave_addr, 
                      uint8_t *data, uint16_t len) {
    i2c_status_t status;
    
    if (!data || len == 0) {
        return I2C_ERROR_INVALID_PARAM;
    }
    
    // Enable ACK
    i2c->CR1 |= I2C_CR1_ACK;
    
    // Generate START condition
    i2c->CR1 |= I2C_CR1_START;
    
    // Wait for START
    status = i2c_wait_flag(i2c, I2C_SR1_SB, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) return status;
    
    // Send slave address with read bit (LSB = 1)
    i2c->DR = (slave_addr << 1) | 1;
    
    // Wait for address ACK
    status = i2c_wait_flag(i2c, I2C_SR1_ADDR, true, I2C_TIMEOUT_TICKS);
    if (status != I2C_OK) {
        printf("ERROR: Slave 0x%02X NACK on read address\n", slave_addr);
        i2c->CR1 |= I2C_CR1_STOP;
        return status;
    }
    
    // Clear ADDR flag
    (void)i2c->SR1;
    (void)i2c->SR2;
    
    // Receive data bytes
    for (uint16_t i = 0; i < len; i++) {
        // Before last byte, disable ACK and generate STOP
        if (i == len - 1) {
            i2c->CR1 &= ~I2C_CR1_ACK;
            i2c->CR1 |= I2C_CR1_STOP;
        }
        
        // Wait for RXNE (receive buffer not empty)
        status = i2c_wait_flag(i2c, I2C_SR1_RXNE, true, I2C_TIMEOUT_TICKS);
        if (status != I2C_OK) {
            printf("ERROR: Timeout waiting for data byte %d\n", i);
            i2c->CR1 |= I2C_CR1_STOP;
            return status;
        }
        
        // Read data byte
        data[i] = (uint8_t)i2c->DR;
    }
    
    return I2C_OK;
}

/*
 * Recovery procedure for stuck bus
 */
void i2c_bus_recovery(I2C_TypeDef *i2c) {
    printf("Attempting I2C bus recovery...\n");
    
    // Disable I2C peripheral
    i2c->CR1 &= ~I2C_CR1_PE;
    
    // Platform-specific: Toggle SCL line 9 times to recover slaves
    // This requires GPIO bit-banging
    for (int i = 0; i < 9; i++) {
        // toggle_scl_line(); // Platform-specific implementation
        delay_us(5);
    }
    
    // Re-enable I2C peripheral
    i2c->CR1 |= I2C_CR1_PE;
    
    printf("Bus recovery complete\n");
}

/*
 * Example usage with error handling
 */
void example_usage(void) {
    I2C_TypeDef *i2c = I2C1_BASE;
    uint8_t slave_address = 0x50; // Example EEPROM address
    uint8_t write_data[] = {0x00, 0x10, 0xAA, 0xBB, 0xCC};
    uint8_t read_data[3];
    i2c_status_t status;
    
    // Write operation
    status = i2c_write(i2c, slave_address, write_data, sizeof(write_data));
    if (status != I2C_OK) {
        printf("Write failed with error code: %d\n", status);
        
        if (status == I2C_ERROR_TIMEOUT) {
            i2c_bus_recovery(i2c);
        }
        return;
    }
    
    delay_ms(5); // EEPROM write delay
    
    // Read operation
    status = i2c_read(i2c, slave_address, read_data, sizeof(read_data));
    if (status != I2C_OK) {
        printf("Read failed with error code: %d\n", status);
        return;
    }
    
    printf("Read successful: 0x%02X 0x%02X 0x%02X\n", 
           read_data[0], read_data[1], read_data[2]);
}

// Platform-specific helper functions (to be implemented)
uint32_t get_tick_count(void) {
    // Return current tick count from system timer
    return 0; // Placeholder
}

void delay_ms(uint32_t ms) {
    // Platform-specific delay
}

void delay_us(uint32_t us) {
    // Platform-specific microsecond delay
}