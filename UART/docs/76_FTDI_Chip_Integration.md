# 76. FTDI Chip Integration

**Chip Family Overview** — Comparison table of FT232R, FT232H, FT2232H, FT4232H, and FT230X covering channels, USB speed, MPSSE support, max baud rate, and EEPROM type.

**Hardware Architecture** — USB descriptors/VID-PID, internal dual FIFO buffering, CBUS pin function table, and signal level considerations.

**Driver Ecosystem** — VCP vs. D2XX vs. libftdi1 tradeoffs, and Python bindings (pyserial/pyftdi).

**C/C++ Code Examples:**
- Basic UART open/configure/read/write using D2XX (`ftd2xx.h`)
- Device enumeration (`FT_CreateDeviceInfoList`)
- EEPROM programming with `FT_EE_Program` (custom serial, description, CBUS modes)
- Full libftdi1 UART session with async transfers and bit-bang GPIO
- MPSSE SPI master initialization and full-duplex transfer
- C++ RAII wrapper class with `readUntil()` and timeout support

**Rust Code Examples:**
- Safe high-level `FtdiUart` struct using the `ftdi` crate with `read_until()` and `command()` helpers
- Raw MPSSE SPI master via `libftdi1-sys` FFI with JEDEC ID flash read example
- Async Tokio integration using `spawn_blocking` for non-blocking concurrent RX/TX

**Advanced Features** — CBUS GPIO control, Windows event-driven notification, and EEPROM user area access.

**Diagnostics & Platform Notes** — Linux driver unbinding, udev rules, macOS kext/dext differences, and embedded Linux tips.

## Using FT232, FT2232, and Other FTDI USB-UART Converters

---

## Table of Contents

1. [Introduction](#introduction)
2. [FTDI Chip Family Overview](#ftdi-chip-family-overview)
3. [Hardware Architecture](#hardware-architecture)
4. [Driver and Library Ecosystem](#driver-and-library-ecosystem)
5. [Programming Concepts](#programming-concepts)
6. [C/C++ Implementation](#cc-implementation)
7. [Rust Implementation](#rust-implementation)
8. [Advanced Features](#advanced-features)
9. [Error Handling and Diagnostics](#error-handling-and-diagnostics)
10. [Platform-Specific Considerations](#platform-specific-considerations)
11. [Summary](#summary)

---

## Introduction

FTDI (Future Technology Devices International) chips are the industry-standard solution for bridging USB and serial communication protocols. They are ubiquitous in embedded systems development, test equipment, industrial automation, and hobbyist electronics. Unlike simple UART-to-USB bridges, FTDI chips offer programmable latency, hardware flow control, MPSSE (Multi-Protocol Synchronous Serial Engine) for SPI/I2C/JTAG, GPIO control, and rich configuration options—all accessible through well-documented APIs.

The primary use case is converting 3.3V or 5V UART signals from a microcontroller or embedded system into USB CDC (Communications Device Class) data that a host PC can receive as a virtual COM port (VCP) or access directly through D2XX/libftdi APIs.

---

## FTDI Chip Family Overview

### FT232R / FT232RL
- **Interface:** Single UART channel
- **USB Speed:** Full Speed (12 Mbit/s)
- **Baud Rates:** 300 bps to 3 Mbps
- **Extras:** CBUS programmable I/O pins (4 pins), integrated EEPROM, clock output
- **Package:** SSOP-28, DIP-28 (breakout modules)
- **Typical Use:** Simple serial adapters, Arduino programming headers

### FT232H
- **Interface:** Single channel – UART or MPSSE (SPI/I2C/JTAG)
- **USB Speed:** High Speed (480 Mbit/s)
- **Baud Rates:** Up to 12 Mbps (with MPSSE, synchronous bit-bang)
- **Extras:** 8 GPIO pins via MPSSE, large 1 KB TX/RX buffers
- **Package:** LQFP-32
- **Typical Use:** High-speed logic analyzers, JTAG debuggers, SPI flash programmers

### FT2232H
- **Interface:** Dual channel (A and B) – each independently configurable as UART, MPSSE, or bit-bang
- **USB Speed:** High Speed (480 Mbit/s)
- **Extras:** Each channel has 16 I/O lines, 4 KB FIFO buffers
- **Typical Use:** Combined JTAG + UART (OpenOCD + console), dual SPI busses

### FT4232H
- **Interface:** Quad channel (A, B, C, D)
- **USB Speed:** High Speed (480 Mbit/s)
- **Channels A/B:** Support MPSSE; Channels C/D: UART or bit-bang only
- **Typical Use:** Multi-target JTAG, multiple UART consoles on a single USB connector

### FT230X / FT231X
- **Interface:** Single UART channel
- **USB Speed:** Full Speed
- **Baud Rates:** 300 bps to 3 Mbps
- **Package:** QFN-16 (very small), no separate EEPROM needed (OTP internal)
- **Typical Use:** Cost-sensitive embedded designs, USB charging port with data

### Comparison Table

| Chip     | Channels | USB Speed   | MPSSE | Max Baud  | GPIO Pins | EEPROM       |
|----------|----------|-------------|-------|-----------|-----------|--------------|
| FT232R   | 1        | Full Speed  | No    | 3 Mbps    | 4 (CBUS)  | Internal     |
| FT232H   | 1        | High Speed  | Yes   | 12 Mbps   | 8 (ADBUS) | External/Int |
| FT2232H  | 2        | High Speed  | Yes   | 12 Mbps   | 16        | External     |
| FT4232H  | 4        | High Speed  | A,B   | 12 Mbps   | 16        | External     |
| FT230X   | 1        | Full Speed  | No    | 3 Mbps    | 0         | OTP internal |

---

## Hardware Architecture

### USB Descriptors and VID/PID

Every FTDI device presents itself to the host with:
- **VID:** 0x0403 (FTDI's USB Vendor ID)
- **PID:** Chip-specific (e.g., 0x6001 for FT232R, 0x6010 for FT2232H, 0x6014 for FT232H)

These can be reprogrammed via the internal EEPROM using FT_PROG (Windows) or the `ftdi_eeprom` tool (Linux).

### Internal FIFO Buffers

FTDI chips implement a dual FIFO architecture:
- **RX FIFO:** From UART/serial interface → USB host
- **TX FIFO:** From USB host → UART/serial interface

Latency timer (default: 16 ms) controls when a partially-filled RX FIFO packet is flushed to the host. For low-latency applications, set this to 1 ms.

### CBUS Pin Functions (FT232R)

The 4 CBUS pins can be independently configured (via EEPROM) to serve as:
- `TXLED#` / `RXLED#` – TX/RX activity indicators
- `SLEEP#` – USB suspend indicator
- `CLK6`, `CLK12`, `CLK24`, `CLK48` – clock outputs
- `IOMODE` – bit-bang GPIO
- `PWREN#` – power enable after USB enumeration

### Signal Levels

FTDI chips operate at 3.3V I/O by default (FT232H, FT2232H, FT4232H). The FT232R has a VCC I/O pin that can be tied to 3.3V or 5V. Always verify voltage compatibility with the target device. Level shifters (e.g., TXS0108E) are needed for mixed 1.8V/3.3V/5V environments.

---

## Driver and Library Ecosystem

### VCP (Virtual COM Port) Driver

The VCP driver creates a standard serial port (`COMx` on Windows, `/dev/ttyUSBx` on Linux, `/dev/cu.usbserial-xxx` on macOS). Applications open the port using standard OS serial APIs (POSIX `termios`, Windows `CreateFile`/`SetCommState`).

Pros: Works with any serial terminal, standard POSIX/Win32 APIs, no special SDK needed.
Cons: Subject to OS scheduling latency, no access to FTDI-specific features (CBUS, MPSSE, latency timer).

### D2XX Direct Driver

FTDI's proprietary D2XX library (`ftd2xx.dll` / `libftd2xx.so`) provides direct USB access, bypassing the OS serial stack. It exposes FTDI-specific features:
- `FT_SetLatencyTimer` – tune buffer flush interval
- `FT_SetBitMode` – switch to bit-bang, MPSSE, MCU host bus emulation
- `FT_GetBitMode` – read GPIO state
- `FT_EE_Read` / `FT_EE_Program` – EEPROM access

### libftdi / libftdi1 (Open Source)

An open-source alternative to D2XX, built on libusb. Available on Linux, macOS, Windows. Supports all core D2XX operations. Preferred for open-source projects.

```
libftdi1 → libusb-1.0 → OS USB stack → FTDI hardware
```

### Python: pyserial / pyftdi

For scripting and rapid prototyping:
- `pyserial` – VCP access
- `pyftdi` – Pure-Python D2XX-equivalent using libusb, supports UART, SPI, I2C, GPIO, JTAG

---

## Programming Concepts

### Opening a Device

FTDI devices can be opened by:
1. **Index** – First, second, ... device (fragile if multiple devices)
2. **Serial Number** – Unique string burned in EEPROM (robust)
3. **Description** – Human-readable product string from EEPROM
4. **USB VID/PID + Location** – USB bus and port address

### Baud Rate Generation

FTDI chips use a fractional baud rate divisor. The base clock is 48 MHz (FT232R) or 120 MHz (FT232H/FT2232H/FT4232H). Not all baud rates are achievable exactly; the chip selects the closest supported rate. For standard rates (9600, 115200, 460800, 921600), the error is negligible.

### Flow Control Modes

| Mode        | Description                                           |
|-------------|-------------------------------------------------------|
| None        | No flow control                                       |
| RTS/CTS     | Hardware handshaking using RTS and CTS lines          |
| DTR/DSR     | Hardware handshaking using DTR and DSR lines          |
| XON/XOFF   | Software flow control using control characters 0x11/0x13|

### Latency Timer

The latency timer (1–255 ms, default 16 ms) defines the maximum time the chip waits before sending a partially-filled USB packet. Set it to 1 ms for interactive use (shell consoles, real-time protocols). Set it higher (e.g., 32 ms) for bulk data transfers to reduce USB overhead.

---

## C/C++ Implementation

### Using D2XX API (Windows / Linux / macOS)

The D2XX SDK provides `ftd2xx.h` and platform-specific binaries.

#### Basic UART Communication

```c
#include <stdio.h>
#include <string.h>
#include "ftd2xx.h"  // FTDI D2XX SDK header

/* ------------------------------------------------------------
 * Open device by serial number and configure UART
 * ------------------------------------------------------------ */
int ftdi_uart_open(const char *serial_number, FT_HANDLE *handle)
{
    FT_STATUS status;

    /* Open by serial number (most reliable method) */
    status = FT_OpenEx((PVOID)serial_number, FT_OPEN_BY_SERIAL_NUMBER, handle);
    if (status != FT_OK) {
        fprintf(stderr, "FT_OpenEx failed: %d\n", (int)status);
        return -1;
    }

    /* Reset device to known state */
    FT_ResetDevice(*handle);

    /* Set baud rate: 115200 bps */
    status = FT_SetBaudRate(*handle, 115200);
    if (status != FT_OK) { fprintf(stderr, "SetBaudRate failed\n"); return -1; }

    /* 8 data bits, 1 stop bit, no parity */
    status = FT_SetDataCharacteristics(*handle, FT_BITS_8, FT_STOP_BITS_1, FT_PARITY_NONE);
    if (status != FT_OK) { fprintf(stderr, "SetDataCharacteristics failed\n"); return -1; }

    /* No flow control */
    status = FT_SetFlowControl(*handle, FT_FLOW_NONE, 0x00, 0x00);
    if (status != FT_OK) { fprintf(stderr, "SetFlowControl failed\n"); return -1; }

    /* Set timeouts: 500 ms read, 500 ms write */
    FT_SetTimeouts(*handle, 500, 500);

    /* Set latency timer to 1 ms for low-latency interactive use */
    FT_SetLatencyTimer(*handle, 1);

    /* Clear any pending RX/TX data */
    FT_Purge(*handle, FT_PURGE_RX | FT_PURGE_TX);

    return 0;
}

/* ------------------------------------------------------------
 * Write data to UART
 * ------------------------------------------------------------ */
int ftdi_uart_write(FT_HANDLE handle, const uint8_t *data, DWORD length)
{
    DWORD bytes_written = 0;
    FT_STATUS status = FT_Write(handle, (LPVOID)data, length, &bytes_written);

    if (status != FT_OK) {
        fprintf(stderr, "FT_Write failed: %d\n", (int)status);
        return -1;
    }
    if (bytes_written != length) {
        fprintf(stderr, "Write incomplete: %lu/%lu bytes\n",
                (unsigned long)bytes_written, (unsigned long)length);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------
 * Read data from UART (blocking with timeout)
 * ------------------------------------------------------------ */
int ftdi_uart_read(FT_HANDLE handle, uint8_t *buffer, DWORD max_len, DWORD *received)
{
    DWORD bytes_available = 0;
    FT_STATUS status;

    /* Query how many bytes are in the RX FIFO */
    status = FT_GetQueueStatus(handle, &bytes_available);
    if (status != FT_OK) {
        fprintf(stderr, "FT_GetQueueStatus failed: %d\n", (int)status);
        return -1;
    }

    if (bytes_available == 0) {
        *received = 0;
        return 0;   /* No data available */
    }

    DWORD to_read = (bytes_available < max_len) ? bytes_available : max_len;

    status = FT_Read(handle, buffer, to_read, received);
    if (status != FT_OK) {
        fprintf(stderr, "FT_Read failed: %d\n", (int)status);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------
 * Enumerate all connected FTDI devices
 * ------------------------------------------------------------ */
void ftdi_list_devices(void)
{
    DWORD device_count = 0;
    FT_STATUS status = FT_CreateDeviceInfoList(&device_count);
    if (status != FT_OK || device_count == 0) {
        printf("No FTDI devices found.\n");
        return;
    }

    FT_DEVICE_LIST_INFO_NODE *info =
        (FT_DEVICE_LIST_INFO_NODE *)malloc(sizeof(FT_DEVICE_LIST_INFO_NODE) * device_count);

    FT_GetDeviceInfoList(info, &device_count);

    for (DWORD i = 0; i < device_count; i++) {
        printf("[%lu] Flags=0x%08lX Type=%lu ID=0x%08lX Serial='%s' Desc='%s'\n",
               (unsigned long)i,
               (unsigned long)info[i].Flags,
               (unsigned long)info[i].Type,
               (unsigned long)info[i].ID,
               info[i].SerialNumber,
               info[i].Description);
    }
    free(info);
}

/* ------------------------------------------------------------
 * Main: open device, send command, receive response
 * ------------------------------------------------------------ */
int main(void)
{
    FT_HANDLE handle;

    ftdi_list_devices();

    if (ftdi_uart_open("FT2SP123", &handle) != 0) {
        return 1;
    }

    const uint8_t cmd[] = "AT\r\n";
    ftdi_uart_write(handle, cmd, sizeof(cmd) - 1);

    uint8_t response[256];
    DWORD received = 0;

    /* Poll for response (up to 1 second) */
    for (int i = 0; i < 100; i++) {
        ftdi_uart_read(handle, response, sizeof(response), &received);
        if (received > 0) {
            response[received] = '\0';
            printf("Response: %s\n", response);
            break;
        }
        /* Sleep 10 ms between polls */
        /* On Linux: usleep(10000); on Windows: Sleep(10); */
    }

    FT_Close(handle);
    return 0;
}
```

#### EEPROM Programming (FT232R / FT2232H)

```c
#include "ftd2xx.h"
#include <string.h>
#include <stdio.h>

/* Reprogram EEPROM to set a custom product string and serial number */
int ftdi_program_eeprom(FT_HANDLE handle)
{
    FT_STATUS status;
    FT_PROGRAM_DATA ee_data;

    /* Read current EEPROM content first */
    char ManufacturerBuf[32], ManufacturerIdBuf[16];
    char DescriptionBuf[64], SerialNumberBuf[16];

    memset(&ee_data, 0, sizeof(ee_data));
    ee_data.Signature1    = 0x00000000;
    ee_data.Signature2    = 0xFFFFFFFF;
    ee_data.Version       = 2;   /* FT232R = version 2 */
    ee_data.Manufacturer  = ManufacturerBuf;
    ee_data.ManufacturerId = ManufacturerIdBuf;
    ee_data.Description   = DescriptionBuf;
    ee_data.SerialNumber  = SerialNumberBuf;

    status = FT_EE_Read(handle, &ee_data);
    if (status != FT_OK) {
        fprintf(stderr, "EEPROM read failed: %d\n", (int)status);
        return -1;
    }

    printf("Current VID=0x%04X PID=0x%04X Desc='%s' Serial='%s'\n",
           ee_data.VendorId, ee_data.ProductId,
           ee_data.Description, ee_data.SerialNumber);

    /* Modify fields */
    strncpy(DescriptionBuf, "My Custom UART Adapter", 63);
    strncpy(SerialNumberBuf, "MYCUST001", 15);
    ee_data.SelfPowered     = 0;   /* Bus powered */
    ee_data.MaxPower        = 90;  /* 90 mA (must be <= 500 for bus-powered) */
    ee_data.UseExtOsc       = 0;
    ee_data.InvertTXD       = 0;
    ee_data.InvertRXD       = 0;

    /* Cbus0 = TXLED#, Cbus1 = RXLED#, Cbus2 = SLEEP#, Cbus3 = PWREN# */
    ee_data.Cbus0 = 0x01;  /* TXLED# */
    ee_data.Cbus1 = 0x02;  /* RXLED# */
    ee_data.Cbus2 = 0x03;  /* SLEEP# */
    ee_data.Cbus3 = 0x04;  /* PWREN# */

    status = FT_EE_Program(handle, &ee_data);
    if (status != FT_OK) {
        fprintf(stderr, "EEPROM write failed: %d\n", (int)status);
        return -1;
    }

    /* Cycle the USB port so new descriptors are presented */
    FT_CyclePort(handle);
    printf("EEPROM programmed successfully. Re-plug device to apply changes.\n");
    return 0;
}
```

### Using libftdi1 (Cross-Platform Open-Source)

```c
/*
 * Compile: gcc -o ftdi_example ftdi_example.c $(pkg-config --cflags --libs libftdi1)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ftdi.h>   /* libftdi1 */

/* ------------------------------------------------------------
 * Open FTDI device and configure UART via libftdi1
 * ------------------------------------------------------------ */
struct ftdi_context *ftdi_open_uart(int vendor, int product,
                                    const char *description,
                                    const char *serial,
                                    int interface_index)
{
    struct ftdi_context *ftdi = ftdi_new();
    if (!ftdi) {
        fprintf(stderr, "ftdi_new() failed\n");
        return NULL;
    }

    /* Select interface channel for multi-channel chips (FT2232H/FT4232H)
     * INTERFACE_A = 0, INTERFACE_B = 1, INTERFACE_C = 2, INTERFACE_D = 3 */
    ftdi_set_interface(ftdi, (enum ftdi_interface)interface_index);

    int ret = ftdi_usb_open_desc(ftdi, vendor, product, description, serial);
    if (ret < 0) {
        fprintf(stderr, "ftdi_usb_open_desc failed: %s\n", ftdi_get_error_string(ftdi));
        ftdi_free(ftdi);
        return NULL;
    }

    /* Reset and purge buffers */
    ftdi_usb_reset(ftdi);
    ftdi_tcioflush(ftdi);  /* Purge TX and RX */

    /* Set baud rate */
    if (ftdi_set_baudrate(ftdi, 115200) < 0) {
        fprintf(stderr, "ftdi_set_baudrate: %s\n", ftdi_get_error_string(ftdi));
        ftdi_usb_close(ftdi);
        ftdi_free(ftdi);
        return NULL;
    }

    /* Set line discipline: 8N1 */
    if (ftdi_set_line_property(ftdi, BITS_8, STOP_BIT_1, NONE) < 0) {
        fprintf(stderr, "ftdi_set_line_property: %s\n", ftdi_get_error_string(ftdi));
        ftdi_usb_close(ftdi);
        ftdi_free(ftdi);
        return NULL;
    }

    /* Set latency timer to 1 ms */
    ftdi_set_latency_timer(ftdi, 1);

    /* Set USB chunk sizes for optimal throughput */
    ftdi_read_data_set_chunksize(ftdi, 65536);
    ftdi_write_data_set_chunksize(ftdi, 65536);

    return ftdi;
}

/* ------------------------------------------------------------
 * Asynchronous (non-blocking) write example
 * ------------------------------------------------------------ */
int ftdi_async_write_example(struct ftdi_context *ftdi)
{
    const char payload[] = "Hello from libftdi async!\n";
    struct ftdi_transfer_control *tc;

    /* Submit transfer; returns immediately */
    tc = ftdi_write_data_submit(ftdi, (unsigned char*)payload, strlen(payload));
    if (!tc) {
        fprintf(stderr, "ftdi_write_data_submit failed: %s\n",
                ftdi_get_error_string(ftdi));
        return -1;
    }

    /* ... do other work here ... */

    /* Wait for transfer to complete */
    int transferred = ftdi_transfer_data_done(tc);
    if (transferred < 0) {
        fprintf(stderr, "Async write failed: %s\n", ftdi_get_error_string(ftdi));
        return -1;
    }
    printf("Async wrote %d bytes\n", transferred);
    return 0;
}

/* ------------------------------------------------------------
 * Bit-bang mode: control DBUS pins directly (GPIO)
 * ------------------------------------------------------------ */
int ftdi_bitbang_gpio_example(struct ftdi_context *ftdi)
{
    /* Enable asynchronous bit-bang mode, all 8 DBUS pins as output */
    if (ftdi_set_bitmode(ftdi, 0xFF, BITMODE_BITBANG) < 0) {
        fprintf(stderr, "Set bitbang failed: %s\n", ftdi_get_error_string(ftdi));
        return -1;
    }

    /* Write pin state: D0=1, D1=0, D2=1, D3=0, D4=0, D5=0, D6=0, D7=0 */
    unsigned char pins = 0x05;   /* 0b00000101 */
    if (ftdi_write_data(ftdi, &pins, 1) < 0) {
        fprintf(stderr, "Bitbang write failed: %s\n", ftdi_get_error_string(ftdi));
        return -1;
    }

    /* Read back the current pin state */
    unsigned char pin_state = 0;
    if (ftdi_read_pins(ftdi, &pin_state) < 0) {
        fprintf(stderr, "Read pins failed: %s\n", ftdi_get_error_string(ftdi));
        return -1;
    }
    printf("Current DBUS state: 0x%02X\n", pin_state);

    /* Return to UART mode */
    ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET);
    return 0;
}

/* ------------------------------------------------------------
 * MPSSE SPI master (FT232H / FT2232H channel A or B)
 * ------------------------------------------------------------ */
int ftdi_mpsse_spi_init(struct ftdi_context *ftdi)
{
    unsigned char buf[16];
    int n = 0;

    /* Enable MPSSE mode */
    ftdi_set_bitmode(ftdi, 0x00, BITMODE_RESET);
    ftdi_set_bitmode(ftdi, 0x00, BITMODE_MPSSE);
    ftdi_usb_purge_buffers(ftdi);

    /* Disable divide-by-5 clock divider (enables 60 MHz base clock) */
    buf[n++] = 0x8A;
    /* Disable adaptive clocking */
    buf[n++] = 0x97;
    /* Disable 3-phase data clocking */
    buf[n++] = 0x8D;

    /* Set clock divisor for 1 MHz SPI: divisor = (60 MHz / (2 * 1 MHz)) - 1 = 29 */
    buf[n++] = 0x86;       /* TCK/SK Divisor command */
    buf[n++] = 29 & 0xFF;  /* Value low byte */
    buf[n++] = 29 >> 8;    /* Value high byte */

    /* Set initial pin directions and levels:
     * TCK=out(0), TDI=out(0), TDO=in, TMS/CS=out(1), GPIOL0-3=out(1)
     * Direction byte: 1=output, 0=input
     */
    buf[n++] = 0x80;   /* Set ADBUS low byte direction and value */
    buf[n++] = 0x08;   /* Initial values: CS=1, TCK=0, TDI=0 */
    buf[n++] = 0x0B;   /* Direction: TCK, TDI, CS as outputs; TDO as input */

    ftdi_write_data(ftdi, buf, n);
    return 0;
}

/* SPI transfer: assert CS, clock data, deassert CS */
int ftdi_mpsse_spi_transfer(struct ftdi_context *ftdi,
                             const uint8_t *tx_buf, uint8_t *rx_buf,
                             int length)
{
    unsigned char *cmd = (unsigned char *)malloc(3 + length + 4);
    int n = 0;

    /* Assert CS (ADBUS3 = 0) */
    cmd[n++] = 0x80;
    cmd[n++] = 0x00;   /* CS low */
    cmd[n++] = 0x0B;   /* Direction unchanged */

    /* Clock data bytes out on falling edge, in on rising edge (SPI mode 0) */
    cmd[n++] = 0x31;           /* MSB first, out falling, in rising */
    cmd[n++] = (length - 1) & 0xFF;
    cmd[n++] = (length - 1) >> 8;
    memcpy(&cmd[n], tx_buf, length);
    n += length;

    /* Deassert CS (ADBUS3 = 1) */
    cmd[n++] = 0x80;
    cmd[n++] = 0x08;   /* CS high */
    cmd[n++] = 0x0B;

    /* Send immediate command to flush */
    cmd[n++] = 0x87;

    ftdi_write_data(ftdi, cmd, n);
    free(cmd);

    /* Read response */
    int bytes_read = 0;
    while (bytes_read < length) {
        int ret = ftdi_read_data(ftdi, rx_buf + bytes_read, length - bytes_read);
        if (ret < 0) return -1;
        bytes_read += ret;
    }
    return 0;
}

/* ------------------------------------------------------------
 * Main demonstration
 * ------------------------------------------------------------ */
int main(void)
{
    /* Open FT232H (VID=0x0403, PID=0x6014) */
    struct ftdi_context *ftdi = ftdi_open_uart(0x0403, 0x6014,
                                               "FT232H", NULL,
                                               INTERFACE_A);
    if (!ftdi) return 1;

    /* Write a simple string */
    const char msg[] = "Hello FTDI!\r\n";
    int ret = ftdi_write_data(ftdi, (unsigned char*)msg, strlen(msg));
    printf("Wrote %d bytes\n", ret);

    /* Read up to 256 bytes with 100 ms poll */
    unsigned char rxbuf[256];
    ret = ftdi_read_data(ftdi, rxbuf, sizeof(rxbuf));
    if (ret > 0) {
        rxbuf[ret] = '\0';
        printf("Received: %s\n", rxbuf);
    }

    ftdi_usb_close(ftdi);
    ftdi_free(ftdi);
    return 0;
}
```

### C++ Wrapper Class

```cpp
#include <ftdi.h>
#include <stdexcept>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include <thread>

/**
 * RAII wrapper around libftdi1 for UART communication.
 * Handles resource cleanup automatically via destructor.
 */
class FtdiUart {
public:
    /**
     * Construct and open an FTDI UART device.
     * @param baud    Baud rate in bps (e.g., 115200)
     * @param vid     USB Vendor ID (default FTDI: 0x0403)
     * @param pid     USB Product ID (e.g., 0x6001 = FT232R)
     * @param serial  Device serial number string (empty = first found)
     */
    FtdiUart(int baud = 115200,
             int vid = 0x0403,
             int pid = 0x6001,
             const std::string &serial = "")
    {
        ctx_ = ftdi_new();
        if (!ctx_) throw std::runtime_error("ftdi_new() failed");

        const char *ser_ptr = serial.empty() ? nullptr : serial.c_str();
        int ret = ftdi_usb_open_desc(ctx_, vid, pid, nullptr, ser_ptr);
        if (ret < 0) {
            std::string err = ftdi_get_error_string(ctx_);
            ftdi_free(ctx_);
            throw std::runtime_error("Open failed: " + err);
        }

        ftdi_usb_reset(ctx_);
        ftdi_tcioflush(ctx_);

        if (ftdi_set_baudrate(ctx_, baud) < 0)
            throw std::runtime_error("SetBaudRate: " + error());

        if (ftdi_set_line_property(ctx_, BITS_8, STOP_BIT_1, NONE) < 0)
            throw std::runtime_error("SetLineProperty: " + error());

        ftdi_set_latency_timer(ctx_, 1);
    }

    ~FtdiUart() {
        if (ctx_) {
            ftdi_usb_close(ctx_);
            ftdi_free(ctx_);
        }
    }

    /* Disable copy; allow move */
    FtdiUart(const FtdiUart&) = delete;
    FtdiUart& operator=(const FtdiUart&) = delete;

    /**
     * Write bytes to the UART TX FIFO.
     * @return Number of bytes actually written, or -1 on error.
     */
    int write(const std::vector<uint8_t> &data)
    {
        return ftdi_write_data(ctx_,
                               const_cast<unsigned char*>(data.data()),
                               static_cast<int>(data.size()));
    }

    int write(const std::string &s)
    {
        return ftdi_write_data(ctx_,
                               reinterpret_cast<unsigned char*>(
                                   const_cast<char*>(s.data())),
                               static_cast<int>(s.size()));
    }

    /**
     * Read available bytes from RX FIFO (non-blocking).
     * @param max_bytes Maximum number of bytes to read.
     * @return Vector of received bytes (may be empty).
     */
    std::vector<uint8_t> read(size_t max_bytes = 4096)
    {
        std::vector<uint8_t> buf(max_bytes);
        int ret = ftdi_read_data(ctx_, buf.data(), static_cast<int>(max_bytes));
        if (ret <= 0) return {};
        buf.resize(ret);
        return buf;
    }

    /**
     * Read until a delimiter byte is found or timeout expires.
     * @param delim     Terminating byte (e.g., '\n')
     * @param timeout   Maximum wait time
     * @return Received data including the delimiter, or partial data on timeout.
     */
    std::vector<uint8_t> readUntil(uint8_t delim,
                                   std::chrono::milliseconds timeout = std::chrono::milliseconds(1000))
    {
        std::vector<uint8_t> result;
        auto deadline = std::chrono::steady_clock::now() + timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            auto chunk = read(256);
            for (uint8_t b : chunk) {
                result.push_back(b);
                if (b == delim) return result;
            }
            if (chunk.empty())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return result;   /* Timeout: return what we have */
    }

    /** Flush TX and RX buffers */
    void flush() { ftdi_tcioflush(ctx_); }

    /** Set latency timer (1–255 ms) */
    void setLatencyTimer(uint8_t ms) { ftdi_set_latency_timer(ctx_, ms); }

    /** Enable/disable RTS/CTS hardware flow control */
    void setRtsCts(bool enable)
    {
        ftdi_setflowctrl(ctx_, enable ? SIO_RTS_CTS_HS : SIO_DISABLE_FLOW_CTRL);
    }

private:
    std::string error() const { return ftdi_get_error_string(ctx_); }
    struct ftdi_context *ctx_ = nullptr;
};

/* Usage example */
int main()
{
    try {
        FtdiUart uart(115200, 0x0403, 0x6001);   /* FT232R at 115200 */

        uart.write("AT\r\n");

        auto response = uart.readUntil('\n', std::chrono::milliseconds(500));
        std::string resp(response.begin(), response.end());
        printf("Got: %s\n", resp.c_str());

    } catch (const std::exception &e) {
        fprintf(stderr, "Error: %s\n", e.what());
        return 1;
    }
    return 0;
}
```

---

## Rust Implementation

### Dependencies (`Cargo.toml`)

```toml
[package]
name = "ftdi_uart_example"
version = "0.1.0"
edition = "2021"

[dependencies]
# libftdi1 bindings (links against the system libftdi1)
libftdi1-sys = "0.3"
# Higher-level safe wrapper (recommended)
ftdi = "0.2"
# For async examples
tokio = { version = "1", features = ["full"] }
anyhow = "1"
thiserror = "1"
```

### Safe UART Wrapper Using the `ftdi` Crate

```rust
//! ftdi_uart.rs — Safe FTDI UART wrapper for Rust
//!
//! Build: cargo build
//! Requires: libftdi1-dev installed (apt install libftdi1-dev)

use ftdi::{Device, FlowControl, Interface};
use std::io::{Read, Write};
use std::time::{Duration, Instant};
use anyhow::{Context, Result};

/// Configuration for a FTDI UART connection.
#[derive(Debug, Clone)]
pub struct FtdiConfig {
    /// USB Vendor ID (FTDI default: 0x0403)
    pub vid: u16,
    /// USB Product ID (FT232R: 0x6001, FT232H: 0x6014, FT2232H: 0x6010)
    pub pid: u16,
    /// Channel for multi-port chips (A=0, B=1, C=2, D=3)
    pub interface: Interface,
    /// Baud rate in bits per second
    pub baud_rate: u32,
    /// Latency timer in milliseconds (1–255)
    pub latency_ms: u8,
    /// Optional serial number filter
    pub serial: Option<String>,
}

impl Default for FtdiConfig {
    fn default() -> Self {
        Self {
            vid: 0x0403,
            pid: 0x6001,
            interface: Interface::A,
            baud_rate: 115200,
            latency_ms: 1,
            serial: None,
        }
    }
}

/// RAII FTDI UART session.
pub struct FtdiUart {
    device: Device,
    config: FtdiConfig,
}

impl FtdiUart {
    /// Open and configure an FTDI device.
    pub fn open(config: FtdiConfig) -> Result<Self> {
        let mut builder = ftdi::find_by_vid_pid(config.vid, config.pid)
            .interface(config.interface);

        if let Some(ref serial) = config.serial {
            builder = builder.serial(serial.as_str());
        }

        let mut device = builder.open()
            .context("Failed to open FTDI device")?;

        // Reset and purge
        device.usb_reset().context("USB reset failed")?;
        device.usb_purge_buffers().context("Purge failed")?;

        // Configure UART parameters
        device.set_baud_rate(config.baud_rate)
            .context("Set baud rate failed")?;

        device.set_line_property(
            ftdi::BitsPerWord::Eight,
            ftdi::StopBits::One,
            ftdi::Parity::None,
        ).context("Set line property failed")?;

        device.set_flow_control(FlowControl::None)
            .context("Set flow control failed")?;

        device.set_latency_timer(config.latency_ms)
            .context("Set latency timer failed")?;

        // Optimal chunk sizes for throughput vs. latency
        device.set_read_chunk_size(65536);
        device.set_write_chunk_size(65536);

        Ok(Self { device, config })
    }

    /// Send bytes over UART. Returns number of bytes sent.
    pub fn send(&mut self, data: &[u8]) -> Result<usize> {
        self.device.write_all(data)
            .context("FTDI write failed")?;
        Ok(data.len())
    }

    /// Receive up to `max_bytes` from the RX FIFO (non-blocking).
    pub fn receive(&mut self, max_bytes: usize) -> Result<Vec<u8>> {
        let mut buf = vec![0u8; max_bytes];
        match self.device.read(&mut buf) {
            Ok(n) => {
                buf.truncate(n);
                Ok(buf)
            }
            Err(e) if e.kind() == std::io::ErrorKind::TimedOut => Ok(vec![]),
            Err(e) => Err(e).context("FTDI read failed"),
        }
    }

    /// Read until a delimiter byte appears or the timeout expires.
    /// Returns accumulated data including the delimiter (if found).
    pub fn read_until(&mut self, delimiter: u8, timeout: Duration) -> Result<Vec<u8>> {
        let start = Instant::now();
        let mut result = Vec::new();

        loop {
            if start.elapsed() >= timeout {
                break;   // Timeout
            }

            let chunk = self.receive(256)?;

            for &byte in &chunk {
                result.push(byte);
                if byte == delimiter {
                    return Ok(result);
                }
            }

            if chunk.is_empty() {
                std::thread::sleep(Duration::from_millis(1));
            }
        }

        Ok(result)
    }

    /// Send a command string and await a newline-terminated response.
    pub fn command(&mut self, cmd: &str, timeout: Duration) -> Result<String> {
        self.send(cmd.as_bytes())?;
        let response = self.read_until(b'\n', timeout)?;
        Ok(String::from_utf8_lossy(&response).into_owned())
    }

    /// Flush TX and RX buffers.
    pub fn flush(&mut self) -> Result<()> {
        self.device.usb_purge_buffers().context("Flush failed")
    }

    /// Change baud rate on an open device.
    pub fn set_baud_rate(&mut self, baud: u32) -> Result<()> {
        self.device.set_baud_rate(baud).context("Set baud rate failed")?;
        Ok(())
    }

    /// Enable or disable RTS/CTS hardware flow control.
    pub fn set_rts_cts(&mut self, enable: bool) -> Result<()> {
        let fc = if enable { FlowControl::RtsCts } else { FlowControl::None };
        self.device.set_flow_control(fc).context("Set flow control failed")
    }

    /// Returns the current configuration.
    pub fn config(&self) -> &FtdiConfig { &self.config }
}

// ---------------------------------------------------------------------------
// Enumerate FTDI devices
// ---------------------------------------------------------------------------

/// Information about a discovered FTDI device.
#[derive(Debug)]
pub struct FtdiDeviceInfo {
    pub vid: u16,
    pub pid: u16,
    pub serial: String,
    pub description: String,
}

/// List all FTDI devices currently connected to the system.
pub fn list_ftdi_devices() -> Result<Vec<FtdiDeviceInfo>> {
    // libftdi1 enumerate via ftdi_usb_find_all
    // The ftdi crate exposes this through the find_all() function
    let devices = ftdi::list_devices(0x0403, 0x0000)
        .context("Failed to enumerate FTDI devices")?;

    Ok(devices.into_iter().map(|d| FtdiDeviceInfo {
        vid: d.vendor_id,
        pid: d.product_id,
        serial: d.serial.unwrap_or_default(),
        description: d.description.unwrap_or_default(),
    }).collect())
}

// ---------------------------------------------------------------------------
// Main demonstration
// ---------------------------------------------------------------------------

fn main() -> Result<()> {
    // List connected FTDI devices
    println!("=== Connected FTDI Devices ===");
    match list_ftdi_devices() {
        Ok(devs) if !devs.is_empty() => {
            for (i, d) in devs.iter().enumerate() {
                println!("  [{i}] VID={:#06X} PID={:#06X} Serial='{}' Desc='{}'",
                         d.vid, d.pid, d.serial, d.description);
            }
        }
        Ok(_) => println!("  (none found)"),
        Err(e) => println!("  Enumeration failed: {e}"),
    }
    println!();

    // Open FT232R at 115200 baud
    let config = FtdiConfig {
        pid: 0x6001,   // FT232R
        baud_rate: 115200,
        ..Default::default()
    };

    let mut uart = FtdiUart::open(config)
        .context("Could not open FTDI UART")?;

    println!("Opened FTDI UART at 115200 bps");

    // Send an AT command and wait for a response
    uart.send(b"AT\r\n")?;
    println!("Sent: AT");

    let response = uart.read_until(b'\n', Duration::from_millis(500))?;
    let response_str = String::from_utf8_lossy(&response);
    println!("Received: {response_str}");

    // Demonstrate baud rate change
    uart.set_baud_rate(9600)?;
    println!("Changed baud rate to 9600 bps");
    uart.set_baud_rate(115200)?;

    // Flush before closing
    uart.flush()?;

    println!("Done.");
    Ok(())
}
```

### Low-Level MPSSE (SPI) in Rust via `libftdi1-sys`

```rust
//! mpsse_spi.rs — Raw MPSSE SPI master using libftdi1-sys FFI bindings
//!
//! Demonstrates SPI communication at 1 MHz with an FTDI FT232H.

use libftdi1_sys::*;
use std::ptr;
use anyhow::{bail, Result};

pub struct MpsseSpi {
    ctx: *mut ftdi_context,
}

unsafe impl Send for MpsseSpi {}

impl MpsseSpi {
    /// Initialize FT232H as a 1 MHz SPI master (MPSSE mode).
    pub fn new(vid: u16, pid: u16) -> Result<Self> {
        unsafe {
            let ctx = ftdi_new();
            if ctx.is_null() { bail!("ftdi_new() returned null"); }

            if ftdi_usb_open(ctx, vid as i32, pid as i32) < 0 {
                let msg = std::ffi::CStr::from_ptr(ftdi_get_error_string(ctx))
                    .to_string_lossy().into_owned();
                ftdi_free(ctx);
                bail!("USB open failed: {}", msg);
            }

            ftdi_usb_reset(ctx);
            ftdi_usb_purge_buffers(ctx);

            // Switch to MPSSE mode
            ftdi_set_bitmode(ctx, 0x00, ftdi_mpsse_mode::BITMODE_RESET as u8);
            ftdi_set_bitmode(ctx, 0x00, ftdi_mpsse_mode::BITMODE_MPSSE as u8);
            ftdi_set_latency_timer(ctx, 1);

            // Send MPSSE initialization commands
            let init: &[u8] = &[
                0x8A,        // Disable clock divide-by-5
                0x97,        // Disable adaptive clocking
                0x8D,        // Disable 3-phase data clocking
                0x86,        // Set TCK divisor
                29, 0,       // 60MHz / (2*(29+1)) = 1 MHz
                0x80,        // Set ADBUS direction and level
                0x08,        // Initial: CS=1, TCK=0, TDI=0
                0x0B,        // Direction: TCK,TDI,CS=output; TDO=input
            ];

            ftdi_write_data(ctx, init.as_ptr(), init.len() as i32);
            std::thread::sleep(std::time::Duration::from_millis(10));

            Ok(Self { ctx })
        }
    }

    /// Perform a full-duplex SPI transfer.
    /// Simultaneously sends `tx` bytes and receives the same number of bytes.
    pub fn transfer(&mut self, tx: &[u8]) -> Result<Vec<u8>> {
        let len = tx.len();
        let mut cmd: Vec<u8> = Vec::with_capacity(4 + len + 4);

        // Assert CS
        cmd.extend_from_slice(&[0x80, 0x00, 0x0B]);

        // Data transfer: MSB first, out on falling edge, in on rising edge
        cmd.push(0x31);
        cmd.push(((len - 1) & 0xFF) as u8);
        cmd.push(((len - 1) >> 8) as u8);
        cmd.extend_from_slice(tx);

        // Deassert CS, then flush
        cmd.extend_from_slice(&[0x80, 0x08, 0x0B, 0x87]);

        unsafe {
            ftdi_write_data(self.ctx, cmd.as_ptr(), cmd.len() as i32);
        }

        // Read response bytes
        let mut rx = vec![0u8; len];
        let mut received = 0usize;
        let deadline = std::time::Instant::now() + std::time::Duration::from_millis(500);

        while received < len {
            if std::time::Instant::now() > deadline {
                bail!("SPI read timeout after receiving {}/{} bytes", received, len);
            }
            let ret = unsafe {
                ftdi_read_data(self.ctx,
                               rx[received..].as_mut_ptr(),
                               (len - received) as i32)
            };
            if ret > 0 { received += ret as usize; }
            else if ret < 0 { bail!("ftdi_read_data error: {}", ret); }
        }

        Ok(rx)
    }

    /// Write-only SPI (ignore MISO).
    pub fn write(&mut self, data: &[u8]) -> Result<()> {
        let _ = self.transfer(data)?;
        Ok(())
    }
}

impl Drop for MpsseSpi {
    fn drop(&mut self) {
        unsafe {
            if !self.ctx.is_null() {
                ftdi_usb_close(self.ctx);
                ftdi_free(self.ctx);
            }
        }
    }
}

/// Example: Read device ID from an SPI flash (W25Q32 / similar)
fn read_flash_jedec_id(spi: &mut MpsseSpi) -> Result<[u8; 3]> {
    // JEDEC RDID command: 0x9F, followed by 3 dummy bytes for response
    let tx = [0x9Fu8, 0x00, 0x00, 0x00];
    let rx = spi.transfer(&tx)?;
    Ok([rx[1], rx[2], rx[3]])
}

fn main() -> Result<()> {
    let mut spi = MpsseSpi::new(0x0403, 0x6014)?;  // FT232H
    println!("MPSSE SPI initialized at 1 MHz");

    let jedec = read_flash_jedec_id(&mut spi)?;
    println!("JEDEC ID: {:02X} {:02X} {:02X}", jedec[0], jedec[1], jedec[2]);
    // Typical output for W25Q32: EF 40 16

    // Demonstrate a simple SPI write (e.g., write-enable command 0x06)
    spi.write(&[0x06])?;
    println!("Write Enable command sent");

    Ok(())
}
```

### Async Rust with Tokio

```rust
//! async_ftdi.rs — Non-blocking FTDI reads using Tokio spawn_blocking
//!
//! The FTDI/libftdi APIs are synchronous, but we can wrap them in
//! spawn_blocking to integrate cleanly with an async runtime.

use tokio::task;
use std::sync::{Arc, Mutex};
use std::time::Duration;
use anyhow::Result;

// Wrap FtdiUart in Arc<Mutex<>> for safe sharing across async tasks
type SharedUart = Arc<Mutex<FtdiUart>>;

async fn async_ftdi_read_loop(uart: SharedUart) {
    loop {
        // Move the shared reference into the blocking thread
        let uart_clone = Arc::clone(&uart);

        let result = task::spawn_blocking(move || {
            let mut u = uart_clone.lock().unwrap();
            u.receive(4096)
        }).await;

        match result {
            Ok(Ok(data)) if !data.is_empty() => {
                let text = String::from_utf8_lossy(&data);
                println!("[RX] {}", text.trim());
            }
            Ok(Err(e)) => {
                eprintln!("Read error: {e}");
                break;
            }
            _ => {
                tokio::time::sleep(Duration::from_millis(1)).await;
            }
        }
    }
}

async fn async_ftdi_sender(uart: SharedUart, messages: Vec<String>) {
    for msg in messages {
        let uart_clone = Arc::clone(&uart);
        let data = msg.as_bytes().to_vec();

        let _ = task::spawn_blocking(move || {
            let mut u = uart_clone.lock().unwrap();
            u.send(&data)
        }).await;

        println!("[TX] {}", msg.trim());
        tokio::time::sleep(Duration::from_millis(100)).await;
    }
}

#[tokio::main]
async fn main() -> Result<()> {
    let config = FtdiConfig {
        baud_rate: 115200,
        ..Default::default()
    };

    let uart = Arc::new(Mutex::new(FtdiUart::open(config)?));

    let messages = vec![
        "AT\r\n".to_string(),
        "AT+VERSION\r\n".to_string(),
        "AT+HELP\r\n".to_string(),
    ];

    // Spawn concurrent reader and writer
    let reader_uart = Arc::clone(&uart);
    let reader_handle = tokio::spawn(async move {
        async_ftdi_read_loop(reader_uart).await;
    });

    async_ftdi_sender(Arc::clone(&uart), messages).await;

    // Give reader time to collect responses, then cancel
    tokio::time::sleep(Duration::from_secs(2)).await;
    reader_handle.abort();

    println!("Done.");
    Ok(())
}
```

---

## Advanced Features

### CBUS GPIO Control (FT232R / FT232H)

The CBUS pins on FT232R (4 pins) can be used as GPIOs after configuring them as `IOMODE` in the EEPROM. From software, use `FT_SetBitMode` with the CBUS bit-bang mask.

```c
/* D2XX: Toggle CBUS0 and CBUS2 as outputs */
/* Upper nibble = direction (1=out), lower nibble = value */

/* Set CBUS0=output-high, CBUS2=output-low, rest = input */
UCHAR cbus_mask = (0x01 | 0x04) << 4;  /* Direction: CBUS0, CBUS2 as outputs */
UCHAR cbus_val  = 0x01;                 /* CBUS0=1, CBUS2=0 */
FT_SetBitMode(handle, cbus_mask | cbus_val, FT_BITMODE_CBUS_BITBANG);

/* Read back */
UCHAR cbus_state = 0;
FT_GetBitMode(handle, &cbus_state);
printf("CBUS state: 0x%02X\n", cbus_state & 0x0F);
```

### D2XX Event Notification (Windows)

```c
/* Set up event-driven receive notification (Windows only) */
HANDLE rx_event = CreateEvent(NULL, FALSE, FALSE, NULL);
FT_SetEventNotification(handle, FT_EVENT_RXCHAR | FT_EVENT_MODEM_STATUS, rx_event);

/* Worker thread waits for the event */
DWORD wait_result = WaitForSingleObject(rx_event, 5000 /* ms */);
if (wait_result == WAIT_OBJECT_0) {
    DWORD bytes_rx, bytes_tx, event_word;
    FT_GetStatus(handle, &bytes_rx, &bytes_tx, &event_word);

    if (event_word & FT_EVENT_RXCHAR) {
        uint8_t buf[1024];
        DWORD received;
        FT_Read(handle, buf, bytes_rx, &received);
        /* Process received data */
    }
}
CloseHandle(rx_event);
```

### EEPROM Blank Check and User Area

```c
/* Check if EEPROM is blank (all 0xFF) */
WORD ee_val;
FT_STATUS st = FT_ReadEE(handle, 0, &ee_val);
if (st == FT_OK && ee_val == 0xFFFF) {
    printf("EEPROM is blank\n");
}

/* FT232H has 128-word EEPROM; first 0x12 words are config,
 * the rest are available for user data */
#define USER_EEPROM_START 0x12
FT_WriteEE(handle, USER_EEPROM_START, 0xDEAD);  /* Write user word */
FT_ReadEE (handle, USER_EEPROM_START, &ee_val); /* Read it back */
```

---

## Error Handling and Diagnostics

### D2XX Status Codes

| FT_STATUS Value | Meaning                        | Common Cause                        |
|-----------------|--------------------------------|-------------------------------------|
| FT_OK (0)       | Success                        | —                                   |
| FT_INVALID_HANDLE (1) | Null or closed handle   | Handle not opened or already closed |
| FT_DEVICE_NOT_FOUND (2) | USB device not present | Device unplugged, wrong VID/PID     |
| FT_DEVICE_NOT_OPENED (3) | Not open               | Forgot FT_Open                      |
| FT_IO_ERROR (4) | USB transfer error             | Cable issue, device power problem   |
| FT_INSUFFICIENT_RESOURCES (5) | malloc failed     | Out of memory                       |
| FT_INVALID_PARAMETER (6) | Bad argument           | Null pointer, out-of-range value    |
| FT_INVALID_BAUD_RATE (7) | Unsupported baud      | Fractional divider overflow         |
| FT_DEVICE_NOT_OPENED_FOR_ERASE (9) | EEPROM erase | Missing FT_Open before erase        |
| FT_EEPROM_WRITE_PROTECTED (18) | Write-protected  | Jumper or config flag set           |

### libftdi Error Pattern

```c
int ret = ftdi_some_function(ftdi, ...);
if (ret < 0) {
    fprintf(stderr, "Error %d: %s\n", ret, ftdi_get_error_string(ftdi));
    /* Negative return values in libftdi are always errors */
    /* Check libusb_strerror for underlying USB errors */
}
```

### Diagnosing on Linux

```bash
# Check USB device is detected
lsusb | grep 0403

# Check which driver has claimed the device
ls -la /sys/bus/usb/drivers/ftdi_sio/

# Unbind VCP driver to allow D2XX/libftdi direct access
echo "1-1.3:1.0" > /sys/bus/usb/drivers/ftdi_sio/unbind

# Check kernel messages for USB errors
dmesg | grep -i ftdi | tail -20

# Verify device permissions (add user to 'dialout' group)
sudo usermod -aG dialout $USER

# udev rule for non-root access (save to /etc/udev/rules.d/99-ftdi.rules)
# SUBSYSTEM=="usb", ATTR{idVendor}=="0403", ATTR{idProduct}=="6001", MODE="0666"
```

---

## Platform-Specific Considerations

### Linux

- The `ftdi_sio` kernel module claims FTDI devices automatically as VCP.
- To use D2XX or libftdi directly, unbind the kernel driver (see diagnostics above) or add a udev rule with `RUN+="modprobe -r ftdi_sio"`.
- Prefer libftdi over D2XX for open-source projects; D2XX requires accepting FTDI's binary distribution license.
- `/dev/ttyUSB0`, `/dev/ttyUSB1`, etc. correspond to VCP mode.

### Windows

- Install the D2XX driver package from ftdichip.com or use WinUSB/libusb via Zadig for libftdi.
- D2XX and VCP drivers conflict; only one can claim the device at a time.
- Use `FT_OPEN_BY_DESCRIPTION` or `FT_OPEN_BY_SERIAL_NUMBER` rather than index to avoid ordering issues when multiple FTDI devices are attached.
- COM port numbers (COMx) are assigned by Windows Device Manager and can be fixed via device properties.

### macOS

- FTDI provides a VCP driver as a `.kext` for macOS (legacy) and a dext for macOS 11+.
- For libftdi, use Homebrew: `brew install libftdi`.
- Device path: `/dev/cu.usbserial-XXXXXXXX` (outgoing) and `/dev/tty.usbserial-XXXXXXXX` (incoming, for dial-in use).
- D2XX requires removing Apple's built-in FTDI driver first.

### Embedded Linux (Raspberry Pi, BeagleBone)

- libftdi works on ARM with libusb-1.0; install with `apt install libftdi1-dev`.
- Latency timer is especially important on single-board computers where USB polling is less frequent.
- Use `python3 -m pyftdi.ftdi` for interactive device inspection and testing.

---

## Summary

FTDI chips such as the FT232R, FT232H, FT2232H, and FT4232H are the de facto standard for USB-to-UART (and USB-to-SPI/I2C/JTAG) conversion in professional and hobbyist embedded systems. Their key advantages over generic USB CDC devices include deterministic FIFO buffering, programmable latency, MPSSE for multi-protocol support, CBUS GPIO, and a mature cross-platform software ecosystem.

**Key programming takeaways:**

- Choose the **VCP driver** for simplest compatibility with existing serial tools; choose **D2XX or libftdi** when you need low latency, MPSSE, GPIO, or EEPROM access.
- Always **open by serial number** (not device index) for robust multi-device environments.
- Set the **latency timer to 1 ms** for interactive or low-latency use; raise it for bulk throughput.
- In C/C++, the **D2XX SDK** (`ftd2xx.h`) and **libftdi1** (`ftdi.h`) are the two primary APIs, with libftdi preferred for open-source work.
- In Rust, the **`ftdi` crate** provides a safe high-level wrapper over libftdi1; raw FFI via **`libftdi1-sys`** gives full MPSSE control when needed.
- For MPSSE (SPI/I2C/JTAG), a precise command byte sequence is required; always send the **flush byte (0x87)** after commands that expect a USB response.
- On Linux, **unbind `ftdi_sio`** before using D2XX or libftdi directly; set udev rules for non-root access.
- The **FT2232H** is the most versatile choice for development boards needing simultaneous JTAG debugging and UART console on a single USB port.

---

*Document: 76 — FTDI Chip Integration | UART Programming Reference Series*