#include <stdint.h>
#include <string.h>
#include <stdio.h>

// Hardware abstraction layer
typedef struct {
    void (*cs_low)(void);
    void (*cs_high)(void);
    void (*transfer)(uint8_t *tx_buf, uint8_t *rx_buf, size_t len);
} spi_hal_t;

// Batch buffer structure
typedef struct {
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    size_t capacity;
    size_t current_size;
} spi_batch_t;

// Initialize batch buffer
int spi_batch_init(spi_batch_t *batch, size_t capacity) {
    batch->tx_buffer = (uint8_t *)malloc(capacity);
    batch->rx_buffer = (uint8_t *)malloc(capacity);
    
    if (!batch->tx_buffer || !batch->rx_buffer) {
        free(batch->tx_buffer);
        free(batch->rx_buffer);
        return -1;
    }
    
    batch->capacity = capacity;
    batch->current_size = 0;
    return 0;
}

// Add data to batch
int spi_batch_add(spi_batch_t *batch, const uint8_t *data, size_t len) {
    if (batch->current_size + len > batch->capacity) {
        return -1; // Buffer full
    }
    
    memcpy(&batch->tx_buffer[batch->current_size], data, len);
    batch->current_size += len;
    return 0;
}

// Execute batched transfer
void spi_batch_execute(spi_batch_t *batch, spi_hal_t *hal) {
    if (batch->current_size == 0) {
        return;
    }
    
    hal->cs_low();
    hal->transfer(batch->tx_buffer, batch->rx_buffer, batch->current_size);
    hal->cs_high();
    
    batch->current_size = 0; // Reset for next batch
}

// Example: Reading multiple registers
typedef struct {
    uint8_t reg_addr;
    uint8_t value;
} register_read_t;

void read_multiple_registers(spi_hal_t *hal, const uint8_t *reg_addrs, 
                             register_read_t *results, size_t count) {
    spi_batch_t batch;
    spi_batch_init(&batch, count * 2); // Each read is 2 bytes
    
    // Build batch: command byte + dummy byte for each register
    for (size_t i = 0; i < count; i++) {
        uint8_t cmd[2] = {reg_addrs[i] | 0x80, 0x00}; // 0x80 = read bit
        spi_batch_add(&batch, cmd, 2);
    }
    
    // Execute batched transfer
    spi_batch_execute(&batch, hal);
    
    // Parse results
    for (size_t i = 0; i < count; i++) {
        results[i].reg_addr = reg_addrs[i];
        results[i].value = batch.rx_buffer[i * 2 + 1];
    }
    
    free(batch.tx_buffer);
    free(batch.rx_buffer);
}

// Example: Display framebuffer update with batching
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64
#define CHUNK_SIZE 256

void update_display_batched(spi_hal_t *hal, const uint8_t *framebuffer) {
    const size_t total_bytes = (DISPLAY_WIDTH * DISPLAY_HEIGHT) / 8;
    spi_batch_t batch;
    
    spi_batch_init(&batch, CHUNK_SIZE + 1); // +1 for command byte
    
    for (size_t offset = 0; offset < total_bytes; offset += CHUNK_SIZE) {
        size_t chunk_len = (total_bytes - offset) < CHUNK_SIZE ? 
                           (total_bytes - offset) : CHUNK_SIZE;
        
        // Add data command
        uint8_t cmd = 0x40; // Write to RAM
        spi_batch_add(&batch, &cmd, 1);
        
        // Add pixel data
        spi_batch_add(&batch, &framebuffer[offset], chunk_len);
        
        // Execute this chunk
        spi_batch_execute(&batch, hal);
    }
    
    free(batch.tx_buffer);
    free(batch.rx_buffer);
}

// Advanced: Auto-flushing batch manager
typedef struct {
    spi_batch_t batch;
    spi_hal_t *hal;
    size_t flush_threshold;
} spi_batch_manager_t;

void spi_manager_init(spi_batch_manager_t *mgr, spi_hal_t *hal, 
                      size_t capacity, size_t flush_threshold) {
    spi_batch_init(&mgr->batch, capacity);
    mgr->hal = hal;
    mgr->flush_threshold = flush_threshold;
}

void spi_manager_add(spi_batch_manager_t *mgr, const uint8_t *data, size_t len) {
    // Auto-flush if adding would exceed threshold
    if (mgr->batch.current_size + len >= mgr->flush_threshold) {
        spi_batch_execute(&mgr->batch, mgr->hal);
    }
    
    spi_batch_add(&mgr->batch, data, len);
}

void spi_manager_flush(spi_batch_manager_t *mgr) {
    spi_batch_execute(&mgr->batch, mgr->hal);
}

// Example usage
void example_usage(spi_hal_t *hal) {
    // Read multiple sensor registers
    uint8_t regs[] = {0x00, 0x01, 0x02, 0x03, 0x04};
    register_read_t results[5];
    
    printf("Reading 5 registers in batched transfer...\n");
    read_multiple_registers(hal, regs, results, 5);
    
    for (size_t i = 0; i < 5; i++) {
        printf("Reg 0x%02X = 0x%02X\n", results[i].reg_addr, results[i].value);
    }
    
    // Use batch manager for streaming data
    spi_batch_manager_t mgr;
    spi_manager_init(&mgr, hal, 512, 256); // Flush at 256 bytes
    
    for (int i = 0; i < 100; i++) {
        uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};
        spi_manager_add(&mgr, data, 4); // Auto-flushes when needed
    }
    
    spi_manager_flush(&mgr); // Flush remaining data
}