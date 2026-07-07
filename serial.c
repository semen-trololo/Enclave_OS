#include "serial.h"
#include <stdint.h>
#include <stdarg.h>


#define PORT 0x3f8   /* COM1 */

static inline void outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    asm volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void serial_init() {
    outb(PORT + 1, 0x00);    // Disable all interrupts
    outb(PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(PORT + 1, 0x00);    //                  (hi byte)
    outb(PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
}

void serial_putc(char c) {
    while ((inb(PORT + 5) & 0x20) == 0);
    outb(PORT, c);
}

void serial_print(const char* str) {
    while (*str) {
        if (*str == '\n') serial_putc('\r');
        serial_putc(*str++);
    }
}


// Вспомогательная функция для вывода HEX (без префикса 0x)
static void serial_print_hex(uint32_t val) {
    for (int i = 28; i >= 0; i -= 4) {
        uint8_t nibble = (val >> i) & 0xF;
        char c = (nibble < 10) ? ('0' + nibble) : ('a' + nibble - 10);
        serial_putc(c);
    }
}

// Вспомогательная функция для вывода DEC
static void serial_print_dec(uint32_t val) {
    if (val == 0) {
        serial_putc('0');
        return;
    }
    char buf[12];
    int i = 0;
    while (val > 0) {
        buf[i++] = '0' + (val % 10);
        val /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}

void serial_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%' && *(fmt + 1)) {
            fmt++; // Пропускаем '%'
            if (*fmt == 'x') {
                serial_print_hex(va_arg(args, uint32_t));
            } else if (*fmt == 'p') {
                serial_print("0x");
                serial_print_hex(va_arg(args, uint32_t));
            } else if (*fmt == 'd' || *fmt == 'u') {
                serial_print_dec(va_arg(args, uint32_t));
            } else if (*fmt == 's') {
                serial_print(va_arg(args, const char*));
            } else if (*fmt == 'c') {
                serial_putc((char)va_arg(args, int));
            } else if (*fmt == '%') {
                serial_putc('%');
            }
        } else {
            // Твой оригинальный逻辑 для \n -> \r\n
            if (*fmt == '\n') serial_putc('\r');
            serial_putc(*fmt);
        }
        fmt++;
    }
    va_end(args);
}
