# I2C State Machine Design

## Overview

State machines are essential for robust I2C communication because I2C is an inherently asynchronous, multi-step protocol. A well-designed state machine ensures reliable transaction management, proper error handling, and clean recovery from bus failures. This architectural approach is particularly important in:

- **Interrupt-driven I2C** where transactions span multiple ISR invocations
- **Non-blocking I2C** implementations for real-time systems
- **Complex multi-master** environments
- **Error recovery** scenarios requiring coordinated state transitions

## Core Concepts

### Why State Machines for I2C?

I2C transactions involve multiple sequential steps:
1. Generate START condition
2. Send device address + R/W bit
3. Wait for ACK/NACK
4. Transfer data bytes (with ACK/NACK between each)
5. Generate STOP or repeated START

Each step can fail, requiring different recovery actions. A state machine provides:

- **Deterministic behavior**: Each state has clearly defined transitions
- **Error isolation**: Failures in one state don't corrupt others
- **Timeout handling**: Easy to implement per-state timeouts
- **Testability**: States can be tested independently
- **Maintainability**: Clear structure for complex protocols

### State Machine Types for I2C

**1. Mealy Machines**: Outputs depend on current state AND inputs (most common for I2C)
**2. Moore Machines**: Outputs depend only on current state
**3. Hierarchical State Machines**: Nested states for complex protocols (e.g., SMBus with PEC)

## Architecture Patterns

### Basic I2C Transaction States

```
IDLE → START → ADDR_TX → ADDR_ACK → DATA_TX/RX → DATA_ACK → STOP → IDLE
                    ↓          ↓           ↓            ↓
                  ERROR ← ERROR ← ──────── ERROR ← ─────┘
                    ↓
                  RECOVERY → IDLE
```

### Event-Driven vs. Polling

- **Event-driven**: State transitions triggered by hardware interrupts (efficient)
- **Polling**: State machine executed in main loop checking hardware flags (simpler)

## C Implementation Examples

### Example 1: Basic Interrupt-Driven State Machine

```c
#include <stdint.h>
#include <stdbool.h>

// I2C State Machine States
typedef enum {
    I2C_STATE_IDLE = 0,
    I2C_STATE_START,
    I2C_STATE_ADDR_TX,
    I2C_STATE_ADDR_ACK,
    I2C_STATE_DATA_TX,
    I2C_STATE_DATA_TX_ACK,
    I2C_STATE_DATA_RX,
    I2C_STATE_DATA_RX_ACK,
    I2C_STATE_STOP,
    I2C_STATE_ERROR,
    I2C_STATE_RECOVERY
} i2c_state_t;

// I2C Events (typically from hardware interrupts)
typedef enum {
    I2C_EVENT_NONE = 0,
    I2C_EVENT_START_SENT,
    I2C_EVENT_ADDR_SENT,
    I2C_EVENT_ACK_RECEIVED,
    I2C_EVENT_NACK_RECEIVED,
    I2C_EVENT_DATA_SENT,
    I2C_EVENT_DATA_RECEIVED,
    I2C_EVENT_STOP_SENT,
    I2C_EVENT_ARBITRATION_LOST,
    I2C_EVENT_BUS_ERROR,
    I2C_EVENT_TIMEOUT
} i2c_event_t;

// I2C Transaction Control Block
typedef struct {
    i2c_state_t state;
    uint8_t device_addr;
    uint8_t *tx_buffer;
    uint8_t *rx_buffer;
    uint16_t tx_len;
    uint16_t rx_len;
    uint16_t tx_index;
    uint16_t rx_index;
    bool is_read;
    volatile bool transaction_complete;
    volatile int error_code;
    uint32_t timeout_ms;
    uint32_t state_entry_time;
} i2c_tcb_t;

// Global transaction control block
static i2c_tcb_t g_i2c_tcb = {0};

// Hardware abstraction layer (platform-specific)
extern void i2c_hw_send_start(void);
extern void i2c_hw_send_stop(void);
extern void i2c_hw_send_addr(uint8_t addr, bool read);
extern void i2c_hw_send_data(uint8_t data);
extern uint8_t i2c_hw_read_data(void);
extern void i2c_hw_send_ack(void);
extern void i2c_hw_send_nack(void);
extern void i2c_hw_clear_error(void);
extern uint32_t get_tick_count(void);

// State machine transition function
static void i2c_transition_to(i2c_state_t new_state)
{
    g_i2c_tcb.state = new_state;
    g_i2c_tcb.state_entry_time = get_tick_count();
}

// Check for state timeout
static bool i2c_state_timeout_check(void)
{
    uint32_t elapsed = get_tick_count() - g_i2c_tcb.state_entry_time;
    return (elapsed > g_i2c_tcb.timeout_ms);
}

// State machine event handler (called from ISR or main loop)
void i2c_state_machine(i2c_event_t event)
{
    // Timeout check for all states except IDLE
    if (g_i2c_tcb.state != I2C_STATE_IDLE && i2c_state_timeout_check()) {
        event = I2C_EVENT_TIMEOUT;
    }
    
    switch (g_i2c_tcb.state) {
        case I2C_STATE_IDLE:
            // No processing in IDLE, waiting for transaction start
            break;
            
        case I2C_STATE_START:
            if (event == I2C_EVENT_START_SENT) {
                // START condition sent, now send address
                i2c_hw_send_addr(g_i2c_tcb.device_addr, g_i2c_tcb.is_read);
                i2c_transition_to(I2C_STATE_ADDR_TX);
            } else if (event == I2C_EVENT_ARBITRATION_LOST) {
                g_i2c_tcb.error_code = -1;
                i2c_transition_to(I2C_STATE_ERROR);
            }
            break;
            
        case I2C_STATE_ADDR_TX:
            if (event == I2C_EVENT_ADDR_SENT) {
                i2c_transition_to(I2C_STATE_ADDR_ACK);
            }
            break;
            
        case I2C_STATE_ADDR_ACK:
            if (event == I2C_EVENT_ACK_RECEIVED) {
                if (g_i2c_tcb.is_read) {
                    // Reading: prepare to receive data
                    i2c_transition_to(I2C_STATE_DATA_RX);
                } else {
                    // Writing: send first data byte
                    if (g_i2c_tcb.tx_index < g_i2c_tcb.tx_len) {
                        i2c_hw_send_data(g_i2c_tcb.tx_buffer[g_i2c_tcb.tx_index++]);
                        i2c_transition_to(I2C_STATE_DATA_TX);
                    } else {
                        // No data to send, go to STOP
                        i2c_hw_send_stop();
                        i2c_transition_to(I2C_STATE_STOP);
                    }
                }
            } else if (event == I2C_EVENT_NACK_RECEIVED) {
                // Device not responding
                g_i2c_tcb.error_code = -2;
                i2c_hw_send_stop();
                i2c_transition_to(I2C_STATE_ERROR);
            }
            break;
            
        case I2C_STATE_DATA_TX:
            if (event == I2C_EVENT_DATA_SENT) {
                i2c_transition_to(I2C_STATE_DATA_TX_ACK);
            }
            break;
            
        case I2C_STATE_DATA_TX_ACK:
            if (event == I2C_EVENT_ACK_RECEIVED) {
                if (g_i2c_tcb.tx_index < g_i2c_tcb.tx_len) {
                    // More data to send
                    i2c_hw_send_data(g_i2c_tcb.tx_buffer[g_i2c_tcb.tx_index++]);
                    i2c_transition_to(I2C_STATE_DATA_TX);
                } else {
                    // All data sent, send STOP
                    i2c_hw_send_stop();
                    i2c_transition_to(I2C_STATE_STOP);
                }
            } else if (event == I2C_EVENT_NACK_RECEIVED) {
                // Slave signaled end of transfer
                g_i2c_tcb.error_code = -3;
                i2c_hw_send_stop();
                i2c_transition_to(I2C_STATE_ERROR);
            }
            break;
            
        case I2C_STATE_DATA_RX:
            if (event == I2C_EVENT_DATA_RECEIVED) {
                // Read data from hardware
                if (g_i2c_tcb.rx_index < g_i2c_tcb.rx_len) {
                    g_i2c_tcb.rx_buffer[g_i2c_tcb.rx_index++] = i2c_hw_read_data();
                }
                i2c_transition_to(I2C_STATE_DATA_RX_ACK);
            }
            break;
            
        case I2C_STATE_DATA_RX_ACK:
            if (g_i2c_tcb.rx_index < g_i2c_tcb.rx_len) {
                // More data to receive, send ACK
                i2c_hw_send_ack();
                i2c_transition_to(I2C_STATE_DATA_RX);
            } else {
                // Last byte received, send NACK and STOP
                i2c_hw_send_nack();
                i2c_hw_send_stop();
                i2c_transition_to(I2C_STATE_STOP);
            }
            break;
            
        case I2C_STATE_STOP:
            if (event == I2C_EVENT_STOP_SENT) {
                g_i2c_tcb.transaction_complete = true;
                i2c_transition_to(I2C_STATE_IDLE);
            }
            break;
            
        case I2C_STATE_ERROR:
            // Error handling: attempt recovery
            i2c_hw_clear_error();
            i2c_transition_to(I2C_STATE_RECOVERY);
            break;
            
        case I2C_STATE_RECOVERY:
            // Bus recovery logic (clock pulses, etc.)
            i2c_hw_send_stop();
            g_i2c_tcb.transaction_complete = true;
            i2c_transition_to(I2C_STATE_IDLE);
            break;
            
        default:
            // Unknown state, reset to IDLE
            i2c_transition_to(I2C_STATE_IDLE);
            break;
    }
}

// Initialize a write transaction
int i2c_start_write(uint8_t device_addr, uint8_t *data, uint16_t len, uint32_t timeout_ms)
{
    if (g_i2c_tcb.state != I2C_STATE_IDLE) {
        return -1; // Busy
    }
    
    g_i2c_tcb.device_addr = device_addr << 1; // Add R/W bit space
    g_i2c_tcb.tx_buffer = data;
    g_i2c_tcb.tx_len = len;
    g_i2c_tcb.tx_index = 0;
    g_i2c_tcb.is_read = false;
    g_i2c_tcb.transaction_complete = false;
    g_i2c_tcb.error_code = 0;
    g_i2c_tcb.timeout_ms = timeout_ms;
    
    i2c_hw_send_start();
    i2c_transition_to(I2C_STATE_START);
    
    return 0;
}

// Initialize a read transaction
int i2c_start_read(uint8_t device_addr, uint8_t *buffer, uint16_t len, uint32_t timeout_ms)
{
    if (g_i2c_tcb.state != I2C_STATE_IDLE) {
        return -1; // Busy
    }
    
    g_i2c_tcb.device_addr = (device_addr << 1) | 0x01; // Set read bit
    g_i2c_tcb.rx_buffer = buffer;
    g_i2c_tcb.rx_len = len;
    g_i2c_tcb.rx_index = 0;
    g_i2c_tcb.is_read = true;
    g_i2c_tcb.transaction_complete = false;
    g_i2c_tcb.error_code = 0;
    g_i2c_tcb.timeout_ms = timeout_ms;
    
    i2c_hw_send_start();
    i2c_transition_to(I2C_STATE_START);
    
    return 0;
}

// Wait for transaction completion (blocking)
int i2c_wait_complete(void)
{
    while (!g_i2c_tcb.transaction_complete) {
        // Could yield to RTOS here
    }
    return g_i2c_tcb.error_code;
}
```

### Example 2: Hierarchical State Machine for SMBus

```c
#include <stdint.h>
#include <stdbool.h>

// Top-level protocol states
typedef enum {
    SMBUS_IDLE,
    SMBUS_QUICK_COMMAND,
    SMBUS_SEND_BYTE,
    SMBUS_RECEIVE_BYTE,
    SMBUS_WRITE_BYTE,
    SMBUS_READ_BYTE,
    SMBUS_WRITE_WORD,
    SMBUS_READ_WORD,
    SMBUS_BLOCK_WRITE,
    SMBUS_BLOCK_READ,
    SMBUS_ERROR
} smbus_protocol_state_t;

// Low-level I2C states (nested within protocol states)
typedef enum {
    I2C_SUBSTATE_INIT,
    I2C_SUBSTATE_START,
    I2C_SUBSTATE_ADDR,
    I2C_SUBSTATE_COMMAND,
    I2C_SUBSTATE_DATA,
    I2C_SUBSTATE_PEC,
    I2C_SUBSTATE_STOP,
    I2C_SUBSTATE_COMPLETE
} i2c_substate_t;

typedef struct {
    smbus_protocol_state_t protocol_state;
    i2c_substate_t i2c_substate;
    uint8_t device_addr;
    uint8_t command;
    uint8_t *data_buffer;
    uint16_t data_len;
    uint16_t data_index;
    uint8_t pec;  // Packet Error Code
    bool use_pec;
    volatile bool complete;
    int error;
} smbus_context_t;

static smbus_context_t g_smbus = {0};

// PEC calculation (CRC-8)
static uint8_t smbus_calc_pec(uint8_t *data, uint16_t len)
{
    uint8_t crc = 0;
    for (uint16_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; bit++) {
            if (crc & 0x80) {
                crc = (crc << 1) ^ 0x07;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// Hierarchical state machine handler
void smbus_state_machine(i2c_event_t event)
{
    // Process based on protocol state
    switch (g_smbus.protocol_state) {
        case SMBUS_WRITE_BYTE:
            // Nested I2C state machine for Write Byte protocol
            switch (g_smbus.i2c_substate) {
                case I2C_SUBSTATE_START:
                    if (event == I2C_EVENT_START_SENT) {
                        i2c_hw_send_addr(g_smbus.device_addr, false);
                        g_smbus.i2c_substate = I2C_SUBSTATE_ADDR;
                    }
                    break;
                    
                case I2C_SUBSTATE_ADDR:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        i2c_hw_send_data(g_smbus.command);
                        g_smbus.i2c_substate = I2C_SUBSTATE_COMMAND;
                    } else if (event == I2C_EVENT_NACK_RECEIVED) {
                        g_smbus.error = -1;
                        g_smbus.protocol_state = SMBUS_ERROR;
                    }
                    break;
                    
                case I2C_SUBSTATE_COMMAND:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        i2c_hw_send_data(g_smbus.data_buffer[0]);
                        g_smbus.i2c_substate = I2C_SUBSTATE_DATA;
                    }
                    break;
                    
                case I2C_SUBSTATE_DATA:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        if (g_smbus.use_pec) {
                            // Calculate and send PEC
                            uint8_t pec_data[3] = {
                                g_smbus.device_addr,
                                g_smbus.command,
                                g_smbus.data_buffer[0]
                            };
                            g_smbus.pec = smbus_calc_pec(pec_data, 3);
                            i2c_hw_send_data(g_smbus.pec);
                            g_smbus.i2c_substate = I2C_SUBSTATE_PEC;
                        } else {
                            i2c_hw_send_stop();
                            g_smbus.i2c_substate = I2C_SUBSTATE_STOP;
                        }
                    }
                    break;
                    
                case I2C_SUBSTATE_PEC:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        i2c_hw_send_stop();
                        g_smbus.i2c_substate = I2C_SUBSTATE_STOP;
                    }
                    break;
                    
                case I2C_SUBSTATE_STOP:
                    if (event == I2C_EVENT_STOP_SENT) {
                        g_smbus.complete = true;
                        g_smbus.protocol_state = SMBUS_IDLE;
                        g_smbus.i2c_substate = I2C_SUBSTATE_COMPLETE;
                    }
                    break;
                    
                default:
                    break;
            }
            break;
            
        case SMBUS_BLOCK_READ:
            // More complex nested state machine for block operations
            switch (g_smbus.i2c_substate) {
                case I2C_SUBSTATE_START:
                    if (event == I2C_EVENT_START_SENT) {
                        i2c_hw_send_addr(g_smbus.device_addr, false);
                        g_smbus.i2c_substate = I2C_SUBSTATE_ADDR;
                    }
                    break;
                    
                case I2C_SUBSTATE_ADDR:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        i2c_hw_send_data(g_smbus.command);
                        g_smbus.i2c_substate = I2C_SUBSTATE_COMMAND;
                    }
                    break;
                    
                case I2C_SUBSTATE_COMMAND:
                    if (event == I2C_EVENT_ACK_RECEIVED) {
                        // Repeated START for read
                        i2c_hw_send_start();
                        g_smbus.i2c_substate = I2C_SUBSTATE_DATA;
                    }
                    break;
                    
                case I2C_SUBSTATE_DATA:
                    if (event == I2C_EVENT_START_SENT) {
                        i2c_hw_send_addr(g_smbus.device_addr, true);
                    } else if (event == I2C_EVENT_DATA_RECEIVED) {
                        if (g_smbus.data_index == 0) {
                            // First byte is block length
                            g_smbus.data_len = i2c_hw_read_data();
                        } else {
                            g_smbus.data_buffer[g_smbus.data_index - 1] = i2c_hw_read_data();
                        }
                        
                        g_smbus.data_index++;
                        
                        if (g_smbus.data_index <= g_smbus.data_len) {
                            i2c_hw_send_ack();
                        } else {
                            i2c_hw_send_nack();
                            i2c_hw_send_stop();
                            g_smbus.i2c_substate = I2C_SUBSTATE_STOP;
                        }
                    }
                    break;
                    
                case I2C_SUBSTATE_STOP:
                    if (event == I2C_EVENT_STOP_SENT) {
                        g_smbus.complete = true;
                        g_smbus.protocol_state = SMBUS_IDLE;
                    }
                    break;
                    
                default:
                    break;
            }
            break;
            
        default:
            break;
    }
}
```

## C++ Implementation Examples

### Example 3: Object-Oriented State Machine with Templates

```cpp
#include <cstdint>
#include <functional>
#include <array>
#include <optional>

// Forward declarations
class I2CStateMachine;
enum class I2CState;
enum class I2CEvent;

// State handler function type
using StateHandler = std::function<void(I2CStateMachine&, I2CEvent)>;

// I2C States
enum class I2CState {
    Idle,
    Start,
    AddressTx,
    AddressAck,
    DataTx,
    DataTxAck,
    DataRx,
    DataRxAck,
    Stop,
    Error,
    Recovery,
    MAX_STATES
};

// I2C Events
enum class I2CEvent {
    None,
    StartSent,
    AddressSent,
    AckReceived,
    NackReceived,
    DataSent,
    DataReceived,
    StopSent,
    ArbitrationLost,
    BusError,
    Timeout
};

// Transaction result
struct I2CResult {
    bool success;
    int error_code;
    uint16_t bytes_transferred;
};

// Abstract hardware interface
class II2CHardware {
public:
    virtual ~II2CHardware() = default;
    virtual void sendStart() = 0;
    virtual void sendStop() = 0;
    virtual void sendAddress(uint8_t addr, bool read) = 0;
    virtual void sendData(uint8_t data) = 0;
    virtual uint8_t readData() = 0;
    virtual void sendAck() = 0;
    virtual void sendNack() = 0;
    virtual void clearError() = 0;
};

// State machine class
class I2CStateMachine {
private:
    I2CState current_state_;
    II2CHardware& hardware_;
    
    uint8_t device_addr_;
    uint8_t* tx_buffer_;
    uint8_t* rx_buffer_;
    uint16_t tx_len_;
    uint16_t rx_len_;
    uint16_t tx_index_;
    uint16_t rx_index_;
    bool is_read_;
    bool transaction_complete_;
    int error_code_;
    
    // State handler table
    std::array<StateHandler, static_cast<size_t>(I2CState::MAX_STATES)> state_handlers_;
    
    // State handlers
    void handleIdle(I2CEvent event);
    void handleStart(I2CEvent event);
    void handleAddressTx(I2CEvent event);
    void handleAddressAck(I2CEvent event);
    void handleDataTx(I2CEvent event);
    void handleDataTxAck(I2CEvent event);
    void handleDataRx(I2CEvent event);
    void handleDataRxAck(I2CEvent event);
    void handleStop(I2CEvent event);
    void handleError(I2CEvent event);
    void handleRecovery(I2CEvent event);
    
    void transitionTo(I2CState new_state) {
        current_state_ = new_state;
    }
    
public:
    explicit I2CStateMachine(II2CHardware& hardware) 
        : current_state_(I2CState::Idle),
          hardware_(hardware),
          device_addr_(0),
          tx_buffer_(nullptr),
          rx_buffer_(nullptr),
          tx_len_(0),
          rx_len_(0),
          tx_index_(0),
          rx_index_(0),
          is_read_(false),
          transaction_complete_(false),
          error_code_(0)
    {
        // Initialize state handler table
        state_handlers_[static_cast<size_t>(I2CState::Idle)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleIdle(e); };
        state_handlers_[static_cast<size_t>(I2CState::Start)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleStart(e); };
        state_handlers_[static_cast<size_t>(I2CState::AddressTx)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleAddressTx(e); };
        state_handlers_[static_cast<size_t>(I2CState::AddressAck)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleAddressAck(e); };
        state_handlers_[static_cast<size_t>(I2CState::DataTx)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleDataTx(e); };
        state_handlers_[static_cast<size_t>(I2CState::DataTxAck)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleDataTxAck(e); };
        state_handlers_[static_cast<size_t>(I2CState::DataRx)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleDataRx(e); };
        state_handlers_[static_cast<size_t>(I2CState::DataRxAck)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleDataRxAck(e); };
        state_handlers_[static_cast<size_t>(I2CState::Stop)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleStop(e); };
        state_handlers_[static_cast<size_t>(I2CState::Error)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleError(e); };
        state_handlers_[static_cast<size_t>(I2CState::Recovery)] = 
            [this](I2CStateMachine& sm, I2CEvent e) { handleRecovery(e); };
    }
    
    // Process event
    void processEvent(I2CEvent event) {
        auto handler = state_handlers_[static_cast<size_t>(current_state_)];
        if (handler) {
            handler(*this, event);
        }
    }
    
    // Start write transaction
    bool startWrite(uint8_t device_addr, uint8_t* data, uint16_t len) {
        if (current_state_ != I2CState::Idle) {
            return false;
        }
        
        device_addr_ = device_addr << 1;
        tx_buffer_ = data;
        tx_len_ = len;
        tx_index_ = 0;
        is_read_ = false;
        transaction_complete_ = false;
        error_code_ = 0;
        
        hardware_.sendStart();
        transitionTo(I2CState::Start);
        return true;
    }
    
    // Start read transaction
    bool startRead(uint8_t device_addr, uint8_t* buffer, uint16_t len) {
        if (current_state_ != I2CState::Idle) {
            return false;
        }
        
        device_addr_ = (device_addr << 1) | 0x01;
        rx_buffer_ = buffer;
        rx_len_ = len;
        rx_index_ = 0;
        is_read_ = true;
        transaction_complete_ = false;
        error_code_ = 0;
        
        hardware_.sendStart();
        transitionTo(I2CState::Start);
        return true;
    }
    
    // Check if transaction is complete
    bool isComplete() const { return transaction_complete_; }
    
    // Get result
    I2CResult getResult() const {
        return I2CResult{
            error_code_ == 0,
            error_code_,
            is_read_ ? rx_index_ : tx_index_
        };
    }
    
    // Friend declarations for state handlers
    friend void handleStart(I2CEvent event);
};

// State handler implementations
void I2CStateMachine::handleIdle(I2CEvent event) {
    // Idle state, waiting for transaction start
}

void I2CStateMachine::handleStart(I2CEvent event) {
    if (event == I2CEvent::StartSent) {
        hardware_.sendAddress(device_addr_, is_read_);
        transitionTo(I2CState::AddressTx);
    } else if (event == I2CEvent::ArbitrationLost) {
        error_code_ = -1;
        transitionTo(I2CState::Error);
    }
}

void I2CStateMachine::handleAddressTx(I2CEvent event) {
    if (event == I2CEvent::AddressSent) {
        transitionTo(I2CState::AddressAck);
    }
}

void I2CStateMachine::handleAddressAck(I2CEvent event) {
    if (event == I2CEvent::AckReceive) {
        if (is_read_) {
            transitionTo(I2CState::DataRx);
        } else {
            if (tx_index_ < tx_len_) {
                hardware_.sendData(tx_buffer_[tx_index_++]);
                transitionTo(I2CState::DataTx);
            } else {
                hardware_.sendStop();
                transitionTo(I2CState::Stop);
            }
        }
    } else if (event == I2CEvent::NackReceived) {
        error_code_ = -2;
        hardware_.sendStop();
        transitionTo(I2CState::Error);
    }
}

void I2CStateMachine::handleDataTx(I2CEvent event) {
    if (event == I2CEvent::DataSent) {
        transitionTo(I2CState::DataTxAck);
    }
}

void I2CStateMachine::handleDataTxAck(I2CEvent event) {
    if (event == I2CEvent::AckReceived) {
        if (tx_index_ < tx_len_) {
            hardware_.sendData(tx_buffer_[tx_index_++]);
            transitionTo(I2CState::DataTx);
        } else {
            hardware_.sendStop();
            transitionTo(I2CState::Stop);
        }
    } else if (event == I2CEvent::NackReceived) {
        error_code_ = -3;
        hardware_.sendStop();
        transitionTo(I2CState::Error);
    }
}

void I2CStateMachine::handleDataRx(I2CEvent event) {
    if (event == I2CEvent::DataReceived) {
        if (rx_index_ < rx_len_) {
            rx_buffer_[rx_index_++] = hardware_.readData();
        }
        transitionTo(I2CState::DataRxAck);
    }
}

void I2CStateMachine::handleDataRxAck(I2CEvent event) {
    if (rx_index_ < rx_len_) {
        hardware_.sendAck();
        transitionTo(I2CState::DataRx);
    } else {
        hardware_.sendNack();
        hardware_.sendStop();
        transitionTo(I2CState::Stop);
    }
}

void I2CStateMachine::handleStop(I2CEvent event) {
    if (event == I2CEvent::StopSent) {
        transaction_complete_ = true;
        transitionTo(I2CState::Idle);
    }
}

void I2CStateMachine::handleError(I2CEvent event) {
    hardware_.clearError();
    transitionTo(I2CState::Recovery);
}

void I2CStateMachine::handleRecovery(I2CEvent event) {
    hardware_.sendStop();
    transaction_complete_ = true;
    transitionTo(I2CState::Idle);
}
```

## Rust Implementation Examples

### Example 4: Type-Safe State Machine with Enum States

```rust
use core::marker::PhantomData;

// Hardware abstraction trait
pub trait I2CHardware {
    fn send_start(&mut self);
    fn send_stop(&mut self);
    fn send_address(&mut self, addr: u8, read: bool);
    fn send_data(&mut self, data: u8);
    fn read_data(&mut self) -> u8;
    fn send_ack(&mut self);
    fn send_nack(&mut self);
    fn clear_error(&mut self);
}

// I2C Events
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum I2CEvent {
    StartSent,
    AddressSent,
    AckReceived,
    NackReceived,
    DataSent,
    DataReceived,
    StopSent,
    ArbitrationLost,
    BusError,
    Timeout,
}

// Transaction result
#[derive(Debug)]
pub struct I2CResult {
    pub success: bool,
    pub error_code: i32,
    pub bytes_transferred: u16,
}

// Zero-sized marker types for states
pub struct Idle;
pub struct Start;
pub struct AddressTx;
pub struct AddressAck;
pub struct DataTx;
pub struct DataTxAck;
pub struct DataRx;
pub struct DataRxAck;
pub struct Stop;
pub struct Error;

// Type-safe state machine using PhantomData
pub struct I2CStateMachine<State, H: I2CHardware> {
    hardware: H,
    device_addr: u8,
    tx_buffer: Option<&'static [u8]>,
    rx_buffer: Option<&'static mut [u8]>,
    tx_index: u16,
    rx_index: u16,
    is_read: bool,
    error_code: i32,
    _state: PhantomData<State>,
}

// Idle state implementation
impl<H: I2CHardware> I2CStateMachine<Idle, H> {
    pub fn new(hardware: H) -> Self {
        Self {
            hardware,
            device_addr: 0,
            tx_buffer: None,
            rx_buffer: None,
            tx_index: 0,
            rx_index: 0,
            is_read: false,
            error_code: 0,
            _state: PhantomData,
        }
    }
    
    pub fn start_write(
        mut self,
        device_addr: u8,
        data: &'static [u8],
    ) -> I2CStateMachine<Start, H> {
        self.device_addr = device_addr << 1;
        self.tx_buffer = Some(data);
        self.tx_index = 0;
        self.is_read = false;
        self.error_code = 0;
        
        self.hardware.send_start();
        
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: self.device_addr,
            tx_buffer: self.tx_buffer,
            rx_buffer: None,
            tx_index: 0,
            rx_index: 0,
            is_read: false,
            error_code: 0,
            _state: PhantomData,
        }
    }
    
    pub fn start_read(
        mut self,
        device_addr: u8,
        buffer: &'static mut [u8],
    ) -> I2CStateMachine<Start, H> {
        self.device_addr = (device_addr << 1) | 0x01;
        self.rx_buffer = Some(buffer);
        self.rx_index = 0;
        self.is_read = true;
        self.error_code = 0;
        
        self.hardware.send_start();
        
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: self.device_addr,
            tx_buffer: None,
            rx_buffer: self.rx_buffer,
            tx_index: 0,
            rx_index: 0,
            is_read: true,
            error_code: 0,
            _state: PhantomData,
        }
    }
}

// Start state implementation
impl<H: I2CHardware> I2CStateMachine<Start, H> {
    pub fn on_event(mut self, event: I2CEvent) -> Result<I2CStateMachine<AddressTx, H>, I2CStateMachine<Error, H>> {
        match event {
            I2CEvent::StartSent => {
                self.hardware.send_address(self.device_addr, self.is_read);
                Ok(I2CStateMachine {
                    hardware: self.hardware,
                    device_addr: self.device_addr,
                    tx_buffer: self.tx_buffer,
                    rx_buffer: self.rx_buffer,
                    tx_index: self.tx_index,
                    rx_index: self.rx_index,
                    is_read: self.is_read,
                    error_code: 0,
                    _state: PhantomData,
                })
            }
            I2CEvent::ArbitrationLost => {
                Err(I2CStateMachine {
                    hardware: self.hardware,
                    device_addr: self.device_addr,
                    tx_buffer: self.tx_buffer,
                    rx_buffer: self.rx_buffer,
                    tx_index: self.tx_index,
                    rx_index: self.rx_index,
                    is_read: self.is_read,
                    error_code: -1,
                    _state: PhantomData,
                })
            }
            _ => Ok(I2CStateMachine {
                hardware: self.hardware,
                device_addr: self.device_addr,
                tx_buffer: self.tx_buffer,
                rx_buffer: self.rx_buffer,
                tx_index: self.tx_index,
                rx_index: self.rx_index,
                is_read: self.is_read,
                error_code: 0,
                _state: PhantomData,
            }),
        }
    }
}

// AddressTx state
impl<H: I2CHardware> I2CStateMachine<AddressTx, H> {
    pub fn on_event(self, event: I2CEvent) -> I2CStateMachine<AddressAck, H> {
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: self.device_addr,
            tx_buffer: self.tx_buffer,
            rx_buffer: self.rx_buffer,
            tx_index: self.tx_index,
            rx_index: self.rx_index,
            is_read: self.is_read,
            error_code: self.error_code,
            _state: PhantomData,
        }
    }
}

// AddressAck state with transitions to DataTx, DataRx, or Error
impl<H: I2CHardware> I2CStateMachine<AddressAck, H> {
    pub fn on_ack(mut self) -> Result<DataState<H>, I2CStateMachine<Stop, H>> {
        if self.is_read {
            Ok(DataState::Rx(I2CStateMachine {
                hardware: self.hardware,
                device_addr: self.device_addr,
                tx_buffer: self.tx_buffer,
                rx_buffer: self.rx_buffer,
                tx_index: self.tx_index,
                rx_index: self.rx_index,
                is_read: self.is_read,
                error_code: 0,
                _state: PhantomData,
            }))
        } else {
            if let Some(buffer) = self.tx_buffer {
                if (self.tx_index as usize) < buffer.len() {
                    self.hardware.send_data(buffer[self.tx_index as usize]);
                    self.tx_index += 1;
                    Ok(DataState::Tx(I2CStateMachine {
                        hardware: self.hardware,
                        device_addr: self.device_addr,
                        tx_buffer: self.tx_buffer,
                        rx_buffer: None,
                        tx_index: self.tx_index,
                        rx_index: 0,
                        is_read: false,
                        error_code: 0,
                        _state: PhantomData,
                    }))
                } else {
                    self.hardware.send_stop();
                    Err(I2CStateMachine {
                        hardware: self.hardware,
                        device_addr: self.device_addr,
                        tx_buffer: self.tx_buffer,
                        rx_buffer: None,
                        tx_index: self.tx_index,
                        rx_index: 0,
                        is_read: false,
                        error_code: 0,
                        _state: PhantomData,
                    })
                }
            } else {
                self.hardware.send_stop();
                Err(I2CStateMachine {
                    hardware: self.hardware,
                    device_addr: self.device_addr,
                    tx_buffer: None,
                    rx_buffer: None,
                    tx_index: 0,
                    rx_index: 0,
                    is_read: false,
                    error_code: -4,
                    _state: PhantomData,
                })
            }
        }
    }
    
    pub fn on_nack(mut self) -> I2CStateMachine<Error, H> {
        self.hardware.send_stop();
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: self.device_addr,
            tx_buffer: self.tx_buffer,
            rx_buffer: self.rx_buffer,
            tx_index: self.tx_index,
            rx_index: self.rx_index,
            is_read: self.is_read,
            error_code: -2,
            _state: PhantomData,
        }
    }
}

// Enum to handle both Tx and Rx data states
pub enum DataState<H: I2CHardware> {
    Tx(I2CStateMachine<DataTx, H>),
    Rx(I2CStateMachine<DataRx, H>),
}

// DataTx state implementation
impl<H: I2CHardware> I2CStateMachine<DataTx, H> {
    pub fn on_data_sent(self) -> I2CStateMachine<DataTxAck, H> {
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: self.device_addr,
            tx_buffer: self.tx_buffer,
            rx_buffer: self.rx_buffer,
            tx_index: self.tx_index,
            rx_index: self.rx_index,
            is_read: self.is_read,
            error_code: 0,
            _state: PhantomData,
        }
    }
}

// DataTxAck state
impl<H: I2CHardware> I2CStateMachine<DataTxAck, H> {
    pub fn on_ack(mut self) -> Result<I2CStateMachine<DataTx, H>, I2CStateMachine<Stop, H>> {
        if let Some(buffer) = self.tx_buffer {
            if (self.tx_index as usize) < buffer.len() {
                self.hardware.send_data(buffer[self.tx_index as usize]);
                self.tx_index += 1;
                Ok(I2CStateMachine {
                    hardware: self.hardware,
                    device_addr: self.device_addr,
                    tx_buffer: self.tx_buffer,
                    rx_buffer: None,
                    tx_index: self.tx_index,
                    rx_index: 0,
                    is_read: false,
                    error_code: 0,
                    _state: PhantomData,
                })
            } else {
                self.hardware.send_stop();
                Err(I2CStateMachine {
                    hardware: self.hardware,
                    device_addr: self.device_addr,
                    tx_buffer: self.tx_buffer,
                    rx_buffer: None,
                    tx_index: self.tx_index,
                    rx_index: 0,
                    is_read: false,
                    error_code: 0,
                    _state: PhantomData,
                })
            }
        } else {
            self.hardware.send_stop();
            Err(I2CStateMachine {
                hardware: self.hardware,
                device_addr: self.device_addr,
                tx_buffer: None,
                rx_buffer: None,
                tx_index: 0,
                rx_index: 0,
                is_read: false,
                error_code: -4,
                _state: PhantomData,
            })
        }
    }
}

// Stop state - transition back to Idle
impl<H: I2CHardware> I2CStateMachine<Stop, H> {
    pub fn on_stop_sent(self) -> (I2CStateMachine<Idle, H>, I2CResult) {
        let result = I2CResult {
            success: self.error_code == 0,
            error_code: self.error_code,
            bytes_transferred: if self.is_read {
                self.rx_index
            } else {
                self.tx_index
            },
        };
        
        (I2CStateMachine {
            hardware: self.hardware,
            device_addr: 0,
            tx_buffer: None,
            rx_buffer: None,
            tx_index: 0,
            rx_index: 0,
            is_read: false,
            error_code: 0,
            _state: PhantomData,
        }, result)
    }
}

// Error state
impl<H: I2CHardware> I2CStateMachine<Error, H> {
    pub fn recover(mut self) -> I2CStateMachine<Idle, H> {
        self.hardware.clear_error();
        self.hardware.send_stop();
        
        I2CStateMachine {
            hardware: self.hardware,
            device_addr: 0,
            tx_buffer: None,
            rx_buffer: None,
            tx_index: 0,
            rx_index: 0,
            is_read: false,
            error_code: 0,
            _state: PhantomData,
        }
    }
    
    pub fn get_error_code(&self) -> i32 {
        self.error_code
    }
}
```

## Best Practices

### 1. **State Transition Validation**
Always validate that state transitions are legal. Use assertions or compile-time checks (like Rust's type system) to prevent invalid transitions.

### 2. **Timeout Management**
Implement per-state timeouts to prevent hanging in any state due to hardware failures.

### 3. **Error Recovery**
Design clear error recovery paths. Every error state should have a defined recovery sequence.

### 4. **Atomic State Updates**
In interrupt-driven systems, ensure state variable updates are atomic or protected by critical sections.

### 5. **Testing Strategy**
- Unit test each state handler independently
- Integration test complete transaction sequences
- Fault injection testing for error paths
- Timing analysis for real-time constraints

### 6. **Documentation**
Maintain state diagrams alongside code. Tools like PlantUML or Mermaid can generate diagrams from text descriptions.

### 7. **Scalability**
For complex protocols (SMBus, PMBus), use hierarchical state machines to manage complexity.

## Summary

State machines provide the robust architectural foundation needed for reliable I2C communication. The choice between C, C++, and Rust implementations depends on your project requirements:

- **C**: Maximum portability, minimal overhead, suitable for resource-constrained systems
- **C++**: Object-oriented design, better code organization, template-based type safety
- **Rust**: Compile-time state validation, memory safety, zero-cost abstractions

All three approaches can achieve reliable I2C communication when following proper state machine design principles.        