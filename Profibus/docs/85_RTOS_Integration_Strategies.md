# RTOS Integration Strategies for Profibus

## Overview

Integrating Profibus protocol stacks with Real-Time Operating Systems (RTOS) is critical for industrial automation applications that require deterministic communication timing, task prioritization, and efficient resource management. This integration ensures that Profibus operations meet strict real-time constraints while coexisting with other system tasks.

## Key Integration Considerations

### 1. **Task Architecture**
Profibus operations typically require multiple concurrent tasks:
- **Cyclic data exchange task** - Regular polling/transmission
- **Acyclic service handler** - Request/response processing
- **Diagnostics and monitoring** - Error detection and recovery
- **Watchdog management** - Ensuring communication health

### 2. **Timing Requirements**
- **Bus cycle time** - Typical range: 1ms to 100ms
- **Jitter tolerance** - Usually < 1% of cycle time
- **Response deadlines** - Critical for safety-relevant applications
- **Interrupt latency** - Must be minimized for token rotation

### 3. **Synchronization Mechanisms**
- Semaphores for resource protection
- Message queues for inter-task communication
- Event flags for state synchronization
- Mutexes for shared data structures

## FreeRTOS Integration Example

### C Implementation

```c
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"

// Profibus task priorities
#define PROFIBUS_CYCLIC_PRIORITY    (configMAX_PRIORITIES - 1)
#define PROFIBUS_ACYCLIC_PRIORITY   (configMAX_PRIORITIES - 2)
#define PROFIBUS_DIAG_PRIORITY      (configMAX_PRIORITIES - 3)

// Profibus context structure
typedef struct {
    SemaphoreHandle_t bus_mutex;
    QueueHandle_t rx_queue;
    QueueHandle_t tx_queue;
    TaskHandle_t cyclic_task;
    TaskHandle_t acyclic_task;
    uint8_t station_address;
    volatile bool running;
} profibus_context_t;

// Telegram structure
typedef struct {
    uint8_t src_addr;
    uint8_t dst_addr;
    uint8_t data[246];
    uint16_t data_len;
    uint8_t fc;  // Function code
} profibus_telegram_t;

// Initialize Profibus RTOS integration
profibus_context_t* profibus_init(uint8_t station_addr) {
    profibus_context_t *ctx = pvPortMalloc(sizeof(profibus_context_t));
    
    ctx->bus_mutex = xSemaphoreCreateMutex();
    ctx->rx_queue = xQueueCreate(16, sizeof(profibus_telegram_t));
    ctx->tx_queue = xQueueCreate(16, sizeof(profibus_telegram_t));
    ctx->station_address = station_addr;
    ctx->running = true;
    
    return ctx;
}

// Cyclic data exchange task
void profibus_cyclic_task(void *pvParameters) {
    profibus_context_t *ctx = (profibus_context_t*)pvParameters;
    profibus_telegram_t telegram;
    TickType_t last_wake_time = xTaskGetTickCount();
    const TickType_t cycle_time = pdMS_TO_TICKS(10); // 10ms cycle
    
    while (ctx->running) {
        // Acquire bus access
        if (xSemaphoreTake(ctx->bus_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            
            // Send cyclic output data
            if (xQueueReceive(ctx->tx_queue, &telegram, 0) == pdTRUE) {
                profibus_send_telegram(&telegram);
            }
            
            // Read cyclic input data
            if (profibus_receive_telegram(&telegram) == 0) {
                xQueueSend(ctx->rx_queue, &telegram, 0);
            }
            
            xSemaphoreGive(ctx->bus_mutex);
        }
        
        // Wait for next cycle (deterministic timing)
        vTaskDelayUntil(&last_wake_time, cycle_time);
    }
}

// Acyclic request/response handler
void profibus_acyclic_task(void *pvParameters) {
    profibus_context_t *ctx = (profibus_context_t*)pvParameters;
    profibus_telegram_t request, response;
    
    while (ctx->running) {
        // Wait for acyclic requests (blocking)
        if (profibus_wait_for_acyclic_request(&request, pdMS_TO_TICKS(100))) {
            
            // Acquire bus for response
            if (xSemaphoreTake(ctx->bus_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
                
                // Process request and build response
                profibus_process_acyclic_request(&request, &response);
                profibus_send_telegram(&response);
                
                xSemaphoreGive(ctx->bus_mutex);
            }
        }
    }
}

// Start Profibus tasks
void profibus_start_tasks(profibus_context_t *ctx) {
    xTaskCreate(profibus_cyclic_task, "PB_Cyclic", 
                2048, ctx, PROFIBUS_CYCLIC_PRIORITY, &ctx->cyclic_task);
    
    xTaskCreate(profibus_acyclic_task, "PB_Acyclic", 
                2048, ctx, PROFIBUS_ACYCLIC_PRIORITY, &ctx->acyclic_task);
}

// Hardware interrupt handler for UART/SPC reception
void UART_IRQHandler(void) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    profibus_telegram_t telegram;
    
    // Read from hardware
    if (uart_read_telegram(&telegram)) {
        // Send to queue from ISR
        xQueueSendFromISR(profibus_rx_queue, &telegram, 
                         &xHigherPriorityTaskWoken);
    }
    
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}
```

### C++ Implementation with Modern Features

```cpp
#include "FreeRTOS.h"
#include "task.h"
#include <memory>
#include <functional>
#include <array>
#include <optional>

class ProfibusRTOSStack {
private:
    static constexpr size_t MAX_TELEGRAM_SIZE = 246;
    
    struct Config {
        uint8_t station_address;
        uint16_t cycle_time_ms;
        size_t queue_depth;
    };
    
    Config config_;
    SemaphoreHandle_t bus_mutex_;
    QueueHandle_t rx_queue_;
    QueueHandle_t tx_queue_;
    TaskHandle_t cyclic_handle_;
    TaskHandle_t acyclic_handle_;
    std::atomic<bool> running_{true};
    
public:
    explicit ProfibusRTOSStack(const Config& config) 
        : config_(config) {
        
        bus_mutex_ = xSemaphoreCreateMutex();
        rx_queue_ = xQueueCreate(config.queue_depth, sizeof(Telegram));
        tx_queue_ = xQueueCreate(config.queue_depth, sizeof(Telegram));
    }
    
    ~ProfibusRTOSStack() {
        stop();
        vSemaphoreDelete(bus_mutex_);
        vQueueDelete(rx_queue_);
        vQueueDelete(tx_queue_);
    }
    
    struct Telegram {
        uint8_t src_addr;
        uint8_t dst_addr;
        std::array<uint8_t, MAX_TELEGRAM_SIZE> data;
        uint16_t length;
        uint8_t function_code;
    };
    
    void start() {
        xTaskCreate([](void* param) {
            static_cast<ProfibusRTOSStack*>(param)->cyclicTask();
        }, "PB_Cyclic", 4096, this, configMAX_PRIORITIES - 1, &cyclic_handle_);
        
        xTaskCreate([](void* param) {
            static_cast<ProfibusRTOSStack*>(param)->acyclicTask();
        }, "PB_Acyclic", 4096, this, configMAX_PRIORITIES - 2, &acyclic_handle_);
    }
    
    void stop() {
        running_ = false;
        if (cyclic_handle_) vTaskDelete(cyclic_handle_);
        if (acyclic_handle_) vTaskDelete(acyclic_handle_);
    }
    
    bool sendTelegram(const Telegram& telegram, TickType_t timeout) {
        return xQueueSend(tx_queue_, &telegram, timeout) == pdTRUE;
    }
    
    std::optional<Telegram> receiveTelegram(TickType_t timeout) {
        Telegram telegram;
        if (xQueueReceive(rx_queue_, &telegram, timeout) == pdTRUE) {
            return telegram;
        }
        return std::nullopt;
    }
    
private:
    void cyclicTask() {
        TickType_t last_wake = xTaskGetTickCount();
        const TickType_t cycle = pdMS_TO_TICKS(config_.cycle_time_ms);
        
        while (running_) {
            if (xSemaphoreTake(bus_mutex_, pdMS_TO_TICKS(5)) == pdTRUE) {
                processCyclicData();
                xSemaphoreGive(bus_mutex_);
            }
            
            vTaskDelayUntil(&last_wake, cycle);
        }
    }
    
    void acyclicTask() {
        while (running_) {
            processAcyclicRequests();
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    
    void processCyclicData() {
        // Implementation details
    }
    
    void processAcyclicRequests() {
        // Implementation details
    }
};
```

## Rust Implementation with RTIC Framework

```rust
#![no_std]
#![no_main]

use rtic::app;
use heapless::{Vec, spsc::{Queue, Producer, Consumer}};
use core::sync::atomic::{AtomicBool, Ordering};

const MAX_TELEGRAM_LEN: usize = 246;
const QUEUE_SIZE: usize = 16;

#[derive(Clone, Copy)]
pub struct Telegram {
    src_addr: u8,
    dst_addr: u8,
    data: [u8; MAX_TELEGRAM_LEN],
    length: u16,
    function_code: u8,
}

pub struct ProfibusConfig {
    station_address: u8,
    cycle_time_us: u32,
    bus_speed: u32,
}

#[app(device = stm32f4xx_hal::pac, dispatchers = [EXTI0, EXTI1])]
mod app {
    use super::*;
    use systick_monotonic::*;
    
    #[shared]
    struct Shared {
        tx_queue: Queue<Telegram, QUEUE_SIZE>,
        rx_queue: Queue<Telegram, QUEUE_SIZE>,
    }
    
    #[local]
    struct Local {
        uart: UART,
        profibus_state: ProfibusState,
    }
    
    #[monotonic(binds = SysTick, default = true)]
    type MonoTimer = Systick<1000>; // 1kHz
    
    #[init]
    fn init(ctx: init::Context) -> (Shared, Local, init::Monotonics) {
        let mono = Systick::new(ctx.core.SYST, 168_000_000);
        
        // Start cyclic task
        cyclic_task::spawn().ok();
        
        (
            Shared {
                tx_queue: Queue::new(),
                rx_queue: Queue::new(),
            },
            Local {
                uart: init_uart(),
                profibus_state: ProfibusState::new(),
            },
            init::Monotonics(mono),
        )
    }
    
    // Cyclic data exchange (runs every 10ms)
    #[task(shared = [tx_queue, rx_queue], local = [profibus_state], priority = 3)]
    fn cyclic_task(mut ctx: cyclic_task::Context) {
        // Process cyclic I/O
        ctx.shared.tx_queue.lock(|tx_q| {
            if let Some(telegram) = tx_q.dequeue() {
                send_telegram(&telegram);
            }
        });
        
        // Receive data
        if let Some(telegram) = receive_telegram() {
            ctx.shared.rx_queue.lock(|rx_q| {
                rx_q.enqueue(telegram).ok();
            });
        }
        
        // Reschedule for next cycle
        cyclic_task::spawn_after(10.millis()).ok();
    }
    
    // Acyclic request handler (lower priority)
    #[task(shared = [tx_queue, rx_queue], priority = 2)]
    fn acyclic_task(mut ctx: acyclic_task::Context) {
        ctx.shared.rx_queue.lock(|rx_q| {
            if let Some(request) = rx_q.dequeue() {
                if is_acyclic_request(&request) {
                    let response = process_acyclic_request(&request);
                    ctx.shared.tx_queue.lock(|tx_q| {
                        tx_q.enqueue(response).ok();
                    });
                }
            }
        });
    }
    
    // UART interrupt handler
    #[task(binds = USART1, shared = [rx_queue], local = [uart], priority = 4)]
    fn uart_rx(mut ctx: uart_rx::Context) {
        if let Some(telegram) = ctx.local.uart.read_telegram() {
            ctx.shared.rx_queue.lock(|q| {
                q.enqueue(telegram).ok();
            });
            
            // Trigger acyclic handler
            acyclic_task::spawn().ok();
        }
    }
}

// Helper functions
fn send_telegram(telegram: &Telegram) {
    // Hardware-specific send implementation
}

fn receive_telegram() -> Option<Telegram> {
    // Hardware-specific receive implementation
    None
}

fn is_acyclic_request(telegram: &Telegram) -> bool {
    // Check function code for acyclic services
    telegram.function_code & 0x40 != 0
}

fn process_acyclic_request(request: &Telegram) -> Telegram {
    // Process and build response
    let mut response = *request;
    response.src_addr = request.dst_addr;
    response.dst_addr = request.src_addr;
    response
}

struct ProfibusState {
    // State management
}

impl ProfibusState {
    fn new() -> Self {
        Self {}
    }
}
```

## VxWorks Integration Example

```c
#include <vxWorks.h>
#include <taskLib.h>
#include <msgQLib.h>
#include <semLib.h>
#include <sysLib.h>

#define PROFIBUS_CYCLIC_PRIORITY    50
#define PROFIBUS_ACYCLIC_PRIORITY   100
#define PROFIBUS_STACK_SIZE         20000

typedef struct {
    SEM_ID bus_sem;
    MSG_Q_ID rx_queue;
    MSG_Q_ID tx_queue;
    TASK_ID cyclic_tid;
    TASK_ID acyclic_tid;
    BOOL running;
} PROFIBUS_CTRL;

STATUS profibus_vxworks_init(PROFIBUS_CTRL *ctrl) {
    ctrl->bus_sem = semMCreate(SEM_Q_PRIORITY | SEM_INVERSION_SAFE);
    ctrl->rx_queue = msgQCreate(16, sizeof(PROFIBUS_TELEGRAM), MSG_Q_FIFO);
    ctrl->tx_queue = msgQCreate(16, sizeof(PROFIBUS_TELEGRAM), MSG_Q_FIFO);
    ctrl->running = TRUE;
    
    if (!ctrl->bus_sem || !ctrl->rx_queue || !ctrl->tx_queue) {
        return ERROR;
    }
    
    // Spawn tasks
    ctrl->cyclic_tid = taskSpawn("tPbCyc", PROFIBUS_CYCLIC_PRIORITY,
                                  0, PROFIBUS_STACK_SIZE,
                                  (FUNCPTR)profibus_cyclic_task,
                                  (_Vx_usr_arg_t)ctrl, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    
    ctrl->acyclic_tid = taskSpawn("tPbAcyc", PROFIBUS_ACYCLIC_PRIORITY,
                                   0, PROFIBUS_STACK_SIZE,
                                   (FUNCPTR)profibus_acyclic_task,
                                   (_Vx_usr_arg_t)ctrl, 0, 0, 0, 0, 0, 0, 0, 0, 0);
    
    return (ctrl->cyclic_tid != TASK_ID_ERROR && 
            ctrl->acyclic_tid != TASK_ID_ERROR) ? OK : ERROR;
}

void profibus_cyclic_task(PROFIBUS_CTRL *ctrl) {
    PROFIBUS_TELEGRAM telegram;
    int tick_rate = sysClkRateGet();
    int ticks_per_cycle = tick_rate / 100; // 10ms cycle
    
    while (ctrl->running) {
        UINT64 start_tick = tickGet();
        
        if (semTake(ctrl->bus_sem, WAIT_FOREVER) == OK) {
            // Send and receive cyclic data
            if (msgQReceive(ctrl->tx_queue, (char*)&telegram, 
                           sizeof(telegram), NO_WAIT) != ERROR) {
                profibus_hw_send(&telegram);
            }
            
            semGive(ctrl->bus_sem);
        }
        
        // Wait for next cycle
        taskDelay(ticks_per_cycle - (tickGet() - start_tick));
    }
}
```

## Summary

**RTOS integration for Profibus** requires careful consideration of:

1. **Task Prioritization**: Cyclic tasks need highest priority to meet deterministic timing
2. **Synchronization**: Proper use of mutexes/semaphores prevents bus conflicts
3. **Interrupt Handling**: Efficient ISRs minimize latency and jitter
4. **Queue Management**: Decouples telegram processing from hardware timing
5. **Memory Management**: Static allocation preferred for deterministic behavior
6. **Error Recovery**: Watchdog and diagnostic tasks ensure system reliability

The choice of RTOS affects implementation details, but core principles remain:
- **FreeRTOS**: Lightweight, portable, good for resource-constrained systems
- **VxWorks**: Industrial-grade, deterministic, extensive tooling
- **RTIC (Rust)**: Zero-cost abstractions, memory safety, compile-time guarantees

Proper integration ensures that Profibus communication meets real-time requirements while coexisting efficiently with application logic.