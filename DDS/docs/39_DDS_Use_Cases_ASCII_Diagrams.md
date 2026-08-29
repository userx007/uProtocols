# DDS Use Cases — Visual Reference (ASCII Diagrams)

A collection of ASCII diagrams illustrating how DDS is used in practice, from core mechanics to industry-specific architectures.

---

## 1. Core Pub/Sub Decoupling (vs. Broker-Based Messaging)

```
  BROKER-BASED (Kafka/MQTT/AMQP)              DDS (BROKERLESS, DATA-CENTRIC)
  ------------------------------              -------------------------------

  [Publisher] --> [Broker] --> [Subscriber]    [Publisher] <====> [Subscriber]
                     |                              ^                  ^
                     v                               \                /
               [Subscriber]                           \______________/
                     |                                  direct RTPS
                     v                                  peer-to-peer
               [Subscriber]                             (no middleman)

  Decoupled in: space (broker between)          Decoupled in: space, time (via
  Single point of failure: the broker            Durability QoS), AND flow
                                                  No single point of failure
```

---

## 2. Domain Isolation

```
                         Physical Network
   +---------------------------------------------------------+
   |                                                         |
   |   Domain ID 0                    Domain ID 5            |
   |  +-------------------+          +-------------------+   |
   |  |  Participant A    |          |  Participant C    |   |
   |  |  Participant B    |          |  Participant D    |   |
   |  +-------------------+          +-------------------+   |
   |    (multicast :7400)              (multicast :8650)     |
   |                                                         |
   |   A and B can discover each other.  C and D can discover|
   |   each other.  A/B and C/D are COMPLETELY invisible to  |
   |   one another, even sharing the same wire.              |
   +---------------------------------------------------------+
```

---

## 3. Two-Tier Discovery: SPDP then SEDP

```
   STEP 1 — SPDP (find PARTICIPANTS)
   ----------------------------------
   Participant A  --multicast announce-->  [239.255.0.1:7400]  --> Participant B
   Participant B  --multicast announce-->  [239.255.0.1:7400]  --> Participant A

              Both now know: "a peer exists at these locators"

   STEP 2 — SEDP (find ENDPOINTS, reliable unicast)
   -------------------------------------------------
   Participant A  --DCPSPublication: "Writer on topic X, QoS=..."-->  Participant B
   Participant B  --DCPSSubscription: "Reader on topic X, QoS=..."-->  Participant A

              Matching engine checks: topic name + type + QoS + partition
                                |
                                v
                         [ MATCH FOUND ]
                                |
                                v
                    Actual DATA submessages now flow
```

---

## 4. QoS Request vs. Offered Compatibility

```
      DataWriter (OFFERS)                DataReader (REQUESTS)
   +-----------------------+          +-----------------------+
   | Reliability: RELIABLE |   <-->   | Reliability: RELIABLE |   OK  (offered >= requested)
   | Durability: TRANSIENT |   <-->   | Durability: VOLATILE  |   OK  (offered >= requested)
   +-----------------------+          +-----------------------+
              MATCH

   +------------------------+         +-----------------------+
   | Reliability:BEST_EFFORT|  <-->   | Reliability: RELIABLE |   FAIL
   +------------------------+         +-----------------------+
        writer can't satisfy reader's stronger requirement
              NO MATCH -> on_offered_incompatible_qos fires
```

---

## 5. Keyed Instances — One Topic, Many Independent Streams

```
   Topic: VehicleStatus   (keyed by vehicle_id)

   Writer publishes:
     {id:101, battery:80} {id:102, battery:55} {id:101, battery:78} {id:103, battery:12}

                                  |
                                  v
   Reader's local cache (per-instance history):

     Instance 101:  [battery:80] -> [battery:78]     (ALIVE)
     Instance 102:  [battery:55]                     (ALIVE)
     Instance 103:  [battery:12]                     (ALIVE)

   Each instance has its OWN history depth, ownership, and lifecycle —
   even though all three share one Topic + one Type.
```

---

## 6. Reliable Delivery: HEARTBEAT / ACKNACK / Retransmit

```
   Writer                                         Reader
     |                                                |
     |----- DATA(seq=1) ----------------------------> |  received
     |----- DATA(seq=2) ---------X (lost in transit)  |
     |----- DATA(seq=3) ----------------------------> |  received (gap detected!)
     |                                                |
     |<---- HEARTBEAT(min=1,max=3) ------------------ |  (writer announces range)
     |                                                |
     |<---- ACKNACK(missing=[2]) ---------------------|  (reader requests repair)
     |                                                |
     |----- DATA(seq=2) [RETRANSMIT] ---------------->|  received, gap closed
     |                                                |
                    In-order delivery restored
```

---

## 7. Multicast Fan-Out vs. Unicast Fan-Out

```
   UNICAST (N copies sent)                MULTICAST (1 copy sent)

   Writer                                  Writer
     |--pkt-->[Reader A]                     |
     |--pkt-->[Reader B]                     |--pkt-->[239.255.1.5]
     |--pkt-->[Reader C]                              /    |    \
     |--pkt-->[Reader D]                        Rdr A  Rdr B  Rdr C  Rdr D
                                          (network switches replicate it)

   Writer CPU/NIC load: 4x                Writer CPU/NIC load: 1x
   Works everywhere (cloud/VPN/WAN)       Needs multicast-capable network
```

---

## 8. Ownership Strength — Hot/Warm Standby Failover

```
   Instance: "MainControlLoop"     EXCLUSIVE ownership

   [Primary Controller]  --strength=100-->\
                                            +--> [Actuator Reader]
   [Backup Controller]   --strength=50 -->/        sees only the
                                                    highest-strength
                                                    LIVE writer

   t0: Primary alive   -> Reader sees PRIMARY's data
   t1: Primary crashes (liveliness lease expires)
   t2: Reader automatically switches -> sees BACKUP's data
   t3: Primary restarts, re-asserts strength=100
   t4: Reader automatically switches back -> sees PRIMARY again

           No application failover code required.
```

---

## 9. Content-Filtered Topic — Writer-Side Bandwidth Savings

```
   Writer publishes ALL vehicles (10,000 samples/sec)

   Without CFT:                          With CFT ("battery_level < 20"):

   Writer --> [ALL 10,000/sec] --> Net    Writer --(filter evaluated locally)
              Reader discards            Writer --> [~40/sec matching] --> Net
              9,960 unwanted samples                  Reader
              on receipt                    Only low-battery alerts ever
                                             cross the network
```

---

## 10. Robotics / ROS 2 — Sensor Fusion on One Robot

```
                         ROS_DOMAIN_ID = 7
   +-----------------------------------------------------------------+
   |                                                                 |
   |  [Lidar Node]---BEST_EFFORT--->\                                |
   |  [Camera Node]--BEST_EFFORT--->  [/sensor_data topics]          |
   |  [IMU Node]-----BEST_EFFORT--->/         |                      |
   |                                          v                      |
   |                              [Sensor Fusion Node]               |
   |                                          |                      |
   |                                   RELIABLE, KEEP_LAST(10)       |
   |                                          v                      |
   |                              [/cmd_vel topic] --RELIABLE-->     |
   |                                          |                      |
   |                                          v                      |
   |                                 [Motor Controller Node]         |
   |                                                                 |
   |   SHMEM transport used automatically for same-host node pairs   |
   +-----------------------------------------------------------------+
```

---

## 11. Multi-Robot Fleet — Domain Isolation + Explicit Bridge

```
   ROS_DOMAIN_ID=1              ROS_DOMAIN_ID=2              ROS_DOMAIN_ID=3
  +--------------+             +--------------+             +--------------+
  |   Robot A    |             |   Robot B    |             |   Robot C    |
  | (isolated)   |             | (isolated)   |             | (isolated)   |
  +--------------+             +--------------+             +--------------+
         |                            |                            |
         +----------------------------+----------------------------+
                                      |
                             [ domain_bridge /
                               Routing Service ]
                                      |
                                      v
                            +---------------------+
                            |  Fleet Manager      |
                            |  (sees all robots   |
                            |  via bridged topics)|
                            +---------------------+
```

---

## 12. Industrial / SCADA — Deterministic Control Loop

```
   [PLC / Sensor I/O] --KEEP_LAST(1), DEADLINE=10ms--> [Control Topic]
                                                              |
                                                              v
                                                   [SCADA Controller]
                                                     computes setpoint
                                                              |
                              RELIABLE, DEADLINE=10ms         v
   [Actuator] <----------------------------------- [Setpoint Topic]

   If DEADLINE missed (no update within 10ms):
     on_requested_deadline_missed() fires -> failsafe logic triggers
     (e.g., hold last safe position, alarm operator)
```

---

## 13. Financial Trading — Low-Latency Market Data

```
   [Exchange Feed Handler]
           |
           | SHMEM (co-located) or low-latency UDP
           | BEST_EFFORT, no batching, ASYNC publish disabled
           v
   [Price Topic] ----------------------> [Strategy Engine A]
           |------------------------->  [Strategy Engine B]
           |------------------------->  [Risk Monitor]

   All readers on same host segment: SHMEM auto-selected (microsecond latency)
   No batching: every tick sent immediately (throughput sacrificed for latency)
```

---

## 14. IoT / Smart Building — High Fan-Out, Low Bandwidth

```
   [Thousands of Sensors] --BEST_EFFORT, small payload-->
                                 |
                                 v
                          [Multicast Group]
                            /    |    \
                     [Reader1][Reader2]...[ReaderN]  (dashboards, analytics)

   Batching enabled (max_flush_delay=50ms) to reduce packet count
   ContentFilteredTopic used per-dashboard ("zone = 'Floor3'")
```

---

## 15. Healthcare / Medical Devices — Reliable + Secure

```
   [Patient Monitor] --RELIABLE, ENCRYPT, SIGN---> [Bedside Topic]
                                                          |
                     Access Control: only authenticated   v
                     "ICU_Display" & "CentralNursingStation" [Central Station]
                     grants may SUBSCRIBE                    |
                                                             v
                                                     [Alarm Aggregator]
                                                     (on missed deadline
                                                      -> escalate alert)

   Governance doc: metadata_protection=ENCRYPT, data_protection=ENCRYPT
   Unauthorized participants: rejected at Access Control layer
```

---

## 16. WAN Bridging Across Sites

```
   Site A (Domain 0)                                   Site B (Domain 1)
  +-----------------+                                  +-----------------+
  | Local DDS cloud |                                  | Local DDS cloud |
  +--------+--------+                                  +--------+--------+
           |                                                    |
           v                                                    v
   [Routing Service A] <====== WAN / TCP / VPN ======> [Routing Service B]
           |                                                    |
     content-filtered:                                  republishes filtered
     "battery_level<20"                                 topic locally at Site B

   Only alert-worthy samples cross the expensive WAN link.
```

---

## 17. Security Layering (Defense in Depth)

```
   +-----------------------------------------------------------+
   |  Layer 1: AUTHENTICATION                                  |
   |  X.509 cert + Identity CA + Diffie-Hellman handshake      |
   +-----------------------------------------------------------+
                              |
                              v
   +--------------------------------------------------------------+
   |  Layer 2: ACCESS CONTROL                                     |
   |  Governance (domain policy) + Permissions (per-participant)  |
   +--------------------------------------------------------------+
                              |
                              v
   +--------------------------------------------------------------+
   |  Layer 3: CRYPTOGRAPHIC PROTECTION                           |
   |  Discovery: ENCRYPT   |   RTPS envelope: SIGN                |
   |  Per-topic: FleetCommand=ENCRYPT, Heartbeat=SIGN             |
   +--------------------------------------------------------------+
                              |
                              v
                    [ Data flows on the wire ]
```

---

## 18. Discovery Scalability — Mesh vs. Discovery Server

```
   DEFAULT MESH (O(n^2))                  DISCOVERY SERVER (~O(n))

   A---B                                       [Discovery Server]
   |\ /|                                        /   |   |   \
   | X |          <-- every participant        A    B   C    D
   |/ \|              talks to every                (unicast only
   C---D              other participant               to server)

   6 links for 4 nodes                    4 links for 4 nodes
   Grows quadratically                    Grows linearly
   (server redundancy recommended
    to avoid new SPOF)
```

---

## 19. Type Evolution with XTypes (Appendable)

```
   Legacy Reader (v1 type)             New Writer (v2 type)
   +--------------------+              +----------------------+
   | vehicle_id         |              | vehicle_id           |
   | battery_level      |   <---match--| battery_level        |
   +--------------------+              | tire_pressure  [NEW] |
                                       +----------------------+

   v1 reader receives v2 sample -> tire_pressure silently ignored
   v2 reader receives v1 sample -> tire_pressure defaults/absent

   No redeployment required on either side — @appendable made this safe.
```

---

## Summary — Which Diagram Maps to Which Use Case

| # | Diagram | Primary Use Case |
|---|---|---|
| 1 | Broker vs. brokerless | Understanding DDS's core value prop |
| 2 | Domain isolation | Multi-tenant / multi-system separation |
| 3 | SPDP/SEDP | Any DDS deployment — discovery fundamentals |
| 4 | QoS matching | Debugging non-matching endpoints |
| 5 | Keyed instances | Fleet tracking, sensor arrays, per-object state |
| 6 | Reliable delivery | Guaranteed-delivery command/control links |
| 7 | Multicast vs unicast | Network planning, bandwidth optimization |
| 8 | Ownership strength failover | Redundant controllers, safety-critical systems |
| 9 | Content-filtered topics | Bandwidth-constrained high-fan-out systems |
| 10 | ROS 2 single robot | Robotics sensor fusion pipelines |
| 11 | Multi-robot fleet | Fleet management, domain bridging |
| 12 | Industrial/SCADA | Deterministic real-time control loops |
| 13 | Financial trading | Ultra-low-latency market data distribution |
| 14 | IoT/smart building | Massive fan-out, resource-constrained sensors |
| 15 | Healthcare devices | Secure, reliable, safety-critical telemetry |
| 16 | WAN bridging | Multi-site, cross-domain integration |
| 17 | Security layering | Any deployment requiring DDS Security |
| 18 | Discovery scalability | Large fleets / high participant counts |
| 19 | XTypes evolution | Long-lived systems with incremental upgrades |
