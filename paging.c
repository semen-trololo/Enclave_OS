#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"
#include "serial.h"
#include "isr.h" 

extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];
extern uint8_t boot_page_tables_hh[];

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// ============================================================================
// МАКРОСЫ ДЛЯ РАБОТЫ С HIGHER HALF (ИСПРАВЛЕНО ОТ UNDERFLOW)
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
#define PAGE_PRESENT  0x1    // Страница присутствует в памяти
#define PAGE_WRITE    0x2    // Разрешена запись
#define PAGE_USER     0x4    // Доступна из Ring 3 (User Mode)
#define PAGE_PWT      0x8    // Page-Level Write-Through
#define PAGE_PCD      0x10   // Page-Level Cache Disable (критично для MMIO!)
#define PAGE_ACCESSED 0x20   // Страница была прочитана
#define PAGE_DIRTY    0x40   // В страницу была запись

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
                // ✅ ИСПРАВЛЕНО: Используем PHYS_TO_VIRT для доступа к физической странице
                k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096); 
                
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

#define PAGE_SIZE_4MB 0x80 // Бит Page Size Extension в PDE

// ============================================================================
// ВНЕШНИЕ СИМВОЛЫ ИЗ BOOT.ASM (Для резервирования в PMM)
// ============================================================================
extern uint8_t fb_page_table[];
extern uint8_t boot_stack[];
extern uint8_t boot_stack_top[];

void paging_init(void) {
    serial_print("[VMM] paging_init: Patching PDEs for Ring 3...\n");
    for (uint32_t i = 0; i < 1024; i++) {
        if (boot_page_directory[i] & PAGE_PRESENT) {
            boot_page_directory[i] |= PAGE_USER;
        }
    }

    // ✅ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ: Резервируем ВСЮ память ядра (включая .boot секции)
    // boot.asm размещает .boot.bss, .boot.data, .boot ПЕРЕД _kernel_start или ПОСЛЕ _kernel_end.
    // Мы резервируем диапазон от 1 МБ (конец BIOS/Low Memory) до конца ядра + 1 МБ запаса.
    // Это гарантирует, что PMM никогда не выделит страницу, на которой лежит код ядра.
    serial_print("[VMM] Reserving ENTIRE kernel memory in PMM...\n");
    
    // Резервируем от 1 МБ до 16 МБ (покрывает ядро, .boot.bss, stack, page tables)
    // Это консервативный подход: лучше зарезервировать лишнее, чем затереть код.
    pmm_reserve_region(0x00100000, 0x01000000); // 1 МБ - 16 МБ
    
    // Дополнительно резервируем конкретные структуры (на случай, если они выше 16 МБ)
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_directory), 
                       VIRT_TO_PHYS((uint32_t)boot_page_directory) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables), 
                       VIRT_TO_PHYS((uint32_t)boot_page_tables) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables_hh), 
                       VIRT_TO_PHYS((uint32_t)boot_page_tables_hh) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)fb_page_table), 
                       VIRT_TO_PHYS((uint32_t)fb_page_table) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_stack), 
                       VIRT_TO_PHYS((uint32_t)boot_stack_top));

    serial_print("[VMM] paging_init: Starting Direct Map (512MB)...\n");
    uint32_t total_ram_pages = (512 * 1024 * 1024) / 4096; 
    
    for (uint32_t i = 0; i < total_ram_pages; i++) {
        uint32_t phys = i * 4096;
        uint32_t virt = phys + 0xC0000000;
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE | PAGE_USER);
    }
    serial_print("[VMM] paging_init: Direct Map finished.\n");
    
    serial_print("[VMM] paging_init: Mapping Framebuffer (16MB)...\n");
    uint32_t fb_virt = 0xFD000000;
    uint32_t fb_phys = 0xFD000000;
    uint32_t fb_pages = (16 * 1024 * 1024) / 4096; 
    
    for (uint32_t i = 0; i < fb_pages; i++) { 
        vmm_map_page(fb_virt + i * 4096, fb_phys + i * 4096, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_USER);
    }
    serial_print("[VMM] paging_init: Framebuffer mapped.\n");
    
    isr_register_handler(14, page_fault_handler);
    serial_print("[DEBUG] Page Fault Handler registered (INT 14)\n");
    
    uint32_t pd_phys = VIRT_TO_PHYS(boot_page_directory);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    serial_print("[VMM] paging_init: CR3 reloaded. Paging active.\n");
}

// ============================================================================
// МАППИНГ СТРАНИЦЫ В ПРОИЗВОЛЬНЫЙ PAGE DIRECTORY
// ============================================================================
void vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];

    // 🛡️ КРИТИЧЕСКАЯ ЗАЩИТА: Детектим 4MB страницы
    if (pde & 0x80) { // PAGE_SIZE_4MB
        serial_print("[VMM] FATAL: 4MB Page detected in PDE!\n");
        while(1) __asm__("hlt"); 
    }

    if (!(pde & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) {
            serial_print("[VMM] OOM in vmm_map_page_in_pd!\n");
            return; 
        }
        
        k_memset((void*)PHYS_TO_VIRT(new_pt_phys), 0, 4096); 
        
        // Записываем физический адрес новой PT в указанный PD
        pd_virt[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        
        // Сбрасываем TLB для этой записи (инвалидация)
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }

    uint32_t pt_phys = pd_virt[dir_index] & 0xFFFFF000; 
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys); 
    
    pt[table_index] = phys | flags;

    // Инвалидация TLB для конкретного адреса
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ============================================================================
// СОЗДАНИЕ НОВОГО АДРЕСНОГО ПРОСТРАНСТВА (ДЕНЬ 7.5)
// ============================================================================
uint32_t* vmm_create_address_space(void) {
    // 1. Выделяем физическую страницу под новый Page Directory
    uint32_t phys_pd = pmm_alloc_page();
    if (phys_pd == 0) return 0;
    
    // 2. Получаем виртуальный адрес для манипуляций из C-кода
    uint32_t* virt_pd = (uint32_t*)PHYS_TO_VIRT(phys_pd);
    
    // 3. Очищаем новый PD (все User Space страницы unmapped)
    k_memset(virt_pd, 0, 4096);
    
    // 4. КЛОНИРОВАНИЕ KERNEL SPACE (Индексы 768-1023)
    // Это обеспечивает "общую крышу": код ядра, Direct Map и фреймбуфер 
    // будут доступны новой задаче по тем же виртуальным адресам.
    for (int i = 768; i < 1024; i++) {
        virt_pd[i] = boot_page_directory[i];
    }
    
    serial_print("[VMM] Created new Address Space (PD cloned).\n");
    return virt_pd;
}

// ============================================================================
// ЯВНАЯ СМЕНА CR3
// ============================================================================
void vmm_switch_pdir(uint32_t phys_pd) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pd) : "memory");
}