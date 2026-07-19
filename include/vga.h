#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include "klib.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 50

void vga_init(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putc(char c); // Низкоуровневый вывод одного символа
void clear(void);
// [DAY 29] Cursor Positioning (0-based: row, col)
void vga_set_cursor(int row, int col);

#endif