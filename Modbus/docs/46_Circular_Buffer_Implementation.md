# Circular Buffer Implementation for Modbus Serial Data Handling

## Overview

A circular buffer (also called a ring buffer) is a fixed-size data structure that uses a single, contiguous block of memory as if it were connected end-to-end. In Modbus applications, circular buffers are essential for managing high-performance serial data streams, particularly in scenarios where data arrives asynchronously and needs to be processed without blocking or losing information.

## Why Circular Buffers Matter in Modbus

In Modbus RTU/ASCII serial communications, data arrives at unpredictable intervals. A circular buffer provides:

- **Continuous data flow**: No need to shift data when removing elements
- **Lock-free operations**: Can be implemented without mutexes in single-producer/single-consumer scenarios
- **Predictable performance**: O(1) read and write operations
- **Memory efficiency**: Fixed allocation prevents fragmentation
- **Real-time suitability**: Deterministic behavior for embedded systems

## Core Concepts

### Structure
A circular buffer maintains:
- **Buffer array**: Fixed-size memory block
- **Head pointer**: Write position (producer)
- **Tail pointer**: Read position (consumer)
- **Size tracking**: Current number of elements or full/empty state

### Key Operations
1. **Write (Enqueue)**: Add data at head, advance head pointer
2. **Read (Dequeue)**: Remove data from tail, advance tail pointer
3. **Full/Empty detection**: Compare head and tail positions
4. **Wrap-around**: Use modulo arithmetic for circular behavior

## C Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#define MODBUS_BUFFER_SIZE 256

typedef struct {
    uint8_t buffer[MODBUS_BUFFER_SIZE];
    volatile uint16_t head;  // Write position
    volatile uint16_t tail;  // Read position
    volatile uint16_t count; // Number of bytes in buffer
} modbus_circular_buffer_t;

// Initialize the circular buffer
void modbus_buffer_init(modbus_circular_buffer_t *cb) {
    cb->head = 0;
    cb->tail = 0;
    cb->count = 0;
}

// Check if buffer is full
bool modbus_buffer_is_full(modbus_circular_buffer_t *cb) {
    return cb->count == MODBUS_BUFFER_SIZE;
}

// Check if buffer is empty
bool modbus_buffer_is_empty(modbus_circular_buffer_t *cb) {
    return cb->count == 0;
}

// Get available space
uint16_t modbus_buffer_available_space(modbus_circular_buffer_t *cb) {
    return MODBUS_BUFFER_SIZE - cb->count;
}

// Get number of bytes available to read
uint16_t modbus_buffer_available_data(modbus_circular_buffer_t *cb) {
    return cb->count;
}

// Write a single byte (typically called from ISR)
bool modbus_buffer_write_byte(modbus_circular_buffer_t *cb, uint8_t byte) {
    if (modbus_buffer_is_full(cb)) {
        return false; // Buffer overflow
    }
    
    cb->buffer[cb->head] = byte;
    cb->head = (cb->head + 1) % MODBUS_BUFFER_SIZE;
    cb->count++;
    
    return true;
}

// Read a single byte
bool modbus_buffer_read_byte(modbus_circular_buffer_t *cb, uint8_t *byte) {
    if (modbus_buffer_is_empty(cb)) {
        return false; // Buffer underflow
    }
    
    *byte = cb->buffer[cb->tail];
    cb->tail = (cb->tail + 1) % MODBUS_BUFFER_SIZE;
    cb->count--;
    
    return true;
}

// Write multiple bytes (for transmission)
uint16_t modbus_buffer_write(modbus_circular_buffer_t *cb, 
                              const uint8_t *data, 
                              uint16_t length) {
    uint16_t written = 0;
    
    while (written < length && !modbus_buffer_is_full(cb)) {
        cb->buffer[cb->head] = data[written];
        cb->head = (cb->head + 1) % MODBUS_BUFFER_SIZE;
        cb->count++;
        written++;
    }
    
    return written;
}

// Read multiple bytes (for processing)
uint16_t modbus_buffer_read(modbus_circular_buffer_t *cb, 
                             uint8_t *data, 
                             uint16_t length) {
    uint16_t read = 0;
    
    while (read < length && !modbus_buffer_is_empty(cb)) {
        data[read] = cb->buffer[cb->tail];
        cb->tail = (cb->tail + 1) % MODBUS_BUFFER_SIZE;
        cb->count--;
        read++;
    }
    
    return read;
}

// Peek at data without removing (useful for frame detection)
bool modbus_buffer_peek(modbus_circular_buffer_t *cb, 
                        uint16_t offset, 
                        uint8_t *byte) {
    if (offset >= cb->count) {
        return false;
    }
    
    uint16_t index = (cb->tail + offset) % MODBUS_BUFFER_SIZE;
    *byte = cb->buffer[index];
    
    return true;
}

// Example: UART RX interrupt handler
void UART_RX_IRQHandler(void) {
    modbus_circular_buffer_t *rx_buffer = get_rx_buffer();
    
    if (UART_DataAvailable()) {
        uint8_t byte = UART_ReadByte();
        modbus_buffer_write_byte(rx_buffer, byte);
    }
}

// Example: Modbus frame processor
void modbus_process_received_data(void) {
    modbus_circular_buffer_t *rx_buffer = get_rx_buffer();
    uint8_t frame[256];
    
    // Check if we have at least minimum frame size
    if (modbus_buffer_available_data(rx_buffer) >= 4) {
        // Peek at address and function code
        uint8_t addr, func;
        modbus_buffer_peek(rx_buffer, 0, &addr);
        modbus_buffer_peek(rx_buffer, 1, &func);
        
        // Determine expected frame length based on function code
        uint16_t expected_length = calculate_frame_length(func, rx_buffer);
        
        if (modbus_buffer_available_data(rx_buffer) >= expected_length) {
            // Read complete frame
            uint16_t read = modbus_buffer_read(rx_buffer, frame, expected_length);
            
            // Process the frame
            process_modbus_frame(frame, read);
        }
    }
}
```

## C++ Implementation

```cpp
#include <array>
#include <atomic>
#include <optional>
#include <vector>
#include <cstdint>

template<typename T, size_t Size>
class CircularBuffer {
private:
    std::array<T, Size> buffer_;
    std::atomic<size_t> head_{0};
    std::atomic<size_t> tail_{0};
    std::atomic<size_t> count_{0};

public:
    CircularBuffer() = default;
    
    // Non-copyable for safety
    CircularBuffer(const CircularBuffer&) = delete;
    CircularBuffer& operator=(const CircularBuffer&) = delete;
    
    // Check if buffer is full
    bool is_full() const noexcept {
        return count_.load(std::memory_order_acquire) == Size;
    }
    
    // Check if buffer is empty
    bool is_empty() const noexcept {
        return count_.load(std::memory_order_acquire) == 0;
    }
    
    // Get current size
    size_t size() const noexcept {
        return count_.load(std::memory_order_acquire);
    }
    
    // Get available space
    size_t available_space() const noexcept {
        return Size - size();
    }
    
    // Push single element
    bool push(const T& item) noexcept {
        if (is_full()) {
            return false;
        }
        
        size_t current_head = head_.load(std::memory_order_relaxed);
        buffer_[current_head] = item;
        head_.store((current_head + 1) % Size, std::memory_order_release);
        count_.fetch_add(1, std::memory_order_acq_rel);
        
        return true;
    }
    
    // Pop single element
    std::optional<T> pop() noexcept {
        if (is_empty()) {
            return std::nullopt;
        }
        
        size_t current_tail = tail_.load(std::memory_order_relaxed);
        T value = buffer_[current_tail];
        tail_.store((current_tail + 1) % Size, std::memory_order_release);
        count_.fetch_sub(1, std::memory_order_acq_rel);
        
        return value;
    }
    
    // Push multiple elements
    size_t push_multiple(const T* data, size_t length) noexcept {
        size_t pushed = 0;
        
        while (pushed < length && !is_full()) {
            size_t current_head = head_.load(std::memory_order_relaxed);
            buffer_[current_head] = data[pushed];
            head_.store((current_head + 1) % Size, std::memory_order_release);
            count_.fetch_add(1, std::memory_order_acq_rel);
            pushed++;
        }
        
        return pushed;
    }
    
    // Pop multiple elements
    size_t pop_multiple(T* data, size_t max_length) noexcept {
        size_t popped = 0;
        
        while (popped < max_length && !is_empty()) {
            size_t current_tail = tail_.load(std::memory_order_relaxed);
            data[popped] = buffer_[current_tail];
            tail_.store((current_tail + 1) % Size, std::memory_order_release);
            count_.fetch_sub(1, std::memory_order_acq_rel);
            popped++;
        }
        
        return popped;
    }
    
    // Peek at element without removing
    std::optional<T> peek(size_t offset = 0) const noexcept {
        if (offset >= size()) {
            return std::nullopt;
        }
        
        size_t current_tail = tail_.load(std::memory_order_acquire);
        size_t index = (current_tail + offset) % Size;
        return buffer_[index];
    }
    
    // Clear buffer
    void clear() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        count_.store(0, std::memory_order_release);
    }
};

// Modbus-specific implementation
class ModbusSerialBuffer {
private:
    CircularBuffer<uint8_t, 512> rx_buffer_;
    CircularBuffer<uint8_t, 512> tx_buffer_;
    
public:
    // Receive byte (called from ISR)
    bool receive_byte(uint8_t byte) {
        return rx_buffer_.push(byte);
    }
    
    // Check for complete Modbus frame
    std::optional<std::vector<uint8_t>> extract_frame() {
        // Need at least slave address + function code + CRC
        if (rx_buffer_.size() < 4) {
            return std::nullopt;
        }
        
        // Peek at function code to determine frame length
        auto func_code = rx_buffer_.peek(1);
        if (!func_code) {
            return std::nullopt;
        }
        
        size_t expected_length = calculate_expected_length(*func_code);
        
        if (rx_buffer_.size() < expected_length) {
            return std::nullopt;
        }
        
        // Extract frame
        std::vector<uint8_t> frame(expected_length);
        size_t read = rx_buffer_.pop_multiple(frame.data(), expected_length);
        
        if (read != expected_length) {
            return std::nullopt;
        }
        
        return frame;
    }
    
    // Queue data for transmission
    bool queue_transmission(const std::vector<uint8_t>& data) {
        return tx_buffer_.push_multiple(data.data(), data.size()) == data.size();
    }
    
    // Get byte for transmission (called from ISR)
    std::optional<uint8_t> get_tx_byte() {
        return tx_buffer_.pop();
    }
    
private:
    size_t calculate_expected_length(uint8_t function_code) {
        // Simplified example
        switch (function_code) {
            case 0x03: // Read Holding Registers
            case 0x04: // Read Input Registers
                return 8; // Standard request: addr + func + start + count + CRC
            default:
                return 8; // Minimum frame size
        }
    }
};
```

## Rust Implementation

```rust
use std::sync::atomic::{AtomicUsize, Ordering};
use std::sync::Arc;

/// Lock-free circular buffer for single producer, single consumer
pub struct CircularBuffer<T: Copy, const SIZE: usize> {
    buffer: Box<[T; SIZE]>,
    head: AtomicUsize,
    tail: AtomicUsize,
    count: AtomicUsize,
}

impl<T: Copy + Default, const SIZE: usize> CircularBuffer<T, SIZE> {
    /// Create a new circular buffer
    pub fn new() -> Self {
        Self {
            buffer: Box::new([T::default(); SIZE]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
            count: AtomicUsize::new(0),
        }
    }
    
    /// Check if buffer is full
    pub fn is_full(&self) -> bool {
        self.count.load(Ordering::Acquire) == SIZE
    }
    
    /// Check if buffer is empty
    pub fn is_empty(&self) -> bool {
        self.count.load(Ordering::Acquire) == 0
    }
    
    /// Get number of elements in buffer
    pub fn len(&self) -> usize {
        self.count.load(Ordering::Acquire)
    }
    
    /// Get available space
    pub fn available_space(&self) -> usize {
        SIZE - self.len()
    }
    
    /// Push a single element
    pub fn push(&self, item: T) -> Result<(), T> {
        if self.is_full() {
            return Err(item);
        }
        
        let head = self.head.load(Ordering::Relaxed);
        
        // Safety: We've verified buffer isn't full
        unsafe {
            let buffer_ptr = self.buffer.as_ptr() as *mut T;
            buffer_ptr.add(head).write(item);
        }
        
        self.head.store((head + 1) % SIZE, Ordering::Release);
        self.count.fetch_add(1, Ordering::AcqRel);
        
        Ok(())
    }
    
    /// Pop a single element
    pub fn pop(&self) -> Option<T> {
        if self.is_empty() {
            return None;
        }
        
        let tail = self.tail.load(Ordering::Relaxed);
        let value = self.buffer[tail];
        
        self.tail.store((tail + 1) % SIZE, Ordering::Release);
        self.count.fetch_sub(1, Ordering::AcqRel);
        
        Some(value)
    }
    
    /// Push multiple elements
    pub fn push_slice(&self, data: &[T]) -> usize {
        let mut pushed = 0;
        
        for &item in data {
            if self.push(item).is_ok() {
                pushed += 1;
            } else {
                break;
            }
        }
        
        pushed
    }
    
    /// Pop multiple elements into a vector
    pub fn pop_multiple(&self, max_count: usize) -> Vec<T> {
        let mut result = Vec::with_capacity(max_count.min(self.len()));
        
        for _ in 0..max_count {
            if let Some(item) = self.pop() {
                result.push(item);
            } else {
                break;
            }
        }
        
        result
    }
    
    /// Peek at element without removing
    pub fn peek(&self, offset: usize) -> Option<T> {
        if offset >= self.len() {
            return None;
        }
        
        let tail = self.tail.load(Ordering::Acquire);
        let index = (tail + offset) % SIZE;
        Some(self.buffer[index])
    }
    
    /// Clear the buffer
    pub fn clear(&self) {
        self.head.store(0, Ordering::Relaxed);
        self.tail.store(0, Ordering::Relaxed);
        self.count.store(0, Ordering::Release);
    }
}

// Thread-safe wrapper
pub type SharedCircularBuffer<T, const SIZE: usize> = Arc<CircularBuffer<T, SIZE>>;

/// Modbus-specific circular buffer implementation
pub struct ModbusSerialBuffer {
    rx_buffer: SharedCircularBuffer<u8, 512>,
    tx_buffer: SharedCircularBuffer<u8, 512>,
}

impl ModbusSerialBuffer {
    pub fn new() -> Self {
        Self {
            rx_buffer: Arc::new(CircularBuffer::new()),
            tx_buffer: Arc::new(CircularBuffer::new()),
        }
    }
    
    /// Get RX buffer for ISR/interrupt context
    pub fn rx_buffer(&self) -> SharedCircularBuffer<u8, 512> {
        Arc::clone(&self.rx_buffer)
    }
    
    /// Get TX buffer for ISR/interrupt context
    pub fn tx_buffer(&self) -> SharedCircularBuffer<u8, 512> {
        Arc::clone(&self.tx_buffer)
    }
    
    /// Receive a byte (typically called from interrupt)
    pub fn receive_byte(&self, byte: u8) -> Result<(), u8> {
        self.rx_buffer.push(byte)
    }
    
    /// Extract a complete Modbus frame if available
    pub fn extract_frame(&self) -> Option<Vec<u8>> {
        // Need minimum frame: addr + func + data + CRC (at least 4 bytes)
        if self.rx_buffer.len() < 4 {
            return None;
        }
        
        // Peek at function code to determine expected length
        let function_code = self.rx_buffer.peek(1)?;
        let expected_length = self.calculate_expected_length(function_code);
        
        if self.rx_buffer.len() < expected_length {
            return None;
        }
        
        // Extract frame
        Some(self.rx_buffer.pop_multiple(expected_length))
    }
    
    /// Queue data for transmission
    pub fn queue_transmission(&self, data: &[u8]) -> Result<(), &'static str> {
        if self.tx_buffer.push_slice(data) == data.len() {
            Ok(())
        } else {
            Err("TX buffer full")
        }
    }
    
    /// Get next byte to transmit
    pub fn get_tx_byte(&self) -> Option<u8> {
        self.tx_buffer.pop()
    }
    
    fn calculate_expected_length(&self, function_code: u8) -> usize {
        match function_code {
            0x01 | 0x02 | 0x03 | 0x04 => 8, // Standard read request
            0x05 | 0x06 => 8,                 // Write single
            0x0F | 0x10 => {                  // Write multiple
                if let Some(byte_count) = self.rx_buffer.peek(6) {
                    9 + byte_count as usize
                } else {
                    8
                }
            }
            _ => 8, // Minimum
        }
    }
}

// Example usage in interrupt handler
#[no_mangle]
pub extern "C" fn uart_rx_interrupt_handler() {
    // Get buffer (would be stored as static in real code)
    // let buffer = get_modbus_buffer();
    
    // Read byte from UART hardware
    // let byte = read_uart_data_register();
    
    // Store in circular buffer
    // let _ = buffer.receive_byte(byte);
}

// Example main processing loop
fn modbus_processing_loop(buffer: &ModbusSerialBuffer) {
    loop {
        // Check for complete frames
        if let Some(frame) = buffer.extract_frame() {
            // Validate CRC
            if validate_modbus_crc(&frame) {
                // Process the frame
                let response = process_modbus_request(&frame);
                
                // Queue response for transmission
                let _ = buffer.queue_transmission(&response);
            }
        }
        
        // Small delay or yield to prevent busy waiting
        std::thread::sleep(std::time::Duration::from_micros(100));
    }
}

fn validate_modbus_crc(frame: &[u8]) -> bool {
    // CRC validation implementation
    true // Placeholder
}

fn process_modbus_request(frame: &[u8]) -> Vec<u8> {
    // Request processing implementation
    vec![] // Placeholder
}
```

## Summary

**Circular buffers are fundamental to high-performance Modbus serial communication**, providing lock-free, deterministic data handling for asynchronous serial streams. Key benefits include:

- **Performance**: O(1) operations with no memory allocation or shifting
- **Lock-free design**: Safe for ISR/interrupt contexts without mutexes in single-producer/single-consumer scenarios
- **Memory efficiency**: Fixed allocation prevents fragmentation in embedded systems
- **Real-time suitability**: Predictable, deterministic behavior essential for industrial protocols

The implementations shown demonstrate production-ready circular buffers in C (bare-metal embedded), C++ (with modern atomics), and Rust (memory-safe with zero-cost abstractions). Each supports the critical operations needed for Modbus: byte-by-byte reception in interrupts, frame extraction with peek capabilities, and efficient multi-byte transfers for processing complete protocol frames without blocking or data loss.