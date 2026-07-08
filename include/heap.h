#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>
#include "klib.h"

// Инициализация кучи: маппинг физических страниц в виртуальное пространство 0xD0000000
void heap_init(void);

// Выделение памяти (аналог malloc)
void* kmalloc(size_t size);

// Освобождение памяти (аналог free)
void kfree(void* ptr);

// Отладочные функции для Shell
void heap_print_status(void);
void heap_run_tests(void);

#endif // HEAP_H
