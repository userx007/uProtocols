# I2C Resource Management

Resource management in I2C drivers is critical for embedded systems where memory and CPU cycles are limited. Proper resource management ensures efficient operation, prevents memory leaks, and optimizes power consumption.

## Core Concepts

### 1. Memory Management
- **Static vs Dynamic Allocation**: Choose based on predictability and resource constraints
- **Buffer Pooling**: Reuse buffers to avoid fragmentation
- **DMA Buffers**: Align and manage DMA-capable memory regions
- **Stack vs Heap**: Minimize heap usage in embedded contexts

### 2. CPU Utilization
- **Interrupt-Driven I/O**: Reduce polling overhead
- **DMA Transfers**: Offload CPU during bulk data transfers
- **Clock Management**: Disable I2C peripherals when idle
- **Efficient Algorithms**: Optimize critical paths

### 3. Power Management
- **Clock Gating**: Disable peripheral clocks when unused
- **Sleep Modes**: Support low-power states
- **Bus Idle Detection**: Power down during inactivity

## C/C++ Implementation Examples

### Example 1: Static Buffer Pool Management

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Configuration
#define I2C_BUFFER_POOL_SIZE 4
#define I2C_BUFFER_SIZE 64

// Buffer pool structure
typedef struct {
    uint8_t data[I2C_BUFFER_SIZE];
    bool in_use;
} i2c_buffer_t;

typedef struct {
    i2c_buffer_t buffers[I2C_BUFFER_POOL_SIZE];
    uint32_t allocation_count;
    uint32_t allocation_failures;
} i2c_buffer_pool_t;

// Global buffer pool
static i2c_buffer_pool_t g_buffer_pool = {0};

// Initialize buffer pool
void i2c_buffer_pool_init(void) {
    memset(&g_buffer_pool, 0, sizeof(i2c_buffer_pool_t));
}

// Allocate buffer from pool
uint8_t* i2c_buffer_alloc(void) {
    for (int i = 0; i < I2C_BUFFER_POOL_SIZE; i++) {
        if (!g_buffer_pool.buffers[i].in_use) {
            g_buffer_pool.buffers[i].in_use = true;
            g_buffer_pool.allocation_count++;
            return g_buffer_pool.buffers[i].data;
        }
    }
    g_buffer_pool.allocation_failures++;
    return NULL; // Pool exhausted
}

// Free buffer back to pool
void i2c_buffer_free(uint8_t* buffer) {
    if (!buffer) return;
    
    for (int i = 0; i < I2C_BUFFER_POOL_SIZE; i++) {
        if (g_buffer_pool.buffers[i].data == buffer) {
            g_buffer_pool.buffers[i].in_use = false;
            memset(buffer, 0, I2C_BUFFER_SIZE); // Clear for security
            return;
        }
    }
}

// Get pool statistics
void i2c_buffer_pool_stats(uint32_t* total, uint32_t* used, uint32_t* failures) {
    *total = I2C_BUFFER_POOL_SIZE;
    *used = 0;
    for (int i = 0; i < I2C_BUFFER_POOL_SIZE; i++) {
        if (g_buffer_pool.buffers[i].in_use) (*used)++;
    }
    *failures = g_buffer_pool.allocation_failures;
}
```

### Example 2: Interrupt-Driven I2C with Resource Management

```c
#include <stdint.h>
#include <stdbool.h>

// I2C transaction states
typedef enum {
    I2C_STATE_IDLE,
    I2C_STATE_TX_IN_PROGRESS,
    I2C_STATE_RX_IN_PROGRESS,
    I2C_STATE_COMPLETE,
    I2C_STATE_ERROR
} i2c_state_t;

// I2C transaction descriptor
typedef struct {
    uint8_t slave_address;
    uint8_t* tx_buffer;
    uint16_t tx_length;
    uint8_t* rx_buffer;
    uint16_t rx_length;
    uint16_t tx_index;
    uint16_t rx_index;
    i2c_state_t state;
    void (*completion_callback)(bool success);
} i2c_transaction_t;

// I2C driver context
typedef struct {
    volatile uint32_t* i2c_base_reg; // Hardware registers
    i2c_transaction_t* current_transaction;
    uint32_t interrupt_count;
    uint32_t error_count;
    bool peripheral_enabled;
} i2c_driver_ctx_t;

static i2c_driver_ctx_t g_i2c_ctx = {0};

// Enable I2C peripheral with clock management
void i2c_peripheral_enable(void) {
    if (!g_i2c_ctx.peripheral_enabled) {
        // Enable peripheral clock (platform-specific)
        // RCC->APB1ENR |= RCC_APB1ENR_I2C1EN; // Example for STM32
        
        // Enable I2C peripheral
        // I2C1->CR1 |= I2C_CR1_PE;
        
        g_i2c_ctx.peripheral_enabled = true;
    }
}

// Disable I2C peripheral for power saving
void i2c_peripheral_disable(void) {
    if (g_i2c_ctx.peripheral_enabled && 
        g_i2c_ctx.current_transaction == NULL) {
        // Disable I2C peripheral
        // I2C1->CR1 &= ~I2C_CR1_PE;
        
        // Disable peripheral clock
        // RCC->APB1ENR &= ~RCC_APB1ENR_I2C1EN;
        
        g_i2c_ctx.peripheral_enabled = false;
    }
}

// Start non-blocking I2C transaction
bool i2c_transfer_async(i2c_transaction_t* transaction) {
    if (g_i2c_ctx.current_transaction != NULL) {
        return false; // Busy
    }
    
    // Enable peripheral if needed
    i2c_peripheral_enable();
    
    // Initialize transaction
    transaction->tx_index = 0;
    transaction->rx_index = 0;
    transaction->state = I2C_STATE_TX_IN_PROGRESS;
    g_i2c_ctx.current_transaction = transaction;
    
    // Generate START condition
    // I2C1->CR1 |= I2C_CR1_START;
    
    // Enable interrupts
    // I2C1->CR2 |= I2C_CR2_ITEVTEN | I2C_CR2_ITERREN;
    
    return true;
}

// I2C interrupt handler - efficient state machine
void I2C1_IRQHandler(void) {
    g_i2c_ctx.interrupt_count++;
    i2c_transaction_t* txn = g_i2c_ctx.current_transaction;
    
    if (!txn) return;
    
    // Read status registers once (minimize register access)
    // uint32_t sr1 = I2C1->SR1;
    // uint32_t sr2 = I2C1->SR2;
    uint32_t sr1 = 0, sr2 = 0; // Placeholder
    
    // Error handling
    if (sr1 & 0xFF00) { // Error flags
        txn->state = I2C_STATE_ERROR;
        g_i2c_ctx.error_count++;
        goto transaction_complete;
    }
    
    // State machine
    switch (txn->state) {
        case I2C_STATE_TX_IN_PROGRESS:
            if (txn->tx_index < txn->tx_length) {
                // I2C1->DR = txn->tx_buffer[txn->tx_index++];
            } else {
                if (txn->rx_length > 0) {
                    // Generate repeated START for read
                    txn->state = I2C_STATE_RX_IN_PROGRESS;
                    // I2C1->CR1 |= I2C_CR1_START;
                } else {
                    txn->state = I2C_STATE_COMPLETE;
                    goto transaction_complete;
                }
            }
            break;
            
        case I2C_STATE_RX_IN_PROGRESS:
            if (txn->rx_index < txn->rx_length) {
                // txn->rx_buffer[txn->rx_index++] = I2C1->DR;
                
                // Handle last byte (NACK)
                if (txn->rx_index == txn->rx_length - 1) {
                    // I2C1->CR1 &= ~I2C_CR1_ACK;
                }
            }
            
            if (txn->rx_index >= txn->rx_length) {
                txn->state = I2C_STATE_COMPLETE;
                goto transaction_complete;
            }
            break;
            
        default:
            break;
    }
    return;

transaction_complete:
    // Generate STOP condition
    // I2C1->CR1 |= I2C_CR1_STOP;
    
    // Disable interrupts
    // I2C1->CR2 &= ~(I2C_CR2_ITEVTEN | I2C_CR2_ITERREN);
    
    // Invoke callback
    bool success = (txn->state == I2C_STATE_COMPLETE);
    if (txn->completion_callback) {
        txn->completion_callback(success);
    }
    
    // Release transaction
    g_i2c_ctx.current_transaction = NULL;
    
    // Consider disabling peripheral after timeout
    // This would be done in a timer callback, not in ISR
}
```

### Example 3: DMA-Based Transfer for CPU Efficiency

```c
#include <stdint.h>
#include <stdbool.h>

// DMA-capable buffer (must be properly aligned)
#define DMA_BUFFER_ALIGN 4
typedef struct {
    uint8_t data[256] __attribute__((aligned(DMA_BUFFER_ALIGN)));
    volatile bool transfer_complete;
} i2c_dma_buffer_t;

static i2c_dma_buffer_t g_dma_tx_buffer = {0};
static i2c_dma_buffer_t g_dma_rx_buffer = {0};

// Configure DMA for I2C TX
void i2c_dma_tx_config(uint8_t* data, uint16_t length) {
    // Copy data to DMA buffer
    for (uint16_t i = 0; i < length && i < sizeof(g_dma_tx_buffer.data); i++) {
        g_dma_tx_buffer.data[i] = data[i];
    }
    
    // Configure DMA channel (platform-specific example)
    // DMA1_Channel6->CPAR = (uint32_t)&(I2C1->DR);
    // DMA1_Channel6->CMAR = (uint32_t)g_dma_tx_buffer.data;
    // DMA1_Channel6->CNDTR = length;
    // DMA1_Channel6->CCR = DMA_CCR_MINC | DMA_CCR_DIR | DMA_CCR_TCIE;
    
    g_dma_tx_buffer.transfer_complete = false;
}

// Configure DMA for I2C RX
void i2c_dma_rx_config(uint16_t length) {
    // Configure DMA channel
    // DMA1_Channel7->CPAR = (uint32_t)&(I2C1->DR);
    // DMA1_Channel7->CMAR = (uint32_t)g_dma_rx_buffer.data;
    // DMA1_Channel7->CNDTR = length;
    // DMA1_Channel7->CCR = DMA_CCR_MINC | DMA_CCR_TCIE;
    
    g_dma_rx_buffer.transfer_complete = false;
}

// Start DMA transfer
void i2c_dma_transfer_start(void) {
    // Enable I2C DMA requests
    // I2C1->CR2 |= I2C_CR2_DMAEN;
    
    // Enable DMA channel
    // DMA1_Channel6->CCR |= DMA_CCR_EN;
}

// DMA interrupt handler
void DMA1_Channel6_IRQHandler(void) {
    // Check transfer complete flag
    // if (DMA1->ISR & DMA_ISR_TCIF6) {
        // Clear flag
        // DMA1->IFCR = DMA_IFCR_CTCIF6;
        
        g_dma_tx_buffer.transfer_complete = true;
        
        // Disable DMA
        // DMA1_Channel6->CCR &= ~DMA_CCR_EN;
        // I2C1->CR2 &= ~I2C_CR2_DMAEN;
    // }
}

// Get DMA statistics
typedef struct {
    uint32_t bytes_transferred;
    uint32_t transfers_completed;
    uint32_t cpu_cycles_saved; // Estimated
} i2c_dma_stats_t;

void i2c_dma_get_stats(i2c_dma_stats_t* stats) {
    // Implementation would track these metrics
    stats->bytes_transferred = 0;
    stats->transfers_completed = 0;
    // Rough estimate: each byte via DMA saves ~50 CPU cycles
    stats->cpu_cycles_saved = stats->bytes_transferred * 50;
}
```

### Example 4: C++ RAII Resource Management

```cpp
#include <cstdint>
#include <memory>
#include <functional>

class I2CBuffer {
private:
    uint8_t* buffer_;
    size_t size_;
    bool owned_;

public:
    // Constructor with allocation
    explicit I2CBuffer(size_t size) 
        : buffer_(new uint8_t[size]), size_(size), owned_(true) {
        std::memset(buffer_, 0, size_);
    }
    
    // Constructor with existing buffer (non-owning)
    I2CBuffer(uint8_t* buffer, size_t size)
        : buffer_(buffer), size_(size), owned_(false) {}
    
    // Destructor - automatic cleanup
    ~I2CBuffer() {
        if (owned_ && buffer_) {
            delete[] buffer_;
        }
    }
    
    // Move semantics for efficient transfer
    I2CBuffer(I2CBuffer&& other) noexcept
        : buffer_(other.buffer_), size_(other.size_), owned_(other.owned_) {
        other.buffer_ = nullptr;
        other.owned_ = false;
    }
    
    I2CBuffer& operator=(I2CBuffer&& other) noexcept {
        if (this != &other) {
            if (owned_ && buffer_) {
                delete[] buffer_;
            }
            buffer_ = other.buffer_;
            size_ = other.size_;
            owned_ = other.owned_;
            other.buffer_ = nullptr;
            other.owned_ = false;
        }
        return *this;
    }
    
    // Delete copy operations
    I2CBuffer(const I2CBuffer&) = delete;
    I2CBuffer& operator=(const I2CBuffer&) = delete;
    
    uint8_t* data() { return buffer_; }
    const uint8_t* data() const { return buffer_; }
    size_t size() const { return size_; }
};

class I2CTransaction {
private:
    I2CBuffer tx_buffer_;
    I2CBuffer rx_buffer_;
    uint8_t slave_addr_;
    std::function<void(bool)> callback_;
    
public:
    I2CTransaction(uint8_t addr, size_t tx_size, size_t rx_size)
        : tx_buffer_(tx_size), rx_buffer_(rx_size), slave_addr_(addr) {}
    
    void set_callback(std::function<void(bool)> cb) {
        callback_ = std::move(cb);
    }
    
    void complete(bool success) {
        if (callback_) {
            callback_(success);
        }
    }
    
    I2CBuffer& tx_buffer() { return tx_buffer_; }
    I2CBuffer& rx_buffer() { return rx_buffer_; }
    uint8_t slave_address() const { return slave_addr_; }
};

// Smart pointer for automatic peripheral management
class I2CPeripheral {
private:
    bool enabled_;
    
public:
    I2CPeripheral() : enabled_(false) {
        enable();
    }
    
    ~I2CPeripheral() {
        disable();
    }
    
    void enable() {
        if (!enabled_) {
            // Enable clocks and peripheral
            enabled_ = true;
        }
    }
    
    void disable() {
        if (enabled_) {
            // Disable peripheral and clocks
            enabled_ = false;
        }
    }
    
    // Delete copy/move to ensure single ownership
    I2CPeripheral(const I2CPeripheral&) = delete;
    I2CPeripheral& operator=(const I2CPeripheral&) = delete;
};

// Usage example
void example_cpp_usage() {
    // Peripheral automatically enabled/disabled
    I2CPeripheral i2c_peripheral;
    
    // Create transaction with automatic memory management
    I2CTransaction txn(0x50, 32, 64);
    
    // Set up data
    uint8_t* tx = txn.tx_buffer().data();
    tx[0] = 0x00; // Register address
    
    // Set callback
    txn.set_callback([](bool success) {
        // Handle completion
    });
    
    // Transaction and buffers automatically cleaned up
}
```

## Rust Implementation Examples

### Example 1: Safe Buffer Management with Ownership

```rust
use core::mem::MaybeUninit;

/// Fixed-size buffer pool for I2C transfers
pub struct I2CBufferPool<const SIZE: usize, const COUNT: usize> {
    buffers: [BufferSlot<SIZE>; COUNT],
    stats: PoolStats,
}

struct BufferSlot<const SIZE: usize> {
    data: [u8; SIZE],
    in_use: bool,
}

#[derive(Default)]
struct PoolStats {
    allocations: u32,
    failures: u32,
}

impl<const SIZE: usize, const COUNT: usize> I2CBufferPool<SIZE, COUNT> {
    /// Create a new buffer pool
    pub const fn new() -> Self {
        const INIT_SLOT: BufferSlot<SIZE> = BufferSlot {
            data: [0; SIZE],
            in_use: false,
        };
        
        Self {
            buffers: [INIT_SLOT; COUNT],
            stats: PoolStats {
                allocations: 0,
                failures: 0,
            },
        }
    }
    
    /// Allocate a buffer from the pool
    pub fn alloc(&mut self) -> Option<I2CBuffer<SIZE>> {
        for (idx, slot) in self.buffers.iter_mut().enumerate() {
            if !slot.in_use {
                slot.in_use = true;
                self.stats.allocations += 1;
                return Some(I2CBuffer {
                    pool_index: idx,
                    data: &mut slot.data,
                });
            }
        }
        self.stats.failures += 1;
        None
    }
    
    /// Free a buffer back to the pool
    fn free(&mut self, index: usize) {
        if index < COUNT {
            self.buffers[index].in_use = false;
            self.buffers[index].data.fill(0); // Clear for security
        }
    }
    
    /// Get pool statistics
    pub fn stats(&self) -> (usize, usize, u32) {
        let used = self.buffers.iter().filter(|s| s.in_use).count();
        (COUNT, used, self.stats.failures)
    }
}

/// RAII buffer handle
pub struct I2CBuffer<'a, const SIZE: usize> {
    pool_index: usize,
    data: &'a mut [u8; SIZE],
}

impl<'a, const SIZE: usize> I2CBuffer<'a, SIZE> {
    pub fn as_slice(&self) -> &[u8] {
        &self.data[..]
    }
    
    pub fn as_mut_slice(&mut self) -> &mut [u8] {
        &mut self.data[..]
    }
}

// Buffer automatically freed when dropped
impl<'a, const SIZE: usize> Drop for I2CBuffer<'a, SIZE> {
    fn drop(&mut self) {
        // In real implementation, would call pool.free(self.pool_index)
        // This requires unsafe or a different architecture
    }
}
```

### Example 2: Interrupt-Driven I2C with Zero-Cost Abstractions

```rust
use core::cell::RefCell;
use core::sync::atomic::{AtomicBool, AtomicU32, Ordering};
use critical_section::Mutex;

/// I2C transaction states
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CState {
    Idle,
    TxInProgress,
    RxInProgress,
    Complete,
    Error,
}

/// I2C transaction descriptor
pub struct I2CTransaction {
    slave_address: u8,
    tx_data: &'static [u8],
    rx_buffer: Option<&'static mut [u8]>,
    tx_index: usize,
    rx_index: usize,
    state: I2CState,
}

impl I2CTransaction {
    pub fn new_write(slave_address: u8, data: &'static [u8]) -> Self {
        Self {
            slave_address,
            tx_data: data,
            rx_buffer: None,
            tx_index: 0,
            rx_index: 0,
            state: I2CState::Idle,
        }
    }
    
    pub fn new_read(
        slave_address: u8,
        tx_data: &'static [u8],
        rx_buffer: &'static mut [u8],
    ) -> Self {
        Self {
            slave_address,
            tx_data,
            rx_buffer: Some(rx_buffer),
            tx_index: 0,
            rx_index: 0,
            state: I2CState::Idle,
        }
    }
}

/// I2C driver with resource management
pub struct I2CDriver {
    peripheral_enabled: AtomicBool,
    interrupt_count: AtomicU32,
    error_count: AtomicU32,
}

static CURRENT_TRANSACTION: Mutex<RefCell<Option<I2CTransaction>>> =
    Mutex::new(RefCell::new(None));

impl I2CDriver {
    pub const fn new() -> Self {
        Self {
            peripheral_enabled: AtomicBool::new(false),
            interrupt_count: AtomicU32::new(0),
            error_count: AtomicU32::new(0),
        }
    }
    
    /// Enable peripheral with clock gating
    fn enable_peripheral(&self) {
        if !self.peripheral_enabled.load(Ordering::Relaxed) {
            // Enable peripheral clock (platform-specific)
            // unsafe { (*I2C1::ptr()).cr1.modify(|_, w| w.pe().set_bit()); }
            
            self.peripheral_enabled.store(true, Ordering::Relaxed);
        }
    }
    
    /// Disable peripheral for power saving
    pub fn disable_peripheral(&self) {
        critical_section::with(|cs| {
            let txn = CURRENT_TRANSACTION.borrow(cs).borrow();
            if txn.is_none() && self.peripheral_enabled.load(Ordering::Relaxed) {
                // Disable peripheral
                // unsafe { (*I2C1::ptr()).cr1.modify(|_, w| w.pe().clear_bit()); }
                
                self.peripheral_enabled.store(false, Ordering::Relaxed);
            }
        });
    }
    
    /// Start async transfer
    pub fn transfer_async(&self, mut transaction: I2CTransaction) -> Result<(), ()> {
        critical_section::with(|cs| {
            let mut current = CURRENT_TRANSACTION.borrow(cs).borrow_mut();
            if current.is_some() {
                return Err(()); // Busy
            }
            
            self.enable_peripheral();
            
            transaction.state = I2CState::TxInProgress;
            transaction.tx_index = 0;
            transaction.rx_index = 0;
            
            // Generate START condition
            // unsafe { (*I2C1::ptr()).cr1.modify(|_, w| w.start().set_bit()); }
            
            // Enable interrupts
            // unsafe {
            //     (*I2C1::ptr()).cr2.modify(|_, w| {
            //         w.itevten().set_bit().iterren().set_bit()
            //     });
            // }
            
            *current = Some(transaction);
            Ok(())
        })
    }
    
    /// Handle interrupt - called from ISR
    pub fn handle_interrupt(&self) {
        self.interrupt_count.fetch_add(1, Ordering::Relaxed);
        
        critical_section::with(|cs| {
            let mut txn_cell = CURRENT_TRANSACTION.borrow(cs).borrow_mut();
            let txn = match txn_cell.as_mut() {
                Some(t) => t,
                None => return,
            };
            
            // Read status registers (platform-specific)
            let (sr1, _sr2) = (0u32, 0u32); // Placeholder
            
            // Error handling
            if sr1 & 0xFF00 != 0 {
                txn.state = I2CState::Error;
                self.error_count.fetch_add(1, Ordering::Relaxed);
                self.complete_transaction(txn_cell);
                return;
            }
            
            // State machine
            match txn.state {
                I2CState::TxInProgress => {
                    if txn.tx_index < txn.tx_data.len() {
                        // Write next byte
                        // unsafe {
                        //     (*I2C1::ptr()).dr.write(|w| {
                        //         w.dr().bits(txn.tx_data[txn.tx_index])
                        //     });
                        // }
                        txn.tx_index += 1;
                    } else if txn.rx_buffer.is_some() {
                        // Generate repeated START for read
                        txn.state = I2CState::RxInProgress;
                        // unsafe {
                        //     (*I2C1::ptr()).cr1.modify(|_, w| w.start().set_bit());
                        // }
                    } else {
                        txn.state = I2CState::Complete;
                        self.complete_transaction(txn_cell);
                    }
                }
                
                I2CState::RxInProgress => {
                    if let Some(ref mut rx_buf) = txn.rx_buffer {
                        if txn.rx_index < rx_buf.len() {
                            // Read byte
                            // rx_buf[txn.rx_index] = unsafe {
                            //     (*I2C1::ptr()).dr.read().dr().bits()
                            // };
                            txn.rx_index += 1;
                            
                            // Send NACK on last byte
                            if txn.rx_index == rx_buf.len() - 1 {
                                // unsafe {
                                //     (*I2C1::ptr()).cr1.modify(|_, w| {
                                //         w.ack().clear_bit()
                                //     });
                                // }
                            }
                        }
                        
                        if txn.rx_index >= rx_buf.len() {
                            txn.state = I2CState::Complete;
                            self.complete_transaction(txn_cell);
                        }
                    }
                }
                
                _ => {}
            }
        });
    }
    
    fn complete_transaction(&self, mut txn_cell: core::cell::RefMut<Option<I2CTransaction>>) {
        // Generate STOP
        // unsafe { (*I2C1::ptr()).cr1.modify(|_, w| w.stop().set_bit()); }
        
        // Disable interrupts
        // unsafe {
        //     (*I2C1::ptr()).cr2.modify(|_, w| {
        //         w.itevten().clear_bit().iterren().clear_bit()
        //     });
        // }
        
        // Release transaction
        *txn_cell = None;
    }
    
    pub fn stats(&self) -> (u32, u32) {
        (
            self.interrupt_count.load(Ordering::Relaxed),
            self.error_count.load(Ordering::Relaxed),
        )
    }
}
```

### Example 3: DMA with Type-Safe Memory Management

```rust
use core::marker::PhantomData;
use core::sync::atomic::{AtomicBool, Ordering};

/// Marker trait for DMA-safe memory
pub unsafe trait DmaSafe {}

/// DMA buffer with alignment guarantees
#[repr(align(4))]
pub struct DmaBuffer<const SIZE: usize> {
    data: [u8; SIZE],
    transfer_complete: AtomicBool,
}

unsafe impl<const SIZE: usize> DmaSafe for DmaBuffer<SIZE> {}

impl<const SIZE: usize> DmaBuffer<SIZE> {
    pub const fn new() -> Self {
        Self {
            data: [0; SIZE],
            transfer_complete: AtomicBool::new(false),
        }
    }
    
    pub fn as_ptr(&self) -> *const u8 {
        self.data.as_ptr()
    }
    
    pub fn as_mut_ptr(&mut self) -> *mut u8 {
        self.data.as_mut_ptr()
    }
    
    pub fn len(&self) -> usize {
        SIZE
    }
    
    pub fn mark_complete(&self) {
        self.transfer_complete.store(true, Ordering::Release);
    }
    
    pub fn is_complete(&self) -> bool {
        self.transfer_complete.load(Ordering::Acquire)
    }
    
    pub fn reset(&self) {
        self.transfer_complete.store(false, Ordering::Relaxed);
    }
}

/// DMA transfer configuration
pub struct I2CDmaTransfer<'a, const SIZE: usize> {
    buffer: &'a DmaBuffer<SIZE>,
    length: usize,
    direction: DmaDirection,
}

#[derive(Clone, Copy)]
pub enum DmaDirection {
    MemoryToPeripheral,
    PeripheralToMemory,
}

impl<'a, const SIZE: usize> I2CDmaTransfer<'a, SIZE> {
    pub fn new_tx(buffer: &'a DmaBuffer<SIZE>, length: usize) -> Self {
        assert!(length <= SIZE);
        Self {
            buffer,
            length,
            direction: DmaDirection::MemoryToPeripheral,
        }
    }
    
    pub fn new_rx(buffer: &'a DmaBuffer<SIZE>, length: usize) -> Self {
        assert!(length <= SIZE);
        Self {
            buffer,
            length,
            direction: DmaDirection::PeripheralToMemory,
        }
    }
    
    pub fn start(&self) {
        self.buffer.reset();
        
        // Configure DMA (platform-specific)
        match self.direction {
            DmaDirection::MemoryToPeripheral => {
                // unsafe {
                //     let dma = &*DMA1::ptr();
                //     dma.cpar.write(|w| w.bits(I2C1_DR_ADDRESS));
                //     dma.cmar.write(|w| w.bits(self.buffer.as_ptr() as u32));
                //     dma.cndtr.write(|w| w.ndt().bits(self.length as u16));
                //     dma.ccr.modify(|_, w| {
                //         w.minc().set_bit()
                //          .dir().set_bit()
                //          .tcie().set_bit()
                //          .en().set_bit()
                //     });
                // }
            }
            DmaDirection::PeripheralToMemory => {
                // Configure for RX
            }
        }
    }
    
    pub fn is_complete(&self) -> bool {
        self.buffer.is_complete()
    }
}

/// DMA statistics
#[derive(Default)]
pub struct DmaStats {
    pub bytes_transferred: u32,
    pub transfers_completed: u32,
}

impl DmaStats {
    pub fn cpu_cycles_saved(&self) -> u32 {
        // Estimate: ~50 cycles saved per byte
        self.bytes_transferred * 50
    }
}

// Example usage
pub fn example_dma_usage() {
    static TX_BUFFER: DmaBuffer<256> = DmaBuffer::new();
    
    let transfer = I2CDmaTransfer::new_tx(&TX_BUFFER, 64);
    transfer.start();
    
    // Wait for completion (non-blocking check)
    while !transfer.is_complete() {
        // Can do other work here
    }
}
```

### Example 4: Complete Resource-Managed I2C Driver

```rust
use embedded_hal::i2c::{I2c, Operation};

pub struct ResourceManagedI2C<I2C> {
    i2c: I2C,
    buffer_pool: I2CBufferPool<64, 4>,
    power_state: PowerState,
    stats: DriverStats,
}

#[derive(Clone, Copy, PartialEq)]
enum PowerState {
    Active,
    LowPower,
}

#[derive(Default)]
struct DriverStats {
    transactions: u32,
    errors: u32,
    power_cycles: u32,
}

impl<I2C: I2c> ResourceManagedI2C<I2C> {
    pub fn new(i2c: I2C) -> Self {
        Self {
            i2c,
            buffer_pool: I2CBufferPool::new(),
            power_state: PowerState::LowPower,
            stats: DriverStats::default(),
        }
    }
    
    fn ensure_active(&mut self) {
        if self.power_state == PowerState::LowPower {
            // Wake up peripheral
            self.power_state = PowerState::Active;
            self.stats.power_cycles += 1;
        }
    }
    
    pub fn write(&mut self, address: u8, data: &[u8]) -> Result<(), I2C::Error> {
        self.ensure_active();
        self.stats.transactions += 1;
        
        let result = self.i2c.write(address, data);
        if result.is_err() {
            self.stats.errors += 1;
        }
        result
    }
    
    pub fn read(&mut self, address: u8, buffer: &mut [u8]) -> Result<(), I2C::Error> {
        self.ensure_active();
        self.stats.transactions += 1;
        
        let result = self.i2c.read(address, buffer);
        if result.is_err() {
            self.stats.errors += 1;
        }
        result
    }
    
    pub fn enter_low_power(&mut self) {
        if self.power_state == PowerState::Active {
            // Disable peripheral clocks
            self.power_state = PowerState::LowPower;
        }
    }
    
    pub fn stats(&self) -> &DriverStats {
        &self.stats
    }
    
    pub fn efficiency(&self) -> f32 {
        if self.stats.transactions == 0 {
            return 0.0;
        }
        (self.stats.transactions - self.stats.errors) as f32 
            / self.stats.transactions as f32 * 100.0
    }
}

// Automatic power down on drop
impl<I2C> Drop for ResourceManagedI2C<I2C> {
    fn drop(&mut self) {
        self.enter_low_power();
    }
}
```

## Best Practices

1. **Use static allocation** when buffer sizes are known at compile time
2. **Implement buffer pooling** to avoid runtime allocation and fragmentation
3. **Leverage interrupts and DMA** to minimize CPU usage
4. **Implement power management** to disable peripherals when idle
5. **Use RAII patterns** (C++ and Rust) for automatic resource cleanup
6. **Profile and measure** actual resource usage in your specific application
7. **Consider real-time constraints** when designing resource management strategies
8. **Implement watchdogs** to recover from resource exhaustion or deadlocks

These techniques ensure your I2C drivers are efficient, reliable, and suitable for resource-constrained embedded systems.