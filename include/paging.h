#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

// ============================================================================
// МАКРОСЫ ТРАНСЛЯЦИИ АДРЕСОВ (SSOT для Higher Half)
// ============================================================================
#define KERNEL_VIRT_BASE 0xC0000000

// Если адрес >= 0xC0000000, это Higher Half (вычитаем базу).
// Если адрес < 0xC0000000, это уже физический адрес (секции .boot), возвращаем как есть.
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_VIRT_BASE) ? ((uint32_t)(addr) - KERNEL_VIRT_BASE) : (uint32_t)(addr))

// Физический адрес всегда превращаем в виртуальный Higher Half
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_VIRT_BASE)

// ============================================================================
// ФЛАГИ СТРАНИЦ (Page Table Entry Flags)
// ============================================================================
#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PWT      0x8   
#define PAGE_PCD      0x10  // Cache Disable (MMIO/Framebuffer)
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY    0x40
#define PAGE_PS       0x80  // 4MB Page Size Extension

// ============================================================================
// ВНЕШНИЕ СИМВОЛЫ
// ============================================================================
extern uint32_t boot_page_directory[];

// ============================================================================
// API ВИРТУАЛЬНОЙ ПАМЯТИ
// ============================================================================

// Инициализация VMM (Direct Map, Reserving, Page Fault Handler)
void paging_init(void);

// Маппинг в КОНКРЕТНЫЙ Page Directory (для User Space процессов)
void vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags);

// Unmap страницы из конкретного PD
void vmm_unmap_page_in_pd(uint32_t* pd_virt, uint32_t virt);

// Создание нового адресного пространства (клонирование ядра 768-1023)
uint32_t* vmm_create_address_space(void);

// Явная смена CR3 (переключение контекста)
void vmm_switch_pdir(uint32_t phys_pd);

// Уничтожение адресного пространства задачи (вызывается из Reaper в schedule)
void vmm_destroy_address_space(uint32_t* pdir_virt);

// ============================================================================
// INLINE ОБЕРТКИ (Для глобального PD ядра)
// ============================================================================

// Макрос заменен на static inline для type-safety и отсутствия overhead
static inline void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    vmm_map_page_in_pd(boot_page_directory, virt, phys, flags);
}

static inline void vmm_unmap_page(uint32_t virt) {
    vmm_unmap_page_in_pd(boot_page_directory, virt);
}

#endif // PAGING_H
