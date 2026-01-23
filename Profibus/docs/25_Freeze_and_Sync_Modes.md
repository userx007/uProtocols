# Profibus Freeze and Sync Modes

## Overview

Freeze and Sync modes are advanced Profibus DP (Decentralized Periphery) features that enable synchronized data acquisition and output across multiple slaves. These modes are crucial for applications requiring precise timing coordination, such as motion control systems, coordinated manufacturing processes, and synchronized measurement systems.

## Detailed Description

### Freeze Mode

**Freeze mode** allows a master to capture a consistent snapshot of input data from multiple slaves simultaneously. When a Freeze command is issued, all addressed slaves "freeze" their input data at the same moment, ensuring time-coherent data acquisition across the network.

**Key characteristics:**
- Slaves lock their current input values when receiving the Freeze command
- The frozen data remains stable until the next Freeze command
- Eliminates timing skew between multiple sensor readings
- Essential for applications where relative timing between measurements matters

**Use cases:**
- Synchronized sensor data collection in quality control
- Coordinated position feedback in multi-axis systems
- Simultaneous analog input sampling across multiple modules

### Sync Mode

**Sync mode** coordinates the simultaneous output of data to multiple slaves. The master sends output data to slaves, but the slaves don't apply this data to their physical outputs until receiving a Sync command. This ensures all outputs change at precisely the same moment.

**Key characteristics:**
- Slaves buffer output data without applying it immediately
- Data is applied to physical outputs only upon Sync command
- Guarantees simultaneous output updates across multiple devices
- Critical for coordinated motion and process control

**Use cases:**
- Synchronized motion control across multiple axes
- Coordinated valve operations in process control
- Simultaneous setpoint changes in distributed control systems

### Control Commands

Freeze and Sync are implemented through special Profibus control commands:
- **Freeze command (0x0D)**: Triggers simultaneous input data capture
- **Sync command (0x0E)**: Triggers simultaneous output data application
- **Control/Uncontrol**: Enable/disable these modes for specific slave groups

## Code Examples

### C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// Profibus DP control command codes
#define PROFIBUS_CMD_FREEZE         0x0D
#define PROFIBUS_CMD_SYNC           0x0E
#define PROFIBUS_CMD_SET_SLAVE_ADD  0x37
#define PROFIBUS_CMD_GLOBAL_CONTROL 0x3C

// Freeze/Sync group definitions
typedef enum {
    FREEZE_SYNC_GROUP_1 = 0x01,
    FREEZE_SYNC_GROUP_2 = 0x02,
    FREEZE_SYNC_GROUP_3 = 0x04,
    FREEZE_SYNC_GROUP_4 = 0x08,
    FREEZE_SYNC_GROUP_5 = 0x10,
    FREEZE_SYNC_GROUP_6 = 0x20,
    FREEZE_SYNC_GROUP_7 = 0x40,
    FREEZE_SYNC_GROUP_8 = 0x80
} FreezeSyncGroup;

// Slave configuration structure
typedef struct {
    uint8_t address;
    FreezeSyncGroup freeze_group;
    FreezeSyncGroup sync_group;
    bool freeze_enabled;
    bool sync_enabled;
} SlaveConfig;

// Profibus telegram structure
typedef struct {
    uint8_t start_delimiter;
    uint8_t destination_addr;
    uint8_t source_addr;
    uint8_t function_code;
    uint8_t data[246];
    uint16_t data_length;
    uint8_t checksum;
} ProfibusFrame;

// Master context
typedef struct {
    SlaveConfig slaves[126];
    uint8_t num_slaves;
    bool (*send_frame)(ProfibusFrame *frame);
    bool (*receive_frame)(ProfibusFrame *frame);
} ProfibusContext;

// Calculate frame checksum
uint8_t calculate_checksum(ProfibusFrame *frame) {
    uint8_t checksum = frame->destination_addr + 
                       frame->source_addr + 
                       frame->function_code;
    
    for (uint16_t i = 0; i < frame->data_length; i++) {
        checksum += frame->data[i];
    }
    
    return checksum;
}

// Send Freeze command to a specific group
bool send_freeze_command(ProfibusContext *ctx, FreezeSyncGroup group) {
    ProfibusFrame frame = {0};
    
    frame.start_delimiter = 0x68;  // Start delimiter for variable length
    frame.destination_addr = 0xFF; // Broadcast to group
    frame.source_addr = 0x00;      // Master address
    frame.function_code = PROFIBUS_CMD_FREEZE;
    frame.data[0] = group;         // Group selector
    frame.data_length = 1;
    frame.checksum = calculate_checksum(&frame);
    
    return ctx->send_frame(&frame);
}

// Send Sync command to a specific group
bool send_sync_command(ProfibusContext *ctx, FreezeSyncGroup group) {
    ProfibusFrame frame = {0};
    
    frame.start_delimiter = 0x68;
    frame.destination_addr = 0xFF;
    frame.source_addr = 0x00;
    frame.function_code = PROFIBUS_CMD_SYNC;
    frame.data[0] = group;
    frame.data_length = 1;
    frame.checksum = calculate_checksum(&frame);
    
    return ctx->send_frame(&frame);
}

// Enable Freeze mode for a slave
bool enable_freeze_mode(ProfibusContext *ctx, uint8_t slave_addr, 
                        FreezeSyncGroup group) {
    ProfibusFrame frame = {0};
    
    frame.start_delimiter = 0x68;
    frame.destination_addr = slave_addr;
    frame.source_addr = 0x00;
    frame.function_code = PROFIBUS_CMD_GLOBAL_CONTROL;
    
    // Control byte: bit 0 = Freeze enable, bits 1-3 = group
    frame.data[0] = 0x01 | ((group & 0x07) << 1);
    frame.data_length = 1;
    frame.checksum = calculate_checksum(&frame);
    
    if (ctx->send_frame(&frame)) {
        // Update local configuration
        for (int i = 0; i < ctx->num_slaves; i++) {
            if (ctx->slaves[i].address == slave_addr) {
                ctx->slaves[i].freeze_enabled = true;
                ctx->slaves[i].freeze_group = group;
                return true;
            }
        }
    }
    
    return false;
}

// Enable Sync mode for a slave
bool enable_sync_mode(ProfibusContext *ctx, uint8_t slave_addr, 
                      FreezeSyncGroup group) {
    ProfibusFrame frame = {0};
    
    frame.start_delimiter = 0x68;
    frame.destination_addr = slave_addr;
    frame.source_addr = 0x00;
    frame.function_code = PROFIBUS_CMD_GLOBAL_CONTROL;
    
    // Control byte: bit 4 = Sync enable, bits 5-7 = group
    frame.data[0] = 0x10 | ((group & 0x07) << 5);
    frame.data_length = 1;
    frame.checksum = calculate_checksum(&frame);
    
    if (ctx->send_frame(&frame)) {
        // Update local configuration
        for (int i = 0; i < ctx->num_slaves; i++) {
            if (ctx->slaves[i].address == slave_addr) {
                ctx->slaves[i].sync_enabled = true;
                ctx->slaves[i].sync_group = group;
                return true;
            }
        }
    }
    
    return false;
}

// Synchronized data acquisition workflow
bool synchronized_read_workflow(ProfibusContext *ctx, FreezeSyncGroup group,
                                uint8_t *slave_addresses, uint8_t num_slaves,
                                uint8_t **output_data, uint16_t *data_lengths) {
    // Step 1: Issue Freeze command
    if (!send_freeze_command(ctx, group)) {
        return false;
    }
    
    // Small delay to ensure all slaves have frozen their data
    // In real implementation, use proper timing mechanism
    usleep(1000); // 1ms delay
    
    // Step 2: Read data from each slave
    for (uint8_t i = 0; i < num_slaves; i++) {
        ProfibusFrame request = {0};
        ProfibusFrame response = {0};
        
        request.start_delimiter = 0x68;
        request.destination_addr = slave_addresses[i];
        request.source_addr = 0x00;
        request.function_code = 0x0C; // Read cyclic data
        request.data_length = 0;
        request.checksum = calculate_checksum(&request);
        
        if (!ctx->send_frame(&request)) {
            return false;
        }
        
        if (!ctx->receive_frame(&response)) {
            return false;
        }
        
        // Copy received data
        output_data[i] = malloc(response.data_length);
        memcpy(output_data[i], response.data, response.data_length);
        data_lengths[i] = response.data_length;
    }
    
    return true;
}

// Synchronized output workflow
bool synchronized_write_workflow(ProfibusContext *ctx, FreezeSyncGroup group,
                                 uint8_t *slave_addresses, uint8_t num_slaves,
                                 uint8_t **input_data, uint16_t *data_lengths) {
    // Step 1: Send output data to all slaves (buffered, not applied yet)
    for (uint8_t i = 0; i < num_slaves; i++) {
        ProfibusFrame frame = {0};
        
        frame.start_delimiter = 0x68;
        frame.destination_addr = slave_addresses[i];
        frame.source_addr = 0x00;
        frame.function_code = 0x0B; // Write cyclic data
        
        memcpy(frame.data, input_data[i], data_lengths[i]);
        frame.data_length = data_lengths[i];
        frame.checksum = calculate_checksum(&frame);
        
        if (!ctx->send_frame(&frame)) {
            return false;
        }
    }
    
    // Step 2: Issue Sync command to apply all outputs simultaneously
    return send_sync_command(ctx, group);
}

// Example: Multi-axis motion control
void example_motion_control(ProfibusContext *ctx) {
    uint8_t axis_addresses[] = {10, 11, 12, 13}; // 4 servo drives
    uint8_t num_axes = 4;
    FreezeSyncGroup motion_group = FREEZE_SYNC_GROUP_1;
    
    // Enable Sync mode for all axes
    for (int i = 0; i < num_axes; i++) {
        enable_sync_mode(ctx, axis_addresses[i], motion_group);
    }
    
    // Prepare position commands for synchronized move
    uint8_t *position_commands[4];
    uint16_t command_lengths[4];
    
    for (int i = 0; i < num_axes; i++) {
        position_commands[i] = malloc(8);
        // Pack position command (example: 32-bit position + 32-bit velocity)
        int32_t position = 10000 * (i + 1);
        int32_t velocity = 5000;
        
        memcpy(position_commands[i], &position, 4);
        memcpy(position_commands[i] + 4, &velocity, 4);
        command_lengths[i] = 8;
    }
    
    // Execute synchronized motion
    if (synchronized_write_workflow(ctx, motion_group, axis_addresses, 
                                    num_axes, position_commands, 
                                    command_lengths)) {
        printf("Synchronized motion command executed successfully\n");
    }
    
    // Cleanup
    for (int i = 0; i < num_axes; i++) {
        free(position_commands[i]);
    }
}
```

### Rust Implementation

```rust
use std::collections::HashMap;
use std::time::Duration;
use std::thread;

// Profibus command codes
const PROFIBUS_CMD_FREEZE: u8 = 0x0D;
const PROFIBUS_CMD_SYNC: u8 = 0x0E;
const PROFIBUS_CMD_GLOBAL_CONTROL: u8 = 0x3C;

// Freeze/Sync group flags
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum FreezeSyncGroup {
    Group1 = 0x01,
    Group2 = 0x02,
    Group3 = 0x04,
    Group4 = 0x08,
    Group5 = 0x10,
    Group6 = 0x20,
    Group7 = 0x40,
    Group8 = 0x80,
}

// Slave configuration
#[derive(Debug, Clone)]
pub struct SlaveConfig {
    pub address: u8,
    pub freeze_group: Option<FreezeSyncGroup>,
    pub sync_group: Option<FreezeSyncGroup>,
    pub freeze_enabled: bool,
    pub sync_enabled: bool,
}

// Profibus frame structure
#[derive(Debug, Clone)]
pub struct ProfibusFrame {
    pub start_delimiter: u8,
    pub destination_addr: u8,
    pub source_addr: u8,
    pub function_code: u8,
    pub data: Vec<u8>,
    pub checksum: u8,
}

impl ProfibusFrame {
    pub fn new(dest: u8, src: u8, function: u8, data: Vec<u8>) -> Self {
        let mut frame = ProfibusFrame {
            start_delimiter: 0x68,
            destination_addr: dest,
            source_addr: src,
            function_code: function,
            data,
            checksum: 0,
        };
        frame.checksum = frame.calculate_checksum();
        frame
    }

    fn calculate_checksum(&self) -> u8 {
        let mut checksum = self.destination_addr
            .wrapping_add(self.source_addr)
            .wrapping_add(self.function_code);
        
        for byte in &self.data {
            checksum = checksum.wrapping_add(*byte);
        }
        
        checksum
    }
}

// Error types
#[derive(Debug)]
pub enum ProfibusError {
    SendError(String),
    ReceiveError(String),
    SlaveNotFound(u8),
    InvalidConfiguration(String),
}

type Result<T> = std::result::Result<T, ProfibusError>;

// Profibus master context
pub struct ProfibusContext {
    slaves: HashMap<u8, SlaveConfig>,
    send_fn: Box<dyn Fn(&ProfibusFrame) -> Result<()> + Send>,
    receive_fn: Box<dyn Fn() -> Result<ProfibusFrame> + Send>,
}

impl ProfibusContext {
    pub fn new(
        send_fn: impl Fn(&ProfibusFrame) -> Result<()> + Send + 'static,
        receive_fn: impl Fn() -> Result<ProfibusFrame> + Send + 'static,
    ) -> Self {
        ProfibusContext {
            slaves: HashMap::new(),
            send_fn: Box::new(send_fn),
            receive_fn: Box::new(receive_fn),
        }
    }

    pub fn add_slave(&mut self, config: SlaveConfig) {
        self.slaves.insert(config.address, config);
    }

    // Send Freeze command to a specific group
    pub fn send_freeze_command(&self, group: FreezeSyncGroup) -> Result<()> {
        let frame = ProfibusFrame::new(
            0xFF, // Broadcast
            0x00, // Master address
            PROFIBUS_CMD_FREEZE,
            vec![group as u8],
        );
        
        (self.send_fn)(&frame)
    }

    // Send Sync command to a specific group
    pub fn send_sync_command(&self, group: FreezeSyncGroup) -> Result<()> {
        let frame = ProfibusFrame::new(
            0xFF, // Broadcast
            0x00, // Master address
            PROFIBUS_CMD_SYNC,
            vec![group as u8],
        );
        
        (self.send_fn)(&frame)
    }

    // Enable Freeze mode for a slave
    pub fn enable_freeze_mode(
        &mut self,
        slave_addr: u8,
        group: FreezeSyncGroup,
    ) -> Result<()> {
        // Control byte: bit 0 = Freeze enable, bits 1-3 = group
        let control_byte = 0x01 | (((group as u8) & 0x07) << 1);
        
        let frame = ProfibusFrame::new(
            slave_addr,
            0x00,
            PROFIBUS_CMD_GLOBAL_CONTROL,
            vec![control_byte],
        );
        
        (self.send_fn)(&frame)?;
        
        // Update local configuration
        if let Some(slave) = self.slaves.get_mut(&slave_addr) {
            slave.freeze_enabled = true;
            slave.freeze_group = Some(group);
            Ok(())
        } else {
            Err(ProfibusError::SlaveNotFound(slave_addr))
        }
    }

    // Enable Sync mode for a slave
    pub fn enable_sync_mode(
        &mut self,
        slave_addr: u8,
        group: FreezeSyncGroup,
    ) -> Result<()> {
        // Control byte: bit 4 = Sync enable, bits 5-7 = group
        let control_byte = 0x10 | (((group as u8) & 0x07) << 5);
        
        let frame = ProfibusFrame::new(
            slave_addr,
            0x00,
            PROFIBUS_CMD_GLOBAL_CONTROL,
            vec![control_byte],
        );
        
        (self.send_fn)(&frame)?;
        
        // Update local configuration
        if let Some(slave) = self.slaves.get_mut(&slave_addr) {
            slave.sync_enabled = true;
            slave.sync_group = Some(group);
            Ok(())
        } else {
            Err(ProfibusError::SlaveNotFound(slave_addr))
        }
    }

    // Synchronized data acquisition workflow
    pub fn synchronized_read_workflow(
        &self,
        group: FreezeSyncGroup,
        slave_addresses: &[u8],
    ) -> Result<Vec<Vec<u8>>> {
        // Step 1: Issue Freeze command
        self.send_freeze_command(group)?;
        
        // Small delay to ensure all slaves have frozen their data
        thread::sleep(Duration::from_millis(1));
        
        // Step 2: Read data from each slave
        let mut results = Vec::new();
        
        for &addr in slave_addresses {
            let request = ProfibusFrame::new(
                addr,
                0x00,
                0x0C, // Read cyclic data
                vec![],
            );
            
            (self.send_fn)(&request)?;
            let response = (self.receive_fn)()?;
            
            results.push(response.data);
        }
        
        Ok(results)
    }

    // Synchronized output workflow
    pub fn synchronized_write_workflow(
        &self,
        group: FreezeSyncGroup,
        slave_addresses: &[u8],
        data: &[Vec<u8>],
    ) -> Result<()> {
        if slave_addresses.len() != data.len() {
            return Err(ProfibusError::InvalidConfiguration(
                "Slave addresses and data length mismatch".to_string(),
            ));
        }
        
        // Step 1: Send output data to all slaves (buffered, not applied yet)
        for (i, &addr) in slave_addresses.iter().enumerate() {
            let frame = ProfibusFrame::new(
                addr,
                0x00,
                0x0B, // Write cyclic data
                data[i].clone(),
            );
            
            (self.send_fn)(&frame)?;
        }
        
        // Step 2: Issue Sync command to apply all outputs simultaneously
        self.send_sync_command(group)
    }
}

// Example: Multi-axis motion controller
pub struct MotionController {
    profibus: ProfibusContext,
    axis_addresses: Vec<u8>,
    motion_group: FreezeSyncGroup,
}

impl MotionController {
    pub fn new(profibus: ProfibusContext, axis_addresses: Vec<u8>) -> Self {
        MotionController {
            profibus,
            axis_addresses,
            motion_group: FreezeSyncGroup::Group1,
        }
    }

    pub fn initialize(&mut self) -> Result<()> {
        // Enable Sync mode for all axes
        for &addr in &self.axis_addresses {
            self.profibus.enable_sync_mode(addr, self.motion_group)?;
        }
        Ok(())
    }

    pub fn synchronized_move(
        &self,
        positions: &[i32],
        velocities: &[i32],
    ) -> Result<()> {
        if positions.len() != self.axis_addresses.len()
            || velocities.len() != self.axis_addresses.len()
        {
            return Err(ProfibusError::InvalidConfiguration(
                "Position/velocity array size mismatch".to_string(),
            ));
        }

        // Prepare position commands
        let mut commands = Vec::new();
        
        for i in 0..positions.len() {
            let mut cmd = Vec::new();
            cmd.extend_from_slice(&positions[i].to_le_bytes());
            cmd.extend_from_slice(&velocities[i].to_le_bytes());
            commands.push(cmd);
        }

        // Execute synchronized motion
        self.profibus.synchronized_write_workflow(
            self.motion_group,
            &self.axis_addresses,
            &commands,
        )
    }

    pub fn read_synchronized_positions(&self) -> Result<Vec<i32>> {
        let data = self.profibus.synchronized_read_workflow(
            self.motion_group,
            &self.axis_addresses,
        )?;

        let mut positions = Vec::new();
        
        for bytes in data {
            if bytes.len() >= 4 {
                let pos = i32::from_le_bytes([bytes[0], bytes[1], bytes[2], bytes[3]]);
                positions.push(pos);
            }
        }

        Ok(positions)
    }
}

// Example usage
fn example_usage() -> Result<()> {
    // Create mock send/receive functions for demonstration
    let send_fn = |frame: &ProfibusFrame| -> Result<()> {
        println!("Sending frame to addr {}: func=0x{:02X}", 
                 frame.destination_addr, frame.function_code);
        Ok(())
    };

    let receive_fn = || -> Result<ProfibusFrame> {
        Ok(ProfibusFrame::new(0x00, 0x0A, 0x0C, vec![0x12, 0x34, 0x56, 0x78]))
    };

    let mut ctx = ProfibusContext::new(send_fn, receive_fn);

    // Configure slaves
    for addr in 10..=13 {
        ctx.add_slave(SlaveConfig {
            address: addr,
            freeze_group: None,
            sync_group: None,
            freeze_enabled: false,
            sync_enabled: false,
        });
    }

    // Create motion controller
    let mut controller = MotionController::new(ctx, vec![10, 11, 12, 13]);
    controller.initialize()?;

    // Execute synchronized move
    let positions = vec![10000, 20000, 30000, 40000];
    let velocities = vec![5000, 5000, 5000, 5000];
    
    controller.synchronized_move(&positions, &velocities)?;
    println!("Synchronized motion command executed successfully");

    Ok(())
}
```

## Summary

**Profibus Freeze and Sync modes** are sophisticated mechanisms for achieving deterministic, synchronized operation across distributed field devices. **Freeze mode** enables time-coherent data acquisition by simultaneously capturing input states across multiple slaves, eliminating timing skew in sensor readings. **Sync mode** coordinates output updates, ensuring that all commanded changes take effect at precisely the same moment across the network.

These modes operate through group-based control, allowing the master to organize slaves into up to 8 logical groups and issue coordinated commands. The implementation requires proper slave configuration, group assignment, and careful sequencing of control telegrams. Both modes are essential for applications demanding tight temporal coordination, such as multi-axis motion control, synchronized process operations, and coordinated measurement systems where relative timing between devices is critical to system performance and accuracy.

The code examples demonstrate practical implementations including group configuration, command sequencing, error handling, and complete workflows for synchronized read/write operations in both C/C++ and Rust, suitable for industrial automation applications requiring deterministic, coordinated field device control.