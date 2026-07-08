#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"
#include "serial.h"
#include "isr.h" 

// ============================================================================
// ВНЕШНИЕ СИМВОЛЫ И МАКРОСЫ
// ============================================================================
extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];
extern uint8_t boot_page_tables_hh[];
extern uint8_t fb_page_table[];
extern uint8_t boot_stack[];
extern uint8_t boot_stack_top[];

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

#define KERNEL_VIRT_BASE 0xC0000000
#define VIRT_TO_PHYS(addr) (((uint32_t)(addr) >= KERNEL_VIRT_BASE) ? ((uint32_t)(addr) - KERNEL_VIRT_BASE) : (uint32_t)(addr))
#define PHYS_TO_VIRT(addr) ((uint32_t)(addr) + KERNEL_VIRT_BASE)

// Диапазоны памяти
#define LAZY_ALLOC_START 0xD0000000
#define LAZY_ALLOC_END   0xE0000000
#define FB_VIRT_BASE     0xFD000000
#define FB_PHYS_BASE     0xFD000000
#define FB_SIZE_MB       16

// Флаги PTE
#define PAGE_PRESENT  0x1
#define PAGE_WRITE    0x2
#define PAGE_USER     0x4
#define PAGE_PCD      0x10   // Cache Disable (для MMIO/Framebuffer)
#define PAGE_SIZE_4MB 0x80   // PSE (Page Size Extension)

// ============================================================================
// API: БАЗОВЫЙ МАППИНГ (Обертка над vmm_map_page_in_pd)
// ============================================================================
void vmm_map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    vmm_map_page_in_pd(boot_page_directory, virt, phys, flags);
}

// ==============================================================================
// DAY 6.3: ON-DEMAND PAGING (PAGE FAULT HANDLER)
// ==============================================================================
void page_fault_handler(struct regs* r) {
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    int present   = r->err_code & 0x1; 
    int rw        = r->err_code & 0x2; 
    int us        = r->err_code & 0x4; 
    int reserved  = r->err_code & 0x8; 
    int id        = r->err_code & 0x10;
    
    // Логика Lazy Allocation (Kernel Space Only)
    if (!present && !reserved && !us) {
        if (faulting_address >= LAZY_ALLOC_START && faulting_address < LAZY_ALLOC_END) {
            uint32_t phys = pmm_alloc_page();
            if (phys != 0) {
                k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096); 
                uint32_t virt_page = faulting_address & 0xFFFFF000;
                
                // 🛡️ ИСПРАВЛЕНО: Убран PAGE_USER для Kernel Heap!
                vmm_map_page(virt_page, phys, PAGE_PRESENT | PAGE_WRITE);
                
                serial_printf("[PF] Lazy alloc: Virt 0x%x -> Phys 0x%x\n", virt_page, phys);
                return; // IRET повторит инструкцию
            }
        }
    }  
    
    // FATAL: Необработанный Page Fault
    serial_print("\n[PF] === FATAL PAGE FAULT ===\n");
    serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n", faulting_address, r->eip);
    serial_printf("[PF] Code: P:%d W:%d U:%d R:%d I:%d\n", present, rw, us, reserved, id);
    serial_printf("[PF] PMM Free Pages: %d\n", pmm_get_free_pages());
    serial_print("[PF] System Halted.\n");
    
    while(1) { __asm__ volatile("cli; hlt"); }
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ VMM
// ============================================================================
void paging_init(void) {
    serial_print("[VMM] Patching PDEs for Ring 3 access...\n");
    for (uint32_t i = 0; i < 1024; i++) {
        if (boot_page_directory[i] & PAGE_PRESENT) {
            boot_page_directory[i] |= PAGE_USER;
        }
    }

    serial_print("[VMM] Reserving kernel structures in PMM...\n");
    pmm_reserve_region(0x00100000, 0x01000000); // Conservative 1MB-16MB block
    
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_directory), VIRT_TO_PHYS((uint32_t)boot_page_directory) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables), VIRT_TO_PHYS((uint32_t)boot_page_tables) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_page_tables_hh), VIRT_TO_PHYS((uint32_t)boot_page_tables_hh) + (128 * 4096));
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)fb_page_table), VIRT_TO_PHYS((uint32_t)fb_page_table) + 4096);
    pmm_reserve_region(VIRT_TO_PHYS((uint32_t)boot_stack), VIRT_TO_PHYS((uint32_t)boot_stack_top));

    serial_print("[VMM] Building Direct Map (512MB)...\n");
    uint32_t total_ram_pages = (512 * 1024 * 1024) / 4096; 
    for (uint32_t i = 0; i < total_ram_pages; i++) {
        uint32_t phys = i * 4096;
        uint32_t virt = phys + KERNEL_VIRT_BASE;
        vmm_map_page(virt, phys, PAGE_PRESENT | PAGE_WRITE); // Kernel space - no PAGE_USER
    }
    
    serial_print("[VMM] Mapping Framebuffer...\n");
    uint32_t fb_pages = (FB_SIZE_MB * 1024 * 1024) / 4096; 
    for (uint32_t i = 0; i < fb_pages; i++) { 
        vmm_map_page(FB_VIRT_BASE + i * 4096, FB_PHYS_BASE + i * 4096, 
                     PAGE_PRESENT | PAGE_WRITE | PAGE_PCD | PAGE_USER);
    }
    
    isr_register_handler(14, page_fault_handler);
    serial_print("[VMM] Page Fault Handler registered (INT 14).\n");
    
    uint32_t pd_phys = VIRT_TO_PHYS((uint32_t)boot_page_directory);
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys));
    serial_print("[VMM] CR3 reloaded. Paging active.\n");
}

// ============================================================================
// МАППИНГ В ПРОИЗВОЛЬНЫЙ PAGE DIRECTORY
// ============================================================================
void vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];

    if (pde & PAGE_SIZE_4MB) {
        serial_print("[VMM] FATAL: 4MB Page detected in PDE!\n");
        while(1) __asm__("cli; hlt"); 
    }

    if (!(pde & PAGE_PRESENT)) {
        uint32_t new_pt_phys = pmm_alloc_page();
        if (new_pt_phys == 0) {
            serial_print("[VMM] OOM in vmm_map_page_in_pd!\n");
            return; 
        }
        k_memset((void*)PHYS_TO_VIRT(new_pt_phys), 0, 4096); 
        pd_virt[dir_index] = new_pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }

    uint32_t pt_phys = pd_virt[dir_index] & 0xFFFFF000; 
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys); 
    pt[table_index] = phys | flags;

    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ============================================================================
// СОЗДАНИЕ НОВОГО АДРЕСНОГО ПРОСТРАНСТВА (ДЕНЬ 7.5)
// ============================================================================
uint32_t* vmm_create_address_space(void) {
    uint32_t phys_pd = pmm_alloc_page();
    if (phys_pd == 0) return 0;
    
    uint32_t* virt_pd = (uint32_t*)PHYS_TO_VIRT(phys_pd);
    k_memset(virt_pd, 0, 4096);
    
    // Клонирование Kernel Space (Shared Kernel Space)
    for (int i = 768; i < 1024; i++) {
        virt_pd[i] = boot_page_directory[i];
    }
    
    serial_print("[VMM] Created new Address Space.\n");
    return virt_pd;
}

void vmm_switch_pdir(uint32_t phys_pd) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pd) : "memory");
}

// ============================================================================
// УНИЧТОЖЕНИЕ АДРЕСНОГО ПРОСТРАНСТВА (ИСПРАВЛЕНО: Memory Leak)
// ============================================================================
void vmm_destroy_address_space(uint32_t* pdir_virt) {
    if (!pdir_virt || pdir_virt == boot_page_directory) return;
    
    // 1. Освобождаем User Space (индексы 0-767)
    for (uint32_t i = 0; i < 768; i++) {
        if (pdir_virt[i] & PAGE_PRESENT) {
            uint32_t pt_phys = pdir_virt[i] & 0xFFFFF000;
            uint32_t* pt_virt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
            
            // 🛡️ ИСПРАВЛЕНО: Освобождаем сами страницы данных, на которые указывает PT
            for (uint32_t j = 0; j < 1024; j++) {
                if (pt_virt[j] & PAGE_PRESENT) {
                    uint32_t page_phys = pt_virt[j] & 0xFFFFF000;
                    pmm_free_page(page_phys);
                }
            }
            // Освобождаем саму Page Table
            pmm_free_page(pt_phys);
        }
    }
    
    // 2. Освобождаем Page Directory
    uint32_t pdir_phys = VIRT_TO_PHYS((uint32_t)pdir_virt);
    pmm_free_page(pdir_phys);
}
