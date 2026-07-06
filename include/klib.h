#ifndef KLIB_H
#define KLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// --- Строковые функции ---
int k_vsprintf(char* buf, const char* fmt, va_list args);
size_t k_strlen(const char* str);
int k_strcmp(const char* s1, const char* s2);
int k_strncmp(const char* s1, const char* s2, size_t n);

// --- Память (КРИТИЧЕСКИ ВАЖНО ДЛЯ VMM И HEAP) ---
void* k_memset(void* ptr, int value, size_t num);
void* k_memcpy(void* dest, const void* src, size_t num);
int k_memcmp(const void* s1, const void* s2, size_t n);

// --- Вывод и парсинг ---
// НОВОЕ: универсальные функции для shell
void k_putchar(char c);  // Диспетчер: framebuffer или VGA
void k_clear(void);      // Очищает и framebuffer и VGA
void k_print(const char* str);
void k_printf(const char* fmt, ...);
// НОВОЕ: универсальная установка цвета (работает и для VGA, и для framebuffer)
void k_set_color(uint8_t vga_fg, uint8_t vga_bg);

// --- Конвертация чисел ---
void k_itoa(int value, char* buf, int base);
void k_uitoa(unsigned int value, char* buf, int base);
int k_atoi(const char* str);
uint32_t k_atoh(const char* str);

#endif // KLIB_H