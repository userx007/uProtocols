# CANopen — Senior Engineer Knowledge Map

## 🔵 CAN Bus Foundation

[01. **CAN Bus Fundamentals**](docs/01_CAN_Bus_Fundamentals.md)<br>
Frame structure (data/remote/error/overload), arbitration, bit stuffing, ACK mechanism, CRC,
dominant/recessive levels, differential signalling on CAN-H/CAN-L, and the relationship
between bit rate and bus length.

[02. **Bit Timing & Baud Rate Configuration**](docs/02_Bit_Timing_and_Baud_Rate_Configuration.md)<br>
Time quanta, synchronisation segment, propagation segment, phase buffer segments, SJW,
sample point tuning, BTR register programming, and oscillator tolerance requirements.

[03. **CAN Error Handling & Bus-Off Recovery**](docs/03_CAN_Error_Handling_and_Bus_Off_Recovery.md)<br>
TEC/REC counters, error-active/passive/bus-off state machine, error frame types,
automatic retransmission, bus-off recovery sequences, and hardware driver integration.

---

## 🔵 CANopen Core Architecture

[04. **CANopen Stack Architecture & Layer Model**](docs/04_CANopen_Stack_Architecture_and_Layer_Model.md)<br>
CiA 301 overview, OSI mapping, the relationship between CAN driver / HAL / CANopen stack /
application layer, stack initialisation flow, and a survey of popular open-source stacks
(CANopenNode, lely-core, CANfestival).

[05. **NMT State Machine & Network Management**](docs/05_NMT_State_Machine_and_Network_Management.md)<br>
Initialisation → Pre-Operational → Operational → Stopped transitions, NMT master commands
(Start/Stop/Reset Node/Reset Comm), boot-up message (0x700 + NodeID), and NMT slave
implementation in C.

[06. **COB-ID Scheme & Predefined Connection Set**](docs/06_COB_ID_Scheme_and_Predefined_Connection_Set.md)<br>
11-bit COB-ID composition (function code + NodeID), the predefined connection set table,
COB-ID conflict detection, dynamic vs. static assignment, and CAN filter configuration
for multi-node networks.

---

## 🔵 Object Dictionary

[07. **Object Dictionary (OD) Structure & Access**](docs/07_Object_Dictionary_Structure_and_Access.md)<br>
Index/sub-index addressing space (0x0000–0xFFFF), object types (VAR, ARRAY, RECORD),
data types, access attributes (RO/WO/RW/RWW/RWR), the communication profile area
(0x1000–0x1FFF), manufacturer-specific area (0x2000–0x5FFF), device profile area
(0x6000–0x9FFF), and OD lookup strategies.

[08. **EDS & DCF Device Description Files**](docs/08_EDS_and_DCF_Device_Description_Files.md)<br>
EDS INI-file syntax, mandatory/optional sections, data type encoding, generating EDS from
code, parsing EDS at runtime, DCF differences (node-ID-parametrised), and toolchain
integration (CANopen Magic, Peak PCAN-Explorer, EDS Editor).

---

## 🔵 Communication Objects

[09. **SDO — Service Data Object (Expedited & Segmented)**](docs/09_SDO_Service_Data_Object_Expedited_and_Segmented.md)<br>
Client/server roles, multiplexer (index + sub-index), expedited download/upload,
segmented transfer with toggle bit, abort codes (0x05040000 series), SDO timeout handling,
and a complete C implementation of an SDO server state machine.

[10. **SDO Block Transfer**](docs/10_SDO_Block_Transfer.md)<br>
Block download/upload protocol, sequence numbers, CRC verification, block size negotiation,
use cases (firmware update, large parameter sets), and comparison with segmented transfer
throughput.

[11. **PDO — Process Data Object (TPDO & RPDO)**](docs/11_PDO_Process_Data_Object_TPDO_and_RPDO.md)<br>
Producer/consumer model, PDO communication parameters (0x1400/0x1800), PDO mapping
parameters (0x1600/0x1A00), transmission types (0/1–240/254/255), inhibit time, event
timer, and RPDO timeout monitoring.

[12. **PDO Mapping & Dynamic Reconfiguration**](docs/12_PDO_Mapping_and_Dynamic_Reconfiguration.md)<br>
Static vs. dynamic mapping, the mapping procedure (disable PDO → clear map count →
write entries → set count → enable), mixed-type packing, dummy mapping entries,
and runtime remapping via SDO in C.

[13. **SYNC Object & Synchronous Communication**](docs/13_SYNC_Object_and_Synchronous_Communication.md)<br>
SYNC COB-ID (0x80), SYNC counter (CiA 301 v4.2+), synchronous window length,
SYNC producer implementation, synchronised PDO transmission timing, and multi-axis
coordinated motion with SYNC.

[14. **TIME Object & Distributed Timestamps**](docs/14_TIME_Object_and_Distributed_Timestamps.md)<br>
TIME COB-ID (0x100), 48-bit timestamp encoding (ms since epoch + days since 1 Jan 1984),
TIME producer/consumer implementation, accuracy considerations, and correlation with
external RTC or PTP/IEEE-1588.

[15. **EMCY — Emergency Object & Error Codes**](docs/15_EMCY_Emergency_Object_and_Error_Codes.md)<br>
EMCY COB-ID (0x80 + NodeID), 8-byte frame layout (error code + error register + vendor
data), standard error code classes (0x1000–0xFF00), pre-defined error field (0x1003),
inhibit time, and EMCY consumer filtering.

---

## 🔵 Network Health & Supervision

[16. **Heartbeat Protocol**](docs/16_Heartbeat_Protocol.md)<br>
Producer heartbeat time (0x1017), consumer heartbeat table (0x1016), heartbeat state
machine (boot-up / pre-op / op / stopped), monitoring multiple nodes, and timeout
recovery strategy.

[17. **Node Guarding & Life Guarding (Legacy)**](docs/17_Node_Guarding_and_Life_Guarding.md)<br>
Guard time + life-time factor, RTR-based polling, toggle bit, remote frame limitations
on CAN FD networks, and migration path to heartbeat protocol.

---

## 🔵 Network Configuration

[18. **LSS — Layer Setting Services**](docs/18_LSS_Layer_Setting_Services.md)<br>
LSS master/slave roles, LSS address (vendor-ID/product/revision/serial), global vs.
selective addressing, Fastscan algorithm for automatic NodeID assignment,
baud rate setting, store configuration command, and C implementation sketch.

[19. **Boot-Up Sequence & Network Initialisation**](docs/19_Boot_Up_Sequence_and_Network_Initialisation.md)<br>
Master boot sequence, slave auto-start vs. NMT-commanded start, configuration manager
pattern, checking mandatory objects, configuration via SDO before NMT Start, and
robustness against missing nodes.

---

## 🔵 Device Profiles

[20. **CiA 301 — CANopen Application Layer & Communication Profile**](docs/20_CiA_301_Application_Layer_and_Communication_Profile.md)<br>
Mandatory/optional objects, device identity object (0x1018), error behaviour object,
guard/heartbeat interplay, and full conformance checklist.

[21. **CiA 401 — Generic I/O Device Profile**](docs/21_CiA_401_Generic_IO_Device_Profile.md)<br>
Digital/analogue input & output objects, polarity inversion, filter time constants,
interrupt on change, scaling, and a complete 16-DI / 8-DO slave example.

[22. **CiA 402 — Motion Control & Drives Profile**](docs/22_CiA_402_Motion_Control_and_Drives_Profile.md)<br>
CIA 402 state machine (Not Ready / Switch-On Disabled / … / Operation Enabled / Fault),
control word (0x6040) / status word (0x6041), supported drive modes (PP/PV/PT/HM/IP/CSP/CSV/CST),
homing methods, and synchronous cyclic position mode implementation.

[23. **CiA 406 — Encoder Device Profile**](docs/23_CiA_406_Encoder_Device_Profile.md)<br>
Position value object, encoder type (singleturn/multiturn), preset, scaling function,
working area limits, alarm objects, and master polling vs. event-driven PDO strategies.

[24. **CiA 447 / CiA 454 — Additional Device Profiles Overview**](docs/24_Additional_Device_Profiles_Overview.md)<br>
Survey of further standardised profiles: CiA 410 (inclinometers), CiA 412 (medical devices),
CiA 418/419 (battery/charger), CiA 447 (J1939 gateway), CiA 454 (energy management),
with guidance on choosing and extending profiles.

---

## 🔵 Master Implementation

[25. **CANopen Master Architecture**](docs/25_CANopen_Master_Architecture.md)<br>
Responsibilities of a master node, SDO manager, PDO scheduler, NMT supervisor,
heartbeat monitor, configuration manager pattern, task/thread decomposition on RTOS,
and re-entrant OD access.

[26. **Configuration Manager & Automatic Node Configuration**](docs/26_Configuration_Manager_and_Automatic_Node_Configuration.md)<br>
DCF-based configuration download, verify-before-write strategy, version checking via
identity object, incremental configuration, and error recovery during network startup.

---

## 🔵 Slave Implementation

[27. **CANopen Slave Node Implementation**](docs/27_CANopen_Slave_Node_Implementation.md)<br>
Minimal slave object set, OD storage in Flash/EEPROM, store/restore parameters
(0x1010/0x1011), write-protect mechanisms, application callback hooks, watchdog
integration, and a bare-metal C reference implementation.

[28. **OD Storage — Persistent Parameters in Flash/EEPROM**](docs/28_OD_Storage_Persistent_Parameters_in_Flash_EEPROM.md)<br>
Store command signature (0x65766173), restore factory defaults, wear-levelling strategies
for Flash, CRC validation of stored data, and migration between firmware versions.

---

## 🔵 Diagnostics & Testing

[29. **CANopen Error Handling Strategy**](docs/29_CANopen_Error_Handling_Strategy.md)<br>
Error register (0x1001) bit definitions, pre-defined error field (0x1003) FIFO,
EMCY generation policy, communication error counters, and designing a recoverable
fault architecture.

[30. **CANopen Network Diagnostics & Bus Analysis**](docs/30_CANopen_Network_Diagnostics_and_Bus_Analysis.md)<br>
Using CAN analysers (Peak PCAN-View, Kvaser CanKing, Vector CANalyzer), decoding
CANopen frames, SDO/PDO traffic analysis, heartbeat timeline, EMCY monitoring,
and scripted automated conformance checks.

[31. **Conformance & Interoperability Testing**](docs/31_Conformance_and_Interoperability_Testing.md)<br>
CiA conformance test plan, mandatory/optional object verification, SDO/PDO timing tests,
NMT transition stress tests, vendor interoperability workshops, and preparing a
Certificate of Conformance.

---

## 🔵 Advanced Topics

[32. **CANopen FD (CiA 1301)**](docs/32_CANopen_FD.md)<br>
CAN FD frame format, bit-rate switching (nominal vs. data phase), CANopen FD frame
encoding differences, USDO (replacing SDO), LPDO (replacing PDO), migration strategy
from classic CANopen, and hardware/driver requirements.

[33. **CANopen Safety (EN 50325-5 / CiA 304)**](docs/33_CANopen_Safety.md)<br>
Safety architecture concepts, black-channel principle, SPDO (Safety PDO), watchdog
relationships, SIL 2 / PLd compliance, frame integrity measures (CRC, sequence number,
connection ID), and integration with IEC 61508 / ISO 13849 assessments.

[34. **CANopen over Serial / CANopen Tunnelling**](docs/34_CANopen_Tunnelling_and_Gateways.md)<br>
CAN-to-Ethernet gateways (CiA 309), REST/JSON and binary tunnel protocols,
remote SDO access patterns, latency impact on real-time PDOs, and secure remote
commissioning architectures.

[35. **Firmware Update over CANopen (LSS / SDO Block)**](docs/35_Firmware_Update_over_CANopen.md)<br>
Bootloader design, program download via SDO block transfer, flash-verify-execute flow,
dual-bank and golden-image strategies, transfer integrity (CRC32), and production
programming throughput calculations.
