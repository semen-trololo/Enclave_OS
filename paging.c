#include "paging.h"
#include "pmm.h"
#include "klib.h"
#include "framebuffer.h"
#include "serial.h"
#include "isr.h"
#include "config.h"
#include "vma.h"

// Forward declaration для функции убийства процесса
void task_kill_current(const char* reason);

// ============================================================================
// ВНЕШНИЕ СИМБОЛЫ (Boot structures из boot.asm)
// ============================================================================
extern uint32_t boot_page_directory[];
extern uint32_t boot_page_tables[];
extern uint8_t boot_page_tables_hh[];
extern uint8_t fb_page_table[];
extern uint8_t boot_stack[];
extern uint8_t boot_stack_top[];

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// ============================================================================
// ДИАПАЗОНЫ ПАМЯТИ  Framebuffer
// ============================================================================
#define FB_VIRT_BASE     0xFD000000
#define FB_PHYS_BASE     0xFD000000
#define FB_SIZE_MB       16

// ============================================================================
// DAY 9: PARANOID PAGE FAULT HANDLER (Zero Trust Sandbox)
// ============================================================================
void page_fault_handler(struct regs* r) {
    uint32_t faulting_address;
    __asm__ volatile("mov %%cr2, %0" : "=r"(faulting_address));

    int present   = r->err_code & 0x1; 
    int rw        = r->err_code & 0x2; 
    int us        = r->err_code & 0x4; 
    int reserved  = r->err_code & 0x8; 
    int id        = r->err_code & 0x10;
    
    // ========================================================================
    // 1. NULL POINTER GUARD (Первая страница)
    // ========================================================================
    if (faulting_address < 0x1000) {
        if (us) {
            serial_printf("[PF] SIGSEGV: NULL Pointer Dereference in PID %d\n", 
                          current_task->pid);
            task_kill_current("NULL Pointer Dereference");
        } else {
            serial_print("\n[PF] === FATAL: Kernel NULL Pointer Dereference ===\n");
            serial_printf("[PF] EIP: 0x%x\n", r->eip);
            while(1) { __asm__ volatile("cli; hlt"); }
        }
    }
    
    // ========================================================================
    // 2. KERNEL SPACE PROTECTION (Защита ядра от Ring 3)
    // ========================================================================
    if (faulting_address >= KERNEL_SPACE_START && us) {
        serial_printf("[PF] SIGSEGV: Ring 3 attempted access to Kernel Space (0x%x) in PID %d\n", 
                      faulting_address, current_task->pid);
        task_kill_current("Attempted access to Kernel Space");
    }
    
    // ========================================================================
    // 3. KERNEL DEMAND PAGING (Ядро работает по старым правилам)
    // ========================================================================
    if (!us) {
        if (!present && !reserved && faulting_address >= KERNEL_HEAP_VIRT && 
            faulting_address < KERNEL_HEAP_END) {
            uint32_t phys = pmm_alloc_page();
            if (phys != 0) {
                k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096); 
                uint32_t virt_page = faulting_address & 0xFFFFF000;
                
                // 🛡️ БЕЗОПАСНОСТЬ: Убран PAGE_USER для Kernel Heap!
                vmm_map_page(virt_page, phys, PAGE_PRESENT | PAGE_WRITE);
                
                serial_printf("[PF] Lazy alloc: Virt 0x%x -> Phys 0x%x\n", virt_page, phys);
                return; // IRET повторит инструкцию
            } else {
                serial_print("\n[PF] === FATAL: Kernel OOM ===\n");
                while(1) { __asm__ volatile("cli; hlt"); }
            }
        }
        
        // Необработанный Kernel Page Fault
        serial_print("\n[PF] === FATAL PAGE FAULT ===\n");
        serial_printf("[PF] Address: 0x%x | EIP: 0x%x\n", faulting_address, r->eip);
        serial_printf("[PF] Code: P:%d W:%d U:%d R:%d I:%d\n", present, rw, us, reserved, id);
        serial_print("[PF] System Halted.\n");
        while(1) { __asm__ volatile("cli; hlt"); }
    }
    
    // ========================================================================
    // 4. USER SPACE VMA ENFORCEMENT (Zero Trust)
    // ========================================================================
    vma_node_t* vma = vma_find(current_task, faulting_address);
    
    if (!vma) {
        serial_printf("[PF] SIGSEGV: Access to unmapped memory (0x%x) in PID %d\n", 
                      faulting_address, current_task->pid);
        task_kill_current("Access to unmapped memory (No VMA)");
    }
    
    // ========================================================================
    // 5. W^X ENFORCEMENT (Проверка прав доступа)
    // ========================================================================
    if (rw && !(vma->flags & VMA_WRITE)) {
        serial_printf("[PF] SIGSEGV: Write to Read-Only memory (0x%x) in PID %d\n", 
                      faulting_address, current_task->pid);
        task_kill_current("Write to Read-Only memory (W^X violation)");
    }
    
    // ========================================================================
    // 6. DEMAND PAGING (Легальное выделение памяти)
    // ========================================================================
    uint32_t phys = pmm_alloc_page();
    
    // ========================================================================
    // 7. OOM TRAP (Реактивная защита)
    // ========================================================================
    if (phys == 0) {
        serial_printf("[PF] OOM Kill: Physical memory exhausted in PID %d\n", 
                      current_task->pid);
        task_kill_current("Out of Memory (OOM Kill)");
    }
    
    // ========================================================================
    // 8. Маппинг с правильными флагами
    // ========================================================================
    uint32_t flags = PAGE_PRESENT | PAGE_USER;
    if (vma->flags & VMA_WRITE) flags |= PAGE_WRITE;
    
    k_memset((void*)PHYS_TO_VIRT(phys), 0, 4096); // Zero-fill
    uint32_t virt_page = faulting_address & 0xFFFFF000;
    vmm_map_page_in_pd(current_task->pdir_virt, virt_page, phys, flags);
    
    serial_printf("[PF] Demand paging: Virt 0x%x -> Phys 0x%x (PID %d)\n", 
                  virt_page, phys, current_task->pid);
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ VMM (Bootstrap Sequence)
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
// МАППИНГ В ПРОИЗВОЛЬНЫЙ PAGE DIRECTORY (Для User Space процессов)
// ============================================================================
void vmm_map_page_in_pd(uint32_t* pd_virt, uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];

    // 🛡️ БЕЗОПАСНОСТЬ: Проверка на 4MB страницы (PSE)
    // Если PDE уже мапит 4MB блок, создание Page Table внутри него приведет к коррупции памяти.
    if (pde & PAGE_PS) {
        serial_print("[VMM] FATAL: 4MB Page detected in PDE!\n");
        while(1) __asm__("cli; hlt"); 
    }

    // Если Page Table отсутствует, выделяем новую
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

    // Получаем виртуальный адрес Page Table и записываем PTE
    uint32_t pt_phys = pd_virt[dir_index] & 0xFFFFF000; 
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys); 
    pt[table_index] = phys | flags;

    // Аппаратная инвалидация TLB для этого виртуального адреса
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

// ============================================================================
// UNMAP СТРАНИЦЫ (Очистка PTE и инвалидация TLB)
// ============================================================================
void vmm_unmap_page_in_pd(uint32_t* pd_virt, uint32_t virt) {
    uint32_t dir_index = virt >> 22;
    uint32_t table_index = (virt >> 12) & 0x3FF;

    uint32_t pde = pd_virt[dir_index];
    
    // Если Page Table отсутствует, делать нечего
    if (!(pde & PAGE_PRESENT)) return; 

    // Получаем виртуальный адрес самой Page Table
    uint32_t pt_phys = pde & 0xFFFFF000;
    uint32_t* pt = (uint32_t*)PHYS_TO_VIRT(pt_phys);

    // Если страница замаплена, очищаем PTE
    if (pt[table_index] & PAGE_PRESENT) {
        pt[table_index] = 0; 
        
        // Аппаратная инвалидация TLB для этого виртуального адреса
        __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    }
    
    // 💡 TODO (Day 12 Hardening): PT Leak Protection
    // Проверить, стала ли вся Page Table пустой (все 1024 PTE == 0).
    // Если да, освободить саму страницу Page Table через pmm_free_page(pt_phys),
    // и обнулить PDE. Иначе при массовом создании/убийстве процессов мы будем 
    // терять по 4KB памяти на каждую "опустевшую" Page Table.
}

// ============================================================================
// СОЗДАНИЕ НОВОГО АДРЕСНОГО ПРОСТРАНСТВА (Shared Kernel Space)
// ============================================================================
uint32_t* vmm_create_address_space(void) {
    uint32_t phys_pd = pmm_alloc_page();
    if (phys_pd == 0) return 0;
    
    uint32_t* virt_pd = (uint32_t*)PHYS_TO_VIRT(phys_pd);
    k_memset(virt_pd, 0, 4096);
    
    // Клонирование Kernel Space (индексы 768-1023)
    // Это обеспечивает Shared Kernel Space — все процессы видят одно ядро.
    for (int i = 768; i < 1024; i++) {
        virt_pd[i] = boot_page_directory[i];
    }
    
    serial_print("[VMM] Created new Address Space.\n");
    return virt_pd;
}

// ============================================================================
// ПЕРЕКЛЮЧЕНИЕ PAGE DIRECTORY (Context Switch)
// ============================================================================
void vmm_switch_pdir(uint32_t phys_pd) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys_pd) : "memory");
}

// ============================================================================
// УНИЧТОЖЕНИЕ АДРЕСНОГО ПРОСТРАНСТВА (Deep Free - Memory Leak Prevention)
// ============================================================================
void vmm_destroy_address_space(uint32_t* pdir_virt) {
    if (!pdir_virt || pdir_virt == boot_page_directory) return;
    
    // 1. Освобождаем User Space (индексы 0-767)
    for (uint32_t i = 0; i < 768; i++) {
        if (pdir_virt[i] & PAGE_PRESENT) {
            uint32_t pt_phys = pdir_virt[i] & 0xFFFFF000;
            uint32_t* pt_virt = (uint32_t*)PHYS_TO_VIRT(pt_phys);
            
            // 🛡️ CRITICAL: Освобождаем сами страницы данных (PTE)
            // Без этого система быстро упадет в OOM из-за утечки физической памяти.
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

// ============================================================================
// УБИЙСТВО ТЕКУЩЕГО ПРОЦЕССА (Вызывается из Page Fault Handler)
// ============================================================================
void task_kill_current(const char* reason) {
    serial_printf("[KILL] PID %d (%s) terminated: %s\n", 
                  current_task->pid, current_task->name, reason);
    
    // Вызываем стандартный механизм завершения.
    // task_exit() закроет FD, освободит FPU, пометит задачу как DEAD 
    // и вызовет schedule(), который переключит контекст.
    // Возврата в page_fault_handler не будет.
    task_exit();
}