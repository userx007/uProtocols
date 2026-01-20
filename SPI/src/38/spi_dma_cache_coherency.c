/*
 * SPI DMA Cache Coherency Management
 * For ARM Cortex-M7 with data cache enabled
 */

#include <stdint.h>
#include <string.h>

// ARM Cortex-M cache management functions
#define SCB_CCSIDR  (*((volatile uint32_t *)0xE000ED80))
#define SCB_CSSELR  (*((volatile uint32_t *)0xE000ED84))
#define SCB_DCISW   (*((volatile uint32_t *)0xE000EF60))
#define SCB_DCCISW  (*((volatile uint32_t *)0xE000EF74))
#define SCB_DCCMVAC (*((volatile uint32_t *)0xE000EF7C))
#define SCB_DCIMVAC (*((volatile uint32_t *)0xE000EF5C))

// Cache line size (typically 32 bytes on Cortex-M7)
#define CACHE_LINE_SIZE 32

// Ensure buffers are aligned to cache line boundaries
#define CACHE_ALIGNED __attribute__((aligned(CACHE_LINE_SIZE)))

// DMA buffer - must be in non-cached memory or properly managed
static uint8_t CACHE_ALIGNED spi_tx_buffer[256];
static uint8_t CACHE_ALIGNED spi_rx_buffer[256];

/**
 * Data Synchronization Barrier
 * Ensures all memory accesses complete before continuing
 */
static inline void dsb(void) {
    __asm__ volatile ("dsb" : : : "memory");
}

/**
 * Data Memory Barrier
 * Ensures memory accesses occur in program order
 */
static inline void dmb(void) {
    __asm__ volatile ("dmb" : : : "memory");
}

/**
 * Clean (flush) data cache for a memory region
 * This writes cached data back to main memory
 */
void cache_clean(void *addr, size_t size) {
    uint32_t start_addr = (uint32_t)addr & ~(CACHE_LINE_SIZE - 1);
    uint32_t end_addr = ((uint32_t)addr + size + CACHE_LINE_SIZE - 1) 
                        & ~(CACHE_LINE_SIZE - 1);
    
    // Clean each cache line in the range
    for (uint32_t ptr = start_addr; ptr < end_addr; ptr += CACHE_LINE_SIZE) {
        SCB_DCCMVAC = ptr;
    }
    
    dsb(); // Ensure clean completes
    dmb(); // Ensure ordering
}

/**
 * Invalidate data cache for a memory region
 * This marks cache lines as invalid, forcing reads from memory
 */
void cache_invalidate(void *addr, size_t size) {
    uint32_t start_addr = (uint32_t)addr & ~(CACHE_LINE_SIZE - 1);
    uint32_t end_addr = ((uint32_t)addr + size + CACHE_LINE_SIZE - 1) 
                        & ~(CACHE_LINE_SIZE - 1);
    
    // Invalidate each cache line in the range
    for (uint32_t ptr = start_addr; ptr < end_addr; ptr += CACHE_LINE_SIZE) {
        SCB_DCIMVAC = ptr;
    }
    
    dsb(); // Ensure invalidation completes
    dmb(); // Ensure ordering
}

/**
 * Clean and invalidate data cache
 * Use when buffer will be both read (by DMA) and written (by DMA)
 */
void cache_clean_invalidate(void *addr, size_t size) {
    uint32_t start_addr = (uint32_t)addr & ~(CACHE_LINE_SIZE - 1);
    uint32_t end_addr = ((uint32_t)addr + size + CACHE_LINE_SIZE - 1) 
                        & ~(CACHE_LINE_SIZE - 1);
    
    for (uint32_t ptr = start_addr; ptr < end_addr; ptr += CACHE_LINE_SIZE) {
        SCB_DCCISW = ptr;
    }
    
    dsb();
    dmb();
}

/**
 * SPI DMA transmit with proper cache management
 */
int spi_dma_transmit(const uint8_t *data, size_t length) {
    if (length > sizeof(spi_tx_buffer)) {
        return -1;
    }
    
    // Copy data to DMA buffer
    memcpy(spi_tx_buffer, data, length);
    
    // CRITICAL: Clean cache to ensure DMA reads updated data
    cache_clean(spi_tx_buffer, length);
    
    // Configure and start DMA transfer
    // DMA_Configure(spi_tx_buffer, length);
    // DMA_Start();
    
    return 0;
}

/**
 * SPI DMA receive with proper cache management
 */
int spi_dma_receive(uint8_t *data, size_t length) {
    if (length > sizeof(spi_rx_buffer)) {
        return -1;
    }
    
    // CRITICAL: Invalidate cache before DMA writes
    // This prevents reading stale cached data
    cache_invalidate(spi_rx_buffer, length);
    
    // Configure and start DMA transfer
    // DMA_Configure(spi_rx_buffer, length);
    // DMA_Start();
    
    // Wait for DMA completion (interrupt or polling)
    // while (!DMA_Complete());
    
    // CRITICAL: Invalidate again to ensure we read from memory
    cache_invalidate(spi_rx_buffer, length);
    
    // Copy received data
    memcpy(data, spi_rx_buffer, length);
    
    return 0;
}

/**
 * SPI DMA full-duplex transfer
 */
int spi_dma_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t length) {
    if (length > sizeof(spi_tx_buffer) || length > sizeof(spi_rx_buffer)) {
        return -1;
    }
    
    // Prepare TX buffer
    memcpy(spi_tx_buffer, tx_data, length);
    cache_clean(spi_tx_buffer, length);
    
    // Prepare RX buffer
    cache_invalidate(spi_rx_buffer, length);
    
    // Configure DMA for both TX and RX
    // DMA_TX_Configure(spi_tx_buffer, length);
    // DMA_RX_Configure(spi_rx_buffer, length);
    // DMA_Start();
    
    // Wait for completion
    // while (!DMA_Complete());
    
    // Ensure we read fresh data
    cache_invalidate(spi_rx_buffer, length);
    
    memcpy(rx_data, spi_rx_buffer, length);
    
    return 0;
}

/**
 * Alternative: Using non-cached memory regions (linker script)
 * Place DMA buffers in a non-cached memory section
 */
#if defined(USE_NONCACHED_MEMORY)
// In linker script, define .noncached_data section in specific RAM region
__attribute__((section(".noncached_data"))) 
static uint8_t spi_tx_buffer_nc[256];

__attribute__((section(".noncached_data")))
static uint8_t spi_rx_buffer_nc[256];

// No cache management needed for these buffers!
int spi_dma_transmit_nc(const uint8_t *data, size_t length) {
    memcpy(spi_tx_buffer_nc, data, length);
    // DMA_Configure(spi_tx_buffer_nc, length);
    // DMA_Start();
    return 0;
}
#endif

/**
 * Example: Reading from SPI flash with DMA
 */
typedef struct {
    uint8_t cmd;
    uint8_t addr[3];
} CACHE_ALIGNED spi_flash_cmd_t;

int spi_flash_read_dma(uint32_t address, uint8_t *buffer, size_t length) {
    static spi_flash_cmd_t CACHE_ALIGNED cmd;
    
    // Prepare command
    cmd.cmd = 0x03; // READ command
    cmd.addr[0] = (address >> 16) & 0xFF;
    cmd.addr[1] = (address >> 8) & 0xFF;
    cmd.addr[2] = address & 0xFF;
    
    // Clean command cache
    cache_clean(&cmd, sizeof(cmd));
    
    // Invalidate receive buffer
    cache_invalidate(buffer, length);
    
    // Send command via DMA
    // spi_dma_transmit((uint8_t*)&cmd, sizeof(cmd));
    
    // Receive data via DMA
    // spi_dma_receive(buffer, length);
    
    // Final invalidate to ensure fresh data
    cache_invalidate(buffer, length);
    
    return 0;
}