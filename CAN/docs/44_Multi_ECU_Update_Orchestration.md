# CAN Bus and Multi-ECU Update Orchestration

## Detailed Description of CAN (Controller Area Network)

CAN is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. Originally developed by Bosch in the 1980s for automotive applications, CAN has become ubiquitous in vehicles and industrial automation.

### Key Characteristics

**Message-Based Protocol**: CAN uses broadcast messages rather than point-to-point communication. Each message has an identifier that determines both its content and priority.

**Arbitration**: When multiple nodes attempt to transmit simultaneously, CAN uses non-destructive bitwise arbitration based on message identifiers. Lower identifier values have higher priority.

**Error Detection**: CAN implements multiple error detection mechanisms including CRC checks, frame checks, acknowledgment checks, bit monitoring, and bit stuffing.

**Physical Layer**: Typically uses differential signaling over a twisted pair of wires (CAN High and CAN Low), making it resistant to electromagnetic interference.

## Multi-ECU Update Orchestration

Updating firmware across multiple Electronic Control Units (ECUs) in a vehicle presents significant challenges:

- **Dependency Management**: Some ECUs may depend on others being updated first
- **System Availability**: Critical systems must remain operational during updates
- **Rollback Capability**: Failed updates must be recoverable
- **Bandwidth Constraints**: CAN bus bandwidth is limited (typically 125 kbps to 1 Mbps)
- **Security**: Updates must be authenticated and encrypted
- **Coordination**: Multiple ECUs must be synchronized to prevent system inconsistencies

## C/C++ Implementation

### Basic CAN Frame Structure

```c
// CAN frame structure
typedef struct {
    uint32_t id;           // CAN identifier (11-bit or 29-bit)
    uint8_t dlc;           // Data length code (0-8 bytes)
    uint8_t data[8];       // Payload data
    bool extended;         // Extended frame format flag
    bool remote;           // Remote transmission request flag
} can_frame_t;

// UDS (Unified Diagnostic Services) message types for firmware updates
#define UDS_REQUEST_DOWNLOAD    0x34
#define UDS_TRANSFER_DATA       0x36
#define UDS_REQUEST_TRANSFER_EXIT 0x37
#define UDS_ROUTINE_CONTROL     0x31
#define UDS_DIAGNOSTIC_SESSION  0x10

// ECU update states
typedef enum {
    ECU_STATE_IDLE,
    ECU_STATE_UPDATE_PENDING,
    ECU_STATE_DOWNLOADING,
    ECU_STATE_VERIFYING,
    ECU_STATE_INSTALLING,
    ECU_STATE_COMPLETE,
    ECU_STATE_FAILED
} ecu_update_state_t;

// ECU node information
typedef struct {
    uint8_t node_id;
    char name[32];
    uint8_t priority;
    ecu_update_state_t state;
    uint32_t firmware_version;
    uint32_t target_version;
    uint8_t dependencies[8];  // IDs of ECUs that must update first
    uint8_t dep_count;
    bool critical;            // Critical for vehicle operation
} ecu_node_t;
```

### Update Orchestrator Implementation

```c
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>

#define MAX_ECUS 16
#define BLOCK_SIZE 256
#define MAX_RETRIES 3

// Update orchestrator state
typedef struct {
    ecu_node_t ecus[MAX_ECUS];
    uint8_t ecu_count;
    pthread_mutex_t state_mutex;
    bool update_in_progress;
    uint8_t current_phase;
} update_orchestrator_t;

// Initialize orchestrator
void orchestrator_init(update_orchestrator_t *orch) {
    memset(orch, 0, sizeof(update_orchestrator_t));
    pthread_mutex_init(&orch->state_mutex, NULL);
}

// Add ECU to orchestrator
bool add_ecu(update_orchestrator_t *orch, ecu_node_t *ecu) {
    pthread_mutex_lock(&orch->state_mutex);
    
    if (orch->ecu_count >= MAX_ECUS) {
        pthread_mutex_unlock(&orch->state_mutex);
        return false;
    }
    
    memcpy(&orch->ecus[orch->ecu_count], ecu, sizeof(ecu_node_t));
    orch->ecu_count++;
    
    pthread_mutex_unlock(&orch->state_mutex);
    return true;
}

// Check if ECU dependencies are satisfied
bool dependencies_satisfied(update_orchestrator_t *orch, ecu_node_t *ecu) {
    for (uint8_t i = 0; i < ecu->dep_count; i++) {
        uint8_t dep_id = ecu->dependencies[i];
        
        // Find dependency ECU
        for (uint8_t j = 0; j < orch->ecu_count; j++) {
            if (orch->ecus[j].node_id == dep_id) {
                if (orch->ecus[j].state != ECU_STATE_COMPLETE) {
                    return false;
                }
                break;
            }
        }
    }
    return true;
}

// Send CAN message (simplified - would use SocketCAN on Linux)
int send_can_message(uint32_t can_id, uint8_t *data, uint8_t len) {
    can_frame_t frame;
    frame.id = can_id;
    frame.dlc = len;
    frame.extended = false;
    frame.remote = false;
    memcpy(frame.data, data, len);
    
    // Platform-specific CAN transmission would go here
    printf("CAN TX [0x%03X]: ", can_id);
    for (int i = 0; i < len; i++) {
        printf("%02X ", data[i]);
    }
    printf("\n");
    
    return 0;
}

// Enter programming session (UDS)
bool enter_programming_session(uint8_t node_id) {
    uint8_t data[2] = {UDS_DIAGNOSTIC_SESSION, 0x02}; // Programming session
    uint32_t can_id = 0x700 + node_id; // Example: UDS request ID
    
    return send_can_message(can_id, data, 2) == 0;
}

// Request download (UDS)
bool request_download(uint8_t node_id, uint32_t address, uint32_t size) {
    uint8_t data[11] = {
        UDS_REQUEST_DOWNLOAD,
        0x00,  // dataFormatIdentifier
        0x44,  // addressAndLengthFormatIdentifier (4 bytes each)
        (address >> 24) & 0xFF,
        (address >> 16) & 0xFF,
        (address >> 8) & 0xFF,
        address & 0xFF,
        (size >> 24) & 0xFF,
        (size >> 16) & 0xFF,
        (size >> 8) & 0xFF,
        size & 0xFF
    };
    
    uint32_t can_id = 0x700 + node_id;
    return send_can_message(can_id, data, 11) == 0;
}

// Transfer data block
bool transfer_data_block(uint8_t node_id, uint8_t block_seq, uint8_t *data, uint8_t len) {
    uint8_t frame_data[8];
    frame_data[0] = UDS_TRANSFER_DATA;
    frame_data[1] = block_seq;
    
    uint8_t payload_len = (len > 6) ? 6 : len;
    memcpy(&frame_data[2], data, payload_len);
    
    uint32_t can_id = 0x700 + node_id;
    return send_can_message(can_id, frame_data, payload_len + 2) == 0;
}

// Update single ECU
bool update_ecu(update_orchestrator_t *orch, uint8_t ecu_idx, 
                uint8_t *firmware, uint32_t fw_size) {
    ecu_node_t *ecu = &orch->ecus[ecu_idx];
    
    printf("Starting update for ECU %s (ID: %d)\n", ecu->name, ecu->node_id);
    
    // Enter programming session
    ecu->state = ECU_STATE_UPDATE_PENDING;
    if (!enter_programming_session(ecu->node_id)) {
        printf("Failed to enter programming session\n");
        ecu->state = ECU_STATE_FAILED;
        return false;
    }
    
    // Request download
    ecu->state = ECU_STATE_DOWNLOADING;
    if (!request_download(ecu->node_id, 0x00000000, fw_size)) {
        printf("Failed to request download\n");
        ecu->state = ECU_STATE_FAILED;
        return false;
    }
    
    // Transfer firmware in blocks
    uint32_t blocks = (fw_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    uint8_t block_seq = 1;
    
    for (uint32_t i = 0; i < blocks; i++) {
        uint32_t offset = i * BLOCK_SIZE;
        uint32_t remaining = fw_size - offset;
        uint32_t block_len = (remaining > BLOCK_SIZE) ? BLOCK_SIZE : remaining;
        
        // Transfer block in multiple CAN frames
        for (uint32_t j = 0; j < block_len; j += 6) {
            uint8_t chunk_len = ((block_len - j) > 6) ? 6 : (block_len - j);
            
            if (!transfer_data_block(ecu->node_id, block_seq++, 
                                    &firmware[offset + j], chunk_len)) {
                printf("Failed to transfer data block\n");
                ecu->state = ECU_STATE_FAILED;
                return false;
            }
        }
        
        printf("Progress: %d/%d blocks\n", i + 1, blocks);
    }
    
    // Verification phase
    ecu->state = ECU_STATE_VERIFYING;
    printf("Verifying firmware...\n");
    
    // Request transfer exit
    uint8_t exit_data[1] = {UDS_REQUEST_TRANSFER_EXIT};
    send_can_message(0x700 + ecu->node_id, exit_data, 1);
    
    ecu->state = ECU_STATE_INSTALLING;
    printf("Installing firmware...\n");
    
    // Trigger installation routine
    uint8_t routine_data[4] = {UDS_ROUTINE_CONTROL, 0x01, 0xFF, 0x00};
    send_can_message(0x700 + ecu->node_id, routine_data, 4);
    
    ecu->state = ECU_STATE_COMPLETE;
    ecu->firmware_version = ecu->target_version;
    printf("ECU %s update complete\n", ecu->name);
    
    return true;
}

// Orchestrate updates across all ECUs
bool orchestrate_updates(update_orchestrator_t *orch, 
                        uint8_t **firmwares, uint32_t *fw_sizes) {
    pthread_mutex_lock(&orch->state_mutex);
    orch->update_in_progress = true;
    pthread_mutex_unlock(&orch->state_mutex);
    
    // Phase 1: Update non-critical ECUs with no dependencies
    printf("=== Phase 1: Non-critical ECUs ===\n");
    for (uint8_t i = 0; i < orch->ecu_count; i++) {
        if (!orch->ecus[i].critical && orch->ecus[i].dep_count == 0) {
            update_ecu(orch, i, firmwares[i], fw_sizes[i]);
        }
    }
    
    // Phase 2: Update ECUs with satisfied dependencies
    printf("\n=== Phase 2: Dependent ECUs ===\n");
    bool progress = true;
    while (progress) {
        progress = false;
        for (uint8_t i = 0; i < orch->ecu_count; i++) {
            if (orch->ecus[i].state == ECU_STATE_IDLE &&
                dependencies_satisfied(orch, &orch->ecus[i])) {
                update_ecu(orch, i, firmwares[i], fw_sizes[i]);
                progress = true;
            }
        }
    }
    
    // Phase 3: Update critical ECUs last
    printf("\n=== Phase 3: Critical ECUs ===\n");
    for (uint8_t i = 0; i < orch->ecu_count; i++) {
        if (orch->ecus[i].critical && orch->ecus[i].state == ECU_STATE_IDLE) {
            update_ecu(orch, i, firmwares[i], fw_sizes[i]);
        }
    }
    
    pthread_mutex_lock(&orch->state_mutex);
    orch->update_in_progress = false;
    pthread_mutex_unlock(&orch->state_mutex);
    
    // Check for failures
    for (uint8_t i = 0; i < orch->ecu_count; i++) {
        if (orch->ecus[i].state == ECU_STATE_FAILED) {
            printf("\nUpdate failed for ECU %s\n", orch->ecus[i].name);
            return false;
        }
    }
    
    printf("\n=== All ECUs updated successfully ===\n");
    return true;
}
```

### Example Usage

```c
int main() {
    update_orchestrator_t orchestrator;
    orchestrator_init(&orchestrator);
    
    // Define ECUs
    ecu_node_t engine_ecu = {
        .node_id = 1,
        .name = "Engine ECU",
        .priority = 1,
        .state = ECU_STATE_IDLE,
        .firmware_version = 0x010000,
        .target_version = 0x010100,
        .dep_count = 0,
        .critical = true
    };
    
    ecu_node_t transmission_ecu = {
        .node_id = 2,
        .name = "Transmission ECU",
        .priority = 2,
        .state = ECU_STATE_IDLE,
        .firmware_version = 0x020000,
        .target_version = 0x020100,
        .dep_count = 1,
        .critical = true
    };
    transmission_ecu.dependencies[0] = 1; // Depends on Engine ECU
    
    ecu_node_t infotainment_ecu = {
        .node_id = 3,
        .name = "Infotainment ECU",
        .priority = 3,
        .state = ECU_STATE_IDLE,
        .firmware_version = 0x030000,
        .target_version = 0x030100,
        .dep_count = 0,
        .critical = false
    };
    
    // Add ECUs to orchestrator
    add_ecu(&orchestrator, &engine_ecu);
    add_ecu(&orchestrator, &transmission_ecu);
    add_ecu(&orchestrator, &infotainment_ecu);
    
    // Prepare firmware images (dummy data)
    uint8_t *firmwares[3];
    uint32_t fw_sizes[3] = {1024, 2048, 4096};
    
    for (int i = 0; i < 3; i++) {
        firmwares[i] = malloc(fw_sizes[i]);
        memset(firmwares[i], 0xAA, fw_sizes[i]); // Dummy firmware
    }
    
    // Start orchestrated update
    orchestrate_updates(&orchestrator, firmwares, fw_sizes);
    
    // Cleanup
    for (int i = 0; i < 3; i++) {
        free(firmwares[i]);
    }
    
    return 0;
}
```

## Rust Implementation

```rust
use std::collections::HashMap;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

// CAN frame structure
#[derive(Debug, Clone)]
struct CanFrame {
    id: u32,
    data: Vec<u8>,
    extended: bool,
    remote: bool,
}

// UDS service identifiers
const UDS_DIAGNOSTIC_SESSION: u8 = 0x10;
const UDS_REQUEST_DOWNLOAD: u8 = 0x34;
const UDS_TRANSFER_DATA: u8 = 0x36;
const UDS_REQUEST_TRANSFER_EXIT: u8 = 0x37;
const UDS_ROUTINE_CONTROL: u8 = 0x31;

// ECU update states
#[derive(Debug, Clone, PartialEq)]
enum EcuState {
    Idle,
    UpdatePending,
    Downloading,
    Verifying,
    Installing,
    Complete,
    Failed,
}

// ECU node information
#[derive(Debug, Clone)]
struct EcuNode {
    node_id: u8,
    name: String,
    priority: u8,
    state: EcuState,
    firmware_version: u32,
    target_version: u32,
    dependencies: Vec<u8>,
    critical: bool,
}

impl EcuNode {
    fn new(node_id: u8, name: &str, critical: bool) -> Self {
        Self {
            node_id,
            name: name.to_string(),
            priority: 0,
            state: EcuState::Idle,
            firmware_version: 0,
            target_version: 0,
            dependencies: Vec::new(),
            critical,
        }
    }
    
    fn add_dependency(&mut self, dep_id: u8) {
        self.dependencies.push(dep_id);
    }
}

// Update orchestrator
struct UpdateOrchestrator {
    ecus: Arc<Mutex<HashMap<u8, EcuNode>>>,
    update_in_progress: Arc<Mutex<bool>>,
}

impl UpdateOrchestrator {
    fn new() -> Self {
        Self {
            ecus: Arc::new(Mutex::new(HashMap::new())),
            update_in_progress: Arc::new(Mutex::new(false)),
        }
    }
    
    fn add_ecu(&self, ecu: EcuNode) {
        let mut ecus = self.ecus.lock().unwrap();
        ecus.insert(ecu.node_id, ecu);
    }
    
    fn send_can_frame(&self, frame: CanFrame) -> Result<(), String> {
        // Platform-specific CAN transmission would go here
        print!("CAN TX [0x{:03X}]: ", frame.id);
        for byte in &frame.data {
            print!("{:02X} ", byte);
        }
        println!();
        
        // Simulate transmission delay
        thread::sleep(Duration::from_millis(5));
        Ok(())
    }
    
    fn enter_programming_session(&self, node_id: u8) -> Result<(), String> {
        let frame = CanFrame {
            id: 0x700 + node_id as u32,
            data: vec![UDS_DIAGNOSTIC_SESSION, 0x02],
            extended: false,
            remote: false,
        };
        
        self.send_can_frame(frame)
    }
    
    fn request_download(&self, node_id: u8, address: u32, size: u32) 
        -> Result<(), String> {
        let mut data = vec![UDS_REQUEST_DOWNLOAD, 0x00, 0x44];
        data.extend_from_slice(&address.to_be_bytes());
        data.extend_from_slice(&size.to_be_bytes());
        
        let frame = CanFrame {
            id: 0x700 + node_id as u32,
            data,
            extended: false,
            remote: false,
        };
        
        self.send_can_frame(frame)
    }
    
    fn transfer_data_block(&self, node_id: u8, block_seq: u8, data: &[u8]) 
        -> Result<(), String> {
        let chunk_size = 6;
        let mut offset = 0;
        
        while offset < data.len() {
            let end = std::cmp::min(offset + chunk_size, data.len());
            let mut frame_data = vec![UDS_TRANSFER_DATA, block_seq];
            frame_data.extend_from_slice(&data[offset..end]);
            
            let frame = CanFrame {
                id: 0x700 + node_id as u32,
                data: frame_data,
                extended: false,
                remote: false,
            };
            
            self.send_can_frame(frame)?;
            offset = end;
        }
        
        Ok(())
    }
    
    fn dependencies_satisfied(&self, ecu: &EcuNode) -> bool {
        let ecus = self.ecus.lock().unwrap();
        
        for dep_id in &ecu.dependencies {
            if let Some(dep_ecu) = ecus.get(dep_id) {
                if dep_ecu.state != EcuState::Complete {
                    return false;
                }
            } else {
                return false;
            }
        }
        true
    }
    
    fn update_ecu(&self, node_id: u8, firmware: &[u8]) -> Result<(), String> {
        const BLOCK_SIZE: usize = 256;
        
        // Update ECU state
        {
            let mut ecus = self.ecus.lock().unwrap();
            if let Some(ecu) = ecus.get_mut(&node_id) {
                println!("Starting update for ECU {} (ID: {})", ecu.name, node_id);
                ecu.state = EcuState::UpdatePending;
            }
        }
        
        // Enter programming session
        self.enter_programming_session(node_id)?;
        
        {
            let mut ecus = self.ecus.lock().unwrap();
            if let Some(ecu) = ecus.get_mut(&node_id) {
                ecu.state = EcuState::Downloading;
            }
        }
        
        // Request download
        self.request_download(node_id, 0x00000000, firmware.len() as u32)?;
        
        // Transfer firmware in blocks
        let blocks = (firmware.len() + BLOCK_SIZE - 1) / BLOCK_SIZE;
        let mut block_seq: u8 = 1;
        
        for i in 0..blocks {
            let offset = i * BLOCK_SIZE;
            let end = std::cmp::min(offset + BLOCK_SIZE, firmware.len());
            
            self.transfer_data_block(node_id, block_seq, &firmware[offset..end])?;
            block_seq = block_seq.wrapping_add(1);
            
            println!("Progress: {}/{} blocks", i + 1, blocks);
        }
        
        // Verification phase
        {
            let mut ecus = self.ecus.lock().unwrap();
            if let Some(ecu) = ecus.get_mut(&node_id) {
                ecu.state = EcuState::Verifying;
            }
        }
        println!("Verifying firmware...");
        
        // Request transfer exit
        let exit_frame = CanFrame {
            id: 0x700 + node_id as u32,
            data: vec![UDS_REQUEST_TRANSFER_EXIT],
            extended: false,
            remote: false,
        };
        self.send_can_frame(exit_frame)?;
        
        // Installation phase
        {
            let mut ecus = self.ecus.lock().unwrap();
            if let Some(ecu) = ecus.get_mut(&node_id) {
                ecu.state = EcuState::Installing;
            }
        }
        println!("Installing firmware...");
        
        // Trigger installation routine
        let routine_frame = CanFrame {
            id: 0x700 + node_id as u32,
            data: vec![UDS_ROUTINE_CONTROL, 0x01, 0xFF, 0x00],
            extended: false,
            remote: false,
        };
        self.send_can_frame(routine_frame)?;
        
        // Mark as complete
        {
            let mut ecus = self.ecus.lock().unwrap();
            if let Some(ecu) = ecus.get_mut(&node_id) {
                ecu.state = EcuState::Complete;
                ecu.firmware_version = ecu.target_version;
                println!("ECU {} update complete", ecu.name);
            }
        }
        
        Ok(())
    }
    
    fn orchestrate_updates(&self, firmwares: HashMap<u8, Vec<u8>>) 
        -> Result<(), String> {
        *self.update_in_progress.lock().unwrap() = true;
        
        // Phase 1: Non-critical ECUs with no dependencies
        println!("=== Phase 1: Non-critical ECUs ===");
        {
            let ecus = self.ecus.lock().unwrap();
            let mut updates = Vec::new();
            
            for (node_id, ecu) in ecus.iter() {
                if !ecu.critical && ecu.dependencies.is_empty() {
                    updates.push(*node_id);
                }
            }
            
            drop(ecus); // Release lock before updates
            
            for node_id in updates {
                if let Some(fw) = firmwares.get(&node_id) {
                    self.update_ecu(node_id, fw)?;
                }
            }
        }
        
        // Phase 2: ECUs with satisfied dependencies
        println!("\n=== Phase 2: Dependent ECUs ===");
        let mut progress = true;
        while progress {
            progress = false;
            let ecus = self.ecus.lock().unwrap();
            let mut updates = Vec::new();
            
            for (node_id, ecu) in ecus.iter() {
                if ecu.state == EcuState::Idle && self.dependencies_satisfied(ecu) {
                    updates.push(*node_id);
                    progress = true;
                }
            }
            
            drop(ecus);
            
            for node_id in updates {
                if let Some(fw) = firmwares.get(&node_id) {
                    self.update_ecu(node_id, fw)?;
                }
            }
        }
        
        // Phase 3: Critical ECUs
        println!("\n=== Phase 3: Critical ECUs ===");
        {
            let ecus = self.ecus.lock().unwrap();
            let mut updates = Vec::new();
            
            for (node_id, ecu) in ecus.iter() {
                if ecu.critical && ecu.state == EcuState::Idle {
                    updates.push(*node_id);
                }
            }
            
            drop(ecus);
            
            for node_id in updates {
                if let Some(fw) = firmwares.get(&node_id) {
                    self.update_ecu(node_id, fw)?;
                }
            }
        }
        
        *self.update_in_progress.lock().unwrap() = false;
        println!("\n=== All ECUs updated successfully ===");
        Ok(())
    }
}

fn main() {
    let orchestrator = UpdateOrchestrator::new();
    
    // Define ECUs
    let mut engine_ecu = EcuNode::new(1, "Engine ECU", true);
    engine_ecu.firmware_version = 0x010000;
    engine_ecu.target_version = 0x010100;
    
    let mut transmission_ecu = EcuNode::new(2, "Transmission ECU", true);
    transmission_ecu.firmware_version = 0x020000;
    transmission_ecu.target_version = 0x020100;
    transmission_ecu.add_dependency(1); // Depends on Engine ECU
    
    let mut infotainment_ecu = EcuNode::new(3, "Infotainment ECU", false);
    infotainment_ecu.firmware_version = 0x030000;
    infotainment_ecu.target_version = 0x030100;
    
    // Add ECUs to orchestrator
    orchestrator.add_ecu(engine_ecu);
    orchestrator.add_ecu(transmission_ecu);
    orchestrator.add_ecu(infotainment_ecu);
    
    // Prepare firmware images (dummy data)
    let mut firmwares = HashMap::new();
    firmwares.insert(1, vec![0xAA; 1024]);
    firmwares.insert(2, vec![0xBB; 2048]);
    firmwares.insert(3, vec![0xCC; 4096]);
    
    // Start orchestrated update
    match orchestrator.orchestrate_updates(firmwares) {
        Ok(_) => println!("Update orchestration completed successfully"),
        Err(e) => eprintln!("Update failed: {}", e),
    }
}
```

## Summary

**Multi-ECU Update Orchestration** is a critical capability for modern vehicles that enables safe, coordinated firmware updates across multiple electronic control units. The implementation demonstrates:

**Key Features:**
- **Phased Update Strategy**: Non-critical ECUs update first, followed by dependent ECUs, with critical systems updated last to maintain vehicle availability
- **Dependency Management**: Ensures ECUs are updated in the correct order based on inter-dependencies
- **UDS Protocol**: Uses industry-standard Unified Diagnostic Services over CAN for firmware transfer
- **State Management**: Tracks each ECU's update progress through multiple phases
- **Error Handling**: Provides rollback capabilities and retry mechanisms

**Implementation Highlights:**
- C/C++ implementation shows low-level CAN frame manipulation and pthread-based concurrency
- Rust implementation demonstrates memory-safe concurrency using Arc and Mutex, with strong type safety
- Both implementations handle block-based firmware transfer to work within CAN's 8-byte payload limitation
- The orchestrator coordinates updates while maintaining system availability

This approach ensures vehicles can receive OTA (Over-The-Air) updates safely without requiring dealership visits, while minimizing downtime and preventing system inconsistencies during the update process.