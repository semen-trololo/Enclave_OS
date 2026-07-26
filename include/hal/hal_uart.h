#ifndef HAL_UART_H
#define HAL_UART_H

// ============================================================================
// HAL UART — Hardware Abstraction Layer: Serial Console
// ============================================================================
// x86: COM1 (16550A, port 0x3F8, 115200 8N1)
// ARM: PL011 UART0 (MMIO 0x20201000, 115200 8N1, GPIO 14/15 ALT0)
//
// Используется для:
//   - Headless debug (Double Dump Pattern)
//   - Kernel log (k_printf → serial)
//   - RPi: ЕДИНСТВЕННАЯ консоль (нет VGA/FB на early boot)
//
// Portable kernel code включает ТОЛЬКО этот header.
// ============================================================================

#include <stdint.h>
#include "config.h"

// ============================================================================
// INITIALIZATION
// ============================================================================

// Инициализация UART.
// baud: скорость (обычно 115200)
// x86: COM1 port 0x3F8, divisor latch, 8N1, FIFO enable
// ARM: PL011: GPIO ALT0 (pins 14/15), baud rate (UARTCLK 3 MHz или 48 MHz),
//      8N1, FIFO enable, UART enable
//
// ⚠️ ARM: UARTCLK зависит от config.txt (core_freq).
//    Default RPi1: UARTCLK = 3 MHz (core_freq = 250 MHz).
//    Baud divisor = UARTCLK / (16 * baud).
//    Для 115200: IBRD = 1, FBRD = 40 (при 3 MHz)
//              или IBRD = 26, FBRD = 3 (при 48 MHz)
//    Решение: фиксируем core_freq=250 в config.txt → UARTCLK = 3 MHz.
void hal_uart_init(uint32_t baud);

// ============================================================================
// OUTPUT
// ============================================================================

// Отправить один символ (blocking).
// Ждёт пока TX FIFO не освободится.
// x86: poll LSR bit 5 (THR Empty), outb(0x3F8, c)
// ARM: poll PL011_FR.TXFF, write PL011_DR
void hal_uart_putc(char c);

// Отправить строку (null-terminated).
void hal_uart_puts(const char* s);

// Отправить буфер заданной длины.
void hal_uart_write(const char* buf, uint32_t len);

// ============================================================================
// INPUT
// ============================================================================

// Прочитать один символ (blocking).
// Ждёт пока RX FIFO не заполнится.
// x86: poll LSR bit 0 (Data Ready), inb(0x3F8)
// ARM: poll PL011_FR.RXFE, read PL011_DR
// Возвращает символ или -1 при ошибке.
int hal_uart_getc(void);

// Прочитать один символ (non-blocking).
// Возвращает символ или -1 если RX FIFO пуст.
int hal_uart_getc_nonblock(void);

// Проверить, есть ли данные в RX FIFO.
// Возвращает 1 если есть, 0 если пусто.
int hal_uart_rx_ready(void);

// ============================================================================
// FORMATTED OUTPUT ( convenience )
// ============================================================================

// k_printf → serial (Double Dump Pattern).
// Реализация в klib.c, использует hal_uart_putc().
// Здесь только прототип для полноты контракта.
// void k_printf(const char* fmt, ...);  // определён в klib.h

// ============================================================================
// PLATFORM-SPECIFIC NOTES
// ============================================================================
//
// x86 COM1 (16550A):
//   Base port: 0x3F8
//   Registers: THR(0), RBR(0), IER(1), FCR(2), LCR(3), MCR(4), LSR(5), MSR(6)
//   Init: LCR=0x80 (DLAB), DLL/DLM=divisor, LCR=0x03 (8N1), FCR=0xC7 (FIFO)
//
// ARM PL011 (BCM2835):
//   Base: 0x20201000 (physical)
//   GPIO: pins 14 (TXD), 15 (RXD) → ALT0 (GPFSEL1 bits [17:15] = 100, [14:12] = 100)
//   Init sequence:
//     1. Disable UART (CR = 0)
//     2. Wait for BUSY = 0
//     3. Set GPIO ALT0
//     4. Set baud rate (IBRD, FBRD)
//     5. Set line control (LCRH = 8N1 + FIFO)
//     6. Clear interrupts (ICR = 0x7FF)
//     7. Enable UART + TX + RX (CR = UARTEN | TXE | RXE)
//
// ⚠️ RPi config.txt (на SD карте):
//   enable_uart=1
//   core_freq=250        ← фиксирует UARTCLK = 3 MHz
//   (опционально) dtoverlay=miniuart-bt  ← освобождает PL011 от Bluetooth

#endif // HAL_UART_H