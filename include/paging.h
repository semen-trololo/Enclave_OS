#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// Флаги страниц x86
#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PWT      0x8   // Write-Through
#define PAGE_PCD      0x10  // Cache Disable (критично для MMIO!)
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY    0x40
#define PAGE_PS       0x80  // Page Size (4 MB pages)

// Инициализация VMM
void paging_init(void);

// Маппинг виртуального адреса на физический с указанными флагами
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags);

// Unmap страницы (опционально)
void vmm_unmap_page(uint32_t virt);

#endif // PAGING_H