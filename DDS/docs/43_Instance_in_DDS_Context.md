## Instances in the context of DDS

In the context of DDS (Data Distribution Service), specifically Cyclone DDS, the term **"Instance"** is fundamental to understanding how data is managed, especially for **Keyed Topics**.

### 1. The Core Concept: An "Instance" Represents a Logical Entity
An **Instance** is not the raw data itself, but rather a **logical representation of a real-world object** identified by its **Key(s)**.

Think of it this way:
*   **Data (Sample)**: The current state/measurements of something (e.g., "Car A is going 50 mph").
*   **Instance**: The persistent identity of that thing (e.g., "Car A").

If you publish multiple samples for the same Key, they all belong to the **same Instance**. If you publish a sample with a different Key, it belongs to a **different Instance**.

### 2. When are Instances Used?
Instances are only relevant when a Topic has **Key(s)** defined via `DDS_OWNERSHIP_QOS` or more commonly, **`DDS_KEYED_TOPIC_QOS`**.

*   **Non-Keyed Topic**: All data samples are treated as part of one big "Instance" (often called the "anonymous instance"). There is no distinction between samples other than their timestamp/order.
*   **Keyed Topic**: The data is grouped by its Key. Each unique Key value creates a separate Instance.

### 3. How It Works: Example

Imagine a Topic called `VehicleStatus` with a Key defined as `VehicleID`.

| Sample | VehicleID (Key) | Speed | Instance Identity |
| :--- | :--- | :--- | :--- |
| 1 | "Car_001" | 50 km/h | Instance **"Car_001"** |
| 2 | "Car_002" | 70 km/h | Instance **"Car_002"** |
| 3 | "Car_001" | 55 km/h | Instance **"Car_001"** (Same as Sample 1) |
| 4 | "Car_001" | STOP | Instance **"Car_001"** (Same as Sample 1 & 3) |

*   **Samples 1, 3, and 4** all update the **same Instance** ("Car_001").
*   **Sample 2** updates a **different Instance** ("Car_002").

### 4. Why Instances Matter

#### A. State Management
DDS maintains the **last known good state** for each Instance. If a subscriber connects late, it can request the current state of specific Instances it cares about. This is called **Last-Writer-Wins** semantics per Instance.

#### B. Keyed DataReader APIs
Keyed DataReaders provide methods to operate on specific Instances:
*   `read_next_sample()`: Read the next sample from any instance.
*   `read_next_instance()`: Read the next sample for a specific Instance Key.
*   `get_key_value()`: Get the Key value of a sample.
*   `delete_data()`: Delete all samples for a specific Instance (useful for "resetting" or signaling loss).

#### C. Ownership & Liveliness
*   **Ownership QoS**: Determines which DataWriter has the right to update a specific Instance. This prevents race conditions where two writers try to update the same "Car" at the same time.
*   **Liveliness QoS**: Tracks whether an Instance is still "alive." If a writer stops sending updates for an Instance, the subscriber can detect that the real-world object might be gone or the writer failed.

### 5. Special Instance Values
DDS defines special integer constants for instances:

| Constant | Value | Meaning |
| :--- | :--- | :--- |
| `DDS_ANY_INSTANCE` | -1 | Match any instance (used in queries). |
| `DDS_NO_INSTANCE` | -2 | No instance matches (used in queries). |
| `DDS_NOT_ALIVE_DISPOSED_INSTANCE` | -1 | The instance was explicitly disposed (via `dispose()`). |
| `DDS_NOT_ALIVE_NO_WRITERS_INSTANCE` | -2 | No writers are currently publishing this instance. |

### 6. Cyclone DDS Specifics
In Cyclone DDS:
*   **Instance handling is internal**: The library automatically manages the cache of instances per DataReader.
*   **Keyed Topic Definition**: You must define keys when creating the Topic:
  ```c
  DDS_DomainParticipant_create_topic(
      participant,
      "VehicleStatus",
      "VehicleStatusType",
      DDS_TOPIC_KEYED, // Important!
      NULL
  );
  ```
*   **Query Support**: Cyclone DDS supports **DataQuery** on instances. You can ask: "Give me all samples where Instance Key == 'Car_001' AND Speed > 60."

### Summary Analogy
*   **Topic** = A spreadsheet column name (e.g., "EmployeeName").
*   **Key** = The row identifier (e.g., EmployeeID).
*   **Instance** = A single row (e.g., "EmployeeID=123").
*   **Sample** = The data in that row at a specific moment (e.g., "John Doe, Salary: $50k").
*   **DataWriter** = Updates the row.
*   **DataReader** = Reads the row.

If you update "EmployeeID=123" multiple times, you are updating the **same Instance**. Each update overwrites the previous state for that Instance (depending on QoS).