# 53. MISRA C Guidelines for CAN Code

> Applying MISRA-C coding standards to CAN driver and stack implementation for safety-critical systems.

---

## Table of Contents

1. [Introduction](#1-introduction)
2. [What is MISRA C?](#2-what-is-misra-c)
3. [Why MISRA C Matters for CAN Systems](#3-why-misra-c-matters-for-can-systems)
4. [Key MISRA C Rules Applied to CAN Code](#4-key-misra-c-rules-applied-to-can-code)
   - 4.1 [Type Safety and Integer Handling](#41-type-safety-and-integer-handling)
   - 4.2 [Pointer and Memory Safety](#42-pointer-and-memory-safety)
   - 4.3 [Control Flow and Complexity](#43-control-flow-and-complexity)
   - 4.4 [Error Handling and Return Values](#44-error-handling-and-return-values)
   - 4.5 [Bitfield and Bit Manipulation Rules](#45-bitfield-and-bit-manipulation-rules)
   - 4.6 [Volatile and Hardware Register Access](#46-volatile-and-hardware-register-access)
5. [MISRA-Compliant CAN Driver Implementation in C](#5-misra-compliant-can-driver-implementation-in-c)
   - 5.1 [Header and Type Definitions](#51-header-and-type-definitions)
   - 5.2 [CAN Frame Structure](#52-can-frame-structure)
   - 5.3 [Driver Initialization](#53-driver-initialization)
   - 5.4 [Message Transmission](#54-message-transmission)
   - 5.5 [Message Reception](#55-message-reception)
   - 5.6 [Error Handling Module](#56-error-handling-module)
6. [MISRA-Compliant CAN Stack Implementation in C++](#6-misra-compliant-can-stack-implementation-in-c)
7. [CAN Implementation in Rust (Safety by Design)](#7-can-implementation-in-rust-safety-by-design)
   - 7.1 [Type-Safe CAN Frame in Rust](#71-type-safe-can-frame-in-rust)
   - 7.2 [CAN Driver Abstraction in Rust](#72-can-driver-abstraction-in-rust)
   - 7.3 [Error Handling in Rust](#73-error-handling-in-rust)
8. [MISRA Rule Violation Examples and Fixes](#8-misra-rule-violation-examples-and-fixes)
9. [Static Analysis and Tool Integration](#9-static-analysis-and-tool-integration)
10. [MISRA C vs Rust: Safety Comparison for CAN](#10-misra-c-vs-rust-safety-comparison-for-can)
11. [Summary](#11-summary)

---

## 1. Introduction

The Controller Area Network (CAN) protocol is pervasive in safety-critical embedded systems — from automotive ECUs and industrial automation to medical devices and aviation avionics. Because CAN buses directly control physical actuators (brakes, throttles, valve controllers, robot joints), the software driving them must meet the highest standards of reliability and predictability.

MISRA C (Motor Industry Software Reliability Association C guidelines) provides a curated set of rules and directives that restrict or forbid language constructs in C and C++ that are undefined, implementation-defined, or otherwise prone to introducing subtle, hard-to-reproduce faults. Applying MISRA C to CAN driver and protocol stack code reduces:

- **Undefined behaviour** arising from integer overflow, bitwise operations on signed types, and invalid pointer arithmetic.
- **Implementation-defined portability gaps** between microcontroller toolchains (ARM, TriCore, RH850, PowerPC).
- **Undetected runtime errors** caused by unchecked return values, missing error branches, and uninitialized state.

This document covers the most relevant MISRA C:2012 rules (with notes on the older MISRA C:2004 numbering where applicable), shows how each rule applies concretely to CAN code, provides complete MISRA-compliant C and C++ implementations, and contrasts them with an idiomatic Rust implementation that achieves many of the same safety goals through language-level guarantees.

---

## 2. What is MISRA C?

MISRA C is a set of software development guidelines for the C programming language originally developed by the Motor Industry Software Reliability Association. Three major versions exist:

| Version | Year | Rules | Focus |
|---------|------|-------|-------|
| MISRA C:1998 | 1998 | 127 rules | C90, automotive |
| MISRA C:2004 | 2004 | 141 rules | C90/C99, broader |
| MISRA C:2012 | 2012 | 143 rules + 16 directives | C99/C11, mandatory/advisory |
| MISRA C:2012 Amendment 2 | 2020 | +14 rules | C11 thread safety |

MISRA C:2012 classifies each rule as:

- **Mandatory** — must never be violated, no deviation permitted.
- **Required** — must be complied with; deviations require documented justification and approval.
- **Advisory** — best practice; deviation is tolerated with awareness.

Rules are also categorized as **decidable** (can be checked automatically) or **undecidable** (require code review and human judgment).

---

## 3. Why MISRA C Matters for CAN Systems

CAN bus software operates in an environment with unique hazard characteristics:

**Hard Real-Time Constraints.** CAN frames must be transmitted and received within deterministic time windows. Unpredictable control flow (unbounded loops, recursion, dynamic allocation) can cause deadline misses with physical consequences.

**Multi-Node Shared Bus.** A single misbehaving node can corrupt the bus state for all connected ECUs. Incorrect bit manipulation in the frame assembler, wrong endianness handling, or corrupted DLC fields can trigger bus-off conditions network-wide.

**Concurrent Interrupt-Driven Access.** CAN hardware uses interrupts for RX/TX completion. Shared data structures touched by both ISR context and task context must be handled with strict discipline — a rule set enforces this.

**Long Service Life.** Automotive and industrial systems run for 10–20 years. Code correctness must not depend on a specific compiler version, optimization level, or host OS.

MISRA C directly addresses each of these concerns through rules governing types, control flow, concurrency primitives, and defensive coding.

---

## 4. Key MISRA C Rules Applied to CAN Code

### 4.1 Type Safety and Integer Handling

**Rule 10.1 (Required):** Operands must not be of an inappropriate essential type.
**Rule 10.3 (Required):** The value of an expression must not be assigned to an object of a narrower essential type or of a different essential type category.
**Rule 10.4 (Required):** Both operands of an operator must be of the same essential type.

CAN code is full of register-width integers, DLC fields (4 bits), CAN IDs (11 or 29 bits), and data byte arrays. Mixing signed and unsigned types, or assigning a wide type to a narrow one without an explicit cast, violates these rules and can silently truncate CAN IDs or corrupt DLC values.

**Rule 12.1 (Advisory):** The precedence of operators within expressions should be made explicit.

Bitmasking and bitshifting in CAN frame assembly must always use explicit parentheses.

**Rule 7.2 (Required):** A suffix 'u' or 'U' must be applied to all integer constants that are intended to be of unsigned type.

All CAN bitmasks must use the `U` suffix.

### 4.2 Pointer and Memory Safety

**Rule 11.3 (Required):** A cast must not be performed between a pointer to object type and a pointer to a different object type.

This prohibits the common (but dangerous) cast from `uint8_t *` to a hardware register structure pointer without an intervening `memcpy` or union.

**Rule 18.1 (Required):** A pointer resulting from arithmetic on a pointer operand must address an element of the same array as that pointer operand.

Array bounds must be checked before indexing CAN data bytes (DLC can be 0–8; any loop must not overrun).

**Rule 11.5 (Advisory):** A conversion must not be performed from a pointer to `void` to a pointer to an object.

Avoid generic `void *` callback registration in CAN drivers; prefer typed function pointers.

### 4.3 Control Flow and Complexity

**Rule 15.5 (Required):** A function should have a single point of exit at the end.

CAN functions with multiple `return` statements are advisory violations; multiple early exits on error paths complicate proof of correctness.

**Rule 14.1 (Required):** A loop counter variable must not have floating-point type.

All CAN buffer iteration loops must use integer indices.

**Rule 15.1 (Required):** The `goto` statement must not be used.

**Rule 17.2 (Required):** Functions must not call themselves, either directly or indirectly (no recursion).

CAN protocol state machines must be iterative, never recursive, to guarantee bounded stack depth.

### 4.4 Error Handling and Return Values

**Rule 17.7 (Required):** The value returned by a function having non-void return type must be used.

Every CAN transmit, receive, and configure call must check its return value. Silent discard of return values is a mandatory violation.

**Directive 4.7 (Required):** If a function returns error information, that error information must be tested.

### 4.5 Bitfield and Bit Manipulation Rules

**Rule 10.1 (Required):** Do not apply bitwise operators to signed integers.

CAN frame assembly heavily uses shifts and masks. All such operations must work on unsigned types only.

**Rule 12.2 (Required):** The value of an expression must be shifted only when the result is well-defined.

Shift amounts must not exceed the bit width of the type. Shifting a `uint8_t` by 8 is undefined.

### 4.6 Volatile and Hardware Register Access

**Rule 11.8 (Required):** A cast must not remove any `const` or `volatile` qualification from the type pointed to by a pointer.

Hardware CAN peripheral registers must be declared `volatile`. Casting away `volatile` may allow the compiler to eliminate safety-critical reads/writes.

**Directive 4.1 (Required):** Run-time failures must be minimized.

All register accesses must be bounded and bounded-checked.

---

## 5. MISRA-Compliant CAN Driver Implementation in C

### 5.1 Header and Type Definitions

```c
/**
 * @file can_driver.h
 * @brief MISRA C:2012 compliant CAN driver interface
 *
 * MISRA C:2012 Compliance Notes:
 *   - All integer types use stdint.h fixed-width types (Rule 6.1)
 *   - No implicit type conversions (Rule 10.3)
 *   - All constants carry 'U' suffix (Rule 7.2)
 */

#ifndef CAN_DRIVER_H
#define CAN_DRIVER_H

/* Rule 21.1: Do not define/undefine reserved identifiers */
#include <stdint.h>   /* uint8_t, uint16_t, uint32_t */
#include <stdbool.h>  /* bool, true, false            */
#include <stddef.h>   /* NULL, size_t                 */

/* ---------------------------------------------------------------
 * Symbolic constants — all unsigned (Rule 7.2)
 * --------------------------------------------------------------- */
#define CAN_MAX_DLC          (8U)
#define CAN_STD_ID_MASK      (0x7FFU)   /* 11-bit standard ID   */
#define CAN_EXT_ID_MASK      (0x1FFFFFFFU) /* 29-bit extended ID */
#define CAN_MAX_MAILBOXES    (32U)
#define CAN_TIMEOUT_TICKS    (1000U)

/* ---------------------------------------------------------------
 * Enumeration types — use named enums, not bare integers
 * (Rule 8.5, Advisory Rule 6.1)
 * --------------------------------------------------------------- */

/** CAN bus operating mode */
typedef enum
{
    CAN_MODE_NORMAL       = 0,
    CAN_MODE_LOOPBACK     = 1,
    CAN_MODE_SILENT       = 2,
    CAN_MODE_LISTEN_ONLY  = 3
} CAN_Mode_t;

/** CAN frame type */
typedef enum
{
    CAN_FRAME_DATA   = 0,
    CAN_FRAME_REMOTE = 1
} CAN_FrameType_t;

/** CAN ID type */
typedef enum
{
    CAN_ID_STANDARD  = 0,   /* 11-bit */
    CAN_ID_EXTENDED  = 1    /* 29-bit */
} CAN_IdType_t;

/** Driver error codes — all error paths must return a code (Dir 4.7) */
typedef enum
{
    CAN_OK              = 0,
    CAN_ERR_NULL_PTR    = 1,
    CAN_ERR_INVALID_ARG = 2,
    CAN_ERR_BUS_OFF     = 3,
    CAN_ERR_TX_FULL     = 4,
    CAN_ERR_RX_EMPTY    = 5,
    CAN_ERR_TIMEOUT     = 6,
    CAN_ERR_HW_FAULT    = 7
} CAN_Status_t;

/* ---------------------------------------------------------------
 * Structures
 * --------------------------------------------------------------- */

/**
 * @brief CAN frame descriptor
 *
 * Fixed-width members prevent implicit conversion surprises (Rule 10.3).
 * No bitfields — bitfield signedness is implementation-defined (Rule 6.1).
 */
typedef struct
{
    uint32_t        id;           /**< CAN identifier (11 or 29 bit)     */
    CAN_IdType_t    id_type;      /**< Standard or extended frame         */
    CAN_FrameType_t frame_type;   /**< Data or remote frame               */
    uint8_t         dlc;          /**< Data Length Code: 0..8             */
    uint8_t         data[CAN_MAX_DLC]; /**< Payload bytes                 */
} CAN_Frame_t;

/** CAN controller configuration */
typedef struct
{
    uint32_t   baud_rate_bps;    /**< e.g. 500000U for 500 kbps          */
    CAN_Mode_t mode;
    uint8_t    tx_mailbox_count; /**< Number of TX mailboxes to enable    */
    uint8_t    rx_mailbox_count; /**< Number of RX mailboxes to enable    */
} CAN_Config_t;

/** Opaque driver handle */
typedef struct CAN_Handle_s CAN_Handle_t;

/* ---------------------------------------------------------------
 * Function declarations — return type is always CAN_Status_t
 * so callers are forced to check it (Rule 17.7)
 * --------------------------------------------------------------- */
CAN_Status_t CAN_Init(CAN_Handle_t * const p_handle,
                      const CAN_Config_t * const p_config);

CAN_Status_t CAN_Transmit(CAN_Handle_t * const p_handle,
                           const CAN_Frame_t * const p_frame);

CAN_Status_t CAN_Receive(CAN_Handle_t * const p_handle,
                          CAN_Frame_t * const p_frame);

CAN_Status_t CAN_GetErrorState(const CAN_Handle_t * const p_handle,
                                uint8_t * const p_tx_err_count,
                                uint8_t * const p_rx_err_count);

CAN_Status_t CAN_DeInit(CAN_Handle_t * const p_handle);

#endif /* CAN_DRIVER_H */
```

### 5.2 CAN Frame Structure

```c
/**
 * @file can_frame.c
 * @brief CAN frame construction helpers — MISRA C:2012 compliant
 */

#include "can_driver.h"
#include <string.h>  /* memset, memcpy */

/**
 * @brief Validate and populate a CAN data frame.
 *
 * MISRA notes:
 *   - Rule 15.5: single exit point (all paths lead to the final return)
 *   - Rule 10.1: bitwise AND applied only to unsigned types
 *   - Rule 17.7: return value is CAN_Status_t — caller must check
 */
CAN_Status_t CAN_Frame_BuildData(CAN_Frame_t * const p_frame,
                                  uint32_t            id,
                                  CAN_IdType_t        id_type,
                                  const uint8_t * const p_data,
                                  uint8_t             dlc)
{
    CAN_Status_t status = CAN_OK;  /* Rule 15.5: single return variable */

    /* Rule 14.4: condition must be essentially boolean */
    if (p_frame == NULL)
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if (dlc > CAN_MAX_DLC)
    {
        status = CAN_ERR_INVALID_ARG;
    }
    else if ((dlc > 0U) && (p_data == NULL))
    {
        /* Cannot have data bytes with a NULL payload pointer */
        status = CAN_ERR_NULL_PTR;
    }
    else
    {
        /* Rule 10.1 + 10.3: mask with same-width unsigned constant */
        if (id_type == CAN_ID_STANDARD)
        {
            p_frame->id = id & CAN_STD_ID_MASK;       /* 11-bit mask */
        }
        else
        {
            p_frame->id = id & CAN_EXT_ID_MASK;       /* 29-bit mask */
        }

        p_frame->id_type    = id_type;
        p_frame->frame_type = CAN_FRAME_DATA;
        p_frame->dlc        = dlc;

        /* Zero payload first — prevents information leakage from stack  */
        (void)memset(p_frame->data, 0, CAN_MAX_DLC);

        /* Rule 18.1: copy only as many bytes as DLC specifies           */
        if (dlc > 0U)
        {
            /* Cast to (void*) required by memcpy signature; Rule 11.5
             * advisory — justified here as p_frame->data is uint8_t[].  */
            (void)memcpy(p_frame->data, p_data, (size_t)dlc);
        }

        status = CAN_OK;
    }

    return status;  /* Rule 15.5: single exit */
}

/**
 * @brief Extract a signal from a CAN frame using big-endian Motorola encoding.
 *
 * @param p_frame   Pointer to received CAN frame
 * @param start_bit Start bit position (0 = LSB of data[0])
 * @param bit_len   Signal length in bits (1..32)
 * @param p_value   Output: extracted signal value
 *
 * MISRA notes:
 *   - Rule 12.2: shift amount checked against type width before use
 *   - Rule 10.1: all shifts/masks on uint32_t only
 */
CAN_Status_t CAN_Frame_ExtractSignal(const CAN_Frame_t * const p_frame,
                                      uint8_t                   start_bit,
                                      uint8_t                   bit_len,
                                      uint32_t * const          p_value)
{
    CAN_Status_t status  = CAN_OK;
    uint32_t     raw     = 0U;
    uint8_t      byte_idx;
    uint8_t      i;

    if ((p_frame == NULL) || (p_value == NULL))
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if ((bit_len == 0U) || (bit_len > 32U))
    {
        status = CAN_ERR_INVALID_ARG;
    }
    else if (((uint16_t)start_bit + (uint16_t)bit_len) >
             ((uint16_t)CAN_MAX_DLC * 8U))
    {
        /* Signal would exceed frame payload — Rule 18.1 check */
        status = CAN_ERR_INVALID_ARG;
    }
    else
    {
        /* Assemble raw 32-bit value from big-endian bytes.
         * Rule 12.2: shift amounts are bounded by byte loop, max 24. */
        byte_idx = start_bit / 8U;

        for (i = 0U; i < 4U; i++)
        {
            if ((byte_idx + i) < CAN_MAX_DLC)
            {
                /* Rule 10.1: promote uint8_t to uint32_t before shift  */
                raw |= ((uint32_t)p_frame->data[byte_idx + i]) <<
                        (24U - (8U * (uint32_t)i));
            }
        }

        /* Align and mask to extract signal bits */
        raw = raw >> (32U - (uint32_t)(start_bit % 8U) - (uint32_t)bit_len);

        /* Build mask: Rule 12.2 — only shift within type width          */
        if (bit_len < 32U)
        {
            raw = raw & ((1UL << (uint32_t)bit_len) - 1UL);
        }
        /* else bit_len == 32: no masking needed, full uint32_t          */

        *p_value = raw;
        status   = CAN_OK;
    }

    return status;
}
```

### 5.3 Driver Initialization

```c
/**
 * @file can_driver.c
 * @brief CAN peripheral driver — MISRA C:2012 compliant implementation
 *
 * Hardware target: generic abstraction (replace register map with
 * target-specific header, e.g. stm32f4xx.h or S32K1xx.h).
 */

#include "can_driver.h"
#include <string.h>

/* ---------------------------------------------------------------
 * Hardware register map — volatile mandatory (Rule 11.8)
 * --------------------------------------------------------------- */
typedef struct
{
    volatile uint32_t MCR;    /**< Master Control Register    */
    volatile uint32_t MSR;    /**< Master Status Register     */
    volatile uint32_t BTR;    /**< Bit Timing Register        */
    volatile uint32_t ECR;    /**< Error Counter Register     */
    volatile uint32_t ESR;    /**< Error Status Register      */
    volatile uint32_t IER;    /**< Interrupt Enable Register  */
} CAN_Regs_t;

/* Register bit masks — unsigned constants (Rule 7.2) */
#define CAN_MCR_INRQ       (0x00000001U)  /* Initialization Request  */
#define CAN_MCR_SLEEP      (0x00000002U)  /* Sleep Mode Request      */
#define CAN_MCR_TXFP       (0x00000004U)  /* TX FIFO Priority        */
#define CAN_MCR_ABOM       (0x00000040U)  /* Auto Bus-Off Management */
#define CAN_MSR_INAK       (0x00000001U)  /* Init Acknowledge        */
#define CAN_MSR_SLAK       (0x00000002U)  /* Sleep Acknowledge       */
#define CAN_ESR_BOFF       (0x00000004U)  /* Bus-Off Flag            */

/* CAN handle — internal structure definition */
struct CAN_Handle_s
{
    CAN_Regs_t * p_regs;       /**< Pointer to peripheral registers    */
    CAN_Config_t config;       /**< Stored configuration               */
    bool         initialized;  /**< Invariant: false until CAN_Init OK */
};

/* Static storage for handles (no dynamic allocation — Rule 21.3) */
#define CAN_MAX_INSTANCES  (3U)
static CAN_Handle_t s_handles[CAN_MAX_INSTANCES];
static uint8_t      s_handle_count = 0U;

/**
 * @brief Allocate and initialize a CAN driver instance.
 *
 * MISRA notes:
 *   - Rule 17.2: no recursion — iterative loop only
 *   - Rule 21.3: no malloc/free
 *   - Rule 15.5: single return path
 *   - Rule 17.7: callers MUST use the return value
 */
CAN_Status_t CAN_Init(CAN_Handle_t * const p_handle,
                      const CAN_Config_t * const p_config)
{
    CAN_Status_t status    = CAN_OK;
    uint32_t     timeout   = CAN_TIMEOUT_TICKS;

    if ((p_handle == NULL) || (p_config == NULL))
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if (p_config->baud_rate_bps == 0U)
    {
        status = CAN_ERR_INVALID_ARG;
    }
    else if (s_handle_count >= CAN_MAX_INSTANCES)
    {
        status = CAN_ERR_HW_FAULT;
    }
    else
    {
        /* Copy configuration (no pointer aliasing — Rule 13.2) */
        p_handle->config      = *p_config;
        p_handle->initialized = false;
        p_handle->p_regs      = CAN_GetBaseAddr(/* platform call */);

        /* Enter initialization mode — request */
        p_handle->p_regs->MCR |= CAN_MCR_INRQ;

        /* Poll for acknowledge — bounded loop (Rule 14.2)          */
        while ((timeout > 0U) &&
               ((p_handle->p_regs->MSR & CAN_MSR_INAK) == 0U))
        {
            timeout--;
        }

        if (timeout == 0U)
        {
            status = CAN_ERR_TIMEOUT;
        }
        else
        {
            /* Configure bit timing for baud rate */
            status = CAN_ConfigureBitTiming(p_handle,
                                             p_config->baud_rate_bps);
        }

        if (status == CAN_OK)
        {
            /* Enable auto bus-off recovery */
            p_handle->p_regs->MCR |= CAN_MCR_ABOM;

            /* Leave initialization mode */
            p_handle->p_regs->MCR &= ~CAN_MCR_INRQ;
            p_handle->initialized  = true;

            s_handle_count++;
        }
    }

    return status;
}
```

### 5.4 Message Transmission

```c
/**
 * @brief Transmit a CAN frame.
 *
 * Selects the first available TX mailbox, loads the frame, and
 * requests transmission. The function is non-blocking; completion
 * is signaled by TX interrupt (ISR sets a flag or posts a semaphore).
 *
 * MISRA notes:
 *   - Rule 14.3: no unreachable code — all conditions are reachable
 *   - Rule 13.3: no side-effects in boolean conditions
 *   - Rule 17.7: return value must be checked by caller
 */
CAN_Status_t CAN_Transmit(CAN_Handle_t * const p_handle,
                           const CAN_Frame_t * const p_frame)
{
    CAN_Status_t status  = CAN_OK;
    uint8_t      mailbox = 0U;
    uint8_t      i;

    if ((p_handle == NULL) || (p_frame == NULL))
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if (!p_handle->initialized)
    {
        status = CAN_ERR_HW_FAULT;
    }
    else if (p_frame->dlc > CAN_MAX_DLC)
    {
        status = CAN_ERR_INVALID_ARG;
    }
    else if ((p_handle->p_regs->ESR & CAN_ESR_BOFF) != 0U)
    {
        /* Bus-off: do not attempt transmission */
        status = CAN_ERR_BUS_OFF;
    }
    else
    {
        /* Find free TX mailbox */
        status = CAN_FindFreeTxMailbox(p_handle, &mailbox);

        if (status == CAN_OK)
        {
            /* Load identifier into mailbox register
             * Rule 10.1: shift only on uint32_t          */
            if (p_frame->id_type == CAN_ID_EXTENDED)
            {
                /* EXID[28:0] in TIxR register, bit 2 = IDE flag (1U)  */
                p_handle->p_regs->TxMailbox[mailbox].TIR =
                    (p_frame->id << 3U) | 0x00000004U;
            }
            else
            {
                /* STID[10:0] in TIxR[31:21]                           */
                p_handle->p_regs->TxMailbox[mailbox].TIR =
                    (p_frame->id << 21U);
            }

            /* Set DLC */
            p_handle->p_regs->TxMailbox[mailbox].TDTR =
                (uint32_t)p_frame->dlc & 0x0000000FU;

            /* Load data bytes — low word (bytes 0-3) */
            p_handle->p_regs->TxMailbox[mailbox].TDLR = 0U;
            p_handle->p_regs->TxMailbox[mailbox].TDHR = 0U;

            for (i = 0U; (i < p_frame->dlc) && (i < 4U); i++)
            {
                /* Rule 12.2: shift (8U * i) bounded to max 24 bits   */
                p_handle->p_regs->TxMailbox[mailbox].TDLR |=
                    ((uint32_t)p_frame->data[i]) << (8U * (uint32_t)i);
            }

            for (; (i < p_frame->dlc) && (i < CAN_MAX_DLC); i++)
            {
                uint32_t shift = 8U * ((uint32_t)i - 4U); /* max 24   */
                p_handle->p_regs->TxMailbox[mailbox].TDHR |=
                    ((uint32_t)p_frame->data[i]) << shift;
            }

            /* Request transmission — set TXRQ bit */
            p_handle->p_regs->TxMailbox[mailbox].TIR |= 0x00000001U;
        }
    }

    return status;
}
```

### 5.5 Message Reception

```c
/**
 * @brief Receive a CAN frame from the RX FIFO.
 *
 * MISRA notes:
 *   - Rule 2.2: no dead code — all branches are exercised by test suite
 *   - Rule 18.1: array index bounded by DLC check before loop
 */
CAN_Status_t CAN_Receive(CAN_Handle_t * const p_handle,
                          CAN_Frame_t * const p_frame)
{
    CAN_Status_t status = CAN_OK;
    uint32_t     rir;      /* RX FIFO identifier register  */
    uint32_t     rdtr;     /* RX data length and timestamp */
    uint32_t     rdlr;     /* RX data low word             */
    uint32_t     rdhr;     /* RX data high word            */
    uint8_t      i;
    uint8_t      dlc;

    if ((p_handle == NULL) || (p_frame == NULL))
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if (!p_handle->initialized)
    {
        status = CAN_ERR_HW_FAULT;
    }
    else if (CAN_RxFifoEmpty(p_handle))
    {
        status = CAN_ERR_RX_EMPTY;
    }
    else
    {
        /* Read registers into local (non-volatile) copies first.
         * This prevents repeated volatile reads (Rule 13.3).           */
        rir  = p_handle->p_regs->RxFifo[0].RIR;
        rdtr = p_handle->p_regs->RxFifo[0].RDTR;
        rdlr = p_handle->p_regs->RxFifo[0].RDLR;
        rdhr = p_handle->p_regs->RxFifo[0].RDHR;

        /* Extract frame type — bit 1 of RIR is RTR flag               */
        p_frame->frame_type = ((rir & 0x00000002U) != 0U)
                                ? CAN_FRAME_REMOTE
                                : CAN_FRAME_DATA;

        /* Extract ID type — bit 2 of RIR is IDE flag                  */
        if ((rir & 0x00000004U) != 0U)
        {
            p_frame->id_type = CAN_ID_EXTENDED;
            p_frame->id      = (rir >> 3U) & CAN_EXT_ID_MASK;
        }
        else
        {
            p_frame->id_type = CAN_ID_STANDARD;
            p_frame->id      = (rir >> 21U) & CAN_STD_ID_MASK;
        }

        /* Extract DLC — bits [3:0] of RDTR */
        dlc = (uint8_t)(rdtr & 0x0000000FU);

        /* Clamp DLC to maximum — defensive (Rule 14.3)                */
        if (dlc > CAN_MAX_DLC)
        {
            dlc = CAN_MAX_DLC;
        }
        p_frame->dlc = dlc;

        /* Zero unused payload bytes */
        (void)memset(p_frame->data, 0, CAN_MAX_DLC);

        /* Unpack data bytes from 32-bit registers
         * Rule 18.1: i < dlc guarantees i < CAN_MAX_DLC after clamp   */
        for (i = 0U; (i < dlc) && (i < 4U); i++)
        {
            /* Rule 10.3: cast truncated value back to uint8_t          */
            p_frame->data[i] = (uint8_t)(rdlr >> (8U * (uint32_t)i));
        }
        for (; i < dlc; i++)
        {
            p_frame->data[i] = (uint8_t)(rdhr >> (8U * ((uint32_t)i - 4U)));
        }

        /* Release mailbox — signal hardware to free the FIFO entry     */
        p_handle->p_regs->RF0R |= 0x00000020U;  /* RFOM0 bit           */

        status = CAN_OK;
    }

    return status;
}
```

### 5.6 Error Handling Module

```c
/**
 * @file can_error.c
 * @brief CAN bus error state monitoring — MISRA C:2012 compliant
 *
 * The CAN protocol error state machine has three states:
 *   Error Active → Error Passive (TEC or REC > 127)
 *   Error Passive → Bus-Off      (TEC > 255)
 *   Bus-Off → Error Active       (after 128 × 11 recessive bits)
 */

#include "can_driver.h"

/* CAN ESR register masks */
#define CAN_ESR_EWGF   (0x00000001U)  /* Error Warning Flag  (TEC/REC > 96)  */
#define CAN_ESR_EPVF   (0x00000002U)  /* Error Passive Flag  (TEC/REC > 127) */
#define CAN_ESR_BOFF   (0x00000004U)  /* Bus-Off Flag        (TEC > 255)     */
#define CAN_ESR_TEC    (0x00FF0000U)  /* Transmit Error Counter [23:16]       */
#define CAN_ESR_REC    (0xFF000000U)  /* Receive Error Counter  [31:24]       */

typedef enum
{
    CAN_ERROR_STATE_ACTIVE  = 0,
    CAN_ERROR_STATE_PASSIVE = 1,
    CAN_ERROR_STATE_BUS_OFF = 2
} CAN_ErrorState_t;

/**
 * @brief Read hardware error counters.
 *
 * MISRA notes:
 *   - Rule 17.7: return value must be checked by all callers
 *   - Rule 10.3: shift extracts preserve uint8_t width
 */
CAN_Status_t CAN_GetErrorState(const CAN_Handle_t * const p_handle,
                                uint8_t * const            p_tx_err_count,
                                uint8_t * const            p_rx_err_count)
{
    CAN_Status_t status = CAN_OK;
    uint32_t     esr;

    if ((p_handle == NULL) ||
        (p_tx_err_count == NULL) ||
        (p_rx_err_count == NULL))
    {
        status = CAN_ERR_NULL_PTR;
    }
    else if (!p_handle->initialized)
    {
        status = CAN_ERR_HW_FAULT;
    }
    else
    {
        /* Single volatile read — store locally to avoid race (Rule 13.3) */
        esr = p_handle->p_regs->ESR;

        /* Rule 10.3: shift then mask then cast to uint8_t             */
        *p_tx_err_count = (uint8_t)((esr & CAN_ESR_TEC) >> 16U);
        *p_rx_err_count = (uint8_t)((esr & CAN_ESR_REC) >> 24U);

        if ((esr & CAN_ESR_BOFF) != 0U)
        {
            status = CAN_ERR_BUS_OFF;
        }
    }

    return status;
}

/**
 * @brief Periodic error monitoring task — call from OS task or timer ISR.
 *
 * Implements a simple error state machine. Logs transitions and can
 * trigger recovery actions (e.g. restart CAN controller after bus-off).
 *
 * MISRA notes:
 *   - Rule 17.7: result of CAN_GetErrorState is explicitly checked
 *   - Rule 15.5: single exit at bottom
 */
void CAN_ErrorMonitorTask(CAN_Handle_t * const p_handle)
{
    CAN_Status_t     status;
    uint8_t          tec        = 0U;
    uint8_t          rec        = 0U;
    CAN_ErrorState_t new_state  = CAN_ERROR_STATE_ACTIVE;
    static CAN_ErrorState_t s_prev_state = CAN_ERROR_STATE_ACTIVE;

    if (p_handle != NULL)
    {
        /* Rule 17.7: return value is explicitly used                  */
        status = CAN_GetErrorState(p_handle, &tec, &rec);

        if (status == CAN_ERR_BUS_OFF)
        {
            new_state = CAN_ERROR_STATE_BUS_OFF;
        }
        else if (status == CAN_OK)
        {
            if ((tec > 127U) || (rec > 127U))
            {
                new_state = CAN_ERROR_STATE_PASSIVE;
            }
            else
            {
                new_state = CAN_ERROR_STATE_ACTIVE;
            }
        }
        else
        {
            /* Hardware fault — treat as bus-off for safety            */
            new_state = CAN_ERROR_STATE_BUS_OFF;
        }

        /* Log state transition only when it changes                   */
        if (new_state != s_prev_state)
        {
            CAN_LogErrorTransition(s_prev_state, new_state, tec, rec);
            s_prev_state = new_state;

            /* Automatic recovery from bus-off (if ABOM not in HW)    */
            if (new_state == CAN_ERROR_STATE_BUS_OFF)
            {
                (void)CAN_TriggerBusOffRecovery(p_handle);
            }
        }
    }
    /* Rule 15.5: function exits here only */
}
```

---

## 6. MISRA-Compliant CAN Stack Implementation in C++

MISRA C++:2008 extends the C guidelines to C++, restricting additional features (exceptions, RTTI, dynamic_cast, etc.). The example below uses a strict MISRA C++:2008 / Autosar C++14 subset.

```cpp
/**
 * @file CanStack.hpp
 * @brief MISRA C++:2008 / Autosar C++14 compliant CAN protocol stack
 *
 * Restrictions applied:
 *   - No exceptions (A15-0-1): use error codes throughout
 *   - No RTTI / dynamic_cast (A27-0-1)
 *   - No heap allocation (A18-5-1): fixed-size arrays only
 *   - No virtual functions in safety path (M10-3-3 advisory)
 *   - Strong types via wrapper classes (replaces bare integers)
 */

#ifndef CAN_STACK_HPP
#define CAN_STACK_HPP

#include <cstdint>
#include <cstring>   /* std::memset, std::memcpy */

namespace Can
{

/* -------------------------------------------------------------------
 * Strong typedef wrappers — prevent accidental ID/DLC mix-ups
 * (MISRA C++:2008 Rule 5-2-9: use explicit casts; strong types avoid
 * the need for casts between logically different values)
 * ------------------------------------------------------------------- */

class CanId final
{
public:
    /* Rule A7-1-1: explicit constructors for single-argument ctors   */
    explicit CanId(uint32_t raw_id, bool extended = false) noexcept
        : m_id(extended ? (raw_id & 0x1FFFFFFFU) : (raw_id & 0x7FFU)),
          m_extended(extended)
    {}

    uint32_t value()    const noexcept { return m_id; }
    bool     isExtended() const noexcept { return m_extended; }

private:
    uint32_t m_id;
    bool     m_extended;
};

class Dlc final
{
public:
    static constexpr uint8_t MAX_VALUE = 8U;

    explicit Dlc(uint8_t raw) noexcept
        : m_dlc((raw <= MAX_VALUE) ? raw : MAX_VALUE)
    {}

    uint8_t value() const noexcept { return m_dlc; }

private:
    uint8_t m_dlc;
};

/* -------------------------------------------------------------------
 * Error type — MISRA C++:2008 A15-0-1: no exceptions
 * ------------------------------------------------------------------- */
enum class Status : uint8_t
{
    Ok             = 0U,
    NullPtr        = 1U,
    InvalidArg     = 2U,
    BusOff         = 3U,
    TxFull         = 4U,
    RxEmpty        = 5U,
    Timeout        = 6U,
    HwFault        = 7U
};

/* -------------------------------------------------------------------
 * CAN Frame — value type (copyable, no heap)
 * ------------------------------------------------------------------- */
class Frame final
{
public:
    static constexpr uint8_t MAX_DATA = 8U;

    Frame() noexcept
    {
        /* Rule A8-5-2: explicitly initialize all members               */
        std::memset(m_data, 0, sizeof(m_data));
    }

    /** Build a data frame. Returns Status::Ok or error. */
    Status setData(CanId id, const uint8_t * const data, Dlc dlc) noexcept
    {
        Status s = Status::Ok;

        if ((data == nullptr) && (dlc.value() > 0U))
        {
            s = Status::NullPtr;
        }
        else
        {
            m_id  = id;
            m_dlc = dlc;
            m_is_remote = false;

            std::memset(m_data, 0, sizeof(m_data));
            if (dlc.value() > 0U)
            {
                /* Rule A27-0-4: no unsafe string functions
                 * memcpy with explicit size is safe here.               */
                std::memcpy(m_data, data,
                            static_cast<std::size_t>(dlc.value()));
            }
        }
        return s;
    }

    CanId   id()       const noexcept { return m_id; }
    Dlc     dlc()      const noexcept { return m_dlc; }
    bool    isRemote() const noexcept { return m_is_remote; }

    /* Rule 9-3-1: return const reference to prevent external mutation  */
    const uint8_t * data() const noexcept { return m_data; }

private:
    CanId   m_id       { CanId(0U) };
    Dlc     m_dlc      { Dlc(0U)  };
    bool    m_is_remote{ false    };
    uint8_t m_data[MAX_DATA];
};

/* -------------------------------------------------------------------
 * CAN Filter — acceptance filter configuration
 * ------------------------------------------------------------------- */
struct Filter final
{
    uint32_t id_mask;    /**< Mask applied to incoming IDs              */
    uint32_t id_value;   /**< IDs matching (incoming & mask) == value  */
    bool     extended;   /**< Apply to extended frames                   */
};

/* -------------------------------------------------------------------
 * Driver interface — abstract base for HAL injection / unit testing
 * (MISRA C++:2008 M10-3-3 advisory: virtual in non-safety-critical path)
 * ------------------------------------------------------------------- */
class ICanDriver
{
public:
    virtual ~ICanDriver()                                         = default;
    virtual Status transmit(const Frame & frame)                 = 0;
    virtual Status receive(Frame & frame)                        = 0;
    virtual Status getErrorCounters(uint8_t & tx_ec,
                                    uint8_t & rx_ec)             = 0;
    virtual Status applyFilter(const Filter & filter,
                                uint8_t filter_bank)             = 0;
};

/* -------------------------------------------------------------------
 * CAN Stack — combines driver with receive dispatch
 * Fixed-size receive ring buffer (no dynamic allocation)
 * ------------------------------------------------------------------- */
template <uint8_t RX_DEPTH>
class Stack final
{
    static_assert(RX_DEPTH > 0U, "RX_DEPTH must be > 0");
    static_assert(RX_DEPTH <= 64U, "RX_DEPTH must be <= 64 to bound memory");

public:
    explicit Stack(ICanDriver & driver) noexcept
        : m_driver(driver),
          m_rx_head(0U),
          m_rx_tail(0U),
          m_rx_count(0U)
    {}

    /* Rule M0-1-3: all parameters used; no unused variables           */
    Status send(const Frame & frame) noexcept
    {
        return m_driver.transmit(frame);
    }

    /**
     * @brief Called from RX ISR or polling context.
     *
     * Reads one frame from the driver and enqueues it.
     * Rule A11-0-2: only non-virtual methods called in ISR context.
     */
    Status poll() noexcept
    {
        Status status = Status::Ok;
        Frame  frame;

        status = m_driver.receive(frame);

        if (status == Status::Ok)
        {
            status = enqueue(frame);
        }
        else if (status == Status::RxEmpty)
        {
            /* Not an error — bus is quiet                             */
            status = Status::Ok;
        }
        else
        {
            /* Propagate hardware errors                               */
        }

        return status;
    }

    /** Dequeue one received frame for application consumption. */
    Status dequeue(Frame & out_frame) noexcept
    {
        Status status = Status::Ok;

        if (m_rx_count == 0U)
        {
            status = Status::RxEmpty;
        }
        else
        {
            out_frame = m_rx_buffer[m_rx_tail];
            m_rx_tail = static_cast<uint8_t>((m_rx_tail + 1U) % RX_DEPTH);
            m_rx_count--;
        }

        return status;
    }

    uint8_t rxPending() const noexcept { return m_rx_count; }

private:
    Status enqueue(const Frame & frame) noexcept
    {
        Status status = Status::Ok;

        if (m_rx_count >= RX_DEPTH)
        {
            /* Overrun — oldest frame is discarded (logged in production) */
            status = Status::TxFull; /* re-using code; add RxOverrun if needed */
        }
        else
        {
            m_rx_buffer[m_rx_head] = frame;
            m_rx_head  = static_cast<uint8_t>((m_rx_head + 1U) % RX_DEPTH);
            m_rx_count++;
        }

        return status;
    }

    ICanDriver & m_driver;
    Frame        m_rx_buffer[RX_DEPTH];
    uint8_t      m_rx_head;
    uint8_t      m_rx_tail;
    uint8_t      m_rx_count;
};

} /* namespace Can */

#endif /* CAN_STACK_HPP */
```

---

## 7. CAN Implementation in Rust (Safety by Design)

Rust enforces at compile time many of the properties MISRA C enforces through rules and static analysis tools. Integer overflow is checked in debug mode, there is no implicit type coercion, uninitialized memory is forbidden by the type system, and ownership prevents data races. Where MISRA C says "you must not do X," Rust says "you cannot do X."

### 7.1 Type-Safe CAN Frame in Rust

```rust
//! can_frame.rs
//! Type-safe CAN frame implementation.
//!
//! Rust safety properties (vs. MISRA C equivalent):
//!   - No implicit integer conversions        (MISRA Rule 10.1, 10.3)
//!   - Bounds-checked array access            (MISRA Rule 18.1)
//!   - No uninitialised memory               (MISRA Dir 4.1)
//!   - Exhaustive match enforces all paths   (MISRA Rule 14.3)
//!   - Results must be checked               (MISRA Rule 17.7)

#![deny(
    clippy::arithmetic_side_effects,   // Equivalent to MISRA overflow checks
    clippy::cast_possible_truncation,  // Equivalent to MISRA Rule 10.3
    clippy::integer_arithmetic,
    unsafe_code,                       // No unsafe blocks allowed
)]

/// Maximum CAN data payload bytes.
pub const CAN_MAX_DLC: usize = 8;

/// 11-bit standard identifier mask.
pub const CAN_STD_ID_MASK: u32 = 0x7FF;

/// 29-bit extended identifier mask.
pub const CAN_EXT_ID_MASK: u32 = 0x1FFF_FFFF;

/// CAN identifier — validated on construction.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum CanId {
    Standard(u16),  // 0..=0x7FF
    Extended(u32),  // 0..=0x1FFFFFFF
}

impl CanId {
    /// Create a standard 11-bit CAN ID.
    /// Returns `None` if the value exceeds 0x7FF.
    pub fn standard(id: u16) -> Option<Self> {
        if id <= (CAN_STD_ID_MASK as u16) {
            Some(Self::Standard(id))
        } else {
            None
        }
    }

    /// Create an extended 29-bit CAN ID.
    /// Returns `None` if the value exceeds 0x1FFFFFFF.
    pub fn extended(id: u32) -> Option<Self> {
        if id <= CAN_EXT_ID_MASK {
            Some(Self::Extended(id))
        } else {
            None
        }
    }

    /// Raw numeric value of the identifier.
    pub fn raw(self) -> u32 {
        match self {
            Self::Standard(id) => u32::from(id),
            Self::Extended(id) => id,
        }
    }
}

/// Data Length Code — validated 0..=8.
#[derive(Clone, Copy, Debug, PartialEq, Eq, PartialOrd, Ord)]
pub struct Dlc(u8);

impl Dlc {
    /// Create a DLC value. Returns `None` if value > 8.
    pub fn new(value: u8) -> Option<Self> {
        if value <= CAN_MAX_DLC as u8 {
            Some(Self(value))
        } else {
            None
        }
    }

    /// Inner value.
    pub fn value(self) -> u8 { self.0 }
}

/// A validated CAN data frame.
///
/// Invariants (held by construction):
///   - `id` is within the range for its type
///   - `data` contains only `dlc` meaningful bytes; remainder is zeroed
#[derive(Clone, Debug)]
pub struct DataFrame {
    pub id:   CanId,
    pub dlc:  Dlc,
    data:     [u8; CAN_MAX_DLC],
}

impl DataFrame {
    /// Construct a data frame. Returns an error string on invalid input.
    pub fn new(id: CanId, payload: &[u8]) -> Result<Self, &'static str> {
        if payload.len() > CAN_MAX_DLC {
            return Err("Payload exceeds CAN_MAX_DLC");
        }

        let dlc = Dlc::new(payload.len() as u8)
            .ok_or("DLC out of range")?;

        let mut data = [0u8; CAN_MAX_DLC];

        // Bounds are already verified above; Rust also checks at runtime
        data[..payload.len()].copy_from_slice(payload);

        Ok(Self { id, dlc, data })
    }

    /// Read payload bytes (only the DLC-valid slice is returned).
    pub fn payload(&self) -> &[u8] {
        &self.data[..self.dlc.value() as usize]
    }
}

/// A CAN remote frame (no data payload).
#[derive(Clone, Debug)]
pub struct RemoteFrame {
    pub id:  CanId,
    pub dlc: Dlc,  // Requested DLC from sender
}
```

### 7.2 CAN Driver Abstraction in Rust

```rust
//! can_driver.rs
//! Hardware-abstracted CAN driver with error propagation.

use crate::can_frame::{CanId, DataFrame, Dlc, RemoteFrame, CAN_MAX_DLC};
use core::sync::atomic::{AtomicBool, Ordering};

// ---------------------------------------------------------------------------
// Error type — exhaustive, Rust forces handling (MISRA Rule 17.7 equivalent)
// ---------------------------------------------------------------------------

#[derive(Debug, PartialEq, Eq, Clone, Copy)]
pub enum CanError {
    NullPtr,
    InvalidArg,
    BusOff,
    TxFull,
    RxEmpty,
    Timeout,
    HwFault,
    Overrun,
}

impl core::fmt::Display for CanError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        match self {
            Self::NullPtr    => write!(f, "Null pointer"),
            Self::InvalidArg => write!(f, "Invalid argument"),
            Self::BusOff     => write!(f, "Bus-Off state"),
            Self::TxFull     => write!(f, "TX buffer full"),
            Self::RxEmpty    => write!(f, "RX buffer empty"),
            Self::Timeout    => write!(f, "Operation timed out"),
            Self::HwFault    => write!(f, "Hardware fault"),
            Self::Overrun    => write!(f, "RX overrun"),
        }
    }
}

pub type CanResult<T> = Result<T, CanError>;

// ---------------------------------------------------------------------------
// Driver trait — platform-independent interface (no dynamic dispatch needed
// in production; use generics for zero-cost abstraction)
// ---------------------------------------------------------------------------

pub trait CanDriver {
    fn transmit_data(&mut self, frame: &DataFrame)   -> CanResult<()>;
    fn transmit_remote(&mut self, frame: &RemoteFrame) -> CanResult<()>;
    fn receive(&mut self)                            -> CanResult<DataFrame>;
    fn error_counters(&self)                         -> CanResult<(u8, u8)>;
    fn is_bus_off(&self)                             -> bool;
}

// ---------------------------------------------------------------------------
// Acceptance filter
// ---------------------------------------------------------------------------

#[derive(Clone, Copy, Debug)]
pub struct AcceptanceFilter {
    pub id:       CanId,
    pub id_mask:  u32,   // Bits set to 1 must match id
}

impl AcceptanceFilter {
    pub fn matches(&self, incoming_id: CanId) -> bool {
        (incoming_id.raw() & self.id_mask) == (self.id.raw() & self.id_mask)
    }
}

// ---------------------------------------------------------------------------
// RX Ring Buffer — heapless, fixed-size (no alloc, MISRA Rule 21.3 equivalent)
// ---------------------------------------------------------------------------

pub struct RxRingBuffer<const N: usize> {
    buffer: [Option<DataFrame>; N],
    head:   usize,
    tail:   usize,
    count:  usize,
}

impl<const N: usize> RxRingBuffer<N> {
    const _ASSERT_NON_ZERO: () = assert!(N > 0, "Buffer size must be > 0");

    pub const fn new() -> Self {
        Self {
            // Option::None is valid for this const context
            buffer: [const { None }; N],
            head:   0,
            tail:   0,
            count:  0,
        }
    }

    pub fn push(&mut self, frame: DataFrame) -> CanResult<()> {
        if self.count >= N {
            Err(CanError::Overrun)
        } else {
            self.buffer[self.head] = Some(frame);
            self.head = (self.head + 1) % N;   // Wraps within [0, N)
            self.count += 1;
            Ok(())
        }
    }

    pub fn pop(&mut self) -> CanResult<DataFrame> {
        if self.count == 0 {
            Err(CanError::RxEmpty)
        } else {
            // SAFETY (logical): count > 0 guarantees tail slot is Some(_)
            let frame = self.buffer[self.tail]
                .take()
                .ok_or(CanError::HwFault)?;
            self.tail = (self.tail + 1) % N;
            self.count -= 1;
            Ok(frame)
        }
    }

    pub fn len(&self)     -> usize { self.count }
    pub fn is_empty(&self) -> bool { self.count == 0 }
}

// ---------------------------------------------------------------------------
// CAN Stack — wraps driver with filtering and buffering
// ---------------------------------------------------------------------------

pub struct CanStack<D: CanDriver, const RX_DEPTH: usize> {
    driver:  D,
    filters: [Option<AcceptanceFilter>; 8],  // Up to 8 software filters
    rx_buf:  RxRingBuffer<RX_DEPTH>,
    initialized: bool,
}

impl<D: CanDriver, const RX_DEPTH: usize> CanStack<D, RX_DEPTH> {
    pub fn new(driver: D) -> Self {
        Self {
            driver,
            filters: [None; 8],
            rx_buf:  RxRingBuffer::new(),
            initialized: true,
        }
    }

    /// Send a data frame. Caller must check the Result.
    pub fn send(&mut self, frame: &DataFrame) -> CanResult<()> {
        if self.driver.is_bus_off() {
            Err(CanError::BusOff)
        } else {
            self.driver.transmit_data(frame)
        }
    }

    /// Add a software acceptance filter.
    pub fn add_filter(&mut self, filter: AcceptanceFilter) -> CanResult<()> {
        // Find a free filter slot
        let slot = self.filters.iter_mut().find(|s| s.is_none());
        match slot {
            Some(s) => { *s = Some(filter); Ok(()) },
            None    => Err(CanError::InvalidArg),
        }
    }

    /// Poll driver for new frames and buffer them (call from OS task or ISR).
    pub fn poll(&mut self) -> CanResult<()> {
        loop {
            match self.driver.receive() {
                Ok(frame) => {
                    if self.passes_filters(&frame) {
                        // Ignore overrun in poll — caller uses dequeue
                        let _ = self.rx_buf.push(frame);
                    }
                },
                Err(CanError::RxEmpty) => break,
                Err(e)                 => return Err(e),
            }
        }
        Ok(())
    }

    /// Get next received frame for application use.
    pub fn dequeue(&mut self) -> CanResult<DataFrame> {
        self.rx_buf.pop()
    }

    fn passes_filters(&self, frame: &DataFrame) -> bool {
        // No filters configured → accept all
        let any_filter = self.filters.iter().any(|f| f.is_some());
        if !any_filter {
            return true;
        }
        self.filters.iter().flatten().any(|f| f.matches(frame.id))
    }
}
```

### 7.3 Error Handling in Rust

```rust
//! can_error_monitor.rs
//! Error state machine — Rust equivalent of the C error monitor.

use crate::can_driver::{CanDriver, CanError, CanResult};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorState {
    Active,   // TEC and REC <= 127
    Passive,  // TEC or REC > 127
    BusOff,   // TEC > 255
}

pub struct ErrorMonitor {
    prev_state: ErrorState,
    transitions: u32,
}

impl ErrorMonitor {
    pub const fn new() -> Self {
        Self {
            prev_state:  ErrorState::Active,
            transitions: 0,
        }
    }

    /// Update error state. Returns new state.
    ///
    /// The Result return type forces every caller to handle errors —
    /// equivalent to MISRA Rule 17.7 enforced by the compiler.
    pub fn update<D: CanDriver>(
        &mut self,
        driver: &D,
        on_transition: &mut impl FnMut(ErrorState, ErrorState, u8, u8),
    ) -> CanResult<ErrorState> {
        // is_bus_off is a fast path (typically a single register read)
        let new_state = if driver.is_bus_off() {
            ErrorState::BusOff
        } else {
            // error_counters() returns Result — must be handled (Rule 17.7)
            let (tec, rec) = driver.error_counters()?;

            if tec > 127u8 || rec > 127u8 {
                ErrorState::Passive
            } else {
                ErrorState::Active
            }
        };

        if new_state != self.prev_state {
            // Saturating add prevents overflow (MISRA Rule 12.1 equivalent)
            self.transitions = self.transitions.saturating_add(1);

            // Read counters again for the callback — may differ by one tick
            let (tec, rec) = driver.error_counters().unwrap_or((0, 0));
            on_transition(self.prev_state, new_state, tec, rec);

            self.prev_state = new_state;
        }

        Ok(new_state)
    }

    pub fn transition_count(&self) -> u32 { self.transitions }
    pub fn current_state(&self)    -> ErrorState { self.prev_state }
}

// ---------------------------------------------------------------------------
// Unit tests — in Rust, tests live alongside the code
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::can_driver::{CanError, CanResult};
    use crate::can_frame::DataFrame;

    struct MockDriver {
        tec: u8,
        rec: u8,
        bus_off: bool,
    }

    impl CanDriver for MockDriver {
        fn transmit_data(&mut self, _: &DataFrame) -> CanResult<()> { Ok(()) }
        fn transmit_remote(&mut self, _: &crate::can_frame::RemoteFrame)
            -> CanResult<()> { Ok(()) }
        fn receive(&mut self) -> CanResult<DataFrame> { Err(CanError::RxEmpty) }
        fn error_counters(&self) -> CanResult<(u8, u8)> {
            Ok((self.tec, self.rec))
        }
        fn is_bus_off(&self) -> bool { self.bus_off }
    }

    #[test]
    fn test_active_to_passive_transition() {
        let driver = MockDriver { tec: 130, rec: 0, bus_off: false };
        let mut monitor = ErrorMonitor::new();
        let mut transitions = Vec::new();

        let state = monitor.update(&driver, &mut |from, to, tec, rec| {
            transitions.push((from, to, tec, rec));
        }).expect("update should succeed");

        assert_eq!(state, ErrorState::Passive);
        assert_eq!(transitions.len(), 1);
        assert_eq!(transitions[0].0, ErrorState::Active);
        assert_eq!(transitions[0].1, ErrorState::Passive);
    }

    #[test]
    fn test_bus_off_detection() {
        let driver = MockDriver { tec: 255, rec: 0, bus_off: true };
        let mut monitor = ErrorMonitor::new();

        let state = monitor.update(&driver, &mut |_, _, _, _| {})
            .expect("update should succeed");

        assert_eq!(state, ErrorState::BusOff);
    }

    #[test]
    fn test_no_spurious_transitions() {
        let driver = MockDriver { tec: 10, rec: 5, bus_off: false };
        let mut monitor = ErrorMonitor::new();
        let mut count = 0u32;

        for _ in 0..100 {
            monitor.update(&driver, &mut |_, _, _, _| { count += 1; })
                .expect("update should succeed");
        }

        // Active → Active: zero transitions expected
        assert_eq!(count, 0);
        assert_eq!(monitor.transition_count(), 0);
    }
}
```

---

## 8. MISRA Rule Violation Examples and Fixes

The following table pairs common CAN coding patterns with the MISRA rule they violate and shows the corrected version.

### Violation 1 — Implicit Integer Narrowing (Rule 10.3)

```c
/* ❌ VIOLATION: assigning uint32_t to uint8_t without explicit cast */
uint32_t reg_val = CAN->RDL0R;
uint8_t  byte0   = reg_val;        /* Rule 10.3 — implicit narrowing  */

/* ✅ FIXED: explicit cast documents the intentional truncation */
uint8_t  byte0 = (uint8_t)(reg_val & 0xFFU);
```

### Violation 2 — Bitwise Op on Signed Type (Rule 10.1)

```c
/* ❌ VIOLATION: shifting a signed int */
int mask = 1 << 11;                /* Signed shift — UB if bit 31 set */

/* ✅ FIXED: unsigned type and unsigned constant */
uint32_t mask = 1U << 11U;
```

### Violation 3 — Unchecked Return Value (Rule 17.7)

```c
/* ❌ VIOLATION: return value discarded */
CAN_Transmit(handle, &frame);      /* What if it returns CAN_ERR_BUS_OFF? */

/* ✅ FIXED: explicitly check and handle every error code */
CAN_Status_t tx_status = CAN_Transmit(handle, &frame);
if (tx_status != CAN_OK)
{
    CAN_HandleTransmitError(tx_status);
}
```

### Violation 4 — Recursion (Rule 17.2)

```c
/* ❌ VIOLATION: recursive protocol state machine */
void CAN_ProcessState(CAN_State_t state)
{
    if (state == STATE_WAIT_ACK)
    {
        CAN_ProcessState(STATE_TRANSMIT);  /* Recursive — unbounded stack */
    }
}

/* ✅ FIXED: iterative state machine */
void CAN_ProcessStateMachine(CAN_Handle_t * const p_handle)
{
    CAN_State_t current = p_handle->state;

    /* Bounded iteration — maximum transitions per call */
    uint8_t guard = 10U;
    while ((current != STATE_IDLE) && (guard > 0U))
    {
        current = CAN_NextState(p_handle, current);
        guard--;
    }
}
```

### Violation 5 — Casting Away volatile (Rule 11.8)

```c
/* ❌ VIOLATION: volatile qualifier removed by cast */
volatile uint32_t * p_reg = &CAN->MCR;
uint32_t * p_non_volatile  = (uint32_t *)p_reg;  /* Rule 11.8 violation */
*p_non_volatile = 0x00000001U;                    /* Compiler may optimize out */

/* ✅ FIXED: always access hardware registers through volatile pointer */
volatile uint32_t * const p_reg = &CAN->MCR;
*p_reg = 0x00000001U;
```

### Violation 6 — Side Effect in Condition (Rule 13.3)

```c
/* ❌ VIOLATION: assignment with side effect inside condition */
if ((status = CAN_Receive(handle, &frame)) == CAN_OK)  /* Rule 13.3 */
{
    /* process frame */
}

/* ✅ FIXED: assignment separated from condition */
status = CAN_Receive(handle, &frame);
if (status == CAN_OK)
{
    /* process frame */
}
```

### Violation 7 — Multiple Return Points (Rule 15.5 — Required)

```c
/* ❌ VIOLATION: multiple returns */
CAN_Status_t CAN_Send(CAN_Handle_t *h, CAN_Frame_t *f)
{
    if (h == NULL) return CAN_ERR_NULL_PTR;   /* early exit */
    if (f == NULL) return CAN_ERR_NULL_PTR;   /* early exit */
    /* ... */
    return CAN_OK;
}

/* ✅ FIXED: single return variable pattern */
CAN_Status_t CAN_Send(CAN_Handle_t * const h, CAN_Frame_t * const f)
{
    CAN_Status_t status = CAN_OK;

    if (h == NULL)      { status = CAN_ERR_NULL_PTR; }
    else if (f == NULL) { status = CAN_ERR_NULL_PTR; }
    else                { /* transmit logic */        }

    return status;
}
```

---

## 9. Static Analysis and Tool Integration

Applying MISRA C rules manually in code review is error-prone. Static analysis tools automate the detection of decidable rule violations.

### Tool Comparison for CAN Code

| Tool | MISRA Support | CAN-Specific | IDE Integration | Cost |
|------|--------------|--------------|-----------------|------|
| **PC-lint Plus** | C:2012 full | Custom rules | VS Code, Eclipse | Commercial |
| **PRQA QA-C** | C:2012 full | ECU templates | Eclipse, JTAG | Commercial |
| **Polyspace** | C:2012 + C++ | AUTOSAR | MATLAB Simulink | Commercial |
| **Parasoft C/C++test** | C:2012 full | J1939 checks | CI/CD pipelines | Commercial |
| **Cppcheck** | Partial C:2012 | Limited | VS Code, CMake | Open source |
| **Clang-Tidy** | Partial (CERT) | Limited | Wide | Open source |

### Deviation Record Template

When a MISRA rule cannot be avoided (e.g., in a hardware ISR that must be as fast as possible), a deviation record is required:

```
┌──────────────────────────────────────────────────────────────────┐
│ MISRA C:2012 Deviation Record                                    │
├──────────────────────────────────────────────────────────────────┤
│ Rule:        15.5 — A function should have a single exit point   │
│ Category:    Required                                            │
│ Location:    can_isr.c, line 47, function CAN1_RX0_IRQHandler()  │
│ Reason:      Early return avoids disabling global interrupts     │
│              unnecessarily in a fast ISR path where the FIFO    │
│              empty condition is the common case.                 │
│ Risk:        Low — reviewed manually; all paths verified in     │
│              test suite (coverage 100% MC/DC).                  │
│ Approved by: [Safety Manager signature + date]                  │
└──────────────────────────────────────────────────────────────────┘
```

### CMakeLists.txt Integration Example

```cmake
# CMakeLists.txt — integrate PC-lint or cppcheck with CAN project

cmake_minimum_required(VERSION 3.20)
project(can_driver_misra C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Compiler flags that reinforce MISRA discipline
add_compile_options(
    -Wall
    -Wextra
    -Wshadow
    -Wconversion          # Catches implicit narrowing (MISRA Rule 10.3)
    -Wsign-conversion     # Catches signed/unsigned mix (MISRA Rule 10.1)
    -Wmissing-prototypes  # Catches undeclared functions (MISRA Rule 17.3)
    -pedantic
)

add_library(can_driver STATIC
    src/can_driver.c
    src/can_frame.c
    src/can_error.c
)

target_include_directories(can_driver PUBLIC include)

# Optional: cppcheck target
find_program(CPPCHECK cppcheck)
if (CPPCHECK)
    add_custom_target(misra_check
        COMMAND ${CPPCHECK}
            --enable=all
            --suppress=missingIncludeSystem
            --addon=misra.py
            --error-exitcode=1
            ${CMAKE_SOURCE_DIR}/src
        COMMENT "Running MISRA C cppcheck..."
    )
endif()
```

---

## 10. MISRA C vs Rust: Safety Comparison for CAN

The table below maps MISRA C:2012 rules to their Rust language-level equivalents, showing where Rust provides compile-time enforcement versus where MISRA requires external tooling.

| MISRA C:2012 Rule | Risk | Rust Equivalent | Enforcement |
|-------------------|------|-----------------|-------------|
| Rule 10.1 — No bitwise ops on signed types | Undefined behaviour | No implicit sign extension; all arithmetic is typed | Compiler type system |
| Rule 10.3 — No implicit narrowing | Data corruption | No implicit coercion; `as` cast requires explicit choice | Compiler + Clippy |
| Rule 17.7 — Check return values | Silent failure | `Result<T,E>` with `#[must_use]` | Compiler warning |
| Rule 17.2 — No recursion | Stack overflow | Not enforced; use `#[deny(clippy::recursion)]` | Clippy lint |
| Rule 18.1 — No out-of-bounds pointer arithmetic | Memory corruption | Panic on out-of-bounds slice index | Runtime (debug) / compiler (const) |
| Rule 21.3 — No dynamic memory | Non-determinism | `no_std` + heapless crates by default | Build configuration |
| Rule 11.8 — No volatile cast removal | Incorrect HW access | `core::ptr::read_volatile` / `write_volatile` | Explicit API |
| Rule 15.5 — Single exit point | Control flow complexity | Pattern match exhaustion | Compiler |
| Rule 14.3 — No dead code | Hidden bugs | `#[deny(unreachable_patterns)]`, `#[deny(dead_code)]` | Compiler |
| Rule 7.2 — Unsigned suffix on literals | Signed arithmetic | Integer literals are typed; inferred from context | Compiler |
| Dir 4.7 — Test error info | Undetected failures | `?` operator propagates; `unwrap()` panics explicitly | Language idiom |

**Key takeaway:** Rust eliminates most MISRA C:2012 _mandatory_ and _required_ rules at the type system or compiler level, leaving only advisory style rules and architecture-level concerns (recursion depth, ISR stack size, deterministic timing) to be handled by the developer and additional tooling. For environments that mandate MISRA C compliance (ISO 26262, IEC 61508, DO-178C), Rust can serve as a complementary implementation language, with formal proofs in progress through the Ferrocene and RustSafety initiatives.

---

## 11. Summary

| Aspect | Key Points |
|--------|-----------|
| **MISRA C Purpose** | Restricts C/C++ constructs that cause undefined, implementation-defined, or unspecified behaviour in embedded safety-critical systems |
| **Most Critical Rules for CAN** | Rule 10.1/10.3 (types), Rule 17.7 (return values), Rule 17.2 (no recursion), Rule 11.8 (volatile), Rule 18.1 (bounds), Dir 4.7 (error testing) |
| **Integer Safety** | All CAN IDs, DLC values, and register masks must use unsigned fixed-width types (`uint8_t`, `uint32_t`) with `U` suffixes on all literals |
| **Pointer Safety** | Hardware registers must always remain `volatile`; no pointer arithmetic outside verified bounds; `NULL` checked before every dereference |
| **Control Flow** | No recursion in CAN state machines; all loops must terminate; single return point per function; all branches reachable |
| **Error Handling** | Every function returns a status code; every call site must explicitly check the return value; deviations require documented records |
| **C++ Extensions** | MISRA C++:2008 / Autosar C++14 additionally forbids exceptions, RTTI, dynamic allocation, and unrestricted virtual dispatch |
| **Rust Advantage** | Eliminates most mandatory/required MISRA C violations through the type system and ownership model, with compiler-level enforcement rather than tool-level checking |
| **Tool Chain** | PC-lint Plus, PRQA QA-C, Polyspace for commercial-grade MISRA checking; Cppcheck for open-source; integrate into CI/CD with deviation tracking |
| **Deviation Process** | Any rule departure requires a written deviation record with rule number, location, justification, risk assessment, and safety manager sign-off |
| **Functional Safety** | MISRA C compliance is a required evidence artefact for ISO 26262 ASIL-B/C/D, IEC 61508 SIL-2/3, and DO-178C DAL-B/C software qualification |

Applying MISRA C to CAN driver and stack code is not merely a bureaucratic compliance exercise — it is a systematic approach to eliminating the class of subtle, determinism-breaking bugs that are most likely to evade testing but cause field failures in safety-critical systems. The rules are most effective when integrated from the start of development, enforced automatically by static analysis in the CI pipeline, and complemented by a rigorous deviation approval process for the rare cases where full compliance conflicts with hardware constraints.

---

*End of Document — MISRA C Guidelines for CAN Code*