Here's the complete document. It covers:

**Architecture & Hardware**
- CAN 2.0A/B frame format, dominant/recessive bus logic, and wired-AND arbitration
- Full MCP2515 block diagram, pin table, and register map with addresses

**SPI Protocol**
- Mode 0,0 timing, CS assertion rules, and the complete command set (READ, WRITE, BIT MODIFY, RTS, fast buffer commands)
- Burst register access for efficient frame loading

**Driver Implementation**
- `mcp2515.h` — full register/mask/command definitions and `can_frame_t` type
- `mcp2515.c` — complete C driver: `reset()`, `init()`, `send()`, `receive()`, `bit_modify()`, `set_mode()`, error counter reads
- `main.c` — Linux/spidev + libgpiod usage example with polling loop

**Rust Driver**
- `embedded-hal 1.0`-based generic driver (`Mcp2515<SPI, CS>`) working on bare-metal and Linux
- Full `init()`, `send()`, `receive()`, `error_counters()`, mode switching
- `main.rs` — `linux-embedded-hal` / Raspberry Pi example

**Reference Tables**
- CNF1/CNF2/CNF3 values for 125k / 250k / 500k / 1 Mbit/s at 8 MHz
- MCP2515 vs. MCP2517FD feature comparison
- Similar device comparison table (SJA1000, TJA1145, ATA6561, etc.)

# 69. CAN Controllers via SPI

## Integrating MCP2515 and Similar CAN Controllers Through SPI

---

## Table of Contents

1. [Introduction](#introduction)
2. [CAN Bus Overview](#can-bus-overview)
3. [The MCP2515 CAN Controller](#the-mcp2515-can-controller)
4. [SPI Interface to MCP2515](#spi-interface-to-mcp2515)
5. [MCP2515 Register Architecture](#mcp2515-register-architecture)
6. [SPI Command Set](#spi-command-set)
7. [Initialization Sequence](#initialization-sequence)
8. [Transmitting CAN Frames](#transmitting-can-frames)
9. [Receiving CAN Frames](#receiving-can-frames)
10. [Interrupt Handling](#interrupt-handling)
11. [Bit Timing Configuration](#bit-timing-configuration)
12. [Programming Examples — C/C++](#programming-examples--cc)
13. [Programming Examples — Rust](#programming-examples--rust)
14. [Similar Devices](#similar-devices)
15. [Summary](#summary)

---

## Introduction

Many microcontrollers lack a built-in CAN peripheral, yet CAN bus is the dominant protocol in automotive, industrial automation, and robotics applications. The solution is an **external CAN controller** connected via SPI — the most widely used being the **Microchip MCP2515**.

This document covers:
- CAN bus fundamentals as they apply to peripheral integration
- The MCP2515 hardware architecture and register map
- The full SPI communication protocol with the MCP2515
- Initialization, transmission, reception, and interrupt handling
- Complete, production-quality code examples in **C/C++** and **Rust**

---

## CAN Bus Overview

### Physical Layer

CAN (Controller Area Network) is a differential, multi-master serial bus standardized in ISO 11898. Two wires — **CAN_H** and **CAN_L** — carry the signal:

| State      | CAN_H Voltage | CAN_L Voltage | Differential |
|------------|---------------|---------------|--------------|
| Dominant 0 | ~3.5 V        | ~1.5 V        | ~2.0 V       |
| Recessive 1| ~2.5 V        | ~2.5 V        | ~0.0 V       |

The bus uses a **wired-AND** arbitration mechanism: dominant bits always win over recessive bits, enabling non-destructive arbitration without a bus master.

### Frame Format (Classical CAN)

```
┌──────┬─────┬─────┬──────┬─────┬─────┬─────────┬─────┬───┐
│ SOF  │ ID  │ RTR │ IDE  │ r0  │ DLC │  DATA   │ CRC │ACK│
│  1b  │ 11b │  1b │  1b  │  1b │  4b │ 0–64 B  │ 15b │   │
└──────┴─────┴─────┴──────┴─────┴─────┴─────────┴─────┴───┘
```

- **SOF** – Start of Frame (dominant bit)  
- **ID** – 11-bit (standard) or 29-bit (extended) identifier  
- **RTR** – Remote Transmission Request  
- **DLC** – Data Length Code (0–8 bytes for Classical CAN)  
- **CRC** – 15-bit cyclic redundancy check  
- **ACK** – Acknowledgement slot (any receiver pulls dominant)  

### CAN FD

CAN FD (ISO 11898-1:2015) extends data payloads up to **64 bytes** and allows the data phase to run at a higher bit rate than the arbitration phase. The MCP2517FD/MCP2518FD support CAN FD; the classic MCP2515 supports Classical CAN only (up to 1 Mbit/s).

---

## The MCP2515 CAN Controller

### Block Diagram

```
                    ┌─────────────────────────────────────────┐
                    │              MCP2515                    │
                    │                                         │
Host MCU ───SPI────►│  SPI Interface                          │
                    │       │                                 │
                    │       ▼                                 │
                    │  Control Logic / Register File          │
                    │       │                                 │
                    │   ┌───┴────┐   ┌─────────┐              │
                    │   │  TX    │   │   RX    │              │
                    │   │Buffers │   │ Buffers │              │
                    │   │TXB0–2  │   │ RXB0–1  │              │
                    │   └───┬────┘   └────┬────┘              │
                    │       │   CAN Core  │                   │
                    │       └─────┬───────┘                   │
                    │         BitStream                       │
                    │         Generator /                     │
                    │         Error Handler                   │
                    └──────────────┬──────────────────────────┘
                                   │
                            TXD / RXD
                                   │
                            ┌──────▼──────┐
                            │  MCP2551    │  (CAN Transceiver)
                            │  TJA1050    │
                            └──────┬──────┘
                                   │
                             CAN_H / CAN_L
```

### Key Features

| Feature                 | Specification                            |
|-------------------------|------------------------------------------|
| CAN Standard            | 2.0A (11-bit ID) and 2.0B (29-bit ID)    |
| Max Bit Rate            | 1 Mbit/s                                 |
| TX Buffers              | 3 (TXB0, TXB1, TXB2) with priority       |
| RX Buffers              | 2 (RXB0, RXB1) with double-buffering     |
| Acceptance Filters      | 6 masks/filters                          |
| SPI Clock               | Up to 10 MHz                             |
| Supply Voltage          | 2.7 V – 5.5 V                            |
| Interrupt Output        | Active-low open-drain /INT pin           |
| One-Shot Mode           | Prevents automatic retransmission        |

### Pin Description

| Pin       | Function                                 |
|-----------|------------------------------------------|
| SCK       | SPI clock input                          |
| SI (MOSI) | SPI data input                           |
| SO (MISO) | SPI data output                          |
| /CS       | SPI chip select (active low)             |
| /INT      | Interrupt output (active low)            |
| /RESET    | Hardware reset (active low)              |
| TXD       | CAN transmit to transceiver              |
| RXD       | CAN receive from transceiver             |
| TX0RTS    | TX buffer 0 request-to-send pin          |
| TX1RTS    | TX buffer 1 request-to-send pin          |
| TX2RTS    | TX buffer 2 request-to-send pin          |
| RX0BF     | RX buffer 0 interrupt/buffer-full pin    |
| RX1BF     | RX buffer 1 interrupt/buffer-full pin    |
| CLKOUT    | Configurable clock output                |
| OSC1/OSC2 | Crystal oscillator pins                  |

---

## SPI Interface to MCP2515

### SPI Mode

The MCP2515 operates in **SPI Mode 0,0** (CPOL=0, CPHA=0):
- Data is captured on the **rising** edge of SCK
- Data is shifted out on the **falling** edge of SCK
- /CS must be asserted (low) for the entire transaction

```
/CS  ‾‾‾‾┐                              ┌‾‾‾‾
         └──────────────────────────────┘
SCK       ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
          │ │ │ │ │ │ │ │ │ │ │ │ │ │ │
        ──┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─
MOSI   ┌──CMD──┬──ADDR──┬──DATA──────────┐
MISO   └──────────────────────────────────┘
```

### Wiring to a Typical MCU

```
MCU            MCP2515
───────────────────────────
GPIO (output)──► /CS
SPI_SCK ───────► SCK
SPI_MOSI ──────► SI
SPI_MISO ◄─────  SO
GPIO (input) ◄── /INT
GPIO (output)──► /RESET
3V3 / 5V ──────► VDD
GND ────────────► VSS

MCP2515        MCP2551 (Transceiver)
───────────────────────────
TXD ───────────► TXD
RXD ◄────────── RXD
VDD ───────────► VDD (with 0.1µF bypass cap)

MCP2551        CAN Bus
───────────────────────────
CANH ──────────► CAN_H
CANL ──────────► CAN_L
```

---

## MCP2515 Register Architecture

### Important Registers (selected)

| Register   | Address | Description                            |
|------------|---------|----------------------------------------|
| CANSTAT    | 0x0E    | CAN status (operation mode, ICOD)      |
| CANCTRL    | 0x0F    | CAN control (mode, CLKOUT, ABAT)       |
| CNF1       | 0x2A    | Bit timing configuration 1             |
| CNF2       | 0x29    | Bit timing configuration 2             |
| CNF3       | 0x28    | Bit timing configuration 3             |
| CANINTE    | 0x2B    | Interrupt enable register              |
| CANINTF    | 0x2C    | Interrupt flag register                |
| EFLG       | 0x2D    | Error flag register                    |
| TXB0CTRL   | 0x30    | TX buffer 0 control                    |
| TXB0SIDH   | 0x31    | TX buffer 0 standard ID high           |
| TXB0SIDL   | 0x32    | TX buffer 0 standard ID low / EXIDE    |
| TXB0EID8   | 0x33    | TX buffer 0 extended ID high           |
| TXB0EID0   | 0x34    | TX buffer 0 extended ID low            |
| TXB0DLC    | 0x35    | TX buffer 0 data length code           |
| TXB0D0–D7  | 0x36–3D | TX buffer 0 data bytes                 |
| RXB0CTRL   | 0x60    | RX buffer 0 control                    |
| RXB0SIDH   | 0x61    | RX buffer 0 standard ID high           |
| RXB0SIDL   | 0x62    | RX buffer 0 standard ID low            |
| RXB0EID8   | 0x63    | RX buffer 0 extended ID high           |
| RXB0EID0   | 0x64    | RX buffer 0 extended ID low            |
| RXB0DLC    | 0x65    | RX buffer 0 data length code           |
| RXB0D0–D7  | 0x66–6D | RX buffer 0 data bytes                 |
| RXB1CTRL   | 0x70    | RX buffer 1 control                    |
| RXF0SIDH   | 0x00    | Acceptance filter 0 standard ID high   |
| RXM0SIDH   | 0x20    | Acceptance mask 0 standard ID high     |
| TEC        | 0x1C    | Transmit error counter                 |
| REC        | 0x1D    | Receive error counter                  |

### Operation Modes (CANCTRL bits 7:5)

| Mode         | REQOP[2:0] | Description                                 |
|--------------|------------|---------------------------------------------|
| Normal       | 000        | Full CAN operation (TX and RX enabled)      |
| Sleep        | 001        | Low-power sleep mode                        |
| Loopback     | 010        | TX → RX internally, no bus traffic          |
| Listen-only  | 011        | Receive only, no ACK transmitted            |
| Configuration| 100        | Register access for CNF1–3, filters, masks  |

> **Important**: CNF1–CNF3, acceptance filters, and masks can **only** be written in **Configuration mode**.

---

## SPI Command Set

| Command              | Byte  | Description                                       |
|----------------------|-------|---------------------------------------------------|
| RESET                | 0xC0  | Resets device to default state                    |
| READ                 | 0x03  | Read register(s): `0x03 <addr> → data...`         |
| WRITE                | 0x02  | Write register(s): `0x02 <addr> <data...>`        |
| READ STATUS          | 0xA0  | Returns an 8-bit status byte quickly              |
| RX STATUS            | 0xB0  | Returns filter/buffer match info                  |
| BIT MODIFY           | 0x05  | Modify specific bits: `0x05 <addr> <mask> <data>` |
| LOAD TX BUFFER       | 0x40  | Fast load to TX buffer (avoids addr phase)        |
| REQUEST TO SEND (RTS)| 0x80  | Trigger TX: `0x81/0x82/0x84` for TXB0/1/2         |
| READ RX BUFFER       | 0x90  | Fast read from RX buffer                          |

### Bit Modify Command Detail

The `BIT MODIFY` command (0x05) is essential for changing individual bits without a read-modify-write cycle:

```
/CS ──┐                          ┌──
      └──────────────────────────┘
MOSI:  0x05 | <addr> | <mask> | <data>
         ↑       ↑        ↑        ↑
      command  register  bits    values
               address  to mod  to set
```

Only registers explicitly supporting Bit Modify will respond — not all registers do.

---

## Initialization Sequence

```
1. Assert /RESET (or send SPI RESET command 0xC0)
2. Wait ≥ 2 ms (power-on stabilization)
3. Verify Configuration mode (CANSTAT = 0x80)
4. Configure CNF1, CNF2, CNF3  ← bit timing
5. Configure acceptance masks (RXM0, RXM1)
6. Configure acceptance filters (RXF0–RXF5)
7. Configure RXBCTRL for RX buffer behavior
8. Configure TXBCTRL priority if needed
9. Configure CANINTE (enable desired interrupts)
10. Set CANCTRL to Normal mode (REQOP = 000)
11. Verify CANSTAT shows Normal mode
```

---

## Transmitting CAN Frames

### Transmission Steps

1. Check TXBnCTRL.TXREQ bit to confirm buffer is free
2. Write the frame: ID bytes → DLC → Data bytes
3. Set TXBnCTRL.TXREQ = 1 (or use RTS command)
4. Wait for TXREQ to clear (or INT assertion)
5. Check TXERR / MLOA / TXABT in TXBnCTRL on error

### TXB0 Register Sequence for Standard 11-bit ID

```
Register   Byte Value
─────────────────────────────────────────────────────
TXB0SIDH   ID[10:3]                    (bits 10 to 3)
TXB0SIDL   ID[2:0] << 5  | 0x00        (bits 2,1,0 in bits 7,6,5; EXIDE=0)
TXB0DLC    DLC[3:0]                    (0 to 8)
TXB0D0     data[0]
TXB0D1     data[1]
  ...
TXB0D7     data[7]
```

---

## Receiving CAN Frames

### Reception Steps

1. Detect /INT low **or** poll CANINTF for RXnIF flags
2. Identify which buffer received (CANINTF.RX0IF or RX1IF)
3. Read RXBnSIDH, RXBnSIDL → reconstruct ID
4. Read RXBnDLC for data length
5. Read RXBnD0–D7 for data bytes
6. Clear RXnIF by writing 0 to the flag bit

### RXB0SIDL Bit Layout

```
Bit 7   Bit 6   Bit 5   Bit 4   Bit 3  Bit 2   Bit 1   Bit 0
 SID2    SID1    SID0    SRR     IDE    EID17   EID16     —
```

- `IDE=0` → standard frame (11-bit ID)
- `IDE=1` → extended frame (29-bit ID)

---

## Interrupt Handling

### CANINTE / CANINTF Bit Layout

| Bit | Mask  | Name   | Description                          |
|-----|-------|--------|--------------------------------------|
|  0  | 0x01  | RX0IE  | RX Buffer 0 Full Interrupt Enable    |
|  1  | 0x02  | RX1IE  | RX Buffer 1 Full Interrupt Enable    |
|  2  | 0x04  | TX0IE  | TX Buffer 0 Empty Interrupt Enable   |
|  3  | 0x08  | TX1IE  | TX Buffer 1 Empty Interrupt Enable   |
|  4  | 0x10  | TX2IE  | TX Buffer 2 Empty Interrupt Enable   |
|  5  | 0x20  | ERRIE  | Error Interrupt Enable               |
|  6  | 0x40  | WAKIE  | Wake-up Interrupt Enable             |
|  7  | 0x80  | MERRE  | Message Error Interrupt Enable       |

### Interrupt Service Routine Flow

```
ISR triggered by /INT falling edge
    │
    ├─► Read CANINTF
    │
    ├─► RX0IF set? ──► Read RXB0 ──► clear RX0IF
    ├─► RX1IF set? ──► Read RXB1 ──► clear RX1IF
    ├─► TX0IF set? ──► Signal TX0 done ──► clear TX0IF
    ├─► ERRIF set? ──► Read EFLG / TEC / REC ──► handle error ──► clear ERRIF
    └─► All flags clear → release
```

---

## Bit Timing Configuration

### CAN Bit Segments

```
One CAN Bit Period:
┌──────────┬────────────────┬────────────────┬──────────────┐
│  SYNC_SEG│   PROP_SEG     │   PHASE_SEG1   │  PHASE_SEG2  │
│  (fixed) │  (1–8 TQ)      │  (1–8 TQ)      │  (1–8 TQ)    │
└──────────┴────────────────┴────────────────┴──────────────┘
                             ↑
                        Sample point
```

- **TQ** (Time Quantum) = (BRP + 1) × 2 / F_OSC
- **Bit Rate** = F_OSC / (2 × (BRP+1) × (1 + PropSeg + PhaseSeg1 + PhaseSeg2))

### Common CNF Register Values (8 MHz Crystal)

| Bit Rate  | CNF1  | CNF2  | CNF3  |
|-----------|-------|-------|-------|
| 1 Mbit/s  | 0x00  | 0x92  | 0x02  |
| 500 kbit/s| 0x00  | 0xF0  | 0x86  |
| 250 kbit/s| 0x01  | 0xF1  | 0x85  |
| 125 kbit/s| 0x03  | 0xF0  | 0x86  |

---

## Programming Examples — C/C++

### Header: `mcp2515.h`

```c
#ifndef MCP2515_H
#define MCP2515_H

#include <stdint.h>
#include <stdbool.h>

/* ── SPI Command Bytes ─────────────────────────────────────── */
#define MCP_RESET           0xC0
#define MCP_READ            0x03
#define MCP_WRITE           0x02
#define MCP_READ_STATUS     0xA0
#define MCP_RX_STATUS       0xB0
#define MCP_BIT_MODIFY      0x05
#define MCP_LOAD_TX0        0x40  /* Load TXB0 starting at SIDH  */
#define MCP_LOAD_TX1        0x42
#define MCP_LOAD_TX2        0x44
#define MCP_RTS_TX0         0x81  /* Request-to-send TXB0        */
#define MCP_RTS_TX1         0x82
#define MCP_RTS_TX2         0x84
#define MCP_READ_RX0        0x90  /* Read RXB0 starting at SIDH  */
#define MCP_READ_RX1        0x94

/* ── Register Addresses ────────────────────────────────────── */
#define MCP_CANSTAT         0x0E
#define MCP_CANCTRL         0x0F
#define MCP_CNF3            0x28
#define MCP_CNF2            0x29
#define MCP_CNF1            0x2A
#define MCP_CANINTE         0x2B
#define MCP_CANINTF         0x2C
#define MCP_EFLG            0x2D
#define MCP_TEC             0x1C
#define MCP_REC             0x1D

#define MCP_TXB0CTRL        0x30
#define MCP_TXB0SIDH        0x31
#define MCP_TXB0SIDL        0x32
#define MCP_TXB0EID8        0x33
#define MCP_TXB0EID0        0x34
#define MCP_TXB0DLC         0x35
#define MCP_TXB0D0          0x36

#define MCP_RXB0CTRL        0x60
#define MCP_RXB0SIDH        0x61
#define MCP_RXB0SIDL        0x62
#define MCP_RXB0EID8        0x63
#define MCP_RXB0EID0        0x64
#define MCP_RXB0DLC         0x65
#define MCP_RXB0D0          0x66

#define MCP_RXB1CTRL        0x70
#define MCP_RXB1SIDH        0x71
#define MCP_RXB1SIDL        0x72
#define MCP_RXB1EID8        0x73
#define MCP_RXB1EID0        0x74
#define MCP_RXB1DLC         0x75
#define MCP_RXB1D0          0x76

#define MCP_RXM0SIDH        0x20
#define MCP_RXM0SIDL        0x21
#define MCP_RXM1SIDH        0x24
#define MCP_RXM1SIDL        0x25
#define MCP_RXF0SIDH        0x00
#define MCP_RXF0SIDL        0x01

/* ── CANCTRL Bits ──────────────────────────────────────────── */
#define MCP_REQOP_NORMAL    0x00
#define MCP_REQOP_SLEEP     0x20
#define MCP_REQOP_LOOPBACK  0x40
#define MCP_REQOP_LISTEN    0x60
#define MCP_REQOP_CONFIG    0x80
#define MCP_REQOP_MASK      0xE0

/* ── CANINTF Bits ──────────────────────────────────────────── */
#define MCP_INT_RX0         0x01
#define MCP_INT_RX1         0x02
#define MCP_INT_TX0         0x04
#define MCP_INT_TX1         0x08
#define MCP_INT_TX2         0x10
#define MCP_INT_ERR         0x20
#define MCP_INT_WAKE        0x40
#define MCP_INT_MERR        0x80

/* ── TXBnCTRL Bits ─────────────────────────────────────────── */
#define MCP_TXREQ           0x08
#define MCP_TXP_HIGH        0x03
#define MCP_TXP_MED         0x02
#define MCP_TXP_LOW         0x01

/* ── RXBnCTRL Bits ─────────────────────────────────────────── */
#define MCP_RXB_RXM_ANY     0x60  /* Receive any message           */
#define MCP_RXB_RXM_EXT     0x40  /* Receive extended frames only  */
#define MCP_RXB_RXM_STD     0x20  /* Receive standard frames only  */
#define MCP_RXB_RXM_FILTER  0x00  /* Receive with filter matching  */
#define MCP_RXB0_BUKT       0x04  /* Roll-over to RXB1 if full     */

/* ── RXBnSIDL IDE Bit ──────────────────────────────────────── */
#define MCP_SIDL_EXIDE      0x08

/* ── Error Return Codes ────────────────────────────────────── */
typedef enum {
    MCP_OK            =  0,
    MCP_ERR_SPI       = -1,
    MCP_ERR_TIMEOUT   = -2,
    MCP_ERR_TX_BUSY   = -3,
    MCP_ERR_NOMSG     = -4,
    MCP_ERR_BAD_DLC   = -5,
} mcp_err_t;

/* ── CAN Frame Structure ────────────────────────────────────── */
typedef struct {
    uint32_t id;         /* 11-bit or 29-bit identifier          */
    bool     extended;   /* true = 29-bit extended frame         */
    bool     rtr;        /* true = Remote Transmission Request   */
    uint8_t  dlc;        /* Data length (0–8)                    */
    uint8_t  data[8];    /* Payload                              */
} can_frame_t;

/* ── Platform SPI HAL (user must implement) ─────────────────── */
void     spi_cs_assert   (void);          /* /CS → low            */
void     spi_cs_deassert (void);          /* /CS → high           */
uint8_t  spi_transfer    (uint8_t byte);  /* full-duplex 1-byte   */
void     delay_ms        (uint32_t ms);   /* blocking delay       */

/* ── Public API ─────────────────────────────────────────────── */
mcp_err_t mcp2515_reset      (void);
mcp_err_t mcp2515_init       (uint8_t cnf1, uint8_t cnf2, uint8_t cnf3);
mcp_err_t mcp2515_set_mode   (uint8_t mode);
mcp_err_t mcp2515_send       (const can_frame_t *frame);
mcp_err_t mcp2515_receive    (can_frame_t *frame, uint8_t *buf_index);
uint8_t   mcp2515_get_intf   (void);
void      mcp2515_clear_intf (uint8_t mask);
uint8_t   mcp2515_read_reg   (uint8_t addr);
void      mcp2515_write_reg  (uint8_t addr, uint8_t val);
void      mcp2515_bit_modify (uint8_t addr, uint8_t mask, uint8_t val);

#endif /* MCP2515_H */
```

### Implementation: `mcp2515.c`

```c
#include "mcp2515.h"
#include <string.h>

/* ── Low-level SPI helpers ──────────────────────────────────── */

uint8_t mcp2515_read_reg(uint8_t addr)
{
    uint8_t val;
    spi_cs_assert();
    spi_transfer(MCP_READ);
    spi_transfer(addr);
    val = spi_transfer(0xFF);       /* dummy byte to clock in data */
    spi_cs_deassert();
    return val;
}

void mcp2515_write_reg(uint8_t addr, uint8_t val)
{
    spi_cs_assert();
    spi_transfer(MCP_WRITE);
    spi_transfer(addr);
    spi_transfer(val);
    spi_cs_deassert();
}

/*
 * mcp2515_write_regs – burst-write consecutive registers.
 * More efficient than repeated single-byte writes.
 */
static void mcp2515_write_regs(uint8_t addr, const uint8_t *buf, uint8_t len)
{
    spi_cs_assert();
    spi_transfer(MCP_WRITE);
    spi_transfer(addr);
    for (uint8_t i = 0; i < len; i++) {
        spi_transfer(buf[i]);
    }
    spi_cs_deassert();
}

static void mcp2515_read_regs(uint8_t addr, uint8_t *buf, uint8_t len)
{
    spi_cs_assert();
    spi_transfer(MCP_READ);
    spi_transfer(addr);
    for (uint8_t i = 0; i < len; i++) {
        buf[i] = spi_transfer(0xFF);
    }
    spi_cs_deassert();
}

void mcp2515_bit_modify(uint8_t addr, uint8_t mask, uint8_t val)
{
    spi_cs_assert();
    spi_transfer(MCP_BIT_MODIFY);
    spi_transfer(addr);
    spi_transfer(mask);
    spi_transfer(val);
    spi_cs_deassert();
}

/* ── API Implementation ─────────────────────────────────────── */

mcp_err_t mcp2515_reset(void)
{
    spi_cs_assert();
    spi_transfer(MCP_RESET);
    spi_cs_deassert();
    delay_ms(10);  /* allow oscillator to stabilize */

    /* Verify we are in Configuration mode */
    uint8_t status = mcp2515_read_reg(MCP_CANSTAT);
    if ((status & MCP_REQOP_MASK) != MCP_REQOP_CONFIG) {
        return MCP_ERR_SPI;
    }
    return MCP_OK;
}

mcp_err_t mcp2515_init(uint8_t cnf1, uint8_t cnf2, uint8_t cnf3)
{
    mcp_err_t err;

    /* 1. Reset device */
    err = mcp2515_reset();
    if (err != MCP_OK) return err;

    /* 2. Set bit timing (must be in Configuration mode) */
    mcp2515_write_reg(MCP_CNF1, cnf1);
    mcp2515_write_reg(MCP_CNF2, cnf2);
    mcp2515_write_reg(MCP_CNF3, cnf3);

    /* 3. Configure RX buffers:
     *    RXB0 → accept any frame, roll-over to RXB1 if full
     *    RXB1 → accept any frame */
    mcp2515_write_reg(MCP_RXB0CTRL, MCP_RXB_RXM_ANY | MCP_RXB0_BUKT);
    mcp2515_write_reg(MCP_RXB1CTRL, MCP_RXB_RXM_ANY);

    /* 4. Zero out acceptance masks (accept all) */
    mcp2515_write_reg(MCP_RXM0SIDH, 0x00);
    mcp2515_write_reg(MCP_RXM0SIDL, 0x00);
    mcp2515_write_reg(MCP_RXM1SIDH, 0x00);
    mcp2515_write_reg(MCP_RXM1SIDL, 0x00);

    /* 5. Enable RX interrupts */
    mcp2515_write_reg(MCP_CANINTE, MCP_INT_RX0 | MCP_INT_RX1 | MCP_INT_ERR);

    /* 6. Switch to Normal mode */
    err = mcp2515_set_mode(MCP_REQOP_NORMAL);
    return err;
}

mcp_err_t mcp2515_set_mode(uint8_t mode)
{
    mcp2515_bit_modify(MCP_CANCTRL, MCP_REQOP_MASK, mode);

    /* Poll CANSTAT until mode is confirmed (with timeout) */
    for (uint32_t i = 0; i < 10; i++) {
        uint8_t status = mcp2515_read_reg(MCP_CANSTAT);
        if ((status & MCP_REQOP_MASK) == mode) {
            return MCP_OK;
        }
        delay_ms(1);
    }
    return MCP_ERR_TIMEOUT;
}

/*
 * mcp2515_send – transmit a CAN frame via TXB0.
 *
 * For higher throughput, a caller could cycle through TXB0/1/2
 * and track which is available. Here we use TXB0 exclusively.
 */
mcp_err_t mcp2515_send(const can_frame_t *frame)
{
    if (frame->dlc > 8) return MCP_ERR_BAD_DLC;

    /* Check TXB0 is free */
    uint8_t ctrl = mcp2515_read_reg(MCP_TXB0CTRL);
    if (ctrl & MCP_TXREQ) return MCP_ERR_TX_BUSY;

    uint8_t txbuf[13];  /* SIDH, SIDL, EID8, EID0, DLC, D0–D7 */
    memset(txbuf, 0, sizeof(txbuf));

    if (frame->extended) {
        /* 29-bit extended ID layout */
        uint32_t id = frame->id;
        txbuf[0] = (uint8_t)((id >> 21) & 0xFF);           /* EID[28:21] → SIDH */
        txbuf[1] = (uint8_t)(((id >> 18) & 0x07) << 5)     /* EID[20:18] → SIDL[7:5] */
                 | MCP_SIDL_EXIDE                            /* EXIDE = 1 */
                 | (uint8_t)((id >> 16) & 0x03);            /* EID[17:16] → SIDL[1:0] */
        txbuf[2] = (uint8_t)((id >> 8)  & 0xFF);           /* EID[15:8]  → EID8 */
        txbuf[3] = (uint8_t)( id        & 0xFF);            /* EID[7:0]   → EID0 */
    } else {
        /* 11-bit standard ID layout */
        uint16_t id = (uint16_t)(frame->id & 0x7FF);
        txbuf[0] = (uint8_t)(id >> 3);                     /* ID[10:3] → SIDH */
        txbuf[1] = (uint8_t)((id & 0x07) << 5);            /* ID[2:0]  → SIDL[7:5] */
    }

    txbuf[4] = frame->dlc & 0x0F;
    if (frame->rtr) txbuf[4] |= 0x40;                      /* RTR bit in DLC reg */

    memcpy(&txbuf[5], frame->data, frame->dlc);

    /* Burst-write entire TX buffer at once */
    mcp2515_write_regs(MCP_TXB0SIDH, txbuf, 5 + frame->dlc);

    /* Set TXREQ to start transmission */
    mcp2515_bit_modify(MCP_TXB0CTRL, MCP_TXREQ, MCP_TXREQ);

    /* Alternatively, use RTS command for slightly lower latency:
     *   spi_cs_assert();
     *   spi_transfer(MCP_RTS_TX0);
     *   spi_cs_deassert();
     */
    return MCP_OK;
}

/*
 * mcp2515_receive – read one frame from RXB0 or RXB1.
 * buf_index receives 0 or 1 to indicate which buffer was read.
 */
mcp_err_t mcp2515_receive(can_frame_t *frame, uint8_t *buf_index)
{
    uint8_t intf = mcp2515_read_reg(MCP_CANINTF);
    uint8_t base_addr;

    if (intf & MCP_INT_RX0) {
        base_addr  = MCP_RXB0SIDH;
        *buf_index = 0;
    } else if (intf & MCP_INT_RX1) {
        base_addr  = MCP_RXB1SIDH;
        *buf_index = 1;
    } else {
        return MCP_ERR_NOMSG;
    }

    uint8_t rxbuf[13];
    mcp2515_read_regs(base_addr, rxbuf, 5);    /* SIDH, SIDL, EID8, EID0, DLC */

    uint8_t sidl = rxbuf[1];
    bool extended = (sidl & MCP_SIDL_EXIDE) != 0;

    if (extended) {
        frame->extended = true;
        frame->id = ((uint32_t)(rxbuf[0]) << 21)
                  | ((uint32_t)((sidl >> 5) & 0x07) << 18)
                  | ((uint32_t)(sidl & 0x03) << 16)
                  | ((uint32_t)(rxbuf[2]) << 8)
                  |  (uint32_t)(rxbuf[3]);
    } else {
        frame->extended = false;
        frame->id = ((uint16_t)(rxbuf[0]) << 3) | ((sidl >> 5) & 0x07);
    }

    uint8_t dlc_reg = rxbuf[4];
    frame->rtr = (dlc_reg & 0x40) != 0;
    frame->dlc = dlc_reg & 0x0F;
    if (frame->dlc > 8) frame->dlc = 8;  /* clamp */

    if (frame->dlc > 0 && !frame->rtr) {
        mcp2515_read_regs(base_addr + 5, frame->data, frame->dlc);
    }

    /* Clear the interrupt flag to free the buffer */
    uint8_t clear_mask = (*buf_index == 0) ? MCP_INT_RX0 : MCP_INT_RX1;
    mcp2515_bit_modify(MCP_CANINTF, clear_mask, 0x00);

    return MCP_OK;
}

uint8_t mcp2515_get_intf(void)
{
    return mcp2515_read_reg(MCP_CANINTF);
}

void mcp2515_clear_intf(uint8_t mask)
{
    mcp2515_bit_modify(MCP_CANINTF, mask, 0x00);
}
```

### Usage Example: `main.c`

```c
#include "mcp2515.h"
#include <stdio.h>

/*
 * Platform-specific SPI and GPIO stubs — replace with your HAL.
 * Example shown for Linux userspace via spidev / gpiod.
 */
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/spi/spidev.h>
#include <gpiod.h>

static int spi_fd;
static struct gpiod_line *cs_line;
static struct gpiod_line *int_line;

void spi_cs_assert(void)   { gpiod_line_set_value(cs_line, 0); }
void spi_cs_deassert(void) { gpiod_line_set_value(cs_line, 1); }
void delay_ms(uint32_t ms) { usleep(ms * 1000); }

uint8_t spi_transfer(uint8_t byte)
{
    uint8_t rx = 0;
    struct spi_ioc_transfer tr = {
        .tx_buf        = (unsigned long)&byte,
        .rx_buf        = (unsigned long)&rx,
        .len           = 1,
        .speed_hz      = 4000000,   /* 4 MHz */
        .bits_per_word = 8,
    };
    ioctl(spi_fd, SPI_IOC_MESSAGE(1), &tr);
    return rx;
}

static void platform_init(void)
{
    spi_fd = open("/dev/spidev0.0", O_RDWR);
    uint8_t mode  = SPI_MODE_0;
    uint8_t bits  = 8;
    uint32_t speed = 4000000;
    ioctl(spi_fd, SPI_IOC_WR_MODE,           &mode);
    ioctl(spi_fd, SPI_IOC_WR_BITS_PER_WORD,  &bits);
    ioctl(spi_fd, SPI_IOC_WR_MAX_SPEED_HZ,   &speed);

    struct gpiod_chip *chip = gpiod_chip_open_by_name("gpiochip0");
    cs_line  = gpiod_chip_get_line(chip, 8);   /* GPIO8  = CE0 */
    int_line = gpiod_chip_get_line(chip, 25);  /* GPIO25 = /INT */
    gpiod_line_request_output(cs_line,  "mcp2515_cs",  1);
    gpiod_line_request_input (int_line, "mcp2515_int");
}

int main(void)
{
    platform_init();

    /*
     * 8 MHz crystal, 500 kbit/s:
     *   CNF1 = 0x00  (BRP = 0 → TQ = 250 ns at 8 MHz)
     *   CNF2 = 0xF0  (BTLMODE=1, SAM=1, PHSEG1=7, PRSEG=0)
     *   CNF3 = 0x86  (SOF=1, WAKFIL=0, PHSEG2=6)
     */
    mcp_err_t err = mcp2515_init(0x00, 0xF0, 0x86);
    if (err != MCP_OK) {
        fprintf(stderr, "MCP2515 init failed: %d\n", err);
        return 1;
    }
    printf("MCP2515 initialized at 500 kbit/s\n");

    /* ── Transmit a standard CAN frame ──────────────────────── */
    can_frame_t tx = {
        .id       = 0x123,
        .extended = false,
        .rtr      = false,
        .dlc      = 4,
        .data     = {0xDE, 0xAD, 0xBE, 0xEF},
    };
    err = mcp2515_send(&tx);
    printf("TX id=0x%03X  err=%d\n", tx.id, err);

    /* ── Poll for received frames ────────────────────────────── */
    while (1) {
        /* Check /INT pin — active low means data available */
        if (gpiod_line_get_value(int_line) == 0) {
            uint8_t buf_idx;
            can_frame_t rx;
            err = mcp2515_receive(&rx, &buf_idx);
            if (err == MCP_OK) {
                printf("RX buf=%u  id=%s0x%0*X  dlc=%u  data=",
                       buf_idx,
                       rx.extended ? "ext:" : "",
                       rx.extended ? 8 : 3,
                       rx.id,
                       rx.dlc);
                for (int i = 0; i < rx.dlc; i++) {
                    printf("%02X ", rx.data[i]);
                }
                printf("\n");
            }
            uint8_t intf = mcp2515_get_intf();
            if (intf & MCP_INT_ERR) {
                uint8_t eflg = mcp2515_read_reg(MCP_EFLG);
                uint8_t tec  = mcp2515_read_reg(MCP_TEC);
                uint8_t rec  = mcp2515_read_reg(MCP_REC);
                fprintf(stderr, "CAN error: EFLG=0x%02X TEC=%u REC=%u\n",
                        eflg, tec, rec);
                mcp2515_clear_intf(MCP_INT_ERR);
            }
        }
        delay_ms(1);
    }
    return 0;
}
```

---

## Programming Examples — Rust

### `Cargo.toml`

```toml
[package]
name    = "mcp2515_can"
version = "0.1.0"
edition = "2021"

[dependencies]
embedded-hal = "1.0"
linux-embedded-hal = "0.4"
spidev = "0.6"
sysfs-gpio = "0.6"
```

### `src/mcp2515.rs`

```rust
//! MCP2515 CAN controller driver using `embedded-hal` SPI traits.
//!
//! Works on any platform that implements `embedded_hal::spi::SpiBus`
//! and `embedded_hal::digital::OutputPin`.

use embedded_hal::{
    digital::OutputPin,
    spi::SpiBus,
};

// ── SPI Command Bytes ────────────────────────────────────────────
pub const CMD_RESET:        u8 = 0xC0;
pub const CMD_READ:         u8 = 0x03;
pub const CMD_WRITE:        u8 = 0x02;
pub const CMD_BIT_MODIFY:   u8 = 0x05;
pub const CMD_RTS_TXB0:     u8 = 0x81;
pub const CMD_READ_STATUS:  u8 = 0xA0;

// ── Register Addresses ───────────────────────────────────────────
pub const REG_CANSTAT:  u8 = 0x0E;
pub const REG_CANCTRL:  u8 = 0x0F;
pub const REG_CNF3:     u8 = 0x28;
pub const REG_CNF2:     u8 = 0x29;
pub const REG_CNF1:     u8 = 0x2A;
pub const REG_CANINTE:  u8 = 0x2B;
pub const REG_CANINTF:  u8 = 0x2C;
pub const REG_EFLG:     u8 = 0x2D;
pub const REG_TEC:      u8 = 0x1C;
pub const REG_REC:      u8 = 0x1D;

pub const REG_TXB0CTRL: u8 = 0x30;
pub const REG_TXB0SIDH: u8 = 0x31;
pub const REG_TXB0DLC:  u8 = 0x35;

pub const REG_RXB0CTRL: u8 = 0x60;
pub const REG_RXB0SIDH: u8 = 0x61;
pub const REG_RXB1CTRL: u8 = 0x70;
pub const REG_RXB1SIDH: u8 = 0x71;

pub const REG_RXM0SIDH: u8 = 0x20;
pub const REG_RXM1SIDH: u8 = 0x24;

// ── Bit Masks / Values ───────────────────────────────────────────
pub const REQOP_NORMAL:  u8 = 0x00;
pub const REQOP_SLEEP:   u8 = 0x20;
pub const REQOP_LOOPBACK:u8 = 0x40;
pub const REQOP_LISTEN:  u8 = 0x60;
pub const REQOP_CONFIG:  u8 = 0x80;
pub const REQOP_MASK:    u8 = 0xE0;

pub const INT_RX0:  u8 = 0x01;
pub const INT_RX1:  u8 = 0x02;
pub const INT_ERR:  u8 = 0x20;
pub const TXREQ:    u8 = 0x08;

pub const RXB_RXM_ANY:  u8 = 0x60;
pub const RXB0_BUKT:    u8 = 0x04;
pub const SIDL_EXIDE:   u8 = 0x08;

// ── Error Type ───────────────────────────────────────────────────
#[derive(Debug)]
pub enum Error<SpiErr, PinErr> {
    Spi(SpiErr),
    Pin(PinErr),
    Timeout,
    TxBusy,
    NoMessage,
    BadDlc,
}

impl<S, P> From<S> for Error<S, P> {
    fn from(e: S) -> Self { Error::Spi(e) }
}

// ── CAN Frame ────────────────────────────────────────────────────
#[derive(Debug, Clone, Default)]
pub struct CanFrame {
    pub id:       u32,
    pub extended: bool,
    pub rtr:      bool,
    pub dlc:      u8,
    pub data:     [u8; 8],
}

// ── Driver Struct ────────────────────────────────────────────────
pub struct Mcp2515<SPI, CS> {
    spi: SPI,
    cs:  CS,
}

impl<SPI, CS, SpiErr, PinErr> Mcp2515<SPI, CS>
where
    SPI: SpiBus<u8, Error = SpiErr>,
    CS:  OutputPin<Error = PinErr>,
{
    /// Create a new driver instance.
    pub fn new(spi: SPI, cs: CS) -> Self {
        Mcp2515 { spi, cs }
    }

    // ── Private helpers ─────────────────────────────────────────

    fn cs_assert(&mut self) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs.set_low().map_err(Error::Pin)
    }

    fn cs_deassert(&mut self) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs.set_high().map_err(Error::Pin)
    }

    fn transfer_byte(&mut self, byte: u8) -> Result<u8, Error<SpiErr, PinErr>> {
        let mut buf = [byte];
        self.spi.transfer_in_place(&mut buf)?;
        Ok(buf[0])
    }

    fn read_reg(&mut self, addr: u8) -> Result<u8, Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_READ)?;
        self.transfer_byte(addr)?;
        let val = self.transfer_byte(0xFF)?;
        self.cs_deassert()?;
        Ok(val)
    }

    fn write_reg(&mut self, addr: u8, val: u8) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_WRITE)?;
        self.transfer_byte(addr)?;
        self.transfer_byte(val)?;
        self.cs_deassert()?;
        Ok(())
    }

    fn write_regs(&mut self, addr: u8, data: &[u8]) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_WRITE)?;
        self.transfer_byte(addr)?;
        for &b in data {
            self.transfer_byte(b)?;
        }
        self.cs_deassert()?;
        Ok(())
    }

    fn read_regs(&mut self, addr: u8, buf: &mut [u8]) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_READ)?;
        self.transfer_byte(addr)?;
        for slot in buf.iter_mut() {
            *slot = self.transfer_byte(0xFF)?;
        }
        self.cs_deassert()?;
        Ok(())
    }

    pub fn bit_modify(
        &mut self,
        addr: u8,
        mask: u8,
        val:  u8,
    ) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_BIT_MODIFY)?;
        self.transfer_byte(addr)?;
        self.transfer_byte(mask)?;
        self.transfer_byte(val)?;
        self.cs_deassert()?;
        Ok(())
    }

    // ── Public API ──────────────────────────────────────────────

    /// Reset the MCP2515 via the SPI RESET command.
    pub fn reset(&mut self) -> Result<(), Error<SpiErr, PinErr>> {
        self.cs_assert()?;
        self.transfer_byte(CMD_RESET)?;
        self.cs_deassert()?;

        // Blocking delay — replace with your RTOS / HAL delay
        for _ in 0..100_000u32 { core::hint::black_box(()); }

        let stat = self.read_reg(REG_CANSTAT)?;
        if stat & REQOP_MASK != REQOP_CONFIG {
            return Err(Error::Timeout);
        }
        Ok(())
    }

    /// Initialize: reset, set bit timing, accept all messages, enter Normal mode.
    pub fn init(
        &mut self,
        cnf1: u8,
        cnf2: u8,
        cnf3: u8,
    ) -> Result<(), Error<SpiErr, PinErr>> {
        self.reset()?;

        // Bit timing (only writable in Configuration mode)
        self.write_reg(REG_CNF1, cnf1)?;
        self.write_reg(REG_CNF2, cnf2)?;
        self.write_reg(REG_CNF3, cnf3)?;

        // Accept any frame on both RX buffers; RXB0 rolls over to RXB1
        self.write_reg(REG_RXB0CTRL, RXB_RXM_ANY | RXB0_BUKT)?;
        self.write_reg(REG_RXB1CTRL, RXB_RXM_ANY)?;

        // Zero acceptance masks → all messages pass
        self.write_regs(REG_RXM0SIDH, &[0x00, 0x00, 0x00, 0x00])?;
        self.write_regs(REG_RXM1SIDH, &[0x00, 0x00, 0x00, 0x00])?;

        // Enable RX and error interrupts
        self.write_reg(REG_CANINTE, INT_RX0 | INT_RX1 | INT_ERR)?;

        // Switch to Normal mode
        self.set_mode(REQOP_NORMAL)
    }

    /// Change the operating mode and wait for confirmation.
    pub fn set_mode(&mut self, mode: u8) -> Result<(), Error<SpiErr, PinErr>> {
        self.bit_modify(REG_CANCTRL, REQOP_MASK, mode)?;
        for _ in 0..10u32 {
            let stat = self.read_reg(REG_CANSTAT)?;
            if stat & REQOP_MASK == mode {
                return Ok(());
            }
            // Small delay
            for _ in 0..10_000u32 { core::hint::black_box(()); }
        }
        Err(Error::Timeout)
    }

    /// Transmit a CAN frame via TXB0.
    pub fn send(&mut self, frame: &CanFrame) -> Result<(), Error<SpiErr, PinErr>> {
        if frame.dlc > 8 { return Err(Error::BadDlc); }

        let ctrl = self.read_reg(REG_TXB0CTRL)?;
        if ctrl & TXREQ != 0 { return Err(Error::TxBusy); }

        let mut buf = [0u8; 13]; // SIDH,SIDL,EID8,EID0,DLC,D0–D7

        if frame.extended {
            let id = frame.id;
            buf[0] = ((id >> 21) & 0xFF) as u8;
            buf[1] = (((id >> 18) & 0x07) as u8) << 5
                   | SIDL_EXIDE
                   | ((id >> 16) & 0x03) as u8;
            buf[2] = ((id >> 8) & 0xFF) as u8;
            buf[3] = (id & 0xFF) as u8;
        } else {
            let id = frame.id & 0x7FF;
            buf[0] = (id >> 3) as u8;
            buf[1] = ((id & 0x07) << 5) as u8;
        }

        buf[4] = frame.dlc & 0x0F;
        if frame.rtr { buf[4] |= 0x40; }
        buf[5..5 + frame.dlc as usize]
            .copy_from_slice(&frame.data[..frame.dlc as usize]);

        self.write_regs(REG_TXB0SIDH, &buf[..5 + frame.dlc as usize])?;
        self.bit_modify(REG_TXB0CTRL, TXREQ, TXREQ)?;
        Ok(())
    }

    /// Receive one CAN frame from RXB0 or RXB1.
    /// Returns `(frame, buffer_index)`.
    pub fn receive(&mut self) -> Result<(CanFrame, u8), Error<SpiErr, PinErr>> {
        let intf = self.read_reg(REG_CANINTF)?;

        let (base_addr, buf_idx, clear_mask) = if intf & INT_RX0 != 0 {
            (REG_RXB0SIDH, 0u8, INT_RX0)
        } else if intf & INT_RX1 != 0 {
            (REG_RXB1SIDH, 1u8, INT_RX1)
        } else {
            return Err(Error::NoMessage);
        };

        let mut header = [0u8; 5]; // SIDH, SIDL, EID8, EID0, DLC
        self.read_regs(base_addr, &mut header)?;

        let sidl      = header[1];
        let extended  = (sidl & SIDL_EXIDE) != 0;

        let id: u32 = if extended {
            ((header[0] as u32) << 21)
                | (((sidl >> 5) & 0x07) as u32) << 18
                | ((sidl & 0x03) as u32) << 16
                | ((header[2] as u32) << 8)
                |   header[3] as u32
        } else {
            ((header[0] as u32) << 3) | ((sidl >> 5) & 0x07) as u32
        };

        let dlc_reg = header[4];
        let rtr     = (dlc_reg & 0x40) != 0;
        let dlc     = (dlc_reg & 0x0F).min(8);

        let mut data = [0u8; 8];
        if dlc > 0 && !rtr {
            self.read_regs(base_addr + 5, &mut data[..dlc as usize])?;
        }

        // Clear the interrupt flag to release the hardware buffer
        self.bit_modify(REG_CANINTF, clear_mask, 0x00)?;

        Ok((CanFrame { id, extended, rtr, dlc, data }, buf_idx))
    }

    /// Read the interrupt flag register.
    pub fn interrupt_flags(&mut self) -> Result<u8, Error<SpiErr, PinErr>> {
        self.read_reg(REG_CANINTF)
    }

    /// Clear specified interrupt flags.
    pub fn clear_interrupts(&mut self, mask: u8) -> Result<(), Error<SpiErr, PinErr>> {
        self.bit_modify(REG_CANINTF, mask, 0x00)
    }

    /// Read TEC and REC error counters.
    pub fn error_counters(&mut self) -> Result<(u8, u8), Error<SpiErr, PinErr>> {
        let tec = self.read_reg(REG_TEC)?;
        let rec = self.read_reg(REG_REC)?;
        Ok((tec, rec))
    }
}
```

### `src/main.rs`

```rust
//! Example: MCP2515 driver on Linux (Raspberry Pi) using linux-embedded-hal.

use linux_embedded_hal::{
    spidev::{SpiModeFlags, SpidevOptions},
    Spidev,
    SysfsPin,
};
use embedded_hal::digital::OutputPin;
use mcp2515_can::mcp2515::{CanFrame, Mcp2515, INT_ERR, INT_RX0, INT_RX1};

mod mcp2515;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // ── Configure SPI ────────────────────────────────────────────
    let mut spi = Spidev::open("/dev/spidev0.0")?;
    let options = SpidevOptions::new()
        .bits_per_word(8)
        .max_speed_hz(4_000_000)
        .mode(SpiModeFlags::SPI_MODE_0)
        .build();
    spi.configure(&options)?;

    // ── Configure CS GPIO ────────────────────────────────────────
    let mut cs = SysfsPin::new(8); // BCM GPIO8 (SPI CE0)
    cs.export()?;
    cs.set_high()?;

    // ── Configure /INT GPIO ──────────────────────────────────────
    let int_pin = SysfsPin::new(25); // BCM GPIO25
    int_pin.export()?;
    int_pin.set_direction_in()?;

    // ── Instantiate driver ───────────────────────────────────────
    let mut can = Mcp2515::new(spi, cs);

    /*
     * 8 MHz crystal → 500 kbit/s
     * CNF1 = 0x00, CNF2 = 0xF0, CNF3 = 0x86
     */
    can.init(0x00, 0xF0, 0x86)?;
    println!("MCP2515 initialized at 500 kbit/s");

    // ── Transmit ─────────────────────────────────────────────────
    let frame = CanFrame {
        id:       0x123,
        extended: false,
        rtr:      false,
        dlc:      4,
        data:     [0xDE, 0xAD, 0xBE, 0xEF, 0, 0, 0, 0],
    };
    can.send(&frame)?;
    println!("Sent frame id=0x{:03X}", frame.id);

    // ── Receive loop ─────────────────────────────────────────────
    loop {
        // Active-low interrupt pin
        if int_pin.get_value()? == 0 {
            let intf = can.interrupt_flags()?;

            if intf & (INT_RX0 | INT_RX1) != 0 {
                match can.receive() {
                    Ok((rx, buf)) => {
                        print!(
                            "RX buf={} id={}0x{:0>width$X} dlc={} data=",
                            buf,
                            if rx.extended { "ext:" } else { "" },
                            rx.id,
                            rx.dlc,
                            width = if rx.extended { 8 } else { 3 }
                        );
                        for i in 0..rx.dlc as usize {
                            print!("{:02X} ", rx.data[i]);
                        }
                        println!();
                    }
                    Err(e) => eprintln!("Receive error: {:?}", e),
                }
            }

            if intf & INT_ERR != 0 {
                let (tec, rec) = can.error_counters()?;
                eprintln!("CAN error: TEC={} REC={}", tec, rec);
                can.clear_interrupts(INT_ERR)?;
            }
        }
        std::thread::sleep(std::time::Duration::from_millis(1));
    }
}
```

---

## Similar Devices

| Device            | Interface | CAN Standard  | Max Rate    | Notes                                    |
|-------------------|-----------|---------------|-------------|------------------------------------------|
| **MCP2515**       | SPI       | 2.0A / 2.0B   | 1 Mbit/s    | Classic, widely used, 3 TX / 2 RX bufs   |
| **MCP2517FD**     | SPI       | CAN FD        | 8 Mbit/s    | CAN FD, ECC RAM, low-power modes         |
| **MCP2518FD**     | SPI       | CAN FD        | 8 Mbit/s    | Pin-compatible with MCP2517FD            |
| **SJA1000**       | Parallel  | 2.0A / 2.0B   | 1 Mbit/s    | Legacy, common in industrial equipment   |
| **TJA1145**       | SPI       | 2.0A / 2.0B   | 1 Mbit/s    | Transceiver + partial networking         |
| **ATA6561**       | Standalone| Physical only | —           | Transceiver only; no controller logic    |

### MCP2515 vs. MCP2517FD Key Differences

| Feature             | MCP2515            | MCP2517FD                  |
|---------------------|--------------------|----------------------------|
| CAN FD support      | No                 | Yes (up to 64-byte payload)|
| Dual-rate bit timing| No                 | Yes (arb + data phase)     |
| SPI Clock           | 10 MHz max         | 85 MHz max                 |
| RAM                 | Register-based     | 2 KB message RAM (ECC)     |
| Transmit objects    | 3 buffers          | 31 FIFO/queue objects      |
| Receive objects     | 2 buffers          | 31 FIFO/queue objects      |
| Standby current     | ~1 µA (sleep)      | ~5 µA (sleep)              |

---

## Summary

The **MCP2515** is the canonical SPI-attached CAN controller, offering a complete CAN 2.0A/B implementation behind a straightforward 10 MHz SPI port. Its architecture — three prioritized TX buffers, two double-buffered RX buffers, six acceptance filters, and a rich interrupt system — covers the majority of CAN application needs without requiring a native CAN peripheral on the host MCU.

### Key Takeaways

- **SPI Mode 0,0** (CPOL=0, CPHA=0) at up to 10 MHz. The entire transaction must be encompassed within an active /CS assertion.
- **Configuration mode is mandatory** before writing CNF1–CNF3, acceptance masks, or filters. Always verify mode transitions via CANSTAT.
- **Bit Modify (0x05)** avoids software read-modify-write cycles for flag and control registers — use it for atomic operations on CANINTF and TXBnCTRL.
- **Burst SPI writes** (consecutive bytes with CS held low) dramatically reduce bus overhead when loading a full TX frame.
- **Interrupt-driven reception** via the /INT pin is strongly preferred over polling: it reduces latency and allows the host CPU to sleep between CAN events.
- **Error handling** requires monitoring TEC/REC and EFLG; the MCP2515 automatically transitions to error-passive and bus-off states per the CAN standard, but the host must detect and recover from bus-off by resetting.
- For **CAN FD** or more complex traffic management, migrate to the **MCP2517FD/MCP2518FD**, which offer pin-compatible footprints with the same SPI command paradigm but extend to 64-byte payloads and dual-rate bit timing.
- The **embedded-hal** abstractions in Rust make the driver genuinely portable — the same `Mcp2515<SPI, CS>` struct works on bare-metal ARM (no-std), RISC-V, or Linux userspace simply by swapping the SPI and GPIO implementations.