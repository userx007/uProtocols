use std::io::{self, Write};

// SPI device trait
pub trait SpiDevice {
    fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), SpiError>;
    fn chip_select(&mut self, active: bool);
}

#[derive(Debug)]
pub enum SpiError {
    BufferOverflow,
    TransferFailed,
    InvalidParameter,
}

// Batch accumulator with builder pattern
pub struct SpiBatch<'a> {
    device: &'a mut dyn SpiDevice,
    tx_buffer: Vec<u8>,
    rx_buffer: Vec<u8>,
    max_size: usize,
}

impl<'a> SpiBatch<'a> {
    pub fn new(device: &'a mut dyn SpiDevice, max_size: usize) -> Self {
        Self {
            device,
            tx_buffer: Vec::with_capacity(max_size),
            rx_buffer: Vec::with_capacity(max_size),
            max_size,
        }
    }
    
    // Add data to batch (builder pattern)
    pub fn add(&mut self, data: &[u8]) -> Result<&mut Self, SpiError> {
        if self.tx_buffer.len() + data.len() > self.max_size {
            return Err(SpiError::BufferOverflow);
        }
        
        self.tx_buffer.extend_from_slice(data);
        Ok(self)
    }
    
    // Add single byte
    pub fn add_byte(&mut self, byte: u8) -> Result<&mut Self, SpiError> {
        if self.tx_buffer.len() >= self.max_size {
            return Err(SpiError::BufferOverflow);
        }
        
        self.tx_buffer.push(byte);
        Ok(self)
    }
    
    // Execute the batched transfer
    pub fn execute(&mut self) -> Result<(), SpiError> {
        if self.tx_buffer.is_empty() {
            return Ok(());
        }
        
        self.rx_buffer.resize(self.tx_buffer.len(), 0);
        
        self.device.chip_select(true);
        let result = self.device.transfer(&self.tx_buffer, &mut self.rx_buffer);
        self.device.chip_select(false);
        
        self.tx_buffer.clear();
        result
    }
    
    pub fn rx_data(&self) -> &[u8] {
        &self.rx_buffer
    }
    
    pub fn clear(&mut self) {
        self.tx_buffer.clear();
        self.rx_buffer.clear();
    }
    
    pub fn len(&self) -> usize {
        self.tx_buffer.len()
    }
    
    pub fn is_empty(&self) -> bool {
        self.tx_buffer.is_empty()
    }
}

// RAII batch that auto-executes on drop
pub struct AutoBatch<'a> {
    batch: SpiBatch<'a>,
}

impl<'a> AutoBatch<'a> {
    pub fn new(device: &'a mut dyn SpiDevice, max_size: usize) -> Self {
        Self {
            batch: SpiBatch::new(device, max_size),
        }
    }
    
    pub fn add(&mut self, data: &[u8]) -> Result<&mut Self, SpiError> {
        self.batch.add(data)?;
        Ok(self)
    }
}

impl<'a> Drop for AutoBatch<'a> {
    fn drop(&mut self) {
        let _ = self.batch.execute();
    }
}

// Auto-flushing batch manager
pub struct SpiBatchManager<'a> {
    batch: SpiBatch<'a>,
    flush_threshold: usize,
}

impl<'a> SpiBatchManager<'a> {
    pub fn new(device: &'a mut dyn SpiDevice, max_size: usize, flush_threshold: usize) -> Self {
        Self {
            batch: SpiBatch::new(device, max_size),
            flush_threshold,
        }
    }
    
    pub fn add(&mut self, data: &[u8]) -> Result<(), SpiError> {
        if self.batch.len() + data.len() >= self.flush_threshold {
            self.flush()?;
        }
        
        self.batch.add(data)?;
        Ok(())
    }
    
    pub fn flush(&mut self) -> Result<(), SpiError> {
        self.batch.execute()
    }
}

impl<'a> Drop for SpiBatchManager<'a> {
    fn drop(&mut self) {
        let _ = self.flush();
    }
}

// Register operations batch
pub struct RegisterBatch<'a> {
    batch: SpiBatch<'a>,
    read_addresses: Vec<u8>,
}

impl<'a> RegisterBatch<'a> {
    pub fn new(device: &'a mut dyn SpiDevice) -> Self {
        Self {
            batch: SpiBatch::new(device, 256),
            read_addresses: Vec::new(),
        }
    }
    
    pub fn queue_read(&mut self, reg_addr: u8) -> Result<&mut Self, SpiError> {
        self.batch.add_byte(reg_addr | 0x80)?;  // Read bit
        self.batch.add_byte(0x00)?;             // Dummy byte
        self.read_addresses.push(reg_addr);
        Ok(self)
    }
    
    pub fn queue_write(&mut self, reg_addr: u8, value: u8) -> Result<&mut Self, SpiError> {
        self.batch.add_byte(reg_addr & 0x7F)?;  // Write (clear read bit)
        self.batch.add_byte(value)?;
        Ok(self)
    }
    
    pub fn execute(&mut self) -> Result<Vec<(u8, u8)>, SpiError> {
        self.batch.execute()?;
        
        let mut results = Vec::new();
        let rx_data = self.batch.rx_data();
        
        for (i, &addr) in self.read_addresses.iter().enumerate() {
            let value = rx_data.get(i * 2 + 1).copied().unwrap_or(0);
            results.push((addr, value));
        }
        
        self.read_addresses.clear();
        Ok(results)
    }
}

// Zero-copy iterator-based batching
pub struct ChunkedTransfer<'a, I>
where
    I: Iterator<Item = u8>,
{
    device: &'a mut dyn SpiDevice,
    data_iter: I,
    chunk_size: usize,
}

impl<'a, I> ChunkedTransfer<'a, I>
where
    I: Iterator<Item = u8>,
{
    pub fn new(device: &'a mut dyn SpiDevice, data_iter: I, chunk_size: usize) -> Self {
        Self {
            device,
            data_iter,
            chunk_size,
        }
    }
    
    pub fn execute(&mut self) -> Result<Vec<u8>, SpiError> {
        let mut all_rx_data = Vec::new();
        let mut chunk = Vec::with_capacity(self.chunk_size);
        
        for byte in &mut self.data_iter {
            chunk.push(byte);
            
            if chunk.len() >= self.chunk_size {
                let mut rx = vec![0u8; chunk.len()];
                
                self.device.chip_select(true);
                self.device.transfer(&chunk, &mut rx)?;
                self.device.chip_select(false);
                
                all_rx_data.extend_from_slice(&rx);
                chunk.clear();
            }
        }
        
        // Send remaining data
        if !chunk.is_empty() {
            let mut rx = vec![0u8; chunk.len()];
            
            self.device.chip_select(true);
            self.device.transfer(&chunk, &mut rx)?;
            self.device.chip_select(false);
            
            all_rx_data.extend_from_slice(&rx);
        }
        
        Ok(all_rx_data)
    }
}

// Sensor data collector with type safety
pub struct SensorBatcher<'a, T> {
    manager: SpiBatchManager<'a>,
    samples: Vec<T>,
    _phantom: std::marker::PhantomData<T>,
}

impl<'a, T: Copy> SensorBatcher<'a, T> {
    pub fn new(device: &'a mut dyn SpiDevice, batch_size: usize) -> Self {
        Self {
            manager: SpiBatchManager::new(device, batch_size, batch_size / 2),
            samples: Vec::new(),
            _phantom: std::marker::PhantomData,
        }
    }
    
    pub fn queue_sample(&mut self, sensor_id: u8) -> Result<(), SpiError> {
        let cmd = [0xA0, sensor_id];
        self.manager.add(&cmd)
    }
    
    pub fn finalize(&mut self) -> Result<&[T], SpiError> {
        self.manager.flush()?;
        
        // Parse collected data (simplified)
        let rx_data = self.manager.batch.rx_data();
        let sample_size = std::mem::size_of::<T>();
        
        for chunk in rx_data.chunks_exact(sample_size) {
            let mut sample_bytes = [0u8; 16]; // Adjust size as needed
            sample_bytes[..chunk.len()].copy_from_slice(chunk);
            
            // Safety: This is simplified - real code needs proper alignment
            let sample: T = unsafe { std::ptr::read(sample_bytes.as_ptr() as *const T) };
            self.samples.push(sample);
        }
        
        Ok(&self.samples)
    }
}

// Example usage and demonstrations
#[cfg(test)]
mod tests {
    use super::*;
    
    struct MockSpi {
        cs_state: bool,
    }
    
    impl SpiDevice for MockSpi {
        fn transfer(&mut self, tx_data: &[u8], rx_data: &mut [u8]) -> Result<(), SpiError> {
            // Echo back with transformation
            for (i, &byte) in tx_data.iter().enumerate() {
                rx_data[i] = byte.wrapping_add(1);
            }
            Ok(())
        }
        
        fn chip_select(&mut self, active: bool) {
            self.cs_state = active;
        }
    }
    
    #[test]
    fn test_basic_batching() {
        let mut spi = MockSpi { cs_state: false };
        let mut batch = SpiBatch::new(&mut spi, 256);
        
        batch.add(&[0x01, 0x02, 0x03]).unwrap()
             .add(&[0x04, 0x05]).unwrap();
        
        batch.execute().unwrap();
        
        assert_eq!(batch.rx_data().len(), 5);
        println!("Batch executed with {} bytes", batch.rx_data().len());
    }
    
    #[test]
    fn test_register_batch() {
        let mut spi = MockSpi { cs_state: false };
        let mut reg_batch = RegisterBatch::new(&mut spi);
        
        reg_batch.queue_read(0x00).unwrap()
                 .queue_read(0x01).unwrap()
                 .queue_write(0x10, 0x55).unwrap();
        
        let results = reg_batch.execute().unwrap();
        
        println!("Read {} registers", results.len());
        for (addr, value) in results {
            println!("Reg 0x{:02X} = 0x{:02X}", addr, value);
        }
    }
    
    #[test]
    fn test_auto_flush() {
        let mut spi = MockSpi { cs_state: false };
        let mut manager = SpiBatchManager::new(&mut spi, 512, 256);
        
        for i in 0..100u8 {
            manager.add(&[i, i + 1, i + 2, i + 3]).unwrap();
        }
        
        // Auto-flushes on drop
    }
}