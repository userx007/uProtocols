# Real-Time Considerations in I2C Communication

## Overview

Real-time I2C communication in embedded systems requires careful consideration of timing constraints, deterministic behavior, and meeting hard deadlines. This is critical in applications like industrial control, automotive systems, and medical devices where missing a deadline can have serious consequences.

## Key Challenges

**Clock Stretching**: Slave devices can hold SCL low to pause communication, introducing unpredictable delays.

**Interrupt Latency**: Response time to I2C events affects timing predictability.

**Bus Arbitration**: Multi-master scenarios introduce non-deterministic behavior.

**DMA Conflicts**: Direct Memory Access can interfere with CPU-based I2C operations.

**Priority Inversion**: Lower-priority tasks holding I2C resources can block higher-priority tasks.

## Real-Time Design Strategies

### 1. Deterministic Timing Analysis

Calculate worst-case execution time (WCET) for I2C transactions:

```
WCET = (9 bits × number of bytes) / I2C clock frequency + overhead
```

For 100 kHz I2C transmitting 10 bytes:
```
WCET = (9 × 10) / 100,000 + interrupt overhead ≈ 0.9ms + overhead
```

### 2. Priority-Based Scheduling

Assign appropriate task priorities and use priority inheritance protocols to prevent priority inversion.

### 3. Bounded Clock Stretching

Configure maximum clock stretch timeout or avoid devices with unbounded clock stretching.

## C/C++ Implementation Examples

### Example 1: Interrupt-Driven I2C with Deadline Monitoring (STM32)

```c
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

#define I2C_TIMEOUT_MS 10
#define MAX_TRANSACTION_TIME_US 1000

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint32_t deadline_us;
    uint32_t start_time_us;
    bool deadline_missed;
    volatile bool transaction_complete;
} I2CRealTimeContext_t;

static I2CRealTimeContext_t i2c_context;

// High-resolution microsecond timer
static inline uint32_t get_time_us(void) {
    return HAL_GetTick() * 1000 + (SysTick->LOAD - SysTick->VAL) / (SystemCoreClock / 1000000);
}

// Initialize real-time I2C context
void I2C_RT_Init(I2C_HandleTypeDef *hi2c, uint32_t deadline_us) {
    i2c_context.hi2c = hi2c;
    i2c_context.deadline_us = deadline_us;
    i2c_context.deadline_missed = false;
    i2c_context.transaction_complete = false;
}

// Non-blocking I2C write with deadline checking
int I2C_RT_Write(uint8_t dev_addr, uint8_t *data, uint16_t size) {
    i2c_context.start_time_us = get_time_us();
    i2c_context.transaction_complete = false;
    
    // Start interrupt-driven transmission
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_IT(
        i2c_context.hi2c, 
        dev_addr << 1, 
        data, 
        size
    );
    
    if (status != HAL_OK) {
        return -1;
    }
    
    // Wait with deadline monitoring
    while (!i2c_context.transaction_complete) {
        uint32_t elapsed = get_time_us() - i2c_context.start_time_us;
        if (elapsed > i2c_context.deadline_us) {
            i2c_context.deadline_missed = true;
            HAL_I2C_Master_Abort_IT(i2c_context.hi2c, dev_addr << 1);
            return -2; // Deadline missed
        }
    }
    
    return 0;
}

// I2C interrupt callback
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == i2c_context.hi2c) {
        uint32_t elapsed = get_time_us() - i2c_context.start_time_us;
        i2c_context.transaction_complete = true;
        
        // Log if we're close to deadline
        if (elapsed > (i2c_context.deadline_us * 90 / 100)) {
            // Warning: transaction took >90% of deadline
        }
    }
}

void HAL_I2C_ErrorCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == i2c_context.hi2c) {
        i2c_context.transaction_complete = true;
        i2c_context.deadline_missed = true;
    }
}
```

### Example 2: DMA-Based I2C for Predictable CPU Usage

```c
#include "stm32f4xx_hal.h"
#include <string.h>

#define I2C_DMA_BUFFER_SIZE 64

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint8_t tx_buffer[I2C_DMA_BUFFER_SIZE];
    uint8_t rx_buffer[I2C_DMA_BUFFER_SIZE];
    volatile bool dma_complete;
    uint32_t max_wait_cycles;
} I2C_DMA_Context_t;

static I2C_DMA_Context_t dma_ctx;

void I2C_DMA_Init(I2C_HandleTypeDef *hi2c, uint32_t max_wait_us) {
    dma_ctx.hi2c = hi2c;
    dma_ctx.dma_complete = false;
    // Convert microseconds to CPU cycles for tight timing
    dma_ctx.max_wait_cycles = max_wait_us * (SystemCoreClock / 1000000);
}

// DMA transaction with cycle-counted timeout
int I2C_DMA_WriteRead(uint8_t dev_addr, uint8_t *tx_data, uint16_t tx_size,
                       uint8_t *rx_data, uint16_t rx_size) {
    if (tx_size > I2C_DMA_BUFFER_SIZE || rx_size > I2C_DMA_BUFFER_SIZE) {
        return -1;
    }
    
    memcpy(dma_ctx.tx_buffer, tx_data, tx_size);
    dma_ctx.dma_complete = false;
    
    // Write phase
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit_DMA(
        dma_ctx.hi2c,
        dev_addr << 1,
        dma_ctx.tx_buffer,
        tx_size
    );
    
    if (status != HAL_OK) return -2;
    
    // Wait for write with cycle counting
    uint32_t start_cycles = DWT->CYCCNT;
    while (!dma_ctx.dma_complete) {
        if ((DWT->CYCCNT - start_cycles) > dma_ctx.max_wait_cycles) {
            HAL_I2C_Master_Abort_IT(dma_ctx.hi2c, dev_addr << 1);
            return -3; // Timeout
        }
    }
    
    // Read phase
    dma_ctx.dma_complete = false;
    status = HAL_I2C_Master_Receive_DMA(
        dma_ctx.hi2c,
        dev_addr << 1,
        dma_ctx.rx_buffer,
        rx_size
    );
    
    if (status != HAL_OK) return -4;
    
    start_cycles = DWT->CYCCNT;
    while (!dma_ctx.dma_complete) {
        if ((DWT->CYCCNT - start_cycles) > dma_ctx.max_wait_cycles) {
            HAL_I2C_Master_Abort_IT(dma_ctx.hi2c, dev_addr << 1);
            return -5;
        }
    }
    
    memcpy(rx_data, dma_ctx.rx_buffer, rx_size);
    return 0;
}

void HAL_I2C_MemTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == dma_ctx.hi2c) {
        dma_ctx.dma_complete = true;
    }
}

void HAL_I2C_MemRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    if (hi2c == dma_ctx.hi2c) {
        dma_ctx.dma_complete = true;
    }
}
```

### Example 3: RTOS Integration with Priority Inheritance

```c
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"

typedef struct {
    I2C_HandleTypeDef *hi2c;
    SemaphoreHandle_t mutex;
    SemaphoreHandle_t completion_sem;
    TickType_t max_wait_ticks;
} I2C_RTOS_Context_t;

static I2C_RTOS_Context_t rtos_ctx;

void I2C_RTOS_Init(I2C_HandleTypeDef *hi2c, uint32_t timeout_ms) {
    rtos_ctx.hi2c = hi2c;
    // Priority inheritance mutex to prevent priority inversion
    rtos_ctx.mutex = xSemaphoreCreateMutex();
    rtos_ctx.completion_sem = xSemaphoreCreateBinary();
    rtos_ctx.max_wait_ticks = pdMS_TO_TICKS(timeout_ms);
}

int I2C_RTOS_Transaction(uint8_t dev_addr, uint8_t *tx_data, uint16_t tx_size,
                          uint8_t *rx_data, uint16_t rx_size) {
    // Acquire mutex with priority inheritance
    if (xSemaphoreTake(rtos_ctx.mutex, rtos_ctx.max_wait_ticks) != pdTRUE) {
        return -1; // Couldn't acquire bus
    }
    
    // Perform transaction
    HAL_StatusTypeDef status;
    
    if (tx_size > 0) {
        status = HAL_I2C_Master_Transmit_IT(
            rtos_ctx.hi2c,
            dev_addr << 1,
            tx_data,
            tx_size
        );
        
        if (status != HAL_OK) {
            xSemaphoreGive(rtos_ctx.mutex);
            return -2;
        }
        
        // Wait for completion
        if (xSemaphoreTake(rtos_ctx.completion_sem, rtos_ctx.max_wait_ticks) != pdTRUE) {
            xSemaphoreGive(rtos_ctx.mutex);
            return -3; // Timeout
        }
    }
    
    if (rx_size > 0) {
        status = HAL_I2C_Master_Receive_IT(
            rtos_ctx.hi2c,
            dev_addr << 1,
            rx_data,
            rx_size
        );
        
        if (status != HAL_OK) {
            xSemaphoreGive(rtos_ctx.mutex);
            return -4;
        }
        
        if (xSemaphoreTake(rtos_ctx.completion_sem, rtos_ctx.max_wait_ticks) != pdTRUE) {
            xSemaphoreGive(rtos_ctx.mutex);
            return -5;
        }
    }
    
    xSemaphoreGive(rtos_ctx.mutex);
    return 0;
}

// ISR callbacks
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (hi2c == rtos_ctx.hi2c) {
        xSemaphoreGiveFromISR(rtos_ctx.completion_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

void HAL_I2C_MasterRxCpltCallback(I2C_HandleTypeDef *hi2c) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (hi2c == rtos_ctx.hi2c) {
        xSemaphoreGiveFromISR(rtos_ctx.completion_sem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}
```

## Rust Implementation Examples

### Example 1: Async I2C with Timeout using Embassy

```rust
#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::{i2c, time::Hertz, bind_interrupts, peripherals};
use embassy_time::{Duration, Timer, with_timeout};
use embedded_hal_async::i2c::I2c;

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
});

pub struct RealTimeI2c<'d> {
    i2c: i2c::I2c<'d, i2c::Async>,
    deadline: Duration,
    missed_deadlines: u32,
}

impl<'d> RealTimeI2c<'d> {
    pub fn new(i2c: i2c::I2c<'d, i2c::Async>, deadline_ms: u64) -> Self {
        Self {
            i2c,
            deadline: Duration::from_millis(deadline_ms),
            missed_deadlines: 0,
        }
    }
    
    pub async fn write_with_deadline(
        &mut self,
        address: u8,
        data: &[u8],
    ) -> Result<(), RealTimeError> {
        let start = embassy_time::Instant::now();
        
        let result = with_timeout(self.deadline, self.i2c.write(address, data)).await;
        
        match result {
            Ok(Ok(())) => {
                let elapsed = start.elapsed();
                if elapsed > self.deadline * 90 / 100 {
                    // Log warning: close to deadline
                }
                Ok(())
            }
            Ok(Err(e)) => Err(RealTimeError::I2cError(e)),
            Err(_) => {
                self.missed_deadlines += 1;
                Err(RealTimeError::DeadlineMissed)
            }
        }
    }
    
    pub async fn read_with_deadline(
        &mut self,
        address: u8,
        buffer: &mut [u8],
    ) -> Result<(), RealTimeError> {
        let result = with_timeout(
            self.deadline,
            self.i2c.read(address, buffer)
        ).await;
        
        match result {
            Ok(Ok(())) => Ok(()),
            Ok(Err(e)) => Err(RealTimeError::I2cError(e)),
            Err(_) => {
                self.missed_deadlines += 1;
                Err(RealTimeError::DeadlineMissed)
            }
        }
    }
    
    pub async fn write_read_with_deadline(
        &mut self,
        address: u8,
        tx_data: &[u8],
        rx_buffer: &mut [u8],
    ) -> Result<(), RealTimeError> {
        let result = with_timeout(
            self.deadline,
            self.i2c.write_read(address, tx_data, rx_buffer)
        ).await;
        
        match result {
            Ok(Ok(())) => Ok(()),
            Ok(Err(e)) => Err(RealTimeError::I2cError(e)),
            Err(_) => {
                self.missed_deadlines += 1;
                Err(RealTimeError::DeadlineMissed)
            }
        }
    }
    
    pub fn get_missed_deadlines(&self) -> u32 {
        self.missed_deadlines
    }
}

#[derive(Debug)]
pub enum RealTimeError {
    I2cError(i2c::Error),
    DeadlineMissed,
}

#[embassy_executor::main]
async fn main(_spawner: Spawner) {
    let p = embassy_stm32::init(Default::default());
    
    let i2c = i2c::I2c::new(
        p.I2C1,
        p.PB6,
        p.PB7,
        Irqs,
        p.DMA1_CH6,
        p.DMA1_CH7,
        Hertz(100_000),
        Default::default(),
    );
    
    let mut rt_i2c = RealTimeI2c::new(i2c, 10); // 10ms deadline
    
    loop {
        let mut data = [0x42, 0x55];
        
        match rt_i2c.write_with_deadline(0x50, &data).await {
            Ok(()) => {
                // Success
            }
            Err(RealTimeError::DeadlineMissed) => {
                // Handle deadline miss
            }
            Err(RealTimeError::I2cError(_)) => {
                // Handle I2C error
            }
        }
        
        Timer::after(Duration::from_millis(100)).await;
    }
}
```

### Example 2: Priority-Based I2C Manager with RTIC

```rust
#![no_std]
#![no_main]

use rtic::app;
use stm32f4xx_hal::{
    i2c::{I2c, Mode},
    pac,
    prelude::*,
};
use heapless::spsc::{Queue, Producer, Consumer};

const I2C_QUEUE_SIZE: usize = 16;

#[derive(Copy, Clone, Debug)]
pub enum I2cPriority {
    Critical = 3,
    High = 2,
    Normal = 1,
    Low = 0,
}

#[derive(Copy, Clone)]
pub struct I2cRequest {
    priority: I2cPriority,
    address: u8,
    data: [u8; 32],
    length: usize,
    deadline_us: u32,
}

#[app(device = stm32f4xx_hal::pac, peripherals = true, dispatchers = [EXTI0, EXTI1])]
mod app {
    use super::*;
    
    #[shared]
    struct Shared {
        i2c: I2c<pac::I2C1>,
    }
    
    #[local]
    struct Local {
        critical_queue: Queue<I2cRequest, I2C_QUEUE_SIZE>,
        high_queue: Queue<I2cRequest, I2C_QUEUE_SIZE>,
        normal_queue: Queue<I2cRequest, I2C_QUEUE_SIZE>,
    }
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let rcc = ctx.device.RCC.constrain();
        let clocks = rcc.cfgr.freeze();
        
        let gpiob = ctx.device.GPIOB.split();
        let scl = gpiob.pb6.into_alternate_open_drain();
        let sda = gpiob.pb7.into_alternate_open_drain();
        
        let i2c = I2c::new(
            ctx.device.I2C1,
            (scl, sda),
            Mode::Standard {
                frequency: 100_000.Hz(),
            },
            &clocks,
        );
        
        (
            Shared { i2c },
            Local {
                critical_queue: Queue::new(),
                high_queue: Queue::new(),
                normal_queue: Queue::new(),
            },
        )
    }
    
    #[task(shared = [i2c], local = [critical_queue, high_queue, normal_queue], priority = 3)]
    async fn i2c_manager(mut ctx: i2c_manager::Context) {
        loop {
            // Check queues in priority order
            let request = if let Some(req) = ctx.local.critical_queue.dequeue() {
                Some(req)
            } else if let Some(req) = ctx.local.high_queue.dequeue() {
                Some(req)
            } else if let Some(req) = ctx.local.normal_queue.dequeue() {
                Some(req)
            } else {
                None
            };
            
            if let Some(req) = request {
                ctx.shared.i2c.lock(|i2c| {
                    let data = &req.data[..req.length];
                    // Perform transaction with deadline monitoring
                    let _ = i2c.write(req.address, data);
                });
            }
            
            // Yield to allow other tasks to enqueue requests
            cortex_m::asm::wfi();
        }
    }
    
    #[task(priority = 2)]
    async fn critical_sensor_task(_ctx: critical_sensor_task::Context) {
        // High-priority sensor reading
        loop {
            // Queue critical I2C request
            cortex_m::asm::delay(1_000_000);
        }
    }
}
```

### Example 3: Zero-Copy DMA I2C with `embedded-hal-async`

```rust
use embassy_stm32::{i2c, dma};
use embedded_hal_async::i2c::I2c;
use core::future::Future;

pub struct ZeroCopyI2c<'d, T: i2c::Instance, TXDMA, RXDMA> {
    i2c: i2c::I2c<'d, T, TXDMA, RXDMA>,
    tx_buffer: &'static mut [u8; 256],
    rx_buffer: &'static mut [u8; 256],
}

impl<'d, T, TXDMA, RXDMA> ZeroCopyI2c<'d, T, TXDMA, RXDMA>
where
    T: i2c::Instance,
    TXDMA: dma::Channel,
    RXDMA: dma::Channel,
{
    pub fn new(
        i2c: i2c::I2c<'d, T, TXDMA, RXDMA>,
        tx_buffer: &'static mut [u8; 256],
        rx_buffer: &'static mut [u8; 256],
    ) -> Self {
        Self {
            i2c,
            tx_buffer,
            rx_buffer,
        }
    }
    
    /// Perform a write operation with deterministic DMA transfer
    pub async fn write_dma(
        &mut self,
        address: u8,
        data: &[u8],
    ) -> Result<(), i2c::Error> {
        let len = data.len().min(self.tx_buffer.len());
        self.tx_buffer[..len].copy_from_slice(&data[..len]);
        
        // DMA transfer - CPU is free during transfer
        self.i2c.write(address, &self.tx_buffer[..len]).await
    }
    
    /// Perform a read operation with deterministic DMA transfer
    pub async fn read_dma(
        &mut self,
        address: u8,
        len: usize,
    ) -> Result<&[u8], i2c::Error> {
        let len = len.min(self.rx_buffer.len());
        
        self.i2c.read(address, &mut self.rx_buffer[..len]).await?;
        
        Ok(&self.rx_buffer[..len])
    }
    
    /// Write-then-read with single DMA operation
    pub async fn write_read_dma(
        &mut self,
        address: u8,
        tx_data: &[u8],
        rx_len: usize,
    ) -> Result<&[u8], i2c::Error> {
        let tx_len = tx_data.len().min(self.tx_buffer.len());
        let rx_len = rx_len.min(self.rx_buffer.len());
        
        self.tx_buffer[..tx_len].copy_from_slice(&tx_data[..tx_len]);
        
        self.i2c
            .write_read(
                address,
                &self.tx_buffer[..tx_len],
                &mut self.rx_buffer[..rx_len],
            )
            .await?;
        
        Ok(&self.rx_buffer[..rx_len])
    }
}
```

## Best Practices for Real-Time I2C

1. **Use DMA**: Offloads CPU, provides deterministic timing
2. **Set Timeouts**: Always implement bounded waits
3. **Priority Inheritance**: Use proper RTOS primitives to prevent priority inversion
4. **Monitor Deadlines**: Track and log deadline misses for system tuning
5. **Disable Clock Stretching**: When possible, or set maximum stretch limits
6. **Avoid Polling**: Use interrupts or async/await for better CPU utilization
7. **Preallocate Buffers**: Avoid dynamic memory allocation in real-time paths
8. **Test Worst-Case**: Verify timing under maximum system load

## Timing Analysis Considerations

- Account for interrupt latency (typically 1-10 μs)
- Consider context switch overhead in RTOS (10-50 μs)
- Include DMA setup time (~1 μs)
- Factor in clock stretching (device-dependent)
- Add safety margin (20-30% of calculated WCET)

Real-time I2C requires careful system design, proper use of hardware features like DMA, and thorough testing under worst-case conditions. The examples above demonstrate practical approaches to achieving deterministic I2C communication in embedded systems.