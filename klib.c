#include "klib.h"
#include "vga.h"
#include "framebuffer.h"
#include <stdarg.h>

// ==========================================
// ДИСПЕТЧЕР ВЫВОДА (Strategy Pattern)
// ==========================================

// Внутренний диспетчер: выбирает между framebuffer и VGA
static void output_char(char c) {
    if (fb_is_available()) {
        fb_putc(c);
    } else {
        vga_putc(c);
    }
}

// Установка цвета одновременно для framebuffer и VGA
// VGA-цвета (0-15) мапим на близкие RGB-значения
void k_set_color(uint8_t vga_fg, uint8_t vga_bg) {
    // Устанавливаем VGA (на случай fallback)
    vga_set_color(vga_fg, vga_bg);
    
    // Параллельно синхронизируем framebuffer-цвета
    if (fb_is_available()) {
        // Маппинг VGA -> RGB (упрощенная палитра)
        static const uint32_t vga_to_rgb[16] = {
            0x00000000, // BLACK
            0x000000AA, // BLUE
            0x0000AA00, // GREEN
            0x0000AAAA, // CYAN
            0x00AA0000, // RED
            0x00AA00AA, // MAGENTA
            0x00AA5500, // BROWN
            0x00AAAAAA, // LIGHT_GREY
            0x00555555, // DARK_GREY
            0x005555FF, // LIGHT_BLUE
            0x0055FF55, // LIGHT_GREEN
            0x0055FFFF, // LIGHT_CYAN
            0x00FF5555, // LIGHT_RED
            0x00FF55FF, // LIGHT_MAGENTA
            0x00FFFF55, // YELLOW
            0x00FFFFFF  // WHITE
        };
        fb_set_color(vga_to_rgb[vga_fg & 0x0F], vga_to_rgb[vga_bg & 0x0F]);
    }
}

// ==========================================
// РАБОТА С ПАМЯТЬЮ
// ==========================================

void* k_memset(void* ptr, int value, size_t num) {
    uint8_t* p = (uint8_t*)ptr;
    while (num--) {
        *p++ = (uint8_t)value;
    }
    return ptr;
}

void* k_memcpy(void* dest, const void* src, size_t num) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    while (num--) {
        *d++ = *s++;
    }
    return dest;
}

int k_memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++; p2++;
    }
    return 0;
}
// ==========================================
// VSPRINTF (Для fb_printf и будущих нужд)
// ==========================================

int k_vsprintf(char* buf, const char* fmt, va_list args) {
    char* start = buf;
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = va_arg(args, int);
                    char tmp[16];
                    k_itoa(val, tmp, 10);
                    char* p = tmp;
                    while (*p) *buf++ = *p++;
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    char tmp[16];
                    k_uitoa(val, tmp, 10);
                    char* p = tmp;
                    while (*p) *buf++ = *p++;
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    *buf++ = '0';
                    *buf++ = 'x';
                    char tmp[16];
                    k_uitoa(val, tmp, 16);
                    char* p = tmp;
                    while (*p) *buf++ = *p++;
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    *buf++ = '0';
                    *buf++ = 'x';
                    char tmp[16];
                    k_uitoa((unsigned int)(uintptr_t)ptr, tmp, 16);
                    char* p = tmp;
                    while (*p) *buf++ = *p++;
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    if (!str) str = "(null)";
                    while (*str) *buf++ = *str++;
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    *buf++ = c;
                    break;
                }
                case '%': *buf++ = '%'; break;
                case '\0': goto end;
                default: *buf++ = '%'; *buf++ = *fmt; break;
            }
        } else {
            *buf++ = *fmt;
        }
        fmt++;
    }
end:
    *buf = '\0';
    return buf - start;
}
// ==========================================
// СТРОКИ И ВЫВОД
// ==========================================

size_t k_strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

void k_print(const char* str) {
    if (!str) return;
    while (*str) output_char(*str++);
}

int k_strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

int k_strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) { s1++; s2++; n--; }
    if (n == 0) return 0;
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

void k_printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 'd': {
                    int val = va_arg(args, int);
                    char buf[16];
                    k_itoa(val, buf, 10);
                    k_print(buf);
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    char buf[16];
                    k_uitoa(val, buf, 10);
                    k_print(buf);
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    k_print("0x");
                    char buf[16];
                    k_uitoa(val, buf, 16);
                    k_print(buf);
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    k_print("0x");
                    char buf[16];
                    k_uitoa((unsigned int)(uintptr_t)ptr, buf, 16);
                    k_print(buf);
                    break;
                }
                case 's': {
                    const char* str = va_arg(args, const char*);
                    k_print(str ? str : "(null)");
                    break;
                }
                case 'c': {
                    char c = (char)va_arg(args, int);
                    output_char(c);
                    break;
                }
                case '%': output_char('%'); break;
                case '\0': goto end;
                default: output_char('%'); output_char(*fmt); break;
            }
        } else {
            output_char(*fmt);
        }
        fmt++;
    }
end:
    va_end(args);
}

// ==========================================
// УНИВЕРСАЛЬНЫЕ ФУНКЦИИ ДЛЯ SHELL
// ==========================================

// Универсальный вывод одного символа (для shell input echo)
void k_putchar(char c) {
    output_char(c);
}

// Универсальная очистка экрана
void k_clear(void) {
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK);
    } else {
        clear();  // существующая VGA функция
    }
}

// ==========================================
// КОНВЕРТАЦИЯ ЧИСЕЛ
// ==========================================

int k_atoi(const char* str) {
    if (!str) return 0;
    int result = 0, sign = 1;
    while (*str == ' ') str++;
    if (*str == '-') { sign = -1; str++; } 
    else if (*str == '+') str++;
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    return result * sign;
}

uint32_t k_atoh(const char* str) {
    if (!str) return 0;
    uint32_t result = 0;
    if (str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) str += 2;
    while (*str) {
        char c = *str++;
        result <<= 4;
        if (c >= '0' && c <= '9') result |= (c - '0');
        else if (c >= 'a' && c <= 'f') result |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') result |= (c - 'A' + 10);
        else break;
    }
    return result;
}

void k_itoa(int value, char* buf, int base) {
    char tmp[33];
    int i = 0, negative = 0;
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    
    unsigned int uvalue;
    if (value < 0 && base == 10) {
        negative = 1;
        uvalue = -(unsigned int)value; 
    } else {
        uvalue = (unsigned int)value;
    }
    
    while (uvalue > 0) {
        int rem = uvalue % base;
        tmp[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        uvalue /= base;
    }
    
    int j = 0;
    if (negative) buf[j++] = '-';
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}

void k_uitoa(unsigned int value, char* buf, int base) {
    char tmp[33];
    int i = 0;
    if (value == 0) { buf[0] = '0'; buf[1] = '\0'; return; }
    while (value > 0) {
        int rem = value % base;
        tmp[i++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
        value /= base;
    }
    int j = 0;
    while (i > 0) buf[j++] = tmp[--i];
    buf[j] = '\0';
}