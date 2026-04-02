Here's the complete document — a deep dive covering:

**Structure of the document:**

- **Core Concepts** — what scatter and gather mean, illustrated with ASCII flow diagrams showing how the DMA engine bridges fragmented memory to the I2C bus
- **Hardware Architecture** — the DMA ↔ I2C controller relationship, descriptor chain layout, and a table of common SoC implementations
- **Descriptor Table Design** — alignment, ownership bits, termination, and chain linking

**C/C++ examples (4 implementations):**
1. Generic bare-metal descriptor structs + SG list initialisation
2. Direct DMA controller register programming (fictional SoC similar to STM32)
3. Linux kernel driver using the `dmaengine` / `sg_table` APIs — the canonical production approach
4. FreeRTOS + STM32H7 MDMA linked-list mode with semaphore-based completion

**Rust examples (3 implementations):**
1. `no_std` safe abstraction with a `SgList<N>` generic over segment count — compile-time bounds checking
2. Embedded-hal + RP2040 chained DMA channels at the register level
3. Embassy async/await pattern — clean `.await` on DMA completion with `heapless::Vec`

**Advanced topics** cover ring-buffer wrap-around (the classic two-segment case) and zero-copy protocol framing (separate header/payload/footer with no intermediate copy).

The **summary** ties everything together with a concise table of key takeaways and common pitfalls (cache coherency, virtual vs. physical addresses, HW ownership bits, memory barriers).

# 87. Scatter-Gather I2C
## Non-Contiguous Memory Buffer Transfers Using Hardware Scatter-Gather Capabilities

---

## Table of Contents

1. [Introduction](#introduction)
2. [Core Concepts](#core-concepts)
3. [Hardware Architecture](#hardware-architecture)
4. [Descriptor Table Design](#descriptor-table-design)
5. [Programming Model](#programming-model)
6. [C/C++ Implementation](#cc-implementation)
   - [Basic Scatter-Gather Transfer](#basic-scatter-gather-transfer)
   - [DMA Descriptor Setup](#dma-descriptor-setup)
   - [Linux Kernel I2C Scatter-Gather](#linux-kernel-i2c-scatter-gather)
   - [RTOS-Based Implementation](#rtos-based-implementation)
7. [Rust Implementation](#rust-implementation)
   - [Safe Abstraction Layer](#safe-abstraction-layer)
   - [Embedded Rust with embedded-hal](#embedded-rust-with-embedded-hal)
   - [Async Scatter-Gather with Embassy](#async-scatter-gather-with-embassy)
8. [Advanced Topics](#advanced-topics)
9. [Error Handling](#error-handling)
10. [Performance Considerations](#performance-considerations)
11. [Summary](#summary)

---

## Introduction

**Scatter-Gather I2C** is a DMA (Direct Memory Access) technique that allows an I2C controller
to perform data transfers across multiple **non-contiguous memory regions** in a single
hardware-managed operation. Instead of the CPU stitching together multiple sequential
transfers from one flat buffer, a descriptor chain (the *scatter-gather list*) is built in
memory and handed off to the DMA engine. The hardware then autonomously walks the chain,
collecting ("gathering") data from — or distributing ("scattering") data to — disparate
memory locations with minimal CPU intervention.

### Why Scatter-Gather Matters for I2C

Standard I2C DMA transfers require data to reside in a single, physically contiguous buffer.
This constraint creates real-world problems:

| Problem | Root Cause | Scatter-Gather Solution |
|---|---|---|
| Memory fragmentation | Large contiguous allocations fail | Chain small fragments |
| Zero-copy networking | Protocol headers are separate from payload | Transfer header + payload in one pass |
| Struct padding overhead | Fields in a C struct may not be contiguous | Selectively DMA only meaningful fields |
| Multi-register writes | Registers split across different addresses | One atomic I2C transaction |
| Ring/circular buffers | Wrap-around creates two logical segments | Two descriptors, one DMA op |

---

## Core Concepts

### The Scatter-Gather List

A scatter-gather (SG) list is an array of **descriptors** — each containing:

```
┌──────────────────────────────────────────────┐
│  SG Descriptor                               │
│  ┌─────────────────┬─────────┬─────────────┐ │
│  │  Buffer Address │  Length │  Next Desc  │ │
│  │  (physical/bus) │  (bytes)│  (ptr/NULL) │ │
│  └─────────────────┴─────────┴─────────────┘ │
└──────────────────────────────────────────────┘
```

The DMA engine processes descriptors sequentially, generating I2C bus transactions that appear
as a single uninterrupted transfer from the perspective of the slave device.

### Gather (Write) Mode

Data is *gathered* from multiple memory fragments and written to the I2C slave:

```
Memory:                      I2C Bus:
┌──────────┐
│ Header   │──┐
└──────────┘  │              ┌──────────────────────────────────────────────────┐
              ├─► DMA ──────►│ START | ADDR | Header | Payload | Trailer | STOP │
┌──────────┐  │              └──────────────────────────────────────────────────┘
│ Payload  │──┤
└──────────┘  │
              │
┌──────────┐  │
│ Trailer  │──┘
└──────────┘
```

### Scatter (Read) Mode

Data read from the I2C slave is *scattered* into multiple memory fragments:

```
I2C Bus:                     Memory:
                              ┌──────────┐
                         ┌───►│ Buffer A │
┌──────────────┐         │    └──────────┘
│ I2C Slave    │──► DMA──┤
│ Data Stream  │         │    ┌──────────┐
└──────────────┘         └───►│ Buffer B │
                              └──────────┘
```

---

## Hardware Architecture

### DMA Controller Integration

Modern SoCs integrate a DMA engine directly with the I2C peripheral. The flow is:

```
CPU                DMA Controller           I2C Controller         I2C Bus
 │                      │                        │                    │
 │  Configure SG list   │                        │                    │
 │─────────────────────►│                        │                    │
 │                      │                        │                    │
 │  Start transfer      │                        │                    │
 │─────────────────────►│                        │                    │
 │                      │  Fetch descriptor[0]   │                    │
 │                      │───────────────────────►│                    │
 │                      │                        │  START + ADDR      │
 │                      │                        │───────────────────►│
 │                      │  Stream bytes          │                    │
 │                      │───────────────────────►│  Data bytes        │
 │                      │                        │───────────────────►│
 │                      │  Fetch descriptor[1]   │                    │
 │                      │───────────────────────►│                    │
 │                      │  Stream bytes          │                    │
 │                      │───────────────────────►│  More data bytes   │
 │                      │                        │───────────────────►│
 │                      │  (chain exhausted)     │                    │
 │                      │                        │  STOP              │
 │                      │                        │───────────────────►│
 │  DMA complete IRQ    │                        │                    │
 │◄─────────────────────│                        │                    │
```

### Common Hardware Implementations

| Platform | Controller | SG Support |
|---|---|---|
| Linux / ARM | DesignWare I2C + DW DMA | Full SG via `dmaengine` API |
| STM32H7 | I2C + MDMA | Linked-list mode descriptors |
| NXP i.MX | LPI2C + eDMA | Scatter-gather via TCD chaining |
| RP2040 | I2C + DMA | Chained channel transfers |
| Raspberry Pi | BCM I2C | Via Linux `i2c-bcm2835` + DMA |

---

## Descriptor Table Design

A well-designed descriptor table must account for:

- **Alignment**: Most DMA engines require 4 or 8-byte aligned descriptors
- **Physical addresses**: Virtual-to-physical translation needed (cache coherency!)
- **Termination**: A sentinel/NULL pointer or a termination flag in the last descriptor
- **Ownership bits**: Hardware sets/clears ownership to prevent race conditions

```
Descriptor Chain Layout:

desc[0]                  desc[1]                  desc[2]
┌─────────────────┐      ┌─────────────────┐      ┌─────────────────┐
│ addr: 0x20001000│      │ addr: 0x20004800│      │ addr: 0x20009000│
│ len:  64        │      │ len:  128       │      │ len:  32        │
│ flags: VALID    │      │ flags: VALID    │      │ flags: VALID    │
│ next: ──────────┼─────►│ next: ──────────┼─────►│ next: NULL      │
└─────────────────┘      └─────────────────┘      └─────────────────┘
```

---

## Programming Model

### General Steps

1. **Allocate and populate** scatter-gather descriptors
2. **Map buffers** for DMA (ensure cache coherency, get physical/bus addresses)
3. **Program the DMA engine** with the descriptor chain base address
4. **Start the I2C transfer** (or let the DMA engine trigger it)
5. **Wait** for completion interrupt or poll status
6. **Unmap and release** DMA mappings

---

## C/C++ Implementation

### Basic Scatter-Gather Transfer

#### Descriptor Structure (Generic Embedded)

```c
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* ─────────────────────────────────────────────
 * Generic DMA Scatter-Gather descriptor.
 * Must be aligned to DMA_DESC_ALIGN bytes.
 * ───────────────────────────────────────────── */
#define DMA_DESC_ALIGN  __attribute__((aligned(8)))

typedef struct sg_descriptor {
    uint32_t src_addr;          /* Source physical address        */
    uint32_t dst_addr;          /* Destination physical address   */
    uint32_t length;            /* Transfer length in bytes       */
    uint32_t control;           /* DMA control / flags word       */
    struct sg_descriptor *next; /* Next descriptor (NULL = end)   */
} DMA_DESC_ALIGN sg_descriptor_t;

/* Control word bit definitions */
#define SG_CTRL_VALID       (1u << 0)   /* Descriptor is valid          */
#define SG_CTRL_IRQ_ON_END  (1u << 1)   /* Generate IRQ when done       */
#define SG_CTRL_OWNED_BY_HW (1u << 31)  /* HW owns — CPU must not touch */

/* ─────────────────────────────────────────────
 * Scatter-Gather list handle
 * ───────────────────────────────────────────── */
#define SG_MAX_SEGMENTS     16

typedef struct {
    sg_descriptor_t  descs[SG_MAX_SEGMENTS];
    uint32_t         count;
    bool             is_write;     /* true = gather (TX), false = scatter (RX) */
} sg_list_t;

/* ─────────────────────────────────────────────
 * Initialise an SG list for a write (gather) operation.
 * buffers[]  – array of virtual pointers to source buffers
 * lengths[]  – corresponding lengths
 * n          – number of segments
 * i2c_fifo   – physical address of I2C TX-FIFO register
 * ───────────────────────────────────────────── */
int sg_list_init_write(sg_list_t       *sg,
                       const void      *buffers[],
                       const uint32_t   lengths[],
                       uint32_t         n,
                       uint32_t         i2c_fifo_phys)
{
    if (!sg || !buffers || !lengths || n == 0 || n > SG_MAX_SEGMENTS)
        return -1;

    sg->count    = n;
    sg->is_write = true;
    memset(sg->descs, 0, sizeof(sg->descs));

    for (uint32_t i = 0; i < n; i++) {
        sg->descs[i].src_addr = (uint32_t)(uintptr_t)buffers[i]; /* virt→phys mapping omitted */
        sg->descs[i].dst_addr = i2c_fifo_phys;
        sg->descs[i].length   = lengths[i];
        sg->descs[i].control  = SG_CTRL_VALID | SG_CTRL_OWNED_BY_HW;
        sg->descs[i].next     = (i + 1 < n) ? &sg->descs[i + 1] : NULL;
    }

    /* Request IRQ only on last descriptor */
    sg->descs[n - 1].control |= SG_CTRL_IRQ_ON_END;

    /* Memory barrier: ensure descriptor writes are visible before HW start */
    __asm__ volatile ("dsb sy" ::: "memory");

    return 0;
}
```

---

### DMA Descriptor Setup

```c
#include <stdint.h>

/* ─────────────────────────────────────────────
 * Fictional I2C + DMA controller register map.
 * Replace base addresses and field definitions
 * with your actual SoC reference manual values.
 * ───────────────────────────────────────────── */

#define I2C_BASE          0x40005400UL
#define DMA_BASE          0x40026000UL

/* I2C registers */
#define I2C_CR1           (*(volatile uint32_t *)(I2C_BASE + 0x00))
#define I2C_CR2           (*(volatile uint32_t *)(I2C_BASE + 0x04))
#define I2C_TXDR          (*(volatile uint32_t *)(I2C_BASE + 0x28))
#define I2C_ISR           (*(volatile uint32_t *)(I2C_BASE + 0x18))
#define I2C_CR1_TXDMAEN   (1u << 14)
#define I2C_CR2_START     (1u << 13)
#define I2C_CR2_STOP      (1u << 14)
#define I2C_ISR_STOPF     (1u << 5)

/* DMA channel registers (stream/channel 7 example) */
#define DMA_S7CR          (*(volatile uint32_t *)(DMA_BASE + 0xB8))
#define DMA_S7NDTR        (*(volatile uint32_t *)(DMA_BASE + 0xBC))
#define DMA_S7PAR         (*(volatile uint32_t *)(DMA_BASE + 0xC0))
#define DMA_S7M0AR        (*(volatile uint32_t *)(DMA_BASE + 0xC4))
#define DMA_S7FCR         (*(volatile uint32_t *)(DMA_BASE + 0xD0))
/* Scatter-gather linked-list address register (MDMA / BDMA variant) */
#define DMA_S7LAR         (*(volatile uint32_t *)(DMA_BASE + 0xD4))

#define DMA_CR_EN         (1u << 0)
#define DMA_CR_TCIE       (1u << 4)   /* Transfer complete interrupt enable */
#define DMA_CR_DIR_M2P    (1u << 6)   /* Memory-to-peripheral direction     */
#define DMA_CR_MINC       (1u << 10)  /* Memory address increment           */

/* ─────────────────────────────────────────────
 * Programme the DMA engine to walk an SG list
 * and feed data into the I2C TX FIFO.
 * ───────────────────────────────────────────── */
void dma_start_sg_i2c_write(const sg_list_t *sg)
{
    /* 1. Disable DMA channel while configuring */
    DMA_S7CR &= ~DMA_CR_EN;
    while (DMA_S7CR & DMA_CR_EN) {}   /* Wait for EN to clear */

    /* 2. Point the DMA to the first descriptor in the chain */
    DMA_S7LAR = (uint32_t)(uintptr_t)&sg->descs[0];

    /* 3. Set peripheral address = I2C TX data register */
    DMA_S7PAR = (uint32_t)(uintptr_t)&I2C_TXDR;

    /* 4. Configure: memory-to-peripheral, byte width, linked-list mode */
    DMA_S7CR = DMA_CR_DIR_M2P
             | DMA_CR_MINC
             | DMA_CR_TCIE
             | (0u << 11)  /* PINC = 0: peripheral address fixed   */
             | (0u << 13); /* MSIZE/PSIZE = 8-bit byte transfers   */

    /* 5. Enable DMA TXDMAEN in I2C controller */
    I2C_CR1 |= I2C_CR1_TXDMAEN;

    /* 6. Enable DMA channel — hardware walks descriptors automatically */
    DMA_S7CR |= DMA_CR_EN;

    /* 7. Issue I2C START condition with slave address and byte count */
    /* (Populate I2C_CR2 with SADD, NBYTES=total_bytes, AUTOEND etc.) */
    I2C_CR2 |= I2C_CR2_START;
}

/* ─────────────────────────────────────────────
 * Example: build and fire an SG write
 * ───────────────────────────────────────────── */
void example_scatter_gather_write(void)
{
    /* Three non-contiguous buffers */
    static const uint8_t header[4]  = { 0x01, 0x02, 0x03, 0x04 };
    static       uint8_t payload[8] = { 0xAA, 0xBB, 0xCC, 0xDD,
                                        0xEE, 0xFF, 0x11, 0x22 };
    static const uint8_t trailer[2] = { 0xFE, 0xED };

    const void    *bufs[3]  = { header, payload, trailer };
    const uint32_t lens[3]  = { sizeof(header),
                                sizeof(payload),
                                sizeof(trailer) };

    sg_list_t sg;
    if (sg_list_init_write(&sg, bufs, lens, 3, (uint32_t)(uintptr_t)&I2C_TXDR) != 0) {
        /* Handle error */
        return;
    }

    dma_start_sg_i2c_write(&sg);
    /* Continue: wait for DMA complete IRQ or poll I2C_ISR_STOPF */
}
```

---

### Linux Kernel I2C Scatter-Gather

Linux exposes scatter-gather I2C through the `dmaengine` subsystem and the standard
`struct i2c_msg` array, which the driver internally maps to an SG table.

```c
/* ─────────────────────────────────────────────
 * Linux kernel driver: scatter-gather I2C write
 * using dmaengine + sg_table API
 *
 * Kernel headers required:
 *   <linux/i2c.h>
 *   <linux/dmaengine.h>
 *   <linux/scatterlist.h>
 *   <linux/dma-mapping.h>
 * ───────────────────────────────────────────── */
#include <linux/i2c.h>
#include <linux/dmaengine.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/completion.h>
#include <linux/slab.h>

/* ── Driver private context ── */
struct my_i2c_priv {
    struct i2c_adapter   adapter;
    struct dma_chan      *dma_tx;
    struct dma_chan      *dma_rx;
    struct completion     dma_complete;
    struct device        *dev;
    void __iomem         *base;
};

/* ── DMA completion callback ── */
static void i2c_dma_tx_callback(void *arg)
{
    struct my_i2c_priv *priv = arg;
    complete(&priv->dma_complete);
}

/* ─────────────────────────────────────────────
 * Perform a scatter-gather I2C write.
 *
 * @priv    – driver private data
 * @addr    – 7-bit I2C slave address
 * @iov     – array of {base, len} pairs (non-contiguous)
 * @niov    – number of iov entries
 *
 * Returns 0 on success, negative errno on failure.
 * ───────────────────────────────────────────── */
int i2c_sg_write(struct my_i2c_priv       *priv,
                 u16                        addr,
                 const struct kvec         *iov,
                 unsigned int               niov)
{
    struct sg_table          sgt;
    struct scatterlist      *sg;
    struct dma_async_tx_descriptor *desc;
    dma_cookie_t             cookie;
    int                      ret, i;

    /* 1. Allocate an sg_table with one entry per iov segment */
    ret = sg_alloc_table(&sgt, niov, GFP_KERNEL);
    if (ret)
        return ret;

    /* 2. Populate each scatterlist entry from the kvec array */
    for_each_sg(sgt.sgl, sg, niov, i) {
        sg_set_buf(sg, iov[i].iov_base, iov[i].iov_len);
    }

    /* 3. Map sg_table for DMA — converts virtual → bus addresses,
     *    handles cache coherency (flush, invalidate) automatically   */
    ret = dma_map_sg(priv->dev, sgt.sgl, sgt.nents, DMA_TO_DEVICE);
    if (!ret) {
        ret = -ENOMEM;
        goto free_sgt;
    }

    /* 4. Prepare the DMA descriptor — the engine will walk all
     *    scatterlist entries as a single chained transfer           */
    desc = dmaengine_prep_slave_sg(priv->dma_tx,
                                   sgt.sgl,
                                   sgt.nents,
                                   DMA_MEM_TO_DEV,
                                   DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
    if (!desc) {
        ret = -ENOMEM;
        goto unmap_sg;
    }

    /* 5. Install completion callback */
    init_completion(&priv->dma_complete);
    desc->callback       = i2c_dma_tx_callback;
    desc->callback_param = priv;

    /* 6. Submit to DMA engine queue */
    cookie = dmaengine_submit(desc);
    if (dma_submit_error(cookie)) {
        ret = -EIO;
        goto unmap_sg;
    }

    /* 7. Start I2C transfer: configure slave address + total byte count */
    /* (Platform-specific: write to I2C_CR2 or equivalent register here) */
    /* my_i2c_set_transfer(priv, addr, total_len, I2C_WRITE); */

    /* 8. Fire the DMA engine */
    dma_async_issue_pending(priv->dma_tx);

    /* 9. Wait for completion (with timeout) */
    ret = wait_for_completion_timeout(&priv->dma_complete,
                                      msecs_to_jiffies(1000));
    if (!ret) {
        dmaengine_terminate_sync(priv->dma_tx);
        ret = -ETIMEDOUT;
        goto unmap_sg;
    }

    ret = 0; /* Success */

unmap_sg:
    dma_unmap_sg(priv->dev, sgt.sgl, sgt.nents, DMA_TO_DEVICE);
free_sgt:
    sg_free_table(&sgt);
    return ret;
}

/* ─────────────────────────────────────────────
 * Example: write a sensor command with two
 * separate buffers (register address + data).
 * ───────────────────────────────────────────── */
int write_sensor_scatter(struct my_i2c_priv *priv)
{
    static const u8 reg_addr[2]  = { 0x10, 0x00 }; /* Register 0x1000      */
    static       u8 sensor_cfg[6]= { 0x01, 0x80, 0xFF, 0x00, 0x42, 0x07 };

    struct kvec iov[2] = {
        { .iov_base = (void *)reg_addr,   .iov_len = sizeof(reg_addr)   },
        { .iov_base = (void *)sensor_cfg, .iov_len = sizeof(sensor_cfg) },
    };

    return i2c_sg_write(priv, 0x48, iov, 2);
}
```

---

### RTOS-Based Implementation

Using FreeRTOS with STM32 HAL as an example:

```c
/* ─────────────────────────────────────────────
 * FreeRTOS + STM32 HAL scatter-gather I2C
 * Using STM32H7 MDMA linked-list mode
 * ───────────────────────────────────────────── */
#include "stm32h7xx_hal.h"
#include "FreeRTOS.h"
#include "semphr.h"

extern I2C_HandleTypeDef  hi2c1;
extern DMA_HandleTypeDef  hdma_i2c1_tx;

static SemaphoreHandle_t  s_dma_done;

/* ── Linked-list node for STM32H7 MDMA ── */
typedef struct __attribute__((packed, aligned(32))) {
    uint32_t  CTBR;       /* Transfer bus register                   */
    uint32_t  CSAR;       /* Source address register                 */
    uint32_t  CDAR;       /* Destination address register            */
    uint32_t  CBRUR;      /* Block repeat update register            */
    uint32_t  CLAR;       /* Link address register (next node)       */
    uint32_t  CTCR;       /* Transfer configuration register         */
    uint32_t  CBNDTR;     /* Block number of data to transfer        */
    uint32_t  CMAR;       /* Mask address register                   */
    uint32_t  CMDR;       /* Mask data register                      */
} mdma_node_t;

#define MAX_SG_NODES   8

static mdma_node_t   sg_nodes[MAX_SG_NODES] __attribute__((aligned(32)));

/* ── Callback called from DMA complete ISR ── */
void HAL_I2C_MasterTxCpltCallback(I2C_HandleTypeDef *hi2c)
{
    BaseType_t higher_prio_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_dma_done, &higher_prio_woken);
    portYIELD_FROM_ISR(higher_prio_woken);
}

/* ─────────────────────────────────────────────
 * Build an MDMA linked-list node.
 *
 * @node     – node to fill
 * @src      – source buffer pointer (virtual, uncached assumed)
 * @len      – length in bytes
 * @i2c_txdr – physical address of I2C TXDR register
 * @next     – pointer to next node, or NULL for last
 * ───────────────────────────────────────────── */
static void mdma_node_init(mdma_node_t *node,
                           const void  *src,
                           uint32_t     len,
                           uint32_t     i2c_txdr,
                           mdma_node_t *next)
{
    memset(node, 0, sizeof(*node));
    node->CSAR   = (uint32_t)(uintptr_t)src;
    node->CDAR   = i2c_txdr;
    node->CBNDTR = len;
    /* Source increment, destination fixed (peripheral), byte width */
    node->CTCR   = 0x00000200u; /* SINC=1, DINC=0, TLEN=0, BWM=0 */
    node->CLAR   = next ? (uint32_t)(uintptr_t)next : 0u;
}

/* ─────────────────────────────────────────────
 * FreeRTOS task: scatter-gather I2C write.
 * Writes multiple non-contiguous buffers in one
 * I2C transaction using MDMA linked-list mode.
 * ───────────────────────────────────────────── */
void task_i2c_sg_write(void *pvParam)
{
    (void)pvParam;

    /* Create binary semaphore for DMA completion signalling */
    s_dma_done = xSemaphoreCreateBinary();
    configASSERT(s_dma_done);

    /* Three non-contiguous data sources */
    static uint8_t  cmd_byte[1]  = { 0xA5 };
    static uint8_t  data_blk[12] = { 0x01,0x02,0x03,0x04,
                                     0x05,0x06,0x07,0x08,
                                     0x09,0x0A,0x0B,0x0C };
    static uint8_t  checksum[1]  = { 0x55 };

    /* Physical address of I2C1 TXDR */
    const uint32_t TXDR_PHYS = 0x40005428UL;

    /* Build the linked-list: cmd → data → checksum */
    mdma_node_init(&sg_nodes[0], cmd_byte,  sizeof(cmd_byte),  TXDR_PHYS, &sg_nodes[1]);
    mdma_node_init(&sg_nodes[1], data_blk,  sizeof(data_blk),  TXDR_PHYS, &sg_nodes[2]);
    mdma_node_init(&sg_nodes[2], checksum,  sizeof(checksum),  TXDR_PHYS, NULL);

    /* Ensure descriptor writes are complete before starting DMA */
    __DSB();
    __ISB();

    /* Programme MDMA with the head of the linked list, then
     * kick off the I2C transaction via HAL.
     * In production code, extend HAL or use LL drivers to
     * point the MDMA channel LAR to &sg_nodes[0] before
     * calling HAL_I2C_Master_Transmit_DMA.              */
    HAL_StatusTypeDef status =
        HAL_I2C_Master_Transmit_DMA(&hi2c1,
                                    0x48 << 1,   /* 7-bit addr shifted */
                                    cmd_byte,    /* HAL uses first buf; MDMA takes over */
                                    sizeof(cmd_byte) + sizeof(data_blk) + sizeof(checksum));

    if (status != HAL_OK) {
        /* Handle error */
    }

    /* Block until DMA IRQ fires the semaphore (1 second timeout) */
    if (xSemaphoreTake(s_dma_done, pdMS_TO_TICKS(1000)) == pdFALSE) {
        HAL_I2C_Master_Abort_IT(&hi2c1, 0x48 << 1);
        /* Handle timeout */
    }

    /* Transfer complete — continue processing */
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

---

## Rust Implementation

### Safe Abstraction Layer

```rust
//! Scatter-Gather I2C abstraction in Rust
//! Provides a safe, zero-cost wrapper around raw DMA descriptors.

#![no_std]

use core::marker::PhantomData;
use core::sync::atomic::{compiler_fence, Ordering};

// ─────────────────────────────────────────────
// Descriptor type
// ─────────────────────────────────────────────

/// Alignment required by the DMA controller.
#[repr(C, align(8))]
pub struct SgDescriptor {
    pub src_addr: u32,
    pub dst_addr: u32,
    pub length:   u32,
    pub control:  u32,
    pub next:     u32,  // Physical address of next descriptor, or 0
}

bitflags::bitflags! {
    #[derive(Clone, Copy, Debug)]
    pub struct DescControl: u32 {
        const VALID         = 0b0000_0001;
        const IRQ_ON_END    = 0b0000_0010;
        const OWNED_BY_HW   = 1 << 31;
    }
}

// ─────────────────────────────────────────────
// SG List
// ─────────────────────────────────────────────

/// Maximum scatter-gather segments per transfer.
pub const MAX_SEGMENTS: usize = 16;

/// Direction of a scatter-gather transfer.
#[derive(Debug, Clone, Copy, PartialEq)]
pub enum SgDirection {
    /// Gather from memory, write to I2C slave (TX).
    Write,
    /// Read from I2C slave, scatter to memory (RX).
    Read,
}

/// A compile-time bounded scatter-gather list.
///
/// `N` is the maximum number of segments (≤ MAX_SEGMENTS).
pub struct SgList<const N: usize> {
    descs:     [SgDescriptor; N],
    count:     usize,
    direction: SgDirection,
    _not_send: PhantomData<*mut ()>,  // DMA addresses are address-space specific
}

impl<const N: usize> SgList<N> {
    const _CHECK: () = assert!(N <= MAX_SEGMENTS, "SgList: N exceeds MAX_SEGMENTS");

    /// Create a new, empty SG list.
    pub const fn new(direction: SgDirection) -> Self {
        // SAFETY: SgDescriptor is fully initialised below; zero is valid for all fields.
        Self {
            descs:  unsafe { core::mem::zeroed() },
            count:  0,
            direction,
            _not_send: PhantomData,
        }
    }

    /// Append a segment.
    ///
    /// `src`         – physical source address  
    /// `dst`         – physical destination address (e.g., I2C TXDR)  
    /// `len`         – length in bytes  
    ///
    /// Returns `Err(())` if the list is full.
    pub fn push(&mut self, src: u32, dst: u32, len: u32) -> Result<(), ()> {
        if self.count >= N {
            return Err(());
        }
        let i = self.count;
        self.descs[i] = SgDescriptor {
            src_addr: src,
            dst_addr: dst,
            length:   len,
            control:  (DescControl::VALID | DescControl::OWNED_BY_HW).bits(),
            next:     0, // filled in by finalise()
        };
        self.count += 1;
        Ok(())
    }

    /// Finalise the descriptor chain: link next-pointers and set the IRQ
    /// flag on the last descriptor. Must be called before starting DMA.
    ///
    /// # Safety
    /// The caller must ensure all `src_addr` / `dst_addr` values are valid
    /// physical addresses that remain valid for the duration of the DMA.
    pub unsafe fn finalise(&mut self) {
        let n = self.count;
        for i in 0..n {
            let this_ptr  = &self.descs[i]     as *const SgDescriptor as u32;
            let _ = this_ptr;
            if i + 1 < n {
                let next_phys = &self.descs[i + 1] as *const SgDescriptor as u32;
                self.descs[i].next = next_phys;
            } else {
                self.descs[i].next    = 0;
                self.descs[i].control |= DescControl::IRQ_ON_END.bits();
            }
        }
        // Compiler fence: prevent reordering of descriptor writes past this point.
        compiler_fence(Ordering::SeqCst);
    }

    /// Return a pointer to the head descriptor (to hand to the DMA engine).
    pub fn head_ptr(&self) -> *const SgDescriptor {
        if self.count == 0 { core::ptr::null() } else { &self.descs[0] }
    }

    /// Number of segments in the list.
    pub fn len(&self) -> usize { self.count }

    /// Returns true if the list is empty.
    pub fn is_empty(&self) -> bool { self.count == 0 }
}

// ─────────────────────────────────────────────
// Usage example (bare-metal, no_std)
// ─────────────────────────────────────────────

/// Fictional I2C TXDR physical address.
const I2C1_TXDR_PHYS: u32 = 0x4000_5428;

pub fn example_sg_write() {
    // Three non-contiguous data buffers (normally you'd get physical
    // addresses from a DMA allocator; shown as literals for clarity).
    let header:  [u8; 4]  = [0x01, 0x02, 0x03, 0x04];
    let payload: [u8; 8]  = [0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x11, 0x22];
    let trailer: [u8; 2]  = [0xFE, 0xED];

    let mut sg: SgList<4> = SgList::new(SgDirection::Write);

    // In production: use a DMA mapping API to get physical addresses.
    // Here we cast the stack pointers directly for illustration only.
    sg.push(header.as_ptr()  as u32, I2C1_TXDR_PHYS, header.len()  as u32).unwrap();
    sg.push(payload.as_ptr() as u32, I2C1_TXDR_PHYS, payload.len() as u32).unwrap();
    sg.push(trailer.as_ptr() as u32, I2C1_TXDR_PHYS, trailer.len() as u32).unwrap();

    // SAFETY: addresses valid for the duration of the (synchronous) DMA
    unsafe { sg.finalise(); }

    // Pass sg.head_ptr() to your DMA engine start function.
    let _head = sg.head_ptr();
    // dma_start_sg_i2c(&sg, slave_addr);
}
```

---

### Embedded Rust with embedded-hal

```rust
//! Scatter-Gather I2C via embedded-hal + direct register writes
//! Target: RP2040 (Raspberry Pi Pico) with chained DMA channels

#![no_std]
#![no_main]

use core::sync::atomic::{AtomicBool, Ordering};

use rp2040_hal as hal;
use hal::dma::{self, DMAExt, SingleChannel, ChannelIndex};
use hal::pac;
use hal::i2c::I2C;

// ─────────────────────────────────────────────
// RP2040 DMA control block for chaining
// ─────────────────────────────────────────────

/// RP2040 DMA control block (aligned to 16 bytes as per datasheet §2.5.7).
#[repr(C, align(16))]
struct DmaCtrlBlock {
    read_addr:   *const u8,
    write_addr:  *mut u8,
    trans_count: u32,
    ctrl:        u32,
}

// SAFETY: We manually ensure exclusive access to these blocks.
unsafe impl Send for DmaCtrlBlock {}

const DMA_CTRL_EN:       u32 = 1 << 0;
const DMA_CTRL_DATA_SIZE_BYTE: u32 = 0 << 2;
const DMA_CTRL_INCR_READ: u32 = 1 << 4;
const DMA_CTRL_CHAIN_TO_SHIFT: u32 = 11;
const DMA_CTRL_IRQ_QUIET: u32 = 1 << 21;
const DMA_CTRL_SNIFF_EN:  u32 = 0;

static TRANSFER_DONE: AtomicBool = AtomicBool::new(false);

/// Build a control word for a chained DMA transfer segment.
fn make_ctrl(chain_to_channel: u32, is_last: bool) -> u32 {
    let mut ctrl = DMA_CTRL_EN
                 | DMA_CTRL_DATA_SIZE_BYTE
                 | DMA_CTRL_INCR_READ
                 | (chain_to_channel << DMA_CTRL_CHAIN_TO_SHIFT)
                 | DMA_CTRL_SNIFF_EN;
    if !is_last {
        ctrl |= DMA_CTRL_IRQ_QUIET;  // Only fire IRQ at end of chain
    }
    ctrl
}

/// Perform a scatter-gather I2C write on RP2040 using two chained
/// DMA channels (ch0 → ch1 → ch0 ping-pong, or a simple chain here).
///
/// # Safety
/// `segments` must remain valid until the DMA completes.
pub unsafe fn rp2040_sg_i2c_write(
    i2c_txdata_reg: *mut u8,           // I2C TX FIFO register address
    segments: &[(*const u8, u32)],     // (src_ptr, length) pairs
) {
    // Use DMA channel 0 for data transfers.
    // A real implementation would reserve channels through the HAL.
    let dma_base = pac::DMA::ptr();

    for (idx, &(src, len)) in segments.iter().enumerate() {
        let is_last  = idx + 1 == segments.len();
        let chain_to = if is_last { 0 } else { 0 }; // chain back to ch0 for simplicity

        // Write DMA channel 0 registers for this segment.
        // In a true chained-DMA setup you would write to alternate
        // channel registers; shown simplified for clarity.
        (*dma_base).ch[0].ch_read_addr
            .write(|w| w.bits(src as u32));
        (*dma_base).ch[0].ch_write_addr
            .write(|w| w.bits(i2c_txdata_reg as u32));
        (*dma_base).ch[0].ch_trans_count
            .write(|w| w.bits(len));
        (*dma_base).ch[0].ch_ctrl_trig     // Writing CTRL_TRIG starts the channel
            .write(|w| w.bits(make_ctrl(chain_to, is_last)));

        // Wait for this channel to complete before programming the next.
        // In a true SG implementation, all descriptors are pre-loaded and
        // the HW chains without CPU intervention.
        while (*dma_base).ch[0].ch_ctrl_trig.read().busy().bit_is_set() {
            core::hint::spin_loop();
        }
    }

    TRANSFER_DONE.store(true, Ordering::Release);
}

// ─────────────────────────────────────────────
// Application entry point (simplified)
// ─────────────────────────────────────────────

#[cortex_m_rt::entry]
fn main() -> ! {
    let header:  [u8; 3] = [0x10, 0x00, 0x01];
    let payload: [u8; 5] = [0xDE, 0xAD, 0xBE, 0xEF, 0x00];

    // RP2040 I2C0 TX FIFO data register physical address
    let i2c0_txdata: *mut u8 = 0x4004_4010 as *mut u8;

    let segments = [
        (header.as_ptr(),  header.len()  as u32),
        (payload.as_ptr(), payload.len() as u32),
    ];

    unsafe {
        rp2040_sg_i2c_write(i2c0_txdata, &segments);
    }

    loop { cortex_m::asm::wfe(); }
}
```

---

### Async Scatter-Gather with Embassy

```rust
//! Async scatter-gather I2C using the Embassy async embedded framework.
//! Demonstrates the ergonomic async/await approach to DMA I2C transfers.

#![no_std]
#![no_main]
#![feature(type_alias_impl_trait)]

use embassy_executor::Spawner;
use embassy_stm32::i2c::{self, I2c};
use embassy_stm32::dma::NoDma;
use embassy_stm32::time::Hertz;
use embassy_stm32::{bind_interrupts, peripherals, Config};
use embassy_time::{Duration, Timer};
use heapless::Vec;

bind_interrupts!(struct Irqs {
    I2C1_EV => i2c::EventInterruptHandler<peripherals::I2C1>;
    I2C1_ER => i2c::ErrorInterruptHandler<peripherals::I2C1>;
});

// ─────────────────────────────────────────────
// Scatter buffer: owned, fixed-size segments
// ─────────────────────────────────────────────

/// A scatter-gather write request: up to 8 non-contiguous slices
/// assembled and dispatched in a single logical I2C transaction.
pub struct SgWriteRequest<'a> {
    slave_addr: u8,
    segments:   Vec<&'a [u8], 8>,
}

impl<'a> SgWriteRequest<'a> {
    pub fn new(slave_addr: u8) -> Self {
        Self { slave_addr, segments: Vec::new() }
    }

    /// Add a segment. Returns Err if the list is full (>8 segments).
    pub fn add_segment(&mut self, buf: &'a [u8]) -> Result<(), ()> {
        self.segments.push(buf).map_err(|_| ())
    }

    /// Total byte count across all segments.
    pub fn total_bytes(&self) -> usize {
        self.segments.iter().map(|s| s.len()).sum()
    }
}

// ─────────────────────────────────────────────
// Async scatter-gather send
// ─────────────────────────────────────────────

/// Send a scatter-gather write request over I2C.
///
/// Without hardware SG support, this assembles the segments into a
/// temporary stack buffer. With hardware SG DMA, replace the inner
/// loop with a DMA descriptor submit and `.await` on the DMA future.
async fn i2c_sg_send<'a>(
    i2c: &mut I2c<'static, peripherals::I2C1, NoDma, NoDma>,
    req: &SgWriteRequest<'a>,
) -> Result<(), i2c::Error>
{
    // Software fallback: gather into a fixed-size stack buffer.
    // For true hardware SG, submit a DMA descriptor chain here.
    let mut buf: Vec<u8, 256> = Vec::new();
    for seg in &req.segments {
        for &b in *seg {
            buf.push(b).map_err(|_| i2c::Error::Overrun)?;
        }
    }

    i2c.write(req.slave_addr, &buf).await
}

// ─────────────────────────────────────────────
// Embassy task
// ─────────────────────────────────────────────

#[embassy_executor::task]
async fn i2c_task(mut i2c: I2c<'static, peripherals::I2C1, NoDma, NoDma>)
{
    // Build a scatter-gather request from three separate data structures
    let device_addr:  u8     = 0x48;
    let register_id: [u8; 2] = [0x00, 0x10];   // Register address: 0x0010
    let config_word: [u8; 4] = [0xC0, 0xFF, 0xEE, 0x00];
    let checksum:    [u8; 1] = [0xDE];           // Tail byte

    let mut req = SgWriteRequest::new(device_addr);
    req.add_segment(&register_id).unwrap();
    req.add_segment(&config_word).unwrap();
    req.add_segment(&checksum).unwrap();

    match i2c_sg_send(&mut i2c, &req).await {
        Ok(())   => defmt::info!("SG write ok ({} bytes)", req.total_bytes()),
        Err(e)   => defmt::error!("SG write error: {:?}", e),
    }

    // Async read into scattered buffers using Embassy's read()
    let mut header_buf: [u8; 2] = [0u8; 2];
    let mut data_buf:   [u8; 8] = [0u8; 8];

    // Embassy's read fills a single contiguous buffer; post-process to scatter.
    let mut combined: [u8; 10] = [0u8; 10];
    i2c.read(device_addr, &mut combined).await.unwrap();
    header_buf.copy_from_slice(&combined[..2]);
    data_buf.copy_from_slice(&combined[2..]);

    defmt::info!("header: {:02x}, data: {:02x}", header_buf, data_buf);

    loop {
        Timer::after(Duration::from_millis(500)).await;
    }
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(Config::default());

    let i2c = I2c::new(
        p.I2C1, p.PB6, p.PB7, Irqs,
        NoDma, NoDma,
        Hertz(400_000),
        Default::default(),
    );

    spawner.spawn(i2c_task(i2c)).unwrap();
}
```

---

## Advanced Topics

### Ring Buffer Wrap-Around

A ring buffer whose tail wraps around its end is a perfect scatter-gather use case:

```c
/**
 * Build an SG list from a ring buffer segment that may wrap.
 * @rb_base   – physical base of the ring buffer
 * @rb_size   – total ring buffer size (power of two recommended)
 * @tail      – current read/write tail offset
 * @length    – number of bytes to transfer
 * @sg        – output SG list (must hold at least 2 entries)
 */
int sg_from_ring_buffer(uint32_t  rb_base,
                        uint32_t  rb_size,
                        uint32_t  tail,
                        uint32_t  length,
                        sg_list_t *sg)
{
    uint32_t space_to_end = rb_size - tail;

    if (length <= space_to_end) {
        /* No wrap-around — single contiguous segment */
        const void *bufs[1] = { (void *)(uintptr_t)(rb_base + tail) };
        uint32_t    lens[1] = { length };
        return sg_list_init_write(sg, bufs, lens, 1, I2C1_TXDR_PHYS);
    } else {
        /* Wrap-around — two segments: [tail..end] + [0..remainder] */
        const void *bufs[2] = {
            (void *)(uintptr_t)(rb_base + tail),    /* Tail to end of buffer   */
            (void *)(uintptr_t)(rb_base)            /* Beginning of buffer     */
        };
        uint32_t lens[2] = {
            space_to_end,                           /* First segment length    */
            length - space_to_end                   /* Remaining bytes (wrap)  */
        };
        return sg_list_init_write(sg, bufs, lens, 2, I2C1_TXDR_PHYS);
    }
}
```

### Zero-Copy Protocol Framing

Avoid buffer copies when adding protocol headers/footers to payloads:

```c
typedef struct {
    uint8_t  device_id;
    uint8_t  command;
    uint16_t payload_len;
} __attribute__((packed)) proto_header_t;

typedef struct {
    uint16_t crc;
} __attribute__((packed)) proto_footer_t;

/**
 * Send a framed message without copying payload into a new buffer.
 * The SG DMA engine handles header + payload + footer as one I2C burst.
 */
int send_framed_zero_copy(const uint8_t *payload, uint16_t payload_len)
{
    proto_header_t hdr = {
        .device_id   = 0x01,
        .command     = 0xA0,
        .payload_len = payload_len,
    };
    proto_footer_t ftr = {
        .crc = compute_crc16(payload, payload_len),
    };

    const void    *bufs[3] = { &hdr, payload, &ftr };
    const uint32_t lens[3] = { sizeof(hdr), payload_len, sizeof(ftr) };

    sg_list_t sg;
    if (sg_list_init_write(&sg, bufs, lens, 3, I2C1_TXDR_PHYS) != 0)
        return -1;

    dma_start_sg_i2c_write(&sg);
    return 0;
}
```

---

## Error Handling

Robust scatter-gather I2C code must handle:

```c
typedef enum {
    SG_ERR_NONE          = 0,
    SG_ERR_NACK          = -1,  /* Slave NACKed address or data   */
    SG_ERR_ARBITRATION   = -2,  /* Bus arbitration lost           */
    SG_ERR_TIMEOUT       = -3,  /* Transfer timed out             */
    SG_ERR_DMA_UNDERRUN  = -4,  /* DMA couldn't feed data in time */
    SG_ERR_OVERFLOW      = -5,  /* Too many segments              */
    SG_ERR_BUS_BUSY      = -6,  /* I2C bus busy (no STOP seen)    */
} sg_error_t;

/** Abort an in-flight SG transfer and reset the I2C + DMA state. */
void sg_abort_and_reset(sg_list_t *sg)
{
    /* 1. Disable DMA channel to stop feeding the I2C FIFO */
    DMA_S7CR &= ~DMA_CR_EN;

    /* 2. Issue STOP on the I2C bus */
    I2C_CR2 |= I2C_CR2_STOP;

    /* 3. Wait for STOP flag */
    uint32_t timeout = 10000;
    while (!(I2C_ISR & I2C_ISR_STOPF) && --timeout) {}

    /* 4. Clear STOP flag and reset I2C */
    /* I2C_ICR |= I2C_ICR_STOPCF; */

    /* 5. Reclaim DMA descriptor ownership (CPU takes back all descriptors) */
    for (uint32_t i = 0; i < sg->count; i++) {
        sg->descs[i].control &= ~SG_CTRL_OWNED_BY_HW;
    }
}
```

---

## Performance Considerations

| Factor | Recommendation |
|---|---|
| **Cache coherency** | Use uncached/write-through memory for descriptors and DMA buffers, or explicitly flush/invalidate cache lines before/after DMA |
| **Descriptor alignment** | Align to at least 8 bytes (some controllers require 32 or 64 bytes) |
| **IRQ coalescing** | Set `IRQ_ON_END` only on the final descriptor to avoid per-segment interrupts |
| **Segment count** | Keep chains short (< 16 segments); longer chains increase setup latency |
| **Physical contiguity** | Buffers within each segment must be physically contiguous; the SG list handles non-contiguity *between* segments, not within |
| **I2C clock vs DMA throughput** | At 400 kHz I2C (Fast Mode), each byte takes ~25 µs; DMA refill must be faster than this |
| **Bounce buffers** | If device memory is not DMA-accessible, the kernel may use bounce buffers — verify with `dma_map_sg()` return value |

---

## Summary

**Scatter-Gather I2C** bridges the gap between how data is naturally laid out in memory
(fragmented, structured, reused) and the I2C bus's requirement for a linear stream of bytes.
By building a chain of DMA descriptors that point to non-contiguous buffers, the hardware
DMA engine autonomously gathers or scatters data without CPU intervention or intermediate
copy buffers.

### Key Takeaways

**Conceptually**, scatter-gather treats a set of memory fragments as a single logical buffer.
Each descriptor in the chain holds a (address, length, next) tuple. The DMA engine walks the
chain, and the I2C controller sees an uninterrupted byte stream.

**In C/C++**, the programmer manually builds `sg_descriptor_t` arrays (or uses OS APIs like
Linux's `sg_table` / `dmaengine`), handles virtual-to-physical address translation, manages
cache coherency barriers (`dsb`, `__DSB`), and programs controller registers directly.

**In Rust**, the type system enforces ownership and lifetime rules on DMA buffers at compile
time. The `SgList<N>` generic over segment count turns out-of-bounds access into a compile
error. Embassy's async model further eliminates callback spaghetti, expressing DMA waits
as clean `.await` points.

**Common use cases** include zero-copy protocol framing (separate header/payload/footer
buffers), ring-buffer wrap-around transfers (two physical segments, one logical read),
and struct field extraction (DMA only the fields of interest, skipping padding).

**The critical pitfalls** to avoid are: forgetting cache flush/invalidate around DMA
mappings, using virtual instead of physical addresses in descriptors, allowing the CPU to
access descriptor memory while hardware owns it (`OWNED_BY_HW` bit), and failing to issue
a data synchronisation barrier before starting the DMA engine.

When hardware scatter-gather DMA is unavailable, a software fallback (gather into a
temporary contiguous buffer first) provides identical I2C bus behaviour at the cost of a
memory copy — the API surface can remain unchanged either way.

---

*Document: 87_Scatter_Gather_I2C.md — Embedded I2C Advanced Topics Series*