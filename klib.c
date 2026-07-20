//klib.c

#include "klib.h"
#include "vga.h"
#include "framebuffer.h"
#include <stdarg.h>
#include <stdbool.h>

// ==========================================
// ДИСПЕТЧЕР ВЫВОДА (Strategy Pattern)
// ==========================================

static void output_char(char c) {
    if (fb_is_available()) {
        fb_putc(c);
    } else {
        vga_putc(c);
    }
}

// ==========================================
// ЦВЕТА
// ==========================================

void k_set_color(uint8_t vga_fg, uint8_t vga_bg) {
    vga_set_color(vga_fg, vga_bg);
    
    if (fb_is_available()) {
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
// СТРОКИ
// ==========================================

size_t k_strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
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

char* k_strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

// ============================================================================
// PRINTF FAMILY — C99 COMPLIANT (Ring 0 Kernel Space)
// ============================================================================

static void k_put_int(char** buf, char* end, int value, int base, int width, int precision, int pad_zero, int is_signed, int left_align, int show_sign, int space_flag) {
    char tmp[33];
    int len = 0;
    int negative = 0;
    
    if (is_signed && value < 0) {
        negative = 1;
        value = -value;
    }
    
    unsigned int uval = (unsigned int)value;
    if (uval == 0) {
        tmp[len++] = '0';
    } else {
        while (uval > 0) {
            int rem = uval % base;
            tmp[len++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
            uval /= base;
        }
    }
    
    int num_digits = len;
    if (precision > num_digits) num_digits = precision;
    
    int sign_len = 0;
    if (negative) sign_len = 1;
    else if (show_sign) sign_len = 1;
    else if (space_flag) sign_len = 1;
    
    int total_len = num_digits + sign_len;
    int pad = width - total_len;
    char pad_char = (pad_zero && precision < 0) ? '0' : ' ';
    
    if (!left_align) {
        while (pad-- > 0 && *buf < end) *(*buf)++ = pad_char;
    }
    
    if (negative && *buf < end) *(*buf)++ = '-';
    else if (show_sign && !negative && *buf < end) *(*buf)++ = '+';
    else if (space_flag && !negative && *buf < end) *(*buf)++ = ' ';
    
    int leading_zeros = num_digits - len;
    while (leading_zeros-- > 0 && *buf < end) *(*buf)++ = '0';
    
    while (len > 0 && *buf < end) *(*buf)++ = tmp[--len];
    
    if (left_align) {
        while (pad-- > 0 && *buf < end) *(*buf)++ = ' ';
    }
}

static void k_put_uint(char** buf, char* end, unsigned int value, int base, int width, int precision, int pad_zero, int left_align, int alt_form) {
    char tmp[33];
    int len = 0;
    
    if (value == 0) {
        tmp[len++] = '0';
    } else {
        while (value > 0) {
            int rem = value % base;
            tmp[len++] = (rem < 10) ? (rem + '0') : (rem - 10 + 'a');
            value /= base;
        }
    }
    
    int num_digits = len;
    if (precision > num_digits) num_digits = precision;
    
    int prefix_len = 0;
    if (alt_form && base == 16 && value != 0) prefix_len = 2;
    else if (alt_form && base == 8 && value != 0) prefix_len = 1;
    
    int total_len = num_digits + prefix_len;
    int pad = width - total_len;
    char pad_char = (pad_zero && precision < 0) ? '0' : ' ';
    
    if (!left_align) {
        while (pad-- > 0 && *buf < end) *(*buf)++ = pad_char;
    }
    
    if (alt_form && base == 16 && value != 0) {
        if (*buf < end) *(*buf)++ = '0';
        if (*buf < end) *(*buf)++ = 'x';
    } else if (alt_form && base == 8 && value != 0) {
        if (*buf < end) *(*buf)++ = '0';
    }
    
    int leading_zeros = num_digits - len;
    while (leading_zeros-- > 0 && *buf < end) *(*buf)++ = '0';
    
    while (len > 0 && *buf < end) *(*buf)++ = tmp[--len];
    
    if (left_align) {
        while (pad-- > 0 && *buf < end) *(*buf)++ = ' ';
    }
}

// ----------------------------------------------------------------------------
// k_vsprintf — C99 compliant kernel-space formatter
// ----------------------------------------------------------------------------
int k_vsprintf(char* buf, const char* fmt, va_list args) {
    char* start = buf;
    char* end = buf + 1023;
    
    while (*fmt && buf < end) {
        if (*fmt != '%') {
            *buf++ = *fmt++;
            continue;
        }
        fmt++;
        
        int left_align = 0, show_sign = 0, space_flag = 0, pad_zero = 0, alt_form = 0;
        while (1) {
            if (*fmt == '-') { left_align = 1; fmt++; }
            else if (*fmt == '+') { show_sign = 1; fmt++; }
            else if (*fmt == ' ') { space_flag = 1; fmt++; }
            else if (*fmt == '0') { pad_zero = 1; fmt++; }
            else if (*fmt == '#') { alt_form = 1; fmt++; }
            else break;
        }
        
        int width = 0;
        if (*fmt == '*') {
            width = va_arg(args, int);
            if (width < 0) { left_align = 1; width = -width; }
            fmt++;
        } else {
            while (*fmt >= '0' && *fmt <= '9') {
                width = width * 10 + (*fmt - '0');
                fmt++;
            }
        }
        
        int precision = -1;
        if (*fmt == '.') {
            fmt++;
            precision = 0;
            if (*fmt == '*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                while (*fmt >= '0' && *fmt <= '9') {
                    precision = precision * 10 + (*fmt - '0');
                    fmt++;
                }
            }
        }
        
        int length = 0;
        if (*fmt == 'h') {
            fmt++;
            if (*fmt == 'h') { length = 2; fmt++; }
            else length = 1;
        } else if (*fmt == 'l') {
            fmt++;
            if (*fmt == 'l') { length = 4; fmt++; }
            else length = 3;
        } else if (*fmt == 'z') {
            length = 5; fmt++;
        }
        (void)length;
        
        switch (*fmt) {
            case 'd':
            case 'i': {
                int val = va_arg(args, int);
                k_put_int(&buf, end, val, 10, width, precision, pad_zero, 1, left_align, show_sign, space_flag);
                break;
            }
            case 'u': {
                unsigned int val = va_arg(args, unsigned int);
                k_put_uint(&buf, end, val, 10, width, precision, pad_zero, left_align, alt_form);
                break;
            }
            case 'x':
            case 'X': {
                unsigned int val = va_arg(args, unsigned int);
                k_put_uint(&buf, end, val, 16, width, precision, pad_zero, left_align, alt_form);
                break;
            }
            case 'o': {
                unsigned int val = va_arg(args, unsigned int);
                k_put_uint(&buf, end, val, 8, width, precision, pad_zero, left_align, alt_form);
                break;
            }
            case 'p': {
                void* p = va_arg(args, void*);
                if (buf < end) *buf++ = '0';
                if (buf < end) *buf++ = 'x';
                k_put_uint(&buf, end, (unsigned int)(uintptr_t)p, 16, 8, -1, 1, 0, 0);
                break;
            }
            case 's': {
                const char* str = va_arg(args, const char*);
                if (!str) str = "(null)";
                int slen = k_strlen(str);
                
                if (precision >= 0 && precision < slen) {
                    slen = precision;
                }
                
                int pad = width - slen;
                if (!left_align) {
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                }
                for (int i = 0; i < slen && buf < end; i++) {
                    *buf++ = str[i];
                }
                if (left_align) {
                    while (pad-- > 0 && buf < end) *buf++ = ' ';
                }
                break;
            }
            case 'c':
                if (buf < end) *buf++ = (char)va_arg(args, int);
                break;
            case '%':
                if (buf < end) *buf++ = '%';
                break;
            default:
                if (buf < end) *buf++ = '%';
                if (buf < end) *buf++ = *fmt;
                break;
        }
        fmt++;
    }
    
    *buf = '\0';
    return buf - start;
}

// ==========================================
// ВЫВОД
// ==========================================

void k_print(const char* str) {
    if (!str) return;
    while (*str) output_char(*str++);
}

void k_putchar(char c) {
    output_char(c);
}

void k_clear(void) {
    if (fb_is_available()) {
        fb_clear(COLOR_BLACK);
    } else {
        clear();
    }
}

void k_printf(const char* fmt, ...) {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    
    k_vsprintf(buf, fmt, args);
    
    va_end(args);
    k_print(buf);
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

// ============================================================================
// [DAY 29] CURSOR POSITIONING — Strategy Pattern (VGA vs Framebuffer)
// ============================================================================
void k_set_cursor(int row, int col) {
    if (fb_is_available()) {
        fb_set_cursor((uint32_t)col, (uint32_t)row);
    } else {
        vga_set_cursor(row, col);
    }
}
