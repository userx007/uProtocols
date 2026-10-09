To understand how DDS (Data Distribution Service implementations like Cyclone DDS) performs internal communication, we must look past the simple concept of "Publisher sends, Subscriber receives." DDS is a complex middleware that handles discovery, QoS (Quality of Service), security, reliability, and transport abstraction.

Here is a breakdown of the internal communication flow, followed by an ASCII diagram.

---

### Key Internal Components Involved in Communication

1. **Application Layer**: Your C++/Java/Python code creating Publishers/Subscribers.
2. **DDS API/Implementation Layer**: The Cyclone DDS library linked to your application.
3. **Discovery Service (Auto-Discovery)**: DDS nodes must find each other. Cyclone DDS uses:
   - **Multicast/UDP** for local discovery (by default).
   - **RTPS (Real-Time Publish-Subscribe)** protocol messages.
4. **Transport Layer**: Cyclone DDS abstracts the network. By default, it uses **UDP** (for low latency) but can switch to **TCP** or **SHM (Shared Memory)** for performance.
5. **Data-Writer/Data-Reader**: Internal structures that manage QoS, serialization, and packetization.
6. **Network Stack**: The OS kernel network stack (or SHM kernel subsystem).

---

### Internal Communication Flow: Step-by-Step

#### **Phase 1: Discovery**
Before any data flows, the Publisher and Subscriber must know each other exists.

1. **Publisher Starts**:
   - Creates a `DomainParticipant`.
   - Creates a `Publisher` and `DataWriter`.
   - Sends **Hello** messages via UDP multicast (or unicast to known nodes) announcing its presence, domain ID, and capabilities.

2. **Subscriber Starts**:
   - Creates a `DomainParticipant` in the same domain.
   - Creates a `Subscriber` and `DataReader`.
   - Listens for **Hello** messages.
   - Receives the Publisher’s Hello.
   - Sends a **Hello-Ack** back (or waits for the Publisher to notice it).

3. **Matchmaking**:
   - Both nodes exchange capability information (QoS, types, endpoint info).
   - A **Writer-Reader Match** is established. This triggers the creation of internal communication channels.

#### **Phase 2: Data Transport**
Once matched, data flows. The flow depends on the configured **Reliability QoS** and **Transport**.

##### **Scenario A: Unreliable + Best Effort (Default for low latency)**
1. **Publisher Application** writes data.
2. **Cyclone DDS Writer** serializes the data into an **RTPS Send Message**.
3. The message is passed to the **Network Stack** (UDP).
4. The message is **broadcast/multicast** on the local network (or unicast if only one subscriber).
5. **Subscriber’s Network Stack** receives the UDP packet.
6. **Cyclone DDS Reader** deserializes the RTPS message.
7. **Subscriber Application** receives the data via the callback or `read()` call.

> **Note**: No ACKs are sent. If the packet is lost, it’s gone.

##### **Scenario B: Reliable + Best Effort (Retries on loss)**
1. Publisher sends RTPS message.
2. Subscriber receives it and sends an **ACK** back to the Publisher.
3. If the Publisher doesn’t receive an ACK within a timeout, it **retransmits** the message.
4. This continues until ACK is received or max retries are reached.

##### **Scenario C: Shared Memory (SHM) Transport (Optimized)**
1. Publisher and Subscriber are on the **same machine**.
2. Cyclone DDS detects this and maps a **shared memory segment**.
3. Publisher writes data directly into SHM.
4. Subscriber reads data directly from SHM.
5. **No network stack involvement** → extremely low latency.

---

### ASCII Diagram: Communication Channel Flow

Here is a simplified ASCII diagram showing the **Reliable + UDP Transport** scenario:

```
+---------------------+          +---------------------+
|   Publisher App     |          |   Subscriber App    |
|                     |          |                     |
|  +---------------+  |          |  +---------------+  |
|  | DataWriter    |  |          |  | DataReader    |  |
|  +---------------+  |          |  +---------------+  |
|         |           |          |         |           |
|         v           |          |         v           |
|  +---------------+  |          |  +---------------+  |
|  | Cyclone DDS   |  |          |  | Cyclone DDS   |  |
|  | Implementation|  |          |  | Implementation|  |
|  +---------------+  |          |  +---------------+  |
|         |           |          |         |           |
|         v           |          |         v           |
|  +---------------+  |          |  +---------------+  |
|  | Network Stack |  |          |  | Network Stack |  |
|  | (UDP/IP)      |  |          |  | (UDP/IP)      |  |
|  +---------------+  |          |  +---------------+  |
|         |           |          |         ^           |
|         v           |          |         |           |
+-----> [ UDP Packet ] <-------> +         |           |
|         |           |          |         |           |
|         v           |          |         v           |
|  +---------------+  |          |  +---------------+  |
|  | RTPS Protocol |  |          |  | RTPS Protocol |  |
|  | Message       |  |          |  | Message       |  |
|  +---------------+  |          |  +---------------+  |
|         |           |          |         |           |
|         v           |          |         v           |
|  +---------------+  |          |  +---------------+  |
|  | ACK Generator |<------------+  | ACK Receiver  |  |
|  +---------------+  |          |  +---------------+  |
|         |           |          |         |           |
+---------+           |          +---------+           |
                      |
                      |
              +-------v-------+
              |   Network     |
              |(UDP/Multicast |
              |  or Unicast)  |
              +---------------+
```

### Detailed Flow Explanation from Diagram:

1. **Publisher App → DataWriter**: Application calls `write()`.
2. **DataWriter → Cyclone DDS**: Passes serialized data to the library.
3. **Cyclone DDS → Network Stack**: The library encapsulates data in **RTPS Send Message**, then in **UDP packet**.
4. **UDP Packet → Network**: Sent over the local network (multicast/unicast).
5. **Network → Subscriber’s Network Stack**: Received by the subscriber’s host.
6. **Subscriber’s Network Stack → RTPS Protocol**: UDP payload extracted, RTPS message parsed.
7. **RTPS Protocol → DataReader**: Data deserialized and queued for the application.
8. **ACK Path (Reliable QoS)**:
   - Subscriber’s **ACK Receiver** generates an ACK.
   - ACK sent back via UDP to Publisher.
   - Publisher’s **ACK Generator** receives ACK.
   - If no ACK, Publisher retransmits from its cache.

---

### Internal Cyclone DDS Specifics

- **RTPS (Real-Time Publish-Subscribe Protocol)**: This is the core protocol. All DDS communication is encoded as RTPS messages. Cyclone DDS implements the RTPS standard.
- **Discovery Domain**: Each DDS domain has a unique ID. Discovery only works within the same domain.
- **Endpoint Matching**: A `DataWriter` matches with any `DataReader` that:
  - Has the same topic name.
  - Has compatible QoS settings.
  - Is in the same domain.
- **Zero-Copy Optimization**: Cyclone DDS often uses zero-copy serialization/deserialization when possible, especially with Shared Memory or aligned memory layouts, to minimize CPU overhead.

### Summary

Internal communication in Cyclone DDS is:
1. **Protocol-based**: Uses RTPS over UDP (or TCP/SHM).
2. **Discovery-driven**: Nodes find each other before sending data.
3. **QoS-aware**: Reliability, durability, and history settings dictate whether ACKs are sent, retransmissions occur, and data is buffered.
4. **Abstraction-layered**: The application sees a simple `write()`/`read()`, but internally, RTPS messages, ACKs, and discovery packets flow through the network stack.

For maximum performance, Cyclone DDS uses **Shared Memory** when possible, bypassing the OS network stack entirely. For remote communication, it uses optimized UDP with RTPS headers.

---

## Additional Remarks

### 1. Is Discovery always over UDP?
**Mostly Yes, but configurable.**
- By default, Cyclone DDS uses **UDP Multicast** for discovery (`Discovery: Auto` or `Static`).
- However, you **can** configure discovery to use **UDP Unicast** (to a specific peer) or even **Shared Memory** (in newer versions) if the participants are on the same machine.
- **But**: In your `tcpdump`, seeing UDP discovery logs is the **default and expected behavior**.

### 2. Is Data Exchange **always** over Shared Memory (SHM) on the same machine?
**Yes, by default in Cyclone DDS.**
- If two participants (Publisher and Subscriber) are running on the **same OS kernel** (same machine), Cyclone DDS automatically detects this and switches the transport for data exchange from **UDP** to **Shared Memory (SHM)**.
- This is why `tcpdump` shows **no UDP packets** for data: the data is copied directly into a shared memory segment mapped into both processes, bypassing the network stack entirely.

---

### Why `tcpdump` shows nothing for data

`tcpdump` listens to the **network interface** (e.g., `eth0`, `lo`). It captures packets that go through the kernel’s network stack.

- **Discovery**: Uses UDP Multicast → Goes through `eth0` (or loopback) → **Captured by tcpdump**.
- **Data (SHM)**: Written to `/dev/shm` or a mapped memory region → **Never touches the network interface** → **Not captured by tcpdump**.

---

### How to Verify Shared Memory is Being Used

#### 1. Check Cyclone DDS Configuration
Look at your `cyclonedds.xml` or environment variables. Ensure the transport is set to `SHM` (or `UDP` with `SHM` fallback).

Default Cyclone DDS config often looks like this:
```xml
<CycloneDDS>
  <Domain>
    <Id>*</Id>
    <General>
      <MaxMessageSize>65535</MaxMessageSize>
      <MaxMessageCount>10000</MaxMessageCount>
    </General>
    <Discovery>
      <Mode>AUTO</Mode>
    </Discovery>
    <Transport>
      <Type>UDP</Type> <!-- This allows SHM fallback -->
    </Transport>
  </Domain>
</CycloneDDS>
```

#### 2. Use `ss` or `netstat` to Check Sockets
Even if data doesn’t go over UDP, the endpoints still have sockets. But SHM data doesn’t use these sockets for transfer.

#### 3. Use `strace` to See Shared Memory Syscalls
Run your Publisher and Subscriber with `strace` to see `mmap`, `shmget`, `shmat`, etc.

**Publisher Side (`strace -e trace=mmap,shmget,shmat ./publisher`):**
```bash
shmget(IPC_PRIVATE, 4096, IPC_CREAT|0666) = 98304
shmat(98304, NULL, 0)                    = 0x7f8b4c000000
mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, 98304, 0) = 0x7f8b4c000000
...
write(1, "Data written to shared memory\n", 30) = 30
```

**Subscriber Side (`strace -e trace=mmap,shmget,shmat,shmdt ./subscriber`):**
```bash
shmget(IPC_PRIVATE, 4096, IPC_CREAT|0666) = 98305
shmat(98305, NULL, 0)                    = 0x7f8b4c001000
mmap(NULL, 4096, PROT_READ|PROT_WRITE, MAP_SHARED, 98305, 0) = 0x7f8b4c001000
...
read(3, "Data from shared memory\n", 1024) = 18
```

#### 4. Use `lsof` to Find Shared Memory Segments
```bash
lsof | grep shm
```
You should see entries like:
```
publisher  12345 user  mem   REG   0,9       4096  123456 /dev/shm/CycloneDDS_shm_1
subscriber 12346 user  mem   REG   0,9       4096  123456 /dev/shm/CycloneDDS_shm_1
```
This confirms both processes are mapping the **same** shared memory segment.

---

### What if You Want UDP for Data?

If you want to force Cyclone DDS to use UDP even on the same machine, you can:

1. **Disable SHM Transport**:
   ```xml
   <CycloneDDS>
     <Domain>
       <Transport>
         <Type>UDP</Type>
         <SHM>
           <Enabled>false</Enabled>
         </SHM>
       </Transport>
     </Domain>
   </CycloneDDS>
   ```

2. **Use Different Nodes**:
   Run the Publisher and Subscriber on **different machines** (or in Docker containers on the same host with network isolation). Then you’ll see UDP packets in `tcpdump`.

3. **Use `lo` Loopback**:
   Even if SHM is enabled, some DDS implementations may fall back to loopback UDP if SHM fails. Check if `tcpdump -i lo` shows anything.

---

### Summary

| Scenario | Transport | `tcpdump` Shows Data? | Why? |
|----------|-----------|------------------------|------|
| **Same Machine** (Default) | **Shared Memory (SHM)** | ❌ No | Data stays in kernel memory, never touches NIC. |
| **Same Machine** (SHM disabled) | UDP (Loopback) | ✅ Yes | Data goes through `lo` interface. |
| **Different Machines** | UDP/TCP | ✅ Yes | Data goes through physical/virtual NIC. |

### Conclusion

Your trace is **perfectly normal** for Cyclone DDS:
- **Discovery over UDP**: Yes, because SHM discovery is not always enabled by default.
- **Data over SHM**: Yes, because SHM is enabled by default for same-machine communication.

To see data in `tcpdump`, either **disable SHM** in the config or **run the nodes on different machines**.