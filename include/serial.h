#ifndef SERIAL_H
#define SERIAL_H

void serial_init(void);
void serial_putc(char c);
void serial_print(const char* str);
// Форматированный вывод в Serial-порт (поддерживает %x, %p, %d, %u, %s, %c)
void serial_printf(const char* fmt, ...);

#endif
