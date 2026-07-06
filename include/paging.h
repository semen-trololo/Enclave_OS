#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include "idt.h"

// ============================================================================
// МАКРОСЫ ТРАНСЛЯЦИИ АДРЕСОВ (Вынесены для общего доступа)
// ============================================================================
#define KERNEL_VIRT_BASE 0xC0000000

// Если адрес >= 0xC0000000, это Higher Half (вычитаем базу).
// Если адрес < 0xC0000000, это уже физический адрес (секции .boot), возвращаем как есть.
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_VIRT_BASE) ? ((uint32_t)(addr) - KERNEL_VIRT_BASE) : (uint32_t)(addr))

// Физический адрес всегда превращаем в виртуальный Higher Half
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_VIRT_BASE)

// ============================================================================
// ФЛАГИ И СТРУКТУРЫ
// ============================================================================
#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PWT      0x8   
#define PAGE_PCD      0x10  
#define PAGE_ACCESSED 0x20
#define PAGE_DIRTY    0x40
#define PAGE_PS       0x80  

// Глобальный PD ядра (нужен для клонирования Kernel Space)
extern uint32_t boot_page_directory[];

// Инициализация VMM
void paging_init(void);

// Маппинг в КОНКРЕТНЫЙ Page Directory
void vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags);

// Создание нового адресного пространства (клонирование ядра)
uint32_t* vmm_create_address_space(void);

// Явная смена CR3
void vmm_switch_pdir(uint32_t phys_pd);

// Unmap страницы
void vmm_unmap_page(uint32_t virt);

// Макрос для обратной совместимости (маппинг в глобальный PD ядра)
#define vmm_map_page(virt, phys, flags) vmm_map_page_in_pd(boot_page_directory, virt, phys, flags)

#endif // PAGING_H