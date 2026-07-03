#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"

extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PCD      0x10  // Cache Disable (критично для MMIO!)
#define PAGE_PWT      0x8   

void paging_init(void) {
    // ИСПРАВЛЕНИЕ ДНЯ 4 (Ring 3): Снимаем защиту Supervisor со всех существующих 
    // Page Directory Entries. В x86, если PDE имеет U/S=0, то весь 4МБ регион 
    // недоступен из Ring 3, даже если в дочерних PTE стоит U/S=1.
    // Без этого цикла переход в Ring 3 вызовет Page Fault на уровне каталога.
    for (uint32_t i = 0; i < 1024; i++) {
        if (boot_page_directory[i] & PAGE_PRESENT) {
            boot_page_directory[i] |= PAGE_USER;
        }
    }

    // 1. Direct Map: Мапим ВСЮ физическую память в Higher Half (0xC0000000+)
    uint32_t total_ram_pages = (256 * 1024 * 1024) / 4096; 
    
    for (uint32_t i = 0; i < total_ram_pages; i++) {
        uint32_t phys = i * 4096;
        uint32_t virt = phys + 0xC0000000;
        // Добавлен PAGE_USER для Варианта А (тест Ring 3)
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    
    // 2. Сохраняем маппинг фреймбуфера (0xFD000000)
    uint32_t fb_virt = 0xFD000000;
    uint32_t fb_phys = 0xFD000000;
    for (uint32_t i = 0; i < 768; i++) { 
        // Добавлен PAGE_USER для доступа из Ring 3 (временно)
        vmm_map_page(fb_virt + i * 4096, fb_phys + i * 4096, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_USER);
    }
    
    // 3. Reload CR3 to flush TLB (обязательно после модификации PDE!)
    uint32_t pd_phys = (uint32_t)boot_page_directory;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    if (!(boot_page_directory[dir_index] & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) return; 
        
        // ✅ Используем ФИЗИЧЕСКИЙ адрес напрямую благодаря Identity Map.
        k_memset((void*)new_pt_phys, 0, 4096); 
        
        // ИСПРАВЛЕНИЕ ДНЯ 4 (Ring 3): Добавляем PAGE_USER в саму PDE!
        // Это гарантирует, что любая новая Page Table будет доступна из Ring 3.
        boot_page_directory[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        
        // Сбрасываем TLB, чтобы MMU перечитал новую PDE
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
    }

    uint32_t pt_phys = boot_page_directory[dir_index] & 0xFFFFF000; 
    
    // ✅ Используем физический адрес благодаря Identity Map.
    uint32_t* pt = (uint32_t*)pt_phys; 
    
    pt[table_index] = phys | flags;

    // Инвалидируем TLB для конкретной страницы
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}