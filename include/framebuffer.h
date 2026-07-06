#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t address;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
} framebuffer_info_t;

// Цвета (0x00RRGGBB)
#define COLOR_BLACK     0x00000000
#define COLOR_WHITE     0x00FFFFFF
#define COLOR_RED       0x00FF0000
#define COLOR_GREEN     0x0000FF00
#define COLOR_BLUE      0x000000FF
#define COLOR_YELLOW    0x00FFFF00
#define COLOR_CYAN      0x0000FFFF
#define COLOR_MAGENTA   0x00FF00FF
#define COLOR_ORANGE    0x00FF8000
#define COLOR_GREY      0x00808080
#define COLOR_DARKGREY  0x00404040
#define COLOR_LIGHT_GREY 0x00C0C0C0

// Инициализация
void fb_init(framebuffer_info_t* info);
int  fb_is_available(void);

// Двойная буферизация
void fb_enable_double_buffering(void);
void fb_flush(void);

// Шрифты
void fb_init_font(const void* font_data, uint32_t font_size);
uint32_t fb_get_font_width(void);
uint32_t fb_get_font_height(void);

// Примитивы рисования
void fb_put_pixel(uint32_t x, uint32_t y, uint32_t color);
void fb_clear(uint32_t color);
void fb_fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);
void fb_draw_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint32_t color);

// Потоковый вывод
void fb_set_color(uint32_t fg, uint32_t bg);
void fb_putc(char c);               
void fb_print(const char* str);     
void fb_set_cursor(uint32_t x, uint32_t y);
void fb_scroll_up(void);

// Позиционный вывод
void fb_put_char(char c, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void fb_put_unicode_char(uint32_t unicode, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void fb_draw_string(const char* str, uint32_t x, uint32_t y, uint32_t fg, uint32_t bg);
void fb_printf(uint32_t x, uint32_t y, uint32_t fg, uint32_t bg, const char* fmt, ...);

// Информация
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);
uint32_t fb_get_pitch(void);
uint32_t fb_get_cols(void);   
uint32_t fb_get_rows(void);   

#endif