# CAN Bus Architecture and Physical Layer

## Overview

The Controller Area Network (CAN) bus is a robust vehicle bus standard designed to allow microcontrollers and devices to communicate with each other without a host computer. Understanding its physical layer and architecture is fundamental to working with automotive and industrial communication systems.

## Physical Layer Fundamentals

### Differential Signaling

CAN uses **differential signaling** as its core transmission method, which provides exceptional noise immunity. The system uses two wires:

**CAN_H (CAN High)** - Carries the high signal
**CAN_L (CAN Low)** - Carries the low signal

The differential voltage between these two lines determines the bus state:

**Dominant bit (logical 0)**: CAN_H is approximately 3.5V and CAN_L is approximately 1.5V, creating a differential voltage of about 2V. This state "dominates" because any node can force the bus to this state.

**Recessive bit (logical 1)**: Both CAN_H and CAN_L sit at approximately 2.5V, resulting in a differential voltage near 0V. This is the default idle state of the bus.

The differential approach provides several critical advantages. Common-mode noise (interference affecting both wires equally) gets cancelled out when the receiver measures the voltage difference. The system can tolerate significant ground potential differences between nodes. Even if one wire is damaged, communication may continue with reduced noise immunity.

### Bus Topology

CAN networks use a **linear bus topology** with specific structural requirements:

The main bus consists of a twisted pair of wires (CAN_H and CAN_L) running the length of the network. Nodes connect to this main bus via short stub connections, ideally less than 30cm (12 inches) to minimize signal reflections. The bus must have exactly two endpoints where termination resistors are placed.

Star, ring, or complex branching topologies are avoided because they create multiple signal reflection points that corrupt data transmission. The linear topology ensures signal integrity by providing a single, well-defined transmission path.

### Termination Resistors

**Termination is critical** for preventing signal reflections that would distort the waveform. The standard configuration includes:

A 120Ω resistor at each end of the bus (two total), creating a total bus impedance of 60Ω when measured between CAN_H and CAN_L. These resistors absorb electromagnetic energy at the bus endpoints, preventing it from reflecting back along the cable.

Without proper termination, reflected signals create standing waves, ringing, and distortion that cause bit errors. With correct termination, signals travel cleanly along the bus and are absorbed at the endpoints.

Some systems use split termination (two 60Ω resistors with a capacitor to ground at each end) to improve electromagnetic compatibility, or biasing resistors to ensure proper recessive voltage levels.

## Electrical Characteristics

### Voltage Levels

**High-speed CAN (ISO 11898-2)** operates with these voltage specifications:

During dominant state: CAN_H ranges from 2.75V to 4.5V (typically 3.5V), while CAN_L ranges from 0.5V to 2.25V (typically 1.5V). The differential voltage must be between 1.5V and 3.0V.

During recessive state: Both lines sit between 2.0V and 3.0V (typically 2.5V), with differential voltage between -0.5V and 0.05V (ideally 0V).

**Low-speed CAN (ISO 11898-3)** uses different voltage levels optimized for fault tolerance, with one wire capable of continuing communication if the other fails.

### Bus Impedance and Capacitance

The characteristic impedance of the CAN bus cable should be approximately **120Ω**. This matches the termination resistor values to eliminate reflections.

Maximum bus capacitance depends on the number of nodes and cable type but typically should not exceed 0.1µF per meter. Lower capacitance allows higher bit rates and longer cable runs.

### Speed vs. Distance Tradeoffs

CAN's maximum communication speed is inversely related to network length due to signal propagation delays:

- **1 Mbps**: Maximum length of 40 meters (131 feet)
- **500 kbps**: Maximum length of 100 meters (328 feet)
- **250 kbps**: Maximum length of 250 meters (820 feet)
- **125 kbps**: Maximum length of 500 meters (1,640 feet)
- **50 kbps**: Maximum length of 1,000 meters (3,280 feet)

These limits ensure that the signal propagation time doesn't exceed the bit time, which would prevent proper bit sampling and arbitration.

## Layer Architecture

### ISO OSI Model Mapping

CAN implements portions of the OSI seven-layer model:

**Physical Layer**: Defines electrical signaling, bit timing, and synchronization. This includes the transceiver chips that convert between the controller's digital signals and the bus differential voltages.

**Data Link Layer**: Split into two sublayers. The Logical Link Control (LLC) handles message filtering and overload notification. The Medium Access Control (MAC) manages frame formatting, arbitration, error detection, and fault confinement.

CAN doesn't implement the upper layers (Network, Transport, Session, Presentation, Application) in the base standard. Higher-layer protocols like CANopen, J1939, or DeviceNet build these functions on top of CAN.

### Transceiver Role

The **CAN transceiver** is the physical interface between the CAN controller and the bus. Common transceivers include the MCP2551, TJA1050, and SN65HVD230.

Transceivers perform critical functions: converting the controller's TX (transmit) signal into differential CAN_H/CAN_L voltages, converting differential bus voltages back to an RX (receive) signal for the controller, providing electrical isolation and protection against voltage spikes and electrostatic discharge, and implementing dominant/recessive bus states.

The transceiver's slew rate (how quickly voltage transitions occur) affects electromagnetic emissions. Controlled slew rates reduce EMI but may limit maximum bit rate.

## Cable Specifications

### Recommended Cable Types

Proper cabling is essential for reliable CAN communication:

**Twisted pair construction**: Twisting CAN_H and CAN_L together ensures both wires experience identical electromagnetic interference, which gets cancelled by differential signaling.

**Shielded cable**: A grounded shield provides additional protection against external electromagnetic interference, especially important in electrically noisy automotive and industrial environments.

**Characteristic impedance**: Cable should have 120Ω impedance, matching the termination resistors.

Common specifications include AWG 24-26 wire gauge, with typical options being DeviceNet cables (specifically designed for CAN), automotive-grade twisted pair with shield, or ISO 11898 compliant cables.

### Wire Color Coding

While not strictly standardized, common color conventions include CAN_H as yellow or white/green, CAN_L as green or white/brown, and shield/ground as bare or green/yellow. Always verify the specific color coding for your application as conventions vary by industry and region.

## Practical Considerations

### Node Count Limitations

A single CAN bus typically supports 30 to 110 nodes, depending on the transceiver type and bus loading. Each node adds capacitance to the bus, which affects signal integrity and maximum bit rate. High-speed networks may be limited to fewer nodes to maintain signal quality.

### Fault Tolerance Features

The physical layer includes several fault-tolerance mechanisms:

Dominant/recessive arbitration ensures that no data is lost even when multiple nodes transmit simultaneously. Differential signaling continues working even with significant electromagnetic interference. Some implementations (especially low-speed CAN) can operate with one wire disconnected. Transceivers typically include protection against short circuits, voltage spikes, and reverse polarity.

### Debugging Physical Layer Issues

Common physical layer problems include incorrect or missing termination (check for 60Ω between CAN_H and CAN_L with bus powered off), excessive stub lengths causing reflections, bus capacitance too high for the selected bit rate, damaged cables or connectors, and ground potential differences between nodes.

Oscilloscopes are invaluable for diagnosing physical layer issues by examining the actual CAN_H and CAN_L waveforms, differential voltage levels, rise/fall times, and signal quality.

---

Understanding the CAN bus physical layer and architecture provides the foundation for designing robust, reliable networks. Proper implementation of differential signaling, linear topology, correct termination, and appropriate cable selection ensures that CAN networks can operate reliably even in harsh electromagnetic environments.