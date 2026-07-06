#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"
#include "isr.h"
#include "framebuffer.h"
#include "vga.h"

extern void switch_context(uint32_t* old_esp, uint32_t new_esp, uint32_t new_cr3);

task_t* current_task = 0;
uint32_t next_pid = 1;
static task_t* fpu_owner = 0;

void task_entry_trampoline(void (*entry_point)(void)) {
    __asm__ volatile("sti");
    entry_point();
    task_exit();
}

static volatile int in_nm_handler = 0;

static void device_not_available_handler(struct regs* r) {
    (void)r;
    
    if (in_nm_handler) {
        serial_print("\n[FPU] FATAL: Recursive #NM detected!\n");
        while(1) __asm__("hlt");
    }
    in_nm_handler = 1;

    // ✅ КРИТИЧЕСКОЕ ИСПРАВЛЕНИЕ (THE FXSAVE TRAP):
    // Инструкции fxsave и fxrstor сами по себе являются FPU-инструкциями.
    // Если бит CR0.TS (Task Switched) установлен, процессор аппаратно 
    // сгенерирует НОВОЕ исключение #NM при попытке выполнить fxsave/fxrstor!
    // Это и была причина бесконечной рекурсии и Triple Fault.
    // Мы ОБЯЗАНЫ снять бит TS (инструкцией clts) ДО любых операций с FPU.
    __asm__ volatile("clts");

    // Self-Healing: Гарантируем, что CR4.OSFXSR установлен
    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    if (!(cr4 & (1 << 9))) {
        cr4 |= (1 << 9) | (1 << 10);
        __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    }

    // 1. Сохраняем состояние FPU предыдущего владельца
    if (fpu_owner && fpu_owner != current_task) {
        __asm__ volatile("fxsave (%0)" : : "r"(fpu_owner->fpu_state) : "memory");
    }
    
    // 2. Восстанавливаем состояние FPU для ТЕКУЩЕЙ задачи
    if (current_task->fpu_initialized) {
        __asm__ volatile("fxrstor (%0)" : : "r"(current_task->fpu_state) : "memory");
    } else {
        __asm__ volatile("fninit");
        __asm__ volatile("fxsave (%0)" : : "r"(current_task->fpu_state) : "memory");
        current_task->fpu_initialized = 1;
    }
    
    // 3. Обновляем владельца (TS уже сброшен, задача может продолжать работу)
    fpu_owner = current_task;
    
    in_nm_handler = 0; 
}

void fpu_release_ownership(task_t* task) {
    if (fpu_owner == task) fpu_owner = 0;
}

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

void tasking_init(void) {
    serial_print("[TASK] Initializing Task Manager...\n");
    
    // Динамическая аллокация main_task для идеального выравнивания (кратно 4096 -> кратно 16)
    uint32_t main_pcb_phys = pmm_alloc_page();
    if (main_pcb_phys == 0) {
        serial_print("[TASK] FATAL: OOM allocating main_task PCB!\n");
        while(1) __asm__("hlt");
    }
    
    task_t* main_task_ptr = (task_t*)PHYS_TO_VIRT(main_pcb_phys);
    k_memset(main_task_ptr, 0, sizeof(task_t));
    
    main_task_ptr->pid = next_pid++;
    main_task_ptr->state = TASK_RUNNING;
    main_task_ptr->kernel_stack = 0; 
    
    main_task_ptr->pdir_virt = boot_page_directory;
    main_task_ptr->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);

    const char* name = "main";
    int i = 0; while(name[i] && i < 31) { main_task_ptr->name[i] = name[i]; i++; }
    main_task_ptr->name[i] = '\0';
    
    current_task = main_task_ptr;
    main_task_ptr->next = main_task_ptr;
    main_task_ptr->prev = main_task_ptr;
    
    // Настройка CR0 и CR4 для FPU
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
    
    serial_print("[TASK] Main task registered (Dynamically Aligned).\n");
    serial_print("[TASK] FPU Lazy Switching enabled (INT 7 handler).\n");
}

task_t* task_create(const char* name, void (*entry_point)(void)) {
    uint32_t stack_phys = pmm_alloc_page();
    if (stack_phys == 0) return 0;
    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) return 0;

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

    uint32_t* stack_top = (uint32_t*)PHYS_TO_VIRT(stack_phys + 4096);
    *(--stack_top) = (uint32_t)entry_point; 
    *(--stack_top) = (uint32_t)task_exit; 
    *(--stack_top) = (uint32_t)task_entry_trampoline; 
    *(--stack_top) = 0; 
    *(--stack_top) = 0; 
    *(--stack_top) = 0; 
    *(--stack_top) = 0; 
    new_task->esp = (uint32_t)stack_top;

    task_queue_add(new_task);
    serial_print("[TASK] Created new task with isolated memory: ");
    serial_print(name); serial_print("\n");

    return new_task;
}

void schedule(void) {
    if (!current_task || !current_task->next) return;
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    if (old_task == new_task) return; 

    if (old_task->state == TASK_RUNNING) old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    if (new_task->kernel_stack != 0) {
        tss_set_kernel_stack(0x10, new_task->kernel_stack + 4096);
    }

    switch_context(&old_task->esp, new_task->esp, new_task->cr3);
}

void task_yield(void) { schedule(); }

void task_exit(void) {
    fpu_release_ownership(current_task);
    current_task->state = TASK_DEAD;
    
    if (current_task->next != current_task) {
        current_task->prev->next = current_task->next;
        current_task->next->prev = current_task->prev;
    } else {
        current_task = 0; 
    }

    schedule();
    while(1) __asm__ volatile("hlt");
}
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