#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "multiboot.h"

// ============================================================================
// КОНСТАНТЫ PMM
// ============================================================================
#define PMM_PAGE_SIZE   4096
#define PMM_MAX_PAGES   1048576  // 4GB / 4KB

// ============================================================================
// API ФИЗИЧЕСКОГО АЛЛОКАТОРА
// ============================================================================
void pmm_init(multiboot_info_t* info);
uint32_t pmm_alloc_page(void);
void pmm_reserve_region(uint64_t base, uint64_t end);
void pmm_free_page(uint32_t phys_addr);

// ============================================================================
// [ДЕНЬ 14] REFERENCE COUNTING (Для Copy-on-Write)
// ============================================================================
// Инкрементирует счетчик ссылок на физическую страницу.
// Используется при fork() для marking страниц как shared.
void pmm_inc_ref(uint32_t phys_addr);

// Декрементирует счетчик ссылок. Если счетчик стал 0, автоматически
// вызывает pmm_free_page() для возврата страницы в пул.
void pmm_dec_ref(uint32_t phys_addr);

// ✅ НОВЫЙ API: Безопасное получение refcount без нарушения инкапсуляции PMM
uint16_t pmm_get_refcount(uint32_t phys_addr);

// ============================================================================
// СТАТИСТИКА И ДИАГНОСТИКА
// ============================================================================
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_total_pages(void);
uint32_t pmm_get_max_pages(void);

// ============================================================================
// [ДЕНЬ 10] PMM ACCOUNTING (Для Test Runner)
// ============================================================================
uint32_t pmm_get_alloc_count(void);
uint32_t pmm_get_free_count(void);
int32_t  pmm_check_balance(void);

// ============================================================================
// E820 MEMORY MAP
// ============================================================================
typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} e820_entry_t;

const e820_entry_t* pmm_get_memory_map(uint32_t* count);
void pmm_dump_e820(void);

#endif // PMM_H