/*
 * I2C Multi-Master Bus Arbitration Implementation
 * Demonstrates collision detection and graceful arbitration loss handling
 */

#include <stdint.h>
#include <stdbool.h>

// Hardware-specific definitions (adjust for your platform)
#define I2C_SDA_PIN     (1 << 0)
#define I2C_SCL_PIN     (1 << 1)
#define I2C_PORT        (*((volatile uint32_t*)0x40000000))
#define I2C_PIN_STATE   (*((volatile uint32_t*)0x40000004))

// Arbitration states
typedef enum {
    ARB_WON,
    ARB_LOST,
    ARB_IN_PROGRESS
} ArbitrationState;

// I2C Master context
typedef struct {
    ArbitrationState arb_state;
    uint32_t retry_count;
    uint32_t max_retries;
    bool is_master;
} I2C_Master;

// Initialize I2C pins as open-drain
void i2c_init_multi_master(I2C_Master *master) {
    // Configure pins as open-drain outputs with pull-ups
    // Implementation depends on your microcontroller
    master->arb_state = ARB_IN_PROGRESS;
    master->retry_count = 0;
    master->max_retries = 3;
    master->is_master = true;
}

// Set SDA line (0 = LOW, 1 = release to HIGH via pull-up)
static inline void i2c_sda_write(bool state) {
    if (state) {
        I2C_PORT |= I2C_SDA_PIN;  // Release (HIGH via pull-up)
    } else {
        I2C_PORT &= ~I2C_SDA_PIN; // Drive LOW
    }
}

// Read SDA line state
static inline bool i2c_sda_read(void) {
    return (I2C_PIN_STATE & I2C_SDA_PIN) != 0;
}

// Set SCL line
static inline void i2c_scl_write(bool state) {
    if (state) {
        I2C_PORT |= I2C_SCL_PIN;
    } else {
        I2C_PORT &= ~I2C_SCL_PIN;
    }
}

// Read SCL line (for clock stretching)
static inline bool i2c_scl_read(void) {
    return (I2C_PIN_STATE & I2C_SCL_PIN) != 0;
}

// Delay function (adjust timing for your clock speed)
static void i2c_delay(void) {
    for (volatile int i = 0; i < 100; i++);
}

// Write a bit with arbitration checking
bool i2c_write_bit_with_arbitration(I2C_Master *master, bool bit) {
    // Release SCL
    i2c_scl_write(1);
    i2c_delay();
    
    // Wait for clock stretching
    while (!i2c_scl_read()) {
        // Slave might be stretching clock
    }
    
    // Write the bit
    i2c_sda_write(bit);
    i2c_delay();
    
    // Read back the actual bus state
    bool actual_bit = i2c_sda_read();
    
    // Arbitration check: if we wrote 1 but read 0, we lost
    if (bit && !actual_bit) {
        master->arb_state = ARB_LOST;
        master->is_master = false;
        return false; // Arbitration lost
    }
    
    // Pull SCL low for next bit
    i2c_scl_write(0);
    i2c_delay();
    
    return true; // Still winning or bit matched
}

// Send START condition with arbitration
bool i2c_start_with_arbitration(I2C_Master *master) {
    // Ensure bus is idle (both lines HIGH)
    i2c_sda_write(1);
    i2c_scl_write(1);
    i2c_delay();
    
    // Check if bus is busy
    if (!i2c_sda_read() || !i2c_scl_read()) {
        // Bus busy, another master is using it
        return false;
    }
    
    // Generate START: SDA falls while SCL is HIGH
    i2c_sda_write(0);
    i2c_delay();
    i2c_scl_write(0);
    i2c_delay();
    
    master->arb_state = ARB_IN_PROGRESS;
    return true;
}

// Write a byte with arbitration checking
bool i2c_write_byte_with_arbitration(I2C_Master *master, uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        bool bit = (byte >> i) & 0x01;
        
        if (!i2c_write_bit_with_arbitration(master, bit)) {
            // Arbitration lost during transmission
            return false;
        }
    }
    
    // Read ACK/NACK bit
    i2c_scl_write(1);
    i2c_sda_write(1); // Release SDA for ACK
    i2c_delay();
    
    bool ack = !i2c_sda_read(); // ACK is LOW
    
    i2c_scl_write(0);
    i2c_delay();
    
    master->arb_state = ARB_WON;
    return ack;
}

// Send STOP condition
void i2c_stop(I2C_Master *master) {
    i2c_sda_write(0);
    i2c_delay();
    i2c_scl_write(1);
    i2c_delay();
    i2c_sda_write(1);
    i2c_delay();
    
    master->is_master = true;
}

// High-level transaction with automatic retry on arbitration loss
bool i2c_master_write_with_retry(I2C_Master *master, uint8_t addr, 
                                  uint8_t *data, uint8_t len) {
    master->retry_count = 0;
    
    while (master->retry_count < master->max_retries) {
        // Try to start transaction
        if (!i2c_start_with_arbitration(master)) {
            master->retry_count++;
            continue; // Bus busy, retry
        }
        
        // Send address with write bit
        if (!i2c_write_byte_with_arbitration(master, (addr << 1) | 0)) {
            if (master->arb_state == ARB_LOST) {
                // Lost arbitration, retry after delay
                master->retry_count++;
                i2c_delay();
                continue;
            }
            // NACK received
            i2c_stop(master);
            return false;
        }
        
        // Send data bytes
        for (uint8_t i = 0; i < len; i++) {
            if (!i2c_write_byte_with_arbitration(master, data[i])) {
                if (master->arb_state == ARB_LOST) {
                    master->retry_count++;
                    i2c_delay();
                    goto retry;
                }
                i2c_stop(master);
                return false;
            }
        }
        
        i2c_stop(master);
        return true; // Success
        
        retry:
        continue;
    }
    
    return false; // Max retries exceeded
}

// Example usage
void example_multi_master(void) {
    I2C_Master master;
    i2c_init_multi_master(&master);
    
    uint8_t data[] = {0x12, 0x34, 0x56};
    uint8_t slave_addr = 0x50;
    
    if (i2c_master_write_with_retry(&master, slave_addr, data, 3)) {
        // Transaction successful
    } else {
        // Failed after retries
    }
}