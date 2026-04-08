# 81. Bluetooth Serial Port Profile (SPP)

- **Protocol Architecture** — the full stack diagram from Application → RFCOMM → L2CAP → Baseband, and how SDP channel discovery works
- **C/C++ Examples** — three complete implementations:
  - *Linux BlueZ*: full RFCOMM server with SDP registration + client with SDP channel discovery
  - *Windows WinSock2*: `AF_BTH`/`BTHPROTO_RFCOMM` client with SDP query
  - *ESP32 ESP-IDF*: bidirectional hardware UART ↔ Bluetooth SPP bridge using FreeRTOS tasks
- **Rust Examples** — using the `bluer` async crate:
  - SPP echo server with tokio `spawn` per connection
  - SPP client with exponential-backoff reconnection
  - Full async UART↔BT bridge combining `bluer` + `tokio-serial`
- **RFCOMM Channel Management** — dynamic free-channel scanning in both C and Rust
- **Reconnection patterns** — exponential backoff in both languages
- **Security table** — SSP, encryption, discoverability, PIN pitfalls
- **Common Pitfalls** — SDP omission, BLE vs Classic confusion, partial read/write handling, permission issues
- **Summary** — concise recap of all key points

## Wireless UART over Bluetooth

---

## Table of Contents

1. [Introduction](#introduction)
2. [Protocol Architecture](#protocol-architecture)
3. [SPP Communication Flow](#spp-communication-flow)
4. [Platform Support Overview](#platform-support-overview)
5. [Programming in C/C++](#programming-in-cc)
   - [Linux: BlueZ Stack](#linux-bluez-stack)
   - [Windows: WinSock2 with Bluetooth](#windows-winsock2-with-bluetooth)
   - [Embedded: ESP-IDF (ESP32)](#embedded-esp-idf-esp32)
6. [Programming in Rust](#programming-in-rust)
   - [Using the `btleplug` / `bluer` Crate](#using-bluer-crate)
   - [SPP Server and Client in Rust](#spp-server-and-client-in-rust)
7. [RFCOMM Channel Management](#rfcomm-channel-management)
8. [Error Handling and Reconnection](#error-handling-and-reconnection)
9. [Security Considerations](#security-considerations)
10. [Common Pitfalls](#common-pitfalls)
11. [Summary](#summary)

---

## Introduction

The **Bluetooth Serial Port Profile (SPP)** is a Bluetooth Classic profile that emulates a serial RS-232 cable connection over a wireless Bluetooth link. It provides a transparent, bidirectional byte-stream channel between two devices — effectively turning Bluetooth into a wireless UART.

SPP sits on top of the **RFCOMM** protocol, which itself emulates RS-232 serial ports over an L2CAP Bluetooth transport. The resulting abstraction behaves like a standard COM port (Windows) or `/dev/rfcomm` device (Linux), making it straightforward to port existing serial/UART code to wireless operation.

### Key Characteristics

- **Data model:** Raw bidirectional byte stream (no framing, no message boundaries)
- **Speed:** Typically 128 kbps–2 Mbps effective throughput (Bluetooth Classic)
- **Range:** ~10 m (Class 2) to ~100 m (Class 1)
- **Latency:** ~10–50 ms typical round trip
- **Pairing:** Requires Bluetooth pairing; supports PIN-based and SSP (Secure Simple Pairing)
- **Addressing:** RFCOMM channel number (1–30) analogous to a TCP port
- **Standard:** Defined in Bluetooth SIG Specification v1.1 and later

### Typical Use Cases

- Cable replacement for RS-232/UART peripherals (GPS modules, industrial sensors, barcode scanners)
- Wireless firmware update (bootloaders with UART interface)
- Communication with microcontroller-based embedded systems (Arduino, ESP32, STM32)
- Legacy industrial equipment modernization
- Wireless modem/AT-command interfaces (Bluetooth-to-serial adapters like HC-05, HC-06)

---

## Protocol Architecture

```
┌──────────────────────────────────────────┐
│         Application (SPP)                │
│    (serial read/write byte stream)        │
├──────────────────────────────────────────┤
│              RFCOMM                      │
│   (RS-232 emulation over L2CAP)          │
├──────────────────────────────────────────┤
│               L2CAP                      │
│  (Logical Link Control & Adaptation)     │
├──────────────────────────────────────────┤
│          Baseband / LMP                  │
│     (Bluetooth radio / link manager)     │
└──────────────────────────────────────────┘
```

### RFCOMM and Channel Numbers

RFCOMM multiplexes multiple virtual serial ports over a single L2CAP channel. Each virtual port is identified by a **DLCI** (Data Link Connection Identifier) derived from the RFCOMM channel number and direction bit. Channel numbers 1–30 are usable.

The server **advertises** a channel via the **SDP (Service Discovery Protocol)**, which allows clients to discover available SPP services by UUID `0x1101`.

---

## SPP Communication Flow

```
Server (Peripheral)                    Client (Central)
      │                                       │
      │  1. Register SDP record (UUID 0x1101) │
      │  2. Listen on RFCOMM channel N        │
      │                                       │
      │                                       │  3. Inquiry / device scan
      │                                       │  4. SDP query → get channel N
      │                                       │
      │◄──── 5. RFCOMM connect (channel N) ───│
      │                                       │
      │◄══════ 6. Bidirectional data  ════════│
      │                                       │
      │◄──── 7. Disconnect ───────────────────│
```

---

## Platform Support Overview

| Platform        | Stack / API                    | Device Node / Handle            |
|-----------------|--------------------------------|---------------------------------|
| Linux           | BlueZ, RFCOMM sockets          | `/dev/rfcomm0`, raw socket      |
| Windows         | WinSock2 Bluetooth             | `SOCKET` with `AF_BTH`          |
| macOS           | IOBluetooth (Objective-C/Swift)| `IOBluetoothRFCOMMChannel`      |
| Android         | Android Bluetooth API (Java)   | `BluetoothSocket`               |
| ESP32 (ESP-IDF) | Classic BT component           | `esp_spp_api.h`                 |
| Arduino         | SoftwareSerial / Serial        | `Serial` over HC-05/HC-06 UART  |

---

## Programming in C/C++

### Linux: BlueZ Stack

Linux exposes RFCOMM via standard POSIX sockets with the `AF_BLUETOOTH` family. Two approaches exist:

1. **Raw RFCOMM socket** — direct kernel RFCOMM access
2. **`/dev/rfcommN` device** — bind a virtual TTY via `rfcomm bind`, then use standard `open()`/`read()`/`write()`

#### 1a. RFCOMM Socket Server (C, BlueZ)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>   // sudo apt install libbluetooth-dev
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

#define SPP_UUID    0x1101
#define RFCOMM_CH   1
#define BUFSIZE     1024

/* Register an SDP record so clients can discover us via UUID 0x1101 */
static sdp_session_t *register_spp_service(uint8_t rfcomm_channel) {
    uuid_t root_uuid, l2cap_uuid, rfcomm_uuid, svc_uuid;
    sdp_list_t *l2cap_list = NULL, *rfcomm_list = NULL,
               *root_list  = NULL, *proto_list  = NULL,
               *access_proto_list = NULL;
    sdp_data_t *channel_data = NULL;
    sdp_record_t *record = sdp_record_alloc();
    sdp_session_t *session = NULL;

    /* Set overall access class: SPP UUID */
    sdp_uuid16_create(&svc_uuid, SPP_UUID);
    sdp_set_service_id(record, svc_uuid);

    /* Make the service publicly browsable */
    sdp_uuid16_create(&root_uuid, PUBLIC_BROWSE_GROUP);
    root_list = sdp_list_append(NULL, &root_uuid);
    sdp_set_browse_groups(record, root_list);

    /* L2CAP protocol */
    sdp_uuid16_create(&l2cap_uuid, L2CAP_UUID);
    l2cap_list = sdp_list_append(NULL, &l2cap_uuid);
    proto_list = sdp_list_append(NULL, l2cap_list);

    /* RFCOMM protocol + channel */
    sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
    channel_data = sdp_data_alloc(SDP_UINT8, &rfcomm_channel);
    rfcomm_list = sdp_list_append(NULL, &rfcomm_uuid);
    rfcomm_list = sdp_list_append(rfcomm_list, channel_data);
    proto_list  = sdp_list_append(proto_list, rfcomm_list);

    /* Attach protocol list to record */
    access_proto_list = sdp_list_append(NULL, proto_list);
    sdp_set_access_protos(record, access_proto_list);

    /* Set a human-readable name */
    sdp_set_info_attr(record, "SPP Server", NULL, "RFCOMM Serial Port");

    /* Connect to local SDP server and register */
    session = sdp_connect(BDADDR_ANY, BDADDR_LOCAL, SDP_RETRY_IF_BUSY);
    if (!session) {
        fprintf(stderr, "sdp_connect failed: %s\n", strerror(errno));
        goto cleanup;
    }
    if (sdp_record_register(session, record, 0) < 0) {
        fprintf(stderr, "sdp_record_register failed: %s\n", strerror(errno));
        sdp_close(session);
        session = NULL;
    }

cleanup:
    sdp_data_free(channel_data);
    sdp_list_free(rfcomm_list, NULL);
    sdp_list_free(l2cap_list,  NULL);
    sdp_list_free(proto_list,  NULL);
    sdp_list_free(root_list,   NULL);
    sdp_list_free(access_proto_list, NULL);
    sdp_record_free(record);
    return session;
}

int main(void) {
    struct sockaddr_rc local_addr  = {0};
    struct sockaddr_rc remote_addr = {0};
    socklen_t addr_len = sizeof(remote_addr);
    char buf[BUFSIZE];
    char bdaddr_str[18];
    int server_sock, client_sock;
    ssize_t n;

    /* Register SPP in SDP */
    sdp_session_t *sdp = register_spp_service(RFCOMM_CH);
    if (!sdp) {
        fprintf(stderr, "Failed to register SDP record\n");
        return EXIT_FAILURE;
    }

    /* Create RFCOMM socket */
    server_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (server_sock < 0) { perror("socket"); return EXIT_FAILURE; }

    /* Bind to any local adapter, chosen channel */
    local_addr.rc_family  = AF_BLUETOOTH;
    local_addr.rc_bdaddr  = *BDADDR_ANY;
    local_addr.rc_channel = (uint8_t)RFCOMM_CH;
    if (bind(server_sock, (struct sockaddr *)&local_addr, sizeof(local_addr)) < 0) {
        perror("bind"); close(server_sock); return EXIT_FAILURE;
    }

    listen(server_sock, 1);
    printf("SPP server listening on RFCOMM channel %d...\n", RFCOMM_CH);

    /* Accept one client */
    client_sock = accept(server_sock,
                         (struct sockaddr *)&remote_addr, &addr_len);
    if (client_sock < 0) { perror("accept"); goto done; }

    ba2str(&remote_addr.rc_bdaddr, bdaddr_str);
    printf("Connected: %s\n", bdaddr_str);

    /* Echo loop */
    while ((n = read(client_sock, buf, sizeof(buf))) > 0) {
        printf("RX %zd bytes\n", n);
        write(client_sock, buf, (size_t)n);   /* Echo back */
    }

    close(client_sock);
done:
    close(server_sock);
    sdp_close(sdp);
    return EXIT_SUCCESS;
}
```

**Build:**
```bash
gcc spp_server.c -o spp_server -lbluetooth
```

#### 1b. RFCOMM Socket Client (C, BlueZ)

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>

/* Discover SPP RFCOMM channel via SDP */
static int spp_get_channel(const char *target_bdaddr) {
    bdaddr_t target;
    str2ba(target_bdaddr, &target);

    sdp_session_t *session =
        sdp_connect(BDADDR_ANY, &target, SDP_RETRY_IF_BUSY);
    if (!session) { perror("sdp_connect"); return -1; }

    uuid_t svc_uuid;
    sdp_uuid16_create(&svc_uuid, SPP_UUID);   /* 0x1101 */

    uint32_t range = 0x0000ffff;
    sdp_list_t *search = sdp_list_append(NULL, &svc_uuid);
    sdp_list_t *attrid = sdp_list_append(NULL, &range);

    sdp_list_t *response = NULL;
    int err = sdp_service_search_attr_req(session, search,
                                          SDP_ATTR_REQ_RANGE,
                                          attrid, &response);
    sdp_close(session);
    if (err < 0) { perror("sdp_search"); return -1; }

    int channel = -1;
    for (sdp_list_t *r = response; r; r = r->next) {
        sdp_record_t *rec = (sdp_record_t *)r->data;
        sdp_list_t *protos = NULL;
        if (sdp_get_access_protos(rec, &protos) == 0) {
            channel = sdp_get_proto_port(protos, RFCOMM_UUID);
            sdp_list_foreach(protos, (sdp_list_func_t)sdp_list_free, NULL);
            sdp_list_free(protos, NULL);
        }
        sdp_record_free(rec);
        if (channel > 0) break;
    }
    sdp_list_free(response, NULL);
    return channel;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <BD_ADDR>\n", argv[0]);
        return EXIT_FAILURE;
    }

    int channel = spp_get_channel(argv[1]);
    if (channel < 0) { fprintf(stderr, "SPP channel not found\n"); return EXIT_FAILURE; }
    printf("Found SPP on RFCOMM channel %d\n", channel);

    struct sockaddr_rc addr = {0};
    int sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    addr.rc_family  = AF_BLUETOOTH;
    addr.rc_channel = (uint8_t)channel;
    str2ba(argv[1], &addr.rc_bdaddr);

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("connect"); close(sock); return EXIT_FAILURE;
    }
    printf("Connected to %s\n", argv[1]);

    const char *msg = "Hello from SPP client!\r\n";
    write(sock, msg, strlen(msg));

    char buf[256];
    ssize_t n = read(sock, buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; printf("Echo: %s", buf); }

    close(sock);
    return EXIT_SUCCESS;
}
```

---

### Windows: WinSock2 with Bluetooth

Windows exposes Bluetooth via `AF_BTH` / `BTHPROTO_RFCOMM`. The channel can be determined by an SDP query or hardcoded.

```cpp
// spp_client_win.cpp
// Compile: cl spp_client_win.cpp /link Ws2_32.lib
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2bth.h>          // Bluetooth extensions
#include <bluetoothapis.h>
#include <initguid.h>
#include <stdio.h>
#include <string.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Bthprops.lib")

// SPP Service Class UUID: {00001101-0000-1000-8000-00805F9B34FB}
DEFINE_GUID(SPP_SERVICE_CLASS_ID,
    0x00001101, 0x0000, 0x1000, 0x80, 0x00,
    0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB);

// Query SDP for the SPP RFCOMM channel on a remote device
static int QuerySppChannel(BTH_ADDR btAddr) {
    WSAQUERYSET  querySet = {0};
    GUID         serviceGuid = SPP_SERVICE_CLASS_ID;
    HANDLE       lookup = NULL;

    querySet.dwSize           = sizeof(WSAQUERYSET);
    querySet.lpServiceClassId = &serviceGuid;
    querySet.dwNameSpace      = NS_BTH;

    // Encode the remote BT address into the query
    SOCKADDR_BTH sa = {0};
    sa.addressFamily = AF_BTH;
    sa.btAddr        = btAddr;
    CSADDR_INFO csAddr = {0};
    csAddr.RemoteAddr.lpSockaddr  = (LPSOCKADDR)&sa;
    csAddr.RemoteAddr.iSockaddrLength = sizeof(sa);
    querySet.lpcsaBuffer = &csAddr;
    querySet.dwNumberOfCsAddrs = 1;

    DWORD flags = LUP_FLUSHCACHE | LUP_RETURN_ADDR;
    if (WSALookupServiceBegin(&querySet, flags, &lookup) != 0) {
        fprintf(stderr, "SDP lookup begin failed: %d\n", WSAGetLastError());
        return -1;
    }

    int channel = -1;
    DWORD bufSize = 4096;
    WSAQUERYSET *result = (WSAQUERYSET *)malloc(bufSize);
    if (result && WSALookupServiceNext(lookup, LUP_RETURN_ADDR | LUP_RETURN_BLOB,
                                       &bufSize, result) == 0) {
        if (result->dwNumberOfCsAddrs > 0) {
            SOCKADDR_BTH *pAddr =
                (SOCKADDR_BTH *)result->lpcsaBuffer->RemoteAddr.lpSockaddr;
            channel = (int)pAddr->port;
        }
    }
    free(result);
    WSALookupServiceEnd(lookup);
    return channel;
}

int main(void) {
    WSADATA wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // Target device address — replace with your device's BT address
    // Format: "XX:XX:XX:XX:XX:XX"
    BTH_ADDR target = 0x001122334455ULL; // placeholder

    int channel = QuerySppChannel(target);
    if (channel < 0) { channel = 1; /* fallback */ }
    printf("Connecting to RFCOMM channel %d\n", channel);

    SOCKET s = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (s == INVALID_SOCKET) {
        fprintf(stderr, "socket() failed: %d\n", WSAGetLastError());
        WSACleanup(); return 1;
    }

    SOCKADDR_BTH remoteAddr = {0};
    remoteAddr.addressFamily = AF_BTH;
    remoteAddr.btAddr        = target;
    remoteAddr.port          = (ULONG)channel;

    if (connect(s, (SOCKADDR*)&remoteAddr, sizeof(remoteAddr)) == SOCKET_ERROR) {
        fprintf(stderr, "connect() failed: %d\n", WSAGetLastError());
        closesocket(s); WSACleanup(); return 1;
    }
    printf("SPP connected!\n");

    const char *msg = "Hello SPP!\r\n";
    send(s, msg, (int)strlen(msg), 0);

    char buf[256] = {0};
    int n = recv(s, buf, sizeof(buf) - 1, 0);
    if (n > 0) printf("Received: %.*s", n, buf);

    closesocket(s);
    WSACleanup();
    return 0;
}
```

---

### Embedded: ESP-IDF (ESP32)

The ESP32 has native Bluetooth Classic support via the `ESP_SPP_API`. The following example sets up an SPP server that streams UART data wirelessly.

```c
// esp32_spp_uart_bridge.c
// Requires: CONFIG_BT_ENABLED=y, CONFIG_BT_CLASSIC_ENABLED=y in sdkconfig
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_bt_device.h"
#include "esp_spp_api.h"
#include "driver/uart.h"

#define TAG         "SPP_UART"
#define SPP_NAME    "ESP32_SPP"
#define UART_NUM    UART_NUM_1
#define UART_TX_PIN 17
#define UART_RX_PIN 16
#define UART_BAUD   115200
#define BUF_SIZE    1024

static uint32_t spp_handle = 0;         /* Handle of active SPP connection */
static QueueHandle_t uart_queue;

/* UART → SPP forwarding task */
static void uart_to_spp_task(void *arg) {
    uint8_t buf[BUF_SIZE];
    uart_event_t event;
    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            if (event.type == UART_DATA && spp_handle) {
                int len = uart_read_bytes(UART_NUM, buf,
                                          event.size, 20 / portTICK_PERIOD_MS);
                if (len > 0) {
                    esp_spp_write(spp_handle, len, buf);
                    ESP_LOGD(TAG, "UART→BT: %d bytes", len);
                }
            }
        }
    }
}

/* SPP event handler */
static void spp_callback(esp_spp_cb_event_t event,
                          esp_spp_cb_param_t *param) {
    switch (event) {
    case ESP_SPP_INIT_EVT:
        ESP_LOGI(TAG, "SPP initialized");
        esp_bt_dev_set_device_name(SPP_NAME);
        esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,
                                  ESP_BT_GENERAL_DISCOVERABLE);
        /* Start SPP server, accept inbound connections */
        esp_spp_start_srv(ESP_SPP_SEC_AUTHENTICATE,
                          ESP_SPP_ROLE_SLAVE, 0, SPP_NAME);
        break;

    case ESP_SPP_SRV_OPEN_EVT:
        spp_handle = param->srv_open.handle;
        ESP_LOGI(TAG, "Client connected, handle=%lu", spp_handle);
        break;

    case ESP_SPP_DATA_IND_EVT:
        /* BT → UART: forward received bytes to hardware UART */
        ESP_LOGD(TAG, "BT→UART: %d bytes", param->data_ind.len);
        uart_write_bytes(UART_NUM,
                         (const char *)param->data_ind.data,
                         param->data_ind.len);
        break;

    case ESP_SPP_CLOSE_EVT:
        ESP_LOGI(TAG, "Client disconnected");
        spp_handle = 0;
        break;

    default:
        break;
    }
}

void app_main(void) {
    /* Configure UART */
    uart_config_t uart_cfg = {
        .baud_rate  = UART_BAUD,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
    };
    uart_driver_install(UART_NUM, BUF_SIZE * 2, 0, 20, &uart_queue, 0);
    uart_param_config(UART_NUM, &uart_cfg);
    uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN,
                 UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);

    /* Initialize Bluetooth controller */
    ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_BLE));
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&bt_cfg));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));

    /* Initialize Bluedroid stack */
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());

    /* Register SPP callback and initialize SPP */
    ESP_ERROR_CHECK(esp_spp_register_callback(spp_callback));
    ESP_ERROR_CHECK(esp_spp_init(ESP_SPP_MODE_CB));

    /* Start UART→BT forwarding task */
    xTaskCreate(uart_to_spp_task, "uart_spp", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "SPP UART Bridge started. Device name: %s", SPP_NAME);
}
```

---

## Programming in Rust

Rust Bluetooth Classic (SPP/RFCOMM) support on Linux is provided primarily by the **`bluer`** crate, which wraps the BlueZ D-Bus API. For lower-level RFCOMM socket access, raw `libc` sockets can also be used.

### Dependencies (`Cargo.toml`)

```toml
[dependencies]
bluer   = { version = "0.17", features = ["full"] }
tokio   = { version = "1",   features = ["full"] }
futures = "0.3"
bytes   = "1"
log     = "0.4"
env_logger = "0.11"
```

### Using `bluer` Crate

The `bluer` crate provides an async Tokio-based interface to BlueZ. It supports RFCOMM streams directly.

#### SPP Server in Rust

```rust
// src/spp_server.rs
use bluer::{
    rfcomm::{Listener, SocketAddr},
    Address,
};
use std::error::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt};

const RFCOMM_CHANNEL: u8 = 4;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    env_logger::init();

    // Create RFCOMM listener on the given channel.
    // `Address::any()` binds to the first available local adapter.
    let local_addr = SocketAddr::new(Address::any(), RFCOMM_CHANNEL);
    let listener = Listener::bind(local_addr).await?;

    println!(
        "SPP server listening on RFCOMM channel {}",
        RFCOMM_CHANNEL
    );
    println!("Waiting for connection...");

    loop {
        let (mut stream, remote_addr) = listener.accept().await?;
        println!("Connected: {}", remote_addr.addr);

        tokio::spawn(async move {
            let mut buf = vec![0u8; 1024];
            loop {
                match stream.read(&mut buf).await {
                    Ok(0) => {
                        println!("Client {} disconnected", remote_addr.addr);
                        break;
                    }
                    Ok(n) => {
                        let received = &buf[..n];
                        println!(
                            "RX {} bytes: {:?}",
                            n,
                            String::from_utf8_lossy(received)
                        );
                        // Echo back
                        if let Err(e) = stream.write_all(received).await {
                            eprintln!("Write error: {}", e);
                            break;
                        }
                    }
                    Err(e) => {
                        eprintln!("Read error: {}", e);
                        break;
                    }
                }
            }
        });
    }
}
```

#### SPP Client in Rust

```rust
// src/spp_client.rs
use bluer::{
    rfcomm::{Stream, SocketAddr},
    Address, Session,
};
use std::error::Error;
use std::str::FromStr;
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::time::{sleep, Duration};

async fn discover_spp_channel(
    session: &Session,
    addr: Address,
) -> Result<u8, Box<dyn Error>> {
    // Connect to the remote device and query its SDP records
    // for SPP service UUID 0x1101
    let adapter = session.default_adapter().await?;
    let device = adapter.device(addr)?;

    // Ensure we have the service records (may trigger SDP browse)
    let uuids = device.uuids().await?.unwrap_or_default();
    let spp_uuid = bluer::Uuid::from_u16(0x1101);

    if uuids.contains(&spp_uuid) {
        // bluer does not yet expose SDP channel extraction directly;
        // use channel 1 as the conventional default for SPP.
        // In production, parse the SDP records via the D-Bus interface.
        println!("SPP UUID found; using channel 1 (default)");
        Ok(1)
    } else {
        Err("SPP service not found on device".into())
    }
}

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    env_logger::init();

    // Replace with actual target Bluetooth address
    let target: Address = Address::from_str("00:11:22:33:44:55")?;
    let channel: u8 = 1;

    println!("Connecting to {} on RFCOMM channel {}", target, channel);

    // Retry loop for robustness
    let mut attempts = 0u32;
    let stream = loop {
        attempts += 1;
        let addr = SocketAddr::new(target, channel);
        match Stream::connect(addr).await {
            Ok(s) => {
                println!("Connected after {} attempt(s)", attempts);
                break s;
            }
            Err(e) if attempts < 5 => {
                eprintln!("Attempt {} failed: {}. Retrying...", attempts, e);
                sleep(Duration::from_secs(1 << attempts)).await; // exponential backoff
            }
            Err(e) => return Err(e.into()),
        }
    };

    run_session(stream).await
}

async fn run_session(mut stream: Stream) -> Result<(), Box<dyn Error>> {
    // Send an initial greeting
    stream.write_all(b"Hello from Rust SPP client!\r\n").await?;

    let mut buf = [0u8; 256];
    loop {
        tokio::select! {
            result = stream.read(&mut buf) => {
                match result? {
                    0 => { println!("Server closed connection"); break; }
                    n => {
                        println!("RX: {}", String::from_utf8_lossy(&buf[..n]));
                    }
                }
            }
        }
    }
    Ok(())
}
```

#### SPP UART Bridge in Rust (tokio + bluer + serialport)

This example bridges an RFCOMM Bluetooth stream to a real hardware UART (serial port), combining `bluer` with the `serialport` crate.

```toml
[dependencies]
bluer      = { version = "0.17", features = ["full"] }
tokio      = { version = "1",   features = ["full"] }
serialport = "4"
tokio-serial = "5"
```

```rust
// src/bt_uart_bridge.rs
use bluer::rfcomm::{Listener, SocketAddr};
use bluer::Address;
use std::error::Error;
use tokio::io::{AsyncReadExt, AsyncWriteExt, split};
use tokio_serial::SerialPortBuilderExt;

const BT_CHANNEL:  u8  = 1;
const SERIAL_PORT: &str = "/dev/ttyUSB0";
const SERIAL_BAUD: u32 = 115200;

#[tokio::main]
async fn main() -> Result<(), Box<dyn Error>> {
    let listener =
        Listener::bind(SocketAddr::new(Address::any(), BT_CHANNEL)).await?;
    println!("Waiting for SPP client on channel {}...", BT_CHANNEL);

    let (mut bt_stream, peer) = listener.accept().await?;
    println!("BT connected: {}", peer.addr);

    // Open the hardware serial port (async via tokio-serial)
    let serial = tokio_serial::new(SERIAL_PORT, SERIAL_BAUD).open_native_async()?;
    println!("Serial port {} opened at {} baud", SERIAL_PORT, SERIAL_BAUD);

    let (mut serial_rx, mut serial_tx) = split(serial);
    let (mut bt_rx, mut bt_tx) = split(bt_stream);

    let bt_to_serial = tokio::spawn(async move {
        let mut buf = [0u8; 1024];
        loop {
            match bt_rx.read(&mut buf).await {
                Ok(0) | Err(_) => break,
                Ok(n) => { let _ = serial_tx.write_all(&buf[..n]).await; }
            }
        }
    });

    let serial_to_bt = tokio::spawn(async move {
        let mut buf = [0u8; 1024];
        loop {
            match serial_rx.read(&mut buf).await {
                Ok(0) | Err(_) => break,
                Ok(n) => { let _ = bt_tx.write_all(&buf[..n]).await; }
            }
        }
    });

    tokio::select! {
        _ = bt_to_serial  => println!("BT→Serial task ended"),
        _ = serial_to_bt  => println!("Serial→BT task ended"),
    }
    println!("Bridge session finished");
    Ok(())
}
```

---

## RFCOMM Channel Management

RFCOMM channels 1–30 may be occupied by other system services. Recommended strategy:

```c
/* C: Scan for a free RFCOMM channel by attempting binds */
#include <bluetooth/rfcomm.h>
#include <sys/socket.h>

int find_free_rfcomm_channel(void) {
    for (int ch = 1; ch <= 30; ch++) {
        int s = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
        struct sockaddr_rc addr = {
            .rc_family  = AF_BLUETOOTH,
            .rc_bdaddr  = *BDADDR_ANY,
            .rc_channel = (uint8_t)ch,
        };
        int ok = bind(s, (struct sockaddr *)&addr, sizeof(addr));
        close(s);
        if (ok == 0) return ch;
    }
    return -1; /* No free channel found */
}
```

```rust
// Rust: Try channels in sequence until bind succeeds
async fn find_free_channel() -> Option<u8> {
    use bluer::rfcomm::{Listener, SocketAddr};
    use bluer::Address;

    for ch in 1u8..=30 {
        let addr = SocketAddr::new(Address::any(), ch);
        if Listener::bind(addr).await.is_ok() {
            return Some(ch);
        }
    }
    None
}
```

---

## Error Handling and Reconnection

SPP connections can drop due to range, interference, or power events. A robust application implements reconnection with exponential backoff:

```c
/* C reconnection loop */
#define MAX_RETRIES    10
#define INITIAL_DELAY  1   /* seconds */

int connect_with_retry(int sock,
                        const struct sockaddr *addr,
                        socklen_t len) {
    int delay = INITIAL_DELAY;
    for (int i = 0; i < MAX_RETRIES; i++) {
        if (connect(sock, addr, len) == 0) return 0;
        fprintf(stderr, "Retry %d/%d in %d s: %s\n",
                i+1, MAX_RETRIES, delay, strerror(errno));
        sleep((unsigned)delay);
        delay = (delay < 60) ? delay * 2 : 60; /* cap at 60 s */
    }
    return -1;
}
```

```rust
// Rust reconnection with exponential backoff
use tokio::time::{sleep, Duration};

async fn connect_with_backoff(addr: bluer::rfcomm::SocketAddr)
    -> Result<bluer::rfcomm::Stream, Box<dyn std::error::Error>>
{
    let mut delay = Duration::from_secs(1);
    for attempt in 1..=10 {
        match bluer::rfcomm::Stream::connect(addr).await {
            Ok(s) => return Ok(s),
            Err(e) => {
                eprintln!("Attempt {}: {}. Retrying in {:?}", attempt, e, delay);
                sleep(delay).await;
                delay = (delay * 2).min(Duration::from_secs(60));
            }
        }
    }
    Err("Max reconnection attempts exceeded".into())
}
```

---

## Security Considerations

| Concern                  | Recommendation                                               |
|--------------------------|--------------------------------------------------------------|
| **Pairing**              | Use SSP (Secure Simple Pairing) with Numeric Comparison      |
| **Authentication**       | Set `ESP_SPP_SEC_AUTHENTICATE` or BlueZ auth requirement     |
| **Encryption**           | Ensure link-level encryption is enabled (Bluetooth mandates it post-pairing) |
| **Channel exposure**     | Only advertise SPP in SDP when a connection is expected      |
| **Discoverability**      | Set device non-discoverable after pairing                    |
| **PIN codes**            | Avoid legacy fixed PINs (e.g., "1234"); use SSP instead      |
| **Data validation**      | Never trust incoming serial data blindly — validate/sanitize |

---

## Common Pitfalls

**1. Forgetting to register an SDP record**
Without an SDP record advertising UUID `0x1101`, clients cannot discover the service programmatically; they must hardcode the channel number.

**2. Using BLE APIs for SPP**
SPP requires **Bluetooth Classic**, not BLE. Many modern mobile APIs default to BLE. On Android, use `BluetoothAdapter.getRemoteDevice()` + `createRfcommSocketToServiceRecord()` with the SPP UUID.

**3. Blocking reads on the main thread**
RFCOMM sockets are blocking by default. Use threads, `select()`/`poll()`, or async I/O to avoid deadlocks.

**4. Not handling partial reads/writes**
Like all POSIX byte-stream sockets, RFCOMM `read()`/`write()` may return fewer bytes than requested. Always loop until the full message is consumed.

```c
/* Robust write loop */
ssize_t write_all(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    size_t remaining = n;
    while (remaining > 0) {
        ssize_t written = write(fd, p, remaining);
        if (written <= 0) return written; /* error or EOF */
        p         += written;
        remaining -= (size_t)written;
    }
    return (ssize_t)n;
}
```

**5. Channel conflicts with system services**
On Linux, channel 1 is often reserved. Use `sdptool browse local` to check which channels are in use and select a free one dynamically.

**6. Missing `bluetoothd` permissions**
Running SPP server code as a non-root user requires appropriate D-Bus policy or membership in the `bluetooth` group:
```bash
sudo usermod -aG bluetooth $USER
```

---

## Summary

The **Bluetooth Serial Port Profile (SPP)** provides a transparent wireless byte stream between devices, directly analogous to a physical UART connection. It abstracts the underlying RFCOMM/L2CAP/Baseband Bluetooth stack into a simple socket or file-descriptor interface, making it the natural choice for cable-replacement and UART-over-wireless applications.

**Key takeaways:**

- SPP operates over **RFCOMM**, which emulates RS-232 over L2CAP. The API surface is virtually identical to standard POSIX TCP sockets or COM port I/O.
- **SDP registration** (UUID `0x1101`) allows clients to discover the server dynamically, avoiding hardcoded channel numbers.
- On **Linux (BlueZ)**, use raw `AF_BLUETOOTH`/`BTPROTO_RFCOMM` sockets or the higher-level `bluer` D-Bus API.
- On **Windows**, use `WinSock2` with `AF_BTH`/`BTHPROTO_RFCOMM`.
- On **ESP32 (ESP-IDF)**, the `esp_spp_api.h` component provides a callback-driven event model and can bridge directly to hardware UART with minimal code.
- In **Rust**, the `bluer` crate (async/tokio) provides idiomatic RFCOMM stream support on Linux and handles SDP and connection management cleanly.
- Always implement **reconnection logic** with exponential backoff, **loop all reads/writes** to handle partial transfers, and **validate incoming data** before acting on it.
- SPP is a **Bluetooth Classic** profile — it is not available on BLE-only devices and must not be confused with BLE UART services (NUS, custom GATT characteristics).

---

*Document: 81 — Bluetooth Serial Port Profile | Wireless UART over Bluetooth SPP*