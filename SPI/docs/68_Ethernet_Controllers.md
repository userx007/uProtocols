# 68. SPI Ethernet Controllers

1. **Introduction** — why SPI Ethernet chips exist and where they fit
2. **Overview** — pinout, SPI wiring diagram, and a feature comparison table
3. **ENC28J60 Architecture** — internal block diagram, SPI opcode format, register bank system
4. **W5500 Architecture** — hardwired TCP/IP stack, SPI 3-byte frame format, block select encoding
5. **SPI Protocol** — timing diagram, Mode 0 requirements, clock speed limits
6. **C/C++ Examples** — four detailed, commented examples:
   - ENC28J60 full initialization (reset, buffers, MAC, PHY)
   - ENC28J60 packet send and receive (with ERXRDPT errata workaround)
   - W5500 full initialization (chip version check, IP/MAC/subnet/gateway config)
   - W5500 TCP connect/send/recv/close + HTTP GET example
7. **Rust Examples** — two `no_std` / `embedded-hal 1.0` drivers:
   - ENC28J60 generic driver with bank switching, PHY access, TX/RX
   - W5500 generic TCP socket driver
8. **Comparison table** — 14 attributes side-by-side with "when to choose" guidance
9. **Common Pitfalls** — ERXRDPT errata, bank overhead, dummy bytes, buffer sizing, reset timing, SPI mode, and hardware tips (magnetics, decoupling)
10. **Summary** — concise architectural recap of both chips and their ideal use cases

## Using SPI-to-Ethernet Chips like ENC28J60 and W5500

---

## Table of Contents

1. [Introduction](#introduction)
2. [Overview of SPI-to-Ethernet Chips](#overview)
3. [ENC28J60 – Architecture and Features](#enc28j60)
4. [W5500 – Architecture and Features](#w5500)
5. [SPI Communication Protocol](#spi-protocol)
6. [C/C++ Programming Examples](#c-cpp-examples)
   - [ENC28J60 Initialization](#enc28j60-init-c)
   - [ENC28J60 Packet Send/Receive](#enc28j60-packet-c)
   - [W5500 Initialization](#w5500-init-c)
   - [W5500 TCP Socket Communication](#w5500-tcp-c)
7. [Rust Programming Examples](#rust-examples)
   - [ENC28J60 SPI Driver in Rust](#enc28j60-rust)
   - [W5500 TCP Socket in Rust](#w5500-rust)
8. [Comparison: ENC28J60 vs W5500](#comparison)
9. [Common Pitfalls and Tips](#pitfalls)
10. [Summary](#summary)

---

## 1. Introduction 

Ethernet connectivity is a fundamental requirement for embedded systems that need wired network access — IoT devices, industrial controllers, network-attached instruments, and more. While some microcontrollers include a built-in MAC and PHY, the majority of low-cost or resource-constrained MCUs (such as AVR, STM32F0, RP2040, or bare-metal ARM Cortex-M) do not.

**SPI-to-Ethernet chips** solve this problem by bridging the gap: they implement the full Ethernet MAC (Media Access Control) and PHY (Physical Layer) in a dedicated IC, and expose a simple SPI interface to the host MCU. The host can then read/write Ethernet frames, configure network settings, and manage TCP/IP either via a software stack running on the MCU or (in more capable chips) via an on-chip hardware TCP/IP stack.

The two most widely used SPI Ethernet controllers in embedded and hobbyist systems are:

- **ENC28J60** – A raw Ethernet MAC+PHY with no built-in TCP/IP stack (Microchip Technology)
- **W5500** – A hardwired TCP/IP offload engine with built-in socket support (WIZnet)

Understanding both devices, their SPI command structures, register sets, and typical usage patterns is essential for firmware engineers building networked embedded applications.

---

## 2. Overview of SPI-to-Ethernet Chips

Both the ENC28J60 and W5500 connect to a host MCU using the standard 4-wire SPI bus:

```
MCU                          Ethernet Chip
-----                        -------------
MOSI  ─────────────────────► MOSI / SI
MISO  ◄─────────────────────  MISO / SO
SCLK  ─────────────────────► SCLK / SCLK
CS    ─────────────────────► CS / /SS
```

They also typically require:
- A **crystal or clock input** (typically 25 MHz for W5500, internal for ENC28J60)
- An **RJ-45 connector** (with or without integrated magnetics/transformer)
- A **reset pin** (active-low)
- An **interrupt pin** (INT, active-low, to notify the host of received packets or events)

The key architectural difference is:

| Feature | ENC28J60 | W5500 |
|---|---|---|
| TCP/IP stack | Software on MCU | Hardware offload |
| Sockets | Raw frames only | 8 hardware sockets |
| Max SPI speed | 20 MHz | 80 MHz |
| Duplex | Half | Full |
| Package | 28-DIP / SSOP | 48-QFN / 80-QFP |

---

## 3. ENC28J60 – Architecture and Features 

The **ENC28J60** is one of the most popular and affordable Ethernet controllers for AVR and ARM microcontrollers. It implements a 10BASE-T (10 Mbps) Ethernet MAC and PHY, with the host responsible for the entire TCP/IP software stack.

### Internal Architecture

```
 ┌─────────────────────────────────────────────────────┐
 │                     ENC28J60                        │
 │  ┌──────────┐   ┌───────────────┐   ┌─────────────┐ │
 │  │ SPI I/F  │──►│  Control Regs │──►│   Ethernet  │ │
 │  │20MHz max │   │  (Banks 0-3)  │   │    MAC      │ │
 │  └──────────┘   └───────────────┘   └──────┬──────┘ │
 │                 ┌───────────────┐          │        │
 │                 │  8 KB RX/TX   │◄─────────┘        │
 │                 │   Buffer RAM  │   ┌─────────────┐ │
 │                 └───────────────┘   │  Ethernet   │ │
 │                                     │    PHY      │ │
 │                                     └──────┬──────┘ │
 └────────────────────────────────────────────┼────────┘
                                              │
                                        RJ-45 / Magnetics
```

### Key Features

- **10BASE-T** Ethernet, half-duplex only
- **8 KB** on-chip receive/transmit buffer RAM (FIFO)
- Register banks: 4 banks of control registers, selected via ECON1
- Hardware CRC generation and checking
- **SPI opcodes**: Read Control Register (RCR), Write Control Register (WCR), Bit Field Set (BFS), Bit Field Clear (BFC), Read Buffer Memory (RBM), Write Buffer Memory (WBM), System Reset (SRC)
- Wake-on-LAN, link status change interrupt, and receive packet pending interrupt
- **No built-in TCP/IP** — requires a software network stack such as `uIP`, `lwIP`, or a custom implementation

### SPI Opcode Format

All SPI communication starts with a 1-byte opcode:

```
Bits [7:5] = Opcode
Bits [4:0] = Register Address (or don't care for buffer operations)

Opcodes:
  000 = Read Control Register  (RCR)
  001 = Read Buffer Memory     (RBM) — address = 11010
  010 = Write Control Register (WCR)
  011 = Write Buffer Memory    (WBM) — address = 11010
  100 = Bit Field Set          (BFS)
  101 = Bit Field Clear        (BFC)
  111 = System Reset           (SRC) — address = 11111
```

Example: to read register 0x1A in bank 0: send byte `0x00 | 0x1A = 0x1A`, then read the response byte.

---

## 4. W5500 – Architecture and Features <a name="w5500"></a>

The **W5500** by WIZnet integrates a **hardwired TCP/IP stack** into the chip itself. The MCU communicates via SPI using high-level socket operations (open, connect, send, receive, close), without needing to implement TCP/IP in firmware. This dramatically reduces firmware complexity and MCU processing overhead.

### Internal Architecture

```
 ┌─────────────────────────────────────────────────────┐
 │                       W5500                         │
 │  ┌──────────┐   ┌───────────────────────────────┐   │
 │  │  SPI I/F │──►│  Common Registers (IP, MAC...)│   │
 │  │(80MHz max│   └───────────────────────────────┘   │
 │  └──────────┘   ┌───────────────────────────────┐   │
 │                 │  Socket 0..7 Registers + Buf  │   │
 │                 │  (TCP/UDP/IPRAW/MACRAW)       │   │
 │                 └───────────────────────────────┘   │
 │                 ┌───────────────────────────────┐   │
 │                 │  Hardwired TCP/IP Stack       │   │
 │                 │  (ARP, IP, ICMP, TCP, UDP)    │   │
 │                 └──────────────┬────────────────┘   │
 │                                │  ┌──────────────┐  │
 │                                └─►│ 10/100 MAC+  │  │
 │                                   │PHY (100BASE-T│  │
 │                                   └──────┬───────┘  │
 └──────────────────────────────────────────┼──────────┘
                                            │
                                      RJ-45 / Magnetics
```

### Key Features

- **10/100BASE-T** (10 or 100 Mbps), full-duplex
- **8 independent hardware sockets** (TCP, UDP, IPRAW, MACRAW)
- Supports protocols: TCP, UDP, ICMP, IPv4, ARP, IGMP, PPPoE
- 32 KB internal RX/TX socket buffers (configurable per socket)
- SPI frame: 3-byte header (address[15:0] + control byte), then data
- **Control byte** encodes block select (common/socket N/TX buf/RX buf), read/write, and SPI mode (variable or fixed data length)
- Wake-on-LAN, interrupt per socket, link status detection
- **No TCP/IP software stack needed** on the MCU

### SPI Frame Format

```
Byte 0: Address High [15:8]
Byte 1: Address Low  [7:0]
Byte 2: Control
          [7:3] = Block Select
                  00000 = Common registers
                  00001 = Socket 0 registers  (00001 to 01111 = Sockets 0-7)
                  01000 = Socket 0 TX buffer
                  01001 = Socket 0 RX buffer
          [2]   = Read/Write (0=read, 1=write)
          [1:0] = SPI operation mode (00=Variable, 01=Fixed 1B, 10=Fixed 2B, 11=Fixed 4B)
Byte N: Data bytes (N = 1 to 65535 in variable mode)
```

---

## 5. SPI Communication Protocol <a name="spi-protocol"></a>

Both chips use **SPI Mode 0** (CPOL=0, CPHA=0): clock idle low, data sampled on the rising edge.

### Timing Considerations

```
CS   ─┐             ┌─
      └─────────────┘
CLK  ──┐ ┌─┐ ┌─┐ ┌──
       └─┘ └─┘ └─┘
MOSI ─┬─── D7..D0 ──┬─
MISO ─┴─── Q7..Q0 ──┴─

CS must be pulled LOW before and held LOW throughout the transaction.
CS must return HIGH between transactions.
ENC28J60: max 20 MHz SPI
W5500:    max 80 MHz SPI
```

Both chips require a dedicated chip select (CS) per device on a shared SPI bus.

---

## 6. C/C++ Programming Examples <a name="c-cpp-examples"></a>

The examples below assume a typical embedded C/C++ environment with low-level SPI primitives available. HAL functions like `spi_transfer()`, `cs_low()`, `cs_high()` map to your specific platform (Arduino, STM32 HAL, etc.).

---

### 6.1 ENC28J60 – Initialization in C <a name="enc28j60-init-c"></a>

```c
#include <stdint.h>
#include <string.h>

/* ── SPI platform primitives (implement for your MCU) ── */
extern void     spi_cs_low(void);
extern void     spi_cs_high(void);
extern uint8_t  spi_transfer(uint8_t byte);
extern void     delay_ms(uint32_t ms);

/* ── ENC28J60 SPI opcodes ── */
#define ENC_RCR(addr)   (0x00 | ((addr) & 0x1F))   /* Read Control Register  */
#define ENC_WCR(addr)   (0x40 | ((addr) & 0x1F))   /* Write Control Register */
#define ENC_BFS(addr)   (0x80 | ((addr) & 0x1F))   /* Bit Field Set          */
#define ENC_BFC(addr)   (0xA0 | ((addr) & 0x1F))   /* Bit Field Clear        */
#define ENC_RBM         0x3A                         /* Read Buffer Memory     */
#define ENC_WBM         0x7A                         /* Write Buffer Memory    */
#define ENC_SRC         0xFF                         /* System Reset           */

/* ── Register bank selection ── */
#define ECON1           0x1F   /* always accessible, all banks */
#define ECON1_BSEL0     (1<<0)
#define ECON1_BSEL1     (1<<1)
#define ECON1_RXEN      (1<<2)
#define ECON1_TXRTS     (1<<3)

/* ── Bank 0 registers ── */
#define ERDPTL          0x00
#define ERDPTH          0x01
#define EWRPTL          0x02
#define EWRPTH          0x03
#define ETXSTL          0x04
#define ETXSTH          0x05
#define ETXNDL          0x06
#define ETXNDH          0x07
#define ERXSTL          0x08
#define ERXSTH          0x09
#define ERXNDL          0x0A
#define ERXNDH          0x0B
#define ERXRDPTL        0x0C
#define ERXRDPTH        0x0D

/* ── Bank 2 registers ── */
#define MACON1          0x00   /* MAC Control 1                */
#define MACON3          0x02   /* MAC Control 3                */
#define MACON4          0x03
#define MABBIPG         0x04   /* Back-to-Back Inter-Packet Gap */
#define MAIPGL          0x06
#define MAIPGH          0x07
#define MAMXFLL         0x0A   /* Max Frame Length Low          */
#define MAMXFLH         0x0B   /* Max Frame Length High         */

/* ── Bank 3 registers ── */
#define MAADR1          0x04   /* MAC Address byte 1 (MSB)      */
#define MAADR2          0x05
#define MAADR3          0x02
#define MAADR4          0x03
#define MAADR5          0x00
#define MAADR6          0x01   /* MAC Address byte 6 (LSB)      */

/* ── PHY registers (accessed via MIREGADR/MIWRL/MIWRH) ── */
#define PHCON1          0x00
#define PHCON2          0x10
#define PHLCON          0x14

/* RX buffer: 0x0000 – 0x17FF (6 KB), TX buffer: 0x1800 – 0x1FFF (2 KB) */
#define RXSTART_INIT    0x0000
#define RXSTOP_INIT     0x17FF
#define TXSTART_INIT    0x1800
#define TXSTOP_INIT     0x1FFF

/* Maximum frame length */
#define MAX_FRAMELEN    1518

/* ------------------------------------------------------------------ */
/* Low-level register read/write                                       */
/* ------------------------------------------------------------------ */

static uint8_t current_bank = 0xFF;  /* force bank switch on first call */

static void enc_select_bank(uint8_t bank)
{
    if ((bank & 0x03) == current_bank) return;
    current_bank = bank & 0x03;
    /* Clear BSEL1:BSEL0, then set desired bank */
    spi_cs_low();
    spi_transfer(ENC_BFC(ECON1));
    spi_transfer(ECON1_BSEL1 | ECON1_BSEL0);
    spi_cs_high();
    if (current_bank) {
        spi_cs_low();
        spi_transfer(ENC_BFS(ECON1));
        spi_transfer(current_bank);
        spi_cs_high();
    }
}

static uint8_t enc_read_reg(uint8_t bank, uint8_t addr)
{
    enc_select_bank(bank);
    spi_cs_low();
    spi_transfer(ENC_RCR(addr));
    /* MAC/MII registers need a dummy byte before the real data */
    uint8_t val = (bank == 2 || bank == 3) ? spi_transfer(0x00) : 0;
    val = spi_transfer(0x00);
    spi_cs_high();
    return val;
}

static void enc_write_reg(uint8_t bank, uint8_t addr, uint8_t data)
{
    enc_select_bank(bank);
    spi_cs_low();
    spi_transfer(ENC_WCR(addr));
    spi_transfer(data);
    spi_cs_high();
}

static void enc_bfs(uint8_t addr, uint8_t mask)
{
    /* BFS/BFC only work on Ethernet registers, not MAC/MII */
    spi_cs_low();
    spi_transfer(ENC_BFS(addr));
    spi_transfer(mask);
    spi_cs_high();
}

static void enc_bfc(uint8_t addr, uint8_t mask)
{
    spi_cs_low();
    spi_transfer(ENC_BFC(addr));
    spi_transfer(mask);
    spi_cs_high();
}

/* ------------------------------------------------------------------ */
/* PHY register read/write (uses MIREGADR, MIWRL/MIWRH, MIRDL/MIRDH) */
/* ------------------------------------------------------------------ */
#define MIREGADR        0x14   /* bank 2 */
#define MIWRL           0x16
#define MIWRH           0x17
#define MISTAT          0x0A   /* bank 3 */
#define MISTAT_BUSY     (1<<0)
#define MIRDL           0x18   /* bank 2 */
#define MIRDH           0x19

static void enc_phy_write(uint8_t addr, uint16_t data)
{
    enc_write_reg(2, MIREGADR, addr);
    enc_write_reg(2, MIWRL,    (uint8_t)(data & 0xFF));
    enc_write_reg(2, MIWRH,    (uint8_t)(data >> 8));
    /* Wait until MII write completes (~10.24 µs) */
    delay_ms(1);
    while (enc_read_reg(3, MISTAT) & MISTAT_BUSY) {}
}

/* ------------------------------------------------------------------ */
/* ENC28J60 Initialization                                             */
/* ------------------------------------------------------------------ */

void enc28j60_init(const uint8_t mac[6])
{
    /* 1. Soft reset */
    spi_cs_low();
    spi_transfer(ENC_SRC);
    spi_cs_high();
    delay_ms(2);  /* datasheet requires ≥1 ms after reset */

    /* 2. Set up RX buffer (bank 0) */
    enc_write_reg(0, ERXSTL,   RXSTART_INIT & 0xFF);
    enc_write_reg(0, ERXSTH,   RXSTART_INIT >> 8);
    enc_write_reg(0, ERXNDL,   RXSTOP_INIT  & 0xFF);
    enc_write_reg(0, ERXNDH,   RXSTOP_INIT  >> 8);
    /* ERXRDPT must be set to RXSTOP for errata compliance */
    enc_write_reg(0, ERXRDPTL, RXSTOP_INIT  & 0xFF);
    enc_write_reg(0, ERXRDPTH, RXSTOP_INIT  >> 8);

    /* 3. Set TX buffer (bank 0) */
    enc_write_reg(0, ETXSTL,   TXSTART_INIT & 0xFF);
    enc_write_reg(0, ETXSTH,   TXSTART_INIT >> 8);

    /* 4. Configure MAC: MARXEN=receive enable, TXPAUS/RXPAUS=flow ctrl */
    enc_write_reg(2, MACON1,   0x0D);  /* MARXEN | TXPAUS | RXPAUS */
    enc_write_reg(2, MACON3,   0x32);  /* PADCFG0 | TXCRCEN | FRMLNEN | half-duplex */
    enc_write_reg(2, MACON4,   0x40);  /* DEFER bit for standard compliance */
    enc_write_reg(2, MABBIPG,  0x12);  /* half-duplex inter-packet gap */
    enc_write_reg(2, MAIPGL,   0xC2);
    enc_write_reg(2, MAIPGH,   0x12);
    enc_write_reg(2, MAMXFLL,  MAX_FRAMELEN & 0xFF);
    enc_write_reg(2, MAMXFLH,  MAX_FRAMELEN >> 8);

    /* 5. Program MAC address (bank 3) — loaded LSB-first in hardware */
    enc_write_reg(3, MAADR5,   mac[0]);
    enc_write_reg(3, MAADR6,   mac[1]);
    enc_write_reg(3, MAADR3,   mac[2]);
    enc_write_reg(3, MAADR4,   mac[3]);
    enc_write_reg(3, MAADR1,   mac[4]);
    enc_write_reg(3, MAADR2,   mac[5]);

    /* 6. Configure PHY: half-duplex, disable loopback */
    enc_phy_write(PHCON1,  0x0000);  /* half-duplex */
    enc_phy_write(PHCON2,  0x0100);  /* disable loopback (HDLDIS) */
    /* LED config: LEDA=link status, LEDB=RX/TX activity */
    enc_phy_write(PHLCON,  0x0476);

    /* 7. Enable receive */
    enc_bfs(ECON1, ECON1_RXEN);
}
```

---

### 6.2 ENC28J60 – Packet Send and Receive in C <a name="enc28j60-packet-c"></a>

```c
/* ---- Sending a raw Ethernet frame -------------------------------- */

#define ESTAT           0x1D   /* Ethernet Status – all banks */
#define ESTAT_CLKRDY    (1<<0)
#define ESTAT_TXABRT    (1<<1)
#define EIR             0x1C
#define EIR_TXERIF      (1<<1)
#define EIR_TXIF        (1<<3)

void enc28j60_send_packet(const uint8_t *data, uint16_t len)
{
    /* 1. Set EWRPT to start of TX buffer */
    enc_write_reg(0, EWRPTL, TXSTART_INIT & 0xFF);
    enc_write_reg(0, EWRPTH, TXSTART_INIT >> 8);

    /* 2. Write per-packet control byte (0x00 = use MACON3 settings) */
    spi_cs_low();
    spi_transfer(ENC_WBM);
    spi_transfer(0x00);
    spi_cs_high();

    /* 3. Write frame data to buffer RAM */
    spi_cs_low();
    spi_transfer(ENC_WBM);
    for (uint16_t i = 0; i < len; i++) {
        spi_transfer(data[i]);
    }
    spi_cs_high();

    /* 4. Set ETXND = start + 1 (control byte) + len - 1 */
    uint16_t txend = TXSTART_INIT + len;
    enc_write_reg(0, ETXNDL, txend & 0xFF);
    enc_write_reg(0, ETXNDH, txend >> 8);

    /* 5. Clear TX abort flag (errata workaround) */
    enc_bfs(ECON1, ECON1_TXRTS);
    if (enc_read_reg(0, ESTAT) & ESTAT_TXABRT) {
        enc_bfc(ECON1, ECON1_TXRTS);
        enc_bfs(0, EIR);   /* clear EIR.TXERIF */
    }

    /* 6. Initiate transmission */
    enc_bfs(ECON1, ECON1_TXRTS);

    /* 7. Wait for completion (poll EIR.TXIF or use interrupt) */
    uint16_t timeout = 10000;
    while (!(enc_read_reg(0, EIR) & (EIR_TXIF | EIR_TXERIF)) && --timeout) {}
    enc_bfc(ECON1, ECON1_TXRTS);
}


/* ---- Receiving a raw Ethernet frame ------------------------------ */

#define EPKTCNT         0x19   /* bank 1: received packet count       */
#define EIR_RXERIF      (1<<0)

static uint16_t next_packet_ptr = RXSTART_INIT;

uint16_t enc28j60_recv_packet(uint8_t *buf, uint16_t buflen)
{
    /* 1. Check if a packet is available */
    if (enc_read_reg(1, EPKTCNT) == 0) return 0;

    /* 2. Set read pointer to next packet */
    enc_write_reg(0, ERDPTL, next_packet_ptr & 0xFF);
    enc_write_reg(0, ERDPTH, next_packet_ptr >> 8);

    /* 3. Read 6-byte receive header:
          [1:0] = Next packet pointer
          [3:2] = Received byte count (includes CRC)
          [5:4] = Receive status vector              */
    uint8_t header[6];
    spi_cs_low();
    spi_transfer(ENC_RBM);
    for (int i = 0; i < 6; i++) header[i] = spi_transfer(0x00);
    spi_cs_high();

    next_packet_ptr = header[0] | ((uint16_t)header[1] << 8);
    uint16_t rx_len = (header[2] | ((uint16_t)header[3] << 8)) - 4; /* strip CRC */
    /* header[4] bit0 = receive OK */

    if (rx_len > buflen) rx_len = buflen;  /* clamp to buffer size */

    /* 4. Read packet payload */
    spi_cs_low();
    spi_transfer(ENC_RBM);
    for (uint16_t i = 0; i < rx_len; i++) buf[i] = spi_transfer(0x00);
    spi_cs_high();

    /* 5. Advance ERXRDPT (with even-address errata workaround) */
    uint16_t new_rdpt = (next_packet_ptr == RXSTART_INIT) ?
                         RXSTOP_INIT : (next_packet_ptr - 1);
    enc_write_reg(0, ERXRDPTL, new_rdpt & 0xFF);
    enc_write_reg(0, ERXRDPTH, new_rdpt >> 8);

    /* 6. Decrement packet count */
    enc_bfs(ECON2, 0x40);  /* ECON2.PKTDEC = 1 */

    return rx_len;
}
```

---

### 6.3 W5500 – Initialization in C <a name="w5500-init-c"></a>

```c
#include <stdint.h>
#include <string.h>

/* ── SPI platform primitives ── */
extern void    spi_cs_low(void);
extern void    spi_cs_high(void);
extern uint8_t spi_transfer(uint8_t byte);
extern void    delay_ms(uint32_t ms);

/* ── W5500 block select (BSB field, bits [7:3] of control byte) ── */
#define W55_BSB_COMMON     (0x00 << 3)  /* common registers  */
#define W55_BSB_S0_REG     (0x01 << 3)  /* socket 0 registers */
#define W55_BSB_S0_TX      (0x02 << 3)  /* socket 0 TX buffer */
#define W55_BSB_S0_RX      (0x03 << 3)  /* socket 0 RX buffer */
/* Sockets 1-7: BSB = (socket*4 + 1..3) << 3  */
#define W55_BSB_Sn_REG(n)  (((n)*4 + 1) << 3)
#define W55_BSB_Sn_TX(n)   (((n)*4 + 2) << 3)
#define W55_BSB_Sn_RX(n)   (((n)*4 + 3) << 3)

/* ── Control byte flags ── */
#define W55_RWB_READ       0x00
#define W55_RWB_WRITE      (1<<2)
#define W55_OM_VDM         0x00  /* variable data length mode */

/* ── Common register addresses ── */
#define W55_MR             0x0000  /* Mode register             */
#define W55_GAR            0x0001  /* Gateway IP address (4B)   */
#define W55_SUBR           0x0005  /* Subnet mask (4B)          */
#define W55_SHAR           0x0009  /* Source MAC address (6B)   */
#define W55_SIPR           0x000F  /* Source IP address (4B)    */
#define W55_PHYCFGR        0x002E  /* PHY configuration         */
#define W55_VERSIONR       0x0039  /* Chip version (read-only)  */

/* ── Socket register offsets (relative to socket base) ── */
#define Sn_MR              0x0000  /* Socket mode               */
#define Sn_CR              0x0001  /* Socket command            */
#define Sn_SR              0x0003  /* Socket status             */
#define Sn_PORT            0x0004  /* Source port (2B)          */
#define Sn_DIPR            0x000C  /* Destination IP (4B)       */
#define Sn_DPORT           0x0010  /* Destination port (2B)     */
#define Sn_TX_FSR          0x0020  /* TX free size (2B)         */
#define Sn_TX_WR           0x0024  /* TX write pointer (2B)     */
#define Sn_RX_RSR          0x0026  /* RX received size (2B)     */
#define Sn_RX_RD           0x0028  /* RX read pointer (2B)      */

/* ── Socket commands ── */
#define Sn_CR_OPEN         0x01
#define Sn_CR_LISTEN       0x02
#define Sn_CR_CONNECT      0x04
#define Sn_CR_DISCON       0x08
#define Sn_CR_CLOSE        0x10
#define Sn_CR_SEND         0x20
#define Sn_CR_RECV         0x40

/* ── Socket status ── */
#define Sn_SR_CLOSED       0x00
#define Sn_SR_INIT         0x13
#define Sn_SR_LISTEN       0x14
#define Sn_SR_ESTABLISHED  0x17
#define Sn_SR_CLOSE_WAIT   0x1C

/* ── Socket modes ── */
#define Sn_MR_TCP          0x01
#define Sn_MR_UDP          0x02

/* ── W5500 MR bits ── */
#define W55_MR_RST         (1<<7)  /* Software reset            */


/* ------------------------------------------------------------------ */
/* Low-level SPI read/write                                            */
/* ------------------------------------------------------------------ */

static void w5500_write(uint16_t addr, uint8_t bsb, const uint8_t *data, uint16_t len)
{
    spi_cs_low();
    spi_transfer((uint8_t)(addr >> 8));
    spi_transfer((uint8_t)(addr & 0xFF));
    spi_transfer(bsb | W55_RWB_WRITE | W55_OM_VDM);
    for (uint16_t i = 0; i < len; i++) spi_transfer(data[i]);
    spi_cs_high();
}

static void w5500_read(uint16_t addr, uint8_t bsb, uint8_t *data, uint16_t len)
{
    spi_cs_low();
    spi_transfer((uint8_t)(addr >> 8));
    spi_transfer((uint8_t)(addr & 0xFF));
    spi_transfer(bsb | W55_RWB_READ | W55_OM_VDM);
    for (uint16_t i = 0; i < len; i++) data[i] = spi_transfer(0x00);
    spi_cs_high();
}

static void w5500_write8(uint16_t addr, uint8_t bsb, uint8_t val)
{
    w5500_write(addr, bsb, &val, 1);
}

static uint8_t w5500_read8(uint16_t addr, uint8_t bsb)
{
    uint8_t val;
    w5500_read(addr, bsb, &val, 1);
    return val;
}

static void w5500_write16(uint16_t addr, uint8_t bsb, uint16_t val)
{
    uint8_t b[2] = { (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    w5500_write(addr, bsb, b, 2);
}

static uint16_t w5500_read16(uint16_t addr, uint8_t bsb)
{
    uint8_t b[2];
    w5500_read(addr, bsb, b, 2);
    return ((uint16_t)b[0] << 8) | b[1];
}


/* ------------------------------------------------------------------ */
/* W5500 Initialization                                                */
/* ------------------------------------------------------------------ */

void w5500_init(const uint8_t mac[6],
                const uint8_t ip[4],
                const uint8_t subnet[4],
                const uint8_t gateway[4])
{
    /* 1. Software reset */
    w5500_write8(W55_MR, W55_BSB_COMMON, W55_MR_RST);
    delay_ms(10);  /* wait for reset to complete */

    /* 2. Verify chip version (should be 0x04 for W5500) */
    uint8_t ver = w5500_read8(W55_VERSIONR, W55_BSB_COMMON);
    if (ver != 0x04) {
        /* Handle error: unexpected chip version */
        return;
    }

    /* 3. Configure MAC address */
    w5500_write(W55_SHAR, W55_BSB_COMMON, mac, 6);

    /* 4. Configure IP, subnet mask, gateway */
    w5500_write(W55_SIPR, W55_BSB_COMMON, ip,      4);
    w5500_write(W55_SUBR, W55_BSB_COMMON, subnet,  4);
    w5500_write(W55_GAR,  W55_BSB_COMMON, gateway, 4);

    /* 5. (Optional) Configure PHY: 100Mbps full-duplex auto-negotiation */
    w5500_write8(W55_PHYCFGR, W55_BSB_COMMON, 0xBF);

    /* 6. Set socket RX/TX buffer sizes (2KB each × 8 sockets = 32 KB total)
          Each socket gets 2KB: write 0x02 to each socket's buffer size reg */
    for (uint8_t s = 0; s < 8; s++) {
        w5500_write8(0x001E, W55_BSB_Sn_REG(s), 2);  /* Sn_RXBUF_SIZE = 2 KB */
        w5500_write8(0x001F, W55_BSB_Sn_REG(s), 2);  /* Sn_TXBUF_SIZE = 2 KB */
    }
}
```

---

### 6.4 W5500 – TCP Socket Communication in C <a name="w5500-tcp-c"></a>

```c
/* ------------------------------------------------------------------ */
/* W5500 TCP client: connect, send, receive, close                     */
/* ------------------------------------------------------------------ */

#define SOCK_TIMEOUT_MS  3000
#define MAX_RETRY        100

/* Send command to a socket and wait for it to be accepted */
static void socket_command(uint8_t sock, uint8_t cmd)
{
    w5500_write8(Sn_CR, W55_BSB_Sn_REG(sock), cmd);
    /* Command register auto-clears when accepted */
    uint16_t timeout = MAX_RETRY;
    while (w5500_read8(Sn_CR, W55_BSB_Sn_REG(sock)) && --timeout) {
        delay_ms(1);
    }
}

/* Open a TCP socket and connect to a remote host */
int w5500_tcp_connect(uint8_t sock,
                      const uint8_t remote_ip[4],
                      uint16_t remote_port,
                      uint16_t local_port)
{
    /* 1. Close if not already closed */
    socket_command(sock, Sn_CR_CLOSE);
    delay_ms(1);

    /* 2. Set mode to TCP */
    w5500_write8(Sn_MR, W55_BSB_Sn_REG(sock), Sn_MR_TCP);

    /* 3. Set local (source) port */
    w5500_write16(Sn_PORT, W55_BSB_Sn_REG(sock), local_port);

    /* 4. Open socket */
    socket_command(sock, Sn_CR_OPEN);

    /* 5. Verify status is INIT */
    if (w5500_read8(Sn_SR, W55_BSB_Sn_REG(sock)) != Sn_SR_INIT) return -1;

    /* 6. Set destination IP and port */
    w5500_write(Sn_DIPR,  W55_BSB_Sn_REG(sock), remote_ip, 4);
    w5500_write16(Sn_DPORT, W55_BSB_Sn_REG(sock), remote_port);

    /* 7. Issue CONNECT command */
    socket_command(sock, Sn_CR_CONNECT);

    /* 8. Wait for ESTABLISHED status */
    uint16_t timeout = SOCK_TIMEOUT_MS;
    while (--timeout) {
        uint8_t status = w5500_read8(Sn_SR, W55_BSB_Sn_REG(sock));
        if (status == Sn_SR_ESTABLISHED) return 0;
        if (status == Sn_SR_CLOSED)      return -2;
        delay_ms(1);
    }
    return -3;  /* timeout */
}

/* Send data over an established TCP socket */
int w5500_tcp_send(uint8_t sock, const uint8_t *data, uint16_t len)
{
    /* 1. Check free TX buffer space */
    uint16_t freesize;
    uint16_t timeout = MAX_RETRY;
    do {
        freesize = w5500_read16(Sn_TX_FSR, W55_BSB_Sn_REG(sock));
        if (freesize >= len) break;
        delay_ms(1);
    } while (--timeout);
    if (freesize < len) return -1;

    /* 2. Get current TX write pointer */
    uint16_t wr_ptr = w5500_read16(Sn_TX_WR, W55_BSB_Sn_REG(sock));

    /* 3. Write data to TX buffer (W5500 handles the circular wrap internally) */
    w5500_write(wr_ptr, W55_BSB_Sn_TX(sock), data, len);

    /* 4. Advance TX write pointer */
    w5500_write16(Sn_TX_WR, W55_BSB_Sn_REG(sock), wr_ptr + len);

    /* 5. Issue SEND command */
    socket_command(sock, Sn_CR_SEND);

    return (int)len;
}

/* Receive data from an established TCP socket (non-blocking) */
int w5500_tcp_recv(uint8_t sock, uint8_t *buf, uint16_t buflen)
{
    /* 1. Check how many bytes are available */
    uint16_t rxsize = w5500_read16(Sn_RX_RSR, W55_BSB_Sn_REG(sock));
    if (rxsize == 0) return 0;

    uint16_t to_read = (rxsize > buflen) ? buflen : rxsize;

    /* 2. Get current RX read pointer */
    uint16_t rd_ptr = w5500_read16(Sn_RX_RD, W55_BSB_Sn_REG(sock));

    /* 3. Read data from RX buffer */
    w5500_read(rd_ptr, W55_BSB_Sn_RX(sock), buf, to_read);

    /* 4. Advance RX read pointer */
    w5500_write16(Sn_RX_RD, W55_BSB_Sn_REG(sock), rd_ptr + to_read);

    /* 5. Issue RECV command to acknowledge bytes read */
    socket_command(sock, Sn_CR_RECV);

    return (int)to_read;
}

/* Close a TCP socket gracefully */
void w5500_tcp_close(uint8_t sock)
{
    socket_command(sock, Sn_CR_DISCON);
    delay_ms(10);
    socket_command(sock, Sn_CR_CLOSE);
}


/* ------------------------------------------------------------------ */
/* Example usage: send an HTTP GET request                             */
/* ------------------------------------------------------------------ */

void example_http_get(void)
{
    const uint8_t mac[6]     = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
    const uint8_t ip[4]      = {192, 168, 1, 100};
    const uint8_t subnet[4]  = {255, 255, 255, 0};
    const uint8_t gateway[4] = {192, 168, 1, 1};
    const uint8_t server[4]  = {93, 184, 216, 34};  /* example.com */

    w5500_init(mac, ip, subnet, gateway);

    if (w5500_tcp_connect(0, server, 80, 5000) != 0) return;

    const char *req = "GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
    w5500_tcp_send(0, (const uint8_t *)req, (uint16_t)strlen(req));

    uint8_t response[512];
    int received;
    while ((received = w5500_tcp_recv(0, response, sizeof(response) - 1)) > 0) {
        response[received] = '\0';
        /* process response... */
    }

    w5500_tcp_close(0);
}
```

---

## 7. Rust Programming Examples <a name="rust-examples"></a>

Rust's embedded ecosystem (the `embedded-hal` traits) provides a clean, type-safe abstraction over SPI hardware. The examples below use `embedded-hal` traits and follow `no_std` conventions for bare-metal use.

---

### 7.1 ENC28J60 SPI Driver in Rust <a name="enc28j60-rust"></a>

```rust
// Cargo.toml dependencies:
// embedded-hal = "1.0"
// nb = "1.0"

#![no_std]

use embedded_hal::spi::{SpiBus, SpiDevice};
use embedded_hal::digital::OutputPin;

/// ENC28J60 SPI opcodes
const RCR: u8 = 0x00; // Read Control Register
const WCR: u8 = 0x40; // Write Control Register
const BFS: u8 = 0x80; // Bit Field Set
const BFC: u8 = 0xA0; // Bit Field Clear
const RBM: u8 = 0x3A; // Read Buffer Memory
const WBM: u8 = 0x7A; // Write Buffer Memory
const SRC: u8 = 0xFF; // System Reset

/// Register bank layout
#[derive(Clone, Copy)]
enum Bank { B0, B1, B2, B3 }

/// ENC28J60 driver
pub struct Enc28j60<SPI, CS> {
    spi: SPI,
    cs:  CS,
    current_bank: Option<Bank>,
    next_packet: u16,
}

impl<SPI, CS, E> Enc28j60<SPI, CS>
where
    SPI: SpiBus<Error = E>,
    CS:  OutputPin,
{
    const RXSTART: u16 = 0x0000;
    const RXSTOP:  u16 = 0x17FF;
    const TXSTART: u16 = 0x1800;

    pub fn new(spi: SPI, cs: CS) -> Self {
        Self {
            spi,
            cs,
            current_bank: None,
            next_packet: Self::RXSTART,
        }
    }

    fn cs_low(&mut self)  { let _ = self.cs.set_low(); }
    fn cs_high(&mut self) { let _ = self.cs.set_high(); }

    fn transfer_byte(&mut self, byte: u8) -> u8 {
        let mut buf = [byte];
        let _ = self.spi.transfer_in_place(&mut buf);
        buf[0]
    }

    /// Send a 2-byte SPI transaction: opcode then data
    fn spi2(&mut self, opcode: u8, data: u8) -> u8 {
        self.cs_low();
        self.transfer_byte(opcode);
        let r = self.transfer_byte(data);
        self.cs_high();
        r
    }

    fn select_bank(&mut self, bank: Bank) {
        let bnum = bank as u8;
        // Clear BSEL bits
        self.cs_low();
        self.transfer_byte(BFC | 0x1F);  // BFC ECON1
        self.transfer_byte(0x03);         // BSEL1:BSEL0
        self.cs_high();
        // Set new bank
        if bnum != 0 {
            self.cs_low();
            self.transfer_byte(BFS | 0x1F);
            self.transfer_byte(bnum);
            self.cs_high();
        }
        self.current_bank = Some(bank);
    }

    fn read_reg(&mut self, bank: Bank, addr: u8) -> u8 {
        self.select_bank(bank);
        let is_mac_mii = matches!(bank, Bank::B2 | Bank::B3);
        self.cs_low();
        self.transfer_byte(RCR | (addr & 0x1F));
        if is_mac_mii { self.transfer_byte(0x00); } // dummy byte
        let val = self.transfer_byte(0x00);
        self.cs_high();
        val
    }

    fn write_reg(&mut self, bank: Bank, addr: u8, data: u8) {
        self.select_bank(bank);
        self.cs_low();
        self.transfer_byte(WCR | (addr & 0x1F));
        self.transfer_byte(data);
        self.cs_high();
    }

    fn write_reg16(&mut self, bank: Bank, lo_addr: u8, val: u16) {
        self.write_reg(bank, lo_addr,     (val & 0xFF) as u8);
        self.write_reg(bank, lo_addr + 1, (val >> 8)   as u8);
    }

    fn bfs_econ1(&mut self, mask: u8) {
        self.cs_low();
        self.transfer_byte(BFS | 0x1F);
        self.transfer_byte(mask);
        self.cs_high();
    }

    /// Software reset
    pub fn reset(&mut self) {
        self.cs_low();
        self.transfer_byte(SRC);
        self.cs_high();
    }

    /// Initialize the ENC28J60 with a given MAC address
    pub fn init(&mut self, mac: &[u8; 6]) {
        self.reset();
        // A small delay here is platform-dependent; caller should ensure
        // at least 1ms after reset before proceeding.

        // RX buffer
        self.write_reg16(Bank::B0, 0x08, Self::RXSTART);
        self.write_reg16(Bank::B0, 0x0A, Self::RXSTOP);
        self.write_reg16(Bank::B0, 0x0C, Self::RXSTOP); // ERXRDPT errata

        // TX buffer start
        self.write_reg16(Bank::B0, 0x04, Self::TXSTART);

        // MAC configuration
        self.write_reg(Bank::B2, 0x00, 0x0D); // MACON1: MARXEN | TXPAUS | RXPAUS
        self.write_reg(Bank::B2, 0x02, 0x32); // MACON3: PADCFG | TXCRCEN | FRMLNEN
        self.write_reg(Bank::B2, 0x04, 0x12); // MABBIPG half-duplex
        self.write_reg(Bank::B2, 0x06, 0xC2); // MAIPGL
        self.write_reg(Bank::B2, 0x07, 0x12); // MAIPGH
        self.write_reg16(Bank::B2, 0x0A, 1518); // MAMXFL

        // MAC address (order per ENC28J60 datasheet)
        self.write_reg(Bank::B3, 0x04, mac[0]);
        self.write_reg(Bank::B3, 0x05, mac[1]);
        self.write_reg(Bank::B3, 0x02, mac[2]);
        self.write_reg(Bank::B3, 0x03, mac[3]);
        self.write_reg(Bank::B3, 0x00, mac[4]);
        self.write_reg(Bank::B3, 0x01, mac[5]);

        // Enable receive
        self.bfs_econ1(0x04); // ECON1.RXEN
    }

    /// Transmit a raw Ethernet frame
    pub fn send_packet(&mut self, frame: &[u8]) {
        // Set write pointer to TX buffer start
        self.write_reg16(Bank::B0, 0x02, Self::TXSTART);

        // Write per-packet control byte (0x00 = use MACON3 defaults)
        self.cs_low();
        self.transfer_byte(WBM);
        self.transfer_byte(0x00);
        self.cs_high();

        // Write frame bytes
        self.cs_low();
        self.transfer_byte(WBM);
        for &byte in frame { self.transfer_byte(byte); }
        self.cs_high();

        // Set TX end pointer
        let txend = Self::TXSTART + frame.len() as u16;
        self.write_reg16(Bank::B0, 0x06, txend);

        // Initiate transmission
        self.bfs_econ1(0x08); // ECON1.TXRTS
    }

    /// Receive a raw Ethernet frame; returns the number of bytes read
    pub fn recv_packet(&mut self, buf: &mut [u8]) -> usize {
        // Check packet count (bank 1, register 0x19)
        if self.read_reg(Bank::B1, 0x19) == 0 { return 0; }

        // Set read pointer
        let npp = self.next_packet;
        self.write_reg16(Bank::B0, 0x00, npp);

        // Read 6-byte receive header
        let mut header = [0u8; 6];
        self.cs_low();
        self.transfer_byte(RBM);
        for b in header.iter_mut() { *b = self.transfer_byte(0x00); }
        self.cs_high();

        self.next_packet = (header[0] as u16) | ((header[1] as u16) << 8);
        let rx_len = ((header[2] as u16) | ((header[3] as u16) << 8))
            .saturating_sub(4) as usize; // strip CRC

        let to_read = rx_len.min(buf.len());

        // Read payload
        self.cs_low();
        self.transfer_byte(RBM);
        for b in buf[..to_read].iter_mut() { *b = self.transfer_byte(0x00); }
        self.cs_high();

        // Advance ERXRDPT (errata: must be odd)
        let new_rdpt = if self.next_packet == Self::RXSTART {
            Self::RXSTOP
        } else {
            self.next_packet - 1
        };
        self.write_reg16(Bank::B0, 0x0C, new_rdpt);

        // Decrement packet counter: BFS ECON2 (0x1E), bit 6
        self.cs_low();
        self.transfer_byte(BFS | 0x1E);
        self.transfer_byte(0x40);
        self.cs_high();

        to_read
    }
}
```

---

### 7.2 W5500 TCP Socket in Rust <a name="w5500-rust"></a>

```rust
// Cargo.toml:
// embedded-hal = "1.0"
// w5500-ll = "0.12"   (optional community crate; shown here is the manual approach)

#![no_std]

use embedded_hal::spi::SpiBus;
use embedded_hal::digital::OutputPin;

/// W5500 block select byte helpers
fn bsb_common()        -> u8 { 0x00 << 3 }
fn bsb_sn_reg(sock: u8) -> u8 { (sock * 4 + 1) << 3 }
fn bsb_sn_tx(sock: u8)  -> u8 { (sock * 4 + 2) << 3 }
fn bsb_sn_rx(sock: u8)  -> u8 { (sock * 4 + 3) << 3 }

const WRITE: u8 = 1 << 2;
const READ:  u8 = 0;

/// Socket commands
const CMD_OPEN:    u8 = 0x01;
const CMD_CONNECT: u8 = 0x04;
const CMD_DISCON:  u8 = 0x08;
const CMD_CLOSE:   u8 = 0x10;
const CMD_SEND:    u8 = 0x20;
const CMD_RECV:    u8 = 0x40;

/// Socket statuses
const SR_CLOSED:      u8 = 0x00;
const SR_INIT:        u8 = 0x13;
const SR_ESTABLISHED: u8 = 0x17;

/// Common register addresses
const REG_MR:      u16 = 0x0000;
const REG_GAR:     u16 = 0x0001;
const REG_SUBR:    u16 = 0x0005;
const REG_SHAR:    u16 = 0x0009;
const REG_SIPR:    u16 = 0x000F;
const REG_VERSION: u16 = 0x0039;

/// Socket register offsets
const Sn_MR:    u16 = 0x0000;
const Sn_CR:    u16 = 0x0001;
const Sn_SR:    u16 = 0x0003;
const Sn_PORT:  u16 = 0x0004;
const Sn_DIPR:  u16 = 0x000C;
const Sn_DPORT: u16 = 0x0010;
const Sn_TX_FSR: u16 = 0x0020;
const Sn_TX_WR:  u16 = 0x0024;
const Sn_RX_RSR: u16 = 0x0026;
const Sn_RX_RD:  u16 = 0x0028;

/// W5500 driver
pub struct W5500<SPI, CS> {
    spi: SPI,
    cs:  CS,
}

impl<SPI, CS, E> W5500<SPI, CS>
where
    SPI: SpiBus<Error = E>,
    CS:  OutputPin,
{
    pub fn new(spi: SPI, cs: CS) -> Self {
        Self { spi, cs }
    }

    fn cs_low(&mut self)  { let _ = self.cs.set_low(); }
    fn cs_high(&mut self) { let _ = self.cs.set_high(); }

    fn write_bytes(&mut self, addr: u16, bsb: u8, data: &[u8]) {
        self.cs_low();
        let header = [(addr >> 8) as u8, addr as u8, bsb | WRITE];
        let _ = self.spi.write(&header);
        let _ = self.spi.write(data);
        self.cs_high();
    }

    fn read_bytes(&mut self, addr: u16, bsb: u8, buf: &mut [u8]) {
        self.cs_low();
        let header = [(addr >> 8) as u8, addr as u8, bsb | READ];
        let _ = self.spi.write(&header);
        let _ = self.spi.read(buf);
        self.cs_high();
    }

    fn write8(&mut self, addr: u16, bsb: u8, val: u8) {
        self.write_bytes(addr, bsb, &[val]);
    }

    fn read8(&mut self, addr: u16, bsb: u8) -> u8 {
        let mut buf = [0u8; 1];
        self.read_bytes(addr, bsb, &mut buf);
        buf[0]
    }

    fn write16(&mut self, addr: u16, bsb: u8, val: u16) {
        self.write_bytes(addr, bsb, &[(val >> 8) as u8, val as u8]);
    }

    fn read16(&mut self, addr: u16, bsb: u8) -> u16 {
        let mut buf = [0u8; 2];
        self.read_bytes(addr, bsb, &mut buf);
        ((buf[0] as u16) << 8) | buf[1] as u16
    }

    /// Initialize chip: set MAC, IP, subnet, gateway
    pub fn init(
        &mut self,
        mac:     &[u8; 6],
        ip:      &[u8; 4],
        subnet:  &[u8; 4],
        gateway: &[u8; 4],
    ) -> Result<(), ()> {
        // Software reset
        self.write8(REG_MR, bsb_common(), 0x80);
        // Caller must wait ~10ms after reset

        // Verify chip version
        let ver = self.read8(REG_VERSION, bsb_common());
        if ver != 0x04 { return Err(()); }

        self.write_bytes(REG_SHAR, bsb_common(), mac);
        self.write_bytes(REG_SIPR, bsb_common(), ip);
        self.write_bytes(REG_SUBR, bsb_common(), subnet);
        self.write_bytes(REG_GAR,  bsb_common(), gateway);

        // 2KB TX/RX buffers per socket
        for s in 0..8u8 {
            self.write8(0x001E, bsb_sn_reg(s), 2);
            self.write8(0x001F, bsb_sn_reg(s), 2);
        }

        Ok(())
    }

    fn socket_cmd(&mut self, sock: u8, cmd: u8) {
        self.write8(Sn_CR, bsb_sn_reg(sock), cmd);
        // Poll until command register clears (auto-clears when accepted)
        for _ in 0..1000 {
            if self.read8(Sn_CR, bsb_sn_reg(sock)) == 0 { break; }
        }
    }

    /// Open a TCP socket and connect to a remote endpoint
    /// Returns Ok(()) when connection is established
    pub fn tcp_connect(
        &mut self,
        sock:        u8,
        remote_ip:   &[u8; 4],
        remote_port: u16,
        local_port:  u16,
    ) -> Result<(), ()> {
        self.socket_cmd(sock, CMD_CLOSE);
        self.write8(Sn_MR, bsb_sn_reg(sock), 0x01); // TCP mode
        self.write16(Sn_PORT, bsb_sn_reg(sock), local_port);
        self.socket_cmd(sock, CMD_OPEN);

        if self.read8(Sn_SR, bsb_sn_reg(sock)) != SR_INIT {
            return Err(());
        }

        self.write_bytes(Sn_DIPR,  bsb_sn_reg(sock), remote_ip);
        self.write16(Sn_DPORT, bsb_sn_reg(sock), remote_port);
        self.socket_cmd(sock, CMD_CONNECT);

        // Poll for ESTABLISHED (caller should add a timeout in production)
        for _ in 0..30_000u32 {
            let status = self.read8(Sn_SR, bsb_sn_reg(sock));
            match status {
                SR_ESTABLISHED => return Ok(()),
                SR_CLOSED      => return Err(()),
                _              => {}
            }
        }
        Err(()) // timeout
    }

    /// Send data over an established TCP socket
    pub fn tcp_send(&mut self, sock: u8, data: &[u8]) -> Result<usize, ()> {
        let len = data.len() as u16;

        // Wait for TX free space
        for _ in 0..1000u32 {
            if self.read16(Sn_TX_FSR, bsb_sn_reg(sock)) >= len { break; }
        }
        if self.read16(Sn_TX_FSR, bsb_sn_reg(sock)) < len {
            return Err(());
        }

        let wr = self.read16(Sn_TX_WR, bsb_sn_reg(sock));
        self.write_bytes(wr, bsb_sn_tx(sock), data);
        self.write16(Sn_TX_WR, bsb_sn_reg(sock), wr.wrapping_add(len));
        self.socket_cmd(sock, CMD_SEND);

        Ok(data.len())
    }

    /// Receive available data from a TCP socket (non-blocking)
    pub fn tcp_recv(&mut self, sock: u8, buf: &mut [u8]) -> usize {
        let avail = self.read16(Sn_RX_RSR, bsb_sn_reg(sock)) as usize;
        if avail == 0 { return 0; }

        let to_read = avail.min(buf.len());
        let rd = self.read16(Sn_RX_RD, bsb_sn_reg(sock));
        self.read_bytes(rd, bsb_sn_rx(sock), &mut buf[..to_read]);
        self.write16(Sn_RX_RD, bsb_sn_reg(sock), rd.wrapping_add(to_read as u16));
        self.socket_cmd(sock, CMD_RECV);

        to_read
    }

    /// Close a socket
    pub fn tcp_close(&mut self, sock: u8) {
        self.socket_cmd(sock, CMD_DISCON);
        self.socket_cmd(sock, CMD_CLOSE);
    }
}


/// Example: HTTP GET over W5500 TCP (no_std, illustrative)
#[allow(dead_code)]
fn example_usage<SPI: SpiBus, CS: OutputPin>(mut driver: W5500<SPI, CS>) {
    let mac     = [0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED];
    let ip      = [192, 168, 1, 100];
    let subnet  = [255, 255, 255, 0];
    let gateway = [192, 168, 1, 1];
    let server  = [93, 184, 216, 34]; // example.com

    let _ = driver.init(&mac, &ip, &subnet, &gateway);
    // Add delay_ms(10) here for reset settling

    if driver.tcp_connect(0, &server, 80, 1025).is_ok() {
        let req = b"GET / HTTP/1.0\r\nHost: example.com\r\n\r\n";
        let _ = driver.tcp_send(0, req);

        let mut buf = [0u8; 256];
        loop {
            let n = driver.tcp_recv(0, &mut buf);
            if n == 0 { break; }
            // process buf[..n]
        }
        driver.tcp_close(0);
    }
}
```

---

## 8. Comparison: ENC28J60 vs W5500 <a name="comparison"></a>

| Attribute | ENC28J60 | W5500 |
|---|---|---|
| **Speed** | 10 Mbps (half-duplex) | 10/100 Mbps (full-duplex) |
| **TCP/IP stack** | Software (MCU) | Hardwired (chip) |
| **SPI speed** | Up to 20 MHz | Up to 80 MHz |
| **Sockets** | None (raw frames) | 8 hardware sockets |
| **MCU RAM needed** | High (stack buffers) | Minimal (socket I/O only) |
| **MCU CPU load** | High (software TCP/IP) | Very low (offloaded) |
| **Internal buffer** | 8 KB shared RX/TX | 32 KB (configurable per socket) |
| **Protocols** | Any (you implement) | TCP, UDP, ICMP, IGMP, PPPoE |
| **Complexity** | High firmware | Low firmware |
| **Cost** | Very low (~$1) | Moderate (~$3–5) |
| **Package** | DIP-28 (breadboard friendly) | QFN-48 (SMD only) |
| **Typical use** | Low-cost raw networking, learning | Production IoT, quick integration |
| **Community libraries** | UIPEthernet, EtherCard | Ethernet (Arduino), W5500 HAL |
| **Rust crate** | `enc28j60` (crates.io) | `w5500-ll`, `w5500` (crates.io) |

### When to choose ENC28J60

- Learning embedded networking from the ground up
- Very cost-sensitive designs
- Need custom protocols (non-TCP/IP) — e.g., raw Ethernet frames, custom Layer 2
- Already have a software TCP/IP stack (like lwIP) in your system

### When to choose W5500

- Fastest path to a working networked product
- MCU with limited RAM or CPU cycles (no room for lwIP)
- Multiple simultaneous TCP/UDP connections needed
- 100 Mbps throughput required
- Production IoT devices where reliability matters

---

## 9. Common Pitfalls and Tips <a name="pitfalls"></a>

### ENC28J60 Pitfalls

**Errata workaround for ERXRDPT:** The ERXRDPT register must always be set to an odd address. A common bug is setting it to `next_packet - 1` without checking that it doesn't underflow the RX buffer start, or forgetting the odd-address constraint. Always apply the workaround shown in the code above.

**Bank switching overhead:** Every MAC/MII register access requires a bank switch (two SPI transactions). Minimize bank switches by grouping reads/writes by bank, or cache the current bank selection.

**MAC/MII dummy byte:** Reads from bank 2 and bank 3 (MAC and MII registers) require an extra dummy byte after the opcode but before the real data byte. Forgetting this returns incorrect values.

**SPI speed limit:** The ENC28J60 cannot reliably exceed 20 MHz SPI. Many users push their MCU SPI bus to its maximum and see corrupted data with the ENC28J60 when it's above spec.

**Half-duplex only:** The ENC28J60 does not support full-duplex operation. If connected to a switch that forces full-duplex, communication will be unreliable. Ensure your switch port is set to half-duplex or auto-negotiate to half-duplex.

### W5500 Pitfalls

**Chip version check:** Always read `VERSIONR` (0x0039) after reset. It should return `0x04`. If it returns `0x00` or garbage, the SPI wiring or timing is incorrect.

**TX pointer management:** The `Sn_TX_WR` pointer is 16-bit and wraps around within the allocated socket TX buffer. The W5500 handles the circular wrap automatically — you write to the physical address given by `Sn_TX_WR` and the chip maps it into the circular buffer. However, you must correctly advance the pointer by the number of bytes sent.

**Buffer size configuration:** The 32 KB of internal buffer is shared across all 8 sockets. Default is 2KB TX + 2KB RX per socket. If you only use 1 socket, you can allocate up to 16KB TX + 16KB RX for higher throughput. Setting sizes incorrectly can cause silent data corruption.

**Reset timing:** After issuing a software reset (writing `MR.RST = 1`), the W5500 requires approximately 10 ms before registers can be reliably accessed. Always add a delay after reset.

### General SPI Ethernet Tips

- **Use a dedicated CS pin** — never share CS with other devices if the Ethernet controller is on a shared SPI bus. The controller must be fully deselected between transactions.
- **Add a 100 nF decoupling capacitor** close to the chip's VCC pin. SPI Ethernet chips have significant transient current draw during transmit.
- **Use proper RJ-45 with integrated magnetics** — the 1:1 isolation transformer in the RJ-45 jack is required by IEEE 802.3. Using a connector without magnetics will result in unreliable (or no) link establishment.
- **Pull INT low with a 10kΩ resistor** to a known state if not using the interrupt pin actively; a floating INT can cause spurious MCU interrupts.
- **Watch for SPI clock phase/polarity** — both chips require SPI Mode 0 (CPOL=0, CPHA=0). Configure your MCU SPI peripheral accordingly.

---

## 10. Summary <a name="summary"></a>

SPI-to-Ethernet controllers bridge the gap between SPI-capable microcontrollers and wired Ethernet networks. The **ENC28J60** and **W5500** represent two distinct design philosophies:

The **ENC28J60** is a minimal, low-cost 10BASE-T MAC+PHY with no built-in TCP/IP. It exposes raw Ethernet frames over SPI and relies entirely on firmware (such as lwIP or uIP) to implement ARP, IP, TCP, and UDP. Its simple SPI opcode set — RCR, WCR, BFS, BFC, RBM, WBM — maps naturally to low-level C/C++ and Rust drivers. Key implementation considerations include bank selection, MAC/MII dummy-read requirements, and the ERXRDPT errata workaround. It is ideally suited for educational projects, ultra-cost-sensitive designs, or applications requiring custom Layer 2 protocols.

The **W5500** is a full-featured 10/100BASE-T controller with a **hardwired TCP/IP stack** inside the chip itself. The MCU communicates using a straightforward 3-byte SPI header (address + control byte) followed by register or buffer data, and issues high-level socket commands (OPEN, CONNECT, SEND, RECV, CLOSE) without needing any TCP/IP code in firmware. With 8 independent hardware sockets, 32 KB of configurable buffer RAM, and support for full-duplex 100 Mbps, the W5500 is the preferred choice for production IoT applications, resource-constrained MCUs, and any scenario where rapid integration and reliable networking are priorities.

Both devices integrate cleanly with the `embedded-hal` SPI abstraction in Rust, making them suitable for `no_std` bare-metal firmware. In C/C++, thin HAL wrappers over platform-specific SPI primitives provide the portability needed to run the same driver logic on STM32, AVR, RP2040, or any other SPI-capable MCU.

---

*Document covers: ENC28J60 (Microchip) and W5500 (WIZnet) SPI Ethernet controllers. Code examples target embedded C/C++ and Rust (`no_std`, `embedded-hal 1.0`). All register addresses and opcode values are derived from the respective manufacturer datasheets.*