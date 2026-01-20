# Cache Coherency in SPI DMA Transfers

## Overview

Cache coherency is a critical consideration when using Direct Memory Access (DMA) for SPI transfers. The challenge arises because modern CPUs use multi-level caches to speed up memory access, but DMA controllers access main memory directly, bypassing the CPU cache hierarchy. This can lead to data inconsistencies where the CPU cache contains different data than what the DMA controller reads from or writes to main memory.

## The Cache Coherency Problem

When performing SPI transfers with DMA, two main scenarios create coherency issues:

**TX (Transmit) Scenario**: The CPU prepares data in a buffer. This data may only exist in the CPU cache and hasn't been written back to main memory yet. When the DMA controller reads from this buffer to send via SPI, it reads stale data from main memory instead of the updated cache data.

**RX (Receive) Scenario**: The DMA controller writes received SPI data directly to main memory. The CPU cache may still contain old data from previous reads of that memory location. When the CPU reads the buffer, it gets stale cached data instead of the new data the DMA wrote.

## Memory Barriers and Cache Operations

To maintain coherency, you need to perform cache management operations:

- **Cache Clean (Flush)**: Write cached data back to main memory before DMA reads it
- **Cache Invalidate**: Mark cache lines as invalid so the CPU re-reads from main memory after DMA writes
- **Memory Barriers**: Ensure operations complete in the correct order

## Code Examples

### C/C++ Implementation (ARM Cortex-M)

```c
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
```

### C++ Implementation with RAII

```cpp
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
```

### Rust Implementation

```rust
/*
 * SPI DMA Cache Coherency Management - Rust
 * Safe abstractions with zero-cost guarantees
 */

#![no_std]

use core::ops::{Deref, DerefMut};
use core::ptr;
use core::sync::atomic::{compiler_fence, Ordering};

// Cache line size for alignment
const CACHE_LINE_SIZE: usize = 32;

// ARM Cortex-M cache registers
const SCB_DCCMVAC: *mut u32 = 0xE000_EF7C as *mut u32;
const SCB_DCIMVAC: *mut u32 = 0xE000_EF5C as *mut u32;
const SCB_DCCISW: *mut u32 = 0xE000_EF74 as *mut u32;

/// Memory barriers for ARM Cortex-M
#[inline(always)]
fn dsb() {
    unsafe {
        core::arch::asm!("dsb", options(nostack, nomem));
    }
}

#[inline(always)]
fn dmb() {
    unsafe {
        core::arch::asm!("dmb", options(nostack, nomem));
    }
}

/// Cache operation types
#[derive(Debug, Clone, Copy)]
pub enum CacheOp {
    Clean,
    Invalidate,
    CleanInvalidate,
}

/// Perform cache operation on a memory range
pub unsafe fn cache_operation(op: CacheOp, addr: *const u8, size: usize) {
    let start = (addr as usize) & !(CACHE_LINE_SIZE - 1);
    let end = ((addr as usize) + size + CACHE_LINE_SIZE - 1) & !(CACHE_LINE_SIZE - 1);
    
    let cache_reg = match op {
        CacheOp::Clean => SCB_DCCMVAC,
        CacheOp::Invalidate => SCB_DCIMVAC,
        CacheOp::CleanInvalidate => SCB_DCCISW,
    };
    
    let mut ptr = start;
    while ptr < end {
        ptr::write_volatile(cache_reg, ptr as u32);
        ptr += CACHE_LINE_SIZE;
    }
    
    dsb();
    dmb();
}

/// Cache-aligned buffer for DMA operations
#[repr(C, align(32))] // Align to cache line size
pub struct DmaBuffer<const N: usize> {
    data: [u8; N],
}

impl<const N: usize> DmaBuffer<N> {
    /// Create a new DMA buffer
    pub const fn new() -> Self {
        Self { data: [0; N] }
    }
    
    /// Clean cache (write back to memory) before DMA reads
    pub fn clean(&self) {
        unsafe {
            cache_operation(CacheOp::Clean, self.data.as_ptr(), N);
        }
    }
    
    /// Invalidate cache before and after DMA writes
    pub fn invalidate(&self) {
        unsafe {
            cache_operation(CacheOp::Invalidate, self.data.as_ptr(), N);
        }
    }
    
    /// Clean and invalidate cache
    pub fn clean_invalidate(&self) {
        unsafe {
            cache_operation(CacheOp::CleanInvalidate, self.data.as_ptr(), N);
        }
    }
    
    /// Get pointer for DMA
    pub fn as_ptr(&self) -> *const u8 {
        self.data.as_ptr()
    }
    
    /// Get mutable pointer for DMA
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.data.as_mut_ptr()
    }
}

impl<const N: usize> Deref for DmaBuffer<N> {
    type Target = [u8];
    
    fn deref(&self) -> &Self::Target {
        &self.data
    }
}

impl<const N: usize> DerefMut for DmaBuffer<N> {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.data
    }
}

/// RAII guard for cache coherency
pub struct CacheGuard<'a> {
    addr: *const u8,
    size: usize,
    post_op: CacheOp,
    _marker: core::marker::PhantomData<&'a ()>,
}

impl<'a> CacheGuard<'a> {
    /// Create a new cache guard with pre and post operations
    pub fn new(addr: *const u8, size: usize, pre_op: CacheOp, post_op: CacheOp) -> Self {
        unsafe {
            cache_operation(pre_op, addr, size);
        }
        Self {
            addr,
            size,
            post_op,
            _marker: core::marker::PhantomData,
        }
    }
    
    /// Create guard for TX (transmit) operations
    pub fn for_tx(addr: *const u8, size: usize) -> Self {
        Self::new(addr, size, CacheOp::Clean, CacheOp::Clean)
    }
    
    /// Create guard for RX (receive) operations
    pub fn for_rx(addr: *const u8, size: usize) -> Self {
        Self::new(addr, size, CacheOp::Invalidate, CacheOp::Invalidate)
    }
}

impl<'a> Drop for CacheGuard<'a> {
    fn drop(&mut self) {
        unsafe {
            cache_operation(self.post_op, self.addr, self.size);
        }
    }
}

/// DMA-safe SPI peripheral abstraction
pub struct SpiDma<const TX_SIZE: usize, const RX_SIZE: usize> {
    tx_buffer: DmaBuffer<TX_SIZE>,
    rx_buffer: DmaBuffer<RX_SIZE>,
}

impl<const TX_SIZE: usize, const RX_SIZE: usize> SpiDma<TX_SIZE, RX_SIZE> {
    /// Create a new SPI DMA instance
    pub const fn new() -> Self {
        Self {
            tx_buffer: DmaBuffer::new(),
            rx_buffer: DmaBuffer::new(),
        }
    }
    
    /// Transmit data via DMA
    pub fn transmit(&mut self, data: &[u8]) -> Result<(), SpiError> {
        if data.len() > TX_SIZE {
            return Err(SpiError::BufferTooSmall);
        }
        
        // Copy data to DMA buffer
        self.tx_buffer[..data.len()].copy_from_slice(data);
        
        // Clean cache to ensure DMA reads updated data
        self.tx_buffer.clean();
        
        // Compiler fence to prevent reordering
        compiler_fence(Ordering::Release);
        
        // Configure and start DMA (hardware-specific)
        unsafe {
            self.configure_dma_tx(self.tx_buffer.as_ptr(), data.len());
            self.start_dma();
        }
        
        Ok(())
    }
    
    /// Receive data via DMA
    pub fn receive(&mut self, buffer: &mut [u8]) -> Result<(), SpiError> {
        if buffer.len() > RX_SIZE {
            return Err(SpiError::BufferTooSmall);
        }
        
        // Invalidate cache before DMA writes
        self.rx_buffer.invalidate();
        
        // Compiler fence
        compiler_fence(Ordering::Release);
        
        // Configure and start DMA
        unsafe {
            self.configure_dma_rx(self.rx_buffer.as_mut_ptr(), buffer.len());
            self.start_dma();
            self.wait_for_completion();
        }
        
        // Compiler fence
        compiler_fence(Ordering::Acquire);
        
        // Invalidate again to ensure we read from memory
        self.rx_buffer.invalidate();
        
        // Copy received data
        buffer.copy_from_slice(&self.rx_buffer[..buffer.len()]);
        
        Ok(())
    }
    
    /// Full-duplex transfer
    pub fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), SpiError> {
        if tx_data.len() != rx_data.len() {
            return Err(SpiError::LengthMismatch);
        }
        
        let len = tx_data.len();
        if len > TX_SIZE || len > RX_SIZE {
            return Err(SpiError::BufferTooSmall);
        }
        
        // Prepare TX buffer
        self.tx_buffer[..len].copy_from_slice(tx_data);
        self.tx_buffer.clean();
        
        // Prepare RX buffer
        self.rx_buffer.invalidate();
        
        compiler_fence(Ordering::Release);
        
        // Configure and start DMA
        unsafe {
            self.configure_dma_duplex(
                self.tx_buffer.as_ptr(),
                self.rx_buffer.as_mut_ptr(),
                len
            );
            self.start_dma();
            self.wait_for_completion();
        }
        
        compiler_fence(Ordering::Acquire);
        
        // Finalize RX buffer
        self.rx_buffer.invalidate();
        
        // Copy received data
        rx_data.copy_from_slice(&self.rx_buffer[..len]);
        
        Ok(())
    }
    
    // Hardware-specific DMA configuration (would be implemented per platform)
    unsafe fn configure_dma_tx(&self, _ptr: *const u8, _len: usize) {
        // Platform-specific implementation
    }
    
    unsafe fn configure_dma_rx(&self, _ptr: *mut u8, _len: usize) {
        // Platform-specific implementation
    }
    
    unsafe fn configure_dma_duplex(&self, _tx_ptr: *const u8, _rx_ptr: *mut u8, _len: usize) {
        // Platform-specific implementation
    }
    
    unsafe fn start_dma(&self) {
        // Platform-specific implementation
    }
    
    unsafe fn wait_for_completion(&self) {
        // Platform-specific implementation
    }
}

/// SPI error types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiError {
    BufferTooSmall,
    LengthMismatch,
    TransferFailed,
}

/// SPI Flash driver with cache coherency
pub struct SpiFlash<'a, const TX: usize, const RX: usize> {
    spi: &'a mut SpiDma<TX, RX>,
}

impl<'a, const TX: usize, const RX: usize> SpiFlash<'a, TX, RX> {
    pub fn new(spi: &'a mut SpiDma<TX, RX>) -> Self {
        Self { spi }
    }
    
    /// Read from flash memory
    pub fn read(&mut self, address: u32, buffer: &mut [u8]) -> Result<(), SpiError> {
        // Prepare command (aligned for cache)
        #[repr(C, align(32))]
        struct Command {
            cmd: u8,
            addr: [u8; 3],
        }
        
        let command = Command {
            cmd: 0x03, // READ command
            addr: [
                ((address >> 16) & 0xFF) as u8,
                ((address >> 8) & 0xFF) as u8,
                (address & 0xFF) as u8,
            ],
        };
        
        // Send command
        let cmd_bytes = unsafe {
            core::slice::from_raw_parts(
                &command as *const _ as *const u8,
                core::mem::size_of::<Command>()
            )
        };
        self.spi.transmit(cmd_bytes)?;
        
        // Receive data
        self.spi.receive(buffer)?;
        
        Ok(())
    }
    
    /// Write to flash memory
    pub fn write(&mut self, address: u32, data: &[u8]) -> Result<(), SpiError> {
        if data.len() > 256 {
            return Err(SpiError::BufferTooSmall);
        }
        
        // Similar implementation
        Ok(())
    }
}

/// Example usage
pub fn example_usage() {
    // Create SPI DMA instance
    static mut SPI: SpiDma<256, 256> = SpiDma::new();
    
    unsafe {
        // Create flash driver
        let mut flash = SpiFlash::new(&mut SPI);
        
        // Read from flash
        let mut read_buffer = [0u8; 128];
        flash.read(0x1000, &mut read_buffer).ok();
        
        // Write to flash
        let write_data = [0xAA; 64];
        flash.write(0x2000, &write_data).ok();
        
        // Direct SPI transfer
        let tx_data = [0x55; 32];
        let mut rx_data = [0u8; 32];
        SPI.transfer(&tx_data, &mut rx_data).ok();
    }
}
```

## Summary

Cache coherency in SPI DMA transfers is essential for ensuring data consistency between the CPU cache and main memory. The key challenges involve:

**Problem**: DMA controllers bypass CPU caches, creating potential mismatches between cached data and memory contents during transmit and receive operations.

**Solutions**:
1. **Cache Clean (Flush)**: Before DMA reads data for transmission, ensure cached modifications are written back to main memory
2. **Cache Invalidate**: Before DMA writes received data, invalidate cache lines to prevent reading stale cached values. Invalidate again after DMA completes
3. **Memory Barriers**: Use DSB and DMB instructions to ensure operations complete in the correct order
4. **Buffer Alignment**: Align DMA buffers to cache line boundaries to avoid partial cache line issues

**Implementation Approaches**:
- **Manual Management (C)**: Explicit cache clean/invalidate calls with memory barriers
- **RAII Guards (C++)**: Automatic cache management using constructor/destructor pairs for exception safety
- **Zero-Cost Abstractions (Rust)**: Type-safe wrappers with compile-time guarantees and no runtime overhead
- **Non-Cached Memory**: Alternative approach placing DMA buffers in memory regions with caching disabled

**Best Practices**:
- Always align DMA buffers to cache line boundaries
- Clean cache before DMA reads from buffers
- Invalidate cache before and after DMA writes to buffers
- Use memory barriers to enforce operation ordering
- Consider using non-cached memory regions for frequently-used DMA buffers
- Be aware of false sharing between cache lines

Proper cache coherency management prevents subtle bugs, data corruption, and ensures reliable SPI communication in high-performance embedded systems.