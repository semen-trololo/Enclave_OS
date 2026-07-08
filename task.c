#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"
#include "isr.h"
#include "vfs.h" 

extern void switch_context(uint32_t* old_esp, uint32_t new_esp, uint32_t new_cr3);

task_t* current_task = 0;
uint32_t next_pid = 1;
static task_t* fpu_owner = 0;

// 🆕 ГЛОБАЛЬНЫЙ REAPER: Задача, которую нужно "похоронить" после переключения контекста
static task_t* task_to_reap = NULL;

// ============================================================================
// ТРАМПЛИН (Task Wrapper)
// ============================================================================
void task_entry_trampoline(void (*entry_point)(void)) {
    __asm__ volatile("sti"); // Гарантированно включаем прерывания для новой задачи
    entry_point();
    task_exit();
}

// ============================================================================
// LAZY FPU SWITCHING (INT 7 - #NM HANDLER)
// ============================================================================
static volatile int in_nm_handler = 0;

static void device_not_available_handler(struct regs* r) {
    (void)r;
    if (in_nm_handler) {
        serial_print("\n[FPU] FATAL: Recursive #NM detected!\n");
        while(1) __asm__("cli; hlt");
    }
    in_nm_handler = 1;

    __asm__ volatile("clts"); // Сбрасываем CR0.TS ДО fxsave

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) {
        cr4 |= (1 << 9) | (1 << 10);
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    }

    if (fpu_owner && fpu_owner != current_task) {
        __asm__ volatile("fxsave (%0)" : : "r"(fpu_owner->fpu_state) : "memory");
    }
    
    if (current_task->fpu_initialized) {
        __asm__ volatile("fxrstor (%0)" : : "r"(current_task->fpu_state) : "memory");
    } else {
        __asm__ volatile("fninit");
        __asm__ volatile("fxsave (%0)" : : "r"(current_task->fpu_state) : "memory");
        current_task->fpu_initialized = 1;
    }
    
    fpu_owner = current_task;
    in_nm_handler = 0; 
}

void fpu_release_ownership(task_t* task) {
    if (fpu_owner == task) fpu_owner = 0;
}

// ============================================================================
// УПРАВЛЕНИЕ ОЧЕРЕДЬЮ
// ============================================================================
static void task_queue_add(task_t* task) {
    if (!current_task) {
        current_task = task;
        task->next = task;
        task->prev = task;
    } else {
        task->next = current_task->next;
        task->prev = current_task;
        current_task->next->prev = task;
        current_task->next = task;
    }
}

// ============================================================================
// ИНИЦИАЛИЗАЦИЯ ПЛАНИРОВЩИКА
// ============================================================================
void tasking_init(void) {
    serial_print("[TASK] Initializing Task Manager...\n");
    
    uint32_t main_pcb_phys = pmm_alloc_page();
    if (main_pcb_phys == 0) {
        serial_print("[TASK] FATAL: OOM allocating main_task PCB!\n");
        while(1) __asm__("cli; hlt");
    }
    
    task_t* main_task_ptr = (task_t*)PHYS_TO_VIRT(main_pcb_phys);
    k_memset(main_task_ptr, 0, sizeof(task_t));
    
    main_task_ptr->pid = next_pid++;
    main_task_ptr->state = TASK_RUNNING;
    main_task_ptr->kernel_stack = 0; // Использует boot_stack
    
    main_task_ptr->pdir_virt = boot_page_directory;
    main_task_ptr->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);

    const char* name = "main";
    int i = 0; while(name[i] && i < 31) { main_task_ptr->name[i] = name[i]; i++; }
    main_task_ptr->name[i] = '\0';
    
    current_task = main_task_ptr;
    main_task_ptr->next = main_task_ptr;
    main_task_ptr->prev = main_task_ptr;
    
    task_init_fds(main_task_ptr);
    
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Clear EM
    cr0 |= (1 << 1);  // Set MP
    cr0 |= (1 << 5);  // Set NE
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  // OSFXSR
    cr4 |= (1 << 10); // OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    
    isr_register_handler(7, device_not_available_handler);
    
    serial_print("[TASK] Main task registered.\n");
    serial_print("[TASK] FPU Lazy Switching enabled.\n");
}

// ============================================================================
// СОЗДАНИЕ НОВОЙ ЗАДАЧИ
// ============================================================================
task_t* task_create(const char* name, void (*entry_point)(void)) {
    uint32_t stack_phys = pmm_alloc_page();
    if (stack_phys == 0) return 0;
    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) {
        pmm_free_page(stack_phys);
        return 0;
    }

    task_t* new_task = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(new_task, 0, sizeof(task_t));

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->kernel_stack = stack_phys;
    
    new_task->pdir_virt = vmm_create_address_space();
    if (!new_task->pdir_virt) {
        serial_print("[TASK] OOM: Failed to create Address Space!\n");
        pmm_free_page(stack_phys);
        pmm_free_page(pcb_phys);
        return 0;
    }
    new_task->cr3 = VIRT_TO_PHYS((uint32_t)new_task->pdir_virt);

    int i = 0; while(name[i] && i < 31) { new_task->name[i] = name[i]; i++; }
    new_task->name[i] = '\0';

    // 🔥 STACK FORGING (Идеальное соответствие CDECL ABI)
    uint32_t* stack_top = (uint32_t*)PHYS_TO_VIRT(stack_phys + 4096);
    *(--stack_top) = (uint32_t)entry_point;             // [ESP+8] Arg 1 для trampoline
    *(--stack_top) = (uint32_t)task_exit;               // [ESP+4] Fake Return Address
    *(--stack_top) = (uint32_t)task_entry_trampoline;   // [ESP+0] EIP для первого ret
    *(--stack_top) = 0; // EBX
    *(--stack_top) = 0; // ESI
    *(--stack_top) = 0; // EDI
    *(--stack_top) = 0; // EBP
    new_task->esp = (uint32_t)stack_top;

    for (int j = 0; j < TASK_MAX_OPEN_FILES; j++) new_task->fd_table[j] = 0;
    task_init_fds(new_task); 
    
    task_queue_add(new_task);

    serial_printf("[TASK] Created PID %d: %s\n", new_task->pid, name);
    return new_task;
}

// ============================================================================
// ПЛАНИРОВЩИК (ROUND-ROBIN + REAPER + IRQ SAFE)
// ============================================================================
void schedule(void) {
    // 🛡️ ЗАЩИТА ОТ RACE CONDITION: Сохраняем EFLAGS и отключаем прерывания
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));

    if (!current_task || !current_task->next) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return;
    }
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    if (old_task == new_task) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return; 
    }

    if (old_task->state == TASK_RUNNING) old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    // Обновление TSS ESP0 (для прерываний из Ring 3)
    if (new_task->kernel_stack != 0) {
        tss_set_kernel_stack(0x10, PHYS_TO_VIRT(new_task->kernel_stack + 4096));
    }

    // Если предыдущая задача умерла, передаем её "следующему" для зачистки
    if (old_task->state == TASK_DEAD) {
        task_to_reap = old_task;
    }

    // ТЕЛЕПОРТАЦИЯ
    switch_context(&old_task->esp, new_task->esp, new_task->cr3);
    
    // --- ЗДЕСЬ ПРОДОЛЖАЕТ ВЫПОЛНЕНИЕ УЖЕ НОВАЯ ЗАДАЧА ---

    // 🆕 REAPER MECHANISM: Зачистка памяти DEAD задачи
    if (task_to_reap) {
        task_t* dead = task_to_reap;
        task_to_reap = NULL; // Сбрасываем глобальный флаг
        
        serial_printf("[REAPER] Cleaning up PID %d (%s)\n", dead->pid, dead->name);
        if (dead->pdir_virt && dead->pdir_virt != boot_page_directory) {
            vmm_destroy_address_space(dead->pdir_virt);
        }
        if (dead->kernel_stack != 0) {
            pmm_free_page(dead->kernel_stack);
        }
        pmm_free_page(VIRT_TO_PHYS((uint32_t)dead));
    }

    // Восстанавливаем EFLAGS (включаем прерывания, если они были включены)
    __asm__ volatile("push %0; popf" : : "r"(eflags));
}

void task_yield(void) { schedule(); }

// ============================================================================
// ЗАВЕРШЕНИЕ ЗАДАЧИ
// ============================================================================
void task_exit(void) {
    fpu_release_ownership(current_task);
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            sys_close(i); 
            current_task->fd_table[i] = 0; 
        }
    }

    current_task->state = TASK_DEAD;
    
    if (current_task->next != current_task) {
        current_task->prev->next = current_task->next;
        current_task->next->prev = current_task->prev;
    } else {
        current_task = 0; 
    }

    schedule();
    while(1) __asm__ volatile("cli; hlt"); 
}

// ============================================================================
// ОТЛАДКА (ps)
// ============================================================================
void task_print_list(void) {
    if (!current_task) {
        k_print("[PS] No tasks running.\n");
        return;
    }

    k_set_color(VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    k_print("\n  PID | STATE   | NAME\n");
    k_print("------+---------+----------------\n");
    
    task_t* t = current_task;
    do {
        const char* state_str = "UNKNOWN";
        uint8_t state_color = VGA_COLOR_LIGHT_GREY;
        
        switch (t->state) {
            case TASK_RUNNING: 
                state_str = "RUNNING"; 
                state_color = VGA_COLOR_LIGHT_GREEN; 
                break;
            case TASK_READY:   
                state_str = "READY";   
                state_color = VGA_COLOR_YELLOW; 
                break;
            case TASK_DEAD:    
                state_str = "DEAD";    
                state_color = VGA_COLOR_RED; 
                break;
        }
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_printf(" %3d  | ", t->pid);
        
        k_set_color(state_color, VGA_COLOR_BLACK);
        k_print(state_str);
        
        // Выравнивание пробелами (так как k_printf не поддерживает %s с шириной)
        int len = 0; while(state_str[len]) len++;
        for (int i = 0; i < 7 - len; i++) k_print(" ");
        
        k_set_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
        k_print(" | ");
        k_print(t->name);
        k_print("\n");
        
        t = t->next;
    } while (t != current_task);
    
    k_print("\n");
}
