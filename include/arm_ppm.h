#ifndef ARM_PMM_H
#define ARM_PMM_H

// ============================================================================
// ARM Physical Memory Manager (Day 51B)
// ============================================================================
// Bitmap-based page allocator для ARM1176JZF-S.
//
// Дизайн:
//   - 1 бит = 1 страница (4 KB)
//   - Safe-by-default: bitmap = 0xFF (всё занято при init)
//   - O(1) allocation через software ctz32()
//   - IRQ-safe: hal_irq_save/restore вокруг bitmap ops
//   - ATAGS parsing для RAM discovery
//
// ⚠️ ARM1176 (ARMv6): НЕ использовать __builtin_ctz / __builtin_clz.
//    GCC может сгенерировать rbit (ARMv7-only) → Undefined Instruction.
// ============================================================================

#include <stdint.h>

// ============================================================================
// INITIALIZATION
// ============================================================================

// Инициализация PMM. Вызывается ОДИН раз при boot.
//
// atags_addr: физический адрес ATAGS (r2 при boot)
// fallback_size: размер RAM если ATAGS не найдены (байты)
//
// Последовательность:
//   1. Parse ATAGS → определить RAM size
//   2. Вычислить pmm_max_page = total_ram / 4096
//   3. Заполнить bitmap = 0xFF (safe-by-default)
//   4. Освободить usable RAM (после kernel image)
//
// После init вызывающий код ОБЯЗАН зарезервировать:
//   - Lower 64 KB (ARM exception vectors)
//   - Kernel image (.text, .rodata, .data, .bss)
//   - .boot.bss (TTBR0 + stacks)
//   - User spike regions (если используются)
//   - Peripherals (0x20000000+, не RAM)
void arm_pmm_init(uint32_t atags_addr, uint32_t fallback_size);

// ============================================================================
// ALLOCATION / DEALLOCATION
// ============================================================================

// Выделить одну физическую страницу (4 KB).
// Возвращает физический адрес страницы или 0 при OOM.
//
// IRQ-safe: использует hal_irq_save/restore.
// Thread-safe: N/A (single-core ARM1176).
uint32_t arm_pmm_alloc_page(void);

// Освободить физическую страницу.
// phys_addr: физический адрес страницы (должен быть 4 KB aligned).
//
// IRQ-safe: использует hal_irq_save/restore.
// Defensive: проверяет range и alignment.
void arm_pmm_free_page(uint32_t phys_addr);

// ============================================================================
// RESERVATION
// ============================================================================

// Зарезервировать диапазон физической памяти.
// start: физический адрес начала (должен быть 4 KB aligned).
// size: размер диапазона в байтах (округляется вверх до 4 KB).
//
// Используется при boot для резервирования:
//   - Kernel image
//   - Stacks
//   - Page tables
//   - User spike regions
//   - Peripherals (не RAM, но для uniformity)
void arm_pmm_reserve_range(uint32_t start, uint32_t size);

// ============================================================================
// ACCOUNTING (для self-test и диагностики)
// ============================================================================

// Получить количество свободных страниц.
uint32_t arm_pmm_get_free_pages(void);

// Получить общее количество страниц в системе.
uint32_t arm_pmm_get_total_pages(void);

// Получить количество успешных alloc операций.
uint32_t arm_pmm_get_allocs(void);

// Получить количество успешных free операций.
uint32_t arm_pmm_get_frees(void);

// Проверить баланс allocs/frees (для leak detection).
// Выводит диагностику в UART.
void arm_pmm_check_balance(void);

#endif // ARM_PMM_H
