#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include "multiboot.h"

#define PMM_PAGE_SIZE   4096

// Пока ограничиваем PMM 512 МБ, так как наш Direct Map (VMM) покрывает только 
// первые 512 МБ (Virt == Phys). Страницы выше 512 МБ мы не сможем безопасно 
// адресовать через C-указатели без window mapping.
#define PMM_MAX_MEMORY_SIZE (512 * 1024 * 1024)
#define PMM_PAGES_COUNT (PMM_MAX_MEMORY_SIZE / PMM_PAGE_SIZE) // 131072 страницы

// Инициализация PMM с парсингом Multiboot E820 memory map
void pmm_init(multiboot_info_t* info);

// Выделение физической страницы (возвращает физ. адрес или 0 при OOM)
uint32_t pmm_alloc_page(void);

// Освобождение физической страницы
void pmm_free_page(uint32_t phys_addr);

// Статистика
uint32_t pmm_get_used_pages(void);
uint32_t pmm_get_free_pages(void);
uint32_t pmm_get_total_pages(void); // Сколько страниц доступно согласно E820

// Структура для хранения записи E820 (безопасная копия для Shell)
typedef struct {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
} e820_entry_t;

// Возвращает указатель на массив E820 и его размер
const e820_entry_t* pmm_get_memory_map(uint32_t* count);

// [НОВОЕ] Вывод карты E820 в консоль для диагностики
void pmm_dump_e820(void);

#endif // PMM_H