# CAN Protocol Knowledge Base for Senior Engineers

## Fundamentals

[01. **CAN Bus Architecture and Physical Layer**](docs/01_CAN_Bus_Architecture_and_Physical_Layer.md)<br>
Understanding the differential signaling, bus topology, termination resistors, and electrical characteristics of CAN networks.

[02. **CAN Frame Structure**](docs/02_CAN_Frame_Structure.md)<br>
Deep dive into standard and extended frame formats, including SOF, arbitration field, control field, data field, CRC, ACK, and EOF.

[03. **Bit Timing and Synchronization**](docs/03_Bit_Timing_and_Synchronization.md)<br>
Configuration of bit timing parameters, sample points, time quanta, and synchronization jump width for reliable communication.

[04. **Arbitration and Priority**](docs/04_Arbitration_and_Priority.md)<br>
How CAN messages compete for bus access using identifier-based priority and non-destructive bitwise arbitration.

[05. **Error Detection Mechanisms**](docs/05_Error_Detection_Mechanisms.md)<br>
CRC checks, frame checks, ACK errors, bit stuffing rules, and the five types of CAN errors.

## Protocol Layers

[06. **CAN 2.0A vs 2.0B Standards**](docs/06_CAN_2_0A_vs_2_0B_Standards.md)<br>
Differences between standard 11-bit and extended 29-bit identifier formats and their use cases.

[07. **CAN FD (Flexible Data Rate)**](docs/07_CAN_FD_Flexible_Data_Rate.md)<br>
Enhanced protocol supporting higher data rates and larger payloads up to 64 bytes.

[08. **CAN XL Protocol**](docs/08_CAN_XL_Protocol.md)<br>
Next generation CAN with support for up to 2048 bytes payload and 10 Mbit/s data phase.

[09. **ISO-TP (ISO 15765-2)**](docs/09_ISO_TP_ISO_15765_2.md)<br>
Transport protocol for segmentation and reassembly of messages larger than 8 bytes.

[10. **CANopen Protocol**](docs/10_CANopen_Protocol.md)<br>
Higher-layer protocol with object dictionary, PDO, SDO, NMT, and emergency messages for industrial automation.

## Hardware Interface

[11. **CAN Controller Integration**](docs/11_CAN_Controller_Integration.md)<br>
Working with hardware CAN controllers like MCP2515, SJA1000, and integrated MCU peripherals.

[12. **CAN Transceiver Selection**](docs/12_CAN_Transceiver_Selection.md)<br>
Choosing appropriate transceivers (TJA1050, MCP2551, SN65HVD230) based on speed and environmental requirements.

[13. **Hardware Filtering and Masking**](docs/13_Hardware_Filtering_and_Masking.md)<br>
Configuring acceptance filters and masks to reduce CPU load by filtering unwanted messages at hardware level.

[14. **DMA for CAN Reception**](docs/14_DMA_for_CAN_Reception.md)<br>
Using Direct Memory Access to handle high-throughput CAN traffic efficiently.

[15. **Timestamp and Time Synchronization**](docs/15_Timestamp_and_Time_Synchronization.md)<br>
Implementing precise message timestamping for logging, diagnostics, and time-triggered communication.

## Software Implementation

[16. **SocketCAN on Linux**](docs/16_SocketCAN_on_Linux.md)<br>
Using the native Linux CAN framework for application development with standard socket APIs.

[17. **Ring Buffers for CAN Queuing**](docs/17_Ring_Buffers_for_CAN_Queuing.md)<br>
Implementing circular buffers for efficient TX/RX message queuing without dynamic allocation.

[18. **Interrupt-Driven CAN Handling**](docs/18_Interrupt_Driven_CAN_Handling.md)<br>
Designing interrupt service routines for low-latency message processing and error handling.

[19. **Message Scheduling Strategies**](docs/19_Message_Scheduling_Strategies.md)<br>
Time-triggered vs event-triggered transmission patterns and priority-based scheduling.

[20. **Zero-Copy Message Processing**](docs/20_Zero_Copy_Message_Processing.md)<br>
Techniques to minimize memory copies in CAN stack implementation for performance optimization.

## Diagnostics and Testing

[21. **UDS (Unified Diagnostic Services)**](docs/21_UDS_Unified_Diagnostic_Services.md)<br>
ISO 14229 standard for automotive diagnostics including session control, data transfer, and fault memory access.

[22. **OBD-II over CAN**](docs/22_OBD_II_over_CAN.md)<br>
On-board diagnostics implementation using CAN with standard PIDs and diagnostic trouble codes.

[23. **CAN Bus Monitoring and Sniffing**](docs/23_CAN_Bus_Monitoring_and_Sniffing.md)<br>
Tools and techniques for passive bus observation, traffic analysis, and protocol debugging.

[24. **Error Frame Injection**](docs/24_Error_Frame_Injection.md)<br>
Programmatically generating error frames for testing error handling and bus recovery mechanisms.

[25. **Bus Load Calculation**](docs/25_Bus_Load_Calculation.md)<br>
Methods to measure and optimize CAN bus utilization to prevent bus overload conditions.

## Security

[26. **CAN Bus Authentication**](docs/26_CAN_Bus_Authentication.md)<br>
Implementing message authentication codes (MAC) to prevent spoofing and replay attacks.

[27. **Secure Onboard Communication (SecOC)**](docs/27_Secure_Onboard_Communication_SecOC.md)<br>
AUTOSAR standard for cryptographic protection of CAN messages in automotive systems.

[28. **Intrusion Detection Systems**](docs/28_Intrusion_Detection_Systems.md)<br>
Monitoring CAN traffic patterns to detect anomalous behavior and potential attacks.

[29. **Gateway Firewalling**](docs/29_Gateway_Firewalling.md)<br>
Implementing filtering rules at CAN gateways to isolate network segments and prevent unauthorized access.

[30. **Fuzzing CAN Protocols**](docs/30_Fuzzing_CAN_Protocols.md)<br>
Using fuzzing techniques to discover vulnerabilities in CAN implementations and protocol stacks.

## Advanced Topics

[31. **CAN Gateway and Routing**](docs/31_CAN_Gateway_and_Routing.md)<br>
Bridging multiple CAN networks with message filtering, translation, and routing logic.

[32. **J1939 Protocol for Heavy Vehicles**](docs/32_J1939_Protocol_for_Heavy_Vehicles.md)<br>
SAE standard for commercial vehicles with parameter groups numbers and transport protocol.

[33. **Database Files (DBC/KCD)**](docs/33_Database_Files_DBC_KCD.md)<br>
Working with CAN database formats to define signals, messages, and network configurations.

[34. **Signal Encoding and Decoding**](docs/34_Signal_Encoding_and_Decoding.md)<br>
Packing and unpacking signals with different byte orders, scaling factors, and offsets.

[35. **Network Management (NM)**](docs/35_Network_Management_NM.md)<br>
Implementing sleep/wake mechanisms and node monitoring for power management.

## Real-Time Considerations

[36. **RTOS Integration**](docs/36_RTOS_Integration.md)<br>
Integrating CAN drivers with real-time operating systems like FreeRTOS, Zephyr, or embedded Linux.

[37. **Priority Inversion Prevention**](docs/37_Priority_Inversion_Prevention.md)<br>
Strategies to avoid priority inversion issues in multi-threaded CAN applications.

[38. **Deadline Monitoring**](docs/38_Deadline_Monitoring.md)<br>
Implementing watchdogs and timeout mechanisms to detect missed message deadlines.

[39. **Jitter Analysis and Reduction**](docs/39_Jitter_Analysis_and_Reduction.md)<br>
Measuring and minimizing transmission jitter for time-critical applications.

[40. **Bus Guardians**](docs/40_Bus_Guardians.md)<br>
Hardware and software mechanisms to prevent babbling nodes from monopolizing the bus.

## Bootloader and Firmware

[41. **CAN Bootloader Design**](docs/41_CAN_Bootloader_Design.md)<br>
Implementing over-the-air firmware updates via CAN using bootloader protocols.

[42. **Flash Programming over CAN**](docs/42_Flash_Programming_over_CAN.md)<br>
Block transfer protocols and memory programming sequences for ECU reprogramming.

[43. **Bootloader Security**](docs/43_Bootloader_Security.md)<br>
Secure boot, signature verification, and rollback protection in CAN-based bootloaders.

[44. **Multi-ECU Update Orchestration**](docs/44_Multi_ECU_Update_Orchestration.md)<br>
Coordinating firmware updates across multiple nodes while maintaining system availability.

[45. **Error Recovery in Bootloader**](docs/45_Error_Recovery_in_Bootloader.md)<br>
Handling interrupted updates, checksum failures, and fallback mechanisms.

## Performance and Optimization

[46. **Message Batching**](docs/46_Message_Batching.md)<br>
Combining multiple signals into fewer CAN frames to optimize bus utilization.

[47. **Cyclic vs Sporadic Messages**](docs/47_Cyclic_vs_Sporadic_Messages.md)<br>
Designing communication patterns based on data volatility and latency requirements.

[48. **Bus-Off Recovery Strategies**](docs/48_Bus_Off_Recovery_Strategies.md)<br>
Automatic recovery algorithms and backoff strategies after entering bus-off state.

[49. **Compiler Optimizations for CAN Code**](docs/49_Compiler_Optimizations_for_CAN_Code.md)<br>
Leveraging compiler features and inline assembly for performance-critical CAN paths.

[50. **Profiling CAN Stack Performance**](docs/50_Profiling_CAN_Stack_Performance.md)<br>
Tools and techniques to measure latency, throughput, and CPU utilization in CAN implementations.

## Compliance and Standards

[51. **AUTOSAR CAN Stack Architecture**](docs/51_AUTOSAR_CAN_Stack_Architecture.md)<br>
Understanding the layered AUTOSAR communication stack: CAN driver, CAN interface, PDU router, and COM module.

[52. **ISO 11898 Compliance Testing**](docs/52_ISO_11898_Compliance_Testing.md)<br>
Validation procedures and test cases for ensuring CAN controller conformance to international standards.

[53. **MISRA C Guidelines for CAN Code**](docs/53_MISRA_C_Guidelines_for_CAN_Code.md)<br>
Applying MISRA-C coding standards to CAN driver and stack implementation for safety-critical systems.

[54. **Functional Safety (ISO 26262)**](docs/54_Functional_Safety_ISO_26262.md)<br>
Designing CAN communication systems to meet ASIL requirements including safety mechanisms and fault metrics.

[55. **EMC Considerations**](docs/55_EMC_Considerations.md)<br>
Electromagnetic compatibility design practices for CAN physical layer to meet automotive EMC standards.

## Advanced Protocols

[56. **CAN Kingdom Protocol**](docs/56_CAN_Kingdom_Protocol.md)<br>
Plug-and-play CAN protocol with dynamic address allocation and capability exchange for modular systems.

[57. **DeviceNet Protocol**](docs/57_DeviceNet_Protocol.md)<br>
Industrial CAN-based protocol for factory automation with predefined connection sets and explicit messaging.

[58. **CANaerospace Standard**](docs/58_CANaerospace_Standard.md)<br>
Lightweight protocol for avionics and aerospace applications with standardized message identifiers.

[59. **NMEA 2000 Marine Protocol**](docs/59_NMEA_2000_Marine_Protocol.md)<br>
Maritime CAN network standard for vessel instrumentation and sensor integration.

[60. **SAE J2284 (CAN for Diagnostics)**](docs/60_SAE_J2284_CAN_for_Diagnostics.md)<br>
Enhanced diagnostic communication protocol building on ISO 15765 for automotive applications.

## Embedded Systems Integration

[61. **Power Management and Sleep Modes**](docs/61_Power_Management_and_Sleep_Modes.md)<br>
Implementing selective wake-up, partial networking, and low-power CAN transceiver modes for energy efficiency.

[62. **Watchdog Integration**](docs/62_Watchdog_Integration.md)<br>
Coordinating hardware and software watchdogs with CAN communication health monitoring.

[63. **Multi-Core CAN Processing**](docs/63_Multi_Core_CAN_Processing.md)<br>
Distributing CAN processing across multiple CPU cores with lock-free queues and synchronization strategies.

[64. **Memory-Constrained Implementations**](docs/64_Memory_Constrained_Implementations.md)<br>
Optimizing CAN stack footprint for microcontrollers with limited RAM and flash resources.

[65. **CAN over Ethernet Tunneling**](docs/65_CAN_over_Ethernet_Tunneling.md)<br>
Encapsulating CAN frames in Ethernet packets for remote access and distributed development.

## Testing and Validation

[66. **HIL (Hardware-in-the-Loop) Testing**](docs/66_HIL_Hardware_in_the_Loop_Testing.md)<br>
Setting up hardware-in-the-loop test benches for automated CAN ECU validation and regression testing.

[67. **Simulation and Virtual Networks**](docs/67_Simulation_and_Virtual_Networks.md)<br>
Using CANoe, Busmaster, and virtual CAN interfaces for protocol development without physical hardware.

[68. **Fault Injection Testing**](docs/68_Fault_Injection_Testing.md)<br>
Systematic introduction of errors (short circuits, open circuits, babbling nodes) to validate robustness.

[69. **Coverage Analysis**](docs/69_Coverage_Analysis.md)<br>
Measuring code coverage and scenario coverage for CAN driver and protocol stack validation.

[70. **Automated Test Case Generation**](docs/70_Automated_Test_Case_Generation.md)<br>
Using model-based testing and property-based testing to generate comprehensive CAN test suites.

## Advanced Diagnostics

[71. **XCP Protocol over CAN**](docs/71_XCP_Protocol_over_CAN.md)<br>
Universal measurement and calibration protocol for ECU parameter tuning and data acquisition.

[72. **Logging and Trace Systems**](docs/72_Logging_and_Trace_Systems.md)<br>
Implementing efficient circular logging buffers and triggered trace capture for post-mortem analysis.

[73. **Remote Diagnostics and Telematics**](docs/73_Remote_Diagnostics_and_Telematics.md)<br>
Bridging CAN networks to cloud services for fleet monitoring and predictive maintenance.

[74. **Diagnostic Data Flash**](docs/74_Diagnostic_Data_Flash.md)<br>
Managing freeze frame data, environmental data, and extended diagnostic records in non-volatile memory.

[75. **CAN Health Monitoring**](docs/75_CAN_Health_Monitoring.md)<br>
Tracking error counters, bus-off events, and signal quality metrics to predict and prevent failures.

## Industrial Applications

[76. **CAN in Robotics**](docs/76_CAN_in_Robotics.md)<br>
Using CAN for real-time motor control, sensor fusion, and coordinated multi-axis robot communication.

[77. **CAN in Medical Devices**](docs/77_CAN_in_Medical_Devices.md)<br>
Applying IEC 62304 software lifecycle and ISO 13485 quality management to medical CAN systems.

[78. **CAN in Renewable Energy**](docs/78_CAN_in_Renewable_Energy.md)<br>
Monitoring and control of solar inverters, wind turbines, and battery management systems via CAN.

[79. **CAN in Building Automation**](docs/79_CAN_in_Building_Automation.md)<br>
Integration with HVAC, lighting, and access control systems using CAN-based protocols.

[80. **CAN in Railway Systems**](docs/80_CAN_in_Railway_Systems.md)<br>
Train control networks, passenger information systems, and safety-critical rail communication.

## Emerging Technologies

[81. **Machine Learning on CAN Data**](docs/81_Machine_Learning_on_CAN_Data.md)<br>
Applying anomaly detection, predictive maintenance, and pattern recognition to CAN traffic analysis.

[82. **CAN for Autonomous Vehicles**](docs/82_CAN_for_Autonomous_Vehicles.md)<br>
Integrating CAN with Ethernet and sensor fusion for ADAS and self-driving systems.

[83. **Time-Sensitive Networking (TSN)**](docs/83_Time_Sensitive_Networking_TSN.md)<br>
Bridging CAN to IEEE 802.1 TSN networks for deterministic real-time communication.

[84. **CAN in Electric Vehicle Architecture**](docs/84_CAN_in_Electric_Vehicle_Architecture.md)<br>
High-voltage battery management, motor control, and charging communication over CAN.

[85. **Quantum-Safe CAN Security**](docs/85_Quantum_Safe_CAN_Security.md)<br>
Preparing CAN security architectures for post-quantum cryptographic algorithms and key management.

## Documentation and Maintenance

[86. **CAN Network Documentation**](docs/86_CAN_Network_Documentation.md)<br>
Best practices for maintaining network topology diagrams, signal lists, and configuration management.

[87. **Version Control for DBC Files**](docs/87_Version_Control_for_DBC_Files.md)<br>
Managing database file evolution, compatibility matrices, and change tracking in multi-project environments.

[88. **Code Generation from CAN Databases**](docs/88_Code_Generation_from_CAN_Databases.md)<br>
Automated generation of packing/unpacking functions, type definitions, and documentation from DBC files.

[89. **Legacy System Integration**](docs/89_Legacy_System_Integration.md)<br>
Bridging older CAN implementations with modern systems using protocol translators and adapters.

[90. **Migration Strategies (CAN to CAN FD)**](docs/90_Migration_Strategies_CAN_to_CAN_FD.md)<br>
Planning and executing network upgrades while maintaining backward compatibility and system availability.

## Specialized Topics

[91. **Redundant CAN Networks**](docs/91_Redundant_CAN_Networks.md)<br>
Designing dual-CAN architectures for fault tolerance in safety-critical applications.

[92. **CAN Bus Arbitration Timing Analysis**](docs/92_CAN_Bus_Arbitration_Timing_Analysis.md)<br>
Mathematical modeling of worst-case message latency and response time analysis.

[93. **Thermal Management for CAN Systems**](docs/93_Thermal_Management_for_CAN_Systems.md)<br>
Designing for extreme temperature operation and thermal shutdown protection in transceivers.

[94. **CAN Bus Length and Stub Calculations**](docs/94_CAN_Bus_Length_and_Stub_Calculations.md)<br>
Engineering guidelines for maximum bus length, stub length, and node count based on bit rate.

[95. **Common Mode Choke Selection**](docs/95_Common_Mode_Choke_Selection.md)<br>
Choosing appropriate common mode filters to reduce EMI and improve signal integrity.

[96. **Oscilloscope Analysis of CAN Signals**](docs/96_Oscilloscope_Analysis_of_CAN_Signals.md)<br>
Interpreting eye diagrams, rise times, and voltage levels to diagnose physical layer problems.

[97. **CAN Bus Repair and Maintenance**](docs/97_CAN_Bus_Repair_and_Maintenance.md)<br>
Troubleshooting techniques for identifying broken wires, failed transceivers, and intermittent faults.

[98. **Cost Optimization in CAN Design**](docs/98_Cost_Optimization_in_CAN_Design.md)<br>
Balancing component selection, bandwidth requirements, and system partitioning for cost-effective designs.

[99. **CAN Penetration Testing**](docs/99_CAN_Penetration_Testing.md)<br>
Ethical hacking methodologies for assessing CAN network security and identifying vulnerabilities.

[100. **Future of CAN Technology**](docs/100_Future_of_CAN_Technology.md)<br>
Exploring CAN XL adoption, integration with Ethernet backbones, and evolution in next-generation vehicles.


---


# CANopen — Knowledge Map

## CAN Bus Foundation

[01. **CAN Bus Fundamentals**](docs_canopen/01_CAN_Bus_Fundamentals.md)<br>
Frame structure (data/remote/error/overload), arbitration, bit stuffing, ACK mechanism, CRC,
dominant/recessive levels, differential signalling on CAN-H/CAN-L, and the relationship
between bit rate and bus length.

[02. **Bit Timing & Baud Rate Configuration**](docs_canopen/02_Bit_Timing_and_Baud_Rate_Configuration.md)<br>
Time quanta, synchronisation segment, propagation segment, phase buffer segments, SJW,
sample point tuning, BTR register programming, and oscillator tolerance requirements.

[03. **CAN Error Handling & Bus-Off Recovery**](docs_canopen/03_CAN_Error_Handling_and_Bus_Off_Recovery.md)<br>
TEC/REC counters, error-active/passive/bus-off state machine, error frame types,
automatic retransmission, bus-off recovery sequences, and hardware driver integration.


## CANopen Core Architecture

[04. **CANopen Stack Architecture & Layer Model**](docs_canopen/04_CANopen_Stack_Architecture_and_Layer_Model.md)<br>
CiA 301 overview, OSI mapping, the relationship between CAN driver / HAL / CANopen stack /
application layer, stack initialisation flow, and a survey of popular open-source stacks
(CANopenNode, lely-core, CANfestival).

[05. **NMT State Machine & Network Management**](docs_canopen/05_NMT_State_Machine_and_Network_Management.md)<br>
Initialisation → Pre-Operational → Operational → Stopped transitions, NMT master commands
(Start/Stop/Reset Node/Reset Comm), boot-up message (0x700 + NodeID), and NMT slave
implementation in C.

[06. **COB-ID Scheme & Predefined Connection Set**](docs_canopen/06_COB_ID_Scheme_and_Predefined_Connection_Set.md)<br>
11-bit COB-ID composition (function code + NodeID), the predefined connection set table,
COB-ID conflict detection, dynamic vs. static assignment, and CAN filter configuration
for multi-node networks.


## Object Dictionary

[07. **Object Dictionary (OD) Structure & Access**](docs_canopen/07_Object_Dictionary_Structure_and_Access.md)<br>
Index/sub-index addressing space (0x0000–0xFFFF), object types (VAR, ARRAY, RECORD),
data types, access attributes (RO/WO/RW/RWW/RWR), the communication profile area
(0x1000–0x1FFF), manufacturer-specific area (0x2000–0x5FFF), device profile area
(0x6000–0x9FFF), and OD lookup strategies.

[08. **EDS & DCF Device Description Files**](docs_canopen/08_EDS_and_DCF_Device_Description_Files.md)<br>
EDS INI-file syntax, mandatory/optional sections, data type encoding, generating EDS from
code, parsing EDS at runtime, DCF differences (node-ID-parametrised), and toolchain
integration (CANopen Magic, Peak PCAN-Explorer, EDS Editor).



## Communication Objects

[09. **SDO — Service Data Object (Expedited & Segmented)**](docs_canopen/09_SDO_Service_Data_Object_Expedited_and_Segmented.md)<br>
Client/server roles, multiplexer (index + sub-index), expedited download/upload,
segmented transfer with toggle bit, abort codes (0x05040000 series), SDO timeout handling,
and a complete C implementation of an SDO server state machine.

[10. **SDO Block Transfer**](docs_canopen/10_SDO_Block_Transfer.md)<br>
Block download/upload protocol, sequence numbers, CRC verification, block size negotiation,
use cases (firmware update, large parameter sets), and comparison with segmented transfer
throughput.

[11. **PDO — Process Data Object (TPDO & RPDO)**](docs_canopen/11_PDO_Process_Data_Object_TPDO_and_RPDO.md)<br>
Producer/consumer model, PDO communication parameters (0x1400/0x1800), PDO mapping
parameters (0x1600/0x1A00), transmission types (0/1–240/254/255), inhibit time, event
timer, and RPDO timeout monitoring.

[12. **PDO Mapping & Dynamic Reconfiguration**](docs_canopen/12_PDO_Mapping_and_Dynamic_Reconfiguration.md)<br>
Static vs. dynamic mapping, the mapping procedure (disable PDO → clear map count →
write entries → set count → enable), mixed-type packing, dummy mapping entries,
and runtime remapping via SDO in C.

[13. **SYNC Object & Synchronous Communication**](docs_canopen/13_SYNC_Object_and_Synchronous_Communication.md)<br>
SYNC COB-ID (0x80), SYNC counter (CiA 301 v4.2+), synchronous window length,
SYNC producer implementation, synchronised PDO transmission timing, and multi-axis
coordinated motion with SYNC.

[14. **TIME Object & Distributed Timestamps**](docs_canopen/14_TIME_Object_and_Distributed_Timestamps.md)<br>
TIME COB-ID (0x100), 48-bit timestamp encoding (ms since epoch + days since 1 Jan 1984),
TIME producer/consumer implementation, accuracy considerations, and correlation with
external RTC or PTP/IEEE-1588.

[15. **EMCY — Emergency Object & Error Codes**](docs_canopen/15_EMCY_Emergency_Object_and_Error_Codes.md)<br>
EMCY COB-ID (0x80 + NodeID), 8-byte frame layout (error code + error register + vendor
data), standard error code classes (0x1000–0xFF00), pre-defined error field (0x1003),
inhibit time, and EMCY consumer filtering.



## Network Health & Supervision

[16. **Heartbeat Protocol**](docs_canopen/16_Heartbeat_Protocol.md)<br>
Producer heartbeat time (0x1017), consumer heartbeat table (0x1016), heartbeat state
machine (boot-up / pre-op / op / stopped), monitoring multiple nodes, and timeout
recovery strategy.

[17. **Node Guarding & Life Guarding (Legacy)**](docs_canopen/17_Node_Guarding_and_Life_Guarding.md)<br>
Guard time + life-time factor, RTR-based polling, toggle bit, remote frame limitations
on CAN FD networks, and migration path to heartbeat protocol.



## Network Configuration

[18. **LSS — Layer Setting Services**](docs_canopen/18_LSS_Layer_Setting_Services.md)<br>
LSS master/slave roles, LSS address (vendor-ID/product/revision/serial), global vs.
selective addressing, Fastscan algorithm for automatic NodeID assignment,
baud rate setting, store configuration command, and C implementation sketch.

[19. **Boot-Up Sequence & Network Initialisation**](docs_canopen/19_Boot_Up_Sequence_and_Network_Initialisation.md)<br>
Master boot sequence, slave auto-start vs. NMT-commanded start, configuration manager
pattern, checking mandatory objects, configuration via SDO before NMT Start, and
robustness against missing nodes.



## Device Profiles

[20. **CiA 301 — CANopen Application Layer & Communication Profile**](docs_canopen/20_CiA_301_Application_Layer_and_Communication_Profile.md)<br>
Mandatory/optional objects, device identity object (0x1018), error behaviour object,
guard/heartbeat interplay, and full conformance checklist.

[21. **CiA 401 — Generic I/O Device Profile**](docs_canopen/21_CiA_401_Generic_IO_Device_Profile.md)<br>
Digital/analogue input & output objects, polarity inversion, filter time constants,
interrupt on change, scaling, and a complete 16-DI / 8-DO slave example.

[22. **CiA 402 — Motion Control & Drives Profile**](docs_canopen/22_CiA_402_Motion_Control_and_Drives_Profile.md)<br>
CIA 402 state machine (Not Ready / Switch-On Disabled / … / Operation Enabled / Fault),
control word (0x6040) / status word (0x6041), supported drive modes (PP/PV/PT/HM/IP/CSP/CSV/CST),
homing methods, and synchronous cyclic position mode implementation.

[23. **CiA 406 — Encoder Device Profile**](docs_canopen/23_CiA_406_Encoder_Device_Profile.md)<br>
Position value object, encoder type (singleturn/multiturn), preset, scaling function,
working area limits, alarm objects, and master polling vs. event-driven PDO strategies.

[24. **CiA 447 / CiA 454 — Additional Device Profiles Overview**](docs_canopen/24_Additional_Device_Profiles_Overview.md)<br>
Survey of further standardised profiles: CiA 410 (inclinometers), CiA 412 (medical devices),
CiA 418/419 (battery/charger), CiA 447 (J1939 gateway), CiA 454 (energy management),
with guidance on choosing and extending profiles.



## Master Implementation

[25. **CANopen Master Architecture**](docs_canopen/25_CANopen_Master_Architecture.md)<br>
Responsibilities of a master node, SDO manager, PDO scheduler, NMT supervisor,
heartbeat monitor, configuration manager pattern, task/thread decomposition on RTOS,
and re-entrant OD access.

[26. **Configuration Manager & Automatic Node Configuration**](docs_canopen/26_Configuration_Manager_and_Automatic_Node_Configuration.md)<br>
DCF-based configuration download, verify-before-write strategy, version checking via
identity object, incremental configuration, and error recovery during network startup.



## Slave Implementation

[27. **CANopen Slave Node Implementation**](docs_canopen/27_CANopen_Slave_Node_Implementation.md)<br>
Minimal slave object set, OD storage in Flash/EEPROM, store/restore parameters
(0x1010/0x1011), write-protect mechanisms, application callback hooks, watchdog
integration, and a bare-metal C reference implementation.

[28. **OD Storage — Persistent Parameters in Flash/EEPROM**](docs_canopen/28_OD_Storage_Persistent_Parameters_in_Flash_EEPROM.md)<br>
Store command signature (0x65766173), restore factory defaults, wear-levelling strategies
for Flash, CRC validation of stored data, and migration between firmware versions.



## Diagnostics & Testing

[29. **CANopen Error Handling Strategy**](docs_canopen/29_CANopen_Error_Handling_Strategy.md)<br>
Error register (0x1001) bit definitions, pre-defined error field (0x1003) FIFO,
EMCY generation policy, communication error counters, and designing a recoverable
fault architecture.

[30. **CANopen Network Diagnostics & Bus Analysis**](docs_canopen/30_CANopen_Network_Diagnostics_and_Bus_Analysis.md)<br>
Using CAN analysers (Peak PCAN-View, Kvaser CanKing, Vector CANalyzer), decoding
CANopen frames, SDO/PDO traffic analysis, heartbeat timeline, EMCY monitoring,
and scripted automated conformance checks.

[31. **Conformance & Interoperability Testing**](docs_canopen/31_Conformance_and_Interoperability_Testing.md)<br>
CiA conformance test plan, mandatory/optional object verification, SDO/PDO timing tests,
NMT transition stress tests, vendor interoperability workshops, and preparing a
Certificate of Conformance.



## Advanced Topics

[32. **CANopen FD (CiA 1301)**](docs_canopen/32_CANopen_FD.md)<br>
CAN FD frame format, bit-rate switching (nominal vs. data phase), CANopen FD frame
encoding differences, USDO (replacing SDO), LPDO (replacing PDO), migration strategy
from classic CANopen, and hardware/driver requirements.

[33. **CANopen Safety (EN 50325-5 / CiA 304)**](docs_canopen/33_CANopen_Safety.md)<br>
Safety architecture concepts, black-channel principle, SPDO (Safety PDO), watchdog
relationships, SIL 2 / PLd compliance, frame integrity measures (CRC, sequence number,
connection ID), and integration with IEC 61508 / ISO 13849 assessments.

[34. **CANopen over Serial / CANopen Tunnelling**](docs_canopen/34_CANopen_Tunnelling_and_Gateways.md)<br>
CAN-to-Ethernet gateways (CiA 309), REST/JSON and binary tunnel protocols,
remote SDO access patterns, latency impact on real-time PDOs, and secure remote
commissioning architectures.

[35. **Firmware Update over CANopen (LSS / SDO Block)**](docs_canopen/35_Firmware_Update_over_CANopen.md)<br>
Bootloader design, program download via SDO block transfer, flash-verify-execute flow,
dual-bank and golden-image strategies, transfer integrity (CRC32), and production
programming throughput calculations.
