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