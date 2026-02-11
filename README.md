# uProtocols

[**CAN Protocol**](CAN/README.md)<br>
The **CAN (Controller Area Network) protocol** is a robust serial communication standard designed for reliable real-time communication in distributed systems.
It was **developed by Robert Bosch GmbH in 1986**.
CAN allows multiple nodes to communicate over a shared bus and is widely used in automotive and industrial applications.
CAN is known for real-time performance, reliability, and strong error detection.

---

[**I2C Protocol**](I2C/README.md)<br>
The **I²C (Inter-Integrated Circuit) protocol** is a serial communication standard designed for short-distance, low-speed data exchange between integrated circuits.
It was **developed by Philips Semiconductor (now NXP) in 1982**.
I²C uses a two-wire master–slave bus (SDA and SCL) and is widely used for sensors and peripheral devices.

---

[**SPI Protocol**](SPI/README.md)<br>
The **SPI (Serial Peripheral Interface) protocol** is a high-speed serial communication standard used for short-distance device communication.
It was **developed by Motorola in the early 1980s (around 1983)**.
SPI uses a master–slave architecture and is valued for fast data transfer and simple hardware design.

---

[**UART Protocol**](UART/README.md)<br>
The **UART (Universal Asynchronous Receiver–Transmitter) protocol** is a simple serial communication method used for direct data exchange between two devices.
It was **developed in the 1960s**, with early implementations by **companies like Western Digital and DEC** as part of computer serial interfaces.
UART uses asynchronous communication, meaning no shared clock is required.
Data is transmitted using start bits, data bits, optional parity, and stop bits.
It is widely used in microcontrollers, debugging interfaces, and serial ports.

---

[**Modbus Protocol**](Modbus/README.md)<br>
The **Modbus protocol** is a serial communication protocol used for transmitting data between electronic devices in industrial systems.
It was **developed by Modicon (now Schneider Electric) in 1979**.
Modbus follows a master–slave (client–server) architecture.
It is commonly used over **RS-232, RS-485, and TCP/IP** networks.
Modbus is valued for its simplicity, reliability, and wide industrial adoption.

---

[**MQTT**](MQTT/README.md)<br>
**MQTT (Message Queuing Telemetry Transport)** is a lightweight, publish-subscribe network protocol designed for efficient communication between devices, particularly in constrained environments. Originally developed by IBM in 1999 for monitoring oil pipelines, MQTT has evolved into an OASIS and ISO standard protocol widely used in Internet of Things (IoT), mobile applications, and machine-to-machine (M2M) communication.

---

[**Profibus Protocol**](Profibus/README.md)<br>
The **PROFIBUS (Process Field Bus) protocol** is an industrial communication standard used for real-time data exchange in automation systems.
It was **developed in Germany in 1989** by a consortium led by **Siemens**.
PROFIBUS uses a master–slave architecture with cyclic data communication.
It supports high-speed communication over **RS-485 and fiber-optic** media.
PROFIBUS is widely used in factory and process automation.

---

[**Protobuf Protocol**](Protobuf/README.md)<br>
**Protocol Buffers (Protobuf)** is a language-neutral, platform-neutral mechanism for serializing structured data.
It was **developed by Google and released publicly in 2008**.
Protobuf uses a compact binary format that is faster and smaller than XML or JSON.
Data structures are defined using `.proto` files, enabling automatic code generation.
It is widely used in distributed systems, APIs, and microservices.

---

[**TCP-IP / UDP Protocols**](TCPIP/README.md)<br>
**TCP/IP (Transmission Control Protocol/Internet Protocol)** is a fundamental communication protocol suite for networks, including the Internet.
It was **developed in the 1970s by Vint Cerf and Bob Kahn** and became the standard for ARPANET in 1983.
TCP/IP provides reliable, ordered, and error-checked delivery of data (TCP) and addressing/routing of packets across networks (IP).
It supports a wide range of applications, including web browsing, email, and file transfer.
The protocol suite is layered, scalable, and forms the backbone of modern networking.

**UDP (User Datagram Protocol)** is a simple, connectionless transport-layer protocol used for fast data transmission over IP networks.
It was **developed in the 1980s by David P. Reed** as part of the original TCP/IP protocol suite.
Unlike TCP, UDP does not guarantee delivery, order, or error correction, making it lightweight and low-latency.
It is commonly used in applications like video streaming, online gaming, and DNS queries.
UDP is ideal for scenarios where speed is more critical than reliability.

---

[**gRPC**](gRPC/README.md)<br>
**gRPC** (gRPC Remote Procedure Call) is a modern, high-performance, open-source RPC (Remote Procedure Call) framework developed by Google. It enables client and server applications to communicate transparently and build connected systems.

---

[**CommonAPI**](CommonAPI/README.md)<br>
**CommonAPI** is a C++-based Inter-Process Communication (IPC) framework primarily used in automotive and embedded systems. 
It provides a language-independent middleware abstraction layer that enables communication between different software components across process boundaries.

---

[**SOME/IP**](SomeIp/README.md)<br>
**SOME/IP (Scalable service-Oriented MiddlewarE over IP)** is an automotive middleware solution designed for control messages in automotive Ethernet networks. 
Developed by BMW and standardized by AUTOSAR, it enables service-oriented communication between Electronic Control Units (ECUs) in modern vehicles.

---

[**REST**](REST/README.md)<br>
**REST (Representational State Transfer)** is an architectural style for designing networked applications. 
REST defines a set of constraints that, when applied to web services, create scalable, stateless, and cacheable systems.
