# 57. Windows COM Port API

**Structure:**
- **Core Concepts** — handle types, DCB, COMMTIMEOUTS, OVERLAPPED, COMSTAT
- **Opening & Configuring** — `CreateFile` with `FILE_FLAG_OVERLAPPED`, the `\\\\.\\COMx` notation for all port numbers
- **DCB Configuration** — baud rate, parity, stop bits, flow control in C/C++
- **Timeouts** — how `ReadIntervalTimeout`, `ReadTotalTimeoutMultiplier`, and `ReadTotalTimeoutConstant` interact
- **Synchronous I/O** — simple blocking `ReadFile`/`WriteFile`
- **Overlapped I/O** — full async read/write using `OVERLAPPED` structs, `CreateEvent`, `WaitForSingleObject`, and `GetOverlappedResult`
- **Event-Driven Communication** — `WaitCommEvent` / `SetCommMask` with all comm event flags
- **Flow Control** — both hardware (RTS/CTS) and software (XON/XOFF) variants
- **Error Handling** — `ClearCommError`, `COMSTAT`, error flag bitmask interpretation
- **Full C/C++ Example** — self-contained program with overlapped I/O and a dedicated reader thread
- **Rust Implementation** — both high-level (`serialport` crate) and raw Windows API (`windows` crate) versions
- **Comparison table** — Synchronous vs. Overlapped I/O tradeoffs
- **Best Practices** — 12 practical rules for robust serial applications
- **Summary** — architectural overview and key takeaways

## Serial Port Programming with Windows API and Overlapped I/O

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Opening and Configuring a COM Port](#opening-and-configuring-a-com-port)
4. [DCB – Device Control Block](#dcb--device-control-block)
5. [Timeouts](#timeouts)
6. [Synchronous I/O](#synchronous-io)
7. [Overlapped (Asynchronous) I/O](#overlapped-asynchronous-io)
8. [Event-Driven Communication](#event-driven-communication)
9. [Flow Control](#flow-control)
10. [Error Handling](#error-handling)
11. [C/C++ Full Example – Overlapped I/O](#cc-full-example--overlapped-io)
12. [Rust Implementation](#rust-implementation)
13. [Comparing Synchronous vs. Overlapped I/O](#comparing-synchronous-vs-overlapped-io)
14. [Best Practices](#best-practices)
15. [Summary](#summary)

---

## Introduction

The **Windows COM Port API** provides a comprehensive interface for communicating with serial (UART) devices using the Win32 API. Serial ports are exposed as file-like objects under Windows and can be opened, read, written, and closed using standard file I/O functions — with some serial-specific extensions.

Serial ports are identified by names such as `COM1`, `COM2`, ..., `COM9`. For ports with numbers 10 and above, the extended device path notation must be used: `\\.\COM10`, `\\.\COM22`, etc.

Windows supports two fundamental I/O modes for COM ports:

- **Synchronous I/O**: Blocking calls — the thread waits until the operation completes.
- **Overlapped (Asynchronous) I/O**: Non-blocking calls — the operation is initiated and the thread is notified on completion via events or callbacks.

Overlapped I/O is the preferred model in production applications, enabling responsive UIs and scalable multi-device handling.

---

## Core Concepts

| Concept | Description |
|---|---|
| `HANDLE` | Opaque Win32 handle returned by `CreateFile` |
| `DCB` | Device Control Block — holds baud rate, parity, stop bits, etc. |
| `COMMTIMEOUTS` | Controls read/write timeout behavior |
| `OVERLAPPED` | Structure used for asynchronous I/O operations |
| `COMSTAT` | Reports current status of the COM port buffer |
| `DWORD dwErrors` | Bitmask of COM port errors cleared by `ClearCommError` |

---

## Opening and Configuring a COM Port

### C/C++

```cpp
#include <windows.h>
#include <stdio.h>

HANDLE OpenSerialPort(const char* portName) {
    // Use \\.\COMx notation to support all port numbers
    char fullPath[64];
    snprintf(fullPath, sizeof(fullPath), "\\\\.\\%s", portName);

    HANDLE hPort = CreateFileA(
        fullPath,
        GENERIC_READ | GENERIC_WRITE,   // read+write access
        0,                              // no sharing
        NULL,                           // default security
        OPEN_EXISTING,                  // must already exist
        FILE_FLAG_OVERLAPPED,           // enable overlapped (async) I/O
        NULL                            // no template
    );

    if (hPort == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Failed to open %s: error %lu\n", portName, GetLastError());
        return INVALID_HANDLE_VALUE;
    }

    // Purge any stale data from buffers
    PurgeComm(hPort, PURGE_RXCLEAR | PURGE_TXCLEAR);

    return hPort;
}
```

**Key flags for `CreateFile`:**

- `FILE_FLAG_OVERLAPPED` — required for overlapped I/O; if omitted, all operations are synchronous.
- `OPEN_EXISTING` — COM ports must already exist; `CREATE_NEW` or `CREATE_ALWAYS` are invalid here.
- Sharing mode must be `0` — exclusive access is required for COM ports.

---

## DCB – Device Control Block

The `DCB` (Device Control Block) structure configures all UART parameters: baud rate, data bits, parity, stop bits, and flow control.

### C/C++

```cpp
bool ConfigureSerialPort(HANDLE hPort, DWORD baudRate) {
    DCB dcb = {0};
    dcb.DCBlength = sizeof(DCB);

    // Retrieve current settings first
    if (!GetCommState(hPort, &dcb)) {
        fprintf(stderr, "GetCommState failed: %lu\n", GetLastError());
        return false;
    }

    // Configure UART parameters
    dcb.BaudRate = baudRate;        // e.g., CBR_9600, CBR_115200
    dcb.ByteSize = 8;               // 8 data bits
    dcb.Parity   = NOPARITY;        // no parity (EVENPARITY, ODDPARITY, etc.)
    dcb.StopBits = ONESTOPBIT;      // 1 stop bit (ONE5STOPBITS, TWOSTOPBITS)

    // Flow control — disabled
    dcb.fOutxCtsFlow = FALSE;       // no CTS output flow control
    dcb.fOutxDsrFlow = FALSE;       // no DSR output flow control
    dcb.fDtrControl  = DTR_CONTROL_ENABLE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    dcb.fOutX        = FALSE;       // no XON/XOFF output
    dcb.fInX         = FALSE;       // no XON/XOFF input

    if (!SetCommState(hPort, &dcb)) {
        fprintf(stderr, "SetCommState failed: %lu\n", GetLastError());
        return false;
    }

    return true;
}
```

### Common Baud Rate Constants

| Constant | Value |
|---|---|
| `CBR_9600` | 9600 |
| `CBR_19200` | 19200 |
| `CBR_38400` | 38400 |
| `CBR_57600` | 57600 |
| `CBR_115200` | 115200 |
| `CBR_230400` | 230400 |

---

## Timeouts

`COMMTIMEOUTS` controls how read and write operations behave when data is not immediately available.

```cpp
bool SetSerialTimeouts(HANDLE hPort) {
    COMMTIMEOUTS timeouts = {0};

    // ReadIntervalTimeout: max time between received bytes (ms)
    // If set to MAXDWORD with ReadTotalTimeoutMultiplier=0 and
    // ReadTotalTimeoutConstant=0, ReadFile returns immediately with
    // whatever is in the buffer (non-blocking read).
    timeouts.ReadIntervalTimeout         = 50;   // 50 ms between chars
    timeouts.ReadTotalTimeoutMultiplier  = 10;   // 10 ms per byte
    timeouts.ReadTotalTimeoutConstant    = 100;  // 100 ms constant overhead

    // WriteTotalTimeoutMultiplier: ms per byte for writes
    timeouts.WriteTotalTimeoutMultiplier = 10;
    timeouts.WriteTotalTimeoutConstant   = 100;

    if (!SetCommTimeouts(hPort, &timeouts)) {
        fprintf(stderr, "SetCommTimeouts failed: %lu\n", GetLastError());
        return false;
    }

    return true;
}
```

### Timeout Calculation

The total read timeout for N bytes is computed as:

```
Total = ReadTotalTimeoutMultiplier * N + ReadTotalTimeoutConstant
```

Setting all fields to `0` disables timeouts entirely (infinite wait).

---

## Synchronous I/O

Synchronous I/O is straightforward but blocks the calling thread for the duration of the operation. It is suitable for simple tools and scripts, but not for GUI or multi-device applications.

> **Note:** The port must be opened **without** `FILE_FLAG_OVERLAPPED` for purely synchronous operation.

```cpp
// Synchronous write
bool SyncWrite(HANDLE hPort, const BYTE* data, DWORD len) {
    DWORD bytesWritten = 0;
    if (!WriteFile(hPort, data, len, &bytesWritten, NULL)) {
        fprintf(stderr, "WriteFile failed: %lu\n", GetLastError());
        return false;
    }
    return bytesWritten == len;
}

// Synchronous read
DWORD SyncRead(HANDLE hPort, BYTE* buf, DWORD maxLen) {
    DWORD bytesRead = 0;
    if (!ReadFile(hPort, buf, maxLen, &bytesRead, NULL)) {
        fprintf(stderr, "ReadFile failed: %lu\n", GetLastError());
        return 0;
    }
    return bytesRead;
}
```

---

## Overlapped (Asynchronous) I/O

Overlapped I/O allows the calling thread to initiate an I/O operation and then continue doing other work. Completion is signaled via an event object embedded in the `OVERLAPPED` structure.

### How It Works

1. Create a manual-reset event with `CreateEvent`.
2. Embed the event handle in an `OVERLAPPED` structure.
3. Call `ReadFile` or `WriteFile` with the `OVERLAPPED` pointer.
4. If the call returns `FALSE` and `GetLastError()` is `ERROR_IO_PENDING`, the I/O is in progress.
5. Wait for the event using `WaitForSingleObject` or `WaitForMultipleObjects`.
6. Retrieve the result with `GetOverlappedResult`.

### C/C++ – Overlapped Write

```cpp
bool AsyncWrite(HANDLE hPort, const BYTE* data, DWORD len) {
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset, initially non-signaled

    if (!ov.hEvent) return false;

    DWORD bytesWritten = 0;
    BOOL result = WriteFile(hPort, data, len, &bytesWritten, &ov);

    if (!result) {
        if (GetLastError() != ERROR_IO_PENDING) {
            fprintf(stderr, "WriteFile failed immediately: %lu\n", GetLastError());
            CloseHandle(ov.hEvent);
            return false;
        }

        // Wait for completion (5 second timeout)
        DWORD waitResult = WaitForSingleObject(ov.hEvent, 5000);
        if (waitResult != WAIT_OBJECT_0) {
            fprintf(stderr, "Write timeout or error\n");
            CancelIo(hPort);
            CloseHandle(ov.hEvent);
            return false;
        }

        if (!GetOverlappedResult(hPort, &ov, &bytesWritten, FALSE)) {
            fprintf(stderr, "GetOverlappedResult failed: %lu\n", GetLastError());
            CloseHandle(ov.hEvent);
            return false;
        }
    }

    CloseHandle(ov.hEvent);
    return bytesWritten == len;
}
```

### C/C++ – Overlapped Read

```cpp
DWORD AsyncRead(HANDLE hPort, BYTE* buf, DWORD maxLen, DWORD timeoutMs) {
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (!ov.hEvent) return 0;

    DWORD bytesRead = 0;
    BOOL result = ReadFile(hPort, buf, maxLen, &bytesRead, &ov);

    if (!result) {
        if (GetLastError() != ERROR_IO_PENDING) {
            fprintf(stderr, "ReadFile failed: %lu\n", GetLastError());
            CloseHandle(ov.hEvent);
            return 0;
        }

        DWORD waitResult = WaitForSingleObject(ov.hEvent, timeoutMs);
        if (waitResult == WAIT_TIMEOUT) {
            CancelIo(hPort);  // cancel the pending I/O
            // Wait for cancellation to complete
            GetOverlappedResult(hPort, &ov, &bytesRead, TRUE);
            CloseHandle(ov.hEvent);
            return bytesRead; // return partial data if any
        } else if (waitResult != WAIT_OBJECT_0) {
            CloseHandle(ov.hEvent);
            return 0;
        }

        if (!GetOverlappedResult(hPort, &ov, &bytesRead, FALSE)) {
            if (GetLastError() != ERROR_OPERATION_ABORTED) {
                fprintf(stderr, "GetOverlappedResult failed: %lu\n", GetLastError());
            }
            CloseHandle(ov.hEvent);
            return 0;
        }
    }

    CloseHandle(ov.hEvent);
    return bytesRead;
}
```

---

## Event-Driven Communication

Windows provides `WaitCommEvent` to wait asynchronously for specific serial port events, such as the arrival of a character in the receive buffer.

### Setting Up Comm Events

```cpp
bool SetupCommEvents(HANDLE hPort) {
    // Listen for: character received, line status error, CTS changed, DSR changed
    if (!SetCommMask(hPort, EV_RXCHAR | EV_ERR | EV_CTS | EV_DSR)) {
        fprintf(stderr, "SetCommMask failed: %lu\n", GetLastError());
        return false;
    }
    return true;
}

// Run in a dedicated reader thread
DWORD WINAPI CommEventThread(LPVOID param) {
    HANDLE hPort = (HANDLE)param;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

    DWORD eventMask = 0;

    while (true) {
        ResetEvent(ov.hEvent);
        eventMask = 0;

        if (!WaitCommEvent(hPort, &eventMask, &ov)) {
            if (GetLastError() != ERROR_IO_PENDING) break;

            DWORD wait = WaitForSingleObject(ov.hEvent, INFINITE);
            if (wait != WAIT_OBJECT_0) break;

            DWORD transferred = 0;
            if (!GetOverlappedResult(hPort, &ov, &transferred, FALSE)) break;
        }

        if (eventMask & EV_RXCHAR) {
            // Data available — read it
            BYTE buf[256];
            DWORD bytesRead = AsyncRead(hPort, buf, sizeof(buf), 500);
            if (bytesRead > 0) {
                // Process buf[0..bytesRead-1]
                printf("Received %lu bytes\n", bytesRead);
            }
        }

        if (eventMask & EV_ERR) {
            DWORD errors = 0;
            COMSTAT cs = {0};
            ClearCommError(hPort, &errors, &cs);
            fprintf(stderr, "COM error flags: 0x%08lX\n", errors);
        }
    }

    CloseHandle(ov.hEvent);
    return 0;
}
```

### Common Comm Event Flags

| Flag | Meaning |
|---|---|
| `EV_RXCHAR` | Character received and placed in the input buffer |
| `EV_RXFLAG` | Event character received (set in DCB) |
| `EV_TXEMPTY` | Output buffer empty |
| `EV_CTS` | CTS signal changed state |
| `EV_DSR` | DSR signal changed state |
| `EV_RLSD` | RLSD (DCD) signal changed state |
| `EV_BREAK` | Break detected on input |
| `EV_ERR` | Line status error (CE_FRAME, CE_OVERRUN, CE_RXPARITY) |
| `EV_RING` | Ring indicator detected |

---

## Flow Control

### Hardware Flow Control (RTS/CTS)

```cpp
void EnableHardwareFlowControl(DCB& dcb) {
    dcb.fOutxCtsFlow = TRUE;                  // output halts when CTS is low
    dcb.fRtsControl  = RTS_CONTROL_HANDSHAKE; // RTS controlled by driver
    dcb.fOutxDsrFlow = FALSE;
    dcb.fOutX        = FALSE;
    dcb.fInX         = FALSE;
}
```

### Software Flow Control (XON/XOFF)

```cpp
void EnableSoftwareFlowControl(DCB& dcb) {
    dcb.fOutX   = TRUE;    // XON/XOFF on output
    dcb.fInX    = TRUE;    // XON/XOFF on input
    dcb.XonChar  = 0x11;   // DC1 = XON
    dcb.XoffChar = 0x13;   // DC3 = XOFF
    dcb.XonLim   = 100;    // send XON when buffer drops below this level
    dcb.XoffLim  = 100;    // send XOFF when buffer exceeds (size - XoffLim)
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
}
```

---

## Error Handling

`ClearCommError` is the primary function for diagnosing COM port errors. It must be called whenever the port enters an error state; without it, further I/O may be blocked.

```cpp
void HandleCommError(HANDLE hPort) {
    DWORD errors = 0;
    COMSTAT cs   = {0};

    if (!ClearCommError(hPort, &errors, &cs)) {
        fprintf(stderr, "ClearCommError itself failed: %lu\n", GetLastError());
        return;
    }

    if (errors & CE_RXOVER)   fprintf(stderr, "Error: Receive buffer overflow\n");
    if (errors & CE_OVERRUN)  fprintf(stderr, "Error: Byte overrun\n");
    if (errors & CE_RXPARITY) fprintf(stderr, "Error: Parity error\n");
    if (errors & CE_FRAME)    fprintf(stderr, "Error: Framing error\n");
    if (errors & CE_BREAK)    fprintf(stderr, "Error: Break condition\n");

    printf("RX buffer bytes: %lu | TX buffer bytes: %lu\n",
           cs.cbInQue, cs.cbOutQue);
}
```

---

## C/C++ Full Example – Overlapped I/O

This self-contained example opens a COM port, configures it, sends a command, and reads a response using overlapped I/O with a dedicated read thread.

```cpp
#include <windows.h>
#include <stdio.h>
#include <string.h>

//-----------------------------------------------------------------------------
// Global port handle
//-----------------------------------------------------------------------------
static HANDLE g_hPort = INVALID_HANDLE_VALUE;

//-----------------------------------------------------------------------------
// Open the port
//-----------------------------------------------------------------------------
HANDLE OpenPort(const char* name, DWORD baud) {
    char path[32];
    snprintf(path, sizeof(path), "\\\\.\\%s", name);

    HANDLE h = CreateFileA(path,
        GENERIC_READ | GENERIC_WRITE, 0, NULL,
        OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "Cannot open %s (err=%lu)\n", name, GetLastError());
        return h;
    }

    // Configure DCB
    DCB dcb = {0};
    dcb.DCBlength = sizeof(dcb);
    GetCommState(h, &dcb);
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity   = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fOutxCtsFlow = FALSE;
    dcb.fRtsControl  = RTS_CONTROL_ENABLE;
    SetCommState(h, &dcb);

    // Timeouts
    COMMTIMEOUTS to = {0};
    to.ReadIntervalTimeout        = 50;
    to.ReadTotalTimeoutMultiplier = 10;
    to.ReadTotalTimeoutConstant   = 200;
    to.WriteTotalTimeoutConstant  = 500;
    SetCommTimeouts(h, &to);

    // Events: receive character or error
    SetCommMask(h, EV_RXCHAR | EV_ERR);
    PurgeComm(h, PURGE_RXCLEAR | PURGE_TXCLEAR);

    printf("Opened %s @ %lu baud\n", name, baud);
    return h;
}

//-----------------------------------------------------------------------------
// Overlapped write
//-----------------------------------------------------------------------------
BOOL WritePort(HANDLE h, const BYTE* data, DWORD len) {
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD written = 0;

    if (!WriteFile(h, data, len, &written, &ov)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(ov.hEvent, 2000);
            GetOverlappedResult(h, &ov, &written, FALSE);
        }
    }
    CloseHandle(ov.hEvent);
    return (written == len);
}

//-----------------------------------------------------------------------------
// Overlapped read with timeout
//-----------------------------------------------------------------------------
DWORD ReadPort(HANDLE h, BYTE* buf, DWORD maxLen, DWORD timeoutMs) {
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD bytesRead = 0;

    if (!ReadFile(h, buf, maxLen, &bytesRead, &ov)) {
        if (GetLastError() == ERROR_IO_PENDING) {
            DWORD w = WaitForSingleObject(ov.hEvent, timeoutMs);
            if (w == WAIT_TIMEOUT) {
                CancelIo(h);
                GetOverlappedResult(h, &ov, &bytesRead, TRUE);
            } else {
                GetOverlappedResult(h, &ov, &bytesRead, FALSE);
            }
        }
    }
    CloseHandle(ov.hEvent);
    return bytesRead;
}

//-----------------------------------------------------------------------------
// Reader thread: waits for events, drains receive buffer
//-----------------------------------------------------------------------------
DWORD WINAPI ReaderThread(LPVOID param) {
    HANDLE h = (HANDLE)param;
    OVERLAPPED ov = {0};
    ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
    DWORD mask = 0;

    while (true) {
        ResetEvent(ov.hEvent);
        mask = 0;

        BOOL ok = WaitCommEvent(h, &mask, &ov);
        if (!ok && GetLastError() == ERROR_IO_PENDING) {
            DWORD w = WaitForSingleObject(ov.hEvent, INFINITE);
            if (w != WAIT_OBJECT_0) break;
            DWORD dummy = 0;
            GetOverlappedResult(h, &ov, &dummy, FALSE);
        }

        if (mask & EV_RXCHAR) {
            BYTE buf[512];
            DWORD n = ReadPort(h, buf, sizeof(buf) - 1, 300);
            if (n > 0) {
                buf[n] = '\0';
                printf("[RX %lu bytes]: %s\n", n, buf);
            }
        }

        if (mask & EV_ERR) {
            DWORD errs = 0; COMSTAT cs = {0};
            ClearCommError(h, &errs, &cs);
            fprintf(stderr, "[ERR 0x%08lX] rxq=%lu txq=%lu\n",
                    errs, cs.cbInQue, cs.cbOutQue);
        }
    }

    CloseHandle(ov.hEvent);
    return 0;
}

//-----------------------------------------------------------------------------
// main
//-----------------------------------------------------------------------------
int main(void) {
    g_hPort = OpenPort("COM3", CBR_115200);
    if (g_hPort == INVALID_HANDLE_VALUE) return 1;

    // Start the reader thread
    HANDLE hThread = CreateThread(NULL, 0, ReaderThread, g_hPort, 0, NULL);

    // Send a command
    const char* cmd = "AT\r\n";
    WritePort(g_hPort, (const BYTE*)cmd, (DWORD)strlen(cmd));
    printf("[TX]: %s", cmd);

    // Wait a moment for response
    Sleep(1000);

    // Cleanup
    SetCommMask(g_hPort, 0);   // unblock WaitCommEvent in reader thread
    WaitForSingleObject(hThread, 2000);
    CloseHandle(hThread);
    CloseHandle(g_hPort);
    return 0;
}
```

---

## Rust Implementation

Rust can interact with Windows COM ports at two levels:

- **Using the `serialport` crate** — high-level, cross-platform, recommended for most use cases.
- **Using raw `windows` / `winapi` crates** — full access to the Windows COM Port API, mimicking the C approach.

### Using the `serialport` Crate

Add to `Cargo.toml`:

```toml
[dependencies]
serialport = "4"
```

#### Basic Synchronous Example

```rust
use serialport::{SerialPort, DataBits, FlowControl, Parity, StopBits};
use std::io::{Read, Write};
use std::time::Duration;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // List available ports
    let ports = serialport::available_ports()?;
    println!("Available ports:");
    for p in &ports {
        println!("  {}", p.port_name);
    }

    // Open and configure COM3 at 115200 baud
    let mut port = serialport::new("COM3", 115_200)
        .data_bits(DataBits::Eight)
        .parity(Parity::None)
        .stop_bits(StopBits::One)
        .flow_control(FlowControl::None)
        .timeout(Duration::from_millis(500))
        .open()?;

    println!("Opened {} at {} baud", port.name().unwrap_or("?"), 115200);

    // Write a command
    let cmd = b"AT\r\n";
    port.write_all(cmd)?;
    port.flush()?;
    println!("[TX]: AT");

    // Read response (up to 256 bytes)
    let mut buf = vec![0u8; 256];
    match port.read(&mut buf) {
        Ok(n) => {
            let response = String::from_utf8_lossy(&buf[..n]);
            println!("[RX {} bytes]: {}", n, response.trim());
        }
        Err(e) => eprintln!("Read error: {}", e),
    }

    Ok(())
}
```

#### Threaded Reader with Channels

```rust
use serialport::SerialPort;
use std::io::Read;
use std::sync::{Arc, Mutex};
use std::thread;
use std::time::Duration;

fn spawn_reader(
    port: Arc<Mutex<Box<dyn SerialPort>>>,
    tx: std::sync::mpsc::Sender<Vec<u8>>,
) -> thread::JoinHandle<()> {
    thread::spawn(move || {
        let mut buf = [0u8; 512];
        loop {
            let result = {
                let mut p = port.lock().unwrap();
                p.read(&mut buf)
            };
            match result {
                Ok(0) => continue,
                Ok(n) => {
                    if tx.send(buf[..n].to_vec()).is_err() {
                        break; // receiver dropped, exit thread
                    }
                }
                Err(ref e) if e.kind() == std::io::ErrorKind::TimedOut => continue,
                Err(e) => {
                    eprintln!("Reader thread error: {}", e);
                    break;
                }
            }
        }
    })
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let port = serialport::new("COM3", 115_200)
        .timeout(Duration::from_millis(100))
        .open()?;

    let port = Arc::new(Mutex::new(port));
    let (tx, rx) = std::sync::mpsc::channel::<Vec<u8>>();

    let reader_handle = spawn_reader(Arc::clone(&port), tx);

    // Send a command
    {
        let mut p = port.lock().unwrap();
        p.write_all(b"AT\r\n")?;
        p.flush()?;
    }

    // Collect responses for 2 seconds
    let deadline = std::time::Instant::now() + Duration::from_secs(2);
    while std::time::Instant::now() < deadline {
        if let Ok(data) = rx.recv_timeout(Duration::from_millis(100)) {
            println!("[RX {} bytes]: {}", data.len(),
                     String::from_utf8_lossy(&data).trim());
        }
    }

    // Signal reader to stop (close the shared port)
    drop(port);
    let _ = reader_handle.join();

    Ok(())
}
```

### Using Raw Windows API in Rust

Add to `Cargo.toml`:

```toml
[dependencies]
windows = { version = "0.58", features = [
    "Win32_Devices_Communication",
    "Win32_Foundation",
    "Win32_Storage_FileSystem",
    "Win32_System_IO",
    "Win32_System_Threading",
] }
```

```rust
use windows::{
    core::PCSTR,
    Win32::{
        Devices::Communication::*,
        Foundation::*,
        Storage::FileSystem::*,
        System::IO::*,
        System::Threading::*,
    },
};
use std::ffi::CString;

fn open_com_port(name: &str, baud: u32) -> windows::core::Result<HANDLE> {
    let path = CString::new(format!("\\\\.\\{}", name)).unwrap();

    let handle = unsafe {
        CreateFileA(
            PCSTR(path.as_ptr() as *const u8),
            (GENERIC_READ | GENERIC_WRITE).0,
            FILE_SHARE_NONE,
            None,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            None,
        )?
    };

    // Configure DCB
    let mut dcb = DCB {
        DCBlength: std::mem::size_of::<DCB>() as u32,
        ..Default::default()
    };
    unsafe { GetCommState(handle, &mut dcb)? };
    dcb.BaudRate = baud;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    unsafe { SetCommState(handle, &dcb)? };

    // Configure timeouts
    let timeouts = COMMTIMEOUTS {
        ReadIntervalTimeout: 50,
        ReadTotalTimeoutMultiplier: 10,
        ReadTotalTimeoutConstant: 200,
        WriteTotalTimeoutMultiplier: 10,
        WriteTotalTimeoutConstant: 500,
    };
    unsafe { SetCommTimeouts(handle, &timeouts)? };

    // Purge buffers
    unsafe { PurgeComm(handle, PURGE_RXCLEAR | PURGE_TXCLEAR)? };

    Ok(handle)
}

fn overlapped_write(handle: HANDLE, data: &[u8]) -> windows::core::Result<u32> {
    let event = unsafe { CreateEventA(None, true, false, None)? };
    let mut ov = OVERLAPPED {
        hEvent: event,
        ..Default::default()
    };
    let mut written: u32 = 0;

    let ok = unsafe {
        WriteFile(handle, Some(data), Some(&mut written), Some(&mut ov)).is_ok()
    };

    if !ok {
        unsafe {
            WaitForSingleObject(event, 5000);
            GetOverlappedResult(handle, &ov, &mut written, false)?;
        }
    }

    unsafe { CloseHandle(event)? };
    Ok(written)
}

fn overlapped_read(
    handle: HANDLE,
    buf: &mut [u8],
    timeout_ms: u32,
) -> windows::core::Result<u32> {
    let event = unsafe { CreateEventA(None, true, false, None)? };
    let mut ov = OVERLAPPED {
        hEvent: event,
        ..Default::default()
    };
    let mut bytes_read: u32 = 0;

    let ok = unsafe {
        ReadFile(handle, Some(buf), Some(&mut bytes_read), Some(&mut ov)).is_ok()
    };

    if !ok {
        let wait = unsafe { WaitForSingleObject(event, timeout_ms) };
        if wait == WAIT_TIMEOUT {
            unsafe { CancelIo(handle)?; }
        }
        unsafe { GetOverlappedResult(handle, &ov, &mut bytes_read, true)?; }
    }

    unsafe { CloseHandle(event)? };
    Ok(bytes_read)
}

fn main() -> windows::core::Result<()> {
    let handle = open_com_port("COM3", 115200)?;
    println!("Port opened successfully");

    let cmd = b"AT\r\n";
    let written = overlapped_write(handle, cmd)?;
    println!("[TX {} bytes]: AT", written);

    let mut buf = vec![0u8; 256];
    let n = overlapped_read(handle, &mut buf, 1000)?;
    if n > 0 {
        println!("[RX {} bytes]: {}", n, String::from_utf8_lossy(&buf[..n as usize]).trim());
    }

    unsafe { CloseHandle(handle)? };
    Ok(())
}
```

---

## Comparing Synchronous vs. Overlapped I/O

| Aspect | Synchronous | Overlapped |
|---|---|---|
| `CreateFile` flag | (none) | `FILE_FLAG_OVERLAPPED` |
| Thread blocking | Blocks until complete | Returns immediately |
| `OVERLAPPED` struct | Not used (pass `NULL`) | Required |
| Event signaling | Not applicable | `OVERLAPPED.hEvent` |
| Cancellation | Not possible mid-call | `CancelIo` / `CancelIoEx` |
| GUI responsiveness | Requires separate thread | Native async, no extra thread needed |
| Complexity | Low | Medium |
| Best for | Scripts, simple tools | Production, multi-port, GUI apps |

---

## Best Practices

1. **Always use `\\\\.\\COMx` notation** — it works for all port numbers including COM10 and above.
2. **Open with `FILE_FLAG_OVERLAPPED`** — required for event-driven, non-blocking operation.
3. **Call `PurgeComm` after opening** — clears stale data from hardware and driver buffers.
4. **Always initialize `OVERLAPPED` to zero** — `OVERLAPPED ov = {0};` or memset before use.
5. **Check `ERROR_IO_PENDING` on failed `ReadFile`/`WriteFile`** — this is not a real error; it means the operation is in progress.
6. **Call `CancelIo` before closing the handle** — ensures pending overlapped operations are terminated cleanly.
7. **Call `ClearCommError` on `EV_ERR`** — without it, the port stays in an error state and further I/O is blocked.
8. **Separate read and write overlapped structures** — never share a single `OVERLAPPED` between concurrent reads and writes.
9. **Use a dedicated thread for `WaitCommEvent`** — this allows the main thread to remain responsive.
10. **In Rust, prefer the `serialport` crate** — it provides a safe, cross-platform abstraction with built-in timeout and error handling.
11. **Handle `WAIT_TIMEOUT` explicitly** — design the reader loop to handle partial reads and reconnection gracefully.
12. **Set `SetCommMask(h, 0)` before stopping** — this unblocks any pending `WaitCommEvent` call in reader threads, allowing clean shutdown.

---

## Summary

The **Windows COM Port API** provides fine-grained control over UART serial communication through Win32 file I/O functions extended with serial-specific configuration and event mechanisms.

**Key takeaways:**

- COM ports are opened with `CreateFile` and configured via `DCB` (baud rate, parity, data bits, stop bits) and `COMMTIMEOUTS`.
- **Synchronous I/O** is simple but blocks the calling thread; suitable only for single-threaded, simple tools.
- **Overlapped I/O** (using `FILE_FLAG_OVERLAPPED`, `OVERLAPPED` structures, and Win32 event handles) is the recommended model for responsive, production-grade applications. It enables non-blocking reads and writes, with completion signaled via `WaitForSingleObject` or `WaitForMultipleObjects`.
- **`WaitCommEvent`** provides efficient, interrupt-style notification when data arrives or a line-status event occurs, eliminating the need for polling.
- **Error handling** centers on `ClearCommError`, which both retrieves the error bitmask and resets the port so that I/O can continue.
- In **C/C++**, the full Win32 API is used directly. In **Rust**, the high-level `serialport` crate covers the vast majority of use cases, while the `windows` crate provides direct API access when low-level control is required.
- A robust architecture for a serial application typically combines: an overlapped read loop on a dedicated thread, `WaitCommEvent` for efficient event detection, channel-based communication back to the main thread, and explicit resource cleanup (`CancelIo`, `CloseHandle`) on shutdown.

This API gives developers complete control over UART communication on Windows — from simple AT command dialogs to high-throughput, multi-port industrial protocols.

---

*Document generated for the UART Programming Series — Topic 57.*