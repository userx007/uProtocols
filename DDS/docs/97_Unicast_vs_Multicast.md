## Unicast vs. Multicast

In the context of Ethernet networking protocols, **Unicast** and **Multicast** are two fundamental methods for delivering data frames from a source to one or more destination devices on a Local Area Network (LAN). They differ primarily in how many destinations receive the data and how the MAC (Media Access Control) addresses are interpreted.

Here’s a detailed explanation of both concepts:

### 1. Unicast
**Definition**:
Unicast communication is a **one-to-one** transmission. A frame is sent from a single source to a **single specific destination** identified by its unique MAC address.

#### Key Characteristics:
- **Destination MAC Address**: The destination MAC address in the Ethernet frame header is the **unique unicast MAC address** of a single network interface card (NIC).
- **Processing**: Only the device whose MAC address matches the destination field processes the frame. All other devices on the same Ethernet segment discard the frame (after checking the MAC address).
- **Efficiency**: Efficient because only one device processes the data, saving CPU and bandwidth.
- **Use Cases**:
  - Web browsing (HTTP/HTTPS)
  - Email (SMTP, IMAP)
  - File transfers (FTP)
  - Any standard client-server communication.

#### Example:
If Device A (MAC `00:1A:2B:3C:4D:5E`) sends a frame to Device B (MAC `00:1A:2B:3C:4D:5F`), only Device B will process the frame.

---

### 2. Multicast
**Definition**:
Multicast communication is a **one-to-many** transmission. A single frame is sent from a source to a **specific group of destinations** that have joined that multicast group.

#### Key Characteristics:
- **Destination MAC Address**: The destination MAC address in the Ethernet frame header is a **multicast MAC address**. This is a special range of MAC addresses reserved for multicast traffic.
  - In Ethernet, multicast MAC addresses start with the least significant bit of the first octet set to **1** (e.g., `01:00:5E:xx:xx:xx` for IPv4 multicast).
- **Group Membership**: Devices must explicitly "join" a multicast group (usually via protocols like IGMP for IP multicast) to receive frames sent to that group’s MAC address.
- **Processing**: Only devices that are members of the multicast group process the frame. Non-members discard it.
- **Bandwidth Efficiency**: More efficient than broadcasting because it sends data only to interested parties, unlike broadcast which sends to all devices on the LAN.
- **Use Cases**:
  - Video streaming (IPTV)
  - Online multiplayer games
  - Financial data feeds (stock prices)
  - Routing protocols (e.g., OSPF uses multicast `224.0.0.5` and `224.0.0.6`)

#### Example:
If a video server sends a frame to multicast MAC `01:00:5E:00:00:01`, only devices that have subscribed to that multicast group will receive and process the frame.

---

### Comparison: Unicast vs. Multicast

| Feature | Unicast | Multicast |
|---------|---------|-----------|
| **Destination** | One specific device | A group of devices |
| **MAC Address Type** | Unique unicast MAC address | Special multicast MAC address (LSB of first octet = 1) |
| **Network Load** | Higher if sending to many devices (one copy per device) | Lower (one copy over the network, replicated only where needed) |
| **Processing Overhead** | Low (only one device processes) | Low (only group members process) |
| **Complexity** | Simple | Requires group management (e.g., IGMP, PIM) |
| **Reliability** | Inherently reliable (ACKs possible) | Often unreliable (best-effort); ACKs may require reverse unicast |
| **Typical Use** | Web, email, file transfer | Streaming video, audio, real-time data feeds |

---

### Important Note: Broadcast vs. Multicast
While not asked, it’s helpful to distinguish Multicast from **Broadcast**:
- **Broadcast**: Sent to **all** devices on the LAN (destination MAC `FF:FF:FF:FF:FF:FF`). Every device processes the frame.
- **Multicast**: Sent only to **interested** devices. Only devices that joined the group process the frame.

---

### How Ethernet Handles Multicast
1. **Addressing**:
   IPv4 multicast addresses (`224.0.0.0` to `239.255.255.255`) are mapped to Ethernet multicast MAC addresses using a standardized algorithm (e.g., `01:00:5E:00:00:00` to `01:00:5E:7F:FF:FF`).

2. **Switching**:
   - **Unicast frames**: Switches learn the source MAC address and forward the frame only to the port associated with the destination MAC (learned via MAC address table).
   - **Multicast frames**: Switches use protocols like **IGMP Snooping** to determine which ports have devices interested in a multicast group. The switch then forwards the multicast frame only to those ports, reducing unnecessary traffic.

3. **Joining a Group**:
   Devices use **IGMP (Internet Group Management Protocol)** to tell their local router (and switch, if IGMP snooping is enabled) which multicast groups they want to receive.

---

### Summary
- **Unicast**: One-to-one. Used for most general network traffic. Simple, reliable, but less bandwidth-efficient for group communication.
- **Multicast**: One-to-many. Used for streaming and real-time group data. More bandwidth-efficient than unicast for many-to-one communication, but requires group management.

Both are essential for modern Ethernet networks, with unicast handling the bulk of general traffic and multicast optimizing delivery for group-oriented applications.

---

When a device decides to join a multicast group (e.g., a streaming video service), it doesn't just guess who the destination is. It actively communicates this information to the network using **IGMP messages**.

Here is exactly **how** this information is provided, step-by-step:

### 1. The Device Sends an IGMP Join Message
When an application (like VLC, Zoom, or IPTV client) wants to receive data for a specific multicast IP address (e.g., `239.1.1.100`), the device’s network stack generates an **IGMP Membership Report** message.

- **Protocol**: IGMP (Version 2 or Version 3, commonly IGMPv3 in modern networks).
- **Destination IP**: The message is sent to the **All-IGMP-Routers** multicast address (`224.0.0.2`). This ensures that any router listening for IGMP traffic on the local subnet receives it.
- **Content**: The message contains the **Multicast Group Address** (`239.1.1.100`) that the device wishes to join.

> **Key Point**: The device does **not** send its own IP address as the destination. It sends the *group’s* IP address as the payload.

### 2. How the Message Travels: Link-Local Scope
IGMP messages have a **Time-To-Live (TTL)** value of **1**. This means:
- The message is **only** processed by devices on the **same local subnet** (same VLAN).
- It does **not** cross routers (unless the router is the default gateway for that subnet).
- This is crucial because multicast group membership is local to each subnet. A router for Subnet A doesn’t care about a device in Subnet B unless they communicate via multicast routing (like PIM).

### 3. The Switch’s Role: IGMP Snooping
If the network uses switches instead of hubs, the switch needs to know where to forward the multicast frames. Here’s how it learns:

1. **Listening**: The switch “ snoops” (listens) to all IGMP messages passing through it.
2. **Learning**: When it sees an IGMP Join Report from Device A for Group `239.1.1.100`, it records:
   > *"Port 5 (connected to Device A) is interested in Group `239.1.1.100`."*
3. **Forwarding**: When a multicast frame arrives for `239.1.1.100`, the switch forwards it **only** to Port 5 (and any other ports that have also sent IGMP Join Reports for that group).

Without IGMP snooping, the switch would flood the multicast frame to **all ports** (like a broadcast), wasting bandwidth.

### 4. The Router’s Role: IGMP Proxy/Querier
The local router (Layer 3 device) also receives the IGMP Join Report.

1. **Recording Membership**: The router notes that Subnet X has a host interested in Group `239.1.1.100`.
2. **Upstream Communication**: The router uses a **multicast routing protocol** (like **PIM – Protocol Independent Multicast**) to tell its upstream router:
   > *"Hey, I have a host on Subnet X who wants data from `239.1.1.100`. Please send me that traffic."*
3. **Traffic Flow**: The upstream router starts sending multicast traffic for that group to this router, which then forwards it down to the subnet.

### 5. Keeping the Membership Alive: IGMP Queries
Multicast group membership is **not permanent**. If a device leaves without telling the network, the router might keep sending traffic to it unnecessarily. To prevent this, the router periodically sends **IGMP General Queries** (every 125 seconds by default).

- **Router**: Sends an IGMP Query to `224.0.0.1` (All Hosts).
- **Device**: If it still wants the group, it responds with an IGMP Report.
- **Device**: If it no longer wants the group, it either sends an **IGMP Leave Message** (IGMPv2/v3) or simply stops responding to the query.
- **Router**: If no devices respond to the query for a group, the router stops forwarding traffic for that group.

---

### Summary of the Information Flow

| Step | Who? | What? | How is the Info Provided? |
|------|------|-------|---------------------------|
| 1 | Device | Wants Group `239.1.1.100` | Sends **IGMP Report** message to `224.0.0.2` containing Group IP. |
| 2 | Switch | Learns Port Group Mapping | **Snoops** IGMP Report; records: *"Port 5 → Group `239.1.1.100`"* |
| 3 | Router | Learns Subnet Group Interest | Receives IGMP Report; records: *"Subnet X → Group `239.1.1.100`"* |
| 4 | Router | Requests Upstream Traffic | Uses **PIM** to tell upstream router: *"Send me traffic for `239.1.1.100`"* |
| 5 | Switch | Forwards Multicast Frames | Only forwards frames for `239.1.1.100` to **Port 5** (not all ports). |

### Key Takeaway
The "information" is provided via **IGMP Membership Report messages**, which are Ethernet frames containing an IP header with the **source IP** of the device and the **destination IP** of the multicast group. These messages are link-local (TTL=1) and are intercepted by switches (via snooping) and routers (via their IP stack) to build the necessary forwarding state.

---

## ASCII diagram illustrating the flow of information when a device joins a multicast group.

### Scenario Setup:
*   **Subnet:** `192.168.1.0/24`
*   **Device (PC):** `192.168.1.100`
*   **Multicast Group:** `239.1.1.100`
*   **Router/Gateway:** `192.168.1.1`
*   **Switch:** Connects PC and Router.

```text
       [Internet / Multicast Source]
                   |
                   | (Multicast Traffic for 239.1.1.100 arrives)
                   v
   +-------------------------------------------------------+
   |                 [ Core Network / Router ]             |
   |                                                       |
   |  Receives PIM Join from 192.168.1.1                   |
   |  Sends Data Packet --> 239.1.1.100 to 192.168.1.1     |
   +--------------------------+----------------------------+
                              |
                              | (Ethernet Frame with DST MAC
                              |  of 239.1.1.100 group)
                              v
   +-------------------------------------------------------+
   |                   [ SW (Switch) ]                     |
   |                                                       |
   |  [IGMP Snooping Table]:                               |
   |  Port 1 (PC) --> Group 239.1.1.100                    |
   |  Port 2 (Other PCs) --> [No Group]                    |
   |                                                       |
   |  Action: Forwards Multicast Frame ONLY to Port 1      |
   +--------------------------+----------------------------+
                              |
                              | (Frame sent to Port 1 only)
                              v
   +-------------------------------------------------------+
   |       [ PC (192.168.1.100) ]                          |
   |                                                       |
   |  1. App requests stream for 239.1.1.100               |
   |  2. OS sends IGMP Report (Group 239.1.1.100)          |
   |     -> DST IP: 224.0.0.2 (All Routers)                |
   |     -> TTL: 1 (Local Subnet only)                     |
   |  3. Switch snoopers this message                      |
   |  4. PC receives Multicast Data from Router            |
   |  5. App processes Data                                |
   +-------------------------------------------------------+
                              ^
                              |
                              | (IGMP Report Message)
                              |
   +-------------------------------------------------------+
   |                   [ PC ]                              |
   |  Sends IGMP Membership Report                         |
   |  Payload: Group 239.1.1.100                           |
   +-------------------------------------------------------+
```

### Step-by-Step Walkthrough of the Diagram:

1.  **Initial State**:
    *   The PC wants to watch a video stream from `239.1.1.100`.
    *   The Switch has no record of this group. It would normally flood multicast traffic to all ports.

2.  **PC Sends IGMP Report (Step A)**:
    *   The PC generates an **IGMP Membership Report**.
    *   It sets the destination IP to `224.0.0.2` (All IGMP Routers).
    *   It sets the TTL to `1`.
    *   **The Switch "Snoops" this packet**: It sees the report and updates its internal table: *"Port 1 is interested in Group `239.1.1.100`."*

3.  **Router Receives IGMP Report (Step B)**:
    *   The IGMP packet also reaches the **Router** (since it’s on the same subnet).
    *   The Router records that Host `192.168.1.100` wants Group `239.1.1.100`.
    *   The Router uses **PIM (Protocol Independent Multicast)** to send a "Join" message upstream toward the source of the video stream.

4.  **Multicast Data Arrives (Step C)**:
    *   The video stream (from the Internet/Core Network) reaches the Router for Group `239.1.1.100`.
    *   The Router encapsulates the data in an Ethernet frame with the **Multicast MAC Address** for `239.1.1.100` (e.g., `01:00:5E:00:01:64`).

5.  **Switch Forwards Efficiently (Step D)**:
    *   The Switch receives the frame.
    *   It looks up its **IGMP Snooping Table**.
    *   It sees that **only Port 1** is interested in this group.
    *   It forwards the frame **only** to Port 1 (to the PC).
    *   *If there were other PCs on Port 2 that didn't join the group, they do **not** receive the frame.*

6.  **PC Receives Data (Step E)**:
    *   The PC’s NIC receives the frame because its MAC address matches the multicast MAC (or it’s in promiscuous mode for the group).
    *   The OS delivers the payload to the application (e.g., VLC).

### Why This Matters:
*   **Without IGMP**: The Switch would flood the video stream to **all** ports. Every PC on the network would receive the data, increasing CPU load on all devices and consuming bandwidth.
*   **With IGMP**: Only the interested PC receives the data. Bandwidth and CPU resources are conserved.