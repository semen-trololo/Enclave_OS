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
    // 1. Direct Map: Мапим ВСЮ физическую память в Higher Half (0xC0000000+)
    uint32_t total_ram_pages = (256 * 1024 * 1024) / 4096; 
    
    for (uint32_t i = 0; i < total_ram_pages; i++) {
        uint32_t phys = i * 4096;
        uint32_t virt = phys + 0xC0000000;
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE);
    }
    
    // 2. Сохраняем маппинг фреймбуфера (0xFD000000)
    uint32_t fb_virt = 0xFD000000;
    uint32_t fb_phys = 0xFD000000;
    for (uint32_t i = 0; i < 768; i++) { 
        vmm_map_page(fb_virt + i * 4096, fb_phys + i * 4096, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD);
    }
    
    // 3. Reload CR3 to flush TLB
    uint32_t pd_phys = (uint32_t)boot_page_directory;
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
}

void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    if (!(boot_page_directory[dir_index] & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) return; 
        
        // ✅ ИСПРАВЛЕНИЕ: Используем ФИЗИЧЕСКИЙ адрес напрямую!
        // Почему это работает? Потому что Identity Map (первые 512 МБ) всё ещё 
        // активен в boot_page_directory (настроен в boot.asm). 
        // PMM выделяет страницы из RAM (< 256 МБ), поэтому они гарантированно 
        // попадают в Identity Map. Это позволяет избежать Page Fault при 
        // попытке замапить Direct Map (который ещё не построен).
        k_memset((void*)new_pt_phys, 0, 4096); 
        
        boot_page_directory[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE;
        
        uint32_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        __asm__ volatile("mov %0, %%cr3" : : "r"(cr3));
    }

    uint32_t pt_phys = boot_page_directory[dir_index] & 0xFFFFF000; 
    
    // ✅ ИСПРАВЛЕНИЕ: Снова используем физический адрес напрямую благодаря Identity Map.
    uint32_t* pt = (uint32_t*)pt_phys; 
    
    pt[table_index] = phys | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}