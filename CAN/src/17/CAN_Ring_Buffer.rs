use core::sync::atomic::{AtomicUsize, Ordering};
use core::cell::UnsafeCell;

/// CAN message representation
#[derive(Clone, Copy, Debug)]
pub struct CanMessage {
    pub id: u32,
    pub dlc: u8,
    pub data: [u8; 8],
    pub timestamp: u32,
    pub is_extended: bool,
    pub is_rtr: bool,
}

impl Default for CanMessage {
    fn default() -> Self {
        Self {
            id: 0,
            dlc: 0,
            data: [0; 8],
            timestamp: 0,
            is_extended: false,
            is_rtr: false,
        }
    }
}

/// Lock-free ring buffer implementation
/// Safe for single-producer, single-consumer scenarios in interrupt contexts
pub struct RingBuffer<T, const N: usize> {
    buffer: UnsafeCell<[T; N]>,
    head: AtomicUsize,
    tail: AtomicUsize,
    count: AtomicUsize,
}

// Safety: The ring buffer is designed to be safely shared across threads
// when used in single-producer, single-consumer contexts
unsafe impl<T: Send, const N: usize> Send for RingBuffer<T, N> {}
unsafe impl<T: Send, const N: usize> Sync for RingBuffer<T, N> {}

impl<T: Default + Copy, const N: usize> RingBuffer<T, N> {
    /// Create a new ring buffer
    pub const fn new() -> Self {
        Self {
            buffer: UnsafeCell::new([T::default(); N]),
            head: AtomicUsize::new(0),
            tail: AtomicUsize::new(0),
            count: AtomicUsize::new(0),
        }
    }
    
    /// Push an item onto the buffer
    /// Returns Ok(()) on success, Err(item) if buffer is full
    pub fn push(&self, item: T) -> Result<(), T> {
        // Check if buffer is full
        if self.is_full() {
            return Err(item);
        }
        
        // Get current head position
        let head = self.head.load(Ordering::Acquire);
        
        // SAFETY: We've verified the buffer isn't full, and in SPSC
        // only the producer modifies the head position
        unsafe {
            let buffer = &mut *self.buffer.get();
            buffer[head] = item;
        }
        
        // Update head with wrap-around
        let next_head = (head + 1) % N;
        self.head.store(next_head, Ordering::Release);
        
        // Increment count atomically
        self.count.fetch_add(1, Ordering::Release);
        
        Ok(())
    }
    
    /// Pop an item from the buffer
    /// Returns Some(item) on success, None if buffer is empty
    pub fn pop(&self) -> Option<T> {
        // Check if buffer is empty
        if self.is_empty() {
            return None;
        }
        
        // Get current tail position
        let tail = self.tail.load(Ordering::Acquire);
        
        // SAFETY: We've verified the buffer isn't empty, and in SPSC
        // only the consumer modifies the tail position
        let item = unsafe {
            let buffer = &*self.buffer.get();
            buffer[tail]
        };
        
        // Update tail with wrap-around
        let next_tail = (tail + 1) % N;
        self.tail.store(next_tail, Ordering::Release);
        
        // Decrement count atomically
        self.count.fetch_sub(1, Ordering::Release);
        
        Some(item)
    }
    
    /// Peek at the next item without removing it
    pub fn peek(&self) -> Option<T> {
        if self.is_empty() {
            return None;
        }
        
        let tail = self.tail.load(Ordering::Acquire);
        
        // SAFETY: We've verified the buffer isn't empty
        unsafe {
            let buffer = &*self.buffer.get();
            Some(buffer[tail])
        }
    }
    
    /// Check if buffer is empty
    #[inline]
    pub fn is_empty(&self) -> bool {
        self.count.load(Ordering::Acquire) == 0
    }
    
    /// Check if buffer is full
    #[inline]
    pub fn is_full(&self) -> bool {
        self.count.load(Ordering::Acquire) >= N
    }
    
    /// Get current number of items
    #[inline]
    pub fn len(&self) -> usize {
        self.count.load(Ordering::Acquire)
    }
    
    /// Get buffer capacity
    #[inline]
    pub const fn capacity(&self) -> usize {
        N
    }
    
    /// Get available space
    #[inline]
    pub fn available(&self) -> usize {
        N - self.len()
    }
    
    /// Clear the buffer
    pub fn clear(&self) {
        self.head.store(0, Ordering::Release);
        self.tail.store(0, Ordering::Release);
        self.count.store(0, Ordering::Release);
    }
    
    /// Get buffer utilization percentage (0-100)
    pub fn utilization(&self) -> u8 {
        ((self.len() * 100) / N) as u8
    }
}

/// CAN buffer manager with separate TX and RX queues
pub struct CanBufferManager<const TX_SIZE: usize, const RX_SIZE: usize> {
    tx_buffer: RingBuffer<CanMessage, TX_SIZE>,
    rx_buffer: RingBuffer<CanMessage, RX_SIZE>,
    tx_overflows: AtomicUsize,
    rx_overflows: AtomicUsize,
}

impl<const TX_SIZE: usize, const RX_SIZE: usize> CanBufferManager<TX_SIZE, RX_SIZE> {
    /// Create a new buffer manager
    pub const fn new() -> Self {
        Self {
            tx_buffer: RingBuffer::new(),
            rx_buffer: RingBuffer::new(),
            tx_overflows: AtomicUsize::new(0),
            rx_overflows: AtomicUsize::new(0),
        }
    }
    
    // TX operations
    
    /// Queue a message for transmission
    pub fn queue_tx(&self, msg: CanMessage) -> Result<(), CanMessage> {
        self.tx_buffer.push(msg).map_err(|msg| {
            self.tx_overflows.fetch_add(1, Ordering::Relaxed);
            msg
        })
    }
    
    /// Get next message to transmit
    pub fn get_tx(&self) -> Option<CanMessage> {
        self.tx_buffer.pop()
    }
    
    /// Peek at next TX message
    pub fn peek_tx(&self) -> Option<CanMessage> {
        self.tx_buffer.peek()
    }
    
    /// Check if there's data to transmit
    pub fn has_tx_data(&self) -> bool {
        !self.tx_buffer.is_empty()
    }
    
    // RX operations
    
    /// Queue a received message
    pub fn queue_rx(&self, msg: CanMessage) -> Result<(), CanMessage> {
        self.rx_buffer.push(msg).map_err(|msg| {
            self.rx_overflows.fetch_add(1, Ordering::Relaxed);
            msg
        })
    }
    
    /// Get next received message
    pub fn get_rx(&self) -> Option<CanMessage> {
        self.rx_buffer.pop()
    }
    
    /// Check if there's received data
    pub fn has_rx_data(&self) -> bool {
        !self.rx_buffer.is_empty()
    }
    
    // Status and statistics
    
    pub fn tx_count(&self) -> usize {
        self.tx_buffer.len()
    }
    
    pub fn rx_count(&self) -> usize {
        self.rx_buffer.len()
    }
    
    pub fn tx_overflow_count(&self) -> usize {
        self.tx_overflows.load(Ordering::Relaxed)
    }
    
    pub fn rx_overflow_count(&self) -> usize {
        self.rx_overflows.load(Ordering::Relaxed)
    }
    
    pub fn clear_buffers(&self) {
        self.tx_buffer.clear();
        self.rx_buffer.clear();
    }
    
    pub fn reset_overflow_counters(&self) {
        self.tx_overflows.store(0, Ordering::Relaxed);
        self.rx_overflows.store(0, Ordering::Relaxed);
    }
}

/// Buffer statistics
#[derive(Debug, Clone, Copy)]
pub struct BufferStats {
    pub tx_count: usize,
    pub tx_capacity: usize,
    pub tx_utilization: u8,
    pub tx_overflows: usize,
    
    pub rx_count: usize,
    pub rx_capacity: usize,
    pub rx_utilization: u8,
    pub rx_overflows: usize,
}

impl<const TX_SIZE: usize, const RX_SIZE: usize> CanBufferManager<TX_SIZE, RX_SIZE> {
    pub fn get_stats(&self) -> BufferStats {
        BufferStats {
            tx_count: self.tx_buffer.len(),
            tx_capacity: TX_SIZE,
            tx_utilization: self.tx_buffer.utilization(),
            tx_overflows: self.tx_overflow_count(),
            
            rx_count: self.rx_buffer.len(),
            rx_capacity: RX_SIZE,
            rx_utilization: self.rx_buffer.utilization(),
            rx_overflows: self.rx_overflow_count(),
        }
    }
}

// ============================================================================
// Usage Examples
// ============================================================================

// Global buffer manager (common in embedded systems)
static CAN_BUFFERS: CanBufferManager<32, 64> = CanBufferManager::new();

/// Send a CAN message (application level)
pub fn send_can_message(id: u32, data: &[u8]) -> Result<(), &'static str> {
    let mut msg = CanMessage::default();
    msg.id = id;
    msg.dlc = data.len().min(8) as u8;
    msg.data[..msg.dlc as usize].copy_from_slice(&data[..msg.dlc as usize]);
    
    CAN_BUFFERS.queue_tx(msg)
        .map_err(|_| "TX buffer full")?;
    
    // Enable TX interrupt
    enable_can_tx_interrupt();
    
    Ok(())
}

/// CAN TX interrupt handler
pub fn can_tx_irq_handler() {
    if let Some(msg) = CAN_BUFFERS.get_tx() {
        // Load message into hardware registers
        load_can_hardware(&msg);
    } else {
        // No more messages - disable interrupt
        disable_can_tx_interrupt();
    }
}

/// CAN RX interrupt handler
pub fn can_rx_irq_handler() {
    // Read message from hardware
    let msg = read_can_hardware();
    
    // Queue for processing (log error if full)
    if let Err(_) = CAN_BUFFERS.queue_rx(msg) {
        // Buffer overflow - message dropped
    }
}

/// Process received messages with closure
pub fn process_rx_messages<F>(mut handler: F)
where
    F: FnMut(CanMessage),
{
    while let Some(msg) = CAN_BUFFERS.get_rx() {
        handler(msg);
    }
}

/// Example: Main loop message processing
pub fn main_loop() {
    process_rx_messages(|msg| {
        match msg.id {
            0x100 => handle_sensor_data(&msg),
            0x200 => handle_command(&msg),
            _ => { /* Unknown message */ }
        }
    });
}

/// Example: Message builder pattern
pub struct CanMessageBuilder {
    msg: CanMessage,
}

impl CanMessageBuilder {
    pub fn new(id: u32) -> Self {
        Self {
            msg: CanMessage {
                id,
                ..Default::default()
            }
        }
    }
    
    pub fn data(mut self, data: &[u8]) -> Self {
        let len = data.len().min(8);
        self.msg.dlc = len as u8;
        self.msg.data[..len].copy_from_slice(&data[..len]);
        self
    }
    
    pub fn extended(mut self, extended: bool) -> Self {
        self.msg.is_extended = extended;
        self
    }
    
    pub fn rtr(mut self, rtr: bool) -> Self {
        self.msg.is_rtr = rtr;
        self
    }
    
    pub fn send(self) -> Result<(), &'static str> {
        CAN_BUFFERS.queue_tx(self.msg)
            .map_err(|_| "TX buffer full")?;
        enable_can_tx_interrupt();
        Ok(())
    }
}

/// Example: Buffer diagnostics
pub struct CanDiagnostics {
    last_tx_overflows: usize,
    last_rx_overflows: usize,
}

impl CanDiagnostics {
    pub const fn new() -> Self {
        Self {
            last_tx_overflows: 0,
            last_rx_overflows: 0,
        }
    }
    
    pub fn periodic_check(&mut self) {
        let stats = CAN_BUFFERS.get_stats();
        
        // Check utilization
        if stats.tx_utilization > 80 {
            log_warning!("TX buffer high: {}%", stats.tx_utilization);
        }
        
        if stats.rx_utilization > 80 {
            log_warning!("RX buffer high: {}%", stats.rx_utilization);
        }
        
        // Check for new overflows
        let new_tx_overflows = stats.tx_overflows - self.last_tx_overflows;
        if new_tx_overflows > 0 {
            log_error!("TX overflows: {} new, {} total", 
                      new_tx_overflows, stats.tx_overflows);
            self.last_tx_overflows = stats.tx_overflows;
        }
        
        let new_rx_overflows = stats.rx_overflows - self.last_rx_overflows;
        if new_rx_overflows > 0 {
            log_error!("RX overflows: {} new, {} total",
                      new_rx_overflows, stats.rx_overflows);
            self.last_rx_overflows = stats.rx_overflows;
        }
    }
}

// Placeholder functions for hardware interaction
fn enable_can_tx_interrupt() { /* Hardware specific */ }
fn disable_can_tx_interrupt() { /* Hardware specific */ }
fn load_can_hardware(_msg: &CanMessage) { /* Hardware specific */ }
fn read_can_hardware() -> CanMessage { CanMessage::default() }
fn handle_sensor_data(_msg: &CanMessage) { /* Application specific */ }
fn handle_command(_msg: &CanMessage) { /* Application specific */ }

// Placeholder logging macros
macro_rules! log_warning { ($($arg:tt)*) => { /* ... */ }; }
macro_rules! log_error { ($($arg:tt)*) => { /* ... */ }; }