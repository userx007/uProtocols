/*
 * SPI DMA Cache Coherency Management - C++
 * Modern C++ approach with RAII and type safety
 */

#include <cstdint>
#include <cstring>
#include <array>
#include <span>
#include <memory>
#include <type_traits>

// Cache line size
inline constexpr size_t CACHE_LINE_SIZE = 32;

// Alignment attribute for cache line boundaries
#define CACHE_ALIGNED alignas(CACHE_LINE_SIZE)

namespace cache {

// ARM Cortex-M cache registers
namespace reg {
    volatile uint32_t& DCCMVAC = *reinterpret_cast<volatile uint32_t*>(0xE000EF7C);
    volatile uint32_t& DCIMVAC = *reinterpret_cast<volatile uint32_t*>(0xE000EF5C);
    volatile uint32_t& DCCISW  = *reinterpret_cast<volatile uint32_t*>(0xE000EF74);
}

// Memory barriers
inline void dsb() { __asm__ volatile ("dsb" ::: "memory"); }
inline void dmb() { __asm__ volatile ("dmb" ::: "memory"); }

// Cache operation types
enum class Operation {
    Clean,      // Write back to memory
    Invalidate, // Mark as invalid
    CleanInvalidate
};

/**
 * Perform cache operation on a memory range
 */
template<Operation Op>
void operate(void* addr, size_t size) {
    const uintptr_t start = reinterpret_cast<uintptr_t>(addr) & ~(CACHE_LINE_SIZE - 1);
    const uintptr_t end = (reinterpret_cast<uintptr_t>(addr) + size + CACHE_LINE_SIZE - 1) 
                          & ~(CACHE_LINE_SIZE - 1);
    
    for (uintptr_t ptr = start; ptr < end; ptr += CACHE_LINE_SIZE) {
        if constexpr (Op == Operation::Clean) {
            reg::DCCMVAC = static_cast<uint32_t>(ptr);
        } else if constexpr (Op == Operation::Invalidate) {
            reg::DCIMVAC = static_cast<uint32_t>(ptr);
        } else if constexpr (Op == Operation::CleanInvalidate) {
            reg::DCCISW = static_cast<uint32_t>(ptr);
        }
    }
    
    dsb();
    dmb();
}

// Convenience functions
inline void clean(void* addr, size_t size) {
    operate<Operation::Clean>(addr, size);
}

inline void invalidate(void* addr, size_t size) {
    operate<Operation::Invalidate>(addr, size);
}

inline void clean_invalidate(void* addr, size_t size) {
    operate<Operation::CleanInvalidate>(addr, size);
}

} // namespace cache

/**
 * RAII guard for cache coherency
 * Automatically manages cache operations for a buffer
 */
template<cache::Operation PreOp, cache::Operation PostOp = cache::Operation::Invalidate>
class CacheGuard {
public:
    CacheGuard(void* addr, size_t size) 
        : addr_(addr), size_(size) {
        cache::operate<PreOp>(addr_, size_);
    }
    
    ~CacheGuard() {
        cache::operate<PostOp>(addr_, size_);
    }
    
    // Non-copyable, non-movable
    CacheGuard(const CacheGuard&) = delete;
    CacheGuard& operator=(const CacheGuard&) = delete;
    
private:
    void* addr_;
    size_t size_;
};

// Type aliases for common use cases
using TxCacheGuard = CacheGuard<cache::Operation::Clean, cache::Operation::Clean>;
using RxCacheGuard = CacheGuard<cache::Operation::Invalidate, cache::Operation::Invalidate>;

/**
 * DMA-safe buffer with automatic cache management
 */
template<size_t N>
class DmaBuffer {
public:
    DmaBuffer() = default;
    
    // Get writable span (for CPU access)
    std::span<uint8_t> writable_span() {
        return std::span(buffer_.data(), buffer_.size());
    }
    
    // Get readable span (for CPU access)
    std::span<const uint8_t> readable_span() const {
        return std::span(buffer_.data(), buffer_.size());
    }
    
    // Prepare buffer for DMA read (TX)
    void prepare_for_dma_read() {
        cache::clean(buffer_.data(), buffer_.size());
    }
    
    // Prepare buffer for DMA write (RX)
    void prepare_for_dma_write() {
        cache::invalidate(buffer_.data(), buffer_.size());
    }
    
    // Finalize after DMA write (ensure CPU reads fresh data)
    void finalize_after_dma_write() {
        cache::invalidate(buffer_.data(), buffer_.size());
    }
    
    // Direct access
    uint8_t* data() { return buffer_.data(); }
    const uint8_t* data() const { return buffer_.data(); }
    size_t size() const { return buffer_.size(); }
    
private:
    CACHE_ALIGNED std::array<uint8_t, N> buffer_{};
};

/**
 * SPI DMA driver with automatic cache management
 */
class SpiDma {
public:
    static constexpr size_t MAX_TRANSFER_SIZE = 256;
    
    // Transmit data via DMA
    bool transmit(std::span<const uint8_t> data) {
        if (data.size() > MAX_TRANSFER_SIZE) {
            return false;
        }
        
        // Copy to DMA buffer
        std::memcpy(tx_buffer_.data(), data.data(), data.size());
        
        // Use RAII guard for cache management
        TxCacheGuard guard(tx_buffer_.data(), data.size());
        
        // Configure and start DMA
        // configure_dma_tx(tx_buffer_.data(), data.size());
        // start_dma();
        
        return true;
    }
    
    // Receive data via DMA
    bool receive(std::span<uint8_t> data) {
        if (data.size() > MAX_TRANSFER_SIZE) {
            return false;
        }
        
        // Use RAII guard for cache management
        RxCacheGuard guard(rx_buffer_.data(), data.size());
        
        // Configure and start DMA
        // configure_dma_rx(rx_buffer_.data(), data.size());
        // start_dma();
        // wait_for_completion();
        
        // Copy from DMA buffer
        std::memcpy(data.data(), rx_buffer_.data(), data.size());
        
        return true;
    }
    
    // Full-duplex transfer
    bool transfer(std::span<const uint8_t> tx_data, std::span<uint8_t> rx_data) {
        if (tx_data.size() != rx_data.size() || 
            tx_data.size() > MAX_TRANSFER_SIZE) {
            return false;
        }
        
        const size_t length = tx_data.size();
        
        // Prepare TX buffer
        std::memcpy(tx_buffer_.data(), tx_data.data(), length);
        tx_buffer_.prepare_for_dma_read();
        
        // Prepare RX buffer
        rx_buffer_.prepare_for_dma_write();
        
        // Configure and start DMA
        // configure_dma_duplex(tx_buffer_.data(), rx_buffer_.data(), length);
        // start_dma();
        // wait_for_completion();
        
        // Finalize RX buffer
        rx_buffer_.finalize_after_dma_write();
        
        // Copy received data
        std::memcpy(rx_data.data(), rx_buffer_.data(), length);
        
        return true;
    }
    
private:
    DmaBuffer<MAX_TRANSFER_SIZE> tx_buffer_;
    DmaBuffer<MAX_TRANSFER_SIZE> rx_buffer_;
};

/**
 * Example: SPI Flash driver with cache coherency
 */
class SpiFlash {
public:
    enum class Command : uint8_t {
        READ = 0x03,
        WRITE = 0x02,
        ERASE_SECTOR = 0x20
    };
    
    explicit SpiFlash(SpiDma& spi) : spi_(spi) {}
    
    bool read(uint32_t address, std::span<uint8_t> buffer) {
        // Prepare command
        CACHE_ALIGNED struct {
            uint8_t cmd;
            uint8_t addr[3];
        } command{
            static_cast<uint8_t>(Command::READ),
            {
                static_cast<uint8_t>((address >> 16) & 0xFF),
                static_cast<uint8_t>((address >> 8) & 0xFF),
                static_cast<uint8_t>(address & 0xFF)
            }
        };
        
        // Send command
        if (!spi_.transmit(std::span(reinterpret_cast<uint8_t*>(&command), 
                                      sizeof(command)))) {
            return false;
        }
        
        // Receive data
        return spi_.receive(buffer);
    }
    
    bool write(uint32_t address, std::span<const uint8_t> data) {
        if (data.size() > 256) { // Page size limit
            return false;
        }
        
        // Similar implementation with write command
        return true;
    }
    
private:
    SpiDma& spi_;
};

/**
 * Usage example
 */
void example_usage() {
    SpiDma spi;
    SpiFlash flash(spi);
    
    // Read from flash
    std::array<uint8_t, 128> read_buffer{};
    if (flash.read(0x1000, std::span(read_buffer))) {
        // Process data
    }
    
    // Write to flash
    std::array<uint8_t, 64> write_data{/* data */};
    flash.write(0x2000, std::span(write_data));
    
    // Direct SPI transfer
    std::array<uint8_t, 32> tx_data{/* data */};
    std::array<uint8_t, 32> rx_data{};
    spi.transfer(std::span(tx_data), std::span(rx_data));
}