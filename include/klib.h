#ifndef KLIB_H
#define KLIB_H

#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// ==========================================
// 🎨 ЦВЕТОВАЯ ПАЛИТРА ЯДРА (Абстракция)
// ==========================================
enum k_color {
    K_COLOR_BLACK = 0,
    K_COLOR_BLUE = 1,
    K_COLOR_GREEN = 2,
    K_COLOR_CYAN = 3,
    K_COLOR_RED = 4,
    K_COLOR_MAGENTA = 5,
    K_COLOR_BROWN = 6,
    K_COLOR_LIGHT_GREY = 7,
    K_COLOR_DARK_GREY = 8,
    K_COLOR_LIGHT_BLUE = 9,
    K_COLOR_LIGHT_GREEN = 10,
    K_COLOR_LIGHT_CYAN = 11,
    K_COLOR_LIGHT_RED = 12,
    K_COLOR_LIGHT_MAGENTA = 13,
    K_COLOR_YELLOW = 14,
    K_COLOR_WHITE = 15,
};

// 🛡️ Legacy aliases для обратной совместимости (чтобы не переписывать shell.c)
#define VGA_COLOR_BLACK         K_COLOR_BLACK
#define VGA_COLOR_BLUE          K_COLOR_BLUE
#define VGA_COLOR_GREEN         K_COLOR_GREEN
#define VGA_COLOR_CYAN          K_COLOR_CYAN
#define VGA_COLOR_RED           K_COLOR_RED
#define VGA_COLOR_MAGENTA       K_COLOR_MAGENTA
#define VGA_COLOR_BROWN         K_COLOR_BROWN
#define VGA_COLOR_LIGHT_GREY    K_COLOR_LIGHT_GREY
#define VGA_COLOR_DARK_GREY     K_COLOR_DARK_GREY
#define VGA_COLOR_LIGHT_BLUE    K_COLOR_LIGHT_BLUE
#define VGA_COLOR_LIGHT_GREEN   K_COLOR_LIGHT_GREEN
#define VGA_COLOR_LIGHT_CYAN    K_COLOR_LIGHT_CYAN
#define VGA_COLOR_LIGHT_RED     K_COLOR_LIGHT_RED
#define VGA_COLOR_LIGHT_MAGENTA K_COLOR_LIGHT_MAGENTA
#define VGA_COLOR_YELLOW        K_COLOR_YELLOW
#define VGA_COLOR_WHITE         K_COLOR_WHITE


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

// --- Конвертация чисел ---
void k_itoa(int value, char* buf, int base);
void k_uitoa(unsigned int value, char* buf, int base);
int k_atoi(const char* str);
uint32_t k_atoh(const char* str);

// НОВОЕ: универсальная установка цвета
void k_set_color(uint8_t fg, uint8_t bg); // Убрали vga_ из названий параметров!

#endif // KLIB_H