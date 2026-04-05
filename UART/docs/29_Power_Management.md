# 29. UART Power Management

**Core concepts** — Power state hierarchies (Run → Sleep → Stop → Standby), clock gating, low-speed clock sources (LSI/LSE), baud rate limitations in low-power mode, and the three WoR trigger types: start-bit detection, address match, and FIFO threshold.

**C/C++ examples** include:
- **STM32 LPUART** with LSE clock source, Stop 1 mode entry, and HAL wake-on-receive configuration
- **STM32 Address Match** wake-up for RS-485 multidrop networks (mute mode)
- **ESP32 light sleep** with `esp_sleep_enable_uart_wakeup()` and FreeRTOS event queues
- **Linux/POSIX** epoll-based non-blocking UART with sysfs wakeup source enablement

**Rust examples** include:
- **Embassy async** UART with the low-power executor automatically calling `WFI` at every `await`
- **Embassy Stop mode** with LSE-clocked LPUART and `enable_stop_mode_wakeup`
- **RTIC** with the `#[idle]` task executing `cortex_m::asm::wfi()`
- **Cross-platform trait abstraction** for power-aware UART behind a `PowerAwareUart` trait

**Six common pitfalls** are documented with code fixes, plus a power consumption comparison table showing the ~1000× current reduction achievable with Stop mode + LPUART vs. full-power active operation.


## Low-Power UART Modes and Wake-on-Receive Functionality

---

## Table of Contents

1. [Introduction](#introduction)
2. [Power States and UART](#power-states-and-uart)
3. [Low-Power UART Modes](#low-power-uart-modes)
   - [Clock Gating](#clock-gating)
   - [Baud Rate Clock Sources](#baud-rate-clock-sources)
   - [Auto Baud Detection in Low-Power Mode](#auto-baud-detection-in-low-power-mode)
4. [Wake-on-Receive (WoR)](#wake-on-receive-wor)
   - [Start Bit Detection Wake-Up](#start-bit-detection-wake-up)
   - [Address Match Wake-Up](#address-match-wake-up)
   - [FIFO Threshold Wake-Up](#fifo-threshold-wake-up)
5. [DMA-Assisted Low-Power Reception](#dma-assisted-low-power-reception)
6. [Platform-Specific Considerations](#platform-specific-considerations)
7. [C/C++ Code Examples](#cc-code-examples)
   - [STM32: Low-Power UART with Wake-on-Receive](#stm32-low-power-uart-with-wake-on-receive)
   - [ESP32: Light Sleep with UART Wake-Up](#esp32-light-sleep-with-uart-wake-up)
   - [Generic POSIX: Low-Power Aware UART](#generic-posix-low-power-aware-uart)
8. [Rust Code Examples](#rust-code-examples)
   - [Embassy (STM32): Async UART in Low-Power Mode](#embassy-stm32-async-uart-in-low-power-mode)
   - [RTIC with UART Wake-Up](#rtic-with-uart-wake-up)
   - [Cross-Platform Trait Abstraction](#cross-platform-trait-abstraction)
9. [Power Consumption Comparison](#power-consumption-comparison)
10. [Common Pitfalls and Best Practices](#common-pitfalls-and-best-practices)
11. [Summary](#summary)

---

## Introduction

In embedded systems and IoT devices, the UART peripheral is often a critical communication interface — but it can also be a significant source of power drain if left running at full speed continuously. Modern microcontrollers offer sophisticated power management features that allow UART interfaces to remain responsive while the system operates at drastically reduced power consumption.

UART power management encompasses two broad strategies:

- **Low-power UART modes**: Running the UART peripheral itself at reduced clock speeds, using lower-power oscillators, or gating the clock when the peripheral is idle.
- **Wake-on-receive (WoR)**: Placing the CPU and most peripherals into a deep sleep or stop state, but configuring the UART receiver to detect incoming data and wake the system automatically.

Together, these techniques enable designs where a device can sleep drawing microamps, yet respond within milliseconds to an incoming serial message.

---

## Power States and UART

Most microcontrollers define a hierarchy of power states, commonly including:

| State | CPU | High-Speed Clocks | Low-Speed Clocks | UART | Current |
|---|---|---|---|---|---|
| Run | Active | On | On | Full | ~10–100 mA |
| Sleep | Halted | On | On | Full | ~1–10 mA |
| Low-Power Run | Active (slow) | Off | On | LP mode | ~100–500 µA |
| Stop / Deep Sleep | Halted | Off | On (LSI/LSE) | WoR only | ~1–50 µA |
| Standby / Hibernate | Halted | Off | Off | None | ~0.1–5 µA |
| Shutdown | Off | Off | Off | None | <1 µA |

UART functionality is generally available in **Run**, **Sleep**, **Low-Power Run**, and with special hardware support, in **Stop** modes. Deeper states (Standby, Shutdown) typically require external interrupt pins for wake-up, not UART reception.

---

## Low-Power UART Modes

### Clock Gating

The simplest form of UART power saving is **clock gating**: disabling the peripheral clock when the UART is not actively transmitting or receiving. This prevents the UART registers and internal logic from toggling unnecessarily.

On most MCUs, clock gating is controlled through the RCC (Reset and Clock Control) or equivalent peripheral clock enable registers. When the clock is gated, the UART configuration is preserved; it can be re-enabled almost instantly.

```
[System Clock] --[Clock Gate]---> [UART Peripheral]
                     |
                 [SW Control]
```

Clock gating should be combined with disabling the UART transmitter and receiver when idle, especially if the pin will float or be driven to a known level externally.

### Baud Rate Clock Sources

In standard operation, UART baud rate generators derive their reference from the high-speed system clock (HSI, HSE, PLL). In low-power modes, these clocks are often stopped. Many modern MCUs provide an alternative UART clock source:

| Clock Source | Frequency | Available in Stop | Accuracy |
|---|---|---|---|
| HSI (High-Speed Internal) | 16–64 MHz | No | ±1–2% |
| HSE (High-Speed External) | 4–48 MHz | No | Very high |
| PLL | Up to 500 MHz | No | High |
| LSI (Low-Speed Internal) | 32–37 kHz | Yes | ±5–15% |
| LSE (Low-Speed External) | 32.768 kHz | Yes | Very high |

When using LSI or LSE as the UART clock source in low-power mode, baud rates are severely limited. At 32.768 kHz, the maximum practical baud rate is around 2400 bps (with 32.768 kHz / 16 ≈ 2048 baud). This is often sufficient for simple wake-up commands.

**Key formula for low-power baud rate:**

```
BaudRate = ClockFrequency / (Oversampling × BRR_Value)
```

where `Oversampling` is typically 16 (or 8 in some modes), and `BRR_Value` is the baud rate register integer divider.

### Auto Baud Detection in Low-Power Mode

Some UARTs support **auto-baud detection** (ABD), which measures the duration of the first received character to determine the baud rate automatically. In low-power contexts, this can be combined with wake-on-start-bit: the MCU wakes on the first start bit edge, runs ABD to lock onto the sender's baud rate, then processes the message, and returns to sleep.

---

## Wake-on-Receive (WoR)

Wake-on-receive is a hardware feature where the UART receiver (or an associated low-power comparator) monitors the RX line for activity while the CPU is sleeping. When activity is detected, a wake-up event is generated.

### Start Bit Detection Wake-Up

The most fundamental WoR mechanism. The UART receiver watches for a falling edge on the RX pin (idle HIGH → start bit LOW transition). When detected, an interrupt or wake-up signal is asserted before the first data bit arrives. This gives the CPU time to wake and configure itself before the character is complete (at typical baud rates of 9600–115200 bps, a character takes 86–868 µs).

```
RX Line:  _____|S|D0|D1|D2|D3|D4|D5|D6|D7|P|STOP|____

Wake event fires here ↑
CPU begins waking up ←——→ Character complete
```

**Critical timing constraint**: The CPU must wake fast enough to read the received data before it is overwritten or an overrun occurs. With a hardware FIFO (even 1 byte), this constraint is relaxed.

### Address Match Wake-Up

More sophisticated MCUs support **9-bit addressing** or **address match** wake-up. In multidrop RS-485 or multiprocessor UART networks, only packets addressed to this node should wake the MCU.

The UART operates in **mute mode** — it monitors the line but suppresses all received characters and interrupts until it detects an address byte matching a programmed value. Only then does it generate a wake-up event.

This is a major power saver in bus systems where many nodes share a line and most traffic is not addressed to this node.

```
[Node A] ─────────────────────── RS-485 Bus ──────────────────────
              [Addr 0x01] → [Data] [Data]   (Only Node 1 wakes)
              [Addr 0x02] → [Data] [Data]   (Only Node 2 wakes)
              [Addr 0xFF] → [Data] [Data]   (All nodes wake - broadcast)
```

### FIFO Threshold Wake-Up

Some MCUs combine low-power sleep with a DMA controller and a receive FIFO. The UART fills the FIFO autonomously (without CPU involvement), and the CPU only wakes when the FIFO reaches a programmed threshold. This is ideal for protocols with fixed-length messages: configure the FIFO depth to match the message length, and wake only when a complete message has arrived.

---

## DMA-Assisted Low-Power Reception

In systems with a DMA controller that can run during CPU sleep (but not deep stop), DMA-assisted UART reception allows the CPU to sleep while data flows into a memory buffer:

```
[UART RX HW] --[DMA Request]--> [DMA Controller] ---> [Memory Buffer]
                                                              |
                                          [DMA Transfer Complete Interrupt]
                                                              |
                                                    [Wake CPU, Process Buffer]
```

This pattern is highly efficient for high-throughput data at moderate duty cycles. The DMA controller consumes only a fraction of the power of an active CPU.

---

## Platform-Specific Considerations

| Platform | Low-Power UART Feature | Peripheral Name | Wake-from-Stop |
|---|---|---|---|
| STM32L/G/U series | LPUART with LSI/LSE clock | LPUART1 | Yes |
| STM32F series | USART with start-bit detection | USART1-6 | Limited |
| ESP32 | Light Sleep + UART wake | UART0-2 | Yes (light sleep only) |
| nRF52 series | UARTE with low-power mode | UARTE0/1 | Yes |
| SAMD21/51 | SERCOM USART in standby | SERCOMx | Yes |
| RP2040 | PIO-based UART (custom) | PIO0/1 | No (PIO stops) |
| Linux (SoC) | autosuspend, wakeup sysfs | ttyS/ttyUSB | Platform-specific |

---

## C/C++ Code Examples

### STM32: Low-Power UART with Wake-on-Receive

This example configures the STM32 LPUART1 peripheral to use the LSE clock source (32.768 kHz), enabling reception at 2400 baud from Stop mode. The CPU enters Stop 1 mode and wakes on receipt of a character.

```c
/**
 * STM32L4 LPUART Low-Power Wake-on-Receive Example
 * Uses LSE (32.768 kHz) as LPUART clock source
 * Baud rate: 2400 bps (practical maximum with LSE)
 * CPU enters Stop 1 mode; wakes on LPUART character received
 */

#include "stm32l4xx_hal.h"

/* Handle declarations */
UART_HandleTypeDef hlpuart1;

/* Received data buffer */
static volatile uint8_t rx_byte = 0;
static volatile uint8_t data_received = 0;

/**
 * Configure LPUART1 with LSE clock source for low-power operation.
 *
 * At 32.768 kHz LSE, max baud ≈ 2048. We use 2400 with HSI during
 * init, then switch to LSE before entering Stop mode.
 */
void LPUART1_LP_Init(void)
{
    /* Step 1: Enable LSE clock */
    RCC_OscInitTypeDef osc_init = {0};
    osc_init.OscillatorType = RCC_OSCILLATORTYPE_LSE;
    osc_init.LSEState       = RCC_LSE_ON;
    HAL_RCC_OscConfig(&osc_init);

    /* Step 2: Select LSE as LPUART1 clock source */
    RCC_PeriphCLKInitTypeDef periphclk = {0};
    periphclk.PeriphClockSelection  = RCC_PERIPHCLK_LPUART1;
    periphclk.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_LSE;
    HAL_RCCEx_PeriphCLKConfig(&periphclk);

    /* Step 3: Enable LPUART1 clock */
    __HAL_RCC_LPUART1_CLK_ENABLE();

    /* Step 4: Enable LPUART1 clock in Stop mode so it can wake us */
    __HAL_RCC_LPUART1_CLKAM_ENABLE(); /* Autonomous mode: keep clocked in Stop */

    /* Step 5: Configure GPIO for LPUART1 RX (e.g. PC0 on STM32L4) */
    GPIO_InitTypeDef gpio = {0};
    __HAL_RCC_GPIOC_CLK_ENABLE();
    gpio.Pin       = GPIO_PIN_0;
    gpio.Mode      = GPIO_MODE_AF_PP;
    gpio.Pull      = GPIO_PULLUP; /* Keep line idle-HIGH */
    gpio.Speed     = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate = GPIO_AF8_LPUART1;
    HAL_GPIO_Init(GPIOC, &gpio);

    /* Step 6: Configure LPUART1 peripheral */
    hlpuart1.Instance          = LPUART1;
    hlpuart1.Init.BaudRate     = 2400;       /* Must be achievable with 32.768 kHz LSE */
    hlpuart1.Init.WordLength   = UART_WORDLENGTH_8B;
    hlpuart1.Init.StopBits     = UART_STOPBITS_1;
    hlpuart1.Init.Parity       = UART_PARITY_NONE;
    hlpuart1.Init.Mode         = UART_MODE_RX; /* RX only for wake-up */
    hlpuart1.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    hlpuart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&hlpuart1);

    /* Step 7: Configure wake-up from Stop on character received */
    UART_WakeUpTypeDef wakeup = {0};
    wakeup.WakeUpEvent = UART_WAKEUP_ON_READDATA_NONEMPTY; /* Wake when RDR is not empty */
    HAL_UARTEx_StopModeWakeUpSourceConfig(&hlpuart1, wakeup);

    /* Step 8: Enable LPUART1 wake-up from Stop mode */
    HAL_UARTEx_EnableStopMode(&hlpuart1);

    /* Step 9: Enable LPUART1 interrupt in NVIC */
    HAL_NVIC_SetPriority(LPUART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LPUART1_IRQn);

    /* Step 10: Start receiving in interrupt mode */
    HAL_UART_Receive_IT(&hlpuart1, (uint8_t *)&rx_byte, 1);
}

/**
 * Enter CPU Stop 1 mode.
 * LPUART1 continues monitoring RX line via LSE clock.
 * Returns after LPUART1 generates wake-up event.
 */
void Enter_Stop1_Mode(void)
{
    /* Ensure all pending transmissions are complete before sleeping */
    HAL_UART_StateTypeDef state = HAL_UART_GetState(&hlpuart1);
    while ((state & HAL_UART_STATE_BUSY_TX) != 0) {
        state = HAL_UART_GetState(&hlpuart1);
    }

    /* Clear wake-up flags */
    __HAL_PWR_CLEAR_FLAG(PWR_FLAG_WUF);

    /* Enter Stop 1 mode: SRAM and registers retained, HSI/HSE/PLL off */
    HAL_PWREx_EnterSTOP1Mode(PWR_STOPENTRY_WFI); /* Wait-for-interrupt */

    /* ---- CPU RESUMES HERE AFTER LPUART1 WAKE-UP ---- */

    /* Restore system clock (Stop mode switches to MSI by default) */
    SystemClock_Restore(); /* Your clock re-init function */
}

/**
 * LPUART1 interrupt handler.
 * Called when a byte is received (wake-up event).
 */
void LPUART1_IRQHandler(void)
{
    HAL_UART_IRQHandler(&hlpuart1);
}

/**
 * HAL RX complete callback.
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance == LPUART1) {
        data_received = 1;
        /* Restart reception for next byte */
        HAL_UART_Receive_IT(&hlpuart1, (uint8_t *)&rx_byte, 1);
    }
}

/**
 * Main application loop demonstrating low-power UART wake pattern.
 */
int main(void)
{
    HAL_Init();
    SystemClock_Config(); /* Configure main clocks */
    LPUART1_LP_Init();

    while (1) {
        if (data_received) {
            data_received = 0;
            uint8_t byte = rx_byte;

            /* Process received byte */
            Process_Command(byte);
        }

        /* Return to Stop 1 mode until next byte arrives */
        Enter_Stop1_Mode();
    }
}
```

---

### STM32: Address Match Wake-Up (Multiprocessor Mode)

```c
/**
 * STM32 UART Address Match Wake-Up Example
 * Useful in RS-485 multidrop systems: only wake when our address is seen.
 * Address byte has MSB set (9-bit frame); data bytes have MSB clear.
 */

#include "stm32l4xx_hal.h"

#define MY_NODE_ADDRESS  0x42U  /* 7-bit address for this node */

UART_HandleTypeDef huart1;

void UART1_AddressMatch_Init(void)
{
    /* Standard UART init (assume GPIO and clocks already configured) */
    huart1.Instance          = USART1;
    huart1.Init.BaudRate     = 9600;
    huart1.Init.WordLength   = UART_WORDLENGTH_9B; /* 9-bit for address bit */
    huart1.Init.StopBits     = UART_STOPBITS_1;
    huart1.Init.Parity       = UART_PARITY_NONE;
    huart1.Init.Mode         = UART_MODE_RX;
    huart1.Init.HwFlowCtl   = UART_HWCONTROL_NONE;
    huart1.Init.OverSampling = UART_OVERSAMPLING_16;
    HAL_UART_Init(&huart1);

    /* Configure mute mode: suppress RX until address match */
    /* Address is 4-bit (upper nibble of the address byte by default) */
    /* or 7-bit depending on ADDM7 bit setting                        */
    UART_WakeUpTypeDef wakeup = {0};
    wakeup.WakeUpEvent  = UART_WAKEUP_ON_ADDRESS;
    wakeup.AddressLength = UART_ADDRESS_DETECT_7B; /* 7-bit address detection */
    wakeup.Address       = MY_NODE_ADDRESS;         /* Our node address */
    HAL_UARTEx_StopModeWakeUpSourceConfig(&huart1, wakeup);

    /* Enable mute mode: UART will suppress all input until address match */
    HAL_MultiProcessor_EnableMuteMode(&huart1);
    HAL_MultiProcessor_EnterMuteMode(&huart1);

    /* Enable Stop mode wake-up */
    HAL_UARTEx_EnableStopMode(&huart1);

    HAL_NVIC_SetPriority(USART1_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
}

/**
 * After wake-up on address match, receive the following data bytes.
 */
void Receive_Message_After_WakeUp(uint8_t *buffer, uint16_t length)
{
    /* The UART automatically exits mute mode after address match */
    /* Now receive the data payload with a timeout */
    HAL_StatusTypeDef status = HAL_UART_Receive(&huart1, buffer, length, 100);

    if (status == HAL_OK) {
        Process_Received_Message(buffer, length);
    }

    /* Re-enter mute mode for next sleep cycle */
    HAL_MultiProcessor_EnterMuteMode(&huart1);
}
```

---

### ESP32: Light Sleep with UART Wake-Up

```c
/**
 * ESP32 Light Sleep Wake-on-UART Example
 * Using ESP-IDF v5.x APIs
 *
 * In light sleep, peripherals retain state. The UART receiver can
 * continue operating and wake the CPU on threshold event.
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "esp_sleep.h"
#include "esp_log.h"

#define TAG         "UART_LP"
#define UART_PORT   UART_NUM_0
#define UART_TX_PIN 1
#define UART_RX_PIN 3
#define BAUD_RATE   115200
#define BUF_SIZE    1024

static QueueHandle_t uart_queue;

/**
 * Initialize UART with event queue for interrupt-driven reception.
 */
static void uart_lp_init(void)
{
    uart_config_t uart_cfg = {
        .baud_rate  = BAUD_RATE,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    ESP_ERROR_CHECK(uart_driver_install(UART_PORT, BUF_SIZE, BUF_SIZE,
                                        10, &uart_queue, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_PORT, &uart_cfg));
    ESP_ERROR_CHECK(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN,
                                  UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
}

/**
 * Configure UART as wake-up source for light sleep.
 * The ESP32 wakes when a configurable number of positive edges
 * are detected on the RX pin (each start bit = 1 positive edge after line goes low).
 *
 * Note: 'threshold' here is the number of rising edges on RX.
 * For standard 8N1 UART, set threshold = 3 (detects activity reliably).
 */
static void configure_uart_wakeup(int threshold)
{
    ESP_ERROR_CHECK(uart_set_wakeup_threshold(UART_PORT, threshold));
    ESP_ERROR_CHECK(esp_sleep_enable_uart_wakeup(UART_PORT));
}

/**
 * UART event task: processes incoming data after wake-up.
 */
static void uart_event_task(void *arg)
{
    uart_event_t event;
    uint8_t *dtmp = malloc(BUF_SIZE);

    while (1) {
        if (xQueueReceive(uart_queue, &event, portMAX_DELAY)) {
            switch (event.type) {
                case UART_DATA:
                    {
                        int len = uart_read_bytes(UART_PORT, dtmp,
                                                   event.size, portMAX_DELAY);
                        ESP_LOGI(TAG, "Received %d bytes after wake-up", len);
                        /* Process data here */
                        dtmp[len] = '\0';
                        ESP_LOGI(TAG, "Data: %s", dtmp);
                    }
                    break;

                case UART_FIFO_OVF:
                    ESP_LOGW(TAG, "FIFO overflow");
                    uart_flush_input(UART_PORT);
                    xQueueReset(uart_queue);
                    break;

                case UART_BUFFER_FULL:
                    ESP_LOGW(TAG, "Ring buffer full");
                    uart_flush_input(UART_PORT);
                    xQueueReset(uart_queue);
                    break;

                default:
                    break;
            }
        }
    }
    free(dtmp);
    vTaskDelete(NULL);
}

/**
 * Power management task: enters light sleep and wakes on UART.
 */
static void power_manager_task(void *arg)
{
    while (1) {
        ESP_LOGI(TAG, "Entering light sleep... waiting for UART activity");

        /* Flush any pending output before sleeping */
        uart_wait_tx_idle_polling(UART_PORT);

        /* Enter light sleep: CPU halted, UART RX monitoring active */
        esp_light_sleep_start();

        /* ---- EXECUTION RESUMES HERE ON WAKE-UP ---- */

        esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
        if (cause == ESP_SLEEP_WAKEUP_UART) {
            ESP_LOGI(TAG, "Woke up from UART activity");
            /* UART event task will process the data */
            /* Give event task time to drain the FIFO */
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            ESP_LOGI(TAG, "Woke up from cause: %d", cause);
        }

        /* Re-arm and go back to sleep */
        /* Small delay to allow for additional data before next sleep */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    uart_lp_init();
    configure_uart_wakeup(3); /* 3 edges = reliable start-bit detection */

    xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);
    xTaskCreate(power_manager_task, "power_mgr", 2048, NULL, 5, NULL);
}
```

---

### Generic POSIX: Low-Power Aware UART

```c
/**
 * Linux/POSIX: UART with system power management awareness.
 *
 * On Linux SoCs, power management is handled by the kernel PM framework.
 * User space can participate via:
 *  - /sys/bus/platform/devices/.../power/wakeup  (enable wakeup source)
 *  - /sys/bus/platform/devices/.../power/autosuspend_delay_ms
 *  - epoll/select with O_NONBLOCK for non-blocking, power-efficient I/O
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>

#define UART_DEVICE   "/dev/ttyS1"
#define WAKEUP_SYSFS  "/sys/class/tty/ttyS1/device/power/wakeup"
#define MAX_EVENTS    1
#define BUF_SIZE      256

/**
 * Configure UART port for low-power aware operation.
 * Uses O_NONBLOCK to avoid blocking the CPU in read().
 */
static int uart_open_lp(const char *device, speed_t baud)
{
    int fd = open(device, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (fd < 0) {
        perror("open UART");
        return -1;
    }

    struct termios tty;
    if (tcgetattr(fd, &tty) != 0) {
        perror("tcgetattr");
        close(fd);
        return -1;
    }

    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    /* 8N1, no flow control */
    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_cflag |= CREAD | CLOCAL;

    /* Raw mode: no canonical processing */
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;

    /* Non-blocking: return immediately if no data */
    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        perror("tcsetattr");
        close(fd);
        return -1;
    }

    return fd;
}

/**
 * Enable UART as a system wakeup source via sysfs.
 * This allows the UART to wake the system from suspend.
 */
static int enable_uart_wakeup(void)
{
    FILE *f = fopen(WAKEUP_SYSFS, "w");
    if (!f) {
        /* Not fatal: device may not support this */
        fprintf(stderr, "Warning: cannot set wakeup source: %s\n",
                strerror(errno));
        return -1;
    }
    fprintf(f, "enabled\n");
    fclose(f);
    printf("UART wakeup source enabled\n");
    return 0;
}

/**
 * Event-driven UART reader using epoll.
 * CPU blocks in epoll_wait (low-power idle) until data arrives.
 * This is the power-efficient alternative to busy-polling.
 */
static int uart_epoll_loop(int uart_fd)
{
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        return -1;
    }

    struct epoll_event ev = {
        .events  = EPOLLIN,
        .data.fd = uart_fd,
    };
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, uart_fd, &ev);

    struct epoll_event events[MAX_EVENTS];
    uint8_t buf[BUF_SIZE];

    printf("Waiting for UART data (power-efficient epoll)...\n");

    while (1) {
        /* Block here until data arrives: CPU enters idle/sleep state */
        int n = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
        if (n < 0) {
            if (errno == EINTR) continue; /* Interrupted by signal, retry */
            perror("epoll_wait");
            break;
        }

        for (int i = 0; i < n; i++) {
            if (events[i].data.fd == uart_fd &&
                (events[i].events & EPOLLIN)) {

                ssize_t len = read(uart_fd, buf, sizeof(buf) - 1);
                if (len > 0) {
                    buf[len] = '\0';
                    printf("Received %zd bytes: %s\n", len, buf);
                    /* Process data here */
                } else if (len < 0 && errno != EAGAIN) {
                    perror("read UART");
                }
            }
        }
    }

    close(epoll_fd);
    return 0;
}

int main(void)
{
    enable_uart_wakeup();

    int fd = uart_open_lp(UART_DEVICE, B115200);
    if (fd < 0) return EXIT_FAILURE;

    uart_epoll_loop(fd);

    close(fd);
    return EXIT_SUCCESS;
}
```

---

## Rust Code Examples

### Embassy (STM32): Async UART in Low-Power Mode

Embassy is an async embedded framework for Rust that integrates deeply with MCU power management. UART operations are `async`, allowing the executor to sleep the CPU while waiting.

```rust
//! Embassy STM32 Low-Power UART Example
//! 
//! Uses embassy-stm32 with LPUART1 and the low-power executor.
//! The executor automatically enters WFI (Wait For Interrupt) when
//! all tasks are awaiting, which puts the CPU into Sleep mode.
//! For Stop mode, use `embassy_stm32::low_power::Executor`.
//!
//! Cargo.toml dependencies:
//!   embassy-executor = { version = "0.5", features = ["arch-cortex-m", "executor-thread"] }
//!   embassy-stm32   = { version = "0.1", features = ["stm32l476rg", "time-driver-any", "unstable-pac"] }
//!   embassy-time    = { version = "0.3" }

#![no_std]
#![no_main]

use embassy_executor::Spawner;
use embassy_stm32::usart::{Config, UartRx};
use embassy_stm32::lpuart::Lpuart;
use embassy_stm32::{bind_interrupts, peripherals};
use embassy_time::{Duration, Timer};
use defmt::{info, warn};
use {defmt_rtt as _, panic_probe as _};

bind_interrupts!(struct Irqs {
    LPUART1 => embassy_stm32::usart::InterruptHandler<peripherals::LPUART1>;
});

/// UART receiver task.
/// The `await` on `read_until_idle` or `read` allows Embassy's executor
/// to sleep the CPU (WFI/WFE) until UART data arrives.
#[embassy_executor::task]
async fn uart_receiver_task(
    mut rx: UartRx<'static, peripherals::LPUART1, peripherals::DMA1_CH6>,
) {
    let mut buf = [0u8; 64];

    loop {
        // This await point is where the CPU sleeps.
        // Embassy executor calls WFI, reducing CPU consumption to ~10-100µA
        // depending on clock configuration.
        match rx.read_until_idle(&mut buf).await {
            Ok(n) if n > 0 => {
                info!("Received {} bytes: {:?}", n, &buf[..n]);
                process_command(&buf[..n]);
            }
            Ok(_) => {
                // Idle timeout with no data (line went idle)
            }
            Err(e) => {
                warn!("UART RX error: {:?}", e);
            }
        }
    }
}

/// Periodic work task.
/// While this task sleeps via Timer::after, the executor can run
/// uart_receiver_task or sleep the CPU entirely.
#[embassy_executor::task]  
async fn periodic_task() {
    loop {
        // CPU can sleep here while waiting for timer
        Timer::after(Duration::from_secs(5)).await;
        info!("Periodic heartbeat");
        // Do periodic maintenance work
    }
}

fn process_command(data: &[u8]) {
    info!("Processing command: {:02x}", data);
    // Command processing logic
}

#[embassy_executor::main]
async fn main(spawner: Spawner) {
    let p = embassy_stm32::init(embassy_stm32::Config::default());

    // Configure LPUART1
    let mut config = Config::default();
    config.baudrate = 2400; // Use 2400 for LSE-clocked LPUART in Stop mode

    // Create UART RX with DMA for async operation
    // DMA allows reception without CPU involvement until transfer completes
    let rx = UartRx::new(
        p.LPUART1,
        Irqs,
        p.PC0,          // RX pin
        p.DMA1_CH6,     // DMA channel for RX
        config,
    )
    .expect("LPUART1 init failed");

    // Spawn tasks; executor manages CPU sleep between events
    spawner.spawn(uart_receiver_task(rx)).unwrap();
    spawner.spawn(periodic_task()).unwrap();

    // Main task exits; executor runs spawned tasks
}
```

---

### Embassy: Stop Mode with LPUART Wake-Up

```rust
//! Embassy Stop Mode Example with LPUART Wake-Up
//!
//! Uses embassy_stm32::low_power which implements a proper
//! low-power executor that enters Stop mode between events.

#![no_std]
#![no_main]

use embassy_stm32::lpuart::{LpuartConfig, LpuartRx};
use embassy_stm32::rcc::{LseDrive, RccConfig};
use embassy_stm32::low_power::Executor;
use static_cell::StaticCell;
use defmt::info;

/// Configure RCC to use LSE for LPUART so it works in Stop mode.
fn make_rcc_config() -> RccConfig {
    let mut config = RccConfig::default();
    // Enable LSE (32.768 kHz crystal)
    config.lse = Some(LseDrive::MediumLow);
    // Route LSE to LPUART1 clock mux
    config.lpuart1_src = Some(embassy_stm32::rcc::LpuartClockSource::Lse);
    config
}

static EXECUTOR: StaticCell<Executor> = StaticCell::new();

#[cortex_m_rt::entry]
fn main() -> ! {
    // The low-power executor is initialized statically and runs forever.
    // It enters Stop1 mode when all tasks are blocked, and wakes
    // automatically on LPUART1 interrupt (character received).
    let executor = EXECUTOR.init(Executor::new());

    executor.run(|spawner| {
        let mut stm32_config = embassy_stm32::Config::default();
        stm32_config.rcc = make_rcc_config();
        let p = embassy_stm32::init(stm32_config);

        // Create LPUART RX — operates during Stop mode via LSE clock
        let mut lp_config = LpuartConfig::default();
        lp_config.baudrate = 2400;
        lp_config.enable_stop_mode_wakeup = true; // Key: allow wake from Stop

        let rx = LpuartRx::new(p.LPUART1, p.PC0, lp_config)
            .expect("LPUART init failed");

        spawner.spawn(receive_loop(rx)).unwrap();
    });
}

#[embassy_executor::task]
async fn receive_loop(mut rx: LpuartRx<'static>) {
    let mut byte = [0u8; 1];
    loop {
        // CPU enters Stop1 mode here via the low-power executor.
        // LPUART1 (clocked by LSE) watches the RX line.
        // Wake-up event fires → executor resumes → read completes.
        rx.read(&mut byte).await.expect("RX error");
        info!("Wake-up! Received byte: 0x{:02x}", byte[0]);
        handle_wakeup_byte(byte[0]);
    }
}

fn handle_wakeup_byte(byte: u8) {
    // Interpret the wake-up command
    match byte {
        0xA0 => info!("Command: Start sensor reading"),
        0xA1 => info!("Command: Transmit stored data"),
        0xFF => info!("Command: Deep sleep indefinitely"),
        _    => info!("Unknown command: 0x{:02x}", byte),
    }
}
```

---

### RTIC with UART Wake-Up

```rust
//! RTIC v2 UART Low-Power Example
//!
//! RTIC's idle task is the natural place to enter low-power modes.
//! The UART interrupt fires on reception and wakes the CPU.

#![no_std]
#![no_main]

use rtic::app;
use stm32l4xx_hal::{
    pac,
    prelude::*,
    serial::{Config, Serial},
    pwr::PwrExt,
};

/// Shared resources between tasks
#[app(device = pac, peripherals = true, dispatchers = [EXTI0])]
mod app {
    use super::*;

    #[shared]
    struct Shared {
        rx_data: heapless::Vec<u8, 64>,
    }

    #[local]
    struct Local {
        rx: stm32l4xx_hal::serial::Rx<pac::USART1>,
    }

    #[init]
    fn init(ctx: init::Context) -> (Shared, Local) {
        let dp = ctx.device;

        // Configure clocks
        let mut rcc    = dp.RCC.constrain();
        let mut flash  = dp.FLASH.constrain();
        let mut pwr    = dp.PWR.constrain(&mut rcc.apb1r1);
        let clocks = rcc.cfgr
            .sysclk(4.MHz())   // Low sysclk to reduce active power
            .freeze(&mut flash.acr, &mut pwr);

        // Configure USART1 with interrupt on received byte
        let mut gpioa = dp.GPIOA.split(&mut rcc.ahb2);
        let tx_pin = gpioa.pa9.into_alternate(&mut gpioa.moder,
                                               &mut gpioa.otyper,
                                               &mut gpioa.afrh);
        let rx_pin = gpioa.pa10.into_alternate(&mut gpioa.moder,
                                                &mut gpioa.otyper,
                                                &mut gpioa.afrh);

        let serial = Serial::usart1(
            dp.USART1,
            (tx_pin, rx_pin),
            Config::default().baudrate(9600.bps()),
            clocks,
            &mut rcc.apb2,
        );
        let (_tx, mut rx) = serial.split();

        // Enable RXNE (receive data register not empty) interrupt
        rx.listen();

        (
            Shared { rx_data: heapless::Vec::new() },
            Local { rx },
        )
    }

    /// UART1 interrupt handler — fires when a byte is received.
    /// This interrupt also serves as the wake-up event from sleep.
    #[task(binds = USART1, local = [rx], shared = [rx_data])]
    fn usart1_irq(ctx: usart1_irq::Context) {
        let rx = ctx.local.rx;
        let mut rx_data = ctx.shared.rx_data;

        if let Ok(byte) = rx.read() {
            rx_data.lock(|buf| {
                let _ = buf.push(byte);
                // If we have a complete message (e.g., ends with newline),
                // spawn processing task
                if byte == b'\n' || buf.is_full() {
                    process_message::spawn().ok();
                }
            });
        }
    }

    /// Message processing task, dispatched from UART interrupt context.
    #[task(shared = [rx_data])]
    async fn process_message(mut ctx: process_message::Context) {
        ctx.shared.rx_data.lock(|buf| {
            // Process the accumulated bytes
            defmt::info!("Processing message: {} bytes", buf.len());
            buf.clear();
        });
    }

    /// Idle task: runs when no other task is ready.
    /// This is where we enter low-power sleep.
    /// The USART1 interrupt will wake the CPU when data arrives.
    #[idle]
    fn idle(_ctx: idle::Context) -> ! {
        loop {
            // WFI: CPU stops executing and enters Sleep mode.
            // Next interrupt (including USART1 RXNE) will wake it.
            // This is the simplest low-power strategy: Sleep mode,
            // where peripherals continue running normally.
            cortex_m::asm::wfi();
        }
    }
}
```

---

### Cross-Platform Trait Abstraction

```rust
//! Power-aware UART abstraction in Rust
//!
//! A trait-based design that allows platform-specific implementations
//! of low-power UART behavior behind a common interface.

use core::future::Future;

/// Error type for power-aware UART operations
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartPowerError {
    /// Hardware does not support the requested power mode
    UnsupportedMode,
    /// Baud rate not achievable with low-power clock source
    BaudRateNotAchievable,
    /// Wake-up source configuration failed
    WakeupConfigFailed,
    /// I/O error during receive
    ReceiveError,
}

/// Supported power modes for UART operation
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum UartPowerMode {
    /// Full-speed operation (normal mode)
    FullPower,
    /// Clock-gated when idle: UART stops when TX/RX complete
    ClockGated,
    /// Low-power clock source (e.g., LSI/LSE): limited baud rate
    LowPowerClock,
    /// Stop mode with wake-on-receive: CPU can sleep deeply
    StopModeWakeOnReceive,
}

/// Wake-up trigger configuration
#[derive(Debug, Clone, Copy)]
pub enum WakeupTrigger {
    /// Wake on any received character (start-bit detection)
    AnyCharacter,
    /// Wake only when received address byte matches
    AddressMatch(u8),
    /// Wake when FIFO has at least N bytes
    FifoThreshold(u8),
}

/// Trait for power-aware UART receivers
pub trait PowerAwareUart {
    type Error;

    /// Configure the power mode for subsequent operations.
    fn set_power_mode(
        &mut self,
        mode: UartPowerMode,
    ) -> Result<(), Self::Error>;

    /// Configure the wake-up trigger (for StopModeWakeOnReceive).
    fn set_wakeup_trigger(
        &mut self,
        trigger: WakeupTrigger,
    ) -> Result<(), Self::Error>;

    /// Receive bytes asynchronously.
    /// In low-power modes, the implementation should allow the CPU
    /// to sleep while waiting for data.
    fn receive<'a>(
        &'a mut self,
        buf: &'a mut [u8],
    ) -> impl Future<Output = Result<usize, Self::Error>> + 'a;

    /// Enter low-power standby.
    /// The implementation configures the hardware for the current
    /// power mode and suspends until data arrives.
    fn enter_low_power_standby(&mut self) -> Result<(), Self::Error>;

    /// Query current power consumption estimate in microamps.
    fn estimated_current_ua(&self) -> u32;
}

/// Power consumption profiles for logging/optimization
pub struct PowerProfile {
    pub mode: UartPowerMode,
    pub baud_rate: u32,
    pub estimated_ua: u32,
    pub wakeup_latency_us: u32,
}

impl PowerProfile {
    /// Typical profiles for STM32L4 LPUART1
    pub const STM32L4_LPUART_FULL_POWER: Self = Self {
        mode: UartPowerMode::FullPower,
        baud_rate: 115_200,
        estimated_ua: 1_500,   // ~1.5 mA active UART + CPU
        wakeup_latency_us: 0,
    };

    pub const STM32L4_LPUART_STOP_MODE: Self = Self {
        mode: UartPowerMode::StopModeWakeOnReceive,
        baud_rate: 2_400,
        estimated_ua: 3,       // ~3 µA in Stop 1 mode
        wakeup_latency_us: 100, // ~100 µs to restore clocks and resume
    };

    pub const ESP32_LIGHT_SLEEP: Self = Self {
        mode: UartPowerMode::StopModeWakeOnReceive,
        baud_rate: 115_200,
        estimated_ua: 800,     // ~0.8 mA in light sleep (higher than STM32)
        wakeup_latency_us: 300, // ~300 µs light sleep resume
    };
}

/// A simple state machine for power-managed UART communication
pub enum UartPowerState {
    Active,
    LowPowerListening,
    Processing,
    Sleeping,
}

pub struct PowerManagedUart<T: PowerAwareUart> {
    uart: T,
    state: UartPowerState,
    rx_buffer: [u8; 256],
    rx_count: usize,
}

impl<T: PowerAwareUart<Error = UartPowerError>> PowerManagedUart<T> {
    pub fn new(mut uart: T) -> Result<Self, UartPowerError> {
        // Configure for low-power wake-on-receive by default
        uart.set_power_mode(UartPowerMode::StopModeWakeOnReceive)?;
        uart.set_wakeup_trigger(WakeupTrigger::AnyCharacter)?;
        
        Ok(Self {
            uart,
            state: UartPowerState::LowPowerListening,
            rx_buffer: [0u8; 256],
            rx_count: 0,
        })
    }

    /// Main event loop: sleep → wake → receive → process → sleep
    pub async fn run_event_loop(&mut self) -> Result<(), UartPowerError> {
        loop {
            self.state = UartPowerState::LowPowerListening;
            self.uart.enter_low_power_standby()?;

            // CPU wakes here on UART activity
            self.state = UartPowerState::Active;

            // Read available data
            self.rx_count = self.uart
                .receive(&mut self.rx_buffer)
                .await?;

            if self.rx_count > 0 {
                self.state = UartPowerState::Processing;
                self.process_received_data();
            }
        }
    }

    fn process_received_data(&self) {
        let data = &self.rx_buffer[..self.rx_count];
        // Application-specific processing
        let _ = data;
    }
}
```

---

## Power Consumption Comparison

The following figures are representative of a typical STM32L4-class microcontroller with LPUART enabled. Actual values vary by silicon revision, voltage, and temperature.

| Mode | CPU State | UART State | Typical Current | Wake Latency |
|---|---|---|---|---|
| Active (115200 baud) | Running | Full speed | 5–20 mA | 0 µs |
| Sleep (115200 baud) | WFI halted | Full speed | 1–5 mA | <10 µs |
| LP Run (low clock) | Running @ 100 kHz | LP mode | 100–300 µA | 0 µs |
| Stop 0 | Deep halt | LPUART @ LSE | 10–50 µA | 50–300 µs |
| Stop 1 | Deep halt | LPUART @ LSE | 3–10 µA | 100–500 µs |
| Stop 2 | Deeper halt | None | 1–5 µA | 200 µs–1 ms |
| Standby | Ultra-deep | None | 0.3–1 µA | >1 ms |

> **Key insight**: Stop 1 with LPUART wake-on-receive achieves roughly 1000× reduction in current compared to full-power active mode, while remaining responsive to UART input within ~500 µs — imperceptible at human timescales.

---

## Common Pitfalls and Best Practices

### Pitfall 1: Clock not available in Stop Mode

**Problem**: Configuring UART with HSI or PLL as the clock source, then entering Stop mode. The UART clock disappears and reception stops entirely — or worse, the UART freezes mid-byte and corrupts data.

**Solution**: Always switch the UART clock source to LSI or LSE before entering Stop mode. Verify the target baud rate is achievable at the lower clock frequency.

```c
/* WRONG: USART1 clocked from PCLK (stops in Stop mode) */
periphclk.Usart1ClockSelection = RCC_USART1CLKSOURCE_PCLK2;

/* CORRECT: LPUART1 clocked from LSE (runs in Stop mode) */
periphclk.Lpuart1ClockSelection = RCC_LPUART1CLKSOURCE_LSE;
```

### Pitfall 2: Overrun Before CPU Wakes

**Problem**: A fast sender transmits multiple bytes before the MCU wakes from Stop mode and reads the FIFO. If the FIFO is only 1 byte deep, an overrun error occurs and data is lost.

**Solution**:
- Use a UART with a deep FIFO (8+ bytes).
- Use DMA to drain the FIFO without CPU involvement.
- Ensure the sender includes inter-character delays or waits for an acknowledgement.
- Always check and clear the UART ORE (Overrun Error) flag on wake-up.

### Pitfall 3: RX Pin Not Pulled Up

**Problem**: If the UART RX pin floats while the sender is powered off or disconnected, noise can trigger spurious wake-up events.

**Solution**: Always configure UART RX with an internal pull-up (most UARTs idle the line HIGH). Ensure the pull-up is active even in Stop mode.

```c
gpio.Pull = GPIO_PULLUP; /* Essential for UART idle line stability */
```

### Pitfall 4: Forgetting to Re-Enable Receiver After Wake

**Problem**: Some UART implementations disable the receiver in Stop mode and do not automatically re-enable it after wake-up. Subsequent bytes are missed.

**Solution**: In the wake-up interrupt handler, verify the receiver is enabled (`HAL_UART_Receive_IT(...)` or equivalent) and restart the reception pipeline if needed.

### Pitfall 5: Clock Restoration After Stop Mode

**Problem**: After waking from Stop mode, the system clock reverts to MSI (on STM32). If the application code assumes the PLL is still active, timer and UART baud rates will be wrong.

**Solution**: Always restore clocks in the post-Stop wake-up sequence before performing any time-critical operations.

```c
/* Always call this after returning from HAL_PWREx_EnterSTOP1Mode() */
SystemClock_Restore();
```

### Pitfall 6: Baud Rate Mismatch at Low-Power Clock

**Problem**: Using 9600 baud with a 32.768 kHz LSE clock. The BRR register cannot divide 32768 by an integer to produce exactly 9600 Hz, causing bit-timing errors.

**Solution**: Use baud rates that divide evenly into the low-power clock:
- LSE (32.768 kHz) → 2048 baud (32768/16), typically rounded to 2400 baud (within ±17%, may be unreliable with strict devices)
- LSI (32 kHz typical) → 2000 baud

For higher baud rates in low-power contexts, wake on start bit, then switch to HSI before reading the remainder of the message.

---

## Summary

UART power management is a multi-layered discipline combining hardware peripheral configuration, clock management, and CPU power state control. The key concepts are:

**Low-power UART modes** reduce power by gating the peripheral clock when idle, switching to a low-power clock source (LSI/LSE) that remains active in deep sleep, and operating at reduced baud rates. The tradeoff is lower throughput and increased clock-source error, but power consumption can be reduced from milliamps to low microamps.

**Wake-on-receive (WoR)** enables the CPU to enter deep sleep (Stop mode) while the UART receiver hardware monitors the RX line. A start-bit falling edge, an address match in multiprocessor mode, or a FIFO threshold event generates a wake-up signal that resumes CPU execution within a few hundred microseconds. This pattern is essential for IoT devices that must remain responsive while consuming near-zero standby power.

**In C/C++**, low-power UART is configured through HAL/register-level peripheral setup: selecting LSE as the LPUART clock source, calling `HAL_UARTEx_EnableStopMode()`, and entering Stop mode with `HAL_PWREx_EnterSTOP1Mode()`. On Linux, the equivalent is epoll-based non-blocking I/O combined with sysfs wakeup source configuration.

**In Rust**, the Embassy framework provides ergonomic async/await UART APIs that automatically invoke `WFI` at every await point via the executor's idle loop. For deep Stop mode, Embassy's `low_power::Executor` extends this to full Stop mode entry, with the LPUART interrupt serving as the hardware wake-up source. RTIC provides an alternative with its `#[idle]` task as the natural location for `cortex_m::asm::wfi()`.

The most effective real-world strategy combines several of these techniques: a low-power UART peripheral clocked from LSE, DMA reception to avoid waking the CPU for every byte, address-match filtering to ignore irrelevant bus traffic, and a deep Stop mode with a well-characterized clock restoration sequence on wake-up. Together, these can reduce a device's average current from tens of milliamps to single-digit microamps while maintaining millisecond-class responsiveness to incoming commands.