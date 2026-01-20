/*
 * SPI Timeout Management Implementation in C
 * Demonstrates various timeout strategies for robust SPI communication
 */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Hardware abstraction layer (platform-specific)
typedef struct {
    volatile uint32_t* DR;      // Data register
    volatile uint32_t* SR;      // Status register
    volatile uint32_t* CR;      // Control register
} SPI_Registers;

// Status register bit definitions
#define SPI_SR_TXE   (1 << 1)   // Transmit buffer empty
#define SPI_SR_RXNE  (1 << 0)   // Receive buffer not empty
#define SPI_SR_BSY   (1 << 7)   // Busy flag

// Timeout configuration
#define SPI_DEFAULT_TIMEOUT_MS  100
#define SPI_BYTE_TIMEOUT_MS     10
#define SYSTICK_FREQ_HZ         1000  // 1ms tick

// Error codes
typedef enum {
    SPI_OK = 0,
    SPI_ERROR_TIMEOUT,
    SPI_ERROR_BUSY,
    SPI_ERROR_HARDWARE,
    SPI_ERROR_INVALID_PARAM
} SPI_Status;

// SPI Handle with timeout tracking
typedef struct {
    SPI_Registers* regs;
    uint32_t timeout_ms;
    volatile uint32_t* systick;  // Pointer to system tick counter
    bool use_dma;
} SPI_Handle;

// Global system tick counter (incremented by SysTick ISR)
volatile uint32_t g_system_tick = 0;

/*
 * Get current system time in milliseconds
 */
static inline uint32_t get_tick(void) {
    return g_system_tick;
}

/*
 * Check if timeout has elapsed
 */
static inline bool is_timeout(uint32_t start_tick, uint32_t timeout_ms) {
    return (get_tick() - start_tick) >= timeout_ms;
}

/*
 * Wait for SPI flag with timeout
 */
static SPI_Status wait_for_flag(SPI_Handle* hspi, uint32_t flag, 
                                bool flag_state, uint32_t timeout_ms) {
    uint32_t start = get_tick();
    
    while (1) {
        bool current_state = (*hspi->regs->SR & flag) != 0;
        
        if (current_state == flag_state) {
            return SPI_OK;
        }
        
        if (is_timeout(start, timeout_ms)) {
            return SPI_ERROR_TIMEOUT;
        }
        
        // Optional: yield to other tasks in RTOS environment
        // osThreadYield();
    }
}

/*
 * Transmit single byte with timeout
 */
SPI_Status spi_transmit_byte(SPI_Handle* hspi, uint8_t data, uint32_t timeout_ms) {
    SPI_Status status;
    
    // Wait for TX buffer empty
    status = wait_for_flag(hspi, SPI_SR_TXE, true, timeout_ms);
    if (status != SPI_OK) {
        return status;
    }
    
    // Send data
    *hspi->regs->DR = data;
    
    // Wait for transmission complete
    return wait_for_flag(hspi, SPI_SR_BSY, false, timeout_ms);
}

/*
 * Receive single byte with timeout
 */
SPI_Status spi_receive_byte(SPI_Handle* hspi, uint8_t* data, uint32_t timeout_ms) {
    SPI_Status status;
    
    // Send dummy byte to generate clock
    status = wait_for_flag(hspi, SPI_SR_TXE, true, timeout_ms);
    if (status != SPI_OK) {
        return status;
    }
    *hspi->regs->DR = 0xFF;
    
    // Wait for received data
    status = wait_for_flag(hspi, SPI_SR_RXNE, true, timeout_ms);
    if (status != SPI_OK) {
        return status;
    }
    
    *data = (uint8_t)(*hspi->regs->DR);
    return SPI_OK;
}

/*
 * Transmit buffer with per-byte timeout tracking
 */
SPI_Status spi_transmit(SPI_Handle* hspi, const uint8_t* tx_data, 
                       size_t length, uint32_t timeout_ms) {
    if (!hspi || !tx_data || length == 0) {
        return SPI_ERROR_INVALID_PARAM;
    }
    
    uint32_t start = get_tick();
    uint32_t byte_timeout = (timeout_ms > 0) ? timeout_ms / length : SPI_BYTE_TIMEOUT_MS;
    
    if (byte_timeout == 0) {
        byte_timeout = 1;
    }
    
    for (size_t i = 0; i < length; i++) {
        // Check overall timeout
        if (is_timeout(start, timeout_ms)) {
            return SPI_ERROR_TIMEOUT;
        }
        
        // Transmit with per-byte timeout
        SPI_Status status = spi_transmit_byte(hspi, tx_data[i], byte_timeout);
        if (status != SPI_OK) {
            return status;
        }
    }
    
    return SPI_OK;
}

/*
 * Full-duplex transfer with timeout
 */
SPI_Status spi_transfer(SPI_Handle* hspi, const uint8_t* tx_data, 
                       uint8_t* rx_data, size_t length, uint32_t timeout_ms) {
    if (!hspi || !tx_data || !rx_data || length == 0) {
        return SPI_ERROR_INVALID_PARAM;
    }
    
    uint32_t start = get_tick();
    uint32_t byte_timeout = (timeout_ms > 0) ? timeout_ms / length : SPI_BYTE_TIMEOUT_MS;
    
    if (byte_timeout == 0) {
        byte_timeout = 1;
    }
    
    for (size_t i = 0; i < length; i++) {
        if (is_timeout(start, timeout_ms)) {
            return SPI_ERROR_TIMEOUT;
        }
        
        // Wait for TX buffer empty
        SPI_Status status = wait_for_flag(hspi, SPI_SR_TXE, true, byte_timeout);
        if (status != SPI_OK) {
            return status;
        }
        
        // Send data
        *hspi->regs->DR = tx_data[i];
        
        // Wait for received data
        status = wait_for_flag(hspi, SPI_SR_RXNE, true, byte_timeout);
        if (status != SPI_OK) {
            return status;
        }
        
        // Read received data
        rx_data[i] = (uint8_t)(*hspi->regs->DR);
    }
    
    return SPI_OK;
}

/*
 * Advanced: Transaction with automatic retry on timeout
 */
typedef struct {
    uint8_t max_retries;
    uint32_t retry_delay_ms;
    bool (*error_callback)(SPI_Handle* hspi, SPI_Status error);
} SPI_RetryConfig;

SPI_Status spi_transfer_with_retry(SPI_Handle* hspi, const uint8_t* tx_data,
                                   uint8_t* rx_data, size_t length,
                                   uint32_t timeout_ms, 
                                   const SPI_RetryConfig* retry_cfg) {
    uint8_t attempt = 0;
    SPI_Status status;
    
    do {
        status = spi_transfer(hspi, tx_data, rx_data, length, timeout_ms);
        
        if (status == SPI_OK) {
            return SPI_OK;
        }
        
        // Call error callback if provided
        if (retry_cfg->error_callback) {
            if (!retry_cfg->error_callback(hspi, status)) {
                return status;  // Callback says don't retry
            }
        }
        
        // Delay before retry
        if (attempt < retry_cfg->max_retries) {
            uint32_t delay_start = get_tick();
            while (!is_timeout(delay_start, retry_cfg->retry_delay_ms)) {
                // Busy wait or yield
            }
        }
        
        attempt++;
    } while (attempt <= retry_cfg->max_retries);
    
    return status;
}

/*
 * Watchdog-aware SPI transfer
 * Refreshes watchdog during long transfers
 */
SPI_Status spi_transfer_with_watchdog(SPI_Handle* hspi, const uint8_t* tx_data,
                                     uint8_t* rx_data, size_t length,
                                     uint32_t timeout_ms,
                                     void (*watchdog_refresh)(void)) {
    if (!hspi || !tx_data || !rx_data || length == 0) {
        return SPI_ERROR_INVALID_PARAM;
    }
    
    uint32_t start = get_tick();
    uint32_t last_watchdog_refresh = start;
    const uint32_t WATCHDOG_REFRESH_INTERVAL_MS = 50;
    
    for (size_t i = 0; i < length; i++) {
        if (is_timeout(start, timeout_ms)) {
            return SPI_ERROR_TIMEOUT;
        }
        
        // Refresh watchdog periodically
        if (watchdog_refresh && 
            is_timeout(last_watchdog_refresh, WATCHDOG_REFRESH_INTERVAL_MS)) {
            watchdog_refresh();
            last_watchdog_refresh = get_tick();
        }
        
        // Perform transfer
        SPI_Status status = wait_for_flag(hspi, SPI_SR_TXE, true, SPI_BYTE_TIMEOUT_MS);
        if (status != SPI_OK) return status;
        
        *hspi->regs->DR = tx_data[i];
        
        status = wait_for_flag(hspi, SPI_SR_RXNE, true, SPI_BYTE_TIMEOUT_MS);
        if (status != SPI_OK) return status;
        
        rx_data[i] = (uint8_t)(*hspi->regs->DR);
    }
    
    return SPI_OK;
}

/*
 * Example: Reading from SPI flash with timeout
 */
#define FLASH_CMD_READ  0x03

SPI_Status flash_read_with_timeout(SPI_Handle* hspi, uint32_t address,
                                   uint8_t* buffer, size_t length) {
    uint8_t cmd[4] = {
        FLASH_CMD_READ,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF
    };
    
    SPI_Status status;
    
    // Send command with timeout
    status = spi_transmit(hspi, cmd, sizeof(cmd), 100);
    if (status != SPI_OK) {
        return status;
    }
    
    // Read data with timeout
    for (size_t i = 0; i < length; i++) {
        status = spi_receive_byte(hspi, &buffer[i], SPI_BYTE_TIMEOUT_MS);
        if (status != SPI_OK) {
            return status;
        }
    }
    
    return SPI_OK;
}

// SysTick interrupt handler (called every 1ms)
void SysTick_Handler(void) {
    g_system_tick++;
}