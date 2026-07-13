#include "task.h"
#include "pmm.h"
#include "klib.h"
#include "serial.h"
#include "tss.h"
#include "paging.h"
#include "isr.h"
#include "vfs.h"
#include "vma.h"
#include "syscall.h"
#include "heap.h"
#include <stdbool.h>

extern void switch_context(uint32_t* old_esp, uint32_t new_esp, uint32_t new_cr3);
extern void ret_from_fork(void); // ✅ ASM-трамплин из context_switch.asm

task_t* current_task = 0;
uint32_t next_pid = 1;
static task_t* fpu_owner = 0;

static task_t* dead_tasks_head = NULL;

static uint32_t task_count = 0;
task_t* init_task = 0;
uint32_t task_get_count(void) { return task_count; }

// ============================================================================
// ТРАМПЛИН (Task Wrapper)
// ============================================================================
void task_entry_trampoline(void (*entry_point)(void), bool is_user_mode, uint32_t user_esp) {
    __asm__ volatile("sti"); 
    
    if (is_user_mode) {
        extern void enter_usermode(uint32_t entry_point, uint32_t user_esp);
        enter_usermode((uint32_t)entry_point, user_esp);
    } else {
        entry_point();
        task_exit(0);
    }
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

    __asm__ volatile("clts"); 

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
// УПРАВЛЕНИЕ ОЧЕРЕДЬЮ (RUN QUEUE)
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
    task_count = 0;
    
    uint32_t main_pcb_phys = pmm_alloc_page();
    if (main_pcb_phys == 0) {
        serial_print("[TASK] FATAL: OOM allocating main_task PCB!\n");
        while(1) __asm__("cli; hlt");
    }
    
    task_t* main_task_ptr = (task_t*)PHYS_TO_VIRT(main_pcb_phys);
    k_memset(main_task_ptr, 0, sizeof(task_t));
    
    main_task_ptr->pid = next_pid++;
    main_task_ptr->state = TASK_RUNNING;
    main_task_ptr->kernel_stack_virt = 0;
    main_task_ptr->sleep_until = 0;
    
    main_task_ptr->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);

    const char* name = "main";
    int i = 0; while(name[i] && i < 31) { main_task_ptr->name[i] = name[i]; i++; }
    main_task_ptr->name[i] = '\0';
    
    current_task = main_task_ptr;
    main_task_ptr->next = main_task_ptr;
    main_task_ptr->prev = main_task_ptr;
    main_task_ptr->reaper_next = NULL;
    main_task_ptr->parent = NULL;
    main_task_ptr->children = NULL;
    main_task_ptr->next_sibling = NULL;
    main_task_ptr->exit_code = 0;
    main_task_ptr->orphan_on_exit = 1;
    main_task_ptr->monitor_children = 0;
    
    init_task = main_task_ptr;

    task_count++;
    
    task_init_fds(main_task_ptr);
    
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); 
    cr0 |= (1 << 1);  
    cr0 |= (1 << 5);  
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    uint32_t cr4;
    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);  
    cr4 |= (1 << 10); 
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));
    
    isr_register_handler(7, device_not_available_handler);
    
    serial_print("[TASK] Main task registered.\n");
    serial_print("[TASK] FPU Lazy Switching enabled.\n");
}

// ============================================================================
// СОЗДАНИЕ НОВОЙ ЗАДАЧИ (С поддержкой готового Address Space)
// ============================================================================
task_t* task_create(const char* name, void (*entry_point)(void), 
                    bool is_user_mode, uint32_t user_esp, uint32_t* custom_pdir) {
    
    uint32_t stack_size = 16384; 
    uint32_t stack_virt = (uint32_t)kmalloc(stack_size);
    if (stack_virt == 0) return 0;

    uint32_t pcb_phys = pmm_alloc_page(); 
    if (pcb_phys == 0) {
        kfree((void*)stack_virt);
        return 0;
    }

    task_t* new_task = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(new_task, 0, sizeof(task_t));
    new_task->vma_head = NULL; 
    new_task->sleep_until = 0;

    new_task->pid = next_pid++;
    new_task->state = TASK_READY;
    new_task->kernel_stack_virt = stack_virt;
    new_task->reaper_next = NULL; 
    
    if (is_user_mode) {
        if (custom_pdir) {
            new_task->pdir_virt = custom_pdir;
            new_task->cr3 = VIRT_TO_PHYS((uint32_t)custom_pdir);
        } else {
            new_task->pdir_virt = vmm_create_address_space();
            if (!new_task->pdir_virt) {
                serial_print("[TASK] OOM: Failed to create Address Space!\n");
                kfree((void*)stack_virt);
                pmm_free_page(pcb_phys);
                return 0;
            }
            new_task->cr3 = VIRT_TO_PHYS((uint32_t)new_task->pdir_virt);
        }
    } else {
        new_task->pdir_virt = boot_page_directory;
        new_task->cr3 = VIRT_TO_PHYS((uint32_t)boot_page_directory);
    }

    int i = 0; while(name[i] && i < 31) { new_task->name[i] = name[i]; i++; }
    new_task->name[i] = '\0';

    uint32_t* stack_top = (uint32_t*)(stack_virt + stack_size);
    
    *(--stack_top) = user_esp;                      
    *(--stack_top) = (uint32_t)is_user_mode;        
    *(--stack_top) = (uint32_t)entry_point;         
    *(--stack_top) = (uint32_t)task_exit;           
    
    *(--stack_top) = (uint32_t)task_entry_trampoline; 
    *(--stack_top) = 0; // EBX
    *(--stack_top) = 0; // ESI
    *(--stack_top) = 0; // EDI
    *(--stack_top) = 0; // EBP
    
    new_task->esp = (uint32_t)stack_top;

    for (int j = 0; j < TASK_MAX_OPEN_FILES; j++) new_task->fd_table[j] = 0;
    task_init_fds(new_task); 
    
    new_task->parent = current_task;
    new_task->children = NULL;
    new_task->next_sibling = NULL;
    new_task->exit_code = 0;
    new_task->orphan_on_exit = 1;
    new_task->monitor_children = 0;
    
    if (current_task) {
        new_task->next_sibling = current_task->children;
        current_task->children = new_task;
    }
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    task_queue_add(new_task);
    task_count++;
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    serial_printf("[TASK] Created PID %d: %s (mode: %s)\n", 
                  new_task->pid, name, is_user_mode ? "USER" : "KERNEL");
    return new_task;
}

// ============================================================================
// ПЛАНИРОВЩИК (ROUND-ROBIN + SLEEP-AWARE + IDLE HLT)
// ============================================================================
void schedule(void) {
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));

    if (!current_task || !current_task->next) {
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return;
    }
    
    task_t* old_task = current_task;
    task_t* new_task = current_task->next;
    
    while (new_task != old_task && 
           (new_task->state == TASK_SLEEPING || 
            new_task->state == TASK_ZOMBIE || 
            new_task->state == TASK_DEAD)) {
        new_task = new_task->next;
    }
    
    if (old_task == new_task) {
        if (old_task->state != TASK_RUNNING) {
            __asm__ volatile("sti; hlt; cli");
        }
        __asm__ volatile("push %0; popf" : : "r"(eflags));
        return; 
    }

    if (old_task->state == TASK_RUNNING) old_task->state = TASK_READY;
    new_task->state = TASK_RUNNING;
    current_task = new_task;

    if (new_task->kernel_stack_virt != 0) {
        tss_set_kernel_stack(0x10, new_task->kernel_stack_virt + 16384);
    }

    switch_context(&old_task->esp, new_task->esp, new_task->cr3);
    
    // --- ЗДЕСЬ ПРОДОЛЖАЕТ ВЫПОЛНЕНИЕ УЖЕ НОВАЯ ЗАДАЧА ---

    if (dead_tasks_head != NULL) {
        uint32_t reap_flags;
        __asm__ volatile("pushf; pop %0; cli" : "=r"(reap_flags));
        
        while (dead_tasks_head != NULL) {
            task_t* dead = dead_tasks_head;
            dead_tasks_head = dead->reaper_next; 
        
            serial_printf("[REAPER] Cleaning up PID %d (%s)\n", dead->pid, dead->name);
        
            vma_destroy_all(dead);
            if (dead->pdir_virt && dead->pdir_virt != boot_page_directory) {
                vmm_destroy_address_space(dead->pdir_virt);
            }
            
            if (dead->kernel_stack_virt != 0) {
                kfree((void*)dead->kernel_stack_virt);
            }
            
            pmm_free_page(VIRT_TO_PHYS((uint32_t)dead));
            task_count--;
        }
        __asm__ volatile("push %0; popf" : : "r"(reap_flags));
    }

    __asm__ volatile("push %0; popf" : : "r"(eflags));
}

void task_yield(void) { schedule(); }

// ============================================================================
// ЗАВЕРШЕНИЕ ЗАДАЧИ (Normal Exit)
// ============================================================================
void task_exit(int exit_code) {
    fpu_release_ownership(current_task);
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            sys_close(i); 
            current_task->fd_table[i] = 0; 
        }
    }
    
    if (current_task->orphan_on_exit) {
        while (current_task->children != NULL) {
            task_t* child = current_task->children;
            current_task->children = child->next_sibling;
            
            child->parent = init_task;
            child->next_sibling = init_task->children;
            init_task->children = child;
            
            serial_printf("[TASK] PID %d adopted by Init Task (PID 1)\n", child->pid);
        }
    } else if (current_task->monitor_children) {
        task_t* child = current_task->children;
        while (child != NULL) {
            task_t* next = child->next_sibling;
            if (child->state != TASK_ZOMBIE && child->state != TASK_DEAD) {
                serial_printf("[TASK] Killing child PID %d (Supervisor Tree)\n", child->pid);
                child->state = TASK_ZOMBIE;
                child->exit_code = -1;
                
                if (child->next != child) {
                    child->prev->next = child->next;
                    child->next->prev = child->prev;
                }
                
                child->reaper_next = dead_tasks_head;
                dead_tasks_head = child;
            }
            child = next;
        }
        current_task->children = NULL;
    }
    
    current_task->state = TASK_ZOMBIE;
    current_task->exit_code = exit_code;
    
    if (current_task->parent && current_task->parent->state == TASK_SLEEPING) {
        current_task->parent->state = TASK_READY;
        current_task->parent->sleep_until = 0;
        serial_printf("[TASK] Waking up parent PID %d\n", current_task->parent->pid);
    }
    
    vma_destroy_all(current_task);
    if (current_task->pdir_virt && current_task->pdir_virt != boot_page_directory) {
        vmm_destroy_address_space(current_task->pdir_virt);
        current_task->pdir_virt = NULL;
    }
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* dead_task = current_task; 

    if (dead_task->next != dead_task) {
        dead_task->prev->next = dead_task->next;
        dead_task->next->prev = dead_task->prev;
    }
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));

    schedule(); 
    
    while(1) __asm__ volatile("cli; hlt"); 
}

// ============================================================================
// [ДЕНЬ 10] ПРИНУДИТЕЛЬНОЕ УБИЙСТВО (Page Fault / OOM Killer)
// ============================================================================
void task_kill_current(const char* reason) {
    if (!current_task || current_task->pid == 0) {
        serial_printf("[KILL] FATAL: Attempt to kill invalid task: %s\n", reason);
        while(1) __asm__ volatile("cli; hlt");
    }
    
    serial_printf("[KILL] PID %d (%s) killed: %s\n", current_task->pid, current_task->name, reason);
    
    fpu_release_ownership(current_task);
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        if (current_task->fd_table[i] != 0) {
            sys_close(i);
            current_task->fd_table[i] = 0;
        }
    }
    
    current_task->state = TASK_DEAD;
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    
    task_t* dead_task = current_task;

    if (dead_task->next != dead_task) {
        dead_task->prev->next = dead_task->next;
        dead_task->next->prev = dead_task->prev;
    }
    
    dead_task->reaper_next = dead_tasks_head; 
    dead_tasks_head = dead_task;       
    
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    schedule();
    while(1) __asm__ volatile("cli; hlt"); 
}

// ============================================================================
// [ДЕНЬ 14] TASK FORK (Copy-on-Write) - PRODUCTION READY
// ============================================================================
extern void ret_from_fork(void);

int task_fork(struct regs* r) {
    uint32_t stack_size = 16384;
    uint32_t stack_virt = (uint32_t)kmalloc(stack_size);
    if (stack_virt == 0) return -ENOMEM;
    
    uint32_t pcb_phys = pmm_alloc_page();
    if (pcb_phys == 0) {
        kfree((void*)stack_virt);
        return -ENOMEM;
    }
    
    task_t* child = (task_t*)PHYS_TO_VIRT(pcb_phys);
    k_memset(child, 0, sizeof(task_t));
    
    child->pid = next_pid++;
    child->state = TASK_READY;
    child->kernel_stack_virt = stack_virt;
    child->reaper_next = NULL;
    child->sleep_until = 0;
    
    child->pdir_virt = vmm_clone_address_space(current_task->pdir_virt);
    if (!child->pdir_virt) {
        kfree((void*)stack_virt);
        pmm_free_page(pcb_phys);
        return -ENOMEM;
    }
    child->cr3 = VIRT_TO_PHYS((uint32_t)child->pdir_virt);
    
    if (vma_clone(child, current_task) < 0) {
        vmm_destroy_address_space(child->pdir_virt);
        kfree((void*)stack_virt);
        pmm_free_page(pcb_phys);
        return -ENOMEM;
    }
    
    k_strncpy(child->name, current_task->name, sizeof(child->name));
    
    for (int i = 0; i < TASK_MAX_OPEN_FILES; i++) {
        child->fd_table[i] = current_task->fd_table[i];
    }
    
    k_memcpy(child->fpu_state, current_task->fpu_state, 512);
    child->fpu_initialized = current_task->fpu_initialized;
    
    child->parent = current_task;
    child->children = NULL;
    child->next_sibling = current_task->children;
    current_task->children = child;
    child->exit_code = 0;
    child->orphan_on_exit = current_task->orphan_on_exit;
    child->monitor_children = current_task->monitor_children;
    
    // ========================================================================
    // 🛡️ BULLETPROOF KERNEL STACK COPYING & FORGING
    // ========================================================================
    k_memcpy((void*)(stack_virt), (void*)(current_task->kernel_stack_virt), stack_size);
    
    uint32_t offset_from_base = (uint32_t)r - current_task->kernel_stack_virt;
    struct regs* original_child_r = (struct regs*)(stack_virt + offset_from_base);
    
    // Сдвигаем ISR frame вниз на 32 байта, освобождая место для фейкового фрейма
    uint32_t shift = 32;
    struct regs* child_r = (struct regs*)((uint32_t)original_child_r - shift);
    k_memcpy(child_r, original_child_r, sizeof(struct regs));
    
    child_r->eax = 0; // POSIX: ребенок видит 0
    
    uint32_t* child_stack_top = (uint32_t*)child_r;
    
    *(--child_stack_top) = (uint32_t)child_r;
    *(--child_stack_top) = (uint32_t)ret_from_fork;
    *(--child_stack_top) = 0; // EBX
    *(--child_stack_top) = 0; // ESI
    *(--child_stack_top) = 0; // EDI
    *(--child_stack_top) = 0; // EBP
    
    child->esp = (uint32_t)child_stack_top;
    // ========================================================================
    
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
    task_queue_add(child);
    task_count++;
    __asm__ volatile("push %0; popf" : : "r"(eflags));
    
    serial_printf("[TASK] Fork: PID %d -> PID %d (CoW)\n", current_task->pid, child->pid);
    
    return child->pid;
}

// ============================================================================
// [ДЕНЬ 14] TASK WAITPID
// ============================================================================
int task_waitpid(int pid, int* status, int options) {
    while (1) {
        task_t* child = current_task->children;
        task_t* zombie = NULL;
        
        while (child != NULL) {
            if (child->state == TASK_ZOMBIE) {
                if (pid == -1 || child->pid == (uint32_t)pid) {
                    zombie = child;
                    break;
                }
            }
            child = child->next_sibling;
        }
        
        if (zombie) {
            int exit_code = zombie->exit_code;
            
            if (status) {
                *status = exit_code;
            }
            
            uint32_t zombie_pid = zombie->pid;
            
            if (current_task->children == zombie) {
                current_task->children = zombie->next_sibling;
            } else {
                task_t* sibling = current_task->children;
                while (sibling && sibling->next_sibling != zombie) {
                    sibling = sibling->next_sibling;
                }
                if (sibling) {
                    sibling->next_sibling = zombie->next_sibling;
                }
            }
            
            zombie->state = TASK_DEAD;
            
            uint32_t eflags;
            __asm__ volatile("pushf; pop %0; cli" : "=r"(eflags));
            
            zombie->reaper_next = dead_tasks_head;
            dead_tasks_head = zombie;
            
            __asm__ volatile("push %0; popf" : : "r"(eflags));
            
            serial_printf("[TASK] waitpid: Reaped PID %d (exit code: %d)\n", zombie_pid, exit_code);
            
            return zombie_pid;
        }
        
        if (options & WNOHANG) {
            return 0;
        }
        
        current_task->state = TASK_SLEEPING;
        current_task->sleep_until = 0;
        serial_printf("[TASK] PID %d sleeping in waitpid\n", current_task->pid);
        schedule();
    }
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
            case TASK_SLEEPING:
                state_str = "SLEEPING";
                state_color = VGA_COLOR_LIGHT_BLUE;
                break;
            case TASK_ZOMBIE:
                state_str = "ZOMBIE";
                state_color = VGA_COLOR_MAGENTA;
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