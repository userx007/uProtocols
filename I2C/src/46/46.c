// I2C Throughput Optimization Examples in C/C++
// Demonstrates batching, burst operations, and performance measurement

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Platform-specific I2C HAL (example interface)
typedef struct {
    void* handle;
    uint32_t clock_speed;
} i2c_bus_t;

// Mock HAL functions (replace with actual platform HAL)
extern int hal_i2c_write(i2c_bus_t* bus, uint8_t addr, const uint8_t* data, size_t len);
extern int hal_i2c_read(i2c_bus_t* bus, uint8_t addr, uint8_t* data, size_t len);
extern int hal_i2c_write_read(i2c_bus_t* bus, uint8_t addr, 
                               const uint8_t* tx_data, size_t tx_len,
                               uint8_t* rx_data, size_t rx_len);
extern uint32_t hal_get_tick_ms(void);

// ============================================================================
// NAIVE APPROACH - Low Throughput
// ============================================================================

// Read multiple registers individually (SLOW)
int read_registers_naive(i2c_bus_t* bus, uint8_t device_addr, 
                          uint8_t start_reg, uint8_t* values, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint8_t reg = start_reg + i;
        
        // Each iteration: START + ADDR + REG + RESTART + ADDR + DATA + STOP
        if (hal_i2c_write_read(bus, device_addr, &reg, 1, &values[i], 1) != 0) {
            return -1;
        }
    }
    return 0;
}

// Write multiple registers individually (SLOW)
int write_registers_naive(i2c_bus_t* bus, uint8_t device_addr,
                          uint8_t start_reg, const uint8_t* values, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint8_t buffer[2] = {start_reg + i, values[i]};
        
        // Each iteration: START + ADDR + REG + DATA + STOP
        if (hal_i2c_write(bus, device_addr, buffer, 2) != 0) {
            return -1;
        }
    }
    return 0;
}

// ============================================================================
// OPTIMIZED APPROACH - Burst Transfers
// ============================================================================

// Read multiple consecutive registers in one transaction (FAST)
int read_registers_burst(i2c_bus_t* bus, uint8_t device_addr,
                         uint8_t start_reg, uint8_t* values, size_t count) {
    // Single transaction: START + ADDR + REG + RESTART + ADDR + DATA[0..n] + STOP
    return hal_i2c_write_read(bus, device_addr, &start_reg, 1, values, count);
}

// Write multiple consecutive registers in one transaction (FAST)
int write_registers_burst(i2c_bus_t* bus, uint8_t device_addr,
                          uint8_t start_reg, const uint8_t* values, size_t count) {
    uint8_t buffer[256]; // Adjust size based on max burst length
    
    if (count > 255) return -1; // Buffer overflow protection
    
    buffer[0] = start_reg;
    memcpy(&buffer[1], values, count);
    
    // Single transaction: START + ADDR + REG + DATA[0..n] + STOP
    return hal_i2c_write(bus, device_addr, buffer, count + 1);
}

// ============================================================================
// BATCHED OPERATIONS - Group Multiple Non-Consecutive Registers
// ============================================================================

typedef struct {
    uint8_t reg_addr;
    uint8_t value;
} reg_value_pair_t;

// Write multiple non-consecutive registers efficiently
int write_registers_batched(i2c_bus_t* bus, uint8_t device_addr,
                            const reg_value_pair_t* pairs, size_t count) {
    uint8_t buffer[512]; // Adjust based on needs
    size_t pos = 0;
    
    // Pack all register writes into a single buffer
    for (size_t i = 0; i < count; i++) {
        if (pos + 2 > sizeof(buffer)) return -1;
        buffer[pos++] = pairs[i].reg_addr;
        buffer[pos++] = pairs[i].value;
    }
    
    // Note: This assumes device supports writing reg-value pairs
    // Some devices require separate transactions
    return hal_i2c_write(bus, device_addr, buffer, pos);
}

// ============================================================================
// DMA-BASED TRANSFERS (Platform Specific)
// ============================================================================

typedef struct {
    volatile bool complete;
    int error;
} i2c_dma_transfer_t;

// Callback for DMA completion (called from interrupt context)
void i2c_dma_callback(i2c_dma_transfer_t* transfer, int error) {
    transfer->error = error;
    transfer->complete = true;
}

// Initiate non-blocking DMA transfer
int read_registers_dma(i2c_bus_t* bus, uint8_t device_addr,
                       uint8_t start_reg, uint8_t* values, size_t count,
                       i2c_dma_transfer_t* transfer) {
    transfer->complete = false;
    transfer->error = 0;
    
    // Platform-specific DMA setup would go here
    // Example: hal_i2c_read_dma(bus, device_addr, start_reg, values, count, callback);
    
    return 0;
}

// Wait for DMA transfer to complete
int wait_for_dma(i2c_dma_transfer_t* transfer, uint32_t timeout_ms) {
    uint32_t start = hal_get_tick_ms();
    
    while (!transfer->complete) {
        if (hal_get_tick_ms() - start > timeout_ms) {
            return -1; // Timeout
        }
        // Could use WFI/sleep here to save power
    }
    
    return transfer->error;
}

// ============================================================================
// PERFORMANCE BENCHMARKING
// ============================================================================

typedef struct {
    uint32_t duration_ms;
    size_t bytes_transferred;
    float throughput_kbps;
    uint32_t transactions;
} benchmark_result_t;

benchmark_result_t benchmark_read(i2c_bus_t* bus, uint8_t device_addr,
                                  uint8_t start_reg, size_t count, bool use_burst) {
    uint8_t buffer[256];
    benchmark_result_t result = {0};
    
    uint32_t start = hal_get_tick_ms();
    
    if (use_burst) {
        read_registers_burst(bus, device_addr, start_reg, buffer, count);
        result.transactions = 1;
    } else {
        read_registers_naive(bus, device_addr, start_reg, buffer, count);
        result.transactions = count;
    }
    
    uint32_t end = hal_get_tick_ms();
    
    result.duration_ms = end - start;
    result.bytes_transferred = count;
    result.throughput_kbps = (count * 8.0f) / (result.duration_ms);
    
    return result;
}

// ============================================================================
// EXAMPLE: IMU SENSOR WITH BURST READ
// ============================================================================

#define IMU_ADDR 0x68
#define IMU_REG_ACCEL_X_H 0x3B
#define IMU_DATA_SIZE 14  // 6 accel + 2 temp + 6 gyro bytes

typedef struct {
    int16_t accel_x, accel_y, accel_z;
    int16_t temp;
    int16_t gyro_x, gyro_y, gyro_z;
} imu_data_t;

// Optimized: Read all sensor data in single burst
int read_imu_burst(i2c_bus_t* bus, imu_data_t* data) {
    uint8_t buffer[IMU_DATA_SIZE];
    
    // Single I2C transaction reads all 14 bytes
    if (read_registers_burst(bus, IMU_ADDR, IMU_REG_ACCEL_X_H, 
                            buffer, IMU_DATA_SIZE) != 0) {
        return -1;
    }
    
    // Parse big-endian sensor data
    data->accel_x = (buffer[0] << 8) | buffer[1];
    data->accel_y = (buffer[2] << 8) | buffer[3];
    data->accel_z = (buffer[4] << 8) | buffer[5];
    data->temp = (buffer[6] << 8) | buffer[7];
    data->gyro_x = (buffer[8] << 8) | buffer[9];
    data->gyro_y = (buffer[10] << 8) | buffer[11];
    data->gyro_z = (buffer[12] << 8) | buffer[13];
    
    return 0;
}

// ============================================================================
// CLOCK SPEED OPTIMIZATION
// ============================================================================

typedef enum {
    I2C_SPEED_STANDARD = 100000,   // 100 kHz
    I2C_SPEED_FAST = 400000,       // 400 kHz
    I2C_SPEED_FAST_PLUS = 1000000, // 1 MHz
    I2C_SPEED_HIGH = 3400000       // 3.4 MHz
} i2c_speed_t;

int set_optimal_speed(i2c_bus_t* bus, i2c_speed_t desired_speed) {
    // Platform-specific implementation
    // Should verify signal integrity and device support
    bus->clock_speed = desired_speed;
    
    // Example: hal_i2c_set_clock(bus->handle, desired_speed);
    
    return 0;
}

// ============================================================================
// USAGE EXAMPLE
// ============================================================================

void example_usage(void) {
    i2c_bus_t bus = {0};
    bus.clock_speed = I2C_SPEED_FAST;
    
    // Configure for maximum throughput
    set_optimal_speed(&bus, I2C_SPEED_FAST);
    
    // Benchmark comparison
    benchmark_result_t naive = benchmark_read(&bus, IMU_ADDR, 0x00, 100, false);
    benchmark_result_t burst = benchmark_read(&bus, IMU_ADDR, 0x00, 100, true);
    
    // Burst mode typically 5-10x faster for sequential reads
    
    // Read IMU data efficiently
    imu_data_t imu;
    read_imu_burst(&bus, &imu);
    
    // Batch write configuration registers
    reg_value_pair_t config[] = {
        {0x6B, 0x00},  // PWR_MGMT_1: Wake up
        {0x1B, 0x18},  // GYRO_CONFIG: ±2000°/s
        {0x1C, 0x10},  // ACCEL_CONFIG: ±8g
    };
    write_registers_batched(&bus, IMU_ADDR, config, 3);
}