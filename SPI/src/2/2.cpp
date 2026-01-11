/*
 * SPI Signal Lines - Object-Oriented C++ Implementation
 * Demonstrates signal line control with modern C++ features
 */

#include <cstdint>
#include <array>
#include <vector>
#include <chrono>
#include <stdexcept>

/**
 * SPI Mode enumeration
 * Combines CPOL and CPHA settings
 */
enum class SPIMode : uint8_t {
    MODE_0 = 0,  // CPOL=0, CPHA=0: Sample on rising, shift on falling
    MODE_1 = 1,  // CPOL=0, CPHA=1: Sample on falling, shift on rising
    MODE_2 = 2,  // CPOL=1, CPHA=0: Sample on falling, shift on rising
    MODE_3 = 3   // CPOL=1, CPHA=1: Sample on rising, shift on falling
};

/**
 * SPI Clock speed enumeration
 */
enum class SPIClockSpeed : uint32_t {
    SPEED_125KHZ  = 125000,
    SPEED_250KHZ  = 250000,
    SPEED_500KHZ  = 500000,
    SPEED_1MHZ    = 1000000,
    SPEED_2MHZ    = 2000000,
    SPEED_4MHZ    = 4000000,
    SPEED_8MHZ    = 8000000,
    SPEED_10MHZ   = 10000000,
    SPEED_20MHZ   = 20000000
};

/**
 * SPI Bit Order
 */
enum class SPIBitOrder : uint8_t {
    MSB_FIRST = 0,  // Most Significant Bit first (standard)
    LSB_FIRST = 1   // Least Significant Bit first (uncommon)
};

/**
 * SPI Configuration structure
 */
struct SPIConfig {
    SPIMode mode;
    SPIClockSpeed clock_speed;
    SPIBitOrder bit_order;
    uint8_t data_bits;  // Typically 8, some devices support 16
    
    // Timing parameters (in nanoseconds)
    uint32_t cs_setup_time_ns;     // CS low to first clock edge
    uint32_t cs_hold_time_ns;      // Last clock edge to CS high
    uint32_t inter_transfer_delay_ns;  // Delay between bytes
    
    SPIConfig() 
        : mode(SPIMode::MODE_0)
        , clock_speed(SPIClockSpeed::SPEED_1MHZ)
        , bit_order(SPIBitOrder::MSB_FIRST)
        , data_bits(8)
        , cs_setup_time_ns(100)
        , cs_hold_time_ns(100)
        , inter_transfer_delay_ns(0) {}
};

/**
 * GPIO Pin abstraction for CS line
 */
class GPIOPin {
private:
    volatile uint32_t* port_;
    uint8_t pin_;
    
public:
    GPIOPin(volatile uint32_t* port, uint8_t pin) 
        : port_(port), pin_(pin) {}
    
    void set_high() {
        *port_ |= (1 << pin_);
    }
    
    void set_low() {
        *port_ &= ~(1 << pin_);
    }
    
    bool read() const {
        return (*port_ & (1 << pin_)) != 0;
    }
};

/**
 * SPI Signal Line State
 * Useful for debugging and visualization
 */
struct SPISignalState {
    bool cs;    // Chip Select state (false = active LOW)
    bool sck;   // Serial Clock state
    bool mosi;  // Master Out state
    bool miso;  // Master In state
    uint8_t bit_position;  // Current bit being transferred (0-7)
    
    void print() const {
        printf("CS:%d SCK:%d MOSI:%d MISO:%d Bit:%d\n", 
               cs, sck, mosi, miso, bit_position);
    }
};

/**
 * SPI Master Controller Class
 * Encapsulates all four signal lines and their interactions
 */
class SPIMaster {
private:
    SPIConfig config_;
    GPIOPin cs_pin_;
    volatile uint32_t* spi_base_;
    
    // Hardware register offsets
    static constexpr uint32_t CR1_OFFSET = 0x00;
    static constexpr uint32_t SR_OFFSET  = 0x08;
    static constexpr uint32_t DR_OFFSET  = 0x0C;
    
    // Status flags
    static constexpr uint32_t SR_TXE  = (1 << 1);  // TX empty
    static constexpr uint32_t SR_RXNE = (1 << 0);  // RX not empty
    static constexpr uint32_t SR_BSY  = (1 << 7);  // Busy
    
    /**
     * Control CS line - manages chip select signal
     */
    void assert_cs() {
        cs_pin_.set_low();  // Active LOW
        delay_ns(config_.cs_setup_time_ns);
    }
    
    void deassert_cs() {
        delay_ns(config_.cs_hold_time_ns);
        cs_pin_.set_high();  // Inactive HIGH
    }
    
    /**
     * Timing delay helper
     */
    void delay_ns(uint32_t nanoseconds) {
        // Implementation depends on platform
        // Could use busy-wait, timer, or hardware delay
        auto start = std::chrono::high_resolution_clock::now();
        while (true) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                now - start).count();
            if (elapsed >= nanoseconds) break;
        }
    }
    
    /**
     * Wait for hardware to be ready
     */
    void wait_tx_empty() {
        volatile uint32_t* sr = spi_base_ + SR_OFFSET;
        while (!(*sr & SR_TXE));
    }
    
    void wait_rx_not_empty() {
        volatile uint32_t* sr = spi_base_ + SR_OFFSET;
        while (!(*sr & SR_RXNE));
    }
    
    void wait_not_busy() {
        volatile uint32_t* sr = spi_base_ + SR_OFFSET;
        while (*sr & SR_BSY);
    }
    
public:
    SPIMaster(volatile uint32_t* spi_base, GPIOPin cs_pin, 
              const SPIConfig& config)
        : spi_base_(spi_base)
        , cs_pin_(cs_pin)
        , config_(config) {
        initialize();
    }
    
    /**
     * Initialize SPI hardware
     * Configures all signal lines and timing
     */
    void initialize() {
        volatile uint32_t* cr1 = spi_base_ + CR1_OFFSET;
        uint32_t config_val = 0;
        
        // Master mode
        config_val |= (1 << 2);  // MSTR bit
        
        // Clock polarity (CPOL) and phase (CPHA)
        uint8_t mode_val = static_cast<uint8_t>(config_.mode);
        if (mode_val & 0x01) config_val |= (1 << 0);  // CPHA
        if (mode_val & 0x02) config_val |= (1 << 1);  // CPOL
        
        // Bit order
        if (config_.bit_order == SPIBitOrder::LSB_FIRST) {
            config_val |= (1 << 7);  // LSBFIRST
        }
        
        // Calculate and set baud rate divider for SCK
        uint32_t sys_clock = 16000000;  // Example: 16 MHz
        uint32_t target_freq = static_cast<uint32_t>(config_.clock_speed);
        uint8_t divider = 0;
        uint32_t test_freq = sys_clock;
        
        while (test_freq > target_freq && divider < 7) {
            test_freq /= 2;
            divider++;
        }
        config_val |= ((divider & 0x07) << 3);  // BR[2:0]
        
        // Software slave management
        config_val |= (1 << 9) | (1 << 8);  // SSM and SSI
        
        *cr1 = config_val;
        *cr1 |= (1 << 6);  // Enable SPI (SPE bit)
        
        // Initialize CS high (inactive)
        deassert_cs();
    }
    
    /**
     * Transfer single byte (full-duplex)
     * This is where all signal line magic happens:
     * - SCK generates clock pulses
     * - MOSI shifts out tx_data
     * - MISO shifts in return data
     */
    uint8_t transfer_byte(uint8_t tx_data) {
        volatile uint32_t* dr = spi_base_ + DR_OFFSET;
        
        // Wait for TX buffer empty (TXE flag)
        wait_tx_empty();
        
        // Write to data register
        // This triggers:
        // 1. MOSI line starts outputting data bits
        // 2. SCK line starts generating clock pulses
        // 3. MISO line is sampled on clock edges
        *dr = tx_data;
        
        // Wait for RX buffer not empty (RXNE flag)
        wait_rx_not_empty();
        
        // Read received data from MISO line
        uint8_t rx_data = static_cast<uint8_t>(*dr);
        
        return rx_data;
    }
    
    /**
     * Transfer multiple bytes with CS management
     */
    std::vector<uint8_t> transfer(const std::vector<uint8_t>& tx_data) {
        std::vector<uint8_t> rx_data;
        rx_data.reserve(tx_data.size());
        
        // Assert CS line to select slave
        assert_cs();
        
        // Transfer all bytes
        // SCK generates continuous clock during this period
        // MOSI and MISO are active throughout
        for (uint8_t byte : tx_data) {
            rx_data.push_back(transfer_byte(byte));
            
            if (config_.inter_transfer_delay_ns > 0) {
                delay_ns(config_.inter_transfer_delay_ns);
            }
        }
        
        // Wait for final byte to complete
        wait_not_busy();
        
        // Deassert CS line
        deassert_cs();
        
        return rx_data;
    }
    
    /**
     * Read-only operation (common pattern)
     * MOSI sends dummy bytes to generate clock
     */
    std::vector<uint8_t> read(size_t num_bytes) {
        std::vector<uint8_t> dummy_data(num_bytes, 0xFF);
        return transfer(dummy_data);
    }
    
    /**
     * Write-only operation
     * MISO data is ignored
     */
    void write(const std::vector<uint8_t>& data) {
        transfer(data);  // Ignore return value
    }
    
    /**
     * Transaction helper - wraps CS management
     */
    template<typename Func>
    auto transaction(Func&& func) -> decltype(func()) {
        assert_cs();
        auto result = func();
        wait_not_busy();
        deassert_cs();
        return result;
    }
};

/**
 * Example: SPI Flash Memory Device
 * Demonstrates real-world signal line usage
 */
class SPIFlash {
private:
    SPIMaster& spi_;
    
    // Flash commands (sent on MOSI line)
    static constexpr uint8_t CMD_READ_ID       = 0x9F;
    static constexpr uint8_t CMD_READ_DATA     = 0x03;
    static constexpr uint8_t CMD_WRITE_ENABLE  = 0x06;
    static constexpr uint8_t CMD_PAGE_PROGRAM  = 0x02;
    static constexpr uint8_t CMD_READ_STATUS   = 0x05;
    
public:
    explicit SPIFlash(SPIMaster& spi) : spi_(spi) {}
    
    /**
     * Read manufacturer ID
     * Shows typical command-response pattern on SPI lines
     */
    std::array<uint8_t, 3> read_id() {
        return spi_.transaction([this]() {
            // Send command on MOSI
            spi_.transfer_byte(CMD_READ_ID);
            
            // Read 3 bytes from MISO
            std::array<uint8_t, 3> id;
            id[0] = spi_.transfer_byte(0xFF);  // Manufacturer
            id[1] = spi_.transfer_byte(0xFF);  // Device ID high
            id[2] = spi_.transfer_byte(0xFF);  // Device ID low
            
            return id;
        });
    }
    
    /**
     * Read data from flash
     * Demonstrates address phase and data phase
     */
    std::vector<uint8_t> read(uint32_t address, size_t length) {
        return spi_.transaction([this, address, length]() {
            // Command phase (MOSI active)
            spi_.transfer_byte(CMD_READ_DATA);
            
            // Address phase (MOSI active, 24-bit address)
            spi_.transfer_byte((address >> 16) & 0xFF);
            spi_.transfer_byte((address >> 8) & 0xFF);
            spi_.transfer_byte(address & 0xFF);
            
            // Data phase (MISO active)
            std::vector<uint8_t> data;
            data.reserve(length);
            for (size_t i = 0; i < length; i++) {
                data.push_back(spi_.transfer_byte(0xFF));
            }
            
            return data;
        });
    }
    
    /**
     * Write enable command
     * Simple command-only transaction
     */
    void write_enable() {
        spi_.transaction([this]() {
            spi_.transfer_byte(CMD_WRITE_ENABLE);
            return 0;
        });
    }
};

/**
 * Example usage demonstrating signal line interactions
 */
void example_spi_usage() {
    // Configure SPI with specific signal timing
    SPIConfig config;
    config.mode = SPIMode::MODE_0;  // CPOL=0, CPHA=0
    config.clock_speed = SPIClockSpeed::SPEED_1MHZ;  // 1 MHz SCK
    config.bit_order = SPIBitOrder::MSB_FIRST;
    config.cs_setup_time_ns = 100;  // 100ns CS setup
    config.cs_hold_time_ns = 100;   // 100ns CS hold
    
    // Create SPI master
    volatile uint32_t* spi1_base = reinterpret_cast<volatile uint32_t*>(0x40013000);
    GPIOPin cs_pin(reinterpret_cast<volatile uint32_t*>(0x40010800), 4);
    
    SPIMaster spi(spi1_base, cs_pin, config);
    
    // Use with flash device
    SPIFlash flash(spi);
    
    // Read device ID
    // Signal sequence: CS↓ → CMD on MOSI → 3 bytes on MISO → CS↑
    auto id = flash.read_id();
    
    // Read 256 bytes
    // Signal sequence: CS↓ → CMD + ADDR on MOSI → 256 bytes on MISO → CS↑
    auto data = flash.read(0x1000, 256);
}