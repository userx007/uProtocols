/*
 * SPI Master Implementation - Bare Metal C
 * Demonstrates manual control of SPI signal lines
 * Target: Generic microcontroller (AVR/ARM style)
 */

#include <stdint.h>
#include <stdbool.h>

// Hardware register definitions (example addresses)
#define SPI_BASE_ADDR    0x40013000
#define SPI_CR1          (*(volatile uint32_t*)(SPI_BASE_ADDR + 0x00))
#define SPI_CR2          (*(volatile uint32_t*)(SPI_BASE_ADDR + 0x04))
#define SPI_SR           (*(volatile uint32_t*)(SPI_BASE_ADDR + 0x08))
#define SPI_DR           (*(volatile uint32_t*)(SPI_BASE_ADDR + 0x0C))

// SPI Control Register bits
#define SPI_CR1_CPHA     (1 << 0)  // Clock phase
#define SPI_CR1_CPOL     (1 << 1)  // Clock polarity
#define SPI_CR1_MSTR     (1 << 2)  // Master mode
#define SPI_CR1_BR_MASK  (7 << 3)  // Baud rate control
#define SPI_CR1_SPE      (1 << 6)  // SPI enable
#define SPI_CR1_LSBFIRST (1 << 7)  // Frame format
#define SPI_CR1_SSI      (1 << 8)  // Internal slave select
#define SPI_CR1_SSM      (1 << 9)  // Software slave management

// SPI Status Register bits
#define SPI_SR_RXNE      (1 << 0)  // Receive buffer not empty
#define SPI_SR_TXE       (1 << 1)  // Transmit buffer empty
#define SPI_SR_BSY       (1 << 7)  // Busy flag

// GPIO definitions for CS line
#define GPIO_CS_PORT     GPIOA
#define GPIO_CS_PIN      4

// SPI Mode enumeration
typedef enum {
    SPI_MODE_0 = 0,  // CPOL=0, CPHA=0
    SPI_MODE_1 = 1,  // CPOL=0, CPHA=1
    SPI_MODE_2 = 2,  // CPOL=1, CPHA=0
    SPI_MODE_3 = 3   // CPOL=1, CPHA=1
} spi_mode_t;

// SPI Clock divider
typedef enum {
    SPI_CLOCK_DIV2   = 0,
    SPI_CLOCK_DIV4   = 1,
    SPI_CLOCK_DIV8   = 2,
    SPI_CLOCK_DIV16  = 3,
    SPI_CLOCK_DIV32  = 4,
    SPI_CLOCK_DIV64  = 5,
    SPI_CLOCK_DIV128 = 6,
    SPI_CLOCK_DIV256 = 7
} spi_clock_div_t;

// SPI Configuration structure
typedef struct {
    spi_mode_t mode;
    spi_clock_div_t clock_div;
    bool lsb_first;
} spi_config_t;

/**
 * Initialize SPI as master with specified configuration
 */
void spi_init(const spi_config_t *config) {
    uint32_t cr1_value = 0;
    
    // Configure as master
    cr1_value |= SPI_CR1_MSTR;
    
    // Set clock polarity and phase based on mode
    if (config->mode & 0x01) {
        cr1_value |= SPI_CR1_CPHA;
    }
    if (config->mode & 0x02) {
        cr1_value |= SPI_CR1_CPOL;
    }
    
    // Set clock divider
    cr1_value |= ((config->clock_div & 0x07) << 3);
    
    // Set bit order
    if (config->lsb_first) {
        cr1_value |= SPI_CR1_LSBFIRST;
    }
    
    // Software slave management
    cr1_value |= SPI_CR1_SSM | SPI_CR1_SSI;
    
    // Write configuration
    SPI_CR1 = cr1_value;
    
    // Enable SPI
    SPI_CR1 |= SPI_CR1_SPE;
}

/**
 * CS/SS line control - Assert (active LOW)
 */
static inline void spi_cs_select(void) {
    // Pull CS line LOW to select slave
    GPIO_CS_PORT->BSRR = (1 << (GPIO_CS_PIN + 16));
}

/**
 * CS/SS line control - Deassert
 */
static inline void spi_cs_deselect(void) {
    // Pull CS line HIGH to deselect slave
    GPIO_CS_PORT->BSRR = (1 << GPIO_CS_PIN);
}

/**
 * Transfer a single byte (full-duplex)
 * Demonstrates MOSI, MISO, and SCK interaction
 */
uint8_t spi_transfer_byte(uint8_t data_out) {
    // Wait for transmit buffer to be empty (TXE flag)
    while (!(SPI_SR & SPI_SR_TXE));
    
    // Write data to DR register (shifts out on MOSI line)
    // This also triggers the SCK clock generation
    SPI_DR = data_out;
    
    // Wait for receive buffer to have data (RXNE flag)
    // Data is simultaneously shifted in on MISO line
    while (!(SPI_SR & SPI_SR_RXNE));
    
    // Read received data from DR register
    return (uint8_t)SPI_DR;
}

/**
 * Transfer multiple bytes
 * Shows continuous clock and data transfer on signal lines
 */
void spi_transfer(const uint8_t *tx_data, uint8_t *rx_data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        uint8_t tx_byte = tx_data ? tx_data[i] : 0xFF;
        uint8_t rx_byte = spi_transfer_byte(tx_byte);
        
        if (rx_data) {
            rx_data[i] = rx_byte;
        }
    }
}

/**
 * Wait for SPI to not be busy
 * Important: Must wait for BSY flag before deselecting CS
 */
void spi_wait_not_busy(void) {
    while (SPI_SR & SPI_SR_BSY);
}

/**
 * Example: Read from an SPI device register
 */
uint8_t spi_read_register(uint8_t reg_addr) {
    uint8_t value;
    
    // 1. Assert CS line (LOW) - begins transaction
    spi_cs_select();
    
    // 2. Send read command and register address on MOSI
    //    Clock pulses on SCK synchronize the transfer
    spi_transfer_byte(0x80 | reg_addr);  // 0x80 = read bit
    
    // 3. Read data back on MISO
    //    Send dummy byte on MOSI to generate clock
    value = spi_transfer_byte(0xFF);
    
    // 4. Wait for transaction to complete
    spi_wait_not_busy();
    
    // 5. Deassert CS line (HIGH) - ends transaction
    spi_cs_deselect();
    
    return value;
}

/**
 * Example: Write to an SPI device register
 */
void spi_write_register(uint8_t reg_addr, uint8_t value) {
    spi_cs_select();
    
    // Send write command and address on MOSI
    spi_transfer_byte(reg_addr & 0x7F);  // Clear read bit
    
    // Send data on MOSI
    spi_transfer_byte(value);
    
    spi_wait_not_busy();
    spi_cs_deselect();
}

/**
 * Example usage with timing considerations
 */
void example_usage(void) {
    // Configure SPI Mode 0, 1 MHz clock (assuming 16 MHz system clock)
    spi_config_t config = {
        .mode = SPI_MODE_0,        // CPOL=0, CPHA=0
        .clock_div = SPI_CLOCK_DIV16,  // 16MHz / 16 = 1MHz SCK
        .lsb_first = false         // MSB first
    };
    
    spi_init(&config);
    
    // Read device ID
    uint8_t device_id = spi_read_register(0x00);
    
    // Write configuration
    spi_write_register(0x01, 0xA5);
    
    // Multi-byte transfer example
    uint8_t tx_buffer[] = {0x02, 0x11, 0x22, 0x33};
    uint8_t rx_buffer[4];
    
    spi_cs_select();
    spi_transfer(tx_buffer, rx_buffer, 4);
    spi_wait_not_busy();
    spi_cs_deselect();
}

/*
 * Signal Line Timing Notes:
 * 
 * CS (Chip Select):
 *   - Must be LOW before first clock edge
 *   - Must stay LOW during entire transaction
 *   - Must go HIGH after transaction completes
 *   - Setup time: typically 50-100ns before first clock
 *   - Hold time: typically 50-100ns after last clock
 *
 * SCK (Clock):
 *   - Generated by master during data transfer
 *   - Frequency determined by clock divider
 *   - Duty cycle: typically 50%
 *   - Max frequency limited by both master and slave
 *
 * MOSI (Master Out):
 *   - Data valid on appropriate clock edge per CPHA
 *   - Setup time: data stable before sampling edge
 *   - Hold time: data stable after sampling edge
 *   - Tri-stated when master not transmitting (multi-master)
 *
 * MISO (Master In):
 *   - Slave drives line when CS is active
 *   - Master samples on appropriate clock edge
 *   - High-Z when slave not selected
 *   - May need pull-up/pull-down resistor
 */