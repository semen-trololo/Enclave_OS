#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"
#include "serial.h"
#include "isr.h" 

extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PCD      0x10  // Cache Disable (критично для MMIO!)
#define PAGE_PWT      0x8   

// ==============================================================================
// DAY 6.3: ON-DEMAND PAGING (PAGE FAULT HANDLER)
// ==============================================================================
void page_fault_handler(struct regs* r) {
    // 1. Читаем адрес, на котором произошел сбой (аппаратно сохраняется в CR2)
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    // 2. Декодируем Error Code, который процессор пушил в стек
    int present   = r->err_code & 0x1; // 0 = Страница отсутствует
    int rw        = r->err_code & 0x2; // 1 = Запись, 0 = Чтение
    int us        = r->err_code & 0x4; // 1 = User Mode, 0 = Kernel Mode
    int reserved  = r->err_code & 0x8; // 1 = Затронуты reserved биты (фатально)
    int id        = r->err_code & 0x10;// 1 = Instruction fetch

    serial_print("\n[PF] === PAGE FAULT TRIGGERED ===\n");
    
    // 3. Логика Lazy Allocation (Ленивая аллокация)
    // Мы выделяем память "по требованию" для виртуального диапазона 0xD0000000 - 0xE0000000
    // Условие: Страницы нет (!present), нет криминала с битами (!reserved), и это ядро (!us).
    if (!present && !reserved && !us) {
        if (faulting_address >= 0xD0000000 && faulting_address < 0xE0000000) {
            
            uint32_t phys = pmm_alloc_page();
            if (phys != 0) {
                // Zero-filled page: Стандарт безопасности Linux/Windows. 
                // Очищаем страницу, чтобы процесс не увидел чужие данные из освобожденной памяти.
                k_memset((void*)phys, 0, 4096); 
                
                // Округляем адрес сбоя до границы страницы (4KB aligned)
                uint32_t virt_page = faulting_address & 0xFFFFF000;
                
                // Мапим физическую страницу в виртуальное пространство
                vmm_map_page(virt_page, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
                
                serial_print("[PF] Lazy allocation successful! Resuming CPU...\n");
                k_printf("[PF] Mapped 0x%x -> Phys 0x%x\n", virt_page, phys);
                
                // ВЫХОД ИЗ ОБРАБОТЧИКА:
                // Мы делаем return. ASM-стаб сделает IRET.
                // Процессор АВТОМАТИЧЕСКИ повторит инструкцию, которая вызвала fault.
                // Но теперь страница на месте, и инструкция выполнится успешно!
                return; 
            }
        }
    }
    // 4. Если дошли сюда — это фатальная ошибка (кривой указатель, segfault)
    serial_print("[PF] FATAL: Unhandled Page Fault! Halting.\n");
    
    k_printf("\n KERNEL PANIC: PAGE FAULT \n");
    k_printf(" Address: 0x%x \n", faulting_address);
    k_printf(" EIP:     0x%x \n", r->eip);
    k_printf(" Code:    [P:%d W:%d U:%d R:%d I:%d] \n", present, rw, us, reserved, id);
    k_print(" System Halted. \n");
    
    while(1) { __asm__ volatile("hlt"); }
}

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
    // Перехватываем вектор 14 (Page Fault) у дефолтного обработчика исключений
    isr_register_handler(14, page_fault_handler);
    serial_print("[DEBUG] Page Fault Handler registered (INT 14)\n");
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