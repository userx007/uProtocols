# Bluetooth Networking: RFCOMM, L2CAP, and BLE

## Detailed Description

Bluetooth networking enables wireless communication between devices over short distances (typically 10-100 meters). Unlike traditional TCP/IP networking that operates over Ethernet or Wi-Fi, Bluetooth uses its own protocol stack designed for low-power, point-to-point, and mesh communications.

### Core Bluetooth Protocols

**1. RFCOMM (Radio Frequency Communications)**
RFCOMM is a serial port emulation protocol that provides a simple reliable data stream similar to TCP. It's built on top of L2CAP and emulates RS-232 serial ports, making it easy to replace wired serial connections with wireless ones. RFCOMM is commonly used for:
- Bluetooth serial port profile (SPP)
- File transfers
- Dial-up networking
- Headset and hands-free profiles

**2. L2CAP (Logical Link Control and Adaptation Protocol)**
L2CAP is the foundation protocol that sits above the Bluetooth baseband layer. It provides:
- Connection-oriented and connectionless data services
- Protocol multiplexing (multiple protocols over a single connection)
- Segmentation and reassembly of large packets
- Quality of service (QoS) management
- Group abstractions for efficient multicast

**3. BLE (Bluetooth Low Energy)**
BLE, introduced in Bluetooth 4.0, is designed for applications requiring low power consumption. Key features include:
- Significantly reduced power usage compared to Classic Bluetooth
- GATT (Generic Attribute Profile) for data exchange
- Advertising and scanning mechanisms
- Connection intervals optimized for battery life
- Use cases: IoT sensors, fitness trackers, beacons, smart home devices

### Protocol Stack Comparison

**Classic Bluetooth Stack:**
```
Application
    ↓
RFCOMM / Other Profiles
    ↓
L2CAP
    ↓
HCI (Host Controller Interface)
    ↓
Baseband / Link Manager
```

**BLE Stack:**
```
Application
    ↓
GATT (Generic Attribute Profile)
    ↓
ATT (Attribute Protocol)
    ↓
L2CAP (simplified)
    ↓
HCI
    ↓
Link Layer
```

## Programming Examples

### C/C++ Examples

#### 1. RFCOMM Server (Linux BlueZ)

```c
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <string.h>
#include <errno.h>

#define RFCOMM_CHANNEL 1

int create_rfcomm_server() {
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    char buffer[1024] = { 0 };
    int server_sock, client_sock, bytes_read;
    socklen_t opt = sizeof(rem_addr);
    
    // Create socket
    server_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (server_sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Bind to local Bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = (uint8_t) RFCOMM_CHANNEL;
    
    if (bind(server_sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return -1;
    }
    
    // Listen for connections
    if (listen(server_sock, 1) < 0) {
        perror("listen");
        close(server_sock);
        return -1;
    }
    
    printf("RFCOMM server listening on channel %d\n", RFCOMM_CHANNEL);
    
    // Accept connection
    client_sock = accept(server_sock, (struct sockaddr *)&rem_addr, &opt);
    if (client_sock < 0) {
        perror("accept");
        close(server_sock);
        return -1;
    }
    
    // Get remote device address
    char remote_addr[18] = { 0 };
    ba2str(&rem_addr.rc_bdaddr, remote_addr);
    printf("Accepted connection from %s\n", remote_addr);
    
    // Read data
    while ((bytes_read = read(client_sock, buffer, sizeof(buffer) - 1)) > 0) {
        buffer[bytes_read] = '\0';
        printf("Received: %s\n", buffer);
        
        // Echo back
        write(client_sock, buffer, bytes_read);
    }
    
    close(client_sock);
    close(server_sock);
    return 0;
}

int main() {
    return create_rfcomm_server();
}
```

#### 2. RFCOMM Client (Linux BlueZ)

```c
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <string.h>

int connect_rfcomm_client(const char *dest_addr, int channel) {
    struct sockaddr_rc addr = { 0 };
    int sock, status;
    char message[1024];
    
    // Create socket
    sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Set connection parameters
    addr.rc_family = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t) channel;
    str2ba(dest_addr, &addr.rc_bdaddr);
    
    // Connect
    status = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
    if (status < 0) {
        perror("connect");
        close(sock);
        return -1;
    }
    
    printf("Connected to %s on channel %d\n", dest_addr, channel);
    
    // Send data
    const char *msg = "Hello from RFCOMM client!";
    status = write(sock, msg, strlen(msg));
    if (status < 0) {
        perror("write");
    } else {
        printf("Sent: %s\n", msg);
    }
    
    // Receive response
    int bytes = read(sock, message, sizeof(message) - 1);
    if (bytes > 0) {
        message[bytes] = '\0';
        printf("Received: %s\n", message);
    }
    
    close(sock);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <bluetooth_address>\n", argv[0]);
        return 1;
    }
    
    return connect_rfcomm_client(argv[1], 1);
}
```

#### 3. L2CAP Socket Programming

```c
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/l2cap.h>
#include <string.h>

#define L2CAP_PSM 0x1001  // Dynamic PSM

int create_l2cap_server() {
    struct sockaddr_l2 loc_addr = { 0 }, rem_addr = { 0 };
    char buffer[1024];
    int server_sock, client_sock, bytes_read;
    socklen_t opt = sizeof(rem_addr);
    
    // Create L2CAP socket
    server_sock = socket(AF_BLUETOOTH, SOCK_SEQPACKET, BTPROTO_L2CAP);
    if (server_sock < 0) {
        perror("socket");
        return -1;
    }
    
    // Bind socket
    loc_addr.l2_family = AF_BLUETOOTH;
    loc_addr.l2_bdaddr = *BDADDR_ANY;
    loc_addr.l2_psm = htobs(L2CAP_PSM);
    
    if (bind(server_sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr)) < 0) {
        perror("bind");
        close(server_sock);
        return -1;
    }
    
    // Listen
    if (listen(server_sock, 1) < 0) {
        perror("listen");
        close(server_sock);
        return -1;
    }
    
    printf("L2CAP server listening on PSM 0x%04x\n", L2CAP_PSM);
    
    // Accept connection
    client_sock = accept(server_sock, (struct sockaddr *)&rem_addr, &opt);
    if (client_sock < 0) {
        perror("accept");
        close(server_sock);
        return -1;
    }
    
    char remote_addr[18];
    ba2str(&rem_addr.l2_bdaddr, remote_addr);
    printf("Accepted L2CAP connection from %s\n", remote_addr);
    
    // Communication loop
    while ((bytes_read = read(client_sock, buffer, sizeof(buffer))) > 0) {
        printf("Received %d bytes\n", bytes_read);
        write(client_sock, buffer, bytes_read);
    }
    
    close(client_sock);
    close(server_sock);
    return 0;
}

int main() {
    return create_l2cap_server();
}
```

#### 4. BLE GATT Server (Using BlueZ D-Bus API)

```cpp
#include <iostream>
#include <memory>
#include <gio/gio.h>
#include <string>

class BLEGattServer {
private:
    GDBusConnection *connection;
    guint registration_id;
    
    static void on_method_call(
        GDBusConnection *connection,
        const gchar *sender,
        const gchar *object_path,
        const gchar *interface_name,
        const gchar *method_name,
        GVariant *parameters,
        GDBusMethodInvocation *invocation,
        gpointer user_data) {
        
        if (g_strcmp0(method_name, "ReadValue") == 0) {
            // Return characteristic value
            guchar value[] = {0x48, 0x65, 0x6c, 0x6c, 0x6f}; // "Hello"
            GVariant *result = g_variant_new_fixed_array(
                G_VARIANT_TYPE_BYTE, value, sizeof(value), 1);
            g_dbus_method_invocation_return_value(
                invocation, g_variant_new("(@ay)", result));
        }
        else if (g_strcmp0(method_name, "WriteValue") == 0) {
            GVariant *value_variant;
            g_variant_get(parameters, "(@ay@a{sv})", &value_variant, nullptr);
            
            gsize length;
            const guchar *data = (const guchar *)g_variant_get_fixed_array(
                value_variant, &length, 1);
            
            std::cout << "Received " << length << " bytes" << std::endl;
            g_variant_unref(value_variant);
            
            g_dbus_method_invocation_return_value(invocation, nullptr);
        }
    }
    
public:
    BLEGattServer() : connection(nullptr), registration_id(0) {}
    
    bool initialize() {
        GError *error = nullptr;
        connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, nullptr, &error);
        
        if (error) {
            std::cerr << "Error connecting to D-Bus: " 
                      << error->message << std::endl;
            g_error_free(error);
            return false;
        }
        
        // Register GATT characteristic interface
        const gchar *introspection_xml =
            "<node>"
            "  <interface name='org.bluez.GattCharacteristic1'>"
            "    <method name='ReadValue'>"
            "      <arg type='a{sv}' name='options' direction='in'/>"
            "      <arg type='ay' name='value' direction='out'/>"
            "    </method>"
            "    <method name='WriteValue'>"
            "      <arg type='ay' name='value' direction='in'/>"
            "      <arg type='a{sv}' name='options' direction='in'/>"
            "    </method>"
            "    <property name='UUID' type='s' access='read'/>"
            "    <property name='Flags' type='as' access='read'/>"
            "  </interface>"
            "</node>";
        
        GDBusNodeInfo *introspection_data = 
            g_dbus_node_info_new_for_xml(introspection_xml, &error);
        
        if (error) {
            std::cerr << "Error parsing introspection: " 
                      << error->message << std::endl;
            g_error_free(error);
            return false;
        }
        
        static const GDBusInterfaceVTable interface_vtable = {
            on_method_call,
            nullptr,  // get_property
            nullptr   // set_property
        };
        
        registration_id = g_dbus_connection_register_object(
            connection,
            "/org/bluez/example/characteristic0",
            introspection_data->interfaces[0],
            &interface_vtable,
            this,
            nullptr,
            &error);
        
        g_dbus_node_info_unref(introspection_data);
        
        if (error) {
            std::cerr << "Error registering object: " 
                      << error->message << std::endl;
            g_error_free(error);
            return false;
        }
        
        std::cout << "BLE GATT server initialized" << std::endl;
        return true;
    }
    
    ~BLEGattServer() {
        if (registration_id > 0) {
            g_dbus_connection_unregister_object(connection, registration_id);
        }
        if (connection) {
            g_object_unref(connection);
        }
    }
};

int main() {
    BLEGattServer server;
    if (!server.initialize()) {
        return 1;
    }
    
    GMainLoop *loop = g_main_loop_new(nullptr, FALSE);
    g_main_loop_run(loop);
    g_main_loop_unref(loop);
    
    return 0;
}
```

### Rust Examples

#### 1. BLE Central (Scanner) using btleplug

```rust
use btleplug::api::{Central, Manager as _, Peripheral as _, ScanFilter};
use btleplug::platform::{Adapter, Manager};
use std::error::Error;
use std::time::Duration;
use tokio::time;

async fn scan_devices(adapter: &Adapter) -> Result<(), Box<dyn Error>> {
    println!("Starting BLE scan...");
    
    // Start scanning for devices
    adapter.start_scan(ScanFilter::default()).await?;
    
    // Scan for 10 seconds
    time::sleep(Duration::from_secs(10)).await;
    
    // Stop scanning
    adapter.stop_scan().await?;
    
    // Get discovered peripherals
    let peripherals = adapter.peripherals().await?;
    
    println!("Found {} devices:", peripherals.len());
    
    for peripheral in peripherals.iter() {
        let properties = peripheral.properties().await?;
        let is_connected = peripheral.is_connected().await?;
        
        let local_name = properties
            .as_ref()
            .and_then(|p| p.local_name.clone())
            .unwrap_or_else(|| String::from("Unknown"));
        
        let address = properties
            .as_ref()
            .map(|p| p.address.to_string())
            .unwrap_or_else(|| String::from("N/A"));
        
        println!(
            "  Device: {} [{}] - Connected: {}",
            local_name, address, is_connected
        );
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Get the default Bluetooth adapter
    let manager = Manager::new().await?;
    let adapters = manager.adapters().await?;
    
    if adapters.is_empty() {
        eprintln!("No Bluetooth adapters found");
        return Ok(());
    }
    
    let adapter = &adapters[0];
    println!("Using adapter: {:?}", adapter.adapter_info().await?);
    
    scan_devices(adapter).await?;
    
    Ok(())
}
```

#### 2. BLE GATT Client

```rust
use btleplug::api::{
    Central, Characteristic, Manager as _, Peripheral as _,
    ScanFilter, WriteType,
};
use btleplug::platform::{Adapter, Manager};
use std::error::Error;
use std::time::Duration;
use tokio::time;
use uuid::Uuid;

// Example UUIDs (replace with your service/characteristic UUIDs)
const SERVICE_UUID: Uuid = Uuid::from_u128(0x0000180F_0000_1000_8000_00805F9B34FB);
const CHARACTERISTIC_UUID: Uuid = Uuid::from_u128(0x00002A19_0000_1000_8000_00805F9B34FB);

async fn connect_and_read(
    adapter: &Adapter,
    device_name: &str,
) -> Result<(), Box<dyn Error>> {
    println!("Searching for device: {}", device_name);
    
    adapter.start_scan(ScanFilter::default()).await?;
    time::sleep(Duration::from_secs(5)).await;
    adapter.stop_scan().await?;
    
    let peripherals = adapter.peripherals().await?;
    
    // Find target device
    let peripheral = peripherals
        .into_iter()
        .find(|p| {
            if let Ok(Some(props)) = p.properties().await {
                props.local_name.as_deref() == Some(device_name)
            } else {
                false
            }
        })
        .ok_or("Device not found")?;
    
    println!("Connecting to device...");
    peripheral.connect().await?;
    
    println!("Discovering services...");
    peripheral.discover_services().await?;
    
    // Find characteristic
    let characteristics = peripheral.characteristics();
    let target_char = characteristics
        .iter()
        .find(|c| c.uuid == CHARACTERISTIC_UUID)
        .ok_or("Characteristic not found")?;
    
    println!("Reading characteristic...");
    let value = peripheral.read(target_char).await?;
    println!("Value: {:?}", value);
    
    // Write to characteristic (if writable)
    if target_char.properties.write {
        println!("Writing to characteristic...");
        let data = vec![0x01, 0x02, 0x03];
        peripheral.write(target_char, &data, WriteType::WithResponse).await?;
        println!("Write successful");
    }
    
    // Subscribe to notifications
    if target_char.properties.notify {
        println!("Subscribing to notifications...");
        peripheral.subscribe(target_char).await?;
        
        let mut notification_stream = peripheral.notifications().await?;
        
        // Listen for 10 seconds
        let timeout = time::sleep(Duration::from_secs(10));
        tokio::pin!(timeout);
        
        loop {
            tokio::select! {
                Some(notification) = notification_stream.next() => {
                    println!(
                        "Notification from {:?}: {:?}",
                        notification.uuid, notification.value
                    );
                }
                _ = &mut timeout => {
                    println!("Timeout reached");
                    break;
                }
            }
        }
        
        peripheral.unsubscribe(target_char).await?;
    }
    
    peripheral.disconnect().await?;
    println!("Disconnected");
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let manager = Manager::new().await?;
    let adapters = manager.adapters().await?;
    
    if adapters.is_empty() {
        eprintln!("No Bluetooth adapters found");
        return Ok(());
    }
    
    let adapter = &adapters[0];
    
    // Replace with your device name
    connect_and_read(adapter, "MyBLEDevice").await?;
    
    Ok(())
}
```

#### 3. RFCOMM-style Serial Port (using bluer crate)

```rust
use bluer::{Address, AddressType, Session};
use std::error::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

async fn rfcomm_client(address: Address) -> Result<(), Box<dyn Error>> {
    // Create Bluetooth session
    let session = Session::new().await?;
    let adapter = session.default_adapter().await?;
    
    println!("Using adapter: {}", adapter.name());
    
    // Connect to RFCOMM service
    // Note: bluer uses L2CAP sockets; for RFCOMM, you'd typically use
    // the Serial Port Profile (SPP) through a different mechanism
    
    // For demonstration, here's L2CAP connection:
    let mut socket = adapter.connect_profile(
        &address,
        bluer::rfcomm::Profile::SerialPort,
    ).await?;
    
    println!("Connected to {}", address);
    
    // Send data
    let message = b"Hello from Rust Bluetooth client!";
    socket.write_all(message).await?;
    println!("Sent: {:?}", String::from_utf8_lossy(message));
    
    // Receive response
    let mut buffer = vec![0u8; 1024];
    let n = socket.read(&mut buffer).await?;
    
    if n > 0 {
        println!("Received: {:?}", String::from_utf8_lossy(&buffer[..n]));
    }
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    // Example Bluetooth address (replace with actual device address)
    let address = Address::new([0x00, 0x11, 0x22, 0x33, 0x44, 0x55]);
    
    rfcomm_client(address).await?;
    
    Ok(())
}
```

#### 4. BLE Peripheral (Advertisement)

```rust
use btleplug::api::{bleuuid::uuid_from_u16, Peripheral as _, WriteType};
use btleplug::platform::Peripheral;
use std::error::Error;

// Simplified BLE advertising example
async fn advertise_ble_service() -> Result<(), Box<dyn Error>> {
    // Note: BLE peripheral mode (advertising) is platform-specific
    // and may require elevated privileges
    
    println!("BLE Peripheral/Advertising mode");
    println!("This functionality is platform-dependent");
    
    // On Linux, you'd typically use BlueZ D-Bus API
    // On other platforms, native APIs are required
    
    // Pseudo-code for advertising:
    // 1. Set advertising data (service UUIDs, local name, etc.)
    // 2. Start advertising
    // 3. Handle incoming connections
    // 4. Implement GATT server for characteristics
    
    println!(
        "For full peripheral implementation, consider using:\n\
         - BlueZ D-Bus API on Linux\n\
         - CoreBluetooth on macOS/iOS\n\
         - Windows Bluetooth LE APIs on Windows"
    );
    
    Ok(())
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    advertise_ble_service().await?;
    Ok(())
}
```

## Summary

**Bluetooth networking** provides wireless communication through three main protocol layers:

**RFCOMM** emulates serial ports over Bluetooth, offering a simple stream-based interface similar to TCP. It's ideal for replacing wired serial connections and is widely used in legacy Bluetooth profiles. Programming involves creating sockets with `BTPROTO_RFCOMM`, binding to channels, and using standard socket read/write operations.

**L2CAP** operates at a lower level, providing connection-oriented and connectionless packet services. It offers more control over packet structure and QoS parameters, making it suitable for applications requiring fine-grained control over data transmission. L2CAP uses `BTPROTO_L2CAP` with PSM (Protocol Service Multiplexer) identifiers.

**BLE (Bluetooth Low Energy)** revolutionizes IoT and wearable devices through its ultra-low power consumption. It uses the GATT (Generic Attribute Profile) architecture with services and characteristics, rather than traditional sockets. BLE supports advertising, scanning, and attribute-based data exchange, making it perfect for sensor networks and battery-powered devices.

**Key differences**: Classic Bluetooth (RFCOMM/L2CAP) offers higher throughput and is backward-compatible with older devices, while BLE provides dramatically lower power consumption at the cost of reduced data rates. Programming approaches differ significantly: Classic Bluetooth uses socket-based APIs similar to TCP/IP, while BLE requires working with GATT services, characteristics, and often platform-specific APIs (BlueZ on Linux, CoreBluetooth on macOS/iOS).

Both C/C++ and Rust provide robust libraries for Bluetooth development, with C typically using BlueZ directly on Linux, while Rust leverages crates like `btleplug` and `bluer` for cross-platform and idiomatic async programming.