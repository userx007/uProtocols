/*
 * I2C Logic Analyzer Debugging Examples (C/C++)
 * 
 * This code demonstrates I2C patterns useful for logic analyzer debugging
 * Includes timing markers and deliberate test cases
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

// Platform-specific I2C headers
#ifdef __linux__
#include <linux/i2c-dev.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#elif defined(ARDUINO)
#include <Wire.h>
#endif

// =============================================================================
// Configuration for Logic Analyzer Debugging
// =============================================================================

#define I2C_BUS "/dev/i2c-1"
#define SLAVE_ADDR 0x48  // Example: TMP102 temperature sensor

// Debug markers - toggle GPIO to create timing markers in logic analyzer
#define DEBUG_PIN_START 23
#define DEBUG_PIN_END 24

// =============================================================================
// Debug Helper Functions
// =============================================================================

typedef struct {
    int fd;
    uint8_t addr;
    char name[32];
} i2c_device_t;

typedef struct {
    uint64_t start_time;
    uint64_t end_time;
    uint8_t address;
    uint8_t reg;
    uint8_t data_len;
    uint8_t data[64];
    bool success;
    char error_msg[128];
} i2c_transaction_log_t;

// Log buffer for post-analysis
#define MAX_LOG_ENTRIES 100
static i2c_transaction_log_t transaction_log[MAX_LOG_ENTRIES];
static int log_index = 0;

// Simple timestamp function (microseconds)
uint64_t get_timestamp_us() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Log transaction for correlation with logic analyzer capture
void log_transaction(uint8_t addr, uint8_t reg, uint8_t *data, 
                     uint8_t len, bool success, const char *error) {
    if (log_index >= MAX_LOG_ENTRIES) log_index = 0;
    
    i2c_transaction_log_t *entry = &transaction_log[log_index++];
    entry->end_time = get_timestamp_us();
    entry->address = addr;
    entry->reg = reg;
    entry->data_len = len;
    entry->success = success;
    
    if (data && len > 0) {
        memcpy(entry->data, data, len < 64 ? len : 64);
    }
    
    if (error) {
        strncpy(entry->error_msg, error, 127);
        entry->error_msg[127] = '\0';
    }
}

// =============================================================================
// I2C Communication Functions with Debug Support
// =============================================================================

int i2c_open(i2c_device_t *dev, const char *bus, uint8_t addr) {
    dev->fd = open(bus, O_RDWR);
    if (dev->fd < 0) {
        perror("Failed to open I2C bus");
        return -1;
    }
    
    if (ioctl(dev->fd, I2C_SLAVE, addr) < 0) {
        perror("Failed to set I2C slave address");
        close(dev->fd);
        return -1;
    }
    
    dev->addr = addr;
    return 0;
}

// Write with detailed logging for logic analyzer correlation
int i2c_write_reg_debug(i2c_device_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    uint8_t buffer[65];
    buffer[0] = reg;
    memcpy(buffer + 1, data, len);
    
    uint64_t start = get_timestamp_us();
    printf("[%llu us] WRITE: Addr=0x%02X Reg=0x%02X Len=%zu Data=", 
           start, dev->addr, reg, len);
    for (size_t i = 0; i < len; i++) {
        printf("0x%02X ", data[i]);
    }
    printf("\n");
    
    ssize_t result = write(dev->fd, buffer, len + 1);
    
    bool success = (result == (ssize_t)(len + 1));
    log_transaction(dev->addr, reg, data, len, success, 
                    success ? NULL : "Write failed");
    
    if (!success) {
        printf("[ERROR] Write failed: expected %zu bytes, got %zd\n", 
               len + 1, result);
    }
    
    return success ? 0 : -1;
}

// Read with detailed logging
int i2c_read_reg_debug(i2c_device_t *dev, uint8_t reg, uint8_t *data, size_t len) {
    uint64_t start = get_timestamp_us();
    
    // Write register address
    if (write(dev->fd, &reg, 1) != 1) {
        printf("[ERROR] Failed to write register address\n");
        log_transaction(dev->addr, reg, NULL, 0, false, "Reg write failed");
        return -1;
    }
    
    // Read data
    ssize_t result = read(dev->fd, data, len);
    
    printf("[%llu us] READ: Addr=0x%02X Reg=0x%02X Len=%zu Data=", 
           get_timestamp_us(), dev->addr, reg, len);
    
    if (result == (ssize_t)len) {
        for (size_t i = 0; i < len; i++) {
            printf("0x%02X ", data[i]);
        }
        printf("\n");
        log_transaction(dev->addr, reg, data, len, true, NULL);
        return 0;
    } else {
        printf("[ERROR] Read failed: expected %zu bytes, got %zd\n", len, result);
        log_transaction(dev->addr, reg, NULL, 0, false, "Read failed");
        return -1;
    }
}

// =============================================================================
// Test Patterns for Logic Analyzer Debugging
// =============================================================================

// Test 1: Single byte write/read sequence
void test_basic_transaction(i2c_device_t *dev) {
    printf("\n=== Test 1: Basic Transaction ===\n");
    uint8_t config = 0x60;
    
    i2c_write_reg_debug(dev, 0x01, &config, 1);
    usleep(10000); // 10ms delay
    
    uint8_t read_back;
    i2c_read_reg_debug(dev, 0x01, &read_back, 1);
    
    if (read_back == config) {
        printf("✓ Read-back verification passed\n");
    } else {
        printf("✗ Read-back mismatch: wrote 0x%02X, read 0x%02X\n", 
               config, read_back);
    }
}

// Test 2: Burst write - good for testing multi-byte transactions
void test_burst_write(i2c_device_t *dev) {
    printf("\n=== Test 2: Burst Write ===\n");
    uint8_t burst_data[] = {0x11, 0x22, 0x33, 0x44, 0x55};
    
    i2c_write_reg_debug(dev, 0x00, burst_data, sizeof(burst_data));
}

// Test 3: Rapid successive transactions - test timing
void test_rapid_transactions(i2c_device_t *dev) {
    printf("\n=== Test 3: Rapid Transactions ===\n");
    uint8_t data;
    
    for (int i = 0; i < 5; i++) {
        i2c_read_reg_debug(dev, 0x00, &data, 1);
        usleep(100); // Very short delay
    }
}

// Test 4: Deliberate error conditions
void test_error_conditions(i2c_device_t *dev) {
    printf("\n=== Test 4: Error Conditions ===\n");
    
    // Try to access non-existent register
    uint8_t data;
    printf("Attempting to read invalid register 0xFF:\n");
    i2c_read_reg_debug(dev, 0xFF, &data, 1);
    
    // Try wrong slave address
    printf("\nAttempting to access wrong address 0x7F:\n");
    i2c_device_t wrong_dev = *dev;
    wrong_dev.addr = 0x7F;
    ioctl(wrong_dev.fd, I2C_SLAVE, 0x7F);
    i2c_read_reg_debug(&wrong_dev, 0x00, &data, 1);
}

// Test 5: Clock stretching scenario (device-dependent)
void test_clock_stretching(i2c_device_t *dev) {
    printf("\n=== Test 5: Clock Stretching Test ===\n");
    printf("Reading temperature (may cause clock stretching):\n");
    
    uint8_t temp_data[2];
    i2c_read_reg_debug(dev, 0x00, temp_data, 2);
    
    // Convert to temperature
    int16_t raw = (temp_data[0] << 4) | (temp_data[1] >> 4);
    float temp = raw * 0.0625;
    printf("Temperature: %.2f°C\n", temp);
}

// =============================================================================
// Analysis and Reporting
// =============================================================================

void print_transaction_summary() {
    printf("\n=== Transaction Summary ===\n");
    printf("Total transactions: %d\n", log_index);
    
    int success_count = 0;
    int failure_count = 0;
    uint64_t total_time = 0;
    
    for (int i = 0; i < log_index; i++) {
        if (transaction_log[i].success) {
            success_count++;
        } else {
            failure_count++;
            printf("Failed transaction %d: Addr=0x%02X Reg=0x%02X Error=%s\n",
                   i, transaction_log[i].address, transaction_log[i].reg,
                   transaction_log[i].error_msg);
        }
        
        if (i > 0) {
            total_time += transaction_log[i].end_time - 
                         transaction_log[i-1].end_time;
        }
    }
    
    printf("Success: %d, Failures: %d\n", success_count, failure_count);
    if (log_index > 1) {
        printf("Average time between transactions: %llu us\n", 
               total_time / (log_index - 1));
    }
}

// Export log in CSV format for correlation with logic analyzer
void export_log_csv(const char *filename) {
    FILE *f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open CSV file");
        return;
    }
    
    fprintf(f, "Timestamp_us,Address,Register,Length,Success,Data,Error\n");
    
    for (int i = 0; i < log_index; i++) {
        i2c_transaction_log_t *entry = &transaction_log[i];
        fprintf(f, "%llu,0x%02X,0x%02X,%d,%s,",
                entry->end_time, entry->address, entry->reg,
                entry->data_len, entry->success ? "true" : "false");
        
        for (int j = 0; j < entry->data_len && j < 64; j++) {
            fprintf(f, "%02X ", entry->data[j]);
        }
        
        fprintf(f, ",%s\n", entry->error_msg);
    }
    
    fclose(f);
    printf("Log exported to %s\n", filename);
}

// =============================================================================
// Main Test Routine
// =============================================================================

int main(int argc, char *argv[]) {
    i2c_device_t device;
    
    printf("I2C Logic Analyzer Debug Tool\n");
    printf("==============================\n\n");
    printf("Connect your logic analyzer to:\n");
    printf("  - SDA (GPIO2 on Raspberry Pi)\n");
    printf("  - SCL (GPIO3 on Raspberry Pi)\n");
    printf("  - GND\n\n");
    printf("Press Enter to start tests...");
    getchar();
    
    // Open I2C device
    if (i2c_open(&device, I2C_BUS, SLAVE_ADDR) < 0) {
        return 1;
    }
    
    printf("\n[%llu us] Starting test sequence\n", get_timestamp_us());
    
    // Run test patterns
    test_basic_transaction(&device);
    usleep(100000); // 100ms between tests
    
    test_burst_write(&device);
    usleep(100000);
    
    test_rapid_transactions(&device);
    usleep(100000);
    
    test_error_conditions(&device);
    usleep(100000);
    
    test_clock_stretching(&device);
    
    printf("\n[%llu us] Test sequence complete\n", get_timestamp_us());
    
    // Analysis
    print_transaction_summary();
    export_log_csv("i2c_debug_log.csv");
    
    close(device.fd);
    
    printf("\nAnalysis Tips:\n");
    printf("1. Import i2c_debug_log.csv into your logic analyzer software\n");
    printf("2. Use timestamps to correlate software events with captures\n");
    printf("3. Look for ACK/NACK patterns on failed transactions\n");
    printf("4. Check for clock stretching during temperature reads\n");
    printf("5. Verify timing meets I2C spec (100kHz/400kHz)\n");
    
    return 0;
}