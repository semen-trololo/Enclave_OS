#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>
#include "klib.h"
#include "config.h" // ✅ SSOT: Подключаем глобальные константы памяти

// ============================================================================
// API KERNEL HEAP
// ============================================================================

// Инициализация кучи: маппинг физических страниц в виртуальное пространство
void heap_init(void);

// Выделение памяти (аналог malloc)
void* kmalloc(size_t size);

// Освобождение памяти (аналог free)
void kfree(void* ptr);

// ============================================================================
// [ДЕНЬ 10] HEAP ACCOUNTING (Для Test Runner)
// ============================================================================
uint32_t heap_get_alloc_count(void);
uint32_t heap_get_free_count(void);
int32_t  heap_check_balance(void); // Возвращает (allocs - frees). Должно быть 0 после тестов.

// ============================================================================
// ДИАГНОСТИКА
// ============================================================================
void heap_print_status(void);
void heap_run_tests(void);
void heap_print_fragmentation(void);

#endif // HEAP_H
