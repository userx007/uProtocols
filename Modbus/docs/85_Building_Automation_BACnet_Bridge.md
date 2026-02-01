# Building Automation: Modbus-BACnet Bridge

## Overview

A Modbus-BACnet bridge serves as a protocol gateway that enables seamless communication between Modbus-based HVAC equipment (heating, ventilation, air conditioning) and BACnet building automation systems. BACnet (Building Automation and Control Networks) is the dominant protocol in commercial building automation, while many HVAC devices still use Modbus. The bridge translates between these protocols, allowing legacy Modbus equipment to participate in modern BACnet networks.

## Technical Architecture

### Protocol Translation Layers

**Modbus Side:**
- RTU (serial) or TCP (Ethernet) communication
- Register-based data model (coils, discrete inputs, holding registers, input registers)
- Master-slave architecture
- Typical HVAC data: temperatures, setpoints, fan speeds, valve positions

**BACnet Side:**
- BACnet/IP (most common) or BACnet MS/TP
- Object-oriented data model (Analog Input, Analog Output, Binary Input, Binary Output, etc.)
- Peer-to-peer communication
- Rich service model (ReadProperty, WriteProperty, COV - Change of Value notifications)

### Mapping Strategy

The bridge maps Modbus registers to BACnet objects:
- Modbus holding registers → BACnet Analog Value/Output objects
- Modbus input registers → BACnet Analog Input objects
- Modbus coils → BACnet Binary Output objects
- Modbus discrete inputs → BACnet Binary Input objects

## C/C++ Implementation

```c
#include <stdint.h>
#include <stdbool.h>
#include <modbus/modbus.h>
#include "bacnet/bacdef.h"
#include "bacnet/bacdcode.h"
#include "bacnet/npdu.h"
#include "bacnet/apdu.h"
#include "bacnet/datalink.h"

// Configuration structure for register mapping
typedef struct {
    uint16_t modbus_address;
    BACNET_OBJECT_TYPE bacnet_type;
    uint32_t bacnet_instance;
    int modbus_register_type; // 0=coil, 1=discrete, 2=holding, 3=input
    float scale_factor;
    float offset;
} RegisterMapping;

// Bridge context
typedef struct {
    modbus_t *modbus_ctx;
    uint32_t bacnet_device_instance;
    RegisterMapping *mappings;
    size_t mapping_count;
    uint16_t *register_cache;
    time_t last_poll_time;
} ModbusBacnetBridge;

// Initialize the bridge
ModbusBacnetBridge* bridge_init(const char *modbus_device, 
                                 int baud_rate,
                                 uint32_t bacnet_device_id) {
    ModbusBacnetBridge *bridge = malloc(sizeof(ModbusBacnetBridge));
    
    // Initialize Modbus RTU connection
    bridge->modbus_ctx = modbus_new_rtu(modbus_device, baud_rate, 'N', 8, 1);
    if (bridge->modbus_ctx == NULL) {
        fprintf(stderr, "Failed to create Modbus context\n");
        free(bridge);
        return NULL;
    }
    
    if (modbus_connect(bridge->modbus_ctx) == -1) {
        fprintf(stderr, "Modbus connection failed: %s\n", 
                modbus_strerror(errno));
        modbus_free(bridge->modbus_ctx);
        free(bridge);
        return NULL;
    }
    
    // Initialize BACnet stack
    bridge->bacnet_device_instance = bacnet_device_id;
    Device_Set_Object_Instance_Number(bacnet_device_id);
    
    // Initialize datalink (BACnet/IP)
    bip_set_interface("eth0");
    datalink_init("bip");
    
    bridge->last_poll_time = time(NULL);
    
    return bridge;
}

// Add a register mapping
void bridge_add_mapping(ModbusBacnetBridge *bridge,
                       uint16_t modbus_addr,
                       BACNET_OBJECT_TYPE bacnet_type,
                       uint32_t bacnet_instance,
                       int register_type,
                       float scale,
                       float offset) {
    size_t idx = bridge->mapping_count++;
    bridge->mappings = realloc(bridge->mappings, 
                               bridge->mapping_count * sizeof(RegisterMapping));
    
    bridge->mappings[idx].modbus_address = modbus_addr;
    bridge->mappings[idx].bacnet_type = bacnet_type;
    bridge->mappings[idx].bacnet_instance = bacnet_instance;
    bridge->mappings[idx].modbus_register_type = register_type;
    bridge->mappings[idx].scale_factor = scale;
    bridge->mappings[idx].offset = offset;
}

// Poll Modbus devices and update BACnet objects
void bridge_poll_modbus(ModbusBacnetBridge *bridge, uint8_t slave_id) {
    uint16_t registers[125];
    
    modbus_set_slave(bridge->modbus_ctx, slave_id);
    
    for (size_t i = 0; i < bridge->mapping_count; i++) {
        RegisterMapping *map = &bridge->mappings[i];
        int rc;
        
        switch (map->modbus_register_type) {
            case 2: // Holding register
                rc = modbus_read_registers(bridge->modbus_ctx, 
                                          map->modbus_address, 1, registers);
                if (rc == 1) {
                    float value = (registers[0] * map->scale_factor) + map->offset;
                    
                    // Update corresponding BACnet object
                    if (map->bacnet_type == OBJECT_ANALOG_VALUE) {
                        Analog_Value_Present_Value_Set(map->bacnet_instance, value);
                    }
                }
                break;
                
            case 3: // Input register
                rc = modbus_read_input_registers(bridge->modbus_ctx,
                                                 map->modbus_address, 1, registers);
                if (rc == 1) {
                    float value = (registers[0] * map->scale_factor) + map->offset;
                    
                    if (map->bacnet_type == OBJECT_ANALOG_INPUT) {
                        Analog_Input_Present_Value_Set(map->bacnet_instance, value);
                    }
                }
                break;
                
            case 0: // Coil
                {
                    uint8_t coil_value;
                    rc = modbus_read_bits(bridge->modbus_ctx,
                                        map->modbus_address, 1, &coil_value);
                    if (rc == 1) {
                        Binary_Output_Present_Value_Set(map->bacnet_instance,
                            coil_value ? BINARY_ACTIVE : BINARY_INACTIVE);
                    }
                }
                break;
        }
    }
}

// Handle BACnet write requests and update Modbus
void bridge_handle_bacnet_write(ModbusBacnetBridge *bridge,
                               BACNET_OBJECT_TYPE object_type,
                               uint32_t object_instance,
                               float value,
                               uint8_t slave_id) {
    // Find corresponding Modbus register
    for (size_t i = 0; i < bridge->mapping_count; i++) {
        RegisterMapping *map = &bridge->mappings[i];
        
        if (map->bacnet_type == object_type && 
            map->bacnet_instance == object_instance) {
            
            modbus_set_slave(bridge->modbus_ctx, slave_id);
            
            // Convert BACnet value to Modbus register value
            uint16_t modbus_value = (uint16_t)((value - map->offset) / 
                                              map->scale_factor);
            
            if (map->modbus_register_type == 2) { // Holding register
                modbus_write_register(bridge->modbus_ctx, 
                                     map->modbus_address, modbus_value);
            } else if (map->modbus_register_type == 0) { // Coil
                modbus_write_bit(bridge->modbus_ctx, 
                               map->modbus_address, (int)value);
            }
            break;
        }
    }
}

// Main bridge loop
void bridge_run(ModbusBacnetBridge *bridge) {
    while (1) {
        // Poll Modbus devices every second
        time_t now = time(NULL);
        if (now - bridge->last_poll_time >= 1) {
            bridge_poll_modbus(bridge, 1); // Poll slave ID 1
            bridge->last_poll_time = now;
        }
        
        // Process BACnet messages
        uint8_t rx_buf[MAX_MPDU];
        BACNET_ADDRESS src;
        uint16_t pdu_len;
        
        pdu_len = datalink_receive(&src, rx_buf, MAX_MPDU, 100);
        
        if (pdu_len > 0) {
            npdu_handler(&src, rx_buf, pdu_len);
        }
    }
}

// Example: Configure bridge for HVAC application
int main(void) {
    ModbusBacnetBridge *bridge = bridge_init("/dev/ttyUSB0", 19200, 123456);
    
    if (!bridge) {
        return 1;
    }
    
    // Map room temperature (Modbus input register 0 → BACnet AI 1)
    // Temperature in 0.1°C units
    bridge_add_mapping(bridge, 0, OBJECT_ANALOG_INPUT, 1, 3, 0.1, 0.0);
    
    // Map temperature setpoint (Modbus holding register 10 → BACnet AV 1)
    bridge_add_mapping(bridge, 10, OBJECT_ANALOG_VALUE, 1, 2, 0.1, 0.0);
    
    // Map fan status (Modbus coil 0 → BACnet BO 1)
    bridge_add_mapping(bridge, 0, OBJECT_BINARY_OUTPUT, 1, 0, 1.0, 0.0);
    
    // Map damper position (Modbus holding register 20 → BACnet AO 1)
    // 0-10000 = 0-100%
    bridge_add_mapping(bridge, 20, OBJECT_ANALOG_OUTPUT, 1, 2, 0.01, 0.0);
    
    bridge_run(bridge);
    
    return 0;
}
```

## Rust Implementation

```rust
use tokio::time::{interval, Duration};
use tokio_modbus::prelude::*;
use bacnet::{BacnetDevice, BacnetObject, ObjectType, PropertyIdentifier};
use std::collections::HashMap;
use std::sync::Arc;
use tokio::sync::RwLock;

#[derive(Debug, Clone)]
struct RegisterMapping {
    modbus_address: u16,
    modbus_type: ModbusRegisterType,
    bacnet_object_type: ObjectType,
    bacnet_instance: u32,
    scale_factor: f32,
    offset: f32,
}

#[derive(Debug, Clone, Copy)]
enum ModbusRegisterType {
    Coil,
    DiscreteInput,
    HoldingRegister,
    InputRegister,
}

struct ModbusBacnetBridge {
    modbus_ctx: Arc<RwLock<client::Context>>,
    bacnet_device: Arc<RwLock<BacnetDevice>>,
    mappings: Vec<RegisterMapping>,
    modbus_slave_id: u8,
}

impl ModbusBacnetBridge {
    async fn new(
        modbus_device: &str,
        baud_rate: u32,
        bacnet_device_id: u32,
        slave_id: u8,
    ) -> Result<Self, Box<dyn std::error::Error>> {
        // Initialize Modbus RTU context
        let modbus_ctx = client::rtu::connect_slave(
            modbus_device,
            Slave(slave_id)
        ).await?;
        
        // Initialize BACnet device
        let bacnet_device = BacnetDevice::new(bacnet_device_id)?;
        
        Ok(Self {
            modbus_ctx: Arc::new(RwLock::new(modbus_ctx)),
            bacnet_device: Arc::new(RwLock::new(bacnet_device)),
            mappings: Vec::new(),
            modbus_slave_id: slave_id,
        })
    }
    
    fn add_mapping(&mut self, mapping: RegisterMapping) {
        self.mappings.push(mapping);
    }
    
    async fn poll_modbus(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut modbus = self.modbus_ctx.write().await;
        let mut bacnet = self.bacnet_device.write().await;
        
        for mapping in &self.mappings {
            match mapping.modbus_type {
                ModbusRegisterType::HoldingRegister => {
                    let registers = modbus
                        .read_holding_registers(mapping.modbus_address, 1)
                        .await?;
                    
                    if let Some(&reg_value) = registers.first() {
                        let value = (reg_value as f32 * mapping.scale_factor) 
                                  + mapping.offset;
                        
                        // Update BACnet object
                        bacnet.set_property(
                            mapping.bacnet_object_type,
                            mapping.bacnet_instance,
                            PropertyIdentifier::PresentValue,
                            value,
                        )?;
                    }
                }
                
                ModbusRegisterType::InputRegister => {
                    let registers = modbus
                        .read_input_registers(mapping.modbus_address, 1)
                        .await?;
                    
                    if let Some(&reg_value) = registers.first() {
                        let value = (reg_value as f32 * mapping.scale_factor) 
                                  + mapping.offset;
                        
                        bacnet.set_property(
                            mapping.bacnet_object_type,
                            mapping.bacnet_instance,
                            PropertyIdentifier::PresentValue,
                            value,
                        )?;
                    }
                }
                
                ModbusRegisterType::Coil => {
                    let coils = modbus
                        .read_coils(mapping.modbus_address, 1)
                        .await?;
                    
                    if let Some(&coil_value) = coils.first() {
                        bacnet.set_property(
                            mapping.bacnet_object_type,
                            mapping.bacnet_instance,
                            PropertyIdentifier::PresentValue,
                            if coil_value { 1.0 } else { 0.0 },
                        )?;
                    }
                }
                
                ModbusRegisterType::DiscreteInput => {
                    let inputs = modbus
                        .read_discrete_inputs(mapping.modbus_address, 1)
                        .await?;
                    
                    if let Some(&input_value) = inputs.first() {
                        bacnet.set_property(
                            mapping.bacnet_object_type,
                            mapping.bacnet_instance,
                            PropertyIdentifier::PresentValue,
                            if input_value { 1.0 } else { 0.0 },
                        )?;
                    }
                }
            }
        }
        
        Ok(())
    }
    
    async fn handle_bacnet_write(
        &self,
        object_type: ObjectType,
        instance: u32,
        value: f32,
    ) -> Result<(), Box<dyn std::error::Error>> {
        // Find corresponding Modbus mapping
        for mapping in &self.mappings {
            if mapping.bacnet_object_type == object_type 
               && mapping.bacnet_instance == instance {
                
                let mut modbus = self.modbus_ctx.write().await;
                
                match mapping.modbus_type {
                    ModbusRegisterType::HoldingRegister => {
                        let modbus_value = ((value - mapping.offset) 
                                          / mapping.scale_factor) as u16;
                        modbus.write_single_register(
                            mapping.modbus_address,
                            modbus_value
                        ).await?;
                    }
                    
                    ModbusRegisterType::Coil => {
                        modbus.write_single_coil(
                            mapping.modbus_address,
                            value > 0.5
                        ).await?;
                    }
                    
                    _ => {
                        return Err("Cannot write to input register or discrete input".into());
                    }
                }
                
                break;
            }
        }
        
        Ok(())
    }
    
    async fn run(&self) -> Result<(), Box<dyn std::error::Error>> {
        let mut poll_interval = interval(Duration::from_secs(1));
        
        loop {
            tokio::select! {
                _ = poll_interval.tick() => {
                    if let Err(e) = self.poll_modbus().await {
                        eprintln!("Modbus poll error: {}", e);
                    }
                }
                
                // Handle BACnet requests (pseudo-code, actual implementation
                // would use proper BACnet message handling)
                bacnet_req = self.receive_bacnet_request() => {
                    if let Ok((obj_type, instance, value)) = bacnet_req {
                        if let Err(e) = self.handle_bacnet_write(
                            obj_type, instance, value
                        ).await {
                            eprintln!("BACnet write error: {}", e);
                        }
                    }
                }
            }
        }
    }
    
    // Placeholder for BACnet request handling
    async fn receive_bacnet_request(&self) 
        -> Result<(ObjectType, u32, f32), Box<dyn std::error::Error>> {
        // Actual implementation would handle BACnet protocol
        tokio::time::sleep(Duration::from_millis(100)).await;
        Err("No request".into())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut bridge = ModbusBacnetBridge::new(
        "/dev/ttyUSB0",
        19200,
        123456,
        1
    ).await?;
    
    // Configure HVAC mappings
    
    // Room temperature sensor
    bridge.add_mapping(RegisterMapping {
        modbus_address: 0,
        modbus_type: ModbusRegisterType::InputRegister,
        bacnet_object_type: ObjectType::AnalogInput,
        bacnet_instance: 1,
        scale_factor: 0.1,  // 0.1°C per unit
        offset: 0.0,
    });
    
    // Temperature setpoint
    bridge.add_mapping(RegisterMapping {
        modbus_address: 10,
        modbus_type: ModbusRegisterType::HoldingRegister,
        bacnet_object_type: ObjectType::AnalogValue,
        bacnet_instance: 1,
        scale_factor: 0.1,
        offset: 0.0,
    });
    
    // Fan control
    bridge.add_mapping(RegisterMapping {
        modbus_address: 0,
        modbus_type: ModbusRegisterType::Coil,
        bacnet_object_type: ObjectType::BinaryOutput,
        bacnet_instance: 1,
        scale_factor: 1.0,
        offset: 0.0,
    });
    
    // Damper position (0-100%)
    bridge.add_mapping(RegisterMapping {
        modbus_address: 20,
        modbus_type: ModbusRegisterType::HoldingRegister,
        bacnet_object_type: ObjectType::AnalogOutput,
        bacnet_instance: 1,
        scale_factor: 0.01,  // 0-10000 = 0-100%
        offset: 0.0,
    });
    
    bridge.run().await
}
```

## Summary

A Modbus-BACnet bridge enables integration of Modbus HVAC equipment into BACnet building automation systems through protocol translation. The bridge maps Modbus registers to BACnet objects, handles bidirectional data flow, and manages the different communication paradigms of both protocols.

**Key aspects include:**
- Register-to-object mapping with scaling/offset transformations
- Periodic polling of Modbus devices to update BACnet object values
- Handling BACnet write requests and translating them to Modbus commands
- Supporting both read-only monitoring and bi-directional control
- Proper error handling for network failures on either protocol side

**Common applications:**
- Integrating legacy Modbus chillers into BACnet BMS systems
- Connecting Modbus VAV boxes to BACnet supervisory controllers
- Bridging Modbus heat pumps to enterprise building automation
- Enabling centralized monitoring of mixed-protocol HVAC installations

The implementations shown demonstrate core bridge functionality with real-world mapping examples for typical HVAC parameters like temperatures, setpoints, fan controls, and damper positions.