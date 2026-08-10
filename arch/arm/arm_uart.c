// ============================================================================
// arm_uart.c — PL011 UART Driver (BCM2835, Raspberry Pi 1)
// ============================================================================
// Реализация hal_uart.h для ARM.
//
// PL011 UART0:
//   Physical: 0x20201000
//   Virtual:  BCM2835_VIRT(0x20201000) = 0xE0201000 (Higher Half Peripherals)
//   GPIO: pins 14 (TXD), 15 (RXD) → ALT0
//   Baud: 115200, 8N1, FIFO enabled
//   UARTCLK: 3 MHz (core_freq=250 в config.txt)
//
// ⚠️ Все MMIO через volatile uint32_t* + dsb barrier.
// ⚠️ Day 52 Zero Trust fix:
//    Все MMIO-адреса ОБЯЗАНЫ использовать BCM2835_VIRT() для трансляции
//    физических адресов в Higher-Half виртуальные адреса (0xE0000000+).
//    Это гарантирует, что UART доступен из любого address space,
//    включая user L1 tables (entries 3584-3599 копируются из boot TTBR0).
// ⚠️ hal_timer_delay_us() реализована в arm_timer.c (BCM2835 CLO).
//    Работает ДО hal_timer_init(): hardware counter тикает с power-on.
// ============================================================================

#include <stdint.h>
#include "config.h"
#include "hal/hal_uart.h"
#include "hal/hal_cpu.h"
#include "hal/hal_timer.h"

// ============================================================================
// MMIO ACCESS (volatile + barrier + BCM2835_VIRT translation)
// ============================================================================
// Day 52 Zero Trust fix:
// Физические адреса периферии (0x20000000+) не замаплены в user L1 tables.
// Все обращения ДОЛЖНЫ идти через Higher-Half Direct Map (0xE0000000+),
// который автоматически присутствует во всех address spaces.
// ============================================================================

#define MMIO_READ(addr)     (*(volatile uint32_t *)BCM2835_VIRT(addr))
#define MMIO_WRITE(addr, v) do { \
    (*(volatile uint32_t *)BCM2835_VIRT(addr)) = (v); \
    hal_dsb(); \
} while(0)

// ============================================================================
// GPIO SETUP (ALT0 for UART0)
// ============================================================================
// GPFSEL1: bits [17:15] = GPIO15 function, bits [14:12] = GPIO14 function
// ALT0 = 0b100
// ============================================================================

static void gpio_set_alt0(void)
{
    uint32_t reg;

    // GPFSEL1: set GPIO14 and GPIO15 to ALT0 (100)
    reg = MMIO_READ(BCM2835_GPIO_GPFSEL1);
    reg &= ~(7 << 12);          // Clear GPIO14 function bits
    reg &= ~(7 << 15);          // Clear GPIO15 function bits
    reg |=  (4 << 12);          // GPIO14 = ALT0 (100)
    reg |=  (4 << 15);          // GPIO15 = ALT0 (100)
    MMIO_WRITE(BCM2835_GPIO_GPFSEL1, reg);

    // Disable pull-up/pull-down on GPIO14/15
    MMIO_WRITE(BCM2835_GPIO_GPPUD, 0);
    hal_timer_delay_us(1);      // Wait 150 cycles (~1us at 250MHz)
    MMIO_WRITE(BCM2835_GPIO_GPPUDCLK0, (1 << 14) | (1 << 15));
    hal_timer_delay_us(1);
    MMIO_WRITE(BCM2835_GPIO_GPPUDCLK0, 0);
}

// ============================================================================
// HAL UART IMPLEMENTATION
// ============================================================================

void hal_uart_init(uint32_t baud)
{
    // 1. Disable UART
    MMIO_WRITE(PL011_CR, 0);

    // 2. Wait for UART to finish any current transmission
    while (MMIO_READ(PL011_FR) & PL011_FR_BUSY)
        ;

    // 3. Setup GPIO (ALT0 for pins 14/15)
    gpio_set_alt0();

    // 4. Set baud rate
    // UARTCLK = 3 MHz (core_freq=250 в config.txt)
    // BRD = 3000000 / (16 * 115200) = 1.627
    // IBRD = 1, FBRD = round(0.627 * 64) = 40
    //
    // ⚠️ Hardcoded: ARM1176 не имеет hardware divide.
    //    Для других baud — добавить lookup table или libgcc.
    (void)baud;
    MMIO_WRITE(PL011_IBRD, 1);
    MMIO_WRITE(PL011_FBRD, 40);

    // 5. Line control: 8 bits, no parity, 1 stop, FIFO enable
    MMIO_WRITE(PL011_LCRH, PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);

    // 6. Clear all interrupts
    MMIO_WRITE(PL011_ICR, 0x7FF);

    // 7. Mask all interrupts (polling mode, no IRQ)
    MMIO_WRITE(PL011_IMSC, 0);

    // 8. Enable UART + TX + RX
    MMIO_WRITE(PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
}

void hal_uart_putc(char c)
{
    // Wait until TX FIFO is not full
    while (MMIO_READ(PL011_FR) & PL011_FR_TXFF)
        ;

    MMIO_WRITE(PL011_DR, (uint32_t)c);

    // Auto \r\n (serial console)
    if (c == '\n') {
        while (MMIO_READ(PL011_FR) & PL011_FR_TXFF)
            ;
        MMIO_WRITE(PL011_DR, '\r');
    }
}

void hal_uart_puts(const char* s)
{
    while (*s) {
        hal_uart_putc(*s++);
    }
}

void hal_uart_write(const char* buf, uint32_t len)
{
    for (uint32_t i = 0; i < len; i++) {
        hal_uart_putc(buf[i]);
    }
}

int hal_uart_getc(void)
{
    // Blocking: wait until RX FIFO is not empty
    while (MMIO_READ(PL011_FR) & PL011_FR_RXFE)
        ;

    uint32_t data = MMIO_READ(PL011_DR);

    // Check for errors (FE, PE, BE, OE in bits [11:8])
    if (data & 0xF00)
        return -1;

    return (int)(data & 0xFF);
}

int hal_uart_getc_nonblock(void)
{
    if (MMIO_READ(PL011_FR) & PL011_FR_RXFE)
        return -1;

    uint32_t data = MMIO_READ(PL011_DR);
    if (data & 0xF00)
        return -1;

    return (int)(data & 0xFF);
}

int hal_uart_rx_ready(void)
{
    return !(MMIO_READ(PL011_FR) & PL011_FR_RXFE);
}

// ============================================================================
// hal_timer_delay_us / hal_timer_delay_ms:
// Реализованы в arm_timer.c (BCM2835 System Timer CLO, 1 MHz).
// Hardware counter работает с power-on, ДО hal_timer_init().
// Calibrated loop (Day 37 stub) удалён — hardware timer точнее.
// ============================================================================