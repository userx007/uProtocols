/*
 * SPI Timeout Management Implementation in Rust
 * Demonstrates type-safe timeout handling with Rust's ownership system
 */

use core::time::Duration;
use core::marker::PhantomData;

// Platform-specific timer trait
pub trait Timer {
    fn now(&self) -> u64;
    fn elapsed_ms(&self, start: u64) -> u64 {
        self.now().saturating_sub(start)
    }
}

// SPI Error types
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum SpiError {
    Timeout,
    Busy,
    Hardware,
    InvalidParameter,
    TransferIncomplete,
}

pub type SpiResult<T> = Result<T, SpiError>;

// SPI Status flags
#[derive(Clone, Copy)]
pub struct StatusFlags {
    txe: bool,   // Transmit buffer empty
    rxne: bool,  // Receive buffer not empty
    bsy: bool,   // Busy
}

// Hardware abstraction
pub trait SpiPeripheral {
    fn status(&self) -> StatusFlags;
    fn write_data(&mut self, data: u8);
    fn read_data(&mut self) -> u8;
    fn is_busy(&self) -> bool {
        self.status().bsy
    }
}

// Timeout configuration
#[derive(Clone, Copy)]
pub struct TimeoutConfig {
    pub total_timeout_ms: u64,
    pub byte_timeout_ms: u64,
}

impl Default for TimeoutConfig {
    fn default() -> Self {
        Self {
            total_timeout_ms: 100,
            byte_timeout_ms: 10,
        }
    }
}

// Main SPI driver with timeout support
pub struct SpiDriver<P: SpiPeripheral, T: Timer> {
    peripheral: P,
    timer: T,
    timeout_config: TimeoutConfig,
}

impl<P: SpiPeripheral, T: Timer> SpiDriver<P, T> {
    pub fn new(peripheral: P, timer: T, timeout_config: TimeoutConfig) -> Self {
        Self {
            peripheral,
            timer,
            timeout_config,
        }
    }

    // Wait for a specific flag with timeout
    fn wait_for_flag<F>(&self, mut check: F, timeout_ms: u64) -> SpiResult<()>
    where
        F: FnMut(&StatusFlags) -> bool,
    {
        let start = self.timer.now();

        loop {
            let status = self.peripheral.status();
            
            if check(&status) {
                return Ok(());
            }

            if self.timer.elapsed_ms(start) >= timeout_ms {
                return Err(SpiError::Timeout);
            }

            // Optional: yield in RTOS environment
            // cortex_m::asm::wfi();
        }
    }

    // Transmit a single byte
    pub fn transmit_byte(&mut self, data: u8) -> SpiResult<()> {
        // Wait for TX buffer empty
        self.wait_for_flag(
            |s| s.txe,
            self.timeout_config.byte_timeout_ms,
        )?;

        // Write data
        self.peripheral.write_data(data);

        // Wait for transmission complete
        self.wait_for_flag(
            |s| !s.bsy,
            self.timeout_config.byte_timeout_ms,
        )
    }

    // Receive a single byte
    pub fn receive_byte(&mut self) -> SpiResult<u8> {
        // Send dummy byte to generate clock
        self.wait_for_flag(
            |s| s.txe,
            self.timeout_config.byte_timeout_ms,
        )?;
        self.peripheral.write_data(0xFF);

        // Wait for received data
        self.wait_for_flag(
            |s| s.rxne,
            self.timeout_config.byte_timeout_ms,
        )?;

        Ok(self.peripheral.read_data())
    }

    // Transmit buffer with timeout
    pub fn transmit(&mut self, data: &[u8]) -> SpiResult<()> {
        if data.is_empty() {
            return Err(SpiError::InvalidParameter);
        }

        let start = self.timer.now();

        for byte in data {
            // Check overall timeout
            if self.timer.elapsed_ms(start) >= self.timeout_config.total_timeout_ms {
                return Err(SpiError::Timeout);
            }

            self.transmit_byte(*byte)?;
        }

        Ok(())
    }

    // Full-duplex transfer with timeout
    pub fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> SpiResult<()> {
        if tx_data.len() != rx_data.len() || tx_data.is_empty() {
            return Err(SpiError::InvalidParameter);
        }

        let start = self.timer.now();
        let byte_timeout = self.timeout_config.byte_timeout_ms;

        for (tx_byte, rx_byte) in tx_data.iter().zip(rx_data.iter_mut()) {
            if self.timer.elapsed_ms(start) >= self.timeout_config.total_timeout_ms {
                return Err(SpiError::Timeout);
            }

            // Wait for TX buffer empty
            self.wait_for_flag(|s| s.txe, byte_timeout)?;
            self.peripheral.write_data(*tx_byte);

            // Wait for received data
            self.wait_for_flag(|s| s.rxne, byte_timeout)?;
            *rx_byte = self.peripheral.read_data();
        }

        Ok(())
    }

    // Transfer with automatic retry
    pub fn transfer_with_retry(
        &mut self,
        tx_data: &[u8],
        rx_data: &mut [u8],
        max_retries: u8,
        retry_delay_ms: u64,
    ) -> SpiResult<()> {
        let mut attempts = 0;

        loop {
            match self.transfer(tx_data, rx_data) {
                Ok(()) => return Ok(()),
                Err(e) if attempts < max_retries => {
                    attempts += 1;
                    
                    // Delay before retry
                    let delay_start = self.timer.now();
                    while self.timer.elapsed_ms(delay_start) < retry_delay_ms {
                        // Busy wait
                    }
                }
                Err(e) => return Err(e),
            }
        }
    }

    // Get reference to timer for custom operations
    pub fn timer(&self) -> &T {
        &self.timer
    }
}

// Advanced: Non-blocking SPI with timeout state machine
#[derive(Debug, Clone, Copy, PartialEq)]
enum TransferState {
    Idle,
    WaitingTxEmpty,
    Transmitting,
    WaitingRxData,
    Complete,
    TimedOut,
}

pub struct NonBlockingSpi<P: SpiPeripheral, T: Timer> {
    peripheral: P,
    timer: T,
    state: TransferState,
    start_time: u64,
    timeout_ms: u64,
    current_index: usize,
}

impl<P: SpiPeripheral, T: Timer> NonBlockingSpi<P, T> {
    pub fn new(peripheral: P, timer: T, timeout_ms: u64) -> Self {
        Self {
            peripheral,
            timer,
            state: TransferState::Idle,
            start_time: 0,
            timeout_ms,
            current_index: 0,
        }
    }

    // Start a non-blocking transfer
    pub fn start_transfer(&mut self, _tx_len: usize) -> SpiResult<()> {
        if self.state != TransferState::Idle {
            return Err(SpiError::Busy);
        }

        self.state = TransferState::WaitingTxEmpty;
        self.start_time = self.timer.now();
        self.current_index = 0;
        Ok(())
    }

    // Poll the transfer - call repeatedly
    pub fn poll(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> SpiResult<bool> {
        // Check timeout
        if self.timer.elapsed_ms(self.start_time) >= self.timeout_ms {
            self.state = TransferState::TimedOut;
            return Err(SpiError::Timeout);
        }

        match self.state {
            TransferState::Idle => Ok(true),
            TransferState::WaitingTxEmpty => {
                if self.peripheral.status().txe {
                    if self.current_index < tx_data.len() {
                        self.peripheral.write_data(tx_data[self.current_index]);
                        self.state = TransferState::WaitingRxData;
                    }
                }
                Ok(false)
            }
            TransferState::WaitingRxData => {
                if self.peripheral.status().rxne {
                    if self.current_index < rx_data.len() {
                        rx_data[self.current_index] = self.peripheral.read_data();
                        self.current_index += 1;

                        if self.current_index >= tx_data.len() {
                            self.state = TransferState::Complete;
                            Ok(true)
                        } else {
                            self.state = TransferState::WaitingTxEmpty;
                            Ok(false)
                        }
                    } else {
                        Ok(false)
                    }
                } else {
                    Ok(false)
                }
            }
            TransferState::Complete | TransferState::TimedOut => Ok(true),
            _ => Ok(false),
        }
    }

    pub fn is_complete(&self) -> bool {
        self.state == TransferState::Complete
    }

    pub fn reset(&mut self) {
        self.state = TransferState::Idle;
        self.current_index = 0;
    }
}

// Example: SPI Flash operations with timeout
const FLASH_CMD_READ: u8 = 0x03;
const FLASH_CMD_WRITE: u8 = 0x02;

pub struct SpiFlash<P: SpiPeripheral, T: Timer> {
    spi: SpiDriver<P, T>,
}

impl<P: SpiPeripheral, T: Timer> SpiFlash<P, T> {
    pub fn new(spi: SpiDriver<P, T>) -> Self {
        Self { spi }
    }

    pub fn read(&mut self, address: u32, buffer: &mut [u8]) -> SpiResult<()> {
        let cmd = [
            FLASH_CMD_READ,
            ((address >> 16) & 0xFF) as u8,
            ((address >> 8) & 0xFF) as u8,
            (address & 0xFF) as u8,
        ];

        // Send command
        self.spi.transmit(&cmd)?;

        // Read data byte by byte with timeout
        for byte in buffer.iter_mut() {
            *byte = self.spi.receive_byte()?;
        }

        Ok(())
    }

    pub fn write_page(&mut self, address: u32, data: &[u8]) -> SpiResult<()> {
        if data.len() > 256 {
            return Err(SpiError::InvalidParameter);
        }

        let mut cmd = [0u8; 260]; // 4 byte command + 256 data
        cmd[0] = FLASH_CMD_WRITE;
        cmd[1] = ((address >> 16) & 0xFF) as u8;
        cmd[2] = ((address >> 8) & 0xFF) as u8;
        cmd[3] = (address & 0xFF) as u8;
        cmd[4..4 + data.len()].copy_from_slice(data);

        self.spi.transmit(&cmd[..4 + data.len()])
    }
}

// Mock implementations for testing
#[cfg(test)]
mod mock {
    use super::*;

    pub struct MockTimer {
        current_time: core::cell::Cell<u64>,
    }

    impl MockTimer {
        pub fn new() -> Self {
            Self {
                current_time: core::cell::Cell::new(0),
            }
        }

        pub fn advance(&self, ms: u64) {
            self.current_time.set(self.current_time.get() + ms);
        }
    }

    impl Timer for MockTimer {
        fn now(&self) -> u64 {
            self.current_time.get()
        }
    }

    pub struct MockSpi {
        ready: core::cell::Cell<bool>,
    }

    impl MockSpi {
        pub fn new() -> Self {
            Self {
                ready: core::cell::Cell::new(true),
            }
        }
    }

    impl SpiPeripheral for MockSpi {
        fn status(&self) -> StatusFlags {
            StatusFlags {
                txe: self.ready.get(),
                rxne: self.ready.get(),
                bsy: false,
            }
        }

        fn write_data(&mut self, _data: u8) {}
        fn read_data(&mut self) -> u8 {
            0x42
        }
    }
}